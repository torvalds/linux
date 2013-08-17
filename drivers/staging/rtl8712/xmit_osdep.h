/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
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
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/
#ifndef __XMIT_OSDEP_H_
#define __XMIT_OSDEP_H_

#include "osdep_service.h"
#include "drv_types.h"

struct pkt_file {
	_pkt	*pkt;
	u32	pkt_len;	 /*the remainder length of the open_file*/
	_buffer	*cur_buffer;
	u8	*buf_start;
	u8	*cur_addr;
	u32	buf_len;
};

#define NR_XMITFRAME	256

struct xmit_priv;
struct pkt_attrib;
struct sta_xmit_priv;
struct xmit_frame;
struct xmit_buf;

int r8712_xmit_entry(_pkt *pkt, struct  net_device *pnetdev);
void r8712_SetFilter(struct work_struct *work);
int r8712_xmit_resource_alloc(struct _adapter *padapter,
			   struct xmit_buf *pxmitbuf);
void r8712_xmit_resource_free(struct _adapter *padapter,
			   struct xmit_buf *pxmitbuf);

void r8712_set_qos(struct pkt_file *ppktfile,
		    struct pkt_attrib *pattrib);
void _r8712_open_pktfile(_pkt *pktptr, struct pkt_file *pfile);
uint _r8712_pktfile_read(struct pkt_file *pfile, u8 *rmem, uint rlen);
sint r8712_endofpktfile(struct pkt_file *pfile);
void r8712_xmit_complete(struct _adapter *padapter,
			     struct xmit_frame *pxframe);

#endif
