// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2009, Microsoft Corporation.
 *
 * Authors:
 *   Haiyang Zhang <haiyangz@microsoft.com>
 *   Hank Janssen  <hjanssen@microsoft.com>
 *   K. Y. Srinivasan <kys@microsoft.com>
 */

#include <linux/kernel.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/completion.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/hyperv.h>
#include <linux/blkdev.h>
#include <linux/dma-mapping.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_tcq.h>
#include <scsi/scsi_eh.h>
#include <scsi/scsi_devinfo.h>
#include <scsi/scsi_dbg.h>
#include <scsi/scsi_transport_fc.h>
#include <scsi/scsi_transport.h>

/*
 * All wire protocol details (storage protocol between the guest and the host)
 * are consolidated here.
 *
 * Begin protocol definitions.
 */

/*
 * Version history:
 * V1 Beta: 0.1
 * V1 RC < 2008/1/31: 1.0
 * V1 RC > 2008/1/31:  2.0
 * Win7: 4.2
 * Win8: 5.1
 * Win8.1: 6.0
 * Win10: 6.2
 */

#define VMSTOR_PROTO_VERSION(MAJOR_, MINOR_)	((((MAJOR_) & 0xff) << 8) | \
						(((MINOR_) & 0xff)))
#define VMSTOR_PROTO_VERSION_WIN6	VMSTOR_PROTO_VERSION(2, 0)
#define VMSTOR_PROTO_VERSION_WIN7	VMSTOR_PROTO_VERSION(4, 2)
#define VMSTOR_PROTO_VERSION_WIN8	VMSTOR_PROTO_VERSION(5, 1)
#define VMSTOR_PROTO_VERSION_WIN8_1	VMSTOR_PROTO_VERSION(6, 0)
#define VMSTOR_PROTO_VERSION_WIN10	VMSTOR_PROTO_VERSION(6, 2)

/* channel callback timeout in ms */
#define CALLBACK_TIMEOUT               2

/*  Packet structure describing virtual storage requests. */
enum vstor_packet_operation {
	VSTOR_OPERATION_COMPLETE_IO		= 1,
	VSTOR_OPERATION_REMOVE_DEVICE		= 2,
	VSTOR_OPERATION_EXECUTE_SRB		= 3,
	VSTOR_OPERATION_RESET_LUN		= 4,
	VSTOR_OPERATION_RESET_ADAPTER		= 5,
	VSTOR_OPERATION_RESET_BUS		= 6,
	VSTOR_OPERATION_BEGIN_INITIALIZATION	= 7,
	VSTOR_OPERATION_END_INITIALIZATION	= 8,
	VSTOR_OPERATION_QUERY_PROTOCOL_VERSION	= 9,
	VSTOR_OPERATION_QUERY_PROPERTIES	= 10,
	VSTOR_OPERATION_ENUMERATE_BUS		= 11,
	VSTOR_OPERATION_FCHBA_DATA              = 12,
	VSTOR_OPERATION_CREATE_SUB_CHANNELS     = 13,
	VSTOR_OPERATION_MAXIMUM                 = 13
};

/*
 * WWN packet for Fibre Channel HBA
 */

struct hv_fc_wwn_packet {
	u8	primary_active;
	u8	reserved1[3];
	u8	primary_port_wwn[8];
	u8	primary_node_wwn[8];
	u8	secondary_port_wwn[8];
	u8	secondary_node_wwn[8];
};



/*
 * SRB Flag Bits
 */

#define SRB_FLAGS_QUEUE_ACTION_ENABLE		0x00000002
#define SRB_FLAGS_DISABLE_DISCONNECT		0x00000004
#define SRB_FLAGS_DISABLE_SYNCH_TRANSFER	0x00000008
#define SRB_FLAGS_BYPASS_FROZEN_QUEUE		0x00000010
#define SRB_FLAGS_DISABLE_AUTOSENSE		0x00000020
#define SRB_FLAGS_DATA_IN			0x00000040
#define SRB_FLAGS_DATA_OUT			0x00000080
#define SRB_FLAGS_NO_DATA_TRANSFER		0x00000000
#define SRB_FLAGS_UNSPECIFIED_DIRECTION	(SRB_FLAGS_DATA_IN | SRB_FLAGS_DATA_OUT)
#define SRB_FLAGS_NO_QUEUE_FREEZE		0x00000100
#define SRB_FLAGS_ADAPTER_CACHE_ENABLE		0x00000200
#define SRB_FLAGS_FREE_SENSE_BUFFER		0x00000400

/*
 * This flag indicates the request is part of the workflow for processing a D3.
 */
#define SRB_FLAGS_D3_PROCESSING			0x00000800
#define SRB_FLAGS_IS_ACTIVE			0x00010000
#define SRB_FLAGS_ALLOCATED_FROM_ZONE		0x00020000
#define SRB_FLAGS_SGLIST_FROM_POOL		0x00040000
#define SRB_FLAGS_BYPASS_LOCKED_QUEUE		0x00080000
#define SRB_FLAGS_NO_KEEP_AWAKE			0x00100000
#define SRB_FLAGS_PORT_DRIVER_ALLOCSENSE	0x00200000
#define SRB_FLAGS_PORT_DRIVER_SENSEHASPORT	0x00400000
#define SRB_FLAGS_DONT_START_NEXT_PACKET	0x00800000
#define SRB_FLAGS_PORT_DRIVER_RESERVED		0x0F000000
#define SRB_FLAGS_CLASS_DRIVER_RESERVED		0xF0000000

#define SP_UNTAGGED			((unsigned char) ~0)
#define SRB_SIMPLE_TAG_REQUEST		0x20

/*
 * Platform neutral description of a scsi request -
 * this remains the same across the write regardless of 32/64 bit
 * note: it's patterned off the SCSI_PASS_THROUGH structure
 */
#define STORVSC_MAX_CMD_LEN			0x10

/* Sense buffer size is the same for all versions since Windows 8 */
#define STORVSC_SENSE_BUFFER_SIZE		0x14
#define STORVSC_MAX_BUF_LEN_WITH_PADDING	0x14

/*
 * The storage protocol version is determined during the
 * initial exchange with the host.  It will indicate which
 * storage functionality is available in the host.
*/
static int vmstor_proto_version;

#define STORVSC_LOGGING_NONE	0
#define STORVSC_LOGGING_ERROR	1
#define STORVSC_LOGGING_WARN	2

static int logging_level = STORVSC_LOGGING_ERROR;
module_param(logging_level, int, S_IRUGO|S_IWUSR);
MODULE_PARM_DESC(logging_level,
	"Logging level, 0 - None, 1 - Error (default), 2 - Warning.");

static inline bool do_logging(int level)
{
	return logging_level >= level;
}

