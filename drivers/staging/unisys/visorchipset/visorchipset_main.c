/* visorchipset_main.c
 *
 * Copyright (C) 2010 - 2013 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

#include "globals.h"
#include "controlvm.h"
#include "visorchipset.h"
#include "procobjecttree.h"
#include "visorchannel.h"
#include "periodic_work.h"
#include "testing.h"
#include "file.h"
#include "parser.h"
#include "uniklog.h"
#include "uisutils.h"
#include "controlvmcompletionstatus.h"
#include "guestlinuxdebug.h"

#include <linux/nls.h>
#include <linux/netdevice.h>
#include <linux/platform_device.h>
#include <linux/uuid.h>

#define CURRENT_FILE_PC VISOR_CHIPSET_PC_visorchipset_main_c
#define TEST_VNIC_PHYSITF "eth0"	/* physical network itf for
					 * vnic loopback test */
#define TEST_VNIC_SWITCHNO 1
#define TEST_VNIC_BUSNO 9

#define MAX_NAME_SIZE 128
#define MAX_IP_SIZE   50
#define MAXOUTSTANDINGCHANNELCOMMAND 256
#define POLLJIFFIES_CONTROLVMCHANNEL_FAST   1
#define POLLJIFFIES_CONTROLVMCHANNEL_SLOW 100

/* When the controlvm channel is idle for at least MIN_IDLE_SECONDS,
* we switch to slow polling mode.  As soon as we get a controlvm
* message, we switch back to fast polling mode.
*/
#define MIN_IDLE_SECONDS 10
static ulong Poll_jiffies = POLLJIFFIES_CONTROLVMCHANNEL_FAST;
static ulong Most_recent_message_jiffies;	/* when we got our last
						 * controlvm message */
static inline char *
NONULLSTR(char *s)
{
	if (s)
		return s;
	else
		return "";
}

static int serverregistered;
static int clientregistered;

#define MAX_CHIPSET_EVENTS 2
static U8 chipset_events[MAX_CHIPSET_EVENTS] = { 0, 0 };

static struct delayed_work Periodic_controlvm_work;
static struct workqueue_struct *Periodic_controlvm_workqueue;
static DEFINE_SEMAPHORE(NotifierLock);

typedef struct {
	CONTROLVM_MESSAGE message;
	unsigned int crc;
} MESSAGE_ENVELOPE;

static CONTROLVM_MESSAGE_HEADER g_DiagMsgHdr;
static CONTROLVM_MESSAGE_HEADER g_ChipSetMsgHdr;
static CONTROLVM_MESSAGE_HEADER g_DelDumpMsgHdr;
static const uuid_le UltraDiagPoolChannelProtocolGuid =
	ULTRA_DIAG_POOL_CHANNEL_PROTOCOL_GUID;
/* 0xffffff is an invalid Bus/Device number */
static ulong g_diagpoolBusNo = 0xffffff;
static ulong g_diagpoolDevNo = 0xffffff;
static CONTROLVM_MESSAGE_PACKET g_DeviceChangeStatePacket;

/* Only VNIC and VHBA channels are sent to visorclientbus (aka
 * "visorhackbus")
 */
#define FOR_VISORHACKBUS(channel_type_guid) \
	(((uuid_le_cmp(channel_type_guid, UltraVnicChannelProtocolGuid) == 0)\
	|| (uuid_le_cmp(channel_type_guid, UltraVhbaChannelProtocolGuid) == 0)))
#define FOR_VISORBUS(channel_type_guid) (!(FOR_VISORHACKBUS(channel_type_guid)))

#define is_diagpool_channel(channel_type_guid) \
	 (uuid_le_cmp(channel_type_guid, UltraDiagPoolChannelProtocolGuid) == 0)

typedef enum {
	PARTPROP_invalid,
	PARTPROP_name,
	PARTPROP_description,
	PARTPROP_handle,
	PARTPROP_busNumber,
	/* add new properties above, but don't forget to change
	 * InitPartitionProperties() and show_partition_property() also...
	 */
	PARTPROP_last
} PARTITION_property;
static const char *PartitionTypeNames[] = { "partition", NULL };

static char *PartitionPropertyNames[PARTPROP_last + 1];
static void
InitPartitionProperties(void)
{
	char **p = PartitionPropertyNames;
	p[PARTPROP_invalid] = "";
	p[PARTPROP_name] = "name";
	p[PARTPROP_description] = "description";
	p[PARTPROP_handle] = "handle";
	p[PARTPROP_busNumber] = "busNumber";
	p[PARTPROP_last] = NULL;
}

typedef enum {
	CTLVMPROP_invalid,
	CTLVMPROP_physAddr,
	CTLVMPROP_controlChannelAddr,
	CTLVMPROP_controlChannelBytes,
	CTLVMPROP_sparBootPart,
	CTLVMPROP_sparStoragePart,
	CTLVMPROP_livedumpLength,
	CTLVMPROP_livedumpCrc32,
	/* add new properties above, but don't forget to change
	 * InitControlVmProperties() show_controlvm_property() also...
	 */
	CTLVMPROP_last
} CONTROLVM_property;

static const char *ControlVmTypeNames[] = { "controlvm", NULL };

static char *ControlVmPropertyNames[CTLVMPROP_last + 1];
static void
InitControlVmProperties(void)
{
	char **p = ControlVmPropertyNames;
	p[CTLVMPROP_invalid] = "";
	p[CTLVMPROP_physAddr] = "physAddr";
	p[CTLVMPROP_controlChannelAddr] = "controlChannelAddr";
	p[CTLVMPROP_controlChannelBytes] = "controlChannelBytes";
	p[CTLVMPROP_sparBootPart] = "spar_boot_part";
	p[CTLVMPROP_sparStoragePart] = "spar_storage_part";
	p[CTLVMPROP_livedumpLength] = "livedumpLength";
	p[CTLVMPROP_livedumpCrc32] = "livedumpCrc32";
	p[CTLVMPROP_last] = NULL;
}

static MYPROCOBJECT *ControlVmObject;
static MYPROCTYPE *PartitionType;
static MYPROCTYPE *ControlVmType;

#define VISORCHIPSET_DIAG_PROC_ENTRY_FN "diagdump"
static struct proc_dir_entry *diag_proc_dir;

#define VISORCHIPSET_CHIPSET_PROC_ENTRY_FN "chipsetready"
static struct proc_dir_entry *chipset_proc_dir;

#define VISORCHIPSET_PARAHOTPLUG_PROC_ENTRY_FN "parahotplug"
static struct proc_dir_entry *parahotplug_proc_dir;

static LIST_HEAD(BusInfoList);
static LIST_HEAD(DevInfoList);

static struct proc_dir_entry *ProcDir;
static VISORCHANNEL *ControlVm_channel;

static ssize_t visorchipset_proc_read_writeonly(struct file *file,
						char __user *buf,
						size_t len, loff_t *offset);
static ssize_t proc_read_installer(struct file *file, char __user *buf,
				   size_t len, loff_t *offset);
static ssize_t proc_write_installer(struct file *file,
				    const char __user *buffer,
				    size_t count, loff_t *ppos);
static ssize_t proc_read_toolaction(struct file *file, char __user *buf,
				    size_t len, loff_t *offset);
static ssize_t proc_write_toolaction(struct file *file,
				     const char __user *buffer,
				     size_t count, loff_t *ppos);
static ssize_t proc_read_bootToTool(struct file *file, char __user *buf,
				    size_t len, loff_t *offset);
static ssize_t proc_write_bootToTool(struct file *file,
				     const char __user *buffer,
				     size_t count, loff_t *ppos);
static const struct file_operations proc_installer_fops = {
	.read = proc_read_installer,
	.write = proc_write_installer,
};

static const struct file_operations proc_toolaction_fops = {
	.read = proc_read_toolaction,
	.write = proc_write_toolaction,
};

static const struct file_operations proc_bootToTool_fops = {
	.read = proc_read_bootToTool,
	.write = proc_write_bootToTool,
};

typedef struct {
	U8 __iomem *ptr;	/* pointer to base address of payload pool */
	U64 offset;		/* offset from beginning of controlvm
				 * channel to beginning of payload * pool */
	U32 bytes;		/* number of bytes in payload pool */
} CONTROLVM_PAYLOAD_INFO;

/* Manages the request payload in the controlvm channel */
static CONTROLVM_PAYLOAD_INFO ControlVm_payload_info;

static pCHANNEL_HEADER Test_Vnic_channel;

typedef struct {
	CONTROLVM_MESSAGE_HEADER Dumpcapture_header;
	CONTROLVM_MESSAGE_HEADER Gettextdump_header;
	CONTROLVM_MESSAGE_HEADER Dumpcomplete_header;
	BOOL Gettextdump_outstanding;
	u32 crc32;
	ulong length;
	atomic_t buffers_in_use;
	ulong destination;
} LIVEDUMP_INFO;
/* Manages the info for a CONTROLVM_DUMP_CAPTURESTATE /
 * CONTROLVM_DUMP_GETTEXTDUMP / CONTROLVM_DUMP_COMPLETE conversation.
 */
static LIVEDUMP_INFO LiveDump_info;

/* The following globals are used to handle the scenario where we are unable to
 * offload the payload from a controlvm message due to memory requirements.  In
 * this scenario, we simply stash the controlvm message, then attempt to
 * process it again the next time controlvm_periodic_work() runs.
 */
static CONTROLVM_MESSAGE ControlVm_Pending_Msg;
static BOOL ControlVm_Pending_Msg_Valid = FALSE;

/* Pool of struct putfile_buffer_entry, for keeping track of pending (incoming)
 * TRANSMIT_FILE PutFile payloads.
 */
static struct kmem_cache *Putfile_buffer_list_pool;
static const char Putfile_buffer_list_pool_name[] =
	"controlvm_putfile_buffer_list_pool";

/* This identifies a data buffer that has been received via a controlvm messages
 * in a remote --> local CONTROLVM_TRANSMIT_FILE conversation.
 */
struct putfile_buffer_entry {
	struct list_head next;	/* putfile_buffer_entry list */
	PARSER_CONTEXT *parser_ctx; /* points to buffer containing input data */
};

/* List of struct putfile_request *, via next_putfile_request member.
 * Each entry in this list identifies an outstanding TRANSMIT_FILE
 * conversation.
 */
static LIST_HEAD(Putfile_request_list);

/* This describes a buffer and its current state of transfer (e.g., how many
 * bytes have already been supplied as putfile data, and how many bytes are
 * remaining) for a putfile_request.
 */
struct putfile_active_buffer {
	/* a payload from a controlvm message, containing a file data buffer */
	PARSER_CONTEXT *parser_ctx;
	/* points within data area of parser_ctx to next byte of data */
	u8 *pnext;
	/* # bytes left from <pnext> to the end of this data buffer */
	size_t bytes_remaining;
};

#define PUTFILE_REQUEST_SIG 0x0906101302281211
/* This identifies a single remote --> local CONTROLVM_TRANSMIT_FILE
 * conversation.  Structs of this type are dynamically linked into
 * <Putfile_request_list>.
 */
struct putfile_request {
	u64 sig;		/* PUTFILE_REQUEST_SIG */

	/* header from original TransmitFile request */
	CONTROLVM_MESSAGE_HEADER controlvm_header;
	u64 file_request_number;	/* from original TransmitFile request */

	/* link to next struct putfile_request */
	struct list_head next_putfile_request;

	/* most-recent sequence number supplied via a controlvm message */
	u64 data_sequence_number;

	/* head of putfile_buffer_entry list, which describes the data to be
	 * supplied as putfile data;
	 * - this list is added to when controlvm messages come in that supply
	 * file data
	 * - this list is removed from via the hotplug program that is actually
	 * consuming these buffers to write as file data */
	struct list_head input_buffer_list;
	spinlock_t req_list_lock;	/* lock for input_buffer_list */

	/* waiters for input_buffer_list to go non-empty */
	wait_queue_head_t input_buffer_wq;

	/* data not yet read within current putfile_buffer_entry */
	struct putfile_active_buffer active_buf;

	/* <0 = failed, 0 = in-progress, >0 = successful; */
	/* note that this must be set with req_list_lock, and if you set <0, */
	/* it is your responsibility to also free up all of the other objects */
	/* in this struct (like input_buffer_list, active_buf.parser_ctx) */
	/* before releasing the lock */
	int completion_status;
};

static atomic_t Visorchipset_cache_buffers_in_use = ATOMIC_INIT(0);

struct parahotplug_request {
	struct list_head list;
	int id;
	unsigned long expiration;
	CONTROLVM_MESSAGE msg;
};

static LIST_HEAD(Parahotplug_request_list);
static DEFINE_SPINLOCK(Parahotplug_request_list_lock);	/* lock for above */
static void parahotplug_process_list(void);

/* Manages the info for a CONTROLVM_DUMP_CAPTURESTATE /
 * CONTROLVM_REPORTEVENT.
 */
static VISORCHIPSET_BUSDEV_NOTIFIERS BusDev_Server_Notifiers;
static VISORCHIPSET_BUSDEV_NOTIFIERS BusDev_Client_Notifiers;

static void bus_create_response(ulong busNo, int response);
static void bus_destroy_response(ulong busNo, int response);
static void device_create_response(ulong busNo, ulong devNo, int response);
static void device_destroy_response(ulong busNo, ulong devNo, int response);
static void device_resume_response(ulong busNo, ulong devNo, int response);

static VISORCHIPSET_BUSDEV_RESPONDERS BusDev_Responders = {
	.bus_create = bus_create_response,
	.bus_destroy = bus_destroy_response,
	.device_create = device_create_response,
	.device_destroy = device_destroy_response,
	.device_pause = visorchipset_device_pause_response,
	.device_resume = device_resume_response,
};

/* info for /dev/visorchipset */
static dev_t MajorDev = -1; /**< indicates major num for device */

/* /sys/devices/platform/visorchipset */
static struct platform_device Visorchipset_platform_device = {
	.name = "visorchipset",
	.id = -1,
};

/* Function prototypes */
static void controlvm_respond(CONTROLVM_MESSAGE_HEADER *msgHdr, int response);
static void controlvm_respond_chipset_init(CONTROLVM_MESSAGE_HEADER *msgHdr,
					   int response,
					   ULTRA_CHIPSET_FEATURE features);
static void controlvm_respond_physdev_changestate(CONTROLVM_MESSAGE_HEADER *
						  msgHdr, int response,
						  ULTRA_SEGMENT_STATE state);

