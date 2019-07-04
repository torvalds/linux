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

/**
 * port_enable - enable a port
 * @pdev: port platform device.
 *
 * Enable Port by clear the port soft reset bit, which is set by default.
 * The AFU is unable to respond to any MMIO access while in reset.
 * port_enable function should only be used after port_disable function.
 */
static void port_enable(struct platform_device *pdev)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(&pdev->dev);
	void __iomem *base;
	u64 v;

	WARN_ON(!pdata->disable_count);

	if (--pdata->disable_count != 0)
		return;

	base = dfl_get_feature_ioaddr_by_id(&pdev->dev, PORT_FEATURE_ID_HEADER);

	/* Clear port soft reset */
	v = readq(base + PORT_HDR_CTRL);
	v &= ~PORT_CTRL_SFTRST;
	writeq(v, base + PORT_HDR_CTRL);
}

#define RST_POLL_INVL 10 /* us */
#define RST_POLL_TIMEOUT 1000 /* us */

/**
 * port_disable - disable a port
 * @pdev: port platform device.
 *
 * Disable Port by setting the port soft reset bit, it puts the port into
 * reset.
 */
static int port_disable(struct platform_device *pdev)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(&pdev->dev);
	void __iomem *base;
	u64 v;

	if (pdata->disable_count++ != 0)
		return 0;

	base = dfl_get_feature_ioaddr_by_id(&pdev->dev, PORT_FEATURE_ID_HEADER);

	/* Set port soft reset */
	v = readq(base + PORT_HDR_CTRL);
	v |= PORT_CTRL_SFTRST;
	writeq(v, base + PORT_HDR_CTRL);

	/*
	 * HW sets ack bit to 1 when all outstanding requests have been drained
	 * on this port and minimum soft reset pulse width has elapsed.
	 * Driver polls port_soft_reset_ack to determine if reset done by HW.
	 */
	if (readq_poll_timeout(base + PORT_HDR_CTRL, v, v & PORT_CTRL_SFTRST,
			       RST_POLL_INVL, RST_POLL_TIMEOUT)) {
		dev_err(&pdev->dev, "timeout, fail to reset device\n");
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
static int __port_reset(struct platform_device *pdev)
{
	int ret;

	ret = port_disable(pdev);
	if (!ret)
		port_enable(pdev);

	return ret;
}

static int port_reset(struct platform_device *pdev)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(&pdev->dev);
	int ret;

	mutex_lock(&pdata->lock);
	ret = __port_reset(pdev);
	mutex_unlock(&pdata->lock);

	return ret;
}

static int port_get_id(struct platform_device *pdev)
{
	void __iomem *base;

	base = dfl_get_feature_ioaddr_by_id(&pdev->dev, PORT_FEATURE_ID_HEADER);

	return FIELD_GET(PORT_CAP_PORT_NUM, readq(base + PORT_HDR_CAP));
}

static ssize_t
id_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int id = port_get_id(to_platform_device(dev));

	return scnprintf(buf, PAGE_SIZE, "%d\n", id);
}
static DEVICE_ATTR_RO(id);

static struct attribute *port_hdr_attrs[] = {
	&dev_attr_id.attr,
	NULL,
};
ATTRIBUTE_GROUPS(port_hdr);

static int port_hdr_init(struct platform_device *pdev,
			 struct dfl_feature *feature)
{
	dev_dbg(&pdev->dev, "PORT HDR Init.\n");

	port_reset(pdev);

	return device_add_groups(&pdev->dev, port_hdr_groups);
}

static void port_hdr_uinit(struct platform_device *pdev,
			   struct dfl_feature *feature)
{
	dev_dbg(&pdev->dev, "PORT HDR UInit.\n");

	device_remove_groups(&pdev->dev, port_hdr_groups);
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

static const struct dfl_feature_ops port_hdr_ops = {
	.init = port_hdr_init,
	.uinit = port_hdr_uinit,
	.ioctl = port_hdr_ioctl,
};

static ssize_t
afu_id_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(dev);
	void __iomem *base;
	u64 guidl, guidh;

	base = dfl_get_feature_ioaddr_by_id(dev, PORT_FEATURE_ID_AFU);

	mutex_lock(&pdata->lock);
	if (pdata->disable_count) {
		mutex_unlock(&pdata->lock);
		return -EBUSY;
	}

	guidl = readq(base + GUID_L);
	guidh = readq(base + GUID_H);
	mutex_unlock(&pdata->lock);

	return scnprintf(buf, PAGE_SIZE, "%016llx%016llx\n", guidh, guidl);
}
static DEVICE_ATTR_RO(afu_id);

static struct attribute *port_afu_attrs[] = {
	&dev_attr_afu_id.attr,
	NULL
};
ATTRIBUTE_GROUPS(port_afu);

