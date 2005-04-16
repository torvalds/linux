/*
 *  Copyright (c) 2000-2003 LSI Logic Corporation.
 *
 *
 *           Name:  mpi_ioc.h
 *          Title:  MPI IOC, Port, Event, FW Download, and FW Upload messages
 *  Creation Date:  August 11, 2000
 *
 *    mpi_ioc.h Version:  01.05.xx
 *
 *  Version History
 *  ---------------
 *
 *  Date      Version   Description
 *  --------  --------  ------------------------------------------------------
 *  05-08-00  00.10.01  Original release for 0.10 spec dated 4/26/2000.
 *  05-24-00  00.10.02  Added _MSG_IOC_INIT_REPLY structure.
 *  06-06-00  01.00.01  Added CurReplyFrameSize field to _MSG_IOC_FACTS_REPLY.
 *  06-12-00  01.00.02  Added _MSG_PORT_ENABLE_REPLY structure.
 *                      Added _MSG_EVENT_ACK_REPLY structure.
 *                      Added _MSG_FW_DOWNLOAD_REPLY structure.
 *                      Added _MSG_TOOLBOX_REPLY structure.
 *  06-30-00  01.00.03  Added MaxLanBuckets to _PORT_FACT_REPLY structure.
 *  07-27-00  01.00.04  Added _EVENT_DATA structure definitions for _SCSI,
 *                      _LINK_STATUS, _LOOP_STATE and _LOGOUT.
 *  08-11-00  01.00.05  Switched positions of MsgLength and Function fields in
 *                      _MSG_EVENT_ACK_REPLY structure to match specification.
 *  11-02-00  01.01.01  Original release for post 1.0 work.
 *                      Added a value for Manufacturer to WhoInit.
 *  12-04-00  01.01.02  Modified IOCFacts reply, added FWUpload messages, and
 *                      removed toolbox message.
 *  01-09-01  01.01.03  Added event enabled and disabled defines.
 *                      Added structures for FwHeader and DataHeader.
 *                      Added ImageType to FwUpload reply.
 *  02-20-01  01.01.04  Started using MPI_POINTER.
 *  02-27-01  01.01.05  Added event for RAID status change and its event data.
 *                      Added IocNumber field to MSG_IOC_FACTS_REPLY.
 *  03-27-01  01.01.06  Added defines for ProductId field of MPI_FW_HEADER.
 *                      Added structure offset comments.
 *  04-09-01  01.01.07  Added structure EVENT_DATA_EVENT_CHANGE.
 *  08-08-01  01.02.01  Original release for v1.2 work.
 *                      New format for FWVersion and ProductId in
 *                      MSG_IOC_FACTS_REPLY and MPI_FW_HEADER.
 *  08-31-01  01.02.02  Addded event MPI_EVENT_SCSI_DEVICE_STATUS_CHANGE and
 *                      related structure and defines.
 *                      Added event MPI_EVENT_ON_BUS_TIMER_EXPIRED.
 *                      Added MPI_IOCINIT_FLAGS_DISCARD_FW_IMAGE.
 *                      Replaced a reserved field in MSG_IOC_FACTS_REPLY with
 *                      IOCExceptions and changed DataImageSize to reserved.
 *                      Added MPI_FW_DOWNLOAD_ITYPE_NVSTORE_DATA and
 *                      MPI_FW_UPLOAD_ITYPE_NVDATA.
 *  09-28-01  01.02.03  Modified Event Data for Integrated RAID.
 *  11-01-01  01.02.04  Added defines for MPI_EXT_IMAGE_HEADER ImageType field.
 *  03-14-02  01.02.05  Added HeaderVersion field to MSG_IOC_FACTS_REPLY.
 *  05-31-02  01.02.06  Added define for
 *                      MPI_IOCFACTS_EXCEPT_RAID_CONFIG_INVALID.
 *                      Added AliasIndex to EVENT_DATA_LOGOUT structure.
 *  04-01-03  01.02.07  Added defines for MPI_FW_HEADER_SIGNATURE_.
 *  06-26-03  01.02.08  Added new values to the product family defines.
 *  --------------------------------------------------------------------------
 */

#ifndef MPI_IOC_H
#define MPI_IOC_H


/*****************************************************************************
*
*               I O C    M e s s a g e s
*
*****************************************************************************/

/****************************************************************************/
/*  IOCInit message                                                         */
/****************************************************************************/

typedef struct _MSG_IOC_INIT
{
    U8                      WhoInit;                    /* 00h */
    U8                      Reserved;                   /* 01h */
    U8                      ChainOffset;                /* 02h */
    U8                      Function;                   /* 03h */
    U8                      Flags;                      /* 04h */
    U8                      MaxDevices;                 /* 05h */
    U8                      MaxBuses;                   /* 06h */
    U8                      MsgFlags;                   /* 07h */
    U32                     MsgContext;                 /* 08h */
    U16                     ReplyFrameSize;             /* 0Ch */
    U8                      Reserved1[2];               /* 0Eh */
    U32                     HostMfaHighAddr;            /* 10h */
    U32                     SenseBufferHighAddr;        /* 14h */
    U32                     ReplyFifoHostSignalingAddr; /* 18h */
} MSG_IOC_INIT, MPI_POINTER PTR_MSG_IOC_INIT,
  IOCInit_t, MPI_POINTER pIOCInit_t;

