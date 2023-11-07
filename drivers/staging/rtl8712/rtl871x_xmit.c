// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 * rtl871x_xmit.c
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

#define _RTL871X_XMIT_C_

#include "osdep_service.h"
#include "drv_types.h"
#include "wifi.h"
#include "osdep_intf.h"
#include "usb_ops.h"

#include <linux/usb.h>
#include <linux/ieee80211.h>

static const u8 P802_1H_OUI[P80211_OUI_LEN] = {0x00, 0x00, 0xf8};
static const u8 RFC1042_OUI[P80211_OUI_LEN] = {0x00, 0x00, 0x00};
static void init_hwxmits(struct hw_xmit *phwxmit, sint entry);
static void alloc_hwxmits(struct _adapter *padapter);
static void free_hwxmits(struct _adapter *padapter);

static void _init_txservq(struct tx_servq *ptxservq)
{
	INIT_LIST_HEAD(&ptxservq->tx_pending);
	_init_queue(&ptxservq->sta_pending);
	ptxservq->qcnt = 0;
}

void _r8712_init_sta_xmit_priv(struct sta_xmit_priv *psta_xmitpriv)
{
	memset((unsigned char *)psta_xmitpriv, 0,
		 sizeof(struct sta_xmit_priv));
	spin_lock_init(&psta_xmitpriv->lock);
	_init_txservq(&psta_xmitpriv->be_q);
	_init_txservq(&psta_xmitpriv->bk_q);
	_init_txservq(&psta_xmitpriv->vi_q);
	_init_txservq(&psta_xmitpriv->vo_q);
	INIT_LIST_HEAD(&psta_xmitpriv->legacy_dz);
	INIT_LIST_HEAD(&psta_xmitpriv->apsd);
}

int _r8712_init_xmit_priv(struct xmit_priv *pxmitpriv,
			  struct _adapter *padapter)
{
	sint i;
	struct xmit_buf *pxmitbuf;
	struct xmit_frame *pxframe;
	int j;

	memset((unsigned char *)pxmitpriv, 0, sizeof(struct xmit_priv));
	spin_lock_init(&pxmitpriv->lock);
	/*
	 *Please insert all the queue initialization using _init_queue below
	 */
	pxmitpriv->adapter = padapter;
	_init_queue(&pxmitpriv->be_pending);
	_init_queue(&pxmitpriv->bk_pending);
	_init_queue(&pxmitpriv->vi_pending);
	_init_queue(&pxmitpriv->vo_pending);
	_init_queue(&pxmitpriv->bm_pending);
	_init_queue(&pxmitpriv->legacy_dz_queue);
	_init_queue(&pxmitpriv->apsd_queue);
	_init_queue(&pxmitpriv->free_xmit_queue);
	/*
	 * Please allocate memory with sz = (struct xmit_frame) * NR_XMITFRAME,
	 * and initialize free_xmit_frame below.
	 * Please also apply  free_txobj to link_up all the xmit_frames...
	 */
	pxmitpriv->pallocated_frame_buf =
		kmalloc(NR_XMITFRAME * sizeof(struct xmit_frame) + 4,
			GFP_ATOMIC);
	if (!pxmitpriv->pallocated_frame_buf) {
		pxmitpriv->pxmit_frame_buf = NULL;
		return -ENOMEM;
	}
	pxmitpriv->pxmit_frame_buf = pxmitpriv->pallocated_frame_buf + 4 -
			((addr_t) (pxmitpriv->pallocated_frame_buf) & 3);
	pxframe = (struct xmit_frame *) pxmitpriv->pxmit_frame_buf;
	for (i = 0; i < NR_XMITFRAME; i++) {
		INIT_LIST_HEAD(&(pxframe->list));
		pxframe->padapter = padapter;
		pxframe->frame_tag = DATA_FRAMETAG;
		pxframe->pkt = NULL;
		pxframe->buf_addr = NULL;
		pxframe->pxmitbuf = NULL;
		list_add_tail(&(pxframe->list),
				 &(pxmitpriv->free_xmit_queue.queue));
		pxframe++;
	}
	pxmitpriv->free_xmitframe_cnt = NR_XMITFRAME;
	/*
	 * init xmit hw_txqueue
	 */
	_r8712_init_hw_txqueue(&pxmitpriv->be_txqueue, BE_QUEUE_INX);
	_r8712_init_hw_txqueue(&pxmitpriv->bk_txqueue, BK_QUEUE_INX);
	_r8712_init_hw_txqueue(&pxmitpriv->vi_txqueue, VI_QUEUE_INX);
	_r8712_init_hw_txqueue(&pxmitpriv->vo_txqueue, VO_QUEUE_INX);
	_r8712_init_hw_txqueue(&pxmitpriv->bmc_txqueue, BMC_QUEUE_INX);
	pxmitpriv->frag_len = MAX_FRAG_THRESHOLD;
	pxmitpriv->txirp_cnt = 1;
	/*per AC pending irp*/
	pxmitpriv->beq_cnt = 0;
	pxmitpriv->bkq_cnt = 0;
	pxmitpriv->viq_cnt = 0;
	pxmitpriv->voq_cnt = 0;
	/*init xmit_buf*/
	_init_queue(&pxmitpriv->free_xmitbuf_queue);
	_init_queue(&pxmitpriv->pending_xmitbuf_queue);
	pxmitpriv->pallocated_xmitbuf =
		kmalloc(NR_XMITBUFF * sizeof(struct xmit_buf) + 4, GFP_ATOMIC);
	if (!pxmitpriv->pallocated_xmitbuf)
		goto clean_up_frame_buf;
	pxmitpriv->pxmitbuf = pxmitpriv->pallocated_xmitbuf + 4 -
			      ((addr_t)(pxmitpriv->pallocated_xmitbuf) & 3);
	pxmitbuf = (struct xmit_buf *)pxmitpriv->pxmitbuf;
	for (i = 0; i < NR_XMITBUFF; i++) {
		INIT_LIST_HEAD(&pxmitbuf->list);
		pxmitbuf->pallocated_buf =
			kmalloc(MAX_XMITBUF_SZ + XMITBUF_ALIGN_SZ, GFP_ATOMIC);
		if (!pxmitbuf->pallocated_buf) {
			j = 0;
			goto clean_up_alloc_buf;
		}
		pxmitbuf->pbuf = pxmitbuf->pallocated_buf + XMITBUF_ALIGN_SZ -
				 ((addr_t) (pxmitbuf->pallocated_buf) &
				 (XMITBUF_ALIGN_SZ - 1));
		if (r8712_xmit_resource_alloc(padapter, pxmitbuf)) {
			j = 1;
			goto clean_up_alloc_buf;
		}
		list_add_tail(&pxmitbuf->list,
				 &(pxmitpriv->free_xmitbuf_queue.queue));
		pxmitbuf++;
	}
	pxmitpriv->free_xmitbuf_cnt = NR_XMITBUFF;
	INIT_WORK(&padapter->wk_filter_rx_ff0, r8712_SetFilter);
	alloc_hwxmits(padapter);
	init_hwxmits(pxmitpriv->hwxmits, pxmitpriv->hwxmit_entry);
	tasklet_setup(&pxmitpriv->xmit_tasklet, r8712_xmit_bh);
	return 0;

clean_up_alloc_buf:
	if (j) {
		/* failure happened in r8712_xmit_resource_alloc()
		 * delete extra pxmitbuf->pallocated_buf
		 */
		kfree(pxmitbuf->pallocated_buf);
	}
	for (j = 0; j < i; j++) {
		int k;

		pxmitbuf--;			/* reset pointer */
		kfree(pxmitbuf->pallocated_buf);
		for (k = 0; k < 8; k++)		/* delete xmit urb's */
			usb_free_urb(pxmitbuf->pxmit_urb[k]);
	}
	kfree(pxmitpriv->pallocated_xmitbuf);
	pxmitpriv->pallocated_xmitbuf = NULL;
clean_up_frame_buf:
	kfree(pxmitpriv->pallocated_frame_buf);
	pxmitpriv->pallocated_frame_buf = NULL;
	return -ENOMEM;
}

