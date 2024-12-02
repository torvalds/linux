// SPDX-License-Identifier: GPL-2.0-only
/*
 *  PS3 System Manager.
 *
 *  Copyright (C) 2007 Sony Computer Entertainment Inc.
 *  Copyright 2007 Sony Corp.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/reboot.h>
#include <linux/sched/signal.h>

#include <asm/firmware.h>
#include <asm/lv1call.h>
#include <asm/ps3.h>

#include "vuart.h"

/**
 * ps3_sys_manager - PS3 system manager driver.
 *
 * The system manager provides an asynchronous system event notification
 * mechanism for reporting events like thermal alert and button presses to
 * guests.  It also provides support to control system shutdown and startup.
 *
 * The actual system manager is implemented as an application running in the
 * system policy module in lpar_1.  Guests communicate with the system manager
 * through port 2 of the vuart using a simple packet message protocol.
 * Messages are comprised of a fixed field header followed by a message
 * specific payload.
 */

/**
 * struct ps3_sys_manager_header - System manager message header.
 * @version: Header version, currently 1.
 * @size: Header size in bytes, currently 16.
 * @payload_size: Message payload size in bytes.
 * @service_id: Message type, one of enum ps3_sys_manager_service_id.
 * @request_tag: Unique number to identify reply.
 */

struct ps3_sys_manager_header {
	/* version 1 */
	u8 version;
	u8 size;
	u16 reserved_1;
	u32 payload_size;
	u16 service_id;
	u16 reserved_2;
	u32 request_tag;
};

#define dump_sm_header(_h) _dump_sm_header(_h, __func__, __LINE__)
static void __maybe_unused _dump_sm_header(
	const struct ps3_sys_manager_header *h, const char *func, int line)
{
	pr_debug("%s:%d: version:      %xh\n", func, line, h->version);
	pr_debug("%s:%d: size:         %xh\n", func, line, h->size);
	pr_debug("%s:%d: payload_size: %xh\n", func, line, h->payload_size);
	pr_debug("%s:%d: service_id:   %xh\n", func, line, h->service_id);
	pr_debug("%s:%d: request_tag:  %xh\n", func, line, h->request_tag);
}

/**
 * @PS3_SM_RX_MSG_LEN_MIN - Shortest received message length.
 * @PS3_SM_RX_MSG_LEN_MAX - Longest received message length.
 *
 * Currently all messages received from the system manager are either
 * (16 bytes header + 8 bytes payload = 24 bytes) or (16 bytes header
 * + 16 bytes payload = 32 bytes).  This knowledge is used to simplify
 * the logic.
 */

enum {
	PS3_SM_RX_MSG_LEN_MIN = 24,
	PS3_SM_RX_MSG_LEN_MAX = 32,
};

/**
 * enum ps3_sys_manager_service_id - Message header service_id.
 * @PS3_SM_SERVICE_ID_REQUEST:       guest --> sys_manager.
 * @PS3_SM_SERVICE_ID_REQUEST_ERROR: guest <-- sys_manager.
 * @PS3_SM_SERVICE_ID_COMMAND:       guest <-- sys_manager.
 * @PS3_SM_SERVICE_ID_RESPONSE:      guest --> sys_manager.
 * @PS3_SM_SERVICE_ID_SET_ATTR:      guest --> sys_manager.
 * @PS3_SM_SERVICE_ID_EXTERN_EVENT:  guest <-- sys_manager.
 * @PS3_SM_SERVICE_ID_SET_NEXT_OP:   guest --> sys_manager.
 *
 * PS3_SM_SERVICE_ID_REQUEST_ERROR is returned for invalid data values in a
 * a PS3_SM_SERVICE_ID_REQUEST message.  It also seems to be returned when
 * a REQUEST message is sent at the wrong time.
 */

enum ps3_sys_manager_service_id {
	/* version 1 */
	PS3_SM_SERVICE_ID_REQUEST = 1,
	PS3_SM_SERVICE_ID_RESPONSE = 2,
	PS3_SM_SERVICE_ID_COMMAND = 3,
	PS3_SM_SERVICE_ID_EXTERN_EVENT = 4,
	PS3_SM_SERVICE_ID_SET_NEXT_OP = 5,
	PS3_SM_SERVICE_ID_REQUEST_ERROR = 6,
	PS3_SM_SERVICE_ID_SET_ATTR = 8,
};

