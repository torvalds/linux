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
#define _RTL8812A_HAL_INIT_C_

//#include <drv_types.h>
#include <rtl8812a_hal.h>


#if defined(CONFIG_IOL)
void iol_mode_enable(PADAPTER padapter, u8 enable)
{
	u8 reg_0xf0 = 0;
	
	if(enable)
	{
		//Enable initial offload
		reg_0xf0 = rtw_read8(padapter, REG_SYS_CFG);
		//DBG_871X("%s reg_0xf0:0x%02x, write 0x%02x\n", __FUNCTION__, reg_0xf0, reg_0xf0|IOL_ENABLE);
		rtw_write8(padapter, REG_SYS_CFG, reg_0xf0|SW_OFFLOAD_EN);

		_8051Reset8812(padapter);
	}
	else
	{
		//disable initial offload
		reg_0xf0 = rtw_read8(padapter, REG_SYS_CFG);
		//DBG_871X("%s reg_0xf0:0x%02x, write 0x%02x\n", __FUNCTION__, reg_0xf0, reg_0xf0& ~IOL_ENABLE);
		rtw_write8(padapter, REG_SYS_CFG, reg_0xf0 & ~SW_OFFLOAD_EN);
	}
}

s32 iol_execute(PADAPTER padapter, u8 control)
{
	s32 status = _FAIL;
	u8 reg_0x88 = 0;
	u32 start = 0, passing_time = 0;
	control = control&0x0f;
	
	reg_0x88 = rtw_read8(padapter, 0x88);
	//DBG_871X("%s reg_0x88:0x%02x, write 0x%02x\n", __FUNCTION__, reg_0x88, reg_0x88|control);
	rtw_write8(padapter, 0x88,  reg_0x88|control);

	start = rtw_get_current_time();
	while((reg_0x88=rtw_read8(padapter, 0x88)) & control
		&& (passing_time=rtw_get_passing_time_ms(start))<1000
	) {
		DBG_871X("%s polling reg_0x88:0x%02x\n", __FUNCTION__, reg_0x88);
		rtw_usleep_os(100);
	}

	reg_0x88 = rtw_read8(padapter, 0x88);
	status = (reg_0x88 & control)?_FAIL:_SUCCESS;
	if(reg_0x88 & control<<4)
		status = _FAIL;

	DBG_871X("%s in %u ms, reg_0x88:0x%02x\n", __FUNCTION__, passing_time, reg_0x88);
	
	return status;
}

s32 iol_InitLLTTable(
	PADAPTER padapter,
	u8 txpktbuf_bndy
	)
{
	//DBG_871X("%s txpktbuf_bndy:%u\n", __FUNCTION__, txpktbuf_bndy);
	rtw_write8(padapter, REG_TDECTRL+1, txpktbuf_bndy);
	return iol_execute(padapter, IOL_INIT_LLT);
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


	efuseTbl = (u8*)rtw_zmalloc(EFUSE_MAP_LEN_JAGUAR);
	if(efuseTbl == NULL)
	{
		DBG_871X("%s: alloc efuseTbl fail!\n", __FUNCTION__);
		goto exit;
	}

	eFuseWord= (u16 **)rtw_malloc2d(EFUSE_MAX_SECTION_JAGUAR, EFUSE_MAX_WORD_UNIT, sizeof(u16));
	if(eFuseWord == NULL)
	{
		DBG_871X("%s: alloc eFuseWord fail!\n", __FUNCTION__);
		goto exit;
	}

	// 0. Refresh efuse init map as all oxFF.
	for (i = 0; i < EFUSE_MAX_SECTION_JAGUAR; i++)
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
	while((rtemp8 != 0xFF) && (eFuse_Addr < EFUSE_REAL_CONTENT_LEN_JAGUAR))
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
				
				if(rtemp8 != 0xFF && (eFuse_Addr < EFUSE_REAL_CONTENT_LEN_JAGUAR))
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
		
		if(offset < EFUSE_MAX_SECTION_JAGUAR)
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
					

					if(eFuse_Addr >= EFUSE_REAL_CONTENT_LEN_JAGUAR) 
						break;

					//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Addr=%d", eFuse_Addr));
					rtemp8 = *(phymap+eFuse_Addr);
					eFuse_Addr++;
					//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Data=0x%x\n", *rtemp8)); 				
					
					efuse_utilized++;
					eFuseWord[offset][i] |= (((u2Byte)rtemp8 << 8) & 0xff00);

					if(eFuse_Addr >= EFUSE_REAL_CONTENT_LEN_JAGUAR) 
						break;
				}
				
				wren >>= 1;
				
			}
		}

		// Read next PG header
		rtemp8 = *(phymap+eFuse_Addr);
		//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Addr=%d rtemp 0x%x\n", eFuse_Addr, *rtemp8));
		
		if(rtemp8 != 0xFF && (eFuse_Addr < EFUSE_REAL_CONTENT_LEN_JAGUAR))
		{
			efuse_utilized++;
			eFuse_Addr++;
		}
	}

	//
	// 3. Collect 16 sections and 4 word unit into Efuse map.
	//
	for(i=0; i<EFUSE_MAX_SECTION_JAGUAR; i++)
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
	efuse_usage = (u1Byte)((efuse_utilized*100)/EFUSE_REAL_CONTENT_LEN_JAGUAR);
	//Adapter->HalFunc.SetHwRegHandler(Adapter, HW_VAR_EFUSE_BYTES, (u8 *)&efuse_utilized);

exit:
	if(efuseTbl)
		rtw_mfree(efuseTbl, EFUSE_MAP_LEN_JAGUAR);

	if(eFuseWord)
		rtw_mfree2d((void *)eFuseWord, EFUSE_MAX_SECTION_JAGUAR, EFUSE_MAX_WORD_UNIT, sizeof(u16));
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

			limit = len-2<limit?len-2:limit;

			DBG_871X("%s len:%u, lenbak:%u, aaa:%u, aaabak:%u\n", __FUNCTION__, len, lenbak, aaa, aaabak);

			_rtw_memcpy(pos, ((u8*)&lo32)+2, limit>=count+2?2:limit-count);
			count+=limit>=count+2?2:limit-count;
			pos=content+count;
			
		}
		else
		{
			_rtw_memcpy(pos, ((u8*)&lo32), limit>=count+4?4:limit-count);
			count+=limit>=count+4?4:limit-count;
			pos=content+count;
			

		}

		if(limit>count && len-2>count) {
			_rtw_memcpy(pos, (u8*)&hi32, limit>=count+4?4:limit-count);
			count+=limit>=count+4?4:limit-count;
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

s32 iol_read_efuse(
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

	status = iol_execute(padapter, IOL_READ_EFUSE_MAP);

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
#endif /* defined(CONFIG_IOL) */

//-------------------------------------------------------------------------
//
// LLT R/W/Init function
//
//-------------------------------------------------------------------------
s32
_LLTWrite_8812A(
	IN	PADAPTER	Adapter,
	IN	u32			address,
	IN	u32			data
	)
{
	u32	status = _SUCCESS;
	s32	count = 0;
	u32	value = _LLT_INIT_ADDR(address) | _LLT_INIT_DATA(data) | _LLT_OP(_LLT_WRITE_ACCESS);

	rtw_write32(Adapter, REG_LLT_INIT, value);

	//polling
	do {
		value = rtw_read32(Adapter, REG_LLT_INIT);
		if (_LLT_NO_ACTIVE == _LLT_OP_VALUE(value)) {
			break;
		}

		if (count > POLLING_LLT_THRESHOLD) {
			RT_TRACE(_module_hal_init_c_, _drv_err_, ("Failed to polling write LLT done at address %d!\n", address));
			status = _FAIL;
			break;
		}
	} while (++count);

	return status;
}

static u8
_LLTRead_8812A(
	IN	PADAPTER	Adapter,
	IN	u32			address
	)
{
	u32	count = 0;
	u32	value = _LLT_INIT_ADDR(address) | _LLT_OP(_LLT_READ_ACCESS);
	u16	LLTReg = REG_LLT_INIT;


	rtw_write32(Adapter, LLTReg, value);

	//polling and get value
	do {
		value = rtw_read32(Adapter, LLTReg);
		if (_LLT_NO_ACTIVE == _LLT_OP_VALUE(value)) {
			return (u8)value;
		}

		if (count > POLLING_LLT_THRESHOLD) {
			RT_TRACE(_module_hal_init_c_, _drv_err_, ("Failed to polling read LLT done at address %d!\n", address));
			break;
		}
	} while (++count);

	return 0xFF;
}

s32 InitLLTTable8812A(PADAPTER padapter, u8 txpktbuf_bndy)
{
	s32	status = _FAIL;
	u32	i;
	u32	Last_Entry_Of_TxPktBuf = LAST_ENTRY_OF_TX_PKT_BUFFER_8812;
	HAL_DATA_TYPE *pHalData	= GET_HAL_DATA(padapter);

#if defined(CONFIG_IOL_LLT)
	if(1 || rtw_IOL_applied(padapter))
	{
		iol_mode_enable(padapter, 1);
		status = iol_InitLLTTable(padapter, txpktbuf_bndy);	
		iol_mode_enable(padapter, 0);
	}
	else
#endif
	{
		for (i = 0; i < (txpktbuf_bndy - 1); i++) {
			status = _LLTWrite_8812A(padapter, i, i + 1);
			if (_SUCCESS != status) {
				return status;
			}
		}

		// end of list
		status = _LLTWrite_8812A(padapter, (txpktbuf_bndy - 1), 0xFF);
		if (_SUCCESS != status) {
			return status;
		}

		// Make the other pages as ring buffer
		// This ring buffer is used as beacon buffer if we config this MAC as two MAC transfer.
		// Otherwise used as local loopback buffer.
		for (i = txpktbuf_bndy; i < Last_Entry_Of_TxPktBuf; i++) {
			status = _LLTWrite_8812A(padapter, i, (i + 1));
			if (_SUCCESS != status) {
				return status;
			}
		}

		// Let last entry point to the start entry of ring buffer
		status = _LLTWrite_8812A(padapter, Last_Entry_Of_TxPktBuf, txpktbuf_bndy);
		if (_SUCCESS != status) {
			return status;
		}
	}

	return status;
}

BOOLEAN HalDetectPwrDownMode8812(PADAPTER Adapter)
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

static VOID
_FWDownloadEnable_8812(
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
#define MAX_REG_BOLCK_SIZE	196 
static int
_BlockWrite_8812(
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
_PageWrite_8812(
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

	return _BlockWrite_8812(padapter,buffer,size);
}

static VOID
_FillDummy_8812(
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
_WriteFW_8812(
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
		ret = _PageWrite_8812(padapter, page, bufferPtr+offset, MAX_DLFW_PAGE_SIZE);
		
		if(ret == _FAIL)
			goto exit;
	}
	if (remainSize) {
		offset = pageNums * MAX_DLFW_PAGE_SIZE;
		page = pageNums;
		ret = _PageWrite_8812(padapter, page, bufferPtr+offset, remainSize);
		
		if(ret == _FAIL)
			goto exit;

	}
	RT_TRACE(_module_hal_init_c_, _drv_info_, ("_WriteFW Done- for Normal chip.\n"));

exit:
	return ret;
}

void _8051Reset8812(PADAPTER padapter)
{
	u8 u1bTmp, u1bTmp2;

	// Reset MCU IO Wrapper- sugggest by SD1-Gimmy
	if(IS_HARDWARE_TYPE_8812(padapter))
	{
		u1bTmp2 = rtw_read8(padapter, REG_RSV_CTRL);
		rtw_write8(padapter, REG_RSV_CTRL, u1bTmp2&(~BIT1));
		u1bTmp2 = rtw_read8(padapter, REG_RSV_CTRL+1);
		rtw_write8(padapter, REG_RSV_CTRL+1, u1bTmp2&(~BIT3));
	}
	else if(IS_HARDWARE_TYPE_8821(padapter))
	{
		u1bTmp2 = rtw_read8(padapter, REG_RSV_CTRL);
		rtw_write8(padapter, REG_RSV_CTRL, u1bTmp2&(~BIT1));
		u1bTmp2 = rtw_read8(padapter, REG_RSV_CTRL+1);
		rtw_write8(padapter, REG_RSV_CTRL+1, u1bTmp2&(~BIT0));
	}

	u1bTmp = rtw_read8(padapter, REG_SYS_FUNC_EN+1);
	rtw_write8(padapter, REG_SYS_FUNC_EN+1, u1bTmp&(~BIT2));

	// Enable MCU IO Wrapper
	if(IS_HARDWARE_TYPE_8812(padapter))
	{
		u1bTmp2 = rtw_read8(padapter, REG_RSV_CTRL);
		rtw_write8(padapter, REG_RSV_CTRL, u1bTmp2&(~BIT1));
		u1bTmp2 = rtw_read8(padapter, REG_RSV_CTRL+1);
		rtw_write8(padapter, REG_RSV_CTRL+1, u1bTmp2 |(BIT3));
	}
	else if(IS_HARDWARE_TYPE_8821(padapter))
	{
		u1bTmp2 = rtw_read8(padapter, REG_RSV_CTRL);
		rtw_write8(padapter, REG_RSV_CTRL, u1bTmp2&(~BIT1));
		u1bTmp2 = rtw_read8(padapter, REG_RSV_CTRL+1);
		rtw_write8(padapter, REG_RSV_CTRL+1, u1bTmp2|(BIT0));
	}

	rtw_write8(padapter, REG_SYS_FUNC_EN+1, u1bTmp|(BIT2));

	DBG_871X("=====> _8051Reset8812(): 8051 reset success .\n");
}

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

	if (rtw_fwdl_test_trigger_chksum_fail())
		goto exit;

	ret = _SUCCESS;

exit:
	DBG_871X("%s: Checksum report %s! (%u, %dms), REG_MCUFWDL:0x%08x\n", __FUNCTION__
	, (ret==_SUCCESS)?"OK":"Fail", cnt, rtw_get_passing_time_ms(start), value32);

	return ret;
}

static s32 _FWFreeToGo8812(_adapter *adapter, u32 min_cnt, u32 timeout_ms)
{
	s32 ret = _FAIL;
	u32	value32;
	u32 start = rtw_get_current_time();
	u32 cnt = 0;

	value32 = rtw_read32(adapter, REG_MCUFWDL);
	value32 |= MCUFWDL_RDY;
	value32 &= ~WINTINI_RDY;
	rtw_write32(adapter, REG_MCUFWDL, value32);

	_8051Reset8812(adapter);

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

	if (rtw_fwdl_test_trigger_wintint_rdy_fail())
		goto exit;

	ret = _SUCCESS;

exit:
	DBG_871X("%s: Polling FW ready %s! (%u, %dms), REG_MCUFWDL:0x%08x\n", __FUNCTION__
		, (ret==_SUCCESS)?"OK":"Fail", cnt, rtw_get_passing_time_ms(start), value32);

	return ret;
}

#ifdef CONFIG_FILE_FWIMG
extern char *rtw_fw_file_path;
u8	FwBuffer8812[FW_SIZE_8812];
#ifdef CONFIG_MP_INCLUDED
extern char *rtw_fw_mp_bt_file_path;
#endif // CONFIG_MP_INCLUDED
u8 FwBuffer[FW_SIZE_8812];
#endif //CONFIG_FILE_FWIMG

s32
FirmwareDownload8812(
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
	PRT_FIRMWARE_8812	pFirmware = NULL;
	u8				*pFwHdr = NULL;
	u8				*pFirmwareBuf;
	u32				FirmwareLen;


	RT_TRACE(_module_hal_init_c_, _drv_info_, ("+%s\n", __FUNCTION__));
	pFirmware = (PRT_FIRMWARE_8812)rtw_zmalloc(sizeof(RT_FIRMWARE_8812));
	if(!pFirmware)
	{
		rtStatus = _FAIL;
		goto exit;
	}

	#ifdef CONFIG_FILE_FWIMG
	if(rtw_is_file_readable(rtw_fw_file_path) == _TRUE)
	{
		DBG_871X("%s accquire FW from file:%s\n", __FUNCTION__, rtw_fw_file_path);
		pFirmware->eFWSource = FW_SOURCE_IMG_FILE;
	}
	else
	#endif //CONFIG_FILE_FWIMG
	{
		DBG_871X("%s fw source from Header\n", __FUNCTION__);
		pFirmware->eFWSource = FW_SOURCE_HEADER_FILE;
	}

	switch(pFirmware->eFWSource)
	{
		case FW_SOURCE_IMG_FILE:
			#ifdef CONFIG_FILE_FWIMG
			rtStatus = rtw_retrive_from_file(rtw_fw_file_path, FwBuffer8812, FW_SIZE_8812);
			pFirmware->ulFwLength = rtStatus>=0?rtStatus:0;
			pFirmware->szFwBuffer = FwBuffer8812;
			#endif //CONFIG_FILE_FWIMG
			break;
		case FW_SOURCE_HEADER_FILE:
			#ifdef CONFIG_WOWLAN
			if (bUsedWoWLANFw) {
				ODM_ConfigFWWithHeaderFile(&pHalData->odmpriv, CONFIG_FW_WoWLAN, (u8 *)&(pFirmware->szFwBuffer), &(pFirmware->ulFwLength));
				DBG_871X("%s fw:%s, size: %d\n", __FUNCTION__, "WoWLAN", pFirmware->ulFwLength);
			} else
			#endif /* CONFIG_WOWLAN */
			#ifdef CONFIG_BT_COEXIST
			if (pHalData->EEPROMBluetoothCoexist == _TRUE) {
				ODM_ConfigFWWithHeaderFile(&pHalData->odmpriv, CONFIG_FW_BT, (u8 *)&(pFirmware->szFwBuffer), &(pFirmware->ulFwLength));
				DBG_871X("%s fw:%s, size: %d\n", __FUNCTION__, "NIC-BTCOEX", pFirmware->ulFwLength);
			} else
			#endif /* CONFIG_BT_COEXIST */
			{
				ODM_ConfigFWWithHeaderFile(&pHalData->odmpriv, CONFIG_FW_NIC, (u8 *)&(pFirmware->szFwBuffer), &(pFirmware->ulFwLength));
				DBG_871X("%s fw:%s, size: %d\n", __FUNCTION__, "NIC", pFirmware->ulFwLength);
			}
			break;
	}

	if (pFirmware->ulFwLength > FW_SIZE_8812) {
		rtStatus = _FAIL;
		DBG_871X_LEVEL(_drv_emerg_, "Firmware size:%u exceed %u\n", pFirmware->ulFwLength, FW_SIZE_8812);
		goto exit;
	}

	pFirmwareBuf = pFirmware->szFwBuffer;
	FirmwareLen = pFirmware->ulFwLength;
	pFwHdr = (u8 *)pFirmware->szFwBuffer;

	pHalData->FirmwareVersion =  (u16)GET_FIRMWARE_HDR_VERSION_8812(pFwHdr);
	pHalData->FirmwareSubVersion = (u16)GET_FIRMWARE_HDR_SUB_VER_8812(pFwHdr);
	pHalData->FirmwareSignature = (u16)GET_FIRMWARE_HDR_SIGNATURE_8812(pFwHdr);

	DBG_871X ("%s: fw_ver=%d fw_subver=%d sig=0x%x\n",
		  __FUNCTION__, pHalData->FirmwareVersion, pHalData->FirmwareSubVersion, pHalData->FirmwareSignature);

	if (IS_FW_HEADER_EXIST_8812(pFwHdr) || IS_FW_HEADER_EXIST_8821(pFwHdr))
	{
		// Shift 32 bytes for FW header
		pFirmwareBuf = pFirmwareBuf + 32;
		FirmwareLen = FirmwareLen - 32;
	}

	// Suggested by Filen. If 8051 is running in RAM code, driver should inform Fw to reset by itself,
	// or it will cause download Fw fail. 2010.02.01. by tynli.
	if (rtw_read8(Adapter, REG_MCUFWDL) & BIT7) //8051 RAM code
	{
		rtw_write8(Adapter, REG_MCUFWDL, 0x00);
		_8051Reset8812(Adapter);		
	}

	_FWDownloadEnable_8812(Adapter, _TRUE);
	fwdl_start_time = rtw_get_current_time();
	while(!Adapter->bDriverStopped && !Adapter->bSurpriseRemoved
			&& (write_fw++ < 3 || rtw_get_passing_time_ms(fwdl_start_time) < 500))
	{
		/* reset FWDL chksum */
		rtw_write8(Adapter, REG_MCUFWDL, rtw_read8(Adapter, REG_MCUFWDL)|FWDL_ChkSum_rpt);
		
		rtStatus = _WriteFW_8812(Adapter, pFirmwareBuf, FirmwareLen);
		if (rtStatus != _SUCCESS)
			continue;

		rtStatus = polling_fwdl_chksum(Adapter, 5, 50);
		if (rtStatus == _SUCCESS)
			break;
	}
	_FWDownloadEnable_8812(Adapter, _FALSE);
	if(_SUCCESS != rtStatus)
		goto fwdl_stat;

	rtStatus = _FWFreeToGo8812(Adapter, 10, 200);
	if (_SUCCESS != rtStatus)
		goto fwdl_stat;

	if(IS_HARDWARE_TYPE_8821(Adapter))
	{
		//to do download BT
	}

fwdl_stat:
	DBG_871X("FWDL %s. write_fw:%u, %dms\n"
		, (rtStatus == _SUCCESS)?"success":"fail"
		, write_fw
		, rtw_get_passing_time_ms(fwdl_start_time)
	);

exit:
	if (pFirmware)
		rtw_mfree((u8*)pFirmware, sizeof(RT_FIRMWARE_8812));

#ifdef CONFIG_WOWLAN
	if (adapter_to_pwrctl(Adapter)->wowlan_mode)
		InitializeFirmwareVars8812(Adapter);	
	else
		DBG_871X_LEVEL(_drv_always_, "%s: wowland_mode:%d wowlan_wake_reason:%d\n", 
			__func__, adapter_to_pwrctl(Adapter)->wowlan_mode, 
			adapter_to_pwrctl(Adapter)->wowlan_wake_reason);
#endif

	return rtStatus;
}


void InitializeFirmwareVars8812(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);

	// Init Fw LPS related.
	pwrpriv->bFwCurrentInPSMode = _FALSE;
	// Init H2C counter. by tynli. 2009.12.09.
	pHalData->LastHMEBoxNum = 0;
}


//
// Description: Determine the contents of H2C BT_FW_PATCH Command sent to FW.
// 2013.01.23 by tynli
// Porting from 8723B. 2013.04.01
//
VOID
SetFwBTFwPatchCmd_8821(
	IN PADAPTER Adapter,
	IN u2Byte		FwSize
	)
{
	u1Byte		u1BTFwPatchParm[6]={0};

	DBG_871X("SetFwBTFwPatchCmd_8821(): FwSize = %d\n", FwSize);

	//SET_8812_H2CCMD_BT_FW_PATCH_ENABLE(u1BTFwPatchParm, 1);
	SET_8812_H2CCMD_BT_FW_PATCH_SIZE(u1BTFwPatchParm, FwSize);
	SET_8812_H2CCMD_BT_FW_PATCH_ADDR0(u1BTFwPatchParm, 0);
	SET_8812_H2CCMD_BT_FW_PATCH_ADDR1(u1BTFwPatchParm, 0xa0);
	SET_8812_H2CCMD_BT_FW_PATCH_ADDR2(u1BTFwPatchParm, 0x10);
	SET_8812_H2CCMD_BT_FW_PATCH_ADDR3(u1BTFwPatchParm, 0x80);

	FillH2CCmd_8812(Adapter, H2C_8812_BT_FW_PATCH, 6 , u1BTFwPatchParm);
}


int _CheckWLANFwPatchBTFwReady_8821A( PADAPTER	Adapter )
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u4Byte	count=0;
	u1Byte	u1bTmp;
	int ret = _FAIL;
	
#if (DEV_BUS_TYPE == RT_SDIO_INTERFACE)
		u4Byte	txpktbuf_bndy;
#endif

	//---------------------------------------------------------
	// Check if BT FW patch procedure is ready.
	//---------------------------------------------------------
	do{
		u1bTmp = PlatformEFIORead1Byte(Adapter, REG_FW_DRV_MSG_8812);
		if((u1bTmp&BIT6) || (u1bTmp&BIT7))
		{	
			ret = _SUCCESS;
			break;
		}
		count++;
		RT_TRACE(_module_mp_, _drv_info_,("0x81=%x, wait for 50 ms (%d) times.\n",
					u1bTmp, count));
		rtw_msleep_os(50); // 50ms
	}while(!((u1bTmp&BIT6) || (u1bTmp&BIT7)) && count < 50);

	RT_TRACE(_module_mp_, _drv_notice_,("_CheckWLANFwPatchBTFwReady():"
				" Polling ready bit 0x88[6:7] for %d times.\n", count));

	if(count >= 50)
	{
		DBG_871X("_CheckWLANFwPatchBTFwReady():"
				" Polling ready bit 0x88[6:7] FAIL!!\n");
	}

	//---------------------------------------------------------
	// Reset beacon setting to the initial value.
	//---------------------------------------------------------
#if (DEV_BUS_TYPE == RT_SDIO_INTERFACE)
#if 0
		if(!Adapter->MgntInfo.bWiFiConfg)
		{
			txpktbuf_bndy = TX_PAGE_BOUNDARY_8821;
		}
		else
#endif
		{// for WMM
			txpktbuf_bndy = WMM_NORMAL_TX_PAGE_BOUNDARY_8821;
		}
		
		ret =	InitLLTTable8812A(Adapter, txpktbuf_bndy);
		if(_SUCCESS != ret){
			DBG_871X("_CheckWLANFwPatchBTFwReady_8821A(): Failed to init LLT!\n");
		}
	
		// Init Tx boundary.
		PlatformEFIOWrite1Byte(Adapter, REG_TDECTRL+1, (u1Byte)txpktbuf_bndy);
#endif

	SetBcnCtrlReg(Adapter, BIT3, 0);
	SetBcnCtrlReg(Adapter, 0, BIT4);
	
	PlatformEFIOWrite1Byte(Adapter, REG_FWHW_TXQ_CTRL+2, (pHalData->RegFwHwTxQCtrl|BIT6));
	pHalData->RegFwHwTxQCtrl |= BIT6;

	u1bTmp = PlatformEFIORead1Byte(Adapter, REG_CR+1);
	PlatformEFIOWrite1Byte(Adapter, REG_CR+1, (u1bTmp&(~BIT0)));
	
	return ret;
}


int _WriteBTFWtoTxPktBuf8812(
	PADAPTER		Adapter,
	PVOID			buffer,
	u4Byte			FwBufLen,
	u1Byte			times
	)
{
	int 		rtStatus = _SUCCESS;
	//u4Byte				value32;
	//u1Byte				numHQ, numLQ, numPubQ;//, txpktbuf_bndy;
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	//PMGNT_INFO		pMgntInfo = &(Adapter->MgntInfo);
	u1Byte				BcnValidReg;
	u1Byte				count=0, DLBcnCount=0;
	pu1Byte 			FwbufferPtr = (pu1Byte)buffer;
	//PRT_TCB			pTcb, ptempTcb;
	//PRT_TX_LOCAL_BUFFER pBuf;
	BOOLEAN 			bRecover=_FALSE;
	pu1Byte 			ReservedPagePacket = NULL;
	pu1Byte 			pGenBufReservedPagePacket = NULL;
	u4Byte				TotalPktLen,txpktbuf_bndy;
	//u1Byte				tmpReg422;
	//u1Byte				u1bTmp;
	u8			*pframe;
	struct xmit_priv	*pxmitpriv = &(Adapter->xmitpriv);
	struct xmit_frame	*pmgntframe;
	struct pkt_attrib	*pattrib;
	u8			txdesc_offset = TXDESC_OFFSET;
	u8			val8;

#if 1//(DEV_BUS_TYPE == RT_PCI_INTERFACE)
	TotalPktLen = FwBufLen;
#else
	TotalPktLen = FwBufLen+pHalData->HWDescHeadLength;
#endif
	if((TotalPktLen+TXDESC_OFFSET) > MAX_CMDBUF_SZ)
	{
		DBG_871X(" WARNING %s => Total packet len = %d over MAX_CMDBUF_SZ:%d \n"
			,__FUNCTION__,(TotalPktLen+TXDESC_OFFSET),MAX_CMDBUF_SZ);
		return _FAIL;
	}
	pGenBufReservedPagePacket = rtw_zmalloc(TotalPktLen);//GetGenTempBuffer (Adapter, TotalPktLen);
	if (!pGenBufReservedPagePacket)
		return _FAIL;

	ReservedPagePacket = (u1Byte *)pGenBufReservedPagePacket;

	_rtw_memset(ReservedPagePacket, 0, TotalPktLen);

#if 1//(DEV_BUS_TYPE == RT_PCI_INTERFACE)
	_rtw_memcpy(ReservedPagePacket, FwbufferPtr, FwBufLen);

#else
	PlatformMoveMemory(ReservedPagePacket+Adapter->HWDescHeadLength , FwbufferPtr, FwBufLen);
#endif

	//---------------------------------------------------------
	// 1. Pause BCN
	//---------------------------------------------------------
	//Set REG_CR bit 8. DMA beacon by SW.
#if 0//(DEV_BUS_TYPE == RT_PCI_INTERFACE)
	u1bTmp = PlatformEFIORead1Byte(Adapter, REG_CR+1);
	PlatformEFIOWrite1Byte(Adapter,  REG_CR+1, (u1bTmp|BIT0));
#else
	// Remove for temparaily because of the code on v2002 is not sync to MERGE_TMEP for USB/SDIO.
	// De not remove this part on MERGE_TEMP. by tynli.
	//pHalData->RegCR_1 |= (BIT0);
	//PlatformEFIOWrite1Byte(Adapter,  REG_CR+1, pHalData->RegCR_1);
#endif

	// Disable Hw protection for a time which revserd for Hw sending beacon.
	// Fix download reserved page packet fail that access collision with the protection time.
	// 2010.05.11. Added by tynli.
	val8 = rtw_read8(Adapter, REG_BCN_CTRL);
	val8 &= ~BIT(3);
	val8 |= BIT(4);
	rtw_write8(Adapter, REG_BCN_CTRL, val8);

#if 0//(DEV_BUS_TYPE == RT_PCI_INTERFACE)
	tmpReg422 = PlatformEFIORead1Byte(Adapter, REG_FWHW_TXQ_CTRL+2);
	if( tmpReg422&BIT6)
		bRecover = TRUE;
	PlatformEFIOWrite1Byte(Adapter, REG_FWHW_TXQ_CTRL+2,  tmpReg422&(~BIT6));
#else
	// Set FWHW_TXQ_CTRL 0x422[6]=0 to tell Hw the packet is not a real beacon frame.
	if(pHalData->RegFwHwTxQCtrl & BIT(6))
		bRecover=_TRUE;
	PlatformEFIOWrite1Byte(Adapter, REG_FWHW_TXQ_CTRL+2, (pHalData->RegFwHwTxQCtrl&(~BIT(6))));
	pHalData->RegFwHwTxQCtrl &= (~ BIT(6));
#endif

	//---------------------------------------------------------
	// 2. Adjust LLT table to an even boundary.
	//---------------------------------------------------------
#if 0//(DEV_BUS_TYPE == RT_SDIO_INTERFACE)
	txpktbuf_bndy = 10; // rsvd page start address should be an even value. 														
	rtStatus =	InitLLTTable8723BS(Adapter, txpktbuf_bndy);
	if(RT_STATUS_SUCCESS != rtStatus){
		DBG_8192C("_CheckWLANFwPatchBTFwReady_8723B(): Failed to init LLT!\n");
		return RT_STATUS_FAILURE;
	}
	
	// Init Tx boundary.
	PlatformEFIOWrite1Byte(Adapter, REG_DWBCN0_CTRL_8723B+1, (u1Byte)txpktbuf_bndy);	
#endif


	//---------------------------------------------------------
	// 3. Write Fw to Tx packet buffer by reseverd page.
	//---------------------------------------------------------
	do
	{
		// download rsvd page.
		// Clear beacon valid check bit.
		BcnValidReg = PlatformEFIORead1Byte(Adapter, REG_TDECTRL+2);
		PlatformEFIOWrite1Byte(Adapter, REG_TDECTRL+2, BcnValidReg&(~BIT(0)));

		//BT patch is big, we should set 0x209 < 0x40 suggested from Gimmy
		RT_TRACE(_module_mp_, _drv_info_,("0x209:%x\n",
					PlatformEFIORead1Byte(Adapter, REG_TDECTRL+1)));//209 < 0x40

		PlatformEFIOWrite1Byte(Adapter, REG_TDECTRL+1, (0x90-0x20*(times-1)));
		DBG_871X("0x209:0x%x\n", PlatformEFIORead1Byte(Adapter, REG_TDECTRL+1));
		RT_TRACE(_module_mp_, _drv_info_,("0x209:%x\n",
					PlatformEFIORead1Byte(Adapter, REG_TDECTRL+1)));

#if 0
		// Acquice TX spin lock before GetFwBuf and send the packet to prevent system deadlock.
		// Advertised by Roger. Added by tynli. 2010.02.22.
		PlatformAcquireSpinLock(Adapter, RT_TX_SPINLOCK);
		if(MgntGetFWBuffer(Adapter, &pTcb, &pBuf))
		{
			PlatformMoveMemory(pBuf->Buffer.VirtualAddress, ReservedPagePacket, TotalPktLen);
			CmdSendPacket(Adapter, pTcb, pBuf, TotalPktLen, DESC_PACKET_TYPE_NORMAL, FALSE);
		}
		else
			dbgdump("SetFwRsvdPagePkt(): MgntGetFWBuffer FAIL!!!!!!!!.\n");
		PlatformReleaseSpinLock(Adapter, RT_TX_SPINLOCK);
#else
		/*---------------------------------------------------------
		tx reserved_page_packet
		----------------------------------------------------------*/
			if ((pmgntframe = rtw_alloc_cmdxmitframe(pxmitpriv)) == NULL) {
					rtStatus = _FAIL;
					goto exit;
			}
			//update attribute
			pattrib = &pmgntframe->attrib;
			update_mgntframe_attrib(Adapter, pattrib);

			pattrib->qsel = QSLT_BEACON;
			pattrib->pktlen = pattrib->last_txcmdsz = FwBufLen ;

			//_rtw_memset(pmgntframe->buf_addr, 0, TotalPktLen+txdesc_size);
			//pmgntframe->buf_addr = ReservedPagePacket ;

			_rtw_memcpy( (u8*) (pmgntframe->buf_addr + txdesc_offset), ReservedPagePacket, FwBufLen);
			DBG_871X("[%d]===>TotalPktLen + TXDESC_OFFSET TotalPacketLen:%d \n", DLBcnCount, (FwBufLen + txdesc_offset));

#ifdef CONFIG_PCI_HCI
			dump_mgntframe(Adapter, pmgntframe);
#else
			dump_mgntframe_and_wait(Adapter, pmgntframe, 100);
#endif

#endif
#if 1
		// check rsvd page download OK.
		BcnValidReg = PlatformEFIORead1Byte(Adapter, REG_TDECTRL+2);
		while(!(BcnValidReg & BIT(0)) && count <200)
		{
			count++;
			//PlatformSleepUs(10);
			rtw_msleep_os(1);
			BcnValidReg = PlatformEFIORead1Byte(Adapter, REG_TDECTRL+2);
			RT_TRACE(_module_mp_, _drv_notice_,("Poll 0x20A = %x\n", BcnValidReg));
		}
		DLBcnCount++;
		//DBG_871X("##0x208:%08x,0x210=%08x\n",PlatformEFIORead4Byte(Adapter, REG_TDECTRL),PlatformEFIORead4Byte(Adapter, 0x210));

		PlatformEFIOWrite1Byte(Adapter, REG_TDECTRL+2,BcnValidReg);
		
	}while((!(BcnValidReg&BIT(0))) && DLBcnCount<5);


#endif
	if(DLBcnCount >=5){
		DBG_871X(" check rsvd page download OK DLBcnCount =%d  \n",DLBcnCount);
		rtStatus = _FAIL;
		goto exit;
	}

	if(!(BcnValidReg&BIT(0)))
	{
		DBG_871X("_WriteFWtoTxPktBuf(): 1 Download RSVD page failed!\n");
		rtStatus = _FAIL;
		goto exit;
	}

	//---------------------------------------------------------
	// 4. Set Tx boundary to the initial value
	//---------------------------------------------------------


	//---------------------------------------------------------
	// 5. Reset beacon setting to the initial value.
	//	 After _CheckWLANFwPatchBTFwReady().
	//---------------------------------------------------------

exit:

	if(pGenBufReservedPagePacket)
	{
		DBG_871X("_WriteBTFWtoTxPktBuf8723B => rtw_mfree pGenBufReservedPagePacket!\n");
		rtw_mfree((u8*)pGenBufReservedPagePacket, TotalPktLen);
		}
	return rtStatus;
}


int ReservedPage_Compare(PADAPTER Adapter,PRT_MP_FIRMWARE pFirmware,u32 BTPatchSize)
{
	u8 temp,ret,lastBTsz;
	u32 u1bTmp=0,address_start=0,count=0,i=0;
	u8	*myBTFwBuffer = NULL;

	myBTFwBuffer = rtw_zmalloc(BTPatchSize);
	if (myBTFwBuffer == NULL)
	{
		DBG_871X("%s can't be executed due to the failed malloc.\n", __FUNCTION__);
		Adapter->mppriv.bTxBufCkFail=_TRUE;
		return _FALSE;
	}	
	
	temp=rtw_read8(Adapter,0x209);
	
	address_start=(temp*128)/8;
	
	rtw_write32(Adapter,0x140,0x00000000);
	rtw_write32(Adapter,0x144,0x00000000);
	rtw_write32(Adapter,0x148,0x00000000);

	rtw_write8(Adapter,0x106,0x69);

	
	for(i=0;i<(BTPatchSize/8);i++)
	{
		rtw_write32(Adapter,0x140,address_start+5+i) ;		  
			
		//polling until reg 0x140[23]=1;
		do{
			u1bTmp = rtw_read32(Adapter, 0x140);
			if(u1bTmp&BIT(23))
			{
				ret = _SUCCESS;
				break;
			}
			count++;
			DBG_871X("0x140=%x, wait for 10 ms (%d) times.\n",u1bTmp, count);
			rtw_msleep_os(10); // 10ms
		}while(!(u1bTmp&BIT(23)) && count < 50);
		
			myBTFwBuffer[i*8+0]=rtw_read8(Adapter, 0x144);
			myBTFwBuffer[i*8+1]=rtw_read8(Adapter, 0x145);
			myBTFwBuffer[i*8+2]=rtw_read8(Adapter, 0x146); 
			myBTFwBuffer[i*8+3]=rtw_read8(Adapter, 0x147);
			myBTFwBuffer[i*8+4]=rtw_read8(Adapter, 0x148);
			myBTFwBuffer[i*8+5]=rtw_read8(Adapter, 0x149);
			myBTFwBuffer[i*8+6]=rtw_read8(Adapter, 0x14a);
			myBTFwBuffer[i*8+7]=rtw_read8(Adapter, 0x14b);
	}
	
	rtw_write32(Adapter,0x140,address_start+5+BTPatchSize/8) ;			  

	lastBTsz=BTPatchSize%8;
	
	//polling until reg 0x140[23]=1;
	u1bTmp=0;
	count=0;
	do{
			u1bTmp = rtw_read32(Adapter, 0x140);
			if(u1bTmp&BIT(23))
			{
				ret = _SUCCESS;
				break;
			}
			count++;
			DBG_871X("0x140=%x, wait for 10 ms (%d) times.\n",u1bTmp, count);
			rtw_msleep_os(10); // 10ms
	}while(!(u1bTmp&BIT(23)) && count < 50);

	for(i=0;i<lastBTsz;i++)
	{
		myBTFwBuffer[(BTPatchSize/8)*8+i] = rtw_read8(Adapter, (0x144+i));

	}

	for(i=0;i<BTPatchSize;i++)
	{
		if(myBTFwBuffer[i]!= pFirmware->szFwBuffer[i])
		{
			DBG_871X(" In direct myBTFwBuffer[%d]=%x , pFirmware->szFwBuffer=%x\n",i, myBTFwBuffer[i],pFirmware->szFwBuffer[i]);
			Adapter->mppriv.bTxBufCkFail=_TRUE;
			break;
		}
	}

	if (myBTFwBuffer != NULL)
	{
		rtw_mfree(myBTFwBuffer, BTPatchSize);
	}
	
	return _TRUE;
}


#ifdef CONFIG_RTL8821A
s32 FirmwareDownloadBT(PADAPTER padapter, PRT_MP_FIRMWARE pFirmware)
{
	s32 rtStatus;
	u8 *pBTFirmwareBuf;
	u32 BTFirmwareLen;
	u8 download_time;
	s8 i;


	rtStatus = _SUCCESS;
	pBTFirmwareBuf = NULL;
	BTFirmwareLen = 0;

	//
	// Patch BT Fw. Download BT RAM code to Tx packet buffer.
	//
	if (padapter->bBTFWReady) {
		DBG_8192C("%s: BT Firmware is ready!!\n", __FUNCTION__);
		return _FAIL;
	}

#ifdef CONFIG_FILE_FWIMG
	if (rtw_is_file_readable(rtw_fw_mp_bt_file_path) == _TRUE)
	{
		DBG_8192C("%s: accquire MP BT FW from file:%s\n", __FUNCTION__, rtw_fw_mp_bt_file_path);

		rtStatus = rtw_retrive_from_file(rtw_fw_mp_bt_file_path, FwBuffer, 0x8000);
		BTFirmwareLen = rtStatus>=0?rtStatus:0;
		pBTFirmwareBuf = FwBuffer;
	}
	else
#endif // CONFIG_FILE_FWIMG
	{
#ifdef CONFIG_EMBEDDED_FWIMG
		DBG_8192C("%s: Download MP BT FW from header\n", __FUNCTION__);

		pBTFirmwareBuf = (u8*)Rtl8821A_BT_MP_Patch_FW;
		BTFirmwareLen = Rtl8812BFwBTImgArrayLength;
		pFirmware->szFwBuffer = pBTFirmwareBuf;
		pFirmware->ulFwLength = BTFirmwareLen;
#endif // CONFIG_EMBEDDED_FWIMG
	}

	DBG_8192C("%s: MP BT Firmware size=%d\n", __FUNCTION__, BTFirmwareLen);

	// for h2c cam here should be set to  true
	padapter->bFWReady = _TRUE;

	download_time = (BTFirmwareLen + 4095) / 4096;
	DBG_8192C("%s: download_time is %d\n", __FUNCTION__, download_time);

	// Download BT patch Fw.
	for (i = (download_time-1); i >= 0; i--)
	{
		if (i == (download_time - 1))
		{
			rtStatus = _WriteBTFWtoTxPktBuf8812(padapter, pBTFirmwareBuf+(4096*i), (BTFirmwareLen-(4096*i)), 1);
			DBG_8192C("%s: start %d, len %d, time 1\n", __FUNCTION__, 4096*i, BTFirmwareLen-(4096*i));
		}
		else
		{
			rtStatus = _WriteBTFWtoTxPktBuf8812(padapter, pBTFirmwareBuf+(4096*i), 4096, (download_time-i));
			DBG_8192C("%s: start %d, len 4096, time %d\n", __FUNCTION__, 4096*i, download_time-i);
		}

		if (rtStatus != _SUCCESS)
		{
			DBG_8192C("%s: BT Firmware download to Tx packet buffer fail!\n", __FUNCTION__);
			padapter->bBTFWReady = _FALSE;
			return rtStatus;
		}
	}

	ReservedPage_Compare(padapter,pFirmware,BTFirmwareLen);

	padapter->bBTFWReady = _TRUE;
	SetFwBTFwPatchCmd_8821(padapter, (u16)BTFirmwareLen);
	rtStatus = _CheckWLANFwPatchBTFwReady_8821A(padapter);

	DBG_8192C("<===%s: return %s!\n", __FUNCTION__, rtStatus==_SUCCESS?"SUCCESS":"FAIL");
	return rtStatus;
}
#endif

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
SetFwRelatedForWoWLAN8812(
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
	status = FirmwareDownload8812(padapter, bHostIsGoingtoSleep);
	if(status != _SUCCESS) {
		DBG_871X("SetFwRelatedForWoWLAN8812(): Re-Download Firmware failed!!\n");
		return;
	} else {
		DBG_871X("SetFwRelatedForWoWLAN8812(): Re-Download Firmware Success !!\n");
	}
	//
	// 2. Re-Init the variables about Fw related setting.
	//
	InitializeFirmwareVars8812(padapter);
}
#endif //CONFIG_WOWLAN

static void rtl8812_free_hal_data(PADAPTER padapter)
{
_func_enter_;
	
_func_exit_;
}

//===========================================================
//				Efuse related code
//===========================================================
BOOLEAN 
Hal_GetChnlGroup8812A(
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
			DBG_871X("==>mpt_GetChnlGroup8812A in 2.4 G, but Channel %d in Group not found \n", Channel);
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
			DBG_871X("==>mpt_GetChnlGroup8812A in 5G, but Channel %d in Group not found \n",Channel);
		}

	}
	//DBG_871X("<==mpt_GetChnlGroup8812A,  (%s) Channel = %d, Group =%d,\n", (bIn24G) ? "2.4G" : "5G", Channel, *pGroup);

	return bIn24G;
}

