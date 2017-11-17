// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2010 - 2015 UNISYS CORPORATION
 * All rights reserved.
 */

#include <linux/acpi.h>
#include <linux/crash_dump.h>

#include "visorbus.h"
#include "visorbus_private.h"

/* {72120008-4AAB-11DC-8530-444553544200} */
#define VISOR_SIOVM_GUID GUID_INIT(0x72120008, 0x4AAB, 0x11DC, 0x85, 0x30, \
				   0x44, 0x45, 0x53, 0x54, 0x42, 0x00)

static const guid_t visor_vhba_channel_guid = VISOR_VHBA_CHANNEL_GUID;
static const guid_t visor_siovm_guid = VISOR_SIOVM_GUID;
static const guid_t visor_controlvm_channel_guid = VISOR_CONTROLVM_CHANNEL_GUID;

#define POLLJIFFIES_CONTROLVM_FAST 1
#define POLLJIFFIES_CONTROLVM_SLOW 100

#define MAX_CONTROLVM_PAYLOAD_BYTES (1024 * 128)

#define UNISYS_VISOR_LEAF_ID 0x40000000

/* The s-Par leaf ID returns "UnisysSpar64" encoded across ebx, ecx, edx */
#define UNISYS_VISOR_ID_EBX 0x73696e55
#define UNISYS_VISOR_ID_ECX 0x70537379
#define UNISYS_VISOR_ID_EDX 0x34367261

/*
 * When the controlvm channel is idle for at least MIN_IDLE_SECONDS, we switch
 * to slow polling mode. As soon as we get a controlvm message, we switch back
 * to fast polling mode.
 */
#define MIN_IDLE_SECONDS 10

struct parser_context {
	unsigned long allocbytes;
	unsigned long param_bytes;
	u8 *curr;
	unsigned long bytes_remaining;
	bool byte_stream;
	struct visor_controlvm_parameters_header data;
};

/* VMCALL_CONTROLVM_ADDR: Used by all guests, not just IO. */
#define VMCALL_CONTROLVM_ADDR 0x0501

enum vmcall_result {
	VMCALL_RESULT_SUCCESS = 0,
	VMCALL_RESULT_INVALID_PARAM = 1,
	VMCALL_RESULT_DATA_UNAVAILABLE = 2,
	VMCALL_RESULT_FAILURE_UNAVAILABLE = 3,
	VMCALL_RESULT_DEVICE_ERROR = 4,
	VMCALL_RESULT_DEVICE_NOT_READY = 5
};

/*
 * struct vmcall_io_controlvm_addr_params - Structure for IO VMCALLS. Has
 *					    parameters to VMCALL_CONTROLVM_ADDR
 *					    interface.
 * @address:	   The Guest-relative physical address of the ControlVm channel.
 *		   This VMCall fills this in with the appropriate address.
 *		   Contents provided by this VMCALL (OUT).
 * @channel_bytes: The size of the ControlVm channel in bytes This VMCall fills
 *		   this in with the appropriate address. Contents provided by
 *		   this VMCALL (OUT).
 * @unused:	   Unused Bytes in the 64-Bit Aligned Struct.
 */
struct vmcall_io_controlvm_addr_params {
	u64 address;
	u32 channel_bytes;
	u8 unused[4];
} __packed;

struct visorchipset_device {
	struct acpi_device *acpi_device;
	unsigned long poll_jiffies;
	/* when we got our last controlvm message */
	unsigned long most_recent_message_jiffies;
	struct delayed_work periodic_controlvm_work;
	struct visorchannel *controlvm_channel;
	unsigned long controlvm_payload_bytes_buffered;
	/*
	 * The following variables are used to handle the scenario where we are
	 * unable to offload the payload from a controlvm message due to memory
	 * requirements. In this scenario, we simply stash the controlvm
	 * message, then attempt to process it again the next time
	 * controlvm_periodic_work() runs.
	 */
	struct controlvm_message controlvm_pending_msg;
	bool controlvm_pending_msg_valid;
	struct vmcall_io_controlvm_addr_params controlvm_params;
};

static struct visorchipset_device *chipset_dev;

struct parahotplug_request {
	struct list_head list;
	int id;
	unsigned long expiration;
	struct controlvm_message msg;
};

/* prototypes for attributes */
static ssize_t toolaction_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	u8 tool_action = 0;
	int err;

	err = visorchannel_read(chipset_dev->controlvm_channel,
				offsetof(struct visor_controlvm_channel,
					 tool_action),
				&tool_action, sizeof(u8));
	if (err)
		return err;
	return sprintf(buf, "%u\n", tool_action);
}

static ssize_t toolaction_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	u8 tool_action;
	int err;

	if (kstrtou8(buf, 10, &tool_action))
		return -EINVAL;
	err = visorchannel_write(chipset_dev->controlvm_channel,
				 offsetof(struct visor_controlvm_channel,
					  tool_action),
				 &tool_action, sizeof(u8));
	if (err)
		return err;
	return count;
}
static DEVICE_ATTR_RW(toolaction);

static ssize_t boottotool_show(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct efi_visor_indication efi_visor_indication;
	int err;

	err = visorchannel_read(chipset_dev->controlvm_channel,
				offsetof(struct visor_controlvm_channel,
					 efi_visor_ind),
				&efi_visor_indication,
				sizeof(struct efi_visor_indication));
	if (err)
		return err;
	return sprintf(buf, "%u\n", efi_visor_indication.boot_to_tool);
}

static ssize_t boottotool_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	int val, err;
	struct efi_visor_indication efi_visor_indication;

	if (kstrtoint(buf, 10, &val))
		return -EINVAL;
	efi_visor_indication.boot_to_tool = val;
	err = visorchannel_write(chipset_dev->controlvm_channel,
				 offsetof(struct visor_controlvm_channel,
					  efi_visor_ind),
				 &(efi_visor_indication),
				 sizeof(struct efi_visor_indication));
	if (err)
		return err;
	return count;
}
static DEVICE_ATTR_RW(boottotool);

static ssize_t error_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	u32 error = 0;
	int err;

	err = visorchannel_read(chipset_dev->controlvm_channel,
				offsetof(struct visor_controlvm_channel,
					 installation_error),
				&error, sizeof(u32));
	if (err)
		return err;
	return sprintf(buf, "%u\n", error);
}

static ssize_t error_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	u32 error;
	int err;

	if (kstrtou32(buf, 10, &error))
		return -EINVAL;
	err = visorchannel_write(chipset_dev->controlvm_channel,
				 offsetof(struct visor_controlvm_channel,
					  installation_error),
				 &error, sizeof(u32));
	if (err)
		return err;
	return count;
}
static DEVICE_ATTR_RW(error);

static ssize_t textid_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	u32 text_id = 0;
	int err;

	err = visorchannel_read(chipset_dev->controlvm_channel,
				offsetof(struct visor_controlvm_channel,
					 installation_text_id),
				&text_id, sizeof(u32));
	if (err)
		return err;
	return sprintf(buf, "%u\n", text_id);
}

