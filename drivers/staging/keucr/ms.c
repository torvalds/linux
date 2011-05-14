#include <linux/slab.h>
#include <asm/byteorder.h>

#include "usb.h"
#include "scsiglue.h"
#include "transport.h"
#include "ms.h"

//----- MS_ReaderCopyBlock() ------------------------------------------
int MS_ReaderCopyBlock(struct us_data *us, WORD oldphy, WORD newphy, WORD PhyBlockAddr, BYTE PageNum, PBYTE buf, WORD len)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	int	result;

	//printk("MS_ReaderCopyBlock --- PhyBlockAddr = %x, PageNum = %x\n", PhyBlockAddr, PageNum);
	result = ENE_LoadBinCode(us, MS_RW_PATTERN);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength = 0x200*len;
	bcb->Flags			= 0x00;
	bcb->CDB[0]			= 0xF0;
	bcb->CDB[1]			= 0x08;
	bcb->CDB[4]			= (BYTE)(oldphy);
	bcb->CDB[3]			= (BYTE)(oldphy>>8);
	bcb->CDB[2]			= (BYTE)(oldphy>>16);
	bcb->CDB[7]			= (BYTE)(newphy);
	bcb->CDB[6]			= (BYTE)(newphy>>8);
	bcb->CDB[5]			= (BYTE)(newphy>>16);
	bcb->CDB[9]			= (BYTE)(PhyBlockAddr);
	bcb->CDB[8]			= (BYTE)(PhyBlockAddr>>8);
	bcb->CDB[10]		= PageNum;

	result = ENE_SendScsiCmd(us, FDIR_WRITE, buf, 0);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	return USB_STOR_TRANSPORT_GOOD;
}

//----- MS_ReaderReadPage() ------------------------------------------
int MS_ReaderReadPage(struct us_data *us, DWORD PhyBlockAddr, BYTE PageNum, PDWORD PageBuf, MS_LibTypeExtdat *ExtraDat)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	int	result;
	BYTE	ExtBuf[4];
	DWORD	bn = PhyBlockAddr * 0x20 + PageNum;

	//printk("MS --- MS_ReaderReadPage,  PhyBlockAddr = %x, PageNum = %x\n", PhyBlockAddr, PageNum);

	result = ENE_LoadBinCode(us, MS_RW_PATTERN);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	// Read Page Data
	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength = 0x200;
	bcb->Flags			= 0x80;
	bcb->CDB[0]			= 0xF1;
	bcb->CDB[1]			= 0x02;
	bcb->CDB[5]			= (BYTE)(bn);
	bcb->CDB[4]			= (BYTE)(bn>>8);
	bcb->CDB[3]			= (BYTE)(bn>>16);
	bcb->CDB[2]			= (BYTE)(bn>>24);
	
	result = ENE_SendScsiCmd(us, FDIR_READ, PageBuf, 0);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	// Read Extra Data
	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength = 0x4;
	bcb->Flags			= 0x80;
	bcb->CDB[0]			= 0xF1;
	bcb->CDB[1]			= 0x03;
	bcb->CDB[5]			= (BYTE)(PageNum);
	bcb->CDB[4]			= (BYTE)(PhyBlockAddr);
	bcb->CDB[3]			= (BYTE)(PhyBlockAddr>>8);
	bcb->CDB[2]			= (BYTE)(PhyBlockAddr>>16);
	bcb->CDB[6]			= 0x01;

	result = ENE_SendScsiCmd(us, FDIR_READ, &ExtBuf, 0);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	ExtraDat->reserved = 0;
	ExtraDat->intr     = 0x80;  // Not yet, 安砞, 单 fireware support
	ExtraDat->status0  = 0x10;  // Not yet, 安砞, 单 fireware support
	ExtraDat->status1  = 0x00;  // Not yet, 安砞, 单 fireware support
	ExtraDat->ovrflg   = ExtBuf[0];
	ExtraDat->mngflg   = ExtBuf[1];
	ExtraDat->logadr   = MemStickLogAddr(ExtBuf[2], ExtBuf[3]);

	return USB_STOR_TRANSPORT_GOOD;
}

//----- MS_ReaderEraseBlock() ----------------------------------------
int MS_ReaderEraseBlock(struct us_data *us, DWORD PhyBlockAddr)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	int	result;
	DWORD	bn = PhyBlockAddr;

	//printk("MS --- MS_ReaderEraseBlock,  PhyBlockAddr = %x\n", PhyBlockAddr);
	result = ENE_LoadBinCode(us, MS_RW_PATTERN);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength = 0x200;
	bcb->Flags			= 0x80;
	bcb->CDB[0]			= 0xF2;
	bcb->CDB[1]			= 0x06;
	bcb->CDB[4]			= (BYTE)(bn);
	bcb->CDB[3]			= (BYTE)(bn>>8);
	bcb->CDB[2]			= (BYTE)(bn>>16);
	
	result = ENE_SendScsiCmd(us, FDIR_READ, NULL, 0);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	return USB_STOR_TRANSPORT_GOOD;
}

