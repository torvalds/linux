/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2005-2006 Silicon Graphics, Inc. All rights reserved.
 *
 * This work was based on the 2.4/2.6 kernel development by Dick Reigner.
 * Work to add BIOS PROM support was completed by Mike Habeck.
 */

#include <linux/acpi.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pci_hotplug.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/mutex.h>

#include <asm/sn/addrs.h>
#include <asm/sn/geo.h>
#include <asm/sn/l1.h>
#include <asm/sn/module.h>
#include <asm/sn/pcibr_provider.h>
#include <asm/sn/pcibus_provider_defs.h>
#include <asm/sn/pcidev.h>
#include <asm/sn/sn_feature_sets.h>
#include <asm/sn/sn_sal.h>
#include <asm/sn/types.h>
#include <asm/sn/acpi.h>

#include "../pci.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SGI (prarit@sgi.com, dickie@sgi.com, habeck@sgi.com)");
MODULE_DESCRIPTION("SGI Altix Hot Plug PCI Controller Driver");


/* SAL call error codes. Keep in sync with prom header io/include/pcibr.h */
#define PCI_SLOT_ALREADY_UP		2	/* slot already up */
#define PCI_SLOT_ALREADY_DOWN		3	/* slot already down */
#define PCI_L1_ERR			7	/* L1 console command error */
#define PCI_EMPTY_33MHZ			15	/* empty 33 MHz bus */


#define PCIIO_ASIC_TYPE_TIOCA		4
#define PCI_L1_QSIZE			128	/* our L1 message buffer size */
#define SN_MAX_HP_SLOTS			32	/* max hotplug slots */
#define SN_SLOT_NAME_SIZE		33	/* size of name string */

/* internal list head */
static struct list_head sn_hp_list;

/* hotplug_slot struct's private pointer */
struct slot {
	int device_num;
	struct pci_bus *pci_bus;
	/* this struct for glue internal only */
	struct hotplug_slot *hotplug_slot;
	struct list_head hp_list;
	char physical_path[SN_SLOT_NAME_SIZE];
};

struct pcibr_slot_enable_resp {
	int resp_sub_errno;
	char resp_l1_msg[PCI_L1_QSIZE + 1];
};

struct pcibr_slot_disable_resp {
	int resp_sub_errno;
	char resp_l1_msg[PCI_L1_QSIZE + 1];
};

enum sn_pci_req_e {
	PCI_REQ_SLOT_ELIGIBLE,
	PCI_REQ_SLOT_DISABLE
};

static int enable_slot(struct hotplug_slot *slot);
static int disable_slot(struct hotplug_slot *slot);
static inline int get_power_status(struct hotplug_slot *slot, u8 *value);

static struct hotplug_slot_ops sn_hotplug_slot_ops = {
	.enable_slot            = enable_slot,
	.disable_slot           = disable_slot,
	.get_power_status       = get_power_status,
};

static DEFINE_MUTEX(sn_hotplug_mutex);

static ssize_t path_show(struct pci_slot *pci_slot, char *buf)
{
	int retval = -ENOENT;
	struct slot *slot = pci_slot->hotplug->private;

	if (!slot)
		return retval;

	retval = sprintf (buf, "%s\n", slot->physical_path);
	return retval;
}

static struct pci_slot_attribute sn_slot_path_attr = __ATTR_RO(path);

static int sn_pci_slot_valid(struct pci_bus *pci_bus, int device)
{
	struct pcibus_info *pcibus_info;
	u16 busnum, segment, ioboard_type;

	pcibus_info = SN_PCIBUS_BUSSOFT_INFO(pci_bus);

	/* Check to see if this is a valid slot on 'pci_bus' */
	if (!(pcibus_info->pbi_valid_devices & (1 << device)))
		return -EPERM;

	ioboard_type = sn_ioboard_to_pci_bus(pci_bus);
	busnum = pcibus_info->pbi_buscommon.bs_persist_busnum;
	segment = pci_domain_nr(pci_bus) & 0xf;

	/* Do not allow hotplug operations on base I/O cards */
	if ((ioboard_type == L1_BRICKTYPE_IX ||
	     ioboard_type == L1_BRICKTYPE_IA) &&
	    (segment == 1 && busnum == 0 && device != 1))
		return -EPERM;

	return 1;
}

