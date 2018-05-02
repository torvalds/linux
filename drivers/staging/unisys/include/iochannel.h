// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2010 - 2016 UNISYS CORPORATION
 * All rights reserved.
 */

#ifndef __IOCHANNEL_H__
#define __IOCHANNEL_H__

/*
 * Everything needed for IOPart-GuestPart communication is define in
 * this file. Note: Everything is OS-independent because this file is
 * used by Windows, Linux and possible EFI drivers.
 *
 * Communication flow between the IOPart and GuestPart uses the channel headers
 * channel state. The following states are currently being used:
 *       UNINIT(All Zeroes), CHANNEL_ATTACHING, CHANNEL_ATTACHED, CHANNEL_OPENED
 *
 * Additional states will be used later. No locking is needed to switch between
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
#include <linux/skbuff.h>
#include <linux/visorbus.h>

/*
 * Must increment these whenever you insert or delete fields within this channel
 * struct. Also increment whenever you change the meaning of fields within this
 * channel struct so as to break pre-existing software. Note that you can
 * usually add fields to the END of the channel struct without needing to
 * increment this.
 */
#define VISOR_VHBA_CHANNEL_VERSIONID 2
#define VISOR_VNIC_CHANNEL_VERSIONID 2

/*
 * Everything necessary to handle SCSI & NIC traffic between Guest Partition and
 * IO Partition is defined below.
 */

/*
 * Define the two queues per data channel between iopart and ioguestparts.
 *	IOCHAN_TO_IOPART -- used by guest to 'insert' signals to iopart.
 *	IOCHAN_FROM_IOPART -- used by guest to 'remove' signals from IO part.
 */
#define IOCHAN_TO_IOPART 0
#define IOCHAN_FROM_IOPART 1

/* Size of cdb - i.e., SCSI cmnd */
#define MAX_CMND_SIZE 16

/* Unisys-specific DMA direction values */
enum uis_dma_data_direction {
	UIS_DMA_BIDIRECTIONAL = 0,
	UIS_DMA_TO_DEVICE = 1,
	UIS_DMA_FROM_DEVICE = 2,
	UIS_DMA_NONE = 3
};

#define MAX_SENSE_SIZE 64
#define MAX_PHYS_INFO 64

/*
 * enum net_types - Various types of network packets that can be sent in cmdrsp.
 * @NET_RCV_POST:	Submit buffer to hold receiving incoming packet.
 * @NET_RCV:		visornic -> uisnic. Incoming packet received.
 * @NET_XMIT:		uisnic -> visornic. For outgoing packet.
 * @NET_XMIT_DONE:	visornic -> uisnic. Outgoing packet xmitted.
 * @NET_RCV_ENBDIS:	uisnic -> visornic. Enable/Disable packet reception.
 * @NET_RCV_ENBDIS_ACK:	visornic -> uisnic. Acknowledge enable/disable packet.
 * @NET_RCV_PROMISC:	uisnic -> visornic. Enable/Disable promiscuous mode.
 * @NET_CONNECT_STATUS:	visornic -> uisnic. Indicate the loss or restoration of
 *			a network connection.
 * @NET_MACADDR:	uisnic -> visornic. Indicates the client has requested
 *			to update it's MAC address.
 * @NET_MACADDR_ACK:	MAC address acknowledge.
 */
enum net_types {
	NET_RCV_POST = 0,
	NET_RCV,
	NET_XMIT,
	NET_XMIT_DONE,
	NET_RCV_ENBDIS,
	NET_RCV_ENBDIS_ACK,
	/* Reception */
	NET_RCV_PROMISC,
	NET_CONNECT_STATUS,
	NET_MACADDR,
	NET_MACADDR_ACK,
};

/* Minimum eth data size */
#define ETH_MIN_DATA_SIZE 46
#define ETH_MIN_PACKET_SIZE (ETH_HLEN + ETH_MIN_DATA_SIZE)

