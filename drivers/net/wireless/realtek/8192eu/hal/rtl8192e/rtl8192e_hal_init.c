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
#define _RTL8192E_HAL_INIT_C_

//#include <drv_types.h>
#include <rtl8192e_hal.h>

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
			_8051Reset8192E(padapter);
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
	rtw_write8(padapter, REG_DWBCN0_CTRL_8192E+1, txpktbuf_bndy);
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

	eFuseWord= (u16 **)rtw_malloc2d(EFUSE_MAX_SECTION_88E, EFUSE_MAX_WORD_UNIT, sizeof(u16));
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
		bcnhead = rtw_read8(adapter, REG_DWBCN0_CTRL_8192E+1);

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


	rtw_write8(padapter, REG_DWBCN0_CTRL_8192E+1, txpktbuf_bndy);
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

s32 rtl8192e_iol_efuse_patch(PADAPTER padapter)
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
	rtw_write8(padapter, REG_DWBCN0_CTRL_8192E+1, iocfg_bndy);
	rst = iol_execute(padapter, CMD_IOCONFIG);
	
	return rst;
}

int rtl8192e_IOL_exec_cmds_sync(ADAPTER *adapter, struct xmit_frame *xmit_frame, u32 max_wating_ms,u32 bndy_cnt)
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
	rtw_write8(adapter, REG_DWBCN0_CTRL_8192E+1, 0);	
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
//-------------------------------------------------------------------------
//
// LLT R/W/Init function
//
//-------------------------------------------------------------------------
static s32 _LLTWrite(PADAPTER padapter, u32 address, u32 data)
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

	if (count <=0 ) {
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

s32 InitLLTTable8192E(PADAPTER padapter, u8 txpktbuf_bndy)
{
	s32	status = _FAIL;	
	HAL_DATA_TYPE *pHalData	= GET_HAL_DATA(padapter);
	u32 value32;
	u32 start = 0, passing_time = 0;
#if 1
	value32 = rtw_read32(padapter,REG_AUTO_LLT);

	rtw_write32(	padapter,REG_AUTO_LLT,value32 |BIT_AUTO_INIT_LLT);
	start = rtw_get_current_time();
	while( ((value32 = rtw_read32(padapter,REG_AUTO_LLT))&BIT_AUTO_INIT_LLT ) 
		&& ((passing_time=rtw_get_passing_time_ms(start))<1000)
	){
		rtw_usleep_os(2);	
	}
	if(value32 & BIT_AUTO_INIT_LLT){
		DBG_8192C("Auto %s(%08x) failed\n",__FUNCTION__,value32);
		status = _FAIL;	
	}
	else{
		DBG_8192C("Auto %s success \n",__FUNCTION__);
		status = _SUCCESS;	
	}

#else
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
#endif
	return status;
}

BOOLEAN HalDetectPwrDownMode8192E(PADAPTER Adapter)
{
	u8 tmpvalue = 0;
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);
	struct pwrctrl_priv *pwrctrlpriv = adapter_to_pwrctl(Adapter);

	EFUSE_ShadowRead(Adapter, 1, EEPROM_RF_OPT3_92C, (u32 *)&tmpvalue);

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
	u32 bcn_ctrl_reg;

#ifdef CONFIG_CONCURRENT_MODE
	if (padapter->iface_type == IFACE_PORT1)	
		bcn_ctrl_reg = REG_BCN_CTRL_1;			
	else
#endif	
		bcn_ctrl_reg = REG_BCN_CTRL;



	pHalData = GET_HAL_DATA(padapter);

	pHalData->RegBcnCtrlVal |= SetBits;
	pHalData->RegBcnCtrlVal &= ~ClearBits;

#if 0
//#ifdef CONFIG_SDIO_HCI
	if (pHalData->sdio_himr & (SDIO_HIMR_TXBCNOK_MSK | SDIO_HIMR_TXBCNERR_MSK))
		pHalData->RegBcnCtrlVal |= EN_TXBCN_RPT;
#endif

	rtw_write8(padapter, bcn_ctrl_reg, (u8)pHalData->RegBcnCtrlVal);
}

static VOID
_FWDownloadEnable_8192E(
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
	}
}
#define MAX_REG_BOLCK_SIZE 254
static int
_BlockWrite_8192E(
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
		ret = rtw_writeN(padapter, (FW_START_ADDRESS + i * blockSize_p1), blockSize_p1, (bufferPtr + i * blockSize_p1));
#else
		ret = rtw_write32(padapter, (FW_START_ADDRESS + i * blockSize_p1), le32_to_cpu(*((u32*)(bufferPtr + i * blockSize_p1))));
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
			ret = rtw_write32(padapter, (FW_START_ADDRESS + blockCount_p1 * blockSize_p1), 
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
			ret = rtw_writeN(padapter, (FW_START_ADDRESS + offset + i*blockSize_p2), blockSize_p2, (bufferPtr + offset + i*blockSize_p2));
			
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
			ret =rtw_write8(padapter, (FW_START_ADDRESS + offset + i), *(bufferPtr + offset + i));
			
			if(ret == _FAIL)
				goto exit;
		}
	}

exit:
	return ret;
}

static int
_PageWrite_8192E(
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

	return _BlockWrite_8192E(padapter,buffer,size);
}

static VOID
_FillDummy_8192E(
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
_WriteFW_8192E(
	IN		PADAPTER		padapter,
	IN		PVOID			buffer,
	IN		u32			size
	)
{
	// Since we need dynamic decide method of dwonload fw, so we call this function to get chip version.
	// We can remove _ReadChipVersion from ReadpadapterInfo8192C later.
	int	ret = _SUCCESS;
	u32	pageNums,remainSize ;
	u32	page, offset;
	u8	*bufferPtr = (u8*)buffer;

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
		ret = _PageWrite_8192E(padapter, page, bufferPtr+offset, MAX_DLFW_PAGE_SIZE);
		
		if(ret == _FAIL)
			goto exit;
	}
	if (remainSize) {
		offset = pageNums * MAX_DLFW_PAGE_SIZE;
		page = pageNums;
		ret = _PageWrite_8192E(padapter, page, bufferPtr+offset, remainSize);
		
		if(ret == _FAIL)
			goto exit;

	}
	RT_TRACE(_module_hal_init_c_, _drv_info_, ("_WriteFW Done- for Normal chip.\n"));

exit:
	return ret;
}


