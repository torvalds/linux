/*
 * Header file describing the internal (inter-module) DHD interfaces.
 *
 * Provides type definitions and function prototypes used to link the
 * DHD OS, bus, and protocol modules.
 *
 * Portions of this code are copyright (c) 2022 Cypress Semiconductor Corporation
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: dhd_proto.h 694343 2017-04-11 19:05:58Z $
 */

#ifndef _dhd_proto_h_
#define _dhd_proto_h_

#include <dhdioctl.h>
#include <wlioctl.h>
#ifdef BCMPCIE
#include <dhd_flowring.h>
#endif // endif

#define DEFAULT_IOCTL_RESP_TIMEOUT	5000
#ifndef IOCTL_RESP_TIMEOUT
#ifdef BCMQT
#define IOCTL_RESP_TIMEOUT  130000 /* In milli second */
#else
/* In milli second default value for Production FW */
#define IOCTL_RESP_TIMEOUT  DEFAULT_IOCTL_RESP_TIMEOUT
#endif /* BCMQT */
#endif /* IOCTL_RESP_TIMEOUT */

#ifdef BCMQT
#define IOCTL_DMAXFER_TIMEOUT  260000 /* In milli second */
#else
/* In milli second default value for Production FW */
#define IOCTL_DMAXFER_TIMEOUT  10000
#endif /* BCMQT */

#ifndef MFG_IOCTL_RESP_TIMEOUT
#define MFG_IOCTL_RESP_TIMEOUT  20000  /* In milli second default value for MFG FW */
#endif /* MFG_IOCTL_RESP_TIMEOUT */

#define DEFAULT_D3_ACK_RESP_TIMEOUT	2000
#ifndef D3_ACK_RESP_TIMEOUT
#define D3_ACK_RESP_TIMEOUT		DEFAULT_D3_ACK_RESP_TIMEOUT
#endif /* D3_ACK_RESP_TIMEOUT */

#define DEFAULT_DHD_BUS_BUSY_TIMEOUT	(IOCTL_RESP_TIMEOUT + 1000)
#ifndef DHD_BUS_BUSY_TIMEOUT
#define DHD_BUS_BUSY_TIMEOUT	DEFAULT_DHD_BUS_BUSY_TIMEOUT
#endif /* DEFAULT_DHD_BUS_BUSY_TIMEOUT */

#define DS_EXIT_TIMEOUT	1000 /* In ms */
#define DS_ENTER_TIMEOUT 1000 /* In ms */

#define IOCTL_DISABLE_TIMEOUT 0

/*
 * Exported from the dhd protocol module (dhd_cdc, dhd_rndis)
 */

/* Linkage, sets prot link and updates hdrlen in pub */
extern int dhd_prot_attach(dhd_pub_t *dhdp);

/* Initilizes the index block for dma'ing indices */
extern int dhd_prot_dma_indx_init(dhd_pub_t *dhdp, uint32 rw_index_sz,
	uint8 type, uint32 length);

/* Unlink, frees allocated protocol memory (including dhd_prot) */
extern void dhd_prot_detach(dhd_pub_t *dhdp);

/* Initialize protocol: sync w/dongle state.
 * Sets dongle media info (iswl, drv_version, mac address).
 */
extern int dhd_sync_with_dongle(dhd_pub_t *dhdp);

/* Protocol initialization needed for IOCTL/IOVAR path */
extern int dhd_prot_init(dhd_pub_t *dhd);

/* Stop protocol: sync w/dongle state. */
extern void dhd_prot_stop(dhd_pub_t *dhdp);

/* Add any protocol-specific data header.
 * Caller must reserve prot_hdrlen prepend space.
 */
extern void dhd_prot_hdrpush(dhd_pub_t *, int ifidx, void *txp);
extern uint dhd_prot_hdrlen(dhd_pub_t *, void *txp);

/* Remove any protocol-specific data header. */
extern int dhd_prot_hdrpull(dhd_pub_t *, int *ifidx, void *rxp, uchar *buf, uint *len);

/* Use protocol to issue ioctl to dongle */
extern int dhd_prot_ioctl(dhd_pub_t *dhd, int ifidx, wl_ioctl_t * ioc, void * buf, int len);

/* Handles a protocol control response asynchronously */
extern int dhd_prot_ctl_complete(dhd_pub_t *dhd);

/* Check for and handle local prot-specific iovar commands */
extern int dhd_prot_iovar_op(dhd_pub_t *dhdp, const char *name,
                             void *params, int plen, void *arg, int len, bool set);

