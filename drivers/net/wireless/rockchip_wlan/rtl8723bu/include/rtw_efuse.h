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
#ifndef __RTW_EFUSE_H__
#define __RTW_EFUSE_H__


#define	EFUSE_ERROE_HANDLE		1

#define	PG_STATE_HEADER 		0x01
#define	PG_STATE_WORD_0		0x02
#define	PG_STATE_WORD_1		0x04
#define	PG_STATE_WORD_2		0x08
#define	PG_STATE_WORD_3		0x10
#define	PG_STATE_DATA			0x20

#define	PG_SWBYTE_H			0x01
#define	PG_SWBYTE_L			0x02

#define	PGPKT_DATA_SIZE		8

#define	EFUSE_WIFI				0
#define	EFUSE_BT				1

enum _EFUSE_DEF_TYPE {
	TYPE_EFUSE_MAX_SECTION				= 0,
	TYPE_EFUSE_REAL_CONTENT_LEN			= 1,
	TYPE_AVAILABLE_EFUSE_BYTES_BANK		= 2,
	TYPE_AVAILABLE_EFUSE_BYTES_TOTAL	= 3,
	TYPE_EFUSE_MAP_LEN					= 4,
	TYPE_EFUSE_PROTECT_BYTES_BANK		= 5,
	TYPE_EFUSE_CONTENT_LEN_BANK			= 6,
};

#define		EFUSE_MAX_MAP_LEN		512

#define		EFUSE_MAX_HW_SIZE		512
#define		EFUSE_MAX_SECTION_BASE	16

#define EXT_HEADER(header) ((header & 0x1F ) == 0x0F)
#define ALL_WORDS_DISABLED(wde)	((wde & 0x0F) == 0x0F)
#define GET_HDR_OFFSET_2_0(header) ( (header & 0xE0) >> 5)

#define		EFUSE_REPEAT_THRESHOLD_			3

#define IS_MASKED_MP(ic, txt, offset) (EFUSE_IsAddressMasked_MP_##ic##txt(offset))
#define IS_MASKED_TC(ic, txt, offset) (EFUSE_IsAddressMasked_TC_##ic##txt(offset))
#define GET_MASK_ARRAY_LEN_MP(ic, txt) (EFUSE_GetArrayLen_MP_##ic##txt())
#define GET_MASK_ARRAY_LEN_TC(ic, txt) (EFUSE_GetArrayLen_TC_##ic##txt())
#define GET_MASK_ARRAY_MP(ic, txt, offset) (EFUSE_GetMaskArray_MP_##ic##txt(offset))
#define GET_MASK_ARRAY_TC(ic, txt, offset) (EFUSE_GetMaskArray_TC_##ic##txt(offset))


#define IS_MASKED(ic, txt, offset) ( IS_MASKED_MP(ic,txt, offset) )
#define GET_MASK_ARRAY_LEN(ic, txt) ( GET_MASK_ARRAY_LEN_MP(ic,txt) )
#define GET_MASK_ARRAY(ic, txt, out) do { GET_MASK_ARRAY_MP(ic,txt, out);} while(0)

//=============================================
//	The following is for BT Efuse definition
//=============================================
#define		EFUSE_BT_MAX_MAP_LEN		1024
#define		EFUSE_MAX_BANK			4
#define		EFUSE_MAX_BT_BANK		(EFUSE_MAX_BANK-1)
//=============================================
/*--------------------------Define Parameters-------------------------------*/
#define		EFUSE_MAX_WORD_UNIT			4

/*------------------------------Define structure----------------------------*/ 
typedef struct PG_PKT_STRUCT_A{
	u8 offset;
	u8 word_en;
	u8 data[8];	
	u8 word_cnts;
}PGPKT_STRUCT,*PPGPKT_STRUCT;

typedef enum
{
	ERR_SUCCESS = 0,
	ERR_DRIVER_FAILURE,
	ERR_IO_FAILURE,
	ERR_WI_TIMEOUT,
	ERR_WI_BUSY,
	ERR_BAD_FORMAT,
	ERR_INVALID_DATA,
	ERR_NOT_ENOUGH_SPACE,
	ERR_WRITE_PROTECT,
	ERR_READ_BACK_FAIL,
	ERR_OUT_OF_RANGE
} ERROR_CODE;