/* WhoInit values */
#define MPI_WHOINIT_NO_ONE                          (0x00)
#define MPI_WHOINIT_SYSTEM_BIOS                     (0x01)
#define MPI_WHOINIT_ROM_BIOS                        (0x02)
#define MPI_WHOINIT_PCI_PEER                        (0x03)
#define MPI_WHOINIT_HOST_DRIVER                     (0x04)
#define MPI_WHOINIT_MANUFACTURER                    (0x05)

/* Flags values */
#define MPI_IOCINIT_FLAGS_DISCARD_FW_IMAGE          (0x01)
#define MPI_IOCINIT_FLAGS_REPLY_FIFO_HOST_SIGNAL    (0x02)

typedef struct _MSG_IOC_INIT_REPLY
{
    U8                      WhoInit;                    /* 00h */
    U8                      Reserved;                   /* 01h */
    U8                      MsgLength;                  /* 02h */
    U8                      Function;                   /* 03h */
    U8                      Flags;                      /* 04h */
    U8                      MaxDevices;                 /* 05h */
    U8                      MaxBuses;                   /* 06h */
    U8                      MsgFlags;                   /* 07h */
    U32                     MsgContext;                 /* 08h */
    U16                     Reserved2;                  /* 0Ch */
    U16                     IOCStatus;                  /* 0Eh */
    U32                     IOCLogInfo;                 /* 10h */
} MSG_IOC_INIT_REPLY, MPI_POINTER PTR_MSG_IOC_INIT_REPLY,
  IOCInitReply_t, MPI_POINTER pIOCInitReply_t;



/****************************************************************************/
/*  IOC Facts message                                                       */
/****************************************************************************/

typedef struct _MSG_IOC_FACTS
{
    U8                      Reserved[2];                /* 00h */
    U8                      ChainOffset;                /* 01h */
    U8                      Function;                   /* 02h */
    U8                      Reserved1[3];               /* 03h */
    U8                      MsgFlags;                   /* 04h */
    U32                     MsgContext;                 /* 08h */
} MSG_IOC_FACTS, MPI_POINTER PTR_IOC_FACTS,
  IOCFacts_t, MPI_POINTER pIOCFacts_t;

typedef struct _MPI_FW_VERSION_STRUCT
{
    U8                      Dev;                        /* 00h */
    U8                      Unit;                       /* 01h */
    U8                      Minor;                      /* 02h */
    U8                      Major;                      /* 03h */
} MPI_FW_VERSION_STRUCT;

typedef union _MPI_FW_VERSION
{
    MPI_FW_VERSION_STRUCT   Struct;
    U32                     Word;
} MPI_FW_VERSION;

/* IOC Facts Reply */
typedef struct _MSG_IOC_FACTS_REPLY
{
    U16                     MsgVersion;                 /* 00h */
    U8                      MsgLength;                  /* 02h */
    U8                      Function;                   /* 03h */
    U16                     HeaderVersion;              /* 04h */
    U8                      IOCNumber;                  /* 06h */
    U8                      MsgFlags;                   /* 07h */
    U32                     MsgContext;                 /* 08h */
    U16                     IOCExceptions;              /* 0Ch */
    U16                     IOCStatus;                  /* 0Eh */
    U32                     IOCLogInfo;                 /* 10h */
    U8                      MaxChainDepth;              /* 14h */
    U8                      WhoInit;                    /* 15h */
    U8                      BlockSize;                  /* 16h */
    U8                      Flags;                      /* 17h */
    U16                     ReplyQueueDepth;            /* 18h */
    U16                     RequestFrameSize;           /* 1Ah */
    U16                     Reserved_0101_FWVersion;    /* 1Ch */ /* obsolete 16-bit FWVersion */
    U16                     ProductID;                  /* 1Eh */
    U32                     CurrentHostMfaHighAddr;     /* 20h */
    U16                     GlobalCredits;              /* 24h */
    U8                      NumberOfPorts;              /* 26h */
    U8                      EventState;                 /* 27h */
    U32                     CurrentSenseBufferHighAddr; /* 28h */
    U16                     CurReplyFrameSize;          /* 2Ch */
    U8                      MaxDevices;                 /* 2Eh */
    U8                      MaxBuses;                   /* 2Fh */
    U32                     FWImageSize;                /* 30h */
    U32                     IOCCapabilities;            /* 34h */
    MPI_FW_VERSION          FWVersion;                  /* 38h */
    U16                     HighPriorityQueueDepth;     /* 3Ch */
    U16                     Reserved2;                  /* 3Eh */
} MSG_IOC_FACTS_REPLY, MPI_POINTER PTR_MSG_IOC_FACTS_REPLY,
  IOCFactsReply_t, MPI_POINTER pIOCFactsReply_t;

#define MPI_IOCFACTS_MSGVERSION_MAJOR_MASK          (0xFF00)
#define MPI_IOCFACTS_MSGVERSION_MINOR_MASK          (0x00FF)

