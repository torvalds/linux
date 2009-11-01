/*
 *	Copyright (C) 2002 Motorola GSG-China
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version 2
 *	of the License, or (at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 *	USA.
 *
 * Author:
 *	Darius Augulis, Teltonika Inc.
 *
 * Desc.:
 *	Implementation of I2C Adapter/Algorithm Driver
 *	for I2C Bus integrated in Freescale i.MX/MXC processors
 *
 *	Derived from Motorola GSG China I2C example driver
 *
 *	Copyright (C) 2005 Torsten Koschorrek <koschorrek at synertronixx.de
 *	Copyright (C) 2005 Matthias Blaschke <blaschke at synertronixx.de
 *	Copyright (C) 2007 RightHand Technologies, Inc.
 *	Copyright (C) 2008 Darius Augulis <darius.augulis at teltonika.lt>
 *
 */

/** Includes *******************************************************************
*******************************************************************************/

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/sched.h>
#include <linux/platform_device.h>
#include <linux/clk.h>

#include <mach/irqs.h>
#include <mach/hardware.h>
#include <mach/i2c.h>

/** Defines ********************************************************************
*******************************************************************************/

/* This will be the driver name the kernel reports */
#define DRIVER_NAME "imx-i2c"

/* Default value */
#define IMX_I2C_BIT_RATE	100000	/* 100kHz */

/* IMX I2C registers */
#define IMX_I2C_IADR	0x00	/* i2c slave address */
#define IMX_I2C_IFDR	0x04	/* i2c frequency divider */
#define IMX_I2C_I2CR	0x08	/* i2c control */
#define IMX_I2C_I2SR	0x0C	/* i2c status */
#define IMX_I2C_I2DR	0x10	/* i2c transfer data */

/* Bits of IMX I2C registers */
#define I2SR_RXAK	0x01
#define I2SR_IIF	0x02
#define I2SR_SRW	0x04
#define I2SR_IAL	0x10
#define I2SR_IBB	0x20
#define I2SR_IAAS	0x40
#define I2SR_ICF	0x80
#define I2CR_RSTA	0x04
#define I2CR_TXAK	0x08
#define I2CR_MTX	0x10
#define I2CR_MSTA	0x20
#define I2CR_IIEN	0x40
#define I2CR_IEN	0x80

/** Variables ******************************************************************
*******************************************************************************/

/*
 * sorted list of clock divider, register value pairs
 * taken from table 26-5, p.26-9, Freescale i.MX
 * Integrated Portable System Processor Reference Manual
 * Document Number: MC9328MXLRM, Rev. 5.1, 06/2007
 *
 * Duplicated divider values removed from list
 */

static u16 __initdata i2c_clk_div[50][2] = {
	{ 22,	0x20 }, { 24,	0x21 }, { 26,	0x22 }, { 28,	0x23 },
	{ 30,	0x00 },	{ 32,	0x24 }, { 36,	0x25 }, { 40,	0x26 },
	{ 42,	0x03 }, { 44,	0x27 },	{ 48,	0x28 }, { 52,	0x05 },
	{ 56,	0x29 }, { 60,	0x06 }, { 64,	0x2A },	{ 72,	0x2B },
	{ 80,	0x2C }, { 88,	0x09 }, { 96,	0x2D }, { 104,	0x0A },
	{ 112,	0x2E }, { 128,	0x2F }, { 144,	0x0C }, { 160,	0x30 },
	{ 192,	0x31 },	{ 224,	0x32 }, { 240,	0x0F }, { 256,	0x33 },
	{ 288,	0x10 }, { 320,	0x34 },	{ 384,	0x35 }, { 448,	0x36 },
	{ 480,	0x13 }, { 512,	0x37 }, { 576,	0x14 },	{ 640,	0x38 },
	{ 768,	0x39 }, { 896,	0x3A }, { 960,	0x17 }, { 1024,	0x3B },
	{ 1152,	0x18 }, { 1280,	0x3C }, { 1536,	0x3D }, { 1792,	0x3E },
	{ 1920,	0x1B },	{ 2048,	0x3F }, { 2304,	0x1C }, { 2560,	0x1D },
	{ 3072,	0x1E }, { 3840,	0x1F }
};

