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
 ******************************************************************************/
#define _RTL8192CU_RECV_C_
#include <osdep_service.h>
#include <drv_types.h>
#include <recv_osdep.h>
#include <mlme_osdep.h>
#include <linux/ip.h>
#include <linux/if_ether.h>
#include <ethernet.h>
#include <usb_ops.h>
#include <wifi.h>
#include <rtl8723a_hal.h>

void rtl8723au_init_recvbuf(struct rtw_adapter *padapter,
			    struct recv_buf *precvbuf)
{
}

int rtl8723au_init_recv_priv(struct rtw_adapter *padapter)
{
	struct recv_priv *precvpriv = &padapter->recvpriv;
	int i, size, res = _SUCCESS;
	struct recv_buf *precvbuf;
	unsigned long tmpaddr;
	unsigned long alignment;
	struct sk_buff *pskb;

	tasklet_init(&precvpriv->recv_tasklet,
		     (void(*)(unsigned long))rtl8723au_recv_tasklet,
		     (unsigned long)padapter);

	precvpriv->int_in_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!precvpriv->int_in_urb)
		DBG_8723A("alloc_urb for interrupt in endpoint fail !!!!\n");
	precvpriv->int_in_buf = kzalloc(USB_INTR_CONTENT_LENGTH, GFP_KERNEL);
	if (!precvpriv->int_in_buf)
		DBG_8723A("alloc_mem for interrupt in endpoint fail !!!!\n");

	/* init recv_buf */
	_rtw_init_queue23a(&precvpriv->free_recv_buf_queue);

	size = NR_RECVBUFF * sizeof(struct recv_buf);
	precvpriv->precv_buf = kzalloc(size, GFP_KERNEL);
	if (!precvpriv->precv_buf) {
		res = _FAIL;
		RT_TRACE(_module_rtl871x_recv_c_, _drv_err_,
			 ("alloc recv_buf fail!\n"));
		goto exit;
	}

	precvbuf = (struct recv_buf *)precvpriv->precv_buf;

	for (i = 0; i < NR_RECVBUFF; i++) {
		INIT_LIST_HEAD(&precvbuf->list);

		res = rtw_os_recvbuf_resource_alloc23a(padapter, precvbuf);
		if (res == _FAIL)
			break;

		precvbuf->adapter = padapter;

		precvbuf++;
	}

	precvpriv->free_recv_buf_queue_cnt = NR_RECVBUFF;

	skb_queue_head_init(&precvpriv->rx_skb_queue);
	skb_queue_head_init(&precvpriv->free_recv_skb_queue);

	for (i = 0; i < NR_PREALLOC_RECV_SKB; i++) {
		size = MAX_RECVBUF_SZ + RECVBUFF_ALIGN_SZ;
		pskb = __netdev_alloc_skb(padapter->pnetdev, size, GFP_KERNEL);

		if (pskb) {
			pskb->dev = padapter->pnetdev;

			tmpaddr = (unsigned long)pskb->data;
			alignment = tmpaddr & (RECVBUFF_ALIGN_SZ-1);
			skb_reserve(pskb, (RECVBUFF_ALIGN_SZ - alignment));

			skb_queue_tail(&precvpriv->free_recv_skb_queue, pskb);
		}

		pskb = NULL;
	}

exit:
	return res;
}

void rtl8723au_free_recv_priv(struct rtw_adapter *padapter)
{
	int	i;
	struct recv_buf	*precvbuf;
	struct recv_priv *precvpriv = &padapter->recvpriv;

	precvbuf = (struct recv_buf *)precvpriv->precv_buf;

	for (i = 0; i < NR_RECVBUFF; i++) {
		rtw_os_recvbuf_resource_free23a(padapter, precvbuf);
		precvbuf++;
	}

	kfree(precvpriv->precv_buf);

	usb_free_urb(precvpriv->int_in_urb);
	kfree(precvpriv->int_in_buf);

	if (skb_queue_len(&precvpriv->rx_skb_queue))
		DBG_8723A(KERN_WARNING "rx_skb_queue not empty\n");

	skb_queue_purge(&precvpriv->rx_skb_queue);

	if (skb_queue_len(&precvpriv->free_recv_skb_queue)) {
		DBG_8723A(KERN_WARNING "free_recv_skb_queue not empty, %d\n",
			  skb_queue_len(&precvpriv->free_recv_skb_queue));
	}

	skb_queue_purge(&precvpriv->free_recv_skb_queue);
}

void update_recvframe_attrib(struct recv_frame *precvframe,
			     struct recv_stat *prxstat)
{
	struct rx_pkt_attrib *pattrib;
	struct recv_stat report;
	struct rxreport_8723a *prxreport;

