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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#define _RTL8189ES_XMIT_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <sdio_ops.h>
#include <rtl8188e_hal.h>

static void fill_txdesc_sectype(struct pkt_attrib *pattrib, PTXDESC ptxdesc)
{
	if ((pattrib->encrypt > 0) && !pattrib->bswenc)
	{
		switch (pattrib->encrypt)
		{
			// SEC_TYPE
			case _WEP40_:
			case _WEP104_:
			case _TKIP_:
			case _TKIP_WTMIC_:
				ptxdesc->sectype = 1;
				break;

			case _AES_:
				ptxdesc->sectype = 3;
				break;

			case _NO_PRIVACY_:
			default:
					break;
		}
	}
}

 
 static void fill_txdesc_vcs(struct pkt_attrib *pattrib, PTXDESC ptxdesc)
{
	//DBG_8192C("cvs_mode=%d\n", pattrib->vcs_mode);

	switch (pattrib->vcs_mode)
	{
		case RTS_CTS:
			ptxdesc->rtsen = 1;
			break;

		case CTS_TO_SELF:
			ptxdesc->cts2self = 1;
			break;

		case NONE_VCS:
		default:
			break;
	}

	if (pattrib->vcs_mode)
		ptxdesc->hw_rts_en = 1; // ENABLE HW RTS
}

static void fill_txdesc_phy(struct pkt_attrib *pattrib, PTXDESC ptxdesc)
{
	//DBG_8192C("bwmode=%d, ch_off=%d\n", pattrib->bwmode, pattrib->ch_offset);

	if (pattrib->ht_en)
	{
		if (pattrib->bwmode & HT_CHANNEL_WIDTH_40)
			ptxdesc->data_bw = 1;

		switch (pattrib->ch_offset)
		{
			case HAL_PRIME_CHNL_OFFSET_DONT_CARE:
				ptxdesc->data_sc = 0;
				break;

			case HAL_PRIME_CHNL_OFFSET_LOWER:
				ptxdesc->data_sc = 1;
				break;

			case HAL_PRIME_CHNL_OFFSET_UPPER:
				ptxdesc->data_sc = 2;
				break;

			default:
				ptxdesc->data_sc = 3; // Duplicate
				break;
		}
	}
}

static void rtl8188e_cal_txdesc_chksum(struct tx_desc *ptxdesc)
{
	u16	*usPtr = (u16*)ptxdesc;
	u32 count = 16;		// (32 bytes / 2 bytes per XOR) => 16 times
	u32 index;
	u16 checksum = 0;


	// Clear first
	ptxdesc->txdw7 &= cpu_to_le32(0xffff0000);

	for (index = 0; index < count; index++) {
		checksum ^= le16_to_cpu(*(usPtr + index));
	}

	ptxdesc->txdw7 |= cpu_to_le32(checksum & 0x0000ffff);
}
//
// Description: In normal chip, we should send some packet to Hw which will be used by Fw
//			in FW LPS mode. The function is to fill the Tx descriptor of this packets, then
//			Fw can tell Hw to send these packet derectly.
//
void rtl8188e_fill_fake_txdesc(
	PADAPTER	padapter,
	u8*			pDesc,
	u32			BufferLen,
	u8			IsPsPoll,
	u8			IsBTQosNull)
{
	struct tx_desc *ptxdesc;


	// Clear all status
	ptxdesc = (struct tx_desc*)pDesc;
	_rtw_memset(pDesc, 0, TXDESC_SIZE);

	//offset 0
	ptxdesc->txdw0 |= cpu_to_le32( OWN | FSG | LSG); //own, bFirstSeg, bLastSeg;

	ptxdesc->txdw0 |= cpu_to_le32(((TXDESC_SIZE+OFFSET_SZ)<<OFFSET_SHT)&0x00ff0000); //32 bytes for TX Desc

	ptxdesc->txdw0 |= cpu_to_le32(BufferLen&0x0000ffff); // Buffer size + command header

	//offset 4
	ptxdesc->txdw1 |= cpu_to_le32((QSLT_MGNT<<QSEL_SHT)&0x00001f00); // Fixed queue of Mgnt queue

	//Set NAVUSEHDR to prevent Ps-poll AId filed to be changed to error vlaue by Hw.
	if (IsPsPoll)
	{
		ptxdesc->txdw1 |= cpu_to_le32(NAVUSEHDR);
	}
	else
	{
		ptxdesc->txdw4 |= cpu_to_le32(BIT(7)); // Hw set sequence number
		ptxdesc->txdw3 |= cpu_to_le32((8 <<28)); //set bit3 to 1. Suugested by TimChen. 2009.12.29.
	}

	if (_TRUE == IsBTQosNull)
	{
		ptxdesc->txdw2 |= cpu_to_le32(BIT(23)); // BT NULL
	}

	//offset 16
	ptxdesc->txdw4 |= cpu_to_le32(BIT(8));//driver uses rate

#if defined(CONFIG_USB_HCI) || defined(CONFIG_SDIO_HCI)
	// USB interface drop packet if the checksum of descriptor isn't correct.
	// Using this checksum can let hardware recovery from packet bulk out error (e.g. Cancel URC, Bulk out error.).
	rtl8188e_cal_txdesc_chksum(ptxdesc);
#endif
}


#define SDIO_TX_AGG_MAX	(5)
//#define CONFIG_FIX_CORE_DUMP ==> have bug
//#define DBG_EMINFO

#if 0
void rtl8188e_cal_txdesc_chksum(struct tx_desc *ptxdesc)
{
	u16	*usPtr = (u16*)ptxdesc;
	u32 count = 16;		// (32 bytes / 2 bytes per XOR) => 16 times
	u32 index;
	u16 checksum = 0;


	// Clear first
	ptxdesc->txdw7 &= cpu_to_le32(0xffff0000);

	for (index = 0; index < count; index++) {
		checksum ^= le16_to_cpu(*(usPtr + index));
	}

	ptxdesc->txdw7 |= cpu_to_le32(checksum & 0x0000ffff);
}

static void fill_txdesc_sectype(struct pkt_attrib *pattrib, PTXDESC ptxdesc)
{
	if ((pattrib->encrypt > 0) && !pattrib->bswenc)
	{
		switch (pattrib->encrypt)
		{
			// SEC_TYPE
			case _WEP40_:
			case _WEP104_:
			case _TKIP_:
			case _TKIP_WTMIC_:
				ptxdesc->sectype = 1;
				break;

			case _AES_:
				ptxdesc->sectype = 3;
				break;

			case _NO_PRIVACY_:
			default:
					break;
		}
	}
}

static void fill_txdesc_vcs(struct pkt_attrib *pattrib, PTXDESC ptxdesc)
{
	//DBG_8192C("cvs_mode=%d\n", pattrib->vcs_mode);

	switch (pattrib->vcs_mode)
	{
		case RTS_CTS:
			ptxdesc->rtsen = 1;
			break;

		case CTS_TO_SELF:
			ptxdesc->cts2self = 1;
			break;

		case NONE_VCS:
		default:
			break;
	}

	if (pattrib->vcs_mode)
		ptxdesc->hw_rts_en = 1; // ENABLE HW RTS
}

