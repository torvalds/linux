/* SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB */
/* Copyright (c) 2015 - 2020 Intel Corporation */
#ifndef IRDMA_PUDA_H
#define IRDMA_PUDA_H

#define IRDMA_IEQ_MPA_FRAMING	6
#define IRDMA_TCP_OFFSET	40
#define IRDMA_IPV4_PAD		20
#define IRDMA_MRK_BLK_SZ	512

enum puda_rsrc_type {
	IRDMA_PUDA_RSRC_TYPE_ILQ = 1,
	IRDMA_PUDA_RSRC_TYPE_IEQ,
	IRDMA_PUDA_RSRC_TYPE_MAX, /* Must be last entry */
};

enum puda_rsrc_complete {
	PUDA_CQ_CREATED = 1,
	PUDA_QP_CREATED,
	PUDA_TX_COMPLETE,
	PUDA_RX_COMPLETE,
	PUDA_HASH_CRC_COMPLETE,
};

struct irdma_sc_dev;
struct irdma_sc_qp;
struct irdma_sc_cq;

struct irdma_puda_cmpl_info {
	struct irdma_qp_uk *qp;
	u8 q_type;
	u8 l3proto;
	u8 l4proto;
	u16 vlan;
	u32 payload_len;
	u32 compl_error; /* No_err=0, else major and minor err code */
	u32 qp_id;
	u32 wqe_idx;
	bool ipv4:1;
	bool smac_valid:1;
	bool vlan_valid:1;
	u8 smac[ETH_ALEN];
};

struct irdma_puda_send_info {
	u64 paddr; /* Physical address */
	u32 len;
	u32 ah_id;
	u8 tcplen;
	u8 maclen;
	bool ipv4:1;
	bool do_lpb:1;
	void *scratch;
};

struct irdma_puda_buf {
	struct list_head list; /* MUST be first entry */
	struct irdma_dma_mem mem; /* DMA memory for the buffer */
	struct irdma_puda_buf *next; /* for alloclist in rsrc struct */
	struct irdma_virt_mem buf_mem; /* Buffer memory for this buffer */
	void *scratch;
	u8 *iph;
	u8 *tcph;
	u8 *data;
	u16 datalen;
	u16 vlan_id;
	u8 tcphlen; /* tcp length in bytes */
	u8 maclen; /* mac length in bytes */
	u32 totallen; /* machlen+iphlen+tcphlen+datalen */
	refcount_t refcount;
	u8 hdrlen;
	bool ipv4:1;
	bool vlan_valid:1;
	bool do_lpb:1; /* Loopback buffer */
	bool smac_valid:1;
	u32 seqnum;
	u32 ah_id;
	u8 smac[ETH_ALEN];
	struct irdma_sc_vsi *vsi;
};

struct irdma_puda_rsrc_info {
	void (*receive)(struct irdma_sc_vsi *vsi, struct irdma_puda_buf *buf);
	void (*xmit_complete)(struct irdma_sc_vsi *vsi, void *sqwrid);
	enum puda_rsrc_type type; /* ILQ or IEQ */
	u32 count;
	u32 pd_id;
	u32 cq_id;
	u32 qp_id;
	u32 sq_size;
	u32 rq_size;
	u32 tx_buf_cnt; /* total bufs allocated will be rq_size + tx_buf_cnt */
	u16 buf_size;
	u8 stats_idx;
	bool stats_idx_valid:1;
	int abi_ver;
};