static void
hal_ReadPowerValueFromPROM8812A(
	IN	PADAPTER 		Adapter,
	IN	PTxPowerInfo24G	pwrInfo24G,
	IN	PTxPowerInfo5G	pwrInfo5G,
	IN	u8*				PROMContent,
	IN	BOOLEAN			AutoLoadFail
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u32 rfPath, eeAddr=EEPROM_TX_PWR_INX_8812, group,TxCount=0;
	
	_rtw_memset(pwrInfo24G, 0, sizeof(TxPowerInfo24G));
	_rtw_memset(pwrInfo5G, 0, sizeof(TxPowerInfo5G));

	//DBG_871X("hal_ReadPowerValueFromPROM8812A(): PROMContent[0x%x]=0x%x\n", (eeAddr+1), PROMContent[eeAddr+1]);
	if(0xFF == PROMContent[eeAddr+1])  //YJ,add,120316
		AutoLoadFail = _TRUE;

	if(AutoLoadFail)
	{
		DBG_871X("hal_ReadPowerValueFromPROM8812A(): Use Default value!\n");
		for(rfPath = 0 ; rfPath < MAX_RF_PATH ; rfPath++)
		{
			// 2.4G default value
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

			// 5G default value
			for(group = 0 ; group < MAX_CHNL_GROUP_5G; group++)
			{
				pwrInfo5G->IndexBW40_Base[rfPath][group] =		EEPROM_DEFAULT_5G_INDEX;
			}
			
			for(TxCount=0;TxCount<MAX_TX_COUNT;TxCount++)
			{
				if(TxCount==0)
				{
					pwrInfo5G->OFDM_Diff[rfPath][0] =	EEPROM_DEFAULT_5G_OFDM_DIFF;
					pwrInfo5G->BW20_Diff[rfPath][0] =	EEPROM_DEFAULT_5G_HT20_DIFF;
					pwrInfo5G->BW80_Diff[rfPath][0] =	EEPROM_DEFAULT_DIFF;
					pwrInfo5G->BW160_Diff[rfPath][0] =	EEPROM_DEFAULT_DIFF;
				}
				else
				{
					pwrInfo5G->OFDM_Diff[rfPath][TxCount] =	EEPROM_DEFAULT_DIFF;
					pwrInfo5G->BW20_Diff[rfPath][TxCount] =	EEPROM_DEFAULT_DIFF;
					pwrInfo5G->BW40_Diff[rfPath][TxCount]=	EEPROM_DEFAULT_DIFF;
					pwrInfo5G->BW80_Diff[rfPath][TxCount]=	EEPROM_DEFAULT_DIFF;
					pwrInfo5G->BW160_Diff[rfPath][TxCount] =	EEPROM_DEFAULT_DIFF;

				}
			}	
			
		}
		
		//pHalData->bNOPG = _TRUE;				
		return;
	}

	pHalData->bTXPowerDataReadFromEEPORM = _TRUE;		//YJ,move,120316

	for(rfPath = 0 ; rfPath < MAX_RF_PATH ; rfPath++)
	{
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

				{
					pwrInfo24G->BW20_Diff[rfPath][TxCount] =	(PROMContent[eeAddr]&0xf0)>>4;				
				 	if(pwrInfo24G->BW20_Diff[rfPath][TxCount] & BIT3)		//4bit sign number to 8 bit sign number
						pwrInfo24G->BW20_Diff[rfPath][TxCount] |= 0xF0;
				}
				//DBG_871X("8812-2G RF-%d-SS-%d BW20-Addr-%x DIFF=%d\n", 
				//rfPath, TxCount, eeAddr, pwrInfo24G->BW20_Diff[rfPath][TxCount]);

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

				{
					pwrInfo24G->BW40_Diff[rfPath][TxCount] =	(PROMContent[eeAddr]&0xf0)>>4;				
					if(pwrInfo24G->BW40_Diff[rfPath][TxCount] & BIT3)		//4bit sign number to 8 bit sign number
						pwrInfo24G->BW40_Diff[rfPath][TxCount] |= 0xF0;
				}
				//DBG_871X("8812-2G RF-%d-SS-%d BW40-Addr-%x DIFF=%d\n", 
				//rfPath, TxCount, eeAddr, pwrInfo24G->BW40_Diff[rfPath][TxCount]);


				{
					pwrInfo24G->BW20_Diff[rfPath][TxCount] =	(PROMContent[eeAddr]&0x0f);				
					if(pwrInfo24G->BW20_Diff[rfPath][TxCount] & BIT3)		//4bit sign number to 8 bit sign number
						pwrInfo24G->BW20_Diff[rfPath][TxCount] |= 0xF0;
				}
				//DBG_871X("8812-2G RF-%d-SS-%d BW20-Addr-%x DIFF=%d\n", 
				//rfPath, TxCount, eeAddr, pwrInfo24G->BW20_Diff[rfPath][TxCount]);

				eeAddr++;
				

				{
					pwrInfo24G->OFDM_Diff[rfPath][TxCount] =	(PROMContent[eeAddr]&0xf0)>>4;				
					if(pwrInfo24G->OFDM_Diff[rfPath][TxCount] & BIT3)		//4bit sign number to 8 bit sign number
						pwrInfo24G->OFDM_Diff[rfPath][TxCount] |= 0xF0;
				}
				//DBG_871X("8812-2G RF-%d-SS-%d LGOD-Addr-%x DIFF=%d\n", 
				//rfPath, TxCount, eeAddr, pwrInfo24G->BW20_Diff[rfPath][TxCount]);


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

		//5G default value
		for(group = 0 ; group < MAX_CHNL_GROUP_5G; group++)
		{
			pwrInfo5G->IndexBW40_Base[rfPath][group] =		PROMContent[eeAddr++];
			if(pwrInfo5G->IndexBW40_Base[rfPath][group] == 0xFF)
				pwrInfo5G->IndexBW40_Base[rfPath][group] = EEPROM_DEFAULT_DIFF;												

			//DBG_871X("8812-5G RF-%d-G-%d BW40-Addr-%x BASE=%x\n", 
			//	rfPath, TxCount, eeAddr-1, pwrInfo5G->IndexBW40_Base[rfPath][group]);
		}
		
		for(TxCount=0;TxCount<MAX_TX_COUNT;TxCount++)
		{
			if(TxCount==0)
			{
				pwrInfo5G->BW40_Diff[rfPath][TxCount]=	 0;				
			

				{
					pwrInfo5G->BW20_Diff[rfPath][0] =	(PROMContent[eeAddr]&0xf0)>>4;				
					if(pwrInfo5G->BW20_Diff[rfPath][TxCount] & BIT3)		//4bit sign number to 8 bit sign number
						pwrInfo5G->BW20_Diff[rfPath][TxCount] |= 0xF0;		
				}
				//DBG_871X("8812-5G RF-%d-SS-%d BW20-Addr-%x DIFF=%d\n", 
				//rfPath, TxCount, eeAddr, pwrInfo5G->BW20_Diff[rfPath][TxCount]);


				{
					pwrInfo5G->OFDM_Diff[rfPath][0] =	(PROMContent[eeAddr]&0x0f);				
					if(pwrInfo5G->OFDM_Diff[rfPath][TxCount] & BIT3)		//4bit sign number to 8 bit sign number
						pwrInfo5G->OFDM_Diff[rfPath][TxCount] |= 0xF0;								
				}
				//DBG_871X("8812-5G RF-%d-SS-%d LGOD-Addr-%x DIFF=%d\n", 
				//rfPath, TxCount, eeAddr, pwrInfo5G->OFDM_Diff[rfPath][TxCount]);

				eeAddr++;
			}
			else
			{

				{
					pwrInfo5G->BW40_Diff[rfPath][TxCount]=	 (PROMContent[eeAddr]&0xf0)>>4;					
					if(pwrInfo5G->BW40_Diff[rfPath][TxCount] & BIT3)		//4bit sign number to 8 bit sign number
					pwrInfo5G->BW40_Diff[rfPath][TxCount] |= 0xF0;	
				}
				//DBG_871X("8812-5G RF-%d-SS-%d BW40-Addr-%x DIFF=%d\n", 
				//rfPath, TxCount, eeAddr, pwrInfo5G->BW40_Diff[rfPath][TxCount]);
				

				{
					pwrInfo5G->BW20_Diff[rfPath][TxCount] = (PROMContent[eeAddr]&0x0f);				
					if(pwrInfo5G->BW20_Diff[rfPath][TxCount] & BIT3)		//4bit sign number to 8 bit sign number
					pwrInfo5G->BW20_Diff[rfPath][TxCount] |= 0xF0;					
				}
				//DBG_871X("8812-5G RF-%d-SS-%d BW20-Addr-%x DIFF=%d\n", 
				//rfPath, TxCount, eeAddr, pwrInfo5G->BW20_Diff[rfPath][TxCount]);
				
				eeAddr++;
			}
		}	


		{
			pwrInfo5G->OFDM_Diff[rfPath][1] =	(PROMContent[eeAddr]&0xf0)>>4;
			pwrInfo5G->OFDM_Diff[rfPath][2] =	(PROMContent[eeAddr]&0x0f);
		}
		//DBG_871X("8812-5G RF-%d-SS-%d LGOD-Addr-%x DIFF=%d\n", 
		//		rfPath, 2, eeAddr, pwrInfo5G->OFDM_Diff[rfPath][2]);
		eeAddr++;		


			pwrInfo5G->OFDM_Diff[rfPath][3] =	(PROMContent[eeAddr]&0x0f);

		//DBG_871X("8812-5G RF-%d-SS-%d LGOD-Addr-%x DIFF=%d\n", 
		//rfPath, 3, eeAddr, pwrInfo5G->OFDM_Diff[rfPath][3]);
		eeAddr++;
		
		for(TxCount=1;TxCount<MAX_TX_COUNT;TxCount++)
		{
			if(pwrInfo5G->OFDM_Diff[rfPath][TxCount] & BIT3)		//4bit sign number to 8 bit sign number
				pwrInfo5G->OFDM_Diff[rfPath][TxCount] |= 0xF0;			

			//DBG_871X("8812-5G RF-%d-SS-%d LGOD-Addr-%x DIFF=%d\n", 
			//rfPath, TxCount, eeAddr, pwrInfo5G->OFDM_Diff[rfPath][TxCount]);
		}	
		
		for(TxCount=0;TxCount<MAX_TX_COUNT;TxCount++)
		{		

			{
				pwrInfo5G->BW80_Diff[rfPath][TxCount] =	(PROMContent[eeAddr]&0xf0)>>4;			
				if(pwrInfo5G->BW80_Diff[rfPath][TxCount] & BIT3)		//4bit sign number to 8 bit sign number
					pwrInfo5G->BW80_Diff[rfPath][TxCount] |= 0xF0;			
			}
			//DBG_871X("8812-5G RF-%d-SS-%d BW80-Addr-%x DIFF=%d\n", 
			//rfPath, TxCount, eeAddr, pwrInfo5G->BW80_Diff[rfPath][TxCount]);
			

			{
				pwrInfo5G->BW160_Diff[rfPath][TxCount]=	(PROMContent[eeAddr]&0x0f);			
				if(pwrInfo5G->BW160_Diff[rfPath][TxCount] & BIT3)		//4bit sign number to 8 bit sign number
					pwrInfo5G->BW160_Diff[rfPath][TxCount] |= 0xF0;						
			}
			//DBG_871X("8812-5G RF-%d-SS-%d BW160-Addr-%x DIFF=%d\n", 
			//rfPath, TxCount, eeAddr, pwrInfo5G->BW160_Diff[rfPath][TxCount]);
			eeAddr++;
		}
	}

}