#define storvsc_log(dev, level, fmt, ...)			\
do {								\
	if (do_logging(level))					\
		dev_warn(&(dev)->device, fmt, ##__VA_ARGS__);	\
} while (0)

struct vmscsi_request {
	u16 length;
	u8 srb_status;
	u8 scsi_status;

	u8  port_number;
	u8  path_id;
	u8  target_id;
	u8  lun;

	u8  cdb_length;
	u8  sense_info_length;
	u8  data_in;
	u8  reserved;

	u32 data_transfer_length;

	union {
		u8 cdb[STORVSC_MAX_CMD_LEN];
		u8 sense_data[STORVSC_SENSE_BUFFER_SIZE];
		u8 reserved_array[STORVSC_MAX_BUF_LEN_WITH_PADDING];
	};
	/*
	 * The following was added in win8.
	 */
	u16 reserve;
	u8  queue_tag;
	u8  queue_action;
	u32 srb_flags;
	u32 time_out_value;
	u32 queue_sort_ey;

} __attribute((packed));

/*
 * The list of windows version in order of preference.
 */

static const int protocol_version[] = {
		VMSTOR_PROTO_VERSION_WIN10,
		VMSTOR_PROTO_VERSION_WIN8_1,
		VMSTOR_PROTO_VERSION_WIN8,
};


/*
 * This structure is sent during the initialization phase to get the different
 * properties of the channel.
 */

#define STORAGE_CHANNEL_SUPPORTS_MULTI_CHANNEL		0x1

struct vmstorage_channel_properties {
	u32 reserved;
	u16 max_channel_cnt;
	u16 reserved1;

	u32 flags;
	u32   max_transfer_bytes;

	u64  reserved2;
} __packed;

/*  This structure is sent during the storage protocol negotiations. */
struct vmstorage_protocol_version {
	/* Major (MSW) and minor (LSW) version numbers. */
	u16 major_minor;

	/*
	 * Revision number is auto-incremented whenever this file is changed
	 * (See FILL_VMSTOR_REVISION macro above).  Mismatch does not
	 * definitely indicate incompatibility--but it does indicate mismatched
	 * builds.
	 * This is only used on the windows side. Just set it to 0.
	 */
	u16 revision;
} __packed;

/* Channel Property Flags */
#define STORAGE_CHANNEL_REMOVABLE_FLAG		0x1
#define STORAGE_CHANNEL_EMULATED_IDE_FLAG	0x2

struct vstor_packet {
	/* Requested operation type */
	enum vstor_packet_operation operation;

	/*  Flags - see below for values */
	u32 flags;

	/* Status of the request returned from the server side. */
	u32 status;

	/* Data payload area */
	union {
		/*
		 * Structure used to forward SCSI commands from the
		 * client to the server.
		 */
		struct vmscsi_request vm_srb;

		/* Structure used to query channel properties. */
		struct vmstorage_channel_properties storage_channel_properties;

		/* Used during version negotiations. */
		struct vmstorage_protocol_version version;

		/* Fibre channel address packet */
		struct hv_fc_wwn_packet wwn_packet;

		/* Number of sub-channels to create */
		u16 sub_channel_count;

		/* This will be the maximum of the union members */
		u8  buffer[0x34];
	};
} __packed;

/*
 * Packet Flags:
 *
 * This flag indicates that the server should send back a completion for this
 * packet.
 */

#define REQUEST_COMPLETION_FLAG	0x1

/* Matches Windows-end */
enum storvsc_request_type {
	WRITE_TYPE = 0,
	READ_TYPE,
	UNKNOWN_TYPE,
};

/*
 * SRB status codes and masks. In the 8-bit field, the two high order bits
 * are flags, while the remaining 6 bits are an integer status code.  The
 * definitions here include only the subset of the integer status codes that
 * are tested for in this driver.
 */
#define SRB_STATUS_AUTOSENSE_VALID	0x80
#define SRB_STATUS_QUEUE_FROZEN		0x40

/* SRB status integer codes */
#define SRB_STATUS_SUCCESS		0x01
#define SRB_STATUS_ABORTED		0x02
#define SRB_STATUS_ERROR		0x04
#define SRB_STATUS_INVALID_REQUEST	0x06
#define SRB_STATUS_DATA_OVERRUN		0x12
#define SRB_STATUS_INVALID_LUN		0x20
#define SRB_STATUS_INTERNAL_ERROR	0x30

#define SRB_STATUS(status) \
	(status & ~(SRB_STATUS_AUTOSENSE_VALID | SRB_STATUS_QUEUE_FROZEN))
/*
 * This is the end of Protocol specific defines.
 */

static int storvsc_ringbuffer_size = (128 * 1024);
static u32 max_outstanding_req_per_channel;
static int storvsc_change_queue_depth(struct scsi_device *sdev, int queue_depth);

static int storvsc_vcpus_per_sub_channel = 4;
static unsigned int storvsc_max_hw_queues;

module_param(storvsc_ringbuffer_size, int, S_IRUGO);
MODULE_PARM_DESC(storvsc_ringbuffer_size, "Ring buffer size (bytes)");

module_param(storvsc_max_hw_queues, uint, 0644);
MODULE_PARM_DESC(storvsc_max_hw_queues, "Maximum number of hardware queues");

module_param(storvsc_vcpus_per_sub_channel, int, S_IRUGO);
MODULE_PARM_DESC(storvsc_vcpus_per_sub_channel, "Ratio of VCPUs to subchannels");

static int ring_avail_percent_lowater = 10;
module_param(ring_avail_percent_lowater, int, S_IRUGO);
MODULE_PARM_DESC(ring_avail_percent_lowater,
		"Select a channel if available ring size > this in percent");

/*
 * Timeout in seconds for all devices managed by this driver.
 */
static int storvsc_timeout = 180;

#if IS_ENABLED(CONFIG_SCSI_FC_ATTRS)
static struct scsi_transport_template *fc_transport_template;
#endif

static struct scsi_host_template scsi_driver;
static void storvsc_on_channel_callback(void *context);

#define STORVSC_MAX_LUNS_PER_TARGET			255
#define STORVSC_MAX_TARGETS				2
#define STORVSC_MAX_CHANNELS				8

#define STORVSC_FC_MAX_LUNS_PER_TARGET			255
#define STORVSC_FC_MAX_TARGETS				128
#define STORVSC_FC_MAX_CHANNELS				8
#define STORVSC_FC_MAX_XFER_SIZE			((u32)(512 * 1024))

#define STORVSC_IDE_MAX_LUNS_PER_TARGET			64
#define STORVSC_IDE_MAX_TARGETS				1
#define STORVSC_IDE_MAX_CHANNELS			1

/*
 * Upper bound on the size of a storvsc packet.
 */
#define STORVSC_MAX_PKT_SIZE (sizeof(struct vmpacket_descriptor) +\
			      sizeof(struct vstor_packet))

struct storvsc_cmd_request {
	struct scsi_cmnd *cmd;

	struct hv_device *device;

	/* Synchronize the request/response if needed */
	struct completion wait_event;

	struct vmbus_channel_packet_multipage_buffer mpb;
	struct vmbus_packet_mpb_array *payload;
	u32 payload_sz;

	struct vstor_packet vstor_packet;
};


/* A storvsc device is a device object that contains a vmbus channel */
struct storvsc_device {
	struct hv_device *device;

	bool	 destroy;
	bool	 drain_notify;
	atomic_t num_outstanding_req;
	struct Scsi_Host *host;

	wait_queue_head_t waiting_to_drain;

	/*
	 * Each unique Port/Path/Target represents 1 channel ie scsi
	 * controller. In reality, the pathid, targetid is always 0
	 * and the port is set by us
	 */
	unsigned int port_number;
	unsigned char path_id;
	unsigned char target_id;

	/*
	 * Max I/O, the device can support.
	 */
	u32   max_transfer_bytes;
	/*
	 * Number of sub-channels we will open.
	 */
	u16 num_sc;
	struct vmbus_channel **stor_chns;
	/*
	 * Mask of CPUs bound to subchannels.
	 */
	struct cpumask alloced_cpus;
	/*
	 * Serializes modifications of stor_chns[] from storvsc_do_io()
	 * and storvsc_change_target_cpu().
	 */
	spinlock_t lock;
	/* Used for vsc/vsp channel reset process */
	struct storvsc_cmd_request init_request;
	struct storvsc_cmd_request reset_request;
	/*
	 * Currently active port and node names for FC devices.
	 */
	u64 node_name;
	u64 port_name;
#if IS_ENABLED(CONFIG_SCSI_FC_ATTRS)
	struct fc_rport *rport;
#endif
};

struct hv_host_device {
	struct hv_device *dev;
	unsigned int port;
	unsigned char path;
	unsigned char target;
	struct workqueue_struct *handle_error_wq;
	struct work_struct host_scan_work;
	struct Scsi_Host *host;
};

struct storvsc_scan_work {
	struct work_struct work;
	struct Scsi_Host *host;
	u8 lun;
	u8 tgt_id;
};

static void storvsc_device_scan(struct work_struct *work)
{
	struct storvsc_scan_work *wrk;
	struct scsi_device *sdev;

	wrk = container_of(work, struct storvsc_scan_work, work);

	sdev = scsi_device_lookup(wrk->host, 0, wrk->tgt_id, wrk->lun);
	if (!sdev)
		goto done;
	scsi_rescan_device(&sdev->sdev_gendev);
	scsi_device_put(sdev);

done:
	kfree(wrk);
}

static void storvsc_host_scan(struct work_struct *work)
{
	struct Scsi_Host *host;
	struct scsi_device *sdev;
	struct hv_host_device *host_device =
		container_of(work, struct hv_host_device, host_scan_work);

	host = host_device->host;
	/*
	 * Before scanning the host, first check to see if any of the
	 * currently known devices have been hot removed. We issue a
	 * "unit ready" command against all currently known devices.
	 * This I/O will result in an error for devices that have been
	 * removed. As part of handling the I/O error, we remove the device.
	 *
	 * When a LUN is added or removed, the host sends us a signal to
	 * scan the host. Thus we are forced to discover the LUNs that
	 * may have been removed this way.
	 */
	mutex_lock(&host->scan_mutex);
	shost_for_each_device(sdev, host)
		scsi_test_unit_ready(sdev, 1, 1, NULL);
	mutex_unlock(&host->scan_mutex);
	/*
	 * Now scan the host to discover LUNs that may have been added.
	 */
	scsi_scan_host(host);
}

static void storvsc_remove_lun(struct work_struct *work)
{
	struct storvsc_scan_work *wrk;
	struct scsi_device *sdev;

	wrk = container_of(work, struct storvsc_scan_work, work);
	if (!scsi_host_get(wrk->host))
		goto done;

	sdev = scsi_device_lookup(wrk->host, 0, wrk->tgt_id, wrk->lun);

	if (sdev) {
		scsi_remove_device(sdev);
		scsi_device_put(sdev);
	}
	scsi_host_put(wrk->host);

done:
	kfree(wrk);
}


/*
 * We can get incoming messages from the host that are not in response to
 * messages that we have sent out. An example of this would be messages
 * received by the guest to notify dynamic addition/removal of LUNs. To
 * deal with potential race conditions where the driver may be in the
 * midst of being unloaded when we might receive an unsolicited message
 * from the host, we have implemented a mechanism to gurantee sequential
 * consistency:
 *
 * 1) Once the device is marked as being destroyed, we will fail all
 *    outgoing messages.
 * 2) We permit incoming messages when the device is being destroyed,
 *    only to properly account for messages already sent out.
 */

static inline struct storvsc_device *get_out_stor_device(
					struct hv_device *device)
{
	struct storvsc_device *stor_device;

	stor_device = hv_get_drvdata(device);

	if (stor_device && stor_device->destroy)
		stor_device = NULL;

	return stor_device;
}


static inline void storvsc_wait_to_drain(struct storvsc_device *dev)
{
	dev->drain_notify = true;
	wait_event(dev->waiting_to_drain,
		   atomic_read(&dev->num_outstanding_req) == 0);
	dev->drain_notify = false;
}