/**
 * enum ps3_sys_manager_attr - Notification attribute (bit position mask).
 * @PS3_SM_ATTR_POWER: Power button.
 * @PS3_SM_ATTR_RESET: Reset button, not available on retail console.
 * @PS3_SM_ATTR_THERMAL: System thermal alert.
 * @PS3_SM_ATTR_CONTROLLER: Remote controller event.
 * @PS3_SM_ATTR_ALL: Logical OR of all.
 *
 * The guest tells the system manager which events it is interested in receiving
 * notice of by sending the system manager a logical OR of notification
 * attributes via the ps3_sys_manager_send_attr() routine.
 */

enum ps3_sys_manager_attr {
	/* version 1 */
	PS3_SM_ATTR_POWER = 1,
	PS3_SM_ATTR_RESET = 2,
	PS3_SM_ATTR_THERMAL = 4,
	PS3_SM_ATTR_CONTROLLER = 8, /* bogus? */
	PS3_SM_ATTR_ALL = 0x0f,
};

/**
 * enum ps3_sys_manager_event - External event type, reported by system manager.
 * @PS3_SM_EVENT_POWER_PRESSED: payload.value =
 *  enum ps3_sys_manager_button_event.
 * @PS3_SM_EVENT_POWER_RELEASED: payload.value = time pressed in millisec.
 * @PS3_SM_EVENT_RESET_PRESSED: payload.value =
 *  enum ps3_sys_manager_button_event.
 * @PS3_SM_EVENT_RESET_RELEASED: payload.value = time pressed in millisec.
 * @PS3_SM_EVENT_THERMAL_ALERT: payload.value = thermal zone id.
 * @PS3_SM_EVENT_THERMAL_CLEARED: payload.value = thermal zone id.
 */

enum ps3_sys_manager_event {
	/* version 1 */
	PS3_SM_EVENT_POWER_PRESSED = 3,
	PS3_SM_EVENT_POWER_RELEASED = 4,
	PS3_SM_EVENT_RESET_PRESSED = 5,
	PS3_SM_EVENT_RESET_RELEASED = 6,
	PS3_SM_EVENT_THERMAL_ALERT = 7,
	PS3_SM_EVENT_THERMAL_CLEARED = 8,
	/* no info on controller events */
};

/**
 * enum ps3_sys_manager_button_event - Button event payload values.
 * @PS3_SM_BUTTON_EVENT_HARD: Hardware generated event.
 * @PS3_SM_BUTTON_EVENT_SOFT: Software generated event.
 */

enum ps3_sys_manager_button_event {
	PS3_SM_BUTTON_EVENT_HARD = 0,
	PS3_SM_BUTTON_EVENT_SOFT = 1,
};

/**
 * enum ps3_sys_manager_next_op - Operation to perform after lpar is destroyed.
 */

enum ps3_sys_manager_next_op {
	/* version 3 */
	PS3_SM_NEXT_OP_SYS_SHUTDOWN = 1,
	PS3_SM_NEXT_OP_SYS_REBOOT = 2,
	PS3_SM_NEXT_OP_LPAR_REBOOT = 0x82,
};

/**
 * enum ps3_sys_manager_wake_source - Next-op wakeup source (bit position mask).
 * @PS3_SM_WAKE_DEFAULT: Disk insert, power button, eject button.
 * @PS3_SM_WAKE_W_O_L: Ether or wireless LAN.
 * @PS3_SM_WAKE_P_O_R: Power on reset.
 *
 * Additional wakeup sources when specifying PS3_SM_NEXT_OP_SYS_SHUTDOWN.
 * The system will always wake from the PS3_SM_WAKE_DEFAULT sources.
 * Sources listed here are the only ones available to guests in the
 * other-os lpar.
 */

enum ps3_sys_manager_wake_source {
	/* version 3 */
	PS3_SM_WAKE_DEFAULT   = 0,
	PS3_SM_WAKE_W_O_L     = 0x00000400,
	PS3_SM_WAKE_P_O_R     = 0x80000000,
};

/**
 * user_wake_sources - User specified wakeup sources.
 *
 * Logical OR of enum ps3_sys_manager_wake_source types.
 */

static u32 user_wake_sources = PS3_SM_WAKE_DEFAULT;

/**
 * enum ps3_sys_manager_cmd - Command from system manager to guest.
 *
 * The guest completes the actions needed, then acks or naks the command via
 * ps3_sys_manager_send_response().  In the case of @PS3_SM_CMD_SHUTDOWN,
 * the guest must be fully prepared for a system poweroff prior to acking the
 * command.
 */

