// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for FPGA Accelerated Function Unit (AFU)
 *
 * Copyright (C) 2017-2018 Intel Corporation, Inc.
 *
 * Authors:
 *   Wu Hao <hao.wu@intel.com>
 *   Xiao Guangrong <guangrong.xiao@linux.intel.com>
 *   Joseph Grecco <joe.grecco@intel.com>
 *   Enno Luebbers <enno.luebbers@intel.com>
 *   Tim Whisonant <tim.whisonant@intel.com>
 *   Ananda Ravuri <ananda.ravuri@intel.com>
 *   Henry Mitchel <henry.mitchel@intel.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/fpga-dfl.h>

#include "dfl-afu.h"

#define RST_POLL_INVL 10 /* us */
#define RST_POLL_TIMEOUT 1000 /* us */

/**
 * __afu_port_enable - enable a port by clear reset
 * @fdata: port feature dev data.
 *
 * Enable Port by clear the port soft reset bit, which is set by default.
 * The AFU is unable to respond to any MMIO access while in reset.
 * __afu_port_enable function should only be used after __afu_port_disable
 * function.
 *
 * The caller needs to hold lock for protection.
 */
int __afu_port_enable(struct dfl_feature_dev_data *fdata)
{
	void __iomem *base;
	u64 v;

	WARN_ON(!fdata->disable_count);

	if (--fdata->disable_count != 0)
		return 0;

	base = dfl_get_feature_ioaddr_by_id(fdata, PORT_FEATURE_ID_HEADER);

	/* Clear port soft reset */
	v = readq(base + PORT_HDR_CTRL);
	v &= ~PORT_CTRL_SFTRST;
	writeq(v, base + PORT_HDR_CTRL);

	/*
	 * HW clears the ack bit to indicate that the port is fully out
	 * of reset.
	 */
	if (readq_poll_timeout(base + PORT_HDR_CTRL, v,
			       !(v & PORT_CTRL_SFTRST_ACK),
			       RST_POLL_INVL, RST_POLL_TIMEOUT)) {
		dev_err(fdata->dfl_cdev->parent,
			"timeout, failure to enable device\n");
		return -ETIMEDOUT;
	}

	return 0;
}

/**
 * __afu_port_disable - disable a port by hold reset
 * @fdata: port feature dev data.
 *
 * Disable Port by setting the port soft reset bit, it puts the port into reset.
 *
 * The caller needs to hold lock for protection.
 */
int __afu_port_disable(struct dfl_feature_dev_data *fdata)
{
	void __iomem *base;
	u64 v;

	if (fdata->disable_count++ != 0)
		return 0;

	base = dfl_get_feature_ioaddr_by_id(fdata, PORT_FEATURE_ID_HEADER);

	/* Set port soft reset */
	v = readq(base + PORT_HDR_CTRL);
	v |= PORT_CTRL_SFTRST;
	writeq(v, base + PORT_HDR_CTRL);

	/*
	 * HW sets ack bit to 1 when all outstanding requests have been drained
	 * on this port and minimum soft reset pulse width has elapsed.
	 * Driver polls port_soft_reset_ack to determine if reset done by HW.
	 */
	if (readq_poll_timeout(base + PORT_HDR_CTRL, v,
			       v & PORT_CTRL_SFTRST_ACK,
			       RST_POLL_INVL, RST_POLL_TIMEOUT)) {
		dev_err(fdata->dfl_cdev->parent,
			"timeout, failure to disable device\n");
		return -ETIMEDOUT;
	}

	return 0;
}

/*
 * This function resets the FPGA Port and its accelerator (AFU) by function
 * __port_disable and __port_enable (set port soft reset bit and then clear
 * it). Userspace can do Port reset at any time, e.g. during DMA or Partial
 * Reconfiguration. But it should never cause any system level issue, only
 * functional failure (e.g. DMA or PR operation failure) and be recoverable
 * from the failure.
 *
 * Note: the accelerator (AFU) is not accessible when its port is in reset
 * (disabled). Any attempts on MMIO access to AFU while in reset, will
 * result errors reported via port error reporting sub feature (if present).
 */