static void fill_txdesc_phy(struct pkt_attrib *pattrib, PTXDESC ptxdesc)
{
	//DBG_8192C("bwmode=%d, ch_off=%d\n", pattrib->bwmode, pattrib->ch_offset);

	if (pattrib->ht_en)
	{
		if (pattrib->bwmode & HT_CHANNEL_WIDTH_40)
			ptxdesc->data_bw = 1;

		switch (pattrib->ch_offset)
		{
			case HAL_PRIME_CHNL_OFFSET_DONT_CARE:
				ptxdesc->data_sc = 0;
				break;

			case HAL_PRIME_CHNL_OFFSET_LOWER:
				ptxdesc->data_sc = 1;
				break;

			case HAL_PRIME_CHNL_OFFSET_UPPER:
				ptxdesc->data_sc = 2;
				break;

			default:
				ptxdesc->data_sc = 3; // Duplicate
				break;
		}
	}
}
#endif

void rtl8188es_fill_default_txdesc(
	struct xmit_frame *pxmitframe,
	u8 *pbuf)
{
	PADAPTER padapter;
	HAL_DATA_TYPE *pHalData;
	struct dm_priv *pdmpriv;
	struct pkt_attrib *pattrib;
	PTXDESC ptxdesc;
	s32 bmcst;
#ifdef CONFIG_P2P
	struct wifidirect_info* pwdinfo;
#endif //CONFIG_P2P


	padapter = pxmitframe->padapter;
	pHalData = GET_HAL_DATA(padapter);
	pdmpriv = &pHalData->dmpriv;
#ifdef CONFIG_P2P
	pwdinfo = &padapter->wdinfo;
#endif //CONFIG_P2P

	pattrib = &pxmitframe->attrib;
	bmcst = IS_MCAST(pattrib->ra);

	ptxdesc = (PTXDESC)pbuf;

	if (pxmitframe->frame_tag == DATA_FRAMETAG)
	{
		ptxdesc->macid = pattrib->mac_id; // CAM_ID(MAC_ID)

		if (pattrib->ampdu_en == _TRUE)
			ptxdesc->agg_en = 1; // AGG EN
		else
			ptxdesc->bk = 1; // AGG BK

		ptxdesc->qsel = pattrib->qsel;
		ptxdesc->rate_id = pattrib->raid;

		fill_txdesc_sectype(pattrib, ptxdesc);

		ptxdesc->seq = pattrib->seqnum;

		//todo: qos_en

		ptxdesc->userate = 1; // driver uses rate	

		if ((pattrib->ether_type != 0x888e) &&
			(pattrib->ether_type != 0x0806) &&
			(pattrib->dhcp_pkt != 1))
		{
			// Non EAP & ARP & DHCP type data packet

			fill_txdesc_vcs(pattrib, ptxdesc);
			fill_txdesc_phy(pattrib, ptxdesc);

			ptxdesc->rtsrate = 8; // RTS Rate=24M
			ptxdesc->data_ratefb_lmt = 0x1F;
			ptxdesc->rts_ratefb_lmt = 0xF;
		#if (RATE_ADAPTIVE_SUPPORT == 1)				
			if(pattrib->ht_en){
				ptxdesc->sgi = ODM_RA_GetShortGI_8188E(&pHalData->odmpriv,pattrib->mac_id);
			}
			ptxdesc->datarate = ODM_RA_GetDecisionRate_8188E(&pHalData->odmpriv,pattrib->mac_id);			
			
	               //for debug
	               #if 0
	                if(padapter->fix_rate!= 0xFF){
					ptxdesc->datarate = padapter->fix_rate;
				}
	                #endif
			#if (POWER_TRAINING_ACTIVE==1)
			ptxdesc->pwr_status = ODM_RA_GetHwPwrStatus_8188E(&pHalData->odmpriv,pattrib->mac_id);
			#endif		
		#else			
			ptxdesc->datarate = 0x13; //MCS7 
			ptxdesc->sgi = 1; // SGI
			if(padapter->fix_rate!= 0xFF){//modify datat by iwpriv
				ptxdesc->datarate = padapter->fix_rate;				
			}
		#endif	
		
			
		}
		else
		{
			// EAP data packet and ARP and DHCP packet.
			// Use the 1M data rate to send the EAP/ARP packet.
			// This will maybe make the handshake smooth.

			ptxdesc->bk = 1; // AGG BK		

#ifdef CONFIG_P2P
			//	Added by Albert 2011/03/22
			//	In the P2P mode, the driver should not support the b mode.
			//	So, the Tx packet shouldn't use the CCK rate
			if ( pwdinfo->p2p_state != P2P_STATE_NONE )
			{
				ptxdesc->datarate = 0x4; // Use the 6M data rate.
			}
#endif //CONFIG_P2P
		}

		ptxdesc->usb_txagg_num = pxmitframe->agg_num;
	}
	else if (pxmitframe->frame_tag == MGNT_FRAMETAG)
	{
//		RT_TRACE(_module_hal_xmit_c_, _drv_notice_, ("%s: MGNT_FRAMETAG\n", __FUNCTION__));

		ptxdesc->macid = pattrib->mac_id; // CAM_ID(MAC_ID)
		ptxdesc->qsel = pattrib->qsel;
		ptxdesc->rate_id = pattrib->raid; // Rate ID
		ptxdesc->seq = pattrib->seqnum;
		ptxdesc->userate = 1; // driver uses rate, 1M
		ptxdesc->rty_lmt_en = 1; // retry limit enable
		ptxdesc->data_rt_lmt = 6; // retry limit = 6

#ifdef CONFIG_P2P
		//	Added by Albert 2011/03/17
		//	In the P2P mode, the driver should not support the b mode.
		//	So, the Tx packet shouldn't use the CCK rate
		if ( pwdinfo->p2p_state != P2P_STATE_NONE )
		{
			ptxdesc->datarate = 0x4; // Use the 6M data rate.
		}
#endif //CONFIG_P2P
	}
	else if (pxmitframe->frame_tag == TXAGG_FRAMETAG)
	{
		RT_TRACE(_module_hal_xmit_c_, _drv_warning_, ("%s: TXAGG_FRAMETAG\n", __FUNCTION__));
	}
#ifdef CONFIG_MP_INCLUDED
	else if (pxmitframe->frame_tag == MP_FRAMETAG)
	{
		struct tx_desc *pdesc;

		pdesc = (struct tx_desc*)ptxdesc;
		RT_TRACE(_module_hal_xmit_c_, _drv_notice_, ("%s: MP_FRAMETAG\n", __FUNCTION__));
		fill_txdesc_for_mp(padapter, pdesc);

		pdesc->txdw0 = le32_to_cpu(pdesc->txdw0);
		pdesc->txdw1 = le32_to_cpu(pdesc->txdw1);
		pdesc->txdw2 = le32_to_cpu(pdesc->txdw2);
		pdesc->txdw3 = le32_to_cpu(pdesc->txdw3);
		pdesc->txdw4 = le32_to_cpu(pdesc->txdw4);
		pdesc->txdw5 = le32_to_cpu(pdesc->txdw5);
		pdesc->txdw6 = le32_to_cpu(pdesc->txdw6);
		pdesc->txdw7 = le32_to_cpu(pdesc->txdw7);
#ifdef CONFIG_PCI_HCI
		pdesc->txdw8 = le32_to_cpu(pdesc->txdw8);
		pdesc->txdw9 = le32_to_cpu(pdesc->txdw9);
		pdesc->txdw10 = le32_to_cpu(pdesc->txdw10);
		pdesc->txdw11 = le32_to_cpu(pdesc->txdw11);
		pdesc->txdw12 = le32_to_cpu(pdesc->txdw12);
		pdesc->txdw13 = le32_to_cpu(pdesc->txdw13);
		pdesc->txdw14 = le32_to_cpu(pdesc->txdw14);
		pdesc->txdw15 = le32_to_cpu(pdesc->txdw15);
#endif

	}
#endif // CONFIG_MP_INCLUDED
	else
	{
		RT_TRACE(_module_hal_xmit_c_, _drv_warning_, ("%s: frame_tag=0x%x\n", __FUNCTION__, pxmitframe->frame_tag));

		ptxdesc->macid = 4; // CAM_ID(MAC_ID)
		ptxdesc->rate_id = 6; // Rate ID
		ptxdesc->seq = pattrib->seqnum;
		ptxdesc->userate = 1; // driver uses rate, 1M
	}

	ptxdesc->pktlen = pattrib->last_txcmdsz;
	if (pxmitframe->frame_tag == DATA_FRAMETAG){
		#ifdef CONFIG_TX_EARLY_MODE
		ptxdesc->offset = TXDESC_SIZE +EARLY_MODE_INFO_SIZE ;
		ptxdesc->pkt_offset = 0x01;
		#else
		ptxdesc->offset = TXDESC_SIZE ;
		ptxdesc->pkt_offset = 0;
		#endif
	}
	else{
		ptxdesc->offset = TXDESC_SIZE ;
	}
	
	if (bmcst) ptxdesc->bmc = 1;
	ptxdesc->ls = 1;
	ptxdesc->fs = 1;
	ptxdesc->own = 1;

	// 2009.11.05. tynli_test. Suggested by SD4 Filen for FW LPS.
	// (1) The sequence number of each non-Qos frame / broadcast / multicast /
	// mgnt frame should be controled by Hw because Fw will also send null data
	// which we cannot control when Fw LPS enable.
	// --> default enable non-Qos data sequense number. 2010.06.23. by tynli.
	// (2) Enable HW SEQ control for beacon packet, because we use Hw beacon.
	// (3) Use HW Qos SEQ to control the seq num of Ext port non-Qos packets.
	// 2010.06.23. Added by tynli.
	if (!pattrib->qos_en)
	{
		// Hw set sequence number
		ptxdesc->hwseq_en = 1; // HWSEQ_EN
		ptxdesc->hwseq_sel = 0; // HWSEQ_SEL
	}

}

