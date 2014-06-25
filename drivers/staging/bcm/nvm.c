#include "headers.h"

#define DWORD unsigned int

static int BcmDoChipSelect(struct bcm_mini_adapter *Adapter, unsigned int offset);
static int BcmGetActiveDSD(struct bcm_mini_adapter *Adapter);
static int BcmGetActiveISO(struct bcm_mini_adapter *Adapter);
static unsigned int BcmGetEEPROMSize(struct bcm_mini_adapter *Adapter);
static int BcmGetFlashCSInfo(struct bcm_mini_adapter *Adapter);
static unsigned int BcmGetFlashSectorSize(struct bcm_mini_adapter *Adapter, unsigned int FlashSectorSizeSig, unsigned int FlashSectorSize);

static VOID BcmValidateNvmType(struct bcm_mini_adapter *Adapter);
static int BcmGetNvmSize(struct bcm_mini_adapter *Adapter);
static unsigned int BcmGetFlashSize(struct bcm_mini_adapter *Adapter);
static enum bcm_nvm_type BcmGetNvmType(struct bcm_mini_adapter *Adapter);

static int BcmGetSectionValEndOffset(struct bcm_mini_adapter *Adapter, enum bcm_flash2x_section_val eFlash2xSectionVal);

static B_UINT8 IsOffsetWritable(struct bcm_mini_adapter *Adapter, unsigned int uiOffset);
static int IsSectionWritable(struct bcm_mini_adapter *Adapter, enum bcm_flash2x_section_val Section);
static int IsSectionExistInVendorInfo(struct bcm_mini_adapter *Adapter, enum bcm_flash2x_section_val section);

static int ReadDSDPriority(struct bcm_mini_adapter *Adapter, enum bcm_flash2x_section_val dsd);
static int ReadDSDSignature(struct bcm_mini_adapter *Adapter, enum bcm_flash2x_section_val dsd);
static int ReadISOPriority(struct bcm_mini_adapter *Adapter, enum bcm_flash2x_section_val iso);
static int ReadISOSignature(struct bcm_mini_adapter *Adapter, enum bcm_flash2x_section_val iso);

static int CorruptDSDSig(struct bcm_mini_adapter *Adapter, enum bcm_flash2x_section_val eFlash2xSectionVal);
static int CorruptISOSig(struct bcm_mini_adapter *Adapter, enum bcm_flash2x_section_val eFlash2xSectionVal);
static int SaveHeaderIfPresent(struct bcm_mini_adapter *Adapter, PUCHAR pBuff, unsigned int uiSectAlignAddr);
static int WriteToFlashWithoutSectorErase(struct bcm_mini_adapter *Adapter, PUINT pBuff,
					enum bcm_flash2x_section_val eFlash2xSectionVal,
					unsigned int uiOffset, unsigned int uiNumBytes);
static enum bcm_flash2x_section_val getHighestPriDSD(struct bcm_mini_adapter *Adapter);
static enum bcm_flash2x_section_val getHighestPriISO(struct bcm_mini_adapter *Adapter);

static int BeceemFlashBulkRead(
	struct bcm_mini_adapter *Adapter,
	PUINT pBuffer,
	unsigned int uiOffset,
	unsigned int uiNumBytes);

static int BeceemFlashBulkWrite(
	struct bcm_mini_adapter *Adapter,
	PUINT pBuffer,
	unsigned int uiOffset,
	unsigned int uiNumBytes,
	bool bVerify);

static int GetFlashBaseAddr(struct bcm_mini_adapter *Adapter);

static int ReadBeceemEEPROMBulk(struct bcm_mini_adapter *Adapter, unsigned int dwAddress, unsigned int *pdwData, unsigned int dwNumData);

/* Procedure:	ReadEEPROMStatusRegister
 *
 * Description: Reads the standard EEPROM Status Register.
 *
 * Arguments:
 *		Adapter    - ptr to Adapter object instance
 * Returns:
 *		OSAL_STATUS_CODE
 */
static UCHAR ReadEEPROMStatusRegister(struct bcm_mini_adapter *Adapter)
{
	UCHAR uiData = 0;
	DWORD dwRetries = MAX_EEPROM_RETRIES * RETRIES_PER_DELAY;
	unsigned int uiStatus = 0;
	unsigned int value = 0;
	unsigned int value1 = 0;

	/* Read the EEPROM status register */
	value = EEPROM_READ_STATUS_REGISTER;
	wrmalt(Adapter, EEPROM_CMDQ_SPI_REG, &value, sizeof(value));

	while (dwRetries != 0) {
		value = 0;
		uiStatus = 0;
		rdmalt(Adapter, EEPROM_SPI_Q_STATUS1_REG, &uiStatus, sizeof(uiStatus));
		if (Adapter->device_removed == TRUE) {
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Modem has got removed hence exiting....");
			break;
		}

		/* Wait for Avail bit to be set. */
		if ((uiStatus & EEPROM_READ_DATA_AVAIL) != 0) {
			/* Clear the Avail/Full bits - which ever is set. */
			value = uiStatus & (EEPROM_READ_DATA_AVAIL | EEPROM_READ_DATA_FULL);
			wrmalt(Adapter, EEPROM_SPI_Q_STATUS1_REG, &value, sizeof(value));

			value = 0;
			rdmalt(Adapter, EEPROM_READ_DATAQ_REG, &value, sizeof(value));
			uiData = (UCHAR)value;

			break;
		}

		dwRetries--;
		if (dwRetries == 0) {
			rdmalt(Adapter, EEPROM_SPI_Q_STATUS1_REG, &value, sizeof(value));
			rdmalt(Adapter, EEPROM_SPI_Q_STATUS_REG, &value1, sizeof(value1));
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "0x3004 = %x 0x3008 = %x, retries = %d failed.\n", value, value1, MAX_EEPROM_RETRIES * RETRIES_PER_DELAY);
			return uiData;
		}
		if (!(dwRetries%RETRIES_PER_DELAY))
			udelay(1000);
		uiStatus = 0;
	}
	return uiData;
} /* ReadEEPROMStatusRegister */

/*
 * Procedure:	ReadBeceemEEPROMBulk
 *
 * Description: This routine reads 16Byte data from EEPROM
 *
 * Arguments:
 *		Adapter    - ptr to Adapter object instance
 *      dwAddress   - EEPROM Offset to read the data from.
 *      pdwData     - Pointer to double word where data needs to be stored in.  //		dwNumWords  - Number of words.  Valid values are 4 ONLY.
 *
 * Returns:
 *		OSAL_STATUS_CODE:
 */

static int ReadBeceemEEPROMBulk(struct bcm_mini_adapter *Adapter,
			DWORD dwAddress,
			DWORD *pdwData,
			DWORD dwNumWords)
{
	DWORD dwIndex = 0;
	DWORD dwRetries = MAX_EEPROM_RETRIES * RETRIES_PER_DELAY;
	unsigned int uiStatus  = 0;
	unsigned int value = 0;
	unsigned int value1 = 0;
	UCHAR *pvalue;

	/* Flush the read and cmd queue. */
	value = (EEPROM_READ_QUEUE_FLUSH | EEPROM_CMD_QUEUE_FLUSH);
	wrmalt(Adapter, SPI_FLUSH_REG, &value, sizeof(value));
	value = 0;
	wrmalt(Adapter, SPI_FLUSH_REG, &value, sizeof(value));

	/* Clear the Avail/Full bits. */
	value = (EEPROM_READ_DATA_AVAIL | EEPROM_READ_DATA_FULL);
	wrmalt(Adapter, EEPROM_SPI_Q_STATUS1_REG, &value, sizeof(value));

	value = dwAddress | ((dwNumWords == 4) ? EEPROM_16_BYTE_PAGE_READ : EEPROM_4_BYTE_PAGE_READ);
	wrmalt(Adapter, EEPROM_CMDQ_SPI_REG, &value, sizeof(value));

	while (dwRetries != 0) {
		uiStatus = 0;
		rdmalt(Adapter, EEPROM_SPI_Q_STATUS1_REG, &uiStatus, sizeof(uiStatus));
		if (Adapter->device_removed == TRUE) {
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Modem has got Removed.hence exiting from loop...");
			return -ENODEV;
		}

		/* If we are reading 16 bytes we want to be sure that the queue
		 * is full before we read.  In the other cases we are ok if the
		 * queue has data available
		 */
		if (dwNumWords == 4) {
			if ((uiStatus & EEPROM_READ_DATA_FULL) != 0) {
				/* Clear the Avail/Full bits - which ever is set. */
				value = (uiStatus & (EEPROM_READ_DATA_AVAIL | EEPROM_READ_DATA_FULL));
				wrmalt(Adapter, EEPROM_SPI_Q_STATUS1_REG, &value, sizeof(value));
				break;
			}
		} else if (dwNumWords == 1) {
			if ((uiStatus & EEPROM_READ_DATA_AVAIL) != 0) {
				/* We just got Avail and we have to read 32bits so we
				 * need this sleep for Cardbus kind of devices.
				 */
				if (Adapter->chip_id == 0xBECE0210)
					udelay(800);

				/* Clear the Avail/Full bits - which ever is set. */
				value = (uiStatus & (EEPROM_READ_DATA_AVAIL | EEPROM_READ_DATA_FULL));
				wrmalt(Adapter, EEPROM_SPI_Q_STATUS1_REG, &value, sizeof(value));
				break;
			}
		}

		uiStatus = 0;

		dwRetries--;
		if (dwRetries == 0) {
			value = 0;
			value1 = 0;
			rdmalt(Adapter, EEPROM_SPI_Q_STATUS1_REG, &value, sizeof(value));
			rdmalt(Adapter, EEPROM_SPI_Q_STATUS_REG, &value1, sizeof(value1));
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "dwNumWords %d 0x3004 = %x 0x3008 = %x  retries = %d failed.\n",
					dwNumWords, value,  value1,  MAX_EEPROM_RETRIES * RETRIES_PER_DELAY);
			return STATUS_FAILURE;
		}

		if (!(dwRetries%RETRIES_PER_DELAY))
			udelay(1000);
	}

	for (dwIndex = 0; dwIndex < dwNumWords; dwIndex++) {
		/* We get only a byte at a time - from LSB to MSB. We shift it into an integer. */
		pvalue = (PUCHAR)(pdwData + dwIndex);

		value = 0;
		rdmalt(Adapter, EEPROM_READ_DATAQ_REG, &value, sizeof(value));

		pvalue[0] = value;

		value = 0;
		rdmalt(Adapter, EEPROM_READ_DATAQ_REG, &value, sizeof(value));

		pvalue[1] = value;

		value = 0;
		rdmalt(Adapter, EEPROM_READ_DATAQ_REG, &value, sizeof(value));

		pvalue[2] = value;

		value = 0;
		rdmalt(Adapter, EEPROM_READ_DATAQ_REG, &value, sizeof(value));

		pvalue[3] = value;
	}

	return STATUS_SUCCESS;
} /* ReadBeceemEEPROMBulk() */

/*
 * Procedure:	ReadBeceemEEPROM
 *
 * Description: This routine reads 4 data from EEPROM.  It uses 1 or 2 page
 *				reads to do this operation.
 *
 * Arguments:
 *		Adapter     - ptr to Adapter object instance
 *      uiOffset	- EEPROM Offset to read the data from.
 *      pBuffer		- Pointer to word where data needs to be stored in.
 *
 * Returns:
 *		OSAL_STATUS_CODE:
 */

int ReadBeceemEEPROM(struct bcm_mini_adapter *Adapter,
		DWORD uiOffset,
		DWORD *pBuffer)
{
	unsigned int uiData[8]		= {0};
	unsigned int uiByteOffset	= 0;
	unsigned int uiTempOffset	= 0;

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, " ====> ");

	uiTempOffset = uiOffset - (uiOffset % MAX_RW_SIZE);
	uiByteOffset = uiOffset - uiTempOffset;

	ReadBeceemEEPROMBulk(Adapter, uiTempOffset, (PUINT)&uiData[0], 4);

	/* A word can overlap at most over 2 pages. In that case we read the
	 * next page too.
	 */
	if (uiByteOffset > 12)
		ReadBeceemEEPROMBulk(Adapter, uiTempOffset + MAX_RW_SIZE, (PUINT)&uiData[4], 4);

	memcpy((PUCHAR)pBuffer, (((PUCHAR)&uiData[0]) + uiByteOffset), 4);

	return STATUS_SUCCESS;
} /* ReadBeceemEEPROM() */

int ReadMacAddressFromNVM(struct bcm_mini_adapter *Adapter)
{
	int Status;
	unsigned char puMacAddr[6];

	Status = BeceemNVMRead(Adapter,
			(PUINT)&puMacAddr[0],
			INIT_PARAMS_1_MACADDRESS_ADDRESS,
			MAC_ADDRESS_SIZE);

	if (Status == STATUS_SUCCESS)
		memcpy(Adapter->dev->dev_addr, puMacAddr, MAC_ADDRESS_SIZE);

	return Status;
}

/*
 * Procedure:	BeceemEEPROMBulkRead
 *
 * Description: Reads the EEPROM and returns the Data.
 *
 * Arguments:
 *		Adapter    - ptr to Adapter object instance
 *		pBuffer    - Buffer to store the data read from EEPROM
 *		uiOffset   - Offset of EEPROM from where data should be read
 *		uiNumBytes - Number of bytes to be read from the EEPROM.
 *
 * Returns:
 *		OSAL_STATUS_SUCCESS - if EEPROM read is successful.
 *		<FAILURE>			- if failed.
 */

int BeceemEEPROMBulkRead(struct bcm_mini_adapter *Adapter,
			PUINT pBuffer,
			unsigned int uiOffset,
			unsigned int uiNumBytes)
{
	unsigned int uiData[4]		= {0};
	/* unsigned int uiAddress	= 0; */
	unsigned int uiBytesRemaining	= uiNumBytes;
	unsigned int uiIndex		= 0;
	unsigned int uiTempOffset	= 0;
	unsigned int uiExtraBytes	= 0;
	unsigned int uiFailureRetries	= 0;
	PUCHAR pcBuff = (PUCHAR)pBuffer;

	if (uiOffset % MAX_RW_SIZE && uiBytesRemaining) {
		uiTempOffset = uiOffset - (uiOffset % MAX_RW_SIZE);
		uiExtraBytes = uiOffset - uiTempOffset;
		ReadBeceemEEPROMBulk(Adapter, uiTempOffset, (PUINT)&uiData[0], 4);
		if (uiBytesRemaining >= (MAX_RW_SIZE - uiExtraBytes)) {
			memcpy(pBuffer, (((PUCHAR)&uiData[0]) + uiExtraBytes), MAX_RW_SIZE - uiExtraBytes);
			uiBytesRemaining -= (MAX_RW_SIZE - uiExtraBytes);
			uiIndex += (MAX_RW_SIZE - uiExtraBytes);
			uiOffset += (MAX_RW_SIZE - uiExtraBytes);
		} else {
			memcpy(pBuffer, (((PUCHAR)&uiData[0]) + uiExtraBytes), uiBytesRemaining);
			uiIndex += uiBytesRemaining;
			uiOffset += uiBytesRemaining;
			uiBytesRemaining = 0;
		}
	}

	while (uiBytesRemaining && uiFailureRetries != 128) {
		if (Adapter->device_removed)
			return -1;

		if (uiBytesRemaining >= MAX_RW_SIZE) {
			/* For the requests more than or equal to 16 bytes, use bulk
			 * read function to make the access faster.
			 * We read 4 Dwords of data
			 */
			if (ReadBeceemEEPROMBulk(Adapter, uiOffset, &uiData[0], 4) == 0) {
				memcpy(pcBuff + uiIndex, &uiData[0], MAX_RW_SIZE);
				uiOffset += MAX_RW_SIZE;
				uiBytesRemaining -= MAX_RW_SIZE;
				uiIndex += MAX_RW_SIZE;
			} else {
				uiFailureRetries++;
				mdelay(3); /* sleep for a while before retry... */
			}
		} else if (uiBytesRemaining >= 4) {
			if (ReadBeceemEEPROM(Adapter, uiOffset, &uiData[0]) == 0) {
				memcpy(pcBuff + uiIndex, &uiData[0], 4);
				uiOffset += 4;
				uiBytesRemaining -= 4;
				uiIndex += 4;
			} else {
				uiFailureRetries++;
				mdelay(3); /* sleep for a while before retry... */
			}
		} else {
			/* Handle the reads less than 4 bytes... */
			PUCHAR pCharBuff = (PUCHAR)pBuffer;
			pCharBuff += uiIndex;
			if (ReadBeceemEEPROM(Adapter, uiOffset, &uiData[0]) == 0) {
				memcpy(pCharBuff, &uiData[0], uiBytesRemaining); /* copy only bytes requested. */
				uiBytesRemaining = 0;
			} else {
				uiFailureRetries++;
				mdelay(3); /* sleep for a while before retry... */
			}
		}
	}

	return 0;
}

/*
 * Procedure:	BeceemFlashBulkRead
 *
 * Description: Reads the FLASH and returns the Data.
 *
 * Arguments:
 *		Adapter    - ptr to Adapter object instance
 *		pBuffer    - Buffer to store the data read from FLASH
 *		uiOffset   - Offset of FLASH from where data should be read
 *		uiNumBytes - Number of bytes to be read from the FLASH.
 *
 * Returns:
 *		OSAL_STATUS_SUCCESS - if FLASH read is successful.
 *		<FAILURE>			- if failed.
 */

static int BeceemFlashBulkRead(struct bcm_mini_adapter *Adapter,
			PUINT pBuffer,
			unsigned int uiOffset,
			unsigned int uiNumBytes)
{
	unsigned int uiIndex = 0;
	unsigned int uiBytesToRead = uiNumBytes;
	int Status = 0;
	unsigned int uiPartOffset = 0;
	int bytes;

	if (Adapter->device_removed) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Device Got Removed");
		return -ENODEV;
	}

	/* Adding flash Base address
	 * uiOffset = uiOffset + GetFlashBaseAddr(Adapter);
	 */
	#if defined(BCM_SHM_INTERFACE) && !defined(FLASH_DIRECT_ACCESS)
		Status = bcmflash_raw_read((uiOffset/FLASH_PART_SIZE), (uiOffset % FLASH_PART_SIZE), (unsigned char *)pBuffer, uiNumBytes);
		return Status;
	#endif

	Adapter->SelectedChip = RESET_CHIP_SELECT;

	if (uiOffset % MAX_RW_SIZE) {
		BcmDoChipSelect(Adapter, uiOffset);
		uiPartOffset = (uiOffset & (FLASH_PART_SIZE - 1)) + GetFlashBaseAddr(Adapter);

		uiBytesToRead = MAX_RW_SIZE - (uiOffset % MAX_RW_SIZE);
		uiBytesToRead = MIN(uiNumBytes, uiBytesToRead);

		bytes = rdm(Adapter, uiPartOffset, (PCHAR)pBuffer + uiIndex, uiBytesToRead);
		if (bytes < 0) {
			Status = bytes;
			Adapter->SelectedChip = RESET_CHIP_SELECT;
			return Status;
		}

		uiIndex += uiBytesToRead;
		uiOffset += uiBytesToRead;
		uiNumBytes -= uiBytesToRead;
	}

	while (uiNumBytes) {
		BcmDoChipSelect(Adapter, uiOffset);
		uiPartOffset = (uiOffset & (FLASH_PART_SIZE - 1)) + GetFlashBaseAddr(Adapter);

		uiBytesToRead = MIN(uiNumBytes, MAX_RW_SIZE);

		bytes = rdm(Adapter, uiPartOffset, (PCHAR)pBuffer + uiIndex, uiBytesToRead);
		if (bytes < 0) {
			Status = bytes;
			break;
		}

		uiIndex += uiBytesToRead;
		uiOffset += uiBytesToRead;
		uiNumBytes -= uiBytesToRead;
	}
	Adapter->SelectedChip = RESET_CHIP_SELECT;
	return Status;
}

/*
 * Procedure:	BcmGetFlashSize
 *
 * Description: Finds the size of FLASH.
 *
 * Arguments:
 *		Adapter    - ptr to Adapter object instance
 *
 * Returns:
 *		unsigned int - size of the FLASH Storage.
 *
 */

static unsigned int BcmGetFlashSize(struct bcm_mini_adapter *Adapter)
{
	if (IsFlash2x(Adapter))
		return Adapter->psFlash2xCSInfo->OffsetFromDSDStartForDSDHeader + sizeof(struct bcm_dsd_header);
	else
		return 32 * 1024;
}

/*
 * Procedure:	BcmGetEEPROMSize
 *
 * Description: Finds the size of EEPROM.
 *
 * Arguments:
 *		Adapter    - ptr to Adapter object instance
 *
 * Returns:
 *		unsigned int - size of the EEPROM Storage.
 *
 */

static unsigned int BcmGetEEPROMSize(struct bcm_mini_adapter *Adapter)
{
	unsigned int uiData = 0;
	unsigned int uiIndex = 0;

	/*
	 * if EEPROM is present and already Calibrated,it will have
	 * 'BECM' string at 0th offset.
	 * To find the EEPROM size read the possible boundaries of the
	 * EEPROM like 4K,8K etc..accessing the EEPROM beyond its size will
	 * result in wrap around. So when we get the End of the EEPROM we will
	 * get 'BECM' string which is indeed at offset 0.
	 */
	BeceemEEPROMBulkRead(Adapter, &uiData, 0x0, 4);
	if (uiData == BECM) {
		for (uiIndex = 2; uiIndex <= 256; uiIndex *= 2)	{
			BeceemEEPROMBulkRead(Adapter, &uiData, uiIndex * 1024, 4);
			if (uiData == BECM)
				return uiIndex * 1024;
		}
	} else {
		/*
		 * EEPROM may not be present or not programmed
		 */
		uiData = 0xBABEFACE;
		if (BeceemEEPROMBulkWrite(Adapter, (PUCHAR)&uiData, 0, 4, TRUE) == 0) {
			uiData = 0;
			for (uiIndex = 2; uiIndex <= 256; uiIndex *= 2) {
				BeceemEEPROMBulkRead(Adapter, &uiData, uiIndex * 1024, 4);
				if (uiData == 0xBABEFACE)
					return uiIndex * 1024;
			}
		}
	}
	return 0;
}

/*
 * Procedure:	FlashSectorErase
 *
 * Description: Finds the sector size of the FLASH.
 *
 * Arguments:
 *		Adapter    - ptr to Adapter object instance
 *		addr	   - sector start address
 *		numOfSectors - number of sectors to  be erased.
 *
 * Returns:
 *		OSAL_STATUS_CODE
 *
 */

static int FlashSectorErase(struct bcm_mini_adapter *Adapter,
			unsigned int addr,
			unsigned int numOfSectors)
{
	unsigned int iIndex = 0, iRetries = 0;
	unsigned int uiStatus = 0;
	unsigned int value;
	int bytes;

	for (iIndex = 0; iIndex < numOfSectors; iIndex++) {
		value = 0x06000000;
		wrmalt(Adapter, FLASH_SPI_CMDQ_REG, &value, sizeof(value));

		value = (0xd8000000 | (addr & 0xFFFFFF));
		wrmalt(Adapter, FLASH_SPI_CMDQ_REG, &value, sizeof(value));
		iRetries = 0;

		do {
			value = (FLASH_CMD_STATUS_REG_READ << 24);
			if (wrmalt(Adapter, FLASH_SPI_CMDQ_REG, &value, sizeof(value)) < 0) {
				BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Programing of FLASH_SPI_CMDQ_REG fails");
				return STATUS_FAILURE;
			}

			bytes = rdmalt(Adapter, FLASH_SPI_READQ_REG, &uiStatus, sizeof(uiStatus));
			if (bytes < 0) {
				uiStatus = bytes;
				BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Reading status of FLASH_SPI_READQ_REG fails");
				return uiStatus;
			}
			iRetries++;
			/* After every try lets make the CPU free for 10 ms. generally time taken by the
			 * the sector erase cycle is 500 ms to 40000 msec. hence sleeping 10 ms
			 * won't hamper performance in any case.
			 */
			mdelay(10);
		} while ((uiStatus & 0x1) && (iRetries < 400));

		if (uiStatus & 0x1) {
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "iRetries crossing the limit of 80000\n");
			return STATUS_FAILURE;
		}

		addr += Adapter->uiSectorSize;
	}
	return 0;
}
/*
 * Procedure:	flashByteWrite
 *
 * Description: Performs Byte by Byte write to flash
 *
 * Arguments:
 *		Adapter   - ptr to Adapter object instance
 *		uiOffset   - Offset of the flash where data needs to be written to.
 *		pData	- Address of Data to be written.
 * Returns:
 *		OSAL_STATUS_CODE
 *
 */

static int flashByteWrite(struct bcm_mini_adapter *Adapter,
			unsigned int uiOffset,
			PVOID pData)
{
	unsigned int uiStatus = 0;
	int  iRetries = MAX_FLASH_RETRIES * FLASH_PER_RETRIES_DELAY; /* 3 */
	unsigned int value;
	ULONG ulData = *(PUCHAR)pData;
	int bytes;
	/*
	 * need not write 0xFF because write requires an erase and erase will
	 * make whole sector 0xFF.
	 */

	if (0xFF == ulData)
		return STATUS_SUCCESS;

	/* DumpDebug(NVM_RW,("flashWrite ====>\n")); */
	value = (FLASH_CMD_WRITE_ENABLE << 24);
	if (wrmalt(Adapter, FLASH_SPI_CMDQ_REG, &value, sizeof(value)) < 0) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Write enable in FLASH_SPI_CMDQ_REG register fails");
		return STATUS_FAILURE;
	}

	if (wrm(Adapter, FLASH_SPI_WRITEQ_REG, (PCHAR)&ulData, 4) < 0) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "DATA Write on FLASH_SPI_WRITEQ_REG fails");
		return STATUS_FAILURE;
	}
	value = (0x02000000 | (uiOffset & 0xFFFFFF));
	if (wrmalt(Adapter, FLASH_SPI_CMDQ_REG, &value, sizeof(value)) < 0) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Programming of FLASH_SPI_CMDQ_REG fails");
		return STATUS_FAILURE;
	}

	/* __udelay(950); */

	do {
		value = (FLASH_CMD_STATUS_REG_READ << 24);
		if (wrmalt(Adapter, FLASH_SPI_CMDQ_REG, &value, sizeof(value)) < 0) {
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Programing of FLASH_SPI_CMDQ_REG fails");
			return STATUS_FAILURE;
		}
		/* __udelay(1); */
		bytes = rdmalt(Adapter, FLASH_SPI_READQ_REG, &uiStatus, sizeof(uiStatus));
		if (bytes < 0) {
			uiStatus = bytes;
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Reading status of FLASH_SPI_READQ_REG fails");
			return uiStatus;
		}
		iRetries--;
		if (iRetries && ((iRetries % FLASH_PER_RETRIES_DELAY) == 0))
			udelay(1000);

	} while ((uiStatus & 0x1) && (iRetries  > 0));

	if (uiStatus & 0x1) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Flash Write fails even after checking status for 200 times.");
		return STATUS_FAILURE;
	}

	return STATUS_SUCCESS;
}

