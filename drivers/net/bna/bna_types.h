/*
 * Linux network driver for Brocade Converged Network Adapter.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (GPL) Version 2 as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */
/*
 * Copyright (c) 2005-2010 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 */
#ifndef __BNA_TYPES_H__
#define __BNA_TYPES_H__

#include "cna.h"
#include "bna_hw.h"
#include "bfa_cee.h"

/**
 *
 * Forward declarations
 *
 */

struct bna_txq;
struct bna_tx;
struct bna_rxq;
struct bna_cq;
struct bna_rx;
struct bna_rxf;
struct bna_port;
struct bna;
struct bnad;

/**
 *
 * Enums, primitive data types
 *
 */

enum bna_status {
	BNA_STATUS_T_DISABLED	= 0,
	BNA_STATUS_T_ENABLED	= 1
};

enum bna_cleanup_type {
	BNA_HARD_CLEANUP 	= 0,
	BNA_SOFT_CLEANUP 	= 1
};

enum bna_cb_status {
	BNA_CB_SUCCESS 		= 0,
	BNA_CB_FAIL		= 1,
	BNA_CB_INTERRUPT	= 2,
	BNA_CB_BUSY		= 3,
	BNA_CB_INVALID_MAC	= 4,
	BNA_CB_MCAST_LIST_FULL	= 5,
	BNA_CB_UCAST_CAM_FULL	= 6,
	BNA_CB_WAITING		= 7,
	BNA_CB_NOT_EXEC		= 8
};

enum bna_res_type {
	BNA_RES_T_MEM		= 1,
	BNA_RES_T_INTR		= 2
};

enum bna_mem_type {
	BNA_MEM_T_KVA 		= 1,
	BNA_MEM_T_DMA 		= 2
};

enum bna_intr_type {
	BNA_INTR_T_INTX		= 1,
	BNA_INTR_T_MSIX		= 2
};

enum bna_res_req_type {
	BNA_RES_MEM_T_COM 		= 0,
	BNA_RES_MEM_T_ATTR 		= 1,
	BNA_RES_MEM_T_FWTRC 		= 2,
	BNA_RES_MEM_T_STATS 		= 3,
	BNA_RES_MEM_T_SWSTATS		= 4,
	BNA_RES_MEM_T_IBIDX		= 5,
	BNA_RES_MEM_T_IB_ARRAY		= 6,
	BNA_RES_MEM_T_INTR_ARRAY	= 7,
	BNA_RES_MEM_T_IDXSEG_ARRAY	= 8,
	BNA_RES_MEM_T_TX_ARRAY		= 9,
	BNA_RES_MEM_T_TXQ_ARRAY		= 10,
	BNA_RES_MEM_T_RX_ARRAY		= 11,
	BNA_RES_MEM_T_RXP_ARRAY		= 12,
	BNA_RES_MEM_T_RXQ_ARRAY		= 13,
	BNA_RES_MEM_T_UCMAC_ARRAY	= 14,
	BNA_RES_MEM_T_MCMAC_ARRAY	= 15,
	BNA_RES_MEM_T_RIT_ENTRY		= 16,
	BNA_RES_MEM_T_RIT_SEGMENT	= 17,
	BNA_RES_INTR_T_MBOX		= 18,
	BNA_RES_T_MAX
};

enum bna_tx_res_req_type {
	BNA_TX_RES_MEM_T_TCB	= 0,
	BNA_TX_RES_MEM_T_UNMAPQ	= 1,
	BNA_TX_RES_MEM_T_QPT 	= 2,
	BNA_TX_RES_MEM_T_SWQPT	= 3,
	BNA_TX_RES_MEM_T_PAGE 	= 4,
	BNA_TX_RES_INTR_T_TXCMPL = 5,
	BNA_TX_RES_T_MAX,
};

