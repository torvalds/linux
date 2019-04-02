/*
 * Copyright (c) 2005-2014 Brocade Communications Systems, Inc.
 * Copyright (c) 2014- QLogic Corporation.
 * All rights reserved
 * www.qlogic.com
 *
 * Linux driver for QLogic BR-series Fibre Channel Host Bus Adapter.
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

#include <linux/defs.h>
#include <linux/export.h>

#include "bfad_drv.h"
#include "bfad_im.h"

/*
 * BFA debufs interface
 *
 * To access the interface, defs file system should be mounted
 * if not already mounted using:
 * mount -t defs none /sys/kernel/de
 *
 * BFA Hierarchy:
 *	- bfa/pci_dev:<pci_name>
 * where the pci_name corresponds to the one under /sys/bus/pci/drivers/bfa
 *
 * Deging service available per pci_dev:
 * fwtrc:  To collect current firmware trace.
 * drvtrc: To collect current driver trace
 * fwsave: To collect last saved fw trace as a result of firmware crash.
 * regwr:  To write one word to chip register
 * regrd:  To read one or more words from chip register.
 */

struct bfad_de_info {
	char *de_buffer;
	void *i_private;
	int buffer_len;
};

static int
bfad_defs_open_drvtrc(struct inode *inode, struct file *file)
{
	struct bfad_port_s *port = inode->i_private;
	struct bfad_s *bfad = port->bfad;
	struct bfad_de_info *de;

	de = kzalloc(sizeof(struct bfad_de_info), GFP_KERNEL);
	if (!de)
		return -ENOMEM;

	de->de_buffer = (void *) bfad->trcmod;
	de->buffer_len = sizeof(struct bfa_trc_mod_s);

	file->private_data = de;

	return 0;
}

static int
bfad_defs_open_fwtrc(struct inode *inode, struct file *file)
{
	struct bfad_port_s *port = inode->i_private;
	struct bfad_s *bfad = port->bfad;
	struct bfad_de_info *fw_de;
	unsigned long flags;
	int rc;

	fw_de = kzalloc(sizeof(struct bfad_de_info), GFP_KERNEL);
	if (!fw_de)
		return -ENOMEM;

	fw_de->buffer_len = sizeof(struct bfa_trc_mod_s);

	fw_de->de_buffer = vzalloc(fw_de->buffer_len);
	if (!fw_de->de_buffer) {
		kfree(fw_de);
		printk(KERN_INFO "bfad[%d]: Failed to allocate fwtrc buffer\n",
				bfad->inst_no);
		return -ENOMEM;
	}

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	rc = bfa_ioc_de_fwtrc(&bfad->bfa.ioc,
			fw_de->de_buffer,
			&fw_de->buffer_len);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	if (rc != BFA_STATUS_OK) {
		vfree(fw_de->de_buffer);
		fw_de->de_buffer = NULL;
		kfree(fw_de);
		printk(KERN_INFO "bfad[%d]: Failed to collect fwtrc\n",
				bfad->inst_no);
		return -ENOMEM;
	}

	file->private_data = fw_de;

	return 0;
}

static int
bfad_defs_open_fwsave(struct inode *inode, struct file *file)
{
	struct bfad_port_s *port = inode->i_private;
	struct bfad_s *bfad = port->bfad;
	struct bfad_de_info *fw_de;
	unsigned long flags;
	int rc;

	fw_de = kzalloc(sizeof(struct bfad_de_info), GFP_KERNEL);
	if (!fw_de)
		return -ENOMEM;

	fw_de->buffer_len = sizeof(struct bfa_trc_mod_s);

	fw_de->de_buffer = vzalloc(fw_de->buffer_len);
	if (!fw_de->de_buffer) {
		kfree(fw_de);
		printk(KERN_INFO "bfad[%d]: Failed to allocate fwsave buffer\n",
				bfad->inst_no);
		return -ENOMEM;
	}

	spin_lock_irqsave(&bfad->bfad_lock, flags);
	rc = bfa_ioc_de_fwsave(&bfad->bfa.ioc,
			fw_de->de_buffer,
			&fw_de->buffer_len);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);
	if (rc != BFA_STATUS_OK) {
		vfree(fw_de->de_buffer);
		fw_de->de_buffer = NULL;
		kfree(fw_de);
		printk(KERN_INFO "bfad[%d]: Failed to collect fwsave\n",
				bfad->inst_no);
		return -ENOMEM;
	}

	file->private_data = fw_de;

	return 0;
}

