/*
 * pnpbios -- PnP BIOS driver
 *
 * This driver provides access to Plug-'n'-Play services provided by
 * the PnP BIOS firmware, described in the following documents:
 *   Plug and Play BIOS Specification, Version 1.0A, 5 May 1994
 *   Plug and Play BIOS Clarification Paper, 6 October 1994
 *     Compaq Computer Corporation, Phoenix Technologies Ltd., Intel Corp.
 * 
 * Originally (C) 1998 Christian Schmidt <schmidt@digadd.de>
 * Modifications (C) 1998 Tom Lees <tom@lpsg.demon.co.uk>
 * Minor reorganizations by David Hinds <dahinds@users.sourceforge.net>
 * Further modifications (C) 2001, 2002 by:
 *   Alan Cox <alan@redhat.com>
 *   Thomas Hood
 *   Brian Gerst <bgerst@didntduck.org>
 *
 * Ported to the PnP Layer and several additional improvements (C) 2002
 * by Adam Belay <ambx1@neo.rr.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
 
/* Change Log
 *
 * Adam Belay - <ambx1@neo.rr.com> - March 16, 2003
 * rev 1.01	Only call pnp_bios_dev_node_info once
 *		Added pnpbios_print_status
 *		Added several new error messages and info messages
 *		Added pnpbios_interface_attach_device
 *		integrated core and proc init system
 *		Introduced PNPMODE flags
 *		Removed some useless includes
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/linkage.h>
#include <linux/kernel.h>
#include <linux/pnpbios.h>
#include <linux/device.h>
#include <linux/pnp.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/slab.h>
#include <linux/completion.h>
#include <linux/spinlock.h>
#include <linux/dmi.h>
#include <linux/delay.h>
#include <linux/acpi.h>
#include <linux/freezer.h>
#include <linux/kthread.h>

#include <asm/page.h>
#include <asm/desc.h>
#include <asm/system.h>
#include <asm/byteorder.h>

#include "pnpbios.h"


/*
 *
 * PnP BIOS INTERFACE
 *
 */

static union pnp_bios_install_struct * pnp_bios_install = NULL;

int pnp_bios_present(void)
{
	return (pnp_bios_install != NULL);
}

struct pnp_dev_node_info node_info;

/*
 *
 * DOCKING FUNCTIONS
 *
 */

#ifdef CONFIG_HOTPLUG

static int unloading = 0;
static struct completion unload_sem;

/*
 * (Much of this belongs in a shared routine somewhere)
 */
 
static int pnp_dock_event(int dock, struct pnp_docking_station_info *info)
{
	char *argv [3], **envp, *buf, *scratch;
	int i = 0, value;

	if (!current->fs->root) {
		return -EAGAIN;
	}
	if (!(envp = kcalloc(20, sizeof (char *), GFP_KERNEL))) {
		return -ENOMEM;
	}
	if (!(buf = kzalloc(256, GFP_KERNEL))) {
		kfree (envp);
		return -ENOMEM;
	}

	/* FIXME: if there are actual users of this, it should be integrated into
	 * the driver core and use the usual infrastructure like sysfs and uevents */
	argv [0] = "/sbin/pnpbios";
	argv [1] = "dock";
	argv [2] = NULL;

	/* minimal command environment */
	envp [i++] = "HOME=/";
	envp [i++] = "PATH=/sbin:/bin:/usr/sbin:/usr/bin";

#ifdef	DEBUG
	/* hint that policy agent should enter no-stdout debug mode */
	envp [i++] = "DEBUG=kernel";
#endif
	/* extensible set of named bus-specific parameters,
	 * supporting multiple driver selection algorithms.
	 */
	scratch = buf;

	/* action:  add, remove */
	envp [i++] = scratch;
	scratch += sprintf (scratch, "ACTION=%s", dock?"add":"remove") + 1;

	/* Report the ident for the dock */
	envp [i++] = scratch;
	scratch += sprintf (scratch, "DOCK=%x/%x/%x",
		info->location_id, info->serial, info->capabilities);
	envp[i] = NULL;
	
	value = call_usermodehelper (argv [0], argv, envp, 0);
	kfree (buf);
	kfree (envp);
	return 0;
}

/*
 * Poll the PnP docking at regular intervals
 */
static int pnp_dock_thread(void * unused)
{
	static struct pnp_docking_station_info now;
	int docked = -1, d = 0;
	while (!unloading)
	{
		int status;
		
		/*
		 * Poll every 2 seconds
		 */
		msleep_interruptible(2000);

		if (try_to_freeze())
			continue;

		status = pnp_bios_dock_station_info(&now);

		switch(status)
		{
			/*
			 * No dock to manage
			 */
			case PNP_FUNCTION_NOT_SUPPORTED:
				complete_and_exit(&unload_sem, 0);
			case PNP_SYSTEM_NOT_DOCKED:
				d = 0;
				break;
			case PNP_SUCCESS:
				d = 1;
				break;
			default:
				pnpbios_print_status( "pnp_dock_thread", status );
				continue;
		}
		if(d != docked)
		{
			if(pnp_dock_event(d, &now)==0)
			{
				docked = d;
#if 0
				printk(KERN_INFO "PnPBIOS: Docking station %stached\n", docked?"at":"de");
#endif
			}
		}
	}
	complete_and_exit(&unload_sem, 0);
}