enum bna_rx_mem_type {
	BNA_RX_RES_MEM_T_CCB		= 0,	/* CQ context */
	BNA_RX_RES_MEM_T_RCB		= 1,	/* CQ context */
	BNA_RX_RES_MEM_T_UNMAPQ		= 2,	/* UnmapQ for RxQs */
	BNA_RX_RES_MEM_T_CQPT		= 3,	/* CQ QPT */
	BNA_RX_RES_MEM_T_CSWQPT		= 4,	/* S/W QPT */
	BNA_RX_RES_MEM_T_CQPT_PAGE	= 5,	/* CQPT page */
	BNA_RX_RES_MEM_T_HQPT		= 6,	/* RX QPT */
	BNA_RX_RES_MEM_T_DQPT		= 7,	/* RX QPT */
	BNA_RX_RES_MEM_T_HSWQPT		= 8,	/* RX s/w QPT */
	BNA_RX_RES_MEM_T_DSWQPT		= 9,	/* RX s/w QPT */
	BNA_RX_RES_MEM_T_DPAGE		= 10,	/* RX s/w QPT */
	BNA_RX_RES_MEM_T_HPAGE		= 11,	/* RX s/w QPT */
	BNA_RX_RES_T_INTR		= 12,	/* Rx interrupts */
	BNA_RX_RES_T_MAX		= 13
};

enum bna_mbox_state {
	BNA_MBOX_FREE		= 0,
	BNA_MBOX_POSTED		= 1
};

enum bna_tx_type {
	BNA_TX_T_REGULAR	= 0,
	BNA_TX_T_LOOPBACK	= 1,
};

enum bna_tx_flags {
	BNA_TX_F_PORT_STARTED	= 1,
	BNA_TX_F_ENABLED	= 2,
	BNA_TX_F_PRIO_LOCK	= 4,
};

enum bna_tx_mod_flags {
	BNA_TX_MOD_F_PORT_STARTED	= 1,
	BNA_TX_MOD_F_PORT_LOOPBACK	= 2,
};

enum bna_rx_type {
	BNA_RX_T_REGULAR	= 0,
	BNA_RX_T_LOOPBACK	= 1,
};

enum bna_rxp_type {
	BNA_RXP_SINGLE 		= 1,
	BNA_RXP_SLR 		= 2,
	BNA_RXP_HDS 		= 3
};

enum bna_rxmode {
	BNA_RXMODE_PROMISC 	= 1,
	BNA_RXMODE_ALLMULTI 	= 2
};

enum bna_rx_event {
	RX_E_START			= 1,
	RX_E_STOP			= 2,
	RX_E_FAIL			= 3,
	RX_E_RXF_STARTED		= 4,
	RX_E_RXF_STOPPED		= 5,
	RX_E_RXQ_STOPPED		= 6,
};

enum bna_rx_state {
	BNA_RX_STOPPED			= 1,
	BNA_RX_RXF_START_WAIT		= 2,
	BNA_RX_STARTED			= 3,
	BNA_RX_RXF_STOP_WAIT		= 4,
	BNA_RX_RXQ_STOP_WAIT		= 5,
};

enum bna_rx_flags {
	BNA_RX_F_ENABLE		= 0x01,		/* bnad enabled rxf */
	BNA_RX_F_PORT_ENABLED	= 0x02,		/* Port object is enabled */
	BNA_RX_F_PORT_FAILED	= 0x04,		/* Port in failed state */
};

enum bna_rx_mod_flags {
	BNA_RX_MOD_F_PORT_STARTED	= 1,
	BNA_RX_MOD_F_PORT_LOOPBACK	= 2,
};

enum bna_rxf_oper_state {
	BNA_RXF_OPER_STATE_RUNNING	= 0x01, /* rxf operational */
	BNA_RXF_OPER_STATE_PAUSED	= 0x02,	/* rxf in PAUSED state */
};

enum bna_rxf_flags {
	BNA_RXF_FL_STOP_PENDING 	= 0x01,
	BNA_RXF_FL_FAILED		= 0x02,
	BNA_RXF_FL_RSS_CONFIG_PENDING	= 0x04,
	BNA_RXF_FL_OPERSTATE_CHANGED	= 0x08,
	BNA_RXF_FL_RXF_ENABLED		= 0x10,
	BNA_RXF_FL_VLAN_CONFIG_PENDING	= 0x20,
};

enum bna_rxf_event {
	RXF_E_START			= 1,
	RXF_E_STOP			= 2,
	RXF_E_FAIL			= 3,
	RXF_E_CAM_FLTR_MOD		= 4,
	RXF_E_STARTED			= 5,
	RXF_E_STOPPED			= 6,
	RXF_E_CAM_FLTR_RESP		= 7,
	RXF_E_PAUSE			= 8,
	RXF_E_RESUME			= 9,
	RXF_E_STAT_CLEARED		= 10,
};

