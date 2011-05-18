#include <linux/slab.h>
#include "usb.h"
#include "scsiglue.h"
#include "transport.h"
//#include "init.h"

//#include "stdlib.h"
//#include "EUCR6SK.h"
#include "smcommon.h"
#include "smil.h"

void   _Set_D_SsfdcRdCmd     (BYTE);
void   _Set_D_SsfdcRdAddr    (BYTE);
void   _Set_D_SsfdcRdChip    (void);
void   _Set_D_SsfdcRdStandby (void);
void   _Start_D_SsfdcRdHwECC (void);
void   _Stop_D_SsfdcRdHwECC  (void);
void   _Load_D_SsfdcRdHwECC  (BYTE);
void   _Set_D_SsfdcWrCmd     (BYTE);
void   _Set_D_SsfdcWrAddr    (BYTE);
void   _Set_D_SsfdcWrBlock   (void);
void   _Set_D_SsfdcWrStandby (void);
void   _Start_D_SsfdcWrHwECC (void);
void   _Load_D_SsfdcWrHwECC  (BYTE);
int    _Check_D_SsfdcBusy    (WORD);
int    _Check_D_SsfdcStatus  (void);
void   _Reset_D_SsfdcErr     (void);
void   _Read_D_SsfdcBuf      (BYTE *);
void   _Write_D_SsfdcBuf     (BYTE *);
void   _Read_D_SsfdcByte     (BYTE *);
void   _ReadRedt_D_SsfdcBuf  (BYTE *);
void   _WriteRedt_D_SsfdcBuf (BYTE *);
BYTE   _Check_D_DevCode      (BYTE);

void   _Set_D_ECCdata        (BYTE,BYTE *);
void   _Calc_D_ECCdata       (BYTE *);

//void   SM_ReadDataWithDMA      (PFDO_DEVICE_EXTENSION, BYTE *, WORD);
//void   SM_WriteDataWithDMA     (PFDO_DEVICE_EXTENSION, BYTE *, WORD);
//
struct SSFDCTYPE                Ssfdc;
struct ADDRESS                  Media;
struct CIS_AREA                 CisArea;

BYTE                            EccBuf[6];
extern PBYTE                    SMHostAddr;
extern BYTE                     IsSSFDCCompliance;
extern BYTE                     IsXDCompliance;
extern DWORD                    ErrXDCode;

extern WORD  ReadBlock;
extern WORD  WriteBlock;

//KEVENT                          SM_DMADoneEvent;

#define EVEN                    0             // Even Page for 256byte/page
#define ODD                     1             // Odd Page for 256byte/page


//SmartMedia Redundant buffer data Control Subroutine
//----- Check_D_DataBlank() --------------------------------------------
int Check_D_DataBlank(BYTE *redundant)
{
	char i;

	for(i=0; i<REDTSIZE; i++)
		if (*redundant++!=0xFF)
			return(ERROR);

	return(SUCCESS);
}

//----- Check_D_FailBlock() --------------------------------------------
int Check_D_FailBlock(BYTE *redundant)
{
	redundant+=REDT_BLOCK;

	if (*redundant==0xFF)
		return(SUCCESS);
	if (!*redundant)
		return(ERROR);
	if (hweight8(*redundant)<7)
		return(ERROR);

	return(SUCCESS);
}

//----- Check_D_DataStatus() -------------------------------------------
int Check_D_DataStatus(BYTE *redundant)
{
	redundant+=REDT_DATA;

	if (*redundant==0xFF)
		return(SUCCESS);
	if (!*redundant)
	{
		ErrXDCode = ERR_DataStatus;
		return(ERROR);
	}
	else
		ErrXDCode = NO_ERROR;

	if (hweight8(*redundant)<5)
		return(ERROR);

	return(SUCCESS);
}

//----- Load_D_LogBlockAddr() ------------------------------------------
int Load_D_LogBlockAddr(BYTE *redundant)
{
	WORD addr1,addr2;
	//SSFDCTYPE_T aa = (SSFDCTYPE_T ) &Ssfdc;
	//ADDRESS_T   bb = (ADDRESS_T) &Media;

	addr1=(WORD)*(redundant+REDT_ADDR1H)*0x0100+(WORD)*(redundant+REDT_ADDR1L);
	addr2=(WORD)*(redundant+REDT_ADDR2H)*0x0100+(WORD)*(redundant+REDT_ADDR2L);

	if (addr1==addr2)
		if ((addr1 &0xF000)==0x1000)
		{ Media.LogBlock=(addr1 &0x0FFF)/2; return(SUCCESS); }

	if (hweight16((WORD)(addr1^addr2))!=0x01) return(ERROR);

	if ((addr1 &0xF000)==0x1000)
		if (!(hweight16(addr1) &0x01))
		{ Media.LogBlock=(addr1 &0x0FFF)/2; return(SUCCESS); }

	if ((addr2 &0xF000)==0x1000)
		if (!(hweight16(addr2) &0x01))
		{ Media.LogBlock=(addr2 &0x0FFF)/2; return(SUCCESS); }

	return(ERROR);
}

//----- Clr_D_RedundantData() ------------------------------------------
void Clr_D_RedundantData(BYTE *redundant)
{
	char i;

	for(i=0; i<REDTSIZE; i++)
	*(redundant+i)=0xFF;
}

//----- Set_D_LogBlockAddr() -------------------------------------------
void Set_D_LogBlockAddr(BYTE *redundant)
{
	WORD addr;

	*(redundant+REDT_BLOCK)=0xFF;
	*(redundant+REDT_DATA) =0xFF;
	addr=Media.LogBlock*2+0x1000;

	if ((hweight16(addr)%2))
		addr++;

	*(redundant+REDT_ADDR1H)=*(redundant+REDT_ADDR2H)=(BYTE)(addr/0x0100);
	*(redundant+REDT_ADDR1L)=*(redundant+REDT_ADDR2L)=(BYTE)addr;
}

//----- Set_D_FailBlock() ----------------------------------------------
void Set_D_FailBlock(BYTE *redundant)
{
    char i;

    for(i=0; i<REDTSIZE; i++)
        *redundant++=(BYTE)((i==REDT_BLOCK)?0xF0:0xFF);
}

//----- Set_D_DataStaus() ----------------------------------------------
void Set_D_DataStaus(BYTE *redundant)
{
    redundant+=REDT_DATA;
    *redundant=0x00;
}

//SmartMedia Function Command Subroutine
// 6250 CMD 6
//----- Ssfdc_D_Reset() ------------------------------------------------
void Ssfdc_D_Reset(struct us_data *us)
{
	//NTSTATUS        ntStatus = STATUS_SUCCESS;
	//PBULK_CBW       pBulkCbw = fdoExt->pBulkCbw;
	//BYTE            buf[0x200];

	//printk("Ssfdc_D_Reset --- But do nothing !!\n");
	return;
/*	RtlZeroMemory(pBulkCbw, sizeof(struct _BULK_CBW));
	pBulkCbw->dCBWSignature          = CBW_SIGNTURE;
	pBulkCbw->bCBWLun                = CBW_LUN;
	//pBulkCbw->dCBWDataTransferLength = 0x200;
	pBulkCbw->bmCBWFlags             = 0x80;
	pBulkCbw->CBWCb[0]               = 0xF2;
	pBulkCbw->CBWCb[1]               = 0x07;

	ntStatus = ENE_SendScsiCmd(fdoExt, FDIR_READ, NULL);

	if (!NT_SUCCESS(ntStatus))
	{
		ENE_Print("Ssfdc_D_Reset Fail !!\n");
		//return ntStatus;
	}*/
}

