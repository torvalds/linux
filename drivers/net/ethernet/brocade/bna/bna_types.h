/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Linux network driver for QLogic BR-series Converged Network Adapter.
 */
/*
 * Copyright (c) 2005-2014 Brocade Communications Systems, Inc.
 * Copyright (c) 2014-2015 QLogic Corporation
 * All rights reserved
 * www.qlogic.com
 */
#ifndef __BNA_TYPES_H__
#define __BNA_TYPES_H__

#include "cna.h"
#include "bna_hw_defs.h"
#include "bfa_cee.h"
#include "bfa_msgq.h"

/* Forward declarations */

struct bna_mcam_handle;
struct bna_txq;
struct bna_tx;
struct bna_rxq;
struct bna_cq;
struct bna_rx;
struct bna_rxf;
struct bna_enet;
struct bna;
struct bnad;

/* Enums, primitive data types */

enum bna_status {
	BNA_STATUS_T_DISABLED	= 0,
	BNA_STATUS_T_ENABLED	= 1
};

enum bna_cleanup_type {
	BNA_HARD_CLEANUP	= 0,
	BNA_SOFT_CLEANUP	= 1
};

enum bna_cb_status {
	BNA_CB_SUCCESS		= 0,
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
	BNA_MEM_T_KVA		= 1,
	BNA_MEM_T_DMA		= 2
};

enum bna_intr_type {
	BNA_INTR_T_INTX		= 1,
	BNA_INTR_T_MSIX		= 2
};

enum bna_res_req_type {
	BNA_RES_MEM_T_COM		= 0,
	BNA_RES_MEM_T_ATTR		= 1,
	BNA_RES_MEM_T_FWTRC		= 2,
	BNA_RES_MEM_T_STATS		= 3,
	BNA_RES_T_MAX
};

enum bna_mod_res_req_type {
	BNA_MOD_RES_MEM_T_TX_ARRAY	= 0,
	BNA_MOD_RES_MEM_T_TXQ_ARRAY	= 1,
	BNA_MOD_RES_MEM_T_RX_ARRAY	= 2,
	BNA_MOD_RES_MEM_T_RXP_ARRAY	= 3,
	BNA_MOD_RES_MEM_T_RXQ_ARRAY	= 4,
	BNA_MOD_RES_MEM_T_UCMAC_ARRAY	= 5,
	BNA_MOD_RES_MEM_T_MCMAC_ARRAY	= 6,
	BNA_MOD_RES_MEM_T_MCHANDLE_ARRAY = 7,
	BNA_MOD_RES_T_MAX
};

enum bna_tx_res_req_type {
	BNA_TX_RES_MEM_T_TCB	= 0,
	BNA_TX_RES_MEM_T_UNMAPQ	= 1,
	BNA_TX_RES_MEM_T_QPT	= 2,
	BNA_TX_RES_MEM_T_SWQPT	= 3,
	BNA_TX_RES_MEM_T_PAGE	= 4,
	BNA_TX_RES_MEM_T_IBIDX	= 5,
	BNA_TX_RES_INTR_T_TXCMPL = 6,
	BNA_TX_RES_T_MAX,
};

enum bna_rx_mem_type {
	BNA_RX_RES_MEM_T_CCB		= 0,	/* CQ context */
	BNA_RX_RES_MEM_T_RCB		= 1,	/* CQ context */
	BNA_RX_RES_MEM_T_UNMAPHQ	= 2,
	BNA_RX_RES_MEM_T_UNMAPDQ	= 3,
	BNA_RX_RES_MEM_T_CQPT		= 4,
	BNA_RX_RES_MEM_T_CSWQPT		= 5,
	BNA_RX_RES_MEM_T_CQPT_PAGE	= 6,
	BNA_RX_RES_MEM_T_HQPT		= 7,
	BNA_RX_RES_MEM_T_DQPT		= 8,
	BNA_RX_RES_MEM_T_HSWQPT		= 9,
	BNA_RX_RES_MEM_T_DSWQPT		= 10,
	BNA_RX_RES_MEM_T_DPAGE		= 11,
	BNA_RX_RES_MEM_T_HPAGE		= 12,
	BNA_RX_RES_MEM_T_IBIDX		= 13,
	BNA_RX_RES_MEM_T_RIT		= 14,
	BNA_RX_RES_T_INTR		= 15,
	BNA_RX_RES_T_MAX		= 16
};

