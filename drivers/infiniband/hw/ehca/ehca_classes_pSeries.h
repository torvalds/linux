/*
 *  IBM eServer eHCA Infiniband device driver for Linux on POWER
 *
 *  pSeries interface definitions
 *
 *  Authors: Waleri Fomin <fomin@de.ibm.com>
 *           Christoph Raisch <raisch@de.ibm.com>
 *
 *  Copyright (c) 2005 IBM Corporation
 *
 *  All rights reserved.
 *
 *  This source code is distributed under a dual license of GPL v2.0 and OpenIB
 *  BSD.
 *
 * OpenIB BSD License
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials
 * provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __EHCA_CLASSES_PSERIES_H__
#define __EHCA_CLASSES_PSERIES_H__

#include "hcp_phyp.h"
#include "ipz_pt_fn.h"


struct ehca_pfqp {
	struct ipz_qpt sqpt;
	struct ipz_qpt rqpt;
};

struct ehca_pfcq {
	struct ipz_qpt qpt;
	u32 cqnr;
};

struct ehca_pfeq {
	struct ipz_qpt qpt;
	struct h_galpa galpa;
	u32 eqnr;
};

struct ipz_adapter_handle {
	u64 handle;
};

struct ipz_cq_handle {
	u64 handle;
};

struct ipz_eq_handle {
	u64 handle;
};

struct ipz_qp_handle {
	u64 handle;
};
struct ipz_mrmw_handle {
	u64 handle;
};

struct ipz_pd {
	u32 value;
};

struct hcp_modify_qp_control_block {
	u32 qkey;                      /* 00 */
	u32 rdd;                       /* reliable datagram domain */
	u32 send_psn;                  /* 02 */
	u32 receive_psn;               /* 03 */
	u32 prim_phys_port;            /* 04 */
	u32 alt_phys_port;             /* 05 */
	u32 prim_p_key_idx;            /* 06 */
	u32 alt_p_key_idx;             /* 07 */
	u32 rdma_atomic_ctrl;          /* 08 */
	u32 qp_state;                  /* 09 */
	u32 reserved_10;               /* 10 */
	u32 rdma_nr_atomic_resp_res;   /* 11 */
	u32 path_migration_state;      /* 12 */
	u32 rdma_atomic_outst_dest_qp; /* 13 */
	u32 dest_qp_nr;                /* 14 */
	u32 min_rnr_nak_timer_field;   /* 15 */
	u32 service_level;             /* 16 */
	u32 send_grh_flag;             /* 17 */
	u32 retry_count;               /* 18 */
	u32 timeout;                   /* 19 */
	u32 path_mtu;                  /* 20 */
	u32 max_static_rate;           /* 21 */
	u32 dlid;                      /* 22 */
	u32 rnr_retry_count;           /* 23 */
	u32 source_path_bits;          /* 24 */
	u32 traffic_class;             /* 25 */
	u32 hop_limit;                 /* 26 */
	u32 source_gid_idx;            /* 27 */
	u32 flow_label;                /* 28 */
	u32 reserved_29;               /* 29 */
	union {                        /* 30 */
		u64 dw[2];
		u8 byte[16];
	} dest_gid;
	u32 service_level_al;          /* 34 */
	u32 send_grh_flag_al;          /* 35 */
	u32 retry_count_al;            /* 36 */
	u32 timeout_al;                /* 37 */
	u32 max_static_rate_al;        /* 38 */
	u32 dlid_al;                   /* 39 */
	u32 rnr_retry_count_al;        /* 40 */
	u32 source_path_bits_al;       /* 41 */
	u32 traffic_class_al;          /* 42 */
	u32 hop_limit_al;              /* 43 */
	u32 source_gid_idx_al;         /* 44 */
	u32 flow_label_al;             /* 45 */
	u32 reserved_46;               /* 46 */
	u32 reserved_47;               /* 47 */
	union {                        /* 48 */
		u64 dw[2];
		u8 byte[16];
	} dest_gid_al;
	u32 max_nr_outst_send_wr;      /* 52 */
	u32 max_nr_outst_recv_wr;      /* 53 */
	u32 disable_ete_credit_check;  /* 54 */
	u32 qp_number;                 /* 55 */
	u64 send_queue_handle;         /* 56 */
	u64 recv_queue_handle;         /* 58 */
	u32 actual_nr_sges_in_sq_wqe;  /* 60 */
	u32 actual_nr_sges_in_rq_wqe;  /* 61 */
	u32 qp_enable;                 /* 62 */
	u32 curr_srq_limit;            /* 63 */
	u64 qp_aff_asyn_ev_log_reg;    /* 64 */
	u64 shared_rq_hndl;            /* 66 */
	u64 trigg_doorbell_qp_hndl;    /* 68 */
	u32 reserved_70_127[58];       /* 70 */
};

