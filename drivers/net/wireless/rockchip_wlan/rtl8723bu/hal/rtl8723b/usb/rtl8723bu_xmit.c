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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
 
 #define _RTL8723BU_XMIT_C_

#include <rtl8723b_hal.h>


s32	rtl8723bu_init_xmit_priv(_adapter *padapter)
{
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;

#ifdef PLATFORM_LINUX
	tasklet_init(&pxmitpriv->xmit_tasklet,
	     (void(*)(unsigned long))rtl8723bu_xmit_tasklet,
	     (unsigned long)padapter);
#endif
	return _SUCCESS;
}

void	rtl8723bu_free_xmit_priv(_adapter *padapter)
{
}

void _dbg_dump_tx_info(_adapter	*padapter,int frame_tag,struct tx_desc *ptxdesc)
{
	u8 bDumpTxPkt;
	u8 bDumpTxDesc = _FALSE;
	rtw_hal_get_def_var(padapter, HAL_DEF_DBG_DUMP_TXPKT, &(bDumpTxPkt));

	if(bDumpTxPkt ==1){//dump txdesc for data frame
		DBG_871X("dump tx_desc for data frame\n");
		if((frame_tag&0x0f) == DATA_FRAMETAG){	
			bDumpTxDesc = _TRUE;		
		}
	}	
	else if(bDumpTxPkt ==2){//dump txdesc for mgnt frame
		DBG_871X("dump tx_desc for mgnt frame\n");
		if((frame_tag&0x0f) == MGNT_FRAMETAG){	
			bDumpTxDesc = _TRUE;		
		}
	}	
	else if(bDumpTxPkt ==3){//dump early info
	}

	if(bDumpTxDesc){
		//	ptxdesc->txdw4 = cpu_to_le32(0x00001006);//RTS Rate=24M
		//	ptxdesc->txdw6 = 0x6666f800;
		DBG_8192C("=====================================\n");
		DBG_8192C("txdw0(0x%08x)\n",ptxdesc->txdw0);
		DBG_8192C("txdw1(0x%08x)\n",ptxdesc->txdw1);
		DBG_8192C("txdw2(0x%08x)\n",ptxdesc->txdw2);
		DBG_8192C("txdw3(0x%08x)\n",ptxdesc->txdw3);
		DBG_8192C("txdw4(0x%08x)\n",ptxdesc->txdw4);
		DBG_8192C("txdw5(0x%08x)\n",ptxdesc->txdw5);
		DBG_8192C("txdw6(0x%08x)\n",ptxdesc->txdw6);
		DBG_8192C("txdw7(0x%08x)\n",ptxdesc->txdw7);
		DBG_8192C("=====================================\n");
	}

}