static ssize_t textid_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	u32 text_id;
	int err;

	if (kstrtou32(buf, 10, &text_id))
		return -EINVAL;
	err = visorchannel_write(chipset_dev->controlvm_channel,
				 offsetof(struct visor_controlvm_channel,
					  installation_text_id),
				 &text_id, sizeof(u32));
	if (err)
		return err;
	return count;
}
static DEVICE_ATTR_RW(textid);

static ssize_t remaining_steps_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	u16 remaining_steps = 0;
	int err;

	err = visorchannel_read(chipset_dev->controlvm_channel,
				offsetof(struct visor_controlvm_channel,
					 installation_remaining_steps),
				&remaining_steps, sizeof(u16));
	if (err)
		return err;
	return sprintf(buf, "%hu\n", remaining_steps);
}

static ssize_t remaining_steps_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	u16 remaining_steps;
	int err;

	if (kstrtou16(buf, 10, &remaining_steps))
		return -EINVAL;
	err = visorchannel_write(chipset_dev->controlvm_channel,
				 offsetof(struct visor_controlvm_channel,
					  installation_remaining_steps),
				 &remaining_steps, sizeof(u16));
	if (err)
		return err;
	return count;
}
static DEVICE_ATTR_RW(remaining_steps);

static void controlvm_init_response(struct controlvm_message *msg,
				    struct controlvm_message_header *msg_hdr,
				    int response)
{
	memset(msg, 0, sizeof(struct controlvm_message));
	memcpy(&msg->hdr, msg_hdr, sizeof(struct controlvm_message_header));
	msg->hdr.payload_bytes = 0;
	msg->hdr.payload_vm_offset = 0;
	msg->hdr.payload_max_bytes = 0;
	if (response < 0) {
		msg->hdr.flags.failed = 1;
		msg->hdr.completion_status = (u32)(-response);
	}
}

static int controlvm_respond_chipset_init(
				struct controlvm_message_header *msg_hdr,
				int response,
				enum visor_chipset_feature features)
{
	struct controlvm_message outmsg;

	controlvm_init_response(&outmsg, msg_hdr, response);
	outmsg.cmd.init_chipset.features = features;
	return visorchannel_signalinsert(chipset_dev->controlvm_channel,
					 CONTROLVM_QUEUE_REQUEST, &outmsg);
}

static int chipset_init(struct controlvm_message *inmsg)
{
	static int chipset_inited;
	enum visor_chipset_feature features = 0;
	int rc = CONTROLVM_RESP_SUCCESS;
	int res = 0;

	if (chipset_inited) {
		rc = -CONTROLVM_RESP_ALREADY_DONE;
		res = -EIO;
		goto out_respond;
	}
	chipset_inited = 1;
	/*
	 * Set features to indicate we support parahotplug (if Command also
	 * supports it). Set the "reply" bit so Command knows this is a
	 * features-aware driver.
	 */
	features = inmsg->cmd.init_chipset.features &
		   VISOR_CHIPSET_FEATURE_PARA_HOTPLUG;
	features |= VISOR_CHIPSET_FEATURE_REPLY;

out_respond:
	if (inmsg->hdr.flags.response_expected)
		res = controlvm_respond_chipset_init(&inmsg->hdr, rc, features);

	return res;
}

static int controlvm_respond(struct controlvm_message_header *msg_hdr,
			     int response, struct visor_segment_state *state)
{
	struct controlvm_message outmsg;

	controlvm_init_response(&outmsg, msg_hdr, response);
	if (outmsg.hdr.flags.test_message == 1)
		return -EINVAL;
	if (state) {
		outmsg.cmd.device_change_state.state = *state;
		outmsg.cmd.device_change_state.flags.phys_device = 1;
	}
	return visorchannel_signalinsert(chipset_dev->controlvm_channel,
					 CONTROLVM_QUEUE_REQUEST, &outmsg);
}

enum crash_obj_type {
	CRASH_DEV,
	CRASH_BUS,
};

static int save_crash_message(struct controlvm_message *msg,
			      enum crash_obj_type cr_type)
{
	u32 local_crash_msg_offset;
	u16 local_crash_msg_count;
	int err;

	err = visorchannel_read(chipset_dev->controlvm_channel,
				offsetof(struct visor_controlvm_channel,
					 saved_crash_message_count),
				&local_crash_msg_count, sizeof(u16));
	if (err) {
		dev_err(&chipset_dev->acpi_device->dev,
			"failed to read message count\n");
		return err;
	}
	if (local_crash_msg_count != CONTROLVM_CRASHMSG_MAX) {
		dev_err(&chipset_dev->acpi_device->dev,
			"invalid number of messages\n");
		return -EIO;
	}
	err = visorchannel_read(chipset_dev->controlvm_channel,
				offsetof(struct visor_controlvm_channel,
					 saved_crash_message_offset),
				&local_crash_msg_offset, sizeof(u32));
	if (err) {
		dev_err(&chipset_dev->acpi_device->dev,
			"failed to read offset\n");
		return err;
	}
	switch (cr_type) {
	case CRASH_DEV:
		local_crash_msg_offset += sizeof(struct controlvm_message);
		err = visorchannel_write(chipset_dev->controlvm_channel,
					 local_crash_msg_offset, msg,
					 sizeof(struct controlvm_message));
		if (err) {
			dev_err(&chipset_dev->acpi_device->dev,
				"failed to write dev msg\n");
			return err;
		}
		break;
	case CRASH_BUS:
		err = visorchannel_write(chipset_dev->controlvm_channel,
					 local_crash_msg_offset, msg,
					 sizeof(struct controlvm_message));
		if (err) {
			dev_err(&chipset_dev->acpi_device->dev,
				"failed to write bus msg\n");
			return err;
		}
		break;
	default:
		dev_err(&chipset_dev->acpi_device->dev,
			"Invalid crash_obj_type\n");
		break;
	}
	return 0;
}

static int controlvm_responder(enum controlvm_id cmd_id,
			       struct controlvm_message_header *pending_msg_hdr,
			       int response)
{
	if (pending_msg_hdr->id != (u32)cmd_id)
		return -EINVAL;

	return controlvm_respond(pending_msg_hdr, response, NULL);
}

static int device_changestate_responder(enum controlvm_id cmd_id,
					struct visor_device *p, int response,
					struct visor_segment_state state)
{
	struct controlvm_message outmsg;

	if (p->pending_msg_hdr->id != cmd_id)
		return -EINVAL;

	controlvm_init_response(&outmsg, p->pending_msg_hdr, response);
	outmsg.cmd.device_change_state.bus_no = p->chipset_bus_no;
	outmsg.cmd.device_change_state.dev_no = p->chipset_dev_no;
	outmsg.cmd.device_change_state.state = state;
	return visorchannel_signalinsert(chipset_dev->controlvm_channel,
					 CONTROLVM_QUEUE_REQUEST, &outmsg);
}