	report.rxdw0 = le32_to_cpu(prxstat->rxdw0);
	report.rxdw1 = le32_to_cpu(prxstat->rxdw1);
	report.rxdw2 = le32_to_cpu(prxstat->rxdw2);
	report.rxdw3 = le32_to_cpu(prxstat->rxdw3);
	report.rxdw4 = le32_to_cpu(prxstat->rxdw4);
	report.rxdw5 = le32_to_cpu(prxstat->rxdw5);

	prxreport = (struct rxreport_8723a *)&report;

	pattrib = &precvframe->attrib;
	memset(pattrib, 0, sizeof(struct rx_pkt_attrib));

	/*  update rx report to recv_frame attribute */
	pattrib->pkt_len = (u16)prxreport->pktlen;
	pattrib->drvinfo_sz = (u8)(prxreport->drvinfosize << 3);
	pattrib->physt = (u8)prxreport->physt;

	pattrib->crc_err = (u8)prxreport->crc32;
	pattrib->icv_err = (u8)prxreport->icverr;

	pattrib->bdecrypted = (u8)(prxreport->swdec ? 0 : 1);
	pattrib->encrypt = (u8)prxreport->security;

	pattrib->qos = (u8)prxreport->qos;
	pattrib->priority = (u8)prxreport->tid;

	pattrib->amsdu = (u8)prxreport->amsdu;

	pattrib->seq_num = (u16)prxreport->seq;
	pattrib->frag_num = (u8)prxreport->frag;
	pattrib->mfrag = (u8)prxreport->mf;
	pattrib->mdata = (u8)prxreport->md;

	pattrib->mcs_rate = (u8)prxreport->rxmcs;
	pattrib->rxht = (u8)prxreport->rxht;
}

void update_recvframe_phyinfo(struct recv_frame *precvframe,
			      struct phy_stat *pphy_status)
{
	struct rtw_adapter *padapter = precvframe->adapter;
	struct rx_pkt_attrib *pattrib = &precvframe->attrib;
	struct hal_data_8723a *pHalData = GET_HAL_DATA(padapter);
	struct odm_phy_info *pPHYInfo = (struct odm_phy_info *)(&pattrib->phy_info);
	struct odm_packet_info pkt_info;
	u8 *sa = NULL, *da;
	struct sta_priv *pstapriv;
	struct sta_info *psta;
	struct sk_buff *skb = precvframe->pkt;
	struct ieee80211_hdr *hdr = (struct ieee80211_hdr *) skb->data;
	u8 *wlanhdr = skb->data;

	pkt_info.bPacketMatchBSSID = false;
	pkt_info.bPacketToSelf = false;
	pkt_info.bPacketBeacon = false;

	pkt_info.bPacketMatchBSSID =
		(!ieee80211_is_ctl(hdr->frame_control) &&
		 !pattrib->icv_err &&
		 !pattrib->crc_err &&
		 !memcmp(get_hdr_bssid(wlanhdr),
			 get_bssid(&padapter->mlmepriv), ETH_ALEN));

	da = ieee80211_get_DA(hdr);
	pkt_info.bPacketToSelf = pkt_info.bPacketMatchBSSID &&
		(!memcmp(da, myid(&padapter->eeprompriv), ETH_ALEN));

	pkt_info.bPacketBeacon = pkt_info.bPacketMatchBSSID &&
		ieee80211_is_beacon(hdr->frame_control);

	pkt_info.StationID = 0xFF;
	if (pkt_info.bPacketBeacon) {
		if (check_fwstate(&padapter->mlmepriv, WIFI_STATION_STATE) == true)
			sa = padapter->mlmepriv.cur_network.network.MacAddress;
		/* to do Ad-hoc */
	} else {
		sa = ieee80211_get_SA(hdr);
	}

	pstapriv = &padapter->stapriv;
	psta = rtw_get_stainfo23a(pstapriv, sa);
	if (psta) {
		pkt_info.StationID = psta->mac_id;
		/* printk("%s ==> StationID(%d)\n", __FUNCTION__, pkt_info.StationID); */
	}
	pkt_info.Rate = pattrib->mcs_rate;

	ODM_PhyStatusQuery23a(&pHalData->odmpriv, pPHYInfo,
			   (u8 *)pphy_status, &pkt_info);
	precvframe->psta = NULL;
	if (pkt_info.bPacketMatchBSSID &&
	    (check_fwstate(&padapter->mlmepriv, WIFI_AP_STATE) == true)) {
		if (psta) {
			precvframe->psta = psta;
			rtl8723a_process_phy_info(padapter, precvframe);
		}
	} else if (pkt_info.bPacketToSelf || pkt_info.bPacketBeacon) {
		if (check_fwstate(&padapter->mlmepriv,
				  WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE) ==
		    true) {
			if (psta)
				precvframe->psta = psta;
		}
		rtl8723a_process_phy_info(padapter, precvframe);
	}
}
