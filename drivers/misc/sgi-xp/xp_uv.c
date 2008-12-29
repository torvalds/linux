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
#if defined CONFIG_X86_64
#include <asm/uv/bios.h>
#elif defined CONFIG_IA64_GENERIC || defined CONFIG_IA64_SGI_UV
#include <asm/sn/sn_sal.h>
#endif
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

static enum xp_retval
xp_expand_memprotect_uv(unsigned long phys_addr, unsigned long size)
{
	int ret;

#if defined CONFIG_X86_64
	ret = uv_bios_change_memprotect(phys_addr, size, UV_MEMPROT_ALLOW_RW);
	if (ret != BIOS_STATUS_SUCCESS) {
		dev_err(xp, "uv_bios_change_memprotect(,, "
			"UV_MEMPROT_ALLOW_RW) failed, ret=%d\n", ret);
		return xpBiosError;
	}

#elif defined CONFIG_IA64_GENERIC || defined CONFIG_IA64_SGI_UV
	u64 nasid_array;

	ret = sn_change_memprotect(phys_addr, size, SN_MEMPROT_ACCESS_CLASS_1,
				   &nasid_array);
	if (ret != 0) {
		dev_err(xp, "sn_change_memprotect(,, "
			"SN_MEMPROT_ACCESS_CLASS_1,) failed ret=%d\n", ret);
		return xpSalError;
	}
#else
	#error not a supported configuration
#endif
	return xpSuccess;
}

static enum xp_retval
xp_restrict_memprotect_uv(unsigned long phys_addr, unsigned long size)
{
	int ret;

#if defined CONFIG_X86_64
	ret = uv_bios_change_memprotect(phys_addr, size,
					UV_MEMPROT_RESTRICT_ACCESS);
	if (ret != BIOS_STATUS_SUCCESS) {
		dev_err(xp, "uv_bios_change_memprotect(,, "
			"UV_MEMPROT_RESTRICT_ACCESS) failed, ret=%d\n", ret);
		return xpBiosError;
	}

#elif defined CONFIG_IA64_GENERIC || defined CONFIG_IA64_SGI_UV
	u64 nasid_array;

	ret = sn_change_memprotect(phys_addr, size, SN_MEMPROT_ACCESS_CLASS_0,
				   &nasid_array);
	if (ret != 0) {
		dev_err(xp, "sn_change_memprotect(,, "
			"SN_MEMPROT_ACCESS_CLASS_0,) failed ret=%d\n", ret);
		return xpSalError;
	}
#else
	#error not a supported configuration
#endif
	return xpSuccess;
}

enum xp_retval
xp_init_uv(void)
{
	BUG_ON(!is_uv());

	xp_max_npartitions = XP_MAX_NPARTITIONS_UV;
	xp_partition_id = sn_partition_id;
	xp_region_size = sn_region_size;

	xp_pa = xp_pa_uv;
	xp_remote_memcpy = xp_remote_memcpy_uv;
	xp_cpu_to_nasid = xp_cpu_to_nasid_uv;
	xp_expand_memprotect = xp_expand_memprotect_uv;
	xp_restrict_memprotect = xp_restrict_memprotect_uv;

	return xpSuccess;
}

void
xp_exit_uv(void)
{
	BUG_ON(!is_uv());
}