static inline struct storvsc_device *get_in_stor_device(
					struct hv_device *device)
{
	struct storvsc_device *stor_device;

	stor_device = hv_get_drvdata(device);

	if (!stor_device)
		goto get_in_err;

	/*
	 * If the device is being destroyed; allow incoming
	 * traffic only to cleanup outstanding requests.
	 */

	if (stor_device->destroy  &&
		(atomic_read(&stor_device->num_outstanding_req) == 0))
		stor_device = NULL;

get_in_err:
	return stor_device;

}

static void storvsc_change_target_cpu(struct vmbus_channel *channel, u32 old,
				      u32 new)
{
	struct storvsc_device *stor_device;
	struct vmbus_channel *cur_chn;
	bool old_is_alloced = false;
	struct hv_device *device;
	unsigned long flags;
	int cpu;

	device = channel->primary_channel ?
			channel->primary_channel->device_obj
				: channel->device_obj;
	stor_device = get_out_stor_device(device);
	if (!stor_device)
		return;

	/* See storvsc_do_io() -> get_og_chn(). */
	spin_lock_irqsave(&stor_device->lock, flags);

	/*
	 * Determines if the storvsc device has other channels assigned to
	 * the "old" CPU to update the alloced_cpus mask and the stor_chns
	 * array.
	 */
	if (device->channel != channel && device->channel->target_cpu == old) {
		cur_chn = device->channel;
		old_is_alloced = true;
		goto old_is_alloced;
	}
	list_for_each_entry(cur_chn, &device->channel->sc_list, sc_list) {
		if (cur_chn == channel)
			continue;
		if (cur_chn->target_cpu == old) {
			old_is_alloced = true;
			goto old_is_alloced;
		}
	}

old_is_alloced:
	if (old_is_alloced)
		WRITE_ONCE(stor_device->stor_chns[old], cur_chn);
	else
		cpumask_clear_cpu(old, &stor_device->alloced_cpus);

	/* "Flush" the stor_chns array. */
	for_each_possible_cpu(cpu) {
		if (stor_device->stor_chns[cpu] && !cpumask_test_cpu(
					cpu, &stor_device->alloced_cpus))
			WRITE_ONCE(stor_device->stor_chns[cpu], NULL);
	}

	WRITE_ONCE(stor_device->stor_chns[new], channel);
	cpumask_set_cpu(new, &stor_device->alloced_cpus);

	spin_unlock_irqrestore(&stor_device->lock, flags);
}

static u64 storvsc_next_request_id(struct vmbus_channel *channel, u64 rqst_addr)
{
	struct storvsc_cmd_request *request =
		(struct storvsc_cmd_request *)(unsigned long)rqst_addr;

	if (rqst_addr == VMBUS_RQST_INIT)
		return VMBUS_RQST_INIT;
	if (rqst_addr == VMBUS_RQST_RESET)
		return VMBUS_RQST_RESET;

	/*
	 * Cannot return an ID of 0, which is reserved for an unsolicited
	 * message from Hyper-V.
	 */
	return (u64)blk_mq_unique_tag(scsi_cmd_to_rq(request->cmd)) + 1;
}

static void handle_sc_creation(struct vmbus_channel *new_sc)
{
	struct hv_device *device = new_sc->primary_channel->device_obj;
	struct device *dev = &device->device;
	struct storvsc_device *stor_device;
	struct vmstorage_channel_properties props;
	int ret;

	stor_device = get_out_stor_device(device);
	if (!stor_device)
		return;

	memset(&props, 0, sizeof(struct vmstorage_channel_properties));
	new_sc->max_pkt_size = STORVSC_MAX_PKT_SIZE;

	new_sc->next_request_id_callback = storvsc_next_request_id;

	ret = vmbus_open(new_sc,
			 storvsc_ringbuffer_size,
			 storvsc_ringbuffer_size,
			 (void *)&props,
			 sizeof(struct vmstorage_channel_properties),
			 storvsc_on_channel_callback, new_sc);

	/* In case vmbus_open() fails, we don't use the sub-channel. */
	if (ret != 0) {
		dev_err(dev, "Failed to open sub-channel: err=%d\n", ret);
		return;
	}

	new_sc->change_target_cpu_callback = storvsc_change_target_cpu;

	/* Add the sub-channel to the array of available channels. */
	stor_device->stor_chns[new_sc->target_cpu] = new_sc;
	cpumask_set_cpu(new_sc->target_cpu, &stor_device->alloced_cpus);
}

static void  handle_multichannel_storage(struct hv_device *device, int max_chns)
{
	struct device *dev = &device->device;
	struct storvsc_device *stor_device;
	int num_sc;
	struct storvsc_cmd_request *request;
	struct vstor_packet *vstor_packet;
	int ret, t;

	/*
	 * If the number of CPUs is artificially restricted, such as
	 * with maxcpus=1 on the kernel boot line, Hyper-V could offer
	 * sub-channels >= the number of CPUs. These sub-channels
	 * should not be created. The primary channel is already created
	 * and assigned to one CPU, so check against # CPUs - 1.
	 */
	num_sc = min((int)(num_online_cpus() - 1), max_chns);
	if (!num_sc)
		return;

	stor_device = get_out_stor_device(device);
	if (!stor_device)
		return;

	stor_device->num_sc = num_sc;
	request = &stor_device->init_request;
	vstor_packet = &request->vstor_packet;

	/*
	 * Establish a handler for dealing with subchannels.
	 */
	vmbus_set_sc_create_callback(device->channel, handle_sc_creation);

	/*
	 * Request the host to create sub-channels.
	 */
	memset(request, 0, sizeof(struct storvsc_cmd_request));
	init_completion(&request->wait_event);
	vstor_packet->operation = VSTOR_OPERATION_CREATE_SUB_CHANNELS;
	vstor_packet->flags = REQUEST_COMPLETION_FLAG;
	vstor_packet->sub_channel_count = num_sc;

	ret = vmbus_sendpacket(device->channel, vstor_packet,
			       sizeof(struct vstor_packet),
			       VMBUS_RQST_INIT,
			       VM_PKT_DATA_INBAND,
			       VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);

	if (ret != 0) {
		dev_err(dev, "Failed to create sub-channel: err=%d\n", ret);
		return;
	}

	t = wait_for_completion_timeout(&request->wait_event, 10*HZ);
	if (t == 0) {
		dev_err(dev, "Failed to create sub-channel: timed out\n");
		return;
	}

	if (vstor_packet->operation != VSTOR_OPERATION_COMPLETE_IO ||
	    vstor_packet->status != 0) {
		dev_err(dev, "Failed to create sub-channel: op=%d, sts=%d\n",
			vstor_packet->operation, vstor_packet->status);
		return;
	}

	/*
	 * We need to do nothing here, because vmbus_process_offer()
	 * invokes channel->sc_creation_callback, which will open and use
	 * the sub-channel(s).
	 */
}

static void cache_wwn(struct storvsc_device *stor_device,
		      struct vstor_packet *vstor_packet)
{
	/*
	 * Cache the currently active port and node ww names.
	 */
	if (vstor_packet->wwn_packet.primary_active) {
		stor_device->node_name =
			wwn_to_u64(vstor_packet->wwn_packet.primary_node_wwn);
		stor_device->port_name =
			wwn_to_u64(vstor_packet->wwn_packet.primary_port_wwn);
	} else {
		stor_device->node_name =
			wwn_to_u64(vstor_packet->wwn_packet.secondary_node_wwn);
		stor_device->port_name =
			wwn_to_u64(vstor_packet->wwn_packet.secondary_port_wwn);
	}
}


static int storvsc_execute_vstor_op(struct hv_device *device,
				    struct storvsc_cmd_request *request,
				    bool status_check)
{
	struct storvsc_device *stor_device;
	struct vstor_packet *vstor_packet;
	int ret, t;

	stor_device = get_out_stor_device(device);
	if (!stor_device)
		return -ENODEV;

	vstor_packet = &request->vstor_packet;

	init_completion(&request->wait_event);
	vstor_packet->flags = REQUEST_COMPLETION_FLAG;

	ret = vmbus_sendpacket(device->channel, vstor_packet,
			       sizeof(struct vstor_packet),
			       VMBUS_RQST_INIT,
			       VM_PKT_DATA_INBAND,
			       VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);
	if (ret != 0)
		return ret;

	t = wait_for_completion_timeout(&request->wait_event, 5*HZ);
	if (t == 0)
		return -ETIMEDOUT;

	if (!status_check)
		return ret;

	if (vstor_packet->operation != VSTOR_OPERATION_COMPLETE_IO ||
	    vstor_packet->status != 0)
		return -EINVAL;

	return ret;
}