enum bna_rxf_state {
	BNA_RXF_STOPPED			= 1,
	BNA_RXF_START_WAIT		= 2,
	BNA_RXF_CAM_FLTR_MOD_WAIT	= 3,
	BNA_RXF_STARTED			= 4,
	BNA_RXF_CAM_FLTR_CLR_WAIT	= 5,
	BNA_RXF_STOP_WAIT		= 6,
	BNA_RXF_PAUSE_WAIT		= 7,
	BNA_RXF_RESUME_WAIT		= 8,
	BNA_RXF_STAT_CLR_WAIT		= 9,
};

enum bna_port_type {
	BNA_PORT_T_REGULAR		= 0,
	BNA_PORT_T_LOOPBACK_INTERNAL	= 1,
	BNA_PORT_T_LOOPBACK_EXTERNAL	= 2,
};

enum bna_link_status {
	BNA_LINK_DOWN		= 0,
	BNA_LINK_UP		= 1,
	BNA_CEE_UP 		= 2
};

enum bna_llport_flags {
	BNA_LLPORT_F_ADMIN_UP	 	= 1,
	BNA_LLPORT_F_PORT_ENABLED	= 2,
	BNA_LLPORT_F_RX_STARTED		= 4
};

enum bna_port_flags {
	BNA_PORT_F_DEVICE_READY	= 1,
	BNA_PORT_F_ENABLED	= 2,
	BNA_PORT_F_PAUSE_CHANGED = 4,
	BNA_PORT_F_MTU_CHANGED	= 8
};

enum bna_pkt_rates {
	BNA_PKT_RATE_10K		= 10000,
	BNA_PKT_RATE_20K		= 20000,
	BNA_PKT_RATE_30K		= 30000,
	BNA_PKT_RATE_40K		= 40000,
	BNA_PKT_RATE_50K		= 50000,
	BNA_PKT_RATE_60K		= 60000,
	BNA_PKT_RATE_70K		= 70000,
	BNA_PKT_RATE_80K		= 80000,
};

enum bna_dim_load_types {
	BNA_LOAD_T_HIGH_4		= 0, /* 80K <= r */
	BNA_LOAD_T_HIGH_3		= 1, /* 60K <= r < 80K */
	BNA_LOAD_T_HIGH_2		= 2, /* 50K <= r < 60K */
	BNA_LOAD_T_HIGH_1		= 3, /* 40K <= r < 50K */
	BNA_LOAD_T_LOW_1		= 4, /* 30K <= r < 40K */
	BNA_LOAD_T_LOW_2		= 5, /* 20K <= r < 30K */
	BNA_LOAD_T_LOW_3		= 6, /* 10K <= r < 20K */
	BNA_LOAD_T_LOW_4		= 7, /* r < 10K */
	BNA_LOAD_T_MAX			= 8
};

enum bna_dim_bias_types {
	BNA_BIAS_T_SMALL		= 0, /* small pkts > (large pkts * 2) */
	BNA_BIAS_T_LARGE		= 1, /* Not BNA_BIAS_T_SMALL */
	BNA_BIAS_T_MAX			= 2
};

struct bna_mac {
	/* This should be the first one */
	struct list_head			qe;
	u8			addr[ETH_ALEN];
};

struct bna_mem_descr {
	u32		len;
	void		*kva;
	struct bna_dma_addr dma;
};

struct bna_mem_info {
	enum bna_mem_type mem_type;
	u32		len;
	u32 		num;
	u32		align_sz; /* 0/1 = no alignment */
	struct bna_mem_descr *mdl;
	void			*cookie; /* For bnad to unmap dma later */
};

struct bna_intr_descr {
	int			vector;
};

struct bna_intr_info {
	enum bna_intr_type intr_type;
	int			num;
	struct bna_intr_descr *idl;
};

union bna_res_u {
	struct bna_mem_info mem_info;
	struct bna_intr_info intr_info;
};

struct bna_res_info {
	enum bna_res_type res_type;
	union bna_res_u		res_u;
};

/* HW QPT */
struct bna_qpt {
	struct bna_dma_addr hw_qpt_ptr;
	void		*kv_qpt_ptr;
	u32		page_count;
	u32		page_size;
};

