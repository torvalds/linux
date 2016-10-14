/*
 * Copyright 2000-2015 Avago Technologies.  All rights reserved.
 *
 *
 *          Name:  mpi2_init.h
 *         Title:  MPI SCSI initiator mode messages and structures
 * Creation Date:  June 23, 2006
 *
 * mpi2_init.h Version:  02.00.20
 *
 * NOTE: Names (typedefs, defines, etc.) beginning with an MPI25 or Mpi25
 *       prefix are for use only on MPI v2.5 products, and must not be used
 *       with MPI v2.0 products. Unless otherwise noted, names beginning with
 *       MPI2 or Mpi2 are for use with both MPI v2.0 and MPI v2.5 products.
 *
 * Version History
 * ---------------
 *
 * Date      Version   Description
 * --------  --------  ------------------------------------------------------
 * 04-30-07  02.00.00  Corresponds to Fusion-MPT MPI Specification Rev A.
 * 10-31-07  02.00.01  Fixed name for pMpi2SCSITaskManagementRequest_t.
 * 12-18-07  02.00.02  Modified Task Management Target Reset Method defines.
 * 02-29-08  02.00.03  Added Query Task Set and Query Unit Attention.
 * 03-03-08  02.00.04  Fixed name of struct _MPI2_SCSI_TASK_MANAGE_REPLY.
 * 05-21-08  02.00.05  Fixed typo in name of Mpi2SepRequest_t.
 * 10-02-08  02.00.06  Removed Untagged and No Disconnect values from SCSI IO
 *                     Control field Task Attribute flags.
 *                     Moved LUN field defines to mpi2.h becasue they are
 *                     common to many structures.
 * 05-06-09  02.00.07  Changed task management type of Query Unit Attention to
 *                     Query Asynchronous Event.
 *                     Defined two new bits in the SlotStatus field of the SCSI
 *                     Enclosure Processor Request and Reply.
 * 10-28-09  02.00.08  Added defines for decoding the ResponseInfo bytes for
 *                     both SCSI IO Error Reply and SCSI Task Management Reply.
 *                     Added ResponseInfo field to MPI2_SCSI_TASK_MANAGE_REPLY.
 *                     Added MPI2_SCSITASKMGMT_RSP_TM_OVERLAPPED_TAG define.
 * 02-10-10  02.00.09  Removed unused structure that had "#if 0" around it.
 * 05-12-10  02.00.10  Added optional vendor-unique region to SCSI IO Request.
 * 11-10-10  02.00.11  Added MPI2_SCSIIO_NUM_SGLOFFSETS define.
 * 11-18-11  02.00.12  Incorporating additions for MPI v2.5.
 * 02-06-12  02.00.13  Added alternate defines for Task Priority / Command
 *                     Priority to match SAM-4.
 *                     Added EEDPErrorOffset to MPI2_SCSI_IO_REPLY.
 * 07-10-12  02.00.14  Added MPI2_SCSIIO_CONTROL_SHIFT_DATADIRECTION.
 * 04-09-13  02.00.15  Added SCSIStatusQualifier field to MPI2_SCSI_IO_REPLY,
 *                     replacing the Reserved4 field.
 * 11-18-14  02.00.16  Updated copyright information.
 * 03-16-15  02.00.17  Updated for MPI v2.6.
 *                     Added MPI26_SCSIIO_IOFLAGS_ESCAPE_PASSTHROUGH.
 *                     Added MPI2_SEP_REQ_SLOTSTATUS_DEV_OFF and
 *                     MPI2_SEP_REPLY_SLOTSTATUS_DEV_OFF.
 * 08-26-15  02.00.18  Added SCSITASKMGMT_MSGFLAGS for Target Reset.
 * 12-18-15  02.00.19  Added EEDPObservedValue added to SCSI IO Reply message.
 * 01-04-16  02.00.20  Modified EEDP reported values in SCSI IO Reply message.
 * --------------------------------------------------------------------------
 */

#ifndef MPI2_INIT_H
#define MPI2_INIT_H

/*****************************************************************************
*
*              SCSI Initiator Messages
*
*****************************************************************************/

/****************************************************************************
* SCSI IO messages and associated structures
****************************************************************************/

typedef struct _MPI2_SCSI_IO_CDB_EEDP32 {
	U8 CDB[20];		/*0x00 */
	U32 PrimaryReferenceTag;	/*0x14 */
	U16 PrimaryApplicationTag;	/*0x18 */
	U16 PrimaryApplicationTagMask;	/*0x1A */
	U32 TransferLength;	/*0x1C */
} MPI2_SCSI_IO_CDB_EEDP32, *PTR_MPI2_SCSI_IO_CDB_EEDP32,
	Mpi2ScsiIoCdbEedp32_t, *pMpi2ScsiIoCdbEedp32_t;

