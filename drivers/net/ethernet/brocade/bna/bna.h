/*
 * Linux network driver for QLogic BR-series Converged Network Adapter.
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
 * Copyright (c) 2005-2014 Brocade Communications Systems, Inc.
 * Copyright (c) 2014-2015 QLogic Corporation
 * All rights reserved
 * www.qlogic.com
 */
#ifndef __BNA_H__
#define __BNA_H__

#include "bfa_defs.h"
#include "bfa_ioc.h"
#include "bfi_enet.h"
#include "bna_types.h"

extern const u32 bna_napi_dim_vector[][BNA_BIAS_T_MAX];

/*  Macros and constants  */

#define bna_is_small_rxq(_id) ((_id) & 0x1)

/*
 * input : _addr-> os dma addr in host endian format,
 * output : _bna_dma_addr-> pointer to hw dma addr
 */
#define BNA_SET_DMA_ADDR(_addr, _bna_dma_addr)				\
do {									\
	u64 tmp_addr =						\
	cpu_to_be64((u64)(_addr));				\
	(_bna_dma_addr)->msb = ((struct bna_dma_addr *)&tmp_addr)->msb; \
	(_bna_dma_addr)->lsb = ((struct bna_dma_addr *)&tmp_addr)->lsb; \
} while (0)

/*
 * input : _bna_dma_addr-> pointer to hw dma addr
 * output : _addr-> os dma addr in host endian format
 */
#define BNA_GET_DMA_ADDR(_bna_dma_addr, _addr)			\
do {								\
	(_addr) = ((((u64)ntohl((_bna_dma_addr)->msb))) << 32)		\
	| ((ntohl((_bna_dma_addr)->lsb) & 0xffffffff));	\
} while (0)

#define BNA_TXQ_WI_NEEDED(_vectors)	(((_vectors) + 3) >> 2)

#define BNA_QE_INDX_ADD(_qe_idx, _qe_num, _q_depth)			\
	((_qe_idx) = ((_qe_idx) + (_qe_num)) & ((_q_depth) - 1))

#define BNA_QE_INDX_INC(_idx, _q_depth) BNA_QE_INDX_ADD(_idx, 1, _q_depth)

#define BNA_Q_INDEX_CHANGE(_old_idx, _updated_idx, _q_depth)		\
	(((_updated_idx) - (_old_idx)) & ((_q_depth) - 1))

#define BNA_QE_FREE_CNT(_q_ptr, _q_depth)				\
	(((_q_ptr)->consumer_index - (_q_ptr)->producer_index - 1) &	\
	 ((_q_depth) - 1))
#define BNA_QE_IN_USE_CNT(_q_ptr, _q_depth)				\
	((((_q_ptr)->producer_index - (_q_ptr)->consumer_index)) &	\
	 (_q_depth - 1))

#define BNA_LARGE_PKT_SIZE		1000

#define BNA_UPDATE_PKT_CNT(_pkt, _len)					\
do {									\
	if ((_len) > BNA_LARGE_PKT_SIZE) {				\
		(_pkt)->large_pkt_cnt++;				\
	} else {							\
		(_pkt)->small_pkt_cnt++;				\
	}								\
} while (0)

#define	call_rxf_stop_cbfn(rxf)						\
do {									\
	if ((rxf)->stop_cbfn) {						\
		void (*cbfn)(struct bna_rx *);			\
		struct bna_rx *cbarg;					\
		cbfn = (rxf)->stop_cbfn;				\
		cbarg = (rxf)->stop_cbarg;				\
		(rxf)->stop_cbfn = NULL;				\
		(rxf)->stop_cbarg = NULL;				\
		cbfn(cbarg);						\
	}								\
} while (0)

#define	call_rxf_start_cbfn(rxf)					\
do {									\
	if ((rxf)->start_cbfn) {					\
		void (*cbfn)(struct bna_rx *);			\
		struct bna_rx *cbarg;					\
		cbfn = (rxf)->start_cbfn;				\
		cbarg = (rxf)->start_cbarg;				\
		(rxf)->start_cbfn = NULL;				\
		(rxf)->start_cbarg = NULL;				\
		cbfn(cbarg);						\
	}								\
} while (0)

#define	call_rxf_cam_fltr_cbfn(rxf)					\
do {									\
	if ((rxf)->cam_fltr_cbfn) {					\
		void (*cbfn)(struct bnad *, struct bna_rx *);	\
		struct bnad *cbarg;					\
		cbfn = (rxf)->cam_fltr_cbfn;				\
		cbarg = (rxf)->cam_fltr_cbarg;				\
		(rxf)->cam_fltr_cbfn = NULL;				\
		(rxf)->cam_fltr_cbarg = NULL;				\
		cbfn(cbarg, rxf->rx);					\
	}								\
} while (0)

