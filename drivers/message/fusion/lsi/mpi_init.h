/*
 *  Copyright (c) 2000-2003 LSI Logic Corporation.
 *
 *
 *           Name:  mpi_init.h
 *          Title:  MPI initiator mode messages and structures
 *  Creation Date:  June 8, 2000
 *
 *    mpi_init.h Version:  01.05.xx
 *
 *  Version History
 *  ---------------
 *
 *  Date      Version   Description
 *  --------  --------  ------------------------------------------------------
 *  05-08-00  00.10.01  Original release for 0.10 spec dated 4/26/2000.
 *  05-24-00  00.10.02  Added SenseBufferLength to _MSG_SCSI_IO_REPLY.
 *  06-06-00  01.00.01  Update version number for 1.0 release.
 *  06-08-00  01.00.02  Added MPI_SCSI_RSP_INFO_ definitions.
 *  11-02-00  01.01.01  Original release for post 1.0 work.
 *  12-04-00  01.01.02  Added MPI_SCSIIO_CONTROL_NO_DISCONNECT.
 *  02-20-01  01.01.03  Started using MPI_POINTER.
 *  03-27-01  01.01.04  Added structure offset comments.
 *  04-10-01  01.01.05  Added new MsgFlag for MSG_SCSI_TASK_MGMT.
 *  08-08-01  01.02.01  Original release for v1.2 work.
 *  08-29-01  01.02.02  Added MPI_SCSITASKMGMT_TASKTYPE_LOGICAL_UNIT_RESET.
 *                      Added MPI_SCSI_STATE_QUEUE_TAG_REJECTED for
 *                      MSG_SCSI_IO_REPLY.
 *  09-28-01  01.02.03  Added structures and defines for SCSI Enclosure
 *                      Processor messages.
 *  10-04-01  01.02.04  Added defines for SEP request Action field.
 *  05-31-02  01.02.05  Added MPI_SCSIIO_MSGFLGS_CMD_DETERMINES_DATA_DIR define
 *                      for SCSI IO requests.
 *  11-15-02  01.02.06  Added special extended SCSI Status defines for FCP.
 *  06-26-03  01.02.07  Added MPI_SCSI_STATUS_FCPEXT_UNASSIGNED define.
 *  --------------------------------------------------------------------------
 */

#ifndef MPI_INIT_H
#define MPI_INIT_H


/*****************************************************************************
*
*               S C S I    I n i t i a t o r    M e s s a g e s
*
*****************************************************************************/

/****************************************************************************/
/*  SCSI IO messages and associated structures                              */
/****************************************************************************/

typedef struct _MSG_SCSI_IO_REQUEST
{
    U8                      TargetID;           /* 00h */
    U8                      Bus;                /* 01h */
    U8                      ChainOffset;        /* 02h */
    U8                      Function;           /* 03h */
    U8                      CDBLength;          /* 04h */
    U8                      SenseBufferLength;  /* 05h */
    U8                      Reserved;           /* 06h */
    U8                      MsgFlags;           /* 07h */
    U32                     MsgContext;         /* 08h */
    U8                      LUN[8];             /* 0Ch */
    U32                     Control;            /* 14h */
    U8                      CDB[16];            /* 18h */
    U32                     DataLength;         /* 28h */
    U32                     SenseBufferLowAddr; /* 2Ch */
    SGE_IO_UNION            SGL;                /* 30h */
} MSG_SCSI_IO_REQUEST, MPI_POINTER PTR_MSG_SCSI_IO_REQUEST,
  SCSIIORequest_t, MPI_POINTER pSCSIIORequest_t;


/* SCSI IO MsgFlags bits */

#define MPI_SCSIIO_MSGFLGS_SENSE_WIDTH              (0x01)
#define MPI_SCSIIO_MSGFLGS_SENSE_WIDTH_32           (0x00)
#define MPI_SCSIIO_MSGFLGS_SENSE_WIDTH_64           (0x01)
#define MPI_SCSIIO_MSGFLGS_SENSE_LOCATION           (0x02)
#define MPI_SCSIIO_MSGFLGS_SENSE_LOC_HOST           (0x00)
#define MPI_SCSIIO_MSGFLGS_SENSE_LOC_IOC            (0x02)
#define MPI_SCSIIO_MSGFLGS_CMD_DETERMINES_DATA_DIR  (0x04)
#define MPI_SCSIIO_MSGFLGS_EEDP_TYPE_MASK           (0xE0)
#define MPI_SCSIIO_MSGFLGS_EEDP_NONE                (0x00)
#define MPI_SCSIIO_MSGFLGS_EEDP_RDPROTECT_T10       (0x20)
#define MPI_SCSIIO_MSGFLGS_EEDP_VRPROTECT_T10       (0x40)
#define MPI_SCSIIO_MSGFLGS_EEDP_WRPROTECT_T10       (0x60)
#define MPI_SCSIIO_MSGFLGS_EEDP_520_READ_MODE1      (0x20)
#define MPI_SCSIIO_MSGFLGS_EEDP_520_WRITE_MODE1     (0x40)
#define MPI_SCSIIO_MSGFLGS_EEDP_8_9_READ_MODE1      (0x60)
#define MPI_SCSIIO_MSGFLGS_EEDP_8_9_WRITE_MODE1     (0x80)


