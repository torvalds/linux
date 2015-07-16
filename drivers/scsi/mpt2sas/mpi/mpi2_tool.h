/*
 *  Copyright (c) 2000-2014 LSI Corporation.
 *
 *
 *           Name:  mpi2_tool.h
 *          Title:  MPI diagnostic tool structures and definitions
 *  Creation Date:  March 26, 2007
 *
 *    mpi2_tool.h Version:  02.00.12
 *
 *  Version History
 *  ---------------
 *
 *  Date      Version   Description
 *  --------  --------  ------------------------------------------------------
 *  04-30-07  02.00.00  Corresponds to Fusion-MPT MPI Specification Rev A.
 *  12-18-07  02.00.01  Added Diagnostic Buffer Post and Diagnostic Release
 *                      structures and defines.
 *  02-29-08  02.00.02  Modified various names to make them 32-character unique.
 *  05-06-09  02.00.03  Added ISTWI Read Write Tool and Diagnostic CLI Tool.
 *  07-30-09  02.00.04  Added ExtendedType field to DiagnosticBufferPost request
 *                      and reply messages.
 *                      Added MPI2_DIAG_BUF_TYPE_EXTENDED.
 *                      Incremented MPI2_DIAG_BUF_TYPE_COUNT.
 *  05-12-10  02.00.05  Added Diagnostic Data Upload tool.
 *  08-11-10  02.00.06  Added defines that were missing for Diagnostic Buffer
 *                      Post Request.
 *  05-25-11  02.00.07  Added Flags field and related defines to
 *                      MPI2_TOOLBOX_ISTWI_READ_WRITE_REQUEST.
 *  07-26-12  02.00.10  Modified MPI2_TOOLBOX_DIAGNOSTIC_CLI_REQUEST so that
 *			it uses MPI Chain SGE as well as MPI Simple SGE.
 *  08-19-13  02.00.11  Added MPI2_TOOLBOX_TEXT_DISPLAY_TOOL and related info.
 *  01-08-14  02.00.12  Added MPI2_TOOLBOX_CLEAN_BIT26_PRODUCT_SPECIFIC.
 *  --------------------------------------------------------------------------
 */

#ifndef MPI2_TOOL_H
#define MPI2_TOOL_H

/*****************************************************************************
*
*               Toolbox Messages
*
*****************************************************************************/

/* defines for the Tools */
#define MPI2_TOOLBOX_CLEAN_TOOL                     (0x00)
#define MPI2_TOOLBOX_MEMORY_MOVE_TOOL               (0x01)
#define MPI2_TOOLBOX_DIAG_DATA_UPLOAD_TOOL          (0x02)
#define MPI2_TOOLBOX_ISTWI_READ_WRITE_TOOL          (0x03)
#define MPI2_TOOLBOX_BEACON_TOOL                    (0x05)
#define MPI2_TOOLBOX_DIAGNOSTIC_CLI_TOOL            (0x06)
#define MPI2_TOOLBOX_TEXT_DISPLAY_TOOL              (0x07)


/****************************************************************************
*  Toolbox reply
****************************************************************************/

typedef struct _MPI2_TOOLBOX_REPLY
{
    U8                      Tool;                       /* 0x00 */
    U8                      Reserved1;                  /* 0x01 */
    U8                      MsgLength;                  /* 0x02 */
    U8                      Function;                   /* 0x03 */
    U16                     Reserved2;                  /* 0x04 */
    U8                      Reserved3;                  /* 0x06 */
    U8                      MsgFlags;                   /* 0x07 */
    U8                      VP_ID;                      /* 0x08 */
    U8                      VF_ID;                      /* 0x09 */
    U16                     Reserved4;                  /* 0x0A */
    U16                     Reserved5;                  /* 0x0C */
    U16                     IOCStatus;                  /* 0x0E */
    U32                     IOCLogInfo;                 /* 0x10 */
} MPI2_TOOLBOX_REPLY, MPI2_POINTER PTR_MPI2_TOOLBOX_REPLY,
  Mpi2ToolboxReply_t, MPI2_POINTER pMpi2ToolboxReply_t;


/****************************************************************************
*  Toolbox Clean Tool request
****************************************************************************/

