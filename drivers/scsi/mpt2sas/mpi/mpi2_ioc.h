/*
 *  Copyright (c) 2000-2010 LSI Corporation.
 *
 *
 *           Name:  mpi2_ioc.h
 *          Title:  MPI IOC, Port, Event, FW Download, and FW Upload messages
 *  Creation Date:  October 11, 2006
 *
 *  mpi2_ioc.h Version:  02.00.15
 *
 *  Version History
 *  ---------------
 *
 *  Date      Version   Description
 *  --------  --------  ------------------------------------------------------
 *  04-30-07  02.00.00  Corresponds to Fusion-MPT MPI Specification Rev A.
 *  06-04-07  02.00.01  In IOCFacts Reply structure, renamed MaxDevices to
 *                      MaxTargets.
 *                      Added TotalImageSize field to FWDownload Request.
 *                      Added reserved words to FWUpload Request.
 *  06-26-07  02.00.02  Added IR Configuration Change List Event.
 *  08-31-07  02.00.03  Removed SystemReplyQueueDepth field from the IOCInit
 *                      request and replaced it with
 *                      ReplyDescriptorPostQueueDepth and ReplyFreeQueueDepth.
 *                      Replaced the MinReplyQueueDepth field of the IOCFacts
 *                      reply with MaxReplyDescriptorPostQueueDepth.
 *                      Added MPI2_RDPQ_DEPTH_MIN define to specify the minimum
 *                      depth for the Reply Descriptor Post Queue.
 *                      Added SASAddress field to Initiator Device Table
 *                      Overflow Event data.
 *  10-31-07  02.00.04  Added ReasonCode MPI2_EVENT_SAS_INIT_RC_NOT_RESPONDING
 *                      for SAS Initiator Device Status Change Event data.
 *                      Modified Reason Code defines for SAS Topology Change
 *                      List Event data, including adding a bit for PHY Vacant
 *                      status, and adding a mask for the Reason Code.
 *                      Added define for
 *                      MPI2_EVENT_SAS_TOPO_ES_DELAY_NOT_RESPONDING.
 *                      Added define for MPI2_EXT_IMAGE_TYPE_MEGARAID.
 *  12-18-07  02.00.05  Added Boot Status defines for the IOCExceptions field of
 *                      the IOCFacts Reply.
 *                      Removed MPI2_IOCFACTS_CAPABILITY_EXTENDED_BUFFER define.
 *                      Moved MPI2_VERSION_UNION to mpi2.h.
 *                      Changed MPI2_EVENT_NOTIFICATION_REQUEST to use masks
 *                      instead of enables, and added SASBroadcastPrimitiveMasks
 *                      field.
 *                      Added Log Entry Added Event and related structure.
 *  02-29-08  02.00.06  Added define MPI2_IOCFACTS_CAPABILITY_INTEGRATED_RAID.
 *                      Removed define MPI2_IOCFACTS_PROTOCOL_SMP_TARGET.
 *                      Added MaxVolumes and MaxPersistentEntries fields to
 *                      IOCFacts reply.
 *                      Added ProtocalFlags and IOCCapabilities fields to
 *                      MPI2_FW_IMAGE_HEADER.
 *                      Removed MPI2_PORTENABLE_FLAGS_ENABLE_SINGLE_PORT.
 *  03-03-08  02.00.07  Fixed MPI2_FW_IMAGE_HEADER by changing Reserved26 to
 *                      a U16 (from a U32).
 *                      Removed extra 's' from EventMasks name.
 *  06-27-08  02.00.08  Fixed an offset in a comment.
 *  10-02-08  02.00.09  Removed SystemReplyFrameSize from MPI2_IOC_INIT_REQUEST.
 *                      Removed CurReplyFrameSize from MPI2_IOC_FACTS_REPLY and
 *                      renamed MinReplyFrameSize to ReplyFrameSize.
 *                      Added MPI2_IOCFACTS_EXCEPT_IR_FOREIGN_CONFIG_MAX.
 *                      Added two new RAIDOperation values for Integrated RAID
 *                      Operations Status Event data.
 *                      Added four new IR Configuration Change List Event data
 *                      ReasonCode values.
 *                      Added two new ReasonCode defines for SAS Device Status
 *                      Change Event data.
 *                      Added three new DiscoveryStatus bits for the SAS
 *                      Discovery event data.
 *                      Added Multiplexing Status Change bit to the PhyStatus
 *                      field of the SAS Topology Change List event data.
 *                      Removed define for MPI2_INIT_IMAGE_BOOTFLAGS_XMEMCOPY.
 *                      BootFlags are now product-specific.
 *                      Added defines for the indivdual signature bytes
 *                      for MPI2_INIT_IMAGE_FOOTER.
 *  01-19-09  02.00.10  Added MPI2_IOCFACTS_CAPABILITY_EVENT_REPLAY define.
 *                      Added MPI2_EVENT_SAS_DISC_DS_DOWNSTREAM_INITIATOR
 *                      define.
 *                      Added MPI2_EVENT_SAS_DEV_STAT_RC_SATA_INIT_FAILURE
 *                      define.
 *                      Removed MPI2_EVENT_SAS_DISC_DS_SATA_INIT_FAILURE define.
 *  05-06-09  02.00.11  Added MPI2_IOCFACTS_CAPABILITY_RAID_ACCELERATOR define.
 *                      Added MPI2_IOCFACTS_CAPABILITY_MSI_X_INDEX define.
 *                      Added two new reason codes for SAS Device Status Change
 *                      Event.
 *                      Added new event: SAS PHY Counter.
 *  07-30-09  02.00.12  Added GPIO Interrupt event define and structure.
 *                      Added MPI2_IOCFACTS_CAPABILITY_EXTENDED_BUFFER define.
 *                      Added new product id family for 2208.
 *  10-28-09  02.00.13  Added HostMSIxVectors field to MPI2_IOC_INIT_REQUEST.
 *                      Added MaxMSIxVectors field to MPI2_IOC_FACTS_REPLY.
 *                      Added MinDevHandle field to MPI2_IOC_FACTS_REPLY.
 *                      Added MPI2_IOCFACTS_CAPABILITY_HOST_BASED_DISCOVERY.
 *                      Added MPI2_EVENT_HOST_BASED_DISCOVERY_PHY define.
 *                      Added MPI2_EVENT_SAS_TOPO_ES_NO_EXPANDER define.
 *                      Added Host Based Discovery Phy Event data.
 *                      Added defines for ProductID Product field
 *                      (MPI2_FW_HEADER_PID_).
 *                      Modified values for SAS ProductID Family
 *                      (MPI2_FW_HEADER_PID_FAMILY_).
 *  02-10-10  02.00.14  Added SAS Quiesce Event structure and defines.
 *                      Added PowerManagementControl Request structures and
 *                      defines.
 *  05-12-10  02.00.15  Marked Task Set Full Event as obsolete.
 *                      Added MPI2_EVENT_SAS_TOPO_LR_UNSUPPORTED_PHY define.
 *  --------------------------------------------------------------------------
 */

#ifndef MPI2_IOC_H
#define MPI2_IOC_H

/*****************************************************************************
*
*               IOC Messages
*
*****************************************************************************/

/****************************************************************************
*  IOCInit message
****************************************************************************/

/* IOCInit Request message */
typedef struct _MPI2_IOC_INIT_REQUEST
{
    U8                      WhoInit;                        /* 0x00 */
    U8                      Reserved1;                      /* 0x01 */
    U8                      ChainOffset;                    /* 0x02 */
    U8                      Function;                       /* 0x03 */
    U16                     Reserved2;                      /* 0x04 */
    U8                      Reserved3;                      /* 0x06 */
    U8                      MsgFlags;                       /* 0x07 */
    U8                      VP_ID;                          /* 0x08 */
    U8                      VF_ID;                          /* 0x09 */
    U16                     Reserved4;                      /* 0x0A */
    U16                     MsgVersion;                     /* 0x0C */
    U16                     HeaderVersion;                  /* 0x0E */
    U32                     Reserved5;                      /* 0x10 */
    U16                     Reserved6;                      /* 0x14 */
    U8                      Reserved7;                      /* 0x16 */
    U8                      HostMSIxVectors;                /* 0x17 */
    U16                     Reserved8;                      /* 0x18 */
    U16                     SystemRequestFrameSize;         /* 0x1A */
    U16                     ReplyDescriptorPostQueueDepth;  /* 0x1C */
    U16                     ReplyFreeQueueDepth;            /* 0x1E */
    U32                     SenseBufferAddressHigh;         /* 0x20 */
    U32                     SystemReplyAddressHigh;         /* 0x24 */
    U64                     SystemRequestFrameBaseAddress;  /* 0x28 */
    U64                     ReplyDescriptorPostQueueAddress;/* 0x30 */
    U64                     ReplyFreeQueueAddress;          /* 0x38 */
    U64                     TimeStamp;                      /* 0x40 */
} MPI2_IOC_INIT_REQUEST, MPI2_POINTER PTR_MPI2_IOC_INIT_REQUEST,
  Mpi2IOCInitRequest_t, MPI2_POINTER pMpi2IOCInitRequest_t;

/* WhoInit values */
#define MPI2_WHOINIT_NOT_INITIALIZED            (0x00)
#define MPI2_WHOINIT_SYSTEM_BIOS                (0x01)
#define MPI2_WHOINIT_ROM_BIOS                   (0x02)
#define MPI2_WHOINIT_PCI_PEER                   (0x03)
#define MPI2_WHOINIT_HOST_DRIVER                (0x04)
#define MPI2_WHOINIT_MANUFACTURER               (0x05)

/* MsgVersion */
#define MPI2_IOCINIT_MSGVERSION_MAJOR_MASK      (0xFF00)
#define MPI2_IOCINIT_MSGVERSION_MAJOR_SHIFT     (8)
#define MPI2_IOCINIT_MSGVERSION_MINOR_MASK      (0x00FF)
#define MPI2_IOCINIT_MSGVERSION_MINOR_SHIFT     (0)

/* HeaderVersion */
#define MPI2_IOCINIT_HDRVERSION_UNIT_MASK       (0xFF00)
#define MPI2_IOCINIT_HDRVERSION_UNIT_SHIFT      (8)
#define MPI2_IOCINIT_HDRVERSION_DEV_MASK        (0x00FF)
#define MPI2_IOCINIT_HDRVERSION_DEV_SHIFT       (0)

/* minimum depth for the Reply Descriptor Post Queue */
#define MPI2_RDPQ_DEPTH_MIN                     (16)


/* IOCInit Reply message */
typedef struct _MPI2_IOC_INIT_REPLY
{
    U8                      WhoInit;                        /* 0x00 */
    U8                      Reserved1;                      /* 0x01 */
    U8                      MsgLength;                      /* 0x02 */
    U8                      Function;                       /* 0x03 */
    U16                     Reserved2;                      /* 0x04 */
    U8                      Reserved3;                      /* 0x06 */
    U8                      MsgFlags;                       /* 0x07 */
    U8                      VP_ID;                          /* 0x08 */
    U8                      VF_ID;                          /* 0x09 */
    U16                     Reserved4;                      /* 0x0A */
    U16                     Reserved5;                      /* 0x0C */
    U16                     IOCStatus;                      /* 0x0E */
    U32                     IOCLogInfo;                     /* 0x10 */
} MPI2_IOC_INIT_REPLY, MPI2_POINTER PTR_MPI2_IOC_INIT_REPLY,
  Mpi2IOCInitReply_t, MPI2_POINTER pMpi2IOCInitReply_t;


