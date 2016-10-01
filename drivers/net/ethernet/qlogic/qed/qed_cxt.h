/* QLogic qed NIC Driver
 * Copyright (c) 2015 QLogic Corporation
 *
 * This software is available under the terms of the GNU General Public License
 * (GPL) Version 2, available from the file COPYING in the main directory of
 * this source tree.
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
 * @brief qed_cxt_acquire - Acquire a new cid of a specific protocol type
 *
 * @param p_hwfn
 * @param type
 * @param p_cid
 *
 * @return int
 */
int qed_cxt_acquire_cid(struct qed_hwfn *p_hwfn,
			enum protocol_type type,
			u32 *p_cid);

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
enum qed_cxt_elem_type {
	QED_ELEM_CXT,
	QED_ELEM_SRQ,
	QED_ELEM_TASK
};

u32 qed_cxt_get_proto_cid_count(struct qed_hwfn *p_hwfn,
				enum protocol_type type, u32 *vf_cid);

/**
 * @brief qed_cxt_set_pf_params - Set the PF params for cxt init
 *
 * @param p_hwfn
 *
 * @return int
 */
int qed_cxt_set_pf_params(struct qed_hwfn *p_hwfn);

/**
 * @brief qed_cxt_cfg_ilt_compute - compute ILT init parameters
 *
 * @param p_hwfn
 *
 * @return int
 */
int qed_cxt_cfg_ilt_compute(struct qed_hwfn *p_hwfn);

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
 *
 *
 * @param p_hwfn
 */
void qed_cxt_hw_init_pf(struct qed_hwfn *p_hwfn);

/**
 * @brief qed_qm_init_pf - Initailze the QM PF phase, per path
 *
 * @param p_hwfn
 */

void qed_qm_init_pf(struct qed_hwfn *p_hwfn);

/**
 * @brief Reconfigures QM pf on the fly
 *
 * @param p_hwfn
 * @param p_ptt
 *
 * @return int
 */
int qed_qm_reconf(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt);

/**
 * @brief qed_cxt_release - Release a cid
 *
 * @param p_hwfn
 * @param cid
 */
void qed_cxt_release_cid(struct qed_hwfn *p_hwfn,
			 u32 cid);
int qed_cxt_dynamic_ilt_alloc(struct qed_hwfn *p_hwfn,
			      enum qed_cxt_elem_type elem_type, u32 iid);
u32 qed_cxt_get_proto_tid_count(struct qed_hwfn *p_hwfn,
				enum protocol_type type);
u32 qed_cxt_get_proto_cid_start(struct qed_hwfn *p_hwfn,
				enum protocol_type type);
int qed_cxt_free_proto_ilt(struct qed_hwfn *p_hwfn, enum protocol_type proto);

#define QED_CTX_WORKING_MEM 0
#define QED_CTX_FL_MEM 1
#endif
