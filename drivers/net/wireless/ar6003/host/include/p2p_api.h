//------------------------------------------------------------------------------
// <copyright file="p2p_api.h" company="Atheros">
//    Copyright (c) 2004-2010 Atheros Corporation.  All rights reserved.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//------------------------------------------------------------------------------
//==============================================================================
// This file contains definitions exported by the P2P host module.
//
// Author(s): ="Atheros"
//==============================================================================

#ifndef _HOST_P2P_API_H_
#define _HOST_P2P_API_H_

#include "utils_api.h"
#include "wmi.h"

#ifdef __cplusplus
extern "C" {
#endif

#define P2P_OUI 0x99a6f50

static int __inline
isp2poui(const A_UINT8 *frm)
{
    return frm[1] > 3 && LE_READ_4(frm+2) == (P2P_OUI);
}

/* API function declarations */

void *p2p_init(void *dev);

struct host_p2p_dev *p2p_get_device(void *p2p_dev_ctx, const A_UINT8 *addr);

void *p2p_bssinfo_rx(void *p2p_dev_ctx, WMI_BI_FTYPE fType, A_UINT8 *addr, A_UINT16 channel, const A_UINT8 *data, A_UINT32 len);

void p2p_go_neg_req_rx(void *p2p_dev_ctx, const A_UINT8 *datap, A_UINT8 len);

void p2p_invite_req_rx(void *p2p_dev_ctx, const A_UINT8 *datap, A_UINT8 len);

void p2p_prov_disc_req_rx(void *p2p_dev_ctx, const A_UINT8 *datap, A_UINT8 len);
void p2p_prov_disc_resp_rx(void *p2p_dev_ctx,
                     const A_UINT8 *datap, A_UINT8 len);
void p2p_start_sdpd_event_rx(void *p2p_dev_ctx);
void p2p_sdpd_rx_event_rx(void *p2p_dev_ctx,
                      const A_UINT8 *datap, A_UINT8 len);
void p2p_free_all_devices(void *ctx);
void p2p_device_free(void *peer_dev);

A_STATUS p2p_auth_go_neg(void *ctx,
                WMI_P2P_GO_NEG_START_CMD *auth_go_neg_param);

A_STATUS p2p_auth_invite(void *ctx, A_UINT8 *auth_peer);

A_STATUS p2p_peer_reject(void *ctx, A_UINT8 *peer_addr);

A_STATUS p2p_go_neg_start(void *ctx, WMI_P2P_GO_NEG_START_CMD *go_neg_param);

A_STATUS p2p_invite_cmd(void *ctx, WMI_P2P_INVITE_CMD *invite_param);

A_STATUS p2p_prov_disc_req(void *ctx, A_UINT8 *peer, A_UINT16 wps_method);

A_STATUS p2p_peer(void *ctx, A_UINT8 *peer, A_UINT8 next);

A_STATUS p2p_get_device_p2p_buf(void *ctx, A_UINT8 *peer,  A_UINT8 **p2p_buf, A_UINT8 *p2p_buf_len);

A_STATUS wmi_p2p_get_go_params(void *ctx, A_UINT8 *go_dev_addr,
             A_UINT16 *oper_freq, A_UINT8 *ssid, A_UINT8 *ssid_len);

A_STATUS p2p_get_devaddr (void *ctx, A_UINT8 *intf_addr);

A_STATUS p2p_get_ifaddr (void *ctx, A_UINT8 *dev_addr);

struct host_p2p_dev *p2p_get_device_intf_addrs(void *ctx, const A_UINT8 *intfaddr);

void p2p_increment_dev_ref_count(struct host_p2p_dev *dev);

void p2p_free_all_sd_queries(void *ctx);

A_STATUS p2p_sd_request(void *ctx, A_UINT8 *peer_addr, A_UINT8 *tlvbuf,
                A_UINT8 tlv_buflen, A_UINT32 *qid);

A_STATUS p2p_sdpd_tx_cmd(void *ctx, WMI_P2P_SDPD_TX_CMD *sdpd_tx_cmd, A_UINT32 *qid);

A_STATUS p2p_sd_cancel_request(void *ctx, A_UINT32 qid);

void p2p_go_neg_complete_rx(void *ctx, const A_UINT8 *datap, A_UINT8 len);

int p2p_get_peer_info(void *ctx, A_UINT8 *peer_addr, A_UINT8 *buf, A_UINT32 buflen);

int p2p_get_next_addr(void *ctx, A_UINT8 *addr, A_UINT8 *buf, A_UINT32 buflen, int first_element);

void p2p_clear_peers_reported_flag(void *ctx);

void p2p_clear_peers_authorized_flag(void *ctx, const A_UINT8 *addr);

#ifdef __cplusplus
}
#endif

#endif /* _HOST_P2P_API_H_ */
