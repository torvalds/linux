/* Copyright (c) 2011-2012, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/clk.h>
#include <linux/seq_file.h>
#include <linux/coresight.h>
#include <linux/amba/bus.h>

#include "coresight-priv.h"

#define ETB_RAM_DEPTH_REG	0x004
#define ETB_STATUS_REG		0x00c
#define ETB_RAM_READ_DATA_REG	0x010
#define ETB_RAM_READ_POINTER	0x014
#define ETB_RAM_WRITE_POINTER	0x018
#define ETB_TRG			0x01c
#define ETB_CTL_REG		0x020
#define ETB_RWD_REG		0x024
#define ETB_FFSR		0x300
#define ETB_FFCR		0x304
#define ETB_ITMISCOP0		0xee0
#define ETB_ITTRFLINACK		0xee4
#define ETB_ITTRFLIN		0xee8
#define ETB_ITATBDATA0		0xeeC
#define ETB_ITATBCTR2		0xef0
#define ETB_ITATBCTR1		0xef4
#define ETB_ITATBCTR0		0xef8

/* register description */
/* STS - 0x00C */
#define ETB_STATUS_RAM_FULL	BIT(0)
/* CTL - 0x020 */
#define ETB_CTL_CAPT_EN		BIT(0)
/* FFCR - 0x304 */
#define ETB_FFCR_EN_FTC		BIT(0)
#define ETB_FFCR_FON_MAN	BIT(6)
#define ETB_FFCR_STOP_FI	BIT(12)
#define ETB_FFCR_STOP_TRIGGER	BIT(13)

#define ETB_FFCR_BIT		6
#define ETB_FFSR_BIT		1
#define ETB_FRAME_SIZE_WORDS	4

/**
 * struct etb_drvdata - specifics associated to an ETB component
 * @base:	memory mapped base address for this component.
 * @dev:	the device entity associated to this component.
 * @csdev:	component vitals needed by the framework.
 * @miscdev:	specifics to handle "/dev/xyz.etb" entry.
 * @clk:	the clock this component is associated to.
 * @spinlock:	only one at a time pls.
 * @in_use:	synchronise user space access to etb buffer.
 * @buf:	area of memory where ETB buffer content gets sent.
 * @buffer_depth: size of @buf.
 * @enable:	this ETB is being used.
 * @trigger_cntr: amount of words to store after a trigger.
 */
struct etb_drvdata {
	void __iomem		*base;
	struct device		*dev;
	struct coresight_device	*csdev;
	struct miscdevice	miscdev;
	struct clk		*clk;
	spinlock_t		spinlock;
	atomic_t		in_use;
	u8			*buf;
	u32			buffer_depth;
	bool			enable;
	u32			trigger_cntr;
};

static unsigned int etb_get_buffer_depth(struct etb_drvdata *drvdata)
{
	int ret;
	u32 depth = 0;

	ret = clk_prepare_enable(drvdata->clk);
	if (ret)
		return ret;

	/* RO registers don't need locking */
	depth = readl_relaxed(drvdata->base + ETB_RAM_DEPTH_REG);

	clk_disable_unprepare(drvdata->clk);
	return depth;
}

static void etb_enable_hw(struct etb_drvdata *drvdata)
{
	int i;
	u32 depth;

	CS_UNLOCK(drvdata->base);

	depth = drvdata->buffer_depth;
	/* reset write RAM pointer address */
	writel_relaxed(0x0, drvdata->base + ETB_RAM_WRITE_POINTER);
	/* clear entire RAM buffer */
	for (i = 0; i < depth; i++)
		writel_relaxed(0x0, drvdata->base + ETB_RWD_REG);

	/* reset write RAM pointer address */
	writel_relaxed(0x0, drvdata->base + ETB_RAM_WRITE_POINTER);
	/* reset read RAM pointer address */
	writel_relaxed(0x0, drvdata->base + ETB_RAM_READ_POINTER);

	writel_relaxed(drvdata->trigger_cntr, drvdata->base + ETB_TRG);
	writel_relaxed(ETB_FFCR_EN_FTC | ETB_FFCR_STOP_TRIGGER,
		       drvdata->base + ETB_FFCR);
	/* ETB trace capture enable */
	writel_relaxed(ETB_CTL_CAPT_EN, drvdata->base + ETB_CTL_REG);

	CS_LOCK(drvdata->base);
}