enum bna_tx_type {
	BNA_TX_T_REGULAR	= 0,
	BNA_TX_T_LOOPBACK	= 1,
};

enum bna_tx_flags {
	BNA_TX_F_ENET_STARTED	= 1,
	BNA_TX_F_ENABLED	= 2,
	BNA_TX_F_BW_UPDATED	= 8,
};

enum bna_tx_mod_flags {
	BNA_TX_MOD_F_ENET_STARTED	= 1,
	BNA_TX_MOD_F_ENET_LOOPBACK	= 2,
};

enum bna_rx_type {
	BNA_RX_T_REGULAR	= 0,
	BNA_RX_T_LOOPBACK	= 1,
};

enum bna_rxp_type {
	BNA_RXP_SINGLE		= 1,
	BNA_RXP_SLR		= 2,
	BNA_RXP_HDS		= 3
};

enum bna_rxmode {
	BNA_RXMODE_PROMISC	= 1,
	BNA_RXMODE_DEFAULT	= 2,
	BNA_RXMODE_ALLMULTI	= 4
};

enum bna_rx_event {
	RX_E_START			= 1,
	RX_E_STOP			= 2,
	RX_E_FAIL			= 3,
	RX_E_STARTED			= 4,
	RX_E_STOPPED			= 5,
	RX_E_RXF_STARTED		= 6,
	RX_E_RXF_STOPPED		= 7,
	RX_E_CLEANUP_DONE		= 8,
};

enum bna_rx_flags {
	BNA_RX_F_ENET_STARTED	= 1,
	BNA_RX_F_ENABLED	= 2,
};

enum bna_rx_mod_flags {
	BNA_RX_MOD_F_ENET_STARTED	= 1,
	BNA_RX_MOD_F_ENET_LOOPBACK	= 2,
};

enum bna_rxf_event {
	RXF_E_START			= 1,
	RXF_E_STOP			= 2,
	RXF_E_FAIL			= 3,
	RXF_E_CONFIG			= 4,
	RXF_E_FW_RESP			= 7,
};

enum bna_enet_type {
	BNA_ENET_T_REGULAR		= 0,
	BNA_ENET_T_LOOPBACK_INTERNAL	= 1,
	BNA_ENET_T_LOOPBACK_EXTERNAL	= 2,
};

enum bna_link_status {
	BNA_LINK_DOWN		= 0,
	BNA_LINK_UP		= 1,
	BNA_CEE_UP		= 2
};

enum bna_ethport_flags {
	BNA_ETHPORT_F_ADMIN_UP		= 1,
	BNA_ETHPORT_F_PORT_ENABLED	= 2,
	BNA_ETHPORT_F_RX_STARTED	= 4,
};

enum bna_enet_flags {
	BNA_ENET_F_IOCETH_READY		= 1,
	BNA_ENET_F_ENABLED		= 2,
	BNA_ENET_F_PAUSE_CHANGED	= 4,
	BNA_ENET_F_MTU_CHANGED		= 8
};

enum bna_rss_flags {
	BNA_RSS_F_RIT_PENDING		= 1,
	BNA_RSS_F_CFG_PENDING		= 2,
	BNA_RSS_F_STATUS_PENDING	= 4,
};

enum bna_mod_flags {
	BNA_MOD_F_INIT_DONE		= 1,
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

#define BNA_MAX_NAME_SIZE	64
struct bna_ident {
	int			id;
	char			name[BNA_MAX_NAME_SIZE];
};

struct bna_mac {
	/* This should be the first one */
	struct list_head			qe;
	u8			addr[ETH_ALEN];
	struct bna_mcam_handle *handle;
};

struct bna_mem_descr {
	u32		len;
	void		*kva;
	struct bna_dma_addr dma;
};

struct bna_mem_info {
	enum bna_mem_type mem_type;
	u32		len;
	u32		num;
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

struct bna_attr {
	bool			fw_query_complete;
	int			num_txq;
	int			num_rxp;
	int			num_ucmac;
	int			num_mcmac;
	int			max_rit_size;
};

/* IOCEth */

enum bna_ioceth_event;

struct bna_ioceth {
	void (*fsm)(struct bna_ioceth *s, enum bna_ioceth_event e);
	struct bfa_ioc ioc;

