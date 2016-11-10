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
					 * specified group */
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

typedef struct {
	lnet_nid_t	ses_nid;	/* nid of console node */
	__u64		ses_stamp;	/* time stamp */
} lst_sid_t;				/*** session id */

extern lst_sid_t LST_INVALID_SID;

typedef struct {
	__u64	bat_id;		/* unique id in session */
} lst_bid_t;			/*** batch id (group of tests) */

/* Status of test node */
#define LST_NODE_ACTIVE		0x1	/* node in this session */
#define LST_NODE_BUSY		0x2	/* node is taken by other session */
#define LST_NODE_DOWN		0x4	/* node is down */
#define LST_NODE_UNKNOWN	0x8	/* node not in session */

typedef struct {
	lnet_process_id_t       nde_id;		/* id of node */
	int			nde_state;	/* state of node */
} lstcon_node_ent_t;		/*** node entry, for list_group command */

typedef struct {
	int	nle_nnode;	/* # of nodes */
	int	nle_nactive;	/* # of active nodes */
	int	nle_nbusy;	/* # of busy nodes */
	int	nle_ndown;	/* # of down nodes */
	int	nle_nunknown;	/* # of unknown nodes */
} lstcon_ndlist_ent_t;		/*** node_list entry, for list_batch command */

typedef struct {
	int	tse_type;       /* test type */
	int	tse_loop;       /* loop count */
	int	tse_concur;     /* concurrency of test */
} lstcon_test_ent_t;		/*** test summary entry, for
				 *** list_batch command */

typedef struct {
	int	bae_state;	/* batch status */
	int	bae_timeout;	/* batch timeout */
	int	bae_ntest;	/* # of tests in the batch */
} lstcon_batch_ent_t;		/*** batch summary entry, for
				 *** list_batch command */

typedef struct {
	lstcon_ndlist_ent_t     tbe_cli_nle;	/* client (group) node_list
						 * entry */
	lstcon_ndlist_ent_t     tbe_srv_nle;	/* server (group) node_list
						 * entry */
	union {
		lstcon_test_ent_t  tbe_test;	/* test entry */
		lstcon_batch_ent_t tbe_batch;	/* batch entry */
	} u;
} lstcon_test_batch_ent_t;	/*** test/batch verbose information entry,
				 *** for list_batch command */

typedef struct {
	struct list_head	rpe_link;	/* link chain */
	lnet_process_id_t	rpe_peer;	/* peer's id */
	struct timeval		rpe_stamp;	/* time stamp of RPC */
	int			rpe_state;	/* peer's state */
	int			rpe_rpc_errno;	/* RPC errno */

	lst_sid_t		rpe_sid;	/* peer's session id */
	int			rpe_fwk_errno;	/* framework errno */
	int			rpe_priv[4];	/* private data */
	char			rpe_payload[0];	/* private reply payload */
} lstcon_rpc_ent_t;

typedef struct {
	int	trs_rpc_stat[4];	/* RPCs stat (0: total
						      1: failed
						      2: finished
						      4: reserved */
	int	trs_rpc_errno;		/* RPC errno */
	int	trs_fwk_stat[8];	/* framework stat */
	int	trs_fwk_errno;		/* errno of the first remote error */
	void	*trs_fwk_private;	/* private framework stat */
} lstcon_trans_stat_t;

static inline int
lstcon_rpc_stat_total(lstcon_trans_stat_t *stat, int inc)
{
	return inc ? ++stat->trs_rpc_stat[0] : stat->trs_rpc_stat[0];
}

static inline int
lstcon_rpc_stat_success(lstcon_trans_stat_t *stat, int inc)
{
	return inc ? ++stat->trs_rpc_stat[1] : stat->trs_rpc_stat[1];
}

static inline int
lstcon_rpc_stat_failure(lstcon_trans_stat_t *stat, int inc)
{
	return inc ? ++stat->trs_rpc_stat[2] : stat->trs_rpc_stat[2];
}

static inline int
lstcon_sesop_stat_success(lstcon_trans_stat_t *stat, int inc)
{
	return inc ? ++stat->trs_fwk_stat[0] : stat->trs_fwk_stat[0];
}

static inline int
lstcon_sesop_stat_failure(lstcon_trans_stat_t *stat, int inc)
{
	return inc ? ++stat->trs_fwk_stat[1] : stat->trs_fwk_stat[1];
}

static inline int
lstcon_sesqry_stat_active(lstcon_trans_stat_t *stat, int inc)
{
	return inc ? ++stat->trs_fwk_stat[0] : stat->trs_fwk_stat[0];
}

