/* Copyright (C) 2010 - 2013 UNISYS CORPORATION */
/* All rights reserved. */
#ifndef __IOCHANNEL_H__
#define __IOCHANNEL_H__

/*
 * Everything needed for IOPart-GuestPart communication is define in
 * this file.  Note: Everything is OS-independent because this file is
 * used by Windows, Linux and possible EFI drivers.
 */

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

#include <linux/dma-direction.h>
#include "channel.h"
#include "channel_guid.h"

#define ULTRA_VHBA_CHANNEL_PROTOCOL_SIGNATURE ULTRA_CHANNEL_PROTOCOL_SIGNATURE
#define ULTRA_VNIC_CHANNEL_PROTOCOL_SIGNATURE ULTRA_CHANNEL_PROTOCOL_SIGNATURE
#define ULTRA_VSWITCH_CHANNEL_PROTOCOL_SIGNATURE \
	ULTRA_CHANNEL_PROTOCOL_SIGNATURE

/* Must increment these whenever you insert or delete fields within this channel
 * struct.  Also increment whenever you change the meaning of fields within this
 * channel struct so as to break pre-existing software.  Note that you can
 * usually add fields to the END of the channel struct withOUT needing to
 * increment this.
 */
#define ULTRA_VHBA_CHANNEL_PROTOCOL_VERSIONID 2
#define ULTRA_VNIC_CHANNEL_PROTOCOL_VERSIONID 2
#define ULTRA_VSWITCH_CHANNEL_PROTOCOL_VERSIONID 1

#define SPAR_VHBA_CHANNEL_OK_CLIENT(ch)			\
	(spar_check_channel_client(ch, spar_vhba_channel_protocol_uuid, \
				   "vhba", MIN_IO_CHANNEL_SIZE,	\
				   ULTRA_VHBA_CHANNEL_PROTOCOL_VERSIONID, \
				   ULTRA_VHBA_CHANNEL_PROTOCOL_SIGNATURE))

#define SPAR_VNIC_CHANNEL_OK_CLIENT(ch)			\
	(spar_check_channel_client(ch, spar_vnic_channel_protocol_uuid, \
				   "vnic", MIN_IO_CHANNEL_SIZE,	\
				   ULTRA_VNIC_CHANNEL_PROTOCOL_VERSIONID, \
				   ULTRA_VNIC_CHANNEL_PROTOCOL_SIGNATURE))

/*
 * Everything necessary to handle SCSI & NIC traffic between Guest Partition and
 * IO Partition is defined below.
 */

/* Defines and enums. */
#define MINNUM(a, b) (((a) < (b)) ? (a) : (b))
#define MAXNUM(a, b) (((a) > (b)) ? (a) : (b))

/* define the two queues per data channel between iopart and ioguestparts */
/* used by ioguestpart to 'insert' signals to iopart */
#define IOCHAN_TO_IOPART 0
/* used by ioguestpart to 'remove' signals from iopart, same previous queue */
#define IOCHAN_FROM_IOPART 1

/* size of cdb - i.e., scsi cmnd */
#define MAX_CMND_SIZE 16

#define MAX_SENSE_SIZE 64

#define MAX_PHYS_INFO 64

/* various types of network packets that can be sent in cmdrsp */
enum net_types {
	NET_RCV_POST = 0,	/* submit buffer to hold receiving
				 * incoming packet
				 */
	/* virtnic -> uisnic */
	NET_RCV,		/* incoming packet received */
	/* uisnic -> virtpci */
	NET_XMIT,		/* for outgoing net packets */
	/* virtnic -> uisnic */
	NET_XMIT_DONE,		/* outgoing packet xmitted */
	/* uisnic -> virtpci */
	NET_RCV_ENBDIS,		/* enable/disable packet reception */
	/* virtnic -> uisnic */
	NET_RCV_ENBDIS_ACK,	/* acknowledge enable/disable packet */
				/* reception */
	/* uisnic -> virtnic */
	NET_RCV_PROMISC,	/* enable/disable promiscuous mode */
	/* virtnic -> uisnic */
	NET_CONNECT_STATUS,	/* indicate the loss or restoration of a network
				 * connection
				 */
	/* uisnic -> virtnic */
	NET_MACADDR,		/* indicates the client has requested to update
				 * its MAC addr
				 */
	NET_MACADDR_ACK,	/* MAC address */

};

