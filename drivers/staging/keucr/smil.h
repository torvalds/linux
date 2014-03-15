/*----- < smil.h> ----------------------------------------------------*/
#ifndef SMIL_INCD
#define SMIL_INCD

/***************************************************************************
Define Definition
***************************************************************************/
#define K_BYTE              1024   /* Kilo Byte */
#define SECTSIZE            512    /* Sector buffer size */
#define REDTSIZE            16     /* Redundant buffer size */

/***************************************************************************/
#define DUMMY_DATA          0xFF   /* No Assign Sector Read Data */

/***************************************************************************
Max Zone/Block/Sectors Data Definition
***************************************************************************/
#define MAX_ZONENUM         128    /* Max Zone Numbers in a SmartMedia    */
#define MAX_BLOCKNUM        0x0400 /* Max Block Numbers in a Zone         */
#define MAX_SECTNUM         0x20   /* Max Sector Numbers in a Block       */
#define MAX_LOGBLOCK        1000   /* Max Logical Block Numbers in a Zone */

/***************************************************************************/
#define CIS_SEARCH_SECT     0x08   /* Max CIS Search Sector Number */

/***************************************************************************
Logical to Physical Block Table Data Definition
***************************************************************************/
#define NO_ASSIGN           0xFFFF /* No Assign Logical Block Address */

/***************************************************************************
'SectCopyMode' Data
***************************************************************************/
#define COMPLETED           0      /* Sector Copy Completed */
#define REQ_ERASE           1      /* Request Read Block Erase */
#define REQ_FAIL            2      /* Request Read Block Failed */

/***************************************************************************
Retry Counter Definition
***************************************************************************/
#define RDERR_REASSIGN      1      /* Reassign with Read Error */
#define L2P_ERR_ERASE       1      /* BlockErase for Contradicted L2P Table */

/***************************************************************************
Hardware ECC Definition
***************************************************************************/
#define HW_ECC_SUPPORTED    1	   /* Hardware ECC Supported */
/* No definition for Software ECC */

/***************************************************************************
SmartMedia Command & Status Definition
***************************************************************************/
/* SmartMedia Command */
#define WRDATA        0x80
/* #define READ          0x00 */
#define READ_REDT     0x50
/* #define WRITE         0x10 */
#define RDSTATUS      0x70

#define READ1         0x00 /* NO */
#define READ2         0x01 /* NO */
#define READ3         0x50 /* NO */
#define RST_CHIP      0xFF
#define ERASE1        0x60
#define ERASE2        0xD0
#define READ_ID_1     0x90
#define READ_ID_2     0x91
#define READ_ID_3     0x9A

/* 712 SmartMedia Command */
#define SM_CMD_RESET                0x00    /* 0xFF */
#define SM_CMD_READ_ID_1            0x10    /* 0x90 */
#define SM_CMD_READ_ID_2            0x20    /* 0x91 */
#define SM_CMD_READ_STAT            0x30    /* 0x70 */
#define SM_CMD_RDMULTPL_STAT        0x40    /* 0x71 */
#define SM_CMD_READ_1               0x50    /* 0x00 */
#define SM_CMD_READ_2               0x60    /* 0x01 */
#define SM_CMD_READ_3               0x70    /* 0x50 */
#define SM_CMD_PAGPRGM_TRUE         0x80    /* {0x80, 0x10} */
#define SM_CMD_PAGPRGM_DUMY         0x90    /* {0x80, 0x11} */
#define SM_CMD_PAGPRGM_MBLK         0xA0    /* {0x80, 0x15} */
#define SM_CMD_BLKERASE             0xB0    /* {0x60, 0xD0} */
#define SM_CMD_BLKERASE_MULTPL      0xC0    /* {0x60-0x60, 0xD0} */

#define SM_CRADDTCT_DEBNCETIMER_EN  0x02
#define SM_CMD_START_BIT            0x01

#define SM_WaitCmdDone { while (!SM_CmdDone); }
#define SM_WaitDmaDone { while (!SM_DmaDone); }

/* SmartMedia Status */
#define WR_FAIL       0x01      /* 0:Pass, 1:Fail */
#define SUSPENDED     0x20      /* 0:Not Suspended, 1:Suspended */
#define READY         0x40      /* 0:Busy, 1:Ready */
#define WR_PRTCT      0x80      /* 0:Protect, 1:Not Protect */