void _free_xmit_priv(struct xmit_priv *pxmitpriv)
{
	int i;
	struct _adapter *padapter = pxmitpriv->adapter;
	struct xmit_frame *pxmitframe = (struct xmit_frame *)
					pxmitpriv->pxmit_frame_buf;
	struct xmit_buf *pxmitbuf = (struct xmit_buf *)pxmitpriv->pxmitbuf;

	if (!pxmitpriv->pxmit_frame_buf)
		return;
	for (i = 0; i < NR_XMITFRAME; i++) {
		r8712_xmit_complete(padapter, pxmitframe);
		pxmitframe++;
	}
	for (i = 0; i < NR_XMITBUFF; i++) {
		r8712_xmit_resource_free(padapter, pxmitbuf);
		kfree(pxmitbuf->pallocated_buf);
		pxmitbuf++;
	}
	kfree(pxmitpriv->pallocated_frame_buf);
	kfree(pxmitpriv->pallocated_xmitbuf);
	free_hwxmits(padapter);
}

int r8712_update_attrib(struct _adapter *padapter, _pkt *pkt,
			struct pkt_attrib *pattrib)
{
	struct pkt_file pktfile;
	struct sta_info *psta = NULL;
	struct ethhdr etherhdr;

	struct tx_cmd txdesc;

	bool bmcast;
	struct sta_priv		*pstapriv = &padapter->stapriv;
	struct security_priv	*psecuritypriv = &padapter->securitypriv;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct qos_priv		*pqospriv = &pmlmepriv->qospriv;

	_r8712_open_pktfile(pkt, &pktfile);

	_r8712_pktfile_read(&pktfile, (unsigned char *)&etherhdr, ETH_HLEN);

	pattrib->ether_type = ntohs(etherhdr.h_proto);

	/*
	 * If driver xmit ARP packet, driver can set ps mode to initial
	 * setting. It stands for getting DHCP or fix IP.
	 */
	if (pattrib->ether_type == 0x0806) {
		if (padapter->pwrctrlpriv.pwr_mode !=
		    padapter->registrypriv.power_mgnt) {
			del_timer_sync(&pmlmepriv->dhcp_timer);
			r8712_set_ps_mode(padapter,
					  padapter->registrypriv.power_mgnt,
					  padapter->registrypriv.smart_ps);
		}
	}

