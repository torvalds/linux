/*
 * Linux network driver for Brocade Converged Network Adapter.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (GPL) Version 2 as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
/*
 * Copyright (c) 2005-2011 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 */

#include <linux/debugfs.h>
#include <linux/module.h>
#include "bnad.h"

/*
 * BNA debufs interface
 *
 * To access the interface, debugfs file system should be mounted
 * if not already mounted using:
 *	mount -t debugfs none /sys/kernel/debug
 *
 * BNA Hierarchy:
 *	- bna/pci_dev:<pci_name>
 * where the pci_name corresponds to the one under /sys/bus/pci/drivers/bna
 *
 * Debugging service available per pci_dev:
 *	fwtrc:  To collect current firmware trace.
 *	fwsave: To collect last saved fw trace as a result of firmware crash.
 *	regwr:  To write one word to chip register
 *	regrd:  To read one or more words from chip register.
 */

struct bnad_debug_info {
	char *debug_buffer;
	void *i_private;
	int buffer_len;
};

static int
bnad_debugfs_open_fwtrc(struct inode *inode, struct file *file)
{
	struct bnad *bnad = inode->i_private;
	struct bnad_debug_info *fw_debug;
	unsigned long flags;
	int rc;

	fw_debug = kzalloc(sizeof(struct bnad_debug_info), GFP_KERNEL);
	if (!fw_debug)
		return -ENOMEM;

	fw_debug->buffer_len = BNA_DBG_FWTRC_LEN;

	fw_debug->debug_buffer = kzalloc(fw_debug->buffer_len, GFP_KERNEL);
	if (!fw_debug->debug_buffer) {
		kfree(fw_debug);
		fw_debug = NULL;
		return -ENOMEM;
	}

	spin_lock_irqsave(&bnad->bna_lock, flags);
	rc = bfa_nw_ioc_debug_fwtrc(&bnad->bna.ioceth.ioc,
			fw_debug->debug_buffer,
			&fw_debug->buffer_len);
	spin_unlock_irqrestore(&bnad->bna_lock, flags);
	if (rc != BFA_STATUS_OK) {
		kfree(fw_debug->debug_buffer);
		fw_debug->debug_buffer = NULL;
		kfree(fw_debug);
		fw_debug = NULL;
		pr_warn("bnad %s: Failed to collect fwtrc\n",
			pci_name(bnad->pcidev));
		return -ENOMEM;
	}

	file->private_data = fw_debug;

	return 0;
}

static int
bnad_debugfs_open_fwsave(struct inode *inode, struct file *file)
{
	struct bnad *bnad = inode->i_private;
	struct bnad_debug_info *fw_debug;
	unsigned long flags;
	int rc;

	fw_debug = kzalloc(sizeof(struct bnad_debug_info), GFP_KERNEL);
	if (!fw_debug)
		return -ENOMEM;

	fw_debug->buffer_len = BNA_DBG_FWTRC_LEN;

	fw_debug->debug_buffer = kzalloc(fw_debug->buffer_len, GFP_KERNEL);
	if (!fw_debug->debug_buffer) {
		kfree(fw_debug);
		fw_debug = NULL;
		return -ENOMEM;
	}

	spin_lock_irqsave(&bnad->bna_lock, flags);
	rc = bfa_nw_ioc_debug_fwsave(&bnad->bna.ioceth.ioc,
			fw_debug->debug_buffer,
			&fw_debug->buffer_len);
	spin_unlock_irqrestore(&bnad->bna_lock, flags);
	if (rc != BFA_STATUS_OK && rc != BFA_STATUS_ENOFSAVE) {
		kfree(fw_debug->debug_buffer);
		fw_debug->debug_buffer = NULL;
		kfree(fw_debug);
		fw_debug = NULL;
		pr_warn("bna %s: Failed to collect fwsave\n",
			pci_name(bnad->pcidev));
		return -ENOMEM;
	}

	file->private_data = fw_debug;

	return 0;
}