/*MPI v2.0 CDB field */
typedef union _MPI2_SCSI_IO_CDB_UNION {
	U8 CDB32[32];
	MPI2_SCSI_IO_CDB_EEDP32 EEDP32;
	MPI2_SGE_SIMPLE_UNION SGE;
} MPI2_SCSI_IO_CDB_UNION, *PTR_MPI2_SCSI_IO_CDB_UNION,
	Mpi2ScsiIoCdb_t, *pMpi2ScsiIoCdb_t;

/*MPI v2.0 SCSI IO Request Message */
typedef struct _MPI2_SCSI_IO_REQUEST {
	U16 DevHandle;		/*0x00 */
	U8 ChainOffset;		/*0x02 */
	U8 Function;		/*0x03 */
	U16 Reserved1;		/*0x04 */
	U8 Reserved2;		/*0x06 */
	U8 MsgFlags;		/*0x07 */
	U8 VP_ID;		/*0x08 */
	U8 VF_ID;		/*0x09 */
	U16 Reserved3;		/*0x0A */
	U32 SenseBufferLowAddress;	/*0x0C */
	U16 SGLFlags;		/*0x10 */
	U8 SenseBufferLength;	/*0x12 */
	U8 Reserved4;		/*0x13 */
	U8 SGLOffset0;		/*0x14 */
	U8 SGLOffset1;		/*0x15 */
	U8 SGLOffset2;		/*0x16 */
	U8 SGLOffset3;		/*0x17 */
	U32 SkipCount;		/*0x18 */
	U32 DataLength;		/*0x1C */
	U32 BidirectionalDataLength;	/*0x20 */
	U16 IoFlags;		/*0x24 */
	U16 EEDPFlags;		/*0x26 */
	U32 EEDPBlockSize;	/*0x28 */
	U32 SecondaryReferenceTag;	/*0x2C */
	U16 SecondaryApplicationTag;	/*0x30 */
	U16 ApplicationTagTranslationMask;	/*0x32 */
	U8 LUN[8];		/*0x34 */
	U32 Control;		/*0x3C */
	MPI2_SCSI_IO_CDB_UNION CDB;	/*0x40 */

#ifdef MPI2_SCSI_IO_VENDOR_UNIQUE_REGION /*typically this is left undefined */
	MPI2_SCSI_IO_VENDOR_UNIQUE VendorRegion;
#endif

	MPI2_SGE_IO_UNION SGL;	/*0x60 */

} MPI2_SCSI_IO_REQUEST, *PTR_MPI2_SCSI_IO_REQUEST,
	Mpi2SCSIIORequest_t, *pMpi2SCSIIORequest_t;

/*SCSI IO MsgFlags bits */

/*MsgFlags for SenseBufferAddressSpace */
#define MPI2_SCSIIO_MSGFLAGS_MASK_SENSE_ADDR        (0x0C)
#define MPI2_SCSIIO_MSGFLAGS_SYSTEM_SENSE_ADDR      (0x00)
#define MPI2_SCSIIO_MSGFLAGS_IOCDDR_SENSE_ADDR      (0x04)
#define MPI2_SCSIIO_MSGFLAGS_IOCPLB_SENSE_ADDR      (0x08)
#define MPI2_SCSIIO_MSGFLAGS_IOCPLBNTA_SENSE_ADDR   (0x0C)
#define MPI26_SCSIIO_MSGFLAGS_IOCCTL_SENSE_ADDR     (0x08)

/*SCSI IO SGLFlags bits */

/*base values for Data Location Address Space */
#define MPI2_SCSIIO_SGLFLAGS_ADDR_MASK              (0x0C)
#define MPI2_SCSIIO_SGLFLAGS_SYSTEM_ADDR            (0x00)
#define MPI2_SCSIIO_SGLFLAGS_IOCDDR_ADDR            (0x04)
#define MPI2_SCSIIO_SGLFLAGS_IOCPLB_ADDR            (0x08)
#define MPI2_SCSIIO_SGLFLAGS_IOCPLBNTA_ADDR         (0x0C)

/*base values for Type */
#define MPI2_SCSIIO_SGLFLAGS_TYPE_MASK              (0x03)
#define MPI2_SCSIIO_SGLFLAGS_TYPE_MPI               (0x00)
#define MPI2_SCSIIO_SGLFLAGS_TYPE_IEEE32            (0x01)
#define MPI2_SCSIIO_SGLFLAGS_TYPE_IEEE64            (0x02)

