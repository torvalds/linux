/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *****************************************************************************/
#ifndef _RTW_IOCTL_H_
#define _RTW_IOCTL_H_

enum oid_type {
	QUERY_OID,
	SET_OID
};

struct oid_par_priv {
	void		*adapter_context;
	NDIS_OID	oid;
	void		*information_buf;
	u32		information_buf_len;
	u32		*bytes_rw;
	u32		*bytes_needed;
	enum oid_type	type_of_oid;
	u32		dbg;
};

#if defined(PLATFORM_LINUX) && defined(CONFIG_WIRELESS_EXT)
extern struct iw_handler_def  rtw_handlers_def;
#endif

extern void rtw_request_wps_pbc_event(_adapter *padapter);

#ifdef CONFIG_APPEND_VENDOR_IE_ENABLE
extern int rtw_vendor_ie_get_raw_data(struct net_device *, u32, char *, u32);
extern int rtw_vendor_ie_get_data(struct net_device*, int , char*);
extern int rtw_vendor_ie_get(struct net_device *, struct iw_request_info *, union iwreq_data *, char *);
extern int rtw_vendor_ie_set(struct net_device*, struct iw_request_info*, union iwreq_data*, char*);
#endif

#endif /*  #ifndef __INC_CEINFO_ */
