/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef __XMIT_OSDEP_H_
#define __XMIT_OSDEP_H_

#include "osdep_service.h"
#include "drv_types.h"

extern int rtw_ht_enable;
extern int rtw_cbw40_enable;
extern int rtw_ampdu_enable;/* for enable tx_ampdu */

struct xmit_priv;
struct pkt_attrib;
struct sta_xmit_priv;
struct xmit_frame;
struct xmit_buf;

int rtw_xmit_entry(struct sk_buff *pkt, struct  net_device *pnetdev);

#endif /* __XMIT_OSDEP_H_ */
