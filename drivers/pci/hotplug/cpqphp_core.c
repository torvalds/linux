/*
 * Compaq Hot Plug Controller Driver
 *
 * Copyright (C) 1995,2001 Compaq Computer Corporation
 * Copyright (C) 2001 Greg Kroah-Hartman <greg@kroah.com>
 * Copyright (C) 2001 IBM Corp.
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Send feedback to <greg@kroah.com>
 *
 * Jan 12, 2003 -	Added 66/100/133MHz PCI-X support,
 *			Torben Mathiasen <torben.mathiasen@hp.com>
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/pci.h>
#include <linux/pci_hotplug.h>
#include <linux/init.h>
#include <linux/interrupt.h>

#include <asm/uaccess.h>

#include "cpqphp.h"
#include "cpqphp_nvram.h"


/* Global variables */
int cpqhp_debug;
int cpqhp_legacy_mode;
struct controller *cpqhp_ctrl_list;	/* = NULL */
struct pci_func *cpqhp_slot_list[256];
struct irq_routing_table *cpqhp_routing_table;

/* local variables */
static void __iomem *smbios_table;
static void __iomem *smbios_start;
static void __iomem *cpqhp_rom_start;
static bool power_mode;
static bool debug;
static int initialized;

#define DRIVER_VERSION	"0.9.8"
#define DRIVER_AUTHOR	"Dan Zink <dan.zink@compaq.com>, Greg Kroah-Hartman <greg@kroah.com>"
#define DRIVER_DESC	"Compaq Hot Plug PCI Controller Driver"

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

module_param(power_mode, bool, 0644);
MODULE_PARM_DESC(power_mode, "Power mode enabled or not");

module_param(debug, bool, 0644);
MODULE_PARM_DESC(debug, "Debugging mode enabled or not");

#define CPQHPC_MODULE_MINOR 208

static inline int is_slot64bit(struct slot *slot)
{
	return (readb(slot->p_sm_slot + SMBIOS_SLOT_WIDTH) == 0x06) ? 1 : 0;
}

static inline int is_slot66mhz(struct slot *slot)
{
	return (readb(slot->p_sm_slot + SMBIOS_SLOT_TYPE) == 0x0E) ? 1 : 0;
}

/**
 * detect_SMBIOS_pointer - find the System Management BIOS Table in mem region.
 * @begin: begin pointer for region to be scanned.
 * @end: end pointer for region to be scanned.
 *
 * Returns pointer to the head of the SMBIOS tables (or %NULL).
 */
static void __iomem * detect_SMBIOS_pointer(void __iomem *begin, void __iomem *end)
{
	void __iomem *fp;
	void __iomem *endp;
	u8 temp1, temp2, temp3, temp4;
	int status = 0;

	endp = (end - sizeof(u32) + 1);

	for (fp = begin; fp <= endp; fp += 16) {
		temp1 = readb(fp);
		temp2 = readb(fp+1);
		temp3 = readb(fp+2);
		temp4 = readb(fp+3);
		if (temp1 == '_' &&
		    temp2 == 'S' &&
		    temp3 == 'M' &&
		    temp4 == '_') {
			status = 1;
			break;
		}
	}

	if (!status)
		fp = NULL;

	dbg("Discovered SMBIOS Entry point at %p\n", fp);

	return fp;
}

/**
 * init_SERR - Initializes the per slot SERR generation.
 * @ctrl: controller to use
 *
 * For unexpected switch opens
 */
static int init_SERR(struct controller * ctrl)
{
	u32 tempdword;
	u32 number_of_slots;
	u8 physical_slot;

	if (!ctrl)
		return 1;

	tempdword = ctrl->first_slot;

	number_of_slots = readb(ctrl->hpc_reg + SLOT_MASK) & 0x0F;
	/* Loop through slots */
	while (number_of_slots) {
		physical_slot = tempdword;
		writeb(0, ctrl->hpc_reg + SLOT_SERR);
		tempdword++;
		number_of_slots--;
	}

	return 0;
}

static int init_cpqhp_routing_table(void)
{
	int len;

	cpqhp_routing_table = pcibios_get_irq_routing_table();
	if (cpqhp_routing_table == NULL)
		return -ENOMEM;

	len = cpqhp_routing_table_length();
	if (len == 0) {
		kfree(cpqhp_routing_table);
		cpqhp_routing_table = NULL;
		return -1;
	}

	return 0;
}

/* nice debugging output */
static void pci_print_IRQ_route(void)
{
	int len;
	int loop;
	u8 tbus, tdevice, tslot;

	len = cpqhp_routing_table_length();

	dbg("bus dev func slot\n");
	for (loop = 0; loop < len; ++loop) {
		tbus = cpqhp_routing_table->slots[loop].bus;
		tdevice = cpqhp_routing_table->slots[loop].devfn;
		tslot = cpqhp_routing_table->slots[loop].slot;
		dbg("%d %d %d %d\n", tbus, tdevice >> 3, tdevice & 0x7, tslot);

	}
	return;
}


/**
 * get_subsequent_smbios_entry: get the next entry from bios table.
 * @smbios_start: where to start in the SMBIOS table
 * @smbios_table: location of the SMBIOS table
 * @curr: %NULL or pointer to previously returned structure
 *
 * Gets the first entry if previous == NULL;
 * otherwise, returns the next entry.
 * Uses global SMBIOS Table pointer.
 *
 * Returns a pointer to an SMBIOS structure or NULL if none found.
 */
