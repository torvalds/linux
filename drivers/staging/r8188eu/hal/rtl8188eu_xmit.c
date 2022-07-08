// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#define _RTL8188E_XMIT_C_
#include "../include/osdep_service.h"
#include "../include/drv_types.h"
#include "../include/wifi.h"
#include "../include/osdep_intf.h"
#include "../include/usb_ops.h"
#include "../include/rtl8188e_hal.h"

s32	rtl8188eu_init_xmit_priv(struct adapter *adapt)
{
	struct xmit_priv	*pxmitpriv = &adapt->xmitpriv;

	tasklet_init(&pxmitpriv->xmit_tasklet,
		     rtl8188eu_xmit_tasklet,
		     (unsigned long)adapt);
	return _SUCCESS;
}

static void rtl8188eu_cal_txdesc_chksum(struct tx_desc	*ptxdesc)
{
	u16	*usptr = (u16 *)ptxdesc;
	u32 count = 16;		/*  (32 bytes / 2 bytes per XOR) => 16 times */
	u32 index;
	u16 checksum = 0;

	/* Clear first */
	ptxdesc->txdw7 &= cpu_to_le32(0xffff0000);

	for (index = 0; index < count; index++)
		checksum = checksum ^ le16_to_cpu(*(__le16 *)(usptr + index));
	ptxdesc->txdw7 |= cpu_to_le32(0x0000ffff & checksum);
}

/*  Description: In normal chip, we should send some packet to Hw which will be used by Fw */
/*			in FW LPS mode. The function is to fill the Tx descriptor of this packets, then */
/*			Fw can tell Hw to send these packet derectly. */
void rtl8188e_fill_fake_txdesc(struct adapter *adapt, u8 *desc, u32 BufferLen, u8  ispspoll, u8  is_btqosnull)
{
	struct tx_desc *ptxdesc;

	/*  Clear all status */
	ptxdesc = (struct tx_desc *)desc;
	memset(desc, 0, TXDESC_SIZE);

	/* offset 0 */
	ptxdesc->txdw0 |= cpu_to_le32(OWN | FSG | LSG); /* own, bFirstSeg, bLastSeg; */

	ptxdesc->txdw0 |= cpu_to_le32(((TXDESC_SIZE + OFFSET_SZ) << OFFSET_SHT) & 0x00ff0000); /* 32 bytes for TX Desc */

	ptxdesc->txdw0 |= cpu_to_le32(BufferLen & 0x0000ffff); /*  Buffer size + command header */

	/* offset 4 */
	ptxdesc->txdw1 |= cpu_to_le32((QSLT_MGNT << QSEL_SHT) & 0x00001f00); /*  Fixed queue of Mgnt queue */

	/* Set NAVUSEHDR to prevent Ps-poll AId filed to be changed to error vlaue by Hw. */
	if (ispspoll) {
		ptxdesc->txdw1 |= cpu_to_le32(NAVUSEHDR);
	} else {
		ptxdesc->txdw4 |= cpu_to_le32(BIT(7)); /*  Hw set sequence number */
		ptxdesc->txdw3 |= cpu_to_le32((8 << 28)); /* set bit3 to 1. Suugested by TimChen. 2009.12.29. */
	}

	if (is_btqosnull)
		ptxdesc->txdw2 |= cpu_to_le32(BIT(23)); /*  BT NULL */

	/* offset 16 */
	ptxdesc->txdw4 |= cpu_to_le32(BIT(8));/* driver uses rate */

	/*  USB interface drop packet if the checksum of descriptor isn't correct. */
	/*  Using this checksum can let hardware recovery from packet bulk out error (e.g. Cancel URC, Bulk out error.). */
	rtl8188eu_cal_txdesc_chksum(ptxdesc);
}