enum ps3_sys_manager_cmd {
	/* version 1 */
	PS3_SM_CMD_SHUTDOWN = 1, /* shutdown guest OS */
};

/**
 * ps3_sm_force_power_off - Poweroff helper.
 *
 * A global variable used to force a poweroff when the power button has
 * been pressed irrespective of how init handles the ctrl_alt_del signal.
 *
 */

static unsigned int ps3_sm_force_power_off;

/**
 * ps3_sys_manager_write - Helper to write a two part message to the vuart.
 *
 */

static int ps3_sys_manager_write(struct ps3_system_bus_device *dev,
	const struct ps3_sys_manager_header *header, const void *payload)
{
	int result;

	BUG_ON(header->version != 1);
	BUG_ON(header->size != 16);
	BUG_ON(header->payload_size != 8 && header->payload_size != 16);
	BUG_ON(header->service_id > 8);

	result = ps3_vuart_write(dev, header,
		sizeof(struct ps3_sys_manager_header));

	if (!result)
		result = ps3_vuart_write(dev, payload, header->payload_size);

	return result;
}

/**
 * ps3_sys_manager_send_attr - Send a 'set attribute' to the system manager.
 *
 */

static int ps3_sys_manager_send_attr(struct ps3_system_bus_device *dev,
	enum ps3_sys_manager_attr attr)
{
	struct ps3_sys_manager_header header;
	struct {
		u8 version;
		u8 reserved_1[3];
		u32 attribute;
	} payload;

	BUILD_BUG_ON(sizeof(payload) != 8);

	dev_dbg(&dev->core, "%s:%d: %xh\n", __func__, __LINE__, attr);

	memset(&header, 0, sizeof(header));
	header.version = 1;
	header.size = 16;
	header.payload_size = 16;
	header.service_id = PS3_SM_SERVICE_ID_SET_ATTR;

	memset(&payload, 0, sizeof(payload));
	payload.version = 1;
	payload.attribute = attr;

	return ps3_sys_manager_write(dev, &header, &payload);
}

/**
 * ps3_sys_manager_send_next_op - Send a 'set next op' to the system manager.
 *
 * Tell the system manager what to do after this lpar is destroyed.
 */

static int ps3_sys_manager_send_next_op(struct ps3_system_bus_device *dev,
	enum ps3_sys_manager_next_op op,
	enum ps3_sys_manager_wake_source wake_source)
{
	struct ps3_sys_manager_header header;
	struct {
		u8 version;
		u8 type;
		u8 gos_id;
		u8 reserved_1;
		u32 wake_source;
		u8 reserved_2[8];
	} payload;

	BUILD_BUG_ON(sizeof(payload) != 16);

	dev_dbg(&dev->core, "%s:%d: (%xh)\n", __func__, __LINE__, op);

	memset(&header, 0, sizeof(header));
	header.version = 1;
	header.size = 16;
	header.payload_size = 16;
	header.service_id = PS3_SM_SERVICE_ID_SET_NEXT_OP;

	memset(&payload, 0, sizeof(payload));
	payload.version = 3;
	payload.type = op;
	payload.gos_id = 3; /* other os */
	payload.wake_source = wake_source;

	return ps3_sys_manager_write(dev, &header, &payload);
}

/**
 * ps3_sys_manager_send_request_shutdown - Send 'request' to the system manager.
 *
 * The guest sends this message to request an operation or action of the system
 * manager.  The reply is a command message from the system manager.  In the
 * command handler the guest performs the requested operation.  The result of
 * the command is then communicated back to the system manager with a response
 * message.
 *
 * Currently, the only supported request is the 'shutdown self' request.
 */

static int ps3_sys_manager_send_request_shutdown(
	struct ps3_system_bus_device *dev)
{
	struct ps3_sys_manager_header header;
	struct {
		u8 version;
		u8 type;
		u8 gos_id;
		u8 reserved_1[13];
	} payload;

	BUILD_BUG_ON(sizeof(payload) != 16);

	dev_dbg(&dev->core, "%s:%d\n", __func__, __LINE__);

	memset(&header, 0, sizeof(header));
	header.version = 1;
	header.size = 16;
	header.payload_size = 16;
	header.service_id = PS3_SM_SERVICE_ID_REQUEST;

	memset(&payload, 0, sizeof(payload));
	payload.version = 1;
	payload.type = 1; /* shutdown */
	payload.gos_id = 0; /* self */

	return ps3_sys_manager_write(dev, &header, &payload);
}

