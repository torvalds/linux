/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef	__MLME_OSDEP_H_
#define __MLME_OSDEP_H_


extern void rtw_report_sec_ie(struct adapter *adapter, u8 authmode, u8 *sec_ie);

void rtw_reset_securitypriv(struct adapter *adapter);

#endif	/* _MLME_OSDEP_H_ */