//----- Ssfdc_D_ReadCisSect() ------------------------------------------
int Ssfdc_D_ReadCisSect(struct us_data *us, BYTE *buf,BYTE *redundant)
{
	BYTE zone,sector;
	WORD block;
	//SSFDCTYPE_T aa = (SSFDCTYPE_T ) &Ssfdc;
	//ADDRESS_T   bb = (ADDRESS_T) &Media;

	zone=Media.Zone; block=Media.PhyBlock; sector=Media.Sector;
	Media.Zone=0;
	Media.PhyBlock=CisArea.PhyBlock;
	Media.Sector=CisArea.Sector;

	if (Ssfdc_D_ReadSect(us,buf,redundant))
	{
		Media.Zone=zone; Media.PhyBlock=block; Media.Sector=sector;
		return(ERROR);
	}

	Media.Zone=zone; Media.PhyBlock=block; Media.Sector=sector;
	return(SUCCESS);
}
/*
////----- Ssfdc_D_WriteRedtMode() ----------------------------------------
//void Ssfdc_D_WriteRedtMode(void)
//{
//    _Set_D_SsfdcRdCmd     (RST_CHIP);
//    _Check_D_SsfdcBusy    (BUSY_RESET);
//    _Set_D_SsfdcRdCmd     (READ_REDT);
//    _Check_D_SsfdcBusy    (BUSY_READ);
//    _Set_D_SsfdcRdStandby ();
//}
//
////----- Ssfdc_D_ReadID() -----------------------------------------------
//void Ssfdc_D_ReadID(BYTE *buf, BYTE ReadID)
//{
//    _Set_D_SsfdcRdCmd     (ReadID);
//    _Set_D_SsfdcRdChip    ();
//    _Read_D_SsfdcByte     (buf++);
//    _Read_D_SsfdcByte     (buf++);
//    _Read_D_SsfdcByte     (buf++);
//    _Read_D_SsfdcByte     (buf);
//    _Set_D_SsfdcRdStandby ();
//}
*/
// 6250 CMD 1
//----- Ssfdc_D_ReadSect() ---------------------------------------------
int Ssfdc_D_ReadSect(struct us_data *us, BYTE *buf,BYTE *redundant)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	int	result;
	WORD	addr;

	result = ENE_LoadBinCode(us, SM_RW_PATTERN);
	if (result != USB_STOR_XFER_GOOD)
	{
		printk("Load SM RW Code Fail !!\n");
		return USB_STOR_TRANSPORT_ERROR;
	}

	addr = (WORD)Media.Zone*Ssfdc.MaxBlocks+Media.PhyBlock;
	addr = addr*(WORD)Ssfdc.MaxSectors+Media.Sector;

	// Read sect data
	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength	= 0x200;
	bcb->Flags			= 0x80;
	bcb->CDB[0]			= 0xF1;
	bcb->CDB[1]			= 0x02;
	bcb->CDB[4]			= (BYTE)addr;
	bcb->CDB[3]			= (BYTE)(addr/0x0100);
	bcb->CDB[2]			= Media.Zone/2;

	result = ENE_SendScsiCmd(us, FDIR_READ, buf, 0);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	// Read redundant
	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength	= 0x10;
	bcb->Flags			= 0x80;
	bcb->CDB[0]			= 0xF1;
	bcb->CDB[1]			= 0x03;
	bcb->CDB[4]			= (BYTE)addr;
	bcb->CDB[3]			= (BYTE)(addr/0x0100);
	bcb->CDB[2]			= Media.Zone/2;
	bcb->CDB[8]			= 0;
	bcb->CDB[9]			= 1;

	result = ENE_SendScsiCmd(us, FDIR_READ, redundant, 0);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	return USB_STOR_TRANSPORT_GOOD;
}

