/*
 * acpi_processor.c - ACPI Processor Driver ($Revision: 71 $)
 *
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *  Copyright (C) 2004       Dominik Brodowski <linux@brodo.de>
 *  Copyright (C) 2004  Anil S Keshavamurthy <anil.s.keshavamurthy@intel.com>
 *  			- Added processor hotplug support
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *  TBD:
 *	1. Make # power states dynamic.
 *	2. Support duty_cycle values that span bit 4.
 *	3. Optimize by having scheduler determine business instead of
 *	   having us try to calculate it here.
 *	4. Need C1 timing -- must modify kernel (IRQ handler) to get this.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/pm.h>
#include <linux/cpufreq.h>
#include <linux/cpu.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/dmi.h>
#include <linux/moduleparam.h>

#include <asm/io.h>
#include <asm/system.h>
#include <asm/cpu.h>
#include <asm/delay.h>
#include <asm/uaccess.h>
#include <asm/processor.h>
#include <asm/smp.h>
#include <asm/acpi.h>

#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>
#include <acpi/processor.h>

#define ACPI_PROCESSOR_COMPONENT	0x01000000
#define ACPI_PROCESSOR_CLASS		"processor"
#define ACPI_PROCESSOR_DRIVER_NAME	"ACPI Processor Driver"
#define ACPI_PROCESSOR_DEVICE_NAME	"Processor"
#define ACPI_PROCESSOR_FILE_INFO	"info"
#define ACPI_PROCESSOR_FILE_THROTTLING	"throttling"
#define ACPI_PROCESSOR_FILE_LIMIT	"limit"
#define ACPI_PROCESSOR_NOTIFY_PERFORMANCE 0x80
#define ACPI_PROCESSOR_NOTIFY_POWER	0x81

#define ACPI_PROCESSOR_LIMIT_USER	0
#define ACPI_PROCESSOR_LIMIT_THERMAL	1

#define ACPI_STA_PRESENT 0x00000001

#define _COMPONENT		ACPI_PROCESSOR_COMPONENT
ACPI_MODULE_NAME("acpi_processor")

    MODULE_AUTHOR("Paul Diefenbaugh");
MODULE_DESCRIPTION(ACPI_PROCESSOR_DRIVER_NAME);
MODULE_LICENSE("GPL");

static int acpi_processor_add(struct acpi_device *device);
static int acpi_processor_start(struct acpi_device *device);
static int acpi_processor_remove(struct acpi_device *device, int type);
static int acpi_processor_info_open_fs(struct inode *inode, struct file *file);
static void acpi_processor_notify(acpi_handle handle, u32 event, void *data);
static acpi_status acpi_processor_hotadd_init(acpi_handle handle, int *p_cpu);
static int acpi_processor_handle_eject(struct acpi_processor *pr);

static struct acpi_driver acpi_processor_driver = {
	.name = ACPI_PROCESSOR_DRIVER_NAME,
	.class = ACPI_PROCESSOR_CLASS,
	.ids = ACPI_PROCESSOR_HID,
	.ops = {
		.add = acpi_processor_add,
		.remove = acpi_processor_remove,
		.start = acpi_processor_start,
		},
};

#define INSTALL_NOTIFY_HANDLER		1
#define UNINSTALL_NOTIFY_HANDLER	2

static const struct file_operations acpi_processor_info_fops = {
	.open = acpi_processor_info_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

struct acpi_processor *processors[NR_CPUS];
struct acpi_processor_errata errata __read_mostly;

/* --------------------------------------------------------------------------
                                Errata Handling
   -------------------------------------------------------------------------- */