static void __iomem *get_subsequent_smbios_entry(void __iomem *smbios_start,
						void __iomem *smbios_table,
						void __iomem *curr)
{
	u8 bail = 0;
	u8 previous_byte = 1;
	void __iomem *p_temp;
	void __iomem *p_max;

	if (!smbios_table || !curr)
		return NULL;

	/* set p_max to the end of the table */
	p_max = smbios_start + readw(smbios_table + ST_LENGTH);

	p_temp = curr;
	p_temp += readb(curr + SMBIOS_GENERIC_LENGTH);

	while ((p_temp < p_max) && !bail) {
		/* Look for the double NULL terminator
		 * The first condition is the previous byte
		 * and the second is the curr
		 */
		if (!previous_byte && !(readb(p_temp)))
			bail = 1;

		previous_byte = readb(p_temp);
		p_temp++;
	}

	if (p_temp < p_max)
		return p_temp;
	else
		return NULL;
}


/**
 * get_SMBIOS_entry - return the requested SMBIOS entry or %NULL
 * @smbios_start: where to start in the SMBIOS table
 * @smbios_table: location of the SMBIOS table
 * @type: SMBIOS structure type to be returned
 * @previous: %NULL or pointer to previously returned structure
 *
 * Gets the first entry of the specified type if previous == %NULL;
 * Otherwise, returns the next entry of the given type.
 * Uses global SMBIOS Table pointer.
 * Uses get_subsequent_smbios_entry.
 *
 * Returns a pointer to an SMBIOS structure or %NULL if none found.
 */
static void __iomem *get_SMBIOS_entry(void __iomem *smbios_start,
					void __iomem *smbios_table,
					u8 type,
					void __iomem *previous)
{
	if (!smbios_table)
		return NULL;

	if (!previous)
		previous = smbios_start;
	else
		previous = get_subsequent_smbios_entry(smbios_start,
					smbios_table, previous);

	while (previous)
		if (readb(previous + SMBIOS_GENERIC_TYPE) != type)
			previous = get_subsequent_smbios_entry(smbios_start,
						smbios_table, previous);
		else
			break;

	return previous;
}

static void release_slot(struct hotplug_slot *hotplug_slot)
{
	struct slot *slot = hotplug_slot->private;

	dbg("%s - physical_slot = %s\n", __func__, slot_name(slot));

	kfree(slot->hotplug_slot->info);
	kfree(slot->hotplug_slot);
	kfree(slot);
}

static int ctrl_slot_cleanup (struct controller * ctrl)
{
	struct slot *old_slot, *next_slot;

	old_slot = ctrl->slot;
	ctrl->slot = NULL;

	while (old_slot) {
		/* memory will be freed by the release_slot callback */
		next_slot = old_slot->next;
		pci_hp_deregister (old_slot->hotplug_slot);
		old_slot = next_slot;
	}

	cpqhp_remove_debugfs_files(ctrl);

	/* Free IRQ associated with hot plug device */
	free_irq(ctrl->interrupt, ctrl);
	/* Unmap the memory */
	iounmap(ctrl->hpc_reg);
	/* Finally reclaim PCI mem */
	release_mem_region(pci_resource_start(ctrl->pci_dev, 0),
			   pci_resource_len(ctrl->pci_dev, 0));

	return 0;
}


/**
 * get_slot_mapping - determine logical slot mapping for PCI device
 *
 * Won't work for more than one PCI-PCI bridge in a slot.
 *
 * @bus_num - bus number of PCI device
 * @dev_num - device number of PCI device
 * @slot - Pointer to u8 where slot number will	be returned
 *
 * Output:	SUCCESS or FAILURE
 */
static int
get_slot_mapping(struct pci_bus *bus, u8 bus_num, u8 dev_num, u8 *slot)
{
	u32 work;
	long len;
	long loop;

	u8 tbus, tdevice, tslot, bridgeSlot;

	dbg("%s: %p, %d, %d, %p\n", __func__, bus, bus_num, dev_num, slot);

	bridgeSlot = 0xFF;

	len = cpqhp_routing_table_length();
	for (loop = 0; loop < len; ++loop) {
		tbus = cpqhp_routing_table->slots[loop].bus;
		tdevice = cpqhp_routing_table->slots[loop].devfn >> 3;
		tslot = cpqhp_routing_table->slots[loop].slot;

		if ((tbus == bus_num) && (tdevice == dev_num)) {
			*slot = tslot;
			return 0;
		} else {
			/* Did not get a match on the target PCI device. Check
			 * if the current IRQ table entry is a PCI-to-PCI
			 * bridge device.  If so, and it's secondary bus
			 * matches the bus number for the target device, I need
			 * to save the bridge's slot number.  If I can not find
			 * an entry for the target device, I will have to
			 * assume it's on the other side of the bridge, and
			 * assign it the bridge's slot.
			 */
			bus->number = tbus;
			pci_bus_read_config_dword(bus, PCI_DEVFN(tdevice, 0),
						PCI_CLASS_REVISION, &work);

			if ((work >> 8) == PCI_TO_PCI_BRIDGE_CLASS) {
				pci_bus_read_config_dword(bus,
							PCI_DEVFN(tdevice, 0),
							PCI_PRIMARY_BUS, &work);
				// See if bridge's secondary bus matches target bus.
				if (((work >> 8) & 0x000000FF) == (long) bus_num)
					bridgeSlot = tslot;
			}
		}

	}

	/* If we got here, we didn't find an entry in the IRQ mapping table for
	 * the target PCI device.  If we did determine that the target device
	 * is on the other side of a PCI-to-PCI bridge, return the slot number
	 * for the bridge.
	 */
	if (bridgeSlot != 0xFF) {
		*slot = bridgeSlot;
		return 0;
	}
	/* Couldn't find an entry in the routing table for this PCI device */
	return -1;
}


/**
 * cpqhp_set_attention_status - Turns the Amber LED for a slot on or off
 * @ctrl: struct controller to use
 * @func: PCI device/function info
 * @status: LED control flag: 1 = LED on, 0 = LED off
 */