//----- MS_CardInit() ------------------------------------------------
int MS_CardInit(struct us_data *us)
{
	DWORD			result=0;
	WORD			TmpBlock;
	PBYTE			PageBuffer0 = NULL, PageBuffer1 = NULL;
	MS_LibTypeExtdat	extdat;
	WORD			btBlk1st, btBlk2nd;
	DWORD			btBlk1stErred;

	printk("MS_CardInit start\n");

	MS_LibFreeAllocatedArea(us);

	if (((PageBuffer0 = kmalloc(MS_BYTES_PER_PAGE, GFP_KERNEL)) == NULL) ||
	    ((PageBuffer1 = kmalloc(MS_BYTES_PER_PAGE, GFP_KERNEL)) == NULL))
	{
		result = MS_NO_MEMORY_ERROR;
		goto exit;
	}

	btBlk1st = btBlk2nd = MS_LB_NOT_USED;
	btBlk1stErred = 0;

	for (TmpBlock=0; TmpBlock < MS_MAX_INITIAL_ERROR_BLOCKS+2; TmpBlock++)
	{
		switch (MS_ReaderReadPage(us, TmpBlock, 0, (DWORD *)PageBuffer0, &extdat))
		{
			case MS_STATUS_SUCCESS:
			break;
			case MS_STATUS_INT_ERROR:
			break;
			case MS_STATUS_ERROR:
			default:
			continue;
		}

		if ((extdat.ovrflg & MS_REG_OVR_BKST) == MS_REG_OVR_BKST_NG)
			continue;

		if (((extdat.mngflg & MS_REG_MNG_SYSFLG) == MS_REG_MNG_SYSFLG_USER) ||
			(be16_to_cpu(((MemStickBootBlockPage0 *)PageBuffer0)->header.wBlockID) != MS_BOOT_BLOCK_ID) ||
			(be16_to_cpu(((MemStickBootBlockPage0 *)PageBuffer0)->header.wFormatVersion) != MS_BOOT_BLOCK_FORMAT_VERSION) ||
			(((MemStickBootBlockPage0 *)PageBuffer0)->header.bNumberOfDataEntry != MS_BOOT_BLOCK_DATA_ENTRIES))
				continue;

		if (btBlk1st != MS_LB_NOT_USED)
		{
			btBlk2nd = TmpBlock;
			break;
		}

		btBlk1st = TmpBlock;
		memcpy(PageBuffer1, PageBuffer0, MS_BYTES_PER_PAGE);
		if (extdat.status1 & (MS_REG_ST1_DTER | MS_REG_ST1_EXER | MS_REG_ST1_FGER))
			btBlk1stErred = 1;
	}

	if (btBlk1st == MS_LB_NOT_USED)
	{
		result = MS_STATUS_ERROR;
		goto exit;
	}

	// write protect
	if ((extdat.status0 & MS_REG_ST0_WP) == MS_REG_ST0_WP_ON)
		MS_LibCtrlSet(us, MS_LIB_CTRL_WRPROTECT);

	result = MS_STATUS_ERROR;
	// 1st Boot Block
	if (btBlk1stErred == 0)
		result = MS_LibProcessBootBlock(us, btBlk1st, PageBuffer1);   // 1st
	// 2nd Boot Block
	if (result && (btBlk2nd != MS_LB_NOT_USED))
		result = MS_LibProcessBootBlock(us, btBlk2nd, PageBuffer0);

	if (result)
	{
		result = MS_STATUS_ERROR;
		goto exit;
	}

	for (TmpBlock = 0; TmpBlock < btBlk1st; TmpBlock++)
		us->MS_Lib.Phy2LogMap[TmpBlock] = MS_LB_INITIAL_ERROR;

	us->MS_Lib.Phy2LogMap[btBlk1st] = MS_LB_BOOT_BLOCK;

	if (btBlk2nd != MS_LB_NOT_USED)
	{
		for (TmpBlock = btBlk1st + 1; TmpBlock < btBlk2nd; TmpBlock++)
			us->MS_Lib.Phy2LogMap[TmpBlock] = MS_LB_INITIAL_ERROR;
		us->MS_Lib.Phy2LogMap[btBlk2nd] = MS_LB_BOOT_BLOCK;
	}

	result = MS_LibScanLogicalBlockNumber(us, btBlk1st);
	if (result)
		goto exit;

	for (TmpBlock=MS_PHYSICAL_BLOCKS_PER_SEGMENT; TmpBlock<us->MS_Lib.NumberOfPhyBlock; TmpBlock+=MS_PHYSICAL_BLOCKS_PER_SEGMENT)
	{
		if (MS_CountFreeBlock(us, TmpBlock) == 0)
		{
			MS_LibCtrlSet(us, MS_LIB_CTRL_WRPROTECT);
			break;
		}
	}

	// write
	if (MS_LibAllocWriteBuf(us))
	{
		result = MS_NO_MEMORY_ERROR;
		goto exit;
	}

	result = MS_STATUS_SUCCESS;

exit:
	kfree(PageBuffer1);
    	kfree(PageBuffer0);

	printk("MS_CardInit end\n");
	return result;
}