/****************************************************************************
*  IOCFacts message
****************************************************************************/

/* IOCFacts Request message */
typedef struct _MPI2_IOC_FACTS_REQUEST
{
    U16                     Reserved1;                      /* 0x00 */
    U8                      ChainOffset;                    /* 0x02 */
    U8                      Function;                       /* 0x03 */
    U16                     Reserved2;                      /* 0x04 */
    U8                      Reserved3;                      /* 0x06 */
    U8                      MsgFlags;                       /* 0x07 */
    U8                      VP_ID;                          /* 0x08 */
    U8                      VF_ID;                          /* 0x09 */
    U16                     Reserved4;                      /* 0x0A */
} MPI2_IOC_FACTS_REQUEST, MPI2_POINTER PTR_MPI2_IOC_FACTS_REQUEST,
  Mpi2IOCFactsRequest_t, MPI2_POINTER pMpi2IOCFactsRequest_t;


/* IOCFacts Reply message */
typedef struct _MPI2_IOC_FACTS_REPLY
{
    U16                     MsgVersion;                     /* 0x00 */
    U8                      MsgLength;                      /* 0x02 */
    U8                      Function;                       /* 0x03 */
    U16                     HeaderVersion;                  /* 0x04 */
    U8                      IOCNumber;                      /* 0x06 */
    U8                      MsgFlags;                       /* 0x07 */
    U8                      VP_ID;                          /* 0x08 */
    U8                      VF_ID;                          /* 0x09 */
    U16                     Reserved1;                      /* 0x0A */
    U16                     IOCExceptions;                  /* 0x0C */
    U16                     IOCStatus;                      /* 0x0E */
    U32                     IOCLogInfo;                     /* 0x10 */
    U8                      MaxChainDepth;                  /* 0x14 */
    U8                      WhoInit;                        /* 0x15 */
    U8                      NumberOfPorts;                  /* 0x16 */
    U8                      MaxMSIxVectors;                 /* 0x17 */
    U16                     RequestCredit;                  /* 0x18 */
    U16                     ProductID;                      /* 0x1A */
    U32                     IOCCapabilities;                /* 0x1C */
    MPI2_VERSION_UNION      FWVersion;                      /* 0x20 */
    U16                     IOCRequestFrameSize;            /* 0x24 */
    U16                     Reserved3;                      /* 0x26 */
    U16                     MaxInitiators;                  /* 0x28 */
    U16                     MaxTargets;                     /* 0x2A */
    U16                     MaxSasExpanders;                /* 0x2C */
    U16                     MaxEnclosures;                  /* 0x2E */
    U16                     ProtocolFlags;                  /* 0x30 */
    U16                     HighPriorityCredit;             /* 0x32 */
    U16                     MaxReplyDescriptorPostQueueDepth; /* 0x34 */
    U8                      ReplyFrameSize;                 /* 0x36 */
    U8                      MaxVolumes;                     /* 0x37 */
    U16                     MaxDevHandle;                   /* 0x38 */
    U16                     MaxPersistentEntries;           /* 0x3A */
    U16                     MinDevHandle;                   /* 0x3C */
    U16                     Reserved4;                      /* 0x3E */
} MPI2_IOC_FACTS_REPLY, MPI2_POINTER PTR_MPI2_IOC_FACTS_REPLY,
  Mpi2IOCFactsReply_t, MPI2_POINTER pMpi2IOCFactsReply_t;

/* MsgVersion */
#define MPI2_IOCFACTS_MSGVERSION_MAJOR_MASK             (0xFF00)
#define MPI2_IOCFACTS_MSGVERSION_MAJOR_SHIFT            (8)
#define MPI2_IOCFACTS_MSGVERSION_MINOR_MASK             (0x00FF)
#define MPI2_IOCFACTS_MSGVERSION_MINOR_SHIFT            (0)

/* HeaderVersion */
#define MPI2_IOCFACTS_HDRVERSION_UNIT_MASK              (0xFF00)
#define MPI2_IOCFACTS_HDRVERSION_UNIT_SHIFT             (8)
#define MPI2_IOCFACTS_HDRVERSION_DEV_MASK               (0x00FF)
#define MPI2_IOCFACTS_HDRVERSION_DEV_SHIFT              (0)

/* IOCExceptions */
#define MPI2_IOCFACTS_EXCEPT_IR_FOREIGN_CONFIG_MAX      (0x0100)

#define MPI2_IOCFACTS_EXCEPT_BOOTSTAT_MASK              (0x00E0)
#define MPI2_IOCFACTS_EXCEPT_BOOTSTAT_GOOD              (0x0000)
#define MPI2_IOCFACTS_EXCEPT_BOOTSTAT_BACKUP            (0x0020)
#define MPI2_IOCFACTS_EXCEPT_BOOTSTAT_RESTORED          (0x0040)
#define MPI2_IOCFACTS_EXCEPT_BOOTSTAT_CORRUPT_BACKUP    (0x0060)

#define MPI2_IOCFACTS_EXCEPT_METADATA_UNSUPPORTED       (0x0010)
#define MPI2_IOCFACTS_EXCEPT_MANUFACT_CHECKSUM_FAIL     (0x0008)
#define MPI2_IOCFACTS_EXCEPT_FW_CHECKSUM_FAIL           (0x0004)
#define MPI2_IOCFACTS_EXCEPT_RAID_CONFIG_INVALID        (0x0002)
#define MPI2_IOCFACTS_EXCEPT_CONFIG_CHECKSUM_FAIL       (0x0001)

/* defines for WhoInit field are after the IOCInit Request */

/* ProductID field uses MPI2_FW_HEADER_PID_ */

/* IOCCapabilities */
#define MPI2_IOCFACTS_CAPABILITY_HOST_BASED_DISCOVERY   (0x00010000)
#define MPI2_IOCFACTS_CAPABILITY_MSI_X_INDEX            (0x00008000)
#define MPI2_IOCFACTS_CAPABILITY_RAID_ACCELERATOR       (0x00004000)
#define MPI2_IOCFACTS_CAPABILITY_EVENT_REPLAY           (0x00002000)
#define MPI2_IOCFACTS_CAPABILITY_INTEGRATED_RAID        (0x00001000)
#define MPI2_IOCFACTS_CAPABILITY_TLR                    (0x00000800)
#define MPI2_IOCFACTS_CAPABILITY_MULTICAST              (0x00000100)
#define MPI2_IOCFACTS_CAPABILITY_BIDIRECTIONAL_TARGET   (0x00000080)
#define MPI2_IOCFACTS_CAPABILITY_EEDP                   (0x00000040)
#define MPI2_IOCFACTS_CAPABILITY_EXTENDED_BUFFER        (0x00000020)
#define MPI2_IOCFACTS_CAPABILITY_SNAPSHOT_BUFFER        (0x00000010)
#define MPI2_IOCFACTS_CAPABILITY_DIAG_TRACE_BUFFER      (0x00000008)
#define MPI2_IOCFACTS_CAPABILITY_TASK_SET_FULL_HANDLING (0x00000004)

/* ProtocolFlags */
#define MPI2_IOCFACTS_PROTOCOL_SCSI_TARGET              (0x0001)
#define MPI2_IOCFACTS_PROTOCOL_SCSI_INITIATOR           (0x0002)


/****************************************************************************
*  PortFacts message
****************************************************************************/

/* PortFacts Request message */
typedef struct _MPI2_PORT_FACTS_REQUEST
{
    U16                     Reserved1;                      /* 0x00 */
    U8                      ChainOffset;                    /* 0x02 */
    U8                      Function;                       /* 0x03 */
    U16                     Reserved2;                      /* 0x04 */
    U8                      PortNumber;                     /* 0x06 */
    U8                      MsgFlags;                       /* 0x07 */
    U8                      VP_ID;                          /* 0x08 */
    U8                      VF_ID;                          /* 0x09 */
    U16                     Reserved3;                      /* 0x0A */
} MPI2_PORT_FACTS_REQUEST, MPI2_POINTER PTR_MPI2_PORT_FACTS_REQUEST,
  Mpi2PortFactsRequest_t, MPI2_POINTER pMpi2PortFactsRequest_t;

/* PortFacts Reply message */
typedef struct _MPI2_PORT_FACTS_REPLY
{
    U16                     Reserved1;                      /* 0x00 */
    U8                      MsgLength;                      /* 0x02 */
    U8                      Function;                       /* 0x03 */
    U16                     Reserved2;                      /* 0x04 */
    U8                      PortNumber;                     /* 0x06 */
    U8                      MsgFlags;                       /* 0x07 */
    U8                      VP_ID;                          /* 0x08 */
    U8                      VF_ID;                          /* 0x09 */
    U16                     Reserved3;                      /* 0x0A */
    U16                     Reserved4;                      /* 0x0C */
    U16                     IOCStatus;                      /* 0x0E */
    U32                     IOCLogInfo;                     /* 0x10 */
    U8                      Reserved5;                      /* 0x14 */
    U8                      PortType;                       /* 0x15 */
    U16                     Reserved6;                      /* 0x16 */
    U16                     MaxPostedCmdBuffers;            /* 0x18 */
    U16                     Reserved7;                      /* 0x1A */
} MPI2_PORT_FACTS_REPLY, MPI2_POINTER PTR_MPI2_PORT_FACTS_REPLY,
  Mpi2PortFactsReply_t, MPI2_POINTER pMpi2PortFactsReply_t;

/* PortType values */
#define MPI2_PORTFACTS_PORTTYPE_INACTIVE            (0x00)
#define MPI2_PORTFACTS_PORTTYPE_FC                  (0x10)
#define MPI2_PORTFACTS_PORTTYPE_ISCSI               (0x20)
#define MPI2_PORTFACTS_PORTTYPE_SAS_PHYSICAL        (0x30)
#define MPI2_PORTFACTS_PORTTYPE_SAS_VIRTUAL         (0x31)


/****************************************************************************
*  PortEnable message
****************************************************************************/

/* PortEnable Request message */
typedef struct _MPI2_PORT_ENABLE_REQUEST
{
    U16                     Reserved1;                      /* 0x00 */
    U8                      ChainOffset;                    /* 0x02 */
    U8                      Function;                       /* 0x03 */
    U8                      Reserved2;                      /* 0x04 */
    U8                      PortFlags;                      /* 0x05 */
    U8                      Reserved3;                      /* 0x06 */
    U8                      MsgFlags;                       /* 0x07 */
    U8                      VP_ID;                          /* 0x08 */
    U8                      VF_ID;                          /* 0x09 */
    U16                     Reserved4;                      /* 0x0A */
} MPI2_PORT_ENABLE_REQUEST, MPI2_POINTER PTR_MPI2_PORT_ENABLE_REQUEST,
  Mpi2PortEnableRequest_t, MPI2_POINTER pMpi2PortEnableRequest_t;