static int
bnad_debugfs_open_reg(struct inode *inode, struct file *file)
{
	struct bnad_debug_info *reg_debug;

	reg_debug = kzalloc(sizeof(struct bnad_debug_info), GFP_KERNEL);
	if (!reg_debug)
		return -ENOMEM;

	reg_debug->i_private = inode->i_private;

	file->private_data = reg_debug;

	return 0;
}

static int
bnad_get_debug_drvinfo(struct bnad *bnad, void *buffer, u32 len)
{
	struct bnad_drvinfo *drvinfo = (struct bnad_drvinfo *) buffer;
	struct bnad_iocmd_comp fcomp;
	unsigned long flags = 0;
	int ret = BFA_STATUS_FAILED;

	/* Get IOC info */
	spin_lock_irqsave(&bnad->bna_lock, flags);
	bfa_nw_ioc_get_attr(&bnad->bna.ioceth.ioc, &drvinfo->ioc_attr);
	spin_unlock_irqrestore(&bnad->bna_lock, flags);

	/* Retrieve CEE related info */
	fcomp.bnad = bnad;
	fcomp.comp_status = 0;
	init_completion(&fcomp.comp);
	spin_lock_irqsave(&bnad->bna_lock, flags);
	ret = bfa_nw_cee_get_attr(&bnad->bna.cee, &drvinfo->cee_attr,
				bnad_cb_completion, &fcomp);
	if (ret != BFA_STATUS_OK) {
		spin_unlock_irqrestore(&bnad->bna_lock, flags);
		goto out;
	}
	spin_unlock_irqrestore(&bnad->bna_lock, flags);
	wait_for_completion(&fcomp.comp);
	drvinfo->cee_status = fcomp.comp_status;

	/* Retrieve flash partition info */
	fcomp.comp_status = 0;
	reinit_completion(&fcomp.comp);
	spin_lock_irqsave(&bnad->bna_lock, flags);
	ret = bfa_nw_flash_get_attr(&bnad->bna.flash, &drvinfo->flash_attr,
				bnad_cb_completion, &fcomp);
	if (ret != BFA_STATUS_OK) {
		spin_unlock_irqrestore(&bnad->bna_lock, flags);
		goto out;
	}
	spin_unlock_irqrestore(&bnad->bna_lock, flags);
	wait_for_completion(&fcomp.comp);
	drvinfo->flash_status = fcomp.comp_status;
out:
	return ret;
}

static int
bnad_debugfs_open_drvinfo(struct inode *inode, struct file *file)
{
	struct bnad *bnad = inode->i_private;
	struct bnad_debug_info *drv_info;
	int rc;

	drv_info = kzalloc(sizeof(struct bnad_debug_info), GFP_KERNEL);
	if (!drv_info)
		return -ENOMEM;

	drv_info->buffer_len = sizeof(struct bnad_drvinfo);

	drv_info->debug_buffer = kzalloc(drv_info->buffer_len, GFP_KERNEL);
	if (!drv_info->debug_buffer) {
		kfree(drv_info);
		drv_info = NULL;
		return -ENOMEM;
	}

	mutex_lock(&bnad->conf_mutex);
	rc = bnad_get_debug_drvinfo(bnad, drv_info->debug_buffer,
				drv_info->buffer_len);
	mutex_unlock(&bnad->conf_mutex);
	if (rc != BFA_STATUS_OK) {
		kfree(drv_info->debug_buffer);
		drv_info->debug_buffer = NULL;
		kfree(drv_info);
		drv_info = NULL;
		pr_warn("bna %s: Failed to collect drvinfo\n",
			pci_name(bnad->pcidev));
		return -ENOMEM;
	}

	file->private_data = drv_info;

	return 0;
}

/* Changes the current file position */
static loff_t
bnad_debugfs_lseek(struct file *file, loff_t offset, int orig)
{
	struct bnad_debug_info *debug = file->private_data;

	if (!debug)
		return -EINVAL;

	return fixed_size_llseek(file, offset, orig, debug->buffer_len);
}

