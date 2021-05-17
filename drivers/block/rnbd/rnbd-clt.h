/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * RDMA Network Block Driver
 *
 * Copyright (c) 2014 - 2018 ProfitBricks GmbH. All rights reserved.
 * Copyright (c) 2018 - 2019 1&1 IONOS Cloud GmbH. All rights reserved.
 * Copyright (c) 2019 - 2020 1&1 IONOS SE. All rights reserved.
 */

#ifndef RNBD_CLT_H
#define RNBD_CLT_H

#include <linux/wait.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/blk-mq.h>
#include <linux/refcount.h>

#include <rtrs.h>
#include "rnbd-proto.h"
#include "rnbd-log.h"

/* Max. number of segments per IO request, Mellanox Connect X ~ Connect X5,
 * choose minimial 30 for all, minus 1 for internal protocol, so 29.
 */
#define BMAX_SEGMENTS 29
/*  time in seconds between reconnect tries, default to 30 s */
#define RECONNECT_DELAY 30
/*
 * Number of times to reconnect on error before giving up, 0 for * disabled,
 * -1 for forever
 */
#define MAX_RECONNECTS -1

enum rnbd_clt_dev_state {
	DEV_STATE_INIT,
	DEV_STATE_MAPPED,
	DEV_STATE_MAPPED_DISCONNECTED,
	DEV_STATE_UNMAPPED,
};

struct rnbd_iu_comp {
	wait_queue_head_t wait;
	int errno;
};

#ifdef CONFIG_ARCH_NO_SG_CHAIN
#define RNBD_INLINE_SG_CNT 0
#else
#define RNBD_INLINE_SG_CNT 2
#endif
#define RNBD_RDMA_SGL_SIZE (sizeof(struct scatterlist) * RNBD_INLINE_SG_CNT)

struct rnbd_iu {
	union {
		struct request *rq; /* for block io */
		void *buf; /* for user messages */
	};
	struct rtrs_permit	*permit;
	union {
		/* use to send msg associated with a dev */
		struct rnbd_clt_dev *dev;
		/* use to send msg associated with a sess */
		struct rnbd_clt_session *sess;
	};
	struct sg_table		sgt;
	struct work_struct	work;
	int			errno;
	struct rnbd_iu_comp	comp;
	atomic_t		refcount;
	struct scatterlist	first_sgl[]; /* must be the last one */
};

struct rnbd_cpu_qlist {
	struct list_head	requeue_list;
	spinlock_t		requeue_lock;
	unsigned int		cpu;
};

struct rnbd_clt_session {
	struct list_head        list;
	struct rtrs_clt        *rtrs;
	wait_queue_head_t       rtrs_waitq;
	bool                    rtrs_ready;
	struct rnbd_cpu_qlist	__percpu
				*cpu_queues;
	DECLARE_BITMAP(cpu_queues_bm, NR_CPUS);
	int	__percpu	*cpu_rr; /* per-cpu var for CPU round-robin */
	atomic_t		busy;
	size_t			queue_depth;
	u32			max_io_size;
	struct blk_mq_tag_set	tag_set;
	u32			nr_poll_queues;
	struct mutex		lock; /* protects state and devs_list */
	struct list_head        devs_list; /* list of struct rnbd_clt_dev */
	refcount_t		refcount;
	char			sessname[NAME_MAX];
	u8			ver; /* protocol version */
};

/**
 * Submission queues.
 */
struct rnbd_queue {
	struct list_head	requeue_list;
	unsigned long		in_list;
	struct rnbd_clt_dev	*dev;
	struct blk_mq_hw_ctx	*hctx;
};

struct rnbd_clt_dev {
	struct rnbd_clt_session	*sess;
	struct request_queue	*queue;
	struct rnbd_queue	*hw_queues;
	u32			device_id;
	/* local Idr index - used to track minor number allocations. */
	u32			clt_device_id;
	struct mutex		lock;
	enum rnbd_clt_dev_state	dev_state;
	char			*pathname;
	enum rnbd_access_mode	access_mode;
	u32			nr_poll_queues;
	bool			read_only;
	bool			rotational;
	bool			wc;
	bool			fua;
	u32			max_hw_sectors;
	u32			max_write_same_sectors;
	u32			max_discard_sectors;
	u32			discard_granularity;
	u32			discard_alignment;
	u16			secure_discard;
	u16			physical_block_size;
	u16			logical_block_size;
	u16			max_segments;
	size_t			nsectors;
	u64			size;		/* device size in bytes */
	struct list_head        list;
	struct gendisk		*gd;
	struct kobject		kobj;
	char			*blk_symlink_name;
	refcount_t		refcount;
	struct work_struct	unmap_on_rmmod_work;
};

/* rnbd-clt.c */

struct rnbd_clt_dev *rnbd_clt_map_device(const char *sessname,
					   struct rtrs_addr *paths,
					   size_t path_cnt, u16 port_nr,
					   const char *pathname,
					   enum rnbd_access_mode access_mode,
					   u32 nr_poll_queues);
int rnbd_clt_unmap_device(struct rnbd_clt_dev *dev, bool force,
			   const struct attribute *sysfs_self);

int rnbd_clt_remap_device(struct rnbd_clt_dev *dev);
int rnbd_clt_resize_disk(struct rnbd_clt_dev *dev, size_t newsize);

/* rnbd-clt-sysfs.c */

int rnbd_clt_create_sysfs_files(void);

void rnbd_clt_destroy_sysfs_files(void);

void rnbd_clt_remove_dev_symlink(struct rnbd_clt_dev *dev);

#endif /* RNBD_CLT_H */