/*
 * Procedure:	flashWrite
 *
 * Description: Performs write to flash
 *
 * Arguments:
 *		Adapter    - ptr to Adapter object instance
 *		uiOffset   - Offset of the flash where data needs to be written to.
 *		pData	- Address of Data to be written.
 * Returns:
 *		OSAL_STATUS_CODE
 *
 */

static int flashWrite(struct bcm_mini_adapter *Adapter,
		unsigned int uiOffset,
		PVOID pData)
{
	/* unsigned int uiStatus = 0;
	 * int  iRetries = 0;
	 * unsigned int uiReadBack = 0;
	 */
	unsigned int uiStatus = 0;
	int  iRetries = MAX_FLASH_RETRIES * FLASH_PER_RETRIES_DELAY; /* 3 */
	unsigned int value;
	unsigned int uiErasePattern[4] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
	int bytes;
	/*
	 * need not write 0xFFFFFFFF because write requires an erase and erase will
	 * make whole sector 0xFFFFFFFF.
	 */
	if (!memcmp(pData, uiErasePattern, MAX_RW_SIZE))
		return 0;

	value = (FLASH_CMD_WRITE_ENABLE << 24);

	if (wrmalt(Adapter, FLASH_SPI_CMDQ_REG, &value, sizeof(value)) < 0) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Write Enable of FLASH_SPI_CMDQ_REG fails");
		return STATUS_FAILURE;
	}

	if (wrm(Adapter, uiOffset, (PCHAR)pData, MAX_RW_SIZE) < 0) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Data write fails...");
		return STATUS_FAILURE;
	}

	/* __udelay(950); */
	do {
		value = (FLASH_CMD_STATUS_REG_READ << 24);
		if (wrmalt(Adapter, FLASH_SPI_CMDQ_REG, &value, sizeof(value)) < 0) {
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Programing of FLASH_SPI_CMDQ_REG fails");
			return STATUS_FAILURE;
		}
		/* __udelay(1); */
		bytes = rdmalt(Adapter, FLASH_SPI_READQ_REG, &uiStatus, sizeof(uiStatus));
		if (bytes < 0) {
			uiStatus = bytes;
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Reading status of FLASH_SPI_READQ_REG fails");
			return uiStatus;
		}

		iRetries--;
		/* this will ensure that in there will be no changes in the current path.
		 * currently one rdm/wrm takes 125 us.
		 * Hence  125 *2 * FLASH_PER_RETRIES_DELAY > 3 ms(worst case delay)
		 * Hence current implementation cycle will intoduce no delay in current path
		 */
		if (iRetries && ((iRetries % FLASH_PER_RETRIES_DELAY) == 0))
			udelay(1000);
	} while ((uiStatus & 0x1) && (iRetries > 0));

	if (uiStatus & 0x1) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Flash Write fails even after checking status for 200 times.");
		return STATUS_FAILURE;
	}

	return STATUS_SUCCESS;
}

/*-----------------------------------------------------------------------------
 * Procedure:	flashByteWriteStatus
 *
 * Description: Performs byte by byte write to flash with write done status check
 *
 * Arguments:
 *		Adapter    - ptr to Adapter object instance
 *		uiOffset    - Offset of the flash where data needs to be written to.
 *		pData	 - Address of the Data to be written.
 * Returns:
 *		OSAL_STATUS_CODE
 *
 */
static int flashByteWriteStatus(struct bcm_mini_adapter *Adapter,
				unsigned int uiOffset,
				PVOID pData)
{
	unsigned int uiStatus = 0;
	int  iRetries = MAX_FLASH_RETRIES * FLASH_PER_RETRIES_DELAY; /* 3 */
	ULONG ulData  = *(PUCHAR)pData;
	unsigned int value;
	int bytes;

	/*
	 * need not write 0xFFFFFFFF because write requires an erase and erase will
	 * make whole sector 0xFFFFFFFF.
	 */

	if (0xFF == ulData)
		return STATUS_SUCCESS;

	/* DumpDebug(NVM_RW,("flashWrite ====>\n")); */

	value = (FLASH_CMD_WRITE_ENABLE << 24);
	if (wrmalt(Adapter, FLASH_SPI_CMDQ_REG, &value, sizeof(value)) < 0) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Write enable in FLASH_SPI_CMDQ_REG register fails");
		return STATUS_SUCCESS;
	}
	if (wrm(Adapter, FLASH_SPI_WRITEQ_REG, (PCHAR)&ulData, 4) < 0) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "DATA Write on FLASH_SPI_WRITEQ_REG fails");
		return STATUS_FAILURE;
	}
	value = (0x02000000 | (uiOffset & 0xFFFFFF));
	if (wrmalt(Adapter, FLASH_SPI_CMDQ_REG, &value, sizeof(value)) < 0) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Programming of FLASH_SPI_CMDQ_REG fails");
		return STATUS_FAILURE;
	}

	/* msleep(1); */

	do {
		value = (FLASH_CMD_STATUS_REG_READ << 24);
		if (wrmalt(Adapter, FLASH_SPI_CMDQ_REG, &value, sizeof(value)) < 0) {
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Programing of FLASH_SPI_CMDQ_REG fails");
			return STATUS_FAILURE;
		}
		/* __udelay(1); */
		bytes = rdmalt(Adapter, FLASH_SPI_READQ_REG, &uiStatus, sizeof(uiStatus));
		if (bytes < 0) {
			uiStatus = bytes;
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Reading status of FLASH_SPI_READQ_REG fails");
			return uiStatus;
		}

		iRetries--;
		if (iRetries && ((iRetries % FLASH_PER_RETRIES_DELAY) == 0))
			udelay(1000);

	} while ((uiStatus & 0x1) && (iRetries > 0));

	if (uiStatus & 0x1) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Flash Write fails even after checking status for 200 times.");
		return STATUS_FAILURE;
	}

	return STATUS_SUCCESS;
}
/*
 * Procedure:	flashWriteStatus
 *
 * Description: Performs write to flash with write done status check
 *
 * Arguments:
 *		Adapter    - ptr to Adapter object instance
 *		uiOffset    - Offset of the flash where data needs to be written to.
 *		pData	 - Address of the Data to be written.
 * Returns:
 *		OSAL_STATUS_CODE
 *
 */

static int flashWriteStatus(struct bcm_mini_adapter *Adapter,
			unsigned int uiOffset,
			PVOID pData)
{
	unsigned int uiStatus = 0;
	int  iRetries = MAX_FLASH_RETRIES * FLASH_PER_RETRIES_DELAY; /* 3 */
	/* unsigned int uiReadBack = 0; */
	unsigned int value;
	unsigned int uiErasePattern[4] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
	int bytes;

	/*
	 * need not write 0xFFFFFFFF because write requires an erase and erase will
	 * make whole sector 0xFFFFFFFF.
	 */
	if (!memcmp(pData, uiErasePattern, MAX_RW_SIZE))
		return 0;

	value = (FLASH_CMD_WRITE_ENABLE << 24);
	if (wrmalt(Adapter, FLASH_SPI_CMDQ_REG, &value, sizeof(value)) < 0) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Write Enable of FLASH_SPI_CMDQ_REG fails");
		return STATUS_FAILURE;
	}

	if (wrm(Adapter, uiOffset, (PCHAR)pData, MAX_RW_SIZE) < 0) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Data write fails...");
		return STATUS_FAILURE;
	}
	/* __udelay(1); */

	do {
		value = (FLASH_CMD_STATUS_REG_READ << 24);
		if (wrmalt(Adapter, FLASH_SPI_CMDQ_REG, &value, sizeof(value)) < 0) {
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Programing of FLASH_SPI_CMDQ_REG fails");
			return STATUS_FAILURE;
		}
		/* __udelay(1); */
		bytes = rdmalt(Adapter, FLASH_SPI_READQ_REG, &uiStatus, sizeof(uiStatus));
		if (bytes < 0) {
			uiStatus = bytes;
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Reading status of FLASH_SPI_READQ_REG fails");
			return uiStatus;
		}
		iRetries--;
		/* this will ensure that in there will be no changes in the current path.
		 * currently one rdm/wrm takes 125 us.
		 * Hence  125 *2  * FLASH_PER_RETRIES_DELAY  >3 ms(worst case delay)
		 * Hence current implementation cycle will intoduce no delay in current path
		 */
		if (iRetries && ((iRetries % FLASH_PER_RETRIES_DELAY) == 0))
			udelay(1000);

	} while ((uiStatus & 0x1) && (iRetries > 0));

	if (uiStatus & 0x1) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Flash Write fails even after checking status for 200 times.");
		return STATUS_FAILURE;
	}

	return STATUS_SUCCESS;
}

/*
 * Procedure:	BcmRestoreBlockProtectStatus
 *
 * Description: Restores the original block protection status.
 *
 * Arguments:
 *		Adapter    - ptr to Adapter object instance
 *		ulWriteStatus   -Original status
 * Returns:
 *		<VOID>
 *
 */

static VOID BcmRestoreBlockProtectStatus(struct bcm_mini_adapter *Adapter, ULONG ulWriteStatus)
{
	unsigned int value;
	value = (FLASH_CMD_WRITE_ENABLE << 24);
	wrmalt(Adapter, FLASH_SPI_CMDQ_REG, &value, sizeof(value));

	udelay(20);
	value = (FLASH_CMD_STATUS_REG_WRITE << 24) | (ulWriteStatus << 16);
	wrmalt(Adapter, FLASH_SPI_CMDQ_REG, &value, sizeof(value));
	udelay(20);
}

/*
 * Procedure:	BcmFlashUnProtectBlock
 *
 * Description: UnProtects appropriate blocks for writing.
 *
 * Arguments:
 *		Adapter    - ptr to Adapter object instance
 *		uiOffset   - Offset of the flash where data needs to be written to. This should be Sector aligned.
 * Returns:
 *		ULONG   - Status value before UnProtect.
 *
 */

static ULONG BcmFlashUnProtectBlock(struct bcm_mini_adapter *Adapter, unsigned int uiOffset, unsigned int uiLength)
{
	ULONG ulStatus		= 0;
	ULONG ulWriteStatus	= 0;
	unsigned int value;

	uiOffset = uiOffset&0x000FFFFF;
	/*
	 * Implemented only for 1MB Flash parts.
	 */
	if (FLASH_PART_SST25VF080B == Adapter->ulFlashID) {
		/*
		 * Get Current BP status.
		 */
		value = (FLASH_CMD_STATUS_REG_READ << 24);
		wrmalt(Adapter, FLASH_SPI_CMDQ_REG, &value, sizeof(value));
		udelay(10);
		/*
		 * Read status will be WWXXYYZZ. We have to take only WW.
		 */
		rdmalt(Adapter, FLASH_SPI_READQ_REG, (PUINT)&ulStatus, sizeof(ulStatus));
		ulStatus >>= 24;
		ulWriteStatus = ulStatus;
		/*
		 * Bits [5-2] give current block level protection status.
		 * Bit5: BP3 - DONT CARE
		 * BP2-BP0: 0 - NO PROTECTION, 1 - UPPER 1/16, 2 - UPPER 1/8, 3 - UPPER 1/4
		 *                4 - UPPER 1/2. 5 to 7 - ALL BLOCKS
		 */

		if (ulStatus) {
			if ((uiOffset+uiLength) <= 0x80000) {
				/*
				 * Offset comes in lower half of 1MB. Protect the upper half.
				 * Clear BP1 and BP0 and set BP2.
				 */
				ulWriteStatus |= (0x4<<2);
				ulWriteStatus &= ~(0x3<<2);
			} else if ((uiOffset + uiLength) <= 0xC0000) {
				/*
				 * Offset comes below Upper 1/4. Upper 1/4 can be protected.
				 *  Clear BP2 and set BP1 and BP0.
				 */
				ulWriteStatus |= (0x3<<2);
				ulWriteStatus &= ~(0x1<<4);
			} else if ((uiOffset + uiLength) <= 0xE0000) {
				/*
				 * Offset comes below Upper 1/8. Upper 1/8 can be protected.
				 * Clear BP2 and BP0  and set BP1
				 */
				ulWriteStatus |= (0x1<<3);
				ulWriteStatus &= ~(0x5<<2);
			} else if ((uiOffset + uiLength) <= 0xF0000) {
				/*
				 * Offset comes below Upper 1/16. Only upper 1/16 can be protected.
				 * Set BP0 and Clear BP2,BP1.
				 */
				ulWriteStatus |= (0x1<<2);
				ulWriteStatus &= ~(0x3<<3);
			} else {
				/*
				 * Unblock all.
				 * Clear BP2,BP1 and BP0.
				 */
				ulWriteStatus &= ~(0x7<<2);
			}

			value = (FLASH_CMD_WRITE_ENABLE << 24);
			wrmalt(Adapter, FLASH_SPI_CMDQ_REG, &value, sizeof(value));
			udelay(20);
			value = (FLASH_CMD_STATUS_REG_WRITE << 24) | (ulWriteStatus << 16);
			wrmalt(Adapter, FLASH_SPI_CMDQ_REG, &value, sizeof(value));
			udelay(20);
		}
	}
	return ulStatus;
}

/*
 * Procedure:	BeceemFlashBulkWrite
 *
 * Description: Performs write to the flash
 *
 * Arguments:
 *		Adapter    - ptr to Adapter object instance
 * pBuffer - Data to be written.
 *		uiOffset   - Offset of the flash where data needs to be written to.
 *		uiNumBytes - Number of bytes to be written.
 *		bVerify    - read verify flag.
 * Returns:
 *		OSAL_STATUS_CODE
 *
 */

static int BeceemFlashBulkWrite(struct bcm_mini_adapter *Adapter,
				PUINT pBuffer,
				unsigned int uiOffset,
				unsigned int uiNumBytes,
				bool bVerify)
{
	PCHAR pTempBuff			= NULL;
	PUCHAR pcBuffer			= (PUCHAR)pBuffer;
	unsigned int uiIndex			= 0;
	unsigned int uiOffsetFromSectStart	= 0;
	unsigned int uiSectAlignAddr		= 0;
	unsigned int uiCurrSectOffsetAddr	= 0;
	unsigned int uiSectBoundary		= 0;
	unsigned int uiNumSectTobeRead		= 0;
	UCHAR ucReadBk[16]		= {0};
	ULONG ulStatus			= 0;
	int Status			= STATUS_SUCCESS;
	unsigned int uiTemp			= 0;
	unsigned int index			= 0;
	unsigned int uiPartOffset		= 0;

	#if defined(BCM_SHM_INTERFACE) && !defined(FLASH_DIRECT_ACCESS)
		Status = bcmflash_raw_write((uiOffset / FLASH_PART_SIZE), (uiOffset % FLASH_PART_SIZE), (unsigned char *)pBuffer, uiNumBytes);
		return Status;
	#endif

	uiOffsetFromSectStart = uiOffset & ~(Adapter->uiSectorSize - 1);

	/* Adding flash Base address
	 * uiOffset = uiOffset + GetFlashBaseAddr(Adapter);
	 */

	uiSectAlignAddr	= uiOffset & ~(Adapter->uiSectorSize - 1);
	uiCurrSectOffsetAddr = uiOffset & (Adapter->uiSectorSize - 1);
	uiSectBoundary = uiSectAlignAddr + Adapter->uiSectorSize;

	pTempBuff = kmalloc(Adapter->uiSectorSize, GFP_KERNEL);
	if (!pTempBuff)
		goto BeceemFlashBulkWrite_EXIT;
	/*
	 * check if the data to be written is overlapped across sectors
	 */
	if (uiOffset+uiNumBytes < uiSectBoundary) {
		uiNumSectTobeRead = 1;
	} else {
		/* Number of sectors  = Last sector start address/First sector start address */
		uiNumSectTobeRead =  (uiCurrSectOffsetAddr + uiNumBytes) / Adapter->uiSectorSize;
		if ((uiCurrSectOffsetAddr + uiNumBytes)%Adapter->uiSectorSize)
			uiNumSectTobeRead++;
	}
	/* Check whether Requested sector is writable or not in case of flash2x write. But if  write call is
	 * for DSD calibration, allow it without checking of sector permission
	 */

	if (IsFlash2x(Adapter) && (Adapter->bAllDSDWriteAllow == false)) {
		index = 0;
		uiTemp = uiNumSectTobeRead;
		while (uiTemp) {
			if (IsOffsetWritable(Adapter, uiOffsetFromSectStart + index * Adapter->uiSectorSize) == false) {
				BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Sector Starting at offset <0X%X> is not writable",
						(uiOffsetFromSectStart + index * Adapter->uiSectorSize));
				Status = SECTOR_IS_NOT_WRITABLE;
				goto BeceemFlashBulkWrite_EXIT;
			}
			uiTemp = uiTemp - 1;
			index = index + 1;
		}
	}
	Adapter->SelectedChip = RESET_CHIP_SELECT;
	while (uiNumSectTobeRead) {
		/* do_gettimeofday(&tv1);
		 * BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "\nTime In start of write :%ld ms\n",(tv1.tv_sec *1000 + tv1.tv_usec /1000));
		 */
		uiPartOffset = (uiSectAlignAddr & (FLASH_PART_SIZE - 1)) + GetFlashBaseAddr(Adapter);

		BcmDoChipSelect(Adapter, uiSectAlignAddr);

		if (0 != BeceemFlashBulkRead(Adapter,
						(PUINT)pTempBuff,
						uiOffsetFromSectStart,
						Adapter->uiSectorSize)) {
			Status = -1;
			goto BeceemFlashBulkWrite_EXIT;
		}

		/* do_gettimeofday(&tr);
		 * BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "Total time taken by Read :%ld ms\n", (tr.tv_sec *1000 + tr.tv_usec/1000) - (tv1.tv_sec *1000 + tv1.tv_usec/1000));
		 */
		ulStatus = BcmFlashUnProtectBlock(Adapter, uiSectAlignAddr, Adapter->uiSectorSize);

		if (uiNumSectTobeRead > 1) {
			memcpy(&pTempBuff[uiCurrSectOffsetAddr], pcBuffer, uiSectBoundary - (uiSectAlignAddr + uiCurrSectOffsetAddr));
			pcBuffer += ((uiSectBoundary - (uiSectAlignAddr + uiCurrSectOffsetAddr)));
			uiNumBytes -= (uiSectBoundary - (uiSectAlignAddr + uiCurrSectOffsetAddr));
		} else {
			memcpy(&pTempBuff[uiCurrSectOffsetAddr], pcBuffer, uiNumBytes);
		}

		if (IsFlash2x(Adapter))
			SaveHeaderIfPresent(Adapter, (PUCHAR)pTempBuff, uiOffsetFromSectStart);

		FlashSectorErase(Adapter, uiPartOffset, 1);
		/* do_gettimeofday(&te);
		 * BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "Total time taken by Erase :%ld ms\n", (te.tv_sec *1000 + te.tv_usec/1000) - (tr.tv_sec *1000 + tr.tv_usec/1000));
		 */
		for (uiIndex = 0; uiIndex < Adapter->uiSectorSize; uiIndex += Adapter->ulFlashWriteSize) {
			if (Adapter->device_removed) {
				Status = -1;
				goto BeceemFlashBulkWrite_EXIT;
			}

			if (STATUS_SUCCESS != (*Adapter->fpFlashWrite)(Adapter, uiPartOffset + uiIndex, (&pTempBuff[uiIndex]))) {
				Status = -1;
				goto BeceemFlashBulkWrite_EXIT;
			}
		}

		/* do_gettimeofday(&tw);
		 * BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "Total time taken in Write  to Flash :%ld ms\n", (tw.tv_sec *1000 + tw.tv_usec/1000) - (te.tv_sec *1000 + te.tv_usec/1000));
		 */
		for (uiIndex = 0; uiIndex < Adapter->uiSectorSize; uiIndex += MAX_RW_SIZE) {
			if (STATUS_SUCCESS == BeceemFlashBulkRead(Adapter, (PUINT)ucReadBk, uiOffsetFromSectStart + uiIndex, MAX_RW_SIZE)) {
				if (Adapter->ulFlashWriteSize == 1) {
					unsigned int uiReadIndex = 0;
					for (uiReadIndex = 0; uiReadIndex < 16; uiReadIndex++) {
						if (ucReadBk[uiReadIndex] != pTempBuff[uiIndex + uiReadIndex]) {
							if (STATUS_SUCCESS != (*Adapter->fpFlashWriteWithStatusCheck)(Adapter, uiPartOffset + uiIndex + uiReadIndex, &pTempBuff[uiIndex+uiReadIndex])) {
								Status = STATUS_FAILURE;
								goto BeceemFlashBulkWrite_EXIT;
							}
						}
					}
				} else {
					if (memcmp(ucReadBk, &pTempBuff[uiIndex], MAX_RW_SIZE)) {
						if (STATUS_SUCCESS != (*Adapter->fpFlashWriteWithStatusCheck)(Adapter, uiPartOffset + uiIndex, &pTempBuff[uiIndex])) {
							Status = STATUS_FAILURE;
							goto BeceemFlashBulkWrite_EXIT;
						}
					}
				}
			}
		}
		/* do_gettimeofday(&twv);
		 * BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "Total time taken in Write  to Flash verification :%ld ms\n", (twv.tv_sec *1000 + twv.tv_usec/1000) - (tw.tv_sec *1000 + tw.tv_usec/1000));
		 */
		if (ulStatus) {
			BcmRestoreBlockProtectStatus(Adapter, ulStatus);
			ulStatus = 0;
		}

		uiCurrSectOffsetAddr = 0;
		uiSectAlignAddr = uiSectBoundary;
		uiSectBoundary += Adapter->uiSectorSize;
		uiOffsetFromSectStart += Adapter->uiSectorSize;
		uiNumSectTobeRead--;
	}
	/* do_gettimeofday(&tv2);
	 * BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "Time after Write :%ld ms\n",(tv2.tv_sec *1000 + tv2.tv_usec/1000));
	 * BCM_DEBUG_PRINT(Adapter,DBG_TYPE_PRINTK, 0, 0, "Total time taken by in Write is :%ld ms\n", (tv2.tv_sec *1000 + tv2.tv_usec/1000) - (tv1.tv_sec *1000 + tv1.tv_usec/1000));
	 *
	 * Cleanup.
	 */
BeceemFlashBulkWrite_EXIT:
	if (ulStatus)
		BcmRestoreBlockProtectStatus(Adapter, ulStatus);

	kfree(pTempBuff);

	Adapter->SelectedChip = RESET_CHIP_SELECT;
	return Status;
}

/*
 * Procedure:	BeceemFlashBulkWriteStatus
 *
 * Description: Writes to Flash. Checks the SPI status after each write.
 *
 * Arguments:
 *		Adapter		- ptr to Adapter object instance
 *		pBuffer		- Data to be written.
 *		uiOffset	- Offset of the flash where data needs to be written to.
 *		uiNumBytes	- Number of bytes to be written.
 *		bVerify		- read verify flag.
 * Returns:
 *		OSAL_STATUS_CODE
 *
 */

