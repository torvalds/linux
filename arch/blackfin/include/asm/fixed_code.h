/* This file defines the fixed addresses where userspace programs can find
   atomic code sequences.  */

#ifndef __BFIN_ASM_FIXED_CODE_H__
#define __BFIN_ASM_FIXED_CODE_H__

#ifdef __KERNEL__
#ifndef __ASSEMBLY__
#include <linux/linkage.h>
#include <linux/ptrace.h>
extern asmlinkage void finish_atomic_sections(struct pt_regs *regs);
extern char fixed_code_start;
extern char fixed_code_end;
extern int atomic_xchg32(void);
extern int atomic_cas32(void);
extern int atomic_add32(void);
extern int atomic_sub32(void);
extern int atomic_ior32(void);
extern int atomic_and32(void);
extern int atomic_xor32(void);
extern void safe_user_instruction(void);
extern void sigreturn_stub(void);
#endif
#endif

#define FIXED_CODE_START	0x400

#define SIGRETURN_STUB		0x400

#define ATOMIC_SEQS_START	0x410

#define ATOMIC_XCHG32		0x410
#define ATOMIC_CAS32		0x420
#define ATOMIC_ADD32		0x430
#define ATOMIC_SUB32		0x440
#define ATOMIC_IOR32		0x450
#define ATOMIC_AND32		0x460
#define ATOMIC_XOR32		0x470

#define ATOMIC_SEQS_END		0x480

#define SAFE_USER_INSTRUCTION   0x480

#define FIXED_CODE_END		0x490

#endif
