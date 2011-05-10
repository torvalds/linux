/*
 *  Linux MegaRAID driver for SAS based RAID controllers
 *
 *  Copyright (c) 2009-2011  LSI Corporation.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 *  FILE: megaraid_sas_fusion.h
 *
 *  Authors: LSI Corporation
 *           Manoj Jose
 *           Sumant Patro
 *
 *  Send feedback to: <megaraidlinux@lsi.com>
 *
 *  Mail to: LSI Corporation, 1621 Barber Lane, Milpitas, CA 95035
 *     ATTN: Linuxraid
 */

#ifndef _MEGARAID_SAS_FUSION_H_
#define _MEGARAID_SAS_FUSION_H_

/* Fusion defines */
#define MEGASAS_MAX_SZ_CHAIN_FRAME 1024
#define MFI_FUSION_ENABLE_INTERRUPT_MASK (0x00000009)
#define MEGA_MPI2_RAID_DEFAULT_IO_FRAME_SIZE 256
#define MEGASAS_MPI2_FUNCTION_PASSTHRU_IO_REQUEST   0xF0
#define MEGASAS_MPI2_FUNCTION_LD_IO_REQUEST         0xF1
#define MEGASAS_LOAD_BALANCE_FLAG		    0x1
#define MEGASAS_DCMD_MBOX_PEND_FLAG		    0x1
#define HOST_DIAG_WRITE_ENABLE			    0x80
#define HOST_DIAG_RESET_ADAPTER			    0x4
#define MEGASAS_FUSION_MAX_RESET_TRIES		    3

/* T10 PI defines */
#define MR_PROT_INFO_TYPE_CONTROLLER                0x8
#define MEGASAS_SCSI_VARIABLE_LENGTH_CMD            0x7f
#define MEGASAS_SCSI_SERVICE_ACTION_READ32          0x9
#define MEGASAS_SCSI_SERVICE_ACTION_WRITE32         0xB
#define MEGASAS_SCSI_ADDL_CDB_LEN                   0x18
#define MEGASAS_RD_WR_PROTECT_CHECK_ALL		    0x20
#define MEGASAS_RD_WR_PROTECT_CHECK_NONE	    0x60
#define MEGASAS_EEDPBLOCKSIZE			    512

/*
 * Raid context flags
 */

#define MR_RAID_CTX_RAID_FLAGS_IO_SUB_TYPE_SHIFT   0x4
#define MR_RAID_CTX_RAID_FLAGS_IO_SUB_TYPE_MASK    0x30
enum MR_RAID_FLAGS_IO_SUB_TYPE {
	MR_RAID_FLAGS_IO_SUB_TYPE_NONE = 0,
	MR_RAID_FLAGS_IO_SUB_TYPE_SYSTEM_PD = 1,
};

/*
 * Request descriptor types
 */
#define MEGASAS_REQ_DESCRIPT_FLAGS_LD_IO           0x7
#define MEGASAS_REQ_DESCRIPT_FLAGS_MFA             0x1

#define MEGASAS_REQ_DESCRIPT_FLAGS_TYPE_SHIFT      1

#define MEGASAS_FP_CMD_LEN	16
#define MEGASAS_FUSION_IN_RESET 0

/*
 * Raid Context structure which describes MegaRAID specific IO Paramenters
 * This resides at offset 0x60 where the SGL normally starts in MPT IO Frames
 */

struct RAID_CONTEXT {
	u16     resvd0;
	u16     timeoutValue;
	u8      regLockFlags;
	u8      resvd1;
	u16     VirtualDiskTgtId;
	u64     regLockRowLBA;
	u32     regLockLength;
	u16     nextLMId;
	u8      exStatus;
	u8      status;
	u8      RAIDFlags;
	u8      numSGE;
	u16	configSeqNum;
	u8      spanArm;
	u8      resvd2[3];
};

#define RAID_CTX_SPANARM_ARM_SHIFT	(0)
#define RAID_CTX_SPANARM_ARM_MASK	(0x1f)

#define RAID_CTX_SPANARM_SPAN_SHIFT	(5)
#define RAID_CTX_SPANARM_SPAN_MASK	(0xE0)

/*
 * define region lock types
 */
