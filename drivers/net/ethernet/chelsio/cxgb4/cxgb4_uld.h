/*
 * This file is part of the Chelsio T4 Ethernet driver for Linux.
 *
 * Copyright (c) 2003-2016 Chelsio Communications, Inc. All rights reserved.
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
 */

#ifndef __CXGB4_ULD_H
#define __CXGB4_ULD_H

#include <linux/cache.h>
#include <linux/spinlock.h>
#include <linux/skbuff.h>
#include <linux/inetdevice.h>
#include <linux/atomic.h>
#include "cxgb4.h"

#define MAX_ULD_QSETS 16

/* CPL message priority levels */
enum {
	CPL_PRIORITY_DATA     = 0,  /* data messages */
	CPL_PRIORITY_SETUP    = 1,  /* connection setup messages */
	CPL_PRIORITY_TEARDOWN = 0,  /* connection teardown messages */
	CPL_PRIORITY_LISTEN   = 1,  /* listen start/stop messages */
	CPL_PRIORITY_ACK      = 1,  /* RX ACK messages */
	CPL_PRIORITY_CONTROL  = 1   /* control messages */
};

#define INIT_TP_WR(w, tid) do { \
	(w)->wr.wr_hi = htonl(FW_WR_OP_V(FW_TP_WR) | \
			      FW_WR_IMMDLEN_V(sizeof(*w) - sizeof(w->wr))); \
	(w)->wr.wr_mid = htonl(FW_WR_LEN16_V(DIV_ROUND_UP(sizeof(*w), 16)) | \
			       FW_WR_FLOWID_V(tid)); \
	(w)->wr.wr_lo = cpu_to_be64(0); \
} while (0)

#define INIT_TP_WR_CPL(w, cpl, tid) do { \
	INIT_TP_WR(w, tid); \
	OPCODE_TID(w) = htonl(MK_OPCODE_TID(cpl, tid)); \
} while (0)

#define INIT_ULPTX_WR(w, wrlen, atomic, tid) do { \
	(w)->wr.wr_hi = htonl(FW_WR_OP_V(FW_ULPTX_WR) | \
			      FW_WR_ATOMIC_V(atomic)); \
	(w)->wr.wr_mid = htonl(FW_WR_LEN16_V(DIV_ROUND_UP(wrlen, 16)) | \
			       FW_WR_FLOWID_V(tid)); \
	(w)->wr.wr_lo = cpu_to_be64(0); \
} while (0)

/* Special asynchronous notification message */
#define CXGB4_MSG_AN ((void *)1)
#define TX_ULD(uld)(((uld) != CXGB4_ULD_CRYPTO) ? CXGB4_TX_OFLD :\
		      CXGB4_TX_CRYPTO)

struct serv_entry {
	void *data;
};

union aopen_entry {
	void *data;
	union aopen_entry *next;
};

struct eotid_entry {
	void *data;
};

/*
 * Holds the size, base address, free list start, etc of the TID, server TID,
 * and active-open TID tables.  The tables themselves are allocated dynamically.
 */
struct tid_info {
	void **tid_tab;
	unsigned int ntids;

	struct serv_entry *stid_tab;
	unsigned long *stid_bmap;
	unsigned int nstids;
	unsigned int stid_base;
	unsigned int hash_base;

	union aopen_entry *atid_tab;
	unsigned int natids;
	unsigned int atid_base;

	struct filter_entry *hpftid_tab;
	unsigned long *hpftid_bmap;
	unsigned int nhpftids;
	unsigned int hpftid_base;

	struct filter_entry *ftid_tab;
	unsigned long *ftid_bmap;
	unsigned int nftids;
	unsigned int ftid_base;
	unsigned int aftid_base;
	unsigned int aftid_end;
	/* Server filter region */
	unsigned int sftid_base;
	unsigned int nsftids;

	spinlock_t atid_lock ____cacheline_aligned_in_smp;
	union aopen_entry *afree;
	unsigned int atids_in_use;

	spinlock_t stid_lock;
	unsigned int stids_in_use;
	unsigned int v6_stids_in_use;
	unsigned int sftids_in_use;

	/* ETHOFLD range */
	struct eotid_entry *eotid_tab;
	unsigned long *eotid_bmap;
	unsigned int eotid_base;
	unsigned int neotids;

	/* TIDs in the TCAM */
	atomic_t tids_in_use;
	/* TIDs in the HASH */
	atomic_t hash_tids_in_use;
	atomic_t conns_in_use;
	/* lock for setting/clearing filter bitmap */
	spinlock_t ftid_lock;
};

