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
#include "visorchipset.h"
#include "procobjecttree.h"
#include "visorchannel.h"
#include "periodic_work.h"
#include "file.h"
#include "parser.h"
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
static ulong poll_jiffies = POLLJIFFIES_CONTROLVMCHANNEL_FAST;
static ulong most_recent_message_jiffies;	/* when we got our last
						 * controlvm message */
static inline char *
NONULLSTR(char *s)
{
	if (s)
		return s;
	return "";
}

static int serverregistered;
static int clientregistered;

#define MAX_CHIPSET_EVENTS 2
static u8 chipset_events[MAX_CHIPSET_EVENTS] = { 0, 0 };

static struct delayed_work periodic_controlvm_work;
static struct workqueue_struct *periodic_controlvm_workqueue;
static DEFINE_SEMAPHORE(NotifierLock);

static struct controlvm_message_header g_diag_msg_hdr;
static struct controlvm_message_header g_chipset_msg_hdr;
static struct controlvm_message_header g_del_dump_msg_hdr;
static const uuid_le UltraDiagPoolChannelProtocolGuid =
	SPAR_DIAG_POOL_CHANNEL_PROTOCOL_UUID;
/* 0xffffff is an invalid Bus/Device number */
static ulong g_diagpoolBusNo = 0xffffff;
static ulong g_diagpoolDevNo = 0xffffff;
static struct controlvm_message_packet g_DeviceChangeStatePacket;

/* Only VNIC and VHBA channels are sent to visorclientbus (aka
 * "visorhackbus")
 */
#define FOR_VISORHACKBUS(channel_type_guid) \
	(((uuid_le_cmp(channel_type_guid,\
		       spar_vnic_channel_protocol_uuid) == 0)\
	|| (uuid_le_cmp(channel_type_guid,\
			spar_vhba_channel_protocol_uuid) == 0)))
#define FOR_VISORBUS(channel_type_guid) (!(FOR_VISORHACKBUS(channel_type_guid)))

#define is_diagpool_channel(channel_type_guid) \
	 (uuid_le_cmp(channel_type_guid, UltraDiagPoolChannelProtocolGuid) == 0)

static LIST_HEAD(BusInfoList);
static LIST_HEAD(DevInfoList);

static struct visorchannel *ControlVm_channel;

struct controlvm_payload_info {
	u8 __iomem *ptr;	/* pointer to base address of payload pool */
	u64 offset;		/* offset from beginning of controlvm
				 * channel to beginning of payload * pool */
	u32 bytes;		/* number of bytes in payload pool */
};

/* Manages the request payload in the controlvm channel */
static struct controlvm_payload_info ControlVm_payload_info;

static struct channel_header *Test_Vnic_channel;

struct livedump_info {
	struct controlvm_message_header Dumpcapture_header;
	struct controlvm_message_header Gettextdump_header;
	struct controlvm_message_header Dumpcomplete_header;
	BOOL Gettextdump_outstanding;
	u32 crc32;
	ulong length;
	atomic_t buffers_in_use;
	ulong destination;
};
/* Manages the info for a CONTROLVM_DUMP_CAPTURESTATE /
 * CONTROLVM_DUMP_GETTEXTDUMP / CONTROLVM_DUMP_COMPLETE conversation.
 */
static struct livedump_info LiveDump_info;

/* The following globals are used to handle the scenario where we are unable to
 * offload the payload from a controlvm message due to memory requirements.  In
 * this scenario, we simply stash the controlvm message, then attempt to
 * process it again the next time controlvm_periodic_work() runs.
 */
static struct controlvm_message ControlVm_Pending_Msg;
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
	struct parser_context *parser_ctx; /* points to input data buffer */
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
	struct parser_context *parser_ctx;
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
	struct controlvm_message_header controlvm_header;
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
	struct controlvm_message msg;
};

static LIST_HEAD(Parahotplug_request_list);
static DEFINE_SPINLOCK(Parahotplug_request_list_lock);	/* lock for above */
static void parahotplug_process_list(void);

/* Manages the info for a CONTROLVM_DUMP_CAPTURESTATE /
 * CONTROLVM_REPORTEVENT.
 */
static struct visorchipset_busdev_notifiers BusDev_Server_Notifiers;
static struct visorchipset_busdev_notifiers BusDev_Client_Notifiers;

static void bus_create_response(ulong busNo, int response);
static void bus_destroy_response(ulong busNo, int response);
static void device_create_response(ulong busNo, ulong devNo, int response);
static void device_destroy_response(ulong busNo, ulong devNo, int response);
static void device_resume_response(ulong busNo, ulong devNo, int response);

static struct visorchipset_busdev_responders BusDev_Responders = {
	.bus_create = bus_create_response,
	.bus_destroy = bus_destroy_response,
	.device_create = device_create_response,
	.device_destroy = device_destroy_response,
	.device_pause = visorchipset_device_pause_response,
	.device_resume = device_resume_response,
};

/* info for /dev/visorchipset */
static dev_t MajorDev = -1; /**< indicates major num for device */

/* prototypes for attributes */
static ssize_t toolaction_show(struct device *dev,
	struct device_attribute *attr, char *buf);
static ssize_t toolaction_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count);
static DEVICE_ATTR_RW(toolaction);

static ssize_t boottotool_show(struct device *dev,
	struct device_attribute *attr, char *buf);
static ssize_t boottotool_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count);
static DEVICE_ATTR_RW(boottotool);

static ssize_t error_show(struct device *dev, struct device_attribute *attr,
	char *buf);
static ssize_t error_store(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count);
static DEVICE_ATTR_RW(error);

static ssize_t textid_show(struct device *dev, struct device_attribute *attr,
	char *buf);
static ssize_t textid_store(struct device *dev, struct device_attribute *attr,
	const char *buf, size_t count);
static DEVICE_ATTR_RW(textid);

static ssize_t remaining_steps_show(struct device *dev,
	struct device_attribute *attr, char *buf);
static ssize_t remaining_steps_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count);
static DEVICE_ATTR_RW(remaining_steps);

static ssize_t chipsetready_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);
static DEVICE_ATTR_WO(chipsetready);

static ssize_t devicedisabled_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);
static DEVICE_ATTR_WO(devicedisabled);

static ssize_t deviceenabled_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count);
static DEVICE_ATTR_WO(deviceenabled);

static struct attribute *visorchipset_install_attrs[] = {
	&dev_attr_toolaction.attr,
	&dev_attr_boottotool.attr,
	&dev_attr_error.attr,
	&dev_attr_textid.attr,
	&dev_attr_remaining_steps.attr,
	NULL
};

static struct attribute_group visorchipset_install_group = {
	.name = "install",
	.attrs = visorchipset_install_attrs
};

static struct attribute *visorchipset_guest_attrs[] = {
	&dev_attr_chipsetready.attr,
	NULL
};

static struct attribute_group visorchipset_guest_group = {
	.name = "guest",
	.attrs = visorchipset_guest_attrs
};

static struct attribute *visorchipset_parahotplug_attrs[] = {
	&dev_attr_devicedisabled.attr,
	&dev_attr_deviceenabled.attr,
	NULL
};

static struct attribute_group visorchipset_parahotplug_group = {
	.name = "parahotplug",
	.attrs = visorchipset_parahotplug_attrs
};

static const struct attribute_group *visorchipset_dev_groups[] = {
	&visorchipset_install_group,
	&visorchipset_guest_group,
	&visorchipset_parahotplug_group,
	NULL
};

/* /sys/devices/platform/visorchipset */
static struct platform_device Visorchipset_platform_device = {
	.name = "visorchipset",
	.id = -1,
	.dev.groups = visorchipset_dev_groups,
};

/* Function prototypes */
static void controlvm_respond(struct controlvm_message_header *msgHdr,
			      int response);
