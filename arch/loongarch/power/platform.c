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

static struct platform_device loongson3_cpufreq_device = {
	.name = "loongson3_cpufreq",
	.id = -1,
};

static int __init loongson_cpufreq_init(void)
{
	if (!cpu_has_scalefreq)
		return -ENODEV;

	return platform_device_register(&loongson3_cpufreq_device);
}

arch_initcall(loongson_cpufreq_init);

static void default_suspend_addr(void)
{
	acpi_enter_sleep_state(ACPI_STATE_S3);
}

static int __init loongson3_acpi_suspend_init(void)
{
#ifdef CONFIG_ACPI
	acpi_status status;
	uint64_t suspend_addr = 0;

	if (acpi_disabled)
		return 0;

	if (!acpi_gbl_reduced_hardware)
		acpi_write_bit_register(ACPI_BITREG_SCI_ENABLE, 1);

	if (!acpi_sleep_state_supported(ACPI_STATE_S3))
		return 0;

	status = acpi_evaluate_integer(NULL, "\\SADR", NULL, &suspend_addr);
	if (ACPI_FAILURE(status) || !suspend_addr) {
		pr_info("ACPI S3 supported with hardware register default\n");
		loongson_sysconf.suspend_addr = (u64)default_suspend_addr;
	} else {
		pr_info("ACPI S3 supported with Loongson ACPI SADR extension\n");
		loongson_sysconf.suspend_addr = (u64)phys_to_virt(PHYSADDR(suspend_addr));
	}
#endif
	return 0;
}

device_initcall(loongson3_acpi_suspend_init);