VOID
Hal_EfuseParseBTCoexistInfo8812A(
	IN PADAPTER			Adapter,
	IN u8				*hwinfo,
	IN BOOLEAN			AutoLoadFail
	)
{
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);
	u8 tmp_u8;
	u32 tmp_u32;

	if (IS_HARDWARE_TYPE_8812(Adapter)) {

		pHalData->EEPROMBluetoothType = BT_RTL8812A;

		if (!AutoLoadFail) {
			tmp_u8 = hwinfo[EEPROM_RF_BOARD_OPTION_8812];
			if( ((tmp_u8 & 0xe0)>>5) == 0x1)// [7:5]
				pHalData->EEPROMBluetoothCoexist = _TRUE;
			else
				pHalData->EEPROMBluetoothCoexist = _FALSE;
		
			tmp_u8 = hwinfo[EEPROM_RF_BT_SETTING_8812];
			pHalData->EEPROMBluetoothAntNum = (tmp_u8&0x1); // bit [0]
		} else {
			pHalData->EEPROMBluetoothCoexist = _FALSE;
			pHalData->EEPROMBluetoothAntNum = Ant_x1;
		}
	} else if (IS_HARDWARE_TYPE_8821(Adapter)){

		pHalData->EEPROMBluetoothType = BT_RTL8821;

		if (!AutoLoadFail) {
			tmp_u32 = rtw_read32(Adapter, REG_MULTI_FUNC_CTRL);
			if(tmp_u32 & BT_FUNC_EN)
				pHalData->EEPROMBluetoothCoexist = _TRUE;
			else
				pHalData->EEPROMBluetoothCoexist = _FALSE;
		
			tmp_u8 = hwinfo[EEPROM_RF_BT_SETTING_8821];
			pHalData->EEPROMBluetoothAntNum = (tmp_u8&0x1); // bit [0]
		} else {
			pHalData->EEPROMBluetoothCoexist = _FALSE;
			pHalData->EEPROMBluetoothAntNum = Ant_x2;
		}

		#ifdef CONFIG_BTCOEX_FORCE_CSR
		pHalData->EEPROMBluetoothType = BT_CSR_BC8;
		pHalData->EEPROMBluetoothCoexist = _TRUE;
		pHalData->EEPROMBluetoothAntNum = Ant_x2;
		#endif
	} else {
		rtw_warn_on(1);
	}

#ifdef CONFIG_BT_COEXIST
	rtw_btcoex_SetBTCoexist(Adapter, pHalData->EEPROMBluetoothCoexist);
	rtw_btcoex_SetChipType(Adapter, pHalData->EEPROMBluetoothType);
	rtw_btcoex_SetPGAntNum(Adapter, pHalData->EEPROMBluetoothAntNum==Ant_x2?2:1);
#endif /* CONFIG_BT_COEXIST */
}

void
Hal_EfuseParseIDCode8812A(
	IN	PADAPTER	padapter,
	IN	u8			*hwinfo
	)
{
	EEPROM_EFUSE_PRIV *pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);
	u16			EEPROMId;


	// Checl 0x8129 again for making sure autoload status!!
	EEPROMId = EF2Byte(*((u16*)&hwinfo[0]));
	if (EEPROMId != RTL_EEPROM_ID)
	{
		DBG_8192C("EEPROM ID(%#x) is invalid!!\n", EEPROMId);
		pEEPROM->bautoload_fail_flag = _TRUE;
	}
	else
	{
		pEEPROM->bautoload_fail_flag = _FALSE;
	}

	DBG_8192C("EEPROM ID=0x%04x\n", EEPROMId);
}

VOID
Hal_ReadPROMVersion8812A(
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
		if(IS_HARDWARE_TYPE_8812(Adapter))
			pHalData->EEPROMVersion = *(u8 *)&PROMContent[EEPROM_VERSION_8812];
		else
			pHalData->EEPROMVersion = *(u8 *)&PROMContent[EEPROM_VERSION_8821];
		if(pHalData->EEPROMVersion == 0xFF)
			pHalData->EEPROMVersion = EEPROM_Default_Version;					
	}
	//DBG_871X("pHalData->EEPROMVersion is 0x%x\n", pHalData->EEPROMVersion);
}

void
Hal_ReadTxPowerInfo8812A(
	IN	PADAPTER 		Adapter,
	IN	u8*				PROMContent,
	IN	BOOLEAN			AutoLoadFail
	)
{	
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	TxPowerInfo24G	pwrInfo24G;
	TxPowerInfo5G	pwrInfo5G;
	u8	rfPath, ch, group, TxCount;
	u8	channel5G[CHANNEL_MAX_NUMBER_5G] = 
			{36,38,40,42,44,46,48,50,52,54,56,58,60,62,64,100,102,104,106,108,110,112,
			114,116,118,120,122,124,126,128,130,132,134,136,138,140,142,144,149,151,
			153,155,157,159,161,163,165,167,168,169,171,173,175,177};
	u8	channel5G_80M[CHANNEL_MAX_NUMBER_5G_80M] = {42, 58, 106, 122, 138, 155, 171};

	hal_ReadPowerValueFromPROM8812A(Adapter, &pwrInfo24G,&pwrInfo5G, PROMContent, AutoLoadFail);

	//if(!AutoLoadFail)
	//	pHalData->bTXPowerDataReadFromEEPORM = _TRUE;		

	for(rfPath = 0 ; rfPath < MAX_RF_PATH ; rfPath++)
	{
		for(ch = 0 ; ch < CHANNEL_MAX_NUMBER_2G ; ch++)
		{
			Hal_GetChnlGroup8812A(ch+1, &group);

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
			//DBG_871X("======= Path %d, ChannelIndex %d, Group %d=======\n",rfPath,ch, group);	
			//DBG_871X("Index24G_CCK_Base[%d][%d] = 0x%x\n",rfPath,ch ,pHalData->Index24G_CCK_Base[rfPath][ch]);
			//DBG_871X("Index24G_BW40_Base[%d][%d] = 0x%x\n",rfPath,ch,pHalData->Index24G_BW40_Base[rfPath][ch]);
		}	

		for(ch = 0 ; ch < CHANNEL_MAX_NUMBER_5G; ch++) 
		{
			Hal_GetChnlGroup8812A(channel5G[ch], &group);
			
			pHalData->Index5G_BW40_Base[rfPath][ch] = pwrInfo5G.IndexBW40_Base[rfPath][group];
			//DBG_871X("======= Path %d, ChannelIndex %d, Group %d=======\n",rfPath,ch, group);			
			//DBG_871X("Index5G_BW40_Base[%d][%d] = 0x%x\n",rfPath,ch,pHalData->Index5G_BW40_Base[rfPath][ch]);
		}
		for(ch = 0 ; ch < CHANNEL_MAX_NUMBER_5G_80M; ch++) 
		{
			u8	upper, lower;
			
			Hal_GetChnlGroup8812A(channel5G_80M[ch], &group);
			upper = pwrInfo5G.IndexBW40_Base[rfPath][group];
			lower = pwrInfo5G.IndexBW40_Base[rfPath][group+1];
			
			pHalData->Index5G_BW80_Base[rfPath][ch] = (upper + lower) / 2;
			
			//DBG_871X("======= Path %d, ChannelIndex %d, Group %d=======\n",rfPath,ch, group);	
			//DBG_871X("Index5G_BW80_Base[%d][%d] = 0x%x\n",rfPath,ch,pHalData->Index5G_BW80_Base[rfPath][ch]);
		}

		for(TxCount=0;TxCount<MAX_TX_COUNT;TxCount++)
		{
			pHalData->CCK_24G_Diff[rfPath][TxCount]=pwrInfo24G.CCK_Diff[rfPath][TxCount];
			pHalData->OFDM_24G_Diff[rfPath][TxCount]=pwrInfo24G.OFDM_Diff[rfPath][TxCount];
			pHalData->BW20_24G_Diff[rfPath][TxCount]=pwrInfo24G.BW20_Diff[rfPath][TxCount];
			pHalData->BW40_24G_Diff[rfPath][TxCount]=pwrInfo24G.BW40_Diff[rfPath][TxCount];
			
			pHalData->OFDM_5G_Diff[rfPath][TxCount]=pwrInfo5G.OFDM_Diff[rfPath][TxCount];
			pHalData->BW20_5G_Diff[rfPath][TxCount]=pwrInfo5G.BW20_Diff[rfPath][TxCount];
			pHalData->BW40_5G_Diff[rfPath][TxCount]=pwrInfo5G.BW40_Diff[rfPath][TxCount];
			pHalData->BW80_5G_Diff[rfPath][TxCount]=pwrInfo5G.BW80_Diff[rfPath][TxCount];
//#if DBG
#if 0
			DBG_871X("--------------------------------------- 2.4G ---------------------------------------\n");
			DBG_871X("CCK_24G_Diff[%d][%d]= %d\n",rfPath,TxCount,pHalData->CCK_24G_Diff[rfPath][TxCount]);
			DBG_871X("OFDM_24G_Diff[%d][%d]= %d\n",rfPath,TxCount,pHalData->OFDM_24G_Diff[rfPath][TxCount]);
			DBG_871X("BW20_24G_Diff[%d][%d]= %d\n",rfPath,TxCount,pHalData->BW20_24G_Diff[rfPath][TxCount]);
			DBG_871X("BW40_24G_Diff[%d][%d]= %d\n",rfPath,TxCount,pHalData->BW40_24G_Diff[rfPath][TxCount]);
			DBG_871X("---------------------------------------- 5G ----------------------------------------\n");
			DBG_871X("OFDM_5G_Diff[%d][%d]= %d\n",rfPath,TxCount,pHalData->OFDM_5G_Diff[rfPath][TxCount]);
			DBG_871X("BW20_5G_Diff[%d][%d]= %d\n",rfPath,TxCount,pHalData->BW20_5G_Diff[rfPath][TxCount]);
			DBG_871X("BW40_5G_Diff[%d][%d]= %d\n",rfPath,TxCount,pHalData->BW40_5G_Diff[rfPath][TxCount]);
			DBG_871X("BW80_5G_Diff[%d][%d]= %d\n",rfPath,TxCount,pHalData->BW80_5G_Diff[rfPath][TxCount]);
#endif							
		}
	}

	
	// 2010/10/19 MH Add Regulator recognize for CU.
	if(!AutoLoadFail)
	{
		struct registry_priv  *registry_par = &Adapter->registrypriv;
		
			
		if(PROMContent[EEPROM_RF_BOARD_OPTION_8812] == 0xFF)
			pHalData->EEPROMRegulatory = (EEPROM_DEFAULT_BOARD_OPTION&0x7);	//bit0~2
		else
			pHalData->EEPROMRegulatory = (PROMContent[EEPROM_RF_BOARD_OPTION_8812]&0x7);	//bit0~2	
		

		// 2012/09/26 MH Add for TX power calibrate rate.
		pHalData->TxPwrCalibrateRate = PROMContent[EEPROM_TX_PWR_CALIBRATE_RATE_8812];
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
Hal_ReadBoardType8812A(
	IN	PADAPTER	Adapter,	
	IN	u8*			PROMContent,
	IN	BOOLEAN		AutoloadFail
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	if(!AutoloadFail)
	{
		pHalData->InterfaceSel = (PROMContent[EEPROM_RF_BOARD_OPTION_8812]&0xE0)>>5;
		if(PROMContent[EEPROM_RF_BOARD_OPTION_8812] == 0xFF)
			pHalData->InterfaceSel = (EEPROM_DEFAULT_BOARD_OPTION&0xE0)>>5;
	}
	else
	{
		pHalData->InterfaceSel = 0;
	}
	DBG_871X("Board Type: 0x%2x\n", pHalData->InterfaceSel);

}

VOID
Hal_ReadThermalMeter_8812A(
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
		pHalData->EEPROMThermalMeter = PROMContent[EEPROM_THERMAL_METER_8812];
	else
		pHalData->EEPROMThermalMeter = EEPROM_Default_ThermalMeter_8812;
	//pHalData->EEPROMThermalMeter = (tempval&0x1f);	//[4:0]

	if(pHalData->EEPROMThermalMeter == 0xff || AutoloadFail)
	{
		pHalData->bAPKThermalMeterIgnore = _TRUE;
		pHalData->EEPROMThermalMeter = 0xFF;		
	}

	//pHalData->ThermalMeter[0] = pHalData->EEPROMThermalMeter;	
	DBG_871X("ThermalMeter = 0x%x\n", pHalData->EEPROMThermalMeter);
}

void Hal_ReadRemoteWakeup_8812A(
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
	else
	{		
		// decide hw if support remote wakeup function
		// if hw supported, 8051 (SIE) will generate WeakUP signal( D+/D- toggle) when autoresume
#ifdef CONFIG_USB_HCI
		if(IS_HARDWARE_TYPE_8821U(padapter))
			pwrctl->bSupportRemoteWakeup = (hwinfo[EEPROM_USB_OPTIONAL_FUNCTION0_8811AU] & BIT1)?_TRUE :_FALSE;
		else
			pwrctl->bSupportRemoteWakeup = (hwinfo[EEPROM_USB_OPTIONAL_FUNCTION0] & BIT1)?_TRUE :_FALSE;
#endif //CONFIG_USB_HCI

		DBG_871X("%s...bSupportRemoteWakeup(%x)\n",__FUNCTION__, pwrctl->bSupportRemoteWakeup);
	}
}

VOID
Hal_ReadChannelPlan8812A(
	IN	PADAPTER		padapter,
	IN	u8*				hwinfo,
	IN	BOOLEAN			AutoLoadFail
	)
{
	padapter->mlmepriv.ChannelPlan = hal_com_config_channel_plan(
		padapter
		, hwinfo?hwinfo[EEPROM_ChannelPlan_8812]:0xFF
		, padapter->registrypriv.channel_plan
		, RT_CHANNEL_DOMAIN_REALTEK_DEFINE
		, AutoLoadFail
	);

	DBG_871X("mlmepriv.ChannelPlan = 0x%02x\n", padapter->mlmepriv.ChannelPlan);
}

VOID
Hal_EfuseParseXtal_8812A(
	IN	PADAPTER	pAdapter,
	IN	u8*			hwinfo,
	IN	BOOLEAN		AutoLoadFail
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);

	if(!AutoLoadFail)
	{
		pHalData->CrystalCap = hwinfo[EEPROM_XTAL_8812];
		if(pHalData->CrystalCap == 0xFF)
			pHalData->CrystalCap = EEPROM_Default_CrystalCap_8812;	 //what value should 8812 set?
	}
	else
	{
		pHalData->CrystalCap = EEPROM_Default_CrystalCap_8812;
	}
	DBG_871X("CrystalCap: 0x%2x\n", pHalData->CrystalCap);
}

VOID
Hal_ReadAntennaDiversity8812A(
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
		if(registry_par->antdiv_cfg == 2)
		{
			pHalData->AntDivCfg = (PROMContent[EEPROM_RF_BOARD_OPTION_8812]&0x18)>>3;
			if(PROMContent[EEPROM_RF_BOARD_OPTION_8812] == 0xFF)			
				pHalData->AntDivCfg = (EEPROM_DEFAULT_BOARD_OPTION&0x18)>>3;;				
		}
		else
		{
			pHalData->AntDivCfg = registry_par->antdiv_cfg;
		}

#ifdef CONFIG_BT_COEXIST
		if(hal_btcoex_1Ant(pAdapter))
			pHalData->AntDivCfg = 0;
#endif

		pHalData->TRxAntDivType = PROMContent[EEPROM_RF_ANTENNA_OPT_8812];  //todo by page
		if (pHalData->TRxAntDivType == 0xFF)
			pHalData->TRxAntDivType = FIXED_HW_ANTDIV; // For 88EE, 1Tx and 1RxCG are fixed.(1Ant, Tx and RxCG are both on aux port)
	}
	else
	{
		pHalData->AntDivCfg = 0;
	}
	
	DBG_871X("SWAS: bHwAntDiv = %x, TRxAntDivType = %x\n", pHalData->AntDivCfg, pHalData->TRxAntDivType);
}

VOID
Hal_ReadAntennaDiversity8821A(
	IN	PADAPTER		pAdapter,
	IN	u8				*PROMContent,
	IN	BOOLEAN			AutoLoadFail
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct registry_priv	*registry_par = &pAdapter->registrypriv;
	
	if(!AutoLoadFail)
	{
		// Antenna Diversity setting.
		if(registry_par->antdiv_cfg == 2)
			pHalData->AntDivCfg = (PROMContent[EEPROM_RF_BOARD_OPTION_8812]& BIT3)?_TRUE:_FALSE;
		else
			pHalData->AntDivCfg = registry_par->antdiv_cfg;

#ifdef CONFIG_BT_COEXIST
		if(hal_btcoex_1Ant(pAdapter))
			pHalData->AntDivCfg = 0;
#endif

		//pHalData->TRxAntDivType = FIXED_HW_ANTDIV; // For 88EE, 1Tx and 1RxCG are fixed.(1Ant, Tx and RxCG are both on aux port)
		pHalData->TRxAntDivType = CG_TRX_HW_ANTDIV; //DPDT

		//3 TODO	
		pHalData->AntDivCfg = 0;
	}
	else
	{
		pHalData->AntDivCfg = 0;		
	}

	DBG_871X("SWAS: bHwAntDiv = %x, TRxAntDivType = %x\n", pHalData->AntDivCfg, pHalData->TRxAntDivType);
}

VOID
hal_ReadPAType_8812A(
	IN	PADAPTER	Adapter,
	IN	u8*			PROMContent,
	IN	BOOLEAN		AutoloadFail
	)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);

	if( ! AutoloadFail )
	{
		if (GetRegAmplifierType2G(Adapter) == 0) // AUTO
		{
			pHalData->PAType_2G = EF1Byte( *(u8 *)&PROMContent[EEPROM_PA_TYPE_8812AU] );
			pHalData->LNAType_2G = EF1Byte( *(u8 *)&PROMContent[EEPROM_LNA_TYPE_2G_8812AU] );
			if (pHalData->PAType_2G == 0xFF)
				pHalData->PAType_2G = 0;
			if(pHalData->LNAType_2G == 0xFF)
				pHalData->LNAType_2G = 0;
			
			pHalData->ExternalPA_2G = ((pHalData->PAType_2G & BIT5) && (pHalData->PAType_2G & BIT4)) ? 1 : 0;
			pHalData->ExternalLNA_2G = ((pHalData->LNAType_2G & BIT7) && (pHalData->LNAType_2G & BIT3)) ? 1 : 0;
		}
		else 
		{
			pHalData->ExternalPA_2G  = (GetRegAmplifierType2G(Adapter)&ODM_BOARD_EXT_PA)  ? 1 : 0;
			pHalData->ExternalLNA_2G = (GetRegAmplifierType2G(Adapter)&ODM_BOARD_EXT_LNA) ? 1 : 0;
		}

		if (GetRegAmplifierType5G(Adapter) == 0) // AUTO
		{
			pHalData->PAType_5G = EF1Byte( *(u8 *)&PROMContent[EEPROM_PA_TYPE_8812AU] );
			pHalData->LNAType_5G = EF1Byte( *(u8 *)&PROMContent[EEPROM_LNA_TYPE_5G_8812AU] );
			if (pHalData->PAType_5G == 0xFF && pHalData->LNAType_5G == 0xFF) {
				pHalData->PAType_5G = 0;
				pHalData->LNAType_5G = 0;
			}
			pHalData->ExternalPA_5G = ((pHalData->PAType_5G & BIT1) && (pHalData->PAType_5G & BIT0)) ? 1 : 0;
			pHalData->ExternalLNA_5G = ((pHalData->LNAType_5G & BIT7) && (pHalData->LNAType_5G & BIT3)) ? 1 : 0;
		}
		else
		{
			pHalData->ExternalPA_5G  = (GetRegAmplifierType5G(Adapter)&ODM_BOARD_EXT_PA_5G)  ? 1 : 0;
			pHalData->ExternalLNA_5G = (GetRegAmplifierType5G(Adapter)&ODM_BOARD_EXT_LNA_5G) ? 1 : 0;
		}
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
	}
	DBG_871X("pHalData->PAType_2G is 0x%x, pHalData->ExternalPA_2G = %d\n", pHalData->PAType_2G, pHalData->ExternalPA_2G);
	DBG_871X("pHalData->PAType_5G is 0x%x, pHalData->ExternalPA_5G = %d\n", pHalData->PAType_5G, pHalData->ExternalPA_5G);
	DBG_871X("pHalData->LNAType_2G is 0x%x, pHalData->ExternalLNA_2G = %d\n", pHalData->LNAType_2G, pHalData->ExternalLNA_2G);
	DBG_871X("pHalData->LNAType_5G is 0x%x, pHalData->ExternalLNA_5G = %d\n", pHalData->LNAType_5G, pHalData->ExternalLNA_5G);
}

VOID
Hal_ReadAmplifierType_8812A(
	IN	PADAPTER	Adapter,
	IN	u8*			PROMContent,
	IN	BOOLEAN		AutoloadFail
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	u8 extTypePA_2G_A  = (PROMContent[0xBD] & BIT2)      >> 2; // 0xBD[2]
	u8 extTypePA_2G_B  = (PROMContent[0xBD] & BIT6)      >> 6; // 0xBD[6]
	u8 extTypePA_5G_A  = (PROMContent[0xBF] & BIT2)      >> 2; // 0xBF[2]
	u8 extTypePA_5G_B  = (PROMContent[0xBF] & BIT6)      >> 6; // 0xBF[6]
	u8 extTypeLNA_2G_A = (PROMContent[0xBD] & (BIT1|BIT0)) >> 0; // 0xBD[1:0]
	u8 extTypeLNA_2G_B = (PROMContent[0xBD] & (BIT5|BIT4)) >> 4; // 0xBD[5:4]
	u8 extTypeLNA_5G_A = (PROMContent[0xBF] & (BIT1|BIT0)) >> 0; // 0xBF[1:0]
	u8 extTypeLNA_5G_B = (PROMContent[0xBF] & (BIT5|BIT4)) >> 4; // 0xBF[5:4]

	hal_ReadPAType_8812A(Adapter, PROMContent, AutoloadFail);

	if ((pHalData->PAType_2G & (BIT5|BIT4)) == (BIT5|BIT4)) // [2.4G] Path A and B are both extPA 
	    pHalData->TypeGPA  = extTypePA_2G_B  << 2 | extTypePA_2G_A;

	if ((pHalData->PAType_5G & (BIT1|BIT0)) == (BIT1|BIT0)) // [5G] Path A and B are both extPA 
	    pHalData->TypeAPA  = extTypePA_5G_B  << 2 | extTypePA_5G_A; 

	if ((pHalData->LNAType_2G & (BIT7|BIT3)) == (BIT7|BIT3)) // [2.4G] Path A and B are both extLNA
	    pHalData->TypeGLNA = extTypeLNA_2G_B << 2 | extTypeLNA_2G_A;

	if ((pHalData->LNAType_5G & (BIT7|BIT3)) == (BIT7|BIT3)) // [5G] Path A and B are both extLNA
	    pHalData->TypeALNA = extTypeLNA_5G_B << 2 | extTypeLNA_5G_A;
	
	DBG_871X("pHalData->TypeGPA = 0x%X\n", pHalData->TypeGPA);
	DBG_871X("pHalData->TypeAPA = 0x%X\n", pHalData->TypeAPA);
	DBG_871X("pHalData->TypeGLNA = 0x%X\n", pHalData->TypeGLNA);
	DBG_871X("pHalData->TypeALNA = 0x%X\n", pHalData->TypeALNA);
}