/*shift values for each sub-field */
#define MPI2_SCSIIO_SGLFLAGS_SGL3_SHIFT             (12)
#define MPI2_SCSIIO_SGLFLAGS_SGL2_SHIFT             (8)
#define MPI2_SCSIIO_SGLFLAGS_SGL1_SHIFT             (4)
#define MPI2_SCSIIO_SGLFLAGS_SGL0_SHIFT             (0)

/*number of SGLOffset fields */
#define MPI2_SCSIIO_NUM_SGLOFFSETS                  (4)

/*SCSI IO IoFlags bits */

/*Large CDB Address Space */
#define MPI2_SCSIIO_CDB_ADDR_MASK                   (0x6000)
#define MPI2_SCSIIO_CDB_ADDR_SYSTEM                 (0x0000)
#define MPI2_SCSIIO_CDB_ADDR_IOCDDR                 (0x2000)
#define MPI2_SCSIIO_CDB_ADDR_IOCPLB                 (0x4000)
#define MPI2_SCSIIO_CDB_ADDR_IOCPLBNTA              (0x6000)

#define MPI2_SCSIIO_IOFLAGS_LARGE_CDB               (0x1000)
#define MPI2_SCSIIO_IOFLAGS_BIDIRECTIONAL           (0x0800)
#define MPI2_SCSIIO_IOFLAGS_MULTICAST               (0x0400)
#define MPI2_SCSIIO_IOFLAGS_CMD_DETERMINES_DATA_DIR (0x0200)
#define MPI2_SCSIIO_IOFLAGS_CDBLENGTH_MASK          (0x01FF)

/*SCSI IO EEDPFlags bits */

#define MPI2_SCSIIO_EEDPFLAGS_INC_PRI_REFTAG        (0x8000)
#define MPI2_SCSIIO_EEDPFLAGS_INC_SEC_REFTAG        (0x4000)
#define MPI2_SCSIIO_EEDPFLAGS_INC_PRI_APPTAG        (0x2000)
#define MPI2_SCSIIO_EEDPFLAGS_INC_SEC_APPTAG        (0x1000)

#define MPI2_SCSIIO_EEDPFLAGS_CHECK_REFTAG          (0x0400)
#define MPI2_SCSIIO_EEDPFLAGS_CHECK_APPTAG          (0x0200)
#define MPI2_SCSIIO_EEDPFLAGS_CHECK_GUARD           (0x0100)

#define MPI2_SCSIIO_EEDPFLAGS_PASSTHRU_REFTAG       (0x0008)

#define MPI2_SCSIIO_EEDPFLAGS_MASK_OP               (0x0007)
#define MPI2_SCSIIO_EEDPFLAGS_NOOP_OP               (0x0000)
#define MPI2_SCSIIO_EEDPFLAGS_CHECK_OP              (0x0001)
#define MPI2_SCSIIO_EEDPFLAGS_STRIP_OP              (0x0002)
#define MPI2_SCSIIO_EEDPFLAGS_CHECK_REMOVE_OP       (0x0003)
#define MPI2_SCSIIO_EEDPFLAGS_INSERT_OP             (0x0004)
#define MPI2_SCSIIO_EEDPFLAGS_REPLACE_OP            (0x0006)
#define MPI2_SCSIIO_EEDPFLAGS_CHECK_REGEN_OP        (0x0007)

/*SCSI IO LUN fields: use MPI2_LUN_ from mpi2.h */

/*SCSI IO Control bits */
#define MPI2_SCSIIO_CONTROL_ADDCDBLEN_MASK      (0xFC000000)
#define MPI2_SCSIIO_CONTROL_ADDCDBLEN_SHIFT     (26)

#define MPI2_SCSIIO_CONTROL_DATADIRECTION_MASK  (0x03000000)
#define MPI2_SCSIIO_CONTROL_SHIFT_DATADIRECTION (24)
#define MPI2_SCSIIO_CONTROL_NODATATRANSFER      (0x00000000)
#define MPI2_SCSIIO_CONTROL_WRITE               (0x01000000)
#define MPI2_SCSIIO_CONTROL_READ                (0x02000000)
#define MPI2_SCSIIO_CONTROL_BIDIRECTIONAL       (0x03000000)

#define MPI2_SCSIIO_CONTROL_TASKPRI_MASK        (0x00007800)
#define MPI2_SCSIIO_CONTROL_TASKPRI_SHIFT       (11)
/*alternate name for the previous field; called Command Priority in SAM-4 */
#define MPI2_SCSIIO_CONTROL_CMDPRI_MASK         (0x00007800)
#define MPI2_SCSIIO_CONTROL_CMDPRI_SHIFT        (11)