/* Maximum data size */
#define VISOR_ETH_MAX_MTU 16384

#ifndef MAX_MACADDR_LEN
/* Number of bytes in MAC address */
#define MAX_MACADDR_LEN 6
#endif

/* Various types of scsi task mgmt commands. */
enum task_mgmt_types {
	TASK_MGMT_ABORT_TASK = 1,
	TASK_MGMT_BUS_RESET,
	TASK_MGMT_LUN_RESET,
	TASK_MGMT_TARGET_RESET,
};

/* Various types of vdisk mgmt commands. */
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

/* Structs with pragma pack. */

struct guest_phys_info {
	u64 address;
	u64 length;
} __packed;

/*
 * struct uisscsi_dest
 * @channel: Bus number.
 * @id:      Target number.
 * @lun:     Logical unit number.
 */
struct uisscsi_dest {
	u32 channel;
	u32 id;
	u32 lun;
} __packed;

struct vhba_wwnn {
	u32 wwnn1;
	u32 wwnn2;
} __packed;

/*
 * struct vhba_config_max
 * @max_channel: Maximum channel for devices attached to this bus.
 * @max_id:	 Maximum SCSI ID for devices attached to bus.
 * @max_lun:	 Maximum SCSI LUN for devices attached to bus.
 * @cmd_per_lun: Maximum number of outstanding commands per LUN.
 * @max_io_size: Maximum io size for devices attached to this bus. Max io size
 *		 is often determined by the resource of the hba.
 *		 e.g Max scatter gather list length * page size / sector size.
 *
 * WARNING: Values stored in this structure must contain maximum counts (not
 * maximum values).
 *
 * 20 bytes
 */
struct vhba_config_max {
	u32 max_channel;
	u32 max_id;
	u32 max_lun;
	u32 cmd_per_lun;
	u32 max_io_size;
} __packed;

/*
 * struct uiscmdrsp_scsi
 *
 * @handle:		The handle to the cmd that was received. Send it back as
 *			is in the rsp packet.
 * @cmnd:		The cdb for the command.
 * @bufflen:		Length of data to be transferred out or in.
 * @guest_phys_entries:	Number of entries in scatter-gather list.
 * @struct gpi_list:	Physical address information for each fragment.
 * @data_dir:		Direction of the data, if any.
 * @struct vdest:	Identifies the virtual hba, id, channel, lun to which
 *			cmd was sent.
 * @linuxstat:		Original Linux status used by Linux vdisk.
 * @scsistat:		The scsi status.
 * @addlstat:		Non-scsi status.
 * @sensebuf:		Sense info in case cmd failed. sensebuf holds the
 *			sense_data struct. See sense_data struct for more
 *			details.
 * @*vdisk:		Pointer to the vdisk to clean up when IO completes.
 * @no_disk_result:	Used to return no disk inquiry result when
 *			no_disk_result is set to 1
 *			scsi.scsistat is SAM_STAT_GOOD
 *			scsi.addlstat is 0
 *			scsi.linuxstat is SAM_STAT_GOOD
 *			That is, there is NO error.
 */
struct uiscmdrsp_scsi {
	u64 handle;
	u8 cmnd[MAX_CMND_SIZE];
	u32 bufflen;
	u16 guest_phys_entries;
	struct guest_phys_info gpi_list[MAX_PHYS_INFO];
	u32 data_dir;
	struct uisscsi_dest vdest;
	/* Needed to queue the rsp back to cmd originator. */
	int linuxstat;
	u8 scsistat;
	u8 addlstat;
#define ADDL_SEL_TIMEOUT 4
	/* The following fields are need to determine the result of command. */
	u8 sensebuf[MAX_SENSE_SIZE];
	void *vdisk;
	int no_disk_result;
} __packed;