/* PortEnable Reply message */
typedef struct _MPI2_PORT_ENABLE_REPLY
{
    U16                     Reserved1;                      /* 0x00 */
    U8                      MsgLength;                      /* 0x02 */
    U8                      Function;                       /* 0x03 */
    U8                      Reserved2;                      /* 0x04 */
    U8                      PortFlags;                      /* 0x05 */
    U8                      Reserved3;                      /* 0x06 */
    U8                      MsgFlags;                       /* 0x07 */
    U8                      VP_ID;                          /* 0x08 */
    U8                      VF_ID;                          /* 0x09 */
    U16                     Reserved4;                      /* 0x0A */
    U16                     Reserved5;                      /* 0x0C */
    U16                     IOCStatus;                      /* 0x0E */
    U32                     IOCLogInfo;                     /* 0x10 */
} MPI2_PORT_ENABLE_REPLY, MPI2_POINTER PTR_MPI2_PORT_ENABLE_REPLY,
  Mpi2PortEnableReply_t, MPI2_POINTER pMpi2PortEnableReply_t;


/****************************************************************************
*  EventNotification message
****************************************************************************/

/* EventNotification Request message */
#define MPI2_EVENT_NOTIFY_EVENTMASK_WORDS           (4)

typedef struct _MPI2_EVENT_NOTIFICATION_REQUEST
{
    U16                     Reserved1;                      /* 0x00 */
    U8                      ChainOffset;                    /* 0x02 */
    U8                      Function;                       /* 0x03 */
    U16                     Reserved2;                      /* 0x04 */
    U8                      Reserved3;                      /* 0x06 */
    U8                      MsgFlags;                       /* 0x07 */
    U8                      VP_ID;                          /* 0x08 */
    U8                      VF_ID;                          /* 0x09 */
    U16                     Reserved4;                      /* 0x0A */
    U32                     Reserved5;                      /* 0x0C */
    U32                     Reserved6;                      /* 0x10 */
    U32                     EventMasks[MPI2_EVENT_NOTIFY_EVENTMASK_WORDS];/* 0x14 */
    U16                     SASBroadcastPrimitiveMasks;     /* 0x24 */
    U16                     Reserved7;                      /* 0x26 */
    U32                     Reserved8;                      /* 0x28 */
} MPI2_EVENT_NOTIFICATION_REQUEST,
  MPI2_POINTER PTR_MPI2_EVENT_NOTIFICATION_REQUEST,
  Mpi2EventNotificationRequest_t, MPI2_POINTER pMpi2EventNotificationRequest_t;


/* EventNotification Reply message */
typedef struct _MPI2_EVENT_NOTIFICATION_REPLY
{
    U16                     EventDataLength;                /* 0x00 */
    U8                      MsgLength;                      /* 0x02 */
    U8                      Function;                       /* 0x03 */
    U16                     Reserved1;                      /* 0x04 */
    U8                      AckRequired;                    /* 0x06 */
    U8                      MsgFlags;                       /* 0x07 */
    U8                      VP_ID;                          /* 0x08 */
    U8                      VF_ID;                          /* 0x09 */
    U16                     Reserved2;                      /* 0x0A */
    U16                     Reserved3;                      /* 0x0C */
    U16                     IOCStatus;                      /* 0x0E */
    U32                     IOCLogInfo;                     /* 0x10 */
    U16                     Event;                          /* 0x14 */
    U16                     Reserved4;                      /* 0x16 */
    U32                     EventContext;                   /* 0x18 */
    U32                     EventData[1];                   /* 0x1C */
} MPI2_EVENT_NOTIFICATION_REPLY, MPI2_POINTER PTR_MPI2_EVENT_NOTIFICATION_REPLY,
  Mpi2EventNotificationReply_t, MPI2_POINTER pMpi2EventNotificationReply_t;

/* AckRequired */
#define MPI2_EVENT_NOTIFICATION_ACK_NOT_REQUIRED    (0x00)
#define MPI2_EVENT_NOTIFICATION_ACK_REQUIRED        (0x01)

/* Event */
#define MPI2_EVENT_LOG_DATA                         (0x0001)
#define MPI2_EVENT_STATE_CHANGE                     (0x0002)
#define MPI2_EVENT_HARD_RESET_RECEIVED              (0x0005)
#define MPI2_EVENT_EVENT_CHANGE                     (0x000A)
#define MPI2_EVENT_TASK_SET_FULL                    (0x000E) /* obsolete */
#define MPI2_EVENT_SAS_DEVICE_STATUS_CHANGE         (0x000F)
#define MPI2_EVENT_IR_OPERATION_STATUS              (0x0014)
#define MPI2_EVENT_SAS_DISCOVERY                    (0x0016)
#define MPI2_EVENT_SAS_BROADCAST_PRIMITIVE          (0x0017)
#define MPI2_EVENT_SAS_INIT_DEVICE_STATUS_CHANGE    (0x0018)
#define MPI2_EVENT_SAS_INIT_TABLE_OVERFLOW          (0x0019)
#define MPI2_EVENT_SAS_TOPOLOGY_CHANGE_LIST         (0x001C)
#define MPI2_EVENT_SAS_ENCL_DEVICE_STATUS_CHANGE    (0x001D)
#define MPI2_EVENT_IR_VOLUME                        (0x001E)
#define MPI2_EVENT_IR_PHYSICAL_DISK                 (0x001F)
#define MPI2_EVENT_IR_CONFIGURATION_CHANGE_LIST     (0x0020)
#define MPI2_EVENT_LOG_ENTRY_ADDED                  (0x0021)
#define MPI2_EVENT_SAS_PHY_COUNTER                  (0x0022)
#define MPI2_EVENT_GPIO_INTERRUPT                   (0x0023)
#define MPI2_EVENT_HOST_BASED_DISCOVERY_PHY         (0x0024)
#define MPI2_EVENT_SAS_QUIESCE                      (0x0025)


/* Log Entry Added Event data */

/* the following structure matches MPI2_LOG_0_ENTRY in mpi2_cnfg.h */
#define MPI2_EVENT_DATA_LOG_DATA_LENGTH             (0x1C)

typedef struct _MPI2_EVENT_DATA_LOG_ENTRY_ADDED
{
    U64         TimeStamp;                          /* 0x00 */
    U32         Reserved1;                          /* 0x08 */
    U16         LogSequence;                        /* 0x0C */
    U16         LogEntryQualifier;                  /* 0x0E */
    U8          VP_ID;                              /* 0x10 */
    U8          VF_ID;                              /* 0x11 */
    U16         Reserved2;                          /* 0x12 */
    U8          LogData[MPI2_EVENT_DATA_LOG_DATA_LENGTH];/* 0x14 */
} MPI2_EVENT_DATA_LOG_ENTRY_ADDED,
  MPI2_POINTER PTR_MPI2_EVENT_DATA_LOG_ENTRY_ADDED,
  Mpi2EventDataLogEntryAdded_t, MPI2_POINTER pMpi2EventDataLogEntryAdded_t;

/* GPIO Interrupt Event data */

typedef struct _MPI2_EVENT_DATA_GPIO_INTERRUPT {
    U8          GPIONum;                            /* 0x00 */
    U8          Reserved1;                          /* 0x01 */
    U16         Reserved2;                          /* 0x02 */
} MPI2_EVENT_DATA_GPIO_INTERRUPT,
  MPI2_POINTER PTR_MPI2_EVENT_DATA_GPIO_INTERRUPT,
  Mpi2EventDataGpioInterrupt_t, MPI2_POINTER pMpi2EventDataGpioInterrupt_t;

/* Hard Reset Received Event data */

typedef struct _MPI2_EVENT_DATA_HARD_RESET_RECEIVED
{
    U8                      Reserved1;                      /* 0x00 */
    U8                      Port;                           /* 0x01 */
    U16                     Reserved2;                      /* 0x02 */
} MPI2_EVENT_DATA_HARD_RESET_RECEIVED,
  MPI2_POINTER PTR_MPI2_EVENT_DATA_HARD_RESET_RECEIVED,
  Mpi2EventDataHardResetReceived_t,
  MPI2_POINTER pMpi2EventDataHardResetReceived_t;

/* Task Set Full Event data */
/*   this event is obsolete */

typedef struct _MPI2_EVENT_DATA_TASK_SET_FULL
{
    U16                     DevHandle;                      /* 0x00 */
    U16                     CurrentDepth;                   /* 0x02 */
} MPI2_EVENT_DATA_TASK_SET_FULL, MPI2_POINTER PTR_MPI2_EVENT_DATA_TASK_SET_FULL,
  Mpi2EventDataTaskSetFull_t, MPI2_POINTER pMpi2EventDataTaskSetFull_t;


/* SAS Device Status Change Event data */

typedef struct _MPI2_EVENT_DATA_SAS_DEVICE_STATUS_CHANGE
{
    U16                     TaskTag;                        /* 0x00 */
    U8                      ReasonCode;                     /* 0x02 */
    U8                      Reserved1;                      /* 0x03 */
    U8                      ASC;                            /* 0x04 */
    U8                      ASCQ;                           /* 0x05 */
    U16                     DevHandle;                      /* 0x06 */
    U32                     Reserved2;                      /* 0x08 */
    U64                     SASAddress;                     /* 0x0C */
    U8                      LUN[8];                         /* 0x14 */
} MPI2_EVENT_DATA_SAS_DEVICE_STATUS_CHANGE,
  MPI2_POINTER PTR_MPI2_EVENT_DATA_SAS_DEVICE_STATUS_CHANGE,
  Mpi2EventDataSasDeviceStatusChange_t,
  MPI2_POINTER pMpi2EventDataSasDeviceStatusChange_t;

/* SAS Device Status Change Event data ReasonCode values */
#define MPI2_EVENT_SAS_DEV_STAT_RC_SMART_DATA                           (0x05)
#define MPI2_EVENT_SAS_DEV_STAT_RC_UNSUPPORTED                          (0x07)
#define MPI2_EVENT_SAS_DEV_STAT_RC_INTERNAL_DEVICE_RESET                (0x08)
#define MPI2_EVENT_SAS_DEV_STAT_RC_TASK_ABORT_INTERNAL                  (0x09)
#define MPI2_EVENT_SAS_DEV_STAT_RC_ABORT_TASK_SET_INTERNAL              (0x0A)
#define MPI2_EVENT_SAS_DEV_STAT_RC_CLEAR_TASK_SET_INTERNAL              (0x0B)
#define MPI2_EVENT_SAS_DEV_STAT_RC_QUERY_TASK_INTERNAL                  (0x0C)
#define MPI2_EVENT_SAS_DEV_STAT_RC_ASYNC_NOTIFICATION                   (0x0D)
#define MPI2_EVENT_SAS_DEV_STAT_RC_CMP_INTERNAL_DEV_RESET               (0x0E)
#define MPI2_EVENT_SAS_DEV_STAT_RC_CMP_TASK_ABORT_INTERNAL              (0x0F)
#define MPI2_EVENT_SAS_DEV_STAT_RC_SATA_INIT_FAILURE                    (0x10)
#define MPI2_EVENT_SAS_DEV_STAT_RC_EXPANDER_REDUCED_FUNCTIONALITY       (0x11)
#define MPI2_EVENT_SAS_DEV_STAT_RC_CMP_EXPANDER_REDUCED_FUNCTIONALITY   (0x12)


