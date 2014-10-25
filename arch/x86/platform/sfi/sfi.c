/*
 * sfi.c - x86 architecture SFI support.
 *
 * Copyright (c) 2009, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#define KMSG_COMPONENT "SFI"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/acpi.h>
#include <linux/init.h>
#include <linux/sfi.h>
#include <linux/io.h>
#include <linux/irqdomain.h>

#include <asm/io_apic.h>
#include <asm/mpspec.h>
#include <asm/setup.h>
#include <asm/apic.h>

#ifdef CONFIG_X86_LOCAL_APIC
static unsigned long sfi_lapic_addr __initdata = APIC_DEFAULT_PHYS_BASE;

/* All CPUs enumerated by SFI must be present and enabled */
static void __init mp_sfi_register_lapic(u8 id)
{
	if (MAX_LOCAL_APIC - id <= 0) {
		pr_warning("Processor #%d invalid (max %d)\n",
			id, MAX_LOCAL_APIC);
		return;
	}

	pr_info("registering lapic[%d]\n", id);

	generic_processor_info(id, GET_APIC_VERSION(apic_read(APIC_LVR)));
}

static int __init sfi_parse_cpus(struct sfi_table_header *table)
{
	struct sfi_table_simple *sb;
	struct sfi_cpu_table_entry *pentry;
	int i;
	int cpu_num;

	sb = (struct sfi_table_simple *)table;
	cpu_num = SFI_GET_NUM_ENTRIES(sb, struct sfi_cpu_table_entry);
	pentry = (struct sfi_cpu_table_entry *)sb->pentry;

	for (i = 0; i < cpu_num; i++) {
		mp_sfi_register_lapic(pentry->apic_id);
		pentry++;
	}

	smp_found_config = 1;
	return 0;
}
#endif /* CONFIG_X86_LOCAL_APIC */

#ifdef CONFIG_X86_IO_APIC
static struct irq_domain_ops sfi_ioapic_irqdomain_ops = {
	.map = mp_irqdomain_map,
};

static int __init sfi_parse_ioapic(struct sfi_table_header *table)
{
	struct sfi_table_simple *sb;
	struct sfi_apic_table_entry *pentry;
	int i, num;
	struct ioapic_domain_cfg cfg = {
		.type = IOAPIC_DOMAIN_STRICT,
		.ops = &sfi_ioapic_irqdomain_ops,
	};

	sb = (struct sfi_table_simple *)table;
	num = SFI_GET_NUM_ENTRIES(sb, struct sfi_apic_table_entry);
	pentry = (struct sfi_apic_table_entry *)sb->pentry;

	for (i = 0; i < num; i++) {
		mp_register_ioapic(i, pentry->phys_addr, gsi_top, &cfg);
		pentry++;
	}

	WARN(pic_mode, KERN_WARNING
		"SFI: pic_mod shouldn't be 1 when IOAPIC table is present\n");
	pic_mode = 0;
	return 0;
}
#endif /* CONFIG_X86_IO_APIC */

/*
 * sfi_platform_init(): register lapics & io-apics
 */
int __init sfi_platform_init(void)
{
#ifdef CONFIG_X86_LOCAL_APIC
	register_lapic_address(sfi_lapic_addr);
	sfi_table_parse(SFI_SIG_CPUS, NULL, NULL, sfi_parse_cpus);
#endif
#ifdef CONFIG_X86_IO_APIC
	sfi_table_parse(SFI_SIG_APIC, NULL, NULL, sfi_parse_ioapic);
#endif
	return 0;
}
