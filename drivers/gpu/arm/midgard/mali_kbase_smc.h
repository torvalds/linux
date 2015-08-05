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





#ifndef _KBASE_SMC_H_
#define _KBASE_SMC_H_

#ifdef CONFIG_ARM64

#include <mali_kbase.h>

#define SMC_FAST_CALL (1 << 31)

#define SMC_OEN_OFFSET 24
#define SMC_OEN_MASK (0x3F << SMC_OEN_OFFSET) /* 6 bits */
#define SMC_OEN_SIP (2 << SMC_OEN_OFFSET)
#define SMC_OEN_STD (4 << SMC_OEN_OFFSET)


/**
  * kbase_invoke_smc_fid - Does a secure monitor call with the given function_id
  * @function_id: The SMC function to call, see SMC Calling convention.
  */
void kbase_invoke_smc_fid(u32 function_id);

/**
  * kbase_invoke_smc_fid - Does a secure monitor call with the given parameters.
  * see SMC Calling Convention for details
  * @oen: Owning Entity number (SIP, STD etc).
  * @function_number: ID specifiy which function within the OEN.
  * @arg0: argument 0 to pass in the SMC call.
  * @arg1: argument 1 to pass in the SMC call.
  * @arg2: argument 2 to pass in the SMC call.
  * @res0: result 0 returned from the SMC call.
  * @res1: result 1 returned from the SMC call.
  * @res2: result 2 returned from the SMC call.
  */
void kbase_invoke_smc(u32 oen, u16 function_number, u64 arg0, u64 arg1,
		u64 arg2, u64 *res0, u64 *res1, u64 *res2);

#endif /* CONFIG_ARM64 */

#endif /* _KBASE_SMC_H_ */
