/*
 *    Disk Array driver for HP Smart Array SAS controllers
 *    Copyright 2016 Microsemi Corporation
 *    Copyright 2014-2015 PMC-Sierra, Inc.
 *    Copyright 2000,2009-2015 Hewlett-Packard Development Company, L.P.
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; version 2 of the License.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *    NON INFRINGEMENT.  See the GNU General Public License for more details.
 *
 *    Questions/Comments/Bugfixes to esc.storagedev@microsemi.com
 *
 */
#ifndef HPSA_CMD_H
#define HPSA_CMD_H

/* general boundary defintions */
#define SENSEINFOBYTES          32 /* may vary between hbas */
#define SG_ENTRIES_IN_CMD	32 /* Max SG entries excluding chain blocks */
#define HPSA_SG_CHAIN		0x80000000
#define HPSA_SG_LAST		0x40000000
#define MAXREPLYQS              256

/* Command Status value */
#define CMD_SUCCESS             0x0000
#define CMD_TARGET_STATUS       0x0001
#define CMD_DATA_UNDERRUN       0x0002
#define CMD_DATA_OVERRUN        0x0003
#define CMD_INVALID             0x0004
#define CMD_PROTOCOL_ERR        0x0005
#define CMD_HARDWARE_ERR        0x0006
#define CMD_CONNECTION_LOST     0x0007
#define CMD_ABORTED             0x0008
#define CMD_ABORT_FAILED        0x0009
#define CMD_UNSOLICITED_ABORT   0x000A
#define CMD_TIMEOUT             0x000B
#define CMD_UNABORTABLE		0x000C
#define CMD_TMF_STATUS		0x000D
#define CMD_IOACCEL_DISABLED	0x000E
#define CMD_CTLR_LOCKUP		0xffff
/* Note: CMD_CTLR_LOCKUP is not a value defined by the CISS spec
 * it is a value defined by the driver that commands can be marked
 * with when a controller lockup has been detected by the driver
 */

/* TMF function status values */
#define CISS_TMF_COMPLETE	0x00
#define CISS_TMF_INVALID_FRAME	0x02
#define CISS_TMF_NOT_SUPPORTED	0x04
#define CISS_TMF_FAILED		0x05
#define CISS_TMF_SUCCESS	0x08
#define CISS_TMF_WRONG_LUN	0x09
#define CISS_TMF_OVERLAPPED_TAG 0x0a

/* Unit Attentions ASC's as defined for the MSA2012sa */
#define POWER_OR_RESET			0x29
#define STATE_CHANGED			0x2a
#define UNIT_ATTENTION_CLEARED		0x2f
#define LUN_FAILED			0x3e
#define REPORT_LUNS_CHANGED		0x3f

/* Unit Attentions ASCQ's as defined for the MSA2012sa */

	/* These ASCQ's defined for ASC = POWER_OR_RESET */
#define POWER_ON_RESET			0x00
#define POWER_ON_REBOOT			0x01
#define SCSI_BUS_RESET			0x02
#define MSA_TARGET_RESET		0x03
#define CONTROLLER_FAILOVER		0x04
#define TRANSCEIVER_SE			0x05
#define TRANSCEIVER_LVD			0x06

	/* These ASCQ's defined for ASC = STATE_CHANGED */
#define RESERVATION_PREEMPTED		0x03
#define ASYM_ACCESS_CHANGED		0x06
#define LUN_CAPACITY_CHANGED		0x09

/* transfer direction */
#define XFER_NONE               0x00
#define XFER_WRITE              0x01
#define XFER_READ               0x02
#define XFER_RSVD               0x03

/* task attribute */
#define ATTR_UNTAGGED           0x00
#define ATTR_SIMPLE             0x04
#define ATTR_HEADOFQUEUE        0x05
#define ATTR_ORDERED            0x06
#define ATTR_ACA                0x07

/* cdb type */
#define TYPE_CMD		0x00
#define TYPE_MSG		0x01
#define TYPE_IOACCEL2_CMD	0x81 /* 0x81 is not used by hardware */

/* Message Types  */
#define HPSA_TASK_MANAGEMENT    0x00
#define HPSA_RESET              0x01
#define HPSA_SCAN               0x02
#define HPSA_NOOP               0x03

#define HPSA_CTLR_RESET_TYPE    0x00
#define HPSA_BUS_RESET_TYPE     0x01
#define HPSA_TARGET_RESET_TYPE  0x03
#define HPSA_LUN_RESET_TYPE     0x04
#define HPSA_NEXUS_RESET_TYPE   0x05

/* Task Management Functions */
#define HPSA_TMF_ABORT_TASK     0x00
#define HPSA_TMF_ABORT_TASK_SET 0x01
#define HPSA_TMF_CLEAR_ACA      0x02
#define HPSA_TMF_CLEAR_TASK_SET 0x03
#define HPSA_TMF_QUERY_TASK     0x04
#define HPSA_TMF_QUERY_TASK_SET 0x05
#define HPSA_TMF_QUERY_ASYNCEVENT 0x06