//----- Ssfdc_D_ReadBlock() ---------------------------------------------
int Ssfdc_D_ReadBlock(struct us_data *us, WORD count, BYTE *buf,BYTE *redundant)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	int	result;
	WORD	addr;

	//printk("Ssfdc_D_ReadBlock\n");
	result = ENE_LoadBinCode(us, SM_RW_PATTERN);
	if (result != USB_STOR_XFER_GOOD)
	{
		printk("Load SM RW Code Fail !!\n");
		return USB_STOR_TRANSPORT_ERROR;
	}

	addr = (WORD)Media.Zone*Ssfdc.MaxBlocks+Media.PhyBlock;
	addr = addr*(WORD)Ssfdc.MaxSectors+Media.Sector;

	// Read sect data
	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength	= 0x200*count;
	bcb->Flags			= 0x80;
	bcb->CDB[0]			= 0xF1;
	bcb->CDB[1]			= 0x02;
	bcb->CDB[4]			= (BYTE)addr;
	bcb->CDB[3]			= (BYTE)(addr/0x0100);
	bcb->CDB[2]			= Media.Zone/2;

	result = ENE_SendScsiCmd(us, FDIR_READ, buf, 0);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	// Read redundant
	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength	= 0x10;
	bcb->Flags			= 0x80;
	bcb->CDB[0]			= 0xF1;
	bcb->CDB[1]			= 0x03;
	bcb->CDB[4]			= (BYTE)addr;
	bcb->CDB[3]			= (BYTE)(addr/0x0100);
	bcb->CDB[2]			= Media.Zone/2;
	bcb->CDB[8]			= 0;
	bcb->CDB[9]			= 1;

	result = ENE_SendScsiCmd(us, FDIR_READ, redundant, 0);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	return USB_STOR_TRANSPORT_GOOD;
}
/*
////----- Ssfdc_D_ReadSect_DMA() ---------------------------------------------
//int Ssfdc_D_ReadSect_DMA(PFDO_DEVICE_EXTENSION fdoExt, BYTE *buf,BYTE *redundant)
//{
//    WORD    SectByteCount, addr;
//    DWORD   Buffer[4];
//    WORD    len;
//
//    if (!_Hw_D_ChkCardIn())
//       return(ERROR);
//    addr=(WORD)Media.Zone*Ssfdc.MaxBlocks+Media.PhyBlock;
//    addr=addr*(WORD)Ssfdc.MaxSectors+Media.Sector;
//    // cycle starting address
//    SM_STARTADDR_LSB = 0x00;
//    SM_STARTADDR_IISB = (BYTE)addr;
//    SM_STARTADDR_IIISB = (BYTE)(addr/0x0100);
//    SM_STARTADDR_MSB = Media.Zone/2;
//
//    //Sector byte count = 0x200(DMA)
//    SectByteCount = 0x20f;
//    SM_BYTECNT_LO = (BYTE)SectByteCount;
//    SM_CMD_CTRL3 = (SM_CMD_CTRL3 & 0xFC) | (BYTE)(SectByteCount/0x0100);
//    if ( ((fdoExt->ChipID==READER_CB712)&&(fdoExt->RevID==CHIP_A)) || fdoExt->IsHibernate )
//       SM_FIFO_CTRL = (SM_APB08_MASK | SM_DMAEN_MASK | SM_DMA_UPSTREAM_MASK | SM_FIFOSHLDVLU_8_MASK);
//    else
//       SM_FIFO_CTRL = (SM_APB32_MASK | SM_DMAEN_MASK | SM_DMA_UPSTREAM_MASK | SM_FIFOSHLDVLU_8_MASK);
//
//    _Hw_D_EccRdReset();
//    _Hw_D_EccRdStart();
//
//    SM_CMD_CTRL1 = (SM_CMD_READ_1);
//    SM_CMD_CTRL1 = (SM_CMD_READ_1 | SM_CMD_START_BIT);
//
//    SectByteCount = 0x1ff;
//    //SM_ReadDataWithDMA(fdoExt, buf, SectByteCount);
//    //_ReadRedt_D_SsfdcBuf(redundant);
//    len = 0x1000 - ((WORD)(buf) & 0x0FFF);
//    if (len < 0x200)
//    {
//       SM_ReadDataWithDMA(fdoExt, buf, len-1);
//       SM_ReadDataWithDMA(fdoExt, buf+len, SectByteCount-len);
//       //ENE_Print("Read DMA !!! buf1 = %p, len = %x, buf2 = %p\n", buf, len, buf+len);
//    }
//    else
//      SM_ReadDataWithDMA(fdoExt, buf, SectByteCount);
//
//    if ( ((fdoExt->ChipID==READER_CB712)&&(fdoExt->RevID==CHIP_A)) || fdoExt->IsHibernate )
//    {
//       _ReadRedt_D_SsfdcBuf(redundant);
//    }
//    else
//    {
//       Buffer[0] = READ_PORT_DWORD(SM_REG_DATA);
//       Buffer[1] = READ_PORT_DWORD(SM_REG_DATA);
//       Buffer[2] = READ_PORT_DWORD(SM_REG_DATA);
//       Buffer[3] = READ_PORT_DWORD(SM_REG_DATA);
//       memcpy(redundant, Buffer, 0x10);
//    }
//
//    while ( _Hw_D_ChkCardIn() )
//    {
//        if((READ_PORT_BYTE(SM_REG_INT_STATUS) & 0x10))
//        {
//            WRITE_PORT_BYTE(SM_REG_INT_STATUS, 0x10);
//            break;
//        }
//    }
//    _Hw_D_EccRdStop();
//    _Hw_D_SetRdStandby();
//    _Load_D_SsfdcRdHwECC(EVEN);
//
//    _Calc_D_ECCdata(buf);
//    _Set_D_SsfdcRdStandby();
//
//    if (!_Hw_D_ChkCardIn())
//       return(ERROR);
//    return(SUCCESS);
//}
//
////----- Ssfdc_D_ReadSect_PIO() ---------------------------------------------
//int Ssfdc_D_ReadSect_PIO(PFDO_DEVICE_EXTENSION fdoExt, BYTE *buf,BYTE *redundant)
//{
//    _Set_D_SsfdcRdCmd(READ);
//    _Set_D_SsfdcRdAddr(EVEN);
//
//    if (_Check_D_SsfdcBusy(BUSY_READ))
//    { _Reset_D_SsfdcErr(); return(ERROR); }
//
//    _Start_D_SsfdcRdHwECC();
//    _Read_D_SsfdcBuf(buf);
//    _Stop_D_SsfdcRdHwECC();
//    _ReadRedt_D_SsfdcBuf(redundant);
//    _Load_D_SsfdcRdHwECC(EVEN);
//
//    if (_Check_D_SsfdcBusy(BUSY_READ))
//    { _Reset_D_SsfdcErr(); return(ERROR); }
//
//    _Calc_D_ECCdata(buf);
//    _Set_D_SsfdcRdStandby();
//    return(SUCCESS);
//}

// 6250 CMD 3
//----- Ssfdc_D_WriteSect() --------------------------------------------
int Ssfdc_D_WriteSect(PFDO_DEVICE_EXTENSION fdoExt, BYTE *buf,BYTE *redundant)
{
    PBULK_CBW               pBulkCbw = fdoExt->pBulkCbw;
    NTSTATUS                ntStatus;
    WORD                    addr;

    //ENE_Print("SMILSUB --- Ssfdc_D_WriteSect\n");
    ENE_LoadBinCode(fdoExt, SM_RW_PATTERN);

    addr = (WORD)Media.Zone*Ssfdc.MaxBlocks+Media.PhyBlock;
    addr = addr*(WORD)Ssfdc.MaxSectors+Media.Sector;

    // Write sect data
    RtlZeroMemory(pBulkCbw, sizeof(struct _BULK_CBW));
    pBulkCbw->dCBWSignature          = CBW_SIGNTURE;
    pBulkCbw->bCBWLun                = CBW_LUN;
    pBulkCbw->dCBWDataTransferLength = 0x200;
    pBulkCbw->bmCBWFlags             = 0x00;
    pBulkCbw->CBWCb[0]               = 0xF0;
    pBulkCbw->CBWCb[1]               = 0x04;
    //pBulkCbw->CBWCb[4]               = (BYTE)addr;
    //pBulkCbw->CBWCb[3]               = (BYTE)(addr/0x0100);
    //pBulkCbw->CBWCb[2]               = Media.Zone/2;
    //pBulkCbw->CBWCb[5]               = *(redundant+REDT_ADDR1H);
    //pBulkCbw->CBWCb[6]               = *(redundant+REDT_ADDR1L);
    pBulkCbw->CBWCb[7]               = (BYTE)addr;
    pBulkCbw->CBWCb[6]               = (BYTE)(addr/0x0100);
    pBulkCbw->CBWCb[5]               = Media.Zone/2;
    pBulkCbw->CBWCb[8]               = *(redundant+REDT_ADDR1H);
    pBulkCbw->CBWCb[9]               = *(redundant+REDT_ADDR1L);

    ntStatus = ENE_SendScsiCmd(fdoExt, FDIR_WRITE, buf);

    if (!NT_SUCCESS(ntStatus))
       return(ERROR);

//  // For Test
//  {
//     BYTE   bf[0x200], rdd[0x10];
//     ULONG  i;
//
//     RtlZeroMemory(bf, 0x200);
//     RtlZeroMemory(rdd, 0x10);
//     ntStatus = SM_ReadBlock(fdoExt, bf, rdd);
//     for (i=0; i<0x200; i++)
//     {
//         if (buf[i] != bf[i])
//            ENE_Print("buf[%x] = %x, bf[%x] = %x\n", buf, bf);
//     }
//     if (!NT_SUCCESS(ntStatus))
//        ENE_Print("Error\n");
//  }

    return(SUCCESS);
}
*/
//----- Ssfdc_D_CopyBlock() --------------------------------------------
int Ssfdc_D_CopyBlock(struct us_data *us, WORD count, BYTE *buf,BYTE *redundant)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	int	result;
    //PBULK_CBW               pBulkCbw = fdoExt->pBulkCbw;
    //NTSTATUS                ntStatus;
	WORD	ReadAddr, WriteAddr;

	//printk("Ssfdc_D_WriteSect --- ZONE = %x, ReadBlock = %x, WriteBlock = %x\n", Media.Zone, ReadBlock, WriteBlock);

	result = ENE_LoadBinCode(us, SM_RW_PATTERN);
	if (result != USB_STOR_XFER_GOOD)
	{
		printk("Load SM RW Code Fail !!\n");
		return USB_STOR_TRANSPORT_ERROR;
	}

	ReadAddr = (WORD)Media.Zone*Ssfdc.MaxBlocks+ReadBlock;
	ReadAddr = ReadAddr*(WORD)Ssfdc.MaxSectors;
	WriteAddr = (WORD)Media.Zone*Ssfdc.MaxBlocks+WriteBlock;
	WriteAddr = WriteAddr*(WORD)Ssfdc.MaxSectors;

	// Write sect data
	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength	= 0x200*count;
	bcb->Flags			= 0x00;
	bcb->CDB[0]			= 0xF0;
	bcb->CDB[1]			= 0x08;
	bcb->CDB[7]			= (BYTE)WriteAddr;
	bcb->CDB[6]			= (BYTE)(WriteAddr/0x0100);
	bcb->CDB[5]			= Media.Zone/2;
	bcb->CDB[8]			= *(redundant+REDT_ADDR1H);
	bcb->CDB[9]			= *(redundant+REDT_ADDR1L);
	bcb->CDB[10]		= Media.Sector;

	if (ReadBlock != NO_ASSIGN)
	{
		bcb->CDB[4]		= (BYTE)ReadAddr;
		bcb->CDB[3]		= (BYTE)(ReadAddr/0x0100);
		bcb->CDB[2]		= Media.Zone/2;
	}
	else
		bcb->CDB[11]	= 1;

	result = ENE_SendScsiCmd(us, FDIR_WRITE, buf, 0);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	return USB_STOR_TRANSPORT_GOOD;
}
/*
//----- Ssfdc_D_WriteBlock() --------------------------------------------
int Ssfdc_D_WriteBlock(PFDO_DEVICE_EXTENSION fdoExt, WORD count, BYTE *buf,BYTE *redundant)
{
    PBULK_CBW               pBulkCbw = fdoExt->pBulkCbw;
    NTSTATUS                ntStatus;
    WORD                    addr;

    //ENE_Print("SMILSUB --- Ssfdc_D_WriteSect\n");
    ENE_LoadBinCode(fdoExt, SM_RW_PATTERN);

    addr = (WORD)Media.Zone*Ssfdc.MaxBlocks+Media.PhyBlock;
    addr = addr*(WORD)Ssfdc.MaxSectors+Media.Sector;

    // Write sect data
    RtlZeroMemory(pBulkCbw, sizeof(struct _BULK_CBW));
    pBulkCbw->dCBWSignature          = CBW_SIGNTURE;
    pBulkCbw->bCBWLun                = CBW_LUN;
    pBulkCbw->dCBWDataTransferLength = 0x200*count;
    pBulkCbw->bmCBWFlags             = 0x00;
    pBulkCbw->CBWCb[0]               = 0xF0;
    pBulkCbw->CBWCb[1]               = 0x04;
    pBulkCbw->CBWCb[7]               = (BYTE)addr;
    pBulkCbw->CBWCb[6]               = (BYTE)(addr/0x0100);
    pBulkCbw->CBWCb[5]               = Media.Zone/2;
    pBulkCbw->CBWCb[8]               = *(redundant+REDT_ADDR1H);
    pBulkCbw->CBWCb[9]               = *(redundant+REDT_ADDR1L);

    ntStatus = ENE_SendScsiCmd(fdoExt, FDIR_WRITE, buf);

    if (!NT_SUCCESS(ntStatus))
       return(ERROR);

//  // For Test
//  {
//     BYTE   bf[0x200], rdd[0x10];
//     ULONG  i;
//
//     RtlZeroMemory(bf, 0x200);
//     RtlZeroMemory(rdd, 0x10);
//     ntStatus = SM_ReadBlock(fdoExt, bf, rdd);
//     for (i=0; i<0x200; i++)
//     {
//         if (buf[i] != bf[i])
//            ENE_Print("buf[%x] = %x, bf[%x] = %x\n", buf, bf);
//     }
//     if (!NT_SUCCESS(ntStatus))
//        ENE_Print("Error\n");
//  }

    return(SUCCESS);
}
//
////----- Ssfdc_D_WriteSect_DMA() --------------------------------------------
//int Ssfdc_D_WriteSect_DMA(PFDO_DEVICE_EXTENSION fdoExt, BYTE *buf,BYTE *redundant)
//{
//    WORD    SectByteCount, addr;
//    DWORD   Buffer[4];
//    WORD    len;
//
//    if (!_Hw_D_ChkCardIn())
//       return(ERROR);
//    addr=(WORD)Media.Zone*Ssfdc.MaxBlocks+Media.PhyBlock;
//    addr=addr*(WORD)Ssfdc.MaxSectors+Media.Sector;
//    // cycle starting address
//    SM_STARTADDR_LSB = 0x00;
//    SM_STARTADDR_IISB = (BYTE)addr;
//    SM_STARTADDR_IIISB = (BYTE)(addr/0x0100);
//    SM_STARTADDR_MSB = Media.Zone/2;
//
//    //Sector byte count (DMA)
//    SectByteCount = 0x20f;
//    SM_BYTECNT_LO = (BYTE)SectByteCount;
//    SM_CMD_CTRL3 = (SM_CMD_CTRL3 & 0xFC) | 0x20 | (BYTE)(SectByteCount/0x0100);
//    if ( ((fdoExt->ChipID==READER_CB712)&&(fdoExt->RevID==CHIP_A)) || fdoExt->IsHibernate )
//       SM_FIFO_CTRL = (SM_APB08_MASK | SM_DMAEN_MASK | SM_DMA_DOWNSTREAM_MASK | SM_FIFOSHLDVLU_8_MASK);
//    else
//       SM_FIFO_CTRL = (SM_APB32_MASK | SM_DMAEN_MASK | SM_DMA_DOWNSTREAM_MASK | SM_FIFOSHLDVLU_8_MASK);
//
//    _Hw_D_EccRdReset();
//    _Hw_D_EccRdStart();
//
//    SM_CMD_CTRL1 = SM_CMD_PAGPRGM_TRUE;
//    SM_CMD_CTRL1 = (SM_CMD_PAGPRGM_TRUE | SM_CMD_START_BIT);
//
//    SectByteCount = 0x1ff;
//    //SM_WriteDataWithDMA(fdoExt, buf, SectByteCount);
//    //_WriteRedt_D_SsfdcBuf(redundant);
//    len = 0x1000 - ((WORD)(buf) & 0x0FFF);
//    if (len < 0x200)
//    {
//       SM_WriteDataWithDMA(fdoExt, buf, len-1);
//       SM_WriteDataWithDMA(fdoExt, buf+len, SectByteCount-len);
//       //ENE_Print("Read DMA !!! buf1 = %p, len = %x, buf2 = %p\n", buf, len, buf+len);
//    }
//    else
//      SM_WriteDataWithDMA(fdoExt, buf, SectByteCount);
//
//    //T1 = (ULONGLONG)buf & 0xFFFFFFFFFFFFF000;
//    //T2 = ((ULONGLONG)buf + 0x1FF) & 0xFFFFFFFFFFFFF000;
//    //if (T1 != T2)
//    //   ENE_Print("Ssfdc_D_WriteSect_DMA !!! buf = %p, T1 = %p, T2 = %p\n", buf, T1, T2);
//    //if (T2-T1)
//    //{
//    //   l1 = (WORD)(T2 - (ULONGLONG)buf);
//    //   SM_WriteDataWithDMA(fdoExt, buf, l1-1);
//    //   SM_WriteDataWithDMA(fdoExt, (PBYTE)T2, SectByteCount-l1);
//    //}
//    //else
//    //  SM_WriteDataWithDMA(fdoExt, buf, SectByteCount);
//
//    if ( ((fdoExt->ChipID==READER_CB712)&&(fdoExt->RevID==CHIP_A)) || fdoExt->IsHibernate )
//    {
//       _WriteRedt_D_SsfdcBuf(redundant);
//    }
//    else
//    {
//       memcpy(Buffer, redundant, 0x10);
//       WRITE_PORT_DWORD(SM_REG_DATA, Buffer[0]);
//       WRITE_PORT_DWORD(SM_REG_DATA, Buffer[1]);
//       WRITE_PORT_DWORD(SM_REG_DATA, Buffer[2]);
//       WRITE_PORT_DWORD(SM_REG_DATA, Buffer[3]);
//    }
//
//    while ( _Hw_D_ChkCardIn() )
//    {
//       if ((READ_PORT_BYTE(SM_REG_INT_STATUS) & 0x10))
//       {
//           WRITE_PORT_BYTE(SM_REG_INT_STATUS, 0x10);
//           break;
//       }
//    }
//    _Hw_D_EccRdStop();
//    _Hw_D_SetRdStandby();
//
//    _Set_D_SsfdcWrStandby();
//    _Set_D_SsfdcRdStandby();
//    if (!_Hw_D_ChkCardIn())
//       return(ERROR);
//
//    return(SUCCESS);
//}
//
////----- Ssfdc_D_WriteSect_PIO() --------------------------------------------
//int Ssfdc_D_WriteSect_PIO(PFDO_DEVICE_EXTENSION fdoExt, BYTE *buf,BYTE *redundant)
//{
//    _Calc_D_ECCdata(buf);
//    _Set_D_SsfdcWrCmd(WRDATA);
//    _Set_D_SsfdcWrAddr(EVEN);
//    _Start_D_SsfdcWrHwECC();
//
//    _Write_D_SsfdcBuf(buf);
//
//    _Load_D_SsfdcWrHwECC(EVEN);
//    _Set_D_ECCdata(EVEN,redundant);
//
//    _WriteRedt_D_SsfdcBuf(redundant);
//
//    _Set_D_SsfdcWrCmd(WRITE);
//
//    if (_Check_D_SsfdcBusy(BUSY_PROG))
//    { _Reset_D_SsfdcErr(); return(ERROR); }
//
//    _Set_D_SsfdcWrStandby();
//    _Set_D_SsfdcRdStandby();
//    return(SUCCESS);
//}
*/
//----- Ssfdc_D_WriteSectForCopy() -------------------------------------
int Ssfdc_D_WriteSectForCopy(struct us_data *us, BYTE *buf, BYTE *redundant)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	int	result;
	//PBULK_CBW               pBulkCbw = fdoExt->pBulkCbw;
	//NTSTATUS                ntStatus;
	WORD                    addr;

	//printk("SMILSUB --- Ssfdc_D_WriteSectForCopy\n");
	result = ENE_LoadBinCode(us, SM_RW_PATTERN);
	if (result != USB_STOR_XFER_GOOD)
	{
		printk("Load SM RW Code Fail !!\n");
		return USB_STOR_TRANSPORT_ERROR;
	}


	addr = (WORD)Media.Zone*Ssfdc.MaxBlocks+Media.PhyBlock;
	addr = addr*(WORD)Ssfdc.MaxSectors+Media.Sector;

	// Write sect data
	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength	= 0x200;
	bcb->Flags			= 0x00;
	bcb->CDB[0]			= 0xF0;
	bcb->CDB[1]			= 0x04;
	bcb->CDB[7]			= (BYTE)addr;
	bcb->CDB[6]			= (BYTE)(addr/0x0100);
	bcb->CDB[5]			= Media.Zone/2;
	bcb->CDB[8]			= *(redundant+REDT_ADDR1H);
	bcb->CDB[9]			= *(redundant+REDT_ADDR1L);

	result = ENE_SendScsiCmd(us, FDIR_WRITE, buf, 0);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	return USB_STOR_TRANSPORT_GOOD;
}

