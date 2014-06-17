/***************************************************************************************
 *
 * Copyright (c) Beceem Communications Inc.
 *
 * Module Name:
 *	NVM.h
 *
 * Abstract:
 *	This file has the prototypes,preprocessors and definitions various NVM libraries.
 *
 *
 * Revision History:
 *	Who		When		What
 *	--------	--------	----------------------------------------------
 *	Name		Date		Created/reviewed/modified
 *
 * Notes:
 *
 ****************************************************************************************/

#ifndef _NVM_H_
#define _NVM_H_

struct bcm_flash_cs_info {
	u32 MagicNumber;
	/* let the magic number be 0xBECE-F1A5 - F1A5 for "flas-h" */
	u32 FlashLayoutVersion;
	u32 ISOImageVersion;
	u32 SCSIFirmwareVersion;
	u32 OffsetFromZeroForPart1ISOImage;
	u32 OffsetFromZeroForScsiFirmware;
	u32 SizeOfScsiFirmware;
	u32 OffsetFromZeroForPart2ISOImage;
	u32 OffsetFromZeroForCalibrationStart;
	u32 OffsetFromZeroForCalibrationEnd;
	u32 OffsetFromZeroForVSAStart;
	u32 OffsetFromZeroForVSAEnd;
	u32 OffsetFromZeroForControlSectionStart;
	u32 OffsetFromZeroForControlSectionData;
	u32 CDLessInactivityTimeout;
	u32 NewImageSignature;
	u32 FlashSectorSizeSig;
	u32 FlashSectorSize;
	u32 FlashWriteSupportSize;
	u32 TotalFlashSize;
	u32 FlashBaseAddr;
	u32 FlashPartMaxSize;
	u32 IsCDLessDeviceBootSig;
	/* MSC Timeout after reset to switch from MSC to NW Mode */
	u32 MassStorageTimeout;
};

#define FLASH2X_TOTAL_SIZE	(64 * 1024 * 1024)
#define DEFAULT_SECTOR_SIZE	(64 * 1024)

struct bcm_flash2x_cs_info {
	/* magic number as 0xBECE-F1A5 - F1A5 for "flas-h" */
	u32 MagicNumber;
	u32 FlashLayoutVersion;
	u32 ISOImageVersion;
	u32 SCSIFirmwareVersion;
	u32 OffsetFromZeroForPart1ISOImage;
	u32 OffsetFromZeroForScsiFirmware;
	u32 SizeOfScsiFirmware;
	u32 OffsetFromZeroForPart2ISOImage;
	u32 OffsetFromZeroForDSDStart;
	u32 OffsetFromZeroForDSDEnd;
	u32 OffsetFromZeroForVSAStart;
	u32 OffsetFromZeroForVSAEnd;
	u32 OffsetFromZeroForControlSectionStart;
	u32 OffsetFromZeroForControlSectionData;
	/* NO Data Activity timeout to switch from MSC to NW Mode */
	u32 CDLessInactivityTimeout;
	u32 NewImageSignature;
	u32 FlashSectorSizeSig;
	u32 FlashSectorSize;
	u32 FlashWriteSupportSize;
	u32 TotalFlashSize;
	u32 FlashBaseAddr;
	u32 FlashPartMaxSize;
	u32 IsCDLessDeviceBootSig;
	/* MSC Timeout after reset to switch from MSC to NW Mode */
	u32 MassStorageTimeout;
	/* Flash Map 2.0 Field */
	u32 OffsetISOImage1Part1Start;
	u32 OffsetISOImage1Part1End;
	u32 OffsetISOImage1Part2Start;
	u32 OffsetISOImage1Part2End;
	u32 OffsetISOImage1Part3Start;
	u32 OffsetISOImage1Part3End;
	u32 OffsetISOImage2Part1Start;
	u32 OffsetISOImage2Part1End;
	u32 OffsetISOImage2Part2Start;
	u32 OffsetISOImage2Part2End;
	u32 OffsetISOImage2Part3Start;
	u32 OffsetISOImage2Part3End;
	/* DSD Header offset from start of DSD */
	u32 OffsetFromDSDStartForDSDHeader;
	u32 OffsetFromZeroForDSD1Start;
	u32 OffsetFromZeroForDSD1End;
	u32 OffsetFromZeroForDSD2Start;
	u32 OffsetFromZeroForDSD2End;
	u32 OffsetFromZeroForVSA1Start;
	u32 OffsetFromZeroForVSA1End;
	u32 OffsetFromZeroForVSA2Start;
	u32 OffsetFromZeroForVSA2End;
	/*
	 * ACCESS_BITS_PER_SECTOR	2
	 * ACCESS_RW			0
	 * ACCESS_RO			1
	 * ACCESS_RESVD			2
	 * ACCESS_RESVD			3
	 */
	u32 SectorAccessBitMap[FLASH2X_TOTAL_SIZE / (DEFAULT_SECTOR_SIZE * 16)];
	/* All expansions to the control data structure should add here */
};