	struct bna_attr attr;
	struct bfa_msgq_cmd_entry msgq_cmd;
	struct bfi_enet_attr_req attr_req;

	void (*stop_cbfn)(struct bnad *bnad);
	struct bnad *stop_cbarg;

	struct bna *bna;
};

/* Enet */

/* Pause configuration */
struct bna_pause_config {
	enum bna_status tx_pause;
	enum bna_status rx_pause;
};

enum bna_enet_event;

struct bna_enet {
	void (*fsm)(struct bna_enet *s, enum bna_enet_event e);
	enum bna_enet_flags flags;

	enum bna_enet_type type;

	struct bna_pause_config pause_config;
	int			mtu;

	/* Callback for bna_enet_disable(), enet_stop() */
	void (*stop_cbfn)(void *);
	void			*stop_cbarg;

	/* Callback for bna_enet_mtu_set() */
	void (*mtu_cbfn)(struct bnad *);

	struct bfa_wc		chld_stop_wc;

	struct bfa_msgq_cmd_entry msgq_cmd;
	struct bfi_enet_set_pause_req pause_req;

	struct bna *bna;
};

/* Ethport */

enum bna_ethport_event;

struct bna_ethport {
	void (*fsm)(struct bna_ethport *s, enum bna_ethport_event e);
	enum bna_ethport_flags flags;

	enum bna_link_status link_status;

	int			rx_started_count;

	void (*stop_cbfn)(struct bna_enet *);

	void (*adminup_cbfn)(struct bnad *, enum bna_cb_status);

	void (*link_cbfn)(struct bnad *, enum bna_link_status);

	struct bfa_msgq_cmd_entry msgq_cmd;
	union {
		struct bfi_enet_enable_req admin_req;
		struct bfi_enet_diag_lb_req lpbk_req;
	} bfi_enet_cmd;

	struct bna *bna;
};

/* Interrupt Block */

/* Doorbell structure */
struct bna_ib_dbell {
	void __iomem   *doorbell_addr;
	u32		doorbell_ack;
};

/* IB structure */
struct bna_ib {
	struct bna_dma_addr ib_seg_host_addr;
	void		*ib_seg_host_addr_kva;

	struct bna_ib_dbell door_bell;

	enum bna_intr_type	intr_type;
	int			intr_vector;

	u8			coalescing_timeo;    /* Unit is 5usec. */

	int			interpkt_count;
	int			interpkt_timeo;
};

/* Tx object */

/* Tx datapath control structure */
#define BNA_Q_NAME_SIZE		16
struct bna_tcb {
	/* Fast path */
	void			**sw_qpt;
	void			*sw_q;
	void			*unmap_q;
	u32		producer_index;
	u32		consumer_index;
	volatile u32	*hw_consumer_index;
	u32		q_depth;
	void __iomem   *q_dbell;
	struct bna_ib_dbell *i_dbell;
	/* Control path */
	struct bna_txq *txq;
	struct bnad *bnad;
	void			*priv; /* BNAD's cookie */
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

	u8			priority;

	struct bna_qpt qpt;
	struct bna_tcb *tcb;
	struct bna_ib ib;

	struct bna_tx *tx;

	int			hw_id;

	u64		tx_packets;
	u64		tx_bytes;
};

/* Tx object */

enum bna_tx_event;

struct bna_tx {
	/* This should be the first one */
	struct list_head			qe;
	int			rid;
	int			hw_id;

	void (*fsm)(struct bna_tx *s, enum bna_tx_event e);
	enum bna_tx_flags flags;

	enum bna_tx_type type;
	int			num_txq;

	struct list_head			txq_q;
	u16			txf_vlan_id;

