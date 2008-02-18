/*
 * arch/i386/kernel/acpi/processor.c
 *
 * Copyright (C) 2005 Intel Corporation
 * 	Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>
 * 	- Added _PDC for platforms with Intel CPUs
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/acpi.h>

#include <acpi/processor.h>
#include <asm/acpi.h>

static void init_intel_pdc(struct acpi_processor *pr, struct cpuinfo_x86 *c)
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

	/*
	 * The default of PDC_SMP_T_SWCOORD bit is set for intel x86 cpu so
	 * that OSPM is capable of native ACPI throttling software
	 * coordination using BIOS supplied _TSD info.
	 */
	buf[2] |= ACPI_PDC_SMP_T_SWCOORD;
	if (cpu_has(c, X86_FEATURE_EST))
		buf[2] |= ACPI_PDC_EST_CAPABILITY_SWSMP;

	if (cpu_has(c, X86_FEATURE_ACPI))
		buf[2] |= ACPI_PDC_T_FFH;

	obj->type = ACPI_TYPE_BUFFER;
	obj->buffer.length = 12;
	obj->buffer.pointer = (u8 *) buf;
	obj_list->count = 1;
	obj_list->pointer = obj;
	pr->pdc = obj_list;

	return;
}

/* Initialize _PDC data based on the CPU vendor */
void arch_acpi_processor_init_pdc(struct acpi_processor *pr)
{
	struct cpuinfo_x86 *c = &cpu_data(pr->id);

	pr->pdc = NULL;
	if (c->x86_vendor == X86_VENDOR_INTEL)
		init_intel_pdc(pr, c);

	return;
}

EXPORT_SYMBOL(arch_acpi_processor_init_pdc);
