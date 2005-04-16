/*
 *  Copyright (c) 2003 LSI Logic Corporation.
 *
 *
 *           Name:  mpi_inb.h
 *          Title:  MPI Inband structures and definitions
 *  Creation Date:  September 30, 2003
 *
 *    mpi_inb.h Version:  01.03.xx
 *
 *  Version History
 *  ---------------
 *
 *  Date      Version   Description
 *  --------  --------  ------------------------------------------------------
 *  ??-??-??  01.03.01  Original release.
 *  --------------------------------------------------------------------------
 */

#ifndef MPI_INB_H
#define MPI_INB_H

/******************************************************************************
*
*        I n b a n d    M e s s a g e s
*
*******************************************************************************/


/****************************************************************************/
/* Inband Buffer Post Request                                               */
/****************************************************************************/

typedef struct _MSG_INBAND_BUFFER_POST_REQUEST
{
    U8                      Reserved1;          /* 00h */
    U8                      BufferCount;        /* 01h */
    U8                      ChainOffset;        /* 02h */
    U8                      Function;           /* 03h */
    U16                     Reserved2;          /* 04h */
    U8                      Reserved3;          /* 06h */
    U8                      MsgFlags;           /* 07h */
    U32                     MsgContext;         /* 08h */
    U32                     Reserved4;          /* 0Ch */
    SGE_TRANS_SIMPLE_UNION  SGL;                /* 10h */
} MSG_INBAND_BUFFER_POST_REQUEST, MPI_POINTER PTR_MSG_INBAND_BUFFER_POST_REQUEST,
  MpiInbandBufferPostRequest_t , MPI_POINTER pMpiInbandBufferPostRequest_t;


typedef struct _WWN_FC_FORMAT
{
    U64                     NodeName;           /* 00h */
    U64                     PortName;           /* 08h */
} WWN_FC_FORMAT, MPI_POINTER PTR_WWN_FC_FORMAT,
  WwnFcFormat_t, MPI_POINTER pWwnFcFormat_t;

typedef struct _WWN_SAS_FORMAT
{
    U64                     WorldWideID;        /* 00h */
    U32                     Reserved1;          /* 08h */
    U32                     Reserved2;          /* 0Ch */
} WWN_SAS_FORMAT, MPI_POINTER PTR_WWN_SAS_FORMAT,
  WwnSasFormat_t, MPI_POINTER pWwnSasFormat_t;

typedef union _WWN_INBAND_FORMAT
{
    WWN_FC_FORMAT           Fc;
    WWN_SAS_FORMAT          Sas;
} WWN_INBAND_FORMAT, MPI_POINTER PTR_WWN_INBAND_FORMAT,
  WwnInbandFormat, MPI_POINTER pWwnInbandFormat;


/* Inband Buffer Post reply message */

typedef struct _MSG_INBAND_BUFFER_POST_REPLY
{
    U16                     Reserved1;          /* 00h */
    U8                      MsgLength;          /* 02h */
    U8                      Function;           /* 03h */
    U16                     Reserved2;          /* 04h */
    U8                      Reserved3;          /* 06h */
    U8                      MsgFlags;           /* 07h */
    U32                     MsgContext;         /* 08h */
    U16                     Reserved4;          /* 0Ch */
    U16                     IOCStatus;          /* 0Eh */
    U32                     IOCLogInfo;         /* 10h */
    U32                     TransferLength;     /* 14h */
    U32                     TransactionContext; /* 18h */
    WWN_INBAND_FORMAT       Wwn;                /* 1Ch */
    U32                     IOCIdentifier[4];   /* 2Ch */
} MSG_INBAND_BUFFER_POST_REPLY, MPI_POINTER PTR_MSG_INBAND_BUFFER_POST_REPLY,
  MpiInbandBufferPostReply_t, MPI_POINTER pMpiInbandBufferPostReply_t;


/****************************************************************************/
/* Inband Send Request                                                      */
/****************************************************************************/

typedef struct _MSG_INBAND_SEND_REQUEST
{
    U16                     Reserved1;          /* 00h */
    U8                      ChainOffset;        /* 02h */
    U8                      Function;           /* 03h */
    U16                     Reserved2;          /* 04h */
    U8                      Reserved3;          /* 06h */
    U8                      MsgFlags;           /* 07h */
    U32                     MsgContext;         /* 08h */
    U32                     Reserved4;          /* 0Ch */
    WWN_INBAND_FORMAT       Wwn;                /* 10h */
    U32                     Reserved5;          /* 20h */
    SGE_IO_UNION            SGL;                /* 24h */
} MSG_INBAND_SEND_REQUEST, MPI_POINTER PTR_MSG_INBAND_SEND_REQUEST,
  MpiInbandSendRequest_t , MPI_POINTER pMpiInbandSendRequest_t;