static int
bfad_defs_open_reg(struct inode *inode, struct file *file)
{
	struct bfad_de_info *reg_de;

	reg_de = kzalloc(sizeof(struct bfad_de_info), GFP_KERNEL);
	if (!reg_de)
		return -ENOMEM;

	reg_de->i_private = inode->i_private;

	file->private_data = reg_de;

	return 0;
}

/* Changes the current file position */
static loff_t
bfad_defs_lseek(struct file *file, loff_t offset, int orig)
{
	struct bfad_de_info *de = file->private_data;
	return fixed_size_llseek(file, offset, orig,
				de->buffer_len);
}

static ssize_t
bfad_defs_read(struct file *file, char __user *buf,
			size_t nbytes, loff_t *pos)
{
	struct bfad_de_info *de = file->private_data;

	if (!de || !de->de_buffer)
		return 0;

	return simple_read_from_buffer(buf, nbytes, pos,
				de->de_buffer, de->buffer_len);
}

#define BFA_REG_CT_ADDRSZ	(0x40000)
#define BFA_REG_CB_ADDRSZ	(0x20000)
#define BFA_REG_ADDRSZ(__ioc)	\
	((u32)(bfa_asic_id_ctc(bfa_ioc_devid(__ioc)) ?	\
	 BFA_REG_CT_ADDRSZ : BFA_REG_CB_ADDRSZ))
#define BFA_REG_ADDRMSK(__ioc)	(BFA_REG_ADDRSZ(__ioc) - 1)

static bfa_status_t
bfad_reg_offset_check(struct bfa_s *bfa, u32 offset, u32 len)
{
	u8	area;

	/* check [16:15] */
	area = (offset >> 15) & 0x7;
	if (area == 0) {
		/* PCIe core register */
		if ((offset + (len<<2)) > 0x8000)    /* 8k dwords or 32KB */
			return BFA_STATUS_EINVAL;
	} else if (area == 0x1) {
		/* CB 32 KB memory page */
		if ((offset + (len<<2)) > 0x10000)    /* 8k dwords or 32KB */
			return BFA_STATUS_EINVAL;
	} else {
		/* CB register space 64KB */
		if ((offset + (len<<2)) > BFA_REG_ADDRMSK(&bfa->ioc))
			return BFA_STATUS_EINVAL;
	}
	return BFA_STATUS_OK;
}

static ssize_t
bfad_defs_read_regrd(struct file *file, char __user *buf,
		size_t nbytes, loff_t *pos)
{
	struct bfad_de_info *regrd_de = file->private_data;
	struct bfad_port_s *port = (struct bfad_port_s *)regrd_de->i_private;
	struct bfad_s *bfad = port->bfad;
	ssize_t rc;

	if (!bfad->regdata)
		return 0;

	rc = simple_read_from_buffer(buf, nbytes, pos,
			bfad->regdata, bfad->reglen);

	if ((*pos + nbytes) >= bfad->reglen) {
		kfree(bfad->regdata);
		bfad->regdata = NULL;
		bfad->reglen = 0;
	}

	return rc;
}