static int __port_reset(struct dfl_feature_dev_data *fdata)
{
	int ret;

	ret = __afu_port_disable(fdata);
	if (ret)
		return ret;

	return __afu_port_enable(fdata);
}

static int port_reset(struct platform_device *pdev)
{
	struct dfl_feature_dev_data *fdata = to_dfl_feature_dev_data(&pdev->dev);
	int ret;

	mutex_lock(&fdata->lock);
	ret = __port_reset(fdata);
	mutex_unlock(&fdata->lock);

	return ret;
}

static int port_get_id(struct dfl_feature_dev_data *fdata)
{
	void __iomem *base;

	base = dfl_get_feature_ioaddr_by_id(fdata, PORT_FEATURE_ID_HEADER);

	return FIELD_GET(PORT_CAP_PORT_NUM, readq(base + PORT_HDR_CAP));
}

static ssize_t
id_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dfl_feature_dev_data *fdata = to_dfl_feature_dev_data(dev);
	int id = port_get_id(fdata);

	return scnprintf(buf, PAGE_SIZE, "%d\n", id);
}
static DEVICE_ATTR_RO(id);

static ssize_t
ltr_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dfl_feature_dev_data *fdata = to_dfl_feature_dev_data(dev);
	void __iomem *base;
	u64 v;

	base = dfl_get_feature_ioaddr_by_id(fdata, PORT_FEATURE_ID_HEADER);

	mutex_lock(&fdata->lock);
	v = readq(base + PORT_HDR_CTRL);
	mutex_unlock(&fdata->lock);

	return sprintf(buf, "%x\n", (u8)FIELD_GET(PORT_CTRL_LATENCY, v));
}

static ssize_t
ltr_store(struct device *dev, struct device_attribute *attr,
	  const char *buf, size_t count)
{
	struct dfl_feature_dev_data *fdata = to_dfl_feature_dev_data(dev);
	void __iomem *base;
	bool ltr;
	u64 v;

	if (kstrtobool(buf, &ltr))
		return -EINVAL;

	base = dfl_get_feature_ioaddr_by_id(fdata, PORT_FEATURE_ID_HEADER);

	mutex_lock(&fdata->lock);
	v = readq(base + PORT_HDR_CTRL);
	v &= ~PORT_CTRL_LATENCY;
	v |= FIELD_PREP(PORT_CTRL_LATENCY, ltr ? 1 : 0);
	writeq(v, base + PORT_HDR_CTRL);
	mutex_unlock(&fdata->lock);

	return count;
}
static DEVICE_ATTR_RW(ltr);

static ssize_t
ap1_event_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dfl_feature_dev_data *fdata = to_dfl_feature_dev_data(dev);
	void __iomem *base;
	u64 v;

	base = dfl_get_feature_ioaddr_by_id(fdata, PORT_FEATURE_ID_HEADER);

	mutex_lock(&fdata->lock);
	v = readq(base + PORT_HDR_STS);
	mutex_unlock(&fdata->lock);

	return sprintf(buf, "%x\n", (u8)FIELD_GET(PORT_STS_AP1_EVT, v));
}

static ssize_t
ap1_event_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct dfl_feature_dev_data *fdata = to_dfl_feature_dev_data(dev);
	void __iomem *base;
	bool clear;

	if (kstrtobool(buf, &clear) || !clear)
		return -EINVAL;

	base = dfl_get_feature_ioaddr_by_id(fdata, PORT_FEATURE_ID_HEADER);

	mutex_lock(&fdata->lock);
	writeq(PORT_STS_AP1_EVT, base + PORT_HDR_STS);
	mutex_unlock(&fdata->lock);

	return count;
}
static DEVICE_ATTR_RW(ap1_event);

