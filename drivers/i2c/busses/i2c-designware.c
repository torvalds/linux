/*
 * Synopsys Designware I2C adapter driver (master only).
 *
 * Based on the TI DAVINCI I2C adapter driver.
 *
 * Copyright (C) 2006 Texas Instruments.
 * Copyright (C) 2007 MontaVista Software Inc.
 * Copyright (C) 2009 Provigent Ltd.
 *
 * ----------------------------------------------------------------------------
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 * ----------------------------------------------------------------------------
 *
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/clk.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/io.h>

/*
 * Registers offset
 */
#define DW_IC_CON		0x0
#define DW_IC_TAR		0x4
#define DW_IC_DATA_CMD		0x10
#define DW_IC_SS_SCL_HCNT	0x14
#define DW_IC_SS_SCL_LCNT	0x18
#define DW_IC_FS_SCL_HCNT	0x1c
#define DW_IC_FS_SCL_LCNT	0x20
#define DW_IC_INTR_STAT		0x2c
#define DW_IC_INTR_MASK		0x30
#define DW_IC_CLR_INTR		0x40
#define DW_IC_ENABLE		0x6c
#define DW_IC_STATUS		0x70
#define DW_IC_TXFLR		0x74
#define DW_IC_RXFLR		0x78
#define DW_IC_COMP_PARAM_1	0xf4
#define DW_IC_TX_ABRT_SOURCE	0x80

#define DW_IC_CON_MASTER		0x1
#define DW_IC_CON_SPEED_STD		0x2
#define DW_IC_CON_SPEED_FAST		0x4
#define DW_IC_CON_10BITADDR_MASTER	0x10
#define DW_IC_CON_RESTART_EN		0x20
#define DW_IC_CON_SLAVE_DISABLE		0x40

#define DW_IC_INTR_TX_EMPTY	0x10
#define DW_IC_INTR_TX_ABRT	0x40
#define DW_IC_INTR_STOP_DET	0x200

#define DW_IC_STATUS_ACTIVITY	0x1

#define DW_IC_ERR_TX_ABRT	0x1

/*
 * status codes
 */
#define STATUS_IDLE			0x0
#define STATUS_WRITE_IN_PROGRESS	0x1
#define STATUS_READ_IN_PROGRESS		0x2

#define TIMEOUT			20 /* ms */

/*
 * hardware abort codes from the DW_IC_TX_ABRT_SOURCE register
 *
 * only expected abort codes are listed here
 * refer to the datasheet for the full list
 */
#define ABRT_7B_ADDR_NOACK	0
#define ABRT_10ADDR1_NOACK	1
#define ABRT_10ADDR2_NOACK	2
#define ABRT_TXDATA_NOACK	3
#define ABRT_GCALL_NOACK	4
#define ABRT_GCALL_READ		5
#define ABRT_SBYTE_ACKDET	7
#define ABRT_SBYTE_NORSTRT	9
#define ABRT_10B_RD_NORSTRT	10
#define ARB_MASTER_DIS		11
#define ARB_LOST		12

static char *abort_sources[] = {
	[ABRT_7B_ADDR_NOACK]	=
		"slave address not acknowledged (7bit mode)",
	[ABRT_10ADDR1_NOACK]	=
		"first address byte not acknowledged (10bit mode)",
	[ABRT_10ADDR2_NOACK]	=
		"second address byte not acknowledged (10bit mode)",
	[ABRT_TXDATA_NOACK]		=
		"data not acknowledged",
	[ABRT_GCALL_NOACK]		=
		"no acknowledgement for a general call",
	[ABRT_GCALL_READ]		=
		"read after general call",
	[ABRT_SBYTE_ACKDET]		=
		"start byte acknowledged",
	[ABRT_SBYTE_NORSTRT]	=
		"trying to send start byte when restart is disabled",
	[ABRT_10B_RD_NORSTRT]	=
		"trying to read when restart is disabled (10bit mode)",
	[ARB_MASTER_DIS]		=
		"trying to use disabled adapter",
	[ARB_LOST]			=
		"lost arbitration",
};