static ssize_t
bfad_defs_write_regrd(struct file *file, const char __user *buf,
		size_t nbytes, loff_t *ppos)
{
	struct bfad_de_info *regrd_de = file->private_data;
	struct bfad_port_s *port = (struct bfad_port_s *)regrd_de->i_private;
	struct bfad_s *bfad = port->bfad;
	struct bfa_s *bfa = &bfad->bfa;
	struct bfa_ioc_s *ioc = &bfa->ioc;
	int addr, rc, i;
	u32 len;
	u32 *regbuf;
	void __iomem *rb, *reg_addr;
	unsigned long flags;
	void *kern_buf;

	kern_buf = memdup_user(buf, nbytes);
	if (IS_ERR(kern_buf))
		return PTR_ERR(kern_buf);

	rc = sscanf(kern_buf, "%x:%x", &addr, &len);
	if (rc < 2 || len > (UINT_MAX >> 2)) {
		printk(KERN_INFO
			"bfad[%d]: %s failed to read user buf\n",
			bfad->inst_no, __func__);
		kfree(kern_buf);
		return -EINVAL;
	}

	kfree(kern_buf);
	kfree(bfad->regdata);
	bfad->regdata = NULL;
	bfad->reglen = 0;

	bfad->regdata = kzalloc(len << 2, GFP_KERNEL);
	if (!bfad->regdata) {
		printk(KERN_INFO "bfad[%d]: Failed to allocate regrd buffer\n",
				bfad->inst_no);
		return -ENOMEM;
	}

	bfad->reglen = len << 2;
	rb = bfa_ioc_bar0(ioc);
	addr &= BFA_REG_ADDRMSK(ioc);

	/* offset and len sanity check */
	rc = bfad_reg_offset_check(bfa, addr, len);
	if (rc) {
		printk(KERN_INFO "bfad[%d]: Failed reg offset check\n",
				bfad->inst_no);
		kfree(bfad->regdata);
		bfad->regdata = NULL;
		bfad->reglen = 0;
		return -EINVAL;
	}

	reg_addr = rb + addr;
	regbuf =  (u32 *)bfad->regdata;
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	for (i = 0; i < len; i++) {
		*regbuf = readl(reg_addr);
		regbuf++;
		reg_addr += sizeof(u32);
	}
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	return nbytes;
}

static ssize_t
bfad_defs_write_regwr(struct file *file, const char __user *buf,
		size_t nbytes, loff_t *ppos)
{
	struct bfad_de_info *de = file->private_data;
	struct bfad_port_s *port = (struct bfad_port_s *)de->i_private;
	struct bfad_s *bfad = port->bfad;
	struct bfa_s *bfa = &bfad->bfa;
	struct bfa_ioc_s *ioc = &bfa->ioc;
	int addr, val, rc;
	void __iomem *reg_addr;
	unsigned long flags;
	void *kern_buf;

	kern_buf = memdup_user(buf, nbytes);
	if (IS_ERR(kern_buf))
		return PTR_ERR(kern_buf);

	rc = sscanf(kern_buf, "%x:%x", &addr, &val);
	if (rc < 2) {
		printk(KERN_INFO
			"bfad[%d]: %s failed to read user buf\n",
			bfad->inst_no, __func__);
		kfree(kern_buf);
		return -EINVAL;
	}
	kfree(kern_buf);

	addr &= BFA_REG_ADDRMSK(ioc); /* offset only 17 bit and word align */

	/* offset and len sanity check */
	rc = bfad_reg_offset_check(bfa, addr, 1);
	if (rc) {
		printk(KERN_INFO
			"bfad[%d]: Failed reg offset check\n",
			bfad->inst_no);
		return -EINVAL;
	}

	reg_addr = (bfa_ioc_bar0(ioc)) + addr;
	spin_lock_irqsave(&bfad->bfad_lock, flags);
	writel(val, reg_addr);
	spin_unlock_irqrestore(&bfad->bfad_lock, flags);

	return nbytes;
}

static int
bfad_defs_release(struct inode *inode, struct file *file)
{
	struct bfad_de_info *de = file->private_data;

	if (!de)
		return 0;

	file->private_data = NULL;
	kfree(de);
	return 0;
}

static int
bfad_defs_release_fwtrc(struct inode *inode, struct file *file)
{
	struct bfad_de_info *fw_de = file->private_data;

	if (!fw_de)
		return 0;

	if (fw_de->de_buffer)
		vfree(fw_de->de_buffer);

	file->private_data = NULL;
	kfree(fw_de);
	return 0;
}

static const struct file_operations bfad_defs_op_drvtrc = {
	.owner		=	THIS_MODULE,
	.open		=	bfad_defs_open_drvtrc,
	.llseek		=	bfad_defs_lseek,
	.read		=	bfad_defs_read,
	.release	=	bfad_defs_release,
};

