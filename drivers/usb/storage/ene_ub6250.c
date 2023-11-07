// SPDX-License-Identifier: GPL-2.0+
#include <linux/jiffies.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/slab.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>

#include <linux/firmware.h>

#include "usb.h"
#include "transport.h"
#include "protocol.h"
#include "debug.h"
#include "scsiglue.h"

#define SD_INIT1_FIRMWARE "ene-ub6250/sd_init1.bin"
#define SD_INIT2_FIRMWARE "ene-ub6250/sd_init2.bin"
#define SD_RW_FIRMWARE "ene-ub6250/sd_rdwr.bin"
#define MS_INIT_FIRMWARE "ene-ub6250/ms_init.bin"
#define MSP_RW_FIRMWARE "ene-ub6250/msp_rdwr.bin"
#define MS_RW_FIRMWARE "ene-ub6250/ms_rdwr.bin"

#define DRV_NAME "ums_eneub6250"

MODULE_DESCRIPTION("Driver for ENE UB6250 reader");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(USB_STORAGE);
MODULE_FIRMWARE(SD_INIT1_FIRMWARE);
MODULE_FIRMWARE(SD_INIT2_FIRMWARE);
MODULE_FIRMWARE(SD_RW_FIRMWARE);
MODULE_FIRMWARE(MS_INIT_FIRMWARE);
MODULE_FIRMWARE(MSP_RW_FIRMWARE);
MODULE_FIRMWARE(MS_RW_FIRMWARE);

/*
 * The table of devices
 */
#define UNUSUAL_DEV(id_vendor, id_product, bcdDeviceMin, bcdDeviceMax, \
		    vendorName, productName, useProtocol, useTransport, \
		    initFunction, flags) \
{ USB_DEVICE_VER(id_vendor, id_product, bcdDeviceMin, bcdDeviceMax), \
	.driver_info = (flags)}

static struct usb_device_id ene_ub6250_usb_ids[] = {
#	include "unusual_ene_ub6250.h"
	{ }		/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, ene_ub6250_usb_ids);

#undef UNUSUAL_DEV

/*
 * The flags table
 */
#define UNUSUAL_DEV(idVendor, idProduct, bcdDeviceMin, bcdDeviceMax, \
		    vendor_name, product_name, use_protocol, use_transport, \
		    init_function, Flags) \
{ \
	.vendorName = vendor_name,	\
	.productName = product_name,	\
	.useProtocol = use_protocol,	\
	.useTransport = use_transport,	\
	.initFunction = init_function,	\
}

static struct us_unusual_dev ene_ub6250_unusual_dev_list[] = {
#	include "unusual_ene_ub6250.h"
	{ }		/* Terminating entry */
};

#undef UNUSUAL_DEV



/* ENE bin code len */
#define ENE_BIN_CODE_LEN    0x800
/* EnE HW Register */
#define REG_CARD_STATUS     0xFF83
#define REG_HW_TRAP1        0xFF89

/* SRB Status */
#define SS_SUCCESS		0x000000	/* No Sense */
#define SS_NOT_READY		0x023A00	/* Medium not present */
#define SS_MEDIUM_ERR		0x031100	/* Unrecovered read error */
#define SS_HW_ERR		0x040800	/* Communication failure */
#define SS_ILLEGAL_REQUEST	0x052000	/* Invalid command */
#define SS_UNIT_ATTENTION	0x062900	/* Reset occurred */

/* ENE Load FW Pattern */
#define SD_INIT1_PATTERN   1
#define SD_INIT2_PATTERN   2
#define SD_RW_PATTERN      3
#define MS_INIT_PATTERN    4
#define MSP_RW_PATTERN     5
#define MS_RW_PATTERN      6
#define SM_INIT_PATTERN    7
#define SM_RW_PATTERN      8

#define FDIR_WRITE         0
#define FDIR_READ          1

/* For MS Card */

/* Status Register 1 */
#define MS_REG_ST1_MB           0x80    /* media busy */
#define MS_REG_ST1_FB1          0x40    /* flush busy 1 */
#define MS_REG_ST1_DTER         0x20    /* error on data(corrected) */
#define MS_REG_ST1_UCDT         0x10    /* unable to correct data */
#define MS_REG_ST1_EXER         0x08    /* error on extra(corrected) */
#define MS_REG_ST1_UCEX         0x04    /* unable to correct extra */
#define MS_REG_ST1_FGER         0x02    /* error on overwrite flag(corrected) */
#define MS_REG_ST1_UCFG         0x01    /* unable to correct overwrite flag */
#define MS_REG_ST1_DEFAULT	(MS_REG_ST1_MB | MS_REG_ST1_FB1 | MS_REG_ST1_DTER | MS_REG_ST1_UCDT | MS_REG_ST1_EXER | MS_REG_ST1_UCEX | MS_REG_ST1_FGER | MS_REG_ST1_UCFG)

/* Overwrite Area */
#define MS_REG_OVR_BKST		0x80            /* block status */
#define MS_REG_OVR_BKST_OK	MS_REG_OVR_BKST     /* OK */
#define MS_REG_OVR_BKST_NG	0x00            /* NG */
#define MS_REG_OVR_PGST0	0x40            /* page status */
#define MS_REG_OVR_PGST1	0x20
#define MS_REG_OVR_PGST_MASK	(MS_REG_OVR_PGST0 | MS_REG_OVR_PGST1)
#define MS_REG_OVR_PGST_OK	(MS_REG_OVR_PGST0 | MS_REG_OVR_PGST1) /* OK */
#define MS_REG_OVR_PGST_NG	MS_REG_OVR_PGST1                      /* NG */
#define MS_REG_OVR_PGST_DATA_ERROR	0x00        /* data error */
#define MS_REG_OVR_UDST			0x10        /* update status */
#define MS_REG_OVR_UDST_UPDATING	0x00        /* updating */
#define MS_REG_OVR_UDST_NO_UPDATE	MS_REG_OVR_UDST
#define MS_REG_OVR_RESERVED	0x08
#define MS_REG_OVR_DEFAULT	(MS_REG_OVR_BKST_OK | MS_REG_OVR_PGST_OK | MS_REG_OVR_UDST_NO_UPDATE | MS_REG_OVR_RESERVED)

/* Management Flag */
#define MS_REG_MNG_SCMS0	0x20    /* serial copy management system */
#define MS_REG_MNG_SCMS1	0x10
#define MS_REG_MNG_SCMS_MASK		(MS_REG_MNG_SCMS0 | MS_REG_MNG_SCMS1)
#define MS_REG_MNG_SCMS_COPY_OK		(MS_REG_MNG_SCMS0 | MS_REG_MNG_SCMS1)
#define MS_REG_MNG_SCMS_ONE_COPY	MS_REG_MNG_SCMS1
#define MS_REG_MNG_SCMS_NO_COPY	0x00
#define MS_REG_MNG_ATFLG	0x08    /* address transfer table flag */
#define MS_REG_MNG_ATFLG_OTHER	MS_REG_MNG_ATFLG    /* other */
#define MS_REG_MNG_ATFLG_ATTBL	0x00	/* address transfer table */
#define MS_REG_MNG_SYSFLG	0x04	/* system flag */
#define MS_REG_MNG_SYSFLG_USER	MS_REG_MNG_SYSFLG   /* user block */
#define MS_REG_MNG_SYSFLG_BOOT	0x00	/* system block */
#define MS_REG_MNG_RESERVED	0xc3
#define MS_REG_MNG_DEFAULT	(MS_REG_MNG_SCMS_COPY_OK | MS_REG_MNG_ATFLG_OTHER | MS_REG_MNG_SYSFLG_USER | MS_REG_MNG_RESERVED)


#define MS_MAX_PAGES_PER_BLOCK		32
#define MS_MAX_INITIAL_ERROR_BLOCKS 	10
#define MS_LIB_BITS_PER_BYTE		8

#define MS_SYSINF_FORMAT_FAT		1
#define MS_SYSINF_USAGE_GENERAL		0

#define MS_SYSINF_MSCLASS_TYPE_1	1
#define MS_SYSINF_PAGE_SIZE		MS_BYTES_PER_PAGE /* fixed */

#define MS_SYSINF_CARDTYPE_RDONLY	1
#define MS_SYSINF_CARDTYPE_RDWR		2
#define MS_SYSINF_CARDTYPE_HYBRID	3
#define MS_SYSINF_SECURITY		0x01
#define MS_SYSINF_SECURITY_NO_SUPPORT	MS_SYSINF_SECURITY
#define MS_SYSINF_SECURITY_SUPPORT	0

#define MS_SYSINF_RESERVED1		1
#define MS_SYSINF_RESERVED2		1

#define MS_SYSENT_TYPE_INVALID_BLOCK	0x01
#define MS_SYSENT_TYPE_CIS_IDI		0x0a    /* CIS/IDI */

#define SIZE_OF_KIRO		1024
#define BYTE_MASK		0xff

/* ms error code */
#define MS_STATUS_WRITE_PROTECT	0x0106
#define MS_STATUS_SUCCESS	0x0000
#define MS_ERROR_FLASH_READ	0x8003
#define MS_ERROR_FLASH_ERASE	0x8005
#define MS_LB_ERROR		0xfff0
#define MS_LB_BOOT_BLOCK	0xfff1
#define MS_LB_INITIAL_ERROR	0xfff2
#define MS_STATUS_SUCCESS_WITH_ECC 0xfff3
#define MS_LB_ACQUIRED_ERROR	0xfff4
#define MS_LB_NOT_USED_ERASED	0xfff5
#define MS_NOCARD_ERROR		0xfff8
#define MS_NO_MEMORY_ERROR	0xfff9
#define MS_STATUS_INT_ERROR	0xfffa
#define MS_STATUS_ERROR		0xfffe
#define MS_LB_NOT_USED		0xffff

#define MS_REG_MNG_SYSFLG	0x04    /* system flag */
#define MS_REG_MNG_SYSFLG_USER	MS_REG_MNG_SYSFLG   /* user block */

#define MS_BOOT_BLOCK_ID                        0x0001
#define MS_BOOT_BLOCK_FORMAT_VERSION            0x0100
#define MS_BOOT_BLOCK_DATA_ENTRIES              2

#define MS_NUMBER_OF_SYSTEM_ENTRY       	4
#define MS_NUMBER_OF_BOOT_BLOCK			2
#define MS_BYTES_PER_PAGE			512
#define MS_LOGICAL_BLOCKS_PER_SEGMENT		496
#define MS_LOGICAL_BLOCKS_IN_1ST_SEGMENT        494

#define MS_PHYSICAL_BLOCKS_PER_SEGMENT		0x200 /* 512 */
#define MS_PHYSICAL_BLOCKS_PER_SEGMENT_MASK     0x1ff

/* overwrite area */
#define MS_REG_OVR_BKST		0x80		/* block status */
#define MS_REG_OVR_BKST_OK	MS_REG_OVR_BKST	/* OK */
#define MS_REG_OVR_BKST_NG	0x00            /* NG */

/* Status Register 1 */
#define MS_REG_ST1_DTER		0x20	/* error on data(corrected) */
#define MS_REG_ST1_EXER		0x08	/* error on extra(corrected) */
#define MS_REG_ST1_FGER		0x02	/* error on overwrite flag(corrected) */

/* MemoryStick Register */
/* Status Register 0 */
#define MS_REG_ST0_WP		0x01	/* write protected */
#define MS_REG_ST0_WP_ON	MS_REG_ST0_WP

#define MS_LIB_CTRL_RDONLY      0
#define MS_LIB_CTRL_WRPROTECT   1

/*dphy->log table */
#define ms_libconv_to_logical(pdx, PhyBlock) (((PhyBlock) >= (pdx)->MS_Lib.NumberOfPhyBlock) ? MS_STATUS_ERROR : (pdx)->MS_Lib.Phy2LogMap[PhyBlock])
#define ms_libconv_to_physical(pdx, LogBlock) (((LogBlock) >= (pdx)->MS_Lib.NumberOfLogBlock) ? MS_STATUS_ERROR : (pdx)->MS_Lib.Log2PhyMap[LogBlock])