/* Add prot dump output to a buffer */
extern void dhd_prot_dump(dhd_pub_t *dhdp, struct bcmstrbuf *strbuf);

/* Dump extended trap data */
extern int dhd_prot_dump_extended_trap(dhd_pub_t *dhdp, struct bcmstrbuf *b, bool raw);

/* Update local copy of dongle statistics */
extern void dhd_prot_dstats(dhd_pub_t *dhdp);

extern int dhd_ioctl(dhd_pub_t * dhd_pub, dhd_ioctl_t *ioc, void * buf, uint buflen);

extern int dhd_preinit_ioctls(dhd_pub_t *dhd);

extern int dhd_process_pkt_reorder_info(dhd_pub_t *dhd, uchar *reorder_info_buf,
	uint reorder_info_len, void **pkt, uint32 *free_buf_count);

#ifdef BCMPCIE
extern bool dhd_prot_process_msgbuf_txcpl(dhd_pub_t *dhd, uint bound, int ringtype);
extern bool dhd_prot_process_msgbuf_rxcpl(dhd_pub_t *dhd, uint bound, int ringtype);
extern bool dhd_prot_process_msgbuf_infocpl(dhd_pub_t *dhd, uint bound);
extern int dhd_prot_process_ctrlbuf(dhd_pub_t * dhd);
extern int dhd_prot_process_trapbuf(dhd_pub_t * dhd);
extern bool dhd_prot_dtohsplit(dhd_pub_t * dhd);
extern int dhd_post_dummy_msg(dhd_pub_t *dhd);
extern int dhdmsgbuf_lpbk_req(dhd_pub_t *dhd, uint len);
extern void dhd_prot_rx_dataoffset(dhd_pub_t *dhd, uint32 offset);
extern int dhd_prot_txdata(dhd_pub_t *dhd, void *p, uint8 ifidx);
extern int dhdmsgbuf_dmaxfer_req(dhd_pub_t *dhd,
	uint len, uint srcdelay, uint destdelay, uint d11_lpbk, uint core_num);
extern int dhdmsgbuf_dmaxfer_status(dhd_pub_t *dhd, dma_xfer_info_t *result);

extern void dhd_dma_buf_init(dhd_pub_t *dhd, void *dma_buf,
	void *va, uint32 len, dmaaddr_t pa, void *dmah, void *secdma);
extern void dhd_prot_flowrings_pool_release(dhd_pub_t *dhd,
	uint16 flowid, void *msgbuf_ring);
extern int dhd_prot_flow_ring_create(dhd_pub_t *dhd, flow_ring_node_t *flow_ring_node);
extern int dhd_post_tx_ring_item(dhd_pub_t *dhd, void *PKTBUF, uint8 ifindex);
extern int dhd_prot_flow_ring_delete(dhd_pub_t *dhd, flow_ring_node_t *flow_ring_node);
extern int dhd_prot_flow_ring_flush(dhd_pub_t *dhd, flow_ring_node_t *flow_ring_node);
extern int dhd_prot_ringupd_dump(dhd_pub_t *dhd, struct bcmstrbuf *b);
extern uint32 dhd_prot_metadata_dbg_set(dhd_pub_t *dhd, bool val);
extern uint32 dhd_prot_metadata_dbg_get(dhd_pub_t *dhd);
extern uint32 dhd_prot_metadatalen_set(dhd_pub_t *dhd, uint32 val, bool rx);
extern uint32 dhd_prot_metadatalen_get(dhd_pub_t *dhd, bool rx);
extern void dhd_prot_print_flow_ring(dhd_pub_t *dhd, void *msgbuf_flow_info,
	struct bcmstrbuf *strbuf, const char * fmt);
extern void dhd_prot_print_info(dhd_pub_t *dhd, struct bcmstrbuf *strbuf);
extern void dhd_prot_update_txflowring(dhd_pub_t *dhdp, uint16 flow_id, void *msgring_info);
extern void dhd_prot_txdata_write_flush(dhd_pub_t *dhd, uint16 flow_id);
extern uint32 dhd_prot_txp_threshold(dhd_pub_t *dhd, bool set, uint32 val);
extern void dhd_prot_reset(dhd_pub_t *dhd);
extern uint16 dhd_get_max_flow_rings(dhd_pub_t *dhd);