static int BeceemFlashBulkWriteStatus(struct bcm_mini_adapter *Adapter,
				PUINT pBuffer,
				unsigned int uiOffset,
				unsigned int uiNumBytes,
				bool bVerify)
{
	PCHAR pTempBuff			= NULL;
	PUCHAR pcBuffer			= (PUCHAR)pBuffer;
	unsigned int uiIndex			= 0;
	unsigned int uiOffsetFromSectStart	= 0;
	unsigned int uiSectAlignAddr		= 0;
	unsigned int uiCurrSectOffsetAddr	= 0;
	unsigned int uiSectBoundary		= 0;
	unsigned int uiNumSectTobeRead		= 0;
	UCHAR ucReadBk[16]		= {0};
	ULONG ulStatus			= 0;
	unsigned int Status			= STATUS_SUCCESS;
	unsigned int uiTemp			= 0;
	unsigned int index			= 0;
	unsigned int uiPartOffset		= 0;

	uiOffsetFromSectStart = uiOffset & ~(Adapter->uiSectorSize - 1);

	/* uiOffset += Adapter->ulFlashCalStart;
	 * Adding flash Base address
	 * uiOffset = uiOffset + GetFlashBaseAddr(Adapter);
	 */
	uiSectAlignAddr = uiOffset & ~(Adapter->uiSectorSize - 1);
	uiCurrSectOffsetAddr = uiOffset & (Adapter->uiSectorSize - 1);
	uiSectBoundary = uiSectAlignAddr + Adapter->uiSectorSize;

	pTempBuff = kmalloc(Adapter->uiSectorSize, GFP_KERNEL);
	if (!pTempBuff)
		goto BeceemFlashBulkWriteStatus_EXIT;

	/*
	 * check if the data to be written is overlapped across sectors
	 */
	if (uiOffset+uiNumBytes < uiSectBoundary) {
		uiNumSectTobeRead = 1;
	} else {
		/* Number of sectors  = Last sector start address/First sector start address */
		uiNumSectTobeRead =  (uiCurrSectOffsetAddr + uiNumBytes) / Adapter->uiSectorSize;
		if ((uiCurrSectOffsetAddr + uiNumBytes)%Adapter->uiSectorSize)
			uiNumSectTobeRead++;
	}

	if (IsFlash2x(Adapter) && (Adapter->bAllDSDWriteAllow == false)) {
		index = 0;
		uiTemp = uiNumSectTobeRead;
		while (uiTemp) {
			if (IsOffsetWritable(Adapter, uiOffsetFromSectStart + index * Adapter->uiSectorSize) == false) {
				BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Sector Starting at offset <0X%x> is not writable",
						(uiOffsetFromSectStart + index * Adapter->uiSectorSize));
				Status = SECTOR_IS_NOT_WRITABLE;
				goto BeceemFlashBulkWriteStatus_EXIT;
			}
			uiTemp = uiTemp - 1;
			index = index + 1;
		}
	}

	Adapter->SelectedChip = RESET_CHIP_SELECT;
	while (uiNumSectTobeRead) {
		uiPartOffset = (uiSectAlignAddr & (FLASH_PART_SIZE - 1)) + GetFlashBaseAddr(Adapter);

		BcmDoChipSelect(Adapter, uiSectAlignAddr);
		if (0 != BeceemFlashBulkRead(Adapter,
						(PUINT)pTempBuff,
						uiOffsetFromSectStart,
						Adapter->uiSectorSize))	{
			Status = -1;
			goto BeceemFlashBulkWriteStatus_EXIT;
		}

		ulStatus = BcmFlashUnProtectBlock(Adapter, uiOffsetFromSectStart, Adapter->uiSectorSize);

		if (uiNumSectTobeRead > 1) {
			memcpy(&pTempBuff[uiCurrSectOffsetAddr], pcBuffer, uiSectBoundary - (uiSectAlignAddr + uiCurrSectOffsetAddr));
			pcBuffer += ((uiSectBoundary - (uiSectAlignAddr + uiCurrSectOffsetAddr)));
			uiNumBytes -= (uiSectBoundary - (uiSectAlignAddr + uiCurrSectOffsetAddr));
		} else {
			memcpy(&pTempBuff[uiCurrSectOffsetAddr], pcBuffer, uiNumBytes);
		}

		if (IsFlash2x(Adapter))
			SaveHeaderIfPresent(Adapter, (PUCHAR)pTempBuff, uiOffsetFromSectStart);

		FlashSectorErase(Adapter, uiPartOffset, 1);

		for (uiIndex = 0; uiIndex < Adapter->uiSectorSize; uiIndex += Adapter->ulFlashWriteSize) {
			if (Adapter->device_removed) {
				Status = -1;
				goto BeceemFlashBulkWriteStatus_EXIT;
			}

			if (STATUS_SUCCESS != (*Adapter->fpFlashWriteWithStatusCheck)(Adapter, uiPartOffset+uiIndex, &pTempBuff[uiIndex])) {
				Status = -1;
				goto BeceemFlashBulkWriteStatus_EXIT;
			}
		}

		if (bVerify) {
			for (uiIndex = 0; uiIndex < Adapter->uiSectorSize; uiIndex += MAX_RW_SIZE) {
				if (STATUS_SUCCESS == BeceemFlashBulkRead(Adapter, (PUINT)ucReadBk, uiOffsetFromSectStart + uiIndex, MAX_RW_SIZE)) {
					if (memcmp(ucReadBk, &pTempBuff[uiIndex], MAX_RW_SIZE)) {
						Status = STATUS_FAILURE;
						goto BeceemFlashBulkWriteStatus_EXIT;
					}
				}
			}
		}

		if (ulStatus) {
			BcmRestoreBlockProtectStatus(Adapter, ulStatus);
			ulStatus = 0;
		}

		uiCurrSectOffsetAddr = 0;
		uiSectAlignAddr = uiSectBoundary;
		uiSectBoundary += Adapter->uiSectorSize;
		uiOffsetFromSectStart += Adapter->uiSectorSize;
		uiNumSectTobeRead--;
	}
/*
 * Cleanup.
 */
BeceemFlashBulkWriteStatus_EXIT:
	if (ulStatus)
		BcmRestoreBlockProtectStatus(Adapter, ulStatus);

	kfree(pTempBuff);
	Adapter->SelectedChip = RESET_CHIP_SELECT;
	return Status;
}

/*
 * Procedure:	PropagateCalParamsFromFlashToMemory
 *
 * Description: Dumps the calibration section of EEPROM to DDR.
 *
 * Arguments:
 *		Adapter    - ptr to Adapter object instance
 * Returns:
 *		OSAL_STATUS_CODE
 *
 */

int PropagateCalParamsFromFlashToMemory(struct bcm_mini_adapter *Adapter)
{
	PCHAR pBuff, pPtr;
	unsigned int uiEepromSize = 0;
	unsigned int uiBytesToCopy = 0;
	/* unsigned int uiIndex = 0; */
	unsigned int uiCalStartAddr = EEPROM_CALPARAM_START;
	unsigned int uiMemoryLoc = EEPROM_CAL_DATA_INTERNAL_LOC;
	unsigned int value;
	int Status = 0;

	/*
	 * Write the signature first. This will ensure firmware does not access EEPROM.
	 */
	value = 0xbeadbead;
	wrmalt(Adapter, EEPROM_CAL_DATA_INTERNAL_LOC - 4, &value, sizeof(value));
	value = 0xbeadbead;
	wrmalt(Adapter, EEPROM_CAL_DATA_INTERNAL_LOC - 8, &value, sizeof(value));

	if (0 != BeceemNVMRead(Adapter, &uiEepromSize, EEPROM_SIZE_OFFSET, 4))
		return -1;

	uiEepromSize = ntohl(uiEepromSize);
	uiEepromSize >>= 16;

	/*
	 * subtract the auto init section size
	 */
	uiEepromSize -= EEPROM_CALPARAM_START;

	if (uiEepromSize > 1024 * 1024)
		return -1;

	pBuff = kmalloc(uiEepromSize, GFP_KERNEL);
	if (pBuff == NULL)
		return -ENOMEM;

	if (0 != BeceemNVMRead(Adapter, (PUINT)pBuff, uiCalStartAddr, uiEepromSize)) {
		kfree(pBuff);
		return -1;
	}

	pPtr = pBuff;

	uiBytesToCopy = MIN(BUFFER_4K, uiEepromSize);

	while (uiBytesToCopy) {
		Status = wrm(Adapter, uiMemoryLoc, (PCHAR)pPtr, uiBytesToCopy);
		if (Status) {
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "wrm failed with status :%d", Status);
			break;
		}

		pPtr += uiBytesToCopy;
		uiEepromSize -= uiBytesToCopy;
		uiMemoryLoc += uiBytesToCopy;
		uiBytesToCopy = MIN(BUFFER_4K, uiEepromSize);
	}

	kfree(pBuff);
	return Status;
}

/*
 * Procedure:	BeceemEEPROMReadBackandVerify
 *
 * Description: Read back the data written and verifies.
 *
 * Arguments:
 *		Adapter		- ptr to Adapter object instance
 *		pBuffer		- Data to be written.
 *		uiOffset	- Offset of the flash where data needs to be written to.
 *		uiNumBytes	- Number of bytes to be written.
 * Returns:
 *		OSAL_STATUS_CODE
 *
 */

static int BeceemEEPROMReadBackandVerify(struct bcm_mini_adapter *Adapter,
					PUINT pBuffer,
					unsigned int uiOffset,
					unsigned int uiNumBytes)
{
	unsigned int uiRdbk	= 0;
	unsigned int uiIndex	= 0;
	unsigned int uiData	= 0;
	unsigned int auiData[4]	= {0};

	while (uiNumBytes) {
		if (Adapter->device_removed)
			return -1;

		if (uiNumBytes >= MAX_RW_SIZE) {
			/* for the requests more than or equal to MAX_RW_SIZE bytes, use bulk read function to make the access faster. */
			BeceemEEPROMBulkRead(Adapter, &auiData[0], uiOffset, MAX_RW_SIZE);

			if (memcmp(&pBuffer[uiIndex], &auiData[0], MAX_RW_SIZE)) {
				/* re-write */
				BeceemEEPROMBulkWrite(Adapter, (PUCHAR)(pBuffer + uiIndex), uiOffset, MAX_RW_SIZE, false);
				mdelay(3);
				BeceemEEPROMBulkRead(Adapter, &auiData[0], uiOffset, MAX_RW_SIZE);

				if (memcmp(&pBuffer[uiIndex], &auiData[0], MAX_RW_SIZE))
					return -1;
			}
			uiOffset += MAX_RW_SIZE;
			uiNumBytes -= MAX_RW_SIZE;
			uiIndex += 4;
		} else if (uiNumBytes >= 4) {
			BeceemEEPROMBulkRead(Adapter, &uiData, uiOffset, 4);
			if (uiData != pBuffer[uiIndex]) {
				/* re-write */
				BeceemEEPROMBulkWrite(Adapter, (PUCHAR)(pBuffer + uiIndex), uiOffset, 4, false);
				mdelay(3);
				BeceemEEPROMBulkRead(Adapter, &uiData, uiOffset, 4);
				if (uiData != pBuffer[uiIndex])
					return -1;
			}
			uiOffset += 4;
			uiNumBytes -= 4;
			uiIndex++;
		} else {
			/* Handle the reads less than 4 bytes... */
			uiData = 0;
			memcpy(&uiData, ((PUCHAR)pBuffer) + (uiIndex * sizeof(unsigned int)), uiNumBytes);
			BeceemEEPROMBulkRead(Adapter, &uiRdbk, uiOffset, 4);

			if (memcmp(&uiData, &uiRdbk, uiNumBytes))
				return -1;

			uiNumBytes = 0;
		}
	}

	return 0;
}

static VOID BcmSwapWord(unsigned int *ptr1)
{
	unsigned int tempval = (unsigned int)*ptr1;
	char *ptr2 = (char *)&tempval;
	char *ptr = (char *)ptr1;

	ptr[0] = ptr2[3];
	ptr[1] = ptr2[2];
	ptr[2] = ptr2[1];
	ptr[3] = ptr2[0];
}

/*
 * Procedure:	BeceemEEPROMWritePage
 *
 * Description: Performs page write (16bytes) to the EEPROM
 *
 * Arguments:
 *		Adapter		- ptr to Adapter object instance
 *		uiData		- Data to be written.
 *		uiOffset	- Offset of the EEPROM where data needs to be written to.
 * Returns:
 *		OSAL_STATUS_CODE
 *
 */

static int BeceemEEPROMWritePage(struct bcm_mini_adapter *Adapter, unsigned int uiData[], unsigned int uiOffset)
{
	unsigned int uiRetries = MAX_EEPROM_RETRIES * RETRIES_PER_DELAY;
	unsigned int uiStatus = 0;
	UCHAR uiEpromStatus = 0;
	unsigned int value = 0;

	/* Flush the Write/Read/Cmd queues. */
	value = (EEPROM_WRITE_QUEUE_FLUSH | EEPROM_CMD_QUEUE_FLUSH | EEPROM_READ_QUEUE_FLUSH);
	wrmalt(Adapter, SPI_FLUSH_REG, &value, sizeof(value));
	value = 0;
	wrmalt(Adapter, SPI_FLUSH_REG, &value, sizeof(value));

	/* Clear the Empty/Avail/Full bits.  After this it has been confirmed
	 * that the bit was cleared by reading back the register. See NOTE below.
	 * We also clear the Read queues as we do a EEPROM status register read
	 * later.
	 */
	value = (EEPROM_WRITE_QUEUE_EMPTY | EEPROM_WRITE_QUEUE_AVAIL | EEPROM_WRITE_QUEUE_FULL | EEPROM_READ_DATA_AVAIL | EEPROM_READ_DATA_FULL);
	wrmalt(Adapter, EEPROM_SPI_Q_STATUS1_REG, &value, sizeof(value));

	/* Enable write */
	value = EEPROM_WRITE_ENABLE;
	wrmalt(Adapter, EEPROM_CMDQ_SPI_REG, &value, sizeof(value));

	/* We can write back to back 8bits * 16 into the queue and as we have
	 * checked for the queue to be empty we can write in a burst.
	 */

	value = uiData[0];
	BcmSwapWord(&value);
	wrm(Adapter, EEPROM_WRITE_DATAQ_REG, (PUCHAR)&value, 4);

	value = uiData[1];
	BcmSwapWord(&value);
	wrm(Adapter, EEPROM_WRITE_DATAQ_REG, (PUCHAR)&value, 4);

	value = uiData[2];
	BcmSwapWord(&value);
	wrm(Adapter, EEPROM_WRITE_DATAQ_REG, (PUCHAR)&value, 4);

	value = uiData[3];
	BcmSwapWord(&value);
	wrm(Adapter, EEPROM_WRITE_DATAQ_REG, (PUCHAR)&value, 4);

	/* NOTE : After this write, on readback of EEPROM_SPI_Q_STATUS1_REG
	 * shows that we see 7 for the EEPROM data write.  Which means that
	 * queue got full, also space is available as well as the queue is empty.
	 * This may happen in sequence.
	 */
	value =  EEPROM_16_BYTE_PAGE_WRITE | uiOffset;
	wrmalt(Adapter, EEPROM_CMDQ_SPI_REG, &value, sizeof(value));

	/* Ideally we should loop here without tries and eventually succeed.
	 * What we are checking if the previous write has completed, and this
	 * may take time. We should wait till the Empty bit is set.
	 */
	uiStatus = 0;
	rdmalt(Adapter, EEPROM_SPI_Q_STATUS1_REG, &uiStatus, sizeof(uiStatus));
	while ((uiStatus & EEPROM_WRITE_QUEUE_EMPTY) == 0) {
		uiRetries--;
		if (uiRetries == 0) {
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "0x0f003004 = %x, %d retries failed.\n", uiStatus, MAX_EEPROM_RETRIES * RETRIES_PER_DELAY);
			return STATUS_FAILURE;
		}

		if (!(uiRetries%RETRIES_PER_DELAY))
			udelay(1000);

		uiStatus = 0;
		rdmalt(Adapter, EEPROM_SPI_Q_STATUS1_REG, &uiStatus, sizeof(uiStatus));
		if (Adapter->device_removed == TRUE) {
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Modem got removed hence exiting from loop....");
			return -ENODEV;
		}
	}

	if (uiRetries != 0) {
		/* Clear the ones that are set - either, Empty/Full/Avail bits */
		value = (uiStatus & (EEPROM_WRITE_QUEUE_EMPTY | EEPROM_WRITE_QUEUE_AVAIL | EEPROM_WRITE_QUEUE_FULL));
		wrmalt(Adapter, EEPROM_SPI_Q_STATUS1_REG, &value, sizeof(value));
	}

	/* Here we should check if the EEPROM status register is correct before
	 * proceeding. Bit 0 in the EEPROM Status register should be 0 before
	 * we proceed further.  A 1 at Bit 0 indicates that the EEPROM is busy
	 * with the previous write. Note also that issuing this read finally
	 * means the previous write to the EEPROM has completed.
	 */
	uiRetries = MAX_EEPROM_RETRIES * RETRIES_PER_DELAY;
	uiEpromStatus = 0;
	while (uiRetries != 0) {
		uiEpromStatus = ReadEEPROMStatusRegister(Adapter);
		if (Adapter->device_removed == TRUE) {
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Modem has got removed hence exiting from loop...");
			return -ENODEV;
		}
		if ((EEPROM_STATUS_REG_WRITE_BUSY & uiEpromStatus) == 0) {
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "EEPROM status register = %x tries = %d\n", uiEpromStatus, (MAX_EEPROM_RETRIES * RETRIES_PER_DELAY - uiRetries));
			return STATUS_SUCCESS;
		}
		uiRetries--;
		if (uiRetries == 0) {
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "0x0f003004 = %x, for EEPROM status read %d retries failed.\n", uiEpromStatus, MAX_EEPROM_RETRIES * RETRIES_PER_DELAY);
			return STATUS_FAILURE;
		}
		uiEpromStatus = 0;
		if (!(uiRetries%RETRIES_PER_DELAY))
			udelay(1000);
	}

	return STATUS_SUCCESS;
} /* BeceemEEPROMWritePage */

/*
 * Procedure:	BeceemEEPROMBulkWrite
 *
 * Description: Performs write to the EEPROM
 *
 * Arguments:
 *		Adapter		- ptr to Adapter object instance
 *		pBuffer		- Data to be written.
 *		uiOffset	- Offset of the EEPROM where data needs to be written to.
 *		uiNumBytes	- Number of bytes to be written.
 *		bVerify		- read verify flag.
 * Returns:
 *		OSAL_STATUS_CODE
 *
 */

int BeceemEEPROMBulkWrite(struct bcm_mini_adapter *Adapter,
			PUCHAR pBuffer,
			unsigned int uiOffset,
			unsigned int uiNumBytes,
			bool bVerify)
{
	unsigned int uiBytesToCopy	= uiNumBytes;
	/* unsigned int uiRdbk		= 0; */
	unsigned int uiData[4]		= {0};
	unsigned int uiIndex		= 0;
	unsigned int uiTempOffset	= 0;
	unsigned int uiExtraBytes	= 0;
	/* PUINT puiBuffer	= (PUINT)pBuffer;
	 * int value;
	 */

	if (uiOffset % MAX_RW_SIZE && uiBytesToCopy) {
		uiTempOffset = uiOffset - (uiOffset % MAX_RW_SIZE);
		uiExtraBytes = uiOffset - uiTempOffset;

		BeceemEEPROMBulkRead(Adapter, &uiData[0], uiTempOffset, MAX_RW_SIZE);

		if (uiBytesToCopy >= (16 - uiExtraBytes)) {
			memcpy((((PUCHAR)&uiData[0]) + uiExtraBytes), pBuffer, MAX_RW_SIZE - uiExtraBytes);

			if (STATUS_FAILURE == BeceemEEPROMWritePage(Adapter, uiData, uiTempOffset))
				return STATUS_FAILURE;

			uiBytesToCopy -= (MAX_RW_SIZE - uiExtraBytes);
			uiIndex += (MAX_RW_SIZE - uiExtraBytes);
			uiOffset += (MAX_RW_SIZE - uiExtraBytes);
		} else {
			memcpy((((PUCHAR)&uiData[0]) + uiExtraBytes), pBuffer, uiBytesToCopy);

			if (STATUS_FAILURE == BeceemEEPROMWritePage(Adapter, uiData, uiTempOffset))
				return STATUS_FAILURE;

			uiIndex += uiBytesToCopy;
			uiOffset += uiBytesToCopy;
			uiBytesToCopy = 0;
		}
	}

	while (uiBytesToCopy) {
		if (Adapter->device_removed)
			return -1;

		if (uiBytesToCopy >= MAX_RW_SIZE) {
			if (STATUS_FAILURE == BeceemEEPROMWritePage(Adapter, (PUINT) &pBuffer[uiIndex], uiOffset))
				return STATUS_FAILURE;

			uiIndex += MAX_RW_SIZE;
			uiOffset += MAX_RW_SIZE;
			uiBytesToCopy -= MAX_RW_SIZE;
		} else {
			/*
			 * To program non 16byte aligned data, read 16byte and then update.
			 */
			BeceemEEPROMBulkRead(Adapter, &uiData[0], uiOffset, 16);
			memcpy(&uiData[0], pBuffer + uiIndex, uiBytesToCopy);

			if (STATUS_FAILURE == BeceemEEPROMWritePage(Adapter, uiData, uiOffset))
				return STATUS_FAILURE;

			uiBytesToCopy = 0;
		}
	}

	return 0;
}

/*
 * Procedure:	BeceemNVMRead
 *
 * Description: Reads n number of bytes from NVM.
 *
 * Arguments:
 *		Adapter      - ptr to Adapter object instance
 *		pBuffer       - Buffer to store the data read from NVM
 *		uiOffset       - Offset of NVM from where data should be read
 *		uiNumBytes - Number of bytes to be read from the NVM.
 *
 * Returns:
 *		OSAL_STATUS_SUCCESS - if NVM read is successful.
 *		<FAILURE>			- if failed.
 */

int BeceemNVMRead(struct bcm_mini_adapter *Adapter,
		PUINT pBuffer,
		unsigned int uiOffset,
		unsigned int uiNumBytes)
{
	int Status = 0;

	#if !defined(BCM_SHM_INTERFACE) || defined(FLASH_DIRECT_ACCESS)
		unsigned int uiTemp = 0, value;
	#endif

	if (Adapter->eNVMType == NVM_FLASH) {
		if (Adapter->bFlashRawRead == false) {
			if (IsSectionExistInVendorInfo(Adapter, Adapter->eActiveDSD))
				return vendorextnReadSection(Adapter, (PUCHAR)pBuffer, Adapter->eActiveDSD, uiOffset, uiNumBytes);

			uiOffset = uiOffset + Adapter->ulFlashCalStart;
		}

		#if defined(BCM_SHM_INTERFACE) && !defined(FLASH_DIRECT_ACCESS)
			Status = bcmflash_raw_read((uiOffset / FLASH_PART_SIZE), (uiOffset % FLASH_PART_SIZE), (unsigned char *)pBuffer, uiNumBytes);
		#else
			rdmalt(Adapter, 0x0f000C80, &uiTemp, sizeof(uiTemp));
			value = 0;
			wrmalt(Adapter, 0x0f000C80, &value, sizeof(value));
			Status = BeceemFlashBulkRead(Adapter,
						pBuffer,
						uiOffset,
						uiNumBytes);
			wrmalt(Adapter, 0x0f000C80, &uiTemp, sizeof(uiTemp));
		#endif
	} else if (Adapter->eNVMType == NVM_EEPROM) {
		Status = BeceemEEPROMBulkRead(Adapter,
					pBuffer,
					uiOffset,
					uiNumBytes);
	} else {
		Status = -1;
	}

	return Status;
}

/*
 * Procedure:	BeceemNVMWrite
 *
 * Description: Writes n number of bytes to NVM.
 *
 * Arguments:
 *		Adapter      - ptr to Adapter object instance
 *		pBuffer       - Buffer contains the data to be written.
 *		uiOffset       - Offset of NVM where data to be written to.
 *		uiNumBytes - Number of bytes to be written..
 *
 * Returns:
 *		OSAL_STATUS_SUCCESS - if NVM write is successful.
 *		<FAILURE>			- if failed.
 */

int BeceemNVMWrite(struct bcm_mini_adapter *Adapter,
		PUINT pBuffer,
		unsigned int uiOffset,
		unsigned int uiNumBytes,
		bool bVerify)
{
	int Status = 0;
	unsigned int uiTemp = 0;
	unsigned int uiMemoryLoc = EEPROM_CAL_DATA_INTERNAL_LOC;
	unsigned int uiIndex = 0;

	#if !defined(BCM_SHM_INTERFACE) || defined(FLASH_DIRECT_ACCESS)
		unsigned int value;
	#endif

	unsigned int uiFlashOffset = 0;

	if (Adapter->eNVMType == NVM_FLASH) {
		if (IsSectionExistInVendorInfo(Adapter, Adapter->eActiveDSD))
			Status = vendorextnWriteSection(Adapter, (PUCHAR)pBuffer, Adapter->eActiveDSD, uiOffset, uiNumBytes, bVerify);
		else {
			uiFlashOffset = uiOffset + Adapter->ulFlashCalStart;

			#if defined(BCM_SHM_INTERFACE) && !defined(FLASH_DIRECT_ACCESS)
				Status = bcmflash_raw_write((uiFlashOffset / FLASH_PART_SIZE), (uiFlashOffset % FLASH_PART_SIZE), (unsigned char *)pBuffer, uiNumBytes);
			#else
				rdmalt(Adapter, 0x0f000C80, &uiTemp, sizeof(uiTemp));
				value = 0;
				wrmalt(Adapter, 0x0f000C80, &value, sizeof(value));

				if (Adapter->bStatusWrite == TRUE)
					Status = BeceemFlashBulkWriteStatus(Adapter,
									pBuffer,
									uiFlashOffset,
									uiNumBytes ,
									bVerify);
				else

					Status = BeceemFlashBulkWrite(Adapter,
								pBuffer,
								uiFlashOffset,
								uiNumBytes,
								bVerify);
			#endif
		}

		if (uiOffset >= EEPROM_CALPARAM_START) {
			uiMemoryLoc += (uiOffset - EEPROM_CALPARAM_START);
			while (uiNumBytes) {
				if (uiNumBytes > BUFFER_4K) {
					wrm(Adapter, (uiMemoryLoc+uiIndex), (PCHAR)(pBuffer + (uiIndex / 4)), BUFFER_4K);
					uiNumBytes -= BUFFER_4K;
					uiIndex += BUFFER_4K;
				} else {
					wrm(Adapter, uiMemoryLoc+uiIndex, (PCHAR)(pBuffer + (uiIndex / 4)), uiNumBytes);
					uiNumBytes = 0;
					break;
				}
			}
		} else {
			if ((uiOffset + uiNumBytes) > EEPROM_CALPARAM_START) {
				ULONG ulBytesTobeSkipped = 0;
				PUCHAR pcBuffer = (PUCHAR)pBuffer; /* char pointer to take care of odd byte cases. */
				uiNumBytes -= (EEPROM_CALPARAM_START - uiOffset);
				ulBytesTobeSkipped += (EEPROM_CALPARAM_START - uiOffset);
				uiOffset += (EEPROM_CALPARAM_START - uiOffset);
				while (uiNumBytes) {
					if (uiNumBytes > BUFFER_4K) {
						wrm(Adapter, uiMemoryLoc + uiIndex, (PCHAR)&pcBuffer[ulBytesTobeSkipped + uiIndex], BUFFER_4K);
						uiNumBytes -= BUFFER_4K;
						uiIndex += BUFFER_4K;
					} else {
						wrm(Adapter, uiMemoryLoc + uiIndex, (PCHAR)&pcBuffer[ulBytesTobeSkipped + uiIndex], uiNumBytes);
						uiNumBytes = 0;
						break;
					}
				}
			}
		}
		/* restore the values. */
		wrmalt(Adapter, 0x0f000C80, &uiTemp, sizeof(uiTemp));
	} else if (Adapter->eNVMType == NVM_EEPROM) {
		Status = BeceemEEPROMBulkWrite(Adapter,
					(PUCHAR)pBuffer,
					uiOffset,
					uiNumBytes,
					bVerify);
		if (bVerify)
			Status = BeceemEEPROMReadBackandVerify(Adapter, (PUINT)pBuffer, uiOffset, uiNumBytes);
	} else {
		Status = -1;
	}
	return Status;
}

/*
 * Procedure:	BcmUpdateSectorSize
 *
 * Description: Updates the sector size to FLASH.
 *
 * Arguments:
 *		Adapter       - ptr to Adapter object instance
 *          uiSectorSize - sector size
 *
 * Returns:
 *		OSAL_STATUS_SUCCESS - if NVM write is successful.
 *		<FAILURE>			- if failed.
 */