enum REGION_TYPE {
	REGION_TYPE_UNUSED       = 0,
	REGION_TYPE_SHARED_READ  = 1,
	REGION_TYPE_SHARED_WRITE = 2,
	REGION_TYPE_EXCLUSIVE    = 3,
};

/* MPI2 defines */
#define MPI2_FUNCTION_IOC_INIT              (0x02) /* IOC Init */
#define MPI2_WHOINIT_HOST_DRIVER            (0x04)
#define MPI2_VERSION_MAJOR                  (0x02)
#define MPI2_VERSION_MINOR                  (0x00)
#define MPI2_VERSION_MAJOR_MASK             (0xFF00)
#define MPI2_VERSION_MAJOR_SHIFT            (8)
#define MPI2_VERSION_MINOR_MASK             (0x00FF)
#define MPI2_VERSION_MINOR_SHIFT            (0)
#define MPI2_VERSION ((MPI2_VERSION_MAJOR << MPI2_VERSION_MAJOR_SHIFT) | \
		      MPI2_VERSION_MINOR)
#define MPI2_HEADER_VERSION_UNIT            (0x10)
#define MPI2_HEADER_VERSION_DEV             (0x00)
#define MPI2_HEADER_VERSION_UNIT_MASK       (0xFF00)
#define MPI2_HEADER_VERSION_UNIT_SHIFT      (8)
#define MPI2_HEADER_VERSION_DEV_MASK        (0x00FF)
#define MPI2_HEADER_VERSION_DEV_SHIFT       (0)
#define MPI2_HEADER_VERSION ((MPI2_HEADER_VERSION_UNIT << 8) | \
			     MPI2_HEADER_VERSION_DEV)
#define MPI2_IEEE_SGE_FLAGS_IOCPLBNTA_ADDR      (0x03)
#define MPI2_SCSIIO_EEDPFLAGS_INC_PRI_REFTAG        (0x8000)
#define MPI2_SCSIIO_EEDPFLAGS_CHECK_REFTAG          (0x0400)
#define MPI2_SCSIIO_EEDPFLAGS_CHECK_REMOVE_OP       (0x0003)
#define MPI2_SCSIIO_EEDPFLAGS_CHECK_APPTAG          (0x0200)
#define MPI2_SCSIIO_EEDPFLAGS_CHECK_GUARD           (0x0100)
#define MPI2_SCSIIO_EEDPFLAGS_INSERT_OP             (0x0004)
#define MPI2_FUNCTION_SCSI_IO_REQUEST               (0x00) /* SCSI IO */
#define MPI2_REQ_DESCRIPT_FLAGS_HIGH_PRIORITY           (0x06)
#define MPI2_REQ_DESCRIPT_FLAGS_SCSI_IO                 (0x00)
#define MPI2_SGE_FLAGS_64_BIT_ADDRESSING        (0x02)
#define MPI2_SCSIIO_CONTROL_WRITE               (0x01000000)
#define MPI2_SCSIIO_CONTROL_READ                (0x02000000)
#define MPI2_REQ_DESCRIPT_FLAGS_TYPE_MASK       (0x0E)
#define MPI2_RPY_DESCRIPT_FLAGS_UNUSED          (0x0F)
#define MPI2_RPY_DESCRIPT_FLAGS_SCSI_IO_SUCCESS (0x00)
#define MPI2_RPY_DESCRIPT_FLAGS_TYPE_MASK       (0x0F)
#define MPI2_WRSEQ_FLUSH_KEY_VALUE              (0x0)
#define MPI2_WRITE_SEQUENCE_OFFSET              (0x00000004)
#define MPI2_WRSEQ_1ST_KEY_VALUE                (0xF)
#define MPI2_WRSEQ_2ND_KEY_VALUE                (0x4)
#define MPI2_WRSEQ_3RD_KEY_VALUE                (0xB)
#define MPI2_WRSEQ_4TH_KEY_VALUE                (0x2)
#define MPI2_WRSEQ_5TH_KEY_VALUE                (0x7)
#define MPI2_WRSEQ_6TH_KEY_VALUE                (0xD)

struct MPI25_IEEE_SGE_CHAIN64 {
	u64                     Address;
	u32                     Length;
	u16                     Reserved1;
	u8                      NextChainOffset;
	u8                      Flags;
};