#define		ETH_HEADER_SIZE 14	/* size of ethernet header */

#define		ETH_MIN_DATA_SIZE 46	/* minimum eth data size */
#define		ETH_MIN_PACKET_SIZE (ETH_HEADER_SIZE + ETH_MIN_DATA_SIZE)

#define		ETH_MAX_MTU 16384	/* maximum data size */

#ifndef MAX_MACADDR_LEN
#define MAX_MACADDR_LEN 6	/* number of bytes in MAC address */
#endif				/* MAX_MACADDR_LEN */

/* various types of scsi task mgmt commands  */
enum task_mgmt_types {
	TASK_MGMT_ABORT_TASK = 1,
	TASK_MGMT_BUS_RESET,
	TASK_MGMT_LUN_RESET,
	TASK_MGMT_TARGET_RESET,
};

/* various types of vdisk mgmt commands  */
enum vdisk_mgmt_types {
	VDISK_MGMT_ACQUIRE = 1,
	VDISK_MGMT_RELEASE,
};

struct phys_info {
	u64 pi_pfn;
	u16 pi_off;
	u16 pi_len;
} __packed;

#define MIN_NUMSIGNALS 64

/* structs with pragma pack  */

struct guest_phys_info {
	u64 address;
	u64 length;
} __packed;

#define GPI_ENTRIES_PER_PAGE (PAGE_SIZE / sizeof(struct guest_phys_info))

struct uisscsi_dest {
	u32 channel;		/* channel == bus number */
	u32 id;			/* id == target number */
	u32 lun;		/* lun == logical unit number */
} __packed;

struct vhba_wwnn {
	u32 wwnn1;
	u32 wwnn2;
} __packed;

/* WARNING: Values stired in this structure must contain maximum counts (not
 * maximum values).
 */
struct vhba_config_max {/* 20 bytes */
	u32 max_channel;/* maximum channel for devices attached to this bus */
	u32 max_id;	/* maximum SCSI ID for devices attached to bus */
	u32 max_lun;	/* maximum SCSI LUN for devices attached to bus */
	u32 cmd_per_lun;/* maximum number of outstanding commands per LUN */
	u32 max_io_size;/* maximum io size for devices attached to this bus */
	/* max io size is often determined by the resource of the hba. e.g */
	/* max scatter gather list length * page size / sector size */
} __packed;

struct uiscmdrsp_scsi {
	u64 handle;		/* the handle to the cmd that was received */
				/* send it back as is in the rsp packet.  */
	u8 cmnd[MAX_CMND_SIZE];	/* the cdb for the command */
	u32 bufflen;		/* length of data to be transferred out or in */
	u16 guest_phys_entries;	/* Number of entries in scatter-gather list */
	struct guest_phys_info gpi_list[MAX_PHYS_INFO];	/* physical address
							 * information for each
							 * fragment
							 */
	enum dma_data_direction  data_dir; /* direction of the data, if any */
	struct uisscsi_dest vdest;	/* identifies the virtual hba, id, */
					/* channel, lun to which cmd was sent */

	/* Needed to queue the rsp back to cmd originator */
	int linuxstat;		/* original Linux status used by linux vdisk */
	u8 scsistat;		/* the scsi status */
	u8 addlstat;		/* non-scsi status */
#define ADDL_SEL_TIMEOUT	4

	/* the following fields are need to determine the result of command */
	 u8 sensebuf[MAX_SENSE_SIZE];	/* sense info in case cmd failed; */
	/* it holds the sense_data struct; */
	/* see that struct for details. */
	void *vdisk; /* pointer to the vdisk to clean up when IO completes. */
	int no_disk_result;
	/* used to return no disk inquiry result
	 * when no_disk_result is set to 1,
	 * scsi.scsistat is SAM_STAT_GOOD
	 * scsi.addlstat is 0
	 * scsi.linuxstat is SAM_STAT_GOOD
	 * That is, there is NO error.
	 */
} __packed;

/* Defines to support sending correct inquiry result when no disk is
 * configured.
 */

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

#define DEV_NOT_CAPABLE 0x7f	/* peripheral qualifier of 0x3  */
				/* peripheral type of 0x1f */
				/* specifies no device but target present */

