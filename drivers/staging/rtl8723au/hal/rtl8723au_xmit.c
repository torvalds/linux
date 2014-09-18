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
#define _RTL8192C_XMIT_C_
#include <osdep_service.h>
#include <drv_types.h>
#include <wifi.h>
#include <osdep_intf.h>
#include <usb_ops.h>
/* include <rtl8192c_hal.h> */
#include <rtl8723a_hal.h>

static void do_queue_select(struct rtw_adapter	*padapter, struct pkt_attrib *pattrib)
{
	u8 qsel;

	qsel = pattrib->priority;
	RT_TRACE(_module_rtl871x_xmit_c_, _drv_info_,
		 ("### do_queue_select priority =%d , qsel = %d\n",
		  pattrib->priority, qsel));

	pattrib->qsel = qsel;
}

static int urb_zero_packet_chk(struct rtw_adapter *padapter, int sz)
{
	int blnSetTxDescOffset;
	struct dvobj_priv	*pdvobj = adapter_to_dvobj(padapter);

	if (pdvobj->ishighspeed) {
		if (((sz + TXDESC_SIZE) % 512) == 0)
			blnSetTxDescOffset = 1;
		else
			blnSetTxDescOffset = 0;
	} else {
		if (((sz + TXDESC_SIZE) % 64) == 0)
			blnSetTxDescOffset = 1;
		else
			blnSetTxDescOffset = 0;
	}
	return blnSetTxDescOffset;
}

static void rtl8192cu_cal_txdesc_chksum(struct tx_desc	*ptxdesc)
{
		u16	*usPtr = (u16 *)ptxdesc;
		u32 count = 16;		/*  (32 bytes / 2 bytes per XOR) => 16 times */
		u32 index;
		u16 checksum = 0;

		/* Clear first */
		ptxdesc->txdw7 &= cpu_to_le32(0xffff0000);

		for (index = 0 ; index < count ; index++)
			checksum = checksum ^ le16_to_cpu(*(usPtr + index));

		ptxdesc->txdw7 |= cpu_to_le32(0x0000ffff&checksum);
}

static void fill_txdesc_sectype(struct pkt_attrib *pattrib, struct tx_desc *ptxdesc)
{
	if ((pattrib->encrypt > 0) && !pattrib->bswenc) {
		switch (pattrib->encrypt) {
		/* SEC_TYPE */
		case WLAN_CIPHER_SUITE_WEP40:
		case WLAN_CIPHER_SUITE_WEP104:
			ptxdesc->txdw1 |= cpu_to_le32((0x01<<22)&0x00c00000);
			break;
		case WLAN_CIPHER_SUITE_TKIP:
			/* ptxdesc->txdw1 |= cpu_to_le32((0x02<<22)&0x00c00000); */
			ptxdesc->txdw1 |= cpu_to_le32((0x01<<22)&0x00c00000);
			break;
		case WLAN_CIPHER_SUITE_CCMP:
			ptxdesc->txdw1 |= cpu_to_le32((0x03<<22)&0x00c00000);
			break;
		case 0:
		default:
			break;
		}
	}
}

static void fill_txdesc_vcs(struct pkt_attrib *pattrib, u32 *pdw)
{
	/* DBG_8723A("cvs_mode =%d\n", pattrib->vcs_mode); */

	switch (pattrib->vcs_mode) {
	case RTS_CTS:
		*pdw |= cpu_to_le32(BIT(12));
		break;
	case CTS_TO_SELF:
		*pdw |= cpu_to_le32(BIT(11));
		break;
	case NONE_VCS:
	default:
		break;
	}

	if (pattrib->vcs_mode) {
		*pdw |= cpu_to_le32(BIT(13));

		/*  Set RTS BW */
		if (pattrib->ht_en) {
			*pdw |= (pattrib->bwmode&HT_CHANNEL_WIDTH_40) ?	cpu_to_le32(BIT(27)) : 0;

			if (pattrib->ch_offset == HAL_PRIME_CHNL_OFFSET_LOWER)
				*pdw |= cpu_to_le32((0x01<<28)&0x30000000);
			else if (pattrib->ch_offset == HAL_PRIME_CHNL_OFFSET_UPPER)
				*pdw |= cpu_to_le32((0x02<<28)&0x30000000);
			else if (pattrib->ch_offset == HAL_PRIME_CHNL_OFFSET_DONT_CARE)
				*pdw |= 0;
			else
				*pdw |= cpu_to_le32((0x03<<28)&0x30000000);
		}
	}
}

