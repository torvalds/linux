/*
 * Copyright 2000-2015 Avago Technologies.  All rights reserved.
 *
 *
 *          Name:  mpi2.h
 *         Title:  MPI Message independent structures and definitions
 *                 including System Interface Register Set and
 *                 scatter/gather formats.
 * Creation Date:  June 21, 2006
 *
 * mpi2.h Version:  02.00.39
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
 * 06-04-07  02.00.01  Bumped MPI2_HEADER_VERSION_UNIT.
 * 06-26-07  02.00.02  Bumped MPI2_HEADER_VERSION_UNIT.
 * 08-31-07  02.00.03  Bumped MPI2_HEADER_VERSION_UNIT.
 *                     Moved ReplyPostHostIndex register to offset 0x6C of the
 *                     MPI2_SYSTEM_INTERFACE_REGS and modified the define for
 *                     MPI2_REPLY_POST_HOST_INDEX_OFFSET.
 *                     Added union of request descriptors.
 *                     Added union of reply descriptors.
 * 10-31-07  02.00.04  Bumped MPI2_HEADER_VERSION_UNIT.
 *                     Added define for MPI2_VERSION_02_00.
 *                     Fixed the size of the FunctionDependent5 field in the
 *                     MPI2_DEFAULT_REPLY structure.
 * 12-18-07  02.00.05  Bumped MPI2_HEADER_VERSION_UNIT.
 *                     Removed the MPI-defined Fault Codes and extended the
 *                     product specific codes up to 0xEFFF.
 *                     Added a sixth key value for the WriteSequence register
 *                     and changed the flush value to 0x0.
 *                     Added message function codes for Diagnostic Buffer Post
 *                     and Diagnsotic Release.
 *                     New IOCStatus define: MPI2_IOCSTATUS_DIAGNOSTIC_RELEASED
 *                     Moved MPI2_VERSION_UNION from mpi2_ioc.h.
 * 02-29-08  02.00.06  Bumped MPI2_HEADER_VERSION_UNIT.
 * 03-03-08  02.00.07  Bumped MPI2_HEADER_VERSION_UNIT.
 * 05-21-08  02.00.08  Bumped MPI2_HEADER_VERSION_UNIT.
 *                     Added #defines for marking a reply descriptor as unused.
 * 06-27-08  02.00.09  Bumped MPI2_HEADER_VERSION_UNIT.
 * 10-02-08  02.00.10  Bumped MPI2_HEADER_VERSION_UNIT.
 *                     Moved LUN field defines from mpi2_init.h.
 * 01-19-09  02.00.11  Bumped MPI2_HEADER_VERSION_UNIT.
 * 05-06-09  02.00.12  Bumped MPI2_HEADER_VERSION_UNIT.
 *                     In all request and reply descriptors, replaced VF_ID
 *                     field with MSIxIndex field.
 *                     Removed DevHandle field from
 *                     MPI2_SCSI_IO_SUCCESS_REPLY_DESCRIPTOR and made those
 *                     bytes reserved.
 *                     Added RAID Accelerator functionality.
 * 07-30-09  02.00.13  Bumped MPI2_HEADER_VERSION_UNIT.
 * 10-28-09  02.00.14  Bumped MPI2_HEADER_VERSION_UNIT.
 *                     Added MSI-x index mask and shift for Reply Post Host
 *                     Index register.
 *                     Added function code for Host Based Discovery Action.
 * 02-10-10  02.00.15  Bumped MPI2_HEADER_VERSION_UNIT.
 *                     Added define for MPI2_FUNCTION_PWR_MGMT_CONTROL.
 *                     Added defines for product-specific range of message
 *                     function codes, 0xF0 to 0xFF.
 * 05-12-10  02.00.16  Bumped MPI2_HEADER_VERSION_UNIT.
 *                     Added alternative defines for the SGE Direction bit.
 * 08-11-10  02.00.17  Bumped MPI2_HEADER_VERSION_UNIT.
 * 11-10-10  02.00.18  Bumped MPI2_HEADER_VERSION_UNIT.
 *                     Added MPI2_IEEE_SGE_FLAGS_SYSTEMPLBCPI_ADDR define.
 * 02-23-11  02.00.19  Bumped MPI2_HEADER_VERSION_UNIT.
 *                     Added MPI2_FUNCTION_SEND_HOST_MESSAGE.
 * 03-09-11  02.00.20  Bumped MPI2_HEADER_VERSION_UNIT.
 * 05-25-11  02.00.21  Bumped MPI2_HEADER_VERSION_UNIT.
 * 08-24-11  02.00.22  Bumped MPI2_HEADER_VERSION_UNIT.
 * 11-18-11  02.00.23  Bumped MPI2_HEADER_VERSION_UNIT.
 *                     Incorporating additions for MPI v2.5.
 * 02-06-12  02.00.24  Bumped MPI2_HEADER_VERSION_UNIT.
 * 03-29-12  02.00.25  Bumped MPI2_HEADER_VERSION_UNIT.
 *                     Added Hard Reset delay timings.
 * 07-10-12  02.00.26  Bumped MPI2_HEADER_VERSION_UNIT.
 * 07-26-12  02.00.27  Bumped MPI2_HEADER_VERSION_UNIT.
 * 11-27-12  02.00.28  Bumped MPI2_HEADER_VERSION_UNIT.
 * 12-20-12  02.00.29  Bumped MPI2_HEADER_VERSION_UNIT.
 *                     Added MPI25_SUP_REPLY_POST_HOST_INDEX_OFFSET.
 * 04-09-13  02.00.30  Bumped MPI2_HEADER_VERSION_UNIT.
 * 04-17-13  02.00.31  Bumped MPI2_HEADER_VERSION_UNIT.
 * 08-19-13  02.00.32  Bumped MPI2_HEADER_VERSION_UNIT.
 * 12-05-13  02.00.33  Bumped MPI2_HEADER_VERSION_UNIT.
 * 01-08-14  02.00.34  Bumped MPI2_HEADER_VERSION_UNIT
 * 06-13-14  02.00.35  Bumped MPI2_HEADER_VERSION_UNIT.
 * 11-18-14  02.00.36  Updated copyright information.
 *                     Bumped MPI2_HEADER_VERSION_UNIT.
 * 03-16-15  02.00.37  Bumped MPI2_HEADER_VERSION_UNIT.
 *                     Added Scratchpad registers to
 *                     MPI2_SYSTEM_INTERFACE_REGS.
 *                     Added MPI2_DIAG_SBR_RELOAD.
 * 03-19-15  02.00.38  Bumped MPI2_HEADER_VERSION_UNIT.
 * 05-25-15  02.00.39  Bumped MPI2_HEADER_VERSION_UNIT.
 * --------------------------------------------------------------------------
 */

#ifndef MPI2_H
#define MPI2_H

/*****************************************************************************
*
*       MPI Version Definitions
*
*****************************************************************************/

#define MPI2_VERSION_MAJOR_MASK             (0xFF00)
#define MPI2_VERSION_MAJOR_SHIFT            (8)
#define MPI2_VERSION_MINOR_MASK             (0x00FF)
#define MPI2_VERSION_MINOR_SHIFT            (0)

/*major version for all MPI v2.x */
#define MPI2_VERSION_MAJOR                  (0x02)

/*minor version for MPI v2.0 compatible products */
#define MPI2_VERSION_MINOR                  (0x00)
#define MPI2_VERSION ((MPI2_VERSION_MAJOR << MPI2_VERSION_MAJOR_SHIFT) | \
					MPI2_VERSION_MINOR)
#define MPI2_VERSION_02_00                  (0x0200)

/*minor version for MPI v2.5 compatible products */
#define MPI25_VERSION_MINOR                 (0x05)
#define MPI25_VERSION ((MPI2_VERSION_MAJOR << MPI2_VERSION_MAJOR_SHIFT) | \
					MPI25_VERSION_MINOR)
#define MPI2_VERSION_02_05                  (0x0205)

/*minor version for MPI v2.6 compatible products */
#define MPI26_VERSION_MINOR		    (0x06)
#define MPI26_VERSION ((MPI2_VERSION_MAJOR << MPI2_VERSION_MAJOR_SHIFT) | \
					MPI26_VERSION_MINOR)
#define MPI2_VERSION_02_06		    (0x0206)