typedef struct _MPI2_TOOLBOX_CLEAN_REQUEST
{
    U8                      Tool;                       /* 0x00 */
    U8                      Reserved1;                  /* 0x01 */
    U8                      ChainOffset;                /* 0x02 */
    U8                      Function;                   /* 0x03 */
    U16                     Reserved2;                  /* 0x04 */
    U8                      Reserved3;                  /* 0x06 */
    U8                      MsgFlags;                   /* 0x07 */
    U8                      VP_ID;                      /* 0x08 */
    U8                      VF_ID;                      /* 0x09 */
    U16                     Reserved4;                  /* 0x0A */
    U32                     Flags;                      /* 0x0C */
   } MPI2_TOOLBOX_CLEAN_REQUEST, MPI2_POINTER PTR_MPI2_TOOLBOX_CLEAN_REQUEST,
  Mpi2ToolboxCleanRequest_t, MPI2_POINTER pMpi2ToolboxCleanRequest_t;

/* values for the Flags field */
#define MPI2_TOOLBOX_CLEAN_BOOT_SERVICES            (0x80000000)
#define MPI2_TOOLBOX_CLEAN_PERSIST_MANUFACT_PAGES   (0x40000000)
#define MPI2_TOOLBOX_CLEAN_OTHER_PERSIST_PAGES      (0x20000000)
#define MPI2_TOOLBOX_CLEAN_FW_CURRENT               (0x10000000)
#define MPI2_TOOLBOX_CLEAN_FW_BACKUP                (0x08000000)
#define MPI2_TOOLBOX_CLEAN_BIT26_PRODUCT_SPECIFIC   (0x04000000)
#define MPI2_TOOLBOX_CLEAN_MEGARAID                 (0x02000000)
#define MPI2_TOOLBOX_CLEAN_INITIALIZATION           (0x01000000)
#define MPI2_TOOLBOX_CLEAN_FLASH                    (0x00000004)
#define MPI2_TOOLBOX_CLEAN_SEEPROM                  (0x00000002)
#define MPI2_TOOLBOX_CLEAN_NVSRAM                   (0x00000001)


/****************************************************************************
*  Toolbox Memory Move request
****************************************************************************/

typedef struct _MPI2_TOOLBOX_MEM_MOVE_REQUEST {
    U8                      Tool;                       /* 0x00 */
    U8                      Reserved1;                  /* 0x01 */
    U8                      ChainOffset;                /* 0x02 */
    U8                      Function;                   /* 0x03 */
    U16                     Reserved2;                  /* 0x04 */
    U8                      Reserved3;                  /* 0x06 */
    U8                      MsgFlags;                   /* 0x07 */
    U8                      VP_ID;                      /* 0x08 */
    U8                      VF_ID;                      /* 0x09 */
    U16                     Reserved4;                  /* 0x0A */
    MPI2_SGE_SIMPLE_UNION   SGL;                        /* 0x0C */
} MPI2_TOOLBOX_MEM_MOVE_REQUEST, MPI2_POINTER PTR_MPI2_TOOLBOX_MEM_MOVE_REQUEST,
  Mpi2ToolboxMemMoveRequest_t, MPI2_POINTER pMpi2ToolboxMemMoveRequest_t;


/****************************************************************************
*  Toolbox Diagnostic Data Upload request
****************************************************************************/

typedef struct _MPI2_TOOLBOX_DIAG_DATA_UPLOAD_REQUEST {
	U8                      Tool;                       /* 0x00 */
	U8                      Reserved1;                  /* 0x01 */
	U8                      ChainOffset;                /* 0x02 */
	U8                      Function;                   /* 0x03 */
	U16                     Reserved2;                  /* 0x04 */
	U8                      Reserved3;                  /* 0x06 */
	U8                      MsgFlags;                   /* 0x07 */
	U8                      VP_ID;                      /* 0x08 */
	U8                      VF_ID;                      /* 0x09 */
	U16                     Reserved4;                  /* 0x0A */
	U8                      SGLFlags;                   /* 0x0C */
	U8                      Reserved5;                  /* 0x0D */
	U16                     Reserved6;                  /* 0x0E */
	U32                     Flags;                      /* 0x10 */
	U32                     DataLength;                 /* 0x14 */
	MPI2_SGE_SIMPLE_UNION   SGL;                        /* 0x18 */
} MPI2_TOOLBOX_DIAG_DATA_UPLOAD_REQUEST,
MPI2_POINTER PTR_MPI2_TOOLBOX_DIAG_DATA_UPLOAD_REQUEST,
Mpi2ToolboxDiagDataUploadRequest_t,
MPI2_POINTER pMpi2ToolboxDiagDataUploadRequest_t;