VOID
Hal_ReadPAType_8821A(
	IN	PADAPTER	Adapter,
	IN	u8*			PROMContent,
	IN	BOOLEAN		AutoloadFail
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	if( ! AutoloadFail )
	{
		if (GetRegAmplifierType2G(Adapter) == 0) // AUTO
		{
			pHalData->PAType_2G = EF1Byte( *(u8 *)&PROMContent[EEPROM_PA_TYPE_8812AU] );
			pHalData->LNAType_2G = EF1Byte( *(u8 *)&PROMContent[EEPROM_LNA_TYPE_2G_8812AU] );
			if(pHalData->PAType_2G == 0xFF )
				pHalData->PAType_2G = 0;
			if(pHalData->LNAType_2G == 0xFF) 
				pHalData->LNAType_2G = 0;
			
			pHalData->ExternalPA_2G = (pHalData->PAType_2G & BIT4) ? 1 : 0;
			pHalData->ExternalLNA_2G = (pHalData->LNAType_2G & BIT3) ? 1 : 0;
		}
		else 
		{
			pHalData->ExternalPA_2G  = (GetRegAmplifierType2G(Adapter)&ODM_BOARD_EXT_PA)  ? 1 : 0;
			pHalData->ExternalLNA_2G = (GetRegAmplifierType2G(Adapter)&ODM_BOARD_EXT_LNA) ? 1 : 0;
		}

		if (GetRegAmplifierType5G(Adapter) == 0) // AUTO
		{
			pHalData->PAType_5G = EF1Byte( *(u8 *)&PROMContent[EEPROM_PA_TYPE_8812AU] );
			pHalData->LNAType_5G = EF1Byte( *(u8 *)&PROMContent[EEPROM_LNA_TYPE_5G_8812AU] );
			if (pHalData->PAType_5G == 0xFF && pHalData->LNAType_5G == 0xFF) {
				pHalData->PAType_5G = 0;
				pHalData->LNAType_5G = 0;
			}
			pHalData->ExternalPA_5G = (pHalData->PAType_5G & BIT0) ? 1 : 0;
			pHalData->ExternalLNA_5G = (pHalData->LNAType_5G & BIT3) ? 1 : 0;
		}
		else
		{
			pHalData->ExternalPA_5G  = (GetRegAmplifierType5G(Adapter)&ODM_BOARD_EXT_PA_5G)  ? 1 : 0;
			pHalData->ExternalLNA_5G = (GetRegAmplifierType5G(Adapter)&ODM_BOARD_EXT_LNA_5G) ? 1 : 0;
		}
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
	}
	DBG_871X("pHalData->PAType_2G is 0x%x, pHalData->ExternalPA_2G = %d\n", pHalData->PAType_2G, pHalData->ExternalPA_2G);
	DBG_871X("pHalData->PAType_5G is 0x%x, pHalData->ExternalPA_5G = %d\n", pHalData->PAType_5G, pHalData->ExternalPA_5G);
	DBG_871X("pHalData->LNAType_2G is 0x%x, pHalData->ExternalLNA_2G = %d\n", pHalData->LNAType_2G, pHalData->ExternalLNA_2G);
	DBG_871X("pHalData->LNAType_5G is 0x%x, pHalData->ExternalLNA_5G = %d\n", pHalData->LNAType_5G, pHalData->ExternalLNA_5G);
}

VOID
Hal_ReadRFEType_8812A(
	IN	PADAPTER	Adapter,	
	IN	u8*			PROMContent,
	IN	BOOLEAN		AutoloadFail
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	if(!AutoloadFail)
	{
		if(( GetRegRFEType(Adapter) != 64)|| 0xFF == PROMContent[EEPROM_RFE_OPTION_8812])
		{	
			if(GetRegRFEType(Adapter) != 64)
				pHalData->RFEType = GetRegRFEType(Adapter);
			else
			{ 
				if (IS_HARDWARE_TYPE_8812AU(Adapter))
					pHalData->RFEType = 0;
				else if (IS_HARDWARE_TYPE_8812E(Adapter))
					pHalData->RFEType = 2;
				else
					pHalData->RFEType = EEPROM_DEFAULT_RFE_OPTION;
			}	
			
		}
		else if(PROMContent[EEPROM_RFE_OPTION_8812] & BIT7)
		{
			if(pHalData->ExternalLNA_5G)
			{
				if(pHalData->ExternalPA_5G)		
				{
					if(pHalData->ExternalLNA_2G && pHalData->ExternalPA_2G )
						pHalData->RFEType = 3;
					else
						pHalData->RFEType = 0;
				}
				else
					pHalData->RFEType = 2;
			}
			else
				pHalData->RFEType = 4;
		}
		else
		{
			pHalData->RFEType = PROMContent[EEPROM_RFE_OPTION_8812]&0x3F;

			// 2013/03/19 MH Due to othe customer already use incorrect EFUSE map
			// to for their product. We need to add workaround to prevent to modify 
			// spec and notify all customer to revise the IC 0xca content. After
			// discussing with Willis an YN, revise driver code to prevent.
			if (pHalData->RFEType == 4 && 
				(pHalData->ExternalPA_5G == _TRUE || pHalData->ExternalPA_2G == _TRUE ||
				pHalData->ExternalLNA_5G == _TRUE || pHalData->ExternalLNA_2G == _TRUE))
			{
				if (IS_HARDWARE_TYPE_8812AU(Adapter))
					pHalData->RFEType = 0;
				else if (IS_HARDWARE_TYPE_8812E(Adapter))
					pHalData->RFEType = 2;
			}
		}
	}
	else
	{
		if(GetRegRFEType(Adapter) != 64)
				pHalData->RFEType = GetRegRFEType(Adapter);
		else
		{ 
				if (IS_HARDWARE_TYPE_8812AU(Adapter))
					pHalData->RFEType = 0;
				else if (IS_HARDWARE_TYPE_8812E(Adapter))
					pHalData->RFEType = 2;
				else
					pHalData->RFEType = EEPROM_DEFAULT_RFE_OPTION;
		}	
	}

	DBG_871X("RFE Type: 0x%2x\n", pHalData->RFEType);
}

//
// 2013/04/15 MH Add 8812AU- VL/VS/VN for different board type.
//
VOID
hal_ReadUsbType_8812AU(
	IN	PADAPTER	Adapter,
	IN	u8			*PROMContent,
	IN	BOOLEAN		AutoloadFail
	)
{
	//if (IS_HARDWARE_TYPE_8812AU(Adapter) && Adapter->UsbModeMechanism.RegForcedUsbMode == 5)
	{
		PHAL_DATA_TYPE pHalData = GET_HAL_DATA(Adapter);
		u8	reg_tmp, i, j, antenna = 0, wmode = 0;
		// Read anenna type from EFUSE 1019/1018
		for (i = 0; i < 2; i++)
		{
			// Check efuse address 1019
			// Check efuse address 1018
			efuse_OneByteRead(Adapter, 1019-i, &reg_tmp, _FALSE);

			for (j = 0; j < 2; j++)
			{
				// CHeck bit 7-5
				// Check bit 3-1
				antenna = ((reg_tmp&0xee) >> (5-(j*4)));
				if (antenna == 0)
					continue;
				else
				{					
					break;
				}	
			}
		}

		// Read anenna type from EFUSE 1021/1020
		for (i = 0; i < 2; i++)
		{
			// Check efuse address 1019
			// Check efuse address 1018
			efuse_OneByteRead(Adapter, 1021-i, &reg_tmp, _FALSE);

			for (j = 0; j < 2; j++)
			{
				// CHeck bit 3-2
				// Check bit 1-0
				wmode = ((reg_tmp&0x0f) >> (2-(j*2)));
				if (wmode)
					continue;
				else
				{					
					break;
				}	
			}
		}

		// Antenna == 1 WMODE = 3 RTL8812AU-VL 11AC + USB2.0 Mode
		if (antenna == 1)
		{
			// Config 8812AU as 1*1 mode AC mode.
			pHalData->rf_type = RF_1T1R;
			//UsbModeSwitch_SetUsbModeMechOn(Adapter, FALSE);
			//pHalData->EFUSEHidden = EFUSE_HIDDEN_812AU_VL;
			DBG_871X("%s(): EFUSE_HIDDEN_812AU_VL\n",__FUNCTION__);
		}
		else if (antenna == 2)
		{
			if (wmode == 3)
			{
				if (PROMContent[EEPROM_USB_MODE_8812] == 0x2)
				{
					// RTL8812AU Normal Mode. No further action.
					//pHalData->EFUSEHidden = EFUSE_HIDDEN_812AU;
					DBG_871X("%s(): EFUSE_HIDDEN_812AU\n",__FUNCTION__);
				}
				else	
				{
					// Antenna == 2 WMODE = 3 RTL8812AU-VS 11AC + USB2.0 Mode
					// Driver will not support USB automatic switch
					//UsbModeSwitch_SetUsbModeMechOn(Adapter, FALSE);
					//pHalData->EFUSEHidden = EFUSE_HIDDEN_812AU_VS;
					DBG_871X("%s(): EFUSE_HIDDEN_812AU_VS\n",__FUNCTION__);
				}
			}
			else if (wmode == 2)
			{
				// Antenna == 2 WMODE = 2 RTL8812AU-VN 11N only + USB2.0 Mode
				//UsbModeSwitch_SetUsbModeMechOn(Adapter, FALSE);
				//pHalData->EFUSEHidden = EFUSE_HIDDEN_812AU_VN;
				DBG_871X("%s(): EFUSE_HIDDEN_812AU_VN\n",__FUNCTION__);
			}
		}	
	}
}

enum{
		VOLTAGE_V25						= 0x03,
		LDOE25_SHIFT						= 28 ,
	};

static VOID
Hal_EfusePowerSwitch8812A(
	IN	PADAPTER	pAdapter,
	IN	u8		bWrite,
	IN	u8		PwrState)
{
	u8	tempval;
	u16	tmpV16;
	#define EFUSE_ACCESS_ON_JAGUAR 0x69
	#define EFUSE_ACCESS_OFF_JAGUAR 0x00
	if (PwrState == _TRUE)
	{
		rtw_write8(pAdapter, REG_EFUSE_BURN_GNT_8812, EFUSE_ACCESS_ON_JAGUAR);

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
		if( (!(tmpV16 & LOADER_CLK_EN) )  ||(!(tmpV16 & ANA8M) ) )
		{
			tmpV16 |= (LOADER_CLK_EN |ANA8M ) ;
			rtw_write16(pAdapter,REG_SYS_CLKR,tmpV16);
		}

		if(bWrite == _TRUE)
		{
			// Enable LDO 2.5V before read/write action
			tempval = rtw_read8(pAdapter, EFUSE_TEST+3);
			tempval &= ~(BIT3|BIT4|BIT5|BIT6);
			tempval |= (VOLTAGE_V25 << 3);
			tempval |= BIT7;
			rtw_write8(pAdapter, EFUSE_TEST+3, tempval);
		}
	}
	else
	{
		rtw_write8(pAdapter, REG_EFUSE_BURN_GNT_8812, EFUSE_ACCESS_OFF_JAGUAR);

		if(bWrite == _TRUE){
			// Disable LDO 2.5V after read/write action
			tempval = rtw_read8(pAdapter, EFUSE_TEST+3);
			rtw_write8(pAdapter, EFUSE_TEST+3, (tempval & 0x7F));
		}
	}
}

static VOID
rtl8812_EfusePowerSwitch(
	IN	PADAPTER	pAdapter,
	IN	u8		bWrite,
	IN	u8		PwrState)
{
	Hal_EfusePowerSwitch8812A(pAdapter, bWrite, PwrState);	
}

static BOOLEAN
Hal_EfuseSwitchToBank8812A(
	IN		PADAPTER	pAdapter,
	IN		u1Byte		bank,
	IN		BOOLEAN		bPseudoTest
	)
{
	return _FALSE;
}

static VOID
Hal_EfuseReadEFuse8812A(
	PADAPTER		Adapter,
	u16			_offset,
	u16 			_size_byte,
	u8      		*pbuf,
	IN	BOOLEAN	bPseudoTest
	)
{
	u8	*efuseTbl = NULL;
	u16	eFuse_Addr = 0;
	u8	offset=0, wden=0;
	u16	i, j;
	u16	**eFuseWord = NULL;
	u16	efuse_utilized = 0;
	u8	efuse_usage = 0;
	u8	offset_2_0=0;
	u8	efuseHeader=0, efuseExtHdr=0, efuseData=0;

	//
	// Do NOT excess total size of EFuse table. Added by Roger, 2008.11.10.
	//
	if((_offset + _size_byte)>EFUSE_MAP_LEN_JAGUAR)
	{// total E-Fuse table is 512bytes
		DBG_8192C("Hal_EfuseReadEFuse8812A(): Invalid offset(%#x) with read bytes(%#x)!!\n",_offset, _size_byte);
		goto exit;
	}

	efuseTbl = (u8*)rtw_zmalloc(EFUSE_MAP_LEN_JAGUAR);
	if(efuseTbl == NULL)
	{
		DBG_871X("%s: alloc efuseTbl fail!\n", __FUNCTION__);
		goto exit;
	}

	eFuseWord= (u16 **)rtw_malloc2d(EFUSE_MAX_SECTION_JAGUAR, EFUSE_MAX_WORD_UNIT, 2);
	if(eFuseWord == NULL)
	{
		DBG_871X("%s: alloc eFuseWord fail!\n", __FUNCTION__);
		goto exit;
	}

	// 0. Refresh efuse init map as all oxFF.
	for (i = 0; i < EFUSE_MAX_SECTION_JAGUAR; i++)
		for (j = 0; j < EFUSE_MAX_WORD_UNIT; j++)
			eFuseWord[i][j] = 0xFFFF;

	//
	// 1. Read the first byte to check if efuse is empty!!!
	// 
	//
	efuse_OneByteRead(Adapter, eFuse_Addr++, &efuseHeader, bPseudoTest);	

	if(efuseHeader != 0xFF)
	{
		efuse_utilized++;
	}
	else
	{
		DBG_871X("EFUSE is empty\n");
		efuse_utilized = 0;
		goto exit;
	}
	//RT_DISP(FEEPROM, EFUSE_READ_ALL, ("Hal_EfuseReadEFuse8812A(): efuse_utilized: %d\n", efuse_utilized));

	//
	// 2. Read real efuse content. Filter PG header and every section data.
	//
	while((efuseHeader != 0xFF) && AVAILABLE_EFUSE_ADDR_8812(eFuse_Addr))
	{
		//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("efuse_Addr-%d efuse_data=%x\n", eFuse_Addr-1, *rtemp8));
	
		// Check PG header for section num.
		if(EXT_HEADER(efuseHeader))		//extended header
		{
			offset_2_0 = GET_HDR_OFFSET_2_0(efuseHeader);
			//RT_DISP(FEEPROM, EFUSE_READ_ALL, ("extended header offset_2_0=%X\n", offset_2_0));

			efuse_OneByteRead(Adapter, eFuse_Addr++, &efuseExtHdr, bPseudoTest);	

			//RT_DISP(FEEPROM, EFUSE_READ_ALL, ("efuse[%X]=%X\n", eFuse_Addr-1, efuseExtHdr));

			if(efuseExtHdr != 0xff)
			{
				efuse_utilized++;
				if(ALL_WORDS_DISABLED(efuseExtHdr))
				{
					efuse_OneByteRead(Adapter, eFuse_Addr++, &efuseHeader, bPseudoTest);
					if(efuseHeader != 0xff)
					{
						efuse_utilized++;
					}
					break;
				}
				else
				{
					offset = ((efuseExtHdr & 0xF0) >> 1) | offset_2_0;
					wden = (efuseExtHdr & 0x0F);
				}
			}
			else
			{
				DBG_871X("Error condition, extended = 0xff\n");
				// We should handle this condition.
				break;
			}
		}
		else
		{
			offset = ((efuseHeader >> 4) & 0x0f);
			wden = (efuseHeader & 0x0f);
		}
		
		if(offset < EFUSE_MAX_SECTION_JAGUAR)
		{
			// Get word enable value from PG header
			//RT_DISP(FEEPROM, EFUSE_READ_ALL, ("Offset-%X Worden=%X\n", offset, wden));

			for(i=0; i<EFUSE_MAX_WORD_UNIT; i++)
			{
				// Check word enable condition in the section				
				if(!(wden & (0x01<<i)))
				{
					efuse_OneByteRead(Adapter, eFuse_Addr++, &efuseData, bPseudoTest);
					//RT_DISP(FEEPROM, EFUSE_READ_ALL, ("efuse[%X]=%X\n", eFuse_Addr-1, efuseData));
					efuse_utilized++;
					eFuseWord[offset][i] = (efuseData & 0xff);

					if(!AVAILABLE_EFUSE_ADDR_8812(eFuse_Addr))
						break;

					efuse_OneByteRead(Adapter, eFuse_Addr++, &efuseData, bPseudoTest);
					//RT_DISP(FEEPROM, EFUSE_READ_ALL, ("efuse[%X]=%X\n", eFuse_Addr-1, efuseData));
					efuse_utilized++;
					eFuseWord[offset][i] |= (((u16)efuseData << 8) & 0xff00);

					if(!AVAILABLE_EFUSE_ADDR_8812(eFuse_Addr)) 
						break;
				}
			}
		}

		// Read next PG header
		efuse_OneByteRead(Adapter, eFuse_Addr++, &efuseHeader, bPseudoTest);	
		//RTPRINT(FEEPROM, EFUSE_READ_ALL, ("Addr=%d rtemp 0x%x\n", eFuse_Addr, *rtemp8));

		if(efuseHeader != 0xFF)
		{
			efuse_utilized++;
		}
	}

	//
	// 3. Collect 16 sections and 4 word unit into Efuse map.
	//
	for(i=0; i<EFUSE_MAX_SECTION_JAGUAR; i++)
	{
		for(j=0; j<EFUSE_MAX_WORD_UNIT; j++)
		{
			efuseTbl[(i*8)+(j*2)]=(eFuseWord[i][j] & 0xff);
			efuseTbl[(i*8)+((j*2)+1)]=((eFuseWord[i][j] >> 8) & 0xff);
		}
	}

	//RT_DISP(FEEPROM, EFUSE_READ_ALL, ("Hal_EfuseReadEFuse8812A(): efuse_utilized: %d\n", efuse_utilized));

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
	efuse_usage = (u1Byte)((eFuse_Addr*100)/EFUSE_REAL_CONTENT_LEN_JAGUAR);
	rtw_hal_set_hwreg(Adapter, HW_VAR_EFUSE_BYTES, (u8 *)&eFuse_Addr);

exit:
	if(efuseTbl)
		rtw_mfree(efuseTbl, EFUSE_MAP_LEN_JAGUAR);

	if(eFuseWord)
		rtw_mfree2d((void *)eFuseWord, EFUSE_MAX_SECTION_JAGUAR, EFUSE_MAX_WORD_UNIT, sizeof(u16));
}