/*Unit and Dev versioning for this MPI header set */
#define MPI2_HEADER_VERSION_UNIT            (0x27)
#define MPI2_HEADER_VERSION_DEV             (0x00)
#define MPI2_HEADER_VERSION_UNIT_MASK       (0xFF00)
#define MPI2_HEADER_VERSION_UNIT_SHIFT      (8)
#define MPI2_HEADER_VERSION_DEV_MASK        (0x00FF)
#define MPI2_HEADER_VERSION_DEV_SHIFT       (0)
#define MPI2_HEADER_VERSION ((MPI2_HEADER_VERSION_UNIT << 8) | \
					MPI2_HEADER_VERSION_DEV)

/*****************************************************************************
*
*       IOC State Definitions
*
*****************************************************************************/

#define MPI2_IOC_STATE_RESET               (0x00000000)
#define MPI2_IOC_STATE_READY               (0x10000000)
#define MPI2_IOC_STATE_OPERATIONAL         (0x20000000)
#define MPI2_IOC_STATE_FAULT               (0x40000000)

#define MPI2_IOC_STATE_MASK                (0xF0000000)
#define MPI2_IOC_STATE_SHIFT               (28)

/*Fault state range for prodcut specific codes */
#define MPI2_FAULT_PRODUCT_SPECIFIC_MIN                 (0x0000)
#define MPI2_FAULT_PRODUCT_SPECIFIC_MAX                 (0xEFFF)

/*****************************************************************************
*
*       System Interface Register Definitions
*
*****************************************************************************/

typedef volatile struct _MPI2_SYSTEM_INTERFACE_REGS {
	U32 Doorbell;		/*0x00 */
	U32 WriteSequence;	/*0x04 */
	U32 HostDiagnostic;	/*0x08 */
	U32 Reserved1;		/*0x0C */
	U32 DiagRWData;		/*0x10 */
	U32 DiagRWAddressLow;	/*0x14 */
	U32 DiagRWAddressHigh;	/*0x18 */
	U32 Reserved2[5];	/*0x1C */
	U32 HostInterruptStatus;	/*0x30 */
	U32 HostInterruptMask;	/*0x34 */
	U32 DCRData;		/*0x38 */
	U32 DCRAddress;		/*0x3C */
	U32 Reserved3[2];	/*0x40 */
	U32 ReplyFreeHostIndex;	/*0x48 */
	U32 Reserved4[8];	/*0x4C */
	U32 ReplyPostHostIndex;	/*0x6C */
	U32 Reserved5;		/*0x70 */
	U32 HCBSize;		/*0x74 */
	U32 HCBAddressLow;	/*0x78 */
	U32 HCBAddressHigh;	/*0x7C */
	U32 Reserved6[12];	/*0x80 */
	U32 Scratchpad[4];	/*0xB0 */
	U32 RequestDescriptorPostLow;	/*0xC0 */
	U32 RequestDescriptorPostHigh;	/*0xC4 */
	U32 AtomicRequestDescriptorPost;/*0xC8 */
	U32 Reserved7[13];	/*0xCC */
} MPI2_SYSTEM_INTERFACE_REGS,
	*PTR_MPI2_SYSTEM_INTERFACE_REGS,
	Mpi2SystemInterfaceRegs_t,
	*pMpi2SystemInterfaceRegs_t;

/*
 *Defines for working with the Doorbell register.
 */
#define MPI2_DOORBELL_OFFSET                    (0x00000000)

/*IOC --> System values */
#define MPI2_DOORBELL_USED                      (0x08000000)
#define MPI2_DOORBELL_WHO_INIT_MASK             (0x07000000)
#define MPI2_DOORBELL_WHO_INIT_SHIFT            (24)
#define MPI2_DOORBELL_FAULT_CODE_MASK           (0x0000FFFF)
#define MPI2_DOORBELL_DATA_MASK                 (0x0000FFFF)

/*System --> IOC values */
#define MPI2_DOORBELL_FUNCTION_MASK             (0xFF000000)
#define MPI2_DOORBELL_FUNCTION_SHIFT            (24)
#define MPI2_DOORBELL_ADD_DWORDS_MASK           (0x00FF0000)
#define MPI2_DOORBELL_ADD_DWORDS_SHIFT          (16)

/*
 *Defines for the WriteSequence register
 */
#define MPI2_WRITE_SEQUENCE_OFFSET              (0x00000004)
#define MPI2_WRSEQ_KEY_VALUE_MASK               (0x0000000F)
#define MPI2_WRSEQ_FLUSH_KEY_VALUE              (0x0)
#define MPI2_WRSEQ_1ST_KEY_VALUE                (0xF)
#define MPI2_WRSEQ_2ND_KEY_VALUE                (0x4)
#define MPI2_WRSEQ_3RD_KEY_VALUE                (0xB)
#define MPI2_WRSEQ_4TH_KEY_VALUE                (0x2)
#define MPI2_WRSEQ_5TH_KEY_VALUE                (0x7)
#define MPI2_WRSEQ_6TH_KEY_VALUE                (0xD)

/*
 *Defines for the HostDiagnostic register
 */
#define MPI2_HOST_DIAGNOSTIC_OFFSET             (0x00000008)

#define MPI2_DIAG_SBR_RELOAD                    (0x00002000)

#define MPI2_DIAG_BOOT_DEVICE_SELECT_MASK       (0x00001800)
#define MPI2_DIAG_BOOT_DEVICE_SELECT_DEFAULT    (0x00000000)
#define MPI2_DIAG_BOOT_DEVICE_SELECT_HCDW       (0x00000800)

#define MPI2_DIAG_CLEAR_FLASH_BAD_SIG           (0x00000400)
#define MPI2_DIAG_FORCE_HCB_ON_RESET            (0x00000200)
#define MPI2_DIAG_HCB_MODE                      (0x00000100)
#define MPI2_DIAG_DIAG_WRITE_ENABLE             (0x00000080)
#define MPI2_DIAG_FLASH_BAD_SIG                 (0x00000040)
#define MPI2_DIAG_RESET_HISTORY                 (0x00000020)
#define MPI2_DIAG_DIAG_RW_ENABLE                (0x00000010)
#define MPI2_DIAG_RESET_ADAPTER                 (0x00000004)
#define MPI2_DIAG_HOLD_IOC_RESET                (0x00000002)

/*
 *Offsets for DiagRWData and address
 */
#define MPI2_DIAG_RW_DATA_OFFSET                (0x00000010)
#define MPI2_DIAG_RW_ADDRESS_LOW_OFFSET         (0x00000014)
#define MPI2_DIAG_RW_ADDRESS_HIGH_OFFSET        (0x00000018)

/*
 *Defines for the HostInterruptStatus register
 */
#define MPI2_HOST_INTERRUPT_STATUS_OFFSET       (0x00000030)
#define MPI2_HIS_SYS2IOC_DB_STATUS              (0x80000000)
#define MPI2_HIS_IOP_DOORBELL_STATUS MPI2_HIS_SYS2IOC_DB_STATUS
#define MPI2_HIS_RESET_IRQ_STATUS               (0x40000000)
#define MPI2_HIS_REPLY_DESCRIPTOR_INTERRUPT     (0x00000008)
#define MPI2_HIS_IOC2SYS_DB_STATUS              (0x00000001)
#define MPI2_HIS_DOORBELL_INTERRUPT MPI2_HIS_IOC2SYS_DB_STATUS

/*
 *Defines for the HostInterruptMask register
 */
#define MPI2_HOST_INTERRUPT_MASK_OFFSET         (0x00000034)
#define MPI2_HIM_RESET_IRQ_MASK                 (0x40000000)
#define MPI2_HIM_REPLY_INT_MASK                 (0x00000008)
#define MPI2_HIM_RIM                            MPI2_HIM_REPLY_INT_MASK
#define MPI2_HIM_IOC2SYS_DB_MASK                (0x00000001)
#define MPI2_HIM_DIM                            MPI2_HIM_IOC2SYS_DB_MASK

/*
 *Offsets for DCRData and address
 */
#define MPI2_DCR_DATA_OFFSET                    (0x00000038)
#define MPI2_DCR_ADDRESS_OFFSET                 (0x0000003C)

/*
 *Offset for the Reply Free Queue
 */
#define MPI2_REPLY_FREE_HOST_INDEX_OFFSET       (0x00000048)

/*
 *Defines for the Reply Descriptor Post Queue
 */
