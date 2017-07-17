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
 * # Check the link status
 * root@self# cat $DBG_DIR/link
 *
 * # Block until the link is up
 * root@self# echo Y > $DBG_DIR/link_event
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
 *
 * # Check the memory window translation info
 * cat $DBG_DIR/peer_trans0
 *
 * # Setup a 16k memory window buffer
 * echo 16384 > $DBG_DIR/peer_trans0
 *
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/debugfs.h>
#include <linux/dma-mapping.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

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

/* It is rare to have hadrware with greater than six MWs */
#define MAX_MWS	6
/* Only two-ports devices are supported */
#define PIDX	NTB_DEF_PEER_IDX

static struct dentry *tool_dbgfs;

struct tool_mw {
	int idx;
	struct tool_ctx *tc;
	resource_size_t win_size;
	resource_size_t size;
	u8 __iomem *local;
	u8 *peer;
	dma_addr_t peer_dma;
	struct dentry *peer_dbg_file;
};

struct tool_ctx {
	struct ntb_dev *ntb;
	struct dentry *dbgfs;
	wait_queue_head_t link_wq;
	int mw_count;
	struct tool_mw mws[MAX_MWS];
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

	wake_up(&tc->link_wq);
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

	spad_count = ntb_spad_count(tc->ntb);

	/*
	 * We multiply the number of spads by 15 to get the buffer size
	 * this is from 3 for the %d, 10 for the largest hex value
	 * (0x00000000) and 2 for the tab and line feed.
	 */
	buf_size = min_t(size_t, size, spad_count * 15);

