/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012 MIPS Technologies, Inc.  All rights reserved.
 */
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/slab.h>

#define PIC32_I2CxCON		0x0000
#define PIC32_I2CxCONCLR	0x0004
#define PIC32_I2CxCONSET	0x0008
#define PIC32_I2CxCONINV	0x000C
#define	 I2CCON_ON		(1<<15)
#define	 I2CCON_FRZ		(1<<14)
#define	 I2CCON_SIDL		(1<<13)
#define	 I2CCON_SCLREL		(1<<12)
#define	 I2CCON_STRICT		(1<<11)
#define	 I2CCON_A10M		(1<<10)
#define	 I2CCON_DISSLW		(1<<9)
#define	 I2CCON_SMEN		(1<<8)
#define	 I2CCON_GCEN		(1<<7)
#define	 I2CCON_STREN		(1<<6)
#define	 I2CCON_ACKDT		(1<<5)
#define	 I2CCON_ACKEN		(1<<4)
#define	 I2CCON_RCEN		(1<<3)
#define	 I2CCON_PEN		(1<<2)
#define	 I2CCON_RSEN		(1<<1)
#define	 I2CCON_SEN		(1<<0)

#define PIC32_I2CxSTAT		0x0010
#define PIC32_I2CxSTATCLR	0x0014
#define PIC32_I2CxSTATSET	0x0018
#define PIC32_I2CxSTATINV	0x001C
#define	 I2CSTAT_ACKSTAT	(1<<15)
#define	 I2CSTAT_TRSTAT		(1<<14)
#define	 I2CSTAT_BCL		(1<<10)
#define	 I2CSTAT_GCSTAT		(1<<9)
#define	 I2CSTAT_ADD10		(1<<8)
#define	 I2CSTAT_IWCOL		(1<<7)
#define	 I2CSTAT_I2COV		(1<<6)
#define	 I2CSTAT_DA		(1<<5)
#define	 I2CSTAT_P		(1<<4)
#define	 I2CSTAT_S		(1<<3)
#define	 I2CSTAT_RW		(1<<2)
#define	 I2CSTAT_RBF		(1<<1)
#define	 I2CSTAT_TBF		(1<<0)

#define PIC32_I2CxADD		0x0020
#define PIC32_I2CxADDCLR	0x0024
#define PIC32_I2CxADDSET	0x0028
#define PIC32_I2CxADDINV	0x002C
#define PIC32_I2CxMSK		0x0030
#define PIC32_I2CxMSKCLR	0x0034
#define PIC32_I2CxMSKSET	0x0038
#define PIC32_I2CxMSKINV	0x003C
#define PIC32_I2CxBRG		0x0040
#define PIC32_I2CxBRGCLR	0x0044
#define PIC32_I2CxBRGSET	0x0048
#define PIC32_I2CxBRGINV	0x004C
#define PIC32_I2CxTRN		0x0050
#define PIC32_I2CxTRNCLR	0x0054
#define PIC32_I2CxTRNSET	0x0058
#define PIC32_I2CxTRNINV	0x005C
#define PIC32_I2CxRCV		0x0060

struct i2c_platform_data {
	u32	base;
	struct i2c_adapter adap;
	u32	xfer_timeout;
	u32	ack_timeout;
	u32	ctl_timeout;
};

extern u32 pic32_bus_readl(u32 reg);
extern void pic32_bus_writel(u32 val, u32 reg);

static inline void
StartI2C(struct i2c_platform_data *adap)
{
	pr_debug("StartI2C\n");
	pic32_bus_writel(I2CCON_SEN, adap->base + PIC32_I2CxCONSET);
}

static inline void
StopI2C(struct i2c_platform_data *adap)
{
	pr_debug("StopI2C\n");
	pic32_bus_writel(I2CCON_PEN, adap->base + PIC32_I2CxCONSET);
}

static inline void
AckI2C(struct i2c_platform_data *adap)
{
	pr_debug("AckI2C\n");
	pic32_bus_writel(I2CCON_ACKDT, adap->base + PIC32_I2CxCONCLR);
	pic32_bus_writel(I2CCON_ACKEN, adap->base + PIC32_I2CxCONSET);
}

static inline void
NotAckI2C(struct i2c_platform_data *adap)
{
	pr_debug("NakI2C\n");
	pic32_bus_writel(I2CCON_ACKDT, adap->base + PIC32_I2CxCONSET);
	pic32_bus_writel(I2CCON_ACKEN, adap->base + PIC32_I2CxCONSET);
}

