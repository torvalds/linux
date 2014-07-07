#include <linux/slab.h>
#include "usb.h"
#include "scsiglue.h"
#include "smcommon.h"
#include "smil.h"

static int         Conv_D_MediaAddr(struct us_data *, u32);
static int         Inc_D_MediaAddr(struct us_data *);
static int         Media_D_ReadOneSect(struct us_data *, u16, u8 *);

static int  Copy_D_BlockAll(struct us_data *, u32);

static int  Assign_D_WriteBlock(void);
static int  Release_D_ReadBlock(struct us_data *);
static int  Release_D_WriteBlock(struct us_data *);
static int  Release_D_CopySector(struct us_data *);

static int  Copy_D_PhyOneSect(struct us_data *);
static int  Read_D_PhyOneSect(struct us_data *, u16, u8 *);
static int  Erase_D_PhyOneBlock(struct us_data *);

static int  Set_D_PhyFmtValue(struct us_data *);
static int  Search_D_CIS(struct us_data *);
static int  Make_D_LogTable(struct us_data *);

static int  MarkFail_D_PhyOneBlock(struct us_data *);

static u32 ErrCode;
static u8  WorkBuf[SECTSIZE];
static u8  Redundant[REDTSIZE];
static u8  WorkRedund[REDTSIZE];
/* 128 x 1000, Log2Phy[MAX_ZONENUM][MAX_LOGBLOCK]; */
static u16  *Log2Phy[MAX_ZONENUM];
static u8  Assign[MAX_ZONENUM][MAX_BLOCKNUM / 8];
static u16  AssignStart[MAX_ZONENUM];
u16  ReadBlock;
u16  WriteBlock;
u32 MediaChange;
static u32 SectCopyMode;

/* BIT Control Macro */
static u8 BitData[] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 };
#define Set_D_Bit(a, b)    (a[(u8)((b) / 8)] |= BitData[(b) % 8])
#define Clr_D_Bit(a, b)    (a[(u8)((b) / 8)] &= ~BitData[(b) % 8])
#define Chk_D_Bit(a, b)    (a[(u8)((b) / 8)] & BitData[(b) % 8])

/* ----- SM_FreeMem() ------------------------------------------------- */
int SM_FreeMem(void)
{
	int	i;

	pr_info("SM_FreeMem start\n");
	for (i = 0; i < MAX_ZONENUM; i++) {
		if (Log2Phy[i] != NULL) {
			pr_info("Free Zone = %x, Addr = %p\n", i, Log2Phy[i]);
			kfree(Log2Phy[i]);
			Log2Phy[i] = NULL;
		}
	}
	return NO_ERROR;
}

/* SmartMedia Read/Write/Erase Function */
/* ----- Media_D_ReadSector() ------------------------------------------- */
int Media_D_ReadSector(struct us_data *us, u32 start, u16 count, u8 *buf)
{
	u16 len, bn;

	if (Conv_D_MediaAddr(us, start))
		return ErrCode;

	while (1) {
		len = Ssfdc.MaxSectors - Media.Sector;
		if (count > len)
			bn = len;
		else
			bn = count;

		if (Media_D_ReadOneSect(us, bn, buf)) {
			ErrCode = ERR_EccReadErr;
			return ErrCode;
		}

		Media.Sector += bn;
		count -= bn;

		if (count <= 0)
			break;

		buf += bn * SECTSIZE;

		if (Inc_D_MediaAddr(us))
			return ErrCode;
	}

	return NO_ERROR;
}
/* here */
/* ----- Media_D_CopySector() ------------------------------------------ */
int Media_D_CopySector(struct us_data *us, u32 start, u16 count, u8 *buf)
{
	u16 len, bn;

	/* pr_info("Media_D_CopySector !!!\n"); */
	if (Conv_D_MediaAddr(us, start))
		return ErrCode;

	while (1) {
		if (Assign_D_WriteBlock())
			return ERROR;

		len = Ssfdc.MaxSectors - Media.Sector;
		if (count > len)
			bn = len;
		else
		bn = count;

		if (Ssfdc_D_CopyBlock(us, bn, buf, Redundant)) {
			ErrCode = ERR_WriteFault;
			return ErrCode;
		}

		Media.Sector = 0x1F;
		if (Release_D_CopySector(us)) {
			if (ErrCode == ERR_HwError) {
				ErrCode = ERR_WriteFault;
				return ErrCode;
			}
		}
		count -= bn;

		if (count <= 0)
			break;

		buf += bn * SECTSIZE;

		if (Inc_D_MediaAddr(us))
			return ErrCode;

	}
	return NO_ERROR;
}