/*
 * Defines to support sending correct inquiry result when no disk is
 * configured.
 *
 * From SCSI SPC2 -
 *
 * If the target is not capable of supporting a device on this logical unit, the
 * device server shall set this field to 7Fh (PERIPHERAL QUALIFIER set to 011b
 * and PERIPHERAL DEVICE TYPE set to 1Fh).
 *
 * The device server is capable of supporting the specified peripheral device
 * type on this logical unit. However, the physical device is not currently
 * connected to this logical unit.
 */

/*
 * Peripheral qualifier of 0x3
 * Peripheral type of 0x1f
 * Specifies no device but target present
 */
#define DEV_NOT_CAPABLE 0x7f
/*
 * Peripheral qualifier of 0x1
 * Peripheral type of 0 - disk
 * Specifies device capable, but not present
 */
#define DEV_DISK_CAPABLE_NOT_PRESENT 0x20
/* HiSup = 1; shows support for report luns must be returned for lun 0. */
#define DEV_HISUPPORT 0x10

/*
 * Peripheral qualifier of 0x3
 * Peripheral type of 0x1f
 * Specifies no device but target present
 */
#define DEV_NOT_CAPABLE 0x7f
/*
 * Peripheral qualifier of 0x1
 * Peripheral type of 0 - disk
 * Specifies device capable, but not present
 */
#define DEV_DISK_CAPABLE_NOT_PRESENT 0x20
/* HiSup = 1; shows support for report luns must be returned for lun 0. */
#define DEV_HISUPPORT 0x10

/*
 * NOTE: Linux code assumes inquiry contains 36 bytes. Without checking length
 * in buf[4] some Linux code accesses bytes beyond 5 to retrieve vendor, product
 * and revision. Yikes! So let us always send back 36 bytes, the minimum for
 * inquiry result.
 */
#define NO_DISK_INQUIRY_RESULT_LEN 36
/* 5 bytes minimum for inquiry result */
#define MIN_INQUIRY_RESULT_LEN 5

/* SCSI device version for no disk inquiry result */
/* indicates SCSI SPC2 (SPC3 is 5) */
#define SCSI_SPC2_VER 4

/* Struct and Defines to support sense information. */

/*
 * The following struct is returned in sensebuf field in uiscmdrsp_scsi. It is
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

/*
 * struct net_pkt_xmt
 * @len:		    Full length of data in the packet.
 * @num_frags:		    Number of fragments in frags containing data.
 * @struct phys_info frags: Physical page information.
 * @ethhdr:		    The ethernet header.
 * @struct lincsum:	    These are needed for csum at uisnic end.
 *      @valid:	    1 = struct is valid - else ignore.
 *      @hrawoffv:  1 = hwrafoff is valid.
 *      @nhrawoffv: 1 = nhwrafoff is valid.
 *      @protocol:  Specifies packet protocol.
 *      @csum:	    Value used to set skb->csum at IOPart.
 *      @hrawoff:   Value used to set skb->h.raw at IOPart. hrawoff points to
 *		    the start of the TRANSPORT LAYER HEADER.
 *      @nhrawoff:  Value used to set skb->nh.raw at IOPart. nhrawoff points to
 *		    the start of the NETWORK LAYER HEADER.
 *
 * NOTE:
 * The full packet is described in frags but the ethernet header is separately
 * kept in ethhdr so that uisnic doesn't have "MAP" the guest memory to get to
 * the header. uisnic needs ethhdr to determine how to route the packet.
 */
struct net_pkt_xmt {
	int len;
	int num_frags;
	struct phys_info frags[MAX_PHYS_INFO];
	char ethhdr[ETH_HLEN];
	struct {
		u8 valid;
		u8 hrawoffv;
		u8 nhrawoffv;
		__be16 protocol;
		__wsum csum;
		u32 hrawoff;
		u32 nhrawoff;
	} lincsum;
} __packed;

struct net_pkt_xmtdone {
	/* Result of NET_XMIT */
	u32 xmt_done_result;
} __packed;

