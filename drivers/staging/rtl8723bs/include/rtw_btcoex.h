/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2013 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef __RTW_BTCOEX_H__
#define __RTW_BTCOEX_H__

#include <drv_types.h>

#define	PACKET_NORMAL			0
#define	PACKET_DHCP				1
#define	PACKET_ARP				2
#define	PACKET_EAPOL			3

void rtw_btcoex_media_status_notify(struct adapter *padapter, u8 media_status);
void rtw_btcoex_halt_notify(struct adapter *padapter);

/*  ================================================== */
/*  Below Functions are called by BT-Coex */
/*  ================================================== */
void rtw_btcoex_reject_ap_aggregated_packet(struct adapter *padapter, u8 enable);
void rtw_btcoex_lps_enter(struct adapter *padapter);
void rtw_btcoex_lps_leave(struct adapter *padapter);

#endif /*  __RTW_BTCOEX_H__ */