int BcmUpdateSectorSize(struct bcm_mini_adapter *Adapter, unsigned int uiSectorSize)
{
	int Status = -1;
	struct bcm_flash_cs_info sFlashCsInfo = {0};
	unsigned int uiTemp = 0;
	unsigned int uiSectorSig = 0;
	unsigned int uiCurrentSectorSize = 0;
	unsigned int value;

	rdmalt(Adapter, 0x0f000C80, &uiTemp, sizeof(uiTemp));
	value = 0;
	wrmalt(Adapter, 0x0f000C80, &value, sizeof(value));

	/*
	 * Before updating the sector size in the reserved area, check if already present.
	 */
	BeceemFlashBulkRead(Adapter, (PUINT)&sFlashCsInfo, Adapter->ulFlashControlSectionStart, sizeof(sFlashCsInfo));
	uiSectorSig = ntohl(sFlashCsInfo.FlashSectorSizeSig);
	uiCurrentSectorSize = ntohl(sFlashCsInfo.FlashSectorSize);

	if (uiSectorSig == FLASH_SECTOR_SIZE_SIG) {
		if ((uiCurrentSectorSize <= MAX_SECTOR_SIZE) && (uiCurrentSectorSize >= MIN_SECTOR_SIZE)) {
			if (uiSectorSize == uiCurrentSectorSize) {
				BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "Provided sector size is same as programmed in Flash");
				Status = STATUS_SUCCESS;
				goto Restore;
			}
		}
	}

	if ((uiSectorSize <= MAX_SECTOR_SIZE) && (uiSectorSize >= MIN_SECTOR_SIZE)) {
		sFlashCsInfo.FlashSectorSize = htonl(uiSectorSize);
		sFlashCsInfo.FlashSectorSizeSig = htonl(FLASH_SECTOR_SIZE_SIG);

		Status = BeceemFlashBulkWrite(Adapter,
					(PUINT)&sFlashCsInfo,
					Adapter->ulFlashControlSectionStart,
					sizeof(sFlashCsInfo),
					TRUE);
	}

Restore:
	/* restore the values. */
	wrmalt(Adapter, 0x0f000C80, &uiTemp, sizeof(uiTemp));

	return Status;
}

/*
 * Procedure:	BcmGetFlashSectorSize
 *
 * Description: Finds the sector size of the FLASH.
 *
 * Arguments:
 *		Adapter    - ptr to Adapter object instance
 *
 * Returns:
 *		unsigned int - sector size.
 *
 */

static unsigned int BcmGetFlashSectorSize(struct bcm_mini_adapter *Adapter, unsigned int FlashSectorSizeSig, unsigned int FlashSectorSize)
{
	unsigned int uiSectorSize = 0;
	unsigned int uiSectorSig = 0;

	if (Adapter->bSectorSizeOverride &&
		(Adapter->uiSectorSizeInCFG <= MAX_SECTOR_SIZE &&
			Adapter->uiSectorSizeInCFG >= MIN_SECTOR_SIZE)) {
		Adapter->uiSectorSize = Adapter->uiSectorSizeInCFG;
	} else {
		uiSectorSig = FlashSectorSizeSig;

		if (uiSectorSig == FLASH_SECTOR_SIZE_SIG) {
			uiSectorSize = FlashSectorSize;
			/*
			 * If the sector size stored in the FLASH makes sense then use it.
			 */
			if (uiSectorSize <= MAX_SECTOR_SIZE && uiSectorSize >= MIN_SECTOR_SIZE) {
				Adapter->uiSectorSize = uiSectorSize;
			} else if (Adapter->uiSectorSizeInCFG <= MAX_SECTOR_SIZE &&
				Adapter->uiSectorSizeInCFG >= MIN_SECTOR_SIZE) {
				/* No valid size in FLASH, check if Config file has it. */
				Adapter->uiSectorSize = Adapter->uiSectorSizeInCFG;
			} else {
				/* Init to Default, if none of the above works. */
				Adapter->uiSectorSize = DEFAULT_SECTOR_SIZE;
			}
		} else {
			if (Adapter->uiSectorSizeInCFG <= MAX_SECTOR_SIZE &&
				Adapter->uiSectorSizeInCFG >= MIN_SECTOR_SIZE)
				Adapter->uiSectorSize = Adapter->uiSectorSizeInCFG;
			else
				Adapter->uiSectorSize = DEFAULT_SECTOR_SIZE;
		}
	}

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "Sector size  :%x\n", Adapter->uiSectorSize);

	return Adapter->uiSectorSize;
}

/*
 * Procedure:	BcmInitEEPROMQueues
 *
 * Description: Initialization of EEPROM queues.
 *
 * Arguments:
 *		Adapter    - ptr to Adapter object instance
 *
 * Returns:
 *		<OSAL_STATUS_CODE>
 */

static int BcmInitEEPROMQueues(struct bcm_mini_adapter *Adapter)
{
	unsigned int value = 0;
	/* CHIP Bug : Clear the Avail bits on the Read queue. The default
	 * value on this register is supposed to be 0x00001102.
	 * But we get 0x00001122.
	 */
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "Fixing reset value on 0x0f003004 register\n");
	value = EEPROM_READ_DATA_AVAIL;
	wrmalt(Adapter, EEPROM_SPI_Q_STATUS1_REG, &value, sizeof(value));

	/* Flush the all the EEPROM queues. */
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, " Flushing the queues\n");
	value = EEPROM_ALL_QUEUE_FLUSH;
	wrmalt(Adapter, SPI_FLUSH_REG, &value, sizeof(value));

	value = 0;
	wrmalt(Adapter, SPI_FLUSH_REG, &value, sizeof(value));

	/* Read the EEPROM Status Register. Just to see, no real purpose. */
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "EEPROM Status register value = %x\n", ReadEEPROMStatusRegister(Adapter));

	return STATUS_SUCCESS;
} /* BcmInitEEPROMQueues() */

/*
 * Procedure:	BcmInitNVM
 *
 * Description: Initialization of NVM, EEPROM size,FLASH size, sector size etc.
 *
 * Arguments:
 *		Adapter    - ptr to Adapter object instance
 *
 * Returns:
 *		<OSAL_STATUS_CODE>
 */

int BcmInitNVM(struct bcm_mini_adapter *ps_adapter)
{
	BcmValidateNvmType(ps_adapter);
	BcmInitEEPROMQueues(ps_adapter);

	if (ps_adapter->eNVMType == NVM_AUTODETECT) {
		ps_adapter->eNVMType = BcmGetNvmType(ps_adapter);
		if (ps_adapter->eNVMType == NVM_UNKNOWN)
			BCM_DEBUG_PRINT(ps_adapter, DBG_TYPE_PRINTK, 0, 0, "NVM Type is unknown!!\n");
	} else if (ps_adapter->eNVMType == NVM_FLASH) {
		BcmGetFlashCSInfo(ps_adapter);
	}

	BcmGetNvmSize(ps_adapter);

	return STATUS_SUCCESS;
}

/* BcmGetNvmSize : set the EEPROM or flash size in Adapter.
 *
 * Input Parameter:
 *		Adapter data structure
 * Return Value :
 *		0. means success;
 */

static int BcmGetNvmSize(struct bcm_mini_adapter *Adapter)
{
	if (Adapter->eNVMType == NVM_EEPROM)
		Adapter->uiNVMDSDSize = BcmGetEEPROMSize(Adapter);
	else if (Adapter->eNVMType == NVM_FLASH)
		Adapter->uiNVMDSDSize = BcmGetFlashSize(Adapter);

	return 0;
}

/*
 * Procedure:	BcmValidateNvm
 *
 * Description: Validates the NVM Type option selected against the device
 *
 * Arguments:
 *		Adapter    - ptr to Adapter object instance
 *
 * Returns:
 *		<VOID>
 */

static VOID BcmValidateNvmType(struct bcm_mini_adapter *Adapter)
{
	/*
	 * if forcing the FLASH through CFG file, we should ensure device really has a FLASH.
	 * Accessing the FLASH address without the FLASH being present can cause hang/freeze etc.
	 * So if NVM_FLASH is selected for older chipsets, change it to AUTODETECT where EEPROM is 1st choice.
	 */

	if (Adapter->eNVMType == NVM_FLASH &&
		Adapter->chip_id < 0xBECE3300)
		Adapter->eNVMType = NVM_AUTODETECT;
}

/*
 * Procedure:	BcmReadFlashRDID
 *
 * Description: Reads ID from Serial Flash
 *
 * Arguments:
 *		Adapter    - ptr to Adapter object instance
 *
 * Returns:
 *		Flash ID
 */

static ULONG BcmReadFlashRDID(struct bcm_mini_adapter *Adapter)
{
	ULONG ulRDID = 0;
	unsigned int value;

	/*
	 * Read ID Instruction.
	 */
	value = (FLASH_CMD_READ_ID << 24);
	wrmalt(Adapter, FLASH_SPI_CMDQ_REG, &value, sizeof(value));

	/* Delay */
	udelay(10);

	/*
	 * Read SPI READQ REG. The output will be WWXXYYZZ.
	 * The ID is 3Bytes long and is WWXXYY. ZZ needs to be Ignored.
	 */
	rdmalt(Adapter, FLASH_SPI_READQ_REG, (PUINT)&ulRDID, sizeof(ulRDID));

	return ulRDID >> 8;
}

int BcmAllocFlashCSStructure(struct bcm_mini_adapter *psAdapter)
{
	if (!psAdapter) {
		BCM_DEBUG_PRINT(psAdapter, DBG_TYPE_PRINTK, 0, 0, "Adapter structure point is NULL");
		return -EINVAL;
	}
	psAdapter->psFlashCSInfo = kzalloc(sizeof(struct bcm_flash_cs_info), GFP_KERNEL);
	if (psAdapter->psFlashCSInfo == NULL) {
		BCM_DEBUG_PRINT(psAdapter, DBG_TYPE_PRINTK, 0, 0, "Can't Allocate memory for Flash 1.x");
		return -ENOMEM;
	}

	psAdapter->psFlash2xCSInfo = kzalloc(sizeof(struct bcm_flash2x_cs_info), GFP_KERNEL);
	if (!psAdapter->psFlash2xCSInfo) {
		BCM_DEBUG_PRINT(psAdapter, DBG_TYPE_PRINTK, 0, 0, "Can't Allocate memory for Flash 2.x");
		kfree(psAdapter->psFlashCSInfo);
		return -ENOMEM;
	}

	psAdapter->psFlash2xVendorInfo = kzalloc(sizeof(struct bcm_flash2x_vendor_info), GFP_KERNEL);
	if (!psAdapter->psFlash2xVendorInfo) {
		BCM_DEBUG_PRINT(psAdapter, DBG_TYPE_PRINTK, 0, 0, "Can't Allocate Vendor Info Memory for Flash 2.x");
		kfree(psAdapter->psFlashCSInfo);
		kfree(psAdapter->psFlash2xCSInfo);
		return -ENOMEM;
	}

	return STATUS_SUCCESS;
}

int BcmDeAllocFlashCSStructure(struct bcm_mini_adapter *psAdapter)
{
	if (!psAdapter) {
		BCM_DEBUG_PRINT(psAdapter, DBG_TYPE_PRINTK, 0, 0, "Adapter structure point is NULL");
		return -EINVAL;
	}
	kfree(psAdapter->psFlashCSInfo);
	kfree(psAdapter->psFlash2xCSInfo);
	kfree(psAdapter->psFlash2xVendorInfo);
	return STATUS_SUCCESS;
}

static int BcmDumpFlash2XCSStructure(struct bcm_flash2x_cs_info *psFlash2xCSInfo, struct bcm_mini_adapter *Adapter)
{
	unsigned int Index = 0;

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "**********************FLASH2X CS Structure *******************");
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "Signature is  :%x", (psFlash2xCSInfo->MagicNumber));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "Flash Major Version :%d", MAJOR_VERSION(psFlash2xCSInfo->FlashLayoutVersion));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "Flash Minor Version :%d", MINOR_VERSION(psFlash2xCSInfo->FlashLayoutVersion));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, " ISOImageMajorVersion:0x%x", (psFlash2xCSInfo->ISOImageVersion));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "SCSIFirmwareMajorVersion :0x%x", (psFlash2xCSInfo->SCSIFirmwareVersion));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "OffsetFromZeroForPart1ISOImage :0x%x", (psFlash2xCSInfo->OffsetFromZeroForPart1ISOImage));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "OffsetFromZeroForScsiFirmware :0x%x", (psFlash2xCSInfo->OffsetFromZeroForScsiFirmware));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "SizeOfScsiFirmware  :0x%x", (psFlash2xCSInfo->SizeOfScsiFirmware));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "OffsetFromZeroForPart2ISOImage :0x%x", (psFlash2xCSInfo->OffsetFromZeroForPart2ISOImage));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "OffsetFromZeroForDSDStart :0x%x", (psFlash2xCSInfo->OffsetFromZeroForDSDStart));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "OffsetFromZeroForDSDEnd :0x%x", (psFlash2xCSInfo->OffsetFromZeroForDSDEnd));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "OffsetFromZeroForVSAStart :0x%x", (psFlash2xCSInfo->OffsetFromZeroForVSAStart));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "OffsetFromZeroForVSAEnd :0x%x", (psFlash2xCSInfo->OffsetFromZeroForVSAEnd));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "OffsetFromZeroForControlSectionStart :0x%x", (psFlash2xCSInfo->OffsetFromZeroForControlSectionStart));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "OffsetFromZeroForControlSectionData :0x%x", (psFlash2xCSInfo->OffsetFromZeroForControlSectionData));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "CDLessInactivityTimeout :0x%x", (psFlash2xCSInfo->CDLessInactivityTimeout));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "NewImageSignature :0x%x", (psFlash2xCSInfo->NewImageSignature));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "FlashSectorSizeSig :0x%x", (psFlash2xCSInfo->FlashSectorSizeSig));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "FlashSectorSize :0x%x", (psFlash2xCSInfo->FlashSectorSize));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "FlashWriteSupportSize :0x%x", (psFlash2xCSInfo->FlashWriteSupportSize));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "TotalFlashSize :0x%X", (psFlash2xCSInfo->TotalFlashSize));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "FlashBaseAddr :0x%x", (psFlash2xCSInfo->FlashBaseAddr));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "FlashPartMaxSize :0x%x", (psFlash2xCSInfo->FlashPartMaxSize));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "IsCDLessDeviceBootSig :0x%x", (psFlash2xCSInfo->IsCDLessDeviceBootSig));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "MassStorageTimeout :0x%x", (psFlash2xCSInfo->MassStorageTimeout));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "OffsetISOImage1Part1Start :0x%x", (psFlash2xCSInfo->OffsetISOImage1Part1Start));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "OffsetISOImage1Part1End :0x%x", (psFlash2xCSInfo->OffsetISOImage1Part1End));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "OffsetISOImage1Part2Start :0x%x", (psFlash2xCSInfo->OffsetISOImage1Part2Start));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "OffsetISOImage1Part2End :0x%x", (psFlash2xCSInfo->OffsetISOImage1Part2End));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "OffsetISOImage1Part3Start :0x%x", (psFlash2xCSInfo->OffsetISOImage1Part3Start));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "OffsetISOImage1Part3End :0x%x", (psFlash2xCSInfo->OffsetISOImage1Part3End));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "OffsetISOImage2Part1Start :0x%x", (psFlash2xCSInfo->OffsetISOImage2Part1Start));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "OffsetISOImage2Part1End	:0x%x", (psFlash2xCSInfo->OffsetISOImage2Part1End));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "OffsetISOImage2Part2Start :0x%x", (psFlash2xCSInfo->OffsetISOImage2Part2Start));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "OffsetISOImage2Part2End :0x%x", (psFlash2xCSInfo->OffsetISOImage2Part2End));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "OffsetISOImage2Part3Start :0x%x", (psFlash2xCSInfo->OffsetISOImage2Part3Start));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "OffsetISOImage2Part3End :0x%x", (psFlash2xCSInfo->OffsetISOImage2Part3End));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "OffsetFromDSDStartForDSDHeader :0x%x", (psFlash2xCSInfo->OffsetFromDSDStartForDSDHeader));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "OffsetFromZeroForDSD1Start :0x%x", (psFlash2xCSInfo->OffsetFromZeroForDSD1Start));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "OffsetFromZeroForDSD1End :0x%x", (psFlash2xCSInfo->OffsetFromZeroForDSD1End));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "OffsetFromZeroForDSD2Start :0x%x", (psFlash2xCSInfo->OffsetFromZeroForDSD2Start));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "OffsetFromZeroForDSD2End :0x%x", (psFlash2xCSInfo->OffsetFromZeroForDSD2End));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "OffsetFromZeroForVSA1Start :0x%x", (psFlash2xCSInfo->OffsetFromZeroForVSA1Start));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "OffsetFromZeroForVSA1End :0x%x", (psFlash2xCSInfo->OffsetFromZeroForVSA1End));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "OffsetFromZeroForVSA2Start :0x%x", (psFlash2xCSInfo->OffsetFromZeroForVSA2Start));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "OffsetFromZeroForVSA2End :0x%x", (psFlash2xCSInfo->OffsetFromZeroForVSA2End));
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "Sector Access Bit Map is Defined as :");

	for (Index = 0; Index < (FLASH2X_TOTAL_SIZE / (DEFAULT_SECTOR_SIZE * 16)); Index++)
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "SectorAccessBitMap[%d] :0x%x", Index,
				(psFlash2xCSInfo->SectorAccessBitMap[Index]));

	return STATUS_SUCCESS;
}

static int ConvertEndianOf2XCSStructure(struct bcm_flash2x_cs_info *psFlash2xCSInfo)
{
	unsigned int Index = 0;

	psFlash2xCSInfo->MagicNumber = ntohl(psFlash2xCSInfo->MagicNumber);
	psFlash2xCSInfo->FlashLayoutVersion = ntohl(psFlash2xCSInfo->FlashLayoutVersion);
	/* psFlash2xCSInfo->FlashLayoutMinorVersion = ntohs(psFlash2xCSInfo->FlashLayoutMinorVersion); */
	psFlash2xCSInfo->ISOImageVersion = ntohl(psFlash2xCSInfo->ISOImageVersion);
	psFlash2xCSInfo->SCSIFirmwareVersion = ntohl(psFlash2xCSInfo->SCSIFirmwareVersion);
	psFlash2xCSInfo->OffsetFromZeroForPart1ISOImage = ntohl(psFlash2xCSInfo->OffsetFromZeroForPart1ISOImage);
	psFlash2xCSInfo->OffsetFromZeroForScsiFirmware = ntohl(psFlash2xCSInfo->OffsetFromZeroForScsiFirmware);
	psFlash2xCSInfo->SizeOfScsiFirmware = ntohl(psFlash2xCSInfo->SizeOfScsiFirmware);
	psFlash2xCSInfo->OffsetFromZeroForPart2ISOImage = ntohl(psFlash2xCSInfo->OffsetFromZeroForPart2ISOImage);
	psFlash2xCSInfo->OffsetFromZeroForDSDStart = ntohl(psFlash2xCSInfo->OffsetFromZeroForDSDStart);
	psFlash2xCSInfo->OffsetFromZeroForDSDEnd = ntohl(psFlash2xCSInfo->OffsetFromZeroForDSDEnd);
	psFlash2xCSInfo->OffsetFromZeroForVSAStart = ntohl(psFlash2xCSInfo->OffsetFromZeroForVSAStart);
	psFlash2xCSInfo->OffsetFromZeroForVSAEnd = ntohl(psFlash2xCSInfo->OffsetFromZeroForVSAEnd);
	psFlash2xCSInfo->OffsetFromZeroForControlSectionStart = ntohl(psFlash2xCSInfo->OffsetFromZeroForControlSectionStart);
	psFlash2xCSInfo->OffsetFromZeroForControlSectionData = ntohl(psFlash2xCSInfo->OffsetFromZeroForControlSectionData);
	psFlash2xCSInfo->CDLessInactivityTimeout = ntohl(psFlash2xCSInfo->CDLessInactivityTimeout);
	psFlash2xCSInfo->NewImageSignature = ntohl(psFlash2xCSInfo->NewImageSignature);
	psFlash2xCSInfo->FlashSectorSizeSig = ntohl(psFlash2xCSInfo->FlashSectorSizeSig);
	psFlash2xCSInfo->FlashSectorSize = ntohl(psFlash2xCSInfo->FlashSectorSize);
	psFlash2xCSInfo->FlashWriteSupportSize = ntohl(psFlash2xCSInfo->FlashWriteSupportSize);
	psFlash2xCSInfo->TotalFlashSize = ntohl(psFlash2xCSInfo->TotalFlashSize);
	psFlash2xCSInfo->FlashBaseAddr = ntohl(psFlash2xCSInfo->FlashBaseAddr);
	psFlash2xCSInfo->FlashPartMaxSize = ntohl(psFlash2xCSInfo->FlashPartMaxSize);
	psFlash2xCSInfo->IsCDLessDeviceBootSig = ntohl(psFlash2xCSInfo->IsCDLessDeviceBootSig);
	psFlash2xCSInfo->MassStorageTimeout = ntohl(psFlash2xCSInfo->MassStorageTimeout);
	psFlash2xCSInfo->OffsetISOImage1Part1Start = ntohl(psFlash2xCSInfo->OffsetISOImage1Part1Start);
	psFlash2xCSInfo->OffsetISOImage1Part1End = ntohl(psFlash2xCSInfo->OffsetISOImage1Part1End);
	psFlash2xCSInfo->OffsetISOImage1Part2Start = ntohl(psFlash2xCSInfo->OffsetISOImage1Part2Start);
	psFlash2xCSInfo->OffsetISOImage1Part2End = ntohl(psFlash2xCSInfo->OffsetISOImage1Part2End);
	psFlash2xCSInfo->OffsetISOImage1Part3Start = ntohl(psFlash2xCSInfo->OffsetISOImage1Part3Start);
	psFlash2xCSInfo->OffsetISOImage1Part3End = ntohl(psFlash2xCSInfo->OffsetISOImage1Part3End);
	psFlash2xCSInfo->OffsetISOImage2Part1Start = ntohl(psFlash2xCSInfo->OffsetISOImage2Part1Start);
	psFlash2xCSInfo->OffsetISOImage2Part1End = ntohl(psFlash2xCSInfo->OffsetISOImage2Part1End);
	psFlash2xCSInfo->OffsetISOImage2Part2Start = ntohl(psFlash2xCSInfo->OffsetISOImage2Part2Start);
	psFlash2xCSInfo->OffsetISOImage2Part2End = ntohl(psFlash2xCSInfo->OffsetISOImage2Part2End);
	psFlash2xCSInfo->OffsetISOImage2Part3Start = ntohl(psFlash2xCSInfo->OffsetISOImage2Part3Start);
	psFlash2xCSInfo->OffsetISOImage2Part3End = ntohl(psFlash2xCSInfo->OffsetISOImage2Part3End);
	psFlash2xCSInfo->OffsetFromDSDStartForDSDHeader = ntohl(psFlash2xCSInfo->OffsetFromDSDStartForDSDHeader);
	psFlash2xCSInfo->OffsetFromZeroForDSD1Start = ntohl(psFlash2xCSInfo->OffsetFromZeroForDSD1Start);
	psFlash2xCSInfo->OffsetFromZeroForDSD1End = ntohl(psFlash2xCSInfo->OffsetFromZeroForDSD1End);
	psFlash2xCSInfo->OffsetFromZeroForDSD2Start = ntohl(psFlash2xCSInfo->OffsetFromZeroForDSD2Start);
	psFlash2xCSInfo->OffsetFromZeroForDSD2End = ntohl(psFlash2xCSInfo->OffsetFromZeroForDSD2End);
	psFlash2xCSInfo->OffsetFromZeroForVSA1Start = ntohl(psFlash2xCSInfo->OffsetFromZeroForVSA1Start);
	psFlash2xCSInfo->OffsetFromZeroForVSA1End = ntohl(psFlash2xCSInfo->OffsetFromZeroForVSA1End);
	psFlash2xCSInfo->OffsetFromZeroForVSA2Start = ntohl(psFlash2xCSInfo->OffsetFromZeroForVSA2Start);
	psFlash2xCSInfo->OffsetFromZeroForVSA2End = ntohl(psFlash2xCSInfo->OffsetFromZeroForVSA2End);

	for (Index = 0; Index < (FLASH2X_TOTAL_SIZE / (DEFAULT_SECTOR_SIZE * 16)); Index++)
		psFlash2xCSInfo->SectorAccessBitMap[Index] = ntohl(psFlash2xCSInfo->SectorAccessBitMap[Index]);

	return STATUS_SUCCESS;
}

static int ConvertEndianOfCSStructure(struct bcm_flash_cs_info *psFlashCSInfo)
{
	/* unsigned int Index = 0; */
	psFlashCSInfo->MagicNumber				= ntohl(psFlashCSInfo->MagicNumber);
	psFlashCSInfo->FlashLayoutVersion			= ntohl(psFlashCSInfo->FlashLayoutVersion);
	psFlashCSInfo->ISOImageVersion				= ntohl(psFlashCSInfo->ISOImageVersion);
	/* won't convert according to old assumption */
	psFlashCSInfo->SCSIFirmwareVersion			= (psFlashCSInfo->SCSIFirmwareVersion);
	psFlashCSInfo->OffsetFromZeroForPart1ISOImage		= ntohl(psFlashCSInfo->OffsetFromZeroForPart1ISOImage);
	psFlashCSInfo->OffsetFromZeroForScsiFirmware		= ntohl(psFlashCSInfo->OffsetFromZeroForScsiFirmware);
	psFlashCSInfo->SizeOfScsiFirmware			= ntohl(psFlashCSInfo->SizeOfScsiFirmware);
	psFlashCSInfo->OffsetFromZeroForPart2ISOImage		= ntohl(psFlashCSInfo->OffsetFromZeroForPart2ISOImage);
	psFlashCSInfo->OffsetFromZeroForCalibrationStart	= ntohl(psFlashCSInfo->OffsetFromZeroForCalibrationStart);
	psFlashCSInfo->OffsetFromZeroForCalibrationEnd		= ntohl(psFlashCSInfo->OffsetFromZeroForCalibrationEnd);
	psFlashCSInfo->OffsetFromZeroForVSAStart		= ntohl(psFlashCSInfo->OffsetFromZeroForVSAStart);
	psFlashCSInfo->OffsetFromZeroForVSAEnd			= ntohl(psFlashCSInfo->OffsetFromZeroForVSAEnd);
	psFlashCSInfo->OffsetFromZeroForControlSectionStart	= ntohl(psFlashCSInfo->OffsetFromZeroForControlSectionStart);
	psFlashCSInfo->OffsetFromZeroForControlSectionData	= ntohl(psFlashCSInfo->OffsetFromZeroForControlSectionData);
	psFlashCSInfo->CDLessInactivityTimeout			= ntohl(psFlashCSInfo->CDLessInactivityTimeout);
	psFlashCSInfo->NewImageSignature			= ntohl(psFlashCSInfo->NewImageSignature);
	psFlashCSInfo->FlashSectorSizeSig			= ntohl(psFlashCSInfo->FlashSectorSizeSig);
	psFlashCSInfo->FlashSectorSize				= ntohl(psFlashCSInfo->FlashSectorSize);
	psFlashCSInfo->FlashWriteSupportSize			= ntohl(psFlashCSInfo->FlashWriteSupportSize);
	psFlashCSInfo->TotalFlashSize				= ntohl(psFlashCSInfo->TotalFlashSize);
	psFlashCSInfo->FlashBaseAddr				= ntohl(psFlashCSInfo->FlashBaseAddr);
	psFlashCSInfo->FlashPartMaxSize				= ntohl(psFlashCSInfo->FlashPartMaxSize);
	psFlashCSInfo->IsCDLessDeviceBootSig			= ntohl(psFlashCSInfo->IsCDLessDeviceBootSig);
	psFlashCSInfo->MassStorageTimeout			= ntohl(psFlashCSInfo->MassStorageTimeout);

	return STATUS_SUCCESS;
}

