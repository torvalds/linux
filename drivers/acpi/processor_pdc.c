/*
 * Copyright (C) 2005 Intel Corporation
 * Copyright (C) 2009 Hewlett-Packard Development Company, L.P.
 *
 *      Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>
 *      - Added _PDC for platforms with Intel CPUs
 */

#define pr_fmt(fmt) "ACPI: " fmt

#include <linux/dmi.h>
#include <linux/slab.h>
#include <linux/acpi.h>
#include <acpi/processor.h>

#include "internal.h"

#define _COMPONENT              ACPI_PROCESSOR_COMPONENT
ACPI_MODULE_NAME("processor_pdc");

static bool __init processor_physically_present(acpi_handle handle)
{
	int cpuid, type;
	u32 acpi_id;
	acpi_status status;
	acpi_object_type acpi_type;
	unsigned long long tmp;
	union acpi_object object = { 0 };
	struct acpi_buffer buffer = { sizeof(union acpi_object), &object };

	status = acpi_get_type(handle, &acpi_type);
	if (ACPI_FAILURE(status))
		return false;

	switch (acpi_type) {
	case ACPI_TYPE_PROCESSOR:
		status = acpi_evaluate_object(handle, NULL, NULL, &buffer);
		if (ACPI_FAILURE(status))
			return false;
		acpi_id = object.processor.proc_id;
		break;
	case ACPI_TYPE_DEVICE:
		status = acpi_evaluate_integer(handle, "_UID", NULL, &tmp);
		if (ACPI_FAILURE(status))
			return false;
		acpi_id = tmp;
		break;
	default:
		return false;
	}

	type = (acpi_type == ACPI_TYPE_DEVICE) ? 1 : 0;
	cpuid = acpi_get_cpuid(handle, type, acpi_id);

	if (cpuid == -1)
		return false;

	return true;
}

static void acpi_set_pdc_bits(u32 *buf)
{
	buf[0] = ACPI_PDC_REVISION_ID;
	buf[1] = 1;

	/* Enable coordination with firmware's _TSD info */
	buf[2] = ACPI_PDC_SMP_T_SWCOORD;

	/* Twiddle arch-specific bits needed for _PDC */
	arch_acpi_set_pdc_bits(buf);
}

static struct acpi_object_list *acpi_processor_alloc_pdc(void)
{
	struct acpi_object_list *obj_list;
	union acpi_object *obj;
	u32 *buf;

	/* allocate and initialize pdc. It will be used later. */
	obj_list = kmalloc(sizeof(struct acpi_object_list), GFP_KERNEL);
	if (!obj_list)
		goto out;

	obj = kmalloc(sizeof(union acpi_object), GFP_KERNEL);
	if (!obj) {
		kfree(obj_list);
		goto out;
	}

	buf = kmalloc(12, GFP_KERNEL);
	if (!buf) {
		kfree(obj);
		kfree(obj_list);
		goto out;
	}

	acpi_set_pdc_bits(buf);

	obj->type = ACPI_TYPE_BUFFER;
	obj->buffer.length = 12;
	obj->buffer.pointer = (u8 *) buf;
	obj_list->count = 1;
	obj_list->pointer = obj;

	return obj_list;
out:
	pr_err("Memory allocation error\n");
	return NULL;
}

/*
 * _PDC is required for a BIOS-OS handshake for most of the newer
 * ACPI processor features.
 */
static acpi_status
acpi_processor_eval_pdc(acpi_handle handle, struct acpi_object_list *pdc_in)
{
	acpi_status status = AE_OK;

	if (boot_option_idle_override == IDLE_NOMWAIT) {
		/*
		 * If mwait is disabled for CPU C-states, the C2C3_FFH access
		 * mode will be disabled in the parameter of _PDC object.
		 * Of course C1_FFH access mode will also be disabled.
		 */
		union acpi_object *obj;
		u32 *buffer = NULL;

		obj = pdc_in->pointer;
		buffer = (u32 *)(obj->buffer.pointer);
		buffer[2] &= ~(ACPI_PDC_C_C2C3_FFH | ACPI_PDC_C_C1_FFH);

	}
	status = acpi_evaluate_object(handle, "_PDC", pdc_in, NULL);

	if (ACPI_FAILURE(status))
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
		    "Could not evaluate _PDC, using legacy perf. control.\n"));

	return status;
}

void acpi_processor_set_pdc(acpi_handle handle)
{
	struct acpi_object_list *obj_list;

	if (arch_has_acpi_pdc() == false)
		return;

	obj_list = acpi_processor_alloc_pdc();
	if (!obj_list)
		return;

	acpi_processor_eval_pdc(handle, obj_list);

	kfree(obj_list->pointer->buffer.pointer);
	kfree(obj_list->pointer);
	kfree(obj_list);
}

static acpi_status __init
early_init_pdc(acpi_handle handle, u32 lvl, void *context, void **rv)
{
	if (processor_physically_present(handle) == false)
		return AE_OK;

	acpi_processor_set_pdc(handle);
	return AE_OK;
}

static int __init set_no_mwait(const struct dmi_system_id *id)
{
	pr_notice("%s detected - disabling mwait for CPU C-states\n",
		  id->ident);
	boot_option_idle_override = IDLE_NOMWAIT;
	return 0;
}

static struct dmi_system_id processor_idle_dmi_table[] __initdata = {
	{
	set_no_mwait, "Extensa 5220", {
	DMI_MATCH(DMI_BIOS_VENDOR, "Phoenix Technologies LTD"),
	DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
	DMI_MATCH(DMI_PRODUCT_VERSION, "0100"),
	DMI_MATCH(DMI_BOARD_NAME, "Columbia") }, NULL},
	{},
};

static void __init processor_dmi_check(void)
{
	/*
	 * Check whether the system is DMI table. If yes, OSPM
	 * should not use mwait for CPU-states.
	 */
	dmi_check_system(processor_idle_dmi_table);
}

void __init acpi_early_processor_set_pdc(void)
{
	processor_dmi_check();

	acpi_walk_namespace(ACPI_TYPE_PROCESSOR, ACPI_ROOT_OBJECT,
			    ACPI_UINT32_MAX,
			    early_init_pdc, NULL, NULL, NULL);
	acpi_get_devices(ACPI_PROCESSOR_DEVICE_HID, early_init_pdc, NULL, NULL);
}
