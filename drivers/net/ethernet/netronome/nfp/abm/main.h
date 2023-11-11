/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2018 Netronome Systems, Inc. */

#ifndef __NFP_ABM_H__
#define __NFP_ABM_H__ 1

#include <linux/bits.h>
#include <linux/list.h>
#include <linux/radix-tree.h>
#include <net/devlink.h>
#include <net/pkt_cls.h>
#include <net/pkt_sched.h>

/* Dump of 64 PRIOs and 256 REDs seems to take 850us on Xeon v4 @ 2.20GHz;
 * 2.5ms / 400Hz seems more than sufficient for stats resolution.
 */
#define NFP_ABM_STATS_REFRESH_IVAL	(2500 * 1000) /* ns */

#define NFP_ABM_LVL_INFINITY		S32_MAX

struct nfp_app;
struct nfp_net;

#define NFP_ABM_PORTID_TYPE	GENMASK(23, 16)
#define NFP_ABM_PORTID_ID	GENMASK(7, 0)

/* The possible actions if thresholds are exceeded */
enum nfp_abm_q_action {
	/* mark if ECN capable, otherwise drop */
	NFP_ABM_ACT_MARK_DROP		= 0,
	/* mark if ECN capable, otherwise goto QM */
	NFP_ABM_ACT_MARK_QUEUE		= 1,
	NFP_ABM_ACT_DROP		= 2,
	NFP_ABM_ACT_QUEUE		= 3,
	NFP_ABM_ACT_NOQUEUE		= 4,
};

/**
 * struct nfp_abm - ABM NIC app structure
 * @app:	back pointer to nfp_app
 * @pf_id:	ID of our PF link
 *
 * @red_support:	is RED offload supported
 * @num_prios:	number of supported DSCP priorities
 * @num_bands:	number of supported DSCP priority bands
 * @action_mask:	bitmask of supported actions
 *
 * @thresholds:		current threshold configuration
 * @threshold_undef:	bitmap of thresholds which have not been set
 * @actions:		current FW action configuration
 * @num_thresholds:	number of @thresholds and bits in @threshold_undef
 *
 * @prio_map_len:	computed length of FW priority map (in bytes)
 * @dscp_mask:		mask FW will apply on DSCP field
 *
 * @eswitch_mode:	devlink eswitch mode, advanced functions only visible
 *			in switchdev mode
 *
 * @q_lvls:	queue level control area
 * @qm_stats:	queue statistics symbol
 * @q_stats:	basic queue statistics (only in per-band case)
 */
struct nfp_abm {
	struct nfp_app *app;
	unsigned int pf_id;

	unsigned int red_support;
	unsigned int num_prios;
	unsigned int num_bands;
	unsigned int action_mask;

	u32 *thresholds;
	unsigned long *threshold_undef;
	u8 *actions;
	size_t num_thresholds;

	unsigned int prio_map_len;
	u8 dscp_mask;

	enum devlink_eswitch_mode eswitch_mode;

	const struct nfp_rtsym *q_lvls;
	const struct nfp_rtsym *qm_stats;
	const struct nfp_rtsym *q_stats;
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
	NFP_QDISC_GRED,
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
 * @red.num_bands:	Number of valid entries in the @red.band table
 * @red.band:		Per-band array of RED instances
 * @red.band.ecn:		ECN marking is enabled (rather than drop)
 * @red.band.threshold:		ECN marking threshold
 * @red.band.stats:		current stats of the RED Qdisc
 * @red.band.prev_stats:	previously reported @red.stats
 * @red.band.xstats:		extended stats for RED - current
 * @red.band.prev_xstats:	extended stats for RED - previously reported
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
		/* TC_SETUP_QDISC_RED, TC_SETUP_QDISC_GRED */
		struct {
			unsigned int num_bands;

			struct {
				bool ecn;
				u32 threshold;
				struct nfp_alink_stats stats;
				struct nfp_alink_stats prev_stats;
				struct nfp_alink_xstats xstats;
				struct nfp_alink_xstats prev_xstats;
			} band[MAX_DPs];
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
 * @prio_map:		current map of priorities
 * @has_prio:		@prio_map is valid
 *
 * @def_band:		default band to use
 * @dscp_map:		list of DSCP to band mappings
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

	u32 *prio_map;
	bool has_prio;

	u8 def_band;
	struct list_head dscp_map;

	struct nfp_qdisc *root_qdisc;
	struct radix_tree_root qdiscs;
};

static inline bool nfp_abm_has_prio(struct nfp_abm *abm)
{
	return abm->num_bands > 1;
}

static inline bool nfp_abm_has_drop(struct nfp_abm *abm)
{
	return abm->action_mask & BIT(NFP_ABM_ACT_DROP);
}

static inline bool nfp_abm_has_mark(struct nfp_abm *abm)
{
	return abm->action_mask & BIT(NFP_ABM_ACT_MARK_DROP);
}

void nfp_abm_qdisc_offload_update(struct nfp_abm_link *alink);
int nfp_abm_setup_root(struct net_device *netdev, struct nfp_abm_link *alink,
		       struct tc_root_qopt_offload *opt);
int nfp_abm_setup_tc_red(struct net_device *netdev, struct nfp_abm_link *alink,
			 struct tc_red_qopt_offload *opt);
int nfp_abm_setup_tc_mq(struct net_device *netdev, struct nfp_abm_link *alink,
			struct tc_mq_qopt_offload *opt);
int nfp_abm_setup_tc_gred(struct net_device *netdev, struct nfp_abm_link *alink,
			  struct tc_gred_qopt_offload *opt);
int nfp_abm_setup_cls_block(struct net_device *netdev, struct nfp_repr *repr,
			    struct flow_block_offload *opt);

int nfp_abm_ctrl_read_params(struct nfp_abm_link *alink);
int nfp_abm_ctrl_find_addrs(struct nfp_abm *abm);
int __nfp_abm_ctrl_set_q_lvl(struct nfp_abm *abm, unsigned int id, u32 val);
int nfp_abm_ctrl_set_q_lvl(struct nfp_abm_link *alink, unsigned int band,
			   unsigned int queue, u32 val);
int __nfp_abm_ctrl_set_q_act(struct nfp_abm *abm, unsigned int id,
			     enum nfp_abm_q_action act);
int nfp_abm_ctrl_set_q_act(struct nfp_abm_link *alink, unsigned int band,
			   unsigned int queue, enum nfp_abm_q_action act);
int nfp_abm_ctrl_read_q_stats(struct nfp_abm_link *alink,
			      unsigned int band, unsigned int queue,
			      struct nfp_alink_stats *stats);
int nfp_abm_ctrl_read_q_xstats(struct nfp_abm_link *alink,
			       unsigned int band, unsigned int queue,
			       struct nfp_alink_xstats *xstats);
u64 nfp_abm_ctrl_stat_non_sto(struct nfp_abm_link *alink, unsigned int i);
u64 nfp_abm_ctrl_stat_sto(struct nfp_abm_link *alink, unsigned int i);
int nfp_abm_ctrl_qm_enable(struct nfp_abm *abm);
int nfp_abm_ctrl_qm_disable(struct nfp_abm *abm);
void nfp_abm_prio_map_update(struct nfp_abm *abm);
int nfp_abm_ctrl_prio_map_update(struct nfp_abm_link *alink, u32 *packed);
#endif
