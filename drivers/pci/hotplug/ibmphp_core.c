// SPDX-License-Identifier: GPL-2.0+
/*
 * IBM Hot Plug Controller Driver
 *
 * Written By: Chuck Cole, Jyoti Shah, Tong Yu, Irene Zubarev, IBM Corporation
 *
 * Copyright (C) 2001,2003 Greg Kroah-Hartman (greg@kroah.com)
 * Copyright (C) 2001-2003 IBM Corp.
 *
 * All rights reserved.
 *
 * Send feedback to <gregkh@us.ibm.com>
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include "../pci.h"
#include <asm/pci_x86.h>		/* for struct irq_routing_table */
#include <asm/io_apic.h>
#include "ibmphp.h"

#define attn_on(sl)  ibmphp_hpc_writeslot(sl, HPC_SLOT_ATTNON)
#define attn_off(sl) ibmphp_hpc_writeslot(sl, HPC_SLOT_ATTNOFF)
#define attn_LED_blink(sl) ibmphp_hpc_writeslot(sl, HPC_SLOT_BLINKLED)
#define get_ctrl_revision(sl, rev) ibmphp_hpc_readslot(sl, READ_REVLEVEL, rev)
#define get_hpc_options(sl, opt) ibmphp_hpc_readslot(sl, READ_HPCOPTIONS, opt)

#define DRIVER_VERSION	"0.6"
#define DRIVER_DESC	"IBM Hot Plug PCI Controller Driver"

int ibmphp_debug;

static bool debug;
module_param(debug, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "Debugging mode enabled or not");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION(DRIVER_DESC);

struct pci_bus *ibmphp_pci_bus;
static int max_slots;

static int irqs[16];    /* PIC mode IRQs we're using so far (in case MPS
			 * tables don't provide default info for empty slots */

static int init_flag;

/*
static int get_max_adapter_speed_1 (struct hotplug_slot *, u8 *, u8);

static inline int get_max_adapter_speed (struct hotplug_slot *hs, u8 *value)
{
	return get_max_adapter_speed_1 (hs, value, 1);
}
*/
static inline int get_cur_bus_info(struct slot **sl)
{
	int rc = 1;
	struct slot *slot_cur = *sl;

	debug("options = %x\n", slot_cur->ctrl->options);
	debug("revision = %x\n", slot_cur->ctrl->revision);

	if (READ_BUS_STATUS(slot_cur->ctrl))
		rc = ibmphp_hpc_readslot(slot_cur, READ_BUSSTATUS, NULL);

	if (rc)
		return rc;

	slot_cur->bus_on->current_speed = CURRENT_BUS_SPEED(slot_cur->busstatus);
	if (READ_BUS_MODE(slot_cur->ctrl))
		slot_cur->bus_on->current_bus_mode =
				CURRENT_BUS_MODE(slot_cur->busstatus);
	else
		slot_cur->bus_on->current_bus_mode = 0xFF;

	debug("busstatus = %x, bus_speed = %x, bus_mode = %x\n",
			slot_cur->busstatus,
			slot_cur->bus_on->current_speed,
			slot_cur->bus_on->current_bus_mode);

	*sl = slot_cur;
	return 0;
}

static inline int slot_update(struct slot **sl)
{
	int rc;
	rc = ibmphp_hpc_readslot(*sl, READ_ALLSTAT, NULL);
	if (rc)
		return rc;
	if (!init_flag)
		rc = get_cur_bus_info(sl);
	return rc;
}

static int __init get_max_slots(void)
{
	struct slot *slot_cur;
	u8 slot_count = 0;

	list_for_each_entry(slot_cur, &ibmphp_slot_head, ibm_slot_list) {
		/* sometimes the hot-pluggable slots start with 4 (not always from 1) */
		slot_count = max(slot_count, slot_cur->number);
	}
	return slot_count;
}

/* This routine will put the correct slot->device information per slot.  It's
 * called from initialization of the slot structures. It will also assign
 * interrupt numbers per each slot.
 * Parameters: struct slot
 * Returns 0 or errors
 */
int ibmphp_init_devno(struct slot **cur_slot)
{
	struct irq_routing_table *rtable;
	int len;
	int loop;
	int i;

	rtable = pcibios_get_irq_routing_table();
	if (!rtable) {
		err("no BIOS routing table...\n");
		return -ENOMEM;
	}

	len = (rtable->size - sizeof(struct irq_routing_table)) /
			sizeof(struct irq_info);

	if (!len) {
		kfree(rtable);
		return -1;
	}
	for (loop = 0; loop < len; loop++) {
		if ((*cur_slot)->number == rtable->slots[loop].slot &&
		    (*cur_slot)->bus == rtable->slots[loop].bus) {
			(*cur_slot)->device = PCI_SLOT(rtable->slots[loop].devfn);
			for (i = 0; i < 4; i++)
				(*cur_slot)->irq[i] = IO_APIC_get_PCI_irq_vector((int) (*cur_slot)->bus,
						(int) (*cur_slot)->device, i);

			debug("(*cur_slot)->irq[0] = %x\n",
					(*cur_slot)->irq[0]);
			debug("(*cur_slot)->irq[1] = %x\n",
					(*cur_slot)->irq[1]);
			debug("(*cur_slot)->irq[2] = %x\n",
					(*cur_slot)->irq[2]);
			debug("(*cur_slot)->irq[3] = %x\n",
					(*cur_slot)->irq[3]);

			debug("rtable->exclusive_irqs = %x\n",
					rtable->exclusive_irqs);
			debug("rtable->slots[loop].irq[0].bitmap = %x\n",
					rtable->slots[loop].irq[0].bitmap);
			debug("rtable->slots[loop].irq[1].bitmap = %x\n",
					rtable->slots[loop].irq[1].bitmap);
			debug("rtable->slots[loop].irq[2].bitmap = %x\n",
					rtable->slots[loop].irq[2].bitmap);
			debug("rtable->slots[loop].irq[3].bitmap = %x\n",
					rtable->slots[loop].irq[3].bitmap);

			debug("rtable->slots[loop].irq[0].link = %x\n",
					rtable->slots[loop].irq[0].link);
			debug("rtable->slots[loop].irq[1].link = %x\n",
					rtable->slots[loop].irq[1].link);
			debug("rtable->slots[loop].irq[2].link = %x\n",
					rtable->slots[loop].irq[2].link);
			debug("rtable->slots[loop].irq[3].link = %x\n",
					rtable->slots[loop].irq[3].link);
			debug("end of init_devno\n");
			kfree(rtable);
			return 0;
		}
	}

	kfree(rtable);
	return -1;
}