//----- MS_LibCheckDisableBlock() ------------------------------------
int MS_LibCheckDisableBlock(struct us_data *us, WORD PhyBlock)
{
	PWORD			PageBuf=NULL;
	DWORD			result=MS_STATUS_SUCCESS;
	DWORD			blk, index=0;
	MS_LibTypeExtdat	extdat;

	if (((PageBuf = kmalloc(MS_BYTES_PER_PAGE, GFP_KERNEL)) == NULL))
	{
		result = MS_NO_MEMORY_ERROR;
		goto exit;
	}

	MS_ReaderReadPage(us, PhyBlock, 1, (DWORD *)PageBuf, &extdat);
	do
	{
		blk = be16_to_cpu(PageBuf[index]);
		if (blk == MS_LB_NOT_USED)
			break;
		if (blk == us->MS_Lib.Log2PhyMap[0])
		{
			result = MS_ERROR_FLASH_READ;
			break;
		}
		index++;
	} while(1);

exit:
	kfree(PageBuf);
	return result;
}

//----- MS_LibFreeAllocatedArea() ------------------------------------
void MS_LibFreeAllocatedArea(struct us_data *us)
{
	MS_LibFreeWriteBuf(us);
	MS_LibFreeLogicalMap(us);

	us->MS_Lib.flags			= 0;
	us->MS_Lib.BytesPerSector	= 0;
	us->MS_Lib.SectorsPerCylinder	= 0;

	us->MS_Lib.cardType		= 0;
	us->MS_Lib.blockSize		= 0;
	us->MS_Lib.PagesPerBlock	= 0;

	us->MS_Lib.NumberOfPhyBlock	= 0;
	us->MS_Lib.NumberOfLogBlock	= 0;
}

//----- MS_LibFreeWriteBuf() -----------------------------------------
void MS_LibFreeWriteBuf(struct us_data *us)
{
	us->MS_Lib.wrtblk = (WORD)-1; //set to -1
	MS_LibClearPageMap(us); // memset((fdoExt)->MS_Lib.pagemap, 0, sizeof((fdoExt)->MS_Lib.pagemap))

	if (us->MS_Lib.blkpag)
	{
		kfree((BYTE *)(us->MS_Lib.blkpag));  // Arnold test ...
		us->MS_Lib.blkpag = NULL;
	}

	if (us->MS_Lib.blkext)
	{
		kfree((BYTE *)(us->MS_Lib.blkext));  // Arnold test ...
		us->MS_Lib.blkext = NULL;
	}
}

//----- MS_LibFreeLogicalMap() ---------------------------------------
int MS_LibFreeLogicalMap(struct us_data *us)
{
	kfree(us->MS_Lib.Phy2LogMap);
	us->MS_Lib.Phy2LogMap = NULL;

	kfree(us->MS_Lib.Log2PhyMap);
	us->MS_Lib.Log2PhyMap = NULL;

    return 0;
}