void _8051Reset8192E(PADAPTER padapter)
{
	u8 u1bTmp, u1bTmp2;

	// Reset MCU IO Wrapper,suggested by SD1-Gimmy
	u1bTmp2 = rtw_read8(padapter, REG_RSV_CTRL+1);
	rtw_write8(padapter,REG_RSV_CTRL+1, (u1bTmp2&(~BIT0)));	

	//Reset 8051
	u1bTmp = rtw_read8(padapter, REG_SYS_FUNC_EN+1);
	rtw_write8(padapter, REG_SYS_FUNC_EN+1, u1bTmp&(~BIT2));

	// Enable MCU IO Wrapper
	u1bTmp2 = rtw_read8(padapter, REG_RSV_CTRL+1);
	rtw_write8(padapter, REG_RSV_CTRL+1, u1bTmp2|BIT0);	

	// Enable 8051
	rtw_write8(padapter, REG_SYS_FUNC_EN+1, u1bTmp|(BIT2));

	DBG_871X("=====> _8051Reset8192E(): 8051 reset success .\n");
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
static s32 _FWFreeToGo8192E(_adapter *adapter, u32 min_cnt, u32 timeout_ms)
{
	s32 ret = _FAIL;
	u32	value32;
	u32 start = rtw_get_current_time();
	u32 cnt = 0;

	value32 = rtw_read32(adapter, REG_MCUFWDL);
	value32 |= MCUFWDL_RDY;
	value32 &= ~WINTINI_RDY;
	rtw_write32(adapter, REG_MCUFWDL, value32);

	_8051Reset8192E(adapter);

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

s32
FirmwareDownload8192E(
	IN	PADAPTER			Adapter,
	IN	BOOLEAN			bUsedWoWLANFw
)
{
	s32	rtStatus = _SUCCESS;
	u8	write_fw = 0;
	u32 fwdl_start_time;
	PHAL_DATA_TYPE	pHalData = GET_HAL_DATA(Adapter);	
	
	u8				*pFwImageFileName;
	u8				*pucMappedFile = NULL;
	PRT_FIRMWARE_8192E	pFirmware = NULL;
	u8				*pFwHdr = NULL;
	u8				*pFirmwareBuf;
	u32				FirmwareLen;
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(Adapter);

	RT_TRACE(_module_hal_init_c_, _drv_info_, ("+%s\n", __FUNCTION__));
	pFirmware = (PRT_FIRMWARE_8192E)rtw_zmalloc(sizeof(RT_FIRMWARE_8192E));
	if(!pFirmware)
	{
		rtStatus = _FAIL;
		goto exit;
	}

#ifdef CONFIG_EMBEDDED_FWIMG
	pFirmware->eFWSource = FW_SOURCE_HEADER_FILE;
#else
	pFirmware->eFWSource = FW_SOURCE_IMG_FILE; // We should decided by Reg.
#endif

	DBG_871X(" ===> FirmwareDownload88E() fw source from %s.\n", (pFirmware->eFWSource ? "Header": "File"));

	switch(pFirmware->eFWSource)
	{
		case FW_SOURCE_IMG_FILE:
			//TODO:
			break;
		case FW_SOURCE_HEADER_FILE:
#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
			if (bUsedWoWLANFw) {
				if (!pwrpriv->wowlan_ap_mode) {
				ODM_ConfigFWWithHeaderFile(&pHalData->odmpriv, CONFIG_FW_WoWLAN, (u8 *)&(pFirmware->szFwBuffer), &(pFirmware->ulFwLength));
				DBG_871X("%s fw:%s, size: %d\n", __FUNCTION__, "WoWLAN", pFirmware->ulFwLength);
				} else {
					ODM_ConfigFWWithHeaderFile(&pHalData->odmpriv, CONFIG_FW_AP_WoWLAN, (u8*)&pFirmware->szFwBuffer, &pFirmware->ulFwLength);
					DBG_8192C(" ===> %s fw: %s, size: %d\n", __FUNCTION__, "AP_WoWLAN", pFirmware->ulFwLength);
				}
			} else
			#endif /* CONFIG_WOWLAN */
			{
				ODM_ConfigFWWithHeaderFile(&pHalData->odmpriv, CONFIG_FW_NIC, (u8 *)&(pFirmware->szFwBuffer), &(pFirmware->ulFwLength));
				DBG_871X("%s fw:%s, size: %d\n", __FUNCTION__, "NIC", pFirmware->ulFwLength);
			}
			break;
	}

	if (pFirmware->ulFwLength > FW_SIZE_8192E) {
		rtStatus = _FAIL;
		DBG_871X_LEVEL(_drv_emerg_, "Firmware size:%u exceed %u\n", pFirmware->ulFwLength, FW_SIZE_8192E);
		goto exit;
	}

	pFirmwareBuf = pFirmware->szFwBuffer;
	FirmwareLen = pFirmware->ulFwLength;
	pFwHdr = (u8 *)pFirmware->szFwBuffer;

	if (IS_FW_HEADER_EXIST_8192E(pFwHdr))
	{
		// Shift 32 bytes for FW header
		pFirmwareBuf = pFirmwareBuf + 32;
		FirmwareLen = FirmwareLen - 32;

		pHalData->FirmwareVersion =  (u16)GET_FIRMWARE_HDR_VERSION_8192E(pFwHdr);
		pHalData->FirmwareSubVersion = (u16)GET_FIRMWARE_HDR_SUB_VER_8192E(pFwHdr);
		pHalData->FirmwareSignature = (u16)GET_FIRMWARE_HDR_SIGNATURE_8192E(pFwHdr);

		DBG_871X ("%s: fw_ver=%d fw_subver=%d sig=0x%x\n",
			  __FUNCTION__, pHalData->FirmwareVersion, pHalData->FirmwareSubVersion, pHalData->FirmwareSignature);
	}
	else{
		DBG_871X ("%s:FW header check failed .....\n", __FUNCTION__);
	}

	// Suggested by Filen. If 8051 is running in RAM code, driver should inform Fw to reset by itself,
	// or it will cause download Fw fail. 2010.02.01. by tynli.
	if (rtw_read8(Adapter, REG_MCUFWDL) & BIT7) //8051 RAM code
	{
		rtw_write8(Adapter, REG_MCUFWDL, 0x00);
		_8051Reset8192E(Adapter);		
	}

	_FWDownloadEnable_8192E(Adapter, _TRUE);
	fwdl_start_time = rtw_get_current_time();
	while(!Adapter->bDriverStopped && !Adapter->bSurpriseRemoved
			&& (write_fw++ < 3 || rtw_get_passing_time_ms(fwdl_start_time) < 500))
	{
		/* reset FWDL chksum */
		rtw_write8(Adapter, REG_MCUFWDL, rtw_read8(Adapter, REG_MCUFWDL)|FWDL_ChkSum_rpt);
		
		rtStatus = _WriteFW_8192E(Adapter, pFirmwareBuf, FirmwareLen);
		if (rtStatus != _SUCCESS)
			continue;

		rtStatus = polling_fwdl_chksum(Adapter, 5, 50);
		if (rtStatus == _SUCCESS)
			break;
	}
	_FWDownloadEnable_8192E(Adapter, _FALSE);
	if(_SUCCESS != rtStatus)
		goto fwdl_stat;

	rtStatus = _FWFreeToGo8192E(Adapter, 10, 200);
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
		rtw_mfree((u8*)pFirmware, sizeof(RT_FIRMWARE_8192E));

	//RT_TRACE(COMP_INIT, DBG_LOUD, (" <=== FirmwareDownload91C()\n"));
#ifdef CONFIG_WOWLAN
	if (adapter_to_pwrctl(Adapter)->wowlan_mode)
		InitializeFirmwareVars8192E(Adapter);	
	else
		DBG_871X_LEVEL(_drv_always_, "%s: wowland_mode:%d wowlan_wake_reason:%d\n", 
			__func__, adapter_to_pwrctl(Adapter)->wowlan_mode, 
			adapter_to_pwrctl(Adapter)->wowlan_wake_reason);
#endif

	return rtStatus;
}


void InitializeFirmwareVars8192E(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);

	// Init Fw LPS related.
	pwrpriv->bFwCurrentInPSMode = _FALSE;
	// Init H2C counter. by tynli. 2009.12.09.
	pHalData->LastHMEBoxNum = 0;
}

#if defined(CONFIG_WOWLAN) || defined(CONFIG_AP_WOWLAN)
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
SetFwRelatedForWoWLAN8192E(
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
	status = FirmwareDownload8192E(padapter, bHostIsGoingtoSleep);
	if(status != _SUCCESS) {
		DBG_871X("SetFwRelatedForWoWLAN8192E(): Re-Download Firmware failed!!\n");
		return;
	} else {
		DBG_871X("SetFwRelatedForWoWLAN8192E(): Re-Download Firmware Success !!\n");
	}
	//
	// 2. Re-Init the variables about Fw related setting.
	//
	InitializeFirmwareVars8192E(padapter);
}
#endif //CONFIG_WOWLAN

static void rtl8192e_free_hal_data(PADAPTER padapter)
{
_func_enter_;
	if (padapter->HalData) {
		phy_free_filebuf(padapter);
		rtw_vmfree(padapter->HalData, sizeof(HAL_DATA_TYPE));
		padapter->HalData = NULL;
	}
_func_exit_;
}

//===========================================================
//				Efuse related code
//===========================================================
VOID
Hal_EfuseParseBTCoexistInfo8192E(
	IN PADAPTER			Adapter,
	IN pu1Byte			hwinfo,
	IN BOOLEAN			AutoLoadFail
	)
{
#ifdef CONFIG_BT_COEXIST
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	pHalData->EEPROMBluetoothCoexist = 0;
	pHalData->EEPROMBluetoothType = BT_CSR_BC8;
	pHalData->EEPROMBluetoothAntNum = Ant_x2;
	pHalData->EEPROMBluetoothAntIsolation = 1;
	pHalData->EEPROMBluetoothRadioShared = BT_Radio_Shared;
	BT_InitHalVars(Adapter);
#endif
}

void
Hal_EfuseParseIDCode8192E(
	IN	PADAPTER	padapter,
	IN	u8			*hwinfo
	)
{
	EEPROM_EFUSE_PRIV *pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);
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

VOID
Hal_ReadPROMVersion8192E(
	IN	PADAPTER	Adapter,	
	IN	u8* 			PROMContent,
	IN	BOOLEAN 	AutoloadFail
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	if(AutoloadFail){
		pHalData->EEPROMVersion = EEPROM_Default_Version;		
	}
	else{
		pHalData->EEPROMVersion = *(u8 *)&PROMContent[EEPROM_VERSION_8192E];
		if(pHalData->EEPROMVersion == 0xFF)
			pHalData->EEPROMVersion = EEPROM_Default_Version;					
	}
	//DBG_871X("pHalData->EEPROMVersion is 0x%x\n", pHalData->EEPROMVersion);
}

BOOLEAN 
Hal_GetChnlGroup8192E(
	IN	u8	Channel,
	OUT u8*	pGroup
	)
{
	BOOLEAN bIn24G=_TRUE;

	if(Channel <= 14)
	{
		bIn24G=_TRUE;

		if      (1  <= Channel && Channel <= 2 )   *pGroup = 0;
		else if (3  <= Channel && Channel <= 5 )   *pGroup = 1;
		else if (6  <= Channel && Channel <= 8 )   *pGroup = 2;
		else if (9  <= Channel && Channel <= 11)   *pGroup = 3;
		else if (12 <= Channel && Channel <= 14)   *pGroup = 4;
		else
		{
			DBG_871X("==>%s in 2.4 G, but Channel %d in Group not found \n",__FUNCTION__, Channel);
		}
	}
	else
	{
		bIn24G=_FALSE;

		if      (36   <= Channel && Channel <=  42)   *pGroup = 0;
		else if (44   <= Channel && Channel <=  48)   *pGroup = 1;
		else if (50   <= Channel && Channel <=  58)   *pGroup = 2;
		else if (60   <= Channel && Channel <=  64)   *pGroup = 3;
		else if (100  <= Channel && Channel <= 106)   *pGroup = 4;
		else if (108  <= Channel && Channel <= 114)   *pGroup = 5;
		else if (116  <= Channel && Channel <= 122)   *pGroup = 6;
		else if (124  <= Channel && Channel <= 130)   *pGroup = 7;
		else if (132  <= Channel && Channel <= 138)   *pGroup = 8;
		else if (140  <= Channel && Channel <= 144)   *pGroup = 9;
		else if (149  <= Channel && Channel <= 155)   *pGroup = 10;
		else if (157  <= Channel && Channel <= 161)   *pGroup = 11;
		else if (165  <= Channel && Channel <= 171)   *pGroup = 12;
		else if (173  <= Channel && Channel <= 177)   *pGroup = 13;
		else
		{
			DBG_871X("==>%s in 5G, but Channel %d in Group not found \n",__FUNCTION__,Channel);
		}

	}
	//DBG_871X("<==mpt_GetChnlGroup8812A,  (%s) Channel = %d, Group =%d,\n", (bIn24G) ? "2.4G" : "5G", Channel, *pGroup);

	return bIn24G;
}

static void
hal_ReadPowerValueFromPROM8192E(
	IN	PADAPTER 		Adapter,
	IN	PTxPowerInfo24G	pwrInfo24G,
	IN	u8*				PROMContent,
	IN	BOOLEAN			AutoLoadFail
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u32 rfPath, eeAddr=EEPROM_TX_PWR_INX_8192E, group,TxCount=0;
	
	_rtw_memset(pwrInfo24G, 0, sizeof(TxPowerInfo24G));
//	_rtw_memset(pwrInfo5G, 0, sizeof(TxPowerInfo5G));

	//DBG_871X("hal_ReadPowerValueFromPROM8812A(): PROMContent[0x%x]=0x%x\n", (eeAddr+1), PROMContent[eeAddr+1]);
	if(0xFF == PROMContent[eeAddr+1])  //YJ,add,120316
		AutoLoadFail = _TRUE;

	if(AutoLoadFail)
	{
		DBG_871X("%s: Use Default value!\n",__FUNCTION__);
		//for(rfPath = 0 ; rfPath < pHalData->NumTotalRFPath ; rfPath++)
		for(rfPath = 0 ; rfPath < GET_HAL_RFPATH_NUM(Adapter); rfPath++)

		{
			// 2.4G default value
			for(group = 0 ; group < MAX_CHNL_GROUP_24G; group++)
			{
				pwrInfo24G->IndexCCK_Base[rfPath][group] =	EEPROM_DEFAULT_24G_INDEX;
				pwrInfo24G->IndexBW40_Base[rfPath][group] =EEPROM_DEFAULT_24G_INDEX;
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

	pHalData->bTXPowerDataReadFromEEPORM = _TRUE;		//YJ,move,120316

	for(rfPath = 0 ; rfPath < pHalData->NumTotalRFPath ; rfPath++)
	{
		if(rfPath == RF_PATH_B)
			eeAddr = 0x3A;
		
		// 2.4G default value
		for(group = 0 ; group < MAX_CHNL_GROUP_24G; group++)
		{
			pwrInfo24G->IndexCCK_Base[rfPath][group] =	PROMContent[eeAddr++];
			if(pwrInfo24G->IndexCCK_Base[rfPath][group] == 0xFF)
			{
				pwrInfo24G->IndexCCK_Base[rfPath][group] = EEPROM_DEFAULT_24G_INDEX;
				//pHalData->bNOPG = _TRUE;
			}
			//DBG_871X("8812-2G RF-%d-G-%d CCK-Addr-%x BASE=%x\n", 
			//rfPath, group, eeAddr-1, pwrInfo24G->IndexCCK_Base[rfPath][group]);
		}
		for(group = 0 ; group < MAX_CHNL_GROUP_24G-1; group++)
		{
			pwrInfo24G->IndexBW40_Base[rfPath][group] =	PROMContent[eeAddr++];
			if(pwrInfo24G->IndexBW40_Base[rfPath][group] == 0xFF)
				pwrInfo24G->IndexBW40_Base[rfPath][group] =	EEPROM_DEFAULT_24G_INDEX;
			//DBG_871X("8812-2G RF-%d-G-%d BW40-Addr-%x BASE=%x\n", 
			//rfPath, group, eeAddr-1, pwrInfo24G->IndexBW40_Base[rfPath][group]);
		}
		for(TxCount=0;TxCount<MAX_TX_COUNT;TxCount++)
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
				//DBG_871X("8812-2G RF-%d-SS-%d BW20-Addr-%x DIFF=%d\n", 
				//rfPath, TxCount, eeAddr, pwrInfo24G->BW20_Diff[rfPath][TxCount]);

				if(PROMContent[eeAddr] == 0xFF)
					pwrInfo24G->OFDM_Diff[rfPath][TxCount] =	EEPROM_DEFAULT_24G_OFDM_DIFF;				
				else 
				{
					pwrInfo24G->OFDM_Diff[rfPath][TxCount] =	(PROMContent[eeAddr]&0x0f);					
					if(pwrInfo24G->OFDM_Diff[rfPath][TxCount] & BIT3)		//4bit sign number to 8 bit sign number
						pwrInfo24G->OFDM_Diff[rfPath][TxCount] |= 0xF0;
				}
				//DBG_871X("8812-2G RF-%d-SS-%d LGOD-Addr-%x DIFF=%d\n", 
				//rfPath, TxCount, eeAddr, pwrInfo24G->OFDM_Diff[rfPath][TxCount]);

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
				//DBG_871X("8812-2G RF-%d-SS-%d BW40-Addr-%x DIFF=%d\n", 
				//rfPath, TxCount, eeAddr, pwrInfo24G->BW40_Diff[rfPath][TxCount]);

				if(PROMContent[eeAddr] == 0xFF)
					pwrInfo24G->BW20_Diff[rfPath][TxCount] =	EEPROM_DEFAULT_DIFF;				
				else 
				{
					pwrInfo24G->BW20_Diff[rfPath][TxCount] =	(PROMContent[eeAddr]&0x0f);				
					if(pwrInfo24G->BW20_Diff[rfPath][TxCount] & BIT3)		//4bit sign number to 8 bit sign number
						pwrInfo24G->BW20_Diff[rfPath][TxCount] |= 0xF0;
				}
				//DBG_871X("8812-2G RF-%d-SS-%d BW20-Addr-%x DIFF=%d\n", 
				//rfPath, TxCount, eeAddr, pwrInfo24G->BW20_Diff[rfPath][TxCount]);

				eeAddr++;
				
				if(PROMContent[eeAddr] == 0xFF)
					pwrInfo24G->OFDM_Diff[rfPath][TxCount] =	EEPROM_DEFAULT_DIFF;								
				else 
				{
					pwrInfo24G->OFDM_Diff[rfPath][TxCount] =	(PROMContent[eeAddr]&0xf0)>>4;				
					if(pwrInfo24G->OFDM_Diff[rfPath][TxCount] & BIT3)		//4bit sign number to 8 bit sign number
						pwrInfo24G->OFDM_Diff[rfPath][TxCount] |= 0xF0;
				}
				//DBG_871X("8812-2G RF-%d-SS-%d LGOD-Addr-%x DIFF=%d\n", 
				//rfPath, TxCount, eeAddr, pwrInfo24G->BW20_Diff[rfPath][TxCount]);

				if(PROMContent[eeAddr] == 0xFF)
					pwrInfo24G->CCK_Diff[rfPath][TxCount] =	EEPROM_DEFAULT_DIFF;												
				else 
				{
					pwrInfo24G->CCK_Diff[rfPath][TxCount] =	(PROMContent[eeAddr]&0x0f);				
					if(pwrInfo24G->CCK_Diff[rfPath][TxCount] & BIT3)		//4bit sign number to 8 bit sign number
						pwrInfo24G->CCK_Diff[rfPath][TxCount] |= 0xF0;
				}
				//DBG_871X("8812-2G RF-%d-SS-%d CCK-Addr-%x DIFF=%d\n", 
				//rfPath, TxCount, eeAddr, pwrInfo24G->CCK_Diff[rfPath][TxCount]);

				eeAddr++;
			}
		}

	}

}

void Hal_ReadPowerSavingMode8192E(
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
			pwrctl->bHWPowerdown = (hwinfo[EEPROM_RF_FEATURE_OPTION_8192E] & BIT4);
		else
			pwrctl->bHWPowerdown = padapter->registrypriv.hwpdn_mode;
				
		// decide hw if support remote wakeup function
		// if hw supported, 8051 (SIE) will generate WeakUP signal( D+/D- toggle) when autoresume
#ifdef CONFIG_USB_HCI
		pwrctl->bSupportRemoteWakeup = (hwinfo[EEPROM_USB_OPTIONAL_FUNCTION0] & BIT1)?_TRUE :_FALSE;
#endif //CONFIG_USB_HCI

		//if(SUPPORT_HW_RADIO_DETECT(Adapter))	
			//Adapter->registrypriv.usbss_enable = pwrctl->bSupportRemoteWakeup ;
		
		DBG_871X("%s...bHWPwrPindetect(%x)-bHWPowerdown(%x) ,bSupportRemoteWakeup(%x)\n",__FUNCTION__,
			pwrctl->bHWPwrPindetect, pwrctl->bHWPowerdown, pwrctl->bSupportRemoteWakeup);

		DBG_871X("### PS params=>  power_mgnt(%x),usbss_enable(%x) ###\n",padapter->registrypriv.power_mgnt,padapter->registrypriv.usbss_enable);
	
	}

}

void
Hal_ReadTxPowerInfo8192E(
	IN	PADAPTER 		Adapter,
	IN	u8*				PROMContent,
	IN	BOOLEAN			AutoLoadFail
	)
{	
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	TxPowerInfo24G	pwrInfo24G;	
	u8	rfPath, ch, group, TxCount;
	

	hal_ReadPowerValueFromPROM8192E(Adapter, &pwrInfo24G,PROMContent, AutoLoadFail);

	//if(!AutoLoadFail)
	//	pHalData->bTXPowerDataReadFromEEPORM = _TRUE;		

	for(rfPath = 0 ; rfPath < pHalData->NumTotalRFPath ; rfPath++)
	{
		for(ch = 0 ; ch < CHANNEL_MAX_NUMBER_2G ; ch++)
		{
			Hal_GetChnlGroup8192E(ch+1, &group);

			if(ch == 14-1) 
			{
				pHalData->Index24G_CCK_Base[rfPath][ch] = pwrInfo24G.IndexCCK_Base[rfPath][5];
				pHalData->Index24G_BW40_Base[rfPath][ch] = pwrInfo24G.IndexBW40_Base[rfPath][group];
			}
			else
			{
				pHalData->Index24G_CCK_Base[rfPath][ch] = pwrInfo24G.IndexCCK_Base[rfPath][group];
				pHalData->Index24G_BW40_Base[rfPath][ch] = pwrInfo24G.IndexBW40_Base[rfPath][group];
			}
#if defined(DBG_TX_POWER_IDX)			
			DBG_871X("======= Path %d, ChannelIndex %d, Group %d=======\n",rfPath,ch, group);	
			DBG_871X("Index24G_CCK_Base[%d][%d] = 0x%x\n",rfPath,ch ,pHalData->Index24G_CCK_Base[rfPath][ch]);
			DBG_871X("Index24G_BW40_Base[%d][%d] = 0x%x\n",rfPath,ch,pHalData->Index24G_BW40_Base[rfPath][ch]);
#endif
		}	

		for(TxCount=0;TxCount<MAX_TX_COUNT;TxCount++)
		{
			pHalData->CCK_24G_Diff[rfPath][TxCount]=pwrInfo24G.CCK_Diff[rfPath][TxCount];
			pHalData->OFDM_24G_Diff[rfPath][TxCount]=pwrInfo24G.OFDM_Diff[rfPath][TxCount];
			pHalData->BW20_24G_Diff[rfPath][TxCount]=pwrInfo24G.BW20_Diff[rfPath][TxCount];
			pHalData->BW40_24G_Diff[rfPath][TxCount]=pwrInfo24G.BW40_Diff[rfPath][TxCount];		

#if defined(DBG_TX_POWER_IDX)	
			DBG_871X("--------------------------------------- 2.4G ---------------------------------------\n");
			DBG_871X("CCK_24G_Diff[%d][%d]= %d\n",rfPath,TxCount,pHalData->CCK_24G_Diff[rfPath][TxCount]);
			DBG_871X("OFDM_24G_Diff[%d][%d]= %d\n",rfPath,TxCount,pHalData->OFDM_24G_Diff[rfPath][TxCount]);
			DBG_871X("BW20_24G_Diff[%d][%d]= %d\n",rfPath,TxCount,pHalData->BW20_24G_Diff[rfPath][TxCount]);
			DBG_871X("BW40_24G_Diff[%d][%d]= %d\n",rfPath,TxCount,pHalData->BW40_24G_Diff[rfPath][TxCount]);

#endif							
		}
	}

	
	// 2010/10/19 MH Add Regulator recognize for CU.
	if(!AutoLoadFail)
	{
		struct registry_priv  *registry_par = &Adapter->registrypriv;
		
		if(PROMContent[EEPROM_RF_BOARD_OPTION_8192E] == 0xFF)
			pHalData->EEPROMRegulatory = 2; //(EEPROM_DEFAULT_BOARD_OPTION&0x7);	//bit0~2
		else
			pHalData->EEPROMRegulatory = (PROMContent[EEPROM_RF_BOARD_OPTION_8192E]&0x7);	//bit0~2
		

		// 2012/09/26 MH Add for TX power calibrate rate.
		pHalData->TxPwrCalibrateRate = PROMContent[EEPROM_TX_PWR_CALIBRATE_RATE_8192E];
	}
	else
	{
		pHalData->EEPROMRegulatory = 0;
		// 2012/09/26 MH Add for TX power calibrate rate.
		pHalData->TxPwrCalibrateRate = EEPROM_DEFAULT_TX_CALIBRATE_RATE;
	}
	DBG_871X("EEPROMRegulatory = 0x%x TxPwrCalibrateRate=0x%x\n", pHalData->EEPROMRegulatory, pHalData->TxPwrCalibrateRate);

}

VOID
Hal_ReadBoardType8192E(
	IN	PADAPTER	Adapter,	
	IN	u8*			PROMContent,
	IN	BOOLEAN		AutoloadFail
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8 board_type;
	/*
	Bit[7:5]: Board Type (PCIe)
		0h: WiFi solo-mCard
		1h: WiFi+BT combo-mCard
	*/
	if(!AutoloadFail)
	{
		board_type = (PROMContent[EEPROM_RF_BOARD_OPTION_8192E]&0xE0)>>5;
		
		if(board_type == 0xFF){
			pHalData->InterfaceSel = INTF_SEL0_USB ;
		}
		else{
			pHalData->InterfaceSel = (board_type==1)?INTF_SEL4_USB_Combo :INTF_SEL0_USB;
		}
		
	}
	else
	{
		pHalData->InterfaceSel = 0;
	}
	//DBG_871X("Board Type: 0x%2x\n", pHalData->InterfaceSel);
	if(pHalData->InterfaceSel == INTF_SEL4_USB_Combo)
		DBG_871X("Board Type: Combo Card \n");
	else
		DBG_871X("Board Type: Dongle or WIFI only Module \n");

}

VOID
Hal_ReadThermalMeter_8192E(
	IN	PADAPTER	Adapter,	
	IN	u8*		 	PROMContent,
	IN	BOOLEAN 	AutoloadFail
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	//u8	tempval;

	//
	// ThermalMeter from EEPROM
	//
	if(!AutoloadFail)	
		pHalData->EEPROMThermalMeter = PROMContent[EEPROM_THERMAL_METER_8192E];
	else
		pHalData->EEPROMThermalMeter = EEPROM_Default_ThermalMeter_8192E;
	//pHalData->EEPROMThermalMeter = (tempval&0x1f);	//[4:0]

	if(pHalData->EEPROMThermalMeter == 0xff || AutoloadFail)
	{
		pHalData->bAPKThermalMeterIgnore = _TRUE;
		pHalData->EEPROMThermalMeter = EEPROM_Default_ThermalMeter_8192E;		
	}

	//pHalData->ThermalMeter[0] = pHalData->EEPROMThermalMeter;	
	DBG_871X("ThermalMeter = 0x%x\n", pHalData->EEPROMThermalMeter);
}

VOID
Hal_ReadChannelPlan8192E(
	IN	PADAPTER		padapter,
	IN	u8*				hwinfo,
	IN	BOOLEAN			AutoLoadFail
	)
{
	padapter->mlmepriv.ChannelPlan = hal_com_config_channel_plan(
		padapter
		, hwinfo?hwinfo[EEPROM_ChannelPlan_8192E]:0xFF
		, padapter->registrypriv.channel_plan
		, RT_CHANNEL_DOMAIN_REALTEK_DEFINE
		, AutoLoadFail
	);

	Hal_ChannelPlanToRegulation(padapter, padapter->mlmepriv.ChannelPlan);

	DBG_871X("mlmepriv.ChannelPlan = 0x%02x\n", padapter->mlmepriv.ChannelPlan);
}

VOID
Hal_EfuseParseXtal_8192E(
	IN	PADAPTER	pAdapter,
	IN	u8*			hwinfo,
	IN	BOOLEAN		AutoLoadFail
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);

	if(!AutoLoadFail)
	{
		pHalData->CrystalCap = hwinfo[EEPROM_XTAL_8192E];
		if(pHalData->CrystalCap == 0xFF)
			pHalData->CrystalCap = EEPROM_Default_CrystalCap_8192E;	 //what value should 8812 set?
	}
	else
	{
		pHalData->CrystalCap = EEPROM_Default_CrystalCap_8192E;
	}
	DBG_871X("CrystalCap: 0x%2x\n", pHalData->CrystalCap);
}

VOID
Hal_ReadAntennaDiversity8192E(
	IN	PADAPTER		pAdapter,
	IN	u8*				PROMContent,
	IN	BOOLEAN			AutoLoadFail
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	
	pHalData->TRxAntDivType = NO_ANTDIV; 
	pHalData->AntDivCfg = 0;
	
	DBG_871X("SWAS: bHwAntDiv = %x, TRxAntDivType = %x\n", pHalData->AntDivCfg, pHalData->TRxAntDivType);
}

#define GetRegAmplifierType2G(_Adapter)	(_Adapter->registrypriv.AmplifierType_2G)
#define GetRegAmplifierType5G(_Adapter)	(_Adapter->registrypriv.AmplifierType_5G)

VOID
Hal_ReadPAType_8192E(
	IN	PADAPTER	Adapter,
	IN	u8*			PROMContent,
	IN	BOOLEAN		AutoloadFail
	)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);

	if( ! AutoloadFail )
	{
		// PA Type
		pHalData->PAType_2G = EF1Byte( *(u8*)&PROMContent[EEPROM_PA_TYPE_8192EU] );
		pHalData->PAType_5G = EF1Byte( *(u8*)&PROMContent[EEPROM_PA_TYPE_8192EU] );		
		pHalData->LNAType_2G = EF1Byte( *(u8*)&PROMContent[EEPROM_LNA_TYPE_2G_8192EU] );		
		pHalData->LNAType_5G = EF1Byte( *(u8*)&PROMContent[EEPROM_LNA_TYPE_5G_8192EU] );				
		
		if (GetRegAmplifierType2G(Adapter) == 0) // AUTO
		{
			pHalData->ExternalPA_2G = ((pHalData->PAType_2G & BIT5) && (pHalData->PAType_2G & BIT4)) ? 1 : 0;		
			pHalData->ExternalLNA_2G = ((pHalData->LNAType_2G & BIT7) && (pHalData->LNAType_2G & BIT3)) ? 1 : 0;	// 5G only now.
		}
		else
		{
			pHalData->ExternalPA_2G  =0; //(GetRegAmplifierType2G(Adapter)&ODM_BOARD_EXT_PA)  ? 1 : 0; 
			pHalData->ExternalLNA_2G = 0;//(GetRegAmplifierType2G(Adapter)&ODM_BOARD_EXT_LNA) ? 1 : 0;  
		}
/*
		if (GetRegAmplifierType5G(Adapter) == 0) // AUTO
		{
			pHalData->ExternalPA_5G = ((pHalData->PAType_5G & BIT1) && (pHalData->PAType_5G & BIT0)) ? 1 : 0;					
			pHalData->ExternalLNA_5G = ((pHalData->LNAType_5G & BIT7) && (pHalData->LNAType_5G & BIT3)) ? 1 : 0;	// 5G only now.			
		}
		else
		{
			pHalData->ExternalPA_5G  = (GetRegAmplifierType5G(Adapter)&ODM_BOARD_EXT_PA_5G)  ? 1 : 0; 			
			pHalData->ExternalLNA_5G = (GetRegAmplifierType5G(Adapter)&ODM_BOARD_EXT_LNA_5G) ? 1 : 0;  
		}
*/		
	}
	else
	{
		pHalData->ExternalPA_2G  = EEPROM_Default_PAType; 
		pHalData->ExternalPA_5G  = 0xFF; 
		pHalData->ExternalLNA_2G = EEPROM_Default_LNAType;  
		pHalData->ExternalLNA_5G = 0xFF; 
		
		if (GetRegAmplifierType2G(Adapter) == 0) // AUTO
		{		
			pHalData->ExternalPA_2G  = 0; 
			pHalData->ExternalLNA_2G = 0;  
		}
		else
		{
			pHalData->ExternalPA_2G  = (GetRegAmplifierType2G(Adapter)&ODM_BOARD_EXT_PA)  ? 1 : 0; 
			pHalData->ExternalLNA_2G = (GetRegAmplifierType2G(Adapter)&ODM_BOARD_EXT_LNA) ? 1 : 0;  
		}
/*
		if (GetRegAmplifierType5G(Adapter) == 0) // AUTO
		{		
			pHalData->ExternalPA_5G  = 0; 
			pHalData->ExternalLNA_5G = 0;  
		}
		else
		{
			pHalData->ExternalPA_5G  = (GetRegAmplifierType5G(Adapter)&ODM_BOARD_EXT_PA_5G)  ? 1 : 0; 			
			pHalData->ExternalLNA_5G = (GetRegAmplifierType5G(Adapter)&ODM_BOARD_EXT_LNA_5G) ? 1 : 0;  
		}
*/		
	}
	DBG_871X("pHalData->PAType_2G is 0x%x, pHalData->ExternalPA_2G = %d\n", pHalData->PAType_2G, pHalData->ExternalPA_2G);
	DBG_871X("pHalData->LNAType_2G is 0x%x, pHalData->ExternalLNA_2G = %d\n", pHalData->LNAType_2G, pHalData->ExternalLNA_2G);

}

enum{
		VOLTAGE_V25						= 0x03,
		LDOE25_SHIFT						= 28 ,
	};

