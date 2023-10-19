/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 *   redistributing this file, you may do so under either license.
 *
 *   GPL LICENSE SUMMARY
 *
 *   Copyright (C) 2015 EMC Corporation. All Rights Reserved.
 *   Copyright (C) 2017 T-Platforms All Rights Reserved.
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
 *   Copyright (C) 2017 T-Platforms All Rights Reserved.
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
 */

/*
 * How to use this tool, by example.
 *
 * Assuming $DBG_DIR is something like:
 * '/sys/kernel/debug/ntb_tool/0000:00:03.0'
 * Suppose aside from local device there is at least one remote device
 * connected to NTB with index 0.
 *-----------------------------------------------------------------------------
 * Eg: check local/peer device information.
 *
 * # Get local device port number
 * root@self# cat $DBG_DIR/port
 *
 * # Check local device functionality
 * root@self# ls $DBG_DIR
 * db            msg1          msg_sts     peer4/        port
 * db_event      msg2          peer0/      peer5/        spad0
 * db_mask       msg3          peer1/      peer_db       spad1
 * link          msg_event     peer2/      peer_db_mask  spad2
 * msg0          msg_mask      peer3/      peer_spad     spad3
 * # As one can see it supports:
 * # 1) four inbound message registers
 * # 2) four inbound scratchpads
 * # 3) up to six peer devices
 *
 * # Check peer device port number
 * root@self# cat $DBG_DIR/peer0/port
 *
 * # Check peer device(s) functionality to be used
 * root@self# ls $DBG_DIR/peer0
 * link             mw_trans0       mw_trans6        port
 * link_event       mw_trans1       mw_trans7        spad0
 * msg0             mw_trans2       peer_mw_trans0   spad1
 * msg1             mw_trans3       peer_mw_trans1   spad2
 * msg2             mw_trans4       peer_mw_trans2   spad3
 * msg3             mw_trans5       peer_mw_trans3
 * # As one can see we got:
 * # 1) four outbound message registers
 * # 2) four outbound scratchpads
 * # 3) eight inbound memory windows
 * # 4) four outbound memory windows
 *-----------------------------------------------------------------------------
 * Eg: NTB link tests
 *
 * # Set local link up/down
 * root@self# echo Y > $DBG_DIR/link
 * root@self# echo N > $DBG_DIR/link
 *
 * # Check if link with peer device is up/down:
 * root@self# cat $DBG_DIR/peer0/link
 *
 * # Block until the link is up/down
 * root@self# echo Y > $DBG_DIR/peer0/link_event
 * root@self# echo N > $DBG_DIR/peer0/link_event
 *-----------------------------------------------------------------------------
 * Eg: Doorbell registers tests (some functionality might be absent)
 *
 * # Set/clear/get local doorbell
 * root@self# echo 's 1' > $DBG_DIR/db
 * root@self# echo 'c 1' > $DBG_DIR/db
 * root@self# cat  $DBG_DIR/db
 *
 * # Set/clear/get local doorbell mask
 * root@self# echo 's 1' > $DBG_DIR/db_mask
 * root@self# echo 'c 1' > $DBG_DIR/db_mask
 * root@self# cat $DBG_DIR/db_mask
 *
 * # Ring/clear/get peer doorbell
 * root@peer# echo 's 1' > $DBG_DIR/peer_db
 * root@peer# echo 'c 1' > $DBG_DIR/peer_db
 * root@peer# cat $DBG_DIR/peer_db
 *
 * # Set/clear/get peer doorbell mask
 * root@self# echo 's 1' > $DBG_DIR/peer_db_mask
 * root@self# echo 'c 1' > $DBG_DIR/peer_db_mask
 * root@self# cat $DBG_DIR/peer_db_mask
 *
 * # Block until local doorbell is set with specified value
 * root@self# echo 1 > $DBG_DIR/db_event
 *-----------------------------------------------------------------------------
 * Eg: Message registers tests (functionality might be absent)
 *
 * # Set/clear/get in/out message registers status
 * root@self# echo 's 1' > $DBG_DIR/msg_sts
 * root@self# echo 'c 1' > $DBG_DIR/msg_sts
 * root@self# cat $DBG_DIR/msg_sts
 *
 * # Set/clear in/out message registers mask
 * root@self# echo 's 1' > $DBG_DIR/msg_mask
 * root@self# echo 'c 1' > $DBG_DIR/msg_mask
 *
 * # Get inbound message register #0 value and source of port index
 * root@self# cat  $DBG_DIR/msg0
 *
 * # Send some data to peer over outbound message register #0
 * root@self# echo 0x01020304 > $DBG_DIR/peer0/msg0
 *-----------------------------------------------------------------------------
 * Eg: Scratchpad registers tests (functionality might be absent)
 *
 * # Write/read to/from local scratchpad register #0
 * root@peer# echo 0x01020304 > $DBG_DIR/spad0
 * root@peer# cat $DBG_DIR/spad0
 *
 * # Write/read to/from peer scratchpad register #0
 * root@peer# echo 0x01020304 > $DBG_DIR/peer0/spad0
 * root@peer# cat $DBG_DIR/peer0/spad0
 *-----------------------------------------------------------------------------
 * Eg: Memory windows tests
 *
 * # Create inbound memory window buffer of specified size/get its base address
 * root@peer# echo 16384 > $DBG_DIR/peer0/mw_trans0
 * root@peer# cat $DBG_DIR/peer0/mw_trans0
 *
 * # Write/read data to/from inbound memory window
 * root@peer# echo Hello > $DBG_DIR/peer0/mw0
 * root@peer# head -c 7 $DBG_DIR/peer0/mw0
 *
 * # Map outbound memory window/check it settings (on peer device)
 * root@peer# echo 0xADD0BA5E:16384 > $DBG_DIR/peer0/peer_mw_trans0
 * root@peer# cat $DBG_DIR/peer0/peer_mw_trans0
 *
 * # Write/read data to/from outbound memory window (on peer device)
 * root@peer# echo olleH > $DBG_DIR/peer0/peer_mw0
 * root@peer# head -c 7 $DBG_DIR/peer0/peer_mw0
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

#define DRIVER_NAME		"ntb_tool"
#define DRIVER_VERSION		"2.0"

MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION(DRIVER_VERSION);
MODULE_AUTHOR("Allen Hubbe <Allen.Hubbe@emc.com>");
MODULE_DESCRIPTION("PCIe NTB Debugging Tool");

/*
 * Inbound and outbound memory windows descriptor. Union members selection
 * depends on the MW type the structure describes. mm_base/dma_base are the
 * virtual and DMA address of an inbound MW. io_base/tr_base are the MMIO
 * mapped virtual and xlat addresses of an outbound MW respectively.
 */
