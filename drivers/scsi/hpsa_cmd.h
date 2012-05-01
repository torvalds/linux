/*
 *    Disk Array driver for HP Smart Array SAS controllers
 *    Copyright 2000, 2009 Hewlett-Packard Development Company, L.P.
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
#define TYPE_CMD				0x00
#define TYPE_MSG				0x01

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

#define CFGTBL_Trans_Simple     0x00000002l
#define CFGTBL_Trans_Performant 0x00000004l
#define CFGTBL_Trans_use_short_tags 0x20000000l
#define CFGTBL_Trans_enable_directed_msix (1 << 30)

#define CFGTBL_BusType_Ultra2   0x00000001l
#define CFGTBL_BusType_Ultra3   0x00000002l
#define CFGTBL_BusType_Fibre1G  0x00000100l
#define CFGTBL_BusType_Fibre2G  0x00000200l
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
struct ReportLUNdata {
	u8 LUNListLength[4];
	u32 reserved;
	u8 LUN[HPSA_MAX_LUN][8];
};

struct ReportExtendedLUNdata {
	u8 LUNListLength[4];
	u8 extended_response_flag;
	u8 reserved[3];
	u8 LUN[HPSA_MAX_LUN][24];
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
	struct request *rq;
	struct completion *waiting;
	void   *scsi_cmd;

/* on 64 bit architectures, to get this to be 32-byte-aligned
 * it so happens we need PAD_64 bytes of padding, on 32 bit systems,
 * we need PAD_32 bytes of padding (see below).   This does that.
 * If it happens that 64 bit and 32 bit systems need different
 * padding, PAD_32 and PAD_64 can be set independently, and.
 * the code below will do the right thing.
 */
#define IS_32_BIT ((8 - sizeof(long))/4)
#define IS_64_BIT (!IS_32_BIT)
#define PAD_32 (4)
#define PAD_64 (4)
#define COMMANDLIST_PAD (IS_32_BIT * PAD_32 + IS_64_BIT * PAD_64)
	u8 pad[COMMANDLIST_PAD];
};

/* Configuration Table Structure */
struct HostWrite {
	u32 TransportRequest;
	u32 Reserved;
	u32 CoalIntDelay;
	u32 CoalIntCount;
};

#define SIMPLE_MODE     0x02
#define PERFORMANT_MODE 0x04
#define MEMQ_MODE       0x08

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
	u32           SCSI_Prefetch;
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
	u8		driver_version[32];

};

#define NUM_BLOCKFETCH_ENTRIES 8
struct TransTable_struct {
	u32            BlockFetch[NUM_BLOCKFETCH_ENTRIES];
	u32            RepQSize;
	u32            RepQCount;
	u32            RepQCtrAddrLow32;
	u32            RepQCtrAddrHigh32;
#define MAX_REPLY_QUEUES 8
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
