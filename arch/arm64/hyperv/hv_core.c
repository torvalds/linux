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
#include <linux/arm-smccc.h>
#include <linux/module.h>
#include <asm-generic/bug.h>
#include <hyperv/hvhdk.h>
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

/*
 * hyperv_report_panic - report a panic to Hyper-V.  This function uses
 * the older version of the Hyper-V interface that admittedly doesn't
 * pass enough information to be useful beyond just recording the
 * occurrence of a panic. The parallel hv_kmsg_dump() uses the
 * new interface that allows reporting 4 Kbytes of data, which is much
 * more useful. Hyper-V on ARM64 always supports the newer interface, but
 * we retain support for the older version because the sysadmin is allowed
 * to disable the newer version via sysctl in case of information security
 * concerns about the more verbose version.
 */
void hyperv_report_panic(struct pt_regs *regs, long err, bool in_die)
{
	static bool	panic_reported;
	u64		guest_id;

	/* Don't report a panic to Hyper-V if we're not going to panic */
	if (in_die && !panic_on_oops)
		return;

	/*
	 * We prefer to report panic on 'die' chain as we have proper
	 * registers to report, but if we miss it (e.g. on BUG()) we need
	 * to report it on 'panic'.
	 *
	 * Calling code in the 'die' and 'panic' paths ensures that only
	 * one CPU is running this code, so no atomicity is needed.
	 */
	if (panic_reported)
		return;
	panic_reported = true;

	guest_id = hv_get_vpreg(HV_REGISTER_GUEST_OS_ID);

	/*
	 * Hyper-V provides the ability to store only 5 values.
	 * Pick the passed in error value, the guest_id, the PC,
	 * and the SP.
	 */
	hv_set_vpreg(HV_REGISTER_GUEST_CRASH_P0, err);
	hv_set_vpreg(HV_REGISTER_GUEST_CRASH_P1, guest_id);
	hv_set_vpreg(HV_REGISTER_GUEST_CRASH_P2, regs->pc);
	hv_set_vpreg(HV_REGISTER_GUEST_CRASH_P3, regs->sp);
	hv_set_vpreg(HV_REGISTER_GUEST_CRASH_P4, 0);

	/*
	 * Let Hyper-V know there is crash data available
	 */
	hv_set_vpreg(HV_REGISTER_GUEST_CRASH_CTL, HV_CRASH_CTL_CRASH_NOTIFY);
}
EXPORT_SYMBOL_GPL(hyperv_report_panic);
