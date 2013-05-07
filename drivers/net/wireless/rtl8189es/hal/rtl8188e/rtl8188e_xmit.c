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
#include <sdio_ops.h>
#include <rtl8188e_hal.h>

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
		printk("=====================================\n");
		printk("txdw0(0x%08x)\n",ptxdesc->txdw0);
		printk("txdw1(0x%08x)\n",ptxdesc->txdw1);
		printk("txdw2(0x%08x)\n",ptxdesc->txdw2);
		printk("txdw3(0x%08x)\n",ptxdesc->txdw3);
		printk("txdw4(0x%08x)\n",ptxdesc->txdw4);
		printk("txdw5(0x%08x)\n",ptxdesc->txdw5);
		printk("txdw6(0x%08x)\n",ptxdesc->txdw6);
		printk("txdw7(0x%08x)\n",ptxdesc->txdw7);
		printk("=====================================\n");
	}

}


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
			eminfo.EMPktLen[j] = pxmitpriv->agg_pkt[index+1+j].pkt_len+4;// 4 bytes CRC
		}
			
		if(pmem){
			if(index==0){
				ptxdesc = (PTXDESC)(pmem);
				pEMInfo_mem = ((u8 *)ptxdesc)+TXDESC_SIZE;				
			}
			else{
				pmem = pmem + pxmitpriv->agg_pkt[index-1].offset;
				ptxdesc = (PTXDESC)(pmem);
				pEMInfo_mem = ((u8 *)ptxdesc)+TXDESC_SIZE;					
			}
			
			#ifdef DBG_EMINFO
			printk("%s ==> desc.pkt_len=%d\n",__FUNCTION__,ptxdesc->pktlen);
			#endif
			InsertEMContent_8188E(&eminfo,pEMInfo_mem);
		}	
		
		
	} 
	_rtw_memset(pxmitpriv->agg_pkt,0,sizeof(struct agg_pkt_info)*MAX_AGG_PKT_NUM);

}
#endif


