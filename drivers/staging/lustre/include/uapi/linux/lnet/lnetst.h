/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011 - 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Seagate, Inc.
 *
 * lnet/include/lnet/lnetst.h
 *
 * Author: Liang Zhen <liang.zhen@intel.com>
 */

#ifndef __LNET_ST_H__
#define __LNET_ST_H__

#include <linux/types.h>

#define LST_FEAT_NONE		(0)
#define LST_FEAT_BULK_LEN	(1 << 0)	/* enable variable page size */

#define LST_FEATS_EMPTY		(LST_FEAT_NONE)
#define LST_FEATS_MASK		(LST_FEAT_NONE | LST_FEAT_BULK_LEN)

#define LST_NAME_SIZE		32	/* max name buffer length */

#define LSTIO_DEBUG		0xC00	/* debug */
#define LSTIO_SESSION_NEW	0xC01	/* create session */
#define LSTIO_SESSION_END	0xC02	/* end session */
#define LSTIO_SESSION_INFO	0xC03	/* query session */
#define LSTIO_GROUP_ADD		0xC10	/* add group */
#define LSTIO_GROUP_LIST	0xC11	/* list all groups in session */
#define LSTIO_GROUP_INFO	0xC12	/* query default information of
					 * specified group
					 */
#define LSTIO_GROUP_DEL		0xC13	/* delete group */
#define LSTIO_NODES_ADD		0xC14	/* add nodes to specified group */
#define LSTIO_GROUP_UPDATE      0xC15	/* update group */
#define LSTIO_BATCH_ADD		0xC20	/* add batch */
#define LSTIO_BATCH_START	0xC21	/* start batch */
#define LSTIO_BATCH_STOP	0xC22	/* stop batch */
#define LSTIO_BATCH_DEL		0xC23	/* delete batch */
#define LSTIO_BATCH_LIST	0xC24	/* show all batches in the session */
#define LSTIO_BATCH_INFO	0xC25	/* show defail of specified batch */
#define LSTIO_TEST_ADD		0xC26	/* add test (to batch) */
#define LSTIO_BATCH_QUERY	0xC27	/* query batch status */
#define LSTIO_STAT_QUERY	0xC30	/* get stats */

struct lst_sid {
	lnet_nid_t	ses_nid;	/* nid of console node */
	__u64		ses_stamp;	/* time stamp */
};					/*** session id */

extern struct lst_sid LST_INVALID_SID;

struct lst_bid {
	__u64	bat_id;		/* unique id in session */
};				/*** batch id (group of tests) */

/* Status of test node */
#define LST_NODE_ACTIVE		0x1	/* node in this session */
#define LST_NODE_BUSY		0x2	/* node is taken by other session */
#define LST_NODE_DOWN		0x4	/* node is down */
#define LST_NODE_UNKNOWN	0x8	/* node not in session */

struct lstcon_node_ent {
	struct lnet_process_id	nde_id;		/* id of node */
	int			nde_state;	/* state of node */
};				/*** node entry, for list_group command */

struct lstcon_ndlist_ent {
	int	nle_nnode;	/* # of nodes */
	int	nle_nactive;	/* # of active nodes */
	int	nle_nbusy;	/* # of busy nodes */
	int	nle_ndown;	/* # of down nodes */
	int	nle_nunknown;	/* # of unknown nodes */
};				/*** node_list entry, for list_batch command */

struct lstcon_test_ent {
	int	tse_type;       /* test type */
	int	tse_loop;       /* loop count */
	int	tse_concur;     /* concurrency of test */
};				/* test summary entry, for
				 * list_batch command
				 */

struct lstcon_batch_ent {
	int	bae_state;	/* batch status */
	int	bae_timeout;	/* batch timeout */
	int	bae_ntest;	/* # of tests in the batch */
};				/* batch summary entry, for
				 * list_batch command
				 */