static void fill_txdesc_sectype(struct pkt_attrib *pattrib, struct tx_desc *ptxdesc)
{
	if ((pattrib->encrypt > 0) && !pattrib->bswenc) {
		switch (pattrib->encrypt) {
		/* SEC_TYPE : 0:NO_ENC,1:WEP40/TKIP,2:WAPI,3:AES */
		case _WEP40_:
		case _WEP104_:
			ptxdesc->txdw1 |= cpu_to_le32((0x01 << SEC_TYPE_SHT) & 0x00c00000);
			ptxdesc->txdw2 |= cpu_to_le32(0x7 << AMPDU_DENSITY_SHT);
			break;
		case _TKIP_:
		case _TKIP_WTMIC_:
			ptxdesc->txdw1 |= cpu_to_le32((0x01 << SEC_TYPE_SHT) & 0x00c00000);
			ptxdesc->txdw2 |= cpu_to_le32(0x7 << AMPDU_DENSITY_SHT);
			break;
		case _AES_:
			ptxdesc->txdw1 |= cpu_to_le32((0x03 << SEC_TYPE_SHT) & 0x00c00000);
			ptxdesc->txdw2 |= cpu_to_le32(0x7 << AMPDU_DENSITY_SHT);
			break;
		case _NO_PRIVACY_:
		default:
			break;
		}
	}
}

static void fill_txdesc_vcs(struct pkt_attrib *pattrib, __le32 *pdw)
{
	switch (pattrib->vcs_mode) {
	case RTS_CTS:
		*pdw |= cpu_to_le32(RTS_EN);
		break;
	case CTS_TO_SELF:
		*pdw |= cpu_to_le32(CTS_2_SELF);
		break;
	case NONE_VCS:
	default:
		break;
	}
	if (pattrib->vcs_mode) {
		*pdw |= cpu_to_le32(HW_RTS_EN);
		/*  Set RTS BW */
		if (pattrib->ht_en) {
			*pdw |= (pattrib->bwmode & HT_CHANNEL_WIDTH_40) ? cpu_to_le32(BIT(27)) : 0;

			if (pattrib->ch_offset == HAL_PRIME_CHNL_OFFSET_LOWER)
				*pdw |= cpu_to_le32((0x01 << 28) & 0x30000000);
			else if (pattrib->ch_offset == HAL_PRIME_CHNL_OFFSET_UPPER)
				*pdw |= cpu_to_le32((0x02 << 28) & 0x30000000);
			else if (pattrib->ch_offset == HAL_PRIME_CHNL_OFFSET_DONT_CARE)
				*pdw |= 0;
			else
				*pdw |= cpu_to_le32((0x03 << 28) & 0x30000000);
		}
	}
}

static void fill_txdesc_phy(struct pkt_attrib *pattrib, __le32 *pdw)
{
	if (pattrib->ht_en) {
		*pdw |= (pattrib->bwmode & HT_CHANNEL_WIDTH_40) ? cpu_to_le32(BIT(25)) : 0;

		if (pattrib->ch_offset == HAL_PRIME_CHNL_OFFSET_LOWER)
			*pdw |= cpu_to_le32((0x01 << DATA_SC_SHT) & 0x003f0000);
		else if (pattrib->ch_offset == HAL_PRIME_CHNL_OFFSET_UPPER)
			*pdw |= cpu_to_le32((0x02 << DATA_SC_SHT) & 0x003f0000);
		else if (pattrib->ch_offset == HAL_PRIME_CHNL_OFFSET_DONT_CARE)
			*pdw |= 0;
		else
			*pdw |= cpu_to_le32((0x03 << DATA_SC_SHT) & 0x003f0000);
	}
}

