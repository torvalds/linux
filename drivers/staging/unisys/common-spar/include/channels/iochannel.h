/* Copyright (C) 2010 - 2013 UNISYS CORPORATION */
/* All rights reserved. */
#ifndef __IOCHANNEL_H__
#define __IOCHANNEL_H__

/*
* Everything needed for IOPart-GuestPart communication is define in
* this file.  Note: Everything is OS-independent because this file is
* used by Windows, Linux and possible EFI drivers.  */


/*
* Communication flow between the IOPart and GuestPart uses the channel headers
* channel state.  The following states are currently being used:
*       UNINIT(All Zeroes), CHANNEL_ATTACHING, CHANNEL_ATTACHED, CHANNEL_OPENED
*
* additional states will be used later.  No locking is needed to switch between
* states due to the following rules:
*
*      1.  IOPart is only the only partition allowed to change from UNIT
*      2.  IOPart is only the only partition allowed to change from
*		CHANNEL_ATTACHING
*      3.  GuestPart is only the only partition allowed to change from
*		CHANNEL_ATTACHED
*
* The state changes are the following: IOPart sees the channel is in UNINIT,
*        UNINIT -> CHANNEL_ATTACHING (performed only by IOPart)
*        CHANNEL_ATTACHING -> CHANNEL_ATTACHED (performed only by IOPart)
*        CHANNEL_ATTACHED -> CHANNEL_OPENED (performed only by GuestPart)
*/

#include <linux/uuid.h>

#include "commontypes.h"
#include "vmcallinterface.h"

#define _ULTRA_CONTROLVM_CHANNEL_INLINE_
#include <linux/dma-direction.h>
#include "controlvmchannel.h"
#include "vbuschannel.h"
#undef _ULTRA_CONTROLVM_CHANNEL_INLINE_
#include "channel.h"

/*
 * CHANNEL Guids
 */

#include "channel_guid.h"

#define ULTRA_VHBA_CHANNEL_PROTOCOL_SIGNATURE ULTRA_CHANNEL_PROTOCOL_SIGNATURE
#define ULTRA_VNIC_CHANNEL_PROTOCOL_SIGNATURE ULTRA_CHANNEL_PROTOCOL_SIGNATURE
#define ULTRA_VSWITCH_CHANNEL_PROTOCOL_SIGNATURE \
	ULTRA_CHANNEL_PROTOCOL_SIGNATURE

/* Must increment these whenever you insert or delete fields within this channel
* struct.  Also increment whenever you change the meaning of fields within this
* channel struct so as to break pre-existing software.  Note that you can
* usually add fields to the END of the channel struct withOUT needing to
* increment this. */
#define ULTRA_VHBA_CHANNEL_PROTOCOL_VERSIONID 2
#define ULTRA_VNIC_CHANNEL_PROTOCOL_VERSIONID 2
#define ULTRA_VSWITCH_CHANNEL_PROTOCOL_VERSIONID 1

#define ULTRA_VHBA_CHANNEL_OK_CLIENT(pChannel, logCtx)			\
	(ULTRA_check_channel_client(pChannel, UltraVhbaChannelProtocolGuid, \
				    "vhba", MIN_IO_CHANNEL_SIZE,	\
				    ULTRA_VHBA_CHANNEL_PROTOCOL_VERSIONID, \
				    ULTRA_VHBA_CHANNEL_PROTOCOL_SIGNATURE, \
				    __FILE__, __LINE__, logCtx))
#define ULTRA_VHBA_CHANNEL_OK_SERVER(actualBytes, logCtx)		\
	(ULTRA_check_channel_server(UltraVhbaChannelProtocolGuid,	\
				    "vhba", MIN_IO_CHANNEL_SIZE, actualBytes, \
				    __FILE__, __LINE__, logCtx))
#define ULTRA_VNIC_CHANNEL_OK_CLIENT(pChannel, logCtx)			\
	(ULTRA_check_channel_client(pChannel, UltraVnicChannelProtocolGuid, \
				    "vnic", MIN_IO_CHANNEL_SIZE,	\
				    ULTRA_VNIC_CHANNEL_PROTOCOL_VERSIONID, \
				    ULTRA_VNIC_CHANNEL_PROTOCOL_SIGNATURE, \
				    __FILE__, __LINE__, logCtx))
#define ULTRA_VNIC_CHANNEL_OK_SERVER(actualBytes, logCtx)		\
	(ULTRA_check_channel_server(UltraVnicChannelProtocolGuid,	\
				    "vnic", MIN_IO_CHANNEL_SIZE, actualBytes, \
				    __FILE__, __LINE__, logCtx))
#define ULTRA_VSWITCH_CHANNEL_OK_CLIENT(pChannel, logCtx)		\
	(ULTRA_check_channel_client(pChannel, UltraVswitchChannelProtocolGuid, \
				    "vswitch", MIN_IO_CHANNEL_SIZE,	\
				    ULTRA_VSWITCH_CHANNEL_PROTOCOL_VERSIONID, \
				    ULTRA_VSWITCH_CHANNEL_PROTOCOL_SIGNATURE, \
				    __FILE__, __LINE__, logCtx))
#define ULTRA_VSWITCH_CHANNEL_OK_SERVER(actualBytes, logCtx)          \
	(ULTRA_check_channel_server(UltraVswitchChannelProtocolGuid,	\
				    "vswitch", MIN_IO_CHANNEL_SIZE,	\
				    actualBytes,		    \
				    __FILE__, __LINE__, logCtx))