#define MPI2_SCSIIO_CONTROL_TASKATTRIBUTE_MASK  (0x00000700)
#define MPI2_SCSIIO_CONTROL_SIMPLEQ             (0x00000000)
#define MPI2_SCSIIO_CONTROL_HEADOFQ             (0x00000100)
#define MPI2_SCSIIO_CONTROL_ORDEREDQ            (0x00000200)
#define MPI2_SCSIIO_CONTROL_ACAQ                (0x00000400)

#define MPI2_SCSIIO_CONTROL_TLR_MASK            (0x000000C0)
#define MPI2_SCSIIO_CONTROL_NO_TLR              (0x00000000)
#define MPI2_SCSIIO_CONTROL_TLR_ON              (0x00000040)
#define MPI2_SCSIIO_CONTROL_TLR_OFF             (0x00000080)

/*MPI v2.5 CDB field */
typedef union _MPI25_SCSI_IO_CDB_UNION {
	U8 CDB32[32];
	MPI2_SCSI_IO_CDB_EEDP32 EEDP32;
	MPI2_IEEE_SGE_SIMPLE64 SGE;
} MPI25_SCSI_IO_CDB_UNION, *PTR_MPI25_SCSI_IO_CDB_UNION,
	Mpi25ScsiIoCdb_t, *pMpi25ScsiIoCdb_t;

/*MPI v2.5/2.6 SCSI IO Request Message */
typedef struct _MPI25_SCSI_IO_REQUEST {
	U16 DevHandle;		/*0x00 */
	U8 ChainOffset;		/*0x02 */
	U8 Function;		/*0x03 */
	U16 Reserved1;		/*0x04 */
	U8 Reserved2;		/*0x06 */
	U8 MsgFlags;		/*0x07 */
	U8 VP_ID;		/*0x08 */
	U8 VF_ID;		/*0x09 */
	U16 Reserved3;		/*0x0A */
	U32 SenseBufferLowAddress;	/*0x0C */
	U8 DMAFlags;		/*0x10 */
	U8 Reserved5;		/*0x11 */
	U8 SenseBufferLength;	/*0x12 */
	U8 Reserved4;		/*0x13 */
	U8 SGLOffset0;		/*0x14 */
	U8 SGLOffset1;		/*0x15 */
	U8 SGLOffset2;		/*0x16 */
	U8 SGLOffset3;		/*0x17 */
	U32 SkipCount;		/*0x18 */
	U32 DataLength;		/*0x1C */
	U32 BidirectionalDataLength;	/*0x20 */
	U16 IoFlags;		/*0x24 */
	U16 EEDPFlags;		/*0x26 */
	U16 EEDPBlockSize;	/*0x28 */
	U16 Reserved6;		/*0x2A */
	U32 SecondaryReferenceTag;	/*0x2C */
	U16 SecondaryApplicationTag;	/*0x30 */
	U16 ApplicationTagTranslationMask;	/*0x32 */
	U8 LUN[8];		/*0x34 */
	U32 Control;		/*0x3C */
	MPI25_SCSI_IO_CDB_UNION CDB;	/*0x40 */

#ifdef MPI25_SCSI_IO_VENDOR_UNIQUE_REGION /*typically this is left undefined */
	MPI25_SCSI_IO_VENDOR_UNIQUE VendorRegion;
#endif

	MPI25_SGE_IO_UNION SGL;	/*0x60 */

} MPI25_SCSI_IO_REQUEST, *PTR_MPI25_SCSI_IO_REQUEST,
	Mpi25SCSIIORequest_t, *pMpi25SCSIIORequest_t;

/*use MPI2_SCSIIO_MSGFLAGS_ defines for the MsgFlags field */

/*Defines for the DMAFlags field
 * Each setting affects 4 SGLS, from SGL0 to SGL3.
 *     D = Data
 *     C = Cache DIF
 *     I = Interleaved
 *     H = Host DIF
 */