//----- MS_LibProcessBootBlock() -------------------------------------
int MS_LibProcessBootBlock(struct us_data *us, WORD PhyBlock, BYTE *PageData)
{
	MemStickBootBlockSysEnt  *SysEntry;
	MemStickBootBlockSysInf  *SysInfo;
	DWORD                    i, result;
	BYTE                     PageNumber;
	BYTE                     *PageBuffer;
	MS_LibTypeExtdat         ExtraData;

	if ((PageBuffer = kmalloc(MS_BYTES_PER_PAGE, GFP_KERNEL))==NULL)
		return (DWORD)-1;

	result = (DWORD)-1;

	SysInfo= &(((MemStickBootBlockPage0 *)PageData)->sysinf);

	if ((SysInfo->bMsClass != MS_SYSINF_MSCLASS_TYPE_1)                                   ||
		(be16_to_cpu(SysInfo->wPageSize) != MS_SYSINF_PAGE_SIZE)                       ||
		((SysInfo->bSecuritySupport & MS_SYSINF_SECURITY) == MS_SYSINF_SECURITY_SUPPORT) ||
		(SysInfo->bReserved1 != MS_SYSINF_RESERVED1)                                     ||
		(SysInfo->bReserved2 != MS_SYSINF_RESERVED2)                                     ||
		(SysInfo->bFormatType!= MS_SYSINF_FORMAT_FAT)                                    ||
		(SysInfo->bUsage != MS_SYSINF_USAGE_GENERAL))
		goto exit;

	switch (us->MS_Lib.cardType = SysInfo->bCardType)
	{
		case MS_SYSINF_CARDTYPE_RDONLY:
			MS_LibCtrlSet(us, MS_LIB_CTRL_RDONLY);
			break;
		case MS_SYSINF_CARDTYPE_RDWR:
			MS_LibCtrlReset(us, MS_LIB_CTRL_RDONLY);
			break;
		case MS_SYSINF_CARDTYPE_HYBRID:
		default:
			goto exit;
	}

	us->MS_Lib.blockSize        = be16_to_cpu(SysInfo->wBlockSize);
	us->MS_Lib.NumberOfPhyBlock = be16_to_cpu(SysInfo->wBlockNumber);
	us->MS_Lib.NumberOfLogBlock = be16_to_cpu(SysInfo->wTotalBlockNumber) - 2;
	us->MS_Lib.PagesPerBlock    = us->MS_Lib.blockSize * SIZE_OF_KIRO / MS_BYTES_PER_PAGE;
	us->MS_Lib.NumberOfSegment  = us->MS_Lib.NumberOfPhyBlock / MS_PHYSICAL_BLOCKS_PER_SEGMENT;
	us->MS_Model                = be16_to_cpu(SysInfo->wMemorySize);

	if (MS_LibAllocLogicalMap(us))			//Allocate to all number of logicalblock and physicalblock
		goto exit;

	MS_LibSetBootBlockMark(us, PhyBlock);		//Mark the book block

	SysEntry = &(((MemStickBootBlockPage0 *)PageData)->sysent);

	for (i=0; i<MS_NUMBER_OF_SYSTEM_ENTRY; i++)
	{
		DWORD  EntryOffset, EntrySize;

		if ((EntryOffset = be32_to_cpu(SysEntry->entry[i].dwStart)) == 0xffffff)
			continue;

		if ((EntrySize = be32_to_cpu(SysEntry->entry[i].dwSize)) == 0)
			continue;

		if (EntryOffset + MS_BYTES_PER_PAGE + EntrySize > us->MS_Lib.blockSize * (DWORD)SIZE_OF_KIRO)
			continue;

		if (i == 0)
		{
			BYTE  PrevPageNumber = 0;
			WORD  phyblk;

			if (SysEntry->entry[i].bType != MS_SYSENT_TYPE_INVALID_BLOCK)
				goto exit;

			while (EntrySize > 0)
			{
				if ((PageNumber = (BYTE)(EntryOffset / MS_BYTES_PER_PAGE + 1)) != PrevPageNumber)
				{
					switch (MS_ReaderReadPage(us, PhyBlock, PageNumber, (DWORD *)PageBuffer, &ExtraData))
					{
						case MS_STATUS_SUCCESS:
							break;
						case MS_STATUS_WRITE_PROTECT:
						case MS_ERROR_FLASH_READ:
						case MS_STATUS_ERROR:
						default:
							goto exit;
					}

					PrevPageNumber = PageNumber;
				}

				if ((phyblk = be16_to_cpu(*(WORD *)(PageBuffer + (EntryOffset % MS_BYTES_PER_PAGE)))) < 0x0fff)
					MS_LibSetInitialErrorBlock(us, phyblk);

				EntryOffset += 2;
				EntrySize -= 2;
			}
		}
		else if (i == 1)
		{  // CIS/IDI
			MemStickBootBlockIDI  *idi;

			if (SysEntry->entry[i].bType != MS_SYSENT_TYPE_CIS_IDI)
				goto exit;

			switch (MS_ReaderReadPage(us, PhyBlock, (BYTE)(EntryOffset / MS_BYTES_PER_PAGE + 1), (DWORD *)PageBuffer, &ExtraData))
			{
				case MS_STATUS_SUCCESS:
					break;
				case MS_STATUS_WRITE_PROTECT:
				case MS_ERROR_FLASH_READ:
				case MS_STATUS_ERROR:
				default:
					goto exit;
			}

			idi = &((MemStickBootBlockCIS_IDI *)(PageBuffer + (EntryOffset % MS_BYTES_PER_PAGE)))->idi.idi;
			if (le16_to_cpu(idi->wIDIgeneralConfiguration) != MS_IDI_GENERAL_CONF)
				goto exit;

			us->MS_Lib.BytesPerSector = le16_to_cpu(idi->wIDIbytesPerSector);
			if (us->MS_Lib.BytesPerSector != MS_BYTES_PER_PAGE)
				goto exit;
		}
	} // End for ..

	result = 0;

exit:
	if (result)		MS_LibFreeLogicalMap(us);
	kfree(PageBuffer);

	result = 0;
	return result;
}

