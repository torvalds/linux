/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef __RECV_OSDEP_H_
#define __RECV_OSDEP_H_

#include "osdep_service.h"
#include "drv_types.h"

int _rtw_init_recv_priv(struct recv_priv *precvpriv, struct adapter *padapter);
void _rtw_free_recv_priv(struct recv_priv *precvpriv);

s32  rtw_recv_entry(struct recv_frame *precv_frame);

int netdev_open(struct net_device *pnetdev);
int netdev_close(struct net_device *pnetdev);

#endif /*  */