static inline void *lookup_tid(const struct tid_info *t, unsigned int tid)
{
	return tid < t->ntids ? t->tid_tab[tid] : NULL;
}

static inline void *lookup_atid(const struct tid_info *t, unsigned int atid)
{
	return atid < t->natids ? t->atid_tab[atid].data : NULL;
}

static inline void *lookup_stid(const struct tid_info *t, unsigned int stid)
{
	/* Is it a server filter TID? */
	if (t->nsftids && (stid >= t->sftid_base)) {
		stid -= t->sftid_base;
		stid += t->nstids;
	} else {
		stid -= t->stid_base;
	}

	return stid < (t->nstids + t->nsftids) ? t->stid_tab[stid].data : NULL;
}

static inline void cxgb4_insert_tid(struct tid_info *t, void *data,
				    unsigned int tid, unsigned short family)
{
	t->tid_tab[tid] = data;
	if (t->hash_base && (tid >= t->hash_base)) {
		if (family == AF_INET6)
			atomic_add(2, &t->hash_tids_in_use);
		else
			atomic_inc(&t->hash_tids_in_use);
	} else {
		if (family == AF_INET6)
			atomic_add(2, &t->tids_in_use);
		else
			atomic_inc(&t->tids_in_use);
	}
	atomic_inc(&t->conns_in_use);
}

static inline struct eotid_entry *cxgb4_lookup_eotid(struct tid_info *t,
						     u32 eotid)
{
	return eotid < t->neotids ? &t->eotid_tab[eotid] : NULL;
}

static inline int cxgb4_get_free_eotid(struct tid_info *t)
{
	int eotid;

	eotid = find_first_zero_bit(t->eotid_bmap, t->neotids);
	if (eotid >= t->neotids)
		eotid = -1;

	return eotid;
}

static inline void cxgb4_alloc_eotid(struct tid_info *t, u32 eotid, void *data)
{
	set_bit(eotid, t->eotid_bmap);
	t->eotid_tab[eotid].data = data;
}

static inline void cxgb4_free_eotid(struct tid_info *t, u32 eotid)
{
	clear_bit(eotid, t->eotid_bmap);
	t->eotid_tab[eotid].data = NULL;
}

int cxgb4_alloc_atid(struct tid_info *t, void *data);
int cxgb4_alloc_stid(struct tid_info *t, int family, void *data);
int cxgb4_alloc_sftid(struct tid_info *t, int family, void *data);
void cxgb4_free_atid(struct tid_info *t, unsigned int atid);
void cxgb4_free_stid(struct tid_info *t, unsigned int stid, int family);
void cxgb4_remove_tid(struct tid_info *t, unsigned int qid, unsigned int tid,
		      unsigned short family);
struct in6_addr;

int cxgb4_create_server(const struct net_device *dev, unsigned int stid,
			__be32 sip, __be16 sport, __be16 vlan,
			unsigned int queue);
int cxgb4_create_server6(const struct net_device *dev, unsigned int stid,
			 const struct in6_addr *sip, __be16 sport,
			 unsigned int queue);
int cxgb4_remove_server(const struct net_device *dev, unsigned int stid,
			unsigned int queue, bool ipv6);
int cxgb4_create_server_filter(const struct net_device *dev, unsigned int stid,
			       __be32 sip, __be16 sport, __be16 vlan,
			       unsigned int queue,
			       unsigned char port, unsigned char mask);
int cxgb4_remove_server_filter(const struct net_device *dev, unsigned int stid,
			       unsigned int queue, bool ipv6);

/* Filter operation context to allow callers of cxgb4_set_filter() and
 * cxgb4_del_filter() to wait for an asynchronous completion.
 */
struct filter_ctx {
	struct completion completion;	/* completion rendezvous */
	void *closure;			/* caller's opaque information */
	int result;			/* result of operation */
	u32 tid;			/* to store tid */
};

struct ch_filter_specification;

int cxgb4_get_free_ftid(struct net_device *dev, int family);
int __cxgb4_set_filter(struct net_device *dev, int filter_id,
		       struct ch_filter_specification *fs,
		       struct filter_ctx *ctx);
int __cxgb4_del_filter(struct net_device *dev, int filter_id,
		       struct ch_filter_specification *fs,
		       struct filter_ctx *ctx);
int cxgb4_set_filter(struct net_device *dev, int filter_id,
		     struct ch_filter_specification *fs);
int cxgb4_del_filter(struct net_device *dev, int filter_id,
		     struct ch_filter_specification *fs);
int cxgb4_get_filter_counters(struct net_device *dev, unsigned int fidx,
			      u64 *hitcnt, u64 *bytecnt, bool hash);