static int sn_pci_bus_valid(struct pci_bus *pci_bus)
{
	struct pcibus_info *pcibus_info;
	u32 asic_type;
	u16 ioboard_type;

	/* Don't register slots hanging off the TIOCA bus */
	pcibus_info = SN_PCIBUS_BUSSOFT_INFO(pci_bus);
	asic_type = pcibus_info->pbi_buscommon.bs_asic_type;
	if (asic_type == PCIIO_ASIC_TYPE_TIOCA)
		return -EPERM;

	/* Only register slots in I/O Bricks that support hotplug */
	ioboard_type = sn_ioboard_to_pci_bus(pci_bus);
	switch (ioboard_type) {
		case L1_BRICKTYPE_IX:
		case L1_BRICKTYPE_PX:
		case L1_BRICKTYPE_IA:
		case L1_BRICKTYPE_PA:
		case L1_BOARDTYPE_PCIX3SLOT:
			return 1;
			break;
		default:
			return -EPERM;
			break;
	}

	return -EIO;
}

static int sn_hp_slot_private_alloc(struct hotplug_slot *bss_hotplug_slot,
				    struct pci_bus *pci_bus, int device,
				    char *name)
{
	struct pcibus_info *pcibus_info;
	struct slot *slot;

	pcibus_info = SN_PCIBUS_BUSSOFT_INFO(pci_bus);

	slot = kzalloc(sizeof(*slot), GFP_KERNEL);
	if (!slot)
		return -ENOMEM;
	bss_hotplug_slot->private = slot;

	slot->device_num = device;
	slot->pci_bus = pci_bus;
	sprintf(name, "%04x:%02x:%02x",
		pci_domain_nr(pci_bus),
		((u16)pcibus_info->pbi_buscommon.bs_persist_busnum),
		device + 1);

	sn_generate_path(pci_bus, slot->physical_path);

	slot->hotplug_slot = bss_hotplug_slot;
	list_add(&slot->hp_list, &sn_hp_list);

	return 0;
}

static struct hotplug_slot *sn_hp_destroy(void)
{
	struct slot *slot;
	struct pci_slot *pci_slot;
	struct hotplug_slot *bss_hotplug_slot = NULL;

	list_for_each_entry(slot, &sn_hp_list, hp_list) {
		bss_hotplug_slot = slot->hotplug_slot;
		pci_slot = bss_hotplug_slot->pci_slot;
		list_del(&((struct slot *)bss_hotplug_slot->private)->
			 hp_list);
		sysfs_remove_file(&pci_slot->kobj,
				  &sn_slot_path_attr.attr);
		break;
	}
	return bss_hotplug_slot;
}

static void sn_bus_free_data(struct pci_dev *dev)
{
	struct pci_bus *subordinate_bus;
	struct pci_dev *child;

	/* Recursively clean up sn_irq_info structs */
	if (dev->subordinate) {
		subordinate_bus = dev->subordinate;
		list_for_each_entry(child, &subordinate_bus->devices, bus_list)
			sn_bus_free_data(child);
	}
	/*
	 * Some drivers may use dma accesses during the
	 * driver remove function. We release the sysdata
	 * areas after the driver remove functions have
	 * been called.
	 */
	sn_bus_store_sysdata(dev);
	sn_pci_unfixup_slot(dev);
}