/**
 *
 * Device
 *
 */

struct bna_device {
	bfa_fsm_t		fsm;
	struct bfa_ioc ioc;

	enum bna_intr_type intr_type;
	int			vector;

	void (*ready_cbfn)(struct bnad *bnad, enum bna_cb_status status);
	struct bnad *ready_cbarg;

	void (*stop_cbfn)(struct bnad *bnad, enum bna_cb_status status);
	struct bnad *stop_cbarg;

	struct bna *bna;
};

/**
 *
 * Mail box
 *
 */

struct bna_mbox_qe {
	/* This should be the first one */
	struct list_head			qe;

	struct bfa_mbox_cmd cmd;
	u32 		cmd_len;
	/* Callback for port, tx, rx, rxf */
	void (*cbfn)(void *arg, int status);
	void 			*cbarg;
};

struct bna_mbox_mod {
	enum bna_mbox_state state;
	struct list_head			posted_q;
	u32		msg_pending;
	u32		msg_ctr;
	struct bna *bna;
};

/**
 *
 * Port
 *
 */

/* Pause configuration */
struct bna_pause_config {
	enum bna_status tx_pause;
	enum bna_status rx_pause;
};

struct bna_llport {
	bfa_fsm_t		fsm;
	enum bna_llport_flags flags;

	enum bna_port_type type;

	enum bna_link_status link_status;

	int			rx_started_count;

	void (*stop_cbfn)(struct bna_port *, enum bna_cb_status);

	struct bna_mbox_qe mbox_qe;

	struct bna *bna;
};

struct bna_port {
	bfa_fsm_t		fsm;
	enum bna_port_flags flags;

	enum bna_port_type type;

	struct bna_llport llport;

	struct bna_pause_config pause_config;
	u8			priority;
	int			mtu;

	/* Callback for bna_port_disable(), port_stop() */
	void (*stop_cbfn)(void *, enum bna_cb_status);
	void			*stop_cbarg;

	/* Callback for bna_port_pause_config() */
	void (*pause_cbfn)(struct bnad *, enum bna_cb_status);

	/* Callback for bna_port_mtu_set() */
	void (*mtu_cbfn)(struct bnad *, enum bna_cb_status);

	void (*link_cbfn)(struct bnad *, enum bna_link_status);

	struct bfa_wc		chld_stop_wc;

	struct bna_mbox_qe mbox_qe;

	struct bna *bna;
};

/**
 *
 * Interrupt Block
 *
 */

/* IB index segment structure */
struct bna_ibidx_seg {
	/* This should be the first one */
	struct list_head			qe;

	u8			ib_seg_size;
	u8			ib_idx_tbl_offset;
};

/* Interrupt structure */
struct bna_intr {
	/* This should be the first one */
	struct list_head			qe;
	int			ref_count;

	enum bna_intr_type intr_type;
	int			vector;

	struct bna_ib *ib;
};

/* Doorbell structure */
struct bna_ib_dbell {
	void *__iomem doorbell_addr;
	u32		doorbell_ack;
};

/* Interrupt timer configuration */
struct bna_ib_config {
	u8 		coalescing_timeo;    /* Unit is 5usec. */

	int			interpkt_count;
	int			interpkt_timeo;

	enum ib_flags ctrl_flags;
};

/* IB structure */
struct bna_ib {
	/* This should be the first one */
	struct list_head			qe;

	int			ib_id;

	int			ref_count;
	int			start_count;

	struct bna_dma_addr ib_seg_host_addr;
	void		*ib_seg_host_addr_kva;
	u32		idx_mask; /* Size >= BNA_IBIDX_MAX_SEGSIZE */

	struct bna_ibidx_seg *idx_seg;

	struct bna_ib_dbell door_bell;

	struct bna_intr *intr;

	struct bna_ib_config ib_config;

	struct bna *bna;
};

/* IB module - keeps track of IBs and interrupts */
struct bna_ib_mod {
	struct bna_ib *ib;		/* BFI_MAX_IB entries */
	struct bna_intr *intr;		/* BFI_MAX_IB entries */
	struct bna_ibidx_seg *idx_seg;	/* BNA_IBIDX_TOTAL_SEGS */

	struct list_head			ib_free_q;

	struct list_head		ibidx_seg_pool[BFI_IBIDX_TOTAL_POOLS];

