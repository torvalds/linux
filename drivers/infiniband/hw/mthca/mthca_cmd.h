/*
 * Copyright (c) 2004, 2005 Topspin Communications.  All rights reserved.
 * Copyright (c) 2005 Mellanox Technologies. All rights reserved.
 * Copyright (c) 2006 Cisco Systems.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * $Id: mthca_cmd.h 1349 2004-12-16 21:09:43Z roland $
 */

#ifndef MTHCA_CMD_H
#define MTHCA_CMD_H

#include <rdma/ib_verbs.h>

#define MTHCA_MAILBOX_SIZE 4096

enum {
	/* command completed successfully: */
	MTHCA_CMD_STAT_OK 	      = 0x00,
	/* Internal error (such as a bus error) occurred while processing command: */
	MTHCA_CMD_STAT_INTERNAL_ERR   = 0x01,
	/* Operation/command not supported or opcode modifier not supported: */
	MTHCA_CMD_STAT_BAD_OP 	      = 0x02,
	/* Parameter not supported or parameter out of range: */
	MTHCA_CMD_STAT_BAD_PARAM      = 0x03,
	/* System not enabled or bad system state: */
	MTHCA_CMD_STAT_BAD_SYS_STATE  = 0x04,
	/* Attempt to access reserved or unallocaterd resource: */
	MTHCA_CMD_STAT_BAD_RESOURCE   = 0x05,
	/* Requested resource is currently executing a command, or is otherwise busy: */
	MTHCA_CMD_STAT_RESOURCE_BUSY  = 0x06,
	/* memory error: */
	MTHCA_CMD_STAT_DDR_MEM_ERR    = 0x07,
	/* Required capability exceeds device limits: */
	MTHCA_CMD_STAT_EXCEED_LIM     = 0x08,
	/* Resource is not in the appropriate state or ownership: */
	MTHCA_CMD_STAT_BAD_RES_STATE  = 0x09,
	/* Index out of range: */
	MTHCA_CMD_STAT_BAD_INDEX      = 0x0a,
	/* FW image corrupted: */
	MTHCA_CMD_STAT_BAD_NVMEM      = 0x0b,
	/* Attempt to modify a QP/EE which is not in the presumed state: */
	MTHCA_CMD_STAT_BAD_QPEE_STATE = 0x10,
	/* Bad segment parameters (Address/Size): */
	MTHCA_CMD_STAT_BAD_SEG_PARAM  = 0x20,
	/* Memory Region has Memory Windows bound to: */
	MTHCA_CMD_STAT_REG_BOUND      = 0x21,
	/* HCA local attached memory not present: */
	MTHCA_CMD_STAT_LAM_NOT_PRE    = 0x22,
	/* Bad management packet (silently discarded): */
	MTHCA_CMD_STAT_BAD_PKT 	      = 0x30,
	/* More outstanding CQEs in CQ than new CQ size: */
	MTHCA_CMD_STAT_BAD_SIZE       = 0x40
};

enum {
	MTHCA_TRANS_INVALID = 0,
	MTHCA_TRANS_RST2INIT,
	MTHCA_TRANS_INIT2INIT,
	MTHCA_TRANS_INIT2RTR,
	MTHCA_TRANS_RTR2RTS,
	MTHCA_TRANS_RTS2RTS,
	MTHCA_TRANS_SQERR2RTS,
	MTHCA_TRANS_ANY2ERR,
	MTHCA_TRANS_RTS2SQD,
	MTHCA_TRANS_SQD2SQD,
	MTHCA_TRANS_SQD2RTS,
	MTHCA_TRANS_ANY2RST,
};

enum {
	DEV_LIM_FLAG_RC                 = 1 << 0,
	DEV_LIM_FLAG_UC                 = 1 << 1,
	DEV_LIM_FLAG_UD                 = 1 << 2,
	DEV_LIM_FLAG_RD                 = 1 << 3,
	DEV_LIM_FLAG_RAW_IPV6           = 1 << 4,
	DEV_LIM_FLAG_RAW_ETHER          = 1 << 5,
	DEV_LIM_FLAG_SRQ                = 1 << 6,
	DEV_LIM_FLAG_BAD_PKEY_CNTR      = 1 << 8,
	DEV_LIM_FLAG_BAD_QKEY_CNTR      = 1 << 9,
	DEV_LIM_FLAG_MW                 = 1 << 16,
	DEV_LIM_FLAG_AUTO_PATH_MIG      = 1 << 17,
	DEV_LIM_FLAG_ATOMIC             = 1 << 18,
	DEV_LIM_FLAG_RAW_MULTI          = 1 << 19,
	DEV_LIM_FLAG_UD_AV_PORT_ENFORCE = 1 << 20,
	DEV_LIM_FLAG_UD_MULTI           = 1 << 21,
};