static int acpi_processor_errata_piix4(struct pci_dev *dev)
{
	u8 rev = 0;
	u8 value1 = 0;
	u8 value2 = 0;


	if (!dev)
		return -EINVAL;

	/*
	 * Note that 'dev' references the PIIX4 ACPI Controller.
	 */

	pci_read_config_byte(dev, PCI_REVISION_ID, &rev);

	switch (rev) {
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

	switch (rev) {

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

static int acpi_processor_errata(struct acpi_processor *pr)
{
	int result = 0;
	struct pci_dev *dev = NULL;


	if (!pr)
		return -EINVAL;

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
                              Common ACPI processor functions
   -------------------------------------------------------------------------- */

/*
 * _PDC is required for a BIOS-OS handshake for most of the newer
 * ACPI processor features.
 */
static int acpi_processor_set_pdc(struct acpi_processor *pr)
{
	struct acpi_object_list *pdc_in = pr->pdc;
	acpi_status status = AE_OK;


	if (!pdc_in)
		return status;

	status = acpi_evaluate_object(pr->handle, "_PDC", pdc_in, NULL);

	if (ACPI_FAILURE(status))
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
		    "Could not evaluate _PDC, using legacy perf. control...\n"));

	return status;
}

/* --------------------------------------------------------------------------
                              FS Interface (/proc)
   -------------------------------------------------------------------------- */

static struct proc_dir_entry *acpi_processor_dir = NULL;

static int acpi_processor_info_seq_show(struct seq_file *seq, void *offset)
{
	struct acpi_processor *pr = (struct acpi_processor *)seq->private;


	if (!pr)
		goto end;

	seq_printf(seq, "processor id:            %d\n"
		   "acpi id:                 %d\n"
		   "bus mastering control:   %s\n"
		   "power management:        %s\n"
		   "throttling control:      %s\n"
		   "limit interface:         %s\n",
		   pr->id,
		   pr->acpi_id,
		   pr->flags.bm_control ? "yes" : "no",
		   pr->flags.power ? "yes" : "no",
		   pr->flags.throttling ? "yes" : "no",
		   pr->flags.limit ? "yes" : "no");

      end:
	return 0;
}

static int acpi_processor_info_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_processor_info_seq_show,
			   PDE(inode)->data);
}

static int acpi_processor_add_fs(struct acpi_device *device)
{
	struct proc_dir_entry *entry = NULL;


	if (!acpi_device_dir(device)) {
		acpi_device_dir(device) = proc_mkdir(acpi_device_bid(device),
						     acpi_processor_dir);
		if (!acpi_device_dir(device))
			return -ENODEV;
	}
	acpi_device_dir(device)->owner = THIS_MODULE;

	/* 'info' [R] */
	entry = create_proc_entry(ACPI_PROCESSOR_FILE_INFO,
				  S_IRUGO, acpi_device_dir(device));
	if (!entry)
		return -EIO;
	else {
		entry->proc_fops = &acpi_processor_info_fops;
		entry->data = acpi_driver_data(device);
		entry->owner = THIS_MODULE;
	}

	/* 'throttling' [R/W] */
	entry = create_proc_entry(ACPI_PROCESSOR_FILE_THROTTLING,
				  S_IFREG | S_IRUGO | S_IWUSR,
				  acpi_device_dir(device));
	if (!entry)
		return -EIO;
	else {
		entry->proc_fops = &acpi_processor_throttling_fops;
		entry->data = acpi_driver_data(device);
		entry->owner = THIS_MODULE;
	}

	/* 'limit' [R/W] */
	entry = create_proc_entry(ACPI_PROCESSOR_FILE_LIMIT,
				  S_IFREG | S_IRUGO | S_IWUSR,
				  acpi_device_dir(device));
	if (!entry)
		return -EIO;
	else {
		entry->proc_fops = &acpi_processor_limit_fops;
		entry->data = acpi_driver_data(device);
		entry->owner = THIS_MODULE;
	}

	return 0;
}

static int acpi_processor_remove_fs(struct acpi_device *device)
{

	if (acpi_device_dir(device)) {
		remove_proc_entry(ACPI_PROCESSOR_FILE_INFO,
				  acpi_device_dir(device));
		remove_proc_entry(ACPI_PROCESSOR_FILE_THROTTLING,
				  acpi_device_dir(device));
		remove_proc_entry(ACPI_PROCESSOR_FILE_LIMIT,
				  acpi_device_dir(device));
		remove_proc_entry(acpi_device_bid(device), acpi_processor_dir);
		acpi_device_dir(device) = NULL;
	}

	return 0;
}

/* Use the acpiid in MADT to map cpus in case of SMP */
#ifndef CONFIG_SMP
#define convert_acpiid_to_cpu(acpi_id) (-1)
#else