/* Integrated RAID Operation Status Event data */

typedef struct _MPI2_EVENT_DATA_IR_OPERATION_STATUS
{
    U16                     VolDevHandle;               /* 0x00 */
    U16                     Reserved1;                  /* 0x02 */
    U8                      RAIDOperation;              /* 0x04 */
    U8                      PercentComplete;            /* 0x05 */
    U16                     Reserved2;                  /* 0x06 */
    U32                     Resereved3;                 /* 0x08 */
} MPI2_EVENT_DATA_IR_OPERATION_STATUS,
  MPI2_POINTER PTR_MPI2_EVENT_DATA_IR_OPERATION_STATUS,
  Mpi2EventDataIrOperationStatus_t,
  MPI2_POINTER pMpi2EventDataIrOperationStatus_t;

/* Integrated RAID Operation Status Event data RAIDOperation values */
#define MPI2_EVENT_IR_RAIDOP_RESYNC                     (0x00)
#define MPI2_EVENT_IR_RAIDOP_ONLINE_CAP_EXPANSION       (0x01)
#define MPI2_EVENT_IR_RAIDOP_CONSISTENCY_CHECK          (0x02)
#define MPI2_EVENT_IR_RAIDOP_BACKGROUND_INIT            (0x03)
#define MPI2_EVENT_IR_RAIDOP_MAKE_DATA_CONSISTENT       (0x04)


/* Integrated RAID Volume Event data */

typedef struct _MPI2_EVENT_DATA_IR_VOLUME
{
    U16                     VolDevHandle;               /* 0x00 */
    U8                      ReasonCode;                 /* 0x02 */
    U8                      Reserved1;                  /* 0x03 */
    U32                     NewValue;                   /* 0x04 */
    U32                     PreviousValue;              /* 0x08 */
} MPI2_EVENT_DATA_IR_VOLUME, MPI2_POINTER PTR_MPI2_EVENT_DATA_IR_VOLUME,
  Mpi2EventDataIrVolume_t, MPI2_POINTER pMpi2EventDataIrVolume_t;

/* Integrated RAID Volume Event data ReasonCode values */
#define MPI2_EVENT_IR_VOLUME_RC_SETTINGS_CHANGED        (0x01)
#define MPI2_EVENT_IR_VOLUME_RC_STATUS_FLAGS_CHANGED    (0x02)
#define MPI2_EVENT_IR_VOLUME_RC_STATE_CHANGED           (0x03)


/* Integrated RAID Physical Disk Event data */

typedef struct _MPI2_EVENT_DATA_IR_PHYSICAL_DISK
{
    U16                     Reserved1;                  /* 0x00 */
    U8                      ReasonCode;                 /* 0x02 */
    U8                      PhysDiskNum;                /* 0x03 */
    U16                     PhysDiskDevHandle;          /* 0x04 */
    U16                     Reserved2;                  /* 0x06 */
    U16                     Slot;                       /* 0x08 */
    U16                     EnclosureHandle;            /* 0x0A */
    U32                     NewValue;                   /* 0x0C */
    U32                     PreviousValue;              /* 0x10 */
} MPI2_EVENT_DATA_IR_PHYSICAL_DISK,
  MPI2_POINTER PTR_MPI2_EVENT_DATA_IR_PHYSICAL_DISK,
  Mpi2EventDataIrPhysicalDisk_t, MPI2_POINTER pMpi2EventDataIrPhysicalDisk_t;

/* Integrated RAID Physical Disk Event data ReasonCode values */
#define MPI2_EVENT_IR_PHYSDISK_RC_SETTINGS_CHANGED      (0x01)
#define MPI2_EVENT_IR_PHYSDISK_RC_STATUS_FLAGS_CHANGED  (0x02)
#define MPI2_EVENT_IR_PHYSDISK_RC_STATE_CHANGED         (0x03)


/* Integrated RAID Configuration Change List Event data */

/*
 * Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 * one and check NumElements at runtime.
 */
#ifndef MPI2_EVENT_IR_CONFIG_ELEMENT_COUNT
#define MPI2_EVENT_IR_CONFIG_ELEMENT_COUNT          (1)
#endif

typedef struct _MPI2_EVENT_IR_CONFIG_ELEMENT
{
    U16                     ElementFlags;               /* 0x00 */
    U16                     VolDevHandle;               /* 0x02 */
    U8                      ReasonCode;                 /* 0x04 */
    U8                      PhysDiskNum;                /* 0x05 */
    U16                     PhysDiskDevHandle;          /* 0x06 */
} MPI2_EVENT_IR_CONFIG_ELEMENT, MPI2_POINTER PTR_MPI2_EVENT_IR_CONFIG_ELEMENT,
  Mpi2EventIrConfigElement_t, MPI2_POINTER pMpi2EventIrConfigElement_t;

/* IR Configuration Change List Event data ElementFlags values */
#define MPI2_EVENT_IR_CHANGE_EFLAGS_ELEMENT_TYPE_MASK   (0x000F)
#define MPI2_EVENT_IR_CHANGE_EFLAGS_VOLUME_ELEMENT      (0x0000)
#define MPI2_EVENT_IR_CHANGE_EFLAGS_VOLPHYSDISK_ELEMENT (0x0001)
#define MPI2_EVENT_IR_CHANGE_EFLAGS_HOTSPARE_ELEMENT    (0x0002)

/* IR Configuration Change List Event data ReasonCode values */
#define MPI2_EVENT_IR_CHANGE_RC_ADDED                   (0x01)
#define MPI2_EVENT_IR_CHANGE_RC_REMOVED                 (0x02)
#define MPI2_EVENT_IR_CHANGE_RC_NO_CHANGE               (0x03)
#define MPI2_EVENT_IR_CHANGE_RC_HIDE                    (0x04)
#define MPI2_EVENT_IR_CHANGE_RC_UNHIDE                  (0x05)
#define MPI2_EVENT_IR_CHANGE_RC_VOLUME_CREATED          (0x06)
#define MPI2_EVENT_IR_CHANGE_RC_VOLUME_DELETED          (0x07)
#define MPI2_EVENT_IR_CHANGE_RC_PD_CREATED              (0x08)
#define MPI2_EVENT_IR_CHANGE_RC_PD_DELETED              (0x09)

typedef struct _MPI2_EVENT_DATA_IR_CONFIG_CHANGE_LIST
{
    U8                              NumElements;        /* 0x00 */
    U8                              Reserved1;          /* 0x01 */
    U8                              Reserved2;          /* 0x02 */
    U8                              ConfigNum;          /* 0x03 */
    U32                             Flags;              /* 0x04 */
    MPI2_EVENT_IR_CONFIG_ELEMENT    ConfigElement[MPI2_EVENT_IR_CONFIG_ELEMENT_COUNT];    /* 0x08 */
} MPI2_EVENT_DATA_IR_CONFIG_CHANGE_LIST,
  MPI2_POINTER PTR_MPI2_EVENT_DATA_IR_CONFIG_CHANGE_LIST,
  Mpi2EventDataIrConfigChangeList_t,
  MPI2_POINTER pMpi2EventDataIrConfigChangeList_t;

/* IR Configuration Change List Event data Flags values */
#define MPI2_EVENT_IR_CHANGE_FLAGS_FOREIGN_CONFIG   (0x00000001)


/* SAS Discovery Event data */

typedef struct _MPI2_EVENT_DATA_SAS_DISCOVERY
{
    U8                      Flags;                      /* 0x00 */
    U8                      ReasonCode;                 /* 0x01 */
    U8                      PhysicalPort;               /* 0x02 */
    U8                      Reserved1;                  /* 0x03 */
    U32                     DiscoveryStatus;            /* 0x04 */
} MPI2_EVENT_DATA_SAS_DISCOVERY,
  MPI2_POINTER PTR_MPI2_EVENT_DATA_SAS_DISCOVERY,
  Mpi2EventDataSasDiscovery_t, MPI2_POINTER pMpi2EventDataSasDiscovery_t;

/* SAS Discovery Event data Flags values */
#define MPI2_EVENT_SAS_DISC_DEVICE_CHANGE                   (0x02)
#define MPI2_EVENT_SAS_DISC_IN_PROGRESS                     (0x01)

/* SAS Discovery Event data ReasonCode values */
#define MPI2_EVENT_SAS_DISC_RC_STARTED                      (0x01)
#define MPI2_EVENT_SAS_DISC_RC_COMPLETED                    (0x02)

/* SAS Discovery Event data DiscoveryStatus values */
#define MPI2_EVENT_SAS_DISC_DS_MAX_ENCLOSURES_EXCEED            (0x80000000)
#define MPI2_EVENT_SAS_DISC_DS_MAX_EXPANDERS_EXCEED             (0x40000000)
#define MPI2_EVENT_SAS_DISC_DS_MAX_DEVICES_EXCEED               (0x20000000)
#define MPI2_EVENT_SAS_DISC_DS_MAX_TOPO_PHYS_EXCEED             (0x10000000)
#define MPI2_EVENT_SAS_DISC_DS_DOWNSTREAM_INITIATOR             (0x08000000)
#define MPI2_EVENT_SAS_DISC_DS_MULTI_SUBTRACTIVE_SUBTRACTIVE    (0x00008000)
#define MPI2_EVENT_SAS_DISC_DS_EXP_MULTI_SUBTRACTIVE            (0x00004000)
#define MPI2_EVENT_SAS_DISC_DS_MULTI_PORT_DOMAIN                (0x00002000)
#define MPI2_EVENT_SAS_DISC_DS_TABLE_TO_SUBTRACTIVE_LINK        (0x00001000)
#define MPI2_EVENT_SAS_DISC_DS_UNSUPPORTED_DEVICE               (0x00000800)
#define MPI2_EVENT_SAS_DISC_DS_TABLE_LINK                       (0x00000400)
#define MPI2_EVENT_SAS_DISC_DS_SUBTRACTIVE_LINK                 (0x00000200)
#define MPI2_EVENT_SAS_DISC_DS_SMP_CRC_ERROR                    (0x00000100)
#define MPI2_EVENT_SAS_DISC_DS_SMP_FUNCTION_FAILED              (0x00000080)
#define MPI2_EVENT_SAS_DISC_DS_INDEX_NOT_EXIST                  (0x00000040)
#define MPI2_EVENT_SAS_DISC_DS_OUT_ROUTE_ENTRIES                (0x00000020)
#define MPI2_EVENT_SAS_DISC_DS_SMP_TIMEOUT                      (0x00000010)
#define MPI2_EVENT_SAS_DISC_DS_MULTIPLE_PORTS                   (0x00000004)
#define MPI2_EVENT_SAS_DISC_DS_UNADDRESSABLE_DEVICE             (0x00000002)
#define MPI2_EVENT_SAS_DISC_DS_LOOP_DETECTED                    (0x00000001)


