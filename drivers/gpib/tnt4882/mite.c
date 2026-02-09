// SPDX-License-Identifier: GPL-2.0-only

/*
 *	Hardware driver for NI Mite PCI interface chip,
 *	adapted from COMEDI
 *
 *	Copyright (C) 1997-8 David A. Schleef
 *	Copyright (C) 2002 Frank Mori Hess
 *
 *	The PCI-MIO E series driver was originally written by
 *	Tomasz Motylewski <...>, and ported to comedi by ds.
 *
 *	References for specifications:
 *
 *	   321747b.pdf  Register Level Programmer Manual (obsolete)
 *	   321747c.pdf  Register Level Programmer Manual (new)
 *	   DAQ-STC reference manual
 *
 *	Other possibly relevant info:
 *
 *	   320517c.pdf  User manual (obsolete)
 *	   320517f.pdf  User manual (new)
 *	   320889a.pdf  delete
 *	   320906c.pdf  maximum signal ratings
 *	   321066a.pdf  about 16x
 *	   321791a.pdf  discontinuation of at-mio-16e-10 rev. c
 *	   321808a.pdf  about at-mio-16e-10 rev P
 *	   321837a.pdf  discontinuation of at-mio-16de-10 rev d
 *	   321838a.pdf  about at-mio-16de-10 rev N
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/io.h>
#include <linux/slab.h>

#include "mite.h"

#define PCI_MITE_SIZE		4096
#define PCI_DAQ_SIZE		4096

struct mite_struct *mite_devices;

#define TOP_OF_PAGE(x) ((x) | (~(PAGE_MASK)))

void mite_init(void)
{
	struct pci_dev *pcidev;
	struct mite_struct *mite;

	for (pcidev = pci_get_device(PCI_VENDOR_ID_NATINST, PCI_ANY_ID, NULL);
		pcidev;
		pcidev = pci_get_device(PCI_VENDOR_ID_NATINST, PCI_ANY_ID, pcidev)) {
		mite = kzalloc(sizeof(*mite), GFP_KERNEL);
		if (!mite)
			return;

		mite->pcidev = pcidev;
		pci_dev_get(mite->pcidev);
		mite->next = mite_devices;
		mite_devices = mite;
	}
}

int mite_setup(struct mite_struct *mite)
{
	u32 addr;

	if (pci_enable_device(mite->pcidev)) {
		pr_err("mite: error enabling mite.\n");
		return -EIO;
	}
	pci_set_master(mite->pcidev);
	if (pci_request_regions(mite->pcidev, "mite")) {
		pr_err("mite: failed to request mite io regions.\n");
		return -EIO;
	}
	addr = pci_resource_start(mite->pcidev, 0);
	mite->mite_phys_addr = addr;
	mite->mite_io_addr = ioremap(addr, pci_resource_len(mite->pcidev, 0));
	if (!mite->mite_io_addr) {
		pr_err("mite: failed to remap mite io memory address.\n");
		return -ENOMEM;
	}
	addr = pci_resource_start(mite->pcidev, 1);
	mite->daq_phys_addr = addr;
	mite->daq_io_addr = ioremap(mite->daq_phys_addr, pci_resource_len(mite->pcidev, 1));
	if (!mite->daq_io_addr)	{
		pr_err("mite: failed to remap daq io memory address.\n");
		return -ENOMEM;
	}
	writel(mite->daq_phys_addr | WENAB, mite->mite_io_addr + MITE_IODWBSR);
	mite->used = 1;
	return 0;
}

void mite_cleanup(void)
{
	struct mite_struct *mite, *next;

	for (mite = mite_devices; mite; mite = next) {
		next = mite->next;
		if (mite->pcidev)
			pci_dev_put(mite->pcidev);
		kfree(mite);
	}
}

void mite_unsetup(struct mite_struct *mite)
{
	if (!mite)
		return;
	if (mite->mite_io_addr)	{
		iounmap(mite->mite_io_addr);
		mite->mite_io_addr = NULL;
	}
	if (mite->daq_io_addr) {
		iounmap(mite->daq_io_addr);
		mite->daq_io_addr = NULL;
	}
	if (mite->mite_phys_addr) {
		pci_release_regions(mite->pcidev);
		pci_disable_device(mite->pcidev);
		mite->mite_phys_addr = 0;
	}
	mite->used = 0;
}
