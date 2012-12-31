/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
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
#define _RTL8712_RF_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>


//io_cmd format : (class<<24)|index
//Rate Adaptive related
#define IOCMD_CLS_FD_IDX_A0 0xFD0000A0
#define IOCMD_CLS_FD_IDX_A1 0xFD0000A1
#define IOCMD_CLS_FD_IDX_A2 0xFD0000A2
#define IOCMD_CLS_FD_IDX_A3 0xFD0000A3
#define IOCMD_CLS_FD_IDX_A4 0xFD0000A4
#define IOCMD_CLS_FD_IDX_A5 0xFD0000A5
#define IOCMD_CLS_FD_IDX_A6 0xFD0000A6
#define IOCMD_CLS_FD_IDX_A7 0xFD0000A7
#define IOCMD_CLS_FD_IDX_A8 0xFD0000A8
#define IOCMD_CLS_FD_IDX_A9 0xFD0000A9
#define IOCMD_CLS_FD_IDX_AF 0xFD0000AF

//BB/RF register read/write
#define IOCMD_CLS_F0_IDX_00 0xF0000000 //BB_READ
#define IOCMD_CLS_F0_IDX_01 0xF0000001 //BB_WRITE
#define IOCMD_CLS_F0_IDX_02 0xF0000002 //RF_READ
#define IOCMD_CLS_F0_IDX_03 0xF0000003 //RF_WRIT
#define IOCMD_CLS_F0_IDX_04 0xF0000004	//read EEPROM/EFUSE content



//#define	bMaskDWord	0xffffffff

#define	bMask20Bits        0xfffff	// RF Reg mask bits T65 RF

#define	HST_RDBUSY		BIT(0)

// The following two definition are only used for USB interface.
#if 0
#define	RF_BB_CMD_ADDR			0x02c0	// RF/BB read/write command address.
#define	RF_BB_CMD_DATA			0x02c4	// RF/BB read/write command data.
#else
#define	RF_BB_CMD_ADDR			0x10250370
#define	RF_BB_CMD_DATA			0x10250374
#endif

#define IOCMD_CTRL_ADDR		RF_BB_CMD_ADDR
#define IOCMD_DATA_ADDR		RF_BB_CMD_DATA


/**
* Function:	phy_CalculateBitShift
*
* OverView:	Get shifted position of the BitMask
*
* Input:
*			u4Byte		BitMask,	
*
* Output:	none
* Return:		u4Byte		Return the shift bit bit position of the mask
*/
static u32 phy_CalculateBitShift(u32 BitMask)
{
	u32 i;

	for(i=0; i<=31; i++)
	{
		if ( ((BitMask>>i) &  0x1 ) == 1)
			break;
	}

	return (i);
}

u32 phy_QueryBBReg(IN PADAPTER	Adapter, IN u32 RegAddr)
{	
	u32	ReturnValue = 0xffffffff;
	u8	PollingCnt = 50;	
	
	read32(Adapter, RegAddr);	

	do
	{
		// Make sure that access could be done.
		if((read8(Adapter, PHY_REG_RPT)&HST_RDBUSY) == 0)
			break;
		
	}while( --PollingCnt );

	if(PollingCnt == 0)
	{
		//ERR_8712 ("Fail!!!phy_QueryBBReg(): RegAddr(%#x) = %#x\n", RegAddr, ReturnValue);
	}
	else
	{
		// Data FW read back.
		ReturnValue = read32(Adapter, PHY_REG_DATA);		
	}

	return ReturnValue;
	
}

void phy_SetBBReg(IN	PADAPTER Adapter, IN	u32 RegAddr, IN	u32 Data)
{
	write32(Adapter, RegAddr, Data);	
}