#define MPI25_SCSIIO_DMAFLAGS_OP_MASK               (0x0F)
#define MPI25_SCSIIO_DMAFLAGS_OP_D_D_D_D            (0x00)
#define MPI25_SCSIIO_DMAFLAGS_OP_D_D_D_C            (0x01)
#define MPI25_SCSIIO_DMAFLAGS_OP_D_D_D_I            (0x02)
#define MPI25_SCSIIO_DMAFLAGS_OP_D_D_C_C            (0x03)
#define MPI25_SCSIIO_DMAFLAGS_OP_D_D_C_I            (0x04)
#define MPI25_SCSIIO_DMAFLAGS_OP_D_D_I_I            (0x05)
#define MPI25_SCSIIO_DMAFLAGS_OP_D_C_C_C            (0x06)
#define MPI25_SCSIIO_DMAFLAGS_OP_D_C_C_I            (0x07)
#define MPI25_SCSIIO_DMAFLAGS_OP_D_C_I_I            (0x08)
#define MPI25_SCSIIO_DMAFLAGS_OP_D_I_I_I            (0x09)
#define MPI25_SCSIIO_DMAFLAGS_OP_D_H_D_D            (0x0A)
#define MPI25_SCSIIO_DMAFLAGS_OP_D_H_D_C            (0x0B)
#define MPI25_SCSIIO_DMAFLAGS_OP_D_H_D_I            (0x0C)
#define MPI25_SCSIIO_DMAFLAGS_OP_D_H_C_C            (0x0D)
#define MPI25_SCSIIO_DMAFLAGS_OP_D_H_C_I            (0x0E)
#define MPI25_SCSIIO_DMAFLAGS_OP_D_H_I_I            (0x0F)

/*number of SGLOffset fields */
#define MPI25_SCSIIO_NUM_SGLOFFSETS                 (4)

/*defines for the IoFlags field */
#define MPI25_SCSIIO_IOFLAGS_IO_PATH_MASK               (0xC000)
#define MPI25_SCSIIO_IOFLAGS_NORMAL_PATH                (0x0000)
#define MPI25_SCSIIO_IOFLAGS_FAST_PATH                  (0x4000)

#define MPI26_SCSIIO_IOFLAGS_ESCAPE_PASSTHROUGH         (0x2000)
#define MPI25_SCSIIO_IOFLAGS_LARGE_CDB                  (0x1000)
#define MPI25_SCSIIO_IOFLAGS_BIDIRECTIONAL              (0x0800)
#define MPI26_SCSIIO_IOFLAGS_PORT_REQUEST               (0x0400)
#define MPI25_SCSIIO_IOFLAGS_CDBLENGTH_MASK             (0x01FF)

/*MPI v2.5 defines for the EEDPFlags bits */
/*use MPI2_SCSIIO_EEDPFLAGS_ defines for the other EEDPFlags bits */
#define MPI25_SCSIIO_EEDPFLAGS_ESCAPE_MODE_MASK             (0x00C0)
#define MPI25_SCSIIO_EEDPFLAGS_COMPATIBLE_MODE              (0x0000)
#define MPI25_SCSIIO_EEDPFLAGS_DO_NOT_DISABLE_MODE          (0x0040)
#define MPI25_SCSIIO_EEDPFLAGS_APPTAG_DISABLE_MODE          (0x0080)
#define MPI25_SCSIIO_EEDPFLAGS_APPTAG_REFTAG_DISABLE_MODE   (0x00C0)

#define MPI25_SCSIIO_EEDPFLAGS_HOST_GUARD_METHOD_MASK       (0x0030)
#define MPI25_SCSIIO_EEDPFLAGS_T10_CRC_HOST_GUARD           (0x0000)
#define MPI25_SCSIIO_EEDPFLAGS_IP_CHKSUM_HOST_GUARD         (0x0010)

/*use MPI2_LUN_ defines from mpi2.h for the LUN field */

/*use MPI2_SCSIIO_CONTROL_ defines for the Control field */

/*NOTE: The SCSI IO Reply is nearly the same for MPI 2.0 and MPI 2.5, so
 *      MPI2_SCSI_IO_REPLY is used for both.
 */

/*SCSI IO Error Reply Message */
typedef struct _MPI2_SCSI_IO_REPLY {
	U16 DevHandle;		/*0x00 */
	U8 MsgLength;		/*0x02 */
	U8 Function;		/*0x03 */
	U16 Reserved1;		/*0x04 */
	U8 Reserved2;		/*0x06 */
	U8 MsgFlags;		/*0x07 */
	U8 VP_ID;		/*0x08 */
	U8 VF_ID;		/*0x09 */
	U16 Reserved3;		/*0x0A */
	U8 SCSIStatus;		/*0x0C */
	U8 SCSIState;		/*0x0D */
	U16 IOCStatus;		/*0x0E */
	U32 IOCLogInfo;		/*0x10 */
	U32 TransferCount;	/*0x14 */
	U32 SenseCount;		/*0x18 */
	U32 ResponseInfo;	/*0x1C */
	U16 TaskTag;		/*0x20 */
	U16 SCSIStatusQualifier; /* 0x22 */
	U32 BidirectionalTransferCount;	/*0x24 */
 /* MPI 2.5+ only; Reserved in MPI 2.0 */
	U32 EEDPErrorOffset;	/* 0x28 */
 /* MPI 2.5+ only; Reserved in MPI 2.0 */
	U16 EEDPObservedAppTag;	/* 0x2C */
 /* MPI 2.5+ only; Reserved in MPI 2.0 */
	U16 EEDPObservedGuard;	/* 0x2E */
 /* MPI 2.5+ only; Reserved in MPI 2.0 */
	U32 EEDPObservedRefTag;	/* 0x30 */
} MPI2_SCSI_IO_REPLY, *PTR_MPI2_SCSI_IO_REPLY,
	Mpi2SCSIIOReply_t, *pMpi2SCSIIOReply_t;

