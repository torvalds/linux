/*
 *  ARM64 Specific Low-Level ACPI Boot Support
 *
 *  Copyright (C) 2013-2014, Linaro Ltd.
 *	Author: Al Stone <al.stone@linaro.org>
 *	Author: Graeme Gregory <graeme.gregory@linaro.org>
 *	Author: Hanjun Guo <hanjun.guo@linaro.org>
 *	Author: Tomasz Nowicki <tomasz.nowicki@linaro.org>
 *	Author: Naresh Bhat <naresh.bhat@linaro.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#define pr_fmt(fmt) "ACPI: " fmt

#include <linux/acpi.h>
#include <linux/bootmem.h>
#include <linux/cpumask.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/memblock.h>
#include <linux/of_fdt.h>
#include <linux/smp.h>

#include <asm/cputype.h>
#include <asm/cpu_ops.h>
#include <asm/smp_plat.h>

int acpi_noirq = 1;		/* skip ACPI IRQ initialization */
int acpi_disabled = 1;
EXPORT_SYMBOL(acpi_disabled);

int acpi_pci_disabled = 1;	/* skip ACPI PCI scan and IRQ initialization */
EXPORT_SYMBOL(acpi_pci_disabled);

/* Processors with enabled flag and sane MPIDR */
static int enabled_cpus;

/* Boot CPU is valid or not in MADT */
static bool bootcpu_valid  __initdata;

static bool param_acpi_off __initdata;
static bool param_acpi_force __initdata;

static int __init parse_acpi(char *arg)
{
	if (!arg)
		return -EINVAL;

	/* "acpi=off" disables both ACPI table parsing and interpreter */
	if (strcmp(arg, "off") == 0)
		param_acpi_off = true;
	else if (strcmp(arg, "force") == 0) /* force ACPI to be enabled */
		param_acpi_force = true;
	else
		return -EINVAL;	/* Core will print when we return error */

	return 0;
}
early_param("acpi", parse_acpi);

static int __init dt_scan_depth1_nodes(unsigned long node,
				       const char *uname, int depth,
				       void *data)
{
	/*
	 * Return 1 as soon as we encounter a node at depth 1 that is
	 * not the /chosen node.
	 */
	if (depth == 1 && (strcmp(uname, "chosen") != 0))
		return 1;
	return 0;
}

/*
 * Since we're on ARM, the default interrupt routing model
 * clearly has to be GIC.
 */
enum acpi_irq_model_id acpi_irq_model = ACPI_IRQ_MODEL_GIC;

/*
 * __acpi_map_table() will be called before page_init(), so early_ioremap()
 * or early_memremap() should be called here to for ACPI table mapping.
 */
char *__init __acpi_map_table(unsigned long phys, unsigned long size)
{
	if (!size)
		return NULL;

	return early_memremap(phys, size);
}

void __init __acpi_unmap_table(char *map, unsigned long size)
{
	if (!map || !size)
		return;

	early_memunmap(map, size);
}

/**
 * acpi_map_gic_cpu_interface - generates a logical cpu number
 * and map to MPIDR represented by GICC structure
 * @mpidr: CPU's hardware id to register, MPIDR represented in MADT
 * @enabled: this cpu is enabled or not
 *
 * Returns the logical cpu number which maps to MPIDR
 */