u32 phy_QueryRFReg(
	IN	PADAPTER	Adapter,
	IN	int			eRFPath,
	IN	u32			Offset
	)
{	
	u32	ReturnValue = 0;	
	u8	PollingCnt = 50;


	Offset &= 0x3f; //RF_Offset= 0x00~0x3F		
	
	write32(Adapter, RF_BB_CMD_ADDR, 0xF0000002|(Offset<<8)|//RF_Offset= 0x00~0x3F
											(eRFPath<<16)); 	//RF_Path = 0(A) or 1(B)
	
	do
	{
		// Make sure that access could be done.
		if(read32(Adapter, RF_BB_CMD_ADDR) == 0)
			break;
		
	}while( --PollingCnt );

	// Data FW read back.
	ReturnValue = read32(Adapter, RF_BB_CMD_DATA);	
	
	return ReturnValue;

}

void phy_SetRFReg(
	IN	PADAPTER	Adapter,
	IN	int			eRFPath,
	IN	u32			RegAddr,
	IN	u32			Data
	)
{
	u8	PollingCnt = 50;
	
	
	RegAddr &= 0x3f; //RF_Offset= 0x00~0x3F
	
	write32(Adapter, RF_BB_CMD_DATA, Data);	
	write32(Adapter, RF_BB_CMD_ADDR, 0xF0000003|(RegAddr<<8)| //RF_Offset= 0x00~0x3F
												(eRFPath<<16));  //RF_Path = 0(A) or 1(B)
	
	do
	{
		// Make sure that access could be done.
		if(read32(Adapter, RF_BB_CMD_ADDR) == 0)
				break;
		
	}while( --PollingCnt );		

	if(PollingCnt == 0)
	{		
		//ERR_8712("phy_SetRFReg(): Set RegAddr(%#x) = %#x Fail!!!\n", RegAddr, Data);
	}
	

}

void SetBBReg(PADAPTER padapter, u32 addr, u32 bitmask, u32 data)
{
	u32	OriginalValue, BitShift, NewValue;

	if(bitmask!= bMaskDWord)//if not "double word" write
	{		
		OriginalValue = phy_QueryBBReg(padapter, addr);
		BitShift = phy_CalculateBitShift(bitmask);
            	NewValue = ((OriginalValue & (~bitmask)) | (data << BitShift));
		phy_SetBBReg(padapter, addr, NewValue);	
	}
	else
	{
		phy_SetBBReg(padapter, addr, data);	
	}	

}

u32 QueryBBReg(PADAPTER padapter, u32 addr, u32 bitmask)
{
  	u32	ReturnValue = 0, OriginalValue, BitShift;

	//
	// <Roger_Notes> Due to 8051 operation cycle (limitation cycle: 6us) and 1-Byte access issue, we should use 
	// 4181 to access Base Band instead of 8051 on USB interface to make sure that access could be done in 
	// infinite cycle.
	// 2008.09.06.
	//

	OriginalValue = phy_QueryBBReg(padapter, addr);
	
	if(bitmask!= bMaskDWord)//if not "double word" write
	{		
		//OriginalValue = phy_QueryBBReg(padapter, addr);

		BitShift = phy_CalculateBitShift(bitmask);
		ReturnValue = (OriginalValue & bitmask) >> BitShift;
	}
	else
	{
		//ReturnValue = phy_QueryBBReg(padapter, addr);
		ReturnValue = OriginalValue;
	}

	return (ReturnValue);	
	
}

void SetRFReg(PADAPTER padapter, int rfpath, u32 addr, u32 bitmask, u32 data)
{	
	u32 	Original_Value, BitShift, New_Value;

#if 0//gtest
	if (!Adapter->HalFunc.PHYCheckIsLegalRfPathHandler(Adapter, eRFPath))
	{
		return;
	}
#endif

	//
	// <Roger_Notes> Due to 8051 operation cycle (limitation cycle: 6us) and 1-Byte access issue, we should use 
	// 4181 to access Base Band instead of 8051 on USB interface to make sure that access could be done in 
	// infinite cycle.
	// 2008.09.06.
	//

	if (bitmask != bMask20Bits) // RF data is 12 bits only
	{
		Original_Value = phy_QueryRFReg(padapter, rfpath, addr);
		BitShift =  phy_CalculateBitShift(bitmask);
		New_Value = ((Original_Value & (~bitmask)) | (data<< BitShift));
		phy_SetRFReg(padapter, rfpath, addr, New_Value);
	}
	else
	{
		phy_SetRFReg(padapter, rfpath, addr, data);
	}	
	
}