int urb_zero_packet_chk(_adapter *padapter, int sz)
{
	u8 blnSetTxDescOffset;
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(padapter);
	blnSetTxDescOffset = (((sz + TXDESC_SIZE) %  pHalData->UsbBulkOutSize) ==0)?1:0;

	return blnSetTxDescOffset;
}
void fill_txdesc_sectype(struct pkt_attrib *pattrib, struct tx_desc *ptxdesc)
{
	if ((pattrib->encrypt > 0) && !pattrib->bswenc)
	{
		switch (pattrib->encrypt)
		{	
			//SEC_TYPE
			case _WEP40_:
			case _WEP104_:
					ptxdesc->txdw1 |= cpu_to_le32((0x01<<22)&0x00c00000);
					break;				
			case _TKIP_:
			case _TKIP_WTMIC_:	
					//ptxdesc->txdw1 |= cpu_to_le32((0x02<<22)&0x00c00000);
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

void fill_txdesc_vcs(struct pkt_attrib *pattrib, u32 *pdw)
{
	//DBG_8192C("cvs_mode=%d\n", pattrib->vcs_mode);	

	switch(pattrib->vcs_mode)
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

	if(pattrib->vcs_mode) {
		*pdw |= cpu_to_le32(BIT(13));

		// Set RTS BW
		if(pattrib->ht_en)
		{
			*pdw |= (pattrib->bwmode&CHANNEL_WIDTH_40)?	cpu_to_le32(BIT(27)):0;

			if(pattrib->ch_offset == HAL_PRIME_CHNL_OFFSET_LOWER)
				*pdw |= cpu_to_le32((0x01<<28)&0x30000000);
			else if(pattrib->ch_offset == HAL_PRIME_CHNL_OFFSET_UPPER)
				*pdw |= cpu_to_le32((0x02<<28)&0x30000000);
			else if(pattrib->ch_offset == HAL_PRIME_CHNL_OFFSET_DONT_CARE)
				*pdw |= 0;
			else
				*pdw |= cpu_to_le32((0x03<<28)&0x30000000);
		}
	}
}

void fill_txdesc_phy(struct pkt_attrib *pattrib, u32 *pdw)
{
	//DBG_8192C("bwmode=%d, ch_off=%d\n", pattrib->bwmode, pattrib->ch_offset);

	if(pattrib->ht_en)
	{
		*pdw |= (pattrib->bwmode&CHANNEL_WIDTH_40)?	cpu_to_le32(BIT(25)):0;

		if(pattrib->ch_offset == HAL_PRIME_CHNL_OFFSET_LOWER)
			*pdw |= cpu_to_le32((0x01<<20)&0x003f0000);
		else if(pattrib->ch_offset == HAL_PRIME_CHNL_OFFSET_UPPER)
			*pdw |= cpu_to_le32((0x02<<20)&0x003f0000);
		else if(pattrib->ch_offset == HAL_PRIME_CHNL_OFFSET_DONT_CARE)
			*pdw |= 0;
		else
			*pdw |= cpu_to_le32((0x03<<20)&0x003f0000);
	}
}

static s32 update_txdesc(struct xmit_frame *pxmitframe, u8 *pmem, s32 sz, u8 bagg_pkt)
{
	int	pull=0;
	

	_adapter			*padapter = pxmitframe->padapter;
	struct tx_desc	*ptxdesc = (struct tx_desc *)pmem;
#if 0	
	uint	qsel;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;		
	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	struct ht_priv		*phtpriv = &pmlmepriv->htpriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	sint	bmcst = IS_MCAST(pattrib->ra);
#ifdef CONFIG_P2P
	struct wifidirect_info*	pwdinfo = &padapter->wdinfo;
#endif //CONFIG_P2P
#endif
#ifndef CONFIG_USE_USB_BUFFER_ALLOC_TX
	if ((PACKET_OFFSET_SZ != 0)
		&& (_FALSE == bagg_pkt)
		&& (urb_zero_packet_chk(padapter, sz) == 0)) {
		ptxdesc = (struct tx_desc *)(pmem + PACKET_OFFSET_SZ);
		pull = 1;
		pxmitframe->pkt_offset --;
	}
#endif	// CONFIG_USE_USB_BUFFER_ALLOC_TX

	_rtw_memset(ptxdesc, 0, sizeof(struct tx_desc));

	rtl8723b_update_txdesc(pxmitframe, (u8 *)ptxdesc);
	_dbg_dump_tx_info(padapter,pxmitframe->frame_tag,ptxdesc);
	return pull;
		
}

#ifdef CONFIG_XMIT_THREAD_MODE
/*
 * Description
 *	Transmit xmitbuf to hardware tx fifo
 *
 * Return
 *	_SUCCESS	ok
 *	_FAIL		something error
 */
s32 rtl8723bu_xmit_buf_handler(PADAPTER padapter)
{
	//PHAL_DATA_TYPE phal;
	struct xmit_priv *pxmitpriv;
	struct xmit_buf *pxmitbuf;
	s32 ret;


	//phal = GET_HAL_DATA(padapter);
	pxmitpriv = &padapter->xmitpriv;

	ret = _rtw_down_sema(&pxmitpriv->xmit_sema);
	if (_FAIL == ret) {
		RT_TRACE(_module_hal_xmit_c_, _drv_emerg_,
				 ("%s: down SdioXmitBufSema fail!\n", __FUNCTION__));
		return _FAIL;
	}

	if (RTW_CANNOT_RUN(padapter)) {
		RT_TRACE(_module_hal_xmit_c_, _drv_notice_
				, ("%s: bDriverStopped(%s) bSurpriseRemoved(%s)!\n"
				, __func__
				, rtw_is_drv_stopped(padapter)?"True":"False"
				, rtw_is_surprise_removed(padapter)?"True":"False"));
		return _FAIL;
	}

	if(check_pending_xmitbuf(pxmitpriv) == _FALSE)
		return _SUCCESS;

#ifdef CONFIG_LPS_LCLK
	ret = rtw_register_tx_alive(padapter);
	if (ret != _SUCCESS) {
		RT_TRACE(_module_hal_xmit_c_, _drv_notice_,
				 ("%s: wait to leave LPS_LCLK\n", __FUNCTION__));
		return _SUCCESS;
	}
#endif

	do {
		pxmitbuf = dequeue_pending_xmitbuf(pxmitpriv);
		if (pxmitbuf == NULL) break;

		rtw_write_port(padapter, pxmitbuf->ff_hwaddr, pxmitbuf->len, (unsigned char*)pxmitbuf);

	} while (1);

#ifdef CONFIG_LPS_LCLK
	rtw_unregister_tx_alive(padapter);
#endif

	return _SUCCESS;
}
#endif


static s32 rtw_dump_xframe(_adapter *padapter, struct xmit_frame *pxmitframe)
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
	{
		rtw_issue_addbareq_cmd(padapter, pxmitframe);
	}
	
	mem_addr = pxmitframe->buf_addr;

       RT_TRACE(_module_rtl871x_xmit_c_,_drv_info_,("rtw_dump_xframe()\n"));
	
	for (t = 0; t < pattrib->nr_frags; t++)
	{
		if (inner_ret != _SUCCESS && ret == _SUCCESS)
			ret = _FAIL;

		if (t != (pattrib->nr_frags - 1))
		{
			RT_TRACE(_module_rtl871x_xmit_c_,_drv_err_,("pattrib->nr_frags=%d\n", pattrib->nr_frags));

			sz = pxmitpriv->frag_len;
			sz = sz - 4 - (psecuritypriv->sw_encrypt ? 0 : pattrib->icv_len);					
		}
		else //no frag
		{
			sz = pattrib->last_txcmdsz;
		}

		pull = update_txdesc(pxmitframe, mem_addr, sz, _FALSE);
//		rtl8723b_update_txdesc(pxmitframe, mem_addr+PACKET_OFFSET_SZ);

		if(pull)
		{
			mem_addr += PACKET_OFFSET_SZ; //pull txdesc head
			
			//pxmitbuf ->pbuf = mem_addr;			
			pxmitframe->buf_addr = mem_addr;

			w_sz = sz + TXDESC_SIZE;
		}
		else
		{
			w_sz = sz + TXDESC_SIZE + PACKET_OFFSET_SZ;
		}	

		ff_hwaddr = rtw_get_ff_hwaddr(pxmitframe);
#ifdef CONFIG_XMIT_THREAD_MODE
		pxmitbuf->len = w_sz;
		pxmitbuf->ff_hwaddr = ff_hwaddr;
		enqueue_pending_xmitbuf(pxmitpriv, pxmitbuf);
#else
		inner_ret = rtw_write_port(padapter, ff_hwaddr, w_sz, (unsigned char*)pxmitbuf);
#endif
		rtw_count_tx_stats(padapter, pxmitframe, sz);


		RT_TRACE(_module_rtl871x_xmit_c_,_drv_info_,("rtw_write_port, w_sz=%d\n", w_sz));
		//DBG_8192C("rtw_write_port, w_sz=%d, sz=%d, txdesc_sz=%d, tid=%d\n", w_sz, sz, w_sz-sz, pattrib->priority);      

		mem_addr += w_sz;

		mem_addr = (u8 *)RND4(((SIZE_PTR)(mem_addr)));

	}
	
	rtw_free_xmitframe(pxmitpriv, pxmitframe);

	if  (ret != _SUCCESS)
		rtw_sctx_done_err(&pxmitbuf->sctx, RTW_SCTX_DONE_UNKNOWN);

	return ret;	
}

#ifdef CONFIG_USB_TX_AGGREGATION
static u32 xmitframe_need_length(struct xmit_frame *pxmitframe)
{
	struct pkt_attrib *pattrib = &pxmitframe->attrib;

	u32	len = 0;

	// no consider fragement
	len = pattrib->hdrlen + pattrib->iv_len +
		SNAP_SIZE + sizeof(u16) +
		pattrib->pktlen +
		((pattrib->bswenc) ? pattrib->icv_len : 0);

	if(pattrib->encrypt ==_TKIP_)
		len += 8;

	return len;
}

#define IDEA_CONDITION 1	// check all packets before enqueue
s32 rtl8723bu_xmitframe_complete(_adapter *padapter, struct xmit_priv *pxmitpriv, struct xmit_buf *pxmitbuf)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct xmit_frame *pxmitframe = NULL;
	struct xmit_frame *pfirstframe = NULL;

	// aggregate variable
	struct hw_xmit *phwxmit;
	struct sta_info *psta = NULL;
	struct tx_servq *ptxservq = NULL;

	_irqL irqL;
	_list *xmitframe_plist = NULL, *xmitframe_phead = NULL;

	u32	pbuf;	// next pkt address
	u32	pbuf_tail;	// last pkt tail
	u32	len;	// packet length, except TXDESC_SIZE and PKT_OFFSET

	u32	bulkSize = pHalData->UsbBulkOutSize;
	u8	descCount;
	u32	bulkPtr;

	// dump frame variable
	u32 ff_hwaddr;

	_list *sta_plist, *sta_phead;
	u8 single_sta_in_queue = _FALSE;

#ifndef IDEA_CONDITION
	int res = _SUCCESS;
#endif

	RT_TRACE(_module_rtl8192c_xmit_c_, _drv_info_, ("+xmitframe_complete\n"));


	// check xmitbuffer is ok
	if (pxmitbuf == NULL) {
		pxmitbuf = rtw_alloc_xmitbuf(pxmitpriv);
		if (pxmitbuf == NULL) return _FALSE;
	}


	//3 1. pick up first frame
	do {
		rtw_free_xmitframe(pxmitpriv, pxmitframe);
			
		pxmitframe = rtw_dequeue_xframe(pxmitpriv, pxmitpriv->hwxmits, pxmitpriv->hwxmit_entry);
		if (pxmitframe == NULL) {
			// no more xmit frame, release xmit buffer
			rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
			return _FALSE;
		}


#ifndef IDEA_CONDITION
		if (pxmitframe->frame_tag != DATA_FRAMETAG) {
			RT_TRACE(_module_rtl8192c_xmit_c_, _drv_err_,
				 ("xmitframe_complete: frame tag(%d) is not DATA_FRAMETAG(%d)!\n",
				  pxmitframe->frame_tag, DATA_FRAMETAG));
//			rtw_free_xmitframe(pxmitpriv, pxmitframe);
			continue;
		}

		// TID 0~15
		if ((pxmitframe->attrib.priority < 0) ||
		    (pxmitframe->attrib.priority > 15)) {
			RT_TRACE(_module_rtl8192c_xmit_c_, _drv_err_,
				 ("xmitframe_complete: TID(%d) should be 0~15!\n",
				  pxmitframe->attrib.priority));
//			rtw_free_xmitframe(pxmitpriv, pxmitframe);
			continue;
		}
#endif

		pxmitframe->pxmitbuf = pxmitbuf;
		pxmitframe->buf_addr = pxmitbuf->pbuf;
		pxmitbuf->priv_data = pxmitframe;

		/* pxmitframe->agg_num = 1; */ /* alloc xmitframe should assign to 1. */
		/* pxmitframe->pkt_offset = 1; */ /* first frame of aggregation, reserve offset */
		pxmitframe->pkt_offset = (PACKET_OFFSET_SZ/8);

		if (rtw_xmitframe_coalesce(padapter, pxmitframe->pkt, pxmitframe) == _FALSE) {
			DBG_871X("%s coalesce 1st xmitframe failed\n", __func__);
			continue;
		}


		// always return ndis_packet after rtw_xmitframe_coalesce
		rtw_os_xmit_complete(padapter, pxmitframe);

		break;
	} while (1);

	//3 2. aggregate same priority and same DA(AP or STA) frames
	pfirstframe = pxmitframe;
	len = xmitframe_need_length(pfirstframe) + TXDESC_OFFSET;
	pbuf_tail = len;
	pbuf = _RND8(pbuf_tail);

	// check pkt amount in one bluk
	descCount = 0;
	bulkPtr = bulkSize;
	if (pbuf < bulkPtr)
		descCount++;
	else {
		descCount = 0;
		bulkPtr = ((pbuf / bulkSize) + 1) * bulkSize; // round to next bulkSize
	}

	// dequeue same priority packet from station tx queue
	psta = pfirstframe->attrib.psta;
	switch (pfirstframe->attrib.priority) {
		case 1:
		case 2:
			ptxservq = &(psta->sta_xmitpriv.bk_q);
			phwxmit = pxmitpriv->hwxmits + 3;
			break;

		case 4:
		case 5:
			ptxservq = &(psta->sta_xmitpriv.vi_q);
			phwxmit = pxmitpriv->hwxmits + 1;
			break;

		case 6:
		case 7:
			ptxservq = &(psta->sta_xmitpriv.vo_q);
			phwxmit = pxmitpriv->hwxmits;
			break;

		case 0:
		case 3:
		default:
			ptxservq = &(psta->sta_xmitpriv.be_q);
			phwxmit = pxmitpriv->hwxmits + 2;
			break;
	}

	_enter_critical_bh(&pxmitpriv->lock, &irqL);

	sta_phead = get_list_head(phwxmit->sta_queue);
	sta_plist = get_next(sta_phead);
	single_sta_in_queue = rtw_end_of_queue_search(sta_phead, get_next(sta_plist));

	xmitframe_phead = get_list_head(&ptxservq->sta_pending);
	xmitframe_plist = get_next(xmitframe_phead);
	while (rtw_end_of_queue_search(xmitframe_phead, xmitframe_plist) == _FALSE)
	{
		pxmitframe = LIST_CONTAINOR(xmitframe_plist, struct xmit_frame, list);
		xmitframe_plist = get_next(xmitframe_plist);

		if(_FAIL == rtw_hal_busagg_qsel_check(padapter,pfirstframe->attrib.qsel,pxmitframe->attrib.qsel))
			break;
		
		len = xmitframe_need_length(pxmitframe) + TXDESC_SIZE; // no offset
		if (pbuf + len > MAX_XMITBUF_SZ) break;

		rtw_list_delete(&pxmitframe->list);
		ptxservq->qcnt--;
		phwxmit->accnt--;

#ifndef IDEA_CONDITION
		// suppose only data frames would be in queue
		if (pxmitframe->frame_tag != DATA_FRAMETAG) {
			RT_TRACE(_module_rtl8192c_xmit_c_, _drv_err_,
				 ("xmitframe_complete: frame tag(%d) is not DATA_FRAMETAG(%d)!\n",
				  pxmitframe->frame_tag, DATA_FRAMETAG));
			rtw_free_xmitframe(pxmitpriv, pxmitframe);
			continue;
		}

		// TID 0~15
		if ((pxmitframe->attrib.priority < 0) ||
		    (pxmitframe->attrib.priority > 15)) {
			RT_TRACE(_module_rtl8192c_xmit_c_, _drv_err_,
				 ("xmitframe_complete: TID(%d) should be 0~15!\n",
				  pxmitframe->attrib.priority));
			rtw_free_xmitframe(pxmitpriv, pxmitframe);
			continue;
		}
#endif

//		pxmitframe->pxmitbuf = pxmitbuf;
		pxmitframe->buf_addr = pxmitbuf->pbuf + pbuf;

		pxmitframe->agg_num = 0; // not first frame of aggregation
		pxmitframe->pkt_offset = 0; // not first frame of aggregation, no need to reserve offset

		if (rtw_xmitframe_coalesce(padapter, pxmitframe->pkt, pxmitframe) == _FALSE) {
			DBG_871X("%s coalesce failed \n",__FUNCTION__);
			rtw_free_xmitframe(pxmitpriv, pxmitframe);
			continue;
		}


		// always return ndis_packet after rtw_xmitframe_coalesce
		rtw_os_xmit_complete(padapter, pxmitframe);

		// (len - TXDESC_SIZE) == pxmitframe->attrib.last_txcmdsz
		update_txdesc(pxmitframe, pxmitframe->buf_addr, pxmitframe->attrib.last_txcmdsz, _TRUE);

		// don't need xmitframe any more
		rtw_free_xmitframe(pxmitpriv, pxmitframe);

		// handle pointer and stop condition
		pbuf_tail = pbuf + len;
		pbuf = _RND8(pbuf_tail);

		pfirstframe->agg_num++;
		if (MAX_TX_AGG_PACKET_NUMBER == pfirstframe->agg_num)
			break;

		if (pbuf < bulkPtr) {
			descCount++;
			if (descCount == pHalData->UsbTxAggDescNum)
				break;
		} else {
			descCount = 0;
			bulkPtr = ((pbuf / bulkSize) + 1) * bulkSize;
		}
	}
	if (_rtw_queue_empty(&ptxservq->sta_pending) == _TRUE)
		rtw_list_delete(&ptxservq->tx_pending);
	else if (single_sta_in_queue == _FALSE) {
		/* Re-arrange the order of stations in this ac queue to balance the service for these stations */
		rtw_list_delete(&ptxservq->tx_pending);
		rtw_list_insert_tail(&ptxservq->tx_pending, get_list_head(phwxmit->sta_queue));
	}

	_exit_critical_bh(&pxmitpriv->lock, &irqL);

	if ((pfirstframe->attrib.ether_type != 0x0806) &&
	    (pfirstframe->attrib.ether_type != 0x888e) &&
	    (pfirstframe->attrib.dhcp_pkt != 1))
	{
		rtw_issue_addbareq_cmd(padapter, pfirstframe);
	}

#ifndef CONFIG_USE_USB_BUFFER_ALLOC_TX
	//3 3. update first frame txdesc
	if ((PACKET_OFFSET_SZ != 0)
		&& (pbuf_tail % bulkSize) == 0) {
		// remove pkt_offset
		pbuf_tail -= PACKET_OFFSET_SZ;
		pfirstframe->buf_addr += PACKET_OFFSET_SZ;
		pfirstframe->pkt_offset = 0;
	}
#endif	// CONFIG_USE_USB_BUFFER_ALLOC_TX
	update_txdesc(pfirstframe, pfirstframe->buf_addr, pfirstframe->attrib.last_txcmdsz, _TRUE);

	//3 4. write xmit buffer to USB FIFO
	ff_hwaddr = rtw_get_ff_hwaddr(pfirstframe);

	// xmit address == ((xmit_frame*)pxmitbuf->priv_data)->buf_addr
	rtw_write_port(padapter, ff_hwaddr, pbuf_tail, (u8*)pxmitbuf);


	//3 5. update statisitc
	pbuf_tail -= (pfirstframe->agg_num * TXDESC_SIZE);
	if (pfirstframe->pkt_offset == 1) pbuf_tail -= PACKET_OFFSET_SZ;
	
	rtw_count_tx_stats(padapter, pfirstframe, pbuf_tail);

	rtw_free_xmitframe(pxmitpriv, pfirstframe);

	return _TRUE;
}

#else

s32 rtl8723bu_xmitframe_complete(_adapter *padapter, struct xmit_priv *pxmitpriv, struct xmit_buf *pxmitbuf)
{		

	struct hw_xmit *phwxmits;
	sint hwentry;
	struct xmit_frame *pxmitframe=NULL;	
	int res=_SUCCESS, xcnt = 0;

	phwxmits = pxmitpriv->hwxmits;
	hwentry = pxmitpriv->hwxmit_entry;

	RT_TRACE(_module_rtl871x_xmit_c_,_drv_info_,("xmitframe_complete()\n"));

	if(pxmitbuf==NULL)
	{
		pxmitbuf = rtw_alloc_xmitbuf(pxmitpriv);		
		if(!pxmitbuf)
		{
			return _FALSE;
		}			
	}	


	do
	{		
		pxmitframe =  rtw_dequeue_xframe(pxmitpriv, phwxmits, hwentry);
		
		if(pxmitframe)
		{
			pxmitframe->pxmitbuf = pxmitbuf;				

			pxmitframe->buf_addr = pxmitbuf->pbuf;

			pxmitbuf->priv_data = pxmitframe;	

			if((pxmitframe->frame_tag&0x0f) == DATA_FRAMETAG)
			{	
				if(pxmitframe->attrib.priority<=15)//TID0~15
				{
					res = rtw_xmitframe_coalesce(padapter, pxmitframe->pkt, pxmitframe);
				}	
							
				rtw_os_xmit_complete(padapter, pxmitframe);//always return ndis_packet after rtw_xmitframe_coalesce 			
			}	

				
			RT_TRACE(_module_rtl871x_xmit_c_,_drv_info_,("xmitframe_complete(): rtw_dump_xframe\n"));

			
			if(res == _SUCCESS)
			{
				rtw_dump_xframe(padapter, pxmitframe);
			}
			else
			{
				rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
				rtw_free_xmitframe(pxmitpriv, pxmitframe);	
			}
	 			 		
			xcnt++;
			
		}
		else
		{			
			rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
			return _FALSE;
		}

		break;
		
	}while(0/*xcnt < (NR_XMITFRAME >> 3)*/);

	return _TRUE;
	
}
#endif



static s32 xmitframe_direct(_adapter *padapter, struct xmit_frame *pxmitframe)
{
	s32 res = _SUCCESS;


	res = rtw_xmitframe_coalesce(padapter, pxmitframe->pkt, pxmitframe);
	if (res == _SUCCESS) {
		rtw_dump_xframe(padapter, pxmitframe);
	}

	return res;
}

/*
 * Return
 *	_TRUE	dump packet directly
 *	_FALSE	enqueue packet
 */
static s32 pre_xmitframe(_adapter *padapter, struct xmit_frame *pxmitframe)
{
        _irqL irqL;
	s32 res;
	struct xmit_buf *pxmitbuf = NULL;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;
	struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
	u8 lg_sta_num;

	_enter_critical_bh(&pxmitpriv->lock, &irqL);

	if (rtw_txframes_sta_ac_pending(padapter, pattrib) > 0)
		goto enqueue;

	if (rtw_xmit_ac_blocked(padapter) == _TRUE)
		goto enqueue;

	rtw_dev_iface_status(padapter, NULL, NULL , &lg_sta_num, NULL, NULL);
	if (lg_sta_num)
		goto enqueue;

	pxmitbuf = rtw_alloc_xmitbuf(pxmitpriv);
	if (pxmitbuf == NULL)
		goto enqueue;

	_exit_critical_bh(&pxmitpriv->lock, &irqL);

	pxmitframe->pxmitbuf = pxmitbuf;
	pxmitframe->buf_addr = pxmitbuf->pbuf;
	pxmitbuf->priv_data = pxmitframe;

	if (xmitframe_direct(padapter, pxmitframe) != _SUCCESS) {
		rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
		rtw_free_xmitframe(pxmitpriv, pxmitframe);
	}

	return _TRUE;

enqueue:
	res = rtw_xmitframe_enqueue(padapter, pxmitframe);
	_exit_critical_bh(&pxmitpriv->lock, &irqL);

	if (res != _SUCCESS) {
		RT_TRACE(_module_xmit_osdep_c_, _drv_err_, ("pre_xmitframe: enqueue xmitframe fail\n"));
		rtw_free_xmitframe(pxmitpriv, pxmitframe);

		pxmitpriv->tx_drop++;
		return _TRUE;
	}

	return _FALSE;
}

s32 rtl8723bu_mgnt_xmit(_adapter *padapter, struct xmit_frame *pmgntframe)
{
	return rtw_dump_xframe(padapter, pmgntframe);
}

/*
 * Return
 *	_TRUE	dump packet directly ok
 *	_FALSE	temporary can't transmit packets to hardware
 */
s32 rtl8723bu_hal_xmit(_adapter *padapter, struct xmit_frame *pxmitframe)
{
	return pre_xmitframe(padapter, pxmitframe);
}

s32	rtl8723bu_hal_xmitframe_enqueue(_adapter *padapter, struct xmit_frame *pxmitframe)
{
	struct xmit_priv 	*pxmitpriv = &padapter->xmitpriv;
	s32 err;
	
	if ((err=rtw_xmitframe_enqueue(padapter, pxmitframe)) != _SUCCESS) 
	{
		rtw_free_xmitframe(pxmitpriv, pxmitframe);

		pxmitpriv->tx_drop++;					
	}
	else
	{
#ifdef PLATFORM_LINUX
		tasklet_hi_schedule(&pxmitpriv->xmit_tasklet);
#endif
	}
	
	return err;
	
}


#ifdef  CONFIG_HOSTAPD_MLME

static void rtl8723bu_hostap_mgnt_xmit_cb(struct urb *urb)
{	
#ifdef PLATFORM_LINUX
	struct sk_buff *skb = (struct sk_buff *)urb->context;

	//DBG_8192C("%s\n", __FUNCTION__);

	rtw_skb_free(skb);
#endif	
}

s32 rtl8723bu_hostap_mgnt_xmit_entry(_adapter *padapter, _pkt *pkt)
{
#ifdef PLATFORM_LINUX
	u16 fc;
	int rc, len, pipe;	
	unsigned int bmcst, tid, qsel;
	struct sk_buff *skb, *pxmit_skb;
	struct urb *urb;
	unsigned char *pxmitbuf;
	struct tx_desc *ptxdesc;
	struct ieee80211_hdr *tx_hdr;
	struct hostapd_priv *phostapdpriv = padapter->phostapdpriv;	
	struct net_device *pnetdev = padapter->pnetdev;
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(padapter);
	struct dvobj_priv *pdvobj = adapter_to_dvobj(padapter);	

	
	//DBG_8192C("%s\n", __FUNCTION__);

	skb = pkt;
	
	len = skb->len;
	tx_hdr = (struct ieee80211_hdr *)(skb->data);
	fc = le16_to_cpu(tx_hdr->frame_ctl);
	bmcst = IS_MCAST(tx_hdr->addr1);

	if ((fc & IEEE80211_FCTL_FTYPE) != IEEE80211_FTYPE_MGMT)
		goto _exit;

	pxmit_skb = rtw_skb_alloc(len + TXDESC_SIZE);

	if(!pxmit_skb)
		goto _exit;

	pxmitbuf = pxmit_skb->data;

	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb) {
		goto _exit;
	}

	// ----- fill tx desc -----	
	ptxdesc = (struct tx_desc *)pxmitbuf;	
	_rtw_memset(ptxdesc, 0, sizeof(*ptxdesc));
		
	//offset 0	
	ptxdesc->txdw0 |= cpu_to_le32(len&0x0000ffff); 
	ptxdesc->txdw0 |= cpu_to_le32(((TXDESC_SIZE+OFFSET_SZ)<<OFFSET_SHT)&0x00ff0000);//default = 32 bytes for TX Desc
	ptxdesc->txdw0 |= cpu_to_le32(OWN | FSG | LSG);

	if(bmcst)	
	{
		ptxdesc->txdw0 |= cpu_to_le32(BIT(24));
	}	

	//offset 4	
	ptxdesc->txdw1 |= cpu_to_le32(0x00);//MAC_ID

	ptxdesc->txdw1 |= cpu_to_le32((0x12<<QSEL_SHT)&0x00001f00);

	ptxdesc->txdw1 |= cpu_to_le32((0x06<< 16) & 0x000f0000);//b mode

	//offset 8			

	//offset 12		
	ptxdesc->txdw3 |= cpu_to_le32((le16_to_cpu(tx_hdr->seq_ctl)<<16)&0xffff0000);

	//offset 16		
	ptxdesc->txdw4 |= cpu_to_le32(BIT(8));//driver uses rate
		
	//offset 20


	//HW append seq
	ptxdesc->txdw4 |= cpu_to_le32(BIT(7)); // Hw set sequence number
	ptxdesc->txdw3 |= cpu_to_le32((8 <<28)); //set bit3 to 1. Suugested by TimChen. 2009.12.29.
	

	rtl8723b_cal_txdesc_chksum(ptxdesc);
	// ----- end of fill tx desc -----

	//
	skb_put(pxmit_skb, len + TXDESC_SIZE);
	pxmitbuf = pxmitbuf + TXDESC_SIZE;
	_rtw_memcpy(pxmitbuf, skb->data, len);

	//DBG_8192C("mgnt_xmit, len=%x\n", pxmit_skb->len);


	// ----- prepare urb for submit -----
	
	//translate DMA FIFO addr to pipehandle
	//pipe = ffaddr2pipehdl(pdvobj, MGT_QUEUE_INX);
	pipe = usb_sndbulkpipe(pdvobj->pusbdev, pHalData->Queue2EPNum[(u8)MGT_QUEUE_INX]&0x0f);
	
	usb_fill_bulk_urb(urb, pdvobj->pusbdev, pipe,
			  pxmit_skb->data, pxmit_skb->len, rtl8723bu_hostap_mgnt_xmit_cb, pxmit_skb);
	
	urb->transfer_flags |= URB_ZERO_PACKET;
	usb_anchor_urb(urb, &phostapdpriv->anchored);
	rc = usb_submit_urb(urb, GFP_ATOMIC);
	if (rc < 0) {
		usb_unanchor_urb(urb);
		kfree_skb(skb);
	}
	usb_free_urb(urb);

	
_exit:	
	
	rtw_skb_free(skb);

#endif

	return 0;

}
#endif

