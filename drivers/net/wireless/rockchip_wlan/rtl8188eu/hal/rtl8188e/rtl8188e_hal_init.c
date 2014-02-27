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
#define _HAL_INIT_C_

#include <drv_types.h>
#include <rtl8188e_hal.h>


#if defined(CONFIG_IOL)
static void iol_mode_enable(PADAPTER padapter, u8 enable)
{
	u8 reg_0xf0 = 0;
	
	if(enable)
	{
		//Enable initial offload
		reg_0xf0 = rtw_read8(padapter, REG_SYS_CFG);
		//DBG_871X("%s reg_0xf0:0x%02x, write 0x%02x\n", __FUNCTION__, reg_0xf0, reg_0xf0|SW_OFFLOAD_EN);
		rtw_write8(padapter, REG_SYS_CFG, reg_0xf0|SW_OFFLOAD_EN);
		
		if(padapter->bFWReady == _FALSE)
		{
			printk("bFWReady == _FALSE call reset 8051...\n");
			_8051Reset88E(padapter);
		}		
			
	}
	else
	{
		//disable initial offload
		reg_0xf0 = rtw_read8(padapter, REG_SYS_CFG);
		//DBG_871X("%s reg_0xf0:0x%02x, write 0x%02x\n", __FUNCTION__, reg_0xf0, reg_0xf0& ~SW_OFFLOAD_EN);
		rtw_write8(padapter, REG_SYS_CFG, reg_0xf0 & ~SW_OFFLOAD_EN);
	}
}

static s32 iol_execute(PADAPTER padapter, u8 control)
{
	s32 status = _FAIL;
	u8 reg_0x88 = 0,reg_1c7=0;
	u32 start = 0, passing_time = 0;
	
	u32 t1,t2;
	control = control&0x0f;
	reg_0x88 = rtw_read8(padapter, REG_HMEBOX_E0);
	//DBG_871X("%s reg_0x88:0x%02x, write 0x%02x\n", __FUNCTION__, reg_0x88, reg_0x88|control);
	rtw_write8(padapter, REG_HMEBOX_E0,  reg_0x88|control);

	t1 = start = rtw_get_current_time();
	while(
		//(reg_1c7 = rtw_read8(padapter, 0x1c7) >1) &&
		(reg_0x88=rtw_read8(padapter, REG_HMEBOX_E0)) & control
		&& (passing_time=rtw_get_passing_time_ms(start))<1000
	) {
		//DBG_871X("%s polling reg_0x88:0x%02x,reg_0x1c7:0x%02x\n", __FUNCTION__, reg_0x88,rtw_read8(padapter, 0x1c7) );
		//rtw_udelay_os(100);
	}

	reg_0x88 = rtw_read8(padapter, REG_HMEBOX_E0);
	status = (reg_0x88 & control)?_FAIL:_SUCCESS;
	if(reg_0x88 & control<<4)
		status = _FAIL;
	t2= rtw_get_current_time();
	//printk("==> step iol_execute :  %5u reg-0x1c0= 0x%02x\n",rtw_get_time_interval_ms(t1,t2),rtw_read8(padapter, 0x1c0));
	//DBG_871X("%s in %u ms, reg_0x88:0x%02x\n", __FUNCTION__, passing_time, reg_0x88);
	
	return status;
}

static s32 iol_InitLLTTable(
	PADAPTER padapter,
	u8 txpktbuf_bndy
	)
{
	s32 rst = _SUCCESS; 
	iol_mode_enable(padapter, 1);
	//DBG_871X("%s txpktbuf_bndy:%u\n", __FUNCTION__, txpktbuf_bndy);
	rtw_write8(padapter, REG_TDECTRL+1, txpktbuf_bndy);
	rst = iol_execute(padapter, CMD_INIT_LLT);
	iol_mode_enable(padapter, 0);
	return rst;
}

static VOID
efuse_phymap_to_logical(u8 * phymap, u16 _offset, u16 _size_byte, u8  *pbuf)
{
	u8	*efuseTbl = NULL;
	u8	rtemp8;
	u16	eFuse_Addr = 0;
	u8	offset, wren;
	u16	i, j;
	u16	**eFuseWord = NULL;
	u16	efuse_utilized = 0;
	u8	efuse_usage = 0;
	u8	u1temp = 0;


	efuseTbl = (u8*)rtw_zmalloc(EFUSE_MAP_LEN_88E);
	if(efuseTbl == NULL)
	{
		DBG_871X("%s: alloc efuseTbl fail!\n", __FUNCTION__);
		goto exit;
	}

	eFuseWord= (u16 **)rtw_malloc2d(EFUSE_MAX_SECTION_88E, EFUSE_MAX_WORD_UNIT, 2);
	if(eFuseWord == NULL)
	{
		DBG_871X("%s: alloc eFuseWord fail!\n", __FUNCTION__);
		goto exit;
	}

	// 0. Refresh efuse init map as all oxFF.
	for (i = 0; i < EFUSE_MAX_SECTION_88E; i++)
		for (j = 0; j < EFUSE_MAX_WORD_UNIT; j++)
			eFuseWord[i][j] = 0xFFFF;

	//
	// 1. Read the first byte to check if efuse is empty!!!
	// 
	//
	rtemp8 = *(phymap+eFuse_Addr);
	if(rtemp8 != 0xFF)
	{
		efuse_utilized++;
		//printk("efuse_Addr-%d efuse_data=%x\n", eFuse_Addr, *rtemp8);
		eFuse_Addr++;
	}
	else
	{
		DBG_871X("EFUSE is empty efuse_Addr-%d efuse_data=%x\n", eFuse_Addr, rtemp8);
		goto exit;
	}


	//
	// 2. Read real efuse content. Filter PG header and every section data.
	//
	while((rtemp8 != 0xFF) && (eFuse_Addr < EFUSE_REAL_CONTENT_LEN_88E))
	{
		//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("efuse_Addr-%d efuse_data=%x\n", eFuse_Addr-1, *rtemp8));
	
		// Check PG header for section num.
		if((rtemp8 & 0x1F ) == 0x0F)		//extended header
		{			
			u1temp =( (rtemp8 & 0xE0) >> 5);
			//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("extended header u1temp=%x *rtemp&0xE0 0x%x\n", u1temp, *rtemp8 & 0xE0));

			//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("extended header u1temp=%x \n", u1temp));

			rtemp8 = *(phymap+eFuse_Addr);

			//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("extended header efuse_Addr-%d efuse_data=%x\n", eFuse_Addr, *rtemp8));	
			
			if((rtemp8 & 0x0F) == 0x0F)
			{
				eFuse_Addr++;			
				rtemp8 = *(phymap+eFuse_Addr);
				
				if(rtemp8 != 0xFF && (eFuse_Addr < EFUSE_REAL_CONTENT_LEN_88E))
				{
					eFuse_Addr++;				
				}				
				continue;
			}
			else
			{
				offset = ((rtemp8 & 0xF0) >> 1) | u1temp;
				wren = (rtemp8 & 0x0F);
				eFuse_Addr++;				
			}
		}
		else
		{
			offset = ((rtemp8 >> 4) & 0x0f);
			wren = (rtemp8 & 0x0f);			
		}
		
		if(offset < EFUSE_MAX_SECTION_88E)
		{
			// Get word enable value from PG header
			//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Offset-%d Worden=%x\n", offset, wren));

			for(i=0; i<EFUSE_MAX_WORD_UNIT; i++)
			{
				// Check word enable condition in the section				
				if(!(wren & 0x01))
				{
					//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Addr=%d \n", eFuse_Addr));
					rtemp8 = *(phymap+eFuse_Addr);
					eFuse_Addr++;
					//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Data=0x%x\n", *rtemp8)); 				
					efuse_utilized++;
					eFuseWord[offset][i] = (rtemp8 & 0xff);
					

					if(eFuse_Addr >= EFUSE_REAL_CONTENT_LEN_88E) 
						break;

					//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Addr=%d", eFuse_Addr));
					rtemp8 = *(phymap+eFuse_Addr);
					eFuse_Addr++;
					//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Data=0x%x\n", *rtemp8)); 				
					
					efuse_utilized++;
					eFuseWord[offset][i] |= (((u2Byte)rtemp8 << 8) & 0xff00);

					if(eFuse_Addr >= EFUSE_REAL_CONTENT_LEN_88E) 
						break;
				}
				
				wren >>= 1;
				
			}
		}

		// Read next PG header
		rtemp8 = *(phymap+eFuse_Addr);
		//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Addr=%d rtemp 0x%x\n", eFuse_Addr, *rtemp8));
		
		if(rtemp8 != 0xFF && (eFuse_Addr < EFUSE_REAL_CONTENT_LEN_88E))
		{
			efuse_utilized++;
			eFuse_Addr++;
		}
	}

	//
	// 3. Collect 16 sections and 4 word unit into Efuse map.
	//
	for(i=0; i<EFUSE_MAX_SECTION_88E; i++)
	{
		for(j=0; j<EFUSE_MAX_WORD_UNIT; j++)
		{
			efuseTbl[(i*8)+(j*2)]=(eFuseWord[i][j] & 0xff);
			efuseTbl[(i*8)+((j*2)+1)]=((eFuseWord[i][j] >> 8) & 0xff);
		}
	}


	//
	// 4. Copy from Efuse map to output pointer memory!!!
	//
	for(i=0; i<_size_byte; i++)
	{		
		pbuf[i] = efuseTbl[_offset+i];
	}

	//
	// 5. Calculate Efuse utilization.
	//
	efuse_usage = (u1Byte)((efuse_utilized*100)/EFUSE_REAL_CONTENT_LEN_88E);
	//Adapter->HalFunc.SetHwRegHandler(Adapter, HW_VAR_EFUSE_BYTES, (u8 *)&efuse_utilized);

exit:
	if(efuseTbl)
		rtw_mfree(efuseTbl, EFUSE_MAP_LEN_88E);

	if(eFuseWord)
		rtw_mfree2d((void *)eFuseWord, EFUSE_MAX_SECTION_88E, EFUSE_MAX_WORD_UNIT, sizeof(u16));
}

void efuse_read_phymap_from_txpktbuf(
	ADAPTER *adapter,
	int bcnhead,	//beacon head, where FW store len(2-byte) and efuse physical map.
	u8 *content,	//buffer to store efuse physical map
	u16 *size	//for efuse content: the max byte to read. will update to byte read
	)
{
	u16 dbg_addr = 0;
	u32 start  = 0, passing_time = 0;
	u8 reg_0x143 = 0;
	u8 reg_0x106 = 0;
	u32 lo32 = 0, hi32 = 0;
	u16 len = 0, count = 0;
	int i = 0;
	u16 limit = *size;

	u8 *pos = content;
	
	if(bcnhead<0) //if not valid
		bcnhead = rtw_read8(adapter, REG_TDECTRL+1);

	DBG_871X("%s bcnhead:%d\n", __FUNCTION__, bcnhead);

	//reg_0x106 = rtw_read8(adapter, REG_PKT_BUFF_ACCESS_CTRL);
	//DBG_871X("%s reg_0x106:0x%02x, write 0x%02x\n", __FUNCTION__, reg_0x106, 0x69);
	rtw_write8(adapter, REG_PKT_BUFF_ACCESS_CTRL, TXPKT_BUF_SELECT);
	//DBG_871X("%s reg_0x106:0x%02x\n", __FUNCTION__, rtw_read8(adapter, 0x106));

	dbg_addr = bcnhead*128/8; //8-bytes addressing

	while(1)
	{
		//DBG_871X("%s dbg_addr:0x%x\n", __FUNCTION__, dbg_addr+i);
		rtw_write16(adapter, REG_PKTBUF_DBG_ADDR, dbg_addr+i);

		//DBG_871X("%s write reg_0x143:0x00\n", __FUNCTION__);
		rtw_write8(adapter, REG_TXPKTBUF_DBG, 0);
		start = rtw_get_current_time();
		while(!(reg_0x143=rtw_read8(adapter, REG_TXPKTBUF_DBG))//dbg
		//while(rtw_read8(adapter, REG_TXPKTBUF_DBG) & BIT0
			&& (passing_time=rtw_get_passing_time_ms(start))<1000
		) {
			DBG_871X("%s polling reg_0x143:0x%02x, reg_0x106:0x%02x\n", __FUNCTION__, reg_0x143, rtw_read8(adapter, 0x106));
			rtw_usleep_os(100);
		}


		lo32 = rtw_read32(adapter, REG_PKTBUF_DBG_DATA_L);
		hi32 = rtw_read32(adapter, REG_PKTBUF_DBG_DATA_H);

		#if 0
		DBG_871X("%s lo32:0x%08x, %02x %02x %02x %02x\n", __FUNCTION__, lo32
			, rtw_read8(adapter, REG_PKTBUF_DBG_DATA_L)
			, rtw_read8(adapter, REG_PKTBUF_DBG_DATA_L+1)
			, rtw_read8(adapter, REG_PKTBUF_DBG_DATA_L+2)
			, rtw_read8(adapter, REG_PKTBUF_DBG_DATA_L+3)
		);
		DBG_871X("%s hi32:0x%08x, %02x %02x %02x %02x\n", __FUNCTION__, hi32
			, rtw_read8(adapter, REG_PKTBUF_DBG_DATA_H)
			, rtw_read8(adapter, REG_PKTBUF_DBG_DATA_H+1)
			, rtw_read8(adapter, REG_PKTBUF_DBG_DATA_H+2)
			, rtw_read8(adapter, REG_PKTBUF_DBG_DATA_H+3)
		);
		#endif

		if(i==0)
		{
			#if 1 //for debug
			u8 lenc[2];
			u16 lenbak, aaabak;
			u16 aaa;
			lenc[0] = rtw_read8(adapter, REG_PKTBUF_DBG_DATA_L);
			lenc[1] = rtw_read8(adapter, REG_PKTBUF_DBG_DATA_L+1);

			aaabak = le16_to_cpup((u16*)lenc);
			lenbak = le16_to_cpu(*((u16*)lenc));
			aaa = le16_to_cpup((u16*)&lo32);
			#endif
			len = le16_to_cpu(*((u16*)&lo32));

			limit = (len-2<limit)?len-2:limit;

			DBG_871X("%s len:%u, lenbak:%u, aaa:%u, aaabak:%u\n", __FUNCTION__, len, lenbak, aaa, aaabak);

			_rtw_memcpy(pos, ((u8*)&lo32)+2, (limit>=count+2)?2:limit-count);
			count+= (limit>=count+2)?2:limit-count;
			pos=content+count;
			
		}
		else
		{
			_rtw_memcpy(pos, ((u8*)&lo32), (limit>=count+4)?4:limit-count);
			count+=(limit>=count+4)?4:limit-count;
			pos=content+count;
			

		}

		if(limit>count && len-2>count) {
			_rtw_memcpy(pos, (u8*)&hi32, (limit>=count+4)?4:limit-count);
			count+=(limit>=count+4)?4:limit-count;
			pos=content+count;
		}

		if(limit<=count || len-2<=count)
			break;

		i++;
	}

	rtw_write8(adapter, REG_PKT_BUFF_ACCESS_CTRL, DISABLE_TRXPKT_BUF_ACCESS);
	
	DBG_871X("%s read count:%u\n", __FUNCTION__, count);
	*size = count;

}


static s32 iol_read_efuse(
	PADAPTER padapter,
	u8 txpktbuf_bndy,
	u16 offset,
	u16 size_byte,
	u8 *logical_map
	)
{
	s32 status = _FAIL;
	u8 reg_0x106 = 0;
	u8 physical_map[512];
	u16 size = 512;
	int i;


	rtw_write8(padapter, REG_TDECTRL+1, txpktbuf_bndy);
	_rtw_memset(physical_map, 0xFF, 512);
	
	///reg_0x106 = rtw_read8(padapter, REG_PKT_BUFF_ACCESS_CTRL);
	//DBG_871X("%s reg_0x106:0x%02x, write 0x%02x\n", __FUNCTION__, reg_0x106, 0x69);
	rtw_write8(padapter, REG_PKT_BUFF_ACCESS_CTRL, TXPKT_BUF_SELECT);
	//DBG_871X("%s reg_0x106:0x%02x\n", __FUNCTION__, rtw_read8(padapter, 0x106));

	status = iol_execute(padapter, CMD_READ_EFUSE_MAP);

	if(status == _SUCCESS)
		efuse_read_phymap_from_txpktbuf(padapter, txpktbuf_bndy, physical_map, &size);

	#if 0
	DBG_871X("%s physical map\n", __FUNCTION__);
	for(i=0;i<size;i++)
	{
		DBG_871X("%02x ", physical_map[i]);
		if(i%16==15)
			DBG_871X("\n");
	}
	DBG_871X("\n");
	#endif

	efuse_phymap_to_logical(physical_map, offset, size_byte, logical_map);

	return status;
}

s32 rtl8188e_iol_efuse_patch(PADAPTER padapter)
{
	s32	result = _SUCCESS;
	printk("==> %s \n",__FUNCTION__);
	
	if(rtw_IOL_applied(padapter)){
		iol_mode_enable(padapter, 1);
		result = iol_execute(padapter, CMD_READ_EFUSE_MAP);
		if(result == _SUCCESS)			
			result = iol_execute(padapter, CMD_EFUSE_PATCH);
		
		iol_mode_enable(padapter, 0);
	}
	return result;
}

static s32 iol_ioconfig(
	PADAPTER padapter,
	u8 iocfg_bndy
	)
{
	s32 rst = _SUCCESS; 
	
	//DBG_871X("%s iocfg_bndy:%u\n", __FUNCTION__, iocfg_bndy);
	rtw_write8(padapter, REG_TDECTRL+1, iocfg_bndy);
	rst = iol_execute(padapter, CMD_IOCONFIG);
	
	return rst;
}