// 6250 CMD 5
//----- Ssfdc_D_EraseBlock() -------------------------------------------
int Ssfdc_D_EraseBlock(struct us_data *us)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	int	result;
	WORD	addr;

	result = ENE_LoadBinCode(us, SM_RW_PATTERN);
	if (result != USB_STOR_XFER_GOOD)
	{
		printk("Load SM RW Code Fail !!\n");
		return USB_STOR_TRANSPORT_ERROR;
	}

	addr=(WORD)Media.Zone*Ssfdc.MaxBlocks+Media.PhyBlock;
	addr=addr*(WORD)Ssfdc.MaxSectors;

	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength	= 0x200;
	bcb->Flags			= 0x80;
	bcb->CDB[0]			= 0xF2;
	bcb->CDB[1]			= 0x06;
	bcb->CDB[7]			= (BYTE)addr;
	bcb->CDB[6]			= (BYTE)(addr/0x0100);
	bcb->CDB[5]			= Media.Zone/2;

	result = ENE_SendScsiCmd(us, FDIR_READ, NULL, 0);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	return USB_STOR_TRANSPORT_GOOD;
}

// 6250 CMD 2
//----- Ssfdc_D_ReadRedtData() -----------------------------------------
int Ssfdc_D_ReadRedtData(struct us_data *us, BYTE *redundant)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	int	result;
	WORD	addr;
	BYTE  *buf;

	result = ENE_LoadBinCode(us, SM_RW_PATTERN);
	if (result != USB_STOR_XFER_GOOD)
	{
		printk("Load SM RW Code Fail !!\n");
		return USB_STOR_TRANSPORT_ERROR;
	}

	addr = (WORD)Media.Zone*Ssfdc.MaxBlocks+Media.PhyBlock;
	addr = addr*(WORD)Ssfdc.MaxSectors+Media.Sector;

	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength	= 0x10;
	bcb->Flags			= 0x80;
	bcb->CDB[0]			= 0xF1;
	bcb->CDB[1]			= 0x03;
	bcb->CDB[4]			= (BYTE)addr;
	bcb->CDB[3]			= (BYTE)(addr/0x0100);
	bcb->CDB[2]			= Media.Zone/2;
	bcb->CDB[8]			= 0;
	bcb->CDB[9]			= 1;

	buf = kmalloc(0x10, GFP_KERNEL);
	//result = ENE_SendScsiCmd(us, FDIR_READ, redundant, 0);
	result = ENE_SendScsiCmd(us, FDIR_READ, buf, 0);
	memcpy(redundant, buf, 0x10);
	kfree(buf);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	return USB_STOR_TRANSPORT_GOOD;
}