static void fill_txdesc_phy(struct pkt_attrib *pattrib, u32 *pdw)
{
	if (pattrib->ht_en) {
		*pdw |= (pattrib->bwmode&HT_CHANNEL_WIDTH_40) ? cpu_to_le32(BIT(25)) : 0;

		if (pattrib->ch_offset == HAL_PRIME_CHNL_OFFSET_LOWER)
			*pdw |= cpu_to_le32((0x01<<20)&0x003f0000);
		else if (pattrib->ch_offset == HAL_PRIME_CHNL_OFFSET_UPPER)
			*pdw |= cpu_to_le32((0x02<<20)&0x003f0000);
		else if (pattrib->ch_offset == HAL_PRIME_CHNL_OFFSET_DONT_CARE)
			*pdw |= 0;
		else
			*pdw |= cpu_to_le32((0x03<<20)&0x003f0000);
	}
}

static s32 update_txdesc(struct xmit_frame *pxmitframe, u8 *pmem, s32 sz, u8 bagg_pkt)
{
	int	pull = 0;
	uint	qsel;
	struct rtw_adapter	*padapter = pxmitframe->padapter;
	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	struct hal_data_8723a	*pHalData = GET_HAL_DATA(padapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	struct tx_desc	*ptxdesc = (struct tx_desc *)pmem;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &pmlmeext->mlmext_info;
	int	bmcst = is_multicast_ether_addr(pattrib->ra);

	if ((!bagg_pkt) && (urb_zero_packet_chk(padapter, sz) == 0)) {
		ptxdesc = (struct tx_desc *)(pmem+PACKET_OFFSET_SZ);
		pull = 1;
		pxmitframe->pkt_offset--;
	}

	memset(ptxdesc, 0, sizeof(struct tx_desc));

	if ((pxmitframe->frame_tag&0x0f) == DATA_FRAMETAG) {
		/* offset 4 */
		ptxdesc->txdw1 |= cpu_to_le32(pattrib->mac_id&0x1f);

		qsel = (uint)(pattrib->qsel & 0x0000001f);
		ptxdesc->txdw1 |= cpu_to_le32((qsel << QSEL_SHT) & 0x00001f00);

		ptxdesc->txdw1 |= cpu_to_le32((pattrib->raid<<16) & 0x000f0000);

		fill_txdesc_sectype(pattrib, ptxdesc);

		if (pattrib->ampdu_en)
			ptxdesc->txdw1 |= cpu_to_le32(BIT(5));/* AGG EN */
		else
			ptxdesc->txdw1 |= cpu_to_le32(BIT(6));/* AGG BK */

		/* offset 8 */

		/* offset 12 */
		ptxdesc->txdw3 |= cpu_to_le32((pattrib->seqnum<<16)&0xffff0000);

		/* offset 16 , offset 20 */
		if (pattrib->qos_en)
			ptxdesc->txdw4 |= cpu_to_le32(BIT(6));/* QoS */

		if ((pattrib->ether_type != 0x888e) &&
		    (pattrib->ether_type != 0x0806) &&
		    (pattrib->dhcp_pkt != 1)) {
			/* Non EAP & ARP & DHCP type data packet */

			fill_txdesc_vcs(pattrib, &ptxdesc->txdw4);
			fill_txdesc_phy(pattrib, &ptxdesc->txdw4);

			ptxdesc->txdw4 |= cpu_to_le32(0x00000008);/* RTS Rate = 24M */
			ptxdesc->txdw5 |= cpu_to_le32(0x0001ff00);/*  */

			/* use REG_INIDATA_RATE_SEL value */
			ptxdesc->txdw5 |= cpu_to_le32(pdmpriv->INIDATA_RATE[pattrib->mac_id]);
		} else {
			/*  EAP data packet and ARP packet. */
			/*  Use the 1M data rate to send the EAP/ARP packet. */
			/*  This will maybe make the handshake smooth. */

			ptxdesc->txdw1 |= cpu_to_le32(BIT(6));/* AGG BK */

			ptxdesc->txdw4 |= cpu_to_le32(BIT(8));/* driver uses rate */

			if (pmlmeinfo->preamble_mode == PREAMBLE_SHORT)
				ptxdesc->txdw4 |= cpu_to_le32(BIT(24));/*  DATA_SHORT */

			ptxdesc->txdw5 |= cpu_to_le32(MRateToHwRate23a(pmlmeext->tx_rate));
		}
	} else if ((pxmitframe->frame_tag&0x0f) == MGNT_FRAMETAG) {
		/* offset 4 */
		ptxdesc->txdw1 |= cpu_to_le32(pattrib->mac_id&0x1f);

		qsel = (uint)(pattrib->qsel&0x0000001f);
		ptxdesc->txdw1 |= cpu_to_le32((qsel<<QSEL_SHT)&0x00001f00);

		ptxdesc->txdw1 |= cpu_to_le32((pattrib->raid<<16) & 0x000f0000);

		/* offset 8 */
		/* CCX-TXRPT ack for xmit mgmt frames. */
		if (pxmitframe->ack_report)
			ptxdesc->txdw2 |= cpu_to_le32(BIT(19));

		/* offset 12 */
		ptxdesc->txdw3 |= cpu_to_le32((pattrib->seqnum<<16)&0xffff0000);

		/* offset 16 */
		ptxdesc->txdw4 |= cpu_to_le32(BIT(8));/* driver uses rate */

		/* offset 20 */
		ptxdesc->txdw5 |= cpu_to_le32(BIT(17));/* retry limit enable */
		ptxdesc->txdw5 |= cpu_to_le32(0x00180000);/* retry limit = 6 */

		ptxdesc->txdw5 |= cpu_to_le32(MRateToHwRate23a(pmlmeext->tx_rate));
	} else if ((pxmitframe->frame_tag&0x0f) == TXAGG_FRAMETAG) {
		DBG_8723A("pxmitframe->frame_tag == TXAGG_FRAMETAG\n");
	} else {
		DBG_8723A("pxmitframe->frame_tag = %d\n", pxmitframe->frame_tag);

		/* offset 4 */
		ptxdesc->txdw1 |= cpu_to_le32((4)&0x1f);/* CAM_ID(MAC_ID) */

		ptxdesc->txdw1 |= cpu_to_le32((6<<16) & 0x000f0000);/* raid */

		/* offset 8 */

		/* offset 12 */
		ptxdesc->txdw3 |= cpu_to_le32((pattrib->seqnum<<16)&0xffff0000);

		/* offset 16 */
		ptxdesc->txdw4 |= cpu_to_le32(BIT(8));/* driver uses rate */

		/* offset 20 */
		ptxdesc->txdw5 |= cpu_to_le32(MRateToHwRate23a(pmlmeext->tx_rate));
	}

	/*  (1) The sequence number of each non-Qos frame / broadcast / multicast / */
	/*  mgnt frame should be controled by Hw because Fw will also send null data */
	/*  which we cannot control when Fw LPS enable. */
	/*  --> default enable non-Qos data sequense number. 2010.06.23. by tynli. */
	/*  (2) Enable HW SEQ control for beacon packet, because we use Hw beacon. */
	/*  (3) Use HW Qos SEQ to control the seq num of Ext port non-Qos packets. */
	if (!pattrib->qos_en) {
		/*  Hw set sequence number */
		ptxdesc->txdw4 |= cpu_to_le32(BIT(7));
		/* set bit3 to 1. */
		ptxdesc->txdw3 |= cpu_to_le32((8 << 28));
	}

	/* offset 0 */
	ptxdesc->txdw0 |= cpu_to_le32(sz&0x0000ffff);
	ptxdesc->txdw0 |= cpu_to_le32(OWN | FSG | LSG);
	ptxdesc->txdw0 |= cpu_to_le32(((TXDESC_SIZE+OFFSET_SZ)<<OFFSET_SHT)&0x00ff0000);/* 32 bytes for TX Desc */

	if (bmcst)
		ptxdesc->txdw0 |= cpu_to_le32(BIT(24));

	RT_TRACE(_module_rtl871x_xmit_c_, _drv_info_, ("offset0-txdesc = 0x%x\n", ptxdesc->txdw0));

	/* offset 4 */
	/*  pkt_offset, unit:8 bytes padding */
	if (pxmitframe->pkt_offset > 0)
		ptxdesc->txdw1 |= cpu_to_le32((pxmitframe->pkt_offset << 26) & 0x7c000000);

	rtl8192cu_cal_txdesc_chksum(ptxdesc);
	return pull;
}

static int rtw_dump_xframe(struct rtw_adapter *padapter,
			   struct xmit_frame *pxmitframe)
{
	int ret = _SUCCESS;
	int inner_ret = _SUCCESS;
	int t, sz, w_sz, pull = 0;
	u8 *mem_addr;
	u32 ff_hwaddr;
	struct xmit_buf *pxmitbuf = pxmitframe->pxmitbuf;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	if ((pxmitframe->frame_tag == DATA_FRAMETAG) &&
	    (pxmitframe->attrib.ether_type != 0x0806) &&
	    (pxmitframe->attrib.ether_type != 0x888e) &&
	    (pxmitframe->attrib.dhcp_pkt != 1))
		rtw_issue_addbareq_cmd23a(padapter, pxmitframe);

	mem_addr = pxmitframe->buf_addr;

	RT_TRACE(_module_rtl871x_xmit_c_, _drv_info_, ("rtw_dump_xframe()\n"));

	for (t = 0; t < pattrib->nr_frags; t++) {
		if (inner_ret != _SUCCESS && ret == _SUCCESS)
			ret = _FAIL;

		if (t != (pattrib->nr_frags - 1)) {
			RT_TRACE(_module_rtl871x_xmit_c_, _drv_err_,
				 ("pattrib->nr_frags =%d\n", pattrib->nr_frags));

			sz = pxmitpriv->frag_len;
			sz = sz - 4 - pattrib->icv_len;
		} else {
			/* no frag */
			sz = pattrib->last_txcmdsz;
		}

		pull = update_txdesc(pxmitframe, mem_addr, sz, false);

		if (pull) {
			mem_addr += PACKET_OFFSET_SZ; /* pull txdesc head */

			pxmitframe->buf_addr = mem_addr;

			w_sz = sz + TXDESC_SIZE;
		} else {
			w_sz = sz + TXDESC_SIZE + PACKET_OFFSET_SZ;
		}

		ff_hwaddr = rtw_get_ff_hwaddr23a(pxmitframe);
		inner_ret = rtl8723au_write_port(padapter, ff_hwaddr,
						 w_sz, pxmitbuf);
		rtw_count_tx_stats23a(padapter, pxmitframe, sz);

		RT_TRACE(_module_rtl871x_xmit_c_, _drv_info_,
			 ("rtw_write_port, w_sz =%d\n", w_sz));

		mem_addr += w_sz;

		mem_addr = PTR_ALIGN(mem_addr, 4);
	}

	rtw_free_xmitframe23a(pxmitpriv, pxmitframe);

	if  (ret != _SUCCESS)
		rtw23a_sctx_done_err(&pxmitbuf->sctx, RTW_SCTX_DONE_UNKNOWN);

	return ret;
}

bool rtl8723au_xmitframe_complete(struct rtw_adapter *padapter,
				  struct xmit_priv *pxmitpriv,
				  struct xmit_buf *pxmitbuf)
{
	struct hw_xmit *phwxmits;
	struct xmit_frame *pxmitframe;
	int hwentry;
	int res = _SUCCESS, xcnt = 0;

	phwxmits = pxmitpriv->hwxmits;
	hwentry = pxmitpriv->hwxmit_entry;

	RT_TRACE(_module_rtl871x_xmit_c_, _drv_info_, ("xmitframe_complete()\n"));

	if (pxmitbuf == NULL) {
		pxmitbuf = rtw_alloc_xmitbuf23a(pxmitpriv);
		if (!pxmitbuf)
			return false;
	}
	pxmitframe =  rtw_dequeue_xframe23a(pxmitpriv, phwxmits, hwentry);

	if (pxmitframe) {
		pxmitframe->pxmitbuf = pxmitbuf;

		pxmitframe->buf_addr = pxmitbuf->pbuf;

		pxmitbuf->priv_data = pxmitframe;

		if ((pxmitframe->frame_tag&0x0f) == DATA_FRAMETAG) {
			if (pxmitframe->attrib.priority <= 15)/* TID0~15 */
				res = rtw_xmitframe_coalesce23a(padapter, pxmitframe->pkt, pxmitframe);

			rtw_os_xmit_complete23a(padapter, pxmitframe);/* always return ndis_packet after rtw_xmitframe_coalesce23a */
		}

		RT_TRACE(_module_rtl871x_xmit_c_, _drv_info_, ("xmitframe_complete(): rtw_dump_xframe\n"));

		if (res == _SUCCESS) {
			rtw_dump_xframe(padapter, pxmitframe);
		} else {
			rtw_free_xmitbuf23a(pxmitpriv, pxmitbuf);
			rtw_free_xmitframe23a(pxmitpriv, pxmitframe);
		}
		xcnt++;
	} else {
		rtw_free_xmitbuf23a(pxmitpriv, pxmitbuf);
		return false;
	}
	return true;
}

static int xmitframe_direct(struct rtw_adapter *padapter,
			    struct xmit_frame *pxmitframe)
{
	int res;

	res = rtw_xmitframe_coalesce23a(padapter, pxmitframe->pkt, pxmitframe);
	if (res == _SUCCESS)
		rtw_dump_xframe(padapter, pxmitframe);
	return res;
}

/*
 * Return
 *	true	dump packet directly
 *	false	enqueue packet
 */
bool rtl8723au_hal_xmit(struct rtw_adapter *padapter,
			struct xmit_frame *pxmitframe)
{
	int res;
	struct xmit_buf *pxmitbuf = NULL;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	do_queue_select(padapter, pattrib);
	spin_lock_bh(&pxmitpriv->lock);

#ifdef CONFIG_8723AU_AP_MODE
	if (xmitframe_enqueue_for_sleeping_sta23a(padapter, pxmitframe)) {
		struct sta_info *psta;
		struct sta_priv *pstapriv = &padapter->stapriv;

		spin_unlock_bh(&pxmitpriv->lock);

		if (pattrib->psta)
			psta = pattrib->psta;
		else
			psta = rtw_get_stainfo23a(pstapriv, pattrib->ra);

		if (psta) {
			if (psta->sleepq_len > (NR_XMITFRAME>>3))
				wakeup_sta_to_xmit23a(padapter, psta);
		}

		return false;
	}
#endif

	if (rtw_txframes_sta_ac_pending23a(padapter, pattrib) > 0)
		goto enqueue;

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY|_FW_UNDER_LINKING) == true)
		goto enqueue;

	pxmitbuf = rtw_alloc_xmitbuf23a(pxmitpriv);
	if (pxmitbuf == NULL)
		goto enqueue;

	spin_unlock_bh(&pxmitpriv->lock);

	pxmitframe->pxmitbuf = pxmitbuf;
	pxmitframe->buf_addr = pxmitbuf->pbuf;
	pxmitbuf->priv_data = pxmitframe;

	if (xmitframe_direct(padapter, pxmitframe) != _SUCCESS) {
		rtw_free_xmitbuf23a(pxmitpriv, pxmitbuf);
		rtw_free_xmitframe23a(pxmitpriv, pxmitframe);
	}
	return true;

