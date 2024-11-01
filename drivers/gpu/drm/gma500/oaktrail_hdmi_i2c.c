/*
 * Copyright © 2010 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *	Li Peng <peng.li@intel.com>
 */

#include <linux/export.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include "psb_drv.h"

#define HDMI_READ(reg)		readl(hdmi_dev->regs + (reg))
#define HDMI_WRITE(reg, val)	writel(val, hdmi_dev->regs + (reg))

#define HDMI_HCR	0x1000
#define HCR_DETECT_HDP		(1 << 6)
#define HCR_ENABLE_HDCP		(1 << 5)
#define HCR_ENABLE_AUDIO	(1 << 2)
#define HCR_ENABLE_PIXEL	(1 << 1)
#define HCR_ENABLE_TMDS		(1 << 0)
#define HDMI_HICR	0x1004
#define HDMI_INTR_I2C_ERROR	(1 << 4)
#define HDMI_INTR_I2C_FULL	(1 << 3)
#define HDMI_INTR_I2C_DONE	(1 << 2)
#define HDMI_INTR_HPD		(1 << 0)
#define HDMI_HSR	0x1008
#define HDMI_HISR	0x100C
#define HDMI_HI2CRDB0	0x1200
#define HDMI_HI2CHCR	0x1240
#define HI2C_HDCP_WRITE		(0 << 2)
#define HI2C_HDCP_RI_READ	(1 << 2)
#define HI2C_HDCP_READ		(2 << 2)
#define HI2C_EDID_READ		(3 << 2)
#define HI2C_READ_CONTINUE	(1 << 1)
#define HI2C_ENABLE_TRANSACTION	(1 << 0)

#define HDMI_ICRH	0x1100
#define HDMI_HI2CTDR0	0x1244
#define HDMI_HI2CTDR1	0x1248

#define I2C_STAT_INIT		0
#define I2C_READ_DONE		1
#define I2C_TRANSACTION_DONE	2

struct hdmi_i2c_dev {
	struct i2c_adapter *adap;
	struct mutex i2c_lock;
	struct completion complete;
	int status;
	struct i2c_msg *msg;
	int buf_offset;
};

static void hdmi_i2c_irq_enable(struct oaktrail_hdmi_dev *hdmi_dev)
{
	u32 temp;

	temp = HDMI_READ(HDMI_HICR);
	temp |= (HDMI_INTR_I2C_ERROR | HDMI_INTR_I2C_FULL | HDMI_INTR_I2C_DONE);
	HDMI_WRITE(HDMI_HICR, temp);
	HDMI_READ(HDMI_HICR);
}

static void hdmi_i2c_irq_disable(struct oaktrail_hdmi_dev *hdmi_dev)
{
	HDMI_WRITE(HDMI_HICR, 0x0);
	HDMI_READ(HDMI_HICR);
}

static int xfer_read(struct i2c_adapter *adap, struct i2c_msg *pmsg)
{
	struct oaktrail_hdmi_dev *hdmi_dev = i2c_get_adapdata(adap);
	struct hdmi_i2c_dev *i2c_dev = hdmi_dev->i2c_dev;
	u32 temp;

	i2c_dev->status = I2C_STAT_INIT;
	i2c_dev->msg = pmsg;
	i2c_dev->buf_offset = 0;
	reinit_completion(&i2c_dev->complete);

	/* Enable I2C transaction */
	temp = ((pmsg->len) << 20) | HI2C_EDID_READ | HI2C_ENABLE_TRANSACTION;
	HDMI_WRITE(HDMI_HI2CHCR, temp);
	HDMI_READ(HDMI_HI2CHCR);

	while (i2c_dev->status != I2C_TRANSACTION_DONE)
		wait_for_completion_interruptible_timeout(&i2c_dev->complete,
								10 * HZ);

	return 0;
}