#define MPI_IOCFACTS_HEADERVERSION_UNIT_MASK        (0xFF00)
#define MPI_IOCFACTS_HEADERVERSION_DEV_MASK         (0x00FF)

#define MPI_IOCFACTS_EXCEPT_CONFIG_CHECKSUM_FAIL    (0x0001)
#define MPI_IOCFACTS_EXCEPT_RAID_CONFIG_INVALID     (0x0002)
#define MPI_IOCFACTS_EXCEPT_FW_CHECKSUM_FAIL        (0x0004)
#define MPI_IOCFACTS_EXCEPT_PERSISTENT_TABLE_FULL   (0x0008)

#define MPI_IOCFACTS_FLAGS_FW_DOWNLOAD_BOOT         (0x01)

#define MPI_IOCFACTS_EVENTSTATE_DISABLED            (0x00)
#define MPI_IOCFACTS_EVENTSTATE_ENABLED             (0x01)

#define MPI_IOCFACTS_CAPABILITY_HIGH_PRI_Q          (0x00000001)
#define MPI_IOCFACTS_CAPABILITY_REPLY_HOST_SIGNAL   (0x00000002)
#define MPI_IOCFACTS_CAPABILITY_QUEUE_FULL_HANDLING (0x00000004)
#define MPI_IOCFACTS_CAPABILITY_DIAG_TRACE_BUFFER   (0x00000008)
#define MPI_IOCFACTS_CAPABILITY_SNAPSHOT_BUFFER     (0x00000010)
#define MPI_IOCFACTS_CAPABILITY_EXTENDED_BUFFER     (0x00000020)
#define MPI_IOCFACTS_CAPABILITY_EEDP                (0x00000040)



/*****************************************************************************
*
*               P o r t    M e s s a g e s
*
*****************************************************************************/

/****************************************************************************/
/*  Port Facts message and Reply                                            */
/****************************************************************************/

typedef struct _MSG_PORT_FACTS
{
     U8                     Reserved[2];                /* 00h */
     U8                     ChainOffset;                /* 02h */
     U8                     Function;                   /* 03h */
     U8                     Reserved1[2];               /* 04h */
     U8                     PortNumber;                 /* 06h */
     U8                     MsgFlags;                   /* 07h */
     U32                    MsgContext;                 /* 08h */
} MSG_PORT_FACTS, MPI_POINTER PTR_MSG_PORT_FACTS,
  PortFacts_t, MPI_POINTER pPortFacts_t;

typedef struct _MSG_PORT_FACTS_REPLY
{
     U16                    Reserved;                   /* 00h */
     U8                     MsgLength;                  /* 02h */
     U8                     Function;                   /* 03h */
     U16                    Reserved1;                  /* 04h */
     U8                     PortNumber;                 /* 06h */
     U8                     MsgFlags;                   /* 07h */
     U32                    MsgContext;                 /* 08h */
     U16                    Reserved2;                  /* 0Ch */
     U16                    IOCStatus;                  /* 0Eh */
     U32                    IOCLogInfo;                 /* 10h */
     U8                     Reserved3;                  /* 14h */
     U8                     PortType;                   /* 15h */
     U16                    MaxDevices;                 /* 16h */
     U16                    PortSCSIID;                 /* 18h */
     U16                    ProtocolFlags;              /* 1Ah */
     U16                    MaxPostedCmdBuffers;        /* 1Ch */
     U16                    MaxPersistentIDs;           /* 1Eh */
     U16                    MaxLanBuckets;              /* 20h */
     U16                    Reserved4;                  /* 22h */
     U32                    Reserved5;                  /* 24h */
} MSG_PORT_FACTS_REPLY, MPI_POINTER PTR_MSG_PORT_FACTS_REPLY,
  PortFactsReply_t, MPI_POINTER pPortFactsReply_t;


/* PortTypes values */

#define MPI_PORTFACTS_PORTTYPE_INACTIVE         (0x00)
#define MPI_PORTFACTS_PORTTYPE_SCSI             (0x01)
#define MPI_PORTFACTS_PORTTYPE_FC               (0x10)
#define MPI_PORTFACTS_PORTTYPE_ISCSI            (0x20)
#define MPI_PORTFACTS_PORTTYPE_SAS              (0x30)

/* ProtocolFlags values */

#define MPI_PORTFACTS_PROTOCOL_LOGBUSADDR       (0x01)
#define MPI_PORTFACTS_PROTOCOL_LAN              (0x02)
#define MPI_PORTFACTS_PROTOCOL_TARGET           (0x04)
#define MPI_PORTFACTS_PROTOCOL_INITIATOR        (0x08)


/****************************************************************************/
/*  Port Enable Message                                                     */
/****************************************************************************/

typedef struct _MSG_PORT_ENABLE
{
    U8                      Reserved[2];                /* 00h */
    U8                      ChainOffset;                /* 02h */
    U8                      Function;                   /* 03h */
    U8                      Reserved1[2];               /* 04h */
    U8                      PortNumber;                 /* 06h */
    U8                      MsgFlags;                   /* 07h */
    U32                     MsgContext;                 /* 08h */
} MSG_PORT_ENABLE, MPI_POINTER PTR_MSG_PORT_ENABLE,
  PortEnable_t, MPI_POINTER pPortEnable_t;