/* config space register offsets */
#define CFG_VENDORID            0x00
#define CFG_DEVICEID            0x02
#define CFG_I2OBAR              0x10
#define CFG_MEM1BAR             0x14

/* i2o space register offsets */
#define I2O_IBDB_SET            0x20
#define I2O_IBDB_CLEAR          0x70
#define I2O_INT_STATUS          0x30
#define I2O_INT_MASK            0x34
#define I2O_IBPOST_Q            0x40
#define I2O_OBPOST_Q            0x44
#define I2O_DMA1_CFG		0x214

/* Configuration Table */
#define CFGTBL_ChangeReq        0x00000001l
#define CFGTBL_AccCmds          0x00000001l
#define DOORBELL_CTLR_RESET	0x00000004l
#define DOORBELL_CTLR_RESET2	0x00000020l
#define DOORBELL_CLEAR_EVENTS	0x00000040l

#define CFGTBL_Trans_Simple     0x00000002l
#define CFGTBL_Trans_Performant 0x00000004l
#define CFGTBL_Trans_io_accel1	0x00000080l
#define CFGTBL_Trans_io_accel2	0x00000100l
#define CFGTBL_Trans_use_short_tags 0x20000000l
#define CFGTBL_Trans_enable_directed_msix (1 << 30)

#define CFGTBL_BusType_Ultra2   0x00000001l
#define CFGTBL_BusType_Ultra3   0x00000002l
#define CFGTBL_BusType_Fibre1G  0x00000100l
#define CFGTBL_BusType_Fibre2G  0x00000200l

/* VPD Inquiry types */
#define HPSA_INQUIRY_FAILED		0x02
#define HPSA_VPD_SUPPORTED_PAGES        0x00
#define HPSA_VPD_LV_DEVICE_ID           0x83
#define HPSA_VPD_LV_DEVICE_GEOMETRY     0xC1
#define HPSA_VPD_LV_IOACCEL_STATUS      0xC2
#define HPSA_VPD_LV_STATUS		0xC3
#define HPSA_VPD_HEADER_SZ              4

/* Logical volume states */
#define HPSA_VPD_LV_STATUS_UNSUPPORTED			0xff
#define HPSA_LV_OK                                      0x0
#define HPSA_LV_FAILED					0x01
#define HPSA_LV_NOT_AVAILABLE				0x0b
#define HPSA_LV_UNDERGOING_ERASE			0x0F
#define HPSA_LV_UNDERGOING_RPI				0x12
#define HPSA_LV_PENDING_RPI				0x13
#define HPSA_LV_ENCRYPTED_NO_KEY			0x14
#define HPSA_LV_PLAINTEXT_IN_ENCRYPT_ONLY_CONTROLLER	0x15
#define HPSA_LV_UNDERGOING_ENCRYPTION			0x16
#define HPSA_LV_UNDERGOING_ENCRYPTION_REKEYING		0x17
#define HPSA_LV_ENCRYPTED_IN_NON_ENCRYPTED_CONTROLLER	0x18
#define HPSA_LV_PENDING_ENCRYPTION			0x19
#define HPSA_LV_PENDING_ENCRYPTION_REKEYING		0x1A

struct vals32 {
	u32   lower;
	u32   upper;
};

union u64bit {
	struct vals32 val32;
	u64 val;
};

/* FIXME this is a per controller value (barf!) */
#define HPSA_MAX_LUN 1024
#define HPSA_MAX_PHYS_LUN 1024
#define MAX_EXT_TARGETS 32
#define HPSA_MAX_DEVICES (HPSA_MAX_PHYS_LUN + HPSA_MAX_LUN + \
	MAX_EXT_TARGETS + 1) /* + 1 is for the controller itself */

/* SCSI-3 Commands */
#pragma pack(1)

#define HPSA_INQUIRY 0x12
struct InquiryData {
	u8 data_byte[36];
};

#define HPSA_REPORT_LOG 0xc2    /* Report Logical LUNs */
#define HPSA_REPORT_PHYS 0xc3   /* Report Physical LUNs */
#define HPSA_REPORT_PHYS_EXTENDED 0x02
#define HPSA_CISS_READ	0xc0	/* CISS Read */
#define HPSA_GET_RAID_MAP 0xc8	/* CISS Get RAID Layout Map */

#define RAID_MAP_MAX_ENTRIES   256

struct raid_map_disk_data {
	u32   ioaccel_handle;         /**< Handle to access this disk via the
					*  I/O accelerator */
	u8    xor_mult[2];            /**< XOR multipliers for this position,
					*  valid for data disks only */
	u8    reserved[2];
};