/*
 *	Description:
 *
 *	Parameters:
 *		pxmitframe	xmitframe
 *		pbuf		where to fill tx desc
 */
void rtl8188es_update_txdesc(struct xmit_frame *pxmitframe, u8 *pbuf)
{
	struct tx_desc *pdesc;


	pdesc = (struct tx_desc*)pbuf;
	_rtw_memset(pdesc, 0, sizeof(struct tx_desc));

	rtl8188es_fill_default_txdesc(pxmitframe, pbuf);

	pdesc->txdw0 = cpu_to_le32(pdesc->txdw0);
	pdesc->txdw1 = cpu_to_le32(pdesc->txdw1);
	pdesc->txdw2 = cpu_to_le32(pdesc->txdw2);
	pdesc->txdw3 = cpu_to_le32(pdesc->txdw3);
	pdesc->txdw4 = cpu_to_le32(pdesc->txdw4);
	pdesc->txdw5 = cpu_to_le32(pdesc->txdw5);
	pdesc->txdw6 = cpu_to_le32(pdesc->txdw6);
	pdesc->txdw7 = cpu_to_le32(pdesc->txdw7);
#ifdef CONFIG_PCI_HCI
	pdesc->txdw8 = cpu_to_le32(pdesc->txdw8);
	pdesc->txdw9 = cpu_to_le32(pdesc->txdw9);
	pdesc->txdw10 = cpu_to_le32(pdesc->txdw10);
	pdesc->txdw11 = cpu_to_le32(pdesc->txdw11);
	pdesc->txdw12 = cpu_to_le32(pdesc->txdw12);
	pdesc->txdw13 = cpu_to_le32(pdesc->txdw13);
	pdesc->txdw14 = cpu_to_le32(pdesc->txdw14);
	pdesc->txdw15 = cpu_to_le32(pdesc->txdw15);
#endif

	rtl8188e_cal_txdesc_chksum(pdesc);
}

/*
 * Description
 *	Transmit xmitbuf to hardware tx fifo
 *
 * Return
 *	_SUCCESS	ok
 *	_FAIL		something error
 */
s32 rtl8188es_xmit_buf_handler(PADAPTER padapter)
{
	struct mlme_priv *pmlmepriv;
	struct xmit_priv *pxmitpriv;
	struct xmit_buf *pxmitbuf;
	struct xmit_frame *pframe;
	u8 *freePage;
	u32 requiredPage;
	u8 PageIdx;
	_irqL irql;
	u32 n;
	s32 ret;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	pmlmepriv = &padapter->mlmepriv;
	pxmitpriv = &padapter->xmitpriv;
	freePage = pHalData->SdioTxFIFOFreePage;
	
	ret = _rtw_down_sema(&pxmitpriv->xmit_sema);
	if (ret == _FAIL) {
		RT_TRACE(_module_hal_xmit_c_, _drv_emerg_, ("down SdioXmitBufSema fail!\n"));
		return _FAIL;
	}

	if ((padapter->bDriverStopped == _TRUE) ||
		(padapter->bSurpriseRemoved == _TRUE)) {
		RT_TRACE(_module_hal_xmit_c_, _drv_notice_,
				 ("%s: bDriverStopped(%d) bSurpriseRemoved(%d)\n",
				  __FUNCTION__, padapter->bDriverStopped, padapter->bSurpriseRemoved));
		return _FAIL;
	}

#ifdef CONFIG_LPS_LCLK
	ret = rtw_register_tx_alive(padapter);
	if (ret != _SUCCESS) return _SUCCESS;
#endif

	do {
		ret = check_fwstate(pmlmepriv, _FW_UNDER_SURVEY);
		if (_TRUE == ret)
			pxmitbuf = dequeue_pending_xmitbuf_under_survey(pxmitpriv);
		else
			pxmitbuf = dequeue_pending_xmitbuf(pxmitpriv);
		if (pxmitbuf == NULL) break;
		//pframe = (struct xmit_frame*)pxmitbuf->priv_data;
		//requiredPage = pframe->pg_num;
		requiredPage = pxmitbuf->pg_num;
		//printk("%s==> requiredPage(%d)\n",__FUNCTION__,requiredPage);
		// translate fifo addr to queue index
		switch (pxmitbuf->ff_hwaddr)
		{
			case WLAN_TX_HIQ_DEVICE_ID:
					PageIdx = HI_QUEUE_IDX;
					break;

			case WLAN_TX_MIQ_DEVICE_ID:
					PageIdx = MID_QUEUE_IDX;
					break;

			case WLAN_TX_LOQ_DEVICE_ID:
					PageIdx = LOW_QUEUE_IDX;
					break;
		}

		// check if hardware tx fifo page is enough
		n = 0;
	//	_enter_critical_bh(&pHalData->SdioTxFIFOFreePageLock, &irql);
		do {
			if (requiredPage <= freePage[PageIdx]) {
				freePage[PageIdx] -= requiredPage;
				break;
			}

			// The number of page which public page included is available.
			if ((freePage[PageIdx] + freePage[PUBLIC_QUEUE_IDX]) > (requiredPage + 1))
			{
				u8 requiredPublicPage;

				requiredPublicPage = requiredPage - freePage[PageIdx];
				freePage[PageIdx] = 0;
				freePage[PUBLIC_QUEUE_IDX] -= requiredPublicPage;
				break;
			}
	//		_exit_critical_bh(&pHalData->SdioTxFIFOFreePageLock, &irql);

			if ((padapter->bSurpriseRemoved == _TRUE) || (padapter->bDriverStopped == _TRUE)){
				RT_TRACE(_module_hal_xmit_c_, _drv_notice_,
					 ("%s: bSurpriseRemoved(update TX FIFO page)\n", __FUNCTION__));
				goto free_xmitbuf;
			}

			n++;
			if ((n % 60) == 0) {//or 80
				//DBG_871X("%s: FIFO starvation!(%d) len=%d agg=%d page=(R)%d(A)%d\n",
				//	__func__, n, pxmitbuf->len, pxmitbuf->agg_num, pframe->pg_num, freePage[PageIdx] + freePage[PUBLIC_QUEUE_IDX]);
				rtw_msleep_os(10);
				rtw_yield_os();				
			}

			// Total number of page is NOT available, so update current FIFO status
			HalQueryTxBufferStatus8189ESdio(padapter);

	//		_enter_critical_bh(&pHalData->SdioTxFIFOFreePageLock, &irql);
		} while (1);
//		_exit_critical_bh(&phal->SdioTxFIFOFreePageLock, &irql);

		if (padapter->bSurpriseRemoved == _TRUE) {
			RT_TRACE(_module_hal_xmit_c_, _drv_notice_,
				 ("%s: bSurpriseRemoved(wirte port)\n", __FUNCTION__));
			goto free_xmitbuf;
		}
	
		rtw_write_port(padapter, pxmitbuf->ff_hwaddr, pxmitbuf->len, pxmitbuf->pdata);

free_xmitbuf:		
		//rtw_free_xmitframe(pxmitpriv, pframe);
		//pxmitbuf->priv_data = NULL;
		rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
	} while (1);

#ifdef CONFIG_LPS_LCLK
	rtw_unregister_tx_alive(padapter);
#endif

	return _SUCCESS;
}