static void
show_partition_property(struct seq_file *f, void *ctx, int property)
{
	VISORCHIPSET_BUS_INFO *info = (VISORCHIPSET_BUS_INFO *) (ctx);

	switch (property) {
	case PARTPROP_name:
		seq_printf(f, "%s\n", NONULLSTR(info->name));
		break;
	case PARTPROP_description:
		seq_printf(f, "%s\n", NONULLSTR(info->description));
		break;
	case PARTPROP_handle:
		seq_printf(f, "0x%-16.16Lx\n", info->partitionHandle);
		break;
	case PARTPROP_busNumber:
		seq_printf(f, "%d\n", info->busNo);
		break;
	default:
		seq_printf(f, "(%d??)\n", property);
		break;
	}
}

static void
show_controlvm_property(struct seq_file *f, void *ctx, int property)
{
	/* Note: ctx is not needed since we only have 1 controlvm channel */
	switch (property) {
	case CTLVMPROP_physAddr:
		if (ControlVm_channel == NULL)
			seq_puts(f, "0x0\n");
		else
			seq_printf(f, "0x%-16.16Lx\n",
				   visorchannel_get_physaddr
				   (ControlVm_channel));
		break;
	case CTLVMPROP_controlChannelAddr:
		if (ControlVm_channel == NULL)
			seq_puts(f, "0x0\n");
		else {
			GUEST_PHYSICAL_ADDRESS addr = 0;
			visorchannel_read(ControlVm_channel,
					  offsetof
					  (ULTRA_CONTROLVM_CHANNEL_PROTOCOL,
					   gpControlChannel), &addr,
					  sizeof(addr));
			seq_printf(f, "0x%-16.16Lx\n", (u64) (addr));
		}
		break;
	case CTLVMPROP_controlChannelBytes:
		if (ControlVm_channel == NULL)
			seq_puts(f, "0x0\n");
		else {
			U32 bytes = 0;
			visorchannel_read(ControlVm_channel,
					  offsetof
					  (ULTRA_CONTROLVM_CHANNEL_PROTOCOL,
					   ControlChannelBytes), &bytes,
					  sizeof(bytes));
			seq_printf(f, "%lu\n", (ulong) (bytes));
		}
		break;
	case CTLVMPROP_sparBootPart:
		seq_puts(f, "0:0:0:0/1\n");
		break;
	case CTLVMPROP_sparStoragePart:
		seq_puts(f, "0:0:0:0/2\n");
		break;
	case CTLVMPROP_livedumpLength:
		seq_printf(f, "%lu\n", LiveDump_info.length);
		break;
	case CTLVMPROP_livedumpCrc32:
		seq_printf(f, "%lu\n", (ulong) LiveDump_info.crc32);
		break;
	default:
		seq_printf(f, "(%d??)\n", property);
		break;
	}
}

static void
proc_Init(void)
{
	if (ProcDir == NULL) {
		ProcDir = proc_mkdir(MYDRVNAME, NULL);
		if (ProcDir == NULL) {
			LOGERR("failed to create /proc directory %s",
			       MYDRVNAME);
			POSTCODE_LINUX_2(CHIPSET_INIT_FAILURE_PC,
					 POSTCODE_SEVERITY_ERR);
		}
	}
}

static void
proc_DeInit(void)
{
	if (ProcDir != NULL)
		remove_proc_entry(MYDRVNAME, NULL);
	ProcDir = NULL;
}

#if 0
static void
testUnicode(void)
{
	wchar_t unicodeString[] = { 'a', 'b', 'c', 0 };
	char s[sizeof(unicodeString) * NLS_MAX_CHARSET_SIZE];
	wchar_t unicode2[99];

	/* NOTE: Either due to a bug, or feature I don't understand, the
	 *       kernel utf8_mbstowcs() and utf_wcstombs() do NOT copy the
	 *       trailed NUL byte!!   REALLY!!!!!    Arrrrgggghhhhh
	 */

	LOGINF("sizeof(wchar_t) = %d", sizeof(wchar_t));
	LOGINF("utf8_wcstombs=%d",
	       chrs = utf8_wcstombs(s, unicodeString, sizeof(s)));
	if (chrs >= 0)
		s[chrs] = '\0';	/* GRRRRRRRR */
	LOGINF("s='%s'", s);
	LOGINF("utf8_mbstowcs=%d", chrs = utf8_mbstowcs(unicode2, s, 100));
	if (chrs >= 0)
		unicode2[chrs] = 0;	/* GRRRRRRRR */
	if (memcmp(unicodeString, unicode2, sizeof(unicodeString)) == 0)
		LOGINF("strings match... good");
	else
		LOGINF("strings did not match!!");
}
#endif

static void
busInfo_clear(void *v)
{
	VISORCHIPSET_BUS_INFO *p = (VISORCHIPSET_BUS_INFO *) (v);

	if (p->procObject) {
		visor_proc_DestroyObject(p->procObject);
		p->procObject = NULL;
	}
	kfree(p->name);
	p->name = NULL;

	kfree(p->description);
	p->description = NULL;

	p->state.created = 0;
	memset(p, 0, sizeof(VISORCHIPSET_BUS_INFO));
}

static void
devInfo_clear(void *v)
{
	VISORCHIPSET_DEVICE_INFO *p = (VISORCHIPSET_DEVICE_INFO *) (v);
	p->state.created = 0;
	memset(p, 0, sizeof(VISORCHIPSET_DEVICE_INFO));
}

static U8
check_chipset_events(void)
{
	int i;
	U8 send_msg = 1;
	/* Check events to determine if response should be sent */
	for (i = 0; i < MAX_CHIPSET_EVENTS; i++)
		send_msg &= chipset_events[i];
	return send_msg;
}

static void
clear_chipset_events(void)
{
	int i;
	/* Clear chipset_events */
	for (i = 0; i < MAX_CHIPSET_EVENTS; i++)
		chipset_events[i] = 0;
}

void
visorchipset_register_busdev_server(VISORCHIPSET_BUSDEV_NOTIFIERS *notifiers,
				    VISORCHIPSET_BUSDEV_RESPONDERS *responders,
				    ULTRA_VBUS_DEVICEINFO *driverInfo)
{
	LOCKSEM_UNINTERRUPTIBLE(&NotifierLock);
	if (notifiers == NULL) {
		memset(&BusDev_Server_Notifiers, 0,
		       sizeof(BusDev_Server_Notifiers));
		serverregistered = 0;	/* clear flag */
	} else {
		BusDev_Server_Notifiers = *notifiers;
		serverregistered = 1;	/* set flag */
	}
	if (responders)
		*responders = BusDev_Responders;
	if (driverInfo)
		BusDeviceInfo_Init(driverInfo, "chipset", "visorchipset",
				   VERSION, NULL);

	UNLOCKSEM(&NotifierLock);
}
EXPORT_SYMBOL_GPL(visorchipset_register_busdev_server);

void
visorchipset_register_busdev_client(VISORCHIPSET_BUSDEV_NOTIFIERS *notifiers,
				    VISORCHIPSET_BUSDEV_RESPONDERS *responders,
				    ULTRA_VBUS_DEVICEINFO *driverInfo)
{
	LOCKSEM_UNINTERRUPTIBLE(&NotifierLock);
	if (notifiers == NULL) {
		memset(&BusDev_Client_Notifiers, 0,
		       sizeof(BusDev_Client_Notifiers));
		clientregistered = 0;	/* clear flag */
	} else {
		BusDev_Client_Notifiers = *notifiers;
		clientregistered = 1;	/* set flag */
	}
	if (responders)
		*responders = BusDev_Responders;
	if (driverInfo)
		BusDeviceInfo_Init(driverInfo, "chipset(bolts)", "visorchipset",
				   VERSION, NULL);
	UNLOCKSEM(&NotifierLock);
}
EXPORT_SYMBOL_GPL(visorchipset_register_busdev_client);

static void
cleanup_controlvm_structures(void)
{
	VISORCHIPSET_BUS_INFO *bi, *tmp_bi;
	VISORCHIPSET_DEVICE_INFO *di, *tmp_di;

	list_for_each_entry_safe(bi, tmp_bi, &BusInfoList, entry) {
		busInfo_clear(bi);
		list_del(&bi->entry);
		kfree(bi);
	}

	list_for_each_entry_safe(di, tmp_di, &DevInfoList, entry) {
		devInfo_clear(di);
		list_del(&di->entry);
		kfree(di);
	}
}

static void
chipset_init(CONTROLVM_MESSAGE *inmsg)
{
	static int chipset_inited;
	ULTRA_CHIPSET_FEATURE features = 0;
	int rc = CONTROLVM_RESP_SUCCESS;

	POSTCODE_LINUX_2(CHIPSET_INIT_ENTRY_PC, POSTCODE_SEVERITY_INFO);
	if (chipset_inited) {
		LOGERR("CONTROLVM_CHIPSET_INIT Failed: Already Done.");
		rc = -CONTROLVM_RESP_ERROR_ALREADY_DONE;
		goto Away;
	}
	chipset_inited = 1;
	POSTCODE_LINUX_2(CHIPSET_INIT_EXIT_PC, POSTCODE_SEVERITY_INFO);

	/* Set features to indicate we support parahotplug (if Command
	 * also supports it). */
	features =
	    inmsg->cmd.initChipset.
	    features & ULTRA_CHIPSET_FEATURE_PARA_HOTPLUG;

	/* Set the "reply" bit so Command knows this is a
	 * features-aware driver. */
	features |= ULTRA_CHIPSET_FEATURE_REPLY;

Away:
	if (rc < 0)
		cleanup_controlvm_structures();
	if (inmsg->hdr.Flags.responseExpected)
		controlvm_respond_chipset_init(&inmsg->hdr, rc, features);
}

static void
controlvm_init_response(CONTROLVM_MESSAGE *msg,
			CONTROLVM_MESSAGE_HEADER *msgHdr, int response)
{
	memset(msg, 0, sizeof(CONTROLVM_MESSAGE));
	memcpy(&msg->hdr, msgHdr, sizeof(CONTROLVM_MESSAGE_HEADER));
	msg->hdr.PayloadBytes = 0;
	msg->hdr.PayloadVmOffset = 0;
	msg->hdr.PayloadMaxBytes = 0;
	if (response < 0) {
		msg->hdr.Flags.failed = 1;
		msg->hdr.CompletionStatus = (U32) (-response);
	}
}

static void
controlvm_respond(CONTROLVM_MESSAGE_HEADER *msgHdr, int response)
{
	CONTROLVM_MESSAGE outmsg;
	if (!ControlVm_channel)
		return;
	controlvm_init_response(&outmsg, msgHdr, response);
	/* For DiagPool channel DEVICE_CHANGESTATE, we need to send
	* back the deviceChangeState structure in the packet. */
	if (msgHdr->Id == CONTROLVM_DEVICE_CHANGESTATE
	    && g_DeviceChangeStatePacket.deviceChangeState.busNo ==
	    g_diagpoolBusNo
	    && g_DeviceChangeStatePacket.deviceChangeState.devNo ==
	    g_diagpoolDevNo)
		outmsg.cmd = g_DeviceChangeStatePacket;
	if (outmsg.hdr.Flags.testMessage == 1) {
		LOGINF("%s controlvm_msg=0x%x response=%d for test message",
		       __func__, outmsg.hdr.Id, response);
		return;
	}
	if (!visorchannel_signalinsert(ControlVm_channel,
				       CONTROLVM_QUEUE_REQUEST, &outmsg)) {
		LOGERR("signalinsert failed!");
		return;
	}
}

static void
controlvm_respond_chipset_init(CONTROLVM_MESSAGE_HEADER *msgHdr, int response,
			       ULTRA_CHIPSET_FEATURE features)
{
	CONTROLVM_MESSAGE outmsg;
	if (!ControlVm_channel)
		return;
	controlvm_init_response(&outmsg, msgHdr, response);
	outmsg.cmd.initChipset.features = features;
	if (!visorchannel_signalinsert(ControlVm_channel,
				       CONTROLVM_QUEUE_REQUEST, &outmsg)) {
		LOGERR("signalinsert failed!");
		return;
	}
}

static void
controlvm_respond_physdev_changestate(CONTROLVM_MESSAGE_HEADER *msgHdr,
				      int response, ULTRA_SEGMENT_STATE state)
{
	CONTROLVM_MESSAGE outmsg;
	if (!ControlVm_channel)
		return;
	controlvm_init_response(&outmsg, msgHdr, response);
	outmsg.cmd.deviceChangeState.state = state;
	outmsg.cmd.deviceChangeState.flags.physicalDevice = 1;
	if (!visorchannel_signalinsert(ControlVm_channel,
				       CONTROLVM_QUEUE_REQUEST, &outmsg)) {
		LOGERR("signalinsert failed!");
		return;
	}
}

void
visorchipset_save_message(CONTROLVM_MESSAGE *msg, CRASH_OBJ_TYPE type)
{
	U32 localSavedCrashMsgOffset;
	U16 localSavedCrashMsgCount;

	/* get saved message count */
	if (visorchannel_read(ControlVm_channel,
			      offsetof(ULTRA_CONTROLVM_CHANNEL_PROTOCOL,
				       SavedCrashMsgCount),
			      &localSavedCrashMsgCount, sizeof(U16)) < 0) {
		LOGERR("failed to get Saved Message Count");
		POSTCODE_LINUX_2(CRASH_DEV_CTRL_RD_FAILURE_PC,
				 POSTCODE_SEVERITY_ERR);
		return;
	}

	if (localSavedCrashMsgCount != CONTROLVM_CRASHMSG_MAX) {
		LOGERR("Saved Message Count incorrect %d",
		       localSavedCrashMsgCount);
		POSTCODE_LINUX_3(CRASH_DEV_COUNT_FAILURE_PC,
				 localSavedCrashMsgCount,
				 POSTCODE_SEVERITY_ERR);
		return;
	}

	/* get saved crash message offset */
	if (visorchannel_read(ControlVm_channel,
			      offsetof(ULTRA_CONTROLVM_CHANNEL_PROTOCOL,
				       SavedCrashMsgOffset),
			      &localSavedCrashMsgOffset, sizeof(U32)) < 0) {
		LOGERR("failed to get Saved Message Offset");
		POSTCODE_LINUX_2(CRASH_DEV_CTRL_RD_FAILURE_PC,
				 POSTCODE_SEVERITY_ERR);
		return;
	}

	if (type == CRASH_bus) {
		if (visorchannel_write(ControlVm_channel,
				       localSavedCrashMsgOffset,
				       msg, sizeof(CONTROLVM_MESSAGE)) < 0) {
			LOGERR("SAVE_MSG_BUS_FAILURE: Failed to write CrashCreateBusMsg!");
			POSTCODE_LINUX_2(SAVE_MSG_BUS_FAILURE_PC,
					 POSTCODE_SEVERITY_ERR);
			return;
		}
	} else {
		if (visorchannel_write(ControlVm_channel,
				       localSavedCrashMsgOffset +
				       sizeof(CONTROLVM_MESSAGE), msg,
				       sizeof(CONTROLVM_MESSAGE)) < 0) {
			LOGERR("SAVE_MSG_DEV_FAILURE: Failed to write CrashCreateDevMsg!");
			POSTCODE_LINUX_2(SAVE_MSG_DEV_FAILURE_PC,
					 POSTCODE_SEVERITY_ERR);
			return;
		}
	}
}
EXPORT_SYMBOL_GPL(visorchipset_save_message);