#define MPI2_REPLY_POST_HOST_INDEX_OFFSET       (0x0000006C)
#define MPI2_REPLY_POST_HOST_INDEX_MASK         (0x00FFFFFF)
#define MPI2_RPHI_MSIX_INDEX_MASK               (0xFF000000)
#define MPI2_RPHI_MSIX_INDEX_SHIFT              (24)
#define MPI25_SUP_REPLY_POST_HOST_INDEX_OFFSET  (0x0000030C) /*MPI v2.5 only*/


/*
 *Defines for the HCBSize and address
 */
#define MPI2_HCB_SIZE_OFFSET                    (0x00000074)
#define MPI2_HCB_SIZE_SIZE_MASK                 (0xFFFFF000)
#define MPI2_HCB_SIZE_HCB_ENABLE                (0x00000001)

#define MPI2_HCB_ADDRESS_LOW_OFFSET             (0x00000078)
#define MPI2_HCB_ADDRESS_HIGH_OFFSET            (0x0000007C)

/*
 *Offsets for the Scratchpad registers
 */
#define MPI26_SCRATCHPAD0_OFFSET                (0x000000B0)
#define MPI26_SCRATCHPAD1_OFFSET                (0x000000B4)
#define MPI26_SCRATCHPAD2_OFFSET                (0x000000B8)
#define MPI26_SCRATCHPAD3_OFFSET                (0x000000BC)

/*
 *Offsets for the Request Descriptor Post Queue
 */
#define MPI2_REQUEST_DESCRIPTOR_POST_LOW_OFFSET     (0x000000C0)
#define MPI2_REQUEST_DESCRIPTOR_POST_HIGH_OFFSET    (0x000000C4)
#define MPI26_ATOMIC_REQUEST_DESCRIPTOR_POST_OFFSET (0x000000C8)

/*Hard Reset delay timings */
#define MPI2_HARD_RESET_PCIE_FIRST_READ_DELAY_MICRO_SEC     (50000)
#define MPI2_HARD_RESET_PCIE_RESET_READ_WINDOW_MICRO_SEC    (255000)
#define MPI2_HARD_RESET_PCIE_SECOND_READ_DELAY_MICRO_SEC    (256000)

/*****************************************************************************
*
*       Message Descriptors
*
*****************************************************************************/

/*Request Descriptors */

/*Default Request Descriptor */
typedef struct _MPI2_DEFAULT_REQUEST_DESCRIPTOR {
	U8 RequestFlags;	/*0x00 */
	U8 MSIxIndex;		/*0x01 */
	U16 SMID;		/*0x02 */
	U16 LMID;		/*0x04 */
	U16 DescriptorTypeDependent;	/*0x06 */
} MPI2_DEFAULT_REQUEST_DESCRIPTOR,
	*PTR_MPI2_DEFAULT_REQUEST_DESCRIPTOR,
	Mpi2DefaultRequestDescriptor_t,
	*pMpi2DefaultRequestDescriptor_t;

/*defines for the RequestFlags field */
#define MPI2_REQ_DESCRIPT_FLAGS_TYPE_MASK               (0x1E)
#define MPI2_REQ_DESCRIPT_FLAGS_TYPE_RSHIFT             (1)
#define MPI2_REQ_DESCRIPT_FLAGS_SCSI_IO                 (0x00)
#define MPI2_REQ_DESCRIPT_FLAGS_SCSI_TARGET             (0x02)
#define MPI2_REQ_DESCRIPT_FLAGS_HIGH_PRIORITY           (0x06)
#define MPI2_REQ_DESCRIPT_FLAGS_DEFAULT_TYPE            (0x08)
#define MPI2_REQ_DESCRIPT_FLAGS_RAID_ACCELERATOR        (0x0A)
#define MPI25_REQ_DESCRIPT_FLAGS_FAST_PATH_SCSI_IO      (0x0C)

#define MPI2_REQ_DESCRIPT_FLAGS_IOC_FIFO_MARKER         (0x01)

/*High Priority Request Descriptor */
typedef struct _MPI2_HIGH_PRIORITY_REQUEST_DESCRIPTOR {
	U8 RequestFlags;	/*0x00 */
	U8 MSIxIndex;		/*0x01 */
	U16 SMID;		/*0x02 */
	U16 LMID;		/*0x04 */
	U16 Reserved1;		/*0x06 */
} MPI2_HIGH_PRIORITY_REQUEST_DESCRIPTOR,
	*PTR_MPI2_HIGH_PRIORITY_REQUEST_DESCRIPTOR,
	Mpi2HighPriorityRequestDescriptor_t,
	*pMpi2HighPriorityRequestDescriptor_t;

/*SCSI IO Request Descriptor */
typedef struct _MPI2_SCSI_IO_REQUEST_DESCRIPTOR {
	U8 RequestFlags;	/*0x00 */
	U8 MSIxIndex;		/*0x01 */
	U16 SMID;		/*0x02 */
	U16 LMID;		/*0x04 */
	U16 DevHandle;		/*0x06 */
} MPI2_SCSI_IO_REQUEST_DESCRIPTOR,
	*PTR_MPI2_SCSI_IO_REQUEST_DESCRIPTOR,
	Mpi2SCSIIORequestDescriptor_t,
	*pMpi2SCSIIORequestDescriptor_t;

/*SCSI Target Request Descriptor */
typedef struct _MPI2_SCSI_TARGET_REQUEST_DESCRIPTOR {
	U8 RequestFlags;	/*0x00 */
	U8 MSIxIndex;		/*0x01 */
	U16 SMID;		/*0x02 */
	U16 LMID;		/*0x04 */
	U16 IoIndex;		/*0x06 */
} MPI2_SCSI_TARGET_REQUEST_DESCRIPTOR,
	*PTR_MPI2_SCSI_TARGET_REQUEST_DESCRIPTOR,
	Mpi2SCSITargetRequestDescriptor_t,
	*pMpi2SCSITargetRequestDescriptor_t;

/*RAID Accelerator Request Descriptor */
typedef struct _MPI2_RAID_ACCEL_REQUEST_DESCRIPTOR {
	U8 RequestFlags;	/*0x00 */
	U8 MSIxIndex;		/*0x01 */
	U16 SMID;		/*0x02 */
	U16 LMID;		/*0x04 */
	U16 Reserved;		/*0x06 */
} MPI2_RAID_ACCEL_REQUEST_DESCRIPTOR,
	*PTR_MPI2_RAID_ACCEL_REQUEST_DESCRIPTOR,
	Mpi2RAIDAcceleratorRequestDescriptor_t,
	*pMpi2RAIDAcceleratorRequestDescriptor_t;

/*Fast Path SCSI IO Request Descriptor */
typedef MPI2_SCSI_IO_REQUEST_DESCRIPTOR
	MPI25_FP_SCSI_IO_REQUEST_DESCRIPTOR,
	*PTR_MPI25_FP_SCSI_IO_REQUEST_DESCRIPTOR,
	Mpi25FastPathSCSIIORequestDescriptor_t,
	*pMpi25FastPathSCSIIORequestDescriptor_t;

/*union of Request Descriptors */
typedef union _MPI2_REQUEST_DESCRIPTOR_UNION {
	MPI2_DEFAULT_REQUEST_DESCRIPTOR Default;
	MPI2_HIGH_PRIORITY_REQUEST_DESCRIPTOR HighPriority;
	MPI2_SCSI_IO_REQUEST_DESCRIPTOR SCSIIO;
	MPI2_SCSI_TARGET_REQUEST_DESCRIPTOR SCSITarget;
	MPI2_RAID_ACCEL_REQUEST_DESCRIPTOR RAIDAccelerator;
	MPI25_FP_SCSI_IO_REQUEST_DESCRIPTOR FastPathSCSIIO;
	U64 Words;
} MPI2_REQUEST_DESCRIPTOR_UNION,
	*PTR_MPI2_REQUEST_DESCRIPTOR_UNION,
	Mpi2RequestDescriptorUnion_t,
	*pMpi2RequestDescriptorUnion_t;

/*Atomic Request Descriptors */

/*
 * All Atomic Request Descriptors have the same format, so the following
 * structure is used for all Atomic Request Descriptors:
 *      Atomic Default Request Descriptor
 *      Atomic High Priority Request Descriptor
 *      Atomic SCSI IO Request Descriptor
 *      Atomic SCSI Target Request Descriptor
 *      Atomic RAID Accelerator Request Descriptor
 *      Atomic Fast Path SCSI IO Request Descriptor
 */

