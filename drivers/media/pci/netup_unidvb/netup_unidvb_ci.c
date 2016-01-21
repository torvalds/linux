/*
 * netup_unidvb_ci.c
 *
 * DVB CAM support for NetUP Universal Dual DVB-CI
 *
 * Copyright (C) 2014 NetUP Inc.
 * Copyright (C) 2014 Sergey Kozlov <serjk@netup.ru>
 * Copyright (C) 2014 Abylay Ospan <aospan@netup.ru>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kmod.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include "netup_unidvb.h"

/* CI slot 0 base address */
#define CAM0_CONFIG		0x0
#define CAM0_IO			0x8000
#define CAM0_MEM		0x10000
#define CAM0_SZ			32
/* CI slot 1 base address */
#define CAM1_CONFIG		0x20000
#define CAM1_IO			0x28000
#define CAM1_MEM		0x30000
#define CAM1_SZ			32
/* ctrlstat registers */
#define CAM_CTRLSTAT_READ_SET	0x4980
#define CAM_CTRLSTAT_CLR	0x4982
/* register bits */
#define BIT_CAM_STCHG		(1<<0)
#define BIT_CAM_PRESENT		(1<<1)
#define BIT_CAM_RESET		(1<<2)
#define BIT_CAM_BYPASS		(1<<3)
#define BIT_CAM_READY		(1<<4)
#define BIT_CAM_ERROR		(1<<5)
#define BIT_CAM_OVERCURR	(1<<6)
/* BIT_CAM_BYPASS bit shift for SLOT 1 */
#define CAM1_SHIFT 8

irqreturn_t netup_ci_interrupt(struct netup_unidvb_dev *ndev)
{
	writew(0x101, ndev->bmmio0 + CAM_CTRLSTAT_CLR);
	return IRQ_HANDLED;
}

static int netup_unidvb_ci_slot_ts_ctl(struct dvb_ca_en50221 *en50221,
				       int slot)
{
	struct netup_ci_state *state = en50221->data;
	struct netup_unidvb_dev *dev = state->dev;
	u16 shift = (state->nr == 1) ? CAM1_SHIFT : 0;

	dev_dbg(&dev->pci_dev->dev, "%s(): CAM_CTRLSTAT=0x%x\n",
		__func__, readw(dev->bmmio0 + CAM_CTRLSTAT_READ_SET));
	if (slot != 0)
		return -EINVAL;
	/* pass data to CAM module */
	writew(BIT_CAM_BYPASS << shift, dev->bmmio0 + CAM_CTRLSTAT_CLR);
	dev_dbg(&dev->pci_dev->dev, "%s(): CAM_CTRLSTAT=0x%x done\n",
		__func__, readw(dev->bmmio0 + CAM_CTRLSTAT_READ_SET));
	return 0;
}

static int netup_unidvb_ci_slot_shutdown(struct dvb_ca_en50221 *en50221,
					 int slot)
{
	struct netup_ci_state *state = en50221->data;
	struct netup_unidvb_dev *dev = state->dev;

	dev_dbg(&dev->pci_dev->dev, "%s()\n", __func__);
	return 0;
}

static int netup_unidvb_ci_slot_reset(struct dvb_ca_en50221 *en50221,
				      int slot)
{
	struct netup_ci_state *state = en50221->data;
	struct netup_unidvb_dev *dev = state->dev;
	unsigned long timeout = 0;
	u16 shift = (state->nr == 1) ? CAM1_SHIFT : 0;
	u16 ci_stat = 0;
	int reset_counter = 3;

	dev_dbg(&dev->pci_dev->dev, "%s(): CAM_CTRLSTAT_READ_SET=0x%x\n",
		__func__, readw(dev->bmmio0 + CAM_CTRLSTAT_READ_SET));
reset:
	timeout = jiffies + msecs_to_jiffies(5000);
	/* start reset */
	writew(BIT_CAM_RESET << shift, dev->bmmio0 + CAM_CTRLSTAT_READ_SET);
	dev_dbg(&dev->pci_dev->dev, "%s(): waiting for reset\n", __func__);
	/* wait until reset done */
	while (time_before(jiffies, timeout)) {
		ci_stat = readw(dev->bmmio0 + CAM_CTRLSTAT_READ_SET);
		if (ci_stat & (BIT_CAM_READY << shift))
			break;
		udelay(1000);
	}
	if (!(ci_stat & (BIT_CAM_READY << shift)) && reset_counter > 0) {
		dev_dbg(&dev->pci_dev->dev,
			"%s(): CAMP reset timeout! Will try again..\n",
			 __func__);
		reset_counter--;
		goto reset;
	}
	return 0;
}