/* use MPI2_SGLFLAGS_ defines from mpi2.h for the SGLFlags field */


typedef struct _MPI2_DIAG_DATA_UPLOAD_HEADER {
	U32                     DiagDataLength;             /* 00h */
	U8                      FormatCode;                 /* 04h */
	U8                      Reserved1;                  /* 05h */
	U16                     Reserved2;                  /* 06h */
} MPI2_DIAG_DATA_UPLOAD_HEADER, MPI2_POINTER PTR_MPI2_DIAG_DATA_UPLOAD_HEADER,
Mpi2DiagDataUploadHeader_t, MPI2_POINTER pMpi2DiagDataUploadHeader_t;


/****************************************************************************
*  Toolbox ISTWI Read Write Tool
****************************************************************************/

/* Toolbox ISTWI Read Write Tool request message */
typedef struct _MPI2_TOOLBOX_ISTWI_READ_WRITE_REQUEST {
    U8                      Tool;                       /* 0x00 */
    U8                      Reserved1;                  /* 0x01 */
    U8                      ChainOffset;                /* 0x02 */
    U8                      Function;                   /* 0x03 */
    U16                     Reserved2;                  /* 0x04 */
    U8                      Reserved3;                  /* 0x06 */
    U8                      MsgFlags;                   /* 0x07 */
    U8                      VP_ID;                      /* 0x08 */
    U8                      VF_ID;                      /* 0x09 */
    U16                     Reserved4;                  /* 0x0A */
    U32                     Reserved5;                  /* 0x0C */
    U32                     Reserved6;                  /* 0x10 */
    U8                      DevIndex;                   /* 0x14 */
    U8                      Action;                     /* 0x15 */
    U8                      SGLFlags;                   /* 0x16 */
	 U8                      Flags;                      /* 0x17 */
    U16                     TxDataLength;               /* 0x18 */
    U16                     RxDataLength;               /* 0x1A */
    U32                     Reserved8;                  /* 0x1C */
    U32                     Reserved9;                  /* 0x20 */
    U32                     Reserved10;                 /* 0x24 */
    U32                     Reserved11;                 /* 0x28 */
    U32                     Reserved12;                 /* 0x2C */
    MPI2_SGE_SIMPLE_UNION   SGL;                        /* 0x30 */
} MPI2_TOOLBOX_ISTWI_READ_WRITE_REQUEST,
  MPI2_POINTER PTR_MPI2_TOOLBOX_ISTWI_READ_WRITE_REQUEST,
  Mpi2ToolboxIstwiReadWriteRequest_t,
  MPI2_POINTER pMpi2ToolboxIstwiReadWriteRequest_t;

/* values for the Action field */
#define MPI2_TOOL_ISTWI_ACTION_READ_DATA            (0x01)
#define MPI2_TOOL_ISTWI_ACTION_WRITE_DATA           (0x02)
#define MPI2_TOOL_ISTWI_ACTION_SEQUENCE             (0x03)
#define MPI2_TOOL_ISTWI_ACTION_RESERVE_BUS          (0x10)
#define MPI2_TOOL_ISTWI_ACTION_RELEASE_BUS          (0x11)
#define MPI2_TOOL_ISTWI_ACTION_RESET                (0x12)

/* use MPI2_SGLFLAGS_ defines from mpi2.h for the SGLFlags field */

/* values for the Flags field */
#define MPI2_TOOL_ISTWI_FLAG_AUTO_RESERVE_RELEASE   (0x80)
#define MPI2_TOOL_ISTWI_FLAG_PAGE_ADDR_MASK         (0x07)

