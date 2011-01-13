/*
 *  Copyright (c) 2000-2010 LSI Corporation.
 *
 *
 *           Name:  mpi2_sas.h
 *          Title:  MPI Serial Attached SCSI structures and definitions
 *  Creation Date:  February 9, 2007
 *
 *  mpi2_sas.h Version:  02.00.04
 *
 *  Version History
 *  ---------------
 *
 *  Date      Version   Description
 *  --------  --------  ------------------------------------------------------
 *  04-30-07  02.00.00  Corresponds to Fusion-MPT MPI Specification Rev A.
 *  06-26-07  02.00.01  Added Clear All Persistent Operation to SAS IO Unit
 *                      Control Request.
 *  10-02-08  02.00.02  Added Set IOC Parameter Operation to SAS IO Unit Control
 *                      Request.
 *  10-28-09  02.00.03  Changed the type of SGL in MPI2_SATA_PASSTHROUGH_REQUEST
 *                      to MPI2_SGE_IO_UNION since it supports chained SGLs.
 *  05-12-10  02.00.04  Modified some comments.
 *  --------------------------------------------------------------------------
 */

#ifndef MPI2_SAS_H
#define MPI2_SAS_H

/*
 * Values for SASStatus.
 */
#define MPI2_SASSTATUS_SUCCESS                          (0x00)
#define MPI2_SASSTATUS_UNKNOWN_ERROR                    (0x01)
#define MPI2_SASSTATUS_INVALID_FRAME                    (0x02)
#define MPI2_SASSTATUS_UTC_BAD_DEST                     (0x03)
#define MPI2_SASSTATUS_UTC_BREAK_RECEIVED               (0x04)
#define MPI2_SASSTATUS_UTC_CONNECT_RATE_NOT_SUPPORTED   (0x05)
#define MPI2_SASSTATUS_UTC_PORT_LAYER_REQUEST           (0x06)
#define MPI2_SASSTATUS_UTC_PROTOCOL_NOT_SUPPORTED       (0x07)
#define MPI2_SASSTATUS_UTC_STP_RESOURCES_BUSY           (0x08)
#define MPI2_SASSTATUS_UTC_WRONG_DESTINATION            (0x09)
#define MPI2_SASSTATUS_SHORT_INFORMATION_UNIT           (0x0A)
#define MPI2_SASSTATUS_LONG_INFORMATION_UNIT            (0x0B)
#define MPI2_SASSTATUS_XFER_RDY_INCORRECT_WRITE_DATA    (0x0C)
#define MPI2_SASSTATUS_XFER_RDY_REQUEST_OFFSET_ERROR    (0x0D)
#define MPI2_SASSTATUS_XFER_RDY_NOT_EXPECTED            (0x0E)
#define MPI2_SASSTATUS_DATA_INCORRECT_DATA_LENGTH       (0x0F)
#define MPI2_SASSTATUS_DATA_TOO_MUCH_READ_DATA          (0x10)
#define MPI2_SASSTATUS_DATA_OFFSET_ERROR                (0x11)
#define MPI2_SASSTATUS_SDSF_NAK_RECEIVED                (0x12)
#define MPI2_SASSTATUS_SDSF_CONNECTION_FAILED           (0x13)
#define MPI2_SASSTATUS_INITIATOR_RESPONSE_TIMEOUT       (0x14)


/*
 * Values for the SAS DeviceInfo field used in SAS Device Status Change Event
 * data and SAS Configuration pages.
 */