/*
* Everything necessary to handle SCSI & NIC traffic between Guest Partition and
* IO Partition is defined below.  */


/*
* Defines and enums.
*/

#define MINNUM(a, b) (((a) < (b)) ? (a) : (b))
#define MAXNUM(a, b) (((a) > (b)) ? (a) : (b))

/* these define the two queues per data channel between iopart and
 * ioguestparts */
#define IOCHAN_TO_IOPART 0 /* used by ioguestpart to 'insert' signals to
			    * iopart */
#define IOCHAN_FROM_GUESTPART 0 /* used by iopart to 'remove' signals from
				 * ioguestpart - same queue as previous queue */

#define IOCHAN_TO_GUESTPART 1 /* used by iopart to 'insert' signals to
			       * ioguestpart */
#define IOCHAN_FROM_IOPART 1 /* used by ioguestpart to 'remove' signals from
			      * iopart - same queue as previous queue */

/* these define the two queues per control channel between controlpart and "its"
 * guests, which includes the iopart  */
#define CTRLCHAN_TO_CTRLGUESTPART 0 /* used by ctrlguestpart to 'insert' signals
				     * to ctrlpart */
#define CTLRCHAN_FROM_CTRLPART 0 /* used by ctrlpart to 'remove' signals from
				  * ctrlquestpart - same queue as previous
				  * queue */

#define CTRLCHAN_TO_CTRLPART 1 /* used by ctrlpart to 'insert' signals to
				* ctrlguestpart */
#define CTRLCHAN_FROM_CTRLGUESTPART 1 /* used by ctrguestpart to 'remove'
				       * signals from ctrlpart - same queue as
				       * previous queue */

/* these define the Event & Ack queues per control channel Events are generated
* by CTRLGUESTPART and sent to CTRLPART; Acks are generated by CTRLPART and sent
* to CTRLGUESTPART. */
#define CTRLCHAN_EVENT_TO_CTRLPART 2 /* used by ctrlguestpart to 'insert' Events
				      * to ctrlpart */
#define CTRLCHAN_EVENT_FROM_CTRLGUESTPART 2 /* used by ctrlpart to 'remove'
					     * Events from ctrlguestpart */

#define CTRLCHAN_ACK_TO_CTRLGUESTPART 3	/* used by ctrlpart to 'insert' Acks to
					 * ctrlguestpart */
#define CTRLCHAN_ACK_FROM_CTRLPART 3 /* used by ctrlguestpart to 'remove' Events
				      * from ctrlpart */

/* size of cdb - i.e., scsi cmnd */
#define MAX_CMND_SIZE 16

#define MAX_SENSE_SIZE 64

#define MAX_PHYS_INFO 64

/* Because GuestToGuestCopy is limited to 4KiB segments, and we have limited the
* Emulex Driver to 256 scatter list segments via the lpfc_sg_seg_cnt parameter
* to 256, the maximum I/O size is limited to 256 * 4 KiB = 1 MB */
#define MAX_IO_SIZE   (1024*1024)	/* 1 MB */

/* NOTE 1: lpfc defines its support for segments in
* #define LPFC_SG_SEG_CNT 64
*
* NOTE 2: In Linux, frags array in skb is currently allocated to be
* MAX_SKB_FRAGS size, which is 18 which is smaller than MAX_PHYS_INFO for
* now.  */

#ifndef MAX_SERIAL_NUM
#define MAX_SERIAL_NUM		32
#endif				/* MAX_SERIAL_NUM */

#define MAX_SCSI_BUSES		1
#define MAX_SCSI_TARGETS	8
#define MAX_SCSI_LUNS		16
#define MAX_SCSI_FROM_HOST	0xFFFFFFFF	/* Indicator to use Physical HBA
						 * SCSI Host value */

/* various types of network packets that can be sent in cmdrsp */
typedef enum { NET_RCV_POST = 0,	/* submit buffer to hold receiving
					 * incoming packet */
	/* virtnic -> uisnic */
	NET_RCV,		/* incoming packet received */
	/* uisnic -> virtpci */
	NET_XMIT,		/* for outgoing net packets      */
	/* virtnic -> uisnic */
	NET_XMIT_DONE,		/* outgoing packet xmitted */
	/* uisnic -> virtpci */
	NET_RCV_ENBDIS,		/* enable/disable packet reception */
	/* virtnic -> uisnic */
	NET_RCV_ENBDIS_ACK,	/* acknowledge enable/disable packet
				 * reception */
	/* uisnic -> virtnic */
	NET_RCV_PROMISC,	/* enable/disable promiscuous mode */
	/* virtnic -> uisnic */
	NET_CONNECT_STATUS,	/* indicate the loss or restoration of a network
				 * connection */
	/* uisnic -> virtnic */
	NET_MACADDR,		/* indicates the client has requested to update
				 * its MAC addr */
	NET_MACADDR_ACK,	/* MAC address  */

} NET_TYPES;

#define		ETH_HEADER_SIZE 14	/* size of ethernet header */

#define		ETH_MIN_DATA_SIZE 46	/* minimum eth data size */
#define		ETH_MIN_PACKET_SIZE (ETH_HEADER_SIZE + ETH_MIN_DATA_SIZE)

#define     ETH_DEF_DATA_SIZE 1500	/* default data size */
#define     ETH_DEF_PACKET_SIZE (ETH_HEADER_SIZE + ETH_DEF_DATA_SIZE)

#define		ETH_MAX_MTU 16384	/* maximum data size */

