/*
 *  Copyright (c) 2000-2004 LSI Logic Corporation.
 *
 *
 *           Name:  mpi_fc.h
 *          Title:  MPI Fibre Channel messages and structures
 *  Creation Date:  June 12, 2000
 *
 *    mpi_fc.h Version:  01.05.01
 *
 *  Version History
 *  ---------------
 *
 *  Date      Version   Description
 *  --------  --------  ------------------------------------------------------
 *  05-08-00  00.10.01  Original release for 0.10 spec dated 4/26/2000.
 *  06-06-00  01.00.01  Update version number for 1.0 release.
 *  06-12-00  01.00.02  Added _MSG_FC_ABORT_REPLY structure.
 *  11-02-00  01.01.01  Original release for post 1.0 work
 *  12-04-00  01.01.02  Added messages for Common Transport Send and
 *                      Primitive Send.
 *  01-09-01  01.01.03  Modifed some of the new flags to have an MPI prefix
 *                      and modified the FcPrimitiveSend flags.
 *  01-25-01  01.01.04  Move InitiatorIndex in LinkServiceRsp reply to a larger
 *                      field.
 *                      Added FC_ABORT_TYPE_CT_SEND_REQUEST and
 *                      FC_ABORT_TYPE_EXLINKSEND_REQUEST for FcAbort request.
 *                      Added MPI_FC_PRIM_SEND_FLAGS_STOP_SEND.
 *  02-20-01  01.01.05  Started using MPI_POINTER.
 *  03-27-01  01.01.06  Added Flags field to MSG_LINK_SERVICE_BUFFER_POST_REPLY
 *                      and defined MPI_LS_BUF_POST_REPLY_FLAG_NO_RSP_NEEDED.
 *                      Added MPI_FC_PRIM_SEND_FLAGS_RESET_LINK define.
 *                      Added structure offset comments.
 *  04-09-01  01.01.07  Added RspLength field to MSG_LINK_SERVICE_RSP_REQUEST.
 *  08-08-01  01.02.01  Original release for v1.2 work.
 *  09-28-01  01.02.02  Change name of reserved field in
 *                      MSG_LINK_SERVICE_RSP_REPLY.
 *  05-31-02  01.02.03  Adding AliasIndex to FC Direct Access requests.
 *  01-16-04  01.02.04  Added define for MPI_FC_PRIM_SEND_FLAGS_ML_RESET_LINK.
 *  05-11-04  01.03.01  Original release for MPI v1.3.
 *  08-19-04  01.05.01  Original release for MPI v1.5.
 *  --------------------------------------------------------------------------
 */

#ifndef MPI_FC_H
#define MPI_FC_H


/*****************************************************************************
*
*        F C    D i r e c t    A c c e s s     M e s s a g e s
*
*****************************************************************************/

/****************************************************************************/
/* Link Service Buffer Post messages                                        */
/****************************************************************************/

typedef struct _MSG_LINK_SERVICE_BUFFER_POST_REQUEST
{
    U8                      BufferPostFlags;    /* 00h */
    U8                      BufferCount;        /* 01h */
    U8                      ChainOffset;        /* 02h */
    U8                      Function;           /* 03h */
    U16                     Reserved;           /* 04h */
    U8                      Reserved1;          /* 06h */
    U8                      MsgFlags;           /* 07h */
    U32                     MsgContext;         /* 08h */
    SGE_TRANS_SIMPLE_UNION  SGL;
} MSG_LINK_SERVICE_BUFFER_POST_REQUEST,
 MPI_POINTER PTR_MSG_LINK_SERVICE_BUFFER_POST_REQUEST,
  LinkServiceBufferPostRequest_t, MPI_POINTER pLinkServiceBufferPostRequest_t;

#define LINK_SERVICE_BUFFER_POST_FLAGS_PORT_MASK (0x01)

typedef struct _WWNFORMAT
{
    U32                     PortNameHigh;       /* 00h */
    U32                     PortNameLow;        /* 04h */
    U32                     NodeNameHigh;       /* 08h */
    U32                     NodeNameLow;        /* 0Ch */
} WWNFORMAT,
  WwnFormat_t;

