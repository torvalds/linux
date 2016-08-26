/******************************************************************************
 * platform-pci.c
 *
 * Xen platform PCI device driver
 *
 * Authors: ssmith@xensource.com and stefano.stabellini@eu.citrix.com
 *
 * Copyright (c) 2005, Intel Corporation.
 * Copyright (c) 2007, XenSource Inc.
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


#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/init.h>
#include <linux/pci.h>

#include <xen/platform_pci.h>
#include <xen/grant_table.h>
#include <xen/xenbus.h>
#include <xen/events.h>
#include <xen/hvm.h>
#include <xen/xen-ops.h>

#define DRV_NAME    "xen-platform-pci"

static unsigned long platform_mmio;
static unsigned long platform_mmio_alloc;
static unsigned long platform_mmiolen;

static unsigned long alloc_xen_mmio(unsigned long len)
{
	unsigned long addr;

	addr = platform_mmio + platform_mmio_alloc;
	platform_mmio_alloc += len;
	BUG_ON(platform_mmio_alloc > platform_mmiolen);

	return addr;
}

static int platform_pci_probe(struct pci_dev *pdev,
			      const struct pci_device_id *ent)
{
	int i, ret;
	long ioaddr;
	long mmio_addr, mmio_len;
	unsigned int max_nr_gframes;
	unsigned long grant_frames;

	if (!xen_domain())
		return -ENODEV;

	i = pci_enable_device(pdev);
	if (i)
		return i;

	ioaddr = pci_resource_start(pdev, 0);

	mmio_addr = pci_resource_start(pdev, 1);
	mmio_len = pci_resource_len(pdev, 1);

	if (mmio_addr == 0 || ioaddr == 0) {
		dev_err(&pdev->dev, "no resources found\n");
		ret = -ENOENT;
		goto pci_out;
	}

	ret = pci_request_region(pdev, 1, DRV_NAME);
	if (ret < 0)
		goto pci_out;

	ret = pci_request_region(pdev, 0, DRV_NAME);
	if (ret < 0)
		goto mem_out;

	platform_mmio = mmio_addr;
	platform_mmiolen = mmio_len;

	max_nr_gframes = gnttab_max_grant_frames();
	grant_frames = alloc_xen_mmio(PAGE_SIZE * max_nr_gframes);
	ret = gnttab_setup_auto_xlat_frames(grant_frames);
	if (ret)
		goto out;
	ret = gnttab_init();
	if (ret)
		goto grant_out;
	xenbus_probe(NULL);
	return 0;
grant_out:
	gnttab_free_auto_xlat_frames();
out:
	pci_release_region(pdev, 0);
mem_out:
	pci_release_region(pdev, 1);
pci_out:
	pci_disable_device(pdev);
	return ret;
}

static struct pci_device_id platform_pci_tbl[] = {
	{PCI_VENDOR_ID_XEN, PCI_DEVICE_ID_XEN_PLATFORM,
		PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0,}
};

static struct pci_driver platform_driver = {
	.name =           DRV_NAME,
	.probe =          platform_pci_probe,
	.id_table =       platform_pci_tbl,
};

static int __init platform_pci_init(void)
{
	return pci_register_driver(&platform_driver);
}
device_initcall(platform_pci_init);
