/*
 * acpi_processor.c - ACPI processor enumeration support
 *
 * Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 * Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 * Copyright (C) 2004       Dominik Brodowski <linux@brodo.de>
 * Copyright (C) 2004  Anil S Keshavamurthy <anil.s.keshavamurthy@intel.com>
 * Copyright (C) 2013, Intel Corporation
 *                     Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>

#include <acpi/processor.h>

#include <asm/cpu.h>

#include "internal.h"

#define _COMPONENT	ACPI_PROCESSOR_COMPONENT

ACPI_MODULE_NAME("processor");

DEFINE_PER_CPU(struct acpi_processor *, processors);
EXPORT_PER_CPU_SYMBOL(processors);

/* --------------------------------------------------------------------------
                                Errata Handling
   -------------------------------------------------------------------------- */

struct acpi_processor_errata errata __read_mostly;
EXPORT_SYMBOL_GPL(errata);

static int acpi_processor_errata_piix4(struct pci_dev *dev)
{
	u8 value1 = 0;
	u8 value2 = 0;


	if (!dev)
		return -EINVAL;

	/*
	 * Note that 'dev' references the PIIX4 ACPI Controller.
	 */

	switch (dev->revision) {
	case 0:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Found PIIX4 A-step\n"));
		break;
	case 1:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Found PIIX4 B-step\n"));
		break;
	case 2:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Found PIIX4E\n"));
		break;
	case 3:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Found PIIX4M\n"));
		break;
	default:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Found unknown PIIX4\n"));
		break;
	}

	switch (dev->revision) {

	case 0:		/* PIIX4 A-step */
	case 1:		/* PIIX4 B-step */
		/*
		 * See specification changes #13 ("Manual Throttle Duty Cycle")
		 * and #14 ("Enabling and Disabling Manual Throttle"), plus
		 * erratum #5 ("STPCLK# Deassertion Time") from the January
		 * 2002 PIIX4 specification update.  Applies to only older
		 * PIIX4 models.
		 */
		errata.piix4.throttle = 1;

	case 2:		/* PIIX4E */
	case 3:		/* PIIX4M */
		/*
		 * See erratum #18 ("C3 Power State/BMIDE and Type-F DMA
		 * Livelock") from the January 2002 PIIX4 specification update.
		 * Applies to all PIIX4 models.
		 */

		/*
		 * BM-IDE
		 * ------
		 * Find the PIIX4 IDE Controller and get the Bus Master IDE
		 * Status register address.  We'll use this later to read
		 * each IDE controller's DMA status to make sure we catch all
		 * DMA activity.
		 */
		dev = pci_get_subsys(PCI_VENDOR_ID_INTEL,
				     PCI_DEVICE_ID_INTEL_82371AB,
				     PCI_ANY_ID, PCI_ANY_ID, NULL);
		if (dev) {
			errata.piix4.bmisx = pci_resource_start(dev, 4);
			pci_dev_put(dev);
		}

		/*
		 * Type-F DMA
		 * ----------
		 * Find the PIIX4 ISA Controller and read the Motherboard
		 * DMA controller's status to see if Type-F (Fast) DMA mode
		 * is enabled (bit 7) on either channel.  Note that we'll
		 * disable C3 support if this is enabled, as some legacy
		 * devices won't operate well if fast DMA is disabled.
		 */
		dev = pci_get_subsys(PCI_VENDOR_ID_INTEL,
				     PCI_DEVICE_ID_INTEL_82371AB_0,
				     PCI_ANY_ID, PCI_ANY_ID, NULL);
		if (dev) {
			pci_read_config_byte(dev, 0x76, &value1);
			pci_read_config_byte(dev, 0x77, &value2);
			if ((value1 & 0x80) || (value2 & 0x80))
				errata.piix4.fdma = 1;
			pci_dev_put(dev);
		}

		break;
	}

	if (errata.piix4.bmisx)
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Bus master activity detection (BM-IDE) erratum enabled\n"));
	if (errata.piix4.fdma)
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Type-F DMA livelock erratum (C3 disabled)\n"));

	return 0;
}

static int acpi_processor_errata(void)
{
	int result = 0;
	struct pci_dev *dev = NULL;

	/*
	 * PIIX4
	 */
	dev = pci_get_subsys(PCI_VENDOR_ID_INTEL,
			     PCI_DEVICE_ID_INTEL_82371AB_3, PCI_ANY_ID,
			     PCI_ANY_ID, NULL);
	if (dev) {
		result = acpi_processor_errata_piix4(dev);
		pci_dev_put(dev);
	}

	return result;
}