static VOID
Hal_EfusePowerSwitch8192E(
	IN	PADAPTER	pAdapter,
	IN	u8		bWrite,
	IN	u8		PwrState)
{
	u8	tempval;
	u16	tmpV16;
	#define REG_EFUSE_ACCESS_JAGUAR 0xCF
	#define EFUSE_ACCESS_ON_JAGUAR 0x69
	#define EFUSE_ACCESS_OFF_JAGUAR 0x00
	if (PwrState == _TRUE)
	{
		rtw_write8(pAdapter, REG_EFUSE_ACCESS_JAGUAR, EFUSE_ACCESS_ON_JAGUAR);

		// 1.2V Power: From VDDON with Power Cut(0x0000h[15]), defualt valid
		tmpV16 = rtw_read16(pAdapter,REG_SYS_ISO_CTRL);
		if( ! (tmpV16 & PWC_EV12V ) ){
			tmpV16 |= PWC_EV12V ;
			//rtw_write16(pAdapter,REG_SYS_ISO_CTRL,tmpV16);
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

		if(bWrite == TRUE)
		{
			// Enable LDO 2.5V before read/write action
			tempval = PlatformEFIORead1Byte(pAdapter, EFUSE_TEST+3);
			tempval &= 0x0F;
			tempval |= (VOLTAGE_V25 << 4);
			PlatformEFIOWrite1Byte(pAdapter, EFUSE_TEST+3, (tempval | 0x80));
		}
	}
	else
	{
		rtw_write8(pAdapter, REG_EFUSE_ACCESS_JAGUAR, EFUSE_ACCESS_OFF_JAGUAR);

		if(bWrite == _TRUE){
			// Disable LDO 2.5V after read/write action
			tempval = rtw_read8(pAdapter, EFUSE_TEST+3);
			rtw_write8(pAdapter, EFUSE_TEST+3, (tempval & 0x7F));
		}
	}
}

static VOID
rtl8192E_EfusePowerSwitch(
	IN	PADAPTER	pAdapter,
	IN	u8		bWrite,
	IN	u8		PwrState)
{
	Hal_EfusePowerSwitch8192E(pAdapter, bWrite, PwrState);	
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
Hal_EfuseReadEFuse8192E(
	PADAPTER		Adapter,
	u16			_offset,
	u16 			_size_byte,
	u8      		*pbuf,
	IN	BOOLEAN	bPseudoTest
	)
{
	u8	*efuseTbl = NULL;
	u8	rtemp8[1];
	u16	eFuse_Addr = 0;
	u8	offset, wren;
	u16	i, j;
	u16	**eFuseWord = NULL;
	u16	efuse_utilized = 0;
	u8	efuse_usage = 0;
	u8	u1temp = 0;

	//
	// Do NOT excess total size of EFuse table. Added by Roger, 2008.11.10.
	//
	if((_offset + _size_byte)>EFUSE_MAP_LEN_8192E)
	{// total E-Fuse table is 512bytes
		DBG_8192C("Hal_EfuseReadEFuse8812A(): Invalid offset(%#x) with read bytes(%#x)!!\n",_offset, _size_byte);
		goto exit;
	}

	efuseTbl = (u8*)rtw_zmalloc(EFUSE_MAP_LEN_8192E);
	if(efuseTbl == NULL)
	{
		DBG_871X("%s: alloc efuseTbl fail!\n", __FUNCTION__);
		goto exit;
	}

	eFuseWord= (u16 **)rtw_malloc2d(EFUSE_MAX_SECTION_8192E, EFUSE_MAX_WORD_UNIT, sizeof(u16));
	if(eFuseWord == NULL)
	{
		DBG_871X("%s: alloc eFuseWord fail!\n", __FUNCTION__);
		goto exit;
	}

	// 0. Refresh efuse init map as all oxFF.
	for (i = 0; i < EFUSE_MAX_SECTION_8192E; i++)
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
	while((*rtemp8 != 0xFF) && (eFuse_Addr < EFUSE_REAL_CONTENT_LEN_8192E))
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
				
				if(*rtemp8 != 0xFF && (eFuse_Addr < EFUSE_REAL_CONTENT_LEN_8192E))
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
		
		if(offset < EFUSE_MAX_SECTION_8192E)
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
					

					if(eFuse_Addr >= EFUSE_REAL_CONTENT_LEN_8192E) 
						break;

					//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Addr=%d", eFuse_Addr));
					ReadEFuseByte(Adapter, eFuse_Addr, rtemp8, bPseudoTest);	
					eFuse_Addr++;
					//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Data=0x%x\n", *rtemp8)); 				
					
					efuse_utilized++;
					eFuseWord[offset][i] |= (((u2Byte)*rtemp8 << 8) & 0xff00);

					if(eFuse_Addr >= EFUSE_REAL_CONTENT_LEN_8192E) 
						break;
				}
				
				wren >>= 1;
				
			}
		}

		// Read next PG header
		ReadEFuseByte(Adapter, eFuse_Addr, rtemp8, bPseudoTest);	
		//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Addr=%d rtemp 0x%x\n", eFuse_Addr, *rtemp8));
		
		if(*rtemp8 != 0xFF && (eFuse_Addr < EFUSE_REAL_CONTENT_LEN_8192E))
		{
			efuse_utilized++;
			eFuse_Addr++;
		}
	}

	//
	// 3. Collect 16 sections and 4 word unit into Efuse map.
	//
	for(i=0; i<EFUSE_MAX_SECTION_8192E; i++)
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
	efuse_usage = (u1Byte)((eFuse_Addr*100)/EFUSE_REAL_CONTENT_LEN_8192E);
	rtw_hal_set_hwreg(Adapter, HW_VAR_EFUSE_BYTES, (u8 *)&eFuse_Addr);

exit:
	if(efuseTbl)
		rtw_mfree(efuseTbl, EFUSE_MAP_LEN_8192E);

	if(eFuseWord)
		rtw_mfree2d((void *)eFuseWord, EFUSE_MAX_SECTION_8192E, EFUSE_MAX_WORD_UNIT, sizeof(u16));
}

static VOID
rtl8192E_ReadEFuse(
	PADAPTER	Adapter,
	u8		efuseType,
	u16		_offset,
	u16 		_size_byte,
	u8      	*pbuf,
	IN	BOOLEAN	bPseudoTest
	)
{
#ifdef DBG_IOL_READ_EFUSE_MAP
	u8 logical_map[512];
#endif

#ifdef CONFIG_IOL_READ_EFUSE_MAP
	if(!bPseudoTest )//&& rtw_IOL_applied(Adapter))
	{
		int ret = _FAIL;

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
#endif
	Hal_EfuseReadEFuse8192E(Adapter, _offset, _size_byte, pbuf, bPseudoTest);

#ifdef CONFIG_IOL_READ_EFUSE_MAP
exit:
#endif
	
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
}

//Do not support BT
VOID
Hal_EFUSEGetEfuseDefinition8192E(
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
				pu1Byte	pMax_section;
				pMax_section = (pu1Byte)pOut;

				if(efuseType == EFUSE_WIFI)
				{
					*pMax_section = EFUSE_MAX_SECTION_8192E;
				}
				else
					*pMax_section = EFUSE_BT_MAX_SECTION_8192E;
			}
			break;
		case TYPE_EFUSE_REAL_CONTENT_LEN:
			{
				pu2Byte pu2Tmp;
				pu2Tmp = (pu2Byte)pOut;
				if(efuseType == EFUSE_WIFI)
				{
					*pu2Tmp = EFUSE_REAL_CONTENT_LEN_8192E;
				}
				else
					*pu2Tmp = EFUSE_BT_REAL_CONTENT_LEN_8192E;
			}
			break;
		case TYPE_EFUSE_CONTENT_LEN_BANK:
			{
				pu2Byte pu2Tmp;
				pu2Tmp = (pu2Byte)pOut;
				if(efuseType == EFUSE_WIFI)
				{
					*pu2Tmp = EFUSE_REAL_CONTENT_LEN_8192E;
				}
				else
					*pu2Tmp = EFUSE_BT_REAL_BANK_CONTENT_LEN_8192E;
			}
			break;
		case TYPE_AVAILABLE_EFUSE_BYTES_BANK:
			{
				pu2Byte pu2Tmp;
				pu2Tmp = (pu2Byte)pOut;
				if(efuseType == EFUSE_WIFI)
				{
					*pu2Tmp = (u2Byte)(EFUSE_REAL_CONTENT_LEN_8192E-EFUSE_OOB_PROTECT_BYTES_8192E);
				}
				else
					*pu2Tmp = (u2Byte)(EFUSE_BT_REAL_BANK_CONTENT_LEN_8192E-EFUSE_PROTECT_BYTES_BANK_8192E);
			}
			break;
		case TYPE_AVAILABLE_EFUSE_BYTES_TOTAL:
			{
				pu2Byte pu2Tmp;
				pu2Tmp = (pu2Byte)pOut;
				if(efuseType == EFUSE_WIFI)
				{
					*pu2Tmp = (u2Byte)(EFUSE_REAL_CONTENT_LEN_8192E-EFUSE_OOB_PROTECT_BYTES_8192E);
				}
				else
					*pu2Tmp = (u2Byte)(EFUSE_BT_REAL_CONTENT_LEN_8192E-(EFUSE_PROTECT_BYTES_BANK_8192E*3));
			}
			break;
		case TYPE_EFUSE_MAP_LEN:
			{
				pu2Byte pu2Tmp;
				pu2Tmp = (pu2Byte)pOut;

				if(efuseType == EFUSE_WIFI)
				{
					*pu2Tmp = (u2Byte)EFUSE_MAP_LEN_8192E;
				}
				else
					*pu2Tmp = (u2Byte)EFUSE_BT_MAP_LEN_8192E;
			}
			break;
		case TYPE_EFUSE_PROTECT_BYTES_BANK:
			{
				pu1Byte pu1Tmp;
				pu1Tmp = (pu1Byte)pOut;
				if(efuseType == EFUSE_WIFI)
					*pu1Tmp = (u1Byte)(EFUSE_OOB_PROTECT_BYTES_8192E);
				else
					*pu1Tmp = (u1Byte)(EFUSE_PROTECT_BYTES_BANK_8192E);
			}
			break;
		default:
			{
				pu1Byte pu1Tmp;
				pu1Tmp = (pu1Byte)pOut;
				*pu1Tmp = 0;
			}
			break;
	}
}
VOID
Hal_EFUSEGetEfuseDefinition_Pseudo8192E(
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
				pu1Byte	pMax_section;
				pMax_section = (pu1Byte)pOut;
				if(efuseType == EFUSE_WIFI)
					*pMax_section = EFUSE_MAX_SECTION_8192E;
				else
					*pMax_section = EFUSE_BT_MAX_SECTION_8192E;
			}
			break;
		case TYPE_EFUSE_REAL_CONTENT_LEN:
			{
				pu2Byte pu2Tmp;
				pu2Tmp = (pu2Byte)pOut;
				if(efuseType == EFUSE_WIFI)
				{
					*pu2Tmp = EFUSE_REAL_CONTENT_LEN_8192E;
				}
				else
					*pu2Tmp = EFUSE_BT_REAL_CONTENT_LEN_8192E;
			}
			break;
		case TYPE_EFUSE_CONTENT_LEN_BANK:
			{
				pu2Byte pu2Tmp;
				pu2Tmp = (pu2Byte)pOut;
				if(efuseType == EFUSE_WIFI)
				{
					*pu2Tmp = EFUSE_REAL_CONTENT_LEN_8192E;
				}
				else
					*pu2Tmp = EFUSE_BT_REAL_BANK_CONTENT_LEN_8192E;
			}
			break;
		case TYPE_AVAILABLE_EFUSE_BYTES_BANK:
			{
				pu2Byte pu2Tmp;
				pu2Tmp = (pu2Byte)pOut;
				if(efuseType == EFUSE_WIFI)
				{
					*pu2Tmp = (u2Byte)(EFUSE_REAL_CONTENT_LEN_8192E-EFUSE_OOB_PROTECT_BYTES_8192E);
				}
				else
					*pu2Tmp = (u2Byte)(EFUSE_BT_REAL_BANK_CONTENT_LEN_8192E-EFUSE_PROTECT_BYTES_BANK_8192E);
			}
			break;
		case TYPE_AVAILABLE_EFUSE_BYTES_TOTAL:
			{
				pu2Byte pu2Tmp;
				pu2Tmp = (pu2Byte)pOut;
				if(efuseType == EFUSE_WIFI)
				{
					*pu2Tmp = (u2Byte)(EFUSE_REAL_CONTENT_LEN_8192E-EFUSE_OOB_PROTECT_BYTES_8192E);

				}
				else
					*pu2Tmp = (u2Byte)(EFUSE_BT_REAL_CONTENT_LEN_8192E-(EFUSE_PROTECT_BYTES_BANK_8192E*3));
			}
			break;
		case TYPE_EFUSE_MAP_LEN:
			{
				pu2Byte pu2Tmp;
				pu2Tmp = (pu2Byte)pOut;
				if(efuseType == EFUSE_WIFI)
					*pu2Tmp = (u2Byte)EFUSE_MAP_LEN_8192E;
				else
					*pu2Tmp = (u2Byte)EFUSE_BT_MAP_LEN_8192E;
			}
			break;
		case TYPE_EFUSE_PROTECT_BYTES_BANK:
			{
				pu1Byte pu1Tmp;
				pu1Tmp = (pu1Byte)pOut;
				if(efuseType == EFUSE_WIFI)
					*pu1Tmp = (u1Byte)(EFUSE_OOB_PROTECT_BYTES_8192E);
				else
					*pu1Tmp = (u1Byte)(EFUSE_PROTECT_BYTES_BANK_8192E);
			}
			break;
		default:
			{
				pu1Byte pu1Tmp;
				pu1Tmp = (pu1Byte)pOut;
				*pu1Tmp = 0;
			}
			break;
	}
}


static VOID
rtl8192E_EFUSE_GetEfuseDefinition(
	IN		PADAPTER	pAdapter,
	IN		u8		efuseType,
	IN		u8		type,
	OUT		void		*pOut,
	IN		BOOLEAN		bPseudoTest
	)
{
	if(bPseudoTest)
	{
		Hal_EFUSEGetEfuseDefinition_Pseudo8192E(pAdapter, efuseType, type, pOut);
	}
	else
	{
		Hal_EFUSEGetEfuseDefinition8192E(pAdapter, efuseType, type, pOut);
	}
}