/*SCSI IO Reply SCSIStatus values (SAM-4 status codes) */

#define MPI2_SCSI_STATUS_GOOD                   (0x00)
#define MPI2_SCSI_STATUS_CHECK_CONDITION        (0x02)
#define MPI2_SCSI_STATUS_CONDITION_MET          (0x04)
#define MPI2_SCSI_STATUS_BUSY                   (0x08)
#define MPI2_SCSI_STATUS_INTERMEDIATE           (0x10)
#define MPI2_SCSI_STATUS_INTERMEDIATE_CONDMET   (0x14)
#define MPI2_SCSI_STATUS_RESERVATION_CONFLICT   (0x18)
#define MPI2_SCSI_STATUS_COMMAND_TERMINATED     (0x22)	/*obsolete */
#define MPI2_SCSI_STATUS_TASK_SET_FULL          (0x28)
#define MPI2_SCSI_STATUS_ACA_ACTIVE             (0x30)
#define MPI2_SCSI_STATUS_TASK_ABORTED           (0x40)

/*SCSI IO Reply SCSIState flags */

#define MPI2_SCSI_STATE_RESPONSE_INFO_VALID     (0x10)
#define MPI2_SCSI_STATE_TERMINATED              (0x08)
#define MPI2_SCSI_STATE_NO_SCSI_STATUS          (0x04)
#define MPI2_SCSI_STATE_AUTOSENSE_FAILED        (0x02)
#define MPI2_SCSI_STATE_AUTOSENSE_VALID         (0x01)

/*masks and shifts for the ResponseInfo field */

#define MPI2_SCSI_RI_MASK_REASONCODE            (0x000000FF)
#define MPI2_SCSI_RI_SHIFT_REASONCODE           (0)

#define MPI2_SCSI_TASKTAG_UNKNOWN               (0xFFFF)

/****************************************************************************
* SCSI Task Management messages
****************************************************************************/

/*SCSI Task Management Request Message */
typedef struct _MPI2_SCSI_TASK_MANAGE_REQUEST {
	U16 DevHandle;		/*0x00 */
	U8 ChainOffset;		/*0x02 */
	U8 Function;		/*0x03 */
	U8 Reserved1;		/*0x04 */
	U8 TaskType;		/*0x05 */
	U8 Reserved2;		/*0x06 */
	U8 MsgFlags;		/*0x07 */
	U8 VP_ID;		/*0x08 */
	U8 VF_ID;		/*0x09 */
	U16 Reserved3;		/*0x0A */
	U8 LUN[8];		/*0x0C */
	U32 Reserved4[7];	/*0x14 */
	U16 TaskMID;		/*0x30 */
	U16 Reserved5;		/*0x32 */
} MPI2_SCSI_TASK_MANAGE_REQUEST,
	*PTR_MPI2_SCSI_TASK_MANAGE_REQUEST,
	Mpi2SCSITaskManagementRequest_t,
	*pMpi2SCSITaskManagementRequest_t;

/*TaskType values */

#define MPI2_SCSITASKMGMT_TASKTYPE_ABORT_TASK           (0x01)
#define MPI2_SCSITASKMGMT_TASKTYPE_ABRT_TASK_SET        (0x02)
#define MPI2_SCSITASKMGMT_TASKTYPE_TARGET_RESET         (0x03)
#define MPI2_SCSITASKMGMT_TASKTYPE_LOGICAL_UNIT_RESET   (0x05)
#define MPI2_SCSITASKMGMT_TASKTYPE_CLEAR_TASK_SET       (0x06)
#define MPI2_SCSITASKMGMT_TASKTYPE_QUERY_TASK           (0x07)
#define MPI2_SCSITASKMGMT_TASKTYPE_CLR_ACA              (0x08)
#define MPI2_SCSITASKMGMT_TASKTYPE_QRY_TASK_SET         (0x09)
#define MPI2_SCSITASKMGMT_TASKTYPE_QRY_ASYNC_EVENT      (0x0A)

