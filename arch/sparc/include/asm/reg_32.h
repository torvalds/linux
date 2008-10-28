/*
 * linux/include/asm/reg.h
 * Layout of the registers as expected by gdb on the Sparc
 * we should replace the user.h definitions with those in
 * this file, we don't even use the other
 * -miguel
 *
 * The names of the structures, constants and aliases in this file
 * have the same names as the sunos ones, some programs rely on these
 * names (gdb for example).
 *
 */

#ifndef __SPARC_REG_H
#define __SPARC_REG_H

struct regs {
	int     r_psr;
#define r_ps r_psr
        int     r_pc;
        int     r_npc;
        int     r_y;
        int     r_g1;
        int     r_g2;
        int     r_g3;
        int     r_g4;
        int     r_g5;
        int     r_g6;
        int     r_g7;
        int     r_o0;
        int     r_o1;
        int     r_o2;
        int     r_o3;
        int     r_o4;
        int     r_o5;
        int     r_o6;
        int     r_o7;
};

struct fpq {
        unsigned long *addr;
        unsigned long instr;
};

struct  fq {
        union {
                double  whole;
                struct  fpq fpq;
        } FQu;
};

#define FPU_REGS_TYPE unsigned int
#define FPU_FSR_TYPE unsigned

struct fp_status {
        union {
                FPU_REGS_TYPE Fpu_regs[32];
                double  Fpu_dregs[16];
        } fpu_fr;
        FPU_FSR_TYPE Fpu_fsr;
        unsigned Fpu_flags;
        unsigned Fpu_extra;
        unsigned Fpu_qcnt;
        struct fq Fpu_q[16];
};

#define fpu_regs  f_fpstatus.fpu_fr.Fpu_regs
#define fpu_dregs f_fpstatus.fpu_fr.Fpu_dregs
#define fpu_fsr   f_fpstatus.Fpu_fsr
#define fpu_flags f_fpstatus.Fpu_flags
#define fpu_extra f_fpstatus.Fpu_extra
#define fpu_q     f_fpstatus.Fpu_q
#define fpu_qcnt  f_fpstatus.Fpu_qcnt

struct fpu {
        struct fp_status f_fpstatus;
};

#endif /* __SPARC_REG_H */
