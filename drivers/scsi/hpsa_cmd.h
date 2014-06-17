/*
 *    Disk Array driver for HP Smart Array SAS controllers
 *    Copyright 2000, 2014 Hewlett-Packard Development Company, L.P.
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
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *    Questions/Comments/Bugfixes to iss_storagedev@hp.com
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
#define CMD_IOACCEL_DISABLED	0x000E


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
#define HPSA_VPD_SUPPORTED_PAGES        0x00
#define HPSA_VPD_LV_DEVICE_GEOMETRY     0xC1
#define HPSA_VPD_LV_IOACCEL_STATUS      0xC2
#define HPSA_VPD_LV_STATUS		0xC3
#define HPSA_VPD_HEADER_SZ              4

/* Logical volume states */
#define HPSA_VPD_LV_STATUS_UNSUPPORTED			0xff
#define HPSA_LV_OK                                      0x0
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
	u32   structure_size;		/* Size of entire structure in bytes */
	u32   volume_blk_size;		/* bytes / block in the volume */
	u64   volume_blk_cnt;		/* logical blocks on the volume */
	u8    phys_blk_shift;		/* Shift factor to convert between
					 * units of logical blocks and physical
					 * disk blocks */
	u8    parity_rotation_shift;	/* Shift factor to convert between units
					 * of logical stripes and physical
					 * stripes */
	u16   strip_size;		/* blocks used on each disk / stripe */
	u64   disk_starting_blk;	/* First disk block used in volume */
	u64   disk_blk_cnt;		/* disk blocks used by volume / disk */
	u16   data_disks_per_row;	/* data disk entries / row in the map */
	u16   metadata_disks_per_row;	/* mirror/parity disk entries / row
					 * in the map */
	u16   row_cnt;			/* rows in each layout map */
	u16   layout_map_count;		/* layout maps (1 map per mirror/parity
					 * group) */
	u16   flags;			/* Bit 0 set if encryption enabled */
#define RAID_MAP_FLAG_ENCRYPT_ON  0x01
	u16   dekindex;			/* Data encryption key index. */
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
	struct ext_report_lun_entry LUN[HPSA_MAX_LUN];
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
	u16             SGTotal;
	struct vals32     Tag;
	union LUNAddr     LUN;
};

struct RequestBlock {
	u8   CDBLen;
	struct {
		u8 Type:3;
		u8 Attribute:3;
		u8 Direction:2;
	} Type;
	u16  Timeout;
	u8   CDB[16];
};

struct ErrDescriptor {
	struct vals32 Addr;
	u32  Len;
};

