/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
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
#define _RTL8723BS_RECV_C_

#include <drv_types.h>
#include <rtw_debug.h>
#include <rtl8723b_hal.h>


static s32 initrecvbuf(struct recv_buf *precvbuf, struct adapter *padapter)
{
	INIT_LIST_HEAD(&precvbuf->list);
	spin_lock_init(&precvbuf->recvbuf_lock);

	precvbuf->adapter = padapter;

	return _SUCCESS;
}

static void update_recvframe_attrib(
	struct adapter *padapter, union recv_frame *precvframe, struct recv_stat *prxstat
)
{
	struct rx_pkt_attrib *pattrib;
	struct recv_stat report;
	PRXREPORT prxreport = (PRXREPORT)&report;

	report.rxdw0 = prxstat->rxdw0;
	report.rxdw1 = prxstat->rxdw1;
	report.rxdw2 = prxstat->rxdw2;
	report.rxdw3 = prxstat->rxdw3;
	report.rxdw4 = prxstat->rxdw4;
	report.rxdw5 = prxstat->rxdw5;

	pattrib = &precvframe->u.hdr.attrib;
	memset(pattrib, 0, sizeof(struct rx_pkt_attrib));

	/*  update rx report to recv_frame attribute */
	pattrib->pkt_rpt_type = prxreport->c2h_ind?C2H_PACKET:NORMAL_RX;
/* 	DBG_871X("%s: pkt_rpt_type =%d\n", __func__, pattrib->pkt_rpt_type); */

	if (pattrib->pkt_rpt_type == NORMAL_RX) {
		/*  Normal rx packet */
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

		pattrib->data_rate = (u8)prxreport->rx_rate;
	} else
		pattrib->pkt_len = (u16)prxreport->pktlen;
}

/*
 * Notice:
 *Before calling this function,
 *precvframe->u.hdr.rx_data should be ready!
 */
static void update_recvframe_phyinfo(
	union recv_frame *precvframe, struct phy_stat *pphy_status
)
{
	struct adapter *padapter = precvframe->u.hdr.adapter;
	struct rx_pkt_attrib *pattrib = &precvframe->u.hdr.attrib;
	struct hal_com_data *pHalData = GET_HAL_DATA(padapter);
	PODM_PHY_INFO_T pPHYInfo = (PODM_PHY_INFO_T)(&pattrib->phy_info);

	u8 *wlanhdr;
	ODM_PACKET_INFO_T pkt_info;
	u8 *sa = NULL;
	/* _irqL		irqL; */
	struct sta_priv *pstapriv;
	struct sta_info *psta;

	pkt_info.bPacketMatchBSSID = false;
	pkt_info.bPacketToSelf = false;
	pkt_info.bPacketBeacon = false;


	wlanhdr = get_recvframe_data(precvframe);

	pkt_info.bPacketMatchBSSID = ((!IsFrameTypeCtrl(wlanhdr)) &&
		!pattrib->icv_err && !pattrib->crc_err &&
		!memcmp(get_hdr_bssid(wlanhdr), get_bssid(&padapter->mlmepriv), ETH_ALEN));

	pkt_info.bPacketToSelf = pkt_info.bPacketMatchBSSID && (!memcmp(get_ra(wlanhdr), myid(&padapter->eeprompriv), ETH_ALEN));

	pkt_info.bPacketBeacon = pkt_info.bPacketMatchBSSID && (GetFrameSubType(wlanhdr) == WIFI_BEACON);

	sa = get_ta(wlanhdr);

	pkt_info.StationID = 0xFF;

	pstapriv = &padapter->stapriv;
	psta = rtw_get_stainfo(pstapriv, sa);
	if (psta) {
		pkt_info.StationID = psta->mac_id;
		/* DBG_8192C("%s ==> StationID(%d)\n", __func__, pkt_info.StationID); */
	}
	pkt_info.DataRate = pattrib->data_rate;

	/* rtl8723b_query_rx_phy_status(precvframe, pphy_status); */
	/* spin_lock_bh(&pHalData->odm_stainfo_lock); */
	ODM_PhyStatusQuery(&pHalData->odmpriv, pPHYInfo, (u8 *)pphy_status, &(pkt_info));
	if (psta)
		psta->rssi = pattrib->phy_info.RecvSignalPower;
	/* spin_unlock_bh(&pHalData->odm_stainfo_lock); */
	precvframe->u.hdr.psta = NULL;
	if (
		pkt_info.bPacketMatchBSSID &&
		(check_fwstate(&padapter->mlmepriv, WIFI_AP_STATE) == true)
	) {
		if (psta) {
			precvframe->u.hdr.psta = psta;
			rtl8723b_process_phy_info(padapter, precvframe);
		}
	} else if (pkt_info.bPacketToSelf || pkt_info.bPacketBeacon) {
		if (check_fwstate(&padapter->mlmepriv, WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE) == true)
			if (psta)
				precvframe->u.hdr.psta = psta;
		rtl8723b_process_phy_info(padapter, precvframe);
	}
}