static int IsSectionExistInVendorInfo(struct bcm_mini_adapter *Adapter, enum bcm_flash2x_section_val section)
{
	return (Adapter->uiVendorExtnFlag &&
		(Adapter->psFlash2xVendorInfo->VendorSection[section].AccessFlags & FLASH2X_SECTION_PRESENT) &&
		(Adapter->psFlash2xVendorInfo->VendorSection[section].OffsetFromZeroForSectionStart != UNINIT_PTR_IN_CS));
}

static VOID UpdateVendorInfo(struct bcm_mini_adapter *Adapter)
{
	B_UINT32 i = 0;
	unsigned int uiSizeSection = 0;

	Adapter->uiVendorExtnFlag = false;

	for (i = 0; i < TOTAL_SECTIONS; i++)
		Adapter->psFlash2xVendorInfo->VendorSection[i].OffsetFromZeroForSectionStart = UNINIT_PTR_IN_CS;

	if (STATUS_SUCCESS != vendorextnGetSectionInfo(Adapter, Adapter->psFlash2xVendorInfo))
		return;

	i = 0;
	while (i < TOTAL_SECTIONS) {
		if (!(Adapter->psFlash2xVendorInfo->VendorSection[i].AccessFlags & FLASH2X_SECTION_PRESENT)) {
			i++;
			continue;
		}

		Adapter->uiVendorExtnFlag = TRUE;
		uiSizeSection = (Adapter->psFlash2xVendorInfo->VendorSection[i].OffsetFromZeroForSectionEnd -
				Adapter->psFlash2xVendorInfo->VendorSection[i].OffsetFromZeroForSectionStart);

		switch (i) {
		case DSD0:
			if ((uiSizeSection >= (Adapter->psFlash2xCSInfo->OffsetFromDSDStartForDSDHeader + sizeof(struct bcm_dsd_header))) &&
				(UNINIT_PTR_IN_CS != Adapter->psFlash2xVendorInfo->VendorSection[i].OffsetFromZeroForSectionStart))
				Adapter->psFlash2xCSInfo->OffsetFromZeroForDSDStart = Adapter->psFlash2xCSInfo->OffsetFromZeroForDSDEnd = VENDOR_PTR_IN_CS;
			else
				Adapter->psFlash2xCSInfo->OffsetFromZeroForDSDStart = Adapter->psFlash2xCSInfo->OffsetFromZeroForDSDEnd = UNINIT_PTR_IN_CS;
			break;

		case DSD1:
			if ((uiSizeSection >= (Adapter->psFlash2xCSInfo->OffsetFromDSDStartForDSDHeader + sizeof(struct bcm_dsd_header))) &&
				(UNINIT_PTR_IN_CS != Adapter->psFlash2xVendorInfo->VendorSection[i].OffsetFromZeroForSectionStart))
				Adapter->psFlash2xCSInfo->OffsetFromZeroForDSD1Start = Adapter->psFlash2xCSInfo->OffsetFromZeroForDSD1End = VENDOR_PTR_IN_CS;
			else
				Adapter->psFlash2xCSInfo->OffsetFromZeroForDSD1Start = Adapter->psFlash2xCSInfo->OffsetFromZeroForDSD1End = UNINIT_PTR_IN_CS;
			break;

		case DSD2:
			if ((uiSizeSection >= (Adapter->psFlash2xCSInfo->OffsetFromDSDStartForDSDHeader + sizeof(struct bcm_dsd_header))) &&
				(UNINIT_PTR_IN_CS != Adapter->psFlash2xVendorInfo->VendorSection[i].OffsetFromZeroForSectionStart))
				Adapter->psFlash2xCSInfo->OffsetFromZeroForDSD2Start = Adapter->psFlash2xCSInfo->OffsetFromZeroForDSD2End = VENDOR_PTR_IN_CS;
			else
				Adapter->psFlash2xCSInfo->OffsetFromZeroForDSD2Start = Adapter->psFlash2xCSInfo->OffsetFromZeroForDSD2End = UNINIT_PTR_IN_CS;
			break;
		case VSA0:
			if (UNINIT_PTR_IN_CS != Adapter->psFlash2xVendorInfo->VendorSection[i].OffsetFromZeroForSectionStart)
				Adapter->psFlash2xCSInfo->OffsetFromZeroForVSAStart = Adapter->psFlash2xCSInfo->OffsetFromZeroForVSAEnd = VENDOR_PTR_IN_CS;
			else
				Adapter->psFlash2xCSInfo->OffsetFromZeroForVSAStart = Adapter->psFlash2xCSInfo->OffsetFromZeroForVSAEnd = UNINIT_PTR_IN_CS;
			break;

		case VSA1:
			if (UNINIT_PTR_IN_CS != Adapter->psFlash2xVendorInfo->VendorSection[i].OffsetFromZeroForSectionStart)
				Adapter->psFlash2xCSInfo->OffsetFromZeroForVSA1Start = Adapter->psFlash2xCSInfo->OffsetFromZeroForVSA1End = VENDOR_PTR_IN_CS;
			else
				Adapter->psFlash2xCSInfo->OffsetFromZeroForVSA1Start = Adapter->psFlash2xCSInfo->OffsetFromZeroForVSA1End = UNINIT_PTR_IN_CS;
			break;
		case VSA2:
			if (UNINIT_PTR_IN_CS != Adapter->psFlash2xVendorInfo->VendorSection[i].OffsetFromZeroForSectionStart)
				Adapter->psFlash2xCSInfo->OffsetFromZeroForVSA2Start = Adapter->psFlash2xCSInfo->OffsetFromZeroForVSA2End = VENDOR_PTR_IN_CS;
			else
				Adapter->psFlash2xCSInfo->OffsetFromZeroForVSA2Start = Adapter->psFlash2xCSInfo->OffsetFromZeroForVSA2End = UNINIT_PTR_IN_CS;
			break;

		default:
			break;
		}
		i++;
	}
}

/*
 * Procedure:	BcmGetFlashCSInfo
 *
 * Description: Reads control structure and gets Cal section addresses.
 *
 * Arguments:
 *		Adapter    - ptr to Adapter object instance
 *
 * Returns:
 *		<VOID>
 */

static int BcmGetFlashCSInfo(struct bcm_mini_adapter *Adapter)
{
	/* struct bcm_flash_cs_info sFlashCsInfo = {0}; */

	#if !defined(BCM_SHM_INTERFACE) || defined(FLASH_DIRECT_ACCESS)
		unsigned int value;
	#endif

	unsigned int uiFlashLayoutMajorVersion;
	Adapter->uiFlashLayoutMinorVersion = 0;
	Adapter->uiFlashLayoutMajorVersion = 0;
	Adapter->ulFlashControlSectionStart = FLASH_CS_INFO_START_ADDR;

	Adapter->uiFlashBaseAdd = 0;
	Adapter->ulFlashCalStart = 0;
	memset(Adapter->psFlashCSInfo, 0 , sizeof(struct bcm_flash_cs_info));
	memset(Adapter->psFlash2xCSInfo, 0 , sizeof(struct bcm_flash2x_cs_info));

	if (!Adapter->bDDRInitDone) {
		value = FLASH_CONTIGIOUS_START_ADDR_BEFORE_INIT;
		wrmalt(Adapter, 0xAF00A080, &value, sizeof(value));
	}

	/* Reading first 8 Bytes to get the Flash Layout
	 * MagicNumber(4 bytes) +FlashLayoutMinorVersion(2 Bytes) +FlashLayoutMajorVersion(2 Bytes)
	 */
	BeceemFlashBulkRead(Adapter, (PUINT)Adapter->psFlashCSInfo, Adapter->ulFlashControlSectionStart, 8);

	Adapter->psFlashCSInfo->FlashLayoutVersion =  ntohl(Adapter->psFlashCSInfo->FlashLayoutVersion);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "Flash Layout Version :%X", (Adapter->psFlashCSInfo->FlashLayoutVersion));
	/* BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "Flash Layout Minor Version :%d\n", ntohs(sFlashCsInfo.FlashLayoutMinorVersion)); */
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "Signature is  :%x\n", ntohl(Adapter->psFlashCSInfo->MagicNumber));

	if (FLASH_CONTROL_STRUCT_SIGNATURE == ntohl(Adapter->psFlashCSInfo->MagicNumber)) {
		uiFlashLayoutMajorVersion = MAJOR_VERSION((Adapter->psFlashCSInfo->FlashLayoutVersion));
		Adapter->uiFlashLayoutMinorVersion = MINOR_VERSION((Adapter->psFlashCSInfo->FlashLayoutVersion));
	} else {
		Adapter->uiFlashLayoutMinorVersion = 0;
		uiFlashLayoutMajorVersion = 0;
	}

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "FLASH LAYOUT MAJOR VERSION :%X", uiFlashLayoutMajorVersion);

	if (uiFlashLayoutMajorVersion < FLASH_2X_MAJOR_NUMBER) {
		BeceemFlashBulkRead(Adapter, (PUINT)Adapter->psFlashCSInfo, Adapter->ulFlashControlSectionStart, sizeof(struct bcm_flash_cs_info));
		ConvertEndianOfCSStructure(Adapter->psFlashCSInfo);
		Adapter->ulFlashCalStart = (Adapter->psFlashCSInfo->OffsetFromZeroForCalibrationStart);

		if (!((Adapter->uiFlashLayoutMajorVersion == 1) && (Adapter->uiFlashLayoutMinorVersion == 1)))
			Adapter->ulFlashControlSectionStart = Adapter->psFlashCSInfo->OffsetFromZeroForControlSectionStart;

		if ((FLASH_CONTROL_STRUCT_SIGNATURE == (Adapter->psFlashCSInfo->MagicNumber)) &&
			(SCSI_FIRMWARE_MINOR_VERSION <= MINOR_VERSION(Adapter->psFlashCSInfo->SCSIFirmwareVersion)) &&
			(FLASH_SECTOR_SIZE_SIG == (Adapter->psFlashCSInfo->FlashSectorSizeSig)) &&
			(BYTE_WRITE_SUPPORT == (Adapter->psFlashCSInfo->FlashWriteSupportSize))) {
			Adapter->ulFlashWriteSize = (Adapter->psFlashCSInfo->FlashWriteSupportSize);
			Adapter->fpFlashWrite = flashByteWrite;
			Adapter->fpFlashWriteWithStatusCheck = flashByteWriteStatus;
		} else {
			Adapter->ulFlashWriteSize = MAX_RW_SIZE;
			Adapter->fpFlashWrite = flashWrite;
			Adapter->fpFlashWriteWithStatusCheck = flashWriteStatus;
		}

		BcmGetFlashSectorSize(Adapter, (Adapter->psFlashCSInfo->FlashSectorSizeSig),
				(Adapter->psFlashCSInfo->FlashSectorSize));
		Adapter->uiFlashBaseAdd = Adapter->psFlashCSInfo->FlashBaseAddr & 0xFCFFFFFF;
	} else {
		if (BcmFlash2xBulkRead(Adapter, (PUINT)Adapter->psFlash2xCSInfo, NO_SECTION_VAL,
					Adapter->ulFlashControlSectionStart, sizeof(struct bcm_flash2x_cs_info))) {
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Unable to read CS structure\n");
			return STATUS_FAILURE;
		}

		ConvertEndianOf2XCSStructure(Adapter->psFlash2xCSInfo);
		BcmDumpFlash2XCSStructure(Adapter->psFlash2xCSInfo, Adapter);
		if ((FLASH_CONTROL_STRUCT_SIGNATURE == Adapter->psFlash2xCSInfo->MagicNumber) &&
			(SCSI_FIRMWARE_MINOR_VERSION <= MINOR_VERSION(Adapter->psFlash2xCSInfo->SCSIFirmwareVersion)) &&
			(FLASH_SECTOR_SIZE_SIG == Adapter->psFlash2xCSInfo->FlashSectorSizeSig) &&
			(BYTE_WRITE_SUPPORT == Adapter->psFlash2xCSInfo->FlashWriteSupportSize)) {
			Adapter->ulFlashWriteSize = Adapter->psFlash2xCSInfo->FlashWriteSupportSize;
			Adapter->fpFlashWrite = flashByteWrite;
			Adapter->fpFlashWriteWithStatusCheck = flashByteWriteStatus;
		} else {
			Adapter->ulFlashWriteSize = MAX_RW_SIZE;
			Adapter->fpFlashWrite = flashWrite;
			Adapter->fpFlashWriteWithStatusCheck = flashWriteStatus;
		}

		BcmGetFlashSectorSize(Adapter, Adapter->psFlash2xCSInfo->FlashSectorSizeSig,
				Adapter->psFlash2xCSInfo->FlashSectorSize);

		UpdateVendorInfo(Adapter);

		BcmGetActiveDSD(Adapter);
		BcmGetActiveISO(Adapter);
		Adapter->uiFlashBaseAdd = Adapter->psFlash2xCSInfo->FlashBaseAddr & 0xFCFFFFFF;
		Adapter->ulFlashControlSectionStart = Adapter->psFlash2xCSInfo->OffsetFromZeroForControlSectionStart;
	}
	/*
	 * Concerns: what if CS sector size does not match with this sector size ???
	 * what is the indication of AccessBitMap  in CS in flash 2.x ????
	 */
	Adapter->ulFlashID = BcmReadFlashRDID(Adapter);
	Adapter->uiFlashLayoutMajorVersion = uiFlashLayoutMajorVersion;

	return STATUS_SUCCESS;
}

/*
 * Procedure:	BcmGetNvmType
 *
 * Description: Finds the type of NVM used.
 *
 * Arguments:
 *		Adapter    - ptr to Adapter object instance
 *
 * Returns:
 *		NVM_TYPE
 *
 */

static enum bcm_nvm_type BcmGetNvmType(struct bcm_mini_adapter *Adapter)
{
	unsigned int uiData = 0;

	BeceemEEPROMBulkRead(Adapter, &uiData, 0x0, 4);
	if (uiData == BECM)
		return NVM_EEPROM;

	/*
	 * Read control struct and get cal addresses before accessing the flash
	 */
	BcmGetFlashCSInfo(Adapter);

	BeceemFlashBulkRead(Adapter, &uiData, 0x0 + Adapter->ulFlashCalStart, 4);
	if (uiData == BECM)
		return NVM_FLASH;

	/*
	 * even if there is no valid signature on EEPROM/FLASH find out if they really exist.
	 * if exist select it.
	 */
	if (BcmGetEEPROMSize(Adapter))
		return NVM_EEPROM;

	/* TBD for Flash. */
	return NVM_UNKNOWN;
}

/*
 * BcmGetSectionValStartOffset - this will calculate the section's starting offset if section val is given
 * @Adapter : Drivers Private Data structure
 * @eFlashSectionVal : Flash secion value defined in enum bcm_flash2x_section_val
 *
 * Return value:-
 * On success it return the start offset of the provided section val
 * On Failure -returns STATUS_FAILURE
 */

int BcmGetSectionValStartOffset(struct bcm_mini_adapter *Adapter, enum bcm_flash2x_section_val eFlashSectionVal)
{
	/*
	 * Considering all the section for which end offset can be calculated or directly given
	 * in CS Structure. if matching case does not exist, return STATUS_FAILURE indicating section
	 * endoffset can't be calculated or given in CS Structure.
	 */

	int SectStartOffset = 0;

	SectStartOffset = INVALID_OFFSET;

	if (IsSectionExistInVendorInfo(Adapter, eFlashSectionVal))
		return Adapter->psFlash2xVendorInfo->VendorSection[eFlashSectionVal].OffsetFromZeroForSectionStart;

	switch (eFlashSectionVal) {
	case ISO_IMAGE1:
		if ((Adapter->psFlash2xCSInfo->OffsetISOImage1Part1Start != UNINIT_PTR_IN_CS) &&
			(IsNonCDLessDevice(Adapter) == false))
			SectStartOffset = (Adapter->psFlash2xCSInfo->OffsetISOImage1Part1Start);
		break;
	case ISO_IMAGE2:
		if ((Adapter->psFlash2xCSInfo->OffsetISOImage2Part1Start != UNINIT_PTR_IN_CS) &&
			(IsNonCDLessDevice(Adapter) == false))
			SectStartOffset = (Adapter->psFlash2xCSInfo->OffsetISOImage2Part1Start);
		break;
	case DSD0:
		if (Adapter->psFlash2xCSInfo->OffsetFromZeroForDSDStart != UNINIT_PTR_IN_CS)
			SectStartOffset = (Adapter->psFlash2xCSInfo->OffsetFromZeroForDSDStart);
		break;
	case DSD1:
		if (Adapter->psFlash2xCSInfo->OffsetFromZeroForDSD1Start != UNINIT_PTR_IN_CS)
			SectStartOffset = (Adapter->psFlash2xCSInfo->OffsetFromZeroForDSD1Start);
		break;
	case DSD2:
		if (Adapter->psFlash2xCSInfo->OffsetFromZeroForDSD2Start != UNINIT_PTR_IN_CS)
			SectStartOffset = (Adapter->psFlash2xCSInfo->OffsetFromZeroForDSD2Start);
		break;
	case VSA0:
		if (Adapter->psFlash2xCSInfo->OffsetFromZeroForVSAStart != UNINIT_PTR_IN_CS)
			SectStartOffset = (Adapter->psFlash2xCSInfo->OffsetFromZeroForVSAStart);
		break;
	case VSA1:
		if (Adapter->psFlash2xCSInfo->OffsetFromZeroForVSA1Start != UNINIT_PTR_IN_CS)
			SectStartOffset = (Adapter->psFlash2xCSInfo->OffsetFromZeroForVSA1Start);
		break;
	case VSA2:
		if (Adapter->psFlash2xCSInfo->OffsetFromZeroForVSA2Start != UNINIT_PTR_IN_CS)
			SectStartOffset = (Adapter->psFlash2xCSInfo->OffsetFromZeroForVSA2Start);
		break;
	case SCSI:
		if (Adapter->psFlash2xCSInfo->OffsetFromZeroForScsiFirmware != UNINIT_PTR_IN_CS)
			SectStartOffset = (Adapter->psFlash2xCSInfo->OffsetFromZeroForScsiFirmware);
		break;
	case CONTROL_SECTION:
		if (Adapter->psFlash2xCSInfo->OffsetFromZeroForControlSectionStart != UNINIT_PTR_IN_CS)
			SectStartOffset = (Adapter->psFlash2xCSInfo->OffsetFromZeroForControlSectionStart);
		break;
	case ISO_IMAGE1_PART2:
		if (Adapter->psFlash2xCSInfo->OffsetISOImage1Part2Start != UNINIT_PTR_IN_CS)
			SectStartOffset = (Adapter->psFlash2xCSInfo->OffsetISOImage1Part2Start);
		break;
	case ISO_IMAGE1_PART3:
		if (Adapter->psFlash2xCSInfo->OffsetISOImage1Part3Start != UNINIT_PTR_IN_CS)
			SectStartOffset = (Adapter->psFlash2xCSInfo->OffsetISOImage1Part3Start);
		break;
	case ISO_IMAGE2_PART2:
		if (Adapter->psFlash2xCSInfo->OffsetISOImage2Part2Start != UNINIT_PTR_IN_CS)
			SectStartOffset = (Adapter->psFlash2xCSInfo->OffsetISOImage2Part2Start);
		break;
	case ISO_IMAGE2_PART3:
		if (Adapter->psFlash2xCSInfo->OffsetISOImage2Part3Start != UNINIT_PTR_IN_CS)
			SectStartOffset = (Adapter->psFlash2xCSInfo->OffsetISOImage2Part3Start);
		break;
	default:
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Section Does not exist in Flash 2.x");
		SectStartOffset = INVALID_OFFSET;
	}

	return SectStartOffset;
}

/*
 * BcmGetSectionValEndOffset - this will calculate the section's Ending offset if section val is given
 * @Adapter : Drivers Private Data structure
 * @eFlashSectionVal : Flash secion value defined in enum bcm_flash2x_section_val
 *
 * Return value:-
 * On success it return the end offset of the provided section val
 * On Failure -returns STATUS_FAILURE
 */

static int BcmGetSectionValEndOffset(struct bcm_mini_adapter *Adapter, enum bcm_flash2x_section_val eFlash2xSectionVal)
{
	int SectEndOffset = 0;

	SectEndOffset = INVALID_OFFSET;
	if (IsSectionExistInVendorInfo(Adapter, eFlash2xSectionVal))
		return Adapter->psFlash2xVendorInfo->VendorSection[eFlash2xSectionVal].OffsetFromZeroForSectionEnd;

	switch (eFlash2xSectionVal) {
	case ISO_IMAGE1:
		if ((Adapter->psFlash2xCSInfo->OffsetISOImage1Part1End != UNINIT_PTR_IN_CS) &&
			(IsNonCDLessDevice(Adapter) == false))
			SectEndOffset = (Adapter->psFlash2xCSInfo->OffsetISOImage1Part1End);
		break;
	case ISO_IMAGE2:
		if ((Adapter->psFlash2xCSInfo->OffsetISOImage2Part1End != UNINIT_PTR_IN_CS) &&
			(IsNonCDLessDevice(Adapter) == false))
			SectEndOffset = (Adapter->psFlash2xCSInfo->OffsetISOImage2Part1End);
		break;
	case DSD0:
		if (Adapter->psFlash2xCSInfo->OffsetFromZeroForDSDEnd != UNINIT_PTR_IN_CS)
			SectEndOffset = (Adapter->psFlash2xCSInfo->OffsetFromZeroForDSDEnd);
		break;
	case DSD1:
		if (Adapter->psFlash2xCSInfo->OffsetFromZeroForDSD1End != UNINIT_PTR_IN_CS)
			SectEndOffset = (Adapter->psFlash2xCSInfo->OffsetFromZeroForDSD1End);
		break;
	case DSD2:
		if (Adapter->psFlash2xCSInfo->OffsetFromZeroForDSD2End != UNINIT_PTR_IN_CS)
			SectEndOffset = (Adapter->psFlash2xCSInfo->OffsetFromZeroForDSD2End);
		break;
	case VSA0:
		if (Adapter->psFlash2xCSInfo->OffsetFromZeroForVSAEnd != UNINIT_PTR_IN_CS)
			SectEndOffset = (Adapter->psFlash2xCSInfo->OffsetFromZeroForVSAEnd);
		break;
	case VSA1:
		if (Adapter->psFlash2xCSInfo->OffsetFromZeroForVSA1End != UNINIT_PTR_IN_CS)
			SectEndOffset = (Adapter->psFlash2xCSInfo->OffsetFromZeroForVSA1End);
		break;
	case VSA2:
		if (Adapter->psFlash2xCSInfo->OffsetFromZeroForVSA2End != UNINIT_PTR_IN_CS)
			SectEndOffset = (Adapter->psFlash2xCSInfo->OffsetFromZeroForVSA2End);
		break;
	case SCSI:
		if (Adapter->psFlash2xCSInfo->OffsetFromZeroForScsiFirmware != UNINIT_PTR_IN_CS)
			SectEndOffset = ((Adapter->psFlash2xCSInfo->OffsetFromZeroForScsiFirmware) +
					(Adapter->psFlash2xCSInfo->SizeOfScsiFirmware));
		break;
	case CONTROL_SECTION:
		/* Not Clear So Putting failure. confirm and fix it. */
		SectEndOffset = STATUS_FAILURE;
		break;
	case ISO_IMAGE1_PART2:
		if (Adapter->psFlash2xCSInfo->OffsetISOImage1Part2End != UNINIT_PTR_IN_CS)
			SectEndOffset = (Adapter->psFlash2xCSInfo->OffsetISOImage1Part2End);
		break;
	case ISO_IMAGE1_PART3:
		if (Adapter->psFlash2xCSInfo->OffsetISOImage1Part3End != UNINIT_PTR_IN_CS)
			SectEndOffset = (Adapter->psFlash2xCSInfo->OffsetISOImage1Part3End);
		break;
	case ISO_IMAGE2_PART2:
		if (Adapter->psFlash2xCSInfo->OffsetISOImage2Part2End != UNINIT_PTR_IN_CS)
			SectEndOffset = (Adapter->psFlash2xCSInfo->OffsetISOImage2Part2End);
		break;
	case ISO_IMAGE2_PART3:
		if (Adapter->psFlash2xCSInfo->OffsetISOImage2Part3End != UNINIT_PTR_IN_CS)
			SectEndOffset = (Adapter->psFlash2xCSInfo->OffsetISOImage2Part3End);
		break;
	default:
		SectEndOffset = INVALID_OFFSET;
	}

	return SectEndOffset;
}

/*
 * BcmFlash2xBulkRead:- Read API for Flash Map 2.x .
 * @Adapter :Driver Private Data Structure
 * @pBuffer : Buffer where data has to be put after reading
 * @eFlashSectionVal :Flash Section Val defined in enum bcm_flash2x_section_val
 * @uiOffsetWithinSectionVal :- Offset with in provided section
 * @uiNumBytes : Number of Bytes for Read
 *
 * Return value:-
 * return true on success and STATUS_FAILURE on fail.
 */