/* Toolbox ISTWI Read Write Tool reply message */
typedef struct _MPI2_TOOLBOX_ISTWI_REPLY {
    U8                      Tool;                       /* 0x00 */
    U8                      Reserved1;                  /* 0x01 */
    U8                      MsgLength;                  /* 0x02 */
    U8                      Function;                   /* 0x03 */
    U16                     Reserved2;                  /* 0x04 */
    U8                      Reserved3;                  /* 0x06 */
    U8                      MsgFlags;                   /* 0x07 */
    U8                      VP_ID;                      /* 0x08 */
    U8                      VF_ID;                      /* 0x09 */
    U16                     Reserved4;                  /* 0x0A */
    U16                     Reserved5;                  /* 0x0C */
    U16                     IOCStatus;                  /* 0x0E */
    U32                     IOCLogInfo;                 /* 0x10 */
    U8                      DevIndex;                   /* 0x14 */
    U8                      Action;                     /* 0x15 */
    U8                      IstwiStatus;                /* 0x16 */
    U8                      Reserved6;                  /* 0x17 */
    U16                     TxDataCount;                /* 0x18 */
    U16                     RxDataCount;                /* 0x1A */
} MPI2_TOOLBOX_ISTWI_REPLY, MPI2_POINTER PTR_MPI2_TOOLBOX_ISTWI_REPLY,
  Mpi2ToolboxIstwiReply_t, MPI2_POINTER pMpi2ToolboxIstwiReply_t;


/****************************************************************************
*  Toolbox Beacon Tool request
****************************************************************************/

typedef struct _MPI2_TOOLBOX_BEACON_REQUEST
{
    U8                      Tool;                       /* 0x00 */
    U8                      Reserved1;                  /* 0x01 */
    U8                      ChainOffset;                /* 0x02 */
    U8                      Function;                   /* 0x03 */
    U16                     Reserved2;                  /* 0x04 */
    U8                      Reserved3;                  /* 0x06 */
    U8                      MsgFlags;                   /* 0x07 */
    U8                      VP_ID;                      /* 0x08 */
    U8                      VF_ID;                      /* 0x09 */
    U16                     Reserved4;                  /* 0x0A */
    U8                      Reserved5;                  /* 0x0C */
    U8                      PhysicalPort;               /* 0x0D */
    U8                      Reserved6;                  /* 0x0E */
    U8                      Flags;                      /* 0x0F */
} MPI2_TOOLBOX_BEACON_REQUEST, MPI2_POINTER PTR_MPI2_TOOLBOX_BEACON_REQUEST,
  Mpi2ToolboxBeaconRequest_t, MPI2_POINTER pMpi2ToolboxBeaconRequest_t;

/* values for the Flags field */
#define MPI2_TOOLBOX_FLAGS_BEACONMODE_OFF       (0x00)
#define MPI2_TOOLBOX_FLAGS_BEACONMODE_ON        (0x01)


/****************************************************************************
*  Toolbox Diagnostic CLI Tool
****************************************************************************/

#define MPI2_TOOLBOX_DIAG_CLI_CMD_LENGTH    (0x5C)

/* MPI v2.0 Toolbox Diagnostic CLI Tool request message */
typedef struct _MPI2_TOOLBOX_DIAGNOSTIC_CLI_REQUEST {
    U8                      Tool;                       /* 0x00 */
    U8                      Reserved1;                  /* 0x01 */
    U8                      ChainOffset;                /* 0x02 */
    U8                      Function;                   /* 0x03 */
    U16                     Reserved2;                  /* 0x04 */
    U8                      Reserved3;                  /* 0x06 */
    U8                      MsgFlags;                   /* 0x07 */
    U8                      VP_ID;                      /* 0x08 */
    U8                      VF_ID;                      /* 0x09 */
    U16                     Reserved4;                  /* 0x0A */
    U8                      SGLFlags;                   /* 0x0C */
    U8                      Reserved5;                  /* 0x0D */
    U16                     Reserved6;                  /* 0x0E */
    U32                     DataLength;                 /* 0x10 */
    U8                      DiagnosticCliCommand
		[MPI2_TOOLBOX_DIAG_CLI_CMD_LENGTH];     /* 0x14 */
	MPI2_MPI_SGE_IO_UNION   SGL;                        /* 0x70 */
} MPI2_TOOLBOX_DIAGNOSTIC_CLI_REQUEST,
  MPI2_POINTER PTR_MPI2_TOOLBOX_DIAGNOSTIC_CLI_REQUEST,
  Mpi2ToolboxDiagnosticCliRequest_t,
  MPI2_POINTER pMpi2ToolboxDiagnosticCliRequest_t;

/* use MPI2_SGLFLAGS_ defines from mpi2.h for the SGLFlags field */


