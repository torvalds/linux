/*
 *
 * (C) COPYRIGHT 2015 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */



#ifdef CONFIG_ARM64

#include <mali_kbase.h>
#include <mali_kbase_smc.h>

#include <linux/compiler.h>

static noinline u64 invoke_smc_fid(u64 function_id,
		u64 arg0, u64 arg1, u64 arg2)
{
	asm volatile(
			__asmeq("%0", "x0")
			__asmeq("%1", "x1")
			__asmeq("%2", "x2")
			__asmeq("%3", "x3")
			"smc	#0\n"
		: "+r" (function_id)
		: "r" (arg0), "r" (arg1), "r" (arg2));

	return function_id;
}

u64 kbase_invoke_smc_fid(u32 fid, u64 arg0, u64 arg1, u64 arg2)
{
	/* Is fast call (bit 31 set) */
	KBASE_DEBUG_ASSERT(fid & ~SMC_FAST_CALL);
	/* bits 16-23 must be zero for fast calls */
	KBASE_DEBUG_ASSERT((fid & (0xFF << 16)) == 0);

	return invoke_smc_fid(fid, arg0, arg1, arg2);
}

u64 kbase_invoke_smc(u32 oen, u16 function_number, bool smc64,
		u64 arg0, u64 arg1, u64 arg2)
{
	u32 fid = 0;

	/* Only the six bits allowed should be used. */
	KBASE_DEBUG_ASSERT((oen & ~SMC_OEN_MASK) == 0);

	fid |= SMC_FAST_CALL; /* Bit 31: Fast call */
	if (smc64)
		fid |= SMC_64; /* Bit 30: 1=SMC64, 0=SMC32 */
	fid |= oen; /* Bit 29:24: OEN */
	/* Bit 23:16: Must be zero for fast calls */
	fid |= (function_number); /* Bit 15:0: function number */

	return kbase_invoke_smc_fid(fid, arg0, arg1, arg2);
}

#endif /* CONFIG_ARM64 */