#define is_xxx_enable(mode, bitmask, xxx) ((bitmask & xxx) && (mode & xxx))

#define is_xxx_disable(mode, bitmask, xxx) ((bitmask & xxx) && !(mode & xxx))

#define xxx_enable(mode, bitmask, xxx)					\
do {									\
	bitmask |= xxx;							\
	mode |= xxx;							\
} while (0)

#define xxx_disable(mode, bitmask, xxx)					\
do {									\
	bitmask |= xxx;							\
	mode &= ~xxx;							\
} while (0)

#define xxx_inactive(mode, bitmask, xxx)				\
do {									\
	bitmask &= ~xxx;						\
	mode &= ~xxx;							\
} while (0)

#define is_promisc_enable(mode, bitmask)				\
	is_xxx_enable(mode, bitmask, BNA_RXMODE_PROMISC)

#define is_promisc_disable(mode, bitmask)				\
	is_xxx_disable(mode, bitmask, BNA_RXMODE_PROMISC)

#define promisc_enable(mode, bitmask)					\
	xxx_enable(mode, bitmask, BNA_RXMODE_PROMISC)

#define promisc_disable(mode, bitmask)					\
	xxx_disable(mode, bitmask, BNA_RXMODE_PROMISC)

#define promisc_inactive(mode, bitmask)					\
	xxx_inactive(mode, bitmask, BNA_RXMODE_PROMISC)

#define is_default_enable(mode, bitmask)				\
	is_xxx_enable(mode, bitmask, BNA_RXMODE_DEFAULT)

#define is_default_disable(mode, bitmask)				\
	is_xxx_disable(mode, bitmask, BNA_RXMODE_DEFAULT)

#define default_enable(mode, bitmask)					\
	xxx_enable(mode, bitmask, BNA_RXMODE_DEFAULT)

#define default_disable(mode, bitmask)					\
	xxx_disable(mode, bitmask, BNA_RXMODE_DEFAULT)

#define default_inactive(mode, bitmask)					\
	xxx_inactive(mode, bitmask, BNA_RXMODE_DEFAULT)

#define is_allmulti_enable(mode, bitmask)				\
	is_xxx_enable(mode, bitmask, BNA_RXMODE_ALLMULTI)

#define is_allmulti_disable(mode, bitmask)				\
	is_xxx_disable(mode, bitmask, BNA_RXMODE_ALLMULTI)

#define allmulti_enable(mode, bitmask)					\
	xxx_enable(mode, bitmask, BNA_RXMODE_ALLMULTI)

#define allmulti_disable(mode, bitmask)					\
	xxx_disable(mode, bitmask, BNA_RXMODE_ALLMULTI)

#define allmulti_inactive(mode, bitmask)				\
	xxx_inactive(mode, bitmask, BNA_RXMODE_ALLMULTI)

#define	GET_RXQS(rxp, q0, q1)	do {					\
	switch ((rxp)->type) {						\
	case BNA_RXP_SINGLE:						\
		(q0) = rxp->rxq.single.only;				\
		(q1) = NULL;						\
		break;							\
	case BNA_RXP_SLR:						\
		(q0) = rxp->rxq.slr.large;				\
		(q1) = rxp->rxq.slr.small;				\
		break;							\
	case BNA_RXP_HDS:						\
		(q0) = rxp->rxq.hds.data;				\
		(q1) = rxp->rxq.hds.hdr;				\
		break;							\
	}								\
} while (0)

#define bna_tx_rid_mask(_bna) ((_bna)->tx_mod.rid_mask)

#define bna_rx_rid_mask(_bna) ((_bna)->rx_mod.rid_mask)

#define bna_tx_from_rid(_bna, _rid, _tx)				\
do {									\
	struct bna_tx_mod *__tx_mod = &(_bna)->tx_mod;			\
	struct bna_tx *__tx;						\
	_tx = NULL;							\
	list_for_each_entry(__tx, &__tx_mod->tx_active_q, qe) {		\
		if (__tx->rid == (_rid)) {				\
			(_tx) = __tx;					\
			break;						\
		}							\
	}								\
} while (0)

#define bna_rx_from_rid(_bna, _rid, _rx)				\
do {									\
	struct bna_rx_mod *__rx_mod = &(_bna)->rx_mod;			\
	struct bna_rx *__rx;						\
	_rx = NULL;							\
	list_for_each_entry(__rx, &__rx_mod->rx_active_q, qe) {		\
		if (__rx->rid == (_rid)) {				\
			(_rx) = __rx;					\
			break;						\
		}							\
	}								\
} while (0)