struct lstcon_test_batch_ent {
	struct lstcon_ndlist_ent   tbe_cli_nle;	/* client (group) node_list
						 * entry
						 */
	struct lstcon_ndlist_ent   tbe_srv_nle;	/* server (group) node_list
						 * entry
						 */
	union {
		struct lstcon_test_ent	tbe_test; /* test entry */
		struct lstcon_batch_ent tbe_batch;/* batch entry */
	} u;
};				/* test/batch verbose information entry,
				 * for list_batch command
				 */

struct lstcon_rpc_ent {
	struct list_head	rpe_link;	/* link chain */
	struct lnet_process_id	rpe_peer;	/* peer's id */
	struct timeval		rpe_stamp;	/* time stamp of RPC */
	int			rpe_state;	/* peer's state */
	int			rpe_rpc_errno;	/* RPC errno */

	struct lst_sid		rpe_sid;	/* peer's session id */
	int			rpe_fwk_errno;	/* framework errno */
	int			rpe_priv[4];	/* private data */
	char			rpe_payload[0];	/* private reply payload */
};

struct lstcon_trans_stat {
	int	trs_rpc_stat[4];	/* RPCs stat (0: total 1: failed
					 * 2: finished
					 * 4: reserved
					 */
	int	trs_rpc_errno;		/* RPC errno */
	int	trs_fwk_stat[8];	/* framework stat */
	int	trs_fwk_errno;		/* errno of the first remote error */
	void	*trs_fwk_private;	/* private framework stat */
};

static inline int
lstcon_rpc_stat_total(struct lstcon_trans_stat *stat, int inc)
{
	return inc ? ++stat->trs_rpc_stat[0] : stat->trs_rpc_stat[0];
}

static inline int
lstcon_rpc_stat_success(struct lstcon_trans_stat *stat, int inc)
{
	return inc ? ++stat->trs_rpc_stat[1] : stat->trs_rpc_stat[1];
}

static inline int
lstcon_rpc_stat_failure(struct lstcon_trans_stat *stat, int inc)
{
	return inc ? ++stat->trs_rpc_stat[2] : stat->trs_rpc_stat[2];
}

static inline int
lstcon_sesop_stat_success(struct lstcon_trans_stat *stat, int inc)
{
	return inc ? ++stat->trs_fwk_stat[0] : stat->trs_fwk_stat[0];
}

static inline int
lstcon_sesop_stat_failure(struct lstcon_trans_stat *stat, int inc)
{
	return inc ? ++stat->trs_fwk_stat[1] : stat->trs_fwk_stat[1];
}

static inline int
lstcon_sesqry_stat_active(struct lstcon_trans_stat *stat, int inc)
{
	return inc ? ++stat->trs_fwk_stat[0] : stat->trs_fwk_stat[0];
}

static inline int
lstcon_sesqry_stat_busy(struct lstcon_trans_stat *stat, int inc)
{
	return inc ? ++stat->trs_fwk_stat[1] : stat->trs_fwk_stat[1];
}

static inline int
lstcon_sesqry_stat_unknown(struct lstcon_trans_stat *stat, int inc)
{
	return inc ? ++stat->trs_fwk_stat[2] : stat->trs_fwk_stat[2];
}

static inline int
lstcon_tsbop_stat_success(struct lstcon_trans_stat *stat, int inc)
{
	return inc ? ++stat->trs_fwk_stat[0] : stat->trs_fwk_stat[0];
}

static inline int
lstcon_tsbop_stat_failure(struct lstcon_trans_stat *stat, int inc)
{
	return inc ? ++stat->trs_fwk_stat[1] : stat->trs_fwk_stat[1];
}

static inline int
lstcon_tsbqry_stat_idle(struct lstcon_trans_stat *stat, int inc)
{
	return inc ? ++stat->trs_fwk_stat[0] : stat->trs_fwk_stat[0];
}

static inline int
lstcon_tsbqry_stat_run(struct lstcon_trans_stat *stat, int inc)
{
	return inc ? ++stat->trs_fwk_stat[1] : stat->trs_fwk_stat[1];
}

static inline int
lstcon_tsbqry_stat_failure(struct lstcon_trans_stat *stat, int inc)
{
	return inc ? ++stat->trs_fwk_stat[2] : stat->trs_fwk_stat[2];
}