/* --------------------------------------------------------------------------
                                Initialization
   -------------------------------------------------------------------------- */

#ifdef CONFIG_ACPI_HOTPLUG_CPU
int __weak acpi_map_cpu(acpi_handle handle,
		phys_cpuid_t physid, u32 acpi_id, int *pcpu)
{
	return -ENODEV;
}

int __weak acpi_unmap_cpu(int cpu)
{
	return -ENODEV;
}

int __weak arch_register_cpu(int cpu)
{
	return -ENODEV;
}

void __weak arch_unregister_cpu(int cpu) {}

static int acpi_processor_hotadd_init(struct acpi_processor *pr)
{
	unsigned long long sta;
	acpi_status status;
	int ret;

	if (invalid_phys_cpuid(pr->phys_id))
		return -ENODEV;

	status = acpi_evaluate_integer(pr->handle, "_STA", NULL, &sta);
	if (ACPI_FAILURE(status) || !(sta & ACPI_STA_DEVICE_PRESENT))
		return -ENODEV;

	cpu_maps_update_begin();
	cpu_hotplug_begin();

	ret = acpi_map_cpu(pr->handle, pr->phys_id, pr->acpi_id, &pr->id);
	if (ret)
		goto out;

	ret = arch_register_cpu(pr->id);
	if (ret) {
		acpi_unmap_cpu(pr->id);
		goto out;
	}

	/*
	 * CPU got hot-added, but cpu_data is not initialized yet.  Set a flag
	 * to delay cpu_idle/throttling initialization and do it when the CPU
	 * gets online for the first time.
	 */
	pr_info("CPU%d has been hot-added\n", pr->id);
	pr->flags.need_hotplug_init = 1;

out:
	cpu_hotplug_done();
	cpu_maps_update_done();
	return ret;
}
#else
static inline int acpi_processor_hotadd_init(struct acpi_processor *pr)
{
	return -ENODEV;
}
#endif /* CONFIG_ACPI_HOTPLUG_CPU */