/*obsolete TaskType name */
#define MPI2_SCSITASKMGMT_TASKTYPE_QRY_UNIT_ATTENTION \
		(MPI2_SCSITASKMGMT_TASKTYPE_QRY_ASYNC_EVENT)

/*MsgFlags bits */

#define MPI2_SCSITASKMGMT_MSGFLAGS_MASK_TARGET_RESET    (0x18)
#define MPI2_SCSITASKMGMT_MSGFLAGS_LINK_RESET           (0x00)
#define MPI2_SCSITASKMGMT_MSGFLAGS_NEXUS_RESET_SRST     (0x08)
#define MPI2_SCSITASKMGMT_MSGFLAGS_SAS_HARD_LINK_RESET  (0x10)

#define MPI2_SCSITASKMGMT_MSGFLAGS_DO_NOT_SEND_TASK_IU  (0x01)

/*SCSI Task Management Reply Message */
typedef struct _MPI2_SCSI_TASK_MANAGE_REPLY {
	U16 DevHandle;		/*0x00 */
	U8 MsgLength;		/*0x02 */
	U8 Function;		/*0x03 */
	U8 ResponseCode;	/*0x04 */
	U8 TaskType;		/*0x05 */
	U8 Reserved1;		/*0x06 */
	U8 MsgFlags;		/*0x07 */
	U8 VP_ID;		/*0x08 */
	U8 VF_ID;		/*0x09 */
	U16 Reserved2;		/*0x0A */
	U16 Reserved3;		/*0x0C */
	U16 IOCStatus;		/*0x0E */
	U32 IOCLogInfo;		/*0x10 */
	U32 TerminationCount;	/*0x14 */
	U32 ResponseInfo;	/*0x18 */
} MPI2_SCSI_TASK_MANAGE_REPLY,
	*PTR_MPI2_SCSI_TASK_MANAGE_REPLY,
	Mpi2SCSITaskManagementReply_t, *pMpi2SCSIManagementReply_t;

/*ResponseCode values */

#define MPI2_SCSITASKMGMT_RSP_TM_COMPLETE               (0x00)
#define MPI2_SCSITASKMGMT_RSP_INVALID_FRAME             (0x02)
#define MPI2_SCSITASKMGMT_RSP_TM_NOT_SUPPORTED          (0x04)
#define MPI2_SCSITASKMGMT_RSP_TM_FAILED                 (0x05)
#define MPI2_SCSITASKMGMT_RSP_TM_SUCCEEDED              (0x08)
#define MPI2_SCSITASKMGMT_RSP_TM_INVALID_LUN            (0x09)
#define MPI2_SCSITASKMGMT_RSP_TM_OVERLAPPED_TAG         (0x0A)
#define MPI2_SCSITASKMGMT_RSP_IO_QUEUED_ON_IOC          (0x80)

/*masks and shifts for the ResponseInfo field */

#define MPI2_SCSITASKMGMT_RI_MASK_REASONCODE            (0x000000FF)
#define MPI2_SCSITASKMGMT_RI_SHIFT_REASONCODE           (0)
#define MPI2_SCSITASKMGMT_RI_MASK_ARI2                  (0x0000FF00)
#define MPI2_SCSITASKMGMT_RI_SHIFT_ARI2                 (8)
#define MPI2_SCSITASKMGMT_RI_MASK_ARI1                  (0x00FF0000)
#define MPI2_SCSITASKMGMT_RI_SHIFT_ARI1                 (16)
#define MPI2_SCSITASKMGMT_RI_MASK_ARI0                  (0xFF000000)
#define MPI2_SCSITASKMGMT_RI_SHIFT_ARI0                 (24)

/****************************************************************************
* SCSI Enclosure Processor messages
****************************************************************************/

/*SCSI Enclosure Processor Request Message */
typedef struct _MPI2_SEP_REQUEST {
	U16 DevHandle;		/*0x00 */
	U8 ChainOffset;		/*0x02 */
	U8 Function;		/*0x03 */
	U8 Action;		/*0x04 */
	U8 Flags;		/*0x05 */
	U8 Reserved1;		/*0x06 */
	U8 MsgFlags;		/*0x07 */
	U8 VP_ID;		/*0x08 */
	U8 VF_ID;		/*0x09 */
	U16 Reserved2;		/*0x0A */
	U32 SlotStatus;		/*0x0C */
	U32 Reserved3;		/*0x10 */
	U32 Reserved4;		/*0x14 */
	U32 Reserved5;		/*0x18 */
	U16 Slot;		/*0x1C */
	U16 EnclosureHandle;	/*0x1E */
} MPI2_SEP_REQUEST, *PTR_MPI2_SEP_REQUEST,
	Mpi2SepRequest_t, *pMpi2SepRequest_t;

