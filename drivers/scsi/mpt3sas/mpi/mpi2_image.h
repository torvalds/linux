/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2016-2020 Broadcom Limited. All rights reserved.
 *
 *          Name: mpi2_image.h
 * Description: Contains definitions for firmware and other component images
 * Creation Date: 04/02/2018
 *       Version: 02.06.04
 *
 *
 * Version History
 * ---------------
 *
 * Date      Version   Description
 * --------  --------  ------------------------------------------------------
 * 08-01-18  02.06.00  Initial version for MPI 2.6.5.
 * 08-14-18  02.06.01  Corrected define for MPI26_IMAGE_HEADER_SIGNATURE0_MPI26
 * 08-28-18  02.06.02  Added MPI2_EXT_IMAGE_TYPE_RDE
 * 09-07-18  02.06.03  Added MPI26_EVENT_PCIE_TOPO_PI_16_LANES
 * 12-17-18  02.06.04  Addd MPI2_EXT_IMAGE_TYPE_PBLP
 *			Shorten some defines to be compatible with DOS
 */
#ifndef MPI2_IMAGE_H
#define MPI2_IMAGE_H


/*FW Image Header */
typedef struct _MPI2_FW_IMAGE_HEADER {
	U32 Signature;		/*0x00 */
	U32 Signature0;		/*0x04 */
	U32 Signature1;		/*0x08 */
	U32 Signature2;		/*0x0C */
	MPI2_VERSION_UNION MPIVersion;	/*0x10 */
	MPI2_VERSION_UNION FWVersion;	/*0x14 */
	MPI2_VERSION_UNION NVDATAVersion;	/*0x18 */
	MPI2_VERSION_UNION PackageVersion;	/*0x1C */
	U16 VendorID;		/*0x20 */
	U16 ProductID;		/*0x22 */
	U16 ProtocolFlags;	/*0x24 */
	U16 Reserved26;		/*0x26 */
	U32 IOCCapabilities;	/*0x28 */
	U32 ImageSize;		/*0x2C */
	U32 NextImageHeaderOffset;	/*0x30 */
	U32 Checksum;		/*0x34 */
	U32 Reserved38;		/*0x38 */
	U32 Reserved3C;		/*0x3C */
	U32 Reserved40;		/*0x40 */
	U32 Reserved44;		/*0x44 */
	U32 Reserved48;		/*0x48 */
	U32 Reserved4C;		/*0x4C */
	U32 Reserved50;		/*0x50 */
	U32 Reserved54;		/*0x54 */
	U32 Reserved58;		/*0x58 */
	U32 Reserved5C;		/*0x5C */
	U32 BootFlags;		/*0x60 */
	U32 FirmwareVersionNameWhat;	/*0x64 */
	U8 FirmwareVersionName[32];	/*0x68 */
	U32 VendorNameWhat;	/*0x88 */
	U8 VendorName[32];	/*0x8C */
	U32 PackageNameWhat;	/*0x88 */
	U8 PackageName[32];	/*0x8C */
	U32 ReservedD0;		/*0xD0 */
	U32 ReservedD4;		/*0xD4 */
	U32 ReservedD8;		/*0xD8 */
	U32 ReservedDC;		/*0xDC */
	U32 ReservedE0;		/*0xE0 */
	U32 ReservedE4;		/*0xE4 */
	U32 ReservedE8;		/*0xE8 */
	U32 ReservedEC;		/*0xEC */
	U32 ReservedF0;		/*0xF0 */
	U32 ReservedF4;		/*0xF4 */
	U32 ReservedF8;		/*0xF8 */
	U32 ReservedFC;		/*0xFC */
} MPI2_FW_IMAGE_HEADER, *PTR_MPI2_FW_IMAGE_HEADER,
	Mpi2FWImageHeader_t, *pMpi2FWImageHeader_t;

/*Signature field */
#define MPI2_FW_HEADER_SIGNATURE_OFFSET         (0x00)
#define MPI2_FW_HEADER_SIGNATURE_MASK           (0xFF000000)
#define MPI2_FW_HEADER_SIGNATURE                (0xEA000000)
#define MPI26_FW_HEADER_SIGNATURE               (0xEB000000)