static VOID
rtl8812_ReadEFuse(
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
	Hal_EfuseReadEFuse8812A(Adapter, _offset, _size_byte, pbuf, bPseudoTest);

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
Hal_EFUSEGetEfuseDefinition8812A(
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
				*pMax_section = EFUSE_MAX_SECTION_JAGUAR;
			}
			break;
		case TYPE_EFUSE_REAL_CONTENT_LEN:
			{
				u16* pu2Tmp;
				pu2Tmp = (u16*)pOut;
				*pu2Tmp = EFUSE_REAL_CONTENT_LEN_JAGUAR;
			}
			break;
		case TYPE_EFUSE_CONTENT_LEN_BANK:
			{
				u16* pu2Tmp;
				pu2Tmp = (u16*)pOut;
				*pu2Tmp = EFUSE_REAL_CONTENT_LEN_JAGUAR;
			}
			break;
		case TYPE_AVAILABLE_EFUSE_BYTES_BANK:
			{
				u16* pu2Tmp;
				pu2Tmp = (u16*)pOut;
				*pu2Tmp = (u16)(EFUSE_REAL_CONTENT_LEN_JAGUAR-EFUSE_OOB_PROTECT_BYTES_JAGUAR);
			}
			break;
		case TYPE_AVAILABLE_EFUSE_BYTES_TOTAL:
			{
				u16* pu2Tmp;
				pu2Tmp = (u16*)pOut;
				*pu2Tmp = (u16)(EFUSE_REAL_CONTENT_LEN_JAGUAR-EFUSE_OOB_PROTECT_BYTES_JAGUAR);
			}
			break;
		case TYPE_EFUSE_MAP_LEN:
			{
				u16* pu2Tmp;
				pu2Tmp = (u16*)pOut;
				*pu2Tmp = (u16)EFUSE_MAP_LEN_JAGUAR;
			}
			break;
		case TYPE_EFUSE_PROTECT_BYTES_BANK:
			{
				u8* pu1Tmp;
				pu1Tmp = (u8*)pOut;
				*pu1Tmp = (u8)(EFUSE_OOB_PROTECT_BYTES_JAGUAR);
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
Hal_EFUSEGetEfuseDefinition_Pseudo8812A(
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
				*pMax_section = EFUSE_MAX_SECTION_JAGUAR;
			}
			break;
		case TYPE_EFUSE_REAL_CONTENT_LEN:
			{
				u16* pu2Tmp;
				pu2Tmp = (pu2Byte)pOut;
				*pu2Tmp = EFUSE_REAL_CONTENT_LEN_JAGUAR;
			}
			break;
		case TYPE_EFUSE_CONTENT_LEN_BANK:
			{
				u16* pu2Tmp;
				pu2Tmp = (pu2Byte)pOut;
				*pu2Tmp = EFUSE_REAL_CONTENT_LEN_JAGUAR;
			}
			break;
		case TYPE_AVAILABLE_EFUSE_BYTES_BANK:
			{
				u16* pu2Tmp;
				pu2Tmp = (pu2Byte)pOut;
				*pu2Tmp = (u2Byte)(EFUSE_REAL_CONTENT_LEN_JAGUAR-EFUSE_OOB_PROTECT_BYTES_JAGUAR);
			}
			break;
		case TYPE_AVAILABLE_EFUSE_BYTES_TOTAL:
			{
				u16* pu2Tmp;
				pu2Tmp = (pu2Byte)pOut;
				*pu2Tmp = (u2Byte)(EFUSE_REAL_CONTENT_LEN_JAGUAR-EFUSE_OOB_PROTECT_BYTES_JAGUAR);
			}
			break;
		case TYPE_EFUSE_MAP_LEN:
			{
				u16* pu2Tmp;
				pu2Tmp = (pu2Byte)pOut;
				*pu2Tmp = (u2Byte)EFUSE_MAP_LEN_JAGUAR;
			}
			break;
		case TYPE_EFUSE_PROTECT_BYTES_BANK:
			{
				u8* pu1Tmp;
				pu1Tmp = (u8*)pOut;
				*pu1Tmp = (u8)(EFUSE_OOB_PROTECT_BYTES_JAGUAR);
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
rtl8812_EFUSE_GetEfuseDefinition(
	IN		PADAPTER	pAdapter,
	IN		u8		efuseType,
	IN		u8		type,
	OUT		void		*pOut,
	IN		BOOLEAN		bPseudoTest
	)
{
	if(bPseudoTest)
	{
		Hal_EFUSEGetEfuseDefinition_Pseudo8812A(pAdapter, efuseType, type, pOut);
	}
	else
	{
		Hal_EFUSEGetEfuseDefinition8812A(pAdapter, efuseType, type, pOut);
	}
}

static u8
Hal_EfuseWordEnableDataWrite8812A(	IN	PADAPTER	pAdapter,
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
rtl8812_Efuse_WordEnableDataWrite(	IN	PADAPTER	pAdapter,
							IN	u16		efuse_addr,
							IN	u8		word_en,
							IN	u8		*data,
							IN	BOOLEAN		bPseudoTest)
{
	u8	ret=0;

	ret = Hal_EfuseWordEnableDataWrite8812A(pAdapter, efuse_addr, word_en, data, bPseudoTest);

	return ret;
}


static u16
hal_EfuseGetCurrentSize_8812A(IN	PADAPTER	pAdapter,
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
			(efuse_addr  < EFUSE_REAL_CONTENT_LEN_JAGUAR))
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
rtl8812_EfuseGetCurrentSize(
	IN	PADAPTER	pAdapter,
	IN	u8			efuseType,
	IN	BOOLEAN		bPseudoTest)
{
	u16	ret=0;

	ret = hal_EfuseGetCurrentSize_8812A(pAdapter, bPseudoTest);

	return ret;
}


static int
hal_EfusePgPacketRead_8812A(
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
	if(offset>EFUSE_MAX_SECTION_JAGUAR)
		return _FALSE;

	_rtw_memset((PVOID)data, 0xff, sizeof(u8)*PGPKT_DATA_SIZE);
	_rtw_memset((PVOID)tmpdata, 0xff, sizeof(u8)*PGPKT_DATA_SIZE);


	//
	// <Roger_TODO> Efuse has been pre-programmed dummy 5Bytes at the end of Efuse by CP.
	// Skip dummy parts to prevent unexpected data read from Efuse.
	// By pass right now. 2009.02.19.
	//
	while(bContinual && (efuse_addr  < EFUSE_REAL_CONTENT_LEN_JAGUAR) )
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
						break;
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
rtl8812_Efuse_PgPacketRead(	IN	PADAPTER	pAdapter,
					IN	u8			offset,
					IN	u8			*data,
					IN	BOOLEAN		bPseudoTest)
{
	int	ret=0;

	ret = hal_EfusePgPacketRead_8812A(pAdapter, offset, data, bPseudoTest);

	return ret;
}

int
hal_EfusePgPacketWrite_8812A(IN	PADAPTER	pAdapter, 
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
	if( Efuse_GetCurrentSize(pAdapter, efuseType, bPseudoTest) >= (EFUSE_REAL_CONTENT_LEN_JAGUAR-EFUSE_OOB_PROTECT_BYTES_JAGUAR))
	{
		DBG_871X("hal_EfusePgPacketWrite_8812A() error: %x >= %x\n", Efuse_GetCurrentSize(pAdapter, efuseType, bPseudoTest), (EFUSE_REAL_CONTENT_LEN_JAGUAR-EFUSE_OOB_PROTECT_BYTES_JAGUAR));
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
	while( bContinual && (efuse_addr  < (EFUSE_REAL_CONTENT_LEN_JAGUAR-EFUSE_OOB_PROTECT_BYTES_JAGUAR)) )
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
						u8 next = 0, next_next = 0, data = 0, i = 0; 
						u8 s = ((tmp_header & 0xF0) >> 4);
						efuse_OneByteRead(pAdapter, efuse_addr+1, &next, bPseudoTest);
						efuse_OneByteRead(pAdapter, efuse_addr+2, &next_next, bPseudoTest);
						if (next == 0xFF && next_next == 0xFF) { // Have enough space to make fake data to recover bad header.
							switch (s) {
								case 0x0:
								case 0x2:
								case 0x4:
								case 0x6:
								case 0x8:
								case 0xA:
								case 0xC:
									for (i = 0; i < 3; ++i) {
								        efuse_OneByteWrite(pAdapter, efuse_addr, 0x27, bPseudoTest);
										efuse_OneByteRead(pAdapter, efuse_addr, &data, bPseudoTest);
										if (data == 0x27)
											break;
									}										
									break;
								case 0xE:
									for (i = 0; i < 3; ++i) {
								        efuse_OneByteWrite(pAdapter, efuse_addr, 0x17, bPseudoTest);
										efuse_OneByteRead(pAdapter, efuse_addr, &data, bPseudoTest);
										if (data == 0x17)
											break;
									}				
									break;
								default:
									break;
							}
							efuse_OneByteWrite(pAdapter, efuse_addr+1, 0xFF, bPseudoTest);
							efuse_OneByteWrite(pAdapter, efuse_addr+2, 0xFF, bPseudoTest);							
							efuse_addr += 3;
						} else {						
							efuse_addr++;
						}
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
					u8 i = 0, data = 0;
					tmp_header	=  efuse_data;					
					tmp_pkt.offset	= (tmp_header>>4) & 0x0F;
					tmp_pkt.word_en = tmp_header & 0x0F;	
					
					if (tmp_pkt.word_en == 0xF) {
						u8 next = 0;
						efuse_OneByteRead(pAdapter, efuse_addr+1, &next, bPseudoTest);
						if (next == 0xFF) { // Have enough space to make fake data to recover bad header.
							tmp_header = (tmp_header & 0xF0) | 0x7;
							for (i = 0; i < 3; ++i) {
						        efuse_OneByteWrite(pAdapter, efuse_addr, tmp_header, bPseudoTest);
								efuse_OneByteRead(pAdapter, efuse_addr, &data, bPseudoTest);
								if (data == tmp_header)
									break;
							}											
							efuse_OneByteWrite(pAdapter, efuse_addr+1, 0xFF, bPseudoTest);
							efuse_OneByteWrite(pAdapter, efuse_addr+2, 0xFF, bPseudoTest);
							efuse_addr += 2;				
						}
					}
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
							//target_pkt.word_en= target_pkt.word_en; 				
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

	if(efuse_addr  >= (EFUSE_REAL_CONTENT_LEN_JAGUAR-EFUSE_OOB_PROTECT_BYTES_JAGUAR))
	{
		DBG_871X("hal_EfusePgPacketWrite_8812A(): efuse_addr(%#x) Out of size!!\n", efuse_addr);
	}
	//efuse_reg_ctrl(pAdapter,_FALSE);//power off
	
	return _TRUE;
}

static int
rtl8812_Efuse_PgPacketWrite(IN	PADAPTER	pAdapter,
					IN	u8 			offset,
					IN	u8			word_en,
					IN	u8			*data,
					IN	BOOLEAN		bPseudoTest)
{
	int	ret;

	ret = hal_EfusePgPacketWrite_8812A(pAdapter, offset, word_en, data, bPseudoTest);

	return ret;
}

#ifdef CONFIG_EFUSE_CONFIG_FILE
static s32 _halReadPGDataFromFile(PADAPTER padapter, u8 *pbuf)
{
	u32 i;
	struct file *fp;
	mm_segment_t fs;
	u8 temp[3];
	loff_t pos = 0;


	temp[2] = 0; // add end of string '\0'

	DBG_8192C("%s: Read Efuse from file [%s]\n", __FUNCTION__, EFUSE_FILE_PATH);
	fp = filp_open(EFUSE_FILE_PATH, O_RDONLY,  0);
	if (IS_ERR(fp)) {
		DBG_8192C("%s: Error, Read Efuse configure file FAIL!\n", __FUNCTION__);
		pEEPROM->bloadfile_fail_flag = _TRUE;
		return _FAIL;
	}

	fs = get_fs();
	set_fs(KERNEL_DS);
	for (i=0; i<HWSET_MAX_SIZE_JAGUAR; i++)
	{
		vfs_read(fp, temp, 2, &pos);
		pbuf[i] = simple_strtoul(temp, NULL, 16);
		pos += 1; // Filter the space character
	}
	set_fs(fs);
	filp_close(fp, NULL);

#ifdef CONFIG_DEBUG
	DBG_8192C("Efuse configure file:\n");
	for (i=0; i<HWSET_MAX_SIZE_JAGUAR; i++)
	{
		if (i % 16 == 0)
			printk("\n");

		printk("%02X ", pbuf[i]);
	}
	printk("\n");
	DBG_8192C("\n");
#endif

	pEEPROM->bloadfile_fail_flag = _FALSE;
	return _SUCCESS;
}

static s32 _halReadMACAddrFromFile(PADAPTER padapter, u8 *pbuf)
{
	struct file *fp;
	mm_segment_t fs;
	loff_t pos = 0;
	u8 source_addr[18];
	u8 *head, *end;
	u32	curtime;
	u32 i;
	s32 ret = _SUCCESS;

	u8 null_mac_addr[ETH_ALEN] = {0, 0, 0, 0, 0, 0};
	u8 multi_mac_addr[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};


	curtime = rtw_get_current_time();

	_rtw_memset(source_addr, 0, 18);
	_rtw_memset(pbuf, 0, ETH_ALEN);

	fp = filp_open(MAC_ADDRESS_FILE_PATH, O_RDONLY,  0);
	if (IS_ERR(fp))
	{
		ret = _FAIL;
		DBG_8192C("%s: Error, Read MAC address file FAIL!\n", __FUNCTION__);
	}
	else
	{
		fs = get_fs();
		set_fs(KERNEL_DS);

		vfs_read(fp, source_addr, 18, &pos);
		source_addr[17] = ':';

		head = end = source_addr;
		for (i=0; i<ETH_ALEN; i++)
		{
			while (end && (*end != ':') )
				end++;

			if (end && (*end == ':') )
				*end = '\0';

			pbuf[i] = simple_strtoul(head, NULL, 16 );

			if (end) {
				end++;
				head = end;
			}
		}
		set_fs(fs);
		filp_close(fp, NULL);

		DBG_8192C("%s: Read MAC address from file [%s]\n", __FUNCTION__, MAC_ADDRESS_FILE_PATH);
		DBG_8192C("WiFi MAC address: " MAC_FMT "\n", MAC_ARG(pbuf));
	}

	if (_rtw_memcmp(pbuf, null_mac_addr, ETH_ALEN) ||
		_rtw_memcmp(pbuf, multi_mac_addr, ETH_ALEN))
	{
		pbuf[0] = 0x00;
		pbuf[1] = 0xe0;
		pbuf[2] = 0x4c;
		pbuf[3] = (u8)(curtime & 0xff) ;
		pbuf[4] = (u8)((curtime>>8) & 0xff) ;
		pbuf[5] = (u8)((curtime>>16) & 0xff) ;
	}

	DBG_8192C("%s: Permanent Address = " MAC_FMT "\n", __FUNCTION__, MAC_ARG(pbuf));

	return ret;
}
#endif // CONFIG_EFUSE_CONFIG_FILE

void InitRDGSetting8812A(PADAPTER padapter)
{
	rtw_write8(padapter, REG_RD_CTRL, 0xFF);
	rtw_write16(padapter, REG_RD_NAV_NXT, 0x200);
	rtw_write8(padapter, REG_RD_RESP_PKT_TH, 0x05);
}

void ReadRFType8812A(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData = GET_HAL_DATA(padapter);

#if DISABLE_BB_RF
	pHalData->rf_chip = RF_PSEUDO_11N;
#else
	pHalData->rf_chip = RF_6052;
#endif

	//if (pHalData->rf_type == RF_1T1R){
	//	pHalData->bRFPathRxEnable[0] = _TRUE;
	//}
	//else {	// Default unknow type is 2T2r
	//	pHalData->bRFPathRxEnable[0] = pHalData->bRFPathRxEnable[1] = _TRUE;
	//}

	if (IsSupported24G(padapter->registrypriv.wireless_mode) && 
		IsSupported5G(padapter->registrypriv.wireless_mode))
		pHalData->BandSet = BAND_ON_BOTH;
	else if (IsSupported5G(padapter->registrypriv.wireless_mode))
		pHalData->BandSet = BAND_ON_5G;
	else
		pHalData->BandSet = BAND_ON_2_4G;

	//if(padapter->bInHctTest)
	//	pHalData->BandSet = BAND_ON_2_4G;
}

void rtl8812_GetHalODMVar(	
	PADAPTER				Adapter,
	HAL_ODM_VARIABLE		eVariable,
	PVOID					pValue1,
	PVOID					pValue2)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T podmpriv = &pHalData->odmpriv;
	switch(eVariable){
		default:
			GetHalODMVar(Adapter,eVariable,pValue1,pValue2);
			break;
	}
}

void rtl8812_SetHalODMVar(
	PADAPTER				Adapter,
	HAL_ODM_VARIABLE		eVariable,
	PVOID					pValue1,
	BOOLEAN					bSet)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T podmpriv = &pHalData->odmpriv;
	//_irqL irqL;
	switch(eVariable){
		default:
			SetHalODMVar(Adapter,eVariable,pValue1,bSet);
			break;
	}
}	

void rtl8812_start_thread(PADAPTER padapter)
{
}

void rtl8812_stop_thread(PADAPTER padapter)
{
}

void hal_notch_filter_8812(_adapter *adapter, bool enable)
{
	if (enable) {
		DBG_871X("Enable notch filter\n");
		//rtw_write8(adapter, rOFDM0_RxDSP+1, rtw_read8(adapter, rOFDM0_RxDSP+1) | BIT1);
	} else {
		DBG_871X("Disable notch filter\n");
		//rtw_write8(adapter, rOFDM0_RxDSP+1, rtw_read8(adapter, rOFDM0_RxDSP+1) & ~BIT1);
	}
}

u8
GetEEPROMSize8812A(
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

void CheckAutoloadState8812A(PADAPTER padapter)
{
	PEEPROM_EFUSE_PRIV pEEPROM;
	u8 val8;


	pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);

	/* check system boot selection */
	val8 = rtw_read8(padapter, REG_9346CR);
	pEEPROM->EepromOrEfuse = (val8 & BOOT_FROM_EEPROM) ? _TRUE : _FALSE;
	pEEPROM->bautoload_fail_flag = (val8 & EEPROM_EN) ? _FALSE : _TRUE;

	DBG_8192C("%s: 9346CR(%#x)=0x%02x, Boot from %s, Autoload %s!\n",
			__FUNCTION__, REG_9346CR, val8,
			(pEEPROM->EepromOrEfuse ? "EEPROM" : "EFUSE"),
			(pEEPROM->bautoload_fail_flag ? "Fail" : "OK"));
}

void InitPGData8812A(PADAPTER padapter)
{
	PEEPROM_EFUSE_PRIV pEEPROM;
	u32 i;
	u16 val16;


	pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);
	
#ifdef CONFIG_EFUSE_CONFIG_FILE
	{
		s32 tmp;
		u32 addr;

		tmp = _halReadPGDataFromFile(padapter, pEEPROM->efuse_eeprom_data);
		pEEPROM->bloadfile_fail_flag = ((tmp==_FAIL) ? _TRUE : _FALSE);
		tmp = _halReadMACAddrFromFile(padapter, pEEPROM->mac_addr);
		pEEPROM->bloadmac_fail_flag = ((tmp==_FAIL) ? _TRUE : _FALSE);

#ifdef CONFIG_SDIO_HCI
		addr = EEPROM_MAC_ADDR_8821AS;
#elif defined(CONFIG_USB_HCI)
		if (IS_HARDWARE_TYPE_8812AU(padapter))
			addr = EEPROM_MAC_ADDR_8812AU;
		else
			addr = EEPROM_MAC_ADDR_8821AU;
#elif defined(CONFIG_PCI_HCI)
		if (IS_HARDWARE_TYPE_8812E(padapter))
			addr = EEPROM_MAC_ADDR_8812AE;
		else
			addr = EEPROM_MAC_ADDR_8821AE;
#endif // CONFIG_PCI_HCI
		_rtw_memcpy(&pEEPROM->efuse_eeprom_data[addr], pEEPROM->mac_addr, ETH_ALEN);
	}
#else // !CONFIG_EFUSE_CONFIG_FILE

	if (_FALSE == pEEPROM->bautoload_fail_flag)
	{
		// autoload OK.
		if (is_boot_from_eeprom(padapter))
		{
			// Read all Content from EEPROM or EFUSE.
			for (i = 0; i < HWSET_MAX_SIZE_JAGUAR; i += 2)
			{
				//val16 = EF2Byte(ReadEEprom(pAdapter, (u2Byte) (i>>1)));
				//*((u16*)(&PROMContent[i])) = val16;
			}
		}
		else
		{
			// Read EFUSE real map to shadow.
			EFUSE_ShadowMapUpdate(padapter, EFUSE_WIFI, _FALSE);
		}
	}
	else
	{
		// update to default value 0xFF
		if (!is_boot_from_eeprom(padapter))
			EFUSE_ShadowMapUpdate(padapter, EFUSE_WIFI, _FALSE);
	}
#endif // !CONFIG_EFUSE_CONFIG_FILE
}

void
ReadChipVersion8812A(
	IN	PADAPTER	Adapter
	)
{
	u32	value32;
	HAL_VERSION		ChipVersion;
	PHAL_DATA_TYPE	pHalData;


	pHalData = GET_HAL_DATA(Adapter);

	value32 = rtw_read32(Adapter, REG_SYS_CFG);
	DBG_8192C("%s SYS_CFG(0x%X)=0x%08x \n", __FUNCTION__, REG_SYS_CFG, value32);

	if(IS_HARDWARE_TYPE_8812(Adapter))
		ChipVersion.ICType = CHIP_8812;
	else
		ChipVersion.ICType = CHIP_8821;

	ChipVersion.ChipType = ((value32 & RTL_ID) ? TEST_CHIP : NORMAL_CHIP);

	if (Adapter->registrypriv.rf_config == RF_MAX_TYPE) {
		if(IS_HARDWARE_TYPE_8812(Adapter))
			ChipVersion.RFType = RF_TYPE_2T2R;//RF_2T2R;
		else
			ChipVersion.RFType = RF_TYPE_1T1R;//RF_1T1R;
	}

	if(Adapter->registrypriv.special_rf_path == 1)
		ChipVersion.RFType = RF_TYPE_1T1R;	//RF_1T1R;

	if (IS_HARDWARE_TYPE_8812(Adapter))
		ChipVersion.VendorType = ((value32 & VENDOR_ID) ? CHIP_VENDOR_UMC : CHIP_VENDOR_TSMC);
	else
	{
		u32 vendor;

		vendor = (value32 & EXT_VENDOR_ID) >> EXT_VENDOR_ID_SHIFT;
		switch (vendor)
		{
			case 0:
				vendor = CHIP_VENDOR_TSMC;
				break;
			case 1:
				vendor = CHIP_VENDOR_SMIC;
				break;
			case 2:
				vendor = CHIP_VENDOR_UMC;
				break;
		}
		ChipVersion.VendorType = vendor;
	}
	ChipVersion.CUTVersion = (value32 & CHIP_VER_RTL_MASK)>>CHIP_VER_RTL_SHIFT; // IC version (CUT)
	if(IS_HARDWARE_TYPE_8812(Adapter))
		ChipVersion.CUTVersion += 1;

	//value32 = rtw_read32(Adapter, REG_GPIO_OUTSTS);
	ChipVersion.ROMVer = 0;	// ROM code version.

	// For multi-function consideration. Added by Roger, 2010.10.06.
	pHalData->MultiFunc = RT_MULTI_FUNC_NONE;
	value32 = rtw_read32(Adapter, REG_MULTI_FUNC_CTRL);
	pHalData->MultiFunc |= ((value32 & WL_FUNC_EN) ? RT_MULTI_FUNC_WIFI : 0);
	pHalData->MultiFunc |= ((value32 & BT_FUNC_EN) ? RT_MULTI_FUNC_BT : 0);
	pHalData->PolarityCtl = ((value32 & WL_HWPDN_SL) ? RT_POLARITY_HIGH_ACT : RT_POLARITY_LOW_ACT);

#if 1	
	dump_chip_info(ChipVersion);
#endif

	_rtw_memcpy(&pHalData->VersionID, &ChipVersion, sizeof(HAL_VERSION));

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
	
	DBG_8192C("RF_Type is %x!!\n", pHalData->rf_type);
}

VOID
Hal_PatchwithJaguar_8812(
	IN PADAPTER				Adapter,
	IN RT_MEDIA_STATUS		MediaStatus
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	struct mlme_ext_priv	*pmlmeext = &(Adapter->mlmeextpriv);
	struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

	if(	(MediaStatus == RT_MEDIA_CONNECT) && 
		(pmlmeinfo->assoc_AP_vendor == HT_IOT_PEER_REALTEK_JAGUAR_BCUTAP ))
	{
		rtw_write8(Adapter, rVhtlen_Use_Lsig_Jaguar, 0x1);
		rtw_write8(Adapter, REG_TCR+3, BIT2);
	}
	else
	{
		rtw_write8(Adapter, rVhtlen_Use_Lsig_Jaguar, 0x3F);
		rtw_write8(Adapter, REG_TCR+3, BIT0|BIT1|BIT2);
	}


	if(	(MediaStatus == RT_MEDIA_CONNECT) && 
		((pmlmeinfo->assoc_AP_vendor == HT_IOT_PEER_REALTEK_JAGUAR_BCUTAP) ||
		 (pmlmeinfo->assoc_AP_vendor == HT_IOT_PEER_REALTEK_JAGUAR_CCUTAP)))
	{
		pHalData->Reg837 |= BIT2;
		rtw_write8(Adapter, rBWIndication_Jaguar+3, pHalData->Reg837);
	}
	else
	{
		pHalData->Reg837 &= (~BIT2);
		rtw_write8(Adapter, rBWIndication_Jaguar+3, pHalData->Reg837);
	}
}

void UpdateHalRAMask8812A(PADAPTER padapter, u32 mac_id, u8 rssi_level)
{
	u32	mask,rate_bitmap;
	u8	shortGIrate = _FALSE;
	u8	arg[4] = {0};
	struct sta_info	*psta;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(padapter);
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

	rtl8812_set_raid_cmd(padapter, mask, arg);
}

void InitDefaultValue8821A(PADAPTER padapter)
{
	PHAL_DATA_TYPE pHalData;
	struct pwrctrl_priv *pwrctrlpriv;
	struct dm_priv *pdmpriv;
	u8 i;


	pHalData = GET_HAL_DATA(padapter);
	pwrctrlpriv = adapter_to_pwrctl(padapter);
	pdmpriv = &pHalData->dmpriv;

	// init default value
	pHalData->fw_ractrl = _FALSE;		
	if (!pwrctrlpriv->bkeepfwalive)
		pHalData->LastHMEBoxNum = 0;	

	/* hal capability values */
	#ifdef CONFIG_RTL8821A
	if(IS_HARDWARE_TYPE_8821(padapter)) {
		pHalData->macid_num = MACID_NUM_8821A;
		pHalData->cam_entry_num = CAM_ENTRY_NUM_8821A;
	} else
	#endif
	{
		pHalData->macid_num = MACID_NUM_8812A;
		pHalData->cam_entry_num = CAM_ENTRY_NUM_8812A;
	}

	// init dm default value
	pHalData->bChnlBWInitialized = _FALSE;
	pHalData->odmpriv.RFCalibrateInfo.bIQKInitialized = _FALSE;
	pHalData->odmpriv.RFCalibrateInfo.TM_Trigger = 0;//for IQK
	pHalData->pwrGroupCnt = 0;
	pHalData->PGMaxGroup = MAX_PG_GROUP;
	pHalData->odmpriv.RFCalibrateInfo.ThermalValue_HP_index = 0;
	for (i = 0; i < HP_THERMAL_NUM; i++)
		pHalData->odmpriv.RFCalibrateInfo.ThermalValue_HP[i] = 0;
	pHalData->EfuseHal.fakeEfuseBank = 0;
	pHalData->EfuseHal.fakeEfuseUsedBytes = 0;
	_rtw_memset(pHalData->EfuseHal.fakeEfuseContent, 0xFF, EFUSE_MAX_HW_SIZE);
	_rtw_memset(pHalData->EfuseHal.fakeEfuseInitMap, 0xFF, EFUSE_MAX_MAP_LEN);
	_rtw_memset(pHalData->EfuseHal.fakeEfuseModifiedMap, 0xFF, EFUSE_MAX_MAP_LEN);
}

VOID
_InitBeaconParameters_8812A(
	IN  PADAPTER Adapter
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);

	rtw_write16(Adapter, REG_BCN_CTRL, 0x1010);

	// TODO: Remove these magic number
	rtw_write16(Adapter, REG_TBTT_PROHIBIT,0x6404);// ms
	rtw_write8(Adapter, REG_DRVERLYINT, DRIVER_EARLY_INT_TIME_8812);// 5ms
	rtw_write8(Adapter, REG_BCNDMATIM, BCN_DMA_ATIME_INT_TIME_8812); // 2ms

	// Suggested by designer timchen. Change beacon AIFS to the largest number
	// beacause test chip does not contension before sending beacon. by tynli. 2009.11.03
	rtw_write16(Adapter, REG_BCNTCFG, 0x660F);

	pHalData->RegBcnCtrlVal = rtw_read8(Adapter, REG_BCN_CTRL);
	pHalData->RegTxPause = rtw_read8(Adapter, REG_TXPAUSE); 
	pHalData->RegFwHwTxQCtrl = rtw_read8(Adapter, REG_FWHW_TXQ_CTRL+2);
	pHalData->RegReg542 = rtw_read8(Adapter, REG_TBTT_PROHIBIT+2);
	pHalData->RegCR_1 = rtw_read8(Adapter, REG_CR+1);
}

static VOID
_BeaconFunctionEnable(
	IN	PADAPTER		Adapter,
	IN	BOOLEAN			Enable,
	IN	BOOLEAN			Linked
	)
{
	rtw_write8(Adapter, REG_BCN_CTRL, (BIT4 | BIT3 | BIT1));
	//SetBcnCtrlReg(Adapter, (BIT4 | BIT3 | BIT1), 0x00);
	//RT_TRACE(COMP_BEACON, DBG_LOUD, ("_BeaconFunctionEnable 0x550 0x%x\n", PlatformEFIORead1Byte(Adapter, 0x550)));			

	rtw_write8(Adapter, REG_RD_CTRL+1, 0x6F);	
}

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

	rtw_write8(padapter, REG_FWHW_TXQ_CTRL+2, (pHalData->RegFwHwTxQCtrl) & (~BIT6));
	pHalData->RegFwHwTxQCtrl &= (~BIT6);
	rtw_write8(padapter, REG_TBTT_PROHIBIT+1, 0x64);
	pHalData->RegReg542 &= ~(BIT0);
	rtw_write8(padapter, REG_TBTT_PROHIBIT+2, pHalData->RegReg542);

	 //todo: CheckFwRsvdPageContent(Adapter);  // 2010.06.23. Added by tynli.
}

void SetBeaconRelatedRegisters8812A(PADAPTER padapter)
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
	rtw_write16(padapter, REG_BCN_INTERVAL, pmlmeinfo->bcn_interval);
	rtw_write8(padapter, REG_ATIMWND, 0x02);// 2ms

	_InitBeaconParameters_8812A(padapter);

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

#ifdef CONFIG_BEAMFORMING
VOID
SetBeamformingCLK_8812(
	IN 	PADAPTER			Adapter
	)
{
	struct pwrctrl_priv	*pwrpriv = adapter_to_pwrctl(Adapter);
	u16	u2btmp;
	u8	Count = 0, u1btmp;

	DBG_871X(" ==>%s\n", __FUNCTION__);

	if ( (check_fwstate(&Adapter->mlmepriv, _FW_UNDER_SURVEY)==_TRUE)
#ifdef CONFIG_CONCURRENT_MODE
		|| (check_buddy_fwstate(Adapter, _FW_UNDER_SURVEY) == _TRUE)
#endif
		)
	{
		DBG_871X(" <==%s return by Scan\n", __FUNCTION__);
		return;
	}	
	
	// Stop Usb TxDMA
	rtw_write_port_cancel(Adapter);

#ifdef CONFIG_PCI_HCI
	// Stop PCIe TxDMA
	rtw_write8(Adapter, REG_PCIE_CTRL_REG+1, 0xFE);
#endif

	// Wait TXFF empty
	for(Count = 0; Count < 100; Count++)
	{
		u2btmp = rtw_read16(Adapter, REG_TXPKT_EMPTY);
		u2btmp = u2btmp & 0xfff;
		if(u2btmp != 0xfff)
		{
			rtw_mdelay_os(10);
			continue;
		}
		else
			break;
	}

	DBG_871X(" Tx Empty count %d \n", Count);

	// TX pause
	rtw_write8(Adapter, REG_TXPAUSE, 0xFF);

	// Wait TX State Machine OK
	for(Count = 0; Count < 100; Count++)
	{
		if(rtw_read32(Adapter, REG_SCH_TXCMD_8812) != 0)
			continue;
		else 
			break;
	}

	DBG_871X(" Tx Status count %d \n", Count);
	
	// Stop RX DMA path
	u1btmp = rtw_read8(Adapter, REG_RXDMA_CONTROL_8812);
	rtw_write8(Adapter, REG_RXDMA_CONTROL_8812, u1btmp| BIT2);

	for(Count = 0; Count < 100; Count++)
	{
		u1btmp = rtw_read8(Adapter, REG_RXDMA_CONTROL_8812);
		if(u1btmp & BIT1)
			break;
		else
			rtw_mdelay_os(10);
	}

	DBG_871X(" Rx Empty count %d \n", Count);
	
	// Disable clock
	rtw_write8(Adapter, REG_SYS_CLKR+1, 0xf0);
	// Disable 320M
	rtw_write8(Adapter, REG_AFE_PLL_CTRL+3, 0x8);
	// Enable 320M
	rtw_write8(Adapter, REG_AFE_PLL_CTRL+3, 0xa);
	// Enable clock
	rtw_write8(Adapter, REG_SYS_CLKR+1, 0xfc);

	// Release Tx pause
	rtw_write8(Adapter, REG_TXPAUSE, 0);

	// Enable RX DMA path
	u1btmp = rtw_read8(Adapter, REG_RXDMA_CONTROL_8812);
	rtw_write8(Adapter, REG_RXDMA_CONTROL_8812, u1btmp & (~ BIT2));

	// Start Usb TxDMA
	RTW_ENABLE_FUNC(Adapter, DF_TX_BIT);
	DBG_871X("%s \n", __FUNCTION__);

	DBG_871X("<==%s\n", __FUNCTION__);
}

VOID
SetBeamformRfMode_8812(
	IN PADAPTER				Adapter,
	IN struct beamforming_info	*pBeamInfo
	)
{
	BOOLEAN				bSelfBeamformer = _FALSE;
	BOOLEAN				bSelfBeamformee = _FALSE;
	BEAMFORMING_CAP	BeamformCap = BEAMFORMING_CAP_NONE;

	BeamformCap = beamforming_get_beamform_cap(pBeamInfo);

	if(BeamformCap == pBeamInfo->beamforming_cap)
		return;
	else 
		pBeamInfo->beamforming_cap = BeamformCap;

	if(GET_RF_TYPE(Adapter) == RF_1T1R)
		return;

	bSelfBeamformer = BeamformCap & BEAMFORMER_CAP;
	bSelfBeamformee = BeamformCap & BEAMFORMEE_CAP;

	PHY_SetRFReg(Adapter, ODM_RF_PATH_A, RF_WeLut_Jaguar, 0x80000,0x1); // RF Mode table write enable
	PHY_SetRFReg(Adapter, ODM_RF_PATH_B, RF_WeLut_Jaguar, 0x80000,0x1); // RF Mode table write enable

	if(bSelfBeamformer)
	{	
		// Paath_A
		PHY_SetRFReg(Adapter, ODM_RF_PATH_A, RF_ModeTableAddr, 0x78000,0x3); // Select RX mode
		PHY_SetRFReg(Adapter, ODM_RF_PATH_A, RF_ModeTableData0, 0xfffff,0x3F7FF); // Set Table data
		PHY_SetRFReg(Adapter, ODM_RF_PATH_A, RF_ModeTableData1, 0xfffff,0xE26BF); // Enable TXIQGEN in RX mode
		// Path_B
		PHY_SetRFReg(Adapter, ODM_RF_PATH_B, RF_ModeTableAddr, 0x78000, 0x3); // Select RX mode
		PHY_SetRFReg(Adapter, ODM_RF_PATH_B, RF_ModeTableData0, 0xfffff,0x3F7FF); // Set Table data
		PHY_SetRFReg(Adapter, ODM_RF_PATH_B, RF_ModeTableData1, 0xfffff,0xE26BF); // Enable TXIQGEN in RX mode
	}
	else
	{
		// Paath_A
		PHY_SetRFReg(Adapter, ODM_RF_PATH_A, RF_ModeTableAddr, 0x78000, 0x3); // Select RX mode
		PHY_SetRFReg(Adapter, ODM_RF_PATH_A, RF_ModeTableData0, 0xfffff,0x3F7FF); // Set Table data
		PHY_SetRFReg(Adapter, ODM_RF_PATH_A, RF_ModeTableData1, 0xfffff,0xC26BF); // Disable TXIQGEN in RX mode
		// Path_B
		PHY_SetRFReg(Adapter, ODM_RF_PATH_B, RF_ModeTableAddr, 0x78000, 0x3); // Select RX mode
		PHY_SetRFReg(Adapter, ODM_RF_PATH_B, RF_ModeTableData0, 0xfffff,0x3F7FF); // Set Table data
		PHY_SetRFReg(Adapter, ODM_RF_PATH_B, RF_ModeTableData1, 0xfffff,0xC26BF); // Disable TXIQGEN in RX mode
	}

	PHY_SetRFReg(Adapter, ODM_RF_PATH_A, RF_WeLut_Jaguar, 0x80000,0x0); // RF Mode table write disable
	PHY_SetRFReg(Adapter, ODM_RF_PATH_B, RF_WeLut_Jaguar, 0x80000,0x0); // RF Mode table write disable

	if(bSelfBeamformer)
		PHY_SetBBReg(Adapter, rTxPath_Jaguar, bMaskByte1, 0x33);
	else
		PHY_SetBBReg(Adapter, rTxPath_Jaguar, bMaskByte1, 0x11);
}



VOID
SetBeamformEnter_8812(
	IN PADAPTER				Adapter,
	IN u8					Idx
	)
{
	u8	i = 0;
	u32	CSI_Param;
	HAL_DATA_TYPE			*pHalData = GET_HAL_DATA(Adapter);
	struct mlme_priv			*pmlmepriv = &(Adapter->mlmepriv);
	struct beamforming_info	*pBeamInfo = GET_BEAMFORM_INFO(pmlmepriv);
	struct beamforming_entry	BeamformEntry = pBeamInfo->beamforming_entry[Idx];
	u16	STAid = 0;

	SetBeamformRfMode_8812(Adapter, pBeamInfo);

	if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) || check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE))
		STAid = BeamformEntry.mac_id;
	else 
		STAid = BeamformEntry.p_aid;

	// Sounding protocol control
	rtw_write8(Adapter, REG_SND_PTCL_CTRL_8812, 0xCB);

	// MAC addresss/Partial AID of Beamformer
	if(Idx == 0)
	{
		for(i = 0; i < 6 ; i++)
			rtw_write8(Adapter, (REG_BFMER0_INFO_8812+i), BeamformEntry.mac_addr[i]);
		// CSI report use legacy ofdm so don't need to fill P_AID.
		//rtw_write16(Adapter, REG_BFMER0_INFO_8812+6, BeamformEntry.P_AID);
	}
	else
	{
		for(i = 0; i < 6 ; i++)
			rtw_write8(Adapter, (REG_BFMER1_INFO_8812+i), BeamformEntry.mac_addr[i]);
		// CSI report use legacy ofdm so don't need to fill P_AID.
		//rtw_write16(Adapter, REG_BFMER1_INFO_8812+6, BeamformEntry.P_AID);
	}

	// CSI report parameters of Beamformee 
	if(	(BeamformEntry.beamforming_entry_cap & BEAMFORMEE_CAP_VHT_SU) ||
		(BeamformEntry.beamforming_entry_cap & BEAMFORMER_CAP_VHT_SU) )
	{
		if(pHalData->rf_type == RF_2T2R)
			CSI_Param = 0x01090109;
		else
			CSI_Param = 0x01080108;
	}	
	else 
	{
		if(pHalData->rf_type == RF_2T2R)
			CSI_Param = 0x03090309;
		else
			CSI_Param = 0x03080308;
	}	

	if(pHalData->rf_type == RF_2T2R)
		rtw_write32(Adapter, 0x9B4, 0x00000000);	// Nc =2
	else
		rtw_write32(Adapter, 0x9B4, 0x01081008); // Nc =1

	rtw_write32(Adapter, REG_CSI_RPT_PARAM_BW20_8812, CSI_Param);
	rtw_write32(Adapter, REG_CSI_RPT_PARAM_BW40_8812, CSI_Param);
	rtw_write32(Adapter, REG_CSI_RPT_PARAM_BW80_8812, CSI_Param);

	// P_AID of Beamformee & enable NDPA transmission & enable NDPA interrupt
	if(Idx == 0)
	{	
		rtw_write16(Adapter, REG_TXBF_CTRL_8812, STAid);	
		rtw_write8(Adapter, REG_TXBF_CTRL_8812+3, rtw_read8(Adapter, REG_TXBF_CTRL_8812+3)|BIT4|BIT6|BIT7);
	}	
	else
	{
		rtw_write16(Adapter, REG_TXBF_CTRL_8812+2, STAid |BIT12 | BIT14|BIT15);
	}	

	// CSI report parameters of Beamformee
	if(Idx == 0)	
	{
		// Get BIT24 & BIT25
		u8	tmp = rtw_read8(Adapter, REG_BFMEE_SEL_8812+3) & 0x3;
	
		rtw_write8(Adapter, REG_BFMEE_SEL_8812+3, tmp | 0x60);
		rtw_write16(Adapter, REG_BFMEE_SEL_8812, STAid | BIT9);
	}	
	else
	{
		// Set BIT25
		rtw_write16(Adapter, REG_BFMEE_SEL_8812+2, STAid | (0xE2 << 8));
	}

	// Timeout value for MAC to leave NDP_RX_standby_state (60 us, Test chip) (80 us,  MP chip)
	rtw_write8(Adapter, REG_SND_PTCL_CTRL_8812+3, 0x50);

	beamforming_notify(Adapter);
}