// 6250 CMD 4
//----- Ssfdc_D_WriteRedtData() ----------------------------------------
int Ssfdc_D_WriteRedtData(struct us_data *us, BYTE *redundant)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	int	result;
	//PBULK_CBW               pBulkCbw = fdoExt->pBulkCbw;
	//NTSTATUS                ntStatus;
	WORD                    addr;

	result = ENE_LoadBinCode(us, SM_RW_PATTERN);
	if (result != USB_STOR_XFER_GOOD)
	{
		printk("Load SM RW Code Fail !!\n");
		return USB_STOR_TRANSPORT_ERROR;
	}

	addr = (WORD)Media.Zone*Ssfdc.MaxBlocks+Media.PhyBlock;
	addr = addr*(WORD)Ssfdc.MaxSectors+Media.Sector;

	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength	= 0x10;
	bcb->Flags			= 0x80;
	bcb->CDB[0]			= 0xF2;
	bcb->CDB[1]			= 0x05;
	bcb->CDB[7]			= (BYTE)addr;
	bcb->CDB[6]			= (BYTE)(addr/0x0100);
	bcb->CDB[5]			= Media.Zone/2;
	bcb->CDB[8]			= *(redundant+REDT_ADDR1H);
	bcb->CDB[9]			= *(redundant+REDT_ADDR1L);

	result = ENE_SendScsiCmd(us, FDIR_READ, NULL, 0);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	return USB_STOR_TRANSPORT_GOOD;
}

