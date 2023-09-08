// SPDX-License-Identifier: GPL-2.0
/*
 * Author: Huacai Chen <chenhuacai@loongson.cn>
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#include <linux/acpi.h>
#include <linux/platform_device.h>

#include <asm/bootinfo.h>
#include <asm/loongson.h>

void enable_gpe_wakeup(void)
{
	if (acpi_disabled)
	       return;

	if (acpi_gbl_reduced_hardware)
	       return;

	acpi_enable_all_wakeup_gpes();
}

void enable_pci_wakeup(void)
{
	if (acpi_disabled)
	       return;

	if (acpi_gbl_reduced_hardware)
	       return;

	acpi_write_bit_register(ACPI_BITREG_PCIEXP_WAKE_STATUS, 1);

	if (acpi_gbl_FADT.flags & ACPI_FADT_PCI_EXPRESS_WAKE)
		acpi_write_bit_register(ACPI_BITREG_PCIEXP_WAKE_DISABLE, 0);
}

static int __init loongson3_acpi_suspend_init(void)
{
#ifdef CONFIG_ACPI
	acpi_status status;
	uint64_t suspend_addr = 0;

	if (acpi_disabled || acpi_gbl_reduced_hardware)
		return 0;

	acpi_write_bit_register(ACPI_BITREG_SCI_ENABLE, 1);
	status = acpi_evaluate_integer(NULL, "\\SADR", NULL, &suspend_addr);
	if (ACPI_FAILURE(status) || !suspend_addr) {
		pr_err("ACPI S3 is not support!\n");
		return -1;
	}
	loongson_sysconf.suspend_addr = (u64)phys_to_virt(PHYSADDR(suspend_addr));
#endif
	return 0;
}

device_initcall(loongson3_acpi_suspend_init);
