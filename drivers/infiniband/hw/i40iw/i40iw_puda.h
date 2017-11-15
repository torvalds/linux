/*******************************************************************************
*
* Copyright (c) 2015-2016 Intel Corporation.  All rights reserved.
*
* This software is available to you under a choice of one of two
* licenses.  You may choose to be licensed under the terms of the GNU
* General Public License (GPL) Version 2, available from the file
* COPYING in the main directory of this source tree, or the
* OpenFabrics.org BSD license below:
*
*   Redistribution and use in source and binary forms, with or
*   without modification, are permitted provided that the following
*   conditions are met:
*
*    - Redistributions of source code must retain the above
*	copyright notice, this list of conditions and the following
*	disclaimer.
*
*    - Redistributions in binary form must reproduce the above
*	copyright notice, this list of conditions and the following
*	disclaimer in the documentation and/or other materials
*	provided with the distribution.
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
*******************************************************************************/

#ifndef I40IW_PUDA_H
#define I40IW_PUDA_H

#define I40IW_IEQ_MPA_FRAMING 6

struct i40iw_sc_dev;
struct i40iw_sc_qp;
struct i40iw_sc_cq;

enum puda_resource_type {
	I40IW_PUDA_RSRC_TYPE_ILQ = 1,
	I40IW_PUDA_RSRC_TYPE_IEQ
};

enum puda_rsrc_complete {
	PUDA_CQ_CREATED = 1,
	PUDA_QP_CREATED,
	PUDA_TX_COMPLETE,
	PUDA_RX_COMPLETE,
	PUDA_HASH_CRC_COMPLETE
};

struct i40iw_puda_completion_info {
	struct i40iw_qp_uk *qp;
	u8 q_type;
	u8 vlan_valid;
	u8 l3proto;
	u8 l4proto;
	u16 payload_len;
	u32 compl_error;	/* No_err=0, else major and minor err code */
	u32 qp_id;
	u32 wqe_idx;
};

struct i40iw_puda_send_info {
	u64 paddr;		/* Physical address */
	u32 len;
	u8 tcplen;
	u8 maclen;
	bool ipv4;
	bool doloopback;
	void *scratch;
};

struct i40iw_puda_buf {
	struct list_head list;	/* MUST be first entry */
	struct i40iw_dma_mem mem;	/* DMA memory for the buffer */
	struct i40iw_puda_buf *next;	/* for alloclist in rsrc struct */
	struct i40iw_virt_mem buf_mem;	/* Buffer memory for this buffer */
	void *scratch;
	u8 *iph;
	u8 *tcph;
	u8 *data;
	u16 datalen;
	u16 vlan_id;
	u8 tcphlen;		/* tcp length in bytes */
	u8 maclen;		/* mac length in bytes */
	u32 totallen;		/* machlen+iphlen+tcphlen+datalen */
	atomic_t refcount;
	u8 hdrlen;
	bool ipv4;
	u32 seqnum;
};

struct i40iw_puda_rsrc_info {
	enum puda_resource_type type;	/* ILQ or IEQ */
	u32 count;
	u16 pd_id;
	u32 cq_id;
	u32 qp_id;
	u32 sq_size;
	u32 rq_size;
	u16 buf_size;
	u16 mss;
	u32 tx_buf_cnt;		/* total bufs allocated will be rq_size + tx_buf_cnt */
	void (*receive)(struct i40iw_sc_vsi *, struct i40iw_puda_buf *);
	void (*xmit_complete)(struct i40iw_sc_vsi *, void *);
};