struct imx_i2c_struct {
	struct i2c_adapter	adapter;
	struct resource		*res;
	struct clk		*clk;
	void __iomem		*base;
	int			irq;
	wait_queue_head_t	queue;
	unsigned long		i2csr;
	unsigned int 		disable_delay;
};

/** Functions for IMX I2C adapter driver ***************************************
*******************************************************************************/

static int i2c_imx_bus_busy(struct imx_i2c_struct *i2c_imx)
{
	unsigned long orig_jiffies = jiffies;

	dev_dbg(&i2c_imx->adapter.dev, "<%s>\n", __func__);

	/* wait for bus not busy */
	while (readb(i2c_imx->base + IMX_I2C_I2SR) & I2SR_IBB) {
		if (signal_pending(current)) {
			dev_dbg(&i2c_imx->adapter.dev,
				"<%s> I2C Interrupted\n", __func__);
			return -EINTR;
		}
		if (time_after(jiffies, orig_jiffies + HZ / 1000)) {
			dev_dbg(&i2c_imx->adapter.dev,
				"<%s> I2C bus is busy\n", __func__);
			return -EIO;
		}
		schedule();
	}

	return 0;
}

static int i2c_imx_trx_complete(struct imx_i2c_struct *i2c_imx)
{
	int result;

	result = wait_event_interruptible_timeout(i2c_imx->queue,
		i2c_imx->i2csr & I2SR_IIF, HZ / 10);

	if (unlikely(result < 0)) {
		dev_dbg(&i2c_imx->adapter.dev, "<%s> result < 0\n", __func__);
		return result;
	} else if (unlikely(!(i2c_imx->i2csr & I2SR_IIF))) {
		dev_dbg(&i2c_imx->adapter.dev, "<%s> Timeout\n", __func__);
		return -ETIMEDOUT;
	}
	dev_dbg(&i2c_imx->adapter.dev, "<%s> TRX complete\n", __func__);
	i2c_imx->i2csr = 0;
	return 0;
}

static int i2c_imx_acked(struct imx_i2c_struct *i2c_imx)
{
	if (readb(i2c_imx->base + IMX_I2C_I2SR) & I2SR_RXAK) {
		dev_dbg(&i2c_imx->adapter.dev, "<%s> No ACK\n", __func__);
		return -EIO;  /* No ACK */
	}

	dev_dbg(&i2c_imx->adapter.dev, "<%s> ACK received\n", __func__);
	return 0;
}

static void i2c_imx_start(struct imx_i2c_struct *i2c_imx)
{
	unsigned int temp = 0;

	dev_dbg(&i2c_imx->adapter.dev, "<%s>\n", __func__);

	/* Enable I2C controller */
	writeb(I2CR_IEN, i2c_imx->base + IMX_I2C_I2CR);
	/* Start I2C transaction */
	temp = readb(i2c_imx->base + IMX_I2C_I2CR);
	temp |= I2CR_MSTA;
	writeb(temp, i2c_imx->base + IMX_I2C_I2CR);
	temp |= I2CR_IIEN | I2CR_MTX | I2CR_TXAK;
	writeb(temp, i2c_imx->base + IMX_I2C_I2CR);
}

static void i2c_imx_stop(struct imx_i2c_struct *i2c_imx)
{
	unsigned int temp = 0;

	/* Stop I2C transaction */
	dev_dbg(&i2c_imx->adapter.dev, "<%s>\n", __func__);
	temp = readb(i2c_imx->base + IMX_I2C_I2CR);
	temp &= ~I2CR_MSTA;
	writeb(temp, i2c_imx->base + IMX_I2C_I2CR);
	/* setup chip registers to defaults */
	writeb(I2CR_IEN, i2c_imx->base + IMX_I2C_I2CR);
	writeb(0, i2c_imx->base + IMX_I2C_I2SR);
	/*
	 * This delay caused by an i.MXL hardware bug.
	 * If no (or too short) delay, no "STOP" bit will be generated.
	 */
	udelay(i2c_imx->disable_delay);
	/* Disable I2C controller */
	writeb(0, i2c_imx->base + IMX_I2C_I2CR);
}