//----- MS_LibAllocLogicalMap() --------------------------------------
int MS_LibAllocLogicalMap(struct us_data *us)
{
	DWORD  i;


	us->MS_Lib.Phy2LogMap = kmalloc(us->MS_Lib.NumberOfPhyBlock * sizeof(WORD), GFP_KERNEL);
	us->MS_Lib.Log2PhyMap = kmalloc(us->MS_Lib.NumberOfLogBlock * sizeof(WORD), GFP_KERNEL);

	if ((us->MS_Lib.Phy2LogMap == NULL) || (us->MS_Lib.Log2PhyMap == NULL))
	{
		MS_LibFreeLogicalMap(us);
		return (DWORD)-1;
	}

	for (i = 0; i < us->MS_Lib.NumberOfPhyBlock; i++)
		us->MS_Lib.Phy2LogMap[i] = MS_LB_NOT_USED;

	for (i = 0; i < us->MS_Lib.NumberOfLogBlock; i++)
	us->MS_Lib.Log2PhyMap[i] = MS_LB_NOT_USED;

	return 0;
}

//----- MS_LibSetBootBlockMark() -------------------------------------
int MS_LibSetBootBlockMark(struct us_data *us, WORD phyblk)
{
    return MS_LibSetLogicalBlockMark(us, phyblk, MS_LB_BOOT_BLOCK);
}

//----- MS_LibSetLogicalBlockMark() ----------------------------------
int MS_LibSetLogicalBlockMark(struct us_data *us, WORD phyblk, WORD mark)
{
    if (phyblk >= us->MS_Lib.NumberOfPhyBlock)
        return (DWORD)-1;

    us->MS_Lib.Phy2LogMap[phyblk] = mark;

    return 0;
}

//----- MS_LibSetInitialErrorBlock() ---------------------------------
int MS_LibSetInitialErrorBlock(struct us_data *us, WORD phyblk)
{
    return MS_LibSetLogicalBlockMark(us, phyblk, MS_LB_INITIAL_ERROR);
}

//----- MS_LibScanLogicalBlockNumber() -------------------------------
int MS_LibScanLogicalBlockNumber(struct us_data *us, WORD btBlk1st)
{
	WORD			PhyBlock, newblk, i;
	WORD			LogStart, LogEnde;
	MS_LibTypeExtdat	extdat;
	BYTE			buf[0x200];
	DWORD			count=0, index=0;

	for (PhyBlock = 0; PhyBlock < us->MS_Lib.NumberOfPhyBlock;)
	{
		MS_LibPhy2LogRange(PhyBlock, &LogStart, &LogEnde);

		for (i=0; i<MS_PHYSICAL_BLOCKS_PER_SEGMENT; i++, PhyBlock++)
		{
			switch (MS_LibConv2Logical(us, PhyBlock))
			{
				case MS_STATUS_ERROR:
					continue;
				default:
					break;
			}

			if (count == PhyBlock)
			{
				MS_LibReadExtraBlock(us, PhyBlock, 0, 0x80, &buf);
				count += 0x80;
			}
			index = (PhyBlock % 0x80) * 4;

			extdat.ovrflg = buf[index];
			extdat.mngflg = buf[index+1];
			extdat.logadr = MemStickLogAddr(buf[index+2], buf[index+3]);

			if ((extdat.ovrflg & MS_REG_OVR_BKST) != MS_REG_OVR_BKST_OK)
			{
				MS_LibSetAcquiredErrorBlock(us, PhyBlock);
				continue;
			}

			if ((extdat.mngflg & MS_REG_MNG_ATFLG) == MS_REG_MNG_ATFLG_ATTBL)
			{
				MS_LibErasePhyBlock(us, PhyBlock);
				continue;
			}

			if (extdat.logadr != MS_LB_NOT_USED)
			{
				if ((extdat.logadr < LogStart) || (LogEnde <= extdat.logadr))
				{
					MS_LibErasePhyBlock(us, PhyBlock);
					continue;
				}

				if ((newblk = MS_LibConv2Physical(us, extdat.logadr)) != MS_LB_NOT_USED)
				{
					if (extdat.logadr==0)
					{
						MS_LibSetLogicalPair(us, extdat.logadr, PhyBlock);
						if ( MS_LibCheckDisableBlock(us, btBlk1st) )
						{
							MS_LibSetLogicalPair(us, extdat.logadr, newblk);
							continue;
						}
					}

					MS_LibReadExtra(us, newblk, 0, &extdat);
					if ((extdat.ovrflg & MS_REG_OVR_UDST) == MS_REG_OVR_UDST_UPDATING)
					{
						MS_LibErasePhyBlock(us, PhyBlock);
						continue;
					}
					else
						MS_LibErasePhyBlock(us, newblk);
				}

				MS_LibSetLogicalPair(us, extdat.logadr, PhyBlock);
			}
		}
	} //End for ...

	return MS_STATUS_SUCCESS;
}