#define bna_mcam_mod_free_q(_bna) (&(_bna)->mcam_mod.free_q)

#define bna_mcam_mod_del_q(_bna) (&(_bna)->mcam_mod.del_q)

#define bna_ucam_mod_free_q(_bna) (&(_bna)->ucam_mod.free_q)

#define bna_ucam_mod_del_q(_bna) (&(_bna)->ucam_mod.del_q)

/*  Inline functions  */

static inline struct bna_mac *bna_mac_find(struct list_head *q, const u8 *addr)
{
	struct bna_mac *mac;

	list_for_each_entry(mac, q, qe)
		if (ether_addr_equal(mac->addr, addr))
			return mac;
	return NULL;
}

#define bna_attr(_bna) (&(_bna)->ioceth.attr)

/* Function prototypes */

/* BNA */

/* FW response handlers */
void bna_bfi_stats_clr_rsp(struct bna *bna, struct bfi_msgq_mhdr *msghdr);

/* APIs for BNAD */
void bna_res_req(struct bna_res_info *res_info);
void bna_mod_res_req(struct bna *bna, struct bna_res_info *res_info);
void bna_init(struct bna *bna, struct bnad *bnad,
			struct bfa_pcidev *pcidev,
			struct bna_res_info *res_info);
void bna_mod_init(struct bna *bna, struct bna_res_info *res_info);
void bna_uninit(struct bna *bna);
int bna_num_txq_set(struct bna *bna, int num_txq);
int bna_num_rxp_set(struct bna *bna, int num_rxp);
void bna_hw_stats_get(struct bna *bna);

/* APIs for RxF */
struct bna_mac *bna_cam_mod_mac_get(struct list_head *head);
struct bna_mcam_handle *bna_mcam_mod_handle_get(struct bna_mcam_mod *mod);
void bna_mcam_mod_handle_put(struct bna_mcam_mod *mcam_mod,
			  struct bna_mcam_handle *handle);

/* MBOX */

/* API for BNAD */
void bna_mbox_handler(struct bna *bna, u32 intr_status);

/* ETHPORT */

/* Callbacks for RX */
void bna_ethport_cb_rx_started(struct bna_ethport *ethport);
void bna_ethport_cb_rx_stopped(struct bna_ethport *ethport);

/* TX MODULE AND TX */

/* FW response handelrs */
void bna_bfi_tx_enet_start_rsp(struct bna_tx *tx,
			       struct bfi_msgq_mhdr *msghdr);
void bna_bfi_tx_enet_stop_rsp(struct bna_tx *tx,
			      struct bfi_msgq_mhdr *msghdr);
void bna_bfi_bw_update_aen(struct bna_tx_mod *tx_mod);

/* APIs for BNA */
void bna_tx_mod_init(struct bna_tx_mod *tx_mod, struct bna *bna,
		     struct bna_res_info *res_info);
void bna_tx_mod_uninit(struct bna_tx_mod *tx_mod);

/* APIs for ENET */
void bna_tx_mod_start(struct bna_tx_mod *tx_mod, enum bna_tx_type type);
void bna_tx_mod_stop(struct bna_tx_mod *tx_mod, enum bna_tx_type type);
void bna_tx_mod_fail(struct bna_tx_mod *tx_mod);

/* APIs for BNAD */
void bna_tx_res_req(int num_txq, int txq_depth,
		    struct bna_res_info *res_info);
struct bna_tx *bna_tx_create(struct bna *bna, struct bnad *bnad,
			       struct bna_tx_config *tx_cfg,
			       const struct bna_tx_event_cbfn *tx_cbfn,
			       struct bna_res_info *res_info, void *priv);
void bna_tx_destroy(struct bna_tx *tx);
void bna_tx_enable(struct bna_tx *tx);
void bna_tx_disable(struct bna_tx *tx, enum bna_cleanup_type type,
		    void (*cbfn)(void *, struct bna_tx *));
void bna_tx_cleanup_complete(struct bna_tx *tx);
void bna_tx_coalescing_timeo_set(struct bna_tx *tx, int coalescing_timeo);

/* RX MODULE, RX, RXF */

/* FW response handlers */
void bna_bfi_rx_enet_start_rsp(struct bna_rx *rx,
			       struct bfi_msgq_mhdr *msghdr);
void bna_bfi_rx_enet_stop_rsp(struct bna_rx *rx,
			      struct bfi_msgq_mhdr *msghdr);
void bna_bfi_rxf_cfg_rsp(struct bna_rxf *rxf, struct bfi_msgq_mhdr *msghdr);
void bna_bfi_rxf_mcast_add_rsp(struct bna_rxf *rxf,
			       struct bfi_msgq_mhdr *msghdr);