typedef struct _MSG_PORT_ENABLE_REPLY
{
    U8                      Reserved[2];                /* 00h */
    U8                      MsgLength;                  /* 02h */
    U8                      Function;                   /* 03h */
    U8                      Reserved1[2];               /* 04h */
    U8                      PortNumber;                 /* 05h */
    U8                      MsgFlags;                   /* 07h */
    U32                     MsgContext;                 /* 08h */
    U16                     Reserved2;                  /* 0Ch */
    U16                     IOCStatus;                  /* 0Eh */
    U32                     IOCLogInfo;                 /* 10h */
} MSG_PORT_ENABLE_REPLY, MPI_POINTER PTR_MSG_PORT_ENABLE_REPLY,
  PortEnableReply_t, MPI_POINTER pPortEnableReply_t;


/*****************************************************************************
*
*               E v e n t    M e s s a g e s
*
*****************************************************************************/

/****************************************************************************/
/*  Event Notification messages                                             */
/****************************************************************************/

typedef struct _MSG_EVENT_NOTIFY
{
    U8                      Switch;                     /* 00h */
    U8                      Reserved;                   /* 01h */
    U8                      ChainOffset;                /* 02h */
    U8                      Function;                   /* 03h */
    U8                      Reserved1[3];               /* 04h */
    U8                      MsgFlags;                   /* 07h */
    U32                     MsgContext;                 /* 08h */
} MSG_EVENT_NOTIFY, MPI_POINTER PTR_MSG_EVENT_NOTIFY,
  EventNotification_t, MPI_POINTER pEventNotification_t;

/* Event Notification Reply */

typedef struct _MSG_EVENT_NOTIFY_REPLY
{
     U16                    EventDataLength;            /* 00h */
     U8                     MsgLength;                  /* 02h */
     U8                     Function;                   /* 03h */
     U8                     Reserved1[2];               /* 04h */
     U8                     AckRequired;                /* 06h */
     U8                     MsgFlags;                   /* 07h */
     U32                    MsgContext;                 /* 08h */
     U8                     Reserved2[2];               /* 0Ch */
     U16                    IOCStatus;                  /* 0Eh */
     U32                    IOCLogInfo;                 /* 10h */
     U32                    Event;                      /* 14h */
     U32                    EventContext;               /* 18h */
     U32                    Data[1];                    /* 1Ch */
} MSG_EVENT_NOTIFY_REPLY, MPI_POINTER PTR_MSG_EVENT_NOTIFY_REPLY,
  EventNotificationReply_t, MPI_POINTER pEventNotificationReply_t;

/* Event Acknowledge */

typedef struct _MSG_EVENT_ACK
{
    U8                      Reserved[2];                /* 00h */
    U8                      ChainOffset;                /* 02h */
    U8                      Function;                   /* 03h */
    U8                      Reserved1[3];               /* 04h */
    U8                      MsgFlags;                   /* 07h */
    U32                     MsgContext;                 /* 08h */
    U32                     Event;                      /* 0Ch */
    U32                     EventContext;               /* 10h */
} MSG_EVENT_ACK, MPI_POINTER PTR_MSG_EVENT_ACK,
  EventAck_t, MPI_POINTER pEventAck_t;

typedef struct _MSG_EVENT_ACK_REPLY
{
    U8                      Reserved[2];                /* 00h */
    U8                      MsgLength;                  /* 02h */
    U8                      Function;                   /* 03h */
    U8                      Reserved1[3];               /* 04h */
    U8                      MsgFlags;                   /* 07h */
    U32                     MsgContext;                 /* 08h */
    U16                     Reserved2;                  /* 0Ch */
    U16                     IOCStatus;                  /* 0Eh */
    U32                     IOCLogInfo;                 /* 10h */
} MSG_EVENT_ACK_REPLY, MPI_POINTER PTR_MSG_EVENT_ACK_REPLY,
  EventAckReply_t, MPI_POINTER pEventAckReply_t;

/* Switch */

#define MPI_EVENT_NOTIFICATION_SWITCH_OFF   (0x00)
#define MPI_EVENT_NOTIFICATION_SWITCH_ON    (0x01)

/* Event */

#define MPI_EVENT_NONE                      (0x00000000)
#define MPI_EVENT_LOG_DATA                  (0x00000001)
#define MPI_EVENT_STATE_CHANGE              (0x00000002)
#define MPI_EVENT_UNIT_ATTENTION            (0x00000003)
#define MPI_EVENT_IOC_BUS_RESET             (0x00000004)
#define MPI_EVENT_EXT_BUS_RESET             (0x00000005)
#define MPI_EVENT_RESCAN                    (0x00000006)
#define MPI_EVENT_LINK_STATUS_CHANGE        (0x00000007)
#define MPI_EVENT_LOOP_STATE_CHANGE         (0x00000008)
#define MPI_EVENT_LOGOUT                    (0x00000009)
#define MPI_EVENT_EVENT_CHANGE              (0x0000000A)
#define MPI_EVENT_INTEGRATED_RAID           (0x0000000B)
#define MPI_EVENT_SCSI_DEVICE_STATUS_CHANGE (0x0000000C)
#define MPI_EVENT_ON_BUS_TIMER_EXPIRED      (0x0000000D)
#define MPI_EVENT_QUEUE_FULL                (0x0000000E)
#define MPI_EVENT_SAS_DEVICE_STATUS_CHANGE  (0x0000000F)
#define MPI_EVENT_SAS_SES                   (0x00000010)
#define MPI_EVENT_PERSISTENT_TABLE_FULL     (0x00000011)

