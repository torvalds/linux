/*
 * In-kernel vector facility support functions
 *
 * Copyright IBM Corp. 2015
 * Author(s): Hendrik Brueckner <brueckner@linux.vnet.ibm.com>
 */
#include <linux/kernel.h>
#include <linux/cpu.h>
#include <linux/sched.h>
#include <asm/fpu/types.h>
#include <asm/fpu/api.h>

/*
 * Per-CPU variable to maintain FPU register ranges that are in use
 * by the kernel.
 */
static DEFINE_PER_CPU(u32, kernel_fpu_state);

#define KERNEL_FPU_STATE_MASK	(KERNEL_FPU_MASK|KERNEL_FPC)


void __kernel_fpu_begin(struct kernel_fpu *state, u32 flags)
{
	if (!__this_cpu_read(kernel_fpu_state)) {
		/*
		 * Save user space FPU state and register contents.  Multiple
		 * calls because of interruptions do not matter and return
		 * immediately.  This also sets CIF_FPU to lazy restore FP/VX
		 * register contents when returning to user space.
		 */
		save_fpu_regs();
	}

	/* Update flags to use the vector facility for KERNEL_FPR */
	if (MACHINE_HAS_VX && (state->mask & KERNEL_FPR)) {
		flags |= KERNEL_VXR_LOW | KERNEL_FPC;
		flags &= ~KERNEL_FPR;
	}

	/* Save and update current kernel VX state */
	state->mask = __this_cpu_read(kernel_fpu_state);
	__this_cpu_or(kernel_fpu_state, flags & KERNEL_FPU_STATE_MASK);

	/*
	 * If this is the first call to __kernel_fpu_begin(), no additional
	 * work is required.
	 */
	if (!(state->mask & KERNEL_FPU_STATE_MASK))
		return;

	/*
	 * If KERNEL_FPR is still set, the vector facility is not available
	 * and, thus, save floating-point control and registers only.
	 */
	if (state->mask & KERNEL_FPR) {
		asm volatile("stfpc %0" : "=Q" (state->fpc));
		asm volatile("std 0,%0" : "=Q" (state->fprs[0]));
		asm volatile("std 1,%0" : "=Q" (state->fprs[1]));
		asm volatile("std 2,%0" : "=Q" (state->fprs[2]));
		asm volatile("std 3,%0" : "=Q" (state->fprs[3]));
		asm volatile("std 4,%0" : "=Q" (state->fprs[4]));
		asm volatile("std 5,%0" : "=Q" (state->fprs[5]));
		asm volatile("std 6,%0" : "=Q" (state->fprs[6]));
		asm volatile("std 7,%0" : "=Q" (state->fprs[7]));
		asm volatile("std 8,%0" : "=Q" (state->fprs[8]));
		asm volatile("std 9,%0" : "=Q" (state->fprs[9]));
		asm volatile("std 10,%0" : "=Q" (state->fprs[10]));
		asm volatile("std 11,%0" : "=Q" (state->fprs[11]));
		asm volatile("std 12,%0" : "=Q" (state->fprs[12]));
		asm volatile("std 13,%0" : "=Q" (state->fprs[13]));
		asm volatile("std 14,%0" : "=Q" (state->fprs[14]));
		asm volatile("std 15,%0" : "=Q" (state->fprs[15]));
		return;
	}

	/*
	 * If this is a nested call to __kernel_fpu_begin(), check the saved
	 * state mask to save and later restore the vector registers that
	 * are already in use.	Let's start with checking floating-point
	 * controls.
	 */
	if (state->mask & KERNEL_FPC)
		asm volatile("stfpc %0" : "=m" (state->fpc));

	/* Test and save vector registers */
	asm volatile (
		/*
		 * Test if any vector register must be saved and, if so,
		 * test if all register can be saved.
		 */
		"	tmll	%[m],15\n"	/* KERNEL_VXR_MASK */
		"	jz	20f\n"		/* no work -> done */
		"	la	1,%[vxrs]\n"	/* load save area */
		"	jo	18f\n"		/* -> save V0..V31 */

		/*
		 * Test if V8..V23 can be saved at once... this speeds up
		 * for KERNEL_fpu_MID only. Otherwise continue to split the
		 * range of vector registers into two halves and test them
		 * separately.
		 */
		"	tmll	%[m],6\n"	/* KERNEL_VXR_MID */
		"	jo	17f\n"		/* -> save V8..V23 */

		/* Test and save the first half of 16 vector registers */
		"1:	tmll	%[m],3\n"	/* KERNEL_VXR_LOW */
		"	jz	10f\n"		/* -> KERNEL_VXR_HIGH */
		"	jo	2f\n"		/* 11 -> save V0..V15 */
		"	brc	4,3f\n"		/* 01 -> save V0..V7  */
		"	brc	2,4f\n"		/* 10 -> save V8..V15 */

		/* Test and save the second half of 16 vector registers */
		"10:	tmll	%[m],12\n"	/* KERNEL_VXR_HIGH */
		"	jo	19f\n"		/* 11 -> save V16..V31 */
		"	brc	4,11f\n"	/* 01 -> save V16..V23	*/
		"	brc	2,12f\n"	/* 10 -> save V24..V31 */
		"	j	20f\n"		/* 00 -> done */

		/*
		 * Below are the vstm combinations to save multiple vector
		 * registers at once.
		 */
		"2:	.word	0xe70f,0x1000,0x003e\n"	/* vstm 0,15,0(1) */
		"	j	10b\n"			/* -> VXR_HIGH */
		"3:	.word	0xe707,0x1000,0x003e\n" /* vstm 0,7,0(1) */
		"	j	10b\n"			/* -> VXR_HIGH */
		"4:	.word	0xe78f,0x1080,0x003e\n" /* vstm 8,15,128(1) */
		"	j	10b\n"			/* -> VXR_HIGH */
		"\n"
		"11:	.word	0xe707,0x1100,0x0c3e\n"	/* vstm 16,23,256(1) */
		"	j	20f\n"			/* -> done */
		"12:	.word	0xe78f,0x1180,0x0c3e\n" /* vstm 24,31,384(1) */
		"	j	20f\n"			/* -> done */
		"\n"
		"17:	.word	0xe787,0x1080,0x043e\n"	/* vstm 8,23,128(1) */
		"	nill	%[m],249\n"		/* m &= ~VXR_MID    */
		"	j	1b\n"			/* -> VXR_LOW */
		"\n"
		"18:	.word	0xe70f,0x1000,0x003e\n"	/* vstm 0,15,0(1) */
		"19:	.word	0xe70f,0x1100,0x0c3e\n"	/* vstm 16,31,256(1) */
		"20:"
		: [vxrs] "=Q" (*(struct vx_array *) &state->vxrs)
		: [m] "d" (state->mask)
		: "1", "cc");
}
EXPORT_SYMBOL(__kernel_fpu_begin);