static int visorbus_create(struct controlvm_message *inmsg)
{
	struct controlvm_message_packet *cmd = &inmsg->cmd;
	struct controlvm_message_header *pmsg_hdr;
	u32 bus_no = cmd->create_bus.bus_no;
	struct visor_device *bus_info;
	struct visorchannel *visorchannel;
	int err;

	bus_info = visorbus_get_device_by_id(bus_no, BUS_ROOT_DEVICE, NULL);
	if (bus_info && bus_info->state.created == 1) {
		dev_err(&chipset_dev->acpi_device->dev,
			"failed %s: already exists\n", __func__);
		err = -EEXIST;
		goto err_respond;
	}
	bus_info = kzalloc(sizeof(*bus_info), GFP_KERNEL);
	if (!bus_info) {
		err = -ENOMEM;
		goto err_respond;
	}
	INIT_LIST_HEAD(&bus_info->list_all);
	bus_info->chipset_bus_no = bus_no;
	bus_info->chipset_dev_no = BUS_ROOT_DEVICE;
	if (guid_equal(&cmd->create_bus.bus_inst_guid, &visor_siovm_guid)) {
		err = save_crash_message(inmsg, CRASH_BUS);
		if (err)
			goto err_free_bus_info;
	}
	if (inmsg->hdr.flags.response_expected == 1) {
		pmsg_hdr = kzalloc(sizeof(*pmsg_hdr), GFP_KERNEL);
		if (!pmsg_hdr) {
			err = -ENOMEM;
			goto err_free_bus_info;
		}
		memcpy(pmsg_hdr, &inmsg->hdr,
		       sizeof(struct controlvm_message_header));
		bus_info->pending_msg_hdr = pmsg_hdr;
	}
	visorchannel = visorchannel_create(cmd->create_bus.channel_addr,
					   GFP_KERNEL,
					   &cmd->create_bus.bus_data_type_guid,
					   false);
	if (!visorchannel) {
		err = -ENOMEM;
		goto err_free_pending_msg;
	}
	bus_info->visorchannel = visorchannel;
	/* Response will be handled by visorbus_create_instance on success */
	err = visorbus_create_instance(bus_info);
	if (err)
		goto err_destroy_channel;
	return 0;

err_destroy_channel:
	visorchannel_destroy(visorchannel);

err_free_pending_msg:
	kfree(bus_info->pending_msg_hdr);

err_free_bus_info:
	kfree(bus_info);

err_respond:
	if (inmsg->hdr.flags.response_expected == 1)
		controlvm_responder(inmsg->hdr.id, &inmsg->hdr, err);
	return err;
}

static int visorbus_destroy(struct controlvm_message *inmsg)
{
	struct controlvm_message_header *pmsg_hdr;
	u32 bus_no = inmsg->cmd.destroy_bus.bus_no;
	struct visor_device *bus_info;
	int err;

	bus_info = visorbus_get_device_by_id(bus_no, BUS_ROOT_DEVICE, NULL);
	if (!bus_info) {
		err = -ENODEV;
		goto err_respond;
	}
	if (bus_info->state.created == 0) {
		err = -ENOENT;
		goto err_respond;
	}
	if (bus_info->pending_msg_hdr) {
		/* only non-NULL if dev is still waiting on a response */
		err = -EEXIST;
		goto err_respond;
	}
	if (inmsg->hdr.flags.response_expected == 1) {
		pmsg_hdr = kzalloc(sizeof(*pmsg_hdr), GFP_KERNEL);
		if (!pmsg_hdr) {
			err = -ENOMEM;
			goto err_respond;
		}
		memcpy(pmsg_hdr, &inmsg->hdr,
		       sizeof(struct controlvm_message_header));
		bus_info->pending_msg_hdr = pmsg_hdr;
	}
	/* Response will be handled by visorbus_remove_instance */
	visorbus_remove_instance(bus_info);
	return 0;

err_respond:
	if (inmsg->hdr.flags.response_expected == 1)
		controlvm_responder(inmsg->hdr.id, &inmsg->hdr, err);
	return err;
}

static const guid_t *parser_id_get(struct parser_context *ctx)
{
	return &ctx->data.id;
}

static void *parser_string_get(u8 *pscan, int nscan)
{
	int value_length;
	void *value;

	if (nscan == 0)
		return NULL;

	value_length = strnlen(pscan, nscan);
	value = kzalloc(value_length + 1, GFP_KERNEL);
	if (!value)
		return NULL;
	if (value_length > 0)
		memcpy(value, pscan, value_length);
	return value;
}

static void *parser_name_get(struct parser_context *ctx)
{
	struct visor_controlvm_parameters_header *phdr;

	phdr = &ctx->data;
	if ((unsigned long)phdr->name_offset +
	    (unsigned long)phdr->name_length > ctx->param_bytes)
		return NULL;
	ctx->curr = (char *)&phdr + phdr->name_offset;
	ctx->bytes_remaining = phdr->name_length;
	return parser_string_get(ctx->curr, phdr->name_length);
}

static int visorbus_configure(struct controlvm_message *inmsg,
			      struct parser_context *parser_ctx)
{
	struct controlvm_message_packet *cmd = &inmsg->cmd;
	u32 bus_no;
	struct visor_device *bus_info;
	int err = 0;

	bus_no = cmd->configure_bus.bus_no;
	bus_info = visorbus_get_device_by_id(bus_no, BUS_ROOT_DEVICE, NULL);
	if (!bus_info) {
		err = -EINVAL;
		goto err_respond;
	}
	if (bus_info->state.created == 0) {
		err = -EINVAL;
		goto err_respond;
	}
	if (bus_info->pending_msg_hdr) {
		err = -EIO;
		goto err_respond;
	}
	err = visorchannel_set_clientpartition(bus_info->visorchannel,
					       cmd->configure_bus.guest_handle);
	if (err)
		goto err_respond;
	if (parser_ctx) {
		const guid_t *partition_guid = parser_id_get(parser_ctx);

		guid_copy(&bus_info->partition_guid, partition_guid);
		bus_info->name = parser_name_get(parser_ctx);
	}
	if (inmsg->hdr.flags.response_expected == 1)
		controlvm_responder(inmsg->hdr.id, &inmsg->hdr, err);
	return 0;

err_respond:
	dev_err(&chipset_dev->acpi_device->dev,
		"%s exited with err: %d\n", __func__, err);
	if (inmsg->hdr.flags.response_expected == 1)
		controlvm_responder(inmsg->hdr.id, &inmsg->hdr, err);
	return err;
}