static u8
Hal_EfuseWordEnableDataWrite8192E(	IN	PADAPTER	pAdapter,
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
rtl8192E_Efuse_WordEnableDataWrite(	IN	PADAPTER	pAdapter,
							IN	u16		efuse_addr,
							IN	u8		word_en,
							IN	u8		*data,
							IN	BOOLEAN		bPseudoTest)
{
	u8	ret=0;

	ret = Hal_EfuseWordEnableDataWrite8192E(pAdapter, efuse_addr, word_en, data, bPseudoTest);

	return ret;
}

static u16
hal_EfuseGetCurrentSize_8192E(IN	PADAPTER	pAdapter,
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
			(efuse_addr  < EFUSE_REAL_CONTENT_LEN_8192E))
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
rtl8192E_EfuseGetCurrentSize(
	IN	PADAPTER	pAdapter,
	IN	u8			efuseType,
	IN	BOOLEAN		bPseudoTest)
{
	u16	ret=0;

	ret = hal_EfuseGetCurrentSize_8192E(pAdapter, bPseudoTest);

	return ret;
}


static int
hal_EfusePgPacketRead_8192E(
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

	if(data==NULL)
		return _FALSE;
	if(offset>EFUSE_MAX_SECTION_8192E)
		return _FALSE;

	_rtw_memset((PVOID)data, 0xff, sizeof(u8)*PGPKT_DATA_SIZE);
	_rtw_memset((PVOID)tmpdata, 0xff, sizeof(u8)*PGPKT_DATA_SIZE);


	//
	// <Roger_TODO> Efuse has been pre-programmed dummy 5Bytes at the end of Efuse by CP.
	// Skip dummy parts to prevent unexpected data read from Efuse.
	// By pass right now. 2009.02.19.
	//
	while(bContinual && (efuse_addr  < EFUSE_REAL_CONTENT_LEN_8192E) )
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
rtl8192E_Efuse_PgPacketRead(	IN	PADAPTER	pAdapter,
					IN	u8			offset,
					IN	u8			*data,
					IN	BOOLEAN		bPseudoTest)
{
	int	ret=0;

	ret = hal_EfusePgPacketRead_8192E(pAdapter, offset, data, bPseudoTest);

	return ret;
}

int
hal_EfusePgPacketWrite_8192E(IN	PADAPTER	pAdapter, 
					IN	u8 			offset,
					IN	u8			word_en,
					IN	u8			*data,
					IN	BOOLEAN		bPseudoTest)
{
	u8 WriteState = PG_STATE_HEADER; 	

	int bContinual = _TRUE,bDataEmpty=_TRUE; 
	//int bResult = _TRUE;
	u16	efuse_addr = 0;
	u8	efuse_data;

	u8	pg_header = 0, pg_header_temp = 0;

	u8	tmp_word_cnts=0,target_word_cnts=0;
	u8	tmp_header,match_word_en,tmp_word_en;

	PGPKT_STRUCT target_pkt;	
	PGPKT_STRUCT tmp_pkt;
	
	u8	originaldata[sizeof(u8)*8];
	u8	tmpindex = 0,badworden = 0x0F;

	static int	repeat_times = 0;

	BOOLEAN		bExtendedHeader = _FALSE;
	u8	efuseType=EFUSE_WIFI;
	
	//
	// <Roger_Notes> Efuse has been pre-programmed dummy 5Bytes at the end of Efuse by CP.
	// So we have to prevent unexpected data string connection, which will cause
	// incorrect data auto-load from HW. The total size is equal or smaller than 498bytes
	// (i.e., offset 0~497, and dummy 1bytes) expected after CP test.
	// 2009.02.19.
	//
	if( Efuse_GetCurrentSize(pAdapter, efuseType, bPseudoTest) >= (EFUSE_REAL_CONTENT_LEN_8192E-EFUSE_OOB_PROTECT_BYTES_8192E))
	{
		DBG_871X("hal_EfusePgPacketWrite_8812A() error: %x >= %x\n", Efuse_GetCurrentSize(pAdapter, efuseType, bPseudoTest), (EFUSE_REAL_CONTENT_LEN_8192E-EFUSE_OOB_PROTECT_BYTES_8192E));
		return _FALSE;
	}

	// Init the 8 bytes content as 0xff
	target_pkt.offset = offset;
	target_pkt.word_en= word_en;
	// Initial the value to avoid compile warning
	tmp_pkt.offset = 0;
	tmp_pkt.word_en= 0;

	//DBG_871X("hal_EfusePgPacketWrite_8812A target offset 0x%x word_en 0x%x \n", target_pkt.offset, target_pkt.word_en);

	_rtw_memset((PVOID)target_pkt.data, 0xFF, sizeof(u8)*8);
	
	efuse_WordEnableDataRead(word_en, data, target_pkt.data);
	target_word_cnts = Efuse_CalculateWordCnts(target_pkt.word_en);

	//efuse_reg_ctrl(pAdapter,_TRUE);//power on
	//DBG_871X("EFUSE Power ON\n");

	//
	// <Roger_Notes> Efuse has been pre-programmed dummy 5Bytes at the end of Efuse by CP.
	// So we have to prevent unexpected data string connection, which will cause
	// incorrect data auto-load from HW. Dummy 1bytes is additional.
	// 2009.02.19.
	//
	while( bContinual && (efuse_addr  < (EFUSE_REAL_CONTENT_LEN_8192E-EFUSE_OOB_PROTECT_BYTES_8192E)) )
	{
		if(WriteState==PG_STATE_HEADER)
		{	
			bDataEmpty=_TRUE;
			badworden = 0x0F;		
			//************	so *******************
			//DBG_871X("EFUSE PG_STATE_HEADER\n");
			if (	efuse_OneByteRead(pAdapter, efuse_addr ,&efuse_data, bPseudoTest) &&
				(efuse_data!=0xFF))
			{	
				if((efuse_data&0x1F) == 0x0F)		//extended header
				{
					tmp_header = efuse_data;
					efuse_addr++;
					efuse_OneByteRead(pAdapter, efuse_addr ,&efuse_data, bPseudoTest);
					if((efuse_data & 0x0F) == 0x0F) //wren fail
					{
						efuse_addr++;
						continue;
					}
					else
					{
						tmp_pkt.offset = ((tmp_header & 0xE0) >> 5) | ((efuse_data & 0xF0) >> 1);
						tmp_pkt.word_en = efuse_data & 0x0F;
					}
				}
				else
				{
					tmp_header	=  efuse_data;					
					tmp_pkt.offset	= (tmp_header>>4) & 0x0F;
					tmp_pkt.word_en = tmp_header & 0x0F;					
				}				
				tmp_word_cnts =  Efuse_CalculateWordCnts(tmp_pkt.word_en);

				//DBG_871X("section offset 0x%x worden 0x%x\n", tmp_pkt.offset, tmp_pkt.word_en);

				//************	so-1 *******************
				if(tmp_pkt.offset  != target_pkt.offset)
				{
					efuse_addr = efuse_addr + (tmp_word_cnts*2) +1; //Next pg_packet
#if (EFUSE_ERROE_HANDLE == 1)
					WriteState = PG_STATE_HEADER;
#endif
				}
				else		//write the same offset
				{	
					//DBG_871X("hal_EfusePgPacketWrite_8812A section offset the same\n");
					//************	so-2 *******************
					for(tmpindex=0 ; tmpindex<(tmp_word_cnts*2) ; tmpindex++)
					{
						if(efuse_OneByteRead(pAdapter, (efuse_addr+1+tmpindex) ,&efuse_data, bPseudoTest)&&(efuse_data != 0xFF)){
							bDataEmpty = _FALSE;	
						}
					}	
					//************	so-2-1 *******************
					if(bDataEmpty == _FALSE)
					{		
						//DBG_871X("hal_EfusePgPacketWrite_8812A section offset the same and data is NOT empty\n");
					
						efuse_addr = efuse_addr + (tmp_word_cnts*2) +1; //Next pg_packet										
#if (EFUSE_ERROE_HANDLE == 1)
						WriteState=PG_STATE_HEADER;
#endif
					}
					else
					{//************  so-2-2 *******************
						//DBG_871X("hal_EfusePgPacketWrite_8812A section data empty\n");
						match_word_en = 0x0F;			//same bit as original wren
						if(   !( (target_pkt.word_en&BIT0)|(tmp_pkt.word_en&BIT0)  ))
						{
							 match_word_en &= (~BIT0);
						}	
						if(   !( (target_pkt.word_en&BIT1)|(tmp_pkt.word_en&BIT1)  ))
						{
							 match_word_en &= (~BIT1);
						}
						if(   !( (target_pkt.word_en&BIT2)|(tmp_pkt.word_en&BIT2)  ))
						{
							 match_word_en &= (~BIT2);
						}
						if(   !( (target_pkt.word_en&BIT3)|(tmp_pkt.word_en&BIT3)  ))
						{
							 match_word_en &= (~BIT3);
						}					
												
						//************	so-2-2-A *******************
						if((match_word_en&0x0F)!=0x0F)
						{							
							badworden = Efuse_WordEnableDataWrite(pAdapter,efuse_addr+1, tmp_pkt.word_en ,target_pkt.data, bPseudoTest);
							
							//************	so-2-2-A-1 *******************
							//############################
							if(0x0F != (badworden&0x0F))
							{														
								u8	reorg_offset = offset;
								u8	reorg_worden=badworden;								
								Efuse_PgPacketWrite(pAdapter, reorg_offset, reorg_worden, target_pkt.data, bPseudoTest);	
							}	
							//############################						

							tmp_word_en = 0x0F; 	//not the same bit as original wren
							if(  (target_pkt.word_en&BIT0)^(match_word_en&BIT0)  )
							{
								tmp_word_en &= (~BIT0);
							}
							if(   (target_pkt.word_en&BIT1)^(match_word_en&BIT1) )
							{
								tmp_word_en &=	(~BIT1);
							}
							if(   (target_pkt.word_en&BIT2)^(match_word_en&BIT2) )
							{
								tmp_word_en &= (~BIT2);
							}						
							if(   (target_pkt.word_en&BIT3)^(match_word_en&BIT3) )
							{
								tmp_word_en &=(~BIT3);
							}							
						
							//************	so-2-2-A-2 *******************	
							if((tmp_word_en&0x0F)!=0x0F){
								//reorganize other pg packet						
//								efuse_addr = efuse_addr + (2*tmp_word_cnts) +1;//next pg packet addr							
								efuse_addr = Efuse_GetCurrentSize(pAdapter, efuseType, bPseudoTest);
								//===========================
								target_pkt.offset = offset;
								target_pkt.word_en= tmp_word_en;					
								//===========================
							}else{								
								bContinual = _FALSE;
							}
#if (EFUSE_ERROE_HANDLE == 1)
							WriteState=PG_STATE_HEADER;
							repeat_times++;
							if(repeat_times>EFUSE_REPEAT_THRESHOLD_){
								bContinual = _FALSE;
								//bResult = _FALSE;
							}
#endif
						}
						else{//************  so-2-2-B *******************
							//reorganize other pg packet						
							efuse_addr = efuse_addr + (2*tmp_word_cnts) +1;//next pg packet addr							
							//===========================
							target_pkt.offset = offset;
							target_pkt.word_en= target_pkt.word_en; 				
							//===========================			
#if (EFUSE_ERROE_HANDLE == 1)
							WriteState=PG_STATE_HEADER;
#endif
						}		
					}				
				}				
				DBG_871X("EFUSE PG_STATE_HEADER-1\n");
			}
			else		//************	s1: header == oxff	*******************
			{
				bExtendedHeader = _FALSE;
			
				if(target_pkt.offset >= EFUSE_MAX_SECTION_BASE)
				{
					pg_header = ((target_pkt.offset &0x07) << 5) | 0x0F;

					//DBG_871X("hal_EfusePgPacketWrite_8812A extended pg_header[2:0] |0x0F 0x%x \n", pg_header);

					efuse_OneByteWrite(pAdapter,efuse_addr, pg_header, bPseudoTest);
					efuse_OneByteRead(pAdapter,efuse_addr, &tmp_header, bPseudoTest);

					while(tmp_header == 0xFF)
					{		
						//DBG_871X("hal_EfusePgPacketWrite_8812A extended pg_header[2:0] wirte fail \n");

						repeat_times++; 	
					
						if(repeat_times>EFUSE_REPEAT_THRESHOLD_){
							bContinual = _FALSE;
							//bResult = _FALSE;
							efuse_addr++;
							break;
						}		
						efuse_OneByteWrite(pAdapter,efuse_addr, pg_header, bPseudoTest);
						efuse_OneByteRead(pAdapter,efuse_addr, &tmp_header, bPseudoTest);	
					}
					
					if(!bContinual)
						break;

					if(tmp_header == pg_header)
					{
						efuse_addr++;
						pg_header_temp = pg_header;
						pg_header = ((target_pkt.offset & 0x78) << 1 ) | target_pkt.word_en;

						//DBG_871X("hal_EfusePgPacketWrite_8812A extended pg_header[6:3] | worden 0x%x word_en 0x%x \n", pg_header, target_pkt.word_en);

						efuse_OneByteWrite(pAdapter,efuse_addr, pg_header, bPseudoTest);
						efuse_OneByteRead(pAdapter,efuse_addr, &tmp_header, bPseudoTest);

						while(tmp_header == 0xFF)
						{											
							repeat_times++; 	

							if(repeat_times>EFUSE_REPEAT_THRESHOLD_){
								bContinual = _FALSE;
								//bResult = _FALSE;
								break;
							}							
							efuse_OneByteWrite(pAdapter,efuse_addr, pg_header, bPseudoTest);
							efuse_OneByteRead(pAdapter,efuse_addr, &tmp_header, bPseudoTest);
						}

						if(!bContinual)
							break;

						if((tmp_header & 0x0F) == 0x0F) //wren PG fail
						{
							repeat_times++; 	

							if(repeat_times>EFUSE_REPEAT_THRESHOLD_){
								bContinual = _FALSE;
								//bResult = _FALSE;
								break;
							}							
							else
							{
								efuse_addr++;
								continue;
							}
						}
						else if(pg_header != tmp_header)	//offset PG fail						
						{
							bExtendedHeader = _TRUE;
							tmp_pkt.offset = ((pg_header_temp & 0xE0) >> 5) | ((tmp_header & 0xF0) >> 1);
							tmp_pkt.word_en=  tmp_header & 0x0F;					
							tmp_word_cnts =  Efuse_CalculateWordCnts(tmp_pkt.word_en);	
						}
					}
					else if ((tmp_header & 0x1F) == 0x0F)		//wrong extended header
					{
						efuse_addr+=2;
						continue;						
					}
				}
				else
				{
					pg_header = ((target_pkt.offset << 4)&0xf0) |target_pkt.word_en;
					efuse_OneByteWrite(pAdapter,efuse_addr, pg_header, bPseudoTest);
					efuse_OneByteRead(pAdapter,efuse_addr, &tmp_header, bPseudoTest);
				}
		
				if(tmp_header == pg_header)
				{ //************  s1-1*******************								
					WriteState = PG_STATE_DATA; 					
				}				
#if (EFUSE_ERROE_HANDLE == 1)
				else if(tmp_header == 0xFF){//************	s1-3: if Write or read func doesn't work *******************		
					//efuse_addr doesn't change
					WriteState = PG_STATE_HEADER;					
					repeat_times++;
					if(repeat_times>EFUSE_REPEAT_THRESHOLD_){
						bContinual = _FALSE;
						//bResult = _FALSE;
					}
				}
#endif
				else
				{//************  s1-2 : fixed the header procedure *******************							
					if(!bExtendedHeader)
					{
						tmp_pkt.offset = (tmp_header>>4) & 0x0F;
						tmp_pkt.word_en=  tmp_header & 0x0F;					
						tmp_word_cnts =  Efuse_CalculateWordCnts(tmp_pkt.word_en);
					}
																											
					//************	s1-2-A :cover the exist data *******************
					_rtw_memset(originaldata, 0xff, sizeof(u8)*8);
					
					if(Efuse_PgPacketRead( pAdapter, tmp_pkt.offset,originaldata, bPseudoTest))
					{	//check if data exist					
						//efuse_reg_ctrl(pAdapter,_TRUE);//power on
						badworden = Efuse_WordEnableDataWrite(pAdapter,efuse_addr+1,tmp_pkt.word_en,originaldata, bPseudoTest);
						//############################
						if(0x0F != (badworden&0x0F))
						{														
							u8	reorg_offset = tmp_pkt.offset;
							u8	reorg_worden=badworden;								
							Efuse_PgPacketWrite(pAdapter,reorg_offset,reorg_worden,originaldata, bPseudoTest);	
							efuse_addr = Efuse_GetCurrentSize(pAdapter, efuseType, bPseudoTest);
						}
						//############################
						else{
							efuse_addr = efuse_addr + (tmp_word_cnts*2) +1; //Next pg_packet							
						}
					}
					 //************  s1-2-B: wrong address*******************
					else
					{
						efuse_addr = efuse_addr + (tmp_word_cnts*2) +1; //Next pg_packet
					}

#if (EFUSE_ERROE_HANDLE == 1)
					WriteState=PG_STATE_HEADER; 
					repeat_times++;
					if(repeat_times>EFUSE_REPEAT_THRESHOLD_){
						bContinual = _FALSE;
						//bResult = _FALSE;
					}
#endif

					//DBG_871X("EFUSE PG_STATE_HEADER-2\n");
				}

			}

		}
		//write data state
		else if(WriteState==PG_STATE_DATA) 
		{	//************	s1-1  *******************
			//DBG_871X("EFUSE PG_STATE_DATA\n");
			badworden = 0x0f;
			badworden = Efuse_WordEnableDataWrite(pAdapter,efuse_addr+1,target_pkt.word_en,target_pkt.data, bPseudoTest);	
			if((badworden&0x0F)==0x0F)
			{ //************  s1-1-A *******************
				bContinual = _FALSE;
			}
			else
			{//reorganize other pg packet //************  s1-1-B *******************
				efuse_addr = efuse_addr + (2*target_word_cnts) +1;//next pg packet addr
										
				//===========================
				target_pkt.offset = offset;
				target_pkt.word_en= badworden;		
				target_word_cnts = Efuse_CalculateWordCnts(target_pkt.word_en);
				//===========================			
#if (EFUSE_ERROE_HANDLE == 1)
				WriteState=PG_STATE_HEADER; 
				repeat_times++;
				if(repeat_times>EFUSE_REPEAT_THRESHOLD_){
					bContinual = _FALSE;
					//bResult = _FALSE;
				}
#endif
				//DBG_871X("EFUSE PG_STATE_HEADER-3\n");
			}
		}
	}

	if(efuse_addr  >= (EFUSE_REAL_CONTENT_LEN_8192E-EFUSE_OOB_PROTECT_BYTES_8192E))
	{
		DBG_871X("hal_EfusePgPacketWrite_8812A(): efuse_addr(%#x) Out of size!!\n", efuse_addr);
	}
	//efuse_reg_ctrl(pAdapter,_FALSE);//power off
	
	return _TRUE;
}

static int
rtl8192E_Efuse_PgPacketWrite(IN	PADAPTER	pAdapter,
					IN	u8 			offset,
					IN	u8			word_en,
					IN	u8			*data,
					IN	BOOLEAN		bPseudoTest)
{
	int	ret;

	ret = hal_EfusePgPacketWrite_8192E(pAdapter, offset, word_en, data, bPseudoTest);

	return ret;
}


u8
GetEEPROMSize8192E(
	IN	PADAPTER	Adapter
	)
{
	u8	size = 0;
	u32	curRCR;

	curRCR = rtw_read16(Adapter, REG_SYS_EEPROM_CTRL);
	size = (curRCR & EEPROMSEL) ? 6 : 4; // 6: EEPROM used is 93C46, 4: boot from E-Fuse.
	
	DBG_871X("EEPROM type is %s\n", size==4 ? "E-FUSE" : "93C46");
	//return size;
	return 4; // <20120713, Kordan> The default value of HW is 6 ?!!
}
//===========================================================
//				Efuse related code
//===========================================================
static void ResumeTxBeacon(_adapter *padapter)
{
	HAL_DATA_TYPE*	pHalData = GET_HAL_DATA(padapter);	

	// 2010.03.01. Marked by tynli. No need to call workitem beacause we record the value
	// which should be read from register to a global variable.

	rtw_write8(padapter, REG_FWHW_TXQ_CTRL+2, (pHalData->RegFwHwTxQCtrl) | BIT6);
	pHalData->RegFwHwTxQCtrl |= BIT6;
	rtw_write8(padapter, REG_TBTT_PROHIBIT+1, 0xff);
	pHalData->RegReg542 |= BIT0;
	rtw_write8(padapter, REG_TBTT_PROHIBIT+2, pHalData->RegReg542);
}

static void StopTxBeacon(_adapter *padapter)
{
	HAL_DATA_TYPE*	pHalData = GET_HAL_DATA(padapter);

	// 2010.03.01. Marked by tynli. No need to call workitem beacause we record the value
	// which should be read from register to a global variable.

	rtw_write8(padapter, REG_FWHW_TXQ_CTRL+2, (pHalData->RegFwHwTxQCtrl) & (~BIT6));
	pHalData->RegFwHwTxQCtrl &= (~BIT6);
	rtw_write8(padapter, REG_TBTT_PROHIBIT+1, 0x64);
	pHalData->RegReg542 &= ~(BIT0);
	rtw_write8(padapter, REG_TBTT_PROHIBIT+2, pHalData->RegReg542);

	 //todo: CheckFwRsvdPageContent(Adapter);  // 2010.06.23. Added by tynli.

}

static void hw_var_set_opmode(PADAPTER Adapter, u8 variable, u8* val)
{
	u8	val8;
	u32 val32;
	u8	mode = *((u8 *)val);
	
	DBG_871X( ADPT_FMT "Port-%d  set opmode = %d\n",ADPT_ARG(Adapter),
		#ifdef CONFIG_CONCURRENT_MODE
		Adapter->iface_type
		#else
		0
		#endif
		,mode);
#ifdef CONFIG_CONCURRENT_MODE
	if(Adapter->iface_type == IFACE_PORT1)
	{
		// disable Port1 TSF update
		rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1)|DIS_TSF_UDT);
		
		Set_MSR(Adapter, mode);
		
		//DBG_871X("#### %s() -%d iface_type(%d) mode = %d ####\n", __FUNCTION__, __LINE__, Adapter->iface_type,mode);

		if((mode == _HW_STATE_STATION_) || (mode == _HW_STATE_NOLINK_))
		{
			if(!check_buddy_mlmeinfo_state(Adapter, WIFI_FW_AP_STATE))			
			{
				#ifdef CONFIG_INTERRUPT_BASED_TXBCN

				#ifdef  CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT	
				rtw_write8(Adapter, REG_DRVERLYINT, 0x05);//restore early int time to 5ms
				#if defined(CONFIG_USB_HCI)
				UpdateInterruptMask8192EU(Adapter,_TRUE, 0, IMR_BCNDMAINT1_8192E);	
				#elif defined(CONFIG_SDIO_HCI)
				#endif
				#endif // CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
				
				#ifdef CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR		
				#if defined(CONFIG_USB_HCI)
				UpdateInterruptMask8192EU(Adapter,_TRUE ,0, (IMR_TXBCN0ERR_8192E|IMR_TXBCN0OK_8192E));	
				#elif defined(CONFIG_SDIO_HCI)
				#endif
				#endif// CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR
					
				#endif //CONFIG_INTERRUPT_BASED_TXBCN
				StopTxBeacon(Adapter);
			}
			
			rtw_write8(Adapter,REG_BCN_CTRL_1, 0x11);//disable atim wnd and disable beacon function
			//rtw_write8(Adapter,REG_BCN_CTRL_1, 0x18);
		}
		else if((mode == _HW_STATE_ADHOC_) /*|| (mode == _HW_STATE_AP_)*/)
		{
			ResumeTxBeacon(Adapter);
			rtw_write8(Adapter,REG_BCN_CTRL_1, 0x1a);
		}
		else if(mode == _HW_STATE_AP_)
		{
			#ifdef CONFIG_INTERRUPT_BASED_TXBCN
			#ifdef  CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
			#if defined(CONFIG_USB_HCI)
			UpdateInterruptMask8192EU(Adapter,_TRUE ,IMR_BCNDMAINT1_8192E, 0);
			#elif defined(CONFIG_SDIO_HCI)
			#endif
			#endif//CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT

			#ifdef CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR
			#if defined(CONFIG_USB_HCI)
			UpdateInterruptMask8192EU(Adapter,_TRUE ,(IMR_TXBCN0ERR_8192E|IMR_TXBCN0OK_8192E), 0);
			#elif defined(CONFIG_SDIO_HCI)
			#endif
			#endif//CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR
					
			#endif //CONFIG_INTERRUPT_BASED_TXBCN

			ResumeTxBeacon(Adapter);	
				
			rtw_write8(Adapter, REG_BCN_CTRL_1, 0x12);

			//Set RCR
			//rtw_write32(padapter, REG_RCR, 0x70002a8e);//CBSSID_DATA must set to 0
			//rtw_write32(Adapter, REG_RCR, 0x7000228e);//CBSSID_DATA must set to 0
			rtw_write32(Adapter, REG_RCR, 0x7000208e);//CBSSID_DATA must set to 0,reject ICV_ERR packet
			//enable to rx data frame				
			rtw_write16(Adapter, REG_RXFLTMAP2, 0xFFFF);
			//enable to rx ps-poll
			rtw_write16(Adapter, REG_RXFLTMAP1, 0x0400);

			//Beacon Control related register for first time 
			rtw_write8(Adapter, REG_BCNDMATIM, 0x02); // 2ms		

			//rtw_write8(Adapter, REG_BCN_MAX_ERR, 0xFF);
			rtw_write8(Adapter, REG_ATIMWND_1, 0x0a); // 10ms for port1
			rtw_write16(Adapter, REG_BCNTCFG, 0x00);
			rtw_write16(Adapter, REG_TBTT_PROHIBIT, 0xff04);
			rtw_write16(Adapter, REG_TSFTR_SYN_OFFSET, 0x7fff);// +32767 (~32ms)
	
			//reset TSF2	
			rtw_write8(Adapter, REG_DUAL_TSF_RST, BIT(1));

     	             //enable BCN1 Function for if2
			//don't enable update TSF1 for if2 (due to TSF update when beacon/probe rsp are received)
		     	rtw_write8(Adapter, REG_BCN_CTRL_1, (DIS_TSF_UDT|EN_BCN_FUNCTION | EN_TXBCN_RPT|DIS_RX_BSSID_FIT));

			//SW_BCN_SEL - Port1
			//rtw_write8(Adapter, REG_DWBCN1_CTRL_8192E+2, rtw_read8(Adapter, REG_DWBCN1_CTRL_8192E+2)|BIT4);
			rtw_hal_set_hwreg(Adapter, HW_VAR_DL_BCN_SEL, NULL);
			
			// select BCN on port 1
			rtw_write8(Adapter, REG_CCK_CHECK_8192E,
				(rtw_read8(Adapter, REG_CCK_CHECK_8192E)|BIT_BCN_PORT_SEL));
			
			
#ifdef CONFIG_CONCURRENT_MODE
			if(check_buddy_fwstate(Adapter, WIFI_FW_NULL_STATE))
				rtw_write8(Adapter, REG_BCN_CTRL, 
					rtw_read8(Adapter, REG_BCN_CTRL) & ~EN_BCN_FUNCTION);
#endif      			
			//dis BCN0 ATIM  WND if if1 is station
			//rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)|DIS_ATIM);

#ifdef CONFIG_TSF_RESET_OFFLOAD
			// Reset TSF for STA+AP concurrent mode
			if ( check_buddy_fwstate(Adapter, (WIFI_STATION_STATE|WIFI_ASOC_STATE)) ) {
				if (reset_tsf(Adapter, IFACE_PORT1) == _FALSE)
					DBG_871X("ERROR! %s()-%d: Reset port1 TSF fail\n",
						__FUNCTION__, __LINE__);
			}
#endif	// CONFIG_TSF_RESET_OFFLOAD	

		}
	}
	else
#endif //CONFIG_CONCURRENT_MODE
	{
		// disable Port0 TSF update
		rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)|DIS_TSF_UDT);
		
		Set_MSR(Adapter, mode);
		
		//DBG_871X("#### %s() -%d iface_type(0) mode = %d ####\n", __FUNCTION__, __LINE__, mode);
		
		if((mode == _HW_STATE_STATION_) || (mode == _HW_STATE_NOLINK_))
		{
#ifdef CONFIG_CONCURRENT_MODE
			if(!check_buddy_mlmeinfo_state(Adapter, WIFI_FW_AP_STATE))		
#endif //CONFIG_CONCURRENT_MODE
			{
			#ifdef CONFIG_INTERRUPT_BASED_TXBCN	
				#ifdef  CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
				rtw_write8(Adapter, REG_DRVERLYINT, 0x05);//restore early int time to 5ms					
				#if defined(CONFIG_USB_HCI)
				UpdateInterruptMask8192EU(Adapter,_TRUE, 0, IMR_BCNDMAINT0_8192E);
				#elif defined(CONFIG_SDIO_HCI)
				#endif
				#endif//CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
				
				#ifdef CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR
				#if defined(CONFIG_USB_HCI)				
				UpdateInterruptMask8192EU(Adapter,_TRUE ,0, (IMR_TXBCN0ERR_8192E|IMR_TXBCN0OK_8192E));	
				#elif defined(CONFIG_SDIO_HCI)
				#endif
				#endif //CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR
					
			#endif //CONFIG_INTERRUPT_BASED_TXBCN		
				StopTxBeacon(Adapter);
			}	
			rtw_write8(Adapter,REG_BCN_CTRL, 0x19);//disable atim wnd
			//rtw_write8(Adapter,REG_BCN_CTRL, 0x18);
		}
		else if((mode == _HW_STATE_ADHOC_) /*|| (mode == _HW_STATE_AP_)*/)
		{
			ResumeTxBeacon(Adapter);
			rtw_write8(Adapter,REG_BCN_CTRL, 0x1a);
		}
		else if(mode == _HW_STATE_AP_)
		{

#ifdef CONFIG_INTERRUPT_BASED_TXBCN			
			#ifdef  CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
			#if defined(CONFIG_USB_HCI)
			UpdateInterruptMask8192EU(Adapter,_TRUE ,IMR_BCNDMAINT0_8192E, 0);
			#elif defined(CONFIG_SDIO_HCI)
			#endif
			#endif//CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT

			#ifdef CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR	
			#if defined(CONFIG_USB_HCI)
			UpdateInterruptMask8192EU(Adapter,_TRUE ,(IMR_TXBCN0ERR_8192E|IMR_TXBCN0OK_8192E), 0);
			#elif defined(CONFIG_SDIO_HCI)
			#endif
			#endif//CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR
					
#endif //CONFIG_INTERRUPT_BASED_TXBCN

			ResumeTxBeacon(Adapter);
			rtw_write8(Adapter, REG_BCN_CTRL, 0x12);			

			//Set RCR
			//rtw_write32(padapter, REG_RCR, 0x70002a8e);//CBSSID_DATA must set to 0
			//rtw_write32(Adapter, REG_RCR, 0x7000228e);//CBSSID_DATA must set to 0
			rtw_write32(Adapter, REG_RCR, 0x7000208e);//CBSSID_DATA must set to 0,reject ICV_ERR packet
			//enable to rx data frame
			rtw_write16(Adapter, REG_RXFLTMAP2, 0xFFFF);
			//enable to rx ps-poll
			rtw_write16(Adapter, REG_RXFLTMAP1, 0x0400);

			//Beacon Control related register for first time
			rtw_write8(Adapter, REG_BCNDMATIM, 0x02); // 2ms			
			
			//rtw_write8(Adapter, REG_BCN_MAX_ERR, 0xFF);
			rtw_write8(Adapter, REG_ATIMWND, 0x0a); // 10ms
			rtw_write16(Adapter, REG_BCNTCFG, 0x00);
			rtw_write16(Adapter, REG_TBTT_PROHIBIT, 0xff04);
			rtw_write16(Adapter, REG_TSFTR_SYN_OFFSET, 0x7fff);// +32767 (~32ms)

			//reset TSF
			rtw_write8(Adapter, REG_DUAL_TSF_RST, BIT(0));
	
		      	//enable BCN0 Function for if1
			//don't enable update TSF0 for if1 (due to TSF update when beacon/probe rsp are received)
			rtw_write8(Adapter, REG_BCN_CTRL, (DIS_TSF_UDT|EN_BCN_FUNCTION | EN_TXBCN_RPT|DIS_RX_BSSID_FIT));

			//SW_BCN_SEL - Port0
			//rtw_write8(Adapter, REG_DWBCN1_CTRL_8192E+2, rtw_read8(Adapter, REG_DWBCN1_CTRL_8192E+2) & ~BIT4);
			rtw_hal_set_hwreg(Adapter, HW_VAR_DL_BCN_SEL, NULL);
			
			// select BCN on port 0
			rtw_write8(Adapter, REG_CCK_CHECK_8192E,
				(rtw_read8(Adapter, REG_CCK_CHECK_8192E)& ~BIT_BCN_PORT_SEL));
			
			
#ifdef CONFIG_CONCURRENT_MODE
			if(check_buddy_fwstate(Adapter, WIFI_FW_NULL_STATE))
				rtw_write8(Adapter, REG_BCN_CTRL_1, 
					rtw_read8(Adapter, REG_BCN_CTRL_1) & ~EN_BCN_FUNCTION);
#endif

			//dis BCN1 ATIM  WND if if2 is station
			//rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1)|DIS_ATIM);	
#ifdef CONFIG_TSF_RESET_OFFLOAD
			// Reset TSF for STA+AP concurrent mode
			if ( check_buddy_fwstate(Adapter, (WIFI_STATION_STATE|WIFI_ASOC_STATE)) ) {
				if (reset_tsf(Adapter, IFACE_PORT0) == _FALSE)
					DBG_871X("ERROR! %s()-%d: Reset port0 TSF fail\n",__FUNCTION__, __LINE__);
			}
#endif	// CONFIG_TSF_RESET_OFFLOAD

		}
	}

}

