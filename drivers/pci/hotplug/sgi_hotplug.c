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

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
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

#include "../pci.h"
#include "pci_hotplug.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("SGI (prarit@sgi.com, dickie@sgi.com, habeck@sgi.com)");
MODULE_DESCRIPTION("SGI Altix Hot Plug PCI Controller Driver");

#define PCIIO_ASIC_TYPE_TIOCA		4
#define PCI_SLOT_ALREADY_UP		2	/* slot already up */
#define PCI_SLOT_ALREADY_DOWN		3	/* slot already down */
#define PCI_L1_ERR			7	/* L1 console command error */
#define PCI_EMPTY_33MHZ			15	/* empty 33 MHz bus */
#define PCI_L1_QSIZE			128	/* our L1 message buffer size */
#define SN_MAX_HP_SLOTS			32	/* max hotplug slots */
#define SGI_HOTPLUG_PROM_REV		0x0430	/* Min. required PROM version */
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
	.owner                  = THIS_MODULE,
	.enable_slot            = enable_slot,
	.disable_slot           = disable_slot,
	.get_power_status       = get_power_status,
};

static DEFINE_MUTEX(sn_hotplug_mutex);

static ssize_t path_show (struct hotplug_slot *bss_hotplug_slot,
	       		  char *buf)
{
	int retval = -ENOENT;
	struct slot *slot = bss_hotplug_slot->private;

	if (!slot)
		return retval;

	retval = sprintf (buf, "%s\n", slot->physical_path);
	return retval;
}

static struct hotplug_slot_attribute sn_slot_path_attr = __ATTR_RO(path);

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
				    struct pci_bus *pci_bus, int device)
{
	struct pcibus_info *pcibus_info;
	struct slot *slot;

	pcibus_info = SN_PCIBUS_BUSSOFT_INFO(pci_bus);

	slot = kzalloc(sizeof(*slot), GFP_KERNEL);
	if (!slot)
		return -ENOMEM;
	bss_hotplug_slot->private = slot;

	bss_hotplug_slot->name = kmalloc(SN_SLOT_NAME_SIZE, GFP_KERNEL);
	if (!bss_hotplug_slot->name) {
		kfree(bss_hotplug_slot->private);
		return -ENOMEM;
	}

	slot->device_num = device;
	slot->pci_bus = pci_bus;
	sprintf(bss_hotplug_slot->name, "%04x:%02x:%02x",
		pci_domain_nr(pci_bus),
		((u16)pcibus_info->pbi_buscommon.bs_persist_busnum),
		device + 1);

	sn_generate_path(pci_bus, slot->physical_path);

	slot->hotplug_slot = bss_hotplug_slot;
	list_add(&slot->hp_list, &sn_hp_list);

	return 0;
}

static struct hotplug_slot * sn_hp_destroy(void)
{
	struct slot *slot;
	struct hotplug_slot *bss_hotplug_slot = NULL;

	list_for_each_entry(slot, &sn_hp_list, hp_list) {
		bss_hotplug_slot = slot->hotplug_slot;
		list_del(&((struct slot *)bss_hotplug_slot->private)->
			 hp_list);
		sysfs_remove_file(&bss_hotplug_slot->kobj,
				  &sn_slot_path_attr.attr);
		break;
	}
	return bss_hotplug_slot;
}