static int
cpqhp_set_attention_status(struct controller *ctrl, struct pci_func *func,
				u32 status)
{
	u8 hp_slot;

	if (func == NULL)
		return 1;

	hp_slot = func->device - ctrl->slot_device_offset;

	/* Wait for exclusive access to hardware */
	mutex_lock(&ctrl->crit_sect);

	if (status == 1)
		amber_LED_on (ctrl, hp_slot);
	else if (status == 0)
		amber_LED_off (ctrl, hp_slot);
	else {
		/* Done with exclusive hardware access */
		mutex_unlock(&ctrl->crit_sect);
		return 1;
	}

	set_SOGO(ctrl);

	/* Wait for SOBS to be unset */
	wait_for_ctrl_irq (ctrl);

	/* Done with exclusive hardware access */
	mutex_unlock(&ctrl->crit_sect);

	return 0;
}


/**
 * set_attention_status - Turns the Amber LED for a slot on or off
 * @hotplug_slot: slot to change LED on
 * @status: LED control flag
 */
static int set_attention_status (struct hotplug_slot *hotplug_slot, u8 status)
{
	struct pci_func *slot_func;
	struct slot *slot = hotplug_slot->private;
	struct controller *ctrl = slot->ctrl;
	u8 bus;
	u8 devfn;
	u8 device;
	u8 function;

	dbg("%s - physical_slot = %s\n", __func__, slot_name(slot));

	if (cpqhp_get_bus_dev(ctrl, &bus, &devfn, slot->number) == -1)
		return -ENODEV;

	device = devfn >> 3;
	function = devfn & 0x7;
	dbg("bus, dev, fn = %d, %d, %d\n", bus, device, function);

	slot_func = cpqhp_slot_find(bus, device, function);
	if (!slot_func)
		return -ENODEV;

	return cpqhp_set_attention_status(ctrl, slot_func, status);
}


static int process_SI(struct hotplug_slot *hotplug_slot)
{
	struct pci_func *slot_func;
	struct slot *slot = hotplug_slot->private;
	struct controller *ctrl = slot->ctrl;
	u8 bus;
	u8 devfn;
	u8 device;
	u8 function;

	dbg("%s - physical_slot = %s\n", __func__, slot_name(slot));

	if (cpqhp_get_bus_dev(ctrl, &bus, &devfn, slot->number) == -1)
		return -ENODEV;

	device = devfn >> 3;
	function = devfn & 0x7;
	dbg("bus, dev, fn = %d, %d, %d\n", bus, device, function);

	slot_func = cpqhp_slot_find(bus, device, function);
	if (!slot_func)
		return -ENODEV;

	slot_func->bus = bus;
	slot_func->device = device;
	slot_func->function = function;
	slot_func->configured = 0;
	dbg("board_added(%p, %p)\n", slot_func, ctrl);
	return cpqhp_process_SI(ctrl, slot_func);
}


static int process_SS(struct hotplug_slot *hotplug_slot)
{
	struct pci_func *slot_func;
	struct slot *slot = hotplug_slot->private;
	struct controller *ctrl = slot->ctrl;
	u8 bus;
	u8 devfn;
	u8 device;
	u8 function;

	dbg("%s - physical_slot = %s\n", __func__, slot_name(slot));

	if (cpqhp_get_bus_dev(ctrl, &bus, &devfn, slot->number) == -1)
		return -ENODEV;

	device = devfn >> 3;
	function = devfn & 0x7;
	dbg("bus, dev, fn = %d, %d, %d\n", bus, device, function);

	slot_func = cpqhp_slot_find(bus, device, function);
	if (!slot_func)
		return -ENODEV;

	dbg("In %s, slot_func = %p, ctrl = %p\n", __func__, slot_func, ctrl);
	return cpqhp_process_SS(ctrl, slot_func);
}


static int hardware_test(struct hotplug_slot *hotplug_slot, u32 value)
{
	struct slot *slot = hotplug_slot->private;
	struct controller *ctrl = slot->ctrl;

	dbg("%s - physical_slot = %s\n", __func__, slot_name(slot));

	return cpqhp_hardware_test(ctrl, value);
}


static int get_power_status(struct hotplug_slot *hotplug_slot, u8 *value)
{
	struct slot *slot = hotplug_slot->private;
	struct controller *ctrl = slot->ctrl;

	dbg("%s - physical_slot = %s\n", __func__, slot_name(slot));

	*value = get_slot_enabled(ctrl, slot);
	return 0;
}

static int get_attention_status(struct hotplug_slot *hotplug_slot, u8 *value)
{
	struct slot *slot = hotplug_slot->private;
	struct controller *ctrl = slot->ctrl;

	dbg("%s - physical_slot = %s\n", __func__, slot_name(slot));

	*value = cpq_get_attention_status(ctrl, slot);
	return 0;
}

static int get_latch_status(struct hotplug_slot *hotplug_slot, u8 *value)
{
	struct slot *slot = hotplug_slot->private;
	struct controller *ctrl = slot->ctrl;

	dbg("%s - physical_slot = %s\n", __func__, slot_name(slot));

	*value = cpq_get_latch_status(ctrl, slot);

	return 0;
}

static int get_adapter_status(struct hotplug_slot *hotplug_slot, u8 *value)
{
	struct slot *slot = hotplug_slot->private;
	struct controller *ctrl = slot->ctrl;

	dbg("%s - physical_slot = %s\n", __func__, slot_name(slot));

	*value = get_presence_status(ctrl, slot);

	return 0;
}

static struct hotplug_slot_ops cpqphp_hotplug_slot_ops = {
	.set_attention_status =	set_attention_status,
	.enable_slot =		process_SI,
	.disable_slot =		process_SS,
	.hardware_test =	hardware_test,
	.get_power_status =	get_power_status,
	.get_attention_status =	get_attention_status,
	.get_latch_status =	get_latch_status,
	.get_adapter_status =	get_adapter_status,
};

#define SLOT_NAME_SIZE 10