static int sn_slot_enable(struct hotplug_slot *bss_hotplug_slot,
			  int device_num, char **ssdt)
{
	struct slot *slot = bss_hotplug_slot->private;
	struct pcibus_info *pcibus_info;
	struct pcibr_slot_enable_resp resp;
	int rc;

	pcibus_info = SN_PCIBUS_BUSSOFT_INFO(slot->pci_bus);

	/*
	 * Power-on and initialize the slot in the SN
	 * PCI infrastructure.
	 */
	rc = sal_pcibr_slot_enable(pcibus_info, device_num, &resp, ssdt);


	if (rc == PCI_SLOT_ALREADY_UP) {
		dev_dbg(&slot->pci_bus->self->dev, "is already active\n");
		return 1; /* return 1 to user */
	}

	if (rc == PCI_L1_ERR) {
		dev_dbg(&slot->pci_bus->self->dev, "L1 failure %d with message: %s",
			resp.resp_sub_errno, resp.resp_l1_msg);
		return -EPERM;
	}

	if (rc) {
		dev_dbg(&slot->pci_bus->self->dev, "insert failed with error %d sub-error %d\n",
			rc, resp.resp_sub_errno);
		return -EIO;
	}

	pcibus_info = SN_PCIBUS_BUSSOFT_INFO(slot->pci_bus);
	pcibus_info->pbi_enabled_devices |= (1 << device_num);

	return 0;
}

static int sn_slot_disable(struct hotplug_slot *bss_hotplug_slot,
			   int device_num, int action)
{
	struct slot *slot = bss_hotplug_slot->private;
	struct pcibus_info *pcibus_info;
	struct pcibr_slot_disable_resp resp;
	int rc;

	pcibus_info = SN_PCIBUS_BUSSOFT_INFO(slot->pci_bus);

	rc = sal_pcibr_slot_disable(pcibus_info, device_num, action, &resp);

	if ((action == PCI_REQ_SLOT_ELIGIBLE) &&
	    (rc == PCI_SLOT_ALREADY_DOWN)) {
		dev_dbg(&slot->pci_bus->self->dev, "Slot %s already inactive\n", slot->physical_path);
		return 1; /* return 1 to user */
	}

	if ((action == PCI_REQ_SLOT_ELIGIBLE) && (rc == PCI_EMPTY_33MHZ)) {
		dev_dbg(&slot->pci_bus->self->dev, "Cannot remove last 33MHz card\n");
		return -EPERM;
	}

	if ((action == PCI_REQ_SLOT_ELIGIBLE) && (rc == PCI_L1_ERR)) {
		dev_dbg(&slot->pci_bus->self->dev, "L1 failure %d with message \n%s\n",
			resp.resp_sub_errno, resp.resp_l1_msg);
		return -EPERM;
	}

	if ((action == PCI_REQ_SLOT_ELIGIBLE) && rc) {
		dev_dbg(&slot->pci_bus->self->dev, "remove failed with error %d sub-error %d\n",
			rc, resp.resp_sub_errno);
		return -EIO;
	}

	if ((action == PCI_REQ_SLOT_ELIGIBLE) && !rc)
		return 0;

	if ((action == PCI_REQ_SLOT_DISABLE) && !rc) {
		pcibus_info = SN_PCIBUS_BUSSOFT_INFO(slot->pci_bus);
		pcibus_info->pbi_enabled_devices &= ~(1 << device_num);
		dev_dbg(&slot->pci_bus->self->dev, "remove successful\n");
		return 0;
	}

	if ((action == PCI_REQ_SLOT_DISABLE) && rc) {
		dev_dbg(&slot->pci_bus->self->dev,"remove failed rc = %d\n", rc);
	}

	return rc;
}

/*
 * Power up and configure the slot via a SAL call to PROM.
 * Scan slot (and any children), do any platform specific fixup,
 * and find device driver.
 */
