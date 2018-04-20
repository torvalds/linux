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
#define SMC_64 (1 << 30)

#define SMC_OEN_OFFSET 24
#define SMC_OEN_MASK (0x3F << SMC_OEN_OFFSET) /* 6 bits */
#define SMC_OEN_SIP (2 << SMC_OEN_OFFSET)
#define SMC_OEN_STD (4 << SMC_OEN_OFFSET)


/**
  * kbase_invoke_smc_fid - Perform a secure monitor call
  * @fid: The SMC function to call, see SMC Calling convention.
  * @arg0: First argument to the SMC.
  * @arg1: Second argument to the SMC.
  * @arg2: Third argument to the SMC.
  *
  * See SMC Calling Convention for details.
  *
  * Return: the return value from the SMC.
  */
u64 kbase_invoke_smc_fid(u32 fid, u64 arg0, u64 arg1, u64 arg2);

/**
  * kbase_invoke_smc_fid - Perform a secure monitor call
  * @oen: Owning Entity number (SIP, STD etc).
  * @function_number: The function number within the OEN.
  * @smc64: use SMC64 calling convention instead of SMC32.
  * @arg0: First argument to the SMC.
  * @arg1: Second argument to the SMC.
  * @arg2: Third argument to the SMC.
  *
  * See SMC Calling Convention for details.
  *
  * Return: the return value from the SMC call.
  */
u64 kbase_invoke_smc(u32 oen, u16 function_number, bool smc64,
		u64 arg0, u64 arg1, u64 arg2);

#endif /* CONFIG_ARM64 */

#endif /* _KBASE_SMC_H_ */