/* SAS Broadcast Primitive Event data */

typedef struct _MPI2_EVENT_DATA_SAS_BROADCAST_PRIMITIVE
{
    U8                      PhyNum;                     /* 0x00 */
    U8                      Port;                       /* 0x01 */
    U8                      PortWidth;                  /* 0x02 */
    U8                      Primitive;                  /* 0x03 */
} MPI2_EVENT_DATA_SAS_BROADCAST_PRIMITIVE,
  MPI2_POINTER PTR_MPI2_EVENT_DATA_SAS_BROADCAST_PRIMITIVE,
  Mpi2EventDataSasBroadcastPrimitive_t,
  MPI2_POINTER pMpi2EventDataSasBroadcastPrimitive_t;

/* defines for the Primitive field */
#define MPI2_EVENT_PRIMITIVE_CHANGE                         (0x01)
#define MPI2_EVENT_PRIMITIVE_SES                            (0x02)
#define MPI2_EVENT_PRIMITIVE_EXPANDER                       (0x03)
#define MPI2_EVENT_PRIMITIVE_ASYNCHRONOUS_EVENT             (0x04)
#define MPI2_EVENT_PRIMITIVE_RESERVED3                      (0x05)
#define MPI2_EVENT_PRIMITIVE_RESERVED4                      (0x06)
#define MPI2_EVENT_PRIMITIVE_CHANGE0_RESERVED               (0x07)
#define MPI2_EVENT_PRIMITIVE_CHANGE1_RESERVED               (0x08)


/* SAS Initiator Device Status Change Event data */

typedef struct _MPI2_EVENT_DATA_SAS_INIT_DEV_STATUS_CHANGE
{
    U8                      ReasonCode;                 /* 0x00 */
    U8                      PhysicalPort;               /* 0x01 */
    U16                     DevHandle;                  /* 0x02 */
    U64                     SASAddress;                 /* 0x04 */
} MPI2_EVENT_DATA_SAS_INIT_DEV_STATUS_CHANGE,
  MPI2_POINTER PTR_MPI2_EVENT_DATA_SAS_INIT_DEV_STATUS_CHANGE,
  Mpi2EventDataSasInitDevStatusChange_t,
  MPI2_POINTER pMpi2EventDataSasInitDevStatusChange_t;

/* SAS Initiator Device Status Change event ReasonCode values */
#define MPI2_EVENT_SAS_INIT_RC_ADDED                (0x01)
#define MPI2_EVENT_SAS_INIT_RC_NOT_RESPONDING       (0x02)


/* SAS Initiator Device Table Overflow Event data */

typedef struct _MPI2_EVENT_DATA_SAS_INIT_TABLE_OVERFLOW
{
    U16                     MaxInit;                    /* 0x00 */
    U16                     CurrentInit;                /* 0x02 */
    U64                     SASAddress;                 /* 0x04 */
} MPI2_EVENT_DATA_SAS_INIT_TABLE_OVERFLOW,
  MPI2_POINTER PTR_MPI2_EVENT_DATA_SAS_INIT_TABLE_OVERFLOW,
  Mpi2EventDataSasInitTableOverflow_t,
  MPI2_POINTER pMpi2EventDataSasInitTableOverflow_t;


/* SAS Topology Change List Event data */

/*
 * Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 * one and check NumEntries at runtime.
 */
#ifndef MPI2_EVENT_SAS_TOPO_PHY_COUNT
#define MPI2_EVENT_SAS_TOPO_PHY_COUNT           (1)
#endif

typedef struct _MPI2_EVENT_SAS_TOPO_PHY_ENTRY
{
    U16                     AttachedDevHandle;          /* 0x00 */
    U8                      LinkRate;                   /* 0x02 */
    U8                      PhyStatus;                  /* 0x03 */
} MPI2_EVENT_SAS_TOPO_PHY_ENTRY, MPI2_POINTER PTR_MPI2_EVENT_SAS_TOPO_PHY_ENTRY,
  Mpi2EventSasTopoPhyEntry_t, MPI2_POINTER pMpi2EventSasTopoPhyEntry_t;

typedef struct _MPI2_EVENT_DATA_SAS_TOPOLOGY_CHANGE_LIST
{
    U16                             EnclosureHandle;            /* 0x00 */
    U16                             ExpanderDevHandle;          /* 0x02 */
    U8                              NumPhys;                    /* 0x04 */
    U8                              Reserved1;                  /* 0x05 */
    U16                             Reserved2;                  /* 0x06 */
    U8                              NumEntries;                 /* 0x08 */
    U8                              StartPhyNum;                /* 0x09 */
    U8                              ExpStatus;                  /* 0x0A */
    U8                              PhysicalPort;               /* 0x0B */
    MPI2_EVENT_SAS_TOPO_PHY_ENTRY   PHY[MPI2_EVENT_SAS_TOPO_PHY_COUNT]; /* 0x0C*/
} MPI2_EVENT_DATA_SAS_TOPOLOGY_CHANGE_LIST,
  MPI2_POINTER PTR_MPI2_EVENT_DATA_SAS_TOPOLOGY_CHANGE_LIST,
  Mpi2EventDataSasTopologyChangeList_t,
  MPI2_POINTER pMpi2EventDataSasTopologyChangeList_t;

/* values for the ExpStatus field */
#define MPI2_EVENT_SAS_TOPO_ES_NO_EXPANDER                  (0x00)
#define MPI2_EVENT_SAS_TOPO_ES_ADDED                        (0x01)
#define MPI2_EVENT_SAS_TOPO_ES_NOT_RESPONDING               (0x02)
#define MPI2_EVENT_SAS_TOPO_ES_RESPONDING                   (0x03)
#define MPI2_EVENT_SAS_TOPO_ES_DELAY_NOT_RESPONDING         (0x04)

/* defines for the LinkRate field */
#define MPI2_EVENT_SAS_TOPO_LR_CURRENT_MASK                 (0xF0)
#define MPI2_EVENT_SAS_TOPO_LR_CURRENT_SHIFT                (4)
#define MPI2_EVENT_SAS_TOPO_LR_PREV_MASK                    (0x0F)
#define MPI2_EVENT_SAS_TOPO_LR_PREV_SHIFT                   (0)

#define MPI2_EVENT_SAS_TOPO_LR_UNKNOWN_LINK_RATE            (0x00)
#define MPI2_EVENT_SAS_TOPO_LR_PHY_DISABLED                 (0x01)
#define MPI2_EVENT_SAS_TOPO_LR_NEGOTIATION_FAILED           (0x02)
#define MPI2_EVENT_SAS_TOPO_LR_SATA_OOB_COMPLETE            (0x03)
#define MPI2_EVENT_SAS_TOPO_LR_PORT_SELECTOR                (0x04)
#define MPI2_EVENT_SAS_TOPO_LR_SMP_RESET_IN_PROGRESS        (0x05)
#define MPI2_EVENT_SAS_TOPO_LR_UNSUPPORTED_PHY              (0x06)
#define MPI2_EVENT_SAS_TOPO_LR_RATE_1_5                     (0x08)
#define MPI2_EVENT_SAS_TOPO_LR_RATE_3_0                     (0x09)
#define MPI2_EVENT_SAS_TOPO_LR_RATE_6_0                     (0x0A)

/* values for the PhyStatus field */
#define MPI2_EVENT_SAS_TOPO_PHYSTATUS_VACANT                (0x80)
#define MPI2_EVENT_SAS_TOPO_PS_MULTIPLEX_CHANGE             (0x10)
/* values for the PhyStatus ReasonCode sub-field */
#define MPI2_EVENT_SAS_TOPO_RC_MASK                         (0x0F)
#define MPI2_EVENT_SAS_TOPO_RC_TARG_ADDED                   (0x01)
#define MPI2_EVENT_SAS_TOPO_RC_TARG_NOT_RESPONDING          (0x02)
#define MPI2_EVENT_SAS_TOPO_RC_PHY_CHANGED                  (0x03)
#define MPI2_EVENT_SAS_TOPO_RC_NO_CHANGE                    (0x04)
#define MPI2_EVENT_SAS_TOPO_RC_DELAY_NOT_RESPONDING         (0x05)


/* SAS Enclosure Device Status Change Event data */

typedef struct _MPI2_EVENT_DATA_SAS_ENCL_DEV_STATUS_CHANGE
{
    U16                     EnclosureHandle;            /* 0x00 */
    U8                      ReasonCode;                 /* 0x02 */
    U8                      PhysicalPort;               /* 0x03 */
    U64                     EnclosureLogicalID;         /* 0x04 */
    U16                     NumSlots;                   /* 0x0C */
    U16                     StartSlot;                  /* 0x0E */
    U32                     PhyBits;                    /* 0x10 */
} MPI2_EVENT_DATA_SAS_ENCL_DEV_STATUS_CHANGE,
  MPI2_POINTER PTR_MPI2_EVENT_DATA_SAS_ENCL_DEV_STATUS_CHANGE,
  Mpi2EventDataSasEnclDevStatusChange_t,
  MPI2_POINTER pMpi2EventDataSasEnclDevStatusChange_t;

/* SAS Enclosure Device Status Change event ReasonCode values */
#define MPI2_EVENT_SAS_ENCL_RC_ADDED                (0x01)
#define MPI2_EVENT_SAS_ENCL_RC_NOT_RESPONDING       (0x02)


/* SAS PHY Counter Event data */

typedef struct _MPI2_EVENT_DATA_SAS_PHY_COUNTER {
    U64         TimeStamp;          /* 0x00 */
    U32         Reserved1;          /* 0x08 */
    U8          PhyEventCode;       /* 0x0C */
    U8          PhyNum;             /* 0x0D */
    U16         Reserved2;          /* 0x0E */
    U32         PhyEventInfo;       /* 0x10 */
    U8          CounterType;        /* 0x14 */
    U8          ThresholdWindow;    /* 0x15 */
    U8          TimeUnits;          /* 0x16 */
    U8          Reserved3;          /* 0x17 */
    U32         EventThreshold;     /* 0x18 */
    U16         ThresholdFlags;     /* 0x1C */
    U16         Reserved4;          /* 0x1E */
} MPI2_EVENT_DATA_SAS_PHY_COUNTER,
  MPI2_POINTER PTR_MPI2_EVENT_DATA_SAS_PHY_COUNTER,
  Mpi2EventDataSasPhyCounter_t, MPI2_POINTER pMpi2EventDataSasPhyCounter_t;