static inline int power_on(struct slot *slot_cur)
{
	u8 cmd = HPC_SLOT_ON;
	int retval;

	retval = ibmphp_hpc_writeslot(slot_cur, cmd);
	if (retval) {
		err("power on failed\n");
		return retval;
	}
	if (CTLR_RESULT(slot_cur->ctrl->status)) {
		err("command not completed successfully in power_on\n");
		return -EIO;
	}
	msleep(3000);	/* For ServeRAID cards, and some 66 PCI */
	return 0;
}

static inline int power_off(struct slot *slot_cur)
{
	u8 cmd = HPC_SLOT_OFF;
	int retval;

	retval = ibmphp_hpc_writeslot(slot_cur, cmd);
	if (retval) {
		err("power off failed\n");
		return retval;
	}
	if (CTLR_RESULT(slot_cur->ctrl->status)) {
		err("command not completed successfully in power_off\n");
		retval = -EIO;
	}
	return retval;
}

static int set_attention_status(struct hotplug_slot *hotplug_slot, u8 value)
{
	int rc = 0;
	struct slot *pslot;
	u8 cmd = 0x00;     /* avoid compiler warning */

	debug("set_attention_status - Entry hotplug_slot[%lx] value[%x]\n",
			(ulong) hotplug_slot, value);
	ibmphp_lock_operations();


	if (hotplug_slot) {
		switch (value) {
		case HPC_SLOT_ATTN_OFF:
			cmd = HPC_SLOT_ATTNOFF;
			break;
		case HPC_SLOT_ATTN_ON:
			cmd = HPC_SLOT_ATTNON;
			break;
		case HPC_SLOT_ATTN_BLINK:
			cmd = HPC_SLOT_BLINKLED;
			break;
		default:
			rc = -ENODEV;
			err("set_attention_status - Error : invalid input [%x]\n",
					value);
			break;
		}
		if (rc == 0) {
			pslot = hotplug_slot->private;
			if (pslot)
				rc = ibmphp_hpc_writeslot(pslot, cmd);
			else
				rc = -ENODEV;
		}
	} else
		rc = -ENODEV;

	ibmphp_unlock_operations();

	debug("set_attention_status - Exit rc[%d]\n", rc);
	return rc;
}

static int get_attention_status(struct hotplug_slot *hotplug_slot, u8 *value)
{
	int rc = -ENODEV;
	struct slot *pslot;
	struct slot myslot;

	debug("get_attention_status - Entry hotplug_slot[%lx] pvalue[%lx]\n",
					(ulong) hotplug_slot, (ulong) value);

	ibmphp_lock_operations();
	if (hotplug_slot) {
		pslot = hotplug_slot->private;
		if (pslot) {
			memcpy(&myslot, pslot, sizeof(struct slot));
			rc = ibmphp_hpc_readslot(pslot, READ_SLOTSTATUS,
						&(myslot.status));
			if (!rc)
				rc = ibmphp_hpc_readslot(pslot,
						READ_EXTSLOTSTATUS,
						&(myslot.ext_status));
			if (!rc)
				*value = SLOT_ATTN(myslot.status,
						myslot.ext_status);
		}
	}

	ibmphp_unlock_operations();
	debug("get_attention_status - Exit rc[%d] value[%x]\n", rc, *value);
	return rc;
}

static int get_latch_status(struct hotplug_slot *hotplug_slot, u8 *value)
{
	int rc = -ENODEV;
	struct slot *pslot;
	struct slot myslot;

	debug("get_latch_status - Entry hotplug_slot[%lx] pvalue[%lx]\n",
					(ulong) hotplug_slot, (ulong) value);
	ibmphp_lock_operations();
	if (hotplug_slot) {
		pslot = hotplug_slot->private;
		if (pslot) {
			memcpy(&myslot, pslot, sizeof(struct slot));
			rc = ibmphp_hpc_readslot(pslot, READ_SLOTSTATUS,
						&(myslot.status));
			if (!rc)
				*value = SLOT_LATCH(myslot.status);
		}
	}

	ibmphp_unlock_operations();
	debug("get_latch_status - Exit rc[%d] rc[%x] value[%x]\n",
			rc, rc, *value);
	return rc;
}