static int acpi_processor_get_info(struct acpi_device *device)
{
	union acpi_object object = { 0 };
	struct acpi_buffer buffer = { sizeof(union acpi_object), &object };
	struct acpi_processor *pr = acpi_driver_data(device);
	int device_declaration = 0;
	acpi_status status = AE_OK;
	static int cpu0_initialized;
	unsigned long long value;

	acpi_processor_errata();

	/*
	 * Check to see if we have bus mastering arbitration control.  This
	 * is required for proper C3 usage (to maintain cache coherency).
	 */
	if (acpi_gbl_FADT.pm2_control_block && acpi_gbl_FADT.pm2_control_length) {
		pr->flags.bm_control = 1;
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Bus mastering arbitration control present\n"));
	} else
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "No bus mastering arbitration control\n"));

	if (!strcmp(acpi_device_hid(device), ACPI_PROCESSOR_OBJECT_HID)) {
		/* Declared with "Processor" statement; match ProcessorID */
		status = acpi_evaluate_object(pr->handle, NULL, NULL, &buffer);
		if (ACPI_FAILURE(status)) {
			dev_err(&device->dev,
				"Failed to evaluate processor object (0x%x)\n",
				status);
			return -ENODEV;
		}

		pr->acpi_id = object.processor.proc_id;
	} else {
		/*
		 * Declared with "Device" statement; match _UID.
		 * Note that we don't handle string _UIDs yet.
		 */
		status = acpi_evaluate_integer(pr->handle, METHOD_NAME__UID,
						NULL, &value);
		if (ACPI_FAILURE(status)) {
			dev_err(&device->dev,
				"Failed to evaluate processor _UID (0x%x)\n",
				status);
			return -ENODEV;
		}
		device_declaration = 1;
		pr->acpi_id = value;
	}

	if (acpi_duplicate_processor_id(pr->acpi_id)) {
		dev_err(&device->dev,
			"Failed to get unique processor _UID (0x%x)\n",
			pr->acpi_id);
		return -ENODEV;
	}

	pr->phys_id = acpi_get_phys_id(pr->handle, device_declaration,
					pr->acpi_id);
	if (invalid_phys_cpuid(pr->phys_id))
		acpi_handle_debug(pr->handle, "failed to get CPU physical ID.\n");

	pr->id = acpi_map_cpuid(pr->phys_id, pr->acpi_id);
	if (!cpu0_initialized && !acpi_has_cpu_in_madt()) {
		cpu0_initialized = 1;
		/*
		 * Handle UP system running SMP kernel, with no CPU
		 * entry in MADT
		 */
		if (invalid_logical_cpuid(pr->id) && (num_online_cpus() == 1))
			pr->id = 0;
	}

	/*
	 *  Extra Processor objects may be enumerated on MP systems with
	 *  less than the max # of CPUs. They should be ignored _iff
	 *  they are physically not present.
	 *
	 *  NOTE: Even if the processor has a cpuid, it may not be present
	 *  because cpuid <-> apicid mapping is persistent now.
	 */
	if (invalid_logical_cpuid(pr->id) || !cpu_present(pr->id)) {
		int ret = acpi_processor_hotadd_init(pr);
		if (ret)
			return ret;
	}

	/*
	 * On some boxes several processors use the same processor bus id.
	 * But they are located in different scope. For example:
	 * \_SB.SCK0.CPU0
	 * \_SB.SCK1.CPU0
	 * Rename the processor device bus id. And the new bus id will be
	 * generated as the following format:
	 * CPU+CPU ID.
	 */
	sprintf(acpi_device_bid(device), "CPU%X", pr->id);
	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Processor [%d:%d]\n", pr->id,
			  pr->acpi_id));

	if (!object.processor.pblk_address)
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "No PBLK (NULL address)\n"));
	else if (object.processor.pblk_length != 6)
		dev_err(&device->dev, "Invalid PBLK length [%d]\n",
			    object.processor.pblk_length);
	else {
		pr->throttling.address = object.processor.pblk_address;
		pr->throttling.duty_offset = acpi_gbl_FADT.duty_offset;
		pr->throttling.duty_width = acpi_gbl_FADT.duty_width;

		pr->pblk = object.processor.pblk_address;
	}

	/*
	 * If ACPI describes a slot number for this CPU, we can use it to
	 * ensure we get the right value in the "physical id" field
	 * of /proc/cpuinfo
	 */
	status = acpi_evaluate_integer(pr->handle, "_SUN", NULL, &value);
	if (ACPI_SUCCESS(status))
		arch_fix_phys_package_id(pr->id, value);

	return 0;
}

/*
 * Do not put anything in here which needs the core to be online.
 * For example MSR access or setting up things which check for cpuinfo_x86
 * (cpu_data(cpu)) values, like CPU feature flags, family, model, etc.
 * Such things have to be put in and set up by the processor driver's .probe().
 */
static DEFINE_PER_CPU(void *, processor_device_array);

static int acpi_processor_add(struct acpi_device *device,
					const struct acpi_device_id *id)
{
	struct acpi_processor *pr;
	struct device *dev;
	int result = 0;

	pr = kzalloc(sizeof(struct acpi_processor), GFP_KERNEL);
	if (!pr)
		return -ENOMEM;

	if (!zalloc_cpumask_var(&pr->throttling.shared_cpu_map, GFP_KERNEL)) {
		result = -ENOMEM;
		goto err_free_pr;
	}

	pr->handle = device->handle;
	strcpy(acpi_device_name(device), ACPI_PROCESSOR_DEVICE_NAME);
	strcpy(acpi_device_class(device), ACPI_PROCESSOR_CLASS);
	device->driver_data = pr;

	result = acpi_processor_get_info(device);
	if (result) /* Processor is not physically present or unavailable */
		return 0;

	BUG_ON(pr->id >= nr_cpu_ids);

	/*
	 * Buggy BIOS check.
	 * ACPI id of processors can be reported wrongly by the BIOS.
	 * Don't trust it blindly
	 */
	if (per_cpu(processor_device_array, pr->id) != NULL &&
	    per_cpu(processor_device_array, pr->id) != device) {
		dev_warn(&device->dev,
			"BIOS reported wrong ACPI id %d for the processor\n",
			pr->id);
		/* Give up, but do not abort the namespace scan. */
		goto err;
	}
	/*
	 * processor_device_array is not cleared on errors to allow buggy BIOS
	 * checks.
	 */
	per_cpu(processor_device_array, pr->id) = device;
	per_cpu(processors, pr->id) = pr;

	dev = get_cpu_device(pr->id);
	if (!dev) {
		result = -ENODEV;
		goto err;
	}

	result = acpi_bind_one(dev, device);
	if (result)
		goto err;

	pr->dev = dev;

	/* Trigger the processor driver's .probe() if present. */
	if (device_attach(dev) >= 0)
		return 1;

	dev_err(dev, "Processor driver could not be attached\n");
	acpi_unbind_one(dev);

 err:
	free_cpumask_var(pr->throttling.shared_cpu_map);
	device->driver_data = NULL;
	per_cpu(processors, pr->id) = NULL;
 err_free_pr:
	kfree(pr);
	return result;
}