#ifdef CONFIG_IA64
#define arch_acpiid_to_apicid 	ia64_acpiid_to_sapicid
#define arch_cpu_to_apicid 	ia64_cpu_to_sapicid
#define ARCH_BAD_APICID		(0xffff)
#else
#define arch_acpiid_to_apicid 	x86_acpiid_to_apicid
#define arch_cpu_to_apicid 	x86_cpu_to_apicid
#define ARCH_BAD_APICID		(0xff)
#endif

static int convert_acpiid_to_cpu(u8 acpi_id)
{
	u16 apic_id;
	int i;

	apic_id = arch_acpiid_to_apicid[acpi_id];
	if (apic_id == ARCH_BAD_APICID)
		return -1;

	for (i = 0; i < NR_CPUS; i++) {
		if (arch_cpu_to_apicid[i] == apic_id)
			return i;
	}
	return -1;
}
#endif

/* --------------------------------------------------------------------------
                                 Driver Interface
   -------------------------------------------------------------------------- */

static int acpi_processor_get_info(struct acpi_processor *pr)
{
	acpi_status status = 0;
	union acpi_object object = { 0 };
	struct acpi_buffer buffer = { sizeof(union acpi_object), &object };
	int cpu_index;
	static int cpu0_initialized;


	if (!pr)
		return -EINVAL;

	if (num_online_cpus() > 1)
		errata.smp = TRUE;

	acpi_processor_errata(pr);

	/*
	 * Check to see if we have bus mastering arbitration control.  This
	 * is required for proper C3 usage (to maintain cache coherency).
	 */
	if (acpi_fadt.V1_pm2_cnt_blk && acpi_fadt.pm2_cnt_len) {
		pr->flags.bm_control = 1;
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Bus mastering arbitration control present\n"));
	} else
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "No bus mastering arbitration control\n"));

	/*
	 * Evalute the processor object.  Note that it is common on SMP to
	 * have the first (boot) processor with a valid PBLK address while
	 * all others have a NULL address.
	 */
	status = acpi_evaluate_object(pr->handle, NULL, NULL, &buffer);
	if (ACPI_FAILURE(status)) {
		printk(KERN_ERR PREFIX "Evaluating processor object\n");
		return -ENODEV;
	}

	/*
	 * TBD: Synch processor ID (via LAPIC/LSAPIC structures) on SMP.
	 *      >>> 'acpi_get_processor_id(acpi_id, &id)' in arch/xxx/acpi.c
	 */
	pr->acpi_id = object.processor.proc_id;

	cpu_index = convert_acpiid_to_cpu(pr->acpi_id);

	/* Handle UP system running SMP kernel, with no LAPIC in MADT */
	if (!cpu0_initialized && (cpu_index == -1) &&
	    (num_online_cpus() == 1)) {
		cpu_index = 0;
	}

	cpu0_initialized = 1;

	pr->id = cpu_index;

	/*
	 *  Extra Processor objects may be enumerated on MP systems with
	 *  less than the max # of CPUs. They should be ignored _iff
	 *  they are physically not present.
	 */
	if (cpu_index == -1) {
		if (ACPI_FAILURE
		    (acpi_processor_hotadd_init(pr->handle, &pr->id))) {
			printk(KERN_ERR PREFIX
				    "Getting cpuindex for acpiid 0x%x\n",
				    pr->acpi_id);
			return -ENODEV;
		}
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Processor [%d:%d]\n", pr->id,
			  pr->acpi_id));

	if (!object.processor.pblk_address)
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "No PBLK (NULL address)\n"));
	else if (object.processor.pblk_length != 6)
		printk(KERN_ERR PREFIX "Invalid PBLK length [%d]\n",
			    object.processor.pblk_length);
	else {
		pr->throttling.address = object.processor.pblk_address;
		pr->throttling.duty_offset = acpi_fadt.duty_offset;
		pr->throttling.duty_width = acpi_fadt.duty_width;

		pr->pblk = object.processor.pblk_address;

		/*
		 * We don't care about error returns - we just try to mark
		 * these reserved so that nobody else is confused into thinking
		 * that this region might be unused..
		 *
		 * (In particular, allocating the IO range for Cardbus)
		 */
		request_region(pr->throttling.address, 6, "ACPI CPU throttle");
	}