int rtl8188e_IOL_exec_cmds_sync(ADAPTER *adapter, struct xmit_frame *xmit_frame, u32 max_wating_ms,u32 bndy_cnt)
{
	
	u32 start_time = rtw_get_current_time();
	u32 passing_time_ms;
	u8 polling_ret,i;
	int ret = _FAIL;
	u32 t1,t2;
	
	//printk("===> %s ,bndy_cnt = %d \n",__FUNCTION__,bndy_cnt);
	if (rtw_IOL_append_END_cmd(xmit_frame) != _SUCCESS)
		goto exit;
#ifdef CONFIG_USB_HCI
	{
		struct pkt_attrib	*pattrib = &xmit_frame->attrib;		
		if(rtw_usb_bulk_size_boundary(adapter,TXDESC_SIZE+pattrib->last_txcmdsz))
		{
			if (rtw_IOL_append_END_cmd(xmit_frame) != _SUCCESS)
				goto exit;
		}			
	}
#endif //CONFIG_USB_HCI
	
	//rtw_IOL_cmd_buf_dump(adapter,xmit_frame->attrib.pktlen+TXDESC_OFFSET,xmit_frame->buf_addr);
	//rtw_hal_mgnt_xmit(adapter, xmit_frame);
	//rtw_dump_xframe_sync(adapter, xmit_frame);
	
	dump_mgntframe_and_wait(adapter, xmit_frame, max_wating_ms);
	
	t1=	rtw_get_current_time();
	iol_mode_enable(adapter, 1);
	for(i=0;i<bndy_cnt;i++){
		u8 page_no = 0;
		page_no = i*2 ;
		//printk(" i = %d, page_no = %d \n",i,page_no);	
		if( (ret = iol_ioconfig(adapter, page_no)) != _SUCCESS)
		{
			break;
		}
	}
	iol_mode_enable(adapter, 0);
	t2 = rtw_get_current_time();
	//printk("==> %s :  %5u\n",__FUNCTION__,rtw_get_time_interval_ms(t1,t2));
exit:
	//restore BCN_HEAD
	rtw_write8(adapter, REG_TDECTRL+1, 0);	
	return ret;
}

void rtw_IOL_cmd_tx_pkt_buf_dump(ADAPTER *Adapter,int data_len)
{
	u32 fifo_data,reg_140;
	u32 addr,rstatus,loop=0;

	u16 data_cnts = (data_len/8)+1;				
	u8 *pbuf =rtw_zvmalloc(data_len+10);
	printk("###### %s ######\n",__FUNCTION__);
	
	rtw_write8(Adapter, REG_PKT_BUFF_ACCESS_CTRL, TXPKT_BUF_SELECT);
	if(pbuf){
		for(addr=0;addr< data_cnts;addr++){
			//printk("==> addr:0x%02x\n",addr);
			rtw_write32(Adapter,0x140,addr);
			rtw_usleep_os(2);
			loop=0;
			do{
				rstatus=(reg_140=rtw_read32(Adapter,REG_PKTBUF_DBG_CTRL)&BIT24);	
				//printk("rstatus = %02x, reg_140:0x%08x\n",rstatus,reg_140);
				if(rstatus){
					fifo_data = rtw_read32(Adapter,REG_PKTBUF_DBG_DATA_L);
					//printk("fifo_data_144:0x%08x\n",fifo_data);					
					_rtw_memcpy(pbuf+(addr*8),&fifo_data , 4);  
					
					fifo_data = rtw_read32(Adapter,REG_PKTBUF_DBG_DATA_H);
					//printk("fifo_data_148:0x%08x\n",fifo_data);					
					_rtw_memcpy(pbuf+(addr*8+4), &fifo_data, 4); 
					
				}
				rtw_usleep_os(2);
			}while( !rstatus && (loop++ <10));
		}
		rtw_IOL_cmd_buf_dump(Adapter,data_len,pbuf);
		rtw_vmfree(pbuf, data_len+10);	
		
	}					
	printk("###### %s ######\n",__FUNCTION__);
}

#endif /* defined(CONFIG_IOL) */


static VOID
_FWDownloadEnable_8188E(
	IN	PADAPTER		padapter,
	IN	BOOLEAN			enable
	)
{
	u8	tmp;

	if(enable)
	{
		// MCU firmware download enable.
		tmp = rtw_read8(padapter, REG_MCUFWDL);
		rtw_write8(padapter, REG_MCUFWDL, tmp|0x01);

		// 8051 reset
		tmp = rtw_read8(padapter, REG_MCUFWDL+2);
		rtw_write8(padapter, REG_MCUFWDL+2, tmp&0xf7);
	}
	else
	{		
		
		// MCU firmware download disable.
		tmp = rtw_read8(padapter, REG_MCUFWDL);
		rtw_write8(padapter, REG_MCUFWDL, tmp&0xfe);

		// Reserved for fw extension.
		rtw_write8(padapter, REG_MCUFWDL+1, 0x00);
	}
}
#define MAX_REG_BOLCK_SIZE	196 
static int
_BlockWrite(
	IN		PADAPTER		padapter,
	IN		PVOID		buffer,
	IN		u32			buffSize
	)
{
	int ret = _SUCCESS;

	u32			blockSize_p1 = 4;	// (Default) Phase #1 : PCI muse use 4-byte write to download FW
	u32			blockSize_p2 = 8;	// Phase #2 : Use 8-byte, if Phase#1 use big size to write FW.
	u32			blockSize_p3 = 1;	// Phase #3 : Use 1-byte, the remnant of FW image.
	u32			blockCount_p1 = 0, blockCount_p2 = 0, blockCount_p3 = 0;
	u32			remainSize_p1 = 0, remainSize_p2 = 0;
	u8			*bufferPtr	= (u8*)buffer;
	u32			i=0, offset=0;
#ifdef CONFIG_PCI_HCI
	u8			remainFW[4] = {0, 0, 0, 0};
	u8			*p = NULL;
#endif

#ifdef CONFIG_USB_HCI
	blockSize_p1 = MAX_REG_BOLCK_SIZE;
#endif

	//3 Phase #1
	blockCount_p1 = buffSize / blockSize_p1;
	remainSize_p1 = buffSize % blockSize_p1;

	if (blockCount_p1) {
		RT_TRACE(_module_hal_init_c_, _drv_notice_,
				("_BlockWrite: [P1] buffSize(%d) blockSize_p1(%d) blockCount_p1(%d) remainSize_p1(%d)\n",
				buffSize, blockSize_p1, blockCount_p1, remainSize_p1));
	}

	for (i = 0; i < blockCount_p1; i++)
	{
#ifdef CONFIG_USB_HCI
		ret = rtw_writeN(padapter, (FW_8188E_START_ADDRESS + i * blockSize_p1), blockSize_p1, (bufferPtr + i * blockSize_p1));
#else
		ret = rtw_write32(padapter, (FW_8188E_START_ADDRESS + i * blockSize_p1), le32_to_cpu(*((u32*)(bufferPtr + i * blockSize_p1))));
#endif

		if(ret == _FAIL)
			goto exit;
	}

#ifdef CONFIG_PCI_HCI
	p = (u8*)((u32*)(bufferPtr + blockCount_p1 * blockSize_p1));
	if (remainSize_p1) {
		switch (remainSize_p1) {
		case 0:
			break;
		case 3:
			remainFW[2]=*(p+2);
		case 2: 	
			remainFW[1]=*(p+1);
		case 1: 	
			remainFW[0]=*(p);
			ret = rtw_write32(padapter, (FW_8188E_START_ADDRESS + blockCount_p1 * blockSize_p1), 
				 le32_to_cpu(*(u32*)remainFW));	
		}
		return ret;
	}
#endif

	//3 Phase #2
	if (remainSize_p1)
	{
		offset = blockCount_p1 * blockSize_p1;

		blockCount_p2 = remainSize_p1/blockSize_p2;
		remainSize_p2 = remainSize_p1%blockSize_p2;

		if (blockCount_p2) {
				RT_TRACE(_module_hal_init_c_, _drv_notice_,
						("_BlockWrite: [P2] buffSize_p2(%d) blockSize_p2(%d) blockCount_p2(%d) remainSize_p2(%d)\n",
						(buffSize-offset), blockSize_p2 ,blockCount_p2, remainSize_p2));
		}

#ifdef CONFIG_USB_HCI
		for (i = 0; i < blockCount_p2; i++) {
			ret = rtw_writeN(padapter, (FW_8188E_START_ADDRESS + offset + i*blockSize_p2), blockSize_p2, (bufferPtr + offset + i*blockSize_p2));
			
			if(ret == _FAIL)
				goto exit;
		}
#endif
	}

	//3 Phase #3
	if (remainSize_p2)
	{
		offset = (blockCount_p1 * blockSize_p1) + (blockCount_p2 * blockSize_p2);

		blockCount_p3 = remainSize_p2 / blockSize_p3;

		RT_TRACE(_module_hal_init_c_, _drv_notice_,
				("_BlockWrite: [P3] buffSize_p3(%d) blockSize_p3(%d) blockCount_p3(%d)\n",
				(buffSize-offset), blockSize_p3, blockCount_p3));

		for(i = 0 ; i < blockCount_p3 ; i++){
			ret =rtw_write8(padapter, (FW_8188E_START_ADDRESS + offset + i), *(bufferPtr + offset + i));
			
			if(ret == _FAIL)
				goto exit;
		}
	}

exit:
	return ret;
}

static int
_PageWrite(
	IN		PADAPTER	padapter,
	IN		u32			page,
	IN		PVOID		buffer,
	IN		u32			size
	)
{
	u8 value8;
	u8 u8Page = (u8) (page & 0x07) ;

	value8 = (rtw_read8(padapter, REG_MCUFWDL+2) & 0xF8) | u8Page ;
	rtw_write8(padapter, REG_MCUFWDL+2,value8);

	return _BlockWrite(padapter,buffer,size);
}

static VOID
_FillDummy(
	u8*		pFwBuf,
	u32*	pFwLen
	)
{
	u32	FwLen = *pFwLen;
	u8	remain = (u8)(FwLen%4);
	remain = (remain==0)?0:(4-remain);

	while(remain>0)
	{
		pFwBuf[FwLen] = 0;
		FwLen++;
		remain--;
	}

	*pFwLen = FwLen;
}

static int
_WriteFW(
	IN		PADAPTER		padapter,
	IN		PVOID			buffer,
	IN		u32			size
	)
{
	// Since we need dynamic decide method of dwonload fw, so we call this function to get chip version.
	// We can remove _ReadChipVersion from ReadpadapterInfo8192C later.
	int ret = _SUCCESS;
	u32 	pageNums,remainSize ;
	u32 	page, offset;
	u8		*bufferPtr = (u8*)buffer;

#ifdef CONFIG_PCI_HCI
	// 20100120 Joseph: Add for 88CE normal chip.
	// Fill in zero to make firmware image to dword alignment.
//		_FillDummy(bufferPtr, &size);
#endif

	pageNums = size / MAX_DLFW_PAGE_SIZE ;
	//RT_ASSERT((pageNums <= 4), ("Page numbers should not greater then 4 \n"));
	remainSize = size % MAX_DLFW_PAGE_SIZE;

	for (page = 0; page < pageNums; page++) {
		offset = page * MAX_DLFW_PAGE_SIZE;
		ret = _PageWrite(padapter, page, bufferPtr+offset, MAX_DLFW_PAGE_SIZE);
		
		if(ret == _FAIL)
			goto exit;
	}
	if (remainSize) {
		offset = pageNums * MAX_DLFW_PAGE_SIZE;
		page = pageNums;
		ret = _PageWrite(padapter, page, bufferPtr+offset, remainSize);
		
		if(ret == _FAIL)
			goto exit;

	}
	RT_TRACE(_module_hal_init_c_, _drv_info_, ("_WriteFW Done- for Normal chip.\n"));

exit:
	return ret;
}

void _MCUIO_Reset88E(PADAPTER padapter,u8 bReset)
{
	u8 u1bTmp;

	if(bReset==_TRUE){
		// Reset MCU IO Wrapper- sugggest by SD1-Gimmy
		u1bTmp = rtw_read8(padapter, REG_RSV_CTRL+1);
		rtw_write8(padapter,REG_RSV_CTRL+1, (u1bTmp&(~BIT3)));
	}else{
		// Enable MCU IO Wrapper
		u1bTmp = rtw_read8(padapter, REG_RSV_CTRL+1);
		rtw_write8(padapter, REG_RSV_CTRL+1, u1bTmp|BIT3);
	}

}

void _8051Reset88E(PADAPTER padapter)
{
	u8 u1bTmp;
	
	_MCUIO_Reset88E(padapter,_TRUE);
	u1bTmp = rtw_read8(padapter, REG_SYS_FUNC_EN+1);
	rtw_write8(padapter, REG_SYS_FUNC_EN+1, u1bTmp&(~BIT2));
	_MCUIO_Reset88E(padapter,_FALSE);
	rtw_write8(padapter, REG_SYS_FUNC_EN+1, u1bTmp|(BIT2));
	
	DBG_871X("=====> _8051Reset88E(): 8051 reset success .\n");
}

extern u8 g_fwdl_chksum_fail;
static s32 polling_fwdl_chksum(_adapter *adapter, u32 min_cnt, u32 timeout_ms)
{
	s32 ret = _FAIL;
	u32 value32;
	u32 start = rtw_get_current_time();
	u32 cnt = 0;

	/* polling CheckSum report */
	do {
		cnt++;
		value32 = rtw_read32(adapter, REG_MCUFWDL);
		if (value32 & FWDL_ChkSum_rpt || adapter->bSurpriseRemoved || adapter->bDriverStopped)
			break;
		rtw_yield_os();
	} while (rtw_get_passing_time_ms(start) < timeout_ms || cnt < min_cnt);

	if (!(value32 & FWDL_ChkSum_rpt)) {
		goto exit;
	}

	if (g_fwdl_chksum_fail) {
		DBG_871X("%s: fwdl test case: fwdl_chksum_fail\n", __FUNCTION__);
		g_fwdl_chksum_fail--;
		goto exit;
	}

	ret = _SUCCESS;

exit:
	DBG_871X("%s: Checksum report %s! (%u, %dms), REG_MCUFWDL:0x%08x\n", __FUNCTION__
	, (ret==_SUCCESS)?"OK":"Fail", cnt, rtw_get_passing_time_ms(start), value32);

	return ret;
}

extern u8 g_fwdl_wintint_rdy_fail;
static s32 _FWFreeToGo(_adapter *adapter, u32 min_cnt, u32 timeout_ms)
{
	s32 ret = _FAIL;
	u32	value32;
	u32 start = rtw_get_current_time();
	u32 cnt = 0;

	value32 = rtw_read32(adapter, REG_MCUFWDL);
	value32 |= MCUFWDL_RDY;
	value32 &= ~WINTINI_RDY;
	rtw_write32(adapter, REG_MCUFWDL, value32);

	_8051Reset88E(adapter);

	/*  polling for FW ready */
	do {
		cnt++;
		value32 = rtw_read32(adapter, REG_MCUFWDL);
		if (value32 & WINTINI_RDY || adapter->bSurpriseRemoved || adapter->bDriverStopped)
			break;
		rtw_yield_os();
	} while (rtw_get_passing_time_ms(start) < timeout_ms || cnt < min_cnt);

	if (!(value32 & WINTINI_RDY)) {
		goto exit;
	}

	if (g_fwdl_wintint_rdy_fail) {
		DBG_871X("%s: fwdl test case: wintint_rdy_fail\n", __FUNCTION__);
		g_fwdl_wintint_rdy_fail--;
		goto exit;
	}

	ret = _SUCCESS;

exit:
	DBG_871X("%s: Polling FW ready %s! (%u, %dms), REG_MCUFWDL:0x%08x\n", __FUNCTION__
		, (ret==_SUCCESS)?"OK":"Fail", cnt, rtw_get_passing_time_ms(start), value32);
	return ret;
}

#define IS_FW_81xxC(padapter)	(((GET_HAL_DATA(padapter))->FirmwareSignature & 0xFFF0) == 0x88C0)


#ifdef CONFIG_FILE_FWIMG
extern char *rtw_fw_file_path;
extern char *rtw_fw_wow_file_path;
u8	FwBuffer8188E[FW_8188E_SIZE];
#endif //CONFIG_FILE_FWIMG