struct MPI2_SGE_SIMPLE_UNION {
	u32                     FlagsLength;
	union {
		u32                 Address32;
		u64                 Address64;
	} u;
};

struct MPI2_SCSI_IO_CDB_EEDP32 {
	u8                      CDB[20];                    /* 0x00 */
	u32                     PrimaryReferenceTag;        /* 0x14 */
	u16                     PrimaryApplicationTag;      /* 0x18 */
	u16                     PrimaryApplicationTagMask;  /* 0x1A */
	u32                     TransferLength;             /* 0x1C */
};

struct MPI2_SGE_CHAIN_UNION {
	u16                     Length;
	u8                      NextChainOffset;
	u8                      Flags;
	union {
		u32                 Address32;
		u64                 Address64;
	} u;
};

struct MPI2_IEEE_SGE_SIMPLE32 {
	u32                     Address;
	u32                     FlagsLength;
};

struct MPI2_IEEE_SGE_CHAIN32 {
	u32                     Address;
	u32                     FlagsLength;
};

struct MPI2_IEEE_SGE_SIMPLE64 {
	u64                     Address;
	u32                     Length;
	u16                     Reserved1;
	u8                      Reserved2;
	u8                      Flags;
};

struct MPI2_IEEE_SGE_CHAIN64 {
	u64                     Address;
	u32                     Length;
	u16                     Reserved1;
	u8                      Reserved2;
	u8                      Flags;
};

union MPI2_IEEE_SGE_SIMPLE_UNION {
	struct MPI2_IEEE_SGE_SIMPLE32  Simple32;
	struct MPI2_IEEE_SGE_SIMPLE64  Simple64;
};

union MPI2_IEEE_SGE_CHAIN_UNION {
	struct MPI2_IEEE_SGE_CHAIN32   Chain32;
	struct MPI2_IEEE_SGE_CHAIN64   Chain64;
};

union MPI2_SGE_IO_UNION {
	struct MPI2_SGE_SIMPLE_UNION       MpiSimple;
	struct MPI2_SGE_CHAIN_UNION        MpiChain;
	union MPI2_IEEE_SGE_SIMPLE_UNION  IeeeSimple;
	union MPI2_IEEE_SGE_CHAIN_UNION   IeeeChain;
};

union MPI2_SCSI_IO_CDB_UNION {
	u8                      CDB32[32];
	struct MPI2_SCSI_IO_CDB_EEDP32 EEDP32;
	struct MPI2_SGE_SIMPLE_UNION SGE;
};

/*
 * RAID SCSI IO Request Message
 * Total SGE count will be one less than  _MPI2_SCSI_IO_REQUEST
 */
struct MPI2_RAID_SCSI_IO_REQUEST {
	u16                     DevHandle;                      /* 0x00 */
	u8                      ChainOffset;                    /* 0x02 */
	u8                      Function;                       /* 0x03 */
	u16                     Reserved1;                      /* 0x04 */
	u8                      Reserved2;                      /* 0x06 */
	u8                      MsgFlags;                       /* 0x07 */
	u8                      VP_ID;                          /* 0x08 */
	u8                      VF_ID;                          /* 0x09 */
	u16                     Reserved3;                      /* 0x0A */
	u32                     SenseBufferLowAddress;          /* 0x0C */
	u16                     SGLFlags;                       /* 0x10 */
	u8                      SenseBufferLength;              /* 0x12 */
	u8                      Reserved4;                      /* 0x13 */
	u8                      SGLOffset0;                     /* 0x14 */
	u8                      SGLOffset1;                     /* 0x15 */
	u8                      SGLOffset2;                     /* 0x16 */
	u8                      SGLOffset3;                     /* 0x17 */
	u32                     SkipCount;                      /* 0x18 */
	u32                     DataLength;                     /* 0x1C */
	u32                     BidirectionalDataLength;        /* 0x20 */
	u16                     IoFlags;                        /* 0x24 */
	u16                     EEDPFlags;                      /* 0x26 */
	u32                     EEDPBlockSize;                  /* 0x28 */
	u32                     SecondaryReferenceTag;          /* 0x2C */
	u16                     SecondaryApplicationTag;        /* 0x30 */
	u16                     ApplicationTagTranslationMask;  /* 0x32 */
	u8                      LUN[8];                         /* 0x34 */
	u32                     Control;                        /* 0x3C */
	union MPI2_SCSI_IO_CDB_UNION  CDB;			/* 0x40 */
	struct RAID_CONTEXT	RaidContext;                    /* 0x60 */
	union MPI2_SGE_IO_UNION       SGL;			/* 0x80 */
};