struct raid_map_data {
	__le32   structure_size;	/* Size of entire structure in bytes */
	__le32   volume_blk_size;	/* bytes / block in the volume */
	__le64   volume_blk_cnt;	/* logical blocks on the volume */
	u8    phys_blk_shift;		/* Shift factor to convert between
					 * units of logical blocks and physical
					 * disk blocks */
	u8    parity_rotation_shift;	/* Shift factor to convert between units
					 * of logical stripes and physical
					 * stripes */
	__le16   strip_size;		/* blocks used on each disk / stripe */
	__le64   disk_starting_blk;	/* First disk block used in volume */
	__le64   disk_blk_cnt;		/* disk blocks used by volume / disk */
	__le16   data_disks_per_row;	/* data disk entries / row in the map */
	__le16   metadata_disks_per_row;/* mirror/parity disk entries / row
					 * in the map */
	__le16   row_cnt;		/* rows in each layout map */
	__le16   layout_map_count;	/* layout maps (1 map per mirror/parity
					 * group) */
	__le16   flags;			/* Bit 0 set if encryption enabled */
#define RAID_MAP_FLAG_ENCRYPT_ON  0x01
	__le16   dekindex;		/* Data encryption key index. */
	u8    reserved[16];
	struct raid_map_disk_data data[RAID_MAP_MAX_ENTRIES];
};

struct ReportLUNdata {
	u8 LUNListLength[4];
	u8 extended_response_flag;
	u8 reserved[3];
	u8 LUN[HPSA_MAX_LUN][8];
};

struct ext_report_lun_entry {
	u8 lunid[8];
#define MASKED_DEVICE(x) ((x)[3] & 0xC0)
#define GET_BMIC_BUS(lunid) ((lunid)[7] & 0x3F)
#define GET_BMIC_LEVEL_TWO_TARGET(lunid) ((lunid)[6])
#define GET_BMIC_DRIVE_NUMBER(lunid) (((GET_BMIC_BUS((lunid)) - 1) << 8) + \
			GET_BMIC_LEVEL_TWO_TARGET((lunid)))
	u8 wwid[8];
	u8 device_type;
	u8 device_flags;
	u8 lun_count; /* multi-lun device, how many luns */
	u8 redundant_paths;
	u32 ioaccel_handle; /* ioaccel1 only uses lower 16 bits */
};

struct ReportExtendedLUNdata {
	u8 LUNListLength[4];
	u8 extended_response_flag;
	u8 reserved[3];
	struct ext_report_lun_entry LUN[HPSA_MAX_PHYS_LUN];
};

struct SenseSubsystem_info {
	u8 reserved[36];
	u8 portname[8];
	u8 reserved1[1108];
};

/* BMIC commands */
#define BMIC_READ 0x26
#define BMIC_WRITE 0x27
#define BMIC_CACHE_FLUSH 0xc2
#define HPSA_CACHE_FLUSH 0x01	/* C2 was already being used by HPSA */
#define BMIC_FLASH_FIRMWARE 0xF7
#define BMIC_SENSE_CONTROLLER_PARAMETERS 0x64
#define BMIC_IDENTIFY_PHYSICAL_DEVICE 0x15
#define BMIC_IDENTIFY_CONTROLLER 0x11
#define BMIC_SET_DIAG_OPTIONS 0xF4
#define BMIC_SENSE_DIAG_OPTIONS 0xF5
#define HPSA_DIAG_OPTS_DISABLE_RLD_CACHING 0x80000000
#define BMIC_SENSE_SUBSYSTEM_INFORMATION 0x66
#define BMIC_SENSE_STORAGE_BOX_PARAMS 0x65

/* Command List Structure */
union SCSI3Addr {
	struct {
		u8 Dev;
		u8 Bus:6;
		u8 Mode:2;        /* b00 */
	} PeripDev;
	struct {
		u8 DevLSB;
		u8 DevMSB:6;
		u8 Mode:2;        /* b01 */
	} LogDev;
	struct {
		u8 Dev:5;
		u8 Bus:3;
		u8 Targ:6;
		u8 Mode:2;        /* b10 */
	} LogUnit;
};

struct PhysDevAddr {
	u32             TargetId:24;
	u32             Bus:6;
	u32             Mode:2;
	/* 2 level target device addr */
	union SCSI3Addr  Target[2];
};

struct LogDevAddr {
	u32            VolId:30;
	u32            Mode:2;
	u8             reserved[4];
};

union LUNAddr {
	u8               LunAddrBytes[8];
	union SCSI3Addr    SCSI3Lun[4];
	struct PhysDevAddr PhysDev;
	struct LogDevAddr  LogDev;
};

struct CommandListHeader {
	u8              ReplyQueue;
	u8              SGList;
	__le16          SGTotal;
	__le64		tag;
	union LUNAddr     LUN;
};

struct RequestBlock {
	u8   CDBLen;
	/*
	 * type_attr_dir:
	 * type: low 3 bits
	 * attr: middle 3 bits
	 * dir: high 2 bits
	 */
	u8	type_attr_dir;
#define TYPE_ATTR_DIR(t, a, d) ((((d) & 0x03) << 6) |\
				(((a) & 0x07) << 3) |\
				((t) & 0x07))
#define GET_TYPE(tad) ((tad) & 0x07)
#define GET_ATTR(tad) (((tad) >> 3) & 0x07)
#define GET_DIR(tad) (((tad) >> 6) & 0x03)
	u16  Timeout;
	u8   CDB[16];
};

struct ErrDescriptor {
	__le64 Addr;
	__le32 Len;
};

struct SGDescriptor {
	__le64 Addr;
	__le32 Len;
	__le32 Ext;
};