/*
 * RCVPOST_BUF_SIZE must be at most page_size(4096) - cache_line_size (64) The
 * reason is because dev_skb_alloc which is used to generate RCV_POST skbs in
 * visornic requires that there is "overhead" in the buffer, and pads 16 bytes.
 * Use 1 full cache line size for "overhead" so that transfers are optimized.
 * IOVM requires that a buffer be represented by 1 phys_info structure
 * which can only cover page_size.
 */
#define RCVPOST_BUF_SIZE 4032
#define MAX_NET_RCV_CHAIN \
	((VISOR_ETH_MAX_MTU + ETH_HLEN + RCVPOST_BUF_SIZE - 1) \
	 / RCVPOST_BUF_SIZE)

/* rcv buf size must be large enough to include ethernet data len + ethernet
 * header len - we are choosing 2K because it is guaranteed to be describable.
 */
struct net_pkt_rcvpost {
	/* Physical page information for the single fragment 2K rcv buf */
	struct phys_info frag;
	/*
	 * Ensures that receive posts are returned to the adapter which we sent
	 * them from originally.
	 */
	u64 unique_num;

} __packed;

/*
 * struct net_pkt_rcv
 * @rcv_done_len:	Length of the received data.
 * @numrcvbufs:		Contains the incoming data. Guest side MUST chain these
 *			together.
 * @*rcvbuf:		List of chained rcvbufa. Each entry is a receive buffer
 *			provided by NET_RCV_POST. NOTE: First rcvbuf in the
 *			chain will also be provided in net.buf.
 * @unique_num:
 * @rcvs_dropped_delta:
 *
 * The number of rcvbuf that can be chained is based on max mtu and size of each
 * rcvbuf.
 */
struct net_pkt_rcv {
	u32 rcv_done_len;
	u8 numrcvbufs;
	void *rcvbuf[MAX_NET_RCV_CHAIN];
	u64 unique_num;
	u32 rcvs_dropped_delta;
} __packed;

struct net_pkt_enbdis {
	void *context;
	/* 1 = enable, 0 = disable */
	u16 enable;
} __packed;

struct net_pkt_macaddr {
	void *context;
	/* 6 bytes */
	u8 macaddr[MAX_MACADDR_LEN];
} __packed;

/*
 * struct uiscmdrsp_net - cmd rsp packet used for VNIC network traffic.
 * @enum type:
 * @*buf:
 * @union:
 *	@struct xmt:	 Used for NET_XMIT.
 *	@struct xmtdone: Used for NET_XMIT_DONE.
 *	@struct rcvpost: Used for NET_RCV_POST.
 *	@struct rcv:	 Used for NET_RCV.
 *	@struct enbdis:	 Used for NET_RCV_ENBDIS, NET_RCV_ENBDIS_ACK,
 *			 NET_RCV_PROMSIC, and NET_CONNECT_STATUS.
 *	@struct macaddr:
 */
struct uiscmdrsp_net {
	enum net_types type;
	void *buf;
	union {
		struct net_pkt_xmt xmt;
		struct net_pkt_xmtdone xmtdone;
		struct net_pkt_rcvpost rcvpost;
		struct net_pkt_rcv rcv;
		struct net_pkt_enbdis enbdis;
		struct net_pkt_macaddr macaddr;
	};
} __packed;

/*
 * struct uiscmdrsp_scsitaskmgmt
 * @enum tasktype:	 The type of task.
 * @struct vdest:	 The vdisk for which this task mgmt is generated.
 * @handle:		 This is a handle that the guest has saved off for its
 *			 own use. The handle value is preserved by iopart and
 *			 returned as in task mgmt rsp.
 * @notify_handle:	 For Linux guests, this is a pointer to wait_queue_head
 *			 that a thread is waiting on to see if the taskmgmt
 *			 command has completed. When the rsp is received by
 *			 guest, the thread receiving the response uses this to
 *			 notify the thread waiting for taskmgmt command
 *			 completion. It's value is preserved by iopart and
 *			 returned as in the task mgmt rsp.
 * @notifyresult_handle: This is a handle to the location in the guest where
 *			 the result of the taskmgmt command (result field) is
 *			 saved to when the response is handled. It's value is
 *			 preserved by iopart and returned as is in the task mgmt
 *			 rsp.
 * @result:		 Result of taskmgmt command - set by IOPart.
 */