/* use MPI2_SASPHY3_EVENT_CODE_ values from mpi2_cnfg.h for the
 * PhyEventCode field
 * use MPI2_SASPHY3_COUNTER_TYPE_ values from mpi2_cnfg.h for the
 * CounterType field
 * use MPI2_SASPHY3_TIME_UNITS_ values from mpi2_cnfg.h for the
 * TimeUnits field
 * use MPI2_SASPHY3_TFLAGS_ values from mpi2_cnfg.h for the
 * ThresholdFlags field
 * */


/* SAS Quiesce Event data */

typedef struct _MPI2_EVENT_DATA_SAS_QUIESCE {
    U8                      ReasonCode;                 /* 0x00 */
    U8                      Reserved1;                  /* 0x01 */
    U16                     Reserved2;                  /* 0x02 */
    U32                     Reserved3;                  /* 0x04 */
} MPI2_EVENT_DATA_SAS_QUIESCE,
  MPI2_POINTER PTR_MPI2_EVENT_DATA_SAS_QUIESCE,
  Mpi2EventDataSasQuiesce_t, MPI2_POINTER pMpi2EventDataSasQuiesce_t;

/* SAS Quiesce Event data ReasonCode values */
#define MPI2_EVENT_SAS_QUIESCE_RC_STARTED                   (0x01)
#define MPI2_EVENT_SAS_QUIESCE_RC_COMPLETED                 (0x02)


/* Host Based Discovery Phy Event data */

typedef struct _MPI2_EVENT_HBD_PHY_SAS {
    U8          Flags;                      /* 0x00 */
    U8          NegotiatedLinkRate;         /* 0x01 */
    U8          PhyNum;                     /* 0x02 */
    U8          PhysicalPort;               /* 0x03 */
    U32         Reserved1;                  /* 0x04 */
    U8          InitialFrame[28];           /* 0x08 */
} MPI2_EVENT_HBD_PHY_SAS, MPI2_POINTER PTR_MPI2_EVENT_HBD_PHY_SAS,
  Mpi2EventHbdPhySas_t, MPI2_POINTER pMpi2EventHbdPhySas_t;

/* values for the Flags field */
#define MPI2_EVENT_HBD_SAS_FLAGS_FRAME_VALID        (0x02)
#define MPI2_EVENT_HBD_SAS_FLAGS_SATA_FRAME         (0x01)

/* use MPI2_SAS_NEG_LINK_RATE_ defines from mpi2_cnfg.h for
 * the NegotiatedLinkRate field */

typedef union _MPI2_EVENT_HBD_DESCRIPTOR {
    MPI2_EVENT_HBD_PHY_SAS      Sas;
} MPI2_EVENT_HBD_DESCRIPTOR, MPI2_POINTER PTR_MPI2_EVENT_HBD_DESCRIPTOR,
  Mpi2EventHbdDescriptor_t, MPI2_POINTER pMpi2EventHbdDescriptor_t;

typedef struct _MPI2_EVENT_DATA_HBD_PHY {
    U8                          DescriptorType;     /* 0x00 */
    U8                          Reserved1;          /* 0x01 */
    U16                         Reserved2;          /* 0x02 */
    U32                         Reserved3;          /* 0x04 */
    MPI2_EVENT_HBD_DESCRIPTOR   Descriptor;         /* 0x08 */
} MPI2_EVENT_DATA_HBD_PHY, MPI2_POINTER PTR_MPI2_EVENT_DATA_HBD_PHY,
  Mpi2EventDataHbdPhy_t, MPI2_POINTER pMpi2EventDataMpi2EventDataHbdPhy_t;

/* values for the DescriptorType field */
#define MPI2_EVENT_HBD_DT_SAS               (0x01)



/****************************************************************************
*  EventAck message
****************************************************************************/

/* EventAck Request message */
typedef struct _MPI2_EVENT_ACK_REQUEST
{
    U16                     Reserved1;                      /* 0x00 */
    U8                      ChainOffset;                    /* 0x02 */
    U8                      Function;                       /* 0x03 */
    U16                     Reserved2;                      /* 0x04 */
    U8                      Reserved3;                      /* 0x06 */
    U8                      MsgFlags;                       /* 0x07 */
    U8                      VP_ID;                          /* 0x08 */
    U8                      VF_ID;                          /* 0x09 */
    U16                     Reserved4;                      /* 0x0A */
    U16                     Event;                          /* 0x0C */
    U16                     Reserved5;                      /* 0x0E */
    U32                     EventContext;                   /* 0x10 */
} MPI2_EVENT_ACK_REQUEST, MPI2_POINTER PTR_MPI2_EVENT_ACK_REQUEST,
  Mpi2EventAckRequest_t, MPI2_POINTER pMpi2EventAckRequest_t;


/* EventAck Reply message */
typedef struct _MPI2_EVENT_ACK_REPLY
{
    U16                     Reserved1;                      /* 0x00 */
    U8                      MsgLength;                      /* 0x02 */
    U8                      Function;                       /* 0x03 */
    U16                     Reserved2;                      /* 0x04 */
    U8                      Reserved3;                      /* 0x06 */
    U8                      MsgFlags;                       /* 0x07 */
    U8                      VP_ID;                          /* 0x08 */
    U8                      VF_ID;                          /* 0x09 */
    U16                     Reserved4;                      /* 0x0A */
    U16                     Reserved5;                      /* 0x0C */
    U16                     IOCStatus;                      /* 0x0E */
    U32                     IOCLogInfo;                     /* 0x10 */
} MPI2_EVENT_ACK_REPLY, MPI2_POINTER PTR_MPI2_EVENT_ACK_REPLY,
  Mpi2EventAckReply_t, MPI2_POINTER pMpi2EventAckReply_t;


/****************************************************************************
*  FWDownload message
****************************************************************************/

/* FWDownload Request message */
typedef struct _MPI2_FW_DOWNLOAD_REQUEST
{
    U8                      ImageType;                  /* 0x00 */
    U8                      Reserved1;                  /* 0x01 */
    U8                      ChainOffset;                /* 0x02 */
    U8                      Function;                   /* 0x03 */
    U16                     Reserved2;                  /* 0x04 */
    U8                      Reserved3;                  /* 0x06 */
    U8                      MsgFlags;                   /* 0x07 */
    U8                      VP_ID;                      /* 0x08 */
    U8                      VF_ID;                      /* 0x09 */
    U16                     Reserved4;                  /* 0x0A */
    U32                     TotalImageSize;             /* 0x0C */
    U32                     Reserved5;                  /* 0x10 */
    MPI2_MPI_SGE_UNION      SGL;                        /* 0x14 */
} MPI2_FW_DOWNLOAD_REQUEST, MPI2_POINTER PTR_MPI2_FW_DOWNLOAD_REQUEST,
  Mpi2FWDownloadRequest, MPI2_POINTER pMpi2FWDownloadRequest;

#define MPI2_FW_DOWNLOAD_MSGFLGS_LAST_SEGMENT   (0x01)

#define MPI2_FW_DOWNLOAD_ITYPE_FW                   (0x01)
#define MPI2_FW_DOWNLOAD_ITYPE_BIOS                 (0x02)
#define MPI2_FW_DOWNLOAD_ITYPE_MANUFACTURING        (0x06)
#define MPI2_FW_DOWNLOAD_ITYPE_CONFIG_1             (0x07)
#define MPI2_FW_DOWNLOAD_ITYPE_CONFIG_2             (0x08)
#define MPI2_FW_DOWNLOAD_ITYPE_MEGARAID             (0x09)
#define MPI2_FW_DOWNLOAD_ITYPE_COMPLETE             (0x0A)
#define MPI2_FW_DOWNLOAD_ITYPE_COMMON_BOOT_BLOCK    (0x0B)

/* FWDownload TransactionContext Element */
typedef struct _MPI2_FW_DOWNLOAD_TCSGE
{
    U8                      Reserved1;                  /* 0x00 */
    U8                      ContextSize;                /* 0x01 */
    U8                      DetailsLength;              /* 0x02 */
    U8                      Flags;                      /* 0x03 */
    U32                     Reserved2;                  /* 0x04 */
    U32                     ImageOffset;                /* 0x08 */
    U32                     ImageSize;                  /* 0x0C */
} MPI2_FW_DOWNLOAD_TCSGE, MPI2_POINTER PTR_MPI2_FW_DOWNLOAD_TCSGE,
  Mpi2FWDownloadTCSGE_t, MPI2_POINTER pMpi2FWDownloadTCSGE_t;

/* FWDownload Reply message */
typedef struct _MPI2_FW_DOWNLOAD_REPLY
{
    U8                      ImageType;                  /* 0x00 */
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
} MPI2_FW_DOWNLOAD_REPLY, MPI2_POINTER PTR_MPI2_FW_DOWNLOAD_REPLY,
  Mpi2FWDownloadReply_t, MPI2_POINTER pMpi2FWDownloadReply_t;


/****************************************************************************
*  FWUpload message
****************************************************************************/

/* FWUpload Request message */
typedef struct _MPI2_FW_UPLOAD_REQUEST
{
    U8                      ImageType;                  /* 0x00 */
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
    MPI2_MPI_SGE_UNION      SGL;                        /* 0x14 */
} MPI2_FW_UPLOAD_REQUEST, MPI2_POINTER PTR_MPI2_FW_UPLOAD_REQUEST,
  Mpi2FWUploadRequest_t, MPI2_POINTER pMpi2FWUploadRequest_t;

#define MPI2_FW_UPLOAD_ITYPE_FW_CURRENT         (0x00)
#define MPI2_FW_UPLOAD_ITYPE_FW_FLASH           (0x01)
#define MPI2_FW_UPLOAD_ITYPE_BIOS_FLASH         (0x02)
#define MPI2_FW_UPLOAD_ITYPE_FW_BACKUP          (0x05)
#define MPI2_FW_UPLOAD_ITYPE_MANUFACTURING      (0x06)
#define MPI2_FW_UPLOAD_ITYPE_CONFIG_1           (0x07)
#define MPI2_FW_UPLOAD_ITYPE_CONFIG_2           (0x08)
#define MPI2_FW_UPLOAD_ITYPE_MEGARAID           (0x09)
#define MPI2_FW_UPLOAD_ITYPE_COMPLETE           (0x0A)
#define MPI2_FW_UPLOAD_ITYPE_COMMON_BOOT_BLOCK  (0x0B)

typedef struct _MPI2_FW_UPLOAD_TCSGE
{
    U8                      Reserved1;                  /* 0x00 */
    U8                      ContextSize;                /* 0x01 */
    U8                      DetailsLength;              /* 0x02 */
    U8                      Flags;                      /* 0x03 */
    U32                     Reserved2;                  /* 0x04 */
    U32                     ImageOffset;                /* 0x08 */
    U32                     ImageSize;                  /* 0x0C */
} MPI2_FW_UPLOAD_TCSGE, MPI2_POINTER PTR_MPI2_FW_UPLOAD_TCSGE,
  Mpi2FWUploadTCSGE_t, MPI2_POINTER pMpi2FWUploadTCSGE_t;