static int visorbus_device_create(struct controlvm_message *inmsg)
{
	struct controlvm_message_packet *cmd = &inmsg->cmd;
	struct controlvm_message_header *pmsg_hdr;
	u32 bus_no = cmd->create_device.bus_no;
	u32 dev_no = cmd->create_device.dev_no;
	struct visor_device *dev_info;
	struct visor_device *bus_info;
	struct visorchannel *visorchannel;
	int err;

	bus_info = visorbus_get_device_by_id(bus_no, BUS_ROOT_DEVICE, NULL);
	if (!bus_info) {
		dev_err(&chipset_dev->acpi_device->dev,
			"failed to get bus by id: %d\n", bus_no);
		err = -ENODEV;
		goto err_respond;
	}
	if (bus_info->state.created == 0) {
		dev_err(&chipset_dev->acpi_device->dev,
			"bus not created, id: %d\n", bus_no);
		err = -EINVAL;
		goto err_respond;
	}
	dev_info = visorbus_get_device_by_id(bus_no, dev_no, NULL);
	if (dev_info && dev_info->state.created == 1) {
		dev_err(&chipset_dev->acpi_device->dev,
			"failed to get bus by id: %d/%d\n", bus_no, dev_no);
		err = -EEXIST;
		goto err_respond;
	}

	dev_info = kzalloc(sizeof(*dev_info), GFP_KERNEL);
	if (!dev_info) {
		err = -ENOMEM;
		goto err_respond;
	}
	dev_info->chipset_bus_no = bus_no;
	dev_info->chipset_dev_no = dev_no;
	guid_copy(&dev_info->inst, &cmd->create_device.dev_inst_guid);
	dev_info->device.parent = &bus_info->device;
	visorchannel = visorchannel_create(cmd->create_device.channel_addr,
					   GFP_KERNEL,
					   &cmd->create_device.data_type_guid,
					   true);
	if (!visorchannel) {
		dev_err(&chipset_dev->acpi_device->dev,
			"failed to create visorchannel: %d/%d\n",
			bus_no, dev_no);
		err = -ENOMEM;
		goto err_free_dev_info;
	}
	dev_info->visorchannel = visorchannel;
	guid_copy(&dev_info->channel_type_guid,
		  &cmd->create_device.data_type_guid);
	if (guid_equal(&cmd->create_device.data_type_guid,
		       &visor_vhba_channel_guid)) {
		err = save_crash_message(inmsg, CRASH_DEV);
		if (err)
			goto err_destroy_visorchannel;
	}
	if (inmsg->hdr.flags.response_expected == 1) {
		pmsg_hdr = kzalloc(sizeof(*pmsg_hdr), GFP_KERNEL);
		if (!pmsg_hdr) {
			err = -ENOMEM;
			goto err_destroy_visorchannel;
		}
		memcpy(pmsg_hdr, &inmsg->hdr,
		       sizeof(struct controlvm_message_header));
		dev_info->pending_msg_hdr = pmsg_hdr;
	}
	/* create_visor_device will send response */
	err = create_visor_device(dev_info);
	if (err)
		goto err_destroy_visorchannel;

	return 0;

err_destroy_visorchannel:
	visorchannel_destroy(visorchannel);

err_free_dev_info:
	kfree(dev_info);

err_respond:
	if (inmsg->hdr.flags.response_expected == 1)
		controlvm_responder(inmsg->hdr.id, &inmsg->hdr, err);
	return err;
}

static int visorbus_device_changestate(struct controlvm_message *inmsg)
{
	struct controlvm_message_packet *cmd = &inmsg->cmd;
	struct controlvm_message_header *pmsg_hdr;
	u32 bus_no = cmd->device_change_state.bus_no;
	u32 dev_no = cmd->device_change_state.dev_no;
	struct visor_segment_state state = cmd->device_change_state.state;
	struct visor_device *dev_info;
	int err = 0;

	dev_info = visorbus_get_device_by_id(bus_no, dev_no, NULL);
	if (!dev_info) {
		err = -ENODEV;
		goto err_respond;
	}
	if (dev_info->state.created == 0) {
		err = -EINVAL;
		goto err_respond;
	}
	if (dev_info->pending_msg_hdr) {
		/* only non-NULL if dev is still waiting on a response */
		err = -EIO;
		goto err_respond;
	}

	if (inmsg->hdr.flags.response_expected == 1) {
		pmsg_hdr = kzalloc(sizeof(*pmsg_hdr), GFP_KERNEL);
		if (!pmsg_hdr) {
			err = -ENOMEM;
			goto err_respond;
		}
		memcpy(pmsg_hdr, &inmsg->hdr,
		       sizeof(struct controlvm_message_header));
		dev_info->pending_msg_hdr = pmsg_hdr;
	}
	if (state.alive == segment_state_running.alive &&
	    state.operating == segment_state_running.operating)
		/* Response will be sent from visorchipset_device_resume */
		err = visorchipset_device_resume(dev_info);
	/* ServerNotReady / ServerLost / SegmentStateStandby */
	else if (state.alive == segment_state_standby.alive &&
		 state.operating == segment_state_standby.operating)
		/*
		 * technically this is standby case where server is lost.
		 * Response will be sent from visorchipset_device_pause.
		 */
		err = visorchipset_device_pause(dev_info);
	if (err)
		goto err_respond;
	return 0;

err_respond:
	dev_err(&chipset_dev->acpi_device->dev, "failed: %d\n", err);
	if (inmsg->hdr.flags.response_expected == 1)
		controlvm_responder(inmsg->hdr.id, &inmsg->hdr, err);
	return err;
}

static int visorbus_device_destroy(struct controlvm_message *inmsg)
{
	struct controlvm_message_packet *cmd = &inmsg->cmd;
	struct controlvm_message_header *pmsg_hdr;
	u32 bus_no = cmd->destroy_device.bus_no;
	u32 dev_no = cmd->destroy_device.dev_no;
	struct visor_device *dev_info;
	int err;

	dev_info = visorbus_get_device_by_id(bus_no, dev_no, NULL);
	if (!dev_info) {
		err = -ENODEV;
		goto err_respond;
	}
	if (dev_info->state.created == 0) {
		err = -EINVAL;
		goto err_respond;
	}
	if (dev_info->pending_msg_hdr) {
		/* only non-NULL if dev is still waiting on a response */
		err = -EIO;
		goto err_respond;
	}
	if (inmsg->hdr.flags.response_expected == 1) {
		pmsg_hdr = kzalloc(sizeof(*pmsg_hdr), GFP_KERNEL);
		if (!pmsg_hdr) {
			err = -ENOMEM;
			goto err_respond;
		}

		memcpy(pmsg_hdr, &inmsg->hdr,
		       sizeof(struct controlvm_message_header));
		dev_info->pending_msg_hdr = pmsg_hdr;
	}
	kfree(dev_info->name);
	remove_visor_device(dev_info);
	return 0;

err_respond:
	if (inmsg->hdr.flags.response_expected == 1)
		controlvm_responder(inmsg->hdr.id, &inmsg->hdr, err);
	return err;
}

/*
 * The general parahotplug flow works as follows. The visorchipset receives
 * a DEVICE_CHANGESTATE message from Command specifying a physical device
 * to enable or disable. The CONTROLVM message handler calls
 * parahotplug_process_message, which then adds the message to a global list
 * and kicks off a udev event which causes a user level script to enable or
 * disable the specified device. The udev script then writes to
 * /sys/devices/platform/visorchipset/parahotplug, which causes the
 * parahotplug store functions to get called, at which point the
 * appropriate CONTROLVM message is retrieved from the list and responded to.
 */

#define PARAHOTPLUG_TIMEOUT_MS 2000

/*
 * parahotplug_next_id() - generate unique int to match an outstanding
 *                         CONTROLVM message with a udev script /sys
 *                         response
 *
 * Return: a unique integer value
 */
static int parahotplug_next_id(void)
{
	static atomic_t id = ATOMIC_INIT(0);

	return atomic_inc_return(&id);
}

