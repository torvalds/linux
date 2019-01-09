/*
 *   This file is provided under a dual BSD/GPLv2 license.  When using or
 *   redistributing this file, you may do so under either license.
 *
 *   GPL LICENSE SUMMARY
 *
 *   Copyright (C) 2015 EMC Corporation. All Rights Reserved.
 *   Copyright (C) 2017 T-Platforms. All Rights Reserved.
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
 *   Copyright (C) 2017 T-Platforms. All Rights Reserved.
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
 * PCIe NTB Pingpong Linux driver
 */

/*
 * How to use this tool, by example.
 *
 * Assuming $DBG_DIR is something like:
 * '/sys/kernel/debug/ntb_perf/0000:00:03.0'
 * Suppose aside from local device there is at least one remote device
 * connected to NTB with index 0.
 *-----------------------------------------------------------------------------
 * Eg: install driver with specified delay between doorbell event and response
 *
 * root@self# insmod ntb_pingpong.ko delay_ms=1000
 *-----------------------------------------------------------------------------
 * Eg: get number of ping-pong cycles performed
 *
 * root@self# cat $DBG_DIR/count
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/bitops.h>

#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/hrtimer.h>
#include <linux/debugfs.h>

#include <linux/ntb.h>

#define DRIVER_NAME		"ntb_pingpong"
#define DRIVER_VERSION		"2.0"

MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION(DRIVER_VERSION);
MODULE_AUTHOR("Allen Hubbe <Allen.Hubbe@emc.com>");
MODULE_DESCRIPTION("PCIe NTB Simple Pingpong Client");

static unsigned int unsafe;
module_param(unsafe, uint, 0644);
MODULE_PARM_DESC(unsafe, "Run even though ntb operations may be unsafe");

static unsigned int delay_ms = 1000;
module_param(delay_ms, uint, 0644);
MODULE_PARM_DESC(delay_ms, "Milliseconds to delay the response to peer");

struct pp_ctx {
	struct ntb_dev *ntb;
	struct hrtimer timer;
	u64 in_db;
	u64 out_db;
	int out_pidx;
	u64 nmask;
	u64 pmask;
	atomic_t count;
	spinlock_t lock;
	struct dentry *dbgfs_dir;
};
#define to_pp_timer(__timer) \
	container_of(__timer, struct pp_ctx, timer)

static struct dentry *pp_dbgfs_topdir;

static int pp_find_next_peer(struct pp_ctx *pp)
{
	u64 link, out_db;
	int pidx;

	link = ntb_link_is_up(pp->ntb, NULL, NULL);

	/* Find next available peer */
	if (link & pp->nmask)
		pidx = __ffs64(link & pp->nmask);
	else if (link & pp->pmask)
		pidx = __ffs64(link & pp->pmask);
	else
		return -ENODEV;

	out_db = BIT_ULL(ntb_peer_port_number(pp->ntb, pidx));

	spin_lock(&pp->lock);
	pp->out_pidx = pidx;
	pp->out_db = out_db;
	spin_unlock(&pp->lock);

	return 0;
}

static void pp_setup(struct pp_ctx *pp)
{
	int ret;

	ntb_db_set_mask(pp->ntb, pp->in_db);

	hrtimer_cancel(&pp->timer);

	ret = pp_find_next_peer(pp);
	if (ret == -ENODEV) {
		dev_dbg(&pp->ntb->dev, "Got no peers, so cancel\n");
		return;
	}

	dev_dbg(&pp->ntb->dev, "Ping-pong started with port %d, db %#llx\n",
		ntb_peer_port_number(pp->ntb, pp->out_pidx), pp->out_db);

	hrtimer_start(&pp->timer, ms_to_ktime(delay_ms), HRTIMER_MODE_REL);
}

static void pp_clear(struct pp_ctx *pp)
{
	hrtimer_cancel(&pp->timer);

	ntb_db_set_mask(pp->ntb, pp->in_db);

	dev_dbg(&pp->ntb->dev, "Ping-pong cancelled\n");
}