static int etb_enable(struct coresight_device *csdev)
{
	struct etb_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);
	int ret;
	unsigned long flags;

	ret = clk_prepare_enable(drvdata->clk);
	if (ret)
		return ret;

	spin_lock_irqsave(&drvdata->spinlock, flags);
	etb_enable_hw(drvdata);
	drvdata->enable = true;
	spin_unlock_irqrestore(&drvdata->spinlock, flags);

	dev_info(drvdata->dev, "ETB enabled\n");
	return 0;
}

static void etb_disable_hw(struct etb_drvdata *drvdata)
{
	u32 ffcr;

	CS_UNLOCK(drvdata->base);

	ffcr = readl_relaxed(drvdata->base + ETB_FFCR);
	/* stop formatter when a stop has completed */
	ffcr |= ETB_FFCR_STOP_FI;
	writel_relaxed(ffcr, drvdata->base + ETB_FFCR);
	/* manually generate a flush of the system */
	ffcr |= ETB_FFCR_FON_MAN;
	writel_relaxed(ffcr, drvdata->base + ETB_FFCR);

	if (coresight_timeout(drvdata->base, ETB_FFCR, ETB_FFCR_BIT, 0)) {
		dev_err(drvdata->dev,
			"timeout observed when probing at offset %#x\n",
			ETB_FFCR);
	}

	/* disable trace capture */
	writel_relaxed(0x0, drvdata->base + ETB_CTL_REG);

	if (coresight_timeout(drvdata->base, ETB_FFSR, ETB_FFSR_BIT, 1)) {
		dev_err(drvdata->dev,
			"timeout observed when probing at offset %#x\n",
			ETB_FFCR);
	}

	CS_LOCK(drvdata->base);
}

static void etb_dump_hw(struct etb_drvdata *drvdata)
{
	int i;
	u8 *buf_ptr;
	u32 read_data, depth;
	u32 read_ptr, write_ptr;
	u32 frame_off, frame_endoff;

	CS_UNLOCK(drvdata->base);

	read_ptr = readl_relaxed(drvdata->base + ETB_RAM_READ_POINTER);
	write_ptr = readl_relaxed(drvdata->base + ETB_RAM_WRITE_POINTER);

	frame_off = write_ptr % ETB_FRAME_SIZE_WORDS;
	frame_endoff = ETB_FRAME_SIZE_WORDS - frame_off;
	if (frame_off) {
		dev_err(drvdata->dev,
			"write_ptr: %lu not aligned to formatter frame size\n",
			(unsigned long)write_ptr);
		dev_err(drvdata->dev, "frameoff: %lu, frame_endoff: %lu\n",
			(unsigned long)frame_off, (unsigned long)frame_endoff);
		write_ptr += frame_endoff;
	}

	if ((readl_relaxed(drvdata->base + ETB_STATUS_REG)
		      & ETB_STATUS_RAM_FULL) == 0)
		writel_relaxed(0x0, drvdata->base + ETB_RAM_READ_POINTER);
	else
		writel_relaxed(write_ptr, drvdata->base + ETB_RAM_READ_POINTER);

	depth = drvdata->buffer_depth;
	buf_ptr = drvdata->buf;
	for (i = 0; i < depth; i++) {
		read_data = readl_relaxed(drvdata->base +
					  ETB_RAM_READ_DATA_REG);
		*buf_ptr++ = read_data >> 0;
		*buf_ptr++ = read_data >> 8;
		*buf_ptr++ = read_data >> 16;
		*buf_ptr++ = read_data >> 24;
	}

	if (frame_off) {
		buf_ptr -= (frame_endoff * 4);
		for (i = 0; i < frame_endoff; i++) {
			*buf_ptr++ = 0x0;
			*buf_ptr++ = 0x0;
			*buf_ptr++ = 0x0;
			*buf_ptr++ = 0x0;
		}
	}

	writel_relaxed(read_ptr, drvdata->base + ETB_RAM_READ_POINTER);

	CS_LOCK(drvdata->base);
}