static int enable_slot(struct hotplug_slot *bss_hotplug_slot)
{
	struct slot *slot = bss_hotplug_slot->private;
	struct pci_bus *new_bus = NULL;
	struct pci_dev *dev;
	int num_funcs;
	int new_ppb = 0;
	int rc;
	char *ssdt = NULL;
	void pcibios_fixup_device_resources(struct pci_dev *);

	/* Serialize the Linux PCI infrastructure */
	mutex_lock(&sn_hotplug_mutex);

	/*
	 * Power-on and initialize the slot in the SN
	 * PCI infrastructure. Also, retrieve the ACPI SSDT
	 * table for the slot (if ACPI capable PROM).
	 */
	rc = sn_slot_enable(bss_hotplug_slot, slot->device_num, &ssdt);
	if (rc) {
		mutex_unlock(&sn_hotplug_mutex);
		return rc;
	}

	if (ssdt)
		ssdt = __va(ssdt);
	/* Add the new SSDT for the slot to the ACPI namespace */
	if (SN_ACPI_BASE_SUPPORT() && ssdt) {
		acpi_status ret;

		ret = acpi_load_table((struct acpi_table_header *)ssdt);
		if (ACPI_FAILURE(ret)) {
			printk(KERN_ERR "%s: acpi_load_table failed (0x%x)\n",
			       __func__, ret);
			/* try to continue on */
		}
	}

	num_funcs = pci_scan_slot(slot->pci_bus,
				  PCI_DEVFN(slot->device_num + 1, 0));
	if (!num_funcs) {
		dev_dbg(&slot->pci_bus->self->dev, "no device in slot\n");
		mutex_unlock(&sn_hotplug_mutex);
		return -ENODEV;
	}

	/*
	 * Map SN resources for all functions on the card
	 * to the Linux PCI interface and tell the drivers
	 * about them.
	 */
	list_for_each_entry(dev, &slot->pci_bus->devices, bus_list) {
		if (PCI_SLOT(dev->devfn) != slot->device_num + 1)
			continue;

		/* Need to do slot fixup on PPB before fixup of children
		 * (PPB's pcidev_info needs to be in pcidev_info list
		 * before child's SN_PCIDEV_INFO() call to setup
		 * pdi_host_pcidev_info).
		 */
		pcibios_fixup_device_resources(dev);
		if (SN_ACPI_BASE_SUPPORT())
			sn_acpi_slot_fixup(dev);
		else
			sn_io_slot_fixup(dev);
		if (dev->hdr_type == PCI_HEADER_TYPE_BRIDGE) {
			pci_hp_add_bridge(dev);
			if (dev->subordinate) {
				new_bus = dev->subordinate;
				new_ppb = 1;
			}
		}
	}

	/*
	 * Add the slot's devices to the ACPI infrastructure */
	if (SN_ACPI_BASE_SUPPORT() && ssdt) {
		unsigned long long adr;
		struct acpi_device *pdevice;
		acpi_handle phandle;
		acpi_handle chandle = NULL;
		acpi_handle rethandle;
		acpi_status ret;

		phandle = acpi_device_handle(PCI_CONTROLLER(slot->pci_bus)->companion);

		if (acpi_bus_get_device(phandle, &pdevice)) {
			dev_dbg(&slot->pci_bus->self->dev, "no parent device, assuming NULL\n");
			pdevice = NULL;
		}

		acpi_scan_lock_acquire();
		/*
		 * Walk the rootbus node's immediate children looking for
		 * the slot's device node(s). There can be more than
		 * one for multifunction devices.
		 */
		for (;;) {
			rethandle = NULL;
			ret = acpi_get_next_object(ACPI_TYPE_DEVICE,
						   phandle, chandle,
						   &rethandle);

			if (ret == AE_NOT_FOUND || rethandle == NULL)
				break;

			chandle = rethandle;

			ret = acpi_evaluate_integer(chandle, METHOD_NAME__ADR,
						    NULL, &adr);

			if (ACPI_SUCCESS(ret) &&
			    (adr>>16) == (slot->device_num + 1)) {

				ret = acpi_bus_scan(chandle);
				if (ACPI_FAILURE(ret)) {
					printk(KERN_ERR "%s: acpi_bus_scan failed (0x%x) for slot %d func %d\n",
					       __func__, ret, (int)(adr>>16),
					       (int)(adr&0xffff));
					/* try to continue on */
				}
			}
		}
		acpi_scan_lock_release();
	}

	pci_lock_rescan_remove();

	/* Call the driver for the new device */
	pci_bus_add_devices(slot->pci_bus);
	/* Call the drivers for the new devices subordinate to PPB */
	if (new_ppb)
		pci_bus_add_devices(new_bus);

	pci_unlock_rescan_remove();
	mutex_unlock(&sn_hotplug_mutex);

	if (rc == 0)
		dev_dbg(&slot->pci_bus->self->dev, "insert operation successful\n");
	else
		dev_dbg(&slot->pci_bus->self->dev, "insert operation failed rc = %d\n", rc);

	return rc;
}

