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
	u16 user_priority = 0;

	_r8712_open_pktfile(ppktfile->pkt, ppktfile);
	_r8712_pktfile_read(ppktfile, (unsigned char *)&etherhdr, ETH_HLEN);

	/* get user_priority from IP hdr*/
	if (pattrib->ether_type == 0x0800) {
		_r8712_pktfile_read(ppktfile, (u8 *)&ip_hdr, sizeof(ip_hdr));
		/*user_priority = (ntohs(ip_hdr.tos) >> 5) & 0x3 ;*/
		user_priority = ip_hdr.tos >> 5;
	} else {
		/* "When priority processing of data frames is supported,
		 * a STA's SME should send EAPOL-Key frames at the highest
		 * priority."
		 */

		if (pattrib->ether_type == 0x888e)
			user_priority = 7;
	}
	pattrib->priority = user_priority;
	pattrib->hdrlen = WLAN_HDR_A3_QOS_LEN;
	pattrib->subtype = WIFI_QOS_DATA_TYPE;
}

void r8712_SetFilter(struct work_struct *work)
{
	struct _adapter *adapter = container_of(work, struct _adapter,
						wk_filter_rx_ff0);
	u8  oldvalue = 0x00, newvalue = 0x00;

	oldvalue = r8712_read8(adapter, 0x117);
	newvalue = oldvalue & 0xfe;
	r8712_write8(adapter, 0x117, newvalue);

	wait_for_completion(&adapter->rx_filter_ready);
	r8712_write8(adapter, 0x117, oldvalue);
}

int r8712_xmit_resource_alloc(struct _adapter *padapter,
			      struct xmit_buf *pxmitbuf)
{
	int i;

	for (i = 0; i < 8; i++) {
		pxmitbuf->pxmit_urb[i] = usb_alloc_urb(0, GFP_KERNEL);
		if (!pxmitbuf->pxmit_urb[i]) {
			netdev_err(padapter->pnetdev, "pxmitbuf->pxmit_urb[i] == NULL\n");
			return -ENOMEM;
		}
		kmemleak_not_leak(pxmitbuf->pxmit_urb[i]);
	}
	return 0;
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

netdev_tx_t r8712_xmit_entry(_pkt *pkt, struct  net_device *netdev)
{
	struct xmit_frame *xmitframe = NULL;
	struct _adapter *adapter = netdev_priv(netdev);
	struct xmit_priv *xmitpriv = &(adapter->xmitpriv);

	if (!r8712_if_up(adapter))
		goto _xmit_entry_drop;

	xmitframe = r8712_alloc_xmitframe(xmitpriv);
	if (!xmitframe)
		goto _xmit_entry_drop;

	if (r8712_update_attrib(adapter, pkt, &xmitframe->attrib))
		goto _xmit_entry_drop;

	adapter->ledpriv.LedControlHandler(adapter, LED_CTL_TX);
	xmitframe->pkt = pkt;
	if (r8712_pre_xmit(adapter, xmitframe)) {
		/*dump xmitframe directly or drop xframe*/
		dev_kfree_skb_any(pkt);
		xmitframe->pkt = NULL;
	}
	xmitpriv->tx_pkts++;
	xmitpriv->tx_bytes += xmitframe->attrib.last_txcmdsz;
	return NETDEV_TX_OK;
_xmit_entry_drop:
	if (xmitframe)
		r8712_free_xmitframe(xmitpriv, xmitframe);
	xmitpriv->tx_drop++;
	dev_kfree_skb_any(pkt);
	return NETDEV_TX_OK;
}
