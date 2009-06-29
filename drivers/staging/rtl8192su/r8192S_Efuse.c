/******************************************************************************
 *
 *     (c) Copyright  2008, RealTEK Technologies Inc. All Rights Reserved.
 *
 * Module:	Efuse.c	( Source C File)
 *
 * Note:		Copy from WMAC for the first version!!!!
 *
 *
 * Function:
 *
 * Export:
 *
 * Abbrev:
 *
 * History:
 * Data			Who		Remark
 *
 * 09/23/2008	MHC		Porting Efuse R/W API from WMAC.
 * 11/10/2008	MHC		1. Porting from 8712 EFUSE.
 *						2. Add description and reorganize code arch.
 * 11/16/2008 	MHC		1. Reorganize code architecture.
 *						2. Rename for some API and change extern or static type.
 *
******************************************************************************/
#include "r8192U.h"
#include "r8192S_hw.h"
#include "r8192S_phy.h"
#include "r8192S_phyreg.h"
#include "r8192S_Efuse.h"

#include <linux/types.h>

//typedef  int	INT32;
//
// In the future, we will always support EFUSE!!
//
#ifdef RTL8192SU
/*---------------------------Define Local Constant---------------------------*/
#define 	_POWERON_DELAY_
#define 	_PRE_EXECUTE_READ_CMD_

#define		EFUSE_REPEAT_THRESHOLD_		3
#define		EFUSE_ERROE_HANDLE		1


// From 8712!!!!!
typedef struct _EFUSE_MAP_A{
	u8 offset;		//0~15
	u8 word_start;	//0~3
	u8 byte_start;	//0 or 1
	u8 byte_cnts;

}EFUSE_MAP, *PEFUSE_MAP;

typedef struct PG_PKT_STRUCT_A{
	u8 offset;
	u8 word_en;
	u8 data[8];
}PGPKT_STRUCT,*PPGPKT_STRUCT;

typedef enum _EFUSE_DATA_ITEM{
	EFUSE_CHIP_ID=0,
	EFUSE_LDO_SETTING,
	EFUSE_CLK_SETTING,
	EFUSE_SDIO_SETTING,
	EFUSE_CCCR,
	EFUSE_SDIO_MODE,
	EFUSE_OCR,
	EFUSE_F0CIS,
	EFUSE_F1CIS,
	EFUSE_MAC_ADDR,
	EFUSE_EEPROM_VER,
	EFUSE_CHAN_PLAN,
	EFUSE_TXPW_TAB
} EFUSE_DATA_ITEM;

struct efuse_priv
{
	u8		id[2];
	u8		ldo_setting[2];
	u8		clk_setting[2];
	u8		cccr;
	u8		sdio_mode;
	u8		ocr[3];
	u8		cis0[17];
	u8		cis1[48];
	u8		mac_addr[6];
	u8		eeprom_verno;
	u8		channel_plan;
	u8		tx_power_b[14];
	u8		tx_power_g[14];
};

/*---------------------------Define Local Constant---------------------------*/


/*------------------------Define global variable-----------------------------*/
const u8 MAX_PGPKT_SIZE = 9; //header+ 2* 4 words (BYTES)
const u8 PGPKT_DATA_SIZE = 8; //BYTES sizeof(u8)*8
const u32 EFUSE_MAX_SIZE = 512;


const EFUSE_MAP RTL8712_SDIO_EFUSE_TABLE[]={
				//offset	word_s	byte_start	byte_cnts
/*ID*/			{0		,0		,0			,2	}, // 00~01h
/*LDO Setting*/	{0		,1		,0			,2	}, // 02~03h
/*CLK Setting*/	{0		,2		,0			,2	}, // 04~05h
/*SDIO Setting*/	{1		,0		,0			,1	}, // 08h
/*CCCR*/		{1		,0		,1			,1	}, // 09h
/*SDIO MODE*/	{1		,1		,0			,1	}, // 0Ah
/*OCR*/			{1		,1		,1			,3	}, // 0B~0Dh
/*CCIS*/			{1		,3		,0			,17	}, // 0E~1Eh  2...1
/*F1CIS*/		{3		,3		,1			,48	}, // 1F~4Eh  6...0
/*MAC Addr*/		{10		,0		,0			,6	}, // 50~55h
/*EEPROM ver*/	{10		,3		,0			,1	}, // 56h
/*Channel plan*/	{10		,3		,1			,1	}, // 57h
/*TxPwIndex */	{11		,0		,0			,28	}  // 58~73h  3...4
};

/*------------------------Define global variable-----------------------------*/


/*------------------------Define local variable------------------------------*/

/*------------------------Define local variable------------------------------*/


/*--------------------Define function prototype-----------------------*/
//
// From WMAC Efuse one byte R/W
//
extern	void
EFUSE_Initialize(struct net_device* dev);
extern	u8
EFUSE_Read1Byte(struct net_device* dev, u16 Address);
extern	void
EFUSE_Write1Byte(struct net_device* dev, u16 Address,u8 Value);

//
// Efuse Shadow Area operation
//
static	void
efuse_ShadowRead1Byte(struct net_device* dev,u16 Offset,u8 *Value);
static	void
efuse_ShadowRead2Byte(struct net_device* dev,	u16 Offset,u16 *Value	);
static	void
efuse_ShadowRead4Byte(struct net_device* dev,	u16 Offset,u32 *Value	);
static	void
efuse_ShadowWrite1Byte(struct net_device* dev,	u16 Offset, u8 Value);
static	void
efuse_ShadowWrite2Byte(struct net_device* dev,	u16 Offset,u16 Value);
static	void
efuse_ShadowWrite4Byte(struct net_device* dev,	u16 Offset,u32 Value);

//
// Real Efuse operation
//
static	u8
efuse_OneByteRead(struct net_device* dev,u16 addr,u8 *data);
static	u8
efuse_OneByteWrite(struct net_device* dev,u16 addr, u8 data);

//
// HW setting map file operation
//
static	void
efuse_ReadAllMap(struct net_device* dev,u8 *Efuse);
#ifdef TO_DO_LIST
static	void
efuse_WriteAllMap(struct net_device* dev,u8 *eeprom,u32 eeprom_size);
static	bool
efuse_ParsingMap(char* szStr,u32* pu4bVal,u32* pu4bMove);
#endif
//
// Reald Efuse R/W or other operation API.
//
static	u8
efuse_PgPacketRead(	struct net_device* dev,u8	offset,u8 *data);
static	u8
efuse_PgPacketWrite(struct net_device* dev,u8 offset,u8 word_en,u8	*data);
static	void
efuse_WordEnableDataRead(	u8 word_en,u8 *sourdata,u8 *targetdata);
static	u8
efuse_WordEnableDataWrite(	struct net_device* dev, u16 efuse_addr, u8 word_en, u8 *data);
static	void
efuse_PowerSwitch(struct net_device* dev,u8 PwrState);
static	u16
efuse_GetCurrentSize(struct net_device* dev);
static u8
efuse_CalculateWordCnts(u8 word_en);
#if 0
static	void
efuse_ResetLoader(struct net_device* dev);
#endif
//
// API for power on power off!!!
//
#ifdef TO_DO_LIST
static void efuse_reg_ctrl(struct net_device* dev, u8 bPowerOn);
#endif
/*--------------------Define function prototype-----------------------*/



/*-----------------------------------------------------------------------------
 * Function:	EFUSE_Initialize
 *
 * Overview:	Copy from WMAC fot EFUSE testing setting init.
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 09/23/2008 	MHC		Copy from WMAC.
 *
 *---------------------------------------------------------------------------*/
extern	void
EFUSE_Initialize(struct net_device* dev)
{
	u8	Bytetemp = {0x00};
	u8	temp = {0x00};

	//Enable Digital Core Vdd : 0x2[13]=1
	Bytetemp = read_nic_byte(dev, SYS_FUNC_EN+1);
	temp = Bytetemp | 0x20;
	write_nic_byte(dev, SYS_FUNC_EN+1, temp);

	//EE loader to retention path1: attach 0x0[8]=0
	Bytetemp = read_nic_byte(dev, SYS_ISO_CTRL+1);
	temp = Bytetemp & 0xFE;
	write_nic_byte(dev, SYS_ISO_CTRL+1, temp);


	//Enable E-fuse use 2.5V LDO : 0x37[7]=1
	Bytetemp = read_nic_byte(dev, EFUSE_TEST+3);
	temp = Bytetemp | 0x80;
	write_nic_byte(dev, EFUSE_TEST+3, temp);

	//E-fuse clk switch from 500k to 40M : 0x2F8[1:0]=11b
	write_nic_byte(dev, 0x2F8, 0x3);

	//Set E-fuse program time & read time : 0x30[30:24]=1110010b
	write_nic_byte(dev, EFUSE_CTRL+3, 0x72);

}	/* EFUSE_Initialize */


/*-----------------------------------------------------------------------------
 * Function:	EFUSE_Read1Byte
 *
 * Overview:	Copy from WMAC fot EFUSE read 1 byte.
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 09/23/2008 	MHC		Copy from WMAC.
 *
 *---------------------------------------------------------------------------*/
extern	u8
EFUSE_Read1Byte(struct net_device* dev, u16	Address)
{
	u8	data;
	u8	Bytetemp = {0x00};
	u8	temp = {0x00};
	u32	k=0;

	if (Address < EFUSE_MAC_LEN)	//E-fuse 512Byte
	{
		//Write E-fuse Register address bit0~7
		temp = Address & 0xFF;
		write_nic_byte(dev, EFUSE_CTRL+1, temp);
		Bytetemp = read_nic_byte(dev, EFUSE_CTRL+2);
		//Write E-fuse Register address bit8~9
		temp = ((Address >> 8) & 0x03) | (Bytetemp & 0xFC);
		write_nic_byte(dev, EFUSE_CTRL+2, temp);

		//Write 0x30[31]=0
		Bytetemp = read_nic_byte(dev, EFUSE_CTRL+3);
		temp = Bytetemp & 0x7F;
		write_nic_byte(dev, EFUSE_CTRL+3, temp);

		//Wait Write-ready (0x30[31]=1)
		Bytetemp = read_nic_byte(dev, EFUSE_CTRL+3);
		while(!(Bytetemp & 0x80))
		{
			Bytetemp = read_nic_byte(dev, EFUSE_CTRL+3);
			k++;
			if(k==1000)
			{
				k=0;
				break;
			}
		}
		data=read_nic_byte(dev, EFUSE_CTRL);
		return data;
	}
	else
		return 0xFF;

}	/* EFUSE_Read1Byte */