#if 0

/*
 *	Description:
 *		Translate QSEL to hardware tx FIFO address
 */
//static
u32 get_txfifo_hwaddr(struct xmit_frame *pxmitframe)
{
	u32 addr;
	struct pkt_attrib *pattrib;
	struct registry_priv *pregistrypriv;


	pattrib = &pxmitframe->attrib;
	switch (pattrib->qsel)
	{
		case 0:
		case 3:
			addr = WLAN_TX_LOQ_DEVICE_ID;
		 	break;
		case 1:
		case 2:
			pregistrypriv = &pxmitframe->padapter->registrypriv;
			if (!pregistrypriv->wifi_spec)
				addr = WLAN_TX_LOQ_DEVICE_ID;
			else
				addr = WLAN_TX_MIQ_DEVICE_ID;
			break;
		case 4:
		case 5:
			addr = WLAN_TX_MIQ_DEVICE_ID;
			break;
		case 6:
		case 7:
		case 0x10:
		case 0x11://BC/MC in PS (HIQ)
		case 0x12:
			addr = WLAN_TX_HIQ_DEVICE_ID;
			break;
		default:
			addr = WLAN_TX_LOQ_DEVICE_ID;
			break;
	}

	return addr;
}

/*
 * Description:
 *	Aggregation packets and send to hardware
 *
 * Return:
 *	0	Success
 *	-1	Hardware resource(TX FIFO) not ready
 *	-2	Software resource(xmitbuf) not ready
 */
#ifdef CONFIG_TX_EARLY_MODE
#if RTL8188E_EARLY_MODE_PKT_NUM_10 == 1	
	#define EARLY_MODE_MAX_PKT_NUM	10
#else
	#define EARLY_MODE_MAX_PKT_NUM	5
#endif


struct EMInfo{
	u8 	EMPktNum;
	u16  EMPktLen[EARLY_MODE_MAX_PKT_NUM];
};


void
InsertEMContent_8188E(
	struct EMInfo *pEMInfo,
	IN pu1Byte	VirtualAddress)
{

#if RTL8188E_EARLY_MODE_PKT_NUM_10 == 1
	u1Byte index=0;
	u4Byte	dwtmp=0;
#endif

	_rtw_memset(VirtualAddress, 0, EARLY_MODE_INFO_SIZE);
	if(pEMInfo->EMPktNum==0)
		return;

	#ifdef DBG_EMINFO
	{
		int i;
		printk("\n%s ==> pEMInfo->EMPktNum =%d\n",__FUNCTION__,pEMInfo->EMPktNum);	
		for(i=0;i< EARLY_MODE_MAX_PKT_NUM;i++){
			printk("%s ==> pEMInfo->EMPktLen[%d] =%d\n",__FUNCTION__,i,pEMInfo->EMPktLen[i]);	
		}

	}
	#endif
	
#if RTL8188E_EARLY_MODE_PKT_NUM_10 == 1
	SET_EARLYMODE_PKTNUM(VirtualAddress, pEMInfo->EMPktNum);

	if(pEMInfo->EMPktNum == 1){
		dwtmp = pEMInfo->EMPktLen[0];
	}else{
		dwtmp = pEMInfo->EMPktLen[0];
		dwtmp += ((dwtmp%4)?(4-dwtmp%4):0)+4;
		dwtmp += pEMInfo->EMPktLen[1];
	}
	SET_EARLYMODE_LEN0(VirtualAddress, dwtmp);
	if(pEMInfo->EMPktNum <= 3){
		dwtmp = pEMInfo->EMPktLen[2];
	}else{
		dwtmp = pEMInfo->EMPktLen[2];
		dwtmp += ((dwtmp%4)?(4-dwtmp%4):0)+4;
		dwtmp += pEMInfo->EMPktLen[3];
	}
	SET_EARLYMODE_LEN1(VirtualAddress, dwtmp);
	if(pEMInfo->EMPktNum <= 5){
		dwtmp = pEMInfo->EMPktLen[4];
	}else{
		dwtmp = pEMInfo->EMPktLen[4];
		dwtmp += ((dwtmp%4)?(4-dwtmp%4):0)+4;
		dwtmp += pEMInfo->EMPktLen[5];
	}
	SET_EARLYMODE_LEN2_1(VirtualAddress, dwtmp&0xF);
	SET_EARLYMODE_LEN2_2(VirtualAddress, dwtmp>>4);
	if(pEMInfo->EMPktNum <= 7){
		dwtmp = pEMInfo->EMPktLen[6];
	}else{
		dwtmp = pEMInfo->EMPktLen[6];
		dwtmp += ((dwtmp%4)?(4-dwtmp%4):0)+4;
		dwtmp += pEMInfo->EMPktLen[7];
	}
	SET_EARLYMODE_LEN3(VirtualAddress, dwtmp);
	if(pEMInfo->EMPktNum <= 9){
		dwtmp = pEMInfo->EMPktLen[8];
	}else{
		dwtmp = pEMInfo->EMPktLen[8];
		dwtmp += ((dwtmp%4)?(4-dwtmp%4):0)+4;
		dwtmp += pEMInfo->EMPktLen[9];
	}
	SET_EARLYMODE_LEN4(VirtualAddress, dwtmp);
#else	
	SET_EARLYMODE_PKTNUM(VirtualAddress, pEMInfo->EMPktNum);
	SET_EARLYMODE_LEN0(VirtualAddress, pEMInfo->EMPktLen[0]);
	SET_EARLYMODE_LEN1(VirtualAddress, pEMInfo->EMPktLen[1]);
	SET_EARLYMODE_LEN2_1(VirtualAddress, pEMInfo->EMPktLen[2]&0xF);
	SET_EARLYMODE_LEN2_2(VirtualAddress, pEMInfo->EMPktLen[2]>>4);
	SET_EARLYMODE_LEN3(VirtualAddress, pEMInfo->EMPktLen[3]);
	SET_EARLYMODE_LEN4(VirtualAddress, pEMInfo->EMPktLen[4]);
#endif	
	//RT_PRINT_DATA(COMP_SEND, DBG_LOUD, "EMHdr:", VirtualAddress, 8);

}



