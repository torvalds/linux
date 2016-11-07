/******************************************************************************
 * platform-pci-unplug.c
 *
 * Xen platform PCI device driver
 * Copyright (c) 2010, Citrix
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 */

#include <linux/init.h>
#include <linux/io.h>
#include <linux/export.h>

#include <xen/platform_pci.h>
#include "xen-ops.h"

#define XEN_PLATFORM_ERR_MAGIC -1
#define XEN_PLATFORM_ERR_PROTOCOL -2
#define XEN_PLATFORM_ERR_BLACKLIST -3

#ifdef CONFIG_XEN_PVHVM
/* store the value of xen_emul_unplug after the unplug is done */
static int xen_platform_pci_unplug;
static int xen_emul_unplug;

static int check_platform_magic(void)
{
	short magic;
	char protocol;

	magic = inw(XEN_IOPORT_MAGIC);
	if (magic != XEN_IOPORT_MAGIC_VAL) {
		printk(KERN_ERR "Xen Platform PCI: unrecognised magic value\n");
		return XEN_PLATFORM_ERR_MAGIC;
	}

	protocol = inb(XEN_IOPORT_PROTOVER);

	printk(KERN_DEBUG "Xen Platform PCI: I/O protocol version %d\n",
			protocol);

	switch (protocol) {
	case 1:
		outw(XEN_IOPORT_LINUX_PRODNUM, XEN_IOPORT_PRODNUM);
		outl(XEN_IOPORT_LINUX_DRVVER, XEN_IOPORT_DRVVER);
		if (inw(XEN_IOPORT_MAGIC) != XEN_IOPORT_MAGIC_VAL) {
			printk(KERN_ERR "Xen Platform: blacklisted by host\n");
			return XEN_PLATFORM_ERR_BLACKLIST;
		}
		break;
	default:
		printk(KERN_WARNING "Xen Platform PCI: unknown I/O protocol version\n");
		return XEN_PLATFORM_ERR_PROTOCOL;
	}

	return 0;
}

bool xen_has_pv_devices(void)
{
	if (!xen_domain())
		return false;

	/* PV domains always have them. */
	if (xen_pv_domain())
		return true;

	/* And user has xen_platform_pci=0 set in guest config as
	 * driver did not modify the value. */
	if (xen_platform_pci_unplug == 0)
		return false;

	if (xen_platform_pci_unplug & XEN_UNPLUG_NEVER)
		return false;

	if (xen_platform_pci_unplug & XEN_UNPLUG_ALL)
		return true;

	/* This is an odd one - we are going to run legacy
	 * and PV drivers at the same time. */
	if (xen_platform_pci_unplug & XEN_UNPLUG_UNNECESSARY)
		return true;

	/* And the caller has to follow with xen_pv_{disk,nic}_devices
	 * to be certain which driver can load. */
	return false;
}
EXPORT_SYMBOL_GPL(xen_has_pv_devices);

static bool __xen_has_pv_device(int state)
{
	/* HVM domains might or might not */
	if (xen_hvm_domain() && (xen_platform_pci_unplug & state))
		return true;

	return xen_has_pv_devices();
}

bool xen_has_pv_nic_devices(void)
{
	return __xen_has_pv_device(XEN_UNPLUG_ALL_NICS | XEN_UNPLUG_ALL);
}
EXPORT_SYMBOL_GPL(xen_has_pv_nic_devices);

bool xen_has_pv_disk_devices(void)
{
	return __xen_has_pv_device(XEN_UNPLUG_ALL_IDE_DISKS |
				   XEN_UNPLUG_AUX_IDE_DISKS | XEN_UNPLUG_ALL);
}
EXPORT_SYMBOL_GPL(xen_has_pv_disk_devices);

/*
 * This one is odd - it determines whether you want to run PV _and_
 * legacy (IDE) drivers together. This combination is only possible
 * under HVM.
 */
bool xen_has_pv_and_legacy_disk_devices(void)
{
	if (!xen_domain())
		return false;

	/* N.B. This is only ever used in HVM mode */
	if (xen_pv_domain())
		return false;

	if (xen_platform_pci_unplug & XEN_UNPLUG_UNNECESSARY)
		return true;

	return false;
}
EXPORT_SYMBOL_GPL(xen_has_pv_and_legacy_disk_devices);

void xen_unplug_emulated_devices(void)
{
	int r;

	/* user explicitly requested no unplug */
	if (xen_emul_unplug & XEN_UNPLUG_NEVER)
		return;
	/* check the version of the xen platform PCI device */
	r = check_platform_magic();
	/* If the version matches enable the Xen platform PCI driver.
	 * Also enable the Xen platform PCI driver if the host does
	 * not support the unplug protocol (XEN_PLATFORM_ERR_MAGIC)
	 * but the user told us that unplugging is unnecessary. */
	if (r && !(r == XEN_PLATFORM_ERR_MAGIC &&
			(xen_emul_unplug & XEN_UNPLUG_UNNECESSARY)))
		return;
	/* Set the default value of xen_emul_unplug depending on whether or
	 * not the Xen PV frontends and the Xen platform PCI driver have
	 * been compiled for this kernel (modules or built-in are both OK). */
	if (!xen_emul_unplug) {
		if (xen_must_unplug_nics()) {
			printk(KERN_INFO "Netfront and the Xen platform PCI driver have "
					"been compiled for this kernel: unplug emulated NICs.\n");
			xen_emul_unplug |= XEN_UNPLUG_ALL_NICS;
		}
		if (xen_must_unplug_disks()) {
			printk(KERN_INFO "Blkfront and the Xen platform PCI driver have "
					"been compiled for this kernel: unplug emulated disks.\n"
					"You might have to change the root device\n"
					"from /dev/hd[a-d] to /dev/xvd[a-d]\n"
					"in your root= kernel command line option\n");
			xen_emul_unplug |= XEN_UNPLUG_ALL_IDE_DISKS;
		}
	}
	/* Now unplug the emulated devices */
	if (!(xen_emul_unplug & XEN_UNPLUG_UNNECESSARY))
		outw(xen_emul_unplug, XEN_IOPORT_UNPLUG);
	xen_platform_pci_unplug = xen_emul_unplug;
}

static int __init parse_xen_emul_unplug(char *arg)
{
	char *p, *q;
	int l;

	for (p = arg; p; p = q) {
		q = strchr(p, ',');
		if (q) {
			l = q - p;
			q++;
		} else {
			l = strlen(p);
		}
		if (!strncmp(p, "all", l))
			xen_emul_unplug |= XEN_UNPLUG_ALL;
		else if (!strncmp(p, "ide-disks", l))
			xen_emul_unplug |= XEN_UNPLUG_ALL_IDE_DISKS;
		else if (!strncmp(p, "aux-ide-disks", l))
			xen_emul_unplug |= XEN_UNPLUG_AUX_IDE_DISKS;
		else if (!strncmp(p, "nics", l))
			xen_emul_unplug |= XEN_UNPLUG_ALL_NICS;
		else if (!strncmp(p, "unnecessary", l))
			xen_emul_unplug |= XEN_UNPLUG_UNNECESSARY;
		else if (!strncmp(p, "never", l))
			xen_emul_unplug |= XEN_UNPLUG_NEVER;
		else
			printk(KERN_WARNING "unrecognised option '%s' "
				 "in parameter 'xen_emul_unplug'\n", p);
	}
	return 0;
}
early_param("xen_emul_unplug", parse_xen_emul_unplug);
#endif