static int storvsc_channel_init(struct hv_device *device, bool is_fc)
{
	struct storvsc_device *stor_device;
	struct storvsc_cmd_request *request;
	struct vstor_packet *vstor_packet;
	int ret, i;
	int max_chns;
	bool process_sub_channels = false;

	stor_device = get_out_stor_device(device);
	if (!stor_device)
		return -ENODEV;

	request = &stor_device->init_request;
	vstor_packet = &request->vstor_packet;

	/*
	 * Now, initiate the vsc/vsp initialization protocol on the open
	 * channel
	 */
	memset(request, 0, sizeof(struct storvsc_cmd_request));
	vstor_packet->operation = VSTOR_OPERATION_BEGIN_INITIALIZATION;
	ret = storvsc_execute_vstor_op(device, request, true);
	if (ret)
		return ret;
	/*
	 * Query host supported protocol version.
	 */

	for (i = 0; i < ARRAY_SIZE(protocol_version); i++) {
		/* reuse the packet for version range supported */
		memset(vstor_packet, 0, sizeof(struct vstor_packet));
		vstor_packet->operation =
			VSTOR_OPERATION_QUERY_PROTOCOL_VERSION;

		vstor_packet->version.major_minor = protocol_version[i];

		/*
		 * The revision number is only used in Windows; set it to 0.
		 */
		vstor_packet->version.revision = 0;
		ret = storvsc_execute_vstor_op(device, request, false);
		if (ret != 0)
			return ret;

		if (vstor_packet->operation != VSTOR_OPERATION_COMPLETE_IO)
			return -EINVAL;

		if (vstor_packet->status == 0) {
			vmstor_proto_version = protocol_version[i];

			break;
		}
	}

	if (vstor_packet->status != 0) {
		dev_err(&device->device, "Obsolete Hyper-V version\n");
		return -EINVAL;
	}


	memset(vstor_packet, 0, sizeof(struct vstor_packet));
	vstor_packet->operation = VSTOR_OPERATION_QUERY_PROPERTIES;
	ret = storvsc_execute_vstor_op(device, request, true);
	if (ret != 0)
		return ret;

	/*
	 * Check to see if multi-channel support is there.
	 * Hosts that implement protocol version of 5.1 and above
	 * support multi-channel.
	 */
	max_chns = vstor_packet->storage_channel_properties.max_channel_cnt;

	/*
	 * Allocate state to manage the sub-channels.
	 * We allocate an array based on the numbers of possible CPUs
	 * (Hyper-V does not support cpu online/offline).
	 * This Array will be sparseley populated with unique
	 * channels - primary + sub-channels.
	 * We will however populate all the slots to evenly distribute
	 * the load.
	 */
	stor_device->stor_chns = kcalloc(num_possible_cpus(), sizeof(void *),
					 GFP_KERNEL);
	if (stor_device->stor_chns == NULL)
		return -ENOMEM;

	device->channel->change_target_cpu_callback = storvsc_change_target_cpu;

	stor_device->stor_chns[device->channel->target_cpu] = device->channel;
	cpumask_set_cpu(device->channel->target_cpu,
			&stor_device->alloced_cpus);

	if (vstor_packet->storage_channel_properties.flags &
	    STORAGE_CHANNEL_SUPPORTS_MULTI_CHANNEL)
		process_sub_channels = true;

	stor_device->max_transfer_bytes =
		vstor_packet->storage_channel_properties.max_transfer_bytes;

	if (!is_fc)
		goto done;

	/*
	 * For FC devices retrieve FC HBA data.
	 */
	memset(vstor_packet, 0, sizeof(struct vstor_packet));
	vstor_packet->operation = VSTOR_OPERATION_FCHBA_DATA;
	ret = storvsc_execute_vstor_op(device, request, true);
	if (ret != 0)
		return ret;

	/*
	 * Cache the currently active port and node ww names.
	 */
	cache_wwn(stor_device, vstor_packet);

done:

	memset(vstor_packet, 0, sizeof(struct vstor_packet));
	vstor_packet->operation = VSTOR_OPERATION_END_INITIALIZATION;
	ret = storvsc_execute_vstor_op(device, request, true);
	if (ret != 0)
		return ret;

	if (process_sub_channels)
		handle_multichannel_storage(device, max_chns);

	return ret;
}

static void storvsc_handle_error(struct vmscsi_request *vm_srb,
				struct scsi_cmnd *scmnd,
				struct Scsi_Host *host,
				u8 asc, u8 ascq)
{
	struct storvsc_scan_work *wrk;
	void (*process_err_fn)(struct work_struct *work);
	struct hv_host_device *host_dev = shost_priv(host);

	switch (SRB_STATUS(vm_srb->srb_status)) {
	case SRB_STATUS_ERROR:
	case SRB_STATUS_ABORTED:
	case SRB_STATUS_INVALID_REQUEST:
	case SRB_STATUS_INTERNAL_ERROR:
		if (vm_srb->srb_status & SRB_STATUS_AUTOSENSE_VALID) {
			/* Check for capacity change */
			if ((asc == 0x2a) && (ascq == 0x9)) {
				process_err_fn = storvsc_device_scan;
				/* Retry the I/O that triggered this. */
				set_host_byte(scmnd, DID_REQUEUE);
				goto do_work;
			}

			/*
			 * Check for "Operating parameters have changed"
			 * due to Hyper-V changing the VHD/VHDX BlockSize
			 * when adding/removing a differencing disk. This
			 * causes discard_granularity to change, so do a
			 * rescan to pick up the new granularity. We don't
			 * want scsi_report_sense() to output a message
			 * that a sysadmin wouldn't know what to do with.
			 */
			if ((asc == 0x3f) && (ascq != 0x03) &&
					(ascq != 0x0e)) {
				process_err_fn = storvsc_device_scan;
				set_host_byte(scmnd, DID_REQUEUE);
				goto do_work;
			}

			/*
			 * Otherwise, let upper layer deal with the
			 * error when sense message is present
			 */
			return;
		}

		/*
		 * If there is an error; offline the device since all
		 * error recovery strategies would have already been
		 * deployed on the host side. However, if the command
		 * were a pass-through command deal with it appropriately.
		 */
		switch (scmnd->cmnd[0]) {
		case ATA_16:
		case ATA_12:
			set_host_byte(scmnd, DID_PASSTHROUGH);
			break;
		/*
		 * On some Hyper-V hosts TEST_UNIT_READY command can
		 * return SRB_STATUS_ERROR. Let the upper level code
		 * deal with it based on the sense information.
		 */
		case TEST_UNIT_READY:
			break;
		default:
			set_host_byte(scmnd, DID_ERROR);
		}
		return;

	case SRB_STATUS_INVALID_LUN:
		set_host_byte(scmnd, DID_NO_CONNECT);
		process_err_fn = storvsc_remove_lun;
		goto do_work;

	}
	return;

do_work:
	/*
	 * We need to schedule work to process this error; schedule it.
	 */
	wrk = kmalloc(sizeof(struct storvsc_scan_work), GFP_ATOMIC);
	if (!wrk) {
		set_host_byte(scmnd, DID_BAD_TARGET);
		return;
	}

	wrk->host = host;
	wrk->lun = vm_srb->lun;
	wrk->tgt_id = vm_srb->target_id;
	INIT_WORK(&wrk->work, process_err_fn);
	queue_work(host_dev->handle_error_wq, &wrk->work);
}


static void storvsc_command_completion(struct storvsc_cmd_request *cmd_request,
				       struct storvsc_device *stor_dev)
{
	struct scsi_cmnd *scmnd = cmd_request->cmd;
	struct scsi_sense_hdr sense_hdr;
	struct vmscsi_request *vm_srb;
	u32 data_transfer_length;
	struct Scsi_Host *host;
	u32 payload_sz = cmd_request->payload_sz;
	void *payload = cmd_request->payload;
	bool sense_ok;

	host = stor_dev->host;

	vm_srb = &cmd_request->vstor_packet.vm_srb;
	data_transfer_length = vm_srb->data_transfer_length;

	scmnd->result = vm_srb->scsi_status;

	if (scmnd->result) {
		sense_ok = scsi_normalize_sense(scmnd->sense_buffer,
				SCSI_SENSE_BUFFERSIZE, &sense_hdr);

		if (sense_ok && do_logging(STORVSC_LOGGING_WARN))
			scsi_print_sense_hdr(scmnd->device, "storvsc",
					     &sense_hdr);
	}

	if (vm_srb->srb_status != SRB_STATUS_SUCCESS) {
		storvsc_handle_error(vm_srb, scmnd, host, sense_hdr.asc,
					 sense_hdr.ascq);
		/*
		 * The Windows driver set data_transfer_length on
		 * SRB_STATUS_DATA_OVERRUN. On other errors, this value
		 * is untouched.  In these cases we set it to 0.
		 */
		if (vm_srb->srb_status != SRB_STATUS_DATA_OVERRUN)
			data_transfer_length = 0;
	}

	/* Validate data_transfer_length (from Hyper-V) */
	if (data_transfer_length > cmd_request->payload->range.len)
		data_transfer_length = cmd_request->payload->range.len;

	scsi_set_resid(scmnd,
		cmd_request->payload->range.len - data_transfer_length);

	scsi_done(scmnd);

	if (payload_sz >
		sizeof(struct vmbus_channel_packet_multipage_buffer))
		kfree(payload);
}

static void storvsc_on_io_completion(struct storvsc_device *stor_device,
				  struct vstor_packet *vstor_packet,
				  struct storvsc_cmd_request *request)
{
	struct vstor_packet *stor_pkt;
	struct hv_device *device = stor_device->device;

	stor_pkt = &request->vstor_packet;

	/*
	 * The current SCSI handling on the host side does
	 * not correctly handle:
	 * INQUIRY command with page code parameter set to 0x80
	 * MODE_SENSE command with cmd[2] == 0x1c
	 *
	 * Setup srb and scsi status so this won't be fatal.
	 * We do this so we can distinguish truly fatal failues
	 * (srb status == 0x4) and off-line the device in that case.
	 */