void UpdateEarlyModeInfo8188E(struct xmit_priv *pxmitpriv,struct xmit_buf *pxmitbuf )
{
	//_adapter *padapter, struct xmit_frame *pxmitframe,struct tx_servq	*ptxservq
	int index,j;
	u16 offset,pktlen;
	PTXDESC ptxdesc;
	
	u8 *pmem,*pEMInfo_mem;
	s8 node_num_0=0,node_num_1=0;
	struct EMInfo eminfo;
	struct agg_pkt_info *paggpkt;
	struct xmit_frame *pframe = (struct xmit_frame*)pxmitbuf->priv_data;	
	pmem= pframe->buf_addr;	
	
	#ifdef DBG_EMINFO			
	printk("\n%s ==> agg_num:%d\n",__FUNCTION__, pframe->agg_num);	
	for(index=0;index<pframe->agg_num;index++){
		offset = 	pxmitpriv->agg_pkt[index].offset;
		pktlen = pxmitpriv->agg_pkt[index].pkt_len;
		printk("%s ==> agg_pkt[%d].offset=%d\n",__FUNCTION__,index,offset);	
		printk("%s ==> agg_pkt[%d].pkt_len=%d\n",__FUNCTION__,index,pktlen);
	}
	#endif
	
	if( pframe->agg_num > EARLY_MODE_MAX_PKT_NUM)
	{	
		node_num_0 = pframe->agg_num;
		node_num_1= EARLY_MODE_MAX_PKT_NUM-1;
	}
	
	for(index=0;index<pframe->agg_num;index++){
		offset = pxmitpriv->agg_pkt[index].offset;
		pktlen = pxmitpriv->agg_pkt[index].pkt_len;		

		_rtw_memset(&eminfo,0,sizeof(struct EMInfo));
		if( pframe->agg_num > EARLY_MODE_MAX_PKT_NUM){
			if(node_num_0 > EARLY_MODE_MAX_PKT_NUM){
				eminfo.EMPktNum = EARLY_MODE_MAX_PKT_NUM;
				node_num_0--;
			}
			else{
				eminfo.EMPktNum = node_num_1;
				node_num_1--;				
			}			
		}
		else{
			eminfo.EMPktNum = pframe->agg_num-(index+1);	
		}				
		for(j=0;j< eminfo.EMPktNum ;j++){
			eminfo.EMPktLen[j] = pxmitpriv->agg_pkt[index+1+j].pkt_len+4;//CRC
		}
			
		if(pmem){
			ptxdesc = (PTXDESC)(pmem+offset);
			pEMInfo_mem = pmem+offset+TXDESC_SIZE;	
			#ifdef DBG_EMINFO
			printk("%s ==> desc.pkt_len=%d\n",__FUNCTION__,ptxdesc->pktlen);
			#endif
			InsertEMContent_8188E(&eminfo,pEMInfo_mem);
		}	
		
		
	} 
	_rtw_memset(pxmitpriv->agg_pkt,0,sizeof(struct agg_pkt_info)*MAX_AGG_PKT_NUM);

}
#endif

#endif