//
//	Description:
//		Download 8192C firmware code.
//
//
s32 rtl8188e_FirmwareDownload(PADAPTER padapter, BOOLEAN  bUsedWoWLANFw)
{
	s32	rtStatus = _SUCCESS;
	u8 write_fw = 0;
	u32 fwdl_start_time;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(padapter);	

#ifdef CONFIG_WOWLAN
	u8			*FwImageWoWLAN;
	u32			FwImageWoWLANLen;
#endif

	PRT_FIRMWARE_8188E	pFirmware = NULL;
	PRT_8188E_FIRMWARE_HDR		pFwHdr = NULL;
	
	u8			*pFirmwareBuf;
	u32			FirmwareLen;
#ifdef CONFIG_FILE_FWIMG
	u8 *fwfilepath;
#endif // CONFIG_FILE_FWIMG

	RT_TRACE(_module_hal_init_c_, _drv_info_, ("+%s\n", __FUNCTION__));
	pFirmware = (PRT_FIRMWARE_8188E)rtw_zmalloc(sizeof(RT_FIRMWARE_8188E));
	if(!pFirmware)
	{
		rtStatus = _FAIL;
		goto exit;
	}


//	RT_TRACE(_module_hal_init_c_, _drv_err_, ("rtl8723a_FirmwareDownload: %s\n", pFwImageFileName));

#ifdef CONFIG_FILE_FWIMG
#ifdef CONFIG_WOWLAN
		if (bUsedWoWLANFw)
		{
			fwfilepath = rtw_fw_wow_file_path;
		}
		else
#endif // CONFIG_WOWLAN
		{
			fwfilepath = rtw_fw_file_path;
		}
#endif // CONFIG_FILE_FWIMG

#ifdef CONFIG_FILE_FWIMG
	if(rtw_is_file_readable(fwfilepath) == _TRUE)
	{
		DBG_871X("%s accquire FW from file:%s\n", __FUNCTION__, fwfilepath);
		pFirmware->eFWSource = FW_SOURCE_IMG_FILE;
	}
	else
#endif //CONFIG_FILE_FWIMG
	{
		pFirmware->eFWSource = FW_SOURCE_HEADER_FILE;
	}

	switch(pFirmware->eFWSource)
	{
		case FW_SOURCE_IMG_FILE:
			#ifdef CONFIG_FILE_FWIMG
			rtStatus = rtw_retrive_from_file(fwfilepath, FwBuffer8188E, FW_8188E_SIZE);
			pFirmware->ulFwLength = rtStatus>=0?rtStatus:0;
			pFirmware->szFwBuffer = FwBuffer8188E;
			#endif //CONFIG_FILE_FWIMG
			break;
		case FW_SOURCE_HEADER_FILE:
#ifdef CONFIG_WOWLAN
			if(bUsedWoWLANFw) {
				ODM_ConfigFWWithHeaderFile(&pHalData->odmpriv, CONFIG_FW_WoWLAN, 
					(u8 *)&(pFirmware->szFwBuffer), &(pFirmware->ulFwLength));
				DBG_871X("%s fw:%s, size: %d\n",__FUNCTION__, "WoWLAN", pFirmware->ulFwLength);
			}else
#endif //CONFIG_WOWLAN
			{
				ODM_ConfigFWWithHeaderFile(&pHalData->odmpriv, CONFIG_FW_NIC, 
					(u8 *)&(pFirmware->szFwBuffer), &(pFirmware->ulFwLength));
				DBG_871X("%s fw:%s, size: %d\n", __FUNCTION__, "NIC", pFirmware->ulFwLength);
			}
			break;
	}

	if (pFirmware->ulFwLength > FW_8188E_SIZE) {
		rtStatus = _FAIL;
		DBG_871X_LEVEL(_drv_emerg_, "Firmware size:%u exceed %u\n", pFirmware->ulFwLength, FW_8188E_SIZE);
		goto exit;
	}
	
	pFirmwareBuf = pFirmware->szFwBuffer;
	FirmwareLen = pFirmware->ulFwLength;

	// To Check Fw header. Added by tynli. 2009.12.04.
	pFwHdr = (PRT_8188E_FIRMWARE_HDR)pFirmwareBuf;

	pHalData->FirmwareVersion =  le16_to_cpu(pFwHdr->Version);
	pHalData->FirmwareSubVersion = pFwHdr->Subversion;
	pHalData->FirmwareSignature = le16_to_cpu(pFwHdr->Signature);

	DBG_871X("%s: fw_ver=%x fw_subver=%04x sig=0x%x, Month=%02x, Date=%02x, Hour=%02x, Minute=%02x\n",
		  __FUNCTION__, pHalData->FirmwareVersion, pHalData->FirmwareSubVersion, pHalData->FirmwareSignature
		  ,pFwHdr->Month,pFwHdr->Date,pFwHdr->Hour,pFwHdr->Minute);
		
	if (IS_FW_HEADER_EXIST_88E(pFwHdr))
	{
		// Shift 32 bytes for FW header
		pFirmwareBuf = pFirmwareBuf + 32;
		FirmwareLen = FirmwareLen - 32;
	}

	// Suggested by Filen. If 8051 is running in RAM code, driver should inform Fw to reset by itself,
	// or it will cause download Fw fail. 2010.02.01. by tynli.
	if (rtw_read8(padapter, REG_MCUFWDL) & RAM_DL_SEL) //8051 RAM code
	{
		rtw_write8(padapter, REG_MCUFWDL, 0x00);
		_8051Reset88E(padapter);		
	}

	_FWDownloadEnable_8188E(padapter, _TRUE);
	fwdl_start_time = rtw_get_current_time();
	while(!padapter->bDriverStopped && !padapter->bSurpriseRemoved
			&& (write_fw++ < 3 || rtw_get_passing_time_ms(fwdl_start_time) < 500))
	{
		/* reset FWDL chksum */
		rtw_write8(padapter, REG_MCUFWDL, rtw_read8(padapter, REG_MCUFWDL)|FWDL_ChkSum_rpt);
		
		rtStatus = _WriteFW(padapter, pFirmwareBuf, FirmwareLen);
		if (rtStatus != _SUCCESS)
			continue;

		rtStatus = polling_fwdl_chksum(padapter, 5, 50);
		if (rtStatus == _SUCCESS)
			break;
	}
	_FWDownloadEnable_8188E(padapter, _FALSE);
	if(_SUCCESS != rtStatus)
		goto fwdl_stat;

	rtStatus = _FWFreeToGo(padapter, 10, 200);
	if (_SUCCESS != rtStatus)
		goto fwdl_stat;

fwdl_stat:
	DBG_871X("FWDL %s. write_fw:%u, %dms\n"
		, (rtStatus == _SUCCESS)?"success":"fail"
		, write_fw
		, rtw_get_passing_time_ms(fwdl_start_time)
	);

exit:
	if (pFirmware)
		rtw_mfree((u8*)pFirmware, sizeof(RT_FIRMWARE_8188E));

	return rtStatus;
}

void rtl8188e_InitializeFirmwareVars(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);

	// Init Fw LPS related.
	pwrpriv->bFwCurrentInPSMode = _FALSE;

	//Init H2C cmd.
	rtw_write8(padapter, REG_HMETFR, 0x0f);

	// Init H2C counter. by tynli. 2009.12.09.
	pHalData->LastHMEBoxNum = 0;
}

#ifdef CONFIG_WOWLAN
//===========================================

//
// Description: Prepare some information to Fw for WoWLAN.
//					(1) Download wowlan Fw.
//					(2) Download RSVD page packets.
//					(3) Enable AP offload if needed.
//
// 2011.04.12 by tynli.
//
VOID
SetFwRelatedForWoWLAN8188ES(
		IN		PADAPTER			padapter,
		IN		u8					bHostIsGoingtoSleep
)
{
		int				status=_FAIL;
		HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
		u8				bRecover = _FALSE;
	//
	// 1. Before WoWLAN we need to re-download WoWLAN Fw.
	//
	status = rtl8188e_FirmwareDownload(padapter, bHostIsGoingtoSleep);
	if(status != _SUCCESS) {
		DBG_871X("ConfigFwRelatedForWoWLAN8188ES(): Re-Download Firmware failed!!\n");
		return;
	} else {
		DBG_871X("ConfigFwRelatedForWoWLAN8188ES(): Re-Download Firmware Success !!\n");
	}
	//
	// 2. Re-Init the variables about Fw related setting.
	//
	rtl8188e_InitializeFirmwareVars(padapter);
}
#endif //CONFIG_WOWLAN

static void rtl8188e_free_hal_data(PADAPTER padapter)
{
_func_enter_;

	if(padapter->HalData)
	{
		rtw_vmfree(padapter->HalData, sizeof(HAL_DATA_TYPE));
		padapter->HalData = NULL;
	}
	
_func_exit_;
}

//===========================================================
//				Efuse related code
//===========================================================
enum{
		VOLTAGE_V25						= 0x03,
		LDOE25_SHIFT						= 28 ,
	};

static BOOLEAN
hal_EfusePgPacketWrite2ByteHeader(
	IN	PADAPTER		pAdapter,
	IN	u8				efuseType,
	IN	u16				*pAddr,
	IN	PPGPKT_STRUCT	pTargetPkt,
	IN	BOOLEAN			bPseudoTest);
static BOOLEAN
hal_EfusePgPacketWrite1ByteHeader(
	IN	PADAPTER		pAdapter,
	IN	u8				efuseType,
	IN	u16				*pAddr,
	IN	PPGPKT_STRUCT	pTargetPkt,
	IN	BOOLEAN			bPseudoTest);
static BOOLEAN
hal_EfusePgPacketWriteData(
	IN	PADAPTER		pAdapter,
	IN	u8				efuseType,
	IN	u16				*pAddr,
	IN	PPGPKT_STRUCT	pTargetPkt,
	IN	BOOLEAN			bPseudoTest);

static VOID
hal_EfusePowerSwitch_RTL8188E(
	IN	PADAPTER	pAdapter,
	IN	u8		bWrite,
	IN	u8		PwrState)
{
	u8	tempval;
	u16	tmpV16;

	if (PwrState == _TRUE)
	{
		rtw_write8(pAdapter, REG_EFUSE_ACCESS, EFUSE_ACCESS_ON);

		// 1.2V Power: From VDDON with Power Cut(0x0000h[15]), defualt valid
		tmpV16 = rtw_read16(pAdapter,REG_SYS_ISO_CTRL);
		if( ! (tmpV16 & PWC_EV12V ) ){
			tmpV16 |= PWC_EV12V ;
			 rtw_write16(pAdapter,REG_SYS_ISO_CTRL,tmpV16);
		}
		// Reset: 0x0000h[28], default valid
		tmpV16 =  rtw_read16(pAdapter,REG_SYS_FUNC_EN);
		if( !(tmpV16 & FEN_ELDR) ){
			tmpV16 |= FEN_ELDR ;
			rtw_write16(pAdapter,REG_SYS_FUNC_EN,tmpV16);
		}

		// Clock: Gated(0x0008h[5]) 8M(0x0008h[1]) clock from ANA, default valid
		tmpV16 = rtw_read16(pAdapter,REG_SYS_CLKR);
		if( (!(tmpV16 & LOADER_CLK_EN) )  ||(!(tmpV16 & ANA8M) ) ){
			tmpV16 |= (LOADER_CLK_EN |ANA8M ) ;
			rtw_write16(pAdapter,REG_SYS_CLKR,tmpV16);
		}

		if(bWrite == _TRUE)
		{
			// Enable LDO 2.5V before read/write action
			tempval = rtw_read8(pAdapter, EFUSE_TEST+3);
			tempval &= 0x0F;
			tempval |= (VOLTAGE_V25 << 4);
			rtw_write8(pAdapter, EFUSE_TEST+3, (tempval | 0x80));
		}
	}
	else
	{
		rtw_write8(pAdapter, REG_EFUSE_ACCESS, EFUSE_ACCESS_OFF);

		if(bWrite == _TRUE){
			// Disable LDO 2.5V after read/write action
			tempval = rtw_read8(pAdapter, EFUSE_TEST+3);
			rtw_write8(pAdapter, EFUSE_TEST+3, (tempval & 0x7F));
		}
	}
}

static VOID
rtl8188e_EfusePowerSwitch(
	IN	PADAPTER	pAdapter,
	IN	u8		bWrite,
	IN	u8		PwrState)
{
	hal_EfusePowerSwitch_RTL8188E(pAdapter, bWrite, PwrState);	
}



static bool efuse_read_phymap(
	PADAPTER	Adapter,
	u8			*pbuf,	//buffer to store efuse physical map
	u16			*size	//the max byte to read. will update to byte read
	)
{
	u8 *pos = pbuf;
	u16 limit = *size;
	u16 addr = 0;
	bool reach_end = _FALSE;

	//
	// Refresh efuse init map as all 0xFF.
	//
	_rtw_memset(pbuf, 0xFF, limit);
		
	
	//
	// Read physical efuse content.
	//
	while(addr < limit)
	{
		ReadEFuseByte(Adapter, addr, pos, _FALSE);
		if(*pos != 0xFF)
		{
			pos++;
			addr++;
		}
		else
		{
			reach_end = _TRUE;
			break;
		}
	}

	*size = addr;

	return reach_end;

}

static VOID
Hal_EfuseReadEFuse88E(
	PADAPTER		Adapter,
	u16			_offset,
	u16 			_size_byte,
	u8      		*pbuf,
	IN	BOOLEAN	bPseudoTest
	)
{
	//u8	efuseTbl[EFUSE_MAP_LEN_88E];
	u8	*efuseTbl = NULL;
	u8	rtemp8[1];
	u16	eFuse_Addr = 0;
	u8	offset, wren;
	u16	i, j;
	//u16	eFuseWord[EFUSE_MAX_SECTION_88E][EFUSE_MAX_WORD_UNIT];
	u16	**eFuseWord = NULL;
	u16	efuse_utilized = 0;
	u8	efuse_usage = 0;
	u8	u1temp = 0;

	//
	// Do NOT excess total size of EFuse table. Added by Roger, 2008.11.10.
	//
	if((_offset + _size_byte)>EFUSE_MAP_LEN_88E)
	{// total E-Fuse table is 512bytes
		DBG_8192C("Hal_EfuseReadEFuse88E(): Invalid offset(%#x) with read bytes(%#x)!!\n",_offset, _size_byte);
		goto exit;
	}

	efuseTbl = (u8*)rtw_zmalloc(EFUSE_MAP_LEN_88E);
	if(efuseTbl == NULL)
	{
		DBG_871X("%s: alloc efuseTbl fail!\n", __FUNCTION__);
		goto exit;
	}

	eFuseWord= (u16 **)rtw_malloc2d(EFUSE_MAX_SECTION_88E, EFUSE_MAX_WORD_UNIT, 2);
	if(eFuseWord == NULL)
	{
		DBG_871X("%s: alloc eFuseWord fail!\n", __FUNCTION__);
		goto exit;
	}

	// 0. Refresh efuse init map as all oxFF.
	for (i = 0; i < EFUSE_MAX_SECTION_88E; i++)
		for (j = 0; j < EFUSE_MAX_WORD_UNIT; j++)
			eFuseWord[i][j] = 0xFFFF;

	//
	// 1. Read the first byte to check if efuse is empty!!!
	// 
	//
	ReadEFuseByte(Adapter, eFuse_Addr, rtemp8, bPseudoTest);	
	if(*rtemp8 != 0xFF)
	{
		efuse_utilized++;
		//DBG_8192C("efuse_Addr-%d efuse_data=%x\n", eFuse_Addr, *rtemp8);
		eFuse_Addr++;
	}
	else
	{
		DBG_871X("EFUSE is empty efuse_Addr-%d efuse_data=%x\n", eFuse_Addr, *rtemp8);
		goto exit;
	}


	//
	// 2. Read real efuse content. Filter PG header and every section data.
	//
	while((*rtemp8 != 0xFF) && (eFuse_Addr < EFUSE_REAL_CONTENT_LEN_88E))
	{
		//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("efuse_Addr-%d efuse_data=%x\n", eFuse_Addr-1, *rtemp8));
	
		// Check PG header for section num.
		if((*rtemp8 & 0x1F ) == 0x0F)		//extended header
		{			
			u1temp =( (*rtemp8 & 0xE0) >> 5);
			//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("extended header u1temp=%x *rtemp&0xE0 0x%x\n", u1temp, *rtemp8 & 0xE0));

			//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("extended header u1temp=%x \n", u1temp));
			
			ReadEFuseByte(Adapter, eFuse_Addr, rtemp8, bPseudoTest);	

			//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("extended header efuse_Addr-%d efuse_data=%x\n", eFuse_Addr, *rtemp8));	
			
			if((*rtemp8 & 0x0F) == 0x0F)
			{
				eFuse_Addr++;			
				ReadEFuseByte(Adapter, eFuse_Addr, rtemp8, bPseudoTest); 
				
				if(*rtemp8 != 0xFF && (eFuse_Addr < EFUSE_REAL_CONTENT_LEN_88E))
				{
					eFuse_Addr++;				
				}				
				continue;
			}
			else
			{
				offset = ((*rtemp8 & 0xF0) >> 1) | u1temp;
				wren = (*rtemp8 & 0x0F);
				eFuse_Addr++;				
			}
		}
		else
		{
			offset = ((*rtemp8 >> 4) & 0x0f);
			wren = (*rtemp8 & 0x0f);			
		}
		
		if(offset < EFUSE_MAX_SECTION_88E)
		{
			// Get word enable value from PG header
			//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Offset-%d Worden=%x\n", offset, wren));

			for(i=0; i<EFUSE_MAX_WORD_UNIT; i++)
			{
				// Check word enable condition in the section				
				if(!(wren & 0x01))
				{
					//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Addr=%d \n", eFuse_Addr));
					ReadEFuseByte(Adapter, eFuse_Addr, rtemp8, bPseudoTest);	
					eFuse_Addr++;
					//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Data=0x%x\n", *rtemp8)); 				
					efuse_utilized++;
					eFuseWord[offset][i] = (*rtemp8 & 0xff);
					

					if(eFuse_Addr >= EFUSE_REAL_CONTENT_LEN_88E) 
						break;

					//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Addr=%d", eFuse_Addr));
					ReadEFuseByte(Adapter, eFuse_Addr, rtemp8, bPseudoTest);	
					eFuse_Addr++;
					//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Data=0x%x\n", *rtemp8)); 				
					
					efuse_utilized++;
					eFuseWord[offset][i] |= (((u2Byte)*rtemp8 << 8) & 0xff00);

					if(eFuse_Addr >= EFUSE_REAL_CONTENT_LEN_88E) 
						break;
				}
				
				wren >>= 1;
				
			}
		}

		// Read next PG header
		ReadEFuseByte(Adapter, eFuse_Addr, rtemp8, bPseudoTest);	
		//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Addr=%d rtemp 0x%x\n", eFuse_Addr, *rtemp8));
		
		if(*rtemp8 != 0xFF && (eFuse_Addr < EFUSE_REAL_CONTENT_LEN_88E))
		{
			efuse_utilized++;
			eFuse_Addr++;
		}
	}

	//
	// 3. Collect 16 sections and 4 word unit into Efuse map.
	//
	for(i=0; i<EFUSE_MAX_SECTION_88E; i++)
	{
		for(j=0; j<EFUSE_MAX_WORD_UNIT; j++)
		{
			efuseTbl[(i*8)+(j*2)]=(eFuseWord[i][j] & 0xff);
			efuseTbl[(i*8)+((j*2)+1)]=((eFuseWord[i][j] >> 8) & 0xff);
		}
	}


	//
	// 4. Copy from Efuse map to output pointer memory!!!
	//
	for(i=0; i<_size_byte; i++)
	{		
		pbuf[i] = efuseTbl[_offset+i];
	}

	//
	// 5. Calculate Efuse utilization.
	//
	efuse_usage = (u1Byte)((eFuse_Addr*100)/EFUSE_REAL_CONTENT_LEN_88E);
	rtw_hal_set_hwreg(Adapter, HW_VAR_EFUSE_BYTES, (u8 *)&eFuse_Addr);

exit:
	if(efuseTbl)
		rtw_mfree(efuseTbl, EFUSE_MAP_LEN_88E);

	if(eFuseWord)
		rtw_mfree2d((void *)eFuseWord, EFUSE_MAX_SECTION_88E, EFUSE_MAX_WORD_UNIT, sizeof(u16));
}