	if ((stor_pkt->vm_srb.cdb[0] == INQUIRY) ||
	   (stor_pkt->vm_srb.cdb[0] == MODE_SENSE)) {
		vstor_packet->vm_srb.scsi_status = 0;
		vstor_packet->vm_srb.srb_status = SRB_STATUS_SUCCESS;
	}

	/* Copy over the status...etc */
	stor_pkt->vm_srb.scsi_status = vstor_packet->vm_srb.scsi_status;
	stor_pkt->vm_srb.srb_status = vstor_packet->vm_srb.srb_status;

	/*
	 * Copy over the sense_info_length, but limit to the known max
	 * size if Hyper-V returns a bad value.
	 */
	stor_pkt->vm_srb.sense_info_length = min_t(u8, STORVSC_SENSE_BUFFER_SIZE,
		vstor_packet->vm_srb.sense_info_length);

	if (vstor_packet->vm_srb.scsi_status != 0 ||
	    vstor_packet->vm_srb.srb_status != SRB_STATUS_SUCCESS) {

		/*
		 * Log TEST_UNIT_READY errors only as warnings. Hyper-V can
		 * return errors when detecting devices using TEST_UNIT_READY,
		 * and logging these as errors produces unhelpful noise.
		 */
		int loglevel = (stor_pkt->vm_srb.cdb[0] == TEST_UNIT_READY) ?
			STORVSC_LOGGING_WARN : STORVSC_LOGGING_ERROR;

		storvsc_log(device, loglevel,
			"tag#%d cmd 0x%x status: scsi 0x%x srb 0x%x hv 0x%x\n",
			scsi_cmd_to_rq(request->cmd)->tag,
			stor_pkt->vm_srb.cdb[0],
			vstor_packet->vm_srb.scsi_status,
			vstor_packet->vm_srb.srb_status,
			vstor_packet->status);
	}

	if (vstor_packet->vm_srb.scsi_status == SAM_STAT_CHECK_CONDITION &&
	    (vstor_packet->vm_srb.srb_status & SRB_STATUS_AUTOSENSE_VALID))
		memcpy(request->cmd->sense_buffer,
		       vstor_packet->vm_srb.sense_data,
		       stor_pkt->vm_srb.sense_info_length);

	stor_pkt->vm_srb.data_transfer_length =
		vstor_packet->vm_srb.data_transfer_length;

	storvsc_command_completion(request, stor_device);

	if (atomic_dec_and_test(&stor_device->num_outstanding_req) &&
		stor_device->drain_notify)
		wake_up(&stor_device->waiting_to_drain);
}

static void storvsc_on_receive(struct storvsc_device *stor_device,
			     struct vstor_packet *vstor_packet,
			     struct storvsc_cmd_request *request)
{
	struct hv_host_device *host_dev;
	switch (vstor_packet->operation) {
	case VSTOR_OPERATION_COMPLETE_IO:
		storvsc_on_io_completion(stor_device, vstor_packet, request);
		break;

	case VSTOR_OPERATION_REMOVE_DEVICE:
	case VSTOR_OPERATION_ENUMERATE_BUS:
		host_dev = shost_priv(stor_device->host);
		queue_work(
			host_dev->handle_error_wq, &host_dev->host_scan_work);
		break;

	case VSTOR_OPERATION_FCHBA_DATA:
		cache_wwn(stor_device, vstor_packet);
#if IS_ENABLED(CONFIG_SCSI_FC_ATTRS)
		fc_host_node_name(stor_device->host) = stor_device->node_name;
		fc_host_port_name(stor_device->host) = stor_device->port_name;
#endif
		break;
	default:
		break;
	}
}

static void storvsc_on_channel_callback(void *context)
{
	struct vmbus_channel *channel = (struct vmbus_channel *)context;
	const struct vmpacket_descriptor *desc;
	struct hv_device *device;
	struct storvsc_device *stor_device;
	struct Scsi_Host *shost;
	unsigned long time_limit = jiffies + msecs_to_jiffies(CALLBACK_TIMEOUT);

	if (channel->primary_channel != NULL)
		device = channel->primary_channel->device_obj;
	else
		device = channel->device_obj;

	stor_device = get_in_stor_device(device);
	if (!stor_device)
		return;

	shost = stor_device->host;

	foreach_vmbus_pkt(desc, channel) {
		struct vstor_packet *packet = hv_pkt_data(desc);
		struct storvsc_cmd_request *request = NULL;
		u32 pktlen = hv_pkt_datalen(desc);
		u64 rqst_id = desc->trans_id;
		u32 minlen = rqst_id ? sizeof(struct vstor_packet) :
			sizeof(enum vstor_packet_operation);

		if (unlikely(time_after(jiffies, time_limit))) {
			hv_pkt_iter_close(channel);
			return;
		}

		if (pktlen < minlen) {
			dev_err(&device->device,
				"Invalid pkt: id=%llu, len=%u, minlen=%u\n",
				rqst_id, pktlen, minlen);
			continue;
		}

		if (rqst_id == VMBUS_RQST_INIT) {
			request = &stor_device->init_request;
		} else if (rqst_id == VMBUS_RQST_RESET) {
			request = &stor_device->reset_request;
		} else {
			/* Hyper-V can send an unsolicited message with ID of 0 */
			if (rqst_id == 0) {
				/*
				 * storvsc_on_receive() looks at the vstor_packet in the message
				 * from the ring buffer.
				 *
				 * - If the operation in the vstor_packet is COMPLETE_IO, then
				 *   we call storvsc_on_io_completion(), and dereference the
				 *   guest memory address.  Make sure we don't call
				 *   storvsc_on_io_completion() with a guest memory address
				 *   that is zero if Hyper-V were to construct and send such
				 *   a bogus packet.
				 *
				 * - If the operation in the vstor_packet is FCHBA_DATA, then
				 *   we call cache_wwn(), and access the data payload area of
				 *   the packet (wwn_packet); however, there is no guarantee
				 *   that the packet is big enough to contain such area.
				 *   Future-proof the code by rejecting such a bogus packet.
				 */
				if (packet->operation == VSTOR_OPERATION_COMPLETE_IO ||
				    packet->operation == VSTOR_OPERATION_FCHBA_DATA) {
					dev_err(&device->device, "Invalid packet with ID of 0\n");
					continue;
				}
			} else {
				struct scsi_cmnd *scmnd;

				/* Transaction 'rqst_id' corresponds to tag 'rqst_id - 1' */
				scmnd = scsi_host_find_tag(shost, rqst_id - 1);
				if (scmnd == NULL) {
					dev_err(&device->device, "Incorrect transaction ID\n");
					continue;
				}
				request = (struct storvsc_cmd_request *)scsi_cmd_priv(scmnd);
				scsi_dma_unmap(scmnd);
			}

			storvsc_on_receive(stor_device, packet, request);
			continue;
		}

		memcpy(&request->vstor_packet, packet,
		       sizeof(struct vstor_packet));
		complete(&request->wait_event);
	}
}

static int storvsc_connect_to_vsp(struct hv_device *device, u32 ring_size,
				  bool is_fc)
{
	struct vmstorage_channel_properties props;
	int ret;

	memset(&props, 0, sizeof(struct vmstorage_channel_properties));

	device->channel->max_pkt_size = STORVSC_MAX_PKT_SIZE;
	device->channel->next_request_id_callback = storvsc_next_request_id;

	ret = vmbus_open(device->channel,
			 ring_size,
			 ring_size,
			 (void *)&props,
			 sizeof(struct vmstorage_channel_properties),
			 storvsc_on_channel_callback, device->channel);

	if (ret != 0)
		return ret;

	ret = storvsc_channel_init(device, is_fc);

	return ret;
}

static int storvsc_dev_remove(struct hv_device *device)
{
	struct storvsc_device *stor_device;

	stor_device = hv_get_drvdata(device);

	stor_device->destroy = true;

	/* Make sure flag is set before waiting */
	wmb();

	/*
	 * At this point, all outbound traffic should be disable. We
	 * only allow inbound traffic (responses) to proceed so that
	 * outstanding requests can be completed.
	 */

	storvsc_wait_to_drain(stor_device);

	/*
	 * Since we have already drained, we don't need to busy wait
	 * as was done in final_release_stor_device()
	 * Note that we cannot set the ext pointer to NULL until
	 * we have drained - to drain the outgoing packets, we need to
	 * allow incoming packets.
	 */
	hv_set_drvdata(device, NULL);

	/* Close the channel */
	vmbus_close(device->channel);

	kfree(stor_device->stor_chns);
	kfree(stor_device);
	return 0;
}