#endif   /* CONFIG_HOTPLUG */

static int pnpbios_get_resources(struct pnp_dev * dev, struct pnp_resource_table * res)
{
	u8 nodenum = dev->number;
	struct pnp_bios_node * node;

	/* just in case */
	if(!pnpbios_is_dynamic(dev))
		return -EPERM;

	node = kzalloc(node_info.max_node_size, GFP_KERNEL);
	if (!node)
		return -1;
	if (pnp_bios_get_dev_node(&nodenum, (char )PNPMODE_DYNAMIC, node)) {
		kfree(node);
		return -ENODEV;
	}
	pnpbios_read_resources_from_node(res, node);
	dev->active = pnp_is_active(dev);
	kfree(node);
	return 0;
}

static int pnpbios_set_resources(struct pnp_dev * dev, struct pnp_resource_table * res)
{
	u8 nodenum = dev->number;
	struct pnp_bios_node * node;
	int ret;

	/* just in case */
	if (!pnpbios_is_dynamic(dev))
		return -EPERM;

	node = kzalloc(node_info.max_node_size, GFP_KERNEL);
	if (!node)
		return -1;
	if (pnp_bios_get_dev_node(&nodenum, (char )PNPMODE_DYNAMIC, node)) {
		kfree(node);
		return -ENODEV;
	}
	if(pnpbios_write_resources_to_node(res, node)<0) {
		kfree(node);
		return -1;
	}
	ret = pnp_bios_set_dev_node(node->handle, (char)PNPMODE_DYNAMIC, node);
	kfree(node);
	if (ret > 0)
		ret = -1;
	return ret;
}

static void pnpbios_zero_data_stream(struct pnp_bios_node * node)
{
	unsigned char * p = (char *)node->data;
	unsigned char * end = (char *)(node->data + node->size);
	unsigned int len;
	int i;
	while ((char *)p < (char *)end) {
		if(p[0] & 0x80) { /* large tag */
			len = (p[2] << 8) | p[1];
			p += 3;
		} else {
			if (((p[0]>>3) & 0x0f) == 0x0f)
				return;
			len = p[0] & 0x07;
			p += 1;
		}
		for (i = 0; i < len; i++)
			p[i] = 0;
		p += len;
	}
	printk(KERN_ERR "PnPBIOS: Resource structure did not contain an end tag.\n");
}

static int pnpbios_disable_resources(struct pnp_dev *dev)
{
	struct pnp_bios_node * node;
	u8 nodenum = dev->number;
	int ret;

	/* just in case */
	if(dev->flags & PNPBIOS_NO_DISABLE || !pnpbios_is_dynamic(dev))
		return -EPERM;

	node = kzalloc(node_info.max_node_size, GFP_KERNEL);
	if (!node)
		return -ENOMEM;

	if (pnp_bios_get_dev_node(&nodenum, (char )PNPMODE_DYNAMIC, node)) {
		kfree(node);
		return -ENODEV;
	}
	pnpbios_zero_data_stream(node);

	ret = pnp_bios_set_dev_node(dev->number, (char)PNPMODE_DYNAMIC, node);
	kfree(node);
	if (ret > 0)
		ret = -1;
	return ret;
}

/* PnP Layer support */

struct pnp_protocol pnpbios_protocol = {
	.name	= "Plug and Play BIOS",
	.get	= pnpbios_get_resources,
	.set	= pnpbios_set_resources,
	.disable = pnpbios_disable_resources,
};