/*-----------------------------------------------------------------------------
 * Function:	EFUSE_Write1Byte
 *
 * Overview:	Copy from WMAC fot EFUSE write 1 byte.
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 09/23/2008 	MHC		Copy from WMAC.
 *
 *---------------------------------------------------------------------------*/
extern	void
EFUSE_Write1Byte(struct net_device* dev, u16 Address,u8 Value)
{
	//u8	data;
	u8	Bytetemp = {0x00};
	u8	temp = {0x00};
	u32	k=0;

	//RT_TRACE(COMP_EFUSE, "Addr=%x Data =%x\n", Address, Value);

	if( Address < EFUSE_MAC_LEN)	//E-fuse 512Byte
	{
		write_nic_byte(dev, EFUSE_CTRL, Value);

		//Write E-fuse Register address bit0~7
		temp = Address & 0xFF;
		write_nic_byte(dev, EFUSE_CTRL+1, temp);
		Bytetemp = read_nic_byte(dev, EFUSE_CTRL+2);

		//Write E-fuse Register address bit8~9
		temp = ((Address >> 8) & 0x03) | (Bytetemp & 0xFC);
		write_nic_byte(dev, EFUSE_CTRL+2, temp);

		//Write 0x30[31]=1
		Bytetemp = read_nic_byte(dev, EFUSE_CTRL+3);
		temp = Bytetemp | 0x80;
		write_nic_byte(dev, EFUSE_CTRL+3, temp);

		//Wait Write-ready (0x30[31]=0)
		Bytetemp = read_nic_byte(dev, EFUSE_CTRL+3);
		while(Bytetemp & 0x80)
		{
			Bytetemp = read_nic_byte(dev, EFUSE_CTRL+3);
			k++;
			if(k==100)
			{
				k=0;
				break;
			}
		}
	}

}	/* EFUSE_Write1Byte */


#ifdef EFUSE_FOR_92SU
//
//	Description:
//		1. Process CR93C46 Data polling cycle.
//		2. Refered from SD1 Richard.
//
//	Assumption:
//		1. Boot from E-Fuse and successfully auto-load.
//		2. PASSIVE_LEVEL (USB interface)
//
//	Created by Roger, 2008.10.21.
//
void do_93c46(struct net_device* dev,  u8 addorvalue)
{
    	//u8  clear[1] = {0x0};      // cs=0 , sk=0 , di=0 , do=0
	u8  cs[1] = {0x88};        // cs=1 , sk=0 , di=0 , do=0
	u8  cssk[1] = {0x8c};      // cs=1 , sk=1 , di=0 , do=0
	u8  csdi[1] = {0x8a};      // cs=1 , sk=0 , di=1 , do=0
    	u8  csskdi[1] = {0x8e};    // cs=1 , sk=1 , di=1 , do=0
	//u8  di[1] = {0x82};        // cs=0 , sk=0 , di=1 , do=0
    	u8  count;

    	for(count=0 ; count<8 ; count++)
	{
		if((addorvalue&0x80)!=0)
		{
			write_nic_byte(dev, EPROM_CMD, csdi[0]);
			write_nic_byte(dev, EPROM_CMD, csskdi[0]);
		}
		else
		{
			write_nic_byte(dev, EPROM_CMD, cs[0]);
			write_nic_byte(dev, EPROM_CMD, cssk[0]);
		}
		addorvalue = addorvalue << 1;
	}
}


//
//	Description:
//		Process CR93C46 Data read polling cycle.
//		Refered from SD1 Richard.
//
//	Assumption:
//		1. Boot from E-Fuse and successfully auto-load.
//		2. PASSIVE_LEVEL (USB interface)
//
//	Created by Roger, 2008.10.21.
//
u16 Read93C46(struct net_device*	dev,	u16	Reg	)
{

   	u8  	clear[1] = {0x0};      // cs=0 , sk=0 , di=0 , do=0
	u8  	cs[1] = {0x88};        // cs=1 , sk=0 , di=0 , do=0
	u8  	cssk[1] = {0x8c};      // cs=1 , sk=1 , di=0 , do=0
	u8  	csdi[1] = {0x8a};      // cs=1 , sk=0 , di=1 , do=0
   	u8  	csskdi[1] = {0x8e};    // cs=1 , sk=1 , di=1 , do=0
	//u8  	di[1] = {0x82};        // cs=0 , sk=0 , di=1 , do=0
	u8  	EepromSEL[1]={0x00};
	u8  	address;

	u16   	storedataF[1] = {0x0};   //93c46 data packet for 16bits
	u8   	t,data[1],storedata[1];


	address = (u8)Reg;

	// Suggested by SD1 Alex, 2008.10.20. Revised by Roger.
	*EepromSEL= read_nic_byte(dev, EPROM_CMD);

	if((*EepromSEL & 0x10) == 0x10) // select 93c46
	{
		address = address | 0x80;

		write_nic_byte(dev, EPROM_CMD, csdi[0]);
		write_nic_byte(dev, EPROM_CMD, csskdi[0]);
   		do_93c46(dev, address);
	}


	for(t=0 ; t<16 ; t++)      //if read 93c46 , t=16
	{
		write_nic_byte(dev, EPROM_CMD, cs[0]);
		write_nic_byte(dev, EPROM_CMD, cssk[0]);
		*data= read_nic_byte(dev, EPROM_CMD);

		if(*data & 0x8d) //original code
		{
			*data = *data & 0x01;
			*storedata = *data;
		}
		else
		{
			*data = *data & 0x01 ;
			*storedata = *data;
		}
		*storedataF = (*storedataF << 1 ) + *storedata;
	}
	write_nic_byte(dev, EPROM_CMD, cs[0]);
	write_nic_byte(dev, EPROM_CMD, clear[0]);

	return *storedataF;
}


//
//	Description:
//		Execute E-Fuse read byte operation.
//		Refered from SD1 Richard.
//
//	Assumption:
//		1. Boot from E-Fuse and successfully auto-load.
//		2. PASSIVE_LEVEL (USB interface)
//
//	Created by Roger, 2008.10.21.
//
void
ReadEFuseByte(struct net_device* dev,u16 _offset, u8 *pbuf)
{

	//u16 	indexk=0;
	u32  value32;
	u8 	readbyte;
	u16 	retry;


	//Write Address
	write_nic_byte(dev, EFUSE_CTRL+1, (_offset & 0xff));
	readbyte = read_nic_byte(dev, EFUSE_CTRL+2);
	write_nic_byte(dev, EFUSE_CTRL+2, ((_offset >> 8) & 0x03) | (readbyte & 0xfc));

	//Write bit 32 0
	readbyte = read_nic_byte(dev, EFUSE_CTRL+3);
	write_nic_byte(dev, EFUSE_CTRL+3, (readbyte & 0x7f));

	//Check bit 32 read-ready
	retry = 0;
	value32 = read_nic_dword(dev, EFUSE_CTRL);
	//while(!(((value32 >> 24) & 0xff) & 0x80)  && (retry<10))
	while(!(((value32 >> 24) & 0xff) & 0x80)  && (retry<10000))
	{
		value32 = read_nic_dword(dev, EFUSE_CTRL);
		retry++;
	}
	*pbuf = (u8)(value32 & 0xff);
}


#define		EFUSE_READ_SWITCH		1
//
//	Description:
//		1. Execute E-Fuse read byte operation according as map offset and
//		    save to E-Fuse table.
//		2. Refered from SD1 Richard.
//
//	Assumption:
//		1. Boot from E-Fuse and successfully auto-load.
//		2. PASSIVE_LEVEL (USB interface)
//
//	Created by Roger, 2008.10.21.
//
void
ReadEFuse(struct net_device* dev, u16	 _offset, u16 _size_byte, u8 *pbuf)
{

	u8  	efuseTbl[128];
	u8  	rtemp8[1];
	u16 	eFuse_Addr = 0;
	u8  	offset, wren;
	u16  	i, j;
	u16 	eFuseWord[16][4];// = {0xFF};//FIXLZM

	for(i=0; i<16; i++)
		for(j=0; j<4; j++)
			eFuseWord[i][j]=0xFF;

	// Do NOT excess total size of EFuse table. Added by Roger, 2008.11.10.
	if((_offset + _size_byte)>128)
	{// total E-Fuse table is 128bytes
		//RT_TRACE(COMP_EFUSE, "ReadEFuse(): Invalid offset(%#x) with read bytes(%#x)!!\n",_offset, _size_byte);
		printk("ReadEFuse(): Invalid offset with read bytes!!\n");
		return;
	}

	// Refresh efuse init map as all oxFF.
	for (i = 0; i < 128; i++)
		efuseTbl[i] = 0xFF;

#if (EFUSE_READ_SWITCH == 1)
	ReadEFuseByte(dev, eFuse_Addr, rtemp8);
#else
	rtemp8[0] = EFUSE_Read1Byte(dev, eFuse_Addr);
#endif
	if(*rtemp8 != 0xFF)		eFuse_Addr++;
	while((*rtemp8 != 0xFF) && (eFuse_Addr < 512)){
		offset = ((*rtemp8 >> 4) & 0x0f);
		if(offset <= 0x0F){
			wren = (*rtemp8 & 0x0f);
			for(i=0; i<4; i++){
				if(!(wren & 0x01)){
#if (EFUSE_READ_SWITCH == 1)
					ReadEFuseByte(dev, eFuse_Addr, rtemp8);	eFuse_Addr++;
#else
					rtemp8[0] = EFUSE_Read1Byte(dev, eFuse_Addr);	eFuse_Addr++;
#endif
					eFuseWord[offset][i] = (*rtemp8 & 0xff);
					if(eFuse_Addr >= 512) break;
#if (EFUSE_READ_SWITCH == 1)
					ReadEFuseByte(dev, eFuse_Addr, rtemp8);	eFuse_Addr++;
#else
					rtemp8[0] = EFUSE_Read1Byte(dev, eFuse_Addr);	eFuse_Addr++;
#endif
					eFuseWord[offset][i] |= (((u16)*rtemp8 << 8) & 0xff00);
					if(eFuse_Addr >= 512) break;
				}
				wren >>= 1;
			}
		}
#if (EFUSE_READ_SWITCH == 1)
		ReadEFuseByte(dev, eFuse_Addr, rtemp8);
#else
		rtemp8[0] = EFUSE_Read1Byte(dev, eFuse_Addr);	eFuse_Addr++;
#endif
		if(*rtemp8 != 0xFF && (eFuse_Addr < 512))	eFuse_Addr++;
	}

	for(i=0; i<16; i++){
		for(j=0; j<4; j++){
			efuseTbl[(i*8)+(j*2)]=(eFuseWord[i][j] & 0xff);
			efuseTbl[(i*8)+((j*2)+1)]=((eFuseWord[i][j] >> 8) & 0xff);
		}
	}
	for(i=0; i<_size_byte; i++)
		pbuf[i] = efuseTbl[_offset+i];
}
#endif	// #if (EFUSE_FOR_92SU == 1)