/*Atomic Request Descriptor */
typedef struct _MPI26_ATOMIC_REQUEST_DESCRIPTOR {
	U8 RequestFlags;	/* 0x00 */
	U8 MSIxIndex;		/* 0x01 */
	U16 SMID;		/* 0x02 */
} MPI26_ATOMIC_REQUEST_DESCRIPTOR,
	*PTR_MPI26_ATOMIC_REQUEST_DESCRIPTOR,
	Mpi26AtomicRequestDescriptor_t,
	*pMpi26AtomicRequestDescriptor_t;

/*for the RequestFlags field, use the same
 *defines as MPI2_DEFAULT_REQUEST_DESCRIPTOR
 */

/*Reply Descriptors */

/*Default Reply Descriptor */
typedef struct _MPI2_DEFAULT_REPLY_DESCRIPTOR {
	U8 ReplyFlags;		/*0x00 */
	U8 MSIxIndex;		/*0x01 */
	U16 DescriptorTypeDependent1;	/*0x02 */
	U32 DescriptorTypeDependent2;	/*0x04 */
} MPI2_DEFAULT_REPLY_DESCRIPTOR,
	*PTR_MPI2_DEFAULT_REPLY_DESCRIPTOR,
	Mpi2DefaultReplyDescriptor_t,
	*pMpi2DefaultReplyDescriptor_t;

/*defines for the ReplyFlags field */
#define MPI2_RPY_DESCRIPT_FLAGS_TYPE_MASK                   (0x0F)
#define MPI2_RPY_DESCRIPT_FLAGS_SCSI_IO_SUCCESS             (0x00)
#define MPI2_RPY_DESCRIPT_FLAGS_ADDRESS_REPLY               (0x01)
#define MPI2_RPY_DESCRIPT_FLAGS_TARGETASSIST_SUCCESS        (0x02)
#define MPI2_RPY_DESCRIPT_FLAGS_TARGET_COMMAND_BUFFER       (0x03)
#define MPI2_RPY_DESCRIPT_FLAGS_RAID_ACCELERATOR_SUCCESS    (0x05)
#define MPI25_RPY_DESCRIPT_FLAGS_FAST_PATH_SCSI_IO_SUCCESS  (0x06)
#define MPI2_RPY_DESCRIPT_FLAGS_UNUSED                      (0x0F)

/*values for marking a reply descriptor as unused */
#define MPI2_RPY_DESCRIPT_UNUSED_WORD0_MARK             (0xFFFFFFFF)
#define MPI2_RPY_DESCRIPT_UNUSED_WORD1_MARK             (0xFFFFFFFF)

/*Address Reply Descriptor */
typedef struct _MPI2_ADDRESS_REPLY_DESCRIPTOR {
	U8 ReplyFlags;		/*0x00 */
	U8 MSIxIndex;		/*0x01 */
	U16 SMID;		/*0x02 */
	U32 ReplyFrameAddress;	/*0x04 */
} MPI2_ADDRESS_REPLY_DESCRIPTOR,
	*PTR_MPI2_ADDRESS_REPLY_DESCRIPTOR,
	Mpi2AddressReplyDescriptor_t,
	*pMpi2AddressReplyDescriptor_t;

#define MPI2_ADDRESS_REPLY_SMID_INVALID                 (0x00)

/*SCSI IO Success Reply Descriptor */
typedef struct _MPI2_SCSI_IO_SUCCESS_REPLY_DESCRIPTOR {
	U8 ReplyFlags;		/*0x00 */
	U8 MSIxIndex;		/*0x01 */
	U16 SMID;		/*0x02 */
	U16 TaskTag;		/*0x04 */
	U16 Reserved1;		/*0x06 */
} MPI2_SCSI_IO_SUCCESS_REPLY_DESCRIPTOR,
	*PTR_MPI2_SCSI_IO_SUCCESS_REPLY_DESCRIPTOR,
	Mpi2SCSIIOSuccessReplyDescriptor_t,
	*pMpi2SCSIIOSuccessReplyDescriptor_t;

/*TargetAssist Success Reply Descriptor */
typedef struct _MPI2_TARGETASSIST_SUCCESS_REPLY_DESCRIPTOR {
	U8 ReplyFlags;		/*0x00 */
	U8 MSIxIndex;		/*0x01 */
	U16 SMID;		/*0x02 */
	U8 SequenceNumber;	/*0x04 */
	U8 Reserved1;		/*0x05 */
	U16 IoIndex;		/*0x06 */
} MPI2_TARGETASSIST_SUCCESS_REPLY_DESCRIPTOR,
	*PTR_MPI2_TARGETASSIST_SUCCESS_REPLY_DESCRIPTOR,
	Mpi2TargetAssistSuccessReplyDescriptor_t,
	*pMpi2TargetAssistSuccessReplyDescriptor_t;

/*Target Command Buffer Reply Descriptor */
typedef struct _MPI2_TARGET_COMMAND_BUFFER_REPLY_DESCRIPTOR {
	U8 ReplyFlags;		/*0x00 */
	U8 MSIxIndex;		/*0x01 */
	U8 VP_ID;		/*0x02 */
	U8 Flags;		/*0x03 */
	U16 InitiatorDevHandle;	/*0x04 */
	U16 IoIndex;		/*0x06 */
} MPI2_TARGET_COMMAND_BUFFER_REPLY_DESCRIPTOR,
	*PTR_MPI2_TARGET_COMMAND_BUFFER_REPLY_DESCRIPTOR,
	Mpi2TargetCommandBufferReplyDescriptor_t,
	*pMpi2TargetCommandBufferReplyDescriptor_t;

/*defines for Flags field */
#define MPI2_RPY_DESCRIPT_TCB_FLAGS_PHYNUM_MASK     (0x3F)

/*RAID Accelerator Success Reply Descriptor */
typedef struct _MPI2_RAID_ACCELERATOR_SUCCESS_REPLY_DESCRIPTOR {
	U8 ReplyFlags;		/*0x00 */
	U8 MSIxIndex;		/*0x01 */
	U16 SMID;		/*0x02 */
	U32 Reserved;		/*0x04 */
} MPI2_RAID_ACCELERATOR_SUCCESS_REPLY_DESCRIPTOR,
	*PTR_MPI2_RAID_ACCELERATOR_SUCCESS_REPLY_DESCRIPTOR,
	Mpi2RAIDAcceleratorSuccessReplyDescriptor_t,
	*pMpi2RAIDAcceleratorSuccessReplyDescriptor_t;

/*Fast Path SCSI IO Success Reply Descriptor */
typedef MPI2_SCSI_IO_SUCCESS_REPLY_DESCRIPTOR
	MPI25_FP_SCSI_IO_SUCCESS_REPLY_DESCRIPTOR,
	*PTR_MPI25_FP_SCSI_IO_SUCCESS_REPLY_DESCRIPTOR,
	Mpi25FastPathSCSIIOSuccessReplyDescriptor_t,
	*pMpi25FastPathSCSIIOSuccessReplyDescriptor_t;

/*union of Reply Descriptors */
typedef union _MPI2_REPLY_DESCRIPTORS_UNION {
	MPI2_DEFAULT_REPLY_DESCRIPTOR Default;
	MPI2_ADDRESS_REPLY_DESCRIPTOR AddressReply;
	MPI2_SCSI_IO_SUCCESS_REPLY_DESCRIPTOR SCSIIOSuccess;
	MPI2_TARGETASSIST_SUCCESS_REPLY_DESCRIPTOR TargetAssistSuccess;
	MPI2_TARGET_COMMAND_BUFFER_REPLY_DESCRIPTOR TargetCommandBuffer;
	MPI2_RAID_ACCELERATOR_SUCCESS_REPLY_DESCRIPTOR RAIDAcceleratorSuccess;
	MPI25_FP_SCSI_IO_SUCCESS_REPLY_DESCRIPTOR FastPathSCSIIOSuccess;
	U64 Words;
} MPI2_REPLY_DESCRIPTORS_UNION,
	*PTR_MPI2_REPLY_DESCRIPTORS_UNION,
	Mpi2ReplyDescriptorsUnion_t,
	*pMpi2ReplyDescriptorsUnion_t;