static int disable_slot(struct hotplug_slot *bss_hotplug_slot)
{
	struct slot *slot = bss_hotplug_slot->private;
	struct pci_dev *dev, *temp;
	int rc;
	acpi_owner_id ssdt_id = 0;

	/* Acquire update access to the bus */
	mutex_lock(&sn_hotplug_mutex);

	/* is it okay to bring this slot down? */
	rc = sn_slot_disable(bss_hotplug_slot, slot->device_num,
			     PCI_REQ_SLOT_ELIGIBLE);
	if (rc)
		goto leaving;

	/* free the ACPI resources for the slot */
	if (SN_ACPI_BASE_SUPPORT() &&
            PCI_CONTROLLER(slot->pci_bus)->companion) {
		unsigned long long adr;
		struct acpi_device *device;
		acpi_handle phandle;
		acpi_handle chandle = NULL;
		acpi_handle rethandle;
		acpi_status ret;

		/* Get the rootbus node pointer */
		phandle = acpi_device_handle(PCI_CONTROLLER(slot->pci_bus)->companion);

		acpi_scan_lock_acquire();
		/*
		 * Walk the rootbus node's immediate children looking for
		 * the slot's device node(s). There can be more than
		 * one for multifunction devices.
		 */
		for (;;) {
			rethandle = NULL;
			ret = acpi_get_next_object(ACPI_TYPE_DEVICE,
						   phandle, chandle,
						   &rethandle);

			if (ret == AE_NOT_FOUND || rethandle == NULL)
				break;

			chandle = rethandle;

			ret = acpi_evaluate_integer(chandle,
						    METHOD_NAME__ADR,
						    NULL, &adr);
			if (ACPI_SUCCESS(ret) &&
			    (adr>>16) == (slot->device_num + 1)) {
				/* retain the owner id */
				acpi_get_id(chandle, &ssdt_id);

				ret = acpi_bus_get_device(chandle,
							  &device);
				if (ACPI_SUCCESS(ret))
					acpi_bus_trim(device);
			}
		}
		acpi_scan_lock_release();
	}

	pci_lock_rescan_remove();
	/* Free the SN resources assigned to the Linux device.*/
	list_for_each_entry_safe(dev, temp, &slot->pci_bus->devices, bus_list) {
		if (PCI_SLOT(dev->devfn) != slot->device_num + 1)
			continue;

		pci_dev_get(dev);
		sn_bus_free_data(dev);
		pci_stop_and_remove_bus_device(dev);
		pci_dev_put(dev);
	}
	pci_unlock_rescan_remove();

	/* Remove the SSDT for the slot from the ACPI namespace */
	if (SN_ACPI_BASE_SUPPORT() && ssdt_id) {
		acpi_status ret;
		ret = acpi_unload_table_id(ssdt_id);
		if (ACPI_FAILURE(ret)) {
			printk(KERN_ERR "%s: acpi_unload_table_id failed (0x%x) for id %d\n",
			       __func__, ret, ssdt_id);
			/* try to continue on */
		}
	}

	/* free the collected sysdata pointers */
	sn_bus_free_sysdata();

	/* Deactivate slot */
	rc = sn_slot_disable(bss_hotplug_slot, slot->device_num,
			     PCI_REQ_SLOT_DISABLE);
 leaving:
	/* Release the bus lock */
	mutex_unlock(&sn_hotplug_mutex);

	return rc;
}

static inline int get_power_status(struct hotplug_slot *bss_hotplug_slot,
				   u8 *value)
{
	struct slot *slot = bss_hotplug_slot->private;
	struct pcibus_info *pcibus_info;
	u32 power;

	pcibus_info = SN_PCIBUS_BUSSOFT_INFO(slot->pci_bus);
	mutex_lock(&sn_hotplug_mutex);
	power = pcibus_info->pbi_enabled_devices & (1 << slot->device_num);
	*value = power ? 1 : 0;
	mutex_unlock(&sn_hotplug_mutex);
	return 0;
}