/* Inband Send reply message */

typedef struct _MSG_INBAND_SEND_REPLY
{
    U16                     Reserved1;          /* 00h */
    U8                      MsgLength;          /* 02h */
    U8                      Function;           /* 03h */
    U16                     Reserved2;          /* 04h */
    U8                      Reserved3;          /* 06h */
    U8                      MsgFlags;           /* 07h */
    U32                     MsgContext;         /* 08h */
    U16                     Reserved4;          /* 0Ch */
    U16                     IOCStatus;          /* 0Eh */
    U32                     IOCLogInfo;         /* 10h */
    U32                     ResponseLength;     /* 14h */
} MSG_INBAND_SEND_REPLY, MPI_POINTER PTR_MSG_INBAND_SEND_REPLY,
  MpiInbandSendReply_t, MPI_POINTER pMpiInbandSendReply_t;


/****************************************************************************/
/* Inband Response Request                                                  */
/****************************************************************************/

typedef struct _MSG_INBAND_RSP_REQUEST
{
    U16                     Reserved1;          /* 00h */
    U8                      ChainOffset;        /* 02h */
    U8                      Function;           /* 03h */
    U16                     Reserved2;          /* 04h */
    U8                      Reserved3;          /* 06h */
    U8                      MsgFlags;           /* 07h */
    U32                     MsgContext;         /* 08h */
    U32                     Reserved4;          /* 0Ch */
    WWN_INBAND_FORMAT       Wwn;                /* 10h */
    U32                     IOCIdentifier[4];   /* 20h */
    U32                     ResponseLength;     /* 30h */
    SGE_IO_UNION            SGL;                /* 34h */
} MSG_INBAND_RSP_REQUEST, MPI_POINTER PTR_MSG_INBAND_RSP_REQUEST,
  MpiInbandRspRequest_t , MPI_POINTER pMpiInbandRspRequest_t;


/* Inband Response reply message */

typedef struct _MSG_INBAND_RSP_REPLY
{
    U16                     Reserved1;          /* 00h */
    U8                      MsgLength;          /* 02h */
    U8                      Function;           /* 03h */
    U16                     Reserved2;          /* 04h */
    U8                      Reserved3;          /* 06h */
    U8                      MsgFlags;           /* 07h */
    U32                     MsgContext;         /* 08h */
    U16                     Reserved4;          /* 0Ch */
    U16                     IOCStatus;          /* 0Eh */
    U32                     IOCLogInfo;         /* 10h */
} MSG_INBAND_RSP_REPLY, MPI_POINTER PTR_MSG_INBAND_RSP_REPLY,
  MpiInbandRspReply_t, MPI_POINTER pMpiInbandRspReply_t;


/****************************************************************************/
/* Inband Abort Request                                                     */
/****************************************************************************/

typedef struct _MSG_INBAND_ABORT_REQUEST
{
    U8                      Reserved1;          /* 00h */
    U8                      AbortType;          /* 01h */
    U8                      ChainOffset;        /* 02h */
    U8                      Function;           /* 03h */
    U16                     Reserved2;          /* 04h */
    U8                      Reserved3;          /* 06h */
    U8                      MsgFlags;           /* 07h */
    U32                     MsgContext;         /* 08h */
    U32                     Reserved4;          /* 0Ch */
    U32                     ContextToAbort;     /* 10h */
} MSG_INBAND_ABORT_REQUEST, MPI_POINTER PTR_MSG_INBAND_ABORT_REQUEST,
  MpiInbandAbortRequest_t , MPI_POINTER pMpiInbandAbortRequest_t;

#define MPI_INBAND_ABORT_TYPE_ALL_BUFFERS       (0x00)
#define MPI_INBAND_ABORT_TYPE_EXACT_BUFFER      (0x01)
#define MPI_INBAND_ABORT_TYPE_SEND_REQUEST      (0x02)
#define MPI_INBAND_ABORT_TYPE_RESPONSE_REQUEST  (0x03)


/* Inband Abort reply message */

typedef struct _MSG_INBAND_ABORT_REPLY
{
    U8                      Reserved1;          /* 00h */
    U8                      AbortType;          /* 01h */
    U8                      MsgLength;          /* 02h */
    U8                      Function;           /* 03h */
    U16                     Reserved2;          /* 04h */
    U8                      Reserved3;          /* 06h */
    U8                      MsgFlags;           /* 07h */
    U32                     MsgContext;         /* 08h */
    U16                     Reserved4;          /* 0Ch */
    U16                     IOCStatus;          /* 0Eh */
    U32                     IOCLogInfo;         /* 10h */
} MSG_INBAND_ABORT_REPLY, MPI_POINTER PTR_MSG_INBAND_ABORT_REPLY,
  MpiInbandAbortReply_t, MPI_POINTER pMpiInbandAbortReply_t;


#endif