#ifndef MAX_MACADDR_LEN
#define MAX_MACADDR_LEN 6	/* number of bytes in MAC address */
#endif				/* MAX_MACADDR_LEN */

#define ETH_IS_LOCALLY_ADMINISTERED(Address) \
	(((U8 *) (Address))[0] & ((U8) 0x02))
#define NIC_VENDOR_ID 0x0008000B

/* various types of scsi task mgmt commands  */
typedef enum { TASK_MGMT_ABORT_TASK =
	    1, TASK_MGMT_BUS_RESET, TASK_MGMT_LUN_RESET,
	    TASK_MGMT_TARGET_RESET,
} TASK_MGMT_TYPES;

/* various types of vdisk mgmt commands  */
typedef enum { VDISK_MGMT_ACQUIRE = 1, VDISK_MGMT_RELEASE,
} VDISK_MGMT_TYPES;

/* this is used in the vdest field  */
#define VDEST_ALL 0xFFFF

#define MIN_NUMSIGNALS 64
#define MAX_NUMSIGNALS 4096

/* MAX_NET_RCV_BUF specifies the number of rcv buffers that are created by each
* guest's virtnic and posted to uisnic.  Uisnic, for each channel, keeps the rcv
* buffers posted and uses them to receive data on behalf of the guest's virtnic.
* NOTE: the num_rcv_bufs is configurable for each VNIC. So the following is
* simply an upperlimit on what each VNIC can provide.  Setting it to half of the
* NUMSIGNALS to prevent queue full deadlocks */
#define MAX_NET_RCV_BUFS (MIN_NUMSIGNALS / 2)

/*
 * structs with pragma pack  */


/* ///////////// BEGIN PRAGMA PACK PUSH 1 ///////////////////////// */
/* ///////////// ONLY STRUCT TYPE SHOULD BE BELOW */

#pragma pack(push, 1)

struct guest_phys_info {
	U64 address;
	U64 length;
};

#define GPI_ENTRIES_PER_PAGE (PAGE_SIZE / sizeof(struct guest_phys_info))

struct uisscsi_dest {
	U32 channel;		/* channel == bus number */
	U32 id;			/* id == target number */
	U32 lun;		/* lun == logical unit number */
};

struct vhba_wwnn {
	U32 wwnn1;
	U32 wwnn2;
};

/* WARNING: Values stired in this structure must contain maximum counts (not
 * maximum values). */
struct vhba_config_max {	/* 20 bytes */
	U32 max_channel;	/* maximum channel for devices attached to this
				 * bus */
	U32 max_id;		/* maximum SCSI ID for devices attached to this
				 * bus */
	U32 max_lun;		/* maximum SCSI LUN for devices attached to this
				 * bus */
	U32 cmd_per_lun;	/* maximum number of outstanding commands per
				 * lun that are allowed at one time */
	U32 max_io_size;	/* maximum io size for devices attached to this
				 * bus */
	/* max io size is often determined by the resource of the hba. e.g */
	/* max scatter gather list length * page size / sector size */
};

struct uiscmdrsp_scsi {
	void *scsicmd;		/* the handle to the cmd that was received -
				 * send it back as is in the rsp packet.  */
	U8 cmnd[MAX_CMND_SIZE];	/* the cdb for the command */
	U32 bufflen;		/* length of data to be transferred out or in */
	U16 guest_phys_entries;	/* Number of entries in scatter-gather (sg)
				 * list */
	struct guest_phys_info gpi_list[MAX_PHYS_INFO];	/* physical address
							 * information for each
							 * fragment */
	enum dma_data_direction  data_dir; /* direction of the data, if any */
	struct uisscsi_dest vdest;	/* identifies the virtual hba, id,
					 * channel, lun to which cmd was sent */

	    /* the following fields are needed to queue the rsp back to cmd
	     * originator */
	int linuxstat;		/* the original Linux status - for use by linux
				 * vdisk code */
	U8 scsistat;		/* the scsi status */
	U8 addlstat;		/* non-scsi status - covers cases like timeout
				 * needed by windows guests */
#define ADDL_RESET		1
#define ADDL_TIMEOUT		2
#define ADDL_INTERNAL_ERROR	3
#define ADDL_SEL_TIMEOUT	4
#define ADDL_CMD_TIMEOUT	5
#define ADDL_BAD_TARGET		6
#define ADDL_RETRY		7

	/* the following fields are need to determine the result of command */
	 U8 sensebuf[MAX_SENSE_SIZE];	/* sense info in case cmd failed; */
	/* it holds the sense_data struct; */
	/* see that struct for details. */
	void *vdisk; /* contains pointer to the vdisk so that we can clean up
		      * when the IO completes. */
	int no_disk_result;	/* used to return no disk inquiry result */
	/* when no_disk_result is set to 1,  */
	/* scsi.scsistat is SAM_STAT_GOOD */
	/* scsi.addlstat is 0 */
	/* scsi.linuxstat is SAM_STAT_GOOD */
	/* That is, there is NO error. */
};

/*
* Defines to support sending correct inquiry result when no disk is
* configured.  */

/* From SCSI SPC2 -
 *
 * If the target is not capable of supporting a device on this logical unit, the
 * device server shall set this field to 7Fh (PERIPHERAL QUALIFIER set to 011b
 * and PERIPHERAL DEVICE TYPE set to 1Fh).
 *
 *The device server is capable of supporting the specified peripheral device
 *type on this logical unit. However, the physical device is not currently
 *connected to this logical unit.
 */