/**
 * ps3_sys_manager_send_response - Send a 'response' to the system manager.
 * @status: zero = success, others fail.
 *
 * The guest sends this message to the system manager to acnowledge success or
 * failure of a command sent by the system manager.
 */

static int ps3_sys_manager_send_response(struct ps3_system_bus_device *dev,
	u64 status)
{
	struct ps3_sys_manager_header header;
	struct {
		u8 version;
		u8 reserved_1[3];
		u8 status;
		u8 reserved_2[11];
	} payload;

	BUILD_BUG_ON(sizeof(payload) != 16);

	dev_dbg(&dev->core, "%s:%d: (%s)\n", __func__, __LINE__,
		(status ? "nak" : "ack"));

	memset(&header, 0, sizeof(header));
	header.version = 1;
	header.size = 16;
	header.payload_size = 16;
	header.service_id = PS3_SM_SERVICE_ID_RESPONSE;

	memset(&payload, 0, sizeof(payload));
	payload.version = 1;
	payload.status = status;

	return ps3_sys_manager_write(dev, &header, &payload);
}

/**
 * ps3_sys_manager_handle_event - Second stage event msg handler.
 *
 */

static int ps3_sys_manager_handle_event(struct ps3_system_bus_device *dev)
{
	int result;
	struct {
		u8 version;
		u8 type;
		u8 reserved_1[2];
		u32 value;
		u8 reserved_2[8];
	} event;

	BUILD_BUG_ON(sizeof(event) != 16);

	result = ps3_vuart_read(dev, &event, sizeof(event));
	BUG_ON(result && "need to retry here");

	if (event.version != 1) {
		dev_dbg(&dev->core, "%s:%d: unsupported event version (%u)\n",
			__func__, __LINE__, event.version);
		return -EIO;
	}

	switch (event.type) {
	case PS3_SM_EVENT_POWER_PRESSED:
		dev_dbg(&dev->core, "%s:%d: POWER_PRESSED (%s)\n",
			__func__, __LINE__,
			(event.value == PS3_SM_BUTTON_EVENT_SOFT ? "soft"
			: "hard"));
		ps3_sm_force_power_off = 1;
		/*
		 * A memory barrier is use here to sync memory since
		 * ps3_sys_manager_final_restart() could be called on
		 * another cpu.
		 */
		wmb();
		kill_cad_pid(SIGINT, 1); /* ctrl_alt_del */
		break;
	case PS3_SM_EVENT_POWER_RELEASED:
		dev_dbg(&dev->core, "%s:%d: POWER_RELEASED (%u ms)\n",
			__func__, __LINE__, event.value);
		break;
	case PS3_SM_EVENT_RESET_PRESSED:
		dev_dbg(&dev->core, "%s:%d: RESET_PRESSED (%s)\n",
			__func__, __LINE__,
			(event.value == PS3_SM_BUTTON_EVENT_SOFT ? "soft"
			: "hard"));
		ps3_sm_force_power_off = 0;
		/*
		 * A memory barrier is use here to sync memory since
		 * ps3_sys_manager_final_restart() could be called on
		 * another cpu.
		 */
		wmb();
		kill_cad_pid(SIGINT, 1); /* ctrl_alt_del */
		break;
	case PS3_SM_EVENT_RESET_RELEASED:
		dev_dbg(&dev->core, "%s:%d: RESET_RELEASED (%u ms)\n",
			__func__, __LINE__, event.value);
		break;
	case PS3_SM_EVENT_THERMAL_ALERT:
		dev_dbg(&dev->core, "%s:%d: THERMAL_ALERT (zone %u)\n",
			__func__, __LINE__, event.value);
		pr_info("PS3 Thermal Alert Zone %u\n", event.value);
		break;
	case PS3_SM_EVENT_THERMAL_CLEARED:
		dev_dbg(&dev->core, "%s:%d: THERMAL_CLEARED (zone %u)\n",
			__func__, __LINE__, event.value);
		break;
	default:
		dev_dbg(&dev->core, "%s:%d: unknown event (%u)\n",
			__func__, __LINE__, event.type);
		return -EIO;
	}

	return 0;
}
/**
 * ps3_sys_manager_handle_cmd - Second stage command msg handler.
 *
 * The system manager sends this in reply to a 'request' message from the guest.
 */