#define MPI2_SAS_DEVICE_INFO_SEP                (0x00004000)
#define MPI2_SAS_DEVICE_INFO_ATAPI_DEVICE       (0x00002000)
#define MPI2_SAS_DEVICE_INFO_LSI_DEVICE         (0x00001000)
#define MPI2_SAS_DEVICE_INFO_DIRECT_ATTACH      (0x00000800)
#define MPI2_SAS_DEVICE_INFO_SSP_TARGET         (0x00000400)
#define MPI2_SAS_DEVICE_INFO_STP_TARGET         (0x00000200)
#define MPI2_SAS_DEVICE_INFO_SMP_TARGET         (0x00000100)
#define MPI2_SAS_DEVICE_INFO_SATA_DEVICE        (0x00000080)
#define MPI2_SAS_DEVICE_INFO_SSP_INITIATOR      (0x00000040)
#define MPI2_SAS_DEVICE_INFO_STP_INITIATOR      (0x00000020)
#define MPI2_SAS_DEVICE_INFO_SMP_INITIATOR      (0x00000010)
#define MPI2_SAS_DEVICE_INFO_SATA_HOST          (0x00000008)

#define MPI2_SAS_DEVICE_INFO_MASK_DEVICE_TYPE   (0x00000007)
#define MPI2_SAS_DEVICE_INFO_NO_DEVICE          (0x00000000)
#define MPI2_SAS_DEVICE_INFO_END_DEVICE         (0x00000001)
#define MPI2_SAS_DEVICE_INFO_EDGE_EXPANDER      (0x00000002)
#define MPI2_SAS_DEVICE_INFO_FANOUT_EXPANDER    (0x00000003)


/*****************************************************************************
*
*        SAS Messages
*
*****************************************************************************/

/****************************************************************************
*  SMP Passthrough messages
****************************************************************************/

/* SMP Passthrough Request Message */
typedef struct _MPI2_SMP_PASSTHROUGH_REQUEST
{
    U8                      PassthroughFlags;   /* 0x00 */
    U8                      PhysicalPort;       /* 0x01 */
    U8                      ChainOffset;        /* 0x02 */
    U8                      Function;           /* 0x03 */
    U16                     RequestDataLength;  /* 0x04 */
    U8                      SGLFlags;           /* 0x06 */
    U8                      MsgFlags;           /* 0x07 */
    U8                      VP_ID;              /* 0x08 */
    U8                      VF_ID;              /* 0x09 */
    U16                     Reserved1;          /* 0x0A */
    U32                     Reserved2;          /* 0x0C */
    U64                     SASAddress;         /* 0x10 */
    U32                     Reserved3;          /* 0x18 */
    U32                     Reserved4;          /* 0x1C */
    MPI2_SIMPLE_SGE_UNION   SGL;                /* 0x20 */
} MPI2_SMP_PASSTHROUGH_REQUEST, MPI2_POINTER PTR_MPI2_SMP_PASSTHROUGH_REQUEST,
  Mpi2SmpPassthroughRequest_t, MPI2_POINTER pMpi2SmpPassthroughRequest_t;

/* values for PassthroughFlags field */
#define MPI2_SMP_PT_REQ_PT_FLAGS_IMMEDIATE      (0x80)

/* use MPI2_SGLFLAGS_ defines from mpi2.h for the SGLFlags field */


/* SMP Passthrough Reply Message */
typedef struct _MPI2_SMP_PASSTHROUGH_REPLY
{
    U8                      PassthroughFlags;   /* 0x00 */
    U8                      PhysicalPort;       /* 0x01 */
    U8                      MsgLength;          /* 0x02 */
    U8                      Function;           /* 0x03 */
    U16                     ResponseDataLength; /* 0x04 */
    U8                      SGLFlags;           /* 0x06 */
    U8                      MsgFlags;           /* 0x07 */
    U8                      VP_ID;              /* 0x08 */
    U8                      VF_ID;              /* 0x09 */
    U16                     Reserved1;          /* 0x0A */
    U8                      Reserved2;          /* 0x0C */
    U8                      SASStatus;          /* 0x0D */
    U16                     IOCStatus;          /* 0x0E */
    U32                     IOCLogInfo;         /* 0x10 */
    U32                     Reserved3;          /* 0x14 */
    U8                      ResponseData[4];    /* 0x18 */
} MPI2_SMP_PASSTHROUGH_REPLY, MPI2_POINTER PTR_MPI2_SMP_PASSTHROUGH_REPLY,
  Mpi2SmpPassthroughReply_t, MPI2_POINTER pMpi2SmpPassthroughReply_t;

