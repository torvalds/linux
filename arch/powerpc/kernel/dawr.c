// SPDX-License-Identifier: GPL-2.0+
/*
 * DAWR infrastructure
 *
 * Copyright 2019, Michael Neuling, IBM Corporation.
 */

#include <linux/types.h>
#include <linux/export.h>
#include <linux/fs.h>
#include <linux/debugfs.h>
#include <asm/debugfs.h>
#include <asm/machdep.h>
#include <asm/hvcall.h>

bool dawr_force_enable;
EXPORT_SYMBOL_GPL(dawr_force_enable);

int set_dawr(struct arch_hw_breakpoint *brk)
{
	unsigned long dawr, dawrx, mrd;

	dawr = brk->address;

	dawrx  = (brk->type & (HW_BRK_TYPE_READ | HW_BRK_TYPE_WRITE))
		<< (63 - 58);
	dawrx |= ((brk->type & (HW_BRK_TYPE_TRANSLATE)) >> 2) << (63 - 59);
	dawrx |= (brk->type & (HW_BRK_TYPE_PRIV_ALL)) >> 3;
	/*
	 * DAWR length is stored in field MDR bits 48:53.  Matches range in
	 * doublewords (64 bits) baised by -1 eg. 0b000000=1DW and
	 * 0b111111=64DW.
	 * brk->hw_len is in bytes.
	 * This aligns up to double word size, shifts and does the bias.
	 */
	mrd = ((brk->hw_len + 7) >> 3) - 1;
	dawrx |= (mrd & 0x3f) << (63 - 53);

	if (ppc_md.set_dawr)
		return ppc_md.set_dawr(dawr, dawrx);

	mtspr(SPRN_DAWR, dawr);
	mtspr(SPRN_DAWRX, dawrx);

	return 0;
}

static void set_dawr_cb(void *info)
{
	set_dawr(info);
}

static ssize_t dawr_write_file_bool(struct file *file,
				    const char __user *user_buf,
				    size_t count, loff_t *ppos)
{
	struct arch_hw_breakpoint null_brk = {0};
	size_t rc;

	/* Send error to user if they hypervisor won't allow us to write DAWR */
	if (!dawr_force_enable &&
	    firmware_has_feature(FW_FEATURE_LPAR) &&
	    set_dawr(&null_brk) != H_SUCCESS)
		return -ENODEV;

	rc = debugfs_write_file_bool(file, user_buf, count, ppos);
	if (rc)
		return rc;

	/* If we are clearing, make sure all CPUs have the DAWR cleared */
	if (!dawr_force_enable)
		smp_call_function(set_dawr_cb, &null_brk, 0);

	return rc;
}

static const struct file_operations dawr_enable_fops = {
	.read =		debugfs_read_file_bool,
	.write =	dawr_write_file_bool,
	.open =		simple_open,
	.llseek =	default_llseek,
};

static int __init dawr_force_setup(void)
{
	if (cpu_has_feature(CPU_FTR_DAWR)) {
		/* Don't setup sysfs file for user control on P8 */
		dawr_force_enable = true;
		return 0;
	}

	if (PVR_VER(mfspr(SPRN_PVR)) == PVR_POWER9) {
		/* Turn DAWR off by default, but allow admin to turn it on */
		debugfs_create_file_unsafe("dawr_enable_dangerous", 0600,
					   powerpc_debugfs_root,
					   &dawr_force_enable,
					   &dawr_enable_fops);
	}
	return 0;
}
arch_initcall(dawr_force_setup);