/* SmartMedia Physical Format Test Subroutine */
/* ----- Check_D_MediaFmt() --------------------------------------------- */
int Check_D_MediaFmt(struct us_data *us)
{
	pr_info("Check_D_MediaFmt\n");

	if (!MediaChange)
		return SMSUCCESS;

	MediaChange  = ERROR;
	SectCopyMode = COMPLETED;

	if (Set_D_PhyFmtValue(us)) {
		ErrCode = ERR_UnknownMedia;
		return ERROR;
	}

	if (Search_D_CIS(us)) {
		ErrCode = ERR_IllegalFmt;
		return ERROR;
	}

	MediaChange = SMSUCCESS;
	return SMSUCCESS;
}

/* ----- Release_D_CopySector() ------------------------------------------ */
static int Release_D_CopySector(struct us_data *us)
{
	Log2Phy[Media.Zone][Media.LogBlock] = WriteBlock;
	Media.PhyBlock = ReadBlock;

	if (Media.PhyBlock == NO_ASSIGN) {
		Media.PhyBlock = WriteBlock;
		return SMSUCCESS;
	}

	Clr_D_Bit(Assign[Media.Zone], Media.PhyBlock);
	Media.PhyBlock = WriteBlock;

	return SMSUCCESS;
}

/* SmartMedia Physical Address Control Subroutine */
/* ----- Conv_D_MediaAddr() --------------------------------------------- */
static int Conv_D_MediaAddr(struct us_data *us, u32 addr)
{
	u32 temp;

	temp           = addr / Ssfdc.MaxSectors;
	Media.Zone     = (u8) (temp / Ssfdc.MaxLogBlocks);

	if (Log2Phy[Media.Zone] == NULL) {
		if (Make_D_LogTable(us)) {
			ErrCode = ERR_IllegalFmt;
			return ERROR;
		}
	}

	Media.Sector   = (u8) (addr % Ssfdc.MaxSectors);
	Media.LogBlock = (u16) (temp % Ssfdc.MaxLogBlocks);

	if (Media.Zone < Ssfdc.MaxZones) {
		Clr_D_RedundantData(Redundant);
		Set_D_LogBlockAddr(Redundant);
		Media.PhyBlock = Log2Phy[Media.Zone][Media.LogBlock];
		return SMSUCCESS;
	}

	ErrCode = ERR_OutOfLBA;
	return ERROR;
}

/* ----- Inc_D_MediaAddr() ---------------------------------------------- */
static int Inc_D_MediaAddr(struct us_data *us)
{
	u16        LogBlock = Media.LogBlock;

	if (++Media.Sector < Ssfdc.MaxSectors)
		return SMSUCCESS;

	if (Log2Phy[Media.Zone] == NULL) {
		if (Make_D_LogTable(us)) {
			ErrCode = ERR_IllegalFmt;
			return ERROR;
		}
	}

	Media.Sector = 0;
	Media.LogBlock = LogBlock;

	if (++Media.LogBlock < Ssfdc.MaxLogBlocks) {
		Clr_D_RedundantData(Redundant);
		Set_D_LogBlockAddr(Redundant);
		Media.PhyBlock = Log2Phy[Media.Zone][Media.LogBlock];
		return SMSUCCESS;
	}

	Media.LogBlock = 0;

	if (++Media.Zone < Ssfdc.MaxZones) {
		if (Log2Phy[Media.Zone] == NULL) {
			if (Make_D_LogTable(us)) {
				ErrCode = ERR_IllegalFmt;
				return ERROR;
			}
		}

		Media.LogBlock = 0;

		Clr_D_RedundantData(Redundant);
		Set_D_LogBlockAddr(Redundant);
		Media.PhyBlock = Log2Phy[Media.Zone][Media.LogBlock];
		return SMSUCCESS;
	}

	Media.Zone = 0;
	ErrCode = ERR_OutOfLBA;

	return ERROR;
}