static int ps3_sys_manager_handle_cmd(struct ps3_system_bus_device *dev)
{
	int result;
	struct {
		u8 version;
		u8 type;
		u8 reserved_1[14];
	} cmd;

	BUILD_BUG_ON(sizeof(cmd) != 16);

	dev_dbg(&dev->core, "%s:%d\n", __func__, __LINE__);

	result = ps3_vuart_read(dev, &cmd, sizeof(cmd));
	BUG_ON(result && "need to retry here");

	if (result)
		return result;

	if (cmd.version != 1) {
		dev_dbg(&dev->core, "%s:%d: unsupported cmd version (%u)\n",
			__func__, __LINE__, cmd.version);
		return -EIO;
	}

	if (cmd.type != PS3_SM_CMD_SHUTDOWN) {
		dev_dbg(&dev->core, "%s:%d: unknown cmd (%u)\n",
			__func__, __LINE__, cmd.type);
		return -EIO;
	}

	ps3_sys_manager_send_response(dev, 0);
	return 0;
}

/**
 * ps3_sys_manager_handle_msg - First stage msg handler.
 *
 * Can be called directly to manually poll vuart and pump message handler.
 */

static int ps3_sys_manager_handle_msg(struct ps3_system_bus_device *dev)
{
	int result;
	struct ps3_sys_manager_header header;

	result = ps3_vuart_read(dev, &header,
		sizeof(struct ps3_sys_manager_header));

	if (result)
		return result;

	if (header.version != 1) {
		dev_dbg(&dev->core, "%s:%d: unsupported header version (%u)\n",
			__func__, __LINE__, header.version);
		dump_sm_header(&header);
		goto fail_header;
	}

	BUILD_BUG_ON(sizeof(header) != 16);

	if (header.size != 16 || (header.payload_size != 8
		&& header.payload_size != 16)) {
		dump_sm_header(&header);
		BUG();
	}

	switch (header.service_id) {
	case PS3_SM_SERVICE_ID_EXTERN_EVENT:
		dev_dbg(&dev->core, "%s:%d: EVENT\n", __func__, __LINE__);
		return ps3_sys_manager_handle_event(dev);
	case PS3_SM_SERVICE_ID_COMMAND:
		dev_dbg(&dev->core, "%s:%d: COMMAND\n", __func__, __LINE__);
		return ps3_sys_manager_handle_cmd(dev);
	case PS3_SM_SERVICE_ID_REQUEST_ERROR:
		dev_dbg(&dev->core, "%s:%d: REQUEST_ERROR\n", __func__,
			__LINE__);
		dump_sm_header(&header);
		break;
	default:
		dev_dbg(&dev->core, "%s:%d: unknown service_id (%u)\n",
			__func__, __LINE__, header.service_id);
		break;
	}
	goto fail_id;

fail_header:
	ps3_vuart_clear_rx_bytes(dev, 0);
	return -EIO;
fail_id:
	ps3_vuart_clear_rx_bytes(dev, header.payload_size);
	return -EIO;
}

static void ps3_sys_manager_fin(struct ps3_system_bus_device *dev)
{
	ps3_sys_manager_send_request_shutdown(dev);

	pr_emerg("System Halted, OK to turn off power\n");

	while (ps3_sys_manager_handle_msg(dev)) {
		/* pause until next DEC interrupt */
		lv1_pause(0);
	}

	while (1) {
		/* pause, ignoring DEC interrupt */
		lv1_pause(1);
	}
}

/**
 * ps3_sys_manager_final_power_off - The final platform machine_power_off routine.
 *
 * This routine never returns.  The routine disables asynchronous vuart reads
 * then spins calling ps3_sys_manager_handle_msg() to receive and acknowledge
 * the shutdown command sent from the system manager.  Soon after the
 * acknowledgement is sent the lpar is destroyed by the HV.  This routine
 * should only be called from ps3_power_off() through
 * ps3_sys_manager_ops.power_off.
 */

static void ps3_sys_manager_final_power_off(struct ps3_system_bus_device *dev)
{
	BUG_ON(!dev);

	dev_dbg(&dev->core, "%s:%d\n", __func__, __LINE__);

	ps3_vuart_cancel_async(dev);

	ps3_sys_manager_send_next_op(dev, PS3_SM_NEXT_OP_SYS_SHUTDOWN,
		user_wake_sources);

	ps3_sys_manager_fin(dev);
}

