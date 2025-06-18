// SPDX-License-Identifier: (GPL-2.0-or-later OR BSD-3-Clause)
/*
 * Copyright (c) 2014-2025, Advanced Micro Devices, Inc.
 * Copyright (c) 2014, Synopsys, Inc.
 * All rights reserved
 */

#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "xgbe.h"
#include "xgbe-common.h"

static ssize_t xgbe_common_read(char __user *buffer, size_t count,
				loff_t *ppos, unsigned int value)
{
	char *buf;
	ssize_t len;

	if (*ppos != 0)
		return 0;

	buf = kasprintf(GFP_KERNEL, "0x%08x\n", value);
	if (!buf)
		return -ENOMEM;

	if (count < strlen(buf)) {
		kfree(buf);
		return -ENOSPC;
	}

	len = simple_read_from_buffer(buffer, count, ppos, buf, strlen(buf));
	kfree(buf);

	return len;
}

static ssize_t xgbe_common_write(const char __user *buffer, size_t count,
				 loff_t *ppos, unsigned int *value)
{
	char workarea[32];
	ssize_t len;
	int ret;

	if (*ppos != 0)
		return -EINVAL;

	if (count >= sizeof(workarea))
		return -ENOSPC;

	len = simple_write_to_buffer(workarea, sizeof(workarea) - 1, ppos,
				     buffer, count);
	if (len < 0)
		return len;

	workarea[len] = '\0';
	ret = kstrtouint(workarea, 16, value);
	if (ret)
		return -EIO;

	return len;
}

static ssize_t xgmac_reg_addr_read(struct file *filp, char __user *buffer,
				   size_t count, loff_t *ppos)
{
	struct xgbe_prv_data *pdata = filp->private_data;

	return xgbe_common_read(buffer, count, ppos, pdata->debugfs_xgmac_reg);
}

static ssize_t xgmac_reg_addr_write(struct file *filp,
				    const char __user *buffer,
				    size_t count, loff_t *ppos)
{
	struct xgbe_prv_data *pdata = filp->private_data;

	return xgbe_common_write(buffer, count, ppos,
				 &pdata->debugfs_xgmac_reg);
}

static ssize_t xgmac_reg_value_read(struct file *filp, char __user *buffer,
				    size_t count, loff_t *ppos)
{
	struct xgbe_prv_data *pdata = filp->private_data;
	unsigned int value;

	value = XGMAC_IOREAD(pdata, pdata->debugfs_xgmac_reg);

	return xgbe_common_read(buffer, count, ppos, value);
}

static ssize_t xgmac_reg_value_write(struct file *filp,
				     const char __user *buffer,
				     size_t count, loff_t *ppos)
{
	struct xgbe_prv_data *pdata = filp->private_data;
	unsigned int value;
	ssize_t len;

	len = xgbe_common_write(buffer, count, ppos, &value);
	if (len < 0)
		return len;

	XGMAC_IOWRITE(pdata, pdata->debugfs_xgmac_reg, value);

	return len;
}

static const struct file_operations xgmac_reg_addr_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read =  xgmac_reg_addr_read,
	.write = xgmac_reg_addr_write,
};

static const struct file_operations xgmac_reg_value_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read =  xgmac_reg_value_read,
	.write = xgmac_reg_value_write,
};

static ssize_t xpcs_mmd_read(struct file *filp, char __user *buffer,
			     size_t count, loff_t *ppos)
{
	struct xgbe_prv_data *pdata = filp->private_data;

	return xgbe_common_read(buffer, count, ppos, pdata->debugfs_xpcs_mmd);
}

static ssize_t xpcs_mmd_write(struct file *filp, const char __user *buffer,
			      size_t count, loff_t *ppos)
{
	struct xgbe_prv_data *pdata = filp->private_data;

	return xgbe_common_write(buffer, count, ppos,
				 &pdata->debugfs_xpcs_mmd);
}

