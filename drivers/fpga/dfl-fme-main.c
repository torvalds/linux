// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for FPGA Management Engine (FME)
 *
 * Copyright (C) 2017-2018 Intel Corporation, Inc.
 *
 * Authors:
 *   Kang Luwei <luwei.kang@intel.com>
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

#include "dfl.h"
#include "dfl-fme.h"

static ssize_t ports_num_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	void __iomem *base;
	u64 v;

	base = dfl_get_feature_ioaddr_by_id(dev, FME_FEATURE_ID_HEADER);

	v = readq(base + FME_HDR_CAP);

	return scnprintf(buf, PAGE_SIZE, "%u\n",
			 (unsigned int)FIELD_GET(FME_CAP_NUM_PORTS, v));
}
static DEVICE_ATTR_RO(ports_num);

/*
 * Bitstream (static FPGA region) identifier number. It contains the
 * detailed version and other information of this static FPGA region.
 */
static ssize_t bitstream_id_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	void __iomem *base;
	u64 v;

	base = dfl_get_feature_ioaddr_by_id(dev, FME_FEATURE_ID_HEADER);

	v = readq(base + FME_HDR_BITSTREAM_ID);

	return scnprintf(buf, PAGE_SIZE, "0x%llx\n", (unsigned long long)v);
}
static DEVICE_ATTR_RO(bitstream_id);

/*
 * Bitstream (static FPGA region) meta data. It contains the synthesis
 * date, seed and other information of this static FPGA region.
 */
static ssize_t bitstream_metadata_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	void __iomem *base;
	u64 v;

	base = dfl_get_feature_ioaddr_by_id(dev, FME_FEATURE_ID_HEADER);

	v = readq(base + FME_HDR_BITSTREAM_MD);

	return scnprintf(buf, PAGE_SIZE, "0x%llx\n", (unsigned long long)v);
}
static DEVICE_ATTR_RO(bitstream_metadata);

static ssize_t cache_size_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	void __iomem *base;
	u64 v;

	base = dfl_get_feature_ioaddr_by_id(dev, FME_FEATURE_ID_HEADER);

	v = readq(base + FME_HDR_CAP);

	return sprintf(buf, "%u\n",
		       (unsigned int)FIELD_GET(FME_CAP_CACHE_SIZE, v));
}
static DEVICE_ATTR_RO(cache_size);

static ssize_t fabric_version_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	void __iomem *base;
	u64 v;

	base = dfl_get_feature_ioaddr_by_id(dev, FME_FEATURE_ID_HEADER);

	v = readq(base + FME_HDR_CAP);

	return sprintf(buf, "%u\n",
		       (unsigned int)FIELD_GET(FME_CAP_FABRIC_VERID, v));
}
static DEVICE_ATTR_RO(fabric_version);

static ssize_t socket_id_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	void __iomem *base;
	u64 v;

	base = dfl_get_feature_ioaddr_by_id(dev, FME_FEATURE_ID_HEADER);

	v = readq(base + FME_HDR_CAP);

	return sprintf(buf, "%u\n",
		       (unsigned int)FIELD_GET(FME_CAP_SOCKET_ID, v));
}
static DEVICE_ATTR_RO(socket_id);

static struct attribute *fme_hdr_attrs[] = {
	&dev_attr_ports_num.attr,
	&dev_attr_bitstream_id.attr,
	&dev_attr_bitstream_metadata.attr,
	&dev_attr_cache_size.attr,
	&dev_attr_fabric_version.attr,
	&dev_attr_socket_id.attr,
	NULL,
};

static const struct attribute_group fme_hdr_group = {
	.attrs = fme_hdr_attrs,
};

static long fme_hdr_ioctl_release_port(struct dfl_feature_platform_data *pdata,
				       unsigned long arg)
{
	struct dfl_fpga_cdev *cdev = pdata->dfl_cdev;
	int port_id;

	if (get_user(port_id, (int __user *)arg))
		return -EFAULT;

	return dfl_fpga_cdev_release_port(cdev, port_id);
}

static long fme_hdr_ioctl_assign_port(struct dfl_feature_platform_data *pdata,
				      unsigned long arg)
{
	struct dfl_fpga_cdev *cdev = pdata->dfl_cdev;
	int port_id;

	if (get_user(port_id, (int __user *)arg))
		return -EFAULT;

	return dfl_fpga_cdev_assign_port(cdev, port_id);
}

static long fme_hdr_ioctl(struct platform_device *pdev,
			  struct dfl_feature *feature,
			  unsigned int cmd, unsigned long arg)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(&pdev->dev);

	switch (cmd) {
	case DFL_FPGA_FME_PORT_RELEASE:
		return fme_hdr_ioctl_release_port(pdata, arg);
	case DFL_FPGA_FME_PORT_ASSIGN:
		return fme_hdr_ioctl_assign_port(pdata, arg);
	}

	return -ENODEV;
}

static const struct dfl_feature_id fme_hdr_id_table[] = {
	{.id = FME_FEATURE_ID_HEADER,},
	{0,}
};