	memcpy(pattrib->dst, &etherhdr.h_dest, ETH_ALEN);
	memcpy(pattrib->src, &etherhdr.h_source, ETH_ALEN);
	pattrib->pctrl = 0;
	if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) ||
	    check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)) {
		memcpy(pattrib->ra, pattrib->dst, ETH_ALEN);
		memcpy(pattrib->ta, pattrib->src, ETH_ALEN);
	} else if (check_fwstate(pmlmepriv, WIFI_STATION_STATE)) {
		memcpy(pattrib->ra, get_bssid(pmlmepriv), ETH_ALEN);
		memcpy(pattrib->ta, pattrib->src, ETH_ALEN);
	} else if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
		memcpy(pattrib->ra, pattrib->dst, ETH_ALEN);
		memcpy(pattrib->ta, get_bssid(pmlmepriv), ETH_ALEN);
	} else if (check_fwstate(pmlmepriv, WIFI_MP_STATE)) {
		/*firstly, filter packet not belongs to mp*/
		if (pattrib->ether_type != 0x8712)
			return -EINVAL;
		/* for mp storing the txcmd per packet,
		 * according to the info of txcmd to update pattrib
		 */
		/*get MP_TXDESC_SIZE bytes txcmd per packet*/
		_r8712_pktfile_read(&pktfile, (u8 *)&txdesc, TXDESC_SIZE);
		memcpy(pattrib->ra, pattrib->dst, ETH_ALEN);
		memcpy(pattrib->ta, pattrib->src, ETH_ALEN);
		pattrib->pctrl = 1;
	}
	/* r8712_xmitframe_coalesce() overwrite this!*/
	pattrib->pktlen = pktfile.pkt_len;
	if (pattrib->ether_type == ETH_P_IP) {
		/* The following is for DHCP and ARP packet, we use cck1M to
		 * tx these packets and let LPS awake some time
		 * to prevent DHCP protocol fail
		 */
		u8 tmp[24];

		_r8712_pktfile_read(&pktfile, &tmp[0], 24);
		pattrib->dhcp_pkt = 0;
		if (pktfile.pkt_len > 282) {/*MINIMUM_DHCP_PACKET_SIZE)*/
			if (pattrib->ether_type == ETH_P_IP) {/* IP header*/
				if (((tmp[21] == 68) && (tmp[23] == 67)) ||
					((tmp[21] == 67) && (tmp[23] == 68))) {
					/* 68 : UDP BOOTP client
					 * 67 : UDP BOOTP server
					 * Use low rate to send DHCP packet.
					 */
					pattrib->dhcp_pkt = 1;
				}
			}
		}
	}
	bmcast = is_multicast_ether_addr(pattrib->ra);
	/* get sta_info*/
	if (bmcast) {
		psta = r8712_get_bcmc_stainfo(padapter);
		pattrib->mac_id = 4;
	} else {
		if (check_fwstate(pmlmepriv, WIFI_MP_STATE)) {
			psta = r8712_get_stainfo(pstapriv,
						 get_bssid(pmlmepriv));
			pattrib->mac_id = 5;
		} else {
			psta = r8712_get_stainfo(pstapriv, pattrib->ra);
			if (!psta)  /* drop the pkt */
				return -ENOMEM;
			if (check_fwstate(pmlmepriv, WIFI_STATION_STATE))
				pattrib->mac_id = 5;
			else
				pattrib->mac_id = psta->mac_id;
		}
	}

	if (psta) {
		pattrib->psta = psta;
	} else {
		/* if we cannot get psta => drrp the pkt */
		return -ENOMEM;
	}

	pattrib->ack_policy = 0;
	/* get ether_hdr_len */
	pattrib->pkt_hdrlen = ETH_HLEN;

	if (pqospriv->qos_option) {
		r8712_set_qos(&pktfile, pattrib);
	} else {
		pattrib->hdrlen = WLAN_HDR_A3_LEN;
		pattrib->subtype = WIFI_DATA_TYPE;
		pattrib->priority = 0;
	}
	if (psta->ieee8021x_blocked) {
		pattrib->encrypt = 0;
		if ((pattrib->ether_type != 0x888e) &&
		    !check_fwstate(pmlmepriv, WIFI_MP_STATE))
			return -EINVAL;
	} else {
		GET_ENCRY_ALGO(psecuritypriv, psta, pattrib->encrypt, bmcast);
	}
	switch (pattrib->encrypt) {
	case _WEP40_:
	case _WEP104_:
		pattrib->iv_len = 4;
		pattrib->icv_len = 4;
		break;
	case _TKIP_:
		pattrib->iv_len = 8;
		pattrib->icv_len = 4;
		if (padapter->securitypriv.busetkipkey == _FAIL)
			return -EINVAL;
		break;
	case _AES_:
		pattrib->iv_len = 8;
		pattrib->icv_len = 8;
		break;
	default:
		pattrib->iv_len = 0;
		pattrib->icv_len = 0;
		break;
	}

	if (pattrib->encrypt &&
	    (padapter->securitypriv.sw_encrypt ||
	    !psecuritypriv->hw_decrypted))
		pattrib->bswenc = true;
	else
		pattrib->bswenc = false;
	/* if in MP_STATE, update pkt_attrib from mp_txcmd, and overwrite
	 * some settings above.
	 */
	if (check_fwstate(pmlmepriv, WIFI_MP_STATE))
		pattrib->priority =
		    (le32_to_cpu(txdesc.txdw1) >> QSEL_SHT) & 0x1f;
	return 0;
}