	struct list_head			intr_free_q;
	struct list_head			intr_active_q;

	struct bna *bna;
};

/**
 *
 * Tx object
 *
 */

/* Tx datapath control structure */
#define BNA_Q_NAME_SIZE		16
struct bna_tcb {
	/* Fast path */
	void			**sw_qpt;
	void			*unmap_q;
	u32		producer_index;
	u32		consumer_index;
	volatile u32	*hw_consumer_index;
	u32		q_depth;
	void *__iomem q_dbell;
	struct bna_ib_dbell *i_dbell;
	int			page_idx;
	int			page_count;
	/* Control path */
	struct bna_txq *txq;
	struct bnad *bnad;
	enum bna_intr_type intr_type;
	int			intr_vector;
	u8			priority; /* Current priority */
	unsigned long		flags; /* Used by bnad as required */
	int			id;
	char			name[BNA_Q_NAME_SIZE];
};

/* TxQ QPT and configuration */
struct bna_txq {
	/* This should be the first one */
	struct list_head			qe;

	int			txq_id;

	u8			priority;

	struct bna_qpt qpt;
	struct bna_tcb *tcb;
	struct bna_ib *ib;
	int			ib_seg_offset;

	struct bna_tx *tx;

	u64 		tx_packets;
	u64 		tx_bytes;
};

/* TxF structure (hardware Tx Function) */
struct bna_txf {
	int			txf_id;
	enum txf_flags ctrl_flags;
	u16		vlan;
};

/* Tx object */
struct bna_tx {
	/* This should be the first one */
	struct list_head			qe;

	bfa_fsm_t		fsm;
	enum bna_tx_flags flags;

	enum bna_tx_type type;

	struct list_head			txq_q;
	struct bna_txf txf;

	/* Tx event handlers */
	void (*tcb_setup_cbfn)(struct bnad *, struct bna_tcb *);
	void (*tcb_destroy_cbfn)(struct bnad *, struct bna_tcb *);
	void (*tx_stall_cbfn)(struct bnad *, struct bna_tcb *);
	void (*tx_resume_cbfn)(struct bnad *, struct bna_tcb *);
	void (*tx_cleanup_cbfn)(struct bnad *, struct bna_tcb *);

	/* callback for bna_tx_disable(), bna_tx_stop() */
	void (*stop_cbfn)(void *arg, struct bna_tx *tx,
				enum bna_cb_status status);
	void			*stop_cbarg;

	/* callback for bna_tx_prio_set() */
	void (*prio_change_cbfn)(struct bnad *bnad, struct bna_tx *tx,
				enum bna_cb_status status);

	struct bfa_wc		txq_stop_wc;

	struct bna_mbox_qe mbox_qe;

	struct bna *bna;
	void			*priv;	/* bnad's cookie */
};

struct bna_tx_config {
	int			num_txq;
	int			txq_depth;
	enum bna_tx_type tx_type;
};

struct bna_tx_event_cbfn {
	/* Optional */
	void (*tcb_setup_cbfn)(struct bnad *, struct bna_tcb *);
	void (*tcb_destroy_cbfn)(struct bnad *, struct bna_tcb *);
	/* Mandatory */
	void (*tx_stall_cbfn)(struct bnad *, struct bna_tcb *);
	void (*tx_resume_cbfn)(struct bnad *, struct bna_tcb *);
	void (*tx_cleanup_cbfn)(struct bnad *, struct bna_tcb *);
};

/* Tx module - keeps track of free, active tx objects */
struct bna_tx_mod {
	struct bna_tx *tx;		/* BFI_MAX_TXQ entries */
	struct bna_txq *txq;		/* BFI_MAX_TXQ entries */

	struct list_head			tx_free_q;
	struct list_head			tx_active_q;

	struct list_head			txq_free_q;

	/* callback for bna_tx_mod_stop() */
	void (*stop_cbfn)(struct bna_port *port,
				enum bna_cb_status status);

	struct bfa_wc		tx_stop_wc;

	enum bna_tx_mod_flags flags;

	int			priority;
	int			cee_link;

	u32		txf_bmap[2];

	struct bna *bna;
};

/**
 *
 * Receive Indirection Table
 *
 */

/* One row of RIT table */
struct bna_rit_entry {
	u8 large_rxq_id;	/* used for either large or data buffers */
	u8 small_rxq_id;	/* used for either small or header buffers */
};