/**
 * struct dw_i2c_dev - private i2c-designware data
 * @dev: driver model device node
 * @base: IO registers pointer
 * @cmd_complete: tx completion indicator
 * @pump_msg: continue in progress transfers
 * @lock: protect this struct and IO registers
 * @clk: input reference clock
 * @cmd_err: run time hadware error code
 * @msgs: points to an array of messages currently being transfered
 * @msgs_num: the number of elements in msgs
 * @msg_write_idx: the element index of the current tx message in the msgs
 *	array
 * @tx_buf_len: the length of the current tx buffer
 * @tx_buf: the current tx buffer
 * @msg_read_idx: the element index of the current rx message in the msgs
 *	array
 * @rx_buf_len: the length of the current rx buffer
 * @rx_buf: the current rx buffer
 * @msg_err: error status of the current transfer
 * @status: i2c master status, one of STATUS_*
 * @abort_source: copy of the TX_ABRT_SOURCE register
 * @irq: interrupt number for the i2c master
 * @adapter: i2c subsystem adapter node
 * @tx_fifo_depth: depth of the hardware tx fifo
 * @rx_fifo_depth: depth of the hardware rx fifo
 */
struct dw_i2c_dev {
	struct device		*dev;
	void __iomem		*base;
	struct completion	cmd_complete;
	struct tasklet_struct	pump_msg;
	struct mutex		lock;
	struct clk		*clk;
	int			cmd_err;
	struct i2c_msg		*msgs;
	int			msgs_num;
	int			msg_write_idx;
	u16			tx_buf_len;
	u8			*tx_buf;
	int			msg_read_idx;
	u16			rx_buf_len;
	u8			*rx_buf;
	int			msg_err;
	unsigned int		status;
	u16			abort_source;
	int			irq;
	struct i2c_adapter	adapter;
	unsigned int		tx_fifo_depth;
	unsigned int		rx_fifo_depth;
};

/**
 * i2c_dw_init() - initialize the designware i2c master hardware
 * @dev: device private data
 *
 * This functions configures and enables the I2C master.
 * This function is called during I2C init function, and in case of timeout at
 * run time.
 */
static void i2c_dw_init(struct dw_i2c_dev *dev)
{
	u32 input_clock_khz = clk_get_rate(dev->clk) / 1000;
	u16 ic_con;

	/* Disable the adapter */
	writeb(0, dev->base + DW_IC_ENABLE);

	/* set standard and fast speed deviders for high/low periods */
	writew((input_clock_khz * 40 / 10000)+1, /* std speed high, 4us */
			dev->base + DW_IC_SS_SCL_HCNT);
	writew((input_clock_khz * 47 / 10000)+1, /* std speed low, 4.7us */
			dev->base + DW_IC_SS_SCL_LCNT);
	writew((input_clock_khz *  6 / 10000)+1, /* fast speed high, 0.6us */
			dev->base + DW_IC_FS_SCL_HCNT);
	writew((input_clock_khz * 13 / 10000)+1, /* fast speed low, 1.3us */
			dev->base + DW_IC_FS_SCL_LCNT);

	/* configure the i2c master */
	ic_con = DW_IC_CON_MASTER | DW_IC_CON_SLAVE_DISABLE |
		DW_IC_CON_RESTART_EN | DW_IC_CON_SPEED_FAST;
	writew(ic_con, dev->base + DW_IC_CON);
}

/*
 * Waiting for bus not busy
 */
static int i2c_dw_wait_bus_not_busy(struct dw_i2c_dev *dev)
{
	int timeout = TIMEOUT;

	while (readb(dev->base + DW_IC_STATUS) & DW_IC_STATUS_ACTIVITY) {
		if (timeout <= 0) {
			dev_warn(dev->dev, "timeout waiting for bus ready\n");
			return -ETIMEDOUT;
		}
		timeout--;
		mdelay(1);
	}

	return 0;
}

/*
 * Initiate low level master read/write transaction.
 * This function is called from i2c_dw_xfer when starting a transfer.
 * This function is also called from dw_i2c_pump_msg to continue a transfer
 * that is longer than the size of the TX FIFO.
 */