#ifdef CONFIG_CPU_FREQ
	acpi_processor_ppc_has_changed(pr);
#endif
	acpi_processor_get_throttling_info(pr);
	acpi_processor_get_limit_info(pr);

	return 0;
}

static void *processor_device_array[NR_CPUS];

static int __cpuinit acpi_processor_start(struct acpi_device *device)
{
	int result = 0;
	acpi_status status = AE_OK;
	struct acpi_processor *pr;


	pr = acpi_driver_data(device);

	result = acpi_processor_get_info(pr);
	if (result) {
		/* Processor is physically not present */
		return 0;
	}

	BUG_ON((pr->id >= NR_CPUS) || (pr->id < 0));

	/*
	 * Buggy BIOS check
	 * ACPI id of processors can be reported wrongly by the BIOS.
	 * Don't trust it blindly
	 */
	if (processor_device_array[pr->id] != NULL &&
	    processor_device_array[pr->id] != (void *)device) {
		printk(KERN_WARNING "BIOS reported wrong ACPI id"
			"for the processor\n");
		return -ENODEV;
	}
	processor_device_array[pr->id] = (void *)device;

	processors[pr->id] = pr;

	result = acpi_processor_add_fs(device);
	if (result)
		goto end;

	status = acpi_install_notify_handler(pr->handle, ACPI_DEVICE_NOTIFY,
					     acpi_processor_notify, pr);

	/* _PDC call should be done before doing anything else (if reqd.). */
	arch_acpi_processor_init_pdc(pr);
	acpi_processor_set_pdc(pr);

	acpi_processor_power_init(pr, device);

	if (pr->flags.throttling) {
		printk(KERN_INFO PREFIX "%s [%s] (supports",
		       acpi_device_name(device), acpi_device_bid(device));
		printk(" %d throttling states", pr->throttling.state_count);
		printk(")\n");
	}

      end:

	return result;
}

static void acpi_processor_notify(acpi_handle handle, u32 event, void *data)
{
	struct acpi_processor *pr = (struct acpi_processor *)data;
	struct acpi_device *device = NULL;


	if (!pr)
		return;

	if (acpi_bus_get_device(pr->handle, &device))
		return;

	switch (event) {
	case ACPI_PROCESSOR_NOTIFY_PERFORMANCE:
		acpi_processor_ppc_has_changed(pr);
		acpi_bus_generate_event(device, event,
					pr->performance_platform_limit);
		break;
	case ACPI_PROCESSOR_NOTIFY_POWER:
		acpi_processor_cst_has_changed(pr);
		acpi_bus_generate_event(device, event, 0);
		break;
	default:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Unsupported event [0x%x]\n", event));
		break;
	}

	return;
}

static int acpi_processor_add(struct acpi_device *device)
{
	struct acpi_processor *pr = NULL;


	if (!device)
		return -EINVAL;

	pr = kmalloc(sizeof(struct acpi_processor), GFP_KERNEL);
	if (!pr)
		return -ENOMEM;
	memset(pr, 0, sizeof(struct acpi_processor));

	pr->handle = device->handle;
	strcpy(acpi_device_name(device), ACPI_PROCESSOR_DEVICE_NAME);
	strcpy(acpi_device_class(device), ACPI_PROCESSOR_CLASS);
	acpi_driver_data(device) = pr;

	return 0;
}

static int acpi_processor_remove(struct acpi_device *device, int type)
{
	acpi_status status = AE_OK;
	struct acpi_processor *pr = NULL;


	if (!device || !acpi_driver_data(device))
		return -EINVAL;

	pr = (struct acpi_processor *)acpi_driver_data(device);

	if (pr->id >= NR_CPUS) {
		kfree(pr);
		return 0;
	}

	if (type == ACPI_BUS_REMOVAL_EJECT) {
		if (acpi_processor_handle_eject(pr))
			return -EINVAL;
	}

	acpi_processor_power_exit(pr, device);

	status = acpi_remove_notify_handler(pr->handle, ACPI_DEVICE_NOTIFY,
					    acpi_processor_notify);

	acpi_processor_remove_fs(device);

	processors[pr->id] = NULL;

	kfree(pr);

	return 0;
}