void __kernel_fpu_end(struct kernel_fpu *state)
{
	/* Just update the per-CPU state if there is nothing to restore */
	if (!(state->mask & KERNEL_FPU_STATE_MASK))
		goto update_fpu_state;

	/*
	 * If KERNEL_FPR is specified, the vector facility is not available
	 * and, thus, restore floating-point control and registers only.
	 */
	if (state->mask & KERNEL_FPR) {
		asm volatile("lfpc %0" : : "Q" (state->fpc));
		asm volatile("ld 0,%0" : : "Q" (state->fprs[0]));
		asm volatile("ld 1,%0" : : "Q" (state->fprs[1]));
		asm volatile("ld 2,%0" : : "Q" (state->fprs[2]));
		asm volatile("ld 3,%0" : : "Q" (state->fprs[3]));
		asm volatile("ld 4,%0" : : "Q" (state->fprs[4]));
		asm volatile("ld 5,%0" : : "Q" (state->fprs[5]));
		asm volatile("ld 6,%0" : : "Q" (state->fprs[6]));
		asm volatile("ld 7,%0" : : "Q" (state->fprs[7]));
		asm volatile("ld 8,%0" : : "Q" (state->fprs[8]));
		asm volatile("ld 9,%0" : : "Q" (state->fprs[9]));
		asm volatile("ld 10,%0" : : "Q" (state->fprs[10]));
		asm volatile("ld 11,%0" : : "Q" (state->fprs[11]));
		asm volatile("ld 12,%0" : : "Q" (state->fprs[12]));
		asm volatile("ld 13,%0" : : "Q" (state->fprs[13]));
		asm volatile("ld 14,%0" : : "Q" (state->fprs[14]));
		asm volatile("ld 15,%0" : : "Q" (state->fprs[15]));
		goto update_fpu_state;
	}

	/* Test and restore floating-point controls */
	if (state->mask & KERNEL_FPC)
		asm volatile("lfpc %0" : : "Q" (state->fpc));

	/* Test and restore (load) vector registers */
	asm volatile (
		/*
		 * Test if any vector registers must be loaded and, if so,
		 * test if all registers can be loaded at once.
		 */
		"	tmll	%[m],15\n"	/* KERNEL_VXR_MASK */
		"	jz	20f\n"		/* no work -> done */
		"	la	1,%[vxrs]\n"	/* load load area */
		"	jo	18f\n"		/* -> load V0..V31 */

		/*
		 * Test if V8..V23 can be restored at once... this speeds up
		 * for KERNEL_VXR_MID only. Otherwise continue to split the
		 * range of vector registers into two halves and test them
		 * separately.
		 */
		"	tmll	%[m],6\n"	/* KERNEL_VXR_MID */
		"	jo	17f\n"		/* -> load V8..V23 */

		/* Test and load the first half of 16 vector registers */
		"1:	tmll	%[m],3\n"	/* KERNEL_VXR_LOW */
		"	jz	10f\n"		/* -> KERNEL_VXR_HIGH */
		"	jo	2f\n"		/* 11 -> load V0..V15 */
		"	brc	4,3f\n"		/* 01 -> load V0..V7  */
		"	brc	2,4f\n"		/* 10 -> load V8..V15 */

		/* Test and load the second half of 16 vector registers */
		"10:	tmll	%[m],12\n"	/* KERNEL_VXR_HIGH */
		"	jo	19f\n"		/* 11 -> load V16..V31 */
		"	brc	4,11f\n"	/* 01 -> load V16..V23	*/
		"	brc	2,12f\n"	/* 10 -> load V24..V31 */
		"	j	20f\n"		/* 00 -> done */

		/*
		 * Below are the vstm combinations to load multiple vector
		 * registers at once.
		 */
		"2:	.word	0xe70f,0x1000,0x0036\n"	/* vlm 0,15,0(1) */
		"	j	10b\n"			/* -> VXR_HIGH */
		"3:	.word	0xe707,0x1000,0x0036\n" /* vlm 0,7,0(1) */
		"	j	10b\n"			/* -> VXR_HIGH */
		"4:	.word	0xe78f,0x1080,0x0036\n" /* vlm 8,15,128(1) */
		"	j	10b\n"			/* -> VXR_HIGH */
		"\n"
		"11:	.word	0xe707,0x1100,0x0c36\n"	/* vlm 16,23,256(1) */
		"	j	20f\n"			/* -> done */
		"12:	.word	0xe78f,0x1180,0x0c36\n" /* vlm 24,31,384(1) */
		"	j	20f\n"			/* -> done */
		"\n"
		"17:	.word	0xe787,0x1080,0x0436\n"	/* vlm 8,23,128(1) */
		"	nill	%[m],249\n"		/* m &= ~VXR_MID    */
		"	j	1b\n"			/* -> VXR_LOW */
		"\n"
		"18:	.word	0xe70f,0x1000,0x0036\n"	/* vlm 0,15,0(1) */
		"19:	.word	0xe70f,0x1100,0x0c36\n"	/* vlm 16,31,256(1) */
		"20:"
		:
		: [vxrs] "Q" (*(struct vx_array *) &state->vxrs),
		  [m] "d" (state->mask)
		: "1", "cc");

update_fpu_state:
	/* Update current kernel VX state */
	__this_cpu_write(kernel_fpu_state, state->mask);
}
EXPORT_SYMBOL(__kernel_fpu_end);