static struct vmbus_channel *get_og_chn(struct storvsc_device *stor_device,
					u16 q_num)
{
	u16 slot = 0;
	u16 hash_qnum;
	const struct cpumask *node_mask;
	int num_channels, tgt_cpu;

	if (stor_device->num_sc == 0) {
		stor_device->stor_chns[q_num] = stor_device->device->channel;
		return stor_device->device->channel;
	}

	/*
	 * Our channel array is sparsley populated and we
	 * initiated I/O on a processor/hw-q that does not
	 * currently have a designated channel. Fix this.
	 * The strategy is simple:
	 * I. Ensure NUMA locality
	 * II. Distribute evenly (best effort)
	 */

	node_mask = cpumask_of_node(cpu_to_node(q_num));

	num_channels = 0;
	for_each_cpu(tgt_cpu, &stor_device->alloced_cpus) {
		if (cpumask_test_cpu(tgt_cpu, node_mask))
			num_channels++;
	}
	if (num_channels == 0) {
		stor_device->stor_chns[q_num] = stor_device->device->channel;
		return stor_device->device->channel;
	}

	hash_qnum = q_num;
	while (hash_qnum >= num_channels)
		hash_qnum -= num_channels;

	for_each_cpu(tgt_cpu, &stor_device->alloced_cpus) {
		if (!cpumask_test_cpu(tgt_cpu, node_mask))
			continue;
		if (slot == hash_qnum)
			break;
		slot++;
	}

	stor_device->stor_chns[q_num] = stor_device->stor_chns[tgt_cpu];

	return stor_device->stor_chns[q_num];
}


static int storvsc_do_io(struct hv_device *device,
			 struct storvsc_cmd_request *request, u16 q_num)
{
	struct storvsc_device *stor_device;
	struct vstor_packet *vstor_packet;
	struct vmbus_channel *outgoing_channel, *channel;
	unsigned long flags;
	int ret = 0;
	const struct cpumask *node_mask;
	int tgt_cpu;

	vstor_packet = &request->vstor_packet;
	stor_device = get_out_stor_device(device);

	if (!stor_device)
		return -ENODEV;


	request->device  = device;
	/*
	 * Select an appropriate channel to send the request out.
	 */
	/* See storvsc_change_target_cpu(). */
	outgoing_channel = READ_ONCE(stor_device->stor_chns[q_num]);
	if (outgoing_channel != NULL) {
		if (outgoing_channel->target_cpu == q_num) {
			/*
			 * Ideally, we want to pick a different channel if
			 * available on the same NUMA node.
			 */
			node_mask = cpumask_of_node(cpu_to_node(q_num));
			for_each_cpu_wrap(tgt_cpu,
				 &stor_device->alloced_cpus, q_num + 1) {
				if (!cpumask_test_cpu(tgt_cpu, node_mask))
					continue;
				if (tgt_cpu == q_num)
					continue;
				channel = READ_ONCE(
					stor_device->stor_chns[tgt_cpu]);
				if (channel == NULL)
					continue;
				if (hv_get_avail_to_write_percent(
							&channel->outbound)
						> ring_avail_percent_lowater) {
					outgoing_channel = channel;
					goto found_channel;
				}
			}

			/*
			 * All the other channels on the same NUMA node are
			 * busy. Try to use the channel on the current CPU
			 */
			if (hv_get_avail_to_write_percent(
						&outgoing_channel->outbound)
					> ring_avail_percent_lowater)
				goto found_channel;

			/*
			 * If we reach here, all the channels on the current
			 * NUMA node are busy. Try to find a channel in
			 * other NUMA nodes
			 */
			for_each_cpu(tgt_cpu, &stor_device->alloced_cpus) {
				if (cpumask_test_cpu(tgt_cpu, node_mask))
					continue;
				channel = READ_ONCE(
					stor_device->stor_chns[tgt_cpu]);
				if (channel == NULL)
					continue;
				if (hv_get_avail_to_write_percent(
							&channel->outbound)
						> ring_avail_percent_lowater) {
					outgoing_channel = channel;
					goto found_channel;
				}
			}
		}
	} else {
		spin_lock_irqsave(&stor_device->lock, flags);
		outgoing_channel = stor_device->stor_chns[q_num];
		if (outgoing_channel != NULL) {
			spin_unlock_irqrestore(&stor_device->lock, flags);
			goto found_channel;
		}
		outgoing_channel = get_og_chn(stor_device, q_num);
		spin_unlock_irqrestore(&stor_device->lock, flags);
	}

found_channel:
	vstor_packet->flags |= REQUEST_COMPLETION_FLAG;

	vstor_packet->vm_srb.length = sizeof(struct vmscsi_request);


	vstor_packet->vm_srb.sense_info_length = STORVSC_SENSE_BUFFER_SIZE;


	vstor_packet->vm_srb.data_transfer_length =
	request->payload->range.len;

	vstor_packet->operation = VSTOR_OPERATION_EXECUTE_SRB;

	if (request->payload->range.len) {

		ret = vmbus_sendpacket_mpb_desc(outgoing_channel,
				request->payload, request->payload_sz,
				vstor_packet,
				sizeof(struct vstor_packet),
				(unsigned long)request);
	} else {
		ret = vmbus_sendpacket(outgoing_channel, vstor_packet,
			       sizeof(struct vstor_packet),
			       (unsigned long)request,
			       VM_PKT_DATA_INBAND,
			       VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);
	}

	if (ret != 0)
		return ret;

	atomic_inc(&stor_device->num_outstanding_req);

	return ret;
}

static int storvsc_device_alloc(struct scsi_device *sdevice)
{
	/*
	 * Set blist flag to permit the reading of the VPD pages even when
	 * the target may claim SPC-2 compliance. MSFT targets currently
	 * claim SPC-2 compliance while they implement post SPC-2 features.
	 * With this flag we can correctly handle WRITE_SAME_16 issues.
	 *
	 * Hypervisor reports SCSI_UNKNOWN type for DVD ROM device but
	 * still supports REPORT LUN.
	 */
	sdevice->sdev_bflags = BLIST_REPORTLUN2 | BLIST_TRY_VPD_PAGES;

	return 0;
}

static int storvsc_device_configure(struct scsi_device *sdevice)
{
	blk_queue_rq_timeout(sdevice->request_queue, (storvsc_timeout * HZ));

	/* storvsc devices don't support MAINTENANCE_IN SCSI cmd */
	sdevice->no_report_opcodes = 1;
	sdevice->no_write_same = 1;

	/*
	 * If the host is WIN8 or WIN8 R2, claim conformance to SPC-3
	 * if the device is a MSFT virtual device.  If the host is
	 * WIN10 or newer, allow write_same.
	 */
	if (!strncmp(sdevice->vendor, "Msft", 4)) {
		switch (vmstor_proto_version) {
		case VMSTOR_PROTO_VERSION_WIN8:
		case VMSTOR_PROTO_VERSION_WIN8_1:
			sdevice->scsi_level = SCSI_SPC_3;
			break;
		}

		if (vmstor_proto_version >= VMSTOR_PROTO_VERSION_WIN10)
			sdevice->no_write_same = 0;
	}

	return 0;
}

static int storvsc_get_chs(struct scsi_device *sdev, struct block_device * bdev,
			   sector_t capacity, int *info)
{
	sector_t nsect = capacity;
	sector_t cylinders = nsect;
	int heads, sectors_pt;

	/*
	 * We are making up these values; let us keep it simple.
	 */
	heads = 0xff;
	sectors_pt = 0x3f;      /* Sectors per track */
	sector_div(cylinders, heads * sectors_pt);
	if ((sector_t)(cylinders + 1) * heads * sectors_pt < nsect)
		cylinders = 0xffff;

	info[0] = heads;
	info[1] = sectors_pt;
	info[2] = (int)cylinders;

	return 0;
}

static int storvsc_host_reset_handler(struct scsi_cmnd *scmnd)
{
	struct hv_host_device *host_dev = shost_priv(scmnd->device->host);
	struct hv_device *device = host_dev->dev;

	struct storvsc_device *stor_device;
	struct storvsc_cmd_request *request;
	struct vstor_packet *vstor_packet;
	int ret, t;

	stor_device = get_out_stor_device(device);
	if (!stor_device)
		return FAILED;

	request = &stor_device->reset_request;
	vstor_packet = &request->vstor_packet;
	memset(vstor_packet, 0, sizeof(struct vstor_packet));

	init_completion(&request->wait_event);

	vstor_packet->operation = VSTOR_OPERATION_RESET_BUS;
	vstor_packet->flags = REQUEST_COMPLETION_FLAG;
	vstor_packet->vm_srb.path_id = stor_device->path_id;

	ret = vmbus_sendpacket(device->channel, vstor_packet,
			       sizeof(struct vstor_packet),
			       VMBUS_RQST_RESET,
			       VM_PKT_DATA_INBAND,
			       VMBUS_DATA_PACKET_FLAG_COMPLETION_REQUESTED);
	if (ret != 0)
		return FAILED;

	t = wait_for_completion_timeout(&request->wait_event, 5*HZ);
	if (t == 0)
		return TIMEOUT_ERROR;


	/*
	 * At this point, all outstanding requests in the adapter
	 * should have been flushed out and return to us
	 * There is a potential race here where the host may be in
	 * the process of responding when we return from here.
	 * Just wait for all in-transit packets to be accounted for
	 * before we return from here.
	 */
	storvsc_wait_to_drain(stor_device);

	return SUCCESS;
}

/*
 * The host guarantees to respond to each command, although I/O latencies might
 * be unbounded on Azure.  Reset the timer unconditionally to give the host a
 * chance to perform EH.
 */
static enum scsi_timeout_action storvsc_eh_timed_out(struct scsi_cmnd *scmnd)
{
#if IS_ENABLED(CONFIG_SCSI_FC_ATTRS)
	if (scmnd->device->host->transportt == fc_transport_template)
		return fc_eh_timed_out(scmnd);
#endif
	return SCSI_EH_RESET_TIMER;
}