#ifdef CONFIG_ACPI_HOTPLUG_CPU
/****************************************************************************
 * 	Acpi processor hotplug support 				       	    *
 ****************************************************************************/

static int is_processor_present(acpi_handle handle);

static int is_processor_present(acpi_handle handle)
{
	acpi_status status;
	unsigned long sta = 0;


	status = acpi_evaluate_integer(handle, "_STA", NULL, &sta);
	if (ACPI_FAILURE(status) || !(sta & ACPI_STA_PRESENT)) {
		ACPI_EXCEPTION((AE_INFO, status, "Processor Device is not present"));
		return 0;
	}
	return 1;
}

static
int acpi_processor_device_add(acpi_handle handle, struct acpi_device **device)
{
	acpi_handle phandle;
	struct acpi_device *pdev;
	struct acpi_processor *pr;


	if (acpi_get_parent(handle, &phandle)) {
		return -ENODEV;
	}

	if (acpi_bus_get_device(phandle, &pdev)) {
		return -ENODEV;
	}

	if (acpi_bus_add(device, pdev, handle, ACPI_BUS_TYPE_PROCESSOR)) {
		return -ENODEV;
	}

	acpi_bus_start(*device);

	pr = acpi_driver_data(*device);
	if (!pr)
		return -ENODEV;

	if ((pr->id >= 0) && (pr->id < NR_CPUS)) {
		kobject_uevent(&(*device)->kobj, KOBJ_ONLINE);
	}
	return 0;
}

static void
acpi_processor_hotplug_notify(acpi_handle handle, u32 event, void *data)
{
	struct acpi_processor *pr;
	struct acpi_device *device = NULL;
	int result;


	switch (event) {
	case ACPI_NOTIFY_BUS_CHECK:
	case ACPI_NOTIFY_DEVICE_CHECK:
		printk("Processor driver received %s event\n",
		       (event == ACPI_NOTIFY_BUS_CHECK) ?
		       "ACPI_NOTIFY_BUS_CHECK" : "ACPI_NOTIFY_DEVICE_CHECK");

		if (!is_processor_present(handle))
			break;

		if (acpi_bus_get_device(handle, &device)) {
			result = acpi_processor_device_add(handle, &device);
			if (result)
				printk(KERN_ERR PREFIX
					    "Unable to add the device\n");
			break;
		}

		pr = acpi_driver_data(device);
		if (!pr) {
			printk(KERN_ERR PREFIX "Driver data is NULL\n");
			break;
		}

		if (pr->id >= 0 && (pr->id < NR_CPUS)) {
			kobject_uevent(&device->kobj, KOBJ_OFFLINE);
			break;
		}

		result = acpi_processor_start(device);
		if ((!result) && ((pr->id >= 0) && (pr->id < NR_CPUS))) {
			kobject_uevent(&device->kobj, KOBJ_ONLINE);
		} else {
			printk(KERN_ERR PREFIX "Device [%s] failed to start\n",
				    acpi_device_bid(device));
		}
		break;
	case ACPI_NOTIFY_EJECT_REQUEST:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "received ACPI_NOTIFY_EJECT_REQUEST\n"));

		if (acpi_bus_get_device(handle, &device)) {
			printk(KERN_ERR PREFIX
				    "Device don't exist, dropping EJECT\n");
			break;
		}
		pr = acpi_driver_data(device);
		if (!pr) {
			printk(KERN_ERR PREFIX
				    "Driver data is NULL, dropping EJECT\n");
			return;
		}

		if ((pr->id < NR_CPUS) && (cpu_present(pr->id)))
			kobject_uevent(&device->kobj, KOBJ_OFFLINE);
		break;
	default:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Unsupported event [0x%x]\n", event));
		break;
	}

	return;
}