static BOOLEAN
Hal_EfuseSwitchToBank(
	IN		PADAPTER	pAdapter,
	IN		u8			bank,
	IN		BOOLEAN		bPseudoTest
	)
{
	BOOLEAN		bRet = _FALSE;
	u32		value32=0;

	//RTPRINT(FEEPROM, EFUSE_PG, ("Efuse switch bank to %d\n", bank));
	if(bPseudoTest)
	{
		fakeEfuseBank = bank;
		bRet = _TRUE;
	}
	else
	{
		if(IS_HARDWARE_TYPE_8723A(pAdapter) &&
			INCLUDE_MULTI_FUNC_BT(pAdapter))
		{
			value32 = rtw_read32(pAdapter, EFUSE_TEST);
			bRet = _TRUE;
			switch(bank)
			{
			case 0:
				value32 = (value32 & ~EFUSE_SEL_MASK) | EFUSE_SEL(EFUSE_WIFI_SEL_0);
				break;
			case 1:
				value32 = (value32 & ~EFUSE_SEL_MASK) | EFUSE_SEL(EFUSE_BT_SEL_0);
				break;
			case 2:
				value32 = (value32 & ~EFUSE_SEL_MASK) | EFUSE_SEL(EFUSE_BT_SEL_1);
				break;
			case 3:
				value32 = (value32 & ~EFUSE_SEL_MASK) | EFUSE_SEL(EFUSE_BT_SEL_2);
				break;
			default:
				value32 = (value32 & ~EFUSE_SEL_MASK) | EFUSE_SEL(EFUSE_WIFI_SEL_0);
				bRet = _FALSE;
				break;
			}
			rtw_write32(pAdapter, EFUSE_TEST, value32);
		}
		else
			bRet = _TRUE;
	}
	return bRet;
}



static VOID
ReadEFuseByIC(
	PADAPTER	Adapter,
	u8		efuseType,
	u16		 _offset,
	u16 		_size_byte,
	u8      	*pbuf,
	IN BOOLEAN	bPseudoTest
	)
{
#ifdef DBG_IOL_READ_EFUSE_MAP
	u8 logical_map[512];
#endif

#ifdef CONFIG_IOL_READ_EFUSE_MAP
	if(!bPseudoTest )//&& rtw_IOL_applied(Adapter))
	{
		int ret = _FAIL;
		if(rtw_IOL_applied(Adapter))
		{
			rtw_hal_power_on(Adapter);
			
			iol_mode_enable(Adapter, 1);
			#ifdef DBG_IOL_READ_EFUSE_MAP
			iol_read_efuse(Adapter, 0, _offset, _size_byte, logical_map);
			#else
			ret = iol_read_efuse(Adapter, 0, _offset, _size_byte, pbuf);
			#endif
			iol_mode_enable(Adapter, 0);	
			
			if(_SUCCESS == ret) 
				goto exit;
		}
	}
#endif
	Hal_EfuseReadEFuse88E(Adapter, _offset, _size_byte, pbuf, bPseudoTest);

exit:
	
#ifdef DBG_IOL_READ_EFUSE_MAP
	if(_rtw_memcmp(logical_map, Adapter->eeprompriv.efuse_eeprom_data, 0x130) == _FALSE)
	{
		int i;
		DBG_871X("%s compare first 0x130 byte fail\n", __FUNCTION__);
		for(i=0;i<512;i++)
		{
			if(i%16==0)
				DBG_871X("0x%03x: ", i);
			DBG_871X("%02x ", logical_map[i]);
			if(i%16==15)
				DBG_871X("\n");
		}
		DBG_871X("\n");
	}
#endif

	return;	
}

static VOID
ReadEFuse_Pseudo(
	PADAPTER	Adapter,
	u8		efuseType,
	u16		 _offset,
	u16 		_size_byte,
	u8      	*pbuf,
	IN BOOLEAN	bPseudoTest
	)
{
	Hal_EfuseReadEFuse88E(Adapter, _offset, _size_byte, pbuf, bPseudoTest);
}

static VOID
rtl8188e_ReadEFuse(
	PADAPTER	Adapter,
	u8		efuseType,
	u16		_offset,
	u16 		_size_byte,
	u8      	*pbuf,
	IN	BOOLEAN	bPseudoTest
	)
{
	if(bPseudoTest)
	{
		ReadEFuse_Pseudo(Adapter, efuseType, _offset, _size_byte, pbuf, bPseudoTest);
	}
	else
	{
		ReadEFuseByIC(Adapter, efuseType, _offset, _size_byte, pbuf, bPseudoTest);
	}
}

//Do not support BT
VOID
Hal_EFUSEGetEfuseDefinition88E(
	IN		PADAPTER	pAdapter,
	IN		u1Byte		efuseType,
	IN		u1Byte		type,
	OUT		PVOID		pOut
	)
{
	switch(type)
	{
		case TYPE_EFUSE_MAX_SECTION:
			{
				u8*	pMax_section;
				pMax_section = (u8*)pOut;
				*pMax_section = EFUSE_MAX_SECTION_88E;
			}
			break;
		case TYPE_EFUSE_REAL_CONTENT_LEN:
			{
				u16* pu2Tmp;
				pu2Tmp = (u16*)pOut;
				*pu2Tmp = EFUSE_REAL_CONTENT_LEN_88E;
			}
			break;
		case TYPE_EFUSE_CONTENT_LEN_BANK:
			{
				u16* pu2Tmp;
				pu2Tmp = (u16*)pOut;
				*pu2Tmp = EFUSE_REAL_CONTENT_LEN_88E;
			}
			break;
		case TYPE_AVAILABLE_EFUSE_BYTES_BANK:
			{
				u16* pu2Tmp;
				pu2Tmp = (u16*)pOut;
				*pu2Tmp = (u16)(EFUSE_REAL_CONTENT_LEN_88E-EFUSE_OOB_PROTECT_BYTES_88E);
			}
			break;
		case TYPE_AVAILABLE_EFUSE_BYTES_TOTAL:
			{
				u16* pu2Tmp;
				pu2Tmp = (u16*)pOut;
				*pu2Tmp = (u16)(EFUSE_REAL_CONTENT_LEN_88E-EFUSE_OOB_PROTECT_BYTES_88E);
			}
			break;
		case TYPE_EFUSE_MAP_LEN:
			{
				u16* pu2Tmp;
				pu2Tmp = (u16*)pOut;
				*pu2Tmp = (u16)EFUSE_MAP_LEN_88E;
			}
			break;
		case TYPE_EFUSE_PROTECT_BYTES_BANK:
			{
				u8* pu1Tmp;
				pu1Tmp = (u8*)pOut;
				*pu1Tmp = (u8)(EFUSE_OOB_PROTECT_BYTES_88E);
			}
			break;
		default:
			{
				u8* pu1Tmp;
				pu1Tmp = (u8*)pOut;
				*pu1Tmp = 0;
			}
			break;
	}
}
VOID
Hal_EFUSEGetEfuseDefinition_Pseudo88E(
	IN		PADAPTER	pAdapter,
	IN		u8			efuseType,
	IN		u8			type,
	OUT		PVOID		pOut
	)
{
	switch(type)
	{
		case TYPE_EFUSE_MAX_SECTION:
			{
				u8*		pMax_section;
				pMax_section = (pu1Byte)pOut;
				*pMax_section = EFUSE_MAX_SECTION_88E;
			}
			break;
		case TYPE_EFUSE_REAL_CONTENT_LEN:
			{
				u16* pu2Tmp;
				pu2Tmp = (pu2Byte)pOut;
				*pu2Tmp = EFUSE_REAL_CONTENT_LEN_88E;
			}
			break;
		case TYPE_EFUSE_CONTENT_LEN_BANK:
			{
				u16* pu2Tmp;
				pu2Tmp = (pu2Byte)pOut;
				*pu2Tmp = EFUSE_REAL_CONTENT_LEN_88E;
			}
			break;
		case TYPE_AVAILABLE_EFUSE_BYTES_BANK:
			{
				u16* pu2Tmp;
				pu2Tmp = (pu2Byte)pOut;
				*pu2Tmp = (u2Byte)(EFUSE_REAL_CONTENT_LEN_88E-EFUSE_OOB_PROTECT_BYTES_88E);
			}
			break;
		case TYPE_AVAILABLE_EFUSE_BYTES_TOTAL:
			{
				u16* pu2Tmp;
				pu2Tmp = (pu2Byte)pOut;
				*pu2Tmp = (u2Byte)(EFUSE_REAL_CONTENT_LEN_88E-EFUSE_OOB_PROTECT_BYTES_88E);
			}
			break;
		case TYPE_EFUSE_MAP_LEN:
			{
				u16* pu2Tmp;
				pu2Tmp = (pu2Byte)pOut;
				*pu2Tmp = (u2Byte)EFUSE_MAP_LEN_88E;
			}
			break;
		case TYPE_EFUSE_PROTECT_BYTES_BANK:
			{
				u8* pu1Tmp;
				pu1Tmp = (u8*)pOut;
				*pu1Tmp = (u8)(EFUSE_OOB_PROTECT_BYTES_88E);
			}
			break;
		default:
			{
				u8* pu1Tmp;
				pu1Tmp = (u8*)pOut;
				*pu1Tmp = 0;
			}
			break;
	}
}


static VOID
rtl8188e_EFUSE_GetEfuseDefinition(
	IN		PADAPTER	pAdapter,
	IN		u8		efuseType,
	IN		u8		type,
	OUT		void		*pOut,
	IN		BOOLEAN		bPseudoTest
	)
{
	if(bPseudoTest)
	{
		Hal_EFUSEGetEfuseDefinition_Pseudo88E(pAdapter, efuseType, type, pOut);
	}
	else
	{
		Hal_EFUSEGetEfuseDefinition88E(pAdapter, efuseType, type, pOut);
	}
}

static u8
Hal_EfuseWordEnableDataWrite(	IN	PADAPTER	pAdapter,
							IN	u16		efuse_addr,
							IN	u8		word_en,
							IN	u8		*data,
							IN	BOOLEAN		bPseudoTest)
{
	u16	tmpaddr = 0;
	u16	start_addr = efuse_addr;
	u8	badworden = 0x0F;
	u8	tmpdata[8];

	_rtw_memset((PVOID)tmpdata, 0xff, PGPKT_DATA_SIZE);
	//RT_TRACE(COMP_EFUSE, DBG_LOUD, ("word_en = %x efuse_addr=%x\n", word_en, efuse_addr));

	if(!(word_en&BIT0))
	{
		tmpaddr = start_addr;
		efuse_OneByteWrite(pAdapter,start_addr++, data[0], bPseudoTest);
		efuse_OneByteWrite(pAdapter,start_addr++, data[1], bPseudoTest);

		efuse_OneByteRead(pAdapter,tmpaddr, &tmpdata[0], bPseudoTest);
		efuse_OneByteRead(pAdapter,tmpaddr+1, &tmpdata[1], bPseudoTest);
		if((data[0]!=tmpdata[0])||(data[1]!=tmpdata[1])){
			badworden &= (~BIT0);
		}
	}
	if(!(word_en&BIT1))
	{
		tmpaddr = start_addr;
		efuse_OneByteWrite(pAdapter,start_addr++, data[2], bPseudoTest);
		efuse_OneByteWrite(pAdapter,start_addr++, data[3], bPseudoTest);

		efuse_OneByteRead(pAdapter,tmpaddr    , &tmpdata[2], bPseudoTest);
		efuse_OneByteRead(pAdapter,tmpaddr+1, &tmpdata[3], bPseudoTest);
		if((data[2]!=tmpdata[2])||(data[3]!=tmpdata[3])){
			badworden &=( ~BIT1);
		}
	}
	if(!(word_en&BIT2))
	{
		tmpaddr = start_addr;
		efuse_OneByteWrite(pAdapter,start_addr++, data[4], bPseudoTest);
		efuse_OneByteWrite(pAdapter,start_addr++, data[5], bPseudoTest);

		efuse_OneByteRead(pAdapter,tmpaddr, &tmpdata[4], bPseudoTest);
		efuse_OneByteRead(pAdapter,tmpaddr+1, &tmpdata[5], bPseudoTest);
		if((data[4]!=tmpdata[4])||(data[5]!=tmpdata[5])){
			badworden &=( ~BIT2);
		}
	}
	if(!(word_en&BIT3))
	{
		tmpaddr = start_addr;
		efuse_OneByteWrite(pAdapter,start_addr++, data[6], bPseudoTest);
		efuse_OneByteWrite(pAdapter,start_addr++, data[7], bPseudoTest);

		efuse_OneByteRead(pAdapter,tmpaddr, &tmpdata[6], bPseudoTest);
		efuse_OneByteRead(pAdapter,tmpaddr+1, &tmpdata[7], bPseudoTest);
		if((data[6]!=tmpdata[6])||(data[7]!=tmpdata[7])){
			badworden &=( ~BIT3);
		}
	}
	return badworden;
}

static u8
Hal_EfuseWordEnableDataWrite_Pseudo(	IN	PADAPTER	pAdapter,
							IN	u16		efuse_addr,
							IN	u8		word_en,
							IN	u8		*data,
							IN	BOOLEAN		bPseudoTest)
{
	u8	ret=0;

	ret = Hal_EfuseWordEnableDataWrite(pAdapter, efuse_addr, word_en, data, bPseudoTest);

	return ret;
}

static u8
rtl8188e_Efuse_WordEnableDataWrite(	IN	PADAPTER	pAdapter,
							IN	u16		efuse_addr,
							IN	u8		word_en,
							IN	u8		*data,
							IN	BOOLEAN		bPseudoTest)
{
	u8	ret=0;

	if(bPseudoTest)
	{
		ret = Hal_EfuseWordEnableDataWrite_Pseudo(pAdapter, efuse_addr, word_en, data, bPseudoTest);
	}
	else
	{
		ret = Hal_EfuseWordEnableDataWrite(pAdapter, efuse_addr, word_en, data, bPseudoTest);
	}

	return ret;
}


static u16
hal_EfuseGetCurrentSize_8188e(IN	PADAPTER	pAdapter,
		IN		BOOLEAN			bPseudoTest)
{
	int	bContinual = _TRUE;

	u16	efuse_addr = 0;
	u8	hoffset=0,hworden=0;
	u8	efuse_data,word_cnts=0;

	if(bPseudoTest)
	{
		efuse_addr = (u16)(fakeEfuseUsedBytes);
	}
	else
	{
		rtw_hal_get_hwreg(pAdapter, HW_VAR_EFUSE_BYTES, (u8 *)&efuse_addr);
	}
	//RTPRINT(FEEPROM, EFUSE_PG, ("hal_EfuseGetCurrentSize_8723A(), start_efuse_addr = %d\n", efuse_addr));

	while (	bContinual &&
			efuse_OneByteRead(pAdapter, efuse_addr ,&efuse_data, bPseudoTest) &&
			AVAILABLE_EFUSE_ADDR(efuse_addr))
	{
		if(efuse_data!=0xFF)
		{
			if((efuse_data&0x1F) == 0x0F)		//extended header
			{
				hoffset = efuse_data;
				efuse_addr++;
				efuse_OneByteRead(pAdapter, efuse_addr ,&efuse_data, bPseudoTest);
				if((efuse_data & 0x0F) == 0x0F)
				{
					efuse_addr++;
					continue;
				}
				else
				{
					hoffset = ((hoffset & 0xE0) >> 5) | ((efuse_data & 0xF0) >> 1);
					hworden = efuse_data & 0x0F;
				}
			}
			else
			{
				hoffset = (efuse_data>>4) & 0x0F;
				hworden =  efuse_data & 0x0F;
			}
			word_cnts = Efuse_CalculateWordCnts(hworden);
			//read next header
			efuse_addr = efuse_addr + (word_cnts*2)+1;
		}
		else
		{
			bContinual = _FALSE ;
		}
	}

	if(bPseudoTest)
	{
		fakeEfuseUsedBytes = efuse_addr;
		//RTPRINT(FEEPROM, EFUSE_PG, ("hal_EfuseGetCurrentSize_8723A(), return %d\n", fakeEfuseUsedBytes));
	}
	else
	{
		rtw_hal_set_hwreg(pAdapter, HW_VAR_EFUSE_BYTES, (u8 *)&efuse_addr);
		//RTPRINT(FEEPROM, EFUSE_PG, ("hal_EfuseGetCurrentSize_8723A(), return %d\n", efuse_addr));
	}

	return efuse_addr;
}

static u16
Hal_EfuseGetCurrentSize_Pseudo(IN	PADAPTER	pAdapter,
		IN		BOOLEAN			bPseudoTest)
{
	u16	ret=0;

	ret = hal_EfuseGetCurrentSize_8188e(pAdapter, bPseudoTest);

	return ret;
}


static u16
rtl8188e_EfuseGetCurrentSize(
	IN	PADAPTER	pAdapter,
	IN	u8			efuseType,
	IN	BOOLEAN		bPseudoTest)
{
	u16	ret=0;

	if(bPseudoTest)
	{
		ret = Hal_EfuseGetCurrentSize_Pseudo(pAdapter, bPseudoTest);
	}
	else
	{
		ret = hal_EfuseGetCurrentSize_8188e(pAdapter, bPseudoTest);
		
	}

	return ret;
}


static int
hal_EfusePgPacketRead_8188e(
	IN	PADAPTER	pAdapter,
	IN	u8			offset,
	IN	u8			*data,
	IN	BOOLEAN		bPseudoTest)
{
	u8	ReadState = PG_STATE_HEADER;

	int	bContinual = _TRUE;
	int	bDataEmpty = _TRUE ;

	u8	efuse_data,word_cnts = 0;
	u16	efuse_addr = 0;
	u8	hoffset = 0,hworden = 0;
	u8	tmpidx = 0;
	u8	tmpdata[8];
	u8	max_section = 0;
	u8	tmp_header = 0;

	EFUSE_GetEfuseDefinition(pAdapter, EFUSE_WIFI, TYPE_EFUSE_MAX_SECTION, (PVOID)&max_section, bPseudoTest);

	if(data==NULL)
		return _FALSE;
	if(offset>max_section)
		return _FALSE;

	_rtw_memset((PVOID)data, 0xff, sizeof(u8)*PGPKT_DATA_SIZE);
	_rtw_memset((PVOID)tmpdata, 0xff, sizeof(u8)*PGPKT_DATA_SIZE);


	//
	// <Roger_TODO> Efuse has been pre-programmed dummy 5Bytes at the end of Efuse by CP.
	// Skip dummy parts to prevent unexpected data read from Efuse.
	// By pass right now. 2009.02.19.
	//
	while(bContinual && AVAILABLE_EFUSE_ADDR(efuse_addr) )
	{
		//-------  Header Read -------------
		if(ReadState & PG_STATE_HEADER)
		{
			if(efuse_OneByteRead(pAdapter, efuse_addr ,&efuse_data, bPseudoTest)&&(efuse_data!=0xFF))
			{
				if(EXT_HEADER(efuse_data))
				{
					tmp_header = efuse_data;
					efuse_addr++;
					efuse_OneByteRead(pAdapter, efuse_addr ,&efuse_data, bPseudoTest);
					if(!ALL_WORDS_DISABLED(efuse_data))
					{
						hoffset = ((tmp_header & 0xE0) >> 5) | ((efuse_data & 0xF0) >> 1);
						hworden = efuse_data & 0x0F;
					}
					else
					{
						DBG_8192C("Error, All words disabled\n");
						efuse_addr++;
						continue;
					}
				}
				else
				{
					hoffset = (efuse_data>>4) & 0x0F;
					hworden =  efuse_data & 0x0F;
				}
				word_cnts = Efuse_CalculateWordCnts(hworden);
				bDataEmpty = _TRUE ;

				if(hoffset==offset)
				{
					for(tmpidx = 0;tmpidx< word_cnts*2 ;tmpidx++)
					{
						if(efuse_OneByteRead(pAdapter, efuse_addr+1+tmpidx ,&efuse_data, bPseudoTest) )
						{
							tmpdata[tmpidx] = efuse_data;
							if(efuse_data!=0xff)
							{
								bDataEmpty = _FALSE;
							}
						}
					}
					if(bDataEmpty==_FALSE){
						ReadState = PG_STATE_DATA;
					}else{//read next header
						efuse_addr = efuse_addr + (word_cnts*2)+1;
						ReadState = PG_STATE_HEADER;
					}
				}
				else{//read next header
					efuse_addr = efuse_addr + (word_cnts*2)+1;
					ReadState = PG_STATE_HEADER;
				}

			}
			else{
				bContinual = _FALSE ;
			}
		}
		//-------  Data section Read -------------
		else if(ReadState & PG_STATE_DATA)
		{
			efuse_WordEnableDataRead(hworden,tmpdata,data);
			efuse_addr = efuse_addr + (word_cnts*2)+1;
			ReadState = PG_STATE_HEADER;
		}

	}

	if(	(data[0]==0xff) &&(data[1]==0xff) && (data[2]==0xff)  && (data[3]==0xff) &&
		(data[4]==0xff) &&(data[5]==0xff) && (data[6]==0xff)  && (data[7]==0xff))
		return _FALSE;
	else
		return _TRUE;

}