static int ctrl_slot_setup(struct controller *ctrl,
			void __iomem *smbios_start,
			void __iomem *smbios_table)
{
	struct slot *slot;
	struct hotplug_slot *hotplug_slot;
	struct hotplug_slot_info *hotplug_slot_info;
	struct pci_bus *bus = ctrl->pci_bus;
	u8 number_of_slots;
	u8 slot_device;
	u8 slot_number;
	u8 ctrl_slot;
	u32 tempdword;
	char name[SLOT_NAME_SIZE];
	void __iomem *slot_entry= NULL;
	int result;

	dbg("%s\n", __func__);

	tempdword = readl(ctrl->hpc_reg + INT_INPUT_CLEAR);

	number_of_slots = readb(ctrl->hpc_reg + SLOT_MASK) & 0x0F;
	slot_device = readb(ctrl->hpc_reg + SLOT_MASK) >> 4;
	slot_number = ctrl->first_slot;

	while (number_of_slots) {
		slot = kzalloc(sizeof(*slot), GFP_KERNEL);
		if (!slot) {
			result = -ENOMEM;
			goto error;
		}

		slot->hotplug_slot = kzalloc(sizeof(*(slot->hotplug_slot)),
						GFP_KERNEL);
		if (!slot->hotplug_slot) {
			result = -ENOMEM;
			goto error_slot;
		}
		hotplug_slot = slot->hotplug_slot;

		hotplug_slot->info = kzalloc(sizeof(*(hotplug_slot->info)),
							GFP_KERNEL);
		if (!hotplug_slot->info) {
			result = -ENOMEM;
			goto error_hpslot;
		}
		hotplug_slot_info = hotplug_slot->info;

		slot->ctrl = ctrl;
		slot->bus = ctrl->bus;
		slot->device = slot_device;
		slot->number = slot_number;
		dbg("slot->number = %u\n", slot->number);

		slot_entry = get_SMBIOS_entry(smbios_start, smbios_table, 9,
					slot_entry);

		while (slot_entry && (readw(slot_entry + SMBIOS_SLOT_NUMBER) !=
				slot->number)) {
			slot_entry = get_SMBIOS_entry(smbios_start,
						smbios_table, 9, slot_entry);
		}

		slot->p_sm_slot = slot_entry;

		init_timer(&slot->task_event);
		slot->task_event.expires = jiffies + 5 * HZ;
		slot->task_event.function = cpqhp_pushbutton_thread;

		/*FIXME: these capabilities aren't used but if they are
		 *	 they need to be correctly implemented
		 */
		slot->capabilities |= PCISLOT_REPLACE_SUPPORTED;
		slot->capabilities |= PCISLOT_INTERLOCK_SUPPORTED;

		if (is_slot64bit(slot))
			slot->capabilities |= PCISLOT_64_BIT_SUPPORTED;
		if (is_slot66mhz(slot))
			slot->capabilities |= PCISLOT_66_MHZ_SUPPORTED;
		if (bus->cur_bus_speed == PCI_SPEED_66MHz)
			slot->capabilities |= PCISLOT_66_MHZ_OPERATION;

		ctrl_slot =
			slot_device - (readb(ctrl->hpc_reg + SLOT_MASK) >> 4);

		/* Check presence */
		slot->capabilities |=
			((((~tempdword) >> 23) |
			 ((~tempdword) >> 15)) >> ctrl_slot) & 0x02;
		/* Check the switch state */
		slot->capabilities |=
			((~tempdword & 0xFF) >> ctrl_slot) & 0x01;
		/* Check the slot enable */
		slot->capabilities |=
			((read_slot_enable(ctrl) << 2) >> ctrl_slot) & 0x04;

		/* register this slot with the hotplug pci core */
		hotplug_slot->release = &release_slot;
		hotplug_slot->private = slot;
		snprintf(name, SLOT_NAME_SIZE, "%u", slot->number);
		hotplug_slot->ops = &cpqphp_hotplug_slot_ops;

		hotplug_slot_info->power_status = get_slot_enabled(ctrl, slot);
		hotplug_slot_info->attention_status =
			cpq_get_attention_status(ctrl, slot);
		hotplug_slot_info->latch_status =
			cpq_get_latch_status(ctrl, slot);
		hotplug_slot_info->adapter_status =
			get_presence_status(ctrl, slot);

		dbg("registering bus %d, dev %d, number %d, "
				"ctrl->slot_device_offset %d, slot %d\n",
				slot->bus, slot->device,
				slot->number, ctrl->slot_device_offset,
				slot_number);
		result = pci_hp_register(hotplug_slot,
					 ctrl->pci_dev->bus,
					 slot->device,
					 name);
		if (result) {
			err("pci_hp_register failed with error %d\n", result);
			goto error_info;
		}

		slot->next = ctrl->slot;
		ctrl->slot = slot;

		number_of_slots--;
		slot_device++;
		slot_number++;
	}

	return 0;
error_info:
	kfree(hotplug_slot_info);
error_hpslot:
	kfree(hotplug_slot);
error_slot:
	kfree(slot);
error:
	return result;
}