/* FWUpload Reply message */
typedef struct _MPI2_FW_UPLOAD_REPLY
{
    U8                      ImageType;                  /* 0x00 */
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
    U32                     ActualImageSize;            /* 0x14 */
} MPI2_FW_UPLOAD_REPLY, MPI2_POINTER PTR_MPI2_FW_UPLOAD_REPLY,
  Mpi2FWUploadReply_t, MPI2_POINTER pMPi2FWUploadReply_t;


/* FW Image Header */
typedef struct _MPI2_FW_IMAGE_HEADER
{
    U32                     Signature;                  /* 0x00 */
    U32                     Signature0;                 /* 0x04 */
    U32                     Signature1;                 /* 0x08 */
    U32                     Signature2;                 /* 0x0C */
    MPI2_VERSION_UNION      MPIVersion;                 /* 0x10 */
    MPI2_VERSION_UNION      FWVersion;                  /* 0x14 */
    MPI2_VERSION_UNION      NVDATAVersion;              /* 0x18 */
    MPI2_VERSION_UNION      PackageVersion;             /* 0x1C */
    U16                     VendorID;                   /* 0x20 */
    U16                     ProductID;                  /* 0x22 */
    U16                     ProtocolFlags;              /* 0x24 */
    U16                     Reserved26;                 /* 0x26 */
    U32                     IOCCapabilities;            /* 0x28 */
    U32                     ImageSize;                  /* 0x2C */
    U32                     NextImageHeaderOffset;      /* 0x30 */
    U32                     Checksum;                   /* 0x34 */
    U32                     Reserved38;                 /* 0x38 */
    U32                     Reserved3C;                 /* 0x3C */
    U32                     Reserved40;                 /* 0x40 */
    U32                     Reserved44;                 /* 0x44 */
    U32                     Reserved48;                 /* 0x48 */
    U32                     Reserved4C;                 /* 0x4C */
    U32                     Reserved50;                 /* 0x50 */
    U32                     Reserved54;                 /* 0x54 */
    U32                     Reserved58;                 /* 0x58 */
    U32                     Reserved5C;                 /* 0x5C */
    U32                     Reserved60;                 /* 0x60 */
    U32                     FirmwareVersionNameWhat;    /* 0x64 */
    U8                      FirmwareVersionName[32];    /* 0x68 */
    U32                     VendorNameWhat;             /* 0x88 */
    U8                      VendorName[32];             /* 0x8C */
    U32                     PackageNameWhat;            /* 0x88 */
    U8                      PackageName[32];            /* 0x8C */
    U32                     ReservedD0;                 /* 0xD0 */
    U32                     ReservedD4;                 /* 0xD4 */
    U32                     ReservedD8;                 /* 0xD8 */
    U32                     ReservedDC;                 /* 0xDC */
    U32                     ReservedE0;                 /* 0xE0 */
    U32                     ReservedE4;                 /* 0xE4 */
    U32                     ReservedE8;                 /* 0xE8 */
    U32                     ReservedEC;                 /* 0xEC */
    U32                     ReservedF0;                 /* 0xF0 */
    U32                     ReservedF4;                 /* 0xF4 */
    U32                     ReservedF8;                 /* 0xF8 */
    U32                     ReservedFC;                 /* 0xFC */
} MPI2_FW_IMAGE_HEADER, MPI2_POINTER PTR_MPI2_FW_IMAGE_HEADER,
  Mpi2FWImageHeader_t, MPI2_POINTER pMpi2FWImageHeader_t;

/* Signature field */
#define MPI2_FW_HEADER_SIGNATURE_OFFSET         (0x00)
#define MPI2_FW_HEADER_SIGNATURE_MASK           (0xFF000000)
#define MPI2_FW_HEADER_SIGNATURE                (0xEA000000)

/* Signature0 field */
#define MPI2_FW_HEADER_SIGNATURE0_OFFSET        (0x04)
#define MPI2_FW_HEADER_SIGNATURE0               (0x5AFAA55A)

/* Signature1 field */
#define MPI2_FW_HEADER_SIGNATURE1_OFFSET        (0x08)
#define MPI2_FW_HEADER_SIGNATURE1               (0xA55AFAA5)

/* Signature2 field */
#define MPI2_FW_HEADER_SIGNATURE2_OFFSET        (0x0C)
#define MPI2_FW_HEADER_SIGNATURE2               (0x5AA55AFA)


/* defines for using the ProductID field */
#define MPI2_FW_HEADER_PID_TYPE_MASK            (0xF000)
#define MPI2_FW_HEADER_PID_TYPE_SAS             (0x2000)

#define MPI2_FW_HEADER_PID_PROD_MASK                    (0x0F00)
#define MPI2_FW_HEADER_PID_PROD_A                       (0x0000)
#define MPI2_FW_HEADER_PID_PROD_TARGET_INITIATOR_SCSI   (0x0200)
#define MPI2_FW_HEADER_PID_PROD_IR_SCSI                 (0x0700)


#define MPI2_FW_HEADER_PID_FAMILY_MASK          (0x00FF)
/* SAS */
#define MPI2_FW_HEADER_PID_FAMILY_2108_SAS      (0x0013)
#define MPI2_FW_HEADER_PID_FAMILY_2208_SAS      (0x0014)

/* use MPI2_IOCFACTS_PROTOCOL_ defines for ProtocolFlags field */

/* use MPI2_IOCFACTS_CAPABILITY_ defines for IOCCapabilities field */


#define MPI2_FW_HEADER_IMAGESIZE_OFFSET         (0x2C)
#define MPI2_FW_HEADER_NEXTIMAGE_OFFSET         (0x30)
#define MPI2_FW_HEADER_VERNMHWAT_OFFSET         (0x64)

#define MPI2_FW_HEADER_WHAT_SIGNATURE           (0x29232840)

#define MPI2_FW_HEADER_SIZE                     (0x100)


/* Extended Image Header */
typedef struct _MPI2_EXT_IMAGE_HEADER

{
    U8                      ImageType;                  /* 0x00 */
    U8                      Reserved1;                  /* 0x01 */
    U16                     Reserved2;                  /* 0x02 */
    U32                     Checksum;                   /* 0x04 */
    U32                     ImageSize;                  /* 0x08 */
    U32                     NextImageHeaderOffset;      /* 0x0C */
    U32                     PackageVersion;             /* 0x10 */
    U32                     Reserved3;                  /* 0x14 */
    U32                     Reserved4;                  /* 0x18 */
    U32                     Reserved5;                  /* 0x1C */
    U8                      IdentifyString[32];         /* 0x20 */
} MPI2_EXT_IMAGE_HEADER, MPI2_POINTER PTR_MPI2_EXT_IMAGE_HEADER,
  Mpi2ExtImageHeader_t, MPI2_POINTER pMpi2ExtImageHeader_t;

/* useful offsets */
#define MPI2_EXT_IMAGE_IMAGETYPE_OFFSET         (0x00)
#define MPI2_EXT_IMAGE_IMAGESIZE_OFFSET         (0x08)
#define MPI2_EXT_IMAGE_NEXTIMAGE_OFFSET         (0x0C)

#define MPI2_EXT_IMAGE_HEADER_SIZE              (0x40)

/* defines for the ImageType field */
#define MPI2_EXT_IMAGE_TYPE_UNSPECIFIED         (0x00)
#define MPI2_EXT_IMAGE_TYPE_FW                  (0x01)
#define MPI2_EXT_IMAGE_TYPE_NVDATA              (0x03)
#define MPI2_EXT_IMAGE_TYPE_BOOTLOADER          (0x04)
#define MPI2_EXT_IMAGE_TYPE_INITIALIZATION      (0x05)
#define MPI2_EXT_IMAGE_TYPE_FLASH_LAYOUT        (0x06)
#define MPI2_EXT_IMAGE_TYPE_SUPPORTED_DEVICES   (0x07)
#define MPI2_EXT_IMAGE_TYPE_MEGARAID            (0x08)

#define MPI2_EXT_IMAGE_TYPE_MAX                 (MPI2_EXT_IMAGE_TYPE_MEGARAID)



/* FLASH Layout Extended Image Data */

/*
 * Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 * one and check RegionsPerLayout at runtime.
 */
#ifndef MPI2_FLASH_NUMBER_OF_REGIONS
#define MPI2_FLASH_NUMBER_OF_REGIONS        (1)
#endif

/*
 * Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 * one and check NumberOfLayouts at runtime.
 */
#ifndef MPI2_FLASH_NUMBER_OF_LAYOUTS
#define MPI2_FLASH_NUMBER_OF_LAYOUTS        (1)
#endif

typedef struct _MPI2_FLASH_REGION
{
    U8                      RegionType;                 /* 0x00 */
    U8                      Reserved1;                  /* 0x01 */
    U16                     Reserved2;                  /* 0x02 */
    U32                     RegionOffset;               /* 0x04 */
    U32                     RegionSize;                 /* 0x08 */
    U32                     Reserved3;                  /* 0x0C */
} MPI2_FLASH_REGION, MPI2_POINTER PTR_MPI2_FLASH_REGION,
  Mpi2FlashRegion_t, MPI2_POINTER pMpi2FlashRegion_t;

typedef struct _MPI2_FLASH_LAYOUT
{
    U32                     FlashSize;                  /* 0x00 */
    U32                     Reserved1;                  /* 0x04 */
    U32                     Reserved2;                  /* 0x08 */
    U32                     Reserved3;                  /* 0x0C */
    MPI2_FLASH_REGION       Region[MPI2_FLASH_NUMBER_OF_REGIONS];/* 0x10 */
} MPI2_FLASH_LAYOUT, MPI2_POINTER PTR_MPI2_FLASH_LAYOUT,
  Mpi2FlashLayout_t, MPI2_POINTER pMpi2FlashLayout_t;

typedef struct _MPI2_FLASH_LAYOUT_DATA
{
    U8                      ImageRevision;              /* 0x00 */
    U8                      Reserved1;                  /* 0x01 */
    U8                      SizeOfRegion;               /* 0x02 */
    U8                      Reserved2;                  /* 0x03 */
    U16                     NumberOfLayouts;            /* 0x04 */
    U16                     RegionsPerLayout;           /* 0x06 */
    U16                     MinimumSectorAlignment;     /* 0x08 */
    U16                     Reserved3;                  /* 0x0A */
    U32                     Reserved4;                  /* 0x0C */
    MPI2_FLASH_LAYOUT       Layout[MPI2_FLASH_NUMBER_OF_LAYOUTS];/* 0x10 */
} MPI2_FLASH_LAYOUT_DATA, MPI2_POINTER PTR_MPI2_FLASH_LAYOUT_DATA,
  Mpi2FlashLayoutData_t, MPI2_POINTER pMpi2FlashLayoutData_t;

/* defines for the RegionType field */
#define MPI2_FLASH_REGION_UNUSED                (0x00)
#define MPI2_FLASH_REGION_FIRMWARE              (0x01)
#define MPI2_FLASH_REGION_BIOS                  (0x02)
#define MPI2_FLASH_REGION_NVDATA                (0x03)
#define MPI2_FLASH_REGION_FIRMWARE_BACKUP       (0x05)
#define MPI2_FLASH_REGION_MFG_INFORMATION       (0x06)
#define MPI2_FLASH_REGION_CONFIG_1              (0x07)
#define MPI2_FLASH_REGION_CONFIG_2              (0x08)
#define MPI2_FLASH_REGION_MEGARAID              (0x09)
#define MPI2_FLASH_REGION_INIT                  (0x0A)

