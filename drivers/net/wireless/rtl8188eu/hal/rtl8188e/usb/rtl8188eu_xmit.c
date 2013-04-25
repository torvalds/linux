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
#define _RTL8188E_XMIT_C_
#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <rtw_byteorder.h>
#include <wifi.h>
#include <osdep_intf.h>
#include <circ_buf.h>
#include <usb_ops.h>
#include <rtl8188e_hal.h>

#if defined (PLATFORM_LINUX) && defined (PLATFORM_WINDOWS)
#error "Shall be Linux or Windows, but not both!\n"
#endif

s32	rtl8188eu_init_xmit_priv(_adapter *padapter)
{
	struct xmit_priv	*pxmitpriv = &padapter->xmitpriv;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

#ifdef PLATFORM_LINUX
	tasklet_init(&pxmitpriv->xmit_tasklet,
	     (void(*)(unsigned long))rtl8188eu_xmit_tasklet,
	     (unsigned long)padapter);
#endif
#ifdef CONFIG_TX_EARLY_MODE
	pHalData->bEarlyModeEnable = padapter->registrypriv.early_mode;
#endif

	return _SUCCESS;
}

void	rtl8188eu_free_xmit_priv(_adapter *padapter)
{
}

u32 rtw_get_ff_hwaddr(struct xmit_frame	*pxmitframe)
{
	u32 addr;
	struct pkt_attrib *pattrib = &pxmitframe->attrib;	
	
	switch(pattrib->qsel)
	{
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
		case 0x11://BC/MC in PS (HIQ)
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

u8 urb_zero_packet_chk(_adapter *padapter, int sz)
{
#if 1
	u8 blnSetTxDescOffset;
	HAL_DATA_TYPE	*pHalData	= GET_HAL_DATA(padapter);
	blnSetTxDescOffset = (((sz + TXDESC_SIZE) %  pHalData->UsbBulkOutSize) ==0)?1:0;
	
#else

	struct dvobj_priv	*pdvobj = (struct dvobj_priv*)&padapter->dvobjpriv;	
	if ( pdvobj->ishighspeed )
	{
		if ( ( (sz + TXDESC_SIZE) % 512 ) == 0 ) {
			blnSetTxDescOffset = 1;
		} else {
			blnSetTxDescOffset = 0;
		}
	}
	else
	{
		if ( ( (sz + TXDESC_SIZE) % 64 ) == 0 ) 	{
			blnSetTxDescOffset = 1;
		} else {
			blnSetTxDescOffset = 0;
		}
	}
#endif	
	return blnSetTxDescOffset;
	
}

void rtl8188eu_cal_txdesc_chksum(struct tx_desc	*ptxdesc)
{
		u16	*usPtr = (u16*)ptxdesc;
		u32 count = 16;		// (32 bytes / 2 bytes per XOR) => 16 times
		u32 index;
		u16 checksum = 0;

		//Clear first
		ptxdesc->txdw7 &= cpu_to_le32(0xffff0000);
	
		for(index = 0 ; index < count ; index++){
			checksum = checksum ^ le16_to_cpu(*(usPtr + index));
		}

		ptxdesc->txdw7 |= cpu_to_le32(0x0000ffff&checksum);	

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
	rtl8188eu_cal_txdesc_chksum(ptxdesc);
#endif
}

void fill_txdesc_sectype(struct pkt_attrib *pattrib, struct tx_desc *ptxdesc)
{
	if ((pattrib->encrypt > 0) && !pattrib->bswenc)
	{
		switch (pattrib->encrypt)
		{	
			//SEC_TYPE : 0:NO_ENC,1:WEP40/TKIP,2:WAPI,3:AES
			case _WEP40_:
			case _WEP104_:
					ptxdesc->txdw1 |= cpu_to_le32((0x01<<SEC_TYPE_SHT)&0x00c00000);
					ptxdesc->txdw2 |= cpu_to_le32(0x7 << AMPDU_DENSITY_SHT);
					break;				
			case _TKIP_:
			case _TKIP_WTMIC_:	
					ptxdesc->txdw1 |= cpu_to_le32((0x01<<SEC_TYPE_SHT)&0x00c00000);
					ptxdesc->txdw2 |= cpu_to_le32(0x7 << AMPDU_DENSITY_SHT);
					break;
			case _AES_:
					ptxdesc->txdw1 |= cpu_to_le32((0x03<<SEC_TYPE_SHT)&0x00c00000);
					ptxdesc->txdw2 |= cpu_to_le32(0x7 << AMPDU_DENSITY_SHT);
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
			*pdw |= cpu_to_le32(RTS_EN);
			break;
		case CTS_TO_SELF:
			*pdw |= cpu_to_le32(CTS_2_SELF);
			break;
		case NONE_VCS:
		default:
			break;		
	}

	if(pattrib->vcs_mode)
	{
		*pdw |= cpu_to_le32(HW_RTS_EN);//ENABLE HW RTS
	}

}

void fill_txdesc_phy(struct pkt_attrib *pattrib, u32 *pdw)
{
	//DBG_8192C("bwmode=%d, ch_off=%d\n", pattrib->bwmode, pattrib->ch_offset);

	if(pattrib->ht_en)
	{
		*pdw |= (pattrib->bwmode&HT_CHANNEL_WIDTH_40)?	cpu_to_le32(BIT(25)):0;

		if(pattrib->ch_offset == HAL_PRIME_CHNL_OFFSET_LOWER)
			*pdw |= cpu_to_le32((0x01<<DATA_SC_SHT)&0x003f0000);		
		else if(pattrib->ch_offset == HAL_PRIME_CHNL_OFFSET_UPPER)
			*pdw |= cpu_to_le32((0x02<<DATA_SC_SHT)&0x003f0000);
		else if(pattrib->ch_offset == HAL_PRIME_CHNL_OFFSET_DONT_CARE)
			*pdw |= 0;
		else
			*pdw |= cpu_to_le32((0x03<<DATA_SC_SHT)&0x003f0000);
	}
}


static s32 update_txdesc(struct xmit_frame *pxmitframe, u8 *pmem, s32 sz ,u8 bagg_pkt)
{	
      int	pull=0;
	uint	qsel;
	u8 data_rate,pwr_status,offset;
	_adapter			*padapter = pxmitframe->padapter;
	struct mlme_priv	*pmlmepriv = &padapter->mlmepriv;		
	struct pkt_attrib	*pattrib = &pxmitframe->attrib;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	struct tx_desc	*ptxdesc = (struct tx_desc *)pmem;
	struct ht_priv		*phtpriv = &pmlmepriv->htpriv;
	struct mlme_ext_info	*pmlmeinfo = &padapter->mlmeextpriv.mlmext_info;
	sint	bmcst = IS_MCAST(pattrib->ra);
	
#ifdef CONFIG_P2P
	struct wifidirect_info*	pwdinfo = &padapter->wdinfo;
#endif //CONFIG_P2P

#ifndef CONFIG_USE_USB_BUFFER_ALLOC_TX
	if((!bagg_pkt) &&(urb_zero_packet_chk(padapter, sz)==0))//(sz %512) != 0
	{
		ptxdesc = (struct tx_desc *)(pmem+PACKET_OFFSET_SZ);
		//printk("==> non-agg-pkt,shift pointer...\n");
		pull = 1;
	}
#endif	// CONFIG_USE_USB_BUFFER_ALLOC_TX

	_rtw_memset(ptxdesc, 0, sizeof(struct tx_desc));
	
        //4 offset 0
	ptxdesc->txdw0 |= cpu_to_le32(OWN | FSG | LSG);
	//printk("%s==> pkt_len=%d,bagg_pkt=%02x\n",__FUNCTION__,sz,bagg_pkt);
	ptxdesc->txdw0 |= cpu_to_le32(sz & 0x0000ffff);//update TXPKTSIZE
	
	offset = TXDESC_SIZE + OFFSET_SZ;		

	#ifdef CONFIG_TX_EARLY_MODE	
	if(bagg_pkt){		
		offset += EARLY_MODE_INFO_SIZE ;//0x28			
	}
	#endif
	//printk("%s==>offset(0x%02x)  \n",__FUNCTION__,offset);
	ptxdesc->txdw0 |= cpu_to_le32(((offset) << OFFSET_SHT) & 0x00ff0000);//32 bytes for TX Desc

	if (bmcst) ptxdesc->txdw0 |= cpu_to_le32(BMC);

#ifndef CONFIG_USE_USB_BUFFER_ALLOC_TX
	if(!bagg_pkt){
		if((pull) && (pxmitframe->pkt_offset>0)) {	
			pxmitframe->pkt_offset = pxmitframe->pkt_offset -1;		
		}
	}
#endif
	//printk("%s, pkt_offset=0x%02x\n",__FUNCTION__,pxmitframe->pkt_offset);

	// pkt_offset, unit:8 bytes padding
	if (pxmitframe->pkt_offset > 0)
		ptxdesc->txdw1 |= cpu_to_le32((pxmitframe->pkt_offset << 26) & 0x7c000000);

	//driver uses rate
	ptxdesc->txdw4 |= cpu_to_le32(USERATE);//rate control always by driver

	if((pxmitframe->frame_tag&0x0f) == DATA_FRAMETAG)
	{
		//DBG_8192C("pxmitframe->frame_tag == DATA_FRAMETAG\n");		

		//offset 4
		ptxdesc->txdw1 |= cpu_to_le32(pattrib->mac_id&0x3F);

		qsel = (uint)(pattrib->qsel & 0x0000001f);
		//printk("==> macid(%d) qsel:0x%02x \n",pattrib->mac_id,qsel);
		ptxdesc->txdw1 |= cpu_to_le32((qsel << QSEL_SHT) & 0x00001f00);

		ptxdesc->txdw1 |= cpu_to_le32((pattrib->raid<< RATE_ID_SHT) & 0x000F0000);

		fill_txdesc_sectype(pattrib, ptxdesc);

		if(pattrib->ampdu_en==_TRUE){
			ptxdesc->txdw2 |= cpu_to_le32(AGG_EN);//AGG EN
		
			//SET_TX_DESC_MAX_AGG_NUM_88E(pDesc, 0x1F);
			//SET_TX_DESC_MCSG1_MAX_LEN_88E(pDesc, 0x6);
			//SET_TX_DESC_MCSG2_MAX_LEN_88E(pDesc, 0x6);
			//SET_TX_DESC_MCSG3_MAX_LEN_88E(pDesc, 0x6);
			//SET_TX_DESC_MCS7_SGI_MAX_LEN_88E(pDesc, 0x6);
			ptxdesc->txdw6 = 0x6666f800;		
		}
		else{
			ptxdesc->txdw2 |= cpu_to_le32(AGG_BK);//AGG BK
		}
		
		//offset 8
		

		//offset 12
		ptxdesc->txdw3 |= cpu_to_le32((pattrib->seqnum<< SEQ_SHT)&0x0FFF0000);


		//offset 16 , offset 20
		if (pattrib->qos_en)
			ptxdesc->txdw4 |= cpu_to_le32(QOS);//QoS

		//offset 20
	#ifdef CONFIG_USB_TX_AGGREGATION
		if (pxmitframe->agg_num > 1){
			//printk("%s agg_num:%d\n",__FUNCTION__,pxmitframe->agg_num );
			ptxdesc->txdw5 |= cpu_to_le32((pxmitframe->agg_num << USB_TXAGG_NUM_SHT) & 0xFF000000);
		}
	#endif

		if ((pattrib->ether_type != 0x888e) &&
		    (pattrib->ether_type != 0x0806) &&
		    (pattrib->dhcp_pkt != 1))
		{
			//Non EAP & ARP & DHCP type data packet
              	
			fill_txdesc_vcs(pattrib, &ptxdesc->txdw4);
			fill_txdesc_phy(pattrib, &ptxdesc->txdw4);

			ptxdesc->txdw4 |= cpu_to_le32(0x00000008);//RTS Rate=24M
			ptxdesc->txdw5 |= cpu_to_le32(0x0001ff00);//DATA/RTS  Rate FB LMT			

	#if (RATE_ADAPTIVE_SUPPORT == 1)		
			if(pattrib->ht_en){
				if( ODM_RA_GetShortGI_8188E(&pHalData->odmpriv,pattrib->mac_id))
					ptxdesc->txdw5 |= cpu_to_le32(SGI);//SGI
			}
				
			data_rate =ODM_RA_GetDecisionRate_8188E(&pHalData->odmpriv,pattrib->mac_id);			
			ptxdesc->txdw5 |= cpu_to_le32(data_rate & 0x3F);
			
               	//for debug
               	#if 0
               	 if(padapter->fix_rate!= 0xFF){
				ptxdesc->datarate = padapter->fix_rate;
			}
                   #endif

			#if (POWER_TRAINING_ACTIVE==1)
			pwr_status = ODM_RA_GetHwPwrStatus_8188E(&pHalData->odmpriv,pattrib->mac_id);
			ptxdesc->txdw4 |=cpu_to_le32( (pwr_status & 0x7)<< PWR_STATUS_SHT);
			#endif		   
	#else//if (RATE_ADAPTIVE_SUPPORT == 1)	
				
			if(pattrib->ht_en)
				ptxdesc->txdw5 |= cpu_to_le32(SGI);//SGI

			data_rate = 0x13; //default rate: MCS7									
			 if(padapter->fix_rate!= 0xFF){//rate control by iwpriv
				data_rate = padapter->fix_rate;				
			}
			ptxdesc->txdw5 |= cpu_to_le32(data_rate & 0x3F); 

	#endif//if (RATE_ADAPTIVE_SUPPORT == 1)				

		}
		else
		{
			// EAP data packet and ARP packet and DHCP.
			// Use the 1M data rate to send the EAP/ARP packet.
			// This will maybe make the handshake smooth.

			ptxdesc->txdw2 |= cpu_to_le32(AGG_BK);//AGG BK		   

#ifdef CONFIG_P2P
			//	Added by Albert 2011/03/22
			//	In the P2P mode, the driver should not support the b mode.
			//	So, the Tx packet shouldn't use the CCK rate		
			if(!rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
			{
				data_rate = 0x04;//	Use the 6M data rate.
				ptxdesc->txdw5 |=  cpu_to_le32(data_rate & 0x3F); 
			}
#endif //CONFIG_P2P		

		}
		
#ifdef CONFIG_TCP_CSUM_OFFLOAD_TX
		//offset 24
		if ( pattrib->hw_tcp_csum == 1 ) {
			// ptxdesc->txdw6 = 0; // clear TCP_CHECKSUM and IP_CHECKSUM. It's zero already!!
			u8 ip_hdr_offset = 32 + pattrib->hdrlen + pattrib->iv_len + 8;
			ptxdesc->txdw7 = (1 << 31) | (ip_hdr_offset << 16);
			DBG_8192C("ptxdesc->txdw7 = %08x\n", ptxdesc->txdw7);
		}
#endif	

	}
	else if((pxmitframe->frame_tag&0x0f)== MGNT_FRAMETAG)
	{
		//DBG_8192C("pxmitframe->frame_tag == MGNT_FRAMETAG\n");	
		
		//offset 4		
		ptxdesc->txdw1 |= cpu_to_le32(pattrib->mac_id&0x3f);
		
		qsel = (uint)(pattrib->qsel&0x0000001f);
		ptxdesc->txdw1 |= cpu_to_le32((qsel<<QSEL_SHT)&0x00001f00);

		ptxdesc->txdw1 |= cpu_to_le32((pattrib->raid<< RATE_ID_SHT) & 0x000f0000);
		
		//fill_txdesc_sectype(pattrib, ptxdesc);
		
		//offset 8		

		//offset 12
		ptxdesc->txdw3 |= cpu_to_le32((pattrib->seqnum<<SEQ_SHT)&0x0FFF0000);
				
		//offset 20
		ptxdesc->txdw5 |= cpu_to_le32(RTY_LMT_EN);//retry limit enable
		if(pattrib->retry_ctrl == _TRUE)
			ptxdesc->txdw5 |= cpu_to_le32(0x00180000);//retry limit = 6
		else
			ptxdesc->txdw5 |= cpu_to_le32(0x00300000);//retry limit = 12
			
#ifdef CONFIG_P2P
		//	Added by Albert 2011/03/17
		//	In the P2P mode, the driver should not support the b mode.
		//	So, the Tx packet shouldn't use the CCK rate
		if(!rtw_p2p_chk_state(pwdinfo, P2P_STATE_NONE))
		{
			data_rate = 0x04;//	Use the 6M data rate.
			ptxdesc->txdw5 |=  cpu_to_le32(data_rate & 0x3F); 
		}
#endif //CONFIG_P2P

#ifdef CONFIG_INTEL_PROXIM
		if((padapter->proximity.proxim_on==_TRUE)&&(pattrib->intel_proxim==_TRUE)){
			DBG_871X("\n %s pattrib->rate=%d\n",__FUNCTION__,pattrib->rate);
			ptxdesc->txdw5 |= cpu_to_le32( pattrib->rate);
		}
#endif
	}
	else if((pxmitframe->frame_tag&0x0f) == TXAGG_FRAMETAG)
	{
		DBG_8192C("pxmitframe->frame_tag == TXAGG_FRAMETAG\n");
	}
#ifdef CONFIG_MP_INCLUDED
	else if((pxmitframe->frame_tag&0x0f) == MP_FRAMETAG)
	{
		fill_txdesc_for_mp(padapter, ptxdesc);
	}
#endif
	else
	{
		DBG_8192C("pxmitframe->frame_tag = %d\n", pxmitframe->frame_tag);
		
		//offset 4	
		ptxdesc->txdw1 |= cpu_to_le32((4)&0x3f);//CAM_ID(MAC_ID)
		
		ptxdesc->txdw1 |= cpu_to_le32((6<< RATE_ID_SHT) & 0x000f0000);//raid
		
		//offset 8		

		//offset 12
		ptxdesc->txdw3 |= cpu_to_le32((pattrib->seqnum<<SEQ_SHT)&0x0fff0000);
				
		
		//offset 20
	}

	// 2009.11.05. tynli_test. Suggested by SD4 Filen for FW LPS.
	// (1) The sequence number of each non-Qos frame / broadcast / multicast /
	// mgnt frame should be controled by Hw because Fw will also send null data
	// which we cannot control when Fw LPS enable.
	// --> default enable non-Qos data sequense number. 2010.06.23. by tynli.
	// (2) Enable HW SEQ control for beacon packet, because we use Hw beacon.
	// (3) Use HW Qos SEQ to control the seq num of Ext port non-Qos packets.
	// 2010.06.23. Added by tynli.
	if(!pattrib->qos_en)
	{		
		//ptxdesc->txdw4 |= cpu_to_le32(BIT(7)); // Hw set sequence number
		//ptxdesc->txdw3 |= cpu_to_le32((8 <<28)); //set bit3 to 1. Suugested by TimChen. 2009.12.29.

		ptxdesc->txdw3 |= cpu_to_le32(EN_HWSEQ); // Hw set sequence number
		ptxdesc->txdw4 |= cpu_to_le32(HW_SSN); 	// Hw set sequence number
		
	}

#ifdef CONFIG_HW_ANTENNA_DIVERSITY //CONFIG_ANTENNA_DIVERSITY 
	ODM_SetTxAntByTxInfo_88E(&pHalData->odmpriv, pmem, pattrib->mac_id);
#endif
	
	rtl8188eu_cal_txdesc_chksum(ptxdesc);
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
s32 rtl8188eu_xmit_buf_handler(PADAPTER padapter)
{
	PHAL_DATA_TYPE phal;
	struct xmit_priv *pxmitpriv;
	struct xmit_buf *pxmitbuf;
	s32 ret;


	phal = GET_HAL_DATA(padapter);
	pxmitpriv = &padapter->xmitpriv;

	ret = _rtw_down_sema(&pxmitpriv->xmit_sema);
	if (_FAIL == ret) {
		RT_TRACE(_module_hal_xmit_c_, _drv_emerg_,
				 ("%s: down SdioXmitBufSema fail!\n", __FUNCTION__));
		return _FAIL;
	}

	ret = (padapter->bDriverStopped == _TRUE) || (padapter->bSurpriseRemoved == _TRUE);
	if (ret) {
		RT_TRACE(_module_hal_xmit_c_, _drv_notice_,
				 ("%s: bDriverStopped(%d) bSurpriseRemoved(%d)!\n",
				  __FUNCTION__, padapter->bDriverStopped, padapter->bSurpriseRemoved));
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

		ret = (padapter->bDriverStopped == _TRUE) || (padapter->bSurpriseRemoved == _TRUE);
		if (ret) {
			RT_TRACE(_module_hal_xmit_c_, _drv_notice_,
					 ("%s: bDriverStopped(%d) bSurpriseRemoved(%d)!\n",
					  __FUNCTION__, padapter->bDriverStopped, padapter->bSurpriseRemoved));
			pxmitbuf->priv_data = NULL;
			rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
			continue;
		}

		if(pxmitbuf->isSync)
		{
			rtw_write_port_sync(padapter, pxmitbuf->ff_hwaddr, pxmitbuf->len, (unsigned char*)pxmitbuf);
		}
		else
		{
			rtw_write_port(padapter, pxmitbuf->ff_hwaddr, pxmitbuf->len, (unsigned char*)pxmitbuf);
			rtw_yield_os();
		}

	} while (1);

#ifdef CONFIG_LPS_LCLK
	rtw_unregister_tx_alive(padapter);
#endif

	return _SUCCESS;
}
#endif


//for non-agg data frame or  management frame
static void _rtw_dump_xframe(_adapter *padapter, struct xmit_frame *pxmitframe, u8 sync)
{
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
            	
            	pull = update_txdesc(pxmitframe, mem_addr, sz,_FALSE);
             
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
		if(sync == _TRUE)
			pxmitbuf->isSync = _TRUE;
		else
			pxmitbuf->isSync = _FALSE;
		enqueue_pending_xmitbuf(pxmitpriv, pxmitbuf);
#else
		if(sync == _TRUE)
			rtw_write_port_sync(padapter, ff_hwaddr, w_sz, (unsigned char*)pxmitbuf);
		else
			rtw_write_port(padapter, ff_hwaddr, w_sz, (unsigned char*)pxmitbuf);
#endif

		rtw_count_tx_stats(padapter, pxmitframe, sz);

		RT_TRACE(_module_rtl871x_xmit_c_,_drv_info_,("rtw_write_port, w_sz=%d\n", w_sz));
		//DBG_8192C("rtw_write_port, w_sz=%d, sz=%d, txdesc_sz=%d, tid=%d\n", w_sz, sz, w_sz-sz, pattrib->priority);      

		mem_addr += w_sz;

		mem_addr = (u8 *)RND4(((SIZE_PTR)(mem_addr)));

	}
	
	rtw_free_xmitframe_ex(pxmitpriv, pxmitframe);
	
}

inline void rtw_dump_xframe(_adapter *padapter, struct xmit_frame *pxmitframe)
{
	_rtw_dump_xframe(padapter, pxmitframe, _FALSE);
}

inline void rtw_dump_xframe_sync(_adapter *padapter, struct xmit_frame *pxmitframe)
{
	_rtw_dump_xframe(padapter, pxmitframe, _TRUE);
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
s32 rtl8188eu_xmitframe_complete(_adapter *padapter, struct xmit_priv *pxmitpriv, struct xmit_buf *pxmitbuf)
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

#ifndef IDEA_CONDITION
	int res = _SUCCESS;
#endif

	RT_TRACE(_module_rtl8192c_xmit_c_, _drv_info_, ("+xmitframe_complete\n"));


	// check xmitbuffer is ok
	if (pxmitbuf == NULL) {
		pxmitbuf = rtw_alloc_xmitbuf(pxmitpriv);
		if (pxmitbuf == NULL){
			//DBG_871X("%s #1, connot alloc xmitbuf!!!! \n",__FUNCTION__);
			return _FALSE;
		}
	}

//printk("%s ===================================== \n",__FUNCTION__);
	//3 1. pick up first frame
	do {
		rtw_free_xmitframe_ex(pxmitpriv, pxmitframe);
			
		pxmitframe = rtw_dequeue_xframe(pxmitpriv, pxmitpriv->hwxmits, pxmitpriv->hwxmit_entry);
		if (pxmitframe == NULL) {
			// no more xmit frame, release xmit buffer
			//printk("no more xmit frame ,return\n");
			rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
			return _FALSE;
		}

#ifndef IDEA_CONDITION
		if (pxmitframe->frame_tag != DATA_FRAMETAG) {
			RT_TRACE(_module_rtl8192c_xmit_c_, _drv_err_,
				 ("xmitframe_complete: frame tag(%d) is not DATA_FRAMETAG(%d)!\n",
				  pxmitframe->frame_tag, DATA_FRAMETAG));
//			rtw_free_xmitframe_ex(pxmitpriv, pxmitframe);
			continue;
		}

		// TID 0~15
		if ((pxmitframe->attrib.priority < 0) ||
		    (pxmitframe->attrib.priority > 15)) {
			RT_TRACE(_module_rtl8192c_xmit_c_, _drv_err_,
				 ("xmitframe_complete: TID(%d) should be 0~15!\n",
				  pxmitframe->attrib.priority));
//			rtw_free_xmitframe_ex(pxmitpriv, pxmitframe);
			continue;
		}
#endif
		//printk("==> pxmitframe->attrib.priority:%d\n",pxmitframe->attrib.priority);	
		pxmitframe->pxmitbuf = pxmitbuf;
		pxmitframe->buf_addr = pxmitbuf->pbuf;
		pxmitbuf->priv_data = pxmitframe;

		pxmitframe->agg_num = 1; // alloc xmitframe should assign to 1.
		#ifdef CONFIG_TX_EARLY_MODE
		pxmitframe->pkt_offset = 2; // first frame of aggregation, reserve one offset for EM info ,another for usb bulk-out block check
		#else
		pxmitframe->pkt_offset = 1; // first frame of aggregation, reserve offset
		#endif

#ifdef IDEA_CONDITION
		rtw_xmitframe_coalesce(padapter, pxmitframe->pkt, pxmitframe);
#else
		res = rtw_xmitframe_coalesce(padapter, pxmitframe->pkt, pxmitframe);
		if (res == _FALSE) {
//			rtw_free_xmitframe_ex(pxmitpriv, pxmitframe);
			continue;
		}
#endif

		// always return ndis_packet after rtw_xmitframe_coalesce
		rtw_os_xmit_complete(padapter, pxmitframe);

		break;
	} while (1);

	//3 2. aggregate same priority and same DA(AP or STA) frames
	pfirstframe = pxmitframe;
	len = xmitframe_need_length(pfirstframe) + TXDESC_SIZE+(pfirstframe->pkt_offset*PACKET_OFFSET_SZ);
	pbuf_tail = len;
	pbuf = _RND8(pbuf_tail);

	// check pkt amount in one bulk
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
//printk("==> pkt_no=%d,pkt_len=%d,len=%d,RND8_LEN=%d,pkt_offset=0x%02x\n",
	//pxmitframe->agg_num,pxmitframe->attrib.last_txcmdsz,len,pbuf,pxmitframe->pkt_offset );


	_enter_critical_bh(&pxmitpriv->lock, &irqL);

	xmitframe_phead = get_list_head(&ptxservq->sta_pending);
	xmitframe_plist = get_next(xmitframe_phead);
	
	while (rtw_end_of_queue_search(xmitframe_phead, xmitframe_plist) == _FALSE)
	{
		pxmitframe = LIST_CONTAINOR(xmitframe_plist, struct xmit_frame, list);
		xmitframe_plist = get_next(xmitframe_plist);

             pxmitframe->agg_num = 0; // not first frame of aggregation
		#ifdef CONFIG_TX_EARLY_MODE
		pxmitframe->pkt_offset = 1;// not first frame of aggregation,reserve offset for EM Info
		#else
		pxmitframe->pkt_offset = 0; // not first frame of aggregation, no need to reserve offset
		#endif	

		len = xmitframe_need_length(pxmitframe) + TXDESC_SIZE +(pxmitframe->pkt_offset*PACKET_OFFSET_SZ);
		
		if (_RND8(pbuf + len) > MAX_XMITBUF_SZ)
		//if (_RND8(pbuf + len) > (MAX_XMITBUF_SZ/2))//to do : for TX TP finial tune , Georgia 2012-0323
		{
			//printk("%s....len> MAX_XMITBUF_SZ\n",__FUNCTION__);
			pxmitframe->agg_num = 1;
			pxmitframe->pkt_offset = 1;			
			break;		
		}
		rtw_list_delete(&pxmitframe->list);
		ptxservq->qcnt--;
		phwxmit->accnt--;

#ifndef IDEA_CONDITION
		// suppose only data frames would be in queue
		if (pxmitframe->frame_tag != DATA_FRAMETAG) {
			RT_TRACE(_module_rtl8192c_xmit_c_, _drv_err_,
				 ("xmitframe_complete: frame tag(%d) is not DATA_FRAMETAG(%d)!\n",
				  pxmitframe->frame_tag, DATA_FRAMETAG));
			rtw_free_xmitframe_ex(pxmitpriv, pxmitframe);
			continue;
		}

		// TID 0~15
		if ((pxmitframe->attrib.priority < 0) ||
		    (pxmitframe->attrib.priority > 15)) {
			RT_TRACE(_module_rtl8192c_xmit_c_, _drv_err_,
				 ("xmitframe_complete: TID(%d) should be 0~15!\n",
				  pxmitframe->attrib.priority));
			rtw_free_xmitframe_ex(pxmitpriv, pxmitframe);
			continue;
		}
#endif

//		pxmitframe->pxmitbuf = pxmitbuf;
		pxmitframe->buf_addr = pxmitbuf->pbuf + pbuf;
	
#ifdef IDEA_CONDITION
		rtw_xmitframe_coalesce(padapter, pxmitframe->pkt, pxmitframe);
#else
		res = rtw_xmitframe_coalesce(padapter, pxmitframe->pkt, pxmitframe);
		if (res == _FALSE) {
			DBG_871X("%s coalesce failed \n",__FUNCTION__);
			rtw_free_xmitframe_ex(pxmitpriv, pxmitframe);
			continue;
		}
#endif
		//printk("==> pxmitframe->attrib.priority:%d\n",pxmitframe->attrib.priority);	
		// always return ndis_packet after rtw_xmitframe_coalesce
		rtw_os_xmit_complete(padapter, pxmitframe);

		// (len - TXDESC_SIZE) == pxmitframe->attrib.last_txcmdsz
		update_txdesc(pxmitframe, pxmitframe->buf_addr, pxmitframe->attrib.last_txcmdsz,_TRUE);
				
		// don't need xmitframe any more
		rtw_free_xmitframe_ex(pxmitpriv, pxmitframe);

		// handle pointer and stop condition
		pbuf_tail = pbuf + len;
		pbuf = _RND8(pbuf_tail);


		pfirstframe->agg_num++;
#ifdef CONFIG_TX_EARLY_MODE	
		pxmitpriv->agg_pkt[pfirstframe->agg_num-1].offset = _RND8(len); 			
		pxmitpriv->agg_pkt[pfirstframe->agg_num-1].pkt_len = pxmitframe->attrib.last_txcmdsz;						
#endif
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
	}//end while( aggregate same priority and same DA(AP or STA) frames)


	if (_rtw_queue_empty(&ptxservq->sta_pending) == _TRUE)
		rtw_list_delete(&ptxservq->tx_pending);

	_exit_critical_bh(&pxmitpriv->lock, &irqL);

	if ((pfirstframe->attrib.ether_type != 0x0806) &&
	    (pfirstframe->attrib.ether_type != 0x888e) &&
	    (pfirstframe->attrib.dhcp_pkt != 1))
	{
		rtw_issue_addbareq_cmd(padapter, pfirstframe);
	}

#ifndef CONFIG_USE_USB_BUFFER_ALLOC_TX
	//3 3. update first frame txdesc
	if ((pbuf_tail % bulkSize) == 0) {
		// remove pkt_offset
		pbuf_tail -= PACKET_OFFSET_SZ;
		pfirstframe->buf_addr += PACKET_OFFSET_SZ;
		pfirstframe->pkt_offset--;
		//printk("$$$$$ buf size equal to USB block size $$$$$$\n");
	}
#endif	// CONFIG_USE_USB_BUFFER_ALLOC_TX

	update_txdesc(pfirstframe, pfirstframe->buf_addr, pfirstframe->attrib.last_txcmdsz,_TRUE);
		
        #ifdef CONFIG_TX_EARLY_MODE
	//prepare EM info for first frame, agg_num value start from 1
	pxmitpriv->agg_pkt[0].offset = _RND8(pfirstframe->attrib.last_txcmdsz +TXDESC_SIZE +(pfirstframe->pkt_offset*PACKET_OFFSET_SZ));
	pxmitpriv->agg_pkt[0].pkt_len = pfirstframe->attrib.last_txcmdsz;//get from rtw_xmitframe_coalesce 			

	UpdateEarlyModeInfo8188E(pxmitpriv,pxmitbuf );
	#endif
	
	//3 4. write xmit buffer to USB FIFO
	ff_hwaddr = rtw_get_ff_hwaddr(pfirstframe);
//printk("%s ===================================== write port,buf_size(%d) \n",__FUNCTION__,pbuf_tail);
	// xmit address == ((xmit_frame*)pxmitbuf->priv_data)->buf_addr
	rtw_write_port(padapter, ff_hwaddr, pbuf_tail, (u8*)pxmitbuf);


	//3 5. update statisitc
	pbuf_tail -= (pfirstframe->agg_num * TXDESC_SIZE);
	pbuf_tail -= (pfirstframe->pkt_offset * PACKET_OFFSET_SZ);
	
	
	rtw_count_tx_stats(padapter, pfirstframe, pbuf_tail);

	rtw_free_xmitframe_ex(pxmitpriv, pfirstframe);

	return _TRUE;
}

#else

s32 rtl8188eu_xmitframe_complete(_adapter *padapter, struct xmit_priv *pxmitpriv, struct xmit_buf *pxmitbuf)
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
				//printk("==> pxmitframe->attrib.priority:%d\n",pxmitframe->attrib.priority);		
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
				rtw_free_xmitframe_ex(pxmitpriv, pxmitframe);	
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
//printk("==> %s \n",__FUNCTION__);

	res = rtw_xmitframe_coalesce(padapter, pxmitframe->pkt, pxmitframe);
	if (res == _SUCCESS) {
		rtw_dump_xframe(padapter, pxmitframe);
	}
	else{
		printk("==> %s xmitframe_coalsece failed\n",__FUNCTION__);
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
#ifdef CONFIG_CONCURRENT_MODE	
	PADAPTER pbuddy_adapter = padapter->pbuddy_adapter;
	struct mlme_priv *pbuddy_mlmepriv = &(pbuddy_adapter->mlmepriv);	
#endif
	
	_enter_critical_bh(&pxmitpriv->lock, &irqL);

//printk("==> %s \n",__FUNCTION__);

	if (rtw_txframes_sta_ac_pending(padapter, pattrib) > 0)
	{
		//printk("enqueue AC(%d)\n",pattrib->priority);
		goto enqueue;
	}


	if (check_fwstate(pmlmepriv, _FW_UNDER_SURVEY|_FW_UNDER_LINKING) == _TRUE)
		goto enqueue;

#ifdef CONFIG_CONCURRENT_MODE	
	if (check_fwstate(pbuddy_mlmepriv, _FW_UNDER_SURVEY|_FW_UNDER_LINKING) == _TRUE)
		goto enqueue;
#endif

	pxmitbuf = rtw_alloc_xmitbuf(pxmitpriv);
	if (pxmitbuf == NULL)
		goto enqueue;

	_exit_critical_bh(&pxmitpriv->lock, &irqL);

	pxmitframe->pxmitbuf = pxmitbuf;
	pxmitframe->buf_addr = pxmitbuf->pbuf;
	pxmitbuf->priv_data = pxmitframe;

	if (xmitframe_direct(padapter, pxmitframe) != _SUCCESS) {
		rtw_free_xmitbuf(pxmitpriv, pxmitbuf);
		rtw_free_xmitframe_ex(pxmitpriv, pxmitframe);
	}

	return _TRUE;

enqueue:
	res = rtw_xmitframe_enqueue(padapter, pxmitframe);
	_exit_critical_bh(&pxmitpriv->lock, &irqL);

	if (res != _SUCCESS) {
		RT_TRACE(_module_xmit_osdep_c_, _drv_err_, ("pre_xmitframe: enqueue xmitframe fail\n"));
		rtw_free_xmitframe_ex(pxmitpriv, pxmitframe);

		// Trick, make the statistics correct
		pxmitpriv->tx_pkts--;
		pxmitpriv->tx_drop++;
		return _TRUE;
	}

	return _FALSE;
}

void rtl8188eu_mgnt_xmit(_adapter *padapter, struct xmit_frame *pmgntframe)
{
	rtw_dump_xframe(padapter, pmgntframe);
}

/*
 * Return
 *	_TRUE	dump packet directly ok
 *	_FALSE	temporary can't transmit packets to hardware
 */
s32 rtl8188eu_hal_xmit(_adapter *padapter, struct xmit_frame *pxmitframe)
{
	return pre_xmitframe(padapter, pxmitframe);
}

#ifdef  CONFIG_HOSTAPD_MLME

static void rtl8188eu_hostap_mgnt_xmit_cb(struct urb *urb)
{	
#ifdef PLATFORM_LINUX
	struct sk_buff *skb = (struct sk_buff *)urb->context;

	//DBG_8192C("%s\n", __FUNCTION__);

	dev_kfree_skb_any(skb);
#endif	
}

s32 rtl8188eu_hostap_mgnt_xmit_entry(_adapter *padapter, _pkt *pkt)
{
#ifdef PLATFORM_LINUX
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
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(padapter);
	struct dvobj_priv *pdvobj = &padapter->dvobjpriv;	

	
	//DBG_8192C("%s\n", __FUNCTION__);

	skb = pkt;
	
	len = skb->len;
	tx_hdr = (struct rtw_ieee80211_hdr *)(skb->data);
	fc = le16_to_cpu(tx_hdr->frame_ctl);
	bmcst = IS_MCAST(tx_hdr->addr1);

	if ((fc & RTW_IEEE80211_FCTL_FTYPE) != RTW_IEEE80211_FTYPE_MGMT)
		goto _exit;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)) // http://www.mail-archive.com/netdev@vger.kernel.org/msg17214.html
	pxmit_skb = dev_alloc_skb(len + TXDESC_SIZE);			
#else			
	pxmit_skb = netdev_alloc_skb(pnetdev, len + TXDESC_SIZE);
#endif		

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
	

	rtl8188eu_cal_txdesc_chksum(ptxdesc);
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
			  pxmit_skb->data, pxmit_skb->len, rtl8192cu_hostap_mgnt_xmit_cb, pxmit_skb);
	
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

#endif

	return 0;

}
#endif