struct bcm_vendor_section_info {
	u32 OffsetFromZeroForSectionStart;
	u32 OffsetFromZeroForSectionEnd;
	u32 AccessFlags;
	u32 Reserved[16];
};

struct bcm_flash2x_vendor_info {
	struct bcm_vendor_section_info VendorSection[TOTAL_SECTIONS];
	u32 Reserved[16];
};

struct bcm_dsd_header {
	u32 DSDImageSize;
	u32 DSDImageCRC;
	u32 DSDImagePriority;
	/* We should not consider right now. Reading reserve is worthless. */
	u32 Reserved[252]; /* Resvd for DSD Header */
	u32 DSDImageMagicNumber;
};

struct bcm_iso_header {
	u32 ISOImageMagicNumber;
	u32 ISOImageSize;
	u32 ISOImageCRC;
	u32 ISOImagePriority;
	/* We should not consider right now. Reading reserve is worthless. */
	u32 Reserved[60]; /* Resvd for ISO Header extension */
};

#define EEPROM_BEGIN_CIS	(0)
#define EEPROM_BEGIN_NON_CIS	(0x200)
#define EEPROM_END		(0x2000)
#define INIT_PARAMS_SIGNATURE	(0x95a7a597)
#define MAX_INIT_PARAMS_LENGTH	(2048)
#define MAC_ADDRESS_OFFSET	0x200

#define INIT_PARAMS_1_SIGNATURE_ADDRESS		EEPROM_BEGIN_NON_CIS
#define INIT_PARAMS_1_DATA_ADDRESS		(INIT_PARAMS_1_SIGNATURE_ADDRESS+16)
#define INIT_PARAMS_1_MACADDRESS_ADDRESS	(MAC_ADDRESS_OFFSET)
#define INIT_PARAMS_1_LENGTH_ADDRESS		(INIT_PARAMS_1_SIGNATURE_ADDRESS+4)

#define INIT_PARAMS_2_SIGNATURE_ADDRESS		(EEPROM_BEGIN_NON_CIS + 2048 + 16)
#define INIT_PARAMS_2_DATA_ADDRESS		(INIT_PARAMS_2_SIGNATURE_ADDRESS + 16)
#define INIT_PARAMS_2_MACADDRESS_ADDRESS	(INIT_PARAMS_2_SIGNATURE_ADDRESS + 8)
#define INIT_PARAMS_2_LENGTH_ADDRESS		(INIT_PARAMS_2_SIGNATURE_ADDRESS + 4)

#define EEPROM_SPI_DEV_CONFIG_REG		0x0F003000
#define EEPROM_SPI_Q_STATUS1_REG		0x0F003004
#define EEPROM_SPI_Q_STATUS1_MASK_REG		0x0F00300C

#define EEPROM_SPI_Q_STATUS_REG			0x0F003008
#define EEPROM_CMDQ_SPI_REG			0x0F003018
#define EEPROM_WRITE_DATAQ_REG			0x0F00301C
#define EEPROM_READ_DATAQ_REG			0x0F003020
#define SPI_FLUSH_REG				0x0F00304C

#define EEPROM_WRITE_ENABLE			0x06000000
#define EEPROM_READ_STATUS_REGISTER		0x05000000
#define EEPROM_16_BYTE_PAGE_WRITE		0xFA000000
#define EEPROM_WRITE_QUEUE_EMPTY		0x00001000
#define EEPROM_WRITE_QUEUE_AVAIL		0x00002000
#define EEPROM_WRITE_QUEUE_FULL			0x00004000
#define EEPROM_16_BYTE_PAGE_READ		0xFB000000
#define EEPROM_4_BYTE_PAGE_READ			0x3B000000

#define EEPROM_CMD_QUEUE_FLUSH			0x00000001
#define EEPROM_WRITE_QUEUE_FLUSH		0x00000002
#define EEPROM_READ_QUEUE_FLUSH			0x00000004
#define EEPROM_ETH_QUEUE_FLUSH			0x00000008
#define EEPROM_ALL_QUEUE_FLUSH			0x0000000f
#define EEPROM_READ_ENABLE			0x06000000
#define EEPROM_16_BYTE_PAGE_WRITE		0xFA000000
#define EEPROM_READ_DATA_FULL			0x00000010
#define EEPROM_READ_DATA_AVAIL			0x00000020
#define EEPROM_READ_QUEUE_EMPTY			0x00000002
#define EEPROM_CMD_QUEUE_EMPTY			0x00000100
#define EEPROM_CMD_QUEUE_AVAIL			0x00000200
#define EEPROM_CMD_QUEUE_FULL			0x00000400

/* Most EEPROM status register bit 0 indicates if the EEPROM is busy
 * with a write if set 1. See the details of the EEPROM Status Register
 * in the EEPROM data sheet.
 */
#define EEPROM_STATUS_REG_WRITE_BUSY		0x00000001

/* We will have 1 mSec for every RETRIES_PER_DELAY count and have a max attempts of MAX_EEPROM_RETRIES
 * This will give us 80 mSec minimum of delay = 80mSecs
 */