static bool storvsc_scsi_cmd_ok(struct scsi_cmnd *scmnd)
{
	bool allowed = true;
	u8 scsi_op = scmnd->cmnd[0];

	switch (scsi_op) {
	/* the host does not handle WRITE_SAME, log accident usage */
	case WRITE_SAME:
	/*
	 * smartd sends this command and the host does not handle
	 * this. So, don't send it.
	 */
	case SET_WINDOW:
		set_host_byte(scmnd, DID_ERROR);
		allowed = false;
		break;
	default:
		break;
	}
	return allowed;
}

static int storvsc_queuecommand(struct Scsi_Host *host, struct scsi_cmnd *scmnd)
{
	int ret;
	struct hv_host_device *host_dev = shost_priv(host);
	struct hv_device *dev = host_dev->dev;
	struct storvsc_cmd_request *cmd_request = scsi_cmd_priv(scmnd);
	struct scatterlist *sgl;
	struct vmscsi_request *vm_srb;
	struct vmbus_packet_mpb_array  *payload;
	u32 payload_sz;
	u32 length;

	if (vmstor_proto_version <= VMSTOR_PROTO_VERSION_WIN8) {
		/*
		 * On legacy hosts filter unimplemented commands.
		 * Future hosts are expected to correctly handle
		 * unsupported commands. Furthermore, it is
		 * possible that some of the currently
		 * unsupported commands maybe supported in
		 * future versions of the host.
		 */
		if (!storvsc_scsi_cmd_ok(scmnd)) {
			scsi_done(scmnd);
			return 0;
		}
	}

	/* Setup the cmd request */
	cmd_request->cmd = scmnd;

	memset(&cmd_request->vstor_packet, 0, sizeof(struct vstor_packet));
	vm_srb = &cmd_request->vstor_packet.vm_srb;
	vm_srb->time_out_value = 60;

	vm_srb->srb_flags |=
		SRB_FLAGS_DISABLE_SYNCH_TRANSFER;

	if (scmnd->device->tagged_supported) {
		vm_srb->srb_flags |=
		(SRB_FLAGS_QUEUE_ACTION_ENABLE | SRB_FLAGS_NO_QUEUE_FREEZE);
		vm_srb->queue_tag = SP_UNTAGGED;
		vm_srb->queue_action = SRB_SIMPLE_TAG_REQUEST;
	}

	/* Build the SRB */
	switch (scmnd->sc_data_direction) {
	case DMA_TO_DEVICE:
		vm_srb->data_in = WRITE_TYPE;
		vm_srb->srb_flags |= SRB_FLAGS_DATA_OUT;
		break;
	case DMA_FROM_DEVICE:
		vm_srb->data_in = READ_TYPE;
		vm_srb->srb_flags |= SRB_FLAGS_DATA_IN;
		break;
	case DMA_NONE:
		vm_srb->data_in = UNKNOWN_TYPE;
		vm_srb->srb_flags |= SRB_FLAGS_NO_DATA_TRANSFER;
		break;
	default:
		/*
		 * This is DMA_BIDIRECTIONAL or something else we are never
		 * supposed to see here.
		 */
		WARN(1, "Unexpected data direction: %d\n",
		     scmnd->sc_data_direction);
		return -EINVAL;
	}


	vm_srb->port_number = host_dev->port;
	vm_srb->path_id = scmnd->device->channel;
	vm_srb->target_id = scmnd->device->id;
	vm_srb->lun = scmnd->device->lun;

	vm_srb->cdb_length = scmnd->cmd_len;

	memcpy(vm_srb->cdb, scmnd->cmnd, vm_srb->cdb_length);

	sgl = (struct scatterlist *)scsi_sglist(scmnd);

	length = scsi_bufflen(scmnd);
	payload = (struct vmbus_packet_mpb_array *)&cmd_request->mpb;
	payload_sz = 0;

	if (scsi_sg_count(scmnd)) {
		unsigned long offset_in_hvpg = offset_in_hvpage(sgl->offset);
		unsigned int hvpg_count = HVPFN_UP(offset_in_hvpg + length);
		struct scatterlist *sg;
		unsigned long hvpfn, hvpfns_to_add;
		int j, i = 0, sg_count;

		payload_sz = (hvpg_count * sizeof(u64) +
			      sizeof(struct vmbus_packet_mpb_array));

		if (hvpg_count > MAX_PAGE_BUFFER_COUNT) {
			payload = kzalloc(payload_sz, GFP_ATOMIC);
			if (!payload)
				return SCSI_MLQUEUE_DEVICE_BUSY;
		}

		payload->range.len = length;
		payload->range.offset = offset_in_hvpg;

		sg_count = scsi_dma_map(scmnd);
		if (sg_count < 0) {
			ret = SCSI_MLQUEUE_DEVICE_BUSY;
			goto err_free_payload;
		}

		for_each_sg(sgl, sg, sg_count, j) {
			/*
			 * Init values for the current sgl entry. hvpfns_to_add
			 * is in units of Hyper-V size pages. Handling the
			 * PAGE_SIZE != HV_HYP_PAGE_SIZE case also handles
			 * values of sgl->offset that are larger than PAGE_SIZE.
			 * Such offsets are handled even on other than the first
			 * sgl entry, provided they are a multiple of PAGE_SIZE.
			 */
			hvpfn = HVPFN_DOWN(sg_dma_address(sg));
			hvpfns_to_add = HVPFN_UP(sg_dma_address(sg) +
						 sg_dma_len(sg)) - hvpfn;

			/*
			 * Fill the next portion of the PFN array with
			 * sequential Hyper-V PFNs for the continguous physical
			 * memory described by the sgl entry. The end of the
			 * last sgl should be reached at the same time that
			 * the PFN array is filled.
			 */
			while (hvpfns_to_add--)
				payload->range.pfn_array[i++] = hvpfn++;
		}
	}

	cmd_request->payload = payload;
	cmd_request->payload_sz = payload_sz;

	/* Invokes the vsc to start an IO */
	ret = storvsc_do_io(dev, cmd_request, get_cpu());
	put_cpu();

	if (ret)
		scsi_dma_unmap(scmnd);

	if (ret == -EAGAIN) {
		/* no more space */
		ret = SCSI_MLQUEUE_DEVICE_BUSY;
		goto err_free_payload;
	}

	return 0;

err_free_payload:
	if (payload_sz > sizeof(cmd_request->mpb))
		kfree(payload);

	return ret;
}

static struct scsi_host_template scsi_driver = {
	.module	=		THIS_MODULE,
	.name =			"storvsc_host_t",
	.cmd_size =             sizeof(struct storvsc_cmd_request),
	.bios_param =		storvsc_get_chs,
	.queuecommand =		storvsc_queuecommand,
	.eh_host_reset_handler =	storvsc_host_reset_handler,
	.proc_name =		"storvsc_host",
	.eh_timed_out =		storvsc_eh_timed_out,
	.slave_alloc =		storvsc_device_alloc,
	.slave_configure =	storvsc_device_configure,
	.cmd_per_lun =		2048,
	.this_id =		-1,
	/* Ensure there are no gaps in presented sgls */
	.virt_boundary_mask =	HV_HYP_PAGE_SIZE - 1,
	.no_write_same =	1,
	.track_queue_depth =	1,
	.change_queue_depth =	storvsc_change_queue_depth,
};

enum {
	SCSI_GUID,
	IDE_GUID,
	SFC_GUID,
};

static const struct hv_vmbus_device_id id_table[] = {
	/* SCSI guid */
	{ HV_SCSI_GUID,
	  .driver_data = SCSI_GUID
	},
	/* IDE guid */
	{ HV_IDE_GUID,
	  .driver_data = IDE_GUID
	},
	/* Fibre Channel GUID */
	{
	  HV_SYNTHFC_GUID,
	  .driver_data = SFC_GUID
	},
	{ },
};

MODULE_DEVICE_TABLE(vmbus, id_table);

static const struct { guid_t guid; } fc_guid = { HV_SYNTHFC_GUID };

static bool hv_dev_is_fc(struct hv_device *hv_dev)
{
	return guid_equal(&fc_guid.guid, &hv_dev->dev_type);
}