/*Signature0 field */
#define MPI2_FW_HEADER_SIGNATURE0_OFFSET        (0x04)
#define MPI2_FW_HEADER_SIGNATURE0               (0x5AFAA55A)
/*Last byte is defined by architecture */
#define MPI26_FW_HEADER_SIGNATURE0_BASE         (0x5AEAA500)
#define MPI26_FW_HEADER_SIGNATURE0_ARC_0        (0x5A)
#define MPI26_FW_HEADER_SIGNATURE0_ARC_1        (0x00)
#define MPI26_FW_HEADER_SIGNATURE0_ARC_2        (0x01)
/*legacy (0x5AEAA55A) */
#define MPI26_FW_HEADER_SIGNATURE0_ARC_3        (0x02)
#define MPI26_FW_HEADER_SIGNATURE0 \
	(MPI26_FW_HEADER_SIGNATURE0_BASE+MPI26_FW_HEADER_SIGNATURE0_ARC_0)
#define MPI26_FW_HEADER_SIGNATURE0_3516 \
	(MPI26_FW_HEADER_SIGNATURE0_BASE+MPI26_FW_HEADER_SIGNATURE0_ARC_1)
#define MPI26_FW_HEADER_SIGNATURE0_4008 \
	(MPI26_FW_HEADER_SIGNATURE0_BASE+MPI26_FW_HEADER_SIGNATURE0_ARC_3)

/*Signature1 field */
#define MPI2_FW_HEADER_SIGNATURE1_OFFSET        (0x08)
#define MPI2_FW_HEADER_SIGNATURE1               (0xA55AFAA5)
#define MPI26_FW_HEADER_SIGNATURE1              (0xA55AEAA5)

/*Signature2 field */
#define MPI2_FW_HEADER_SIGNATURE2_OFFSET        (0x0C)
#define MPI2_FW_HEADER_SIGNATURE2               (0x5AA55AFA)
#define MPI26_FW_HEADER_SIGNATURE2              (0x5AA55AEA)

/*defines for using the ProductID field */
#define MPI2_FW_HEADER_PID_TYPE_MASK            (0xF000)
#define MPI2_FW_HEADER_PID_TYPE_SAS             (0x2000)

#define MPI2_FW_HEADER_PID_PROD_MASK                    (0x0F00)
#define MPI2_FW_HEADER_PID_PROD_A                       (0x0000)
#define MPI2_FW_HEADER_PID_PROD_TARGET_INITIATOR_SCSI   (0x0200)
#define MPI2_FW_HEADER_PID_PROD_IR_SCSI                 (0x0700)

#define MPI2_FW_HEADER_PID_FAMILY_MASK          (0x00FF)
/*SAS ProductID Family bits */
#define MPI2_FW_HEADER_PID_FAMILY_2108_SAS      (0x0013)
#define MPI2_FW_HEADER_PID_FAMILY_2208_SAS      (0x0014)
#define MPI25_FW_HEADER_PID_FAMILY_3108_SAS     (0x0021)
#define MPI26_FW_HEADER_PID_FAMILY_3324_SAS     (0x0028)
#define MPI26_FW_HEADER_PID_FAMILY_3516_SAS     (0x0031)

/*use MPI2_IOCFACTS_PROTOCOL_ defines for ProtocolFlags field */

/*use MPI2_IOCFACTS_CAPABILITY_ defines for IOCCapabilities field */

#define MPI2_FW_HEADER_IMAGESIZE_OFFSET         (0x2C)
#define MPI2_FW_HEADER_NEXTIMAGE_OFFSET         (0x30)

#define MPI26_FW_HEADER_BOOTFLAGS_OFFSET          (0x60)
#define MPI2_FW_HEADER_BOOTFLAGS_ISSI32M_FLAG     (0x00000001)
#define MPI2_FW_HEADER_BOOTFLAGS_W25Q256JW_FLAG   (0x00000002)
/*This image has a auto-discovery version of SPI */
#define MPI2_FW_HEADER_BOOTFLAGS_AUTO_SPI_FLAG    (0x00000004)


#define MPI2_FW_HEADER_VERNMHWAT_OFFSET         (0x64)

#define MPI2_FW_HEADER_WHAT_SIGNATURE           (0x29232840)

#define MPI2_FW_HEADER_SIZE                     (0x100)


/****************************************************************************
 *              Component Image Format and related defines                  *
 ****************************************************************************/

/*Maximum number of Hash Exclusion entries in a Component Image Header */
#define MPI26_COMP_IMG_HDR_NUM_HASH_EXCL        (4)

