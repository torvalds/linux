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
 * PCIe NTB Pingpong Linux driver
 *
 * Contact Information:
 * Allen Hubbe <Allen.Hubbe@emc.com>
 */

/* Note: load this module with option 'dyndbg=+p' */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/dma-mapping.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/debugfs.h>

#include <linux/ntb.h>

#define DRIVER_NAME			"ntb_pingpong"
#define DRIVER_DESCRIPTION		"PCIe NTB Simple Pingpong Client"

#define DRIVER_LICENSE			"Dual BSD/GPL"
#define DRIVER_VERSION			"1.0"
#define DRIVER_RELDATE			"24 March 2015"
#define DRIVER_AUTHOR			"Allen Hubbe <Allen.Hubbe@emc.com>"

MODULE_LICENSE(DRIVER_LICENSE);
MODULE_VERSION(DRIVER_VERSION);
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESCRIPTION);

static unsigned int unsafe;
module_param(unsafe, uint, 0644);
MODULE_PARM_DESC(unsafe, "Run even though ntb operations may be unsafe");

static unsigned int delay_ms = 1000;
module_param(delay_ms, uint, 0644);
MODULE_PARM_DESC(delay_ms, "Milliseconds to delay the response to peer");

static unsigned long db_init = 0x7;
module_param(db_init, ulong, 0644);
MODULE_PARM_DESC(delay_ms, "Initial doorbell bits to ring on the peer");

struct pp_ctx {
	struct ntb_dev			*ntb;
	u64				db_bits;
	/* synchronize access to db_bits by ping and pong */
	spinlock_t			db_lock;
	struct timer_list		db_timer;
	unsigned long			db_delay;
	struct dentry			*debugfs_node_dir;
	struct dentry			*debugfs_count;
	atomic_t			count;
};

static struct dentry *pp_debugfs_dir;

static void pp_ping(unsigned long ctx)
{
	struct pp_ctx *pp = (void *)ctx;
	unsigned long irqflags;
	u64 db_bits, db_mask;
	u32 spad_rd, spad_wr;

	spin_lock_irqsave(&pp->db_lock, irqflags);
	{
		db_mask = ntb_db_valid_mask(pp->ntb);
		db_bits = ntb_db_read(pp->ntb);

		if (db_bits) {
			dev_dbg(&pp->ntb->dev,
				"Masked pongs %#llx\n",
				db_bits);
			ntb_db_clear(pp->ntb, db_bits);
		}

		db_bits = ((pp->db_bits | db_bits) << 1) & db_mask;

		if (!db_bits)
			db_bits = db_init;

		spad_rd = ntb_spad_read(pp->ntb, 0);
		spad_wr = spad_rd + 1;

		dev_dbg(&pp->ntb->dev,
			"Ping bits %#llx read %#x write %#x\n",
			db_bits, spad_rd, spad_wr);

		ntb_peer_spad_write(pp->ntb, 0, spad_wr);
		ntb_peer_db_set(pp->ntb, db_bits);
		ntb_db_clear_mask(pp->ntb, db_mask);

		pp->db_bits = 0;
	}
	spin_unlock_irqrestore(&pp->db_lock, irqflags);
}

static void pp_link_event(void *ctx)
{
	struct pp_ctx *pp = ctx;

	if (ntb_link_is_up(pp->ntb, NULL, NULL) == 1) {
		dev_dbg(&pp->ntb->dev, "link is up\n");
		pp_ping((unsigned long)pp);
	} else {
		dev_dbg(&pp->ntb->dev, "link is down\n");
		del_timer(&pp->db_timer);
	}
}