static int port_afu_init(struct platform_device *pdev,
			 struct dfl_feature *feature)
{
	struct resource *res = &pdev->resource[feature->resource_index];
	int ret;

	dev_dbg(&pdev->dev, "PORT AFU Init.\n");

	ret = afu_mmio_region_add(dev_get_platdata(&pdev->dev),
				  DFL_PORT_REGION_INDEX_AFU, resource_size(res),
				  res->start, DFL_PORT_REGION_READ |
				  DFL_PORT_REGION_WRITE | DFL_PORT_REGION_MMAP);
	if (ret)
		return ret;

	return device_add_groups(&pdev->dev, port_afu_groups);
}

static void port_afu_uinit(struct platform_device *pdev,
			   struct dfl_feature *feature)
{
	dev_dbg(&pdev->dev, "PORT AFU UInit.\n");

	device_remove_groups(&pdev->dev, port_afu_groups);
}

static const struct dfl_feature_ops port_afu_ops = {
	.init = port_afu_init,
	.uinit = port_afu_uinit,
};

static struct dfl_feature_driver port_feature_drvs[] = {
	{
		.id = PORT_FEATURE_ID_HEADER,
		.ops = &port_hdr_ops,
	},
	{
		.id = PORT_FEATURE_ID_AFU,
		.ops = &port_afu_ops,
	},
	{
		.ops = NULL,
	}
};

static int afu_open(struct inode *inode, struct file *filp)
{
	struct platform_device *fdev = dfl_fpga_inode_to_feature_dev(inode);
	struct dfl_feature_platform_data *pdata;
	int ret;

	pdata = dev_get_platdata(&fdev->dev);
	if (WARN_ON(!pdata))
		return -ENODEV;

	ret = dfl_feature_dev_use_begin(pdata);
	if (ret)
		return ret;

	dev_dbg(&fdev->dev, "Device File Open\n");
	filp->private_data = fdev;

	return 0;
}

static int afu_release(struct inode *inode, struct file *filp)
{
	struct platform_device *pdev = filp->private_data;
	struct dfl_feature_platform_data *pdata;

	dev_dbg(&pdev->dev, "Device File Release\n");

	pdata = dev_get_platdata(&pdev->dev);

	mutex_lock(&pdata->lock);
	__port_reset(pdev);
	afu_dma_region_destroy(pdata);
	mutex_unlock(&pdata->lock);

	dfl_feature_dev_use_end(pdata);

	return 0;
}

static long afu_ioctl_check_extension(struct dfl_feature_platform_data *pdata,
				      unsigned long arg)
{
	/* No extension support for now */
	return 0;
}

static long
afu_ioctl_get_info(struct dfl_feature_platform_data *pdata, void __user *arg)
{
	struct dfl_fpga_port_info info;
	struct dfl_afu *afu;
	unsigned long minsz;

	minsz = offsetofend(struct dfl_fpga_port_info, num_umsgs);

	if (copy_from_user(&info, arg, minsz))
		return -EFAULT;

	if (info.argsz < minsz)
		return -EINVAL;

	mutex_lock(&pdata->lock);
	afu = dfl_fpga_pdata_get_private(pdata);
	info.flags = 0;
	info.num_regions = afu->num_regions;
	info.num_umsgs = afu->num_umsgs;
	mutex_unlock(&pdata->lock);

	if (copy_to_user(arg, &info, sizeof(info)))
		return -EFAULT;

	return 0;
}

