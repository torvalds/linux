/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2018 Netronome Systems, Inc. */

#ifndef __NFP_ABM_H__
#define __NFP_ABM_H__ 1

#include <net/devlink.h>

struct nfp_app;
struct nfp_net;

#define NFP_ABM_PORTID_TYPE	GENMASK(23, 16)
#define NFP_ABM_PORTID_ID	GENMASK(7, 0)

/**
 * struct nfp_abm - ABM NIC app structure
 * @app:	back pointer to nfp_app
 * @pf_id:	ID of our PF link
 * @eswitch_mode:	devlink eswitch mode, advanced functions only visible
 *			in switchdev mode
 * @q_lvls:	queue level control area
 * @qm_stats:	queue statistics symbol
 */
struct nfp_abm {
	struct nfp_app *app;
	unsigned int pf_id;
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

/**
 * struct nfp_red_qdisc - representation of single RED Qdisc
 * @handle:	handle of currently offloaded RED Qdisc
 * @stats:	statistics from last refresh
 * @xstats:	base of extended statistics
 */
struct nfp_red_qdisc {
	u32 handle;
	struct nfp_alink_stats stats;
	struct nfp_alink_xstats xstats;
};

/**
 * struct nfp_abm_link - port tuple of a ABM NIC
 * @abm:	back pointer to nfp_abm
 * @vnic:	data vNIC
 * @id:		id of the data vNIC
 * @queue_base:	id of base to host queue within PCIe (not QC idx)
 * @total_queues:	number of PF queues
 * @parent:	handle of expected parent, i.e. handle of MQ, or TC_H_ROOT
 * @num_qdiscs:	number of currently used qdiscs
 * @qdiscs:	array of qdiscs
 */
struct nfp_abm_link {
	struct nfp_abm *abm;
	struct nfp_net *vnic;
	unsigned int id;
	unsigned int queue_base;
	unsigned int total_queues;
	u32 parent;
	unsigned int num_qdiscs;
	struct nfp_red_qdisc *qdiscs;
};

void nfp_abm_ctrl_read_params(struct nfp_abm_link *alink);
int nfp_abm_ctrl_find_addrs(struct nfp_abm *abm);
int nfp_abm_ctrl_set_all_q_lvls(struct nfp_abm_link *alink, u32 val);
int nfp_abm_ctrl_set_q_lvl(struct nfp_abm_link *alink, unsigned int i,
			   u32 val);
int nfp_abm_ctrl_read_stats(struct nfp_abm_link *alink,
			    struct nfp_alink_stats *stats);
int nfp_abm_ctrl_read_q_stats(struct nfp_abm_link *alink, unsigned int i,
			      struct nfp_alink_stats *stats);
int nfp_abm_ctrl_read_xstats(struct nfp_abm_link *alink,
			     struct nfp_alink_xstats *xstats);
int nfp_abm_ctrl_read_q_xstats(struct nfp_abm_link *alink, unsigned int i,
			       struct nfp_alink_xstats *xstats);
u64 nfp_abm_ctrl_stat_non_sto(struct nfp_abm_link *alink, unsigned int i);
u64 nfp_abm_ctrl_stat_sto(struct nfp_abm_link *alink, unsigned int i);
int nfp_abm_ctrl_qm_enable(struct nfp_abm *abm);
int nfp_abm_ctrl_qm_disable(struct nfp_abm *abm);
#endif