static void controlvm_respond_chipset_init(
		struct controlvm_message_header *msgHdr, int response,
		enum ultra_chipset_feature features);
static void controlvm_respond_physdev_changestate(
		struct controlvm_message_header *msgHdr, int response,
		struct spar_segment_state state);

static ssize_t toolaction_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	u8 toolAction;

	visorchannel_read(ControlVm_channel,
		offsetof(struct spar_controlvm_channel_protocol,
			   tool_action), &toolAction, sizeof(u8));
	return scnprintf(buf, PAGE_SIZE, "%u\n", toolAction);
}

static ssize_t toolaction_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	u8 toolAction;
	int ret;

	if (kstrtou8(buf, 10, &toolAction) != 0)
		return -EINVAL;

	ret = visorchannel_write(ControlVm_channel,
		offsetof(struct spar_controlvm_channel_protocol, tool_action),
		&toolAction, sizeof(u8));

	if (ret)
		return ret;
	return count;
}

static ssize_t boottotool_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct efi_spar_indication efiSparIndication;

	visorchannel_read(ControlVm_channel,
		offsetof(struct spar_controlvm_channel_protocol,
			efi_spar_ind), &efiSparIndication,
		sizeof(struct efi_spar_indication));
	return scnprintf(buf, PAGE_SIZE, "%u\n",
			efiSparIndication.boot_to_tool);
}

static ssize_t boottotool_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	int val, ret;
	struct efi_spar_indication efiSparIndication;

	if (kstrtoint(buf, 10, &val) != 0)
		return -EINVAL;

	efiSparIndication.boot_to_tool = val;
	ret = visorchannel_write(ControlVm_channel,
			offsetof(struct spar_controlvm_channel_protocol,
				efi_spar_ind),
			&(efiSparIndication),
		sizeof(struct efi_spar_indication));

	if (ret)
		return ret;
	return count;
}

static ssize_t error_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	u32 error;

	visorchannel_read(ControlVm_channel, offsetof(
		struct spar_controlvm_channel_protocol, installation_error),
		&error, sizeof(u32));
	return scnprintf(buf, PAGE_SIZE, "%i\n", error);
}

static ssize_t error_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	u32 error;
	int ret;

	if (kstrtou32(buf, 10, &error) != 0)
		return -EINVAL;

	ret = visorchannel_write(ControlVm_channel,
			offsetof(struct spar_controlvm_channel_protocol,
				installation_error),
			&error, sizeof(u32));
	if (ret)
		return ret;
	return count;
}

static ssize_t textid_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	u32 textId;

	visorchannel_read(ControlVm_channel, offsetof(
		struct spar_controlvm_channel_protocol, installation_text_id),
		&textId, sizeof(u32));
	return scnprintf(buf, PAGE_SIZE, "%i\n", textId);
}

static ssize_t textid_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	u32 textId;
	int ret;

	if (kstrtou32(buf, 10, &textId) != 0)
		return -EINVAL;

	ret = visorchannel_write(ControlVm_channel,
			offsetof(struct spar_controlvm_channel_protocol,
				installation_text_id),
			&textId, sizeof(u32));
	if (ret)
		return ret;
	return count;
}


static ssize_t remaining_steps_show(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	u16 remainingSteps;

	visorchannel_read(ControlVm_channel,
		offsetof(struct spar_controlvm_channel_protocol,
			installation_remaining_steps),
		&remainingSteps,
		sizeof(u16));
	return scnprintf(buf, PAGE_SIZE, "%hu\n", remainingSteps);
}

static ssize_t remaining_steps_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	u16 remainingSteps;
	int ret;

	if (kstrtou16(buf, 10, &remainingSteps) != 0)
		return -EINVAL;

	ret = visorchannel_write(ControlVm_channel,
			offsetof(struct spar_controlvm_channel_protocol,
				installation_remaining_steps),
			&remainingSteps, sizeof(u16));
	if (ret)
		return ret;
	return count;
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
	struct visorchipset_bus_info *p = (struct visorchipset_bus_info *) (v);

	if (p->proc_object) {
		visor_proc_DestroyObject(p->proc_object);
		p->proc_object = NULL;
	}
	kfree(p->name);
	p->name = NULL;

	kfree(p->description);
	p->description = NULL;

	p->state.created = 0;
	memset(p, 0, sizeof(struct visorchipset_bus_info));
}

static void
devInfo_clear(void *v)
{
	struct visorchipset_device_info *p =
			(struct visorchipset_device_info *)(v);

	p->state.created = 0;
	memset(p, 0, sizeof(struct visorchipset_device_info));
}