static void rtl8723bs_c2h_packet_handler(struct adapter *padapter, u8 *pbuf, u16 length)
{
	u8 *tmpBuf = NULL;
	u8 res = false;

	if (length == 0)
		return;

	/* DBG_871X("+%s() length =%d\n", __func__, length); */

	tmpBuf = rtw_zmalloc(length);
	if (tmpBuf == NULL)
		return;

	memcpy(tmpBuf, pbuf, length);

	res = rtw_c2h_packet_wk_cmd(padapter, tmpBuf, length);

	if (res == false)
		kfree(tmpBuf);

	/* DBG_871X("-%s res(%d)\n", __func__, res); */

	return;
}

static void rtl8723bs_recv_tasklet(void *priv)
{
	struct adapter *padapter;
	struct hal_com_data *pHalData;
	struct recv_priv *precvpriv;
	struct recv_buf *precvbuf;
	union recv_frame *precvframe;
	struct rx_pkt_attrib *pattrib;
	u8 *ptr;
	u32 pkt_offset, skb_len, alloc_sz;
	_pkt *pkt_copy = NULL;
	u8 shift_sz = 0, rx_report_sz = 0;


	padapter = (struct adapter *)priv;
	pHalData = GET_HAL_DATA(padapter);
	precvpriv = &padapter->recvpriv;

	do {
		precvbuf = rtw_dequeue_recvbuf(&precvpriv->recv_buf_pending_queue);
		if (NULL == precvbuf)
			break;

		ptr = precvbuf->pdata;

		while (ptr < precvbuf->ptail) {
			precvframe = rtw_alloc_recvframe(&precvpriv->free_recv_queue);
			if (precvframe == NULL) {
				DBG_8192C("%s: no enough recv frame!\n", __func__);
				rtw_enqueue_recvbuf_to_head(precvbuf, &precvpriv->recv_buf_pending_queue);

				/*  The case of can't allocte recvframe should be temporary, */
				/*  schedule again and hope recvframe is available next time. */
				tasklet_schedule(&precvpriv->recv_tasklet);
				return;
			}

			/* rx desc parsing */
			update_recvframe_attrib(padapter, precvframe, (struct recv_stat *)ptr);

			pattrib = &precvframe->u.hdr.attrib;

			/*  fix Hardware RX data error, drop whole recv_buffer */
			if ((!(pHalData->ReceiveConfig & RCR_ACRC32)) && pattrib->crc_err) {
				DBG_8192C("%s()-%d: RX Warning! rx CRC ERROR !!\n", __func__, __LINE__);
				rtw_free_recvframe(precvframe, &precvpriv->free_recv_queue);
				break;
			}

			rx_report_sz = RXDESC_SIZE + pattrib->drvinfo_sz;
			pkt_offset = rx_report_sz + pattrib->shift_sz + pattrib->pkt_len;

			if ((ptr + pkt_offset) > precvbuf->ptail) {
				DBG_8192C("%s()-%d: : next pkt len(%p,%d) exceed ptail(%p)!\n", __func__, __LINE__, ptr, pkt_offset, precvbuf->ptail);
				rtw_free_recvframe(precvframe, &precvpriv->free_recv_queue);
				break;
			}

			if ((pattrib->crc_err) || (pattrib->icv_err)) {
				{
					DBG_8192C("%s: crc_err =%d icv_err =%d, skip!\n", __func__, pattrib->crc_err, pattrib->icv_err);
				}
				rtw_free_recvframe(precvframe, &precvpriv->free_recv_queue);
			} else {
				/* 	Modified by Albert 20101213 */
				/* 	For 8 bytes IP header alignment. */
				if (pattrib->qos)	/* 	Qos data, wireless lan header length is 26 */
					shift_sz = 6;
				else
					shift_sz = 0;

				skb_len = pattrib->pkt_len;

				/*  for first fragment packet, driver need allocate 1536+drvinfo_sz+RXDESC_SIZE to defrag packet. */
				/*  modify alloc_sz for recvive crc error packet by thomas 2011-06-02 */
				if ((pattrib->mfrag == 1) && (pattrib->frag_num == 0)) {
					if (skb_len <= 1650)
						alloc_sz = 1664;
					else
						alloc_sz = skb_len + 14;
				} else {
					alloc_sz = skb_len;
					/* 	6 is for IP header 8 bytes alignment in QoS packet case. */
					/* 	8 is for skb->data 4 bytes alignment. */
					alloc_sz += 14;
				}

				pkt_copy = rtw_skb_alloc(alloc_sz);

				if (pkt_copy) {
					pkt_copy->dev = padapter->pnetdev;
					precvframe->u.hdr.pkt = pkt_copy;
					skb_reserve(pkt_copy, 8 - ((SIZE_PTR)(pkt_copy->data) & 7));/* force pkt_copy->data at 8-byte alignment address */
					skb_reserve(pkt_copy, shift_sz);/* force ip_hdr at 8-byte alignment address according to shift_sz. */
					memcpy(pkt_copy->data, (ptr + rx_report_sz + pattrib->shift_sz), skb_len);
					precvframe->u.hdr.rx_head = pkt_copy->head;
					precvframe->u.hdr.rx_data = precvframe->u.hdr.rx_tail = pkt_copy->data;
					precvframe->u.hdr.rx_end = skb_end_pointer(pkt_copy);
				} else {
					if ((pattrib->mfrag == 1) && (pattrib->frag_num == 0)) {
						DBG_8192C("%s: alloc_skb fail, drop frag frame\n", __func__);
						rtw_free_recvframe(precvframe, &precvpriv->free_recv_queue);
						break;
					}

					precvframe->u.hdr.pkt = rtw_skb_clone(precvbuf->pskb);
					if (precvframe->u.hdr.pkt) {
						_pkt *pkt_clone = precvframe->u.hdr.pkt;

						pkt_clone->data = ptr + rx_report_sz + pattrib->shift_sz;
						skb_reset_tail_pointer(pkt_clone);
						precvframe->u.hdr.rx_head = precvframe->u.hdr.rx_data = precvframe->u.hdr.rx_tail
							= pkt_clone->data;
						precvframe->u.hdr.rx_end =	pkt_clone->data + skb_len;
					} else {
						DBG_8192C("%s: rtw_skb_clone fail\n", __func__);
						rtw_free_recvframe(precvframe, &precvpriv->free_recv_queue);
						break;
					}
				}

				recvframe_put(precvframe, skb_len);
				/* recvframe_pull(precvframe, drvinfo_sz + RXDESC_SIZE); */

				if (pHalData->ReceiveConfig & RCR_APPFCS)
					recvframe_pull_tail(precvframe, IEEE80211_FCS_LEN);

				/*  move to drv info position */
				ptr += RXDESC_SIZE;

				/*  update drv info */
				if (pHalData->ReceiveConfig & RCR_APP_BA_SSN) {
					/* rtl8723s_update_bassn(padapter, pdrvinfo); */
					ptr += 4;
				}

				if (pattrib->pkt_rpt_type == NORMAL_RX) { /* Normal rx packet */
					if (pattrib->physt)
						update_recvframe_phyinfo(precvframe, (struct phy_stat *)ptr);

					if (rtw_recv_entry(precvframe) != _SUCCESS) {
						RT_TRACE(_module_rtl871x_recv_c_, _drv_dump_, ("%s: rtw_recv_entry(precvframe) != _SUCCESS\n", __func__));
					}
				} else if (pattrib->pkt_rpt_type == C2H_PACKET) {
					C2H_EVT_HDR	C2hEvent;

					u16 len_c2h = pattrib->pkt_len;
					u8 *pbuf_c2h = precvframe->u.hdr.rx_data;
					u8 *pdata_c2h;

					C2hEvent.CmdID = pbuf_c2h[0];
					C2hEvent.CmdSeq = pbuf_c2h[1];
					C2hEvent.CmdLen = (len_c2h-2);
					pdata_c2h = pbuf_c2h+2;

					if (C2hEvent.CmdID == C2H_CCX_TX_RPT)
						CCX_FwC2HTxRpt_8723b(padapter, pdata_c2h, C2hEvent.CmdLen);
					else
						rtl8723bs_c2h_packet_handler(padapter, precvframe->u.hdr.rx_data, pattrib->pkt_len);

					rtw_free_recvframe(precvframe, &precvpriv->free_recv_queue);

				}
			}

			pkt_offset = _RND8(pkt_offset);
			precvbuf->pdata += pkt_offset;
			ptr = precvbuf->pdata;
			precvframe = NULL;
			pkt_copy = NULL;
		}

		rtw_enqueue_recvbuf(precvbuf, &precvpriv->free_recv_buf_queue);
	} while (1);

}

