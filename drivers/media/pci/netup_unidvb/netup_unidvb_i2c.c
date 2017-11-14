/*
 * netup_unidvb_i2c.c
 *
 * Internal I2C bus driver for NetUP Universal Dual DVB-CI
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

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include "netup_unidvb.h"

#define NETUP_I2C_BUS0_ADDR		0x4800
#define NETUP_I2C_BUS1_ADDR		0x4840
#define NETUP_I2C_TIMEOUT		1000

/* twi_ctrl0_stat reg bits */
#define TWI_IRQEN_COMPL	0x1
#define TWI_IRQEN_ANACK 0x2
#define TWI_IRQEN_DNACK 0x4
#define TWI_IRQ_COMPL	(TWI_IRQEN_COMPL << 8)
#define TWI_IRQ_ANACK	(TWI_IRQEN_ANACK << 8)
#define TWI_IRQ_DNACK	(TWI_IRQEN_DNACK << 8)
#define TWI_IRQ_TX	0x800
#define TWI_IRQ_RX	0x1000
#define TWI_IRQEN	(TWI_IRQEN_COMPL | TWI_IRQEN_ANACK | TWI_IRQEN_DNACK)
/* twi_addr_ctrl1 reg bits*/
#define TWI_TRANSFER	0x100
#define TWI_NOSTOP	0x200
#define TWI_SOFT_RESET	0x2000
/* twi_clkdiv reg value */
#define TWI_CLKDIV	156
/* fifo_stat_ctrl reg bits */
#define FIFO_IRQEN	0x8000
#define FIFO_RESET	0x4000
/* FIFO size */
#define FIFO_SIZE	16

struct netup_i2c_fifo_regs {
	union {
		__u8	data8;
		__le16	data16;
		__le32	data32;
	};
	__u8		padding[4];
	__le16		stat_ctrl;
} __packed __aligned(1);

struct netup_i2c_regs {
	__le16				clkdiv;
	__le16				twi_ctrl0_stat;
	__le16				twi_addr_ctrl1;
	__le16				length;
	__u8				padding1[8];
	struct netup_i2c_fifo_regs	tx_fifo;
	__u8				padding2[6];
	struct netup_i2c_fifo_regs	rx_fifo;
} __packed __aligned(1);

irqreturn_t netup_i2c_interrupt(struct netup_i2c *i2c)
{
	u16 reg, tmp;
	unsigned long flags;
	irqreturn_t iret = IRQ_HANDLED;

	spin_lock_irqsave(&i2c->lock, flags);
	reg = readw(&i2c->regs->twi_ctrl0_stat);
	writew(reg & ~TWI_IRQEN, &i2c->regs->twi_ctrl0_stat);
	dev_dbg(i2c->adap.dev.parent,
		"%s(): twi_ctrl0_state 0x%x\n", __func__, reg);
	if ((reg & TWI_IRQEN_COMPL) != 0 && (reg & TWI_IRQ_COMPL)) {
		dev_dbg(i2c->adap.dev.parent,
			"%s(): TWI_IRQEN_COMPL\n", __func__);
		i2c->state = STATE_DONE;
		goto irq_ok;
	}
	if ((reg & TWI_IRQEN_ANACK) != 0 && (reg & TWI_IRQ_ANACK)) {
		dev_dbg(i2c->adap.dev.parent,
			"%s(): TWI_IRQEN_ANACK\n", __func__);
		i2c->state = STATE_ERROR;
		goto irq_ok;
	}
	if ((reg & TWI_IRQEN_DNACK) != 0 && (reg & TWI_IRQ_DNACK)) {
		dev_dbg(i2c->adap.dev.parent,
			"%s(): TWI_IRQEN_DNACK\n", __func__);
		i2c->state = STATE_ERROR;
		goto irq_ok;
	}
	if ((reg & TWI_IRQ_RX) != 0) {
		tmp = readw(&i2c->regs->rx_fifo.stat_ctrl);
		writew(tmp & ~FIFO_IRQEN, &i2c->regs->rx_fifo.stat_ctrl);
		i2c->state = STATE_WANT_READ;
		dev_dbg(i2c->adap.dev.parent,
			"%s(): want read\n", __func__);
		goto irq_ok;
	}
	if ((reg & TWI_IRQ_TX) != 0) {
		tmp = readw(&i2c->regs->tx_fifo.stat_ctrl);
		writew(tmp & ~FIFO_IRQEN, &i2c->regs->tx_fifo.stat_ctrl);
		i2c->state = STATE_WANT_WRITE;
		dev_dbg(i2c->adap.dev.parent,
			"%s(): want write\n", __func__);
		goto irq_ok;
	}
	dev_warn(&i2c->adap.dev, "%s(): not mine interrupt\n", __func__);
	iret = IRQ_NONE;
irq_ok:
	spin_unlock_irqrestore(&i2c->lock, flags);
	if (iret == IRQ_HANDLED)
		wake_up(&i2c->wq);
	return iret;
}