static int xmitframe_addmic(struct _adapter *padapter,
			    struct xmit_frame *pxmitframe)
{
	u32	curfragnum, length;
	u8	*pframe, *payload, mic[8];
	struct	mic_data micdata;
	struct	sta_info *stainfo;
	struct	qos_priv *pqospriv = &(padapter->mlmepriv.qospriv);
	struct	pkt_attrib  *pattrib = &pxmitframe->attrib;
	struct	security_priv *psecpriv = &padapter->securitypriv;
	struct	xmit_priv *pxmitpriv = &padapter->xmitpriv;
	u8 priority[4] = {};
	bool bmcst = is_multicast_ether_addr(pattrib->ra);

	if (pattrib->psta)
		stainfo = pattrib->psta;
	else
		stainfo = r8712_get_stainfo(&padapter->stapriv,
					    &pattrib->ra[0]);
	if (pattrib->encrypt == _TKIP_) {
		/*encode mic code*/
		if (stainfo) {
			u8 null_key[16] = {};

			pframe = pxmitframe->buf_addr + TXDESC_OFFSET;
			if (bmcst) {
				if (!memcmp(psecpriv->XGrptxmickey
				   [psecpriv->XGrpKeyid].skey,
				   null_key, 16))
					return -ENOMEM;
				/*start to calculate the mic code*/
				r8712_secmicsetkey(&micdata,
					psecpriv->XGrptxmickey
					[psecpriv->XGrpKeyid].skey);
			} else {
				if (!memcmp(&stainfo->tkiptxmickey.skey[0],
					    null_key, 16))
					return -ENOMEM;
				/* start to calculate the mic code */
				r8712_secmicsetkey(&micdata,
					     &stainfo->tkiptxmickey.skey[0]);
			}
			if (pframe[1] & 1) {   /* ToDS==1 */
				r8712_secmicappend(&micdata,
						   &pframe[16], 6); /*DA*/
				if (pframe[1] & 2)  /* From Ds==1 */
					r8712_secmicappend(&micdata,
							   &pframe[24], 6);
				else
					r8712_secmicappend(&micdata,
							   &pframe[10], 6);
			} else {	/* ToDS==0 */
				r8712_secmicappend(&micdata,
						   &pframe[4], 6); /* DA */
				if (pframe[1] & 2)  /* From Ds==1 */
					r8712_secmicappend(&micdata,
							   &pframe[16], 6);
				else
					r8712_secmicappend(&micdata,
							   &pframe[10], 6);
			}
			if (pqospriv->qos_option == 1)
				priority[0] = (u8)pxmitframe->attrib.priority;
			r8712_secmicappend(&micdata, &priority[0], 4);
			payload = pframe;
			for (curfragnum = 0; curfragnum < pattrib->nr_frags;
			     curfragnum++) {
				payload = (u8 *)RND4((addr_t)(payload));
				payload += pattrib->hdrlen + pattrib->iv_len;
				if ((curfragnum + 1) == pattrib->nr_frags) {
					length = pattrib->last_txcmdsz -
						  pattrib->hdrlen -
						  pattrib->iv_len -
						  ((psecpriv->sw_encrypt)
						  ? pattrib->icv_len : 0);
					r8712_secmicappend(&micdata, payload,
							   length);
					payload = payload + length;
				} else {
					length = pxmitpriv->frag_len -
					    pattrib->hdrlen - pattrib->iv_len -
					    ((psecpriv->sw_encrypt) ?
					    pattrib->icv_len : 0);
					r8712_secmicappend(&micdata, payload,
							   length);
					payload = payload + length +
						  pattrib->icv_len;
				}
			}
			r8712_secgetmic(&micdata, &(mic[0]));
			/* add mic code  and add the mic code length in
			 * last_txcmdsz
			 */
			memcpy(payload, &(mic[0]), 8);
			pattrib->last_txcmdsz += 8;
			payload = payload - pattrib->last_txcmdsz + 8;
		}
	}
	return 0;
}

static sint xmitframe_swencrypt(struct _adapter *padapter,
				struct xmit_frame *pxmitframe)
{
	struct pkt_attrib	*pattrib = &pxmitframe->attrib;

	if (pattrib->bswenc) {
		switch (pattrib->encrypt) {
		case _WEP40_:
		case _WEP104_:
			r8712_wep_encrypt(padapter, (u8 *)pxmitframe);
			break;
		case _TKIP_:
			r8712_tkip_encrypt(padapter, (u8 *)pxmitframe);
			break;
		case _AES_:
			r8712_aes_encrypt(padapter, (u8 *)pxmitframe);
			break;
		default:
				break;
		}
	}
	return _SUCCESS;
}

static int make_wlanhdr(struct _adapter *padapter, u8 *hdr,
			struct pkt_attrib *pattrib)
{
	u16 *qc;

	struct ieee80211_hdr *pwlanhdr = (struct ieee80211_hdr *)hdr;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	struct qos_priv *pqospriv = &pmlmepriv->qospriv;
	__le16 *fctrl = &pwlanhdr->frame_control;
	u8 *bssid;

	memset(hdr, 0, WLANHDR_OFFSET);
	SetFrameSubType(fctrl, pattrib->subtype);
	if (!(pattrib->subtype & WIFI_DATA_TYPE))
		return 0;

	bssid = get_bssid(pmlmepriv);