u32 QueryRFReg(PADAPTER padapter, int rfpath, u32 addr, u32 bitmask)
{
	u32 Original_Value, Readback_Value, BitShift;

	Original_Value = phy_QueryRFReg(padapter, rfpath, addr);

	if(bitmask != bMask20Bits)
	{
		BitShift =  phy_CalculateBitShift(bitmask);
		Readback_Value = (Original_Value & bitmask) >> BitShift;	
	}
	else
	{
		Readback_Value = Original_Value;
	}
	
	return (Readback_Value);
}

static u32 bitshift(u32 bitmask)
{
	u32 i;
	for(i=0; i<=31; i++){
		if ( ((bitmask>>i) &  0x1 ) == 1)	break;
	}
	return (i);
}

static u32 fw_iocmd_read(PADAPTER pAdapter , u32 iocmd)
{
	u32 cmd32 = 0, val32 = 0;	
	int pollingcnts = 50;

	
	cmd32 = cpu_to_le32(iocmd);	
		
	write32(pAdapter, IOCMD_CTRL_ADDR, cmd32);
	
	//usleep_os(100);
	
	while( (0 != read32(pAdapter, IOCMD_CTRL_ADDR) ) && (pollingcnts>0) ){
			
		pollingcnts--;
		usleep_os(10);
	}

	if( pollingcnts != 0){	
		val32 = read32(pAdapter, IOCMD_DATA_ADDR);
	}
	else{//time out	
		val32 = 0;
		//DBG_8712 ("fw_iocmd_read timeout ........\n");
	}
			
	return le32_to_cpu(val32);
	
}

static u8 fw_iocmd_write(PADAPTER pAdapter , u32 iocmd, u32 value)
{
	u32 cmd32 = 0;
	int	pollingcnts = 50;

	while( (0 != read32(pAdapter, IOCMD_CTRL_ADDR)) && (pollingcnts>0) )
	{
		usleep_os(10);
		pollingcnts--;
	}
	pollingcnts = 50;
	
	write32(pAdapter, IOCMD_DATA_ADDR, cpu_to_le32(value));
	
	//usleep_os(100);
	
	cmd32 = cpu_to_le32(iocmd);
	
	write32(pAdapter, IOCMD_CTRL_ADDR , cmd32);	  	
	
	//usleep_os(100);
	
	while( (0 != read32(pAdapter, IOCMD_CTRL_ADDR)) && (pollingcnts>0) )
	{
		usleep_os(10);
		pollingcnts--;
	}

	return (pollingcnts == 0) ?_TRUE : _FALSE ;
	
}

int set_ratid_cmd(PADAPTER pAdapter, unsigned short param, unsigned int bitmap)
{	
	u8 ret;
	u32 iocmd =  0xfd0000a2 | ((param<<8)	&0x00ffff00) ; 
	
	ret = fw_iocmd_write(pAdapter, iocmd, bitmap);

	return ((ret) ? _TRUE:_FALSE);
}

static u32 bb_read_cmd(PADAPTER pAdapter, u16 offset)// offset : 0X800~0XFFF 
{	
	u16 bb_addr = offset & 0x0FFF;
	u32 bb_val, iocmd;
	
	iocmd = IOCMD_CLS_F0_IDX_00 | (bb_addr<<8) ; 
		
	bb_val = fw_iocmd_read(pAdapter, iocmd);
	
	return bb_val;	
}

static u8 bb_write_cmd(PADAPTER pAdapter, u16 offset, u32 value)// offset : 0X800~0XFFF 
{
	u32 iocmd;
	u16 bb_addr = offset & 0x0FFF  ;

	iocmd = IOCMD_CLS_F0_IDX_01 | (bb_addr<<8) ; 
	
	return fw_iocmd_write(pAdapter, iocmd , value);
}