static void netup_i2c_reset(struct netup_i2c *i2c)
{
	dev_dbg(i2c->adap.dev.parent, "%s()\n", __func__);
	i2c->state = STATE_DONE;
	writew(TWI_SOFT_RESET, &i2c->regs->twi_addr_ctrl1);
	writew(TWI_CLKDIV, &i2c->regs->clkdiv);
	writew(FIFO_RESET, &i2c->regs->tx_fifo.stat_ctrl);
	writew(FIFO_RESET, &i2c->regs->rx_fifo.stat_ctrl);
	writew(0x800, &i2c->regs->tx_fifo.stat_ctrl);
	writew(0x800, &i2c->regs->rx_fifo.stat_ctrl);
}

static void netup_i2c_fifo_tx(struct netup_i2c *i2c)
{
	u8 data;
	u32 fifo_space = FIFO_SIZE -
		(readw(&i2c->regs->tx_fifo.stat_ctrl) & 0x3f);
	u32 msg_length = i2c->msg->len - i2c->xmit_size;

	msg_length = (msg_length < fifo_space ? msg_length : fifo_space);
	while (msg_length--) {
		data = i2c->msg->buf[i2c->xmit_size++];
		writeb(data, &i2c->regs->tx_fifo.data8);
		dev_dbg(i2c->adap.dev.parent,
			"%s(): write 0x%02x\n", __func__, data);
	}
	if (i2c->xmit_size < i2c->msg->len) {
		dev_dbg(i2c->adap.dev.parent,
			"%s(): TX IRQ enabled\n", __func__);
		writew(readw(&i2c->regs->tx_fifo.stat_ctrl) | FIFO_IRQEN,
			&i2c->regs->tx_fifo.stat_ctrl);
	}
}

static void netup_i2c_fifo_rx(struct netup_i2c *i2c)
{
	u8 data;
	u32 fifo_size = readw(&i2c->regs->rx_fifo.stat_ctrl) & 0x3f;

	dev_dbg(i2c->adap.dev.parent,
		"%s(): RX fifo size %d\n", __func__, fifo_size);
	while (fifo_size--) {
		data = readb(&i2c->regs->rx_fifo.data8);
		if ((i2c->msg->flags & I2C_M_RD) != 0 &&
					i2c->xmit_size < i2c->msg->len) {
			i2c->msg->buf[i2c->xmit_size++] = data;
			dev_dbg(i2c->adap.dev.parent,
				"%s(): read 0x%02x\n", __func__, data);
		}
	}
	if (i2c->xmit_size < i2c->msg->len) {
		dev_dbg(i2c->adap.dev.parent,
			"%s(): RX IRQ enabled\n", __func__);
		writew(readw(&i2c->regs->rx_fifo.stat_ctrl) | FIFO_IRQEN,
			&i2c->regs->rx_fifo.stat_ctrl);
	}
}

static void netup_i2c_start_xfer(struct netup_i2c *i2c)
{
	u16 rdflag = ((i2c->msg->flags & I2C_M_RD) ? 1 : 0);
	u16 reg = readw(&i2c->regs->twi_ctrl0_stat);

	writew(TWI_IRQEN | reg, &i2c->regs->twi_ctrl0_stat);
	writew(i2c->msg->len, &i2c->regs->length);
	writew(TWI_TRANSFER | (i2c->msg->addr << 1) | rdflag,
		&i2c->regs->twi_addr_ctrl1);
	dev_dbg(i2c->adap.dev.parent,
		"%s(): length %d twi_addr_ctrl1 0x%x twi_ctrl0_stat 0x%x\n",
		__func__, readw(&i2c->regs->length),
		readw(&i2c->regs->twi_addr_ctrl1),
		readw(&i2c->regs->twi_ctrl0_stat));
	i2c->state = STATE_WAIT;
	i2c->xmit_size = 0;
	if (!rdflag)
		netup_i2c_fifo_tx(i2c);
	else
		writew(FIFO_IRQEN | readw(&i2c->regs->rx_fifo.stat_ctrl),
			&i2c->regs->rx_fifo.stat_ctrl);
}

static int netup_i2c_xfer(struct i2c_adapter *adap,
			  struct i2c_msg *msgs, int num)
{
	unsigned long flags;
	int i, trans_done, res = num;
	struct netup_i2c *i2c = i2c_get_adapdata(adap);
	u16 reg;