#define ms_lib_ctrl_set(pdx, Flag)	((pdx)->MS_Lib.flags |= (1 << (Flag)))
#define ms_lib_ctrl_reset(pdx, Flag)	((pdx)->MS_Lib.flags &= ~(1 << (Flag)))
#define ms_lib_ctrl_check(pdx, Flag)	((pdx)->MS_Lib.flags & (1 << (Flag)))

#define ms_lib_iswritable(pdx) ((ms_lib_ctrl_check((pdx), MS_LIB_CTRL_RDONLY) == 0) && (ms_lib_ctrl_check(pdx, MS_LIB_CTRL_WRPROTECT) == 0))
#define ms_lib_clear_pagemap(pdx) memset((pdx)->MS_Lib.pagemap, 0, sizeof((pdx)->MS_Lib.pagemap))
#define memstick_logaddr(logadr1, logadr0) ((((u16)(logadr1)) << 8) | (logadr0))


/* SD_STATUS bits */
#define SD_Insert	BIT(0)
#define SD_Ready	BIT(1)
#define SD_MediaChange	BIT(2)
#define SD_IsMMC	BIT(3)
#define SD_HiCapacity	BIT(4)
#define SD_HiSpeed	BIT(5)
#define SD_WtP		BIT(6)
			/* Bit 7 reserved */

/* MS_STATUS bits */
#define MS_Insert	BIT(0)
#define MS_Ready	BIT(1)
#define MS_MediaChange	BIT(2)
#define MS_IsMSPro	BIT(3)
#define MS_IsMSPHG	BIT(4)
			/* Bit 5 reserved */
#define MS_WtP		BIT(6)
			/* Bit 7 reserved */

/* SM_STATUS bits */
#define SM_Insert	BIT(0)
#define SM_Ready	BIT(1)
#define SM_MediaChange	BIT(2)
			/* Bits 3-5 reserved */
#define SM_WtP		BIT(6)
#define SM_IsMS		BIT(7)

struct ms_bootblock_cis {
	u8 bCistplDEVICE[6];    /* 0 */
	u8 bCistplDEVICE0C[6];  /* 6 */
	u8 bCistplJEDECC[4];    /* 12 */
	u8 bCistplMANFID[6];    /* 16 */
	u8 bCistplVER1[32];     /* 22 */
	u8 bCistplFUNCID[4];    /* 54 */
	u8 bCistplFUNCE0[4];    /* 58 */
	u8 bCistplFUNCE1[5];    /* 62 */
	u8 bCistplCONF[7];      /* 67 */
	u8 bCistplCFTBLENT0[10];/* 74 */
	u8 bCistplCFTBLENT1[8]; /* 84 */
	u8 bCistplCFTBLENT2[12];/* 92 */
	u8 bCistplCFTBLENT3[8]; /* 104 */
	u8 bCistplCFTBLENT4[17];/* 112 */
	u8 bCistplCFTBLENT5[8]; /* 129 */
	u8 bCistplCFTBLENT6[17];/* 137 */
	u8 bCistplCFTBLENT7[8]; /* 154 */
	u8 bCistplNOLINK[3];    /* 162 */
} ;

struct ms_bootblock_idi {
#define MS_IDI_GENERAL_CONF 0x848A
	u16 wIDIgeneralConfiguration;	/* 0 */
	u16 wIDInumberOfCylinder;	/* 1 */
	u16 wIDIreserved0;		/* 2 */
	u16 wIDInumberOfHead;		/* 3 */
	u16 wIDIbytesPerTrack;		/* 4 */
	u16 wIDIbytesPerSector;		/* 5 */
	u16 wIDIsectorsPerTrack;	/* 6 */
	u16 wIDItotalSectors[2];	/* 7-8  high,low */
	u16 wIDIreserved1[11];		/* 9-19 */
	u16 wIDIbufferType;		/* 20 */
	u16 wIDIbufferSize;		/* 21 */
	u16 wIDIlongCmdECC;		/* 22 */
	u16 wIDIfirmVersion[4];		/* 23-26 */
	u16 wIDImodelName[20];		/* 27-46 */
	u16 wIDIreserved2;		/* 47 */
	u16 wIDIlongWordSupported;	/* 48 */
	u16 wIDIdmaSupported;		/* 49 */
	u16 wIDIreserved3;		/* 50 */
	u16 wIDIpioTiming;		/* 51 */
	u16 wIDIdmaTiming;		/* 52 */
	u16 wIDItransferParameter;	/* 53 */
	u16 wIDIformattedCylinder;	/* 54 */
	u16 wIDIformattedHead;		/* 55 */
	u16 wIDIformattedSectorsPerTrack;/* 56 */
	u16 wIDIformattedTotalSectors[2];/* 57-58 */
	u16 wIDImultiSector;		/* 59 */
	u16 wIDIlbaSectors[2];		/* 60-61 */
	u16 wIDIsingleWordDMA;		/* 62 */
	u16 wIDImultiWordDMA;		/* 63 */
	u16 wIDIreserved4[192];		/* 64-255 */
};

struct ms_bootblock_sysent_rec {
	u32 dwStart;
	u32 dwSize;
	u8 bType;
	u8 bReserved[3];
};

struct ms_bootblock_sysent {
	struct ms_bootblock_sysent_rec entry[MS_NUMBER_OF_SYSTEM_ENTRY];
};

struct ms_bootblock_sysinf {
	u8 bMsClass;			/* must be 1 */
	u8 bCardType;			/* see below */
	u16 wBlockSize;			/* n KB */
	u16 wBlockNumber;		/* number of physical block */
	u16 wTotalBlockNumber;		/* number of logical block */
	u16 wPageSize;			/* must be 0x200 */
	u8 bExtraSize;			/* 0x10 */
	u8 bSecuritySupport;
	u8 bAssemblyDate[8];
	u8 bFactoryArea[4];
	u8 bAssemblyMakerCode;
	u8 bAssemblyMachineCode[3];
	u16 wMemoryMakerCode;
	u16 wMemoryDeviceCode;
	u16 wMemorySize;
	u8 bReserved1;
	u8 bReserved2;
	u8 bVCC;
	u8 bVPP;
	u16 wControllerChipNumber;
	u16 wControllerFunction;	/* New MS */
	u8 bReserved3[9];		/* New MS */
	u8 bParallelSupport;		/* New MS */
	u16 wFormatValue;		/* New MS */
	u8 bFormatType;
	u8 bUsage;
	u8 bDeviceType;
	u8 bReserved4[22];
	u8 bFUValue3;
	u8 bFUValue4;
	u8 bReserved5[15];
};

struct ms_bootblock_header {
	u16 wBlockID;
	u16 wFormatVersion;
	u8 bReserved1[184];
	u8 bNumberOfDataEntry;
	u8 bReserved2[179];
};

struct ms_bootblock_page0 {
	struct ms_bootblock_header header;
	struct ms_bootblock_sysent sysent;
	struct ms_bootblock_sysinf sysinf;
};

struct ms_bootblock_cis_idi {
	union {
		struct ms_bootblock_cis cis;
		u8 dmy[256];
	} cis;

	union {
		struct ms_bootblock_idi idi;
		u8 dmy[256];
	} idi;

};

/* ENE MS Lib struct */
struct ms_lib_type_extdat {
	u8 reserved;
	u8 intr;
	u8 status0;
	u8 status1;
	u8 ovrflg;
	u8 mngflg;
	u16 logadr;
};

struct ms_lib_ctrl {
	u32 flags;
	u32 BytesPerSector;
	u32 NumberOfCylinder;
	u32 SectorsPerCylinder;
	u16 cardType;			/* R/W, RO, Hybrid */
	u16 blockSize;
	u16 PagesPerBlock;
	u16 NumberOfPhyBlock;
	u16 NumberOfLogBlock;
	u16 NumberOfSegment;
	u16 *Phy2LogMap;		/* phy2log table */
	u16 *Log2PhyMap;		/* log2phy table */
	u16 wrtblk;
	unsigned char *pagemap[(MS_MAX_PAGES_PER_BLOCK + (MS_LIB_BITS_PER_BYTE-1)) / MS_LIB_BITS_PER_BYTE];
	unsigned char *blkpag;
	struct ms_lib_type_extdat *blkext;
	unsigned char copybuf[512];
};


/* SD Block Length */
/* 2^9 = 512 Bytes, The HW maximum read/write data length */
#define SD_BLOCK_LEN  9

struct ene_ub6250_info {

	/* I/O bounce buffer */
	u8		*bbuf;

	/* for 6250 code */
	u8		SD_Status;
	u8		MS_Status;
	u8		SM_Status;

	/* ----- SD Control Data ---------------- */
	/*SD_REGISTER SD_Regs; */
	u16		SD_Block_Mult;
	u8		SD_READ_BL_LEN;
	u16		SD_C_SIZE;
	u8		SD_C_SIZE_MULT;

	/* SD/MMC New spec. */
	u8		SD_SPEC_VER;
	u8		SD_CSD_VER;
	u8		SD20_HIGH_CAPACITY;
	u32		HC_C_SIZE;
	u8		MMC_SPEC_VER;
	u8		MMC_BusWidth;
	u8		MMC_HIGH_CAPACITY;

	/*----- MS Control Data ---------------- */
	bool		MS_SWWP;
	u32		MSP_TotalBlock;
	struct ms_lib_ctrl MS_Lib;
	bool		MS_IsRWPage;
	u16		MS_Model;

	/*----- SM Control Data ---------------- */
	u8		SM_DeviceID;
	u8		SM_CardID;

	unsigned char	*testbuf;
	u8		BIN_FLAG;
	u32		bl_num;
	int		SrbStatus;

	/*------Power Managerment ---------------*/
	bool		Power_IsResum;
};

static int ene_sd_init(struct us_data *us);
static int ene_ms_init(struct us_data *us);
static int ene_load_bincode(struct us_data *us, unsigned char flag);

static void ene_ub6250_info_destructor(void *extra)
{
	struct ene_ub6250_info *info = (struct ene_ub6250_info *) extra;

	if (!extra)
		return;
	kfree(info->bbuf);
}

static int ene_send_scsi_cmd(struct us_data *us, u8 fDir, void *buf, int use_sg)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	struct bulk_cs_wrap *bcs = (struct bulk_cs_wrap *) us->iobuf;

	int result;
	unsigned int residue;
	unsigned int cswlen = 0, partial = 0;
	unsigned int transfer_length = bcb->DataTransferLength;

	/* usb_stor_dbg(us, "transport --- ene_send_scsi_cmd\n"); */
	/* send cmd to out endpoint */
	result = usb_stor_bulk_transfer_buf(us, us->send_bulk_pipe,
					    bcb, US_BULK_CB_WRAP_LEN, NULL);
	if (result != USB_STOR_XFER_GOOD) {
		usb_stor_dbg(us, "send cmd to out endpoint fail ---\n");
		return USB_STOR_TRANSPORT_ERROR;
	}

	if (buf) {
		unsigned int pipe = fDir;

		if (fDir  == FDIR_READ)
			pipe = us->recv_bulk_pipe;
		else
			pipe = us->send_bulk_pipe;

		/* Bulk */
		if (use_sg) {
			result = usb_stor_bulk_srb(us, pipe, us->srb);
		} else {
			result = usb_stor_bulk_transfer_sg(us, pipe, buf,
						transfer_length, 0, &partial);
		}
		if (result != USB_STOR_XFER_GOOD) {
			usb_stor_dbg(us, "data transfer fail ---\n");
			return USB_STOR_TRANSPORT_ERROR;
		}
	}

	/* Get CSW for device status */
	result = usb_stor_bulk_transfer_buf(us, us->recv_bulk_pipe, bcs,
					    US_BULK_CS_WRAP_LEN, &cswlen);

	if (result == USB_STOR_XFER_SHORT && cswlen == 0) {
		usb_stor_dbg(us, "Received 0-length CSW; retrying...\n");
		result = usb_stor_bulk_transfer_buf(us, us->recv_bulk_pipe,
					    bcs, US_BULK_CS_WRAP_LEN, &cswlen);
	}

	if (result == USB_STOR_XFER_STALLED) {
		/* get the status again */
		usb_stor_dbg(us, "Attempting to get CSW (2nd try)...\n");
		result = usb_stor_bulk_transfer_buf(us, us->recv_bulk_pipe,
						bcs, US_BULK_CS_WRAP_LEN, NULL);
	}

	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	/* check bulk status */
	residue = le32_to_cpu(bcs->Residue);

	/*
	 * try to compute the actual residue, based on how much data
	 * was really transferred and what the device tells us
	 */
	if (residue && !(us->fflags & US_FL_IGNORE_RESIDUE)) {
		residue = min(residue, transfer_length);
		if (us->srb != NULL)
			scsi_set_resid(us->srb, max(scsi_get_resid(us->srb),
								residue));
	}

	if (bcs->Status != US_BULK_STAT_OK)
		return USB_STOR_TRANSPORT_ERROR;

	return USB_STOR_TRANSPORT_GOOD;
}