enqueue:
	res = rtw_xmitframe_enqueue23a(padapter, pxmitframe);
	spin_unlock_bh(&pxmitpriv->lock);

	if (res != _SUCCESS) {
		RT_TRACE(_module_xmit_osdep_c_, _drv_err_,
			 ("pre_xmitframe: enqueue xmitframe fail\n"));
		rtw_free_xmitframe23a(pxmitpriv, pxmitframe);

		/*  Trick, make the statistics correct */
		pxmitpriv->tx_pkts--;
		pxmitpriv->tx_drop++;
		return true;
	}
	return false;
}

int rtl8723au_mgnt_xmit(struct rtw_adapter *padapter,
			struct xmit_frame *pmgntframe)
{
	return rtw_dump_xframe(padapter, pmgntframe);
}

int rtl8723au_hal_xmitframe_enqueue(struct rtw_adapter *padapter,
				    struct xmit_frame *pxmitframe)
{
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	int err;

	err = rtw_xmitframe_enqueue23a(padapter, pxmitframe);
	if (err != _SUCCESS) {
		rtw_free_xmitframe23a(pxmitpriv, pxmitframe);

		/*  Trick, make the statistics correct */
		pxmitpriv->tx_pkts--;
		pxmitpriv->tx_drop++;
	} else {
		tasklet_hi_schedule(&pxmitpriv->xmit_tasklet);
	}
	return err;
}