#define DEV_DISK_CAPABLE_NOT_PRESENT 0x20 /* peripheral qualifier of 0x1 */
    /* peripheral type of 0 - disk */
    /* specifies device capable, but not present */

#define DEV_HISUPPORT 0x10	/* HiSup = 1; shows support for report luns */
				/* must be returned for lun 0. */

/* NOTE: Linux code assumes inquiry contains 36 bytes. Without checking length
 * in buf[4] some linux code accesses bytes beyond 5 to retrieve vendor, product
 * & revision.  Yikes! So let us always send back 36 bytes, the minimum for
 * inquiry result.
 */
#define NO_DISK_INQUIRY_RESULT_LEN 36

#define MIN_INQUIRY_RESULT_LEN 5 /* 5 bytes minimum for inquiry result */

/* SCSI device version for no disk inquiry result */
#define SCSI_SPC2_VER 4		/* indicates SCSI SPC2 (SPC3 is 5) */

/* Windows and Linux want different things for a non-existent lun. So, we'll let
 * caller pass in the peripheral qualifier and type.
 * NOTE:[4] SCSI returns (n-4); so we return length-1-4 or length-5.
 */

#define SET_NO_DISK_INQUIRY_RESULT(buf, len, lun, lun0notpresent, notpresent) \
	do {								\
		memset(buf, 0,						\
		       MINNUM(len,					\
			      (unsigned int)NO_DISK_INQUIRY_RESULT_LEN)); \
		buf[2] = (u8)SCSI_SPC2_VER;				\
		if (lun == 0) {						\
			buf[0] = (u8)lun0notpresent;			\
			buf[3] = (u8)DEV_HISUPPORT;			\
		} else							\
			buf[0] = (u8)notpresent;			\
		buf[4] = (u8)(						\
			MINNUM(len,					\
			       (unsigned int)NO_DISK_INQUIRY_RESULT_LEN) - 5);\
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

/* Struct & Defines to support sense information. */

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
 * AdditionalSenseLength	contains will be sizeof(sense_data)-8=10.
 */
struct sense_data {
	u8 errorcode:7;
	u8 valid:1;
	u8 segment_number;
	u8 sense_key:4;
	u8 reserved:1;
	u8 incorrect_length:1;
	u8 end_of_media:1;
	u8 file_mark:1;
	u8 information[4];
	u8 additional_sense_length;
	u8 command_specific_information[4];
	u8 additional_sense_code;
	u8 additional_sense_code_qualifier;
	u8 fru_code;
	u8 sense_key_specific[3];
} __packed;

struct net_pkt_xmt {
	int len;	/* full length of data in the packet */
	int num_frags;	/* number of fragments in frags containing data */
	struct phys_info frags[MAX_PHYS_INFO];	/* physical page information */
	char ethhdr[ETH_HEADER_SIZE];	/* the ethernet header  */
	struct {
		/* these are needed for csum at uisnic end */
		u8 valid;	/* 1 = struct is valid - else ignore */
		u8 hrawoffv;	/* 1 = hwrafoff is valid */
		u8 nhrawoffv;	/* 1 = nhwrafoff is valid */
		u16 protocol;	/* specifies packet protocol */
		u32 csum;	/* value used to set skb->csum at IOPart */
		u32 hrawoff;	/* value used to set skb->h.raw at IOPart */
		/* hrawoff points to the start of the TRANSPORT LAYER HEADER */
		u32 nhrawoff;	/* value used to set skb->nh.raw at IOPart */
		/* nhrawoff points to the start of the NETWORK LAYER HEADER */
	} lincsum;

	    /* **** NOTE ****
	     * The full packet is described in frags but the ethernet header is
	     * separately kept in ethhdr so that uisnic doesn't have "MAP" the
	     * guest memory to get to the header. uisnic needs ethhdr to
	     * determine how to route the packet.
	     */
} __packed;

struct net_pkt_xmtdone {
	u32 xmt_done_result;	/* result of NET_XMIT */
} __packed;

/* RCVPOST_BUF_SIZe must be at most page_size(4096) - cache_line_size (64) The
 * reason is because dev_skb_alloc which is used to generate RCV_POST skbs in
 * virtnic requires that there is "overhead" in the buffer, and pads 16 bytes. I
 * prefer to use 1 full cache line size for "overhead" so that transfers are
 * better.  IOVM requires that a buffer be represented by 1 phys_info structure
 * which can only cover page_size.
 */
#define RCVPOST_BUF_SIZE 4032
#define MAX_NET_RCV_CHAIN \
	((ETH_MAX_MTU + ETH_HEADER_SIZE + RCVPOST_BUF_SIZE - 1) \
	/ RCVPOST_BUF_SIZE)

struct net_pkt_rcvpost {
	    /* rcv buf size must be large enough to include ethernet data len +
	     * ethernet header len - we are choosing 2K because it is guaranteed
	     * to be describable
	     */
	    struct phys_info frag;	/* physical page information for the */
					/* single fragment 2K rcv buf */
	    u64 unique_num;
	    /* unique_num ensure that receive posts are returned to */
	    /* the Adapter which we sent them originally. */
} __packed;

struct net_pkt_rcv {
	/* the number of receive buffers that can be chained  */
	/* is based on max mtu and size of each rcv buf */
	u32 rcv_done_len;	/* length of received data */
	u8 numrcvbufs;		/* number of receive buffers that contain the */
	/* incoming data; guest end MUST chain these together. */
	void *rcvbuf[MAX_NET_RCV_CHAIN];	/* list of chained rcvbufs */
	/* each entry is a receive buffer provided by NET_RCV_POST. */
	/* NOTE: first rcvbuf in the chain will also be provided in net.buf. */
	u64 unique_num;
	u32 rcvs_dropped_delta;
} __packed;

struct net_pkt_enbdis {
	void *context;
	u16 enable;		/* 1 = enable, 0 = disable */
} __packed;

struct net_pkt_macaddr {
	void *context;
	u8 macaddr[MAX_MACADDR_LEN];	/* 6 bytes */
} __packed;

/* cmd rsp packet used for VNIC network traffic  */
struct uiscmdrsp_net {
	enum net_types type;
	void *buf;
	union {
		struct net_pkt_xmt xmt;		/* used for NET_XMIT */
		struct net_pkt_xmtdone xmtdone;	/* used for NET_XMIT_DONE */
		struct net_pkt_rcvpost rcvpost;	/* used for NET_RCV_POST */
		struct net_pkt_rcv rcv;		/* used for NET_RCV */
		struct net_pkt_enbdis enbdis;	/* used for NET_RCV_ENBDIS, */
						/* NET_RCV_ENBDIS_ACK,  */
						/* NET_RCV_PROMSIC, */
						/* and NET_CONNECT_STATUS */
		struct net_pkt_macaddr macaddr;
	};
} __packed;

struct uiscmdrsp_scsitaskmgmt {
	enum task_mgmt_types tasktype;

	    /* the type of task */
	struct uisscsi_dest vdest;

	    /* the vdisk for which this task mgmt is generated */
	u64 handle;

	    /* This is a handle that the guest has saved off for its own use.
	     * Its value is preserved by iopart & returned as is in the task
	     * mgmt rsp.
	     */
	u64 notify_handle;

	   /* For linux guests, this is a pointer to wait_queue_head that a
	    * thread is waiting on to see if the taskmgmt command has completed.
	    * When the rsp is received by guest, the thread receiving the
	    * response uses this to notify the thread waiting for taskmgmt
	    * command completion.  Its value is preserved by iopart & returned
	    * as is in the task mgmt rsp.
	    */
	u64 notifyresult_handle;

	    /* this is a handle to location in guest where the result of the
	     * taskmgmt command (result field) is to saved off when the response
	     * is handled.  Its value is preserved by iopart & returned as is in
	     * the task mgmt rsp.
	     */
	char result;

	    /* result of taskmgmt command - set by IOPart - values are: */
#define TASK_MGMT_FAILED  0
} __packed;

/* Used by uissd to send disk add/remove notifications to Guest */
/* Note that the vHba pointer is not used by the Client/Guest side. */
struct uiscmdrsp_disknotify {
	u8 add;			/* 0-remove, 1-add */
	void *v_hba;		/* channel info to route msg */
	u32 channel, id, lun;	/* SCSI Path of Disk to added or removed */
} __packed;

/* The following is used by virthba/vSCSI to send the Acquire/Release commands
 * to the IOVM.
 */
struct uiscmdrsp_vdiskmgmt {
	enum vdisk_mgmt_types vdisktype;

	    /* the type of task */
	struct uisscsi_dest vdest;

	    /* the vdisk for which this task mgmt is generated */
	u64 handle;

	    /* This is a handle that the guest has saved off for its own use.
	     * Its value is preserved by iopart & returned as is in the task
	     * mgmt rsp.
	     */
	u64 notify_handle;

	    /* For linux guests, this is a pointer to wait_queue_head that a
	     * thread is waiting on to see if the tskmgmt command has completed.
	     * When the rsp is received by guest, the thread receiving the
	     * response uses this to notify the thread waiting for taskmgmt
	     * command completion.  Its value is preserved by iopart & returned
	     * as is in the task mgmt rsp.
	     */
	u64 notifyresult_handle;

	    /* this is a handle to location in guest where the result of the
	     * taskmgmt command (result field) is to saved off when the response
	     * is handled.  Its value is preserved by iopart & returned as is in
	     * the task mgmt rsp.
	     */
	char result;

	    /* result of taskmgmt command - set by IOPart - values are: */
#define VDISK_MGMT_FAILED  0
} __packed;

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
	void *private_data;	/* send the response when the cmd is */
				/* done (scsi & scsittaskmgmt). */
	struct uiscmdrsp *next;	/* General Purpose Queue Link */
	struct uiscmdrsp *activeQ_next;	/* Used to track active commands */
	struct uiscmdrsp *activeQ_prev;	/* Used to track active commands */
} __packed;