static void etb_disable(struct coresight_device *csdev)
{
	struct etb_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);
	unsigned long flags;

	spin_lock_irqsave(&drvdata->spinlock, flags);
	etb_disable_hw(drvdata);
	etb_dump_hw(drvdata);
	drvdata->enable = false;
	spin_unlock_irqrestore(&drvdata->spinlock, flags);

	clk_disable_unprepare(drvdata->clk);

	dev_info(drvdata->dev, "ETB disabled\n");
}

static const struct coresight_ops_sink etb_sink_ops = {
	.enable		= etb_enable,
	.disable	= etb_disable,
};

static const struct coresight_ops etb_cs_ops = {
	.sink_ops	= &etb_sink_ops,
};

static void etb_dump(struct etb_drvdata *drvdata)
{
	unsigned long flags;

	spin_lock_irqsave(&drvdata->spinlock, flags);
	if (drvdata->enable) {
		etb_disable_hw(drvdata);
		etb_dump_hw(drvdata);
		etb_enable_hw(drvdata);
	}
	spin_unlock_irqrestore(&drvdata->spinlock, flags);

	dev_info(drvdata->dev, "ETB dumped\n");
}

static int etb_open(struct inode *inode, struct file *file)
{
	struct etb_drvdata *drvdata = container_of(file->private_data,
						   struct etb_drvdata, miscdev);

	if (atomic_cmpxchg(&drvdata->in_use, 0, 1))
		return -EBUSY;

	dev_dbg(drvdata->dev, "%s: successfully opened\n", __func__);
	return 0;
}

static ssize_t etb_read(struct file *file, char __user *data,
				size_t len, loff_t *ppos)
{
	u32 depth;
	struct etb_drvdata *drvdata = container_of(file->private_data,
						   struct etb_drvdata, miscdev);

	etb_dump(drvdata);

	depth = drvdata->buffer_depth;
	if (*ppos + len > depth * 4)
		len = depth * 4 - *ppos;

	if (copy_to_user(data, drvdata->buf + *ppos, len)) {
		dev_dbg(drvdata->dev, "%s: copy_to_user failed\n", __func__);
		return -EFAULT;
	}

	*ppos += len;

	dev_dbg(drvdata->dev, "%s: %d bytes copied, %d bytes left\n",
		__func__, len, (int) (depth * 4 - *ppos));
	return len;
}

static int etb_release(struct inode *inode, struct file *file)
{
	struct etb_drvdata *drvdata = container_of(file->private_data,
						   struct etb_drvdata, miscdev);
	atomic_set(&drvdata->in_use, 0);

	dev_dbg(drvdata->dev, "%s: released\n", __func__);
	return 0;
}

static const struct file_operations etb_fops = {
	.owner		= THIS_MODULE,
	.open		= etb_open,
	.read		= etb_read,
	.release	= etb_release,
	.llseek		= no_llseek,
};

static ssize_t status_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	int ret;
	unsigned long flags;
	u32 etb_rdr, etb_sr, etb_rrp, etb_rwp;
	u32 etb_trg, etb_cr, etb_ffsr, etb_ffcr;
	struct etb_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = clk_prepare_enable(drvdata->clk);
	if (ret)
		goto out;

	spin_lock_irqsave(&drvdata->spinlock, flags);
	CS_UNLOCK(drvdata->base);

	etb_rdr = readl_relaxed(drvdata->base + ETB_RAM_DEPTH_REG);
	etb_sr = readl_relaxed(drvdata->base + ETB_STATUS_REG);
	etb_rrp = readl_relaxed(drvdata->base + ETB_RAM_READ_POINTER);
	etb_rwp = readl_relaxed(drvdata->base + ETB_RAM_WRITE_POINTER);
	etb_trg = readl_relaxed(drvdata->base + ETB_TRG);
	etb_cr = readl_relaxed(drvdata->base + ETB_CTL_REG);
	etb_ffsr = readl_relaxed(drvdata->base + ETB_FFSR);
	etb_ffcr = readl_relaxed(drvdata->base + ETB_FFCR);

	CS_LOCK(drvdata->base);
	spin_unlock_irqrestore(&drvdata->spinlock, flags);

	clk_disable_unprepare(drvdata->clk);

	return sprintf(buf,
		       "Depth:\t\t0x%x\n"
		       "Status:\t\t0x%x\n"
		       "RAM read ptr:\t0x%x\n"
		       "RAM wrt ptr:\t0x%x\n"
		       "Trigger cnt:\t0x%x\n"
		       "Control:\t0x%x\n"
		       "Flush status:\t0x%x\n"
		       "Flush ctrl:\t0x%x\n",
		       etb_rdr, etb_sr, etb_rrp, etb_rwp,
		       etb_trg, etb_cr, etb_ffsr, etb_ffcr);