static void
i2c_dw_xfer_msg(struct i2c_adapter *adap)
{
	struct dw_i2c_dev *dev = i2c_get_adapdata(adap);
	struct i2c_msg *msgs = dev->msgs;
	int num = dev->msgs_num;
	u16 ic_con, intr_mask;
	int tx_limit = dev->tx_fifo_depth - readb(dev->base + DW_IC_TXFLR);
	int rx_limit = dev->rx_fifo_depth - readb(dev->base + DW_IC_RXFLR);
	u16 addr = msgs[dev->msg_write_idx].addr;
	u16 buf_len = dev->tx_buf_len;

	if (!(dev->status & STATUS_WRITE_IN_PROGRESS)) {
		/* Disable the adapter */
		writeb(0, dev->base + DW_IC_ENABLE);

		/* set the slave (target) address */
		writew(msgs[dev->msg_write_idx].addr, dev->base + DW_IC_TAR);

		/* if the slave address is ten bit address, enable 10BITADDR */
		ic_con = readw(dev->base + DW_IC_CON);
		if (msgs[dev->msg_write_idx].flags & I2C_M_TEN)
			ic_con |= DW_IC_CON_10BITADDR_MASTER;
		else
			ic_con &= ~DW_IC_CON_10BITADDR_MASTER;
		writew(ic_con, dev->base + DW_IC_CON);

		/* Enable the adapter */
		writeb(1, dev->base + DW_IC_ENABLE);
	}

	for (; dev->msg_write_idx < num; dev->msg_write_idx++) {
		/* if target address has changed, we need to
		 * reprogram the target address in the i2c
		 * adapter when we are done with this transfer
		 */
		if (msgs[dev->msg_write_idx].addr != addr)
			return;

		if (msgs[dev->msg_write_idx].len == 0) {
			dev_err(dev->dev,
				"%s: invalid message length\n", __func__);
			dev->msg_err = -EINVAL;
			return;
		}

		if (!(dev->status & STATUS_WRITE_IN_PROGRESS)) {
			/* new i2c_msg */
			dev->tx_buf = msgs[dev->msg_write_idx].buf;
			buf_len = msgs[dev->msg_write_idx].len;
		}

		while (buf_len > 0 && tx_limit > 0 && rx_limit > 0) {
			if (msgs[dev->msg_write_idx].flags & I2C_M_RD) {
				writew(0x100, dev->base + DW_IC_DATA_CMD);
				rx_limit--;
			} else
				writew(*(dev->tx_buf++),
						dev->base + DW_IC_DATA_CMD);
			tx_limit--; buf_len--;
		}
	}

	intr_mask = DW_IC_INTR_STOP_DET | DW_IC_INTR_TX_ABRT;
	if (buf_len > 0) { /* more bytes to be written */
		intr_mask |= DW_IC_INTR_TX_EMPTY;
		dev->status |= STATUS_WRITE_IN_PROGRESS;
	} else
		dev->status &= ~STATUS_WRITE_IN_PROGRESS;
	writew(intr_mask, dev->base + DW_IC_INTR_MASK);

	dev->tx_buf_len = buf_len;
}

static void
i2c_dw_read(struct i2c_adapter *adap)
{
	struct dw_i2c_dev *dev = i2c_get_adapdata(adap);
	struct i2c_msg *msgs = dev->msgs;
	int num = dev->msgs_num;
	u16 addr = msgs[dev->msg_read_idx].addr;
	int rx_valid = readw(dev->base + DW_IC_RXFLR);

	for (; dev->msg_read_idx < num; dev->msg_read_idx++) {
		u16 len;
		u8 *buf;

		if (!(msgs[dev->msg_read_idx].flags & I2C_M_RD))
			continue;

		/* different i2c client, reprogram the i2c adapter */
		if (msgs[dev->msg_read_idx].addr != addr)
			return;

		if (!(dev->status & STATUS_READ_IN_PROGRESS)) {
			len = msgs[dev->msg_read_idx].len;
			buf = msgs[dev->msg_read_idx].buf;
		} else {
			len = dev->rx_buf_len;
			buf = dev->rx_buf;
		}

		for (; len > 0 && rx_valid > 0; len--, rx_valid--)
			*buf++ = readb(dev->base + DW_IC_DATA_CMD);

		if (len > 0) {
			dev->status |= STATUS_READ_IN_PROGRESS;
			dev->rx_buf_len = len;
			dev->rx_buf = buf;
			return;
		} else
			dev->status &= ~STATUS_READ_IN_PROGRESS;
	}
}

/*
 * Prepare controller for a transaction and call i2c_dw_xfer_msg
 */
