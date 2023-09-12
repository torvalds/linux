// SPDX-License-Identifier: GPL-2.0-only
/*
 * acpi_processor.c - ACPI processor enumeration support
 *
 * Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 * Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 * Copyright (C) 2004       Dominik Brodowski <linux@brodo.de>
 * Copyright (C) 2004  Anil S Keshavamurthy <anil.s.keshavamurthy@intel.com>
 * Copyright (C) 2013, Intel Corporation
 *                     Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 */
#define pr_fmt(fmt) "ACPI: " fmt

#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/dmi.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_device.h>

#include <acpi/processor.h>

#include <asm/cpu.h>

#include <xen/xen.h>

#include "internal.h"

DEFINE_PER_CPU(struct acpi_processor *, processors);
EXPORT_PER_CPU_SYMBOL(processors);

/* Errata Handling */
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
		dev_dbg(&dev->dev, "Found PIIX4 A-step\n");
		break;
	case 1:
		dev_dbg(&dev->dev, "Found PIIX4 B-step\n");
		break;
	case 2:
		dev_dbg(&dev->dev, "Found PIIX4E\n");
		break;
	case 3:
		dev_dbg(&dev->dev, "Found PIIX4M\n");
		break;
	default:
		dev_dbg(&dev->dev, "Found unknown PIIX4\n");
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
		fallthrough;

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
		dev_dbg(&dev->dev, "Bus master activity detection (BM-IDE) erratum enabled\n");
	if (errata.piix4.fdma)
		dev_dbg(&dev->dev, "Type-F DMA livelock erratum (C3 disabled)\n");

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

/* Create a platform device to represent a CPU frequency control mechanism. */
static void cpufreq_add_device(const char *name)
{
	struct platform_device *pdev;

	pdev = platform_device_register_simple(name, PLATFORM_DEVID_NONE, NULL, 0);
	if (IS_ERR(pdev))
		pr_info("%s device creation failed: %ld\n", name, PTR_ERR(pdev));
}

#ifdef CONFIG_X86
/* Check presence of Processor Clocking Control by searching for \_SB.PCCH. */
static void __init acpi_pcc_cpufreq_init(void)
{
	acpi_status status;
	acpi_handle handle;

	status = acpi_get_handle(NULL, "\\_SB", &handle);
	if (ACPI_FAILURE(status))
		return;

	if (acpi_has_method(handle, "PCCH"))
		cpufreq_add_device("pcc-cpufreq");
}
#else
static void __init acpi_pcc_cpufreq_init(void) {}
#endif /* CONFIG_X86 */

/* Initialization */
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
	cpus_write_lock();

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
	cpus_write_unlock();
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
		dev_dbg(&device->dev, "Bus mastering arbitration control present\n");
	} else
		dev_dbg(&device->dev, "No bus mastering arbitration control\n");

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
		if (pr->acpi_id == 0xff)
			dev_info_once(&device->dev,
				"Entry not well-defined, consider updating BIOS\n");
		else
			dev_err(&device->dev,
				"Failed to get unique processor _UID (0x%x)\n",
				pr->acpi_id);
		return -ENODEV;
	}

	pr->phys_id = acpi_get_phys_id(pr->handle, device_declaration,
					pr->acpi_id);
	if (invalid_phys_cpuid(pr->phys_id))
		dev_dbg(&device->dev, "Failed to get CPU physical ID.\n");

	pr->id = acpi_map_cpuid(pr->phys_id, pr->acpi_id);
	if (!cpu0_initialized) {
		cpu0_initialized = 1;
		/*
		 * Handle UP system running SMP kernel, with no CPU
		 * entry in MADT
		 */
		if (!acpi_has_cpu_in_madt() && invalid_logical_cpuid(pr->id) &&
		    (num_online_cpus() == 1))
			pr->id = 0;
		/*
		 * Check availability of Processor Performance Control by
		 * looking at the presence of the _PCT object under the first
		 * processor definition.
		 */
		if (acpi_has_method(pr->handle, "_PCT"))
			cpufreq_add_device("acpi-cpufreq");
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
	dev_dbg(&device->dev, "Processor [%d:%d]\n", pr->id, pr->acpi_id);

	if (!object.processor.pblk_address)
		dev_dbg(&device->dev, "No PBLK (NULL address)\n");
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
/* Removal */
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
	cpus_write_lock();

	/* Remove the CPU. */
	arch_unregister_cpu(pr->id);
	acpi_unmap_cpu(pr->id);

	cpus_write_unlock();
	cpu_maps_update_done();

	try_offline_node(cpu_to_node(pr->id));

 out:
	free_cpumask_var(pr->throttling.shared_cpu_map);
	kfree(pr);
}
#endif /* CONFIG_ACPI_HOTPLUG_CPU */