static ssize_t
ap2_event_show(struct device *dev, struct device_attribute *attr,
	       char *buf)
{
	struct dfl_feature_dev_data *fdata = to_dfl_feature_dev_data(dev);
	void __iomem *base;
	u64 v;

	base = dfl_get_feature_ioaddr_by_id(fdata, PORT_FEATURE_ID_HEADER);

	mutex_lock(&fdata->lock);
	v = readq(base + PORT_HDR_STS);
	mutex_unlock(&fdata->lock);

	return sprintf(buf, "%x\n", (u8)FIELD_GET(PORT_STS_AP2_EVT, v));
}

static ssize_t
ap2_event_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct dfl_feature_dev_data *fdata = to_dfl_feature_dev_data(dev);
	void __iomem *base;
	bool clear;

	if (kstrtobool(buf, &clear) || !clear)
		return -EINVAL;

	base = dfl_get_feature_ioaddr_by_id(fdata, PORT_FEATURE_ID_HEADER);

	mutex_lock(&fdata->lock);
	writeq(PORT_STS_AP2_EVT, base + PORT_HDR_STS);
	mutex_unlock(&fdata->lock);

	return count;
}
static DEVICE_ATTR_RW(ap2_event);

static ssize_t
power_state_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dfl_feature_dev_data *fdata = to_dfl_feature_dev_data(dev);
	void __iomem *base;
	u64 v;

	base = dfl_get_feature_ioaddr_by_id(fdata, PORT_FEATURE_ID_HEADER);

	mutex_lock(&fdata->lock);
	v = readq(base + PORT_HDR_STS);
	mutex_unlock(&fdata->lock);

	return sprintf(buf, "0x%x\n", (u8)FIELD_GET(PORT_STS_PWR_STATE, v));
}
static DEVICE_ATTR_RO(power_state);

static ssize_t
userclk_freqcmd_store(struct device *dev, struct device_attribute *attr,
		      const char *buf, size_t count)
{
	struct dfl_feature_dev_data *fdata = to_dfl_feature_dev_data(dev);
	u64 userclk_freq_cmd;
	void __iomem *base;

	if (kstrtou64(buf, 0, &userclk_freq_cmd))
		return -EINVAL;

	base = dfl_get_feature_ioaddr_by_id(fdata, PORT_FEATURE_ID_HEADER);

	mutex_lock(&fdata->lock);
	writeq(userclk_freq_cmd, base + PORT_HDR_USRCLK_CMD0);
	mutex_unlock(&fdata->lock);

	return count;
}
static DEVICE_ATTR_WO(userclk_freqcmd);

static ssize_t
userclk_freqcntrcmd_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct dfl_feature_dev_data *fdata = to_dfl_feature_dev_data(dev);
	u64 userclk_freqcntr_cmd;
	void __iomem *base;

	if (kstrtou64(buf, 0, &userclk_freqcntr_cmd))
		return -EINVAL;

	base = dfl_get_feature_ioaddr_by_id(fdata, PORT_FEATURE_ID_HEADER);

	mutex_lock(&fdata->lock);
	writeq(userclk_freqcntr_cmd, base + PORT_HDR_USRCLK_CMD1);
	mutex_unlock(&fdata->lock);

	return count;
}
static DEVICE_ATTR_WO(userclk_freqcntrcmd);

static ssize_t
userclk_freqsts_show(struct device *dev, struct device_attribute *attr,
		     char *buf)
{
	struct dfl_feature_dev_data *fdata = to_dfl_feature_dev_data(dev);
	u64 userclk_freqsts;
	void __iomem *base;

	base = dfl_get_feature_ioaddr_by_id(fdata, PORT_FEATURE_ID_HEADER);

	mutex_lock(&fdata->lock);
	userclk_freqsts = readq(base + PORT_HDR_USRCLK_STS0);
	mutex_unlock(&fdata->lock);

	return sprintf(buf, "0x%llx\n", (unsigned long long)userclk_freqsts);
}
static DEVICE_ATTR_RO(userclk_freqsts);