static const struct file_operations bfad_defs_op_fwtrc = {
	.owner		=	THIS_MODULE,
	.open		=	bfad_defs_open_fwtrc,
	.llseek		=	bfad_defs_lseek,
	.read		=	bfad_defs_read,
	.release	=	bfad_defs_release_fwtrc,
};

static const struct file_operations bfad_defs_op_fwsave = {
	.owner		=	THIS_MODULE,
	.open		=	bfad_defs_open_fwsave,
	.llseek		=	bfad_defs_lseek,
	.read		=	bfad_defs_read,
	.release	=	bfad_defs_release_fwtrc,
};

static const struct file_operations bfad_defs_op_regrd = {
	.owner		=	THIS_MODULE,
	.open		=	bfad_defs_open_reg,
	.llseek		=	bfad_defs_lseek,
	.read		=	bfad_defs_read_regrd,
	.write		=	bfad_defs_write_regrd,
	.release	=	bfad_defs_release,
};

static const struct file_operations bfad_defs_op_regwr = {
	.owner		=	THIS_MODULE,
	.open		=	bfad_defs_open_reg,
	.llseek		=	bfad_defs_lseek,
	.write		=	bfad_defs_write_regwr,
	.release	=	bfad_defs_release,
};

struct bfad_defs_entry {
	const char *name;
	umode_t	mode;
	const struct file_operations *fops;
};

static const struct bfad_defs_entry bfad_defs_files[] = {
	{ "drvtrc", S_IFREG|S_IRUGO, &bfad_defs_op_drvtrc, },
	{ "fwtrc",  S_IFREG|S_IRUGO, &bfad_defs_op_fwtrc,  },
	{ "fwsave", S_IFREG|S_IRUGO, &bfad_defs_op_fwsave, },
	{ "regrd",  S_IFREG|S_IRUGO|S_IWUSR, &bfad_defs_op_regrd,  },
	{ "regwr",  S_IFREG|S_IWUSR, &bfad_defs_op_regwr,  },
};

static struct dentry *bfa_defs_root;
static atomic_t bfa_defs_port_count;

inline void
bfad_defs_init(struct bfad_port_s *port)
{
	struct bfad_s *bfad = port->bfad;
	const struct bfad_defs_entry *file;
	char name[64];
	int i;

	if (!bfa_defs_enable)
		return;

	/* Setup the BFA defs root directory*/
	if (!bfa_defs_root) {
		bfa_defs_root = defs_create_dir("bfa", NULL);
		atomic_set(&bfa_defs_port_count, 0);
	}

	/* Setup the pci_dev defs directory for the port */
	snprintf(name, sizeof(name), "pci_dev:%s", bfad->pci_name);
	if (!port->port_defs_root) {
		port->port_defs_root =
			defs_create_dir(name, bfa_defs_root);

		atomic_inc(&bfa_defs_port_count);

		for (i = 0; i < ARRAY_SIZE(bfad_defs_files); i++) {
			file = &bfad_defs_files[i];
			bfad->bfad_dentry_files[i] =
					defs_create_file(file->name,
							file->mode,
							port->port_defs_root,
							port,
							file->fops);
		}
	}

	return;
}

inline void
bfad_defs_exit(struct bfad_port_s *port)
{
	struct bfad_s *bfad = port->bfad;
	int i;

	for (i = 0; i < ARRAY_SIZE(bfad_defs_files); i++) {
		if (bfad->bfad_dentry_files[i]) {
			defs_remove(bfad->bfad_dentry_files[i]);
			bfad->bfad_dentry_files[i] = NULL;
		}
	}

	/* Remove the pci_dev defs directory for the port */
	if (port->port_defs_root) {
		defs_remove(port->port_defs_root);
		port->port_defs_root = NULL;
		atomic_dec(&bfa_defs_port_count);
	}

	/* Remove the BFA defs root directory */
	if (atomic_read(&bfa_defs_port_count) == 0) {
		defs_remove(bfa_defs_root);
		bfa_defs_root = NULL;
	}
}