static ssize_t xpcs_reg_addr_read(struct file *filp, char __user *buffer,
				  size_t count, loff_t *ppos)
{
	struct xgbe_prv_data *pdata = filp->private_data;

	return xgbe_common_read(buffer, count, ppos, pdata->debugfs_xpcs_reg);
}

static ssize_t xpcs_reg_addr_write(struct file *filp, const char __user *buffer,
				   size_t count, loff_t *ppos)
{
	struct xgbe_prv_data *pdata = filp->private_data;

	return xgbe_common_write(buffer, count, ppos,
				 &pdata->debugfs_xpcs_reg);
}

static ssize_t xpcs_reg_value_read(struct file *filp, char __user *buffer,
				   size_t count, loff_t *ppos)
{
	struct xgbe_prv_data *pdata = filp->private_data;
	unsigned int value;

	value = XMDIO_READ(pdata, pdata->debugfs_xpcs_mmd,
			   pdata->debugfs_xpcs_reg);

	return xgbe_common_read(buffer, count, ppos, value);
}

static ssize_t xpcs_reg_value_write(struct file *filp,
				    const char __user *buffer,
				    size_t count, loff_t *ppos)
{
	struct xgbe_prv_data *pdata = filp->private_data;
	unsigned int value;
	ssize_t len;

	len = xgbe_common_write(buffer, count, ppos, &value);
	if (len < 0)
		return len;

	XMDIO_WRITE(pdata, pdata->debugfs_xpcs_mmd, pdata->debugfs_xpcs_reg,
		    value);

	return len;
}

static const struct file_operations xpcs_mmd_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read =  xpcs_mmd_read,
	.write = xpcs_mmd_write,
};

static const struct file_operations xpcs_reg_addr_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read =  xpcs_reg_addr_read,
	.write = xpcs_reg_addr_write,
};

static const struct file_operations xpcs_reg_value_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read =  xpcs_reg_value_read,
	.write = xpcs_reg_value_write,
};

static ssize_t xprop_reg_addr_read(struct file *filp, char __user *buffer,
				   size_t count, loff_t *ppos)
{
	struct xgbe_prv_data *pdata = filp->private_data;

	return xgbe_common_read(buffer, count, ppos, pdata->debugfs_xprop_reg);
}

static ssize_t xprop_reg_addr_write(struct file *filp,
				    const char __user *buffer,
				    size_t count, loff_t *ppos)
{
	struct xgbe_prv_data *pdata = filp->private_data;

	return xgbe_common_write(buffer, count, ppos,
				 &pdata->debugfs_xprop_reg);
}

static ssize_t xprop_reg_value_read(struct file *filp, char __user *buffer,
				    size_t count, loff_t *ppos)
{
	struct xgbe_prv_data *pdata = filp->private_data;
	unsigned int value;

	value = XP_IOREAD(pdata, pdata->debugfs_xprop_reg);

	return xgbe_common_read(buffer, count, ppos, value);
}

static ssize_t xprop_reg_value_write(struct file *filp,
				     const char __user *buffer,
				     size_t count, loff_t *ppos)
{
	struct xgbe_prv_data *pdata = filp->private_data;
	unsigned int value;
	ssize_t len;

	len = xgbe_common_write(buffer, count, ppos, &value);
	if (len < 0)
		return len;

	XP_IOWRITE(pdata, pdata->debugfs_xprop_reg, value);

	return len;
}

static const struct file_operations xprop_reg_addr_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read =  xprop_reg_addr_read,
	.write = xprop_reg_addr_write,
};

static const struct file_operations xprop_reg_value_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read =  xprop_reg_value_read,
	.write = xprop_reg_value_write,
};

static ssize_t xi2c_reg_addr_read(struct file *filp, char __user *buffer,
				  size_t count, loff_t *ppos)
{
	struct xgbe_prv_data *pdata = filp->private_data;

	return xgbe_common_read(buffer, count, ppos, pdata->debugfs_xi2c_reg);
}

static ssize_t xi2c_reg_addr_write(struct file *filp,
				   const char __user *buffer,
				   size_t count, loff_t *ppos)
{
	struct xgbe_prv_data *pdata = filp->private_data;

	return xgbe_common_write(buffer, count, ppos,
				 &pdata->debugfs_xi2c_reg);
}