#define MQPCB_MASK_QKEY                         EHCA_BMASK_IBM(0,0)
#define MQPCB_MASK_SEND_PSN                     EHCA_BMASK_IBM(2,2)
#define MQPCB_MASK_RECEIVE_PSN                  EHCA_BMASK_IBM(3,3)
#define MQPCB_MASK_PRIM_PHYS_PORT               EHCA_BMASK_IBM(4,4)
#define MQPCB_PRIM_PHYS_PORT                    EHCA_BMASK_IBM(24,31)
#define MQPCB_MASK_ALT_PHYS_PORT                EHCA_BMASK_IBM(5,5)
#define MQPCB_MASK_PRIM_P_KEY_IDX               EHCA_BMASK_IBM(6,6)
#define MQPCB_PRIM_P_KEY_IDX                    EHCA_BMASK_IBM(24,31)
#define MQPCB_MASK_ALT_P_KEY_IDX                EHCA_BMASK_IBM(7,7)
#define MQPCB_MASK_RDMA_ATOMIC_CTRL             EHCA_BMASK_IBM(8,8)
#define MQPCB_MASK_QP_STATE                     EHCA_BMASK_IBM(9,9)
#define MQPCB_QP_STATE                          EHCA_BMASK_IBM(24,31)
#define MQPCB_MASK_RDMA_NR_ATOMIC_RESP_RES      EHCA_BMASK_IBM(11,11)
#define MQPCB_MASK_PATH_MIGRATION_STATE         EHCA_BMASK_IBM(12,12)
#define MQPCB_MASK_RDMA_ATOMIC_OUTST_DEST_QP    EHCA_BMASK_IBM(13,13)
#define MQPCB_MASK_DEST_QP_NR                   EHCA_BMASK_IBM(14,14)
#define MQPCB_MASK_MIN_RNR_NAK_TIMER_FIELD      EHCA_BMASK_IBM(15,15)
#define MQPCB_MASK_SERVICE_LEVEL                EHCA_BMASK_IBM(16,16)
#define MQPCB_MASK_SEND_GRH_FLAG                EHCA_BMASK_IBM(17,17)
#define MQPCB_MASK_RETRY_COUNT                  EHCA_BMASK_IBM(18,18)
#define MQPCB_MASK_TIMEOUT                      EHCA_BMASK_IBM(19,19)
#define MQPCB_MASK_PATH_MTU                     EHCA_BMASK_IBM(20,20)
#define MQPCB_PATH_MTU                          EHCA_BMASK_IBM(24,31)
#define MQPCB_MASK_MAX_STATIC_RATE              EHCA_BMASK_IBM(21,21)
#define MQPCB_MAX_STATIC_RATE                   EHCA_BMASK_IBM(24,31)
#define MQPCB_MASK_DLID                         EHCA_BMASK_IBM(22,22)
#define MQPCB_DLID                              EHCA_BMASK_IBM(16,31)
#define MQPCB_MASK_RNR_RETRY_COUNT              EHCA_BMASK_IBM(23,23)
#define MQPCB_RNR_RETRY_COUNT                   EHCA_BMASK_IBM(29,31)
#define MQPCB_MASK_SOURCE_PATH_BITS             EHCA_BMASK_IBM(24,24)
#define MQPCB_SOURCE_PATH_BITS                  EHCA_BMASK_IBM(25,31)
#define MQPCB_MASK_TRAFFIC_CLASS                EHCA_BMASK_IBM(25,25)
#define MQPCB_TRAFFIC_CLASS                     EHCA_BMASK_IBM(24,31)
#define MQPCB_MASK_HOP_LIMIT                    EHCA_BMASK_IBM(26,26)
#define MQPCB_HOP_LIMIT                         EHCA_BMASK_IBM(24,31)
#define MQPCB_MASK_SOURCE_GID_IDX               EHCA_BMASK_IBM(27,27)
#define MQPCB_SOURCE_GID_IDX                    EHCA_BMASK_IBM(24,31)
#define MQPCB_MASK_FLOW_LABEL                   EHCA_BMASK_IBM(28,28)
#define MQPCB_FLOW_LABEL                        EHCA_BMASK_IBM(12,31)
#define MQPCB_MASK_DEST_GID                     EHCA_BMASK_IBM(30,30)
#define MQPCB_MASK_SERVICE_LEVEL_AL             EHCA_BMASK_IBM(31,31)
#define MQPCB_SERVICE_LEVEL_AL                  EHCA_BMASK_IBM(28,31)
#define MQPCB_MASK_SEND_GRH_FLAG_AL             EHCA_BMASK_IBM(32,32)
#define MQPCB_SEND_GRH_FLAG_AL                  EHCA_BMASK_IBM(31,31)
#define MQPCB_MASK_RETRY_COUNT_AL               EHCA_BMASK_IBM(33,33)
#define MQPCB_RETRY_COUNT_AL                    EHCA_BMASK_IBM(29,31)
#define MQPCB_MASK_TIMEOUT_AL                   EHCA_BMASK_IBM(34,34)
#define MQPCB_TIMEOUT_AL                        EHCA_BMASK_IBM(27,31)
#define MQPCB_MASK_MAX_STATIC_RATE_AL           EHCA_BMASK_IBM(35,35)
#define MQPCB_MAX_STATIC_RATE_AL                EHCA_BMASK_IBM(24,31)
#define MQPCB_MASK_DLID_AL                      EHCA_BMASK_IBM(36,36)
#define MQPCB_DLID_AL                           EHCA_BMASK_IBM(16,31)
#define MQPCB_MASK_RNR_RETRY_COUNT_AL           EHCA_BMASK_IBM(37,37)
#define MQPCB_RNR_RETRY_COUNT_AL                EHCA_BMASK_IBM(29,31)
#define MQPCB_MASK_SOURCE_PATH_BITS_AL          EHCA_BMASK_IBM(38,38)
#define MQPCB_SOURCE_PATH_BITS_AL               EHCA_BMASK_IBM(25,31)
#define MQPCB_MASK_TRAFFIC_CLASS_AL             EHCA_BMASK_IBM(39,39)
#define MQPCB_TRAFFIC_CLASS_AL                  EHCA_BMASK_IBM(24,31)
#define MQPCB_MASK_HOP_LIMIT_AL                 EHCA_BMASK_IBM(40,40)
#define MQPCB_HOP_LIMIT_AL                      EHCA_BMASK_IBM(24,31)
#define MQPCB_MASK_SOURCE_GID_IDX_AL            EHCA_BMASK_IBM(41,41)
#define MQPCB_SOURCE_GID_IDX_AL                 EHCA_BMASK_IBM(24,31)
#define MQPCB_MASK_FLOW_LABEL_AL                EHCA_BMASK_IBM(42,42)
#define MQPCB_FLOW_LABEL_AL                     EHCA_BMASK_IBM(12,31)
#define MQPCB_MASK_DEST_GID_AL                  EHCA_BMASK_IBM(44,44)
#define MQPCB_MASK_MAX_NR_OUTST_SEND_WR         EHCA_BMASK_IBM(45,45)
#define MQPCB_MAX_NR_OUTST_SEND_WR              EHCA_BMASK_IBM(16,31)
#define MQPCB_MASK_MAX_NR_OUTST_RECV_WR         EHCA_BMASK_IBM(46,46)
#define MQPCB_MAX_NR_OUTST_RECV_WR              EHCA_BMASK_IBM(16,31)
#define MQPCB_MASK_DISABLE_ETE_CREDIT_CHECK     EHCA_BMASK_IBM(47,47)
#define MQPCB_DISABLE_ETE_CREDIT_CHECK          EHCA_BMASK_IBM(31,31)
#define MQPCB_QP_NUMBER                         EHCA_BMASK_IBM(8,31)
#define MQPCB_MASK_QP_ENABLE                    EHCA_BMASK_IBM(48,48)
#define MQPCB_QP_ENABLE                         EHCA_BMASK_IBM(31,31)
#define MQPCB_MASK_CURR_SRQ_LIMIT               EHCA_BMASK_IBM(49,49)
#define MQPCB_CURR_SRQ_LIMIT                    EHCA_BMASK_IBM(16,31)
#define MQPCB_MASK_QP_AFF_ASYN_EV_LOG_REG       EHCA_BMASK_IBM(50,50)
#define MQPCB_MASK_SHARED_RQ_HNDL               EHCA_BMASK_IBM(51,51)

#endif /* __EHCA_CLASSES_PSERIES_H__ */