static void pp_ping(struct pp_ctx *pp)
{
	u32 count;

	count = atomic_read(&pp->count);

	spin_lock(&pp->lock);
	ntb_peer_spad_write(pp->ntb, pp->out_pidx, 0, count);
	ntb_peer_msg_write(pp->ntb, pp->out_pidx, 0, count);

	dev_dbg(&pp->ntb->dev, "Ping port %d spad %#x, msg %#x\n",
		ntb_peer_port_number(pp->ntb, pp->out_pidx), count, count);

	ntb_peer_db_set(pp->ntb, pp->out_db);
	ntb_db_clear_mask(pp->ntb, pp->in_db);
	spin_unlock(&pp->lock);
}

static void pp_pong(struct pp_ctx *pp)
{
	u32 msg_data = -1, spad_data = -1;
	int pidx = 0;

	/* Read pong data */
	spad_data = ntb_spad_read(pp->ntb, 0);
	msg_data = ntb_msg_read(pp->ntb, &pidx, 0);
	ntb_msg_clear_sts(pp->ntb, -1);

	/*
	 * Scratchpad and message data may differ, since message register can't
	 * be rewritten unless status is cleared. Additionally either of them
	 * might be unsupported
	 */
	dev_dbg(&pp->ntb->dev, "Pong spad %#x, msg %#x (port %d)\n",
		spad_data, msg_data, ntb_peer_port_number(pp->ntb, pidx));

	atomic_inc(&pp->count);

	ntb_db_set_mask(pp->ntb, pp->in_db);
	ntb_db_clear(pp->ntb, pp->in_db);

	hrtimer_start(&pp->timer, ms_to_ktime(delay_ms), HRTIMER_MODE_REL);
}

static enum hrtimer_restart pp_timer_func(struct hrtimer *t)
{
	struct pp_ctx *pp = to_pp_timer(t);

	pp_ping(pp);

	return HRTIMER_NORESTART;
}

static void pp_link_event(void *ctx)
{
	struct pp_ctx *pp = ctx;

	pp_setup(pp);
}

static void pp_db_event(void *ctx, int vec)
{
	struct pp_ctx *pp = ctx;

	pp_pong(pp);
}

static const struct ntb_ctx_ops pp_ops = {
	.link_event = pp_link_event,
	.db_event = pp_db_event
};

static int pp_check_ntb(struct ntb_dev *ntb)
{
	u64 pmask;

	if (ntb_db_is_unsafe(ntb)) {
		dev_dbg(&ntb->dev, "Doorbell is unsafe\n");
		if (!unsafe)
			return -EINVAL;
	}

	if (ntb_spad_is_unsafe(ntb)) {
		dev_dbg(&ntb->dev, "Scratchpad is unsafe\n");
		if (!unsafe)
			return -EINVAL;
	}

	pmask = GENMASK_ULL(ntb_peer_port_count(ntb), 0);
	if ((ntb_db_valid_mask(ntb) & pmask) != pmask) {
		dev_err(&ntb->dev, "Unsupported DB configuration\n");
		return -EINVAL;
	}

	if (ntb_spad_count(ntb) < 1 && ntb_msg_count(ntb) < 1) {
		dev_err(&ntb->dev, "Scratchpads and messages unsupported\n");
		return -EINVAL;
	} else if (ntb_spad_count(ntb) < 1) {
		dev_dbg(&ntb->dev, "Scratchpads unsupported\n");
	} else if (ntb_msg_count(ntb) < 1) {
		dev_dbg(&ntb->dev, "Messages unsupported\n");
	}

	return 0;
}

static struct pp_ctx *pp_create_data(struct ntb_dev *ntb)
{
	struct pp_ctx *pp;

	pp = devm_kzalloc(&ntb->dev, sizeof(*pp), GFP_KERNEL);
	if (!pp)
		return ERR_PTR(-ENOMEM);

