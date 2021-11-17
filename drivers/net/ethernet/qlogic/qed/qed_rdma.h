/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
 * Copyright (c) 2019-2020 Marvell International Ltd.
 */

#ifndef _QED_RDMA_H
#define _QED_RDMA_H
#include <linux/types.h>
#include <linux/bitops.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/qed/qed_if.h>
#include <linux/qed/qed_rdma_if.h>
#include "qed.h"
#include "qed_dev_api.h"
#include "qed_hsi.h"
#include "qed_iwarp.h"
#include "qed_roce.h"

#define QED_RDMA_MAX_P_KEY                  (1)
#define QED_RDMA_MAX_WQE                    (0x7FFF)
#define QED_RDMA_MAX_SRQ_WQE_ELEM           (0x7FFF)
#define QED_RDMA_PAGE_SIZE_CAPS             (0xFFFFF000)
#define QED_RDMA_ACK_DELAY                  (15)
#define QED_RDMA_MAX_MR_SIZE                (0x10000000000ULL)
#define QED_RDMA_MAX_CQS                    (RDMA_MAX_CQS)
#define QED_RDMA_MAX_MRS                    (RDMA_MAX_TIDS)
/* Add 1 for header element */
#define QED_RDMA_MAX_SRQ_ELEM_PER_WQE	    (RDMA_MAX_SGE_PER_RQ_WQE + 1)
#define QED_RDMA_MAX_SGE_PER_SRQ_WQE        (RDMA_MAX_SGE_PER_RQ_WQE)
#define QED_RDMA_SRQ_WQE_ELEM_SIZE          (16)
#define QED_RDMA_MAX_SRQS                   (32 * 1024)

#define QED_RDMA_MAX_CQE_32_BIT             (0x7FFFFFFF - 1)
#define QED_RDMA_MAX_CQE_16_BIT             (0x7FFF - 1)

/* Up to 2^16 XRC Domains are supported, but the actual number of supported XRC
 * SRQs is much smaller so there's no need to have that many domains.
 */
#define QED_RDMA_MAX_XRCDS      (roundup_pow_of_two(RDMA_MAX_XRC_SRQS))

enum qed_rdma_toggle_bit {
	QED_RDMA_TOGGLE_BIT_CLEAR = 0,
	QED_RDMA_TOGGLE_BIT_SET = 1
};

#define QED_RDMA_MAX_BMAP_NAME	(10)
struct qed_bmap {
	unsigned long *bitmap;
	u32 max_count;
	char name[QED_RDMA_MAX_BMAP_NAME];
};

struct qed_rdma_info {
	/* spin lock to protect bitmaps */
	spinlock_t lock;

	struct qed_bmap cq_map;
	struct qed_bmap pd_map;
	struct qed_bmap xrcd_map;
	struct qed_bmap tid_map;
	struct qed_bmap qp_map;
	struct qed_bmap srq_map;
	struct qed_bmap xrc_srq_map;
	struct qed_bmap cid_map;
	struct qed_bmap tcp_cid_map;
	struct qed_bmap real_cid_map;
	struct qed_bmap dpi_map;
	struct qed_bmap toggle_bits;
	struct qed_rdma_events events;
	struct qed_rdma_device *dev;
	struct qed_rdma_port *port;
	u32 last_tid;
	u8 num_cnqs;
	u32 num_qps;
	u32 num_mrs;
	u32 num_srqs;
	u16 srq_id_offset;
	u16 queue_zone_base;
	u16 max_queue_zones;
	enum protocol_type proto;
	struct qed_iwarp_info iwarp;
	u8 active:1;
};

struct qed_rdma_qp {
	struct regpair qp_handle;
	struct regpair qp_handle_async;
	u32 qpid;
	u16 icid;
	enum qed_roce_qp_state cur_state;
	enum qed_rdma_qp_type qp_type;
	enum qed_iwarp_qp_state iwarp_state;
	bool use_srq;
	bool signal_all;
	bool fmr_and_reserved_lkey;