struct i40iw_puda_rsrc {
	struct i40iw_sc_cq cq;
	struct i40iw_sc_qp qp;
	struct i40iw_sc_pd sc_pd;
	struct i40iw_sc_dev *dev;
	struct i40iw_sc_vsi *vsi;
	struct i40iw_dma_mem cqmem;
	struct i40iw_dma_mem qpmem;
	struct i40iw_virt_mem ilq_mem;
	enum puda_rsrc_complete completion;
	enum puda_resource_type type;
	u16 buf_size;		/*buffer must be max datalen + tcpip hdr + mac */
	u16 mss;
	u32 cq_id;
	u32 qp_id;
	u32 sq_size;
	u32 rq_size;
	u32 cq_size;
	struct i40iw_sq_uk_wr_trk_info *sq_wrtrk_array;
	u64 *rq_wrid_array;
	u32 compl_rxwqe_idx;
	u32 rx_wqe_idx;
	u32 rxq_invalid_cnt;
	u32 tx_wqe_avail_cnt;
	bool check_crc;
	struct shash_desc *hash_desc;
	struct list_head txpend;
	struct list_head bufpool;	/* free buffers pool list for recv and xmit */
	u32 alloc_buf_count;
	u32 avail_buf_count;		/* snapshot of currently available buffers */
	spinlock_t bufpool_lock;
	struct i40iw_puda_buf *alloclist;
	void (*receive)(struct i40iw_sc_vsi *, struct i40iw_puda_buf *);
	void (*xmit_complete)(struct i40iw_sc_vsi *, void *);
	/* puda stats */
	u64 stats_buf_alloc_fail;
	u64 stats_pkt_rcvd;
	u64 stats_pkt_sent;
	u64 stats_rcvd_pkt_err;
	u64 stats_sent_pkt_q;
	u64 stats_bad_qp_id;
};

struct i40iw_puda_buf *i40iw_puda_get_bufpool(struct i40iw_puda_rsrc *rsrc);
void i40iw_puda_ret_bufpool(struct i40iw_puda_rsrc *rsrc,
			    struct i40iw_puda_buf *buf);
void i40iw_puda_send_buf(struct i40iw_puda_rsrc *rsrc,
			 struct i40iw_puda_buf *buf);
enum i40iw_status_code i40iw_puda_send(struct i40iw_sc_qp *qp,
				       struct i40iw_puda_send_info *info);
enum i40iw_status_code i40iw_puda_create_rsrc(struct i40iw_sc_vsi *vsi,
					      struct i40iw_puda_rsrc_info *info);
void i40iw_puda_dele_resources(struct i40iw_sc_vsi *vsi,
			       enum puda_resource_type type,
			       bool reset);
enum i40iw_status_code i40iw_puda_poll_completion(struct i40iw_sc_dev *dev,
						  struct i40iw_sc_cq *cq, u32 *compl_err);

struct i40iw_sc_qp *i40iw_ieq_get_qp(struct i40iw_sc_dev *dev,
				     struct i40iw_puda_buf *buf);
enum i40iw_status_code i40iw_puda_get_tcpip_info(struct i40iw_puda_completion_info *info,
						 struct i40iw_puda_buf *buf);
enum i40iw_status_code i40iw_ieq_check_mpacrc(struct shash_desc *desc,
					      void *addr, u32 length, u32 value);
enum i40iw_status_code i40iw_init_hash_desc(struct shash_desc **desc);
void i40iw_ieq_mpa_crc_ae(struct i40iw_sc_dev *dev, struct i40iw_sc_qp *qp);
void i40iw_free_hash_desc(struct shash_desc *desc);
void i40iw_ieq_update_tcpip_info(struct i40iw_puda_buf *buf, u16 length,
				 u32 seqnum);
enum i40iw_status_code i40iw_cqp_qp_create_cmd(struct i40iw_sc_dev *dev, struct i40iw_sc_qp *qp);
enum i40iw_status_code i40iw_cqp_cq_create_cmd(struct i40iw_sc_dev *dev, struct i40iw_sc_cq *cq);
void i40iw_cqp_qp_destroy_cmd(struct i40iw_sc_dev *dev, struct i40iw_sc_qp *qp);
void i40iw_cqp_cq_destroy_cmd(struct i40iw_sc_dev *dev, struct i40iw_sc_cq *cq);
#endif