struct SGDescriptor {
	struct vals32 Addr;
	u32  Len;
	u32  Ext;
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

#define DIRECT_LOOKUP_SHIFT 5
#define DIRECT_LOOKUP_BIT 0x10
#define DIRECT_LOOKUP_MASK (~((1 << DIRECT_LOOKUP_SHIFT) - 1))

#define HPSA_ERROR_BIT          0x02
struct ctlr_info; /* defined in hpsa.h */
/* The size of this structure needs to be divisible by 32
 * on all architectures because low 5 bits of the addresses
 * are used as follows:
 *
 * bit 0: to device, used to indicate "performant mode" command
 *        from device, indidcates error status.
 * bit 1-3: to device, indicates block fetch table entry for
 *          reducing DMA in fetching commands from host memory.
 * bit 4: used to indicate whether tag is "direct lookup" (index),
 *        or a bus address.
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
	struct list_head list;
	struct completion *waiting;
	void   *scsi_cmd;
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
	u16 dev_handle;			/* 0x00 - 0x01 */
	u8  reserved1;			/* 0x02 */
	u8  function;			/* 0x03 */
	u8  reserved2[8];		/* 0x04 - 0x0B */
	u32 err_info;			/* 0x0C - 0x0F */
	u8  reserved3[2];		/* 0x10 - 0x11 */
	u8  err_info_len;		/* 0x12 */
	u8  reserved4;			/* 0x13 */
	u8  sgl_offset;			/* 0x14 */
	u8  reserved5[7];		/* 0x15 - 0x1B */
	u32 transfer_len;		/* 0x1C - 0x1F */
	u8  reserved6[4];		/* 0x20 - 0x23 */
	u16 io_flags;			/* 0x24 - 0x25 */
	u8  reserved7[14];		/* 0x26 - 0x33 */
	u8  LUN[8];			/* 0x34 - 0x3B */
	u32 control;			/* 0x3C - 0x3F */
	u8  CDB[16];			/* 0x40 - 0x4F */
	u8  reserved8[16];		/* 0x50 - 0x5F */
	u16 host_context_flags;		/* 0x60 - 0x61 */
	u16 timeout_sec;		/* 0x62 - 0x63 */
	u8  ReplyQueue;			/* 0x64 */
	u8  reserved9[3];		/* 0x65 - 0x67 */
	struct vals32 Tag;		/* 0x68 - 0x6F */
	struct vals32 host_addr;	/* 0x70 - 0x77 */
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
	u64 address;
	u32 length;
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
	u32 scsi_nexus;			/* Device Handle */
	u32 Tag;			/* cciss tag, lower 4 bytes only */
	u32 tweak_lower;		/* Encryption tweak, lower 4 bytes */
	u8  cdb[16];			/* SCSI Command Descriptor Block */
	u8  cciss_lun[8];		/* 8 byte SCSI address */
	u32 data_len;			/* Total bytes to transfer */
	u8  cmd_priority_task_attr;	/* priority and task attrs */
#define IOACCEL2_PRIORITY_MASK 0x78
#define IOACCEL2_ATTR_MASK 0x07
	u8  sg_count;			/* Number of sg elements */
	u16 dekindex;			/* Data encryption key index */
	u64 err_ptr;			/* Error Pointer */
	u32 err_len;			/* Error Length*/
	u32 tweak_upper;		/* Encryption tweak, upper 4 bytes */
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
/*
 * SCSI Task Management Request format for Accelerator Mode 2
 */
struct hpsa_tmf_struct {
	u8 iu_type;		/* Information Unit Type */
	u8 reply_queue;		/* Reply Queue ID */
	u8 tmf;			/* Task Management Function */
	u8 reserved1;		/* byte 3 Reserved */
	u32 it_nexus;		/* SCSI I-T Nexus */
	u8 lun_id[8];		/* LUN ID for TMF request */
	struct vals32 Tag;	/* cciss tag associated w/ request */
	struct vals32 abort_tag;/* cciss tag of SCSI cmd or task to abort */
	u64 error_ptr;		/* Error Pointer */
	u32 error_len;		/* Error Length */
};

/* Configuration Table Structure */
struct HostWrite {
	u32 TransportRequest;
	u32 command_pool_addr_hi;
	u32 CoalIntDelay;
	u32 CoalIntCount;
};

#define SIMPLE_MODE     0x02
#define PERFORMANT_MODE 0x04
#define MEMQ_MODE       0x08
#define IOACCEL_MODE_1  0x80

#define DRIVER_SUPPORT_UA_ENABLE        0x00000001

struct CfgTable {
	u8            Signature[4];
	u32		SpecValence;
	u32           TransportSupport;
	u32           TransportActive;
	struct 		HostWrite HostWrite;
	u32           CmdsOutMax;
	u32           BusTypes;
	u32           TransMethodOffset;
	u8            ServerName[16];
	u32           HeartBeat;
	u32           driver_support;
#define			ENABLE_SCSI_PREFETCH 0x100
#define			ENABLE_UNIT_ATTN 0x01
	u32	 	MaxScatterGatherElements;
	u32		MaxLogicalUnits;
	u32		MaxPhysicalDevices;
	u32		MaxPhysicalDrivesPerLogicalUnit;
	u32		MaxPerformantModeCommands;
	u32		MaxBlockFetch;
	u32		PowerConservationSupport;
	u32		PowerConservationEnable;
	u32		TMFSupportFlags;
	u8		TMFTagMask[8];
	u8		reserved[0x78 - 0x70];
	u32		misc_fw_support; /* offset 0x78 */
#define			MISC_FW_DOORBELL_RESET (0x02)
#define			MISC_FW_DOORBELL_RESET2 (0x010)
#define			MISC_FW_RAID_OFFLOAD_BASIC (0x020)
#define			MISC_FW_EVENT_NOTIFY (0x080)
	u8		driver_version[32];
	u32             max_cached_write_size;
	u8              driver_scratchpad[16];
	u32             max_error_info_length;
	u32		io_accel_max_embedded_sg_count;
	u32		io_accel_request_size_offset;
	u32		event_notify;
#define HPSA_EVENT_NOTIFY_ACCEL_IO_PATH_STATE_CHANGE (1 << 30)
#define HPSA_EVENT_NOTIFY_ACCEL_IO_PATH_CONFIG_CHANGE (1 << 31)
	u32		clear_event_notify;
};

#define NUM_BLOCKFETCH_ENTRIES 8
struct TransTable_struct {
	u32            BlockFetch[NUM_BLOCKFETCH_ENTRIES];
	u32            RepQSize;
	u32            RepQCount;
	u32            RepQCtrAddrLow32;
	u32            RepQCtrAddrHigh32;
#define MAX_REPLY_QUEUES 64
	struct vals32  RepQAddr[MAX_REPLY_QUEUES];
};

struct hpsa_pci_info {
	unsigned char	bus;
	unsigned char	dev_fn;
	unsigned short	domain;
	u32		board_id;
};

#pragma pack()
#endif /* HPSA_CMD_H */