static int insert_device(struct pnp_dev *dev, struct pnp_bios_node * node)
{
	struct list_head * pos;
	struct pnp_dev * pnp_dev;
	struct pnp_id *dev_id;
	char id[8];

	/* check if the device is already added */
	dev->number = node->handle;
	list_for_each (pos, &pnpbios_protocol.devices){
		pnp_dev = list_entry(pos, struct pnp_dev, protocol_list);
		if (dev->number == pnp_dev->number)
			return -1;
	}

	/* set the initial values for the PnP device */
	dev_id = kzalloc(sizeof(struct pnp_id), GFP_KERNEL);
	if (!dev_id)
		return -1;
	pnpid32_to_pnpid(node->eisa_id,id);
	memcpy(dev_id->id,id,7);
	pnp_add_id(dev_id, dev);
	pnpbios_parse_data_stream(dev, node);
	dev->active = pnp_is_active(dev);
	dev->flags = node->flags;
	if (!(dev->flags & PNPBIOS_NO_CONFIG))
		dev->capabilities |= PNP_CONFIGURABLE;
	if (!(dev->flags & PNPBIOS_NO_DISABLE) && pnpbios_is_dynamic(dev))
		dev->capabilities |= PNP_DISABLE;
	dev->capabilities |= PNP_READ;
	if (pnpbios_is_dynamic(dev))
		dev->capabilities |= PNP_WRITE;
	if (dev->flags & PNPBIOS_REMOVABLE)
		dev->capabilities |= PNP_REMOVABLE;
	dev->protocol = &pnpbios_protocol;

	/* clear out the damaged flags */
	if (!dev->active)
		pnp_init_resource_table(&dev->res);

	pnp_add_device(dev);
	pnpbios_interface_attach_device(node);

	return 0;
}

static void __init build_devlist(void)
{
	u8 nodenum;
	unsigned int nodes_got = 0;
	unsigned int devs = 0;
	struct pnp_bios_node *node;
	struct pnp_dev *dev;

	node = kzalloc(node_info.max_node_size, GFP_KERNEL);
	if (!node)
		return;

	for(nodenum=0; nodenum<0xff; ) {
		u8 thisnodenum = nodenum;
		/* eventually we will want to use PNPMODE_STATIC here but for now
		 * dynamic will help us catch buggy bioses to add to the blacklist.
		 */
		if (!pnpbios_dont_use_current_config) {
			if (pnp_bios_get_dev_node(&nodenum, (char )PNPMODE_DYNAMIC, node))
				break;
		} else {
			if (pnp_bios_get_dev_node(&nodenum, (char )PNPMODE_STATIC, node))
				break;
		}
		nodes_got++;
		dev =  kzalloc(sizeof (struct pnp_dev), GFP_KERNEL);
		if (!dev)
			break;
		if(insert_device(dev,node)<0)
			kfree(dev);
		else
			devs++;
		if (nodenum <= thisnodenum) {
			printk(KERN_ERR "PnPBIOS: build_devlist: Node number 0x%x is out of sequence following node 0x%x. Aborting.\n", (unsigned int)nodenum, (unsigned int)thisnodenum);
			break;
		}
	}
	kfree(node);

	printk(KERN_INFO "PnPBIOS: %i node%s reported by PnP BIOS; %i recorded by driver\n",
		nodes_got, nodes_got != 1 ? "s" : "", devs);
}

/*
 *
 * INIT AND EXIT
 *
 */

static int pnpbios_disabled; /* = 0 */
int pnpbios_dont_use_current_config; /* = 0 */

#ifndef MODULE
static int __init pnpbios_setup(char *str)
{
	int invert;

	while ((str != NULL) && (*str != '\0')) {
		if (strncmp(str, "off", 3) == 0)
			pnpbios_disabled=1;
		if (strncmp(str, "on", 2) == 0)
			pnpbios_disabled=0;
		invert = (strncmp(str, "no-", 3) == 0);
		if (invert)
			str += 3;
		if (strncmp(str, "curr", 4) == 0)
			pnpbios_dont_use_current_config = invert;
		str = strchr(str, ',');
		if (str != NULL)
			str += strspn(str, ", \t");
	}

	return 1;
}

__setup("pnpbios=", pnpbios_setup);
#endif

/* PnP BIOS signature: "$PnP" */
#define PNP_SIGNATURE   (('$' << 0) + ('P' << 8) + ('n' << 16) + ('P' << 24))

static int __init pnpbios_probe_system(void)
{
	union pnp_bios_install_struct *check;
	u8 sum;
	int length, i;

	printk(KERN_INFO "PnPBIOS: Scanning system for PnP BIOS support...\n");

	/*
 	 * Search the defined area (0xf0000-0xffff0) for a valid PnP BIOS
	 * structure and, if one is found, sets up the selectors and
	 * entry points
	 */
	for (check = (union pnp_bios_install_struct *) __va(0xf0000);
	     check < (union pnp_bios_install_struct *) __va(0xffff0);
	     check = (void *)check + 16) {
		if (check->fields.signature != PNP_SIGNATURE)
			continue;
		printk(KERN_INFO "PnPBIOS: Found PnP BIOS installation structure at 0x%p\n", check);
		length = check->fields.length;
		if (!length) {
			printk(KERN_ERR "PnPBIOS: installation structure is invalid, skipping\n");
			continue;
		}
		for (sum = 0, i = 0; i < length; i++)
			sum += check->chars[i];
		if (sum) {
			printk(KERN_ERR "PnPBIOS: installation structure is corrupted, skipping\n");
			continue;
		}
		if (check->fields.version < 0x10) {
			printk(KERN_WARNING "PnPBIOS: PnP BIOS version %d.%d is not supported\n",
			       check->fields.version >> 4,
			       check->fields.version & 15);
			continue;
		}
		printk(KERN_INFO "PnPBIOS: PnP BIOS version %d.%d, entry 0x%x:0x%x, dseg 0x%x\n",
                       check->fields.version >> 4, check->fields.version & 15,
		       check->fields.pm16cseg, check->fields.pm16offset,
		       check->fields.pm16dseg);
		pnp_bios_install = check;
		return 1;
	}

	printk(KERN_INFO "PnPBIOS: PnP BIOS support was not detected.\n");
	return 0;
}

