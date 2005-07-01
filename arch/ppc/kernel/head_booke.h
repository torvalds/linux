#ifndef __HEAD_BOOKE_H__
#define __HEAD_BOOKE_H__

/*
 * Macros used for common Book-e exception handling
 */

#define SET_IVOR(vector_number, vector_label)		\
		li	r26,vector_label@l; 		\
		mtspr	SPRN_IVOR##vector_number,r26;	\
		sync

#define NORMAL_EXCEPTION_PROLOG						     \
	mtspr	SPRN_SPRG0,r10;		/* save two registers to work with */\
	mtspr	SPRN_SPRG1,r11;						     \
	mtspr	SPRN_SPRG4W,r1;						     \
	mfcr	r10;			/* save CR in r10 for now	   */\
	mfspr	r11,SPRN_SRR1;		/* check whether user or kernel    */\
	andi.	r11,r11,MSR_PR;						     \
	beq	1f;							     \
	mfspr	r1,SPRN_SPRG3;		/* if from user, start at top of   */\
	lwz	r1,THREAD_INFO-THREAD(r1); /* this thread's kernel stack   */\
	addi	r1,r1,THREAD_SIZE;					     \
1:	subi	r1,r1,INT_FRAME_SIZE;	/* Allocate an exception frame     */\
	mr	r11,r1;							     \
	stw	r10,_CCR(r11);          /* save various registers	   */\
	stw	r12,GPR12(r11);						     \
	stw	r9,GPR9(r11);						     \
	mfspr	r10,SPRN_SPRG0;						     \
	stw	r10,GPR10(r11);						     \
	mfspr	r12,SPRN_SPRG1;						     \
	stw	r12,GPR11(r11);						     \
	mflr	r10;							     \
	stw	r10,_LINK(r11);						     \
	mfspr	r10,SPRN_SPRG4R;					     \
	mfspr	r12,SPRN_SRR0;						     \
	stw	r10,GPR1(r11);						     \
	mfspr	r9,SPRN_SRR1;						     \
	stw	r10,0(r11);						     \
	rlwinm	r9,r9,0,14,12;		/* clear MSR_WE (necessary?)	   */\
	stw	r0,GPR0(r11);						     \
	SAVE_4GPRS(3, r11);						     \
	SAVE_2GPRS(7, r11)

/* To handle the additional exception priority levels on 40x and Book-E
 * processors we allocate a 4k stack per additional priority level. The various
 * head_xxx.S files allocate space (exception_stack_top) for each priority's
 * stack times the number of CPUs
 *
 * On 40x critical is the only additional level
 * On 44x/e500 we have critical and machine check
 * On e200 we have critical and debug (machine check occurs via critical)
 *
 * Additionally we reserve a SPRG for each priority level so we can free up a
 * GPR to use as the base for indirect access to the exception stacks.  This
 * is necessary since the MMU is always on, for Book-E parts, and the stacks
 * are offset from KERNELBASE.
 *
 */
#define BOOKE_EXCEPTION_STACK_SIZE	(8192)

/* CRIT_SPRG only used in critical exception handling */
#define CRIT_SPRG	SPRN_SPRG2
/* MCHECK_SPRG only used in machine check exception handling */
#define MCHECK_SPRG	SPRN_SPRG6W

#define MCHECK_STACK_TOP	(exception_stack_top - 4096)
#define CRIT_STACK_TOP		(exception_stack_top)

/* only on e200 for now */
#define DEBUG_STACK_TOP		(exception_stack_top - 4096)
#define DEBUG_SPRG		SPRN_SPRG6W

#ifdef CONFIG_SMP
#define BOOKE_LOAD_EXC_LEVEL_STACK(level)		\
	mfspr	r8,SPRN_PIR;				\
	mulli	r8,r8,BOOKE_EXCEPTION_STACK_SIZE;	\
	neg	r8,r8;					\
	addis	r8,r8,level##_STACK_TOP@ha;		\
	addi	r8,r8,level##_STACK_TOP@l