#ifdef CONFIG_ARCH_MIGHT_HAVE_ACPI_PDC
bool __init processor_physically_present(acpi_handle handle)
{
	int cpuid, type;
	u32 acpi_id;
	acpi_status status;
	acpi_object_type acpi_type;
	unsigned long long tmp;
	union acpi_object object = {};
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
		status = acpi_evaluate_integer(handle, METHOD_NAME__UID,
					       NULL, &tmp);
		if (ACPI_FAILURE(status))
			return false;
		acpi_id = tmp;
		break;
	default:
		return false;
	}

	if (xen_initial_domain())
		/*
		 * When running as a Xen dom0 the number of processors Linux
		 * sees can be different from the real number of processors on
		 * the system, and we still need to execute _PDC or _OSC for
		 * all of them.
		 */
		return xen_processor_present(acpi_id);

	type = (acpi_type == ACPI_TYPE_DEVICE) ? 1 : 0;
	cpuid = acpi_get_cpuid(handle, type, acpi_id);

	return !invalid_logical_cpuid(cpuid);
}

/* vendor specific UUID indicating an Intel platform */
static u8 sb_uuid_str[] = "4077A616-290C-47BE-9EBD-D87058713953";

static acpi_status __init acpi_processor_osc(acpi_handle handle, u32 lvl,
					     void *context, void **rv)
{
	u32 capbuf[2] = {};
	struct acpi_osc_context osc_context = {
		.uuid_str = sb_uuid_str,
		.rev = 1,
		.cap.length = 8,
		.cap.pointer = capbuf,
	};
	acpi_status status;

	if (!processor_physically_present(handle))
		return AE_OK;

	arch_acpi_set_proc_cap_bits(&capbuf[OSC_SUPPORT_DWORD]);

	status = acpi_run_osc(handle, &osc_context);
	if (ACPI_FAILURE(status))
		return status;

	kfree(osc_context.ret.pointer);

	return AE_OK;
}

static bool __init acpi_early_processor_osc(void)
{
	acpi_status status;

	acpi_proc_quirk_mwait_check();

	status = acpi_walk_namespace(ACPI_TYPE_PROCESSOR, ACPI_ROOT_OBJECT,
				     ACPI_UINT32_MAX, acpi_processor_osc, NULL,
				     NULL, NULL);
	if (ACPI_FAILURE(status))
		return false;

	status = acpi_get_devices(ACPI_PROCESSOR_DEVICE_HID, acpi_processor_osc,
				  NULL, NULL);
	if (ACPI_FAILURE(status))
		return false;

	return true;
}

void __init acpi_early_processor_control_setup(void)
{
	if (acpi_early_processor_osc()) {
		pr_info("_OSC evaluated successfully for all CPUs\n");
	} else {
		pr_info("_OSC evaluation for CPUs failed, trying _PDC\n");
		acpi_early_processor_set_pdc();
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
		return status;

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
	return AE_OK;

err:
	/* Exit on error, but don't abort the namespace walk */
	acpi_handle_info(handle, "Invalid processor object\n");
	return AE_OK;

}

static void __init acpi_processor_check_duplicates(void)
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
	acpi_pcc_cpufreq_init();
}

#ifdef CONFIG_ACPI_PROCESSOR_CSTATE
/**
 * acpi_processor_claim_cst_control - Request _CST control from the platform.
 */
bool acpi_processor_claim_cst_control(void)
{
	static bool cst_control_claimed;
	acpi_status status;

	if (!acpi_gbl_FADT.cst_control || cst_control_claimed)
		return true;

	status = acpi_os_write_port(acpi_gbl_FADT.smi_command,
				    acpi_gbl_FADT.cst_control, 8);
	if (ACPI_FAILURE(status)) {
		pr_warn("ACPI: Failed to claim processor _CST control\n");
		return false;
	}

	cst_control_claimed = true;
	return true;
}
EXPORT_SYMBOL_GPL(acpi_processor_claim_cst_control);

/**
 * acpi_processor_evaluate_cst - Evaluate the processor _CST control method.
 * @handle: ACPI handle of the processor object containing the _CST.
 * @cpu: The numeric ID of the target CPU.
 * @info: Object write the C-states information into.
 *
 * Extract the C-state information for the given CPU from the output of the _CST
 * control method under the corresponding ACPI processor object (or processor
 * device object) and populate @info with it.
 *
 * If any ACPI_ADR_SPACE_FIXED_HARDWARE C-states are found, invoke
 * acpi_processor_ffh_cstate_probe() to verify them and update the
 * cpu_cstate_entry data for @cpu.
 */
int acpi_processor_evaluate_cst(acpi_handle handle, u32 cpu,
				struct acpi_processor_power *info)
{
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *cst;
	acpi_status status;
	u64 count;
	int last_index = 0;
	int i, ret = 0;

	status = acpi_evaluate_object(handle, "_CST", NULL, &buffer);
	if (ACPI_FAILURE(status)) {
		acpi_handle_debug(handle, "No _CST\n");
		return -ENODEV;
	}

	cst = buffer.pointer;

	/* There must be at least 2 elements. */
	if (!cst || cst->type != ACPI_TYPE_PACKAGE || cst->package.count < 2) {
		acpi_handle_warn(handle, "Invalid _CST output\n");
		ret = -EFAULT;
		goto end;
	}