/* Toolbox Diagnostic CLI Tool reply message */
typedef struct _MPI2_TOOLBOX_DIAGNOSTIC_CLI_REPLY {
    U8                      Tool;                       /* 0x00 */
    U8                      Reserved1;                  /* 0x01 */
    U8                      MsgLength;                  /* 0x02 */
    U8                      Function;                   /* 0x03 */
    U16                     Reserved2;                  /* 0x04 */
    U8                      Reserved3;                  /* 0x06 */
    U8                      MsgFlags;                   /* 0x07 */
    U8                      VP_ID;                      /* 0x08 */
    U8                      VF_ID;                      /* 0x09 */
    U16                     Reserved4;                  /* 0x0A */
    U16                     Reserved5;                  /* 0x0C */
    U16                     IOCStatus;                  /* 0x0E */
    U32                     IOCLogInfo;                 /* 0x10 */
    U32                     ReturnedDataLength;         /* 0x14 */
} MPI2_TOOLBOX_DIAGNOSTIC_CLI_REPLY,
  MPI2_POINTER PTR_MPI2_TOOLBOX_DIAG_CLI_REPLY,
  Mpi2ToolboxDiagnosticCliReply_t,
  MPI2_POINTER pMpi2ToolboxDiagnosticCliReply_t;


/****************************************************************************
*  Toolbox Console Text Display Tool
****************************************************************************/

/* Toolbox Console Text Display Tool request message */
typedef struct _MPI2_TOOLBOX_TEXT_DISPLAY_REQUEST {
	U8                      Tool;               /* 0x00 */
	U8                      Reserved1;          /* 0x01 */
	U8                      ChainOffset;        /* 0x02 */
	U8                      Function;           /* 0x03 */
	U16                     Reserved2;          /* 0x04 */
	U8                      Reserved3;          /* 0x06 */
	U8                      MsgFlags;           /* 0x07 */
	U8                      VP_ID;              /* 0x08 */
	U8                      VF_ID;              /* 0x09 */
	U16                     Reserved4;          /* 0x0A */
	U8                      Console;            /* 0x0C */
	U8                      Flags;              /* 0x0D */
	U16                     Reserved6;          /* 0x0E */
	U8                      TextToDisplay[4];   /* 0x10 */
} MPI2_TOOLBOX_TEXT_DISPLAY_REQUEST,
MPI2_POINTER PTR_MPI2_TOOLBOX_TEXT_DISPLAY_REQUEST,
Mpi2ToolboxTextDisplayRequest_t,
MPI2_POINTER pMpi2ToolboxTextDisplayRequest_t;

/* defines for the Console field */
#define MPI2_TOOLBOX_CONSOLE_TYPE_MASK          (0xF0)
#define MPI2_TOOLBOX_CONSOLE_TYPE_DEFAULT       (0x00)
#define MPI2_TOOLBOX_CONSOLE_TYPE_UART          (0x10)
#define MPI2_TOOLBOX_CONSOLE_TYPE_ETHERNET      (0x20)

#define MPI2_TOOLBOX_CONSOLE_NUMBER_MASK        (0x0F)

/* defines for the Flags field */
#define MPI2_TOOLBOX_CONSOLE_FLAG_TIMESTAMP     (0x01)



/*****************************************************************************
*
*       Diagnostic Buffer Messages
*
*****************************************************************************/


/****************************************************************************
*  Diagnostic Buffer Post request
****************************************************************************/

typedef struct _MPI2_DIAG_BUFFER_POST_REQUEST
{
    U8                      ExtendedType;               /* 0x00 */
    U8                      BufferType;                 /* 0x01 */
    U8                      ChainOffset;                /* 0x02 */
    U8                      Function;                   /* 0x03 */
    U16                     Reserved2;                  /* 0x04 */
    U8                      Reserved3;                  /* 0x06 */
    U8                      MsgFlags;                   /* 0x07 */
    U8                      VP_ID;                      /* 0x08 */
    U8                      VF_ID;                      /* 0x09 */
    U16                     Reserved4;                  /* 0x0A */
    U64                     BufferAddress;              /* 0x0C */
    U32                     BufferLength;               /* 0x14 */
    U32                     Reserved5;                  /* 0x18 */
    U32                     Reserved6;                  /* 0x1C */
    U32                     Flags;                      /* 0x20 */
    U32                     ProductSpecific[23];        /* 0x24 */
} MPI2_DIAG_BUFFER_POST_REQUEST, MPI2_POINTER PTR_MPI2_DIAG_BUFFER_POST_REQUEST,
  Mpi2DiagBufferPostRequest_t, MPI2_POINTER pMpi2DiagBufferPostRequest_t;

