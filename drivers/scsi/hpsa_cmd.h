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
#define MAXSGENTRIES            31
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

#define CFGTBL_Trans_Simple     0x00000002l

#define CFGTBL_BusType_Ultra2   0x00000001l
#define CFGTBL_BusType_Ultra3   0x00000002l
#define CFGTBL_BusType_Fibre1G  0x00000100l
#define CFGTBL_BusType_Fibre2G  0x00000200l
struct vals32 {
	__u32   lower;
	__u32   upper;
};

union u64bit {
	struct vals32 val32;
	__u64 val;
};

/* FIXME this is a per controller value (barf!) */
#define HPSA_MAX_TARGETS_PER_CTLR 16
#define HPSA_MAX_LUN 256
#define HPSA_MAX_PHYS_LUN 1024

/* SCSI-3 Commands */
#pragma pack(1)

#define HPSA_INQUIRY 0x12
struct InquiryData {
	__u8 data_byte[36];
};

#define HPSA_REPORT_LOG 0xc2    /* Report Logical LUNs */
#define HPSA_REPORT_PHYS 0xc3   /* Report Physical LUNs */
struct ReportLUNdata {
	__u8 LUNListLength[4];
	__u32 reserved;
	__u8 LUN[HPSA_MAX_LUN][8];
};

struct ReportExtendedLUNdata {
	__u8 LUNListLength[4];
	__u8 extended_response_flag;
	__u8 reserved[3];
	__u8 LUN[HPSA_MAX_LUN][24];
};

struct SenseSubsystem_info {
	__u8 reserved[36];
	__u8 portname[8];
	__u8 reserved1[1108];
};

#define HPSA_READ_CAPACITY 0x25 /* Read Capacity */
struct ReadCapdata {
	__u8 total_size[4];	/* Total size in blocks */
	__u8 block_size[4];	/* Size of blocks in bytes */
};

#if 0
/* 12 byte commands not implemented in firmware yet. */
#define HPSA_READ 	0xa8
#define HPSA_WRITE	0xaa
#endif

#define HPSA_READ   0x28    /* Read(10) */
#define HPSA_WRITE  0x2a    /* Write(10) */

/* BMIC commands */
#define BMIC_READ 0x26
#define BMIC_WRITE 0x27
#define BMIC_CACHE_FLUSH 0xc2
#define HPSA_CACHE_FLUSH 0x01	/* C2 was already being used by HPSA */

/* Command List Structure */
union SCSI3Addr {
	struct {
		__u8 Dev;
		__u8 Bus:6;
		__u8 Mode:2;        /* b00 */
	} PeripDev;
	struct {
		__u8 DevLSB;
		__u8 DevMSB:6;
		__u8 Mode:2;        /* b01 */
	} LogDev;
	struct {
		__u8 Dev:5;
		__u8 Bus:3;
		__u8 Targ:6;
		__u8 Mode:2;        /* b10 */
	} LogUnit;
};

struct PhysDevAddr {
	__u32             TargetId:24;
	__u32             Bus:6;
	__u32             Mode:2;
	/* 2 level target device addr */
	union SCSI3Addr  Target[2];
};

struct LogDevAddr {
	__u32            VolId:30;
	__u32            Mode:2;
	__u8             reserved[4];
};

union LUNAddr {
	__u8               LunAddrBytes[8];
	union SCSI3Addr    SCSI3Lun[4];
	struct PhysDevAddr PhysDev;
	struct LogDevAddr  LogDev;
};

struct CommandListHeader {
	__u8              ReplyQueue;
	__u8              SGList;
	__u16             SGTotal;
	struct vals32     Tag;
	union LUNAddr     LUN;
};

struct RequestBlock {
	__u8   CDBLen;
	struct {
		__u8 Type:3;
		__u8 Attribute:3;
		__u8 Direction:2;
	} Type;
	__u16  Timeout;
	__u8   CDB[16];
};

struct ErrDescriptor {
	struct vals32 Addr;
	__u32  Len;
};

struct SGDescriptor {
	struct vals32 Addr;
	__u32  Len;
	__u32  Ext;
};

union MoreErrInfo {
	struct {
		__u8  Reserved[3];
		__u8  Type;
		__u32 ErrorInfo;
	} Common_Info;
	struct {
		__u8  Reserved[2];
		__u8  offense_size; /* size of offending entry */
		__u8  offense_num;  /* byte # of offense 0-base */
		__u32 offense_value;
	} Invalid_Cmd;
};
struct ErrorInfo {
	__u8               ScsiStatus;
	__u8               SenseLen;
	__u16              CommandStatus;
	__u32              ResidualCnt;
	union MoreErrInfo  MoreErrInfo;
	__u8               SenseInfo[SENSEINFOBYTES];
};
/* Command types */
#define CMD_IOCTL_PEND  0x01
#define CMD_SCSI	0x03

struct ctlr_info; /* defined in hpsa.h */
/* The size of this structure needs to be divisible by 8
 * od on all architectures, because the controller uses 2
 * lower bits of the address, and the driver uses 1 lower
 * bit (3 bits total.)
 */
struct CommandList {
	struct CommandListHeader Header;
	struct RequestBlock      Request;
	struct ErrDescriptor     ErrDesc;
	struct SGDescriptor      SG[MAXSGENTRIES];
	/* information associated with the command */
	__u32			   busaddr; /* physical addr of this record */
	struct ErrorInfo *err_info; /* pointer to the allocated mem */
	struct ctlr_info	   *h;
	int			   cmd_type;
	long			   cmdindex;
	struct hlist_node list;
	struct CommandList *prev;
	struct CommandList *next;
	struct request *rq;
	struct completion *waiting;
	int	 retry_count;
	void   *scsi_cmd;
};

/* Configuration Table Structure */
struct HostWrite {
	__u32 TransportRequest;
	__u32 Reserved;
	__u32 CoalIntDelay;
	__u32 CoalIntCount;
};

struct CfgTable {
	__u8             Signature[4];
	__u32            SpecValence;
	__u32            TransportSupport;
	__u32            TransportActive;
	struct HostWrite HostWrite;
	__u32            CmdsOutMax;
	__u32            BusTypes;
	__u32            Reserved;
	__u8             ServerName[16];
	__u32            HeartBeat;
	__u32            SCSI_Prefetch;
};

struct hpsa_pci_info {
	unsigned char	bus;
	unsigned char	dev_fn;
	unsigned short	domain;
	__u32		board_id;
};

#pragma pack()
#endif /* HPSA_CMD_H */