struct tool_mw {
	int widx;
	int pidx;
	struct tool_ctx *tc;
	union {
		u8 *mm_base;
		u8 __iomem *io_base;
	};
	union {
		dma_addr_t dma_base;
		u64 tr_base;
	};
	resource_size_t size;
	struct dentry *dbgfs_file;
};

/*
 * Wrapper structure is used to distinguish the outbound MW peers reference
 * within the corresponding DebugFS directory IO operation.
 */
struct tool_mw_wrap {
	int pidx;
	struct tool_mw *mw;
};

struct tool_msg {
	int midx;
	int pidx;
	struct tool_ctx *tc;
};

struct tool_spad {
	int sidx;
	int pidx;
	struct tool_ctx *tc;
};

struct tool_peer {
	int pidx;
	struct tool_ctx *tc;
	int inmw_cnt;
	struct tool_mw *inmws;
	int outmw_cnt;
	struct tool_mw_wrap *outmws;
	int outmsg_cnt;
	struct tool_msg *outmsgs;
	int outspad_cnt;
	struct tool_spad *outspads;
	struct dentry *dbgfs_dir;
};

struct tool_ctx {
	struct ntb_dev *ntb;
	wait_queue_head_t link_wq;
	wait_queue_head_t db_wq;
	wait_queue_head_t msg_wq;
	int outmw_cnt;
	struct tool_mw *outmws;
	int peer_cnt;
	struct tool_peer *peers;
	int inmsg_cnt;
	struct tool_msg *inmsgs;
	int inspad_cnt;
	struct tool_spad *inspads;
	struct dentry *dbgfs_dir;
};

#define TOOL_FOPS_RDWR(__name, __read, __write) \
	const struct file_operations __name = {	\
		.owner = THIS_MODULE,		\
		.open = simple_open,		\
		.read = __read,			\
		.write = __write,		\
	}

#define TOOL_BUF_LEN 32

static struct dentry *tool_dbgfs_topdir;

/*==============================================================================
 *                               NTB events handlers
 *==============================================================================
 */

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

	wake_up(&tc->db_wq);
}

static void tool_msg_event(void *ctx)
{
	struct tool_ctx *tc = ctx;
	u64 msg_sts;

	msg_sts = ntb_msg_read_sts(tc->ntb);

	dev_dbg(&tc->ntb->dev, "message bits %#llx\n", msg_sts);

	wake_up(&tc->msg_wq);
}

static const struct ntb_ctx_ops tool_ops = {
	.link_event = tool_link_event,
	.db_event = tool_db_event,
	.msg_event = tool_msg_event
};

/*==============================================================================
 *                        Common read/write methods
 *==============================================================================
 */

static ssize_t tool_fn_read(struct tool_ctx *tc, char __user *ubuf,
			    size_t size, loff_t *offp,
			    u64 (*fn_read)(struct ntb_dev *))
{
	size_t buf_size;
	char buf[TOOL_BUF_LEN];
	ssize_t pos;

	if (!fn_read)
		return -EINVAL;

	buf_size = min(size, sizeof(buf));

	pos = scnprintf(buf, buf_size, "%#llx\n", fn_read(tc->ntb));

	return simple_read_from_buffer(ubuf, size, offp, buf, pos);
}