static ssize_t
userclk_freqcntrsts_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct dfl_feature_dev_data *fdata = to_dfl_feature_dev_data(dev);
	u64 userclk_freqcntrsts;
	void __iomem *base;

	base = dfl_get_feature_ioaddr_by_id(fdata, PORT_FEATURE_ID_HEADER);

	mutex_lock(&fdata->lock);
	userclk_freqcntrsts = readq(base + PORT_HDR_USRCLK_STS1);
	mutex_unlock(&fdata->lock);

	return sprintf(buf, "0x%llx\n",
		       (unsigned long long)userclk_freqcntrsts);
}
static DEVICE_ATTR_RO(userclk_freqcntrsts);

static struct attribute *port_hdr_attrs[] = {
	&dev_attr_id.attr,
	&dev_attr_ltr.attr,
	&dev_attr_ap1_event.attr,
	&dev_attr_ap2_event.attr,
	&dev_attr_power_state.attr,
	&dev_attr_userclk_freqcmd.attr,
	&dev_attr_userclk_freqcntrcmd.attr,
	&dev_attr_userclk_freqsts.attr,
	&dev_attr_userclk_freqcntrsts.attr,
	NULL,
};

static umode_t port_hdr_attrs_visible(struct kobject *kobj,
				      struct attribute *attr, int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct dfl_feature_dev_data *fdata;
	umode_t mode = attr->mode;
	void __iomem *base;

	fdata = to_dfl_feature_dev_data(dev);
	base = dfl_get_feature_ioaddr_by_id(fdata, PORT_FEATURE_ID_HEADER);

	if (dfl_feature_revision(base) > 0) {
		/*
		 * userclk sysfs interfaces are only visible in case port
		 * revision is 0, as hardware with revision >0 doesn't
		 * support this.
		 */
		if (attr == &dev_attr_userclk_freqcmd.attr ||
		    attr == &dev_attr_userclk_freqcntrcmd.attr ||
		    attr == &dev_attr_userclk_freqsts.attr ||
		    attr == &dev_attr_userclk_freqcntrsts.attr)
			mode = 0;
	}

	return mode;
}

static const struct attribute_group port_hdr_group = {
	.attrs      = port_hdr_attrs,
	.is_visible = port_hdr_attrs_visible,
};

static int port_hdr_init(struct platform_device *pdev,
			 struct dfl_feature *feature)
{
	port_reset(pdev);

	return 0;
}

static long
port_hdr_ioctl(struct platform_device *pdev, struct dfl_feature *feature,
	       unsigned int cmd, unsigned long arg)
{
	long ret;

	switch (cmd) {
	case DFL_FPGA_PORT_RESET:
		if (!arg)
			ret = port_reset(pdev);
		else
			ret = -EINVAL;
		break;
	default:
		dev_dbg(&pdev->dev, "%x cmd not handled", cmd);
		ret = -ENODEV;
	}

	return ret;
}

static const struct dfl_feature_id port_hdr_id_table[] = {
	{.id = PORT_FEATURE_ID_HEADER,},
	{0,}
};

static const struct dfl_feature_ops port_hdr_ops = {
	.init = port_hdr_init,
	.ioctl = port_hdr_ioctl,
};

static ssize_t
afu_id_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dfl_feature_dev_data *fdata = to_dfl_feature_dev_data(dev);
	void __iomem *base;
	u64 guidl, guidh;

	base = dfl_get_feature_ioaddr_by_id(fdata, PORT_FEATURE_ID_AFU);

	mutex_lock(&fdata->lock);
	if (fdata->disable_count) {
		mutex_unlock(&fdata->lock);
		return -EBUSY;
	}

	guidl = readq(base + GUID_L);
	guidh = readq(base + GUID_H);
	mutex_unlock(&fdata->lock);

	return scnprintf(buf, PAGE_SIZE, "%016llx%016llx\n", guidh, guidl);
}
static DEVICE_ATTR_RO(afu_id);

