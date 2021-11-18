/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * RDMA Transport Layer
 *
 * Copyright (c) 2014 - 2018 ProfitBricks GmbH. All rights reserved.
 * Copyright (c) 2018 - 2019 1&1 IONOS Cloud GmbH. All rights reserved.
 * Copyright (c) 2019 - 2020 1&1 IONOS SE. All rights reserved.
 */

#ifndef RTRS_SRV_H
#define RTRS_SRV_H

#include <linux/device.h>
#include <linux/refcount.h>
#include "rtrs-pri.h"

/*
 * enum rtrs_srv_state - Server states.
 */
enum rtrs_srv_state {
	RTRS_SRV_CONNECTING,
	RTRS_SRV_CONNECTED,
	RTRS_SRV_CLOSING,
	RTRS_SRV_CLOSED,
};

/* stats for Read and write operation.
 * see Documentation/ABI/testing/sysfs-class-rtrs-server for details
 */
struct rtrs_srv_stats_rdma_stats {
	struct {
		atomic64_t	cnt;
		atomic64_t	size_total;
	} dir[2];
};

struct rtrs_srv_stats {
	struct kobject				kobj_stats;
	struct rtrs_srv_stats_rdma_stats	rdma_stats;
	struct rtrs_srv_sess			*sess;
};

struct rtrs_srv_con {
	struct rtrs_con		c;
	struct list_head	rsp_wr_wait_list;
	spinlock_t		rsp_wr_wait_lock;
};

/* IO context in rtrs_srv, each io has one */
struct rtrs_srv_op {
	struct rtrs_srv_con		*con;
	u32				msg_id;
	u8				dir;
	struct rtrs_msg_rdma_read	*rd_msg;
	struct ib_rdma_wr		tx_wr;
	struct ib_sge			tx_sg;
	struct list_head		wait_list;
	int				status;
};

/*
 * server side memory region context, when always_invalidate=Y, we need
 * queue_depth of memory region to invalidate each memory region.
 */
struct rtrs_srv_mr {
	struct ib_mr	*mr;
	struct sg_table	sgt;
	struct ib_cqe	inv_cqe;	/* only for always_invalidate=true */
	u32		msg_id;		/* only for always_invalidate=true */
	u32		msg_off;	/* only for always_invalidate=true */
	struct rtrs_iu	*iu;		/* send buffer for new rkey msg */
};

struct rtrs_srv_sess {
	struct rtrs_sess	s;
	struct rtrs_srv	*srv;
	struct work_struct	close_work;
	enum rtrs_srv_state	state;
	spinlock_t		state_lock;
	int			cur_cq_vector;
	struct rtrs_srv_op	**ops_ids;
	struct percpu_ref       ids_inflight_ref;
	struct completion       complete_done;
	struct rtrs_srv_mr	*mrs;
	unsigned int		mrs_num;
	dma_addr_t		*dma_addr;
	bool			established;
	unsigned int		mem_bits;
	struct kobject		kobj;
	struct rtrs_srv_stats	*stats;
};

struct rtrs_srv {
	struct list_head	paths_list;
	int			paths_up;
	struct mutex		paths_ev_mutex;
	size_t			paths_num;
	struct mutex		paths_mutex;
	uuid_t			paths_uuid;
	refcount_t		refcount;
	struct rtrs_srv_ctx	*ctx;
	struct list_head	ctx_list;
	void			*priv;
	size_t			queue_depth;
	struct page		**chunks;
	struct device		dev;
	unsigned int		dev_ref;
	struct kobject		*kobj_paths;
};

struct rtrs_srv_ctx {
	struct rtrs_srv_ops ops;
	struct rdma_cm_id *cm_id_ip;
	struct rdma_cm_id *cm_id_ib;
	struct mutex srv_mutex;
	struct list_head srv_list;
};

struct rtrs_srv_ib_ctx {
	struct rtrs_srv_ctx	*srv_ctx;
	u16			port;
	struct mutex            ib_dev_mutex;
	int			ib_dev_count;
};

extern struct class *rtrs_dev_class;

void close_sess(struct rtrs_srv_sess *sess);

static inline void rtrs_srv_update_rdma_stats(struct rtrs_srv_stats *s,
					      size_t size, int d)
{
	atomic64_inc(&s->rdma_stats.dir[d].cnt);
	atomic64_add(size, &s->rdma_stats.dir[d].size_total);
}

/* functions which are implemented in rtrs-srv-stats.c */
int rtrs_srv_reset_rdma_stats(struct rtrs_srv_stats *stats, bool enable);
ssize_t rtrs_srv_stats_rdma_to_str(struct rtrs_srv_stats *stats, char *page);
int rtrs_srv_reset_all_stats(struct rtrs_srv_stats *stats, bool enable);
ssize_t rtrs_srv_reset_all_help(struct rtrs_srv_stats *stats,
				 char *page, size_t len);

/* functions which are implemented in rtrs-srv-sysfs.c */
int rtrs_srv_create_sess_files(struct rtrs_srv_sess *sess);
void rtrs_srv_destroy_sess_files(struct rtrs_srv_sess *sess);

#endif /* RTRS_SRV_H */