static int get_power_status(struct hotplug_slot *hotplug_slot, u8 *value)
{
	int rc = -ENODEV;
	struct slot *pslot;
	struct slot myslot;

	debug("get_power_status - Entry hotplug_slot[%lx] pvalue[%lx]\n",
					(ulong) hotplug_slot, (ulong) value);
	ibmphp_lock_operations();
	if (hotplug_slot) {
		pslot = hotplug_slot->private;
		if (pslot) {
			memcpy(&myslot, pslot, sizeof(struct slot));
			rc = ibmphp_hpc_readslot(pslot, READ_SLOTSTATUS,
						&(myslot.status));
			if (!rc)
				*value = SLOT_PWRGD(myslot.status);
		}
	}

	ibmphp_unlock_operations();
	debug("get_power_status - Exit rc[%d] rc[%x] value[%x]\n",
			rc, rc, *value);
	return rc;
}

static int get_adapter_present(struct hotplug_slot *hotplug_slot, u8 *value)
{
	int rc = -ENODEV;
	struct slot *pslot;
	u8 present;
	struct slot myslot;

	debug("get_adapter_status - Entry hotplug_slot[%lx] pvalue[%lx]\n",
					(ulong) hotplug_slot, (ulong) value);
	ibmphp_lock_operations();
	if (hotplug_slot) {
		pslot = hotplug_slot->private;
		if (pslot) {
			memcpy(&myslot, pslot, sizeof(struct slot));
			rc = ibmphp_hpc_readslot(pslot, READ_SLOTSTATUS,
						&(myslot.status));
			if (!rc) {
				present = SLOT_PRESENT(myslot.status);
				if (present == HPC_SLOT_EMPTY)
					*value = 0;
				else
					*value = 1;
			}
		}
	}

	ibmphp_unlock_operations();
	debug("get_adapter_present - Exit rc[%d] value[%x]\n", rc, *value);
	return rc;
}

static int get_max_bus_speed(struct slot *slot)
{
	int rc;
	u8 mode = 0;
	enum pci_bus_speed speed;
	struct pci_bus *bus = slot->hotplug_slot->pci_slot->bus;

	debug("%s - Entry slot[%p]\n", __func__, slot);

	ibmphp_lock_operations();
	mode = slot->supported_bus_mode;
	speed = slot->supported_speed;
	ibmphp_unlock_operations();

	switch (speed) {
	case BUS_SPEED_33:
		break;
	case BUS_SPEED_66:
		if (mode == BUS_MODE_PCIX)
			speed += 0x01;
		break;
	case BUS_SPEED_100:
	case BUS_SPEED_133:
		speed += 0x01;
		break;
	default:
		/* Note (will need to change): there would be soon 256, 512 also */
		rc = -ENODEV;
	}

	if (!rc)
		bus->max_bus_speed = speed;

	debug("%s - Exit rc[%d] speed[%x]\n", __func__, rc, speed);
	return rc;
}

/*
static int get_max_adapter_speed_1(struct hotplug_slot *hotplug_slot, u8 *value, u8 flag)
{
	int rc = -ENODEV;
	struct slot *pslot;
	struct slot myslot;

	debug("get_max_adapter_speed_1 - Entry hotplug_slot[%lx] pvalue[%lx]\n",
						(ulong)hotplug_slot, (ulong) value);

	if (flag)
		ibmphp_lock_operations();

	if (hotplug_slot && value) {
		pslot = hotplug_slot->private;
		if (pslot) {
			memcpy(&myslot, pslot, sizeof(struct slot));
			rc = ibmphp_hpc_readslot(pslot, READ_SLOTSTATUS,
						&(myslot.status));

			if (!(SLOT_LATCH (myslot.status)) &&
					(SLOT_PRESENT (myslot.status))) {
				rc = ibmphp_hpc_readslot(pslot,
						READ_EXTSLOTSTATUS,
						&(myslot.ext_status));
				if (!rc)
					*value = SLOT_SPEED(myslot.ext_status);
			} else
				*value = MAX_ADAPTER_NONE;
		}
	}

	if (flag)
		ibmphp_unlock_operations();

	debug("get_max_adapter_speed_1 - Exit rc[%d] value[%x]\n", rc, *value);
	return rc;
}

static int get_bus_name(struct hotplug_slot *hotplug_slot, char *value)
{
	int rc = -ENODEV;
	struct slot *pslot = NULL;

	debug("get_bus_name - Entry hotplug_slot[%lx]\n", (ulong)hotplug_slot);

	ibmphp_lock_operations();

	if (hotplug_slot) {
		pslot = hotplug_slot->private;
		if (pslot) {
			rc = 0;
			snprintf(value, 100, "Bus %x", pslot->bus);
		}
	} else
		rc = -ENODEV;

	ibmphp_unlock_operations();
	debug("get_bus_name - Exit rc[%d] value[%x]\n", rc, *value);
	return rc;
}
*/

/****************************************************************************
 * This routine will initialize the ops data structure used in the validate
 * function. It will also power off empty slots that are powered on since BIOS
 * leaves those on, albeit disconnected
 ****************************************************************************/