static int do_scsi_request_sense(struct us_data *us, struct scsi_cmnd *srb)
{
	struct ene_ub6250_info *info = (struct ene_ub6250_info *) us->extra;
	unsigned char buf[18];

	memset(buf, 0, 18);
	buf[0] = 0x70;				/* Current error */
	buf[2] = info->SrbStatus >> 16;		/* Sense key */
	buf[7] = 10;				/* Additional length */
	buf[12] = info->SrbStatus >> 8;		/* ASC */
	buf[13] = info->SrbStatus;		/* ASCQ */

	usb_stor_set_xfer_buf(buf, sizeof(buf), srb);
	return USB_STOR_TRANSPORT_GOOD;
}

static int do_scsi_inquiry(struct us_data *us, struct scsi_cmnd *srb)
{
	unsigned char data_ptr[36] = {
		0x00, 0x00, 0x02, 0x00, 0x1F, 0x00, 0x00, 0x00, 0x55,
		0x53, 0x42, 0x32, 0x2E, 0x30, 0x20, 0x20, 0x43, 0x61,
		0x72, 0x64, 0x52, 0x65, 0x61, 0x64, 0x65, 0x72, 0x20,
		0x20, 0x20, 0x20, 0x20, 0x20, 0x30, 0x31, 0x30, 0x30 };

	usb_stor_set_xfer_buf(data_ptr, 36, srb);
	return USB_STOR_TRANSPORT_GOOD;
}

static int sd_scsi_test_unit_ready(struct us_data *us, struct scsi_cmnd *srb)
{
	struct ene_ub6250_info *info = (struct ene_ub6250_info *) us->extra;

	if ((info->SD_Status & SD_Insert) && (info->SD_Status & SD_Ready))
		return USB_STOR_TRANSPORT_GOOD;
	else {
		ene_sd_init(us);
		return USB_STOR_TRANSPORT_GOOD;
	}

	return USB_STOR_TRANSPORT_GOOD;
}

static int sd_scsi_mode_sense(struct us_data *us, struct scsi_cmnd *srb)
{
	struct ene_ub6250_info *info = (struct ene_ub6250_info *) us->extra;
	unsigned char mediaNoWP[12] = {
		0x0b, 0x00, 0x00, 0x08, 0x00, 0x00,
		0x71, 0xc0, 0x00, 0x00, 0x02, 0x00 };
	unsigned char mediaWP[12]   = {
		0x0b, 0x00, 0x80, 0x08, 0x00, 0x00,
		0x71, 0xc0, 0x00, 0x00, 0x02, 0x00 };

	if (info->SD_Status & SD_WtP)
		usb_stor_set_xfer_buf(mediaWP, 12, srb);
	else
		usb_stor_set_xfer_buf(mediaNoWP, 12, srb);


	return USB_STOR_TRANSPORT_GOOD;
}

static int sd_scsi_read_capacity(struct us_data *us, struct scsi_cmnd *srb)
{
	u32	bl_num;
	u32	bl_len;
	unsigned int offset = 0;
	unsigned char    buf[8];
	struct scatterlist *sg = NULL;
	struct ene_ub6250_info *info = (struct ene_ub6250_info *) us->extra;

	usb_stor_dbg(us, "sd_scsi_read_capacity\n");
	if (info->SD_Status & SD_HiCapacity) {
		bl_len = 0x200;
		if (info->SD_Status & SD_IsMMC)
			bl_num = info->HC_C_SIZE-1;
		else
			bl_num = (info->HC_C_SIZE + 1) * 1024 - 1;
	} else {
		bl_len = 1 << (info->SD_READ_BL_LEN);
		bl_num = info->SD_Block_Mult * (info->SD_C_SIZE + 1)
				* (1 << (info->SD_C_SIZE_MULT + 2)) - 1;
	}
	info->bl_num = bl_num;
	usb_stor_dbg(us, "bl_len = %x\n", bl_len);
	usb_stor_dbg(us, "bl_num = %x\n", bl_num);

	/*srb->request_bufflen = 8; */
	buf[0] = (bl_num >> 24) & 0xff;
	buf[1] = (bl_num >> 16) & 0xff;
	buf[2] = (bl_num >> 8) & 0xff;
	buf[3] = (bl_num >> 0) & 0xff;
	buf[4] = (bl_len >> 24) & 0xff;
	buf[5] = (bl_len >> 16) & 0xff;
	buf[6] = (bl_len >> 8) & 0xff;
	buf[7] = (bl_len >> 0) & 0xff;

	usb_stor_access_xfer_buf(buf, 8, srb, &sg, &offset, TO_XFER_BUF);

	return USB_STOR_TRANSPORT_GOOD;
}

static int sd_scsi_read(struct us_data *us, struct scsi_cmnd *srb)
{
	int result;
	unsigned char *cdb = srb->cmnd;
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	struct ene_ub6250_info *info = (struct ene_ub6250_info *) us->extra;

	u32 bn = ((cdb[2] << 24) & 0xff000000) | ((cdb[3] << 16) & 0x00ff0000) |
		 ((cdb[4] << 8) & 0x0000ff00) | ((cdb[5] << 0) & 0x000000ff);
	u16 blen = ((cdb[7] << 8) & 0xff00) | ((cdb[8] << 0) & 0x00ff);
	u32 bnByte = bn * 0x200;
	u32 blenByte = blen * 0x200;

	if (bn > info->bl_num)
		return USB_STOR_TRANSPORT_ERROR;

	result = ene_load_bincode(us, SD_RW_PATTERN);
	if (result != USB_STOR_XFER_GOOD) {
		usb_stor_dbg(us, "Load SD RW pattern Fail !!\n");
		return USB_STOR_TRANSPORT_ERROR;
	}

	if (info->SD_Status & SD_HiCapacity)
		bnByte = bn;

	/* set up the command wrapper */
	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength = blenByte;
	bcb->Flags  = US_BULK_FLAG_IN;
	bcb->CDB[0] = 0xF1;
	bcb->CDB[5] = (unsigned char)(bnByte);
	bcb->CDB[4] = (unsigned char)(bnByte>>8);
	bcb->CDB[3] = (unsigned char)(bnByte>>16);
	bcb->CDB[2] = (unsigned char)(bnByte>>24);

	result = ene_send_scsi_cmd(us, FDIR_READ, scsi_sglist(srb), 1);
	return result;
}

static int sd_scsi_write(struct us_data *us, struct scsi_cmnd *srb)
{
	int result;
	unsigned char *cdb = srb->cmnd;
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	struct ene_ub6250_info *info = (struct ene_ub6250_info *) us->extra;

	u32 bn = ((cdb[2] << 24) & 0xff000000) | ((cdb[3] << 16) & 0x00ff0000) |
		 ((cdb[4] << 8) & 0x0000ff00) | ((cdb[5] << 0) & 0x000000ff);
	u16 blen = ((cdb[7] << 8) & 0xff00) | ((cdb[8] << 0) & 0x00ff);
	u32 bnByte = bn * 0x200;
	u32 blenByte = blen * 0x200;

	if (bn > info->bl_num)
		return USB_STOR_TRANSPORT_ERROR;

	result = ene_load_bincode(us, SD_RW_PATTERN);
	if (result != USB_STOR_XFER_GOOD) {
		usb_stor_dbg(us, "Load SD RW pattern Fail !!\n");
		return USB_STOR_TRANSPORT_ERROR;
	}

	if (info->SD_Status & SD_HiCapacity)
		bnByte = bn;

	/* set up the command wrapper */
	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength = blenByte;
	bcb->Flags  = 0x00;
	bcb->CDB[0] = 0xF0;
	bcb->CDB[5] = (unsigned char)(bnByte);
	bcb->CDB[4] = (unsigned char)(bnByte>>8);
	bcb->CDB[3] = (unsigned char)(bnByte>>16);
	bcb->CDB[2] = (unsigned char)(bnByte>>24);

	result = ene_send_scsi_cmd(us, FDIR_WRITE, scsi_sglist(srb), 1);
	return result;
}

/*
 * ENE MS Card
 */

static int ms_lib_set_logicalpair(struct us_data *us, u16 logblk, u16 phyblk)
{
	struct ene_ub6250_info *info = (struct ene_ub6250_info *) us->extra;

	if ((logblk >= info->MS_Lib.NumberOfLogBlock) || (phyblk >= info->MS_Lib.NumberOfPhyBlock))
		return (u32)-1;

	info->MS_Lib.Phy2LogMap[phyblk] = logblk;
	info->MS_Lib.Log2PhyMap[logblk] = phyblk;

	return 0;
}

static int ms_lib_set_logicalblockmark(struct us_data *us, u16 phyblk, u16 mark)
{
	struct ene_ub6250_info *info = (struct ene_ub6250_info *) us->extra;

	if (phyblk >= info->MS_Lib.NumberOfPhyBlock)
		return (u32)-1;

	info->MS_Lib.Phy2LogMap[phyblk] = mark;

	return 0;
}

static int ms_lib_set_initialerrorblock(struct us_data *us, u16 phyblk)
{
	return ms_lib_set_logicalblockmark(us, phyblk, MS_LB_INITIAL_ERROR);
}

static int ms_lib_set_bootblockmark(struct us_data *us, u16 phyblk)
{
	return ms_lib_set_logicalblockmark(us, phyblk, MS_LB_BOOT_BLOCK);
}

static int ms_lib_free_logicalmap(struct us_data *us)
{
	struct ene_ub6250_info *info = (struct ene_ub6250_info *) us->extra;

	kfree(info->MS_Lib.Phy2LogMap);
	info->MS_Lib.Phy2LogMap = NULL;

	kfree(info->MS_Lib.Log2PhyMap);
	info->MS_Lib.Log2PhyMap = NULL;

	return 0;
}

static int ms_lib_alloc_logicalmap(struct us_data *us)
{
	u32  i;
	struct ene_ub6250_info *info = (struct ene_ub6250_info *) us->extra;

	info->MS_Lib.Phy2LogMap = kmalloc_array(info->MS_Lib.NumberOfPhyBlock,
						sizeof(u16),
						GFP_KERNEL);
	info->MS_Lib.Log2PhyMap = kmalloc_array(info->MS_Lib.NumberOfLogBlock,
						sizeof(u16),
						GFP_KERNEL);

	if ((info->MS_Lib.Phy2LogMap == NULL) || (info->MS_Lib.Log2PhyMap == NULL)) {
		ms_lib_free_logicalmap(us);
		return (u32)-1;
	}

	for (i = 0; i < info->MS_Lib.NumberOfPhyBlock; i++)
		info->MS_Lib.Phy2LogMap[i] = MS_LB_NOT_USED;

	for (i = 0; i < info->MS_Lib.NumberOfLogBlock; i++)
		info->MS_Lib.Log2PhyMap[i] = MS_LB_NOT_USED;

	return 0;
}

static void ms_lib_clear_writebuf(struct us_data *us)
{
	int i;
	struct ene_ub6250_info *info = (struct ene_ub6250_info *) us->extra;

	info->MS_Lib.wrtblk = (u16)-1;
	ms_lib_clear_pagemap(info);

	if (info->MS_Lib.blkpag)
		memset(info->MS_Lib.blkpag, 0xff, info->MS_Lib.PagesPerBlock * info->MS_Lib.BytesPerSector);

	if (info->MS_Lib.blkext) {
		for (i = 0; i < info->MS_Lib.PagesPerBlock; i++) {
			info->MS_Lib.blkext[i].status1 = MS_REG_ST1_DEFAULT;
			info->MS_Lib.blkext[i].ovrflg = MS_REG_OVR_DEFAULT;
			info->MS_Lib.blkext[i].mngflg = MS_REG_MNG_DEFAULT;
			info->MS_Lib.blkext[i].logadr = MS_LB_NOT_USED;
		}
	}
}