static struct attribute *port_afu_attrs[] = {
	&dev_attr_afu_id.attr,
	NULL
};

static umode_t port_afu_attrs_visible(struct kobject *kobj,
				      struct attribute *attr, int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct dfl_feature_dev_data *fdata;

	fdata = to_dfl_feature_dev_data(dev);
	/*
	 * sysfs entries are visible only if related private feature is
	 * enumerated.
	 */
	if (!dfl_get_feature_by_id(fdata, PORT_FEATURE_ID_AFU))
		return 0;

	return attr->mode;
}

static const struct attribute_group port_afu_group = {
	.attrs      = port_afu_attrs,
	.is_visible = port_afu_attrs_visible,
};

static int port_afu_init(struct platform_device *pdev,
			 struct dfl_feature *feature)
{
	struct dfl_feature_dev_data *fdata = to_dfl_feature_dev_data(&pdev->dev);
	struct resource *res = &pdev->resource[feature->resource_index];

	return afu_mmio_region_add(fdata,
				   DFL_PORT_REGION_INDEX_AFU,
				   resource_size(res), res->start,
				   DFL_PORT_REGION_MMAP | DFL_PORT_REGION_READ |
				   DFL_PORT_REGION_WRITE);
}

static const struct dfl_feature_id port_afu_id_table[] = {
	{.id = PORT_FEATURE_ID_AFU,},
	{0,}
};

static const struct dfl_feature_ops port_afu_ops = {
	.init = port_afu_init,
};

static int port_stp_init(struct platform_device *pdev,
			 struct dfl_feature *feature)
{
	struct dfl_feature_dev_data *fdata = to_dfl_feature_dev_data(&pdev->dev);
	struct resource *res = &pdev->resource[feature->resource_index];

	return afu_mmio_region_add(fdata,
				   DFL_PORT_REGION_INDEX_STP,
				   resource_size(res), res->start,
				   DFL_PORT_REGION_MMAP | DFL_PORT_REGION_READ |
				   DFL_PORT_REGION_WRITE);
}

static const struct dfl_feature_id port_stp_id_table[] = {
	{.id = PORT_FEATURE_ID_STP,},
	{0,}
};

static const struct dfl_feature_ops port_stp_ops = {
	.init = port_stp_init,
};

static long
port_uint_ioctl(struct platform_device *pdev, struct dfl_feature *feature,
		unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case DFL_FPGA_PORT_UINT_GET_IRQ_NUM:
		return dfl_feature_ioctl_get_num_irqs(pdev, feature, arg);
	case DFL_FPGA_PORT_UINT_SET_IRQ:
		return dfl_feature_ioctl_set_irq(pdev, feature, arg);
	default:
		dev_dbg(&pdev->dev, "%x cmd not handled", cmd);
		return -ENODEV;
	}
}

static const struct dfl_feature_id port_uint_id_table[] = {
	{.id = PORT_FEATURE_ID_UINT,},
	{0,}
};

static const struct dfl_feature_ops port_uint_ops = {
	.ioctl = port_uint_ioctl,
};

static struct dfl_feature_driver port_feature_drvs[] = {
	{
		.id_table = port_hdr_id_table,
		.ops = &port_hdr_ops,
	},
	{
		.id_table = port_afu_id_table,
		.ops = &port_afu_ops,
	},
	{
		.id_table = port_err_id_table,
		.ops = &port_err_ops,
	},
	{
		.id_table = port_stp_id_table,
		.ops = &port_stp_ops,
	},
	{
		.id_table = port_uint_id_table,
		.ops = &port_uint_ops,
	},
	{
		.ops = NULL,
	}
};

