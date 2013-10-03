/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#ifndef __XMIT_OSDEP_H_
#define __XMIT_OSDEP_H_

#include <osdep_service.h>
#include <drv_types.h>

struct pkt_file {
	struct sk_buff *pkt;
	size_t pkt_len;	 /* the remainder length of the open_file */
	unsigned char *cur_buffer;
	u8 *buf_start;
	u8 *cur_addr;
	size_t buf_len;
};

extern int rtw_ht_enable;
extern int rtw_cbw40_enable;
extern int rtw_ampdu_enable;/* for enable tx_ampdu */

#define NR_XMITFRAME	256

struct xmit_priv;
struct pkt_attrib;
struct sta_xmit_priv;
struct xmit_frame;
struct xmit_buf;

int rtw_xmit_entry(struct sk_buff *pkt, struct  net_device *pnetdev);

void rtw_os_xmit_schedule(struct adapter *padapter);

int rtw_os_xmit_resource_alloc(struct adapter *padapter,
			       struct xmit_buf *pxmitbuf, u32 alloc_sz);
void rtw_os_xmit_resource_free(struct adapter *padapter,
			       struct xmit_buf *pxmitbuf, u32 free_sz);

void rtw_set_tx_chksum_offload(struct sk_buff *pkt, struct pkt_attrib *pattrib);

uint rtw_remainder_len(struct pkt_file *pfile);
void _rtw_open_pktfile(struct sk_buff *pkt, struct pkt_file *pfile);
uint _rtw_pktfile_read(struct pkt_file *pfile, u8 *rmem, uint rlen);
int rtw_endofpktfile(struct pkt_file *pfile);

void rtw_os_pkt_complete(struct adapter *padapter, struct sk_buff *pkt);
void rtw_os_xmit_complete(struct adapter *padapter,
			  struct xmit_frame *pxframe);

#endif /* __XMIT_OSDEP_H_ */