VOID
SetBeamformLeave_8812(
	IN PADAPTER				Adapter,
	IN u8					Idx
	)
{
	struct beamforming_info	*pBeamInfo = GET_BEAMFORM_INFO(&(Adapter->mlmepriv));

	SetBeamformRfMode_8812(Adapter, pBeamInfo);

	/*	Clear P_AID of Beamformee
	* 	Clear MAC addresss of Beamformer
	*	Clear Associated Bfmee Sel
	*/
	if(pBeamInfo->beamforming_cap == BEAMFORMING_CAP_NONE)
		rtw_write8(Adapter, REG_SND_PTCL_CTRL_8812, 0xC8);	

	if(Idx == 0)
	{	
		rtw_write16(Adapter, REG_TXBF_CTRL_8812, 0);	

		rtw_write32(Adapter, REG_BFMER0_INFO_8812, 0);
		rtw_write16(Adapter, REG_BFMER0_INFO_8812+4, 0);

		rtw_write16(Adapter, REG_BFMEE_SEL_8812, 0);
	}	
	else
	{
		rtw_write16(	Adapter, REG_TXBF_CTRL_8812+2, rtw_read16(Adapter, REG_TXBF_CTRL_8812+2) & 0xF000);

		rtw_write32(Adapter, REG_BFMER1_INFO_8812, 0);
		rtw_write16(Adapter, REG_BFMER1_INFO_8812+4, 0);

		rtw_write16(	Adapter, REG_BFMEE_SEL_8812+2, rtw_read16(Adapter, REG_BFMEE_SEL_8812+2) & 0x60);
	}
}


VOID
SetBeamformStatus_8812(
	IN PADAPTER				Adapter,
	IN u8					Idx
	)
{
	u16	BeamCtrlVal;
	u32	BeamCtrlReg;
	struct mlme_priv			*pmlmepriv = &(Adapter->mlmepriv);
	struct beamforming_info	*pBeamInfo = GET_BEAMFORM_INFO(pmlmepriv);
	struct beamforming_entry	BeamformEntry = pBeamInfo->beamforming_entry[Idx];

	if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE) || check_fwstate(pmlmepriv, WIFI_ADHOC_MASTER_STATE))
		BeamCtrlVal = BeamformEntry.mac_id;
	else 
		BeamCtrlVal = BeamformEntry.p_aid;

	if(Idx == 0)
		BeamCtrlReg = REG_TXBF_CTRL_8812;
	else
	{
		BeamCtrlReg = REG_TXBF_CTRL_8812+2;
		BeamCtrlVal |= BIT12 | BIT14|BIT15;
	}

	if(BeamformEntry.beamforming_entry_state == BEAMFORMING_ENTRY_STATE_PROGRESSED)
	{
		if(BeamformEntry.sound_bw == CHANNEL_WIDTH_20)
			BeamCtrlVal |= BIT9;
		else if(BeamformEntry.sound_bw == CHANNEL_WIDTH_40)
			BeamCtrlVal |= BIT10;
		else if(BeamformEntry.sound_bw == CHANNEL_WIDTH_80)
			BeamCtrlVal |= BIT11;
	}
	else
	{
		BeamCtrlVal &= ~(BIT9|BIT10|BIT11);
	}

	rtw_write16(Adapter, BeamCtrlReg, BeamCtrlVal);

	DBG_871X("%s Idx %d BeamCtrlReg %x BeamCtrlVal %x\n", __FUNCTION__, Idx, BeamCtrlReg, BeamCtrlVal);
}


VOID
SetBeamformFwTxBFCmd_8812(
	IN	PADAPTER	Adapter
	)
{
	u8	Idx, Period0 = 0, Period1 = 0;
	u8	PageNum0 = 0xFF, PageNum1 = 0xFF;
	u8	u1TxBFParm[3]={0};

	struct mlme_priv			*pmlmepriv = &(Adapter->mlmepriv);
	struct beamforming_info	*pBeamInfo = GET_BEAMFORM_INFO(pmlmepriv);

	for(Idx = 0; Idx < BEAMFORMING_ENTRY_NUM; Idx++)
	{
		if(pBeamInfo->beamforming_entry[Idx].beamforming_entry_state == BEAMFORMING_ENTRY_STATE_PROGRESSED)
		{
			if(Idx == 0)
			{
				if(pBeamInfo->beamforming_entry[Idx].bSound)
					PageNum0 = 0xFE;
				else
					PageNum0 = 0xFF; //stop sounding
				Period0 = (u8)(pBeamInfo->beamforming_entry[Idx].sound_period);
			}	
			else if(Idx == 1)
			{
				if(pBeamInfo->beamforming_entry[Idx].bSound)
					PageNum1 = 0xFE;
				else
					PageNum1 = 0xFF; //stop sounding
				Period1 = (u8)(pBeamInfo->beamforming_entry[Idx].sound_period);
			}	
		}
	}

	u1TxBFParm[0] = PageNum0;
	u1TxBFParm[1] = PageNum1;
	u1TxBFParm[2] = (Period1 << 4) | Period0;
	FillH2CCmd_8812(Adapter, H2C_8812_TxBF, 3, u1TxBFParm);
	
	DBG_871X("%s PageNum0 = %d Period0 = %d\n", __FUNCTION__, PageNum0, Period0);
	DBG_871X("PageNum1 = %d Period1 %d\n", PageNum1, Period1);
}


VOID
SetBeamformDownloadNDPA_8812(
	IN	PADAPTER			Adapter,
	IN	u8					Idx
	)
{
	u8	u1bTmp=0, tmpReg422=0, Head_Page;	
	u8	BcnValidReg=0, count=0, DLBcnCount=0;
	BOOLEAN			bSendBeacon=_FALSE;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u8	TxPageBndy= LAST_ENTRY_OF_TX_PKT_BUFFER_8812; // default reseved 1 page for the IC type which is undefined.
	struct beamforming_info	*pBeamInfo = GET_BEAMFORM_INFO(&(Adapter->mlmepriv));
	struct beamforming_entry	*pBeamEntry = pBeamInfo->beamforming_entry+Idx;

	//pHalData->bFwDwRsvdPageInProgress = _TRUE;

	if(Idx == 0)
		Head_Page = 0xFE;
	else
		Head_Page = 0xFE;

	rtw_hal_get_def_var(Adapter, HAL_DEF_TX_PAGE_BOUNDARY, (u8 *)&TxPageBndy);
	
	//Set REG_CR bit 8. DMA beacon by SW.
	u1bTmp = rtw_read8(Adapter, REG_CR+1);
	rtw_write8(Adapter,  REG_CR+1, (u1bTmp|BIT0));

	pHalData->RegCR_1 |= BIT0;
	rtw_write8(Adapter,  REG_CR+1, pHalData->RegCR_1);

	// Set FWHW_TXQ_CTRL 0x422[6]=0 to tell Hw the packet is not a real beacon frame.
	tmpReg422 = rtw_read8(Adapter, REG_FWHW_TXQ_CTRL+2);
	rtw_write8(Adapter, REG_FWHW_TXQ_CTRL+2,  tmpReg422&(~BIT6));

	if(tmpReg422&BIT6)
	{
		DBG_871X("SetBeamformDownloadNDPA_8812(): There is an Adapter is sending beacon.\n");
		bSendBeacon = _TRUE;
	}

	// 	TDECTRL[15:8] 0x209[7:0] = 0xF6	Beacon Head for TXDMA
	rtw_write8(Adapter,REG_TDECTRL+1, Head_Page);
	
	do
	{		
		// Clear beacon valid check bit.
		BcnValidReg = rtw_read8(Adapter, REG_TDECTRL+2);
		rtw_write8(Adapter, REG_TDECTRL+2, (BcnValidReg|BIT0));
		
		// download NDPA rsvd page.
		if(pBeamEntry->beamforming_entry_cap & BEAMFORMER_CAP_VHT_SU)
			beamforming_send_vht_ndpa_packet(Adapter,pBeamEntry->mac_addr,pBeamEntry->aid,pBeamEntry->sound_bw, BCN_QUEUE_INX);
		else 
			beamforming_send_ht_ndpa_packet(Adapter,pBeamEntry->mac_addr,pBeamEntry->sound_bw, BCN_QUEUE_INX);

		// check rsvd page download OK.
		BcnValidReg = rtw_read8(Adapter, REG_TDECTRL+2);
		count=0;
		while(!(BcnValidReg & BIT0) && count <20)
		{
			count++;
			rtw_udelay_os(10);
			BcnValidReg = rtw_read8(Adapter, REG_TDECTRL+2);
		}
		DLBcnCount++;
	}while(!(BcnValidReg&BIT0) && DLBcnCount<5);
	
	if(!(BcnValidReg&BIT0))
		DBG_871X("%s Download RSVD page failed!\n", __FUNCTION__);

	// 	TDECTRL[15:8] 0x209[7:0] = 0xF6	Beacon Head for TXDMA
	rtw_write8(Adapter,REG_TDECTRL+1, TxPageBndy);

	// To make sure that if there exists an adapter which would like to send beacon.
	// If exists, the origianl value of 0x422[6] will be 1, we should check this to
	// prevent from setting 0x422[6] to 0 after download reserved page, or it will cause 
	// the beacon cannot be sent by HW.
	// 2010.06.23. Added by tynli.
	if(bSendBeacon)
	{
		rtw_write8(Adapter, REG_FWHW_TXQ_CTRL+2, tmpReg422);
	}

	// Do not enable HW DMA BCN or it will cause Pcie interface hang by timing issue. 2011.11.24. by tynli.
	// Clear CR[8] or beacon packet will not be send to TxBuf anymore.
	u1bTmp = rtw_read8(Adapter, REG_CR+1);
	rtw_write8(Adapter, REG_CR+1, (u1bTmp&(~BIT0)));

	pBeamEntry->beamforming_entry_state = BEAMFORMING_ENTRY_STATE_PROGRESSED;

	//pHalData->bFwDwRsvdPageInProgress = _FALSE;
}

VOID
SetBeamformFwTxBF_8812(
	IN	PADAPTER			Adapter,
	IN	u8					Idx
	)
{
	struct beamforming_info	*pBeamInfo = GET_BEAMFORM_INFO(&(Adapter->mlmepriv));
	struct beamforming_entry	*pBeamEntry = pBeamInfo->beamforming_entry+Idx;

	if(pBeamEntry->beamforming_entry_state == BEAMFORMING_ENTRY_STATE_PROGRESSING)
		SetBeamformDownloadNDPA_8812(Adapter, Idx);

	SetBeamformFwTxBFCmd_8812(Adapter);
}


VOID
SetBeamformPatch_8812(
	IN	PADAPTER			Adapter,
	IN	u8					Operation
	)
{
	HAL_DATA_TYPE			*pHalData = GET_HAL_DATA(Adapter);
	struct beamforming_info	*pBeamInfo = GET_BEAMFORM_INFO(&(Adapter->mlmepriv));
	
	if(pBeamInfo->beamforming_cap == BEAMFORMING_CAP_NONE)
		return;
	
	/*if(Operation == SCAN_OPT_BACKUP_BAND0)
	{
		rtw_write8(Adapter, REG_SND_PTCL_CTRL_8812, 0xC8);	
	}
	else if(Operation == SCAN_OPT_RESTORE)
	{
		rtw_write8(Adapter, REG_SND_PTCL_CTRL_8812, 0xCB);
	}*/
}


#endif 


static void hw_var_set_opmode(PADAPTER Adapter, u8 variable, u8* val)
{
	u8	val8;	
	u8	mode = *((u8 *)val);
	u32	value_rcr;

#ifdef CONFIG_CONCURRENT_MODE
	if(Adapter->iface_type == IFACE_PORT1)
	{
		// disable Port1 TSF update
		rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1)|DIS_TSF_UDT);
		
		// set net_type
		val8 = rtw_read8(Adapter, MSR)&0x03;
		val8 |= (mode<<2);
		rtw_write8(Adapter, MSR, val8);
		
		DBG_871X("%s()-%d mode = %d\n", __FUNCTION__, __LINE__, mode);

		if((mode == _HW_STATE_STATION_) || (mode == _HW_STATE_NOLINK_))
		{
			if(!check_buddy_mlmeinfo_state(Adapter, WIFI_FW_AP_STATE))
			{
				StopTxBeacon(Adapter);
#ifdef CONFIG_PCI_HCI
				UpdateInterruptMask8812AE( Adapter, 0, 0, RT_BCN_INT_MASKS, 0);
#else
				#ifdef CONFIG_INTERRUPT_BASED_TXBCN	

				#ifdef  CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT	
				rtw_write8(Adapter, REG_DRVERLYINT, 0x05);//restore early int time to 5ms
				UpdateInterruptMask8812AU(Adapter,_TRUE, 0, IMR_BCNDMAINT0_8812);	
				#endif // CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
				
				#ifdef CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR		
				UpdateInterruptMask8812AU(Adapter,_TRUE ,0, (IMR_TXBCN0ERR_8812|IMR_TXBCN0OK_8812));
				#endif// CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR
 
				#endif //CONFIG_INTERRUPT_BASED_TXBCN
#endif
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
#ifdef CONFIG_PCI_HCI
			UpdateInterruptMask8812AE( Adapter, RT_BCN_INT_MASKS, 0, 0, 0);
#else
	#ifdef CONFIG_INTERRUPT_BASED_TXBCN			
			#ifdef  CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
			UpdateInterruptMask8812AU(Adapter,_TRUE ,IMR_BCNDMAINT0_8812, 0);
			#endif//CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT

			#ifdef CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR	
			UpdateInterruptMask8812AU(Adapter,_TRUE ,(IMR_TXBCN0ERR_8812|IMR_TXBCN0OK_8812), 0);
			#endif//CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR

	#endif //CONFIG_INTERRUPT_BASED_TXBCN
#endif

			ResumeTxBeacon(Adapter);
					
			rtw_write8(Adapter, REG_BCN_CTRL_1, 0x12);

			//Set RCR
			//rtw_write32(padapter, REG_RCR, 0x70002a8e);//CBSSID_DATA must set to 0
			//rtw_write32(Adapter, REG_RCR, 0x7000228e);//CBSSID_DATA must set to 0
			//rtw_write32(Adapter, REG_RCR, 0x7000208e);//CBSSID_DATA must set to 0,reject ICV_ERR packet
			value_rcr = rtw_read32(Adapter, REG_RCR);
			value_rcr &= ~(RCR_CBSSID_DATA);//Clear CBSSID_DATA
			rtw_write32(Adapter, REG_RCR, value_rcr);

			//enable to rx data frame
			rtw_write16(Adapter, REG_RXFLTMAP2, 0xFFFF);

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
			rtw_write8(Adapter, REG_BCN_CTRL_1, (DIS_TSF_UDT|EN_BCN_FUNCTION | EN_TXBCN_RPT|DIS_BCNQ_SUB));

			if(IS_HARDWARE_TYPE_8821(Adapter))
			{
				// select BCN on port 1
				rtw_write8(Adapter, REG_CCK_CHECK_8812,	 rtw_read8(Adapter, REG_CCK_CHECK_8812)|BIT(5));
			}	

#ifdef CONFIG_CONCURRENT_MODE
			if(check_buddy_fwstate(Adapter, WIFI_FW_NULL_STATE))
				rtw_write8(Adapter, REG_BCN_CTRL, 
					rtw_read8(Adapter, REG_BCN_CTRL) & ~EN_BCN_FUNCTION);
#endif
			//BCN1 TSF will sync to BCN0 TSF with offset(0x518) if if1_sta linked
			//rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1)|BIT(5));
			//rtw_write8(Adapter, REG_DUAL_TSF_RST, BIT(3));
					
			//dis BCN0 ATIM  WND if if1 is station
			rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)|DIS_ATIM);

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
	else //else for port0
#endif // CONFIG_CONCURRENT_MODE
	{
		// disable Port0 TSF update
		rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)|DIS_TSF_UDT);
		
		// set net_type
		val8 = rtw_read8(Adapter, MSR)&0x0c;
		val8 |= mode;
		rtw_write8(Adapter, MSR, val8);
		
		DBG_871X("%s()-%d mode = %d\n", __FUNCTION__, __LINE__, mode);
		
		if((mode == _HW_STATE_STATION_) || (mode == _HW_STATE_NOLINK_))
		{
#ifdef CONFIG_CONCURRENT_MODE
			if(!check_buddy_mlmeinfo_state(Adapter, WIFI_FW_AP_STATE))		
#endif // CONFIG_CONCURRENT_MODE
			{
				StopTxBeacon(Adapter);
#ifdef CONFIG_PCI_HCI
				UpdateInterruptMask8812AE( Adapter, 0, 0, RT_BCN_INT_MASKS, 0);
#else
			#ifdef CONFIG_INTERRUPT_BASED_TXBCN
				#ifdef  CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
				rtw_write8(Adapter, REG_DRVERLYINT, 0x05);//restore early int time to 5ms					
				UpdateInterruptMask8812AU(Adapter,_TRUE, 0, IMR_BCNDMAINT0_8812);	
				#endif//CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
				
				#ifdef CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR		
				UpdateInterruptMask8812AU(Adapter,_TRUE ,0, (IMR_TXBCN0ERR_8812|IMR_TXBCN0OK_8812));
				#endif //CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR
					
			#endif //CONFIG_INTERRUPT_BASED_TXBCN
#endif
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
#ifdef CONFIG_PCI_HCI
			UpdateInterruptMask8812AE( Adapter, RT_BCN_INT_MASKS, 0, 0, 0);
#else
	#ifdef CONFIG_INTERRUPT_BASED_TXBCN			
			#ifdef  CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
			UpdateInterruptMask8812AU(Adapter,_TRUE ,IMR_BCNDMAINT0_8812, 0);
			#endif//CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT

			#ifdef CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR	
			UpdateInterruptMask8812AU(Adapter,_TRUE ,(IMR_TXBCN0ERR_8812|IMR_TXBCN0OK_8812), 0);
			#endif//CONFIG_INTERRUPT_BASED_TXBCN_BCN_OK_ERR
					
	#endif //CONFIG_INTERRUPT_BASED_TXBCN
#endif

			ResumeTxBeacon(Adapter);

			rtw_write8(Adapter, REG_BCN_CTRL, 0x12);

			//Set RCR
			//rtw_write32(padapter, REG_RCR, 0x70002a8e);//CBSSID_DATA must set to 0
			//rtw_write32(Adapter, REG_RCR, 0x7000228e);//CBSSID_DATA must set to 0
			//rtw_write32(Adapter, REG_RCR, 0x7000208e);//CBSSID_DATA must set to 0,reject ICV_ERR packet
			value_rcr = rtw_read32(Adapter, REG_RCR);
			value_rcr &= ~(RCR_CBSSID_DATA);//Clear CBSSID_DATA
			rtw_write32(Adapter, REG_RCR, value_rcr);

			//enable to rx data frame
			rtw_write16(Adapter, REG_RXFLTMAP2, 0xFFFF);

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
			rtw_write8(Adapter, REG_BCN_CTRL, (DIS_TSF_UDT|EN_BCN_FUNCTION | EN_TXBCN_RPT|DIS_BCNQ_SUB));

			if(IS_HARDWARE_TYPE_8821(Adapter))
			{
				// select BCN on port 0
				rtw_write8(Adapter, REG_CCK_CHECK_8812,	rtw_read8(Adapter, REG_CCK_CHECK_8812)&(~BIT(5)));				
			}

#ifdef CONFIG_CONCURRENT_MODE
			if(check_buddy_fwstate(Adapter, WIFI_FW_NULL_STATE))
				rtw_write8(Adapter, REG_BCN_CTRL_1, 
					rtw_read8(Adapter, REG_BCN_CTRL_1) & ~EN_BCN_FUNCTION);
#endif

			//dis BCN1 ATIM  WND if if2 is station
			rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1)|DIS_ATIM);