static inline int
lstcon_statqry_stat_success(struct lstcon_trans_stat *stat, int inc)
{
	return inc ? ++stat->trs_fwk_stat[0] : stat->trs_fwk_stat[0];
}

static inline int
lstcon_statqry_stat_failure(struct lstcon_trans_stat *stat, int inc)
{
	return inc ? ++stat->trs_fwk_stat[1] : stat->trs_fwk_stat[1];
}

/* create a session */
struct lstio_session_new_args {
	int		 lstio_ses_key;		/* IN: local key */
	int		 lstio_ses_timeout;	/* IN: session timeout */
	int		 lstio_ses_force;	/* IN: force create ? */
	/** IN: session features */
	unsigned int	 lstio_ses_feats;
	struct lst_sid __user *lstio_ses_idp;	/* OUT: session id */
	int		 lstio_ses_nmlen;	/* IN: name length */
	char __user	 *lstio_ses_namep;	/* IN: session name */
};

/* query current session */
struct lstio_session_info_args {
	struct lst_sid __user	*lstio_ses_idp;		/* OUT: session id */
	int __user		*lstio_ses_keyp;	/* OUT: local key */
	/** OUT: session features */
	unsigned int __user	*lstio_ses_featp;
	struct lstcon_ndlist_ent __user *lstio_ses_ndinfo;/* OUT: */
	int			 lstio_ses_nmlen;	/* IN: name length */
	char __user		*lstio_ses_namep;	/* OUT: session name */
};

/* delete a session */
struct lstio_session_end_args {
	int			lstio_ses_key;	/* IN: session key */
};

#define LST_OPC_SESSION		1
#define LST_OPC_GROUP		2
#define LST_OPC_NODES		3
#define LST_OPC_BATCHCLI	4
#define LST_OPC_BATCHSRV	5

struct lstio_debug_args {
	int			 lstio_dbg_key;		/* IN: session key */
	int			 lstio_dbg_type;	/* IN: debug
							 * session|batch|
							 * group|nodes list
							 */
	int			 lstio_dbg_flags;	/* IN: reserved debug
							 * flags
							 */
	int			 lstio_dbg_timeout;	/* IN: timeout of
							 * debug
							 */
	int			 lstio_dbg_nmlen;	/* IN: len of name */
	char __user		*lstio_dbg_namep;	/* IN: name of
							 * group|batch
							 */
	int			 lstio_dbg_count;	/* IN: # of test nodes
							 * to debug
							 */
	struct lnet_process_id __user *lstio_dbg_idsp;	/* IN: id of test
							 * nodes
							 */
	struct list_head __user	*lstio_dbg_resultp;	/* OUT: list head of
							 * result buffer
							 */
};

struct lstio_group_add_args {
	int		 lstio_grp_key;		/* IN: session key */
	int		 lstio_grp_nmlen;	/* IN: name length */
	char __user	*lstio_grp_namep;	/* IN: group name */
};

struct lstio_group_del_args {
	int		 lstio_grp_key;		/* IN: session key */
	int		 lstio_grp_nmlen;	/* IN: name length */
	char __user	*lstio_grp_namep;	/* IN: group name */
};

#define LST_GROUP_CLEAN		1	/* remove inactive nodes in the group */
#define LST_GROUP_REFRESH	2	/* refresh inactive nodes
					 * in the group
					 */
#define LST_GROUP_RMND		3	/* delete nodes from the group */

struct lstio_group_update_args {
	int			 lstio_grp_key;		/* IN: session key */
	int			 lstio_grp_opc;		/* IN: OPC */
	int			 lstio_grp_args;	/* IN: arguments */
	int			 lstio_grp_nmlen;	/* IN: name length */
	char __user		*lstio_grp_namep;	/* IN: group name */
	int			 lstio_grp_count;	/* IN: # of nodes id */
	struct lnet_process_id __user *lstio_grp_idsp;	/* IN: array of nodes */
	struct list_head __user	*lstio_grp_resultp;	/* OUT: list head of
							 * result buffer
							 */
};