/*
 * Initialize recv private variable for hardware dependent
 * 1. recv buf
 * 2. recv tasklet
 *
 */
s32 rtl8723bs_init_recv_priv(struct adapter *padapter)
{
	s32 res;
	u32 i, n;
	struct recv_priv *precvpriv;
	struct recv_buf *precvbuf;


	res = _SUCCESS;
	precvpriv = &padapter->recvpriv;

	/* 3 1. init recv buffer */
	_rtw_init_queue(&precvpriv->free_recv_buf_queue);
	_rtw_init_queue(&precvpriv->recv_buf_pending_queue);

	n = NR_RECVBUFF * sizeof(struct recv_buf) + 4;
	precvpriv->pallocated_recv_buf = rtw_zmalloc(n);
	if (precvpriv->pallocated_recv_buf == NULL) {
		res = _FAIL;
		RT_TRACE(_module_rtl871x_recv_c_, _drv_err_, ("alloc recv_buf fail!\n"));
		goto exit;
	}

	precvpriv->precv_buf = (u8 *)N_BYTE_ALIGMENT((SIZE_PTR)(precvpriv->pallocated_recv_buf), 4);

	/*  init each recv buffer */
	precvbuf = (struct recv_buf *)precvpriv->precv_buf;
	for (i = 0; i < NR_RECVBUFF; i++) {
		res = initrecvbuf(precvbuf, padapter);
		if (res == _FAIL)
			break;

		if (precvbuf->pskb == NULL) {
			SIZE_PTR tmpaddr = 0;
			SIZE_PTR alignment = 0;

			precvbuf->pskb = rtw_skb_alloc(MAX_RECVBUF_SZ + RECVBUFF_ALIGN_SZ);

			if (precvbuf->pskb) {
				precvbuf->pskb->dev = padapter->pnetdev;

				tmpaddr = (SIZE_PTR)precvbuf->pskb->data;
				alignment = tmpaddr & (RECVBUFF_ALIGN_SZ-1);
				skb_reserve(precvbuf->pskb, (RECVBUFF_ALIGN_SZ - alignment));
			}

			if (precvbuf->pskb == NULL) {
				DBG_871X("%s: alloc_skb fail!\n", __func__);
			}
		}

		list_add_tail(&precvbuf->list, &precvpriv->free_recv_buf_queue.queue);

		precvbuf++;
	}
	precvpriv->free_recv_buf_queue_cnt = i;

	if (res == _FAIL)
		goto initbuferror;

	/* 3 2. init tasklet */
	tasklet_init(
		&precvpriv->recv_tasklet,
		(void(*)(unsigned long))rtl8723bs_recv_tasklet,
		(unsigned long)padapter
	);

	goto exit;

initbuferror:
	precvbuf = (struct recv_buf *)precvpriv->precv_buf;
	if (precvbuf) {
		n = precvpriv->free_recv_buf_queue_cnt;
		precvpriv->free_recv_buf_queue_cnt = 0;
		for (i = 0; i < n ; i++) {
			list_del_init(&precvbuf->list);
			rtw_os_recvbuf_resource_free(padapter, precvbuf);
			precvbuf++;
		}
		precvpriv->precv_buf = NULL;
	}

	if (precvpriv->pallocated_recv_buf) {
		n = NR_RECVBUFF * sizeof(struct recv_buf) + 4;
		kfree(precvpriv->pallocated_recv_buf);
		precvpriv->pallocated_recv_buf = NULL;
	}

exit:
	return res;
}

/*
 * Free recv private variable of hardware dependent
 * 1. recv buf
 * 2. recv tasklet
 *
 */
void rtl8723bs_free_recv_priv(struct adapter *padapter)
{
	u32 i, n;
	struct recv_priv *precvpriv;
	struct recv_buf *precvbuf;


	precvpriv = &padapter->recvpriv;

	/* 3 1. kill tasklet */
	tasklet_kill(&precvpriv->recv_tasklet);

	/* 3 2. free all recv buffers */
	precvbuf = (struct recv_buf *)precvpriv->precv_buf;
	if (precvbuf) {
		n = NR_RECVBUFF;
		precvpriv->free_recv_buf_queue_cnt = 0;
		for (i = 0; i < n ; i++) {
			list_del_init(&precvbuf->list);
			rtw_os_recvbuf_resource_free(padapter, precvbuf);
			precvbuf++;
		}
		precvpriv->precv_buf = NULL;
	}

	if (precvpriv->pallocated_recv_buf) {
		n = NR_RECVBUFF * sizeof(struct recv_buf) + 4;
		kfree(precvpriv->pallocated_recv_buf);
		precvpriv->pallocated_recv_buf = NULL;
	}
}