/* AckRequired field values */

#define MPI_EVENT_NOTIFICATION_ACK_NOT_REQUIRED (0x00)
#define MPI_EVENT_NOTIFICATION_ACK_REQUIRED     (0x01)

/* EventChange Event data */

typedef struct _EVENT_DATA_EVENT_CHANGE
{
    U8                      EventState;                 /* 00h */
    U8                      Reserved;                   /* 01h */
    U16                     Reserved1;                  /* 02h */
} EVENT_DATA_EVENT_CHANGE, MPI_POINTER PTR_EVENT_DATA_EVENT_CHANGE,
  EventDataEventChange_t, MPI_POINTER pEventDataEventChange_t;

/* SCSI Event data for Port, Bus and Device forms */

typedef struct _EVENT_DATA_SCSI
{
    U8                      TargetID;                   /* 00h */
    U8                      BusPort;                    /* 01h */
    U16                     Reserved;                   /* 02h */
} EVENT_DATA_SCSI, MPI_POINTER PTR_EVENT_DATA_SCSI,
  EventDataScsi_t, MPI_POINTER pEventDataScsi_t;

/* SCSI Device Status Change Event data */

typedef struct _EVENT_DATA_SCSI_DEVICE_STATUS_CHANGE
{
    U8                      TargetID;                   /* 00h */
    U8                      Bus;                        /* 01h */
    U8                      ReasonCode;                 /* 02h */
    U8                      LUN;                        /* 03h */
    U8                      ASC;                        /* 04h */
    U8                      ASCQ;                       /* 05h */
    U16                     Reserved;                   /* 06h */
} EVENT_DATA_SCSI_DEVICE_STATUS_CHANGE,
  MPI_POINTER PTR_EVENT_DATA_SCSI_DEVICE_STATUS_CHANGE,
  MpiEventDataScsiDeviceStatusChange_t,
  MPI_POINTER pMpiEventDataScsiDeviceStatusChange_t;

/* MPI SCSI Device Status Change Event data ReasonCode values */
#define MPI_EVENT_SCSI_DEV_STAT_RC_ADDED                (0x03)
#define MPI_EVENT_SCSI_DEV_STAT_RC_NOT_RESPONDING       (0x04)
#define MPI_EVENT_SCSI_DEV_STAT_RC_SMART_DATA           (0x05)

/* SAS Device Status Change Event data */

typedef struct _EVENT_DATA_SAS_DEVICE_STATUS_CHANGE
{
    U8                      TargetID;                   /* 00h */
    U8                      Bus;                        /* 01h */
    U8                      ReasonCode;                 /* 02h */
    U8                      Reserved;                   /* 03h */
    U8                      ASC;                        /* 04h */
    U8                      ASCQ;                       /* 05h */
    U16                     DevHandle;                  /* 06h */
    U32                     DeviceInfo;                 /* 08h */
} EVENT_DATA_SAS_DEVICE_STATUS_CHANGE,
  MPI_POINTER PTR_EVENT_DATA_SAS_DEVICE_STATUS_CHANGE,
  MpiEventDataSasDeviceStatusChange_t,
  MPI_POINTER pMpiEventDataSasDeviceStatusChange_t;

/* MPI SAS Device Status Change Event data ReasonCode values */
#define MPI_EVENT_SAS_DEV_STAT_RC_ADDED                 (0x03)
#define MPI_EVENT_SAS_DEV_STAT_RC_NOT_RESPONDING        (0x04)
#define MPI_EVENT_SAS_DEV_STAT_RC_SMART_DATA            (0x05)
#define MPI_EVENT_SAS_DEV_STAT_RC_NO_PERSIST_ADDED      (0x06)

/* SCSI Event data for Queue Full event */

typedef struct _EVENT_DATA_QUEUE_FULL
{
    U8                      TargetID;                   /* 00h */
    U8                      Bus;                        /* 01h */
    U16                     CurrentDepth;               /* 02h */
} EVENT_DATA_QUEUE_FULL, MPI_POINTER PTR_EVENT_DATA_QUEUE_FULL,
  EventDataQueueFull_t, MPI_POINTER pEventDataQueueFull_t;

/* MPI Link Status Change Event data */