static inline void set_wr_txq(struct sk_buff *skb, int prio, int queue)
{
	skb_set_queue_mapping(skb, (queue << 1) | prio);
}

enum cxgb4_uld {
	CXGB4_ULD_INIT,
	CXGB4_ULD_RDMA,
	CXGB4_ULD_ISCSI,
	CXGB4_ULD_ISCSIT,
	CXGB4_ULD_CRYPTO,
	CXGB4_ULD_TLS,
	CXGB4_ULD_MAX
};

enum cxgb4_tx_uld {
	CXGB4_TX_OFLD,
	CXGB4_TX_CRYPTO,
	CXGB4_TX_MAX
};

enum cxgb4_txq_type {
	CXGB4_TXQ_ETH,
	CXGB4_TXQ_ULD,
	CXGB4_TXQ_CTRL,
	CXGB4_TXQ_MAX
};

enum cxgb4_state {
	CXGB4_STATE_UP,
	CXGB4_STATE_START_RECOVERY,
	CXGB4_STATE_DOWN,
	CXGB4_STATE_DETACH,
	CXGB4_STATE_FATAL_ERROR
};

enum cxgb4_control {
	CXGB4_CONTROL_DB_FULL,
	CXGB4_CONTROL_DB_EMPTY,
	CXGB4_CONTROL_DB_DROP,
};

struct pci_dev;
struct l2t_data;
struct net_device;
struct pkt_gl;
struct tp_tcp_stats;
struct t4_lro_mgr;

struct cxgb4_range {
	unsigned int start;
	unsigned int size;
};

struct cxgb4_virt_res {                      /* virtualized HW resources */
	struct cxgb4_range ddp;
	struct cxgb4_range iscsi;
	struct cxgb4_range stag;
	struct cxgb4_range rq;
	struct cxgb4_range srq;
	struct cxgb4_range pbl;
	struct cxgb4_range qp;
	struct cxgb4_range cq;
	struct cxgb4_range ocq;
	struct cxgb4_range key;
	unsigned int ncrypto_fc;
	struct cxgb4_range ppod_edram;
};

struct chcr_stats_debug {
	atomic_t cipher_rqst;
	atomic_t digest_rqst;
	atomic_t aead_rqst;
	atomic_t complete;
	atomic_t error;
	atomic_t fallback;
	atomic_t ipsec_cnt;
	atomic_t tls_pdu_tx;
	atomic_t tls_pdu_rx;
	atomic_t tls_key;
};

#define OCQ_WIN_OFFSET(pdev, vres) \
	(pci_resource_len((pdev), 2) - roundup_pow_of_two((vres)->ocq.size))

/*
 * Block of information the LLD provides to ULDs attaching to a device.
 */
struct cxgb4_lld_info {
	struct pci_dev *pdev;                /* associated PCI device */
	struct l2t_data *l2t;                /* L2 table */
	struct tid_info *tids;               /* TID table */
	struct net_device **ports;           /* device ports */
	const struct cxgb4_virt_res *vr;     /* assorted HW resources */
	const unsigned short *mtus;          /* MTU table */
	const unsigned short *rxq_ids;       /* the ULD's Rx queue ids */
	const unsigned short *ciq_ids;       /* the ULD's concentrator IQ ids */
	unsigned short nrxq;                 /* # of Rx queues */
	unsigned short ntxq;                 /* # of Tx queues */
	unsigned short nciq;		     /* # of concentrator IQ */
	unsigned char nchan:4;               /* # of channels */
	unsigned char nports:4;              /* # of ports */
	unsigned char wr_cred;               /* WR 16-byte credits */
	unsigned char adapter_type;          /* type of adapter */
	unsigned char fw_api_ver;            /* FW API version */
	unsigned char crypto;                /* crypto support */
	unsigned int fw_vers;                /* FW version */
	unsigned int iscsi_iolen;            /* iSCSI max I/O length */
	unsigned int cclk_ps;                /* Core clock period in psec */
	unsigned short udb_density;          /* # of user DB/page */
	unsigned short ucq_density;          /* # of user CQs/page */
	unsigned int sge_host_page_size;     /* SGE host page size */
	unsigned short filt_mode;            /* filter optional components */
	unsigned short tx_modq[NCHAN];       /* maps each tx channel to a */
					     /* scheduler queue */
	void __iomem *gts_reg;               /* address of GTS register */
	void __iomem *db_reg;                /* address of kernel doorbell */
	int dbfifo_int_thresh;		     /* doorbell fifo int threshold */
	unsigned int sge_ingpadboundary;     /* SGE ingress padding boundary */
	unsigned int sge_egrstatuspagesize;  /* SGE egress status page size */
	unsigned int sge_pktshift;           /* Padding between CPL and */
					     /*	packet data */
	unsigned int pf;		     /* Physical Function we're using */
	bool enable_fw_ofld_conn;            /* Enable connection through fw */
					     /* WR */
	unsigned int max_ordird_qp;          /* Max ORD/IRD depth per RDMA QP */
	unsigned int max_ird_adapter;        /* Max IRD memory per adapter */
	bool ulptx_memwrite_dsgl;            /* use of T5 DSGL allowed */
	unsigned int iscsi_tagmask;	     /* iscsi ddp tag mask */
	unsigned int iscsi_pgsz_order;	     /* iscsi ddp page size orders */
	unsigned int iscsi_llimit;	     /* chip's iscsi region llimit */
	unsigned int ulp_crypto;             /* crypto lookaside support */
	void **iscsi_ppm;		     /* iscsi page pod manager */
	int nodeid;			     /* device numa node id */
	bool fr_nsmr_tpte_wr_support;	     /* FW supports FR_NSMR_TPTE_WR */
	bool write_w_imm_support;         /* FW supports WRITE_WITH_IMMEDIATE */
	bool write_cmpl_support;             /* FW supports WRITE_CMPL WR */
};