static inline int
lstcon_sesqry_stat_busy(lstcon_trans_stat_t *stat, int inc)
{
	return inc ? ++stat->trs_fwk_stat[1] : stat->trs_fwk_stat[1];
}

static inline int
lstcon_sesqry_stat_unknown(lstcon_trans_stat_t *stat, int inc)
{
	return inc ? ++stat->trs_fwk_stat[2] : stat->trs_fwk_stat[2];
}

static inline int
lstcon_tsbop_stat_success(lstcon_trans_stat_t *stat, int inc)
{
	return inc ? ++stat->trs_fwk_stat[0] : stat->trs_fwk_stat[0];
}

static inline int
lstcon_tsbop_stat_failure(lstcon_trans_stat_t *stat, int inc)
{
	return inc ? ++stat->trs_fwk_stat[1] : stat->trs_fwk_stat[1];
}

static inline int
lstcon_tsbqry_stat_idle(lstcon_trans_stat_t *stat, int inc)
{
	return inc ? ++stat->trs_fwk_stat[0] : stat->trs_fwk_stat[0];
}

static inline int
lstcon_tsbqry_stat_run(lstcon_trans_stat_t *stat, int inc)
{
	return inc ? ++stat->trs_fwk_stat[1] : stat->trs_fwk_stat[1];
}

static inline int
lstcon_tsbqry_stat_failure(lstcon_trans_stat_t *stat, int inc)
{
	return inc ? ++stat->trs_fwk_stat[2] : stat->trs_fwk_stat[2];
}

static inline int
lstcon_statqry_stat_success(lstcon_trans_stat_t *stat, int inc)
{
	return inc ? ++stat->trs_fwk_stat[0] : stat->trs_fwk_stat[0];
}

static inline int
lstcon_statqry_stat_failure(lstcon_trans_stat_t *stat, int inc)
{
	return inc ? ++stat->trs_fwk_stat[1] : stat->trs_fwk_stat[1];
}

/* create a session */
typedef struct {
	int		 lstio_ses_key;		/* IN: local key */
	int		 lstio_ses_timeout;	/* IN: session timeout */
	int		 lstio_ses_force;	/* IN: force create ? */
	/** IN: session features */
	unsigned	 lstio_ses_feats;
	lst_sid_t __user *lstio_ses_idp;	/* OUT: session id */
	int		 lstio_ses_nmlen;	/* IN: name length */
	char __user	 *lstio_ses_namep;	/* IN: session name */
} lstio_session_new_args_t;

/* query current session */
typedef struct {
	lst_sid_t __user	*lstio_ses_idp;		/* OUT: session id */
	int __user		*lstio_ses_keyp;	/* OUT: local key */
	/** OUT: session features */
	unsigned __user		*lstio_ses_featp;
	lstcon_ndlist_ent_t __user *lstio_ses_ndinfo;	/* OUT: */
	int			 lstio_ses_nmlen;	/* IN: name length */
	char __user		*lstio_ses_namep;	/* OUT: session name */
} lstio_session_info_args_t;

/* delete a session */
typedef struct {
	int			lstio_ses_key;	/* IN: session key */
} lstio_session_end_args_t;

#define LST_OPC_SESSION		1
#define LST_OPC_GROUP		2
#define LST_OPC_NODES		3
#define LST_OPC_BATCHCLI	4
#define LST_OPC_BATCHSRV	5

typedef struct {
	int			 lstio_dbg_key;		/* IN: session key */
	int			 lstio_dbg_type;	/* IN: debug
								session|batch|
								group|nodes
								list */
	int			 lstio_dbg_flags;	/* IN: reserved debug
							       flags */
	int			 lstio_dbg_timeout;	/* IN: timeout of
							       debug */
	int			 lstio_dbg_nmlen;	/* IN: len of name */
	char __user		*lstio_dbg_namep;	/* IN: name of
							       group|batch */
	int			 lstio_dbg_count;	/* IN: # of test nodes
							       to debug */
	lnet_process_id_t __user *lstio_dbg_idsp;	/* IN: id of test
							       nodes */
	struct list_head __user	*lstio_dbg_resultp;	/* OUT: list head of
								result buffer */
} lstio_debug_args_t;

typedef struct {
	int		 lstio_grp_key;		/* IN: session key */
	int		 lstio_grp_nmlen;	/* IN: name length */
	char __user	*lstio_grp_namep;	/* IN: group name */
} lstio_group_add_args_t;

typedef struct {
	int		 lstio_grp_key;		/* IN: session key */
	int		 lstio_grp_nmlen;	/* IN: name length */
	char __user	*lstio_grp_namep;	/* IN: group name */
} lstio_group_del_args_t;