static int netup_unidvb_poll_ci_slot_status(struct dvb_ca_en50221 *en50221,
					    int slot, int open)
{
	struct netup_ci_state *state = en50221->data;
	struct netup_unidvb_dev *dev = state->dev;
	u16 shift = (state->nr == 1) ? CAM1_SHIFT : 0;
	u16 ci_stat = 0;

	dev_dbg(&dev->pci_dev->dev, "%s(): CAM_CTRLSTAT_READ_SET=0x%x\n",
		__func__, readw(dev->bmmio0 + CAM_CTRLSTAT_READ_SET));
	ci_stat = readw(dev->bmmio0 + CAM_CTRLSTAT_READ_SET);
	if (ci_stat & (BIT_CAM_READY << shift)) {
		state->status = DVB_CA_EN50221_POLL_CAM_PRESENT |
			DVB_CA_EN50221_POLL_CAM_READY;
	} else if (ci_stat & (BIT_CAM_PRESENT << shift)) {
		state->status = DVB_CA_EN50221_POLL_CAM_PRESENT;
	} else {
		state->status = 0;
	}
	return state->status;
}

static int netup_unidvb_ci_read_attribute_mem(struct dvb_ca_en50221 *en50221,
					      int slot, int addr)
{
	struct netup_ci_state *state = en50221->data;
	struct netup_unidvb_dev *dev = state->dev;
	u8 val = *((u8 __force *)state->membase8_io + addr);

	dev_dbg(&dev->pci_dev->dev,
		"%s(): addr=0x%x val=0x%x\n", __func__, addr, val);
	return val;
}

static int netup_unidvb_ci_write_attribute_mem(struct dvb_ca_en50221 *en50221,
					       int slot, int addr, u8 data)
{
	struct netup_ci_state *state = en50221->data;
	struct netup_unidvb_dev *dev = state->dev;

	dev_dbg(&dev->pci_dev->dev,
		"%s(): addr=0x%x data=0x%x\n", __func__, addr, data);
	*((u8 __force *)state->membase8_io + addr) = data;
	return 0;
}

static int netup_unidvb_ci_read_cam_ctl(struct dvb_ca_en50221 *en50221,
					int slot, u8 addr)
{
	struct netup_ci_state *state = en50221->data;
	struct netup_unidvb_dev *dev = state->dev;
	u8 val = *((u8 __force *)state->membase8_io + addr);

	dev_dbg(&dev->pci_dev->dev,
		"%s(): addr=0x%x val=0x%x\n", __func__, addr, val);
	return val;
}

static int netup_unidvb_ci_write_cam_ctl(struct dvb_ca_en50221 *en50221,
					 int slot, u8 addr, u8 data)
{
	struct netup_ci_state *state = en50221->data;
	struct netup_unidvb_dev *dev = state->dev;

	dev_dbg(&dev->pci_dev->dev,
		"%s(): addr=0x%x data=0x%x\n", __func__, addr, data);
	*((u8 __force *)state->membase8_io + addr) = data;
	return 0;
}

int netup_unidvb_ci_register(struct netup_unidvb_dev *dev,
			     int num, struct pci_dev *pci_dev)
{
	int result;
	struct netup_ci_state *state;

	if (num < 0 || num > 1) {
		dev_err(&pci_dev->dev, "%s(): invalid CI adapter %d\n",
			__func__, num);
		return -EINVAL;
	}
	state = &dev->ci[num];
	state->nr = num;
	state->membase8_config = dev->bmmio1 +
		((num == 0) ? CAM0_CONFIG : CAM1_CONFIG);
	state->membase8_io = dev->bmmio1 +
		((num == 0) ? CAM0_IO : CAM1_IO);
	state->dev = dev;
	state->ca.owner = THIS_MODULE;
	state->ca.read_attribute_mem = netup_unidvb_ci_read_attribute_mem;
	state->ca.write_attribute_mem = netup_unidvb_ci_write_attribute_mem;
	state->ca.read_cam_control = netup_unidvb_ci_read_cam_ctl;
	state->ca.write_cam_control = netup_unidvb_ci_write_cam_ctl;
	state->ca.slot_reset = netup_unidvb_ci_slot_reset;
	state->ca.slot_shutdown = netup_unidvb_ci_slot_shutdown;
	state->ca.slot_ts_enable = netup_unidvb_ci_slot_ts_ctl;
	state->ca.poll_slot_status = netup_unidvb_poll_ci_slot_status;
	state->ca.data = state;
	result = dvb_ca_en50221_init(&dev->frontends[num].adapter,
		&state->ca, 0, 1);
	if (result < 0) {
		dev_err(&pci_dev->dev,
			"%s(): dvb_ca_en50221_init result %d\n",
			__func__, result);
		return result;
	}
	writew(NETUP_UNIDVB_IRQ_CI, dev->bmmio0 + REG_IMASK_SET);
	dev_info(&pci_dev->dev,
		"%s(): CI adapter %d init done\n", __func__, num);
	return 0;
}

void netup_unidvb_ci_unregister(struct netup_unidvb_dev *dev, int num)
{
	struct netup_ci_state *state;

	dev_dbg(&dev->pci_dev->dev, "%s()\n", __func__);
	if (num < 0 || num > 1) {
		dev_err(&dev->pci_dev->dev, "%s(): invalid CI adapter %d\n",
				__func__, num);
		return;
	}
	state = &dev->ci[num];
	dvb_ca_en50221_release(&state->ca);
}