/* Link Service Buffer Post Reply */
typedef struct _MSG_LINK_SERVICE_BUFFER_POST_REPLY
{
    U8                      Flags;              /* 00h */
    U8                      Reserved;           /* 01h */
    U8                      MsgLength;          /* 02h */
    U8                      Function;           /* 03h */
    U16                     Reserved1;          /* 04h */
    U8                      PortNumber;         /* 06h */
    U8                      MsgFlags;           /* 07h */
    U32                     MsgContext;         /* 08h */
    U16                     Reserved2;          /* 0Ch */
    U16                     IOCStatus;          /* 0Eh */
    U32                     IOCLogInfo;         /* 10h */
    U32                     TransferLength;     /* 14h */
    U32                     TransactionContext; /* 18h */
    U32                     Rctl_Did;           /* 1Ch */
    U32                     Csctl_Sid;          /* 20h */
    U32                     Type_Fctl;          /* 24h */
    U16                     SeqCnt;             /* 28h */
    U8                      Dfctl;              /* 2Ah */
    U8                      SeqId;              /* 2Bh */
    U16                     Rxid;               /* 2Ch */
    U16                     Oxid;               /* 2Eh */
    U32                     Parameter;          /* 30h */
    WWNFORMAT               Wwn;                /* 34h */
} MSG_LINK_SERVICE_BUFFER_POST_REPLY, MPI_POINTER PTR_MSG_LINK_SERVICE_BUFFER_POST_REPLY,
  LinkServiceBufferPostReply_t, MPI_POINTER pLinkServiceBufferPostReply_t;

#define MPI_LS_BUF_POST_REPLY_FLAG_NO_RSP_NEEDED    (0x80)

#define MPI_FC_DID_MASK                             (0x00FFFFFF)
#define MPI_FC_DID_SHIFT                            (0)
#define MPI_FC_RCTL_MASK                            (0xFF000000)
#define MPI_FC_RCTL_SHIFT                           (24)
#define MPI_FC_SID_MASK                             (0x00FFFFFF)
#define MPI_FC_SID_SHIFT                            (0)
#define MPI_FC_CSCTL_MASK                           (0xFF000000)
#define MPI_FC_CSCTL_SHIFT                          (24)
#define MPI_FC_FCTL_MASK                            (0x00FFFFFF)
#define MPI_FC_FCTL_SHIFT                           (0)
#define MPI_FC_TYPE_MASK                            (0xFF000000)
#define MPI_FC_TYPE_SHIFT                           (24)

/* obsolete name for the above */
#define FCP_TARGET_DID_MASK                         (0x00FFFFFF)
#define FCP_TARGET_DID_SHIFT                        (0)
#define FCP_TARGET_RCTL_MASK                        (0xFF000000)
#define FCP_TARGET_RCTL_SHIFT                       (24)
#define FCP_TARGET_SID_MASK                         (0x00FFFFFF)
#define FCP_TARGET_SID_SHIFT                        (0)
#define FCP_TARGET_CSCTL_MASK                       (0xFF000000)
#define FCP_TARGET_CSCTL_SHIFT                      (24)
#define FCP_TARGET_FCTL_MASK                        (0x00FFFFFF)
#define FCP_TARGET_FCTL_SHIFT                       (0)
#define FCP_TARGET_TYPE_MASK                        (0xFF000000)
#define FCP_TARGET_TYPE_SHIFT                       (24)


/****************************************************************************/
/* Link Service Response messages                                           */
/****************************************************************************/

typedef struct _MSG_LINK_SERVICE_RSP_REQUEST
{
    U8                      RspFlags;           /* 00h */
    U8                      RspLength;          /* 01h */
    U8                      ChainOffset;        /* 02h */
    U8                      Function;           /* 03h */
    U16                     Reserved1;          /* 04h */
    U8                      Reserved2;          /* 06h */
    U8                      MsgFlags;           /* 07h */
    U32                     MsgContext;         /* 08h */
    U32                     Rctl_Did;           /* 0Ch */
    U32                     Csctl_Sid;          /* 10h */
    U32                     Type_Fctl;          /* 14h */
    U16                     SeqCnt;             /* 18h */
    U8                      Dfctl;              /* 1Ah */
    U8                      SeqId;              /* 1Bh */
    U16                     Rxid;               /* 1Ch */
    U16                     Oxid;               /* 1Eh */
    U32                     Parameter;          /* 20h */
    SGE_SIMPLE_UNION        SGL;                /* 24h */
} MSG_LINK_SERVICE_RSP_REQUEST, MPI_POINTER PTR_MSG_LINK_SERVICE_RSP_REQUEST,
  LinkServiceRspRequest_t, MPI_POINTER pLinkServiceRspRequest_t;

#define LINK_SERVICE_RSP_FLAGS_IMMEDIATE        (0x80)
#define LINK_SERVICE_RSP_FLAGS_PORT_MASK        (0x01)


/* Link Service Response Reply  */
typedef struct _MSG_LINK_SERVICE_RSP_REPLY
{
    U16                     Reserved;           /* 00h */
    U8                      MsgLength;          /* 02h */
    U8                      Function;           /* 03h */
    U16                     Reserved1;          /* 04h */
    U8                      Reserved_0100_InitiatorIndex; /* 06h */ /* obsolete InitiatorIndex */
    U8                      MsgFlags;           /* 07h */
    U32                     MsgContext;         /* 08h */
    U16                     Reserved3;          /* 0Ch */
    U16                     IOCStatus;          /* 0Eh */
    U32                     IOCLogInfo;         /* 10h */
    U32                     InitiatorIndex;     /* 14h */
} MSG_LINK_SERVICE_RSP_REPLY, MPI_POINTER PTR_MSG_LINK_SERVICE_RSP_REPLY,
  LinkServiceRspReply_t, MPI_POINTER pLinkServiceRspReply_t;