/* SmartMedia Busy Time (1bit:0.1ms) */
#define BUSY_PROG 200 /* tPROG   : 20ms  ----- Program Time old : 200 */
#define BUSY_ERASE 4000 /* tBERASE : 400ms ----- Block Erase Time old : 4000 */

/*for 712 Test */
/* #define BUSY_READ 1 *//* tR : 100us ----- Data transfer Time   old : 1 */
/* #define BUSY_READ 10 *//* tR : 100us ----- Data transfer Time   old : 1 */

#define BUSY_READ 200 /* tR : 20ms   ----- Data transfer Time   old : 1 */

/* #define BUSY_RESET 60 *//* tRST : 6ms ----- Device Resetting Time old : 60 */

#define BUSY_RESET 600 /* tRST : 60ms   ----- Device Resetting Time old : 60 */

/* Hardware Timer (1bit:0.1ms) */
#define TIME_PON      3000      /* 300ms ------ Power On Wait Time */
#define TIME_CDCHK    200       /* 20ms  ------ Card Check Interval Timer */
#define TIME_WPCHK    50        /* 5ms   ------ WP Check Interval Timer */
#define TIME_5VCHK    10        /* 1ms   ------ 5V Check Interval Timer */

/***************************************************************************
Redundant Data
***************************************************************************/
#define REDT_DATA     0x04
#define REDT_BLOCK    0x05
#define REDT_ADDR1H   0x06
#define REDT_ADDR1L   0x07
#define REDT_ADDR2H   0x0B
#define REDT_ADDR2L   0x0C
#define REDT_ECC10    0x0D
#define REDT_ECC11    0x0E
#define REDT_ECC12    0x0F
#define REDT_ECC20    0x08
#define REDT_ECC21    0x09
#define REDT_ECC22    0x0A

/***************************************************************************
SmartMedia Model & Attribute
***************************************************************************/
/* SmartMedia Attribute */
#define NOWP          0x00 /* 0... .... No Write Protect */
#define WP            0x80 /* 1... .... Write Protected */
#define MASK          0x00 /* .00. .... NAND MASK ROM Model */
#define FLASH         0x20 /* .01. .... NAND Flash ROM Model */
#define AD3CYC        0x00 /* ...0 .... Address 3-cycle */
#define AD4CYC        0x10 /* ...1 .... Address 4-cycle */
#define BS16          0x00 /* .... 00.. 16page/block */
#define BS32          0x04 /* .... 01.. 32page/block */
#define PS256         0x00 /* .... ..00 256byte/page */
#define PS512         0x01 /* .... ..01 512byte/page */
#define MWP           0x80 /* WriteProtect mask */
#define MFLASH        0x60 /* Flash Rom mask */
#define MADC          0x10 /* Address Cycle */
#define MBS           0x0C /* BlockSize mask */
#define MPS           0x03 /* PageSize mask */

/* SmartMedia Model */
#define NOSSFDC       0x00 /* NO   SmartMedia */
#define SSFDC1MB      0x01 /* 1MB  SmartMedia */
#define SSFDC2MB      0x02 /* 2MB  SmartMedia */
#define SSFDC4MB      0x03 /* 4MB  SmartMedia */
#define SSFDC8MB      0x04 /* 8MB  SmartMedia */
#define SSFDC16MB     0x05 /* 16MB SmartMedia */
#define SSFDC32MB     0x06 /* 32MB SmartMedia */
#define SSFDC64MB     0x07 /* 64MB SmartMedia */
#define SSFDC128MB    0x08 /*128MB SmartMedia */
#define SSFDC256MB    0x09
#define SSFDC512MB    0x0A
#define SSFDC1GB      0x0B
#define SSFDC2GB      0x0C

/***************************************************************************
Struct Definition
***************************************************************************/
struct keucr_media_info {
	u8 Model;
	u8 Attribute;
	u8 MaxZones;
	u8 MaxSectors;
	u16 MaxBlocks;
	u16 MaxLogBlocks;
};

struct keucr_media_address {
	u8 Zone;	/* Zone Number */
	u8 Sector;	/* Sector(512byte) Number on Block */
	u16 PhyBlock;	/* Physical Block Number on Zone */
	u16 LogBlock;	/* Logical Block Number of Zone */
};

struct keucr_media_area {
	u8 Sector;	/* Sector(512byte) Number on Block */
	u16 PhyBlock;	/* Physical Block Number on Zone 0 */
};

extern u16	ReadBlock;
extern u16	WriteBlock;
extern u32	MediaChange;

extern struct keucr_media_info    Ssfdc;
extern struct keucr_media_address Media;
extern struct keucr_media_area    CisArea;