static void sn_bus_alloc_data(struct pci_dev *dev)
{
	struct pci_bus *subordinate_bus;
	struct pci_dev *child;

	sn_pci_fixup_slot(dev);

	/* Recursively sets up the sn_irq_info structs */
	if (dev->subordinate) {
		subordinate_bus = dev->subordinate;
		list_for_each_entry(child, &subordinate_bus->devices, bus_list)
			sn_bus_alloc_data(child);
	}
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
			  int device_num)
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
	rc = sal_pcibr_slot_enable(pcibus_info, device_num, &resp);

	if (rc == PCI_SLOT_ALREADY_UP) {
		dev_dbg(slot->pci_bus->self, "is already active\n");
		return 1; /* return 1 to user */
	}

	if (rc == PCI_L1_ERR) {
		dev_dbg(slot->pci_bus->self,
			"L1 failure %d with message: %s",
			resp.resp_sub_errno, resp.resp_l1_msg);
		return -EPERM;
	}

	if (rc) {
		dev_dbg(slot->pci_bus->self,
			"insert failed with error %d sub-error %d\n",
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
		dev_dbg(slot->pci_bus->self, "Slot %s already inactive\n");
		return 1; /* return 1 to user */
	}

	if ((action == PCI_REQ_SLOT_ELIGIBLE) && (rc == PCI_EMPTY_33MHZ)) {
		dev_dbg(slot->pci_bus->self,
			"Cannot remove last 33MHz card\n");
		return -EPERM;
	}

	if ((action == PCI_REQ_SLOT_ELIGIBLE) && (rc == PCI_L1_ERR)) {
		dev_dbg(slot->pci_bus->self,
			"L1 failure %d with message \n%s\n",
			resp.resp_sub_errno, resp.resp_l1_msg);
		return -EPERM;
	}

	if ((action == PCI_REQ_SLOT_ELIGIBLE) && rc) {
		dev_dbg(slot->pci_bus->self,
			"remove failed with error %d sub-error %d\n",
			rc, resp.resp_sub_errno);
		return -EIO;
	}

	if ((action == PCI_REQ_SLOT_ELIGIBLE) && !rc)
		return 0;

	if ((action == PCI_REQ_SLOT_DISABLE) && !rc) {
		pcibus_info = SN_PCIBUS_BUSSOFT_INFO(slot->pci_bus);
		pcibus_info->pbi_enabled_devices &= ~(1 << device_num);
		dev_dbg(slot->pci_bus->self, "remove successful\n");
		return 0;
	}

	if ((action == PCI_REQ_SLOT_DISABLE) && rc) {
		dev_dbg(slot->pci_bus->self,"remove failed rc = %d\n", rc);
	}

	return rc;
}

static int enable_slot(struct hotplug_slot *bss_hotplug_slot)
{
	struct slot *slot = bss_hotplug_slot->private;
	struct pci_bus *new_bus = NULL;
	struct pci_dev *dev;
	int func, num_funcs;
	int new_ppb = 0;
	int rc;

	/* Serialize the Linux PCI infrastructure */
	mutex_lock(&sn_hotplug_mutex);

	/*
	 * Power-on and initialize the slot in the SN
	 * PCI infrastructure.
	 */
	rc = sn_slot_enable(bss_hotplug_slot, slot->device_num);
	if (rc) {
		mutex_unlock(&sn_hotplug_mutex);
		return rc;
	}

	num_funcs = pci_scan_slot(slot->pci_bus,
				  PCI_DEVFN(slot->device_num + 1, 0));
	if (!num_funcs) {
		dev_dbg(slot->pci_bus->self, "no device in slot\n");
		mutex_unlock(&sn_hotplug_mutex);
		return -ENODEV;
	}

	sn_pci_controller_fixup(pci_domain_nr(slot->pci_bus),
				slot->pci_bus->number,
				slot->pci_bus);
	/*
	 * Map SN resources for all functions on the card
	 * to the Linux PCI interface and tell the drivers
	 * about them.
	 */
	for (func = 0; func < num_funcs;  func++) {
		dev = pci_get_slot(slot->pci_bus,
				   PCI_DEVFN(slot->device_num + 1,
					     PCI_FUNC(func)));
		if (dev) {
			if (dev->hdr_type == PCI_HEADER_TYPE_BRIDGE) {
				unsigned char sec_bus;
				pci_read_config_byte(dev, PCI_SECONDARY_BUS,
						     &sec_bus);
				new_bus = pci_add_new_bus(dev->bus, dev,
							  sec_bus);
				pci_scan_child_bus(new_bus);
				sn_pci_controller_fixup(pci_domain_nr(new_bus),
							new_bus->number,
							new_bus);
				new_ppb = 1;
			}
			sn_bus_alloc_data(dev);
			pci_dev_put(dev);
		}
	}

	/* Call the driver for the new device */
	pci_bus_add_devices(slot->pci_bus);
	/* Call the drivers for the new devices subordinate to PPB */
	if (new_ppb)
		pci_bus_add_devices(new_bus);

	mutex_unlock(&sn_hotplug_mutex);

	if (rc == 0)
		dev_dbg(slot->pci_bus->self,
			"insert operation successful\n");
	else
		dev_dbg(slot->pci_bus->self,
			"insert operation failed rc = %d\n", rc);

	return rc;
}