	if (check_fwstate(pmlmepriv,  WIFI_STATION_STATE)) {
		/* to_ds = 1, fr_ds = 0; */
		SetToDs(fctrl);
		ether_addr_copy(pwlanhdr->addr1, bssid);
		ether_addr_copy(pwlanhdr->addr2, pattrib->src);
		ether_addr_copy(pwlanhdr->addr3, pattrib->dst);
	} else if (check_fwstate(pmlmepriv, WIFI_AP_STATE)) {
		/* to_ds = 0, fr_ds = 1; */
		SetFrDs(fctrl);
		ether_addr_copy(pwlanhdr->addr1, pattrib->dst);
		ether_addr_copy(pwlanhdr->addr2, bssid);
		ether_addr_copy(pwlanhdr->addr3, pattrib->src);
	} else if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) ||
		   check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE)) {
		ether_addr_copy(pwlanhdr->addr1, pattrib->dst);
		ether_addr_copy(pwlanhdr->addr2, pattrib->src);
		ether_addr_copy(pwlanhdr->addr3, bssid);
	} else if (check_fwstate(pmlmepriv, WIFI_MP_STATE)) {
		ether_addr_copy(pwlanhdr->addr1, pattrib->dst);
		ether_addr_copy(pwlanhdr->addr2, pattrib->src);
		ether_addr_copy(pwlanhdr->addr3, bssid);
	} else {
		return -EINVAL;
	}

	if (pattrib->encrypt)
		SetPrivacy(fctrl);
	if (pqospriv->qos_option) {
		qc = (unsigned short *)(hdr + pattrib->hdrlen - 2);
		if (pattrib->priority)
			SetPriority(qc, pattrib->priority);
		SetAckpolicy(qc, pattrib->ack_policy);
	}
	/* TODO: fill HT Control Field */
	/* Update Seq Num will be handled by f/w */
	{
		struct sta_info *psta;
		bool bmcst = is_multicast_ether_addr(pattrib->ra);

		if (pattrib->psta)
			psta = pattrib->psta;
		else if (bmcst)
			psta = r8712_get_bcmc_stainfo(padapter);
		else
			psta = r8712_get_stainfo(&padapter->stapriv,
						 pattrib->ra);

		if (psta) {
			u16 *txtid = psta->sta_xmitpriv.txseq_tid;

			txtid[pattrib->priority]++;
			txtid[pattrib->priority] &= 0xFFF;
			pattrib->seqnum = txtid[pattrib->priority];
			SetSeqNum(hdr, pattrib->seqnum);
		}
	}

	return 0;
}

static sint r8712_put_snap(u8 *data, u16 h_proto)
{
	struct ieee80211_snap_hdr *snap;
	const u8 *oui;

	snap = (struct ieee80211_snap_hdr *)data;
	snap->dsap = 0xaa;
	snap->ssap = 0xaa;
	snap->ctrl = 0x03;
	if (h_proto == 0x8137 || h_proto == 0x80f3)
		oui = P802_1H_OUI;
	else
		oui = RFC1042_OUI;
	snap->oui[0] = oui[0];
	snap->oui[1] = oui[1];
	snap->oui[2] = oui[2];
	*(__be16 *)(data + SNAP_SIZE) = htons(h_proto);
	return SNAP_SIZE + sizeof(u16);
}

/*
 * This sub-routine will perform all the following:
 * 1. remove 802.3 header.
 * 2. create wlan_header, based on the info in pxmitframe
 * 3. append sta's iv/ext-iv
 * 4. append LLC
 * 5. move frag chunk from pframe to pxmitframe->mem
 * 6. apply sw-encrypt, if necessary.
 */
sint r8712_xmitframe_coalesce(struct _adapter *padapter, _pkt *pkt,
			struct xmit_frame *pxmitframe)
{
	struct pkt_file pktfile;

	sint	frg_len, mpdu_len, llc_sz;
	u32	mem_sz;
	u8	frg_inx;
	addr_t addr;
	u8 *pframe, *mem_start, *ptxdesc;
	struct sta_info		*psta;
	struct security_priv	*psecpriv = &padapter->securitypriv;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;
	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	u8 *pbuf_start;
	bool bmcst = is_multicast_ether_addr(pattrib->ra);