/*Hash Exclusion Format */
typedef struct _MPI26_HASH_EXCLUSION_FORMAT {
	U32 Offset;        /*0x00 */
	U32 Size;          /*0x04 */
} MPI26_HASH_EXCLUSION_FORMAT,
	*PTR_MPI26_HASH_EXCLUSION_FORMAT,
	Mpi26HashSxclusionFormat_t,
	*pMpi26HashExclusionFormat_t;

/*FW Image Header */
typedef struct _MPI26_COMPONENT_IMAGE_HEADER {
	U32 Signature0;					/*0x00 */
	U32 LoadAddress;				/*0x04 */
	U32 DataSize;					/*0x08 */
	U32 StartAddress;				/*0x0C */
	U32 Signature1;					/*0x10 */
	U32 FlashOffset;				/*0x14 */
	U32 FlashSize;					/*0x18 */
	U32 VersionStringOffset;			/*0x1C */
	U32 BuildDateStringOffset;			/*0x20 */
	U32 BuildTimeStringOffset;			/*0x24 */
	U32 EnvironmentVariableOffset;			/*0x28 */
	U32 ApplicationSpecific;			/*0x2C */
	U32 Signature2;					/*0x30 */
	U32 HeaderSize;					/*0x34 */
	U32 Crc;					/*0x38 */
	U8 NotFlashImage;				/*0x3C */
	U8 Compressed;					/*0x3D */
	U16 Reserved3E;					/*0x3E */
	U32 SecondaryFlashOffset;			/*0x40 */
	U32 Reserved44;					/*0x44 */
	U32 Reserved48;					/*0x48 */
	MPI2_VERSION_UNION RMCInterfaceVersion;		/*0x4C */
	MPI2_VERSION_UNION Reserved50;			/*0x50 */
	MPI2_VERSION_UNION FWVersion;			/*0x54 */
	MPI2_VERSION_UNION NvdataVersion;		/*0x58 */
	MPI26_HASH_EXCLUSION_FORMAT
	HashExclusion[MPI26_COMP_IMG_HDR_NUM_HASH_EXCL];/*0x5C */
	U32 NextImageHeaderOffset;			/*0x7C */
	U32 Reserved80[32];				/*0x80 -- 0xFC */
} MPI26_COMPONENT_IMAGE_HEADER,
	*PTR_MPI26_COMPONENT_IMAGE_HEADER,
	Mpi26ComponentImageHeader_t,
	*pMpi26ComponentImageHeader_t;


/**** Definitions for Signature0 field ****/
#define MPI26_IMAGE_HEADER_SIGNATURE0_MPI26                     (0xEB000042)

/**** Definitions for Signature1 field ****/
#define MPI26_IMAGE_HEADER_SIG1_APPLICATION              (0x20505041)
#define MPI26_IMAGE_HEADER_SIG1_CBB                      (0x20424243)
#define MPI26_IMAGE_HEADER_SIG1_MFG                      (0x2047464D)
#define MPI26_IMAGE_HEADER_SIG1_BIOS                     (0x534F4942)
#define MPI26_IMAGE_HEADER_SIG1_HIIM                     (0x4D494948)
#define MPI26_IMAGE_HEADER_SIG1_HIIA                     (0x41494948)
#define MPI26_IMAGE_HEADER_SIG1_CPLD                     (0x444C5043)
#define MPI26_IMAGE_HEADER_SIG1_SPD                      (0x20445053)
#define MPI26_IMAGE_HEADER_SIG1_NVDATA                   (0x5444564E)
#define MPI26_IMAGE_HEADER_SIG1_GAS_GAUGE                (0x20534147)
#define MPI26_IMAGE_HEADER_SIG1_PBLP                     (0x504C4250)

/**** Definitions for Signature2 field ****/
#define MPI26_IMAGE_HEADER_SIGNATURE2_VALUE                    (0x50584546)

