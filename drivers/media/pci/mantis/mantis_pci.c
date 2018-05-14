/*
	Mantis PCI bridge driver

	Copyright (C) Manu Abraham (abraham.manu@gmail.com)

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

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <asm/io.h>
#include <asm/page.h>
#include <linux/kmod.h>
#include <linux/vmalloc.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/pci.h>

#include <asm/irq.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/interrupt.h>

#include <media/dmxdev.h>
#include <media/dvbdev.h>
#include <media/dvb_demux.h>
#include <media/dvb_frontend.h>
#include <media/dvb_net.h>

#include "mantis_common.h"
#include "mantis_reg.h"
#include "mantis_pci.h"

#define DRIVER_NAME		"Mantis Core"

int mantis_pci_init(struct mantis_pci *mantis)
{
	u8 latency;
	struct mantis_hwconfig *config	= mantis->hwconfig;
	struct pci_dev *pdev		= mantis->pdev;
	int err, ret = 0;

	dprintk(MANTIS_ERROR, 0, "found a %s PCI %s device on (%02x:%02x.%x),\n",
		config->model_name,
		config->dev_type,
		mantis->pdev->bus->number,
		PCI_SLOT(mantis->pdev->devfn),
		PCI_FUNC(mantis->pdev->devfn));

	err = pci_enable_device(pdev);
	if (err != 0) {
		ret = -ENODEV;
		dprintk(MANTIS_ERROR, 1, "ERROR: PCI enable failed <%i>", err);
		goto fail0;
	}

	err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32));
	if (err != 0) {
		dprintk(MANTIS_ERROR, 1, "ERROR: Unable to obtain 32 bit DMA <%i>", err);
		ret = -ENOMEM;
		goto fail1;
	}

	pci_set_master(pdev);

	if (!request_mem_region(pci_resource_start(pdev, 0),
				pci_resource_len(pdev, 0),
				DRIVER_NAME)) {

		dprintk(MANTIS_ERROR, 1, "ERROR: BAR0 Request failed !");
		ret = -ENODEV;
		goto fail1;
	}

	mantis->mmio = ioremap(pci_resource_start(pdev, 0),
			       pci_resource_len(pdev, 0));

	if (!mantis->mmio) {
		dprintk(MANTIS_ERROR, 1, "ERROR: BAR0 remap failed !");
		ret = -ENODEV;
		goto fail2;
	}

	pci_read_config_byte(pdev, PCI_LATENCY_TIMER, &latency);
	mantis->latency = latency;
	mantis->revision = pdev->revision;

	dprintk(MANTIS_ERROR, 0, "    Mantis Rev %d [%04x:%04x], ",
		mantis->revision,
		mantis->pdev->subsystem_vendor,
		mantis->pdev->subsystem_device);

	dprintk(MANTIS_ERROR, 0,
		"irq: %d, latency: %d\n    memory: 0x%lx, mmio: 0x%p\n",
		mantis->pdev->irq,
		mantis->latency,
		mantis->mantis_addr,
		mantis->mmio);

	err = request_irq(pdev->irq,
			  config->irq_handler,
			  IRQF_SHARED,
			  DRIVER_NAME,
			  mantis);

	if (err != 0) {

		dprintk(MANTIS_ERROR, 1, "ERROR: IRQ registration failed ! <%d>", err);
		ret = -ENODEV;
		goto fail3;
	}

	pci_set_drvdata(pdev, mantis);
	return ret;

	/* Error conditions */
fail3:
	dprintk(MANTIS_ERROR, 1, "ERROR: <%d> I/O unmap", ret);
	if (mantis->mmio)
		iounmap(mantis->mmio);

fail2:
	dprintk(MANTIS_ERROR, 1, "ERROR: <%d> releasing regions", ret);
	release_mem_region(pci_resource_start(pdev, 0),
			   pci_resource_len(pdev, 0));

fail1:
	dprintk(MANTIS_ERROR, 1, "ERROR: <%d> disabling device", ret);
	pci_disable_device(pdev);

fail0:
	dprintk(MANTIS_ERROR, 1, "ERROR: <%d> exiting", ret);
	return ret;
}
EXPORT_SYMBOL_GPL(mantis_pci_init);

void mantis_pci_exit(struct mantis_pci *mantis)
{
	struct pci_dev *pdev = mantis->pdev;

	dprintk(MANTIS_NOTICE, 1, " mem: 0x%p", mantis->mmio);
	free_irq(pdev->irq, mantis);
	if (mantis->mmio) {
		iounmap(mantis->mmio);
		release_mem_region(pci_resource_start(pdev, 0),
				   pci_resource_len(pdev, 0));
	}

	pci_disable_device(pdev);
}
EXPORT_SYMBOL_GPL(mantis_pci_exit);

MODULE_DESCRIPTION("Mantis PCI DTV bridge driver");
MODULE_AUTHOR("Manu Abraham");
MODULE_LICENSE("GPL");