typedef struct _EVENT_DATA_LINK_STATUS
{
    U8                      State;                      /* 00h */
    U8                      Reserved;                   /* 01h */
    U16                     Reserved1;                  /* 02h */
    U8                      Reserved2;                  /* 04h */
    U8                      Port;                       /* 05h */
    U16                     Reserved3;                  /* 06h */
} EVENT_DATA_LINK_STATUS, MPI_POINTER PTR_EVENT_DATA_LINK_STATUS,
  EventDataLinkStatus_t, MPI_POINTER pEventDataLinkStatus_t;

#define MPI_EVENT_LINK_STATUS_FAILURE       (0x00000000)
#define MPI_EVENT_LINK_STATUS_ACTIVE        (0x00000001)

/* MPI Loop State Change Event data */

typedef struct _EVENT_DATA_LOOP_STATE
{
    U8                      Character4;                 /* 00h */
    U8                      Character3;                 /* 01h */
    U8                      Type;                       /* 02h */
    U8                      Reserved;                   /* 03h */
    U8                      Reserved1;                  /* 04h */
    U8                      Port;                       /* 05h */
    U16                     Reserved2;                  /* 06h */
} EVENT_DATA_LOOP_STATE, MPI_POINTER PTR_EVENT_DATA_LOOP_STATE,
  EventDataLoopState_t, MPI_POINTER pEventDataLoopState_t;

#define MPI_EVENT_LOOP_STATE_CHANGE_LIP     (0x0001)
#define MPI_EVENT_LOOP_STATE_CHANGE_LPE     (0x0002)
#define MPI_EVENT_LOOP_STATE_CHANGE_LPB     (0x0003)

/* MPI LOGOUT Event data */

typedef struct _EVENT_DATA_LOGOUT
{
    U32                     NPortID;                    /* 00h */
    U8                      AliasIndex;                 /* 04h */
    U8                      Port;                       /* 05h */
    U16                     Reserved1;                  /* 06h */
} EVENT_DATA_LOGOUT, MPI_POINTER PTR_EVENT_DATA_LOGOUT,
  EventDataLogout_t, MPI_POINTER pEventDataLogout_t;

#define MPI_EVENT_LOGOUT_ALL_ALIASES        (0xFF)


/* MPI Integrated RAID Event data */

typedef struct _EVENT_DATA_RAID
{
    U8                      VolumeID;                   /* 00h */
    U8                      VolumeBus;                  /* 01h */
    U8                      ReasonCode;                 /* 02h */
    U8                      PhysDiskNum;                /* 03h */
    U8                      ASC;                        /* 04h */
    U8                      ASCQ;                       /* 05h */
    U16                     Reserved;                   /* 06h */
    U32                     SettingsStatus;             /* 08h */
} EVENT_DATA_RAID, MPI_POINTER PTR_EVENT_DATA_RAID,
  MpiEventDataRaid_t, MPI_POINTER pMpiEventDataRaid_t;

/* MPI Integrated RAID Event data ReasonCode values */
#define MPI_EVENT_RAID_RC_VOLUME_CREATED                (0x00)
#define MPI_EVENT_RAID_RC_VOLUME_DELETED                (0x01)
#define MPI_EVENT_RAID_RC_VOLUME_SETTINGS_CHANGED       (0x02)
#define MPI_EVENT_RAID_RC_VOLUME_STATUS_CHANGED         (0x03)
#define MPI_EVENT_RAID_RC_VOLUME_PHYSDISK_CHANGED       (0x04)
#define MPI_EVENT_RAID_RC_PHYSDISK_CREATED              (0x05)
#define MPI_EVENT_RAID_RC_PHYSDISK_DELETED              (0x06)
#define MPI_EVENT_RAID_RC_PHYSDISK_SETTINGS_CHANGED     (0x07)
#define MPI_EVENT_RAID_RC_PHYSDISK_STATUS_CHANGED       (0x08)
#define MPI_EVENT_RAID_RC_DOMAIN_VAL_NEEDED             (0x09)
#define MPI_EVENT_RAID_RC_SMART_DATA                    (0x0A)
#define MPI_EVENT_RAID_RC_REPLACE_ACTION_STARTED        (0x0B)


/*****************************************************************************
*
*               F i r m w a r e    L o a d    M e s s a g e s
*
*****************************************************************************/

/****************************************************************************/
/*  Firmware Download message and associated structures                     */
/****************************************************************************/

typedef struct _MSG_FW_DOWNLOAD
{
    U8                      ImageType;                  /* 00h */
    U8                      Reserved;                   /* 01h */
    U8                      ChainOffset;                /* 02h */
    U8                      Function;                   /* 03h */
    U8                      Reserved1[3];               /* 04h */
    U8                      MsgFlags;                   /* 07h */
    U32                     MsgContext;                 /* 08h */
    SGE_MPI_UNION           SGL;                        /* 0Ch */
} MSG_FW_DOWNLOAD, MPI_POINTER PTR_MSG_FW_DOWNLOAD,
  FWDownload_t, MPI_POINTER pFWDownload_t;

#define MPI_FW_DOWNLOAD_ITYPE_RESERVED      (0x00)
#define MPI_FW_DOWNLOAD_ITYPE_FW            (0x01)
#define MPI_FW_DOWNLOAD_ITYPE_BIOS          (0x02)
#define MPI_FW_DOWNLOAD_ITYPE_NVDATA        (0x03)
#define MPI_FW_DOWNLOAD_ITYPE_BOOTLOADER    (0x04)