static int
Hal_EfusePgPacketRead(	IN	PADAPTER	pAdapter,
					IN	u8			offset,
					IN	u8			*data,
					IN	BOOLEAN			bPseudoTest)
{
	int	ret=0;

	ret = hal_EfusePgPacketRead_8188e(pAdapter, offset, data, bPseudoTest);
	

	return ret;
}

static int
Hal_EfusePgPacketRead_Pseudo(	IN	PADAPTER	pAdapter,
					IN	u8			offset,
					IN	u8			*data,
					IN	BOOLEAN		bPseudoTest)
{
	int	ret=0;

	ret = hal_EfusePgPacketRead_8188e(pAdapter, offset, data, bPseudoTest);

	return ret;
}

static int
rtl8188e_Efuse_PgPacketRead(	IN	PADAPTER	pAdapter,
					IN	u8			offset,
					IN	u8			*data,
					IN	BOOLEAN		bPseudoTest)
{
	int	ret=0;

	if(bPseudoTest)
	{
		ret = Hal_EfusePgPacketRead_Pseudo(pAdapter, offset, data, bPseudoTest);
	}
	else
	{
		ret = Hal_EfusePgPacketRead(pAdapter, offset, data, bPseudoTest);
	}

	return ret;
}

static BOOLEAN
hal_EfuseFixHeaderProcess(
	IN		PADAPTER			pAdapter,
	IN		u8					efuseType,
	IN		PPGPKT_STRUCT		pFixPkt,
	IN		u16					*pAddr,
	IN		BOOLEAN				bPseudoTest
)
{
	u8	originaldata[8], badworden=0;
	u16	efuse_addr=*pAddr;
	u32	PgWriteSuccess=0;

	_rtw_memset((PVOID)originaldata, 0xff, 8);

	if(Efuse_PgPacketRead(pAdapter, pFixPkt->offset, originaldata, bPseudoTest))
	{	//check if data exist
		badworden = Efuse_WordEnableDataWrite(pAdapter, efuse_addr+1, pFixPkt->word_en, originaldata, bPseudoTest);

		if(badworden != 0xf)	// write fail
		{			
			PgWriteSuccess = Efuse_PgPacketWrite(pAdapter, pFixPkt->offset, badworden, originaldata, bPseudoTest);

			if(!PgWriteSuccess)
				return _FALSE;
			else
				efuse_addr = Efuse_GetCurrentSize(pAdapter, efuseType, bPseudoTest);
		}
		else
		{
			efuse_addr = efuse_addr + (pFixPkt->word_cnts*2) +1;
		}
	}
	else
	{
		efuse_addr = efuse_addr + (pFixPkt->word_cnts*2) +1;
	}
	*pAddr = efuse_addr;
	return _TRUE;
}

static BOOLEAN
hal_EfusePgPacketWrite2ByteHeader(
	IN			PADAPTER		pAdapter,
	IN			u8				efuseType,
	IN			u16				*pAddr,
	IN			PPGPKT_STRUCT	pTargetPkt,
	IN			BOOLEAN			bPseudoTest)
{
	BOOLEAN		bRet=_FALSE, bContinual=_TRUE;
	u16	efuse_addr=*pAddr, efuse_max_available_len=0;
	u8	pg_header=0, tmp_header=0, pg_header_temp=0;
	u8	repeatcnt=0;

	//RTPRINT(FEEPROM, EFUSE_PG, ("Wirte 2byte header\n"));
	EFUSE_GetEfuseDefinition(pAdapter, efuseType, TYPE_AVAILABLE_EFUSE_BYTES_BANK, (PVOID)&efuse_max_available_len, bPseudoTest);

	while(efuse_addr < efuse_max_available_len)
	{
		pg_header = ((pTargetPkt->offset & 0x07) << 5) | 0x0F;
		//RTPRINT(FEEPROM, EFUSE_PG, ("pg_header = 0x%x\n", pg_header));
		efuse_OneByteWrite(pAdapter, efuse_addr, pg_header, bPseudoTest);
		efuse_OneByteRead(pAdapter, efuse_addr, &tmp_header, bPseudoTest);

		while(tmp_header == 0xFF)
		{
			if(repeatcnt++ > EFUSE_REPEAT_THRESHOLD_)
			{
				//RTPRINT(FEEPROM, EFUSE_PG, ("Repeat over limit for pg_header!!\n"));
				return _FALSE;
			}

			efuse_OneByteWrite(pAdapter, efuse_addr, pg_header, bPseudoTest);
			efuse_OneByteRead(pAdapter, efuse_addr, &tmp_header, bPseudoTest);
		}

		//to write ext_header
		if(tmp_header == pg_header)
		{
			efuse_addr++;
			pg_header_temp = pg_header;
			pg_header = ((pTargetPkt->offset & 0x78) << 1) | pTargetPkt->word_en;

			efuse_OneByteWrite(pAdapter, efuse_addr, pg_header, bPseudoTest);
			efuse_OneByteRead(pAdapter, efuse_addr, &tmp_header, bPseudoTest);

			while(tmp_header == 0xFF)
			{
				if(repeatcnt++ > EFUSE_REPEAT_THRESHOLD_)
				{
					//RTPRINT(FEEPROM, EFUSE_PG, ("Repeat over limit for ext_header!!\n"));
					return _FALSE;
				}

				efuse_OneByteWrite(pAdapter, efuse_addr, pg_header, bPseudoTest);
				efuse_OneByteRead(pAdapter, efuse_addr, &tmp_header, bPseudoTest);
			}

			if((tmp_header & 0x0F) == 0x0F)	//word_en PG fail
			{
				if(repeatcnt++ > EFUSE_REPEAT_THRESHOLD_)
				{
					//RTPRINT(FEEPROM, EFUSE_PG, ("Repeat over limit for word_en!!\n"));
					return _FALSE;
				}
				else
				{
					efuse_addr++;
					continue;
				}
			}
			else if(pg_header != tmp_header)	//offset PG fail
			{
				PGPKT_STRUCT	fixPkt;
				//RTPRINT(FEEPROM, EFUSE_PG, ("Error condition for offset PG fail, need to cover the existed data\n"));
				fixPkt.offset = ((pg_header_temp & 0xE0) >> 5) | ((tmp_header & 0xF0) >> 1);
				fixPkt.word_en = tmp_header & 0x0F;
				fixPkt.word_cnts = Efuse_CalculateWordCnts(fixPkt.word_en);
				if(!hal_EfuseFixHeaderProcess(pAdapter, efuseType, &fixPkt, &efuse_addr, bPseudoTest))
					return _FALSE;
			}
			else
			{
				bRet = _TRUE;
				break;
			}
		}
		else if ((tmp_header & 0x1F) == 0x0F)		//wrong extended header
		{
			efuse_addr+=2;
			continue;
		}
	}

	*pAddr = efuse_addr;
	return bRet;
}

static BOOLEAN
hal_EfusePgPacketWrite1ByteHeader(
	IN			PADAPTER		pAdapter,
	IN			u8				efuseType,
	IN			u16				*pAddr,
	IN			PPGPKT_STRUCT	pTargetPkt,
	IN			BOOLEAN			bPseudoTest)
{
	BOOLEAN		bRet=_FALSE;
	u8	pg_header=0, tmp_header=0;
	u16	efuse_addr=*pAddr;
	u8	repeatcnt=0;

	//RTPRINT(FEEPROM, EFUSE_PG, ("Wirte 1byte header\n"));
	pg_header = ((pTargetPkt->offset << 4) & 0xf0) |pTargetPkt->word_en;

	efuse_OneByteWrite(pAdapter, efuse_addr, pg_header, bPseudoTest);
	efuse_OneByteRead(pAdapter, efuse_addr, &tmp_header, bPseudoTest);

	while(tmp_header == 0xFF)
	{
		if(repeatcnt++ > EFUSE_REPEAT_THRESHOLD_)
		{
			return _FALSE;
		}
		efuse_OneByteWrite(pAdapter,efuse_addr, pg_header, bPseudoTest);
		efuse_OneByteRead(pAdapter,efuse_addr, &tmp_header, bPseudoTest);
	}

	if(pg_header == tmp_header)
	{
		bRet = _TRUE;
	}
	else
	{
		PGPKT_STRUCT	fixPkt;
		//RTPRINT(FEEPROM, EFUSE_PG, ("Error condition for fixed PG packet, need to cover the existed data\n"));
		fixPkt.offset = (tmp_header>>4) & 0x0F;
		fixPkt.word_en = tmp_header & 0x0F;
		fixPkt.word_cnts = Efuse_CalculateWordCnts(fixPkt.word_en);
		if(!hal_EfuseFixHeaderProcess(pAdapter, efuseType, &fixPkt, &efuse_addr, bPseudoTest))
			return _FALSE;
	}

	*pAddr = efuse_addr;
	return bRet;
}

static BOOLEAN
hal_EfusePgPacketWriteData(
	IN			PADAPTER		pAdapter,
	IN			u8				efuseType,
	IN			u16				*pAddr,
	IN			PPGPKT_STRUCT	pTargetPkt,
	IN			BOOLEAN			bPseudoTest)
{
	BOOLEAN	bRet=_FALSE;
	u16	efuse_addr=*pAddr;
	u8	badworden=0;
	u32	PgWriteSuccess=0;

	badworden = 0x0f;
	badworden = Efuse_WordEnableDataWrite(pAdapter, efuse_addr+1, pTargetPkt->word_en, pTargetPkt->data, bPseudoTest);
	if(badworden == 0x0F)
	{
		// write ok
		//RTPRINT(FEEPROM, EFUSE_PG, ("hal_EfusePgPacketWriteData ok!!\n"));
		return _TRUE;
	}
	else
	{
		//RTPRINT(FEEPROM, EFUSE_PG, ("hal_EfusePgPacketWriteData Fail!!\n"));
		//reorganize other pg packet
		
		PgWriteSuccess = Efuse_PgPacketWrite(pAdapter, pTargetPkt->offset, badworden, pTargetPkt->data, bPseudoTest);
		
		if(!PgWriteSuccess)
			return _FALSE;
		else
			return _TRUE;
	}

	return bRet;
}

static BOOLEAN
hal_EfusePgPacketWriteHeader(
	IN			PADAPTER		pAdapter,
	IN			u8				efuseType,
	IN			u16				*pAddr,
	IN			PPGPKT_STRUCT	pTargetPkt,
	IN			BOOLEAN			bPseudoTest)
{
	BOOLEAN		bRet=_FALSE;

	if(pTargetPkt->offset >= EFUSE_MAX_SECTION_BASE)
	{
		bRet = hal_EfusePgPacketWrite2ByteHeader(pAdapter, efuseType, pAddr, pTargetPkt, bPseudoTest);
	}
	else
	{
		bRet = hal_EfusePgPacketWrite1ByteHeader(pAdapter, efuseType, pAddr, pTargetPkt, bPseudoTest);
	}

	return bRet;
}

static BOOLEAN
wordEnMatched(
	IN	PPGPKT_STRUCT	pTargetPkt,
	IN	PPGPKT_STRUCT	pCurPkt,
	IN	u8				*pWden
)
{
	u8	match_word_en = 0x0F;	// default all words are disabled
	u8	i;

	// check if the same words are enabled both target and current PG packet
	if( ((pTargetPkt->word_en & BIT0) == 0) &&
		((pCurPkt->word_en & BIT0) == 0) )
	{
		match_word_en &= ~BIT0;				// enable word 0
	}
	if( ((pTargetPkt->word_en & BIT1) == 0) &&
		((pCurPkt->word_en & BIT1) == 0) )
	{
		match_word_en &= ~BIT1;				// enable word 1
	}
	if( ((pTargetPkt->word_en & BIT2) == 0) &&
		((pCurPkt->word_en & BIT2) == 0) )
	{
		match_word_en &= ~BIT2;				// enable word 2
	}
	if( ((pTargetPkt->word_en & BIT3) == 0) &&
		((pCurPkt->word_en & BIT3) == 0) )
	{
		match_word_en &= ~BIT3;				// enable word 3
	}

	*pWden = match_word_en;

	if(match_word_en != 0xf)
		return _TRUE;
	else
		return _FALSE;
}

static BOOLEAN
hal_EfuseCheckIfDatafollowed(
	IN		PADAPTER		pAdapter,
	IN		u8				word_cnts,
	IN		u16				startAddr,
	IN		BOOLEAN			bPseudoTest
	)
{
	BOOLEAN		bRet=_FALSE;
	u8	i, efuse_data;

	for(i=0; i<(word_cnts*2) ; i++)
	{
		if(efuse_OneByteRead(pAdapter, (startAddr+i) ,&efuse_data, bPseudoTest)&&(efuse_data != 0xFF))
			bRet = _TRUE;
	}

	return bRet;
}

static BOOLEAN
hal_EfusePartialWriteCheck(
					IN	PADAPTER		pAdapter,
					IN	u8				efuseType,
					IN	u16				*pAddr,
					IN	PPGPKT_STRUCT	pTargetPkt,
					IN	BOOLEAN			bPseudoTest
					)
{
	BOOLEAN		bRet=_FALSE;
	u8	i, efuse_data=0, cur_header=0;
	u8	new_wden=0, matched_wden=0, badworden=0;
	u16	startAddr=0, efuse_max_available_len=0, efuse_max=0;
	PGPKT_STRUCT	curPkt;

	EFUSE_GetEfuseDefinition(pAdapter, efuseType, TYPE_AVAILABLE_EFUSE_BYTES_BANK, (PVOID)&efuse_max_available_len, bPseudoTest);
	EFUSE_GetEfuseDefinition(pAdapter, efuseType, TYPE_EFUSE_REAL_CONTENT_LEN, (PVOID)&efuse_max, bPseudoTest);

	if(efuseType == EFUSE_WIFI)
	{
		if(bPseudoTest)
		{
			startAddr = (u16)(fakeEfuseUsedBytes%EFUSE_REAL_CONTENT_LEN);
		}
		else
		{
			rtw_hal_get_hwreg(pAdapter, HW_VAR_EFUSE_BYTES, (u8 *)&startAddr);
			startAddr%=EFUSE_REAL_CONTENT_LEN;
		}
	}
	else
	{
		if(bPseudoTest)
		{
			startAddr = (u16)(fakeBTEfuseUsedBytes%EFUSE_REAL_CONTENT_LEN);
		}
		else
		{
			startAddr = (u16)(BTEfuseUsedBytes%EFUSE_REAL_CONTENT_LEN);
		}
	}
	//RTPRINT(FEEPROM, EFUSE_PG, ("hal_EfusePartialWriteCheck(), startAddr=%d\n", startAddr));

	while(1)
	{
		if(startAddr >= efuse_max_available_len)
		{
			bRet = _FALSE;
			break;
		}

		if(efuse_OneByteRead(pAdapter, startAddr, &efuse_data, bPseudoTest) && (efuse_data!=0xFF))
		{
			if(EXT_HEADER(efuse_data))
			{
				cur_header = efuse_data;
				startAddr++;
				efuse_OneByteRead(pAdapter, startAddr, &efuse_data, bPseudoTest);
				if(ALL_WORDS_DISABLED(efuse_data))
				{
					//RTPRINT(FEEPROM, EFUSE_PG, ("Error condition, all words disabled"));
					bRet = _FALSE;
					break;
				}
				else
				{
					curPkt.offset = ((cur_header & 0xE0) >> 5) | ((efuse_data & 0xF0) >> 1);
					curPkt.word_en = efuse_data & 0x0F;
				}
			}
			else
			{
				cur_header  =  efuse_data;
				curPkt.offset = (cur_header>>4) & 0x0F;
				curPkt.word_en = cur_header & 0x0F;
			}

			curPkt.word_cnts = Efuse_CalculateWordCnts(curPkt.word_en);
			// if same header is found but no data followed
			// write some part of data followed by the header.
			if( (curPkt.offset == pTargetPkt->offset) &&
				(!hal_EfuseCheckIfDatafollowed(pAdapter, curPkt.word_cnts, startAddr+1, bPseudoTest)) &&
				wordEnMatched(pTargetPkt, &curPkt, &matched_wden) )
			{
				//RTPRINT(FEEPROM, EFUSE_PG, ("Need to partial write data by the previous wrote header\n"));
				// Here to write partial data
				badworden = Efuse_WordEnableDataWrite(pAdapter, startAddr+1, matched_wden, pTargetPkt->data, bPseudoTest);
				if(badworden != 0x0F)
				{
					u32	PgWriteSuccess=0;
					// if write fail on some words, write these bad words again
					
					PgWriteSuccess = Efuse_PgPacketWrite(pAdapter, pTargetPkt->offset, badworden, pTargetPkt->data, bPseudoTest);
					
					if(!PgWriteSuccess)
					{
						bRet = _FALSE;	// write fail, return
						break;
					}
				}
				// partial write ok, update the target packet for later use
				for(i=0; i<4; i++)
				{
					if((matched_wden & (0x1<<i)) == 0)	// this word has been written
					{
						pTargetPkt->word_en |= (0x1<<i);	// disable the word
					}
				}
				pTargetPkt->word_cnts = Efuse_CalculateWordCnts(pTargetPkt->word_en);
			}
			// read from next header
			startAddr = startAddr + (curPkt.word_cnts*2) +1;
		}
		else
		{
			// not used header, 0xff
			*pAddr = startAddr;
			//RTPRINT(FEEPROM, EFUSE_PG, ("Started from unused header offset=%d\n", startAddr));
			bRet = _TRUE;
			break;
		}
	}
	return bRet;
}