void bna_bfi_rxf_ucast_set_rsp(struct bna_rxf *rxf,
			       struct bfi_msgq_mhdr *msghdr);

/* APIs for BNA */
void bna_rx_mod_init(struct bna_rx_mod *rx_mod, struct bna *bna,
		     struct bna_res_info *res_info);
void bna_rx_mod_uninit(struct bna_rx_mod *rx_mod);

/* APIs for ENET */
void bna_rx_mod_start(struct bna_rx_mod *rx_mod, enum bna_rx_type type);
void bna_rx_mod_stop(struct bna_rx_mod *rx_mod, enum bna_rx_type type);
void bna_rx_mod_fail(struct bna_rx_mod *rx_mod);

/* APIs for BNAD */
void bna_rx_res_req(struct bna_rx_config *rx_config,
		    struct bna_res_info *res_info);
struct bna_rx *bna_rx_create(struct bna *bna, struct bnad *bnad,
			       struct bna_rx_config *rx_cfg,
			       const struct bna_rx_event_cbfn *rx_cbfn,
			       struct bna_res_info *res_info, void *priv);
void bna_rx_destroy(struct bna_rx *rx);
void bna_rx_enable(struct bna_rx *rx);
void bna_rx_disable(struct bna_rx *rx, enum bna_cleanup_type type,
		    void (*cbfn)(void *, struct bna_rx *));
void bna_rx_cleanup_complete(struct bna_rx *rx);
void bna_rx_coalescing_timeo_set(struct bna_rx *rx, int coalescing_timeo);
void bna_rx_dim_reconfig(struct bna *bna, const u32 vector[][BNA_BIAS_T_MAX]);
void bna_rx_dim_update(struct bna_ccb *ccb);
enum bna_cb_status bna_rx_ucast_set(struct bna_rx *rx, const u8 *ucmac);
enum bna_cb_status bna_rx_ucast_listset(struct bna_rx *rx, int count,
					const u8 *uclist);
enum bna_cb_status bna_rx_mcast_add(struct bna_rx *rx, const u8 *mcmac,
				    void (*cbfn)(struct bnad *,
						 struct bna_rx *));
enum bna_cb_status bna_rx_mcast_listset(struct bna_rx *rx, int count,
					const u8 *mcmac);
void
bna_rx_mcast_delall(struct bna_rx *rx);
enum bna_cb_status
bna_rx_mode_set(struct bna_rx *rx, enum bna_rxmode rxmode,
		enum bna_rxmode bitmask);
void bna_rx_vlan_add(struct bna_rx *rx, int vlan_id);
void bna_rx_vlan_del(struct bna_rx *rx, int vlan_id);
void bna_rx_vlanfilter_enable(struct bna_rx *rx);
void bna_rx_vlan_strip_enable(struct bna_rx *rx);
void bna_rx_vlan_strip_disable(struct bna_rx *rx);
/* ENET */

/* API for RX */
int bna_enet_mtu_get(struct bna_enet *enet);

/* Callbacks for TX, RX */
void bna_enet_cb_tx_stopped(struct bna_enet *enet);
void bna_enet_cb_rx_stopped(struct bna_enet *enet);

/* API for BNAD */
void bna_enet_enable(struct bna_enet *enet);
void bna_enet_disable(struct bna_enet *enet, enum bna_cleanup_type type,
		      void (*cbfn)(void *));
void bna_enet_pause_config(struct bna_enet *enet,
			   struct bna_pause_config *pause_config);
void bna_enet_mtu_set(struct bna_enet *enet, int mtu,
		      void (*cbfn)(struct bnad *));
void bna_enet_perm_mac_get(struct bna_enet *enet, u8 *mac);

/* IOCETH */

/* APIs for BNAD */
void bna_ioceth_enable(struct bna_ioceth *ioceth);
void bna_ioceth_disable(struct bna_ioceth *ioceth,
			enum bna_cleanup_type type);

/* BNAD */

/* Callbacks for ENET */
void bnad_cb_ethport_link_status(struct bnad *bnad,
			      enum bna_link_status status);

/* Callbacks for IOCETH */
void bnad_cb_ioceth_ready(struct bnad *bnad);
void bnad_cb_ioceth_failed(struct bnad *bnad);
void bnad_cb_ioceth_disabled(struct bnad *bnad);
void bnad_cb_mbox_intr_enable(struct bnad *bnad);
void bnad_cb_mbox_intr_disable(struct bnad *bnad);

/* Callbacks for BNA */
void bnad_cb_stats_get(struct bnad *bnad, enum bna_cb_status status,
		       struct bna_stats *stats);

#endif  /* __BNA_H__ */