static inline int
IdleI2C(struct i2c_platform_data *adap)
{
	int i;

	pr_debug("IdleI2C\n");
	for (i = 0; i < adap->ctl_timeout; i++) {
		if (((pic32_bus_readl(adap->base + PIC32_I2CxCON) &
		     (I2CCON_ACKEN | I2CCON_RCEN | I2CCON_PEN | I2CCON_RSEN |
		      I2CCON_SEN)) == 0) &&
		    ((pic32_bus_readl(adap->base + PIC32_I2CxSTAT) &
		     (I2CSTAT_TRSTAT)) == 0))
			return 0;
		udelay(1);
	}
	return -ETIMEDOUT;
}

static inline u32
MasterWriteI2C(struct i2c_platform_data *adap, u32 byte)
{
	pr_debug("MasterWriteI2C\n");

	pic32_bus_writel(byte, adap->base + PIC32_I2CxTRN);

	return pic32_bus_readl(adap->base + PIC32_I2CxSTAT) & I2CSTAT_IWCOL;
}

static inline u32
MasterReadI2C(struct i2c_platform_data *adap)
{
	pr_debug("MasterReadI2C\n");

	pic32_bus_writel(I2CCON_RCEN, adap->base + PIC32_I2CxCONSET);

	while (pic32_bus_readl(adap->base + PIC32_I2CxCON) & I2CCON_RCEN)
		;

	pic32_bus_writel(I2CSTAT_I2COV, adap->base + PIC32_I2CxSTATCLR);

	return pic32_bus_readl(adap->base + PIC32_I2CxRCV);
}

static int
do_address(struct i2c_platform_data *adap, unsigned int addr, int rd)
{
	pr_debug("doaddress\n");

	IdleI2C(adap);
	StartI2C(adap);
	IdleI2C(adap);

	addr <<= 1;
	if (rd)
		addr |= 1;

	if (MasterWriteI2C(adap, addr))
		return -EIO;
	IdleI2C(adap);
	if (pic32_bus_readl(adap->base + PIC32_I2CxSTAT) & I2CSTAT_ACKSTAT)
		return -EIO;
	return 0;
}

static int
i2c_read(struct i2c_platform_data *adap, unsigned char *buf,
		    unsigned int len)
{
	int	i;
	u32	data;

	pr_debug("i2c_read\n");

	i = 0;
	while (i < len) {
		data = MasterReadI2C(adap);
		buf[i++] = data;
		if (i < len)
			AckI2C(adap);
		else
			NotAckI2C(adap);
	}

	StopI2C(adap);
	IdleI2C(adap);
	return 0;
}

static int
i2c_write(struct i2c_platform_data *adap, unsigned char *buf,
		     unsigned int len)
{
	int	i;
	u32	data;

	pr_debug("i2c_write\n");

	i = 0;
	while (i < len) {
		data = buf[i];
		if (MasterWriteI2C(adap, data))
			return -EIO;
		IdleI2C(adap);
		if (pic32_bus_readl(adap->base + PIC32_I2CxSTAT) &
		    I2CSTAT_ACKSTAT)
			return -EIO;
		i++;
	}

	StopI2C(adap);
	IdleI2C(adap);
	return 0;
}

static int
platform_xfer(struct i2c_adapter *i2c_adap, struct i2c_msg *msgs, int num)
{
	struct i2c_platform_data *adap = i2c_adap->algo_data;
	struct i2c_msg *p;
	int i, err = 0;

	pr_debug("platform_xfer\n");
	for (i = 0; i < num; i++) {
#define __BUFSIZE 80
		int ii;
		static char buf[__BUFSIZE];
		char *b = buf;

		p = &msgs[i];
		b += sprintf(buf, " [%d bytes]", p->len);
		if ((p->flags & I2C_M_RD) == 0) {
			for (ii = 0; ii < p->len; ii++) {
				if (b < &buf[__BUFSIZE-4]) {
					b += sprintf(b, " %02x", p->buf[ii]);
				} else {
					strcat(b, "...");
					break;
				}
			}
		}
		pr_debug("xfer%d: DevAddr: %04x Op:%s Data:%s\n", i, p->addr,
			 (p->flags & I2C_M_RD) ? "Rd" : "Wr", buf);
	}


	for (i = 0; !err && i < num; i++) {
		p = &msgs[i];
		err = do_address(adap, p->addr, p->flags & I2C_M_RD);
		if (err || !p->len)
			continue;
		if (p->flags & I2C_M_RD)
			err = i2c_read(adap, p->buf, p->len);
		else
			err = i2c_write(adap, p->buf, p->len);
	}

	/* Return the number of messages processed, or the error code. */
	if (err == 0)
		err = num;

	return err;
}