out:
	return -EINVAL;
}
static DEVICE_ATTR_RO(status);

static ssize_t trigger_cntr_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct etb_drvdata *drvdata = dev_get_drvdata(dev->parent);
	unsigned long val = drvdata->trigger_cntr;

	return sprintf(buf, "%#lx\n", val);
}

static ssize_t trigger_cntr_store(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t size)
{
	int ret;
	unsigned long val;
	struct etb_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 16, &val);
	if (ret)
		return ret;

	drvdata->trigger_cntr = val;
	return size;
}
static DEVICE_ATTR_RW(trigger_cntr);

static struct attribute *coresight_etb_attrs[] = {
	&dev_attr_trigger_cntr.attr,
	&dev_attr_status.attr,
	NULL,
};
ATTRIBUTE_GROUPS(coresight_etb);

static int etb_probe(struct amba_device *adev, const struct amba_id *id)
{
	int ret;
	void __iomem *base;
	struct device *dev = &adev->dev;
	struct coresight_platform_data *pdata = NULL;
	struct etb_drvdata *drvdata;
	struct resource *res = &adev->res;
	struct coresight_desc *desc;
	struct device_node *np = adev->dev.of_node;

	if (np) {
		pdata = of_get_coresight_platform_data(dev, np);
		if (IS_ERR(pdata))
			return PTR_ERR(pdata);
		adev->dev.platform_data = pdata;
	}

	drvdata = devm_kzalloc(dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->dev = &adev->dev;
	dev_set_drvdata(dev, drvdata);

	/* validity for the resource is already checked by the AMBA core */
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	drvdata->base = base;

	spin_lock_init(&drvdata->spinlock);

	drvdata->clk = adev->pclk;
	ret = clk_prepare_enable(drvdata->clk);
	if (ret)
		return ret;

	drvdata->buffer_depth = etb_get_buffer_depth(drvdata);
	clk_disable_unprepare(drvdata->clk);

	if (drvdata->buffer_depth < 0)
		return -EINVAL;

	drvdata->buf = devm_kzalloc(dev,
				    drvdata->buffer_depth * 4, GFP_KERNEL);
	if (!drvdata->buf)
		return -ENOMEM;

	desc = devm_kzalloc(dev, sizeof(*desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;

	desc->type = CORESIGHT_DEV_TYPE_SINK;
	desc->subtype.sink_subtype = CORESIGHT_DEV_SUBTYPE_SINK_BUFFER;
	desc->ops = &etb_cs_ops;
	desc->pdata = pdata;
	desc->dev = dev;
	desc->groups = coresight_etb_groups;
	drvdata->csdev = coresight_register(desc);
	if (IS_ERR(drvdata->csdev))
		return PTR_ERR(drvdata->csdev);

	drvdata->miscdev.name = pdata->name;
	drvdata->miscdev.minor = MISC_DYNAMIC_MINOR;
	drvdata->miscdev.fops = &etb_fops;
	ret = misc_register(&drvdata->miscdev);
	if (ret)
		goto err_misc_register;

	dev_info(dev, "ETB initialized\n");
	return 0;

err_misc_register:
	coresight_unregister(drvdata->csdev);
	return ret;
}

static int etb_remove(struct amba_device *adev)
{
	struct etb_drvdata *drvdata = amba_get_drvdata(adev);

	misc_deregister(&drvdata->miscdev);
	coresight_unregister(drvdata->csdev);
	return 0;
}

static struct amba_id etb_ids[] = {
	{
		.id	= 0x0003b907,
		.mask	= 0x0003ffff,
	},
	{ 0, 0},
};

static struct amba_driver etb_driver = {
	.drv = {
		.name	= "coresight-etb10",
		.owner	= THIS_MODULE,
	},
	.probe		= etb_probe,
	.remove		= etb_remove,
	.id_table	= etb_ids,
};

module_amba_driver(etb_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("CoreSight Embedded Trace Buffer driver");