#define LST_GROUP_CLEAN		1	/* remove inactive nodes in the group */
#define LST_GROUP_REFRESH	2	/* refresh inactive nodes
					 * in the group */
#define LST_GROUP_RMND		3	/* delete nodes from the group */

typedef struct {
	int			 lstio_grp_key;		/* IN: session key */
	int			 lstio_grp_opc;		/* IN: OPC */
	int			 lstio_grp_args;	/* IN: arguments */
	int			 lstio_grp_nmlen;	/* IN: name length */
	char __user		*lstio_grp_namep;	/* IN: group name */
	int			 lstio_grp_count;	/* IN: # of nodes id */
	lnet_process_id_t __user *lstio_grp_idsp;	/* IN: array of nodes */
	struct list_head __user	*lstio_grp_resultp;	/* OUT: list head of
								result buffer */
} lstio_group_update_args_t;

typedef struct {
	int			 lstio_grp_key;		/* IN: session key */
	int			 lstio_grp_nmlen;	/* IN: name length */
	char __user		*lstio_grp_namep;	/* IN: group name */
	int			 lstio_grp_count;	/* IN: # of nodes */
	/** OUT: session features */
	unsigned __user		*lstio_grp_featp;
	lnet_process_id_t __user *lstio_grp_idsp;	/* IN: nodes */
	struct list_head __user	*lstio_grp_resultp;	/* OUT: list head of
								result buffer */
} lstio_group_nodes_args_t;

typedef struct {
	int	 lstio_grp_key;		/* IN: session key */
	int	 lstio_grp_idx;		/* IN: group idx */
	int	 lstio_grp_nmlen;	/* IN: name len */
	char __user *lstio_grp_namep;	/* OUT: name */
} lstio_group_list_args_t;

typedef struct {
	int			 lstio_grp_key;		/* IN: session key */
	int			 lstio_grp_nmlen;	/* IN: name len */
	char __user		*lstio_grp_namep;	/* IN: name */
	lstcon_ndlist_ent_t __user *lstio_grp_entp;	/* OUT: description of
								group */
	int __user		*lstio_grp_idxp;	/* IN/OUT: node index */
	int __user		*lstio_grp_ndentp;	/* IN/OUT: # of nodent */
	lstcon_node_ent_t __user *lstio_grp_dentsp;	/* OUT: nodent array */
} lstio_group_info_args_t;

#define LST_DEFAULT_BATCH	"batch"			/* default batch name */

typedef struct {
	int	 lstio_bat_key;		/* IN: session key */
	int	 lstio_bat_nmlen;	/* IN: name length */
	char __user *lstio_bat_namep;	/* IN: batch name */
} lstio_batch_add_args_t;

typedef struct {
	int	 lstio_bat_key;		/* IN: session key */
	int	 lstio_bat_nmlen;	/* IN: name length */
	char __user *lstio_bat_namep;	/* IN: batch name */
} lstio_batch_del_args_t;

typedef struct {
	int			 lstio_bat_key;		/* IN: session key */
	int			 lstio_bat_timeout;	/* IN: timeout for
							       the batch */
	int			 lstio_bat_nmlen;	/* IN: name length */
	char __user		*lstio_bat_namep;	/* IN: batch name */
	struct list_head __user	*lstio_bat_resultp;	/* OUT: list head of
								result buffer */
} lstio_batch_run_args_t;

typedef struct {
	int			 lstio_bat_key;		/* IN: session key */
	int			 lstio_bat_force;	/* IN: abort unfinished
							       test RPC */
	int			 lstio_bat_nmlen;	/* IN: name length */
	char __user		*lstio_bat_namep;	/* IN: batch name */
	struct list_head __user	*lstio_bat_resultp;	/* OUT: list head of
								result buffer */
} lstio_batch_stop_args_t;

typedef struct {
	int			 lstio_bat_key;		/* IN: session key */
	int			 lstio_bat_testidx;	/* IN: test index */
	int			 lstio_bat_client;	/* IN: we testing
							       client? */
	int			 lstio_bat_timeout;	/* IN: timeout for
							       waiting */
	int			 lstio_bat_nmlen;	/* IN: name length */
	char __user		*lstio_bat_namep;	/* IN: batch name */
	struct list_head __user	*lstio_bat_resultp;	/* OUT: list head of
								result buffer */
} lstio_batch_query_args_t;

typedef struct {
	int	 lstio_bat_key;		/* IN: session key */
	int	 lstio_bat_idx;		/* IN: index */
	int	 lstio_bat_nmlen;	/* IN: name length */
	char __user *lstio_bat_namep;	/* IN: batch name */
} lstio_batch_list_args_t;

