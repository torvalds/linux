/*
 * arch/i386/kernel/acpi/cstate.c
 *
 * Copyright (C) 2005 Intel Corporation
 * 	Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>
 * 	- Added _PDC for SMP C-states on Intel CPUs
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/acpi.h>

#include <acpi/processor.h>
#include <asm/acpi.h>

static void acpi_processor_power_init_intel_pdc(struct acpi_processor_power
						*pow)
{
	struct acpi_object_list *obj_list;
	union acpi_object *obj;
	u32 *buf;

	/* allocate and initialize pdc. It will be used later. */
	obj_list = kmalloc(sizeof(struct acpi_object_list), GFP_KERNEL);
	if (!obj_list) {
		printk(KERN_ERR "Memory allocation error\n");
		return;
	}

	obj = kmalloc(sizeof(union acpi_object), GFP_KERNEL);
	if (!obj) {
		printk(KERN_ERR "Memory allocation error\n");
		kfree(obj_list);
		return;
	}

	buf = kmalloc(12, GFP_KERNEL);
	if (!buf) {
		printk(KERN_ERR "Memory allocation error\n");
		kfree(obj);
		kfree(obj_list);
		return;
	}

	buf[0] = ACPI_PDC_REVISION_ID;
	buf[1] = 1;
	buf[2] = ACPI_PDC_C_CAPABILITY_SMP;

	obj->type = ACPI_TYPE_BUFFER;
	obj->buffer.length = 12;
	obj->buffer.pointer = (u8 *) buf;
	obj_list->count = 1;
	obj_list->pointer = obj;
	pow->pdc = obj_list;

	return;
}

/* Initialize _PDC data based on the CPU vendor */
void acpi_processor_power_init_pdc(struct acpi_processor_power *pow,
				   unsigned int cpu)
{
	struct cpuinfo_x86 *c = cpu_data + cpu;

	pow->pdc = NULL;
	if (c->x86_vendor == X86_VENDOR_INTEL)
		acpi_processor_power_init_intel_pdc(pow);

	return;
}

EXPORT_SYMBOL(acpi_processor_power_init_pdc);

/*
 * Initialize bm_flags based on the CPU cache properties
 * On SMP it depends on cache configuration
 * - When cache is not shared among all CPUs, we flush cache
 *   before entering C3.
 * - When cache is shared among all CPUs, we use bm_check
 *   mechanism as in UP case
 *
 * This routine is called only after all the CPUs are online
 */
void acpi_processor_power_init_bm_check(struct acpi_processor_flags *flags,
					unsigned int cpu)
{
	struct cpuinfo_x86 *c = cpu_data + cpu;

	flags->bm_check = 0;
	if (num_online_cpus() == 1)
		flags->bm_check = 1;
	else if (c->x86_vendor == X86_VENDOR_INTEL) {
		/*
		 * Today all CPUs that support C3 share cache.
		 * TBD: This needs to look at cache shared map, once
		 * multi-core detection patch makes to the base.
		 */
		flags->bm_check = 1;
	}
}

EXPORT_SYMBOL(acpi_processor_power_init_bm_check);