struct cxgb4_uld_info {
	char name[IFNAMSIZ];
	void *handle;
	unsigned int nrxq;
	unsigned int rxq_size;
	unsigned int ntxq;
	bool ciq;
	bool lro;
	void *(*add)(const struct cxgb4_lld_info *p);
	int (*rx_handler)(void *handle, const __be64 *rsp,
			  const struct pkt_gl *gl);
	int (*state_change)(void *handle, enum cxgb4_state new_state);
	int (*control)(void *handle, enum cxgb4_control control, ...);
	int (*lro_rx_handler)(void *handle, const __be64 *rsp,
			      const struct pkt_gl *gl,
			      struct t4_lro_mgr *lro_mgr,
			      struct napi_struct *napi);
	void (*lro_flush)(struct t4_lro_mgr *);
	int (*tx_handler)(struct sk_buff *skb, struct net_device *dev);
};

void cxgb4_register_uld(enum cxgb4_uld type, const struct cxgb4_uld_info *p);
int cxgb4_unregister_uld(enum cxgb4_uld type);
int cxgb4_ofld_send(struct net_device *dev, struct sk_buff *skb);
int cxgb4_immdata_send(struct net_device *dev, unsigned int idx,
		       const void *src, unsigned int len);
int cxgb4_crypto_send(struct net_device *dev, struct sk_buff *skb);
unsigned int cxgb4_dbfifo_count(const struct net_device *dev, int lpfifo);
unsigned int cxgb4_port_chan(const struct net_device *dev);
unsigned int cxgb4_port_e2cchan(const struct net_device *dev);
unsigned int cxgb4_port_viid(const struct net_device *dev);
unsigned int cxgb4_tp_smt_idx(enum chip_type chip, unsigned int viid);
unsigned int cxgb4_port_idx(const struct net_device *dev);
unsigned int cxgb4_best_mtu(const unsigned short *mtus, unsigned short mtu,
			    unsigned int *idx);
unsigned int cxgb4_best_aligned_mtu(const unsigned short *mtus,
				    unsigned short header_size,
				    unsigned short data_size_max,
				    unsigned short data_size_align,
				    unsigned int *mtu_idxp);
void cxgb4_get_tcp_stats(struct pci_dev *pdev, struct tp_tcp_stats *v4,
			 struct tp_tcp_stats *v6);
void cxgb4_iscsi_init(struct net_device *dev, unsigned int tag_mask,
		      const unsigned int *pgsz_order);
struct sk_buff *cxgb4_pktgl_to_skb(const struct pkt_gl *gl,
				   unsigned int skb_len, unsigned int pull_len);
int cxgb4_sync_txq_pidx(struct net_device *dev, u16 qid, u16 pidx, u16 size);
int cxgb4_flush_eq_cache(struct net_device *dev);
int cxgb4_read_tpte(struct net_device *dev, u32 stag, __be32 *tpte);
u64 cxgb4_read_sge_timestamp(struct net_device *dev);

enum cxgb4_bar2_qtype { CXGB4_BAR2_QTYPE_EGRESS, CXGB4_BAR2_QTYPE_INGRESS };
int cxgb4_bar2_sge_qregs(struct net_device *dev,
			 unsigned int qid,
			 enum cxgb4_bar2_qtype qtype,
			 int user,
			 u64 *pbar2_qoffset,
			 unsigned int *pbar2_qid);

#endif  /* !__CXGB4_ULD_H */