static int __init acpi_map_gic_cpu_interface(u64 mpidr, u8 enabled)
{
	int i;

	if (mpidr == INVALID_HWID) {
		pr_info("Skip MADT cpu entry with invalid MPIDR\n");
		return -EINVAL;
	}

	total_cpus++;
	if (!enabled)
		return -EINVAL;

	if (enabled_cpus >=  NR_CPUS) {
		pr_warn("NR_CPUS limit of %d reached, Processor %d/0x%llx ignored.\n",
			NR_CPUS, total_cpus, mpidr);
		return -EINVAL;
	}

	/* Check if GICC structure of boot CPU is available in the MADT */
	if (cpu_logical_map(0) == mpidr) {
		if (bootcpu_valid) {
			pr_err("Firmware bug, duplicate CPU MPIDR: 0x%llx in MADT\n",
			       mpidr);
			return -EINVAL;
		}

		bootcpu_valid = true;
	}

	/*
	 * Duplicate MPIDRs are a recipe for disaster. Scan
	 * all initialized entries and check for
	 * duplicates. If any is found just ignore the CPU.
	 */
	for (i = 1; i < enabled_cpus; i++) {
		if (cpu_logical_map(i) == mpidr) {
			pr_err("Firmware bug, duplicate CPU MPIDR: 0x%llx in MADT\n",
			       mpidr);
			return -EINVAL;
		}
	}

	if (!acpi_psci_present())
		return -EOPNOTSUPP;

	cpu_ops[enabled_cpus] = cpu_get_ops("psci");
	/* CPU 0 was already initialized */
	if (enabled_cpus) {
		if (!cpu_ops[enabled_cpus])
			return -EINVAL;

		if (cpu_ops[enabled_cpus]->cpu_init(NULL, enabled_cpus))
			return -EOPNOTSUPP;

		/* map the logical cpu id to cpu MPIDR */
		cpu_logical_map(enabled_cpus) = mpidr;
	}

	enabled_cpus++;
	return enabled_cpus;
}

static int __init
acpi_parse_gic_cpu_interface(struct acpi_subtable_header *header,
				const unsigned long end)
{
	struct acpi_madt_generic_interrupt *processor;

	processor = (struct acpi_madt_generic_interrupt *)header;

	if (BAD_MADT_ENTRY(processor, end))
		return -EINVAL;

	acpi_table_print_madt_entry(header);

	acpi_map_gic_cpu_interface(processor->arm_mpidr & MPIDR_HWID_BITMASK,
		processor->flags & ACPI_MADT_ENABLED);

	return 0;
}

/* Parse GIC cpu interface entries in MADT for SMP init */
void __init acpi_init_cpus(void)
{
	int count, i;

	/*
	 * do a partial walk of MADT to determine how many CPUs
	 * we have including disabled CPUs, and get information
	 * we need for SMP init
	 */
	count = acpi_table_parse_madt(ACPI_MADT_TYPE_GENERIC_INTERRUPT,
			acpi_parse_gic_cpu_interface, 0);

	if (!count) {
		pr_err("No GIC CPU interface entries present\n");
		return;
	} else if (count < 0) {
		pr_err("Error parsing GIC CPU interface entry\n");
		return;
	}

	if (!bootcpu_valid) {
		pr_err("MADT missing boot CPU MPIDR, not enabling secondaries\n");
		return;
	}

	for (i = 0; i < enabled_cpus; i++)
		set_cpu_possible(i, true);

	/* Make boot-up look pretty */
	pr_info("%d CPUs enabled, %d CPUs total\n", enabled_cpus, total_cpus);
}

int acpi_gsi_to_irq(u32 gsi, unsigned int *irq)
{
	*irq = irq_find_mapping(NULL, gsi);

	return 0;
}
EXPORT_SYMBOL_GPL(acpi_gsi_to_irq);

/*
 * success: return IRQ number (>0)
 * failure: return =< 0
 */
int acpi_register_gsi(struct device *dev, u32 gsi, int trigger, int polarity)
{
	unsigned int irq;
	unsigned int irq_type;

	/*
	 * ACPI have no bindings to indicate SPI or PPI, so we
	 * use different mappings from DT in ACPI.
	 *
	 * For FDT
	 * PPI interrupt: in the range [0, 15];
	 * SPI interrupt: in the range [0, 987];
	 *
	 * For ACPI, GSI should be unique so using
	 * the hwirq directly for the mapping:
	 * PPI interrupt: in the range [16, 31];
	 * SPI interrupt: in the range [32, 1019];
	 */

	if (trigger == ACPI_EDGE_SENSITIVE &&
				polarity == ACPI_ACTIVE_LOW)
		irq_type = IRQ_TYPE_EDGE_FALLING;
	else if (trigger == ACPI_EDGE_SENSITIVE &&
				polarity == ACPI_ACTIVE_HIGH)
		irq_type = IRQ_TYPE_EDGE_RISING;
	else if (trigger == ACPI_LEVEL_SENSITIVE &&
				polarity == ACPI_ACTIVE_LOW)
		irq_type = IRQ_TYPE_LEVEL_LOW;
	else if (trigger == ACPI_LEVEL_SENSITIVE &&
				polarity == ACPI_ACTIVE_HIGH)
		irq_type = IRQ_TYPE_LEVEL_HIGH;
	else
		irq_type = IRQ_TYPE_NONE;

	/*
	 * Since only one GIC is supported in ACPI 5.0, we can
	 * create mapping refer to the default domain
	 */
	irq = irq_create_mapping(NULL, gsi);
	if (!irq)
		return irq;

	/* Set irq type if specified and different than the current one */
	if (irq_type != IRQ_TYPE_NONE &&
		irq_type != irq_get_trigger_type(irq))
		irq_set_irq_type(irq, irq_type);
	return irq;
}
EXPORT_SYMBOL_GPL(acpi_register_gsi);