static ssize_t
bnad_debugfs_read(struct file *file, char __user *buf,
		  size_t nbytes, loff_t *pos)
{
	struct bnad_debug_info *debug = file->private_data;

	if (!debug || !debug->debug_buffer)
		return 0;

	return simple_read_from_buffer(buf, nbytes, pos,
				debug->debug_buffer, debug->buffer_len);
}

#define BFA_REG_CT_ADDRSZ	(0x40000)
#define BFA_REG_CB_ADDRSZ	(0x20000)
#define BFA_REG_ADDRSZ(__ioc)	\
	((u32)(bfa_asic_id_ctc(bfa_ioc_devid(__ioc)) ?  \
	 BFA_REG_CT_ADDRSZ : BFA_REG_CB_ADDRSZ))
#define BFA_REG_ADDRMSK(__ioc)	(BFA_REG_ADDRSZ(__ioc) - 1)

/*
 * Function to check if the register offset passed is valid.
 */
static int
bna_reg_offset_check(struct bfa_ioc *ioc, u32 offset, u32 len)
{
	u8 area;

	/* check [16:15] */
	area = (offset >> 15) & 0x7;
	if (area == 0) {
		/* PCIe core register */
		if ((offset + (len<<2)) > 0x8000)	/* 8k dwords or 32KB */
			return BFA_STATUS_EINVAL;
	} else if (area == 0x1) {
		/* CB 32 KB memory page */
		if ((offset + (len<<2)) > 0x10000)	/* 8k dwords or 32KB */
			return BFA_STATUS_EINVAL;
	} else {
		/* CB register space 64KB */
		if ((offset + (len<<2)) > BFA_REG_ADDRMSK(ioc))
			return BFA_STATUS_EINVAL;
	}
	return BFA_STATUS_OK;
}

static ssize_t
bnad_debugfs_read_regrd(struct file *file, char __user *buf,
			size_t nbytes, loff_t *pos)
{
	struct bnad_debug_info *regrd_debug = file->private_data;
	struct bnad *bnad = (struct bnad *)regrd_debug->i_private;
	ssize_t rc;

	if (!bnad->regdata)
		return 0;

	rc = simple_read_from_buffer(buf, nbytes, pos,
			bnad->regdata, bnad->reglen);

	if ((*pos + nbytes) >= bnad->reglen) {
		kfree(bnad->regdata);
		bnad->regdata = NULL;
		bnad->reglen = 0;
	}

	return rc;
}

static ssize_t
bnad_debugfs_write_regrd(struct file *file, const char __user *buf,
		size_t nbytes, loff_t *ppos)
{
	struct bnad_debug_info *regrd_debug = file->private_data;
	struct bnad *bnad = (struct bnad *)regrd_debug->i_private;
	struct bfa_ioc *ioc = &bnad->bna.ioceth.ioc;
	int addr, len, rc, i;
	u32 *regbuf;
	void __iomem *rb, *reg_addr;
	unsigned long flags;
	void *kern_buf;

	/* Allocate memory to store the user space buf */
	kern_buf = kzalloc(nbytes, GFP_KERNEL);
	if (!kern_buf)
		return -ENOMEM;

	if (copy_from_user(kern_buf, (void  __user *)buf, nbytes)) {
		kfree(kern_buf);
		return -ENOMEM;
	}

	rc = sscanf(kern_buf, "%x:%x", &addr, &len);
	if (rc < 2) {
		pr_warn("bna %s: Failed to read user buffer\n",
			pci_name(bnad->pcidev));
		kfree(kern_buf);
		return -EINVAL;
	}

	kfree(kern_buf);
	kfree(bnad->regdata);
	bnad->regdata = NULL;
	bnad->reglen = 0;

	bnad->regdata = kzalloc(len << 2, GFP_KERNEL);
	if (!bnad->regdata)
		return -ENOMEM;

	bnad->reglen = len << 2;
	rb = bfa_ioc_bar0(ioc);
	addr &= BFA_REG_ADDRMSK(ioc);

	/* offset and len sanity check */
	rc = bna_reg_offset_check(ioc, addr, len);
	if (rc) {
		pr_warn("bna %s: Failed reg offset check\n",
			pci_name(bnad->pcidev));
		kfree(bnad->regdata);
		bnad->regdata = NULL;
		bnad->reglen = 0;
		return -EINVAL;
	}

	reg_addr = rb + addr;
	regbuf =  (u32 *)bnad->regdata;
	spin_lock_irqsave(&bnad->bna_lock, flags);
	for (i = 0; i < len; i++) {
		*regbuf = readl(reg_addr);
		regbuf++;
		reg_addr += sizeof(u32);
	}
	spin_unlock_irqrestore(&bnad->bna_lock, flags);

	return nbytes;
}

