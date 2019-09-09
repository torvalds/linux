// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * MXM WMI driver
 *
 * Copyright(C) 2010 Red Hat.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/mxm-wmi.h>
#include <linux/acpi.h>

MODULE_AUTHOR("Dave Airlie");
MODULE_DESCRIPTION("MXM WMI Driver");
MODULE_LICENSE("GPL");

#define MXM_WMMX_GUID "F6CB5C3C-9CAE-4EBD-B577-931EA32A2CC0"

MODULE_ALIAS("wmi:"MXM_WMMX_GUID);

#define MXM_WMMX_FUNC_MXDS 0x5344584D /* "MXDS" */
#define MXM_WMMX_FUNC_MXMX 0x53445344 /* "MXMX" */

struct mxds_args {
	u32 func;
	u32 args;
	u32 xarg;
};

int mxm_wmi_call_mxds(int adapter)
{
	struct mxds_args args = {
		.func = MXM_WMMX_FUNC_MXDS,
		.args = 0,
		.xarg = 1,
	};
	struct acpi_buffer input = { (acpi_size)sizeof(args), &args };
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	acpi_status status;

	printk("calling mux switch %d\n", adapter);

	status = wmi_evaluate_method(MXM_WMMX_GUID, 0x0, adapter, &input,
				     &output);

	if (ACPI_FAILURE(status))
		return status;

	printk("mux switched %d\n", status);
	return 0;
			    
}
EXPORT_SYMBOL_GPL(mxm_wmi_call_mxds);

int mxm_wmi_call_mxmx(int adapter)
{
	struct mxds_args args = {
		.func = MXM_WMMX_FUNC_MXMX,
		.args = 0,
		.xarg = 1,
	};
	struct acpi_buffer input = { (acpi_size)sizeof(args), &args };
	struct acpi_buffer output = { ACPI_ALLOCATE_BUFFER, NULL };
	acpi_status status;

	printk("calling mux switch %d\n", adapter);

	status = wmi_evaluate_method(MXM_WMMX_GUID, 0x0, adapter, &input,
				     &output);

	if (ACPI_FAILURE(status))
		return status;

	printk("mux mutex set switched %d\n", status);
	return 0;
			    
}
EXPORT_SYMBOL_GPL(mxm_wmi_call_mxmx);

bool mxm_wmi_supported(void)
{
	bool guid_valid;
	guid_valid = wmi_has_guid(MXM_WMMX_GUID);
	return guid_valid;
}
EXPORT_SYMBOL_GPL(mxm_wmi_supported);

static int __init mxm_wmi_init(void)
{
	return 0;
}

static void __exit mxm_wmi_exit(void)
{
}

module_init(mxm_wmi_init);
module_exit(mxm_wmi_exit);