/*
 * MPT RAID MFA IO Descriptor.
 */
struct MEGASAS_RAID_MFA_IO_REQUEST_DESCRIPTOR {
	u32     RequestFlags:8;
	u32     MessageAddress1:24; /* bits 31:8*/
	u32     MessageAddress2;      /* bits 61:32 */
};

/* Default Request Descriptor */
struct MPI2_DEFAULT_REQUEST_DESCRIPTOR {
	u8              RequestFlags;               /* 0x00 */
	u8              MSIxIndex;                  /* 0x01 */
	u16             SMID;                       /* 0x02 */
	u16             LMID;                       /* 0x04 */
	u16             DescriptorTypeDependent;    /* 0x06 */
};

/* High Priority Request Descriptor */
struct MPI2_HIGH_PRIORITY_REQUEST_DESCRIPTOR {
	u8              RequestFlags;               /* 0x00 */
	u8              MSIxIndex;                  /* 0x01 */
	u16             SMID;                       /* 0x02 */
	u16             LMID;                       /* 0x04 */
	u16             Reserved1;                  /* 0x06 */
};

/* SCSI IO Request Descriptor */
struct MPI2_SCSI_IO_REQUEST_DESCRIPTOR {
	u8              RequestFlags;               /* 0x00 */
	u8              MSIxIndex;                  /* 0x01 */
	u16             SMID;                       /* 0x02 */
	u16             LMID;                       /* 0x04 */
	u16             DevHandle;                  /* 0x06 */
};

/* SCSI Target Request Descriptor */
struct MPI2_SCSI_TARGET_REQUEST_DESCRIPTOR {
	u8              RequestFlags;               /* 0x00 */
	u8              MSIxIndex;                  /* 0x01 */
	u16             SMID;                       /* 0x02 */
	u16             LMID;                       /* 0x04 */
	u16             IoIndex;                    /* 0x06 */
};

/* RAID Accelerator Request Descriptor */
struct MPI2_RAID_ACCEL_REQUEST_DESCRIPTOR {
	u8              RequestFlags;               /* 0x00 */
	u8              MSIxIndex;                  /* 0x01 */
	u16             SMID;                       /* 0x02 */
	u16             LMID;                       /* 0x04 */
	u16             Reserved;                   /* 0x06 */
};

/* union of Request Descriptors */
union MEGASAS_REQUEST_DESCRIPTOR_UNION {
	struct MPI2_DEFAULT_REQUEST_DESCRIPTOR             Default;
	struct MPI2_HIGH_PRIORITY_REQUEST_DESCRIPTOR       HighPriority;
	struct MPI2_SCSI_IO_REQUEST_DESCRIPTOR             SCSIIO;
	struct MPI2_SCSI_TARGET_REQUEST_DESCRIPTOR         SCSITarget;
	struct MPI2_RAID_ACCEL_REQUEST_DESCRIPTOR          RAIDAccelerator;
	struct MEGASAS_RAID_MFA_IO_REQUEST_DESCRIPTOR      MFAIo;
	union {
		struct {
			u32 low;
			u32 high;
		} u;
		u64 Words;
	};
};

/* Default Reply Descriptor */
struct MPI2_DEFAULT_REPLY_DESCRIPTOR {
	u8              ReplyFlags;                 /* 0x00 */
	u8              MSIxIndex;                  /* 0x01 */
	u16             DescriptorTypeDependent1;   /* 0x02 */
	u32             DescriptorTypeDependent2;   /* 0x04 */
};

/* Address Reply Descriptor */
struct MPI2_ADDRESS_REPLY_DESCRIPTOR {
	u8              ReplyFlags;                 /* 0x00 */
	u8              MSIxIndex;                  /* 0x01 */
	u16             SMID;                       /* 0x02 */
	u32             ReplyFrameAddress;          /* 0x04 */
};

