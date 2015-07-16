/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 *   redistributing this file, you may do so under either license.
 *
 *   GPL LICENSE SUMMARY
 *
 *   Copyright (C) 2015 EMC Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of version 2 of the GNU General Public License as
 *   published by the Free Software Foundation.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   General Public License for more details.
 *
 *   BSD LICENSE
 *
 *   Copyright (C) 2015 EMC Corporation. All Rights Reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copy
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * PCIe NTB Debugging Tool Linux driver
 *
 * Contact Information:
 * Allen Hubbe <Allen.Hubbe@emc.com>
 */

/*
 * How to use this tool, by example.
 *
 * Assuming $DBG_DIR is something like:
 * '/sys/kernel/debug/ntb_tool/0000:00:03.0'
 *
 * Eg: check if clearing the doorbell mask generates an interrupt.
 *
 * # Set the doorbell mask
 * root@self# echo 's 1' > $DBG_DIR/mask
 *
 * # Ring the doorbell from the peer
 * root@peer# echo 's 1' > $DBG_DIR/peer_db
 *
 * # Clear the doorbell mask
 * root@self# echo 'c 1' > $DBG_DIR/mask
 *
 * Observe debugging output in dmesg or your console.  You should see a
 * doorbell event triggered by clearing the mask.  If not, this may indicate an
 * issue with the hardware that needs to be worked around in the driver.
 *
 * Eg: read and write scratchpad registers
 *
 * root@peer# echo '0 0x01010101 1 0x7f7f7f7f' > $DBG_DIR/peer_spad
 *
 * root@self# cat $DBG_DIR/spad
 *
 * Observe that spad 0 and 1 have the values set by the peer.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/debugfs.h>
#include <linux/dma-mapping.h>
#include <linux/pci.h>
#include <linux/slab.h>

#include <linux/ntb.h>

#define DRIVER_NAME			"ntb_tool"
#define DRIVER_DESCRIPTION		"PCIe NTB Debugging Tool"

#define DRIVER_LICENSE			"Dual BSD/GPL"
#define DRIVER_VERSION			"1.0"
#define DRIVER_RELDATE			"22 April 2015"
#define DRIVER_AUTHOR			"Allen Hubbe <Allen.Hubbe@emc.com>"

MODULE_LICENSE(DRIVER_LICENSE);
MODULE_VERSION(DRIVER_VERSION);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESCRIPTION);

static struct dentry *tool_dbgfs;

struct tool_ctx {
	struct ntb_dev *ntb;
	struct dentry *dbgfs;
};

#define SPAD_FNAME_SIZE 0x10
#define INT_PTR(x) ((void *)(unsigned long)x)
#define PTR_INT(x) ((int)(unsigned long)x)

#define TOOL_FOPS_RDWR(__name, __read, __write) \
	const struct file_operations __name = {	\
		.owner = THIS_MODULE,		\
		.open = simple_open,		\
		.read = __read,			\
		.write = __write,		\
	}

static void tool_link_event(void *ctx)
{
	struct tool_ctx *tc = ctx;
	enum ntb_speed speed;
	enum ntb_width width;
	int up;

	up = ntb_link_is_up(tc->ntb, &speed, &width);

	dev_dbg(&tc->ntb->dev, "link is %s speed %d width %d\n",
		up ? "up" : "down", speed, width);
}

static void tool_db_event(void *ctx, int vec)
{
	struct tool_ctx *tc = ctx;
	u64 db_bits, db_mask;

	db_mask = ntb_db_vector_mask(tc->ntb, vec);
	db_bits = ntb_db_read(tc->ntb);

	dev_dbg(&tc->ntb->dev, "doorbell vec %d mask %#llx bits %#llx\n",
		vec, db_mask, db_bits);
}

static const struct ntb_ctx_ops tool_ops = {
	.link_event = tool_link_event,
	.db_event = tool_db_event,
};

static ssize_t tool_dbfn_read(struct tool_ctx *tc, char __user *ubuf,
			      size_t size, loff_t *offp,
			      u64 (*db_read_fn)(struct ntb_dev *))
{
	size_t buf_size;
	char *buf;
	ssize_t pos, rc;

	if (!db_read_fn)
		return -EINVAL;

	buf_size = min_t(size_t, size, 0x20);

	buf = kmalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	pos = scnprintf(buf, buf_size, "%#llx\n",
			db_read_fn(tc->ntb));

	rc = simple_read_from_buffer(ubuf, size, offp, buf, pos);

	kfree(buf);

	return rc;
}

static ssize_t tool_dbfn_write(struct tool_ctx *tc,
			       const char __user *ubuf,
			       size_t size, loff_t *offp,
			       int (*db_set_fn)(struct ntb_dev *, u64),
			       int (*db_clear_fn)(struct ntb_dev *, u64))
{
	u64 db_bits;
	char *buf, cmd;
	ssize_t rc;
	int n;

	buf = kmalloc(size + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	rc = simple_write_to_buffer(buf, size, offp, ubuf, size);
	if (rc < 0) {
		kfree(buf);
		return rc;
	}

	buf[size] = 0;

	n = sscanf(buf, "%c %lli", &cmd, &db_bits);

	kfree(buf);

	if (n != 2) {
		rc = -EINVAL;
	} else if (cmd == 's') {
		if (!db_set_fn)
			rc = -EINVAL;
		else
			rc = db_set_fn(tc->ntb, db_bits);
	} else if (cmd == 'c') {
		if (!db_clear_fn)
			rc = -EINVAL;
		else
			rc = db_clear_fn(tc->ntb, db_bits);
	} else {
		rc = -EINVAL;
	}

	return rc ? : size;
}

static ssize_t tool_spadfn_read(struct tool_ctx *tc, char __user *ubuf,
				size_t size, loff_t *offp,
				u32 (*spad_read_fn)(struct ntb_dev *, int))
{
	size_t buf_size;
	char *buf;
	ssize_t pos, rc;
	int i, spad_count;

	if (!spad_read_fn)
		return -EINVAL;

	buf_size = min_t(size_t, size, 0x100);

	buf = kmalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	pos = 0;

	spad_count = ntb_spad_count(tc->ntb);
	for (i = 0; i < spad_count; ++i) {
		pos += scnprintf(buf + pos, buf_size - pos, "%d\t%#x\n",
				 i, spad_read_fn(tc->ntb, i));
	}

	rc = simple_read_from_buffer(ubuf, size, offp, buf, pos);

	kfree(buf);

	return rc;
}

static ssize_t tool_spadfn_write(struct tool_ctx *tc,
				 const char __user *ubuf,
				 size_t size, loff_t *offp,
				 int (*spad_write_fn)(struct ntb_dev *,
						      int, u32))
{
	int spad_idx;
	u32 spad_val;
	char *buf;
	int pos, n;
	ssize_t rc;

	if (!spad_write_fn) {
		dev_dbg(&tc->ntb->dev, "no spad write fn\n");
		return -EINVAL;
	}

	buf = kmalloc(size + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	rc = simple_write_to_buffer(buf, size, offp, ubuf, size);
	if (rc < 0) {
		kfree(buf);
		return rc;
	}

	buf[size] = 0;

	n = sscanf(buf, "%d %i%n", &spad_idx, &spad_val, &pos);
	while (n == 2) {
		rc = spad_write_fn(tc->ntb, spad_idx, spad_val);
		if (rc)
			break;

		n = sscanf(buf + pos, "%d %i%n", &spad_idx, &spad_val, &pos);
	}

	if (n < 0)
		rc = n;

	kfree(buf);

	return rc ? : size;
}

static ssize_t tool_db_read(struct file *filep, char __user *ubuf,
			    size_t size, loff_t *offp)
{
	struct tool_ctx *tc = filep->private_data;

	return tool_dbfn_read(tc, ubuf, size, offp,
			      tc->ntb->ops->db_read);
}

static ssize_t tool_db_write(struct file *filep, const char __user *ubuf,
			     size_t size, loff_t *offp)
{
	struct tool_ctx *tc = filep->private_data;

	return tool_dbfn_write(tc, ubuf, size, offp,
			       tc->ntb->ops->db_set,
			       tc->ntb->ops->db_clear);
}

static TOOL_FOPS_RDWR(tool_db_fops,
		      tool_db_read,
		      tool_db_write);

static ssize_t tool_mask_read(struct file *filep, char __user *ubuf,
			      size_t size, loff_t *offp)
{
	struct tool_ctx *tc = filep->private_data;

	return tool_dbfn_read(tc, ubuf, size, offp,
			      tc->ntb->ops->db_read_mask);
}

static ssize_t tool_mask_write(struct file *filep, const char __user *ubuf,
			       size_t size, loff_t *offp)
{
	struct tool_ctx *tc = filep->private_data;

	return tool_dbfn_write(tc, ubuf, size, offp,
			       tc->ntb->ops->db_set_mask,
			       tc->ntb->ops->db_clear_mask);
}

static TOOL_FOPS_RDWR(tool_mask_fops,
		      tool_mask_read,
		      tool_mask_write);

static ssize_t tool_peer_db_read(struct file *filep, char __user *ubuf,
				 size_t size, loff_t *offp)
{
	struct tool_ctx *tc = filep->private_data;

	return tool_dbfn_read(tc, ubuf, size, offp,
			      tc->ntb->ops->peer_db_read);
}

static ssize_t tool_peer_db_write(struct file *filep, const char __user *ubuf,
				  size_t size, loff_t *offp)
{
	struct tool_ctx *tc = filep->private_data;

	return tool_dbfn_write(tc, ubuf, size, offp,
			       tc->ntb->ops->peer_db_set,
			       tc->ntb->ops->peer_db_clear);
}

static TOOL_FOPS_RDWR(tool_peer_db_fops,
		      tool_peer_db_read,
		      tool_peer_db_write);

static ssize_t tool_peer_mask_read(struct file *filep, char __user *ubuf,
				   size_t size, loff_t *offp)
{
	struct tool_ctx *tc = filep->private_data;

	return tool_dbfn_read(tc, ubuf, size, offp,
			      tc->ntb->ops->peer_db_read_mask);
}

static ssize_t tool_peer_mask_write(struct file *filep, const char __user *ubuf,
				    size_t size, loff_t *offp)
{
	struct tool_ctx *tc = filep->private_data;

	return tool_dbfn_write(tc, ubuf, size, offp,
			       tc->ntb->ops->peer_db_set_mask,
			       tc->ntb->ops->peer_db_clear_mask);
}

static TOOL_FOPS_RDWR(tool_peer_mask_fops,
		      tool_peer_mask_read,
		      tool_peer_mask_write);

static ssize_t tool_spad_read(struct file *filep, char __user *ubuf,
			      size_t size, loff_t *offp)
{
	struct tool_ctx *tc = filep->private_data;

	return tool_spadfn_read(tc, ubuf, size, offp,
				tc->ntb->ops->spad_read);
}

static ssize_t tool_spad_write(struct file *filep, const char __user *ubuf,
			       size_t size, loff_t *offp)
{
	struct tool_ctx *tc = filep->private_data;

	return tool_spadfn_write(tc, ubuf, size, offp,
				 tc->ntb->ops->spad_write);
}

static TOOL_FOPS_RDWR(tool_spad_fops,
		      tool_spad_read,
		      tool_spad_write);

static ssize_t tool_peer_spad_read(struct file *filep, char __user *ubuf,
				   size_t size, loff_t *offp)
{
	struct tool_ctx *tc = filep->private_data;

	return tool_spadfn_read(tc, ubuf, size, offp,
				tc->ntb->ops->peer_spad_read);
}

static ssize_t tool_peer_spad_write(struct file *filep, const char __user *ubuf,
				    size_t size, loff_t *offp)
{
	struct tool_ctx *tc = filep->private_data;

	return tool_spadfn_write(tc, ubuf, size, offp,
				 tc->ntb->ops->peer_spad_write);
}

static TOOL_FOPS_RDWR(tool_peer_spad_fops,
		      tool_peer_spad_read,
		      tool_peer_spad_write);

static void tool_setup_dbgfs(struct tool_ctx *tc)
{
	/* This modules is useless without dbgfs... */
	if (!tool_dbgfs) {
		tc->dbgfs = NULL;
		return;
	}

	tc->dbgfs = debugfs_create_dir(dev_name(&tc->ntb->dev),
				       tool_dbgfs);
	if (!tc->dbgfs)
		return;

	debugfs_create_file("db", S_IRUSR | S_IWUSR, tc->dbgfs,
			    tc, &tool_db_fops);

	debugfs_create_file("mask", S_IRUSR | S_IWUSR, tc->dbgfs,
			    tc, &tool_mask_fops);

	debugfs_create_file("peer_db", S_IRUSR | S_IWUSR, tc->dbgfs,
			    tc, &tool_peer_db_fops);

	debugfs_create_file("peer_mask", S_IRUSR | S_IWUSR, tc->dbgfs,
			    tc, &tool_peer_mask_fops);

	debugfs_create_file("spad", S_IRUSR | S_IWUSR, tc->dbgfs,
			    tc, &tool_spad_fops);

	debugfs_create_file("peer_spad", S_IRUSR | S_IWUSR, tc->dbgfs,
			    tc, &tool_peer_spad_fops);
}