static ssize_t tool_fn_write(struct tool_ctx *tc,
			     const char __user *ubuf,
			     size_t size, loff_t *offp,
			     int (*fn_set)(struct ntb_dev *, u64),
			     int (*fn_clear)(struct ntb_dev *, u64))
{
	char *buf, cmd;
	ssize_t ret;
	u64 bits;
	int n;

	if (*offp)
		return 0;

	buf = kmalloc(size + 1, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	if (copy_from_user(buf, ubuf, size)) {
		kfree(buf);
		return -EFAULT;
	}

	buf[size] = 0;

	n = sscanf(buf, "%c %lli", &cmd, &bits);

	kfree(buf);

	if (n != 2) {
		ret = -EINVAL;
	} else if (cmd == 's') {
		if (!fn_set)
			ret = -EINVAL;
		else
			ret = fn_set(tc->ntb, bits);
	} else if (cmd == 'c') {
		if (!fn_clear)
			ret = -EINVAL;
		else
			ret = fn_clear(tc->ntb, bits);
	} else {
		ret = -EINVAL;
	}

	return ret ? : size;
}

/*==============================================================================
 *                            Port read/write methods
 *==============================================================================
 */

static ssize_t tool_port_read(struct file *filep, char __user *ubuf,
			      size_t size, loff_t *offp)
{
	struct tool_ctx *tc = filep->private_data;
	char buf[TOOL_BUF_LEN];
	int pos;

	pos = scnprintf(buf, sizeof(buf), "%d\n", ntb_port_number(tc->ntb));

	return simple_read_from_buffer(ubuf, size, offp, buf, pos);
}

static TOOL_FOPS_RDWR(tool_port_fops,
		      tool_port_read,
		      NULL);

static ssize_t tool_peer_port_read(struct file *filep, char __user *ubuf,
				   size_t size, loff_t *offp)
{
	struct tool_peer *peer = filep->private_data;
	struct tool_ctx *tc = peer->tc;
	char buf[TOOL_BUF_LEN];
	int pos;

	pos = scnprintf(buf, sizeof(buf), "%d\n",
		ntb_peer_port_number(tc->ntb, peer->pidx));

	return simple_read_from_buffer(ubuf, size, offp, buf, pos);
}

static TOOL_FOPS_RDWR(tool_peer_port_fops,
		      tool_peer_port_read,
		      NULL);

static int tool_init_peers(struct tool_ctx *tc)
{
	int pidx;

	tc->peer_cnt = ntb_peer_port_count(tc->ntb);
	tc->peers = devm_kcalloc(&tc->ntb->dev, tc->peer_cnt,
				 sizeof(*tc->peers), GFP_KERNEL);
	if (tc->peers == NULL)
		return -ENOMEM;

	for (pidx = 0; pidx < tc->peer_cnt; pidx++) {
		tc->peers[pidx].pidx = pidx;
		tc->peers[pidx].tc = tc;
	}

	return 0;
}

/*==============================================================================
 *                       Link state read/write methods
 *==============================================================================
 */

static ssize_t tool_link_write(struct file *filep, const char __user *ubuf,
			       size_t size, loff_t *offp)
{
	struct tool_ctx *tc = filep->private_data;
	bool val;
	int ret;

	ret = kstrtobool_from_user(ubuf, size, &val);
	if (ret)
		return ret;

	if (val)
		ret = ntb_link_enable(tc->ntb, NTB_SPEED_AUTO, NTB_WIDTH_AUTO);
	else
		ret = ntb_link_disable(tc->ntb);

	if (ret)
		return ret;

	return size;
}

static TOOL_FOPS_RDWR(tool_link_fops,
		      NULL,
		      tool_link_write);

static ssize_t tool_peer_link_read(struct file *filep, char __user *ubuf,
				   size_t size, loff_t *offp)
{
	struct tool_peer *peer = filep->private_data;
	struct tool_ctx *tc = peer->tc;
	char buf[3];

	if (ntb_link_is_up(tc->ntb, NULL, NULL) & BIT(peer->pidx))
		buf[0] = 'Y';
	else
		buf[0] = 'N';
	buf[1] = '\n';
	buf[2] = '\0';

	return simple_read_from_buffer(ubuf, size, offp, buf, 2);
}

static TOOL_FOPS_RDWR(tool_peer_link_fops,
		      tool_peer_link_read,
		      NULL);

static ssize_t tool_peer_link_event_write(struct file *filep,
					  const char __user *ubuf,
					  size_t size, loff_t *offp)
{
	struct tool_peer *peer = filep->private_data;
	struct tool_ctx *tc = peer->tc;
	u64 link_msk;
	bool val;
	int ret;

	ret = kstrtobool_from_user(ubuf, size, &val);
	if (ret)
		return ret;

	link_msk = BIT_ULL_MASK(peer->pidx);

	if (wait_event_interruptible(tc->link_wq,
		!!(ntb_link_is_up(tc->ntb, NULL, NULL) & link_msk) == val))
		return -ERESTART;

	return size;
}

static TOOL_FOPS_RDWR(tool_peer_link_event_fops,
		      NULL,
		      tool_peer_link_event_write);

/*==============================================================================
 *                  Memory windows read/write/setting methods
 *==============================================================================
 */

static ssize_t tool_mw_read(struct file *filep, char __user *ubuf,
			    size_t size, loff_t *offp)
{
	struct tool_mw *inmw = filep->private_data;

	if (inmw->mm_base == NULL)
		return -ENXIO;

	return simple_read_from_buffer(ubuf, size, offp,
				       inmw->mm_base, inmw->size);
}

static ssize_t tool_mw_write(struct file *filep, const char __user *ubuf,
			     size_t size, loff_t *offp)
{
	struct tool_mw *inmw = filep->private_data;

	if (inmw->mm_base == NULL)
		return -ENXIO;

	return simple_write_to_buffer(inmw->mm_base, inmw->size, offp,
				      ubuf, size);
}

static TOOL_FOPS_RDWR(tool_mw_fops,
		      tool_mw_read,
		      tool_mw_write);

static int tool_setup_mw(struct tool_ctx *tc, int pidx, int widx,
			 size_t req_size)
{
	resource_size_t size, addr_align, size_align;
	struct tool_mw *inmw = &tc->peers[pidx].inmws[widx];
	char buf[TOOL_BUF_LEN];
	int ret;

	if (inmw->mm_base != NULL)
		return 0;

	ret = ntb_mw_get_align(tc->ntb, pidx, widx, &addr_align,
				&size_align, &size);
	if (ret)
		return ret;

	inmw->size = min_t(resource_size_t, req_size, size);
	inmw->size = round_up(inmw->size, addr_align);
	inmw->size = round_up(inmw->size, size_align);
	inmw->mm_base = dma_alloc_coherent(&tc->ntb->pdev->dev, inmw->size,
					   &inmw->dma_base, GFP_KERNEL);
	if (!inmw->mm_base)
		return -ENOMEM;

	if (!IS_ALIGNED(inmw->dma_base, addr_align)) {
		ret = -ENOMEM;
		goto err_free_dma;
	}

	ret = ntb_mw_set_trans(tc->ntb, pidx, widx, inmw->dma_base, inmw->size);
	if (ret)
		goto err_free_dma;

	snprintf(buf, sizeof(buf), "mw%d", widx);
	inmw->dbgfs_file = debugfs_create_file(buf, 0600,
					       tc->peers[pidx].dbgfs_dir, inmw,
					       &tool_mw_fops);

	return 0;

err_free_dma:
	dma_free_coherent(&tc->ntb->pdev->dev, inmw->size, inmw->mm_base,
			  inmw->dma_base);
	inmw->mm_base = NULL;
	inmw->dma_base = 0;
	inmw->size = 0;

	return ret;
}

static void tool_free_mw(struct tool_ctx *tc, int pidx, int widx)
{
	struct tool_mw *inmw = &tc->peers[pidx].inmws[widx];

	debugfs_remove(inmw->dbgfs_file);

	if (inmw->mm_base != NULL) {
		ntb_mw_clear_trans(tc->ntb, pidx, widx);
		dma_free_coherent(&tc->ntb->pdev->dev, inmw->size,
				  inmw->mm_base, inmw->dma_base);
	}

	inmw->mm_base = NULL;
	inmw->dma_base = 0;
	inmw->size = 0;
	inmw->dbgfs_file = NULL;
}

static ssize_t tool_mw_trans_read(struct file *filep, char __user *ubuf,
				  size_t size, loff_t *offp)
{
	struct tool_mw *inmw = filep->private_data;
	resource_size_t addr_align;
	resource_size_t size_align;
	resource_size_t size_max;
	ssize_t ret, off = 0;
	size_t buf_size;
	char *buf;

	buf_size = min_t(size_t, size, 512);

	buf = kmalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = ntb_mw_get_align(inmw->tc->ntb, inmw->pidx, inmw->widx,
			       &addr_align, &size_align, &size_max);
	if (ret)
		goto err;

	off += scnprintf(buf + off, buf_size - off,
			 "Inbound MW     \t%d\n",
			 inmw->widx);

	off += scnprintf(buf + off, buf_size - off,
			 "Port           \t%d (%d)\n",
			 ntb_peer_port_number(inmw->tc->ntb, inmw->pidx),
			 inmw->pidx);

	off += scnprintf(buf + off, buf_size - off,
			 "Window Address \t0x%pK\n", inmw->mm_base);

	off += scnprintf(buf + off, buf_size - off,
			 "DMA Address    \t%pad\n",
			 &inmw->dma_base);

	off += scnprintf(buf + off, buf_size - off,
			 "Window Size    \t%pap\n",
			 &inmw->size);

	off += scnprintf(buf + off, buf_size - off,
			 "Alignment      \t%pap\n",
			 &addr_align);

	off += scnprintf(buf + off, buf_size - off,
			 "Size Alignment \t%pap\n",
			 &size_align);

	off += scnprintf(buf + off, buf_size - off,
			 "Size Max       \t%pap\n",
			 &size_max);

	ret = simple_read_from_buffer(ubuf, size, offp, buf, off);

err:
	kfree(buf);

	return ret;
}

static ssize_t tool_mw_trans_write(struct file *filep, const char __user *ubuf,
				   size_t size, loff_t *offp)
{
	struct tool_mw *inmw = filep->private_data;
	unsigned int val;
	int ret;

	ret = kstrtouint_from_user(ubuf, size, 0, &val);
	if (ret)
		return ret;

	tool_free_mw(inmw->tc, inmw->pidx, inmw->widx);
	if (val) {
		ret = tool_setup_mw(inmw->tc, inmw->pidx, inmw->widx, val);
		if (ret)
			return ret;
	}

	return size;
}

static TOOL_FOPS_RDWR(tool_mw_trans_fops,
		      tool_mw_trans_read,
		      tool_mw_trans_write);

static ssize_t tool_peer_mw_read(struct file *filep, char __user *ubuf,
				 size_t size, loff_t *offp)
{
	struct tool_mw *outmw = filep->private_data;
	loff_t pos = *offp;
	ssize_t ret;
	void *buf;

	if (outmw->io_base == NULL)
		return -EIO;

	if (pos >= outmw->size || !size)
		return 0;

	if (size > outmw->size - pos)
		size = outmw->size - pos;

	buf = kmalloc(size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	memcpy_fromio(buf, outmw->io_base + pos, size);
	ret = copy_to_user(ubuf, buf, size);
	if (ret == size) {
		ret = -EFAULT;
		goto err_free;
	}

	size -= ret;
	*offp = pos + size;
	ret = size;

err_free:
	kfree(buf);

	return ret;
}

static ssize_t tool_peer_mw_write(struct file *filep, const char __user *ubuf,
				  size_t size, loff_t *offp)
{
	struct tool_mw *outmw = filep->private_data;
	ssize_t ret;
	loff_t pos = *offp;
	void *buf;

	if (outmw->io_base == NULL)
		return -EIO;

	if (pos >= outmw->size || !size)
		return 0;
	if (size > outmw->size - pos)
		size = outmw->size - pos;

	buf = kmalloc(size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	ret = copy_from_user(buf, ubuf, size);
	if (ret == size) {
		ret = -EFAULT;
		goto err_free;
	}

	size -= ret;
	*offp = pos + size;
	ret = size;

	memcpy_toio(outmw->io_base + pos, buf, size);

err_free:
	kfree(buf);

	return ret;
}

static TOOL_FOPS_RDWR(tool_peer_mw_fops,
		      tool_peer_mw_read,
		      tool_peer_mw_write);

static int tool_setup_peer_mw(struct tool_ctx *tc, int pidx, int widx,
			      u64 req_addr, size_t req_size)
{
	struct tool_mw *outmw = &tc->outmws[widx];
	resource_size_t map_size;
	phys_addr_t map_base;
	char buf[TOOL_BUF_LEN];
	int ret;

	if (outmw->io_base != NULL)
		return 0;

	ret = ntb_peer_mw_get_addr(tc->ntb, widx, &map_base, &map_size);
	if (ret)
		return ret;

	ret = ntb_peer_mw_set_trans(tc->ntb, pidx, widx, req_addr, req_size);
	if (ret)
		return ret;

	outmw->io_base = ioremap_wc(map_base, map_size);
	if (outmw->io_base == NULL) {
		ret = -EFAULT;
		goto err_clear_trans;
	}

	outmw->tr_base = req_addr;
	outmw->size = req_size;
	outmw->pidx = pidx;

	snprintf(buf, sizeof(buf), "peer_mw%d", widx);
	outmw->dbgfs_file = debugfs_create_file(buf, 0600,
					       tc->peers[pidx].dbgfs_dir, outmw,
					       &tool_peer_mw_fops);

	return 0;

err_clear_trans:
	ntb_peer_mw_clear_trans(tc->ntb, pidx, widx);

	return ret;
}

static void tool_free_peer_mw(struct tool_ctx *tc, int widx)
{
	struct tool_mw *outmw = &tc->outmws[widx];

	debugfs_remove(outmw->dbgfs_file);

	if (outmw->io_base != NULL) {
		iounmap(tc->outmws[widx].io_base);
		ntb_peer_mw_clear_trans(tc->ntb, outmw->pidx, widx);
	}

	outmw->io_base = NULL;
	outmw->tr_base = 0;
	outmw->size = 0;
	outmw->pidx = -1;
	outmw->dbgfs_file = NULL;
}

static ssize_t tool_peer_mw_trans_read(struct file *filep, char __user *ubuf,
					size_t size, loff_t *offp)
{
	struct tool_mw_wrap *outmw_wrap = filep->private_data;
	struct tool_mw *outmw = outmw_wrap->mw;
	resource_size_t map_size;
	phys_addr_t map_base;
	ssize_t off = 0;
	size_t buf_size;
	char *buf;
	int ret;

	ret = ntb_peer_mw_get_addr(outmw->tc->ntb, outmw->widx,
				  &map_base, &map_size);
	if (ret)
		return ret;

	buf_size = min_t(size_t, size, 512);

	buf = kmalloc(buf_size, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	off += scnprintf(buf + off, buf_size - off,
			 "Outbound MW:        \t%d\n", outmw->widx);

	if (outmw->io_base != NULL) {
		off += scnprintf(buf + off, buf_size - off,
			"Port attached       \t%d (%d)\n",
			ntb_peer_port_number(outmw->tc->ntb, outmw->pidx),
			outmw->pidx);
	} else {
		off += scnprintf(buf + off, buf_size - off,
				 "Port attached       \t-1 (-1)\n");
	}

	off += scnprintf(buf + off, buf_size - off,
			 "Virtual address     \t0x%pK\n", outmw->io_base);

	off += scnprintf(buf + off, buf_size - off,
			 "Phys Address        \t%pap\n", &map_base);

	off += scnprintf(buf + off, buf_size - off,
			 "Mapping Size        \t%pap\n", &map_size);

	off += scnprintf(buf + off, buf_size - off,
			 "Translation Address \t0x%016llx\n", outmw->tr_base);

	off += scnprintf(buf + off, buf_size - off,
			 "Window Size         \t%pap\n", &outmw->size);

	ret = simple_read_from_buffer(ubuf, size, offp, buf, off);
	kfree(buf);

	return ret;
}

static ssize_t tool_peer_mw_trans_write(struct file *filep,
					const char __user *ubuf,
					size_t size, loff_t *offp)
{
	struct tool_mw_wrap *outmw_wrap = filep->private_data;
	struct tool_mw *outmw = outmw_wrap->mw;
	size_t buf_size, wsize;
	char buf[TOOL_BUF_LEN];
	int ret, n;
	u64 addr;

	buf_size = min(size, (sizeof(buf) - 1));
	if (copy_from_user(buf, ubuf, buf_size))
		return -EFAULT;

	buf[buf_size] = '\0';

	n = sscanf(buf, "%lli:%zi", &addr, &wsize);
	if (n != 2)
		return -EINVAL;

	tool_free_peer_mw(outmw->tc, outmw->widx);
	if (wsize) {
		ret = tool_setup_peer_mw(outmw->tc, outmw_wrap->pidx,
					 outmw->widx, addr, wsize);
		if (ret)
			return ret;
	}

	return size;
}

static TOOL_FOPS_RDWR(tool_peer_mw_trans_fops,
		      tool_peer_mw_trans_read,
		      tool_peer_mw_trans_write);

static int tool_init_mws(struct tool_ctx *tc)
{
	int widx, pidx;

	/* Initialize outbound memory windows */
	tc->outmw_cnt = ntb_peer_mw_count(tc->ntb);
	tc->outmws = devm_kcalloc(&tc->ntb->dev, tc->outmw_cnt,
				  sizeof(*tc->outmws), GFP_KERNEL);
	if (tc->outmws == NULL)
		return -ENOMEM;

	for (widx = 0; widx < tc->outmw_cnt; widx++) {
		tc->outmws[widx].widx = widx;
		tc->outmws[widx].pidx = -1;
		tc->outmws[widx].tc = tc;
	}

	/* Initialize inbound memory windows and outbound MWs wrapper */
	for (pidx = 0; pidx < tc->peer_cnt; pidx++) {
		tc->peers[pidx].inmw_cnt = ntb_mw_count(tc->ntb, pidx);
		tc->peers[pidx].inmws =
			devm_kcalloc(&tc->ntb->dev, tc->peers[pidx].inmw_cnt,
				    sizeof(*tc->peers[pidx].inmws), GFP_KERNEL);
		if (tc->peers[pidx].inmws == NULL)
			return -ENOMEM;

		for (widx = 0; widx < tc->peers[pidx].inmw_cnt; widx++) {
			tc->peers[pidx].inmws[widx].widx = widx;
			tc->peers[pidx].inmws[widx].pidx = pidx;
			tc->peers[pidx].inmws[widx].tc = tc;
		}

		tc->peers[pidx].outmw_cnt = ntb_peer_mw_count(tc->ntb);
		tc->peers[pidx].outmws =
			devm_kcalloc(&tc->ntb->dev, tc->peers[pidx].outmw_cnt,
				   sizeof(*tc->peers[pidx].outmws), GFP_KERNEL);

		for (widx = 0; widx < tc->peers[pidx].outmw_cnt; widx++) {
			tc->peers[pidx].outmws[widx].pidx = pidx;
			tc->peers[pidx].outmws[widx].mw = &tc->outmws[widx];
		}
	}

	return 0;
}

static void tool_clear_mws(struct tool_ctx *tc)
{
	int widx, pidx;

	/* Free outbound memory windows */
	for (widx = 0; widx < tc->outmw_cnt; widx++)
		tool_free_peer_mw(tc, widx);

	/* Free outbound memory windows */
	for (pidx = 0; pidx < tc->peer_cnt; pidx++)
		for (widx = 0; widx < tc->peers[pidx].inmw_cnt; widx++)
			tool_free_mw(tc, pidx, widx);
}

/*==============================================================================
 *                       Doorbell read/write methods
 *==============================================================================
 */

static ssize_t tool_db_read(struct file *filep, char __user *ubuf,
			    size_t size, loff_t *offp)
{
	struct tool_ctx *tc = filep->private_data;

	return tool_fn_read(tc, ubuf, size, offp, tc->ntb->ops->db_read);
}

static ssize_t tool_db_write(struct file *filep, const char __user *ubuf,
			     size_t size, loff_t *offp)
{
	struct tool_ctx *tc = filep->private_data;

	return tool_fn_write(tc, ubuf, size, offp, tc->ntb->ops->db_set,
			     tc->ntb->ops->db_clear);
}

static TOOL_FOPS_RDWR(tool_db_fops,
		      tool_db_read,
		      tool_db_write);

static ssize_t tool_db_valid_mask_read(struct file *filep, char __user *ubuf,
				       size_t size, loff_t *offp)
{
	struct tool_ctx *tc = filep->private_data;

	return tool_fn_read(tc, ubuf, size, offp, tc->ntb->ops->db_valid_mask);
}

static TOOL_FOPS_RDWR(tool_db_valid_mask_fops,
		      tool_db_valid_mask_read,
		      NULL);

static ssize_t tool_db_mask_read(struct file *filep, char __user *ubuf,
				 size_t size, loff_t *offp)
{
	struct tool_ctx *tc = filep->private_data;

	return tool_fn_read(tc, ubuf, size, offp, tc->ntb->ops->db_read_mask);
}

static ssize_t tool_db_mask_write(struct file *filep, const char __user *ubuf,
			       size_t size, loff_t *offp)
{
	struct tool_ctx *tc = filep->private_data;

	return tool_fn_write(tc, ubuf, size, offp, tc->ntb->ops->db_set_mask,
			     tc->ntb->ops->db_clear_mask);
}

static TOOL_FOPS_RDWR(tool_db_mask_fops,
		      tool_db_mask_read,
		      tool_db_mask_write);

static ssize_t tool_peer_db_read(struct file *filep, char __user *ubuf,
				 size_t size, loff_t *offp)
{
	struct tool_ctx *tc = filep->private_data;

	return tool_fn_read(tc, ubuf, size, offp, tc->ntb->ops->peer_db_read);
}

static ssize_t tool_peer_db_write(struct file *filep, const char __user *ubuf,
				  size_t size, loff_t *offp)
{
	struct tool_ctx *tc = filep->private_data;

	return tool_fn_write(tc, ubuf, size, offp, tc->ntb->ops->peer_db_set,
			     tc->ntb->ops->peer_db_clear);
}

static TOOL_FOPS_RDWR(tool_peer_db_fops,
		      tool_peer_db_read,
		      tool_peer_db_write);

static ssize_t tool_peer_db_mask_read(struct file *filep, char __user *ubuf,
				   size_t size, loff_t *offp)
{
	struct tool_ctx *tc = filep->private_data;

	return tool_fn_read(tc, ubuf, size, offp,
			    tc->ntb->ops->peer_db_read_mask);
}

static ssize_t tool_peer_db_mask_write(struct file *filep,
				       const char __user *ubuf,
				       size_t size, loff_t *offp)
{
	struct tool_ctx *tc = filep->private_data;

	return tool_fn_write(tc, ubuf, size, offp,
			     tc->ntb->ops->peer_db_set_mask,
			     tc->ntb->ops->peer_db_clear_mask);
}

static TOOL_FOPS_RDWR(tool_peer_db_mask_fops,
		      tool_peer_db_mask_read,
		      tool_peer_db_mask_write);

static ssize_t tool_db_event_write(struct file *filep,
				   const char __user *ubuf,
				   size_t size, loff_t *offp)
{
	struct tool_ctx *tc = filep->private_data;
	u64 val;
	int ret;

	ret = kstrtou64_from_user(ubuf, size, 0, &val);
	if (ret)
		return ret;

	if (wait_event_interruptible(tc->db_wq, ntb_db_read(tc->ntb) == val))
		return -ERESTART;

	return size;
}

static TOOL_FOPS_RDWR(tool_db_event_fops,
		      NULL,
		      tool_db_event_write);

/*==============================================================================
 *                       Scratchpads read/write methods
 *==============================================================================
 */

static ssize_t tool_spad_read(struct file *filep, char __user *ubuf,
			      size_t size, loff_t *offp)
{
	struct tool_spad *spad = filep->private_data;
	char buf[TOOL_BUF_LEN];
	ssize_t pos;

	if (!spad->tc->ntb->ops->spad_read)
		return -EINVAL;

	pos = scnprintf(buf, sizeof(buf), "%#x\n",
		ntb_spad_read(spad->tc->ntb, spad->sidx));

	return simple_read_from_buffer(ubuf, size, offp, buf, pos);
}

static ssize_t tool_spad_write(struct file *filep, const char __user *ubuf,
			       size_t size, loff_t *offp)
{
	struct tool_spad *spad = filep->private_data;
	u32 val;
	int ret;

	if (!spad->tc->ntb->ops->spad_write) {
		dev_dbg(&spad->tc->ntb->dev, "no spad write fn\n");
		return -EINVAL;
	}

	ret = kstrtou32_from_user(ubuf, size, 0, &val);
	if (ret)
		return ret;

	ret = ntb_spad_write(spad->tc->ntb, spad->sidx, val);

	return ret ?: size;
}

static TOOL_FOPS_RDWR(tool_spad_fops,
		      tool_spad_read,
		      tool_spad_write);

static ssize_t tool_peer_spad_read(struct file *filep, char __user *ubuf,
				   size_t size, loff_t *offp)
{
	struct tool_spad *spad = filep->private_data;
	char buf[TOOL_BUF_LEN];
	ssize_t pos;

	if (!spad->tc->ntb->ops->peer_spad_read)
		return -EINVAL;

	pos = scnprintf(buf, sizeof(buf), "%#x\n",
		ntb_peer_spad_read(spad->tc->ntb, spad->pidx, spad->sidx));

	return simple_read_from_buffer(ubuf, size, offp, buf, pos);
}

static ssize_t tool_peer_spad_write(struct file *filep, const char __user *ubuf,
				    size_t size, loff_t *offp)
{
	struct tool_spad *spad = filep->private_data;
	u32 val;
	int ret;

	if (!spad->tc->ntb->ops->peer_spad_write) {
		dev_dbg(&spad->tc->ntb->dev, "no spad write fn\n");
		return -EINVAL;
	}

	ret = kstrtou32_from_user(ubuf, size, 0, &val);
	if (ret)
		return ret;

	ret = ntb_peer_spad_write(spad->tc->ntb, spad->pidx, spad->sidx, val);

	return ret ?: size;
}

static TOOL_FOPS_RDWR(tool_peer_spad_fops,
		      tool_peer_spad_read,
		      tool_peer_spad_write);

static int tool_init_spads(struct tool_ctx *tc)
{
	int sidx, pidx;

	/* Initialize inbound scratchpad structures */
	tc->inspad_cnt = ntb_spad_count(tc->ntb);
	tc->inspads = devm_kcalloc(&tc->ntb->dev, tc->inspad_cnt,
				   sizeof(*tc->inspads), GFP_KERNEL);
	if (tc->inspads == NULL)
		return -ENOMEM;

	for (sidx = 0; sidx < tc->inspad_cnt; sidx++) {
		tc->inspads[sidx].sidx = sidx;
		tc->inspads[sidx].pidx = -1;
		tc->inspads[sidx].tc = tc;
	}

	/* Initialize outbound scratchpad structures */
	for (pidx = 0; pidx < tc->peer_cnt; pidx++) {
		tc->peers[pidx].outspad_cnt = ntb_spad_count(tc->ntb);
		tc->peers[pidx].outspads =
			devm_kcalloc(&tc->ntb->dev, tc->peers[pidx].outspad_cnt,
				sizeof(*tc->peers[pidx].outspads), GFP_KERNEL);
		if (tc->peers[pidx].outspads == NULL)
			return -ENOMEM;

		for (sidx = 0; sidx < tc->peers[pidx].outspad_cnt; sidx++) {
			tc->peers[pidx].outspads[sidx].sidx = sidx;
			tc->peers[pidx].outspads[sidx].pidx = pidx;
			tc->peers[pidx].outspads[sidx].tc = tc;
		}
	}

	return 0;
}

/*==============================================================================
 *                       Messages read/write methods
 *==============================================================================
 */

static ssize_t tool_inmsg_read(struct file *filep, char __user *ubuf,
			       size_t size, loff_t *offp)
{
	struct tool_msg *msg = filep->private_data;
	char buf[TOOL_BUF_LEN];
	ssize_t pos;
	u32 data;
	int pidx;

	data = ntb_msg_read(msg->tc->ntb, &pidx, msg->midx);

	pos = scnprintf(buf, sizeof(buf), "0x%08x<-%d\n", data, pidx);

	return simple_read_from_buffer(ubuf, size, offp, buf, pos);
}

static TOOL_FOPS_RDWR(tool_inmsg_fops,
		      tool_inmsg_read,
		      NULL);

static ssize_t tool_outmsg_write(struct file *filep,
				 const char __user *ubuf,
				 size_t size, loff_t *offp)
{
	struct tool_msg *msg = filep->private_data;
	u32 val;
	int ret;

	ret = kstrtou32_from_user(ubuf, size, 0, &val);
	if (ret)
		return ret;

	ret = ntb_peer_msg_write(msg->tc->ntb, msg->pidx, msg->midx, val);

	return ret ? : size;
}

static TOOL_FOPS_RDWR(tool_outmsg_fops,
		      NULL,
		      tool_outmsg_write);

static ssize_t tool_msg_sts_read(struct file *filep, char __user *ubuf,
				 size_t size, loff_t *offp)
{
	struct tool_ctx *tc = filep->private_data;

	return tool_fn_read(tc, ubuf, size, offp, tc->ntb->ops->msg_read_sts);
}

static ssize_t tool_msg_sts_write(struct file *filep, const char __user *ubuf,
				  size_t size, loff_t *offp)
{
	struct tool_ctx *tc = filep->private_data;

	return tool_fn_write(tc, ubuf, size, offp, NULL,
			     tc->ntb->ops->msg_clear_sts);
}

static TOOL_FOPS_RDWR(tool_msg_sts_fops,
		      tool_msg_sts_read,
		      tool_msg_sts_write);

static ssize_t tool_msg_inbits_read(struct file *filep, char __user *ubuf,
				    size_t size, loff_t *offp)
{
	struct tool_ctx *tc = filep->private_data;

	return tool_fn_read(tc, ubuf, size, offp, tc->ntb->ops->msg_inbits);
}

static TOOL_FOPS_RDWR(tool_msg_inbits_fops,
		      tool_msg_inbits_read,
		      NULL);

static ssize_t tool_msg_outbits_read(struct file *filep, char __user *ubuf,
				     size_t size, loff_t *offp)
{
	struct tool_ctx *tc = filep->private_data;

	return tool_fn_read(tc, ubuf, size, offp, tc->ntb->ops->msg_outbits);
}

static TOOL_FOPS_RDWR(tool_msg_outbits_fops,
		      tool_msg_outbits_read,
		      NULL);

static ssize_t tool_msg_mask_write(struct file *filep, const char __user *ubuf,
				   size_t size, loff_t *offp)
{
	struct tool_ctx *tc = filep->private_data;

	return tool_fn_write(tc, ubuf, size, offp,
			     tc->ntb->ops->msg_set_mask,
			     tc->ntb->ops->msg_clear_mask);
}

static TOOL_FOPS_RDWR(tool_msg_mask_fops,
		      NULL,
		      tool_msg_mask_write);

static ssize_t tool_msg_event_write(struct file *filep,
				    const char __user *ubuf,
				    size_t size, loff_t *offp)
{
	struct tool_ctx *tc = filep->private_data;
	u64 val;
	int ret;

	ret = kstrtou64_from_user(ubuf, size, 0, &val);
	if (ret)
		return ret;

	if (wait_event_interruptible(tc->msg_wq,
		ntb_msg_read_sts(tc->ntb) == val))
		return -ERESTART;

	return size;
}

static TOOL_FOPS_RDWR(tool_msg_event_fops,
		      NULL,
		      tool_msg_event_write);

static int tool_init_msgs(struct tool_ctx *tc)
{
	int midx, pidx;

	/* Initialize inbound message structures */
	tc->inmsg_cnt = ntb_msg_count(tc->ntb);
	tc->inmsgs = devm_kcalloc(&tc->ntb->dev, tc->inmsg_cnt,
				   sizeof(*tc->inmsgs), GFP_KERNEL);
	if (tc->inmsgs == NULL)
		return -ENOMEM;

	for (midx = 0; midx < tc->inmsg_cnt; midx++) {
		tc->inmsgs[midx].midx = midx;
		tc->inmsgs[midx].pidx = -1;
		tc->inmsgs[midx].tc = tc;
	}

	/* Initialize outbound message structures */
	for (pidx = 0; pidx < tc->peer_cnt; pidx++) {
		tc->peers[pidx].outmsg_cnt = ntb_msg_count(tc->ntb);
		tc->peers[pidx].outmsgs =
			devm_kcalloc(&tc->ntb->dev, tc->peers[pidx].outmsg_cnt,
				sizeof(*tc->peers[pidx].outmsgs), GFP_KERNEL);
		if (tc->peers[pidx].outmsgs == NULL)
			return -ENOMEM;

		for (midx = 0; midx < tc->peers[pidx].outmsg_cnt; midx++) {
			tc->peers[pidx].outmsgs[midx].midx = midx;
			tc->peers[pidx].outmsgs[midx].pidx = pidx;
			tc->peers[pidx].outmsgs[midx].tc = tc;
		}
	}

	return 0;
}

/*==============================================================================
 *                          Initialization methods
 *==============================================================================
 */

static struct tool_ctx *tool_create_data(struct ntb_dev *ntb)
{
	struct tool_ctx *tc;

	tc = devm_kzalloc(&ntb->dev, sizeof(*tc), GFP_KERNEL);
	if (tc == NULL)
		return ERR_PTR(-ENOMEM);

	tc->ntb = ntb;
	init_waitqueue_head(&tc->link_wq);
	init_waitqueue_head(&tc->db_wq);
	init_waitqueue_head(&tc->msg_wq);

	if (ntb_db_is_unsafe(ntb))
		dev_dbg(&ntb->dev, "doorbell is unsafe\n");

	if (ntb_spad_is_unsafe(ntb))
		dev_dbg(&ntb->dev, "scratchpad is unsafe\n");

	return tc;
}

static void tool_clear_data(struct tool_ctx *tc)
{
	wake_up(&tc->link_wq);
	wake_up(&tc->db_wq);
	wake_up(&tc->msg_wq);
}

static int tool_init_ntb(struct tool_ctx *tc)
{
	return ntb_set_ctx(tc->ntb, tc, &tool_ops);
}

static void tool_clear_ntb(struct tool_ctx *tc)
{
	ntb_clear_ctx(tc->ntb);
	ntb_link_disable(tc->ntb);
}

static void tool_setup_dbgfs(struct tool_ctx *tc)
{
	int pidx, widx, sidx, midx;
	char buf[TOOL_BUF_LEN];

	/* This modules is useless without dbgfs... */
	if (!tool_dbgfs_topdir) {
		tc->dbgfs_dir = NULL;
		return;
	}

	tc->dbgfs_dir = debugfs_create_dir(dev_name(&tc->ntb->dev),
					   tool_dbgfs_topdir);
	if (!tc->dbgfs_dir)
		return;

	debugfs_create_file("port", 0600, tc->dbgfs_dir,
			    tc, &tool_port_fops);

	debugfs_create_file("link", 0600, tc->dbgfs_dir,
			    tc, &tool_link_fops);

	debugfs_create_file("db", 0600, tc->dbgfs_dir,
			    tc, &tool_db_fops);

	debugfs_create_file("db_valid_mask", 0600, tc->dbgfs_dir,
			    tc, &tool_db_valid_mask_fops);

	debugfs_create_file("db_mask", 0600, tc->dbgfs_dir,
			    tc, &tool_db_mask_fops);

	debugfs_create_file("db_event", 0600, tc->dbgfs_dir,
			    tc, &tool_db_event_fops);

	debugfs_create_file("peer_db", 0600, tc->dbgfs_dir,
			    tc, &tool_peer_db_fops);

	debugfs_create_file("peer_db_mask", 0600, tc->dbgfs_dir,
			    tc, &tool_peer_db_mask_fops);

	if (tc->inspad_cnt != 0) {
		for (sidx = 0; sidx < tc->inspad_cnt; sidx++) {
			snprintf(buf, sizeof(buf), "spad%d", sidx);

			debugfs_create_file(buf, 0600, tc->dbgfs_dir,
					   &tc->inspads[sidx], &tool_spad_fops);
		}
	}

	if (tc->inmsg_cnt != 0) {
		for (midx = 0; midx < tc->inmsg_cnt; midx++) {
			snprintf(buf, sizeof(buf), "msg%d", midx);
			debugfs_create_file(buf, 0600, tc->dbgfs_dir,
					   &tc->inmsgs[midx], &tool_inmsg_fops);
		}

		debugfs_create_file("msg_sts", 0600, tc->dbgfs_dir,
				    tc, &tool_msg_sts_fops);

		debugfs_create_file("msg_inbits", 0600, tc->dbgfs_dir,
				    tc, &tool_msg_inbits_fops);

		debugfs_create_file("msg_outbits", 0600, tc->dbgfs_dir,
				    tc, &tool_msg_outbits_fops);

		debugfs_create_file("msg_mask", 0600, tc->dbgfs_dir,
				    tc, &tool_msg_mask_fops);

		debugfs_create_file("msg_event", 0600, tc->dbgfs_dir,
				    tc, &tool_msg_event_fops);
	}

	for (pidx = 0; pidx < tc->peer_cnt; pidx++) {
		snprintf(buf, sizeof(buf), "peer%d", pidx);
		tc->peers[pidx].dbgfs_dir =
			debugfs_create_dir(buf, tc->dbgfs_dir);

		debugfs_create_file("port", 0600,
				    tc->peers[pidx].dbgfs_dir,
				    &tc->peers[pidx], &tool_peer_port_fops);

		debugfs_create_file("link", 0200,
				    tc->peers[pidx].dbgfs_dir,
				    &tc->peers[pidx], &tool_peer_link_fops);

		debugfs_create_file("link_event", 0200,
				  tc->peers[pidx].dbgfs_dir,
				  &tc->peers[pidx], &tool_peer_link_event_fops);

		for (widx = 0; widx < tc->peers[pidx].inmw_cnt; widx++) {
			snprintf(buf, sizeof(buf), "mw_trans%d", widx);
			debugfs_create_file(buf, 0600,
					    tc->peers[pidx].dbgfs_dir,
					    &tc->peers[pidx].inmws[widx],
					    &tool_mw_trans_fops);
		}

		for (widx = 0; widx < tc->peers[pidx].outmw_cnt; widx++) {
			snprintf(buf, sizeof(buf), "peer_mw_trans%d", widx);
			debugfs_create_file(buf, 0600,
					    tc->peers[pidx].dbgfs_dir,
					    &tc->peers[pidx].outmws[widx],
					    &tool_peer_mw_trans_fops);
		}

		for (sidx = 0; sidx < tc->peers[pidx].outspad_cnt; sidx++) {
			snprintf(buf, sizeof(buf), "spad%d", sidx);

			debugfs_create_file(buf, 0600,
					    tc->peers[pidx].dbgfs_dir,
					    &tc->peers[pidx].outspads[sidx],
					    &tool_peer_spad_fops);
		}

		for (midx = 0; midx < tc->peers[pidx].outmsg_cnt; midx++) {
			snprintf(buf, sizeof(buf), "msg%d", midx);
			debugfs_create_file(buf, 0600,
					    tc->peers[pidx].dbgfs_dir,
					    &tc->peers[pidx].outmsgs[midx],
					    &tool_outmsg_fops);
		}
	}
}

static void tool_clear_dbgfs(struct tool_ctx *tc)
{
	debugfs_remove_recursive(tc->dbgfs_dir);
}

static int tool_probe(struct ntb_client *self, struct ntb_dev *ntb)
{
	struct tool_ctx *tc;
	int ret;

	tc = tool_create_data(ntb);
	if (IS_ERR(tc))
		return PTR_ERR(tc);

	ret = tool_init_peers(tc);
	if (ret != 0)
		goto err_clear_data;

	ret = tool_init_mws(tc);
	if (ret != 0)
		goto err_clear_data;

	ret = tool_init_spads(tc);
	if (ret != 0)
		goto err_clear_mws;

	ret = tool_init_msgs(tc);
	if (ret != 0)
		goto err_clear_mws;

	ret = tool_init_ntb(tc);
	if (ret != 0)
		goto err_clear_mws;

	tool_setup_dbgfs(tc);

	return 0;

err_clear_mws:
	tool_clear_mws(tc);

err_clear_data:
	tool_clear_data(tc);

	return ret;
}

static void tool_remove(struct ntb_client *self, struct ntb_dev *ntb)
{
	struct tool_ctx *tc = ntb->ctx;

	tool_clear_dbgfs(tc);

	tool_clear_ntb(tc);

	tool_clear_mws(tc);

	tool_clear_data(tc);
}

static struct ntb_client tool_client = {
	.ops = {
		.probe = tool_probe,
		.remove = tool_remove,
	}
};

static int __init tool_init(void)
{
	int ret;

	if (debugfs_initialized())
		tool_dbgfs_topdir = debugfs_create_dir(KBUILD_MODNAME, NULL);

	ret = ntb_register_client(&tool_client);
	if (ret)
		debugfs_remove_recursive(tool_dbgfs_topdir);

	return ret;
}
module_init(tool_init);

static void __exit tool_exit(void)
{
	ntb_unregister_client(&tool_client);
	debugfs_remove_recursive(tool_dbgfs_topdir);
}
module_exit(tool_exit);