static BOOLEAN
hal_EfusePgCheckAvailableAddr(
	IN	PADAPTER	pAdapter,
	IN	u8			efuseType,
	IN	BOOLEAN		bPseudoTest
	)
{
	u16	efuse_max_available_len=0;

	//Change to check TYPE_EFUSE_MAP_LEN ,beacuse 8188E raw 256,logic map over 256.
	EFUSE_GetEfuseDefinition(pAdapter, EFUSE_WIFI, TYPE_EFUSE_MAP_LEN, (PVOID)&efuse_max_available_len, _FALSE);
	
	//EFUSE_GetEfuseDefinition(pAdapter, efuseType, TYPE_AVAILABLE_EFUSE_BYTES_TOTAL, (PVOID)&efuse_max_available_len, bPseudoTest);
	//RTPRINT(FEEPROM, EFUSE_PG, ("efuse_max_available_len = %d\n", efuse_max_available_len));

	if(Efuse_GetCurrentSize(pAdapter, efuseType, bPseudoTest) >= efuse_max_available_len)
	{
		//RTPRINT(FEEPROM, EFUSE_PG, ("hal_EfusePgCheckAvailableAddr error!!\n"));
		return _FALSE;
	}
	return _TRUE;
}

static VOID
hal_EfuseConstructPGPkt(
					IN	u8 				offset,
					IN	u8				word_en,
					IN	u8				*pData,
					IN	PPGPKT_STRUCT	pTargetPkt

)
{
	_rtw_memset((PVOID)pTargetPkt->data, 0xFF, sizeof(u8)*8);
	pTargetPkt->offset = offset;
	pTargetPkt->word_en= word_en;
	efuse_WordEnableDataRead(word_en, pData, pTargetPkt->data);
	pTargetPkt->word_cnts = Efuse_CalculateWordCnts(pTargetPkt->word_en);

	//RTPRINT(FEEPROM, EFUSE_PG, ("hal_EfuseConstructPGPkt(), targetPkt, offset=%d, word_en=0x%x, word_cnts=%d\n", pTargetPkt->offset, pTargetPkt->word_en, pTargetPkt->word_cnts));
}

static BOOLEAN
hal_EfusePgPacketWrite_BT(
					IN	PADAPTER	pAdapter,
					IN	u8 			offset,
					IN	u8			word_en,
					IN	u8			*pData,
					IN	BOOLEAN		bPseudoTest
					)
{
	PGPKT_STRUCT 	targetPkt;
	u16	startAddr=0;
	u8	efuseType=EFUSE_BT;

	if(!hal_EfusePgCheckAvailableAddr(pAdapter, efuseType, bPseudoTest))
		return _FALSE;

	hal_EfuseConstructPGPkt(offset, word_en, pData, &targetPkt);

	if(!hal_EfusePartialWriteCheck(pAdapter, efuseType, &startAddr, &targetPkt, bPseudoTest))
		return _FALSE;

	if(!hal_EfusePgPacketWriteHeader(pAdapter, efuseType, &startAddr, &targetPkt, bPseudoTest))
		return _FALSE;

	if(!hal_EfusePgPacketWriteData(pAdapter, efuseType, &startAddr, &targetPkt, bPseudoTest))
		return _FALSE;

	return _TRUE;
}

static BOOLEAN
hal_EfusePgPacketWrite_8188e(
					IN	PADAPTER		pAdapter,
					IN	u8 			offset,
					IN	u8			word_en,
					IN	u8			*pData,
					IN	BOOLEAN		bPseudoTest
					)
{
	PGPKT_STRUCT 	targetPkt;
	u16			startAddr=0;
	u8			efuseType=EFUSE_WIFI;

	if(!hal_EfusePgCheckAvailableAddr(pAdapter, efuseType, bPseudoTest))
		return _FALSE;

	hal_EfuseConstructPGPkt(offset, word_en, pData, &targetPkt);

	if(!hal_EfusePartialWriteCheck(pAdapter, efuseType, &startAddr, &targetPkt, bPseudoTest))
		return _FALSE;

	if(!hal_EfusePgPacketWriteHeader(pAdapter, efuseType, &startAddr, &targetPkt, bPseudoTest))
		return _FALSE;

	if(!hal_EfusePgPacketWriteData(pAdapter, efuseType, &startAddr, &targetPkt, bPseudoTest))
		return _FALSE;

	return _TRUE;
}


static int
Hal_EfusePgPacketWrite_Pseudo(IN	PADAPTER	pAdapter,
					IN	u8 			offset,
					IN	u8			word_en,
					IN	u8			*data,
					IN	BOOLEAN		bPseudoTest)
{
	int ret;

	ret = hal_EfusePgPacketWrite_8188e(pAdapter, offset, word_en, data, bPseudoTest);

	return ret;
}

static int
Hal_EfusePgPacketWrite(IN	PADAPTER	pAdapter,
					IN	u8 			offset,
					IN	u8			word_en,
					IN	u8			*data,
					IN	BOOLEAN		bPseudoTest)
{
	int	ret=0;
	ret = hal_EfusePgPacketWrite_8188e(pAdapter, offset, word_en, data, bPseudoTest);
	

	return ret;
}

static int
rtl8188e_Efuse_PgPacketWrite(IN	PADAPTER	pAdapter,
					IN	u8 			offset,
					IN	u8			word_en,
					IN	u8			*data,
					IN	BOOLEAN		bPseudoTest)
{
	int	ret;

	if(bPseudoTest)
	{
		ret = Hal_EfusePgPacketWrite_Pseudo(pAdapter, offset, word_en, data, bPseudoTest);
	}
	else
	{
		ret = Hal_EfusePgPacketWrite(pAdapter, offset, word_en, data, bPseudoTest);
	}
	return ret;
}

static HAL_VERSION
ReadChipVersion8188E(
	IN	PADAPTER	padapter
	)
{
	u32				value32;
	HAL_VERSION				ChipVersion;
	HAL_DATA_TYPE	*pHalData;


	pHalData = GET_HAL_DATA(padapter);

	value32 = rtw_read32(padapter, REG_SYS_CFG);
	ChipVersion.ICType = CHIP_8188E ;
	ChipVersion.ChipType = ((value32 & RTL_ID) ? TEST_CHIP : NORMAL_CHIP);

	ChipVersion.RFType = RF_TYPE_1T1R;
	ChipVersion.VendorType = ((value32 & VENDOR_ID) ? CHIP_VENDOR_UMC : CHIP_VENDOR_TSMC);
	ChipVersion.CUTVersion = (value32 & CHIP_VER_RTL_MASK)>>CHIP_VER_RTL_SHIFT; // IC version (CUT)

	// For regulator mode. by tynli. 2011.01.14
	pHalData->RegulatorMode = ((value32 & TRP_BT_EN) ? RT_LDO_REGULATOR : RT_SWITCHING_REGULATOR);

	ChipVersion.ROMVer = 0;	// ROM code version.	
	pHalData->MultiFunc = RT_MULTI_FUNC_NONE;
	

//#if DBG
#if 1	
	dump_chip_info(ChipVersion);
#endif

	pHalData->VersionID = ChipVersion;

	if (IS_1T2R(ChipVersion)){
		pHalData->rf_type = RF_1T2R;
		pHalData->NumTotalRFPath = 2;
	}
	else if (IS_2T2R(ChipVersion)){
		pHalData->rf_type = RF_2T2R;
		pHalData->NumTotalRFPath = 2;
	}
	else{
		pHalData->rf_type = RF_1T1R;
		pHalData->NumTotalRFPath = 1;
	}
		
	MSG_8192C("RF_Type is %x!!\n", pHalData->rf_type);

	return ChipVersion;
}

static void rtl8188e_read_chip_version(PADAPTER padapter)
{
	ReadChipVersion8188E(padapter);	
}
void rtl8188e_GetHalODMVar(	
	PADAPTER				Adapter,
	HAL_ODM_VARIABLE		eVariable,
	PVOID					pValue1,
	BOOLEAN					bSet)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T podmpriv = &pHalData->odmpriv;
	switch(eVariable){
		case HAL_ODM_STA_INFO:
			break;
		default:
			break;
	}
}
void rtl8188e_SetHalODMVar(
	PADAPTER				Adapter,
	HAL_ODM_VARIABLE		eVariable,
	PVOID					pValue1,
	BOOLEAN					bSet)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T podmpriv = &pHalData->odmpriv;
	//_irqL irqL;
	switch(eVariable){
		case HAL_ODM_STA_INFO:
			{	
				struct sta_info *psta = (struct sta_info *)pValue1;				
				if(bSet){
					DBG_8192C("### Set STA_(%d) info\n",psta->mac_id);
					ODM_CmnInfoPtrArrayHook(podmpriv, ODM_CMNINFO_STA_STATUS,psta->mac_id,psta);
					#if(RATE_ADAPTIVE_SUPPORT==1)
					ODM_RAInfo_Init(podmpriv,psta->mac_id);
					#endif
				}
				else{
					DBG_8192C("### Clean STA_(%d) info\n",psta->mac_id);
					//_enter_critical_bh(&pHalData->odm_stainfo_lock, &irqL);
					ODM_CmnInfoPtrArrayHook(podmpriv, ODM_CMNINFO_STA_STATUS,psta->mac_id,NULL);
					
					//_exit_critical_bh(&pHalData->odm_stainfo_lock, &irqL);
			            }
			}
			break;
		case HAL_ODM_P2P_STATE:		
				ODM_CmnInfoUpdate(podmpriv,ODM_CMNINFO_WIFI_DIRECT,bSet);
			break;
		case HAL_ODM_WIFI_DISPLAY_STATE:
				ODM_CmnInfoUpdate(podmpriv,ODM_CMNINFO_WIFI_DISPLAY,bSet);
			break;
		default:
			break;
	}
}	

void rtl8188e_start_thread(_adapter *padapter)
{
#ifdef CONFIG_SDIO_HCI
#ifndef CONFIG_SDIO_TX_TASKLET
	struct xmit_priv *xmitpriv = &padapter->xmitpriv;

	xmitpriv->SdioXmitThread = kthread_run(rtl8188es_xmit_thread, padapter, "RTWHALXT");
	if (IS_ERR(xmitpriv->SdioXmitThread))
	{
		RT_TRACE(_module_hal_xmit_c_, _drv_err_, ("%s: start rtl8188es_xmit_thread FAIL!!\n", __FUNCTION__));
	}
#endif
#endif
}

void rtl8188e_stop_thread(_adapter *padapter)
{
#ifdef CONFIG_SDIO_HCI
#ifndef CONFIG_SDIO_TX_TASKLET
	struct xmit_priv *xmitpriv = &padapter->xmitpriv;

	// stop xmit_buf_thread
	if (xmitpriv->SdioXmitThread ) {
		_rtw_up_sema(&xmitpriv->SdioXmitSema);
		_rtw_down_sema(&xmitpriv->SdioXmitTerminateSema);
		xmitpriv->SdioXmitThread = 0;
	}
#endif
#endif
}
void hal_notch_filter_8188e(_adapter *adapter, bool enable)
{
	if (enable) {
		DBG_871X("Enable notch filter\n");
		rtw_write8(adapter, rOFDM0_RxDSP+1, rtw_read8(adapter, rOFDM0_RxDSP+1) | BIT1);
	} else {
		DBG_871X("Disable notch filter\n");
		rtw_write8(adapter, rOFDM0_RxDSP+1, rtw_read8(adapter, rOFDM0_RxDSP+1) & ~BIT1);
	}
}

void UpdateHalRAMask8188E(PADAPTER padapter, u32 mac_id, u8 rssi_level)
{
	u32	mask,rate_bitmap;
	u8	shortGIrate = _FALSE;
	struct sta_info	*psta;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	//struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if (mac_id >= NUM_STA) //CAM_SIZE
	{
		return;
	}

	psta = pmlmeinfo->FW_sta_info[mac_id].psta;
	if(psta == NULL)
	{
		return;
	}

	shortGIrate = query_ra_short_GI(psta);

	mask = psta->ra_mask;

	rate_bitmap = 0xffffffff;					
	rate_bitmap = ODM_Get_Rate_Bitmap(&pHalData->odmpriv,mac_id,mask,rssi_level);
	DBG_871X("%s => mac_id:%d, networkType:0x%02x, mask:0x%08x\n\t ==> rssi_level:%d, rate_bitmap:0x%08x\n",
			__FUNCTION__,mac_id,psta->wireless_mode,mask,rssi_level,rate_bitmap);

	mask &= rate_bitmap;
	
	if(pHalData->fw_ractrl == _TRUE)
	{
		u8 arg = 0;

		//arg = (cam_idx-4)&0x1f;//MACID
		arg = mac_id&0x1f;//MACID
		
		arg |= BIT(7);
		
		if (shortGIrate==_TRUE)
			arg |= BIT(5);
		mask |= ((psta->raid<<28)&0xf0000000);
		DBG_871X("update raid entry, mask=0x%x, arg=0x%x\n", mask, arg);

#ifdef CONFIG_INTEL_PROXIM
		if(padapter->proximity.proxim_on ==_TRUE){
			arg &= ~BIT(6);
		}
		else {
			arg |= BIT(6);
		}
#endif //CONFIG_INTEL_PROXIM

		//to do ,for 8188E-SMIC
		/*
		*(pu4Byte)&RateMask=EF4Byte((ratr_bitmap&0x0fffffff) | (ratr_index<<28));
		RateMask[4] = macId | (bShortGI?0x20:0x00) | 0x80;
		*/	
		rtl8188e_set_raid_cmd(padapter, mask);	
		
	}
	else
	{	

#if(RATE_ADAPTIVE_SUPPORT == 1)	

		ODM_RA_UpdateRateInfo_8188E(
				&(pHalData->odmpriv),
				mac_id,
				psta->raid, 
				mask,
				shortGIrate
				);

#endif		
	}
}

void rtl8188e_set_hal_ops(struct hal_ops *pHalFunc)
{
	pHalFunc->free_hal_data = &rtl8188e_free_hal_data;

	pHalFunc->dm_init = &rtl8188e_init_dm_priv;
	pHalFunc->dm_deinit = &rtl8188e_deinit_dm_priv;

	pHalFunc->read_chip_version = &rtl8188e_read_chip_version;

	pHalFunc->UpdateRAMaskHandler = &UpdateHalRAMask8188E;

	pHalFunc->set_bwmode_handler = &PHY_SetBWMode8188E;
	pHalFunc->set_channel_handler = &PHY_SwChnl8188E;
	pHalFunc->set_chnl_bw_handler = &PHY_SetSwChnlBWMode8188E;

	pHalFunc->set_tx_power_level_handler = &PHY_SetTxPowerLevel8188E;
	pHalFunc->get_tx_power_level_handler = &PHY_GetTxPowerLevel8188E;

	pHalFunc->hal_dm_watchdog = &rtl8188e_HalDmWatchDog;

	pHalFunc->Add_RateATid = &rtl8188e_Add_RateATid;

	pHalFunc->run_thread= &rtl8188e_start_thread;
	pHalFunc->cancel_thread= &rtl8188e_stop_thread;

#ifdef CONFIG_ANTENNA_DIVERSITY
	pHalFunc->AntDivBeforeLinkHandler = &AntDivBeforeLink8188E;
	pHalFunc->AntDivCompareHandler = &AntDivCompare8188E;
#endif

	pHalFunc->read_bbreg = &PHY_QueryBBReg8188E;
	pHalFunc->write_bbreg = &PHY_SetBBReg8188E;
	pHalFunc->read_rfreg = &PHY_QueryRFReg8188E;
	pHalFunc->write_rfreg = &PHY_SetRFReg8188E;


	// Efuse related function
	pHalFunc->EfusePowerSwitch = &rtl8188e_EfusePowerSwitch;
	pHalFunc->ReadEFuse = &rtl8188e_ReadEFuse;
	pHalFunc->EFUSEGetEfuseDefinition = &rtl8188e_EFUSE_GetEfuseDefinition;
	pHalFunc->EfuseGetCurrentSize = &rtl8188e_EfuseGetCurrentSize;
	pHalFunc->Efuse_PgPacketRead = &rtl8188e_Efuse_PgPacketRead;
	pHalFunc->Efuse_PgPacketWrite = &rtl8188e_Efuse_PgPacketWrite;
	pHalFunc->Efuse_WordEnableDataWrite = &rtl8188e_Efuse_WordEnableDataWrite;

#ifdef DBG_CONFIG_ERROR_DETECT
	pHalFunc->sreset_init_value = &sreset_init_value;
	pHalFunc->sreset_reset_value = &sreset_reset_value;
	pHalFunc->silentreset = &sreset_reset;
	pHalFunc->sreset_xmit_status_check = &rtl8188e_sreset_xmit_status_check;
	pHalFunc->sreset_linked_status_check  = &rtl8188e_sreset_linked_status_check;
	pHalFunc->sreset_get_wifi_status  = &sreset_get_wifi_status;
	pHalFunc->sreset_inprogress= &sreset_inprogress;
#endif //DBG_CONFIG_ERROR_DETECT

	pHalFunc->GetHalODMVarHandler = &rtl8188e_GetHalODMVar;
	pHalFunc->SetHalODMVarHandler = &rtl8188e_SetHalODMVar;

#ifdef CONFIG_IOL
	pHalFunc->IOL_exec_cmds_sync = &rtl8188e_IOL_exec_cmds_sync;
#endif

	pHalFunc->hal_notch_filter = &hal_notch_filter_8188e;

}

