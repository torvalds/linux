#include <linux/slab.h>
#include "usb.h"
#include "scsiglue.h"
#include "transport.h"

#include "smcommon.h"
#include "smil.h"

void   _Set_D_SsfdcRdCmd(BYTE);
void   _Set_D_SsfdcRdAddr(BYTE);
void   _Set_D_SsfdcRdChip(void);
void   _Set_D_SsfdcRdStandby(void);
void   _Start_D_SsfdcRdHwECC(void);
void   _Stop_D_SsfdcRdHwECC(void);
void   _Load_D_SsfdcRdHwECC(BYTE);
void   _Set_D_SsfdcWrCmd(BYTE);
void   _Set_D_SsfdcWrAddr(BYTE);
void   _Set_D_SsfdcWrBlock(void);
void   _Set_D_SsfdcWrStandby(void);
void   _Start_D_SsfdcWrHwECC(void);
void   _Load_D_SsfdcWrHwECC(BYTE);
int    _Check_D_SsfdcBusy(WORD);
int    _Check_D_SsfdcStatus(void);
void   _Reset_D_SsfdcErr(void);
void   _Read_D_SsfdcBuf(BYTE *);
void   _Write_D_SsfdcBuf(BYTE *);
void   _Read_D_SsfdcByte(BYTE *);
void   _ReadRedt_D_SsfdcBuf(BYTE *);
void   _WriteRedt_D_SsfdcBuf(BYTE *);
BYTE   _Check_D_DevCode(BYTE);

void   _Set_D_ECCdata(BYTE, BYTE *);
void   _Calc_D_ECCdata(BYTE *);


struct SSFDCTYPE                Ssfdc;
struct ADDRESS                  Media;
struct CIS_AREA                 CisArea;

static BYTE                            EccBuf[6];
extern PBYTE                    SMHostAddr;
extern DWORD                    ErrXDCode;

extern WORD  ReadBlock;
extern WORD  WriteBlock;



#define EVEN                    0             /* Even Page for 256byte/page */
#define ODD                     1             /* Odd Page for 256byte/page */


/* SmartMedia Redundant buffer data Control Subroutine
 *----- Check_D_DataBlank() --------------------------------------------
 */
int Check_D_DataBlank(BYTE *redundant)
{
	char i;

	for (i = 0; i < REDTSIZE; i++)
		if (*redundant++ != 0xFF)
			return  ERROR;

	return SMSUCCESS;
}

/* ----- Check_D_FailBlock() -------------------------------------------- */
int Check_D_FailBlock(BYTE *redundant)
{
	redundant += REDT_BLOCK;

	if (*redundant == 0xFF)
		return SMSUCCESS;
	if (!*redundant)
		return ERROR;
	if (hweight8(*redundant) < 7)
		return ERROR;

	return SMSUCCESS;
}

/* ----- Check_D_DataStatus() ------------------------------------------- */
int Check_D_DataStatus(BYTE *redundant)
{
	redundant += REDT_DATA;

	if (*redundant == 0xFF)
		return SMSUCCESS;
	if (!*redundant) {
		ErrXDCode = ERR_DataStatus;
		return ERROR;
	} else
		ErrXDCode = NO_ERROR;

	if (hweight8(*redundant) < 5)
		return ERROR;

	return SMSUCCESS;
}

/* ----- Load_D_LogBlockAddr() ------------------------------------------ */
int Load_D_LogBlockAddr(BYTE *redundant)
{
	WORD addr1, addr2;

	addr1 = (WORD)*(redundant + REDT_ADDR1H)*0x0100 + (WORD)*(redundant + REDT_ADDR1L);
	addr2 = (WORD)*(redundant + REDT_ADDR2H)*0x0100 + (WORD)*(redundant + REDT_ADDR2L);

	if (addr1 == addr2)
		if ((addr1 & 0xF000) == 0x1000) {
			Media.LogBlock = (addr1 & 0x0FFF) / 2;
			return SMSUCCESS;
		}

	if (hweight16((WORD)(addr1^addr2)) != 0x01)
		return ERROR;

	if ((addr1 & 0xF000) == 0x1000)
		if (!(hweight16(addr1) & 0x01)) {
			Media.LogBlock = (addr1 & 0x0FFF) / 2;
			return SMSUCCESS;
		}

	if ((addr2 & 0xF000) == 0x1000)
		if (!(hweight16(addr2) & 0x01)) {
			Media.LogBlock = (addr2 & 0x0FFF) / 2;
			return SMSUCCESS;
		}

	return ERROR;
}