/**** Offsets for Image Header Fields ****/
#define MPI26_IMAGE_HEADER_SIGNATURE0_OFFSET                   (0x00)
#define MPI26_IMAGE_HEADER_LOAD_ADDRESS_OFFSET                 (0x04)
#define MPI26_IMAGE_HEADER_DATA_SIZE_OFFSET                    (0x08)
#define MPI26_IMAGE_HEADER_START_ADDRESS_OFFSET                (0x0C)
#define MPI26_IMAGE_HEADER_SIGNATURE1_OFFSET                   (0x10)
#define MPI26_IMAGE_HEADER_FLASH_OFFSET_OFFSET                 (0x14)
#define MPI26_IMAGE_HEADER_FLASH_SIZE_OFFSET                   (0x18)
#define MPI26_IMAGE_HEADER_VERSION_STRING_OFFSET_OFFSET        (0x1C)
#define MPI26_IMAGE_HEADER_BUILD_DATE_STRING_OFFSET_OFFSET     (0x20)
#define MPI26_IMAGE_HEADER_BUILD_TIME_OFFSET_OFFSET            (0x24)
#define MPI26_IMAGE_HEADER_ENVIROMENT_VAR_OFFSET_OFFSET        (0x28)
#define MPI26_IMAGE_HEADER_APPLICATION_SPECIFIC_OFFSET         (0x2C)
#define MPI26_IMAGE_HEADER_SIGNATURE2_OFFSET                   (0x30)
#define MPI26_IMAGE_HEADER_HEADER_SIZE_OFFSET                  (0x34)
#define MPI26_IMAGE_HEADER_CRC_OFFSET                          (0x38)
#define MPI26_IMAGE_HEADER_NOT_FLASH_IMAGE_OFFSET              (0x3C)
#define MPI26_IMAGE_HEADER_COMPRESSED_OFFSET                   (0x3D)
#define MPI26_IMAGE_HEADER_SECONDARY_FLASH_OFFSET_OFFSET       (0x40)
#define MPI26_IMAGE_HEADER_RMC_INTERFACE_VER_OFFSET            (0x4C)
#define MPI26_IMAGE_HEADER_COMPONENT_IMAGE_VER_OFFSET          (0x54)
#define MPI26_IMAGE_HEADER_HASH_EXCLUSION_OFFSET               (0x5C)
#define MPI26_IMAGE_HEADER_NEXT_IMAGE_HEADER_OFFSET_OFFSET     (0x7C)


#define MPI26_IMAGE_HEADER_SIZE                                (0x100)


/*Extended Image Header */
typedef struct _MPI2_EXT_IMAGE_HEADER {
	U8 ImageType;		/*0x00 */
	U8 Reserved1;		/*0x01 */
	U16 Reserved2;		/*0x02 */
	U32 Checksum;		/*0x04 */
	U32 ImageSize;		/*0x08 */
	U32 NextImageHeaderOffset;	/*0x0C */
	U32 PackageVersion;	/*0x10 */
	U32 Reserved3;		/*0x14 */
	U32 Reserved4;		/*0x18 */
	U32 Reserved5;		/*0x1C */
	U8 IdentifyString[32];	/*0x20 */
} MPI2_EXT_IMAGE_HEADER, *PTR_MPI2_EXT_IMAGE_HEADER,
	Mpi2ExtImageHeader_t, *pMpi2ExtImageHeader_t;

/*useful offsets */
#define MPI2_EXT_IMAGE_IMAGETYPE_OFFSET         (0x00)
#define MPI2_EXT_IMAGE_IMAGESIZE_OFFSET         (0x08)
#define MPI2_EXT_IMAGE_NEXTIMAGE_OFFSET         (0x0C)
#define MPI2_EXT_IMAGE_PACKAGEVERSION_OFFSET   (0x10)

#define MPI2_EXT_IMAGE_HEADER_SIZE              (0x40)

/*defines for the ImageType field */
#define MPI2_EXT_IMAGE_TYPE_UNSPECIFIED             (0x00)
#define MPI2_EXT_IMAGE_TYPE_FW                      (0x01)
#define MPI2_EXT_IMAGE_TYPE_NVDATA                  (0x03)
#define MPI2_EXT_IMAGE_TYPE_BOOTLOADER              (0x04)
#define MPI2_EXT_IMAGE_TYPE_INITIALIZATION          (0x05)
#define MPI2_EXT_IMAGE_TYPE_FLASH_LAYOUT            (0x06)
#define MPI2_EXT_IMAGE_TYPE_SUPPORTED_DEVICES       (0x07)
#define MPI2_EXT_IMAGE_TYPE_MEGARAID                (0x08)
#define MPI2_EXT_IMAGE_TYPE_ENCRYPTED_HASH          (0x09)
#define MPI2_EXT_IMAGE_TYPE_RDE                     (0x0A)
#define MPI2_EXT_IMAGE_TYPE_PBLP                    (0x0B)
#define MPI2_EXT_IMAGE_TYPE_MIN_PRODUCT_SPECIFIC    (0x80)
#define MPI2_EXT_IMAGE_TYPE_MAX_PRODUCT_SPECIFIC    (0xFF)