static int __init init_ops(void)
{
	struct slot *slot_cur;
	int retval;
	int rc;

	list_for_each_entry(slot_cur, &ibmphp_slot_head, ibm_slot_list) {
		debug("BEFORE GETTING SLOT STATUS, slot # %x\n",
							slot_cur->number);
		if (slot_cur->ctrl->revision == 0xFF)
			if (get_ctrl_revision(slot_cur,
						&slot_cur->ctrl->revision))
				return -1;

		if (slot_cur->bus_on->current_speed == 0xFF)
			if (get_cur_bus_info(&slot_cur))
				return -1;
		get_max_bus_speed(slot_cur);

		if (slot_cur->ctrl->options == 0xFF)
			if (get_hpc_options(slot_cur, &slot_cur->ctrl->options))
				return -1;

		retval = slot_update(&slot_cur);
		if (retval)
			return retval;

		debug("status = %x\n", slot_cur->status);
		debug("ext_status = %x\n", slot_cur->ext_status);
		debug("SLOT_POWER = %x\n", SLOT_POWER(slot_cur->status));
		debug("SLOT_PRESENT = %x\n", SLOT_PRESENT(slot_cur->status));
		debug("SLOT_LATCH = %x\n", SLOT_LATCH(slot_cur->status));

		if ((SLOT_PWRGD(slot_cur->status)) &&
		    !(SLOT_PRESENT(slot_cur->status)) &&
		    !(SLOT_LATCH(slot_cur->status))) {
			debug("BEFORE POWER OFF COMMAND\n");
				rc = power_off(slot_cur);
				if (rc)
					return rc;

	/*		retval = slot_update(&slot_cur);
	 *		if (retval)
	 *			return retval;
	 *		ibmphp_update_slot_info(slot_cur);
	 */
		}
	}
	init_flag = 0;
	return 0;
}

/* This operation will check whether the slot is within the bounds and
 * the operation is valid to perform on that slot
 * Parameters: slot, operation
 * Returns: 0 or error codes
 */
static int validate(struct slot *slot_cur, int opn)
{
	int number;
	int retval;

	if (!slot_cur)
		return -ENODEV;
	number = slot_cur->number;
	if ((number > max_slots) || (number < 0))
		return -EBADSLT;
	debug("slot_number in validate is %d\n", slot_cur->number);

	retval = slot_update(&slot_cur);
	if (retval)
		return retval;

	switch (opn) {
		case ENABLE:
			if (!(SLOT_PWRGD(slot_cur->status)) &&
			     (SLOT_PRESENT(slot_cur->status)) &&
			     !(SLOT_LATCH(slot_cur->status)))
				return 0;
			break;
		case DISABLE:
			if ((SLOT_PWRGD(slot_cur->status)) &&
			    (SLOT_PRESENT(slot_cur->status)) &&
			    !(SLOT_LATCH(slot_cur->status)))
				return 0;
			break;
		default:
			break;
	}
	err("validate failed....\n");
	return -EINVAL;
}

/****************************************************************************
 * This routine is for updating the data structures in the hotplug core
 * Parameters: struct slot
 * Returns: 0 or error
 ****************************************************************************/