static void pp_db_event(void *ctx, int vec)
{
	struct pp_ctx *pp = ctx;
	u64 db_bits, db_mask;
	unsigned long irqflags;

	spin_lock_irqsave(&pp->db_lock, irqflags);
	{
		db_mask = ntb_db_vector_mask(pp->ntb, vec);
		db_bits = db_mask & ntb_db_read(pp->ntb);
		ntb_db_set_mask(pp->ntb, db_mask);
		ntb_db_clear(pp->ntb, db_bits);

		pp->db_bits |= db_bits;

		mod_timer(&pp->db_timer, jiffies + pp->db_delay);

		dev_dbg(&pp->ntb->dev,
			"Pong vec %d bits %#llx\n",
			vec, db_bits);
		atomic_inc(&pp->count);
	}
	spin_unlock_irqrestore(&pp->db_lock, irqflags);
}

static int pp_debugfs_setup(struct pp_ctx *pp)
{
	struct pci_dev *pdev = pp->ntb->pdev;

	if (!pp_debugfs_dir)
		return -ENODEV;

	pp->debugfs_node_dir = debugfs_create_dir(pci_name(pdev),
						  pp_debugfs_dir);
	if (!pp->debugfs_node_dir)
		return -ENODEV;

	pp->debugfs_count = debugfs_create_atomic_t("count", S_IRUSR | S_IWUSR,
						    pp->debugfs_node_dir,
						    &pp->count);
	if (!pp->debugfs_count)
		return -ENODEV;

	return 0;
}

static const struct ntb_ctx_ops pp_ops = {
	.link_event = pp_link_event,
	.db_event = pp_db_event,
};

static int pp_probe(struct ntb_client *client,
		    struct ntb_dev *ntb)
{
	struct pp_ctx *pp;
	int rc;

	if (ntb_db_is_unsafe(ntb)) {
		dev_dbg(&ntb->dev, "doorbell is unsafe\n");
		if (!unsafe) {
			rc = -EINVAL;
			goto err_pp;
		}
	}

	if (ntb_spad_is_unsafe(ntb)) {
		dev_dbg(&ntb->dev, "scratchpad is unsafe\n");
		if (!unsafe) {
			rc = -EINVAL;
			goto err_pp;
		}
	}

	pp = kmalloc(sizeof(*pp), GFP_KERNEL);
	if (!pp) {
		rc = -ENOMEM;
		goto err_pp;
	}

	pp->ntb = ntb;
	pp->db_bits = 0;
	atomic_set(&pp->count, 0);
	spin_lock_init(&pp->db_lock);
	setup_timer(&pp->db_timer, pp_ping, (unsigned long)pp);
	pp->db_delay = msecs_to_jiffies(delay_ms);

	rc = ntb_set_ctx(ntb, pp, &pp_ops);
	if (rc)
		goto err_ctx;

	rc = pp_debugfs_setup(pp);
	if (rc)
		goto err_ctx;

	ntb_link_enable(ntb, NTB_SPEED_AUTO, NTB_WIDTH_AUTO);
	ntb_link_event(ntb);

	return 0;

err_ctx:
	kfree(pp);
err_pp:
	return rc;
}

static void pp_remove(struct ntb_client *client,
		      struct ntb_dev *ntb)
{
	struct pp_ctx *pp = ntb->ctx;

	debugfs_remove_recursive(pp->debugfs_node_dir);

	ntb_clear_ctx(ntb);
	del_timer_sync(&pp->db_timer);
	ntb_link_disable(ntb);

	kfree(pp);
}

static struct ntb_client pp_client = {
	.ops = {
		.probe = pp_probe,
		.remove = pp_remove,
	},
};

static int __init pp_init(void)
{
	int rc;

	if (debugfs_initialized())
		pp_debugfs_dir = debugfs_create_dir(KBUILD_MODNAME, NULL);

	rc = ntb_register_client(&pp_client);
	if (rc)
		goto err_client;

	return 0;

err_client:
	debugfs_remove_recursive(pp_debugfs_dir);
	return rc;
}
module_init(pp_init);

static void __exit pp_exit(void)
{
	ntb_unregister_client(&pp_client);
	debugfs_remove_recursive(pp_debugfs_dir);
}
module_exit(pp_exit);