static void hw_var_set_macaddr(PADAPTER Adapter, u8 variable, u8* val)
{
	u8 idx = 0;
	u32 reg_macid;

#ifdef CONFIG_CONCURRENT_MODE
	if(Adapter->iface_type == IFACE_PORT1)
	{
		reg_macid = REG_MACID1;
	}
	else
#endif
	{
		reg_macid = REG_MACID;
	}

	for(idx = 0 ; idx < 6; idx++)
	{
		rtw_write8(Adapter, (reg_macid+idx), val[idx]);
	}
	
}

static void hw_var_set_bssid(PADAPTER Adapter, u8 variable, u8* val)
{
	u8	idx = 0;
	u32 reg_bssid;

#ifdef CONFIG_CONCURRENT_MODE
	if(Adapter->iface_type == IFACE_PORT1)
	{
		reg_bssid = REG_BSSID1;
	}
	else
#endif
	{
		reg_bssid = REG_BSSID;
	}

	for(idx = 0 ; idx < 6; idx++)
	{
		rtw_write8(Adapter, (reg_bssid+idx), val[idx]);
	}

}

static void hw_var_set_bcn_func(PADAPTER Adapter, u8 variable, u8* val)
{
	u32 bcn_ctrl_reg;

#ifdef CONFIG_CONCURRENT_MODE
	if(Adapter->iface_type == IFACE_PORT1)
	{
		bcn_ctrl_reg = REG_BCN_CTRL_1;
	}	
	else
#endif		
	{		
		bcn_ctrl_reg = REG_BCN_CTRL;
	}

	if(*((u8 *)val))
	{
		rtw_write8(Adapter, bcn_ctrl_reg, (EN_BCN_FUNCTION | EN_TXBCN_RPT));
	}
	else
	{
		rtw_write8(Adapter, bcn_ctrl_reg, rtw_read8(Adapter, bcn_ctrl_reg)&(~(EN_BCN_FUNCTION | EN_TXBCN_RPT)));
	}
	

}

static void hw_var_set_correct_tsf(PADAPTER Adapter, u8 variable, u8* val)
{
#ifdef CONFIG_CONCURRENT_MODE
	u64	tsf;
	struct mlme_ext_priv	*pmlmeext = &Adapter->mlmeextpriv;
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	PADAPTER pbuddy_adapter = Adapter->pbuddy_adapter;

	//tsf = pmlmeext->TSFValue - ((u32)pmlmeext->TSFValue % (pmlmeinfo->bcn_interval*1024)) -1024; //us
	tsf = pmlmeext->TSFValue - rtw_modular64(pmlmeext->TSFValue, (pmlmeinfo->bcn_interval*1024)) -1024; //us

	if(((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) || ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE))
	{				
		//pHalData->RegTxPause |= STOP_BCNQ;BIT(6)
		//rtw_write8(Adapter, REG_TXPAUSE, (rtw_read8(Adapter, REG_TXPAUSE)|BIT(6)));
		StopTxBeacon(Adapter);
	}

	if(Adapter->iface_type == IFACE_PORT1)
	{
		//disable related TSF function
		rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1)&(~EN_BCN_FUNCTION));
							
		rtw_write32(Adapter, REG_TSFTR1, tsf);
		rtw_write32(Adapter, REG_TSFTR1+4, tsf>>32);


		//enable related TSF function
		rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1)|EN_BCN_FUNCTION);	

		// Update buddy port's TSF if it is SoftAP for beacon TX issue!
		if ( (pmlmeinfo->state&0x03) == WIFI_FW_STATION_STATE
			&& check_buddy_fwstate(Adapter, WIFI_AP_STATE)
		) { 
			//disable related TSF function
			rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)&(~EN_BCN_FUNCTION));

			rtw_write32(Adapter, REG_TSFTR, tsf);
			rtw_write32(Adapter, REG_TSFTR+4, tsf>>32);

			//enable related TSF function
			rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)|EN_BCN_FUNCTION);
#ifdef CONFIG_TSF_RESET_OFFLOAD
		// Update buddy port's TSF(TBTT) if it is SoftAP for beacon TX issue!
			if (reset_tsf(Adapter, IFACE_PORT0) == _FALSE)
				DBG_871X("ERROR! %s()-%d: Reset port0 TSF fail\n",
					__FUNCTION__, __LINE__);

#endif	// CONFIG_TSF_RESET_OFFLOAD	
		}		

		
	}
	else
	{
		//disable related TSF function
		rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)&(~EN_BCN_FUNCTION));
							
		rtw_write32(Adapter, REG_TSFTR, tsf);
		rtw_write32(Adapter, REG_TSFTR+4, tsf>>32);

		//enable related TSF function
		rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)|EN_BCN_FUNCTION);
		
		// Update buddy port's TSF if it is SoftAP for beacon TX issue!
		if ( (pmlmeinfo->state&0x03) == WIFI_FW_STATION_STATE
			&& check_buddy_fwstate(Adapter, WIFI_AP_STATE)
		) { 
			//disable related TSF function
			rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1)&(~EN_BCN_FUNCTION));

			rtw_write32(Adapter, REG_TSFTR1, tsf);
			rtw_write32(Adapter, REG_TSFTR1+4, tsf>>32);

			//enable related TSF function
			rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1)|EN_BCN_FUNCTION);
#ifdef CONFIG_TSF_RESET_OFFLOAD
		// Update buddy port's TSF if it is SoftAP for beacon TX issue!
			if (reset_tsf(Adapter, IFACE_PORT1) == _FALSE)
				DBG_871X("ERROR! %s()-%d: Reset port1 TSF fail\n",
					__FUNCTION__, __LINE__);
#endif	// CONFIG_TSF_RESET_OFFLOAD
		}		

	}
				
							
	if(((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) || ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE))
	{
		//pHalData->RegTxPause  &= (~STOP_BCNQ);
		//rtw_write8(Adapter, REG_TXPAUSE, (rtw_read8(Adapter, REG_TXPAUSE)&(~BIT(6))));
		ResumeTxBeacon(Adapter);
	}
#endif
}

static void hw_var_set_mlme_disconnect(PADAPTER Adapter, u8 variable, u8* val)
{
#ifdef CONFIG_CONCURRENT_MODE
				
	if(check_buddy_mlmeinfo_state(Adapter, _HW_STATE_NOLINK_))	
		rtw_write16(Adapter, REG_RXFLTMAP2, 0x00);
	

	if(Adapter->iface_type == IFACE_PORT1)
	{
		//reset TSF1
		rtw_write8(Adapter, REG_DUAL_TSF_RST, BIT(1));

		//disable update TSF1
		rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1)|DIS_TSF_UDT);

		// disable Port1's beacon function
		rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1)&(~BIT(3)));
	}
	else
	{
		//reset TSF
		rtw_write8(Adapter, REG_DUAL_TSF_RST, BIT(0));

		//disable update TSF
		rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)|DIS_TSF_UDT);
	}
#endif
}
static void hw_var_set_mlme_sitesurvey(PADAPTER Adapter, u8 variable, u8* val)
{
	u32	value_rcr, rcr_clear_bit, reg_bcn_ctl;
	u16	value_rxfltmap2;
	struct mlme_priv *pmlmepriv=&(Adapter->mlmepriv);

#ifdef CONFIG_CONCURRENT_MODE
	if(Adapter->iface_type == IFACE_PORT1)
		reg_bcn_ctl = REG_BCN_CTRL_1;
	else
#endif
		reg_bcn_ctl = REG_BCN_CTRL;

#ifdef CONFIG_FIND_BEST_CHANNEL

	rcr_clear_bit = (RCR_CBSSID_BCN | RCR_CBSSID_DATA);

	// Recieve all data frames
	value_rxfltmap2 = 0xFFFF;

#else /* CONFIG_FIND_BEST_CHANNEL */

	rcr_clear_bit = RCR_CBSSID_BCN;

	//config RCR to receive different BSSID & not to receive data frame
	value_rxfltmap2 = 0;

#endif /* CONFIG_FIND_BEST_CHANNEL */

	if( (check_fwstate(pmlmepriv, WIFI_AP_STATE) == _TRUE)
#ifdef CONFIG_CONCURRENT_MODE
		|| (check_buddy_fwstate(Adapter, WIFI_AP_STATE) == _TRUE)
#endif
		)
	{			
		rcr_clear_bit = RCR_CBSSID_BCN;
	}
#ifdef CONFIG_TDLS
	// TDLS will clear RCR_CBSSID_DATA bit for connection.
	else if (Adapter->tdlsinfo.link_established & _TRUE)
	{
		rcr_clear_bit = RCR_CBSSID_BCN;
	}
#endif // CONFIG_TDLS

	value_rcr = rtw_read32(Adapter, REG_RCR);

	if(*((u8 *)val))//under sitesurvey
	{
		value_rcr &= ~(rcr_clear_bit);
		rtw_write32(Adapter, REG_RCR, value_rcr);

		rtw_write16(Adapter, REG_RXFLTMAP2, value_rxfltmap2);

		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE | WIFI_ADHOC_STATE |WIFI_ADHOC_MASTER_STATE)) {
			//disable update TSF
			rtw_write8(Adapter, reg_bcn_ctl, rtw_read8(Adapter, reg_bcn_ctl)|DIS_TSF_UDT);
		}

		// Save orignal RRSR setting.for Dual band patch
		//pHalData->RegRRSR = rtw_read16(Adapter, REG_RRSR);

#ifdef CONFIG_CONCURRENT_MODE
		if(check_buddy_mlmeinfo_state(Adapter, WIFI_FW_AP_STATE) &&
			check_buddy_fwstate(Adapter, _FW_LINKED))
		{
			StopTxBeacon(Adapter);
		}
#endif
	}
	else//sitesurvey done
	{
		if(check_fwstate(pmlmepriv, (_FW_LINKED|WIFI_AP_STATE)) 
#ifdef CONFIG_CONCURRENT_MODE
			|| check_buddy_fwstate(Adapter, (_FW_LINKED|WIFI_AP_STATE))
#endif
			)
		{
			//enable to rx data frame
			rtw_write16(Adapter, REG_RXFLTMAP2,0xFFFF);
		}

		if (check_fwstate(pmlmepriv, WIFI_STATION_STATE | WIFI_ADHOC_STATE |WIFI_ADHOC_MASTER_STATE)) {
			//enable update TSF
			rtw_write8(Adapter, reg_bcn_ctl, rtw_read8(Adapter, reg_bcn_ctl)&(~(DIS_TSF_UDT)));
		}

		value_rcr |= rcr_clear_bit;
		rtw_write32(Adapter, REG_RCR, value_rcr);

		// Restore orignal RRSR setting.
		//rtw_write16(Adapter, REG_RRSR, pHalData->RegRRSR);

#ifdef CONFIG_CONCURRENT_MODE
		if(check_buddy_mlmeinfo_state(Adapter, WIFI_FW_AP_STATE) &&
			check_buddy_fwstate(Adapter, _FW_LINKED))
		{
			ResumeTxBeacon(Adapter);			
		}
#endif
	}		
}
static void hw_var_set_mlme_join(PADAPTER Adapter, u8 variable, u8* val)
{
#ifdef CONFIG_CONCURRENT_MODE
	u8	RetryLimit = 0x30;
	u8	type = *((u8 *)val);
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);
	struct mlme_priv	*pmlmepriv = &Adapter->mlmepriv;
	EEPROM_EFUSE_PRIV	*pEEPROM = GET_EEPROM_EFUSE_PRIV(Adapter);

	if(type == 0) // prepare to join
	{		
		if(check_buddy_mlmeinfo_state(Adapter, WIFI_FW_AP_STATE) &&
			check_buddy_fwstate(Adapter, _FW_LINKED))		
		{
			StopTxBeacon(Adapter);
		}
	
		//enable to rx data frame.Accept all data frame
		//rtw_write32(padapter, REG_RCR, rtw_read32(padapter, REG_RCR)|RCR_ADF);
		rtw_write16(Adapter, REG_RXFLTMAP2,0xFFFF);

		if(check_buddy_mlmeinfo_state(Adapter, WIFI_FW_AP_STATE))
		{
			//rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)|RCR_CBSSID_BCN);
			u32 val32;
			//Check BSSID BCN, BSSID DATA only for station mode 
			val32 = rtw_read32(Adapter, REG_RCR);
			val32 &= ~(RCR_CBSSID_BCN|RCR_CBSSID_DATA);
			rtw_write32(Adapter, REG_RCR, val32);
		}	
		else
		{
			rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)|RCR_CBSSID_DATA|RCR_CBSSID_BCN);
		}	

		if(check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE)
		{
			RetryLimit = (pEEPROM->CustomerID == RT_CID_CCX) ? 7 : 48;
		}
		else // Ad-hoc Mode
		{
			RetryLimit = 0x7;
		}
	}
	else if(type == 1) //joinbss_event call back when join res < 0
	{		
		if(check_buddy_mlmeinfo_state(Adapter, _HW_STATE_NOLINK_))		
			rtw_write16(Adapter, REG_RXFLTMAP2,0x00);

		if(check_buddy_mlmeinfo_state(Adapter, WIFI_FW_AP_STATE) &&
			check_buddy_fwstate(Adapter, _FW_LINKED))
		{
			ResumeTxBeacon(Adapter);			
			
			//reset TSF 1/2 after ResumeTxBeacon
			rtw_write8(Adapter, REG_DUAL_TSF_RST, BIT(1)|BIT(0));	
			
		}
	}
	else if(type == 2) //sta add event call back
	{
	 
		//enable update TSF
		if(Adapter->iface_type == IFACE_PORT1)
			rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1)&(~DIS_TSF_UDT));
		else
			rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)&(~DIS_TSF_UDT));
		 
	
		if(check_fwstate(pmlmepriv, WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE))
		{
			//fixed beacon issue for 8191su...........
			rtw_write8(Adapter,0x542 ,0x02);
			RetryLimit = 0x7;
		}


		if(check_buddy_mlmeinfo_state(Adapter, WIFI_FW_AP_STATE) &&
			check_buddy_fwstate(Adapter, _FW_LINKED))
		{
			ResumeTxBeacon(Adapter);			
			
			//reset TSF 1/2 after ResumeTxBeacon
			rtw_write8(Adapter, REG_DUAL_TSF_RST, BIT(1)|BIT(0));
		}
		
	}

	rtw_write16(Adapter, REG_RL, RetryLimit << RETRY_LIMIT_SHORT_SHIFT | RetryLimit << RETRY_LIMIT_LONG_SHIFT);
	