//----- Ssfdc_D_CheckStatus() ------------------------------------------
int Ssfdc_D_CheckStatus(void)
{
    // Driver 不做
    return(SUCCESS);
    //_Set_D_SsfdcRdCmd(RDSTATUS);
    //
    //if (_Check_D_SsfdcStatus())
    //{ _Set_D_SsfdcRdStandby(); return(ERROR); }
    //
    //_Set_D_SsfdcRdStandby();
    //return(SUCCESS);
}
/*
////NAND Memory (SmartMedia) Control Subroutine for Read Data
////----- _Set_D_SsfdcRdCmd() --------------------------------------------
//void _Set_D_SsfdcRdCmd(BYTE cmd)
//{
//    _Hw_D_SetRdCmd();
//    _Hw_D_OutData(cmd);
//    _Hw_D_SetRdData();
//}
//
////----- _Set_D_SsfdcRdAddr() -------------------------------------------
//void _Set_D_SsfdcRdAddr(BYTE add)
//{
//    WORD addr;
//    SSFDCTYPE_T aa = (SSFDCTYPE_T ) &Ssfdc;
//    ADDRESS_T   bb = (ADDRESS_T) &Media;
//
//    addr=(WORD)Media.Zone*Ssfdc.MaxBlocks+Media.PhyBlock;
//    addr=addr*(WORD)Ssfdc.MaxSectors+Media.Sector;
//
//    //if ((Ssfdc.Attribute &MPS)==PS256) // for 256byte/page
//    //    addr=addr*2+(WORD)add;
//
//    _Hw_D_SetRdAddr();
//    _Hw_D_OutData(0x00);
//    _Hw_D_OutData((BYTE)addr);
//    _Hw_D_OutData((BYTE)(addr/0x0100));
//
//    if ((Ssfdc.Attribute &MADC)==AD4CYC)
//        _Hw_D_OutData((BYTE)(Media.Zone/2)); // Patch
//
//    _Hw_D_SetRdData();
//}
//
////----- _Set_D_SsfdcRdChip() -------------------------------------------
//void _Set_D_SsfdcRdChip(void)
//{
//    _Hw_D_SetRdAddr();
//    _Hw_D_OutData(0x00);
//    _Hw_D_SetRdData();
//}
//
////----- _Set_D_SsfdcRdStandby() ----------------------------------------
//void _Set_D_SsfdcRdStandby(void)
//{
//    _Hw_D_SetRdStandby();
//}
//
////----- _Start_D_SsfdcRdHwECC() ----------------------------------------
//void _Start_D_SsfdcRdHwECC(void)
//{
//#ifdef HW_ECC_SUPPORTED
//    _Hw_D_EccRdReset();
//    _Hw_D_InData();
//    _Hw_D_EccRdStart();
//#endif
//}
//
////----- _Stop_D_SsfdcRdHwECC() -----------------------------------------
//void _Stop_D_SsfdcRdHwECC(void)
//{
//#ifdef HW_ECC_SUPPORTED
//    _Hw_D_EccRdStop();
//#endif
//}
//
////----- _Load_D_SsfdcRdHwECC() -----------------------------------------
//void _Load_D_SsfdcRdHwECC(BYTE add)
//{
//#ifdef HW_ECC_SUPPORTED
//    _Hw_D_EccRdRead();
//    //if (!(add==ODD && (Ssfdc.Attribute &MPS)==PS256))
//    {
//        EccBuf[0]=_Hw_D_InData();
//        EccBuf[1]=_Hw_D_InData();
//        EccBuf[2]=_Hw_D_InData();
//    }
//
//    //if (!(add==EVEN && (Ssfdc.Attribute &MPS)==PS256))
//    {
//        EccBuf[3]=_Hw_D_InData();
//        EccBuf[4]=_Hw_D_InData();
//        EccBuf[5]=_Hw_D_InData();
//    }
//
//    _Hw_D_EccRdStop();
//#endif
//}
//
////NAND Memory (SmartMedia) Control Subroutine for Write Data
//
////----- _Set_D_SsfdcWrCmd() -----------------------------------------
//void _Set_D_SsfdcWrCmd(BYTE cmd)
//{
//    _Hw_D_SetWrCmd();
//    _Hw_D_OutData(cmd);
//    _Hw_D_SetWrData();
//}
//
////----- _Set_D_SsfdcWrAddr() -----------------------------------------
//void _Set_D_SsfdcWrAddr(BYTE add)
//{
//    WORD addr;
//    SSFDCTYPE_T aa = (SSFDCTYPE_T ) &Ssfdc;
//    ADDRESS_T   bb = (ADDRESS_T) &Media;
//
//    addr=(WORD)Media.Zone*Ssfdc.MaxBlocks+Media.PhyBlock;
//    addr=addr*(WORD)Ssfdc.MaxSectors+Media.Sector;
//
//    //if ((Ssfdc.Attribute &MPS)==PS256) // for 256byte/page
//    //    addr=addr*2+(WORD)add;
//
//    _Hw_D_SetWrAddr();
//    _Hw_D_OutData(0x00);
//    _Hw_D_OutData((BYTE)addr);
//    _Hw_D_OutData((BYTE)(addr/0x0100));
//
//    if ((Ssfdc.Attribute &MADC)==AD4CYC)
//        _Hw_D_OutData((BYTE)(Media.Zone/2)); // Patch
//
//    _Hw_D_SetWrData();
//}
//
////----- _Set_D_SsfdcWrBlock() -----------------------------------------
//void _Set_D_SsfdcWrBlock(void)
//{
//    WORD addr;
//    SSFDCTYPE_T aa = (SSFDCTYPE_T ) &Ssfdc;
//    ADDRESS_T   bb = (ADDRESS_T) &Media;
//
//    addr=(WORD)Media.Zone*Ssfdc.MaxBlocks+Media.PhyBlock;
//    addr=addr*(WORD)Ssfdc.MaxSectors;
//
//    //if ((Ssfdc.Attribute &MPS)==PS256) // for 256byte/page
//    //    addr=addr*2;
//
//    _Hw_D_SetWrAddr();
//    _Hw_D_OutData((BYTE)addr);
//    _Hw_D_OutData((BYTE)(addr/0x0100));
//
//    if ((Ssfdc.Attribute &MADC)==AD4CYC)
//        _Hw_D_OutData((BYTE)(Media.Zone/2)); // Patch
//
//    _Hw_D_SetWrData();
//}
//
////----- _Set_D_SsfdcWrStandby() -----------------------------------------
//void _Set_D_SsfdcWrStandby(void)
//{
//    _Hw_D_SetWrStandby();
//}
//
////----- _Start_D_SsfdcWrHwECC() -----------------------------------------
//void _Start_D_SsfdcWrHwECC(void)
//{
//#ifdef HW_ECC_SUPPORTED
//    _Hw_D_EccWrReset();
//    _Hw_D_InData();
//    _Hw_D_EccWrStart();
//#endif
//}
//
////----- _Load_D_SsfdcWrHwECC() -----------------------------------------
//void _Load_D_SsfdcWrHwECC(BYTE add)
//{
//#ifdef HW_ECC_SUPPORTED
//    _Hw_D_EccWrRead();
//    //if (!(add==ODD && (Ssfdc.Attribute &MPS)==PS256))
//    {
//        EccBuf[0]=_Hw_D_InData();
//        EccBuf[1]=_Hw_D_InData();
//        EccBuf[2]=_Hw_D_InData();
//    }
//
//    //if (!(add==EVEN && (Ssfdc.Attribute &MPS)==PS256))
//    {
//        EccBuf[3]=_Hw_D_InData();
//        EccBuf[4]=_Hw_D_InData();
//        EccBuf[5]=_Hw_D_InData();
//    }
//
//    _Hw_D_EccWrStop();
//#endif
//}
//
////NAND Memory (SmartMedia) Control Subroutine
////----- _Check_D_SsfdcBusy() -------------------------------------------
//int _Check_D_SsfdcBusy(WORD time)
//{
//    WORD  count = 0;
//
//    do {
//        if (!_Hw_D_ChkBusy())
//            return(SUCCESS);
//        EDelay(100);
//        count++;
//    } while (count<=time);
//
//    return(ERROR);
//}
//
////----- _Check_D_SsfdcStatus() -----------------------------------------
//int _Check_D_SsfdcStatus(void)
//{
//    if (_Hw_D_InData() & WR_FAIL)
//        return(ERROR);
//
//    return(SUCCESS);
//}
//
//// For 712
////----- _Reset_D_SsfdcErr() -----------------------------------------
//void _Reset_D_SsfdcErr(void)
//{
//    WORD  count = 0;
//
//    _Hw_D_SetRdCmd();
//    _Hw_D_OutData(RST_CHIP);
//    _Hw_D_SetRdData();
//
//    do {
//        if (!_Hw_D_ChkBusy())
//            break;
//        EDelay(100);
//        count++;
//    } while (count<=BUSY_RESET);
//
//    _Hw_D_SetRdStandby();
//}
//
////NAND Memory (SmartMedia) Buffer Data Xfer Subroutine
////----- SM_ReadDataWithDMA() -----------------------------------------
//void SM_ReadDataWithDMA(PFDO_DEVICE_EXTENSION fdoExt, BYTE *databuf, WORD SectByteCount)
//{
//    PHYSICAL_ADDRESS        Addr;
//    LARGE_INTEGER           ptimeout ;
//
//    KeClearEvent(&fdoExt->SM_DMADoneEvent);
//
//    Addr = MmGetPhysicalAddress(databuf);
//
//    WRITE_PORT_DWORD(SM_DMA_ADDR_REG, (DWORD)Addr.LowPart);
//    WRITE_PORT_BYTE(SM_DMA_DATA_CTRL, 0);
//    WRITE_PORT_WORD(SM_DMA_BYTE_COUNT_REG, SectByteCount);
//
//    while ( _Hw_D_ChkCardIn() )
//    {
//        if ((READ_PORT_BYTE(SM_REG_FIFO_STATUS) & 0x80))
//           break;
//    }
//    if (!_Hw_D_ChkCardIn())      return;
//    WRITE_PORT_BYTE(SM_DMA_DATA_CTRL, 0x01);
//
//    ptimeout.QuadPart = 2000 * (-10000);                                    // 2 sec
//    KeWaitForSingleObject(&fdoExt->SM_DMADoneEvent, Executive, KernelMode, FALSE, &ptimeout);
//    _Hw_D_SetDMAIntMask();
//}
//
////----- SM_WriteDataWithDMA() -----------------------------------------
//void SM_WriteDataWithDMA(PFDO_DEVICE_EXTENSION fdoExt, BYTE *databuf, WORD SectByteCount)
//{
//    PHYSICAL_ADDRESS        Addr;
//    LARGE_INTEGER           ptimeout ;
//
//    KeClearEvent(&fdoExt->SM_DMADoneEvent);
//
//    Addr = MmGetPhysicalAddress(databuf);
//
//    WRITE_PORT_DWORD(SM_DMA_ADDR_REG, (DWORD)Addr.LowPart);
//    WRITE_PORT_BYTE(SM_DMA_DATA_CTRL, 2);
//    WRITE_PORT_WORD(SM_DMA_BYTE_COUNT_REG, SectByteCount);
//
//    while ( _Hw_D_ChkCardIn() )
//    {
//       if ((READ_PORT_BYTE(SM_REG_FIFO_STATUS) & 0x40))
//           break;
//    }
//    if (!_Hw_D_ChkCardIn())      return;
//    WRITE_PORT_BYTE(SM_DMA_DATA_CTRL, 0x03);
//
//    ptimeout.QuadPart = 2000 * (-10000);                                    // 2 sec
//    KeWaitForSingleObject(&fdoExt->SM_DMADoneEvent, Executive, KernelMode, FALSE, &ptimeout);
//    _Hw_D_SetDMAIntMask();
//}
//
////----- _Read_D_SsfdcBuf() -----------------------------------------
//void _Read_D_SsfdcBuf(BYTE *databuf)
//{
//    int i;
//
//    //for(i=0x0000;i<(((Ssfdc.Attribute &MPS)==PS256)?0x0100:0x0200);i++)
//    for(i=0; i<0x200; i++)
//        *databuf++ =_Hw_D_InData();
//}
//
////----- _Write_D_SsfdcBuf() -----------------------------------------
//void _Write_D_SsfdcBuf(BYTE *databuf)
//{
//    int i;
//
//    //for(i=0x0000;i<(((Ssfdc.Attribute &MPS)==PS256)?0x0100:0x0200);i++)
//    for(i=0; i<0x200; i++)
//        _Hw_D_OutData(*databuf++);
//}
//
////----- _Read_D_SsfdcByte() -----------------------------------------
//void _Read_D_SsfdcByte(BYTE *databuf)
//{
//    *databuf=(BYTE)_Hw_D_InData();
//}
//
////----- _ReadRedt_D_SsfdcBuf() -----------------------------------------
//void _ReadRedt_D_SsfdcBuf(BYTE *redundant)
//{
//    char i;
//
//    //for(i=0x00;i<(((Ssfdc.Attribute &MPS)==PS256)?0x08:0x10);i++)
//    for(i=0; i<0x10; i++)
//        redundant[i] =_Hw_D_InData();
//}
//
////----- _WriteRedt_D_SsfdcBuf() -----------------------------------------
//void _WriteRedt_D_SsfdcBuf(BYTE *redundant)
//{
//    char i;
//
//    //for(i=0x00;i<(((Ssfdc.Attribute &MPS)==PS256)?0x08:0x10);i++)
//    for(i=0; i<0x10; i++)
//        _Hw_D_OutData(*redundant++);
//}
*/
//SmartMedia ID Code Check & Mode Set Subroutine
//----- Set_D_SsfdcModel() ---------------------------------------------
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
            return(ERROR);
    }

    return(SUCCESS);
}