#define MPI2_EXT_IMAGE_TYPE_MAX (MPI2_EXT_IMAGE_TYPE_MAX_PRODUCT_SPECIFIC)

/*FLASH Layout Extended Image Data */

/*
 *Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 *one and check RegionsPerLayout at runtime.
 */
#ifndef MPI2_FLASH_NUMBER_OF_REGIONS
#define MPI2_FLASH_NUMBER_OF_REGIONS        (1)
#endif

/*
 *Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 *one and check NumberOfLayouts at runtime.
 */
#ifndef MPI2_FLASH_NUMBER_OF_LAYOUTS
#define MPI2_FLASH_NUMBER_OF_LAYOUTS        (1)
#endif

typedef struct _MPI2_FLASH_REGION {
	U8 RegionType;		/*0x00 */
	U8 Reserved1;		/*0x01 */
	U16 Reserved2;		/*0x02 */
	U32 RegionOffset;	/*0x04 */
	U32 RegionSize;		/*0x08 */
	U32 Reserved3;		/*0x0C */
} MPI2_FLASH_REGION, *PTR_MPI2_FLASH_REGION,
	Mpi2FlashRegion_t, *pMpi2FlashRegion_t;

typedef struct _MPI2_FLASH_LAYOUT {
	U32 FlashSize;		/*0x00 */
	U32 Reserved1;		/*0x04 */
	U32 Reserved2;		/*0x08 */
	U32 Reserved3;		/*0x0C */
	MPI2_FLASH_REGION Region[MPI2_FLASH_NUMBER_OF_REGIONS];	/*0x10 */
} MPI2_FLASH_LAYOUT, *PTR_MPI2_FLASH_LAYOUT,
	Mpi2FlashLayout_t, *pMpi2FlashLayout_t;

typedef struct _MPI2_FLASH_LAYOUT_DATA {
	U8 ImageRevision;	/*0x00 */
	U8 Reserved1;		/*0x01 */
	U8 SizeOfRegion;	/*0x02 */
	U8 Reserved2;		/*0x03 */
	U16 NumberOfLayouts;	/*0x04 */
	U16 RegionsPerLayout;	/*0x06 */
	U16 MinimumSectorAlignment;	/*0x08 */
	U16 Reserved3;		/*0x0A */
	U32 Reserved4;		/*0x0C */
	MPI2_FLASH_LAYOUT Layout[MPI2_FLASH_NUMBER_OF_LAYOUTS];	/*0x10 */
} MPI2_FLASH_LAYOUT_DATA, *PTR_MPI2_FLASH_LAYOUT_DATA,
	Mpi2FlashLayoutData_t, *pMpi2FlashLayoutData_t;

/*defines for the RegionType field */
#define MPI2_FLASH_REGION_UNUSED                (0x00)
#define MPI2_FLASH_REGION_FIRMWARE              (0x01)
#define MPI2_FLASH_REGION_BIOS                  (0x02)
#define MPI2_FLASH_REGION_NVDATA                (0x03)
#define MPI2_FLASH_REGION_FIRMWARE_BACKUP       (0x05)
#define MPI2_FLASH_REGION_MFG_INFORMATION       (0x06)
#define MPI2_FLASH_REGION_CONFIG_1              (0x07)
#define MPI2_FLASH_REGION_CONFIG_2              (0x08)
#define MPI2_FLASH_REGION_MEGARAID              (0x09)
#define MPI2_FLASH_REGION_COMMON_BOOT_BLOCK     (0x0A)
#define MPI2_FLASH_REGION_INIT (MPI2_FLASH_REGION_COMMON_BOOT_BLOCK)
#define MPI2_FLASH_REGION_CBB_BACKUP            (0x0D)
#define MPI2_FLASH_REGION_SBR                   (0x0E)
#define MPI2_FLASH_REGION_SBR_BACKUP            (0x0F)
#define MPI2_FLASH_REGION_HIIM                  (0x10)
#define MPI2_FLASH_REGION_HIIA                  (0x11)
#define MPI2_FLASH_REGION_CTLR                  (0x12)
#define MPI2_FLASH_REGION_IMR_FIRMWARE          (0x13)
#define MPI2_FLASH_REGION_MR_NVDATA             (0x14)
#define MPI2_FLASH_REGION_CPLD                  (0x15)
#define MPI2_FLASH_REGION_PSOC                  (0x16)