/* RIT segment */
struct bna_rit_segment {
	struct list_head			qe;

	u32		rit_offset;
	u32		rit_size;
	/**
	 * max_rit_size: Varies per RIT segment depending on how RIT is
	 * partitioned
	 */
	u32		max_rit_size;

	struct bna_rit_entry *rit;
};

struct bna_rit_mod {
	struct bna_rit_entry *rit;
	struct bna_rit_segment *rit_segment;

	struct list_head		rit_seg_pool[BFI_RIT_SEG_TOTAL_POOLS];
};

/**
 *
 * Rx object
 *
 */

/* Rx datapath control structure */
struct bna_rcb {
	/* Fast path */
	void			**sw_qpt;
	void			*unmap_q;
	u32		producer_index;
	u32		consumer_index;
	u32		q_depth;
	void *__iomem q_dbell;
	int			page_idx;
	int			page_count;
	/* Control path */
	struct bna_rxq *rxq;
	struct bna_cq *cq;
	struct bnad *bnad;
	unsigned long		flags;
	int			id;
};

/* RxQ structure - QPT, configuration */
struct bna_rxq {
	struct list_head			qe;
	int			rxq_id;

	int			buffer_size;
	int			q_depth;

	struct bna_qpt qpt;
	struct bna_rcb *rcb;

	struct bna_rxp *rxp;
	struct bna_rx *rx;

	u64 		rx_packets;
	u64		rx_bytes;
	u64 		rx_packets_with_error;
	u64 		rxbuf_alloc_failed;
};

/* RxQ pair */
union bna_rxq_u {
	struct {
		struct bna_rxq *hdr;
		struct bna_rxq *data;
	} hds;
	struct {
		struct bna_rxq *small;
		struct bna_rxq *large;
	} slr;
	struct {
		struct bna_rxq *only;
		struct bna_rxq *reserved;
	} single;
};

/* Packet rate for Dynamic Interrupt Moderation */
struct bna_pkt_rate {
	u32		small_pkt_cnt;
	u32		large_pkt_cnt;
};

/* Completion control structure */
struct bna_ccb {
	/* Fast path */
	void			**sw_qpt;
	u32		producer_index;
	volatile u32	*hw_producer_index;
	u32		q_depth;
	struct bna_ib_dbell *i_dbell;
	struct bna_rcb *rcb[2];
	void			*ctrl; /* For bnad */
	struct bna_pkt_rate pkt_rate;
	int			page_idx;
	int			page_count;

	/* Control path */
	struct bna_cq *cq;
	struct bnad *bnad;
	enum bna_intr_type intr_type;
	int			intr_vector;
	u8			rx_coalescing_timeo; /* For NAPI */
	int			id;
	char			name[BNA_Q_NAME_SIZE];
};

/* CQ QPT, configuration  */
struct bna_cq {
	int			cq_id;

	struct bna_qpt qpt;
	struct bna_ccb *ccb;

	struct bna_ib *ib;
	u8			ib_seg_offset;

	struct bna_rx *rx;
};

struct bna_rss_config {
	enum rss_hash_type hash_type;
	u8			hash_mask;
	u32		toeplitz_hash_key[BFI_RSS_HASH_KEY_LEN];
};

struct bna_hds_config {
	enum hds_header_type hdr_type;
	int			header_size;
};

/* This structure is used during RX creation */
struct bna_rx_config {
	enum bna_rx_type rx_type;
	int			num_paths;
	enum bna_rxp_type rxp_type;
	int			paused;
	int			q_depth;
	/*
	 * Small/Large (or Header/Data) buffer size to be configured
	 * for SLR and HDS queue type. Large buffer size comes from
	 * port->mtu.
	 */
	int			small_buff_size;

	enum bna_status rss_status;
	struct bna_rss_config rss_config;

	enum bna_status hds_status;
	struct bna_hds_config hds_config;

	enum bna_status vlan_strip_status;
};

/* Rx Path structure - one per MSIX vector/CPU */
struct bna_rxp {
	/* This should be the first one */
	struct list_head			qe;

	enum bna_rxp_type type;
	union	bna_rxq_u	rxq;
	struct bna_cq cq;

	struct bna_rx *rx;

