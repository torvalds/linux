#ifndef _ASM_CRIS_ELF_H
#define _ASM_CRIS_ELF_H

#define ELF_CORE_EFLAGS EF_CRIS_VARIANT_V32

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch(x)			\
 ((x)->e_machine == EM_CRIS			\
  && ((((x)->e_flags & EF_CRIS_VARIANT_MASK) == EF_CRIS_VARIANT_V32	\
      || (((x)->e_flags & EF_CRIS_VARIANT_MASK) == EF_CRIS_VARIANT_COMMON_V10_V32))))

/* CRISv32 ELF register definitions. */

#include <asm/ptrace.h>

/* Explicitly zero out registers to increase determinism. */
#define ELF_PLAT_INIT(_r, load_addr)    do { \
        (_r)->r13 = 0; (_r)->r12 = 0; (_r)->r11 = 0; (_r)->r10 = 0; \
        (_r)->r9 = 0;  (_r)->r8 = 0;  (_r)->r7 = 0;  (_r)->r6 = 0;  \
        (_r)->r5 = 0;  (_r)->r4 = 0;  (_r)->r3 = 0;  (_r)->r2 = 0;  \
        (_r)->r1 = 0;  (_r)->r0 = 0;  (_r)->mof = 0; (_r)->srp = 0; \
        (_r)->acr = 0; \
} while (0)

/*
 * An executable for which elf_read_implies_exec() returns TRUE will
 * have the READ_IMPLIES_EXEC personality flag set automatically.
 */
#define elf_read_implies_exec_binary(ex, have_pt_gnu_stack)	(!(have_pt_gnu_stack))

/*
 * This is basically a pt_regs with the additional definition
 * of the stack pointer since it's needed in a core dump.
 * pr_regs is a elf_gregset_t and should be filled according
 * to the layout of user_regs_struct.
 */
#define ELF_CORE_COPY_REGS(pr_reg, regs)                   \
        pr_reg[0] = regs->r0;                              \
        pr_reg[1] = regs->r1;                              \
        pr_reg[2] = regs->r2;                              \
        pr_reg[3] = regs->r3;                              \
        pr_reg[4] = regs->r4;                              \
        pr_reg[5] = regs->r5;                              \
        pr_reg[6] = regs->r6;                              \
        pr_reg[7] = regs->r7;                              \
        pr_reg[8] = regs->r8;                              \
        pr_reg[9] = regs->r9;                              \
        pr_reg[10] = regs->r10;                            \
        pr_reg[11] = regs->r11;                            \
        pr_reg[12] = regs->r12;                            \
        pr_reg[13] = regs->r13;                            \
        pr_reg[14] = rdusp();               /* SP */       \
        pr_reg[15] = regs->acr;             /* ACR */      \
        pr_reg[16] = 0;                     /* BZ */       \
        pr_reg[17] = rdvr();                /* VR */       \
        pr_reg[18] = 0;                     /* PID */      \
        pr_reg[19] = regs->srs;             /* SRS */      \
        pr_reg[20] = 0;                     /* WZ */       \
        pr_reg[21] = regs->exs;             /* EXS */      \
        pr_reg[22] = regs->eda;             /* EDA */      \
        pr_reg[23] = regs->mof;             /* MOF */      \
        pr_reg[24] = 0;                     /* DZ */       \
        pr_reg[25] = 0;                     /* EBP */      \
        pr_reg[26] = regs->erp;             /* ERP */      \
        pr_reg[27] = regs->srp;             /* SRP */      \
        pr_reg[28] = 0;                     /* NRP */      \
        pr_reg[29] = regs->ccs;             /* CCS */      \
        pr_reg[30] = rdusp();               /* USP */      \
        pr_reg[31] = regs->spc;             /* SPC */      \

#endif /* _ASM_CRIS_ELF_H */