struct lstio_group_nodes_args {
	int			 lstio_grp_key;		/* IN: session key */
	int			 lstio_grp_nmlen;	/* IN: name length */
	char __user		*lstio_grp_namep;	/* IN: group name */
	int			 lstio_grp_count;	/* IN: # of nodes */
	/** OUT: session features */
	unsigned int __user	*lstio_grp_featp;
	struct lnet_process_id __user *lstio_grp_idsp;	/* IN: nodes */
	struct list_head __user	*lstio_grp_resultp;	/* OUT: list head of
							 * result buffer
							 */
};

struct lstio_group_list_args {
	int	 lstio_grp_key;		/* IN: session key */
	int	 lstio_grp_idx;		/* IN: group idx */
	int	 lstio_grp_nmlen;	/* IN: name len */
	char __user *lstio_grp_namep;	/* OUT: name */
};

struct lstio_group_info_args {
	int			 lstio_grp_key;		/* IN: session key */
	int			 lstio_grp_nmlen;	/* IN: name len */
	char __user		*lstio_grp_namep;	/* IN: name */
	struct lstcon_ndlist_ent __user *lstio_grp_entp;/* OUT: description
							 * of group
							 */
	int __user		*lstio_grp_idxp;	/* IN/OUT: node index */
	int __user		*lstio_grp_ndentp;	/* IN/OUT: # of nodent */
	struct lstcon_node_ent __user *lstio_grp_dentsp;/* OUT: nodent array */
};

#define LST_DEFAULT_BATCH	"batch"			/* default batch name */

struct lstio_batch_add_args {
	int	 lstio_bat_key;		/* IN: session key */
	int	 lstio_bat_nmlen;	/* IN: name length */
	char __user *lstio_bat_namep;	/* IN: batch name */
};

struct lstio_batch_del_args {
	int	 lstio_bat_key;		/* IN: session key */
	int	 lstio_bat_nmlen;	/* IN: name length */
	char __user *lstio_bat_namep;	/* IN: batch name */
};

struct lstio_batch_run_args {
	int			 lstio_bat_key;		/* IN: session key */
	int			 lstio_bat_timeout;	/* IN: timeout for
							 * the batch
							 */
	int			 lstio_bat_nmlen;	/* IN: name length */
	char __user		*lstio_bat_namep;	/* IN: batch name */
	struct list_head __user	*lstio_bat_resultp;	/* OUT: list head of
							 * result buffer
							 */
};

struct lstio_batch_stop_args {
	int			 lstio_bat_key;		/* IN: session key */
	int			 lstio_bat_force;	/* IN: abort unfinished
							 * test RPC
							 */
	int			 lstio_bat_nmlen;	/* IN: name length */
	char __user		*lstio_bat_namep;	/* IN: batch name */
	struct list_head __user	*lstio_bat_resultp;	/* OUT: list head of
							 * result buffer
							 */
};

struct lstio_batch_query_args {
	int			 lstio_bat_key;		/* IN: session key */
	int			 lstio_bat_testidx;	/* IN: test index */
	int			 lstio_bat_client;	/* IN: we testing
							 * client?
							 */
	int			 lstio_bat_timeout;	/* IN: timeout for
							 * waiting
							 */
	int			 lstio_bat_nmlen;	/* IN: name length */
	char __user		*lstio_bat_namep;	/* IN: batch name */
	struct list_head __user	*lstio_bat_resultp;	/* OUT: list head of
							 * result buffer
							 */
};

struct lstio_batch_list_args {
	int	 lstio_bat_key;		/* IN: session key */
	int	 lstio_bat_idx;		/* IN: index */
	int	 lstio_bat_nmlen;	/* IN: name length */
	char __user *lstio_bat_namep;	/* IN: batch name */
};

struct lstio_batch_info_args {
	int			 lstio_bat_key;		/* IN: session key */
	int			 lstio_bat_nmlen;	/* IN: name length */
	char __user		*lstio_bat_namep;	/* IN: name */
	int			 lstio_bat_server;	/* IN: query server
							 * or not
							 */
	int			 lstio_bat_testidx;	/* IN: test index */
	struct lstcon_test_batch_ent __user *lstio_bat_entp;/* OUT: batch ent */