typedef struct _FWDownloadTCSGE
{
    U8                      Reserved;                   /* 00h */
    U8                      ContextSize;                /* 01h */
    U8                      DetailsLength;              /* 02h */
    U8                      Flags;                      /* 03h */
    U32                     Reserved_0100_Checksum;     /* 04h */ /* obsolete Checksum */
    U32                     ImageOffset;                /* 08h */
    U32                     ImageSize;                  /* 0Ch */
} FW_DOWNLOAD_TCSGE, MPI_POINTER PTR_FW_DOWNLOAD_TCSGE,
  FWDownloadTCSGE_t, MPI_POINTER pFWDownloadTCSGE_t;

/* Firmware Download reply */
typedef struct _MSG_FW_DOWNLOAD_REPLY
{
    U8                      ImageType;                  /* 00h */
    U8                      Reserved;                   /* 01h */
    U8                      MsgLength;                  /* 02h */
    U8                      Function;                   /* 03h */
    U8                      Reserved1[3];               /* 04h */
    U8                      MsgFlags;                   /* 07h */
    U32                     MsgContext;                 /* 08h */
    U16                     Reserved2;                  /* 0Ch */
    U16                     IOCStatus;                  /* 0Eh */
    U32                     IOCLogInfo;                 /* 10h */
} MSG_FW_DOWNLOAD_REPLY, MPI_POINTER PTR_MSG_FW_DOWNLOAD_REPLY,
  FWDownloadReply_t, MPI_POINTER pFWDownloadReply_t;


/****************************************************************************/
/*  Firmware Upload message and associated structures                       */
/****************************************************************************/

typedef struct _MSG_FW_UPLOAD
{
    U8                      ImageType;                  /* 00h */
    U8                      Reserved;                   /* 01h */
    U8                      ChainOffset;                /* 02h */
    U8                      Function;                   /* 03h */
    U8                      Reserved1[3];               /* 04h */
    U8                      MsgFlags;                   /* 07h */
    U32                     MsgContext;                 /* 08h */
    SGE_MPI_UNION           SGL;                        /* 0Ch */
} MSG_FW_UPLOAD, MPI_POINTER PTR_MSG_FW_UPLOAD,
  FWUpload_t, MPI_POINTER pFWUpload_t;

#define MPI_FW_UPLOAD_ITYPE_FW_IOC_MEM      (0x00)
#define MPI_FW_UPLOAD_ITYPE_FW_FLASH        (0x01)
#define MPI_FW_UPLOAD_ITYPE_BIOS_FLASH      (0x02)
#define MPI_FW_UPLOAD_ITYPE_NVDATA          (0x03)
#define MPI_FW_UPLOAD_ITYPE_BOOTLOADER      (0x04)

typedef struct _FWUploadTCSGE
{
    U8                      Reserved;                   /* 00h */
    U8                      ContextSize;                /* 01h */
    U8                      DetailsLength;              /* 02h */
    U8                      Flags;                      /* 03h */
    U32                     Reserved1;                  /* 04h */
    U32                     ImageOffset;                /* 08h */
    U32                     ImageSize;                  /* 0Ch */
} FW_UPLOAD_TCSGE, MPI_POINTER PTR_FW_UPLOAD_TCSGE,
  FWUploadTCSGE_t, MPI_POINTER pFWUploadTCSGE_t;

/* Firmware Upload reply */
typedef struct _MSG_FW_UPLOAD_REPLY
{
    U8                      ImageType;                  /* 00h */
    U8                      Reserved;                   /* 01h */
    U8                      MsgLength;                  /* 02h */
    U8                      Function;                   /* 03h */
    U8                      Reserved1[3];               /* 04h */
    U8                      MsgFlags;                   /* 07h */
    U32                     MsgContext;                 /* 08h */
    U16                     Reserved2;                  /* 0Ch */
    U16                     IOCStatus;                  /* 0Eh */
    U32                     IOCLogInfo;                 /* 10h */
    U32                     ActualImageSize;            /* 14h */
} MSG_FW_UPLOAD_REPLY, MPI_POINTER PTR_MSG_FW_UPLOAD_REPLY,
  FWUploadReply_t, MPI_POINTER pFWUploadReply_t;


typedef struct _MPI_FW_HEADER
{
    U32                     ArmBranchInstruction0;      /* 00h */
    U32                     Signature0;                 /* 04h */
    U32                     Signature1;                 /* 08h */
    U32                     Signature2;                 /* 0Ch */
    U32                     ArmBranchInstruction1;      /* 10h */
    U32                     ArmBranchInstruction2;      /* 14h */
    U32                     Reserved;                   /* 18h */
    U32                     Checksum;                   /* 1Ch */
    U16                     VendorId;                   /* 20h */
    U16                     ProductId;                  /* 22h */
    MPI_FW_VERSION          FWVersion;                  /* 24h */
    U32                     SeqCodeVersion;             /* 28h */
    U32                     ImageSize;                  /* 2Ch */
    U32                     NextImageHeaderOffset;      /* 30h */
    U32                     LoadStartAddress;           /* 34h */
    U32                     IopResetVectorValue;        /* 38h */
    U32                     IopResetRegAddr;            /* 3Ch */
    U32                     VersionNameWhat;            /* 40h */
    U8                      VersionName[32];            /* 44h */
    U32                     VendorNameWhat;             /* 64h */
    U8                      VendorName[32];             /* 68h */
} MPI_FW_HEADER, MPI_POINTER PTR_MPI_FW_HEADER,
  MpiFwHeader_t, MPI_POINTER pMpiFwHeader_t;