/****************************************************************************/
/* Extended Link Service Send messages                                      */
/****************************************************************************/

typedef struct _MSG_EXLINK_SERVICE_SEND_REQUEST
{
    U8                      SendFlags;          /* 00h */
    U8                      AliasIndex;         /* 01h */
    U8                      ChainOffset;        /* 02h */
    U8                      Function;           /* 03h */
    U32                     MsgFlags_Did;       /* 04h */
    U32                     MsgContext;         /* 08h */
    U32                     ElsCommandCode;     /* 0Ch */
    SGE_SIMPLE_UNION        SGL;                /* 10h */
} MSG_EXLINK_SERVICE_SEND_REQUEST, MPI_POINTER PTR_MSG_EXLINK_SERVICE_SEND_REQUEST,
  ExLinkServiceSendRequest_t, MPI_POINTER pExLinkServiceSendRequest_t;

#define EX_LINK_SERVICE_SEND_DID_MASK           (0x00FFFFFF)
#define EX_LINK_SERVICE_SEND_DID_SHIFT          (0)
#define EX_LINK_SERVICE_SEND_MSGFLAGS_MASK      (0xFF000000)
#define EX_LINK_SERVICE_SEND_MSGFLAGS_SHIFT     (24)


/* Extended Link Service Send Reply */
typedef struct _MSG_EXLINK_SERVICE_SEND_REPLY
{
    U8                      Reserved;           /* 00h */
    U8                      AliasIndex;         /* 01h */
    U8                      MsgLength;          /* 02h */
    U8                      Function;           /* 03h */
    U16                     Reserved1;          /* 04h */
    U8                      Reserved2;          /* 06h */
    U8                      MsgFlags;           /* 07h */
    U32                     MsgContext;         /* 08h */
    U16                     Reserved3;          /* 0Ch */
    U16                     IOCStatus;          /* 0Eh */
    U32                     IOCLogInfo;         /* 10h */
    U32                     ResponseLength;     /* 14h */
} MSG_EXLINK_SERVICE_SEND_REPLY, MPI_POINTER PTR_MSG_EXLINK_SERVICE_SEND_REPLY,
  ExLinkServiceSendReply_t, MPI_POINTER pExLinkServiceSendReply_t;

/****************************************************************************/
/* FC Abort messages                                                        */
/****************************************************************************/

typedef struct _MSG_FC_ABORT_REQUEST
{
    U8                      AbortFlags;                 /* 00h */
    U8                      AbortType;                  /* 01h */
    U8                      ChainOffset;                /* 02h */
    U8                      Function;                   /* 03h */
    U16                     Reserved1;                  /* 04h */
    U8                      Reserved2;                  /* 06h */
    U8                      MsgFlags;                   /* 07h */
    U32                     MsgContext;                 /* 08h */
    U32                     TransactionContextToAbort;  /* 0Ch */
} MSG_FC_ABORT_REQUEST, MPI_POINTER PTR_MSG_FC_ABORT_REQUEST,
  FcAbortRequest_t, MPI_POINTER pFcAbortRequest_t;

#define FC_ABORT_FLAG_PORT_MASK                 (0x01)

#define FC_ABORT_TYPE_ALL_FC_BUFFERS            (0x00)
#define FC_ABORT_TYPE_EXACT_FC_BUFFER           (0x01)
#define FC_ABORT_TYPE_CT_SEND_REQUEST           (0x02)
#define FC_ABORT_TYPE_EXLINKSEND_REQUEST        (0x03)

/* FC Abort Reply */
typedef struct _MSG_FC_ABORT_REPLY
{
    U16                     Reserved;           /* 00h */
    U8                      MsgLength;          /* 02h */
    U8                      Function;           /* 03h */
    U16                     Reserved1;          /* 04h */
    U8                      Reserved2;          /* 06h */
    U8                      MsgFlags;           /* 07h */
    U32                     MsgContext;         /* 08h */
    U16                     Reserved3;          /* 0Ch */
    U16                     IOCStatus;          /* 0Eh */
    U32                     IOCLogInfo;         /* 10h */
} MSG_FC_ABORT_REPLY, MPI_POINTER PTR_MSG_FC_ABORT_REPLY,
  FcAbortReply_t, MPI_POINTER pFcAbortReply_t;


/****************************************************************************/
/* FC Common Transport Send messages                                        */
/****************************************************************************/