static int
i2c_dw_xfer(struct i2c_adapter *adap, struct i2c_msg msgs[], int num)
{
	struct dw_i2c_dev *dev = i2c_get_adapdata(adap);
	int ret;

	dev_dbg(dev->dev, "%s: msgs: %d\n", __func__, num);

	mutex_lock(&dev->lock);

	INIT_COMPLETION(dev->cmd_complete);
	dev->msgs = msgs;
	dev->msgs_num = num;
	dev->cmd_err = 0;
	dev->msg_write_idx = 0;
	dev->msg_read_idx = 0;
	dev->msg_err = 0;
	dev->status = STATUS_IDLE;

	ret = i2c_dw_wait_bus_not_busy(dev);
	if (ret < 0)
		goto done;

	/* start the transfers */
	i2c_dw_xfer_msg(adap);

	/* wait for tx to complete */
	ret = wait_for_completion_interruptible_timeout(&dev->cmd_complete, HZ);
	if (ret == 0) {
		dev_err(dev->dev, "controller timed out\n");
		i2c_dw_init(dev);
		ret = -ETIMEDOUT;
		goto done;
	} else if (ret < 0)
		goto done;

	if (dev->msg_err) {
		ret = dev->msg_err;
		goto done;
	}

	/* no error */
	if (likely(!dev->cmd_err)) {
		/* read rx fifo, and disable the adapter */
		do {
			i2c_dw_read(adap);
		} while (dev->status & STATUS_READ_IN_PROGRESS);
		writeb(0, dev->base + DW_IC_ENABLE);
		ret = num;
		goto done;
	}

	/* We have an error */
	if (dev->cmd_err == DW_IC_ERR_TX_ABRT) {
		unsigned long abort_source = dev->abort_source;
		int i;

		for_each_bit(i, &abort_source, ARRAY_SIZE(abort_sources)) {
		    dev_err(dev->dev, "%s: %s\n", __func__, abort_sources[i]);
		}
	}
	ret = -EIO;

done:
	mutex_unlock(&dev->lock);

	return ret;
}

static u32 i2c_dw_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_10BIT_ADDR;
}

static void dw_i2c_pump_msg(unsigned long data)
{
	struct dw_i2c_dev *dev = (struct dw_i2c_dev *) data;
	u16 intr_mask;

	i2c_dw_read(&dev->adapter);
	i2c_dw_xfer_msg(&dev->adapter);

	intr_mask = DW_IC_INTR_STOP_DET | DW_IC_INTR_TX_ABRT;
	if (dev->status & STATUS_WRITE_IN_PROGRESS)
		intr_mask |= DW_IC_INTR_TX_EMPTY;
	writew(intr_mask, dev->base + DW_IC_INTR_MASK);
}

/*
 * Interrupt service routine. This gets called whenever an I2C interrupt
 * occurs.
 */
static irqreturn_t i2c_dw_isr(int this_irq, void *dev_id)
{
	struct dw_i2c_dev *dev = dev_id;
	u16 stat;

	stat = readw(dev->base + DW_IC_INTR_STAT);
	dev_dbg(dev->dev, "%s: stat=0x%x\n", __func__, stat);
	if (stat & DW_IC_INTR_TX_ABRT) {
		dev->abort_source = readw(dev->base + DW_IC_TX_ABRT_SOURCE);
		dev->cmd_err |= DW_IC_ERR_TX_ABRT;
		dev->status = STATUS_IDLE;
	} else if (stat & DW_IC_INTR_TX_EMPTY)
		tasklet_schedule(&dev->pump_msg);

	readb(dev->base + DW_IC_CLR_INTR);	/* clear interrupts */
	writew(0, dev->base + DW_IC_INTR_MASK);	/* disable interrupts */
	if (stat & (DW_IC_INTR_TX_ABRT | DW_IC_INTR_STOP_DET))
		complete(&dev->cmd_complete);

	return IRQ_HANDLED;
}

static struct i2c_algorithm i2c_dw_algo = {
	.master_xfer	= i2c_dw_xfer,
	.functionality	= i2c_dw_func,
};