/*Action defines */
#define MPI2_SEP_REQ_ACTION_WRITE_STATUS                (0x00)
#define MPI2_SEP_REQ_ACTION_READ_STATUS                 (0x01)

/*Flags defines */
#define MPI2_SEP_REQ_FLAGS_DEVHANDLE_ADDRESS            (0x00)
#define MPI2_SEP_REQ_FLAGS_ENCLOSURE_SLOT_ADDRESS       (0x01)

/*SlotStatus defines */
#define MPI2_SEP_REQ_SLOTSTATUS_DEV_OFF                 (0x00080000)
#define MPI2_SEP_REQ_SLOTSTATUS_REQUEST_REMOVE          (0x00040000)
#define MPI2_SEP_REQ_SLOTSTATUS_IDENTIFY_REQUEST        (0x00020000)
#define MPI2_SEP_REQ_SLOTSTATUS_REBUILD_STOPPED         (0x00000200)
#define MPI2_SEP_REQ_SLOTSTATUS_HOT_SPARE               (0x00000100)
#define MPI2_SEP_REQ_SLOTSTATUS_UNCONFIGURED            (0x00000080)
#define MPI2_SEP_REQ_SLOTSTATUS_PREDICTED_FAULT         (0x00000040)
#define MPI2_SEP_REQ_SLOTSTATUS_IN_CRITICAL_ARRAY       (0x00000010)
#define MPI2_SEP_REQ_SLOTSTATUS_IN_FAILED_ARRAY         (0x00000008)
#define MPI2_SEP_REQ_SLOTSTATUS_DEV_REBUILDING          (0x00000004)
#define MPI2_SEP_REQ_SLOTSTATUS_DEV_FAULTY              (0x00000002)
#define MPI2_SEP_REQ_SLOTSTATUS_NO_ERROR                (0x00000001)

/*SCSI Enclosure Processor Reply Message */
typedef struct _MPI2_SEP_REPLY {
	U16 DevHandle;		/*0x00 */
	U8 MsgLength;		/*0x02 */
	U8 Function;		/*0x03 */
	U8 Action;		/*0x04 */
	U8 Flags;		/*0x05 */
	U8 Reserved1;		/*0x06 */
	U8 MsgFlags;		/*0x07 */
	U8 VP_ID;		/*0x08 */
	U8 VF_ID;		/*0x09 */
	U16 Reserved2;		/*0x0A */
	U16 Reserved3;		/*0x0C */
	U16 IOCStatus;		/*0x0E */
	U32 IOCLogInfo;		/*0x10 */
	U32 SlotStatus;		/*0x14 */
	U32 Reserved4;		/*0x18 */
	U16 Slot;		/*0x1C */
	U16 EnclosureHandle;	/*0x1E */
} MPI2_SEP_REPLY, *PTR_MPI2_SEP_REPLY,
	Mpi2SepReply_t, *pMpi2SepReply_t;

/*SlotStatus defines */
#define MPI2_SEP_REPLY_SLOTSTATUS_DEV_OFF               (0x00080000)
#define MPI2_SEP_REPLY_SLOTSTATUS_REMOVE_READY          (0x00040000)
#define MPI2_SEP_REPLY_SLOTSTATUS_IDENTIFY_REQUEST      (0x00020000)
#define MPI2_SEP_REPLY_SLOTSTATUS_REBUILD_STOPPED       (0x00000200)
#define MPI2_SEP_REPLY_SLOTSTATUS_HOT_SPARE             (0x00000100)
#define MPI2_SEP_REPLY_SLOTSTATUS_UNCONFIGURED          (0x00000080)
#define MPI2_SEP_REPLY_SLOTSTATUS_PREDICTED_FAULT       (0x00000040)
#define MPI2_SEP_REPLY_SLOTSTATUS_IN_CRITICAL_ARRAY     (0x00000010)
#define MPI2_SEP_REPLY_SLOTSTATUS_IN_FAILED_ARRAY       (0x00000008)
#define MPI2_SEP_REPLY_SLOTSTATUS_DEV_REBUILDING        (0x00000004)
#define MPI2_SEP_REPLY_SLOTSTATUS_DEV_FAULTY            (0x00000002)
#define MPI2_SEP_REPLY_SLOTSTATUS_NO_ERROR              (0x00000001)

#endif