#else
#define BOOKE_LOAD_EXC_LEVEL_STACK(level)		\
	lis	r8,level##_STACK_TOP@h;			\
	ori	r8,r8,level##_STACK_TOP@l
#endif

/*
 * Exception prolog for critical/machine check exceptions.  This is a
 * little different from the normal exception prolog above since a
 * critical/machine check exception can potentially occur at any point
 * during normal exception processing. Thus we cannot use the same SPRG
 * registers as the normal prolog above. Instead we use a portion of the
 * critical/machine check exception stack at low physical addresses.
 */
#define EXC_LEVEL_EXCEPTION_PROLOG(exc_level, exc_level_srr0, exc_level_srr1) \
	mtspr	exc_level##_SPRG,r8;					     \
	BOOKE_LOAD_EXC_LEVEL_STACK(exc_level);/* r8 points to the exc_level stack*/ \
	stw	r10,GPR10-INT_FRAME_SIZE(r8);				     \
	stw	r11,GPR11-INT_FRAME_SIZE(r8);				     \
	mfcr	r10;			/* save CR in r10 for now	   */\
	mfspr	r11,exc_level_srr1;	/* check whether user or kernel    */\
	andi.	r11,r11,MSR_PR;						     \
	mr	r11,r8;							     \
	mfspr	r8,exc_level##_SPRG;					     \
	beq	1f;							     \
	/* COMING FROM USER MODE */					     \
	mfspr	r11,SPRN_SPRG3;		/* if from user, start at top of   */\
	lwz	r11,THREAD_INFO-THREAD(r11); /* this thread's kernel stack */\
	addi	r11,r11,THREAD_SIZE;					     \
1:	subi	r11,r11,INT_FRAME_SIZE;	/* Allocate an exception frame     */\
	stw	r10,_CCR(r11);          /* save various registers	   */\
	stw	r12,GPR12(r11);						     \
	stw	r9,GPR9(r11);						     \
	mflr	r10;							     \
	stw	r10,_LINK(r11);						     \
	mfspr	r12,SPRN_DEAR;		/* save DEAR and ESR in the frame  */\
	stw	r12,_DEAR(r11);		/* since they may have had stuff   */\
	mfspr	r9,SPRN_ESR;		/* in them at the point where the  */\
	stw	r9,_ESR(r11);		/* exception was taken		   */\
	mfspr	r12,exc_level_srr0;					     \
	stw	r1,GPR1(r11);						     \
	mfspr	r9,exc_level_srr1;					     \
	stw	r1,0(r11);						     \
	mr	r1,r11;							     \
	rlwinm	r9,r9,0,14,12;		/* clear MSR_WE (necessary?)	   */\
	stw	r0,GPR0(r11);						     \
	SAVE_4GPRS(3, r11);						     \
	SAVE_2GPRS(7, r11)

#define CRITICAL_EXCEPTION_PROLOG \
		EXC_LEVEL_EXCEPTION_PROLOG(CRIT, SPRN_CSRR0, SPRN_CSRR1)
#define DEBUG_EXCEPTION_PROLOG \
		EXC_LEVEL_EXCEPTION_PROLOG(DEBUG, SPRN_DSRR0, SPRN_DSRR1)
#define MCHECK_EXCEPTION_PROLOG \
		EXC_LEVEL_EXCEPTION_PROLOG(MCHECK, SPRN_MCSRR0, SPRN_MCSRR1)

/*
 * Exception vectors.
 */
#define	START_EXCEPTION(label)						     \
        .align 5;              						     \
label:

#define FINISH_EXCEPTION(func)					\
	bl	transfer_to_handler_full;			\
	.long	func;						\
	.long	ret_from_except_full

