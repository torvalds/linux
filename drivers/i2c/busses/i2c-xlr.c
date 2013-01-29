/*
 * Copyright 2011, Netlogic Microsystems Inc.
 * Copyright 2004, Matt Porter <mporter@kernel.crashing.org>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/platform_device.h>

/* XLR I2C REGISTERS */
#define XLR_I2C_CFG		0x00
#define XLR_I2C_CLKDIV		0x01
#define XLR_I2C_DEVADDR		0x02
#define XLR_I2C_ADDR		0x03
#define XLR_I2C_DATAOUT		0x04
#define XLR_I2C_DATAIN		0x05
#define XLR_I2C_STATUS		0x06
#define XLR_I2C_STARTXFR	0x07
#define XLR_I2C_BYTECNT		0x08
#define XLR_I2C_HDSTATIM	0x09

/* XLR I2C REGISTERS FLAGS */
#define XLR_I2C_BUS_BUSY	0x01
#define XLR_I2C_SDOEMPTY	0x02
#define XLR_I2C_RXRDY		0x04
#define XLR_I2C_ACK_ERR		0x08
#define XLR_I2C_ARB_STARTERR	0x30

/* Register Values */
#define XLR_I2C_CFG_ADDR	0xF8
#define XLR_I2C_CFG_NOADDR	0xFA
#define XLR_I2C_STARTXFR_ND	0x02    /* No Data */
#define XLR_I2C_STARTXFR_RD	0x01    /* Read */
#define XLR_I2C_STARTXFR_WR	0x00    /* Write */

#define XLR_I2C_TIMEOUT		10	/* timeout per byte in msec */

/*
 * On XLR/XLS, we need to use __raw_ IO to read the I2C registers
 * because they are in the big-endian MMIO area on the SoC.
 *
 * The readl/writel implementation on XLR/XLS byteswaps, because
 * those are for its little-endian PCI space (see arch/mips/Kconfig).
 */
static inline void xlr_i2c_wreg(u32 __iomem *base, unsigned int reg, u32 val)
{
	__raw_writel(val, base + reg);
}

static inline u32 xlr_i2c_rdreg(u32 __iomem *base, unsigned int reg)
{
	return __raw_readl(base + reg);
}

struct xlr_i2c_private {
	struct i2c_adapter adap;
	u32 __iomem *iobase;
};

static int xlr_i2c_tx(struct xlr_i2c_private *priv,  u16 len,
	u8 *buf, u16 addr)
{
	struct i2c_adapter *adap = &priv->adap;
	unsigned long timeout, stoptime, checktime;
	u32 i2c_status;
	int pos, timedout;
	u8 offset, byte;

	offset = buf[0];
	xlr_i2c_wreg(priv->iobase, XLR_I2C_ADDR, offset);
	xlr_i2c_wreg(priv->iobase, XLR_I2C_DEVADDR, addr);
	xlr_i2c_wreg(priv->iobase, XLR_I2C_CFG, XLR_I2C_CFG_ADDR);
	xlr_i2c_wreg(priv->iobase, XLR_I2C_BYTECNT, len - 1);

	timeout = msecs_to_jiffies(XLR_I2C_TIMEOUT);
	stoptime = jiffies + timeout;
	timedout = 0;
	pos = 1;
retry:
	if (len == 1) {
		xlr_i2c_wreg(priv->iobase, XLR_I2C_STARTXFR,
				XLR_I2C_STARTXFR_ND);
	} else {
		xlr_i2c_wreg(priv->iobase, XLR_I2C_DATAOUT, buf[pos]);
		xlr_i2c_wreg(priv->iobase, XLR_I2C_STARTXFR,
				XLR_I2C_STARTXFR_WR);
	}

	while (!timedout) {
		checktime = jiffies;
		i2c_status = xlr_i2c_rdreg(priv->iobase, XLR_I2C_STATUS);

		if (i2c_status & XLR_I2C_SDOEMPTY) {
			pos++;
			/* need to do a empty dataout after the last byte */
			byte = (pos < len) ? buf[pos] : 0;
			xlr_i2c_wreg(priv->iobase, XLR_I2C_DATAOUT, byte);

			/* reset timeout on successful xmit */
			stoptime = jiffies + timeout;
		}
		timedout = time_after(checktime, stoptime);

		if (i2c_status & XLR_I2C_ARB_STARTERR) {
			if (timedout)
				break;
			goto retry;
		}

		if (i2c_status & XLR_I2C_ACK_ERR)
			return -EIO;

		if ((i2c_status & XLR_I2C_BUS_BUSY) == 0 && pos >= len)
			return 0;
	}
	dev_err(&adap->dev, "I2C transmit timeout\n");
	return -ETIMEDOUT;
}