	buf = kmalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	pos = 0;

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
	char *buf, *buf_ptr;
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
	buf_ptr = buf;
	n = sscanf(buf_ptr, "%d %i%n", &spad_idx, &spad_val, &pos);
	while (n == 2) {
		buf_ptr += pos;
		rc = spad_write_fn(tc->ntb, spad_idx, spad_val);
		if (rc)
			break;

		n = sscanf(buf_ptr, "%d %i%n", &spad_idx, &spad_val, &pos);
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

static u32 ntb_tool_peer_spad_read(struct ntb_dev *ntb, int sidx)
{
	return ntb_peer_spad_read(ntb, PIDX, sidx);
}

static ssize_t tool_peer_spad_read(struct file *filep, char __user *ubuf,
				   size_t size, loff_t *offp)
{
	struct tool_ctx *tc = filep->private_data;

	return tool_spadfn_read(tc, ubuf, size, offp, ntb_tool_peer_spad_read);
}

static int ntb_tool_peer_spad_write(struct ntb_dev *ntb, int sidx, u32 val)
{
	return ntb_peer_spad_write(ntb, PIDX, sidx, val);
}

static ssize_t tool_peer_spad_write(struct file *filep, const char __user *ubuf,
				    size_t size, loff_t *offp)
{
	struct tool_ctx *tc = filep->private_data;

	return tool_spadfn_write(tc, ubuf, size, offp,
				 ntb_tool_peer_spad_write);
}

static TOOL_FOPS_RDWR(tool_peer_spad_fops,
		      tool_peer_spad_read,
		      tool_peer_spad_write);

static ssize_t tool_link_read(struct file *filep, char __user *ubuf,
			      size_t size, loff_t *offp)
{
	struct tool_ctx *tc = filep->private_data;
	char buf[3];

	buf[0] = ntb_link_is_up(tc->ntb, NULL, NULL) ? 'Y' : 'N';
	buf[1] = '\n';
	buf[2] = '\0';

	return simple_read_from_buffer(ubuf, size, offp, buf, 2);
}

static ssize_t tool_link_write(struct file *filep, const char __user *ubuf,
			       size_t size, loff_t *offp)
{
	struct tool_ctx *tc = filep->private_data;
	char buf[32];
	size_t buf_size;
	bool val;
	int rc;

	buf_size = min(size, (sizeof(buf) - 1));
	if (copy_from_user(buf, ubuf, buf_size))
		return -EFAULT;

	buf[buf_size] = '\0';

	rc = strtobool(buf, &val);
	if (rc)
		return rc;

	if (val)
		rc = ntb_link_enable(tc->ntb, NTB_SPEED_AUTO, NTB_WIDTH_AUTO);
	else
		rc = ntb_link_disable(tc->ntb);

	if (rc)
		return rc;

	return size;
}

static TOOL_FOPS_RDWR(tool_link_fops,
		      tool_link_read,
		      tool_link_write);

static ssize_t tool_link_event_write(struct file *filep,
				     const char __user *ubuf,
				     size_t size, loff_t *offp)
{
	struct tool_ctx *tc = filep->private_data;
	char buf[32];
	size_t buf_size;
	bool val;
	int rc;

	buf_size = min(size, (sizeof(buf) - 1));
	if (copy_from_user(buf, ubuf, buf_size))
		return -EFAULT;

	buf[buf_size] = '\0';

	rc = strtobool(buf, &val);
	if (rc)
		return rc;

	if (wait_event_interruptible(tc->link_wq,
		ntb_link_is_up(tc->ntb, NULL, NULL) == val))
		return -ERESTART;

	return size;
}

static TOOL_FOPS_RDWR(tool_link_event_fops,
		      NULL,
		      tool_link_event_write);

static ssize_t tool_mw_read(struct file *filep, char __user *ubuf,
			    size_t size, loff_t *offp)
{
	struct tool_mw *mw = filep->private_data;
	ssize_t rc;
	loff_t pos = *offp;
	void *buf;

	if (mw->local == NULL)
		return -EIO;
	if (pos < 0)
		return -EINVAL;
	if (pos >= mw->win_size || !size)
		return 0;
	if (size > mw->win_size - pos)
		size = mw->win_size - pos;

	buf = kmalloc(size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	memcpy_fromio(buf, mw->local + pos, size);
	rc = copy_to_user(ubuf, buf, size);
	if (rc == size) {
		rc = -EFAULT;
		goto err_free;
	}

	size -= rc;
	*offp = pos + size;
	rc = size;

err_free:
	kfree(buf);

	return rc;
}

static ssize_t tool_mw_write(struct file *filep, const char __user *ubuf,
			     size_t size, loff_t *offp)
{
	struct tool_mw *mw = filep->private_data;
	ssize_t rc;
	loff_t pos = *offp;
	void *buf;

	if (pos < 0)
		return -EINVAL;
	if (pos >= mw->win_size || !size)
		return 0;
	if (size > mw->win_size - pos)
		size = mw->win_size - pos;

	buf = kmalloc(size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	rc = copy_from_user(buf, ubuf, size);
	if (rc == size) {
		rc = -EFAULT;
		goto err_free;
	}

	size -= rc;
	*offp = pos + size;
	rc = size;

	memcpy_toio(mw->local + pos, buf, size);

err_free:
	kfree(buf);

	return rc;
}

static TOOL_FOPS_RDWR(tool_mw_fops,
		      tool_mw_read,
		      tool_mw_write);

static ssize_t tool_peer_mw_read(struct file *filep, char __user *ubuf,
				 size_t size, loff_t *offp)
{
	struct tool_mw *mw = filep->private_data;

	if (!mw->peer)
		return -ENXIO;

	return simple_read_from_buffer(ubuf, size, offp, mw->peer, mw->size);
}

static ssize_t tool_peer_mw_write(struct file *filep, const char __user *ubuf,
				  size_t size, loff_t *offp)
{
	struct tool_mw *mw = filep->private_data;

	if (!mw->peer)
		return -ENXIO;

	return simple_write_to_buffer(mw->peer, mw->size, offp, ubuf, size);
}

static TOOL_FOPS_RDWR(tool_peer_mw_fops,
		      tool_peer_mw_read,
		      tool_peer_mw_write);

static int tool_setup_mw(struct tool_ctx *tc, int idx, size_t req_size)
{
	int rc;
	struct tool_mw *mw = &tc->mws[idx];
	resource_size_t size, align_addr, align_size;
	char buf[16];

	if (mw->peer)
		return 0;

	rc = ntb_mw_get_align(tc->ntb, PIDX, idx, &align_addr,
				&align_size, &size);
	if (rc)
		return rc;

	mw->size = min_t(resource_size_t, req_size, size);
	mw->size = round_up(mw->size, align_addr);
	mw->size = round_up(mw->size, align_size);
	mw->peer = dma_alloc_coherent(&tc->ntb->pdev->dev, mw->size,
				      &mw->peer_dma, GFP_KERNEL);

	if (!mw->peer || !IS_ALIGNED(mw->peer_dma, align_addr))
		return -ENOMEM;

	rc = ntb_mw_set_trans(tc->ntb, PIDX, idx, mw->peer_dma, mw->size);
	if (rc)
		goto err_free_dma;

	snprintf(buf, sizeof(buf), "peer_mw%d", idx);
	mw->peer_dbg_file = debugfs_create_file(buf, S_IRUSR | S_IWUSR,
						mw->tc->dbgfs, mw,
						&tool_peer_mw_fops);

	return 0;

err_free_dma:
	dma_free_coherent(&tc->ntb->pdev->dev, mw->size,
			  mw->peer,
			  mw->peer_dma);
	mw->peer = NULL;
	mw->peer_dma = 0;
	mw->size = 0;

	return rc;
}

static void tool_free_mw(struct tool_ctx *tc, int idx)
{
	struct tool_mw *mw = &tc->mws[idx];

	if (mw->peer) {
		ntb_mw_clear_trans(tc->ntb, PIDX, idx);
		dma_free_coherent(&tc->ntb->pdev->dev, mw->size,
				  mw->peer,
				  mw->peer_dma);
	}

	mw->peer = NULL;
	mw->peer_dma = 0;

	debugfs_remove(mw->peer_dbg_file);

	mw->peer_dbg_file = NULL;
}

static ssize_t tool_peer_mw_trans_read(struct file *filep,
				       char __user *ubuf,
				       size_t size, loff_t *offp)
{
	struct tool_mw *mw = filep->private_data;

	char *buf;
	size_t buf_size;
	ssize_t ret, off = 0;

	phys_addr_t base;
	resource_size_t mw_size;
	resource_size_t align_addr;
	resource_size_t align_size;
	resource_size_t max_size;

	buf_size = min_t(size_t, size, 512);

	buf = kmalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ntb_mw_get_align(mw->tc->ntb, PIDX, mw->idx,
			 &align_addr, &align_size, &max_size);
	ntb_peer_mw_get_addr(mw->tc->ntb, mw->idx, &base, &mw_size);

	off += scnprintf(buf + off, buf_size - off,
			 "Peer MW %d Information:\n", mw->idx);

	off += scnprintf(buf + off, buf_size - off,
			 "Physical Address      \t%pa[p]\n",
			 &base);

	off += scnprintf(buf + off, buf_size - off,
			 "Window Size           \t%lld\n",
			 (unsigned long long)mw_size);

	off += scnprintf(buf + off, buf_size - off,
			 "Alignment             \t%lld\n",
			 (unsigned long long)align_addr);

	off += scnprintf(buf + off, buf_size - off,
			 "Size Alignment        \t%lld\n",
			 (unsigned long long)align_size);

	off += scnprintf(buf + off, buf_size - off,
			 "Size Max              \t%lld\n",
			 (unsigned long long)max_size);

	off += scnprintf(buf + off, buf_size - off,
			 "Ready                 \t%c\n",
			 (mw->peer) ? 'Y' : 'N');

	off += scnprintf(buf + off, buf_size - off,
			 "Allocated Size       \t%zd\n",
			 (mw->peer) ? (size_t)mw->size : 0);

	ret = simple_read_from_buffer(ubuf, size, offp, buf, off);
	kfree(buf);
	return ret;
}

static ssize_t tool_peer_mw_trans_write(struct file *filep,
					const char __user *ubuf,
					size_t size, loff_t *offp)
{
	struct tool_mw *mw = filep->private_data;

	char buf[32];
	size_t buf_size;
	unsigned long long val;
	int rc;

	buf_size = min(size, (sizeof(buf) - 1));
	if (copy_from_user(buf, ubuf, buf_size))
		return -EFAULT;

	buf[buf_size] = '\0';

	rc = kstrtoull(buf, 0, &val);
	if (rc)
		return rc;

	tool_free_mw(mw->tc, mw->idx);
	if (val)
		rc = tool_setup_mw(mw->tc, mw->idx, val);

	if (rc)
		return rc;

	return size;
}

static TOOL_FOPS_RDWR(tool_peer_mw_trans_fops,
		      tool_peer_mw_trans_read,
		      tool_peer_mw_trans_write);

static int tool_init_mw(struct tool_ctx *tc, int idx)
{
	struct tool_mw *mw = &tc->mws[idx];
	phys_addr_t base;
	int rc;

	rc = ntb_peer_mw_get_addr(tc->ntb, idx, &base, &mw->win_size);
	if (rc)
		return rc;

	mw->tc = tc;
	mw->idx = idx;
	mw->local = ioremap_wc(base, mw->win_size);
	if (!mw->local)
		return -EFAULT;

	return 0;
}

static void tool_free_mws(struct tool_ctx *tc)
{
	int i;

	for (i = 0; i < tc->mw_count; i++) {
		tool_free_mw(tc, i);

		if (tc->mws[i].local)
			iounmap(tc->mws[i].local);

		tc->mws[i].local = NULL;
	}
}

static void tool_setup_dbgfs(struct tool_ctx *tc)
{
	int i;

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

	debugfs_create_file("link", S_IRUSR | S_IWUSR, tc->dbgfs,
			    tc, &tool_link_fops);

	debugfs_create_file("link_event", S_IWUSR, tc->dbgfs,
			    tc, &tool_link_event_fops);

	for (i = 0; i < tc->mw_count; i++) {
		char buf[30];

		snprintf(buf, sizeof(buf), "mw%d", i);
		debugfs_create_file(buf, S_IRUSR | S_IWUSR, tc->dbgfs,
				    &tc->mws[i], &tool_mw_fops);

		snprintf(buf, sizeof(buf), "peer_trans%d", i);
		debugfs_create_file(buf, S_IRUSR | S_IWUSR, tc->dbgfs,
				    &tc->mws[i], &tool_peer_mw_trans_fops);
	}
}

static int tool_probe(struct ntb_client *self, struct ntb_dev *ntb)
{
	struct tool_ctx *tc;
	int rc;
	int i;

	if (!ntb->ops->mw_set_trans) {
		dev_dbg(&ntb->dev, "need inbound MW based NTB API\n");
		rc = -EINVAL;
		goto err_tc;
	}

	if (ntb_spad_count(ntb) < 1) {
		dev_dbg(&ntb->dev, "no enough scratchpads\n");
		rc = -EINVAL;
		goto err_tc;
	}

	if (ntb_db_is_unsafe(ntb))
		dev_dbg(&ntb->dev, "doorbell is unsafe\n");

	if (ntb_spad_is_unsafe(ntb))
		dev_dbg(&ntb->dev, "scratchpad is unsafe\n");

	if (ntb_peer_port_count(ntb) != NTB_DEF_PEER_CNT)
		dev_warn(&ntb->dev, "multi-port NTB is unsupported\n");

	tc = kzalloc(sizeof(*tc), GFP_KERNEL);
	if (!tc) {
		rc = -ENOMEM;
		goto err_tc;
	}

	tc->ntb = ntb;
	init_waitqueue_head(&tc->link_wq);

	tc->mw_count = min(ntb_mw_count(tc->ntb, PIDX), MAX_MWS);
	for (i = 0; i < tc->mw_count; i++) {
		rc = tool_init_mw(tc, i);
		if (rc)
			goto err_ctx;
	}

	tool_setup_dbgfs(tc);

	rc = ntb_set_ctx(ntb, tc, &tool_ops);
	if (rc)
		goto err_ctx;

	ntb_link_enable(ntb, NTB_SPEED_AUTO, NTB_WIDTH_AUTO);
	ntb_link_event(ntb);

	return 0;

err_ctx:
	tool_free_mws(tc);
	debugfs_remove_recursive(tc->dbgfs);
	kfree(tc);
err_tc:
	return rc;
}

static void tool_remove(struct ntb_client *self, struct ntb_dev *ntb)
{
	struct tool_ctx *tc = ntb->ctx;

	tool_free_mws(tc);

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