static void
bus_responder(CONTROLVM_ID cmdId, ulong busNo, int response)
{
	VISORCHIPSET_BUS_INFO *p = NULL;
	BOOL need_clear = FALSE;

	p = findbus(&BusInfoList, busNo);
	if (!p) {
		LOGERR("internal error busNo=%lu", busNo);
		return;
	}
	if (response < 0) {
		if ((cmdId == CONTROLVM_BUS_CREATE) &&
		    (response != (-CONTROLVM_RESP_ERROR_ALREADY_DONE)))
			/* undo the row we just created... */
			delbusdevices(&DevInfoList, busNo);
	} else {
		if (cmdId == CONTROLVM_BUS_CREATE)
			p->state.created = 1;
		if (cmdId == CONTROLVM_BUS_DESTROY)
			need_clear = TRUE;
	}

	if (p->pendingMsgHdr.Id == CONTROLVM_INVALID) {
		LOGERR("bus_responder no pending msg");
		return;		/* no controlvm response needed */
	}
	if (p->pendingMsgHdr.Id != (U32) cmdId) {
		LOGERR("expected=%d, found=%d", cmdId, p->pendingMsgHdr.Id);
		return;
	}
	controlvm_respond(&p->pendingMsgHdr, response);
	p->pendingMsgHdr.Id = CONTROLVM_INVALID;
	if (need_clear) {
		busInfo_clear(p);
		delbusdevices(&DevInfoList, busNo);
	}
}

static void
device_changestate_responder(CONTROLVM_ID cmdId,
			     ulong busNo, ulong devNo, int response,
			     ULTRA_SEGMENT_STATE responseState)
{
	VISORCHIPSET_DEVICE_INFO *p = NULL;
	CONTROLVM_MESSAGE outmsg;

	if (!ControlVm_channel)
		return;

	p = finddevice(&DevInfoList, busNo, devNo);
	if (!p) {
		LOGERR("internal error; busNo=%lu, devNo=%lu", busNo, devNo);
		return;
	}
	if (p->pendingMsgHdr.Id == CONTROLVM_INVALID) {
		LOGERR("device_responder no pending msg");
		return;		/* no controlvm response needed */
	}
	if (p->pendingMsgHdr.Id != cmdId) {
		LOGERR("expected=%d, found=%d", cmdId, p->pendingMsgHdr.Id);
		return;
	}

	controlvm_init_response(&outmsg, &p->pendingMsgHdr, response);

	outmsg.cmd.deviceChangeState.busNo = busNo;
	outmsg.cmd.deviceChangeState.devNo = devNo;
	outmsg.cmd.deviceChangeState.state = responseState;

	if (!visorchannel_signalinsert(ControlVm_channel,
				       CONTROLVM_QUEUE_REQUEST, &outmsg)) {
		LOGERR("signalinsert failed!");
		return;
	}

	p->pendingMsgHdr.Id = CONTROLVM_INVALID;
}

static void
device_responder(CONTROLVM_ID cmdId, ulong busNo, ulong devNo, int response)
{
	VISORCHIPSET_DEVICE_INFO *p = NULL;
	BOOL need_clear = FALSE;

	p = finddevice(&DevInfoList, busNo, devNo);
	if (!p) {
		LOGERR("internal error; busNo=%lu, devNo=%lu", busNo, devNo);
		return;
	}
	if (response >= 0) {
		if (cmdId == CONTROLVM_DEVICE_CREATE)
			p->state.created = 1;
		if (cmdId == CONTROLVM_DEVICE_DESTROY)
			need_clear = TRUE;
	}

	if (p->pendingMsgHdr.Id == CONTROLVM_INVALID) {
		LOGERR("device_responder no pending msg");
		return;		/* no controlvm response needed */
	}
	if (p->pendingMsgHdr.Id != (U32) cmdId) {
		LOGERR("expected=%d, found=%d", cmdId, p->pendingMsgHdr.Id);
		return;
	}
	controlvm_respond(&p->pendingMsgHdr, response);
	p->pendingMsgHdr.Id = CONTROLVM_INVALID;
	if (need_clear)
		devInfo_clear(p);
}

static void
bus_epilog(U32 busNo,
	   U32 cmd, CONTROLVM_MESSAGE_HEADER *msgHdr,
	   int response, BOOL needResponse)
{
	BOOL notified = FALSE;

	VISORCHIPSET_BUS_INFO *pBusInfo = findbus(&BusInfoList, busNo);

	if (!pBusInfo) {
		LOGERR("HUH? bad busNo=%d", busNo);
		return;
	}
	if (needResponse) {
		memcpy(&pBusInfo->pendingMsgHdr, msgHdr,
		       sizeof(CONTROLVM_MESSAGE_HEADER));
	} else
		pBusInfo->pendingMsgHdr.Id = CONTROLVM_INVALID;

	LOCKSEM_UNINTERRUPTIBLE(&NotifierLock);
	if (response == CONTROLVM_RESP_SUCCESS) {
		switch (cmd) {
		case CONTROLVM_BUS_CREATE:
			/* We can't tell from the bus_create
			* information which of our 2 bus flavors the
			* devices on this bus will ultimately end up.
			* FORTUNATELY, it turns out it is harmless to
			* send the bus_create to both of them.  We can
			* narrow things down a little bit, though,
			* because we know: - BusDev_Server can handle
			* either server or client devices
			* - BusDev_Client can handle ONLY client
			* devices */
			if (BusDev_Server_Notifiers.bus_create) {
				(*BusDev_Server_Notifiers.bus_create) (busNo);
				notified = TRUE;
			}
			if ((!pBusInfo->flags.server) /*client */ &&
			    BusDev_Client_Notifiers.bus_create) {
				(*BusDev_Client_Notifiers.bus_create) (busNo);
				notified = TRUE;
			}
			break;
		case CONTROLVM_BUS_DESTROY:
			if (BusDev_Server_Notifiers.bus_destroy) {
				(*BusDev_Server_Notifiers.bus_destroy) (busNo);
				notified = TRUE;
			}
			if ((!pBusInfo->flags.server) /*client */ &&
			    BusDev_Client_Notifiers.bus_destroy) {
				(*BusDev_Client_Notifiers.bus_destroy) (busNo);
				notified = TRUE;
			}
			break;
		}
	}
	if (notified)
		/* The callback function just called above is responsible
		 * for calling the appropriate VISORCHIPSET_BUSDEV_RESPONDERS
		 * function, which will call bus_responder()
		 */
		;
	else
		bus_responder(cmd, busNo, response);
	UNLOCKSEM(&NotifierLock);
}

static void
device_epilog(U32 busNo, U32 devNo, ULTRA_SEGMENT_STATE state, U32 cmd,
	      CONTROLVM_MESSAGE_HEADER *msgHdr, int response,
	      BOOL needResponse, BOOL for_visorbus)
{
	VISORCHIPSET_BUSDEV_NOTIFIERS *notifiers = NULL;
	BOOL notified = FALSE;

	VISORCHIPSET_DEVICE_INFO *pDevInfo =
		finddevice(&DevInfoList, busNo, devNo);
	char *envp[] = {
		"SPARSP_DIAGPOOL_PAUSED_STATE = 1",
		NULL
	};

	if (!pDevInfo) {
		LOGERR("HUH? bad busNo=%d, devNo=%d", busNo, devNo);
		return;
	}
	if (for_visorbus)
		notifiers = &BusDev_Server_Notifiers;
	else
		notifiers = &BusDev_Client_Notifiers;
	if (needResponse) {
		memcpy(&pDevInfo->pendingMsgHdr, msgHdr,
		       sizeof(CONTROLVM_MESSAGE_HEADER));
	} else
		pDevInfo->pendingMsgHdr.Id = CONTROLVM_INVALID;

	LOCKSEM_UNINTERRUPTIBLE(&NotifierLock);
	if (response >= 0) {
		switch (cmd) {
		case CONTROLVM_DEVICE_CREATE:
			if (notifiers->device_create) {
				(*notifiers->device_create) (busNo, devNo);
				notified = TRUE;
			}
			break;
		case CONTROLVM_DEVICE_CHANGESTATE:
			/* ServerReady / ServerRunning / SegmentStateRunning */
			if (state.Alive == SegmentStateRunning.Alive &&
			    state.Operating == SegmentStateRunning.Operating) {
				if (notifiers->device_resume) {
					(*notifiers->device_resume) (busNo,
								     devNo);
					notified = TRUE;
				}
			}
			/* ServerNotReady / ServerLost / SegmentStateStandby */
			else if (state.Alive == SegmentStateStandby.Alive &&
				 state.Operating ==
				 SegmentStateStandby.Operating) {
				/* technically this is standby case
				 * where server is lost
				 */
				if (notifiers->device_pause) {
					(*notifiers->device_pause) (busNo,
								    devNo);
					notified = TRUE;
				}
			} else if (state.Alive == SegmentStatePaused.Alive &&
				   state.Operating ==
				   SegmentStatePaused.Operating) {
				/* this is lite pause where channel is
				 * still valid just 'pause' of it
				 */
				if (busNo == g_diagpoolBusNo
				    && devNo == g_diagpoolDevNo) {
					LOGINF("DEVICE_CHANGESTATE(DiagpoolChannel busNo=%d devNo=%d is pausing...)",
					     busNo, devNo);
					/* this will trigger the
					 * diag_shutdown.sh script in
					 * the visorchipset hotplug */
					kobject_uevent_env
					    (&Visorchipset_platform_device.dev.
					     kobj, KOBJ_ONLINE, envp);
				}
			}
			break;
		case CONTROLVM_DEVICE_DESTROY:
			if (notifiers->device_destroy) {
				(*notifiers->device_destroy) (busNo, devNo);
				notified = TRUE;
			}
			break;
		}
	}
	if (notified)
		/* The callback function just called above is responsible
		 * for calling the appropriate VISORCHIPSET_BUSDEV_RESPONDERS
		 * function, which will call device_responder()
		 */
		;
	else
		device_responder(cmd, busNo, devNo, response);
	UNLOCKSEM(&NotifierLock);
}

static void
bus_create(CONTROLVM_MESSAGE *inmsg)
{
	CONTROLVM_MESSAGE_PACKET *cmd = &inmsg->cmd;
	ulong busNo = cmd->createBus.busNo;
	int rc = CONTROLVM_RESP_SUCCESS;
	VISORCHIPSET_BUS_INFO *pBusInfo = NULL;


	pBusInfo = findbus(&BusInfoList, busNo);
	if (pBusInfo && (pBusInfo->state.created == 1)) {
		LOGERR("CONTROLVM_BUS_CREATE Failed: bus %lu already exists",
		       busNo);
		POSTCODE_LINUX_3(BUS_CREATE_FAILURE_PC, busNo,
				 POSTCODE_SEVERITY_ERR);
		rc = -CONTROLVM_RESP_ERROR_ALREADY_DONE;
		goto Away;
	}
	pBusInfo = kzalloc(sizeof(VISORCHIPSET_BUS_INFO), GFP_KERNEL);
	if (pBusInfo == NULL) {
		LOGERR("CONTROLVM_BUS_CREATE Failed: bus %lu kzalloc failed",
		       busNo);
		POSTCODE_LINUX_3(BUS_CREATE_FAILURE_PC, busNo,
				 POSTCODE_SEVERITY_ERR);
		rc = -CONTROLVM_RESP_ERROR_KMALLOC_FAILED;
		goto Away;
	}

	INIT_LIST_HEAD(&pBusInfo->entry);
	pBusInfo->busNo = busNo;
	pBusInfo->devNo = cmd->createBus.deviceCount;

	POSTCODE_LINUX_3(BUS_CREATE_ENTRY_PC, busNo, POSTCODE_SEVERITY_INFO);

	if (inmsg->hdr.Flags.testMessage == 1)
		pBusInfo->chanInfo.addrType = ADDRTYPE_localTest;
	else
		pBusInfo->chanInfo.addrType = ADDRTYPE_localPhysical;

	pBusInfo->flags.server = inmsg->hdr.Flags.server;
	pBusInfo->chanInfo.channelAddr = cmd->createBus.channelAddr;
	pBusInfo->chanInfo.nChannelBytes = cmd->createBus.channelBytes;
	pBusInfo->chanInfo.channelTypeGuid = cmd->createBus.busDataTypeGuid;
	pBusInfo->chanInfo.channelInstGuid = cmd->createBus.busInstGuid;

	list_add(&pBusInfo->entry, &BusInfoList);

	POSTCODE_LINUX_3(BUS_CREATE_EXIT_PC, busNo, POSTCODE_SEVERITY_INFO);

Away:
	bus_epilog(busNo, CONTROLVM_BUS_CREATE, &inmsg->hdr,
		   rc, inmsg->hdr.Flags.responseExpected == 1);
}