static const struct dfl_feature_ops fme_hdr_ops = {
	.ioctl = fme_hdr_ioctl,
};

static struct dfl_feature_driver fme_feature_drvs[] = {
	{
		.id_table = fme_hdr_id_table,
		.ops = &fme_hdr_ops,
	},
	{
		.id_table = fme_pr_mgmt_id_table,
		.ops = &fme_pr_mgmt_ops,
	},
	{
		.id_table = fme_global_err_id_table,
		.ops = &fme_global_err_ops,
	},
	{
		.ops = NULL,
	},
};

static long fme_ioctl_check_extension(struct dfl_feature_platform_data *pdata,
				      unsigned long arg)
{
	/* No extension support for now */
	return 0;
}

static int fme_open(struct inode *inode, struct file *filp)
{
	struct platform_device *fdev = dfl_fpga_inode_to_feature_dev(inode);
	struct dfl_feature_platform_data *pdata = dev_get_platdata(&fdev->dev);
	int ret;

	if (WARN_ON(!pdata))
		return -ENODEV;

	ret = dfl_feature_dev_use_begin(pdata);
	if (ret)
		return ret;

	dev_dbg(&fdev->dev, "Device File Open\n");
	filp->private_data = pdata;

	return 0;
}

static int fme_release(struct inode *inode, struct file *filp)
{
	struct dfl_feature_platform_data *pdata = filp->private_data;
	struct platform_device *pdev = pdata->dev;

	dev_dbg(&pdev->dev, "Device File Release\n");
	dfl_feature_dev_use_end(pdata);

	return 0;
}

static long fme_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct dfl_feature_platform_data *pdata = filp->private_data;
	struct platform_device *pdev = pdata->dev;
	struct dfl_feature *f;
	long ret;

	dev_dbg(&pdev->dev, "%s cmd 0x%x\n", __func__, cmd);

	switch (cmd) {
	case DFL_FPGA_GET_API_VERSION:
		return DFL_FPGA_API_VERSION;
	case DFL_FPGA_CHECK_EXTENSION:
		return fme_ioctl_check_extension(pdata, arg);
	default:
		/*
		 * Let sub-feature's ioctl function to handle the cmd.
		 * Sub-feature's ioctl returns -ENODEV when cmd is not
		 * handled in this sub feature, and returns 0 or other
		 * error code if cmd is handled.
		 */
		dfl_fpga_dev_for_each_feature(pdata, f) {
			if (f->ops && f->ops->ioctl) {
				ret = f->ops->ioctl(pdev, f, cmd, arg);
				if (ret != -ENODEV)
					return ret;
			}
		}
	}

	return -EINVAL;
}

static int fme_dev_init(struct platform_device *pdev)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct dfl_fme *fme;

	fme = devm_kzalloc(&pdev->dev, sizeof(*fme), GFP_KERNEL);
	if (!fme)
		return -ENOMEM;

	fme->pdata = pdata;

	mutex_lock(&pdata->lock);
	dfl_fpga_pdata_set_private(pdata, fme);
	mutex_unlock(&pdata->lock);

	return 0;
}

static void fme_dev_destroy(struct platform_device *pdev)
{
	struct dfl_feature_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct dfl_fme *fme;

	mutex_lock(&pdata->lock);
	fme = dfl_fpga_pdata_get_private(pdata);
	dfl_fpga_pdata_set_private(pdata, NULL);
	mutex_unlock(&pdata->lock);
}

static const struct file_operations fme_fops = {
	.owner		= THIS_MODULE,
	.open		= fme_open,
	.release	= fme_release,
	.unlocked_ioctl = fme_ioctl,
};

static int fme_probe(struct platform_device *pdev)
{
	int ret;

	ret = fme_dev_init(pdev);
	if (ret)
		goto exit;

	ret = dfl_fpga_dev_feature_init(pdev, fme_feature_drvs);
	if (ret)
		goto dev_destroy;

	ret = dfl_fpga_dev_ops_register(pdev, &fme_fops, THIS_MODULE);
	if (ret)
		goto feature_uinit;

	return 0;

feature_uinit:
	dfl_fpga_dev_feature_uinit(pdev);
dev_destroy:
	fme_dev_destroy(pdev);
exit:
	return ret;
}

static int fme_remove(struct platform_device *pdev)
{
	dfl_fpga_dev_ops_unregister(pdev);
	dfl_fpga_dev_feature_uinit(pdev);
	fme_dev_destroy(pdev);

	return 0;
}

static const struct attribute_group *fme_dev_groups[] = {
	&fme_hdr_group,
	&fme_global_err_group,
	NULL
};

static struct platform_driver fme_driver = {
	.driver	= {
		.name       = DFL_FPGA_FEATURE_DEV_FME,
		.dev_groups = fme_dev_groups,
	},
	.probe   = fme_probe,
	.remove  = fme_remove,
};

module_platform_driver(fme_driver);

MODULE_DESCRIPTION("FPGA Management Engine driver");
MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:dfl-fme");