#define MPI_FW_HEADER_WHAT_SIGNATURE        (0x29232840)

/* defines for using the ProductId field */
#define MPI_FW_HEADER_PID_TYPE_MASK             (0xF000)
#define MPI_FW_HEADER_PID_TYPE_SCSI             (0x0000)
#define MPI_FW_HEADER_PID_TYPE_FC               (0x1000)
#define MPI_FW_HEADER_PID_TYPE_SAS              (0x2000)

#define MPI_FW_HEADER_SIGNATURE_0               (0x5AEAA55A)
#define MPI_FW_HEADER_SIGNATURE_1               (0xA55AEAA5)
#define MPI_FW_HEADER_SIGNATURE_2               (0x5AA55AEA)

#define MPI_FW_HEADER_PID_PROD_MASK                     (0x0F00)
#define MPI_FW_HEADER_PID_PROD_INITIATOR_SCSI           (0x0100)
#define MPI_FW_HEADER_PID_PROD_TARGET_INITIATOR_SCSI    (0x0200)
#define MPI_FW_HEADER_PID_PROD_TARGET_SCSI              (0x0300)
#define MPI_FW_HEADER_PID_PROD_IM_SCSI                  (0x0400)
#define MPI_FW_HEADER_PID_PROD_IS_SCSI                  (0x0500)
#define MPI_FW_HEADER_PID_PROD_CTX_SCSI                 (0x0600)

#define MPI_FW_HEADER_PID_FAMILY_MASK           (0x00FF)
/* SCSI */
#define MPI_FW_HEADER_PID_FAMILY_1030A0_SCSI    (0x0001)
#define MPI_FW_HEADER_PID_FAMILY_1030B0_SCSI    (0x0002)
#define MPI_FW_HEADER_PID_FAMILY_1030B1_SCSI    (0x0003)
#define MPI_FW_HEADER_PID_FAMILY_1030C0_SCSI    (0x0004)
#define MPI_FW_HEADER_PID_FAMILY_1020A0_SCSI    (0x0005)
#define MPI_FW_HEADER_PID_FAMILY_1020B0_SCSI    (0x0006)
#define MPI_FW_HEADER_PID_FAMILY_1020B1_SCSI    (0x0007)
#define MPI_FW_HEADER_PID_FAMILY_1020C0_SCSI    (0x0008)
#define MPI_FW_HEADER_PID_FAMILY_1035A0_SCSI    (0x0009)
#define MPI_FW_HEADER_PID_FAMILY_1035B0_SCSI    (0x000A)
#define MPI_FW_HEADER_PID_FAMILY_1030TA0_SCSI   (0x000B)
#define MPI_FW_HEADER_PID_FAMILY_1020TA0_SCSI   (0x000C)
/* Fibre Channel */
#define MPI_FW_HEADER_PID_FAMILY_909_FC         (0x0000)
#define MPI_FW_HEADER_PID_FAMILY_919_FC         (0x0001)
#define MPI_FW_HEADER_PID_FAMILY_919X_FC        (0x0002)
#define MPI_FW_HEADER_PID_FAMILY_919XL_FC       (0x0003)
#define MPI_FW_HEADER_PID_FAMILY_949_FC         (0x0004)
#define MPI_FW_HEADER_PID_FAMILY_959_FC         (0x0005)
/* SAS */
#define MPI_FW_HEADER_PID_FAMILY_1064_SAS       (0x0001)

typedef struct _MPI_EXT_IMAGE_HEADER
{
    U8                      ImageType;                  /* 00h */
    U8                      Reserved;                   /* 01h */
    U16                     Reserved1;                  /* 02h */
    U32                     Checksum;                   /* 04h */
    U32                     ImageSize;                  /* 08h */
    U32                     NextImageHeaderOffset;      /* 0Ch */
    U32                     LoadStartAddress;           /* 10h */
    U32                     Reserved2;                  /* 14h */
} MPI_EXT_IMAGE_HEADER, MPI_POINTER PTR_MPI_EXT_IMAGE_HEADER,
  MpiExtImageHeader_t, MPI_POINTER pMpiExtImageHeader_t;

/* defines for the ImageType field */
#define MPI_EXT_IMAGE_TYPE_UNSPECIFIED          (0x00)
#define MPI_EXT_IMAGE_TYPE_FW                   (0x01)
#define MPI_EXT_IMAGE_TYPE_NVDATA               (0x03)
#define MPI_EXT_IMAGE_TYPE_BOOTLOADER           (0x04)

#endif