/*
 * parahotplug_next_expiration() - returns the time (in jiffies) when a
 *                                 CONTROLVM message on the list should expire
 *                                 -- PARAHOTPLUG_TIMEOUT_MS in the future
 *
 * Return: expected expiration time (in jiffies)
 */
static unsigned long parahotplug_next_expiration(void)
{
	return jiffies + msecs_to_jiffies(PARAHOTPLUG_TIMEOUT_MS);
}

/*
 * parahotplug_request_create() - create a parahotplug_request, which is
 *                                basically a wrapper for a CONTROLVM_MESSAGE
 *                                that we can stick on a list
 * @msg: the message to insert in the request
 *
 * Return: the request containing the provided message
 */
static struct parahotplug_request *parahotplug_request_create(
						struct controlvm_message *msg)
{
	struct parahotplug_request *req;

	req = kmalloc(sizeof(*req), GFP_KERNEL);
	if (!req)
		return NULL;
	req->id = parahotplug_next_id();
	req->expiration = parahotplug_next_expiration();
	req->msg = *msg;
	return req;
}

/*
 * parahotplug_request_destroy() - free a parahotplug_request
 * @req: the request to deallocate
 */
static void parahotplug_request_destroy(struct parahotplug_request *req)
{
	kfree(req);
}

static LIST_HEAD(parahotplug_request_list);
/* lock for above */
static DEFINE_SPINLOCK(parahotplug_request_list_lock);

/*
 * parahotplug_request_complete() - mark request as complete
 * @id:     the id of the request
 * @active: indicates whether the request is assigned to active partition
 *
 * Called from the /sys handler, which means the user script has
 * finished the enable/disable. Find the matching identifier, and
 * respond to the CONTROLVM message with success.
 *
 * Return: 0 on success or -EINVAL on failure
 */
static int parahotplug_request_complete(int id, u16 active)
{
	struct list_head *pos;
	struct list_head *tmp;
	struct parahotplug_request *req;

	spin_lock(&parahotplug_request_list_lock);
	/* Look for a request matching "id". */
	list_for_each_safe(pos, tmp, &parahotplug_request_list) {
		req = list_entry(pos, struct parahotplug_request, list);
		if (req->id == id) {
			/*
			 * Found a match. Remove it from the list and
			 * respond.
			 */
			list_del(pos);
			spin_unlock(&parahotplug_request_list_lock);
			req->msg.cmd.device_change_state.state.active = active;
			if (req->msg.hdr.flags.response_expected)
				controlvm_respond(
				       &req->msg.hdr, CONTROLVM_RESP_SUCCESS,
				       &req->msg.cmd.device_change_state.state);
			parahotplug_request_destroy(req);
			return 0;
		}
	}
	spin_unlock(&parahotplug_request_list_lock);
	return -EINVAL;
}

/*
 * devicedisabled_store() - disables the hotplug device
 * @dev:   sysfs interface variable not utilized in this function
 * @attr:  sysfs interface variable not utilized in this function
 * @buf:   buffer containing the device id
 * @count: the size of the buffer
 *
 * The parahotplug/devicedisabled interface gets called by our support script
 * when an SR-IOV device has been shut down. The ID is passed to the script
 * and then passed back when the device has been removed.
 *
 * Return: the size of the buffer for success or negative for error
 */
static ssize_t devicedisabled_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	unsigned int id;
	int err;

	if (kstrtouint(buf, 10, &id))
		return -EINVAL;
	err = parahotplug_request_complete(id, 0);
	if (err < 0)
		return err;
	return count;
}
static DEVICE_ATTR_WO(devicedisabled);

/*
 * deviceenabled_store() - enables the hotplug device
 * @dev:   sysfs interface variable not utilized in this function
 * @attr:  sysfs interface variable not utilized in this function
 * @buf:   buffer containing the device id
 * @count: the size of the buffer
 *
 * The parahotplug/deviceenabled interface gets called by our support script
 * when an SR-IOV device has been recovered. The ID is passed to the script
 * and then passed back when the device has been brought back up.
 *
 * Return: the size of the buffer for success or negative for error
 */
static ssize_t deviceenabled_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	unsigned int id;

	if (kstrtouint(buf, 10, &id))
		return -EINVAL;
	parahotplug_request_complete(id, 1);
	return count;
}
static DEVICE_ATTR_WO(deviceenabled);

static struct attribute *visorchipset_install_attrs[] = {
	&dev_attr_toolaction.attr,
	&dev_attr_boottotool.attr,
	&dev_attr_error.attr,
	&dev_attr_textid.attr,
	&dev_attr_remaining_steps.attr,
	NULL
};

static const struct attribute_group visorchipset_install_group = {
	.name = "install",
	.attrs = visorchipset_install_attrs
};

static struct attribute *visorchipset_parahotplug_attrs[] = {
	&dev_attr_devicedisabled.attr,
	&dev_attr_deviceenabled.attr,
	NULL
};

static const struct attribute_group visorchipset_parahotplug_group = {
	.name = "parahotplug",
	.attrs = visorchipset_parahotplug_attrs
};

static const struct attribute_group *visorchipset_dev_groups[] = {
	&visorchipset_install_group,
	&visorchipset_parahotplug_group,
	NULL
};

/*
 * parahotplug_request_kickoff() - initiate parahotplug request
 * @req: the request to initiate
 *
 * Cause uevent to run the user level script to do the disable/enable specified
 * in the parahotplug_request.
 */
static int parahotplug_request_kickoff(struct parahotplug_request *req)
{
	struct controlvm_message_packet *cmd = &req->msg.cmd;
	char env_cmd[40], env_id[40], env_state[40], env_bus[40], env_dev[40],
	     env_func[40];
	char *envp[] = { env_cmd, env_id, env_state, env_bus, env_dev,
			 env_func, NULL
	};

	sprintf(env_cmd, "VISOR_PARAHOTPLUG=1");
	sprintf(env_id, "VISOR_PARAHOTPLUG_ID=%d", req->id);
	sprintf(env_state, "VISOR_PARAHOTPLUG_STATE=%d",
		cmd->device_change_state.state.active);
	sprintf(env_bus, "VISOR_PARAHOTPLUG_BUS=%d",
		cmd->device_change_state.bus_no);
	sprintf(env_dev, "VISOR_PARAHOTPLUG_DEVICE=%d",
		cmd->device_change_state.dev_no >> 3);
	sprintf(env_func, "VISOR_PARAHOTPLUG_FUNCTION=%d",
		cmd->device_change_state.dev_no & 0x7);
	return kobject_uevent_env(&chipset_dev->acpi_device->dev.kobj,
				  KOBJ_CHANGE, envp);
}

/*
 * parahotplug_process_message() - enables or disables a PCI device by kicking
 *                                 off a udev script
 * @inmsg: the message indicating whether to enable or disable
 */