#define EXCEPTION(n, label, hdlr, xfer)				\
	START_EXCEPTION(label);					\
	NORMAL_EXCEPTION_PROLOG;				\
	addi	r3,r1,STACK_FRAME_OVERHEAD;			\
	xfer(n, hdlr)

#define CRITICAL_EXCEPTION(n, label, hdlr)			\
	START_EXCEPTION(label);					\
	CRITICAL_EXCEPTION_PROLOG;				\
	addi	r3,r1,STACK_FRAME_OVERHEAD;			\
	EXC_XFER_TEMPLATE(hdlr, n+2, (MSR_KERNEL & ~(MSR_ME|MSR_DE|MSR_CE)), \
			  NOCOPY, crit_transfer_to_handler, \
			  ret_from_crit_exc)

#define MCHECK_EXCEPTION(n, label, hdlr)			\
	START_EXCEPTION(label);					\
	MCHECK_EXCEPTION_PROLOG;				\
	mfspr	r5,SPRN_ESR;					\
	stw	r5,_ESR(r11);					\
	addi	r3,r1,STACK_FRAME_OVERHEAD;			\
	EXC_XFER_TEMPLATE(hdlr, n+2, (MSR_KERNEL & ~(MSR_ME|MSR_DE|MSR_CE)), \
			  NOCOPY, mcheck_transfer_to_handler,   \
			  ret_from_mcheck_exc)

#define EXC_XFER_TEMPLATE(hdlr, trap, msr, copyee, tfer, ret)	\
	li	r10,trap;					\
	stw	r10,TRAP(r11);					\
	lis	r10,msr@h;					\
	ori	r10,r10,msr@l;					\
	copyee(r10, r9);					\
	bl	tfer;		 				\
	.long	hdlr;						\
	.long	ret

#define COPY_EE(d, s)		rlwimi d,s,0,16,16
#define NOCOPY(d, s)

#define EXC_XFER_STD(n, hdlr)		\
	EXC_XFER_TEMPLATE(hdlr, n, MSR_KERNEL, NOCOPY, transfer_to_handler_full, \
			  ret_from_except_full)

#define EXC_XFER_LITE(n, hdlr)		\
	EXC_XFER_TEMPLATE(hdlr, n+1, MSR_KERNEL, NOCOPY, transfer_to_handler, \
			  ret_from_except)

#define EXC_XFER_EE(n, hdlr)		\
	EXC_XFER_TEMPLATE(hdlr, n, MSR_KERNEL, COPY_EE, transfer_to_handler_full, \
			  ret_from_except_full)

#define EXC_XFER_EE_LITE(n, hdlr)	\
	EXC_XFER_TEMPLATE(hdlr, n+1, MSR_KERNEL, COPY_EE, transfer_to_handler, \
			  ret_from_except)

/* Check for a single step debug exception while in an exception
 * handler before state has been saved.  This is to catch the case
 * where an instruction that we are trying to single step causes
 * an exception (eg ITLB/DTLB miss) and thus the first instruction of
 * the exception handler generates a single step debug exception.
 *
 * If we get a debug trap on the first instruction of an exception handler,
 * we reset the MSR_DE in the _exception handler's_ MSR (the debug trap is
 * a critical exception, so we are using SPRN_CSRR1 to manipulate the MSR).
 * The exception handler was handling a non-critical interrupt, so it will
 * save (and later restore) the MSR via SPRN_CSRR1, which will still have
 * the MSR_DE bit set.
 */
