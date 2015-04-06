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
 *
 ******************************************************************************/
#define _RTL8192C_XMIT_C_
#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <wifi.h>
#include <osdep_intf.h>
#include <usb_ops.h>
#include <rtl8192d_hal.h>
#include <rtw_tdls.h>
#include <rtw_tdls.h>

s32	rtl8192du_init_xmit_priv(struct rtw_adapter *padapter)
{
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;

	tasklet_init(&pxmitpriv->xmit_tasklet,
	     (void(*)(unsigned long))rtl8192du_xmit_tasklet,
	     (unsigned long)padapter);
	return _SUCCESS;
}

void	rtl8192du_free_xmit_priv(struct rtw_adapter *padapter)
{
}

static u32 rtw_get_ff_hwaddr(struct xmit_frame	*pxmitframe)
{
	u32 addr;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;

	switch (pattrib->qsel) {
	case 0:
	case 3:
		addr = BE_QUEUE_INX;
		break;
	case 1:
	case 2:
		addr = BK_QUEUE_INX;
		break;
	case 4:
	case 5:
		addr = VI_QUEUE_INX;
		break;
	case 6:
	case 7:
		addr = VO_QUEUE_INX;
		break;
	case 0x10:
		addr = BCN_QUEUE_INX;
		break;
	case 0x11:/* BC/MC in PS (HIQ) */
		addr = HIGH_QUEUE_INX;
		break;
	case 0x12:
		addr = MGT_QUEUE_INX;
		break;
	default:
		addr = BE_QUEUE_INX;
		break;
	}
	return addr;
}

static int urb_zero_packet_chk(struct rtw_adapter *padapter, int sz)
{
	int blnSetTxDescOffset;
	struct dvobj_priv	*pdvobj = adapter_to_dvobj(padapter);

	if (pdvobj->ishighspeed)
	{
		if (((sz + TXDESC_SIZE) % 512) == 0) {
			blnSetTxDescOffset = 1;
		} else {
			blnSetTxDescOffset = 0;
		}
	}
	else
	{
		if (((sz + TXDESC_SIZE) % 64) == 0)		{
			blnSetTxDescOffset = 1;
		} else {
			blnSetTxDescOffset = 0;
		}
	}

	return blnSetTxDescOffset;
}

void rtl8192du_cal_txdesc_chksum(struct tx_desc	*ptxdesc)
{
		u16	*usPtr = (u16*)ptxdesc;
		u32 count = 16;		/*  (32 bytes / 2 bytes per XOR) => 16 times */
		u32 index;
		u16 checksum = 0;

		/* Clear first */
		ptxdesc->txdw7 &= cpu_to_le32(0xffff0000);

		for (index = 0 ; index < count ; index++) {
			checksum = checksum ^ le16_to_cpu(*(__le16 *)(usPtr + index));
		}

		ptxdesc->txdw7 |= cpu_to_le32(0x0000ffff&checksum);
}

static void fill_txdesc_sectype(struct pkt_attrib *pattrib, struct tx_desc *ptxdesc)
{
	if ((pattrib->encrypt > 0) && !pattrib->bswenc)
	{
		switch (pattrib->encrypt)
		{
			/* SEC_TYPE */
			case _WEP40_:
			case _WEP104_:
					ptxdesc->txdw1 |= cpu_to_le32((0x01<<22)&0x00c00000);
					break;
			case _TKIP_:
			case _TKIP_WTMIC_:
					ptxdesc->txdw1 |= cpu_to_le32((0x01<<22)&0x00c00000);
					break;
			case _AES_:
					ptxdesc->txdw1 |= cpu_to_le32((0x03<<22)&0x00c00000);
					break;
			case _NO_PRIVACY_:
			default:
					break;

		}

	}
}