static void
bus_destroy(CONTROLVM_MESSAGE *inmsg)
{
	CONTROLVM_MESSAGE_PACKET *cmd = &inmsg->cmd;
	ulong busNo = cmd->destroyBus.busNo;
	VISORCHIPSET_BUS_INFO *pBusInfo;
	int rc = CONTROLVM_RESP_SUCCESS;

	pBusInfo = findbus(&BusInfoList, busNo);
	if (!pBusInfo) {
		LOGERR("CONTROLVM_BUS_DESTROY Failed: bus %lu invalid", busNo);
		rc = -CONTROLVM_RESP_ERROR_BUS_INVALID;
		goto Away;
	}
	if (pBusInfo->state.created == 0) {
		LOGERR("CONTROLVM_BUS_DESTROY Failed: bus %lu already destroyed",
		     busNo);
		rc = -CONTROLVM_RESP_ERROR_ALREADY_DONE;
		goto Away;
	}

Away:
	bus_epilog(busNo, CONTROLVM_BUS_DESTROY, &inmsg->hdr,
		   rc, inmsg->hdr.Flags.responseExpected == 1);
}

static void
bus_configure(CONTROLVM_MESSAGE *inmsg, PARSER_CONTEXT *parser_ctx)
{
	CONTROLVM_MESSAGE_PACKET *cmd = &inmsg->cmd;
	ulong busNo = cmd->configureBus.busNo;
	VISORCHIPSET_BUS_INFO *pBusInfo = NULL;
	int rc = CONTROLVM_RESP_SUCCESS;
	char s[99];

	busNo = cmd->configureBus.busNo;
	POSTCODE_LINUX_3(BUS_CONFIGURE_ENTRY_PC, busNo, POSTCODE_SEVERITY_INFO);

	pBusInfo = findbus(&BusInfoList, busNo);
	if (!pBusInfo) {
		LOGERR("CONTROLVM_BUS_CONFIGURE Failed: bus %lu invalid",
		       busNo);
		POSTCODE_LINUX_3(BUS_CONFIGURE_FAILURE_PC, busNo,
				 POSTCODE_SEVERITY_ERR);
		rc = -CONTROLVM_RESP_ERROR_BUS_INVALID;
		goto Away;
	}
	if (pBusInfo->state.created == 0) {
		LOGERR("CONTROLVM_BUS_CONFIGURE Failed: Invalid bus %lu - not created yet",
		     busNo);
		POSTCODE_LINUX_3(BUS_CONFIGURE_FAILURE_PC, busNo,
				 POSTCODE_SEVERITY_ERR);
		rc = -CONTROLVM_RESP_ERROR_BUS_INVALID;
		goto Away;
	}
	/* TBD - add this check to other commands also... */
	if (pBusInfo->pendingMsgHdr.Id != CONTROLVM_INVALID) {
		LOGERR("CONTROLVM_BUS_CONFIGURE Failed: bus %lu MsgId=%u outstanding",
		     busNo, (uint) pBusInfo->pendingMsgHdr.Id);
		POSTCODE_LINUX_3(BUS_CONFIGURE_FAILURE_PC, busNo,
				 POSTCODE_SEVERITY_ERR);
		rc = -CONTROLVM_RESP_ERROR_MESSAGE_ID_INVALID_FOR_CLIENT;
		goto Away;
	}

	pBusInfo->partitionHandle = cmd->configureBus.guestHandle;
	pBusInfo->partitionGuid = parser_id_get(parser_ctx);
	parser_param_start(parser_ctx, PARSERSTRING_NAME);
	pBusInfo->name = parser_string_get(parser_ctx);

	visorchannel_uuid_id(&pBusInfo->partitionGuid, s);
	pBusInfo->procObject =
	    visor_proc_CreateObject(PartitionType, s, (void *) (pBusInfo));
	if (pBusInfo->procObject == NULL) {
		LOGERR("CONTROLVM_BUS_CONFIGURE Failed: busNo=%lu failed to create /proc entry",
		     busNo);
		POSTCODE_LINUX_3(BUS_CONFIGURE_FAILURE_PC, busNo,
				 POSTCODE_SEVERITY_ERR);
		rc = -CONTROLVM_RESP_ERROR_KMALLOC_FAILED;
		goto Away;
	}
	POSTCODE_LINUX_3(BUS_CONFIGURE_EXIT_PC, busNo, POSTCODE_SEVERITY_INFO);
Away:
	bus_epilog(busNo, CONTROLVM_BUS_CONFIGURE, &inmsg->hdr,
		   rc, inmsg->hdr.Flags.responseExpected == 1);
}

static void
my_device_create(CONTROLVM_MESSAGE *inmsg)
{
	CONTROLVM_MESSAGE_PACKET *cmd = &inmsg->cmd;
	ulong busNo = cmd->createDevice.busNo;
	ulong devNo = cmd->createDevice.devNo;
	VISORCHIPSET_DEVICE_INFO *pDevInfo = NULL;
	VISORCHIPSET_BUS_INFO *pBusInfo = NULL;
	int rc = CONTROLVM_RESP_SUCCESS;

	pDevInfo = finddevice(&DevInfoList, busNo, devNo);
	if (pDevInfo && (pDevInfo->state.created == 1)) {
		LOGERR("CONTROLVM_DEVICE_CREATE Failed: busNo=%lu, devNo=%lu already exists",
		     busNo, devNo);
		POSTCODE_LINUX_4(DEVICE_CREATE_FAILURE_PC, devNo, busNo,
				 POSTCODE_SEVERITY_ERR);
		rc = -CONTROLVM_RESP_ERROR_ALREADY_DONE;
		goto Away;
	}
	pBusInfo = findbus(&BusInfoList, busNo);
	if (!pBusInfo) {
		LOGERR("CONTROLVM_DEVICE_CREATE Failed: Invalid bus %lu - out of range",
		     busNo);
		POSTCODE_LINUX_4(DEVICE_CREATE_FAILURE_PC, devNo, busNo,
				 POSTCODE_SEVERITY_ERR);
		rc = -CONTROLVM_RESP_ERROR_BUS_INVALID;
		goto Away;
	}
	if (pBusInfo->state.created == 0) {
		LOGERR("CONTROLVM_DEVICE_CREATE Failed: Invalid bus %lu - not created yet",
		     busNo);
		POSTCODE_LINUX_4(DEVICE_CREATE_FAILURE_PC, devNo, busNo,
				 POSTCODE_SEVERITY_ERR);
		rc = -CONTROLVM_RESP_ERROR_BUS_INVALID;
		goto Away;
	}
	pDevInfo = kzalloc(sizeof(VISORCHIPSET_DEVICE_INFO), GFP_KERNEL);
	if (pDevInfo == NULL) {
		LOGERR("CONTROLVM_DEVICE_CREATE Failed: busNo=%lu, devNo=%lu kmaloc failed",
		     busNo, devNo);
		POSTCODE_LINUX_4(DEVICE_CREATE_FAILURE_PC, devNo, busNo,
				 POSTCODE_SEVERITY_ERR);
		rc = -CONTROLVM_RESP_ERROR_KMALLOC_FAILED;
		goto Away;
	}

	INIT_LIST_HEAD(&pDevInfo->entry);
	pDevInfo->busNo = busNo;
	pDevInfo->devNo = devNo;
	pDevInfo->devInstGuid = cmd->createDevice.devInstGuid;
	POSTCODE_LINUX_4(DEVICE_CREATE_ENTRY_PC, devNo, busNo,
			 POSTCODE_SEVERITY_INFO);

	if (inmsg->hdr.Flags.testMessage == 1)
		pDevInfo->chanInfo.addrType = ADDRTYPE_localTest;
	else
		pDevInfo->chanInfo.addrType = ADDRTYPE_localPhysical;
	pDevInfo->chanInfo.channelAddr = cmd->createDevice.channelAddr;
	pDevInfo->chanInfo.nChannelBytes = cmd->createDevice.channelBytes;
	pDevInfo->chanInfo.channelTypeGuid = cmd->createDevice.dataTypeGuid;
	pDevInfo->chanInfo.intr = cmd->createDevice.intr;
	list_add(&pDevInfo->entry, &DevInfoList);
	POSTCODE_LINUX_4(DEVICE_CREATE_EXIT_PC, devNo, busNo,
			 POSTCODE_SEVERITY_INFO);
Away:
	/* get the bus and devNo for DiagPool channel */
	if (is_diagpool_channel(pDevInfo->chanInfo.channelTypeGuid)) {
		g_diagpoolBusNo = busNo;
		g_diagpoolDevNo = devNo;
		LOGINF("CONTROLVM_DEVICE_CREATE for DiagPool channel: busNo=%lu, devNo=%lu",
		     g_diagpoolBusNo, g_diagpoolDevNo);
	}
	device_epilog(busNo, devNo, SegmentStateRunning,
		      CONTROLVM_DEVICE_CREATE, &inmsg->hdr, rc,
		      inmsg->hdr.Flags.responseExpected == 1,
		      FOR_VISORBUS(pDevInfo->chanInfo.channelTypeGuid));
}

static void
my_device_changestate(CONTROLVM_MESSAGE *inmsg)
{
	CONTROLVM_MESSAGE_PACKET *cmd = &inmsg->cmd;
	ulong busNo = cmd->deviceChangeState.busNo;
	ulong devNo = cmd->deviceChangeState.devNo;
	ULTRA_SEGMENT_STATE state = cmd->deviceChangeState.state;
	VISORCHIPSET_DEVICE_INFO *pDevInfo = NULL;
	int rc = CONTROLVM_RESP_SUCCESS;

	pDevInfo = finddevice(&DevInfoList, busNo, devNo);
	if (!pDevInfo) {
		LOGERR("CONTROLVM_DEVICE_CHANGESTATE Failed: busNo=%lu, devNo=%lu invalid (doesn't exist)",
		     busNo, devNo);
		POSTCODE_LINUX_4(DEVICE_CHANGESTATE_FAILURE_PC, devNo, busNo,
				 POSTCODE_SEVERITY_ERR);
		rc = -CONTROLVM_RESP_ERROR_DEVICE_INVALID;
		goto Away;
	}
	if (pDevInfo->state.created == 0) {
		LOGERR("CONTROLVM_DEVICE_CHANGESTATE Failed: busNo=%lu, devNo=%lu invalid (not created)",
		     busNo, devNo);
		POSTCODE_LINUX_4(DEVICE_CHANGESTATE_FAILURE_PC, devNo, busNo,
				 POSTCODE_SEVERITY_ERR);
		rc = -CONTROLVM_RESP_ERROR_DEVICE_INVALID;
	}
Away:
	if ((rc >= CONTROLVM_RESP_SUCCESS) && pDevInfo)
		device_epilog(busNo, devNo, state, CONTROLVM_DEVICE_CHANGESTATE,
			      &inmsg->hdr, rc,
			      inmsg->hdr.Flags.responseExpected == 1,
			      FOR_VISORBUS(pDevInfo->chanInfo.channelTypeGuid));
}

static void
my_device_destroy(CONTROLVM_MESSAGE *inmsg)
{
	CONTROLVM_MESSAGE_PACKET *cmd = &inmsg->cmd;
	ulong busNo = cmd->destroyDevice.busNo;
	ulong devNo = cmd->destroyDevice.devNo;
	VISORCHIPSET_DEVICE_INFO *pDevInfo = NULL;
	int rc = CONTROLVM_RESP_SUCCESS;

	pDevInfo = finddevice(&DevInfoList, busNo, devNo);
	if (!pDevInfo) {
		LOGERR("CONTROLVM_DEVICE_DESTROY Failed: busNo=%lu, devNo=%lu invalid",
		     busNo, devNo);
		rc = -CONTROLVM_RESP_ERROR_DEVICE_INVALID;
		goto Away;
	}
	if (pDevInfo->state.created == 0) {
		LOGERR("CONTROLVM_DEVICE_DESTROY Failed: busNo=%lu, devNo=%lu already destroyed",
		     busNo, devNo);
		rc = -CONTROLVM_RESP_ERROR_ALREADY_DONE;
	}

Away:
	if ((rc >= CONTROLVM_RESP_SUCCESS) && pDevInfo)
		device_epilog(busNo, devNo, SegmentStateRunning,
			      CONTROLVM_DEVICE_DESTROY, &inmsg->hdr, rc,
			      inmsg->hdr.Flags.responseExpected == 1,
			      FOR_VISORBUS(pDevInfo->chanInfo.channelTypeGuid));
}

/* When provided with the physical address of the controlvm channel
 * (phys_addr), the offset to the payload area we need to manage
 * (offset), and the size of this payload area (bytes), fills in the
 * CONTROLVM_PAYLOAD_INFO struct.  Returns TRUE for success or FALSE
 * for failure.
 */
static int
initialize_controlvm_payload_info(HOSTADDRESS phys_addr, U64 offset, U32 bytes,
				  CONTROLVM_PAYLOAD_INFO *info)
{
	U8 __iomem *payload = NULL;
	int rc = CONTROLVM_RESP_SUCCESS;

	if (info == NULL) {
		LOGERR("HUH ? CONTROLVM_PAYLOAD_INIT Failed : Programmer check at %s:%d",
		     __FILE__, __LINE__);
		rc = -CONTROLVM_RESP_ERROR_PAYLOAD_INVALID;
		goto Away;
	}
	memset(info, 0, sizeof(CONTROLVM_PAYLOAD_INFO));
	if ((offset == 0) || (bytes == 0)) {
		LOGERR("CONTROLVM_PAYLOAD_INIT Failed: RequestPayloadOffset=%llu RequestPayloadBytes=%llu!",
		     (u64) offset, (u64) bytes);
		rc = -CONTROLVM_RESP_ERROR_PAYLOAD_INVALID;
		goto Away;
	}
	payload = ioremap_cache(phys_addr + offset, bytes);
	if (payload == NULL) {
		LOGERR("CONTROLVM_PAYLOAD_INIT Failed: ioremap_cache %llu for %llu bytes failed",
		     (u64) offset, (u64) bytes);
		rc = -CONTROLVM_RESP_ERROR_IOREMAP_FAILED;
		goto Away;
	}

	info->offset = offset;
	info->bytes = bytes;
	info->ptr = payload;
	LOGINF("offset=%llu, bytes=%lu, ptr=%p",
	       (u64) (info->offset), (ulong) (info->bytes), info->ptr);

Away:
	if (rc < 0) {
		if (payload != NULL) {
			iounmap(payload);
			payload = NULL;
		}
	}
	return rc;
}

static void
destroy_controlvm_payload_info(CONTROLVM_PAYLOAD_INFO *info)
{
	if (info->ptr != NULL) {
		iounmap(info->ptr);
		info->ptr = NULL;
	}
	memset(info, 0, sizeof(CONTROLVM_PAYLOAD_INFO));
}

