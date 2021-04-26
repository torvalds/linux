/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
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