int BcmFlash2xBulkRead(struct bcm_mini_adapter *Adapter,
		PUINT pBuffer,
		enum bcm_flash2x_section_val eFlash2xSectionVal,
		unsigned int uiOffsetWithinSectionVal,
		unsigned int uiNumBytes)
{
	int Status = STATUS_SUCCESS;
	int SectionStartOffset = 0;
	unsigned int uiAbsoluteOffset = 0;
	unsigned int uiTemp = 0, value = 0;

	if (!Adapter) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Adapter structure is NULL");
		return -EINVAL;
	}
	if (Adapter->device_removed) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Device has been removed");
		return -ENODEV;
	}

	/* NO_SECTION_VAL means absolute offset is given. */
	if (eFlash2xSectionVal == NO_SECTION_VAL)
		SectionStartOffset = 0;
	else
		SectionStartOffset = BcmGetSectionValStartOffset(Adapter, eFlash2xSectionVal);

	if (SectionStartOffset == STATUS_FAILURE) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "This Section<%d> does not exist in Flash 2.x Map ", eFlash2xSectionVal);
		return -EINVAL;
	}

	if (IsSectionExistInVendorInfo(Adapter, eFlash2xSectionVal))
		return vendorextnReadSection(Adapter, (PUCHAR)pBuffer, eFlash2xSectionVal, uiOffsetWithinSectionVal, uiNumBytes);

	/* calculating  the absolute offset from FLASH; */
	uiAbsoluteOffset = uiOffsetWithinSectionVal + SectionStartOffset;
	rdmalt(Adapter, 0x0f000C80, &uiTemp, sizeof(uiTemp));
	value = 0;
	wrmalt(Adapter, 0x0f000C80, &value, sizeof(value));
	Status = BeceemFlashBulkRead(Adapter, pBuffer, uiAbsoluteOffset, uiNumBytes);
	wrmalt(Adapter, 0x0f000C80, &uiTemp, sizeof(uiTemp));
	if (Status) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Flash Read Failed with Status :%d", Status);
		return Status;
	}

	return Status;
}

/*
 * BcmFlash2xBulkWrite :-API for Writing on the Flash Map 2.x.
 * @Adapter :Driver Private Data Structure
 * @pBuffer : Buffer From where data has to taken for writing
 * @eFlashSectionVal :Flash Section Val defined in enum bcm_flash2x_section_val
 * @uiOffsetWithinSectionVal :- Offset with in provided section
 * @uiNumBytes : Number of Bytes for Write
 *
 * Return value:-
 * return true on success and STATUS_FAILURE on fail.
 *
 */

int BcmFlash2xBulkWrite(struct bcm_mini_adapter *Adapter,
			PUINT pBuffer,
			enum bcm_flash2x_section_val eFlash2xSectVal,
			unsigned int uiOffset,
			unsigned int uiNumBytes,
			unsigned int bVerify)
{
	int Status = STATUS_SUCCESS;
	unsigned int FlashSectValStartOffset = 0;
	unsigned int uiTemp = 0, value = 0;

	if (!Adapter) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Adapter structure is NULL");
		return -EINVAL;
	}

	if (Adapter->device_removed) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Device has been removed");
		return -ENODEV;
	}

	/* NO_SECTION_VAL means absolute offset is given. */
	if (eFlash2xSectVal == NO_SECTION_VAL)
		FlashSectValStartOffset = 0;
	else
		FlashSectValStartOffset = BcmGetSectionValStartOffset(Adapter, eFlash2xSectVal);

	if (FlashSectValStartOffset == STATUS_FAILURE) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "This Section<%d> does not exist in Flash Map 2.x", eFlash2xSectVal);
		return -EINVAL;
	}

	if (IsSectionExistInVendorInfo(Adapter, eFlash2xSectVal))
		return vendorextnWriteSection(Adapter, (PUCHAR)pBuffer, eFlash2xSectVal, uiOffset, uiNumBytes, bVerify);

	/* calculating  the absolute offset from FLASH; */
	uiOffset = uiOffset + FlashSectValStartOffset;

	rdmalt(Adapter, 0x0f000C80, &uiTemp, sizeof(uiTemp));
	value = 0;
	wrmalt(Adapter, 0x0f000C80, &value, sizeof(value));

	Status = BeceemFlashBulkWrite(Adapter, pBuffer, uiOffset, uiNumBytes, bVerify);

	wrmalt(Adapter, 0x0f000C80, &uiTemp, sizeof(uiTemp));
	if (Status) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Flash Write failed with Status :%d", Status);
		return Status;
	}

	return Status;
}

/*
 * BcmGetActiveDSD : Set the Active DSD in Adapter Structure which has to be dumped in DDR
 * @Adapter :-Drivers private Data Structure
 *
 * Return Value:-
 * Return STATUS_SUCESS if get success in setting the right DSD else negative error code
 *
 */

static int BcmGetActiveDSD(struct bcm_mini_adapter *Adapter)
{
	enum bcm_flash2x_section_val uiHighestPriDSD = 0;

	uiHighestPriDSD = getHighestPriDSD(Adapter);
	Adapter->eActiveDSD = uiHighestPriDSD;

	if (DSD0  == uiHighestPriDSD)
		Adapter->ulFlashCalStart = Adapter->psFlash2xCSInfo->OffsetFromZeroForDSDStart;
	if (DSD1 == uiHighestPriDSD)
		Adapter->ulFlashCalStart = Adapter->psFlash2xCSInfo->OffsetFromZeroForDSD1Start;
	if (DSD2 == uiHighestPriDSD)
		Adapter->ulFlashCalStart = Adapter->psFlash2xCSInfo->OffsetFromZeroForDSD2Start;
	if (Adapter->eActiveDSD)
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "Active DSD :%d", Adapter->eActiveDSD);
	if (Adapter->eActiveDSD == 0) {
		/* if No DSD gets Active, Make Active the DSD with WR  permission */
		if (IsSectionWritable(Adapter, DSD2)) {
			Adapter->eActiveDSD = DSD2;
			Adapter->ulFlashCalStart = Adapter->psFlash2xCSInfo->OffsetFromZeroForDSD2Start;
		} else if (IsSectionWritable(Adapter, DSD1)) {
			Adapter->eActiveDSD = DSD1;
			Adapter->ulFlashCalStart = Adapter->psFlash2xCSInfo->OffsetFromZeroForDSD1Start;
		} else if (IsSectionWritable(Adapter, DSD0)) {
			Adapter->eActiveDSD = DSD0;
			Adapter->ulFlashCalStart = Adapter->psFlash2xCSInfo->OffsetFromZeroForDSDStart;
		}
	}

	return STATUS_SUCCESS;
}

/*
 * BcmGetActiveISO :- Set the Active ISO in Adapter Data Structue
 * @Adapter : Driver private Data Structure
 *
 * Return Value:-
 * Sucsess:- STATUS_SUCESS
 * Failure- : negative erro code
 *
 */

static int BcmGetActiveISO(struct bcm_mini_adapter *Adapter)
{
	int HighestPriISO = 0;

	HighestPriISO = getHighestPriISO(Adapter);

	Adapter->eActiveISO = HighestPriISO;
	if (Adapter->eActiveISO == ISO_IMAGE2)
		Adapter->uiActiveISOOffset = (Adapter->psFlash2xCSInfo->OffsetISOImage2Part1Start);
	else if (Adapter->eActiveISO == ISO_IMAGE1)
		Adapter->uiActiveISOOffset = (Adapter->psFlash2xCSInfo->OffsetISOImage1Part1Start);

	if (Adapter->eActiveISO)
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "Active ISO :%x", Adapter->eActiveISO);

	return STATUS_SUCCESS;
}

/*
 * IsOffsetWritable :- it will tell the access permission of the sector having passed offset
 * @Adapter : Drivers Private Data Structure
 * @uiOffset : Offset provided in the Flash
 *
 * Return Value:-
 * Success:-TRUE ,  offset is writable
 * Failure:-false, offset is RO
 *
 */

static B_UINT8 IsOffsetWritable(struct bcm_mini_adapter *Adapter, unsigned int uiOffset)
{
	unsigned int uiSectorNum = 0;
	unsigned int uiWordOfSectorPermission = 0;
	unsigned int uiBitofSectorePermission = 0;
	B_UINT32 permissionBits = 0;

	uiSectorNum = uiOffset/Adapter->uiSectorSize;

	/* calculating the word having this Sector Access permission from SectorAccessBitMap Array */
	uiWordOfSectorPermission = Adapter->psFlash2xCSInfo->SectorAccessBitMap[uiSectorNum / 16];

	/* calculating the bit index inside the word for  this sector */
	uiBitofSectorePermission = 2 * (15 - uiSectorNum % 16);

	/* Setting Access permission */
	permissionBits = uiWordOfSectorPermission & (0x3 << uiBitofSectorePermission);
	permissionBits = (permissionBits >> uiBitofSectorePermission) & 0x3;
	if (permissionBits == SECTOR_READWRITE_PERMISSION)
		return TRUE;
	else
		return false;
}

static int BcmDumpFlash2xSectionBitMap(struct bcm_flash2x_bitmap *psFlash2xBitMap)
{
	struct bcm_mini_adapter *Adapter = GET_BCM_ADAPTER(gblpnetdev);

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "***************Flash 2.x Section Bitmap***************");
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "ISO_IMAGE1  :0X%x", psFlash2xBitMap->ISO_IMAGE1);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "ISO_IMAGE2  :0X%x", psFlash2xBitMap->ISO_IMAGE2);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "DSD0  :0X%x", psFlash2xBitMap->DSD0);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "DSD1  :0X%x", psFlash2xBitMap->DSD1);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "DSD2  :0X%x", psFlash2xBitMap->DSD2);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "VSA0  :0X%x", psFlash2xBitMap->VSA0);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "VSA1  :0X%x", psFlash2xBitMap->VSA1);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "VSA2  :0X%x", psFlash2xBitMap->VSA2);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "SCSI  :0X%x", psFlash2xBitMap->SCSI);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "CONTROL_SECTION  :0X%x", psFlash2xBitMap->CONTROL_SECTION);

	return STATUS_SUCCESS;
}

/*
 * BcmGetFlash2xSectionalBitMap :- It will provide the bit map of all the section present in Flash
 * 8bit has been assigned to every section.
 * bit[0] :Section present or not
 * bit[1] :section is valid or not
 * bit[2] : Secton is read only or has write permission too.
 * bit[3] : Active Section -
 * bit[7...4] = Reserved .
 *
 * @Adapter:-Driver private Data Structure
 *
 * Return value:-
 * Success:- STATUS_SUCESS
 * Failure:- negative error code
 */

int BcmGetFlash2xSectionalBitMap(struct bcm_mini_adapter *Adapter, struct bcm_flash2x_bitmap *psFlash2xBitMap)
{
	struct bcm_flash2x_cs_info *psFlash2xCSInfo = Adapter->psFlash2xCSInfo;
	enum bcm_flash2x_section_val uiHighestPriDSD = 0;
	enum bcm_flash2x_section_val uiHighestPriISO = 0;
	bool SetActiveDSDDone = false;
	bool SetActiveISODone = false;

	/* For 1.x map all the section except DSD0 will be shown as not present
	 * This part will be used by calibration tool to detect the number of DSD present in Flash.
	 */
	if (IsFlash2x(Adapter) == false) {
		psFlash2xBitMap->ISO_IMAGE2 = 0;
		psFlash2xBitMap->ISO_IMAGE1 = 0;
		psFlash2xBitMap->DSD0 = FLASH2X_SECTION_VALID | FLASH2X_SECTION_ACT | FLASH2X_SECTION_PRESENT; /* 0xF; 0000(Reseved)1(Active)0(RW)1(valid)1(present) */
		psFlash2xBitMap->DSD1  = 0;
		psFlash2xBitMap->DSD2 = 0;
		psFlash2xBitMap->VSA0 = 0;
		psFlash2xBitMap->VSA1 = 0;
		psFlash2xBitMap->VSA2 = 0;
		psFlash2xBitMap->CONTROL_SECTION = 0;
		psFlash2xBitMap->SCSI = 0;
		psFlash2xBitMap->Reserved0 = 0;
		psFlash2xBitMap->Reserved1 = 0;
		psFlash2xBitMap->Reserved2 = 0;

		return STATUS_SUCCESS;
	}

	uiHighestPriDSD = getHighestPriDSD(Adapter);
	uiHighestPriISO = getHighestPriISO(Adapter);

	/*
	 * IS0 IMAGE 2
	 */
	if ((psFlash2xCSInfo->OffsetISOImage2Part1Start) != UNINIT_PTR_IN_CS) {
		/* Setting the 0th Bit representing the Section is present or not. */
		psFlash2xBitMap->ISO_IMAGE2 = psFlash2xBitMap->ISO_IMAGE2 | FLASH2X_SECTION_PRESENT;

		if (ReadISOSignature(Adapter, ISO_IMAGE2) == ISO_IMAGE_MAGIC_NUMBER)
			psFlash2xBitMap->ISO_IMAGE2 |= FLASH2X_SECTION_VALID;

		/* Calculation for extrating the Access permission */
		if (IsSectionWritable(Adapter, ISO_IMAGE2) == false)
			psFlash2xBitMap->ISO_IMAGE2 |= FLASH2X_SECTION_RO;

		if (SetActiveISODone == false && uiHighestPriISO == ISO_IMAGE2) {
			psFlash2xBitMap->ISO_IMAGE2 |= FLASH2X_SECTION_ACT;
			SetActiveISODone = TRUE;
		}
	}

	/*
	 * IS0 IMAGE 1
	 */
	if ((psFlash2xCSInfo->OffsetISOImage1Part1Start) != UNINIT_PTR_IN_CS) {
		/* Setting the 0th Bit representing the Section is present or not. */
		psFlash2xBitMap->ISO_IMAGE1 = psFlash2xBitMap->ISO_IMAGE1 | FLASH2X_SECTION_PRESENT;

		if (ReadISOSignature(Adapter, ISO_IMAGE1) == ISO_IMAGE_MAGIC_NUMBER)
			psFlash2xBitMap->ISO_IMAGE1 |= FLASH2X_SECTION_VALID;

		/* Calculation for extrating the Access permission */
		if (IsSectionWritable(Adapter, ISO_IMAGE1) == false)
			psFlash2xBitMap->ISO_IMAGE1 |= FLASH2X_SECTION_RO;

		if (SetActiveISODone == false && uiHighestPriISO == ISO_IMAGE1) {
			psFlash2xBitMap->ISO_IMAGE1 |= FLASH2X_SECTION_ACT;
			SetActiveISODone = TRUE;
		}
	}

	/*
	 * DSD2
	 */
	if ((psFlash2xCSInfo->OffsetFromZeroForDSD2Start) != UNINIT_PTR_IN_CS) {
		/* Setting the 0th Bit representing the Section is present or not. */
		psFlash2xBitMap->DSD2 = psFlash2xBitMap->DSD2 | FLASH2X_SECTION_PRESENT;

		if (ReadDSDSignature(Adapter, DSD2) == DSD_IMAGE_MAGIC_NUMBER)
			psFlash2xBitMap->DSD2 |= FLASH2X_SECTION_VALID;

		/* Calculation for extrating the Access permission */
		if (IsSectionWritable(Adapter, DSD2) == false) {
			psFlash2xBitMap->DSD2 |= FLASH2X_SECTION_RO;
		} else {
			/* Means section is writable */
			if ((SetActiveDSDDone == false) && (uiHighestPriDSD == DSD2)) {
				psFlash2xBitMap->DSD2 |= FLASH2X_SECTION_ACT;
				SetActiveDSDDone = TRUE;
			}
		}
	}

	/*
	 * DSD 1
	 */
	if ((psFlash2xCSInfo->OffsetFromZeroForDSD1Start) != UNINIT_PTR_IN_CS) {
		/* Setting the 0th Bit representing the Section is present or not. */
		psFlash2xBitMap->DSD1 = psFlash2xBitMap->DSD1 | FLASH2X_SECTION_PRESENT;

		if (ReadDSDSignature(Adapter, DSD1) == DSD_IMAGE_MAGIC_NUMBER)
			psFlash2xBitMap->DSD1 |= FLASH2X_SECTION_VALID;

		/* Calculation for extrating the Access permission */
		if (IsSectionWritable(Adapter, DSD1) == false) {
			psFlash2xBitMap->DSD1 |= FLASH2X_SECTION_RO;
		} else {
			/* Means section is writable */
			if ((SetActiveDSDDone == false) && (uiHighestPriDSD == DSD1)) {
				psFlash2xBitMap->DSD1 |= FLASH2X_SECTION_ACT;
				SetActiveDSDDone = TRUE;
			}
		}
	}

	/*
	 * For DSD 0
	 */
	if ((psFlash2xCSInfo->OffsetFromZeroForDSDStart) != UNINIT_PTR_IN_CS) {
		/* Setting the 0th Bit representing the Section is present or not. */
		psFlash2xBitMap->DSD0 = psFlash2xBitMap->DSD0 | FLASH2X_SECTION_PRESENT;

		if (ReadDSDSignature(Adapter, DSD0) == DSD_IMAGE_MAGIC_NUMBER)
			psFlash2xBitMap->DSD0 |= FLASH2X_SECTION_VALID;

		/* Setting Access permission */
		if (IsSectionWritable(Adapter, DSD0) == false) {
			psFlash2xBitMap->DSD0 |= FLASH2X_SECTION_RO;
		} else {
			/* Means section is writable */
			if ((SetActiveDSDDone == false) && (uiHighestPriDSD == DSD0)) {
				psFlash2xBitMap->DSD0 |= FLASH2X_SECTION_ACT;
				SetActiveDSDDone = TRUE;
			}
		}
	}

	/*
	 * VSA 0
	 */
	if ((psFlash2xCSInfo->OffsetFromZeroForVSAStart) != UNINIT_PTR_IN_CS) {
		/* Setting the 0th Bit representing the Section is present or not. */
		psFlash2xBitMap->VSA0 = psFlash2xBitMap->VSA0 | FLASH2X_SECTION_PRESENT;

		/* Setting the Access Bit. Map is not defined hece setting it always valid */
		psFlash2xBitMap->VSA0 |= FLASH2X_SECTION_VALID;

		/* Calculation for extrating the Access permission */
		if (IsSectionWritable(Adapter, VSA0) == false)
			psFlash2xBitMap->VSA0 |=  FLASH2X_SECTION_RO;

		/* By Default section is Active */
		psFlash2xBitMap->VSA0 |= FLASH2X_SECTION_ACT;
	}

	/*
	 * VSA 1
	 */
	if ((psFlash2xCSInfo->OffsetFromZeroForVSA1Start) != UNINIT_PTR_IN_CS) {
		/* Setting the 0th Bit representing the Section is present or not. */
		psFlash2xBitMap->VSA1 = psFlash2xBitMap->VSA1 | FLASH2X_SECTION_PRESENT;

		/* Setting the Access Bit. Map is not defined hece setting it always valid */
		psFlash2xBitMap->VSA1 |= FLASH2X_SECTION_VALID;

		/* Checking For Access permission */
		if (IsSectionWritable(Adapter, VSA1) == false)
			psFlash2xBitMap->VSA1 |= FLASH2X_SECTION_RO;

		/* By Default section is Active */
		psFlash2xBitMap->VSA1 |= FLASH2X_SECTION_ACT;
	}

	/*
	 * VSA 2
	 */
	if ((psFlash2xCSInfo->OffsetFromZeroForVSA2Start) != UNINIT_PTR_IN_CS) {
		/* Setting the 0th Bit representing the Section is present or not. */
		psFlash2xBitMap->VSA2 = psFlash2xBitMap->VSA2 | FLASH2X_SECTION_PRESENT;

		/* Setting the Access Bit. Map is not defined hece setting it always valid */
		psFlash2xBitMap->VSA2 |= FLASH2X_SECTION_VALID;

		/* Checking For Access permission */
		if (IsSectionWritable(Adapter, VSA2) == false)
			psFlash2xBitMap->VSA2 |= FLASH2X_SECTION_RO;

		/* By Default section is Active */
		psFlash2xBitMap->VSA2 |= FLASH2X_SECTION_ACT;
	}

	/*
	 * SCSI Section
	 */
	if ((psFlash2xCSInfo->OffsetFromZeroForScsiFirmware) != UNINIT_PTR_IN_CS) {
		/* Setting the 0th Bit representing the Section is present or not. */
		psFlash2xBitMap->SCSI = psFlash2xBitMap->SCSI | FLASH2X_SECTION_PRESENT;

		/* Setting the Access Bit. Map is not defined hece setting it always valid */
		psFlash2xBitMap->SCSI |= FLASH2X_SECTION_VALID;

		/* Checking For Access permission */
		if (IsSectionWritable(Adapter, SCSI) == false)
			psFlash2xBitMap->SCSI |= FLASH2X_SECTION_RO;

		/* By Default section is Active */
		psFlash2xBitMap->SCSI |= FLASH2X_SECTION_ACT;
	}

	/*
	 * Control Section
	 */
	if ((psFlash2xCSInfo->OffsetFromZeroForControlSectionStart) != UNINIT_PTR_IN_CS) {
		/* Setting the 0th Bit representing the Section is present or not. */
		psFlash2xBitMap->CONTROL_SECTION = psFlash2xBitMap->CONTROL_SECTION | (FLASH2X_SECTION_PRESENT);

		/* Setting the Access Bit. Map is not defined hece setting it always valid */
		psFlash2xBitMap->CONTROL_SECTION |= FLASH2X_SECTION_VALID;

		/* Checking For Access permission */
		if (IsSectionWritable(Adapter, CONTROL_SECTION) == false)
			psFlash2xBitMap->CONTROL_SECTION |= FLASH2X_SECTION_RO;

		/* By Default section is Active */
		psFlash2xBitMap->CONTROL_SECTION |= FLASH2X_SECTION_ACT;
	}

	/*
	 * For Reserved Sections
	 */
	psFlash2xBitMap->Reserved0 = 0;
	psFlash2xBitMap->Reserved0 = 0;
	psFlash2xBitMap->Reserved0 = 0;
	BcmDumpFlash2xSectionBitMap(psFlash2xBitMap);

	return STATUS_SUCCESS;
}

/*
 * BcmSetActiveSection :- Set Active section is used to make priority field highest over other
 * section of same type.
 *
 * @Adapater :- Bcm Driver Private Data Structure
 * @eFlash2xSectionVal :- Flash section val whose priority has to be made highest.
 *
 * Return Value:- Make the priorit highest else return erorr code
 *
 */

int BcmSetActiveSection(struct bcm_mini_adapter *Adapter, enum bcm_flash2x_section_val eFlash2xSectVal)
{
	unsigned int SectImagePriority = 0;
	int Status = STATUS_SUCCESS;

	/* struct bcm_dsd_header sDSD = {0};
	 * struct bcm_iso_header sISO = {0};
	 */
	int HighestPriDSD = 0;
	int HighestPriISO = 0;

	Status = IsSectionWritable(Adapter, eFlash2xSectVal);
	if (Status != TRUE) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Provided Section <%d> is not writable", eFlash2xSectVal);
		return STATUS_FAILURE;
	}

	Adapter->bHeaderChangeAllowed = TRUE;
	switch (eFlash2xSectVal) {
	case ISO_IMAGE1:
	case ISO_IMAGE2:
		if (ReadISOSignature(Adapter, eFlash2xSectVal) == ISO_IMAGE_MAGIC_NUMBER) {
			HighestPriISO = getHighestPriISO(Adapter);

			if (HighestPriISO == eFlash2xSectVal) {
				BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "Given ISO<%x> already has highest priority", eFlash2xSectVal);
				Status = STATUS_SUCCESS;
				break;
			}

			SectImagePriority = ReadISOPriority(Adapter, HighestPriISO) + 1;

			if ((SectImagePriority == 0) && IsSectionWritable(Adapter, HighestPriISO)) {
				/* This is a SPECIAL Case which will only happen if the current highest priority ISO has priority value = 0x7FFFFFFF.
				 * We will write 1 to the current Highest priority ISO And then shall increase the priority of the requested ISO
				 * by user
				 */
				BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "SectImagePriority wraparound happened, eFlash2xSectVal: 0x%x\n", eFlash2xSectVal);
				SectImagePriority = htonl(0x1);
				Status = BcmFlash2xBulkWrite(Adapter,
							&SectImagePriority,
							HighestPriISO,
							0 + FIELD_OFFSET_IN_HEADER(struct bcm_iso_header *, ISOImagePriority),
							SIGNATURE_SIZE,
							TRUE);
				if (Status) {
					BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Priority has not been written properly");
					Status = STATUS_FAILURE;
					break;
				}

				HighestPriISO = getHighestPriISO(Adapter);

				if (HighestPriISO == eFlash2xSectVal) {
					BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "Given ISO<%x> already has highest priority", eFlash2xSectVal);
					Status = STATUS_SUCCESS;
					break;
				}

				SectImagePriority = 2;
			}

			SectImagePriority = htonl(SectImagePriority);

			Status = BcmFlash2xBulkWrite(Adapter,
						&SectImagePriority,
						eFlash2xSectVal,
						0 + FIELD_OFFSET_IN_HEADER(struct bcm_iso_header *, ISOImagePriority),
						SIGNATURE_SIZE,
						TRUE);
			if (Status) {
				BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Priority has not been written properly");
				break;
			}
		} else {
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Signature is currupted. Hence can't increase the priority");
			Status = STATUS_FAILURE;
			break;
		}
		break;
	case DSD0:
	case DSD1:
	case DSD2:
		if (ReadDSDSignature(Adapter, eFlash2xSectVal) == DSD_IMAGE_MAGIC_NUMBER) {
			HighestPriDSD = getHighestPriDSD(Adapter);
			if (HighestPriDSD == eFlash2xSectVal) {
				BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "Given DSD<%x> already has highest priority", eFlash2xSectVal);
				Status = STATUS_SUCCESS;
				break;
			}

			SectImagePriority = ReadDSDPriority(Adapter, HighestPriDSD) + 1;
			if (SectImagePriority == 0) {
				/* This is a SPECIAL Case which will only happen if the current highest priority DSD has priority value = 0x7FFFFFFF.
				 * We will write 1 to the current Highest priority DSD And then shall increase the priority of the requested DSD
				 * by user
				 */
				BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, NVM_RW, DBG_LVL_ALL, "SectImagePriority wraparound happened, eFlash2xSectVal: 0x%x\n", eFlash2xSectVal);
				SectImagePriority = htonl(0x1);

				Status = BcmFlash2xBulkWrite(Adapter,
							&SectImagePriority,
							HighestPriDSD,
							Adapter->psFlash2xCSInfo->OffsetFromDSDStartForDSDHeader + FIELD_OFFSET_IN_HEADER(struct bcm_dsd_header *, DSDImagePriority),
							SIGNATURE_SIZE,
							TRUE);
				if (Status) {
					BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Priority has not been written properly");
					break;
				}

				HighestPriDSD = getHighestPriDSD(Adapter);

				if (HighestPriDSD == eFlash2xSectVal) {
					BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "Made the DSD: %x highest by reducing priority of other\n", eFlash2xSectVal);
					Status = STATUS_SUCCESS;
					break;
				}

				SectImagePriority = htonl(0x2);
				Status = BcmFlash2xBulkWrite(Adapter,
							&SectImagePriority,
							HighestPriDSD,
							Adapter->psFlash2xCSInfo->OffsetFromDSDStartForDSDHeader + FIELD_OFFSET_IN_HEADER(struct bcm_dsd_header *, DSDImagePriority),
							SIGNATURE_SIZE,
							TRUE);
				if (Status) {
					BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Priority has not been written properly");
					break;
				}

				HighestPriDSD = getHighestPriDSD(Adapter);
				if (HighestPriDSD == eFlash2xSectVal) {
					Status = STATUS_SUCCESS;
					break;
				}

				SectImagePriority = 3;
			}
			SectImagePriority = htonl(SectImagePriority);
			Status = BcmFlash2xBulkWrite(Adapter,
						&SectImagePriority,
						eFlash2xSectVal,
						Adapter->psFlash2xCSInfo->OffsetFromDSDStartForDSDHeader + FIELD_OFFSET_IN_HEADER(struct bcm_dsd_header *, DSDImagePriority),
						SIGNATURE_SIZE,
						TRUE);
			if (Status) {
				BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Priority has not been written properly");
				Status = STATUS_FAILURE;
				break;
			}
		} else {
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Signature is currupted. Hence can't increase the priority");
			Status = STATUS_FAILURE;
			break;
		}
		break;
	case VSA0:
	case VSA1:
	case VSA2:
		/* Has to be decided */
		break;
	default:
		Status = STATUS_FAILURE;
		break;
	}

	Adapter->bHeaderChangeAllowed = false;
	return Status;
}