/* values for the ExtendedType field */
#define MPI2_DIAG_EXTENDED_TYPE_UTILIZATION         (0x02)

/* values for the BufferType field */
#define MPI2_DIAG_BUF_TYPE_TRACE                    (0x00)
#define MPI2_DIAG_BUF_TYPE_SNAPSHOT                 (0x01)
#define MPI2_DIAG_BUF_TYPE_EXTENDED                 (0x02)
/* count of the number of buffer types */
#define MPI2_DIAG_BUF_TYPE_COUNT                    (0x03)

/* values for the Flags field */
#define MPI2_DIAG_BUF_FLAG_RELEASE_ON_FULL          (0x00000002)
#define MPI2_DIAG_BUF_FLAG_IMMEDIATE_RELEASE        (0x00000001)


/****************************************************************************
*  Diagnostic Buffer Post reply
****************************************************************************/

typedef struct _MPI2_DIAG_BUFFER_POST_REPLY
{
    U8                      ExtendedType;               /* 0x00 */
    U8                      BufferType;                 /* 0x01 */
    U8                      MsgLength;                  /* 0x02 */
    U8                      Function;                   /* 0x03 */
    U16                     Reserved2;                  /* 0x04 */
    U8                      Reserved3;                  /* 0x06 */
    U8                      MsgFlags;                   /* 0x07 */
    U8                      VP_ID;                      /* 0x08 */
    U8                      VF_ID;                      /* 0x09 */
    U16                     Reserved4;                  /* 0x0A */
    U16                     Reserved5;                  /* 0x0C */
    U16                     IOCStatus;                  /* 0x0E */
    U32                     IOCLogInfo;                 /* 0x10 */
    U32                     TransferLength;             /* 0x14 */
} MPI2_DIAG_BUFFER_POST_REPLY, MPI2_POINTER PTR_MPI2_DIAG_BUFFER_POST_REPLY,
  Mpi2DiagBufferPostReply_t, MPI2_POINTER pMpi2DiagBufferPostReply_t;


/****************************************************************************
*  Diagnostic Release request
****************************************************************************/

typedef struct _MPI2_DIAG_RELEASE_REQUEST
{
    U8                      Reserved1;                  /* 0x00 */
    U8                      BufferType;                 /* 0x01 */
    U8                      ChainOffset;                /* 0x02 */
    U8                      Function;                   /* 0x03 */
    U16                     Reserved2;                  /* 0x04 */
    U8                      Reserved3;                  /* 0x06 */
    U8                      MsgFlags;                   /* 0x07 */
    U8                      VP_ID;                      /* 0x08 */
    U8                      VF_ID;                      /* 0x09 */
    U16                     Reserved4;                  /* 0x0A */
} MPI2_DIAG_RELEASE_REQUEST, MPI2_POINTER PTR_MPI2_DIAG_RELEASE_REQUEST,
  Mpi2DiagReleaseRequest_t, MPI2_POINTER pMpi2DiagReleaseRequest_t;


/****************************************************************************
*  Diagnostic Buffer Post reply
****************************************************************************/

typedef struct _MPI2_DIAG_RELEASE_REPLY
{
    U8                      Reserved1;                  /* 0x00 */
    U8                      BufferType;                 /* 0x01 */
    U8                      MsgLength;                  /* 0x02 */
    U8                      Function;                   /* 0x03 */
    U16                     Reserved2;                  /* 0x04 */
    U8                      Reserved3;                  /* 0x06 */
    U8                      MsgFlags;                   /* 0x07 */
    U8                      VP_ID;                      /* 0x08 */
    U8                      VF_ID;                      /* 0x09 */
    U16                     Reserved4;                  /* 0x0A */
    U16                     Reserved5;                  /* 0x0C */
    U16                     IOCStatus;                  /* 0x0E */
    U32                     IOCLogInfo;                 /* 0x10 */
} MPI2_DIAG_RELEASE_REPLY, MPI2_POINTER PTR_MPI2_DIAG_RELEASE_REPLY,
  Mpi2DiagReleaseReply_t, MPI2_POINTER pMpi2DiagReleaseReply_t;


#endif