static int ms_count_freeblock(struct us_data *us, u16 PhyBlock)
{
	u32 Ende, Count;
	struct ene_ub6250_info *info = (struct ene_ub6250_info *) us->extra;

	Ende = PhyBlock + MS_PHYSICAL_BLOCKS_PER_SEGMENT;
	for (Count = 0; PhyBlock < Ende; PhyBlock++) {
		switch (info->MS_Lib.Phy2LogMap[PhyBlock]) {
		case MS_LB_NOT_USED:
		case MS_LB_NOT_USED_ERASED:
			Count++;
		default:
			break;
		}
	}

	return Count;
}

static int ms_read_readpage(struct us_data *us, u32 PhyBlockAddr,
		u8 PageNum, u32 *PageBuf, struct ms_lib_type_extdat *ExtraDat)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	struct ene_ub6250_info *info = (struct ene_ub6250_info *) us->extra;
	u8 *bbuf = info->bbuf;
	int result;
	u32 bn = PhyBlockAddr * 0x20 + PageNum;

	result = ene_load_bincode(us, MS_RW_PATTERN);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	/* Read Page Data */
	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength = 0x200;
	bcb->Flags      = US_BULK_FLAG_IN;
	bcb->CDB[0]     = 0xF1;

	bcb->CDB[1]     = 0x02; /* in init.c ENE_MSInit() is 0x01 */

	bcb->CDB[5]     = (unsigned char)(bn);
	bcb->CDB[4]     = (unsigned char)(bn>>8);
	bcb->CDB[3]     = (unsigned char)(bn>>16);
	bcb->CDB[2]     = (unsigned char)(bn>>24);

	result = ene_send_scsi_cmd(us, FDIR_READ, PageBuf, 0);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;


	/* Read Extra Data */
	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength = 0x4;
	bcb->Flags      = US_BULK_FLAG_IN;
	bcb->CDB[0]     = 0xF1;
	bcb->CDB[1]     = 0x03;

	bcb->CDB[5]     = (unsigned char)(PageNum);
	bcb->CDB[4]     = (unsigned char)(PhyBlockAddr);
	bcb->CDB[3]     = (unsigned char)(PhyBlockAddr>>8);
	bcb->CDB[2]     = (unsigned char)(PhyBlockAddr>>16);
	bcb->CDB[6]     = 0x01;

	result = ene_send_scsi_cmd(us, FDIR_READ, bbuf, 0);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	ExtraDat->reserved = 0;
	ExtraDat->intr     = 0x80;  /* Not yet,fireware support */
	ExtraDat->status0  = 0x10;  /* Not yet,fireware support */

	ExtraDat->status1  = 0x00;  /* Not yet,fireware support */
	ExtraDat->ovrflg   = bbuf[0];
	ExtraDat->mngflg   = bbuf[1];
	ExtraDat->logadr   = memstick_logaddr(bbuf[2], bbuf[3]);

	return USB_STOR_TRANSPORT_GOOD;
}

static int ms_lib_process_bootblock(struct us_data *us, u16 PhyBlock, u8 *PageData)
{
	struct ms_bootblock_sysent *SysEntry;
	struct ms_bootblock_sysinf *SysInfo;
	u32 i, result;
	u8 PageNumber;
	u8 *PageBuffer;
	struct ms_lib_type_extdat ExtraData;
	struct ene_ub6250_info *info = (struct ene_ub6250_info *) us->extra;

	PageBuffer = kzalloc(MS_BYTES_PER_PAGE * 2, GFP_KERNEL);
	if (PageBuffer == NULL)
		return (u32)-1;

	result = (u32)-1;

	SysInfo = &(((struct ms_bootblock_page0 *)PageData)->sysinf);

	if ((SysInfo->bMsClass != MS_SYSINF_MSCLASS_TYPE_1) ||
		(be16_to_cpu(SysInfo->wPageSize) != MS_SYSINF_PAGE_SIZE) ||
		((SysInfo->bSecuritySupport & MS_SYSINF_SECURITY) == MS_SYSINF_SECURITY_SUPPORT) ||
		(SysInfo->bReserved1 != MS_SYSINF_RESERVED1) ||
		(SysInfo->bReserved2 != MS_SYSINF_RESERVED2) ||
		(SysInfo->bFormatType != MS_SYSINF_FORMAT_FAT) ||
		(SysInfo->bUsage != MS_SYSINF_USAGE_GENERAL))
		goto exit;
		/* */
	switch (info->MS_Lib.cardType = SysInfo->bCardType) {
	case MS_SYSINF_CARDTYPE_RDONLY:
		ms_lib_ctrl_set(info, MS_LIB_CTRL_RDONLY);
		break;
	case MS_SYSINF_CARDTYPE_RDWR:
		ms_lib_ctrl_reset(info, MS_LIB_CTRL_RDONLY);
		break;
	case MS_SYSINF_CARDTYPE_HYBRID:
	default:
		goto exit;
	}

	info->MS_Lib.blockSize = be16_to_cpu(SysInfo->wBlockSize);
	info->MS_Lib.NumberOfPhyBlock = be16_to_cpu(SysInfo->wBlockNumber);
	info->MS_Lib.NumberOfLogBlock = be16_to_cpu(SysInfo->wTotalBlockNumber)-2;
	info->MS_Lib.PagesPerBlock = info->MS_Lib.blockSize * SIZE_OF_KIRO / MS_BYTES_PER_PAGE;
	info->MS_Lib.NumberOfSegment = info->MS_Lib.NumberOfPhyBlock / MS_PHYSICAL_BLOCKS_PER_SEGMENT;
	info->MS_Model = be16_to_cpu(SysInfo->wMemorySize);

	/*Allocate to all number of logicalblock and physicalblock */
	if (ms_lib_alloc_logicalmap(us))
		goto exit;

	/* Mark the book block */
	ms_lib_set_bootblockmark(us, PhyBlock);

	SysEntry = &(((struct ms_bootblock_page0 *)PageData)->sysent);

	for (i = 0; i < MS_NUMBER_OF_SYSTEM_ENTRY; i++) {
		u32  EntryOffset, EntrySize;

		EntryOffset = be32_to_cpu(SysEntry->entry[i].dwStart);

		if (EntryOffset == 0xffffff)
			continue;
		EntrySize = be32_to_cpu(SysEntry->entry[i].dwSize);

		if (EntrySize == 0)
			continue;

		if (EntryOffset + MS_BYTES_PER_PAGE + EntrySize > info->MS_Lib.blockSize * (u32)SIZE_OF_KIRO)
			continue;

		if (i == 0) {
			u8 PrevPageNumber = 0;
			u16 phyblk;

			if (SysEntry->entry[i].bType != MS_SYSENT_TYPE_INVALID_BLOCK)
				goto exit;

			while (EntrySize > 0) {

				PageNumber = (u8)(EntryOffset / MS_BYTES_PER_PAGE + 1);
				if (PageNumber != PrevPageNumber) {
					switch (ms_read_readpage(us, PhyBlock, PageNumber, (u32 *)PageBuffer, &ExtraData)) {
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

				phyblk = be16_to_cpu(*(u16 *)(PageBuffer + (EntryOffset % MS_BYTES_PER_PAGE)));
				if (phyblk < 0x0fff)
					ms_lib_set_initialerrorblock(us, phyblk);

				EntryOffset += 2;
				EntrySize -= 2;
			}
		} else if (i == 1) {  /* CIS/IDI */
			struct ms_bootblock_idi *idi;

			if (SysEntry->entry[i].bType != MS_SYSENT_TYPE_CIS_IDI)
				goto exit;

			switch (ms_read_readpage(us, PhyBlock, (u8)(EntryOffset / MS_BYTES_PER_PAGE + 1), (u32 *)PageBuffer, &ExtraData)) {
			case MS_STATUS_SUCCESS:
				break;
			case MS_STATUS_WRITE_PROTECT:
			case MS_ERROR_FLASH_READ:
			case MS_STATUS_ERROR:
			default:
				goto exit;
			}

			idi = &((struct ms_bootblock_cis_idi *)(PageBuffer + (EntryOffset % MS_BYTES_PER_PAGE)))->idi.idi;
			if (le16_to_cpu(idi->wIDIgeneralConfiguration) != MS_IDI_GENERAL_CONF)
				goto exit;

			info->MS_Lib.BytesPerSector = le16_to_cpu(idi->wIDIbytesPerSector);
			if (info->MS_Lib.BytesPerSector != MS_BYTES_PER_PAGE)
				goto exit;
		}
	} /* End for .. */

	result = 0;

exit:
	if (result)
		ms_lib_free_logicalmap(us);

	kfree(PageBuffer);

	result = 0;
	return result;
}

static void ms_lib_free_writebuf(struct us_data *us)
{
	struct ene_ub6250_info *info = (struct ene_ub6250_info *) us->extra;
	info->MS_Lib.wrtblk = (u16)-1; /* set to -1 */

	/* memset((fdoExt)->MS_Lib.pagemap, 0, sizeof((fdoExt)->MS_Lib.pagemap)) */

	ms_lib_clear_pagemap(info); /* (pdx)->MS_Lib.pagemap memset 0 in ms.h */

	if (info->MS_Lib.blkpag) {
		kfree(info->MS_Lib.blkpag);  /* Arnold test ... */
		info->MS_Lib.blkpag = NULL;
	}

	if (info->MS_Lib.blkext) {
		kfree(info->MS_Lib.blkext);  /* Arnold test ... */
		info->MS_Lib.blkext = NULL;
	}
}


static void ms_lib_free_allocatedarea(struct us_data *us)
{
	struct ene_ub6250_info *info = (struct ene_ub6250_info *) us->extra;

	ms_lib_free_writebuf(us); /* Free MS_Lib.pagemap */
	ms_lib_free_logicalmap(us); /* kfree MS_Lib.Phy2LogMap and MS_Lib.Log2PhyMap */

	/* set struct us point flag to 0 */
	info->MS_Lib.flags = 0;
	info->MS_Lib.BytesPerSector = 0;
	info->MS_Lib.SectorsPerCylinder = 0;

	info->MS_Lib.cardType = 0;
	info->MS_Lib.blockSize = 0;
	info->MS_Lib.PagesPerBlock = 0;

	info->MS_Lib.NumberOfPhyBlock = 0;
	info->MS_Lib.NumberOfLogBlock = 0;
}


static int ms_lib_alloc_writebuf(struct us_data *us)
{
	struct ene_ub6250_info *info = (struct ene_ub6250_info *) us->extra;

	info->MS_Lib.wrtblk = (u16)-1;

	info->MS_Lib.blkpag = kmalloc_array(info->MS_Lib.PagesPerBlock,
					    info->MS_Lib.BytesPerSector,
					    GFP_KERNEL);
	info->MS_Lib.blkext = kmalloc_array(info->MS_Lib.PagesPerBlock,
					    sizeof(struct ms_lib_type_extdat),
					    GFP_KERNEL);

	if ((info->MS_Lib.blkpag == NULL) || (info->MS_Lib.blkext == NULL)) {
		ms_lib_free_writebuf(us);
		return (u32)-1;
	}

	ms_lib_clear_writebuf(us);

	return 0;
}

static int ms_lib_force_setlogical_pair(struct us_data *us, u16 logblk, u16 phyblk)
{
	struct ene_ub6250_info *info = (struct ene_ub6250_info *) us->extra;

	if (logblk == MS_LB_NOT_USED)
		return 0;

	if ((logblk >= info->MS_Lib.NumberOfLogBlock) ||
		(phyblk >= info->MS_Lib.NumberOfPhyBlock))
		return (u32)-1;

	info->MS_Lib.Phy2LogMap[phyblk] = logblk;
	info->MS_Lib.Log2PhyMap[logblk] = phyblk;

	return 0;
}

static int ms_read_copyblock(struct us_data *us, u16 oldphy, u16 newphy,
			u16 PhyBlockAddr, u8 PageNum, unsigned char *buf, u16 len)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	int result;

	result = ene_load_bincode(us, MS_RW_PATTERN);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength = 0x200*len;
	bcb->Flags = 0x00;
	bcb->CDB[0] = 0xF0;
	bcb->CDB[1] = 0x08;
	bcb->CDB[4] = (unsigned char)(oldphy);
	bcb->CDB[3] = (unsigned char)(oldphy>>8);
	bcb->CDB[2] = 0; /* (BYTE)(oldphy>>16) */
	bcb->CDB[7] = (unsigned char)(newphy);
	bcb->CDB[6] = (unsigned char)(newphy>>8);
	bcb->CDB[5] = 0; /* (BYTE)(newphy>>16) */
	bcb->CDB[9] = (unsigned char)(PhyBlockAddr);
	bcb->CDB[8] = (unsigned char)(PhyBlockAddr>>8);
	bcb->CDB[10] = PageNum;

	result = ene_send_scsi_cmd(us, FDIR_WRITE, buf, 0);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	return USB_STOR_TRANSPORT_GOOD;
}

static int ms_read_eraseblock(struct us_data *us, u32 PhyBlockAddr)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	int result;
	u32 bn = PhyBlockAddr;

	result = ene_load_bincode(us, MS_RW_PATTERN);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength = 0x200;
	bcb->Flags = US_BULK_FLAG_IN;
	bcb->CDB[0] = 0xF2;
	bcb->CDB[1] = 0x06;
	bcb->CDB[4] = (unsigned char)(bn);
	bcb->CDB[3] = (unsigned char)(bn>>8);
	bcb->CDB[2] = (unsigned char)(bn>>16);

	result = ene_send_scsi_cmd(us, FDIR_READ, NULL, 0);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	return USB_STOR_TRANSPORT_GOOD;
}