#endif
}


/***********************************************************/
// RTL8192E-MAC Setting
VOID
_InitTxBufferBoundary_8192E(
	IN PADAPTER Adapter,
	IN u8 txpktbuf_bndy
	)
{	
	struct registry_priv *pregistrypriv = &Adapter->registrypriv;

	rtw_write8(Adapter, REG_BCNQ_BDNY, txpktbuf_bndy);	
	rtw_write8(Adapter, REG_MGQ_BDNY, txpktbuf_bndy);
	rtw_write8(Adapter, REG_WMAC_LBK_BF_HD, txpktbuf_bndy);
	rtw_write8(Adapter, REG_TRXFF_BNDY, txpktbuf_bndy);	
	rtw_write8(Adapter, REG_DWBCN0_CTRL_8192E+1, txpktbuf_bndy);//BCN_HEAD

#ifdef CONFIG_CONCURRENT_MODE
	rtw_write8(Adapter, REG_BCNQ1_BDNY, txpktbuf_bndy+8);
	rtw_write8(Adapter, REG_DWBCN1_CTRL_8192E+1, txpktbuf_bndy+8);//BCN1_HEAD
	// BIT1- BIT_SW_BCN_SEL_EN
	rtw_write8(Adapter, REG_DWBCN1_CTRL_8192E+2, rtw_read8(Adapter, REG_DWBCN1_CTRL_8192E+2)|BIT1);
#endif
}

VOID
_InitPageBoundary_8192E(
	IN  PADAPTER Adapter
	)
{
	//u2Byte 			rxff_bndy;
	//u2Byte			Offset;
	//BOOLEAN			bSupportRemoteWakeUp;
	
	//Adapter->HalFunc.GetHalDefVarHandler(Adapter, HAL_DEF_WOWLAN , &bSupportRemoteWakeUp);
	// RX Page Boundary
	//srand(static_cast<unsigned int>(time(NULL)) );
	
//	Offset = MAX_RX_DMA_BUFFER_SIZE_8812/256;
//	rxff_bndy = (Offset*256)-1;

	rtw_write16(Adapter, (REG_TRXFF_BNDY + 2), MAX_RX_DMA_BUFFER_SIZE_8192E-1);
}

VOID
_InitDriverInfoSize_8192E(
	IN  PADAPTER	Adapter,
	IN	u8		drvInfoSize
	)
{
	rtw_write8(Adapter,REG_RX_DRVINFO_SZ, drvInfoSize);
}

VOID _InitRxSetting_8192E(	IN	PADAPTER Adapter	)
{
	rtw_write32(Adapter, REG_MACID, 0x87654321);
	rtw_write32(Adapter, REG_MACID1, 0x87654321);
}

VOID _InitRDGSetting_8192E(PADAPTER Adapter)
{
	rtw_write8(Adapter,REG_RD_CTRL,0xFF);
	rtw_write16(Adapter, REG_RD_NAV_NXT, 0x200);
	rtw_write8(Adapter,REG_RD_RESP_PKT_TH,0x05);
}
void _InitID_8192E(IN  PADAPTER Adapter)
{
	hal_init_macaddr(Adapter);//set mac_address
}
VOID
_InitNetworkType_8192E(
	IN  PADAPTER Adapter
	)
{
	u32	value32;

	value32 = rtw_read32(Adapter, REG_CR);
	// TODO: use the other function to set network type
	value32 = (value32 & ~MASK_NETTYPE) | _NETTYPE(NT_LINK_AP);

	rtw_write32(Adapter, REG_CR, value32);
}
VOID
_InitWMACSetting_8192E(
	IN  PADAPTER Adapter
	)
{
	//u4Byte			value32;
	//u16			value16;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	//pHalData->ReceiveConfig = AAP | APM | AM | AB | APP_ICV | ADF | AMF | APP_FCS | HTC_LOC_CTRL | APP_MIC | APP_PHYSTS;
	//pHalData->ReceiveConfig = 
	//RCR_AAP | RCR_APM | RCR_AM | RCR_AB |RCR_CBSSID_DATA| RCR_CBSSID_BCN| RCR_APP_ICV | RCR_AMF | RCR_HTC_LOC_CTRL | RCR_APP_MIC | RCR_APP_PHYST_RXFF;	  
	// don't turn on AAP, it will allow all packets to driver
	pHalData->ReceiveConfig = RCR_APM | RCR_AM | RCR_AB |RCR_CBSSID_DATA| RCR_CBSSID_BCN| RCR_APP_ICV | RCR_AMF | RCR_HTC_LOC_CTRL | RCR_APP_MIC | RCR_APP_PHYST_RXFF;	  
#if (1 == RTL8192E_RX_PACKET_INCLUDE_CRC)
	pHalData->ReceiveConfig |= ACRC32;
#endif

	// some REG_RCR will be modified later by phy_ConfigMACWithHeaderFile()
	rtw_write32(Adapter, REG_RCR, pHalData->ReceiveConfig);

	// Accept all multicast address
	rtw_write32(Adapter, REG_MAR, 0xFFFFFFFF);
	rtw_write32(Adapter, REG_MAR + 4, 0xFFFFFFFF);


	// Accept all data frames
	//value16 = 0xFFFF;
	//rtw_write16(Adapter, REG_RXFLTMAP2, value16);

	// 2010.09.08 hpfan
	// Since ADF is removed from RCR, ps-poll will not be indicate to driver,
	// RxFilterMap should mask ps-poll to gurantee AP mode can rx ps-poll.
	//value16 = 0x400;
	//rtw_write16(Adapter, REG_RXFLTMAP1, value16);

	// Accept all management frames
	//value16 = 0xFFFF;
	//rtw_write16(Adapter, REG_RXFLTMAP0, value16);

	//enable RX_SHIFT bits
	//rtw_write8(Adapter, REG_TRXDMA_CTRL, rtw_read8(Adapter, REG_TRXDMA_CTRL)|BIT(1));	

}

VOID _InitAdaptiveCtrl_8192E(IN  PADAPTER Adapter)
{
	u16	value16;
	u32	value32;

	// Response Rate Set
	value32 = rtw_read32(Adapter, REG_RRSR);
	value32 &= ~RATE_BITMAP_ALL;
	value32 |= RATE_RRSR_CCK_ONLY_1M;
	rtw_write32(Adapter, REG_RRSR, value32);

	// CF-END Threshold
	//m_spIoBase->rtw_write8(REG_CFEND_TH, 0x1);

	// SIFS (used in NAV)
	value16 = _SPEC_SIFS_CCK(0x10) | _SPEC_SIFS_OFDM(0x10);
	rtw_write16(Adapter, REG_SPEC_SIFS, value16);

	// Retry Limit
	value16 = _LRL(0x30) | _SRL(0x30);
	rtw_write16(Adapter, REG_RL, value16);
	
	
}
VOID _InitEDCA_8192E( IN  PADAPTER Adapter)
{
	// Set Spec SIFS (used in NAV)
	rtw_write16(Adapter,REG_SPEC_SIFS, 0x100a);
	rtw_write16(Adapter,REG_MAC_SPEC_SIFS, 0x100a);

	// Set SIFS for CCK
	rtw_write16(Adapter,REG_SIFS_CTX, 0x100a);	

	// Set SIFS for OFDM
	rtw_write16(Adapter,REG_SIFS_TRX, 0x100a);

	// TXOP
	rtw_write32(Adapter, REG_EDCA_BE_PARAM, 0x005EA42B);
	rtw_write32(Adapter, REG_EDCA_BK_PARAM, 0x0000A44F);
	rtw_write32(Adapter, REG_EDCA_VI_PARAM, 0x005EA324);
	rtw_write32(Adapter, REG_EDCA_VO_PARAM, 0x002FA226);
}
VOID _InitRetryFunction_8192E(	IN  PADAPTER Adapter)
{
	u8	value8;
	
	value8 = rtw_read8(Adapter, REG_FWHW_TXQ_CTRL);
	value8 |= EN_AMPDU_RTY_NEW;
	rtw_write8(Adapter, REG_FWHW_TXQ_CTRL, value8);

	// Set ACK timeout
	rtw_write8(Adapter, REG_ACKTO, 0x40);  //masked by page for BCM IOT issue temporally
	//rtw_write8(Adapter, REG_ACKTO, 0x80);
}

VOID
_BeaconFunctionEnable(
	IN	PADAPTER		Adapter,
	IN	BOOLEAN			Enable,
	IN	BOOLEAN			Linked
	)
{
	rtw_write8(Adapter, REG_BCN_CTRL, (DIS_TSF_UDT | EN_BCN_FUNCTION));
	//SetBcnCtrlReg(Adapter, (BIT4 | BIT3 | BIT1), 0x00);
	//RT_TRACE(COMP_BEACON, DBG_LOUD, ("_BeaconFunctionEnable 0x550 0x%x\n", PlatformEFIORead1Byte(Adapter, 0x550)));			

	rtw_write8(Adapter, REG_RD_CTRL+1, 0x6F);	
}

VOID _InitBeaconParameters_8192E(IN  PADAPTER Adapter)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	rtw_write16(Adapter, REG_BCN_CTRL, 0x1010);

	// TODO: Remove these magic number
	rtw_write16(Adapter, REG_TBTT_PROHIBIT,0x6404);// ms
	rtw_write8(Adapter, REG_DRVERLYINT, DRIVER_EARLY_INT_TIME_8192E);// 5ms
	rtw_write8(Adapter, REG_BCNDMATIM, BCN_DMA_ATIME_INT_TIME_8192E); // 2ms

	// Suggested by designer timchen. Change beacon AIFS to the largest number
	// beacause test chip does not contension before sending beacon. by tynli. 2009.11.03
	rtw_write16(Adapter, REG_BCNTCFG, 0x660F);

	pHalData->RegBcnCtrlVal = rtw_read8(Adapter, REG_BCN_CTRL);
	pHalData->RegTxPause = rtw_read8(Adapter, REG_TXPAUSE); 
	pHalData->RegFwHwTxQCtrl = rtw_read8(Adapter, REG_FWHW_TXQ_CTRL+2);
	pHalData->RegReg542 = rtw_read8(Adapter, REG_TBTT_PROHIBIT+2);
	pHalData->RegCR_1 = rtw_read8(Adapter, REG_CR+1);
}

void SetBeaconRelatedRegisters8192E(PADAPTER padapter)
{
	u32	value32;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
	struct mlme_ext_priv	*pmlmeext = &(padapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
	u32 bcn_ctrl_reg 			= REG_BCN_CTRL;
	//reset TSF, enable update TSF, correcting TSF On Beacon 
	
	//REG_BCN_INTERVAL
	//REG_BCNDMATIM
	//REG_ATIMWND
	//REG_TBTT_PROHIBIT
	//REG_DRVERLYINT
	//REG_BCN_MAX_ERR	
	//REG_BCNTCFG //(0x510)
	//REG_DUAL_TSF_RST
	//REG_BCN_CTRL //(0x550) 

	//BCN interval
#ifdef CONFIG_CONCURRENT_MODE
       if (padapter->iface_type == IFACE_PORT1){
		bcn_ctrl_reg = REG_BCN_CTRL_1;
       }	   
#endif	

	#ifdef CONFIG_CONCURRENT_MODE
   	if (padapter->iface_type == IFACE_PORT1){
		rtw_write16(padapter, REG_BCN_INTERVAL+2, pmlmeinfo->bcn_interval);//port 1 - BCN interval
	}
	else
	#endif
	{
		rtw_write16(padapter, REG_BCN_INTERVAL, pmlmeinfo->bcn_interval);
	}

	rtw_write8(padapter, REG_ATIMWND, 0x02);// 2ms

	_InitBeaconParameters_8192E(padapter);

	rtw_write8(padapter, REG_SLOT, 0x09);

	value32 =rtw_read32(padapter, REG_TCR); 
	value32 &= ~TSFRST;
	rtw_write32(padapter,  REG_TCR, value32); 

	value32 |= TSFRST;
	rtw_write32(padapter, REG_TCR, value32); 

	// NOTE: Fix test chip's bug (about contention windows's randomness)
	rtw_write8(padapter,  REG_RXTSF_OFFSET_CCK, 0x50);
	rtw_write8(padapter, REG_RXTSF_OFFSET_OFDM, 0x50);

	_BeaconFunctionEnable(padapter, _TRUE, _TRUE);

	ResumeTxBeacon(padapter);

	//rtw_write8(padapter, 0x422, rtw_read8(padapter, 0x422)|BIT(6));
	
	//rtw_write8(padapter, 0x541, 0xff);

	//rtw_write8(padapter, 0x542, rtw_read8(padapter, 0x541)|BIT(0));

	rtw_write8(padapter, bcn_ctrl_reg, rtw_read8(padapter, bcn_ctrl_reg)|BIT(1));

}

VOID _InitBeaconMaxError_8192E(
	IN  PADAPTER	Adapter,
	IN	BOOLEAN		InfraMode
	)
{
#ifdef RTL8192CU_ADHOC_WORKAROUND_SETTING
	rtw_write8(Adapter, REG_BCN_MAX_ERR, 0xFF);	
#else
	//rtw_write8(Adapter, REG_BCN_MAX_ERR, (InfraMode ? 0xFF : 0x10));	
#endif
}
// Set CCK and OFDM Block "ON"
void _BBTurnOnBlock_8192E(PADAPTER padapter)
{
#if (DISABLE_BB_RF)
	return;
#endif

	PHY_SetBBReg(padapter, rFPGA0_RFMOD, bCCKEn, 0x1);
	PHY_SetBBReg(padapter, rFPGA0_RFMOD, bOFDMEn, 0x1);
}
VOID
hal_ReadRFType_8192E(
	IN	PADAPTER	Adapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

#if DISABLE_BB_RF
	pHalData->rf_chip = RF_PSEUDO_11N;
#else
	pHalData->rf_chip = RF_6052;
#endif

	pHalData->BandSet = BAND_ON_2_4G;

}

u8 Hal_CrystalAFEAdjust(_adapter * Adapter)
{
	u8 val8;
	u32 val32;
	// 40Mhz crystal source,MAC 0x28[2]=0
	val8 = rtw_read8(Adapter, REG_AFE_CTRL2_8192E);
	val8 &= 0xfb;
	rtw_write8(Adapter, REG_AFE_CTRL2_8192E, val8); 
	
	val32 = rtw_read32(Adapter, REG_AFE_CTRL4_8192E);
	val32 &= 0xfffffc7f;
	rtw_write32(Adapter, REG_AFE_CTRL4_8192E, val32); 
	 
	// 92E AFE parameter 
	//AFE PLL KVCO selection, MAC 0x28[6]=1
	val8 = rtw_read8(Adapter, REG_AFE_CTRL2_8192E);
	val8 &= 0xBF;
	rtw_write8(Adapter, REG_AFE_CTRL2_8192E, val8); 

	//AFE PLL KVCO selection, MAC 0x78[21]=0
	val32 = rtw_read32(Adapter, REG_AFE_CTRL4_8192E);
	val32 &= 0xffdfffff;
	rtw_write32(Adapter, REG_AFE_CTRL4_8192E, val32); 	

	return _SUCCESS;
}


// RTL8192E-MAC Setting
/***********************************************************/

void SetHwReg8192E(PADAPTER Adapter, u8 variable, u8* val)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct dm_priv	*pdmpriv = &pHalData->dmpriv;
	DM_ODM_T 		*podmpriv = &pHalData->odmpriv;
_func_enter_;

	switch(variable)
	{
		case HW_VAR_MEDIA_STATUS:
			{
				u8 val8, mode;
				mode = *val;
				val8 = rtw_read8(Adapter, MSR)&0x0c;
				val8 |= mode;
				rtw_write8(Adapter, MSR, val8);
			}
			break;
		case HW_VAR_MEDIA_STATUS1:
			{
				u8 val8, mode;
				mode = *val;		
				val8 = rtw_read8(Adapter, MSR)&0x03;
				val8 |= *((u8 *)val) <<2;
				rtw_write8(Adapter, MSR, val8);
			}
			break;
		case HW_VAR_SET_OPMODE:
			hw_var_set_opmode(Adapter, variable, val);
			break;
                case HW_VAR_MAC_ADDR:
			hw_var_set_macaddr(Adapter, variable, val);			
			break;
		case HW_VAR_BSSID:
			hw_var_set_bssid(Adapter, variable, val);
			break;
		case HW_VAR_BASIC_RATE:
			{
				u16			BrateCfg = 0;
				u8			RateIndex = 0;

				// 2007.01.16, by Emily
				// Select RRSR (in Legacy-OFDM and CCK)
				// For 8190, we select only 24M, 12M, 6M, 11M, 5.5M, 2M, and 1M from the Basic rate.
				// We do not use other rates.
				HalSetBrateCfg( Adapter, val, &BrateCfg );
				
				//2011.03.30 add by Luke Lee
				//CCK 2M ACK should be disabled for some BCM and Atheros AP IOT
				//because CCK 2M has poor TXEVM
				//CCK 5.5M & 11M ACK should be enabled for better performance

				pHalData->BasicRateSet = BrateCfg = (BrateCfg |0xd) & 0x15d;
				BrateCfg |= 0x01; // default enable 1M ACK rate
				
				DBG_8192C(FUNC_ADPT_FMT" HW_VAR_BASIC_RATE: BrateCfg(%#x)\n", FUNC_ADPT_ARG(Adapter),BrateCfg);
				
				// Set RRSR rate table.
				rtw_write8(Adapter, REG_RRSR, BrateCfg&0xff);
				rtw_write8(Adapter, REG_RRSR+1, (BrateCfg>>8)&0xff);
				rtw_write8(Adapter, REG_RRSR+2, rtw_read8(Adapter, REG_RRSR+2)&0xf0);
				
			}
			break;
		case HW_VAR_TXPAUSE:
			rtw_write8(Adapter, REG_TXPAUSE, *((u8 *)val));	
			break;
		case HW_VAR_BCN_FUNC:
			hw_var_set_bcn_func(Adapter, variable, val);
			break;
		case HW_VAR_CORRECT_TSF:
#ifdef CONFIG_CONCURRENT_MODE
			hw_var_set_correct_tsf(Adapter, variable, val);
#else			
			{
				u64	tsf;
				struct mlme_ext_priv	*pmlmeext = &Adapter->mlmeextpriv;
				struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

				//tsf = pmlmeext->TSFValue - ((u32)pmlmeext->TSFValue % (pmlmeinfo->bcn_interval*1024)) -1024; //us
				tsf = pmlmeext->TSFValue - rtw_modular64(pmlmeext->TSFValue, (pmlmeinfo->bcn_interval*1024)) -1024; //us

				if(((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) || ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE))
				{				
					//pHalData->RegTxPause |= STOP_BCNQ;BIT(6)
					//rtw_write8(Adapter, REG_TXPAUSE, (rtw_read8(Adapter, REG_TXPAUSE)|BIT(6)));
					StopTxBeacon(Adapter);
				}

				//disable related TSF function
				rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)&(~EN_BCN_FUNCTION));
							
				rtw_write32(Adapter, REG_TSFTR, tsf);
				rtw_write32(Adapter, REG_TSFTR+4, tsf>>32);

				//enable related TSF function
				rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)|EN_BCN_FUNCTION);
				
							
				if(((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) || ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE))
				{
					//pHalData->RegTxPause  &= (~STOP_BCNQ);
					//rtw_write8(Adapter, REG_TXPAUSE, (rtw_read8(Adapter, REG_TXPAUSE)&(~BIT(6))));
					ResumeTxBeacon(Adapter);
				}
			}