static void __init i2c_imx_set_clk(struct imx_i2c_struct *i2c_imx,
							unsigned int rate)
{
	unsigned int i2c_clk_rate;
	unsigned int div;
	int i;

	/* Divider value calculation */
	i2c_clk_rate = clk_get_rate(i2c_imx->clk);
	div = (i2c_clk_rate + rate - 1) / rate;
	if (div < i2c_clk_div[0][0])
		i = 0;
	else if (div > i2c_clk_div[ARRAY_SIZE(i2c_clk_div) - 1][0])
		i = ARRAY_SIZE(i2c_clk_div) - 1;
	else
		for (i = 0; i2c_clk_div[i][0] < div; i++);

	/* Write divider value to register */
	writeb(i2c_clk_div[i][1], i2c_imx->base + IMX_I2C_IFDR);

	/*
	 * There dummy delay is calculated.
	 * It should be about one I2C clock period long.
	 * This delay is used in I2C bus disable function
	 * to fix chip hardware bug.
	 */
	i2c_imx->disable_delay = (500000U * i2c_clk_div[i][0]
		+ (i2c_clk_rate / 2) - 1) / (i2c_clk_rate / 2);

	/* dev_dbg() can't be used, because adapter is not yet registered */
#ifdef CONFIG_I2C_DEBUG_BUS
	printk(KERN_DEBUG "I2C: <%s> I2C_CLK=%d, REQ DIV=%d\n",
		__func__, i2c_clk_rate, div);
	printk(KERN_DEBUG "I2C: <%s> IFDR[IC]=0x%x, REAL DIV=%d\n",
		__func__, i2c_clk_div[i][1], i2c_clk_div[i][0]);
#endif
}

