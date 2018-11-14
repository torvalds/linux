/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2018 Netronome Systems, Inc. */

#ifndef __NFP_ABM_H__
#define __NFP_ABM_H__ 1

#include <linux/bits.h>
#include <linux/radix-tree.h>
#include <net/devlink.h>
#include <net/pkt_cls.h>

/* Dump of 64 PRIOs and 256 REDs seems to take 850us on Xeon v4 @ 2.20GHz;
 * 2.5ms / 400Hz seems more than sufficient for stats resolution.
 */
#define NFP_ABM_STATS_REFRESH_IVAL	(2500 * 1000) /* ns */

#define NFP_ABM_LVL_INFINITY		S32_MAX

struct nfp_app;
struct nfp_net;

#define NFP_ABM_PORTID_TYPE	GENMASK(23, 16)
#define NFP_ABM_PORTID_ID	GENMASK(7, 0)

/**
 * struct nfp_abm - ABM NIC app structure
 * @app:	back pointer to nfp_app
 * @pf_id:	ID of our PF link
 *
 * @thresholds:		current threshold configuration
 * @threshold_undef:	bitmap of thresholds which have not been set
 * @num_thresholds:	number of @thresholds and bits in @threshold_undef
 *
 * @eswitch_mode:	devlink eswitch mode, advanced functions only visible
 *			in switchdev mode
 * @q_lvls:	queue level control area
 * @qm_stats:	queue statistics symbol
 */
struct nfp_abm {
	struct nfp_app *app;
	unsigned int pf_id;

	u32 *thresholds;
	unsigned long *threshold_undef;
	size_t num_thresholds;

	enum devlink_eswitch_mode eswitch_mode;
	const struct nfp_rtsym *q_lvls;
	const struct nfp_rtsym *qm_stats;
};

/**
 * struct nfp_alink_stats - ABM NIC statistics
 * @tx_pkts:		number of TXed packets
 * @tx_bytes:		number of TXed bytes
 * @backlog_pkts:	momentary backlog length (packets)
 * @backlog_bytes:	momentary backlog length (bytes)
 * @overlimits:		number of ECN marked TXed packets (accumulative)
 * @drops:		number of tail-dropped packets (accumulative)
 */
struct nfp_alink_stats {
	u64 tx_pkts;
	u64 tx_bytes;
	u64 backlog_pkts;
	u64 backlog_bytes;
	u64 overlimits;
	u64 drops;
};

/**
 * struct nfp_alink_xstats - extended ABM NIC statistics
 * @ecn_marked:		number of ECN marked TXed packets
 * @pdrop:		number of hard drops due to queue limit
 */
struct nfp_alink_xstats {
	u64 ecn_marked;
	u64 pdrop;
};

enum nfp_qdisc_type {
	NFP_QDISC_NONE = 0,
	NFP_QDISC_MQ,
	NFP_QDISC_RED,
};

#define NFP_QDISC_UNTRACKED	((struct nfp_qdisc *)1UL)

/**
 * struct nfp_qdisc - tracked TC Qdisc
 * @netdev:		netdev on which Qdisc was created
 * @type:		Qdisc type
 * @handle:		handle of this Qdisc
 * @parent_handle:	handle of the parent (unreliable if Qdisc was grafted)
 * @use_cnt:		number of attachment points in the hierarchy
 * @num_children:	current size of the @children array
 * @children:		pointers to children
 *
 * @params_ok:		parameters of this Qdisc are OK for offload
 * @offload_mark:	offload refresh state - selected for offload
 * @offloaded:		Qdisc is currently offloaded to the HW
 *
 * @mq:			MQ Qdisc specific parameters and state
 * @mq.stats:		current stats of the MQ Qdisc
 * @mq.prev_stats:	previously reported @mq.stats
 *
 * @red:		RED Qdisc specific parameters and state
 * @red.threshold:	ECN marking threshold
 * @red.stats:		current stats of the RED Qdisc
 * @red.prev_stats:	previously reported @red.stats
 * @red.xstats:		extended stats for RED - current
 * @red.prev_xstats:	extended stats for RED - previously reported
 */
struct nfp_qdisc {
	struct net_device *netdev;
	enum nfp_qdisc_type type;
	u32 handle;
	u32 parent_handle;
	unsigned int use_cnt;
	unsigned int num_children;
	struct nfp_qdisc **children;

	bool params_ok;
	bool offload_mark;
	bool offloaded;

	union {
		/* NFP_QDISC_MQ */
		struct {
			struct nfp_alink_stats stats;
			struct nfp_alink_stats prev_stats;
		} mq;
		/* TC_SETUP_QDISC_RED */
		struct {
			u32 threshold;
			struct nfp_alink_stats stats;
			struct nfp_alink_stats prev_stats;
			struct nfp_alink_xstats xstats;
			struct nfp_alink_xstats prev_xstats;
		} red;
	};
};

/**
 * struct nfp_abm_link - port tuple of a ABM NIC
 * @abm:	back pointer to nfp_abm
 * @vnic:	data vNIC
 * @id:		id of the data vNIC
 * @queue_base:	id of base to host queue within PCIe (not QC idx)
 * @total_queues:	number of PF queues
 *
 * @last_stats_update:	ktime of last stats update
 *
 * @root_qdisc:	pointer to the current root of the Qdisc hierarchy
 * @qdiscs:	all qdiscs recorded by major part of the handle
 */
struct nfp_abm_link {
	struct nfp_abm *abm;
	struct nfp_net *vnic;
	unsigned int id;
	unsigned int queue_base;
	unsigned int total_queues;

	u64 last_stats_update;

	struct nfp_qdisc *root_qdisc;
	struct radix_tree_root qdiscs;
};

void nfp_abm_qdisc_offload_update(struct nfp_abm_link *alink);
int nfp_abm_setup_root(struct net_device *netdev, struct nfp_abm_link *alink,
		       struct tc_root_qopt_offload *opt);
int nfp_abm_setup_tc_red(struct net_device *netdev, struct nfp_abm_link *alink,
			 struct tc_red_qopt_offload *opt);
int nfp_abm_setup_tc_mq(struct net_device *netdev, struct nfp_abm_link *alink,
			struct tc_mq_qopt_offload *opt);

void nfp_abm_ctrl_read_params(struct nfp_abm_link *alink);
int nfp_abm_ctrl_find_addrs(struct nfp_abm *abm);
int __nfp_abm_ctrl_set_q_lvl(struct nfp_abm *abm, unsigned int id, u32 val);
int nfp_abm_ctrl_set_q_lvl(struct nfp_abm_link *alink, unsigned int queue,
			   u32 val);
int nfp_abm_ctrl_read_q_stats(struct nfp_abm_link *alink, unsigned int i,
			      struct nfp_alink_stats *stats);
int nfp_abm_ctrl_read_q_xstats(struct nfp_abm_link *alink, unsigned int i,
			       struct nfp_alink_xstats *xstats);
u64 nfp_abm_ctrl_stat_non_sto(struct nfp_abm_link *alink, unsigned int i);
u64 nfp_abm_ctrl_stat_sto(struct nfp_abm_link *alink, unsigned int i);
int nfp_abm_ctrl_qm_enable(struct nfp_abm *abm);
int nfp_abm_ctrl_qm_disable(struct nfp_abm *abm);
#endif