	if (!pattrib->psta)
		return _FAIL;
	psta = pattrib->psta;
	if (!pxmitframe->buf_addr)
		return _FAIL;
	pbuf_start = pxmitframe->buf_addr;
	ptxdesc = pbuf_start;
	mem_start = pbuf_start + TXDESC_OFFSET;
	if (make_wlanhdr(padapter, mem_start, pattrib))
		return _FAIL;
	_r8712_open_pktfile(pkt, &pktfile);
	_r8712_pktfile_read(&pktfile, NULL, (uint) pattrib->pkt_hdrlen);
	if (check_fwstate(pmlmepriv, WIFI_MP_STATE)) {
		/* truncate TXDESC_SIZE bytes txcmd if at mp mode for 871x */
		if (pattrib->ether_type == 0x8712) {
			/* take care -  update_txdesc overwrite this */
			_r8712_pktfile_read(&pktfile, ptxdesc, TXDESC_SIZE);
		}
	}
	pattrib->pktlen = pktfile.pkt_len;
	frg_inx = 0;
	frg_len = pxmitpriv->frag_len - 4;
	while (1) {
		llc_sz = 0;
		mpdu_len = frg_len;
		pframe = mem_start;
		SetMFrag(mem_start);
		pframe += pattrib->hdrlen;
		mpdu_len -= pattrib->hdrlen;
		/* adding icv, if necessary...*/
		if (pattrib->iv_len) {
			if (psta) {
				switch (pattrib->encrypt) {
				case _WEP40_:
				case _WEP104_:
					WEP_IV(pattrib->iv, psta->txpn,
					       (u8)psecpriv->PrivacyKeyIndex);
					break;
				case _TKIP_:
					if (bmcst)
						TKIP_IV(pattrib->iv,
						    psta->txpn,
						    (u8)psecpriv->XGrpKeyid);
					else
						TKIP_IV(pattrib->iv, psta->txpn,
							0);
					break;
				case _AES_:
					if (bmcst)
						AES_IV(pattrib->iv, psta->txpn,
						    (u8)psecpriv->XGrpKeyid);
					else
						AES_IV(pattrib->iv, psta->txpn,
						       0);
					break;
				}
			}
			memcpy(pframe, pattrib->iv, pattrib->iv_len);
			pframe += pattrib->iv_len;
			mpdu_len -= pattrib->iv_len;
		}
		if (frg_inx == 0) {
			llc_sz = r8712_put_snap(pframe, pattrib->ether_type);
			pframe += llc_sz;
			mpdu_len -= llc_sz;
		}
		if ((pattrib->icv_len > 0) && (pattrib->bswenc))
			mpdu_len -= pattrib->icv_len;
		if (bmcst)
			mem_sz = _r8712_pktfile_read(&pktfile, pframe,
				 pattrib->pktlen);
		else
			mem_sz = _r8712_pktfile_read(&pktfile, pframe,
				 mpdu_len);
		pframe += mem_sz;
		if ((pattrib->icv_len > 0) && (pattrib->bswenc)) {
			memcpy(pframe, pattrib->icv, pattrib->icv_len);
			pframe += pattrib->icv_len;
		}
		frg_inx++;
		if (bmcst || r8712_endofpktfile(&pktfile)) {
			pattrib->nr_frags = frg_inx;
			pattrib->last_txcmdsz = pattrib->hdrlen +
						pattrib->iv_len +
						((pattrib->nr_frags == 1) ?
						llc_sz : 0) +
						((pattrib->bswenc) ?
						pattrib->icv_len : 0) + mem_sz;
			ClearMFrag(mem_start);
			break;
		}
		addr = (addr_t)(pframe);
		mem_start = (unsigned char *)RND4(addr) + TXDESC_OFFSET;
		memcpy(mem_start, pbuf_start + TXDESC_OFFSET, pattrib->hdrlen);
	}

	if (xmitframe_addmic(padapter, pxmitframe))
		return _FAIL;
	xmitframe_swencrypt(padapter, pxmitframe);
	return _SUCCESS;
}

void r8712_update_protection(struct _adapter *padapter, u8 *ie, uint ie_len)
{
	uint	protection;
	u8	*perp;
	uint	erp_len;
	struct	xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct	registry_priv *pregistrypriv = &padapter->registrypriv;

	switch (pxmitpriv->vcs_setting) {
	case DISABLE_VCS:
		pxmitpriv->vcs = NONE_VCS;
		break;
	case ENABLE_VCS:
		break;
	case AUTO_VCS:
	default:
		perp = r8712_get_ie(ie, WLAN_EID_ERP_INFO, &erp_len, ie_len);
		if (!perp) {
			pxmitpriv->vcs = NONE_VCS;
		} else {
			protection = (*(perp + 2)) & BIT(1);
			if (protection) {
				if (pregistrypriv->vcs_type == RTS_CTS)
					pxmitpriv->vcs = RTS_CTS;
				else
					pxmitpriv->vcs = CTS_TO_SELF;
			} else {
				pxmitpriv->vcs = NONE_VCS;
			}
		}
		break;
	}
}

struct xmit_buf *r8712_alloc_xmitbuf(struct xmit_priv *pxmitpriv)
{
	unsigned long irqL;
	struct xmit_buf *pxmitbuf;
	struct  __queue *pfree_xmitbuf_queue = &pxmitpriv->free_xmitbuf_queue;

	spin_lock_irqsave(&pfree_xmitbuf_queue->lock, irqL);
	pxmitbuf = list_first_entry_or_null(&pfree_xmitbuf_queue->queue,
					    struct xmit_buf, list);
	if (pxmitbuf) {
		list_del_init(&pxmitbuf->list);
		pxmitpriv->free_xmitbuf_cnt--;
	}
	spin_unlock_irqrestore(&pfree_xmitbuf_queue->lock, irqL);
	return pxmitbuf;
}

void r8712_free_xmitbuf(struct xmit_priv *pxmitpriv, struct xmit_buf *pxmitbuf)
{
	unsigned long irqL;
	struct  __queue *pfree_xmitbuf_queue = &pxmitpriv->free_xmitbuf_queue;

	if (!pxmitbuf)
		return;
	spin_lock_irqsave(&pfree_xmitbuf_queue->lock, irqL);
	list_del_init(&pxmitbuf->list);
	list_add_tail(&(pxmitbuf->list), &pfree_xmitbuf_queue->queue);
	pxmitpriv->free_xmitbuf_cnt++;
	spin_unlock_irqrestore(&pfree_xmitbuf_queue->lock, irqL);
}

/*
 * Calling context:
 * 1. OS_TXENTRY
 * 2. RXENTRY (rx_thread or RX_ISR/RX_CallBack)
 *
 * If we turn on USE_RXTHREAD, then, no need for critical section.
 * Otherwise, we must use _enter/_exit critical to protect free_xmit_queue...
 *
 * Must be very very cautious...
 *
 */
