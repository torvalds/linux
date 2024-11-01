/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Preserved registers that are shared between code in ivt.S and
 * entry.S.  Be careful not to step on these!
 */
#define PRED_LEAVE_SYSCALL	1 /* TRUE iff leave from syscall */
#define PRED_KERNEL_STACK	2 /* returning to kernel-stacks? */
#define PRED_USER_STACK		3 /* returning to user-stacks? */
#define PRED_SYSCALL		4 /* inside a system call? */
#define PRED_NON_SYSCALL	5 /* complement of PRED_SYSCALL */

#ifdef __ASSEMBLY__
# define PASTE2(x,y)	x##y
# define PASTE(x,y)	PASTE2(x,y)

# define pLvSys		PASTE(p,PRED_LEAVE_SYSCALL)
# define pKStk		PASTE(p,PRED_KERNEL_STACK)
# define pUStk		PASTE(p,PRED_USER_STACK)
# define pSys		PASTE(p,PRED_SYSCALL)
# define pNonSys	PASTE(p,PRED_NON_SYSCALL)
#endif

#define PT(f)		(IA64_PT_REGS_##f##_OFFSET)
#define SW(f)		(IA64_SWITCH_STACK_##f##_OFFSET)
#define SOS(f)		(IA64_SAL_OS_STATE_##f##_OFFSET)

#define PT_REGS_SAVES(off)			\
	.unwabi 3, 'i';				\
	.fframe IA64_PT_REGS_SIZE+16+(off);	\
	.spillsp rp, PT(CR_IIP)+16+(off);	\
	.spillsp ar.pfs, PT(CR_IFS)+16+(off);	\
	.spillsp ar.unat, PT(AR_UNAT)+16+(off);	\
	.spillsp ar.fpsr, PT(AR_FPSR)+16+(off);	\
	.spillsp pr, PT(PR)+16+(off);

#define PT_REGS_UNWIND_INFO(off)		\
	.prologue;				\
	PT_REGS_SAVES(off);			\
	.body

#define SWITCH_STACK_SAVES(off)							\
	.savesp ar.unat,SW(CALLER_UNAT)+16+(off);				\
	.savesp ar.fpsr,SW(AR_FPSR)+16+(off);					\
	.spillsp f2,SW(F2)+16+(off); .spillsp f3,SW(F3)+16+(off);		\
	.spillsp f4,SW(F4)+16+(off); .spillsp f5,SW(F5)+16+(off);		\
	.spillsp f16,SW(F16)+16+(off); .spillsp f17,SW(F17)+16+(off);		\
	.spillsp f18,SW(F18)+16+(off); .spillsp f19,SW(F19)+16+(off);		\
	.spillsp f20,SW(F20)+16+(off); .spillsp f21,SW(F21)+16+(off);		\
	.spillsp f22,SW(F22)+16+(off); .spillsp f23,SW(F23)+16+(off);		\
	.spillsp f24,SW(F24)+16+(off); .spillsp f25,SW(F25)+16+(off);		\
	.spillsp f26,SW(F26)+16+(off); .spillsp f27,SW(F27)+16+(off);		\
	.spillsp f28,SW(F28)+16+(off); .spillsp f29,SW(F29)+16+(off);		\
	.spillsp f30,SW(F30)+16+(off); .spillsp f31,SW(F31)+16+(off);		\
	.spillsp r4,SW(R4)+16+(off); .spillsp r5,SW(R5)+16+(off);		\
	.spillsp r6,SW(R6)+16+(off); .spillsp r7,SW(R7)+16+(off);		\
	.spillsp b0,SW(B0)+16+(off); .spillsp b1,SW(B1)+16+(off);		\
	.spillsp b2,SW(B2)+16+(off); .spillsp b3,SW(B3)+16+(off);		\
	.spillsp b4,SW(B4)+16+(off); .spillsp b5,SW(B5)+16+(off);		\
	.spillsp ar.pfs,SW(AR_PFS)+16+(off); .spillsp ar.lc,SW(AR_LC)+16+(off);	\
	.spillsp @priunat,SW(AR_UNAT)+16+(off);					\
	.spillsp ar.rnat,SW(AR_RNAT)+16+(off);					\
	.spillsp ar.bspstore,SW(AR_BSPSTORE)+16+(off);				\
	.spillsp pr,SW(PR)+16+(off)

#define DO_SAVE_SWITCH_STACK			\
	movl r28=1f;				\
	;;					\
	.fframe IA64_SWITCH_STACK_SIZE;		\
	adds sp=-IA64_SWITCH_STACK_SIZE,sp;	\
	mov.ret.sptk b7=r28,1f;			\
	SWITCH_STACK_SAVES(0);			\
	br.cond.sptk.many save_switch_stack;	\
1:

#define DO_LOAD_SWITCH_STACK			\
	movl r28=1f;				\
	;;					\
	invala;					\
	mov.ret.sptk b7=r28,1f;			\
	br.cond.sptk.many load_switch_stack;	\
1:	.restore sp;				\
	adds sp=IA64_SWITCH_STACK_SIZE,sp