static int parahotplug_process_message(struct controlvm_message *inmsg)
{
	struct parahotplug_request *req;
	int err;

	req = parahotplug_request_create(inmsg);
	if (!req)
		return -ENOMEM;
	/*
	 * For enable messages, just respond with success right away, we don't
	 * need to wait to see if the enable was successful.
	 */
	if (inmsg->cmd.device_change_state.state.active) {
		err = parahotplug_request_kickoff(req);
		if (err)
			goto err_respond;
		controlvm_respond(&inmsg->hdr, CONTROLVM_RESP_SUCCESS,
				  &inmsg->cmd.device_change_state.state);
		parahotplug_request_destroy(req);
		return 0;
	}
	/*
	 * For disable messages, add the request to the request list before
	 * kicking off the udev script. It won't get responded to until the
	 * script has indicated it's done.
	 */
	spin_lock(&parahotplug_request_list_lock);
	list_add_tail(&req->list, &parahotplug_request_list);
	spin_unlock(&parahotplug_request_list_lock);
	err = parahotplug_request_kickoff(req);
	if (err)
		goto err_respond;
	return 0;

err_respond:
	controlvm_respond(&inmsg->hdr, err,
			  &inmsg->cmd.device_change_state.state);
	return err;
}

/*
 * chipset_ready_uevent() - sends chipset_ready action
 *
 * Send ACTION=online for DEVPATH=/sys/devices/platform/visorchipset.
 *
 * Return: 0 on success, negative on failure
 */
static int chipset_ready_uevent(struct controlvm_message_header *msg_hdr)
{
	int res;

	res = kobject_uevent(&chipset_dev->acpi_device->dev.kobj, KOBJ_ONLINE);
	if (msg_hdr->flags.response_expected)
		controlvm_respond(msg_hdr, res, NULL);
	return res;
}

/*
 * chipset_selftest_uevent() - sends chipset_selftest action
 *
 * Send ACTION=online for DEVPATH=/sys/devices/platform/visorchipset.
 *
 * Return: 0 on success, negative on failure
 */
static int chipset_selftest_uevent(struct controlvm_message_header *msg_hdr)
{
	char env_selftest[20];
	char *envp[] = { env_selftest, NULL };
	int res;

	sprintf(env_selftest, "SPARSP_SELFTEST=%d", 1);
	res = kobject_uevent_env(&chipset_dev->acpi_device->dev.kobj,
				 KOBJ_CHANGE, envp);
	if (msg_hdr->flags.response_expected)
		controlvm_respond(msg_hdr, res, NULL);
	return res;
}

/*
 * chipset_notready_uevent() - sends chipset_notready action
 *
 * Send ACTION=offline for DEVPATH=/sys/devices/platform/visorchipset.
 *
 * Return: 0 on success, negative on failure
 */
static int chipset_notready_uevent(struct controlvm_message_header *msg_hdr)
{
	int res = kobject_uevent(&chipset_dev->acpi_device->dev.kobj,
				 KOBJ_OFFLINE);

	if (msg_hdr->flags.response_expected)
		controlvm_respond(msg_hdr, res, NULL);
	return res;
}

static int unisys_vmcall(unsigned long tuple, unsigned long param)
{
	int result = 0;
	unsigned int cpuid_eax, cpuid_ebx, cpuid_ecx, cpuid_edx;
	unsigned long reg_ebx;
	unsigned long reg_ecx;

	reg_ebx = param & 0xFFFFFFFF;
	reg_ecx = param >> 32;
	cpuid(0x00000001, &cpuid_eax, &cpuid_ebx, &cpuid_ecx, &cpuid_edx);
	if (!(cpuid_ecx & 0x80000000))
		return -EPERM;
	__asm__ __volatile__(".byte 0x00f, 0x001, 0x0c1" : "=a"(result) :
			     "a"(tuple), "b"(reg_ebx), "c"(reg_ecx));
	if (result)
		goto error;
	return 0;

/* Need to convert from VMCALL error codes to Linux */
error:
	switch (result) {
	case VMCALL_RESULT_INVALID_PARAM:
		return -EINVAL;
	case VMCALL_RESULT_DATA_UNAVAILABLE:
		return -ENODEV;
	default:
		return -EFAULT;
	}
}

static int controlvm_channel_create(struct visorchipset_device *dev)
{
	struct visorchannel *chan;
	u64 addr;
	int err;

	err = unisys_vmcall(VMCALL_CONTROLVM_ADDR,
			    virt_to_phys(&dev->controlvm_params));
	if (err)
		return err;
	addr = dev->controlvm_params.address;
	chan = visorchannel_create(addr, GFP_KERNEL,
				   &visor_controlvm_channel_guid, true);
	if (!chan)
		return -ENOMEM;
	dev->controlvm_channel = chan;
	return 0;
}

static void setup_crash_devices_work_queue(struct work_struct *work)
{
	struct controlvm_message local_crash_bus_msg;
	struct controlvm_message local_crash_dev_msg;
	struct controlvm_message msg;
	u32 local_crash_msg_offset;
	u16 local_crash_msg_count;

	/* send init chipset msg */
	msg.hdr.id = CONTROLVM_CHIPSET_INIT;
	msg.cmd.init_chipset.bus_count = 23;
	msg.cmd.init_chipset.switch_count = 0;
	chipset_init(&msg);
	/* get saved message count */
	if (visorchannel_read(chipset_dev->controlvm_channel,
			      offsetof(struct visor_controlvm_channel,
				       saved_crash_message_count),
			      &local_crash_msg_count, sizeof(u16)) < 0) {
		dev_err(&chipset_dev->acpi_device->dev,
			"failed to read channel\n");
		return;
	}
	if (local_crash_msg_count != CONTROLVM_CRASHMSG_MAX) {
		dev_err(&chipset_dev->acpi_device->dev, "invalid count\n");
		return;
	}
	/* get saved crash message offset */
	if (visorchannel_read(chipset_dev->controlvm_channel,
			      offsetof(struct visor_controlvm_channel,
				       saved_crash_message_offset),
			      &local_crash_msg_offset, sizeof(u32)) < 0) {
		dev_err(&chipset_dev->acpi_device->dev,
			"failed to read channel\n");
		return;
	}
	/* read create device message for storage bus offset */
	if (visorchannel_read(chipset_dev->controlvm_channel,
			      local_crash_msg_offset,
			      &local_crash_bus_msg,
			      sizeof(struct controlvm_message)) < 0) {
		dev_err(&chipset_dev->acpi_device->dev,
			"failed to read channel\n");
		return;
	}
	/* read create device message for storage device */
	if (visorchannel_read(chipset_dev->controlvm_channel,
			      local_crash_msg_offset +
			      sizeof(struct controlvm_message),
			      &local_crash_dev_msg,
			      sizeof(struct controlvm_message)) < 0) {
		dev_err(&chipset_dev->acpi_device->dev,
			"failed to read channel\n");
		return;
	}
	/* reuse IOVM create bus message */
	if (!local_crash_bus_msg.cmd.create_bus.channel_addr) {
		dev_err(&chipset_dev->acpi_device->dev,
			"no valid create_bus message\n");
		return;
	}
	visorbus_create(&local_crash_bus_msg);
	/* reuse create device message for storage device */
	if (!local_crash_dev_msg.cmd.create_device.channel_addr) {
		dev_err(&chipset_dev->acpi_device->dev,
			"no valid create_device message\n");
		return;
	}
	visorbus_device_create(&local_crash_dev_msg);
}