static u32
platform_func(struct i2c_adapter *adap)
{
	pr_debug("platform_algo\n");
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm platform_algo = {
	.master_xfer	= platform_xfer,
	.functionality	= platform_func,
};

static void i2c_platform_setup(struct i2c_platform_data *priv)
{
	pr_debug("i2c_platform_setup\n");

	pic32_bus_writel(500, priv->base + PIC32_I2CxBRG);
	pic32_bus_writel(I2CCON_ON, priv->base + PIC32_I2CxCONCLR);
	pic32_bus_writel(I2CCON_ON, priv->base + PIC32_I2CxCONSET);
	pic32_bus_writel((I2CSTAT_BCL | I2CSTAT_IWCOL),
		(priv->base + PIC32_I2CxSTATCLR));
}

static void i2c_platform_disable(struct i2c_platform_data *priv)
{
	pr_debug("i2c_platform_disable\n");
}

static int i2c_platform_probe(struct platform_device *pdev)
{
	struct i2c_platform_data *priv;
	struct resource *r;
	int ret;

	pr_debug("i2c_platform_probe\n");
	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!r) {
		ret = -ENODEV;
		goto out;
	}

	priv = kzalloc(sizeof(struct i2c_platform_data), GFP_KERNEL);
	if (!priv) {
		ret = -ENOMEM;
		goto out;
	}

	/* FIXME: need to allocate resource in PIC32 space */
#if 0
	priv->base = bus_request_region(r->start, resource_size(r),
					  pdev->name);
#else
	priv->base = r->start;
#endif
	if (!priv->base) {
		ret = -EBUSY;
		goto out_mem;
	}

	priv->xfer_timeout = 200;
	priv->ack_timeout = 200;
	priv->ctl_timeout = 200;

	priv->adap.nr = pdev->id;
	priv->adap.algo = &platform_algo;
	priv->adap.algo_data = priv;
	priv->adap.dev.parent = &pdev->dev;
	strlcpy(priv->adap.name, "PIC32 I2C", sizeof(priv->adap.name));

	i2c_platform_setup(priv);

	ret = i2c_add_numbered_adapter(&priv->adap);
	if (ret == 0) {
		platform_set_drvdata(pdev, priv);
		return 0;
	}

	i2c_platform_disable(priv);

out_mem:
	kfree(priv);
out:
	return ret;
}

static int i2c_platform_remove(struct platform_device *pdev)
{
	struct i2c_platform_data *priv = platform_get_drvdata(pdev);

	pr_debug("i2c_platform_remove\n");
	platform_set_drvdata(pdev, NULL);
	i2c_del_adapter(&priv->adap);
	i2c_platform_disable(priv);
	kfree(priv);
	return 0;
}

#ifdef CONFIG_PM
static int
i2c_platform_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct i2c_platform_data *priv = platform_get_drvdata(pdev);

	dev_dbg(&pdev->dev, "i2c_platform_disable\n");
	i2c_platform_disable(priv);

	return 0;
}

static int
i2c_platform_resume(struct platform_device *pdev)
{
	struct i2c_platform_data *priv = platform_get_drvdata(pdev);

	dev_dbg(&pdev->dev, "i2c_platform_setup\n");
	i2c_platform_setup(priv);

	return 0;
}
#else
#define i2c_platform_suspend	NULL
#define i2c_platform_resume	NULL
#endif

static struct platform_driver i2c_platform_driver = {
	.driver = {
		.name	= "i2c_pic32",
		.owner	= THIS_MODULE,
	},
	.probe		= i2c_platform_probe,
	.remove		= i2c_platform_remove,
	.suspend	= i2c_platform_suspend,
	.resume		= i2c_platform_resume,
};

static int __init
i2c_platform_init(void)
{
	pr_debug("i2c_platform_init\n");
	return platform_driver_register(&i2c_platform_driver);
}

static void __exit
i2c_platform_exit(void)
{
	pr_debug("i2c_platform_exit\n");
	platform_driver_unregister(&i2c_platform_driver);
}

MODULE_AUTHOR("Chris Dearman, MIPS Technologies INC.");
MODULE_DESCRIPTION("PIC32 I2C driver");
MODULE_LICENSE("GPL");

module_init(i2c_platform_init);
module_exit(i2c_platform_exit);