	/* Tx event handlers */
	void (*tcb_setup_cbfn)(struct bnad *, struct bna_tcb *);
	void (*tcb_destroy_cbfn)(struct bnad *, struct bna_tcb *);
	void (*tx_stall_cbfn)(struct bnad *, struct bna_tx *);
	void (*tx_resume_cbfn)(struct bnad *, struct bna_tx *);
	void (*tx_cleanup_cbfn)(struct bnad *, struct bna_tx *);

	/* callback for bna_tx_disable(), bna_tx_stop() */
	void (*stop_cbfn)(void *arg, struct bna_tx *tx);
	void			*stop_cbarg;

	struct bfa_msgq_cmd_entry msgq_cmd;
	union {
		struct bfi_enet_tx_cfg_req	cfg_req;
		struct bfi_enet_req		req;
		struct bfi_enet_tx_cfg_rsp	cfg_rsp;
	} bfi_enet_cmd;

	struct bna *bna;
	void			*priv;	/* bnad's cookie */
};

/* Tx object configuration used during creation */
struct bna_tx_config {
	int			num_txq;
	int			txq_depth;
	int			coalescing_timeo;
	enum bna_tx_type tx_type;
};

struct bna_tx_event_cbfn {
	/* Optional */
	void (*tcb_setup_cbfn)(struct bnad *, struct bna_tcb *);
	void (*tcb_destroy_cbfn)(struct bnad *, struct bna_tcb *);
	/* Mandatory */
	void (*tx_stall_cbfn)(struct bnad *, struct bna_tx *);
	void (*tx_resume_cbfn)(struct bnad *, struct bna_tx *);
	void (*tx_cleanup_cbfn)(struct bnad *, struct bna_tx *);
};

/* Tx module - keeps track of free, active tx objects */
struct bna_tx_mod {
	struct bna_tx *tx;		/* BFI_MAX_TXQ entries */
	struct bna_txq *txq;		/* BFI_MAX_TXQ entries */

	struct list_head			tx_free_q;
	struct list_head			tx_active_q;

	struct list_head			txq_free_q;

	/* callback for bna_tx_mod_stop() */
	void (*stop_cbfn)(struct bna_enet *enet);

	struct bfa_wc		tx_stop_wc;

	enum bna_tx_mod_flags flags;

	u8			prio_map;
	int			default_prio;
	int			iscsi_over_cee;
	int			iscsi_prio;
	int			prio_reconfigured;

	u32			rid_mask;

	struct bna *bna;
};

/* Rx object */

/* Rx datapath control structure */
struct bna_rcb {
	/* Fast path */
	void			**sw_qpt;
	void			*sw_q;
	void			*unmap_q;
	u32		producer_index;
	u32		consumer_index;
	u32		q_depth;
	void __iomem   *q_dbell;
	/* Control path */
	struct bna_rxq *rxq;
	struct bna_ccb *ccb;
	struct bnad *bnad;
	void			*priv; /* BNAD's cookie */
	unsigned long		flags;
	int			id;
};

/* RxQ structure - QPT, configuration */
struct bna_rxq {
	struct list_head			qe;

	int			buffer_size;
	int			q_depth;
	u32			num_vecs;
	enum bna_status		multi_buffer;

	struct bna_qpt qpt;
	struct bna_rcb *rcb;

	struct bna_rxp *rxp;
	struct bna_rx *rx;

	int			hw_id;

	u64		rx_packets;
	u64		rx_bytes;
	u64		rx_packets_with_error;
	u64		rxbuf_alloc_failed;
	u64		rxbuf_map_failed;
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
	void			*sw_q;
	u32		producer_index;
	volatile u32	*hw_producer_index;
	u32		q_depth;
	struct bna_ib_dbell *i_dbell;
	struct bna_rcb *rcb[2];
	void			*ctrl; /* For bnad */
	struct bna_pkt_rate pkt_rate;
	u32			pkts_una;
	u32			bytes_per_intr;

	/* Control path */
	struct bna_cq *cq;
	struct bnad *bnad;
	void			*priv; /* BNAD's cookie */
	enum bna_intr_type intr_type;
	int			intr_vector;
	u8			rx_coalescing_timeo; /* For NAPI */
	int			id;
	char			name[BNA_Q_NAME_SIZE];
};

/* CQ QPT, configuration  */
struct bna_cq {
	struct bna_qpt qpt;
	struct bna_ccb *ccb;