struct mthca_mailbox {
	dma_addr_t dma;
	void      *buf;
};

struct mthca_dev_lim {
	int max_srq_sz;
	int max_qp_sz;
	int reserved_qps;
	int max_qps;
	int reserved_srqs;
	int max_srqs;
	int reserved_eecs;
	int max_eecs;
	int max_cq_sz;
	int reserved_cqs;
	int max_cqs;
	int max_mpts;
	int reserved_eqs;
	int max_eqs;
	int reserved_mtts;
	int max_mrw_sz;
	int reserved_mrws;
	int max_mtt_seg;
	int max_requester_per_qp;
	int max_responder_per_qp;
	int max_rdma_global;
	int local_ca_ack_delay;
	int max_mtu;
	int max_port_width;
	int max_vl;
	int num_ports;
	int max_gids;
	int max_pkeys;
	u32 flags;
	int reserved_uars;
	int uar_size;
	int min_page_sz;
	int max_sg;
	int max_desc_sz;
	int max_qp_per_mcg;
	int reserved_mgms;
	int max_mcgs;
	int reserved_pds;
	int max_pds;
	int reserved_rdds;
	int max_rdds;
	int eec_entry_sz;
	int qpc_entry_sz;
	int eeec_entry_sz;
	int eqpc_entry_sz;
	int eqc_entry_sz;
	int cqc_entry_sz;
	int srq_entry_sz;
	int uar_scratch_entry_sz;
	int mpt_entry_sz;
	union {
		struct {
			int max_avs;
		} tavor;
		struct {
			int resize_srq;
			int max_pbl_sz;
			u8  bmme_flags;
			u32 reserved_lkey;
			int lam_required;
			u64 max_icm_sz;
		} arbel;
	} hca;
};

struct mthca_adapter {
	u32  vendor_id;
	u32  device_id;
	u32  revision_id;
	char board_id[MTHCA_BOARD_ID_LEN];
	u8   inta_pin;
};

struct mthca_init_hca_param {
	u64 qpc_base;
	u64 eec_base;
	u64 srqc_base;
	u64 cqc_base;
	u64 eqpc_base;
	u64 eeec_base;
	u64 eqc_base;
	u64 rdb_base;
	u64 mc_base;
	u64 mpt_base;
	u64 mtt_base;
	u64 uar_scratch_base;
	u64 uarc_base;
	u16 log_mc_entry_sz;
	u16 mc_hash_sz;
	u8  log_num_qps;
	u8  log_num_eecs;
	u8  log_num_srqs;
	u8  log_num_cqs;
	u8  log_num_eqs;
	u8  log_mc_table_sz;
	u8  mtt_seg_sz;
	u8  log_mpt_sz;
	u8  log_uar_sz;
	u8  log_uarc_sz;
};

struct mthca_init_ib_param {
	int port_width;
	int vl_cap;
	int mtu_cap;
	u16 gid_cap;
	u16 pkey_cap;
	int set_guid0;
	u64 guid0;
	int set_node_guid;
	u64 node_guid;
	int set_si_guid;
	u64 si_guid;
};

struct mthca_set_ib_param {
	int set_si_guid;
	int reset_qkey_viol;
	u64 si_guid;
	u32 cap_mask;
};

int mthca_cmd_init(struct mthca_dev *dev);
void mthca_cmd_cleanup(struct mthca_dev *dev);
int mthca_cmd_use_events(struct mthca_dev *dev);
void mthca_cmd_use_polling(struct mthca_dev *dev);
void mthca_cmd_event(struct mthca_dev *dev, u16 token,
		     u8  status, u64 out_param);

struct mthca_mailbox *mthca_alloc_mailbox(struct mthca_dev *dev,
					  gfp_t gfp_mask);
void mthca_free_mailbox(struct mthca_dev *dev, struct mthca_mailbox *mailbox);

int mthca_SYS_EN(struct mthca_dev *dev, u8 *status);
int mthca_SYS_DIS(struct mthca_dev *dev, u8 *status);
int mthca_MAP_FA(struct mthca_dev *dev, struct mthca_icm *icm, u8 *status);
int mthca_UNMAP_FA(struct mthca_dev *dev, u8 *status);
int mthca_RUN_FW(struct mthca_dev *dev, u8 *status);
int mthca_QUERY_FW(struct mthca_dev *dev, u8 *status);
int mthca_ENABLE_LAM(struct mthca_dev *dev, u8 *status);
int mthca_DISABLE_LAM(struct mthca_dev *dev, u8 *status);
int mthca_QUERY_DDR(struct mthca_dev *dev, u8 *status);
int mthca_QUERY_DEV_LIM(struct mthca_dev *dev,
			struct mthca_dev_lim *dev_lim, u8 *status);