static int ms_lib_check_disableblock(struct us_data *us, u16 PhyBlock)
{
	unsigned char *PageBuf = NULL;
	u16 result = MS_STATUS_SUCCESS;
	u16 blk, index = 0;
	struct ms_lib_type_extdat extdat;
	struct ene_ub6250_info *info = (struct ene_ub6250_info *) us->extra;

	PageBuf = kmalloc(MS_BYTES_PER_PAGE, GFP_KERNEL);
	if (PageBuf == NULL) {
		result = MS_NO_MEMORY_ERROR;
		goto exit;
	}

	ms_read_readpage(us, PhyBlock, 1, (u32 *)PageBuf, &extdat);
	do {
		blk = be16_to_cpu(PageBuf[index]);
		if (blk == MS_LB_NOT_USED)
			break;
		if (blk == info->MS_Lib.Log2PhyMap[0]) {
			result = MS_ERROR_FLASH_READ;
			break;
		}
		index++;
	} while (1);

exit:
	kfree(PageBuf);
	return result;
}

static int ms_lib_setacquired_errorblock(struct us_data *us, u16 phyblk)
{
	u16 log;
	struct ene_ub6250_info *info = (struct ene_ub6250_info *) us->extra;

	if (phyblk >= info->MS_Lib.NumberOfPhyBlock)
		return (u32)-1;

	log = info->MS_Lib.Phy2LogMap[phyblk];

	if (log < info->MS_Lib.NumberOfLogBlock)
		info->MS_Lib.Log2PhyMap[log] = MS_LB_NOT_USED;

	if (info->MS_Lib.Phy2LogMap[phyblk] != MS_LB_INITIAL_ERROR)
		info->MS_Lib.Phy2LogMap[phyblk] = MS_LB_ACQUIRED_ERROR;

	return 0;
}

static int ms_lib_overwrite_extra(struct us_data *us, u32 PhyBlockAddr,
				u8 PageNum, u8 OverwriteFlag)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	int result;

	result = ene_load_bincode(us, MS_RW_PATTERN);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength = 0x4;
	bcb->Flags = US_BULK_FLAG_IN;
	bcb->CDB[0] = 0xF2;
	bcb->CDB[1] = 0x05;
	bcb->CDB[5] = (unsigned char)(PageNum);
	bcb->CDB[4] = (unsigned char)(PhyBlockAddr);
	bcb->CDB[3] = (unsigned char)(PhyBlockAddr>>8);
	bcb->CDB[2] = (unsigned char)(PhyBlockAddr>>16);
	bcb->CDB[6] = OverwriteFlag;
	bcb->CDB[7] = 0xFF;
	bcb->CDB[8] = 0xFF;
	bcb->CDB[9] = 0xFF;

	result = ene_send_scsi_cmd(us, FDIR_READ, NULL, 0);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	return USB_STOR_TRANSPORT_GOOD;
}

static int ms_lib_error_phyblock(struct us_data *us, u16 phyblk)
{
	struct ene_ub6250_info *info = (struct ene_ub6250_info *) us->extra;

	if (phyblk >= info->MS_Lib.NumberOfPhyBlock)
		return MS_STATUS_ERROR;

	ms_lib_setacquired_errorblock(us, phyblk);

	if (ms_lib_iswritable(info))
		return ms_lib_overwrite_extra(us, phyblk, 0, (u8)(~MS_REG_OVR_BKST & BYTE_MASK));

	return MS_STATUS_SUCCESS;
}

static int ms_lib_erase_phyblock(struct us_data *us, u16 phyblk)
{
	u16 log;
	struct ene_ub6250_info *info = (struct ene_ub6250_info *) us->extra;

	if (phyblk >= info->MS_Lib.NumberOfPhyBlock)
		return MS_STATUS_ERROR;

	log = info->MS_Lib.Phy2LogMap[phyblk];

	if (log < info->MS_Lib.NumberOfLogBlock)
		info->MS_Lib.Log2PhyMap[log] = MS_LB_NOT_USED;

	info->MS_Lib.Phy2LogMap[phyblk] = MS_LB_NOT_USED;

	if (ms_lib_iswritable(info)) {
		switch (ms_read_eraseblock(us, phyblk)) {
		case MS_STATUS_SUCCESS:
			info->MS_Lib.Phy2LogMap[phyblk] = MS_LB_NOT_USED_ERASED;
			return MS_STATUS_SUCCESS;
		case MS_ERROR_FLASH_ERASE:
		case MS_STATUS_INT_ERROR:
			ms_lib_error_phyblock(us, phyblk);
			return MS_ERROR_FLASH_ERASE;
		case MS_STATUS_ERROR:
		default:
			ms_lib_ctrl_set(info, MS_LIB_CTRL_RDONLY); /* MS_LibCtrlSet will used by ENE_MSInit ,need check, and why us to info*/
			ms_lib_setacquired_errorblock(us, phyblk);
			return MS_STATUS_ERROR;
		}
	}

	ms_lib_setacquired_errorblock(us, phyblk);

	return MS_STATUS_SUCCESS;
}

static int ms_lib_read_extra(struct us_data *us, u32 PhyBlock,
				u8 PageNum, struct ms_lib_type_extdat *ExtraDat)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	struct ene_ub6250_info *info = (struct ene_ub6250_info *) us->extra;
	u8 *bbuf = info->bbuf;
	int result;

	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength = 0x4;
	bcb->Flags      = US_BULK_FLAG_IN;
	bcb->CDB[0]     = 0xF1;
	bcb->CDB[1]     = 0x03;
	bcb->CDB[5]     = (unsigned char)(PageNum);
	bcb->CDB[4]     = (unsigned char)(PhyBlock);
	bcb->CDB[3]     = (unsigned char)(PhyBlock>>8);
	bcb->CDB[2]     = (unsigned char)(PhyBlock>>16);
	bcb->CDB[6]     = 0x01;

	result = ene_send_scsi_cmd(us, FDIR_READ, bbuf, 0);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	ExtraDat->reserved = 0;
	ExtraDat->intr     = 0x80;  /* Not yet, waiting for fireware support */
	ExtraDat->status0  = 0x10;  /* Not yet, waiting for fireware support */
	ExtraDat->status1  = 0x00;  /* Not yet, waiting for fireware support */
	ExtraDat->ovrflg   = bbuf[0];
	ExtraDat->mngflg   = bbuf[1];
	ExtraDat->logadr   = memstick_logaddr(bbuf[2], bbuf[3]);

	return USB_STOR_TRANSPORT_GOOD;
}

static int ms_libsearch_block_from_physical(struct us_data *us, u16 phyblk)
{
	u16 blk;
	struct ms_lib_type_extdat extdat; /* need check */
	struct ene_ub6250_info *info = (struct ene_ub6250_info *) us->extra;


	if (phyblk >= info->MS_Lib.NumberOfPhyBlock)
		return MS_LB_ERROR;

	for (blk = phyblk + 1; blk != phyblk; blk++) {
		if ((blk & MS_PHYSICAL_BLOCKS_PER_SEGMENT_MASK) == 0)
			blk -= MS_PHYSICAL_BLOCKS_PER_SEGMENT;

		if (info->MS_Lib.Phy2LogMap[blk] == MS_LB_NOT_USED_ERASED) {
			return blk;
		} else if (info->MS_Lib.Phy2LogMap[blk] == MS_LB_NOT_USED) {
			switch (ms_lib_read_extra(us, blk, 0, &extdat)) {
			case MS_STATUS_SUCCESS:
			case MS_STATUS_SUCCESS_WITH_ECC:
				break;
			case MS_NOCARD_ERROR:
				return MS_NOCARD_ERROR;
			case MS_STATUS_INT_ERROR:
				return MS_LB_ERROR;
			case MS_ERROR_FLASH_READ:
			default:
				ms_lib_setacquired_errorblock(us, blk);
				continue;
			} /* End switch */

			if ((extdat.ovrflg & MS_REG_OVR_BKST) != MS_REG_OVR_BKST_OK) {
				ms_lib_setacquired_errorblock(us, blk);
				continue;
			}

			switch (ms_lib_erase_phyblock(us, blk)) {
			case MS_STATUS_SUCCESS:
				return blk;
			case MS_STATUS_ERROR:
				return MS_LB_ERROR;
			case MS_ERROR_FLASH_ERASE:
			default:
				ms_lib_error_phyblock(us, blk);
				break;
			}
		}
	} /* End for */

	return MS_LB_ERROR;
}
static int ms_libsearch_block_from_logical(struct us_data *us, u16 logblk)
{
	u16 phyblk;
	struct ene_ub6250_info *info = (struct ene_ub6250_info *) us->extra;

	phyblk = ms_libconv_to_physical(info, logblk);
	if (phyblk >= MS_LB_ERROR) {
		if (logblk >= info->MS_Lib.NumberOfLogBlock)
			return MS_LB_ERROR;

		phyblk = (logblk + MS_NUMBER_OF_BOOT_BLOCK) / MS_LOGICAL_BLOCKS_PER_SEGMENT;
		phyblk *= MS_PHYSICAL_BLOCKS_PER_SEGMENT;
		phyblk += MS_PHYSICAL_BLOCKS_PER_SEGMENT - 1;
	}

	return ms_libsearch_block_from_physical(us, phyblk);
}

static int ms_scsi_test_unit_ready(struct us_data *us, struct scsi_cmnd *srb)
{
	struct ene_ub6250_info *info = (struct ene_ub6250_info *)(us->extra);

	/* pr_info("MS_SCSI_Test_Unit_Ready\n"); */
	if ((info->MS_Status & MS_Insert) && (info->MS_Status & MS_Ready)) {
		return USB_STOR_TRANSPORT_GOOD;
	} else {
		ene_ms_init(us);
		return USB_STOR_TRANSPORT_GOOD;
	}

	return USB_STOR_TRANSPORT_GOOD;
}

static int ms_scsi_mode_sense(struct us_data *us, struct scsi_cmnd *srb)
{
	struct ene_ub6250_info *info = (struct ene_ub6250_info *) us->extra;
	unsigned char mediaNoWP[12] = {
		0x0b, 0x00, 0x00, 0x08, 0x00, 0x00,
		0x71, 0xc0, 0x00, 0x00, 0x02, 0x00 };
	unsigned char mediaWP[12]   = {
		0x0b, 0x00, 0x80, 0x08, 0x00, 0x00,
		0x71, 0xc0, 0x00, 0x00, 0x02, 0x00 };

	if (info->MS_Status & MS_WtP)
		usb_stor_set_xfer_buf(mediaWP, 12, srb);
	else
		usb_stor_set_xfer_buf(mediaNoWP, 12, srb);

	return USB_STOR_TRANSPORT_GOOD;
}