/*****************************************************************************
*
*       Message Functions
*
*****************************************************************************/

#define MPI2_FUNCTION_SCSI_IO_REQUEST		    (0x00)
#define MPI2_FUNCTION_SCSI_TASK_MGMT		    (0x01)
#define MPI2_FUNCTION_IOC_INIT                      (0x02)
#define MPI2_FUNCTION_IOC_FACTS                     (0x03)
#define MPI2_FUNCTION_CONFIG                        (0x04)
#define MPI2_FUNCTION_PORT_FACTS                    (0x05)
#define MPI2_FUNCTION_PORT_ENABLE                   (0x06)
#define MPI2_FUNCTION_EVENT_NOTIFICATION            (0x07)
#define MPI2_FUNCTION_EVENT_ACK                     (0x08)
#define MPI2_FUNCTION_FW_DOWNLOAD                   (0x09)
#define MPI2_FUNCTION_TARGET_ASSIST                 (0x0B)
#define MPI2_FUNCTION_TARGET_STATUS_SEND            (0x0C)
#define MPI2_FUNCTION_TARGET_MODE_ABORT             (0x0D)
#define MPI2_FUNCTION_FW_UPLOAD                     (0x12)
#define MPI2_FUNCTION_RAID_ACTION                   (0x15)
#define MPI2_FUNCTION_RAID_SCSI_IO_PASSTHROUGH      (0x16)
#define MPI2_FUNCTION_TOOLBOX                       (0x17)
#define MPI2_FUNCTION_SCSI_ENCLOSURE_PROCESSOR      (0x18)
#define MPI2_FUNCTION_SMP_PASSTHROUGH               (0x1A)
#define MPI2_FUNCTION_SAS_IO_UNIT_CONTROL           (0x1B)
#define MPI2_FUNCTION_IO_UNIT_CONTROL               (0x1B)
#define MPI2_FUNCTION_SATA_PASSTHROUGH              (0x1C)
#define MPI2_FUNCTION_DIAG_BUFFER_POST              (0x1D)
#define MPI2_FUNCTION_DIAG_RELEASE                  (0x1E)
#define MPI2_FUNCTION_TARGET_CMD_BUF_BASE_POST      (0x24)
#define MPI2_FUNCTION_TARGET_CMD_BUF_LIST_POST      (0x25)
#define MPI2_FUNCTION_RAID_ACCELERATOR              (0x2C)
#define MPI2_FUNCTION_HOST_BASED_DISCOVERY_ACTION   (0x2F)
#define MPI2_FUNCTION_PWR_MGMT_CONTROL              (0x30)
#define MPI2_FUNCTION_SEND_HOST_MESSAGE             (0x31)
#define MPI2_FUNCTION_MIN_PRODUCT_SPECIFIC          (0xF0)
#define MPI2_FUNCTION_MAX_PRODUCT_SPECIFIC          (0xFF)

/*Doorbell functions */
#define MPI2_FUNCTION_IOC_MESSAGE_UNIT_RESET        (0x40)
#define MPI2_FUNCTION_HANDSHAKE                     (0x42)

/*****************************************************************************
*
*       IOC Status Values
*
*****************************************************************************/

/*mask for IOCStatus status value */
#define MPI2_IOCSTATUS_MASK                     (0x7FFF)

/****************************************************************************
* Common IOCStatus values for all replies
****************************************************************************/

#define MPI2_IOCSTATUS_SUCCESS                      (0x0000)
#define MPI2_IOCSTATUS_INVALID_FUNCTION             (0x0001)
#define MPI2_IOCSTATUS_BUSY                         (0x0002)
#define MPI2_IOCSTATUS_INVALID_SGL                  (0x0003)
#define MPI2_IOCSTATUS_INTERNAL_ERROR               (0x0004)
#define MPI2_IOCSTATUS_INVALID_VPID                 (0x0005)
#define MPI2_IOCSTATUS_INSUFFICIENT_RESOURCES       (0x0006)
#define MPI2_IOCSTATUS_INVALID_FIELD                (0x0007)
#define MPI2_IOCSTATUS_INVALID_STATE                (0x0008)
#define MPI2_IOCSTATUS_OP_STATE_NOT_SUPPORTED       (0x0009)
#define MPI2_IOCSTATUS_INSUFFICIENT_POWER           (0x000A)

/****************************************************************************
* Config IOCStatus values
****************************************************************************/

#define MPI2_IOCSTATUS_CONFIG_INVALID_ACTION        (0x0020)
#define MPI2_IOCSTATUS_CONFIG_INVALID_TYPE          (0x0021)
#define MPI2_IOCSTATUS_CONFIG_INVALID_PAGE          (0x0022)
#define MPI2_IOCSTATUS_CONFIG_INVALID_DATA          (0x0023)
#define MPI2_IOCSTATUS_CONFIG_NO_DEFAULTS           (0x0024)
#define MPI2_IOCSTATUS_CONFIG_CANT_COMMIT           (0x0025)

/****************************************************************************
* SCSI IO Reply
****************************************************************************/

#define MPI2_IOCSTATUS_SCSI_RECOVERED_ERROR         (0x0040)
#define MPI2_IOCSTATUS_SCSI_INVALID_DEVHANDLE       (0x0042)
#define MPI2_IOCSTATUS_SCSI_DEVICE_NOT_THERE        (0x0043)
#define MPI2_IOCSTATUS_SCSI_DATA_OVERRUN            (0x0044)
#define MPI2_IOCSTATUS_SCSI_DATA_UNDERRUN           (0x0045)
#define MPI2_IOCSTATUS_SCSI_IO_DATA_ERROR           (0x0046)
#define MPI2_IOCSTATUS_SCSI_PROTOCOL_ERROR          (0x0047)
#define MPI2_IOCSTATUS_SCSI_TASK_TERMINATED         (0x0048)
#define MPI2_IOCSTATUS_SCSI_RESIDUAL_MISMATCH       (0x0049)
#define MPI2_IOCSTATUS_SCSI_TASK_MGMT_FAILED        (0x004A)
#define MPI2_IOCSTATUS_SCSI_IOC_TERMINATED          (0x004B)
#define MPI2_IOCSTATUS_SCSI_EXT_TERMINATED          (0x004C)

/****************************************************************************
* For use by SCSI Initiator and SCSI Target end-to-end data protection
****************************************************************************/

#define MPI2_IOCSTATUS_EEDP_GUARD_ERROR             (0x004D)
#define MPI2_IOCSTATUS_EEDP_REF_TAG_ERROR           (0x004E)
#define MPI2_IOCSTATUS_EEDP_APP_TAG_ERROR           (0x004F)

/****************************************************************************
* SCSI Target values
****************************************************************************/

#define MPI2_IOCSTATUS_TARGET_INVALID_IO_INDEX      (0x0062)
#define MPI2_IOCSTATUS_TARGET_ABORTED               (0x0063)
#define MPI2_IOCSTATUS_TARGET_NO_CONN_RETRYABLE     (0x0064)
#define MPI2_IOCSTATUS_TARGET_NO_CONNECTION         (0x0065)
#define MPI2_IOCSTATUS_TARGET_XFER_COUNT_MISMATCH   (0x006A)
#define MPI2_IOCSTATUS_TARGET_DATA_OFFSET_ERROR     (0x006D)
#define MPI2_IOCSTATUS_TARGET_TOO_MUCH_WRITE_DATA   (0x006E)
#define MPI2_IOCSTATUS_TARGET_IU_TOO_SHORT          (0x006F)
#define MPI2_IOCSTATUS_TARGET_ACK_NAK_TIMEOUT       (0x0070)
#define MPI2_IOCSTATUS_TARGET_NAK_RECEIVED          (0x0071)

/****************************************************************************
* Serial Attached SCSI values
****************************************************************************/

#define MPI2_IOCSTATUS_SAS_SMP_REQUEST_FAILED       (0x0090)
#define MPI2_IOCSTATUS_SAS_SMP_DATA_OVERRUN         (0x0091)

/****************************************************************************
* Diagnostic Buffer Post / Diagnostic Release values
****************************************************************************/

#define MPI2_IOCSTATUS_DIAGNOSTIC_RELEASED          (0x00A0)

/****************************************************************************
* RAID Accelerator values
****************************************************************************/

#define MPI2_IOCSTATUS_RAID_ACCEL_ERROR             (0x00B0)