/*
 * BcmCopyISO - Used only for copying the ISO section
 * @Adapater :- Bcm Driver Private Data Structure
 * @sCopySectStrut :- Section copy structure
 *
 * Return value:- SUCCESS if copies successfully else negative error code
 *
 */

int BcmCopyISO(struct bcm_mini_adapter *Adapter, struct bcm_flash2x_copy_section sCopySectStrut)
{
	PCHAR Buff = NULL;
	enum bcm_flash2x_section_val eISOReadPart = 0, eISOWritePart = 0;
	unsigned int uiReadOffsetWithinPart = 0, uiWriteOffsetWithinPart = 0;
	unsigned int uiTotalDataToCopy = 0;
	bool IsThisHeaderSector = false;
	unsigned int sigOffset = 0;
	unsigned int ISOLength = 0;
	unsigned int Status = STATUS_SUCCESS;
	unsigned int SigBuff[MAX_RW_SIZE];
	unsigned int i = 0;

	if (ReadISOSignature(Adapter, sCopySectStrut.SrcSection) != ISO_IMAGE_MAGIC_NUMBER) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "error as Source ISO Section does not have valid signature");
		return STATUS_FAILURE;
	}

	Status = BcmFlash2xBulkRead(Adapter,
				&ISOLength,
				sCopySectStrut.SrcSection,
				0 + FIELD_OFFSET_IN_HEADER(struct bcm_iso_header *, ISOImageSize),
				4);
	if (Status) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Read failed while copying ISO\n");
		return Status;
	}

	ISOLength = htonl(ISOLength);
	if (ISOLength % Adapter->uiSectorSize)
		ISOLength = Adapter->uiSectorSize * (1 + ISOLength/Adapter->uiSectorSize);

	sigOffset = FIELD_OFFSET_IN_HEADER(struct bcm_iso_header *, ISOImageMagicNumber);

	Buff = kzalloc(Adapter->uiSectorSize, GFP_KERNEL);

	if (!Buff) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Memory allocation failed for section size");
		return -ENOMEM;
	}

	if (sCopySectStrut.SrcSection == ISO_IMAGE1 && sCopySectStrut.DstSection == ISO_IMAGE2) {
		eISOReadPart = ISO_IMAGE1;
		eISOWritePart = ISO_IMAGE2;
		uiReadOffsetWithinPart =  0;
		uiWriteOffsetWithinPart = 0;

		uiTotalDataToCopy = (Adapter->psFlash2xCSInfo->OffsetISOImage1Part1End) -
			(Adapter->psFlash2xCSInfo->OffsetISOImage1Part1Start) +
			(Adapter->psFlash2xCSInfo->OffsetISOImage1Part2End) -
			(Adapter->psFlash2xCSInfo->OffsetISOImage1Part2Start) +
			(Adapter->psFlash2xCSInfo->OffsetISOImage1Part3End) -
			(Adapter->psFlash2xCSInfo->OffsetISOImage1Part3Start);

		if (uiTotalDataToCopy < ISOLength) {
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "error as Source ISO Section does not have valid signature");
			Status = STATUS_FAILURE;
			goto out;
		}

		uiTotalDataToCopy = (Adapter->psFlash2xCSInfo->OffsetISOImage2Part1End) -
			(Adapter->psFlash2xCSInfo->OffsetISOImage2Part1Start) +
			(Adapter->psFlash2xCSInfo->OffsetISOImage2Part2End) -
			(Adapter->psFlash2xCSInfo->OffsetISOImage2Part2Start) +
			(Adapter->psFlash2xCSInfo->OffsetISOImage2Part3End) -
			(Adapter->psFlash2xCSInfo->OffsetISOImage2Part3Start);

		if (uiTotalDataToCopy < ISOLength) {
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "error as Dest ISO Section does not have enough section size");
			Status = STATUS_FAILURE;
			goto out;
		}

		uiTotalDataToCopy = ISOLength;

		CorruptISOSig(Adapter, ISO_IMAGE2);
		while (uiTotalDataToCopy) {
			if (uiTotalDataToCopy == Adapter->uiSectorSize) {
				/* Setting for write of first sector. First sector is assumed to be written in last */
				BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "Writing the signature sector");
				eISOReadPart = ISO_IMAGE1;
				uiReadOffsetWithinPart = 0;
				eISOWritePart = ISO_IMAGE2;
				uiWriteOffsetWithinPart = 0;
				IsThisHeaderSector = TRUE;
			} else {
				uiReadOffsetWithinPart = uiReadOffsetWithinPart + Adapter->uiSectorSize;
				uiWriteOffsetWithinPart = uiWriteOffsetWithinPart + Adapter->uiSectorSize;

				if ((eISOReadPart == ISO_IMAGE1) && (uiReadOffsetWithinPart == (Adapter->psFlash2xCSInfo->OffsetISOImage1Part1End - Adapter->psFlash2xCSInfo->OffsetISOImage1Part1Start))) {
					eISOReadPart = ISO_IMAGE1_PART2;
					uiReadOffsetWithinPart = 0;
				}

				if ((eISOReadPart == ISO_IMAGE1_PART2) && (uiReadOffsetWithinPart == (Adapter->psFlash2xCSInfo->OffsetISOImage1Part2End - Adapter->psFlash2xCSInfo->OffsetISOImage1Part2Start))) {
					eISOReadPart = ISO_IMAGE1_PART3;
					uiReadOffsetWithinPart = 0;
				}

				if ((eISOWritePart == ISO_IMAGE2) && (uiWriteOffsetWithinPart == (Adapter->psFlash2xCSInfo->OffsetISOImage2Part1End - Adapter->psFlash2xCSInfo->OffsetISOImage2Part1Start))) {
					eISOWritePart = ISO_IMAGE2_PART2;
					uiWriteOffsetWithinPart = 0;
				}

				if ((eISOWritePart == ISO_IMAGE2_PART2) && (uiWriteOffsetWithinPart == (Adapter->psFlash2xCSInfo->OffsetISOImage2Part2End - Adapter->psFlash2xCSInfo->OffsetISOImage2Part2Start))) {
					eISOWritePart = ISO_IMAGE2_PART3;
					uiWriteOffsetWithinPart = 0;
				}
			}

			Status = BcmFlash2xBulkRead(Adapter,
						(PUINT)Buff,
						eISOReadPart,
						uiReadOffsetWithinPart,
						Adapter->uiSectorSize);
			if (Status) {
				BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Read failed while copying ISO: Part: %x, OffsetWithinPart: %x\n", eISOReadPart, uiReadOffsetWithinPart);
				break;
			}

			if (IsThisHeaderSector == TRUE) {
				/* If this is header sector write 0xFFFFFFFF at the sig time and in last write sig */
				memcpy(SigBuff, Buff + sigOffset, sizeof(SigBuff));

				for (i = 0; i < MAX_RW_SIZE; i++)
					*(Buff + sigOffset + i) = 0xFF;
			}
			Adapter->bHeaderChangeAllowed = TRUE;
			Status = BcmFlash2xBulkWrite(Adapter,
						(PUINT)Buff,
						eISOWritePart,
						uiWriteOffsetWithinPart,
						Adapter->uiSectorSize,
						TRUE);
			if (Status) {
				BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Write failed while copying ISO: Part: %x, OffsetWithinPart: %x\n", eISOWritePart, uiWriteOffsetWithinPart);
				break;
			}

			Adapter->bHeaderChangeAllowed = false;
			if (IsThisHeaderSector == TRUE) {
				WriteToFlashWithoutSectorErase(Adapter,
							SigBuff,
							eISOWritePart,
							sigOffset,
							MAX_RW_SIZE);
				IsThisHeaderSector = false;
			}
			/* subtracting the written Data */
			uiTotalDataToCopy = uiTotalDataToCopy - Adapter->uiSectorSize;
		}
	}

	if (sCopySectStrut.SrcSection == ISO_IMAGE2 && sCopySectStrut.DstSection == ISO_IMAGE1) {
		eISOReadPart = ISO_IMAGE2;
		eISOWritePart = ISO_IMAGE1;
		uiReadOffsetWithinPart = 0;
		uiWriteOffsetWithinPart = 0;

		uiTotalDataToCopy = (Adapter->psFlash2xCSInfo->OffsetISOImage2Part1End) -
			(Adapter->psFlash2xCSInfo->OffsetISOImage2Part1Start) +
			(Adapter->psFlash2xCSInfo->OffsetISOImage2Part2End) -
			(Adapter->psFlash2xCSInfo->OffsetISOImage2Part2Start) +
			(Adapter->psFlash2xCSInfo->OffsetISOImage2Part3End) -
			(Adapter->psFlash2xCSInfo->OffsetISOImage2Part3Start);

		if (uiTotalDataToCopy < ISOLength) {
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "error as Source ISO Section does not have valid signature");
			Status = STATUS_FAILURE;
			goto out;
		}

		uiTotalDataToCopy = (Adapter->psFlash2xCSInfo->OffsetISOImage1Part1End) -
			(Adapter->psFlash2xCSInfo->OffsetISOImage1Part1Start) +
			(Adapter->psFlash2xCSInfo->OffsetISOImage1Part2End) -
			(Adapter->psFlash2xCSInfo->OffsetISOImage1Part2Start) +
			(Adapter->psFlash2xCSInfo->OffsetISOImage1Part3End) -
			(Adapter->psFlash2xCSInfo->OffsetISOImage1Part3Start);

		if (uiTotalDataToCopy < ISOLength) {
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "error as Dest ISO Section does not have enough section size");
			Status = STATUS_FAILURE;
			goto out;
		}

		uiTotalDataToCopy = ISOLength;

		CorruptISOSig(Adapter, ISO_IMAGE1);

		while (uiTotalDataToCopy) {
			if (uiTotalDataToCopy == Adapter->uiSectorSize) {
				/* Setting for write of first sector. First sector is assumed to be written in last */
				BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "Writing the signature sector");
				eISOReadPart = ISO_IMAGE2;
				uiReadOffsetWithinPart = 0;
				eISOWritePart = ISO_IMAGE1;
				uiWriteOffsetWithinPart = 0;
				IsThisHeaderSector = TRUE;
			} else {
				uiReadOffsetWithinPart = uiReadOffsetWithinPart + Adapter->uiSectorSize;
				uiWriteOffsetWithinPart = uiWriteOffsetWithinPart + Adapter->uiSectorSize;

				if ((eISOReadPart == ISO_IMAGE2) && (uiReadOffsetWithinPart == (Adapter->psFlash2xCSInfo->OffsetISOImage2Part1End - Adapter->psFlash2xCSInfo->OffsetISOImage2Part1Start))) {
					eISOReadPart = ISO_IMAGE2_PART2;
					uiReadOffsetWithinPart = 0;
				}

				if ((eISOReadPart == ISO_IMAGE2_PART2) && (uiReadOffsetWithinPart == (Adapter->psFlash2xCSInfo->OffsetISOImage2Part2End - Adapter->psFlash2xCSInfo->OffsetISOImage2Part2Start))) {
					eISOReadPart = ISO_IMAGE2_PART3;
					uiReadOffsetWithinPart = 0;
				}

				if ((eISOWritePart == ISO_IMAGE1) && (uiWriteOffsetWithinPart == (Adapter->psFlash2xCSInfo->OffsetISOImage1Part1End - Adapter->psFlash2xCSInfo->OffsetISOImage1Part1Start))) {
					eISOWritePart = ISO_IMAGE1_PART2;
					uiWriteOffsetWithinPart = 0;
				}

				if ((eISOWritePart == ISO_IMAGE1_PART2) && (uiWriteOffsetWithinPart == (Adapter->psFlash2xCSInfo->OffsetISOImage1Part2End - Adapter->psFlash2xCSInfo->OffsetISOImage1Part2Start))) {
					eISOWritePart = ISO_IMAGE1_PART3;
					uiWriteOffsetWithinPart = 0;
				}
			}

			Status = BcmFlash2xBulkRead(Adapter,
						(PUINT)Buff,
						eISOReadPart,
						uiReadOffsetWithinPart,
						Adapter->uiSectorSize);
			if (Status) {
				BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Read failed while copying ISO: Part: %x, OffsetWithinPart: %x\n", eISOReadPart, uiReadOffsetWithinPart);
				break;
			}

			if (IsThisHeaderSector == TRUE) {
				/* If this is header sector write 0xFFFFFFFF at the sig time and in last write sig */
				memcpy(SigBuff, Buff + sigOffset, sizeof(SigBuff));

				for (i = 0; i < MAX_RW_SIZE; i++)
					*(Buff + sigOffset + i) = 0xFF;
			}
			Adapter->bHeaderChangeAllowed = TRUE;
			Status = BcmFlash2xBulkWrite(Adapter,
						(PUINT)Buff,
						eISOWritePart,
						uiWriteOffsetWithinPart,
						Adapter->uiSectorSize,
						TRUE);
			if (Status) {
				BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Write failed while copying ISO: Part: %x, OffsetWithinPart: %x\n", eISOWritePart, uiWriteOffsetWithinPart);
				break;
			}

			Adapter->bHeaderChangeAllowed = false;
			if (IsThisHeaderSector == TRUE) {
				WriteToFlashWithoutSectorErase(Adapter,
							SigBuff,
							eISOWritePart,
							sigOffset,
							MAX_RW_SIZE);

				IsThisHeaderSector = false;
			}

			/* subtracting the written Data */
			uiTotalDataToCopy = uiTotalDataToCopy - Adapter->uiSectorSize;
		}
	}
out:
	kfree(Buff);

	return Status;
}

/*
 * BcmFlash2xCorruptSig : this API is used to corrupt the written sig in Bcm Header present in flash section.
 * It will corrupt the sig, if Section is writable, by making first bytes as zero.
 * @Adapater :- Bcm Driver Private Data Structure
 * @eFlash2xSectionVal :- Flash section val which has header
 *
 * Return Value :-
 *	Success :- If Section is present and writable, corrupt the sig and return STATUS_SUCCESS
 *	Failure :-Return negative error code
 */

int BcmFlash2xCorruptSig(struct bcm_mini_adapter *Adapter, enum bcm_flash2x_section_val eFlash2xSectionVal)
{
	int Status = STATUS_SUCCESS;

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Section Value :%x\n", eFlash2xSectionVal);

	if ((eFlash2xSectionVal == DSD0) || (eFlash2xSectionVal == DSD1) || (eFlash2xSectionVal == DSD2)) {
		Status = CorruptDSDSig(Adapter, eFlash2xSectionVal);
	} else if (eFlash2xSectionVal == ISO_IMAGE1 || eFlash2xSectionVal == ISO_IMAGE2) {
		Status = CorruptISOSig(Adapter, eFlash2xSectionVal);
	} else {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "Given Section <%d>does not have Header", eFlash2xSectionVal);
		return STATUS_SUCCESS;
	}
	return Status;
}

/*
 *BcmFlash2xWriteSig :-this API is used to Write the sig if requested Section has
 *					  header and  Write Permission.
 * @Adapater :- Bcm Driver Private Data Structure
 * @eFlashSectionVal :- Flash section val which has header
 *
 * Return Value :-
 *	Success :- If Section is present and writable write the sig and return STATUS_SUCCESS
 *	Failure :-Return negative error code
 */

int BcmFlash2xWriteSig(struct bcm_mini_adapter *Adapter, enum bcm_flash2x_section_val eFlashSectionVal)
{
	unsigned int uiSignature = 0;
	unsigned int uiOffset = 0;

	/* struct bcm_dsd_header dsdHeader = {0}; */
	if (Adapter->bSigCorrupted == false) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "Signature is not corrupted by driver, hence not restoring\n");
		return STATUS_SUCCESS;
	}

	if (Adapter->bAllDSDWriteAllow == false) {
		if (IsSectionWritable(Adapter, eFlashSectionVal) == false) {
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Section is not Writable...Hence can't Write signature");
			return SECTOR_IS_NOT_WRITABLE;
		}
	}

	if ((eFlashSectionVal == DSD0) || (eFlashSectionVal == DSD1) || (eFlashSectionVal == DSD2)) {
		uiSignature = htonl(DSD_IMAGE_MAGIC_NUMBER);
		uiOffset = Adapter->psFlash2xCSInfo->OffsetFromDSDStartForDSDHeader;

		uiOffset += FIELD_OFFSET_IN_HEADER(struct bcm_dsd_header *, DSDImageMagicNumber);

		if ((ReadDSDSignature(Adapter, eFlashSectionVal) & 0xFF000000) != CORRUPTED_PATTERN) {
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Corrupted Pattern is not there. Hence won't write sig");
			return STATUS_FAILURE;
		}
	} else if ((eFlashSectionVal == ISO_IMAGE1) || (eFlashSectionVal == ISO_IMAGE2)) {
		uiSignature = htonl(ISO_IMAGE_MAGIC_NUMBER);
		/* uiOffset = 0; */
		uiOffset = FIELD_OFFSET_IN_HEADER(struct bcm_iso_header *, ISOImageMagicNumber);
		if ((ReadISOSignature(Adapter, eFlashSectionVal) & 0xFF000000) != CORRUPTED_PATTERN) {
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Currupted Pattern is not there. Hence won't write sig");
			return STATUS_FAILURE;
		}
	} else {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "GIVEN SECTION< %d > IS NOT VALID FOR SIG WRITE...", eFlashSectionVal);
		return STATUS_FAILURE;
	}

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "Restoring the signature");

	Adapter->bHeaderChangeAllowed = TRUE;
	Adapter->bSigCorrupted = false;
	BcmFlash2xBulkWrite(Adapter, &uiSignature, eFlashSectionVal, uiOffset, SIGNATURE_SIZE, TRUE);
	Adapter->bHeaderChangeAllowed = false;

	return STATUS_SUCCESS;
}

/*
 * validateFlash2xReadWrite :- This API is used to validate the user request for Read/Write.
 *						      if requested Bytes goes beyond the Requested section, it reports error.
 * @Adapater :- Bcm Driver Private Data Structure
 * @psFlash2xReadWrite :-Flash2x Read/write structure pointer
 *
 * Return values:-Return TRUE is request is valid else false.
 */

int validateFlash2xReadWrite(struct bcm_mini_adapter *Adapter, struct bcm_flash2x_readwrite *psFlash2xReadWrite)
{
	unsigned int uiNumOfBytes = 0;
	unsigned int uiSectStartOffset = 0;
	unsigned int uiSectEndOffset = 0;

	uiNumOfBytes = psFlash2xReadWrite->numOfBytes;

	if (IsSectionExistInFlash(Adapter, psFlash2xReadWrite->Section) != TRUE) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Section<%x> does not exist in Flash", psFlash2xReadWrite->Section);
		return false;
	}
	uiSectStartOffset = BcmGetSectionValStartOffset(Adapter, psFlash2xReadWrite->Section);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "Start offset :%x ,section :%d\n", uiSectStartOffset, psFlash2xReadWrite->Section);
	if ((psFlash2xReadWrite->Section == ISO_IMAGE1) || (psFlash2xReadWrite->Section == ISO_IMAGE2)) {
		if (psFlash2xReadWrite->Section == ISO_IMAGE1) {
			uiSectEndOffset = BcmGetSectionValEndOffset(Adapter, ISO_IMAGE1) -
				BcmGetSectionValStartOffset(Adapter, ISO_IMAGE1) +
				BcmGetSectionValEndOffset(Adapter, ISO_IMAGE1_PART2) -
				BcmGetSectionValStartOffset(Adapter, ISO_IMAGE1_PART2) +
				BcmGetSectionValEndOffset(Adapter, ISO_IMAGE1_PART3) -
				BcmGetSectionValStartOffset(Adapter, ISO_IMAGE1_PART3);
		} else if (psFlash2xReadWrite->Section == ISO_IMAGE2) {
			uiSectEndOffset = BcmGetSectionValEndOffset(Adapter, ISO_IMAGE2) -
				BcmGetSectionValStartOffset(Adapter, ISO_IMAGE2) +
				BcmGetSectionValEndOffset(Adapter, ISO_IMAGE2_PART2) -
				BcmGetSectionValStartOffset(Adapter, ISO_IMAGE2_PART2) +
				BcmGetSectionValEndOffset(Adapter, ISO_IMAGE2_PART3) -
				BcmGetSectionValStartOffset(Adapter, ISO_IMAGE2_PART3);
		}

		/* since this uiSectEndoffset is the size of iso Image. hence for calculating the virtual endoffset
		 * it should be added in startoffset. so that check done in last of this function can be valued.
		 */
		uiSectEndOffset = uiSectStartOffset + uiSectEndOffset;

		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "Total size of the ISO Image :%x", uiSectEndOffset);
	} else
		uiSectEndOffset = BcmGetSectionValEndOffset(Adapter, psFlash2xReadWrite->Section);

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "End offset :%x\n", uiSectEndOffset);

	/* psFlash2xReadWrite->offset and uiNumOfBytes are user controlled and can lead to integer overflows */
	if (psFlash2xReadWrite->offset > uiSectEndOffset) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Invalid Request....");
		return false;
	}
	if (uiNumOfBytes > uiSectEndOffset) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Invalid Request....");
		return false;
	}
	/* Checking the boundary condition */
	if ((uiSectStartOffset + psFlash2xReadWrite->offset + uiNumOfBytes) <= uiSectEndOffset)
		return TRUE;
	else {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Invalid Request....");
		return false;
	}
}

/*
 * IsFlash2x :- check for Flash 2.x
 * Adapater :- Bcm Driver Private Data Structure
 *
 * Return value:-
 *	return TRUE if flah2.x of hgher version else return false.
 */

int IsFlash2x(struct bcm_mini_adapter *Adapter)
{
	if (Adapter->uiFlashLayoutMajorVersion >= FLASH_2X_MAJOR_NUMBER)
		return TRUE;
	else
		return false;
}

/*
 * GetFlashBaseAddr :- Calculate the Flash Base address
 * @Adapater :- Bcm Driver Private Data Structure
 *
 * Return Value:-
 *	Success :- Base Address of the Flash
 */

static int GetFlashBaseAddr(struct bcm_mini_adapter *Adapter)
{
	unsigned int uiBaseAddr = 0;

	if (Adapter->bDDRInitDone) {
		/*
		 * For All Valid Flash Versions... except 1.1, take the value from FlashBaseAddr
		 * In case of Raw Read... use the default value
		 */
		if (Adapter->uiFlashLayoutMajorVersion && (Adapter->bFlashRawRead == false) &&
			!((Adapter->uiFlashLayoutMajorVersion == 1) && (Adapter->uiFlashLayoutMinorVersion == 1)))
			uiBaseAddr = Adapter->uiFlashBaseAdd;
		else
			uiBaseAddr = FLASH_CONTIGIOUS_START_ADDR_AFTER_INIT;
	} else {
		/*
		 * For All Valid Flash Versions... except 1.1, take the value from FlashBaseAddr
		 * In case of Raw Read... use the default value
		 */
		if (Adapter->uiFlashLayoutMajorVersion && (Adapter->bFlashRawRead == false) &&
			!((Adapter->uiFlashLayoutMajorVersion == 1) && (Adapter->uiFlashLayoutMinorVersion == 1)))
			uiBaseAddr = Adapter->uiFlashBaseAdd | FLASH_CONTIGIOUS_START_ADDR_BEFORE_INIT;
		else
			uiBaseAddr = FLASH_CONTIGIOUS_START_ADDR_BEFORE_INIT;
	}

	return uiBaseAddr;
}

/*
 * BcmCopySection :- This API is used to copy the One section in another. Both section should
 *				    be contiuous and of same size. Hence this Will not be applicabe to copy ISO.
 *
 * @Adapater :- Bcm Driver Private Data Structure
 * @SrcSection :- Source section From where data has to be copied
 * @DstSection :- Destination section to which data has to be copied
 * @offset :- Offset from/to  where data has to be copied from one section to another.
 * @numOfBytes :- number of byes that has to be copyed from one section to another at given offset.
 *			     in case of numofBytes  equal zero complete section will be copied.
 * Return Values-
 *	Success : Return STATUS_SUCCESS
 *	Faillure :- return negative error code
 */