/* values for PassthroughFlags field */
#define MPI2_SMP_PT_REPLY_PT_FLAGS_IMMEDIATE    (0x80)

/* values for SASStatus field are at the top of this file */


/****************************************************************************
*  SATA Passthrough messages
****************************************************************************/

/* SATA Passthrough Request Message */
typedef struct _MPI2_SATA_PASSTHROUGH_REQUEST
{
    U16                     DevHandle;          /* 0x00 */
    U8                      ChainOffset;        /* 0x02 */
    U8                      Function;           /* 0x03 */
    U16                     PassthroughFlags;   /* 0x04 */
    U8                      SGLFlags;           /* 0x06 */
    U8                      MsgFlags;           /* 0x07 */
    U8                      VP_ID;              /* 0x08 */
    U8                      VF_ID;              /* 0x09 */
    U16                     Reserved1;          /* 0x0A */
    U32                     Reserved2;          /* 0x0C */
    U32                     Reserved3;          /* 0x10 */
    U32                     Reserved4;          /* 0x14 */
    U32                     DataLength;         /* 0x18 */
    U8                      CommandFIS[20];     /* 0x1C */
    MPI2_SGE_IO_UNION       SGL;                /* 0x20 */
} MPI2_SATA_PASSTHROUGH_REQUEST, MPI2_POINTER PTR_MPI2_SATA_PASSTHROUGH_REQUEST,
  Mpi2SataPassthroughRequest_t, MPI2_POINTER pMpi2SataPassthroughRequest_t;

/* values for PassthroughFlags field */
#define MPI2_SATA_PT_REQ_PT_FLAGS_EXECUTE_DIAG      (0x0100)
#define MPI2_SATA_PT_REQ_PT_FLAGS_DMA               (0x0020)
#define MPI2_SATA_PT_REQ_PT_FLAGS_PIO               (0x0010)
#define MPI2_SATA_PT_REQ_PT_FLAGS_UNSPECIFIED_VU    (0x0004)
#define MPI2_SATA_PT_REQ_PT_FLAGS_WRITE             (0x0002)
#define MPI2_SATA_PT_REQ_PT_FLAGS_READ              (0x0001)

/* use MPI2_SGLFLAGS_ defines from mpi2.h for the SGLFlags field */


/* SATA Passthrough Reply Message */
typedef struct _MPI2_SATA_PASSTHROUGH_REPLY
{
    U16                     DevHandle;          /* 0x00 */
    U8                      MsgLength;          /* 0x02 */
    U8                      Function;           /* 0x03 */
    U16                     PassthroughFlags;   /* 0x04 */
    U8                      SGLFlags;           /* 0x06 */
    U8                      MsgFlags;           /* 0x07 */
    U8                      VP_ID;              /* 0x08 */
    U8                      VF_ID;              /* 0x09 */
    U16                     Reserved1;          /* 0x0A */
    U8                      Reserved2;          /* 0x0C */
    U8                      SASStatus;          /* 0x0D */
    U16                     IOCStatus;          /* 0x0E */
    U32                     IOCLogInfo;         /* 0x10 */
    U8                      StatusFIS[20];      /* 0x14 */
    U32                     StatusControlRegisters; /* 0x28 */
    U32                     TransferCount;      /* 0x2C */
} MPI2_SATA_PASSTHROUGH_REPLY, MPI2_POINTER PTR_MPI2_SATA_PASSTHROUGH_REPLY,
  Mpi2SataPassthroughReply_t, MPI2_POINTER pMpi2SataPassthroughReply_t;

/* values for SASStatus field are at the top of this file */


/****************************************************************************
*  SAS IO Unit Control messages
****************************************************************************/