static int xlr_i2c_rx(struct xlr_i2c_private *priv, u16 len, u8 *buf, u16 addr)
{
	struct i2c_adapter *adap = &priv->adap;
	u32 i2c_status;
	unsigned long timeout, stoptime, checktime;
	int nbytes, timedout;
	u8 byte;

	xlr_i2c_wreg(priv->iobase, XLR_I2C_CFG, XLR_I2C_CFG_NOADDR);
	xlr_i2c_wreg(priv->iobase, XLR_I2C_BYTECNT, len);
	xlr_i2c_wreg(priv->iobase, XLR_I2C_DEVADDR, addr);

	timeout = msecs_to_jiffies(XLR_I2C_TIMEOUT);
	stoptime = jiffies + timeout;
	timedout = 0;
	nbytes = 0;
retry:
	xlr_i2c_wreg(priv->iobase, XLR_I2C_STARTXFR, XLR_I2C_STARTXFR_RD);

	while (!timedout) {
		checktime = jiffies;
		i2c_status = xlr_i2c_rdreg(priv->iobase, XLR_I2C_STATUS);
		if (i2c_status & XLR_I2C_RXRDY) {
			if (nbytes > len)
				return -EIO;	/* should not happen */

			/* we need to do a dummy datain when nbytes == len */
			byte = xlr_i2c_rdreg(priv->iobase, XLR_I2C_DATAIN);
			if (nbytes < len)
				buf[nbytes] = byte;
			nbytes++;

			/* reset timeout on successful read */
			stoptime = jiffies + timeout;
		}

		timedout = time_after(checktime, stoptime);
		if (i2c_status & XLR_I2C_ARB_STARTERR) {
			if (timedout)
				break;
			goto retry;
		}

		if (i2c_status & XLR_I2C_ACK_ERR)
			return -EIO;

		if ((i2c_status & XLR_I2C_BUS_BUSY) == 0)
			return 0;
	}

	dev_err(&adap->dev, "I2C receive timeout\n");
	return -ETIMEDOUT;
}

static int xlr_i2c_xfer(struct i2c_adapter *adap,
	struct i2c_msg *msgs, int num)
{
	struct i2c_msg *msg;
	int i;
	int ret = 0;
	struct xlr_i2c_private *priv = i2c_get_adapdata(adap);

	for (i = 0; ret == 0 && i < num; i++) {
		msg = &msgs[i];
		if (msg->flags & I2C_M_RD)
			ret = xlr_i2c_rx(priv, msg->len, &msg->buf[0],
					msg->addr);
		else
			ret = xlr_i2c_tx(priv, msg->len, &msg->buf[0],
					msg->addr);
	}

	return (ret != 0) ? ret : num;
}

static u32 xlr_func(struct i2c_adapter *adap)
{
	/* Emulate SMBUS over I2C */
	return I2C_FUNC_SMBUS_EMUL | I2C_FUNC_I2C;
}

static struct i2c_algorithm xlr_i2c_algo = {
	.master_xfer	= xlr_i2c_xfer,
	.functionality	= xlr_func,
};

static int xlr_i2c_probe(struct platform_device *pdev)
{
	struct xlr_i2c_private  *priv;
	struct resource *res;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->iobase = devm_request_and_ioremap(&pdev->dev, res);
	if (!priv->iobase) {
		dev_err(&pdev->dev, "devm_request_and_ioremap failed\n");
		return -EBUSY;
	}

	priv->adap.dev.parent = &pdev->dev;
	priv->adap.owner	= THIS_MODULE;
	priv->adap.algo_data	= priv;
	priv->adap.algo		= &xlr_i2c_algo;
	priv->adap.nr		= pdev->id;
	priv->adap.class	= I2C_CLASS_HWMON;
	snprintf(priv->adap.name, sizeof(priv->adap.name), "xlr-i2c");

	i2c_set_adapdata(&priv->adap, priv);
	ret = i2c_add_numbered_adapter(&priv->adap);
	if (ret < 0) {
		dev_err(&priv->adap.dev, "Failed to add i2c bus.\n");
		return ret;
	}

	platform_set_drvdata(pdev, priv);
	dev_info(&priv->adap.dev, "Added I2C Bus.\n");
	return 0;
}

static int xlr_i2c_remove(struct platform_device *pdev)
{
	struct xlr_i2c_private *priv;

	priv = platform_get_drvdata(pdev);
	i2c_del_adapter(&priv->adap);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static struct platform_driver xlr_i2c_driver = {
	.probe  = xlr_i2c_probe,
	.remove = xlr_i2c_remove,
	.driver = {
		.name   = "xlr-i2cbus",
		.owner  = THIS_MODULE,
	},
};

module_platform_driver(xlr_i2c_driver);

MODULE_AUTHOR("Ganesan Ramalingam <ganesanr@netlogicmicro.com>");
MODULE_DESCRIPTION("XLR/XLS SoC I2C Controller driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:xlr-i2cbus");