static int xfer_write(struct i2c_adapter *adap, struct i2c_msg *pmsg)
{
	/*
	 * XXX: i2c write seems isn't useful for EDID probe, don't do anything
	 */
	return 0;
}

static int oaktrail_hdmi_i2c_access(struct i2c_adapter *adap,
				struct i2c_msg *pmsg,
				int num)
{
	struct oaktrail_hdmi_dev *hdmi_dev = i2c_get_adapdata(adap);
	struct hdmi_i2c_dev *i2c_dev = hdmi_dev->i2c_dev;
	int i;

	mutex_lock(&i2c_dev->i2c_lock);

	/* Enable i2c unit */
	HDMI_WRITE(HDMI_ICRH, 0x00008760);

	/* Enable irq */
	hdmi_i2c_irq_enable(hdmi_dev);
	for (i = 0; i < num; i++) {
		if (pmsg->len && pmsg->buf) {
			if (pmsg->flags & I2C_M_RD)
				xfer_read(adap, pmsg);
			else
				xfer_write(adap, pmsg);
		}
		pmsg++;         /* next message */
	}

	/* Disable irq */
	hdmi_i2c_irq_disable(hdmi_dev);

	mutex_unlock(&i2c_dev->i2c_lock);

	return i;
}

static u32 oaktrail_hdmi_i2c_func(struct i2c_adapter *adapter)
{
	return I2C_FUNC_I2C | I2C_FUNC_10BIT_ADDR;
}

static const struct i2c_algorithm oaktrail_hdmi_i2c_algorithm = {
	.master_xfer	= oaktrail_hdmi_i2c_access,
	.functionality  = oaktrail_hdmi_i2c_func,
};

static struct i2c_adapter oaktrail_hdmi_i2c_adapter = {
	.name		= "oaktrail_hdmi_i2c",
	.nr		= 3,
	.owner		= THIS_MODULE,
	.class		= I2C_CLASS_DDC,
	.algo		= &oaktrail_hdmi_i2c_algorithm,
};

static void hdmi_i2c_read(struct oaktrail_hdmi_dev *hdmi_dev)
{
	struct hdmi_i2c_dev *i2c_dev = hdmi_dev->i2c_dev;
	struct i2c_msg *msg = i2c_dev->msg;
	u8 *buf = msg->buf;
	u32 temp;
	int i, offset;

	offset = i2c_dev->buf_offset;
	for (i = 0; i < 0x10; i++) {
		temp = HDMI_READ(HDMI_HI2CRDB0 + (i * 4));
		memcpy(buf + (offset + i * 4), &temp, 4);
	}
	i2c_dev->buf_offset += (0x10 * 4);

	/* clearing read buffer full intr */
	temp = HDMI_READ(HDMI_HISR);
	HDMI_WRITE(HDMI_HISR, temp | HDMI_INTR_I2C_FULL);
	HDMI_READ(HDMI_HISR);

	/* continue read transaction */
	temp = HDMI_READ(HDMI_HI2CHCR);
	HDMI_WRITE(HDMI_HI2CHCR, temp | HI2C_READ_CONTINUE);
	HDMI_READ(HDMI_HI2CHCR);

	i2c_dev->status = I2C_READ_DONE;
	return;
}

static void hdmi_i2c_transaction_done(struct oaktrail_hdmi_dev *hdmi_dev)
{
	struct hdmi_i2c_dev *i2c_dev = hdmi_dev->i2c_dev;
	u32 temp;

	/* clear transaction done intr */
	temp = HDMI_READ(HDMI_HISR);
	HDMI_WRITE(HDMI_HISR, temp | HDMI_INTR_I2C_DONE);
	HDMI_READ(HDMI_HISR);


	temp = HDMI_READ(HDMI_HI2CHCR);
	HDMI_WRITE(HDMI_HI2CHCR, temp & ~HI2C_ENABLE_TRANSACTION);
	HDMI_READ(HDMI_HI2CHCR);

	i2c_dev->status = I2C_TRANSACTION_DONE;
	return;
}