#ifdef IDLE_TX_FLOW_MGMT
extern int dhd_prot_flow_ring_batch_suspend_request(dhd_pub_t *dhd, uint16 *ringid, uint16 count);
extern int dhd_prot_flow_ring_resume(dhd_pub_t *dhd, flow_ring_node_t *flow_ring_node);
#endif /* IDLE_TX_FLOW_MGMT */
extern int dhd_prot_init_info_rings(dhd_pub_t *dhd);
#ifdef DHD_HP2P
extern int dhd_prot_init_hp2p_rings(dhd_pub_t *dhd);
#endif /* DHD_HP2P */

extern int dhd_prot_check_tx_resource(dhd_pub_t *dhd);

extern void dhd_prot_update_pktid_txq_stop_cnt(dhd_pub_t *dhd);
extern void dhd_prot_update_pktid_txq_start_cnt(dhd_pub_t *dhd);
#else
static INLINE void dhd_prot_update_pktid_txq_stop_cnt(dhd_pub_t *dhd) { return; }
static INLINE void dhd_prot_update_pktid_txq_start_cnt(dhd_pub_t *dhd) { return; }
#endif /* BCMPCIE */

#ifdef DHD_LB
extern void dhd_lb_tx_compl_handler(unsigned long data);
extern void dhd_lb_rx_compl_handler(unsigned long data);
extern void dhd_lb_rx_process_handler(unsigned long data);
#endif /* DHD_LB */
extern int dhd_prot_h2d_mbdata_send_ctrlmsg(dhd_pub_t *dhd, uint32 mb_data);

#ifdef BCMPCIE
extern int dhd_prot_send_host_timestamp(dhd_pub_t *dhdp, uchar *tlv, uint16 tlv_len,
	uint16 seq, uint16 xt_id);
extern bool dhd_prot_data_path_tx_timestamp_logging(dhd_pub_t *dhd,  bool enable, bool set);
extern bool dhd_prot_data_path_rx_timestamp_logging(dhd_pub_t *dhd,  bool enable, bool set);
extern bool dhd_prot_pkt_noretry(dhd_pub_t *dhd, bool enable, bool set);
extern bool dhd_prot_pkt_noaggr(dhd_pub_t *dhd, bool enable, bool set);
extern bool dhd_prot_pkt_fixed_rate(dhd_pub_t *dhd, bool enable, bool set);
#else /* BCMPCIE */
#define dhd_prot_send_host_timestamp(a, b, c, d, e)		0
#define dhd_prot_data_path_tx_timestamp_logging(a, b, c)	0
#define dhd_prot_data_path_rx_timestamp_logging(a, b, c)	0
#endif /* BCMPCIE */

extern void dhd_prot_dma_indx_free(dhd_pub_t *dhd);

#ifdef EWP_EDL
int dhd_prot_init_edl_rings(dhd_pub_t *dhd);
bool dhd_prot_process_msgbuf_edl(dhd_pub_t *dhd);
int dhd_prot_process_edl_complete(dhd_pub_t *dhd, void *evt_decode_data);
#endif /* EWP_EDL  */

/* APIs for managing a DMA-able buffer */
int  dhd_dma_buf_alloc(dhd_pub_t *dhd, dhd_dma_buf_t *dma_buf, uint32 buf_len);
void dhd_dma_buf_free(dhd_pub_t *dhd, dhd_dma_buf_t *dma_buf);

/********************************
 * For version-string expansion *
 */
#if defined(BDC)
#define DHD_PROTOCOL "bdc"
#elif defined(CDC)
#define DHD_PROTOCOL "cdc"
#else
#define DHD_PROTOCOL "unknown"
#endif /* proto */

int dhd_get_hscb_info(dhd_pub_t *dhd, void ** va, uint32 *len);
int dhd_get_hscb_buff(dhd_pub_t *dhd, uint32 offset, uint32 length, void * buff);

#ifdef DHD_HP2P
extern uint8 dhd_prot_hp2p_enable(dhd_pub_t *dhd, bool set, int enable);
extern uint32 dhd_prot_pkt_threshold(dhd_pub_t *dhd, bool set, uint32 val);
extern uint32 dhd_prot_time_threshold(dhd_pub_t *dhd, bool set, uint32 val);
extern uint32 dhd_prot_pkt_expiry(dhd_pub_t *dhd, bool set, uint32 val);
#endif // endif

#ifdef DHD_MAP_LOGGING
extern void dhd_prot_smmu_fault_dump(dhd_pub_t *dhdp);
#endif /* DHD_MAP_LOGGING */
#endif /* _dhd_proto_h_ */