void visorbus_response(struct visor_device *bus_info, int response,
		       int controlvm_id)
{
	if (!bus_info->pending_msg_hdr)
		return;

	controlvm_responder(controlvm_id, bus_info->pending_msg_hdr, response);
	kfree(bus_info->pending_msg_hdr);
	bus_info->pending_msg_hdr = NULL;
}

void visorbus_device_changestate_response(struct visor_device *dev_info,
					  int response,
					  struct visor_segment_state state)
{
	if (!dev_info->pending_msg_hdr)
		return;

	device_changestate_responder(CONTROLVM_DEVICE_CHANGESTATE, dev_info,
				     response, state);
	kfree(dev_info->pending_msg_hdr);
	dev_info->pending_msg_hdr = NULL;
}

static void parser_done(struct parser_context *ctx)
{
	chipset_dev->controlvm_payload_bytes_buffered -= ctx->param_bytes;
	kfree(ctx);
}

static struct parser_context *parser_init_stream(u64 addr, u32 bytes,
						 bool *retry)
{
	unsigned long allocbytes;
	struct parser_context *ctx;
	void *mapping;

	*retry = false;
	/* alloc an extra byte to ensure payload is \0 terminated */
	allocbytes = (unsigned long)bytes + 1 + (sizeof(struct parser_context) -
		     sizeof(struct visor_controlvm_parameters_header));
	if ((chipset_dev->controlvm_payload_bytes_buffered + bytes) >
	     MAX_CONTROLVM_PAYLOAD_BYTES) {
		*retry = true;
		return NULL;
	}
	ctx = kzalloc(allocbytes, GFP_KERNEL);
	if (!ctx) {
		*retry = true;
		return NULL;
	}
	ctx->allocbytes = allocbytes;
	ctx->param_bytes = bytes;
	mapping = memremap(addr, bytes, MEMREMAP_WB);
	if (!mapping)
		goto err_finish_ctx;
	memcpy(&ctx->data, mapping, bytes);
	memunmap(mapping);
	ctx->byte_stream = true;
	chipset_dev->controlvm_payload_bytes_buffered += ctx->param_bytes;
	return ctx;

err_finish_ctx:
	kfree(ctx);
	return NULL;
}

/*
 * handle_command() - process a controlvm message
 * @inmsg:        the message to process
 * @channel_addr: address of the controlvm channel
 *
 * Return:
 *	0	- Successfully processed the message
 *	-EAGAIN - ControlVM message was not processed and should be retried
 *		  reading the next controlvm message; a scenario where this can
 *		  occur is when we need to throttle the allocation of memory in
 *		  which to copy out controlvm payload data.
 *	< 0	- error: ControlVM message was processed but an error occurred.
 */
static int handle_command(struct controlvm_message inmsg, u64 channel_addr)
{
	struct controlvm_message_packet *cmd = &inmsg.cmd;
	u64 parm_addr;
	u32 parm_bytes;
	struct parser_context *parser_ctx = NULL;
	struct controlvm_message ackmsg;
	int err = 0;

	/* create parsing context if necessary */
	parm_addr = channel_addr + inmsg.hdr.payload_vm_offset;
	parm_bytes = inmsg.hdr.payload_bytes;
	/*
	 * Parameter and channel addresses within test messages actually lie
	 * within our OS-controlled memory. We need to know that, because it
	 * makes a difference in how we compute the virtual address.
	 */
	if (parm_bytes) {
		bool retry;

		parser_ctx = parser_init_stream(parm_addr, parm_bytes, &retry);
		if (!parser_ctx && retry)
			return -EAGAIN;
	}
	controlvm_init_response(&ackmsg, &inmsg.hdr, CONTROLVM_RESP_SUCCESS);
	err = visorchannel_signalinsert(chipset_dev->controlvm_channel,
					CONTROLVM_QUEUE_ACK, &ackmsg);
	if (err)
		return err;
	switch (inmsg.hdr.id) {
	case CONTROLVM_CHIPSET_INIT:
		err = chipset_init(&inmsg);
		break;
	case CONTROLVM_BUS_CREATE:
		err = visorbus_create(&inmsg);
		break;
	case CONTROLVM_BUS_DESTROY:
		err = visorbus_destroy(&inmsg);
		break;
	case CONTROLVM_BUS_CONFIGURE:
		err = visorbus_configure(&inmsg, parser_ctx);
		break;
	case CONTROLVM_DEVICE_CREATE:
		err = visorbus_device_create(&inmsg);
		break;
	case CONTROLVM_DEVICE_CHANGESTATE:
		if (cmd->device_change_state.flags.phys_device) {
			err = parahotplug_process_message(&inmsg);
		} else {
			/*
			 * save the hdr and cmd structures for later use when
			 * sending back the response to Command
			 */
			err = visorbus_device_changestate(&inmsg);
			break;
		}
		break;
	case CONTROLVM_DEVICE_DESTROY:
		err = visorbus_device_destroy(&inmsg);
		break;
	case CONTROLVM_DEVICE_CONFIGURE:
		/* no op just send a respond that we passed */
		if (inmsg.hdr.flags.response_expected)
			controlvm_respond(&inmsg.hdr, CONTROLVM_RESP_SUCCESS,
					  NULL);
		break;
	case CONTROLVM_CHIPSET_READY:
		err = chipset_ready_uevent(&inmsg.hdr);
		break;
	case CONTROLVM_CHIPSET_SELFTEST:
		err = chipset_selftest_uevent(&inmsg.hdr);
		break;
	case CONTROLVM_CHIPSET_STOP:
		err = chipset_notready_uevent(&inmsg.hdr);
		break;
	default:
		err = -ENOMSG;
		if (inmsg.hdr.flags.response_expected)
			controlvm_respond(&inmsg.hdr,
					  -CONTROLVM_RESP_ID_UNKNOWN, NULL);
		break;
	}
	if (parser_ctx) {
		parser_done(parser_ctx);
		parser_ctx = NULL;
	}
	return err;
}

/*
 * read_controlvm_event() - retreives the next message from the
 *                          CONTROLVM_QUEUE_EVENT queue in the controlvm
 *                          channel
 * @msg: pointer to the retrieved message
 *
 * Return: 0 if valid message was retrieved or -error
 */
static int read_controlvm_event(struct controlvm_message *msg)
{
	int err = visorchannel_signalremove(chipset_dev->controlvm_channel,
					    CONTROLVM_QUEUE_EVENT, msg);

	if (err)
		return err;
	/* got a message */
	if (msg->hdr.flags.test_message == 1)
		return -EINVAL;
	return 0;
}

/*
 * parahotplug_process_list() - remove any request from the list that's been on
 *                              there too long and respond with an error
 */
static void parahotplug_process_list(void)
{
	struct list_head *pos;
	struct list_head *tmp;

	spin_lock(&parahotplug_request_list_lock);
	list_for_each_safe(pos, tmp, &parahotplug_request_list) {
		struct parahotplug_request *req =
		    list_entry(pos, struct parahotplug_request, list);

		if (!time_after_eq(jiffies, req->expiration))
			continue;
		list_del(pos);
		if (req->msg.hdr.flags.response_expected)
			controlvm_respond(
				&req->msg.hdr,
				CONTROLVM_RESP_DEVICE_UDEV_TIMEOUT,
				&req->msg.cmd.device_change_state.state);
		parahotplug_request_destroy(req);
	}
	spin_unlock(&parahotplug_request_list_lock);
}