#ifdef CONFIG_E200
#define DEBUG_EXCEPTION							      \
	START_EXCEPTION(Debug);						      \
	DEBUG_EXCEPTION_PROLOG;						      \
									      \
	/*								      \
	 * If there is a single step or branch-taken exception in an	      \
	 * exception entry sequence, it was probably meant to apply to	      \
	 * the code where the exception occurred (since exception entry	      \
	 * doesn't turn off DE automatically).  We simulate the effect	      \
	 * of turning off DE on entry to an exception handler by turning      \
	 * off DE in the CSRR1 value and clearing the debug status.	      \
	 */								      \
	mfspr	r10,SPRN_DBSR;		/* check single-step/branch taken */  \
	andis.	r10,r10,DBSR_IC@h;					      \
	beq+	2f;							      \
									      \
	lis	r10,KERNELBASE@h;	/* check if exception in vectors */   \
	ori	r10,r10,KERNELBASE@l;					      \
	cmplw	r12,r10;						      \
	blt+	2f;			/* addr below exception vectors */    \
									      \
	lis	r10,Debug@h;						      \
	ori	r10,r10,Debug@l;					      \
	cmplw	r12,r10;						      \
	bgt+	2f;			/* addr above exception vectors */    \
									      \
	/* here it looks like we got an inappropriate debug exception. */     \
1:	rlwinm	r9,r9,0,~MSR_DE;	/* clear DE in the CDRR1 value */     \
	lis	r10,DBSR_IC@h;		/* clear the IC event */	      \
	mtspr	SPRN_DBSR,r10;						      \
	/* restore state and get out */					      \
	lwz	r10,_CCR(r11);						      \
	lwz	r0,GPR0(r11);						      \
	lwz	r1,GPR1(r11);						      \
	mtcrf	0x80,r10;						      \
	mtspr	SPRN_DSRR0,r12;						      \
	mtspr	SPRN_DSRR1,r9;						      \
	lwz	r9,GPR9(r11);						      \
	lwz	r12,GPR12(r11);						      \
	mtspr	DEBUG_SPRG,r8;						      \
	BOOKE_LOAD_EXC_LEVEL_STACK(DEBUG); /* r8 points to the debug stack */ \
	lwz	r10,GPR10-INT_FRAME_SIZE(r8);				      \
	lwz	r11,GPR11-INT_FRAME_SIZE(r8);				      \
	mfspr	r8,DEBUG_SPRG;						      \
									      \
	RFDI;								      \
	b	.;							      \
									      \
	/* continue normal handling for a critical exception... */	      \
2:	mfspr	r4,SPRN_DBSR;						      \
	addi	r3,r1,STACK_FRAME_OVERHEAD;				      \
	EXC_XFER_TEMPLATE(DebugException, 0x2002, (MSR_KERNEL & ~(MSR_ME|MSR_DE|MSR_CE)), NOCOPY, debug_transfer_to_handler, ret_from_debug_exc)
#else
#define DEBUG_EXCEPTION							      \
	START_EXCEPTION(Debug);						      \
	CRITICAL_EXCEPTION_PROLOG;					      \
									      \
	/*								      \
	 * If there is a single step or branch-taken exception in an	      \
	 * exception entry sequence, it was probably meant to apply to	      \
	 * the code where the exception occurred (since exception entry	      \
	 * doesn't turn off DE automatically).  We simulate the effect	      \
	 * of turning off DE on entry to an exception handler by turning      \
	 * off DE in the CSRR1 value and clearing the debug status.	      \
	 */								      \
	mfspr	r10,SPRN_DBSR;		/* check single-step/branch taken */  \
	andis.	r10,r10,DBSR_IC@h;					      \
	beq+	2f;							      \
									      \
	lis	r10,KERNELBASE@h;	/* check if exception in vectors */   \
	ori	r10,r10,KERNELBASE@l;					      \
	cmplw	r12,r10;						      \
	blt+	2f;			/* addr below exception vectors */    \
									      \
	lis	r10,Debug@h;						      \
	ori	r10,r10,Debug@l;					      \
	cmplw	r12,r10;						      \
	bgt+	2f;			/* addr above exception vectors */    \
									      \
	/* here it looks like we got an inappropriate debug exception. */     \