#ifdef CONFIG_TSF_RESET_OFFLOAD
			// Reset TSF for STA+AP concurrent mode
			if ( check_buddy_fwstate(Adapter, (WIFI_STATION_STATE|WIFI_ASOC_STATE)) ) {
				if (reset_tsf(Adapter, IFACE_PORT0) == _FALSE)
					DBG_871X("ERROR! %s()-%d: Reset port0 TSF fail\n",
						__FUNCTION__, __LINE__);
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
		rtw_write8(GET_PRIMARY_ADAPTER(Adapter), (reg_macid+idx), val[idx]);
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
		rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1)&(~BIT(3)));
							
		rtw_write32(Adapter, REG_TSFTR1, tsf);
		rtw_write32(Adapter, REG_TSFTR1+4, tsf>>32);


		//enable related TSF function
		rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1)|BIT(3));	

		// Update buddy port's TSF if it is SoftAP for beacon TX issue!
		if ( (pmlmeinfo->state&0x03) == WIFI_FW_STATION_STATE
			&& check_buddy_fwstate(Adapter, WIFI_AP_STATE)
		) { 
			//disable related TSF function
			rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)&(~BIT(3)));

			rtw_write32(Adapter, REG_TSFTR, tsf);
			rtw_write32(Adapter, REG_TSFTR+4, tsf>>32);

			//enable related TSF function
			rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)|BIT(3));
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
		rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)&(~BIT(3)));
							
		rtw_write32(Adapter, REG_TSFTR, tsf);
		rtw_write32(Adapter, REG_TSFTR+4, tsf>>32);

		//enable related TSF function
		rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)|BIT(3));
		
		// Update buddy port's TSF if it is SoftAP for beacon TX issue!
		if ( (pmlmeinfo->state&0x03) == WIFI_FW_STATION_STATE
			&& check_buddy_fwstate(Adapter, WIFI_AP_STATE)
		) { 
			//disable related TSF function
			rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1)&(~BIT(3)));

			rtw_write32(Adapter, REG_TSFTR1, tsf);
			rtw_write32(Adapter, REG_TSFTR1+4, tsf>>32);

			//enable related TSF function
			rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1)|BIT(3));
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
		rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1)|BIT(4));

		// disable Port1's beacon function
		rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1)&(~BIT(3)));
	}
	else
	{
		//reset TSF
		rtw_write8(Adapter, REG_DUAL_TSF_RST, BIT(0));

		//disable update TSF
		rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)|BIT(4));
	}
#endif
}