/* SmartMedia Read/Write Subroutine with Retry */
/* ----- Media_D_ReadOneSect() ------------------------------------------ */
static int Media_D_ReadOneSect(struct us_data *us, u16 count, u8 *buf)
{
	u32 err, retry;

	if (!Read_D_PhyOneSect(us, count, buf))
		return SMSUCCESS;
	if (ErrCode == ERR_HwError)
		return ERROR;
	if (ErrCode == ERR_DataStatus)
		return ERROR;

#ifdef RDERR_REASSIGN
	if (Ssfdc.Attribute & MWP) {
		if (ErrCode == ERR_CorReadErr)
			return SMSUCCESS;
		return ERROR;
	}

	err = ErrCode;
	for (retry = 0; retry < 2; retry++) {
		if (Copy_D_BlockAll(us,
			(err == ERR_EccReadErr) ? REQ_FAIL : REQ_ERASE)) {
			if (ErrCode == ERR_HwError)
				return ERROR;
			continue;
		}

		ErrCode = err;
		if (ErrCode == ERR_CorReadErr)
			return SMSUCCESS;
		return ERROR;
	}

	MediaChange = ERROR;
#else
	if (ErrCode == ERR_CorReadErr)
		return SMSUCCESS;
#endif

	return ERROR;
}

/* SmartMedia Physical Sector Data Copy Subroutine */
/* ----- Copy_D_BlockAll() ---------------------------------------------- */
static int Copy_D_BlockAll(struct us_data *us, u32 mode)
{
	u8 sect;

	sect = Media.Sector;

	if (Assign_D_WriteBlock())
		return ERROR;
	if (mode == REQ_FAIL)
		SectCopyMode = REQ_FAIL;

	for (Media.Sector = 0; Media.Sector < Ssfdc.MaxSectors;
							Media.Sector++) {
		if (Copy_D_PhyOneSect(us)) {
			if (ErrCode == ERR_HwError)
				return ERROR;
			if (Release_D_WriteBlock(us))
				return ERROR;

			ErrCode = ERR_WriteFault;
			Media.PhyBlock = ReadBlock;
			Media.Sector = sect;

			return ERROR;
		}
	}

	if (Release_D_ReadBlock(us))
		return ERROR;

	Media.PhyBlock = WriteBlock;
	Media.Sector = sect;
	return SMSUCCESS;
}

/* SmartMedia Physical Block Assign/Release Subroutine */
/* ----- Assign_D_WriteBlock() ------------------------------------------ */
static int Assign_D_WriteBlock(void)
{
	ReadBlock = Media.PhyBlock;

	for (WriteBlock = AssignStart[Media.Zone];
			WriteBlock < Ssfdc.MaxBlocks; WriteBlock++) {
		if (!Chk_D_Bit(Assign[Media.Zone], WriteBlock)) {
			Set_D_Bit(Assign[Media.Zone], WriteBlock);
			AssignStart[Media.Zone] = WriteBlock + 1;
			Media.PhyBlock = WriteBlock;
			SectCopyMode = REQ_ERASE;
			return SMSUCCESS;
		}
	}

	for (WriteBlock = 0;
			WriteBlock < AssignStart[Media.Zone]; WriteBlock++) {
		if (!Chk_D_Bit(Assign[Media.Zone], WriteBlock)) {
			Set_D_Bit(Assign[Media.Zone], WriteBlock);
			AssignStart[Media.Zone] = WriteBlock + 1;
			Media.PhyBlock = WriteBlock;
			SectCopyMode = REQ_ERASE;
			return SMSUCCESS;
		}
	}

	WriteBlock = NO_ASSIGN;
	ErrCode = ERR_WriteFault;

	return ERROR;
}

