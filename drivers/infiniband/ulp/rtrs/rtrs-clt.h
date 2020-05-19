/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * RDMA Transport Layer
 *
 * Copyright (c) 2014 - 2018 ProfitBricks GmbH. All rights reserved.
 * Copyright (c) 2018 - 2019 1&1 IONOS Cloud GmbH. All rights reserved.
 * Copyright (c) 2019 - 2020 1&1 IONOS SE. All rights reserved.
 */

#ifndef RTRS_CLT_H
#define RTRS_CLT_H

#include <linux/device.h>
#include "rtrs-pri.h"

/**
 * enum rtrs_clt_state - Client states.
 */
enum rtrs_clt_state {
	RTRS_CLT_CONNECTING,
	RTRS_CLT_CONNECTING_ERR,
	RTRS_CLT_RECONNECTING,
	RTRS_CLT_CONNECTED,
	RTRS_CLT_CLOSING,
	RTRS_CLT_CLOSED,
	RTRS_CLT_DEAD,
};

enum rtrs_mp_policy {
	MP_POLICY_RR,
	MP_POLICY_MIN_INFLIGHT,
};

/* see Documentation/ABI/testing/sysfs-class-rtrs-client for details */
struct rtrs_clt_stats_reconnects {
	int successful_cnt;
	int fail_cnt;
};

/* see Documentation/ABI/testing/sysfs-class-rtrs-client for details */
struct rtrs_clt_stats_cpu_migr {
	atomic_t from;
	int to;
};

/* stats for Read and write operation.
 * see Documentation/ABI/testing/sysfs-class-rtrs-client for details
 */
struct rtrs_clt_stats_rdma {
	struct {
		u64 cnt;
		u64 size_total;
	} dir[2];

	u64 failover_cnt;
};

struct rtrs_clt_stats_pcpu {
	struct rtrs_clt_stats_cpu_migr		cpu_migr;
	struct rtrs_clt_stats_rdma		rdma;
};

struct rtrs_clt_stats {
	struct kobject				kobj_stats;
	struct rtrs_clt_stats_pcpu    __percpu	*pcpu_stats;
	struct rtrs_clt_stats_reconnects	reconnects;
	atomic_t				inflight;
};

struct rtrs_clt_con {
	struct rtrs_con	c;
	struct rtrs_iu		*rsp_ius;
	u32			queue_size;
	unsigned int		cpu;
	atomic_t		io_cnt;
	int			cm_err;
};

/**
 * rtrs_permit - permits the memory allocation for future RDMA operation.
 *		 Combine with irq pinning to keep IO on same CPU.
 */
struct rtrs_permit {
	enum rtrs_clt_con_type con_type;
	unsigned int cpu_id;
	unsigned int mem_id;
	unsigned int mem_off;
};

/**
 * rtrs_clt_io_req - describes one inflight IO request
 */
struct rtrs_clt_io_req {
	struct list_head        list;
	struct rtrs_iu		*iu;
	struct scatterlist	*sglist; /* list holding user data */
	unsigned int		sg_cnt;
	unsigned int		sg_size;
	unsigned int		data_len;
	unsigned int		usr_len;
	void			*priv;
	bool			in_use;
	struct rtrs_clt_con	*con;
	struct rtrs_sg_desc	*desc;
	struct ib_sge		*sge;
	struct rtrs_permit	*permit;
	enum dma_data_direction dir;
	void			(*conf)(void *priv, int errno);
	unsigned long		start_jiffies;

	struct ib_mr		*mr;
	struct ib_cqe		inv_cqe;
	struct completion	inv_comp;
	int			inv_errno;
	bool			need_inv_comp;
	bool			need_inv;
};

struct rtrs_rbuf {
	u64 addr;
	u32 rkey;
};

struct rtrs_clt_sess {
	struct rtrs_sess	s;
	struct rtrs_clt	*clt;
	wait_queue_head_t	state_wq;
	enum rtrs_clt_state	state;
	atomic_t		connected_cnt;
	struct mutex		init_mutex;
	struct rtrs_clt_io_req	*reqs;
	struct delayed_work	reconnect_dwork;
	struct work_struct	close_work;
	unsigned int		reconnect_attempts;
	bool			established;
	struct rtrs_rbuf	*rbufs;
	size_t			max_io_size;
	u32			max_hdr_size;
	u32			chunk_size;
	size_t			queue_depth;
	u32			max_pages_per_mr;
	int			max_send_sge;
	u32			flags;
	struct kobject		kobj;
	struct rtrs_clt_stats	*stats;
	/* cache hca_port and hca_name to display in sysfs */
	u8			hca_port;
	char                    hca_name[IB_DEVICE_NAME_MAX];
	struct list_head __percpu
				*mp_skip_entry;
};

