/*
 * In-kernel FPU support functions
 *
 * Copyright IBM Corp. 2015
 * Author(s): Hendrik Brueckner <brueckner@linux.vnet.ibm.com>
 */

#ifndef _ASM_S390_FPU_API_H
#define _ASM_S390_FPU_API_H

void save_fpu_regs(void);

static inline int test_fp_ctl(u32 fpc)
{
	u32 orig_fpc;
	int rc;

	asm volatile(
		"	efpc    %1\n"
		"	sfpc	%2\n"
		"0:	sfpc	%1\n"
		"	la	%0,0\n"
		"1:\n"
		EX_TABLE(0b,1b)
		: "=d" (rc), "=d" (orig_fpc)
		: "d" (fpc), "0" (-EINVAL));
	return rc;
}

#endif /* _ASM_S390_FPU_API_H */