static int afu_open(struct inode *inode, struct file *filp)
{
	struct dfl_feature_dev_data *fdata = dfl_fpga_inode_to_feature_dev_data(inode);
	struct platform_device *fdev = fdata->dev;
	int ret;

	mutex_lock(&fdata->lock);
	ret = dfl_feature_dev_use_begin(fdata, filp->f_flags & O_EXCL);
	if (!ret) {
		dev_dbg(&fdev->dev, "Device File Opened %d Times\n",
			dfl_feature_dev_use_count(fdata));
		filp->private_data = fdev;
	}
	mutex_unlock(&fdata->lock);

	return ret;
}

static int afu_release(struct inode *inode, struct file *filp)
{
	struct platform_device *pdev = filp->private_data;
	struct dfl_feature_dev_data *fdata;
	struct dfl_feature *feature;

	dev_dbg(&pdev->dev, "Device File Release\n");

	fdata = to_dfl_feature_dev_data(&pdev->dev);

	mutex_lock(&fdata->lock);
	dfl_feature_dev_use_end(fdata);

	if (!dfl_feature_dev_use_count(fdata)) {
		dfl_fpga_dev_for_each_feature(fdata, feature)
			dfl_fpga_set_irq_triggers(feature, 0,
						  feature->nr_irqs, NULL);
		__port_reset(fdata);
		afu_dma_region_destroy(fdata);
	}
	mutex_unlock(&fdata->lock);

	return 0;
}

static long afu_ioctl_check_extension(struct dfl_feature_dev_data *fdata,
				      unsigned long arg)
{
	/* No extension support for now */
	return 0;
}

static long
afu_ioctl_get_info(struct dfl_feature_dev_data *fdata, void __user *arg)
{
	struct dfl_fpga_port_info info;
	struct dfl_afu *afu;
	unsigned long minsz;

	minsz = offsetofend(struct dfl_fpga_port_info, num_umsgs);

	if (copy_from_user(&info, arg, minsz))
		return -EFAULT;

	if (info.argsz < minsz)
		return -EINVAL;

	mutex_lock(&fdata->lock);
	afu = dfl_fpga_fdata_get_private(fdata);
	info.flags = 0;
	info.num_regions = afu->num_regions;
	info.num_umsgs = afu->num_umsgs;
	mutex_unlock(&fdata->lock);

	if (copy_to_user(arg, &info, sizeof(info)))
		return -EFAULT;

	return 0;
}

static long afu_ioctl_get_region_info(struct dfl_feature_dev_data *fdata,
				      void __user *arg)
{
	struct dfl_fpga_port_region_info rinfo;
	struct dfl_afu_mmio_region region;
	unsigned long minsz;
	long ret;

	minsz = offsetofend(struct dfl_fpga_port_region_info, offset);

	if (copy_from_user(&rinfo, arg, minsz))
		return -EFAULT;

	if (rinfo.argsz < minsz || rinfo.padding)
		return -EINVAL;

	ret = afu_mmio_region_get_by_index(fdata, rinfo.index, &region);
	if (ret)
		return ret;

	rinfo.flags = region.flags;
	rinfo.size = region.size;
	rinfo.offset = region.offset;

	if (copy_to_user(arg, &rinfo, sizeof(rinfo)))
		return -EFAULT;

	return 0;
}

static long
afu_ioctl_dma_map(struct dfl_feature_dev_data *fdata, void __user *arg)
{
	struct dfl_fpga_port_dma_map map;
	unsigned long minsz;
	long ret;

	minsz = offsetofend(struct dfl_fpga_port_dma_map, iova);

	if (copy_from_user(&map, arg, minsz))
		return -EFAULT;

	if (map.argsz < minsz || map.flags)
		return -EINVAL;

	ret = afu_dma_map_region(fdata, map.user_addr, map.length, &map.iova);
	if (ret)
		return ret;

	if (copy_to_user(arg, &map, sizeof(map))) {
		afu_dma_unmap_region(fdata, map.iova);
		return -EFAULT;
	}

	dev_dbg(&fdata->dev->dev, "dma map: ua=%llx, len=%llx, iova=%llx\n",
		(unsigned long long)map.user_addr,
		(unsigned long long)map.length,
		(unsigned long long)map.iova);

	return 0;
}