//----- MS_LibAllocWriteBuf() ----------------------------------------
int MS_LibAllocWriteBuf(struct us_data *us)
{
	us->MS_Lib.wrtblk = (WORD)-1;

	us->MS_Lib.blkpag = kmalloc(us->MS_Lib.PagesPerBlock * us->MS_Lib.BytesPerSector, GFP_KERNEL);
	us->MS_Lib.blkext = kmalloc(us->MS_Lib.PagesPerBlock * sizeof(MS_LibTypeExtdat), GFP_KERNEL);

	if ((us->MS_Lib.blkpag == NULL) || (us->MS_Lib.blkext == NULL))
	{
		MS_LibFreeWriteBuf(us);
		return (DWORD)-1;
	}

	MS_LibClearWriteBuf(us);

	return 0;
}

//----- MS_LibClearWriteBuf() ----------------------------------------
void MS_LibClearWriteBuf(struct us_data *us)
{
	int i;

	us->MS_Lib.wrtblk = (WORD)-1;
	MS_LibClearPageMap(us);

	if (us->MS_Lib.blkpag)
		memset(us->MS_Lib.blkpag, 0xff, us->MS_Lib.PagesPerBlock * us->MS_Lib.BytesPerSector);

	if (us->MS_Lib.blkext)
	{
		for (i = 0; i < us->MS_Lib.PagesPerBlock; i++)
		{
			us->MS_Lib.blkext[i].status1 = MS_REG_ST1_DEFAULT;
			us->MS_Lib.blkext[i].ovrflg = MS_REG_OVR_DEFAULT;
			us->MS_Lib.blkext[i].mngflg = MS_REG_MNG_DEFAULT;
			us->MS_Lib.blkext[i].logadr = MS_LB_NOT_USED;
		}
	}
}

//----- MS_LibPhy2LogRange() -----------------------------------------
void MS_LibPhy2LogRange(WORD PhyBlock, WORD *LogStart, WORD *LogEnde)
{
	PhyBlock /= MS_PHYSICAL_BLOCKS_PER_SEGMENT;

	if (PhyBlock)
	{
		*LogStart = MS_LOGICAL_BLOCKS_IN_1ST_SEGMENT + (PhyBlock - 1) * MS_LOGICAL_BLOCKS_PER_SEGMENT;//496
		*LogEnde = *LogStart + MS_LOGICAL_BLOCKS_PER_SEGMENT;//496
	}
	else
	{
		*LogStart = 0;
		*LogEnde = MS_LOGICAL_BLOCKS_IN_1ST_SEGMENT;//494
	}
}

//----- MS_LibReadExtraBlock() --------------------------------------------
int MS_LibReadExtraBlock(struct us_data *us, DWORD PhyBlock, BYTE PageNum, BYTE blen, void *buf)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	int	result;

	//printk("MS_LibReadExtraBlock --- PhyBlock = %x, PageNum = %x, blen = %x\n", PhyBlock, PageNum, blen);

	// Read Extra Data
	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength = 0x4 * blen;
	bcb->Flags			= 0x80;
	bcb->CDB[0]			= 0xF1;
	bcb->CDB[1]			= 0x03;
	bcb->CDB[5]			= (BYTE)(PageNum);
	bcb->CDB[4]			= (BYTE)(PhyBlock);
	bcb->CDB[3]			= (BYTE)(PhyBlock>>8);
	bcb->CDB[2]			= (BYTE)(PhyBlock>>16);
	bcb->CDB[6]			= blen;

	result = ENE_SendScsiCmd(us, FDIR_READ, buf, 0);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	return USB_STOR_TRANSPORT_GOOD;
}

//----- MS_LibReadExtra() --------------------------------------------
int MS_LibReadExtra(struct us_data *us, DWORD PhyBlock, BYTE PageNum, MS_LibTypeExtdat *ExtraDat)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	int	result;
	BYTE	ExtBuf[4];

	//printk("MS_LibReadExtra --- PhyBlock = %x, PageNum = %x\n", PhyBlock, PageNum);
	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength = 0x4;
	bcb->Flags			= 0x80;
	bcb->CDB[0]			= 0xF1;
	bcb->CDB[1]			= 0x03;
	bcb->CDB[5]			= (BYTE)(PageNum);
	bcb->CDB[4]			= (BYTE)(PhyBlock);
	bcb->CDB[3]			= (BYTE)(PhyBlock>>8);
	bcb->CDB[2]			= (BYTE)(PhyBlock>>16);
	bcb->CDB[6]			= 0x01;
	
	result = ENE_SendScsiCmd(us, FDIR_READ, &ExtBuf, 0);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	ExtraDat->reserved = 0;
	ExtraDat->intr     = 0x80;  // Not yet, waiting for fireware support
	ExtraDat->status0  = 0x10;  // Not yet, waiting for fireware support
	ExtraDat->status1  = 0x00;  // Not yet, waiting for fireware support
	ExtraDat->ovrflg   = ExtBuf[0];
	ExtraDat->mngflg   = ExtBuf[1];
	ExtraDat->logadr   = MemStickLogAddr(ExtBuf[2], ExtBuf[3]);
	
	return USB_STOR_TRANSPORT_GOOD;
}