/****************************************************************************
* IOCStatus flag to indicate that log info is available
****************************************************************************/

#define MPI2_IOCSTATUS_FLAG_LOG_INFO_AVAILABLE      (0x8000)

/****************************************************************************
* IOCLogInfo Types
****************************************************************************/

#define MPI2_IOCLOGINFO_TYPE_MASK               (0xF0000000)
#define MPI2_IOCLOGINFO_TYPE_SHIFT              (28)
#define MPI2_IOCLOGINFO_TYPE_NONE               (0x0)
#define MPI2_IOCLOGINFO_TYPE_SCSI               (0x1)
#define MPI2_IOCLOGINFO_TYPE_FC                 (0x2)
#define MPI2_IOCLOGINFO_TYPE_SAS                (0x3)
#define MPI2_IOCLOGINFO_TYPE_ISCSI              (0x4)
#define MPI2_IOCLOGINFO_LOG_DATA_MASK           (0x0FFFFFFF)

/*****************************************************************************
*
*       Standard Message Structures
*
*****************************************************************************/

/****************************************************************************
*Request Message Header for all request messages
****************************************************************************/

typedef struct _MPI2_REQUEST_HEADER {
	U16 FunctionDependent1;	/*0x00 */
	U8 ChainOffset;		/*0x02 */
	U8 Function;		/*0x03 */
	U16 FunctionDependent2;	/*0x04 */
	U8 FunctionDependent3;	/*0x06 */
	U8 MsgFlags;		/*0x07 */
	U8 VP_ID;		/*0x08 */
	U8 VF_ID;		/*0x09 */
	U16 Reserved1;		/*0x0A */
} MPI2_REQUEST_HEADER, *PTR_MPI2_REQUEST_HEADER,
	MPI2RequestHeader_t, *pMPI2RequestHeader_t;

/****************************************************************************
* Default Reply
****************************************************************************/

typedef struct _MPI2_DEFAULT_REPLY {
	U16 FunctionDependent1;	/*0x00 */
	U8 MsgLength;		/*0x02 */
	U8 Function;		/*0x03 */
	U16 FunctionDependent2;	/*0x04 */
	U8 FunctionDependent3;	/*0x06 */
	U8 MsgFlags;		/*0x07 */
	U8 VP_ID;		/*0x08 */
	U8 VF_ID;		/*0x09 */
	U16 Reserved1;		/*0x0A */
	U16 FunctionDependent5;	/*0x0C */
	U16 IOCStatus;		/*0x0E */
	U32 IOCLogInfo;		/*0x10 */
} MPI2_DEFAULT_REPLY, *PTR_MPI2_DEFAULT_REPLY,
	MPI2DefaultReply_t, *pMPI2DefaultReply_t;

/*common version structure/union used in messages and configuration pages */

typedef struct _MPI2_VERSION_STRUCT {
	U8 Dev;			/*0x00 */
	U8 Unit;		/*0x01 */
	U8 Minor;		/*0x02 */
	U8 Major;		/*0x03 */
} MPI2_VERSION_STRUCT;

typedef union _MPI2_VERSION_UNION {
	MPI2_VERSION_STRUCT Struct;
	U32 Word;
} MPI2_VERSION_UNION;

/*LUN field defines, common to many structures */
#define MPI2_LUN_FIRST_LEVEL_ADDRESSING             (0x0000FFFF)
#define MPI2_LUN_SECOND_LEVEL_ADDRESSING            (0xFFFF0000)
#define MPI2_LUN_THIRD_LEVEL_ADDRESSING             (0x0000FFFF)
#define MPI2_LUN_FOURTH_LEVEL_ADDRESSING            (0xFFFF0000)
#define MPI2_LUN_LEVEL_1_WORD                       (0xFF00)
#define MPI2_LUN_LEVEL_1_DWORD                      (0x0000FF00)

/*****************************************************************************
*
*       Fusion-MPT MPI Scatter Gather Elements
*
*****************************************************************************/

/****************************************************************************
* MPI Simple Element structures
****************************************************************************/

typedef struct _MPI2_SGE_SIMPLE32 {
	U32 FlagsLength;
	U32 Address;
} MPI2_SGE_SIMPLE32, *PTR_MPI2_SGE_SIMPLE32,
	Mpi2SGESimple32_t, *pMpi2SGESimple32_t;

typedef struct _MPI2_SGE_SIMPLE64 {
	U32 FlagsLength;
	U64 Address;
} MPI2_SGE_SIMPLE64, *PTR_MPI2_SGE_SIMPLE64,
	Mpi2SGESimple64_t, *pMpi2SGESimple64_t;

typedef struct _MPI2_SGE_SIMPLE_UNION {
	U32 FlagsLength;
	union {
		U32 Address32;
		U64 Address64;
	} u;
} MPI2_SGE_SIMPLE_UNION,
	*PTR_MPI2_SGE_SIMPLE_UNION,
	Mpi2SGESimpleUnion_t,
	*pMpi2SGESimpleUnion_t;

/****************************************************************************
* MPI Chain Element structures - for MPI v2.0 products only
****************************************************************************/

typedef struct _MPI2_SGE_CHAIN32 {
	U16 Length;
	U8 NextChainOffset;
	U8 Flags;
	U32 Address;
} MPI2_SGE_CHAIN32, *PTR_MPI2_SGE_CHAIN32,
	Mpi2SGEChain32_t, *pMpi2SGEChain32_t;

typedef struct _MPI2_SGE_CHAIN64 {
	U16 Length;
	U8 NextChainOffset;
	U8 Flags;
	U64 Address;
} MPI2_SGE_CHAIN64, *PTR_MPI2_SGE_CHAIN64,
	Mpi2SGEChain64_t, *pMpi2SGEChain64_t;

typedef struct _MPI2_SGE_CHAIN_UNION {
	U16 Length;
	U8 NextChainOffset;
	U8 Flags;
	union {
		U32 Address32;
		U64 Address64;
	} u;
} MPI2_SGE_CHAIN_UNION,
	*PTR_MPI2_SGE_CHAIN_UNION,
	Mpi2SGEChainUnion_t,
	*pMpi2SGEChainUnion_t;

/****************************************************************************
* MPI Transaction Context Element structures - for MPI v2.0 products only
****************************************************************************/

typedef struct _MPI2_SGE_TRANSACTION32 {
	U8 Reserved;
	U8 ContextSize;
	U8 DetailsLength;
	U8 Flags;
	U32 TransactionContext[1];
	U32 TransactionDetails[1];
} MPI2_SGE_TRANSACTION32,
	*PTR_MPI2_SGE_TRANSACTION32,
	Mpi2SGETransaction32_t,
	*pMpi2SGETransaction32_t;

typedef struct _MPI2_SGE_TRANSACTION64 {
	U8 Reserved;
	U8 ContextSize;
	U8 DetailsLength;
	U8 Flags;
	U32 TransactionContext[2];
	U32 TransactionDetails[1];
} MPI2_SGE_TRANSACTION64,
	*PTR_MPI2_SGE_TRANSACTION64,
	Mpi2SGETransaction64_t,
	*pMpi2SGETransaction64_t;

typedef struct _MPI2_SGE_TRANSACTION96 {
	U8 Reserved;
	U8 ContextSize;
	U8 DetailsLength;
	U8 Flags;
	U32 TransactionContext[3];
	U32 TransactionDetails[1];
} MPI2_SGE_TRANSACTION96, *PTR_MPI2_SGE_TRANSACTION96,
	Mpi2SGETransaction96_t, *pMpi2SGETransaction96_t;

typedef struct _MPI2_SGE_TRANSACTION128 {
	U8 Reserved;
	U8 ContextSize;
	U8 DetailsLength;
	U8 Flags;
	U32 TransactionContext[4];
	U32 TransactionDetails[1];
} MPI2_SGE_TRANSACTION128, *PTR_MPI2_SGE_TRANSACTION128,
	Mpi2SGETransaction_t128, *pMpi2SGETransaction_t128;

typedef struct _MPI2_SGE_TRANSACTION_UNION {
	U8 Reserved;
	U8 ContextSize;
	U8 DetailsLength;
	U8 Flags;
	union {
		U32 TransactionContext32[1];
		U32 TransactionContext64[2];
		U32 TransactionContext96[3];
		U32 TransactionContext128[4];
	} u;
	U32 TransactionDetails[1];
} MPI2_SGE_TRANSACTION_UNION,
	*PTR_MPI2_SGE_TRANSACTION_UNION,
	Mpi2SGETransactionUnion_t,
	*pMpi2SGETransactionUnion_t;

