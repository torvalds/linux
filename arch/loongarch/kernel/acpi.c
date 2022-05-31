// SPDX-License-Identifier: GPL-2.0
/*
 * acpi.c - Architecture-Specific Low-Level ACPI Boot Support
 *
 * Author: Jianmin Lv <lvjianmin@loongson.cn>
 *         Huacai Chen <chenhuacai@loongson.cn>
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */

#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/memblock.h>
#include <linux/serial_core.h>
#include <asm/io.h>
#include <asm/loongson.h>

int acpi_disabled;
EXPORT_SYMBOL(acpi_disabled);
int acpi_noirq;
int acpi_pci_disabled;
EXPORT_SYMBOL(acpi_pci_disabled);
int acpi_strict = 1; /* We have no workarounds on LoongArch */
int num_processors;
int disabled_cpus;
enum acpi_irq_model_id acpi_irq_model = ACPI_IRQ_MODEL_PLATFORM;

u64 acpi_saved_sp;

#define MAX_CORE_PIC 256

#define PREFIX			"ACPI: "

int acpi_gsi_to_irq(u32 gsi, unsigned int *irqp)
{
	if (irqp != NULL)
		*irqp = acpi_register_gsi(NULL, gsi, -1, -1);
	return (*irqp >= 0) ? 0 : -EINVAL;
}
EXPORT_SYMBOL_GPL(acpi_gsi_to_irq);

int acpi_isa_irq_to_gsi(unsigned int isa_irq, u32 *gsi)
{
	if (gsi)
		*gsi = isa_irq;
	return 0;
}

/*
 * success: return IRQ number (>=0)
 * failure: return < 0
 */
int acpi_register_gsi(struct device *dev, u32 gsi, int trigger, int polarity)
{
	struct irq_fwspec fwspec;

	switch (gsi) {
	case GSI_MIN_CPU_IRQ ... GSI_MAX_CPU_IRQ:
		fwspec.fwnode = liointc_domain->fwnode;
		fwspec.param[0] = gsi - GSI_MIN_CPU_IRQ;
		fwspec.param_count = 1;

		return irq_create_fwspec_mapping(&fwspec);

	case GSI_MIN_LPC_IRQ ... GSI_MAX_LPC_IRQ:
		if (!pch_lpc_domain)
			return -EINVAL;

		fwspec.fwnode = pch_lpc_domain->fwnode;
		fwspec.param[0] = gsi - GSI_MIN_LPC_IRQ;
		fwspec.param[1] = acpi_dev_get_irq_type(trigger, polarity);
		fwspec.param_count = 2;

		return irq_create_fwspec_mapping(&fwspec);

	case GSI_MIN_PCH_IRQ ... GSI_MAX_PCH_IRQ:
		if (!pch_pic_domain[0])
			return -EINVAL;

		fwspec.fwnode = pch_pic_domain[0]->fwnode;
		fwspec.param[0] = gsi - GSI_MIN_PCH_IRQ;
		fwspec.param[1] = IRQ_TYPE_LEVEL_HIGH;
		fwspec.param_count = 2;

		return irq_create_fwspec_mapping(&fwspec);
	}

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(acpi_register_gsi);

void acpi_unregister_gsi(u32 gsi)
{

}
EXPORT_SYMBOL_GPL(acpi_unregister_gsi);

void __init __iomem * __acpi_map_table(unsigned long phys, unsigned long size)
{

	if (!phys || !size)
		return NULL;

	return early_memremap(phys, size);
}
void __init __acpi_unmap_table(void __iomem *map, unsigned long size)
{
	if (!map || !size)
		return;

	early_memunmap(map, size);
}

void __init __iomem *acpi_os_ioremap(acpi_physical_address phys, acpi_size size)
{
	if (!memblock_is_memory(phys))
		return ioremap(phys, size);
	else
		return ioremap_cache(phys, size);
}

void __init acpi_boot_table_init(void)
{
	/*
	 * If acpi_disabled, bail out
	 */
	if (acpi_disabled)
		return;

	/*
	 * Initialize the ACPI boot-time table parser.
	 */
	if (acpi_table_init()) {
		disable_acpi();
		return;
	}
}

static void __init acpi_process_madt(void)
{
	loongson_sysconf.nr_cpus = num_processors;
}

int __init acpi_boot_init(void)
{
	/*
	 * If acpi_disabled, bail out
	 */
	if (acpi_disabled)
		return -1;

	loongson_sysconf.boot_cpu_id = read_csr_cpuid();

	/*
	 * Process the Multiple APIC Description Table (MADT), if present
	 */
	acpi_process_madt();

	/* Do not enable ACPI SPCR console by default */
	acpi_parse_spcr(earlycon_acpi_spcr_enable, false);

	return 0;
}

void __init arch_reserve_mem_area(acpi_physical_address addr, size_t size)
{
	memblock_reserve(addr, size);
}