	/* MSI-x vector number for configuring RSS */
	int			vector;

	struct bna_mbox_qe mbox_qe;
};

/* HDS configuration structure */
struct bna_rxf_hds {
	enum hds_header_type hdr_type;
	int			header_size;
};

/* RSS configuration structure */
struct bna_rxf_rss {
	enum rss_hash_type hash_type;
	u8			hash_mask;
	u32		toeplitz_hash_key[BFI_RSS_HASH_KEY_LEN];
};

/* RxF structure (hardware Rx Function) */
struct bna_rxf {
	bfa_fsm_t		fsm;
	int			rxf_id;
	enum rxf_flags ctrl_flags;
	u16		default_vlan_tag;
	enum bna_rxf_oper_state rxf_oper_state;
	enum bna_status hds_status;
	struct bna_rxf_hds hds_cfg;
	enum bna_status rss_status;
	struct bna_rxf_rss rss_cfg;
	struct bna_rit_segment *rit_segment;
	struct bna_rx *rx;
	u32		forced_offset;
	struct bna_mbox_qe mbox_qe;
	int			mcast_rxq_id;

	/* callback for bna_rxf_start() */
	void (*start_cbfn) (struct bna_rx *rx, enum bna_cb_status status);
	struct bna_rx *start_cbarg;

	/* callback for bna_rxf_stop() */
	void (*stop_cbfn) (struct bna_rx *rx, enum bna_cb_status status);
	struct bna_rx *stop_cbarg;

	/* callback for bna_rxf_receive_enable() / bna_rxf_receive_disable() */
	void (*oper_state_cbfn) (struct bnad *bnad, struct bna_rx *rx,
			enum bna_cb_status status);
	struct bnad *oper_state_cbarg;

	/**
	 * callback for:
	 *	bna_rxf_ucast_set()
	 *	bna_rxf_{ucast/mcast}_add(),
	 * 	bna_rxf_{ucast/mcast}_del(),
	 *	bna_rxf_mode_set()
	 */
	void (*cam_fltr_cbfn)(struct bnad *bnad, struct bna_rx *rx,
				enum bna_cb_status status);
	struct bnad *cam_fltr_cbarg;

	enum bna_rxf_flags rxf_flags;

	/* List of unicast addresses yet to be applied to h/w */
	struct list_head			ucast_pending_add_q;
	struct list_head			ucast_pending_del_q;
	int			ucast_pending_set;
	/* ucast addresses applied to the h/w */
	struct list_head			ucast_active_q;
	struct bna_mac *ucast_active_mac;

	/* List of multicast addresses yet to be applied to h/w */
	struct list_head			mcast_pending_add_q;
	struct list_head			mcast_pending_del_q;
	/* multicast addresses applied to the h/w */
	struct list_head			mcast_active_q;

	/* Rx modes yet to be applied to h/w */
	enum bna_rxmode rxmode_pending;
	enum bna_rxmode rxmode_pending_bitmask;
	/* Rx modes applied to h/w */
	enum bna_rxmode rxmode_active;

	enum bna_status vlan_filter_status;
	u32		vlan_filter_table[(BFI_MAX_VLAN + 1) / 32];
};

/* Rx object */
struct bna_rx {
	/* This should be the first one */
	struct list_head			qe;

	bfa_fsm_t		fsm;

	enum bna_rx_type type;

	/* list-head for RX path objects */
	struct list_head			rxp_q;

	struct bna_rxf rxf;

	enum bna_rx_flags rx_flags;

	struct bna_mbox_qe mbox_qe;

	struct bfa_wc		rxq_stop_wc;

	/* Rx event handlers */
	void (*rcb_setup_cbfn)(struct bnad *, struct bna_rcb *);
	void (*rcb_destroy_cbfn)(struct bnad *, struct bna_rcb *);
	void (*ccb_setup_cbfn)(struct bnad *, struct bna_ccb *);
	void (*ccb_destroy_cbfn)(struct bnad *, struct bna_ccb *);
	void (*rx_cleanup_cbfn)(struct bnad *, struct bna_ccb *);
	void (*rx_post_cbfn)(struct bnad *, struct bna_rcb *);

	/* callback for bna_rx_disable(), bna_rx_stop() */
	void (*stop_cbfn)(void *arg, struct bna_rx *rx,
				enum bna_cb_status status);
	void			*stop_cbarg;