struct uiscmdrsp_scsitaskmgmt {
	enum task_mgmt_types tasktype;
	struct uisscsi_dest vdest;
	u64 handle;
	u64 notify_handle;
	u64 notifyresult_handle;
	char result;

#define TASK_MGMT_FAILED 0
} __packed;

/*
 * struct uiscmdrsp_disknotify - Used by uissd to send disk add/remove
 *				 notifications to Guest.
 * @add:     0-remove, 1-add.
 * @*v_hba:  Channel info to route msg.
 * @channel: SCSI Path of Disk to added or removed.
 * @id:	     SCSI Path of Disk to added or removed.
 * @lun:     SCSI Path of Disk to added or removed.
 *
 * Note that the vHba pointer is not used by the Client/Guest side.
 */
struct uiscmdrsp_disknotify {
	u8 add;
	void *v_hba;
	u32 channel, id, lun;
} __packed;

/* Keeping cmd and rsp info in one structure for now cmd rsp packet for SCSI */
struct uiscmdrsp {
	char cmdtype;
	/* Describes what type of information is in the struct */
#define CMD_SCSI_TYPE	      1
#define CMD_NET_TYPE	      2
#define CMD_SCSITASKMGMT_TYPE 3
#define CMD_NOTIFYGUEST_TYPE  4
	union {
		struct uiscmdrsp_scsi scsi;
		struct uiscmdrsp_net net;
		struct uiscmdrsp_scsitaskmgmt scsitaskmgmt;
		struct uiscmdrsp_disknotify disknotify;
	};
	/* Send the response when the cmd is done (scsi and scsittaskmgmt). */
	void *private_data;
	/* General Purpose Queue Link */
	struct uiscmdrsp *next;
	/* Pointer to the nextactive commands */
	struct uiscmdrsp *activeQ_next;
	/* Pointer to the prevactive commands */
	struct uiscmdrsp *activeQ_prev;
} __packed;

/* total = 28 bytes */
struct iochannel_vhba {
	/* 8 bytes */
	struct vhba_wwnn wwnn;
	/* 20 bytes */
	struct vhba_config_max max;
} __packed;

struct iochannel_vnic {
	/* 6 bytes */
	u8 macaddr[6];
	/* 4 bytes */
	u32 num_rcv_bufs;
	/* 4 bytes */
	u32 mtu;
	/* 16 bytes */
	guid_t zone_guid;
} __packed;

/*
 * This is just the header of the IO channel. It is assumed that directly after
 * this header there is a large region of memory which contains the command and
 * response queues as specified in cmd_q and rsp_q SIGNAL_QUEUE_HEADERS.
 */
struct visor_io_channel {
	struct channel_header channel_header;
	struct signal_queue_header cmd_q;
	struct signal_queue_header rsp_q;
	union {
		struct iochannel_vhba vhba;
		struct iochannel_vnic vnic;
	} __packed;

#define MAX_CLIENTSTRING_LEN 1024
	/* client_string is NULL termimated so holds max-1 bytes */
	 u8 client_string[MAX_CLIENTSTRING_LEN];
} __packed;

/* INLINE functions for initializing and accessing I/O data channels. */
#define SIZEOF_CMDRSP (64 * DIV_ROUND_UP(sizeof(struct uiscmdrsp), 64))

/* Use 4K page sizes when passing page info between Guest and IOPartition. */
#define PI_PAGE_SIZE 0x1000
#define PI_PAGE_MASK 0x0FFF

/* __IOCHANNEL_H__ */
#endif