/* SCSI IO LUN fields */

#define MPI_SCSIIO_LUN_FIRST_LEVEL_ADDRESSING   (0x0000FFFF)
#define MPI_SCSIIO_LUN_SECOND_LEVEL_ADDRESSING  (0xFFFF0000)
#define MPI_SCSIIO_LUN_THIRD_LEVEL_ADDRESSING   (0x0000FFFF)
#define MPI_SCSIIO_LUN_FOURTH_LEVEL_ADDRESSING  (0xFFFF0000)
#define MPI_SCSIIO_LUN_LEVEL_1_WORD             (0xFF00)
#define MPI_SCSIIO_LUN_LEVEL_1_DWORD            (0x0000FF00)

/* SCSI IO Control bits */

#define MPI_SCSIIO_CONTROL_DATADIRECTION_MASK   (0x03000000)
#define MPI_SCSIIO_CONTROL_NODATATRANSFER       (0x00000000)
#define MPI_SCSIIO_CONTROL_WRITE                (0x01000000)
#define MPI_SCSIIO_CONTROL_READ                 (0x02000000)

#define MPI_SCSIIO_CONTROL_ADDCDBLEN_MASK       (0x3C000000)
#define MPI_SCSIIO_CONTROL_ADDCDBLEN_SHIFT      (26)

#define MPI_SCSIIO_CONTROL_TASKATTRIBUTE_MASK   (0x00000700)
#define MPI_SCSIIO_CONTROL_SIMPLEQ              (0x00000000)
#define MPI_SCSIIO_CONTROL_HEADOFQ              (0x00000100)
#define MPI_SCSIIO_CONTROL_ORDEREDQ             (0x00000200)
#define MPI_SCSIIO_CONTROL_ACAQ                 (0x00000400)
#define MPI_SCSIIO_CONTROL_UNTAGGED             (0x00000500)
#define MPI_SCSIIO_CONTROL_NO_DISCONNECT        (0x00000700)

#define MPI_SCSIIO_CONTROL_TASKMANAGE_MASK      (0x00FF0000)
#define MPI_SCSIIO_CONTROL_OBSOLETE             (0x00800000)
#define MPI_SCSIIO_CONTROL_CLEAR_ACA_RSV        (0x00400000)
#define MPI_SCSIIO_CONTROL_TARGET_RESET         (0x00200000)
#define MPI_SCSIIO_CONTROL_LUN_RESET_RSV        (0x00100000)
#define MPI_SCSIIO_CONTROL_RESERVED             (0x00080000)
#define MPI_SCSIIO_CONTROL_CLR_TASK_SET_RSV     (0x00040000)
#define MPI_SCSIIO_CONTROL_ABORT_TASK_SET       (0x00020000)
#define MPI_SCSIIO_CONTROL_RESERVED2            (0x00010000)


/* SCSI IO reply structure */
typedef struct _MSG_SCSI_IO_REPLY
{
    U8                      TargetID;           /* 00h */
    U8                      Bus;                /* 01h */
    U8                      MsgLength;          /* 02h */
    U8                      Function;           /* 03h */
    U8                      CDBLength;          /* 04h */
    U8                      SenseBufferLength;  /* 05h */
    U8                      Reserved;           /* 06h */
    U8                      MsgFlags;           /* 07h */
    U32                     MsgContext;         /* 08h */
    U8                      SCSIStatus;         /* 0Ch */
    U8                      SCSIState;          /* 0Dh */
    U16                     IOCStatus;          /* 0Eh */
    U32                     IOCLogInfo;         /* 10h */
    U32                     TransferCount;      /* 14h */
    U32                     SenseCount;         /* 18h */
    U32                     ResponseInfo;       /* 1Ch */
} MSG_SCSI_IO_REPLY, MPI_POINTER PTR_MSG_SCSI_IO_REPLY,
  SCSIIOReply_t, MPI_POINTER pSCSIIOReply_t;