#ifdef CONFIG_SDIO_TX_TASKLET
static s32 xmit_xmitframes(PADAPTER padapter, struct xmit_priv *pxmitpriv)
{
	s32 ret;
	_irqL irqL;
	struct xmit_buf	*pxmitbuf;
	struct hw_xmit	*phwxmit = pxmitpriv->hwxmits;
	struct tx_servq	*ptxservq = NULL;
	_list	*xmitframe_plist = NULL, *xmitframe_phead = NULL;
	struct xmit_frame	*pxmitframe = NULL, *pfirstframe = NULL;
	u32	pbuf = 0; // next pkt address
	u32	pbuf_tail = 0; // last pkt tail
	u32	txlen = 0; //packet length, except TXDESC_SIZE and PKT_OFFSET
	u32	total_len = 0;
	u8	ac_index = 0;
	u8	bfirst = _TRUE;//first aggregation xmitframe
	u8	bulkstart = _FALSE;
#ifdef CONFIG_TX_EARLY_MODE
	u8	pkt_index=0;
#endif

	pxmitbuf = rtw_alloc_xmitbuf(pxmitpriv);
	if (pxmitbuf == NULL) {
		RT_TRACE(_module_hal_xmit_c_, _drv_err_, ("%s: xmit_buf is not enough!\n", __FUNCTION__));
		return _FALSE;
	}

	do {
		//3 1. pick up first frame
		if(bfirst)
		{
			pxmitframe = rtw_dequeue_xframe(pxmitpriv, pxmitpriv->hwxmits, pxmitpriv->hwxmit_entry);
			if (pxmitframe == NULL) {
				// no more xmit frame, release xmit buffer
				rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
				return _FALSE;
			}

			pxmitframe->pxmitbuf = pxmitbuf;
			pxmitframe->buf_addr = pxmitbuf->pbuf;
			pxmitbuf->priv_data = pxmitframe;
			pxmitbuf->ff_hwaddr = get_txfifo_hwaddr(pxmitframe);

			pfirstframe = pxmitframe;

			//_enter_critical_bh(&pxmitpriv->lock, &irqL);
			ptxservq = rtw_get_sta_pending(padapter, pfirstframe->attrib.psta, pfirstframe->attrib.priority, (u8 *)(&ac_index));
			//_exit_critical_bh(&pxmitpriv->lock, &irqL);
		}
		//3 2. aggregate same priority and same DA(AP or STA) frames
		else
		{
			// dequeue same priority packet from station tx queue
			_enter_critical_bh(&pxmitpriv->lock, &irqL);

			if (_rtw_queue_empty(&ptxservq->sta_pending) == _FALSE)
			{
				xmitframe_phead = get_list_head(&ptxservq->sta_pending);
				xmitframe_plist = get_next(xmitframe_phead);

				pxmitframe = LIST_CONTAINOR(xmitframe_plist, struct xmit_frame, list);

				// check xmit_buf size enough or not
				txlen = TXDESC_SIZE +
				#ifdef CONFIG_TX_EARLY_MODE	
					EARLY_MODE_INFO_SIZE +
				#endif
					rtw_wlan_pkt_size(pxmitframe);

				if (pbuf + _RND8(txlen) > MAX_XMITBUF_SZ)
				{
					bulkstart = _TRUE;
				}
				else
				{
					rtw_list_delete(&pxmitframe->list);
					ptxservq->qcnt--;
					phwxmit[ac_index].accnt--;

					//Remove sta node when there is no pending packets.
					if (_rtw_queue_empty(&ptxservq->sta_pending) == _TRUE)
						rtw_list_delete(&ptxservq->tx_pending);
				}
			}
			else
			{
				bulkstart = _TRUE;
			}

			_exit_critical_bh(&pxmitpriv->lock, &irqL);

			if(bulkstart)
			{
				break;
			}

			pxmitframe->buf_addr = pxmitbuf->pbuf + pbuf;

			pxmitframe->agg_num = 0; // not first frame of aggregation
		}
 
		ret = rtw_xmitframe_coalesce(padapter, pxmitframe->pkt, pxmitframe);
		if (ret == _FAIL) {
			DBG_871X("%s: coalesce FAIL!", __FUNCTION__);
			rtw_free_xmitframe_ex(pxmitpriv, pxmitframe);
			continue;
		} 

		// always return ndis_packet after rtw_xmitframe_coalesce
		//rtw_os_xmit_complete(padapter, pxmitframe);

#ifdef CONFIG_TX_EARLY_MODE
		pxmitpriv->agg_pkt[pkt_index].pkt_len = pxmitframe->attrib.last_txcmdsz;	//get from rtw_xmitframe_coalesce
		pxmitpriv->agg_pkt[pkt_index].offset = _RND8(pxmitframe->attrib.last_txcmdsz+ TXDESC_SIZE+EARLY_MODE_INFO_SIZE);
		pkt_index++;
#endif

		if(bfirst)
		{
			txlen = TXDESC_SIZE +
			#ifdef CONFIG_TX_EARLY_MODE	
				EARLY_MODE_INFO_SIZE +
			#endif
				pxmitframe->attrib.last_txcmdsz;

			total_len = txlen;

			pxmitframe->pg_num = (txlen + 127)/128;
			pxmitbuf->pg_num = (txlen + 127)/128;
			pbuf_tail = txlen;
			pbuf = _RND8(pbuf_tail);
			bfirst = _FALSE;
		}
		else
		{
			rtl8188es_update_txdesc(pxmitframe, pxmitframe->buf_addr);

			// don't need xmitframe any more
			rtw_free_xmitframe_ex(pxmitpriv, pxmitframe);

			pxmitframe->pg_num = (txlen + 127)/128;
			//pfirstframe->pg_num += pxmitframe->pg_num;
			pxmitbuf->pg_num += (txlen + 127)/128;

			total_len += txlen;

			// handle pointer and stop condition
			pbuf_tail = pbuf + txlen;
			pbuf = _RND8(pbuf_tail);

			pfirstframe->agg_num++;
			#ifdef SDIO_TX_AGG_MAX
			if(pfirstframe->agg_num >= SDIO_TX_AGG_MAX)
				break;
			#endif
		}
	}while(1);

	//3 3. update first frame txdesc
	rtl8188es_update_txdesc(pfirstframe, pfirstframe->buf_addr);
#ifdef CONFIG_TX_EARLY_MODE						
	UpdateEarlyModeInfo8188E(pxmitpriv,pxmitbuf );
#endif

	//
	pxmitbuf->agg_num = pfirstframe->agg_num;	
	pxmitbuf->priv_data = NULL;

	//3 4. write xmit buffer to USB FIFO
	pxmitbuf->len = pbuf_tail;
	enqueue_pending_xmitbuf(pxmitpriv, pxmitbuf);

	//3 5. update statisitc
	rtw_count_tx_stats(padapter, pfirstframe, total_len);


	rtw_free_xmitframe(pxmitpriv, pfirstframe);
 
	//rtw_yield_os();

	return _TRUE;
}