//----- MS_LibSetAcquiredErrorBlock() --------------------------------
int MS_LibSetAcquiredErrorBlock(struct us_data *us, WORD phyblk)
{
	WORD log;

	if (phyblk >= us->MS_Lib.NumberOfPhyBlock)
		return (DWORD)-1;

	if ((log = us->MS_Lib.Phy2LogMap[phyblk]) < us->MS_Lib.NumberOfLogBlock)
		us->MS_Lib.Log2PhyMap[log] = MS_LB_NOT_USED;

	if (us->MS_Lib.Phy2LogMap[phyblk] != MS_LB_INITIAL_ERROR)
		us->MS_Lib.Phy2LogMap[phyblk] = MS_LB_ACQUIRED_ERROR;

	return 0;
}

//----- MS_LibErasePhyBlock() ----------------------------------------
int MS_LibErasePhyBlock(struct us_data *us, WORD phyblk)
{
	WORD  log;

	if (phyblk >= us->MS_Lib.NumberOfPhyBlock)
		return MS_STATUS_ERROR;

	if ((log = us->MS_Lib.Phy2LogMap[phyblk]) < us->MS_Lib.NumberOfLogBlock)
		us->MS_Lib.Log2PhyMap[log] = MS_LB_NOT_USED;

	us->MS_Lib.Phy2LogMap[phyblk] = MS_LB_NOT_USED;

	if (MS_LibIsWritable(us))
	{
		switch (MS_ReaderEraseBlock(us, phyblk))
		{
			case MS_STATUS_SUCCESS:
				us->MS_Lib.Phy2LogMap[phyblk] = MS_LB_NOT_USED_ERASED;
				return MS_STATUS_SUCCESS;
			case MS_ERROR_FLASH_ERASE:
			case MS_STATUS_INT_ERROR :
				MS_LibErrorPhyBlock(us, phyblk);
				return MS_ERROR_FLASH_ERASE;
			case MS_STATUS_ERROR:
			default:
				MS_LibCtrlSet(us, MS_LIB_CTRL_RDONLY);
				MS_LibSetAcquiredErrorBlock(us, phyblk);
				return MS_STATUS_ERROR;
		}
	}

	MS_LibSetAcquiredErrorBlock(us, phyblk);

	return MS_STATUS_SUCCESS;
}

//----- MS_LibErrorPhyBlock() ----------------------------------------
int MS_LibErrorPhyBlock(struct us_data *us, WORD phyblk)
{
    if (phyblk >= us->MS_Lib.NumberOfPhyBlock)
        return MS_STATUS_ERROR;

    MS_LibSetAcquiredErrorBlock(us, phyblk);

    if (MS_LibIsWritable(us))
        return MS_LibOverwriteExtra(us, phyblk, 0, (BYTE)(~MS_REG_OVR_BKST));


    return MS_STATUS_SUCCESS;
}

//----- MS_LibOverwriteExtra() ---------------------------------------
int MS_LibOverwriteExtra(struct us_data *us, DWORD PhyBlockAddr, BYTE PageNum, BYTE OverwriteFlag)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	int	result;

	//printk("MS --- MS_LibOverwriteExtra,  PhyBlockAddr = %x, PageNum = %x\n", PhyBlockAddr, PageNum);
	result = ENE_LoadBinCode(us, MS_RW_PATTERN);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength = 0x4;
	bcb->Flags			= 0x80;
	bcb->CDB[0]			= 0xF2;
	bcb->CDB[1]			= 0x05;
	bcb->CDB[5]			= (BYTE)(PageNum);
	bcb->CDB[4]			= (BYTE)(PhyBlockAddr);
	bcb->CDB[3]			= (BYTE)(PhyBlockAddr>>8);
	bcb->CDB[2]			= (BYTE)(PhyBlockAddr>>16);
	bcb->CDB[6]			= OverwriteFlag;
	bcb->CDB[7]			= 0xFF;
	bcb->CDB[8]			= 0xFF;
	bcb->CDB[9]			= 0xFF;
	
	result = ENE_SendScsiCmd(us, FDIR_READ, NULL, 0);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	return USB_STOR_TRANSPORT_GOOD;
}

