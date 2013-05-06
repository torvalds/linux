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
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_i2c.h>
#include <linux/platform_data/i2c-imx.h>

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

enum imx_i2c_type {
	IMX1_I2C,
	IMX21_I2C,
};

struct imx_i2c_struct {
	struct i2c_adapter	adapter;
	struct clk		*clk;
	void __iomem		*base;
	wait_queue_head_t	queue;
	unsigned long		i2csr;
	unsigned int 		disable_delay;
	int			stopped;
	unsigned int		ifdr; /* IMX_I2C_IFDR */
	enum imx_i2c_type	devtype;
};

static struct platform_device_id imx_i2c_devtype[] = {
	{
		.name = "imx1-i2c",
		.driver_data = IMX1_I2C,
	}, {
		.name = "imx21-i2c",
		.driver_data = IMX21_I2C,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(platform, imx_i2c_devtype);

static const struct of_device_id i2c_imx_dt_ids[] = {
	{ .compatible = "fsl,imx1-i2c", .data = &imx_i2c_devtype[IMX1_I2C], },
	{ .compatible = "fsl,imx21-i2c", .data = &imx_i2c_devtype[IMX21_I2C], },
	{ /* sentinel */ }
};

static inline int is_imx1_i2c(struct imx_i2c_struct *i2c_imx)
{
	return i2c_imx->devtype == IMX1_I2C;
}

/** Functions for IMX I2C adapter driver ***************************************
*******************************************************************************/

static int i2c_imx_bus_busy(struct imx_i2c_struct *i2c_imx, int for_busy)
{
	unsigned long orig_jiffies = jiffies;
	unsigned int temp;

	dev_dbg(&i2c_imx->adapter.dev, "<%s>\n", __func__);

	while (1) {
		temp = readb(i2c_imx->base + IMX_I2C_I2SR);
		if (for_busy && (temp & I2SR_IBB))
			break;
		if (!for_busy && !(temp & I2SR_IBB))
			break;
		if (time_after(jiffies, orig_jiffies + msecs_to_jiffies(500))) {
			dev_dbg(&i2c_imx->adapter.dev,
				"<%s> I2C bus is busy\n", __func__);
			return -ETIMEDOUT;
		}
		schedule();
	}

	return 0;
}

static int i2c_imx_trx_complete(struct imx_i2c_struct *i2c_imx)
{
	wait_event_timeout(i2c_imx->queue, i2c_imx->i2csr & I2SR_IIF, HZ / 10);

	if (unlikely(!(i2c_imx->i2csr & I2SR_IIF))) {
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

static int i2c_imx_start(struct imx_i2c_struct *i2c_imx)
{
	unsigned int temp = 0;
	int result;

	dev_dbg(&i2c_imx->adapter.dev, "<%s>\n", __func__);

	clk_prepare_enable(i2c_imx->clk);
	writeb(i2c_imx->ifdr, i2c_imx->base + IMX_I2C_IFDR);
	/* Enable I2C controller */
	writeb(0, i2c_imx->base + IMX_I2C_I2SR);
	writeb(I2CR_IEN, i2c_imx->base + IMX_I2C_I2CR);

	/* Wait controller to be stable */
	udelay(50);

	/* Start I2C transaction */
	temp = readb(i2c_imx->base + IMX_I2C_I2CR);
	temp |= I2CR_MSTA;
	writeb(temp, i2c_imx->base + IMX_I2C_I2CR);
	result = i2c_imx_bus_busy(i2c_imx, 1);
	if (result)
		return result;
	i2c_imx->stopped = 0;

	temp |= I2CR_IIEN | I2CR_MTX | I2CR_TXAK;
	writeb(temp, i2c_imx->base + IMX_I2C_I2CR);
	return result;
}

static void i2c_imx_stop(struct imx_i2c_struct *i2c_imx)
{
	unsigned int temp = 0;

	if (!i2c_imx->stopped) {
		/* Stop I2C transaction */
		dev_dbg(&i2c_imx->adapter.dev, "<%s>\n", __func__);
		temp = readb(i2c_imx->base + IMX_I2C_I2CR);
		temp &= ~(I2CR_MSTA | I2CR_MTX);
		writeb(temp, i2c_imx->base + IMX_I2C_I2CR);
	}
	if (is_imx1_i2c(i2c_imx)) {
		/*
		 * This delay caused by an i.MXL hardware bug.
		 * If no (or too short) delay, no "STOP" bit will be generated.
		 */
		udelay(i2c_imx->disable_delay);
	}

	if (!i2c_imx->stopped) {
		i2c_imx_bus_busy(i2c_imx, 0);
		i2c_imx->stopped = 1;
	}

	/* Disable I2C controller */
	writeb(0, i2c_imx->base + IMX_I2C_I2CR);
	clk_disable_unprepare(i2c_imx->clk);
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

	/* Store divider value */
	i2c_imx->ifdr = i2c_clk_div[i][1];

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
	dev_dbg(&i2c_imx->adapter.dev, "<%s> I2C_CLK=%d, REQ DIV=%d\n",
		__func__, i2c_clk_rate, div);
	dev_dbg(&i2c_imx->adapter.dev, "<%s> IFDR[IC]=0x%x, REAL DIV=%d\n",
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
		wake_up(&i2c_imx->queue);
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
			/* It must generate STOP before read I2DR to prevent
			   controller from generating another clock cycle */
			dev_dbg(&i2c_imx->adapter.dev,
				"<%s> clear MSTA\n", __func__);
			temp = readb(i2c_imx->base + IMX_I2C_I2CR);
			temp &= ~(I2CR_MSTA | I2CR_MTX);
			writeb(temp, i2c_imx->base + IMX_I2C_I2CR);
			i2c_imx_bus_busy(i2c_imx, 0);
			i2c_imx->stopped = 1;
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

	/* Start I2C transfer */
	result = i2c_imx_start(i2c_imx);
	if (result)
		goto fail0;

	/* read/write data */
	for (i = 0; i < num; i++) {
		if (i) {
			dev_dbg(&i2c_imx->adapter.dev,
				"<%s> repeated start\n", __func__);
			temp = readb(i2c_imx->base + IMX_I2C_I2CR);
			temp |= I2CR_RSTA;
			writeb(temp, i2c_imx->base + IMX_I2C_I2CR);
			result =  i2c_imx_bus_busy(i2c_imx, 1);
			if (result)
				goto fail0;
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
		if (result)
			goto fail0;
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
	const struct of_device_id *of_id = of_match_device(i2c_imx_dt_ids,
							   &pdev->dev);
	struct imx_i2c_struct *i2c_imx;
	struct resource *res;
	struct imxi2c_platform_data *pdata = pdev->dev.platform_data;
	void __iomem *base;
	int irq, ret;
	u32 bitrate;

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

	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	i2c_imx = devm_kzalloc(&pdev->dev, sizeof(struct imx_i2c_struct),
				GFP_KERNEL);
	if (!i2c_imx) {
		dev_err(&pdev->dev, "can't allocate interface\n");
		return -ENOMEM;
	}

	if (of_id)
		pdev->id_entry = of_id->data;
	i2c_imx->devtype = pdev->id_entry->driver_data;

	/* Setup i2c_imx driver structure */
	strlcpy(i2c_imx->adapter.name, pdev->name, sizeof(i2c_imx->adapter.name));
	i2c_imx->adapter.owner		= THIS_MODULE;
	i2c_imx->adapter.algo		= &i2c_imx_algo;
	i2c_imx->adapter.dev.parent	= &pdev->dev;
	i2c_imx->adapter.nr 		= pdev->id;
	i2c_imx->adapter.dev.of_node	= pdev->dev.of_node;
	i2c_imx->base			= base;

	/* Get I2C clock */
	i2c_imx->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(i2c_imx->clk)) {
		dev_err(&pdev->dev, "can't get I2C clock\n");
		return PTR_ERR(i2c_imx->clk);
	}

	/* Request IRQ */
	ret = devm_request_irq(&pdev->dev, irq, i2c_imx_isr, 0,
				pdev->name, i2c_imx);
	if (ret) {
		dev_err(&pdev->dev, "can't claim irq %d\n", irq);
		return ret;
	}

	/* Init queue */
	init_waitqueue_head(&i2c_imx->queue);

	/* Set up adapter data */
	i2c_set_adapdata(&i2c_imx->adapter, i2c_imx);

	/* Set up clock divider */
	bitrate = IMX_I2C_BIT_RATE;
	ret = of_property_read_u32(pdev->dev.of_node,
				   "clock-frequency", &bitrate);
	if (ret < 0 && pdata && pdata->bitrate)
		bitrate = pdata->bitrate;
	i2c_imx_set_clk(i2c_imx, bitrate);

	/* Set up chip registers to defaults */
	writeb(0, i2c_imx->base + IMX_I2C_I2CR);
	writeb(0, i2c_imx->base + IMX_I2C_I2SR);

	/* Add I2C adapter */
	ret = i2c_add_numbered_adapter(&i2c_imx->adapter);
	if (ret < 0) {
		dev_err(&pdev->dev, "registration failed\n");
		return ret;
	}

	of_i2c_register_devices(&i2c_imx->adapter);

	/* Set up platform driver data */
	platform_set_drvdata(pdev, i2c_imx);

	dev_dbg(&i2c_imx->adapter.dev, "claimed irq %d\n", irq);
	dev_dbg(&i2c_imx->adapter.dev, "device resources from 0x%x to 0x%x\n",
		res->start, res->end);
	dev_dbg(&i2c_imx->adapter.dev, "allocated %d bytes at 0x%x\n",
		resource_size(res), res->start);
	dev_dbg(&i2c_imx->adapter.dev, "adapter name: \"%s\"\n",
		i2c_imx->adapter.name);
	dev_info(&i2c_imx->adapter.dev, "IMX I2C adapter registered\n");

	return 0;   /* Return OK */
}

static int __exit i2c_imx_remove(struct platform_device *pdev)
{
	struct imx_i2c_struct *i2c_imx = platform_get_drvdata(pdev);

	/* remove adapter */
	dev_dbg(&i2c_imx->adapter.dev, "adapter removed\n");
	i2c_del_adapter(&i2c_imx->adapter);

	/* setup chip registers to defaults */
	writeb(0, i2c_imx->base + IMX_I2C_IADR);
	writeb(0, i2c_imx->base + IMX_I2C_IFDR);
	writeb(0, i2c_imx->base + IMX_I2C_I2CR);
	writeb(0, i2c_imx->base + IMX_I2C_I2SR);

	return 0;
}

static struct platform_driver i2c_imx_driver = {
	.remove		= __exit_p(i2c_imx_remove),
	.driver	= {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = i2c_imx_dt_ids,
	},
	.id_table	= imx_i2c_devtype,
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