	struct bna_ib ib;

	struct bna_rx *rx;
};

struct bna_rss_config {
	enum bfi_enet_rss_type	hash_type;
	u8			hash_mask;
	u32		toeplitz_hash_key[BFI_ENET_RSS_KEY_LEN];
};

struct bna_hds_config {
	enum bfi_enet_hds_type	hdr_type;
	int			forced_offset;
};

/* Rx object configuration used during creation */
struct bna_rx_config {
	enum bna_rx_type rx_type;
	int			num_paths;
	enum bna_rxp_type rxp_type;
	int			coalescing_timeo;
	/*
	 * Small/Large (or Header/Data) buffer size to be configured
	 * for SLR and HDS queue type.
	 */
	u32			frame_size;

	/* header or small queue */
	u32			q1_depth;
	u32			q1_buf_size;

	/* data or large queue */
	u32			q0_depth;
	u32			q0_buf_size;
	u32			q0_num_vecs;
	enum bna_status		q0_multi_buf;

	enum bna_status rss_status;
	struct bna_rss_config rss_config;

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
	int			hw_id;
};

/* RxF structure (hardware Rx Function) */

enum bna_rxf_event;

struct bna_rxf {
	void (*fsm)(struct bna_rxf *s, enum bna_rxf_event e);

	struct bfa_msgq_cmd_entry msgq_cmd;
	union {
		struct bfi_enet_enable_req req;
		struct bfi_enet_rss_cfg_req rss_req;
		struct bfi_enet_rit_req rit_req;
		struct bfi_enet_rx_vlan_req vlan_req;
		struct bfi_enet_mcast_add_req mcast_add_req;
		struct bfi_enet_mcast_del_req mcast_del_req;
		struct bfi_enet_ucast_req ucast_req;
	} bfi_enet_cmd;

	/* callback for bna_rxf_start() */
	void (*start_cbfn) (struct bna_rx *rx);
	struct bna_rx *start_cbarg;

	/* callback for bna_rxf_stop() */
	void (*stop_cbfn) (struct bna_rx *rx);
	struct bna_rx *stop_cbarg;

	/**
	 * callback for:
	 *	bna_rxf_ucast_set()
	 *	bna_rxf_{ucast/mcast}_add(),
	 *	bna_rxf_{ucast/mcast}_del(),
	 *	bna_rxf_mode_set()
	 */
	void (*cam_fltr_cbfn)(struct bnad *bnad, struct bna_rx *rx);
	struct bnad *cam_fltr_cbarg;

	/* List of unicast addresses yet to be applied to h/w */
	struct list_head			ucast_pending_add_q;
	struct list_head			ucast_pending_del_q;
	struct bna_mac *ucast_pending_mac;
	int			ucast_pending_set;
	/* ucast addresses applied to the h/w */
	struct list_head			ucast_active_q;
	struct bna_mac ucast_active_mac;
	int			ucast_active_set;

	/* List of multicast addresses yet to be applied to h/w */
	struct list_head			mcast_pending_add_q;
	struct list_head			mcast_pending_del_q;
	/* multicast addresses applied to the h/w */
	struct list_head			mcast_active_q;
	struct list_head			mcast_handle_q;

	/* Rx modes yet to be applied to h/w */
	enum bna_rxmode rxmode_pending;
	enum bna_rxmode rxmode_pending_bitmask;
	/* Rx modes applied to h/w */
	enum bna_rxmode rxmode_active;

	u8			vlan_pending_bitmask;
	enum bna_status vlan_filter_status;
	u32	vlan_filter_table[(BFI_ENET_VLAN_ID_MAX) / 32];
	bool			vlan_strip_pending;
	enum bna_status		vlan_strip_status;

	enum bna_rss_flags	rss_pending;
	enum bna_status		rss_status;
	struct bna_rss_config	rss_cfg;
	u8			*rit;
	int			rit_size;

