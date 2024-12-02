// SPDX-License-Identifier: GPL-2.0
/*
 * CRC-32 implemented with the z/Architecture Vector Extension Facility.
 *
 * Copyright IBM Corp. 2015
 * Author(s): Hendrik Brueckner <brueckner@linux.vnet.ibm.com>
 */
#define KMSG_COMPONENT	"crc32-vx"
#define pr_fmt(fmt)	KMSG_COMPONENT ": " fmt

#include <linux/module.h>
#include <linux/cpufeature.h>
#include <linux/crc32.h>
#include <asm/fpu.h>
#include "crc32-vx.h"

#define VX_MIN_LEN		64
#define VX_ALIGNMENT		16L
#define VX_ALIGN_MASK		(VX_ALIGNMENT - 1)

static DEFINE_STATIC_KEY_FALSE(have_vxrs);

/*
 * DEFINE_CRC32_VX() - Define a CRC-32 function using the vector extension
 *
 * Creates a function to perform a particular CRC-32 computation. Depending
 * on the message buffer, the hardware-accelerated or software implementation
 * is used.   Note that the message buffer is aligned to improve fetch
 * operations of VECTOR LOAD MULTIPLE instructions.
 */
#define DEFINE_CRC32_VX(___fname, ___crc32_vx, ___crc32_sw)		    \
	u32 ___fname(u32 crc, const u8 *data, size_t datalen)		    \
	{								    \
		unsigned long prealign, aligned, remaining;		    \
		DECLARE_KERNEL_FPU_ONSTACK16(vxstate);			    \
									    \
		if (datalen < VX_MIN_LEN + VX_ALIGN_MASK ||		    \
		    !static_branch_likely(&have_vxrs))			    \
			return ___crc32_sw(crc, data, datalen);		    \
									    \
		if ((unsigned long)data & VX_ALIGN_MASK) {		    \
			prealign = VX_ALIGNMENT -			    \
				  ((unsigned long)data & VX_ALIGN_MASK);    \
			datalen -= prealign;				    \
			crc = ___crc32_sw(crc, data, prealign);		    \
			data = (void *)((unsigned long)data + prealign);    \
		}							    \
									    \
		aligned = datalen & ~VX_ALIGN_MASK;			    \
		remaining = datalen & VX_ALIGN_MASK;			    \
									    \
		kernel_fpu_begin(&vxstate, KERNEL_VXR_LOW);		    \
		crc = ___crc32_vx(crc, data, aligned);			    \
		kernel_fpu_end(&vxstate, KERNEL_VXR_LOW);		    \
									    \
		if (remaining)						    \
			crc = ___crc32_sw(crc, data + aligned, remaining);  \
									    \
		return crc;						    \
	}								    \
	EXPORT_SYMBOL(___fname);

DEFINE_CRC32_VX(crc32_le_arch, crc32_le_vgfm_16, crc32_le_base)
DEFINE_CRC32_VX(crc32_be_arch, crc32_be_vgfm_16, crc32_be_base)
DEFINE_CRC32_VX(crc32c_le_arch, crc32c_le_vgfm_16, crc32c_le_base)

static int __init crc32_s390_init(void)
{
	if (cpu_have_feature(S390_CPU_FEATURE_VXRS))
		static_branch_enable(&have_vxrs);
	return 0;
}
arch_initcall(crc32_s390_init);

static void __exit crc32_s390_exit(void)
{
}
module_exit(crc32_s390_exit);

u32 crc32_optimizations(void)
{
	if (static_key_enabled(&have_vxrs))
		return CRC32_LE_OPTIMIZATION |
		       CRC32_BE_OPTIMIZATION |
		       CRC32C_OPTIMIZATION;
	return 0;
}
EXPORT_SYMBOL(crc32_optimizations);

MODULE_AUTHOR("Hendrik Brueckner <brueckner@linux.vnet.ibm.com>");
MODULE_DESCRIPTION("CRC-32 algorithms using z/Architecture Vector Extension Facility");
MODULE_LICENSE("GPL");