/*ImageRevision */
#define MPI2_FLASH_LAYOUT_IMAGE_REVISION        (0x00)

/*Supported Devices Extended Image Data */

/*
 *Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 *one and check NumberOfDevices at runtime.
 */
#ifndef MPI2_SUPPORTED_DEVICES_IMAGE_NUM_DEVICES
#define MPI2_SUPPORTED_DEVICES_IMAGE_NUM_DEVICES    (1)
#endif

typedef struct _MPI2_SUPPORTED_DEVICE {
	U16 DeviceID;		/*0x00 */
	U16 VendorID;		/*0x02 */
	U16 DeviceIDMask;	/*0x04 */
	U16 Reserved1;		/*0x06 */
	U8 LowPCIRev;		/*0x08 */
	U8 HighPCIRev;		/*0x09 */
	U16 Reserved2;		/*0x0A */
	U32 Reserved3;		/*0x0C */
} MPI2_SUPPORTED_DEVICE, *PTR_MPI2_SUPPORTED_DEVICE,
	Mpi2SupportedDevice_t, *pMpi2SupportedDevice_t;

typedef struct _MPI2_SUPPORTED_DEVICES_DATA {
	U8 ImageRevision;	/*0x00 */
	U8 Reserved1;		/*0x01 */
	U8 NumberOfDevices;	/*0x02 */
	U8 Reserved2;		/*0x03 */
	U32 Reserved3;		/*0x04 */
	MPI2_SUPPORTED_DEVICE
	SupportedDevice[MPI2_SUPPORTED_DEVICES_IMAGE_NUM_DEVICES];/*0x08 */
} MPI2_SUPPORTED_DEVICES_DATA, *PTR_MPI2_SUPPORTED_DEVICES_DATA,
	Mpi2SupportedDevicesData_t, *pMpi2SupportedDevicesData_t;

/*ImageRevision */
#define MPI2_SUPPORTED_DEVICES_IMAGE_REVISION   (0x00)

/*Init Extended Image Data */

typedef struct _MPI2_INIT_IMAGE_FOOTER {
	U32 BootFlags;		/*0x00 */
	U32 ImageSize;		/*0x04 */
	U32 Signature0;		/*0x08 */
	U32 Signature1;		/*0x0C */
	U32 Signature2;		/*0x10 */
	U32 ResetVector;	/*0x14 */
} MPI2_INIT_IMAGE_FOOTER, *PTR_MPI2_INIT_IMAGE_FOOTER,
	Mpi2InitImageFooter_t, *pMpi2InitImageFooter_t;

/*defines for the BootFlags field */
#define MPI2_INIT_IMAGE_BOOTFLAGS_OFFSET        (0x00)

/*defines for the ImageSize field */
#define MPI2_INIT_IMAGE_IMAGESIZE_OFFSET        (0x04)

/*defines for the Signature0 field */
#define MPI2_INIT_IMAGE_SIGNATURE0_OFFSET       (0x08)
#define MPI2_INIT_IMAGE_SIGNATURE0              (0x5AA55AEA)

/*defines for the Signature1 field */
#define MPI2_INIT_IMAGE_SIGNATURE1_OFFSET       (0x0C)
#define MPI2_INIT_IMAGE_SIGNATURE1              (0xA55AEAA5)

/*defines for the Signature2 field */
#define MPI2_INIT_IMAGE_SIGNATURE2_OFFSET       (0x10)
#define MPI2_INIT_IMAGE_SIGNATURE2              (0x5AEAA55A)

/*Signature fields as individual bytes */
#define MPI2_INIT_IMAGE_SIGNATURE_BYTE_0        (0xEA)
#define MPI2_INIT_IMAGE_SIGNATURE_BYTE_1        (0x5A)
#define MPI2_INIT_IMAGE_SIGNATURE_BYTE_2        (0xA5)
#define MPI2_INIT_IMAGE_SIGNATURE_BYTE_3        (0x5A)