/* ----- Release_D_ReadBlock() ------------------------------------------ */
static int Release_D_ReadBlock(struct us_data *us)
{
	u32 mode;

	mode = SectCopyMode;
	SectCopyMode = COMPLETED;

	if (mode == COMPLETED)
		return SMSUCCESS;

	Log2Phy[Media.Zone][Media.LogBlock] = WriteBlock;
	Media.PhyBlock = ReadBlock;

	if (Media.PhyBlock == NO_ASSIGN) {
		Media.PhyBlock = WriteBlock;
		return SMSUCCESS;
	}

	if (mode == REQ_ERASE) {
		if (Erase_D_PhyOneBlock(us)) {
			if (ErrCode == ERR_HwError)
				return ERROR;
			if (MarkFail_D_PhyOneBlock(us))
				return ERROR;
		} else
			Clr_D_Bit(Assign[Media.Zone], Media.PhyBlock);
	} else if (MarkFail_D_PhyOneBlock(us))
		return ERROR;

	Media.PhyBlock = WriteBlock;
	return SMSUCCESS;
}

/* ----- Release_D_WriteBlock() ----------------------------------------- */
static int Release_D_WriteBlock(struct us_data *us)
{
	SectCopyMode = COMPLETED;
	Media.PhyBlock = WriteBlock;

	if (MarkFail_D_PhyOneBlock(us))
		return ERROR;

	Media.PhyBlock = ReadBlock;
	return SMSUCCESS;
}

/* SmartMedia Physical Sector Data Copy Subroutine */
/* ----- Copy_D_PhyOneSect() -------------------------------------------- */
static int Copy_D_PhyOneSect(struct us_data *us)
{
	int           i;
	u32  err, retry;

	/* pr_info("Copy_D_PhyOneSect --- Sector = %x\n", Media.Sector); */
	if (ReadBlock != NO_ASSIGN) {
		Media.PhyBlock = ReadBlock;
		for (retry = 0; retry < 2; retry++) {
			if (retry != 0) {
				Ssfdc_D_Reset(us);
				if (Ssfdc_D_ReadCisSect(us, WorkBuf,
								WorkRedund)) {
					ErrCode = ERR_HwError;
					MediaChange = ERROR;
					return ERROR;
				}

				if (Check_D_CISdata(WorkBuf, WorkRedund)) {
					ErrCode = ERR_HwError;
					MediaChange = ERROR;
					return ERROR;
				}
			}

			if (Ssfdc_D_ReadSect(us, WorkBuf, WorkRedund)) {
				ErrCode = ERR_HwError;
				MediaChange = ERROR;
				return ERROR;
			}
			if (Check_D_DataStatus(WorkRedund)) {
				err = ERROR;
				break;
			}
			if (!Check_D_ReadError(WorkRedund)) {
				err = SMSUCCESS;
				break;
			}
			if (!Check_D_Correct(WorkBuf, WorkRedund)) {
				err = SMSUCCESS;
				break;
			}

			err = ERROR;
			SectCopyMode = REQ_FAIL;
		}
	} else {
		err = SMSUCCESS;
		for (i = 0; i < SECTSIZE; i++)
			WorkBuf[i] = DUMMY_DATA;
		Clr_D_RedundantData(WorkRedund);
	}

	Set_D_LogBlockAddr(WorkRedund);
	if (err == ERROR) {
		Set_D_RightECC(WorkRedund);
		Set_D_DataStaus(WorkRedund);
	}

	Media.PhyBlock = WriteBlock;

	if (Ssfdc_D_WriteSectForCopy(us, WorkBuf, WorkRedund)) {
		ErrCode = ERR_HwError;
		MediaChange = ERROR;
		return ERROR;
	}
	if (Ssfdc_D_CheckStatus()) {
		ErrCode = ERR_WriteFault;
		return ERROR;
	}

	Media.PhyBlock = ReadBlock;
	return SMSUCCESS;
}