static s32 update_txdesc(struct xmit_frame *pxmitframe, u8 *pmem, s32 sz, u8 bagg_pkt)
{
	int	pull = 0;
	uint	qsel;
	u8 data_rate, pwr_status, offset;
	struct adapter		*adapt = pxmitframe->padapter;
	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	struct hal_data_8188e *haldata = &adapt->haldata;
	struct tx_desc	*ptxdesc = (struct tx_desc *)pmem;
	struct mlme_ext_priv	*pmlmeext = &adapt->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &pmlmeext->mlmext_info;

	memset(ptxdesc, 0, sizeof(struct tx_desc));

	/* 4 offset 0 */
	ptxdesc->txdw0 |= cpu_to_le32(OWN | FSG | LSG);
	ptxdesc->txdw0 |= cpu_to_le32(sz & 0x0000ffff);/* update TXPKTSIZE */

	offset = TXDESC_SIZE + OFFSET_SZ;

	ptxdesc->txdw0 |= cpu_to_le32(((offset) << OFFSET_SHT) & 0x00ff0000);/* 32 bytes for TX Desc */

	if (is_multicast_ether_addr(pattrib->ra))
		ptxdesc->txdw0 |= cpu_to_le32(BMC);

	/*  pkt_offset, unit:8 bytes padding */
	if (pxmitframe->pkt_offset > 0)
		ptxdesc->txdw1 |= cpu_to_le32((pxmitframe->pkt_offset << 26) & 0x7c000000);

	/* driver uses rate */
	ptxdesc->txdw4 |= cpu_to_le32(USERATE);/* rate control always by driver */

	if ((pxmitframe->frame_tag & 0x0f) == DATA_FRAMETAG) {
		/* offset 4 */
		ptxdesc->txdw1 |= cpu_to_le32(pattrib->mac_id & 0x3F);

		qsel = (uint)(pattrib->qsel & 0x0000001f);
		ptxdesc->txdw1 |= cpu_to_le32((qsel << QSEL_SHT) & 0x00001f00);

		ptxdesc->txdw1 |= cpu_to_le32((pattrib->raid << RATE_ID_SHT) & 0x000F0000);

		fill_txdesc_sectype(pattrib, ptxdesc);

		if (pattrib->ampdu_en) {
			ptxdesc->txdw2 |= cpu_to_le32(AGG_EN);/* AGG EN */
			ptxdesc->txdw6 = cpu_to_le32(0x6666f800);
		} else {
			ptxdesc->txdw2 |= cpu_to_le32(AGG_BK);/* AGG BK */
		}

		/* offset 8 */

		/* offset 12 */
		ptxdesc->txdw3 |= cpu_to_le32((pattrib->seqnum << SEQ_SHT) & 0x0FFF0000);

		/* offset 16 , offset 20 */
		if (pattrib->qos_en)
			ptxdesc->txdw4 |= cpu_to_le32(QOS);/* QoS */

		/* offset 20 */
		if (pxmitframe->agg_num > 1)
			ptxdesc->txdw5 |= cpu_to_le32((pxmitframe->agg_num << USB_TXAGG_NUM_SHT) & 0xFF000000);

		if ((pattrib->ether_type != 0x888e) &&
		    (pattrib->ether_type != 0x0806) &&
		    (pattrib->ether_type != 0x88b4) &&
		    (pattrib->dhcp_pkt != 1)) {
			/* Non EAP & ARP & DHCP type data packet */

			fill_txdesc_vcs(pattrib, &ptxdesc->txdw4);
			fill_txdesc_phy(pattrib, &ptxdesc->txdw4);

			ptxdesc->txdw4 |= cpu_to_le32(0x00000008);/* RTS Rate=24M */
			ptxdesc->txdw5 |= cpu_to_le32(0x0001ff00);/* DATA/RTS  Rate FB LMT */

			if (pattrib->ht_en) {
				if (ODM_RA_GetShortGI_8188E(&haldata->odmpriv, pattrib->mac_id))
					ptxdesc->txdw5 |= cpu_to_le32(SGI);/* SGI */
			}
			data_rate = ODM_RA_GetDecisionRate_8188E(&haldata->odmpriv, pattrib->mac_id);
			ptxdesc->txdw5 |= cpu_to_le32(data_rate & 0x3F);
			pwr_status = ODM_RA_GetHwPwrStatus_8188E(&haldata->odmpriv, pattrib->mac_id);
			ptxdesc->txdw4 |= cpu_to_le32((pwr_status & 0x7) << PWR_STATUS_SHT);
		} else {
			/*  EAP data packet and ARP packet and DHCP. */
			/*  Use the 1M data rate to send the EAP/ARP packet. */
			/*  This will maybe make the handshake smooth. */
			ptxdesc->txdw2 |= cpu_to_le32(AGG_BK);/* AGG BK */
			if (pmlmeinfo->preamble_mode == PREAMBLE_SHORT)
				ptxdesc->txdw4 |= cpu_to_le32(BIT(24));/*  DATA_SHORT */
			ptxdesc->txdw5 |= cpu_to_le32(MRateToHwRate(pmlmeext->tx_rate));
		}
	} else if ((pxmitframe->frame_tag & 0x0f) == MGNT_FRAMETAG) {
		/* offset 4 */
		ptxdesc->txdw1 |= cpu_to_le32(pattrib->mac_id & 0x3f);

		qsel = (uint)(pattrib->qsel & 0x0000001f);
		ptxdesc->txdw1 |= cpu_to_le32((qsel << QSEL_SHT) & 0x00001f00);

		ptxdesc->txdw1 |= cpu_to_le32((pattrib->raid << RATE_ID_SHT) & 0x000f0000);

		/* offset 8 */
		/* CCX-TXRPT ack for xmit mgmt frames. */
		if (pxmitframe->ack_report)
			ptxdesc->txdw2 |= cpu_to_le32(BIT(19));

		/* offset 12 */
		ptxdesc->txdw3 |= cpu_to_le32((pattrib->seqnum << SEQ_SHT) & 0x0FFF0000);

		/* offset 20 */
		ptxdesc->txdw5 |= cpu_to_le32(RTY_LMT_EN);/* retry limit enable */
		if (pattrib->retry_ctrl)
			ptxdesc->txdw5 |= cpu_to_le32(0x00180000);/* retry limit = 6 */
		else
			ptxdesc->txdw5 |= cpu_to_le32(0x00300000);/* retry limit = 12 */

		ptxdesc->txdw5 |= cpu_to_le32(MRateToHwRate(pmlmeext->tx_rate));
	} else if ((pxmitframe->frame_tag & 0x0f) != TXAGG_FRAMETAG) {
		/* offset 4 */
		ptxdesc->txdw1 |= cpu_to_le32((4) & 0x3f);/* CAM_ID(MAC_ID) */

		ptxdesc->txdw1 |= cpu_to_le32((6 << RATE_ID_SHT) & 0x000f0000);/* raid */

		/* offset 8 */

		/* offset 12 */
		ptxdesc->txdw3 |= cpu_to_le32((pattrib->seqnum << SEQ_SHT) & 0x0fff0000);

		/* offset 20 */
		ptxdesc->txdw5 |= cpu_to_le32(MRateToHwRate(pmlmeext->tx_rate));
	}

	/*  2009.11.05. tynli_test. Suggested by SD4 Filen for FW LPS. */
	/*  (1) The sequence number of each non-Qos frame / broadcast / multicast / */
	/*  mgnt frame should be controlled by Hw because Fw will also send null data */
	/*  which we cannot control when Fw LPS enable. */
	/*  --> default enable non-Qos data sequense number. 2010.06.23. by tynli. */
	/*  (2) Enable HW SEQ control for beacon packet, because we use Hw beacon. */
	/*  (3) Use HW Qos SEQ to control the seq num of Ext port non-Qos packets. */
	/*  2010.06.23. Added by tynli. */
	if (!pattrib->qos_en) {
		ptxdesc->txdw3 |= cpu_to_le32(EN_HWSEQ); /*  Hw set sequence number */
		ptxdesc->txdw4 |= cpu_to_le32(HW_SSN);	/*  Hw set sequence number */
	}

	ODM_SetTxAntByTxInfo_88E(&haldata->odmpriv, pmem, pattrib->mac_id);

	rtl8188eu_cal_txdesc_chksum(ptxdesc);
	return pull;
}