int mthca_QUERY_ADAPTER(struct mthca_dev *dev,
			struct mthca_adapter *adapter, u8 *status);
int mthca_INIT_HCA(struct mthca_dev *dev,
		   struct mthca_init_hca_param *param,
		   u8 *status);
int mthca_INIT_IB(struct mthca_dev *dev,
		  struct mthca_init_ib_param *param,
		  int port, u8 *status);
int mthca_CLOSE_IB(struct mthca_dev *dev, int port, u8 *status);
int mthca_CLOSE_HCA(struct mthca_dev *dev, int panic, u8 *status);
int mthca_SET_IB(struct mthca_dev *dev, struct mthca_set_ib_param *param,
		 int port, u8 *status);
int mthca_MAP_ICM(struct mthca_dev *dev, struct mthca_icm *icm, u64 virt, u8 *status);
int mthca_MAP_ICM_page(struct mthca_dev *dev, u64 dma_addr, u64 virt, u8 *status);
int mthca_UNMAP_ICM(struct mthca_dev *dev, u64 virt, u32 page_count, u8 *status);
int mthca_MAP_ICM_AUX(struct mthca_dev *dev, struct mthca_icm *icm, u8 *status);
int mthca_UNMAP_ICM_AUX(struct mthca_dev *dev, u8 *status);
int mthca_SET_ICM_SIZE(struct mthca_dev *dev, u64 icm_size, u64 *aux_pages,
		       u8 *status);
int mthca_SW2HW_MPT(struct mthca_dev *dev, struct mthca_mailbox *mailbox,
		    int mpt_index, u8 *status);
int mthca_HW2SW_MPT(struct mthca_dev *dev, struct mthca_mailbox *mailbox,
		    int mpt_index, u8 *status);
int mthca_WRITE_MTT(struct mthca_dev *dev, struct mthca_mailbox *mailbox,
		    int num_mtt, u8 *status);
int mthca_SYNC_TPT(struct mthca_dev *dev, u8 *status);
int mthca_MAP_EQ(struct mthca_dev *dev, u64 event_mask, int unmap,
		 int eq_num, u8 *status);
int mthca_SW2HW_EQ(struct mthca_dev *dev, struct mthca_mailbox *mailbox,
		   int eq_num, u8 *status);
int mthca_HW2SW_EQ(struct mthca_dev *dev, struct mthca_mailbox *mailbox,
		   int eq_num, u8 *status);
int mthca_SW2HW_CQ(struct mthca_dev *dev, struct mthca_mailbox *mailbox,
		   int cq_num, u8 *status);
int mthca_HW2SW_CQ(struct mthca_dev *dev, struct mthca_mailbox *mailbox,
		   int cq_num, u8 *status);
int mthca_RESIZE_CQ(struct mthca_dev *dev, int cq_num, u32 lkey, u8 log_size,
		    u8 *status);
int mthca_SW2HW_SRQ(struct mthca_dev *dev, struct mthca_mailbox *mailbox,
		    int srq_num, u8 *status);
int mthca_HW2SW_SRQ(struct mthca_dev *dev, struct mthca_mailbox *mailbox,
		    int srq_num, u8 *status);
int mthca_QUERY_SRQ(struct mthca_dev *dev, u32 num,
		    struct mthca_mailbox *mailbox, u8 *status);
int mthca_ARM_SRQ(struct mthca_dev *dev, int srq_num, int limit, u8 *status);
int mthca_MODIFY_QP(struct mthca_dev *dev, enum ib_qp_state cur,
		    enum ib_qp_state next, u32 num, int is_ee,
		    struct mthca_mailbox *mailbox, u32 optmask,
		    u8 *status);
int mthca_QUERY_QP(struct mthca_dev *dev, u32 num, int is_ee,
		   struct mthca_mailbox *mailbox, u8 *status);
int mthca_CONF_SPECIAL_QP(struct mthca_dev *dev, int type, u32 qpn,
			  u8 *status);
int mthca_MAD_IFC(struct mthca_dev *dev, int ignore_mkey, int ignore_bkey,
		  int port, struct ib_wc *in_wc, struct ib_grh *in_grh,
		  void *in_mad, void *response_mad, u8 *status);
int mthca_READ_MGM(struct mthca_dev *dev, int index,
		   struct mthca_mailbox *mailbox, u8 *status);
int mthca_WRITE_MGM(struct mthca_dev *dev, int index,
		    struct mthca_mailbox *mailbox, u8 *status);
int mthca_MGID_HASH(struct mthca_dev *dev, struct mthca_mailbox *mailbox,
		    u16 *hash, u8 *status);
int mthca_NOP(struct mthca_dev *dev, u8 *status);

#endif /* MTHCA_CMD_H */