static long afu_ioctl_get_region_info(struct dfl_feature_platform_data *pdata,
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

	ret = afu_mmio_region_get_by_index(pdata, rinfo.index, &region);
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
afu_ioctl_dma_map(struct dfl_feature_platform_data *pdata, void __user *arg)
{
	struct dfl_fpga_port_dma_map map;
	unsigned long minsz;
	long ret;

	minsz = offsetofend(struct dfl_fpga_port_dma_map, iova);

	if (copy_from_user(&map, arg, minsz))
		return -EFAULT;

	if (map.argsz < minsz || map.flags)
		return -EINVAL;

	ret = afu_dma_map_region(pdata, map.user_addr, map.length, &map.iova);
	if (ret)
		return ret;

	if (copy_to_user(arg, &map, sizeof(map))) {
		afu_dma_unmap_region(pdata, map.iova);
		return -EFAULT;
	}

	dev_dbg(&pdata->dev->dev, "dma map: ua=%llx, len=%llx, iova=%llx\n",
		(unsigned long long)map.user_addr,
		(unsigned long long)map.length,
		(unsigned long long)map.iova);

	return 0;
}

static long
afu_ioctl_dma_unmap(struct dfl_feature_platform_data *pdata, void __user *arg)
{
	struct dfl_fpga_port_dma_unmap unmap;
	unsigned long minsz;

	minsz = offsetofend(struct dfl_fpga_port_dma_unmap, iova);

	if (copy_from_user(&unmap, arg, minsz))
		return -EFAULT;

	if (unmap.argsz < minsz || unmap.flags)
		return -EINVAL;

	return afu_dma_unmap_region(pdata, unmap.iova);
}

static long afu_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct platform_device *pdev = filp->private_data;
	struct dfl_feature_platform_data *pdata;
	struct dfl_feature *f;
	long ret;

	dev_dbg(&pdev->dev, "%s cmd 0x%x\n", __func__, cmd);

	pdata = dev_get_platdata(&pdev->dev);

	switch (cmd) {
	case DFL_FPGA_GET_API_VERSION:
		return DFL_FPGA_API_VERSION;
	case DFL_FPGA_CHECK_EXTENSION:
		return afu_ioctl_check_extension(pdata, arg);
	case DFL_FPGA_PORT_GET_INFO:
		return afu_ioctl_get_info(pdata, (void __user *)arg);
	case DFL_FPGA_PORT_GET_REGION_INFO:
		return afu_ioctl_get_region_info(pdata, (void __user *)arg);
	case DFL_FPGA_PORT_DMA_MAP:
		return afu_ioctl_dma_map(pdata, (void __user *)arg);
	case DFL_FPGA_PORT_DMA_UNMAP:
		return afu_ioctl_dma_unmap(pdata, (void __user *)arg);
	default:
		/*
		 * Let sub-feature's ioctl function to handle the cmd
		 * Sub-feature's ioctl returns -ENODEV when cmd is not
		 * handled in this sub feature, and returns 0 and other
		 * error code if cmd is handled.
		 */
		dfl_fpga_dev_for_each_feature(pdata, f)
			if (f->ops && f->ops->ioctl) {
				ret = f->ops->ioctl(pdev, f, cmd, arg);
				if (ret != -ENODEV)
					return ret;
			}
	}

	return -EINVAL;
}

static int afu_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct platform_device *pdev = filp->private_data;
	struct dfl_feature_platform_data *pdata;
	u64 size = vma->vm_end - vma->vm_start;
	struct dfl_afu_mmio_region region;
	u64 offset;
	int ret;

	if (!(vma->vm_flags & VM_SHARED))
		return -EINVAL;

	pdata = dev_get_platdata(&pdev->dev);

	offset = vma->vm_pgoff << PAGE_SHIFT;
	ret = afu_mmio_region_get_by_offset(pdata, offset, size, &region);
	if (ret)
		return ret;

	if (!(region.flags & DFL_PORT_REGION_MMAP))
		return -EINVAL;

	if ((vma->vm_flags & VM_READ) && !(region.flags & DFL_PORT_REGION_READ))
		return -EPERM;

	if ((vma->vm_flags & VM_WRITE) &&
	    !(region.flags & DFL_PORT_REGION_WRITE))
		return -EPERM;

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
	struct dfl_feature_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct dfl_afu *afu;

	afu = devm_kzalloc(&pdev->dev, sizeof(*afu), GFP_KERNEL);
	if (!afu)
		return -ENOMEM;

	afu->pdata = pdata;

	mutex_lock(&pdata->lock);
	dfl_fpga_pdata_set_private(pdata, afu);
	afu_mmio_region_init(pdata);
	afu_dma_region_init(pdata);
	mutex_unlock(&pdata->lock);

	return 0;
}

static int afu_dev_destroy(struct platform_device *pdev)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct dfl_afu *afu;

	mutex_lock(&pdata->lock);
	afu = dfl_fpga_pdata_get_private(pdata);
	afu_mmio_region_destroy(pdata);
	afu_dma_region_destroy(pdata);
	dfl_fpga_pdata_set_private(pdata, NULL);
	mutex_unlock(&pdata->lock);

	return 0;
}

static int port_enable_set(struct platform_device *pdev, bool enable)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(&pdev->dev);
	int ret = 0;

	mutex_lock(&pdata->lock);
	if (enable)
		port_enable(pdev);
	else
		ret = port_disable(pdev);
	mutex_unlock(&pdata->lock);

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

static int afu_remove(struct platform_device *pdev)
{
	dev_dbg(&pdev->dev, "%s\n", __func__);

	dfl_fpga_dev_ops_unregister(pdev);
	dfl_fpga_dev_feature_uinit(pdev);
	afu_dev_destroy(pdev);

	return 0;
}

static struct platform_driver afu_driver = {
	.driver	= {
		.name    = DFL_FPGA_FEATURE_DEV_PORT,
	},
	.probe   = afu_probe,
	.remove  = afu_remove,
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
