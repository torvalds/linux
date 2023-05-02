// SPDX-License-Identifier: GPL-2.0-only

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/syscore_ops.h>
#include <asm/cpufeature.h>
#include <asm/mtrr.h>
#include <asm/processor.h>
#include "mtrr.h"

void mtrr_set_if(void)
{
	switch (boot_cpu_data.x86_vendor) {
	case X86_VENDOR_AMD:
		/* Pre-Athlon (K6) AMD CPU MTRRs */
		if (cpu_feature_enabled(X86_FEATURE_K6_MTRR))
			mtrr_if = &amd_mtrr_ops;
		break;
	case X86_VENDOR_CENTAUR:
		if (cpu_feature_enabled(X86_FEATURE_CENTAUR_MCR))
			mtrr_if = &centaur_mtrr_ops;
		break;
	case X86_VENDOR_CYRIX:
		if (cpu_feature_enabled(X86_FEATURE_CYRIX_ARR))
			mtrr_if = &cyrix_mtrr_ops;
		break;
	default:
		break;
	}
}

/*
 * The suspend/resume methods are only for CPUs without MTRR. CPUs using generic
 * MTRR driver don't require this.
 */
struct mtrr_value {
	mtrr_type	ltype;
	unsigned long	lbase;
	unsigned long	lsize;
};

static struct mtrr_value *mtrr_value;

static int mtrr_save(void)
{
	int i;

	if (!mtrr_value)
		return -ENOMEM;

	for (i = 0; i < num_var_ranges; i++) {
		mtrr_if->get(i, &mtrr_value[i].lbase,
				&mtrr_value[i].lsize,
				&mtrr_value[i].ltype);
	}
	return 0;
}

static void mtrr_restore(void)
{
	int i;

	for (i = 0; i < num_var_ranges; i++) {
		if (mtrr_value[i].lsize) {
			mtrr_if->set(i, mtrr_value[i].lbase,
				     mtrr_value[i].lsize,
				     mtrr_value[i].ltype);
		}
	}
}

static struct syscore_ops mtrr_syscore_ops = {
	.suspend	= mtrr_save,
	.resume		= mtrr_restore,
};

void mtrr_register_syscore(void)
{
	mtrr_value = kcalloc(num_var_ranges, sizeof(*mtrr_value), GFP_KERNEL);

	/*
	 * The CPU has no MTRR and seems to not support SMP. They have
	 * specific drivers, we use a tricky method to support
	 * suspend/resume for them.
	 *
	 * TBD: is there any system with such CPU which supports
	 * suspend/resume? If no, we should remove the code.
	 */
	register_syscore_ops(&mtrr_syscore_ops);
}