	struct bna *bna;
	void			*priv; /* bnad's cookie */
};

struct bna_rx_event_cbfn {
	/* Optional */
	void (*rcb_setup_cbfn)(struct bnad *, struct bna_rcb *);
	void (*rcb_destroy_cbfn)(struct bnad *, struct bna_rcb *);
	void (*ccb_setup_cbfn)(struct bnad *, struct bna_ccb *);
	void (*ccb_destroy_cbfn)(struct bnad *, struct bna_ccb *);
	/* Mandatory */
	void (*rx_cleanup_cbfn)(struct bnad *, struct bna_ccb *);
	void (*rx_post_cbfn)(struct bnad *, struct bna_rcb *);
};

/* Rx module - keeps track of free, active rx objects */
struct bna_rx_mod {
	struct bna *bna;		/* back pointer to parent */
	struct bna_rx *rx;		/* BFI_MAX_RXQ entries */
	struct bna_rxp *rxp;		/* BFI_MAX_RXQ entries */
	struct bna_rxq *rxq;		/* BFI_MAX_RXQ entries */

	struct list_head			rx_free_q;
	struct list_head			rx_active_q;
	int			rx_free_count;

	struct list_head			rxp_free_q;
	int			rxp_free_count;

	struct list_head			rxq_free_q;
	int			rxq_free_count;

	enum bna_rx_mod_flags flags;

	/* callback for bna_rx_mod_stop() */
	void (*stop_cbfn)(struct bna_port *port,
				enum bna_cb_status status);

	struct bfa_wc		rx_stop_wc;
	u32		dim_vector[BNA_LOAD_T_MAX][BNA_BIAS_T_MAX];
	u32		rxf_bmap[2];
};

/**
 *
 * CAM
 *
 */

struct bna_ucam_mod {
	struct bna_mac *ucmac;		/* BFI_MAX_UCMAC entries */
	struct list_head			free_q;

	struct bna *bna;
};

struct bna_mcam_mod {
	struct bna_mac *mcmac;		/* BFI_MAX_MCMAC entries */
	struct list_head			free_q;

	struct bna *bna;
};

/**
 *
 * Statistics
 *
 */

struct bna_tx_stats {
	int			tx_state;
	int			tx_flags;
	int			num_txqs;
	u32		txq_bmap[2];
	int			txf_id;
};

struct bna_rx_stats {
	int			rx_state;
	int			rx_flags;
	int			num_rxps;
	int			num_rxqs;
	u32		rxq_bmap[2];
	u32		cq_bmap[2];
	int			rxf_id;
	int			rxf_state;
	int			rxf_oper_state;
	int			num_active_ucast;
	int			num_active_mcast;
	int			rxmode_active;
	int			vlan_filter_status;
	u32		vlan_filter_table[(BFI_MAX_VLAN + 1) / 32];
	int			rss_status;
	int			hds_status;
};

struct bna_sw_stats {
	int			device_state;
	int			port_state;
	int			port_flags;
	int			llport_state;
	int			priority;
	int			num_active_tx;
	int			num_active_rx;
	struct bna_tx_stats tx_stats[BFI_MAX_TXQ];
	struct bna_rx_stats rx_stats[BFI_MAX_RXQ];
};

struct bna_stats {
	u32		txf_bmap[2];
	u32		rxf_bmap[2];
	struct bfi_ll_stats	*hw_stats;
	struct bna_sw_stats *sw_stats;
};

/**
 *
 * BNA
 *
 */

struct bna {
	struct bfa_pcidev pcidev;

	int			port_num;

	struct bna_chip_regs regs;

	struct bna_dma_addr hw_stats_dma;
	struct bna_stats stats;

	struct bna_device device;
	struct bfa_cee cee;

	struct bna_mbox_mod mbox_mod;

	struct bna_port port;

	struct bna_tx_mod tx_mod;

	struct bna_rx_mod rx_mod;

	struct bna_ib_mod ib_mod;

	struct bna_ucam_mod ucam_mod;
	struct bna_mcam_mod mcam_mod;

	struct bna_rit_mod rit_mod;

	int			rxf_promisc_id;

	struct bna_mbox_qe mbox_qe;

	struct bnad *bnad;
};

#endif	/* __BNA_TYPES_H__ */