#define MAX_EEPROM_RETRIES			80
#define RETRIES_PER_DELAY			64
#define MAX_RW_SIZE				0x10
#define MAX_READ_SIZE				0x10
#define MAX_SECTOR_SIZE				(512 * 1024)
#define MIN_SECTOR_SIZE				(1024)
#define FLASH_SECTOR_SIZE_OFFSET		0xEFFFC
#define FLASH_SECTOR_SIZE_SIG_OFFSET		0xEFFF8
#define FLASH_SECTOR_SIZE_SIG			0xCAFEBABE
#define FLASH_CS_INFO_START_ADDR		0xFF0000
#define FLASH_CONTROL_STRUCT_SIGNATURE		0xBECEF1A5
#define SCSI_FIRMWARE_MAJOR_VERSION		0x1
#define SCSI_FIRMWARE_MINOR_VERSION		0x5
#define BYTE_WRITE_SUPPORT			0x1
#define FLASH_AUTO_INIT_BASE_ADDR		0xF00000
#define FLASH_CONTIGIOUS_START_ADDR_AFTER_INIT	0x1C000000
#define FLASH_CONTIGIOUS_START_ADDR_BEFORE_INIT	0x1F000000
#define FLASH_CONTIGIOUS_START_ADDR_BCS350	0x08000000
#define FLASH_CONTIGIOUS_END_ADDR_BCS350	0x08FFFFFF
#define FLASH_SIZE_ADDR				0xFFFFEC
#define FLASH_SPI_CMDQ_REG			0xAF003040
#define FLASH_SPI_WRITEQ_REG			0xAF003044
#define FLASH_SPI_READQ_REG			0xAF003048
#define FLASH_CONFIG_REG			0xAF003050
#define FLASH_GPIO_CONFIG_REG			0xAF000030
#define FLASH_CMD_WRITE_ENABLE			0x06
#define FLASH_CMD_READ_ENABLE			0x03
#define FLASH_CMD_RESET_WRITE_ENABLE		0x04
#define FLASH_CMD_STATUS_REG_READ		0x05
#define FLASH_CMD_STATUS_REG_WRITE		0x01
#define FLASH_CMD_READ_ID			0x9F
#define PAD_SELECT_REGISTER			0xAF000410
#define FLASH_PART_SST25VF080B			0xBF258E
#define EEPROM_CAL_DATA_INTERNAL_LOC		0xbFB00008
#define EEPROM_CALPARAM_START			0x200
#define EEPROM_SIZE_OFFSET			524

/* As Read/Write time vaires from 1.5 to 3.0 ms.
 * so After Ignoring the rdm/wrm time(that is dependent on many factor like interface etc.),
 * here time calculated meets the worst case delay, 3.0 ms
 */
#define MAX_FLASH_RETRIES		4
#define FLASH_PER_RETRIES_DELAY		16
#define EEPROM_MAX_CAL_AREA_SIZE	0xF0000
#define BECM				ntohl(0x4245434d)
#define FLASH_2X_MAJOR_NUMBER		0x2
#define DSD_IMAGE_MAGIC_NUMBER		0xBECE0D5D
#define ISO_IMAGE_MAGIC_NUMBER		0xBECE0150
#define NON_CDLESS_DEVICE_BOOT_SIG	0xBECEB007

#define MINOR_VERSION(x) ((x >> 16) & 0xFFFF)
#define MAJOR_VERSION(x) (x & 0xFFFF)

#define CORRUPTED_PATTERN		0x0
#define UNINIT_PTR_IN_CS		0xBBBBDDDD
#define VENDOR_PTR_IN_CS		0xAAAACCCC
#define FLASH2X_SECTION_PRESENT		(1 << 0)
#define FLASH2X_SECTION_VALID		(1 << 1)
#define FLASH2X_SECTION_RO		(1 << 2)
#define FLASH2X_SECTION_ACT		(1 << 3)
#define SECTOR_IS_NOT_WRITABLE		STATUS_FAILURE
#define INVALID_OFFSET			STATUS_FAILURE
#define INVALID_SECTION			STATUS_FAILURE
#define SECTOR_1K			1024
#define SECTOR_64K			(64 * SECTOR_1K)
#define SECTOR_128K			(2 * SECTOR_64K)
#define SECTOR_256k			(2 * SECTOR_128K)
#define SECTOR_512K			(2 * SECTOR_256k)
#define FLASH_PART_SIZE			(16 * 1024 * 1024)
#define RESET_CHIP_SELECT		-1
#define CHIP_SELECT_BIT12		12
#define SECTOR_READWRITE_PERMISSION	0
#define SECTOR_READONLY			1
#define SIGNATURE_SIZE			4
#define DEFAULT_BUFF_SIZE		0x10000

#define FIELD_OFFSET_IN_HEADER(HeaderPointer, Field) ((u8 *)&((HeaderPointer)(NULL))->Field - (u8 *)(NULL))

#endif