/*-----------------------------------------------------------------------------
 * Function:	EFUSE_ShadowRead
 *
 * Overview:	Read from efuse init map !!!!!
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 11/12/2008 	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
extern void
EFUSE_ShadowRead(	struct net_device*	dev,	u8 Type, u16 Offset, u32 *Value)
{
	//HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);

	if (Type == 1)
		efuse_ShadowRead1Byte(dev, Offset, (u8 *)Value);
	else if (Type == 2)
		efuse_ShadowRead2Byte(dev, Offset, (u16 *)Value);
	else if (Type == 4)
		efuse_ShadowRead4Byte(dev, Offset, (u32 *)Value);

}	// EFUSE_ShadowRead


/*-----------------------------------------------------------------------------
 * Function:	EFUSE_ShadowWrite
 *
 * Overview:	Write efuse modify map for later update operation to use!!!!!
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 11/12/2008 	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
extern	void
EFUSE_ShadowWrite(	struct net_device*	dev,	u8 Type, u16 Offset,u32	Value)
{
	//HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);

	if (Offset >= 0x18 && Offset <= 0x1F)
		return;

	if (Type == 1)
		efuse_ShadowWrite1Byte(dev, Offset, (u8)Value);
	else if (Type == 2)
		efuse_ShadowWrite2Byte(dev, Offset, (u16)Value);
	else if (Type == 4)
		efuse_ShadowWrite4Byte(dev, Offset, (u32)Value);

}	// EFUSE_ShadowWrite


/*-----------------------------------------------------------------------------
 * Function:	EFUSE_ShadowUpdate
 *
 * Overview:	Compare init and modify map to update Efuse!!!!!
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 11/12/2008 	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
extern	void
EFUSE_ShadowUpdate(struct net_device* dev)
{
	//HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	struct r8192_priv *priv = ieee80211_priv(dev);
	u16			i, offset, base = 0;
	u8			word_en = 0x0F;
	bool first_pg = false;
	// For Efuse write action, we must enable LDO2.5V and 40MHZ clk.
	efuse_PowerSwitch(dev, TRUE);

	//
	// Efuse support 16 write are with PG header packet!!!!
	//
	for (offset = 0; offset < 16; offset++)
	{
		// Offset 0x18-1F are reserved now!!!
#ifdef RTL8192SE
		if(priv->card_8192 == NIC_8192SE){
			if (offset == 3)
				continue;
		}
#endif
		word_en = 0x0F;
		base = offset * 8;

		//
		// Decide Word Enable Bit for the Efuse section
		// One section contain 4 words = 8 bytes!!!!!
		//
		for (i = 0; i < 8; i++)
		{
			if (offset == 0 && priv->EfuseMap[EFUSE_INIT_MAP][base+i] == 0xFF)
			{
				first_pg = TRUE;
			}

			// 2008/12/11 MH HW autoload fail workaround for A/BCUT.
#ifdef RTL8192SE
			if (first_pg == TRUE && offset == 1 && (priv->card_8192 == NIC_8192SE))
			{
				continue;
			}
#endif

			if (first_pg == TRUE)
			{
				word_en &= ~(1<<(i/2));
				priv->EfuseMap[EFUSE_INIT_MAP][base+i] =
				priv->EfuseMap[EFUSE_MODIFY_MAP][base+i];
			}else
			{
			if (	priv->EfuseMap[EFUSE_INIT_MAP][base+i] !=
				priv->EfuseMap[EFUSE_MODIFY_MAP][base+i])
			{
				word_en &= ~(EFUSE_BIT(i/2));
				//RT_TRACE(COMP_EFUSE,  "Offset=%d Addr%x %x ==> %x Word_En=%02x\n",
				//offset, base+i, priv->EfuseMap[0][base+i], priv->EfuseMap[1][base+i],word_en);

				// Update init table!!!
				priv->EfuseMap[EFUSE_INIT_MAP][base+i] =
				priv->EfuseMap[EFUSE_MODIFY_MAP][base+i];
				}
			}
		}

		//
		// Call Efuse real write section !!!!
		//
		if (word_en != 0x0F)
		{
			u8	tmpdata[8];

			//FIXLZM
			memcpy(tmpdata, &(priv->EfuseMap[EFUSE_MODIFY_MAP][base]), 8);
			//RT_PRINT_DATA(COMP_INIT, DBG_LOUD, ("U-EFUSE\n"), tmpdata, 8);
			efuse_PgPacketWrite(dev,(u8)offset,word_en,tmpdata);
		}

	}
	// 2008/12/01 MH For Efuse HW load bug workarounf method!!!!
	// We will force write 0x10EC into address 10&11 after all Efuse content.
	//
#ifdef RTL8192SE
	if (first_pg == TRUE && (priv->card_8192 == NIC_8192SE))
	{
		// 2008/12/11 MH Use new method to prevent HW autoload fail.
		u8	tmpdata[8];

		memcpy(tmpdata, (&priv->EfuseMap[EFUSE_MODIFY_MAP][8]), 8);
		efuse_PgPacketWrite(dev, 1, 0x0, tmpdata);
#if 0
		u1Byte	tmpdata[8] = {0xFF, 0xFF, 0xEC, 0x10, 0xFF, 0xFF, 0xFF, 0xFF};

		efuse_PgPacketWrite(pAdapter, 1, 0xD, tmpdata);
#endif
	}
#endif


	// For warm reboot, we must resume Efuse clock to 500K.
	efuse_PowerSwitch(dev, FALSE);
	// 2008/12/01 MH We update shadow content again!!!!
	EFUSE_ShadowMapUpdate(dev);

}	// EFUSE_ShadowUpdate


/*-----------------------------------------------------------------------------
 * Function:	EFUSE_ShadowMapUpdate
 *
 * Overview:	Transfer current EFUSE content to shadow init and modify map.
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 11/13/2008 	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
extern void EFUSE_ShadowMapUpdate(struct net_device* dev)
{
	struct r8192_priv *priv = ieee80211_priv(dev);

	if (priv->AutoloadFailFlag == true){
		memset(&(priv->EfuseMap[EFUSE_INIT_MAP][0]), 0xff, 128);
	}else{
		efuse_ReadAllMap(dev, &priv->EfuseMap[EFUSE_INIT_MAP][0]);
	}
	//PlatformMoveMemory(&priv->EfuseMap[EFUSE_MODIFY_MAP][0],
		//&priv->EfuseMap[EFUSE_INIT_MAP][0], HWSET_MAX_SIZE_92S);//FIXLZM
	memcpy(&priv->EfuseMap[EFUSE_MODIFY_MAP][0],
		&priv->EfuseMap[EFUSE_INIT_MAP][0], HWSET_MAX_SIZE_92S);

}	// EFUSE_ShadowMapUpdate

extern	void
EFUSE_ForceWriteVendorId( struct net_device* dev)
{
	u8 tmpdata[8] = {0xFF, 0xFF, 0xEC, 0x10, 0xFF, 0xFF, 0xFF, 0xFF};

	efuse_PowerSwitch(dev, TRUE);

	efuse_PgPacketWrite(dev, 1, 0xD, tmpdata);

	efuse_PowerSwitch(dev, FALSE);

}	// EFUSE_ForceWriteVendorId

/*-----------------------------------------------------------------------------
 * Function:	efuse_ShadowRead1Byte
 *			efuse_ShadowRead2Byte
 *			efuse_ShadowRead4Byte
 *
 * Overview:	Read from efuse init map by one/two/four bytes !!!!!
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 11/12/2008 	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
static	void
efuse_ShadowRead1Byte(struct net_device*	dev,	u16 Offset,	u8 *Value)
{
	struct r8192_priv *priv = ieee80211_priv(dev);

	*Value = priv->EfuseMap[EFUSE_MODIFY_MAP][Offset];

}	// EFUSE_ShadowRead1Byte

//---------------Read Two Bytes
static	void
efuse_ShadowRead2Byte(struct net_device*	dev,	u16 Offset,	u16 *Value)
{
	struct r8192_priv *priv = ieee80211_priv(dev);

	*Value = priv->EfuseMap[EFUSE_MODIFY_MAP][Offset];
	*Value |= priv->EfuseMap[EFUSE_MODIFY_MAP][Offset+1]<<8;

}	// EFUSE_ShadowRead2Byte

//---------------Read Four Bytes
static	void
efuse_ShadowRead4Byte(struct net_device*	dev,	u16 Offset,	u32 *Value)
{
	struct r8192_priv *priv = ieee80211_priv(dev);

	*Value = priv->EfuseMap[EFUSE_MODIFY_MAP][Offset];
	*Value |= priv->EfuseMap[EFUSE_MODIFY_MAP][Offset+1]<<8;
	*Value |= priv->EfuseMap[EFUSE_MODIFY_MAP][Offset+2]<<16;
	*Value |= priv->EfuseMap[EFUSE_MODIFY_MAP][Offset+3]<<24;

}	// efuse_ShadowRead4Byte



/*-----------------------------------------------------------------------------
 * Function:	efuse_ShadowWrite1Byte
 *			efuse_ShadowWrite2Byte
 *			efuse_ShadowWrite4Byte
 *
 * Overview:	Write efuse modify map by one/two/four byte.
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 11/12/2008 	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
static	void
efuse_ShadowWrite1Byte(struct net_device*	dev,	u16 Offset,	u8 Value)
{
	struct r8192_priv *priv = ieee80211_priv(dev);

	priv->EfuseMap[EFUSE_MODIFY_MAP][Offset] = Value;

}	// efuse_ShadowWrite1Byte

//---------------Write Two Bytes
static	void
efuse_ShadowWrite2Byte(struct net_device*	dev,	u16 Offset,	u16 Value)
{
	struct r8192_priv *priv = ieee80211_priv(dev);

	priv->EfuseMap[EFUSE_MODIFY_MAP][Offset] = Value&0x00FF;
	priv->EfuseMap[EFUSE_MODIFY_MAP][Offset+1] = Value>>8;

}	// efuse_ShadowWrite1Byte

//---------------Write Four Bytes
static	void
efuse_ShadowWrite4Byte(struct net_device*	dev,	u16 Offset,	u32 Value)
{
	struct r8192_priv *priv = ieee80211_priv(dev);

	priv->EfuseMap[EFUSE_MODIFY_MAP][Offset] = (u8)(Value&0x000000FF);
	priv->EfuseMap[EFUSE_MODIFY_MAP][Offset+1] = (u8)((Value>>8)&0x0000FF);
	priv->EfuseMap[EFUSE_MODIFY_MAP][Offset+2] = (u8)((Value>>16)&0x00FF);
	priv->EfuseMap[EFUSE_MODIFY_MAP][Offset+3] = (u8)((Value>>24)&0xFF);

}	// efuse_ShadowWrite1Byte


/*  11/16/2008 MH Read one byte from real Efuse. */
static	u8
efuse_OneByteRead(struct net_device* dev, u16 addr,u8 *data)
{
	u8 tmpidx = 0;
	u8 bResult;

	// -----------------e-fuse reg ctrl ---------------------------------
	//address
	write_nic_byte(dev, EFUSE_CTRL+1, (u8)(addr&0xff));
	write_nic_byte(dev, EFUSE_CTRL+2, ((u8)((addr>>8) &0x03) ) |
	(read_nic_byte(dev, EFUSE_CTRL+2)&0xFC ));

	write_nic_byte(dev, EFUSE_CTRL+3,  0x72);//read cmd

	while(!(0x80 &read_nic_byte(dev, EFUSE_CTRL+3))&&(tmpidx<100))
	{
		tmpidx++;
	}
	if(tmpidx<100)
	{
		*data=read_nic_byte(dev, EFUSE_CTRL);
		bResult = TRUE;
	}
	else
	{
		*data = 0xff;
		bResult = FALSE;
	}
	return bResult;
}	// efuse_OneByteRead

