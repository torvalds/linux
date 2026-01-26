// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2024, Microsoft Corporation.
 *
 * This file contains functions that will be called from one or more modules.
 * If any of these modules are configured to build, this file is built and just
 * statically linked in.
 *
 * Authors: Microsoft Linux virtualization team
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/mshyperv.h>
#include <linux/resume_user_mode.h>
#include <linux/export.h>
#include <linux/acpi.h>
#include <linux/notifier.h>
#include <linux/reboot.h>

#include "mshv.h"

#define HV_GET_REGISTER_BATCH_SIZE	\
	(HV_HYP_PAGE_SIZE / sizeof(union hv_register_value))
#define HV_SET_REGISTER_BATCH_SIZE	\
	((HV_HYP_PAGE_SIZE - sizeof(struct hv_input_set_vp_registers)) \
		/ sizeof(struct hv_register_assoc))

int hv_call_get_vp_registers(u32 vp_index, u64 partition_id, u16 count,
			     union hv_input_vtl input_vtl,
			     struct hv_register_assoc *registers)
{
	struct hv_input_get_vp_registers *input_page;
	union hv_register_value *output_page;
	u16 completed = 0;
	unsigned long remaining = count;
	int rep_count, i;
	u64 status = HV_STATUS_SUCCESS;
	unsigned long flags;

	local_irq_save(flags);

	input_page = *this_cpu_ptr(hyperv_pcpu_input_arg);
	output_page = *this_cpu_ptr(hyperv_pcpu_output_arg);

	input_page->partition_id = partition_id;
	input_page->vp_index = vp_index;
	input_page->input_vtl.as_uint8 = input_vtl.as_uint8;
	input_page->rsvd_z8 = 0;
	input_page->rsvd_z16 = 0;

	while (remaining) {
		rep_count = min(remaining, HV_GET_REGISTER_BATCH_SIZE);
		for (i = 0; i < rep_count; ++i)
			input_page->names[i] = registers[i].name;

		status = hv_do_rep_hypercall(HVCALL_GET_VP_REGISTERS, rep_count,
					     0, input_page, output_page);
		if (!hv_result_success(status))
			break;

		completed = hv_repcomp(status);
		for (i = 0; i < completed; ++i)
			registers[i].value = output_page[i];

		registers += completed;
		remaining -= completed;
	}
	local_irq_restore(flags);

	return hv_result_to_errno(status);
}
EXPORT_SYMBOL_GPL(hv_call_get_vp_registers);

int hv_call_set_vp_registers(u32 vp_index, u64 partition_id, u16 count,
			     union hv_input_vtl input_vtl,
			     struct hv_register_assoc *registers)
{
	struct hv_input_set_vp_registers *input_page;
	u16 completed = 0;
	unsigned long remaining = count;
	int rep_count;
	u64 status = HV_STATUS_SUCCESS;
	unsigned long flags;

	local_irq_save(flags);
	input_page = *this_cpu_ptr(hyperv_pcpu_input_arg);

	input_page->partition_id = partition_id;
	input_page->vp_index = vp_index;
	input_page->input_vtl.as_uint8 = input_vtl.as_uint8;
	input_page->rsvd_z8 = 0;
	input_page->rsvd_z16 = 0;

	while (remaining) {
		rep_count = min(remaining, HV_SET_REGISTER_BATCH_SIZE);
		memcpy(input_page->elements, registers,
		       sizeof(struct hv_register_assoc) * rep_count);

		status = hv_do_rep_hypercall(HVCALL_SET_VP_REGISTERS, rep_count,
					     0, input_page, NULL);
		if (!hv_result_success(status))
			break;

		completed = hv_repcomp(status);
		registers += completed;
		remaining -= completed;
	}

	local_irq_restore(flags);

	return hv_result_to_errno(status);
}
EXPORT_SYMBOL_GPL(hv_call_set_vp_registers);

