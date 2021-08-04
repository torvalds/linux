// SPDX-License-Identifier: GPL-2.0

/*
 * Low level utility routines for interacting with Hyper-V.
 *
 * Copyright (C) 2021, Microsoft, Inc.
 *
 * Author : Michael Kelley <mikelley@microsoft.com>
 */

#include <linux/types.h>
#include <linux/export.h>
#include <linux/mm.h>
#include <linux/hyperv.h>
#include <linux/arm-smccc.h>
#include <linux/module.h>
#include <asm-generic/bug.h>
#include <asm/hyperv-tlfs.h>
#include <asm/mshyperv.h>

/*
 * hv_do_hypercall- Invoke the specified hypercall
 */
u64 hv_do_hypercall(u64 control, void *input, void *output)
{
	struct arm_smccc_res	res;
	u64			input_address;
	u64			output_address;

	input_address = input ? virt_to_phys(input) : 0;
	output_address = output ? virt_to_phys(output) : 0;

	arm_smccc_1_1_hvc(HV_FUNC_ID, control,
			  input_address, output_address, &res);
	return res.a0;
}
EXPORT_SYMBOL_GPL(hv_do_hypercall);

/*
 * hv_do_fast_hypercall8 -- Invoke the specified hypercall
 * with arguments in registers instead of physical memory.
 * Avoids the overhead of virt_to_phys for simple hypercalls.
 */

u64 hv_do_fast_hypercall8(u16 code, u64 input)
{
	struct arm_smccc_res	res;
	u64			control;

	control = (u64)code | HV_HYPERCALL_FAST_BIT;

	arm_smccc_1_1_hvc(HV_FUNC_ID, control, input, &res);
	return res.a0;
}
EXPORT_SYMBOL_GPL(hv_do_fast_hypercall8);

/*
 * Set a single VP register to a 64-bit value.
 */
void hv_set_vpreg(u32 msr, u64 value)
{
	struct arm_smccc_res res;

	arm_smccc_1_1_hvc(HV_FUNC_ID,
		HVCALL_SET_VP_REGISTERS | HV_HYPERCALL_FAST_BIT |
			HV_HYPERCALL_REP_COMP_1,
		HV_PARTITION_ID_SELF,
		HV_VP_INDEX_SELF,
		msr,
		0,
		value,
		0,
		&res);

	/*
	 * Something is fundamentally broken in the hypervisor if
	 * setting a VP register fails. There's really no way to
	 * continue as a guest VM, so panic.
	 */
	BUG_ON(!hv_result_success(res.a0));
}
EXPORT_SYMBOL_GPL(hv_set_vpreg);

/*
 * Get the value of a single VP register.  One version
 * returns just 64 bits and another returns the full 128 bits.
 * The two versions are separate to avoid complicating the
 * calling sequence for the more frequently used 64 bit version.
 */

void hv_get_vpreg_128(u32 msr, struct hv_get_vp_registers_output *result)
{
	struct arm_smccc_1_2_regs args;
	struct arm_smccc_1_2_regs res;

	args.a0 = HV_FUNC_ID;
	args.a1 = HVCALL_GET_VP_REGISTERS | HV_HYPERCALL_FAST_BIT |
			HV_HYPERCALL_REP_COMP_1;
	args.a2 = HV_PARTITION_ID_SELF;
	args.a3 = HV_VP_INDEX_SELF;
	args.a4 = msr;

	/*
	 * Use the SMCCC 1.2 interface because the results are in registers
	 * beyond X0-X3.
	 */
	arm_smccc_1_2_hvc(&args, &res);

	/*
	 * Something is fundamentally broken in the hypervisor if
	 * getting a VP register fails. There's really no way to
	 * continue as a guest VM, so panic.
	 */
	BUG_ON(!hv_result_success(res.a0));

	result->as64.low = res.a6;
	result->as64.high = res.a7;
}
EXPORT_SYMBOL_GPL(hv_get_vpreg_128);

u64 hv_get_vpreg(u32 msr)
{
	struct hv_get_vp_registers_output output;

	hv_get_vpreg_128(msr, &output);

	return output.as64.low;
}
EXPORT_SYMBOL_GPL(hv_get_vpreg);
