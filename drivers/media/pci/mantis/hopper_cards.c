/*
	Hopper PCI bridge driver

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
#include <linux/pci.h>
#include <linux/slab.h>
#include <asm/irq.h>
#include <linux/interrupt.h>

#include "dmxdev.h"
#include "dvbdev.h"
#include "dvb_demux.h"
#include "dvb_frontend.h"
#include "dvb_net.h"

#include "mantis_common.h"
#include "hopper_vp3028.h"
#include "mantis_dma.h"
#include "mantis_dvb.h"
#include "mantis_uart.h"
#include "mantis_ioc.h"
#include "mantis_pci.h"
#include "mantis_i2c.h"
#include "mantis_reg.h"

static unsigned int verbose;
module_param(verbose, int, 0644);
MODULE_PARM_DESC(verbose, "verbose startup messages, default is 0 (no)");

#define DRIVER_NAME	"Hopper"

static char *label[10] = {
	"DMA",
	"IRQ-0",
	"IRQ-1",
	"OCERR",
	"PABRT",
	"RIPRR",
	"PPERR",
	"FTRGT",
	"RISCI",
	"RACK"
};

static int devs;

static irqreturn_t hopper_irq_handler(int irq, void *dev_id)
{
	u32 stat = 0, mask = 0;
	u32 rst_stat = 0, rst_mask = 0;

	struct mantis_pci *mantis;
	struct mantis_ca *ca;

	mantis = (struct mantis_pci *) dev_id;
	if (unlikely(!mantis)) {
		dprintk(MANTIS_ERROR, 1, "Mantis == NULL");
		return IRQ_NONE;
	}
	ca = mantis->mantis_ca;

	stat = mmread(MANTIS_INT_STAT);
	mask = mmread(MANTIS_INT_MASK);
	if (!(stat & mask))
		return IRQ_NONE;

	rst_mask  = MANTIS_GPIF_WRACK  |
		    MANTIS_GPIF_OTHERR |
		    MANTIS_SBUF_WSTO   |
		    MANTIS_GPIF_EXTIRQ;

	rst_stat  = mmread(MANTIS_GPIF_STATUS);
	rst_stat &= rst_mask;
	mmwrite(rst_stat, MANTIS_GPIF_STATUS);

	mantis->mantis_int_stat = stat;
	mantis->mantis_int_mask = mask;
	dprintk(MANTIS_DEBUG, 0, "\n-- Stat=<%02x> Mask=<%02x> --", stat, mask);
	if (stat & MANTIS_INT_RISCEN) {
		dprintk(MANTIS_DEBUG, 0, "<%s>", label[0]);
	}
	if (stat & MANTIS_INT_IRQ0) {
		dprintk(MANTIS_DEBUG, 0, "<%s>", label[1]);
		mantis->gpif_status = rst_stat;
		wake_up(&ca->hif_write_wq);
		schedule_work(&ca->hif_evm_work);
	}
	if (stat & MANTIS_INT_IRQ1) {
		dprintk(MANTIS_DEBUG, 0, "<%s>", label[2]);
		spin_lock(&mantis->intmask_lock);
		mmwrite(mmread(MANTIS_INT_MASK) & ~MANTIS_INT_IRQ1,
			MANTIS_INT_MASK);
		spin_unlock(&mantis->intmask_lock);
		schedule_work(&mantis->uart_work);
	}
	if (stat & MANTIS_INT_OCERR) {
		dprintk(MANTIS_DEBUG, 0, "<%s>", label[3]);
	}
	if (stat & MANTIS_INT_PABORT) {
		dprintk(MANTIS_DEBUG, 0, "<%s>", label[4]);
	}
	if (stat & MANTIS_INT_RIPERR) {
		dprintk(MANTIS_DEBUG, 0, "<%s>", label[5]);
	}
	if (stat & MANTIS_INT_PPERR) {
		dprintk(MANTIS_DEBUG, 0, "<%s>", label[6]);
	}
	if (stat & MANTIS_INT_FTRGT) {
		dprintk(MANTIS_DEBUG, 0, "<%s>", label[7]);
	}
	if (stat & MANTIS_INT_RISCI) {
		dprintk(MANTIS_DEBUG, 0, "<%s>", label[8]);
		mantis->busy_block = (stat & MANTIS_INT_RISCSTAT) >> 28;
		tasklet_schedule(&mantis->tasklet);
	}
	if (stat & MANTIS_INT_I2CDONE) {
		dprintk(MANTIS_DEBUG, 0, "<%s>", label[9]);
		wake_up(&mantis->i2c_wq);
	}
	mmwrite(stat, MANTIS_INT_STAT);
	stat &= ~(MANTIS_INT_RISCEN   | MANTIS_INT_I2CDONE |
		  MANTIS_INT_I2CRACK  | MANTIS_INT_PCMCIA7 |
		  MANTIS_INT_PCMCIA6  | MANTIS_INT_PCMCIA5 |
		  MANTIS_INT_PCMCIA4  | MANTIS_INT_PCMCIA3 |
		  MANTIS_INT_PCMCIA2  | MANTIS_INT_PCMCIA1 |
		  MANTIS_INT_PCMCIA0  | MANTIS_INT_IRQ1	   |
		  MANTIS_INT_IRQ0     | MANTIS_INT_OCERR   |
		  MANTIS_INT_PABORT   | MANTIS_INT_RIPERR  |
		  MANTIS_INT_PPERR    | MANTIS_INT_FTRGT   |
		  MANTIS_INT_RISCI);

	if (stat)
		dprintk(MANTIS_DEBUG, 0, "<Unknown> Stat=<%02x> Mask=<%02x>", stat, mask);

	dprintk(MANTIS_DEBUG, 0, "\n");
	return IRQ_HANDLED;
}

static int hopper_pci_probe(struct pci_dev *pdev,
			    const struct pci_device_id *pci_id)
{
	struct mantis_pci_drvdata *drvdata;
	struct mantis_pci *mantis;
	struct mantis_hwconfig *config;
	int err;

	mantis = kzalloc(sizeof(*mantis), GFP_KERNEL);
	if (!mantis) {
		err = -ENOMEM;
		goto fail0;
	}

	drvdata			= (void *)pci_id->driver_data;
	mantis->num		= devs;
	mantis->verbose		= verbose;
	mantis->pdev		= pdev;
	config			= drvdata->hwconfig;
	config->irq_handler	= &hopper_irq_handler;
	mantis->hwconfig	= config;
	mantis->rc_map_name	= drvdata->rc_map_name;

	spin_lock_init(&mantis->intmask_lock);

	err = mantis_pci_init(mantis);
	if (err) {
		dprintk(MANTIS_ERROR, 1, "ERROR: Mantis PCI initialization failed <%d>", err);
		goto fail1;
	}

	err = mantis_stream_control(mantis, STREAM_TO_HIF);
	if (err < 0) {
		dprintk(MANTIS_ERROR, 1, "ERROR: Mantis stream control failed <%d>", err);
		goto fail1;
	}

	err = mantis_i2c_init(mantis);
	if (err < 0) {
		dprintk(MANTIS_ERROR, 1, "ERROR: Mantis I2C initialization failed <%d>", err);
		goto fail2;
	}

	err = mantis_get_mac(mantis);
	if (err < 0) {
		dprintk(MANTIS_ERROR, 1, "ERROR: Mantis MAC address read failed <%d>", err);
		goto fail2;
	}

	err = mantis_dma_init(mantis);
	if (err < 0) {
		dprintk(MANTIS_ERROR, 1, "ERROR: Mantis DMA initialization failed <%d>", err);
		goto fail3;
	}

	err = mantis_dvb_init(mantis);
	if (err < 0) {
		dprintk(MANTIS_ERROR, 1, "ERROR: Mantis DVB initialization failed <%d>", err);
		goto fail4;
	}
	devs++;

	return err;

fail4:
	dprintk(MANTIS_ERROR, 1, "ERROR: Mantis DMA exit! <%d>", err);
	mantis_dma_exit(mantis);

fail3:
	dprintk(MANTIS_ERROR, 1, "ERROR: Mantis I2C exit! <%d>", err);
	mantis_i2c_exit(mantis);

fail2:
	dprintk(MANTIS_ERROR, 1, "ERROR: Mantis PCI exit! <%d>", err);
	mantis_pci_exit(mantis);

fail1:
	dprintk(MANTIS_ERROR, 1, "ERROR: Mantis free! <%d>", err);
	kfree(mantis);

fail0:
	return err;
}

static void hopper_pci_remove(struct pci_dev *pdev)
{
	struct mantis_pci *mantis = pci_get_drvdata(pdev);

	if (mantis) {
		mantis_dvb_exit(mantis);
		mantis_dma_exit(mantis);
		mantis_i2c_exit(mantis);
		mantis_pci_exit(mantis);
		kfree(mantis);
	}
	return;

}

static const struct pci_device_id hopper_pci_table[] = {
	MAKE_ENTRY(TWINHAN_TECHNOLOGIES, MANTIS_VP_3028_DVB_T, &vp3028_config,
		   NULL),
	{ }
};

MODULE_DEVICE_TABLE(pci, hopper_pci_table);

static struct pci_driver hopper_pci_driver = {
	.name		= DRIVER_NAME,
	.id_table	= hopper_pci_table,
	.probe		= hopper_pci_probe,
	.remove		= hopper_pci_remove,
};

module_pci_driver(hopper_pci_driver);

MODULE_DESCRIPTION("HOPPER driver");
MODULE_AUTHOR("Manu Abraham");
MODULE_LICENSE("GPL");