#ifdef CONFIG_ACPI_HOTPLUG_CPU
/* --------------------------------------------------------------------------
                                    Removal
   -------------------------------------------------------------------------- */

static void acpi_processor_remove(struct acpi_device *device)
{
	struct acpi_processor *pr;

	if (!device || !acpi_driver_data(device))
		return;

	pr = acpi_driver_data(device);
	if (pr->id >= nr_cpu_ids)
		goto out;

	/*
	 * The only reason why we ever get here is CPU hot-removal.  The CPU is
	 * already offline and the ACPI device removal locking prevents it from
	 * being put back online at this point.
	 *
	 * Unbind the driver from the processor device and detach it from the
	 * ACPI companion object.
	 */
	device_release_driver(pr->dev);
	acpi_unbind_one(pr->dev);

	/* Clean up. */
	per_cpu(processor_device_array, pr->id) = NULL;
	per_cpu(processors, pr->id) = NULL;

	cpu_maps_update_begin();
	cpu_hotplug_begin();

	/* Remove the CPU. */
	arch_unregister_cpu(pr->id);
	acpi_unmap_cpu(pr->id);

	cpu_hotplug_done();
	cpu_maps_update_done();

	try_offline_node(cpu_to_node(pr->id));

 out:
	free_cpumask_var(pr->throttling.shared_cpu_map);
	kfree(pr);
}
#endif /* CONFIG_ACPI_HOTPLUG_CPU */

#ifdef CONFIG_X86
static bool acpi_hwp_native_thermal_lvt_set;
static acpi_status __init acpi_hwp_native_thermal_lvt_osc(acpi_handle handle,
							  u32 lvl,
							  void *context,
							  void **rv)
{
	u8 sb_uuid_str[] = "4077A616-290C-47BE-9EBD-D87058713953";
	u32 capbuf[2];
	struct acpi_osc_context osc_context = {
		.uuid_str = sb_uuid_str,
		.rev = 1,
		.cap.length = 8,
		.cap.pointer = capbuf,
	};

	if (acpi_hwp_native_thermal_lvt_set)
		return AE_CTRL_TERMINATE;

	capbuf[0] = 0x0000;
	capbuf[1] = 0x1000; /* set bit 12 */

	if (ACPI_SUCCESS(acpi_run_osc(handle, &osc_context))) {
		if (osc_context.ret.pointer && osc_context.ret.length > 1) {
			u32 *capbuf_ret = osc_context.ret.pointer;

			if (capbuf_ret[1] & 0x1000) {
				acpi_handle_info(handle,
					"_OSC native thermal LVT Acked\n");
				acpi_hwp_native_thermal_lvt_set = true;
			}
		}
		kfree(osc_context.ret.pointer);
	}

	return AE_OK;
}

void __init acpi_early_processor_osc(void)
{
	if (boot_cpu_has(X86_FEATURE_HWP)) {
		acpi_walk_namespace(ACPI_TYPE_PROCESSOR, ACPI_ROOT_OBJECT,
				    ACPI_UINT32_MAX,
				    acpi_hwp_native_thermal_lvt_osc,
				    NULL, NULL, NULL);
		acpi_get_devices(ACPI_PROCESSOR_DEVICE_HID,
				 acpi_hwp_native_thermal_lvt_osc,
				 NULL, NULL);
	}
}
#endif

/*
 * The following ACPI IDs are known to be suitable for representing as
 * processor devices.
 */
static const struct acpi_device_id processor_device_ids[] = {

	{ ACPI_PROCESSOR_OBJECT_HID, },
	{ ACPI_PROCESSOR_DEVICE_HID, },

	{ }
};