struct xmit_frame *r8712_alloc_xmitframe(struct xmit_priv *pxmitpriv)
{
	/*
	 * Please remember to use all the osdep_service api,
	 * and lock/unlock or _enter/_exit critical to protect
	 * pfree_xmit_queue
	 */
	unsigned long irqL;
	struct xmit_frame *pxframe;
	struct  __queue *pfree_xmit_queue = &pxmitpriv->free_xmit_queue;

	spin_lock_irqsave(&pfree_xmit_queue->lock, irqL);
	pxframe = list_first_entry_or_null(&pfree_xmit_queue->queue,
					   struct xmit_frame, list);
	if (pxframe) {
		list_del_init(&pxframe->list);
		pxmitpriv->free_xmitframe_cnt--;
		pxframe->buf_addr = NULL;
		pxframe->pxmitbuf = NULL;
		pxframe->attrib.psta = NULL;
		pxframe->pkt = NULL;
	}
	spin_unlock_irqrestore(&pfree_xmit_queue->lock, irqL);
	return pxframe;
}

void r8712_free_xmitframe(struct xmit_priv *pxmitpriv,
			  struct xmit_frame *pxmitframe)
{
	unsigned long irqL;
	struct  __queue *pfree_xmit_queue = &pxmitpriv->free_xmit_queue;
	struct _adapter *padapter = pxmitpriv->adapter;

	if (!pxmitframe)
		return;
	spin_lock_irqsave(&pfree_xmit_queue->lock, irqL);
	list_del_init(&pxmitframe->list);
	if (pxmitframe->pkt)
		pxmitframe->pkt = NULL;
	list_add_tail(&pxmitframe->list, &pfree_xmit_queue->queue);
	pxmitpriv->free_xmitframe_cnt++;
	spin_unlock_irqrestore(&pfree_xmit_queue->lock, irqL);
	if (netif_queue_stopped(padapter->pnetdev))
		netif_wake_queue(padapter->pnetdev);
}

void r8712_free_xmitframe_ex(struct xmit_priv *pxmitpriv,
		      struct xmit_frame *pxmitframe)
{
	if (!pxmitframe)
		return;
	if (pxmitframe->frame_tag == DATA_FRAMETAG)
		r8712_free_xmitframe(pxmitpriv, pxmitframe);
}

void r8712_free_xmitframe_queue(struct xmit_priv *pxmitpriv,
				struct  __queue *pframequeue)
{
	unsigned long irqL;
	struct list_head *plist, *phead;
	struct	xmit_frame	*pxmitframe;

	spin_lock_irqsave(&(pframequeue->lock), irqL);
	phead = &pframequeue->queue;
	plist = phead->next;
	while (!end_of_queue_search(phead, plist)) {
		pxmitframe = container_of(plist, struct xmit_frame, list);
		plist = plist->next;
		r8712_free_xmitframe(pxmitpriv, pxmitframe);
	}
	spin_unlock_irqrestore(&(pframequeue->lock), irqL);
}

static inline struct tx_servq *get_sta_pending(struct _adapter *padapter,
					       struct  __queue **ppstapending,
					       struct sta_info *psta, sint up)
{

	struct tx_servq *ptxservq;
	struct hw_xmit *phwxmits =  padapter->xmitpriv.hwxmits;

	switch (up) {
	case 1:
	case 2:
		ptxservq = &(psta->sta_xmitpriv.bk_q);
		*ppstapending = &padapter->xmitpriv.bk_pending;
		(phwxmits + 3)->accnt++;
		break;
	case 4:
	case 5:
		ptxservq = &(psta->sta_xmitpriv.vi_q);
		*ppstapending = &padapter->xmitpriv.vi_pending;
		(phwxmits + 1)->accnt++;
		break;
	case 6:
	case 7:
		ptxservq = &(psta->sta_xmitpriv.vo_q);
		*ppstapending = &padapter->xmitpriv.vo_pending;
		(phwxmits + 0)->accnt++;
		break;
	case 0:
	case 3:
	default:
		ptxservq = &(psta->sta_xmitpriv.be_q);
		*ppstapending = &padapter->xmitpriv.be_pending;
		(phwxmits + 2)->accnt++;
		break;
	}
	return ptxservq;
}

/*
 * Will enqueue pxmitframe to the proper queue, and indicate it
 * to xx_pending list.....
 */
int r8712_xmit_classifier(struct _adapter *padapter,
			  struct xmit_frame *pxmitframe)
{
	unsigned long irqL0;
	struct  __queue *pstapending;
	struct sta_info	*psta;
	struct tx_servq	*ptxservq;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;
	struct sta_priv *pstapriv = &padapter->stapriv;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	bool bmcst = is_multicast_ether_addr(pattrib->ra);

	if (pattrib->psta) {
		psta = pattrib->psta;
	} else {
		if (bmcst) {
			psta = r8712_get_bcmc_stainfo(padapter);
		} else {
			if (check_fwstate(pmlmepriv, WIFI_MP_STATE))
				psta = r8712_get_stainfo(pstapriv,
				       get_bssid(pmlmepriv));
			else
				psta = r8712_get_stainfo(pstapriv, pattrib->ra);
		}
	}
	if (!psta)
		return -EINVAL;
	ptxservq = get_sta_pending(padapter, &pstapending,
		   psta, pattrib->priority);
	spin_lock_irqsave(&pstapending->lock, irqL0);
	if (list_empty(&ptxservq->tx_pending))
		list_add_tail(&ptxservq->tx_pending, &pstapending->queue);
	list_add_tail(&pxmitframe->list, &ptxservq->sta_pending.queue);
	ptxservq->qcnt++;
	spin_unlock_irqrestore(&pstapending->lock, irqL0);
	return 0;
}