static int one_time_init(void)
{
	int loop;
	int retval = 0;

	if (initialized)
		return 0;

	power_mode = 0;

	retval = init_cpqhp_routing_table();
	if (retval)
		goto error;

	if (cpqhp_debug)
		pci_print_IRQ_route();

	dbg("Initialize + Start the notification mechanism \n");

	retval = cpqhp_event_start_thread();
	if (retval)
		goto error;

	dbg("Initialize slot lists\n");
	for (loop = 0; loop < 256; loop++)
		cpqhp_slot_list[loop] = NULL;

	/* FIXME: We also need to hook the NMI handler eventually.
	 * this also needs to be worked with Christoph
	 * register_NMI_handler();
	 */
	/* Map rom address */
	cpqhp_rom_start = ioremap(ROM_PHY_ADDR, ROM_PHY_LEN);
	if (!cpqhp_rom_start) {
		err ("Could not ioremap memory region for ROM\n");
		retval = -EIO;
		goto error;
	}

	/* Now, map the int15 entry point if we are on compaq specific
	 * hardware
	 */
	compaq_nvram_init(cpqhp_rom_start);

	/* Map smbios table entry point structure */
	smbios_table = detect_SMBIOS_pointer(cpqhp_rom_start,
					cpqhp_rom_start + ROM_PHY_LEN);
	if (!smbios_table) {
		err ("Could not find the SMBIOS pointer in memory\n");
		retval = -EIO;
		goto error_rom_start;
	}

	smbios_start = ioremap(readl(smbios_table + ST_ADDRESS),
					readw(smbios_table + ST_LENGTH));
	if (!smbios_start) {
		err ("Could not ioremap memory region taken from SMBIOS values\n");
		retval = -EIO;
		goto error_smbios_start;
	}

	initialized = 1;

	return retval;

error_smbios_start:
	iounmap(smbios_start);
error_rom_start:
	iounmap(cpqhp_rom_start);
error:
	return retval;
}

