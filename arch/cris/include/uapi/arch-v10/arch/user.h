#ifndef __ASM_CRIS_ARCH_USER_H
#define __ASM_CRIS_ARCH_USER_H

/* User mode registers, used for core dumps. In order to keep ELF_NGREG
   sensible we let all registers be 32 bits. The csr registers are included
   for future use. */
struct user_regs_struct {
        unsigned long r0;       /* General registers. */
        unsigned long r1;
        unsigned long r2;
        unsigned long r3;
        unsigned long r4;
        unsigned long r5;
        unsigned long r6;
        unsigned long r7;
        unsigned long r8;
        unsigned long r9;
        unsigned long r10;
        unsigned long r11;
        unsigned long r12;
        unsigned long r13;
        unsigned long sp;       /* Stack pointer. */
        unsigned long pc;       /* Program counter. */
        unsigned long p0;       /* Constant zero (only 8 bits). */
        unsigned long vr;       /* Version register (only 8 bits). */
        unsigned long p2;       /* Reserved. */
        unsigned long p3;       /* Reserved. */
        unsigned long p4;       /* Constant zero (only 16 bits). */
        unsigned long ccr;      /* Condition code register (only 16 bits). */
        unsigned long p6;       /* Reserved. */
        unsigned long mof;      /* Multiply overflow register. */
        unsigned long p8;       /* Constant zero. */
        unsigned long ibr;      /* Not accessible. */
        unsigned long irp;      /* Not accessible. */
        unsigned long srp;      /* Subroutine return pointer. */
        unsigned long bar;      /* Not accessible. */
        unsigned long dccr;     /* Dword condition code register. */
        unsigned long brp;      /* Not accessible. */
        unsigned long usp;      /* User-mode stack pointer. Same as sp when 
                                   in user mode. */
        unsigned long csrinstr; /* Internal status registers. */
        unsigned long csraddr;
        unsigned long csrdata;
};

#endif