#define DEV_NOT_PRESENT 0x7f	/* old name - compatibility */
#define DEV_NOT_CAPABLE 0x7f	/* peripheral qualifier of 0x3  */
    /* peripheral type of 0x1f */
    /* specifies no device but target present */

#define DEV_DISK_CAPABLE_NOT_PRESENT 0x20 /* peripheral qualifier of 0x1 */
    /* peripheral type of 0 - disk */
    /* specifies device capable, but not present */

#define DEV_PROC_CAPABLE_NOT_PRESENT 0x23 /* peripheral qualifier of 0x1 */
    /* peripheral type of 3 - processor */
    /* specifies device capable, but not present */

#define DEV_HISUPPORT 0x10;	/* HiSup = 1; shows support for report luns */
    /* must be returned for lun 0. */

/* NOTE: Linux code assumes inquiry contains 36 bytes. Without checking length
* in buf[4] some linux code accesses bytes beyond 5 to retrieve vendor, product
* & revision.  Yikes! So let us always send back 36 bytes, the minimum for
* inquiry result. */
#define NO_DISK_INQUIRY_RESULT_LEN 36

#define MIN_INQUIRY_RESULT_LEN 5 /* we need at least 5 bytes minimum for inquiry
				  * result */

/* SCSI device version for no disk inquiry result */
#define SCSI_SPC2_VER 4		/* indicates SCSI SPC2 (SPC3 is 5) */

/* Windows and Linux want different things for a non-existent lun. So, we'll let
 * caller pass in the peripheral qualifier and type.
 * NOTE:[4] SCSI returns (n-4); so we return length-1-4 or length-5. */

#define SET_NO_DISK_INQUIRY_RESULT(buf, len, lun, lun0notpresent, notpresent) \
	do {								\
		MEMSET(buf, 0,						\
		       MINNUM(len,					\
			      (unsigned int) NO_DISK_INQUIRY_RESULT_LEN)); \
		buf[2] = (U8) SCSI_SPC2_VER;				\
		if (lun == 0) {						\
			buf[0] = (U8) lun0notpresent;			\
			buf[3] = (U8) DEV_HISUPPORT;			\
		} else							\
			buf[0] = (U8) notpresent;			\
		buf[4] = (U8) (						\
			MINNUM(len,					\
			       (unsigned int) NO_DISK_INQUIRY_RESULT_LEN) - 5);	\
		if (len >= NO_DISK_INQUIRY_RESULT_LEN) {		\
			buf[8] = 'D';					\
			buf[9] = 'E';					\
			buf[10] = 'L';					\
			buf[11] = 'L';					\
			buf[16] = 'P';					\
			buf[17] = 'S';					\
			buf[18] = 'E';					\
			buf[19] = 'U';					\
			buf[20] = 'D';					\
			buf[21] = 'O';					\
			buf[22] = ' ';					\
			buf[23] = 'D';					\
			buf[24] = 'E';					\
			buf[25] = 'V';					\
			buf[26] = 'I';					\
			buf[27] = 'C';					\
			buf[28] = 'E';					\
			buf[30] = ' ';					\
			buf[31] = '.';					\
		}							\
	} while (0)


/*
* Struct & Defines to support sense information.
*/


/* The following struct is returned in sensebuf field in uiscmdrsp_scsi.  It is
* initialized in exactly the manner that is recommended in Windows (hence the
* odd values).
* When set, these fields will have the following values:
* ErrorCode = 0x70		indicates current error
* Valid = 1			indicates sense info is valid
* SenseKey			contains sense key as defined by SCSI specs.
* AdditionalSenseCode		contains sense key as defined by SCSI specs.
* AdditionalSenseCodeQualifier	contains qualifier to sense code as defined by
*				scsi docs.
* AdditionalSenseLength		contains will be sizeof(sense_data)-8=10.
*/
struct sense_data {
	U8 ErrorCode:7;
	U8 Valid:1;
	U8 SegmentNumber;
	U8 SenseKey:4;
	U8 Reserved:1;
	U8 IncorrectLength:1;
	U8 EndOfMedia:1;
	U8 FileMark:1;
	U8 Information[4];
	U8 AdditionalSenseLength;
	U8 CommandSpecificInformation[4];
	U8 AdditionalSenseCode;
	U8 AdditionalSenseCodeQualifier;
	U8 FieldReplaceableUnitCode;
	U8 SenseKeySpecific[3];
};

/* some SCSI ADSENSE codes */
#ifndef SCSI_ADSENSE_LUN_NOT_READY
#define SCSI_ADSENSE_LUN_NOT_READY 0x04
#endif	/*  */
#ifndef SCSI_ADSENSE_ILLEGAL_COMMAND
#define SCSI_ADSENSE_ILLEGAL_COMMAND 0x20
#endif	/*  */
#ifndef SCSI_ADSENSE_ILLEGAL_BLOCK
#endif	/*  */
#ifndef SCSI_ADSENSE_ILLEGAL_BLOCK
#define SCSI_ADSENSE_ILLEGAL_BLOCK  0x21
#endif	/*  */
#ifndef SCSI_ADSENSE_INVALID_CDB
#define SCSI_ADSENSE_INVALID_CDB    0x24
#endif	/*  */
#ifndef SCSI_ADSENSE_INVALID_LUN
#define SCSI_ADSENSE_INVALID_LUN    0x25
#endif	/*  */
#ifndef SCSI_ADWRITE_PROTECT
#define SCSI_ADWRITE_PROTECT        0x27
#endif	/*  */
#ifndef SCSI_ADSENSE_MEDIUM_CHANGED
#define SCSI_ADSENSE_MEDIUM_CHANGED 0x28
#endif	/*  */
#ifndef SCSI_ADSENSE_BUS_RESET
#define SCSI_ADSENSE_BUS_RESET      0x29
#endif	/*  */
#ifndef SCSI_ADSENSE_NO_MEDIA_IN_DEVICE
#define SCSI_ADSENSE_NO_MEDIA_IN_DEVICE 0x3a
#endif	/*  */