static void hw_var_set_mlme_sitesurvey(PADAPTER Adapter, u8 variable, u8* val)
{
	u32	value_rcr, rcr_clear_bit, reg_bcn_ctl;
	u16	value_rxfltmap2;
	HAL_DATA_TYPE *pHalData = GET_HAL_DATA(Adapter);
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
	else if (Adapter->tdlsinfo.link_established == _TRUE)
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

		// Save orignal RRSR setting.
		pHalData->RegRRSR = rtw_read16(Adapter, REG_RRSR);

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
		rtw_write16(Adapter, REG_RRSR, pHalData->RegRRSR);

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
			rtw_write32(Adapter, REG_RCR, rtw_read32(Adapter, REG_RCR)|RCR_CBSSID_BCN);			
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
			rtw_write8(Adapter, REG_BCN_CTRL_1, rtw_read8(Adapter, REG_BCN_CTRL_1)&(~BIT(4)));
		else		
			rtw_write8(Adapter, REG_BCN_CTRL, rtw_read8(Adapter, REG_BCN_CTRL)&(~BIT(4)));
				 
	
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

void SetHwReg8812A(PADAPTER padapter, u8 variable, u8 *pval)
{
	PHAL_DATA_TYPE pHalData;
	struct dm_priv *pdmpriv;
	PDM_ODM_T podmpriv;
	u8 val8;
	u16 val16;
	u32 val32;

_func_enter_;

	pHalData = GET_HAL_DATA(padapter);
	pdmpriv = &pHalData->dmpriv;
	podmpriv = &pHalData->odmpriv;

	switch (variable)
	{
		case HW_VAR_MEDIA_STATUS:
			val8 = rtw_read8(padapter, MSR) & 0x0c;
			val8 |= *pval;
			rtw_write8(padapter, MSR, val8);
			break;

		case HW_VAR_MEDIA_STATUS1:
			val8 = rtw_read8(padapter, MSR) & 0x03;
			val8 |= *pval << 2;
			rtw_write8(padapter, MSR, val8);
			break;

		case HW_VAR_SET_OPMODE:
			hw_var_set_opmode(padapter, variable, pval);
			break;

		case HW_VAR_MAC_ADDR:
			hw_var_set_macaddr(padapter, variable, pval);
			break;

		case HW_VAR_BSSID:
			hw_var_set_bssid(padapter, variable, pval);
			break;

		case HW_VAR_BASIC_RATE:
		{
			struct mlme_ext_info *mlmext_info = &padapter->mlmeextpriv.mlmext_info;
			u16 input_b = 0, masked = 0, ioted = 0, BrateCfg = 0;
			u16 rrsr_2g_force_mask = (RRSR_11M|RRSR_5_5M|RRSR_1M);
			u16 rrsr_2g_allow_mask = (RRSR_24M|RRSR_12M|RRSR_6M|RRSR_CCK_RATES);
			u16 rrsr_5g_force_mask = (RRSR_6M);
			u16 rrsr_5g_allow_mask = (RRSR_OFDM_RATES);

			HalSetBrateCfg(padapter, pval, &BrateCfg);
			input_b = BrateCfg;

			/* apply force and allow mask */
			if(pHalData->CurrentBandType == BAND_ON_2_4G)
			{
				BrateCfg |= rrsr_2g_force_mask;
				BrateCfg &= rrsr_2g_allow_mask;
			}
			else // 5G
			{
				BrateCfg |= rrsr_5g_force_mask;
				BrateCfg &= rrsr_5g_allow_mask;
			}
			masked = BrateCfg;

			/* IOT consideration */
			if (mlmext_info->assoc_AP_vendor == HT_IOT_PEER_CISCO) {
				/* if peer is cisco and didn't use ofdm rate, we enable 6M ack */
				if((BrateCfg & (RRSR_24M|RRSR_12M|RRSR_6M)) == 0)
					BrateCfg |= RRSR_6M;
			}
			ioted = BrateCfg;

			pHalData->BasicRateSet = BrateCfg;

			DBG_8192C("HW_VAR_BASIC_RATE: %#x -> %#x -> %#x\n", input_b, masked, ioted);

			// Set RRSR rate table.
			rtw_write16(padapter, REG_RRSR, BrateCfg);
			rtw_write8(padapter, REG_RRSR+2, rtw_read8(padapter, REG_RRSR+2)&0xf0);
		}
			break;

		case HW_VAR_TXPAUSE:
			rtw_write8(padapter, REG_TXPAUSE, *pval);
			break;

		case HW_VAR_BCN_FUNC:
			hw_var_set_bcn_func(padapter, variable, pval);
			break;

		case HW_VAR_CORRECT_TSF:
#ifdef CONFIG_CONCURRENT_MODE
			hw_var_set_correct_tsf(padapter, variable, pval);
#else
			{
				u64	tsf;
				struct mlme_ext_priv	*pmlmeext = &padapter->mlmeextpriv;
				struct mlme_ext_info	*pmlmeinfo = &(pmlmeext->mlmext_info);

				//tsf = pmlmeext->TSFValue - ((u32)pmlmeext->TSFValue % (pmlmeinfo->bcn_interval*1024)) -1024; //us
				tsf = pmlmeext->TSFValue - rtw_modular64(pmlmeext->TSFValue, (pmlmeinfo->bcn_interval*1024)) -1024; //us

				if(((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) || ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE))
				{				
					//pHalData->RegTxPause |= STOP_BCNQ;BIT(6)
					//rtw_write8(padapter, REG_TXPAUSE, (rtw_read8(padapter, REG_TXPAUSE)|BIT(6)));
					StopTxBeacon(padapter);
				}

				//disable related TSF function
				rtw_write8(padapter, REG_BCN_CTRL, rtw_read8(padapter, REG_BCN_CTRL)&(~BIT(3)));
							
				rtw_write32(padapter, REG_TSFTR, tsf);
				rtw_write32(padapter, REG_TSFTR+4, tsf>>32);

				//enable related TSF function
				rtw_write8(padapter, REG_BCN_CTRL, rtw_read8(padapter, REG_BCN_CTRL)|BIT(3));
				
							
				if(((pmlmeinfo->state&0x03) == WIFI_FW_ADHOC_STATE) || ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE))
				{
					//pHalData->RegTxPause  &= (~STOP_BCNQ);
					//rtw_write8(padapter, REG_TXPAUSE, (rtw_read8(padapter, REG_TXPAUSE)&(~BIT(6))));
					ResumeTxBeacon(padapter);
				}
			}
#endif
			break;

		case HW_VAR_CHECK_BSSID:
			val32 = rtw_read32(padapter, REG_RCR);
			if (*pval)
				val32 |= RCR_CBSSID_DATA|RCR_CBSSID_BCN;
			else
				val32 &= ~(RCR_CBSSID_DATA|RCR_CBSSID_BCN);
			rtw_write32(padapter, REG_RCR, val32);
			break;

		case HW_VAR_MLME_DISCONNECT:
#ifdef CONFIG_CONCURRENT_MODE
			hw_var_set_mlme_disconnect(padapter, variable, pval);
#else
			{
				// Set RCR to not to receive data frame when NO LINK state
//				val32 = rtw_read32(padapter, REG_RCR);
//				val32 &= ~RCR_ADF;
//				rtw_write32(padapter, REG_RCR, val32);

				// reject all data frames
				rtw_write16(padapter, REG_RXFLTMAP2, 0x00);

				// reset TSF
				val8 = BIT(0) | BIT(1);
				rtw_write8(padapter, REG_DUAL_TSF_RST, val8);

				// disable update TSF
				val8 = rtw_read8(padapter, REG_BCN_CTRL);
				val8 |= BIT(4);
				rtw_write8(padapter, REG_BCN_CTRL, val8);
			}
#endif
			break;

		case HW_VAR_MLME_SITESURVEY:
			hw_var_set_mlme_sitesurvey(padapter, variable,  pval);

#ifdef CONFIG_BT_COEXIST
			rtw_btcoex_ScanNotify(padapter, *pval?_TRUE:_FALSE);
#endif
			break;

		case HW_VAR_MLME_JOIN:
#ifdef CONFIG_CONCURRENT_MODE
			hw_var_set_mlme_join(padapter, variable, pval);
#else // !CONFIG_CONCURRENT_MODE
			{
				u8 RetryLimit = 0x30;
				u8 type = *(u8*)pval;
				struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
				EEPROM_EFUSE_PRIV *pEEPROM = GET_EEPROM_EFUSE_PRIV(padapter);

				if (type == 0) // prepare to join
				{
					//enable to rx data frame.Accept all data frame
					//rtw_write32(padapter, REG_RCR, rtw_read32(padapter, REG_RCR)|RCR_ADF);
					rtw_write16(padapter, REG_RXFLTMAP2, 0xFFFF);

					val32 = rtw_read32(padapter, REG_RCR);
					if (padapter->in_cta_test)
						val32 &= ~(RCR_CBSSID_DATA | RCR_CBSSID_BCN);//| RCR_ADF
					else
						val32 |= RCR_CBSSID_DATA|RCR_CBSSID_BCN;
					rtw_write32(padapter, REG_RCR, val32);

					if (check_fwstate(pmlmepriv, WIFI_STATION_STATE) == _TRUE)
					{
						RetryLimit = (pEEPROM->CustomerID == RT_CID_CCX) ? 7 : 48;
					}
					else // Ad-hoc Mode
					{
						RetryLimit = 0x7;
					}
				}
				else if (type == 1) //joinbss_event call back when join res < 0
				{
					rtw_write16(padapter, REG_RXFLTMAP2, 0x00);
				}
				else if (type == 2) //sta add event call back
				{
					//enable update TSF
					val8 = rtw_read8(padapter, REG_BCN_CTRL);
					val8 &= ~BIT(4);
					rtw_write8(padapter, REG_BCN_CTRL, val8);

					if (check_fwstate(pmlmepriv, WIFI_ADHOC_STATE|WIFI_ADHOC_MASTER_STATE))
					{
						RetryLimit = 0x7;
					}
				}

				val16 = RetryLimit << RETRY_LIMIT_SHORT_SHIFT | RetryLimit << RETRY_LIMIT_LONG_SHIFT;
				rtw_write16(padapter, REG_RL, val16);
			}
#endif // !CONFIG_CONCURRENT_MODE

#ifdef CONFIG_BT_COEXIST
			switch (*pval)
			{
				case 0:
					// prepare to join
					rtw_btcoex_ConnectNotify(padapter, _TRUE);
					break;
				case 1:
					// joinbss_event callback when join res < 0
					rtw_btcoex_ConnectNotify(padapter, _FALSE);
					break;
				case 2:
					// sta add event callback
//					rtw_btcoex_MediaStatusNotify(padapter, RT_MEDIA_CONNECT);
					break;
			}
#endif
			break;

		case HW_VAR_ON_RCR_AM:
			val32 = rtw_read32(padapter, REG_RCR);
			val32 |= RCR_AM;
			rtw_write32(padapter, REG_RCR, val32);
			DBG_8192C("%s, %d, RCR= %x\n", __FUNCTION__, __LINE__, rtw_read32(padapter, REG_RCR));
			break;

		case HW_VAR_OFF_RCR_AM:
			val32 = rtw_read32(padapter, REG_RCR);
			val32 &= ~RCR_AM;
			rtw_write32(padapter, REG_RCR, val32);
			DBG_8192C("%s, %d, RCR= %x\n", __FUNCTION__, __LINE__, rtw_read32(padapter, REG_RCR));
			break;

		case HW_VAR_BEACON_INTERVAL:
			rtw_write16(padapter, REG_BCN_INTERVAL, *(u16*)pval);
#ifdef CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
			{
				struct mlme_ext_priv *pmlmeext;
				struct mlme_ext_info *pmlmeinfo;
				u16 bcn_interval;

				pmlmeext = &padapter->mlmeextpriv;
				pmlmeinfo = &pmlmeext->mlmext_info;
				bcn_interval = *((u16*)pval);

				if ((pmlmeinfo->state&0x03) == WIFI_FW_AP_STATE)
				{
					DBG_8192C("%s==> bcn_interval:%d, eraly_int:%d\n", __FUNCTION__, bcn_interval, bcn_interval>>1);
					rtw_write8(padapter, REG_DRVERLYINT, bcn_interval>>1);// 50ms for sdio 
				}			
			}
#endif // CONFIG_INTERRUPT_BASED_TXBCN_EARLY_INT
			break;

		case HW_VAR_SLOT_TIME:
			rtw_write8(padapter, REG_SLOT, *pval);
			break;

		case HW_VAR_RESP_SIFS:
			// SIFS_Timer = 0x0a0a0808;
			// RESP_SIFS for CCK
			rtw_write8(padapter, REG_RESP_SIFS_CCK, pval[0]); // SIFS_T2T_CCK (0x08)
			rtw_write8(padapter, REG_RESP_SIFS_CCK+1, pval[1]); //SIFS_R2T_CCK(0x08)
			// RESP_SIFS for OFDM
			rtw_write8(padapter, REG_RESP_SIFS_OFDM, pval[2]); //SIFS_T2T_OFDM (0x0a)
			rtw_write8(padapter, REG_RESP_SIFS_OFDM+1, pval[3]); //SIFS_R2T_OFDM(0x0a)
			break;

		case HW_VAR_ACK_PREAMBLE:
			{
				u8 bShortPreamble = *pval;

				// Joseph marked out for Netgear 3500 TKIP channel 7 issue.(Temporarily)
				val8 = (pHalData->nCur40MhzPrimeSC) << 5;
				if (bShortPreamble)
					val8 |= 0x80;
				rtw_write8(padapter, REG_RRSR+2, val8);
			}
			break;

		case HW_VAR_CAM_EMPTY_ENTRY:
			{
				u8 ucIndex = *pval;
				u8 i;
				u32	ulCommand = 0;
				u32	ulContent = 0;
				u32	ulEncAlgo = CAM_AES;

				for (i=0; i<CAM_CONTENT_COUNT; i++)
				{
					// filled id in CAM config 2 byte
					if (i == 0)
					{
						ulContent |= (ucIndex & 0x03) | ((u16)(ulEncAlgo)<<2);
						//ulContent |= CAM_VALID;
					}
					else
					{
						ulContent = 0;
					}
					// polling bit, and No Write enable, and address
					ulCommand = CAM_CONTENT_COUNT*ucIndex+i;
					ulCommand = ulCommand | CAM_POLLINIG | CAM_WRITE;
					// write content 0 is equall to mark invalid
					rtw_write32(padapter, WCAMI, ulContent);  //delay_ms(40);
					rtw_write32(padapter, RWCAM, ulCommand);  //delay_ms(40);
				}
			}
			break;

		case HW_VAR_CAM_INVALID_ALL:
			val32 = BIT(31) | BIT(30);
			rtw_write32(padapter, RWCAM, val32);
			break;

		case HW_VAR_CAM_WRITE:
			{
				u32 cmd;
				u32 *cam_val = (u32*)pval;

				rtw_write32(padapter, WCAMI, cam_val[0]);

				cmd = CAM_POLLINIG | CAM_WRITE | cam_val[1];
				rtw_write32(padapter, RWCAM, cmd);
			}
			break;

		case HW_VAR_CAM_READ:
			break;

		case HW_VAR_AC_PARAM_VO:
			rtw_write32(padapter, REG_EDCA_VO_PARAM, *(u32*)pval);
			break;

		case HW_VAR_AC_PARAM_VI:
			rtw_write32(padapter, REG_EDCA_VI_PARAM, *(u32*)pval);
			break;

		case HW_VAR_AC_PARAM_BE:
			pHalData->AcParam_BE = *(u32*)pval;
			rtw_write32(padapter, REG_EDCA_BE_PARAM, *(u32*)pval);
			break;

		case HW_VAR_AC_PARAM_BK:
			rtw_write32(padapter, REG_EDCA_BK_PARAM, *(u32*)pval);
			break;

		case HW_VAR_ACM_CTRL:
			{
				u8 acm_ctrl;
				u8 AcmCtrl;

				acm_ctrl = *(u8*)pval;
				AcmCtrl = rtw_read8(padapter, REG_ACMHWCTRL);

				if (acm_ctrl > 1)
					AcmCtrl = AcmCtrl | 0x1;

				if (acm_ctrl & BIT(3))
					AcmCtrl |= AcmHw_VoqEn;
				else
					AcmCtrl &= (~AcmHw_VoqEn);

				if (acm_ctrl & BIT(2))
					AcmCtrl |= AcmHw_ViqEn;
				else
					AcmCtrl &= (~AcmHw_ViqEn);

				if (acm_ctrl & BIT(1))
					AcmCtrl |= AcmHw_BeqEn;
				else
					AcmCtrl &= (~AcmHw_BeqEn);

				DBG_8192C("[HW_VAR_ACM_CTRL] Write 0x%X\n", AcmCtrl);
				rtw_write8(padapter, REG_ACMHWCTRL, AcmCtrl );
			}
			break;
		case HW_VAR_AMPDU_FACTOR:
			{
				u32	AMPDULen = *(u8*)pval;

				if (IS_HARDWARE_TYPE_8812(padapter))
				{
					if (AMPDULen < VHT_AGG_SIZE_128K)
						AMPDULen = (0x2000 << *(u8*)pval) - 1;
					else
						AMPDULen = 0x1ffff;
				}
				else if(IS_HARDWARE_TYPE_8821(padapter))
				{
					if (AMPDULen < HT_AGG_SIZE_64K)
						AMPDULen = (0x2000 << *(u8*)pval) - 1;
					else
						AMPDULen = 0xffff;
				}
				AMPDULen |= BIT(31);
				rtw_write32(padapter, REG_AMPDU_MAX_LENGTH_8812, AMPDULen);
			}
			break;
#if 0
		case HW_VAR_RXDMA_AGG_PG_TH:
			rtw_write8(padapter, REG_RXDMA_AGG_PG_TH, *pval);
			break;
#endif
		case HW_VAR_H2C_FW_PWRMODE:
			{
				u8 psmode = *pval;

				// Forece leave RF low power mode for 1T1R to prevent conficting setting in Fw power
				// saving sequence. 2010.06.07. Added by tynli. Suggested by SD3 yschang.
				if ((psmode != PS_MODE_ACTIVE) && (!IS_92C_SERIAL(pHalData->VersionID)))
				{
					ODM_RF_Saving(podmpriv, _TRUE);
				}
				rtl8812_set_FwPwrMode_cmd(padapter, psmode);
			}
			break;

		case HW_VAR_H2C_FW_JOINBSSRPT:
			rtl8812_set_FwJoinBssReport_cmd(padapter, *pval);
			break;

#ifdef CONFIG_P2P_PS
		case HW_VAR_H2C_FW_P2P_PS_OFFLOAD:
			rtl8812_set_p2p_ps_offload_cmd(padapter, *pval);
			break;
#endif // CONFIG_P2P_PS

#ifdef CONFIG_TDLS
		case HW_VAR_TDLS_WRCR:
			val32 = rtw_read32(padapter, REG_RCR);
			val32 &= ~RCR_CBSSID_DATA;
			rtw_write32(padapter, REG_RCR, val32);
			break;

		case HW_VAR_TDLS_INIT_CH_SEN:
			val32 = rtw_read32(padapter, REG_RCR);
			val32 &= (~RCR_CBSSID_DATA) & (~RCR_CBSSID_BCN);
			rtw_write32(padapter, REG_RCR, val32);
			rtw_write16(padapter, REG_RXFLTMAP2, 0xffff);

			// disable update TSF
			val8 = rtw_read8(padapter, REG_BCN_CTRL);
			val8 |= BIT(4);
			rtw_write8(padapter, REG_BCN_CTRL, val8);
			break;

		case HW_VAR_TDLS_DONE_CH_SEN:
			// enable update TSF
			val8 = rtw_read8(padapter, REG_BCN_CTRL);
			val8 &= ~BIT(4);
			rtw_write8(padapter, REG_BCN_CTRL, val8);

			val32 = rtw_read32(padapter, REG_RCR);
			val32 |= RCR_CBSSID_BCN;
			rtw_write32(padapter, REG_RCR, val32);
			break;

		case HW_VAR_TDLS_RS_RCR:
			val32 = rtw_read32(padapter, REG_RCR);
			val32 |= RCR_CBSSID_DATA;
			rtw_write32(padapter, REG_RCR, val32);
			break;
#endif // CONFIG_TDLS
#if defined(CONFIG_BT_COEXIST) && 0
		case HW_VAR_BT_SET_COEXIST:
			rtl8812_set_dm_bt_coexist(padapter, *pval);
			break;

		case HW_VAR_BT_ISSUE_DELBA:
			rtl8812_issue_delete_ba(padapter, *pval);
			break;
#endif

#ifdef CONFIG_SW_ANTENNA_DIVERSITY
		case HW_VAR_ANTENNA_DIVERSITY_LINK:
			//SwAntDivRestAfterLink8192C(padapter);
			ODM_SwAntDivRestAfterLink(podmpriv);
			break;

		case HW_VAR_ANTENNA_DIVERSITY_SELECT:
			{
				u8 Optimum_antenna = *pval;
				u8 	Ant;

				//switch antenna to Optimum_antenna
				//DBG_8192C("==> HW_VAR_ANTENNA_DIVERSITY_SELECT , Ant_(%s)\n",(Optimum_antenna==2)?"A":"B");
				if (pHalData->CurAntenna != Optimum_antenna)
				{
					Ant = (Optimum_antenna==2) ? MAIN_ANT : AUX_ANT;
					ODM_UpdateRxIdleAnt(podmpriv, Ant);

					pHalData->CurAntenna = Optimum_antenna;
					//DBG_8192C("==> HW_VAR_ANTENNA_DIVERSITY_SELECT , Ant_(%s)\n",(Optimum_antenna==2)?"A":"B");
				}
			}
			break;
#endif

		case HW_VAR_EFUSE_USAGE:
			pHalData->EfuseUsedPercentage = *pval;
			break;

		case HW_VAR_EFUSE_BYTES:
			pHalData->EfuseUsedBytes = *(u16*)pval;
			break;
#if 0
		case HW_VAR_EFUSE_BT_USAGE:
#ifdef HAL_EFUSE_MEMORY
			pHalData->EfuseHal.BTEfuseUsedPercentage = *pval;
#endif
			break;

		case HW_VAR_EFUSE_BT_BYTES:
#ifdef HAL_EFUSE_MEMORY
			pHalData->EfuseHal.BTEfuseUsedBytes = *(u16*)pval;
#else
			BTEfuseUsedBytes = *(u16*)pval;
#endif
			break;
#endif
		case HW_VAR_FIFO_CLEARN_UP:
			{
				struct pwrctrl_priv *pwrpriv;
				u8 trycnt = 100;	

				pwrpriv = adapter_to_pwrctl(padapter);

				// pause tx
				rtw_write8(padapter, REG_TXPAUSE, 0xff);

				// keep sn
				padapter->xmitpriv.nqos_ssn = rtw_read16(padapter, REG_NQOS_SEQ);

				if (pwrpriv->bkeepfwalive != _TRUE)
				{
					// RX DMA stop
					val32 = rtw_read32(padapter, REG_RXPKT_NUM);
					val32 |= RW_RELEASE_EN;
					rtw_write32(padapter, REG_RXPKT_NUM, val32);
					do {
						val32 = rtw_read32(padapter, REG_RXPKT_NUM);
						val32 &= RXDMA_IDLE;
						if (val32)
							break;
					} while (--trycnt);
					if (trycnt == 0)
					{
						DBG_8192C("[HW_VAR_FIFO_CLEARN_UP] Stop RX DMA failed......\n");
					}

					//RQPN Load 0
					rtw_write16(padapter, REG_RQPN_NPQ, 0x0);
					rtw_write32(padapter, REG_RQPN, 0x80000000);
					rtw_mdelay_os(10);
				}
			}
			break;

		case HW_VAR_CHECK_TXBUF:
			{
				u8 retry_limit;
				u32 reg_200 = 0, reg_204 = 0;
				u32 init_reg_200 = 0, init_reg_204 = 0;
				u32 start = rtw_get_current_time();
				u32 pass_ms;
				int i = 0;

				retry_limit = 0x01;

				val16 = retry_limit << RETRY_LIMIT_SHORT_SHIFT | retry_limit << RETRY_LIMIT_LONG_SHIFT;
				rtw_write16(padapter, REG_RL, val16);

				while (rtw_get_passing_time_ms(start) < 2000
					&& !padapter->bDriverStopped && !padapter->bSurpriseRemoved
				) {
					reg_200 = rtw_read32(padapter, 0x200);
					reg_204 = rtw_read32(padapter, 0x204);

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

				if (padapter->bDriverStopped || padapter->bSurpriseRemoved) {
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
				rtw_write16(padapter, REG_RL, val16);
			}
			break;
		case HW_VAR_H2C_MEDIA_STATUS_RPT:
			{
				struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
				RT_MEDIA_STATUS	mstatus = *(u16*)pval & 0xFF;

				rtl8812_set_FwMediaStatus_cmd(padapter, *(u16*)pval);

				if (check_fwstate(pmlmepriv, WIFI_STATION_STATE))
					Hal_PatchwithJaguar_8812(padapter, mstatus);
			}
			break;

		case HW_VAR_APFM_ON_MAC:
			pHalData->bMacPwrCtrlOn = *pval;
			DBG_8192C("%s: bMacPwrCtrlOn=%d\n", __FUNCTION__, pHalData->bMacPwrCtrlOn);
			break;

		case HW_VAR_NAV_UPPER:
			{
				u32 usNavUpper = *((u32*)pval);

				if (usNavUpper > HAL_NAV_UPPER_UNIT * 0xFF)
				{
					DBG_8192C("%s: [HW_VAR_NAV_UPPER] set value(0x%08X us) is larger than (%d * 0xFF)!\n",
						__FUNCTION__, usNavUpper, HAL_NAV_UPPER_UNIT);
					break;
				}

				// The value of ((usNavUpper + HAL_NAV_UPPER_UNIT - 1) / HAL_NAV_UPPER_UNIT)
				// is getting the upper integer.
				usNavUpper = (usNavUpper + HAL_NAV_UPPER_UNIT - 1) / HAL_NAV_UPPER_UNIT;
				rtw_write8(padapter, REG_NAV_UPPER, (u8)usNavUpper);
			}
			break;

		case HW_VAR_BCN_VALID:
#ifdef CONFIG_CONCURRENT_MODE
			if (IS_HARDWARE_TYPE_8821(padapter) && padapter->iface_type == IFACE_PORT1)
			{
				val8 = rtw_read8(padapter, REG_DWBCN1_CTRL_8812+2);
				val8 |= BIT(0);
				rtw_write8(padapter, REG_DWBCN1_CTRL_8812+2, val8); 
			}
			else
#endif
			{
				// BCN_VALID, BIT16 of REG_TDECTRL = BIT0 of REG_TDECTRL+2, write 1 to clear, Clear by sw
				val8 = rtw_read8(padapter, REG_TDECTRL+2);
				val8 |= BIT(0);
				rtw_write8(padapter, REG_TDECTRL+2, val8);
			}
			break;

		case HW_VAR_DL_BCN_SEL:
#ifdef CONFIG_CONCURRENT_MODE
			if (IS_HARDWARE_TYPE_8821(padapter) && padapter->iface_type == IFACE_PORT1)
			{
				// SW_BCN_SEL - Port1
				val8 = rtw_read8(padapter, REG_DWBCN1_CTRL_8812+2);
				val8 |= BIT(4);
				rtw_write8(padapter, REG_DWBCN1_CTRL_8812+2, val8);
			}
			else
#endif
			{
				// SW_BCN_SEL - Port0
				val8 = rtw_read8(padapter, REG_DWBCN1_CTRL_8812+2);
				val8 &= ~BIT(4);
				rtw_write8(padapter, REG_DWBCN1_CTRL_8812+2, val8);	
			}
			break;

		case HW_VAR_WIRELESS_MODE:
			{
				struct mlme_priv *pmlmepriv = &padapter->mlmepriv;
				struct mlme_ext_priv *pmlmeext = &padapter->mlmeextpriv;
				struct mlme_ext_info *pmlmeinfo = &pmlmeext->mlmext_info;
				u8	R2T_SIFS = 0, SIFS_Timer = 0;
				u8	wireless_mode = *pval;

				if ((wireless_mode == WIRELESS_11BG) || (wireless_mode == WIRELESS_11G))
					SIFS_Timer = 0xa;
				else
					SIFS_Timer = 0xe;

				// SIFS for OFDM Data ACK
				rtw_write8(padapter, REG_SIFS_CTX+1, SIFS_Timer);
				// SIFS for OFDM consecutive tx like CTS data!
				rtw_write8(padapter, REG_SIFS_TRX+1, SIFS_Timer);
				
				rtw_write8(padapter,REG_SPEC_SIFS+1, SIFS_Timer);
				rtw_write8(padapter,REG_MAC_SPEC_SIFS+1, SIFS_Timer);

				// 20100719 Joseph: Revise SIFS setting due to Hardware register definition change.
				rtw_write8(padapter, REG_RESP_SIFS_OFDM+1, SIFS_Timer);
				rtw_write8(padapter, REG_RESP_SIFS_OFDM, SIFS_Timer);

				//
				// Adjust R2T SIFS for IOT issue. Add by hpfan 2013.01.25
				// Set R2T SIFS to 0x0a for Atheros IOT. Add by hpfan 2013.02.22
				//
				// Mac has 10 us delay so use 0xa value is enough. 
				R2T_SIFS = 0xa;
#ifdef CONFIG_80211AC_VHT
				if (wireless_mode & WIRELESS_11_5AC &&
						//MgntLinkStatusQuery(Adapter)  && 
						TEST_FLAG(pmlmepriv->vhtpriv.ldpc_cap, LDPC_VHT_ENABLE_RX) && 
						TEST_FLAG(pmlmepriv->vhtpriv.stbc_cap, STBC_VHT_ENABLE_RX))
				{		
					if (pmlmeinfo->assoc_AP_vendor == HT_IOT_PEER_ATHEROS)			
						R2T_SIFS = 0x8;
					else
						R2T_SIFS = 0xa;
				}
#endif //CONFIG_80211AC_VHT

				rtw_write8(padapter, REG_RESP_SIFS_OFDM+1, R2T_SIFS);
			}
			break;

		case HW_VAR_DO_IQK:
			pHalData->bNeedIQK = _TRUE;
			break;

#ifdef CONFIG_BEAMFORMING
		case HW_VAR_SOUNDING_ENTER:
			SetBeamformEnter_8812(padapter, *pval);
			break;

		case HW_VAR_SOUNDING_LEAVE:
			SetBeamformLeave_8812(padapter, *pval);
			break;

		case HW_VAR_SOUNDING_RATE:
			rtw_write8(padapter, REG_NDPA_OPT_CTRL_8812, (MRateToHwRate(pval[1]) << 2 | pval[0]) );
			break;

		case HW_VAR_SOUNDING_STATUS:
			SetBeamformStatus_8812(padapter, *pval);
			break;

		case HW_VAR_SOUNDING_FW_NDPA:
			SetBeamformFwTxBF_8812(padapter, *pval);
			break;

		case HW_VAR_SOUNDING_CLK:
			SetBeamformingCLK_8812(padapter);
			break;
#endif

		case HW_VAR_MACID_SLEEP:
		{
			u32 reg_macid_sleep;
			u8 bit_shift;
			u8 id = *(u8*)pval;
			u32 val32;

			if (id < 32) {
				reg_macid_sleep = REG_MACID_SLEEP;
				bit_shift = id;
			} else if (id < 64) {
				reg_macid_sleep = REG_MACID_SLEEP_1;
				bit_shift = id-32;
			} else if (id < 96) {
				reg_macid_sleep = REG_MACID_SLEEP_2;
				bit_shift = id-64;
			} else if (id < 128) {
				reg_macid_sleep = REG_MACID_SLEEP_3;
				bit_shift = id-96;
			} else {
				rtw_warn_on(1);
				break;
			}

			val32 = rtw_read32(padapter, reg_macid_sleep);
			DBG_8192C(FUNC_ADPT_FMT ": [HW_VAR_MACID_SLEEP] macid=%d, org reg_0x%03x=0x%08X\n",
				FUNC_ADPT_ARG(padapter), id, reg_macid_sleep, val32);

			if (val32 & BIT(bit_shift))
				break;

			val32 |= BIT(bit_shift);
			rtw_write32(padapter, reg_macid_sleep, val32);
		}
			break;

		case HW_VAR_MACID_WAKEUP:
		{
			u32 reg_macid_sleep;
			u8 bit_shift;
			u8 id = *(u8*)pval;
			u32 val32;

			if (id < 32) {
				reg_macid_sleep = REG_MACID_SLEEP;
				bit_shift = id;
			} else if (id < 64) {
				reg_macid_sleep = REG_MACID_SLEEP_1;
				bit_shift = id-32;
			} else if (id < 96) {
				reg_macid_sleep = REG_MACID_SLEEP_2;
				bit_shift = id-64;
			} else if (id < 128) {
				reg_macid_sleep = REG_MACID_SLEEP_3;
				bit_shift = id-96;
			} else {
				rtw_warn_on(1);
				break;
			}

			val32 = rtw_read32(padapter, reg_macid_sleep);
			DBG_8192C(FUNC_ADPT_FMT ": [HW_VAR_MACID_WAKEUP] macid=%d, org reg_0x%03x=0x%08X\n",
				FUNC_ADPT_ARG(padapter), id, reg_macid_sleep, val32);

			if (!(val32 & BIT(bit_shift)))
				break;

			val32 &= ~BIT(bit_shift);
			rtw_write32(padapter, reg_macid_sleep, val32);
		}
			break;

		default:
			SetHwReg(padapter, variable, pval);
			break;
	}

_func_exit_;
}

struct qinfo_8812a {
	u32 head:8;
	u32 pkt_num:7;
	u32 tail:8;
	u32 ac:2;
	u32 macid:7;
};

struct bcn_qinfo_8812a {
	u16 head:8;
	u16 pkt_num:8;
};

void dump_qinfo_8812a(void *sel, struct qinfo_8812a *info, const char *tag)
{
	//if (info->pkt_num)
	DBG_871X_SEL_NL(sel, "%shead:0x%02x, tail:0x%02x, pkt_num:%u, macid:%u, ac:%u\n"
		, tag ? tag : "", info->head, info->tail, info->pkt_num, info->macid, info->ac
	);
}

void dump_bcn_qinfo_8812a(void *sel, struct bcn_qinfo_8812a *info, const char *tag)
{
	//if (info->pkt_num)
	DBG_871X_SEL_NL(sel, "%shead:0x%02x, pkt_num:%u\n"
		, tag ? tag : "", info->head, info->pkt_num
	);
}

void dump_mac_qinfo_8812a(void *sel, _adapter *adapter)
{
	u32 q0_info;
	u32 q1_info;
	u32 q2_info;
	u32 q3_info;
	u32 q4_info;
	u32 q5_info;
	u32 q6_info;
	u32 q7_info;
	u32 mg_q_info;
	u32 hi_q_info;
	u16 bcn_q_info;

	q0_info = rtw_read32(adapter, REG_Q0_INFO);
	q1_info = rtw_read32(adapter, REG_Q1_INFO);
	q2_info = rtw_read32(adapter, REG_Q2_INFO);
	q3_info = rtw_read32(adapter, REG_Q3_INFO);
	q4_info = rtw_read32(adapter, REG_Q4_INFO);
	q5_info = rtw_read32(adapter, REG_Q5_INFO);
	q6_info = rtw_read32(adapter, REG_Q6_INFO);
	q7_info = rtw_read32(adapter, REG_Q7_INFO);
	mg_q_info = rtw_read32(adapter, REG_MGQ_INFO);
	hi_q_info = rtw_read32(adapter, REG_HGQ_INFO);
	bcn_q_info = rtw_read16(adapter, REG_BCNQ_INFO);

	dump_qinfo_8812a(sel, (struct qinfo_8812a *)&q0_info, "Q0 ");
	dump_qinfo_8812a(sel, (struct qinfo_8812a *)&q1_info, "Q1 ");
	dump_qinfo_8812a(sel, (struct qinfo_8812a *)&q2_info, "Q2 ");
	dump_qinfo_8812a(sel, (struct qinfo_8812a *)&q3_info, "Q3 ");
	dump_qinfo_8812a(sel, (struct qinfo_8812a *)&q4_info, "Q4 ");
	dump_qinfo_8812a(sel, (struct qinfo_8812a *)&q5_info, "Q5 ");
	dump_qinfo_8812a(sel, (struct qinfo_8812a *)&q6_info, "Q6 ");
	dump_qinfo_8812a(sel, (struct qinfo_8812a *)&q7_info, "Q7 ");
	dump_qinfo_8812a(sel, (struct qinfo_8812a *)&mg_q_info, "MG ");
	dump_qinfo_8812a(sel, (struct qinfo_8812a *)&hi_q_info, "HI ");
	dump_bcn_qinfo_8812a(sel, (struct bcn_qinfo_8812a *)&bcn_q_info, "BCN ");
}

void GetHwReg8812A(PADAPTER padapter, u8 variable, u8 *pval)
{
	PHAL_DATA_TYPE pHalData;
	PDM_ODM_T podmpriv;
	u8 val8;
	u16 val16;
	u32 val32;

_func_enter_;

	pHalData = GET_HAL_DATA(padapter);
	podmpriv = &pHalData->odmpriv;

	switch (variable)
	{
		case HW_VAR_TXPAUSE:
			*pval = rtw_read8(padapter, REG_TXPAUSE);
			break;

		case HW_VAR_BCN_VALID:
#ifdef CONFIG_CONCURRENT_MODE
			if (IS_HARDWARE_TYPE_8821(padapter) && padapter->iface_type == IFACE_PORT1)
			{
				val8 = rtw_read8(padapter, REG_DWBCN1_CTRL_8812+2);
				*pval = (BIT(0) & val8) ? _TRUE:_FALSE;
			}
			else
#endif
			{
				//BCN_VALID, BIT16 of REG_TDECTRL = BIT0 of REG_TDECTRL+2
				val8 = rtw_read8(padapter, REG_TDECTRL+2);
				*pval = (BIT(0) & val8) ? _TRUE:_FALSE;
			}
			break;

		case HW_VAR_FWLPS_RF_ON:
			//When we halt NIC, we should check if FW LPS is leave.
			if(adapter_to_pwrctl(padapter)->rf_pwrstate == rf_off)
			{
				// If it is in HW/SW Radio OFF or IPS state, we do not check Fw LPS Leave,
				// because Fw is unload.
				*pval = _TRUE;
			}
			else
			{
				u32 valRCR;
				valRCR = rtw_read32(padapter, REG_RCR);
				valRCR &= 0x00070000;
				if(valRCR)
					*pval = _FALSE;
				else
					*pval = _TRUE;
			}
			
			break;

#ifdef CONFIG_ANTENNA_DIVERSITY
		case HW_VAR_CURRENT_ANTENNA:
			*pval = pHalData->CurAntenna;
			break;
#endif

		case HW_VAR_EFUSE_BYTES: // To get EFUE total used bytes, added by Roger, 2008.12.22.
			*(u16*)pval = pHalData->EfuseUsedBytes;	
			break;

		case HW_VAR_APFM_ON_MAC:
			*pval = pHalData->bMacPwrCtrlOn;
			break;

		case HW_VAR_CHK_HI_QUEUE_EMPTY:
			val16 = rtw_read16(padapter, REG_TXPKT_EMPTY);
			*pval = (val16 & BIT(10)) ? _TRUE:_FALSE;
			break;

		case HW_VAR_DUMP_MAC_QUEUE_INFO:
			dump_mac_qinfo_8812a(pval, padapter);
			break;

		default:
			GetHwReg(padapter, variable, pval);
			break;
	}

_func_exit_;
}

/*
 *	Description:
 *		Change default setting of specified variable.
 */
u8 SetHalDefVar8812A(PADAPTER padapter, HAL_DEF_VARIABLE variable, void *pval)
{
	PHAL_DATA_TYPE pHalData;
	u8 bResult;


	pHalData = GET_HAL_DATA(padapter);
	bResult = _SUCCESS;

	switch (variable)
	{
		default:
			bResult = SetHalDefVar(padapter, variable, pval);
			break;
	}

	return bResult;
}

/*
 *	Description: 
 *		Query setting of specified variable.
 */
u8 GetHalDefVar8812A(PADAPTER padapter, HAL_DEF_VARIABLE variable, void *pval)
{
	PHAL_DATA_TYPE pHalData;
	u8 bResult;


	pHalData = GET_HAL_DATA(padapter);
	bResult = _SUCCESS;

	switch (variable)
	{
		

#ifdef CONFIG_ANTENNA_DIVERSITY
		case HAL_DEF_IS_SUPPORT_ANT_DIV:
			*((u8*)pval) = (pHalData->AntDivCfg==0) ? _FALSE : _TRUE;
			break;
#endif

#ifdef CONFIG_ANTENNA_DIVERSITY
		case HAL_DEF_CURRENT_ANTENNA:
			*((u8*)pval) = pHalData->CurAntenna;
			break;
#endif

		case HAL_DEF_DRVINFO_SZ:
			*((u32*)pval) = DRVINFO_SZ;
			break;

		case HAL_DEF_MAX_RECVBUF_SZ:
			*((u32*)pval) = MAX_RECVBUF_SZ;
			break;

		case HAL_DEF_RX_PACKET_OFFSET:
			*((u32*)pval) = RXDESC_SIZE + DRVINFO_SZ*8;
			break;

		case HW_VAR_MAX_RX_AMPDU_FACTOR:
			*((u32*)pval) = MAX_AMPDU_FACTOR_64K;
			break;

		case HAL_DEF_TX_LDPC:
			if (IS_VENDOR_8812A_C_CUT(padapter))
				*(u8*)pval = _TRUE;
			else if (IS_HARDWARE_TYPE_8821(padapter))
				*(u8*)pval = _TRUE;
			else
				*(u8*)pval = _FALSE;
			break;

		case HAL_DEF_RX_LDPC:
			if (IS_VENDOR_8812A_C_CUT(padapter))
				*(u8*)pval = _TRUE;
			else if (IS_HARDWARE_TYPE_8821(padapter))
				*(u8*)pval = _FALSE;
			else
				*(u8*)pval = _FALSE;
			break;

		case HAL_DEF_TX_STBC:
			if (pHalData->rf_type == RF_2T2R)
				*(u8*)pval = 1;
			else
				*(u8*)pval = 0;
			break;

		case HAL_DEF_RX_STBC:
			*(u8*)pval = 1;
			break;

		case HAL_DEF_EXPLICIT_BEAMFORMER:
			if(pHalData->rf_type == RF_2T2R)
				*((PBOOLEAN)pval) = _TRUE;
			else
				*((PBOOLEAN)pval) = _FALSE;
			break;
		
		case HAL_DEF_EXPLICIT_BEAMFORMEE:
			*((PBOOLEAN)pval) = _TRUE;
			break;

		case HW_DEF_RA_INFO_DUMP:
			{
				u8 mac_id = *(u8*)pval;
				u32 cmd ;
				u32 ra_info1, ra_info2;
				u32 rate_mask1, rate_mask2;
				u8 curr_tx_rate,curr_tx_sgi,hight_rate,lowest_rate;				
				
				DBG_8192C("============ RA status check  Mac_id:%d ===================\n", mac_id);

				cmd = 0x40000100 |mac_id;
				rtw_write32(padapter,REG_HMEBOX_E2_E3_8812,cmd);
				rtw_msleep_os(10);
				ra_info1 = rtw_read32(padapter,REG_RSVD5_8812);
				curr_tx_rate = ra_info1&0x7F;
				curr_tx_sgi = (ra_info1>>7)&0x01;
				DBG_8192C("[ ra_info1:0x%08x ] =>cur_tx_rate= %s,cur_sgi:%d, PWRSTS = 0x%02x  \n",
					ra_info1,						
					HDATA_RATE(curr_tx_rate),
					curr_tx_sgi,
					(ra_info1>>8)  & 0x07);

				cmd = 0x40000400 | mac_id;
				rtw_write32(padapter, REG_HMEBOX_E2_E3_8812,cmd);
				rtw_msleep_os(10);
				ra_info1 = rtw_read32(padapter, REG_RSVD5_8812);
				ra_info2 = rtw_read32(padapter, REG_RSVD6_8812);
				rate_mask1 = rtw_read32(padapter,REG_RSVD7_8812);
				rate_mask2 = rtw_read32(padapter,REG_RSVD8_8812);
				hight_rate = ra_info2&0xFF;
				lowest_rate = (ra_info2>>8)  & 0xFF;	
				DBG_8192C("[ ra_info1:0x%08x ] =>RSSI=%d, BW_setting=0x%02x, DISRA=0x%02x, VHT_EN=0x%02x\n",
					ra_info1,
					ra_info1&0xFF,
					(ra_info1>>8)  & 0xFF,
					(ra_info1>>16) & 0xFF,
					(ra_info1>>24) & 0xFF);
					
				DBG_8192C("[ ra_info2:0x%08x ] =>hight_rate=%s, lowest_rate=%s, SGI=0x%02x, RateID=%d\n",
					ra_info2,
					HDATA_RATE(hight_rate),
					HDATA_RATE(lowest_rate),
					(ra_info2>>16) & 0xFF,
					(ra_info2>>24) & 0xFF);
				DBG_8192C("rate_mask2=0x%08x, rate_mask1=0x%08x\n", rate_mask2, rate_mask1);				
			}
			break;

		case HAL_DEF_TX_PAGE_SIZE:
			if (IS_HARDWARE_TYPE_8812(padapter))
				*(u32*)pval = PAGE_SIZE_512;
			else
				*(u32*)pval = PAGE_SIZE_256;
			break;

		case HAL_DEF_TX_PAGE_BOUNDARY:
			if (!padapter->registrypriv.wifi_spec)
			{
				if (IS_HARDWARE_TYPE_8812(padapter))
					*(u8*)pval = TX_PAGE_BOUNDARY_8812;
				else
					*(u8*)pval = TX_PAGE_BOUNDARY_8821;
			}
			else
			{
				if (IS_HARDWARE_TYPE_8812(padapter))
					*(u8*)pval = WMM_NORMAL_TX_PAGE_BOUNDARY_8812;
				else
					*(u8*)pval = WMM_NORMAL_TX_PAGE_BOUNDARY_8821;
			}
			break;

		case HAL_DEF_TX_PAGE_BOUNDARY_WOWLAN:
			*(u8*)pval = TX_PAGE_BOUNDARY_WOWLAN_8812;
			break;

		case HAL_DEF_MACID_SLEEP:
			*(u8*)pval = _TRUE; // support macid sleep
			break;

		default:
			bResult = GetHalDefVar(padapter, variable, pval);
			break;
	}

	return bResult;
}

s32 c2h_id_filter_ccx_8812a(u8 *buf)
{
	struct c2h_evt_hdr_88xx *c2h_evt = (struct c2h_evt_hdr_88xx *)buf;
	s32 ret = _FALSE;
	if (c2h_evt->id == C2H_8812_TX_REPORT)
		ret = _TRUE;
	
	return ret;
}

static s32 c2h_handler_8812a(PADAPTER padapter, u8 *buf)
{
	struct c2h_evt_hdr_88xx *c2h_evt = (struct c2h_evt_hdr_88xx *)buf;
	s32 ret = _SUCCESS;

	if (c2h_evt == NULL) {
		DBG_8192C("%s c2h_evt is NULL\n",__FUNCTION__);
		ret = _FAIL;
		goto exit;
	}

	ret = _C2HContentParsing8812(padapter, c2h_evt->id, c2h_evt->plen, c2h_evt->payload);

exit:
	return ret;
}

void rtl8812_set_hal_ops(struct hal_ops *pHalFunc)
{
	pHalFunc->free_hal_data = &rtl8812_free_hal_data;

	pHalFunc->dm_init = &rtl8812_init_dm_priv;
	pHalFunc->dm_deinit = &rtl8812_deinit_dm_priv;

	pHalFunc->SetBeaconRelatedRegistersHandler = &SetBeaconRelatedRegisters8812A;

	pHalFunc->UpdateRAMaskHandler = &UpdateHalRAMask8812A;

	pHalFunc->read_chip_version = &ReadChipVersion8812A;

	pHalFunc->set_bwmode_handler = &PHY_SetBWMode8812;
	pHalFunc->set_channel_handler = &PHY_SwChnl8812;
	pHalFunc->set_chnl_bw_handler = &PHY_SetSwChnlBWMode8812;

	pHalFunc->set_tx_power_level_handler = &PHY_SetTxPowerLevel8812;
	pHalFunc->get_tx_power_level_handler = &PHY_GetTxPowerLevel8812;

	pHalFunc->hal_dm_watchdog = &rtl8812_HalDmWatchDog;

	pHalFunc->Add_RateATid = &rtl8812_Add_RateATid;

	pHalFunc->run_thread= &rtl8812_start_thread;
	pHalFunc->cancel_thread= &rtl8812_stop_thread;

#ifdef CONFIG_ANTENNA_DIVERSITY
	pHalFunc->AntDivBeforeLinkHandler = &AntDivBeforeLink8812;
	pHalFunc->AntDivCompareHandler = &AntDivCompare8812;
#endif

	pHalFunc->read_bbreg = &PHY_QueryBBReg8812;
	pHalFunc->write_bbreg = &PHY_SetBBReg8812;
	pHalFunc->read_rfreg = &PHY_QueryRFReg8812;
	pHalFunc->write_rfreg = &PHY_SetRFReg8812;


	// Efuse related function
	pHalFunc->EfusePowerSwitch = &rtl8812_EfusePowerSwitch;
	pHalFunc->ReadEFuse = &rtl8812_ReadEFuse;
	pHalFunc->EFUSEGetEfuseDefinition = &rtl8812_EFUSE_GetEfuseDefinition;
	pHalFunc->EfuseGetCurrentSize = &rtl8812_EfuseGetCurrentSize;
	pHalFunc->Efuse_PgPacketRead = &rtl8812_Efuse_PgPacketRead;
	pHalFunc->Efuse_PgPacketWrite = &rtl8812_Efuse_PgPacketWrite;
	pHalFunc->Efuse_WordEnableDataWrite = &rtl8812_Efuse_WordEnableDataWrite;

#ifdef DBG_CONFIG_ERROR_DETECT
	pHalFunc->sreset_init_value = &sreset_init_value;
	pHalFunc->sreset_reset_value = &sreset_reset_value;
	pHalFunc->silentreset = &sreset_reset;
	pHalFunc->sreset_xmit_status_check = &rtl8812_sreset_xmit_status_check;
	pHalFunc->sreset_linked_status_check  = &rtl8812_sreset_linked_status_check;
	pHalFunc->sreset_get_wifi_status  = &sreset_get_wifi_status;
	pHalFunc->sreset_inprogress= &sreset_inprogress;
#endif //DBG_CONFIG_ERROR_DETECT

	pHalFunc->GetHalODMVarHandler = &rtl8812_GetHalODMVar;
	pHalFunc->SetHalODMVarHandler = &rtl8812_SetHalODMVar;
	pHalFunc->hal_notch_filter = &hal_notch_filter_8812;

	pHalFunc->SetBeaconRelatedRegistersHandler = &SetBeaconRelatedRegisters8812A;

	pHalFunc->c2h_handler = c2h_handler_8812a;
	pHalFunc->c2h_id_filter_ccx = c2h_id_filter_ccx_8812a;

	pHalFunc->fill_h2c_cmd = FillH2CCmd_8812;
}