/**
 * ps3_sys_manager_final_restart - The final platform machine_restart routine.
 *
 * This routine never returns.  The routine disables asynchronous vuart reads
 * then spins calling ps3_sys_manager_handle_msg() to receive and acknowledge
 * the shutdown command sent from the system manager.  Soon after the
 * acknowledgement is sent the lpar is destroyed by the HV.  This routine
 * should only be called from ps3_restart() through ps3_sys_manager_ops.restart.
 */

static void ps3_sys_manager_final_restart(struct ps3_system_bus_device *dev)
{
	BUG_ON(!dev);

	dev_dbg(&dev->core, "%s:%d\n", __func__, __LINE__);

	/* Check if we got here via a power button event. */

	if (ps3_sm_force_power_off) {
		dev_dbg(&dev->core, "%s:%d: forcing poweroff\n",
			__func__, __LINE__);
		ps3_sys_manager_final_power_off(dev);
	}

	ps3_vuart_cancel_async(dev);

	ps3_sys_manager_send_attr(dev, 0);
	ps3_sys_manager_send_next_op(dev, PS3_SM_NEXT_OP_SYS_REBOOT,
		user_wake_sources);

	ps3_sys_manager_fin(dev);
}

/**
 * ps3_sys_manager_get_wol - Get wake-on-lan setting.
 */

int ps3_sys_manager_get_wol(void)
{
	pr_debug("%s:%d\n", __func__, __LINE__);

	return (user_wake_sources & PS3_SM_WAKE_W_O_L) != 0;
}
EXPORT_SYMBOL_GPL(ps3_sys_manager_get_wol);

/**
 * ps3_sys_manager_set_wol - Set wake-on-lan setting.
 */

void ps3_sys_manager_set_wol(int state)
{
	static DEFINE_MUTEX(mutex);

	mutex_lock(&mutex);

	pr_debug("%s:%d: %d\n", __func__, __LINE__, state);

	if (state)
		user_wake_sources |= PS3_SM_WAKE_W_O_L;
	else
		user_wake_sources &= ~PS3_SM_WAKE_W_O_L;
	mutex_unlock(&mutex);
}
EXPORT_SYMBOL_GPL(ps3_sys_manager_set_wol);

/**
 * ps3_sys_manager_work - Asynchronous read handler.
 *
 * Signaled when PS3_SM_RX_MSG_LEN_MIN bytes arrive at the vuart port.
 */

static void ps3_sys_manager_work(struct ps3_system_bus_device *dev)
{
	ps3_sys_manager_handle_msg(dev);
	ps3_vuart_read_async(dev, PS3_SM_RX_MSG_LEN_MIN);
}

static int ps3_sys_manager_probe(struct ps3_system_bus_device *dev)
{
	int result;
	struct ps3_sys_manager_ops ops;

	dev_dbg(&dev->core, "%s:%d\n", __func__, __LINE__);

	ops.power_off = ps3_sys_manager_final_power_off;
	ops.restart = ps3_sys_manager_final_restart;
	ops.dev = dev;

	/* ps3_sys_manager_register_ops copies ops. */

	ps3_sys_manager_register_ops(&ops);

	result = ps3_sys_manager_send_attr(dev, PS3_SM_ATTR_ALL);
	BUG_ON(result);

	result = ps3_vuart_read_async(dev, PS3_SM_RX_MSG_LEN_MIN);
	BUG_ON(result);

	return result;
}

static int ps3_sys_manager_remove(struct ps3_system_bus_device *dev)
{
	dev_dbg(&dev->core, "%s:%d\n", __func__, __LINE__);
	return 0;
}

static void ps3_sys_manager_shutdown(struct ps3_system_bus_device *dev)
{
	dev_dbg(&dev->core, "%s:%d\n", __func__, __LINE__);
}

static struct ps3_vuart_port_driver ps3_sys_manager = {
	.core.match_id = PS3_MATCH_ID_SYSTEM_MANAGER,
	.core.core.name = "ps3_sys_manager",
	.probe = ps3_sys_manager_probe,
	.remove = ps3_sys_manager_remove,
	.shutdown = ps3_sys_manager_shutdown,
	.work = ps3_sys_manager_work,
};

static int __init ps3_sys_manager_init(void)
{
	if (!firmware_has_feature(FW_FEATURE_PS3_LV1))
		return -ENODEV;

	return ps3_vuart_port_driver_register(&ps3_sys_manager);
}

module_init(ps3_sys_manager_init);
/* Module remove not supported. */

MODULE_AUTHOR("Sony Corporation");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("PS3 System Manager");
MODULE_ALIAS(PS3_MODULE_ALIAS_SYSTEM_MANAGER);