int BcmCopySection(struct bcm_mini_adapter *Adapter,
		enum bcm_flash2x_section_val SrcSection,
		enum bcm_flash2x_section_val DstSection,
		unsigned int offset,
		unsigned int numOfBytes)
{
	unsigned int BuffSize = 0;
	unsigned int BytesToBeCopied = 0;
	PUCHAR pBuff = NULL;
	int Status = STATUS_SUCCESS;

	if (SrcSection == DstSection) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Source and Destination should be different ...try again");
		return -EINVAL;
	}

	if ((SrcSection != DSD0) && (SrcSection != DSD1) && (SrcSection != DSD2)) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Source should be DSD subsection");
		return -EINVAL;
	}

	if ((DstSection != DSD0) && (DstSection != DSD1) && (DstSection != DSD2)) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Destination should be DSD subsection");
		return -EINVAL;
	}

	/* if offset zero means have to copy complete secton */
	if (numOfBytes == 0) {
		numOfBytes = BcmGetSectionValEndOffset(Adapter, SrcSection)
			- BcmGetSectionValStartOffset(Adapter, SrcSection);

		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "Section Size :0x%x", numOfBytes);
	}

	if ((offset + numOfBytes) > BcmGetSectionValEndOffset(Adapter, SrcSection)
		- BcmGetSectionValStartOffset(Adapter, SrcSection)) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, " Input parameters going beyond the section offS: %x numB: %x of Source Section\n",
				offset, numOfBytes);
		return -EINVAL;
	}

	if ((offset + numOfBytes) > BcmGetSectionValEndOffset(Adapter, DstSection)
		- BcmGetSectionValStartOffset(Adapter, DstSection)) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Input parameters going beyond the section offS: %x numB: %x of Destination Section\n",
				offset, numOfBytes);
		return -EINVAL;
	}

	if (numOfBytes > Adapter->uiSectorSize)
		BuffSize = Adapter->uiSectorSize;
	else
		BuffSize = numOfBytes;

	pBuff = kzalloc(BuffSize, GFP_KERNEL);
	if (!pBuff) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Memory allocation failed.. ");
		return -ENOMEM;
	}

	BytesToBeCopied = Adapter->uiSectorSize;
	if (offset % Adapter->uiSectorSize)
		BytesToBeCopied = Adapter->uiSectorSize - (offset % Adapter->uiSectorSize);
	if (BytesToBeCopied > numOfBytes)
		BytesToBeCopied = numOfBytes;

	Adapter->bHeaderChangeAllowed = TRUE;

	do {
		Status = BcmFlash2xBulkRead(Adapter, (PUINT)pBuff, SrcSection , offset, BytesToBeCopied);
		if (Status) {
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Read failed at offset :%d for NOB :%d", SrcSection, BytesToBeCopied);
			break;
		}
		Status = BcmFlash2xBulkWrite(Adapter, (PUINT)pBuff, DstSection, offset, BytesToBeCopied, false);
		if (Status) {
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Write failed at offset :%d for NOB :%d", DstSection, BytesToBeCopied);
			break;
		}
		offset = offset + BytesToBeCopied;
		numOfBytes = numOfBytes - BytesToBeCopied;
		if (numOfBytes) {
			if (numOfBytes > Adapter->uiSectorSize)
				BytesToBeCopied = Adapter->uiSectorSize;
			else
				BytesToBeCopied = numOfBytes;
		}
	} while (numOfBytes > 0);

	kfree(pBuff);
	Adapter->bHeaderChangeAllowed = false;

	return Status;
}

/*
 * SaveHeaderIfPresent :- This API is use to Protect the Header in case of Header Sector write
 * @Adapater :- Bcm Driver Private Data Structure
 * @pBuff :- Data buffer that has to be written in sector having the header map.
 * @uiOffset :- Flash offset that has to be written.
 *
 * Return value :-
 *	Success :- On success return STATUS_SUCCESS
 *	Faillure :- Return negative error code
 */

static int SaveHeaderIfPresent(struct bcm_mini_adapter *Adapter, PUCHAR pBuff, unsigned int uiOffset)
{
	unsigned int offsetToProtect = 0, HeaderSizeToProtect = 0;
	bool bHasHeader = false;
	PUCHAR pTempBuff = NULL;
	unsigned int uiSectAlignAddr = 0;
	unsigned int sig = 0;

	/* making the offset sector aligned */
	uiSectAlignAddr = uiOffset & ~(Adapter->uiSectorSize - 1);

	if ((uiSectAlignAddr == BcmGetSectionValEndOffset(Adapter, DSD2) - Adapter->uiSectorSize) ||
		(uiSectAlignAddr == BcmGetSectionValEndOffset(Adapter, DSD1) - Adapter->uiSectorSize) ||
		(uiSectAlignAddr == BcmGetSectionValEndOffset(Adapter, DSD0) - Adapter->uiSectorSize)) {
		/* offset from the sector boundary having the header map */
		offsetToProtect = Adapter->psFlash2xCSInfo->OffsetFromDSDStartForDSDHeader % Adapter->uiSectorSize;
		HeaderSizeToProtect = sizeof(struct bcm_dsd_header);
		bHasHeader = TRUE;
	}

	if (uiSectAlignAddr == BcmGetSectionValStartOffset(Adapter, ISO_IMAGE1) ||
		uiSectAlignAddr == BcmGetSectionValStartOffset(Adapter, ISO_IMAGE2)) {
		offsetToProtect = 0;
		HeaderSizeToProtect = sizeof(struct bcm_iso_header);
		bHasHeader = TRUE;
	}
	/* If Header is present overwrite passed buffer with this */
	if (bHasHeader && (Adapter->bHeaderChangeAllowed == false)) {
		pTempBuff = kzalloc(HeaderSizeToProtect, GFP_KERNEL);
		if (!pTempBuff) {
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Memory allocation failed");
			return -ENOMEM;
		}
		/* Read header */
		BeceemFlashBulkRead(Adapter, (PUINT)pTempBuff, (uiSectAlignAddr + offsetToProtect), HeaderSizeToProtect);
		BCM_DEBUG_PRINT_BUFFER(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, pTempBuff, HeaderSizeToProtect);
		/* Replace Buffer content with Header */
		memcpy(pBuff + offsetToProtect, pTempBuff, HeaderSizeToProtect);

		kfree(pTempBuff);
	}
	if (bHasHeader && Adapter->bSigCorrupted) {
		sig = *((PUINT)(pBuff + offsetToProtect + FIELD_OFFSET_IN_HEADER(struct bcm_dsd_header *, DSDImageMagicNumber)));
		sig = ntohl(sig);
		if ((sig & 0xFF000000) != CORRUPTED_PATTERN) {
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "Desired pattern is not at sig offset. Hence won't restore");
			Adapter->bSigCorrupted = false;
			return STATUS_SUCCESS;
		}
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, " Corrupted sig is :%X", sig);
		*((PUINT)(pBuff + offsetToProtect + FIELD_OFFSET_IN_HEADER(struct bcm_dsd_header *, DSDImageMagicNumber))) = htonl(DSD_IMAGE_MAGIC_NUMBER);
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "Restoring the signature in Header Write only");
		Adapter->bSigCorrupted = false;
	}

	return STATUS_SUCCESS;
}

/*
 * BcmDoChipSelect : This will selcet the appropriate chip for writing.
 * @Adapater :- Bcm Driver Private Data Structure
 *
 * OutPut:-
 *	Select the Appropriate chip and retrn status Success
 */
static int BcmDoChipSelect(struct bcm_mini_adapter *Adapter, unsigned int offset)
{
	unsigned int FlashConfig = 0;
	int ChipNum = 0;
	unsigned int GPIOConfig = 0;
	unsigned int PartNum = 0;

	ChipNum = offset / FLASH_PART_SIZE;

	/*
	 * Chip Select mapping to enable flash0.
	 * To select flash 0, we have to OR with (0<<12).
	 * ORing 0 will have no impact so not doing that part.
	 * In future if Chip select value changes from 0 to non zero,
	 * That needs be taken care with backward comaptibility. No worries for now.
	 */

	/*
	 * SelectedChip Variable is the selection that the host is 100% Sure the same as what the register will hold. This can be ONLY ensured
	 * if the Chip doesn't goes to low power mode while the flash operation is in progress (NVMRdmWrmLock is taken)
	 * Before every new Flash Write operation, we reset the variable. This is to ensure that after any wake-up from
	 * power down modes (Idle mode/shutdown mode), the values in the register will be different.
	 */

	if (Adapter->SelectedChip == ChipNum)
		return STATUS_SUCCESS;

	/* BCM_DEBUG_PRINT(Adapter,DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "Selected Chip :%x", ChipNum); */
	Adapter->SelectedChip = ChipNum;

	/* bit[13..12]  will select the appropriate chip */
	rdmalt(Adapter, FLASH_CONFIG_REG, &FlashConfig, 4);
	rdmalt(Adapter, FLASH_GPIO_CONFIG_REG, &GPIOConfig, 4);
	{
		switch (ChipNum) {
		case 0:
			PartNum = 0;
			break;
		case 1:
			PartNum = 3;
			GPIOConfig |= (0x4 << CHIP_SELECT_BIT12);
			break;
		case 2:
			PartNum = 1;
			GPIOConfig |= (0x1 << CHIP_SELECT_BIT12);
			break;
		case 3:
			PartNum = 2;
			GPIOConfig |= (0x2 << CHIP_SELECT_BIT12);
			break;
		}
	}
	/* In case the bits already written in the FLASH_CONFIG_REG is same as what the user desired,
	 * nothing to do... can return immediately.
	 * ASSUMPTION: FLASH_GPIO_CONFIG_REG will be in sync with FLASH_CONFIG_REG.
	 * Even if the chip goes to low power mode, it should wake with values in each register in sync with each other.
	 * These values are not written by host other than during CHIP_SELECT.
	 */
	if (PartNum == ((FlashConfig >> CHIP_SELECT_BIT12) & 0x3))
		return STATUS_SUCCESS;

	/* clearing the bit[13..12] */
	FlashConfig &= 0xFFFFCFFF;
	FlashConfig = (FlashConfig | (PartNum<<CHIP_SELECT_BIT12)); /* 00 */

	wrmalt(Adapter, FLASH_GPIO_CONFIG_REG, &GPIOConfig, 4);
	udelay(100);

	wrmalt(Adapter, FLASH_CONFIG_REG, &FlashConfig, 4);
	udelay(100);

	return STATUS_SUCCESS;
}

static int ReadDSDSignature(struct bcm_mini_adapter *Adapter, enum bcm_flash2x_section_val dsd)
{
	unsigned int uiDSDsig = 0;
	/* unsigned int sigoffsetInMap = 0;
	 * struct bcm_dsd_header dsdHeader = {0};
	 */

	/* sigoffsetInMap =(PUCHAR)&(dsdHeader.DSDImageMagicNumber) -(PUCHAR)&dsdHeader; */

	if (dsd != DSD0 && dsd != DSD1 && dsd != DSD2) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "passed section value is not for DSDs");
		return STATUS_FAILURE;
	}
	BcmFlash2xBulkRead(Adapter,
			&uiDSDsig,
			dsd,
			Adapter->psFlash2xCSInfo->OffsetFromDSDStartForDSDHeader + FIELD_OFFSET_IN_HEADER(struct bcm_dsd_header *, DSDImageMagicNumber),
			SIGNATURE_SIZE);

	uiDSDsig = ntohl(uiDSDsig);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "DSD SIG :%x", uiDSDsig);

	return uiDSDsig;
}

static int ReadDSDPriority(struct bcm_mini_adapter *Adapter, enum bcm_flash2x_section_val dsd)
{
	/* unsigned int priOffsetInMap = 0 ; */
	unsigned int uiDSDPri = STATUS_FAILURE;
	/* struct bcm_dsd_header dsdHeader = {0};
	 * priOffsetInMap = (PUCHAR)&(dsdHeader.DSDImagePriority) -(PUCHAR)&dsdHeader;
	 */
	if (IsSectionWritable(Adapter, dsd)) {
		if (ReadDSDSignature(Adapter, dsd) == DSD_IMAGE_MAGIC_NUMBER) {
			BcmFlash2xBulkRead(Adapter,
					&uiDSDPri,
					dsd,
					Adapter->psFlash2xCSInfo->OffsetFromDSDStartForDSDHeader + FIELD_OFFSET_IN_HEADER(struct bcm_dsd_header *, DSDImagePriority),
					4);

			uiDSDPri = ntohl(uiDSDPri);
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "DSD<%x> Priority :%x", dsd, uiDSDPri);
		}
	}

	return uiDSDPri;
}

static enum bcm_flash2x_section_val getHighestPriDSD(struct bcm_mini_adapter *Adapter)
{
	int DSDHighestPri = STATUS_FAILURE;
	int DsdPri = 0;
	enum bcm_flash2x_section_val HighestPriDSD = 0;

	if (IsSectionWritable(Adapter, DSD2)) {
		DSDHighestPri = ReadDSDPriority(Adapter, DSD2);
		HighestPriDSD = DSD2;
	}

	if (IsSectionWritable(Adapter, DSD1)) {
		DsdPri = ReadDSDPriority(Adapter, DSD1);
		if (DSDHighestPri  < DsdPri) {
			DSDHighestPri = DsdPri;
			HighestPriDSD = DSD1;
		}
	}

	if (IsSectionWritable(Adapter, DSD0)) {
		DsdPri = ReadDSDPriority(Adapter, DSD0);
		if (DSDHighestPri  < DsdPri) {
			DSDHighestPri = DsdPri;
			HighestPriDSD = DSD0;
		}
	}
	if (HighestPriDSD)
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "Highest DSD :%x , and its  Pri :%x", HighestPriDSD, DSDHighestPri);

	return  HighestPriDSD;
}

static int ReadISOSignature(struct bcm_mini_adapter *Adapter, enum bcm_flash2x_section_val iso)
{
	unsigned int uiISOsig = 0;
	/* unsigned int sigoffsetInMap = 0;
	 * struct bcm_iso_header ISOHeader = {0};
	 * sigoffsetInMap =(PUCHAR)&(ISOHeader.ISOImageMagicNumber) -(PUCHAR)&ISOHeader;
	 */
	if (iso != ISO_IMAGE1 && iso != ISO_IMAGE2) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "passed section value is not for ISOs");
		return STATUS_FAILURE;
	}
	BcmFlash2xBulkRead(Adapter,
			&uiISOsig,
			iso,
			0 + FIELD_OFFSET_IN_HEADER(struct bcm_iso_header *, ISOImageMagicNumber),
			SIGNATURE_SIZE);

	uiISOsig = ntohl(uiISOsig);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "ISO SIG :%x", uiISOsig);

	return uiISOsig;
}

static int ReadISOPriority(struct bcm_mini_adapter *Adapter, enum bcm_flash2x_section_val iso)
{
	unsigned int ISOPri = STATUS_FAILURE;
	if (IsSectionWritable(Adapter, iso)) {
		if (ReadISOSignature(Adapter, iso) == ISO_IMAGE_MAGIC_NUMBER) {
			BcmFlash2xBulkRead(Adapter,
					&ISOPri,
					iso,
					0 + FIELD_OFFSET_IN_HEADER(struct bcm_iso_header *, ISOImagePriority),
					4);

			ISOPri = ntohl(ISOPri);
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "ISO<%x> Priority :%x", iso, ISOPri);
		}
	}

	return ISOPri;
}

static enum bcm_flash2x_section_val getHighestPriISO(struct bcm_mini_adapter *Adapter)
{
	int ISOHighestPri = STATUS_FAILURE;
	int ISOPri = 0;
	enum bcm_flash2x_section_val HighestPriISO = NO_SECTION_VAL;

	if (IsSectionWritable(Adapter, ISO_IMAGE2)) {
		ISOHighestPri = ReadISOPriority(Adapter, ISO_IMAGE2);
		HighestPriISO = ISO_IMAGE2;
	}

	if (IsSectionWritable(Adapter, ISO_IMAGE1)) {
		ISOPri = ReadISOPriority(Adapter, ISO_IMAGE1);
		if (ISOHighestPri  < ISOPri) {
			ISOHighestPri = ISOPri;
			HighestPriISO = ISO_IMAGE1;
		}
	}
	if (HighestPriISO)
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "Highest ISO :%x and its Pri :%x", HighestPriISO, ISOHighestPri);

	return HighestPriISO;
}

static int WriteToFlashWithoutSectorErase(struct bcm_mini_adapter *Adapter,
				PUINT pBuff,
				enum bcm_flash2x_section_val eFlash2xSectionVal,
				unsigned int uiOffset,
				unsigned int uiNumBytes)
{
	#if !defined(BCM_SHM_INTERFACE) || defined(FLASH_DIRECT_ACCESS)
		unsigned int uiTemp = 0, value = 0;
		unsigned int i = 0;
		unsigned int uiPartOffset = 0;
	#endif
	unsigned int uiStartOffset = 0;
	/* Adding section start address */
	int Status = STATUS_SUCCESS;
	PUCHAR pcBuff = (PUCHAR)pBuff;

	if (uiNumBytes % Adapter->ulFlashWriteSize) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Writing without Sector Erase for non-FlashWriteSize number of bytes 0x%x\n", uiNumBytes);
		return STATUS_FAILURE;
	}

	uiStartOffset = BcmGetSectionValStartOffset(Adapter, eFlash2xSectionVal);

	if (IsSectionExistInVendorInfo(Adapter, eFlash2xSectionVal))
		return vendorextnWriteSectionWithoutErase(Adapter, pcBuff, eFlash2xSectionVal, uiOffset, uiNumBytes);

	uiOffset = uiOffset + uiStartOffset;

	#if defined(BCM_SHM_INTERFACE) && !defined(FLASH_DIRECT_ACCESS)
		Status = bcmflash_raw_writenoerase((uiOffset / FLASH_PART_SIZE), (uiOffset % FLASH_PART_SIZE), pcBuff, uiNumBytes);
	#else
		rdmalt(Adapter, 0x0f000C80, &uiTemp, sizeof(uiTemp));
		value = 0;
		wrmalt(Adapter, 0x0f000C80, &value, sizeof(value));

		Adapter->SelectedChip = RESET_CHIP_SELECT;
		BcmDoChipSelect(Adapter, uiOffset);
		uiPartOffset = (uiOffset & (FLASH_PART_SIZE - 1)) + GetFlashBaseAddr(Adapter);

		for (i = 0; i < uiNumBytes; i += Adapter->ulFlashWriteSize) {
			if (Adapter->ulFlashWriteSize == BYTE_WRITE_SUPPORT)
				Status = flashByteWrite(Adapter, uiPartOffset, pcBuff);
			else
				Status = flashWrite(Adapter, uiPartOffset, pcBuff);

			if (Status != STATUS_SUCCESS)
				break;

			pcBuff = pcBuff + Adapter->ulFlashWriteSize;
			uiPartOffset = uiPartOffset +  Adapter->ulFlashWriteSize;
		}
		wrmalt(Adapter, 0x0f000C80, &uiTemp, sizeof(uiTemp));
		Adapter->SelectedChip = RESET_CHIP_SELECT;
	#endif

	return Status;
}

bool IsSectionExistInFlash(struct bcm_mini_adapter *Adapter, enum bcm_flash2x_section_val section)
{
	bool SectionPresent = false;

	switch (section) {
	case ISO_IMAGE1:
		if ((Adapter->psFlash2xCSInfo->OffsetISOImage1Part1Start != UNINIT_PTR_IN_CS) &&
			(IsNonCDLessDevice(Adapter) == false))
			SectionPresent = TRUE;
		break;
	case ISO_IMAGE2:
		if ((Adapter->psFlash2xCSInfo->OffsetISOImage2Part1Start != UNINIT_PTR_IN_CS) &&
			(IsNonCDLessDevice(Adapter) == false))
			SectionPresent = TRUE;
		break;
	case DSD0:
		if (Adapter->psFlash2xCSInfo->OffsetFromZeroForDSDStart != UNINIT_PTR_IN_CS)
			SectionPresent = TRUE;
		break;
	case DSD1:
		if (Adapter->psFlash2xCSInfo->OffsetFromZeroForDSD1Start != UNINIT_PTR_IN_CS)
			SectionPresent = TRUE;
		break;
	case DSD2:
		if (Adapter->psFlash2xCSInfo->OffsetFromZeroForDSD2Start != UNINIT_PTR_IN_CS)
			SectionPresent = TRUE;
		break;
	case VSA0:
		if (Adapter->psFlash2xCSInfo->OffsetFromZeroForVSAStart != UNINIT_PTR_IN_CS)
			SectionPresent = TRUE;
		break;
	case VSA1:
		if (Adapter->psFlash2xCSInfo->OffsetFromZeroForVSA1Start != UNINIT_PTR_IN_CS)
			SectionPresent = TRUE;
		break;
	case VSA2:
		if (Adapter->psFlash2xCSInfo->OffsetFromZeroForVSA2Start != UNINIT_PTR_IN_CS)
			SectionPresent = TRUE;
		break;
	case SCSI:
		if (Adapter->psFlash2xCSInfo->OffsetFromZeroForScsiFirmware != UNINIT_PTR_IN_CS)
			SectionPresent = TRUE;
		break;
	case CONTROL_SECTION:
		if (Adapter->psFlash2xCSInfo->OffsetFromZeroForControlSectionStart != UNINIT_PTR_IN_CS)
			SectionPresent = TRUE;
		break;
	default:
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Section Does not exist in Flash 2.x");
		SectionPresent =  false;
	}

	return SectionPresent;
}

static int IsSectionWritable(struct bcm_mini_adapter *Adapter, enum bcm_flash2x_section_val Section)
{
	int offset = STATUS_FAILURE;
	int Status = false;

	if (IsSectionExistInFlash(Adapter, Section) == false) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Section <%d> does not exist", Section);
		return false;
	}

	offset = BcmGetSectionValStartOffset(Adapter, Section);
	if (offset == INVALID_OFFSET) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Section<%d> does not exist", Section);
		return false;
	}

	if (IsSectionExistInVendorInfo(Adapter, Section))
		return !(Adapter->psFlash2xVendorInfo->VendorSection[Section].AccessFlags & FLASH2X_SECTION_RO);

	Status = IsOffsetWritable(Adapter, offset);
	return Status;
}

static int CorruptDSDSig(struct bcm_mini_adapter *Adapter, enum bcm_flash2x_section_val eFlash2xSectionVal)
{
	PUCHAR pBuff = NULL;
	unsigned int sig = 0;
	unsigned int uiOffset = 0;
	unsigned int BlockStatus = 0;
	unsigned int uiSectAlignAddr = 0;

	Adapter->bSigCorrupted = false;
	if (Adapter->bAllDSDWriteAllow == false) {
		if (IsSectionWritable(Adapter, eFlash2xSectionVal) != TRUE) {
			BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Section is not Writable...Hence can't Corrupt signature");
			return SECTOR_IS_NOT_WRITABLE;
		}
	}

	pBuff = kzalloc(MAX_RW_SIZE, GFP_KERNEL);
	if (!pBuff) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Can't allocate memorey");
		return -ENOMEM;
	}

	uiOffset = Adapter->psFlash2xCSInfo->OffsetFromDSDStartForDSDHeader + sizeof(struct bcm_dsd_header);
	uiOffset -= MAX_RW_SIZE;

	BcmFlash2xBulkRead(Adapter, (PUINT)pBuff, eFlash2xSectionVal, uiOffset, MAX_RW_SIZE);

	sig = *((PUINT)(pBuff + 12));
	sig = ntohl(sig);
	BCM_DEBUG_PRINT_BUFFER(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, pBuff, MAX_RW_SIZE);
	/* Now corrupting the sig by corrupting 4th last Byte. */
	*(pBuff + 12) = 0;

	if (sig == DSD_IMAGE_MAGIC_NUMBER) {
		Adapter->bSigCorrupted = TRUE;
		if (Adapter->ulFlashWriteSize == BYTE_WRITE_SUPPORT) {
			uiSectAlignAddr = uiOffset & ~(Adapter->uiSectorSize - 1);
			BlockStatus = BcmFlashUnProtectBlock(Adapter, uiSectAlignAddr, Adapter->uiSectorSize);

			WriteToFlashWithoutSectorErase(Adapter, (PUINT)(pBuff + 12), eFlash2xSectionVal,
						(uiOffset + 12), BYTE_WRITE_SUPPORT);
			if (BlockStatus) {
				BcmRestoreBlockProtectStatus(Adapter, BlockStatus);
				BlockStatus = 0;
			}
		} else {
			WriteToFlashWithoutSectorErase(Adapter, (PUINT)pBuff, eFlash2xSectionVal,
						uiOffset, MAX_RW_SIZE);
		}
	} else {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "BCM Signature is not present in header");
		kfree(pBuff);

		return STATUS_FAILURE;
	}

	kfree(pBuff);
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "Corrupted the signature");

	return STATUS_SUCCESS;
}

static int CorruptISOSig(struct bcm_mini_adapter *Adapter, enum bcm_flash2x_section_val eFlash2xSectionVal)
{
	PUCHAR pBuff = NULL;
	unsigned int sig = 0;
	unsigned int uiOffset = 0;

	Adapter->bSigCorrupted = false;

	if (IsSectionWritable(Adapter, eFlash2xSectionVal) != TRUE) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Section is not Writable...Hence can't Corrupt signature");
		return SECTOR_IS_NOT_WRITABLE;
	}

	pBuff = kzalloc(MAX_RW_SIZE, GFP_KERNEL);
	if (!pBuff) {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "Can't allocate memorey");
		return -ENOMEM;
	}

	uiOffset = 0;

	BcmFlash2xBulkRead(Adapter, (PUINT)pBuff, eFlash2xSectionVal, uiOffset, MAX_RW_SIZE);

	sig = *((PUINT)pBuff);
	sig = ntohl(sig);

	/* corrupt signature */
	*pBuff = 0;

	if (sig == ISO_IMAGE_MAGIC_NUMBER) {
		Adapter->bSigCorrupted = TRUE;
		WriteToFlashWithoutSectorErase(Adapter, (PUINT)pBuff, eFlash2xSectionVal,
					uiOffset, Adapter->ulFlashWriteSize);
	} else {
		BCM_DEBUG_PRINT(Adapter, DBG_TYPE_PRINTK, 0, 0, "BCM Signature is not present in header");
		kfree(pBuff);

		return STATUS_FAILURE;
	}

	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, "Corrupted the signature");
	BCM_DEBUG_PRINT_BUFFER(Adapter, DBG_TYPE_OTHERS, NVM_RW, DBG_LVL_ALL, pBuff, MAX_RW_SIZE);

	kfree(pBuff);
	return STATUS_SUCCESS;
}

bool IsNonCDLessDevice(struct bcm_mini_adapter *Adapter)
{
	if (Adapter->psFlash2xCSInfo->IsCDLessDeviceBootSig == NON_CDLESS_DEVICE_BOOT_SIG)
		return TRUE;
	else
		return false;
}