static acpi_status
processor_walk_namespace_cb(acpi_handle handle,
			    u32 lvl, void *context, void **rv)
{
	acpi_status status;
	int *action = context;
	acpi_object_type type = 0;

	status = acpi_get_type(handle, &type);
	if (ACPI_FAILURE(status))
		return (AE_OK);

	if (type != ACPI_TYPE_PROCESSOR)
		return (AE_OK);

	switch (*action) {
	case INSTALL_NOTIFY_HANDLER:
		acpi_install_notify_handler(handle,
					    ACPI_SYSTEM_NOTIFY,
					    acpi_processor_hotplug_notify,
					    NULL);
		break;
	case UNINSTALL_NOTIFY_HANDLER:
		acpi_remove_notify_handler(handle,
					   ACPI_SYSTEM_NOTIFY,
					   acpi_processor_hotplug_notify);
		break;
	default:
		break;
	}

	return (AE_OK);
}

static acpi_status acpi_processor_hotadd_init(acpi_handle handle, int *p_cpu)
{

	if (!is_processor_present(handle)) {
		return AE_ERROR;
	}

	if (acpi_map_lsapic(handle, p_cpu))
		return AE_ERROR;

	if (arch_register_cpu(*p_cpu)) {
		acpi_unmap_lsapic(*p_cpu);
		return AE_ERROR;
	}

	return AE_OK;
}

static int acpi_processor_handle_eject(struct acpi_processor *pr)
{
	if (cpu_online(pr->id)) {
		return (-EINVAL);
	}
	arch_unregister_cpu(pr->id);
	acpi_unmap_lsapic(pr->id);
	return (0);
}
#else
static acpi_status acpi_processor_hotadd_init(acpi_handle handle, int *p_cpu)
{
	return AE_ERROR;
}
static int acpi_processor_handle_eject(struct acpi_processor *pr)
{
	return (-EINVAL);
}
#endif

static
void acpi_processor_install_hotplug_notify(void)
{
#ifdef CONFIG_ACPI_HOTPLUG_CPU
	int action = INSTALL_NOTIFY_HANDLER;
	acpi_walk_namespace(ACPI_TYPE_PROCESSOR,
			    ACPI_ROOT_OBJECT,
			    ACPI_UINT32_MAX,
			    processor_walk_namespace_cb, &action, NULL);
#endif
}

static
void acpi_processor_uninstall_hotplug_notify(void)
{
#ifdef CONFIG_ACPI_HOTPLUG_CPU
	int action = UNINSTALL_NOTIFY_HANDLER;
	acpi_walk_namespace(ACPI_TYPE_PROCESSOR,
			    ACPI_ROOT_OBJECT,
			    ACPI_UINT32_MAX,
			    processor_walk_namespace_cb, &action, NULL);
#endif
}

/*
 * We keep the driver loaded even when ACPI is not running.
 * This is needed for the powernow-k8 driver, that works even without
 * ACPI, but needs symbols from this driver
 */

static int __init acpi_processor_init(void)
{
	int result = 0;


	memset(&processors, 0, sizeof(processors));
	memset(&errata, 0, sizeof(errata));

	acpi_processor_dir = proc_mkdir(ACPI_PROCESSOR_CLASS, acpi_root_dir);
	if (!acpi_processor_dir)
		return -ENOMEM;
	acpi_processor_dir->owner = THIS_MODULE;

	result = acpi_bus_register_driver(&acpi_processor_driver);
	if (result < 0) {
		remove_proc_entry(ACPI_PROCESSOR_CLASS, acpi_root_dir);
		return result;
	}

	acpi_processor_install_hotplug_notify();

	acpi_thermal_cpufreq_init();

	acpi_processor_ppc_init();

	return 0;
}

static void __exit acpi_processor_exit(void)
{

	acpi_processor_ppc_exit();

	acpi_thermal_cpufreq_exit();

	acpi_processor_uninstall_hotplug_notify();

	acpi_bus_unregister_driver(&acpi_processor_driver);

	remove_proc_entry(ACPI_PROCESSOR_CLASS, acpi_root_dir);

	return;
}

module_init(acpi_processor_init);
module_exit(acpi_processor_exit);

EXPORT_SYMBOL(acpi_processor_set_thermal_limit);

MODULE_ALIAS("processor");