static void controlvm_periodic_work(struct work_struct *work)
{
	struct controlvm_message inmsg;
	int count = 0;
	int err;

	/* Drain the RESPONSE queue make it empty */
	do {
		err = visorchannel_signalremove(chipset_dev->controlvm_channel,
						CONTROLVM_QUEUE_RESPONSE,
						&inmsg);
	} while ((!err) && (++count < CONTROLVM_MESSAGE_MAX));
	if (err != -EAGAIN)
		goto schedule_out;
	if (chipset_dev->controlvm_pending_msg_valid) {
		/*
		 * we throttled processing of a prior msg, so try to process
		 * it again rather than reading a new one
		 */
		inmsg = chipset_dev->controlvm_pending_msg;
		chipset_dev->controlvm_pending_msg_valid = false;
		err = 0;
	} else {
		err = read_controlvm_event(&inmsg);
	}
	while (!err) {
		chipset_dev->most_recent_message_jiffies = jiffies;
		err = handle_command(inmsg,
				     visorchannel_get_physaddr
				     (chipset_dev->controlvm_channel));
		if (err == -EAGAIN) {
			chipset_dev->controlvm_pending_msg = inmsg;
			chipset_dev->controlvm_pending_msg_valid = true;
			break;
		}

		err = read_controlvm_event(&inmsg);
	}
	/* parahotplug_worker */
	parahotplug_process_list();

/*
 * The controlvm messages are sent in a bulk. If we start receiving messages, we
 * want the polling to be fast. If we do not receive any message for
 * MIN_IDLE_SECONDS, we can slow down the polling.
 */
schedule_out:
	if (time_after(jiffies, chipset_dev->most_recent_message_jiffies +
				(HZ * MIN_IDLE_SECONDS))) {
		/*
		 * it's been longer than MIN_IDLE_SECONDS since we processed
		 * our last controlvm message; slow down the polling
		 */
		if (chipset_dev->poll_jiffies != POLLJIFFIES_CONTROLVM_SLOW)
			chipset_dev->poll_jiffies = POLLJIFFIES_CONTROLVM_SLOW;
	} else {
		if (chipset_dev->poll_jiffies != POLLJIFFIES_CONTROLVM_FAST)
			chipset_dev->poll_jiffies = POLLJIFFIES_CONTROLVM_FAST;
	}
	schedule_delayed_work(&chipset_dev->periodic_controlvm_work,
			      chipset_dev->poll_jiffies);
}

static int visorchipset_init(struct acpi_device *acpi_device)
{
	int err = -ENODEV;
	struct visorchannel *controlvm_channel;

	chipset_dev = kzalloc(sizeof(*chipset_dev), GFP_KERNEL);
	if (!chipset_dev)
		goto error;
	err = controlvm_channel_create(chipset_dev);
	if (err)
		goto error_free_chipset_dev;
	acpi_device->driver_data = chipset_dev;
	chipset_dev->acpi_device = acpi_device;
	chipset_dev->poll_jiffies = POLLJIFFIES_CONTROLVM_FAST;
	err = sysfs_create_groups(&chipset_dev->acpi_device->dev.kobj,
				  visorchipset_dev_groups);
	if (err < 0)
		goto error_destroy_channel;
	controlvm_channel = chipset_dev->controlvm_channel;
	if (!visor_check_channel(visorchannel_get_header(controlvm_channel),
				 &chipset_dev->acpi_device->dev,
				 &visor_controlvm_channel_guid,
				 "controlvm",
				 sizeof(struct visor_controlvm_channel),
				 VISOR_CONTROLVM_CHANNEL_VERSIONID,
				 VISOR_CHANNEL_SIGNATURE))
		goto error_delete_groups;
	/* if booting in a crash kernel */
	if (is_kdump_kernel())
		INIT_DELAYED_WORK(&chipset_dev->periodic_controlvm_work,
				  setup_crash_devices_work_queue);
	else
		INIT_DELAYED_WORK(&chipset_dev->periodic_controlvm_work,
				  controlvm_periodic_work);
	chipset_dev->most_recent_message_jiffies = jiffies;
	chipset_dev->poll_jiffies = POLLJIFFIES_CONTROLVM_FAST;
	schedule_delayed_work(&chipset_dev->periodic_controlvm_work,
			      chipset_dev->poll_jiffies);
	err = visorbus_init();
	if (err < 0)
		goto error_cancel_work;
	return 0;

error_cancel_work:
	cancel_delayed_work_sync(&chipset_dev->periodic_controlvm_work);

error_delete_groups:
	sysfs_remove_groups(&chipset_dev->acpi_device->dev.kobj,
			    visorchipset_dev_groups);

error_destroy_channel:
	visorchannel_destroy(chipset_dev->controlvm_channel);

error_free_chipset_dev:
	kfree(chipset_dev);

error:
	dev_err(&acpi_device->dev, "failed with error %d\n", err);
	return err;
}

static int visorchipset_exit(struct acpi_device *acpi_device)
{
	visorbus_exit();
	cancel_delayed_work_sync(&chipset_dev->periodic_controlvm_work);
	sysfs_remove_groups(&chipset_dev->acpi_device->dev.kobj,
			    visorchipset_dev_groups);
	visorchannel_destroy(chipset_dev->controlvm_channel);
	kfree(chipset_dev);
	return 0;
}

static const struct acpi_device_id unisys_device_ids[] = {
	{"PNP0A07", 0},
	{"", 0},
};

static struct acpi_driver unisys_acpi_driver = {
	.name = "unisys_acpi",
	.class = "unisys_acpi_class",
	.owner = THIS_MODULE,
	.ids = unisys_device_ids,
	.ops = {
		.add = visorchipset_init,
		.remove = visorchipset_exit,
	},
};

MODULE_DEVICE_TABLE(acpi, unisys_device_ids);

static __init int visorutil_spar_detect(void)
{
	unsigned int eax, ebx, ecx, edx;

	if (boot_cpu_has(X86_FEATURE_HYPERVISOR)) {
		/* check the ID */
		cpuid(UNISYS_VISOR_LEAF_ID, &eax, &ebx, &ecx, &edx);
		return  (ebx == UNISYS_VISOR_ID_EBX) &&
			(ecx == UNISYS_VISOR_ID_ECX) &&
			(edx == UNISYS_VISOR_ID_EDX);
	}
	return 0;
}

static int __init init_unisys(void)
{
	int result;

	if (!visorutil_spar_detect())
		return -ENODEV;
	result = acpi_bus_register_driver(&unisys_acpi_driver);
	if (result)
		return -ENODEV;
	pr_info("Unisys Visorchipset Driver Loaded.\n");
	return 0;
};

static void __exit exit_unisys(void)
{
	acpi_bus_unregister_driver(&unisys_acpi_driver);
}

module_init(init_unisys);
module_exit(exit_unisys);

MODULE_AUTHOR("Unisys");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("s-Par visorbus driver for virtual device buses");