/* SCSI IO Reply SCSIStatus values (SAM-2 status codes) */

#define MPI_SCSI_STATUS_SUCCESS                 (0x00)
#define MPI_SCSI_STATUS_CHECK_CONDITION         (0x02)
#define MPI_SCSI_STATUS_CONDITION_MET           (0x04)
#define MPI_SCSI_STATUS_BUSY                    (0x08)
#define MPI_SCSI_STATUS_INTERMEDIATE            (0x10)
#define MPI_SCSI_STATUS_INTERMEDIATE_CONDMET    (0x14)
#define MPI_SCSI_STATUS_RESERVATION_CONFLICT    (0x18)
#define MPI_SCSI_STATUS_COMMAND_TERMINATED      (0x22)
#define MPI_SCSI_STATUS_TASK_SET_FULL           (0x28)
#define MPI_SCSI_STATUS_ACA_ACTIVE              (0x30)

#define MPI_SCSI_STATUS_FCPEXT_DEVICE_LOGGED_OUT    (0x80)
#define MPI_SCSI_STATUS_FCPEXT_NO_LINK              (0x81)
#define MPI_SCSI_STATUS_FCPEXT_UNASSIGNED           (0x82)


/* SCSI IO Reply SCSIState values */

#define MPI_SCSI_STATE_AUTOSENSE_VALID          (0x01)
#define MPI_SCSI_STATE_AUTOSENSE_FAILED         (0x02)
#define MPI_SCSI_STATE_NO_SCSI_STATUS           (0x04)
#define MPI_SCSI_STATE_TERMINATED               (0x08)
#define MPI_SCSI_STATE_RESPONSE_INFO_VALID      (0x10)
#define MPI_SCSI_STATE_QUEUE_TAG_REJECTED       (0x20)

/* SCSI IO Reply ResponseInfo values */
/* (FCP-1 RSP_CODE values and SPI-3 Packetized Failure codes) */

#define MPI_SCSI_RSP_INFO_FUNCTION_COMPLETE     (0x00000000)
#define MPI_SCSI_RSP_INFO_FCP_BURST_LEN_ERROR   (0x01000000)
#define MPI_SCSI_RSP_INFO_CMND_FIELDS_INVALID   (0x02000000)
#define MPI_SCSI_RSP_INFO_FCP_DATA_RO_ERROR     (0x03000000)
#define MPI_SCSI_RSP_INFO_TASK_MGMT_UNSUPPORTED (0x04000000)
#define MPI_SCSI_RSP_INFO_TASK_MGMT_FAILED      (0x05000000)
#define MPI_SCSI_RSP_INFO_SPI_LQ_INVALID_TYPE   (0x06000000)


/****************************************************************************/
/*  SCSI IO 32 Request message structure                                    */
/****************************************************************************/

typedef struct _MSG_SCSI_IO32_REQUEST
{
    U8                      TargetID;           /* 00h */
    U8                      Bus;                /* 01h */
    U8                      ChainOffset;        /* 02h */
    U8                      Function;           /* 03h */
    U8                      CDBLength;          /* 04h */
    U8                      SenseBufferLength;  /* 05h */
    U8                      Reserved;           /* 06h */
    U8                      MsgFlags;           /* 07h */
    U32                     MsgContext;         /* 08h */
    U8                      LUN[8];             /* 0Ch */
    U32                     Control;            /* 14h */
    U8                      CDB[32];            /* 18h */
    U32                     DataLength;         /* 38h */
    U32                     SenseBufferLowAddr; /* 3Ch */
    SGE_IO_UNION            SGL;                /* 40h */
} MSG_SCSI_IO32_REQUEST, MPI_POINTER PTR_MSG_SCSI_IO32_REQUEST,
  SCSIIO32Request_t, MPI_POINTER pSCSIIO32Request_t;

/* SCSI IO 32 uses the same defines as above for SCSI IO */


/****************************************************************************/
/*  SCSI Task Management messages                                           */
/****************************************************************************/

