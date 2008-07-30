/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2008 Silicon Graphics, Inc.  All Rights Reserved.
 */

/*
 * Cross Partition (XP) sn2-based functions.
 *
 *      Architecture specific implementation of common functions.
 */

#include <linux/device.h>
#include <asm/sn/sn_sal.h>
#include "xp.h"

/*
 * The export of xp_nofault_PIOR needs to happen here since it is defined
 * in drivers/misc/sgi-xp/xp_nofault.S. The target of the nofault read is
 * defined here.
 */
EXPORT_SYMBOL_GPL(xp_nofault_PIOR);

u64 xp_nofault_PIOR_target;
EXPORT_SYMBOL_GPL(xp_nofault_PIOR_target);

/*
 * Register a nofault code region which performs a cross-partition PIO read.
 * If the PIO read times out, the MCA handler will consume the error and
 * return to a kernel-provided instruction to indicate an error. This PIO read
 * exists because it is guaranteed to timeout if the destination is down
 * (AMO operations do not timeout on at least some CPUs on Shubs <= v1.2,
 * which unfortunately we have to work around).
 */
static enum xp_retval
xp_register_nofault_code_sn2(void)
{
	int ret;
	u64 func_addr;
	u64 err_func_addr;

	func_addr = *(u64 *)xp_nofault_PIOR;
	err_func_addr = *(u64 *)xp_error_PIOR;
	ret = sn_register_nofault_code(func_addr, err_func_addr, err_func_addr,
				       1, 1);
	if (ret != 0) {
		dev_err(xp, "can't register nofault code, error=%d\n", ret);
		return xpSalError;
	}
	/*
	 * Setup the nofault PIO read target. (There is no special reason why
	 * SH_IPI_ACCESS was selected.)
	 */
	if (is_shub1())
		xp_nofault_PIOR_target = SH1_IPI_ACCESS;
	else if (is_shub2())
		xp_nofault_PIOR_target = SH2_IPI_ACCESS0;

	return xpSuccess;
}

void
xp_unregister_nofault_code_sn2(void)
{
	u64 func_addr = *(u64 *)xp_nofault_PIOR;
	u64 err_func_addr = *(u64 *)xp_error_PIOR;

	/* unregister the PIO read nofault code region */
	(void)sn_register_nofault_code(func_addr, err_func_addr,
				       err_func_addr, 1, 0);
}

enum xp_retval
xp_init_sn2(void)
{
	BUG_ON(!is_shub());

	xp_max_npartitions = XP_MAX_NPARTITIONS_SN2;

	return xp_register_nofault_code_sn2();
}

void
xp_exit_sn2(void)
{
	BUG_ON(!is_shub());

	xp_unregister_nofault_code_sn2();
}