static int ms_scsi_read_capacity(struct us_data *us, struct scsi_cmnd *srb)
{
	u32   bl_num;
	u16    bl_len;
	unsigned int offset = 0;
	unsigned char    buf[8];
	struct scatterlist *sg = NULL;
	struct ene_ub6250_info *info = (struct ene_ub6250_info *) us->extra;

	usb_stor_dbg(us, "ms_scsi_read_capacity\n");
	bl_len = 0x200;
	if (info->MS_Status & MS_IsMSPro)
		bl_num = info->MSP_TotalBlock - 1;
	else
		bl_num = info->MS_Lib.NumberOfLogBlock * info->MS_Lib.blockSize * 2 - 1;

	info->bl_num = bl_num;
	usb_stor_dbg(us, "bl_len = %x\n", bl_len);
	usb_stor_dbg(us, "bl_num = %x\n", bl_num);

	/*srb->request_bufflen = 8; */
	buf[0] = (bl_num >> 24) & 0xff;
	buf[1] = (bl_num >> 16) & 0xff;
	buf[2] = (bl_num >> 8) & 0xff;
	buf[3] = (bl_num >> 0) & 0xff;
	buf[4] = (bl_len >> 24) & 0xff;
	buf[5] = (bl_len >> 16) & 0xff;
	buf[6] = (bl_len >> 8) & 0xff;
	buf[7] = (bl_len >> 0) & 0xff;

	usb_stor_access_xfer_buf(buf, 8, srb, &sg, &offset, TO_XFER_BUF);

	return USB_STOR_TRANSPORT_GOOD;
}

static void ms_lib_phy_to_log_range(u16 PhyBlock, u16 *LogStart, u16 *LogEnde)
{
	PhyBlock /= MS_PHYSICAL_BLOCKS_PER_SEGMENT;

	if (PhyBlock) {
		*LogStart = MS_LOGICAL_BLOCKS_IN_1ST_SEGMENT + (PhyBlock - 1) * MS_LOGICAL_BLOCKS_PER_SEGMENT;/*496*/
		*LogEnde = *LogStart + MS_LOGICAL_BLOCKS_PER_SEGMENT;/*496*/
	} else {
		*LogStart = 0;
		*LogEnde = MS_LOGICAL_BLOCKS_IN_1ST_SEGMENT;/*494*/
	}
}

static int ms_lib_read_extrablock(struct us_data *us, u32 PhyBlock,
	u8 PageNum, u8 blen, void *buf)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	int     result;

	/* Read Extra Data */
	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength = 0x4 * blen;
	bcb->Flags      = US_BULK_FLAG_IN;
	bcb->CDB[0]     = 0xF1;
	bcb->CDB[1]     = 0x03;
	bcb->CDB[5]     = (unsigned char)(PageNum);
	bcb->CDB[4]     = (unsigned char)(PhyBlock);
	bcb->CDB[3]     = (unsigned char)(PhyBlock>>8);
	bcb->CDB[2]     = (unsigned char)(PhyBlock>>16);
	bcb->CDB[6]     = blen;

	result = ene_send_scsi_cmd(us, FDIR_READ, buf, 0);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	return USB_STOR_TRANSPORT_GOOD;
}

static int ms_lib_scan_logicalblocknumber(struct us_data *us, u16 btBlk1st)
{
	u16 PhyBlock, newblk, i;
	u16 LogStart, LogEnde;
	struct ms_lib_type_extdat extdat;
	u32 count = 0, index = 0;
	struct ene_ub6250_info *info = (struct ene_ub6250_info *) us->extra;
	u8 *bbuf = info->bbuf;

	for (PhyBlock = 0; PhyBlock < info->MS_Lib.NumberOfPhyBlock;) {
		ms_lib_phy_to_log_range(PhyBlock, &LogStart, &LogEnde);

		for (i = 0; i < MS_PHYSICAL_BLOCKS_PER_SEGMENT; i++, PhyBlock++) {
			switch (ms_libconv_to_logical(info, PhyBlock)) {
			case MS_STATUS_ERROR:
				continue;
			default:
				break;
			}

			if (count == PhyBlock) {
				ms_lib_read_extrablock(us, PhyBlock, 0, 0x80,
						bbuf);
				count += 0x80;
			}
			index = (PhyBlock % 0x80) * 4;

			extdat.ovrflg = bbuf[index];
			extdat.mngflg = bbuf[index+1];
			extdat.logadr = memstick_logaddr(bbuf[index+2],
					bbuf[index+3]);

			if ((extdat.ovrflg & MS_REG_OVR_BKST) != MS_REG_OVR_BKST_OK) {
				ms_lib_setacquired_errorblock(us, PhyBlock);
				continue;
			}

			if ((extdat.mngflg & MS_REG_MNG_ATFLG) == MS_REG_MNG_ATFLG_ATTBL) {
				ms_lib_erase_phyblock(us, PhyBlock);
				continue;
			}

			if (extdat.logadr != MS_LB_NOT_USED) {
				if ((extdat.logadr < LogStart) || (LogEnde <= extdat.logadr)) {
					ms_lib_erase_phyblock(us, PhyBlock);
					continue;
				}

				newblk = ms_libconv_to_physical(info, extdat.logadr);

				if (newblk != MS_LB_NOT_USED) {
					if (extdat.logadr == 0) {
						ms_lib_set_logicalpair(us, extdat.logadr, PhyBlock);
						if (ms_lib_check_disableblock(us, btBlk1st)) {
							ms_lib_set_logicalpair(us, extdat.logadr, newblk);
							continue;
						}
					}

					ms_lib_read_extra(us, newblk, 0, &extdat);
					if ((extdat.ovrflg & MS_REG_OVR_UDST) == MS_REG_OVR_UDST_UPDATING) {
						ms_lib_erase_phyblock(us, PhyBlock);
						continue;
					} else {
						ms_lib_erase_phyblock(us, newblk);
					}
				}

				ms_lib_set_logicalpair(us, extdat.logadr, PhyBlock);
			}
		}
	} /* End for ... */

	return MS_STATUS_SUCCESS;
}


static int ms_scsi_read(struct us_data *us, struct scsi_cmnd *srb)
{
	int result;
	unsigned char *cdb = srb->cmnd;
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	struct ene_ub6250_info *info = (struct ene_ub6250_info *) us->extra;

	u32 bn = ((cdb[2] << 24) & 0xff000000) | ((cdb[3] << 16) & 0x00ff0000) |
		((cdb[4] << 8) & 0x0000ff00) | ((cdb[5] << 0) & 0x000000ff);
	u16 blen = ((cdb[7] << 8) & 0xff00) | ((cdb[8] << 0) & 0x00ff);
	u32 blenByte = blen * 0x200;

	if (bn > info->bl_num)
		return USB_STOR_TRANSPORT_ERROR;

	if (info->MS_Status & MS_IsMSPro) {
		result = ene_load_bincode(us, MSP_RW_PATTERN);
		if (result != USB_STOR_XFER_GOOD) {
			usb_stor_dbg(us, "Load MPS RW pattern Fail !!\n");
			return USB_STOR_TRANSPORT_ERROR;
		}

		/* set up the command wrapper */
		memset(bcb, 0, sizeof(struct bulk_cb_wrap));
		bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
		bcb->DataTransferLength = blenByte;
		bcb->Flags  = US_BULK_FLAG_IN;
		bcb->CDB[0] = 0xF1;
		bcb->CDB[1] = 0x02;
		bcb->CDB[5] = (unsigned char)(bn);
		bcb->CDB[4] = (unsigned char)(bn>>8);
		bcb->CDB[3] = (unsigned char)(bn>>16);
		bcb->CDB[2] = (unsigned char)(bn>>24);

		result = ene_send_scsi_cmd(us, FDIR_READ, scsi_sglist(srb), 1);
	} else {
		void *buf;
		int offset = 0;
		u16 phyblk, logblk;
		u8 PageNum;
		u16 len;
		u32 blkno;

		buf = kmalloc(blenByte, GFP_KERNEL);
		if (buf == NULL)
			return USB_STOR_TRANSPORT_ERROR;

		result = ene_load_bincode(us, MS_RW_PATTERN);
		if (result != USB_STOR_XFER_GOOD) {
			pr_info("Load MS RW pattern Fail !!\n");
			result = USB_STOR_TRANSPORT_ERROR;
			goto exit;
		}

		logblk  = (u16)(bn / info->MS_Lib.PagesPerBlock);
		PageNum = (u8)(bn % info->MS_Lib.PagesPerBlock);

		while (1) {
			if (blen > (info->MS_Lib.PagesPerBlock-PageNum))
				len = info->MS_Lib.PagesPerBlock-PageNum;
			else
				len = blen;

			phyblk = ms_libconv_to_physical(info, logblk);
			blkno  = phyblk * 0x20 + PageNum;

			/* set up the command wrapper */
			memset(bcb, 0, sizeof(struct bulk_cb_wrap));
			bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
			bcb->DataTransferLength = 0x200 * len;
			bcb->Flags  = US_BULK_FLAG_IN;
			bcb->CDB[0] = 0xF1;
			bcb->CDB[1] = 0x02;
			bcb->CDB[5] = (unsigned char)(blkno);
			bcb->CDB[4] = (unsigned char)(blkno>>8);
			bcb->CDB[3] = (unsigned char)(blkno>>16);
			bcb->CDB[2] = (unsigned char)(blkno>>24);

			result = ene_send_scsi_cmd(us, FDIR_READ, buf+offset, 0);
			if (result != USB_STOR_XFER_GOOD) {
				pr_info("MS_SCSI_Read --- result = %x\n", result);
				result = USB_STOR_TRANSPORT_ERROR;
				goto exit;
			}

			blen -= len;
			if (blen <= 0)
				break;
			logblk++;
			PageNum = 0;
			offset += MS_BYTES_PER_PAGE*len;
		}
		usb_stor_set_xfer_buf(buf, blenByte, srb);
exit:
		kfree(buf);
	}
	return result;
}

static int ms_scsi_write(struct us_data *us, struct scsi_cmnd *srb)
{
	int result;
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	unsigned char *cdb = srb->cmnd;
	struct ene_ub6250_info *info = (struct ene_ub6250_info *) us->extra;

	u32 bn = ((cdb[2] << 24) & 0xff000000) |
			((cdb[3] << 16) & 0x00ff0000) |
			((cdb[4] << 8) & 0x0000ff00) |
			((cdb[5] << 0) & 0x000000ff);
	u16 blen = ((cdb[7] << 8) & 0xff00) | ((cdb[8] << 0) & 0x00ff);
	u32 blenByte = blen * 0x200;

	if (bn > info->bl_num)
		return USB_STOR_TRANSPORT_ERROR;

	if (info->MS_Status & MS_IsMSPro) {
		result = ene_load_bincode(us, MSP_RW_PATTERN);
		if (result != USB_STOR_XFER_GOOD) {
			pr_info("Load MSP RW pattern Fail !!\n");
			return USB_STOR_TRANSPORT_ERROR;
		}

		/* set up the command wrapper */
		memset(bcb, 0, sizeof(struct bulk_cb_wrap));
		bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
		bcb->DataTransferLength = blenByte;
		bcb->Flags  = 0x00;
		bcb->CDB[0] = 0xF0;
		bcb->CDB[1] = 0x04;
		bcb->CDB[5] = (unsigned char)(bn);
		bcb->CDB[4] = (unsigned char)(bn>>8);
		bcb->CDB[3] = (unsigned char)(bn>>16);
		bcb->CDB[2] = (unsigned char)(bn>>24);

		result = ene_send_scsi_cmd(us, FDIR_WRITE, scsi_sglist(srb), 1);
	} else {
		void *buf;
		int offset = 0;
		u16 PhyBlockAddr;
		u8 PageNum;
		u16 len, oldphy, newphy;

		buf = kmalloc(blenByte, GFP_KERNEL);
		if (buf == NULL)
			return USB_STOR_TRANSPORT_ERROR;
		usb_stor_set_xfer_buf(buf, blenByte, srb);

		result = ene_load_bincode(us, MS_RW_PATTERN);
		if (result != USB_STOR_XFER_GOOD) {
			pr_info("Load MS RW pattern Fail !!\n");
			result = USB_STOR_TRANSPORT_ERROR;
			goto exit;
		}

		PhyBlockAddr = (u16)(bn / info->MS_Lib.PagesPerBlock);
		PageNum      = (u8)(bn % info->MS_Lib.PagesPerBlock);

		while (1) {
			if (blen > (info->MS_Lib.PagesPerBlock-PageNum))
				len = info->MS_Lib.PagesPerBlock-PageNum;
			else
				len = blen;

			oldphy = ms_libconv_to_physical(info, PhyBlockAddr); /* need check us <-> info */
			newphy = ms_libsearch_block_from_logical(us, PhyBlockAddr);

			result = ms_read_copyblock(us, oldphy, newphy, PhyBlockAddr, PageNum, buf+offset, len);

			if (result != USB_STOR_XFER_GOOD) {
				pr_info("MS_SCSI_Write --- result = %x\n", result);
				result =  USB_STOR_TRANSPORT_ERROR;
				goto exit;
			}

			info->MS_Lib.Phy2LogMap[oldphy] = MS_LB_NOT_USED_ERASED;
			ms_lib_force_setlogical_pair(us, PhyBlockAddr, newphy);

			blen -= len;
			if (blen <= 0)
				break;
			PhyBlockAddr++;
			PageNum = 0;
			offset += MS_BYTES_PER_PAGE*len;
		}
exit:
		kfree(buf);
	}
	return result;
}

