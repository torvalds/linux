/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 * Copyright (c) 2019-2020 Marvell International Ltd.
 */

#ifndef _QED_CXT_H
#define _QED_CXT_H

#include <linux/types.h>
#include <linux/slab.h>
#include <linux/qed/qed_if.h>
#include "qed_hsi.h"
#include "qed.h"

struct qed_cxt_info {
	void			*p_cxt;
	u32			iid;
	enum protocol_type	type;
};

#define MAX_TID_BLOCKS                  512
struct qed_tid_mem {
	u32 tid_size;
	u32 num_tids_per_block;
	u32 waste;
	u8 *blocks[MAX_TID_BLOCKS];	/* 4K */
};

/**
 * @brief qedo_cid_get_cxt_info - Returns the context info for a specific cid
 *
 *
 * @param p_hwfn
 * @param p_info in/out
 *
 * @return int
 */
int qed_cxt_get_cid_info(struct qed_hwfn *p_hwfn,
			 struct qed_cxt_info *p_info);

/**
 * @brief qed_cxt_get_tid_mem_info
 *
 * @param p_hwfn
 * @param p_info
 *
 * @return int
 */
int qed_cxt_get_tid_mem_info(struct qed_hwfn *p_hwfn,
			     struct qed_tid_mem *p_info);

#define QED_CXT_ISCSI_TID_SEG	PROTOCOLID_ISCSI
#define QED_CXT_ROCE_TID_SEG	PROTOCOLID_ROCE
#define QED_CXT_FCOE_TID_SEG	PROTOCOLID_FCOE
enum qed_cxt_elem_type {
	QED_ELEM_CXT,
	QED_ELEM_SRQ,
	QED_ELEM_TASK,
	QED_ELEM_XRC_SRQ,
};

u32 qed_cxt_get_proto_cid_count(struct qed_hwfn *p_hwfn,
				enum protocol_type type, u32 *vf_cid);

/**
 * @brief qed_cxt_set_pf_params - Set the PF params for cxt init
 *
 * @param p_hwfn
 * @param rdma_tasks - requested maximum
 * @return int
 */
int qed_cxt_set_pf_params(struct qed_hwfn *p_hwfn, u32 rdma_tasks);

/**
 * @brief qed_cxt_cfg_ilt_compute - compute ILT init parameters
 *
 * @param p_hwfn
 * @param last_line
 *
 * @return int
 */
int qed_cxt_cfg_ilt_compute(struct qed_hwfn *p_hwfn, u32 *last_line);

/**
 * @brief qed_cxt_cfg_ilt_compute_excess - how many lines can be decreased
 *
 * @param p_hwfn
 * @param used_lines
 */
u32 qed_cxt_cfg_ilt_compute_excess(struct qed_hwfn *p_hwfn, u32 used_lines);

/**
 * @brief qed_cxt_mngr_alloc - Allocate and init the context manager struct
 *
 * @param p_hwfn
 *
 * @return int
 */
int qed_cxt_mngr_alloc(struct qed_hwfn *p_hwfn);

/**
 * @brief qed_cxt_mngr_free
 *
 * @param p_hwfn
 */
void qed_cxt_mngr_free(struct qed_hwfn *p_hwfn);

/**
 * @brief qed_cxt_tables_alloc - Allocate ILT shadow, Searcher T2, acquired map
 *
 * @param p_hwfn
 *
 * @return int
 */
int qed_cxt_tables_alloc(struct qed_hwfn *p_hwfn);

/**
 * @brief qed_cxt_mngr_setup - Reset the acquired CIDs
 *
 * @param p_hwfn
 */
void qed_cxt_mngr_setup(struct qed_hwfn *p_hwfn);

/**
 * @brief qed_cxt_hw_init_common - Initailze ILT and DQ, common phase, per path.
 *
 *
 *
 * @param p_hwfn
 */
void qed_cxt_hw_init_common(struct qed_hwfn *p_hwfn);

/**
 * @brief qed_cxt_hw_init_pf - Initailze ILT and DQ, PF phase, per path.
 *
 * @param p_hwfn
 * @param p_ptt
 */
void qed_cxt_hw_init_pf(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt);

/**
 * @brief qed_qm_init_pf - Initailze the QM PF phase, per path
 *
 * @param p_hwfn
 * @param p_ptt
 * @param is_pf_loading
 */
void qed_qm_init_pf(struct qed_hwfn *p_hwfn,
		    struct qed_ptt *p_ptt, bool is_pf_loading);

/**
 * @brief Reconfigures QM pf on the fly
 *
 * @param p_hwfn
 * @param p_ptt
 *
 * @return int
 */
int qed_qm_reconf(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt);

#define QED_CXT_PF_CID (0xff)

/**
 * @brief qed_cxt_release - Release a cid
 *
 * @param p_hwfn
 * @param cid
 */
void qed_cxt_release_cid(struct qed_hwfn *p_hwfn, u32 cid);

/**
 * @brief qed_cxt_release - Release a cid belonging to a vf-queue
 *
 * @param p_hwfn
 * @param cid
 * @param vfid - engine relative index. QED_CXT_PF_CID if belongs to PF
 */
void _qed_cxt_release_cid(struct qed_hwfn *p_hwfn, u32 cid, u8 vfid);

/**
 * @brief qed_cxt_acquire - Acquire a new cid of a specific protocol type
 *
 * @param p_hwfn
 * @param type
 * @param p_cid
 *
 * @return int
 */
int qed_cxt_acquire_cid(struct qed_hwfn *p_hwfn,
			enum protocol_type type, u32 *p_cid);

/**
 * @brief _qed_cxt_acquire - Acquire a new cid of a specific protocol type
 *                           for a vf-queue
 *
 * @param p_hwfn
 * @param type
 * @param p_cid
 * @param vfid - engine relative index. QED_CXT_PF_CID if belongs to PF
 *
 * @return int
 */