struct irdma_puda_rsrc {
	struct irdma_sc_cq cq;
	struct irdma_sc_qp qp;
	struct irdma_sc_pd sc_pd;
	struct irdma_sc_dev *dev;
	struct irdma_sc_vsi *vsi;
	struct irdma_dma_mem cqmem;
	struct irdma_dma_mem qpmem;
	struct irdma_virt_mem ilq_mem;
	enum puda_rsrc_complete cmpl;
	enum puda_rsrc_type type;
	u16 buf_size; /*buf must be max datalen + tcpip hdr + mac */
	u32 cq_id;
	u32 qp_id;
	u32 sq_size;
	u32 rq_size;
	u32 cq_size;
	struct irdma_sq_uk_wr_trk_info *sq_wrtrk_array;
	u64 *rq_wrid_array;
	u32 compl_rxwqe_idx;
	u32 rx_wqe_idx;
	u32 rxq_invalid_cnt;
	u32 tx_wqe_avail_cnt;
	struct shash_desc *hash_desc;
	struct list_head txpend;
	struct list_head bufpool; /* free buffers pool list for recv and xmit */
	u32 alloc_buf_count;
	u32 avail_buf_count; /* snapshot of currently available buffers */
	spinlock_t bufpool_lock;
	struct irdma_puda_buf *alloclist;
	void (*receive)(struct irdma_sc_vsi *vsi, struct irdma_puda_buf *buf);
	void (*xmit_complete)(struct irdma_sc_vsi *vsi, void *sqwrid);
	/* puda stats */
	u64 stats_buf_alloc_fail;
	u64 stats_pkt_rcvd;
	u64 stats_pkt_sent;
	u64 stats_rcvd_pkt_err;
	u64 stats_sent_pkt_q;
	u64 stats_bad_qp_id;
	/* IEQ stats */
	u64 fpdu_processed;
	u64 bad_seq_num;
	u64 crc_err;
	u64 pmode_count;
	u64 partials_handled;
	u8 stats_idx;
	bool check_crc:1;
	bool stats_idx_valid:1;
};

struct irdma_puda_buf *irdma_puda_get_bufpool(struct irdma_puda_rsrc *rsrc);
void irdma_puda_ret_bufpool(struct irdma_puda_rsrc *rsrc,
			    struct irdma_puda_buf *buf);
void irdma_puda_send_buf(struct irdma_puda_rsrc *rsrc,
			 struct irdma_puda_buf *buf);
int irdma_puda_send(struct irdma_sc_qp *qp, struct irdma_puda_send_info *info);
int irdma_puda_create_rsrc(struct irdma_sc_vsi *vsi,
			   struct irdma_puda_rsrc_info *info);
void irdma_puda_dele_rsrc(struct irdma_sc_vsi *vsi, enum puda_rsrc_type type,
			  bool reset);
int irdma_puda_poll_cmpl(struct irdma_sc_dev *dev, struct irdma_sc_cq *cq,
			 u32 *compl_err);

struct irdma_sc_qp *irdma_ieq_get_qp(struct irdma_sc_dev *dev,
				     struct irdma_puda_buf *buf);
int irdma_puda_get_tcpip_info(struct irdma_puda_cmpl_info *info,
			      struct irdma_puda_buf *buf);
int irdma_ieq_check_mpacrc(struct shash_desc *desc, void *addr, u32 len, u32 val);
int irdma_init_hash_desc(struct shash_desc **desc);
void irdma_ieq_mpa_crc_ae(struct irdma_sc_dev *dev, struct irdma_sc_qp *qp);
void irdma_free_hash_desc(struct shash_desc *desc);
void irdma_ieq_update_tcpip_info(struct irdma_puda_buf *buf, u16 len, u32 seqnum);
int irdma_cqp_qp_create_cmd(struct irdma_sc_dev *dev, struct irdma_sc_qp *qp);
int irdma_cqp_cq_create_cmd(struct irdma_sc_dev *dev, struct irdma_sc_cq *cq);
int irdma_cqp_qp_destroy_cmd(struct irdma_sc_dev *dev, struct irdma_sc_qp *qp);
void irdma_cqp_cq_destroy_cmd(struct irdma_sc_dev *dev, struct irdma_sc_cq *cq);
void irdma_puda_ieq_get_ah_info(struct irdma_sc_qp *qp,
				struct irdma_ah_info *ah_info);
int irdma_puda_create_ah(struct irdma_sc_dev *dev,
			 struct irdma_ah_info *ah_info, bool wait,
			 enum puda_rsrc_type type, void *cb_param,
			 struct irdma_sc_ah **ah);
void irdma_puda_free_ah(struct irdma_sc_dev *dev, struct irdma_sc_ah *ah);
void irdma_ieq_process_fpdus(struct irdma_sc_qp *qp,
			     struct irdma_puda_rsrc *ieq);
void irdma_ieq_cleanup_qp(struct irdma_puda_rsrc *ieq, struct irdma_sc_qp *qp);
#endif /*IRDMA_PROTOS_H */