1:	rlwinm	r9,r9,0,~MSR_DE;	/* clear DE in the CSRR1 value */     \
	lis	r10,DBSR_IC@h;		/* clear the IC event */	      \
	mtspr	SPRN_DBSR,r10;						      \
	/* restore state and get out */					      \
	lwz	r10,_CCR(r11);						      \
	lwz	r0,GPR0(r11);						      \
	lwz	r1,GPR1(r11);						      \
	mtcrf	0x80,r10;						      \
	mtspr	SPRN_CSRR0,r12;						      \
	mtspr	SPRN_CSRR1,r9;						      \
	lwz	r9,GPR9(r11);						      \
	lwz	r12,GPR12(r11);						      \
	mtspr	CRIT_SPRG,r8;						      \
	BOOKE_LOAD_EXC_LEVEL_STACK(CRIT); /* r8 points to the debug stack */  \
	lwz	r10,GPR10-INT_FRAME_SIZE(r8);				      \
	lwz	r11,GPR11-INT_FRAME_SIZE(r8);				      \
	mfspr	r8,CRIT_SPRG;						      \
									      \
	rfci;								      \
	b	.;							      \
									      \
	/* continue normal handling for a critical exception... */	      \
2:	mfspr	r4,SPRN_DBSR;						      \
	addi	r3,r1,STACK_FRAME_OVERHEAD;				      \
	EXC_XFER_TEMPLATE(DebugException, 0x2002, (MSR_KERNEL & ~(MSR_ME|MSR_DE|MSR_CE)), NOCOPY, crit_transfer_to_handler, ret_from_crit_exc)
#endif

#define INSTRUCTION_STORAGE_EXCEPTION					      \
	START_EXCEPTION(InstructionStorage)				      \
	NORMAL_EXCEPTION_PROLOG;					      \
	mfspr	r5,SPRN_ESR;		/* Grab the ESR and save it */	      \
	stw	r5,_ESR(r11);						      \
	mr      r4,r12;                 /* Pass SRR0 as arg2 */		      \
	li      r5,0;                   /* Pass zero as arg3 */		      \
	EXC_XFER_EE_LITE(0x0400, handle_page_fault)

#define ALIGNMENT_EXCEPTION						      \
	START_EXCEPTION(Alignment)					      \
	NORMAL_EXCEPTION_PROLOG;					      \
	mfspr   r4,SPRN_DEAR;           /* Grab the DEAR and save it */	      \
	stw     r4,_DEAR(r11);						      \
	addi    r3,r1,STACK_FRAME_OVERHEAD;				      \
	EXC_XFER_EE(0x0600, AlignmentException)

#define PROGRAM_EXCEPTION						      \
	START_EXCEPTION(Program)					      \
	NORMAL_EXCEPTION_PROLOG;					      \
	mfspr	r4,SPRN_ESR;		/* Grab the ESR and save it */	      \
	stw	r4,_ESR(r11);						      \
	addi	r3,r1,STACK_FRAME_OVERHEAD;				      \
	EXC_XFER_STD(0x0700, ProgramCheckException)

#define DECREMENTER_EXCEPTION						      \
	START_EXCEPTION(Decrementer)					      \
	NORMAL_EXCEPTION_PROLOG;					      \
	lis     r0,TSR_DIS@h;           /* Setup the DEC interrupt mask */    \
	mtspr   SPRN_TSR,r0;		/* Clear the DEC interrupt */	      \
	addi    r3,r1,STACK_FRAME_OVERHEAD;				      \
	EXC_XFER_LITE(0x0900, timer_interrupt)

#define FP_UNAVAILABLE_EXCEPTION					      \
	START_EXCEPTION(FloatingPointUnavailable)			      \
	NORMAL_EXCEPTION_PROLOG;					      \
	bne	load_up_fpu;		/* if from user, just load it up */   \
	addi	r3,r1,STACK_FRAME_OVERHEAD;				      \
	EXC_XFER_EE_LITE(0x800, KernelFP)

#endif /* __HEAD_BOOKE_H__ */