int ibmphp_update_slot_info(struct slot *slot_cur)
{
	struct hotplug_slot_info *info;
	struct pci_bus *bus = slot_cur->hotplug_slot->pci_slot->bus;
	int rc;
	u8 bus_speed;
	u8 mode;

	info = kmalloc(sizeof(struct hotplug_slot_info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->power_status = SLOT_PWRGD(slot_cur->status);
	info->attention_status = SLOT_ATTN(slot_cur->status,
						slot_cur->ext_status);
	info->latch_status = SLOT_LATCH(slot_cur->status);
	if (!SLOT_PRESENT(slot_cur->status)) {
		info->adapter_status = 0;
/*		info->max_adapter_speed_status = MAX_ADAPTER_NONE; */
	} else {
		info->adapter_status = 1;
/*		get_max_adapter_speed_1(slot_cur->hotplug_slot,
					&info->max_adapter_speed_status, 0); */
	}

	bus_speed = slot_cur->bus_on->current_speed;
	mode = slot_cur->bus_on->current_bus_mode;

	switch (bus_speed) {
		case BUS_SPEED_33:
			break;
		case BUS_SPEED_66:
			if (mode == BUS_MODE_PCIX)
				bus_speed += 0x01;
			else if (mode == BUS_MODE_PCI)
				;
			else
				bus_speed = PCI_SPEED_UNKNOWN;
			break;
		case BUS_SPEED_100:
		case BUS_SPEED_133:
			bus_speed += 0x01;
			break;
		default:
			bus_speed = PCI_SPEED_UNKNOWN;
	}

	bus->cur_bus_speed = bus_speed;
	// To do: bus_names

	rc = pci_hp_change_slot_info(slot_cur->hotplug_slot, info);
	kfree(info);
	return rc;
}


/******************************************************************************
 * This function will return the pci_func, given bus and devfunc, or NULL.  It
 * is called from visit routines
 ******************************************************************************/

static struct pci_func *ibm_slot_find(u8 busno, u8 device, u8 function)
{
	struct pci_func *func_cur;
	struct slot *slot_cur;
	list_for_each_entry(slot_cur, &ibmphp_slot_head, ibm_slot_list) {
		if (slot_cur->func) {
			func_cur = slot_cur->func;
			while (func_cur) {
				if ((func_cur->busno == busno) &&
						(func_cur->device == device) &&
						(func_cur->function == function))
					return func_cur;
				func_cur = func_cur->next;
			}
		}
	}
	return NULL;
}

/*************************************************************
 * This routine frees up memory used by struct slot, including
 * the pointers to pci_func, bus, hotplug_slot, controller,
 * and deregistering from the hotplug core
 *************************************************************/
static void free_slots(void)
{
	struct slot *slot_cur, *next;

	debug("%s -- enter\n", __func__);

	list_for_each_entry_safe(slot_cur, next, &ibmphp_slot_head,
				 ibm_slot_list) {
		pci_hp_deregister(slot_cur->hotplug_slot);
	}
	debug("%s -- exit\n", __func__);
}

static void ibm_unconfigure_device(struct pci_func *func)
{
	struct pci_dev *temp;
	u8 j;

	debug("inside %s\n", __func__);
	debug("func->device = %x, func->function = %x\n",
					func->device, func->function);
	debug("func->device << 3 | 0x0  = %x\n", func->device << 3 | 0x0);

	pci_lock_rescan_remove();

	for (j = 0; j < 0x08; j++) {
		temp = pci_get_domain_bus_and_slot(0, func->busno,
						   (func->device << 3) | j);
		if (temp) {
			pci_stop_and_remove_bus_device(temp);
			pci_dev_put(temp);
		}
	}

	pci_dev_put(func->dev);

	pci_unlock_rescan_remove();
}

/*
 * The following function is to fix kernel bug regarding
 * getting bus entries, here we manually add those primary
 * bus entries to kernel bus structure whenever apply
 */
static u8 bus_structure_fixup(u8 busno)
{
	struct pci_bus *bus, *b;
	struct pci_dev *dev;
	u16 l;

	if (pci_find_bus(0, busno) || !(ibmphp_find_same_bus_num(busno)))
		return 1;

	bus = kmalloc(sizeof(*bus), GFP_KERNEL);
	if (!bus)
		return 1;

	dev = kmalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		kfree(bus);
		return 1;
	}

	bus->number = busno;
	bus->ops = ibmphp_pci_bus->ops;
	dev->bus = bus;
	for (dev->devfn = 0; dev->devfn < 256; dev->devfn += 8) {
		if (!pci_read_config_word(dev, PCI_VENDOR_ID, &l) &&
					(l != 0x0000) && (l != 0xffff)) {
			debug("%s - Inside bus_structure_fixup()\n",
							__func__);
			b = pci_scan_bus(busno, ibmphp_pci_bus->ops, NULL);
			if (!b)
				continue;

			pci_bus_add_devices(b);
			break;
		}
	}

	kfree(dev);
	kfree(bus);

	return 0;
}

static int ibm_configure_device(struct pci_func *func)
{
	struct pci_bus *child;
	int num;
	int flag = 0;	/* this is to make sure we don't double scan the bus,
					for bridged devices primarily */

	pci_lock_rescan_remove();

	if (!(bus_structure_fixup(func->busno)))
		flag = 1;
	if (func->dev == NULL)
		func->dev = pci_get_domain_bus_and_slot(0, func->busno,
				PCI_DEVFN(func->device, func->function));

	if (func->dev == NULL) {
		struct pci_bus *bus = pci_find_bus(0, func->busno);
		if (!bus)
			goto out;

		num = pci_scan_slot(bus,
				PCI_DEVFN(func->device, func->function));
		if (num)
			pci_bus_add_devices(bus);

		func->dev = pci_get_domain_bus_and_slot(0, func->busno,
				PCI_DEVFN(func->device, func->function));
		if (func->dev == NULL) {
			err("ERROR... : pci_dev still NULL\n");
			goto out;
		}
	}
	if (!(flag) && (func->dev->hdr_type == PCI_HEADER_TYPE_BRIDGE)) {
		pci_hp_add_bridge(func->dev);
		child = func->dev->subordinate;
		if (child)
			pci_bus_add_devices(child);
	}

 out:
	pci_unlock_rescan_remove();
	return 0;
}

/*******************************************************
 * Returns whether the bus is empty or not
 *******************************************************/
static int is_bus_empty(struct slot *slot_cur)
{
	int rc;
	struct slot *tmp_slot;
	u8 i = slot_cur->bus_on->slot_min;

	while (i <= slot_cur->bus_on->slot_max) {
		if (i == slot_cur->number) {
			i++;
			continue;
		}
		tmp_slot = ibmphp_get_slot_from_physical_num(i);
		if (!tmp_slot)
			return 0;
		rc = slot_update(&tmp_slot);
		if (rc)
			return 0;
		if (SLOT_PRESENT(tmp_slot->status) &&
					SLOT_PWRGD(tmp_slot->status))
			return 0;
		i++;
	}
	return 1;
}

/***********************************************************
 * If the HPC permits and the bus currently empty, tries to set the
 * bus speed and mode at the maximum card and bus capability
 * Parameters: slot
 * Returns: bus is set (0) or error code
 ***********************************************************/
static int set_bus(struct slot *slot_cur)
{
	int rc;
	u8 speed;
	u8 cmd = 0x0;
	int retval;
	static const struct pci_device_id ciobx[] = {
		{ PCI_DEVICE(PCI_VENDOR_ID_SERVERWORKS, 0x0101) },
		{ },
	};

	debug("%s - entry slot # %d\n", __func__, slot_cur->number);
	if (SET_BUS_STATUS(slot_cur->ctrl) && is_bus_empty(slot_cur)) {
		rc = slot_update(&slot_cur);
		if (rc)
			return rc;
		speed = SLOT_SPEED(slot_cur->ext_status);
		debug("ext_status = %x, speed = %x\n", slot_cur->ext_status, speed);
		switch (speed) {
		case HPC_SLOT_SPEED_33:
			cmd = HPC_BUS_33CONVMODE;
			break;
		case HPC_SLOT_SPEED_66:
			if (SLOT_PCIX(slot_cur->ext_status)) {
				if ((slot_cur->supported_speed >= BUS_SPEED_66) &&
						(slot_cur->supported_bus_mode == BUS_MODE_PCIX))
					cmd = HPC_BUS_66PCIXMODE;
				else if (!SLOT_BUS_MODE(slot_cur->ext_status))
					/* if max slot/bus capability is 66 pci
					and there's no bus mode mismatch, then
					the adapter supports 66 pci */
					cmd = HPC_BUS_66CONVMODE;
				else
					cmd = HPC_BUS_33CONVMODE;
			} else {
				if (slot_cur->supported_speed >= BUS_SPEED_66)
					cmd = HPC_BUS_66CONVMODE;
				else
					cmd = HPC_BUS_33CONVMODE;
			}
			break;
		case HPC_SLOT_SPEED_133:
			switch (slot_cur->supported_speed) {
			case BUS_SPEED_33:
				cmd = HPC_BUS_33CONVMODE;
				break;
			case BUS_SPEED_66:
				if (slot_cur->supported_bus_mode == BUS_MODE_PCIX)
					cmd = HPC_BUS_66PCIXMODE;
				else
					cmd = HPC_BUS_66CONVMODE;
				break;
			case BUS_SPEED_100:
				cmd = HPC_BUS_100PCIXMODE;
				break;
			case BUS_SPEED_133:
				/* This is to take care of the bug in CIOBX chip */
				if (pci_dev_present(ciobx))
					ibmphp_hpc_writeslot(slot_cur,
							HPC_BUS_100PCIXMODE);
				cmd = HPC_BUS_133PCIXMODE;
				break;
			default:
				err("Wrong bus speed\n");
				return -ENODEV;
			}
			break;
		default:
			err("wrong slot speed\n");
			return -ENODEV;
		}
		debug("setting bus speed for slot %d, cmd %x\n",
						slot_cur->number, cmd);
		retval = ibmphp_hpc_writeslot(slot_cur, cmd);
		if (retval) {
			err("setting bus speed failed\n");
			return retval;
		}
		if (CTLR_RESULT(slot_cur->ctrl->status)) {
			err("command not completed successfully in set_bus\n");
			return -EIO;
		}
	}
	/* This is for x440, once Brandon fixes the firmware,
	will not need this delay */
	msleep(1000);
	debug("%s -Exit\n", __func__);
	return 0;
}

/* This routine checks the bus limitations that the slot is on from the BIOS.
 * This is used in deciding whether or not to power up the slot.
 * (electrical/spec limitations. For example, >1 133 MHz or >2 66 PCI cards on
 * same bus)
 * Parameters: slot
 * Returns: 0 = no limitations, -EINVAL = exceeded limitations on the bus
 */
static int check_limitations(struct slot *slot_cur)
{
	u8 i;
	struct slot *tmp_slot;
	u8 count = 0;
	u8 limitation = 0;

	for (i = slot_cur->bus_on->slot_min; i <= slot_cur->bus_on->slot_max; i++) {
		tmp_slot = ibmphp_get_slot_from_physical_num(i);
		if (!tmp_slot)
			return -ENODEV;
		if ((SLOT_PWRGD(tmp_slot->status)) &&
					!(SLOT_CONNECT(tmp_slot->status)))
			count++;
	}
	get_cur_bus_info(&slot_cur);
	switch (slot_cur->bus_on->current_speed) {
	case BUS_SPEED_33:
		limitation = slot_cur->bus_on->slots_at_33_conv;
		break;
	case BUS_SPEED_66:
		if (slot_cur->bus_on->current_bus_mode == BUS_MODE_PCIX)
			limitation = slot_cur->bus_on->slots_at_66_pcix;
		else
			limitation = slot_cur->bus_on->slots_at_66_conv;
		break;
	case BUS_SPEED_100:
		limitation = slot_cur->bus_on->slots_at_100_pcix;
		break;
	case BUS_SPEED_133:
		limitation = slot_cur->bus_on->slots_at_133_pcix;
		break;
	}

	if ((count + 1) > limitation)
		return -EINVAL;
	return 0;
}

static inline void print_card_capability(struct slot *slot_cur)
{
	info("capability of the card is ");
	if ((slot_cur->ext_status & CARD_INFO) == PCIX133)
		info("   133 MHz PCI-X\n");
	else if ((slot_cur->ext_status & CARD_INFO) == PCIX66)
		info("    66 MHz PCI-X\n");
	else if ((slot_cur->ext_status & CARD_INFO) == PCI66)
		info("    66 MHz PCI\n");
	else
		info("    33 MHz PCI\n");

}

/* This routine will power on the slot, configure the device(s) and find the
 * drivers for them.
 * Parameters: hotplug_slot
 * Returns: 0 or failure codes
 */
static int enable_slot(struct hotplug_slot *hs)
{
	int rc, i, rcpr;
	struct slot *slot_cur;
	u8 function;
	struct pci_func *tmp_func;

	ibmphp_lock_operations();

	debug("ENABLING SLOT........\n");
	slot_cur = hs->private;

	rc = validate(slot_cur, ENABLE);
	if (rc) {
		err("validate function failed\n");
		goto error_nopower;
	}

	attn_LED_blink(slot_cur);

	rc = set_bus(slot_cur);
	if (rc) {
		err("was not able to set the bus\n");
		goto error_nopower;
	}

	/*-----------------debugging------------------------------*/
	get_cur_bus_info(&slot_cur);
	debug("the current bus speed right after set_bus = %x\n",
					slot_cur->bus_on->current_speed);
	/*----------------------------------------------------------*/

	rc = check_limitations(slot_cur);
	if (rc) {
		err("Adding this card exceeds the limitations of this bus.\n");
		err("(i.e., >1 133MHz cards running on same bus, or >2 66 PCI cards running on same bus.\n");
		err("Try hot-adding into another bus\n");
		rc = -EINVAL;
		goto error_nopower;
	}

	rc = power_on(slot_cur);

	if (rc) {
		err("something wrong when powering up... please see below for details\n");
		/* need to turn off before on, otherwise, blinking overwrites */
		attn_off(slot_cur);
		attn_on(slot_cur);
		if (slot_update(&slot_cur)) {
			attn_off(slot_cur);
			attn_on(slot_cur);
			rc = -ENODEV;
			goto exit;
		}
		/* Check to see the error of why it failed */
		if ((SLOT_POWER(slot_cur->status)) &&
					!(SLOT_PWRGD(slot_cur->status)))
			err("power fault occurred trying to power up\n");
		else if (SLOT_BUS_SPEED(slot_cur->status)) {
			err("bus speed mismatch occurred.  please check current bus speed and card capability\n");
			print_card_capability(slot_cur);
		} else if (SLOT_BUS_MODE(slot_cur->ext_status)) {
			err("bus mode mismatch occurred.  please check current bus mode and card capability\n");
			print_card_capability(slot_cur);
		}
		ibmphp_update_slot_info(slot_cur);
		goto exit;
	}
	debug("after power_on\n");
	/*-----------------------debugging---------------------------*/
	get_cur_bus_info(&slot_cur);
	debug("the current bus speed right after power_on = %x\n",
					slot_cur->bus_on->current_speed);
	/*----------------------------------------------------------*/

	rc = slot_update(&slot_cur);
	if (rc)
		goto error_power;

	rc = -EINVAL;
	if (SLOT_POWER(slot_cur->status) && !(SLOT_PWRGD(slot_cur->status))) {
		err("power fault occurred trying to power up...\n");
		goto error_power;
	}
	if (SLOT_POWER(slot_cur->status) && (SLOT_BUS_SPEED(slot_cur->status))) {
		err("bus speed mismatch occurred.  please check current bus speed and card capability\n");
		print_card_capability(slot_cur);
		goto error_power;
	}
	/* Don't think this case will happen after above checks...
	 * but just in case, for paranoia sake */
	if (!(SLOT_POWER(slot_cur->status))) {
		err("power on failed...\n");
		goto error_power;
	}

	slot_cur->func = kzalloc(sizeof(struct pci_func), GFP_KERNEL);
	if (!slot_cur->func) {
		/* We cannot do update_slot_info here, since no memory for
		 * kmalloc n.e.ways, and update_slot_info allocates some */
		rc = -ENOMEM;
		goto error_power;
	}
	slot_cur->func->busno = slot_cur->bus;
	slot_cur->func->device = slot_cur->device;
	for (i = 0; i < 4; i++)
		slot_cur->func->irq[i] = slot_cur->irq[i];

	debug("b4 configure_card, slot_cur->bus = %x, slot_cur->device = %x\n",
					slot_cur->bus, slot_cur->device);

	if (ibmphp_configure_card(slot_cur->func, slot_cur->number)) {
		err("configure_card was unsuccessful...\n");
		/* true because don't need to actually deallocate resources,
		 * just remove references */
		ibmphp_unconfigure_card(&slot_cur, 1);
		debug("after unconfigure_card\n");
		slot_cur->func = NULL;
		rc = -ENOMEM;
		goto error_power;
	}

	function = 0x00;
	do {
		tmp_func = ibm_slot_find(slot_cur->bus, slot_cur->func->device,
							function++);
		if (tmp_func && !(tmp_func->dev))
			ibm_configure_device(tmp_func);
	} while (tmp_func);

	attn_off(slot_cur);
	if (slot_update(&slot_cur)) {
		rc = -EFAULT;
		goto exit;
	}
	ibmphp_print_test();
	rc = ibmphp_update_slot_info(slot_cur);
exit:
	ibmphp_unlock_operations();
	return rc;

error_nopower:
	attn_off(slot_cur);	/* need to turn off if was blinking b4 */
	attn_on(slot_cur);
error_cont:
	rcpr = slot_update(&slot_cur);
	if (rcpr) {
		rc = rcpr;
		goto exit;
	}
	ibmphp_update_slot_info(slot_cur);
	goto exit;

error_power:
	attn_off(slot_cur);	/* need to turn off if was blinking b4 */
	attn_on(slot_cur);
	rcpr = power_off(slot_cur);
	if (rcpr) {
		rc = rcpr;
		goto exit;
	}
	goto error_cont;
}

/**************************************************************
* HOT REMOVING ADAPTER CARD                                   *
* INPUT: POINTER TO THE HOTPLUG SLOT STRUCTURE                *
* OUTPUT: SUCCESS 0 ; FAILURE: UNCONFIGURE , VALIDATE         *
*		DISABLE POWER ,                               *
**************************************************************/
static int ibmphp_disable_slot(struct hotplug_slot *hotplug_slot)
{
	struct slot *slot = hotplug_slot->private;
	int rc;

	ibmphp_lock_operations();
	rc = ibmphp_do_disable_slot(slot);
	ibmphp_unlock_operations();
	return rc;
}

int ibmphp_do_disable_slot(struct slot *slot_cur)
{
	int rc;
	u8 flag;

	debug("DISABLING SLOT...\n");

	if ((slot_cur == NULL) || (slot_cur->ctrl == NULL))
		return -ENODEV;

	flag = slot_cur->flag;
	slot_cur->flag = 1;

	if (flag == 1) {
		rc = validate(slot_cur, DISABLE);
			/* checking if powered off already & valid slot # */
		if (rc)
			goto error;
	}
	attn_LED_blink(slot_cur);

	if (slot_cur->func == NULL) {
		/* We need this for functions that were there on bootup */
		slot_cur->func = kzalloc(sizeof(struct pci_func), GFP_KERNEL);
		if (!slot_cur->func) {
			rc = -ENOMEM;
			goto error;
		}
		slot_cur->func->busno = slot_cur->bus;
		slot_cur->func->device = slot_cur->device;
	}

	ibm_unconfigure_device(slot_cur->func);

	/*
	 * If we got here from latch suddenly opening on operating card or
	 * a power fault, there's no power to the card, so cannot
	 * read from it to determine what resources it occupied.  This operation
	 * is forbidden anyhow.  The best we can do is remove it from kernel
	 * lists at least */

	if (!flag) {
		attn_off(slot_cur);
		return 0;
	}

	rc = ibmphp_unconfigure_card(&slot_cur, 0);
	slot_cur->func = NULL;
	debug("in disable_slot. after unconfigure_card\n");
	if (rc) {
		err("could not unconfigure card.\n");
		goto error;
	}

	rc = ibmphp_hpc_writeslot(slot_cur, HPC_SLOT_OFF);
	if (rc)
		goto error;

	attn_off(slot_cur);
	rc = slot_update(&slot_cur);
	if (rc)
		goto exit;

	rc = ibmphp_update_slot_info(slot_cur);
	ibmphp_print_test();
exit:
	return rc;

error:
	/*  Need to turn off if was blinking b4 */
	attn_off(slot_cur);
	attn_on(slot_cur);
	if (slot_update(&slot_cur)) {
		rc = -EFAULT;
		goto exit;
	}
	if (flag)
		ibmphp_update_slot_info(slot_cur);
	goto exit;
}

struct hotplug_slot_ops ibmphp_hotplug_slot_ops = {
	.set_attention_status =		set_attention_status,
	.enable_slot =			enable_slot,
	.disable_slot =			ibmphp_disable_slot,
	.hardware_test =		NULL,
	.get_power_status =		get_power_status,
	.get_attention_status =		get_attention_status,
	.get_latch_status =		get_latch_status,
	.get_adapter_status =		get_adapter_present,
/*	.get_max_adapter_speed =	get_max_adapter_speed,
	.get_bus_name_status =		get_bus_name,
*/
};

static void ibmphp_unload(void)
{
	free_slots();
	debug("after slots\n");
	ibmphp_free_resources();
	debug("after resources\n");
	ibmphp_free_bus_info_queue();
	debug("after bus info\n");
	ibmphp_free_ebda_hpc_queue();
	debug("after ebda hpc\n");
	ibmphp_free_ebda_pci_rsrc_queue();
	debug("after ebda pci rsrc\n");
	kfree(ibmphp_pci_bus);
}

static int __init ibmphp_init(void)
{
	struct pci_bus *bus;
	int i = 0;
	int rc = 0;

	init_flag = 1;

	info(DRIVER_DESC " version: " DRIVER_VERSION "\n");

	ibmphp_pci_bus = kmalloc(sizeof(*ibmphp_pci_bus), GFP_KERNEL);
	if (!ibmphp_pci_bus) {
		rc = -ENOMEM;
		goto exit;
	}

	bus = pci_find_bus(0, 0);
	if (!bus) {
		err("Can't find the root pci bus, can not continue\n");
		rc = -ENODEV;
		goto error;
	}
	memcpy(ibmphp_pci_bus, bus, sizeof(*ibmphp_pci_bus));

	ibmphp_debug = debug;

	ibmphp_hpc_initvars();

	for (i = 0; i < 16; i++)
		irqs[i] = 0;

	rc = ibmphp_access_ebda();
	if (rc)
		goto error;
	debug("after ibmphp_access_ebda()\n");

	rc = ibmphp_rsrc_init();
	if (rc)
		goto error;
	debug("AFTER Resource & EBDA INITIALIZATIONS\n");

	max_slots = get_max_slots();

	rc = ibmphp_register_pci();
	if (rc)
		goto error;

	if (init_ops()) {
		rc = -ENODEV;
		goto error;
	}

	ibmphp_print_test();
	rc = ibmphp_hpc_start_poll_thread();
	if (rc)
		goto error;

exit:
	return rc;

error:
	ibmphp_unload();
	goto exit;
}

static void __exit ibmphp_exit(void)
{
	ibmphp_hpc_stop_poll_thread();
	debug("after polling\n");
	ibmphp_unload();
	debug("done\n");
}

module_init(ibmphp_init);
module_exit(ibmphp_exit);