struct net_pkt_xmt {
	int len;	/* full length of data in the packet */
	int num_frags;	/* number of fragments in frags containing data */
	struct phys_info frags[MAX_PHYS_INFO];	/* physical page information for
						 * each fragment */
	char ethhdr[ETH_HEADER_SIZE];	/* the ethernet header  */
	struct {

		    /* these are needed for csum at uisnic end */
		U8 valid;	/* 1 = rest of this struct is valid - else
				 * ignore */
		U8 hrawoffv;	/* 1 = hwrafoff is valid */
		U8 nhrawoffv;	/* 1 = nhwrafoff is valid */
		U16 protocol;	/* specifies packet protocol */
		U32 csum;	/* value used to set skb->csum at IOPart */
		U32 hrawoff;	/* value used to set skb->h.raw at IOPart */
		/* hrawoff points to the start of the TRANSPORT LAYER HEADER */
		U32 nhrawoff;	/* value used to set skb->nh.raw at IOPart */
		/* nhrawoff points to the start of the NETWORK LAYER HEADER */
	} lincsum;

	    /* **** NOTE ****
	     * The full packet is described in frags but the ethernet header is
	     * separately kept in ethhdr so that uisnic doesn't have "MAP" the
	     * guest memory to get to the header. uisnic needs ethhdr to
	     * determine how to route the packet.
	     */
};

struct net_pkt_xmtdone {
	U32 xmt_done_result;	/* result of NET_XMIT */
#define XMIT_SUCCESS 0
#define XMIT_FAILED 1
};

/* RCVPOST_BUF_SIZe must be at most page_size(4096) - cache_line_size (64) The
* reason is because dev_skb_alloc which is used to generate RCV_POST skbs in
* virtnic requires that there is "overhead" in the buffer, and pads 16 bytes.  I
* prefer to use 1 full cache line size for "overhead" so that transfers are
* better.  IOVM requires that a buffer be represented by 1 phys_info structure
* which can only cover page_size. */
#define RCVPOST_BUF_SIZE 4032
#define MAX_NET_RCV_CHAIN \
	((ETH_MAX_MTU+ETH_HEADER_SIZE + RCVPOST_BUF_SIZE-1) / RCVPOST_BUF_SIZE)

struct net_pkt_rcvpost {
	    /* rcv buf size must be large enough to include ethernet data len +
	    * ethernet header len - we are choosing 2K because it is guaranteed
	    * to be describable */
	    struct phys_info frag;	/* physical page information for the
					 * single fragment 2K rcv buf */
	    U64 UniqueNum;		/* This is used to make sure that
					 * receive posts are returned to  */
	    /* the Adapter which sent them origonally. */
};

struct net_pkt_rcv {

	/* the number of receive buffers that can be chained  */
	/* is based on max mtu and size of each rcv buf */
	U32 rcv_done_len;	/* length of received data */
	U8 numrcvbufs;		/* number of receive buffers that contain the */
	/* incoming data; guest end MUST chain these together. */
	void *rcvbuf[MAX_NET_RCV_CHAIN];	/* the list of receive buffers
						 * that must be chained; */
	/* each entry is a receive buffer provided by NET_RCV_POST. */
	/* NOTE: first rcvbuf in the chain will also be provided in net.buf. */
	U64 UniqueNum;
	U32 RcvsDroppedDelta;
};

struct net_pkt_enbdis {
	void *context;
	U16 enable;		/* 1 = enable, 0 = disable */
};

struct net_pkt_macaddr {
	void *context;
	U8 macaddr[MAX_MACADDR_LEN];	/* 6 bytes */
};

/* cmd rsp packet used for VNIC network traffic  */
struct uiscmdrsp_net {
	NET_TYPES type;
	void *buf;
	union {
		struct net_pkt_xmt xmt;	/* used for NET_XMIT */
		struct net_pkt_xmtdone xmtdone;	/* used for NET_XMIT_DONE */
		struct net_pkt_rcvpost rcvpost;	/* used for NET_RCV_POST */
		struct net_pkt_rcv rcv;	/* used for NET_RCV */
		struct net_pkt_enbdis enbdis;	/* used for NET_RCV_ENBDIS, */
		/* NET_RCV_ENBDIS_ACK,  */
		/* NET_RCV_PROMSIC, */
		/* and NET_CONNECT_STATUS */
		struct net_pkt_macaddr macaddr;
	};
};

struct uiscmdrsp_scsitaskmgmt {
	TASK_MGMT_TYPES tasktype;

	    /* the type of task */
	struct uisscsi_dest vdest;

	    /* the vdisk for which this task mgmt is generated */
	void *scsicmd;

	    /* This is some handle that the guest has saved off for its own use.
	    * Its value is preserved by iopart & returned as is in the task mgmt
	    * rsp. */
	void *notify;

	    /* For linux guests, this is a pointer to wait_queue_head that a
	    * thread is waiting on to see if the taskmgmt command has completed.
	    * For windows guests, this is a pointer to a location that a waiting
	    * thread is testing to see if the taskmgmt command has completed.
	    * When the rsp is received by guest, the thread receiving the
	    * response uses this to notify the the thread waiting for taskmgmt
	    * command completion.  Its value is preserved by iopart & returned
	    * as is in the task mgmt rsp. */
	void *notifyresult;