struct rtrs_clt {
	struct list_head	paths_list; /* rcu protected list */
	size_t			paths_num;
	struct rtrs_clt_sess
	__rcu * __percpu	*pcpu_path;
	uuid_t			paths_uuid;
	int			paths_up;
	struct mutex		paths_mutex;
	struct mutex		paths_ev_mutex;
	char			sessname[NAME_MAX];
	u16			port;
	unsigned int		max_reconnect_attempts;
	unsigned int		reconnect_delay_sec;
	unsigned int		max_segments;
	size_t			max_segment_size;
	void			*permits;
	unsigned long		*permits_map;
	size_t			queue_depth;
	size_t			max_io_size;
	wait_queue_head_t	permits_wait;
	size_t			pdu_sz;
	void			*priv;
	void			(*link_ev)(void *priv,
					   enum rtrs_clt_link_ev ev);
	struct device		dev;
	struct kobject		*kobj_paths;
	enum rtrs_mp_policy	mp_policy;
};

static inline struct rtrs_clt_con *to_clt_con(struct rtrs_con *c)
{
	return container_of(c, struct rtrs_clt_con, c);
}

static inline struct rtrs_clt_sess *to_clt_sess(struct rtrs_sess *s)
{
	return container_of(s, struct rtrs_clt_sess, s);
}

static inline int permit_size(struct rtrs_clt *clt)
{
	return sizeof(struct rtrs_permit) + clt->pdu_sz;
}

static inline struct rtrs_permit *get_permit(struct rtrs_clt *clt, int idx)
{
	return (struct rtrs_permit *)(clt->permits + permit_size(clt) * idx);
}

int rtrs_clt_reconnect_from_sysfs(struct rtrs_clt_sess *sess);
int rtrs_clt_disconnect_from_sysfs(struct rtrs_clt_sess *sess);
int rtrs_clt_create_path_from_sysfs(struct rtrs_clt *clt,
				     struct rtrs_addr *addr);
int rtrs_clt_remove_path_from_sysfs(struct rtrs_clt_sess *sess,
				     const struct attribute *sysfs_self);

void rtrs_clt_set_max_reconnect_attempts(struct rtrs_clt *clt, int value);
int rtrs_clt_get_max_reconnect_attempts(const struct rtrs_clt *clt);
void free_sess(struct rtrs_clt_sess *sess);

/* rtrs-clt-stats.c */

int rtrs_clt_init_stats(struct rtrs_clt_stats *stats);

void rtrs_clt_inc_failover_cnt(struct rtrs_clt_stats *s);

void rtrs_clt_update_wc_stats(struct rtrs_clt_con *con);
void rtrs_clt_update_all_stats(struct rtrs_clt_io_req *req, int dir);

int rtrs_clt_reset_rdma_lat_distr_stats(struct rtrs_clt_stats *stats,
					 bool enable);
ssize_t rtrs_clt_stats_rdma_lat_distr_to_str(struct rtrs_clt_stats *stats,
					      char *page, size_t len);
int rtrs_clt_reset_cpu_migr_stats(struct rtrs_clt_stats *stats, bool enable);
int rtrs_clt_stats_migration_cnt_to_str(struct rtrs_clt_stats *stats, char *buf,
					 size_t len);
int rtrs_clt_reset_reconnects_stat(struct rtrs_clt_stats *stats, bool enable);
int rtrs_clt_stats_reconnects_to_str(struct rtrs_clt_stats *stats, char *buf,
				      size_t len);
int rtrs_clt_reset_wc_comp_stats(struct rtrs_clt_stats *stats, bool enable);
int rtrs_clt_stats_wc_completion_to_str(struct rtrs_clt_stats *stats, char *buf,
					 size_t len);
int rtrs_clt_reset_rdma_stats(struct rtrs_clt_stats *stats, bool enable);
ssize_t rtrs_clt_stats_rdma_to_str(struct rtrs_clt_stats *stats,
				    char *page, size_t len);
int rtrs_clt_reset_all_stats(struct rtrs_clt_stats *stats, bool enable);
ssize_t rtrs_clt_reset_all_help(struct rtrs_clt_stats *stats,
				 char *page, size_t len);

/* rtrs-clt-sysfs.c */

int rtrs_clt_create_sysfs_root_files(struct rtrs_clt *clt);
void rtrs_clt_destroy_sysfs_root_folders(struct rtrs_clt *clt);
void rtrs_clt_destroy_sysfs_root_files(struct rtrs_clt *clt);

int rtrs_clt_create_sess_files(struct rtrs_clt_sess *sess);
void rtrs_clt_destroy_sess_files(struct rtrs_clt_sess *sess,
				  const struct attribute *sysfs_self);

#endif /* RTRS_CLT_H */