/* ----- Clr_D_RedundantData() ------------------------------------------ */
void Clr_D_RedundantData(BYTE *redundant)
{
	char i;

	for (i = 0; i < REDTSIZE; i++)
		*(redundant + i) = 0xFF;
}

/* ----- Set_D_LogBlockAddr() ------------------------------------------- */
void Set_D_LogBlockAddr(BYTE *redundant)
{
	WORD addr;

	*(redundant + REDT_BLOCK) = 0xFF;
	*(redundant + REDT_DATA) = 0xFF;
	addr = Media.LogBlock*2 + 0x1000;

	if ((hweight16(addr) % 2))
		addr++;

	*(redundant + REDT_ADDR1H) = *(redundant + REDT_ADDR2H) = (BYTE)(addr / 0x0100);
	*(redundant + REDT_ADDR1L) = *(redundant + REDT_ADDR2L) = (BYTE)addr;
}

/*----- Set_D_FailBlock() ---------------------------------------------- */
void Set_D_FailBlock(BYTE *redundant)
{
	char i;
	for (i = 0; i < REDTSIZE; i++)
		*redundant++ = (BYTE)((i == REDT_BLOCK) ? 0xF0 : 0xFF);
}

/* ----- Set_D_DataStaus() ---------------------------------------------- */
void Set_D_DataStaus(BYTE *redundant)
{
	redundant += REDT_DATA;
	*redundant = 0x00;
}

/* SmartMedia Function Command Subroutine
 * 6250 CMD 6
 */
/* ----- Ssfdc_D_Reset() ------------------------------------------------ */
void Ssfdc_D_Reset(struct us_data *us)
{
	return;
}

/* ----- Ssfdc_D_ReadCisSect() ------------------------------------------ */
int Ssfdc_D_ReadCisSect(struct us_data *us, BYTE *buf, BYTE *redundant)
{
	BYTE zone, sector;
	WORD block;

	zone = Media.Zone; block = Media.PhyBlock; sector = Media.Sector;
	Media.Zone = 0;
	Media.PhyBlock = CisArea.PhyBlock;
	Media.Sector = CisArea.Sector;

	if (Ssfdc_D_ReadSect(us, buf, redundant)) {
		Media.Zone = zone; Media.PhyBlock = block; Media.Sector = sector;
		return ERROR;
	}

	Media.Zone = zone; Media.PhyBlock = block; Media.Sector = sector;
	return SMSUCCESS;
}

/* 6250 CMD 1 */
/* ----- Ssfdc_D_ReadSect() --------------------------------------------- */
int Ssfdc_D_ReadSect(struct us_data *us, BYTE *buf, BYTE *redundant)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	int	result;
	WORD	addr;

	result = ENE_LoadBinCode(us, SM_RW_PATTERN);
	if (result != USB_STOR_XFER_GOOD) {
		printk("Load SM RW Code Fail !!\n");
		return USB_STOR_TRANSPORT_ERROR;
	}

	addr = (WORD)Media.Zone*Ssfdc.MaxBlocks + Media.PhyBlock;
	addr = addr*(WORD)Ssfdc.MaxSectors + Media.Sector;

	/* Read sect data */
	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength	= 0x200;
	bcb->Flags			= 0x80;
	bcb->CDB[0]			= 0xF1;
	bcb->CDB[1]			= 0x02;
	bcb->CDB[4]			= (BYTE)addr;
	bcb->CDB[3]			= (BYTE)(addr / 0x0100);
	bcb->CDB[2]			= Media.Zone / 2;

	result = ENE_SendScsiCmd(us, FDIR_READ, buf, 0);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	/* Read redundant */
	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength	= 0x10;
	bcb->Flags			= 0x80;
	bcb->CDB[0]			= 0xF1;
	bcb->CDB[1]			= 0x03;
	bcb->CDB[4]			= (BYTE)addr;
	bcb->CDB[3]			= (BYTE)(addr / 0x0100);
	bcb->CDB[2]			= Media.Zone / 2;
	bcb->CDB[8]			= 0;
	bcb->CDB[9]			= 1;

	result = ENE_SendScsiCmd(us, FDIR_READ, redundant, 0);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	return USB_STOR_TRANSPORT_GOOD;
}