	    /* this is a handle to location in guest where the result of the
	    * taskmgmt command (result field) is to saved off when the response
	    * is handled.  Its value is preserved by iopart & returned as is in
	    * the task mgmt rsp. */
	char result;

	    /* result of taskmgmt command - set by IOPart - values are: */
#define TASK_MGMT_FAILED  0
#define TASK_MGMT_SUCCESS 1
};

/* The following is used by uissd to send disk add/remove notifications to
 * Guest */
/* Note that the vHba pointer is not used by the Client/Guest side. */
struct uiscmdrsp_disknotify {
	U8 add;		/* 0-remove, 1-add */
	void *vHba;		/* Pointer to vhba_info for channel info to
				 * route msg */
	U32 channel, id, lun;	/* SCSI Path of Disk to added or removed */
};

/* The following is used by virthba/vSCSI to send the Acquire/Release commands
* to the IOVM.  */
struct uiscmdrsp_vdiskmgmt {
	VDISK_MGMT_TYPES vdisktype;

	    /* the type of task */
	struct uisscsi_dest vdest;

	    /* the vdisk for which this task mgmt is generated */
	void *scsicmd;

	    /* This is some handle that the guest has saved off for its own use.
	    * Its value is preserved by iopart & returned as is in the task mgmt
	    * rsp. */
	void *notify;

	    /* For linux guests, this is a pointer to wait_queue_head that a
	    * thread is waiting on to see if the taskmgmt command has completed.
	    * For windows guests, this is a pointer to a location that a waiting
	    * thread is testing to see if the taskmgmt command has completed.
	    * When the rsp is received by guest, the thread receiving the
	    * response uses this to notify the the thread waiting for taskmgmt
	    * command completion.  Its value is preserved by iopart & returned
	    * as is in the task mgmt rsp. */
	void *notifyresult;

	    /* this is a handle to location in guest where the result of the
	    * taskmgmt command (result field) is to saved off when the response
	    * is handled.  Its value is preserved by iopart & returned as is in
	    * the task mgmt rsp. */
	char result;

	    /* result of taskmgmt command - set by IOPart - values are: */
#define VDISK_MGMT_FAILED  0
#define VDISK_MGMT_SUCCESS 1
};

/* keeping cmd & rsp info in one structure for now cmd rsp packet for scsi */
struct uiscmdrsp {
	char cmdtype;

	    /* describes what type of information is in the struct */
#define CMD_SCSI_TYPE		1
#define CMD_NET_TYPE		2
#define CMD_SCSITASKMGMT_TYPE	3
#define CMD_NOTIFYGUEST_TYPE	4
#define CMD_VDISKMGMT_TYPE	5
	union {
		struct uiscmdrsp_scsi scsi;
		struct uiscmdrsp_net net;
		struct uiscmdrsp_scsitaskmgmt scsitaskmgmt;
		struct uiscmdrsp_disknotify disknotify;
		struct uiscmdrsp_vdiskmgmt vdiskmgmt;
	};
	void *private_data;	/* used to send the response when the cmd is
				 * done (scsi & scsittaskmgmt).  */
	struct uiscmdrsp *next;	/* General Purpose Queue Link */
	struct uiscmdrsp *activeQ_next;	/* Used to track active commands */
	struct uiscmdrsp *activeQ_prev;	/* Used to track active commands  */
};

/* This is just the header of the IO channel.  It is assumed that directly after
* this header there is a large region of memory which contains the command and
* response queues as specified in cmdQ and rspQ SIGNAL_QUEUE_HEADERS. */
typedef struct _ULTRA_IO_CHANNEL_PROTOCOL {
	CHANNEL_HEADER ChannelHeader;
	SIGNAL_QUEUE_HEADER cmdQ;
	SIGNAL_QUEUE_HEADER rspQ;
	union {
		struct {
			struct vhba_wwnn wwnn;	/* 8 bytes */
			struct vhba_config_max max;	/* 20 bytes */
		} vhba;		/* 28 */
		struct {
			U8 macaddr[MAX_MACADDR_LEN];	/* 6 bytes */
			U32 num_rcv_bufs;	/* 4 */
			U32 mtu;	/* 4 */
			uuid_le zoneGuid;	/* 16 */
		} vnic;		/* total     30 */
	};

#define MAX_CLIENTSTRING_LEN 1024
	 U8 clientString[MAX_CLIENTSTRING_LEN];	/* NULL terminated - so holds
						 * max - 1 bytes */
} ULTRA_IO_CHANNEL_PROTOCOL;

#pragma pack(pop)
/* ///////////// END PRAGMA PACK PUSH 1 /////////////////////////// */

/* define offsets to members of struct uiscmdrsp */
#define OFFSET_CMDTYPE OFFSETOF(struct uiscmdrsp, cmdtype)
#define OFFSET_SCSI OFFSETOF(struct uiscmdrsp, scsi)
#define OFFSET_NET OFFSETOF(struct uiscmdrsp, net)
#define OFFSET_SCSITASKMGMT OFFSETOF(struct uiscmdrsp, scsitaskmgmt)
#define OFFSET_NEXT OFFSETOF(struct uiscmdrsp, next)