static irqreturn_t i2c_imx_isr(int irq, void *dev_id)
{
	struct imx_i2c_struct *i2c_imx = dev_id;
	unsigned int temp;

	temp = readb(i2c_imx->base + IMX_I2C_I2SR);
	if (temp & I2SR_IIF) {
		/* save status register */
		i2c_imx->i2csr = temp;
		temp &= ~I2SR_IIF;
		writeb(temp, i2c_imx->base + IMX_I2C_I2SR);
		wake_up_interruptible(&i2c_imx->queue);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static int i2c_imx_write(struct imx_i2c_struct *i2c_imx, struct i2c_msg *msgs)
{
	int i, result;

	dev_dbg(&i2c_imx->adapter.dev, "<%s> write slave address: addr=0x%x\n",
		__func__, msgs->addr << 1);

	/* write slave address */
	writeb(msgs->addr << 1, i2c_imx->base + IMX_I2C_I2DR);
	result = i2c_imx_trx_complete(i2c_imx);
	if (result)
		return result;
	result = i2c_imx_acked(i2c_imx);
	if (result)
		return result;
	dev_dbg(&i2c_imx->adapter.dev, "<%s> write data\n", __func__);

	/* write data */
	for (i = 0; i < msgs->len; i++) {
		dev_dbg(&i2c_imx->adapter.dev,
			"<%s> write byte: B%d=0x%X\n",
			__func__, i, msgs->buf[i]);
		writeb(msgs->buf[i], i2c_imx->base + IMX_I2C_I2DR);
		result = i2c_imx_trx_complete(i2c_imx);
		if (result)
			return result;
		result = i2c_imx_acked(i2c_imx);
		if (result)
			return result;
	}
	return 0;
}

static int i2c_imx_read(struct imx_i2c_struct *i2c_imx, struct i2c_msg *msgs)
{
	int i, result;
	unsigned int temp;

	dev_dbg(&i2c_imx->adapter.dev,
		"<%s> write slave address: addr=0x%x\n",
		__func__, (msgs->addr << 1) | 0x01);

	/* write slave address */
	writeb((msgs->addr << 1) | 0x01, i2c_imx->base + IMX_I2C_I2DR);
	result = i2c_imx_trx_complete(i2c_imx);
	if (result)
		return result;
	result = i2c_imx_acked(i2c_imx);
	if (result)
		return result;

	dev_dbg(&i2c_imx->adapter.dev, "<%s> setup bus\n", __func__);

	/* setup bus to read data */
	temp = readb(i2c_imx->base + IMX_I2C_I2CR);
	temp &= ~I2CR_MTX;
	if (msgs->len - 1)
		temp &= ~I2CR_TXAK;
	writeb(temp, i2c_imx->base + IMX_I2C_I2CR);
	readb(i2c_imx->base + IMX_I2C_I2DR); /* dummy read */

	dev_dbg(&i2c_imx->adapter.dev, "<%s> read data\n", __func__);

	/* read data */
	for (i = 0; i < msgs->len; i++) {
		result = i2c_imx_trx_complete(i2c_imx);
		if (result)
			return result;
		if (i == (msgs->len - 1)) {
			dev_dbg(&i2c_imx->adapter.dev,
				"<%s> clear MSTA\n", __func__);
			temp = readb(i2c_imx->base + IMX_I2C_I2CR);
			temp &= ~I2CR_MSTA;
			writeb(temp, i2c_imx->base + IMX_I2C_I2CR);
		} else if (i == (msgs->len - 2)) {
			dev_dbg(&i2c_imx->adapter.dev,
				"<%s> set TXAK\n", __func__);
			temp = readb(i2c_imx->base + IMX_I2C_I2CR);
			temp |= I2CR_TXAK;
			writeb(temp, i2c_imx->base + IMX_I2C_I2CR);
		}
		msgs->buf[i] = readb(i2c_imx->base + IMX_I2C_I2DR);
		dev_dbg(&i2c_imx->adapter.dev,
			"<%s> read byte: B%d=0x%X\n",
			__func__, i, msgs->buf[i]);
	}
	return 0;
}

static int i2c_imx_xfer(struct i2c_adapter *adapter,
						struct i2c_msg *msgs, int num)
{
	unsigned int i, temp;
	int result;
	struct imx_i2c_struct *i2c_imx = i2c_get_adapdata(adapter);

	dev_dbg(&i2c_imx->adapter.dev, "<%s>\n", __func__);

	/* Check if i2c bus is not busy */
	result = i2c_imx_bus_busy(i2c_imx);
	if (result)
		goto fail0;

	/* Start I2C transfer */
	i2c_imx_start(i2c_imx);

	/* read/write data */
	for (i = 0; i < num; i++) {
		if (i) {
			dev_dbg(&i2c_imx->adapter.dev,
				"<%s> repeated start\n", __func__);
			temp = readb(i2c_imx->base + IMX_I2C_I2CR);
			temp |= I2CR_RSTA;
			writeb(temp, i2c_imx->base + IMX_I2C_I2CR);
		}
		dev_dbg(&i2c_imx->adapter.dev,
			"<%s> transfer message: %d\n", __func__, i);
		/* write/read data */
#ifdef CONFIG_I2C_DEBUG_BUS
		temp = readb(i2c_imx->base + IMX_I2C_I2CR);
		dev_dbg(&i2c_imx->adapter.dev, "<%s> CONTROL: IEN=%d, IIEN=%d, "
			"MSTA=%d, MTX=%d, TXAK=%d, RSTA=%d\n", __func__,
			(temp & I2CR_IEN ? 1 : 0), (temp & I2CR_IIEN ? 1 : 0),
			(temp & I2CR_MSTA ? 1 : 0), (temp & I2CR_MTX ? 1 : 0),
			(temp & I2CR_TXAK ? 1 : 0), (temp & I2CR_RSTA ? 1 : 0));
		temp = readb(i2c_imx->base + IMX_I2C_I2SR);
		dev_dbg(&i2c_imx->adapter.dev,
			"<%s> STATUS: ICF=%d, IAAS=%d, IBB=%d, "
			"IAL=%d, SRW=%d, IIF=%d, RXAK=%d\n", __func__,
			(temp & I2SR_ICF ? 1 : 0), (temp & I2SR_IAAS ? 1 : 0),
			(temp & I2SR_IBB ? 1 : 0), (temp & I2SR_IAL ? 1 : 0),
			(temp & I2SR_SRW ? 1 : 0), (temp & I2SR_IIF ? 1 : 0),
			(temp & I2SR_RXAK ? 1 : 0));
#endif
		if (msgs[i].flags & I2C_M_RD)
			result = i2c_imx_read(i2c_imx, &msgs[i]);
		else
			result = i2c_imx_write(i2c_imx, &msgs[i]);
	}

fail0:
	/* Stop I2C transfer */
	i2c_imx_stop(i2c_imx);

	dev_dbg(&i2c_imx->adapter.dev, "<%s> exit with: %s: %d\n", __func__,
		(result < 0) ? "error" : "success msg",
			(result < 0) ? result : num);
	return (result < 0) ? result : num;
}

static u32 i2c_imx_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static struct i2c_algorithm i2c_imx_algo = {
	.master_xfer	= i2c_imx_xfer,
	.functionality	= i2c_imx_func,
};

static int __init i2c_imx_probe(struct platform_device *pdev)
{
	struct imx_i2c_struct *i2c_imx;
	struct resource *res;
	struct imxi2c_platform_data *pdata;
	void __iomem *base;
	resource_size_t res_size;
	int irq;
	int ret;

	dev_dbg(&pdev->dev, "<%s>\n", __func__);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "can't get device resources\n");
		return -ENOENT;
	}
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "can't get irq number\n");
		return -ENOENT;
	}

	pdata = pdev->dev.platform_data;

	if (pdata && pdata->init) {
		ret = pdata->init(&pdev->dev);
		if (ret)
			return ret;
	}

	res_size = resource_size(res);
	base = ioremap(res->start, res_size);
	if (!base) {
		dev_err(&pdev->dev, "ioremap failed\n");
		ret = -EIO;
		goto fail0;
	}

	i2c_imx = kzalloc(sizeof(struct imx_i2c_struct), GFP_KERNEL);
	if (!i2c_imx) {
		dev_err(&pdev->dev, "can't allocate interface\n");
		ret = -ENOMEM;
		goto fail1;
	}

	if (!request_mem_region(res->start, res_size, DRIVER_NAME)) {
		ret = -EBUSY;
		goto fail2;
	}

	/* Setup i2c_imx driver structure */
	strcpy(i2c_imx->adapter.name, pdev->name);
	i2c_imx->adapter.owner		= THIS_MODULE;
	i2c_imx->adapter.algo		= &i2c_imx_algo;
	i2c_imx->adapter.dev.parent	= &pdev->dev;
	i2c_imx->adapter.nr 		= pdev->id;
	i2c_imx->irq			= irq;
	i2c_imx->base			= base;
	i2c_imx->res			= res;

	/* Get I2C clock */
	i2c_imx->clk = clk_get(&pdev->dev, "i2c_clk");
	if (IS_ERR(i2c_imx->clk)) {
		ret = PTR_ERR(i2c_imx->clk);
		dev_err(&pdev->dev, "can't get I2C clock\n");
		goto fail3;
	}
	clk_enable(i2c_imx->clk);

	/* Request IRQ */
	ret = request_irq(i2c_imx->irq, i2c_imx_isr, 0, pdev->name, i2c_imx);
	if (ret) {
		dev_err(&pdev->dev, "can't claim irq %d\n", i2c_imx->irq);
		goto fail4;
	}

	/* Init queue */
	init_waitqueue_head(&i2c_imx->queue);

	/* Set up adapter data */
	i2c_set_adapdata(&i2c_imx->adapter, i2c_imx);

	/* Set up clock divider */
	if (pdata && pdata->bitrate)
		i2c_imx_set_clk(i2c_imx, pdata->bitrate);
	else
		i2c_imx_set_clk(i2c_imx, IMX_I2C_BIT_RATE);

	/* Set up chip registers to defaults */
	writeb(0, i2c_imx->base + IMX_I2C_I2CR);
	writeb(0, i2c_imx->base + IMX_I2C_I2SR);

	/* Add I2C adapter */
	ret = i2c_add_numbered_adapter(&i2c_imx->adapter);
	if (ret < 0) {
		dev_err(&pdev->dev, "registration failed\n");
		goto fail5;
	}

	/* Set up platform driver data */
	platform_set_drvdata(pdev, i2c_imx);

	dev_dbg(&i2c_imx->adapter.dev, "claimed irq %d\n", i2c_imx->irq);
	dev_dbg(&i2c_imx->adapter.dev, "device resources from 0x%x to 0x%x\n",
		i2c_imx->res->start, i2c_imx->res->end);
	dev_dbg(&i2c_imx->adapter.dev, "allocated %d bytes at 0x%x \n",
		res_size, i2c_imx->res->start);
	dev_dbg(&i2c_imx->adapter.dev, "adapter name: \"%s\"\n",
		i2c_imx->adapter.name);
	dev_dbg(&i2c_imx->adapter.dev, "IMX I2C adapter registered\n");

	return 0;   /* Return OK */

