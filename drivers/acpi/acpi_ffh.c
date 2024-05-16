// SPDX-License-Identifier: GPL-2.0-only
/*
 * Author: Sudeep Holla <sudeep.holla@arm.com>
 * Copyright 2022 Arm Limited
 */
#include <linux/kernel.h>
#include <linux/acpi.h>
#include <linux/completion.h>
#include <linux/idr.h>
#include <linux/io.h>

static struct acpi_ffh_info ffh_ctx;

int __weak acpi_ffh_address_space_arch_setup(void *handler_ctxt,
					     void **region_ctxt)
{
	return -EOPNOTSUPP;
}

int __weak acpi_ffh_address_space_arch_handler(acpi_integer *value,
					       void *region_context)
{
	return -EOPNOTSUPP;
}

static acpi_status
acpi_ffh_address_space_setup(acpi_handle region_handle, u32 function,
			     void *handler_context,  void **region_context)
{
	return acpi_ffh_address_space_arch_setup(handler_context,
						 region_context);
}

static acpi_status
acpi_ffh_address_space_handler(u32 function, acpi_physical_address addr,
			       u32 bits, acpi_integer *value,
			       void *handler_context, void *region_context)
{
	return acpi_ffh_address_space_arch_handler(value, region_context);
}

void __init acpi_init_ffh(void)
{
	acpi_status status;

	status = acpi_install_address_space_handler(ACPI_ROOT_OBJECT,
						    ACPI_ADR_SPACE_FIXED_HARDWARE,
						    &acpi_ffh_address_space_handler,
						    &acpi_ffh_address_space_setup,
						    &ffh_ctx);
	if (ACPI_FAILURE(status))
		pr_alert("OperationRegion handler could not be installed\n");
}