//----- _Check_D_DevCode() ---------------------------------------------
BYTE _Check_D_DevCode(BYTE dcode)
{
    switch(dcode){
        case 0x6E:
        case 0xE8:
        case 0xEC: return(SSFDC1MB);   // 8Mbit (1M) NAND
        case 0x64:
        case 0xEA: return(SSFDC2MB);   // 16Mbit (2M) NAND
        case 0x6B:
        case 0xE3:
        case 0xE5: return(SSFDC4MB);   // 32Mbit (4M) NAND
        case 0xE6: return(SSFDC8MB);   // 64Mbit (8M) NAND
        case 0x73: return(SSFDC16MB);  // 128Mbit (16M)NAND
        case 0x75: return(SSFDC32MB);  // 256Mbit (32M)NAND
        case 0x76: return(SSFDC64MB);  // 512Mbit (64M)NAND
        case 0x79: return(SSFDC128MB); // 1Gbit(128M)NAND
        case 0x71: return(SSFDC256MB);
        case 0xDC: return(SSFDC512MB);
        case 0xD3: return(SSFDC1GB);
        case 0xD5: return(SSFDC2GB);
        default: return(NOSSFDC);
    }
}
/*
////SmartMedia Power Control Subroutine
////----- Cnt_D_Reset() ----------------------------------------------
//void Cnt_D_Reset(void)
//{
//    _Hw_D_LedOff();
//    _Hw_D_SetRdStandby();
//    _Hw_D_VccOff();
//}
//
////----- Cnt_D_PowerOn() ----------------------------------------------
//int Cnt_D_PowerOn(void)
//{
//    // No support 5V.
//    _Hw_D_EnableVcc3VOn();                      // Set SM_REG_CTRL_5 Reg. to 3V
//    _Hw_D_VccOn();
//    _Hw_D_SetRdStandby();
//    _Wait_D_Timer(TIME_PON);
//
//    if (_Hw_D_ChkPower())
//    {
//        _Hw_D_EnableOB();                       // Set SM_REG_CTRL_5 Reg. to 0x83
//        return(SUCCESS);
//    }
//
//    _Hw_D_SetVccOff();
//    return(ERROR);
//}
//
////----- Cnt_D_PowerOff() ----------------------------------------------
//void Cnt_D_PowerOff(void)
//{
//    _Hw_D_SetRdStandby();
//    _Hw_D_SetVccOff();
//    _Hw_D_VccOff();
//}
//
////----- Cnt_D_LedOn() ----------------------------------------------
//void Cnt_D_LedOn(void)
//{
//    _Hw_D_LedOn();
//}
//
////----- Cnt_D_LedOff() ----------------------------------------------
//void Cnt_D_LedOff(void)
//{
//    _Hw_D_LedOff();
//}
//
////----- Check_D_CntPower() ----------------------------------------------
//int Check_D_CntPower(void)
//{
//    if (_Hw_D_ChkPower())
//        return(SUCCESS); // Power On
//
//    return(ERROR);       // Power Off
//}
//
////----- Check_D_CardExist() ----------------------------------------------
//int Check_D_CardExist(void)
//{
//    char i,j,k;
//
//    if (!_Hw_D_ChkStatus()) // Not Status Change
//        if (_Hw_D_ChkCardIn())
//            return(SUCCESS); // Card exist in Slot
//
//    for(i=0,j=0,k=0; i<16; i++) {
//        if (_Hw_D_ChkCardIn()) // Status Change
//        {
//            j++; k=0;
//        }
//        else
//        {
//            j=0; k++;
//        }
//
//        if (j>3)
//            return(SUCCESS); // Card exist in Slot
//        if (k>3)
//            return(ERROR); // NO Card exist in Slot
//
//        _Wait_D_Timer(TIME_CDCHK);
//    }
//
//    return(ERROR);
//}
//
////----- Check_D_CardStsChg() ----------------------------------------------
//int Check_D_CardStsChg(void)
//{
//    if (_Hw_D_ChkStatus())
//        return(ERROR); // Status Change
//
//    return(SUCCESS);   // Not Status Change
//}
//
////----- Check_D_SsfdcWP() ----------------------------------------------
//int Check_D_SsfdcWP(void)
//{ // ERROR: WP, SUCCESS: Not WP
//    char i;
//
//    for(i=0; i<8; i++) {
//        if (_Hw_D_ChkWP())
//            return(ERROR);
//        _Wait_D_Timer(TIME_WPCHK);
//    }
//
//    return(SUCCESS);
//}
//
*/
//SmartMedia ECC Control Subroutine
//----- Check_D_ReadError() ----------------------------------------------
int Check_D_ReadError(BYTE *redundant)
{
	return SUCCESS;
}