/*  11/16/2008 MH Write one byte to reald Efuse. */
static	u8
efuse_OneByteWrite(struct net_device* dev,  u16 addr, u8 data)
{
	u8 tmpidx = 0;
	u8 bResult;

	//RT_TRACE(COMP_EFUSE, "Addr = %x Data=%x\n", addr, data);

	//return	0;

	// -----------------e-fuse reg ctrl ---------------------------------
	//address
	write_nic_byte(dev, EFUSE_CTRL+1, (u8)(addr&0xff));
	write_nic_byte(dev, EFUSE_CTRL+2,
	read_nic_byte(dev, EFUSE_CTRL+2)|(u8)((addr>>8)&0x03) );

	write_nic_byte(dev, EFUSE_CTRL, data);//data
	write_nic_byte(dev, EFUSE_CTRL+3, 0xF2);//write cmd

	while((0x80 &  read_nic_byte(dev, EFUSE_CTRL+3)) && (tmpidx<100) ){
		tmpidx++;
	}

	if(tmpidx<100)
	{
		bResult = TRUE;
	}
	else
	{
		bResult = FALSE;
	}

	return bResult;
}	// efuse_OneByteWrite


/*-----------------------------------------------------------------------------
 * Function:	efuse_ReadAllMap
 *
 * Overview:	Read All Efuse content
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 11/11/2008 	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
static	void
efuse_ReadAllMap(struct net_device*	dev, u8	*Efuse)
{
	//u8 	pg_data[8];
	//u8 	offset = 0;
	//u8 	tmpidx;
	//static	u8	index = 0;

	//
	// We must enable clock and LDO 2.5V otherwise, read all map will be fail!!!!
	//
	efuse_PowerSwitch(dev, TRUE);
	ReadEFuse(dev, 0, 128, Efuse);
	efuse_PowerSwitch(dev, FALSE);
#if 0
	// ==> Prevent efuse read error!!!
	RT_TRACE(COMP_INIT, "efuse_ResetLoader\n");
	efuse_ResetLoader(dev);

	// Change Efuse Clock for write action to 40MHZ
	write_nic_byte(dev, EFUSE_CLK, 0x03);

	ReadEFuse(dev, 0, 128, Efuse);

	// Change Efuse Clock for write action to 500K
	write_nic_byte(dev, EFUSE_CLK, 0x02);
#if 0	// Error !!!!!!
	for(offset = 0;offset<16;offset++)	// For 8192SE
	{
		PlatformFillMemory((PVOID)pg_data, 8, 0xff);
		efuse_PgPacketRead(pAdapter,offset,pg_data);

		PlatformMoveMemory((PVOID)&Efuse[offset*8], (PVOID)pg_data, 8);
	}
#endif

	//
	// Error Check and Reset Again!!!!
	//
	if (Efuse[0] != 0x29 || Efuse[1] != 0x81)
	{
		// SW autoload fail, we have to read again!!!
		if (index ++ < 5)
		{
			RT_TRACE(COMP_INIT, "EFUSE R FAIL %d\n", index);
			efuse_ReadAllMap(dev, Efuse);
			// Wait a few time ???? Or need to do some setting ???
			// When we reload driver, efuse will be OK!!
		}
	}
	else
	{
		index = 0;
	}

	//efuse_PowerSwitch(pAdapter, FALSE);
#endif
}	// efuse_ReadAllMap


/*-----------------------------------------------------------------------------
 * Function:	efuse_WriteAllMap
 *
 * Overview:	Write All Efuse content
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 11/11/2008 	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
#ifdef TO_DO_LIST
static	void
efuse_WriteAllMap(struct net_device* dev,u8 *eeprom, u32 eeprom_size)
{
	unsigned char word_en = 0x00;

	unsigned char tmpdata[8];
	unsigned char offset;

	// For Efuse write action, we must enable LDO2.5V and 40MHZ clk.
	efuse_PowerSwitch(dev, TRUE);

	//sdio contents
	for(offset=0 ; offset< eeprom_size/PGPKT_DATA_SIZE ; offset++)
	{
		// 92S will only reserv 0x18-1F 8 bytes now. The 3rd efuse write area!
		if (IS_HARDWARE_TYPE_8192SE(dev))
		{
			// Refer to
			// 0x18-1f Reserve >0x50 Reserve for tx power
			if (offset == 3/* || offset > 9*/)
				continue;//word_en = 0x0F;
			//else if (offset == 9)	// 0x4c-4f Reserve
				//word_en = 0x0C;
			else
				word_en = 0x00;
		}
		//RT_TRACE(COMP_EFUSE, ("Addr=%d size=%d Word_En=%02x\n", offset, eeprom_size, word_en));

		//memcpy(tmpdata,eeprom+(offset*PGPKT_DATA_SIZE),8);
		memcpy(tmpdata, (eeprom+(offset*PGPKT_DATA_SIZE)), 8);

		//RT_PRINT_DATA(COMP_INIT, DBG_LOUD, ("EFUSE\t"), tmpdata, 8);

		efuse_PgPacketWrite(dev,offset,word_en,tmpdata);


	}

	// For warm reboot, we must resume Efuse clock to 500K.
	efuse_PowerSwitch(dev, FALSE);

}	// efuse_WriteAllMap
#endif

/*-----------------------------------------------------------------------------
 * Function:	efuse_PgPacketRead
 *
 * Overview:	Receive dedicated Efuse are content. For92s, we support 16
 *				area now. It will return 8 bytes content for every area.
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 11/16/2008 	MHC		Reorganize code Arch and assign as local API.
 *
 *---------------------------------------------------------------------------*/