static int __init exploding_pnp_bios(struct dmi_system_id *d)
{
	printk(KERN_WARNING "%s detected. Disabling PnPBIOS\n", d->ident);
	return 0;
}

static struct dmi_system_id pnpbios_dmi_table[] __initdata = {
	{	/* PnPBIOS GPF on boot */
		.callback = exploding_pnp_bios,
		.ident = "Higraded P14H",
		.matches = {
			DMI_MATCH(DMI_BIOS_VENDOR, "American Megatrends Inc."),
			DMI_MATCH(DMI_BIOS_VERSION, "07.00T"),
			DMI_MATCH(DMI_SYS_VENDOR, "Higraded"),
			DMI_MATCH(DMI_PRODUCT_NAME, "P14H"),
		},
	},
	{	/* PnPBIOS GPF on boot */
		.callback = exploding_pnp_bios,
		.ident = "ASUS P4P800",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "ASUSTeK Computer Inc."),
			DMI_MATCH(DMI_BOARD_NAME, "P4P800"),
		},
	},
	{ }
};

static int __init pnpbios_init(void)
{
	int ret;

#if defined(CONFIG_PPC_MERGE)
	if (check_legacy_ioport(PNPBIOS_BASE))
		return -ENODEV;
#endif
	if (pnpbios_disabled || dmi_check_system(pnpbios_dmi_table) ||
	    paravirt_enabled()) {
		printk(KERN_INFO "PnPBIOS: Disabled\n");
		return -ENODEV;
	}

#ifdef CONFIG_PNPACPI
	if (!acpi_disabled && !pnpacpi_disabled) {
		pnpbios_disabled = 1;
		printk(KERN_INFO "PnPBIOS: Disabled by ACPI PNP\n");
		return -ENODEV;
	}
#endif /* CONFIG_ACPI */

	/* scan the system for pnpbios support */
	if (!pnpbios_probe_system())
		return -ENODEV;

	/* make preparations for bios calls */
	pnpbios_calls_init(pnp_bios_install);

	/* read the node info */
	ret = pnp_bios_dev_node_info(&node_info);
	if (ret) {
		printk(KERN_ERR "PnPBIOS: Unable to get node info.  Aborting.\n");
		return ret;
	}

	/* register with the pnp layer */
	ret = pnp_register_protocol(&pnpbios_protocol);
	if (ret) {
		printk(KERN_ERR "PnPBIOS: Unable to register driver.  Aborting.\n");
		return ret;
	}

	/* start the proc interface */
	ret = pnpbios_proc_init();
	if (ret)
		printk(KERN_ERR "PnPBIOS: Failed to create proc interface.\n");

	/* scan for pnpbios devices */
	build_devlist();

	return 0;
}

subsys_initcall(pnpbios_init);

static int __init pnpbios_thread_init(void)
{
	struct task_struct *task;
#if defined(CONFIG_PPC_MERGE)
	if (check_legacy_ioport(PNPBIOS_BASE))
		return 0;
#endif
	if (pnpbios_disabled)
		return 0;
#ifdef CONFIG_HOTPLUG
	init_completion(&unload_sem);
	task = kthread_run(pnp_dock_thread, NULL, "kpnpbiosd");
	if (!IS_ERR(task))
		unloading = 0;
#endif
	return 0;
}

#ifndef MODULE

/* init/main.c calls pnpbios_init early */

/* Start the kernel thread later: */
module_init(pnpbios_thread_init);

#else

/*
 * N.B.: Building pnpbios as a module hasn't been fully implemented
 */

MODULE_LICENSE("GPL");

static int __init pnpbios_init_all(void)
{
	int r;

	r = pnpbios_init();
	if (r)
		return r;
	r = pnpbios_thread_init();
	if (r)
		return r;
	return 0;
}

static void __exit pnpbios_exit(void)
{
#ifdef CONFIG_HOTPLUG
	unloading = 1;
	wait_for_completion(&unload_sem);
#endif
	pnpbios_proc_exit();
	/* We ought to free resources here */
	return;
}

module_init(pnpbios_init_all);
module_exit(pnpbios_exit);

#endif

EXPORT_SYMBOL(pnpbios_protocol);