static int disable_slot(struct hotplug_slot *bss_hotplug_slot)
{
	struct slot *slot = bss_hotplug_slot->private;
	struct pci_dev *dev;
	int func;
	int rc;

	/* Acquire update access to the bus */
	mutex_lock(&sn_hotplug_mutex);

	/* is it okay to bring this slot down? */
	rc = sn_slot_disable(bss_hotplug_slot, slot->device_num,
			     PCI_REQ_SLOT_ELIGIBLE);
	if (rc)
		goto leaving;

	/* Free the SN resources assigned to the Linux device.*/
	for (func = 0; func < 8;  func++) {
		dev = pci_get_slot(slot->pci_bus,
				   PCI_DEVFN(slot->device_num + 1,
				   	     PCI_FUNC(func)));
		if (dev) {
			sn_bus_free_data(dev);
			pci_remove_bus_device(dev);
			pci_dev_put(dev);
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

	pcibus_info = SN_PCIBUS_BUSSOFT_INFO(slot->pci_bus);
	mutex_lock(&sn_hotplug_mutex);
	*value = pcibus_info->pbi_enabled_devices & (1 << slot->device_num);
	mutex_unlock(&sn_hotplug_mutex);
	return 0;
}

static void sn_release_slot(struct hotplug_slot *bss_hotplug_slot)
{
	kfree(bss_hotplug_slot->info);
	kfree(bss_hotplug_slot->name);
	kfree(bss_hotplug_slot->private);
	kfree(bss_hotplug_slot);
}

static int sn_hotplug_slot_register(struct pci_bus *pci_bus)
{
	int device;
	struct hotplug_slot *bss_hotplug_slot;
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
					     pci_bus, device)) {
			rc = -ENOMEM;
			goto alloc_err;
		}

		bss_hotplug_slot->ops = &sn_hotplug_slot_ops;
		bss_hotplug_slot->release = &sn_release_slot;

		rc = pci_hp_register(bss_hotplug_slot);
		if (rc)
			goto register_err;

		rc = sysfs_create_file(&bss_hotplug_slot->kobj,
				       &sn_slot_path_attr.attr);
		if (rc)
			goto register_err;
	}
	dev_dbg(pci_bus->self, "Registered bus with hotplug\n");
	return rc;

register_err:
	dev_dbg(pci_bus->self, "bus failed to register with err = %d\n",
	        rc);

alloc_err:
	if (rc == -ENOMEM)
		dev_dbg(pci_bus->self, "Memory allocation error\n");

	/* destroy THIS element */
	if (bss_hotplug_slot)
		sn_release_slot(bss_hotplug_slot);

	/* destroy anything else on the list */
	while ((bss_hotplug_slot = sn_hp_destroy()))
		pci_hp_deregister(bss_hotplug_slot);

	return rc;
}

static int sn_pci_hotplug_init(void)
{
	struct pci_bus *pci_bus = NULL;
	int rc;
	int registered = 0;

	if (!sn_prom_feature_available(PRF_HOTPLUG_SUPPORT)) {
		printk(KERN_ERR "%s: PROM version does not support hotplug.\n",
		       __FUNCTION__);
		return -EPERM;
	}

	INIT_LIST_HEAD(&sn_hp_list);

	while ((pci_bus = pci_find_next_bus(pci_bus))) {
		if (!pci_bus->sysdata)
			continue;

		rc = sn_pci_bus_valid(pci_bus);
		if (rc != 1) {
			dev_dbg(pci_bus->self, "not a valid hotplug bus\n");
			continue;
		}
		dev_dbg(pci_bus->self, "valid hotplug bus\n");

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

static void sn_pci_hotplug_exit(void)
{
	struct hotplug_slot *bss_hotplug_slot;

	while ((bss_hotplug_slot = sn_hp_destroy()))
		pci_hp_deregister(bss_hotplug_slot);

	if (!list_empty(&sn_hp_list))
		printk(KERN_ERR "%s: internal list is not empty\n", __FILE__);
}

module_init(sn_pci_hotplug_init);
module_exit(sn_pci_hotplug_exit);