union MoreErrInfo {
	struct {
		u8  Reserved[3];
		u8  Type;
		u32 ErrorInfo;
	} Common_Info;
	struct {
		u8  Reserved[2];
		u8  offense_size; /* size of offending entry */
		u8  offense_num;  /* byte # of offense 0-base */
		u32 offense_value;
	} Invalid_Cmd;
};
struct ErrorInfo {
	u8               ScsiStatus;
	u8               SenseLen;
	u16              CommandStatus;
	u32              ResidualCnt;
	union MoreErrInfo  MoreErrInfo;
	u8               SenseInfo[SENSEINFOBYTES];
};
/* Command types */
#define CMD_IOCTL_PEND  0x01
#define CMD_SCSI	0x03
#define CMD_IOACCEL1	0x04
#define CMD_IOACCEL2	0x05
#define IOACCEL2_TMF	0x06

#define DIRECT_LOOKUP_SHIFT 4
#define DIRECT_LOOKUP_MASK (~((1 << DIRECT_LOOKUP_SHIFT) - 1))

#define HPSA_ERROR_BIT          0x02
struct ctlr_info; /* defined in hpsa.h */
/* The size of this structure needs to be divisible by 128
 * on all architectures.  The low 4 bits of the addresses
 * are used as follows:
 *
 * bit 0: to device, used to indicate "performant mode" command
 *        from device, indidcates error status.
 * bit 1-3: to device, indicates block fetch table entry for
 *          reducing DMA in fetching commands from host memory.
 */

#define COMMANDLIST_ALIGNMENT 128
struct CommandList {
	struct CommandListHeader Header;
	struct RequestBlock      Request;
	struct ErrDescriptor     ErrDesc;
	struct SGDescriptor      SG[SG_ENTRIES_IN_CMD];
	/* information associated with the command */
	u32			   busaddr; /* physical addr of this record */
	struct ErrorInfo *err_info; /* pointer to the allocated mem */
	struct ctlr_info	   *h;
	int			   cmd_type;
	long			   cmdindex;
	struct completion *waiting;
	struct scsi_cmnd *scsi_cmd;
	struct work_struct work;

	/*
	 * For commands using either of the two "ioaccel" paths to
	 * bypass the RAID stack and go directly to the physical disk
	 * phys_disk is a pointer to the hpsa_scsi_dev_t to which the
	 * i/o is destined.  We need to store that here because the command
	 * may potentially encounter TASK SET FULL and need to be resubmitted
	 * For "normal" i/o's not using the "ioaccel" paths, phys_disk is
	 * not used.
	 */
	struct hpsa_scsi_dev_t *phys_disk;

	int abort_pending;
	struct hpsa_scsi_dev_t *reset_pending;
	atomic_t refcount; /* Must be last to avoid memset in hpsa_cmd_init() */
} __aligned(COMMANDLIST_ALIGNMENT);

/* Max S/G elements in I/O accelerator command */
#define IOACCEL1_MAXSGENTRIES           24
#define IOACCEL2_MAXSGENTRIES		28

/*
 * Structure for I/O accelerator (mode 1) commands.
 * Note that this structure must be 128-byte aligned in size.
 */
#define IOACCEL1_COMMANDLIST_ALIGNMENT 128
struct io_accel1_cmd {
	__le16 dev_handle;		/* 0x00 - 0x01 */
	u8  reserved1;			/* 0x02 */
	u8  function;			/* 0x03 */
	u8  reserved2[8];		/* 0x04 - 0x0B */
	u32 err_info;			/* 0x0C - 0x0F */
	u8  reserved3[2];		/* 0x10 - 0x11 */
	u8  err_info_len;		/* 0x12 */
	u8  reserved4;			/* 0x13 */
	u8  sgl_offset;			/* 0x14 */
	u8  reserved5[7];		/* 0x15 - 0x1B */
	__le32 transfer_len;		/* 0x1C - 0x1F */
	u8  reserved6[4];		/* 0x20 - 0x23 */
	__le16 io_flags;		/* 0x24 - 0x25 */
	u8  reserved7[14];		/* 0x26 - 0x33 */
	u8  LUN[8];			/* 0x34 - 0x3B */
	__le32 control;			/* 0x3C - 0x3F */
	u8  CDB[16];			/* 0x40 - 0x4F */
	u8  reserved8[16];		/* 0x50 - 0x5F */
	__le16 host_context_flags;	/* 0x60 - 0x61 */
	__le16 timeout_sec;		/* 0x62 - 0x63 */
	u8  ReplyQueue;			/* 0x64 */
	u8  reserved9[3];		/* 0x65 - 0x67 */
	__le64 tag;			/* 0x68 - 0x6F */
	__le64 host_addr;		/* 0x70 - 0x77 */
	u8  CISS_LUN[8];		/* 0x78 - 0x7F */
	struct SGDescriptor SG[IOACCEL1_MAXSGENTRIES];
} __aligned(IOACCEL1_COMMANDLIST_ALIGNMENT);

#define IOACCEL1_FUNCTION_SCSIIO        0x00
#define IOACCEL1_SGLOFFSET              32