typedef struct _MSG_SCSI_TASK_MGMT
{
    U8                      TargetID;           /* 00h */
    U8                      Bus;                /* 01h */
    U8                      ChainOffset;        /* 02h */
    U8                      Function;           /* 03h */
    U8                      Reserved;           /* 04h */
    U8                      TaskType;           /* 05h */
    U8                      Reserved1;          /* 06h */
    U8                      MsgFlags;           /* 07h */
    U32                     MsgContext;         /* 08h */
    U8                      LUN[8];             /* 0Ch */
    U32                     Reserved2[7];       /* 14h */
    U32                     TaskMsgContext;     /* 30h */
} MSG_SCSI_TASK_MGMT, MPI_POINTER PTR_SCSI_TASK_MGMT,
  SCSITaskMgmt_t, MPI_POINTER pSCSITaskMgmt_t;

/* TaskType values */

#define MPI_SCSITASKMGMT_TASKTYPE_ABORT_TASK            (0x01)
#define MPI_SCSITASKMGMT_TASKTYPE_ABRT_TASK_SET         (0x02)
#define MPI_SCSITASKMGMT_TASKTYPE_TARGET_RESET          (0x03)
#define MPI_SCSITASKMGMT_TASKTYPE_RESET_BUS             (0x04)
#define MPI_SCSITASKMGMT_TASKTYPE_LOGICAL_UNIT_RESET    (0x05)
#define MPI_SCSITASKMGMT_TASKTYPE_CLEAR_TASK_SET        (0x06)

/* MsgFlags bits */
#define MPI_SCSITASKMGMT_MSGFLAGS_TARGET_RESET_OPTION   (0x00)
#define MPI_SCSITASKMGMT_MSGFLAGS_LIP_RESET_OPTION      (0x02)
#define MPI_SCSITASKMGMT_MSGFLAGS_LIPRESET_RESET_OPTION (0x04)

/* SCSI Task Management Reply */
typedef struct _MSG_SCSI_TASK_MGMT_REPLY
{
    U8                      TargetID;           /* 00h */
    U8                      Bus;                /* 01h */
    U8                      MsgLength;          /* 02h */
    U8                      Function;           /* 03h */
    U8                      Reserved;           /* 04h */
    U8                      TaskType;           /* 05h */
    U8                      Reserved1;          /* 06h */
    U8                      MsgFlags;           /* 07h */
    U32                     MsgContext;         /* 08h */
    U8                      Reserved2[2];       /* 0Ch */
    U16                     IOCStatus;          /* 0Eh */
    U32                     IOCLogInfo;         /* 10h */
    U32                     TerminationCount;   /* 14h */
} MSG_SCSI_TASK_MGMT_REPLY, MPI_POINTER PTR_MSG_SCSI_TASK_MGMT_REPLY,
  SCSITaskMgmtReply_t, MPI_POINTER pSCSITaskMgmtReply_t;


/****************************************************************************/
/*  SCSI Enclosure Processor messages                                       */
/****************************************************************************/

typedef struct _MSG_SEP_REQUEST
{
    U8                      TargetID;           /* 00h */
    U8                      Bus;                /* 01h */
    U8                      ChainOffset;        /* 02h */
    U8                      Function;           /* 03h */
    U8                      Action;             /* 04h */
    U8                      Reserved1;          /* 05h */
    U8                      Reserved2;          /* 06h */
    U8                      MsgFlags;           /* 07h */
    U32                     MsgContext;         /* 08h */
    U32                     SlotStatus;         /* 0Ch */
} MSG_SEP_REQUEST, MPI_POINTER PTR_MSG_SEP_REQUEST,
  SEPRequest_t, MPI_POINTER pSEPRequest_t;

/* Action defines */
#define MPI_SEP_REQ_ACTION_WRITE_STATUS                 (0x00)
#define MPI_SEP_REQ_ACTION_READ_STATUS                  (0x01)