/* ----- Ssfdc_D_ReadBlock() --------------------------------------------- */
int Ssfdc_D_ReadBlock(struct us_data *us, WORD count, BYTE *buf, BYTE *redundant)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	int	result;
	WORD	addr;

	result = ENE_LoadBinCode(us, SM_RW_PATTERN);
	if (result != USB_STOR_XFER_GOOD) {
		printk("Load SM RW Code Fail !!\n");
		return USB_STOR_TRANSPORT_ERROR;
	}

	addr = (WORD)Media.Zone*Ssfdc.MaxBlocks + Media.PhyBlock;
	addr = addr*(WORD)Ssfdc.MaxSectors + Media.Sector;

	/* Read sect data */
	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength	= 0x200*count;
	bcb->Flags			= 0x80;
	bcb->CDB[0]			= 0xF1;
	bcb->CDB[1]			= 0x02;
	bcb->CDB[4]			= (BYTE)addr;
	bcb->CDB[3]			= (BYTE)(addr / 0x0100);
	bcb->CDB[2]			= Media.Zone / 2;

	result = ENE_SendScsiCmd(us, FDIR_READ, buf, 0);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	/* Read redundant */
	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength	= 0x10;
	bcb->Flags			= 0x80;
	bcb->CDB[0]			= 0xF1;
	bcb->CDB[1]			= 0x03;
	bcb->CDB[4]			= (BYTE)addr;
	bcb->CDB[3]			= (BYTE)(addr / 0x0100);
	bcb->CDB[2]			= Media.Zone / 2;
	bcb->CDB[8]			= 0;
	bcb->CDB[9]			= 1;

	result = ENE_SendScsiCmd(us, FDIR_READ, redundant, 0);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	return USB_STOR_TRANSPORT_GOOD;
}


/* ----- Ssfdc_D_CopyBlock() -------------------------------------------- */
int Ssfdc_D_CopyBlock(struct us_data *us, WORD count, BYTE *buf, BYTE *redundant)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	int	result;
	WORD	ReadAddr, WriteAddr;

	result = ENE_LoadBinCode(us, SM_RW_PATTERN);
	if (result != USB_STOR_XFER_GOOD) {
		printk("Load SM RW Code Fail !!\n");
		return USB_STOR_TRANSPORT_ERROR;
	}

	ReadAddr = (WORD)Media.Zone*Ssfdc.MaxBlocks + ReadBlock;
	ReadAddr = ReadAddr*(WORD)Ssfdc.MaxSectors;
	WriteAddr = (WORD)Media.Zone*Ssfdc.MaxBlocks + WriteBlock;
	WriteAddr = WriteAddr*(WORD)Ssfdc.MaxSectors;

	/* Write sect data */
	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength	= 0x200*count;
	bcb->Flags			= 0x00;
	bcb->CDB[0]			= 0xF0;
	bcb->CDB[1]			= 0x08;
	bcb->CDB[7]			= (BYTE)WriteAddr;
	bcb->CDB[6]			= (BYTE)(WriteAddr / 0x0100);
	bcb->CDB[5]			= Media.Zone / 2;
	bcb->CDB[8]			= *(redundant + REDT_ADDR1H);
	bcb->CDB[9]			= *(redundant + REDT_ADDR1L);
	bcb->CDB[10]		= Media.Sector;

	if (ReadBlock != NO_ASSIGN) {
		bcb->CDB[4]		= (BYTE)ReadAddr;
		bcb->CDB[3]		= (BYTE)(ReadAddr / 0x0100);
		bcb->CDB[2]		= Media.Zone / 2;
	} else
		bcb->CDB[11]	= 1;

	result = ENE_SendScsiCmd(us, FDIR_WRITE, buf, 0);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	return USB_STOR_TRANSPORT_GOOD;
}