/* define offsets to members of struct uiscmdrsp_net */
#define OFFSET_TYPE OFFSETOF(struct uiscmdrsp_net, type)
#define OFFSET_BUF OFFSETOF(struct uiscmdrsp_net, buf)
#define OFFSET_XMT OFFSETOF(struct uiscmdrsp_net, xmt)
#define OFFSET_XMT_DONE_RESULT OFFSETOF(struct uiscmdrsp_net, xmtdone)
#define OFFSET_RCVPOST OFFSETOF(struct uiscmdrsp_net, rcvpost)
#define OFFSET_RCV_DONE_LEN OFFSETOF(struct uiscmdrsp_net, rcv)
#define OFFSET_ENBDIS OFFSETOF(struct uiscmdrsp_net, enbdis)

/* define offsets to members of struct net_pkt_rcvpost */
#define OFFSET_TOTALLEN OFFSETOF(struct net_pkt_rcvpost, totallen)
#define	OFFSET_FRAG OFFSETOF(struct net_pkt_rcvpost, frag)

/*
* INLINE functions for initializing and accessing I/O data channels
*/


#define NUMSIGNALS(x, q) (((ULTRA_IO_CHANNEL_PROTOCOL *)(x))->q.MaxSignalSlots)
#define SIZEOF_PROTOCOL (COVER(sizeof(ULTRA_IO_CHANNEL_PROTOCOL), 64))
#define SIZEOF_CMDRSP (COVER(sizeof(struct uiscmdrsp), 64))

#define IO_CHANNEL_SIZE(x) COVER(SIZEOF_PROTOCOL + \
				 (NUMSIGNALS(x, cmdQ) + \
				  NUMSIGNALS(x, rspQ)) * SIZEOF_CMDRSP, 4096)
#define MIN_IO_CHANNEL_SIZE COVER(SIZEOF_PROTOCOL + \
				  2 * MIN_NUMSIGNALS * SIZEOF_CMDRSP, 4096)
#ifdef __GNUC__
/* These defines should only ever be used in service partitons */
/* because they rely on the size of uiscmdrsp */
#define QSLOTSFROMBYTES(bytes) (((bytes-SIZEOF_PROTOCOL)/2)/SIZEOF_CMDRSP)
#define QSIZEFROMBYTES(bytes) (QSLOTSFROMBYTES(bytes)*SIZEOF_CMDRSP)
#define SignalQInit(x)						\
	do {							\
		x->cmdQ.Size = QSIZEFROMBYTES(x->ChannelHeader.Size);	\
		x->cmdQ.oSignalBase = SIZEOF_PROTOCOL -			\
			OFFSETOF(ULTRA_IO_CHANNEL_PROTOCOL, cmdQ);	\
		x->cmdQ.SignalSize = SIZEOF_CMDRSP;			\
		x->cmdQ.MaxSignalSlots =				\
			QSLOTSFROMBYTES(x->ChannelHeader.Size);		\
		x->cmdQ.MaxSignals = x->cmdQ.MaxSignalSlots - 1;	\
		x->rspQ.Size = QSIZEFROMBYTES(x->ChannelHeader.Size);	\
		x->rspQ.oSignalBase =					\
			(SIZEOF_PROTOCOL + x->cmdQ.Size) -		\
			OFFSETOF(ULTRA_IO_CHANNEL_PROTOCOL, rspQ);	\
		x->rspQ.SignalSize = SIZEOF_CMDRSP;			\
		x->rspQ.MaxSignalSlots =				\
			QSLOTSFROMBYTES(x->ChannelHeader.Size);		\
		x->rspQ.MaxSignals = x->rspQ.MaxSignalSlots - 1;	\
		x->ChannelHeader.oChannelSpace =			\
			OFFSETOF(ULTRA_IO_CHANNEL_PROTOCOL, cmdQ);	\
	} while (0)

#define INIT_CLIENTSTRING(chan, type, clientStr, clientStrLen)	\
	do {								\
		if (clientStr) {					\
			chan->ChannelHeader.oClientString =		\
				OFFSETOF(type, clientString);		\
			MEMCPY(chan->clientString, clientStr,		\
			       MINNUM(clientStrLen,			\
				      (U32) (MAX_CLIENTSTRING_LEN - 1))); \
			chan->clientString[MINNUM(clientStrLen,		\
						  (U32) (MAX_CLIENTSTRING_LEN \
							 - 1))]		\
				= '\0';					\
		}							\
		else							\
			if (clientStrLen > 0)				\
				return 0;				\
	} while (0)


#define ULTRA_IO_CHANNEL_SERVER_READY(x, chanId, logCtx) \
	ULTRA_CHANNEL_SERVER_TRANSITION(x, chanId, SrvState, CHANNELSRV_READY, \
					logCtx);

#define ULTRA_IO_CHANNEL_SERVER_NOTREADY(x, chanId, logCtx)	\
	ULTRA_CHANNEL_SERVER_TRANSITION(x, chanId, SrvState, \
					CHANNELSRV_UNINITIALIZED, logCtx);