static int cpqhpc_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	u8 num_of_slots = 0;
	u8 hp_slot = 0;
	u8 device;
	u8 bus_cap;
	u16 temp_word;
	u16 vendor_id;
	u16 subsystem_vid;
	u16 subsystem_deviceid;
	u32 rc;
	struct controller *ctrl;
	struct pci_func *func;
	struct pci_bus *bus;
	int err;

	err = pci_enable_device(pdev);
	if (err) {
		printk(KERN_ERR MY_NAME ": cannot enable PCI device %s (%d)\n",
			pci_name(pdev), err);
		return err;
	}

	bus = pdev->subordinate;
	if (!bus) {
		dev_notice(&pdev->dev, "the device is not a bridge, "
				"skipping\n");
		rc = -ENODEV;
		goto err_disable_device;
	}

	/* Need to read VID early b/c it's used to differentiate CPQ and INTC
	 * discovery
	 */
	vendor_id = pdev->vendor;
	if ((vendor_id != PCI_VENDOR_ID_COMPAQ) &&
	    (vendor_id != PCI_VENDOR_ID_INTEL)) {
		err(msg_HPC_non_compaq_or_intel);
		rc = -ENODEV;
		goto err_disable_device;
	}
	dbg("Vendor ID: %x\n", vendor_id);

	dbg("revision: %d\n", pdev->revision);
	if ((vendor_id == PCI_VENDOR_ID_COMPAQ) && (!pdev->revision)) {
		err(msg_HPC_rev_error);
		rc = -ENODEV;
		goto err_disable_device;
	}

	/* Check for the proper subsystem IDs
	 * Intel uses a different SSID programming model than Compaq.
	 * For Intel, each SSID bit identifies a PHP capability.
	 * Also Intel HPCs may have RID=0.
	 */
	if ((pdev->revision <= 2) && (vendor_id != PCI_VENDOR_ID_INTEL)) {
		err(msg_HPC_not_supported);
		return -ENODEV;
	}

	/* TODO: This code can be made to support non-Compaq or Intel
	 * subsystem IDs
	 */
	subsystem_vid = pdev->subsystem_vendor;
	dbg("Subsystem Vendor ID: %x\n", subsystem_vid);
	if ((subsystem_vid != PCI_VENDOR_ID_COMPAQ) && (subsystem_vid != PCI_VENDOR_ID_INTEL)) {
		err(msg_HPC_non_compaq_or_intel);
		rc = -ENODEV;
		goto err_disable_device;
	}

	ctrl = kzalloc(sizeof(struct controller), GFP_KERNEL);
	if (!ctrl) {
		err("%s : out of memory\n", __func__);
		rc = -ENOMEM;
		goto err_disable_device;
	}

	subsystem_deviceid = pdev->subsystem_device;

	info("Hot Plug Subsystem Device ID: %x\n", subsystem_deviceid);

	/* Set Vendor ID, so it can be accessed later from other
	 * functions
	 */
	ctrl->vendor_id = vendor_id;

	switch (subsystem_vid) {
	case PCI_VENDOR_ID_COMPAQ:
		if (pdev->revision >= 0x13) { /* CIOBX */
			ctrl->push_flag = 1;
			ctrl->slot_switch_type = 1;
			ctrl->push_button = 1;
			ctrl->pci_config_space = 1;
			ctrl->defeature_PHP = 1;
			ctrl->pcix_support = 1;
			ctrl->pcix_speed_capability = 1;
			pci_read_config_byte(pdev, 0x41, &bus_cap);
			if (bus_cap & 0x80) {
				dbg("bus max supports 133MHz PCI-X\n");
				bus->max_bus_speed = PCI_SPEED_133MHz_PCIX;
				break;
			}
			if (bus_cap & 0x40) {
				dbg("bus max supports 100MHz PCI-X\n");
				bus->max_bus_speed = PCI_SPEED_100MHz_PCIX;
				break;
			}
			if (bus_cap & 0x20) {
				dbg("bus max supports 66MHz PCI-X\n");
				bus->max_bus_speed = PCI_SPEED_66MHz_PCIX;
				break;
			}
			if (bus_cap & 0x10) {
				dbg("bus max supports 66MHz PCI\n");
				bus->max_bus_speed = PCI_SPEED_66MHz;
				break;
			}

			break;
		}

		switch (subsystem_deviceid) {
		case PCI_SUB_HPC_ID:
			/* Original 6500/7000 implementation */
			ctrl->slot_switch_type = 1;
			bus->max_bus_speed = PCI_SPEED_33MHz;
			ctrl->push_button = 0;
			ctrl->pci_config_space = 1;
			ctrl->defeature_PHP = 1;
			ctrl->pcix_support = 0;
			ctrl->pcix_speed_capability = 0;
			break;
		case PCI_SUB_HPC_ID2:
			/* First Pushbutton implementation */
			ctrl->push_flag = 1;
			ctrl->slot_switch_type = 1;
			bus->max_bus_speed = PCI_SPEED_33MHz;
			ctrl->push_button = 1;
			ctrl->pci_config_space = 1;
			ctrl->defeature_PHP = 1;
			ctrl->pcix_support = 0;
			ctrl->pcix_speed_capability = 0;
			break;
		case PCI_SUB_HPC_ID_INTC:
			/* Third party (6500/7000) */
			ctrl->slot_switch_type = 1;
			bus->max_bus_speed = PCI_SPEED_33MHz;
			ctrl->push_button = 0;
			ctrl->pci_config_space = 1;
			ctrl->defeature_PHP = 1;
			ctrl->pcix_support = 0;
			ctrl->pcix_speed_capability = 0;
			break;
		case PCI_SUB_HPC_ID3:
			/* First 66 Mhz implementation */
			ctrl->push_flag = 1;
			ctrl->slot_switch_type = 1;
			bus->max_bus_speed = PCI_SPEED_66MHz;
			ctrl->push_button = 1;
			ctrl->pci_config_space = 1;
			ctrl->defeature_PHP = 1;
			ctrl->pcix_support = 0;
			ctrl->pcix_speed_capability = 0;
			break;
		case PCI_SUB_HPC_ID4:
			/* First PCI-X implementation, 100MHz */
			ctrl->push_flag = 1;
			ctrl->slot_switch_type = 1;
			bus->max_bus_speed = PCI_SPEED_100MHz_PCIX;
			ctrl->push_button = 1;
			ctrl->pci_config_space = 1;
			ctrl->defeature_PHP = 1;
			ctrl->pcix_support = 1;
			ctrl->pcix_speed_capability = 0;
			break;
		default:
			err(msg_HPC_not_supported);
			rc = -ENODEV;
			goto err_free_ctrl;
		}
		break;

	case PCI_VENDOR_ID_INTEL:
		/* Check for speed capability (0=33, 1=66) */
		if (subsystem_deviceid & 0x0001)
			bus->max_bus_speed = PCI_SPEED_66MHz;
		else
			bus->max_bus_speed = PCI_SPEED_33MHz;

		/* Check for push button */
		if (subsystem_deviceid & 0x0002)
			ctrl->push_button = 0;
		else
			ctrl->push_button = 1;

		/* Check for slot switch type (0=mechanical, 1=not mechanical) */
		if (subsystem_deviceid & 0x0004)
			ctrl->slot_switch_type = 0;
		else
			ctrl->slot_switch_type = 1;

		/* PHP Status (0=De-feature PHP, 1=Normal operation) */
		if (subsystem_deviceid & 0x0008)
			ctrl->defeature_PHP = 1;	/* PHP supported */
		else
			ctrl->defeature_PHP = 0;	/* PHP not supported */

		/* Alternate Base Address Register Interface
		 * (0=not supported, 1=supported)
		 */
		if (subsystem_deviceid & 0x0010)
			ctrl->alternate_base_address = 1;
		else
			ctrl->alternate_base_address = 0;

		/* PCI Config Space Index (0=not supported, 1=supported) */
		if (subsystem_deviceid & 0x0020)
			ctrl->pci_config_space = 1;
		else
			ctrl->pci_config_space = 0;

		/* PCI-X support */
		if (subsystem_deviceid & 0x0080) {
			ctrl->pcix_support = 1;
			if (subsystem_deviceid & 0x0040)
				/* 133MHz PCI-X if bit 7 is 1 */
				ctrl->pcix_speed_capability = 1;
			else
				/* 100MHz PCI-X if bit 7 is 1 and bit 0 is 0, */
				/* 66MHz PCI-X if bit 7 is 1 and bit 0 is 1 */
				ctrl->pcix_speed_capability = 0;
		} else {
			/* Conventional PCI */
			ctrl->pcix_support = 0;
			ctrl->pcix_speed_capability = 0;
		}
		break;

	default:
		err(msg_HPC_not_supported);
		rc = -ENODEV;
		goto err_free_ctrl;
	}

	/* Tell the user that we found one. */
	info("Initializing the PCI hot plug controller residing on PCI bus %d\n",
					pdev->bus->number);

	dbg("Hotplug controller capabilities:\n");
	dbg("    speed_capability       %d\n", bus->max_bus_speed);
	dbg("    slot_switch_type       %s\n", ctrl->slot_switch_type ?
					"switch present" : "no switch");
	dbg("    defeature_PHP          %s\n", ctrl->defeature_PHP ?
					"PHP supported" : "PHP not supported");
	dbg("    alternate_base_address %s\n", ctrl->alternate_base_address ?
					"supported" : "not supported");
	dbg("    pci_config_space       %s\n", ctrl->pci_config_space ?
					"supported" : "not supported");
	dbg("    pcix_speed_capability  %s\n", ctrl->pcix_speed_capability ?
					"supported" : "not supported");
	dbg("    pcix_support           %s\n", ctrl->pcix_support ?
					"supported" : "not supported");

	ctrl->pci_dev = pdev;
	pci_set_drvdata(pdev, ctrl);

	/* make our own copy of the pci bus structure,
	 * as we like tweaking it a lot */
	ctrl->pci_bus = kmemdup(pdev->bus, sizeof(*ctrl->pci_bus), GFP_KERNEL);
	if (!ctrl->pci_bus) {
		err("out of memory\n");
		rc = -ENOMEM;
		goto err_free_ctrl;
	}

	ctrl->bus = pdev->bus->number;
	ctrl->rev = pdev->revision;
	dbg("bus device function rev: %d %d %d %d\n", ctrl->bus,
		PCI_SLOT(pdev->devfn), PCI_FUNC(pdev->devfn), ctrl->rev);

	mutex_init(&ctrl->crit_sect);
	init_waitqueue_head(&ctrl->queue);

	/* initialize our threads if they haven't already been started up */
	rc = one_time_init();
	if (rc) {
		goto err_free_bus;
	}

	dbg("pdev = %p\n", pdev);
	dbg("pci resource start %llx\n", (unsigned long long)pci_resource_start(pdev, 0));
	dbg("pci resource len %llx\n", (unsigned long long)pci_resource_len(pdev, 0));

	if (!request_mem_region(pci_resource_start(pdev, 0),
				pci_resource_len(pdev, 0), MY_NAME)) {
		err("cannot reserve MMIO region\n");
		rc = -ENOMEM;
		goto err_free_bus;
	}

	ctrl->hpc_reg = ioremap(pci_resource_start(pdev, 0),
					pci_resource_len(pdev, 0));
	if (!ctrl->hpc_reg) {
		err("cannot remap MMIO region %llx @ %llx\n",
		    (unsigned long long)pci_resource_len(pdev, 0),
		    (unsigned long long)pci_resource_start(pdev, 0));
		rc = -ENODEV;
		goto err_free_mem_region;
	}

	/* Check for 66Mhz operation */
	bus->cur_bus_speed = get_controller_speed(ctrl);


	/********************************************************
	 *
	 *              Save configuration headers for this and
	 *              subordinate PCI buses
	 *
	 ********************************************************/

	/* find the physical slot number of the first hot plug slot */

	/* Get slot won't work for devices behind bridges, but
	 * in this case it will always be called for the "base"
	 * bus/dev/func of a slot.
	 * CS: this is leveraging the PCIIRQ routing code from the kernel
	 * (pci-pc.c: get_irq_routing_table) */
	rc = get_slot_mapping(ctrl->pci_bus, pdev->bus->number,
				(readb(ctrl->hpc_reg + SLOT_MASK) >> 4),
				&(ctrl->first_slot));
	dbg("get_slot_mapping: first_slot = %d, returned = %d\n",
				ctrl->first_slot, rc);
	if (rc) {
		err(msg_initialization_err, rc);
		goto err_iounmap;
	}

	/* Store PCI Config Space for all devices on this bus */
	rc = cpqhp_save_config(ctrl, ctrl->bus, readb(ctrl->hpc_reg + SLOT_MASK));
	if (rc) {
		err("%s: unable to save PCI configuration data, error %d\n",
				__func__, rc);
		goto err_iounmap;
	}

	/*
	 * Get IO, memory, and IRQ resources for new devices
	 */
	/* The next line is required for cpqhp_find_available_resources */
	ctrl->interrupt = pdev->irq;
	if (ctrl->interrupt < 0x10) {
		cpqhp_legacy_mode = 1;
		dbg("System seems to be configured for Full Table Mapped MPS mode\n");
	}

	ctrl->cfgspc_irq = 0;
	pci_read_config_byte(pdev, PCI_INTERRUPT_LINE, &ctrl->cfgspc_irq);

	rc = cpqhp_find_available_resources(ctrl, cpqhp_rom_start);
	ctrl->add_support = !rc;
	if (rc) {
		dbg("cpqhp_find_available_resources = 0x%x\n", rc);
		err("unable to locate PCI configuration resources for hot plug add.\n");
		goto err_iounmap;
	}

	/*
	 * Finish setting up the hot plug ctrl device
	 */
	ctrl->slot_device_offset = readb(ctrl->hpc_reg + SLOT_MASK) >> 4;
	dbg("NumSlots %d \n", ctrl->slot_device_offset);

	ctrl->next_event = 0;

	/* Setup the slot information structures */
	rc = ctrl_slot_setup(ctrl, smbios_start, smbios_table);
	if (rc) {
		err(msg_initialization_err, 6);
		err("%s: unable to save PCI configuration data, error %d\n",
			__func__, rc);
		goto err_iounmap;
	}

	/* Mask all general input interrupts */
	writel(0xFFFFFFFFL, ctrl->hpc_reg + INT_MASK);

	/* set up the interrupt */
	dbg("HPC interrupt = %d \n", ctrl->interrupt);
	if (request_irq(ctrl->interrupt, cpqhp_ctrl_intr,
			IRQF_SHARED, MY_NAME, ctrl)) {
		err("Can't get irq %d for the hotplug pci controller\n",
			ctrl->interrupt);
		rc = -ENODEV;
		goto err_iounmap;
	}

	/* Enable Shift Out interrupt and clear it, also enable SERR on power
	 * fault
	 */
	temp_word = readw(ctrl->hpc_reg + MISC);
	temp_word |= 0x4006;
	writew(temp_word, ctrl->hpc_reg + MISC);

	/* Changed 05/05/97 to clear all interrupts at start */
	writel(0xFFFFFFFFL, ctrl->hpc_reg + INT_INPUT_CLEAR);

	ctrl->ctrl_int_comp = readl(ctrl->hpc_reg + INT_INPUT_CLEAR);

	writel(0x0L, ctrl->hpc_reg + INT_MASK);

	if (!cpqhp_ctrl_list) {
		cpqhp_ctrl_list = ctrl;
		ctrl->next = NULL;
	} else {
		ctrl->next = cpqhp_ctrl_list;
		cpqhp_ctrl_list = ctrl;
	}

	/* turn off empty slots here unless command line option "ON" set
	 * Wait for exclusive access to hardware
	 */
	mutex_lock(&ctrl->crit_sect);

	num_of_slots = readb(ctrl->hpc_reg + SLOT_MASK) & 0x0F;

	/* find first device number for the ctrl */
	device = readb(ctrl->hpc_reg + SLOT_MASK) >> 4;

	while (num_of_slots) {
		dbg("num_of_slots: %d\n", num_of_slots);
		func = cpqhp_slot_find(ctrl->bus, device, 0);
		if (!func)
			break;

		hp_slot = func->device - ctrl->slot_device_offset;
		dbg("hp_slot: %d\n", hp_slot);

		/* We have to save the presence info for these slots */
		temp_word = ctrl->ctrl_int_comp >> 16;
		func->presence_save = (temp_word >> hp_slot) & 0x01;
		func->presence_save |= (temp_word >> (hp_slot + 7)) & 0x02;

		if (ctrl->ctrl_int_comp & (0x1L << hp_slot))
			func->switch_save = 0;
		else
			func->switch_save = 0x10;

		if (!power_mode)
			if (!func->is_a_board) {
				green_LED_off(ctrl, hp_slot);
				slot_disable(ctrl, hp_slot);
			}

		device++;
		num_of_slots--;
	}

	if (!power_mode) {
		set_SOGO(ctrl);
		/* Wait for SOBS to be unset */
		wait_for_ctrl_irq(ctrl);
	}

	rc = init_SERR(ctrl);
	if (rc) {
		err("init_SERR failed\n");
		mutex_unlock(&ctrl->crit_sect);
		goto err_free_irq;
	}

	/* Done with exclusive hardware access */
	mutex_unlock(&ctrl->crit_sect);

	cpqhp_create_debugfs_files(ctrl);

	return 0;