/* ----- Ssfdc_D_WriteSectForCopy() ------------------------------------- */
int Ssfdc_D_WriteSectForCopy(struct us_data *us, BYTE *buf, BYTE *redundant)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	int	result;
	WORD	addr;

	result = ENE_LoadBinCode(us, SM_RW_PATTERN);
	if (result != USB_STOR_XFER_GOOD) {
		printk("Load SM RW Code Fail !!\n");
		return USB_STOR_TRANSPORT_ERROR;
	}


	addr = (WORD)Media.Zone*Ssfdc.MaxBlocks + Media.PhyBlock;
	addr = addr*(WORD)Ssfdc.MaxSectors + Media.Sector;

	/* Write sect data */
	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength	= 0x200;
	bcb->Flags			= 0x00;
	bcb->CDB[0]			= 0xF0;
	bcb->CDB[1]			= 0x04;
	bcb->CDB[7]			= (BYTE)addr;
	bcb->CDB[6]			= (BYTE)(addr / 0x0100);
	bcb->CDB[5]			= Media.Zone / 2;
	bcb->CDB[8]			= *(redundant + REDT_ADDR1H);
	bcb->CDB[9]			= *(redundant + REDT_ADDR1L);

	result = ENE_SendScsiCmd(us, FDIR_WRITE, buf, 0);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	return USB_STOR_TRANSPORT_GOOD;
}

/* 6250 CMD 5 */
/* ----- Ssfdc_D_EraseBlock() ------------------------------------------- */
int Ssfdc_D_EraseBlock(struct us_data *us)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	int	result;
	WORD	addr;

	result = ENE_LoadBinCode(us, SM_RW_PATTERN);
	if (result != USB_STOR_XFER_GOOD) {
		printk("Load SM RW Code Fail !!\n");
		return USB_STOR_TRANSPORT_ERROR;
	}

	addr = (WORD)Media.Zone*Ssfdc.MaxBlocks + Media.PhyBlock;
	addr = addr*(WORD)Ssfdc.MaxSectors;

	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength	= 0x200;
	bcb->Flags			= 0x80;
	bcb->CDB[0]			= 0xF2;
	bcb->CDB[1]			= 0x06;
	bcb->CDB[7]			= (BYTE)addr;
	bcb->CDB[6]			= (BYTE)(addr / 0x0100);
	bcb->CDB[5]			= Media.Zone / 2;

	result = ENE_SendScsiCmd(us, FDIR_READ, NULL, 0);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	return USB_STOR_TRANSPORT_GOOD;
}

/* 6250 CMD 2 */
/*----- Ssfdc_D_ReadRedtData() ----------------------------------------- */
int Ssfdc_D_ReadRedtData(struct us_data *us, BYTE *redundant)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	int	result;
	WORD	addr;
	BYTE	*buf;

	result = ENE_LoadBinCode(us, SM_RW_PATTERN);
	if (result != USB_STOR_XFER_GOOD) {
		printk("Load SM RW Code Fail !!\n");
		return USB_STOR_TRANSPORT_ERROR;
	}

	addr = (WORD)Media.Zone*Ssfdc.MaxBlocks + Media.PhyBlock;
	addr = addr*(WORD)Ssfdc.MaxSectors + Media.Sector;

	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength	= 0x10;
	bcb->Flags			= 0x80;
	bcb->CDB[0]			= 0xF1;
	bcb->CDB[1]			= 0x03;
	bcb->CDB[4]			= (BYTE)addr;
	bcb->CDB[3]			= (BYTE)(addr / 0x0100);
	bcb->CDB[2]			= Media.Zone / 2;
	bcb->CDB[8]			= 0;
	bcb->CDB[9]			= 1;

	buf = kmalloc(0x10, GFP_KERNEL);
	result = ENE_SendScsiCmd(us, FDIR_READ, buf, 0);
	memcpy(redundant, buf, 0x10);
	kfree(buf);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	return USB_STOR_TRANSPORT_GOOD;
}

/* 6250 CMD 4 */
/* ----- Ssfdc_D_WriteRedtData() ---------------------------------------- */
int Ssfdc_D_WriteRedtData(struct us_data *us, BYTE *redundant)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	int	result;
	WORD                    addr;

	result = ENE_LoadBinCode(us, SM_RW_PATTERN);
	if (result != USB_STOR_XFER_GOOD) {
		printk("Load SM RW Code Fail !!\n");
		return USB_STOR_TRANSPORT_ERROR;
	}

	addr = (WORD)Media.Zone*Ssfdc.MaxBlocks + Media.PhyBlock;
	addr = addr*(WORD)Ssfdc.MaxSectors + Media.Sector;

	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength	= 0x10;
	bcb->Flags			= 0x80;
	bcb->CDB[0]			= 0xF2;
	bcb->CDB[1]			= 0x05;
	bcb->CDB[7]			= (BYTE)addr;
	bcb->CDB[6]			= (BYTE)(addr / 0x0100);
	bcb->CDB[5]			= Media.Zone / 2;
	bcb->CDB[8]			= *(redundant + REDT_ADDR1H);
	bcb->CDB[9]			= *(redundant + REDT_ADDR1L);

	result = ENE_SendScsiCmd(us, FDIR_READ, NULL, 0);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	return USB_STOR_TRANSPORT_GOOD;
}

