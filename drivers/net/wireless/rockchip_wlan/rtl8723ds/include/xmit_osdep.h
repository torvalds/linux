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
#ifndef __XMIT_OSDEP_H_
#define __XMIT_OSDEP_H_


struct pkt_file {
	_pkt *pkt;
	SIZE_T pkt_len;	 /* the remainder length of the open_file */
	_buffer *cur_buffer;
	u8 *buf_start;
	u8 *cur_addr;
	SIZE_T buf_len;
};

#ifdef PLATFORM_WINDOWS

#ifdef PLATFORM_OS_XP
#ifdef CONFIG_USB_HCI
#include <usb.h>
#include <usbdlib.h>
#include <usbioctl.h>
#endif
#endif

#ifdef CONFIG_GSPI_HCI
	#define NR_XMITFRAME     64
#else
	#define NR_XMITFRAME     128
#endif

#define ETH_ALEN	6

extern NDIS_STATUS rtw_xmit_entry(
	_nic_hdl		cnxt,
	NDIS_PACKET		*pkt,
	u32				flags
);

#endif /* PLATFORM_WINDOWS */

#ifdef PLATFORM_FREEBSD
#define NR_XMITFRAME	256
extern int rtw_xmit_entry(_pkt *pkt, _nic_hdl pnetdev);
extern void rtw_xmit_entry_wrap(struct ifnet *pifp);
#endif /* PLATFORM_FREEBSD */

#ifdef PLATFORM_LINUX

#define NR_XMITFRAME	256

struct xmit_priv;
struct pkt_attrib;
struct sta_xmit_priv;
struct xmit_frame;
struct xmit_buf;

extern int _rtw_xmit_entry(_pkt *pkt, _nic_hdl pnetdev);
extern int rtw_xmit_entry(_pkt *pkt, _nic_hdl pnetdev);

#endif /* PLATFORM_LINUX */

void rtw_os_xmit_schedule(_adapter *padapter);

int rtw_os_xmit_resource_alloc(_adapter *padapter, struct xmit_buf *pxmitbuf, u32 alloc_sz, u8 flag);
void rtw_os_xmit_resource_free(_adapter *padapter, struct xmit_buf *pxmitbuf, u32 free_sz, u8 flag);

extern void rtw_set_tx_chksum_offload(_pkt *pkt, struct pkt_attrib *pattrib);

extern uint rtw_remainder_len(struct pkt_file *pfile);
extern void _rtw_open_pktfile(_pkt *pkt, struct pkt_file *pfile);
extern uint _rtw_pktfile_read(struct pkt_file *pfile, u8 *rmem, uint rlen);
extern sint rtw_endofpktfile(struct pkt_file *pfile);

extern void rtw_os_pkt_complete(_adapter *padapter, _pkt *pkt);
extern void rtw_os_xmit_complete(_adapter *padapter, struct xmit_frame *pxframe);

void rtw_os_check_wakup_queue(_adapter *adapter, u16 os_qid);
bool rtw_os_check_stop_queue(_adapter *adapter, u16 os_qid);
void rtw_os_wake_queue_at_free_stainfo(_adapter *padapter, int *qcnt_freed);

void dump_os_queue(void *sel, _adapter *padapter);

#endif /* __XMIT_OSDEP_H_ */