void rtl8188es_xmit_tasklet(void *priv)
{	
	int ret = _FALSE;
	_adapter *padapter = (_adapter*)priv;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;

	while(1)
	{
		if ((padapter->bDriverStopped == _TRUE)||(padapter->bSurpriseRemoved== _TRUE) || (padapter->bWritePortCancel == _TRUE))
		{
			DBG_871X("xmit_tasklet => bDriverStopped or bSurpriseRemoved or bWritePortCancel\n");
			break;
		}

		ret = xmit_xmitframes(padapter, pxmitpriv);
		if(ret==_FALSE)
			break;
		
	}
}
#else
static s32 xmit_xmitframes(PADAPTER padapter, struct xmit_priv *pxmitpriv)
{
	u32 err, agg_num=0;
	u8 pkt_index=0;
	struct hw_xmit *hwxmits;
	u8 no_res, idx, hwentry;
	_irqL irql;
//	_irqL irqL0, irqL1;
	struct tx_servq *ptxservq;
	_list *sta_plist, *sta_phead, *frame_plist, *frame_phead;
	struct xmit_frame *pxmitframe;
	_queue *pframe_queue;
	struct xmit_buf *pxmitbuf;
	u32 txlen;
	s32 ret;	

	err = 0;
	no_res = _FALSE;
	hwxmits = pxmitpriv->hwxmits;
	hwentry = pxmitpriv->hwxmit_entry;
	ptxservq = NULL;
	pxmitframe = NULL;
	pframe_queue = NULL;
	pxmitbuf = NULL;


	// 0(VO), 1(VI), 2(BE), 3(BK)
	for (idx = 0; idx < hwentry; idx++, hwxmits++)
	{
//		_enter_critical(&hwxmits->sta_queue->lock, &irqL0);

		sta_phead = get_list_head(hwxmits->sta_queue);
		sta_plist = get_next(sta_phead);

		while (rtw_end_of_queue_search(sta_phead, sta_plist) == _FALSE)
		{
			ptxservq = LIST_CONTAINOR(sta_plist, struct tx_servq, tx_pending);

			sta_plist = get_next(sta_plist);

			pframe_queue = &ptxservq->sta_pending;

//			_enter_critical(&pframe_queue->lock, &irqL1);
			_enter_critical_bh(&pxmitpriv->lock, &irql);

			frame_phead = get_list_head(pframe_queue);
			frame_plist = get_next(frame_phead);

			while (rtw_end_of_queue_search(frame_phead, frame_plist) == _FALSE)
			{
				pxmitframe = LIST_CONTAINOR(frame_plist, struct xmit_frame, list);				
				// check xmit_buf size enough or not
				#ifdef CONFIG_TX_EARLY_MODE		
				txlen = TXDESC_SIZE +EARLY_MODE_INFO_SIZE+ rtw_wlan_pkt_size(pxmitframe);
				#else
				txlen = TXDESC_SIZE + rtw_wlan_pkt_size(pxmitframe);
				#endif
				if ((NULL == pxmitbuf) ||
					((pxmitbuf->ptail + txlen) > pxmitbuf->pend)
				#ifdef SDIO_TX_AGG_MAX
				|| (agg_num>= SDIO_TX_AGG_MAX)
				#endif		
				)
				{
					if (pxmitbuf) {
						struct xmit_frame *pframe;
						pframe = (struct xmit_frame*)pxmitbuf->priv_data;
						pframe->agg_num = agg_num;
						pxmitbuf->agg_num = agg_num;
						//printk("==> agg_num:%d\n",agg_num);
						rtl8188es_update_txdesc(pframe, pframe->buf_addr);
						#ifdef CONFIG_TX_EARLY_MODE						
						UpdateEarlyModeInfo8188E(pxmitpriv, pxmitbuf);
						#endif
                        			rtw_free_xmitframe(pxmitpriv, pframe);
						//printk("%s #1 - pxmitbuf->len=%d enqueue,pg_num(%d)\n",__FUNCTION__,pxmitbuf->len,pxmitbuf->pg_num );
						pxmitbuf->priv_data = NULL;
						enqueue_pending_xmitbuf(pxmitpriv, pxmitbuf);
						
						//rtw_yield_os();
						
					}

					pxmitbuf = rtw_alloc_xmitbuf(pxmitpriv);
					if (pxmitbuf == NULL) {
						RT_TRACE(_module_hal_xmit_c_, _drv_err_, ("%s: xmit_buf is not enough!\n", __FUNCTION__));
						err = -2;
						break;
					}
					agg_num = 0;
					pkt_index =0;
				}

				// ok to send, remove frame from queue
						
				
				frame_plist = get_next(frame_plist);
				rtw_list_delete(&pxmitframe->list);
				ptxservq->qcnt--;
				hwxmits->accnt--;
			

				if (agg_num == 0) {
					pxmitbuf->ff_hwaddr = get_txfifo_hwaddr(pxmitframe);
					pxmitbuf->priv_data = (u8*)pxmitframe;
				}

				// coalesce the xmitframe to xmitbuf
				pxmitframe->pxmitbuf = pxmitbuf;
				pxmitframe->buf_addr = pxmitbuf->ptail;

				ret = rtw_xmitframe_coalesce(padapter, pxmitframe->pkt, pxmitframe);
				if (ret == _FAIL) {
					RT_TRACE(_module_hal_xmit_c_, _drv_err_, ("%s: coalesce FAIL!", __FUNCTION__));
					// Todo: error handler
					rtw_free_xmitframe(pxmitpriv, pxmitframe);
				} else {
					agg_num++;
					if (agg_num != 1)
						rtl8188es_update_txdesc(pxmitframe, pxmitframe->buf_addr);
					rtw_count_tx_stats(padapter, pxmitframe, pxmitframe->attrib.last_txcmdsz);
					#ifdef CONFIG_TX_EARLY_MODE		
					txlen = TXDESC_SIZE+ EARLY_MODE_INFO_SIZE+ pxmitframe->attrib.last_txcmdsz;
					#else
					txlen = TXDESC_SIZE + pxmitframe->attrib.last_txcmdsz;
					#endif
					pxmitframe->pg_num = (txlen + 127)/128;
					pxmitbuf->pg_num += (txlen + 127)/128;
					//if (agg_num != 1)
						//((struct xmit_frame*)pxmitbuf->priv_data)->pg_num += pxmitframe->pg_num;
										
					#ifdef CONFIG_TX_EARLY_MODE
					pxmitpriv->agg_pkt[pkt_index].pkt_len = pxmitframe->attrib.last_txcmdsz;	//get from rtw_xmitframe_coalesce 	
					pxmitpriv->agg_pkt[pkt_index].offset = _RND8(pxmitframe->attrib.last_txcmdsz+ TXDESC_SIZE+EARLY_MODE_INFO_SIZE);					
					#endif
					
					pkt_index++;
					pxmitbuf->ptail += _RND(txlen, 8); // round to 8 bytes alignment
					pxmitbuf->len = _RND(pxmitbuf->len, 8) + txlen;
				}

				if (agg_num != 1)
					rtw_free_xmitframe(pxmitpriv, pxmitframe);
				pxmitframe = NULL;			
			}

			if (_rtw_queue_empty(pframe_queue)) {				
				rtw_list_delete(&ptxservq->tx_pending);
			}

//			_exit_critical(&pframe_queue->lock, &irqL1);
			_exit_critical_bh(&pxmitpriv->lock, &irql);

		}

//		_exit_critical(&hwxmits->sta_queue->lock, &irqL0);

		// dump xmit_buf to hw tx fifo
		if (pxmitbuf)
		{
			RT_TRACE(_module_hal_xmit_c_, _drv_notice_, ("pxmitbuf->len=%d enqueue\n",pxmitbuf->len));
		
			if (pxmitbuf->len > 0) {
				struct xmit_frame *pframe;
				pframe = (struct xmit_frame*)pxmitbuf->priv_data;
				pframe->agg_num = agg_num;
				pxmitbuf->agg_num = agg_num;
				rtl8188es_update_txdesc(pframe, pframe->buf_addr);
				#ifdef CONFIG_TX_EARLY_MODE				
				UpdateEarlyModeInfo8188E(pxmitpriv,pxmitbuf );
				#endif
				rtw_free_xmitframe(pxmitpriv, pframe);
				//printk("%s #2 - pxmitbuf->len=%d enqueue,pg_num(%d)\n",__FUNCTION__,pxmitbuf->len,pxmitbuf->pg_num );
				pxmitbuf->priv_data = NULL;
				enqueue_pending_xmitbuf(pxmitpriv, pxmitbuf);				
				rtw_yield_os();
			}
			else
				rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
			pxmitbuf = NULL;
		}
	}

	return err;
}

/*
 * Description
 *	Transmit xmitframe from queue
 *
 * Return
 *	_SUCCESS	ok
 *	_FAIL		something error
 */
s32 rtl8188es_xmit_handler(PADAPTER padapter)
{
	struct xmit_priv *pxmitpriv;
	s32 ret;
	_irqL irql;

	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	pxmitpriv = &padapter->xmitpriv;
	

wait:
	ret = _rtw_down_sema(&pHalData->SdioXmitSema);
	if (_FAIL == ret) {
		RT_TRACE(_module_hal_xmit_c_, _drv_emerg_, ("%s: down sema fail!\n", __FUNCTION__));
		return _FAIL;
	}

next:
	if ((padapter->bDriverStopped == _TRUE) ||
		(padapter->bSurpriseRemoved == _TRUE)) {
		RT_TRACE(_module_hal_xmit_c_, _drv_notice_,
				 ("%s: bDriverStopped(%d) bSurpriseRemoved(%d)\n",
				  __FUNCTION__, padapter->bDriverStopped, padapter->bSurpriseRemoved));
		return _FAIL;
	}

	_enter_critical_bh(&pxmitpriv->lock, &irql);
	ret = rtw_txframes_pending(padapter);
	_exit_critical_bh(&pxmitpriv->lock, &irql);
	if (ret == 0) {
		return _SUCCESS;
	}

	// dequeue frame and write to hardware

	ret = xmit_xmitframes(padapter, pxmitpriv);
	if (ret == -2) {
		rtw_msleep_os(1);
		goto next;
	}

	_enter_critical_bh(&pxmitpriv->lock, &irql);
	ret = rtw_txframes_pending(padapter);
	_exit_critical_bh(&pxmitpriv->lock, &irql);
	if (ret == 1) {
		rtw_msleep_os(1);
		goto next;
	}

	return _SUCCESS;
}

thread_return rtl8188es_xmit_thread(thread_context context)
{
	s32 ret;
	PADAPTER padapter= (PADAPTER)context;	
	struct xmit_priv *pxmitpriv= &padapter->xmitpriv;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	
	ret = _SUCCESS;

	thread_enter("RTWHALXT");

	DBG_871X("start %s\n", __FUNCTION__);

	do {
		ret = rtl8188es_xmit_handler(padapter);
		if (signal_pending(current)) {
			flush_signals(current);
		}
	} while (_SUCCESS == ret);

	_rtw_up_sema(&pHalData->SdioXmitTerminateSema);

	RT_TRACE(_module_hal_xmit_c_, _drv_notice_, ("-%s\n", __FUNCTION__));
	DBG_871X("exit %s\n", __FUNCTION__);

	thread_exit();
}
#endif

