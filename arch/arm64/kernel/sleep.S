#include <linux/errno.h>
#include <linux/linkage.h>
#include <asm/asm-offsets.h>
#include <asm/assembler.h>

	.text
/*
 * Implementation of MPIDR_EL1 hash algorithm through shifting
 * and OR'ing.
 *
 * @dst: register containing hash result
 * @rs0: register containing affinity level 0 bit shift
 * @rs1: register containing affinity level 1 bit shift
 * @rs2: register containing affinity level 2 bit shift
 * @rs3: register containing affinity level 3 bit shift
 * @mpidr: register containing MPIDR_EL1 value
 * @mask: register containing MPIDR mask
 *
 * Pseudo C-code:
 *
 *u32 dst;
 *
 *compute_mpidr_hash(u32 rs0, u32 rs1, u32 rs2, u32 rs3, u64 mpidr, u64 mask) {
 *	u32 aff0, aff1, aff2, aff3;
 *	u64 mpidr_masked = mpidr & mask;
 *	aff0 = mpidr_masked & 0xff;
 *	aff1 = mpidr_masked & 0xff00;
 *	aff2 = mpidr_masked & 0xff0000;
 *	aff2 = mpidr_masked & 0xff00000000;
 *	dst = (aff0 >> rs0 | aff1 >> rs1 | aff2 >> rs2 | aff3 >> rs3);
 *}
 * Input registers: rs0, rs1, rs2, rs3, mpidr, mask
 * Output register: dst
 * Note: input and output registers must be disjoint register sets
         (eg: a macro instance with mpidr = x1 and dst = x1 is invalid)
 */
	.macro compute_mpidr_hash dst, rs0, rs1, rs2, rs3, mpidr, mask
	and	\mpidr, \mpidr, \mask		// mask out MPIDR bits
	and	\dst, \mpidr, #0xff		// mask=aff0
	lsr	\dst ,\dst, \rs0		// dst=aff0>>rs0
	and	\mask, \mpidr, #0xff00		// mask = aff1
	lsr	\mask ,\mask, \rs1
	orr	\dst, \dst, \mask		// dst|=(aff1>>rs1)
	and	\mask, \mpidr, #0xff0000	// mask = aff2
	lsr	\mask ,\mask, \rs2
	orr	\dst, \dst, \mask		// dst|=(aff2>>rs2)
	and	\mask, \mpidr, #0xff00000000	// mask = aff3
	lsr	\mask ,\mask, \rs3
	orr	\dst, \dst, \mask		// dst|=(aff3>>rs3)
	.endm
/*
 * Save CPU state in the provided sleep_stack_data area, and publish its
 * location for cpu_resume()'s use in sleep_save_stash.
 *
 * cpu_resume() will restore this saved state, and return. Because the
 * link-register is saved and restored, it will appear to return from this
 * function. So that the caller can tell the suspend/resume paths apart,
 * __cpu_suspend_enter() will always return a non-zero value, whereas the
 * path through cpu_resume() will return 0.
 *
 *  x0 = struct sleep_stack_data area
 */
ENTRY(__cpu_suspend_enter)
	stp	x29, lr, [x0, #SLEEP_STACK_DATA_CALLEE_REGS]
	stp	x19, x20, [x0,#SLEEP_STACK_DATA_CALLEE_REGS+16]
	stp	x21, x22, [x0,#SLEEP_STACK_DATA_CALLEE_REGS+32]
	stp	x23, x24, [x0,#SLEEP_STACK_DATA_CALLEE_REGS+48]
	stp	x25, x26, [x0,#SLEEP_STACK_DATA_CALLEE_REGS+64]
	stp	x27, x28, [x0,#SLEEP_STACK_DATA_CALLEE_REGS+80]

	/* save the sp in cpu_suspend_ctx */
	mov	x2, sp
	str	x2, [x0, #SLEEP_STACK_DATA_SYSTEM_REGS + CPU_CTX_SP]

	/* find the mpidr_hash */
	ldr	x1, =sleep_save_stash
	ldr	x1, [x1]
	mrs	x7, mpidr_el1
	ldr	x9, =mpidr_hash
	ldr	x10, [x9, #MPIDR_HASH_MASK]
	/*
	 * Following code relies on the struct mpidr_hash
	 * members size.
	 */
	ldp	w3, w4, [x9, #MPIDR_HASH_SHIFTS]
	ldp	w5, w6, [x9, #(MPIDR_HASH_SHIFTS + 8)]
	compute_mpidr_hash x8, x3, x4, x5, x6, x7, x10
	add	x1, x1, x8, lsl #3

	str	x0, [x1]
	add	x0, x0, #SLEEP_STACK_DATA_SYSTEM_REGS
	stp	x29, lr, [sp, #-16]!
	bl	cpu_do_suspend
	ldp	x29, lr, [sp], #16
	mov	x0, #1
	ret
ENDPROC(__cpu_suspend_enter)
	.ltorg

ENTRY(cpu_resume)
	bl	el2_setup		// if in EL2 drop to EL1 cleanly
	/* enable the MMU early - so we can access sleep_save_stash by va */
	adr_l	lr, __enable_mmu	/* __cpu_setup will return here */
	ldr	x27, =_cpu_resume	/* __enable_mmu will branch here */
	adrp	x25, idmap_pg_dir
	adrp	x26, swapper_pg_dir
	b	__cpu_setup
ENDPROC(cpu_resume)

ENTRY(_cpu_resume)
	mrs	x1, mpidr_el1
	adrp	x8, mpidr_hash
	add x8, x8, #:lo12:mpidr_hash // x8 = struct mpidr_hash phys address
        /* retrieve mpidr_hash members to compute the hash */
	ldr	x2, [x8, #MPIDR_HASH_MASK]
	ldp	w3, w4, [x8, #MPIDR_HASH_SHIFTS]
	ldp	w5, w6, [x8, #(MPIDR_HASH_SHIFTS + 8)]
	compute_mpidr_hash x7, x3, x4, x5, x6, x1, x2
        /* x7 contains hash index, let's use it to grab context pointer */
	ldr_l	x0, sleep_save_stash
	ldr	x0, [x0, x7, lsl #3]
	add	x29, x0, #SLEEP_STACK_DATA_CALLEE_REGS
	add	x0, x0, #SLEEP_STACK_DATA_SYSTEM_REGS
	/* load sp from context */
	ldr	x2, [x0, #CPU_CTX_SP]
	mov	sp, x2
	/* save thread_info */
	and	x2, x2, #~(THREAD_SIZE - 1)
	msr	sp_el0, x2
	/*
	 * cpu_do_resume expects x0 to contain context address pointer
	 */
	bl	cpu_do_resume

	ldp	x19, x20, [x29, #16]
	ldp	x21, x22, [x29, #32]
	ldp	x23, x24, [x29, #48]
	ldp	x25, x26, [x29, #64]
	ldp	x27, x28, [x29, #80]
	ldp	x29, lr, [x29]
	mov	x0, #0
	ret
ENDPROC(_cpu_resume)