/* SCSI IO Success Reply Descriptor */
struct MPI2_SCSI_IO_SUCCESS_REPLY_DESCRIPTOR {
	u8              ReplyFlags;                 /* 0x00 */
	u8              MSIxIndex;                  /* 0x01 */
	u16             SMID;                       /* 0x02 */
	u16             TaskTag;                    /* 0x04 */
	u16             Reserved1;                  /* 0x06 */
};

/* TargetAssist Success Reply Descriptor */
struct MPI2_TARGETASSIST_SUCCESS_REPLY_DESCRIPTOR {
	u8              ReplyFlags;                 /* 0x00 */
	u8              MSIxIndex;                  /* 0x01 */
	u16             SMID;                       /* 0x02 */
	u8              SequenceNumber;             /* 0x04 */
	u8              Reserved1;                  /* 0x05 */
	u16             IoIndex;                    /* 0x06 */
};

/* Target Command Buffer Reply Descriptor */
struct MPI2_TARGET_COMMAND_BUFFER_REPLY_DESCRIPTOR {
	u8              ReplyFlags;                 /* 0x00 */
	u8              MSIxIndex;                  /* 0x01 */
	u8              VP_ID;                      /* 0x02 */
	u8              Flags;                      /* 0x03 */
	u16             InitiatorDevHandle;         /* 0x04 */
	u16             IoIndex;                    /* 0x06 */
};

/* RAID Accelerator Success Reply Descriptor */
struct MPI2_RAID_ACCELERATOR_SUCCESS_REPLY_DESCRIPTOR {
	u8              ReplyFlags;                 /* 0x00 */
	u8              MSIxIndex;                  /* 0x01 */
	u16             SMID;                       /* 0x02 */
	u32             Reserved;                   /* 0x04 */
};

/* union of Reply Descriptors */
union MPI2_REPLY_DESCRIPTORS_UNION {
	struct MPI2_DEFAULT_REPLY_DESCRIPTOR                   Default;
	struct MPI2_ADDRESS_REPLY_DESCRIPTOR                   AddressReply;
	struct MPI2_SCSI_IO_SUCCESS_REPLY_DESCRIPTOR           SCSIIOSuccess;
	struct MPI2_TARGETASSIST_SUCCESS_REPLY_DESCRIPTOR TargetAssistSuccess;
	struct MPI2_TARGET_COMMAND_BUFFER_REPLY_DESCRIPTOR TargetCommandBuffer;
	struct MPI2_RAID_ACCELERATOR_SUCCESS_REPLY_DESCRIPTOR
	RAIDAcceleratorSuccess;
	u64                                             Words;
};

/* IOCInit Request message */
struct MPI2_IOC_INIT_REQUEST {
	u8                      WhoInit;                        /* 0x00 */
	u8                      Reserved1;                      /* 0x01 */
	u8                      ChainOffset;                    /* 0x02 */
	u8                      Function;                       /* 0x03 */
	u16                     Reserved2;                      /* 0x04 */
	u8                      Reserved3;                      /* 0x06 */
	u8                      MsgFlags;                       /* 0x07 */
	u8                      VP_ID;                          /* 0x08 */
	u8                      VF_ID;                          /* 0x09 */
	u16                     Reserved4;                      /* 0x0A */
	u16                     MsgVersion;                     /* 0x0C */
	u16                     HeaderVersion;                  /* 0x0E */
	u32                     Reserved5;                      /* 0x10 */
	u16                     Reserved6;                      /* 0x14 */
	u8                      Reserved7;                      /* 0x16 */
	u8                      HostMSIxVectors;                /* 0x17 */
	u16                     Reserved8;                      /* 0x18 */
	u16                     SystemRequestFrameSize;         /* 0x1A */
	u16                     ReplyDescriptorPostQueueDepth;  /* 0x1C */
	u16                     ReplyFreeQueueDepth;            /* 0x1E */
	u32                     SenseBufferAddressHigh;         /* 0x20 */
	u32                     SystemReplyAddressHigh;         /* 0x24 */
	u64                     SystemRequestFrameBaseAddress;  /* 0x28 */
	u64                     ReplyDescriptorPostQueueAddress;/* 0x30 */
	u64                     ReplyFreeQueueAddress;          /* 0x38 */
	u64                     TimeStamp;                      /* 0x40 */
};