static void
initialize_controlvm_payload(void)
{
	HOSTADDRESS phys_addr = visorchannel_get_physaddr(ControlVm_channel);
	U64 payloadOffset = 0;
	U32 payloadBytes = 0;
	if (visorchannel_read(ControlVm_channel,
			      offsetof(ULTRA_CONTROLVM_CHANNEL_PROTOCOL,
				       RequestPayloadOffset),
			      &payloadOffset, sizeof(payloadOffset)) < 0) {
		LOGERR("CONTROLVM_PAYLOAD_INIT Failed to read controlvm channel!");
		POSTCODE_LINUX_2(CONTROLVM_INIT_FAILURE_PC,
				 POSTCODE_SEVERITY_ERR);
		return;
	}
	if (visorchannel_read(ControlVm_channel,
			      offsetof(ULTRA_CONTROLVM_CHANNEL_PROTOCOL,
				       RequestPayloadBytes),
			      &payloadBytes, sizeof(payloadBytes)) < 0) {
		LOGERR("CONTROLVM_PAYLOAD_INIT Failed to read controlvm channel!");
		POSTCODE_LINUX_2(CONTROLVM_INIT_FAILURE_PC,
				 POSTCODE_SEVERITY_ERR);
		return;
	}
	initialize_controlvm_payload_info(phys_addr,
					  payloadOffset, payloadBytes,
					  &ControlVm_payload_info);
}

/*  Send ACTION=online for DEVPATH=/sys/devices/platform/visorchipset.
 *  Returns CONTROLVM_RESP_xxx code.
 */
int
visorchipset_chipset_ready(void)
{
	kobject_uevent(&Visorchipset_platform_device.dev.kobj, KOBJ_ONLINE);
	return CONTROLVM_RESP_SUCCESS;
}
EXPORT_SYMBOL_GPL(visorchipset_chipset_ready);

int
visorchipset_chipset_selftest(void)
{
	char env_selftest[20];
	char *envp[] = { env_selftest, NULL };
	sprintf(env_selftest, "SPARSP_SELFTEST=%d", 1);
	kobject_uevent_env(&Visorchipset_platform_device.dev.kobj, KOBJ_CHANGE,
			   envp);
	return CONTROLVM_RESP_SUCCESS;
}
EXPORT_SYMBOL_GPL(visorchipset_chipset_selftest);

/*  Send ACTION=offline for DEVPATH=/sys/devices/platform/visorchipset.
 *  Returns CONTROLVM_RESP_xxx code.
 */
int
visorchipset_chipset_notready(void)
{
	kobject_uevent(&Visorchipset_platform_device.dev.kobj, KOBJ_OFFLINE);
	return CONTROLVM_RESP_SUCCESS;
}
EXPORT_SYMBOL_GPL(visorchipset_chipset_notready);

static void
chipset_ready(CONTROLVM_MESSAGE_HEADER *msgHdr)
{
	int rc = visorchipset_chipset_ready();
	if (rc != CONTROLVM_RESP_SUCCESS)
		rc = -rc;
	if (msgHdr->Flags.responseExpected && !visorchipset_holdchipsetready)
		controlvm_respond(msgHdr, rc);
	if (msgHdr->Flags.responseExpected && visorchipset_holdchipsetready) {
		/* Send CHIPSET_READY response when all modules have been loaded
		 * and disks mounted for the partition
		 */
		g_ChipSetMsgHdr = *msgHdr;
		LOGINF("Holding CHIPSET_READY response");
	}
}

static void
chipset_selftest(CONTROLVM_MESSAGE_HEADER *msgHdr)
{
	int rc = visorchipset_chipset_selftest();
	if (rc != CONTROLVM_RESP_SUCCESS)
		rc = -rc;
	if (msgHdr->Flags.responseExpected)
		controlvm_respond(msgHdr, rc);
}

static void
chipset_notready(CONTROLVM_MESSAGE_HEADER *msgHdr)
{
	int rc = visorchipset_chipset_notready();
	if (rc != CONTROLVM_RESP_SUCCESS)
		rc = -rc;
	if (msgHdr->Flags.responseExpected)
		controlvm_respond(msgHdr, rc);
}

/* This is your "one-stop" shop for grabbing the next message from the
 * CONTROLVM_QUEUE_EVENT queue in the controlvm channel.
 */
static BOOL
read_controlvm_event(CONTROLVM_MESSAGE *msg)
{
	if (visorchannel_signalremove(ControlVm_channel,
				      CONTROLVM_QUEUE_EVENT, msg)) {
		/* got a message */
		if (msg->hdr.Flags.testMessage == 1) {
			LOGERR("ignoring bad CONTROLVM_QUEUE_EVENT msg with controlvm_msg_id=0x%x because Flags.testMessage is nonsensical (=1)", msg->hdr.Id);
			return FALSE;
		} else
			return TRUE;
	}
	return FALSE;
}

/*
 * The general parahotplug flow works as follows.  The visorchipset
 * driver receives a DEVICE_CHANGESTATE message from Command
 * specifying a physical device to enable or disable.  The CONTROLVM
 * message handler calls parahotplug_process_message, which then adds
 * the message to a global list and kicks off a udev event which
 * causes a user level script to enable or disable the specified
 * device.  The udev script then writes to
 * /proc/visorchipset/parahotplug, which causes parahotplug_proc_write
 * to get called, at which point the appropriate CONTROLVM message is
 * retrieved from the list and responded to.
 */

#define PARAHOTPLUG_TIMEOUT_MS 2000

/*
 * Generate unique int to match an outstanding CONTROLVM message with a
 * udev script /proc response
 */
static int
parahotplug_next_id(void)
{
	static atomic_t id = ATOMIC_INIT(0);
	return atomic_inc_return(&id);
}

/*
 * Returns the time (in jiffies) when a CONTROLVM message on the list
 * should expire -- PARAHOTPLUG_TIMEOUT_MS in the future
 */
static unsigned long
parahotplug_next_expiration(void)
{
	return jiffies + PARAHOTPLUG_TIMEOUT_MS * HZ / 1000;
}

/*
 * Create a parahotplug_request, which is basically a wrapper for a
 * CONTROLVM_MESSAGE that we can stick on a list
 */
static struct parahotplug_request *
parahotplug_request_create(CONTROLVM_MESSAGE *msg)
{
	struct parahotplug_request *req =
	    kmalloc(sizeof(struct parahotplug_request),
		    GFP_KERNEL|__GFP_NORETRY);
	if (req == NULL)
		return NULL;

	req->id = parahotplug_next_id();
	req->expiration = parahotplug_next_expiration();
	req->msg = *msg;

	return req;
}

/*
 * Free a parahotplug_request.
 */
static void
parahotplug_request_destroy(struct parahotplug_request *req)
{
	kfree(req);
}

/*
 * Cause uevent to run the user level script to do the disable/enable
 * specified in (the CONTROLVM message in) the specified
 * parahotplug_request
 */
static void
parahotplug_request_kickoff(struct parahotplug_request *req)
{
	CONTROLVM_MESSAGE_PACKET *cmd = &req->msg.cmd;
	char env_cmd[40], env_id[40], env_state[40], env_bus[40], env_dev[40],
	    env_func[40];
	char *envp[] = {
		env_cmd, env_id, env_state, env_bus, env_dev, env_func, NULL
	};

	sprintf(env_cmd, "SPAR_PARAHOTPLUG=1");
	sprintf(env_id, "SPAR_PARAHOTPLUG_ID=%d", req->id);
	sprintf(env_state, "SPAR_PARAHOTPLUG_STATE=%d",
		cmd->deviceChangeState.state.Active);
	sprintf(env_bus, "SPAR_PARAHOTPLUG_BUS=%d",
		cmd->deviceChangeState.busNo);
	sprintf(env_dev, "SPAR_PARAHOTPLUG_DEVICE=%d",
		cmd->deviceChangeState.devNo >> 3);
	sprintf(env_func, "SPAR_PARAHOTPLUG_FUNCTION=%d",
		cmd->deviceChangeState.devNo & 0x7);

	LOGINF("parahotplug_request_kickoff: state=%d, bdf=%d/%d/%d, id=%u\n",
	       cmd->deviceChangeState.state.Active,
	       cmd->deviceChangeState.busNo, cmd->deviceChangeState.devNo >> 3,
	       cmd->deviceChangeState.devNo & 7, req->id);

	kobject_uevent_env(&Visorchipset_platform_device.dev.kobj, KOBJ_CHANGE,
			   envp);
}

/*
 * Remove any request from the list that's been on there too long and
 * respond with an error.
 */
static void
parahotplug_process_list(void)
{
	struct list_head *pos = NULL;
	struct list_head *tmp = NULL;

	spin_lock(&Parahotplug_request_list_lock);

	list_for_each_safe(pos, tmp, &Parahotplug_request_list) {
		struct parahotplug_request *req =
		    list_entry(pos, struct parahotplug_request, list);
		if (time_after_eq(jiffies, req->expiration)) {
			list_del(pos);
			if (req->msg.hdr.Flags.responseExpected)
				controlvm_respond_physdev_changestate(
					&req->msg.hdr,
					CONTROLVM_RESP_ERROR_DEVICE_UDEV_TIMEOUT,
					req->msg.cmd.deviceChangeState.state);
			parahotplug_request_destroy(req);
		}
	}

	spin_unlock(&Parahotplug_request_list_lock);
}

/*
 * Called from the /proc handler, which means the user script has
 * finished the enable/disable.  Find the matching identifier, and
 * respond to the CONTROLVM message with success.
 */
static int
parahotplug_request_complete(int id, U16 active)
{
	struct list_head *pos = NULL;
	struct list_head *tmp = NULL;

	spin_lock(&Parahotplug_request_list_lock);

	/* Look for a request matching "id". */
	list_for_each_safe(pos, tmp, &Parahotplug_request_list) {
		struct parahotplug_request *req =
		    list_entry(pos, struct parahotplug_request, list);
		if (req->id == id) {
			/* Found a match.  Remove it from the list and
			 * respond.
			 */
			list_del(pos);
			spin_unlock(&Parahotplug_request_list_lock);
			req->msg.cmd.deviceChangeState.state.Active = active;
			if (req->msg.hdr.Flags.responseExpected)
				controlvm_respond_physdev_changestate(
					&req->msg.hdr, CONTROLVM_RESP_SUCCESS,
					req->msg.cmd.deviceChangeState.state);
			parahotplug_request_destroy(req);
			return 0;
		}
	}

	spin_unlock(&Parahotplug_request_list_lock);
	return -1;
}

/*
 * Enables or disables a PCI device by kicking off a udev script
 */
static void
parahotplug_process_message(CONTROLVM_MESSAGE *inmsg)
{
	struct parahotplug_request *req;

	req = parahotplug_request_create(inmsg);

	if (req == NULL) {
		LOGERR("parahotplug_process_message: couldn't allocate request");
		return;
	}

	if (inmsg->cmd.deviceChangeState.state.Active) {
		/* For enable messages, just respond with success
		* right away.  This is a bit of a hack, but there are
		* issues with the early enable messages we get (with
		* either the udev script not detecting that the device
		* is up, or not getting called at all).  Fortunately
		* the messages that get lost don't matter anyway, as
		* devices are automatically enabled at
		* initialization.
		*/
		parahotplug_request_kickoff(req);
		controlvm_respond_physdev_changestate(&inmsg->hdr,
						      CONTROLVM_RESP_SUCCESS,
						      inmsg->cmd.
						      deviceChangeState.state);
		parahotplug_request_destroy(req);
	} else {
		/* For disable messages, add the request to the
		* request list before kicking off the udev script.  It
		* won't get responded to until the script has
		* indicated it's done.
		*/
		spin_lock(&Parahotplug_request_list_lock);
		list_add_tail(&(req->list), &Parahotplug_request_list);
		spin_unlock(&Parahotplug_request_list_lock);

		parahotplug_request_kickoff(req);
	}
}

/*
 * Gets called when the udev script writes to
 * /proc/visorchipset/parahotplug.  Expects input in the form of "<id>
 * <active>" where <id> is the identifier passed to the script that
 * matches a request on the request list, and <active> is 0 or 1
 * indicating whether the device is now enabled or not.
 */
static ssize_t
parahotplug_proc_write(struct file *file, const char __user *buffer,
		       size_t count, loff_t *ppos)
{
	char buf[64];
	uint id;
	ushort active;

	if (count > sizeof(buf) - 1) {
		LOGERR("parahotplug_proc_write: count (%d) exceeds size of buffer (%d)",
		     (int) count, (int) sizeof(buf));
		return -EINVAL;
	}
	if (copy_from_user(buf, buffer, count)) {
		LOGERR("parahotplug_proc_write: copy_from_user failed");
		return -EFAULT;
	}
	buf[count] = '\0';

	if (sscanf(buf, "%u %hu", &id, &active) != 2) {
		id = 0;
		active = 0;
	}

	if (active != 1 && active != 0) {
		LOGERR("parahotplug_proc_write: invalid active field");
		return -EINVAL;
	}

	parahotplug_request_complete((int) id, (U16) active);

	return count;
}

static const struct file_operations parahotplug_proc_fops = {
	.owner = THIS_MODULE,
	.read = visorchipset_proc_read_writeonly,
	.write = parahotplug_proc_write,
};

/* Process a controlvm message.
 * Return result:
 *    FALSE - this function will return FALSE only in the case where the
 *            controlvm message was NOT processed, but processing must be
 *            retried before reading the next controlvm message; a
 *            scenario where this can occur is when we need to throttle
 *            the allocation of memory in which to copy out controlvm
 *            payload data
 *    TRUE  - processing of the controlvm message completed,
 *            either successfully or with an error.
 */