void rtl8188es_mgnt_xmit(PADAPTER padapter, struct xmit_frame *pmgntframe)
{	
	struct pkt_attrib *pattrib;
	struct xmit_buf *pxmitbuf;
	struct xmit_priv *pxmitpriv = &padapter->xmitpriv;
	u8 *pframe = (u8 *)(pmgntframe->buf_addr) + TXDESC_OFFSET;
	
	RT_TRACE(_module_hal_xmit_c_, _drv_info_, ("+%s\n", __FUNCTION__));

	pattrib = &pmgntframe->attrib;
	pxmitbuf = pmgntframe->pxmitbuf;

	rtl8188es_update_txdesc(pmgntframe, pmgntframe->buf_addr);

	pxmitbuf->len = TXDESC_SIZE + pattrib->last_txcmdsz;
	//pmgntframe->pg_num = (pxmitbuf->len + 127)/128; // 128 is tx page size
	pxmitbuf->pg_num = (pxmitbuf->len + 127)/128; // 128 is tx page size
	pxmitbuf->ptail = pmgntframe->buf_addr + pxmitbuf->len;
	pxmitbuf->ff_hwaddr = get_txfifo_hwaddr(pmgntframe);

	rtw_count_tx_stats(padapter, pmgntframe, pattrib->last_txcmdsz);

	rtw_free_xmitframe(pxmitpriv, pmgntframe);

	pxmitbuf->priv_data = NULL;
	
	if(GetFrameSubType(pframe)==WIFI_BEACON) //dump beacon directly
	{	
		rtw_write_port(padapter, pxmitbuf->ff_hwaddr, pxmitbuf->len, pxmitbuf->pdata);
		
		//rtw_free_xmitframe(pxmitpriv, pmgntframe);
		
		//pxmitbuf->priv_data = NULL;
		
		rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
	}		
	else	
	{
		enqueue_pending_xmitbuf(&padapter->xmitpriv, pxmitbuf);
	}
	
}

/*
 * Description:
 *	Handle xmitframe(packet) come from rtw_xmit()
 *
 * Return:
 *	_TRUE	dump packet directly ok
 *	_FALSE	enqueue, temporary can't transmit packets to hardware
 */
s32 rtl8188es_hal_xmit(PADAPTER padapter, struct xmit_frame *pxmitframe)
{
	struct xmit_priv 	*pxmitpriv = &padapter->xmitpriv;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	_irqL irql;
	s32 err;

	//pxmitframe->attrib.qsel = pxmitframe->attrib.priority;

#ifdef CONFIG_80211N_HT
	if ((pxmitframe->frame_tag == DATA_FRAMETAG) &&
		(pxmitframe->attrib.ether_type != 0x0806) &&
		(pxmitframe->attrib.ether_type != 0x888e) &&
		(pxmitframe->attrib.dhcp_pkt != 1))
	{
		if (padapter->mlmepriv.LinkDetectInfo.bBusyTraffic == _TRUE)
			rtw_issue_addbareq_cmd(padapter, pxmitframe);
	}
#endif

	_enter_critical_bh(&pxmitpriv->lock, &irql);
	err = rtw_xmitframe_enqueue(padapter, pxmitframe);
	_exit_critical_bh(&pxmitpriv->lock, &irql);
	if (err != _SUCCESS) {
		RT_TRACE(_module_hal_xmit_c_, _drv_err_, ("%s: enqueue xmitframe fail\n",__FUNCTION__));
		rtw_free_xmitframe_ex(pxmitpriv, pxmitframe);

		// Trick, make the statistics correct
		pxmitpriv->tx_pkts--;
		pxmitpriv->tx_drop++;
		return _TRUE;
	}

#ifdef CONFIG_SDIO_TX_TASKLET
	tasklet_hi_schedule(&pxmitpriv->xmit_tasklet);
#else
	_rtw_up_sema(&pHalData->SdioXmitSema);
#endif

	return _FALSE;
}

/*
 * Return
 *	_SUCCESS	start thread ok
 *	_FAIL		start thread fail
 *
 */
s32 rtl8188es_init_xmit_priv(PADAPTER padapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

#ifdef CONFIG_SDIO_TX_TASKLET
	struct xmit_priv 	*pxmitpriv = &padapter->xmitpriv;

#ifdef PLATFORM_LINUX
	tasklet_init(&pxmitpriv->xmit_tasklet,
	     (void(*)(unsigned long))rtl8188es_xmit_tasklet,
	     (unsigned long)padapter);
#endif

#else //CONFIG_SDIO_TX_TASKLET

	_rtw_init_sema(&pHalData->SdioXmitSema, 0);
	_rtw_init_sema(&pHalData->SdioXmitTerminateSema, 0);
#ifdef PLATFORM_LINUX
	pHalData->SdioXmitThread = kernel_thread(rtl8188es_xmit_thread, padapter, CLONE_FS|CLONE_FILES);
	if (pHalData->SdioXmitThread < 0) {
		RT_TRACE(_module_hal_xmit_c_, _drv_err_, ("%s: start rtl8188es_xmit_thread FAIL!!\n", __FUNCTION__));
		return _FAIL;
	}
#else
#error "can not create SdioXmitThread!\n"
#endif

#endif //CONFIG_SDIO_TX_TASKLET

	_rtw_spinlock_init(&pHalData->SdioTxFIFOFreePageLock);

#ifdef CONFIG_TX_EARLY_MODE
	pHalData->bEarlyModeEnable = padapter->registrypriv.early_mode;
#endif

	return _SUCCESS;
}

void rtl8188es_free_xmit_priv(PADAPTER padapter)
{
	
	struct xmit_buf *pxmitbuf;
	_queue *pqueue;
	_list *plist, *phead;
	_list tmplist;
	_irqL irql;
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(padapter);
	struct xmit_priv  *pxmitpriv =  &padapter->xmitpriv;
	
	pqueue = &pxmitpriv->pending_xmitbuf_queue;
	phead = get_list_head(pqueue);
	_rtw_init_listhead(&tmplist);

	_enter_critical_bh(&pqueue->lock, &irql);
	if (_rtw_queue_empty(pqueue) == _FALSE)
	{
		// Insert tmplist to end of queue, and delete phead
		// then tmplist become head of queue.
		rtw_list_insert_tail(&tmplist, phead);
		rtw_list_delete(phead);
	}
	_exit_critical_bh(&pqueue->lock, &irql);

	phead = &tmplist;
	while (rtw_is_list_empty(phead) == _FALSE)
	{
		plist = get_next(phead);
		rtw_list_delete(plist);

		pxmitbuf = LIST_CONTAINOR(plist, struct xmit_buf, list);
		rtw_free_xmitframe(pxmitpriv, (struct xmit_frame*)pxmitbuf->priv_data);
		pxmitbuf->priv_data = NULL;
		rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
	}

#ifndef CONFIG_SDIO_TX_TASKLET
	// stop xmit_buf_thread
	if (pHalData->SdioXmitThread >= 0) {
		_rtw_up_sema(&pHalData->SdioXmitSema);
		_rtw_down_sema(&pHalData->SdioXmitTerminateSema);
		pHalData->SdioXmitThread = -1;
	}
#endif

	_rtw_spinlock_free(&pHalData->SdioTxFIFOFreePageLock);
}