static int __devinit dw_i2c_probe(struct platform_device *pdev)
{
	struct dw_i2c_dev *dev;
	struct i2c_adapter *adap;
	struct resource *mem, *irq, *ioarea;
	int r;

	/* NOTE: driver uses the static register mapping */
	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!mem) {
		dev_err(&pdev->dev, "no mem resource?\n");
		return -EINVAL;
	}

	irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!irq) {
		dev_err(&pdev->dev, "no irq resource?\n");
		return -EINVAL;
	}

	ioarea = request_mem_region(mem->start, resource_size(mem),
			pdev->name);
	if (!ioarea) {
		dev_err(&pdev->dev, "I2C region already claimed\n");
		return -EBUSY;
	}

	dev = kzalloc(sizeof(struct dw_i2c_dev), GFP_KERNEL);
	if (!dev) {
		r = -ENOMEM;
		goto err_release_region;
	}

	init_completion(&dev->cmd_complete);
	tasklet_init(&dev->pump_msg, dw_i2c_pump_msg, (unsigned long) dev);
	mutex_init(&dev->lock);
	dev->dev = get_device(&pdev->dev);
	dev->irq = irq->start;
	platform_set_drvdata(pdev, dev);

	dev->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(dev->clk)) {
		r = -ENODEV;
		goto err_free_mem;
	}
	clk_enable(dev->clk);

	dev->base = ioremap(mem->start, resource_size(mem));
	if (dev->base == NULL) {
		dev_err(&pdev->dev, "failure mapping io resources\n");
		r = -EBUSY;
		goto err_unuse_clocks;
	}
	{
		u32 param1 = readl(dev->base + DW_IC_COMP_PARAM_1);

		dev->tx_fifo_depth = ((param1 >> 16) & 0xff) + 1;
		dev->rx_fifo_depth = ((param1 >> 8)  & 0xff) + 1;
	}
	i2c_dw_init(dev);

	writew(0, dev->base + DW_IC_INTR_MASK); /* disable IRQ */
	r = request_irq(dev->irq, i2c_dw_isr, 0, pdev->name, dev);
	if (r) {
		dev_err(&pdev->dev, "failure requesting irq %i\n", dev->irq);
		goto err_iounmap;
	}

	adap = &dev->adapter;
	i2c_set_adapdata(adap, dev);
	adap->owner = THIS_MODULE;
	adap->class = I2C_CLASS_HWMON;
	strlcpy(adap->name, "Synopsys DesignWare I2C adapter",
			sizeof(adap->name));
	adap->algo = &i2c_dw_algo;
	adap->dev.parent = &pdev->dev;

	adap->nr = pdev->id;
	r = i2c_add_numbered_adapter(adap);
	if (r) {
		dev_err(&pdev->dev, "failure adding adapter\n");
		goto err_free_irq;
	}

	return 0;

err_free_irq:
	free_irq(dev->irq, dev);
err_iounmap:
	iounmap(dev->base);
err_unuse_clocks:
	clk_disable(dev->clk);
	clk_put(dev->clk);
	dev->clk = NULL;
err_free_mem:
	platform_set_drvdata(pdev, NULL);
	put_device(&pdev->dev);
	kfree(dev);
err_release_region:
	release_mem_region(mem->start, resource_size(mem));

	return r;
}

static int __devexit dw_i2c_remove(struct platform_device *pdev)
{
	struct dw_i2c_dev *dev = platform_get_drvdata(pdev);
	struct resource *mem;

	platform_set_drvdata(pdev, NULL);
	i2c_del_adapter(&dev->adapter);
	put_device(&pdev->dev);

	clk_disable(dev->clk);
	clk_put(dev->clk);
	dev->clk = NULL;

	writeb(0, dev->base + DW_IC_ENABLE);
	free_irq(dev->irq, dev);
	kfree(dev);

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(mem->start, resource_size(mem));
	return 0;
}

/* work with hotplug and coldplug */
MODULE_ALIAS("platform:i2c_designware");

static struct platform_driver dw_i2c_driver = {
	.remove		= __devexit_p(dw_i2c_remove),
	.driver		= {
		.name	= "i2c_designware",
		.owner	= THIS_MODULE,
	},
};

static int __init dw_i2c_init_driver(void)
{
	return platform_driver_probe(&dw_i2c_driver, dw_i2c_probe);
}
module_init(dw_i2c_init_driver);

static void __exit dw_i2c_exit_driver(void)
{
	platform_driver_unregister(&dw_i2c_driver);
}
module_exit(dw_i2c_exit_driver);

MODULE_AUTHOR("Baruch Siach <baruch@tkos.co.il>");
MODULE_DESCRIPTION("Synopsys DesignWare I2C bus adapter");
MODULE_LICENSE("GPL");