static BOOL
handle_command(CONTROLVM_MESSAGE inmsg, HOSTADDRESS channel_addr)
{
	CONTROLVM_MESSAGE_PACKET *cmd = &inmsg.cmd;
	U64 parametersAddr = 0;
	U32 parametersBytes = 0;
	PARSER_CONTEXT *parser_ctx = NULL;
	BOOL isLocalAddr = FALSE;
	CONTROLVM_MESSAGE ackmsg;

	/* create parsing context if necessary */
	isLocalAddr = (inmsg.hdr.Flags.testMessage == 1);
	if (channel_addr == 0) {
		LOGERR("HUH? channel_addr is 0!");
		return TRUE;
	}
	parametersAddr = channel_addr + inmsg.hdr.PayloadVmOffset;
	parametersBytes = inmsg.hdr.PayloadBytes;

	/* Parameter and channel addresses within test messages actually lie
	 * within our OS-controlled memory.  We need to know that, because it
	 * makes a difference in how we compute the virtual address.
	 */
	if (parametersAddr != 0 && parametersBytes != 0) {
		BOOL retry = FALSE;
		parser_ctx =
		    parser_init_byteStream(parametersAddr, parametersBytes,
					   isLocalAddr, &retry);
		if (!parser_ctx) {
			if (retry) {
				LOGWRN("throttling to copy payload");
				return FALSE;
			}
			LOGWRN("parsing failed");
			LOGWRN("inmsg.hdr.Id=0x%lx", (ulong) inmsg.hdr.Id);
			LOGWRN("parametersAddr=0x%llx", (u64) parametersAddr);
			LOGWRN("parametersBytes=%lu", (ulong) parametersBytes);
			LOGWRN("isLocalAddr=%d", isLocalAddr);
		}
	}

	if (!isLocalAddr) {
		controlvm_init_response(&ackmsg, &inmsg.hdr,
					CONTROLVM_RESP_SUCCESS);
		if ((ControlVm_channel)
		    &&
		    (!visorchannel_signalinsert
		     (ControlVm_channel, CONTROLVM_QUEUE_ACK, &ackmsg)))
			LOGWRN("failed to send ACK failed");
	}
	switch (inmsg.hdr.Id) {
	case CONTROLVM_CHIPSET_INIT:
		LOGINF("CHIPSET_INIT(#busses=%lu,#switches=%lu)",
		       (ulong) inmsg.cmd.initChipset.busCount,
		       (ulong) inmsg.cmd.initChipset.switchCount);
		chipset_init(&inmsg);
		break;
	case CONTROLVM_BUS_CREATE:
		LOGINF("BUS_CREATE(%lu,#devs=%lu)",
		       (ulong) cmd->createBus.busNo,
		       (ulong) cmd->createBus.deviceCount);
		bus_create(&inmsg);
		break;
	case CONTROLVM_BUS_DESTROY:
		LOGINF("BUS_DESTROY(%lu)", (ulong) cmd->destroyBus.busNo);
		bus_destroy(&inmsg);
		break;
	case CONTROLVM_BUS_CONFIGURE:
		LOGINF("BUS_CONFIGURE(%lu)", (ulong) cmd->configureBus.busNo);
		bus_configure(&inmsg, parser_ctx);
		break;
	case CONTROLVM_DEVICE_CREATE:
		LOGINF("DEVICE_CREATE(%lu,%lu)",
		       (ulong) cmd->createDevice.busNo,
		       (ulong) cmd->createDevice.devNo);
		my_device_create(&inmsg);
		break;
	case CONTROLVM_DEVICE_CHANGESTATE:
		if (cmd->deviceChangeState.flags.physicalDevice) {
			LOGINF("DEVICE_CHANGESTATE for physical device (%lu,%lu, active=%lu)",
			     (ulong) cmd->deviceChangeState.busNo,
			     (ulong) cmd->deviceChangeState.devNo,
			     (ulong) cmd->deviceChangeState.state.Active);
			parahotplug_process_message(&inmsg);
		} else {
			LOGINF("DEVICE_CHANGESTATE for virtual device (%lu,%lu, state.Alive=0x%lx)",
			     (ulong) cmd->deviceChangeState.busNo,
			     (ulong) cmd->deviceChangeState.devNo,
			     (ulong) cmd->deviceChangeState.state.Alive);
			/* save the hdr and cmd structures for later use */
			/* when sending back the response to Command */
			my_device_changestate(&inmsg);
			g_DiagMsgHdr = inmsg.hdr;
			g_DeviceChangeStatePacket = inmsg.cmd;
			break;
		}
		break;
	case CONTROLVM_DEVICE_DESTROY:
		LOGINF("DEVICE_DESTROY(%lu,%lu)",
		       (ulong) cmd->destroyDevice.busNo,
		       (ulong) cmd->destroyDevice.devNo);
		my_device_destroy(&inmsg);
		break;
	case CONTROLVM_DEVICE_CONFIGURE:
		LOGINF("DEVICE_CONFIGURE(%lu,%lu)",
		       (ulong) cmd->configureDevice.busNo,
		       (ulong) cmd->configureDevice.devNo);
		/* no op for now, just send a respond that we passed */
		if (inmsg.hdr.Flags.responseExpected)
			controlvm_respond(&inmsg.hdr, CONTROLVM_RESP_SUCCESS);
		break;
	case CONTROLVM_CHIPSET_READY:
		LOGINF("CHIPSET_READY");
		chipset_ready(&inmsg.hdr);
		break;
	case CONTROLVM_CHIPSET_SELFTEST:
		LOGINF("CHIPSET_SELFTEST");
		chipset_selftest(&inmsg.hdr);
		break;
	case CONTROLVM_CHIPSET_STOP:
		LOGINF("CHIPSET_STOP");
		chipset_notready(&inmsg.hdr);
		break;
	default:
		LOGERR("unrecognized controlvm cmd=%d", (int) inmsg.hdr.Id);
		if (inmsg.hdr.Flags.responseExpected)
			controlvm_respond(&inmsg.hdr,
					  -CONTROLVM_RESP_ERROR_MESSAGE_ID_UNKNOWN);
		break;
	}

	if (parser_ctx != NULL) {
		parser_done(parser_ctx);
		parser_ctx = NULL;
	}
	return TRUE;
}

static void
controlvm_periodic_work(struct work_struct *work)
{
	VISORCHIPSET_CHANNEL_INFO chanInfo;
	CONTROLVM_MESSAGE inmsg;
	char s[99];
	BOOL gotACommand = FALSE;
	BOOL handle_command_failed = FALSE;
	static U64 Poll_Count;

	/* make sure visorbus server is registered for controlvm callbacks */
	if (visorchipset_serverregwait && !serverregistered)
		goto Away;
	/* make sure visorclientbus server is regsitered for controlvm
	 * callbacks
	 */
	if (visorchipset_clientregwait && !clientregistered)
		goto Away;

	memset(&chanInfo, 0, sizeof(VISORCHIPSET_CHANNEL_INFO));
	if (!ControlVm_channel) {
		HOSTADDRESS addr = controlvm_get_channel_address();
		if (addr != 0) {
			ControlVm_channel =
			    visorchannel_create_with_lock
			    (addr,
			     sizeof(ULTRA_CONTROLVM_CHANNEL_PROTOCOL),
			     UltraControlvmChannelProtocolGuid);
			if (ControlVm_channel == NULL)
				LOGERR("failed to create controlvm channel");
			else if (ULTRA_CONTROLVM_CHANNEL_OK_CLIENT
				 (visorchannel_get_header(ControlVm_channel),
				  NULL)) {
				LOGINF("Channel %s (ControlVm) discovered",
				       visorchannel_id(ControlVm_channel, s));
				initialize_controlvm_payload();
			} else {
				LOGERR("controlvm channel is invalid");
				visorchannel_destroy(ControlVm_channel);
				ControlVm_channel = NULL;
			}
		}
	}

	Poll_Count++;
	if ((ControlVm_channel != NULL) || (Poll_Count >= 250))
		;	/* keep going */
	else
		goto Away;

	/* Check events to determine if response to CHIPSET_READY
	 * should be sent
	 */
	if (visorchipset_holdchipsetready
	    && (g_ChipSetMsgHdr.Id != CONTROLVM_INVALID)) {
		if (check_chipset_events() == 1) {
			LOGINF("Sending CHIPSET_READY response");
			controlvm_respond(&g_ChipSetMsgHdr, 0);
			clear_chipset_events();
			memset(&g_ChipSetMsgHdr, 0,
			       sizeof(CONTROLVM_MESSAGE_HEADER));
		}
	}

	if (ControlVm_channel) {
		while (visorchannel_signalremove(ControlVm_channel,
						 CONTROLVM_QUEUE_RESPONSE,
						 &inmsg)) {
			if (inmsg.hdr.PayloadMaxBytes != 0) {
				LOGERR("Payload of size %lu returned @%lu with unexpected message id %d.",
				     (ulong) inmsg.hdr.PayloadMaxBytes,
				     (ulong) inmsg.hdr.PayloadVmOffset,
				     inmsg.hdr.Id);
			}
		}
		if (!gotACommand) {
			if (ControlVm_Pending_Msg_Valid) {
				/* we throttled processing of a prior
				* msg, so try to process it again
				* rather than reading a new one
				*/
				inmsg = ControlVm_Pending_Msg;
				ControlVm_Pending_Msg_Valid = FALSE;
				gotACommand = TRUE;
			} else
				gotACommand = read_controlvm_event(&inmsg);
		}
	}

	handle_command_failed = FALSE;
	while (gotACommand && (!handle_command_failed)) {
		Most_recent_message_jiffies = jiffies;
		if (ControlVm_channel) {
			if (handle_command(inmsg,
					   visorchannel_get_physaddr
					   (ControlVm_channel)))
				gotACommand = read_controlvm_event(&inmsg);
			else {
				/* this is a scenario where throttling
				* is required, but probably NOT an
				* error...; we stash the current
				* controlvm msg so we will attempt to
				* reprocess it on our next loop
				*/
				handle_command_failed = TRUE;
				ControlVm_Pending_Msg = inmsg;
				ControlVm_Pending_Msg_Valid = TRUE;
			}

		} else {
			handle_command(inmsg, 0);
			gotACommand = FALSE;
		}
	}

	/* parahotplug_worker */
	parahotplug_process_list();

Away:

	if (time_after(jiffies,
		       Most_recent_message_jiffies + (HZ * MIN_IDLE_SECONDS))) {
		/* it's been longer than MIN_IDLE_SECONDS since we
		* processed our last controlvm message; slow down the
		* polling
		*/
		if (Poll_jiffies != POLLJIFFIES_CONTROLVMCHANNEL_SLOW) {
			LOGINF("switched to slow controlvm polling");
			Poll_jiffies = POLLJIFFIES_CONTROLVMCHANNEL_SLOW;
		}
	} else {
		if (Poll_jiffies != POLLJIFFIES_CONTROLVMCHANNEL_FAST) {
			Poll_jiffies = POLLJIFFIES_CONTROLVMCHANNEL_FAST;
			LOGINF("switched to fast controlvm polling");
		}
	}

	queue_delayed_work(Periodic_controlvm_workqueue,
			   &Periodic_controlvm_work, Poll_jiffies);
}

static void
setup_crash_devices_work_queue(struct work_struct *work)
{

	CONTROLVM_MESSAGE localCrashCreateBusMsg;
	CONTROLVM_MESSAGE localCrashCreateDevMsg;
	CONTROLVM_MESSAGE msg;
	HOSTADDRESS host_addr;
	U32 localSavedCrashMsgOffset;
	U16 localSavedCrashMsgCount;

	/* make sure visorbus server is registered for controlvm callbacks */
	if (visorchipset_serverregwait && !serverregistered)
		goto Away;

	/* make sure visorclientbus server is regsitered for controlvm
	 * callbacks
	 */
	if (visorchipset_clientregwait && !clientregistered)
		goto Away;

	POSTCODE_LINUX_2(CRASH_DEV_ENTRY_PC, POSTCODE_SEVERITY_INFO);

	/* send init chipset msg */
	msg.hdr.Id = CONTROLVM_CHIPSET_INIT;
	msg.cmd.initChipset.busCount = 23;
	msg.cmd.initChipset.switchCount = 0;

	chipset_init(&msg);

	host_addr = controlvm_get_channel_address();
	if (!host_addr) {
		LOGERR("Huh?  Host address is NULL");
		POSTCODE_LINUX_2(CRASH_DEV_HADDR_NULL, POSTCODE_SEVERITY_ERR);
		return;
	}

	ControlVm_channel =
	    visorchannel_create_with_lock
	    (host_addr,
	     sizeof(ULTRA_CONTROLVM_CHANNEL_PROTOCOL),
	     UltraControlvmChannelProtocolGuid);

	if (ControlVm_channel == NULL) {
		LOGERR("failed to create controlvm channel");
		POSTCODE_LINUX_2(CRASH_DEV_CONTROLVM_NULL,
				 POSTCODE_SEVERITY_ERR);
		return;
	}

	/* get saved message count */
	if (visorchannel_read(ControlVm_channel,
			      offsetof(ULTRA_CONTROLVM_CHANNEL_PROTOCOL,
				       SavedCrashMsgCount),
			      &localSavedCrashMsgCount, sizeof(U16)) < 0) {
		LOGERR("failed to get Saved Message Count");
		POSTCODE_LINUX_2(CRASH_DEV_CTRL_RD_FAILURE_PC,
				 POSTCODE_SEVERITY_ERR);
		return;
	}

	if (localSavedCrashMsgCount != CONTROLVM_CRASHMSG_MAX) {
		LOGERR("Saved Message Count incorrect %d",
		       localSavedCrashMsgCount);
		POSTCODE_LINUX_3(CRASH_DEV_COUNT_FAILURE_PC,
				 localSavedCrashMsgCount,
				 POSTCODE_SEVERITY_ERR);
		return;
	}

	/* get saved crash message offset */
	if (visorchannel_read(ControlVm_channel,
			      offsetof(ULTRA_CONTROLVM_CHANNEL_PROTOCOL,
				       SavedCrashMsgOffset),
			      &localSavedCrashMsgOffset, sizeof(U32)) < 0) {
		LOGERR("failed to get Saved Message Offset");
		POSTCODE_LINUX_2(CRASH_DEV_CTRL_RD_FAILURE_PC,
				 POSTCODE_SEVERITY_ERR);
		return;
	}

	/* read create device message for storage bus offset */
	if (visorchannel_read(ControlVm_channel,
			      localSavedCrashMsgOffset,
			      &localCrashCreateBusMsg,
			      sizeof(CONTROLVM_MESSAGE)) < 0) {
		LOGERR("CRASH_DEV_RD_BUS_FAIULRE: Failed to read CrashCreateBusMsg!");
		POSTCODE_LINUX_2(CRASH_DEV_RD_BUS_FAIULRE_PC,
				 POSTCODE_SEVERITY_ERR);
		return;
	}

	/* read create device message for storage device */
	if (visorchannel_read(ControlVm_channel,
			      localSavedCrashMsgOffset +
			      sizeof(CONTROLVM_MESSAGE),
			      &localCrashCreateDevMsg,
			      sizeof(CONTROLVM_MESSAGE)) < 0) {
		LOGERR("CRASH_DEV_RD_DEV_FAIULRE: Failed to read CrashCreateDevMsg!");
		POSTCODE_LINUX_2(CRASH_DEV_RD_DEV_FAIULRE_PC,
				 POSTCODE_SEVERITY_ERR);
		return;
	}

	/* reuse IOVM create bus message */
	if (localCrashCreateBusMsg.cmd.createBus.channelAddr != 0)
		bus_create(&localCrashCreateBusMsg);
	else {
		LOGERR("CrashCreateBusMsg is null, no dump will be taken");
		POSTCODE_LINUX_2(CRASH_DEV_BUS_NULL_FAILURE_PC,
				 POSTCODE_SEVERITY_ERR);
		return;
	}

	/* reuse create device message for storage device */
	if (localCrashCreateDevMsg.cmd.createDevice.channelAddr != 0)
		my_device_create(&localCrashCreateDevMsg);
	else {
		LOGERR("CrashCreateDevMsg is null, no dump will be taken");
		POSTCODE_LINUX_2(CRASH_DEV_DEV_NULL_FAILURE_PC,
				 POSTCODE_SEVERITY_ERR);
		return;
	}
	LOGINF("Bus and device ready for dumping");
	POSTCODE_LINUX_2(CRASH_DEV_EXIT_PC, POSTCODE_SEVERITY_INFO);
	return;

Away:

	Poll_jiffies = POLLJIFFIES_CONTROLVMCHANNEL_SLOW;

	queue_delayed_work(Periodic_controlvm_workqueue,
			   &Periodic_controlvm_work, Poll_jiffies);
}