static ssize_t xi2c_reg_value_read(struct file *filp, char __user *buffer,
				   size_t count, loff_t *ppos)
{
	struct xgbe_prv_data *pdata = filp->private_data;
	unsigned int value;

	value = XI2C_IOREAD(pdata, pdata->debugfs_xi2c_reg);

	return xgbe_common_read(buffer, count, ppos, value);
}

static ssize_t xi2c_reg_value_write(struct file *filp,
				    const char __user *buffer,
				    size_t count, loff_t *ppos)
{
	struct xgbe_prv_data *pdata = filp->private_data;
	unsigned int value;
	ssize_t len;

	len = xgbe_common_write(buffer, count, ppos, &value);
	if (len < 0)
		return len;

	XI2C_IOWRITE(pdata, pdata->debugfs_xi2c_reg, value);

	return len;
}

static const struct file_operations xi2c_reg_addr_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read =  xi2c_reg_addr_read,
	.write = xi2c_reg_addr_write,
};

static const struct file_operations xi2c_reg_value_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.read =  xi2c_reg_value_read,
	.write = xi2c_reg_value_write,
};

void xgbe_debugfs_init(struct xgbe_prv_data *pdata)
{
	char *buf;

	/* Set defaults */
	pdata->debugfs_xgmac_reg = 0;
	pdata->debugfs_xpcs_mmd = 1;
	pdata->debugfs_xpcs_reg = 0;

	buf = kasprintf(GFP_KERNEL, "amd-xgbe-%s", pdata->netdev->name);
	if (!buf)
		return;

	pdata->xgbe_debugfs = debugfs_create_dir(buf, NULL);

	debugfs_create_file("xgmac_register", 0600, pdata->xgbe_debugfs, pdata,
			    &xgmac_reg_addr_fops);

	debugfs_create_file("xgmac_register_value", 0600, pdata->xgbe_debugfs,
			    pdata, &xgmac_reg_value_fops);

	debugfs_create_file("xpcs_mmd", 0600, pdata->xgbe_debugfs, pdata,
			    &xpcs_mmd_fops);

	debugfs_create_file("xpcs_register", 0600, pdata->xgbe_debugfs, pdata,
			    &xpcs_reg_addr_fops);

	debugfs_create_file("xpcs_register_value", 0600, pdata->xgbe_debugfs,
			    pdata, &xpcs_reg_value_fops);

	if (pdata->xprop_regs) {
		debugfs_create_file("xprop_register", 0600, pdata->xgbe_debugfs,
				    pdata, &xprop_reg_addr_fops);

		debugfs_create_file("xprop_register_value", 0600,
				    pdata->xgbe_debugfs, pdata,
				    &xprop_reg_value_fops);
	}

	if (pdata->xi2c_regs) {
		debugfs_create_file("xi2c_register", 0600, pdata->xgbe_debugfs,
				    pdata, &xi2c_reg_addr_fops);

		debugfs_create_file("xi2c_register_value", 0600,
				    pdata->xgbe_debugfs, pdata,
				    &xi2c_reg_value_fops);
	}

	if (pdata->vdata->an_cdr_workaround) {
		debugfs_create_bool("an_cdr_workaround", 0600,
				    pdata->xgbe_debugfs,
				    &pdata->debugfs_an_cdr_workaround);

		debugfs_create_bool("an_cdr_track_early", 0600,
				    pdata->xgbe_debugfs,
				    &pdata->debugfs_an_cdr_track_early);
	}

	kfree(buf);
}

void xgbe_debugfs_exit(struct xgbe_prv_data *pdata)
{
	debugfs_remove_recursive(pdata->xgbe_debugfs);
	pdata->xgbe_debugfs = NULL;
}

void xgbe_debugfs_rename(struct xgbe_prv_data *pdata)
{
	debugfs_change_name(pdata->xgbe_debugfs,
			    "amd-xgbe-%s", pdata->netdev->name);
}