static long
afu_ioctl_dma_unmap(struct dfl_feature_dev_data *fdata, void __user *arg)
{
	struct dfl_fpga_port_dma_unmap unmap;
	unsigned long minsz;

	minsz = offsetofend(struct dfl_fpga_port_dma_unmap, iova);

	if (copy_from_user(&unmap, arg, minsz))
		return -EFAULT;

	if (unmap.argsz < minsz || unmap.flags)
		return -EINVAL;

	return afu_dma_unmap_region(fdata, unmap.iova);
}

static long afu_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct platform_device *pdev = filp->private_data;
	struct dfl_feature_dev_data *fdata;
	struct dfl_feature *f;
	long ret;

	dev_dbg(&pdev->dev, "%s cmd 0x%x\n", __func__, cmd);

	fdata = to_dfl_feature_dev_data(&pdev->dev);

	switch (cmd) {
	case DFL_FPGA_GET_API_VERSION:
		return DFL_FPGA_API_VERSION;
	case DFL_FPGA_CHECK_EXTENSION:
		return afu_ioctl_check_extension(fdata, arg);
	case DFL_FPGA_PORT_GET_INFO:
		return afu_ioctl_get_info(fdata, (void __user *)arg);
	case DFL_FPGA_PORT_GET_REGION_INFO:
		return afu_ioctl_get_region_info(fdata, (void __user *)arg);
	case DFL_FPGA_PORT_DMA_MAP:
		return afu_ioctl_dma_map(fdata, (void __user *)arg);
	case DFL_FPGA_PORT_DMA_UNMAP:
		return afu_ioctl_dma_unmap(fdata, (void __user *)arg);
	default:
		/*
		 * Let sub-feature's ioctl function to handle the cmd
		 * Sub-feature's ioctl returns -ENODEV when cmd is not
		 * handled in this sub feature, and returns 0 and other
		 * error code if cmd is handled.
		 */
		dfl_fpga_dev_for_each_feature(fdata, f)
			if (f->ops && f->ops->ioctl) {
				ret = f->ops->ioctl(pdev, f, cmd, arg);
				if (ret != -ENODEV)
					return ret;
			}
	}

	return -EINVAL;
}

static const struct vm_operations_struct afu_vma_ops = {
#ifdef CONFIG_HAVE_IOREMAP_PROT
	.access = generic_access_phys,
#endif
};

static int afu_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct platform_device *pdev = filp->private_data;
	u64 size = vma->vm_end - vma->vm_start;
	struct dfl_feature_dev_data *fdata;
	struct dfl_afu_mmio_region region;
	u64 offset;
	int ret;

	if (!(vma->vm_flags & VM_SHARED))
		return -EINVAL;

	fdata = to_dfl_feature_dev_data(&pdev->dev);

	offset = vma->vm_pgoff << PAGE_SHIFT;
	ret = afu_mmio_region_get_by_offset(fdata, offset, size, &region);
	if (ret)
		return ret;

	if (!(region.flags & DFL_PORT_REGION_MMAP))
		return -EINVAL;

	if ((vma->vm_flags & VM_READ) && !(region.flags & DFL_PORT_REGION_READ))
		return -EPERM;

	if ((vma->vm_flags & VM_WRITE) &&
	    !(region.flags & DFL_PORT_REGION_WRITE))
		return -EPERM;

	/* Support debug access to the mapping */
	vma->vm_ops = &afu_vma_ops;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	return remap_pfn_range(vma, vma->vm_start,
			(region.phys + (offset - region.offset)) >> PAGE_SHIFT,
			size, vma->vm_page_prot);
}