static void
bus_create_response(ulong busNo, int response)
{
	bus_responder(CONTROLVM_BUS_CREATE, busNo, response);
}

static void
bus_destroy_response(ulong busNo, int response)
{
	bus_responder(CONTROLVM_BUS_DESTROY, busNo, response);
}

static void
device_create_response(ulong busNo, ulong devNo, int response)
{
	device_responder(CONTROLVM_DEVICE_CREATE, busNo, devNo, response);
}

static void
device_destroy_response(ulong busNo, ulong devNo, int response)
{
	device_responder(CONTROLVM_DEVICE_DESTROY, busNo, devNo, response);
}

void
visorchipset_device_pause_response(ulong busNo, ulong devNo, int response)
{

	device_changestate_responder(CONTROLVM_DEVICE_CHANGESTATE,
				     busNo, devNo, response,
				     SegmentStateStandby);
}
EXPORT_SYMBOL_GPL(visorchipset_device_pause_response);

static void
device_resume_response(ulong busNo, ulong devNo, int response)
{
	device_changestate_responder(CONTROLVM_DEVICE_CHANGESTATE,
				     busNo, devNo, response,
				     SegmentStateRunning);
}

BOOL
visorchipset_get_bus_info(ulong busNo, VISORCHIPSET_BUS_INFO *busInfo)
{
	void *p = findbus(&BusInfoList, busNo);
	if (!p) {
		LOGERR("(%lu) failed", busNo);
		return FALSE;
	}
	memcpy(busInfo, p, sizeof(VISORCHIPSET_BUS_INFO));
	return TRUE;
}
EXPORT_SYMBOL_GPL(visorchipset_get_bus_info);

BOOL
visorchipset_set_bus_context(ulong busNo, void *context)
{
	VISORCHIPSET_BUS_INFO *p = findbus(&BusInfoList, busNo);
	if (!p) {
		LOGERR("(%lu) failed", busNo);
		return FALSE;
	}
	p->bus_driver_context = context;
	return TRUE;
}
EXPORT_SYMBOL_GPL(visorchipset_set_bus_context);

BOOL
visorchipset_get_device_info(ulong busNo, ulong devNo,
			     VISORCHIPSET_DEVICE_INFO *devInfo)
{
	void *p = finddevice(&DevInfoList, busNo, devNo);
	if (!p) {
		LOGERR("(%lu,%lu) failed", busNo, devNo);
		return FALSE;
	}
	memcpy(devInfo, p, sizeof(VISORCHIPSET_DEVICE_INFO));
	return TRUE;
}
EXPORT_SYMBOL_GPL(visorchipset_get_device_info);

BOOL
visorchipset_set_device_context(ulong busNo, ulong devNo, void *context)
{
	VISORCHIPSET_DEVICE_INFO *p = finddevice(&DevInfoList, busNo, devNo);
	if (!p) {
		LOGERR("(%lu,%lu) failed", busNo, devNo);
		return FALSE;
	}
	p->bus_driver_context = context;
	return TRUE;
}
EXPORT_SYMBOL_GPL(visorchipset_set_device_context);

/* Generic wrapper function for allocating memory from a kmem_cache pool.
 */
void *
visorchipset_cache_alloc(struct kmem_cache *pool, BOOL ok_to_block,
			 char *fn, int ln)
{
	gfp_t gfp;
	void *p;

	if (ok_to_block)
		gfp = GFP_KERNEL;
	else
		gfp = GFP_ATOMIC;
	/* __GFP_NORETRY means "ok to fail", meaning
	 * kmem_cache_alloc() can return NULL, implying the caller CAN
	 * cope with failure.  If you do NOT specify __GFP_NORETRY,
	 * Linux will go to extreme measures to get memory for you
	 * (like, invoke oom killer), which will probably cripple the
	 * system.
	 */
	gfp |= __GFP_NORETRY;
	p = kmem_cache_alloc(pool, gfp);
	if (!p) {
		LOGERR("kmem_cache_alloc failed early @%s:%d\n", fn, ln);
		return NULL;
	}
	atomic_inc(&Visorchipset_cache_buffers_in_use);
	return p;
}

/* Generic wrapper function for freeing memory from a kmem_cache pool.
 */
void
visorchipset_cache_free(struct kmem_cache *pool, void *p, char *fn, int ln)
{
	if (!p) {
		LOGERR("NULL pointer @%s:%d\n", fn, ln);
		return;
	}
	atomic_dec(&Visorchipset_cache_buffers_in_use);
	kmem_cache_free(pool, p);
}

#define gettoken(bufp) strsep(bufp, " -\t\n")

static ssize_t
chipset_proc_write(struct file *file, const char __user *buffer,
		   size_t count, loff_t *ppos)
{
	char buf[512];
	char *token, *p;

	if (count > sizeof(buf) - 1) {
		LOGERR("chipset_proc_write: count (%d) exceeds size of buffer (%d)",
		     (int) count, (int) sizeof(buffer));
		return -EINVAL;
	}
	if (copy_from_user(buf, buffer, count)) {
		LOGERR("chipset_proc_write: copy_from_user failed");
		return -EFAULT;
	}
	buf[count] = '\0';

	p = buf;
	token = gettoken(&p);

	if (strcmp(token, "CALLHOMEDISK_MOUNTED") == 0) {
		token = gettoken(&p);
		/* The Call Home Disk has been mounted */
		if (strcmp(token, "0") == 0)
			chipset_events[0] = 1;
	} else if (strcmp(token, "MODULES_LOADED") == 0) {
		token = gettoken(&p);
		/* All modules for the partition have been loaded */
		if (strcmp(token, "0") == 0)
			chipset_events[1] = 1;
	} else if (token == NULL) {
		/* No event specified */
		LOGERR("No event was specified to send CHIPSET_READY response");
		return -1;
	} else {
		/* Unsupported event specified */
		LOGERR("%s is an invalid event for sending CHIPSET_READY response",		     token);
		return -1;
	}

	return count;
}

static ssize_t
visorchipset_proc_read_writeonly(struct file *file, char __user *buf,
				 size_t len, loff_t *offset)
{
	return 0;
}

/**
 * Reads the InstallationError, InstallationTextId,
 * InstallationRemainingSteps fields of ControlVMChannel.
 */
static ssize_t
proc_read_installer(struct file *file, char __user *buf,
		    size_t len, loff_t *offset)
{
	int length = 0;
	U16 remainingSteps;
	U32 error, textId;
	char *vbuf;
	loff_t pos = *offset;

	if (!ControlVm_channel)
		return -ENODEV;

	if (pos < 0)
		return -EINVAL;

	if (pos > 0 || !len)
		return 0;

	vbuf = kzalloc(len, GFP_KERNEL);
	if (!vbuf)
		return -ENOMEM;

	visorchannel_read(ControlVm_channel,
			  offsetof(ULTRA_CONTROLVM_CHANNEL_PROTOCOL,
				   InstallationRemainingSteps), &remainingSteps,
			  sizeof(U16));
	visorchannel_read(ControlVm_channel,
			  offsetof(ULTRA_CONTROLVM_CHANNEL_PROTOCOL,
				   InstallationError), &error, sizeof(U32));
	visorchannel_read(ControlVm_channel,
			  offsetof(ULTRA_CONTROLVM_CHANNEL_PROTOCOL,
				   InstallationTextId), &textId, sizeof(U32));

	length = sprintf(vbuf, "%u %u %u\n", remainingSteps, error, textId);
	if (copy_to_user(buf, vbuf, length)) {
		kfree(vbuf);
		return -EFAULT;
	}

	kfree(vbuf);
	*offset += length;
	return length;
}

/**
 * Writes to the InstallationError, InstallationTextId,
 * InstallationRemainingSteps fields of
 * ControlVMChannel.
 * Input: RemainingSteps Error TextId
 * Limit 32 characters input
 */
#define UINT16_MAX		(65535U)
#define UINT32_MAX		(4294967295U)
static ssize_t
proc_write_installer(struct file *file,
		     const char __user *buffer, size_t count, loff_t *ppos)
{
	char buf[32];
	U16 remainingSteps;
	U32 error, textId;

	if (!ControlVm_channel)
		return -ENODEV;

	/* Check to make sure there is no buffer overflow */
	if (count > (sizeof(buf) - 1))
		return -EINVAL;

	if (copy_from_user(buf, buffer, count)) {
		WARN(1, "Error copying from user space\n");
		return -EFAULT;
	}

	if (sscanf(buf, "%hu %i %i", &remainingSteps, &error, &textId) != 3) {
		remainingSteps = UINT16_MAX;
		error = UINT32_MAX;
		textId = UINT32_MAX;
	}

	if (remainingSteps != UINT16_MAX) {
		if (visorchannel_write
		    (ControlVm_channel,
		     offsetof(ULTRA_CONTROLVM_CHANNEL_PROTOCOL,
			      InstallationRemainingSteps), &remainingSteps,
		     sizeof(U16)) < 0)
			WARN(1, "Installation Status Write Failed - Write function error - RemainingSteps = %d\n",
			     remainingSteps);
	}

	if (error != UINT32_MAX) {
		if (visorchannel_write
		    (ControlVm_channel,
		     offsetof(ULTRA_CONTROLVM_CHANNEL_PROTOCOL,
			      InstallationError), &error, sizeof(U32)) < 0)
			WARN(1, "Installation Status Write Failed - Write function error - Error = %d\n",
			     error);
	}

	if (textId != UINT32_MAX) {
		if (visorchannel_write
		    (ControlVm_channel,
		     offsetof(ULTRA_CONTROLVM_CHANNEL_PROTOCOL,
			      InstallationTextId), &textId, sizeof(U32)) < 0)
			WARN(1, "Installation Status Write Failed - Write function error - TextId = %d\n",
			     textId);
	}

	/* So this function isn't called multiple times, must return
	 * size of buffer
	 */
	return count;
}

/**
 * Reads the ToolAction field of ControlVMChannel.
 */
static ssize_t
proc_read_toolaction(struct file *file, char __user *buf,
		     size_t len, loff_t *offset)
{
	int length = 0;
	U8 toolAction;
	char *vbuf;
	loff_t pos = *offset;

	if (!ControlVm_channel)
		return -ENODEV;

	if (pos < 0)
		return -EINVAL;

	if (pos > 0 || !len)
		return 0;

	vbuf = kzalloc(len, GFP_KERNEL);
	if (!vbuf)
		return -ENOMEM;

	visorchannel_read(ControlVm_channel,
			  offsetof(ULTRA_CONTROLVM_CHANNEL_PROTOCOL,
				   ToolAction), &toolAction, sizeof(U8));

	length = sprintf(vbuf, "%u\n", toolAction);
	if (copy_to_user(buf, vbuf, length)) {
		kfree(vbuf);
		return -EFAULT;
	}

	kfree(vbuf);
	*offset += length;
	return length;
}

/**
 * Writes to the ToolAction field of ControlVMChannel.
 * Input: ToolAction
 * Limit 3 characters input
 */
#define UINT8_MAX (255U)
static ssize_t
proc_write_toolaction(struct file *file,
		      const char __user *buffer, size_t count, loff_t *ppos)
{
	char buf[3];
	U8 toolAction;

	if (!ControlVm_channel)
		return -ENODEV;

	/* Check to make sure there is no buffer overflow */
	if (count > (sizeof(buf) - 1))
		return -EINVAL;

	if (copy_from_user(buf, buffer, count)) {
		WARN(1, "Error copying from user space\n");
		return -EFAULT;
	}

	if (sscanf(buf, "%hhd", &toolAction) != 1)
		toolAction = UINT8_MAX;

	if (toolAction != UINT8_MAX) {
		if (visorchannel_write
		    (ControlVm_channel,
		     offsetof(ULTRA_CONTROLVM_CHANNEL_PROTOCOL, ToolAction),
		     &toolAction, sizeof(U8)) < 0)
			WARN(1, "Installation ToolAction Write Failed - ToolAction = %d\n",
			     toolAction);
	}

	/* So this function isn't called multiple times, must return
	 * size of buffer
	 */
	return count;
}

/**
 * Reads the EfiSparIndication.BootToTool field of ControlVMChannel.
 */
static ssize_t
proc_read_bootToTool(struct file *file, char __user *buf,
		     size_t len, loff_t *offset)
{
	int length = 0;
	ULTRA_EFI_SPAR_INDICATION efiSparIndication;
	char *vbuf;
	loff_t pos = *offset;

	if (!ControlVm_channel)
		return -ENODEV;

	if (pos < 0)
		return -EINVAL;

	if (pos > 0 || !len)
		return 0;

	vbuf = kzalloc(len, GFP_KERNEL);
	if (!vbuf)
		return -ENOMEM;

	visorchannel_read(ControlVm_channel,
			  offsetof(ULTRA_CONTROLVM_CHANNEL_PROTOCOL,
				   EfiSparIndication), &efiSparIndication,
			  sizeof(ULTRA_EFI_SPAR_INDICATION));

	length = sprintf(vbuf, "%d\n", (int) efiSparIndication.BootToTool);
	if (copy_to_user(buf, vbuf, length)) {
		kfree(vbuf);
		return -EFAULT;
	}

	kfree(vbuf);
	*offset += length;
	return length;
}