static int tool_probe(struct ntb_client *self, struct ntb_dev *ntb)
{
	struct tool_ctx *tc;
	int rc;

	if (ntb_db_is_unsafe(ntb))
		dev_dbg(&ntb->dev, "doorbell is unsafe\n");

	if (ntb_spad_is_unsafe(ntb))
		dev_dbg(&ntb->dev, "scratchpad is unsafe\n");

	tc = kmalloc(sizeof(*tc), GFP_KERNEL);
	if (!tc) {
		rc = -ENOMEM;
		goto err_tc;
	}

	tc->ntb = ntb;

	tool_setup_dbgfs(tc);

	rc = ntb_set_ctx(ntb, tc, &tool_ops);
	if (rc)
		goto err_ctx;

	ntb_link_enable(ntb, NTB_SPEED_AUTO, NTB_WIDTH_AUTO);
	ntb_link_event(ntb);

	return 0;

err_ctx:
	debugfs_remove_recursive(tc->dbgfs);
	kfree(tc);
err_tc:
	return rc;
}

static void tool_remove(struct ntb_client *self, struct ntb_dev *ntb)
{
	struct tool_ctx *tc = ntb->ctx;

	ntb_clear_ctx(ntb);
	ntb_link_disable(ntb);

	debugfs_remove_recursive(tc->dbgfs);
	kfree(tc);
}

static struct ntb_client tool_client = {
	.ops = {
		.probe = tool_probe,
		.remove = tool_remove,
	},
};

static int __init tool_init(void)
{
	int rc;

	if (debugfs_initialized())
		tool_dbgfs = debugfs_create_dir(KBUILD_MODNAME, NULL);

	rc = ntb_register_client(&tool_client);
	if (rc)
		goto err_client;

	return 0;

err_client:
	debugfs_remove_recursive(tool_dbgfs);
	return rc;
}
module_init(tool_init);

static void __exit tool_exit(void)
{
	ntb_unregister_client(&tool_client);
	debugfs_remove_recursive(tool_dbgfs);
}
module_exit(tool_exit);