err_free_irq:
	free_irq(ctrl->interrupt, ctrl);
err_iounmap:
	iounmap(ctrl->hpc_reg);
err_free_mem_region:
	release_mem_region(pci_resource_start(pdev, 0), pci_resource_len(pdev, 0));
err_free_bus:
	kfree(ctrl->pci_bus);
err_free_ctrl:
	kfree(ctrl);
err_disable_device:
	pci_disable_device(pdev);
	return rc;
}

static void __exit unload_cpqphpd(void)
{
	struct pci_func *next;
	struct pci_func *TempSlot;
	int loop;
	u32 rc;
	struct controller *ctrl;
	struct controller *tctrl;
	struct pci_resource *res;
	struct pci_resource *tres;

	rc = compaq_nvram_store(cpqhp_rom_start);

	ctrl = cpqhp_ctrl_list;

	while (ctrl) {
		if (ctrl->hpc_reg) {
			u16 misc;
			rc = read_slot_enable (ctrl);

			writeb(0, ctrl->hpc_reg + SLOT_SERR);
			writel(0xFFFFFFC0L | ~rc, ctrl->hpc_reg + INT_MASK);

			misc = readw(ctrl->hpc_reg + MISC);
			misc &= 0xFFFD;
			writew(misc, ctrl->hpc_reg + MISC);
		}

		ctrl_slot_cleanup(ctrl);

		res = ctrl->io_head;
		while (res) {
			tres = res;
			res = res->next;
			kfree(tres);
		}

		res = ctrl->mem_head;
		while (res) {
			tres = res;
			res = res->next;
			kfree(tres);
		}

		res = ctrl->p_mem_head;
		while (res) {
			tres = res;
			res = res->next;
			kfree(tres);
		}

		res = ctrl->bus_head;
		while (res) {
			tres = res;
			res = res->next;
			kfree(tres);
		}

		kfree (ctrl->pci_bus);

		tctrl = ctrl;
		ctrl = ctrl->next;
		kfree(tctrl);
	}

	for (loop = 0; loop < 256; loop++) {
		next = cpqhp_slot_list[loop];
		while (next != NULL) {
			res = next->io_head;
			while (res) {
				tres = res;
				res = res->next;
				kfree(tres);
			}

			res = next->mem_head;
			while (res) {
				tres = res;
				res = res->next;
				kfree(tres);
			}

			res = next->p_mem_head;
			while (res) {
				tres = res;
				res = res->next;
				kfree(tres);
			}

			res = next->bus_head;
			while (res) {
				tres = res;
				res = res->next;
				kfree(tres);
			}

			TempSlot = next;
			next = next->next;
			kfree(TempSlot);
		}
	}

	/* Stop the notification mechanism */
	if (initialized)
		cpqhp_event_stop_thread();

	/* unmap the rom address */
	if (cpqhp_rom_start)
		iounmap(cpqhp_rom_start);
	if (smbios_start)
		iounmap(smbios_start);
}