/****************************************************************************
* MPI SGE union for IO SGL's - for MPI v2.0 products only
****************************************************************************/

typedef struct _MPI2_MPI_SGE_IO_UNION {
	union {
		MPI2_SGE_SIMPLE_UNION Simple;
		MPI2_SGE_CHAIN_UNION Chain;
	} u;
} MPI2_MPI_SGE_IO_UNION, *PTR_MPI2_MPI_SGE_IO_UNION,
	Mpi2MpiSGEIOUnion_t, *pMpi2MpiSGEIOUnion_t;

/****************************************************************************
* MPI SGE union for SGL's with Simple and Transaction elements - for MPI v2.0 products only
****************************************************************************/

typedef struct _MPI2_SGE_TRANS_SIMPLE_UNION {
	union {
		MPI2_SGE_SIMPLE_UNION Simple;
		MPI2_SGE_TRANSACTION_UNION Transaction;
	} u;
} MPI2_SGE_TRANS_SIMPLE_UNION,
	*PTR_MPI2_SGE_TRANS_SIMPLE_UNION,
	Mpi2SGETransSimpleUnion_t,
	*pMpi2SGETransSimpleUnion_t;

/****************************************************************************
* All MPI SGE types union
****************************************************************************/

typedef struct _MPI2_MPI_SGE_UNION {
	union {
		MPI2_SGE_SIMPLE_UNION Simple;
		MPI2_SGE_CHAIN_UNION Chain;
		MPI2_SGE_TRANSACTION_UNION Transaction;
	} u;
} MPI2_MPI_SGE_UNION, *PTR_MPI2_MPI_SGE_UNION,
	Mpi2MpiSgeUnion_t, *pMpi2MpiSgeUnion_t;

/****************************************************************************
* MPI SGE field definition and masks
****************************************************************************/

/*Flags field bit definitions */

#define MPI2_SGE_FLAGS_LAST_ELEMENT             (0x80)
#define MPI2_SGE_FLAGS_END_OF_BUFFER            (0x40)
#define MPI2_SGE_FLAGS_ELEMENT_TYPE_MASK        (0x30)
#define MPI2_SGE_FLAGS_LOCAL_ADDRESS            (0x08)
#define MPI2_SGE_FLAGS_DIRECTION                (0x04)
#define MPI2_SGE_FLAGS_ADDRESS_SIZE             (0x02)
#define MPI2_SGE_FLAGS_END_OF_LIST              (0x01)

#define MPI2_SGE_FLAGS_SHIFT                    (24)

#define MPI2_SGE_LENGTH_MASK                    (0x00FFFFFF)
#define MPI2_SGE_CHAIN_LENGTH_MASK              (0x0000FFFF)

/*Element Type */

#define MPI2_SGE_FLAGS_TRANSACTION_ELEMENT      (0x00)
#define MPI2_SGE_FLAGS_SIMPLE_ELEMENT           (0x10)
#define MPI2_SGE_FLAGS_CHAIN_ELEMENT            (0x30)
#define MPI2_SGE_FLAGS_ELEMENT_MASK             (0x30)

/*Address location */

#define MPI2_SGE_FLAGS_SYSTEM_ADDRESS           (0x00)

/*Direction */

#define MPI2_SGE_FLAGS_IOC_TO_HOST              (0x00)
#define MPI2_SGE_FLAGS_HOST_TO_IOC              (0x04)

#define MPI2_SGE_FLAGS_DEST (MPI2_SGE_FLAGS_IOC_TO_HOST)
#define MPI2_SGE_FLAGS_SOURCE (MPI2_SGE_FLAGS_HOST_TO_IOC)

/*Address Size */

#define MPI2_SGE_FLAGS_32_BIT_ADDRESSING        (0x00)
#define MPI2_SGE_FLAGS_64_BIT_ADDRESSING        (0x02)

/*Context Size */

#define MPI2_SGE_FLAGS_32_BIT_CONTEXT           (0x00)
#define MPI2_SGE_FLAGS_64_BIT_CONTEXT           (0x02)
#define MPI2_SGE_FLAGS_96_BIT_CONTEXT           (0x04)
#define MPI2_SGE_FLAGS_128_BIT_CONTEXT          (0x06)

#define MPI2_SGE_CHAIN_OFFSET_MASK              (0x00FF0000)
#define MPI2_SGE_CHAIN_OFFSET_SHIFT             (16)

/****************************************************************************
* MPI SGE operation Macros
****************************************************************************/

/*SIMPLE FlagsLength manipulations... */
#define MPI2_SGE_SET_FLAGS(f) ((U32)(f) << MPI2_SGE_FLAGS_SHIFT)
#define MPI2_SGE_GET_FLAGS(f) (((f) & ~MPI2_SGE_LENGTH_MASK) >> \
					MPI2_SGE_FLAGS_SHIFT)
#define MPI2_SGE_LENGTH(f) ((f) & MPI2_SGE_LENGTH_MASK)
#define MPI2_SGE_CHAIN_LENGTH(f) ((f) & MPI2_SGE_CHAIN_LENGTH_MASK)

#define MPI2_SGE_SET_FLAGS_LENGTH(f, l) (MPI2_SGE_SET_FLAGS(f) | \
					MPI2_SGE_LENGTH(l))

#define MPI2_pSGE_GET_FLAGS(psg) MPI2_SGE_GET_FLAGS((psg)->FlagsLength)
#define MPI2_pSGE_GET_LENGTH(psg) MPI2_SGE_LENGTH((psg)->FlagsLength)
#define MPI2_pSGE_SET_FLAGS_LENGTH(psg, f, l) ((psg)->FlagsLength = \
					MPI2_SGE_SET_FLAGS_LENGTH(f, l))

/*CAUTION - The following are READ-MODIFY-WRITE! */
#define MPI2_pSGE_SET_FLAGS(psg, f) ((psg)->FlagsLength |= \
					MPI2_SGE_SET_FLAGS(f))
#define MPI2_pSGE_SET_LENGTH(psg, l) ((psg)->FlagsLength |= \
					MPI2_SGE_LENGTH(l))

#define MPI2_GET_CHAIN_OFFSET(x) ((x & MPI2_SGE_CHAIN_OFFSET_MASK) >> \
					MPI2_SGE_CHAIN_OFFSET_SHIFT)

/*****************************************************************************
*
*       Fusion-MPT IEEE Scatter Gather Elements
*
*****************************************************************************/

/****************************************************************************
* IEEE Simple Element structures
****************************************************************************/

/*MPI2_IEEE_SGE_SIMPLE32 is for MPI v2.0 products only */
typedef struct _MPI2_IEEE_SGE_SIMPLE32 {
	U32 Address;
	U32 FlagsLength;
} MPI2_IEEE_SGE_SIMPLE32, *PTR_MPI2_IEEE_SGE_SIMPLE32,
	Mpi2IeeeSgeSimple32_t, *pMpi2IeeeSgeSimple32_t;

typedef struct _MPI2_IEEE_SGE_SIMPLE64 {
	U64 Address;
	U32 Length;
	U16 Reserved1;
	U8 Reserved2;
	U8 Flags;
} MPI2_IEEE_SGE_SIMPLE64, *PTR_MPI2_IEEE_SGE_SIMPLE64,
	Mpi2IeeeSgeSimple64_t, *pMpi2IeeeSgeSimple64_t;

typedef union _MPI2_IEEE_SGE_SIMPLE_UNION {
	MPI2_IEEE_SGE_SIMPLE32 Simple32;
	MPI2_IEEE_SGE_SIMPLE64 Simple64;
} MPI2_IEEE_SGE_SIMPLE_UNION,
	*PTR_MPI2_IEEE_SGE_SIMPLE_UNION,
	Mpi2IeeeSgeSimpleUnion_t,
	*pMpi2IeeeSgeSimpleUnion_t;

/****************************************************************************
* IEEE Chain Element structures
****************************************************************************/

/*MPI2_IEEE_SGE_CHAIN32 is for MPI v2.0 products only */
typedef MPI2_IEEE_SGE_SIMPLE32 MPI2_IEEE_SGE_CHAIN32;