static ssize_t
bnad_debugfs_write_regwr(struct file *file, const char __user *buf,
		size_t nbytes, loff_t *ppos)
{
	struct bnad_debug_info *debug = file->private_data;
	struct bnad *bnad = (struct bnad *)debug->i_private;
	struct bfa_ioc *ioc = &bnad->bna.ioceth.ioc;
	int addr, val, rc;
	void __iomem *reg_addr;
	unsigned long flags;
	void *kern_buf;

	/* Allocate memory to store the user space buf */
	kern_buf = kzalloc(nbytes, GFP_KERNEL);
	if (!kern_buf)
		return -ENOMEM;

	if (copy_from_user(kern_buf, (void  __user *)buf, nbytes)) {
		kfree(kern_buf);
		return -ENOMEM;
	}

	rc = sscanf(kern_buf, "%x:%x", &addr, &val);
	if (rc < 2) {
		pr_warn("bna %s: Failed to read user buffer\n",
			pci_name(bnad->pcidev));
		kfree(kern_buf);
		return -EINVAL;
	}
	kfree(kern_buf);

	addr &= BFA_REG_ADDRMSK(ioc); /* offset only 17 bit and word align */

	/* offset and len sanity check */
	rc = bna_reg_offset_check(ioc, addr, 1);
	if (rc) {
		pr_warn("bna %s: Failed reg offset check\n",
			pci_name(bnad->pcidev));
		return -EINVAL;
	}

	reg_addr = (bfa_ioc_bar0(ioc)) + addr;
	spin_lock_irqsave(&bnad->bna_lock, flags);
	writel(val, reg_addr);
	spin_unlock_irqrestore(&bnad->bna_lock, flags);

	return nbytes;
}

static int
bnad_debugfs_release(struct inode *inode, struct file *file)
{
	struct bnad_debug_info *debug = file->private_data;

	if (!debug)
		return 0;

	file->private_data = NULL;
	kfree(debug);
	return 0;
}

static int
bnad_debugfs_buffer_release(struct inode *inode, struct file *file)
{
	struct bnad_debug_info *debug = file->private_data;

	if (!debug)
		return 0;

	kfree(debug->debug_buffer);

	file->private_data = NULL;
	kfree(debug);
	debug = NULL;
	return 0;
}

static const struct file_operations bnad_debugfs_op_fwtrc = {
	.owner		=	THIS_MODULE,
	.open		=	bnad_debugfs_open_fwtrc,
	.llseek		=	bnad_debugfs_lseek,
	.read		=	bnad_debugfs_read,
	.release	=	bnad_debugfs_buffer_release,
};

static const struct file_operations bnad_debugfs_op_fwsave = {
	.owner		=	THIS_MODULE,
	.open		=	bnad_debugfs_open_fwsave,
	.llseek		=	bnad_debugfs_lseek,
	.read		=	bnad_debugfs_read,
	.release	=	bnad_debugfs_buffer_release,
};

static const struct file_operations bnad_debugfs_op_regrd = {
	.owner		=       THIS_MODULE,
	.open		=	bnad_debugfs_open_reg,
	.llseek		=	bnad_debugfs_lseek,
	.read		=	bnad_debugfs_read_regrd,
	.write		=	bnad_debugfs_write_regrd,
	.release	=	bnad_debugfs_release,
};

