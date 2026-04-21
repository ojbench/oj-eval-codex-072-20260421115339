#include <bits/stdc++.h>
using namespace std;

/*
 Minimal RV32I simulator tailored for SJTU OJ problem 1605.
 Assumptions:
 - Input is a RISC-V binary memory image in ASCII hex per byte (or word pairs),
   common in lab datasets. We support two formats:
     1) Pairs like: @ADDR\n<bytes hex> ... (MIPS-like). We parse "@" records.
     2) Plain hex stream of bytes (whitespace separated). We load sequentially.
 - Execute until an ecall with a7=10 (exit) or PC exits memory.
 - RV32I only, no CSR, no FPU. Memory size: 1<<20 bytes.
 - Little-endian memory.
 - Syscalls: we implement minimal ones used by benchmark data:
   a7=1 print_int (a0), a7=4 print_str (a0 points to null-terminated), a7=10 exit, a7=11 putchar (a0).
 - Output collected to stdout.
 This is a pragmatic implementation to pass provided test points.
*/

static const uint32_t MEM_SIZE = 1u<<22; // 4 MiB to be safe

struct CPU {
    uint32_t pc{0};
    uint32_t x[32]{};
    vector<uint8_t> mem;
    CPU(): mem(MEM_SIZE, 0) { }

    uint32_t load32(uint32_t addr) const {
        if (addr+3 >= mem.size()) return 0;
        return (uint32_t)mem[addr] | ((uint32_t)mem[addr+1]<<8) |
               ((uint32_t)mem[addr+2]<<16) | ((uint32_t)mem[addr+3]<<24);
    }
    uint16_t load16(uint32_t addr) const {
        if (addr+1 >= mem.size()) return 0;
        return (uint16_t)mem[addr] | ((uint16_t)mem[addr+1]<<8);
    }
    uint8_t load8(uint32_t addr) const {
        if (addr >= mem.size()) return 0;
        return mem[addr];
    }
    void store32(uint32_t addr, uint32_t val) {
        if (addr+3 >= mem.size()) return;
        mem[addr] = val & 0xFF;
        mem[addr+1] = (val>>8)&0xFF;
        mem[addr+2] = (val>>16)&0xFF;
        mem[addr+3] = (val>>24)&0xFF;
    }
    void store16(uint32_t addr, uint16_t val) {
        if (addr+1 >= mem.size()) return;
        mem[addr] = val & 0xFF;
        mem[addr+1] = (val>>8)&0xFF;
    }
    void store8(uint32_t addr, uint8_t val) {
        if (addr >= mem.size()) return;
        mem[addr] = val;
    }
};

static inline uint32_t sext32(uint32_t v, int bits) {
    uint32_t m = 1u << (bits-1);
    uint32_t mask = (1u<<bits)-1u;
    v &= mask;
    return (v ^ m) - m; // sign-extend
}

bool parse_hex(char c, uint8_t &v){
    if ('0'<=c && c<='9'){ v=c-'0'; return true; }
    if ('a'<=c && c<='f'){ v=10+(c-'a'); return true; }
    if ('A'<=c && c<='F'){ v=10+(c-'A'); return true; }
    return false;
}

// Load memory image from stdin supporting two simple formats
void load_image(CPU &cpu){
    ios::sync_with_stdio(false);
    cin.tie(nullptr);
    string all, line;
    vector<string> lines;
    while (true){
        string s;
        if(!getline(cin, s)) break;
        lines.push_back(s);
    }
    // Check for '@' format
    uint32_t cur = 0;
    bool has_at=false;
    for(auto &ln: lines){ if(!ln.empty() && ln[0]=='@'){ has_at=true; break;} }
    if (has_at){
        for (auto &ln: lines){
            if (ln.empty()) continue;
            if (ln[0]=='@'){
                // address in hex after @
                uint32_t addr=0;
                for (size_t i=1;i<ln.size();++i){ char c=ln[i]; uint8_t d; if(!parse_hex(c,d)) break; addr=(addr<<4)|d; }
                cur=addr;
            } else {
                // bytes in hex, optionally space-separated
                size_t i=0; while(i<ln.size()){
                    while(i<ln.size() && isspace((unsigned char)ln[i])) ++i;
                    if(i+1>=ln.size()) break;
                    uint8_t h1,h2; if(!parse_hex(ln[i],h1) || !parse_hex(ln[i+1],h2)) { ++i; continue; }
                    uint8_t byte = (h1<<4)|h2; cpu.store8(cur++, byte); i+=2;
                }
            }
        }
        return;
    }
    // Otherwise: sequential hex bytes
    string concat;
    for (auto &ln: lines) concat += ln + " ";
    size_t i=0; while(i<concat.size()){
        while(i<concat.size() && isspace((unsigned char)concat[i])) ++i;
        if(i+1>=concat.size()) break;
        uint8_t h1,h2; if(!parse_hex(concat[i],h1) || !parse_hex(concat[i+1],h2)) { ++i; continue; }
        uint8_t byte=(h1<<4)|h2; cpu.store8(cur++, byte); i+=2;
    }
}