static struct pci_device_id hpcd_pci_tbl[] = {
	{
	/* handle any PCI Hotplug controller */
	.class =        ((PCI_CLASS_SYSTEM_PCI_HOTPLUG << 8) | 0x00),
	.class_mask =   ~0,

	/* no matter who makes it */
	.vendor =       PCI_ANY_ID,
	.device =       PCI_ANY_ID,
	.subvendor =    PCI_ANY_ID,
	.subdevice =    PCI_ANY_ID,

	}, { /* end: all zeroes */ }
};

MODULE_DEVICE_TABLE(pci, hpcd_pci_tbl);

static struct pci_driver cpqhpc_driver = {
	.name =		"compaq_pci_hotplug",
	.id_table =	hpcd_pci_tbl,
	.probe =	cpqhpc_probe,
	/* remove:	cpqhpc_remove_one, */
};

static int __init cpqhpc_init(void)
{
	int result;

	cpqhp_debug = debug;

	info (DRIVER_DESC " version: " DRIVER_VERSION "\n");
	cpqhp_initialize_debugfs();
	result = pci_register_driver(&cpqhpc_driver);
	dbg("pci_register_driver = %d\n", result);
	return result;
}

static void __exit cpqhpc_cleanup(void)
{
	dbg("unload_cpqphpd()\n");
	unload_cpqphpd();

	dbg("pci_unregister_driver\n");
	pci_unregister_driver(&cpqhpc_driver);
	cpqhp_shutdown_debugfs();
}

module_init(cpqhpc_init);
module_exit(cpqhpc_cleanup);