	struct bna_rx		*rx;
};

/* Rx object */

enum bna_rx_event;

struct bna_rx {
	/* This should be the first one */
	struct list_head			qe;
	int			rid;
	int			hw_id;

	void (*fsm)(struct bna_rx *s, enum bna_rx_event e);

	enum bna_rx_type type;

	int			num_paths;
	struct list_head			rxp_q;

	struct bna_hds_config	hds_cfg;

	struct bna_rxf rxf;

	enum bna_rx_flags rx_flags;

	struct bfa_msgq_cmd_entry msgq_cmd;
	union {
		struct bfi_enet_rx_cfg_req	cfg_req;
		struct bfi_enet_req		req;
		struct bfi_enet_rx_cfg_rsp	cfg_rsp;
	} bfi_enet_cmd;

	/* Rx event handlers */
	void (*rcb_setup_cbfn)(struct bnad *, struct bna_rcb *);
	void (*rcb_destroy_cbfn)(struct bnad *, struct bna_rcb *);
	void (*ccb_setup_cbfn)(struct bnad *, struct bna_ccb *);
	void (*ccb_destroy_cbfn)(struct bnad *, struct bna_ccb *);
	void (*rx_stall_cbfn)(struct bnad *, struct bna_rx *);
	void (*rx_cleanup_cbfn)(struct bnad *, struct bna_rx *);
	void (*rx_post_cbfn)(struct bnad *, struct bna_rx *);

	/* callback for bna_rx_disable(), bna_rx_stop() */
	void (*stop_cbfn)(void *arg, struct bna_rx *rx);
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
	void (*rx_stall_cbfn)(struct bnad *, struct bna_rx *);
	/* Mandatory */
	void (*rx_cleanup_cbfn)(struct bnad *, struct bna_rx *);
	void (*rx_post_cbfn)(struct bnad *, struct bna_rx *);
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
	void (*stop_cbfn)(struct bna_enet *enet);

	struct bfa_wc		rx_stop_wc;
	u32		dim_vector[BNA_LOAD_T_MAX][BNA_BIAS_T_MAX];
	u32		rid_mask;
};

/* CAM */

struct bna_ucam_mod {
	struct bna_mac *ucmac;		/* num_ucmac * 2 entries */
	struct list_head			free_q;
	struct list_head			del_q;

	struct bna *bna;
};

struct bna_mcam_handle {
	/* This should be the first one */
	struct list_head			qe;
	int			handle;
	int			refcnt;
};

struct bna_mcam_mod {
	struct bna_mac *mcmac;		/* num_mcmac * 2 entries */
	struct bna_mcam_handle *mchandle;	/* num_mcmac entries */
	struct list_head			free_q;
	struct list_head			del_q;
	struct list_head			free_handle_q;

	struct bna *bna;
};

/* Statistics */

struct bna_stats {
	struct bna_dma_addr	hw_stats_dma;
	struct bfi_enet_stats	*hw_stats_kva;
	struct bfi_enet_stats	hw_stats;
};

struct bna_stats_mod {
	bool		ioc_ready;
	bool		stats_get_busy;
	bool		stats_clr_busy;
	struct bfa_msgq_cmd_entry stats_get_cmd;
	struct bfa_msgq_cmd_entry stats_clr_cmd;
	struct bfi_enet_stats_req stats_get;
	struct bfi_enet_stats_req stats_clr;
};

/* BNA */

struct bna {
	struct bna_ident ident;
	struct bfa_pcidev pcidev;

	struct bna_reg regs;
	struct bna_bit_defn bits;

	struct bna_stats stats;

	struct bna_ioceth ioceth;
	struct bfa_cee cee;
	struct bfa_flash flash;
	struct bfa_msgq msgq;

	struct bna_ethport ethport;
	struct bna_enet enet;
	struct bna_stats_mod stats_mod;

	struct bna_tx_mod tx_mod;
	struct bna_rx_mod rx_mod;
	struct bna_ucam_mod ucam_mod;
	struct bna_mcam_mod mcam_mod;

	enum bna_mod_flags mod_flags;

	int			default_mode_rid;
	int			promisc_rid;

	struct bnad *bnad;
};
#endif	/* __BNA_TYPES_H__ */