/*MPI2_IEEE_SGE_CHAIN64 is for MPI v2.0 products only */
typedef MPI2_IEEE_SGE_SIMPLE64 MPI2_IEEE_SGE_CHAIN64;

typedef union _MPI2_IEEE_SGE_CHAIN_UNION {
	MPI2_IEEE_SGE_CHAIN32 Chain32;
	MPI2_IEEE_SGE_CHAIN64 Chain64;
} MPI2_IEEE_SGE_CHAIN_UNION,
	*PTR_MPI2_IEEE_SGE_CHAIN_UNION,
	Mpi2IeeeSgeChainUnion_t,
	*pMpi2IeeeSgeChainUnion_t;

/*MPI25_IEEE_SGE_CHAIN64 is for MPI v2.5 and later */
typedef struct _MPI25_IEEE_SGE_CHAIN64 {
	U64 Address;
	U32 Length;
	U16 Reserved1;
	U8 NextChainOffset;
	U8 Flags;
} MPI25_IEEE_SGE_CHAIN64,
	*PTR_MPI25_IEEE_SGE_CHAIN64,
	Mpi25IeeeSgeChain64_t,
	*pMpi25IeeeSgeChain64_t;

/****************************************************************************
* All IEEE SGE types union
****************************************************************************/

/*MPI2_IEEE_SGE_UNION is for MPI v2.0 products only */
typedef struct _MPI2_IEEE_SGE_UNION {
	union {
		MPI2_IEEE_SGE_SIMPLE_UNION Simple;
		MPI2_IEEE_SGE_CHAIN_UNION Chain;
	} u;
} MPI2_IEEE_SGE_UNION, *PTR_MPI2_IEEE_SGE_UNION,
	Mpi2IeeeSgeUnion_t, *pMpi2IeeeSgeUnion_t;

/****************************************************************************
* IEEE SGE union for IO SGL's
****************************************************************************/

typedef union _MPI25_SGE_IO_UNION {
	MPI2_IEEE_SGE_SIMPLE64 IeeeSimple;
	MPI25_IEEE_SGE_CHAIN64 IeeeChain;
} MPI25_SGE_IO_UNION, *PTR_MPI25_SGE_IO_UNION,
	Mpi25SGEIOUnion_t, *pMpi25SGEIOUnion_t;

/****************************************************************************
* IEEE SGE field definitions and masks
****************************************************************************/

/*Flags field bit definitions */

#define MPI2_IEEE_SGE_FLAGS_ELEMENT_TYPE_MASK   (0x80)
#define MPI25_IEEE_SGE_FLAGS_END_OF_LIST        (0x40)

#define MPI2_IEEE32_SGE_FLAGS_SHIFT             (24)

#define MPI2_IEEE32_SGE_LENGTH_MASK             (0x00FFFFFF)

/*Element Type */

#define MPI2_IEEE_SGE_FLAGS_SIMPLE_ELEMENT      (0x00)
#define MPI2_IEEE_SGE_FLAGS_CHAIN_ELEMENT       (0x80)

/*Next Segment Format */

#define MPI26_IEEE_SGE_FLAGS_NSF_MASK           (0x1C)
#define MPI26_IEEE_SGE_FLAGS_NSF_MPI_IEEE       (0x00)

/*Data Location Address Space */

#define MPI2_IEEE_SGE_FLAGS_ADDR_MASK           (0x03)
#define MPI2_IEEE_SGE_FLAGS_SYSTEM_ADDR         (0x00)
#define MPI2_IEEE_SGE_FLAGS_IOCDDR_ADDR         (0x01)
#define MPI2_IEEE_SGE_FLAGS_IOCPLB_ADDR         (0x02)
#define MPI2_IEEE_SGE_FLAGS_IOCPLBNTA_ADDR      (0x03)
#define MPI2_IEEE_SGE_FLAGS_SYSTEMPLBPCI_ADDR   (0x03)
#define MPI2_IEEE_SGE_FLAGS_SYSTEMPLBCPI_ADDR \
	 (MPI2_IEEE_SGE_FLAGS_SYSTEMPLBPCI_ADDR)
#define MPI26_IEEE_SGE_FLAGS_IOCCTL_ADDR        (0x02)

/****************************************************************************
* IEEE SGE operation Macros
****************************************************************************/

/*SIMPLE FlagsLength manipulations... */
#define MPI2_IEEE32_SGE_SET_FLAGS(f) ((U32)(f) << MPI2_IEEE32_SGE_FLAGS_SHIFT)
#define MPI2_IEEE32_SGE_GET_FLAGS(f) (((f) & ~MPI2_IEEE32_SGE_LENGTH_MASK) \
				 >> MPI2_IEEE32_SGE_FLAGS_SHIFT)
#define MPI2_IEEE32_SGE_LENGTH(f)    ((f) & MPI2_IEEE32_SGE_LENGTH_MASK)

#define MPI2_IEEE32_SGE_SET_FLAGS_LENGTH(f, l) (MPI2_IEEE32_SGE_SET_FLAGS(f) |\
						 MPI2_IEEE32_SGE_LENGTH(l))

#define MPI2_IEEE32_pSGE_GET_FLAGS(psg) \
			MPI2_IEEE32_SGE_GET_FLAGS((psg)->FlagsLength)
#define MPI2_IEEE32_pSGE_GET_LENGTH(psg) \
			MPI2_IEEE32_SGE_LENGTH((psg)->FlagsLength)
#define MPI2_IEEE32_pSGE_SET_FLAGS_LENGTH(psg, f, l) ((psg)->FlagsLength = \
					MPI2_IEEE32_SGE_SET_FLAGS_LENGTH(f, l))

/*CAUTION - The following are READ-MODIFY-WRITE! */
#define MPI2_IEEE32_pSGE_SET_FLAGS(psg, f) ((psg)->FlagsLength |= \
					MPI2_IEEE32_SGE_SET_FLAGS(f))
#define MPI2_IEEE32_pSGE_SET_LENGTH(psg, l) ((psg)->FlagsLength |= \
					MPI2_IEEE32_SGE_LENGTH(l))

/*****************************************************************************
*
*       Fusion-MPT MPI/IEEE Scatter Gather Unions
*
*****************************************************************************/

typedef union _MPI2_SIMPLE_SGE_UNION {
	MPI2_SGE_SIMPLE_UNION MpiSimple;
	MPI2_IEEE_SGE_SIMPLE_UNION IeeeSimple;
} MPI2_SIMPLE_SGE_UNION, *PTR_MPI2_SIMPLE_SGE_UNION,
	Mpi2SimpleSgeUntion_t, *pMpi2SimpleSgeUntion_t;

typedef union _MPI2_SGE_IO_UNION {
	MPI2_SGE_SIMPLE_UNION MpiSimple;
	MPI2_SGE_CHAIN_UNION MpiChain;
	MPI2_IEEE_SGE_SIMPLE_UNION IeeeSimple;
	MPI2_IEEE_SGE_CHAIN_UNION IeeeChain;
} MPI2_SGE_IO_UNION, *PTR_MPI2_SGE_IO_UNION,
	Mpi2SGEIOUnion_t, *pMpi2SGEIOUnion_t;

/****************************************************************************
*
* Values for SGLFlags field, used in many request messages with an SGL
*
****************************************************************************/

/*values for MPI SGL Data Location Address Space subfield */
#define MPI2_SGLFLAGS_ADDRESS_SPACE_MASK            (0x0C)
#define MPI2_SGLFLAGS_SYSTEM_ADDRESS_SPACE          (0x00)
#define MPI2_SGLFLAGS_IOCDDR_ADDRESS_SPACE          (0x04)
#define MPI2_SGLFLAGS_IOCPLB_ADDRESS_SPACE          (0x08)
#define MPI26_SGLFLAGS_IOCPLB_ADDRESS_SPACE         (0x08)
#define MPI2_SGLFLAGS_IOCPLBNTA_ADDRESS_SPACE       (0x0C)
/*values for SGL Type subfield */
#define MPI2_SGLFLAGS_SGL_TYPE_MASK                 (0x03)
#define MPI2_SGLFLAGS_SGL_TYPE_MPI                  (0x00)
#define MPI2_SGLFLAGS_SGL_TYPE_IEEE32               (0x01)
#define MPI2_SGLFLAGS_SGL_TYPE_IEEE64               (0x02)

#endif