static const struct file_operations bnad_debugfs_op_regwr = {
	.owner		=	THIS_MODULE,
	.open		=	bnad_debugfs_open_reg,
	.llseek		=	bnad_debugfs_lseek,
	.write		=	bnad_debugfs_write_regwr,
	.release	=	bnad_debugfs_release,
};

static const struct file_operations bnad_debugfs_op_drvinfo = {
	.owner		=	THIS_MODULE,
	.open		=	bnad_debugfs_open_drvinfo,
	.llseek		=	bnad_debugfs_lseek,
	.read		=	bnad_debugfs_read,
	.release	=	bnad_debugfs_buffer_release,
};

struct bnad_debugfs_entry {
	const char *name;
	umode_t  mode;
	const struct file_operations *fops;
};

static const struct bnad_debugfs_entry bnad_debugfs_files[] = {
	{ "fwtrc",  S_IFREG|S_IRUGO, &bnad_debugfs_op_fwtrc, },
	{ "fwsave", S_IFREG|S_IRUGO, &bnad_debugfs_op_fwsave, },
	{ "regrd",  S_IFREG|S_IRUGO|S_IWUSR, &bnad_debugfs_op_regrd, },
	{ "regwr",  S_IFREG|S_IWUSR, &bnad_debugfs_op_regwr, },
	{ "drvinfo", S_IFREG|S_IRUGO, &bnad_debugfs_op_drvinfo, },
};

static struct dentry *bna_debugfs_root;
static atomic_t bna_debugfs_port_count;

/* Initialize debugfs interface for BNA */
void
bnad_debugfs_init(struct bnad *bnad)
{
	const struct bnad_debugfs_entry *file;
	char name[64];
	int i;

	/* Setup the BNA debugfs root directory*/
	if (!bna_debugfs_root) {
		bna_debugfs_root = debugfs_create_dir("bna", NULL);
		atomic_set(&bna_debugfs_port_count, 0);
		if (!bna_debugfs_root) {
			pr_warn("BNA: debugfs root dir creation failed\n");
			return;
		}
	}

	/* Setup the pci_dev debugfs directory for the port */
	snprintf(name, sizeof(name), "pci_dev:%s", pci_name(bnad->pcidev));
	if (!bnad->port_debugfs_root) {
		bnad->port_debugfs_root =
			debugfs_create_dir(name, bna_debugfs_root);
		if (!bnad->port_debugfs_root) {
			pr_warn("bna pci_dev %s: root dir creation failed\n",
				pci_name(bnad->pcidev));
			return;
		}

		atomic_inc(&bna_debugfs_port_count);

		for (i = 0; i < ARRAY_SIZE(bnad_debugfs_files); i++) {
			file = &bnad_debugfs_files[i];
			bnad->bnad_dentry_files[i] =
					debugfs_create_file(file->name,
							file->mode,
							bnad->port_debugfs_root,
							bnad,
							file->fops);
			if (!bnad->bnad_dentry_files[i]) {
				pr_warn(
				     "BNA pci_dev:%s: create %s entry failed\n",
				     pci_name(bnad->pcidev), file->name);
				return;
			}
		}
	}
}

/* Uninitialize debugfs interface for BNA */
void
bnad_debugfs_uninit(struct bnad *bnad)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(bnad_debugfs_files); i++) {
		if (bnad->bnad_dentry_files[i]) {
			debugfs_remove(bnad->bnad_dentry_files[i]);
			bnad->bnad_dentry_files[i] = NULL;
		}
	}

	/* Remove the pci_dev debugfs directory for the port */
	if (bnad->port_debugfs_root) {
		debugfs_remove(bnad->port_debugfs_root);
		bnad->port_debugfs_root = NULL;
		atomic_dec(&bna_debugfs_port_count);
	}

	/* Remove the BNA debugfs root directory */
	if (atomic_read(&bna_debugfs_port_count) == 0) {
		debugfs_remove(bna_debugfs_root);
		bna_debugfs_root = NULL;
	}
}