#endif
			break;
		case HW_VAR_CHECK_BSSID:
			if(*((u8 *)val))
			{ 
				rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)|RCR_CBSSID_DATA|RCR_CBSSID_BCN); 
			}
			else
			{
				u32	val32;

				val32 = rtw_read32(Adapter, REG_RCR);
 
				val32 &= ~(RCR_CBSSID_DATA | RCR_CBSSID_BCN);

				rtw_write32(Adapter, REG_RCR, val32);
			}
			break;
		case HW_VAR_MLME_DISCONNECT:
#ifdef CONFIG_CONCURRENT_MODE
			hw_var_set_mlme_disconnect(Adapter, variable, val);
#else
			{
				//Set RCR to not to receive data frame when NO LINK state
				//rtw_write32(Adapter, REG_RCR, rtw_read32(padapter, REG_RCR) & ~RCR_ADF);
				//reject all data frames
				rtw_write16(Adapter, REG_RXFLTMAP2,0x00);

				//reset TSF
				rtw_write8(Adapter, REG_DUAL_TSF_RST, (BIT(0)|BIT(1)));

				//disable update TSF
				rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)|DIS_TSF_UDT);	
			}
#endif
			break;
		case HW_VAR_MLME_SITESURVEY:
			hw_var_set_mlme_sitesurvey(Adapter, variable,  val);
			break;
		case HW_VAR_MLME_JOIN:
#ifdef CONFIG_CONCURRENT_MODE
			hw_var_set_mlme_join(Adapter, variable,  val);
#else
			{
				u8	RetryLimit = 0x30;
				u8	type = *((u8 *)val);
				struct mlme_priv	*pmlmepriv = &Adapter->mlmepriv;
				EEPROM_EFUSE_PRIV	*pEEPROM = GET_EEPROM_EFUSE_PRIV(Adapter);
				
				if(type == 0) // prepare to join
				{
					//enable to rx data frame.Accept all data frame
					//rtw_write32(padapter, REG_RCR, rtw_read32(padapter, REG_RCR)|RCR_ADF);
					rtw_write16(Adapter, REG_RXFLTMAP2,0xFFFF);

					if(Adapter->in_cta_test)
					{
						u32 v = rtw_read32(Adapter, REG_RCR);
						v &= ~(RCR_CBSSID_DATA | RCR_CBSSID_BCN );//| RCR_ADF
						rtw_write32(Adapter, REG_RCR, v);
					}
					else
					{
						rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)|RCR_CBSSID_DATA|RCR_CBSSID_BCN);
					}

					if(check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE)
					{
						RetryLimit = (pEEPROM->CustomerID == RT_CID_CCX) ? 7 : 48;
					}
					else // Ad-hoc Mode
					{
						RetryLimit = 0x7;
					}
				}
				else if(type == 1) //joinbss_event call back when join res < 0
				{
					rtw_write16(Adapter, REG_RXFLTMAP2,0x00);
				}
				else if(type == 2) //sta add event call back
				{
					//enable update TSF
					rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)&(~DIS_TSF_UDT));

					if(check_fwstate(pmlmepriv, WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE))
					{
						RetryLimit = 0x7;
					}
				}

				rtw_write16(Adapter, REG_RL, RetryLimit << RETRY_LIMIT_SHORT_SHIFT | RetryLimit << RETRY_LIMIT_LONG_SHIFT);
			}
#endif
			break;

		case HW_VAR_ON_RCR_AM:
                        rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)|RCR_AM);
                        DBG_871X("%s, %d, RCR= %x \n", __FUNCTION__,__LINE__, rtw_read32(Adapter, REG_RCR));
                        break;
						
              case HW_VAR_OFF_RCR_AM:
                        rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)& (~RCR_AM));
                        DBG_871X("%s, %d, RCR= %x \n", __FUNCTION__,__LINE__, rtw_read32(Adapter, REG_RCR));
                        break;

		case HW_VAR_BEACON_INTERVAL:
			rtw_write16(Adapter, REG_BCN_INTERVAL, *((u16 *)val));
#ifdef  CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
			{
				struct mlme_ext_priv	*pmlmeext = &Adapter->mlmeextpriv;
				struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);
				u16 bcn_interval = 	*((u16 *)val);
				if((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE){
					DBG_8192C("%s==> bcn_interval:%d, eraly_int:%d \n",__FUNCTION__,bcn_interval,bcn_interval>>1);
					rtw_write8(Adapter, REG_DRVERLYINT, bcn_interval>>1);// 50ms for sdio 
				}			
			}
#endif//CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT

			break;
		case HW_VAR_SLOT_TIME:
			{				
				rtw_write8(Adapter, REG_SLOT, val[0]);
			}
			break;
		case HW_VAR_RESP_SIFS:
			{
				struct mlme_ext_priv	*pmlmeext = &Adapter->mlmeextpriv;

				if((pmlmeext->cur_wireless_mode==WIRELESS_11G) ||
					(pmlmeext->cur_wireless_mode==WIRELESS_11BG))//WIRELESS_MODE_G){
				{
					val[0] = 0x0a;
					val[1] = 0x0a;
				}
				else{
					val[0] = 0x0e;
					val[1] = 0x0e;
				}	
				// SIFS for OFDM Data ACK
				PlatformEFIOWrite1Byte(Adapter, REG_SIFS_CTX_8192E+1, val[0]);
				// SIFS for OFDM consecutive tx like CTS data!
				PlatformEFIOWrite1Byte(Adapter, REG_SIFS_TRX_8192E+1, val[1]);
				
				PlatformEFIOWrite1Byte(Adapter,REG_SPEC_SIFS_8192E+1, val[0]);
				PlatformEFIOWrite1Byte(Adapter,REG_MAC_SPEC_SIFS_8192E+1, val[0]);

				//Revise SIFS setting due to Hardware register definition change.
				PlatformEFIOWrite1Byte(Adapter, REG_RESP_SIFS_OFDM_8192E+1, val[0]);
				PlatformEFIOWrite1Byte(Adapter, REG_RESP_SIFS_OFDM_8192E, val[0]);
		
			}
			/*{
				//SIFS_Timer = 0x0a0a0808;
				//RESP_SIFS for CCK
				
				rtw_write8(Adapter, REG_RESP_SIFS_CCK, val[0]); 	// SIFS_T2T_CCK (0x08)
				rtw_write8(Adapter, REG_RESP_SIFS_CCK+1, val[1]); 	// SIFS_R2T_CCK(0x08)
				//RESP_SIFS for OFDM
				rtw_write8(Adapter, REG_RESP_SIFS_OFDM, val[2]); //SIFS_T2T_OFDM (0x0a)
				rtw_write8(Adapter, REG_RESP_SIFS_OFDM+1, val[3]); //SIFS_R2T_OFDM(0x0a)

			}*/
			break;
		case HW_VAR_ACK_PREAMBLE:
			{
				u8	regTmp;
				u8	bShortPreamble = *( (PBOOLEAN)val );
				// Joseph marked out for Netgear 3500 TKIP channel 7 issue.(Temporarily)
				regTmp = (pHalData->nCur40MhzPrimeSC)<<5;
				rtw_write8(Adapter, REG_RRSR+2, regTmp);
				
				regTmp = BIT(4)|BIT(5);
				if(bShortPreamble)		
					regTmp |= BIT1;
				else
					regTmp &= (~BIT1);
				rtw_write8(Adapter, REG_TRXPTCL_CTL_8192E+2, regTmp);
			}
			break;
		case HW_VAR_SEC_CFG:
#ifdef CONFIG_CONCURRENT_MODE
			rtw_write8(Adapter, REG_SECCFG, 0x0c|BIT(5));// enable tx enc and rx dec engine, and no key search for MC/BC				
#else
			rtw_write8(Adapter, REG_SECCFG, *((u8 *)val));
#endif
			break;
		case HW_VAR_CAM_EMPTY_ENTRY:
			{
				u8	ucIndex = *((u8 *)val);
				u8	i;
				u32	ulCommand=0;
				u32	ulContent=0;
				u32	ulEncAlgo=CAM_AES;

				for(i=0;i<CAM_CONTENT_COUNT;i++)
				{
					// filled id in CAM config 2 byte
					if( i == 0)
					{
						ulContent |=(ucIndex & 0x03) | ((u16)(ulEncAlgo)<<2);
						//ulContent |= CAM_VALID;
					}
					else
					{
						ulContent = 0;
					}
					// polling bit, and No Write enable, and address
					ulCommand= CAM_CONTENT_COUNT*ucIndex+i;
					ulCommand= ulCommand | CAM_POLLINIG|CAM_WRITE;
					// write content 0 is equall to mark invalid
					rtw_write32(Adapter, WCAMI, ulContent);  //delay_ms(40);
					//RT_TRACE(COMP_SEC, DBG_LOUD, ("CAM_empty_entry(): WRITE A4: %lx \n",ulContent));
					rtw_write32(Adapter, RWCAM, ulCommand);  //delay_ms(40);
					//RT_TRACE(COMP_SEC, DBG_LOUD, ("CAM_empty_entry(): WRITE A0: %lx \n",ulCommand));
				}
			}
			break;
		case HW_VAR_CAM_INVALID_ALL:
			rtw_write32(Adapter, RWCAM, BIT(31)|BIT(30));
			break;
		case HW_VAR_CAM_WRITE:
			{
				u32	cmd;
				u32	*cam_val = (u32 *)val;
				rtw_write32(Adapter, WCAMI, cam_val[0]);
				
				cmd = CAM_POLLINIG | CAM_WRITE | cam_val[1];
				rtw_write32(Adapter, RWCAM, cmd);
			}
			break;
		case HW_VAR_CAM_READ:
			{
			}
			break;
		case HW_VAR_AC_PARAM_VO:
			rtw_write32(Adapter, REG_EDCA_VO_PARAM, ((u32 *)(val))[0]);
			break;
		case HW_VAR_AC_PARAM_VI:
			rtw_write32(Adapter, REG_EDCA_VI_PARAM, ((u32 *)(val))[0]);
			break;
		case HW_VAR_AC_PARAM_BE:
			pHalData->AcParam_BE = ((u32 *)(val))[0];
			rtw_write32(Adapter, REG_EDCA_BE_PARAM, ((u32 *)(val))[0]);
			break;
		case HW_VAR_AC_PARAM_BK:
			rtw_write32(Adapter, REG_EDCA_BK_PARAM, ((u32 *)(val))[0]);
			break;
		case HW_VAR_ACM_CTRL:
			{
				u8	acm_ctrl = *((u8 *)val);
				u8	AcmCtrl = rtw_read8( Adapter, REG_ACMHWCTRL);

				if(acm_ctrl > 1)
					AcmCtrl = AcmCtrl | 0x1;

				if(acm_ctrl & BIT(3))
					AcmCtrl |= AcmHw_VoqEn;
				else
					AcmCtrl &= (~AcmHw_VoqEn);

				if(acm_ctrl & BIT(2))
					AcmCtrl |= AcmHw_ViqEn;
				else
					AcmCtrl &= (~AcmHw_ViqEn);

				if(acm_ctrl & BIT(1))
					AcmCtrl |= AcmHw_BeqEn;
				else
					AcmCtrl &= (~AcmHw_BeqEn);

				DBG_871X("[HW_VAR_ACM_CTRL] Write 0x%X\n", AcmCtrl );
				rtw_write8(Adapter, REG_ACMHWCTRL, AcmCtrl );
			}
			break;
		case HW_VAR_AMPDU_MIN_SPACE:
			{
				u8	MinSpacingToSet;
				u8	SecMinSpace;

				MinSpacingToSet = *((u8 *)val);
				pHalData->AMPDUDensity = MinSpacingToSet;
				if(MinSpacingToSet <= 7)
				{
					switch(Adapter->securitypriv.dot11PrivacyAlgrthm)
					{
						case _NO_PRIVACY_:
						case _AES_:
							SecMinSpace = 0;
							break;

						case _WEP40_:
						case _WEP104_:
						case _TKIP_:
						case _TKIP_WTMIC_:
							SecMinSpace = 6;
							break;
						default:
							SecMinSpace = 7;
							break;
					}

					if(MinSpacingToSet < SecMinSpace){
						MinSpacingToSet = SecMinSpace;
					}

					//RT_TRACE(COMP_MLME, DBG_LOUD, ("Set HW_VAR_AMPDU_MIN_SPACE: %#x\n", Adapter->MgntInfo.MinSpaceCfg));
					//rtw_write8(Adapter, REG_AMPDU_MIN_SPACE, (rtw_read8(Adapter, REG_AMPDU_MIN_SPACE) & 0xf8) | MinSpacingToSet);
				}
			}
			break;
		case HW_VAR_AMPDU_FACTOR:
			{
				u32	AMPDULen =  (*(u8 *)val);
				if(AMPDULen < HT_AGG_SIZE_64K)
					AMPDULen = (0x2000 << (*(u8 *)val)) -1;
				else
					AMPDULen = 0xffff;
			
				rtw_write16(Adapter, REG_AMPDU_MAX_LENGTH_8192E, AMPDULen);
			}
			break;
	
		case HW_VAR_H2C_FW_PWRMODE:
			{
				u8	psmode = (*(u8 *)val);
			
				// Forece leave RF low power mode for 1T1R to prevent conficting setting in Fw power
				// saving sequence. 2010.06.07. Added by tynli. Suggested by SD3 yschang.
				if( (psmode != PS_MODE_ACTIVE) && (!IS_92C_SERIAL(pHalData->VersionID)))
				{
					ODM_RF_Saving(podmpriv, _TRUE);
				}
				rtl8192e_set_FwPwrMode_cmd(Adapter, psmode);
			}
			break;
		case HW_VAR_H2C_FW_JOINBSSRPT:
		    {
				u8	mstatus = (*(u8 *)val);
				rtl8192e_set_FwJoinBssReport_cmd(Adapter, mstatus);
			}
			break;
#ifdef CONFIG_P2P_PS
		case HW_VAR_H2C_FW_P2P_PS_OFFLOAD:
			{
				u8	p2p_ps_state = (*(u8 *)val);
				rtl8192e_set_p2p_ps_offload_cmd(Adapter, p2p_ps_state);
			}
			break;
#endif //CONFIG_P2P
#ifdef CONFIG_TDLS
		case HW_VAR_TDLS_WRCR:
			rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)&(~RCR_CBSSID_DATA ));
			break;
		case HW_VAR_TDLS_INIT_CH_SEN:
			{
				rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)&(~ RCR_CBSSID_DATA )&(~RCR_CBSSID_BCN ));
				rtw_write16(Adapter, REG_RXFLTMAP2,0xffff);

				//disable update TSF
				rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)|DIS_TSF_UDT);
			}
			break;
		case HW_VAR_TDLS_DONE_CH_SEN:
			{
				//enable update TSF
				rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)&(~ DIS_TSF_UDT));
				rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)|(RCR_CBSSID_BCN ));
			}
			break;
		case HW_VAR_TDLS_RS_RCR:
			rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)|(RCR_CBSSID_DATA));
			break;
#endif //CONFIG_TDLS
		case HW_VAR_INITIAL_GAIN:
			{				
				DIG_T	*pDigTable = &podmpriv->DM_DigTable;					
				u32 		rx_gain = ((u32 *)(val))[0];
		
				if(rx_gain == 0xff){//restore rx gain					
					ODM_Write_DIG(podmpriv,pDigTable->BackupIGValue);
				}
				else{
					pDigTable->BackupIGValue = pDigTable->CurIGValue;
					ODM_Write_DIG(podmpriv,rx_gain);
				}
			}
			break;
		case HW_VAR_TRIGGER_GPIO_0:
			//rtl8192cu_trigger_gpio_0(Adapter);
			break;
#ifdef CONFIG_BT_COEXIST
		case HW_VAR_BT_SET_COEXIST:
			{
				u8	bStart = (*(u8 *)val);
				rtl8812_set_dm_bt_coexist(Adapter, bStart);
			}
			break;
		case HW_VAR_BT_ISSUE_DELBA:
			{
				u8	dir = (*(u8 *)val);
				rtl8812_issue_delete_ba(Adapter, dir);
			}
			break;
#endif
#ifdef CONFIG_SW_ANTENNA_DIVERSITY

		case HW_VAR_ANTENNA_DIVERSITY_LINK:
			//odm_SwAntDivRestAfterLink8192C(Adapter);
			ODM_SwAntDivRestAfterLink(podmpriv);
			break;
#endif			
#ifdef CONFIG_ANTENNA_DIVERSITY
		case HW_VAR_ANTENNA_DIVERSITY_SELECT:
			{
				u8	Optimum_antenna = (*(u8 *)val);
				u8 	Ant ; 
				//switch antenna to Optimum_antenna
				//DBG_8192C("==> HW_VAR_ANTENNA_DIVERSITY_SELECT , Ant_(%s)\n",(Optimum_antenna==2)?"A":"B");
				if(pHalData->CurAntenna !=  Optimum_antenna)		
				{					
					Ant = (Optimum_antenna==2)?MAIN_ANT:AUX_ANT;
					ODM_UpdateRxIdleAnt(&pHalData->odmpriv, Ant);
					
					pHalData->CurAntenna = Optimum_antenna ;
					//DBG_8192C("==> HW_VAR_ANTENNA_DIVERSITY_SELECT , Ant_(%s)\n",(Optimum_antenna==2)?"A":"B");
				}
			}
			break;
#endif
		case HW_VAR_EFUSE_BYTES: // To set EFUE total used bytes, added by Roger, 2008.12.22.
			pHalData->EfuseUsedBytes = *((u16 *)val);			
			break;
		case HW_VAR_FIFO_CLEARN_UP:
			{				
				struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(Adapter);
				u8 trycnt = 100;	
				
				//pause tx
				rtw_write8(Adapter,REG_TXPAUSE,0xff);
			
				//keep sn
				Adapter->xmitpriv.nqos_ssn = rtw_read16(Adapter,REG_NQOS_SEQ);

				if(pwrpriv->bkeepfwalive != _TRUE)
				{
					//RX DMA stop
					rtw_write32(Adapter,REG_RXPKT_NUM,(rtw_read32(Adapter,REG_RXPKT_NUM)|RW_RELEASE_EN));
					do{
						if(!(rtw_read32(Adapter,REG_RXPKT_NUM)&RXDMA_IDLE))
							break;
					}while(trycnt--);
					if(trycnt ==0)
						DBG_8192C("Stop RX DMA failed...... \n");

					//RQPN Load 0
					rtw_write16(Adapter,REG_RQPN_NPQ,0x0);
					rtw_write32(Adapter,REG_RQPN,0x80000000);
					rtw_mdelay_os(10);
				}
			}
			break;
		case HW_VAR_CHECK_TXBUF:
#ifdef CONFIG_CONCURRENT_MODE				
			{
				int i;
				u8	RetryLimit = 0x01;
				
				rtw_write16(Adapter, REG_RL, RetryLimit << RETRY_LIMIT_SHORT_SHIFT | RetryLimit << RETRY_LIMIT_LONG_SHIFT);
		
				for(i=0;i<1000;i++)
				{
					if(rtw_read32(Adapter, 0x200) != rtw_read32(Adapter, 0x204))
					{
						//DBG_871X("packet in tx packet buffer - 0x204=%x, 0x200=%x (%d)\n", rtw_read32(Adapter, 0x204), rtw_read32(Adapter, 0x200), i);
						rtw_msleep_os(10);
					}
					else
					{
						DBG_871X("no packet in tx packet buffer (%d)\n", i);
						break;
					}
				}

				RetryLimit = 0x30;	
				rtw_write16(Adapter, REG_RL, RetryLimit << RETRY_LIMIT_SHORT_SHIFT | RetryLimit << RETRY_LIMIT_LONG_SHIFT);
		
			}