static void fill_txdesc_vcs(struct pkt_attrib *pattrib, __le32 *pdw)
{

	switch (pattrib->vcs_mode)
	{
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
		if (pattrib->ht_en)
		{
			*pdw |= (pattrib->bwmode&HT_CHANNEL_WIDTH_40)?	cpu_to_le32(BIT(27)):0;

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

static void fill_txdesc_phy(struct pkt_attrib *pattrib, __le32 *pdw)
{

	if (pattrib->ht_en)
	{
		*pdw |= (pattrib->bwmode&HT_CHANNEL_WIDTH_40)?	cpu_to_le32(BIT(25)):0;

		if (pattrib->ch_offset == HAL_PRIME_CHNL_OFFSET_LOWER)
			*pdw |= cpu_to_le32((0x01<<20)&0x00300000);
		else if (pattrib->ch_offset == HAL_PRIME_CHNL_OFFSET_UPPER)
			*pdw |= cpu_to_le32((0x02<<20)&0x00300000);
		else if (pattrib->ch_offset == HAL_PRIME_CHNL_OFFSET_DONT_CARE)
			*pdw |= 0;
		else
			*pdw |= cpu_to_le32((0x03<<20)&0x00300000);
	}
}

/*
	Insert Early mode 's Content,8Byte
Len1	Len0	Pkt_num
Len4	Len3	Len2

*/
static void InsertEMContent(struct xmit_frame *pxmitframe, u8 *VirtualAddress)
{
	memset(VirtualAddress, 0, 8);
	SET_EARLYMODE_PKTNUM(VirtualAddress, pxmitframe->EMPktNum);
	SET_EARLYMODE_LEN0(VirtualAddress, pxmitframe->EMPktLen[0]);
	SET_EARLYMODE_LEN1(VirtualAddress, pxmitframe->EMPktLen[1]);
	SET_EARLYMODE_LEN2_1(VirtualAddress, pxmitframe->EMPktLen[2]&0xF);
	SET_EARLYMODE_LEN2_2(VirtualAddress, pxmitframe->EMPktLen[2]>>4);
	SET_EARLYMODE_LEN3(VirtualAddress, pxmitframe->EMPktLen[3]);
	SET_EARLYMODE_LEN4(VirtualAddress, pxmitframe->EMPktLen[4]);
}

static s32 update_txdesc(struct xmit_frame *pxmitframe, u8 *pmem, s32 sz, u8 bagg_pkt)
{
	int	pull=0;
	uint	qsel;
	struct rtw_adapter				*padapter = pxmitframe->padapter;
	struct hal_data_8192du		*pHalData = GET_HAL_DATA(padapter);
	struct dm_priv		*pdmpriv = &pHalData->dmpriv;
#ifdef CONFIG_92D_AP_MODE
	struct mlme_priv		*pmlmepriv = &padapter->mlmepriv;
#endif /* CONFIG_92D_AP_MODE */
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	struct pkt_attrib		*pattrib = &pxmitframe->attrib;
	struct tx_desc		*ptxdesc = (struct tx_desc *)pmem;
	int	bmcst = IS_MCAST(pattrib->ra);

	if (padapter->registrypriv.mp_mode == 0) {
		if ((false == bagg_pkt) && (urb_zero_packet_chk(padapter, sz)==0)) {
		ptxdesc = (struct tx_desc *)(pmem+PACKET_OFFSET_SZ);
		pull = 1;
		pxmitframe->pkt_offset --;
		}
	}

	memset(ptxdesc, 0, sizeof(struct tx_desc));

	if ((pxmitframe->frame_tag&0x0f) == DATA_FRAMETAG) {
		/* offset 4 */
		ptxdesc->txdw1 |= cpu_to_le32(pattrib->mac_id&0x1f);

		qsel = (uint)(pattrib->qsel & 0x0000001f);
		ptxdesc->txdw1 |= cpu_to_le32((qsel << QSEL_SHT) & 0x00001f00);

		ptxdesc->txdw1 |= cpu_to_le32((pattrib->raid<< 16) & 0x000f0000);

		fill_txdesc_sectype(pattrib, ptxdesc);

		if (pattrib->ampdu_en==true) {
			ptxdesc->txdw1 |= cpu_to_le32(BIT(5));/* AGG EN */
			/* Insert Early Mode Content after tx desc position. */
			if ((pHalData->bEarlyModeEnable) && (true == bagg_pkt)) {
				ptxdesc->txdw0 |= cpu_to_le32(((USB_HWDESC_HEADER_LEN-8) << OFFSET_SHT) & 0x00ff0000);/* 32 bytes for TX Desc */
				if (pxmitframe->EMPktNum > 0) {
					InsertEMContent(pxmitframe, pmem+TXDESC_SIZE);
				}
			}
		}
		else
		{
			ptxdesc->txdw1 |= cpu_to_le32(BIT(6));/* AGG BK */
		}

		/* offset 8 */

		/* offset 12 */
		ptxdesc->txdw3 |= cpu_to_le32((pattrib->seqnum<<16)&0xffff0000);

		/* offset 16 , offset 20 */
		if (pattrib->qos_en)
			ptxdesc->txdw4 |= cpu_to_le32(BIT(6));/* QoS */

		if ((pattrib->ether_type != 0x888e) && (pattrib->ether_type != 0x0806) && (pattrib->dhcp_pkt != 1))
		{
		/* Non EAP & ARP & DHCP type data packet */

			fill_txdesc_vcs(pattrib, &ptxdesc->txdw4);
			fill_txdesc_phy(pattrib, &ptxdesc->txdw4);

			ptxdesc->txdw4 |= cpu_to_le32(0x00000008);/* RTS Rate=24M */
			ptxdesc->txdw5 |= cpu_to_le32(0x0001ff00);/*  */

			/* use REG_INIDATA_RATE_SEL value */
			ptxdesc->txdw5 |= cpu_to_le32(pdmpriv->INIDATA_RATE[pattrib->mac_id]);

		}
		else
		{
			/*  EAP data packet and ARP packet. */
			/*  Use the 1M data rate to send the EAP/ARP packet. */
			/*  This will maybe make the handshake smooth. */

			ptxdesc->txdw1 |= cpu_to_le32(BIT(6));/* AGG BK */

			ptxdesc->txdw4 |= cpu_to_le32(BIT(8));/* driver uses rate */

			if (pmlmeinfo->preamble_mode == PREAMBLE_SHORT)
				ptxdesc->txdw4 |= cpu_to_le32(BIT(24));/*  DATA_SHORT */

			ptxdesc->txdw5 |= cpu_to_le32(ratetohwrate(pmlmeext->tx_rate));
		}

		/* offset 24 */
#ifdef CONFIG_TCP_CSUM_OFFLOAD_TX
		if (pattrib->hw_tcp_csum == 1) {
			u8 ip_hdr_offset = 32 + pattrib->hdrlen + pattrib->iv_len + 8;
			ptxdesc->txdw7 = (1 << 31) | (ip_hdr_offset << 16);
			DBG_8192D("ptxdesc->txdw7 = %08x\n", ptxdesc->txdw7);
		}
#endif
	}
	else if ((pxmitframe->frame_tag&0x0f)== MGNT_FRAMETAG)
	{

		/* offset 4 */
		ptxdesc->txdw1 |= cpu_to_le32(pattrib->mac_id&0x1f);

		qsel = (uint)(pattrib->qsel&0x0000001f);
		ptxdesc->txdw1 |= cpu_to_le32((qsel<<QSEL_SHT)&0x00001f00);

		ptxdesc->txdw1 |= cpu_to_le32((pattrib->raid<< 16) & 0x000f0000);

		/* offset 8 */

		/* CCX-TXRPT ack for xmit mgmt frames. */
		if (pxmitframe->ack_report) {
			ptxdesc->txdw2 |= cpu_to_le32(BIT(19));
			#ifdef DBG_CCX
			DBG_8192D("%s set ccx\n", __func__);
			#endif
		}

		/* offset 12 */
		ptxdesc->txdw3 |= cpu_to_le32((pattrib->seqnum<<16)&0xffff0000);

		/* offset 16 */
		ptxdesc->txdw4 |= cpu_to_le32(BIT(8));/* driver uses rate */

		/* offset 20 */
#ifdef CONFIG_92D_AP_MODE
		if (check_fwstate(pmlmepriv, WIFI_AP_STATE) == true) {
			ptxdesc->txdw5 |= cpu_to_le32(BIT(17));/* retry limit enable */
				ptxdesc->txdw5 |= cpu_to_le32(0x00180000);/* retry limit = 6 */
		}
#endif

		ptxdesc->txdw5 |= cpu_to_le32(ratetohwrate(pmlmeext->tx_rate));
	} else if ((pxmitframe->frame_tag&0x0f) == TXAGG_FRAMETAG) {
		DBG_8192D("pxmitframe->frame_tag == TXAGG_FRAMETAG\n");
	} else {
		DBG_8192D("pxmitframe->frame_tag = %d\n", pxmitframe->frame_tag);

		/* offset 4 */
		ptxdesc->txdw1 |= cpu_to_le32((4)&0x1f);/* CAM_ID(MAC_ID) */

		ptxdesc->txdw1 |= cpu_to_le32((6<< 16) & 0x000f0000);/* raid */

		/* offset 8 */

		/* offset 12 */
		ptxdesc->txdw3 |= cpu_to_le32((pattrib->seqnum<<16)&0xffff0000);

		/* offset 16 */
		ptxdesc->txdw4 |= cpu_to_le32(BIT(8));/* driver uses rate */

		/* offset 20 */
		ptxdesc->txdw5 |= cpu_to_le32(ratetohwrate(pmlmeext->tx_rate));
	}

	/*  2009.11.05. tynli_test. Suggested by SD4 Filen for FW LPS. */
	/*  (1) The sequence number of each non-Qos frame / broadcast / multicast / */
	/*  mgnt frame should be controled by Hw because Fw will also send null data */
	/*  which we cannot control when Fw LPS enable. */
	/*  --> default enable non-Qos data sequense number. 2010.06.23. by tynli. */
	/*  (2) Enable HW SEQ control for beacon packet, because we use Hw beacon. */
	/*  (3) Use HW Qos SEQ to control the seq num of Ext port non-Qos packets. */
	/*  2010.06.23. Added by tynli. */
	if (!pattrib->qos_en)
	{
		ptxdesc->txdw4 |= cpu_to_le32(BIT(7)); /*  Hw set sequence number */
		ptxdesc->txdw3 |= cpu_to_le32((8 <<28)); /* set bit3 to 1. Suugested by TimChen. 2009.12.29. */
	}

	/* offset 0 */
	ptxdesc->txdw0 |= cpu_to_le32(sz&0x0000ffff);
	ptxdesc->txdw0 |= cpu_to_le32(OWN | FSG | LSG);
	ptxdesc->txdw0 |= cpu_to_le32(((TXDESC_SIZE+OFFSET_SZ)<<OFFSET_SHT)&0x00ff0000);/* 32 bytes for TX Desc */

	if (bmcst)
	{
		ptxdesc->txdw0 |= cpu_to_le32(BIT(24));
	}

	RT_TRACE(_module_rtl871x_xmit_c_,_drv_info_,("offset0-txdesc=0x%x\n", ptxdesc->txdw0));

	/* offset 4 */
	/*  pkt_offset, unit:8 bytes padding */
	if (pxmitframe->pkt_offset > 0)
		ptxdesc->txdw1 |= cpu_to_le32((pxmitframe->pkt_offset << 26) &
					      0x7c000000);

	if (pxmitframe->agg_num > 1)
		ptxdesc->txdw5 |= cpu_to_le32((pxmitframe->agg_num << 24) &
					      0xff000000);

	rtl8192du_cal_txdesc_chksum(ptxdesc);

	return pull;
}

s32 rtw_dump_xframe(struct rtw_adapter *padapter, struct xmit_frame *pxmitframe)
{
	s32 ret = _SUCCESS;
	s32 inner_ret = _SUCCESS;
	int t, sz, w_sz, pull=0;
	u8 *mem_addr;
	u32 ff_hwaddr;
	struct xmit_buf *pxmitbuf = pxmitframe->pxmitbuf;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct security_priv *psecuritypriv = &padapter->securitypriv;

	if ((pxmitframe->frame_tag == DATA_FRAMETAG) &&
	    (pxmitframe->attrib.ether_type != 0x0806) &&
	    (pxmitframe->attrib.ether_type != 0x888e) &&
	    (pxmitframe->attrib.dhcp_pkt != 1))
		rtw_issue_addbareq_cmd(padapter, pxmitframe);

	mem_addr = pxmitframe->buf_addr;

       RT_TRACE(_module_rtl871x_xmit_c_,_drv_info_,("rtw_dump_xframe()\n"));

	for (t = 0; t < pattrib->nr_frags; t++) {
		if (inner_ret != _SUCCESS && ret == _SUCCESS)
			ret = _FAIL;

		if (t != (pattrib->nr_frags - 1)) {
			RT_TRACE(_module_rtl871x_xmit_c_,_drv_err_,("pattrib->nr_frags=%d\n", pattrib->nr_frags));

			sz = pxmitpriv->frag_len;
			sz = sz - 4 - (psecuritypriv->sw_encrypt ? 0 : pattrib->icv_len);
		}
		else /* no frag */
		{
			sz = pattrib->last_txcmdsz;
		}

		pull = update_txdesc(pxmitframe, mem_addr, sz, false);

		if (pull)
		{
			mem_addr += PACKET_OFFSET_SZ; /* pull txdesc head */

			pxmitframe->buf_addr = mem_addr;

			w_sz = sz + TXDESC_SIZE;
		}
		else
		{
			w_sz = sz + TXDESC_SIZE + PACKET_OFFSET_SZ;
		}

		ff_hwaddr = rtw_get_ff_hwaddr(pxmitframe);

		inner_ret = rtw_write_port(padapter, ff_hwaddr, w_sz, (unsigned char*)pxmitbuf);

		rtw_count_tx_stats(padapter, pxmitframe, sz);

		RT_TRACE(_module_rtl871x_xmit_c_,_drv_info_,("rtw_write_port, w_sz=%d\n", w_sz));

		mem_addr += w_sz;

		mem_addr = (u8 *)RND4(((SIZE_PTR)(mem_addr)));

	}

	rtw_free_xmitframe(pxmitpriv, pxmitframe);

	if  (ret != _SUCCESS)
		rtw_sctx_done_err(&pxmitbuf->sctx, RTW_SCTX_DONE_UNKNOWN);

	return ret;
}

static u32 xmitframe_need_length(struct xmit_frame *pxmitframe)
{
	struct pkt_attrib *pattrib = &pxmitframe->attrib;

	u32	len = 0;

	/*  no consider fragement */
	len = pattrib->hdrlen + pattrib->iv_len +
		SNAP_SIZE + sizeof(u16) +
		pattrib->pktlen +
		((pattrib->bswenc) ? pattrib->icv_len : 0);

	if (pattrib->encrypt ==_TKIP_)
		len += 8;

	return len;
}

static void UpdateEarlyModeInfo8192D(struct rtw_adapter *padapter,
				     struct xmit_frame *pxmitframe,
				     struct tx_servq *ptxservq)
{
	u32	len;
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;
	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	struct list_head *xmitframe_plist = NULL, *xmitframe_phead = NULL;

	/* Some macaddr can't do early mode. */
	if (MacAddr_isBcst(pattrib->dst) ||IS_MCAST(pattrib->dst) || !!pattrib->qos_en)
		return;

	pxmitframe->EMPktNum = 0;

	/*  dequeue same priority packet from station tx queue */
	spin_lock_bh(&pxmitpriv->lock);

	xmitframe_phead = get_list_head(&ptxservq->sta_pending);
	xmitframe_plist = xmitframe_phead->next;
	while ((rtw_end_of_queue_search(xmitframe_phead, xmitframe_plist) == false)&&(pxmitframe->EMPktNum < 5))
	{
		pxmitframe = container_of(xmitframe_plist, struct xmit_frame, list);
		xmitframe_plist = xmitframe_plist->next;

		len = xmitframe_need_length(pxmitframe);
		pxmitframe->EMPktLen[pxmitframe->EMPktNum] = len;
		pxmitframe->EMPktNum++;
	}
	spin_unlock_bh(&pxmitpriv->lock);
}

#define IDEA_CONDITION 1	/*  check all packets before enqueue */
s32 rtl8192du_xmitframe_complete(struct rtw_adapter *padapter, struct xmit_priv *pxmitpriv, struct xmit_buf *pxmitbuf)
{
	struct hal_data_8192du	*pHalData = GET_HAL_DATA(padapter);
	struct xmit_frame *pxmitframe = NULL;
	struct xmit_frame *pfirstframe = NULL;

	/*  aggregate variable */
	struct hw_xmit	*phwxmit = pxmitpriv->hwxmits;
	struct tx_servq	*ptxservq = NULL;

	struct list_head *xmitframe_plist = NULL, *xmitframe_phead = NULL;

	u32	pbuf=0; /*  next pkt address */
	u32	pbuf_tail; /*  last pkt tail */
	u32	len=0; /* packet length, except TXDESC_SIZE and PKT_OFFSET */
	u32	aggMaxLength = MAX_XMITBUF_SZ;
	u32	bulkSize = pHalData->UsbBulkOutSize;
	u32	bulkPtr=0;
	u8	descCount=0;
	u8	ac_index;
	u8	bfirst = true;/* first aggregation xmitframe */
	u8	bulkstart = false;

	/*  dump frame variable */
	u32 ff_hwaddr;

#ifndef IDEA_CONDITION
	int res = _SUCCESS;
#endif

	RT_TRACE(_module_rtl8192c_xmit_c_, _drv_info_, ("+xmitframe_complete\n"));

	/*  check xmitbuffer is ok */
	if (pxmitbuf == NULL) {
		pxmitbuf = rtw_alloc_xmitbuf(pxmitpriv);
		if (pxmitbuf == NULL) return false;
	}

	if (pHalData->MacPhyMode92D==SINGLEMAC_SINGLEPHY)
		aggMaxLength = MAX_XMITBUF_SZ;
	else
		aggMaxLength = 0x3D00;

	do {
		/* 3 1. pick up first frame */
		if (bfirst)
		{
			pxmitframe = rtw_dequeue_xframe(pxmitpriv, pxmitpriv->hwxmits, pxmitpriv->hwxmit_entry);
			if (pxmitframe == NULL) {
				/*  no more xmit frame, release xmit buffer */
				rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
				return false;
			}

			pxmitframe->pxmitbuf = pxmitbuf;
			pxmitframe->buf_addr = pxmitbuf->pbuf;
			pxmitbuf->priv_data = pxmitframe;

			pxmitframe->pkt_offset = USB_92D_DUMMY_OFFSET; /*  first frame of aggregation, reserve 2 offset for 512 alignment and early mode */

			pfirstframe = pxmitframe;
			spin_lock_bh(&pxmitpriv->lock);
			ptxservq = rtw_get_sta_pending(padapter, pfirstframe->attrib.psta, pfirstframe->attrib.priority, (u8 *)(&ac_index));
			spin_unlock_bh(&pxmitpriv->lock);
		}
		/* 3 2. aggregate same priority and same DA(AP or STA) frames */
		else
		{
			/*  dequeue same priority packet from station tx queue */
			spin_lock_bh(&pxmitpriv->lock);

			if (_rtw_queue_empty(&ptxservq->sta_pending) == false)
			{
				xmitframe_phead = get_list_head(&ptxservq->sta_pending);
				xmitframe_plist = xmitframe_phead->next;

				pxmitframe = container_of(xmitframe_plist, struct xmit_frame, list);

				len = xmitframe_need_length(pxmitframe) + TXDESC_SIZE + ((USB_92D_DUMMY_OFFSET - 1) * PACKET_OFFSET_SZ);
				if (pbuf + _RND8(len) > aggMaxLength)
				{
					bulkstart = true;
				}
				else
				{
					list_del_init(&pxmitframe->list);
					ptxservq->qcnt--;
					phwxmit[ac_index].accnt--;

					/* Remove sta node when there is no pending packets. */
					if (_rtw_queue_empty(&ptxservq->sta_pending) == true)
						list_del_init(&ptxservq->tx_pending);
				}
			}
			else
			{
				bulkstart = true;
			}

			spin_unlock_bh(&pxmitpriv->lock);

			if (bulkstart)
			{
				break;
			}

			pxmitframe->buf_addr = pxmitbuf->pbuf + pbuf;

			pxmitframe->agg_num = 0; /*  not first frame of aggregation */
			pxmitframe->pkt_offset = USB_92D_DUMMY_OFFSET - 1; /*  not first frame of aggregation, reserve 1 offset for early mode */
		}

		if (pHalData->bEarlyModeEnable)
			UpdateEarlyModeInfo8192D(padapter, pxmitframe,ptxservq);

#ifdef IDEA_CONDITION
		rtw_xmitframe_coalesce(padapter, pxmitframe->pkt, pxmitframe);
#else
		res = rtw_xmitframe_coalesce(padapter, pxmitframe->pkt, pxmitframe);
		if (res == false) {
			rtw_free_xmitframe(pxmitpriv, pxmitframe);
			continue;
		}
#endif

		/*  always return ndis_packet after rtw_xmitframe_coalesce */
		rtw_os_xmit_complete(padapter, pxmitframe);

		if (bfirst)
		{
			len = xmitframe_need_length(pfirstframe) + USB_HWDESC_HEADER_LEN;
			pbuf_tail = len;
			pbuf = _RND8(pbuf_tail);

			descCount = 0;
			bulkPtr = bulkSize;
			bfirst = false;
		}
		else
		{
			update_txdesc(pxmitframe, pxmitframe->buf_addr, pxmitframe->attrib.last_txcmdsz, true);

			/*  don't need xmitframe any more */
			rtw_free_xmitframe(pxmitpriv, pxmitframe);

			/*  handle pointer and stop condition */
			pbuf_tail = pbuf + len;
			pbuf = _RND8(pbuf_tail);

			pfirstframe->agg_num++;
			if (MAX_TX_AGG_PACKET_NUMBER == pfirstframe->agg_num)
				break;
		}

		/*  check pkt amount in one bluk */
		if (pbuf < bulkPtr)
		{
			descCount++;
			if (descCount == pHalData->UsbTxAggDescNum)
				break;
		}
		else
		{
			descCount = 0;
			bulkPtr = ((pbuf / bulkSize) + 1) * bulkSize; /*  round to next bulkSize */
		}
	} while (1);

	if ((pfirstframe->attrib.ether_type != 0x0806) &&
	    (pfirstframe->attrib.ether_type != 0x888e) &&
	    (pfirstframe->attrib.dhcp_pkt != 1))
	{
		rtw_issue_addbareq_cmd(padapter, pfirstframe);
	}

	/* 3 3. update first frame txdesc */
	if ((pbuf_tail % bulkSize) == 0) {
		/*  remove 1 pkt_offset */
		pbuf_tail -= PACKET_OFFSET_SZ;
		pfirstframe->buf_addr += PACKET_OFFSET_SZ;
		pfirstframe->pkt_offset--;
	}
	update_txdesc(pfirstframe, pfirstframe->buf_addr, pfirstframe->attrib.last_txcmdsz, true);

	/* 3 4. write xmit buffer to USB FIFO */
	ff_hwaddr = rtw_get_ff_hwaddr(pfirstframe);

	rtw_write_port(padapter, ff_hwaddr, pbuf_tail, (u8*)pxmitbuf);

	/* 3 5. update statisitc */
	pbuf_tail -= (pfirstframe->agg_num * TXDESC_SIZE);
	pbuf_tail -= (pfirstframe->pkt_offset * PACKET_OFFSET_SZ);

	rtw_count_tx_stats(padapter, pfirstframe, pbuf_tail);

	rtw_free_xmitframe(pxmitpriv, pfirstframe);

	return true;
}

static s32 xmitframe_direct(struct rtw_adapter *padapter, struct xmit_frame *pxmitframe)
{
	s32 res = _SUCCESS;

	res = rtw_xmitframe_coalesce(padapter, pxmitframe->pkt, pxmitframe);
	if (res == _SUCCESS) {
		rtw_dump_xframe(padapter, pxmitframe);
	}

	return res;
}

/*	Return
 *	true	dump packet directly
 *	false	enqueue packet
 */
static s32 pre_xmitframe(struct rtw_adapter *padapter, struct xmit_frame *pxmitframe)
{
	s32 res;
	struct xmit_buf *pxmitbuf = NULL;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;

	spin_lock_bh(&pxmitpriv->lock);

	if (rtw_txframes_sta_ac_pending(padapter, pattrib) > 0
#ifdef CONFIG_DUALMAC_CONCURRENT
		|| (dc_check_xmit(padapter)== false)
#endif
		)
		goto enqueue;

	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY|_FW_UNDER_LINKING) == true)
		goto enqueue;

#ifdef CONFIG_CONCURRENT_MODE
	if (check_buddy_fwstate(padapter, _FW_UNDER_SURVEY|_FW_UNDER_LINKING) == true)
		goto enqueue;
#endif /* CONFIG_CONCURRENT_MODE */

	pxmitbuf = rtw_alloc_xmitbuf(pxmitpriv);
	if (pxmitbuf == NULL)
		goto enqueue;

	spin_unlock_bh(&pxmitpriv->lock);

	pxmitframe->pxmitbuf = pxmitbuf;
	pxmitframe->buf_addr = pxmitbuf->pbuf;
	pxmitbuf->priv_data = pxmitframe;

	if (xmitframe_direct(padapter, pxmitframe) != _SUCCESS) {
		rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
		rtw_free_xmitframe(pxmitpriv, pxmitframe);
	}

	return true;

enqueue:
	res = rtw_xmitframe_enqueue(padapter, pxmitframe);
	spin_unlock_bh(&pxmitpriv->lock);

	if (res != _SUCCESS) {
		RT_TRACE(_module_xmit_osdep_c_, _drv_err_, ("pre_xmitframe: enqueue xmitframe fail\n"));
		rtw_free_xmitframe(pxmitpriv, pxmitframe);

		/*  Trick, make the statistics correct */
		pxmitpriv->tx_pkts--;
		pxmitpriv->tx_drop++;
		return true;
	}

	return false;
}

s32 rtl8192du_mgnt_xmit(struct rtw_adapter *padapter, struct xmit_frame *pmgntframe)
{
	return rtw_dump_xframe(padapter, pmgntframe);
}

/*
 * Return
 *	true	dump packet directly ok
 *	false	temporary can't transmit packets to hardware
 */
s32 rtl8192du_hal_xmit(struct rtw_adapter *padapter, struct xmit_frame *pxmitframe)
{
	return pre_xmitframe(padapter, pxmitframe);
}

#ifdef  CONFIG_HOSTAPD_MLME

static void rtl8192du_hostap_mgnt_xmit_cb(struct urb *urb)
{
	struct sk_buff *skb = (struct sk_buff *)urb->context;

	dev_kfree_skb_any(skb);
}

s32 rtl8192du_hostap_mgnt_xmit_entry(struct rtw_adapter *padapter, struct sk_buff *pkt)
{
	u16 fc;
	int rc, len, pipe;
	unsigned int bmcst, tid, qsel;
	struct sk_buff *skb, *pxmit_skb;
	struct urb *urb;
	unsigned char *pxmitbuf;
	struct tx_desc *ptxdesc;
	struct rtw_ieee80211_hdr *tx_hdr;
	struct hostapd_priv *phostapdpriv = padapter->phostapdpriv;
	struct net_device *pnetdev = padapter->pnetdev;
	struct hal_data_8192du  *pHalData = GET_HAL_DATA(padapter);
	struct dvobj_priv *pdvobj = adapter_to_dvobj(padapter);

	skb = pkt;
	len = skb->len;
	tx_hdr = (struct rtw_ieee80211_hdr *)(skb->data);
	fc = le16_to_cpu(tx_hdr->frame_ctl);
	bmcst = IS_MCAST(tx_hdr->addr1);

	if ((fc & RTW_IEEE80211_FCTL_FTYPE) != RTW_IEEE80211_FTYPE_MGMT)
		goto _exit;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)) /*  http://www.mail-archive.com/netdev@vger.kernel.org/msg17214.html */
	pxmit_skb = dev_alloc_skb(len + TXDESC_SIZE);