/*
 * ENE MS Card
 */

static int ene_get_card_type(struct us_data *us, u16 index, void *buf)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	int result;

	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength	= 0x01;
	bcb->Flags			= US_BULK_FLAG_IN;
	bcb->CDB[0]			= 0xED;
	bcb->CDB[2]			= (unsigned char)(index>>8);
	bcb->CDB[3]			= (unsigned char)index;

	result = ene_send_scsi_cmd(us, FDIR_READ, buf, 0);
	return result;
}

static int ene_get_card_status(struct us_data *us, u8 *buf)
{
	u16 tmpreg;
	u32 reg4b;
	struct ene_ub6250_info *info = (struct ene_ub6250_info *) us->extra;

	/*usb_stor_dbg(us, "transport --- ENE_ReadSDReg\n");*/
	reg4b = *(u32 *)&buf[0x18];
	info->SD_READ_BL_LEN = (u8)((reg4b >> 8) & 0x0f);

	tmpreg = (u16) reg4b;
	reg4b = *(u32 *)(&buf[0x14]);
	if ((info->SD_Status & SD_HiCapacity) && !(info->SD_Status & SD_IsMMC))
		info->HC_C_SIZE = (reg4b >> 8) & 0x3fffff;

	info->SD_C_SIZE = ((tmpreg & 0x03) << 10) | (u16)(reg4b >> 22);
	info->SD_C_SIZE_MULT = (u8)(reg4b >> 7)  & 0x07;
	if ((info->SD_Status & SD_HiCapacity) && (info->SD_Status & SD_IsMMC))
		info->HC_C_SIZE = *(u32 *)(&buf[0x100]);

	if (info->SD_READ_BL_LEN > SD_BLOCK_LEN) {
		info->SD_Block_Mult = 1 << (info->SD_READ_BL_LEN-SD_BLOCK_LEN);
		info->SD_READ_BL_LEN = SD_BLOCK_LEN;
	} else {
		info->SD_Block_Mult = 1;
	}

	return USB_STOR_TRANSPORT_GOOD;
}

static int ene_load_bincode(struct us_data *us, unsigned char flag)
{
	int err;
	char *fw_name = NULL;
	unsigned char *buf = NULL;
	const struct firmware *sd_fw = NULL;
	int result = USB_STOR_TRANSPORT_ERROR;
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	struct ene_ub6250_info *info = (struct ene_ub6250_info *) us->extra;

	if (info->BIN_FLAG == flag)
		return USB_STOR_TRANSPORT_GOOD;

	switch (flag) {
	/* For SD */
	case SD_INIT1_PATTERN:
		usb_stor_dbg(us, "SD_INIT1_PATTERN\n");
		fw_name = SD_INIT1_FIRMWARE;
		break;
	case SD_INIT2_PATTERN:
		usb_stor_dbg(us, "SD_INIT2_PATTERN\n");
		fw_name = SD_INIT2_FIRMWARE;
		break;
	case SD_RW_PATTERN:
		usb_stor_dbg(us, "SD_RW_PATTERN\n");
		fw_name = SD_RW_FIRMWARE;
		break;
	/* For MS */
	case MS_INIT_PATTERN:
		usb_stor_dbg(us, "MS_INIT_PATTERN\n");
		fw_name = MS_INIT_FIRMWARE;
		break;
	case MSP_RW_PATTERN:
		usb_stor_dbg(us, "MSP_RW_PATTERN\n");
		fw_name = MSP_RW_FIRMWARE;
		break;
	case MS_RW_PATTERN:
		usb_stor_dbg(us, "MS_RW_PATTERN\n");
		fw_name = MS_RW_FIRMWARE;
		break;
	default:
		usb_stor_dbg(us, "----------- Unknown PATTERN ----------\n");
		goto nofw;
	}

	err = request_firmware(&sd_fw, fw_name, &us->pusb_dev->dev);
	if (err) {
		usb_stor_dbg(us, "load firmware %s failed\n", fw_name);
		goto nofw;
	}
	buf = kmemdup(sd_fw->data, sd_fw->size, GFP_KERNEL);
	if (buf == NULL)
		goto nofw;

	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength = sd_fw->size;
	bcb->Flags = 0x00;
	bcb->CDB[0] = 0xEF;

	result = ene_send_scsi_cmd(us, FDIR_WRITE, buf, 0);
	if (us->srb != NULL)
		scsi_set_resid(us->srb, 0);
	info->BIN_FLAG = flag;
	kfree(buf);

nofw:
	release_firmware(sd_fw);
	return result;
}

static int ms_card_init(struct us_data *us)
{
	u32 result;
	u16 TmpBlock;
	unsigned char *PageBuffer0 = NULL, *PageBuffer1 = NULL;
	struct ms_lib_type_extdat extdat;
	u16 btBlk1st, btBlk2nd;
	u32 btBlk1stErred;
	struct ene_ub6250_info *info = (struct ene_ub6250_info *) us->extra;

	printk(KERN_INFO "MS_CardInit start\n");

	ms_lib_free_allocatedarea(us); /* Clean buffer and set struct us_data flag to 0 */

	/* get two PageBuffer */
	PageBuffer0 = kmalloc(MS_BYTES_PER_PAGE, GFP_KERNEL);
	PageBuffer1 = kmalloc(MS_BYTES_PER_PAGE, GFP_KERNEL);
	if ((PageBuffer0 == NULL) || (PageBuffer1 == NULL)) {
		result = MS_NO_MEMORY_ERROR;
		goto exit;
	}

	btBlk1st = btBlk2nd = MS_LB_NOT_USED;
	btBlk1stErred = 0;

	for (TmpBlock = 0; TmpBlock < MS_MAX_INITIAL_ERROR_BLOCKS+2; TmpBlock++) {

		switch (ms_read_readpage(us, TmpBlock, 0, (u32 *)PageBuffer0, &extdat)) {
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
			(be16_to_cpu(((struct ms_bootblock_page0 *)PageBuffer0)->header.wBlockID) != MS_BOOT_BLOCK_ID) ||
			(be16_to_cpu(((struct ms_bootblock_page0 *)PageBuffer0)->header.wFormatVersion) != MS_BOOT_BLOCK_FORMAT_VERSION) ||
			(((struct ms_bootblock_page0 *)PageBuffer0)->header.bNumberOfDataEntry != MS_BOOT_BLOCK_DATA_ENTRIES))
				continue;

		if (btBlk1st != MS_LB_NOT_USED) {
			btBlk2nd = TmpBlock;
			break;
		}

		btBlk1st = TmpBlock;
		memcpy(PageBuffer1, PageBuffer0, MS_BYTES_PER_PAGE);
		if (extdat.status1 & (MS_REG_ST1_DTER | MS_REG_ST1_EXER | MS_REG_ST1_FGER))
			btBlk1stErred = 1;
	}

	if (btBlk1st == MS_LB_NOT_USED) {
		result = MS_STATUS_ERROR;
		goto exit;
	}

	/* write protect */
	if ((extdat.status0 & MS_REG_ST0_WP) == MS_REG_ST0_WP_ON)
		ms_lib_ctrl_set(info, MS_LIB_CTRL_WRPROTECT);

	result = MS_STATUS_ERROR;
	/* 1st Boot Block */
	if (btBlk1stErred == 0)
		result = ms_lib_process_bootblock(us, btBlk1st, PageBuffer1);
		/* 1st */
	/* 2nd Boot Block */
	if (result && (btBlk2nd != MS_LB_NOT_USED))
		result = ms_lib_process_bootblock(us, btBlk2nd, PageBuffer0);

	if (result) {
		result = MS_STATUS_ERROR;
		goto exit;
	}

	for (TmpBlock = 0; TmpBlock < btBlk1st; TmpBlock++)
		info->MS_Lib.Phy2LogMap[TmpBlock] = MS_LB_INITIAL_ERROR;

	info->MS_Lib.Phy2LogMap[btBlk1st] = MS_LB_BOOT_BLOCK;

	if (btBlk2nd != MS_LB_NOT_USED) {
		for (TmpBlock = btBlk1st + 1; TmpBlock < btBlk2nd; TmpBlock++)
			info->MS_Lib.Phy2LogMap[TmpBlock] = MS_LB_INITIAL_ERROR;

		info->MS_Lib.Phy2LogMap[btBlk2nd] = MS_LB_BOOT_BLOCK;
	}

	result = ms_lib_scan_logicalblocknumber(us, btBlk1st);
	if (result)
		goto exit;

	for (TmpBlock = MS_PHYSICAL_BLOCKS_PER_SEGMENT;
		TmpBlock < info->MS_Lib.NumberOfPhyBlock;
		TmpBlock += MS_PHYSICAL_BLOCKS_PER_SEGMENT) {
		if (ms_count_freeblock(us, TmpBlock) == 0) {
			ms_lib_ctrl_set(info, MS_LIB_CTRL_WRPROTECT);
			break;
		}
	}

	/* write */
	if (ms_lib_alloc_writebuf(us)) {
		result = MS_NO_MEMORY_ERROR;
		goto exit;
	}

	result = MS_STATUS_SUCCESS;

exit:
	kfree(PageBuffer1);
	kfree(PageBuffer0);

	printk(KERN_INFO "MS_CardInit end\n");
	return result;
}

static int ene_ms_init(struct us_data *us)
{
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	int result;
	u16 MSP_BlockSize, MSP_UserAreaBlocks;
	struct ene_ub6250_info *info = (struct ene_ub6250_info *) us->extra;
	u8 *bbuf = info->bbuf;
	unsigned int s;

	printk(KERN_INFO "transport --- ENE_MSInit\n");

	/* the same part to test ENE */

	result = ene_load_bincode(us, MS_INIT_PATTERN);
	if (result != USB_STOR_XFER_GOOD) {
		printk(KERN_ERR "Load MS Init Code Fail !!\n");
		return USB_STOR_TRANSPORT_ERROR;
	}

	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength = 0x200;
	bcb->Flags      = US_BULK_FLAG_IN;
	bcb->CDB[0]     = 0xF1;
	bcb->CDB[1]     = 0x01;

	result = ene_send_scsi_cmd(us, FDIR_READ, bbuf, 0);
	if (result != USB_STOR_XFER_GOOD) {
		printk(KERN_ERR "Execution MS Init Code Fail !!\n");
		return USB_STOR_TRANSPORT_ERROR;
	}
	/* the same part to test ENE */
	info->MS_Status = bbuf[0];

	s = info->MS_Status;
	if ((s & MS_Insert) && (s & MS_Ready)) {
		printk(KERN_INFO "Insert     = %x\n", !!(s & MS_Insert));
		printk(KERN_INFO "Ready      = %x\n", !!(s & MS_Ready));
		printk(KERN_INFO "IsMSPro    = %x\n", !!(s & MS_IsMSPro));
		printk(KERN_INFO "IsMSPHG    = %x\n", !!(s & MS_IsMSPHG));
		printk(KERN_INFO "WtP= %x\n", !!(s & MS_WtP));
		if (s & MS_IsMSPro) {
			MSP_BlockSize      = (bbuf[6] << 8) | bbuf[7];
			MSP_UserAreaBlocks = (bbuf[10] << 8) | bbuf[11];
			info->MSP_TotalBlock = MSP_BlockSize * MSP_UserAreaBlocks;
		} else {
			ms_card_init(us); /* Card is MS (to ms.c)*/
		}
		usb_stor_dbg(us, "MS Init Code OK !!\n");
	} else {
		usb_stor_dbg(us, "MS Card Not Ready --- %x\n", bbuf[0]);
		return USB_STOR_TRANSPORT_ERROR;
	}

	return USB_STOR_TRANSPORT_GOOD;
}

