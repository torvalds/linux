/*
 *  This file is part of the Chelsio T4 Ethernet driver for Linux.
 *  Copyright (C) 2003-2014 Chelsio Communications.  All rights reserved.
 *
 *  Written by Deepak (deepak.s@chelsio.com)
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the LICENSE file included in this
 *  release for licensing terms and conditions.
 */

struct clip_entry {
	spinlock_t lock;	/* Hold while modifying clip reference */
	atomic_t refcnt;
	struct list_head list;
	u32 addr[4];
	int addr_len;
};

struct clip_tbl {
	unsigned int clipt_start;
	unsigned int clipt_size;
	rwlock_t lock;
	atomic_t nfree;
	struct list_head ce_free_head;
	void *cl_list;
	struct list_head hash_list[0];
};

enum {
	CLIPT_MIN_HASH_BUCKETS = 2,
};

struct clip_tbl *t4_init_clip_tbl(unsigned int clipt_start,
				  unsigned int clipt_end);
int cxgb4_clip_get(const struct net_device *dev, const u32 *lip, u8 v6);
void cxgb4_clip_release(const struct net_device *dev, const u32 *lip, u8 v6);
int clip_tbl_show(struct seq_file *seq, void *v);
int cxgb4_update_root_dev_clip(struct net_device *dev);
void t4_cleanup_clip_tbl(struct adapter *adap);