#else
	pxmit_skb = netdev_alloc_skb(pnetdev, len + TXDESC_SIZE);
#endif

	if (!pxmit_skb)
		goto _exit;

	pxmitbuf = pxmit_skb->data;
	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb) {
		goto _exit;
	}

	/*  ----- fill tx desc ----- */
	ptxdesc = (struct tx_desc *)pxmitbuf;
	memset(ptxdesc, 0, sizeof(*ptxdesc));

	/* offset 0 */
	ptxdesc->txdw0 |= cpu_to_le32(len&0x0000ffff);
	ptxdesc->txdw0 |= cpu_to_le32(((TXDESC_SIZE+OFFSET_SZ)<<OFFSET_SHT)&0x00ff0000);/* default = 32 bytes for TX Desc */
	ptxdesc->txdw0 |= cpu_to_le32(OWN | FSG | LSG);

	if (bmcst)
	{
		ptxdesc->txdw0 |= cpu_to_le32(BIT(24));
	}

	/* offset 4 */
	ptxdesc->txdw1 |= cpu_to_le32(0x00);/* MAC_ID */

	ptxdesc->txdw1 |= cpu_to_le32((0x12<<QSEL_SHT)&0x00001f00);

	ptxdesc->txdw1 |= cpu_to_le32((0x06<< 16) & 0x000f0000);/* b mode */

	/* offset 8 */

	/* offset 12 */
	ptxdesc->txdw3 |= cpu_to_le32((le16_to_cpu(tx_hdr->seq_ctl)<<16)&0xffff0000);

	/* offset 16 */
	ptxdesc->txdw4 |= cpu_to_le32(BIT(8));/* driver uses rate */

	/* offset 20 */

	/* HW append seq */
	ptxdesc->txdw4 |= cpu_to_le32(BIT(7)); /*  Hw set sequence number */
	ptxdesc->txdw3 |= cpu_to_le32((8 <<28)); /* set bit3 to 1. Suugested by TimChen. 2009.12.29. */

	rtl8192du_cal_txdesc_chksum(ptxdesc);
	/*  ----- end of fill tx desc ----- */

	/*  */
	skb_put(pxmit_skb, len + TXDESC_SIZE);
	pxmitbuf = pxmitbuf + TXDESC_SIZE;
	memcpy(pxmitbuf, skb->data, len);

	/*  ----- prepare urb for submit ----- */

	/* translate DMA FIFO addr to pipehandle */
	pipe = usb_sndbulkpipe(pdvobj->pusbdev, pHalData->Queue2EPNum[(u8)MGT_QUEUE_INX]&0x0f);
	usb_fill_bulk_urb(urb, pdvobj->pusbdev, pipe,
		pxmit_skb->data, pxmit_skb->len, rtl8192du_hostap_mgnt_xmit_cb, pxmit_skb);

	urb->transfer_flags |= URB_ZERO_PACKET;
	usb_anchor_urb(urb, &phostapdpriv->anchored);
	rc = usb_submit_urb(urb, GFP_ATOMIC);
	if (rc < 0) {
		usb_unanchor_urb(urb);
		kfree_skb(skb);
	}
	usb_free_urb(urb);

_exit:

	dev_kfree_skb_any(skb);
	return 0;
}
#endif