#define MPI2_INIT_IMAGE_SIGNATURE_BYTE_4        (0xA5)
#define MPI2_INIT_IMAGE_SIGNATURE_BYTE_5        (0xEA)
#define MPI2_INIT_IMAGE_SIGNATURE_BYTE_6        (0x5A)
#define MPI2_INIT_IMAGE_SIGNATURE_BYTE_7        (0xA5)

#define MPI2_INIT_IMAGE_SIGNATURE_BYTE_8        (0x5A)
#define MPI2_INIT_IMAGE_SIGNATURE_BYTE_9        (0xA5)
#define MPI2_INIT_IMAGE_SIGNATURE_BYTE_A        (0xEA)
#define MPI2_INIT_IMAGE_SIGNATURE_BYTE_B        (0x5A)

/*defines for the ResetVector field */
#define MPI2_INIT_IMAGE_RESETVECTOR_OFFSET      (0x14)


/* Encrypted Hash Extended Image Data */

typedef struct _MPI25_ENCRYPTED_HASH_ENTRY {
	U8		HashImageType;		/*0x00 */
	U8		HashAlgorithm;		/*0x01 */
	U8		EncryptionAlgorithm;	/*0x02 */
	U8		Reserved1;		/*0x03 */
	U32		Reserved2;		/*0x04 */
	U32		EncryptedHash[1];	/*0x08 */ /* variable length */
} MPI25_ENCRYPTED_HASH_ENTRY, *PTR_MPI25_ENCRYPTED_HASH_ENTRY,
Mpi25EncryptedHashEntry_t, *pMpi25EncryptedHashEntry_t;

/* values for HashImageType */
#define MPI25_HASH_IMAGE_TYPE_UNUSED            (0x00)
#define MPI25_HASH_IMAGE_TYPE_FIRMWARE          (0x01)
#define MPI25_HASH_IMAGE_TYPE_BIOS              (0x02)

#define MPI26_HASH_IMAGE_TYPE_UNUSED            (0x00)
#define MPI26_HASH_IMAGE_TYPE_FIRMWARE          (0x01)
#define MPI26_HASH_IMAGE_TYPE_BIOS              (0x02)
#define MPI26_HASH_IMAGE_TYPE_KEY_HASH          (0x03)

/* values for HashAlgorithm */
#define MPI25_HASH_ALGORITHM_UNUSED             (0x00)
#define MPI25_HASH_ALGORITHM_SHA256             (0x01)

#define MPI26_HASH_ALGORITHM_VER_MASK		(0xE0)
#define MPI26_HASH_ALGORITHM_VER_NONE		(0x00)
#define MPI26_HASH_ALGORITHM_VER_SHA1		(0x20)
#define MPI26_HASH_ALGORITHM_VER_SHA2		(0x40)
#define MPI26_HASH_ALGORITHM_VER_SHA3		(0x60)
#define MPI26_HASH_ALGORITHM_SIZE_MASK		(0x1F)
#define MPI26_HASH_ALGORITHM_SIZE_256           (0x01)
#define MPI26_HASH_ALGORITHM_SIZE_512           (0x02)


/* values for EncryptionAlgorithm */
#define MPI25_ENCRYPTION_ALG_UNUSED             (0x00)
#define MPI25_ENCRYPTION_ALG_RSA256             (0x01)

#define MPI26_ENCRYPTION_ALG_UNUSED             (0x00)
#define MPI26_ENCRYPTION_ALG_RSA256             (0x01)
#define MPI26_ENCRYPTION_ALG_RSA512             (0x02)
#define MPI26_ENCRYPTION_ALG_RSA1024            (0x03)
#define MPI26_ENCRYPTION_ALG_RSA2048            (0x04)
#define MPI26_ENCRYPTION_ALG_RSA4096            (0x05)

typedef struct _MPI25_ENCRYPTED_HASH_DATA {
	U8				ImageVersion;		/*0x00 */
	U8				NumHash;		/*0x01 */
	U16				Reserved1;		/*0x02 */
	U32				Reserved2;		/*0x04 */
	MPI25_ENCRYPTED_HASH_ENTRY	EncryptedHashEntry[1];  /*0x08 */
} MPI25_ENCRYPTED_HASH_DATA, *PTR_MPI25_ENCRYPTED_HASH_DATA,
Mpi25EncryptedHashData_t, *pMpi25EncryptedHashData_t;


#endif /* MPI2_IMAGE_H */