#define IOACCEL1_IOFLAGS_IO_REQ         0x4000
#define IOACCEL1_IOFLAGS_CDBLEN_MASK    0x001F
#define IOACCEL1_IOFLAGS_CDBLEN_MAX     16

#define IOACCEL1_CONTROL_NODATAXFER     0x00000000
#define IOACCEL1_CONTROL_DATA_OUT       0x01000000
#define IOACCEL1_CONTROL_DATA_IN        0x02000000
#define IOACCEL1_CONTROL_TASKPRIO_MASK  0x00007800
#define IOACCEL1_CONTROL_TASKPRIO_SHIFT 11
#define IOACCEL1_CONTROL_SIMPLEQUEUE    0x00000000
#define IOACCEL1_CONTROL_HEADOFQUEUE    0x00000100
#define IOACCEL1_CONTROL_ORDEREDQUEUE   0x00000200
#define IOACCEL1_CONTROL_ACA            0x00000400

#define IOACCEL1_HCFLAGS_CISS_FORMAT    0x0013

#define IOACCEL1_BUSADDR_CMDTYPE        0x00000060

struct ioaccel2_sg_element {
	__le64 address;
	__le32 length;
	u8 reserved[3];
	u8 chain_indicator;
#define IOACCEL2_CHAIN 0x80
};

/*
 * SCSI Response Format structure for IO Accelerator Mode 2
 */
struct io_accel2_scsi_response {
	u8 IU_type;
#define IOACCEL2_IU_TYPE_SRF			0x60
	u8 reserved1[3];
	u8 req_id[4];		/* request identifier */
	u8 reserved2[4];
	u8 serv_response;		/* service response */
#define IOACCEL2_SERV_RESPONSE_COMPLETE		0x000
#define IOACCEL2_SERV_RESPONSE_FAILURE		0x001
#define IOACCEL2_SERV_RESPONSE_TMF_COMPLETE	0x002
#define IOACCEL2_SERV_RESPONSE_TMF_SUCCESS	0x003
#define IOACCEL2_SERV_RESPONSE_TMF_REJECTED	0x004
#define IOACCEL2_SERV_RESPONSE_TMF_WRONG_LUN	0x005
	u8 status;			/* status */
#define IOACCEL2_STATUS_SR_TASK_COMP_GOOD	0x00
#define IOACCEL2_STATUS_SR_TASK_COMP_CHK_COND	0x02
#define IOACCEL2_STATUS_SR_TASK_COMP_BUSY	0x08
#define IOACCEL2_STATUS_SR_TASK_COMP_RES_CON	0x18
#define IOACCEL2_STATUS_SR_TASK_COMP_SET_FULL	0x28
#define IOACCEL2_STATUS_SR_TASK_COMP_ABORTED	0x40
#define IOACCEL2_STATUS_SR_IOACCEL_DISABLED	0x0E
#define IOACCEL2_STATUS_SR_IO_ERROR		0x01
#define IOACCEL2_STATUS_SR_IO_ABORTED		0x02
#define IOACCEL2_STATUS_SR_NO_PATH_TO_DEVICE	0x03
#define IOACCEL2_STATUS_SR_INVALID_DEVICE	0x04
#define IOACCEL2_STATUS_SR_UNDERRUN		0x51
#define IOACCEL2_STATUS_SR_OVERRUN		0x75
	u8 data_present;		/* low 2 bits */
#define IOACCEL2_NO_DATAPRESENT		0x000
#define IOACCEL2_RESPONSE_DATAPRESENT	0x001
#define IOACCEL2_SENSE_DATA_PRESENT	0x002
#define IOACCEL2_RESERVED		0x003
	u8 sense_data_len;		/* sense/response data length */
	u8 resid_cnt[4];		/* residual count */
	u8 sense_data_buff[32];		/* sense/response data buffer */
};

/*
 * Structure for I/O accelerator (mode 2 or m2) commands.
 * Note that this structure must be 128-byte aligned in size.
 */
#define IOACCEL2_COMMANDLIST_ALIGNMENT 128
struct io_accel2_cmd {
	u8  IU_type;			/* IU Type */
	u8  direction;			/* direction, memtype, and encryption */
#define IOACCEL2_DIRECTION_MASK		0x03 /* bits 0,1: direction  */
#define IOACCEL2_DIRECTION_MEMTYPE_MASK	0x04 /* bit 2: memtype source/dest */
					     /*     0b=PCIe, 1b=DDR */
#define IOACCEL2_DIRECTION_ENCRYPT_MASK	0x08 /* bit 3: encryption flag */
					     /*     0=off, 1=on */
	u8  reply_queue;		/* Reply Queue ID */
	u8  reserved1;			/* Reserved */
	__le32 scsi_nexus;		/* Device Handle */
	__le32 Tag;			/* cciss tag, lower 4 bytes only */
	__le32 tweak_lower;		/* Encryption tweak, lower 4 bytes */
	u8  cdb[16];			/* SCSI Command Descriptor Block */
	u8  cciss_lun[8];		/* 8 byte SCSI address */
	__le32 data_len;		/* Total bytes to transfer */
	u8  cmd_priority_task_attr;	/* priority and task attrs */
#define IOACCEL2_PRIORITY_MASK 0x78
#define IOACCEL2_ATTR_MASK 0x07
	u8  sg_count;			/* Number of sg elements */
	__le16 dekindex;		/* Data encryption key index */
	__le64 err_ptr;			/* Error Pointer */
	__le32 err_len;			/* Error Length*/
	__le32 tweak_upper;		/* Encryption tweak, upper 4 bytes */
	struct ioaccel2_sg_element sg[IOACCEL2_MAXSGENTRIES];
	struct io_accel2_scsi_response error_data;
} __aligned(IOACCEL2_COMMANDLIST_ALIGNMENT);

