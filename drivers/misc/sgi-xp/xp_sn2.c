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

#include <linux/module.h>
#include <linux/device.h>
#include <asm/sn/bte.h>
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
 * (amo operations do not timeout on at least some CPUs on Shubs <= v1.2,
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

/*
 * Wrapper for bte_copy().
 *
 *	vdst - virtual address of the destination of the transfer.
 *	psrc - physical address of the source of the transfer.
 *	len - number of bytes to transfer from source to destination.
 *
 * Note: xp_remote_memcpy_sn2() should never be called while holding a spinlock.
 */
static enum xp_retval
xp_remote_memcpy_sn2(void *vdst, const void *psrc, size_t len)
{
	bte_result_t ret;
	u64 pdst = ia64_tpa(vdst);
	/* ??? What are the rules governing the src and dst addresses passed in?
	 * ??? Currently we're assuming that dst is a virtual address and src
	 * ??? is a physical address, is this appropriate? Can we allow them to
	 * ??? be whatever and we make the change here without damaging the
	 * ??? addresses?
	 */

	/*
	 * Ensure that the physically mapped memory is contiguous.
	 *
	 * We do this by ensuring that the memory is from region 7 only.
	 * If the need should arise to use memory from one of the other
	 * regions, then modify the BUG_ON() statement to ensure that the
	 * memory from that region is always physically contiguous.
	 */
	BUG_ON(REGION_NUMBER(vdst) != RGN_KERNEL);

	ret = bte_copy((u64)psrc, pdst, len, (BTE_NOTIFY | BTE_WACQUIRE), NULL);
	if (ret == BTE_SUCCESS)
		return xpSuccess;

	if (is_shub2())
		dev_err(xp, "bte_copy() on shub2 failed, error=0x%x\n", ret);
	else
		dev_err(xp, "bte_copy() failed, error=%d\n", ret);

	return xpBteCopyError;
}

static int
xp_cpu_to_nasid_sn2(int cpuid)
{
	return cpuid_to_nasid(cpuid);
}

enum xp_retval
xp_init_sn2(void)
{
	BUG_ON(!is_shub());

	xp_max_npartitions = XP_MAX_NPARTITIONS_SN2;
	xp_partition_id = sn_partition_id;
	xp_region_size = sn_region_size;

	xp_remote_memcpy = xp_remote_memcpy_sn2;
	xp_cpu_to_nasid = xp_cpu_to_nasid_sn2;

	return xp_register_nofault_code_sn2();
}

void
xp_exit_sn2(void)
{
	BUG_ON(!is_shub());

	xp_unregister_nofault_code_sn2();
}