static int storvsc_probe(struct hv_device *device,
			const struct hv_vmbus_device_id *dev_id)
{
	int ret;
	int num_cpus = num_online_cpus();
	int num_present_cpus = num_present_cpus();
	struct Scsi_Host *host;
	struct hv_host_device *host_dev;
	bool dev_is_ide = ((dev_id->driver_data == IDE_GUID) ? true : false);
	bool is_fc = ((dev_id->driver_data == SFC_GUID) ? true : false);
	int target = 0;
	struct storvsc_device *stor_device;
	int max_sub_channels = 0;
	u32 max_xfer_bytes;

	/*
	 * We support sub-channels for storage on SCSI and FC controllers.
	 * The number of sub-channels offerred is based on the number of
	 * VCPUs in the guest.
	 */
	if (!dev_is_ide)
		max_sub_channels =
			(num_cpus - 1) / storvsc_vcpus_per_sub_channel;

	scsi_driver.can_queue = max_outstanding_req_per_channel *
				(max_sub_channels + 1) *
				(100 - ring_avail_percent_lowater) / 100;

	host = scsi_host_alloc(&scsi_driver,
			       sizeof(struct hv_host_device));
	if (!host)
		return -ENOMEM;

	host_dev = shost_priv(host);
	memset(host_dev, 0, sizeof(struct hv_host_device));

	host_dev->port = host->host_no;
	host_dev->dev = device;
	host_dev->host = host;


	stor_device = kzalloc(sizeof(struct storvsc_device), GFP_KERNEL);
	if (!stor_device) {
		ret = -ENOMEM;
		goto err_out0;
	}

	stor_device->destroy = false;
	init_waitqueue_head(&stor_device->waiting_to_drain);
	stor_device->device = device;
	stor_device->host = host;
	spin_lock_init(&stor_device->lock);
	hv_set_drvdata(device, stor_device);
	dma_set_min_align_mask(&device->device, HV_HYP_PAGE_SIZE - 1);

	stor_device->port_number = host->host_no;
	ret = storvsc_connect_to_vsp(device, storvsc_ringbuffer_size, is_fc);
	if (ret)
		goto err_out1;

	host_dev->path = stor_device->path_id;
	host_dev->target = stor_device->target_id;

	switch (dev_id->driver_data) {
	case SFC_GUID:
		host->max_lun = STORVSC_FC_MAX_LUNS_PER_TARGET;
		host->max_id = STORVSC_FC_MAX_TARGETS;
		host->max_channel = STORVSC_FC_MAX_CHANNELS - 1;
#if IS_ENABLED(CONFIG_SCSI_FC_ATTRS)
		host->transportt = fc_transport_template;
#endif
		break;

	case SCSI_GUID:
		host->max_lun = STORVSC_MAX_LUNS_PER_TARGET;
		host->max_id = STORVSC_MAX_TARGETS;
		host->max_channel = STORVSC_MAX_CHANNELS - 1;
		break;

	default:
		host->max_lun = STORVSC_IDE_MAX_LUNS_PER_TARGET;
		host->max_id = STORVSC_IDE_MAX_TARGETS;
		host->max_channel = STORVSC_IDE_MAX_CHANNELS - 1;
		break;
	}
	/* max cmd length */
	host->max_cmd_len = STORVSC_MAX_CMD_LEN;
	/*
	 * Any reasonable Hyper-V configuration should provide
	 * max_transfer_bytes value aligning to HV_HYP_PAGE_SIZE,
	 * protecting it from any weird value.
	 */
	max_xfer_bytes = round_down(stor_device->max_transfer_bytes, HV_HYP_PAGE_SIZE);
	if (is_fc)
		max_xfer_bytes = min(max_xfer_bytes, STORVSC_FC_MAX_XFER_SIZE);

	/* max_hw_sectors_kb */
	host->max_sectors = max_xfer_bytes >> 9;
	/*
	 * There are 2 requirements for Hyper-V storvsc sgl segments,
	 * based on which the below calculation for max segments is
	 * done:
	 *
	 * 1. Except for the first and last sgl segment, all sgl segments
	 *    should be align to HV_HYP_PAGE_SIZE, that also means the
	 *    maximum number of segments in a sgl can be calculated by
	 *    dividing the total max transfer length by HV_HYP_PAGE_SIZE.
	 *
	 * 2. Except for the first and last, each entry in the SGL must
	 *    have an offset that is a multiple of HV_HYP_PAGE_SIZE.
	 */
	host->sg_tablesize = (max_xfer_bytes >> HV_HYP_PAGE_SHIFT) + 1;
	/*
	 * For non-IDE disks, the host supports multiple channels.
	 * Set the number of HW queues we are supporting.
	 */
	if (!dev_is_ide) {
		if (storvsc_max_hw_queues > num_present_cpus) {
			storvsc_max_hw_queues = 0;
			storvsc_log(device, STORVSC_LOGGING_WARN,
				"Resetting invalid storvsc_max_hw_queues value to default.\n");
		}
		if (storvsc_max_hw_queues)
			host->nr_hw_queues = storvsc_max_hw_queues;
		else
			host->nr_hw_queues = num_present_cpus;
	}

	/*
	 * Set the error handler work queue.
	 */
	host_dev->handle_error_wq =
			alloc_ordered_workqueue("storvsc_error_wq_%d",
						0,
						host->host_no);
	if (!host_dev->handle_error_wq) {
		ret = -ENOMEM;
		goto err_out2;
	}
	INIT_WORK(&host_dev->host_scan_work, storvsc_host_scan);
	/* Register the HBA and start the scsi bus scan */
	ret = scsi_add_host(host, &device->device);
	if (ret != 0)
		goto err_out3;

	if (!dev_is_ide) {
		scsi_scan_host(host);
	} else {
		target = (device->dev_instance.b[5] << 8 |
			 device->dev_instance.b[4]);
		ret = scsi_add_device(host, 0, target, 0);
		if (ret)
			goto err_out4;
	}
#if IS_ENABLED(CONFIG_SCSI_FC_ATTRS)
	if (host->transportt == fc_transport_template) {
		struct fc_rport_identifiers ids = {
			.roles = FC_PORT_ROLE_FCP_DUMMY_INITIATOR,
		};

		fc_host_node_name(host) = stor_device->node_name;
		fc_host_port_name(host) = stor_device->port_name;
		stor_device->rport = fc_remote_port_add(host, 0, &ids);
		if (!stor_device->rport) {
			ret = -ENOMEM;
			goto err_out4;
		}
	}
#endif
	return 0;

err_out4:
	scsi_remove_host(host);

err_out3:
	destroy_workqueue(host_dev->handle_error_wq);

err_out2:
	/*
	 * Once we have connected with the host, we would need to
	 * invoke storvsc_dev_remove() to rollback this state and
	 * this call also frees up the stor_device; hence the jump around
	 * err_out1 label.
	 */
	storvsc_dev_remove(device);
	goto err_out0;

err_out1:
	kfree(stor_device->stor_chns);
	kfree(stor_device);

err_out0:
	scsi_host_put(host);
	return ret;
}

/* Change a scsi target's queue depth */
static int storvsc_change_queue_depth(struct scsi_device *sdev, int queue_depth)
{
	if (queue_depth > scsi_driver.can_queue)
		queue_depth = scsi_driver.can_queue;

	return scsi_change_queue_depth(sdev, queue_depth);
}

static void storvsc_remove(struct hv_device *dev)
{
	struct storvsc_device *stor_device = hv_get_drvdata(dev);
	struct Scsi_Host *host = stor_device->host;
	struct hv_host_device *host_dev = shost_priv(host);

#if IS_ENABLED(CONFIG_SCSI_FC_ATTRS)
	if (host->transportt == fc_transport_template) {
		fc_remote_port_delete(stor_device->rport);
		fc_remove_host(host);
	}
#endif
	destroy_workqueue(host_dev->handle_error_wq);
	scsi_remove_host(host);
	storvsc_dev_remove(dev);
	scsi_host_put(host);
}

static int storvsc_suspend(struct hv_device *hv_dev)
{
	struct storvsc_device *stor_device = hv_get_drvdata(hv_dev);
	struct Scsi_Host *host = stor_device->host;
	struct hv_host_device *host_dev = shost_priv(host);

	storvsc_wait_to_drain(stor_device);

	drain_workqueue(host_dev->handle_error_wq);

	vmbus_close(hv_dev->channel);

	kfree(stor_device->stor_chns);
	stor_device->stor_chns = NULL;

	cpumask_clear(&stor_device->alloced_cpus);

	return 0;
}

static int storvsc_resume(struct hv_device *hv_dev)
{
	int ret;

	ret = storvsc_connect_to_vsp(hv_dev, storvsc_ringbuffer_size,
				     hv_dev_is_fc(hv_dev));
	return ret;
}

static struct hv_driver storvsc_drv = {
	.name = KBUILD_MODNAME,
	.id_table = id_table,
	.probe = storvsc_probe,
	.remove = storvsc_remove,
	.suspend = storvsc_suspend,
	.resume = storvsc_resume,
	.driver = {
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
};

#if IS_ENABLED(CONFIG_SCSI_FC_ATTRS)
static struct fc_function_template fc_transport_functions = {
	.show_host_node_name = 1,
	.show_host_port_name = 1,
};
#endif

static int __init storvsc_drv_init(void)
{
	int ret;

	/*
	 * Divide the ring buffer data size (which is 1 page less
	 * than the ring buffer size since that page is reserved for
	 * the ring buffer indices) by the max request size (which is
	 * vmbus_channel_packet_multipage_buffer + struct vstor_packet + u64)
	 */
	max_outstanding_req_per_channel =
		((storvsc_ringbuffer_size - PAGE_SIZE) /
		ALIGN(MAX_MULTIPAGE_BUFFER_PACKET +
		sizeof(struct vstor_packet) + sizeof(u64),
		sizeof(u64)));

#if IS_ENABLED(CONFIG_SCSI_FC_ATTRS)
	fc_transport_template = fc_attach_transport(&fc_transport_functions);
	if (!fc_transport_template)
		return -ENODEV;
#endif

	ret = vmbus_driver_register(&storvsc_drv);

#if IS_ENABLED(CONFIG_SCSI_FC_ATTRS)
	if (ret)
		fc_release_transport(fc_transport_template);
#endif

	return ret;
}

static void __exit storvsc_drv_exit(void)
{
	vmbus_driver_unregister(&storvsc_drv);
#if IS_ENABLED(CONFIG_SCSI_FC_ATTRS)
	fc_release_transport(fc_transport_template);
#endif
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Microsoft Hyper-V virtual storage driver");
module_init(storvsc_drv_init);
module_exit(storvsc_drv_exit);