static void sn_release_slot(struct hotplug_slot *bss_hotplug_slot)
{
	kfree(bss_hotplug_slot->info);
	kfree(bss_hotplug_slot->private);
	kfree(bss_hotplug_slot);
}

static int sn_hotplug_slot_register(struct pci_bus *pci_bus)
{
	int device;
	struct pci_slot *pci_slot;
	struct hotplug_slot *bss_hotplug_slot;
	char name[SN_SLOT_NAME_SIZE];
	int rc = 0;

	/*
	 * Currently only four devices are supported,
	 * in the future there maybe more -- up to 32.
	 */

	for (device = 0; device < SN_MAX_HP_SLOTS ; device++) {
		if (sn_pci_slot_valid(pci_bus, device) != 1)
			continue;

		bss_hotplug_slot = kzalloc(sizeof(*bss_hotplug_slot),
					   GFP_KERNEL);
		if (!bss_hotplug_slot) {
			rc = -ENOMEM;
			goto alloc_err;
		}

		bss_hotplug_slot->info =
			kzalloc(sizeof(struct hotplug_slot_info),
				GFP_KERNEL);
		if (!bss_hotplug_slot->info) {
			rc = -ENOMEM;
			goto alloc_err;
		}

		if (sn_hp_slot_private_alloc(bss_hotplug_slot,
					     pci_bus, device, name)) {
			rc = -ENOMEM;
			goto alloc_err;
		}
		bss_hotplug_slot->ops = &sn_hotplug_slot_ops;
		bss_hotplug_slot->release = &sn_release_slot;

		rc = pci_hp_register(bss_hotplug_slot, pci_bus, device, name);
		if (rc)
			goto register_err;

		pci_slot = bss_hotplug_slot->pci_slot;
		rc = sysfs_create_file(&pci_slot->kobj,
				       &sn_slot_path_attr.attr);
		if (rc)
			goto register_err;
	}
	dev_dbg(&pci_bus->self->dev, "Registered bus with hotplug\n");
	return rc;

register_err:
	dev_dbg(&pci_bus->self->dev, "bus failed to register with err = %d\n",
		rc);

alloc_err:
	if (rc == -ENOMEM)
		dev_dbg(&pci_bus->self->dev, "Memory allocation error\n");

	/* destroy THIS element */
	if (bss_hotplug_slot)
		sn_release_slot(bss_hotplug_slot);

	/* destroy anything else on the list */
	while ((bss_hotplug_slot = sn_hp_destroy()))
		pci_hp_deregister(bss_hotplug_slot);

	return rc;
}

static int __init sn_pci_hotplug_init(void)
{
	struct pci_bus *pci_bus = NULL;
	int rc;
	int registered = 0;

	if (!sn_prom_feature_available(PRF_HOTPLUG_SUPPORT)) {
		printk(KERN_ERR "%s: PROM version does not support hotplug.\n",
		       __func__);
		return -EPERM;
	}

	INIT_LIST_HEAD(&sn_hp_list);

	while ((pci_bus = pci_find_next_bus(pci_bus))) {
		if (!pci_bus->sysdata)
			continue;

		rc = sn_pci_bus_valid(pci_bus);
		if (rc != 1) {
			dev_dbg(&pci_bus->self->dev, "not a valid hotplug bus\n");
			continue;
		}
		dev_dbg(&pci_bus->self->dev, "valid hotplug bus\n");

		rc = sn_hotplug_slot_register(pci_bus);
		if (!rc) {
			registered = 1;
		} else {
			registered = 0;
			break;
		}
	}

	return registered == 1 ? 0 : -ENODEV;
}

static void __exit sn_pci_hotplug_exit(void)
{
	struct hotplug_slot *bss_hotplug_slot;

	while ((bss_hotplug_slot = sn_hp_destroy()))
		pci_hp_deregister(bss_hotplug_slot);

	if (!list_empty(&sn_hp_list))
		printk(KERN_ERR "%s: internal list is not empty\n", __FILE__);
}

module_init(sn_pci_hotplug_init);
module_exit(sn_pci_hotplug_exit);