//----- Check_D_Correct() ----------------------------------------------
int Check_D_Correct(BYTE *buf,BYTE *redundant)
{
	return SUCCESS;
}

//----- Check_D_CISdata() ----------------------------------------------
int Check_D_CISdata(BYTE *buf, BYTE *redundant)
{
	BYTE cis[] = {0x01, 0x03, 0xD9, 0x01, 0xFF, 0x18, 0x02,
		      0xDF, 0x01, 0x20};

	int cis_len = sizeof(cis);

	if (!IsSSFDCCompliance && !IsXDCompliance)
		return SUCCESS;

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

//----- Set_D_RightECC() ----------------------------------------------
void Set_D_RightECC(BYTE *redundant)
{
    // Driver 不做 ECC Check
    return;
    //StringCopy((char *)(redundant+0x0D),(char *)EccBuf,3);
    //StringCopy((char *)(redundant+0x08),(char *)(EccBuf+0x03),3);
}
/*
////----- _Calc_D_ECCdata() ----------------------------------------------
//void _Calc_D_ECCdata(BYTE *buf)
//{
//#ifdef HW_ECC_SUPPORTED
//#else
//    _Calculate_D_SwECC(buf,EccBuf);
//    buf+=0x0100;
//    _Calculate_D_SwECC(buf,EccBuf+0x03);
//#endif
//}
//
////----- _Set_D_ECCdata() ----------------------------------------------
//void _Set_D_ECCdata(BYTE add,BYTE *redundant)
//{
//    //if (add==EVEN && (Ssfdc.Attribute &MPS)==PS256)
//    //    return;
//
//    // for 256byte/page
//    StringCopy((char *)(redundant+0x0D),(char *)EccBuf,3);
//    StringCopy((char *)(redundant+0x08),(char *)(EccBuf+0x03),3);
//}
*/

/*
//----- SM_ReadBlock() ---------------------------------------------
int SM_ReadBlock(PFDO_DEVICE_EXTENSION fdoExt, BYTE *buf,BYTE *redundant)
{
    PBULK_CBW               pBulkCbw = fdoExt->pBulkCbw;
    NTSTATUS                ntStatus;
    WORD                    addr;

    ENE_LoadBinCode(fdoExt, SM_RW_PATTERN);

    addr = (WORD)Media.Zone*Ssfdc.MaxBlocks+Media.PhyBlock;
    addr = addr*(WORD)Ssfdc.MaxSectors+Media.Sector;

    // Read sect data
    RtlZeroMemory(pBulkCbw, sizeof(struct _BULK_CBW));
    pBulkCbw->dCBWSignature          = CBW_SIGNTURE;
    pBulkCbw->bCBWLun                = CBW_LUN;
    pBulkCbw->dCBWDataTransferLength = 0x200;
    pBulkCbw->bmCBWFlags             = 0x80;
    pBulkCbw->CBWCb[0]               = 0xF1;
    pBulkCbw->CBWCb[1]               = 0x02;
    pBulkCbw->CBWCb[4]               = (BYTE)addr;
    pBulkCbw->CBWCb[3]               = (BYTE)(addr/0x0100);
    pBulkCbw->CBWCb[2]               = Media.Zone/2;

    ntStatus = ENE_SendScsiCmd(fdoExt, FDIR_READ, buf);

    if (!NT_SUCCESS(ntStatus))
       return(ERROR);

    // Read redundant
    RtlZeroMemory(pBulkCbw, sizeof(struct _BULK_CBW));
    pBulkCbw->dCBWSignature          = CBW_SIGNTURE;
    pBulkCbw->bCBWLun                = CBW_LUN;
    pBulkCbw->dCBWDataTransferLength = 0x10;
    pBulkCbw->bmCBWFlags             = 0x80;
    pBulkCbw->CBWCb[0]               = 0xF1;
    pBulkCbw->CBWCb[1]               = 0x03;
    pBulkCbw->CBWCb[4]               = (BYTE)addr;
    pBulkCbw->CBWCb[3]               = (BYTE)(addr/0x0100);
    pBulkCbw->CBWCb[2]               = Media.Zone/2;
    pBulkCbw->CBWCb[5]               = 0;
    pBulkCbw->CBWCb[6]               = 1;

    ntStatus = ENE_SendScsiCmd(fdoExt, FDIR_READ, redundant);

    if (!NT_SUCCESS(ntStatus))
       return(ERROR);

    return(SUCCESS);
}*/