static void alloc_hwxmits(struct _adapter *padapter)
{
	struct hw_xmit *hwxmits;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	pxmitpriv->hwxmit_entry = HWXMIT_ENTRY;
	pxmitpriv->hwxmits = kmalloc_array(pxmitpriv->hwxmit_entry,
				sizeof(struct hw_xmit), GFP_ATOMIC);
	if (!pxmitpriv->hwxmits)
		return;
	hwxmits = pxmitpriv->hwxmits;
	if (pxmitpriv->hwxmit_entry == 5) {
		pxmitpriv->bmc_txqueue.head = 0;
		hwxmits[0] .phwtxqueue = &pxmitpriv->bmc_txqueue;
		hwxmits[0] .sta_queue = &pxmitpriv->bm_pending;
		pxmitpriv->vo_txqueue.head = 0;
		hwxmits[1] .phwtxqueue = &pxmitpriv->vo_txqueue;
		hwxmits[1] .sta_queue = &pxmitpriv->vo_pending;
		pxmitpriv->vi_txqueue.head = 0;
		hwxmits[2] .phwtxqueue = &pxmitpriv->vi_txqueue;
		hwxmits[2] .sta_queue = &pxmitpriv->vi_pending;
		pxmitpriv->bk_txqueue.head = 0;
		hwxmits[3] .phwtxqueue = &pxmitpriv->bk_txqueue;
		hwxmits[3] .sta_queue = &pxmitpriv->bk_pending;
		pxmitpriv->be_txqueue.head = 0;
		hwxmits[4] .phwtxqueue = &pxmitpriv->be_txqueue;
		hwxmits[4] .sta_queue = &pxmitpriv->be_pending;
	} else if (pxmitpriv->hwxmit_entry == 4) {
		pxmitpriv->vo_txqueue.head = 0;
		hwxmits[0] .phwtxqueue = &pxmitpriv->vo_txqueue;
		hwxmits[0] .sta_queue = &pxmitpriv->vo_pending;
		pxmitpriv->vi_txqueue.head = 0;
		hwxmits[1] .phwtxqueue = &pxmitpriv->vi_txqueue;
		hwxmits[1] .sta_queue = &pxmitpriv->vi_pending;
		pxmitpriv->be_txqueue.head = 0;
		hwxmits[2] .phwtxqueue = &pxmitpriv->be_txqueue;
		hwxmits[2] .sta_queue = &pxmitpriv->be_pending;
		pxmitpriv->bk_txqueue.head = 0;
		hwxmits[3] .phwtxqueue = &pxmitpriv->bk_txqueue;
		hwxmits[3] .sta_queue = &pxmitpriv->bk_pending;
	}
}

static void free_hwxmits(struct _adapter *padapter)
{
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	kfree(pxmitpriv->hwxmits);
}

static void init_hwxmits(struct hw_xmit *phwxmit, sint entry)
{
	sint i;

	for (i = 0; i < entry; i++, phwxmit++) {
		spin_lock_init(&phwxmit->xmit_lock);
		INIT_LIST_HEAD(&phwxmit->pending);
		phwxmit->txcmdcnt = 0;
		phwxmit->accnt = 0;
	}
}

void xmitframe_xmitbuf_attach(struct xmit_frame *pxmitframe,
			struct xmit_buf *pxmitbuf)
{
	/* pxmitbuf attach to pxmitframe */
	pxmitframe->pxmitbuf = pxmitbuf;
	/* urb and irp connection */
	pxmitframe->pxmit_urb[0] = pxmitbuf->pxmit_urb[0];
	/* buffer addr assoc */
	pxmitframe->buf_addr = pxmitbuf->pbuf;
	/* pxmitframe attach to pxmitbuf */
	pxmitbuf->priv_data = pxmitframe;
}

/*
 * tx_action == 0 == no frames to transmit
 * tx_action > 0 ==> we have frames to transmit
 * tx_action < 0 ==> we have frames to transmit, but TXFF is not even enough
 *						 to transmit 1 frame.
 */

int r8712_pre_xmit(struct _adapter *padapter, struct xmit_frame *pxmitframe)
{
	unsigned long irqL;
	int ret;
	struct xmit_buf *pxmitbuf = NULL;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;

	r8712_do_queue_select(padapter, pattrib);
	spin_lock_irqsave(&pxmitpriv->lock, irqL);
	if (r8712_txframes_sta_ac_pending(padapter, pattrib) > 0) {
		ret = false;
		r8712_xmit_enqueue(padapter, pxmitframe);
		spin_unlock_irqrestore(&pxmitpriv->lock, irqL);
		return ret;
	}
	pxmitbuf = r8712_alloc_xmitbuf(pxmitpriv);
	if (!pxmitbuf) { /*enqueue packet*/
		ret = false;
		r8712_xmit_enqueue(padapter, pxmitframe);
		spin_unlock_irqrestore(&pxmitpriv->lock, irqL);
	} else { /*dump packet directly*/
		spin_unlock_irqrestore(&pxmitpriv->lock, irqL);
		ret = true;
		xmitframe_xmitbuf_attach(pxmitframe, pxmitbuf);
		r8712_xmit_direct(padapter, pxmitframe);
	}
	return ret;
}
