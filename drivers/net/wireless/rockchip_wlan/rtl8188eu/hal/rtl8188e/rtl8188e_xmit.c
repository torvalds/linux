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

#include <drv_types.h>
#include <rtl8188e_hal.h>

#ifdef CONFIG_XMIT_ACK
void dump_txrpt_ccx_88e(void *buf)
{
	struct txrpt_ccx_88e *txrpt_ccx = (struct txrpt_ccx_88e *)buf;

	DBG_871X("%s:\n"
		"tag1:%u, pkt_num:%u, txdma_underflow:%u, int_bt:%u, int_tri:%u, int_ccx:%u\n"
		"mac_id:%u, pkt_ok:%u, bmc:%u\n"
		"retry_cnt:%u, lifetime_over:%u, retry_over:%u\n"
		"ccx_qtime:%u\n"
		"final_data_rate:0x%02x\n"
		"qsel:%u, sw:0x%03x\n"
		, __func__
		, txrpt_ccx->tag1, txrpt_ccx->pkt_num, txrpt_ccx->txdma_underflow, txrpt_ccx->int_bt, txrpt_ccx->int_tri, txrpt_ccx->int_ccx
		, txrpt_ccx->mac_id, txrpt_ccx->pkt_ok, txrpt_ccx->bmc
		, txrpt_ccx->retry_cnt, txrpt_ccx->lifetime_over, txrpt_ccx->retry_over 
		, txrpt_ccx_qtime_88e(txrpt_ccx)
		, txrpt_ccx->final_data_rate
		, txrpt_ccx->qsel, txrpt_ccx_sw_88e(txrpt_ccx)
	);
}

void handle_txrpt_ccx_88e(_adapter *adapter, u8 *buf)
{
	struct txrpt_ccx_88e *txrpt_ccx = (struct txrpt_ccx_88e *)buf;

	#ifdef DBG_CCX
	dump_txrpt_ccx_88e(buf);
	#endif

	if (txrpt_ccx->int_ccx) {
		if (txrpt_ccx->pkt_ok)
			rtw_ack_tx_done(&adapter->xmitpriv, RTW_SCTX_DONE_SUCCESS);
		else
			rtw_ack_tx_done(&adapter->xmitpriv, RTW_SCTX_DONE_CCX_PKT_FAIL);
	}
}
#endif //CONFIG_XMIT_ACK

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

//#define DBG_EMINFO

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
		DBG_8192C("\n%s ==> pEMInfo->EMPktNum =%d\n",__FUNCTION__,pEMInfo->EMPktNum);
		for(i=0;i< EARLY_MODE_MAX_PKT_NUM;i++){
			DBG_8192C("%s ==> pEMInfo->EMPktLen[%d] =%d\n",__FUNCTION__,i,pEMInfo->EMPktLen[i]);
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
	PTXDESC_8188E ptxdesc;
	
	u8 *pmem,*pEMInfo_mem;
	s8 node_num_0=0,node_num_1=0;
	struct EMInfo eminfo;
	struct agg_pkt_info *paggpkt;
	struct xmit_frame *pframe = (struct xmit_frame*)pxmitbuf->priv_data;	
	pmem= pframe->buf_addr;	
	
	#ifdef DBG_EMINFO			
	DBG_8192C("\n%s ==> agg_num:%d\n",__FUNCTION__, pframe->agg_num);
	for(index=0;index<pframe->agg_num;index++){
		offset = 	pxmitpriv->agg_pkt[index].offset;
		pktlen = pxmitpriv->agg_pkt[index].pkt_len;
		DBG_8192C("%s ==> agg_pkt[%d].offset=%d\n",__FUNCTION__,index,offset);
		DBG_8192C("%s ==> agg_pkt[%d].pkt_len=%d\n",__FUNCTION__,index,pktlen);
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
			eminfo.EMPktLen[j] = pxmitpriv->agg_pkt[index+1+j].pkt_len+4;// 4 bytes CRC
		}
			
		if(pmem){
			if(index==0){
				ptxdesc = (PTXDESC_8188E)(pmem);
				pEMInfo_mem = ((u8 *)ptxdesc)+TXDESC_SIZE;				
			}
			else{
				pmem = pmem + pxmitpriv->agg_pkt[index-1].offset;
				ptxdesc = (PTXDESC_8188E)(pmem);
				pEMInfo_mem = ((u8 *)ptxdesc)+TXDESC_SIZE;					
			}
			
			#ifdef DBG_EMINFO
			DBG_8192C("%s ==> desc.pkt_len=%d\n",__FUNCTION__,ptxdesc->pktlen);
			#endif
			InsertEMContent_8188E(&eminfo,pEMInfo_mem);
		}	
		
		
	} 
	_rtw_memset(pxmitpriv->agg_pkt,0,sizeof(struct agg_pkt_info)*MAX_AGG_PKT_NUM);

}
#endif

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