/*
 * defines for Mode 2 command struct
 * FIXME: this can't be all I need mfm
 */
#define IOACCEL2_IU_TYPE	0x40
#define IOACCEL2_IU_TMF_TYPE	0x41
#define IOACCEL2_DIR_NO_DATA	0x00
#define IOACCEL2_DIR_DATA_IN	0x01
#define IOACCEL2_DIR_DATA_OUT	0x02
#define IOACCEL2_TMF_ABORT	0x01
/*
 * SCSI Task Management Request format for Accelerator Mode 2
 */
struct hpsa_tmf_struct {
	u8 iu_type;		/* Information Unit Type */
	u8 reply_queue;		/* Reply Queue ID */
	u8 tmf;			/* Task Management Function */
	u8 reserved1;		/* byte 3 Reserved */
	__le32 it_nexus;	/* SCSI I-T Nexus */
	u8 lun_id[8];		/* LUN ID for TMF request */
	__le64 tag;		/* cciss tag associated w/ request */
	__le64 abort_tag;	/* cciss tag of SCSI cmd or TMF to abort */
	__le64 error_ptr;		/* Error Pointer */
	__le32 error_len;		/* Error Length */
} __aligned(IOACCEL2_COMMANDLIST_ALIGNMENT);

/* Configuration Table Structure */
struct HostWrite {
	__le32		TransportRequest;
	__le32		command_pool_addr_hi;
	__le32		CoalIntDelay;
	__le32		CoalIntCount;
};

#define SIMPLE_MODE     0x02
#define PERFORMANT_MODE 0x04
#define MEMQ_MODE       0x08
#define IOACCEL_MODE_1  0x80

#define DRIVER_SUPPORT_UA_ENABLE        0x00000001

struct CfgTable {
	u8		Signature[4];
	__le32		SpecValence;
	__le32		TransportSupport;
	__le32		TransportActive;
	struct HostWrite HostWrite;
	__le32		CmdsOutMax;
	__le32		BusTypes;
	__le32		TransMethodOffset;
	u8		ServerName[16];
	__le32		HeartBeat;
	__le32		driver_support;
#define			ENABLE_SCSI_PREFETCH		0x100
#define			ENABLE_UNIT_ATTN		0x01
	__le32		MaxScatterGatherElements;
	__le32		MaxLogicalUnits;
	__le32		MaxPhysicalDevices;
	__le32		MaxPhysicalDrivesPerLogicalUnit;
	__le32		MaxPerformantModeCommands;
	__le32		MaxBlockFetch;
	__le32		PowerConservationSupport;
	__le32		PowerConservationEnable;
	__le32		TMFSupportFlags;
	u8		TMFTagMask[8];
	u8		reserved[0x78 - 0x70];
	__le32		misc_fw_support;		/* offset 0x78 */
#define			MISC_FW_DOORBELL_RESET		0x02
#define			MISC_FW_DOORBELL_RESET2		0x010
#define			MISC_FW_RAID_OFFLOAD_BASIC	0x020
#define			MISC_FW_EVENT_NOTIFY		0x080
	u8		driver_version[32];
	__le32		max_cached_write_size;
	u8		driver_scratchpad[16];
	__le32		max_error_info_length;
	__le32		io_accel_max_embedded_sg_count;
	__le32		io_accel_request_size_offset;
	__le32		event_notify;
#define		HPSA_EVENT_NOTIFY_ACCEL_IO_PATH_STATE_CHANGE (1 << 30)
#define		HPSA_EVENT_NOTIFY_ACCEL_IO_PATH_CONFIG_CHANGE (1 << 31)
	__le32		clear_event_notify;
};

#define NUM_BLOCKFETCH_ENTRIES 8
struct TransTable_struct {
	__le32		BlockFetch[NUM_BLOCKFETCH_ENTRIES];
	__le32		RepQSize;
	__le32		RepQCount;
	__le32		RepQCtrAddrLow32;
	__le32		RepQCtrAddrHigh32;
#define MAX_REPLY_QUEUES 64
	struct vals32  RepQAddr[MAX_REPLY_QUEUES];
};

struct hpsa_pci_info {
	unsigned char	bus;
	unsigned char	dev_fn;
	unsigned short	domain;
	u32		board_id;
};

struct bmic_identify_controller {
	u8	configured_logical_drive_count;	/* offset 0 */
	u8	pad1[153];
	__le16	extended_logical_unit_count;	/* offset 154 */
	u8	pad2[136];
	u8	controller_mode;	/* offset 292 */
	u8	pad3[32];
};


