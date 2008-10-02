/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2008 Silicon Graphics, Inc.  All Rights Reserved.
 */

/*
 * Cross Partition (XP) uv-based functions.
 *
 *      Architecture specific implementation of common functions.
 *
 */

#include <linux/device.h>
#include <asm/uv/uv_hub.h>
#include "../sgi-gru/grukservices.h"
#include "xp.h"

/*
 * Convert a virtual memory address to a physical memory address.
 */
static unsigned long
xp_pa_uv(void *addr)
{
	return uv_gpa(addr);
}

static enum xp_retval
xp_remote_memcpy_uv(unsigned long dst_gpa, const unsigned long src_gpa,
		    size_t len)
{
	int ret;

	ret = gru_copy_gpa(dst_gpa, src_gpa, len);
	if (ret == 0)
		return xpSuccess;

	dev_err(xp, "gru_copy_gpa() failed, dst_gpa=0x%016lx src_gpa=0x%016lx "
		"len=%ld\n", dst_gpa, src_gpa, len);
	return xpGruCopyError;
}

static int
xp_cpu_to_nasid_uv(int cpuid)
{
	/* ??? Is this same as sn2 nasid in mach/part bitmaps set up by SAL? */
	return UV_PNODE_TO_NASID(uv_cpu_to_pnode(cpuid));
}

enum xp_retval
xp_init_uv(void)
{
	BUG_ON(!is_uv());

	xp_max_npartitions = XP_MAX_NPARTITIONS_UV;
	xp_partition_id = 0;	/* !!! not correct value */
	xp_region_size = 0;	/* !!! not correct value */

	xp_pa = xp_pa_uv;
	xp_remote_memcpy = xp_remote_memcpy_uv;
	xp_cpu_to_nasid = xp_cpu_to_nasid_uv;

	return xpSuccess;
}

void
xp_exit_uv(void)
{
	BUG_ON(!is_uv());
}