static irqreturn_t oaktrail_hdmi_i2c_handler(int this_irq, void *dev)
{
	struct oaktrail_hdmi_dev *hdmi_dev = dev;
	struct hdmi_i2c_dev *i2c_dev = hdmi_dev->i2c_dev;
	u32 stat;

	stat = HDMI_READ(HDMI_HISR);

	if (stat & HDMI_INTR_HPD) {
		HDMI_WRITE(HDMI_HISR, stat | HDMI_INTR_HPD);
		HDMI_READ(HDMI_HISR);
	}

	if (stat & HDMI_INTR_I2C_FULL)
		hdmi_i2c_read(hdmi_dev);

	if (stat & HDMI_INTR_I2C_DONE)
		hdmi_i2c_transaction_done(hdmi_dev);

	complete(&i2c_dev->complete);

	return IRQ_HANDLED;
}

/*
 * choose alternate function 2 of GPIO pin 52, 53,
 * which is used by HDMI I2C logic
 */
static void oaktrail_hdmi_i2c_gpio_fix(void)
{
	void __iomem *base;
	unsigned int gpio_base = 0xff12c000;
	int gpio_len = 0x1000;
	u32 temp;

	base = ioremap((resource_size_t)gpio_base, gpio_len);
	if (base == NULL) {
		DRM_ERROR("gpio ioremap fail\n");
		return;
	}

	temp = readl(base + 0x44);
	DRM_DEBUG_DRIVER("old gpio val %x\n", temp);
	writel((temp | 0x00000a00), (base +  0x44));
	temp = readl(base + 0x44);
	DRM_DEBUG_DRIVER("new gpio val %x\n", temp);

	iounmap(base);
}

int oaktrail_hdmi_i2c_init(struct pci_dev *dev)
{
	struct oaktrail_hdmi_dev *hdmi_dev;
	struct hdmi_i2c_dev *i2c_dev;
	int ret;

	hdmi_dev = pci_get_drvdata(dev);

	i2c_dev = kzalloc(sizeof(struct hdmi_i2c_dev), GFP_KERNEL);
	if (!i2c_dev)
		return -ENOMEM;

	i2c_dev->adap = &oaktrail_hdmi_i2c_adapter;
	i2c_dev->status = I2C_STAT_INIT;
	init_completion(&i2c_dev->complete);
	mutex_init(&i2c_dev->i2c_lock);
	i2c_set_adapdata(&oaktrail_hdmi_i2c_adapter, hdmi_dev);
	hdmi_dev->i2c_dev = i2c_dev;

	/* Enable HDMI I2C function on gpio */
	oaktrail_hdmi_i2c_gpio_fix();

	/* request irq */
	ret = request_irq(dev->irq, oaktrail_hdmi_i2c_handler, IRQF_SHARED,
			  oaktrail_hdmi_i2c_adapter.name, hdmi_dev);
	if (ret) {
		DRM_ERROR("Failed to request IRQ for I2C controller\n");
		goto free_dev;
	}

	/* Adapter registration */
	ret = i2c_add_numbered_adapter(&oaktrail_hdmi_i2c_adapter);
	if (ret) {
		DRM_ERROR("Failed to add I2C adapter\n");
		goto free_irq;
	}

	return 0;

free_irq:
	free_irq(dev->irq, hdmi_dev);
free_dev:
	kfree(i2c_dev);

	return ret;
}

void oaktrail_hdmi_i2c_exit(struct pci_dev *dev)
{
	struct oaktrail_hdmi_dev *hdmi_dev;
	struct hdmi_i2c_dev *i2c_dev;

	hdmi_dev = pci_get_drvdata(dev);
	i2c_del_adapter(&oaktrail_hdmi_i2c_adapter);

	i2c_dev = hdmi_dev->i2c_dev;
	kfree(i2c_dev);
	free_irq(dev->irq, hdmi_dev);
}