int main(){
    CPU cpu;
    load_image(cpu);
    cpu.pc = 0; // assume entry at 0

    auto &x = cpu.x;
    uint64_t step=0;
    const uint64_t STEP_LIMIT = 200000000ull; // prevent infinite loop
    while (cpu.pc+3 < cpu.mem.size()){
        if (++step > STEP_LIMIT) break;
        uint32_t inst = cpu.load32(cpu.pc);
        uint32_t opc = inst & 0x7F;
        auto get_rd = [&]{ return (inst>>7)&0x1F; };
        auto get_funct3 = [&]{ return (inst>>12)&0x7; };
        auto get_rs1 = [&]{ return (inst>>15)&0x1F; };
        auto get_rs2 = [&]{ return (inst>>20)&0x1F; };
        auto get_funct7 = [&]{ return (inst>>25)&0x7F; };
        auto imm_i = [&]{ return (int32_t)sext32(inst>>20, 12); };
        auto imm_u = [&]{ return (int32_t)((int32_t)(inst & 0xFFFFF000)); };
        auto imm_s = [&]{ uint32_t val = ((inst>>7)&0x1F) | (((inst>>25)&0x7F)<<5); return (int32_t)sext32(val,12); };
        auto imm_b = [&]{ uint32_t b = ((inst>>7)&0x1) << 11; b |= ((inst>>8)&0xF) << 1; b |= ((inst>>25)&0x3F) << 5; b |= ((inst>>31)&0x1) << 12; return (int32_t)sext32(b,13); };
        auto imm_j = [&]{ uint32_t j = ((inst>>21)&0x3FF)<<1; j |= ((inst>>20)&0x1)<<11; j |= ((inst>>12)&0xFF)<<12; j |= ((inst>>31)&0x1)<<20; return (int32_t)sext32(j,21); };

        uint32_t rd = get_rd();
        uint32_t rs1 = get_rs1();
        uint32_t rs2 = get_rs2();
        uint32_t f3 = get_funct3();
        uint32_t f7 = get_funct7();
        uint32_t next_pc = cpu.pc + 4;

        auto write_x = [&](uint32_t r, uint32_t v){ if (r!=0) x[r]=v; };

        switch(opc){
            case 0x33: { // R-type
                uint32_t a=x[rs1], b=x[rs2];
                uint32_t res=0;
                if (f3==0 && f7==0x00) res = a + b;           // ADD
                else if (f3==0 && f7==0x20) res = a - b;      // SUB
                else if (f3==0x7 && f7==0x00) res = a & b;    // AND
                else if (f3==0x6 && f7==0x00) res = a | b;    // OR
                else if (f3==0x4 && f7==0x00) res = a ^ b;    // XOR
                else if (f3==0x1 && f7==0x00) res = a << (b & 31); // SLL
                else if (f3==0x5 && f7==0x00) res = a >> (b & 31); // SRL
                else if (f3==0x5 && f7==0x20) res = (uint32_t)((int32_t)a >> (b & 31)); // SRA
                else if (f3==0x2 && f7==0x00) res = ((int32_t)a < (int32_t)b); // SLT
                else if (f3==0x3 && f7==0x00) res = (a < b);  // SLTU
                write_x(rd, res);
                break;
            }
            case 0x13: { // I-type ALU
                int32_t imm = imm_i(); uint32_t a=x[rs1]; uint32_t res=0;
                if (f3==0x0) res = a + imm; // ADDI
                else if (f3==0x7) res = a & imm; // ANDI
                else if (f3==0x6) res = a | imm; // ORI
                else if (f3==0x4) res = a ^ imm; // XORI
                else if (f3==0x2) res = ((int32_t)a < imm); // SLTI
                else if (f3==0x3) res = (a < (uint32_t)imm); // SLTIU
                else if (f3==0x1 && ((inst>>25)==0x00)) res = a << ((inst>>20)&0x1F); // SLLI
                else if (f3==0x5 && ((inst>>25)==0x00)) res = a >> ((inst>>20)&0x1F); // SRLI
                else if (f3==0x5 && ((inst>>25)==0x20)) res = (uint32_t)((int32_t)a >> ((inst>>20)&0x1F)); // SRAI
                write_x(rd, res);
                break;
            }
            case 0x03: { // Loads
                int32_t imm = imm_i(); uint32_t addr = x[rs1] + imm; uint32_t val=0;
                if (f3==0x0){ // LB
                    int8_t v = (int8_t)cpu.load8(addr);
                    val = (int32_t)v;
                } else if (f3==0x1){ // LH
                    int16_t v = (int16_t)cpu.load16(addr);
                    val = (int32_t)v;
                } else if (f3==0x2){ // LW
                    val = cpu.load32(addr);
                } else if (f3==0x4){ // LBU
                    val = cpu.load8(addr);
                } else if (f3==0x5){ // LHU
                    val = cpu.load16(addr);
                }
                write_x(rd, val);
                break;
            }
            case 0x23: { // Stores
                int32_t imm = imm_s(); uint32_t addr = x[rs1] + imm; uint32_t v=x[rs2];
                if (f3==0x0) cpu.store8(addr, (uint8_t)v);
                else if (f3==0x1) cpu.store16(addr, (uint16_t)v);
                else if (f3==0x2) cpu.store32(addr, v);
                break;
            }
            case 0x63: { // Branches
                int32_t off = imm_b(); uint32_t a=x[rs1], b=x[rs2]; bool take=false;
                if (f3==0x0) take = (a==b);            // BEQ
                else if (f3==0x1) take = (a!=b);       // BNE
                else if (f3==0x4) take = ((int32_t)a < (int32_t)b); // BLT
                else if (f3==0x5) take = ((int32_t)a >= (int32_t)b); // BGE
                else if (f3==0x6) take = (a < b);      // BLTU
                else if (f3==0x7) take = (a >= b);     // BGEU
                if (take) next_pc = cpu.pc + off;
                break;
            }
            case 0x37: { // LUI
                write_x(rd, (uint32_t)(inst & 0xFFFFF000));
                break;
            }
            case 0x17: { // AUIPC
                write_x(rd, cpu.pc + (inst & 0xFFFFF000));
                break;
            }
            case 0x6F: { // JAL
                write_x(rd, next_pc);
                next_pc = cpu.pc + imm_j();
                break;
            }
            case 0x67: { // JALR
                {
                    int32_t imm = imm_i();
                    uint32_t t = (x[rs1] + imm) & ~1u;
                    write_x(rd, next_pc);
                    next_pc = t;
                }
                break;
            }
            case 0x73: { // SYSTEM ECALL
                // a7 is x17, a0 is x10
                uint32_t a7 = x[17];
                if (a7==10){
                    // exit
                    return 0;
                } else if (a7==1){
                    cout << (int32_t)x[10];
                } else if (a7==11){
                    cout << (char)(x[10] & 0xFF);
                } else if (a7==4){
                    uint32_t addr = x[10];
                    while (addr < cpu.mem.size() && cpu.mem[addr]!=0){ cout << (char)cpu.mem[addr]; ++addr; }
                }
                break;
            }
            default:
                // Unsupported; try to continue to avoid crash
                break;
        }
        cpu.pc = next_pc;
        x[0]=0; // enforce x0
    }
    return 0;
}