#endif
			break;
		case HW_VAR_H2C_MEDIA_STATUS_RPT:
			{				
				rtl8192e_set_FwMediaStatus_cmd(Adapter , (*(u16 *)val));
			}
			break;
		case HW_VAR_BCN_VALID:
			#ifdef CONFIG_CONCURRENT_MODE
			if(Adapter->iface_type == IFACE_PORT1)
			{
				rtw_write8(Adapter, REG_DWBCN1_CTRL_8192E+2, rtw_read8(Adapter, REG_DWBCN1_CTRL_8192E+2) | BIT0); 
			}
			else
			#endif
			{
				//BCN_VALID, BIT16 of REG_DWBCN0_CTRL_8192E = BIT0 of REG_DWBCN0_CTRL_8192E+2, write 1 to clear, Clear by sw
				rtw_write8(Adapter, REG_DWBCN0_CTRL_8192E+2, rtw_read8(Adapter, REG_DWBCN0_CTRL_8192E+2) | BIT0); 
			}
			break;
		case HW_VAR_DL_BCN_SEL:
			#ifdef CONFIG_CONCURRENT_MODE
			if(Adapter->iface_type == IFACE_PORT1)
			{
				//SW_BCN_SEL - Port1
				rtw_write8(Adapter, REG_DWBCN1_CTRL_8192E+2, rtw_read8(Adapter, REG_DWBCN1_CTRL_8192E+2)|BIT4);
			}
			else
			#endif
			{
				//SW_BCN_SEL - Port0
				rtw_write8(Adapter, REG_DWBCN1_CTRL_8192E+2, rtw_read8(Adapter, REG_DWBCN1_CTRL_8192E+2) & ~BIT4);	
			}

			break;
		case HW_VAR_DO_IQK:
			pHalData->bNeedIQK = _TRUE;
			break;
			
		case HW_VAR_APFM_ON_MAC:
			pHalData->bMacPwrCtrlOn = *val;
			DBG_8192C("%s: bMacPwrCtrlOn=%d\n", __FUNCTION__, pHalData->bMacPwrCtrlOn);
			break;
			
		default:
			SetHwReg(Adapter, variable, val);
			break;
	}

_func_exit_;
}


void GetHwReg8192E(PADAPTER Adapter, u8 variable, u8* val)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	DM_ODM_T 		*podmpriv = &pHalData->odmpriv;
_func_enter_;

	switch(variable)
	{
		case HW_VAR_TXPAUSE:
			val[0] = rtw_read8(Adapter, REG_TXPAUSE);
			break;
		case HW_VAR_BCN_VALID:
			#ifdef CONFIG_CONCURRENT_MODE
			if(Adapter->iface_type == IFACE_PORT1)
			{
				val[0] = (BIT0 & rtw_read8(Adapter, REG_DWBCN1_CTRL_8192E+2))?_TRUE:_FALSE;
			}
			else
			#endif
			{
				//BCN_VALID, BIT16 of REG_DWBCN0_CTRL_8192E = BIT0 of REG_DWBCN0_CTRL_8192E+2
				val[0] = (BIT0 & rtw_read8(Adapter, REG_DWBCN0_CTRL_8192E+2))?_TRUE:_FALSE;
			}
			break;
		case HW_VAR_FWLPS_RF_ON:
			{
				//When we halt NIC, we should check if FW LPS is leave.
				if(adapter_to_pwrctl(Adapter)->rf_pwrstate == rf_off)
				{
					// If it is in HW/SW Radio OFF or IPS state, we do not check Fw LPS Leave,
					// because Fw is unload.
					val[0] = _TRUE;
				}
				else
				{
					// refine by scott
					u32	valCR;
		                    		valCR = rtw_read8(Adapter, REG_CR);
		                   		valCR &= 0xC0;

		                    		if(valCR)
		                        			val[0] = _TRUE;
		                    		else
						val[0] = _FALSE;
				}
			}
			break;
#ifdef CONFIG_ANTENNA_DIVERSITY
		case HW_VAR_CURRENT_ANTENNA:
			val[0] = pHalData->CurAntenna;
			break;
#endif
		case HW_VAR_EFUSE_BYTES: // To get EFUE total used bytes, added by Roger, 2008.12.22.
			*((u16 *)(val)) = pHalData->EfuseUsedBytes;	
			break;
		case HW_VAR_CHK_HI_QUEUE_EMPTY:
			//*val = ((rtw_read32(Adapter, REG_HGQ_INFORMATION)&0x00007f00)==0) ? _TRUE:_FALSE;
			*val = (rtw_read16(Adapter, REG_TXPKT_EMPTY)&BIT(10)) ? _TRUE:_FALSE;
			break;
		case HW_VAR_APFM_ON_MAC:
			*val = pHalData->bMacPwrCtrlOn;
			break;
		case HW_VAR_SYS_CLKR:
			*val = rtw_read8(Adapter, REG_SYS_CLKR);
			break;
		default:
			GetHwReg(Adapter, variable, val);
			break;
	}

_func_exit_;
}
//
//	Description:
//		Change default setting of specified variable.
//
u8
SetHalDefVar8192E(
	IN	PADAPTER				Adapter,
	IN	HAL_DEF_VARIABLE		eVariable,
	IN	PVOID					pValue
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8			bResult = _SUCCESS;

	switch(eVariable)
	{
		default:
			bResult =SetHalDefVar(Adapter, eVariable, pValue);
			break;
	}

	return bResult;
}

//
//	Description: 
//		Query setting of specified variable.
//
u8
GetHalDefVar8192E(
	IN	PADAPTER				Adapter,
	IN	HAL_DEF_VARIABLE		eVariable,
	IN	PVOID					pValue
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8			bResult = _SUCCESS;

	switch(eVariable)
	{

		case HAL_DEF_IS_SUPPORT_ANT_DIV:
#ifdef CONFIG_ANTENNA_DIVERSITY
			*((u8 *)pValue) = (pHalData->AntDivCfg==0)?_FALSE:_TRUE;
#endif
			break;			
		case HAL_DEF_CURRENT_ANTENNA:
#ifdef CONFIG_ANTENNA_DIVERSITY
			*(( u8*)pValue) = pHalData->CurAntenna;			
#endif
			break;
		case HAL_DEF_DRVINFO_SZ:
			*(( u32*)pValue) = DRVINFO_SZ;
			break;
		case HAL_DEF_MAX_RECVBUF_SZ:
			*(( u32*)pValue) = MAX_RECVBUF_SZ;
			break;
		case HAL_DEF_RX_PACKET_OFFSET:
			*(( u32*)pValue) = RXDESC_SIZE + (DRVINFO_SZ*8);
			break;
		case HAL_DEF_TX_LDPC:
			if(IS_NORMAL_CHIP(pHalData->VersionID))
				*((PBOOLEAN)pValue) = _FALSE;
			else
				*((PBOOLEAN)pValue) = _FALSE;
			break;

		case HAL_DEF_RX_LDPC:
			if(IS_NORMAL_CHIP(pHalData->VersionID))
				*((PBOOLEAN)pValue) = _FALSE;
			else
				*((PBOOLEAN)pValue) = _FALSE;
			break;

		case HAL_DEF_TX_STBC:
			if (pHalData->rf_type == RF_2T2R)
				*(u8 *)pValue = 1;
			else
				*(u8 *)pValue = 0;
			break;

		case HAL_DEF_RX_STBC:
			*(u8 *)pValue = 1;
			break;

		case HAL_DEF_EXPLICIT_BEAMFORMER:
		case HAL_DEF_EXPLICIT_BEAMFORMEE:
			*((PBOOLEAN)pValue) = _TRUE;
			break;
			
             case HW_DEF_RA_INFO_DUMP:
			{
				u8 mac_id = *((u8*)pValue);
				u32 cmd;
				u32 ra_info1,ra_info2;				
				u32 rate_mask1,rate_mask2;
				u8 curr_tx_rate,curr_tx_sgi,hight_rate,lowest_rate;
				
				
				DBG_871X("============ RA status check  Mac_id:%d ===================\n",mac_id);
				cmd = 0x40000100 |mac_id;
				rtw_write32(Adapter,REG_HMEBOX_E2_E3_8192E,cmd);
				rtw_msleep_os(10);
				ra_info1 = rtw_read32(Adapter,REG_RSVD5_8192E);
				curr_tx_rate = ra_info1&0x7F;
				curr_tx_sgi = (ra_info1>>7)&0x01;
				DBG_8192C("[ ra_info1:0x%08x ] =>cur_tx_rate= %s,cur_sgi:%d, PWRSTS = 0x%02x  \n",
					ra_info1,						
					HDATA_RATE(curr_tx_rate),
					curr_tx_sgi,
					(ra_info1>>8)  & 0x07);
					
				cmd = 0x40000400 |mac_id;
				rtw_write32(Adapter,REG_HMEBOX_E2_E3_8192E,cmd);
				rtw_msleep_os(10);
				ra_info1 = rtw_read32(Adapter,REG_RSVD5_8192E);
				ra_info2 = rtw_read32(Adapter,REG_RSVD6_8192E);
				rate_mask1 = 	rtw_read32(Adapter,REG_RSVD7_8192E);
				rate_mask2 = 	rtw_read32(Adapter,REG_RSVD8_8192E);
				hight_rate = ra_info2&0xFF;
				lowest_rate = (ra_info2>>8)  & 0xFF;
				
				DBG_8192C("[ ra_info1:0x%08x ] =>RSSI = %d, BW_setting = 0x%02x, DISRA = 0x%02x, VHT_EN = 0x%02x  \n",
					ra_info1,						
					ra_info1&0xFF,
					(ra_info1>>8)  & 0xFF,
					(ra_info1>>16) & 0xFF,
					(ra_info1>>24) & 0xFF );
				
				DBG_8192C("[ ra_info2:0x%08x ] =>hight_rate= %s, lowest_rate= %s, SGI = 0x%02x, RateID = %d  \n",
					ra_info2,						
					HDATA_RATE(hight_rate),
					HDATA_RATE(lowest_rate),
					(ra_info2>>16) & 0xFF,
					(ra_info2>>24) & 0xFF );
				DBG_8192C("rate_mask2:0x%08x , rate_mask1:0x%08x \n",rate_mask2,rate_mask1);				
				
			}
			break;

		default:
			bResult = GetHalDefVar(Adapter, eVariable, pValue);
			break;
	}

	return bResult;
}



void rtl8192E_GetHalODMVar(	
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

void rtl8192E_SetHalODMVar(
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

void rtl8192e_start_thread(_adapter *padapter)
{
#ifdef CONFIG_SDIO_HCI
#ifndef CONFIG_SDIO_TX_TASKLET
	struct xmit_priv *xmitpriv = &padapter->xmitpriv;

	xmitpriv->SdioXmitThread = kernel_thread(rtl8192es_xmit_thread, padapter, CLONE_FS|CLONE_FILES);
	if (xmitpriv->SdioXmitThread < 0) {
		RT_TRACE(_module_hal_xmit_c_, _drv_err_, ("%s: start rtl8188es_xmit_thread FAIL!!\n", __FUNCTION__));
	}
#endif
#endif
}

void rtl8192e_stop_thread(_adapter *padapter)
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
void hal_notch_filter_8192E(_adapter *adapter, bool enable)
{
	if (enable) {
		DBG_871X("Enable notch filter\n");
		//rtw_write8(adapter, rOFDM0_RxDSP+1, rtw_read8(adapter, rOFDM0_RxDSP+1) | BIT1);
	} else {
		DBG_871X("Disable notch filter\n");
		//rtw_write8(adapter, rOFDM0_RxDSP+1, rtw_read8(adapter, rOFDM0_RxDSP+1) & ~BIT1);
	}
}


void
ReadChipVersion8192E(
	IN	PADAPTER	Adapter
	)
{
	u32	value32;
	HAL_VERSION		ChipVersion ;
	HAL_DATA_TYPE	*pHalData;
	u8	tmpvdr;
	pHalData = GET_HAL_DATA(Adapter);
	
	value32 = rtw_read32(Adapter, REG_SYS_CFG1_8192E);
	DBG_871X("ReadChipVersion192e 0xF0 = 0x%x \n", value32);

	_rtw_memset(&ChipVersion, 0,sizeof(HAL_VERSION) );
	ChipVersion.ICType = CHIP_8192E;
	ChipVersion.RFType = (value32 & RF_TYPE_ID)?RF_2T2R :RF_1T1R;	
	ChipVersion.ChipType = ((value32 & RTL_ID) ? TEST_CHIP : NORMAL_CHIP);
	
	tmpvdr = (value32 & EXT_VENDOR_ID) >> EXT_VENDOR_ID_SHIFT;
	
	if(tmpvdr == 0x00)
		ChipVersion.VendorType = CHIP_VENDOR_TSMC;
	else if(tmpvdr == 0x01)
		ChipVersion.VendorType = CHIP_VENDOR_SMIC;
	else if(tmpvdr == 0x02)
		ChipVersion.VendorType = CHIP_VENDOR_UMC;	 
	
	ChipVersion.CUTVersion = (value32 & CHIP_VER_RTL_MASK)>>CHIP_VER_RTL_SHIFT; // IC version (CUT)
	pHalData->MultiFunc = RT_MULTI_FUNC_NONE;

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
	
		
	DBG_871X("RF_Type is %x!!\n", pHalData->rf_type);

}
void UpdateHalRAMask8192E(PADAPTER padapter, u32 mac_id, u8 rssi_level)
{
	u32	mask,rate_bitmap;
	u8	shortGIrate = _FALSE;
	u8	arg[4] = {0};
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

#ifdef CONFIG_BT_COEXIST
	rate_bitmap = rtw_btcoex_GetRaMask(padapter);
	mask &= ~rate_bitmap;
#endif // CONFIG_BT_COEXIST

	arg[0] = mac_id;
	arg[1] = psta->raid;
	arg[2] = shortGIrate;
	arg[3] = psta->init_rate;

	rtl8192e_set_raid_cmd(padapter, mask, arg);
}
void rtl8192e_init_default_value(_adapter * padapter)
{
	PHAL_DATA_TYPE pHalData;
	struct pwrctrl_priv *pwrctrlpriv;
	struct dm_priv *pdmpriv;
	u8 i;

	pHalData = GET_HAL_DATA(padapter);
	pwrctrlpriv = adapter_to_pwrctl(padapter);
	pdmpriv = &pHalData->dmpriv;
			
	//init default value
	padapter->registrypriv.wireless_mode = WIRELESS_11BG_24N;
	pHalData->fw_ractrl = _FALSE;		
	if(!pwrctrlpriv->bkeepfwalive)
		pHalData->LastHMEBoxNum = 0;	

	//init dm default value
	pHalData->odmpriv.RFCalibrateInfo.bIQKInitialized = _FALSE;
	pHalData->odmpriv.RFCalibrateInfo.TM_Trigger = 0;//for IQK
	pHalData->pwrGroupCnt = 0;
	pHalData->PGMaxGroup= MAX_PG_GROUP;
	pHalData->odmpriv.RFCalibrateInfo.ThermalValue_HP_index = 0;
	for(i = 0; i < HP_THERMAL_NUM; i++)
		pHalData->odmpriv.RFCalibrateInfo.ThermalValue_HP[i] = 0;

	pHalData->IntrMask[0]	= (u32)(			\
								//IMR_ROK 		|
								//IMR_RDU			|
								//IMR_VODOK		|
								//IMR_VIDOK		|
								//IMR_BEDOK		|
								//IMR_BKDOK		|
								//IMR_MGNTDOK		|
								//IMR_HIGHDOK		|
								//IMR_CPWM		|
								//IMR_CPWM2		|
								//IMR_C2HCMD		|
								//IMR_HISR1_IND_INT	|
								//IMR_ATIMEND		|
								//IMR_BCNDMAINT_E	|
								//IMR_HSISR_IND_ON_INT	|
								//IMR_BCNDOK0		|
								//IMR_BCNDMAINT0	|
								//IMR_TSF_BIT32_TOGGLE	|
								//IMR_TXBCN0OK	|
								//IMR_TXBCN0ERR	|
								//IMR_GTINT3		|
								//IMR_GTINT4		|
								//IMR_TXCCK		|
								0);

	pHalData->IntrMask[1] 	= (u32)(\
								//IMR_RXFOVW		|
								//IMR_TXFOVW		|
								//IMR_RXERR		|
								//IMR_TXERR		|
								//IMR_ATIMEND_E	|
								//IMR_BCNDOK1		|
								//IMR_BCNDOK2		|
								//IMR_BCNDOK3		|
								//IMR_BCNDOK4		|
								//IMR_BCNDOK5		|
								//IMR_BCNDOK6		|
								//IMR_BCNDOK7		|
								//IMR_BCNDMAINT1	|
								//IMR_BCNDMAINT2	|
								//IMR_BCNDMAINT3	|
								//IMR_BCNDMAINT4	|
								//IMR_BCNDMAINT5	|
								//IMR_BCNDMAINT6	|
								//IMR_BCNDMAINT7	|
								0);
	pHalData->EfuseHal.fakeEfuseBank = 0;
	pHalData->EfuseHal.fakeEfuseUsedBytes = 0;
	_rtw_memset(pHalData->EfuseHal.fakeEfuseContent, 0xFF, EFUSE_MAX_HW_SIZE);
	_rtw_memset(pHalData->EfuseHal.fakeEfuseInitMap, 0xFF, EFUSE_MAX_MAP_LEN);
	_rtw_memset(pHalData->EfuseHal.fakeEfuseModifiedMap, 0xFF, EFUSE_MAX_MAP_LEN);
}

void rtl8192e_set_hal_ops(struct hal_ops *pHalFunc)
{
	pHalFunc->free_hal_data = &rtl8192e_free_hal_data;

	pHalFunc->dm_init = &rtl8192e_init_dm_priv;
	pHalFunc->dm_deinit = &rtl8192e_deinit_dm_priv;

	pHalFunc->read_chip_version = &ReadChipVersion8192E;

	pHalFunc->set_bwmode_handler = &PHY_SetBWMode8192E;
	pHalFunc->set_channel_handler = &PHY_SwChnl8192E;
	pHalFunc->set_chnl_bw_handler = &PHY_SetSwChnlBWMode8192E;

	pHalFunc->set_tx_power_level_handler = &PHY_SetTxPowerLevel8192E;
	pHalFunc->get_tx_power_level_handler = &PHY_GetTxPowerLevel8192E;

	pHalFunc->hal_dm_watchdog = &rtl8192e_HalDmWatchDog;

	pHalFunc->Add_RateATid = &rtl8192e_Add_RateATid;

	pHalFunc->run_thread= &rtl8192e_start_thread;
	pHalFunc->cancel_thread= &rtl8192e_stop_thread;

#ifdef CONFIG_ANTENNA_DIVERSITY
	pHalFunc->AntDivBeforeLinkHandler = &AntDivBeforeLink8192e;
	pHalFunc->AntDivCompareHandler = &AntDivCompare8192e;
#endif

	pHalFunc->read_bbreg = &PHY_QueryBBReg8192E;
	pHalFunc->write_bbreg = &PHY_SetBBReg8192E;
	pHalFunc->read_rfreg = &PHY_QueryRFReg8192E;
	pHalFunc->write_rfreg = &PHY_SetRFReg8192E;


	// Efuse related function
	pHalFunc->EfusePowerSwitch = &rtl8192E_EfusePowerSwitch;
	pHalFunc->ReadEFuse = &rtl8192E_ReadEFuse;
	pHalFunc->EFUSEGetEfuseDefinition = &rtl8192E_EFUSE_GetEfuseDefinition;
	pHalFunc->EfuseGetCurrentSize = &rtl8192E_EfuseGetCurrentSize;
	pHalFunc->Efuse_PgPacketRead = &rtl8192E_Efuse_PgPacketRead;
	pHalFunc->Efuse_PgPacketWrite = &rtl8192E_Efuse_PgPacketWrite;
	pHalFunc->Efuse_WordEnableDataWrite = &rtl8192E_Efuse_WordEnableDataWrite;

#ifdef DBG_CONFIG_ERROR_DETECT
	pHalFunc->sreset_init_value = &sreset_init_value;
	pHalFunc->sreset_reset_value = &sreset_reset_value;
	pHalFunc->silentreset = &sreset_reset;
	pHalFunc->sreset_xmit_status_check = &rtl8192e_sreset_xmit_status_check;
	pHalFunc->sreset_linked_status_check  = &rtl8192e_sreset_linked_status_check;
	pHalFunc->sreset_get_wifi_status  = &sreset_get_wifi_status;
	pHalFunc->sreset_inprogress= &sreset_inprogress;
#endif //DBG_CONFIG_ERROR_DETECT

	pHalFunc->GetHalODMVarHandler = &rtl8192E_GetHalODMVar;
	pHalFunc->SetHalODMVarHandler = &rtl8192E_SetHalODMVar;
	//pHalFunc->hal_notch_filter = &hal_notch_filter_rtl8192E;
	pHalFunc->UpdateRAMaskHandler = &UpdateHalRAMask8192E;	
	
}