/* SmartMedia Physical Sector Read/Write/Erase Subroutine */
/* ----- Read_D_PhyOneSect() -------------------------------------------- */
static int Read_D_PhyOneSect(struct us_data *us, u16 count, u8 *buf)
{
	int           i;
	u32  retry;

	if (Media.PhyBlock == NO_ASSIGN) {
		for (i = 0; i < SECTSIZE; i++)
			*buf++ = DUMMY_DATA;
		return SMSUCCESS;
	}

	for (retry = 0; retry < 2; retry++) {
		if (retry != 0) {
			Ssfdc_D_Reset(us);

			if (Ssfdc_D_ReadCisSect(us, WorkBuf, WorkRedund)) {
				ErrCode = ERR_HwError;
				MediaChange = ERROR;
				return ERROR;
			}
			if (Check_D_CISdata(WorkBuf, WorkRedund)) {
				ErrCode = ERR_HwError;
				MediaChange = ERROR;
				return ERROR;
			}
		}

		if (Ssfdc_D_ReadBlock(us, count, buf, Redundant)) {
			ErrCode = ERR_HwError;
			MediaChange = ERROR;
			return ERROR;
		}
		if (Check_D_DataStatus(Redundant)) {
			ErrCode = ERR_DataStatus;
			return ERROR;
		}

		if (!Check_D_ReadError(Redundant))
			return SMSUCCESS;

		if (!Check_D_Correct(buf, Redundant)) {
			ErrCode = ERR_CorReadErr;
			return ERROR;
		}
	}

	ErrCode = ERR_EccReadErr;
	return ERROR;
}

/* ----- Erase_D_PhyOneBlock() ------------------------------------------ */
static int Erase_D_PhyOneBlock(struct us_data *us)
{
	if (Ssfdc_D_EraseBlock(us)) {
		ErrCode = ERR_HwError;
		MediaChange = ERROR;
		return ERROR;
	}
	if (Ssfdc_D_CheckStatus()) {
		ErrCode = ERR_WriteFault;
		return ERROR;
	}

	return SMSUCCESS;
}

/* SmartMedia Physical Format Check Local Subroutine */
/* ----- Set_D_PhyFmtValue() -------------------------------------------- */
static int Set_D_PhyFmtValue(struct us_data *us)
{
	if (Set_D_SsfdcModel(us->SM_DeviceID))
		return ERROR;

	return SMSUCCESS;
}

/* ----- Search_D_CIS() ------------------------------------------------- */
static int Search_D_CIS(struct us_data *us)
{
	Media.Zone = 0;
	Media.Sector = 0;

	for (Media.PhyBlock = 0;
		Media.PhyBlock < (Ssfdc.MaxBlocks - Ssfdc.MaxLogBlocks - 1);
		Media.PhyBlock++) {
		if (Ssfdc_D_ReadRedtData(us, Redundant)) {
			Ssfdc_D_Reset(us);
			return ERROR;
		}

		if (!Check_D_FailBlock(Redundant))
			break;
	}

	if (Media.PhyBlock == (Ssfdc.MaxBlocks - Ssfdc.MaxLogBlocks - 1)) {
		Ssfdc_D_Reset(us);
		return ERROR;
	}

	while (Media.Sector < CIS_SEARCH_SECT) {
		if (Media.Sector) {
			if (Ssfdc_D_ReadRedtData(us, Redundant)) {
				Ssfdc_D_Reset(us);
				return ERROR;
			}
		}
		if (!Check_D_DataStatus(Redundant)) {
			if (Ssfdc_D_ReadSect(us, WorkBuf, Redundant)) {
				Ssfdc_D_Reset(us);
				return ERROR;
			}

			if (Check_D_CISdata(WorkBuf, Redundant)) {
				Ssfdc_D_Reset(us);
				return ERROR;
			}

			CisArea.PhyBlock = Media.PhyBlock;
			CisArea.Sector = Media.Sector;
			Ssfdc_D_Reset(us);
			return SMSUCCESS;
		}

		Media.Sector++;
	}

	Ssfdc_D_Reset(us);
	return ERROR;
}