	if (num <= 0) {
		dev_dbg(i2c->adap.dev.parent,
			"%s(): num == %d\n", __func__, num);
		return -EINVAL;
	}
	spin_lock_irqsave(&i2c->lock, flags);
	if (i2c->state != STATE_DONE) {
		dev_dbg(i2c->adap.dev.parent,
			"%s(): i2c->state == %d, resetting I2C\n",
			__func__, i2c->state);
		netup_i2c_reset(i2c);
	}
	dev_dbg(i2c->adap.dev.parent, "%s() num %d\n", __func__, num);
	for (i = 0; i < num; i++) {
		i2c->msg = &msgs[i];
		netup_i2c_start_xfer(i2c);
		trans_done = 0;
		while (!trans_done) {
			spin_unlock_irqrestore(&i2c->lock, flags);
			if (wait_event_timeout(i2c->wq,
					i2c->state != STATE_WAIT,
					msecs_to_jiffies(NETUP_I2C_TIMEOUT))) {
				spin_lock_irqsave(&i2c->lock, flags);
				switch (i2c->state) {
				case STATE_WANT_READ:
					netup_i2c_fifo_rx(i2c);
					break;
				case STATE_WANT_WRITE:
					netup_i2c_fifo_tx(i2c);
					break;
				case STATE_DONE:
					if ((i2c->msg->flags & I2C_M_RD) != 0 &&
						i2c->xmit_size != i2c->msg->len)
						netup_i2c_fifo_rx(i2c);
					dev_dbg(i2c->adap.dev.parent,
						"%s(): msg %d OK\n",
						__func__, i);
					trans_done = 1;
					break;
				case STATE_ERROR:
					res = -EIO;
					dev_dbg(i2c->adap.dev.parent,
						"%s(): error state\n",
						__func__);
					goto done;
				default:
					dev_dbg(i2c->adap.dev.parent,
						"%s(): invalid state %d\n",
						__func__, i2c->state);
					res = -EINVAL;
					goto done;
				}
				if (!trans_done) {
					i2c->state = STATE_WAIT;
					reg = readw(
						&i2c->regs->twi_ctrl0_stat);
					writew(TWI_IRQEN | reg,
						&i2c->regs->twi_ctrl0_stat);
				}
				spin_unlock_irqrestore(&i2c->lock, flags);
			} else {
				spin_lock_irqsave(&i2c->lock, flags);
				dev_dbg(i2c->adap.dev.parent,
					"%s(): wait timeout\n", __func__);
				res = -ETIMEDOUT;
				goto done;
			}
			spin_lock_irqsave(&i2c->lock, flags);
		}
	}
done:
	spin_unlock_irqrestore(&i2c->lock, flags);
	dev_dbg(i2c->adap.dev.parent, "%s(): result %d\n", __func__, res);
	return res;
}

static u32 netup_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm netup_i2c_algorithm = {
	.master_xfer	= netup_i2c_xfer,
	.functionality	= netup_i2c_func,
};

static const struct i2c_adapter netup_i2c_adapter = {
	.owner		= THIS_MODULE,
	.name		= NETUP_UNIDVB_NAME,
	.class		= I2C_CLASS_HWMON | I2C_CLASS_SPD,
	.algo		= &netup_i2c_algorithm,
};

static int netup_i2c_init(struct netup_unidvb_dev *ndev, int bus_num)
{
	int ret;
	struct netup_i2c *i2c;

	if (bus_num < 0 || bus_num > 1) {
		dev_err(&ndev->pci_dev->dev,
			"%s(): invalid bus_num %d\n", __func__, bus_num);
		return -EINVAL;
	}
	i2c = &ndev->i2c[bus_num];
	spin_lock_init(&i2c->lock);
	init_waitqueue_head(&i2c->wq);
	i2c->regs = (struct netup_i2c_regs __iomem *)(ndev->bmmio0 +
		(bus_num == 0 ? NETUP_I2C_BUS0_ADDR : NETUP_I2C_BUS1_ADDR));
	netup_i2c_reset(i2c);
	i2c->adap = netup_i2c_adapter;
	i2c->adap.dev.parent = &ndev->pci_dev->dev;
	i2c_set_adapdata(&i2c->adap, i2c);
	ret = i2c_add_adapter(&i2c->adap);
	if (ret)
		return ret;
	dev_info(&ndev->pci_dev->dev,
		"%s(): registered I2C bus %d at 0x%x\n",
		__func__,
		bus_num, (bus_num == 0 ?
			NETUP_I2C_BUS0_ADDR :
			NETUP_I2C_BUS1_ADDR));
	return 0;
}

static void netup_i2c_remove(struct netup_unidvb_dev *ndev, int bus_num)
{
	struct netup_i2c *i2c;

	if (bus_num < 0 || bus_num > 1) {
		dev_err(&ndev->pci_dev->dev,
			"%s(): invalid bus number %d\n", __func__, bus_num);
		return;
	}
	i2c = &ndev->i2c[bus_num];
	netup_i2c_reset(i2c);
	/* remove adapter */
	i2c_del_adapter(&i2c->adap);
	dev_info(&ndev->pci_dev->dev,
		"netup_i2c_remove: unregistered I2C bus %d\n", bus_num);
}

int netup_i2c_register(struct netup_unidvb_dev *ndev)
{
	int ret;

	ret = netup_i2c_init(ndev, 0);
	if (ret)
		return ret;
	ret = netup_i2c_init(ndev, 1);
	if (ret) {
		netup_i2c_remove(ndev, 0);
		return ret;
	}
	return 0;
}

void netup_i2c_unregister(struct netup_unidvb_dev *ndev)
{
	netup_i2c_remove(ndev, 0);
	netup_i2c_remove(ndev, 1);
}

