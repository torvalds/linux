// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2005 Intel Corporation
 * Copyright (C) 2009 Hewlett-Packard Development Company, L.P.
 *
 *      Venkatesh Pallipadi <venkatesh.pallipadi@intel.com>
 *      - Added _PDC for platforms with Intel CPUs
 */

#define pr_fmt(fmt) "ACPI: " fmt

#include <linux/slab.h>
#include <linux/acpi.h>
#include <acpi/processor.h>

#include "internal.h"

static void acpi_set_pdc_bits(u32 *buf)
{
	buf[0] = ACPI_PDC_REVISION_ID;
	buf[1] = 1;

	/* Enable coordination with firmware's _TSD info */
	buf[2] = ACPI_PDC_SMP_T_SWCOORD;

	/* Twiddle arch-specific bits needed for _PDC */
	arch_acpi_set_proc_cap_bits(&buf[2]);
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
		acpi_handle_debug(handle,
		    "Could not evaluate _PDC, using legacy perf control\n");

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

void __init acpi_early_processor_set_pdc(void)
{
	acpi_proc_quirk_mwait_check();

	acpi_walk_namespace(ACPI_TYPE_PROCESSOR, ACPI_ROOT_OBJECT,
			    ACPI_UINT32_MAX,
			    early_init_pdc, NULL, NULL, NULL);
	acpi_get_devices(ACPI_PROCESSOR_DEVICE_HID, early_init_pdc, NULL, NULL);
}