/* ----- Make_D_LogTable() ---------------------------------------------- */
static int Make_D_LogTable(struct us_data *us)
{
	u16  phyblock, logblock;

	if (Log2Phy[Media.Zone] == NULL) {
		Log2Phy[Media.Zone] = kmalloc(MAX_LOGBLOCK * sizeof(u16),
								GFP_KERNEL);
		/* pr_info("ExAllocatePool Zone = %x, Addr = %x\n",
				Media.Zone, Log2Phy[Media.Zone]); */
		if (Log2Phy[Media.Zone] == NULL)
			return ERROR;
	}

	Media.Sector = 0;

	/* pr_info("Make_D_LogTable --- MediaZone = 0x%x\n",
						Media.Zone); */
	for (Media.LogBlock = 0; Media.LogBlock < Ssfdc.MaxLogBlocks;
						Media.LogBlock++)
		Log2Phy[Media.Zone][Media.LogBlock] = NO_ASSIGN;

	for (Media.PhyBlock = 0; Media.PhyBlock < (MAX_BLOCKNUM / 8);
						Media.PhyBlock++)
		Assign[Media.Zone][Media.PhyBlock] = 0x00;

	for (Media.PhyBlock = 0; Media.PhyBlock < Ssfdc.MaxBlocks;
						Media.PhyBlock++) {
		if ((!Media.Zone) && (Media.PhyBlock <= CisArea.PhyBlock)) {
			Set_D_Bit(Assign[Media.Zone], Media.PhyBlock);
			continue;
		}

		if (Ssfdc_D_ReadRedtData(us, Redundant)) {
			Ssfdc_D_Reset(us);
			return ERROR;
		}

		if (!Check_D_DataBlank(Redundant))
			continue;

		Set_D_Bit(Assign[Media.Zone], Media.PhyBlock);

		if (Check_D_FailBlock(Redundant))
			continue;

		if (Load_D_LogBlockAddr(Redundant))
			continue;

		if (Media.LogBlock >= Ssfdc.MaxLogBlocks)
			continue;

		if (Log2Phy[Media.Zone][Media.LogBlock] == NO_ASSIGN) {
			Log2Phy[Media.Zone][Media.LogBlock] = Media.PhyBlock;
			continue;
		}

		phyblock     = Media.PhyBlock;
		logblock     = Media.LogBlock;
		Media.Sector = (u8)(Ssfdc.MaxSectors - 1);

		if (Ssfdc_D_ReadRedtData(us, Redundant)) {
			Ssfdc_D_Reset(us);
			return ERROR;
		}

		if (!Load_D_LogBlockAddr(Redundant) &&
				(Media.LogBlock == logblock)) {
			Media.PhyBlock = Log2Phy[Media.Zone][logblock];

			if (Ssfdc_D_ReadRedtData(us, Redundant)) {
				Ssfdc_D_Reset(us);
				return ERROR;
			}

			Media.PhyBlock = phyblock;

			if (!Load_D_LogBlockAddr(Redundant)) {
				if (Media.LogBlock != logblock) {
					Media.PhyBlock =
						Log2Phy[Media.Zone][logblock];
					Log2Phy[Media.Zone][logblock] =
								phyblock;
				}
			} else {
				Media.PhyBlock = Log2Phy[Media.Zone][logblock];
				Log2Phy[Media.Zone][logblock] = phyblock;
			}
		}

		Media.Sector = 0;
		Media.PhyBlock = phyblock;

	AssignStart[Media.Zone] = 0;

	} /* End for (Media.Zone<MAX_ZONENUM) */

	Ssfdc_D_Reset(us);
	return SMSUCCESS;
}

/* ----- MarkFail_D_PhyOneBlock() --------------------------------------- */
static int MarkFail_D_PhyOneBlock(struct us_data *us)
{
	u8 sect;

	sect = Media.Sector;
	Set_D_FailBlock(WorkRedund);

	for (Media.Sector = 0; Media.Sector < Ssfdc.MaxSectors;
							Media.Sector++) {
		if (Ssfdc_D_WriteRedtData(us, WorkRedund)) {
			Ssfdc_D_Reset(us);
			Media.Sector   = sect;
			ErrCode        = ERR_HwError;
			MediaChange = ERROR;
			return ERROR;
		} /* NO Status Check */
	}

	Ssfdc_D_Reset(us);
	Media.Sector = sect;
	return SMSUCCESS;
}