	bool incoming_rdma_read_en;
	bool incoming_rdma_write_en;
	bool incoming_atomic_en;
	bool e2e_flow_control_en;

	u16 pd;
	u16 pkey;
	u32 dest_qp;
	u16 mtu;
	u16 srq_id;
	u8 traffic_class_tos;
	u8 hop_limit_ttl;
	u16 dpi;
	u32 flow_label;
	bool lb_indication;
	u16 vlan_id;
	u32 ack_timeout;
	u8 retry_cnt;
	u8 rnr_retry_cnt;
	u8 min_rnr_nak_timer;
	bool sqd_async;
	union qed_gid sgid;
	union qed_gid dgid;
	enum roce_mode roce_mode;
	u16 udp_src_port;
	u8 stats_queue;

	/* requeseter */
	u8 max_rd_atomic_req;
	u32 sq_psn;
	u16 sq_cq_id;
	u16 sq_num_pages;
	dma_addr_t sq_pbl_ptr;
	void *orq;
	dma_addr_t orq_phys_addr;
	u8 orq_num_pages;
	bool req_offloaded;
	bool has_req;

	/* responder */
	u8 max_rd_atomic_resp;
	u32 rq_psn;
	u16 rq_cq_id;
	u16 rq_num_pages;
	u16 xrcd_id;
	dma_addr_t rq_pbl_ptr;
	void *irq;
	dma_addr_t irq_phys_addr;
	u8 irq_num_pages;
	bool resp_offloaded;
	u32 cq_prod;
	bool has_resp;

	u8 remote_mac_addr[6];
	u8 local_mac_addr[6];

	void *shared_queue;
	dma_addr_t shared_queue_phys_addr;
	struct qed_iwarp_ep *ep;
	u8 edpm_mode;
};

static inline bool qed_rdma_is_xrc_qp(struct qed_rdma_qp *qp)
{
	if (qp->qp_type == QED_RDMA_QP_TYPE_XRC_TGT ||
	    qp->qp_type == QED_RDMA_QP_TYPE_XRC_INI)
		return true;

	return false;
}
#if IS_ENABLED(CONFIG_QED_RDMA)
void qed_rdma_dpm_bar(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt);
void qed_rdma_dpm_conf(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt);
int qed_rdma_info_alloc(struct qed_hwfn *p_hwfn);
void qed_rdma_info_free(struct qed_hwfn *p_hwfn);
#else
static inline void qed_rdma_dpm_conf(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt) {}
static inline void qed_rdma_dpm_bar(struct qed_hwfn *p_hwfn,
				    struct qed_ptt *p_ptt) {}
static inline int qed_rdma_info_alloc(struct qed_hwfn *p_hwfn) {return -EINVAL;}
static inline void qed_rdma_info_free(struct qed_hwfn *p_hwfn) {}
#endif

int
qed_rdma_bmap_alloc(struct qed_hwfn *p_hwfn,
		    struct qed_bmap *bmap, u32 max_count, char *name);

void
qed_rdma_bmap_free(struct qed_hwfn *p_hwfn, struct qed_bmap *bmap, bool check);

int
qed_rdma_bmap_alloc_id(struct qed_hwfn *p_hwfn,
		       struct qed_bmap *bmap, u32 *id_num);

void
qed_bmap_set_id(struct qed_hwfn *p_hwfn, struct qed_bmap *bmap, u32 id_num);

void
qed_bmap_release_id(struct qed_hwfn *p_hwfn, struct qed_bmap *bmap, u32 id_num);

int
qed_bmap_test_id(struct qed_hwfn *p_hwfn, struct qed_bmap *bmap, u32 id_num);

void qed_rdma_set_fw_mac(__le16 *p_fw_mac, const u8 *p_qed_mac);

bool qed_rdma_allocated_qps(struct qed_hwfn *p_hwfn);
#endif