/* mrpriv defines */
#define MR_PD_INVALID 0xFFFF
#define MAX_SPAN_DEPTH 8
#define MAX_RAIDMAP_SPAN_DEPTH (MAX_SPAN_DEPTH)
#define MAX_ROW_SIZE 32
#define MAX_RAIDMAP_ROW_SIZE (MAX_ROW_SIZE)
#define MAX_LOGICAL_DRIVES 64
#define MAX_RAIDMAP_LOGICAL_DRIVES (MAX_LOGICAL_DRIVES)
#define MAX_RAIDMAP_VIEWS (MAX_LOGICAL_DRIVES)
#define MAX_ARRAYS 128
#define MAX_RAIDMAP_ARRAYS (MAX_ARRAYS)
#define MAX_PHYSICAL_DEVICES 256
#define MAX_RAIDMAP_PHYSICAL_DEVICES (MAX_PHYSICAL_DEVICES)
#define MR_DCMD_LD_MAP_GET_INFO             0x0300e101

struct MR_DEV_HANDLE_INFO {
	u16     curDevHdl;
	u8      validHandles;
	u8      reserved;
	u16     devHandle[2];
};

struct MR_ARRAY_INFO {
	u16      pd[MAX_RAIDMAP_ROW_SIZE];
};

struct MR_QUAD_ELEMENT {
	u64     logStart;
	u64     logEnd;
	u64     offsetInSpan;
	u32     diff;
	u32     reserved1;
};

struct MR_SPAN_INFO {
	u32             noElements;
	u32             reserved1;
	struct MR_QUAD_ELEMENT quad[MAX_RAIDMAP_SPAN_DEPTH];
};

struct MR_LD_SPAN {
	u64      startBlk;
	u64      numBlks;
	u16      arrayRef;
	u8       reserved[6];
};

struct MR_SPAN_BLOCK_INFO {
	u64          num_rows;
	struct MR_LD_SPAN   span;
	struct MR_SPAN_INFO block_span_info;
};

struct MR_LD_RAID {
	struct {
		u32     fpCapable:1;
		u32     reserved5:3;
		u32     ldPiMode:4;
		u32     pdPiMode:4;
		u32     encryptionType:8;
		u32     fpWriteCapable:1;
		u32     fpReadCapable:1;
		u32     fpWriteAcrossStripe:1;
		u32     fpReadAcrossStripe:1;
		u32     reserved4:8;
	} capability;
	u32     reserved6;
	u64     size;
	u8      spanDepth;
	u8      level;
	u8      stripeShift;
	u8      rowSize;
	u8      rowDataSize;
	u8      writeMode;
	u8      PRL;
	u8      SRL;
	u16     targetId;
	u8      ldState;
	u8      regTypeReqOnWrite;
	u8      modFactor;
	u8      reserved2[1];
	u16     seqNum;

	struct {
		u32 ldSyncRequired:1;
		u32 reserved:31;
	} flags;

	u8      reserved3[0x5C];
};

struct MR_LD_SPAN_MAP {
	struct MR_LD_RAID          ldRaid;
	u8                  dataArmMap[MAX_RAIDMAP_ROW_SIZE];
	struct MR_SPAN_BLOCK_INFO  spanBlock[MAX_RAIDMAP_SPAN_DEPTH];
};

struct MR_FW_RAID_MAP {
	u32                 totalSize;
	union {
		struct {
			u32         maxLd;
			u32         maxSpanDepth;
			u32         maxRowSize;
			u32         maxPdCount;
			u32         maxArrays;
		} validationInfo;
		u32             version[5];
		u32             reserved1[5];
	};

	u32                 ldCount;
	u32                 Reserved1;
	u8                  ldTgtIdToLd[MAX_RAIDMAP_LOGICAL_DRIVES+
					MAX_RAIDMAP_VIEWS];
	u8                  fpPdIoTimeoutSec;
	u8                  reserved2[7];
	struct MR_ARRAY_INFO       arMapInfo[MAX_RAIDMAP_ARRAYS];
	struct MR_DEV_HANDLE_INFO  devHndlInfo[MAX_RAIDMAP_PHYSICAL_DEVICES];
	struct MR_LD_SPAN_MAP      ldSpanMap[1];
};