static inline int ULTRA_VHBA_init_channel(ULTRA_IO_CHANNEL_PROTOCOL *x,
					      struct vhba_wwnn *wwnn,
					      struct vhba_config_max *max,
					      unsigned char *clientStr,
					      U32 clientStrLen, U64 bytes)  {
	MEMSET(x, 0, sizeof(ULTRA_IO_CHANNEL_PROTOCOL));
	x->ChannelHeader.VersionId = ULTRA_VHBA_CHANNEL_PROTOCOL_VERSIONID;
	x->ChannelHeader.Signature = ULTRA_VHBA_CHANNEL_PROTOCOL_SIGNATURE;
	x->ChannelHeader.SrvState = CHANNELSRV_UNINITIALIZED;
	x->ChannelHeader.HeaderSize = sizeof(x->ChannelHeader);
	x->ChannelHeader.Size = COVER(bytes, 4096);
	x->ChannelHeader.Type = UltraVhbaChannelProtocolGuid;
	x->ChannelHeader.ZoneGuid = NULL_UUID_LE;
	x->vhba.wwnn = *wwnn;
	x->vhba.max = *max;
	INIT_CLIENTSTRING(x, ULTRA_IO_CHANNEL_PROTOCOL, clientStr,
			  clientStrLen);
	SignalQInit(x);
	if ((x->cmdQ.MaxSignalSlots > MAX_NUMSIGNALS) ||
	     (x->rspQ.MaxSignalSlots > MAX_NUMSIGNALS)) {
		return 0;
	}
	if ((x->cmdQ.MaxSignalSlots < MIN_NUMSIGNALS) ||
	     (x->rspQ.MaxSignalSlots < MIN_NUMSIGNALS)) {
		return 0;
	}
	return 1;
}

static inline void ULTRA_VHBA_set_max(ULTRA_IO_CHANNEL_PROTOCOL *x,
				      struct vhba_config_max *max)  {
	x->vhba.max = *max;
}

static inline int ULTRA_VNIC_init_channel(ULTRA_IO_CHANNEL_PROTOCOL *x,
						 unsigned char *macaddr,
						 U32 num_rcv_bufs, U32 mtu,
						 uuid_le zoneGuid,
						 unsigned char *clientStr,
						 U32 clientStrLen,
						 U64 bytes)  {
	MEMSET(x, 0, sizeof(ULTRA_IO_CHANNEL_PROTOCOL));
	x->ChannelHeader.VersionId = ULTRA_VNIC_CHANNEL_PROTOCOL_VERSIONID;
	x->ChannelHeader.Signature = ULTRA_VNIC_CHANNEL_PROTOCOL_SIGNATURE;
	x->ChannelHeader.SrvState = CHANNELSRV_UNINITIALIZED;
	x->ChannelHeader.HeaderSize = sizeof(x->ChannelHeader);
	x->ChannelHeader.Size = COVER(bytes, 4096);
	x->ChannelHeader.Type = UltraVnicChannelProtocolGuid;
	x->ChannelHeader.ZoneGuid = NULL_UUID_LE;
	MEMCPY(x->vnic.macaddr, macaddr, MAX_MACADDR_LEN);
	x->vnic.num_rcv_bufs = num_rcv_bufs;
	x->vnic.mtu = mtu;
	x->vnic.zoneGuid = zoneGuid;
	INIT_CLIENTSTRING(x, ULTRA_IO_CHANNEL_PROTOCOL, clientStr,
			   clientStrLen);
	SignalQInit(x);
	if ((x->cmdQ.MaxSignalSlots > MAX_NUMSIGNALS) ||
	     (x->rspQ.MaxSignalSlots > MAX_NUMSIGNALS)) {
		return 0;
	}
	if ((x->cmdQ.MaxSignalSlots < MIN_NUMSIGNALS) ||
	     (x->rspQ.MaxSignalSlots < MIN_NUMSIGNALS)) {
		return 0;
	}
	return 1;
}

#endif	/* __GNUC__ */

/*
* INLINE function for expanding a guest's pfn-off-size into multiple 4K page
* pfn-off-size entires.
*/


/* we deal with 4K page sizes when we it comes to passing page information
 * between */
/* Guest and IOPartition. */
#define PI_PAGE_SIZE  0x1000
#define PI_PAGE_MASK  0x0FFF
#define PI_PAGE_SHIFT 12

/* returns next non-zero index on success or zero on failure (i.e. out of
 * room)
 */
static INLINE  U16
add_physinfo_entries(U32 inp_pfn,	/* input - specifies the pfn to be used
					 * to add entries */
		     U16 inp_off,	/* input - specifies the off to be used
					 * to add entries */
		     U32 inp_len,	/* input - specifies the len to be used
					 * to add entries */
		     U16 index,		/* input - index in array at which new
					 * entries are added */
		     U16 max_pi_arr_entries,	/* input - specifies the maximum
						 * entries pi_arr can hold */
		     struct phys_info pi_arr[]) /* input & output - array to
						  * which entries are added */
{
	U32 len;
	U16 i, firstlen;

	firstlen = PI_PAGE_SIZE - inp_off;
	if (inp_len <= firstlen) {

		/* the input entry spans only one page - add as is */
		if (index >= max_pi_arr_entries)
			return 0;
		pi_arr[index].pi_pfn = inp_pfn;
		pi_arr[index].pi_off = (U16) inp_off;
		pi_arr[index].pi_len = (U16) inp_len;
		    return index + 1;
	}

	    /* this entry spans multiple pages */
	    for (len = inp_len, i = 0; len;
		 len -= pi_arr[index + i].pi_len, i++) {
		if (index + i >= max_pi_arr_entries)
			return 0;
		pi_arr[index + i].pi_pfn = inp_pfn + i;
		if (i == 0) {
			pi_arr[index].pi_off = inp_off;
			pi_arr[index].pi_len = firstlen;
		}

		else {
			pi_arr[index + i].pi_off = 0;
			pi_arr[index + i].pi_len =
			    (U16) MINNUM(len, (U32) PI_PAGE_SIZE);
		}

	}
	return index + i;
}

#endif				/* __IOCHANNEL_H__ */