u8 GetEEPROMSize8188E(PADAPTER padapter)
{
	u8 size = 0;
	u32	cr;

	cr = rtw_read16(padapter, REG_9346CR);
	// 6: EEPROM used is 93C46, 4: boot from E-Fuse.
	size = (cr & BOOT_FROM_EEPROM) ? 6 : 4;

	MSG_8192C("EEPROM type is %s\n", size==4 ? "E-FUSE" : "93C46");

	return size;
}

#if defined(CONFIG_USB_HCI) || defined(CONFIG_SDIO_HCI) || defined(CONFIG_PCI_HCI)
//-------------------------------------------------------------------------
//
// LLT R/W/Init function
//
//-------------------------------------------------------------------------
s32 _LLTWrite(PADAPTER padapter, u32 address, u32 data)
{
	s32	status = _SUCCESS;
	s8	count = POLLING_LLT_THRESHOLD;
	u32	value = _LLT_INIT_ADDR(address) | _LLT_INIT_DATA(data) | _LLT_OP(_LLT_WRITE_ACCESS);
	
	rtw_write32(padapter, REG_LLT_INIT, value);

	//polling
	do {
		value = rtw_read32(padapter, REG_LLT_INIT);
		if (_LLT_NO_ACTIVE == _LLT_OP_VALUE(value)) {
			break;
		}
	} while (--count);

	if(count<=0){
		DBG_871X("Failed to polling write LLT done at address %d!\n", address);
		status = _FAIL;
	}

	return status;
}

u8 _LLTRead(PADAPTER padapter, u32 address)
{
	s32	count = 0;
	u32	value = _LLT_INIT_ADDR(address) | _LLT_OP(_LLT_READ_ACCESS);
	u16	LLTReg = REG_LLT_INIT;


	rtw_write32(padapter, LLTReg, value);

	//polling and get value
	do {
		value = rtw_read32(padapter, LLTReg);
		if (_LLT_NO_ACTIVE == _LLT_OP_VALUE(value)) {
			return (u8)value;
		}

		if (count > POLLING_LLT_THRESHOLD) {
			RT_TRACE(_module_hal_init_c_, _drv_err_, ("Failed to polling read LLT done at address %d!\n", address));
			break;
		}
	} while (count++);

	return 0xFF;
}

s32 InitLLTTable(PADAPTER padapter, u8 txpktbuf_bndy)
{
	s32	status = _FAIL;
	u32	i;
	u32	Last_Entry_Of_TxPktBuf = LAST_ENTRY_OF_TX_PKT_BUFFER_8188E;// 176, 22k
	HAL_DATA_TYPE *pHalData	= GET_HAL_DATA(padapter);

#if defined(CONFIG_IOL_LLT)
	if(rtw_IOL_applied(padapter))
	{		
		status = iol_InitLLTTable(padapter, txpktbuf_bndy);			
	}
	else
#endif
	{
		for (i = 0; i < (txpktbuf_bndy - 1); i++) {
			status = _LLTWrite(padapter, i, i + 1);
			if (_SUCCESS != status) {
				return status;
			}
		}

		// end of list
		status = _LLTWrite(padapter, (txpktbuf_bndy - 1), 0xFF);
		if (_SUCCESS != status) {
			return status;
		}

		// Make the other pages as ring buffer
		// This ring buffer is used as beacon buffer if we config this MAC as two MAC transfer.
		// Otherwise used as local loopback buffer.
		for (i = txpktbuf_bndy; i < Last_Entry_Of_TxPktBuf; i++) {
			status = _LLTWrite(padapter, i, (i + 1));
			if (_SUCCESS != status) {
				return status;
			}
		}

		// Let last entry point to the start entry of ring buffer
		status = _LLTWrite(padapter, Last_Entry_Of_TxPktBuf, txpktbuf_bndy);
		if (_SUCCESS != status) {
			return status;
		}
	}

	return status;
}
#endif


void
Hal_InitPGData88E(PADAPTER	padapter)
{
	EEPROM_EFUSE_PRIV *pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);
//	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	u32			i;
	u16			value16;

	if(_FALSE == pEEPROM->bautoload_fail_flag)
	{ // autoload OK.
		if (is_boot_from_eeprom(padapter))
		{
			// Read all Content from EEPROM or EFUSE.
			for(i = 0; i < HWSET_MAX_SIZE_88E; i += 2)
			{
//				value16 = EF2Byte(ReadEEprom(pAdapter, (u2Byte) (i>>1)));
//				*((u16*)(&PROMContent[i])) = value16;
			}
		}
		else
		{
			// Read EFUSE real map to shadow.
			EFUSE_ShadowMapUpdate(padapter, EFUSE_WIFI, _FALSE);
		}
	}
	else
	{//autoload fail
		RT_TRACE(_module_hci_hal_init_c_, _drv_notice_, ("AutoLoad Fail reported from CR9346!!\n"));
//		pHalData->AutoloadFailFlag = _TRUE;
		//update to default value 0xFF
		if (!is_boot_from_eeprom(padapter))
			EFUSE_ShadowMapUpdate(padapter, EFUSE_WIFI, _FALSE);
	}
}

void
Hal_EfuseParseIDCode88E(
	IN	PADAPTER	padapter,
	IN	u8			*hwinfo
	)
{
	EEPROM_EFUSE_PRIV *pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);
//	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	u16			EEPROMId;


	// Checl 0x8129 again for making sure autoload status!!
	EEPROMId = le16_to_cpu(*((u16*)hwinfo));
	if (EEPROMId != RTL_EEPROM_ID)
	{
		DBG_8192C("EEPROM ID(%#x) is invalid!!\n", EEPROMId);
		pEEPROM->bautoload_fail_flag = _TRUE;
	}
	else
	{
		pEEPROM->bautoload_fail_flag = _FALSE;
	}

	DBG_871X("EEPROM ID=0x%04x\n", EEPROMId);
}

static void
Hal_ReadPowerValueFromPROM_8188E(
	IN	PADAPTER 		padapter,
	IN	PTxPowerInfo24G	pwrInfo24G,
	IN	u8*				PROMContent,
	IN	BOOLEAN			AutoLoadFail
	)
{
	u32 rfPath, eeAddr=EEPROM_TX_PWR_INX_88E, group,TxCount=0;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	
	_rtw_memset(pwrInfo24G, 0, sizeof(TxPowerInfo24G));

	if(AutoLoadFail)
	{	
		for(rfPath = 0 ; rfPath < pHalData->NumTotalRFPath ; rfPath++)
		{
			//2.4G default value
			for(group = 0 ; group < MAX_CHNL_GROUP_24G; group++)
			{
				pwrInfo24G->IndexCCK_Base[rfPath][group] =	EEPROM_DEFAULT_24G_INDEX;
				pwrInfo24G->IndexBW40_Base[rfPath][group] =	EEPROM_DEFAULT_24G_INDEX;
			}
			for(TxCount=0;TxCount<MAX_TX_COUNT;TxCount++)
			{
				if(TxCount==0)
				{
					pwrInfo24G->BW20_Diff[rfPath][0] =	EEPROM_DEFAULT_24G_HT20_DIFF;
					pwrInfo24G->OFDM_Diff[rfPath][0] =	EEPROM_DEFAULT_24G_OFDM_DIFF;
				}
				else
				{
					pwrInfo24G->BW20_Diff[rfPath][TxCount] =	EEPROM_DEFAULT_DIFF;
					pwrInfo24G->BW40_Diff[rfPath][TxCount] =	EEPROM_DEFAULT_DIFF;
					pwrInfo24G->CCK_Diff[rfPath][TxCount] =	EEPROM_DEFAULT_DIFF;
					pwrInfo24G->OFDM_Diff[rfPath][TxCount] =	EEPROM_DEFAULT_DIFF;
				}
			}	
			
			
		}
		
		//pHalData->bNOPG = TRUE;				
		return;
	}	

	for(rfPath = 0 ; rfPath < pHalData->NumTotalRFPath ; rfPath++)
	{
		//2.4G default value
		for(group = 0 ; group < MAX_CHNL_GROUP_24G; group++)
		{
			//printk(" IndexCCK_Base rfPath:%d group:%d,eeAddr:0x%02x ",rfPath,group,eeAddr);
			pwrInfo24G->IndexCCK_Base[rfPath][group] =	PROMContent[eeAddr++];
			//printk(" IndexCCK_Base:%02x \n",pwrInfo24G->IndexCCK_Base[rfPath][group] );
			if(pwrInfo24G->IndexCCK_Base[rfPath][group] == 0xFF)
			{
				pwrInfo24G->IndexCCK_Base[rfPath][group] = EEPROM_DEFAULT_24G_INDEX;
//				pHalData->bNOPG = TRUE; 							
			}
		}
		for(group = 0 ; group < MAX_CHNL_GROUP_24G-1; group++)
		{
			//printk(" IndexBW40_Base rfPath:%d group:%d,eeAddr:0x%02x ",rfPath,group,eeAddr);
			pwrInfo24G->IndexBW40_Base[rfPath][group] =	PROMContent[eeAddr++];
			//printk(" IndexBW40_Base: %02x \n",pwrInfo24G->IndexBW40_Base[rfPath][group]  );
			if(pwrInfo24G->IndexBW40_Base[rfPath][group] == 0xFF)
				pwrInfo24G->IndexBW40_Base[rfPath][group] =	EEPROM_DEFAULT_24G_INDEX;
		}			
		for(TxCount=0;TxCount<MAX_TX_COUNT_8188E;TxCount++)
		{
			if(TxCount==0)
			{
				pwrInfo24G->BW40_Diff[rfPath][TxCount] = 0;
				if(PROMContent[eeAddr] == 0xFF)
					pwrInfo24G->BW20_Diff[rfPath][TxCount] =	EEPROM_DEFAULT_24G_HT20_DIFF;
				else
				{
					pwrInfo24G->BW20_Diff[rfPath][TxCount] =	(PROMContent[eeAddr]&0xf0)>>4;				
				 	if(pwrInfo24G->BW20_Diff[rfPath][TxCount] & BIT3)		//4bit sign number to 8 bit sign number
						pwrInfo24G->BW20_Diff[rfPath][TxCount] |= 0xF0;
				}

				if(PROMContent[eeAddr] == 0xFF)
					pwrInfo24G->OFDM_Diff[rfPath][TxCount] =	EEPROM_DEFAULT_24G_OFDM_DIFF;				
				else 
				{
					pwrInfo24G->OFDM_Diff[rfPath][TxCount] =	(PROMContent[eeAddr]&0x0f);					
					if(pwrInfo24G->OFDM_Diff[rfPath][TxCount] & BIT3)		//4bit sign number to 8 bit sign number
						pwrInfo24G->OFDM_Diff[rfPath][TxCount] |= 0xF0;
				}		
				pwrInfo24G->CCK_Diff[rfPath][TxCount] = 0;
				eeAddr++;
			}
			else
			{				
				if(PROMContent[eeAddr] == 0xFF)
					pwrInfo24G->BW40_Diff[rfPath][TxCount] =	EEPROM_DEFAULT_DIFF;								
				else 
				{
					pwrInfo24G->BW40_Diff[rfPath][TxCount] =	(PROMContent[eeAddr]&0xf0)>>4;				
					if(pwrInfo24G->BW40_Diff[rfPath][TxCount] & BIT3)		//4bit sign number to 8 bit sign number
						pwrInfo24G->BW40_Diff[rfPath][TxCount] |= 0xF0;
				}
				
				if(PROMContent[eeAddr] == 0xFF)
					pwrInfo24G->BW20_Diff[rfPath][TxCount] =	EEPROM_DEFAULT_DIFF;				
				else 
				{
					pwrInfo24G->BW20_Diff[rfPath][TxCount] =	(PROMContent[eeAddr]&0x0f);				
					if(pwrInfo24G->BW20_Diff[rfPath][TxCount] & BIT3)		//4bit sign number to 8 bit sign number
						pwrInfo24G->BW20_Diff[rfPath][TxCount] |= 0xF0;
				}
				eeAddr++;
				
				if(PROMContent[eeAddr] == 0xFF)
					pwrInfo24G->OFDM_Diff[rfPath][TxCount] =	EEPROM_DEFAULT_DIFF;								
				else 
				{
					pwrInfo24G->OFDM_Diff[rfPath][TxCount] =	(PROMContent[eeAddr]&0xf0)>>4;				
					if(pwrInfo24G->OFDM_Diff[rfPath][TxCount] & BIT3)		//4bit sign number to 8 bit sign number
						pwrInfo24G->OFDM_Diff[rfPath][TxCount] |= 0xF0;
				}
			
				if(PROMContent[eeAddr] == 0xFF)
					pwrInfo24G->CCK_Diff[rfPath][TxCount] =	EEPROM_DEFAULT_DIFF;												
				else 
				{
					pwrInfo24G->CCK_Diff[rfPath][TxCount] =	(PROMContent[eeAddr]&0x0f);				
					if(pwrInfo24G->CCK_Diff[rfPath][TxCount] & BIT3)		//4bit sign number to 8 bit sign number
						pwrInfo24G->CCK_Diff[rfPath][TxCount] |= 0xF0;
				}
				eeAddr++;
			}
		}

	}


}

static u8
Hal_GetChnlGroup(
	IN	u8 chnl
	)
{
	u8	group=0;

	if (chnl < 3)			// Cjanel 1-3
		group = 0;
	else if (chnl < 9)		// Channel 4-9
		group = 1;
	else					// Channel 10-14
		group = 2;

	return group;
}
static u8 
Hal_GetChnlGroup88E(
	IN	u8 	chnl,
	OUT u8*	pGroup
	)
{
	u8 bIn24G=_TRUE;

	if(chnl<=14)
	{
		bIn24G=_TRUE;

		if (chnl < 3)			// Chanel 1-2
			*pGroup = 0;
		else if (chnl < 6)		// Channel 3-5
			*pGroup = 1;
		else	 if(chnl <9)		// Channel 6-8
			*pGroup = 2;
		else if(chnl <12)		// Channel 9-11
			*pGroup = 3;
		else if(chnl <14)		// Channel 12-13
			*pGroup = 4;
		else if(chnl ==14)		// Channel 14
			*pGroup = 5;	
		else
		{
			//RT_TRACE(COMP_EFUSE,DBG_LOUD,("==>Hal_GetChnlGroup88E in 2.4 G, but Channel %d in Group not found \n",chnl));
		}
	}
	else
	{
		bIn24G=_FALSE;
		
		if (chnl <=40)	
			*pGroup = 0;
		else if (chnl <=48)
			*pGroup = 1;
		else	 if(chnl <=56)	
			*pGroup = 2;
		else if(chnl <=64)	
			*pGroup = 3;
		else if(chnl <=104)
			*pGroup = 4;
		else if(chnl <=112)
			*pGroup = 5;	
		else if(chnl <=120)
			*pGroup = 5;	
		else if(chnl <=128)
			*pGroup = 6;		
		else if(chnl <=136)
			*pGroup = 7;		
		else if(chnl <=144)
			*pGroup = 8;		
		else if(chnl <=153)
			*pGroup = 9;		
		else if(chnl <=161)
			*pGroup = 10;		
		else if(chnl <=177)
			*pGroup = 11;	
		else
		{
			//RT_TRACE(COMP_EFUSE,DBG_LOUD,("==>Hal_GetChnlGroup88E in 5G, but Channel %d in Group not found \n",chnl));
		}

	}
	//RT_TRACE(COMP_EFUSE,DBG_LOUD,("<==Hal_GetChnlGroup88E,  Channel = %d, bIn24G =%d,\n",chnl,bIn24G));
	return bIn24G;
}

void Hal_ReadPowerSavingMode88E(
	PADAPTER		padapter,
	IN	u8*			hwinfo,
	IN	BOOLEAN			AutoLoadFail
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(padapter);
	u8 tmpvalue;

	if(AutoLoadFail){
		pwrctl->bHWPowerdown = _FALSE;
		pwrctl->bSupportRemoteWakeup = _FALSE;
	}
	else	{		

		//hw power down mode selection , 0:rf-off / 1:power down

		if(padapter->registrypriv.hwpdn_mode==2)
			pwrctl->bHWPowerdown = (hwinfo[EEPROM_RF_FEATURE_OPTION_88E] & BIT4);
		else
			pwrctl->bHWPowerdown = padapter->registrypriv.hwpdn_mode;
				
		// decide hw if support remote wakeup function
		// if hw supported, 8051 (SIE) will generate WeakUP signal( D+/D- toggle) when autoresume
#ifdef CONFIG_USB_HCI
		pwrctl->bSupportRemoteWakeup = (hwinfo[EEPROM_USB_OPTIONAL_FUNCTION0] & BIT1)?_TRUE :_FALSE;
#endif //CONFIG_USB_HCI

		//if(SUPPORT_HW_RADIO_DETECT(Adapter))	
			//Adapter->registrypriv.usbss_enable = pwrctl->bSupportRemoteWakeup ;
		
		DBG_8192C("%s...bHWPwrPindetect(%x)-bHWPowerdown(%x) ,bSupportRemoteWakeup(%x)\n",__FUNCTION__,
			pwrctl->bHWPwrPindetect, pwrctl->bHWPowerdown, pwrctl->bSupportRemoteWakeup);

		DBG_8192C("### PS params=>  power_mgnt(%x),usbss_enable(%x) ###\n",padapter->registrypriv.power_mgnt,padapter->registrypriv.usbss_enable);
	
	}

}