int _qed_cxt_acquire_cid(struct qed_hwfn *p_hwfn,
			 enum protocol_type type, u32 *p_cid, u8 vfid);

int qed_cxt_dynamic_ilt_alloc(struct qed_hwfn *p_hwfn,
			      enum qed_cxt_elem_type elem_type, u32 iid);
u32 qed_cxt_get_proto_tid_count(struct qed_hwfn *p_hwfn,
				enum protocol_type type);
u32 qed_cxt_get_proto_cid_start(struct qed_hwfn *p_hwfn,
				enum protocol_type type);
int qed_cxt_free_proto_ilt(struct qed_hwfn *p_hwfn, enum protocol_type proto);

#define QED_CTX_WORKING_MEM 0
#define QED_CTX_FL_MEM 1
int qed_cxt_get_task_ctx(struct qed_hwfn *p_hwfn,
			 u32 tid, u8 ctx_type, void **task_ctx);

/* Max number of connection types in HW (DQ/CDU etc.) */
#define MAX_CONN_TYPES          PROTOCOLID_COMMON
#define NUM_TASK_TYPES          2
#define NUM_TASK_PF_SEGMENTS    4
#define NUM_TASK_VF_SEGMENTS    1

/* PF per protocl configuration object */
#define TASK_SEGMENTS   (NUM_TASK_PF_SEGMENTS + NUM_TASK_VF_SEGMENTS)
#define TASK_SEGMENT_VF (NUM_TASK_PF_SEGMENTS)

struct qed_tid_seg {
	u32 count;
	u8 type;
	bool has_fl_mem;
};

struct qed_conn_type_cfg {
	u32 cid_count;
	u32 cids_per_vf;
	struct qed_tid_seg tid_seg[TASK_SEGMENTS];
};

/* ILT Client configuration,
 * Per connection type (protocol) resources (cids, tis, vf cids etc.)
 * 1 - for connection context (CDUC) and for each task context we need two
 * values, for regular task context and for force load memory
 */
#define ILT_CLI_PF_BLOCKS       (1 + NUM_TASK_PF_SEGMENTS * 2)
#define ILT_CLI_VF_BLOCKS       (1 + NUM_TASK_VF_SEGMENTS * 2)
#define CDUC_BLK                (0)
#define SRQ_BLK                 (0)
#define CDUT_SEG_BLK(n)         (1 + (u8)(n))
#define CDUT_FL_SEG_BLK(n, X)   (1 + (n) + NUM_TASK_ ## X ## _SEGMENTS)

struct ilt_cfg_pair {
	u32 reg;
	u32 val;
};

struct qed_ilt_cli_blk {
	u32 total_size;		/* 0 means not active */
	u32 real_size_in_page;
	u32 start_line;
	u32 dynamic_line_offset;
	u32 dynamic_line_cnt;
};

struct qed_ilt_client_cfg {
	bool active;

	/* ILT boundaries */
	struct ilt_cfg_pair first;
	struct ilt_cfg_pair last;
	struct ilt_cfg_pair p_size;

	/* ILT client blocks for PF */
	struct qed_ilt_cli_blk pf_blks[ILT_CLI_PF_BLOCKS];
	u32 pf_total_lines;

	/* ILT client blocks for VFs */
	struct qed_ilt_cli_blk vf_blks[ILT_CLI_VF_BLOCKS];
	u32 vf_total_lines;
};

struct qed_cid_acquired_map {
	u32		start_cid;
	u32		max_count;
	unsigned long	*cid_map;
};

struct qed_src_t2 {
	struct phys_mem_desc *dma_mem;
	u32 num_pages;
	u64 first_free;
	u64 last_free;
};

struct qed_cxt_mngr {
	/* Per protocl configuration */
	struct qed_conn_type_cfg	conn_cfg[MAX_CONN_TYPES];

	/* computed ILT structure */
	struct qed_ilt_client_cfg	clients[MAX_ILT_CLIENTS];

	/* Task type sizes */
	u32 task_type_size[NUM_TASK_TYPES];

	/* total number of VFs for this hwfn -
	 * ALL VFs are symmetric in terms of HW resources
	 */
	u32 vf_count;
	u32 first_vf_in_pf;

	/* Acquired CIDs */
	struct qed_cid_acquired_map	acquired[MAX_CONN_TYPES];

	struct qed_cid_acquired_map
	acquired_vf[MAX_CONN_TYPES][MAX_NUM_VFS];

	/* ILT  shadow table */
	struct phys_mem_desc *ilt_shadow;
	u32 ilt_shadow_size;
	u32 pf_start_line;

	/* Mutex for a dynamic ILT allocation */
	struct mutex mutex;

	/* SRC T2 */
	struct qed_src_t2 src_t2;

	/* total number of SRQ's for this hwfn */
	u32 srq_count;
	u32 xrc_srq_count;

	/* Maximal number of L2 steering filters */
	u32 arfs_count;

	u8 task_type_id;
	u16 task_ctx_size;
	u16 conn_ctx_size;
};

u16 qed_get_cdut_num_pf_init_pages(struct qed_hwfn *p_hwfn);
u16 qed_get_cdut_num_vf_init_pages(struct qed_hwfn *p_hwfn);
u16 qed_get_cdut_num_pf_work_pages(struct qed_hwfn *p_hwfn);
u16 qed_get_cdut_num_vf_work_pages(struct qed_hwfn *p_hwfn);

u32 qed_cxt_get_ilt_page_size(struct qed_hwfn *p_hwfn,
			      enum ilt_clients ilt_client);

u32 qed_cxt_get_total_srq_count(struct qed_hwfn *p_hwfn);

#endif