struct iochannel_vhba {
	struct vhba_wwnn wwnn;		/* 8 bytes */
	struct vhba_config_max max;	/* 20 bytes */
} __packed;				/* total = 28 bytes */
struct iochannel_vnic {
	u8 macaddr[6];			/* 6 bytes */
	u32 num_rcv_bufs;		/* 4 bytes */
	u32 mtu;			/* 4 bytes */
	uuid_le zone_uuid;		/* 16 bytes */
} __packed;
/* This is just the header of the IO channel.  It is assumed that directly after
 * this header there is a large region of memory which contains the command and
 * response queues as specified in cmd_q and rsp_q SIGNAL_QUEUE_HEADERS.
 */
struct spar_io_channel_protocol {
	struct channel_header channel_header;
	struct signal_queue_header cmd_q;
	struct signal_queue_header rsp_q;
	union {
		struct iochannel_vhba vhba;
		struct iochannel_vnic vnic;
	} __packed;

#define MAX_CLIENTSTRING_LEN 1024
	/* client_string is NULL termimated so holds max -1 bytes */
	 u8 client_string[MAX_CLIENTSTRING_LEN];
} __packed;

/* INLINE functions for initializing and accessing I/O data channels */
#define SIZEOF_PROTOCOL (COVER(sizeof(struct spar_io_channel_protocol), 64))
#define SIZEOF_CMDRSP (COVER(sizeof(struct uiscmdrsp), 64))