/*------------------------------Define structure----------------------------*/ 
typedef struct _EFUSE_HAL{
	u8	fakeEfuseBank;
	u32	fakeEfuseUsedBytes;
	u8	fakeEfuseContent[EFUSE_MAX_HW_SIZE];
	u8	fakeEfuseInitMap[EFUSE_MAX_MAP_LEN];
	u8	fakeEfuseModifiedMap[EFUSE_MAX_MAP_LEN];
	u32	EfuseUsedBytes;
	u8	EfuseUsedPercentage;
	
	u16	BTEfuseUsedBytes;
	u8	BTEfuseUsedPercentage;
	u8	BTEfuseContent[EFUSE_MAX_BT_BANK][EFUSE_MAX_HW_SIZE];
	u8	BTEfuseInitMap[EFUSE_BT_MAX_MAP_LEN];
	u8	BTEfuseModifiedMap[EFUSE_BT_MAX_MAP_LEN];

	u16	fakeBTEfuseUsedBytes;
	u8	fakeBTEfuseContent[EFUSE_MAX_BT_BANK][EFUSE_MAX_HW_SIZE];
	u8	fakeBTEfuseInitMap[EFUSE_BT_MAX_MAP_LEN];
	u8	fakeBTEfuseModifiedMap[EFUSE_BT_MAX_MAP_LEN];

	// EFUSE Configuration, initialized in HAL_CmnInitPGData().
	const u16  MaxSecNum_WiFi;
	const u16  MaxSecNum_BT;	
	const u16  WordUnit;	
	const u16  PhysicalLen_WiFi;
	const u16  PhysicalLen_BT;	
	const u16  LogicalLen_WiFi;
	const u16  LogicalLen_BT;	
	const u16  BankSize;
	const u16  TotalBankNum;
	const u16  BankNum_WiFi;
	const u16  BankNum_BT;	
	const u16  OOBProtectBytes;
	const u16  ProtectBytes;
	const u16  BankAvailBytes;	
	const u16  TotalAvailBytes_WiFi;
	const u16  TotalAvailBytes_BT;	
	const u16  HeaderRetry;	
	const u16  DataRetry;	

	ERROR_CODE 	  Status;
	
}EFUSE_HAL, *PEFUSE_HAL;

extern u8 maskfileBuffer[32];

/*------------------------Export global variable----------------------------*/
extern u8 fakeEfuseBank;
extern u32 fakeEfuseUsedBytes;
extern u8 fakeEfuseContent[];
extern u8 fakeEfuseInitMap[];
extern u8 fakeEfuseModifiedMap[];

extern u32 BTEfuseUsedBytes;
extern u8 BTEfuseContent[EFUSE_MAX_BT_BANK][EFUSE_MAX_HW_SIZE];
extern u8 BTEfuseInitMap[];
extern u8 BTEfuseModifiedMap[];

extern u32 fakeBTEfuseUsedBytes;
extern u8 fakeBTEfuseContent[EFUSE_MAX_BT_BANK][EFUSE_MAX_HW_SIZE];
extern u8 fakeBTEfuseInitMap[];
extern u8 fakeBTEfuseModifiedMap[];
/*------------------------Export global variable----------------------------*/

u8	efuse_GetCurrentSize(PADAPTER padapter, u16 *size);
u16	efuse_GetMaxSize(PADAPTER padapter);
u8	rtw_efuse_access(PADAPTER padapter, u8 bRead, u16 start_addr, u16 cnts, u8 *data);
u8	rtw_efuse_mask_map_read(PADAPTER padapter, u16 addr, u16 cnts, u8 *data);
u8	rtw_efuse_map_read(PADAPTER padapter, u16 addr, u16 cnts, u8 *data);
u8	rtw_efuse_map_write(PADAPTER padapter, u16 addr, u16 cnts, u8 *data);
u8	rtw_BT_efuse_map_read(PADAPTER padapter, u16 addr, u16 cnts, u8 *data);
u8 	rtw_BT_efuse_map_write(PADAPTER padapter, u16 addr, u16 cnts, u8 *data);

u16	Efuse_GetCurrentSize(PADAPTER pAdapter, u8 efuseType, BOOLEAN bPseudoTest);
u8	Efuse_CalculateWordCnts(u8 word_en);
void	ReadEFuseByte(PADAPTER Adapter, u16 _offset, u8 *pbuf, BOOLEAN bPseudoTest) ;
void	EFUSE_GetEfuseDefinition(PADAPTER pAdapter, u8 efuseType, u8 type, void *pOut, BOOLEAN bPseudoTest);
u8	efuse_OneByteRead(PADAPTER pAdapter, u16 addr, u8 *data, BOOLEAN	 bPseudoTest);
u8	efuse_OneByteWrite(PADAPTER pAdapter, u16 addr, u8 data, BOOLEAN	 bPseudoTest);

void	BTEfuse_PowerSwitch(PADAPTER pAdapter,u8	bWrite,u8	 PwrState);
void	Efuse_PowerSwitch(PADAPTER pAdapter,u8	bWrite,u8	 PwrState);
int 	Efuse_PgPacketRead(PADAPTER pAdapter, u8 offset, u8 *data, BOOLEAN bPseudoTest);
int 	Efuse_PgPacketWrite(PADAPTER pAdapter, u8 offset, u8 word_en, u8 *data, BOOLEAN bPseudoTest);
void	efuse_WordEnableDataRead(u8 word_en, u8 *sourdata, u8 *targetdata);
u8	Efuse_WordEnableDataWrite(PADAPTER pAdapter, u16 efuse_addr, u8 word_en, u8 *data, BOOLEAN bPseudoTest);

u8	EFUSE_Read1Byte(PADAPTER pAdapter, u16 Address);
void	EFUSE_ShadowMapUpdate(PADAPTER pAdapter, u8 efuseType, BOOLEAN bPseudoTest);
void	EFUSE_ShadowRead(PADAPTER pAdapter, u8 Type, u16 Offset, u32 *Value);
void Rtw_Hal_ReadMACAddrFromFile(PADAPTER padapter);
u32 Rtw_Hal_readPGDataFromConfigFile(PADAPTER	padapter);
u8 rtw_efuse_file_read(PADAPTER padapter,u8 *filepatch,u8 *buf, u32 len);

#endif