struct IO_REQUEST_INFO {
	u64 ldStartBlock;
	u32 numBlocks;
	u16 ldTgtId;
	u8 isRead;
	u16 devHandle;
	u64 pdBlock;
	u8 fpOkForIo;
};

struct MR_LD_TARGET_SYNC {
	u8  targetId;
	u8  reserved;
	u16 seqNum;
};

#define IEEE_SGE_FLAGS_ADDR_MASK            (0x03)
#define IEEE_SGE_FLAGS_SYSTEM_ADDR          (0x00)
#define IEEE_SGE_FLAGS_IOCDDR_ADDR          (0x01)
#define IEEE_SGE_FLAGS_IOCPLB_ADDR          (0x02)
#define IEEE_SGE_FLAGS_IOCPLBNTA_ADDR       (0x03)
#define IEEE_SGE_FLAGS_CHAIN_ELEMENT        (0x80)
#define IEEE_SGE_FLAGS_END_OF_LIST          (0x40)

struct megasas_register_set;
struct megasas_instance;

union desc_word {
	u64 word;
	struct {
		u32 low;
		u32 high;
	} u;
};

struct megasas_cmd_fusion {
	struct MPI2_RAID_SCSI_IO_REQUEST	*io_request;
	dma_addr_t			io_request_phys_addr;

	union MPI2_SGE_IO_UNION	*sg_frame;
	dma_addr_t		sg_frame_phys_addr;

	u8 *sense;
	dma_addr_t sense_phys_addr;

	struct list_head list;
	struct scsi_cmnd *scmd;
	struct megasas_instance *instance;

	u8 retry_for_fw_reset;
	union MEGASAS_REQUEST_DESCRIPTOR_UNION  *request_desc;

	/*
	 * Context for a MFI frame.
	 * Used to get the mfi cmd from list when a MFI cmd is completed
	 */
	u32 sync_cmd_idx;
	u32 index;
	u8 flags;
};

struct LD_LOAD_BALANCE_INFO {
	u8	loadBalanceFlag;
	u8	reserved1;
	u16     raid1DevHandle[2];
	atomic_t     scsi_pending_cmds[2];
	u64     last_accessed_block[2];
};

struct MR_FW_RAID_MAP_ALL {
	struct MR_FW_RAID_MAP raidMap;
	struct MR_LD_SPAN_MAP ldSpanMap[MAX_LOGICAL_DRIVES - 1];
} __attribute__ ((packed));

struct fusion_context {
	struct megasas_cmd_fusion **cmd_list;
	struct list_head cmd_pool;

	spinlock_t cmd_pool_lock;

	dma_addr_t req_frames_desc_phys;
	u8 *req_frames_desc;

	struct dma_pool *io_request_frames_pool;
	dma_addr_t io_request_frames_phys;
	u8 *io_request_frames;

	struct dma_pool *sg_dma_pool;
	struct dma_pool *sense_dma_pool;

	dma_addr_t reply_frames_desc_phys;
	union MPI2_REPLY_DESCRIPTORS_UNION *reply_frames_desc;
	struct dma_pool *reply_frames_desc_pool;

	u16 last_reply_idx;

	u32 reply_q_depth;
	u32 request_alloc_sz;
	u32 reply_alloc_sz;
	u32 io_frames_alloc_sz;

	u16	max_sge_in_main_msg;
	u16	max_sge_in_chain;

	u8	chain_offset_io_request;
	u8	chain_offset_mfi_pthru;

	struct MR_FW_RAID_MAP_ALL *ld_map[2];
	dma_addr_t ld_map_phys[2];

	u32 map_sz;
	u8 fast_path_io;
	struct LD_LOAD_BALANCE_INFO load_balance_info[MAX_LOGICAL_DRIVES];
};

union desc_value {
	u64 word;
	struct {
		u32 low;
		u32 high;
	} u;
};

#endif /* _MEGARAID_SAS_FUSION_H_ */