struct bmic_identify_physical_device {
	u8    scsi_bus;          /* SCSI Bus number on controller */
	u8    scsi_id;           /* SCSI ID on this bus */
	__le16 block_size;	     /* sector size in bytes */
	__le32 total_blocks;	     /* number for sectors on drive */
	__le32 reserved_blocks;   /* controller reserved (RIS) */
	u8    model[40];         /* Physical Drive Model */
	u8    serial_number[40]; /* Drive Serial Number */
	u8    firmware_revision[8]; /* drive firmware revision */
	u8    scsi_inquiry_bits; /* inquiry byte 7 bits */
	u8    compaq_drive_stamp; /* 0 means drive not stamped */
	u8    last_failure_reason;
#define BMIC_LAST_FAILURE_TOO_SMALL_IN_LOAD_CONFIG		0x01
#define BMIC_LAST_FAILURE_ERROR_ERASING_RIS			0x02
#define BMIC_LAST_FAILURE_ERROR_SAVING_RIS			0x03
#define BMIC_LAST_FAILURE_FAIL_DRIVE_COMMAND			0x04
#define BMIC_LAST_FAILURE_MARK_BAD_FAILED			0x05
#define BMIC_LAST_FAILURE_MARK_BAD_FAILED_IN_FINISH_REMAP	0x06
#define BMIC_LAST_FAILURE_TIMEOUT				0x07
#define BMIC_LAST_FAILURE_AUTOSENSE_FAILED			0x08
#define BMIC_LAST_FAILURE_MEDIUM_ERROR_1			0x09
#define BMIC_LAST_FAILURE_MEDIUM_ERROR_2			0x0a
#define BMIC_LAST_FAILURE_NOT_READY_BAD_SENSE			0x0b
#define BMIC_LAST_FAILURE_NOT_READY				0x0c
#define BMIC_LAST_FAILURE_HARDWARE_ERROR			0x0d
#define BMIC_LAST_FAILURE_ABORTED_COMMAND			0x0e
#define BMIC_LAST_FAILURE_WRITE_PROTECTED			0x0f
#define BMIC_LAST_FAILURE_SPIN_UP_FAILURE_IN_RECOVER		0x10
#define BMIC_LAST_FAILURE_REBUILD_WRITE_ERROR			0x11
#define BMIC_LAST_FAILURE_TOO_SMALL_IN_HOT_PLUG			0x12
#define BMIC_LAST_FAILURE_BUS_RESET_RECOVERY_ABORTED		0x13
#define BMIC_LAST_FAILURE_REMOVED_IN_HOT_PLUG			0x14
#define BMIC_LAST_FAILURE_INIT_REQUEST_SENSE_FAILED		0x15
#define BMIC_LAST_FAILURE_INIT_START_UNIT_FAILED		0x16
#define BMIC_LAST_FAILURE_INQUIRY_FAILED			0x17
#define BMIC_LAST_FAILURE_NON_DISK_DEVICE			0x18
#define BMIC_LAST_FAILURE_READ_CAPACITY_FAILED			0x19
#define BMIC_LAST_FAILURE_INVALID_BLOCK_SIZE			0x1a
#define BMIC_LAST_FAILURE_HOT_PLUG_REQUEST_SENSE_FAILED		0x1b
#define BMIC_LAST_FAILURE_HOT_PLUG_START_UNIT_FAILED		0x1c
#define BMIC_LAST_FAILURE_WRITE_ERROR_AFTER_REMAP		0x1d
#define BMIC_LAST_FAILURE_INIT_RESET_RECOVERY_ABORTED		0x1e
#define BMIC_LAST_FAILURE_DEFERRED_WRITE_ERROR			0x1f
#define BMIC_LAST_FAILURE_MISSING_IN_SAVE_RIS			0x20
#define BMIC_LAST_FAILURE_WRONG_REPLACE				0x21
#define BMIC_LAST_FAILURE_GDP_VPD_INQUIRY_FAILED		0x22
#define BMIC_LAST_FAILURE_GDP_MODE_SENSE_FAILED			0x23
#define BMIC_LAST_FAILURE_DRIVE_NOT_IN_48BIT_MODE		0x24
#define BMIC_LAST_FAILURE_DRIVE_TYPE_MIX_IN_HOT_PLUG		0x25
#define BMIC_LAST_FAILURE_DRIVE_TYPE_MIX_IN_LOAD_CFG		0x26
#define BMIC_LAST_FAILURE_PROTOCOL_ADAPTER_FAILED		0x27
#define BMIC_LAST_FAILURE_FAULTY_ID_BAY_EMPTY			0x28
#define BMIC_LAST_FAILURE_FAULTY_ID_BAY_OCCUPIED		0x29
#define BMIC_LAST_FAILURE_FAULTY_ID_INVALID_BAY			0x2a
#define BMIC_LAST_FAILURE_WRITE_RETRIES_FAILED			0x2b

#define BMIC_LAST_FAILURE_SMART_ERROR_REPORTED			0x37
#define BMIC_LAST_FAILURE_PHY_RESET_FAILED			0x38
#define BMIC_LAST_FAILURE_ONLY_ONE_CTLR_CAN_SEE_DRIVE		0x40
#define BMIC_LAST_FAILURE_KC_VOLUME_FAILED			0x41
#define BMIC_LAST_FAILURE_UNEXPECTED_REPLACEMENT		0x42
#define BMIC_LAST_FAILURE_OFFLINE_ERASE				0x80
#define BMIC_LAST_FAILURE_OFFLINE_TOO_SMALL			0x81
#define BMIC_LAST_FAILURE_OFFLINE_DRIVE_TYPE_MIX		0x82
#define BMIC_LAST_FAILURE_OFFLINE_ERASE_COMPLETE		0x83

