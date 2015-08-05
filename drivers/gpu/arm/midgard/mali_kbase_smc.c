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


static noinline u32 invoke_smc_fid(u32 function_id, u64 arg0, u64 arg1,
		u64 arg2, u64 *res0, u64 *res1, u64 *res2)
{
	/* 3 args and 3 returns are chosen arbitrarily,
	   see SMC calling convention for limits */
	asm volatile(
			"mov x0, %[fid]\n"
			"mov x1, %[a0]\n"
			"mov x2, %[a1]\n"
			"mov x3, %[a2]\n"
			"smc #0\n"
			"str x0, [%[re0]]\n"
			"str x1, [%[re1]]\n"
			"str x2, [%[re2]]\n"
			: [fid] "+r" (function_id), [a0] "+r" (arg0),
				[a1] "+r" (arg1), [a2] "+r" (arg2)
			: [re0] "r" (res0), [re1] "r" (res1), [re2] "r" (res2)
			: "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7",
				"x8", "x9",	"x10", "x11", "x12", "x13",
				"x14", "x15", "x16", "x17");
	return function_id;
}

void kbase_invoke_smc_fid(u32 fid)
{
	u64 res0, res1, res2;

	/* Is fast call (bit 31 set) */
	KBASE_DEBUG_ASSERT(fid & ~SMC_FAST_CALL);
	/* bits 16-23 must be zero for fast calls */
	KBASE_DEBUG_ASSERT((fid & (0xFF << 16)) == 0);

	invoke_smc_fid(fid, 0, 0, 0, &res0, &res1, &res2);
}

void kbase_invoke_smc(u32 oen, u16 function_number, u64 arg0, u64 arg1,
		u64 arg2, u64 *res0, u64 *res1, u64 *res2)
{
	u32 fid = 0;

	/* Only the six bits allowed should be used. */
	KBASE_DEBUG_ASSERT((oen & ~SMC_OEN_MASK) == 0);

	fid |= SMC_FAST_CALL; /* Bit 31: Fast call */
	/* Bit 30: 1=SMC64, 0=SMC32 */
	fid |= oen; /* Bit 29:24: OEN */
	/* Bit 23:16: Must be zero for fast calls */
	fid |= (function_number); /* Bit 15:0: function number */

	invoke_smc_fid(fid, arg0, arg1, arg2, res0, res1, res2);
}

#endif /* CONFIG_ARM64 */