static int ene_sd_init(struct us_data *us)
{
	int result;
	struct bulk_cb_wrap *bcb = (struct bulk_cb_wrap *) us->iobuf;
	struct ene_ub6250_info *info = (struct ene_ub6250_info *) us->extra;
	u8 *bbuf = info->bbuf;

	usb_stor_dbg(us, "transport --- ENE_SDInit\n");
	/* SD Init Part-1 */
	result = ene_load_bincode(us, SD_INIT1_PATTERN);
	if (result != USB_STOR_XFER_GOOD) {
		usb_stor_dbg(us, "Load SD Init Code Part-1 Fail !!\n");
		return USB_STOR_TRANSPORT_ERROR;
	}

	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->Flags = US_BULK_FLAG_IN;
	bcb->CDB[0] = 0xF2;

	result = ene_send_scsi_cmd(us, FDIR_READ, NULL, 0);
	if (result != USB_STOR_XFER_GOOD) {
		usb_stor_dbg(us, "Execution SD Init Code Fail !!\n");
		return USB_STOR_TRANSPORT_ERROR;
	}

	/* SD Init Part-2 */
	result = ene_load_bincode(us, SD_INIT2_PATTERN);
	if (result != USB_STOR_XFER_GOOD) {
		usb_stor_dbg(us, "Load SD Init Code Part-2 Fail !!\n");
		return USB_STOR_TRANSPORT_ERROR;
	}

	memset(bcb, 0, sizeof(struct bulk_cb_wrap));
	bcb->Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb->DataTransferLength = 0x200;
	bcb->Flags              = US_BULK_FLAG_IN;
	bcb->CDB[0]             = 0xF1;

	result = ene_send_scsi_cmd(us, FDIR_READ, bbuf, 0);
	if (result != USB_STOR_XFER_GOOD) {
		usb_stor_dbg(us, "Execution SD Init Code Fail !!\n");
		return USB_STOR_TRANSPORT_ERROR;
	}

	info->SD_Status = bbuf[0];
	if ((info->SD_Status & SD_Insert) && (info->SD_Status & SD_Ready)) {
		unsigned int s = info->SD_Status;

		ene_get_card_status(us, bbuf);
		usb_stor_dbg(us, "Insert     = %x\n", !!(s & SD_Insert));
		usb_stor_dbg(us, "Ready      = %x\n", !!(s & SD_Ready));
		usb_stor_dbg(us, "IsMMC      = %x\n", !!(s & SD_IsMMC));
		usb_stor_dbg(us, "HiCapacity = %x\n", !!(s & SD_HiCapacity));
		usb_stor_dbg(us, "HiSpeed    = %x\n", !!(s & SD_HiSpeed));
		usb_stor_dbg(us, "WtP        = %x\n", !!(s & SD_WtP));
	} else {
		usb_stor_dbg(us, "SD Card Not Ready --- %x\n", bbuf[0]);
		return USB_STOR_TRANSPORT_ERROR;
	}
	return USB_STOR_TRANSPORT_GOOD;
}


static int ene_init(struct us_data *us)
{
	int result;
	u8  misc_reg03;
	struct ene_ub6250_info *info = (struct ene_ub6250_info *)(us->extra);
	u8 *bbuf = info->bbuf;

	result = ene_get_card_type(us, REG_CARD_STATUS, bbuf);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	misc_reg03 = bbuf[0];
	if (misc_reg03 & 0x01) {
		if (!(info->SD_Status & SD_Ready)) {
			result = ene_sd_init(us);
			if (result != USB_STOR_XFER_GOOD)
				return USB_STOR_TRANSPORT_ERROR;
		}
	}
	if (misc_reg03 & 0x02) {
		if (!(info->MS_Status & MS_Ready)) {
			result = ene_ms_init(us);
			if (result != USB_STOR_XFER_GOOD)
				return USB_STOR_TRANSPORT_ERROR;
		}
	}
	return result;
}

/*----- sd_scsi_irp() ---------*/
static int sd_scsi_irp(struct us_data *us, struct scsi_cmnd *srb)
{
	int    result;
	struct ene_ub6250_info *info = (struct ene_ub6250_info *)us->extra;

	switch (srb->cmnd[0]) {
	case TEST_UNIT_READY:
		result = sd_scsi_test_unit_ready(us, srb);
		break; /* 0x00 */
	case REQUEST_SENSE:
		result = do_scsi_request_sense(us, srb);
		break; /* 0x03 */
	case INQUIRY:
		result = do_scsi_inquiry(us, srb);
		break; /* 0x12 */
	case MODE_SENSE:
		result = sd_scsi_mode_sense(us, srb);
		break; /* 0x1A */
	/*
	case START_STOP:
		result = SD_SCSI_Start_Stop(us, srb);
		break; //0x1B
	*/
	case READ_CAPACITY:
		result = sd_scsi_read_capacity(us, srb);
		break; /* 0x25 */
	case READ_10:
		result = sd_scsi_read(us, srb);
		break; /* 0x28 */
	case WRITE_10:
		result = sd_scsi_write(us, srb);
		break; /* 0x2A */
	default:
		info->SrbStatus = SS_ILLEGAL_REQUEST;
		result = USB_STOR_TRANSPORT_FAILED;
		break;
	}
	if (result == USB_STOR_TRANSPORT_GOOD)
		info->SrbStatus = SS_SUCCESS;
	return result;
}

/*
 * ms_scsi_irp()
 */
static int ms_scsi_irp(struct us_data *us, struct scsi_cmnd *srb)
{
	int result;
	struct ene_ub6250_info *info = (struct ene_ub6250_info *)us->extra;

	switch (srb->cmnd[0]) {
	case TEST_UNIT_READY:
		result = ms_scsi_test_unit_ready(us, srb);
		break; /* 0x00 */
	case REQUEST_SENSE:
		result = do_scsi_request_sense(us, srb);
		break; /* 0x03 */
	case INQUIRY:
		result = do_scsi_inquiry(us, srb);
		break; /* 0x12 */
	case MODE_SENSE:
		result = ms_scsi_mode_sense(us, srb);
		break; /* 0x1A */
	case READ_CAPACITY:
		result = ms_scsi_read_capacity(us, srb);
		break; /* 0x25 */
	case READ_10:
		result = ms_scsi_read(us, srb);
		break; /* 0x28 */
	case WRITE_10:
		result = ms_scsi_write(us, srb);
		break;  /* 0x2A */
	default:
		info->SrbStatus = SS_ILLEGAL_REQUEST;
		result = USB_STOR_TRANSPORT_FAILED;
		break;
	}
	if (result == USB_STOR_TRANSPORT_GOOD)
		info->SrbStatus = SS_SUCCESS;
	return result;
}

static int ene_transport(struct scsi_cmnd *srb, struct us_data *us)
{
	int result = USB_STOR_XFER_GOOD;
	struct ene_ub6250_info *info = (struct ene_ub6250_info *)(us->extra);

	/*US_DEBUG(usb_stor_show_command(us, srb)); */
	scsi_set_resid(srb, 0);
	if (unlikely(!(info->SD_Status & SD_Ready) || (info->MS_Status & MS_Ready)))
		result = ene_init(us);
	if (result == USB_STOR_XFER_GOOD) {
		result = USB_STOR_TRANSPORT_ERROR;
		if (info->SD_Status & SD_Ready)
			result = sd_scsi_irp(us, srb);

		if (info->MS_Status & MS_Ready)
			result = ms_scsi_irp(us, srb);
	}
	return result;
}

static struct scsi_host_template ene_ub6250_host_template;

static int ene_ub6250_probe(struct usb_interface *intf,
			 const struct usb_device_id *id)
{
	int result;
	u8  misc_reg03;
	struct us_data *us;
	struct ene_ub6250_info *info;

	result = usb_stor_probe1(&us, intf, id,
		   (id - ene_ub6250_usb_ids) + ene_ub6250_unusual_dev_list,
		   &ene_ub6250_host_template);
	if (result)
		return result;

	/* FIXME: where should the code alloc extra buf ? */
	us->extra = kzalloc(sizeof(struct ene_ub6250_info), GFP_KERNEL);
	if (!us->extra)
		return -ENOMEM;
	us->extra_destructor = ene_ub6250_info_destructor;

	info = (struct ene_ub6250_info *)(us->extra);
	info->bbuf = kmalloc(512, GFP_KERNEL);
	if (!info->bbuf) {
		kfree(us->extra);
		return -ENOMEM;
	}

	us->transport_name = "ene_ub6250";
	us->transport = ene_transport;
	us->max_lun = 0;

	result = usb_stor_probe2(us);
	if (result)
		return result;

	/* probe card type */
	result = ene_get_card_type(us, REG_CARD_STATUS, info->bbuf);
	if (result != USB_STOR_XFER_GOOD) {
		usb_stor_disconnect(intf);
		return USB_STOR_TRANSPORT_ERROR;
	}

	misc_reg03 = info->bbuf[0];
	if (!(misc_reg03 & 0x01)) {
		pr_info("ums_eneub6250: This driver only supports SD/MS cards. "
			"It does not support SM cards.\n");
	}

	return result;
}


#ifdef CONFIG_PM

static int ene_ub6250_resume(struct usb_interface *iface)
{
	struct us_data *us = usb_get_intfdata(iface);
	struct ene_ub6250_info *info = (struct ene_ub6250_info *)(us->extra);

	mutex_lock(&us->dev_mutex);

	if (us->suspend_resume_hook)
		(us->suspend_resume_hook)(us, US_RESUME);

	mutex_unlock(&us->dev_mutex);

	info->Power_IsResum = true;
	/* info->SD_Status &= ~SD_Ready; */
	info->SD_Status = 0;
	info->MS_Status = 0;
	info->SM_Status = 0;

	return 0;
}

static int ene_ub6250_reset_resume(struct usb_interface *iface)
{
	struct us_data *us = usb_get_intfdata(iface);
	struct ene_ub6250_info *info = (struct ene_ub6250_info *)(us->extra);

	/* Report the reset to the SCSI core */
	usb_stor_reset_resume(iface);

	/*
	 * FIXME: Notify the subdrivers that they need to reinitialize
	 * the device
	 */
	info->Power_IsResum = true;
	/* info->SD_Status &= ~SD_Ready; */
	info->SD_Status = 0;
	info->MS_Status = 0;
	info->SM_Status = 0;

	return 0;
}

#else

#define ene_ub6250_resume		NULL
#define ene_ub6250_reset_resume		NULL

#endif

static struct usb_driver ene_ub6250_driver = {
	.name =		DRV_NAME,
	.probe =	ene_ub6250_probe,
	.disconnect =	usb_stor_disconnect,
	.suspend =	usb_stor_suspend,
	.resume =	ene_ub6250_resume,
	.reset_resume =	ene_ub6250_reset_resume,
	.pre_reset =	usb_stor_pre_reset,
	.post_reset =	usb_stor_post_reset,
	.id_table =	ene_ub6250_usb_ids,
	.soft_unbind =	1,
	.no_dynamic_id = 1,
};

module_usb_stor_driver(ene_ub6250_driver, ene_ub6250_host_template, DRV_NAME);