/* SAS IO Unit Control Request Message */
typedef struct _MPI2_SAS_IOUNIT_CONTROL_REQUEST
{
    U8                      Operation;          /* 0x00 */
    U8                      Reserved1;          /* 0x01 */
    U8                      ChainOffset;        /* 0x02 */
    U8                      Function;           /* 0x03 */
    U16                     DevHandle;          /* 0x04 */
    U8                      IOCParameter;       /* 0x06 */
    U8                      MsgFlags;           /* 0x07 */
    U8                      VP_ID;              /* 0x08 */
    U8                      VF_ID;              /* 0x09 */
    U16                     Reserved3;          /* 0x0A */
    U16                     Reserved4;          /* 0x0C */
    U8                      PhyNum;             /* 0x0E */
    U8                      PrimFlags;          /* 0x0F */
    U32                     Primitive;          /* 0x10 */
    U8                      LookupMethod;       /* 0x14 */
    U8                      Reserved5;          /* 0x15 */
    U16                     SlotNumber;         /* 0x16 */
    U64                     LookupAddress;      /* 0x18 */
    U32                     IOCParameterValue;  /* 0x20 */
    U32                     Reserved7;          /* 0x24 */
    U32                     Reserved8;          /* 0x28 */
} MPI2_SAS_IOUNIT_CONTROL_REQUEST,
  MPI2_POINTER PTR_MPI2_SAS_IOUNIT_CONTROL_REQUEST,
  Mpi2SasIoUnitControlRequest_t, MPI2_POINTER pMpi2SasIoUnitControlRequest_t;

/* values for the Operation field */
#define MPI2_SAS_OP_CLEAR_ALL_PERSISTENT        (0x02)
#define MPI2_SAS_OP_PHY_LINK_RESET              (0x06)
#define MPI2_SAS_OP_PHY_HARD_RESET              (0x07)
#define MPI2_SAS_OP_PHY_CLEAR_ERROR_LOG         (0x08)
#define MPI2_SAS_OP_SEND_PRIMITIVE              (0x0A)
#define MPI2_SAS_OP_FORCE_FULL_DISCOVERY        (0x0B)
#define MPI2_SAS_OP_TRANSMIT_PORT_SELECT_SIGNAL (0x0C)
#define MPI2_SAS_OP_REMOVE_DEVICE               (0x0D)
#define MPI2_SAS_OP_LOOKUP_MAPPING              (0x0E)
#define MPI2_SAS_OP_SET_IOC_PARAMETER           (0x0F)
#define MPI2_SAS_OP_PRODUCT_SPECIFIC_MIN        (0x80)

/* values for the PrimFlags field */
#define MPI2_SAS_PRIMFLAGS_SINGLE               (0x08)
#define MPI2_SAS_PRIMFLAGS_TRIPLE               (0x02)
#define MPI2_SAS_PRIMFLAGS_REDUNDANT            (0x01)

/* values for the LookupMethod field */
#define MPI2_SAS_LOOKUP_METHOD_SAS_ADDRESS          (0x01)
#define MPI2_SAS_LOOKUP_METHOD_SAS_ENCLOSURE_SLOT   (0x02)
#define MPI2_SAS_LOOKUP_METHOD_SAS_DEVICE_NAME      (0x03)


/* SAS IO Unit Control Reply Message */
typedef struct _MPI2_SAS_IOUNIT_CONTROL_REPLY
{
    U8                      Operation;          /* 0x00 */
    U8                      Reserved1;          /* 0x01 */
    U8                      MsgLength;          /* 0x02 */
    U8                      Function;           /* 0x03 */
    U16                     DevHandle;          /* 0x04 */
    U8                      IOCParameter;       /* 0x06 */
    U8                      MsgFlags;           /* 0x07 */
    U8                      VP_ID;              /* 0x08 */
    U8                      VF_ID;              /* 0x09 */
    U16                     Reserved3;          /* 0x0A */
    U16                     Reserved4;          /* 0x0C */
    U16                     IOCStatus;          /* 0x0E */
    U32                     IOCLogInfo;         /* 0x10 */
} MPI2_SAS_IOUNIT_CONTROL_REPLY,
  MPI2_POINTER PTR_MPI2_SAS_IOUNIT_CONTROL_REPLY,
  Mpi2SasIoUnitControlReply_t, MPI2_POINTER pMpi2SasIoUnitControlReply_t;


#endif