/*
 * SMILMain.c
 */
/******************************************/
int         Init_D_SmartMedia(void);
int         Pwoff_D_SmartMedia(void);
int         Check_D_SmartMedia(void);
int         Check_D_Parameter(struct us_data *, u16 *, u8 *, u8 *);
int         Media_D_ReadSector(struct us_data *, u32, u16, u8 *);
int         Media_D_WriteSector(struct us_data *, u32, u16, u8 *);
int         Media_D_CopySector(struct us_data *, u32, u16, u8 *);
int         Media_D_EraseBlock(struct us_data *, u32, u16);
int         Media_D_EraseAll(struct us_data *);
/******************************************/
int         Media_D_OneSectWriteStart(struct us_data *, u32, u8 *);
int         Media_D_OneSectWriteNext(struct us_data *, u8 *);
int         Media_D_OneSectWriteFlush(struct us_data *);

/******************************************/
extern int	SM_FreeMem(void);	/* ENE SM function */
void        SM_EnableLED(struct us_data *, bool);
void        Led_D_TernOn(void);
void        Led_D_TernOff(void);

int         Media_D_EraseAllRedtData(u32 Index, bool CheckBlock);
/*DWORD Media_D_GetMediaInfo(struct us_data * fdoExt,
	PIOCTL_MEDIA_INFO_IN pParamIn, PIOCTL_MEDIA_INFO_OUT pParamOut); */

/*
 * SMILSub.c
 */
/******************************************/
int  Check_D_DataBlank(u8 *);
int  Check_D_FailBlock(u8 *);
int  Check_D_DataStatus(u8 *);
int  Load_D_LogBlockAddr(u8 *);
void Clr_D_RedundantData(u8 *);
void Set_D_LogBlockAddr(u8 *);
void Set_D_FailBlock(u8 *);
void Set_D_DataStaus(u8 *);

/******************************************/
void Ssfdc_D_Reset(struct us_data *);
int  Ssfdc_D_ReadCisSect(struct us_data *, u8 *, u8 *);
void Ssfdc_D_WriteRedtMode(void);
void Ssfdc_D_ReadID(u8 *, u8);
int  Ssfdc_D_ReadSect(struct us_data *, u8 *, u8 *);
int  Ssfdc_D_ReadBlock(struct us_data *, u16, u8 *, u8 *);
int  Ssfdc_D_WriteSect(struct us_data *, u8 *, u8 *);
int  Ssfdc_D_WriteBlock(struct us_data *, u16, u8 *, u8 *);
int  Ssfdc_D_CopyBlock(struct us_data *, u16, u8 *, u8 *);
int  Ssfdc_D_WriteSectForCopy(struct us_data *, u8 *, u8 *);
int  Ssfdc_D_EraseBlock(struct us_data *);
int  Ssfdc_D_ReadRedtData(struct us_data *, u8 *);
int  Ssfdc_D_WriteRedtData(struct us_data *, u8 *);
int  Ssfdc_D_CheckStatus(void);
int  Set_D_SsfdcModel(u8);
void Cnt_D_Reset(void);
int  Cnt_D_PowerOn(void);
void Cnt_D_PowerOff(void);
void Cnt_D_LedOn(void);
void Cnt_D_LedOff(void);
int  Check_D_CntPower(void);
int  Check_D_CardExist(void);
int  Check_D_CardStsChg(void);
int  Check_D_SsfdcWP(void);
int  SM_ReadBlock(struct us_data *, u8 *, u8 *);

int  Ssfdc_D_ReadSect_DMA(struct us_data *, u8 *, u8 *);
int  Ssfdc_D_ReadSect_PIO(struct us_data *, u8 *, u8 *);
int  Ssfdc_D_WriteSect_DMA(struct us_data *, u8 *, u8 *);
int  Ssfdc_D_WriteSect_PIO(struct us_data *, u8 *, u8 *);

/******************************************/
int  Check_D_ReadError(u8 *);
int  Check_D_Correct(u8 *, u8 *);
int  Check_D_CISdata(u8 *, u8 *);
void Set_D_RightECC(u8 *);

/*
 * SMILECC.c
 */
void calculate_ecc(u8 *, u8 *, u8 *, u8 *, u8 *);
u8 correct_data(u8 *, u8 *, u8,   u8,   u8);
int  _Correct_D_SwECC(u8 *, u8 *, u8 *);
void _Calculate_D_SwECC(u8 *, u8 *);

void SM_Init(void);

#endif /* already included */