static	u8
efuse_PgPacketRead(	struct net_device*	dev,	u8 offset, u8	*data)
{
	u8 ReadState = PG_STATE_HEADER;

	bool bContinual = TRUE;
	bool  bDataEmpty = TRUE ;

	u8 efuse_data,word_cnts=0;
	u16 efuse_addr = 0;
	u8 hoffset=0,hworden=0;
	u8 tmpidx=0;
	u8 tmpdata[8];

	if(data==NULL)	return FALSE;
	if(offset>15)		return FALSE;

	//FIXLZM
	//PlatformFillMemory((PVOID)data, sizeof(u8)*PGPKT_DATA_SIZE, 0xff);
	//PlatformFillMemory((PVOID)tmpdata, sizeof(u8)*PGPKT_DATA_SIZE, 0xff);
	memset(data, 0xff, sizeof(u8)*PGPKT_DATA_SIZE);
	memset(tmpdata, 0xff, sizeof(u8)*PGPKT_DATA_SIZE);

	//RT_PRINT_DATA(COMP_EFUSE, DBG_LOUD, ("efuse_PgPacketRead-1\n"), data, 8);

	//efuse_reg_ctrl(pAdapter,TRUE);//power on
	while(bContinual && (efuse_addr  < EFUSE_MAX_SIZE) )
	{
		//-------  Header Read -------------
		if(ReadState & PG_STATE_HEADER)
		{
			if(efuse_OneByteRead(dev, efuse_addr ,&efuse_data)&&(efuse_data!=0xFF)){
				hoffset = (efuse_data>>4) & 0x0F;
				hworden =  efuse_data & 0x0F;
				word_cnts = efuse_CalculateWordCnts(hworden);
				bDataEmpty = TRUE ;

				if(hoffset==offset){
					for(tmpidx = 0;tmpidx< word_cnts*2 ;tmpidx++){
						if(efuse_OneByteRead(dev, efuse_addr+1+tmpidx ,&efuse_data) ){
							tmpdata[tmpidx] = efuse_data;
							if(efuse_data!=0xff){
								bDataEmpty = FALSE;
							}
						}
					}
					if(bDataEmpty==FALSE){
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
				bContinual = FALSE ;
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
	//efuse_reg_ctrl(pAdapter,FALSE);//power off

	//RT_PRINT_DATA(COMP_EFUSE, DBG_LOUD, ("efuse_PgPacketRead-2\n"), data, 8);

	if(	(data[0]==0xff) &&(data[1]==0xff) && (data[2]==0xff)  && (data[3]==0xff) &&
		(data[4]==0xff) &&(data[5]==0xff) && (data[6]==0xff)  && (data[7]==0xff))
		return FALSE;
	else
		return TRUE;

}	// efuse_PgPacketRead


/*-----------------------------------------------------------------------------
 * Function:	efuse_PgPacketWrite
 *
 * Overview:	Send A G package for different section in real efuse area.
 *				For 92S, One PG package contain 8 bytes content and 4 word
 *				unit. PG header = 0x[bit7-4=offset][bit3-0word enable]
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 11/16/2008 	MHC		Reorganize code Arch and assign as local API.
 *
 *---------------------------------------------------------------------------*/
static u8 efuse_PgPacketWrite(struct net_device* dev, u8 offset, u8 word_en,u8 *data)
{
	u8 WriteState = PG_STATE_HEADER;

	bool bContinual = TRUE,bDataEmpty=TRUE, bResult = TRUE;
	u16 efuse_addr = 0;
	u8 efuse_data;

	u8 pg_header = 0;

	//u16 tmp_addr=0;
	u8 tmp_word_cnts=0,target_word_cnts=0;
	u8 tmp_header,match_word_en,tmp_word_en;

	//u8	efuse_clk_ori,efuse_clk_new;

	PGPKT_STRUCT target_pkt;
	PGPKT_STRUCT tmp_pkt;

	u8 originaldata[sizeof(u8)*8];
	u8 tmpindex = 0,badworden = 0x0F;

	static u32 repeat_times = 0;

	if( efuse_GetCurrentSize(dev) >= EFUSE_MAX_SIZE)
	{
		printk("efuse_PgPacketWrite error \n");
		return FALSE;
	}

	// Init the 8 bytes content as 0xff
	target_pkt.offset = offset;
	target_pkt.word_en= word_en;

	//PlatformFillMemory((PVOID)target_pkt.data, sizeof(u8)*8, 0xFF);
	memset(target_pkt.data,0xFF,sizeof(u8)*8);

	efuse_WordEnableDataRead(word_en,data,target_pkt.data);
	target_word_cnts = efuse_CalculateWordCnts(target_pkt.word_en);

	//efuse_reg_ctrl(pAdapter,TRUE);//power on
	printk("EFUSE Power ON\n");

	while( bContinual && (efuse_addr  < EFUSE_MAX_SIZE) )
	{

		if(WriteState==PG_STATE_HEADER)
		{
			bDataEmpty=TRUE;
			badworden = 0x0F;
			//************  so *******************
			printk("EFUSE PG_STATE_HEADER\n");
			if (	efuse_OneByteRead(dev, efuse_addr ,&efuse_data) &&
				(efuse_data!=0xFF))
			{
				tmp_header  =  efuse_data;

				tmp_pkt.offset 	= (tmp_header>>4) & 0x0F;
				tmp_pkt.word_en 	= tmp_header & 0x0F;
				tmp_word_cnts =  efuse_CalculateWordCnts(tmp_pkt.word_en);

				//************  so-1 *******************
				if(tmp_pkt.offset  != target_pkt.offset)
				{
					efuse_addr = efuse_addr + (tmp_word_cnts*2) +1; //Next pg_packet
					#if (EFUSE_ERROE_HANDLE == 1)
					WriteState = PG_STATE_HEADER;
					#endif
				}
				else
				{
					//************  so-2 *******************
					for(tmpindex=0 ; tmpindex<(tmp_word_cnts*2) ; tmpindex++)
					{
						if(efuse_OneByteRead(dev, (efuse_addr+1+tmpindex) ,&efuse_data)&&(efuse_data != 0xFF)){
							bDataEmpty = FALSE;
						}
					}
					//************  so-2-1 *******************
					if(bDataEmpty == FALSE)
					{
						efuse_addr = efuse_addr + (tmp_word_cnts*2) +1; //Next pg_packet
						#if (EFUSE_ERROE_HANDLE == 1)
						WriteState=PG_STATE_HEADER;
						#endif
					}
					else
					{//************  so-2-2 *******************
						match_word_en = 0x0F;
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

						//************  so-2-2-A *******************
						if((match_word_en&0x0F)!=0x0F)
						{
							badworden = efuse_WordEnableDataWrite(dev,efuse_addr+1, tmp_pkt.word_en ,target_pkt.data);

							//************  so-2-2-A-1 *******************
							//############################
							if(0x0F != (badworden&0x0F))
							{
								u8 reorg_offset = offset;
								u8 reorg_worden=badworden;
								efuse_PgPacketWrite(dev,reorg_offset,reorg_worden,originaldata);
							}
							//############################

							tmp_word_en = 0x0F;
							if(  (target_pkt.word_en&BIT0)^(match_word_en&BIT0)  )
							{
								tmp_word_en &= (~BIT0);
							}
							if(   (target_pkt.word_en&BIT1)^(match_word_en&BIT1) )
							{
								tmp_word_en &=  (~BIT1);
							}
							if(   (target_pkt.word_en&BIT2)^(match_word_en&BIT2) )
							{
								tmp_word_en &= (~BIT2);
							}
							if(   (target_pkt.word_en&BIT3)^(match_word_en&BIT3) )
							{
								tmp_word_en &=(~BIT3);
							}

							//************  so-2-2-A-2 *******************
							if((tmp_word_en&0x0F)!=0x0F){
								//reorganize other pg packet
								//efuse_addr = efuse_addr + (2*tmp_word_cnts) +1;//next pg packet addr
								efuse_addr = efuse_GetCurrentSize(dev);
								//===========================
								target_pkt.offset = offset;
								target_pkt.word_en= tmp_word_en;
								//===========================
							}else{
								bContinual = FALSE;
							}
							#if (EFUSE_ERROE_HANDLE == 1)
							WriteState=PG_STATE_HEADER;
							repeat_times++;
							if(repeat_times>EFUSE_REPEAT_THRESHOLD_){
								bContinual = FALSE;
								bResult = FALSE;
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
				printk("EFUSE PG_STATE_HEADER-1\n");
			}
			else		//************  s1: header == oxff  *******************
			{
				pg_header = ((target_pkt.offset << 4)&0xf0) |target_pkt.word_en;

				efuse_OneByteWrite(dev,efuse_addr, pg_header);
				efuse_OneByteRead(dev,efuse_addr, &tmp_header);

				if(tmp_header == pg_header)
				{ //************  s1-1*******************
					WriteState = PG_STATE_DATA;
				}
				#if (EFUSE_ERROE_HANDLE == 1)
				else if(tmp_header == 0xFF){//************  s1-3: if Write or read func doesn't work *******************
					//efuse_addr doesn't change
					WriteState = PG_STATE_HEADER;
					repeat_times++;
					if(repeat_times>EFUSE_REPEAT_THRESHOLD_){
						bContinual = FALSE;
						bResult = FALSE;
					}
				}
				#endif
				else
				{//************  s1-2 : fixed the header procedure *******************
					tmp_pkt.offset = (tmp_header>>4) & 0x0F;
					tmp_pkt.word_en=  tmp_header & 0x0F;
					tmp_word_cnts =  efuse_CalculateWordCnts(tmp_pkt.word_en);

					//************  s1-2-A :cover the exist data *******************
					memset(originaldata,0xff,sizeof(u8)*8);
					//PlatformFillMemory((PVOID)originaldata, sizeof(u8)*8, 0xff);

					if(efuse_PgPacketRead( dev, tmp_pkt.offset,originaldata))
					{	//check if data exist
						//efuse_reg_ctrl(pAdapter,TRUE);//power on
						badworden = efuse_WordEnableDataWrite(dev,efuse_addr+1,tmp_pkt.word_en,originaldata);
						//############################
						if(0x0F != (badworden&0x0F))
						{
							u8 reorg_offset = tmp_pkt.offset;
							u8 reorg_worden=badworden;
							efuse_PgPacketWrite(dev,reorg_offset,reorg_worden,originaldata);
							efuse_addr = efuse_GetCurrentSize(dev);
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
						bContinual = FALSE;
						bResult = FALSE;
					}
					#endif

					printk("EFUSE PG_STATE_HEADER-2\n");
				}

			}

		}
		//write data state
		else if(WriteState==PG_STATE_DATA)
		{	//************  s1-1  *******************
			printk("EFUSE PG_STATE_DATA\n");
			badworden = 0x0f;
			badworden = efuse_WordEnableDataWrite(dev,efuse_addr+1,target_pkt.word_en,target_pkt.data);
			if((badworden&0x0F)==0x0F)
			{ //************  s1-1-A *******************
				bContinual = FALSE;
			}
			else
			{//reorganize other pg packet //************  s1-1-B *******************
				efuse_addr = efuse_addr + (2*target_word_cnts) +1;//next pg packet addr

				//===========================
				target_pkt.offset = offset;
				target_pkt.word_en= badworden;
				target_word_cnts =  efuse_CalculateWordCnts(target_pkt.word_en);
				//===========================
				#if (EFUSE_ERROE_HANDLE == 1)
				WriteState=PG_STATE_HEADER;
				repeat_times++;
				if(repeat_times>EFUSE_REPEAT_THRESHOLD_){
					bContinual = FALSE;
					bResult = FALSE;
				}
				#endif
				printk("EFUSE PG_STATE_HEADER-3\n");
			}
		}
	}

	//efuse_reg_ctrl(pAdapter,FALSE);//power off

	return TRUE;
}	// efuse_PgPacketWrite


/*-----------------------------------------------------------------------------
 * Function:	efuse_WordEnableDataRead
 *
 * Overview:	Read allowed word in current efuse section data.
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 11/16/2008 	MHC		Create Version 0.
 * 11/21/2008 	MHC		Fix Write bug when we only enable late word.
 *
 *---------------------------------------------------------------------------*/
static	void
efuse_WordEnableDataRead(	u8 word_en,u8 *sourdata,u8 *targetdata)
{
	//u8 tmpindex = 0;

	//DbgPrint("efuse_WordEnableDataRead word_en = %x\n", word_en);

	//RT_PRINT_DATA(COMP_EFUSE, DBG_LOUD, ("sourdata\n"), sourdata, 8);
	//RT_PRINT_DATA(COMP_EFUSE, DBG_LOUD, ("targetdata\n"), targetdata, 8);

	if (!(word_en&BIT0))
	{
		targetdata[0] = sourdata[0];//sourdata[tmpindex++];
		targetdata[1] = sourdata[1];//sourdata[tmpindex++];
	}
	if (!(word_en&BIT1))
	{
		targetdata[2] = sourdata[2];//sourdata[tmpindex++];
		targetdata[3] = sourdata[3];//sourdata[tmpindex++];
	}
	if (!(word_en&BIT2))
	{
		targetdata[4] = sourdata[4];//sourdata[tmpindex++];
		targetdata[5] = sourdata[5];//sourdata[tmpindex++];
	}
	if (!(word_en&BIT3))
	{
		targetdata[6] = sourdata[6];//sourdata[tmpindex++];
		targetdata[7] = sourdata[7];//sourdata[tmpindex++];
	}
}	// efuse_WordEnableDataRead


/*-----------------------------------------------------------------------------
 * Function:	efuse_WordEnableDataWrite
 *
 * Overview:	Write necessary word unit into current efuse section!
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 11/16/2008 	MHC		Reorganize Efuse operate flow!!.
 *
 *---------------------------------------------------------------------------*/
static	u8
efuse_WordEnableDataWrite(	struct net_device*	dev,	u16 efuse_addr, u8 word_en, u8 *data)
{
	u16 tmpaddr = 0;
	u16 start_addr = efuse_addr;
	u8 badworden = 0x0F;
	//u8 NextState;
	u8 tmpdata[8];

	memset(tmpdata,0xff,PGPKT_DATA_SIZE);
	//PlatformFillMemory((PVOID)tmpdata, PGPKT_DATA_SIZE, 0xff);

	//RT_TRACE(COMP_EFUSE, "word_en = %x efuse_addr=%x\n", word_en, efuse_addr);

	//RT_PRINT_DATA(COMP_EFUSE, DBG_LOUD, ("U-EFUSE\n"), data, 8);

	if(!(word_en&BIT0))
	{
		tmpaddr = start_addr;
		efuse_OneByteWrite(dev,start_addr++, data[0]);
		efuse_OneByteWrite(dev,start_addr++, data[1]);

		efuse_OneByteRead(dev,tmpaddr, &tmpdata[0]);
		efuse_OneByteRead(dev,tmpaddr+1, &tmpdata[1]);
		if((data[0]!=tmpdata[0])||(data[1]!=tmpdata[1])){
			badworden &= (~BIT0);
		}
	}
	if(!(word_en&BIT1))
	{
		tmpaddr = start_addr;
		efuse_OneByteWrite(dev,start_addr++, data[2]);
		efuse_OneByteWrite(dev,start_addr++, data[3]);

		efuse_OneByteRead(dev,tmpaddr    , &tmpdata[2]);
		efuse_OneByteRead(dev,tmpaddr+1, &tmpdata[3]);
		if((data[2]!=tmpdata[2])||(data[3]!=tmpdata[3])){
			badworden &=( ~BIT1);
		}
	}
	if(!(word_en&BIT2))
	{
		tmpaddr = start_addr;
		efuse_OneByteWrite(dev,start_addr++, data[4]);
		efuse_OneByteWrite(dev,start_addr++, data[5]);

		efuse_OneByteRead(dev,tmpaddr, &tmpdata[4]);
		efuse_OneByteRead(dev,tmpaddr+1, &tmpdata[5]);
		if((data[4]!=tmpdata[4])||(data[5]!=tmpdata[5])){
			badworden &=( ~BIT2);
		}
	}
	if(!(word_en&BIT3))
	{
		tmpaddr = start_addr;
		efuse_OneByteWrite(dev,start_addr++, data[6]);
		efuse_OneByteWrite(dev,start_addr++, data[7]);

		efuse_OneByteRead(dev,tmpaddr, &tmpdata[6]);
		efuse_OneByteRead(dev,tmpaddr+1, &tmpdata[7]);
		if((data[6]!=tmpdata[6])||(data[7]!=tmpdata[7])){
			badworden &=( ~BIT3);
		}
	}
	return badworden;
}	// efuse_WordEnableDataWrite


/*-----------------------------------------------------------------------------
 * Function:	efuse_PowerSwitch
 *
 * Overview:	When we want to enable write operation, we should change to
 *				pwr on state. When we stop write, we should switch to 500k mode
 *				and disable LDO 2.5V.
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 11/17/2008 	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
static	void
efuse_PowerSwitch(struct net_device* dev, u8 PwrState)
{
	u8	tempval;
	if (PwrState == TRUE)
	{
		// Enable LDO 2.5V for write action
		tempval = read_nic_byte(dev, EFUSE_TEST+3);
		write_nic_byte(dev, EFUSE_TEST+3, (tempval | 0x80));

		// Change Efuse Clock for write action to 40MHZ
		write_nic_byte(dev, EFUSE_CLK, 0x03);
	}
	else
	{
		// Enable LDO 2.5V for write action
		tempval = read_nic_byte(dev, EFUSE_TEST+3);
		write_nic_byte(dev, EFUSE_TEST+3, (tempval & 0x7F));

		// Change Efuse Clock for write action to 500K
		write_nic_byte(dev, EFUSE_CLK, 0x02);
	}

}	/* efuse_PowerSwitch */


/*-----------------------------------------------------------------------------
 * Function:	efuse_GetCurrentSize
 *
 * Overview:	Get current efuse size!!!
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 11/16/2008 	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
static	u16
efuse_GetCurrentSize(struct net_device*	dev)
{
	bool bContinual = TRUE;

	u16 efuse_addr = 0;
	u8 hoffset=0,hworden=0;
	u8 efuse_data,word_cnts=0;

	//efuse_reg_ctrl(pAdapter,TRUE);//power on

	while (	bContinual &&
			efuse_OneByteRead(dev, efuse_addr ,&efuse_data) &&
			(efuse_addr  < EFUSE_MAX_SIZE) )
	{
		if(efuse_data!=0xFF)
		{
			hoffset = (efuse_data>>4) & 0x0F;
			hworden =  efuse_data & 0x0F;
			word_cnts = efuse_CalculateWordCnts(hworden);
			//read next header
			efuse_addr = efuse_addr + (word_cnts*2)+1;
		}
		else
		{
			bContinual = FALSE ;
		}
	}

	//efuse_reg_ctrl(pAdapter,FALSE);//power off

	return efuse_addr;

}	// efuse_GetCurrentSize}


/*  11/16/2008 MH Add description. Get current efuse area enabled word!!. */
static u8
efuse_CalculateWordCnts(u8	word_en)
{
	u8 word_cnts = 0;
	if(!(word_en & BIT0))	word_cnts++; // 0 : write enable
	if(!(word_en & BIT1))	word_cnts++;
	if(!(word_en & BIT2))	word_cnts++;
	if(!(word_en & BIT3))	word_cnts++;
	return word_cnts;
}	// efuse_CalculateWordCnts


/*-----------------------------------------------------------------------------
 * Function:	efuse_ResetLoader
 *
 * Overview:	When read Efuse Fail we must reset loader!!!!
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 11/22/2008 	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
#if 0
static void efuse_ResetLoader(struct net_device* dev)
{
	u16	tmpU2b;

	//
	// 2008/11/22 MH Sometimes, we may read efuse fail, for preventing the condition
	// We have to reset loader.
	//
	tmpU2b = read_nic_word(dev, SYS_FUNC_EN);
	write_nic_word(dev, SYS_FUNC_EN, (tmpU2b&~(BIT12)));
	//PlatformStallExecution(10000);	// How long should we delay!!!
	mdelay(10);
	write_nic_word(dev, SYS_FUNC_EN, (tmpU2b|BIT12));
	//PlatformStallExecution(10000);	// How long should we delay!!!
	mdelay(10);

}	// efuse_ResetLoader
#endif

/*-----------------------------------------------------------------------------
 * Function:	EFUSE_ProgramMap
 *
 * Overview:	Read EFUSE map file and execute PG.
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 11/10/2008 	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
 #ifdef TO_DO_LIST
extern	bool	// 0=Shadow 1=Real Efuse
EFUSE_ProgramMap(struct net_device* dev, char* pFileName,u8	TableType)
{
	struct r8192_priv 	*priv = ieee80211_priv(dev);
	s4Byte			nLinesRead, ithLine;
	RT_STATUS		rtStatus = RT_STATUS_SUCCESS;
	char* 			szLine;
	u32			u4bRegValue, u4RegMask;
	u32			u4bMove;
	u16			index = 0;
	u16			i;
	u8			eeprom[HWSET_MAX_SIZE_92S];

	rtStatus = PlatformReadFile(
					dev,
					pFileName,
					(u8*)(priv->BufOfLines),
					MAX_LINES_HWCONFIG_TXT,
					MAX_BYTES_LINE_HWCONFIG_TXT,
					&nLinesRead
					);

	if(rtStatus == RT_STATUS_SUCCESS)
	{
		memcp(pHalData->BufOfLines3, pHalData->BufOfLines,
			nLinesRead*MAX_BYTES_LINE_HWCONFIG_TXT);
		pHalData->nLinesRead3 = nLinesRead;
	}

	if(rtStatus == RT_STATUS_SUCCESS)
	{
		printk("szEepromFile(): read %s ok\n", pFileName);
		for(ithLine = 0; ithLine < nLinesRead; ithLine++)
		{
			szLine = pHalData->BufOfLines[ithLine];
			printk("Line-%d String =%s\n", ithLine, szLine);

			if(!IsCommentString(szLine))
			{
				// EEPROM map one line has 8 words content.
				for (i = 0; i < 8; i++)
				{
					u32	j;

					//GetHexValueFromString(szLine, &u4bRegValue, &u4bMove);
					efuse_ParsingMap(szLine, &u4bRegValue, &u4bMove);

					// Get next hex value as EEPROM value.
					szLine += u4bMove;
					//WriteEEprom(dev, (u16)(ithLine*8+i), (u16)u4bRegValue);
					eeprom[index++] = (u8)(u4bRegValue&0xff);
					eeprom[index++] = (u8)((u4bRegValue>>8)&0xff);

					printk("Addr-%d = %x\n", (ithLine*8+i), u4bRegValue);
				}
			}

		}

	}
	else
	{
		printk("szEepromFile(): Fail read%s\n", pFileName);
		return	RT_STATUS_FAILURE;
	}


	//RT_PRINT_DATA(COMP_EFUSE, DBG_LOUD, ("EFUSE "), eeprom, HWSET_MAX_SIZE_92S);

	// Use map file to update real Efuse or shadow modify table.
	if (TableType == 1)
	{
		efuse_WriteAllMap(dev, eeprom, HWSET_MAX_SIZE_92S);
	}
	else
	{
		// Modify shadow table.
		for (i = 0; i < HWSET_MAX_SIZE_92S; i++)
			EFUSE_ShadowWrite(dev, 1, i, (u32)eeprom[i]);
	}

	return	rtStatus;
}	/* EFUSE_ProgramMap */

#endif

//
//	Description:
//		Return TRUE if chTmp is represent for hex digit and
//		FALSE otherwise.
//
//
bool IsHexDigit(	char chTmp)
{
	if( (chTmp >= '0' && chTmp <= '9') ||
		(chTmp >= 'a' && chTmp <= 'f') ||
		(chTmp >= 'A' && chTmp <= 'F') )
	{
		return TRUE;
	}
	else
	{
		return FALSE;
	}
}

//
//	Description:
//		Translate a character to hex digit.
//
u32 MapCharToHexDigit(char chTmp)
{
	if(chTmp >= '0' && chTmp <= '9')
		return (chTmp - '0');
	else if(chTmp >= 'a' && chTmp <= 'f')
		return (10 + (chTmp - 'a'));
	else if(chTmp >= 'A' && chTmp <= 'F')
		return (10 + (chTmp - 'A'));
	else
		return 0;
}

/*-----------------------------------------------------------------------------
 * Function:	efuse_ParsingMap
 *
 * Overview:
 *
 * Input:       NONE
 *
 * Output:      NONE
 *
 * Return:      NONE
 *
 * Revised History:
 * When			Who		Remark
 * 11/08/2008 	MHC		Create Version 0.
 *
 *---------------------------------------------------------------------------*/
#ifdef TO_DO_LIST
static	bool
efuse_ParsingMap(char* szStr,u32* pu4bVal,u32* pu4bMove)
{
	char* 		szScan = szStr;

	// Check input parameter.
	if(szStr == NULL || pu4bVal == NULL || pu4bMove == NULL)
	{
		//RT_TRACE(COMP_EFUSE,
		//"eeprom_ParsingMap(): Invalid IN args! szStr: %p, pu4bVal: %p, pu4bMove: %p\n",
		//szStr, pu4bVal, pu4bMove);
		return FALSE;
	}

	// Initialize output.
	*pu4bMove = 0;
	*pu4bVal = 0;

	// Skip leading space.
	while(	*szScan != '\0' &&
			(*szScan == ' ' || *szScan == '\t') )
	{
		szScan++;
		(*pu4bMove)++;
	}

	// Check if szScan is now pointer to a character for hex digit,
	// if not, it means this is not a valid hex number.
	if(!IsHexDigit(*szScan))
	{
		return FALSE;
	}

	// Parse each digit.
	do
	{
		(*pu4bVal) <<= 4;
		*pu4bVal += MapCharToHexDigit(*szScan);

		szScan++;
		(*pu4bMove)++;
	} while(IsHexDigit(*szScan));

	return TRUE;

}	/* efuse_ParsingMap */
#endif

//
// Useless Section Code Now!!!!!!
//
// Porting from 8712 SDIO
int efuse_one_byte_rw(struct net_device* dev, u8 bRead, u16 addr, u8 *data)
{
	u32 bResult;
	//u8 efuse_ctlreg,tmpidx = 0;
	u8 tmpidx = 0;
	u8 tmpv8=0;

	// -----------------e-fuse reg ctrl ---------------------------------

	write_nic_byte(dev, EFUSE_CTRL+1, (u8)(addr&0xff));		//address
	tmpv8 = ((u8)((addr>>8) &0x03) ) | (read_nic_byte(dev, EFUSE_CTRL+2)&0xFC );
	write_nic_byte(dev, EFUSE_CTRL+2, tmpv8);

	if(TRUE==bRead){

		write_nic_byte(dev, EFUSE_CTRL+3,  0x72);//read cmd

		while(!(0x80 & read_nic_byte(dev, EFUSE_CTRL+3)) && (tmpidx<100) ){
			tmpidx++;
		}
		if(tmpidx<100){
			*data=read_nic_byte(dev, EFUSE_CTRL);
			bResult = TRUE;
		}
		else
		{
			*data = 0;
			bResult = FALSE;
		}

	}
	else{
		//return	0;
		write_nic_byte(dev, EFUSE_CTRL, *data);//data

		write_nic_byte(dev, EFUSE_CTRL+3, 0xF2);//write cmd

		while((0x80 & read_nic_byte(dev, EFUSE_CTRL+3)) && (tmpidx<100) ){
			tmpidx++;
		}
		if(tmpidx<100)
		{
			*data=read_nic_byte(dev, EFUSE_CTRL);
			bResult = TRUE;
		}
		else
		{
			*data = 0;
			bResult = FALSE;
		}

	}
	return bResult;
}
//------------------------------------------------------------------------------
void efuse_access(struct net_device* dev, u8 bRead,u16 start_addr, u8 cnts, u8 *data)
{
	u8	efuse_clk_ori,efuse_clk_new;//,tmp8;
	u32 i = 0;

	if(start_addr>0x200) return;
	//RT_TRACE(_module_rtl871x_mp_ioctl_c_,_drv_err_,
	//	("\n ===> efuse_access [start_addr=0x%x cnts:%d dataarray:0x%08x  Query Efuse].\n",start_addr,cnts,data));
	// -----------------SYS_FUNC_EN Digital Core Vdd enable ---------------------------------
	efuse_clk_ori = read_nic_byte(dev,SYS_FUNC_EN+1);
	efuse_clk_new = efuse_clk_ori|0x20;

	if(efuse_clk_new!= efuse_clk_ori){
		//RT_TRACE(_module_rtl871x_mp_ioctl_c_,_drv_err_,("====write 0x10250003=====\n"));
		write_nic_byte(dev, SYS_FUNC_EN+1, efuse_clk_new);
	}
#ifdef _POWERON_DELAY_
	mdelay(10);
#endif
	// -----------------e-fuse pwr & clk reg ctrl ---------------------------------
	write_nic_byte(dev, EFUSE_TEST+3, (read_nic_byte(dev, EFUSE_TEST+3)|0x80));
	write_nic_byte(dev, EFUSE_CLK_CTRL, (read_nic_byte(dev, EFUSE_CLK_CTRL)|0x03));

#ifdef _PRE_EXECUTE_READ_CMD_
	{
		unsigned char tmpdata;
		efuse_OneByteRead(dev, 0,&tmpdata);
	}
#endif

	//-----------------e-fuse one byte read / write ------------------------------
	for(i=0;i<cnts;i++){
		efuse_one_byte_rw(dev,bRead, start_addr+i , data+i);
		////RT_TRACE(_module_rtl871x_mp_ioctl_c_,_drv_err_,("==>efuse_access addr:0x%02x value:0x%02x\n",data+i,*(data+i)));
	}
	// -----------------e-fuse pwr & clk reg ctrl ---------------------------------
	write_nic_byte(dev, EFUSE_TEST+3, read_nic_byte(dev, EFUSE_TEST+3)&0x7f);
	write_nic_byte(dev, EFUSE_CLK_CTRL, read_nic_byte(dev, EFUSE_CLK_CTRL)&0xfd);

	// -----------------SYS_FUNC_EN Digital Core Vdd disable ---------------------------------
	if(efuse_clk_new != efuse_clk_ori)	write_nic_byte(dev, 0x10250003, efuse_clk_ori);

}
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------

#ifdef TO_DO_LIST
static	void efuse_reg_ctrl(struct net_device* dev, u8 bPowerOn)
{
	if(TRUE==bPowerOn){
		// -----------------SYS_FUNC_EN Digital Core Vdd enable ---------------------------------
		write_nic_byte(dev, SYS_FUNC_EN+1,  read_nic_byte(dev,SYS_FUNC_EN+1)|0x20);
#ifdef _POWERON_DELAY_
		mdelay(10);
#endif
		// -----------------e-fuse pwr & clk reg ctrl ---------------------------------
		write_nic_byte(dev, EFUSE_TEST+4, (read_nic_byte(dev, EFUSE_TEST+4)|0x80));
		write_nic_byte(dev, EFUSE_CLK_CTRL, (read_nic_byte(dev, EFUSE_CLK_CTRL)|0x03));
#ifdef _PRE_EXECUTE_READ_CMD_
		{
			unsigned char tmpdata;
			efuse_OneByteRead(dev, 0,&tmpdata);
		}

#endif
	}
	else{
		// -----------------e-fuse pwr & clk reg ctrl ---------------------------------
		write_nic_byte(dev, EFUSE_TEST+4, read_nic_byte(dev, EFUSE_TEST+4)&0x7f);
		write_nic_byte(dev, EFUSE_CLK_CTRL, read_nic_byte(dev, EFUSE_CLK_CTRL)&0xfd);
		// -----------------SYS_FUNC_EN Digital Core Vdd disable ---------------------------------

		//write_nic_byte(pAdapter, SYS_FUNC_EN+1,  read_nic_byte(pAdapter,SYS_FUNC_EN+1)&0xDF);
	}


}
#endif
//------------------------------------------------------------------------------

//------------------------------------------------------------------------------
void efuse_read_data(struct net_device* dev,u8 efuse_read_item,u8 *data,u32 data_size)
{
	u8 offset, word_start,byte_start,byte_cnts;
	u8	efusedata[EFUSE_MAC_LEN];
	u8 *tmpdata = NULL;

	u8 pg_pkt_cnts ;

	u8 tmpidx;
	u8 pg_data[8];
	//u8	temp_value[8] = {0xff};

	if(efuse_read_item>  (sizeof(RTL8712_SDIO_EFUSE_TABLE)/sizeof(EFUSE_MAP))){
		//error msg
		return ;
	}

	offset		= RTL8712_SDIO_EFUSE_TABLE[efuse_read_item].offset ;
	word_start	= RTL8712_SDIO_EFUSE_TABLE[efuse_read_item].word_start;
	byte_start 	= RTL8712_SDIO_EFUSE_TABLE[efuse_read_item].byte_start;
	byte_cnts   	= RTL8712_SDIO_EFUSE_TABLE[efuse_read_item].byte_cnts;

	if(data_size!=byte_cnts){
		//error msg
		return;
	}

	pg_pkt_cnts = (byte_cnts /PGPKT_DATA_SIZE) +1;

	if(pg_pkt_cnts > 1){
		//tmpdata = _malloc(pg_pkt_cnts*PGPKT_DATA_SIZE);
		tmpdata = efusedata;

		if(tmpdata!=NULL)
		{
			memset(tmpdata,0xff,pg_pkt_cnts*PGPKT_DATA_SIZE);
			//PlatformFillMemory((PVOID)pg_data, pg_pkt_cnts*PGPKT_DATA_SIZE, 0xff);

			for(tmpidx=0;tmpidx<pg_pkt_cnts;tmpidx++)
			{
				memset(pg_data,0xff,PGPKT_DATA_SIZE);
				//PlatformFillMemory((PVOID)pg_data, PGPKT_DATA_SIZE, 0xff);
				if(TRUE== efuse_PgPacketRead(dev,offset+tmpidx,pg_data))
				{
					memcpy(tmpdata+(PGPKT_DATA_SIZE*tmpidx),pg_data,PGPKT_DATA_SIZE);
					//PlatformMoveMemory((PVOID)(tmpdata+(PGPKT_DATA_SIZE*tmpidx)), (PVOID)pg_data, PGPKT_DATA_SIZE);
				}
			}
			memcpy(data,(tmpdata+ (2*word_start)+byte_start ),data_size);
			//PlatformMoveMemory((PVOID)data, (PVOID)(tmpdata+ (2*word_start)+byte_start ), data_size);
			//_mfree(tmpdata, pg_pkt_cnts*PGPKT_DATA_SIZE);
		}
	}
	else
	{
		memset(pg_data,0xff,PGPKT_DATA_SIZE);
		//PlatformFillMemory((PVOID)pg_data, PGPKT_DATA_SIZE, 0xff);
		if(TRUE==efuse_PgPacketRead(dev,offset,pg_data)){
			memcpy(data,pg_data+ (2*word_start)+byte_start ,data_size);
			//PlatformMoveMemory((PVOID)data, (PVOID)(pg_data+ (2*word_start)+byte_start), data_size);
		}
	}

}
//------------------------------------------------------------------------------
//per interface doesn't alike
void efuse_write_data(struct net_device* dev,u8 efuse_write_item,u8 *data,u32 data_size,u32 bWordUnit)
{
	u8 offset, word_start,byte_start,byte_cnts;
	u8 word_en = 0x0f,word_cnts;
	u8 pg_pkt_cnts ;

	u8 tmpidx,tmpbitmask;
	u8 pg_data[8],tmpbytes=0;

	if(efuse_write_item>  (sizeof(RTL8712_SDIO_EFUSE_TABLE)/sizeof(EFUSE_MAP))){
		//error msg
		return ;
	}

	offset		= RTL8712_SDIO_EFUSE_TABLE[efuse_write_item].offset ;
	word_start	= RTL8712_SDIO_EFUSE_TABLE[efuse_write_item].word_start;
	byte_start 	= RTL8712_SDIO_EFUSE_TABLE[efuse_write_item].byte_start;
	byte_cnts   	= RTL8712_SDIO_EFUSE_TABLE[efuse_write_item].byte_cnts;

	if(data_size >  byte_cnts){
		//error msg
		return;
	}
	pg_pkt_cnts = (byte_cnts /PGPKT_DATA_SIZE) +1;
	word_cnts = byte_cnts /2 ;

	if(byte_cnts %2){
		word_cnts+=1;
	}
	if((byte_start==1)||((byte_cnts%2)==1)){//situation A

		if((efuse_write_item==EFUSE_F0CIS)||(efuse_write_item==EFUSE_F1CIS)){
			memset(pg_data,0xff,PGPKT_DATA_SIZE);
			//PlatformFillMemory((PVOID)pg_data, PGPKT_DATA_SIZE, 0xff);
			efuse_PgPacketRead(dev,offset,pg_data);

			if(efuse_write_item==EFUSE_F0CIS){
				word_en = 0x07;
				memcpy(pg_data+word_start*2+byte_start,data,sizeof(u8)*2);
				//PlatformMoveMemory((PVOID)(pg_data+word_start*2+byte_start), (PVOID)data, sizeof(u8)*2);
				efuse_PgPacketWrite(dev,offset,word_en,pg_data+(word_start*2));

				word_en = 0x00;
				efuse_PgPacketWrite(dev,(offset+1),word_en,data+2);

				word_en = 0x00;
				efuse_PgPacketRead(dev,offset+2,pg_data);
				memcpy(pg_data,data+2+8,sizeof(u8)*7);
				//PlatformMoveMemory((PVOID)(pg_data), (PVOID)(data+2+8), sizeof(u8)*7);

				efuse_PgPacketWrite(dev,(offset+2),word_en,pg_data);
			}
			else if(efuse_write_item==EFUSE_F1CIS){
				word_en = 0x07;
				efuse_PgPacketRead(dev,offset,pg_data);
				pg_data[7] = data[0];
				efuse_PgPacketWrite(dev,offset,word_en,pg_data+(word_start*2));

				word_en = 0x00;
				for(tmpidx = 0 ;tmpidx<(word_cnts/4);tmpidx++){
					efuse_PgPacketWrite(dev,(offset+1+tmpidx),word_en,data+1+(tmpidx*PGPKT_DATA_SIZE));
				}
			}

		}
		else{
			memset(pg_data,0xff,PGPKT_DATA_SIZE);
			//PlatformFillMemory((PVOID)pg_data, PGPKT_DATA_SIZE, 0xff);
			if((efuse_write_item==EFUSE_SDIO_SETTING)||(efuse_write_item==EFUSE_CCCR)){
				word_en = 0x0e ;
				tmpbytes = 2;
			}
			else if(efuse_write_item == EFUSE_SDIO_MODE){
				word_en = 0x0d ;
				tmpbytes = 2;
			}
			else if(efuse_write_item == EFUSE_OCR){
				word_en = 0x09 ;
				tmpbytes = 4;
			}
			else if((efuse_write_item == EFUSE_EEPROM_VER)||(efuse_write_item==EFUSE_CHAN_PLAN)){
				word_en = 0x07 ;
				tmpbytes = 2;
			}
			if(bWordUnit==TRUE){
				memcpy(pg_data+word_start*2 ,data,sizeof(u8)*tmpbytes);
				//PlatformMoveMemory((PVOID)(pg_data+word_start*2), (PVOID)(data), sizeof(u8)*tmpbytes);
			}
			else{
				efuse_PgPacketRead(dev,offset,pg_data);
				memcpy(pg_data+(2*word_start)+byte_start,data,sizeof(u8)*byte_cnts);
				//PlatformMoveMemory((PVOID)(pg_data+(2*word_start)+byte_start), (PVOID)(data), sizeof(u8)*byte_cnts);
			}

			efuse_PgPacketWrite(dev,offset,word_en,pg_data+(word_start*2));

		}

	}
	//========================================================================
	else if(pg_pkt_cnts>1){//situation B
		if(word_start==0){
			word_en = 0x00;
			for(tmpidx = 0 ;tmpidx<(word_cnts/4);tmpidx++)
			{
				efuse_PgPacketWrite(dev,(offset+tmpidx),word_en,data+(tmpidx*PGPKT_DATA_SIZE));
			}
			word_en = 0x0f;
			for(tmpidx= 0; tmpidx<(word_cnts%4) ; tmpidx++)
			{
				tmpbitmask =tmpidx;
				word_en &= (~(EFUSE_BIT(tmpbitmask)));
				//BIT0
			}
			efuse_PgPacketWrite(dev,offset+(word_cnts/4),word_en,data+((word_cnts/4)*PGPKT_DATA_SIZE));
		}else
		{

		}
	}
	//========================================================================
	else{//situation C
		word_en = 0x0f;
		for(tmpidx= 0; tmpidx<word_cnts ; tmpidx++)
		{
			tmpbitmask = word_start + tmpidx ;
			word_en &= (~(EFUSE_BIT(tmpbitmask)));
		}
		efuse_PgPacketWrite(dev,offset,word_en,data);
	}

}
//------------------------------------------------------------------------------

void efuset_test_func_read(struct net_device* dev)
{
	u8 chipid[2];
	u8 ocr[3];
	u8 macaddr[6];
	u8 txpowertable[28];

	memset(chipid,0,sizeof(u8)*2);
	efuse_read_data(dev,EFUSE_CHIP_ID,chipid,sizeof(chipid));

	memset(ocr,0,sizeof(u8)*3);
	efuse_read_data(dev,EFUSE_CCCR,ocr,sizeof(ocr));

	memset(macaddr,0,sizeof(u8)*6);
	efuse_read_data(dev,EFUSE_MAC_ADDR,macaddr,sizeof(macaddr));

	memset(txpowertable,0,sizeof(u8)*28);
	efuse_read_data(dev,EFUSE_TXPW_TAB,txpowertable,sizeof(txpowertable));
}
//------------------------------------------------------------------------------

void efuset_test_func_write(struct net_device* dev)
{
	u32 bWordUnit = TRUE;
	u8 CCCR=0x02,SDIO_SETTING = 0xFF;
	u8 tmpdata[2];

	u8 macaddr[6] = {0x00,0xe0,0x4c,0x87,0x12,0x66};
	efuse_write_data(dev,EFUSE_MAC_ADDR,macaddr,sizeof(macaddr),bWordUnit);

	bWordUnit = FALSE;
	efuse_write_data(dev,EFUSE_CCCR,&CCCR,sizeof(u8),bWordUnit);

	bWordUnit = FALSE;
	efuse_write_data(dev,EFUSE_SDIO_SETTING,&SDIO_SETTING,sizeof(u8),bWordUnit);

	bWordUnit = TRUE;
	tmpdata[0] =SDIO_SETTING ;
	tmpdata[1] =CCCR ;
	efuse_write_data(dev,EFUSE_SDIO_SETTING,tmpdata,sizeof(tmpdata),bWordUnit);

}
//------------------------------------------------------------------------------




#endif	// #if (HAL_CODE_BASE == RTL8192_S)






/* End of Efuse.c */