typedef struct {
	int			 lstio_bat_key;		/* IN: session key */
	int			 lstio_bat_nmlen;	/* IN: name length */
	char __user		*lstio_bat_namep;	/* IN: name */
	int			 lstio_bat_server;	/* IN: query server
							       or not */
	int			 lstio_bat_testidx;	/* IN: test index */
	lstcon_test_batch_ent_t __user *lstio_bat_entp;	/* OUT: batch ent */

	int __user		*lstio_bat_idxp;	/* IN/OUT: index of node */
	int __user		*lstio_bat_ndentp;	/* IN/OUT: # of nodent */
	lstcon_node_ent_t __user *lstio_bat_dentsp;	/* array of nodent */
} lstio_batch_info_args_t;

/* add stat in session */
typedef struct {
	int			 lstio_sta_key;		/* IN: session key */
	int			 lstio_sta_timeout;	/* IN: timeout for
							       stat request */
	int			 lstio_sta_nmlen;	/* IN: group name
							       length */
	char __user		*lstio_sta_namep;	/* IN: group name */
	int			 lstio_sta_count;	/* IN: # of pid */
	lnet_process_id_t __user *lstio_sta_idsp;	/* IN: pid */
	struct list_head __user	*lstio_sta_resultp;	/* OUT: list head of
								result buffer */
} lstio_stat_args_t;

typedef enum {
	LST_TEST_BULK	= 1,
	LST_TEST_PING	= 2
} lst_test_type_t;

/* create a test in a batch */
#define LST_MAX_CONCUR	1024	/* Max concurrency of test */

typedef struct {
	int		  lstio_tes_key;	/* IN: session key */
	int		  lstio_tes_bat_nmlen;	/* IN: batch name len */
	char __user	 *lstio_tes_bat_name;	/* IN: batch name */
	int		  lstio_tes_type;	/* IN: test type */
	int		  lstio_tes_oneside;	/* IN: one sided test */
	int		  lstio_tes_loop;	/* IN: loop count */
	int		  lstio_tes_concur;	/* IN: concurrency */

	int		  lstio_tes_dist;	/* IN: node distribution in
						       destination groups */
	int		  lstio_tes_span;	/* IN: node span in
						       destination groups */
	int		  lstio_tes_sgrp_nmlen;	/* IN: source group
						       name length */
	char __user	 *lstio_tes_sgrp_name;	/* IN: group name */
	int		  lstio_tes_dgrp_nmlen;	/* IN: destination group
						       name length */
	char __user	 *lstio_tes_dgrp_name;	/* IN: group name */

	int		  lstio_tes_param_len;	/* IN: param buffer len */
	void __user	 *lstio_tes_param;	/* IN: parameter for specified
						       test:
						       lstio_bulk_param_t,
						       lstio_ping_param_t,
						       ... more */
	int __user	 *lstio_tes_retp;	/* OUT: private returned
							value */
	struct list_head __user *lstio_tes_resultp;/* OUT: list head of
							result buffer */
} lstio_test_args_t;

typedef enum {
	LST_BRW_READ	= 1,
	LST_BRW_WRITE	= 2
} lst_brw_type_t;

typedef enum {
	LST_BRW_CHECK_NONE	= 1,
	LST_BRW_CHECK_SIMPLE	= 2,
	LST_BRW_CHECK_FULL	= 3
} lst_brw_flags_t;

typedef struct {
	int	blk_opc;	/* bulk operation code */
	int	blk_size;       /* size (bytes) */
	int	blk_time;       /* time of running the test*/
	int	blk_flags;      /* reserved flags */
	int	blk_cli_off;	/* bulk offset on client */
	int	blk_srv_off;	/* reserved: bulk offset on server */
} lst_test_bulk_param_t;

typedef struct {
	int	png_size;	/* size of ping message */
	int	png_time;	/* time */
	int	png_loop;	/* loop */
	int	png_flags;	/* reserved flags */
} lst_test_ping_param_t;

typedef struct {
	__u32 errors;
	__u32 rpcs_sent;
	__u32 rpcs_rcvd;
	__u32 rpcs_dropped;
	__u32 rpcs_expired;
	__u64 bulk_get;
	__u64 bulk_put;
} WIRE_ATTR srpc_counters_t;

typedef struct {
	/** milliseconds since current session started */
	__u32 running_ms;
	__u32 active_batches;
	__u32 zombie_sessions;
	__u32 brw_errors;
	__u32 ping_errors;
} WIRE_ATTR sfw_counters_t;

#endif