	count = cst->package.elements[0].integer.value;

	/* Validate the number of C-states. */
	if (count < 1 || count != cst->package.count - 1) {
		acpi_handle_warn(handle, "Inconsistent _CST data\n");
		ret = -EFAULT;
		goto end;
	}

	for (i = 1; i <= count; i++) {
		union acpi_object *element;
		union acpi_object *obj;
		struct acpi_power_register *reg;
		struct acpi_processor_cx cx;

		/*
		 * If there is not enough space for all C-states, skip the
		 * excess ones and log a warning.
		 */
		if (last_index >= ACPI_PROCESSOR_MAX_POWER - 1) {
			acpi_handle_warn(handle,
					 "No room for more idle states (limit: %d)\n",
					 ACPI_PROCESSOR_MAX_POWER - 1);
			break;
		}

		memset(&cx, 0, sizeof(cx));

		element = &cst->package.elements[i];
		if (element->type != ACPI_TYPE_PACKAGE) {
			acpi_handle_info(handle, "_CST C%d type(%x) is not package, skip...\n",
					 i, element->type);
			continue;
		}

		if (element->package.count != 4) {
			acpi_handle_info(handle, "_CST C%d package count(%d) is not 4, skip...\n",
					 i, element->package.count);
			continue;
		}

		obj = &element->package.elements[0];

		if (obj->type != ACPI_TYPE_BUFFER) {
			acpi_handle_info(handle, "_CST C%d package element[0] type(%x) is not buffer, skip...\n",
					 i, obj->type);
			continue;
		}

		reg = (struct acpi_power_register *)obj->buffer.pointer;

		obj = &element->package.elements[1];
		if (obj->type != ACPI_TYPE_INTEGER) {
			acpi_handle_info(handle, "_CST C[%d] package element[1] type(%x) is not integer, skip...\n",
					 i, obj->type);
			continue;
		}

		cx.type = obj->integer.value;
		/*
		 * There are known cases in which the _CST output does not
		 * contain C1, so if the type of the first state found is not
		 * C1, leave an empty slot for C1 to be filled in later.
		 */
		if (i == 1 && cx.type != ACPI_STATE_C1)
			last_index = 1;

		cx.address = reg->address;
		cx.index = last_index + 1;

		if (reg->space_id == ACPI_ADR_SPACE_FIXED_HARDWARE) {
			if (!acpi_processor_ffh_cstate_probe(cpu, &cx, reg)) {
				/*
				 * In the majority of cases _CST describes C1 as
				 * a FIXED_HARDWARE C-state, but if the command
				 * line forbids using MWAIT, use CSTATE_HALT for
				 * C1 regardless.
				 */
				if (cx.type == ACPI_STATE_C1 &&
				    boot_option_idle_override == IDLE_NOMWAIT) {
					cx.entry_method = ACPI_CSTATE_HALT;
					snprintf(cx.desc, ACPI_CX_DESC_LEN, "ACPI HLT");
				} else {
					cx.entry_method = ACPI_CSTATE_FFH;
				}
			} else if (cx.type == ACPI_STATE_C1) {
				/*
				 * In the special case of C1, FIXED_HARDWARE can
				 * be handled by executing the HLT instruction.
				 */
				cx.entry_method = ACPI_CSTATE_HALT;
				snprintf(cx.desc, ACPI_CX_DESC_LEN, "ACPI HLT");
			} else {
				acpi_handle_info(handle, "_CST C%d declares FIXED_HARDWARE C-state but not supported in hardware, skip...\n",
						 i);
				continue;
			}
		} else if (reg->space_id == ACPI_ADR_SPACE_SYSTEM_IO) {
			cx.entry_method = ACPI_CSTATE_SYSTEMIO;
			snprintf(cx.desc, ACPI_CX_DESC_LEN, "ACPI IOPORT 0x%x",
				 cx.address);
		} else {
			acpi_handle_info(handle, "_CST C%d space_id(%x) neither FIXED_HARDWARE nor SYSTEM_IO, skip...\n",
					 i, reg->space_id);
			continue;
		}

		if (cx.type == ACPI_STATE_C1)
			cx.valid = 1;

		obj = &element->package.elements[2];
		if (obj->type != ACPI_TYPE_INTEGER) {
			acpi_handle_info(handle, "_CST C%d package element[2] type(%x) not integer, skip...\n",
					 i, obj->type);
			continue;
		}

		cx.latency = obj->integer.value;

		obj = &element->package.elements[3];
		if (obj->type != ACPI_TYPE_INTEGER) {
			acpi_handle_info(handle, "_CST C%d package element[3] type(%x) not integer, skip...\n",
					 i, obj->type);
			continue;
		}

		memcpy(&info->states[++last_index], &cx, sizeof(cx));
	}

	acpi_handle_info(handle, "Found %d idle states\n", last_index);

	info->count = last_index;

end:
	kfree(buffer.pointer);

	return ret;
}
EXPORT_SYMBOL_GPL(acpi_processor_evaluate_cst);
#endif /* CONFIG_ACPI_PROCESSOR_CSTATE */