//----- MS_LibForceSetLogicalPair() ----------------------------------
int MS_LibForceSetLogicalPair(struct us_data *us, WORD logblk, WORD phyblk)
{
	if (logblk == MS_LB_NOT_USED)
		return 0;

	if ((logblk >= us->MS_Lib.NumberOfLogBlock) || (phyblk >= us->MS_Lib.NumberOfPhyBlock))
		return (DWORD)-1;

	us->MS_Lib.Phy2LogMap[phyblk] = logblk;
	us->MS_Lib.Log2PhyMap[logblk] = phyblk;

	return 0;
}

//----- MS_LibSetLogicalPair() ---------------------------------------
int MS_LibSetLogicalPair(struct us_data *us, WORD logblk, WORD phyblk)
{
	if ((logblk >= us->MS_Lib.NumberOfLogBlock) || (phyblk >= us->MS_Lib.NumberOfPhyBlock))
		return (DWORD)-1;

	us->MS_Lib.Phy2LogMap[phyblk] = logblk;
	us->MS_Lib.Log2PhyMap[logblk] = phyblk;

	return 0;
}

//----- MS_CountFreeBlock() ------------------------------------------
int MS_CountFreeBlock(struct us_data *us, WORD PhyBlock)
{
	DWORD Ende, Count;

	Ende = PhyBlock + MS_PHYSICAL_BLOCKS_PER_SEGMENT;
	for (Count = 0; PhyBlock < Ende; PhyBlock++)
	{
		switch (us->MS_Lib.Phy2LogMap[PhyBlock])
		{
			case MS_LB_NOT_USED:
			case MS_LB_NOT_USED_ERASED:
				Count++;
			default:
				break;
		}
	}

	return Count;
}

//----- MS_LibSearchBlockFromPhysical() ------------------------------
int MS_LibSearchBlockFromPhysical(struct us_data *us, WORD phyblk)
{
	WORD			Newblk;
	WORD			blk;
	MS_LibTypeExtdat	extdat;

	if (phyblk >= us->MS_Lib.NumberOfPhyBlock)
		return MS_LB_ERROR;

	for (blk = phyblk + 1; blk != phyblk; blk++)
	{
		if ((blk & MS_PHYSICAL_BLOCKS_PER_SEGMENT_MASK) == 0)
			blk -= MS_PHYSICAL_BLOCKS_PER_SEGMENT;

		Newblk = us->MS_Lib.Phy2LogMap[blk];
		if (us->MS_Lib.Phy2LogMap[blk] == MS_LB_NOT_USED_ERASED)
			return blk;
		else if (us->MS_Lib.Phy2LogMap[blk] == MS_LB_NOT_USED)
		{
			switch (MS_LibReadExtra(us, blk, 0, &extdat))
			{
				case MS_STATUS_SUCCESS :
				case MS_STATUS_SUCCESS_WITH_ECC:
					break;
				case MS_NOCARD_ERROR:
					return MS_NOCARD_ERROR;
				case MS_STATUS_INT_ERROR:
					return MS_LB_ERROR;
				case MS_ERROR_FLASH_READ:
				default:
					MS_LibSetAcquiredErrorBlock(us, blk);     // MS_LibErrorPhyBlock(fdoExt, blk);
					continue;
			} // End switch

			if ((extdat.ovrflg & MS_REG_OVR_BKST) != MS_REG_OVR_BKST_OK)
			{
				MS_LibSetAcquiredErrorBlock(us, blk);
				continue;
			}

			switch (MS_LibErasePhyBlock(us, blk))
			{
				case MS_STATUS_SUCCESS:
					return blk;
				case MS_STATUS_ERROR:
					return MS_LB_ERROR;
				case MS_ERROR_FLASH_ERASE:
				default:
					MS_LibErrorPhyBlock(us, blk);
					break;
			}
		}
	} // End for

	return MS_LB_ERROR;
}

//----- MS_LibSearchBlockFromLogical() -------------------------------
int MS_LibSearchBlockFromLogical(struct us_data *us, WORD logblk)
{
	WORD phyblk;

	if ((phyblk=MS_LibConv2Physical(us, logblk)) >= MS_LB_ERROR)
	{
		if (logblk >= us->MS_Lib.NumberOfLogBlock)
			return MS_LB_ERROR;

		phyblk = (logblk + MS_NUMBER_OF_BOOT_BLOCK) / MS_LOGICAL_BLOCKS_PER_SEGMENT;
		phyblk *= MS_PHYSICAL_BLOCKS_PER_SEGMENT;
		phyblk += MS_PHYSICAL_BLOCKS_PER_SEGMENT - 1;
	}

	return MS_LibSearchBlockFromPhysical(us, phyblk);
}


