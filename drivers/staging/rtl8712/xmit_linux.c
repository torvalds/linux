// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 * xmit_linux.c
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
 * Linux device driver for RTL8192SU
 *
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/

#define _XMIT_OSDEP_C_

#include <linux/usb.h>
#include <linux/ip.h>
#include <linux/if_ether.h>
#include <linux/kmemleak.h>

#include "osdep_service.h"
#include "drv_types.h"

#include "wifi.h"
#include "mlme_osdep.h"
#include "xmit_osdep.h"
#include "osdep_intf.h"

static uint remainder_len(struct pkt_file *pfile)
{
	return (uint)(pfile->buf_len - ((addr_t)(pfile->cur_addr) -
	       (addr_t)(pfile->buf_start)));
}

void _r8712_open_pktfile(_pkt *pktptr, struct pkt_file *pfile)
{
	pfile->pkt = pktptr;
	pfile->cur_addr = pfile->buf_start = pktptr->data;
	pfile->pkt_len = pfile->buf_len = pktptr->len;
	pfile->cur_buffer = pfile->buf_start;
}

uint _r8712_pktfile_read(struct pkt_file *pfile, u8 *rmem, uint rlen)
{
	uint len;

	len = remainder_len(pfile);
	len = (rlen > len) ? len : rlen;
	if (rmem)
		skb_copy_bits(pfile->pkt, pfile->buf_len - pfile->pkt_len,
			      rmem, len);
	pfile->cur_addr += len;
	pfile->pkt_len -= len;
	return len;
}

sint r8712_endofpktfile(struct pkt_file *pfile)
{
	return (pfile->pkt_len == 0);
}


void r8712_set_qos(struct pkt_file *ppktfile, struct pkt_attrib *pattrib)
{
	struct ethhdr etherhdr;
	struct iphdr ip_hdr;
	u16 UserPriority = 0;

	_r8712_open_pktfile(ppktfile->pkt, ppktfile);
	_r8712_pktfile_read(ppktfile, (unsigned char *)&etherhdr, ETH_HLEN);

	/* get UserPriority from IP hdr*/
	if (pattrib->ether_type == 0x0800) {
		_r8712_pktfile_read(ppktfile, (u8 *)&ip_hdr, sizeof(ip_hdr));
		/*UserPriority = (ntohs(ip_hdr.tos) >> 5) & 0x3 ;*/
		UserPriority = ip_hdr.tos >> 5;
	} else {
		/* "When priority processing of data frames is supported,
		 * a STA's SME should send EAPOL-Key frames at the highest
		 * priority."
		 */

		if (pattrib->ether_type == 0x888e)
			UserPriority = 7;
	}
	pattrib->priority = UserPriority;
	pattrib->hdrlen = WLAN_HDR_A3_QOS_LEN;
	pattrib->subtype = WIFI_QOS_DATA_TYPE;
}

void r8712_SetFilter(struct work_struct *work)
{
	struct _adapter *padapter = container_of(work, struct _adapter,
						wkFilterRxFF0);
	u8  oldvalue = 0x00, newvalue = 0x00;
	unsigned long irqL;

	oldvalue = r8712_read8(padapter, 0x117);
	newvalue = oldvalue & 0xfe;
	r8712_write8(padapter, 0x117, newvalue);

	spin_lock_irqsave(&padapter->lockRxFF0Filter, irqL);
	padapter->blnEnableRxFF0Filter = 1;
	spin_unlock_irqrestore(&padapter->lockRxFF0Filter, irqL);
	do {
		msleep(100);
	} while (padapter->blnEnableRxFF0Filter == 1);
	r8712_write8(padapter, 0x117, oldvalue);
}

int r8712_xmit_resource_alloc(struct _adapter *padapter,
			      struct xmit_buf *pxmitbuf)
{
	int i;

	for (i = 0; i < 8; i++) {
		pxmitbuf->pxmit_urb[i] = usb_alloc_urb(0, GFP_KERNEL);
		if (!pxmitbuf->pxmit_urb[i]) {
			netdev_err(padapter->pnetdev, "pxmitbuf->pxmit_urb[i] == NULL\n");
			return _FAIL;
		}
		kmemleak_not_leak(pxmitbuf->pxmit_urb[i]);
	}
	return _SUCCESS;
}

void r8712_xmit_resource_free(struct _adapter *padapter,
			      struct xmit_buf *pxmitbuf)
{
	int i;

	for (i = 0; i < 8; i++) {
		if (pxmitbuf->pxmit_urb[i]) {
			usb_kill_urb(pxmitbuf->pxmit_urb[i]);
			usb_free_urb(pxmitbuf->pxmit_urb[i]);
		}
	}
}

void r8712_xmit_complete(struct _adapter *padapter, struct xmit_frame *pxframe)
{
	if (pxframe->pkt)
		dev_kfree_skb_any(pxframe->pkt);
	pxframe->pkt = NULL;
}

int r8712_xmit_entry(_pkt *pkt, struct  net_device *pnetdev)
{
	struct xmit_frame *pxmitframe = NULL;
	struct _adapter *padapter = netdev_priv(pnetdev);
	struct xmit_priv *pxmitpriv = &(padapter->xmitpriv);

	if (!r8712_if_up(padapter))
		goto _xmit_entry_drop;

	pxmitframe = r8712_alloc_xmitframe(pxmitpriv);
	if (!pxmitframe)
		goto _xmit_entry_drop;

	if ((!r8712_update_attrib(padapter, pkt, &pxmitframe->attrib)))
		goto _xmit_entry_drop;

	padapter->ledpriv.LedControlHandler(padapter, LED_CTL_TX);
	pxmitframe->pkt = pkt;
	if (r8712_pre_xmit(padapter, pxmitframe)) {
		/*dump xmitframe directly or drop xframe*/
		dev_kfree_skb_any(pkt);
		pxmitframe->pkt = NULL;
	}
	pxmitpriv->tx_pkts++;
	pxmitpriv->tx_bytes += pxmitframe->attrib.last_txcmdsz;
	return 0;
_xmit_entry_drop:
	if (pxmitframe)
		r8712_free_xmitframe(pxmitpriv, pxmitframe);
	pxmitpriv->tx_drop++;
	dev_kfree_skb_any(pkt);
	return 0;
}