int hv_call_get_partition_property(u64 partition_id,
				   u64 property_code,
				   u64 *property_value)
{
	u64 status;
	unsigned long flags;
	struct hv_input_get_partition_property *input;
	struct hv_output_get_partition_property *output;

	local_irq_save(flags);
	input = *this_cpu_ptr(hyperv_pcpu_input_arg);
	output = *this_cpu_ptr(hyperv_pcpu_output_arg);
	memset(input, 0, sizeof(*input));
	input->partition_id = partition_id;
	input->property_code = property_code;
	status = hv_do_hypercall(HVCALL_GET_PARTITION_PROPERTY, input, output);

	if (!hv_result_success(status)) {
		local_irq_restore(flags);
		return hv_result_to_errno(status);
	}
	*property_value = output->property_value;

	local_irq_restore(flags);

	return 0;
}
EXPORT_SYMBOL_GPL(hv_call_get_partition_property);

#ifdef CONFIG_X86
/*
 * Corresponding sleep states have to be initialized in order for a subsequent
 * HVCALL_ENTER_SLEEP_STATE call to succeed. Currently only S5 state as per
 * ACPI 6.4 chapter 7.4.2 is relevant, while S1, S2 and S3 can be supported.
 *
 * In order to pass proper PM values to mshv, ACPI should be initialized and
 * should support S5 sleep state when this method is invoked.
 */
static int hv_initialize_sleep_states(void)
{
	u64 status;
	unsigned long flags;
	struct hv_input_set_system_property *in;
	acpi_status acpi_status;
	u8 sleep_type_a, sleep_type_b;

	if (!acpi_sleep_state_supported(ACPI_STATE_S5)) {
		pr_err("%s: S5 sleep state not supported.\n", __func__);
		return -ENODEV;
	}

	acpi_status = acpi_get_sleep_type_data(ACPI_STATE_S5, &sleep_type_a,
					       &sleep_type_b);
	if (ACPI_FAILURE(acpi_status))
		return -ENODEV;

	local_irq_save(flags);
	in = *this_cpu_ptr(hyperv_pcpu_input_arg);
	memset(in, 0, sizeof(*in));

	in->property_id = HV_SYSTEM_PROPERTY_SLEEP_STATE;
	in->set_sleep_state_info.sleep_state = HV_SLEEP_STATE_S5;
	in->set_sleep_state_info.pm1a_slp_typ = sleep_type_a;
	in->set_sleep_state_info.pm1b_slp_typ = sleep_type_b;

	status = hv_do_hypercall(HVCALL_SET_SYSTEM_PROPERTY, in, NULL);
	local_irq_restore(flags);

	if (!hv_result_success(status)) {
		hv_status_err(status, "\n");
		return hv_result_to_errno(status);
	}

	return 0;
}

/*
 * This notifier initializes sleep states in mshv hypervisor which will be
 * used during power off.
 */
static int hv_reboot_notifier_handler(struct notifier_block *this,
				      unsigned long code, void *another)
{
	int ret = 0;

	if (code == SYS_HALT || code == SYS_POWER_OFF)
		ret = hv_initialize_sleep_states();

	return ret ? NOTIFY_DONE : NOTIFY_OK;
}

static struct notifier_block hv_reboot_notifier = {
	.notifier_call = hv_reboot_notifier_handler,
};

void hv_sleep_notifiers_register(void)
{
	int ret;

	ret = register_reboot_notifier(&hv_reboot_notifier);
	if (ret)
		pr_err("%s: cannot register reboot notifier %d\n", __func__,
		       ret);
}

/*
 * Power off the machine by entering S5 sleep state via Hyper-V hypercall.
 * This call does not return if successful.
 */
void hv_machine_power_off(void)
{
	unsigned long flags;
	struct hv_input_enter_sleep_state *in;

	local_irq_save(flags);
	in = *this_cpu_ptr(hyperv_pcpu_input_arg);
	in->sleep_state = HV_SLEEP_STATE_S5;

	(void)hv_do_hypercall(HVCALL_ENTER_SLEEP_STATE, in, NULL);
	local_irq_restore(flags);

	/* should never reach here */
	BUG();

}
#endif