	int __user		*lstio_bat_idxp;	/* IN/OUT: index of node */
	int __user		*lstio_bat_ndentp;	/* IN/OUT: # of nodent */
	struct lstcon_node_ent __user *lstio_bat_dentsp;/* array of nodent */
};

/* add stat in session */
struct lstio_stat_args {
	int			 lstio_sta_key;		/* IN: session key */
	int			 lstio_sta_timeout;	/* IN: timeout for
							 * stat request
							 */
	int			 lstio_sta_nmlen;	/* IN: group name
							 * length
							 */
	char __user		*lstio_sta_namep;	/* IN: group name */
	int			 lstio_sta_count;	/* IN: # of pid */
	struct lnet_process_id __user *lstio_sta_idsp;	/* IN: pid */
	struct list_head __user	*lstio_sta_resultp;	/* OUT: list head of
							 * result buffer
							 */
};

enum lst_test_type {
	LST_TEST_BULK	= 1,
	LST_TEST_PING	= 2
};

/* create a test in a batch */
#define LST_MAX_CONCUR	1024	/* Max concurrency of test */

struct lstio_test_args {
	int		  lstio_tes_key;	/* IN: session key */
	int		  lstio_tes_bat_nmlen;	/* IN: batch name len */
	char __user	 *lstio_tes_bat_name;	/* IN: batch name */
	int		  lstio_tes_type;	/* IN: test type */
	int		  lstio_tes_oneside;	/* IN: one sided test */
	int		  lstio_tes_loop;	/* IN: loop count */
	int		  lstio_tes_concur;	/* IN: concurrency */

	int		  lstio_tes_dist;	/* IN: node distribution in
						 * destination groups
						 */
	int		  lstio_tes_span;	/* IN: node span in
						 * destination groups
						 */
	int		  lstio_tes_sgrp_nmlen;	/* IN: source group
						 * name length
						 */
	char __user	 *lstio_tes_sgrp_name;	/* IN: group name */
	int		  lstio_tes_dgrp_nmlen;	/* IN: destination group
						 * name length
						 */
	char __user	 *lstio_tes_dgrp_name;	/* IN: group name */

	int		  lstio_tes_param_len;	/* IN: param buffer len */
	void __user	 *lstio_tes_param;	/* IN: parameter for specified
						 * test: lstio_bulk_param_t,
						 * lstio_ping_param_t,
						 * ... more
						 */
	int __user	 *lstio_tes_retp;	/* OUT: private returned
						 * value
						 */
	struct list_head __user *lstio_tes_resultp;/* OUT: list head of
						    * result buffer
						    */
};

enum lst_brw_type {
	LST_BRW_READ	= 1,
	LST_BRW_WRITE	= 2
};

enum lst_brw_flags {
	LST_BRW_CHECK_NONE	= 1,
	LST_BRW_CHECK_SIMPLE	= 2,
	LST_BRW_CHECK_FULL	= 3
};

struct lst_test_bulk_param {
	int	blk_opc;	/* bulk operation code */
	int	blk_size;       /* size (bytes) */
	int	blk_time;       /* time of running the test*/
	int	blk_flags;      /* reserved flags */
	int	blk_cli_off;	/* bulk offset on client */
	int	blk_srv_off;	/* reserved: bulk offset on server */
};

struct lst_test_ping_param {
	int	png_size;	/* size of ping message */
	int	png_time;	/* time */
	int	png_loop;	/* loop */
	int	png_flags;	/* reserved flags */
};

struct srpc_counters {
	__u32 errors;
	__u32 rpcs_sent;
	__u32 rpcs_rcvd;
	__u32 rpcs_dropped;
	__u32 rpcs_expired;
	__u64 bulk_get;
	__u64 bulk_put;
} WIRE_ATTR;

struct sfw_counters {
	/** milliseconds since current session started */
	__u32 running_ms;
	__u32 active_batches;
	__u32 zombie_sessions;
	__u32 brw_errors;
	__u32 ping_errors;
} WIRE_ATTR;

#endif