static const struct file_operations afu_fops = {
	.owner = THIS_MODULE,
	.open = afu_open,
	.release = afu_release,
	.unlocked_ioctl = afu_ioctl,
	.mmap = afu_mmap,
};

static int afu_dev_init(struct platform_device *pdev)
{
	struct dfl_feature_dev_data *fdata = to_dfl_feature_dev_data(&pdev->dev);
	struct dfl_afu *afu;

	afu = devm_kzalloc(&pdev->dev, sizeof(*afu), GFP_KERNEL);
	if (!afu)
		return -ENOMEM;

	mutex_lock(&fdata->lock);
	dfl_fpga_fdata_set_private(fdata, afu);
	afu_mmio_region_init(fdata);
	afu_dma_region_init(fdata);
	mutex_unlock(&fdata->lock);

	return 0;
}

static int afu_dev_destroy(struct platform_device *pdev)
{
	struct dfl_feature_dev_data *fdata = to_dfl_feature_dev_data(&pdev->dev);

	mutex_lock(&fdata->lock);
	afu_mmio_region_destroy(fdata);
	afu_dma_region_destroy(fdata);
	dfl_fpga_fdata_set_private(fdata, NULL);
	mutex_unlock(&fdata->lock);

	return 0;
}

static int port_enable_set(struct dfl_feature_dev_data *fdata, bool enable)
{
	int ret;

	mutex_lock(&fdata->lock);
	if (enable)
		ret = __afu_port_enable(fdata);
	else
		ret = __afu_port_disable(fdata);
	mutex_unlock(&fdata->lock);

	return ret;
}

static struct dfl_fpga_port_ops afu_port_ops = {
	.name = DFL_FPGA_FEATURE_DEV_PORT,
	.owner = THIS_MODULE,
	.get_id = port_get_id,
	.enable_set = port_enable_set,
};

static int afu_probe(struct platform_device *pdev)
{
	int ret;

	dev_dbg(&pdev->dev, "%s\n", __func__);

	ret = afu_dev_init(pdev);
	if (ret)
		goto exit;

	ret = dfl_fpga_dev_feature_init(pdev, port_feature_drvs);
	if (ret)
		goto dev_destroy;

	ret = dfl_fpga_dev_ops_register(pdev, &afu_fops, THIS_MODULE);
	if (ret) {
		dfl_fpga_dev_feature_uinit(pdev);
		goto dev_destroy;
	}

	return 0;

dev_destroy:
	afu_dev_destroy(pdev);
exit:
	return ret;
}

static void afu_remove(struct platform_device *pdev)
{
	dev_dbg(&pdev->dev, "%s\n", __func__);

	dfl_fpga_dev_ops_unregister(pdev);
	dfl_fpga_dev_feature_uinit(pdev);
	afu_dev_destroy(pdev);
}

static const struct attribute_group *afu_dev_groups[] = {
	&port_hdr_group,
	&port_afu_group,
	&port_err_group,
	NULL
};

static struct platform_driver afu_driver = {
	.driver = {
		.name = DFL_FPGA_FEATURE_DEV_PORT,
		.dev_groups = afu_dev_groups,
	},
	.probe = afu_probe,
	.remove = afu_remove,
};

static int __init afu_init(void)
{
	int ret;

	dfl_fpga_port_ops_add(&afu_port_ops);

	ret = platform_driver_register(&afu_driver);
	if (ret)
		dfl_fpga_port_ops_del(&afu_port_ops);

	return ret;
}

static void __exit afu_exit(void)
{
	platform_driver_unregister(&afu_driver);

	dfl_fpga_port_ops_del(&afu_port_ops);
}

module_init(afu_init);
module_exit(afu_exit);

MODULE_DESCRIPTION("FPGA Accelerated Function Unit driver");
MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:dfl-port");