#define MIN_IO_CHANNEL_SIZE COVER(SIZEOF_PROTOCOL + \
				  2 * MIN_NUMSIGNALS * SIZEOF_CMDRSP, 4096)

/*
 * INLINE function for expanding a guest's pfn-off-size into multiple 4K page
 * pfn-off-size entires.
 */

/* use 4K page sizes when we it comes to passing page information between */
/* Guest and IOPartition. */
#define PI_PAGE_SIZE  0x1000
#define PI_PAGE_MASK  0x0FFF

/* returns next non-zero index on success or zero on failure (i.e. out of
 * room)
 */
static inline  u16
add_physinfo_entries(u32 inp_pfn, u16 inp_off, u32 inp_len, u16 index,
		     u16 max_pi_arr_entries, struct phys_info pi_arr[])
{
	u32 len;
	u16 i, firstlen;

	firstlen = PI_PAGE_SIZE - inp_off;
	if (inp_len <= firstlen) {
		/* the input entry spans only one page - add as is */
		if (index >= max_pi_arr_entries)
			return 0;
		pi_arr[index].pi_pfn = inp_pfn;
		pi_arr[index].pi_off = (u16)inp_off;
		pi_arr[index].pi_len = (u16)inp_len;
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
			    (u16)MINNUM(len, (u32)PI_PAGE_SIZE);
		}
	}
	return index + i;
}

#endif				/* __IOCHANNEL_H__ */