/* SlotStatus bits for MSG_SEP_REQUEST */
#define MPI_SEP_REQ_SLOTSTATUS_NO_ERROR                 (0x00000001)
#define MPI_SEP_REQ_SLOTSTATUS_DEV_FAULTY               (0x00000002)
#define MPI_SEP_REQ_SLOTSTATUS_DEV_REBUILDING           (0x00000004)
#define MPI_SEP_REQ_SLOTSTATUS_IN_FAILED_ARRAY          (0x00000008)
#define MPI_SEP_REQ_SLOTSTATUS_IN_CRITICAL_ARRAY        (0x00000010)
#define MPI_SEP_REQ_SLOTSTATUS_PARITY_CHECK             (0x00000020)
#define MPI_SEP_REQ_SLOTSTATUS_PREDICTED_FAULT          (0x00000040)
#define MPI_SEP_REQ_SLOTSTATUS_UNCONFIGURED             (0x00000080)
#define MPI_SEP_REQ_SLOTSTATUS_HOT_SPARE                (0x00000100)
#define MPI_SEP_REQ_SLOTSTATUS_REBUILD_STOPPED          (0x00000200)
#define MPI_SEP_REQ_SLOTSTATUS_IDENTIFY_REQUEST         (0x00020000)
#define MPI_SEP_REQ_SLOTSTATUS_REQUEST_REMOVE           (0x00040000)
#define MPI_SEP_REQ_SLOTSTATUS_REQUEST_INSERT           (0x00080000)
#define MPI_SEP_REQ_SLOTSTATUS_DO_NOT_MOVE              (0x00400000)
#define MPI_SEP_REQ_SLOTSTATUS_B_ENABLE_BYPASS          (0x04000000)
#define MPI_SEP_REQ_SLOTSTATUS_A_ENABLE_BYPASS          (0x08000000)
#define MPI_SEP_REQ_SLOTSTATUS_DEV_OFF                  (0x10000000)
#define MPI_SEP_REQ_SLOTSTATUS_SWAP_RESET               (0x80000000)


typedef struct _MSG_SEP_REPLY
{
    U8                      TargetID;           /* 00h */
    U8                      Bus;                /* 01h */
    U8                      MsgLength;          /* 02h */
    U8                      Function;           /* 03h */
    U8                      Action;             /* 04h */
    U8                      Reserved1;          /* 05h */
    U8                      Reserved2;          /* 06h */
    U8                      MsgFlags;           /* 07h */
    U32                     MsgContext;         /* 08h */
    U16                     Reserved3;          /* 0Ch */
    U16                     IOCStatus;          /* 0Eh */
    U32                     IOCLogInfo;         /* 10h */
    U32                     SlotStatus;         /* 14h */
} MSG_SEP_REPLY, MPI_POINTER PTR_MSG_SEP_REPLY,
  SEPReply_t, MPI_POINTER pSEPReply_t;

/* SlotStatus bits for MSG_SEP_REPLY */
#define MPI_SEP_REPLY_SLOTSTATUS_NO_ERROR               (0x00000001)
#define MPI_SEP_REPLY_SLOTSTATUS_DEV_FAULTY             (0x00000002)
#define MPI_SEP_REPLY_SLOTSTATUS_DEV_REBUILDING         (0x00000004)
#define MPI_SEP_REPLY_SLOTSTATUS_IN_FAILED_ARRAY        (0x00000008)
#define MPI_SEP_REPLY_SLOTSTATUS_IN_CRITICAL_ARRAY      (0x00000010)
#define MPI_SEP_REPLY_SLOTSTATUS_PARITY_CHECK           (0x00000020)
#define MPI_SEP_REPLY_SLOTSTATUS_PREDICTED_FAULT        (0x00000040)
#define MPI_SEP_REPLY_SLOTSTATUS_UNCONFIGURED           (0x00000080)
#define MPI_SEP_REPLY_SLOTSTATUS_HOT_SPARE              (0x00000100)
#define MPI_SEP_REPLY_SLOTSTATUS_REBUILD_STOPPED        (0x00000200)
#define MPI_SEP_REPLY_SLOTSTATUS_REPORT                 (0x00010000)
#define MPI_SEP_REPLY_SLOTSTATUS_IDENTIFY_REQUEST       (0x00020000)
#define MPI_SEP_REPLY_SLOTSTATUS_REMOVE_READY           (0x00040000)
#define MPI_SEP_REPLY_SLOTSTATUS_INSERT_READY           (0x00080000)
#define MPI_SEP_REPLY_SLOTSTATUS_DO_NOT_REMOVE          (0x00400000)
#define MPI_SEP_REPLY_SLOTSTATUS_B_BYPASS_ENABLED       (0x01000000)
#define MPI_SEP_REPLY_SLOTSTATUS_A_BYPASS_ENABLED       (0x02000000)
#define MPI_SEP_REPLY_SLOTSTATUS_B_ENABLE_BYPASS        (0x04000000)
#define MPI_SEP_REPLY_SLOTSTATUS_A_ENABLE_BYPASS        (0x08000000)
#define MPI_SEP_REPLY_SLOTSTATUS_DEV_OFF                (0x10000000)
#define MPI_SEP_REPLY_SLOTSTATUS_FAULT_SENSED           (0x40000000)
#define MPI_SEP_REPLY_SLOTSTATUS_SWAPPED                (0x80000000)

#endif
