// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PowerNV SCOM bus debugfs interface
 *
 * Copyright 2010 Benjamin Herrenschmidt, IBM Corp
 *                <benh@kernel.crashing.org>
 *     and        David Gibson, IBM Corporation.
 * Copyright 2013 IBM Corp.
 */

#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/bug.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include <asm/machdep.h>
#include <asm/firmware.h>
#include <asm/opal.h>
#include <asm/debugfs.h>
#include <asm/prom.h>

static u64 opal_scom_unmangle(u64 addr)
{
	u64 tmp;

	/*
	 * XSCOM addresses use the top nibble to set indirect mode and
	 * its form.  Bits 4-11 are always 0.
	 *
	 * Because the debugfs interface uses signed offsets and shifts
	 * the address left by 3, we basically cannot use the top 4 bits
	 * of the 64-bit address, and thus cannot use the indirect bit.
	 *
	 * To deal with that, we support the indirect bits being in
	 * bits 4-7 (IBM notation) instead of bit 0-3 in this API, we
	 * do the conversion here.
	 *
	 * For in-kernel use, we don't need to do this mangling.  In
	 * kernel won't have bits 4-7 set.
	 *
	 * So:
	 *   debugfs will always   set 0-3 = 0 and clear 4-7
	 *    kernel will always clear 0-3 = 0 and   set 4-7
	 */
	tmp = addr;
	tmp  &= 0x0f00000000000000;
	addr &= 0xf0ffffffffffffff;
	addr |= tmp << 4;

	return addr;
}

static int opal_scom_read(uint32_t chip, uint64_t addr, u64 reg, u64 *value)
{
	int64_t rc;
	__be64 v;

	reg = opal_scom_unmangle(addr + reg);
	rc = opal_xscom_read(chip, reg, (__be64 *)__pa(&v));
	if (rc) {
		*value = 0xfffffffffffffffful;
		return -EIO;
	}
	*value = be64_to_cpu(v);
	return 0;
}

static int opal_scom_write(uint32_t chip, uint64_t addr, u64 reg, u64 value)
{
	int64_t rc;

	reg = opal_scom_unmangle(addr + reg);
	rc = opal_xscom_write(chip, reg, value);
	if (rc)
		return -EIO;
	return 0;
}

struct scom_debug_entry {
	u32 chip;
	struct debugfs_blob_wrapper path;
	char name[16];
};

static ssize_t scom_debug_read(struct file *filp, char __user *ubuf,
			       size_t count, loff_t *ppos)
{
	struct scom_debug_entry *ent = filp->private_data;
	u64 __user *ubuf64 = (u64 __user *)ubuf;
	loff_t off = *ppos;
	ssize_t done = 0;
	u64 reg, reg_base, reg_cnt, val;
	int rc;

	if (off < 0 || (off & 7) || (count & 7))
		return -EINVAL;
	reg_base = off >> 3;
	reg_cnt = count >> 3;

	for (reg = 0; reg < reg_cnt; reg++) {
		rc = opal_scom_read(ent->chip, reg_base, reg, &val);
		if (!rc)
			rc = put_user(val, ubuf64);
		if (rc) {
			if (!done)
				done = rc;
			break;
		}
		ubuf64++;
		*ppos += 8;
		done += 8;
	}
	return done;
}

static ssize_t scom_debug_write(struct file *filp, const char __user *ubuf,
				size_t count, loff_t *ppos)
{
	struct scom_debug_entry *ent = filp->private_data;
	u64 __user *ubuf64 = (u64 __user *)ubuf;
	loff_t off = *ppos;
	ssize_t done = 0;
	u64 reg, reg_base, reg_cnt, val;
	int rc;

	if (off < 0 || (off & 7) || (count & 7))
		return -EINVAL;
	reg_base = off >> 3;
	reg_cnt = count >> 3;

	for (reg = 0; reg < reg_cnt; reg++) {
		rc = get_user(val, ubuf64);
		if (!rc)
			rc = opal_scom_write(ent->chip, reg_base, reg,  val);
		if (rc) {
			if (!done)
				done = rc;
			break;
		}
		ubuf64++;
		done += 8;
	}
	return done;
}

static const struct file_operations scom_debug_fops = {
	.read =		scom_debug_read,
	.write =	scom_debug_write,
	.open =		simple_open,
	.llseek =	default_llseek,
};

static int scom_debug_init_one(struct dentry *root, struct device_node *dn,
			       int chip)
{
	struct scom_debug_entry *ent;
	struct dentry *dir;

	ent = kzalloc(sizeof(*ent), GFP_KERNEL);
	if (!ent)
		return -ENOMEM;

	ent->chip = chip;
	snprintf(ent->name, 16, "%08x", chip);
	ent->path.data = (void *)kasprintf(GFP_KERNEL, "%pOF", dn);
	ent->path.size = strlen((char *)ent->path.data);

	dir = debugfs_create_dir(ent->name, root);
	if (!dir) {
		kfree(ent->path.data);
		kfree(ent);
		return -1;
	}

	debugfs_create_blob("devspec", 0400, dir, &ent->path);
	debugfs_create_file("access", 0600, dir, ent, &scom_debug_fops);

	return 0;
}

static int scom_debug_init(void)
{
	struct device_node *dn;
	struct dentry *root;
	int chip, rc;

	if (!firmware_has_feature(FW_FEATURE_OPAL))
		return 0;

	root = debugfs_create_dir("scom", powerpc_debugfs_root);
	if (!root)
		return -1;

	rc = 0;
	for_each_node_with_property(dn, "scom-controller") {
		chip = of_get_ibm_chip_id(dn);
		WARN_ON(chip == -1);
		rc |= scom_debug_init_one(root, dn, chip);
	}

	return rc;
}
device_initcall(scom_debug_init);