/* for non-agg data frame or  management frame */
static s32 rtw_dump_xframe(struct adapter *adapt, struct xmit_frame *pxmitframe)
{
	s32 ret = _SUCCESS;
	s32 inner_ret = _SUCCESS;
	int t, sz, w_sz, pull = 0;
	u8 *mem_addr;
	u32 ff_hwaddr;
	struct xmit_buf *pxmitbuf = pxmitframe->pxmitbuf;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;
	struct xmit_priv *pxmitpriv = &adapt->xmitpriv;
	struct security_priv *psecuritypriv = &adapt->securitypriv;
	if ((pxmitframe->frame_tag == DATA_FRAMETAG) &&
	    (pxmitframe->attrib.ether_type != 0x0806) &&
	    (pxmitframe->attrib.ether_type != 0x888e) &&
	    (pxmitframe->attrib.ether_type != 0x88b4) &&
	    (pxmitframe->attrib.dhcp_pkt != 1))
		rtw_issue_addbareq_cmd(adapt, pxmitframe);
	mem_addr = pxmitframe->buf_addr;

	for (t = 0; t < pattrib->nr_frags; t++) {
		if (inner_ret != _SUCCESS && ret == _SUCCESS)
			ret = _FAIL;

		if (t != (pattrib->nr_frags - 1)) {
			sz = pxmitpriv->frag_len;
			sz = sz - 4 - (psecuritypriv->sw_encrypt ? 0 : pattrib->icv_len);
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
		ff_hwaddr = rtw_get_ff_hwaddr(pxmitframe);

		inner_ret = rtw_write_port(adapt, ff_hwaddr, w_sz, (unsigned char *)pxmitbuf);

		rtw_count_tx_stats(adapt, pxmitframe, sz);

		mem_addr += w_sz;

		mem_addr = PTR_ALIGN(mem_addr, 4);
	}

	rtw_free_xmitframe(pxmitpriv, pxmitframe);

	if  (ret != _SUCCESS)
		rtw_sctx_done_err(&pxmitbuf->sctx, RTW_SCTX_DONE_UNKNOWN);

	return ret;
}

static u32 xmitframe_need_length(struct xmit_frame *pxmitframe)
{
	struct pkt_attrib *pattrib = &pxmitframe->attrib;

	u32 len = 0;

	/*  no consider fragement */
	len = pattrib->hdrlen + pattrib->iv_len +
		SNAP_SIZE + sizeof(u16) +
		pattrib->pktlen +
		((pattrib->bswenc) ? pattrib->icv_len : 0);

	if (pattrib->encrypt == _TKIP_)
		len += 8;

	return len;
}

bool rtl8188eu_xmitframe_complete(struct adapter *adapt, struct xmit_priv *pxmitpriv, struct xmit_buf *pxmitbuf)
{
	struct dvobj_priv *pdvobjpriv = adapter_to_dvobj(adapt);
	struct xmit_frame *pxmitframe = NULL;
	struct xmit_frame *pfirstframe = NULL;

	/*  aggregate variable */
	struct hw_xmit *phwxmit;
	struct sta_info *psta = NULL;
	struct tx_servq *ptxservq = NULL;
	struct list_head *xmitframe_plist = NULL, *xmitframe_phead = NULL;

	u32 pbuf;	/*  next pkt address */
	u32 pbuf_tail;	/*  last pkt tail */
	u32 len;	/*  packet length, except TXDESC_SIZE and PKT_OFFSET */

	u32 bulksize;
	u8 desc_cnt;
	u32 bulkptr;

	/*  dump frame variable */
	u32 ff_hwaddr;

	if (pdvobjpriv->pusbdev->speed == USB_SPEED_HIGH)
		bulksize = USB_HIGH_SPEED_BULK_SIZE;
	else
		bulksize = USB_FULL_SPEED_BULK_SIZE;

	/*  check xmitbuffer is ok */
	if (!pxmitbuf) {
		pxmitbuf = rtw_alloc_xmitbuf(pxmitpriv);
		if (!pxmitbuf)
			return false;
	}

	/* 3 1. pick up first frame */
	rtw_free_xmitframe(pxmitpriv, pxmitframe);

	pxmitframe = rtw_dequeue_xframe(pxmitpriv, pxmitpriv->hwxmits, pxmitpriv->hwxmit_entry);
	if (!pxmitframe) {
		/*  no more xmit frame, release xmit buffer */
		rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
		return false;
	}

	pxmitframe->pxmitbuf = pxmitbuf;
	pxmitframe->buf_addr = pxmitbuf->pbuf;
	pxmitbuf->priv_data = pxmitframe;

	pxmitframe->agg_num = 1; /*  alloc xmitframe should assign to 1. */
	pxmitframe->pkt_offset = 1; /*  first frame of aggregation, reserve offset */

	rtw_xmitframe_coalesce(adapt, pxmitframe->pkt, pxmitframe);

	/*  always return ndis_packet after rtw_xmitframe_coalesce */
	rtw_os_xmit_complete(adapt, pxmitframe);

	/* 3 2. aggregate same priority and same DA(AP or STA) frames */
	pfirstframe = pxmitframe;
	len = xmitframe_need_length(pfirstframe) + TXDESC_SIZE + (pfirstframe->pkt_offset * PACKET_OFFSET_SZ);
	pbuf_tail = len;
	pbuf = round_up(pbuf_tail, 8);

	/*  check pkt amount in one bulk */
	desc_cnt = 0;
	bulkptr = bulksize;
	if (pbuf < bulkptr) {
		desc_cnt++;
	} else {
		desc_cnt = 0;
		bulkptr = ((pbuf / bulksize) + 1) * bulksize; /*  round to next bulksize */
	}

	/*  dequeue same priority packet from station tx queue */
	psta = pfirstframe->attrib.psta;
	switch (pfirstframe->attrib.priority) {
	case 1:
	case 2:
		ptxservq = &psta->sta_xmitpriv.bk_q;
		phwxmit = pxmitpriv->hwxmits + 3;
		break;
	case 4:
	case 5:
		ptxservq = &psta->sta_xmitpriv.vi_q;
		phwxmit = pxmitpriv->hwxmits + 1;
		break;
	case 6:
	case 7:
		ptxservq = &psta->sta_xmitpriv.vo_q;
		phwxmit = pxmitpriv->hwxmits;
		break;
	case 0:
	case 3:
	default:
		ptxservq = &psta->sta_xmitpriv.be_q;
		phwxmit = pxmitpriv->hwxmits + 2;
		break;
	}
	spin_lock_bh(&pxmitpriv->lock);

	xmitframe_phead = get_list_head(&ptxservq->sta_pending);
	xmitframe_plist = xmitframe_phead->next;

	while (xmitframe_phead != xmitframe_plist) {
		pxmitframe = container_of(xmitframe_plist, struct xmit_frame, list);
		xmitframe_plist = xmitframe_plist->next;

		pxmitframe->agg_num = 0; /*  not first frame of aggregation */
		pxmitframe->pkt_offset = 0; /*  not first frame of aggregation, no need to reserve offset */

		len = xmitframe_need_length(pxmitframe) + TXDESC_SIZE + (pxmitframe->pkt_offset * PACKET_OFFSET_SZ);

		if (pbuf + len > MAX_XMITBUF_SZ) {
			pxmitframe->agg_num = 1;
			pxmitframe->pkt_offset = 1;
			break;
		}
		list_del_init(&pxmitframe->list);
		ptxservq->qcnt--;
		phwxmit->accnt--;

		pxmitframe->buf_addr = pxmitbuf->pbuf + pbuf;

		rtw_xmitframe_coalesce(adapt, pxmitframe->pkt, pxmitframe);
		/*  always return ndis_packet after rtw_xmitframe_coalesce */
		rtw_os_xmit_complete(adapt, pxmitframe);

		/*  (len - TXDESC_SIZE) == pxmitframe->attrib.last_txcmdsz */
		update_txdesc(pxmitframe, pxmitframe->buf_addr, pxmitframe->attrib.last_txcmdsz, true);

		/*  don't need xmitframe any more */
		rtw_free_xmitframe(pxmitpriv, pxmitframe);

		/*  handle pointer and stop condition */
		pbuf_tail = pbuf + len;
		pbuf = round_up(pbuf_tail, 8);

		pfirstframe->agg_num++;
		if (MAX_TX_AGG_PACKET_NUMBER == pfirstframe->agg_num)
			break;

		if (pbuf < bulkptr) {
			desc_cnt++;
			if (desc_cnt == USB_TXAGG_DESC_NUM)
				break;
		} else {
			desc_cnt = 0;
			bulkptr = ((pbuf / bulksize) + 1) * bulksize;
		}
	} /* end while (aggregate same priority and same DA(AP or STA) frames) */

	if (list_empty(&ptxservq->sta_pending.queue))
		list_del_init(&ptxservq->tx_pending);

	spin_unlock_bh(&pxmitpriv->lock);
	if ((pfirstframe->attrib.ether_type != 0x0806) &&
	    (pfirstframe->attrib.ether_type != 0x888e) &&
	    (pfirstframe->attrib.ether_type != 0x88b4) &&
	    (pfirstframe->attrib.dhcp_pkt != 1))
		rtw_issue_addbareq_cmd(adapt, pfirstframe);
	/* 3 3. update first frame txdesc */
	if ((pbuf_tail % bulksize) == 0) {
		/*  remove pkt_offset */
		pbuf_tail -= PACKET_OFFSET_SZ;
		pfirstframe->buf_addr += PACKET_OFFSET_SZ;
		pfirstframe->pkt_offset--;
	}

	update_txdesc(pfirstframe, pfirstframe->buf_addr, pfirstframe->attrib.last_txcmdsz, true);

	/* 3 4. write xmit buffer to USB FIFO */
	ff_hwaddr = rtw_get_ff_hwaddr(pfirstframe);
	rtw_write_port(adapt, ff_hwaddr, pbuf_tail, (u8 *)pxmitbuf);

	/* 3 5. update statisitc */
	pbuf_tail -= (pfirstframe->agg_num * TXDESC_SIZE);
	pbuf_tail -= (pfirstframe->pkt_offset * PACKET_OFFSET_SZ);

	rtw_count_tx_stats(adapt, pfirstframe, pbuf_tail);

	rtw_free_xmitframe(pxmitpriv, pfirstframe);

	return true;
}

static s32 xmitframe_direct(struct adapter *adapt, struct xmit_frame *pxmitframe)
{
	s32 res = _SUCCESS;

	res = rtw_xmitframe_coalesce(adapt, pxmitframe->pkt, pxmitframe);
	if (res == _SUCCESS)
		rtw_dump_xframe(adapt, pxmitframe);

	return res;
}

/*
 * Return
 *	true	dump packet directly
 *	false	enqueue packet
 */
static s32 pre_xmitframe(struct adapter *adapt, struct xmit_frame *pxmitframe)
{
	s32 res;
	struct xmit_buf *pxmitbuf = NULL;
	struct xmit_priv *pxmitpriv = &adapt->xmitpriv;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;
	struct mlme_priv *pmlmepriv = &adapt->mlmepriv;

	spin_lock_bh(&pxmitpriv->lock);

	if (rtw_txframes_sta_ac_pending(adapt, pattrib) > 0)
		goto enqueue;

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY | _FW_UNDER_LINKING))
		goto enqueue;

	pxmitbuf = rtw_alloc_xmitbuf(pxmitpriv);
	if (!pxmitbuf)
		goto enqueue;

	spin_unlock_bh(&pxmitpriv->lock);

	pxmitframe->pxmitbuf = pxmitbuf;
	pxmitframe->buf_addr = pxmitbuf->pbuf;
	pxmitbuf->priv_data = pxmitframe;

	if (xmitframe_direct(adapt, pxmitframe) != _SUCCESS) {
		rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
		rtw_free_xmitframe(pxmitpriv, pxmitframe);
	}

	return true;

enqueue:
	res = rtw_xmitframe_enqueue(adapt, pxmitframe);
	spin_unlock_bh(&pxmitpriv->lock);

	if (res != _SUCCESS) {
		rtw_free_xmitframe(pxmitpriv, pxmitframe);

		/*  Trick, make the statistics correct */
		pxmitpriv->tx_pkts--;
		pxmitpriv->tx_drop++;
		return true;
	}

	return false;
}

s32 rtl8188eu_mgnt_xmit(struct adapter *adapt, struct xmit_frame *pmgntframe)
{
	return rtw_dump_xframe(adapt, pmgntframe);
}

/*
 * Return
 *	true	dump packet directly ok
 *	false	temporary can't transmit packets to hardware
 */
s32 rtl8188eu_hal_xmit(struct adapter *adapt, struct xmit_frame *pxmitframe)
{
	return pre_xmitframe(adapt, pxmitframe);
}