	pp->ntb = ntb;
	atomic_set(&pp->count, 0);
	spin_lock_init(&pp->lock);
	hrtimer_init(&pp->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	pp->timer.function = pp_timer_func;

	return pp;
}

static void pp_init_flds(struct pp_ctx *pp)
{
	int pidx, lport, pcnt;

	/* Find global port index */
	lport = ntb_port_number(pp->ntb);
	pcnt = ntb_peer_port_count(pp->ntb);
	for (pidx = 0; pidx < pcnt; pidx++) {
		if (lport < ntb_peer_port_number(pp->ntb, pidx))
			break;
	}

	pp->in_db = BIT_ULL(lport);
	pp->pmask = GENMASK_ULL(pidx, 0) >> 1;
	pp->nmask = GENMASK_ULL(pcnt - 1, pidx);

	dev_dbg(&pp->ntb->dev, "Inbound db %#llx, prev %#llx, next %#llx\n",
		pp->in_db, pp->pmask, pp->nmask);
}

static int pp_mask_events(struct pp_ctx *pp)
{
	u64 db_mask, msg_mask;
	int ret;

	db_mask = ntb_db_valid_mask(pp->ntb);
	ret = ntb_db_set_mask(pp->ntb, db_mask);
	if (ret)
		return ret;

	/* Skip message events masking if unsupported */
	if (ntb_msg_count(pp->ntb) < 1)
		return 0;

	msg_mask = ntb_msg_outbits(pp->ntb) | ntb_msg_inbits(pp->ntb);
	return ntb_msg_set_mask(pp->ntb, msg_mask);
}

static int pp_setup_ctx(struct pp_ctx *pp)
{
	int ret;

	ret = ntb_set_ctx(pp->ntb, pp, &pp_ops);
	if (ret)
		return ret;

	ntb_link_enable(pp->ntb, NTB_SPEED_AUTO, NTB_WIDTH_AUTO);
	/* Might be not necessary */
	ntb_link_event(pp->ntb);

	return 0;
}

static void pp_clear_ctx(struct pp_ctx *pp)
{
	ntb_link_disable(pp->ntb);

	ntb_clear_ctx(pp->ntb);
}

static void pp_setup_dbgfs(struct pp_ctx *pp)
{
	struct pci_dev *pdev = pp->ntb->pdev;
	void *ret;

	pp->dbgfs_dir = debugfs_create_dir(pci_name(pdev), pp_dbgfs_topdir);

	ret = debugfs_create_atomic_t("count", 0600, pp->dbgfs_dir, &pp->count);
	if (!ret)
		dev_warn(&pp->ntb->dev, "DebugFS unsupported\n");
}

static void pp_clear_dbgfs(struct pp_ctx *pp)
{
	debugfs_remove_recursive(pp->dbgfs_dir);
}

static int pp_probe(struct ntb_client *client, struct ntb_dev *ntb)
{
	struct pp_ctx *pp;
	int ret;

	ret = pp_check_ntb(ntb);
	if (ret)
		return ret;

	pp = pp_create_data(ntb);
	if (IS_ERR(pp))
		return PTR_ERR(pp);

	pp_init_flds(pp);

	ret = pp_mask_events(pp);
	if (ret)
		return ret;

	ret = pp_setup_ctx(pp);
	if (ret)
		return ret;

	pp_setup_dbgfs(pp);

	return 0;
}

static void pp_remove(struct ntb_client *client, struct ntb_dev *ntb)
{
	struct pp_ctx *pp = ntb->ctx;

	pp_clear_dbgfs(pp);

	pp_clear_ctx(pp);

	pp_clear(pp);
}

static struct ntb_client pp_client = {
	.ops = {
		.probe = pp_probe,
		.remove = pp_remove
	}
};

static int __init pp_init(void)
{
	int ret;

	if (debugfs_initialized())
		pp_dbgfs_topdir = debugfs_create_dir(KBUILD_MODNAME, NULL);

	ret = ntb_register_client(&pp_client);
	if (ret)
		debugfs_remove_recursive(pp_dbgfs_topdir);

	return ret;
}
module_init(pp_init);

static void __exit pp_exit(void)
{
	ntb_unregister_client(&pp_client);
	debugfs_remove_recursive(pp_dbgfs_topdir);
}
module_exit(pp_exit);