/* ----- Ssfdc_D_CheckStatus() ------------------------------------------ */
int Ssfdc_D_CheckStatus(void)
{
	return SMSUCCESS;
}



/* SmartMedia ID Code Check & Mode Set Subroutine
 * ----- Set_D_SsfdcModel() ---------------------------------------------
 */
int Set_D_SsfdcModel(BYTE dcode)
{
	switch (_Check_D_DevCode(dcode)) {
	case SSFDC1MB:
		Ssfdc.Model        = SSFDC1MB;
		Ssfdc.Attribute    = FLASH | AD3CYC | BS16 | PS256;
		Ssfdc.MaxZones     = 1;
		Ssfdc.MaxBlocks    = 256;
		Ssfdc.MaxLogBlocks = 250;
		Ssfdc.MaxSectors   = 8;
		break;
	case SSFDC2MB:
		Ssfdc.Model        = SSFDC2MB;
		Ssfdc.Attribute    = FLASH | AD3CYC | BS16 | PS256;
		Ssfdc.MaxZones     = 1;
		Ssfdc.MaxBlocks    = 512;
		Ssfdc.MaxLogBlocks = 500;
		Ssfdc.MaxSectors   = 8;
		break;
	case SSFDC4MB:
		Ssfdc.Model        = SSFDC4MB;
		Ssfdc.Attribute    = FLASH | AD3CYC | BS16 | PS512;
		Ssfdc.MaxZones     = 1;
		Ssfdc.MaxBlocks    = 512;
		Ssfdc.MaxLogBlocks = 500;
		Ssfdc.MaxSectors   = 16;
		break;
	case SSFDC8MB:
		Ssfdc.Model        = SSFDC8MB;
		Ssfdc.Attribute    = FLASH | AD3CYC | BS16 | PS512;
		Ssfdc.MaxZones     = 1;
		Ssfdc.MaxBlocks    = 1024;
		Ssfdc.MaxLogBlocks = 1000;
		Ssfdc.MaxSectors   = 16;
		break;
	case SSFDC16MB:
		Ssfdc.Model        = SSFDC16MB;
		Ssfdc.Attribute    = FLASH | AD3CYC | BS32 | PS512;
		Ssfdc.MaxZones     = 1;
		Ssfdc.MaxBlocks    = 1024;
		Ssfdc.MaxLogBlocks = 1000;
		Ssfdc.MaxSectors   = 32;
		break;
	case SSFDC32MB:
		Ssfdc.Model        = SSFDC32MB;
		Ssfdc.Attribute    = FLASH | AD3CYC | BS32 | PS512;
		Ssfdc.MaxZones     = 2;
		Ssfdc.MaxBlocks    = 1024;
		Ssfdc.MaxLogBlocks = 1000;
		Ssfdc.MaxSectors   = 32;
		break;
	case SSFDC64MB:
		Ssfdc.Model        = SSFDC64MB;
		Ssfdc.Attribute    = FLASH | AD4CYC | BS32 | PS512;
		Ssfdc.MaxZones     = 4;
		Ssfdc.MaxBlocks    = 1024;
		Ssfdc.MaxLogBlocks = 1000;
		Ssfdc.MaxSectors   = 32;
		break;
	case SSFDC128MB:
		Ssfdc.Model        = SSFDC128MB;
		Ssfdc.Attribute    = FLASH | AD4CYC | BS32 | PS512;
		Ssfdc.MaxZones     = 8;
		Ssfdc.MaxBlocks    = 1024;
		Ssfdc.MaxLogBlocks = 1000;
		Ssfdc.MaxSectors   = 32;
		break;
	case SSFDC256MB:
		Ssfdc.Model        = SSFDC256MB;
		Ssfdc.Attribute    = FLASH | AD4CYC | BS32 | PS512;
		Ssfdc.MaxZones     = 16;
		Ssfdc.MaxBlocks    = 1024;
		Ssfdc.MaxLogBlocks = 1000;
		Ssfdc.MaxSectors   = 32;
		break;
	case SSFDC512MB:
		Ssfdc.Model        = SSFDC512MB;
		Ssfdc.Attribute    = FLASH | AD4CYC | BS32 | PS512;
		Ssfdc.MaxZones     = 32;
		Ssfdc.MaxBlocks    = 1024;
		Ssfdc.MaxLogBlocks = 1000;
		Ssfdc.MaxSectors   = 32;
		break;
	case SSFDC1GB:
		Ssfdc.Model        = SSFDC1GB;
		Ssfdc.Attribute    = FLASH | AD4CYC | BS32 | PS512;
		Ssfdc.MaxZones     = 64;
		Ssfdc.MaxBlocks    = 1024;
		Ssfdc.MaxLogBlocks = 1000;
		Ssfdc.MaxSectors   = 32;
		break;
	case SSFDC2GB:
		Ssfdc.Model        = SSFDC2GB;
		Ssfdc.Attribute    = FLASH | AD4CYC | BS32 | PS512;
		Ssfdc.MaxZones     = 128;
		Ssfdc.MaxBlocks    = 1024;
		Ssfdc.MaxLogBlocks = 1000;
		Ssfdc.MaxSectors   = 32;
		break;
	default:
		Ssfdc.Model = NOSSFDC;
		return ERROR;
	}

    return SMSUCCESS;
}