static struct acpi_scan_handler processor_handler = {
	.ids = processor_device_ids,
	.attach = acpi_processor_add,
#ifdef CONFIG_ACPI_HOTPLUG_CPU
	.detach = acpi_processor_remove,
#endif
	.hotplug = {
		.enabled = true,
	},
};

static int acpi_processor_container_attach(struct acpi_device *dev,
					   const struct acpi_device_id *id)
{
	return 1;
}

static const struct acpi_device_id processor_container_ids[] = {
	{ ACPI_PROCESSOR_CONTAINER_HID, },
	{ }
};

static struct acpi_scan_handler processor_container_handler = {
	.ids = processor_container_ids,
	.attach = acpi_processor_container_attach,
};

/* The number of the unique processor IDs */
static int nr_unique_ids __initdata;

/* The number of the duplicate processor IDs */
static int nr_duplicate_ids;

/* Used to store the unique processor IDs */
static int unique_processor_ids[] __initdata = {
	[0 ... NR_CPUS - 1] = -1,
};

/* Used to store the duplicate processor IDs */
static int duplicate_processor_ids[] = {
	[0 ... NR_CPUS - 1] = -1,
};

static void __init processor_validated_ids_update(int proc_id)
{
	int i;

	if (nr_unique_ids == NR_CPUS||nr_duplicate_ids == NR_CPUS)
		return;

	/*
	 * Firstly, compare the proc_id with duplicate IDs, if the proc_id is
	 * already in the IDs, do nothing.
	 */
	for (i = 0; i < nr_duplicate_ids; i++) {
		if (duplicate_processor_ids[i] == proc_id)
			return;
	}

	/*
	 * Secondly, compare the proc_id with unique IDs, if the proc_id is in
	 * the IDs, put it in the duplicate IDs.
	 */
	for (i = 0; i < nr_unique_ids; i++) {
		if (unique_processor_ids[i] == proc_id) {
			duplicate_processor_ids[nr_duplicate_ids] = proc_id;
			nr_duplicate_ids++;
			return;
		}
	}

	/*
	 * Lastly, the proc_id is a unique ID, put it in the unique IDs.
	 */
	unique_processor_ids[nr_unique_ids] = proc_id;
	nr_unique_ids++;
}

static acpi_status __init acpi_processor_ids_walk(acpi_handle handle,
						  u32 lvl,
						  void *context,
						  void **rv)
{
	acpi_status status;
	acpi_object_type acpi_type;
	unsigned long long uid;
	union acpi_object object = { 0 };
	struct acpi_buffer buffer = { sizeof(union acpi_object), &object };

	status = acpi_get_type(handle, &acpi_type);
	if (ACPI_FAILURE(status))
		return false;

	switch (acpi_type) {
	case ACPI_TYPE_PROCESSOR:
		status = acpi_evaluate_object(handle, NULL, NULL, &buffer);
		if (ACPI_FAILURE(status))
			goto err;
		uid = object.processor.proc_id;
		break;

	case ACPI_TYPE_DEVICE:
		status = acpi_evaluate_integer(handle, "_UID", NULL, &uid);
		if (ACPI_FAILURE(status))
			goto err;
		break;
	default:
		goto err;
	}

	processor_validated_ids_update(uid);
	return true;

err:
	acpi_handle_info(handle, "Invalid processor object\n");
	return false;

}

void __init acpi_processor_check_duplicates(void)
{
	/* check the correctness for all processors in ACPI namespace */
	acpi_walk_namespace(ACPI_TYPE_PROCESSOR, ACPI_ROOT_OBJECT,
						ACPI_UINT32_MAX,
						acpi_processor_ids_walk,
						NULL, NULL, NULL);
	acpi_get_devices(ACPI_PROCESSOR_DEVICE_HID, acpi_processor_ids_walk,
						NULL, NULL);
}

bool acpi_duplicate_processor_id(int proc_id)
{
	int i;

	/*
	 * compare the proc_id with duplicate IDs, if the proc_id is already
	 * in the duplicate IDs, return true, otherwise, return false.
	 */
	for (i = 0; i < nr_duplicate_ids; i++) {
		if (duplicate_processor_ids[i] == proc_id)
			return true;
	}
	return false;
}

void __init acpi_processor_init(void)
{
	acpi_processor_check_duplicates();
	acpi_scan_add_handler_with_hotplug(&processor_handler, "processor");
	acpi_scan_add_handler(&processor_container_handler);
}