static u8
check_chipset_events(void)
{
	int i;
	u8 send_msg = 1;
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
visorchipset_register_busdev_server(
			struct visorchipset_busdev_notifiers *notifiers,
			struct visorchipset_busdev_responders *responders,
			struct ultra_vbus_deviceinfo *driver_info)
{
	down(&NotifierLock);
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
	if (driver_info)
		bus_device_info_init(driver_info, "chipset", "visorchipset",
				   VERSION, NULL);

	up(&NotifierLock);
}
EXPORT_SYMBOL_GPL(visorchipset_register_busdev_server);

void
visorchipset_register_busdev_client(
			struct visorchipset_busdev_notifiers *notifiers,
			struct visorchipset_busdev_responders *responders,
			struct ultra_vbus_deviceinfo *driver_info)
{
	down(&NotifierLock);
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
	if (driver_info)
		bus_device_info_init(driver_info, "chipset(bolts)",
				     "visorchipset", VERSION, NULL);
	up(&NotifierLock);
}
EXPORT_SYMBOL_GPL(visorchipset_register_busdev_client);

static void
cleanup_controlvm_structures(void)
{
	struct visorchipset_bus_info *bi, *tmp_bi;
	struct visorchipset_device_info *di, *tmp_di;

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
chipset_init(struct controlvm_message *inmsg)
{
	static int chipset_inited;
	enum ultra_chipset_feature features = 0;
	int rc = CONTROLVM_RESP_SUCCESS;

	POSTCODE_LINUX_2(CHIPSET_INIT_ENTRY_PC, POSTCODE_SEVERITY_INFO);
	if (chipset_inited) {
		rc = -CONTROLVM_RESP_ERROR_ALREADY_DONE;
		goto Away;
	}
	chipset_inited = 1;
	POSTCODE_LINUX_2(CHIPSET_INIT_EXIT_PC, POSTCODE_SEVERITY_INFO);

	/* Set features to indicate we support parahotplug (if Command
	 * also supports it). */
	features =
	    inmsg->cmd.init_chipset.
	    features & ULTRA_CHIPSET_FEATURE_PARA_HOTPLUG;

	/* Set the "reply" bit so Command knows this is a
	 * features-aware driver. */
	features |= ULTRA_CHIPSET_FEATURE_REPLY;

Away:
	if (rc < 0)
		cleanup_controlvm_structures();
	if (inmsg->hdr.flags.response_expected)
		controlvm_respond_chipset_init(&inmsg->hdr, rc, features);
}

static void
controlvm_init_response(struct controlvm_message *msg,
			struct controlvm_message_header *msgHdr, int response)
{
	memset(msg, 0, sizeof(struct controlvm_message));
	memcpy(&msg->hdr, msgHdr, sizeof(struct controlvm_message_header));
	msg->hdr.payload_bytes = 0;
	msg->hdr.payload_vm_offset = 0;
	msg->hdr.payload_max_bytes = 0;
	if (response < 0) {
		msg->hdr.flags.failed = 1;
		msg->hdr.completion_status = (u32) (-response);
	}
}

static void
controlvm_respond(struct controlvm_message_header *msgHdr, int response)
{
	struct controlvm_message outmsg;

	controlvm_init_response(&outmsg, msgHdr, response);
	/* For DiagPool channel DEVICE_CHANGESTATE, we need to send
	* back the deviceChangeState structure in the packet. */
	if (msgHdr->id == CONTROLVM_DEVICE_CHANGESTATE
	    && g_DeviceChangeStatePacket.device_change_state.bus_no ==
	    g_diagpoolBusNo
	    && g_DeviceChangeStatePacket.device_change_state.dev_no ==
	    g_diagpoolDevNo)
		outmsg.cmd = g_DeviceChangeStatePacket;
	if (outmsg.hdr.flags.test_message == 1)
		return;

	if (!visorchannel_signalinsert(ControlVm_channel,
				       CONTROLVM_QUEUE_REQUEST, &outmsg)) {
		return;
	}
}

static void
controlvm_respond_chipset_init(struct controlvm_message_header *msgHdr,
			       int response,
			       enum ultra_chipset_feature features)
{
	struct controlvm_message outmsg;

	controlvm_init_response(&outmsg, msgHdr, response);
	outmsg.cmd.init_chipset.features = features;
	if (!visorchannel_signalinsert(ControlVm_channel,
				       CONTROLVM_QUEUE_REQUEST, &outmsg)) {
		return;
	}
}

static void controlvm_respond_physdev_changestate(
		struct controlvm_message_header *msgHdr, int response,
		struct spar_segment_state state)
{
	struct controlvm_message outmsg;

	controlvm_init_response(&outmsg, msgHdr, response);
	outmsg.cmd.device_change_state.state = state;
	outmsg.cmd.device_change_state.flags.phys_device = 1;
	if (!visorchannel_signalinsert(ControlVm_channel,
				       CONTROLVM_QUEUE_REQUEST, &outmsg)) {
		return;
	}
}

void
visorchipset_save_message(struct controlvm_message *msg,
			  enum crash_obj_type type)
{
	u32 localSavedCrashMsgOffset;
	u16 localSavedCrashMsgCount;

	/* get saved message count */
	if (visorchannel_read(ControlVm_channel,
			      offsetof(struct spar_controlvm_channel_protocol,
				       saved_crash_message_count),
			      &localSavedCrashMsgCount, sizeof(u16)) < 0) {
		POSTCODE_LINUX_2(CRASH_DEV_CTRL_RD_FAILURE_PC,
				 POSTCODE_SEVERITY_ERR);
		return;
	}

	if (localSavedCrashMsgCount != CONTROLVM_CRASHMSG_MAX) {
		POSTCODE_LINUX_3(CRASH_DEV_COUNT_FAILURE_PC,
				 localSavedCrashMsgCount,
				 POSTCODE_SEVERITY_ERR);
		return;
	}

	/* get saved crash message offset */
	if (visorchannel_read(ControlVm_channel,
			      offsetof(struct spar_controlvm_channel_protocol,
				       saved_crash_message_offset),
			      &localSavedCrashMsgOffset, sizeof(u32)) < 0) {
		POSTCODE_LINUX_2(CRASH_DEV_CTRL_RD_FAILURE_PC,
				 POSTCODE_SEVERITY_ERR);
		return;
	}

	if (type == CRASH_BUS) {
		if (visorchannel_write(ControlVm_channel,
				       localSavedCrashMsgOffset,
				       msg,
				       sizeof(struct controlvm_message)) < 0) {
			POSTCODE_LINUX_2(SAVE_MSG_BUS_FAILURE_PC,
					 POSTCODE_SEVERITY_ERR);
			return;
		}
	} else {
		if (visorchannel_write(ControlVm_channel,
				       localSavedCrashMsgOffset +
				       sizeof(struct controlvm_message), msg,
				       sizeof(struct controlvm_message)) < 0) {
			POSTCODE_LINUX_2(SAVE_MSG_DEV_FAILURE_PC,
					 POSTCODE_SEVERITY_ERR);
			return;
		}
	}
}
EXPORT_SYMBOL_GPL(visorchipset_save_message);

static void
bus_responder(enum controlvm_id cmdId, ulong busNo, int response)
{
	struct visorchipset_bus_info *p = NULL;
	BOOL need_clear = FALSE;

	p = findbus(&BusInfoList, busNo);
	if (!p)
		return;

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

	if (p->pending_msg_hdr.id == CONTROLVM_INVALID)
		return;		/* no controlvm response needed */
	if (p->pending_msg_hdr.id != (u32) cmdId)
		return;
	controlvm_respond(&p->pending_msg_hdr, response);
	p->pending_msg_hdr.id = CONTROLVM_INVALID;
	if (need_clear) {
		busInfo_clear(p);
		delbusdevices(&DevInfoList, busNo);
	}
}

static void
device_changestate_responder(enum controlvm_id cmdId,
			     ulong busNo, ulong devNo, int response,
			     struct spar_segment_state responseState)
{
	struct visorchipset_device_info *p = NULL;
	struct controlvm_message outmsg;

	p = finddevice(&DevInfoList, busNo, devNo);
	if (!p)
		return;
	if (p->pending_msg_hdr.id == CONTROLVM_INVALID)
		return;		/* no controlvm response needed */
	if (p->pending_msg_hdr.id != cmdId)
		return;

	controlvm_init_response(&outmsg, &p->pending_msg_hdr, response);

	outmsg.cmd.device_change_state.bus_no = busNo;
	outmsg.cmd.device_change_state.dev_no = devNo;
	outmsg.cmd.device_change_state.state = responseState;

	if (!visorchannel_signalinsert(ControlVm_channel,
				       CONTROLVM_QUEUE_REQUEST, &outmsg))
		return;

	p->pending_msg_hdr.id = CONTROLVM_INVALID;
}

static void
device_responder(enum controlvm_id cmdId, ulong busNo, ulong devNo,
		 int response)
{
	struct visorchipset_device_info *p = NULL;
	BOOL need_clear = FALSE;

	p = finddevice(&DevInfoList, busNo, devNo);
	if (!p)
		return;
	if (response >= 0) {
		if (cmdId == CONTROLVM_DEVICE_CREATE)
			p->state.created = 1;
		if (cmdId == CONTROLVM_DEVICE_DESTROY)
			need_clear = TRUE;
	}

	if (p->pending_msg_hdr.id == CONTROLVM_INVALID)
		return;		/* no controlvm response needed */

	if (p->pending_msg_hdr.id != (u32) cmdId)
		return;

	controlvm_respond(&p->pending_msg_hdr, response);
	p->pending_msg_hdr.id = CONTROLVM_INVALID;
	if (need_clear)
		devInfo_clear(p);
}

static void
bus_epilog(u32 busNo,
	   u32 cmd, struct controlvm_message_header *msgHdr,
	   int response, BOOL needResponse)
{
	BOOL notified = FALSE;

	struct visorchipset_bus_info *pBusInfo = findbus(&BusInfoList, busNo);

	if (!pBusInfo)
		return;

	if (needResponse) {
		memcpy(&pBusInfo->pending_msg_hdr, msgHdr,
		       sizeof(struct controlvm_message_header));
	} else
		pBusInfo->pending_msg_hdr.id = CONTROLVM_INVALID;

	down(&NotifierLock);
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
		 * for calling the appropriate visorchipset_busdev_responders
		 * function, which will call bus_responder()
		 */
		;
	else
		bus_responder(cmd, busNo, response);
	up(&NotifierLock);
}

static void
device_epilog(u32 busNo, u32 devNo, struct spar_segment_state state, u32 cmd,
	      struct controlvm_message_header *msgHdr, int response,
	      BOOL needResponse, BOOL for_visorbus)
{
	struct visorchipset_busdev_notifiers *notifiers = NULL;
	BOOL notified = FALSE;

	struct visorchipset_device_info *pDevInfo =
		finddevice(&DevInfoList, busNo, devNo);
	char *envp[] = {
		"SPARSP_DIAGPOOL_PAUSED_STATE = 1",
		NULL
	};

	if (!pDevInfo)
		return;

	if (for_visorbus)
		notifiers = &BusDev_Server_Notifiers;
	else
		notifiers = &BusDev_Client_Notifiers;
	if (needResponse) {
		memcpy(&pDevInfo->pending_msg_hdr, msgHdr,
		       sizeof(struct controlvm_message_header));
	} else
		pDevInfo->pending_msg_hdr.id = CONTROLVM_INVALID;

	down(&NotifierLock);
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
			if (state.alive == segment_state_running.alive &&
			    state.operating ==
				segment_state_running.operating) {
				if (notifiers->device_resume) {
					(*notifiers->device_resume) (busNo,
								     devNo);
					notified = TRUE;
				}
			}
			/* ServerNotReady / ServerLost / SegmentStateStandby */
			else if (state.alive == segment_state_standby.alive &&
				 state.operating ==
				 segment_state_standby.operating) {
				/* technically this is standby case
				 * where server is lost
				 */
				if (notifiers->device_pause) {
					(*notifiers->device_pause) (busNo,
								    devNo);
					notified = TRUE;
				}
			} else if (state.alive == segment_state_paused.alive &&
				   state.operating ==
				   segment_state_paused.operating) {
				/* this is lite pause where channel is
				 * still valid just 'pause' of it
				 */
				if (busNo == g_diagpoolBusNo
				    && devNo == g_diagpoolDevNo) {
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
		 * for calling the appropriate visorchipset_busdev_responders
		 * function, which will call device_responder()
		 */
		;
	else
		device_responder(cmd, busNo, devNo, response);
	up(&NotifierLock);
}

static void
bus_create(struct controlvm_message *inmsg)
{
	struct controlvm_message_packet *cmd = &inmsg->cmd;
	ulong busNo = cmd->create_bus.bus_no;
	int rc = CONTROLVM_RESP_SUCCESS;
	struct visorchipset_bus_info *pBusInfo = NULL;


	pBusInfo = findbus(&BusInfoList, busNo);
	if (pBusInfo && (pBusInfo->state.created == 1)) {
		POSTCODE_LINUX_3(BUS_CREATE_FAILURE_PC, busNo,
				 POSTCODE_SEVERITY_ERR);
		rc = -CONTROLVM_RESP_ERROR_ALREADY_DONE;
		goto Away;
	}
	pBusInfo = kzalloc(sizeof(struct visorchipset_bus_info), GFP_KERNEL);
	if (pBusInfo == NULL) {
		POSTCODE_LINUX_3(BUS_CREATE_FAILURE_PC, busNo,
				 POSTCODE_SEVERITY_ERR);
		rc = -CONTROLVM_RESP_ERROR_KMALLOC_FAILED;
		goto Away;
	}

	INIT_LIST_HEAD(&pBusInfo->entry);
	pBusInfo->bus_no = busNo;
	pBusInfo->dev_no = cmd->create_bus.dev_count;

	POSTCODE_LINUX_3(BUS_CREATE_ENTRY_PC, busNo, POSTCODE_SEVERITY_INFO);

	if (inmsg->hdr.flags.test_message == 1)
		pBusInfo->chan_info.addr_type = ADDRTYPE_LOCALTEST;
	else
		pBusInfo->chan_info.addr_type = ADDRTYPE_LOCALPHYSICAL;

	pBusInfo->flags.server = inmsg->hdr.flags.server;
	pBusInfo->chan_info.channel_addr = cmd->create_bus.channel_addr;
	pBusInfo->chan_info.n_channel_bytes = cmd->create_bus.channel_bytes;
	pBusInfo->chan_info.channel_type_uuid =
			cmd->create_bus.bus_data_type_uuid;
	pBusInfo->chan_info.channel_inst_uuid = cmd->create_bus.bus_inst_uuid;

	list_add(&pBusInfo->entry, &BusInfoList);

	POSTCODE_LINUX_3(BUS_CREATE_EXIT_PC, busNo, POSTCODE_SEVERITY_INFO);

Away:
	bus_epilog(busNo, CONTROLVM_BUS_CREATE, &inmsg->hdr,
		   rc, inmsg->hdr.flags.response_expected == 1);
}

static void
bus_destroy(struct controlvm_message *inmsg)
{
	struct controlvm_message_packet *cmd = &inmsg->cmd;
	ulong busNo = cmd->destroy_bus.bus_no;
	struct visorchipset_bus_info *pBusInfo;
	int rc = CONTROLVM_RESP_SUCCESS;

	pBusInfo = findbus(&BusInfoList, busNo);
	if (!pBusInfo) {
		rc = -CONTROLVM_RESP_ERROR_BUS_INVALID;
		goto Away;
	}
	if (pBusInfo->state.created == 0) {
		rc = -CONTROLVM_RESP_ERROR_ALREADY_DONE;
		goto Away;
	}

Away:
	bus_epilog(busNo, CONTROLVM_BUS_DESTROY, &inmsg->hdr,
		   rc, inmsg->hdr.flags.response_expected == 1);
}

static void
bus_configure(struct controlvm_message *inmsg,
	      struct parser_context *parser_ctx)
{
	struct controlvm_message_packet *cmd = &inmsg->cmd;
	ulong busNo = cmd->configure_bus.bus_no;
	struct visorchipset_bus_info *pBusInfo = NULL;
	int rc = CONTROLVM_RESP_SUCCESS;
	char s[99];

	busNo = cmd->configure_bus.bus_no;
	POSTCODE_LINUX_3(BUS_CONFIGURE_ENTRY_PC, busNo, POSTCODE_SEVERITY_INFO);

	pBusInfo = findbus(&BusInfoList, busNo);
	if (!pBusInfo) {
		POSTCODE_LINUX_3(BUS_CONFIGURE_FAILURE_PC, busNo,
				 POSTCODE_SEVERITY_ERR);
		rc = -CONTROLVM_RESP_ERROR_BUS_INVALID;
		goto Away;
	}
	if (pBusInfo->state.created == 0) {
		POSTCODE_LINUX_3(BUS_CONFIGURE_FAILURE_PC, busNo,
				 POSTCODE_SEVERITY_ERR);
		rc = -CONTROLVM_RESP_ERROR_BUS_INVALID;
		goto Away;
	}
	/* TBD - add this check to other commands also... */
	if (pBusInfo->pending_msg_hdr.id != CONTROLVM_INVALID) {
		POSTCODE_LINUX_3(BUS_CONFIGURE_FAILURE_PC, busNo,
				 POSTCODE_SEVERITY_ERR);
		rc = -CONTROLVM_RESP_ERROR_MESSAGE_ID_INVALID_FOR_CLIENT;
		goto Away;
	}

	pBusInfo->partition_handle = cmd->configure_bus.guest_handle;
	pBusInfo->partition_uuid = parser_id_get(parser_ctx);
	parser_param_start(parser_ctx, PARSERSTRING_NAME);
	pBusInfo->name = parser_string_get(parser_ctx);

	visorchannel_uuid_id(&pBusInfo->partition_uuid, s);
	POSTCODE_LINUX_3(BUS_CONFIGURE_EXIT_PC, busNo, POSTCODE_SEVERITY_INFO);
Away:
	bus_epilog(busNo, CONTROLVM_BUS_CONFIGURE, &inmsg->hdr,
		   rc, inmsg->hdr.flags.response_expected == 1);
}

static void
my_device_create(struct controlvm_message *inmsg)
{
	struct controlvm_message_packet *cmd = &inmsg->cmd;
	ulong busNo = cmd->create_device.bus_no;
	ulong devNo = cmd->create_device.dev_no;
	struct visorchipset_device_info *pDevInfo = NULL;
	struct visorchipset_bus_info *pBusInfo = NULL;
	int rc = CONTROLVM_RESP_SUCCESS;

	pDevInfo = finddevice(&DevInfoList, busNo, devNo);
	if (pDevInfo && (pDevInfo->state.created == 1)) {
		POSTCODE_LINUX_4(DEVICE_CREATE_FAILURE_PC, devNo, busNo,
				 POSTCODE_SEVERITY_ERR);
		rc = -CONTROLVM_RESP_ERROR_ALREADY_DONE;
		goto Away;
	}
	pBusInfo = findbus(&BusInfoList, busNo);
	if (!pBusInfo) {
		POSTCODE_LINUX_4(DEVICE_CREATE_FAILURE_PC, devNo, busNo,
				 POSTCODE_SEVERITY_ERR);
		rc = -CONTROLVM_RESP_ERROR_BUS_INVALID;
		goto Away;
	}
	if (pBusInfo->state.created == 0) {
		POSTCODE_LINUX_4(DEVICE_CREATE_FAILURE_PC, devNo, busNo,
				 POSTCODE_SEVERITY_ERR);
		rc = -CONTROLVM_RESP_ERROR_BUS_INVALID;
		goto Away;
	}
	pDevInfo = kzalloc(sizeof(struct visorchipset_device_info), GFP_KERNEL);
	if (pDevInfo == NULL) {
		POSTCODE_LINUX_4(DEVICE_CREATE_FAILURE_PC, devNo, busNo,
				 POSTCODE_SEVERITY_ERR);
		rc = -CONTROLVM_RESP_ERROR_KMALLOC_FAILED;
		goto Away;
	}

	INIT_LIST_HEAD(&pDevInfo->entry);
	pDevInfo->bus_no = busNo;
	pDevInfo->dev_no = devNo;
	pDevInfo->dev_inst_uuid = cmd->create_device.dev_inst_uuid;
	POSTCODE_LINUX_4(DEVICE_CREATE_ENTRY_PC, devNo, busNo,
			 POSTCODE_SEVERITY_INFO);

	if (inmsg->hdr.flags.test_message == 1)
		pDevInfo->chan_info.addr_type = ADDRTYPE_LOCALTEST;
	else
		pDevInfo->chan_info.addr_type = ADDRTYPE_LOCALPHYSICAL;
	pDevInfo->chan_info.channel_addr = cmd->create_device.channel_addr;
	pDevInfo->chan_info.n_channel_bytes = cmd->create_device.channel_bytes;
	pDevInfo->chan_info.channel_type_uuid =
			cmd->create_device.data_type_uuid;
	pDevInfo->chan_info.intr = cmd->create_device.intr;
	list_add(&pDevInfo->entry, &DevInfoList);
	POSTCODE_LINUX_4(DEVICE_CREATE_EXIT_PC, devNo, busNo,
			 POSTCODE_SEVERITY_INFO);
Away:
	/* get the bus and devNo for DiagPool channel */
	if (pDevInfo &&
	    is_diagpool_channel(pDevInfo->chan_info.channel_type_uuid)) {
		g_diagpoolBusNo = busNo;
		g_diagpoolDevNo = devNo;
	}
	device_epilog(busNo, devNo, segment_state_running,
		      CONTROLVM_DEVICE_CREATE, &inmsg->hdr, rc,
		      inmsg->hdr.flags.response_expected == 1,
		      FOR_VISORBUS(pDevInfo->chan_info.channel_type_uuid));
}

static void
my_device_changestate(struct controlvm_message *inmsg)
{
	struct controlvm_message_packet *cmd = &inmsg->cmd;
	ulong busNo = cmd->device_change_state.bus_no;
	ulong devNo = cmd->device_change_state.dev_no;
	struct spar_segment_state state = cmd->device_change_state.state;
	struct visorchipset_device_info *pDevInfo = NULL;
	int rc = CONTROLVM_RESP_SUCCESS;

	pDevInfo = finddevice(&DevInfoList, busNo, devNo);
	if (!pDevInfo) {
		POSTCODE_LINUX_4(DEVICE_CHANGESTATE_FAILURE_PC, devNo, busNo,
				 POSTCODE_SEVERITY_ERR);
		rc = -CONTROLVM_RESP_ERROR_DEVICE_INVALID;
		goto Away;
	}
	if (pDevInfo->state.created == 0) {
		POSTCODE_LINUX_4(DEVICE_CHANGESTATE_FAILURE_PC, devNo, busNo,
				 POSTCODE_SEVERITY_ERR);
		rc = -CONTROLVM_RESP_ERROR_DEVICE_INVALID;
	}
Away:
	if ((rc >= CONTROLVM_RESP_SUCCESS) && pDevInfo)
		device_epilog(busNo, devNo, state, CONTROLVM_DEVICE_CHANGESTATE,
			      &inmsg->hdr, rc,
			      inmsg->hdr.flags.response_expected == 1,
			      FOR_VISORBUS(
					pDevInfo->chan_info.channel_type_uuid));
}

static void
my_device_destroy(struct controlvm_message *inmsg)
{
	struct controlvm_message_packet *cmd = &inmsg->cmd;
	ulong busNo = cmd->destroy_device.bus_no;
	ulong devNo = cmd->destroy_device.dev_no;
	struct visorchipset_device_info *pDevInfo = NULL;
	int rc = CONTROLVM_RESP_SUCCESS;

	pDevInfo = finddevice(&DevInfoList, busNo, devNo);
	if (!pDevInfo) {
		rc = -CONTROLVM_RESP_ERROR_DEVICE_INVALID;
		goto Away;
	}
	if (pDevInfo->state.created == 0) {
		rc = -CONTROLVM_RESP_ERROR_ALREADY_DONE;
	}

Away:
	if ((rc >= CONTROLVM_RESP_SUCCESS) && pDevInfo)
		device_epilog(busNo, devNo, segment_state_running,
			      CONTROLVM_DEVICE_DESTROY, &inmsg->hdr, rc,
			      inmsg->hdr.flags.response_expected == 1,
			      FOR_VISORBUS(
					pDevInfo->chan_info.channel_type_uuid));
}

/* When provided with the physical address of the controlvm channel
 * (phys_addr), the offset to the payload area we need to manage
 * (offset), and the size of this payload area (bytes), fills in the
 * controlvm_payload_info struct.  Returns TRUE for success or FALSE
 * for failure.
 */
static int
initialize_controlvm_payload_info(HOSTADDRESS phys_addr, u64 offset, u32 bytes,
				  struct controlvm_payload_info *info)
{
	u8 __iomem *payload = NULL;
	int rc = CONTROLVM_RESP_SUCCESS;

	if (info == NULL) {
		rc = -CONTROLVM_RESP_ERROR_PAYLOAD_INVALID;
		goto Away;
	}
	memset(info, 0, sizeof(struct controlvm_payload_info));
	if ((offset == 0) || (bytes == 0)) {
		rc = -CONTROLVM_RESP_ERROR_PAYLOAD_INVALID;
		goto Away;
	}
	payload = ioremap_cache(phys_addr + offset, bytes);
	if (payload == NULL) {
		rc = -CONTROLVM_RESP_ERROR_IOREMAP_FAILED;
		goto Away;
	}

	info->offset = offset;
	info->bytes = bytes;
	info->ptr = payload;

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
destroy_controlvm_payload_info(struct controlvm_payload_info *info)
{
	if (info->ptr != NULL) {
		iounmap(info->ptr);
		info->ptr = NULL;
	}
	memset(info, 0, sizeof(struct controlvm_payload_info));
}

static void
initialize_controlvm_payload(void)
{
	HOSTADDRESS phys_addr = visorchannel_get_physaddr(ControlVm_channel);
	u64 payloadOffset = 0;
	u32 payloadBytes = 0;

	if (visorchannel_read(ControlVm_channel,
			      offsetof(struct spar_controlvm_channel_protocol,
				       request_payload_offset),
			      &payloadOffset, sizeof(payloadOffset)) < 0) {
		POSTCODE_LINUX_2(CONTROLVM_INIT_FAILURE_PC,
				 POSTCODE_SEVERITY_ERR);
		return;
	}
	if (visorchannel_read(ControlVm_channel,
			      offsetof(struct spar_controlvm_channel_protocol,
				       request_payload_bytes),
			      &payloadBytes, sizeof(payloadBytes)) < 0) {
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
chipset_ready(struct controlvm_message_header *msgHdr)
{
	int rc = visorchipset_chipset_ready();

	if (rc != CONTROLVM_RESP_SUCCESS)
		rc = -rc;
	if (msgHdr->flags.response_expected && !visorchipset_holdchipsetready)
		controlvm_respond(msgHdr, rc);
	if (msgHdr->flags.response_expected && visorchipset_holdchipsetready) {
		/* Send CHIPSET_READY response when all modules have been loaded
		 * and disks mounted for the partition
		 */
		g_chipset_msg_hdr = *msgHdr;
	}
}

static void
chipset_selftest(struct controlvm_message_header *msgHdr)
{
	int rc = visorchipset_chipset_selftest();

	if (rc != CONTROLVM_RESP_SUCCESS)
		rc = -rc;
	if (msgHdr->flags.response_expected)
		controlvm_respond(msgHdr, rc);
}

static void
chipset_notready(struct controlvm_message_header *msgHdr)
{
	int rc = visorchipset_chipset_notready();

	if (rc != CONTROLVM_RESP_SUCCESS)
		rc = -rc;
	if (msgHdr->flags.response_expected)
		controlvm_respond(msgHdr, rc);
}

/* This is your "one-stop" shop for grabbing the next message from the
 * CONTROLVM_QUEUE_EVENT queue in the controlvm channel.
 */
static BOOL
read_controlvm_event(struct controlvm_message *msg)
{
	if (visorchannel_signalremove(ControlVm_channel,
				      CONTROLVM_QUEUE_EVENT, msg)) {
		/* got a message */
		if (msg->hdr.flags.test_message == 1)
			return FALSE;
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
	return jiffies + msecs_to_jiffies(PARAHOTPLUG_TIMEOUT_MS);
}

/*
 * Create a parahotplug_request, which is basically a wrapper for a
 * CONTROLVM_MESSAGE that we can stick on a list
 */
static struct parahotplug_request *
parahotplug_request_create(struct controlvm_message *msg)
{
	struct parahotplug_request *req;

	req = kmalloc(sizeof(*req), GFP_KERNEL|__GFP_NORETRY);
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
	struct controlvm_message_packet *cmd = &req->msg.cmd;
	char env_cmd[40], env_id[40], env_state[40], env_bus[40], env_dev[40],
	    env_func[40];
	char *envp[] = {
		env_cmd, env_id, env_state, env_bus, env_dev, env_func, NULL
	};

	sprintf(env_cmd, "SPAR_PARAHOTPLUG=1");
	sprintf(env_id, "SPAR_PARAHOTPLUG_ID=%d", req->id);
	sprintf(env_state, "SPAR_PARAHOTPLUG_STATE=%d",
		cmd->device_change_state.state.active);
	sprintf(env_bus, "SPAR_PARAHOTPLUG_BUS=%d",
		cmd->device_change_state.bus_no);
	sprintf(env_dev, "SPAR_PARAHOTPLUG_DEVICE=%d",
		cmd->device_change_state.dev_no >> 3);
	sprintf(env_func, "SPAR_PARAHOTPLUG_FUNCTION=%d",
		cmd->device_change_state.dev_no & 0x7);

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
			if (req->msg.hdr.flags.response_expected)
				controlvm_respond_physdev_changestate(
					&req->msg.hdr,
					CONTROLVM_RESP_ERROR_DEVICE_UDEV_TIMEOUT,
					req->msg.cmd.device_change_state.state);
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
parahotplug_request_complete(int id, u16 active)
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
			req->msg.cmd.device_change_state.state.active = active;
			if (req->msg.hdr.flags.response_expected)
				controlvm_respond_physdev_changestate(
					&req->msg.hdr, CONTROLVM_RESP_SUCCESS,
					req->msg.cmd.device_change_state.state);
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
parahotplug_process_message(struct controlvm_message *inmsg)
{
	struct parahotplug_request *req;

	req = parahotplug_request_create(inmsg);

	if (req == NULL)
		return;

	if (inmsg->cmd.device_change_state.state.active) {
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
				CONTROLVM_RESP_SUCCESS, inmsg->cmd.
				device_change_state.state);
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
handle_command(struct controlvm_message inmsg, HOSTADDRESS channel_addr)
{
	struct controlvm_message_packet *cmd = &inmsg.cmd;
	u64 parametersAddr = 0;
	u32 parametersBytes = 0;
	struct parser_context *parser_ctx = NULL;
	BOOL isLocalAddr = FALSE;
	struct controlvm_message ackmsg;

	/* create parsing context if necessary */
	isLocalAddr = (inmsg.hdr.flags.test_message == 1);
	if (channel_addr == 0)
		return TRUE;
	parametersAddr = channel_addr + inmsg.hdr.payload_vm_offset;
	parametersBytes = inmsg.hdr.payload_bytes;

	/* Parameter and channel addresses within test messages actually lie
	 * within our OS-controlled memory.  We need to know that, because it
	 * makes a difference in how we compute the virtual address.
	 */
	if (parametersAddr != 0 && parametersBytes != 0) {
		BOOL retry = FALSE;

		parser_ctx =
		    parser_init_byte_stream(parametersAddr, parametersBytes,
					   isLocalAddr, &retry);
		if (!parser_ctx && retry)
			return FALSE;
	}

	if (!isLocalAddr) {
		controlvm_init_response(&ackmsg, &inmsg.hdr,
					CONTROLVM_RESP_SUCCESS);
		if (ControlVm_channel)
			visorchannel_signalinsert(ControlVm_channel,
						  CONTROLVM_QUEUE_ACK,
						  &ackmsg);
	}
	switch (inmsg.hdr.id) {
	case CONTROLVM_CHIPSET_INIT:
		chipset_init(&inmsg);
		break;
	case CONTROLVM_BUS_CREATE:
		bus_create(&inmsg);
		break;
	case CONTROLVM_BUS_DESTROY:
		bus_destroy(&inmsg);
		break;
	case CONTROLVM_BUS_CONFIGURE:
		bus_configure(&inmsg, parser_ctx);
		break;
	case CONTROLVM_DEVICE_CREATE:
		my_device_create(&inmsg);
		break;
	case CONTROLVM_DEVICE_CHANGESTATE:
		if (cmd->device_change_state.flags.phys_device) {
			parahotplug_process_message(&inmsg);
		} else {
			/* save the hdr and cmd structures for later use */
			/* when sending back the response to Command */
			my_device_changestate(&inmsg);
			g_diag_msg_hdr = inmsg.hdr;
			g_DeviceChangeStatePacket = inmsg.cmd;
			break;
		}
		break;
	case CONTROLVM_DEVICE_DESTROY:
		my_device_destroy(&inmsg);
		break;
	case CONTROLVM_DEVICE_CONFIGURE:
		/* no op for now, just send a respond that we passed */
		if (inmsg.hdr.flags.response_expected)
			controlvm_respond(&inmsg.hdr, CONTROLVM_RESP_SUCCESS);
		break;
	case CONTROLVM_CHIPSET_READY:
		chipset_ready(&inmsg.hdr);
		break;
	case CONTROLVM_CHIPSET_SELFTEST:
		chipset_selftest(&inmsg.hdr);
		break;
	case CONTROLVM_CHIPSET_STOP:
		chipset_notready(&inmsg.hdr);
		break;
	default:
		if (inmsg.hdr.flags.response_expected)
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

static HOSTADDRESS controlvm_get_channel_address(void)
{
	u64 addr = 0;
	u32 size = 0;

	if (!VMCALL_SUCCESSFUL(issue_vmcall_io_controlvm_addr(&addr, &size)))
		return 0;

	return addr;
}

static void
controlvm_periodic_work(struct work_struct *work)
{
	struct controlvm_message inmsg;
	BOOL gotACommand = FALSE;
	BOOL handle_command_failed = FALSE;
	static u64 Poll_Count;

	/* make sure visorbus server is registered for controlvm callbacks */
	if (visorchipset_serverregwait && !serverregistered)
		goto Away;
	/* make sure visorclientbus server is regsitered for controlvm
	 * callbacks
	 */
	if (visorchipset_clientregwait && !clientregistered)
		goto Away;

	Poll_Count++;
	if (Poll_Count >= 250)
		;	/* keep going */
	else
		goto Away;

	/* Check events to determine if response to CHIPSET_READY
	 * should be sent
	 */
	if (visorchipset_holdchipsetready
	    && (g_chipset_msg_hdr.id != CONTROLVM_INVALID)) {
		if (check_chipset_events() == 1) {
			controlvm_respond(&g_chipset_msg_hdr, 0);
			clear_chipset_events();
			memset(&g_chipset_msg_hdr, 0,
			       sizeof(struct controlvm_message_header));
		}
	}

	while (visorchannel_signalremove(ControlVm_channel,
					 CONTROLVM_QUEUE_RESPONSE,
					 &inmsg)) {
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

	handle_command_failed = FALSE;
	while (gotACommand && (!handle_command_failed)) {
		most_recent_message_jiffies = jiffies;
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
	}

	/* parahotplug_worker */
	parahotplug_process_list();

Away:

	if (time_after(jiffies,
		       most_recent_message_jiffies + (HZ * MIN_IDLE_SECONDS))) {
		/* it's been longer than MIN_IDLE_SECONDS since we
		* processed our last controlvm message; slow down the
		* polling
		*/
		if (poll_jiffies != POLLJIFFIES_CONTROLVMCHANNEL_SLOW)
			poll_jiffies = POLLJIFFIES_CONTROLVMCHANNEL_SLOW;
	} else {
		if (poll_jiffies != POLLJIFFIES_CONTROLVMCHANNEL_FAST)
			poll_jiffies = POLLJIFFIES_CONTROLVMCHANNEL_FAST;
	}

	queue_delayed_work(periodic_controlvm_workqueue,
			   &periodic_controlvm_work, poll_jiffies);
}

static void
setup_crash_devices_work_queue(struct work_struct *work)
{

	struct controlvm_message localCrashCreateBusMsg;
	struct controlvm_message localCrashCreateDevMsg;
	struct controlvm_message msg;
	u32 localSavedCrashMsgOffset;
	u16 localSavedCrashMsgCount;

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
	msg.hdr.id = CONTROLVM_CHIPSET_INIT;
	msg.cmd.init_chipset.bus_count = 23;
	msg.cmd.init_chipset.switch_count = 0;

	chipset_init(&msg);

	/* get saved message count */
	if (visorchannel_read(ControlVm_channel,
			      offsetof(struct spar_controlvm_channel_protocol,
				       saved_crash_message_count),
			      &localSavedCrashMsgCount, sizeof(u16)) < 0) {
		POSTCODE_LINUX_2(CRASH_DEV_CTRL_RD_FAILURE_PC,
				 POSTCODE_SEVERITY_ERR);
		return;
	}

	if (localSavedCrashMsgCount != CONTROLVM_CRASHMSG_MAX) {
		POSTCODE_LINUX_3(CRASH_DEV_COUNT_FAILURE_PC,
				 localSavedCrashMsgCount,
				 POSTCODE_SEVERITY_ERR);
		return;
	}

	/* get saved crash message offset */
	if (visorchannel_read(ControlVm_channel,
			      offsetof(struct spar_controlvm_channel_protocol,
				       saved_crash_message_offset),
			      &localSavedCrashMsgOffset, sizeof(u32)) < 0) {
		POSTCODE_LINUX_2(CRASH_DEV_CTRL_RD_FAILURE_PC,
				 POSTCODE_SEVERITY_ERR);
		return;
	}

	/* read create device message for storage bus offset */
	if (visorchannel_read(ControlVm_channel,
			      localSavedCrashMsgOffset,
			      &localCrashCreateBusMsg,
			      sizeof(struct controlvm_message)) < 0) {
		POSTCODE_LINUX_2(CRASH_DEV_RD_BUS_FAIULRE_PC,
				 POSTCODE_SEVERITY_ERR);
		return;
	}

	/* read create device message for storage device */
	if (visorchannel_read(ControlVm_channel,
			      localSavedCrashMsgOffset +
			      sizeof(struct controlvm_message),
			      &localCrashCreateDevMsg,
			      sizeof(struct controlvm_message)) < 0) {
		POSTCODE_LINUX_2(CRASH_DEV_RD_DEV_FAIULRE_PC,
				 POSTCODE_SEVERITY_ERR);
		return;
	}

	/* reuse IOVM create bus message */
	if (localCrashCreateBusMsg.cmd.create_bus.channel_addr != 0)
		bus_create(&localCrashCreateBusMsg);
	else {
		POSTCODE_LINUX_2(CRASH_DEV_BUS_NULL_FAILURE_PC,
				 POSTCODE_SEVERITY_ERR);
		return;
	}

	/* reuse create device message for storage device */
	if (localCrashCreateDevMsg.cmd.create_device.channel_addr != 0)
		my_device_create(&localCrashCreateDevMsg);
	else {
		POSTCODE_LINUX_2(CRASH_DEV_DEV_NULL_FAILURE_PC,
				 POSTCODE_SEVERITY_ERR);
		return;
	}
	POSTCODE_LINUX_2(CRASH_DEV_EXIT_PC, POSTCODE_SEVERITY_INFO);
	return;

Away:

	poll_jiffies = POLLJIFFIES_CONTROLVMCHANNEL_SLOW;

	queue_delayed_work(periodic_controlvm_workqueue,
			   &periodic_controlvm_work, poll_jiffies);
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
visorchipset_device_pause_response(ulong bus_no, ulong dev_no, int response)
{

	device_changestate_responder(CONTROLVM_DEVICE_CHANGESTATE,
				     bus_no, dev_no, response,
				     segment_state_standby);
}
EXPORT_SYMBOL_GPL(visorchipset_device_pause_response);

static void
device_resume_response(ulong busNo, ulong devNo, int response)
{
	device_changestate_responder(CONTROLVM_DEVICE_CHANGESTATE,
				     busNo, devNo, response,
				     segment_state_running);
}

BOOL
visorchipset_get_bus_info(ulong bus_no, struct visorchipset_bus_info *bus_info)
{
	void *p = findbus(&BusInfoList, bus_no);

	if (!p)
		return FALSE;
	memcpy(bus_info, p, sizeof(struct visorchipset_bus_info));
	return TRUE;
}
EXPORT_SYMBOL_GPL(visorchipset_get_bus_info);

BOOL
visorchipset_set_bus_context(ulong bus_no, void *context)
{
	struct visorchipset_bus_info *p = findbus(&BusInfoList, bus_no);

	if (!p)
		return FALSE;
	p->bus_driver_context = context;
	return TRUE;
}
EXPORT_SYMBOL_GPL(visorchipset_set_bus_context);

BOOL
visorchipset_get_device_info(ulong bus_no, ulong dev_no,
			     struct visorchipset_device_info *dev_info)
{
	void *p = finddevice(&DevInfoList, bus_no, dev_no);

	if (!p)
		return FALSE;
	memcpy(dev_info, p, sizeof(struct visorchipset_device_info));
	return TRUE;
}
EXPORT_SYMBOL_GPL(visorchipset_get_device_info);

BOOL
visorchipset_set_device_context(ulong bus_no, ulong dev_no, void *context)
{
	struct visorchipset_device_info *p =
			finddevice(&DevInfoList, bus_no, dev_no);

	if (!p)
		return FALSE;
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
	if (!p)
		return NULL;

	atomic_inc(&Visorchipset_cache_buffers_in_use);
	return p;
}

/* Generic wrapper function for freeing memory from a kmem_cache pool.
 */
void
visorchipset_cache_free(struct kmem_cache *pool, void *p, char *fn, int ln)
{
	if (!p)
		return;

	atomic_dec(&Visorchipset_cache_buffers_in_use);
	kmem_cache_free(pool, p);
}

static ssize_t chipsetready_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	char msgtype[64];

	if (sscanf(buf, "%63s", msgtype) != 1)
		return -EINVAL;

	if (strcmp(msgtype, "CALLHOMEDISK_MOUNTED") == 0) {
		chipset_events[0] = 1;
		return count;
	} else if (strcmp(msgtype, "MODULES_LOADED") == 0) {
		chipset_events[1] = 1;
		return count;
	}
	return -EINVAL;
}

/* The parahotplug/devicedisabled interface gets called by our support script
 * when an SR-IOV device has been shut down. The ID is passed to the script
 * and then passed back when the device has been removed.
 */
static ssize_t devicedisabled_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	uint id;

	if (kstrtouint(buf, 10, &id) != 0)
		return -EINVAL;

	parahotplug_request_complete(id, 0);
	return count;
}

/* The parahotplug/deviceenabled interface gets called by our support script
 * when an SR-IOV device has been recovered. The ID is passed to the script
 * and then passed back when the device has been brought back up.
 */
static ssize_t deviceenabled_store(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	uint id;

	if (kstrtouint(buf, 10, &id) != 0)
		return -EINVAL;

	parahotplug_request_complete(id, 1);
	return count;
}

static int __init
visorchipset_init(void)
{
	int rc = 0, x = 0;
	HOSTADDRESS addr;

	if (!unisys_spar_platform)
		return -ENODEV;

	memset(&BusDev_Server_Notifiers, 0, sizeof(BusDev_Server_Notifiers));
	memset(&BusDev_Client_Notifiers, 0, sizeof(BusDev_Client_Notifiers));
	memset(&ControlVm_payload_info, 0, sizeof(ControlVm_payload_info));
	memset(&LiveDump_info, 0, sizeof(LiveDump_info));
	atomic_set(&LiveDump_info.buffers_in_use, 0);

	if (visorchipset_testvnic) {
		POSTCODE_LINUX_3(CHIPSET_INIT_FAILURE_PC, x, DIAG_SEVERITY_ERR);
		rc = x;
		goto Away;
	}

	addr = controlvm_get_channel_address();
	if (addr != 0) {
		ControlVm_channel =
		    visorchannel_create_with_lock
		    (addr,
		     sizeof(struct spar_controlvm_channel_protocol),
		     spar_controlvm_channel_protocol_uuid);
		if (SPAR_CONTROLVM_CHANNEL_OK_CLIENT(
				visorchannel_get_header(ControlVm_channel))) {
			initialize_controlvm_payload();
		} else {
			visorchannel_destroy(ControlVm_channel);
			ControlVm_channel = NULL;
			return -ENODEV;
		}
	} else {
		return -ENODEV;
	}

	MajorDev = MKDEV(visorchipset_major, 0);
	rc = visorchipset_file_init(MajorDev, &ControlVm_channel);
	if (rc < 0) {
		POSTCODE_LINUX_2(CHIPSET_INIT_FAILURE_PC, DIAG_SEVERITY_ERR);
		goto Away;
	}

	memset(&g_diag_msg_hdr, 0, sizeof(struct controlvm_message_header));

	memset(&g_chipset_msg_hdr, 0, sizeof(struct controlvm_message_header));

	memset(&g_del_dump_msg_hdr, 0, sizeof(struct controlvm_message_header));

	Putfile_buffer_list_pool =
	    kmem_cache_create(Putfile_buffer_list_pool_name,
			      sizeof(struct putfile_buffer_entry),
			      0, SLAB_HWCACHE_ALIGN, NULL);
	if (!Putfile_buffer_list_pool) {
		POSTCODE_LINUX_2(CHIPSET_INIT_FAILURE_PC, DIAG_SEVERITY_ERR);
		rc = -1;
		goto Away;
	}
	if (!visorchipset_disable_controlvm) {
		/* if booting in a crash kernel */
		if (visorchipset_crash_kernel)
			INIT_DELAYED_WORK(&periodic_controlvm_work,
					  setup_crash_devices_work_queue);
		else
			INIT_DELAYED_WORK(&periodic_controlvm_work,
					  controlvm_periodic_work);
		periodic_controlvm_workqueue =
		    create_singlethread_workqueue("visorchipset_controlvm");

		if (periodic_controlvm_workqueue == NULL) {
			POSTCODE_LINUX_2(CREATE_WORKQUEUE_FAILED_PC,
					 DIAG_SEVERITY_ERR);
			rc = -ENOMEM;
			goto Away;
		}
		most_recent_message_jiffies = jiffies;
		poll_jiffies = POLLJIFFIES_CONTROLVMCHANNEL_FAST;
		rc = queue_delayed_work(periodic_controlvm_workqueue,
					&periodic_controlvm_work, poll_jiffies);
		if (rc < 0) {
			POSTCODE_LINUX_2(QUEUE_DELAYED_WORK_PC,
					 DIAG_SEVERITY_ERR);
			goto Away;
		}

	}

	Visorchipset_platform_device.dev.devt = MajorDev;
	if (platform_device_register(&Visorchipset_platform_device) < 0) {
		POSTCODE_LINUX_2(DEVICE_REGISTER_FAILURE_PC, DIAG_SEVERITY_ERR);
		rc = -1;
		goto Away;
	}
	POSTCODE_LINUX_2(CHIPSET_INIT_SUCCESS_PC, POSTCODE_SEVERITY_INFO);
	rc = 0;
Away:
	if (rc) {
		POSTCODE_LINUX_3(CHIPSET_INIT_FAILURE_PC, rc,
				 POSTCODE_SEVERITY_ERR);
	}
	return rc;
}

static void
visorchipset_exit(void)
{
	POSTCODE_LINUX_2(DRIVER_EXIT_PC, POSTCODE_SEVERITY_INFO);

	if (visorchipset_disable_controlvm) {
		;
	} else {
		cancel_delayed_work(&periodic_controlvm_work);
		flush_workqueue(periodic_controlvm_workqueue);
		destroy_workqueue(periodic_controlvm_workqueue);
		periodic_controlvm_workqueue = NULL;
		destroy_controlvm_payload_info(&ControlVm_payload_info);
	}
	Test_Vnic_channel = NULL;
	if (Putfile_buffer_list_pool) {
		kmem_cache_destroy(Putfile_buffer_list_pool);
		Putfile_buffer_list_pool = NULL;
	}

	cleanup_controlvm_structures();

	memset(&g_diag_msg_hdr, 0, sizeof(struct controlvm_message_header));

	memset(&g_chipset_msg_hdr, 0, sizeof(struct controlvm_message_header));

	memset(&g_del_dump_msg_hdr, 0, sizeof(struct controlvm_message_header));

	visorchannel_destroy(ControlVm_channel);

	visorchipset_file_cleanup();
	POSTCODE_LINUX_2(DRIVER_EXIT_PC, POSTCODE_SEVERITY_INFO);
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