/* ----- _Check_D_DevCode() --------------------------------------------- */
BYTE _Check_D_DevCode(BYTE dcode)
{
	switch (dcode) {
	case 0x6E:
	case 0xE8:
	case 0xEC: return SSFDC1MB;   /* 8Mbit (1M) NAND */
	case 0x64:
	case 0xEA: return SSFDC2MB;   /* 16Mbit (2M) NAND */
	case 0x6B:
	case 0xE3:
	case 0xE5: return SSFDC4MB;   /* 32Mbit (4M) NAND */
	case 0xE6: return SSFDC8MB;   /* 64Mbit (8M) NAND */
	case 0x73: return SSFDC16MB;  /* 128Mbit (16M)NAND */
	case 0x75: return SSFDC32MB;  /* 256Mbit (32M)NAND */
	case 0x76: return SSFDC64MB;  /* 512Mbit (64M)NAND */
	case 0x79: return SSFDC128MB; /* 1Gbit(128M)NAND */
	case 0x71: return SSFDC256MB;
	case 0xDC: return SSFDC512MB;
	case 0xD3: return SSFDC1GB;
	case 0xD5: return SSFDC2GB;
	default: return NOSSFDC;
	}
}




/* SmartMedia ECC Control Subroutine
 * ----- Check_D_ReadError() ----------------------------------------------
 */
int Check_D_ReadError(BYTE *redundant)
{
	return SMSUCCESS;
}

/* ----- Check_D_Correct() ---------------------------------------------- */
int Check_D_Correct(BYTE *buf, BYTE *redundant)
{
	return SMSUCCESS;
}

/* ----- Check_D_CISdata() ---------------------------------------------- */
int Check_D_CISdata(BYTE *buf, BYTE *redundant)
{
	BYTE cis[] = {0x01, 0x03, 0xD9, 0x01, 0xFF, 0x18, 0x02,
		      0xDF, 0x01, 0x20};

	int cis_len = sizeof(cis);

	if (!IsSSFDCCompliance && !IsXDCompliance)
		return SMSUCCESS;

	if (!memcmp(redundant + 0x0D, EccBuf, 3))
		return memcmp(buf, cis, cis_len);

	if (!_Correct_D_SwECC(buf, redundant + 0x0D, EccBuf))
		return memcmp(buf, cis, cis_len);

	buf += 0x100;
	if (!memcmp(redundant + 0x08, EccBuf + 0x03, 3))
		return memcmp(buf, cis, cis_len);

	if (!_Correct_D_SwECC(buf, redundant + 0x08, EccBuf + 0x03))
		return memcmp(buf, cis, cis_len);

	return ERROR;
}

/* ----- Set_D_RightECC() ---------------------------------------------- */
void Set_D_RightECC(BYTE *redundant)
{
    /* Driver ECC Check */
    return;
}