	u8     flags;
	u8     more_flags;
	u8     scsi_lun;          /* SCSI LUN for phys drive */
	u8     yet_more_flags;
	u8     even_more_flags;
	__le32 spi_speed_rules;/* SPI Speed data:Ultra disable diagnose */
	u8     phys_connector[2];         /* connector number on controller */
	u8     phys_box_on_bus;  /* phys enclosure this drive resides */
	u8     phys_bay_in_box;  /* phys drv bay this drive resides */
	__le32 rpm;              /* Drive rotational speed in rpm */
	u8     device_type;       /* type of drive */
	u8     sata_version;     /* only valid when drive_type is SATA */
	__le64 big_total_block_count;
	__le64 ris_starting_lba;
	__le32 ris_size;
	u8     wwid[20];
	u8     controller_phy_map[32];
	__le16 phy_count;
	u8     phy_connected_dev_type[256];
	u8     phy_to_drive_bay_num[256];
	__le16 phy_to_attached_dev_index[256];
	u8     box_index;
	u8     reserved;
	__le16 extra_physical_drive_flags;
#define BMIC_PHYS_DRIVE_SUPPORTS_GAS_GAUGE(idphydrv) \
	(idphydrv->extra_physical_drive_flags & (1 << 10))
	u8     negotiated_link_rate[256];
	u8     phy_to_phy_map[256];
	u8     redundant_path_present_map;
	u8     redundant_path_failure_map;
	u8     active_path_number;
	__le16 alternate_paths_phys_connector[8];
	u8     alternate_paths_phys_box_on_port[8];
	u8     multi_lun_device_lun_count;
	u8     minimum_good_fw_revision[8];
	u8     unique_inquiry_bytes[20];
	u8     current_temperature_degreesC;
	u8     temperature_threshold_degreesC;
	u8     max_temperature_degreesC;
	u8     logical_blocks_per_phys_block_exp; /* phyblocksize = 512*2^exp */
	__le16 current_queue_depth_limit;
	u8     reserved_switch_stuff[60];
	__le16 power_on_hours; /* valid only if gas gauge supported */
	__le16 percent_endurance_used; /* valid only if gas gauge supported. */
#define BMIC_PHYS_DRIVE_SSD_WEAROUT(idphydrv) \
	((idphydrv->percent_endurance_used & 0x80) || \
	 (idphydrv->percent_endurance_used > 10000))
	u8     drive_authentication;
#define BMIC_PHYS_DRIVE_AUTHENTICATED(idphydrv) \
	(idphydrv->drive_authentication == 0x80)
	u8     smart_carrier_authentication;
#define BMIC_SMART_CARRIER_AUTHENTICATION_SUPPORTED(idphydrv) \
	(idphydrv->smart_carrier_authentication != 0x0)
#define BMIC_SMART_CARRIER_AUTHENTICATED(idphydrv) \
	(idphydrv->smart_carrier_authentication == 0x01)
	u8     smart_carrier_app_fw_version;
	u8     smart_carrier_bootloader_fw_version;
	u8     sanitize_support_flags;
	u8     drive_key_flags;
	u8     encryption_key_name[64];
	__le32 misc_drive_flags;
	__le16 dek_index;
	__le16 hba_drive_encryption_flags;
	__le16 max_overwrite_time;
	__le16 max_block_erase_time;
	__le16 max_crypto_erase_time;
	u8     device_connector_info[5];
	u8     connector_name[8][8];
	u8     page_83_id[16];
	u8     max_link_rate[256];
	u8     neg_phys_link_rate[256];
	u8     box_conn_name[8];
} __attribute((aligned(512)));

struct bmic_sense_subsystem_info {
	u8	primary_slot_number;
	u8	reserved[3];
	u8	chasis_serial_number[32];
	u8	primary_world_wide_id[8];
	u8	primary_array_serial_number[32]; /* NULL terminated */
	u8	primary_cache_serial_number[32]; /* NULL terminated */
	u8	reserved_2[8];
	u8	secondary_array_serial_number[32];
	u8	secondary_cache_serial_number[32];
	u8	pad[332];
};

struct bmic_sense_storage_box_params {
	u8	reserved[36];
	u8	inquiry_valid;
	u8	reserved_1[68];
	u8	phys_box_on_port;
	u8	reserved_2[22];
	u16	connection_info;
	u8	reserver_3[84];
	u8	phys_connector[2];
	u8	reserved_4[296];
};

#pragma pack()
#endif /* HPSA_CMD_H */