typedef struct _MSG_FC_COMMON_TRANSPORT_SEND_REQUEST
{
    U8                      SendFlags;          /* 00h */
    U8                      AliasIndex;         /* 01h */
    U8                      ChainOffset;        /* 02h */
    U8                      Function;           /* 03h */
    U32                     MsgFlags_Did;       /* 04h */
    U32                     MsgContext;         /* 08h */
    U16                     CTCommandCode;      /* 0Ch */
    U8                      FsType;             /* 0Eh */
    U8                      Reserved1;          /* 0Fh */
    SGE_SIMPLE_UNION        SGL;                /* 10h */
} MSG_FC_COMMON_TRANSPORT_SEND_REQUEST,
 MPI_POINTER PTR_MSG_FC_COMMON_TRANSPORT_SEND_REQUEST,
  FcCommonTransportSendRequest_t, MPI_POINTER pFcCommonTransportSendRequest_t;

#define MPI_FC_CT_SEND_DID_MASK                 (0x00FFFFFF)
#define MPI_FC_CT_SEND_DID_SHIFT                (0)
#define MPI_FC_CT_SEND_MSGFLAGS_MASK            (0xFF000000)
#define MPI_FC_CT_SEND_MSGFLAGS_SHIFT           (24)


/* FC Common Transport Send Reply */
typedef struct _MSG_FC_COMMON_TRANSPORT_SEND_REPLY
{
    U8                      Reserved;           /* 00h */
    U8                      AliasIndex;         /* 01h */
    U8                      MsgLength;          /* 02h */
    U8                      Function;           /* 03h */
    U16                     Reserved1;          /* 04h */
    U8                      Reserved2;          /* 06h */
    U8                      MsgFlags;           /* 07h */
    U32                     MsgContext;         /* 08h */
    U16                     Reserved3;          /* 0Ch */
    U16                     IOCStatus;          /* 0Eh */
    U32                     IOCLogInfo;         /* 10h */
    U32                     ResponseLength;     /* 14h */
} MSG_FC_COMMON_TRANSPORT_SEND_REPLY, MPI_POINTER PTR_MSG_FC_COMMON_TRANSPORT_SEND_REPLY,
  FcCommonTransportSendReply_t, MPI_POINTER pFcCommonTransportSendReply_t;


/****************************************************************************/
/* FC Primitive Send messages                                               */
/****************************************************************************/

typedef struct _MSG_FC_PRIMITIVE_SEND_REQUEST
{
    U8                      SendFlags;          /* 00h */
    U8                      Reserved;           /* 01h */
    U8                      ChainOffset;        /* 02h */
    U8                      Function;           /* 03h */
    U16                     Reserved1;          /* 04h */
    U8                      Reserved2;          /* 06h */
    U8                      MsgFlags;           /* 07h */
    U32                     MsgContext;         /* 08h */
    U8                      FcPrimitive[4];     /* 0Ch */
} MSG_FC_PRIMITIVE_SEND_REQUEST, MPI_POINTER PTR_MSG_FC_PRIMITIVE_SEND_REQUEST,
  FcPrimitiveSendRequest_t, MPI_POINTER pFcPrimitiveSendRequest_t;

#define MPI_FC_PRIM_SEND_FLAGS_PORT_MASK       (0x01)
#define MPI_FC_PRIM_SEND_FLAGS_ML_RESET_LINK   (0x02)
#define MPI_FC_PRIM_SEND_FLAGS_RESET_LINK      (0x04)
#define MPI_FC_PRIM_SEND_FLAGS_STOP_SEND       (0x08)
#define MPI_FC_PRIM_SEND_FLAGS_SEND_ONCE       (0x10)
#define MPI_FC_PRIM_SEND_FLAGS_SEND_AROUND     (0x20)
#define MPI_FC_PRIM_SEND_FLAGS_UNTIL_FULL      (0x40)
#define MPI_FC_PRIM_SEND_FLAGS_FOREVER         (0x80)

/* FC Primitive Send Reply */
typedef struct _MSG_FC_PRIMITIVE_SEND_REPLY
{
    U8                      SendFlags;          /* 00h */
    U8                      Reserved;           /* 01h */
    U8                      MsgLength;          /* 02h */
    U8                      Function;           /* 03h */
    U16                     Reserved1;          /* 04h */
    U8                      Reserved2;          /* 06h */
    U8                      MsgFlags;           /* 07h */
    U32                     MsgContext;         /* 08h */
    U16                     Reserved3;          /* 0Ch */
    U16                     IOCStatus;          /* 0Eh */
    U32                     IOCLogInfo;         /* 10h */
} MSG_FC_PRIMITIVE_SEND_REPLY, MPI_POINTER PTR_MSG_FC_PRIMITIVE_SEND_REPLY,
  FcPrimitiveSendReply_t, MPI_POINTER pFcPrimitiveSendReply_t;

#endif