/* ImageRevision */
#define MPI2_FLASH_LAYOUT_IMAGE_REVISION        (0x00)



/* Supported Devices Extended Image Data */

/*
 * Host code (drivers, BIOS, utilities, etc.) should leave this define set to
 * one and check NumberOfDevices at runtime.
 */
#ifndef MPI2_SUPPORTED_DEVICES_IMAGE_NUM_DEVICES
#define MPI2_SUPPORTED_DEVICES_IMAGE_NUM_DEVICES    (1)
#endif

typedef struct _MPI2_SUPPORTED_DEVICE
{
    U16                     DeviceID;                   /* 0x00 */
    U16                     VendorID;                   /* 0x02 */
    U16                     DeviceIDMask;               /* 0x04 */
    U16                     Reserved1;                  /* 0x06 */
    U8                      LowPCIRev;                  /* 0x08 */
    U8                      HighPCIRev;                 /* 0x09 */
    U16                     Reserved2;                  /* 0x0A */
    U32                     Reserved3;                  /* 0x0C */
} MPI2_SUPPORTED_DEVICE, MPI2_POINTER PTR_MPI2_SUPPORTED_DEVICE,
  Mpi2SupportedDevice_t, MPI2_POINTER pMpi2SupportedDevice_t;

typedef struct _MPI2_SUPPORTED_DEVICES_DATA
{
    U8                      ImageRevision;              /* 0x00 */
    U8                      Reserved1;                  /* 0x01 */
    U8                      NumberOfDevices;            /* 0x02 */
    U8                      Reserved2;                  /* 0x03 */
    U32                     Reserved3;                  /* 0x04 */
    MPI2_SUPPORTED_DEVICE   SupportedDevice[MPI2_SUPPORTED_DEVICES_IMAGE_NUM_DEVICES]; /* 0x08 */
} MPI2_SUPPORTED_DEVICES_DATA, MPI2_POINTER PTR_MPI2_SUPPORTED_DEVICES_DATA,
  Mpi2SupportedDevicesData_t, MPI2_POINTER pMpi2SupportedDevicesData_t;

/* ImageRevision */
#define MPI2_SUPPORTED_DEVICES_IMAGE_REVISION   (0x00)


/* Init Extended Image Data */

typedef struct _MPI2_INIT_IMAGE_FOOTER

{
    U32                     BootFlags;                  /* 0x00 */
    U32                     ImageSize;                  /* 0x04 */
    U32                     Signature0;                 /* 0x08 */
    U32                     Signature1;                 /* 0x0C */
    U32                     Signature2;                 /* 0x10 */
    U32                     ResetVector;                /* 0x14 */
} MPI2_INIT_IMAGE_FOOTER, MPI2_POINTER PTR_MPI2_INIT_IMAGE_FOOTER,
  Mpi2InitImageFooter_t, MPI2_POINTER pMpi2InitImageFooter_t;

/* defines for the BootFlags field */
#define MPI2_INIT_IMAGE_BOOTFLAGS_OFFSET        (0x00)

/* defines for the ImageSize field */
#define MPI2_INIT_IMAGE_IMAGESIZE_OFFSET        (0x04)

/* defines for the Signature0 field */
#define MPI2_INIT_IMAGE_SIGNATURE0_OFFSET       (0x08)
#define MPI2_INIT_IMAGE_SIGNATURE0              (0x5AA55AEA)

/* defines for the Signature1 field */
#define MPI2_INIT_IMAGE_SIGNATURE1_OFFSET       (0x0C)
#define MPI2_INIT_IMAGE_SIGNATURE1              (0xA55AEAA5)

/* defines for the Signature2 field */
#define MPI2_INIT_IMAGE_SIGNATURE2_OFFSET       (0x10)
#define MPI2_INIT_IMAGE_SIGNATURE2              (0x5AEAA55A)

/* Signature fields as individual bytes */
#define MPI2_INIT_IMAGE_SIGNATURE_BYTE_0        (0xEA)
#define MPI2_INIT_IMAGE_SIGNATURE_BYTE_1        (0x5A)
#define MPI2_INIT_IMAGE_SIGNATURE_BYTE_2        (0xA5)
#define MPI2_INIT_IMAGE_SIGNATURE_BYTE_3        (0x5A)

#define MPI2_INIT_IMAGE_SIGNATURE_BYTE_4        (0xA5)
#define MPI2_INIT_IMAGE_SIGNATURE_BYTE_5        (0xEA)
#define MPI2_INIT_IMAGE_SIGNATURE_BYTE_6        (0x5A)
#define MPI2_INIT_IMAGE_SIGNATURE_BYTE_7        (0xA5)

#define MPI2_INIT_IMAGE_SIGNATURE_BYTE_8        (0x5A)
#define MPI2_INIT_IMAGE_SIGNATURE_BYTE_9        (0xA5)
#define MPI2_INIT_IMAGE_SIGNATURE_BYTE_A        (0xEA)
#define MPI2_INIT_IMAGE_SIGNATURE_BYTE_B        (0x5A)

/* defines for the ResetVector field */
#define MPI2_INIT_IMAGE_RESETVECTOR_OFFSET      (0x14)


/****************************************************************************
*  PowerManagementControl message
****************************************************************************/

/* PowerManagementControl Request message */
typedef struct _MPI2_PWR_MGMT_CONTROL_REQUEST {
    U8                      Feature;                    /* 0x00 */
    U8                      Reserved1;                  /* 0x01 */
    U8                      ChainOffset;                /* 0x02 */
    U8                      Function;                   /* 0x03 */
    U16                     Reserved2;                  /* 0x04 */
    U8                      Reserved3;                  /* 0x06 */
    U8                      MsgFlags;                   /* 0x07 */
    U8                      VP_ID;                      /* 0x08 */
    U8                      VF_ID;                      /* 0x09 */
    U16                     Reserved4;                  /* 0x0A */
    U8                      Parameter1;                 /* 0x0C */
    U8                      Parameter2;                 /* 0x0D */
    U8                      Parameter3;                 /* 0x0E */
    U8                      Parameter4;                 /* 0x0F */
    U32                     Reserved5;                  /* 0x10 */
    U32                     Reserved6;                  /* 0x14 */
} MPI2_PWR_MGMT_CONTROL_REQUEST, MPI2_POINTER PTR_MPI2_PWR_MGMT_CONTROL_REQUEST,
  Mpi2PwrMgmtControlRequest_t, MPI2_POINTER pMpi2PwrMgmtControlRequest_t;

/* defines for the Feature field */
#define MPI2_PM_CONTROL_FEATURE_DA_PHY_POWER_COND       (0x01)
#define MPI2_PM_CONTROL_FEATURE_PORT_WIDTH_MODULATION   (0x02)
#define MPI2_PM_CONTROL_FEATURE_PCIE_LINK               (0x03)
#define MPI2_PM_CONTROL_FEATURE_IOC_SPEED               (0x04)
#define MPI2_PM_CONTROL_FEATURE_MIN_PRODUCT_SPECIFIC    (0x80)
#define MPI2_PM_CONTROL_FEATURE_MAX_PRODUCT_SPECIFIC    (0xFF)

/* parameter usage for the MPI2_PM_CONTROL_FEATURE_DA_PHY_POWER_COND Feature */
/* Parameter1 contains a PHY number */
/* Parameter2 indicates power condition action using these defines */
#define MPI2_PM_CONTROL_PARAM2_PARTIAL                  (0x01)
#define MPI2_PM_CONTROL_PARAM2_SLUMBER                  (0x02)
#define MPI2_PM_CONTROL_PARAM2_EXIT_PWR_MGMT            (0x03)
/* Parameter3 and Parameter4 are reserved */

/* parameter usage for the MPI2_PM_CONTROL_FEATURE_PORT_WIDTH_MODULATION
 *  Feature */
/* Parameter1 contains SAS port width modulation group number */
/* Parameter2 indicates IOC action using these defines */
#define MPI2_PM_CONTROL_PARAM2_REQUEST_OWNERSHIP        (0x01)
#define MPI2_PM_CONTROL_PARAM2_CHANGE_MODULATION        (0x02)
#define MPI2_PM_CONTROL_PARAM2_RELINQUISH_OWNERSHIP     (0x03)
/* Parameter3 indicates desired modulation level using these defines */
#define MPI2_PM_CONTROL_PARAM3_25_PERCENT               (0x00)
#define MPI2_PM_CONTROL_PARAM3_50_PERCENT               (0x01)
#define MPI2_PM_CONTROL_PARAM3_75_PERCENT               (0x02)
#define MPI2_PM_CONTROL_PARAM3_100_PERCENT              (0x03)
/* Parameter4 is reserved */

/* parameter usage for the MPI2_PM_CONTROL_FEATURE_PCIE_LINK Feature */
/* Parameter1 indicates desired PCIe link speed using these defines */
#define MPI2_PM_CONTROL_PARAM1_PCIE_2_5_GBPS            (0x00)
#define MPI2_PM_CONTROL_PARAM1_PCIE_5_0_GBPS            (0x01)
#define MPI2_PM_CONTROL_PARAM1_PCIE_8_0_GBPS            (0x02)
/* Parameter2 indicates desired PCIe link width using these defines */
#define MPI2_PM_CONTROL_PARAM2_WIDTH_X1                 (0x01)
#define MPI2_PM_CONTROL_PARAM2_WIDTH_X2                 (0x02)
#define MPI2_PM_CONTROL_PARAM2_WIDTH_X4                 (0x04)
#define MPI2_PM_CONTROL_PARAM2_WIDTH_X8                 (0x08)
/* Parameter3 and Parameter4 are reserved */

/* parameter usage for the MPI2_PM_CONTROL_FEATURE_IOC_SPEED Feature */
/* Parameter1 indicates desired IOC hardware clock speed using these defines */
#define MPI2_PM_CONTROL_PARAM1_FULL_IOC_SPEED           (0x01)
#define MPI2_PM_CONTROL_PARAM1_HALF_IOC_SPEED           (0x02)
#define MPI2_PM_CONTROL_PARAM1_QUARTER_IOC_SPEED        (0x04)
#define MPI2_PM_CONTROL_PARAM1_EIGHTH_IOC_SPEED         (0x08)
/* Parameter2, Parameter3, and Parameter4 are reserved */


/* PowerManagementControl Reply message */
typedef struct _MPI2_PWR_MGMT_CONTROL_REPLY {
    U8                      Feature;                    /* 0x00 */
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
} MPI2_PWR_MGMT_CONTROL_REPLY, MPI2_POINTER PTR_MPI2_PWR_MGMT_CONTROL_REPLY,
  Mpi2PwrMgmtControlReply_t, MPI2_POINTER pMpi2PwrMgmtControlReply_t;


#endif

