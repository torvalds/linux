/*
	Mantis PCI bridge driver

	Copyright (C) 2005, 2006 Manu Abraham (abraham.manu@gmail.com)

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <linux/kmod.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/device.h>
#include "mantis_common.h"
#include "mantis_core.h"

#include <asm/irq.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/interrupt.h>

unsigned int verbose = 1;
module_param(verbose, int, 0644);
MODULE_PARM_DESC(verbose, "verbose startup messages, default is 1 (yes)");

unsigned int devs;

#define PCI_VENDOR_ID_MANTIS			0x1822
#define PCI_DEVICE_ID_MANTIS_R11		0x4e35
#define DRIVER_NAME				"Mantis"

static struct pci_device_id mantis_pci_table[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_MANTIS, PCI_DEVICE_ID_MANTIS_R11) },
	{ 0 },
};

MODULE_DEVICE_TABLE(pci, mantis_pci_table);

static irqreturn_t mantis_pci_irq(int irq, void *dev_id)
{
	int i = 0, interrupts = 0;
	u32 stat = 0, mask = 0, lstat = 0, mstat = 0;
	struct mantis_pci *mantis;

	mantis = (struct mantis_pci *) dev_id;
	if (unlikely(mantis == NULL)) {
		dprintk(verbose, MANTIS_ERROR, 1, "Mantis == NULL");
		return IRQ_NONE;
	}
	stat = mmread(MANTIS_INT_STAT);
	mask = mmread(MANTIS_INT_MASK);
	mstat = lstat = stat & ~MANTIS_INT_RISCSTAT;

	if (!(stat & mask)) {
		dprintk(verbose, MANTIS_DEBUG, 1, "Not ours !");
		return IRQ_NONE;
	}
	mmwrite(lstat, MANTIS_INT_STAT);
	interrupts = hweight32(stat);
	dprintk(verbose, MANTIS_DEBUG, 0, "=== Interrupts[%04x/%04x]=%d [", stat, mask, interrupts);

	while (lstat) {
		if (lstat & MANTIS_INT_RISCEN) {
			dprintk(verbose, MANTIS_DEBUG, 0, "* DMA enabl *");
			lstat &= ~MANTIS_INT_RISCEN;

		} else if (lstat & MANTIS_INT_I2CRACK) {
			dprintk(verbose, MANTIS_DEBUG, 0, "* I2C R-ACK *");
			mantis->mantis_int_stat = stat;
			mantis->mantis_int_mask = mask;
			wake_up(&mantis->i2c_wq);
			lstat &= ~MANTIS_INT_I2CRACK;

		} else if (lstat & MANTIS_INT_PCMCIA7) {
			dprintk(verbose, MANTIS_DEBUG, 0, "* PCMCIA-07 *");
			lstat &= ~MANTIS_INT_PCMCIA7;

		} else if (lstat & MANTIS_INT_PCMCIA6) {
			dprintk(verbose, MANTIS_DEBUG, 0, "* PCMCIA-06 *");
			lstat &= ~MANTIS_INT_PCMCIA6;

		} else if (lstat & MANTIS_INT_PCMCIA5) {
			dprintk(verbose, MANTIS_DEBUG, 0, "* PCMCIA-05 *");
			lstat &= ~MANTIS_INT_PCMCIA5;

		} else if (lstat & MANTIS_INT_PCMCIA4) {
			dprintk(verbose, MANTIS_DEBUG, 0, "* PCMCIA-04 *");
			lstat &= ~MANTIS_INT_PCMCIA4;

		} else if (lstat & MANTIS_INT_PCMCIA3) {
			dprintk(verbose, MANTIS_DEBUG, 0, "* PCMCIA-03 *");
			lstat &= ~MANTIS_INT_PCMCIA3;

		} else if (lstat & MANTIS_INT_PCMCIA2) {
			dprintk(verbose, MANTIS_DEBUG, 0, "* PCMCIA-02 *");
			lstat &= ~MANTIS_INT_PCMCIA2;

		} else if (lstat & MANTIS_INT_PCMCIA1) {
			dprintk(verbose, MANTIS_DEBUG, 0, "* PCMCIA-01 *");
			lstat &= ~MANTIS_INT_PCMCIA1;

		} else if (lstat & MANTIS_INT_PCMCIA0) {
			dprintk(verbose, MANTIS_DEBUG, 0, "* PCMCIA-00 *");
			lstat &= ~MANTIS_INT_PCMCIA0;

		} else if (lstat & MANTIS_INT_IRQ0) {
			dprintk(verbose, MANTIS_DEBUG, 0, "* INT IRQ-0 *");
			lstat &= ~MANTIS_INT_IRQ0;

		} else if (lstat & MANTIS_INT_IRQ1) {
			dprintk(verbose, MANTIS_DEBUG, 0, "* INT IRQ-1 *");
			lstat &= ~MANTIS_INT_IRQ1;

		} else if (lstat & MANTIS_INT_OCERR) {
			dprintk(verbose, MANTIS_DEBUG, 0, "* INT OCERR *");
			lstat &= ~MANTIS_INT_OCERR;

		} else if (lstat & MANTIS_INT_PABORT) {
			dprintk(verbose, MANTIS_DEBUG, 0, "* INT PABRT *");
			lstat &= ~MANTIS_INT_PABORT;

		} else if (lstat & MANTIS_INT_RIPERR) {
			dprintk(verbose, MANTIS_DEBUG, 0, "* INT RIPRR *");
			lstat &= ~MANTIS_INT_RIPERR;

		} else if (lstat & MANTIS_INT_PPERR) {
			dprintk(verbose, MANTIS_DEBUG, 0, "* INT PPERR *");
			lstat &= ~MANTIS_INT_PPERR;

		} else if (lstat & MANTIS_INT_FTRGT) {
			dprintk(verbose, MANTIS_DEBUG, 0, "* INT FTRGT *");
			lstat &= ~MANTIS_INT_FTRGT;

		} else if (lstat & MANTIS_INT_RISCI) {
			dprintk(verbose, MANTIS_DEBUG, 0, "* INT RISCI *");
			mantis->finished_block = (stat & MANTIS_INT_RISCSTAT) >> 28;
			tasklet_schedule(&mantis->tasklet);
			lstat &= ~MANTIS_INT_RISCI;

		} else if (lstat & MANTIS_INT_I2CDONE) {
			dprintk(verbose, MANTIS_DEBUG, 0, "* I2C DONE  *");
			mantis->mantis_int_stat = stat;
			mantis->mantis_int_mask = mask;
			lstat &= ~MANTIS_INT_I2CDONE;
		} else {
			dprintk(verbose, MANTIS_DEBUG, 0,
				"* Unknown [%04x/%04x] *", stat, mask);
			break;
		}
		i++;
		if (i > interrupts) {
			dprintk(verbose, MANTIS_ERROR, 1, "going Loopy ! -- BREAK --");
			break;
		}
	}
	dprintk(verbose, MANTIS_DEBUG, 0, "] ===\n");

	return IRQ_HANDLED;
}


static int __devinit mantis_pci_probe(struct pci_dev *pdev,
				      const struct pci_device_id *mantis_pci_table)
{
	u8 revision, latency;
	struct mantis_pci *mantis;
	int ret = 0;

	devs++;

	mantis = kmalloc(sizeof (struct mantis_pci), GFP_KERNEL);
	if (mantis == NULL) {
		printk("%s: Out of memory\n", __func__);
		ret = -ENOMEM;
		goto err;
	}
	memset(mantis, 0, sizeof (struct mantis_pci));
	mantis->num = devs;
	if (pci_enable_device(pdev)) {
		dprintk(verbose, MANTIS_ERROR, 1, "Mantis PCI enable failed");
		ret = -ENODEV;
		goto err;
	}
	mantis->mantis_addr = pci_resource_start(pdev, 0);
	if (!request_mem_region(pci_resource_start(pdev, 0),
			pci_resource_len(pdev, 0), DRIVER_NAME)) {
		ret = -ENODEV;
		goto err0;
	}

	if ((mantis->mantis_mmio = ioremap(mantis->mantis_addr, 0x1000)) == NULL) {
		dprintk(verbose, MANTIS_ERROR, 1, "IO remap failed");
		ret = -ENODEV;
		goto err1;
	}

	// Clear and disable all interrupts at startup
	// to avoid lockup situations
	mmwrite(0x00, MANTIS_INT_MASK);
	if (request_irq(pdev->irq, mantis_pci_irq, IRQF_SHARED | IRQF_DISABLED,
						DRIVER_NAME, mantis) < 0) {

		dprintk(verbose, MANTIS_ERROR, 1, "Mantis IRQ reg failed");
		ret = -ENODEV;
		goto err2;
	}
	pci_set_master(pdev);
	pci_set_drvdata(pdev, mantis);
	pci_read_config_byte(pdev, PCI_LATENCY_TIMER, &latency);
	pci_read_config_byte(pdev, PCI_CLASS_REVISION, &revision);
	mantis->latency = latency;
	mantis->revision = revision;
	mantis->pdev = pdev;
	init_waitqueue_head(&mantis->i2c_wq);

	// CAM bypass
	//mmwrite(mmread(MANTIS_INT_MASK) | MANTIS_INT_IRQ1, MANTIS_INT_MASK);
	dprintk(verbose, MANTIS_INFO, 1, "gpif status: %04x  irqcfg: %04x", mmread(0x9c), mmread(0x98));
	if ((mmread(0x9c) & 0x200) != 0) { //CAM inserted
		msleep_interruptible(1);
		if ((mmread(0x9c) & 0x200) != 0)
			mmwrite(((mmread(0x98) | 0x01) & ~0x02), 0x98);
		else
			mmwrite(((mmread(0x98) | 0x02) & ~0x01), 0x98);

	} else {
		mmwrite(((mmread(0x98) | 0x02) & ~0x01), 0x98);
	}
	mantis_set_direction(mantis, 0);

	// default latency if none specified
	if (!latency)
		pci_write_config_byte(pdev, PCI_LATENCY_TIMER, 32);
	dprintk(verbose, MANTIS_ERROR, 0, "Mantis Rev %d, ",
		mantis->revision);

	dprintk(verbose, MANTIS_ERROR, 0,
		"irq: %d, latency: %d\n memory: 0x%lx, mmio: 0x%p\n",
		pdev->irq, mantis->latency,
		mantis->mantis_addr, mantis->mantis_mmio);

	// No more PCI specific stuff !
	if (mantis_core_init(mantis) < 0) {
		dprintk(verbose, MANTIS_ERROR, 1, "Mantis core init failed");
		ret = -ENODEV;
		goto err2;
	}

	return 0;

	// Error conditions ..
err2:
	dprintk(verbose, MANTIS_DEBUG, 1, "Err: IO Unmap");
	if (mantis->mantis_mmio)
		iounmap(mantis->mantis_mmio);
err1:
	dprintk(verbose, MANTIS_DEBUG, 1, "Err: Release regions");
	release_mem_region(pci_resource_start(pdev, 0),
				pci_resource_len(pdev, 0));
	pci_disable_device(pdev);
err0:
	dprintk(verbose, MANTIS_DEBUG, 1, "Err: Free");
	kfree(mantis);
err:
	dprintk(verbose, MANTIS_DEBUG, 1, "Err:");
	return ret;
}

static void __devexit mantis_pci_remove(struct pci_dev *pdev)
{
	struct mantis_pci *mantis = pci_get_drvdata(pdev);

	if (mantis == NULL) {
		dprintk(verbose, MANTIS_ERROR, 1, "Aeio, Mantis NULL ptr");
		return;
	}
	mantis_core_exit(mantis);
	dprintk(verbose, MANTIS_ERROR, 1, "Removing -->Mantis irq: %d, latency: %d\n memory: 0x%lx, mmio: 0x%p",
		pdev->irq, mantis->latency, mantis->mantis_addr,
		mantis->mantis_mmio);

	free_irq(pdev->irq, mantis);
	pci_release_regions(pdev);
	if (mantis_dma_exit(mantis) < 0)
		dprintk(verbose, MANTIS_ERROR, 1, "DMA exit failed");

	pci_set_drvdata(pdev, NULL);
	pci_disable_device(pdev);
	kfree(mantis);
}

static struct pci_driver mantis_pci_driver = {
	.name = DRIVER_NAME,
	.id_table = mantis_pci_table,
	.probe = mantis_pci_probe,
	.remove = mantis_pci_remove,
};

static int __devinit mantis_pci_init(void)
{
	return pci_register_driver(&mantis_pci_driver);
}

static void __devexit mantis_pci_exit(void)
{
	pci_unregister_driver(&mantis_pci_driver);
}

module_init(mantis_pci_init);
module_exit(mantis_pci_exit);

MODULE_DESCRIPTION("Mantis PCI DTV bridge driver");
MODULE_AUTHOR("Manu Abraham");
MODULE_LICENSE("GPL");