void acpi_unregister_gsi(u32 gsi)
{
}
EXPORT_SYMBOL_GPL(acpi_unregister_gsi);

static int __init acpi_parse_fadt(struct acpi_table_header *table)
{
	struct acpi_table_fadt *fadt = (struct acpi_table_fadt *)table;

	/*
	 * Revision in table header is the FADT Major revision, and there
	 * is a minor revision of FADT which was introduced by ACPI 5.1,
	 * we only deal with ACPI 5.1 or newer revision to get GIC and SMP
	 * boot protocol configuration data, or we will disable ACPI.
	 */
	if (table->revision > 5 ||
	    (table->revision == 5 && fadt->minor_revision >= 1)) {
		/*
		 * ACPI 5.1 only has two explicit methods to boot up SMP,
		 * PSCI and Parking protocol, but the Parking protocol is
		 * only specified for ARMv7 now, so make PSCI as the only
		 * way for the SMP boot protocol before some updates for
		 * the Parking protocol spec.
		 */
		if (acpi_psci_present())
			return 0;

		pr_warn("No PSCI support, will not bring up secondary CPUs\n");
		return -EOPNOTSUPP;
	}

	pr_warn("Unsupported FADT revision %d.%d, should be 5.1+, will disable ACPI\n",
		table->revision, fadt->minor_revision);
	disable_acpi();

	return -EINVAL;
}

/*
 * acpi_boot_table_init() called from setup_arch(), always.
 *	1. find RSDP and get its address, and then find XSDT
 *	2. extract all tables and checksums them all
 *	3. check ACPI FADT revision
 *
 * We can parse ACPI boot-time tables such as MADT after
 * this function is called.
 */
void __init acpi_boot_table_init(void)
{
	/*
	 * Enable ACPI instead of device tree unless
	 * - ACPI has been disabled explicitly (acpi=off), or
	 * - the device tree is not empty (it has more than just a /chosen node)
	 *   and ACPI has not been force enabled (acpi=force)
	 */
	if (param_acpi_off ||
	    (!param_acpi_force && of_scan_flat_dt(dt_scan_depth1_nodes, NULL)))
		return;

	enable_acpi();

	/* Initialize the ACPI boot-time table parser. */
	if (acpi_table_init()) {
		disable_acpi();
		return;
	}

	if (acpi_table_parse(ACPI_SIG_FADT, acpi_parse_fadt)) {
		/* disable ACPI if no FADT is found */
		disable_acpi();
		pr_err("Can't find FADT\n");
	}
}

void __init acpi_gic_init(void)
{
	struct acpi_table_header *table;
	acpi_status status;
	acpi_size tbl_size;
	int err;

	if (acpi_disabled)
		return;

	status = acpi_get_table_with_size(ACPI_SIG_MADT, 0, &table, &tbl_size);
	if (ACPI_FAILURE(status)) {
		const char *msg = acpi_format_exception(status);

		pr_err("Failed to get MADT table, %s\n", msg);
		return;
	}

	err = gic_v2_acpi_init(table);
	if (err)
		pr_err("Failed to initialize GIC IRQ controller");

	early_acpi_os_unmap_memory((char *)table, tbl_size);
}