/**
 * Writes to the EfiSparIndication.BootToTool field of ControlVMChannel.
 * Input: 1 or 0 (1 being on, 0 being off)
 */
static ssize_t
proc_write_bootToTool(struct file *file,
		      const char __user *buffer, size_t count, loff_t *ppos)
{
	char buf[3];
	int inputVal;
	ULTRA_EFI_SPAR_INDICATION efiSparIndication;

	if (!ControlVm_channel)
		return -ENODEV;

	/* Check to make sure there is no buffer overflow */
	if (count > (sizeof(buf) - 1))
		return -EINVAL;

	if (copy_from_user(buf, buffer, count)) {
		WARN(1, "Error copying from user space\n");
		return -EFAULT;
	}

	if (sscanf(buf, "%i", &inputVal) != 1)
		inputVal = 0;

	efiSparIndication.BootToTool = (inputVal == 1 ? 1 : 0);

	if (visorchannel_write
	    (ControlVm_channel,
	     offsetof(ULTRA_CONTROLVM_CHANNEL_PROTOCOL, EfiSparIndication),
	     &efiSparIndication, sizeof(ULTRA_EFI_SPAR_INDICATION)) < 0)
		printk
		    ("Installation BootToTool Write Failed - BootToTool = %d\n",
		     (int) efiSparIndication.BootToTool);

	/* So this function isn't called multiple times, must return
	 * size of buffer
	 */
	return count;
}

static const struct file_operations chipset_proc_fops = {
	.owner = THIS_MODULE,
	.read = visorchipset_proc_read_writeonly,
	.write = chipset_proc_write,
};

static int __init
visorchipset_init(void)
{
	int rc = 0, x = 0;
	struct proc_dir_entry *installer_file;
	struct proc_dir_entry *toolaction_file;
	struct proc_dir_entry *bootToTool_file;

	if (!unisys_spar_platform)
		return -ENODEV;

	LOGINF("chipset driver version %s loaded", VERSION);
	/* process module options */
	POSTCODE_LINUX_2(DRIVER_ENTRY_PC, POSTCODE_SEVERITY_INFO);

	LOGINF("option - testvnic=%d", visorchipset_testvnic);
	LOGINF("option - testvnicclient=%d", visorchipset_testvnicclient);
	LOGINF("option - testmsg=%d", visorchipset_testmsg);
	LOGINF("option - testteardown=%d", visorchipset_testteardown);
	LOGINF("option - major=%d", visorchipset_major);
	LOGINF("option - serverregwait=%d", visorchipset_serverregwait);
	LOGINF("option - clientregwait=%d", visorchipset_clientregwait);
	LOGINF("option - holdchipsetready=%d", visorchipset_holdchipsetready);

	memset(&BusDev_Server_Notifiers, 0, sizeof(BusDev_Server_Notifiers));
	memset(&BusDev_Client_Notifiers, 0, sizeof(BusDev_Client_Notifiers));
	memset(&ControlVm_payload_info, 0, sizeof(ControlVm_payload_info));
	memset(&LiveDump_info, 0, sizeof(LiveDump_info));
	atomic_set(&LiveDump_info.buffers_in_use, 0);

	if (visorchipset_testvnic) {
		ERRDRV("testvnic option no longer supported: (status = %d)\n",
		       x);
		POSTCODE_LINUX_3(CHIPSET_INIT_FAILURE_PC, x, DIAG_SEVERITY_ERR);
		rc = x;
		goto Away;
	}

	controlvm_init();
	MajorDev = MKDEV(visorchipset_major, 0);
	rc = visorchipset_file_init(MajorDev, &ControlVm_channel);
	if (rc < 0) {
		ERRDRV("visorchipset_file_init(MajorDev, &ControlVm_channel): error (status=%d)\n", rc);
		POSTCODE_LINUX_2(CHIPSET_INIT_FAILURE_PC, DIAG_SEVERITY_ERR);
		goto Away;
	}

	proc_Init();
	memset(PartitionPropertyNames, 0, sizeof(PartitionPropertyNames));
	memset(ControlVmPropertyNames, 0, sizeof(ControlVmPropertyNames));
	InitPartitionProperties();
	InitControlVmProperties();

	PartitionType = visor_proc_CreateType(ProcDir, PartitionTypeNames,
					      (const char **)
					      PartitionPropertyNames,
					      &show_partition_property);
	ControlVmType =
	    visor_proc_CreateType(ProcDir, ControlVmTypeNames,
				  (const char **) ControlVmPropertyNames,
				  &show_controlvm_property);

	ControlVmObject = visor_proc_CreateObject(ControlVmType, NULL, NULL);

	/* Setup Installation fields */
	installer_file = proc_create("installer", 0644, ProcDir,
				     &proc_installer_fops);
	/* Setup the ToolAction field */
	toolaction_file = proc_create("toolaction", 0644, ProcDir,
				      &proc_toolaction_fops);
	/* Setup the BootToTool field */
	bootToTool_file = proc_create("boottotool", 0644, ProcDir,
				      &proc_bootToTool_fops);

	memset(&g_DiagMsgHdr, 0, sizeof(CONTROLVM_MESSAGE_HEADER));

	chipset_proc_dir = proc_create(VISORCHIPSET_CHIPSET_PROC_ENTRY_FN,
				       0644, ProcDir, &chipset_proc_fops);
	memset(&g_ChipSetMsgHdr, 0, sizeof(CONTROLVM_MESSAGE_HEADER));

	parahotplug_proc_dir =
	    proc_create(VISORCHIPSET_PARAHOTPLUG_PROC_ENTRY_FN, 0200,
			ProcDir, &parahotplug_proc_fops);
	memset(&g_DelDumpMsgHdr, 0, sizeof(CONTROLVM_MESSAGE_HEADER));

	Putfile_buffer_list_pool =
	    kmem_cache_create(Putfile_buffer_list_pool_name,
			      sizeof(struct putfile_buffer_entry),
			      0, SLAB_HWCACHE_ALIGN, NULL);
	if (!Putfile_buffer_list_pool) {
		ERRDRV("failed to alloc Putfile_buffer_list_pool: (status=-1)\n");
		POSTCODE_LINUX_2(CHIPSET_INIT_FAILURE_PC, DIAG_SEVERITY_ERR);
		rc = -1;
		goto Away;
	}
	if (visorchipset_disable_controlvm) {
		LOGINF("visorchipset_init:controlvm disabled");
	} else {
		/* if booting in a crash kernel */
		if (visorchipset_crash_kernel)
			INIT_DELAYED_WORK(&Periodic_controlvm_work,
					  setup_crash_devices_work_queue);
		else
			INIT_DELAYED_WORK(&Periodic_controlvm_work,
					  controlvm_periodic_work);
		Periodic_controlvm_workqueue =
		    create_singlethread_workqueue("visorchipset_controlvm");

		if (Periodic_controlvm_workqueue == NULL) {
			ERRDRV("cannot create controlvm workqueue: (status=%d)\n",
			       -ENOMEM);
			POSTCODE_LINUX_2(CREATE_WORKQUEUE_FAILED_PC,
					 DIAG_SEVERITY_ERR);
			rc = -ENOMEM;
			goto Away;
		}
		Most_recent_message_jiffies = jiffies;
		Poll_jiffies = POLLJIFFIES_CONTROLVMCHANNEL_FAST;
		rc = queue_delayed_work(Periodic_controlvm_workqueue,
					&Periodic_controlvm_work, Poll_jiffies);
		if (rc < 0) {
			ERRDRV("queue_delayed_work(Periodic_controlvm_workqueue, &Periodic_controlvm_work, Poll_jiffies): error (status=%d)\n", rc);
			POSTCODE_LINUX_2(QUEUE_DELAYED_WORK_PC,
					 DIAG_SEVERITY_ERR);
			goto Away;
		}

	}

	Visorchipset_platform_device.dev.devt = MajorDev;
	if (platform_device_register(&Visorchipset_platform_device) < 0) {
		ERRDRV("platform_device_register(visorchipset) failed: (status=-1)\n");
		POSTCODE_LINUX_2(DEVICE_REGISTER_FAILURE_PC, DIAG_SEVERITY_ERR);
		rc = -1;
		goto Away;
	}
	LOGINF("visorchipset device created");
	POSTCODE_LINUX_2(CHIPSET_INIT_SUCCESS_PC, POSTCODE_SEVERITY_INFO);
	rc = 0;
Away:
	if (rc) {
		LOGERR("visorchipset_init failed");
		POSTCODE_LINUX_3(CHIPSET_INIT_FAILURE_PC, rc,
				 POSTCODE_SEVERITY_ERR);
	}
	return rc;
}

static void
visorchipset_exit(void)
{
	char s[99];
	POSTCODE_LINUX_2(DRIVER_EXIT_PC, POSTCODE_SEVERITY_INFO);

	if (visorchipset_disable_controlvm) {
		;
	} else {
		cancel_delayed_work(&Periodic_controlvm_work);
		flush_workqueue(Periodic_controlvm_workqueue);
		destroy_workqueue(Periodic_controlvm_workqueue);
		Periodic_controlvm_workqueue = NULL;
		destroy_controlvm_payload_info(&ControlVm_payload_info);
	}
	Test_Vnic_channel = NULL;
	if (Putfile_buffer_list_pool) {
		kmem_cache_destroy(Putfile_buffer_list_pool);
		Putfile_buffer_list_pool = NULL;
	}
	if (ControlVmObject) {
		visor_proc_DestroyObject(ControlVmObject);
		ControlVmObject = NULL;
	}
	cleanup_controlvm_structures();

	if (ControlVmType) {
		visor_proc_DestroyType(ControlVmType);
		ControlVmType = NULL;
	}
	if (PartitionType) {
		visor_proc_DestroyType(PartitionType);
		PartitionType = NULL;
	}
	if (diag_proc_dir) {
		remove_proc_entry(VISORCHIPSET_DIAG_PROC_ENTRY_FN, ProcDir);
		diag_proc_dir = NULL;
	}
	memset(&g_DiagMsgHdr, 0, sizeof(CONTROLVM_MESSAGE_HEADER));

	if (chipset_proc_dir) {
		remove_proc_entry(VISORCHIPSET_CHIPSET_PROC_ENTRY_FN, ProcDir);
		chipset_proc_dir = NULL;
	}
	memset(&g_ChipSetMsgHdr, 0, sizeof(CONTROLVM_MESSAGE_HEADER));

	if (parahotplug_proc_dir) {
		remove_proc_entry(VISORCHIPSET_PARAHOTPLUG_PROC_ENTRY_FN,
				  ProcDir);
		parahotplug_proc_dir = NULL;
	}

	memset(&g_DelDumpMsgHdr, 0, sizeof(CONTROLVM_MESSAGE_HEADER));

	proc_DeInit();
	if (ControlVm_channel != NULL) {
		LOGINF("Channel %s (ControlVm) disconnected",
		       visorchannel_id(ControlVm_channel, s));
		visorchannel_destroy(ControlVm_channel);
		ControlVm_channel = NULL;
	}
	controlvm_deinit();
	visorchipset_file_cleanup();
	POSTCODE_LINUX_2(DRIVER_EXIT_PC, POSTCODE_SEVERITY_INFO);
	LOGINF("chipset driver unloaded");
}

module_param_named(testvnic, visorchipset_testvnic, int, S_IRUGO);
MODULE_PARM_DESC(visorchipset_testvnic, "1 to test vnic, using dummy VNIC connected via a loopback to a physical ethernet");
int visorchipset_testvnic = 0;

module_param_named(testvnicclient, visorchipset_testvnicclient, int, S_IRUGO);
MODULE_PARM_DESC(visorchipset_testvnicclient, "1 to test vnic, using real VNIC channel attached to a separate IOVM guest");
int visorchipset_testvnicclient = 0;

module_param_named(testmsg, visorchipset_testmsg, int, S_IRUGO);
MODULE_PARM_DESC(visorchipset_testmsg,
		 "1 to manufacture the chipset, bus, and switch messages");
int visorchipset_testmsg = 0;

module_param_named(major, visorchipset_major, int, S_IRUGO);
MODULE_PARM_DESC(visorchipset_major, "major device number to use for the device node");
int visorchipset_major = 0;

module_param_named(serverregwait, visorchipset_serverregwait, int, S_IRUGO);
MODULE_PARM_DESC(visorchipset_serverreqwait,
		 "1 to have the module wait for the visor bus to register");
int visorchipset_serverregwait = 0;	/* default is off */
module_param_named(clientregwait, visorchipset_clientregwait, int, S_IRUGO);
MODULE_PARM_DESC(visorchipset_clientregwait, "1 to have the module wait for the visorclientbus to register");
int visorchipset_clientregwait = 1;	/* default is on */
module_param_named(testteardown, visorchipset_testteardown, int, S_IRUGO);
MODULE_PARM_DESC(visorchipset_testteardown,
		 "1 to test teardown of the chipset, bus, and switch");
int visorchipset_testteardown = 0;	/* default is off */
module_param_named(disable_controlvm, visorchipset_disable_controlvm, int,
		   S_IRUGO);
MODULE_PARM_DESC(visorchipset_disable_controlvm,
		 "1 to disable polling of controlVm channel");
int visorchipset_disable_controlvm = 0;	/* default is off */
module_param_named(crash_kernel, visorchipset_crash_kernel, int, S_IRUGO);
MODULE_PARM_DESC(visorchipset_crash_kernel,
		 "1 means we are running in crash kernel");
int visorchipset_crash_kernel = 0; /* default is running in non-crash kernel */
module_param_named(holdchipsetready, visorchipset_holdchipsetready,
		   int, S_IRUGO);
MODULE_PARM_DESC(visorchipset_holdchipsetready,
		 "1 to hold response to CHIPSET_READY");
int visorchipset_holdchipsetready = 0; /* default is to send CHIPSET_READY
				      * response immediately */
module_init(visorchipset_init);
module_exit(visorchipset_exit);

MODULE_AUTHOR("Unisys");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Supervisor chipset driver for service partition: ver "
		   VERSION);
MODULE_VERSION(VERSION);