void
Hal_ReadTxPowerInfo88E(
	IN	PADAPTER 		padapter,
	IN	u8*				PROMContent,
	IN	BOOLEAN			AutoLoadFail
	)
{	
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	TxPowerInfo24G		pwrInfo24G;
	u8			rfPath, ch, group=0, rfPathMax=1;
	u8			pwr, diff,bIn24G,TxCount;


	Hal_ReadPowerValueFromPROM_8188E(padapter, &pwrInfo24G, PROMContent, AutoLoadFail);

	if(!AutoLoadFail)
		pHalData->bTXPowerDataReadFromEEPORM = TRUE;		

	//for(rfPath = 0 ; rfPath < MAX_RF_PATH ; rfPath++)
	for(rfPath = 0 ; rfPath < pHalData->NumTotalRFPath ; rfPath++)
	{
		for(ch = 0 ; ch < CHANNEL_MAX_NUMBER ; ch++)
		{
			bIn24G = Hal_GetChnlGroup88E(ch+1,&group);
			if(bIn24G)
			{

				pHalData->Index24G_CCK_Base[rfPath][ch]=pwrInfo24G.IndexCCK_Base[rfPath][group];

				if(ch==(14-1))
					pHalData->Index24G_BW40_Base[rfPath][ch]=pwrInfo24G.IndexBW40_Base[rfPath][4];
				else
					pHalData->Index24G_BW40_Base[rfPath][ch]=pwrInfo24G.IndexBW40_Base[rfPath][group];
			}
			
			if(bIn24G)
			{
				DBG_871X("======= Path %d, Channel %d =======\n",rfPath,ch+1 );			
				DBG_871X("Index24G_CCK_Base[%d][%d] = 0x%x\n",rfPath,ch+1 ,pHalData->Index24G_CCK_Base[rfPath][ch]);
				DBG_871X("Index24G_BW40_Base[%d][%d] = 0x%x\n",rfPath,ch+1 ,pHalData->Index24G_BW40_Base[rfPath][ch]);			
			}			
		}	

		for(TxCount=0;TxCount<MAX_TX_COUNT_8188E;TxCount++)
		{
			pHalData->CCK_24G_Diff[rfPath][TxCount]=pwrInfo24G.CCK_Diff[rfPath][TxCount];
			pHalData->OFDM_24G_Diff[rfPath][TxCount]=pwrInfo24G.OFDM_Diff[rfPath][TxCount];
			pHalData->BW20_24G_Diff[rfPath][TxCount]=pwrInfo24G.BW20_Diff[rfPath][TxCount];
			pHalData->BW40_24G_Diff[rfPath][TxCount]=pwrInfo24G.BW40_Diff[rfPath][TxCount];
#if DBG			
			DBG_871X("======= TxCount %d =======\n",TxCount );	
			DBG_871X("CCK_24G_Diff[%d][%d]= %d\n",rfPath,TxCount,pHalData->CCK_24G_Diff[rfPath][TxCount]);
			DBG_871X("OFDM_24G_Diff[%d][%d]= %d\n",rfPath,TxCount,pHalData->OFDM_24G_Diff[rfPath][TxCount]);
			DBG_871X("BW20_24G_Diff[%d][%d]= %d\n",rfPath,TxCount,pHalData->BW20_24G_Diff[rfPath][TxCount]);
			DBG_871X("BW40_24G_Diff[%d][%d]= %d\n",rfPath,TxCount,pHalData->BW40_24G_Diff[rfPath][TxCount]);
#endif							
		}
	}

	
	// 2010/10/19 MH Add Regulator recognize for EU.
	if(!AutoLoadFail)
	{
		struct registry_priv  *registry_par = &padapter->registrypriv;
		
		if(PROMContent[EEPROM_RF_BOARD_OPTION_88E] == 0xFF)
			pHalData->EEPROMRegulatory = (EEPROM_DEFAULT_BOARD_OPTION&0x7);	//bit0~2
		else
			pHalData->EEPROMRegulatory = (PROMContent[EEPROM_RF_BOARD_OPTION_88E]&0x7);	//bit0~2
		
	}
	else
	{
		pHalData->EEPROMRegulatory = 0;
	}
	DBG_871X("EEPROMRegulatory = 0x%x\n", pHalData->EEPROMRegulatory);

}


VOID
Hal_EfuseParseXtal_8188E(
	IN	PADAPTER		pAdapter,
	IN	u8*			hwinfo,
	IN	BOOLEAN		AutoLoadFail
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);

	if(!AutoLoadFail)
	{
		pHalData->CrystalCap = hwinfo[EEPROM_XTAL_88E];
		if(pHalData->CrystalCap == 0xFF)
			pHalData->CrystalCap = EEPROM_Default_CrystalCap_88E;	
	}
	else
	{
		pHalData->CrystalCap = EEPROM_Default_CrystalCap_88E;
	}
	DBG_871X("CrystalCap: 0x%2x\n", pHalData->CrystalCap);
}

void
Hal_EfuseParseBoardType88E(
	IN	PADAPTER		pAdapter,
	IN	u8*				hwinfo,
	IN	BOOLEAN			AutoLoadFail
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);

	if (!AutoLoadFail)
		pHalData->BoardType = ((hwinfo[EEPROM_RF_BOARD_OPTION_88E]&0xE0)>>5);
	else
		pHalData->BoardType = 0;
	DBG_871X("Board Type: 0x%2x\n", pHalData->BoardType);
}

void
Hal_EfuseParseEEPROMVer88E(
	IN	PADAPTER		padapter,
	IN	u8*			hwinfo,
	IN	BOOLEAN			AutoLoadFail
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	if(!AutoLoadFail){		
		pHalData->EEPROMVersion = hwinfo[EEPROM_VERSION_88E];
		if(pHalData->EEPROMVersion == 0xFF)
			pHalData->EEPROMVersion = EEPROM_Default_Version;				
	}
	else{
		pHalData->EEPROMVersion = 1;
	}
	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("Hal_EfuseParseEEPROMVer(), EEVer = %d\n",
		pHalData->EEPROMVersion));
}

void
rtl8188e_EfuseParseChnlPlan(
	IN	PADAPTER		padapter,
	IN	u8*			hwinfo,
	IN	BOOLEAN			AutoLoadFail
	)
{
	padapter->mlmepriv.ChannelPlan = hal_com_config_channel_plan(
		padapter
		, hwinfo?hwinfo[EEPROM_ChannelPlan_88E]:0xFF
		, padapter->registrypriv.channel_plan
		, RT_CHANNEL_DOMAIN_WORLD_NULL
		, AutoLoadFail
	);

	Hal_ChannelPlanToRegulation(padapter, padapter->mlmepriv.ChannelPlan);

	DBG_871X("mlmepriv.ChannelPlan = 0x%02x\n", padapter->mlmepriv.ChannelPlan);
}

void
Hal_EfuseParseCustomerID88E(
	IN	PADAPTER		padapter,
	IN	u8*			hwinfo,
	IN	BOOLEAN			AutoLoadFail
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	if (!AutoLoadFail)
	{
		pHalData->EEPROMCustomerID = hwinfo[EEPROM_CustomID_88E];
		//pHalData->EEPROMSubCustomerID = hwinfo[EEPROM_CustomID_88E];
	}
	else
	{
		pHalData->EEPROMCustomerID = 0;
		pHalData->EEPROMSubCustomerID = 0;
	}
	DBG_871X("EEPROM Customer ID: 0x%2x\n", pHalData->EEPROMCustomerID);
	//DBG_871X("EEPROM SubCustomer ID: 0x%02x\n", pHalData->EEPROMSubCustomerID);
}


void
Hal_ReadAntennaDiversity88E(
	IN	PADAPTER		pAdapter,
	IN	u8*				PROMContent,
	IN	BOOLEAN			AutoLoadFail
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct registry_priv	*registry_par = &pAdapter->registrypriv;	

	if(!AutoLoadFail)
	{	
		// Antenna Diversity setting.
		if(registry_par->antdiv_cfg == 2)// 2:By EFUSE
		{
			pHalData->AntDivCfg = (PROMContent[EEPROM_RF_BOARD_OPTION_88E]&0x18)>>3;
			if(PROMContent[EEPROM_RF_BOARD_OPTION_88E] == 0xFF)			
				pHalData->AntDivCfg = (EEPROM_DEFAULT_BOARD_OPTION&0x18)>>3;;				
		}
		else
		{
			pHalData->AntDivCfg = registry_par->antdiv_cfg ;  // 0:OFF , 1:ON, 2:By EFUSE
		}

		if(registry_par->antdiv_type == 0)// If TRxAntDivType is AUTO in advanced setting, use EFUSE value instead.
		{
			pHalData->TRxAntDivType = PROMContent[EEPROM_RF_ANTENNA_OPT_88E];
			if (pHalData->TRxAntDivType == 0xFF)
				pHalData->TRxAntDivType = CG_TRX_HW_ANTDIV; // For 88EE, 1Tx and 1RxCG are fixed.(1Ant, Tx and RxCG are both on aux port)
		}
		else{
			pHalData->TRxAntDivType = registry_par->antdiv_type ;
		}
			
		if (pHalData->TRxAntDivType == CG_TRX_HW_ANTDIV || pHalData->TRxAntDivType == CGCS_RX_HW_ANTDIV)
			pHalData->AntDivCfg = 1; // 0xC1[3] is ignored.
	}
	else
	{
		pHalData->AntDivCfg = 0;
	}
	
	DBG_871X("EEPROM : AntDivCfg = %x, TRxAntDivType = %x\n",pHalData->AntDivCfg, pHalData->TRxAntDivType);


}

void
Hal_ReadThermalMeter_88E(
	IN	PADAPTER	Adapter,	
	IN	u8* 			PROMContent,
	IN	BOOLEAN 	AutoloadFail
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u1Byte			tempval;

	//
	// ThermalMeter from EEPROM
	//
	if(!AutoloadFail)	
		pHalData->EEPROMThermalMeter = PROMContent[EEPROM_THERMAL_METER_88E];
	else
		pHalData->EEPROMThermalMeter = EEPROM_Default_ThermalMeter_88E;
//	pHalData->EEPROMThermalMeter = (tempval&0x1f);	//[4:0]

	if(pHalData->EEPROMThermalMeter == 0xff || AutoloadFail)
	{
		pHalData->bAPKThermalMeterIgnore = _TRUE;
		pHalData->EEPROMThermalMeter = EEPROM_Default_ThermalMeter_88E;		
	}

	//pHalData->ThermalMeter[0] = pHalData->EEPROMThermalMeter;	
	DBG_871X("ThermalMeter = 0x%x\n", pHalData->EEPROMThermalMeter);	

}


void
Hal_InitChannelPlan(
	IN		PADAPTER	padapter
	)
{
#if 0
	PMGNT_INFO		pMgntInfo = &(padapter->MgntInfo);
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);

	if((pMgntInfo->RegChannelPlan >= RT_CHANNEL_DOMAIN_MAX) || (pHalData->EEPROMChannelPlan & EEPROM_CHANNEL_PLAN_BY_HW_MASK))
	{
		pMgntInfo->ChannelPlan = hal_MapChannelPlan8192C(padapter, (pHalData->EEPROMChannelPlan & (~(EEPROM_CHANNEL_PLAN_BY_HW_MASK))));
		pMgntInfo->bChnlPlanFromHW = (pHalData->EEPROMChannelPlan & EEPROM_CHANNEL_PLAN_BY_HW_MASK) ? TRUE : FALSE; // User cannot change  channel plan.
	}
	else
	{
		pMgntInfo->ChannelPlan = (RT_CHANNEL_DOMAIN)pMgntInfo->RegChannelPlan;
	}

	switch(pMgntInfo->ChannelPlan)
	{
		case RT_CHANNEL_DOMAIN_GLOBAL_DOAMIN:
		{
			PRT_DOT11D_INFO	pDot11dInfo = GET_DOT11D_INFO(pMgntInfo);

			pDot11dInfo->bEnabled = TRUE;
		}
			RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("ReadAdapterInfo8187(): Enable dot11d when RT_CHANNEL_DOMAIN_GLOBAL_DOAMIN!\n"));
			break;

		default: //for MacOSX compiler warning.
			break;
	}

	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("RegChannelPlan(%d) EEPROMChannelPlan(%d)", pMgntInfo->RegChannelPlan, pHalData->EEPROMChannelPlan));
	RT_TRACE(_module_hci_hal_init_c_, _drv_info_, ("Mgnt ChannelPlan = %d\n" , pMgntInfo->ChannelPlan));
#endif
}

BOOLEAN HalDetectPwrDownMode88E(PADAPTER Adapter)
{
	u8 tmpvalue = 0;
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);
	struct pwrctrl_priv *pwrctrlpriv = adapter_to_pwrctl(Adapter);

	EFUSE_ShadowRead(Adapter, 1, EEPROM_RF_FEATURE_OPTION_88E, (u32 *)&tmpvalue);

	// 2010/08/25 MH INF priority > PDN Efuse value.
	if(tmpvalue & BIT(4) && pwrctrlpriv->reg_pdnmode)
	{
		pHalData->pwrdown = _TRUE;
	}
	else
	{
		pHalData->pwrdown = _FALSE;
	}

	DBG_8192C("HalDetectPwrDownMode(): PDN=%d\n", pHalData->pwrdown);

	return pHalData->pwrdown;
}	// HalDetectPwrDownMode

#ifdef CONFIG_WOWLAN
void Hal_DetectWoWMode(PADAPTER pAdapter)
{
	adapter_to_pwrctl(pAdapter)->bSupportRemoteWakeup = _TRUE;
	DBG_871X("%s\n", __func__);
}
#endif

//====================================================================================
//
// 20100209 Joseph:
// This function is used only for 92C to set REG_BCN_CTRL(0x550) register.
// We just reserve the value of the register in variable pHalData->RegBcnCtrlVal and then operate
// the value of the register via atomic operation.
// This prevents from race condition when setting this register.
// The value of pHalData->RegBcnCtrlVal is initialized in HwConfigureRTL8192CE() function.
//
void SetBcnCtrlReg(
	PADAPTER	padapter,
	u8		SetBits,
	u8		ClearBits)
{
	PHAL_DATA_TYPE pHalData;


	pHalData = GET_HAL_DATA(padapter);

	pHalData->RegBcnCtrlVal |= SetBits;
	pHalData->RegBcnCtrlVal &= ~ClearBits;

#if 0
//#ifdef CONFIG_SDIO_HCI
	if (pHalData->sdio_himr & (SDIO_HIMR_TXBCNOK_MSK | SDIO_HIMR_TXBCNERR_MSK))
		pHalData->RegBcnCtrlVal |= EN_TXBCN_RPT;
#endif

	rtw_write8(padapter, REG_BCN_CTRL, (u8)pHalData->RegBcnCtrlVal);
}

void _InitTransferPageSize(PADAPTER padapter)
{
	// Tx page size is always 128.

	u8 value8;
	value8 = _PSRX(PBP_128) | _PSTX(PBP_128);
	rtw_write8(padapter, REG_PBP, value8);
}

void SetHwReg8188E(_adapter *adapter, u8 variable, u8 *val)
{
	HAL_DATA_TYPE *HalData = GET_HAL_DATA(adapter);

_func_enter_;

	switch (variable) {
	case HW_VAR_CHECK_TXBUF:
	{
		u8 retry_limit;
		u16 val16;
		u32 reg_200 = 0, reg_204 = 0;
		u32 init_reg_200 = 0, init_reg_204 = 0;
		u32 start = rtw_get_current_time();
		u32 pass_ms;
		int i = 0;

		retry_limit = 0x01;

		val16 = retry_limit << RETRY_LIMIT_SHORT_SHIFT | retry_limit << RETRY_LIMIT_LONG_SHIFT;
		rtw_write16(adapter, REG_RL, val16);

		while (rtw_get_passing_time_ms(start) < 2000
			&& !adapter->bDriverStopped && !adapter->bSurpriseRemoved
		) {
			reg_200 = rtw_read32(adapter, 0x200);
			reg_204 = rtw_read32(adapter, 0x204);

			if (i == 0) {
				init_reg_200 = reg_200;
				init_reg_204 = reg_204;
			}

			i++;
			if ((reg_200 & 0x00ffffff) != (reg_204 & 0x00ffffff)) {
				//DBG_871X("%s: (HW_VAR_CHECK_TXBUF)TXBUF NOT empty - 0x204=0x%x, 0x200=0x%x (%d)\n", __FUNCTION__, reg_204, reg_200, i);
				rtw_msleep_os(10);
			} else {
				break;
			}
		}

		pass_ms = rtw_get_passing_time_ms(start);

		if (adapter->bDriverStopped || adapter->bSurpriseRemoved) {
		} else if (pass_ms >= 2000 || (reg_200 & 0x00ffffff) != (reg_204 & 0x00ffffff)) {
			DBG_871X_LEVEL(_drv_always_, "%s:(HW_VAR_CHECK_TXBUF)NOT empty(%d) in %d ms\n", __FUNCTION__, i, pass_ms);
			DBG_871X_LEVEL(_drv_always_, "%s:(HW_VAR_CHECK_TXBUF)0x200=0x%08x, 0x204=0x%08x (0x%08x, 0x%08x)\n",
				__FUNCTION__, reg_200, reg_204, init_reg_200, init_reg_204);
			//rtw_warn_on(1);
		} else {
			DBG_871X("%s:(HW_VAR_CHECK_TXBUF)TXBUF Empty(%d) in %d ms\n", __FUNCTION__, i, pass_ms);
		}

		retry_limit = 0x30;
		val16 = retry_limit << RETRY_LIMIT_SHORT_SHIFT | retry_limit << RETRY_LIMIT_LONG_SHIFT;
		rtw_write16(adapter, REG_RL, val16);
	}
		break;
	case HW_VAR_RESP_SIFS:
	{
		struct mlme_ext_priv	*pmlmeext = &adapter->mlmeextpriv;

		if((pmlmeext->cur_wireless_mode==WIRELESS_11G) ||
		(pmlmeext->cur_wireless_mode==WIRELESS_11BG))//WIRELESS_MODE_G){
		{
			val[0] = 0x0a;
			val[1] = 0x0a;
		} else {
			val[0] = 0x0e;
			val[1] = 0x0e;
		}

		// SIFS for OFDM Data ACK
		rtw_write8(adapter, REG_SIFS_CTX+1, val[0]);
		// SIFS for OFDM consecutive tx like CTS data!
		rtw_write8(adapter, REG_SIFS_TRX+1, val[1]);

		rtw_write8(adapter, REG_SPEC_SIFS+1, val[0]);
		rtw_write8(adapter, REG_MAC_SPEC_SIFS+1, val[0]);
				
		//RESP_SIFS for OFDM
		rtw_write8(adapter, REG_RESP_SIFS_OFDM, val[0]);
		rtw_write8(adapter, REG_RESP_SIFS_OFDM+1, val[0]);
	}
		break;
	default:
		SetHwReg(adapter, variable, val);
		break;
	}

_func_exit_;
}

void GetHwReg8188E(_adapter *adapter, u8 variable, u8 *val)
{
	HAL_DATA_TYPE *HalData = GET_HAL_DATA(adapter);

_func_enter_;

	switch (variable) {
	case HW_VAR_SYS_CLKR:
		*val = rtw_read8(adapter, REG_SYS_CLKR);
		break;
	default:
		GetHwReg(adapter, variable, val);
		break;
	}

_func_exit_;
}