fail5:
	free_irq(i2c_imx->irq, i2c_imx);
fail4:
	clk_disable(i2c_imx->clk);
	clk_put(i2c_imx->clk);
fail3:
	release_mem_region(i2c_imx->res->start, resource_size(res));
fail2:
	kfree(i2c_imx);
fail1:
	iounmap(base);
fail0:
	if (pdata && pdata->exit)
		pdata->exit(&pdev->dev);
	return ret; /* Return error number */
}

static int __exit i2c_imx_remove(struct platform_device *pdev)
{
	struct imx_i2c_struct *i2c_imx = platform_get_drvdata(pdev);
	struct imxi2c_platform_data *pdata = pdev->dev.platform_data;

	/* remove adapter */
	dev_dbg(&i2c_imx->adapter.dev, "adapter removed\n");
	i2c_del_adapter(&i2c_imx->adapter);
	platform_set_drvdata(pdev, NULL);

	/* free interrupt */
	free_irq(i2c_imx->irq, i2c_imx);

	/* setup chip registers to defaults */
	writeb(0, i2c_imx->base + IMX_I2C_IADR);
	writeb(0, i2c_imx->base + IMX_I2C_IFDR);
	writeb(0, i2c_imx->base + IMX_I2C_I2CR);
	writeb(0, i2c_imx->base + IMX_I2C_I2SR);

	/* Shut down hardware */
	if (pdata && pdata->exit)
		pdata->exit(&pdev->dev);

	/* Disable I2C clock */
	clk_disable(i2c_imx->clk);
	clk_put(i2c_imx->clk);

	release_mem_region(i2c_imx->res->start, resource_size(i2c_imx->res));
	iounmap(i2c_imx->base);
	kfree(i2c_imx);
	return 0;
}

static struct platform_driver i2c_imx_driver = {
	.probe		= i2c_imx_probe,
	.remove		= __exit_p(i2c_imx_remove),
	.driver	= {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
	}
};

static int __init i2c_adap_imx_init(void)
{
	return platform_driver_probe(&i2c_imx_driver, i2c_imx_probe);
}
subsys_initcall(i2c_adap_imx_init);

static void __exit i2c_adap_imx_exit(void)
{
	platform_driver_unregister(&i2c_imx_driver);
}
module_exit(i2c_adap_imx_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Darius Augulis");
MODULE_DESCRIPTION("I2C adapter driver for IMX I2C bus");
MODULE_ALIAS("platform:" DRIVER_NAME);