static u32 rf_read_cmd(PADAPTER pAdapter, u8 path, u8 offset) // offset : 0x00 ~ 0xFF
{	
	u16 rf_addr;
	u32 rf_data, iocmd;
	
	rf_addr = (path << 8 ) | offset;
	iocmd = IOCMD_CLS_F0_IDX_02 | (rf_addr<<8) ; 

	rf_data =  fw_iocmd_read(pAdapter, iocmd);
	
	return rf_data;
	
}
static u8 rf_write_cmd(PADAPTER pAdapter, u8 path, u8 offset, u32 value)
{	
	u16 rf_addr;
	u32 rf_data, iocmd;
	
	rf_addr = (path << 8 ) | offset;
	iocmd = IOCMD_CLS_F0_IDX_03 | (rf_addr<<8) ;

	return fw_iocmd_write(pAdapter, iocmd , value);	
}

u32 get_bbreg(PADAPTER pAdapter ,u16 offset ,u32 bitmask)	
{
	u32 org_value,bit_shift,new_value;
	
	org_value = bb_read_cmd(pAdapter ,offset);

	bit_shift = bitshift(bitmask);
		
	new_value =( org_value & bitmask) >> bit_shift;
	
	return new_value;
}
u8 set_bbreg(PADAPTER pAdapter, u16 offset, u32 bitmask, u32 value)
{
	u32 org_value,bit_shift,new_value;
	
	if(bitmask!=bMaskDWord)
	{		
		org_value = bb_read_cmd(pAdapter ,offset);		
		bit_shift = bitshift(bitmask);
		new_value =  ((org_value &  (~bitmask)) | (value << bit_shift) );
		//DBG_8712("set_bbreg #2  offset :0x%04x org:0x%08x  new:0x%08x.........\n",offset,org_value,new_value);
	}
	else
	{
		new_value = value;
		//DBG_8712("set_bbreg #2  offset :0x%04x data:0x%08x.........\n",offset,new_value);
	}
	
	return bb_write_cmd(pAdapter,offset,new_value);
}

u32 get_rfreg(PADAPTER pAdapter ,u8 path,u8 offset,u32 bitmask)
{
	u32 org_value,bit_shift,new_value;
	
	org_value = rf_read_cmd(pAdapter , path, offset) ;
	
	bit_shift = bitshift(bitmask);
	
	new_value =( org_value & bitmask) >> bit_shift;
		
	return new_value;
} 

u8 set_rfreg(PADAPTER pAdapter, u8 path, u8 offset, u32 bitmask, u32 value)
{
	u32 org_value,bit_shift,new_value;
	
	if(bitmask!=bMaskDWord)
	{	
		org_value = rf_read_cmd(pAdapter , path, offset) ;
		bit_shift = bitshift(bitmask);
		new_value =  ((org_value &  (~bitmask)) | (value << bit_shift) );
		
		//DBG_8712("set_rfreg #2  v:0x%08x org:0x%08x  new:0x%08x.........\n", value, org_value, new_value);
	}
	else
	{
		new_value = 	value ;
		//DBG_8712("set_rfreg #2  v:0x%08x data:0x%08x.........\n", value, new_value);
	}
	
	return rf_write_cmd(pAdapter,path,offset,new_value);
	
}


u32 get_efuse_content(_adapter *padapter, u16 offset)
{
	u32 iocmd;	
	u16 addr = offset & 0xFFFF;	
	
	iocmd = IOCMD_CLS_F0_IDX_04 | (addr<<8) ; 
		
	return fw_iocmd_read(padapter, iocmd);

}

void dump_efuse_content(_adapter *padapter, unsigned int *pbuf, int sz)
{
	int i, limit;
	

	limit = sz>>2;
	

	for(i=0; i<limit; i++)
	{
		*(pbuf+i) = get_efuse_content(padapter, (i<<2));		
	}
	
}

