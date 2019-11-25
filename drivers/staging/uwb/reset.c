// SPDX-License-Identifier: GPL-2.0-only
/*
 * Ultra Wide Band
 * UWB basic command support and radio reset
 *
 * Copyright (C) 2005-2006 Intel Corporation
 * Inaky Perez-Gonzalez <inaky.perez-gonzalez@intel.com>
 *
 * FIXME:
 *
 *  - docs
 *
 *  - Now we are serializing (using the uwb_dev->mutex) the command
 *    execution; it should be parallelized as much as possible some
 *    day.
 */
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/export.h>

#include "uwb-internal.h"

/**
 * Command result codes (WUSB1.0[T8-69])
 */
static
const char *__strerror[] = {
	"success",
	"failure",
	"hardware failure",
	"no more slots",
	"beacon is too large",
	"invalid parameter",
	"unsupported power level",
	"time out (wa) or invalid ie data (whci)",
	"beacon size exceeded",
	"cancelled",
	"invalid state",
	"invalid size",
	"ack not received",
	"no more asie notification",
};


/** Return a string matching the given error code */
const char *uwb_rc_strerror(unsigned code)
{
	if (code == 255)
		return "time out";
	if (code >= ARRAY_SIZE(__strerror))
		return "unknown error";
	return __strerror[code];
}

int uwb_rc_cmd_async(struct uwb_rc *rc, const char *cmd_name,
		     struct uwb_rccb *cmd, size_t cmd_size,
		     u8 expected_type, u16 expected_event,
		     uwb_rc_cmd_cb_f cb, void *arg)
{
	struct device *dev = &rc->uwb_dev.dev;
	struct uwb_rc_neh *neh;
	int needtofree = 0;
	int result;

	uwb_dev_lock(&rc->uwb_dev);	/* Protect against rc->priv being removed */
	if (rc->priv == NULL) {
		uwb_dev_unlock(&rc->uwb_dev);
		return -ESHUTDOWN;
	}

	if (rc->filter_cmd) {
		needtofree = rc->filter_cmd(rc, &cmd, &cmd_size);
		if (needtofree < 0 && needtofree != -ENOANO) {
			dev_err(dev, "%s: filter error: %d\n",
				cmd_name, needtofree);
			uwb_dev_unlock(&rc->uwb_dev);
			return needtofree;
		}
	}

	neh = uwb_rc_neh_add(rc, cmd, expected_type, expected_event, cb, arg);
	if (IS_ERR(neh)) {
		result = PTR_ERR(neh);
		uwb_dev_unlock(&rc->uwb_dev);
		goto out;
	}

	result = rc->cmd(rc, cmd, cmd_size);
	uwb_dev_unlock(&rc->uwb_dev);
	if (result < 0)
		uwb_rc_neh_rm(rc, neh);
	else
		uwb_rc_neh_arm(rc, neh);
	uwb_rc_neh_put(neh);
out:
	if (needtofree == 1)
		kfree(cmd);
	return result < 0 ? result : 0;
}
EXPORT_SYMBOL_GPL(uwb_rc_cmd_async);

struct uwb_rc_cmd_done_params {
	struct completion completion;
	struct uwb_rceb *reply;
	ssize_t reply_size;
};

static void uwb_rc_cmd_done(struct uwb_rc *rc, void *arg,
			    struct uwb_rceb *reply, ssize_t reply_size)
{
	struct uwb_rc_cmd_done_params *p = (struct uwb_rc_cmd_done_params *)arg;

	if (reply_size > 0) {
		if (p->reply)
			reply_size = min(p->reply_size, reply_size);
		else
			p->reply = kmalloc(reply_size, GFP_ATOMIC);

		if (p->reply)
			memcpy(p->reply, reply, reply_size);
		else
			reply_size = -ENOMEM;
	}
	p->reply_size = reply_size;
	complete(&p->completion);
}


/**
 * Generic function for issuing commands to the Radio Control Interface
 *
 * @rc:       UWB Radio Control descriptor
 * @cmd_name: Name of the command being issued (for error messages)
 * @cmd:      Pointer to rccb structure containing the command;
 *            normally you embed this structure as the first member of
 *            the full command structure.
 * @cmd_size: Size of the whole command buffer pointed to by @cmd.
 * @reply:    Pointer to where to store the reply
 * @reply_size: @reply's size
 * @expected_type: Expected type in the return event
 * @expected_event: Expected event code in the return event
 * @preply:   Here a pointer to where the event data is received will
 *            be stored. Once done with the data, free with kfree().
 *
 * This function is generic; it works for commands that return a fixed
 * and known size or for commands that return a variable amount of data.
 *
 * If a buffer is provided, that is used, although it could be chopped
 * to the maximum size of the buffer. If the buffer is NULL, then one
 * be allocated in *preply with the whole contents of the reply.
 *
 * @rc needs to be referenced
 */
static
ssize_t __uwb_rc_cmd(struct uwb_rc *rc, const char *cmd_name,
		     struct uwb_rccb *cmd, size_t cmd_size,
		     struct uwb_rceb *reply, size_t reply_size,
		     u8 expected_type, u16 expected_event,
		     struct uwb_rceb **preply)
{
	ssize_t result = 0;
	struct device *dev = &rc->uwb_dev.dev;
	struct uwb_rc_cmd_done_params params;

	init_completion(&params.completion);
	params.reply = reply;
	params.reply_size = reply_size;

	result = uwb_rc_cmd_async(rc, cmd_name, cmd, cmd_size,
				  expected_type, expected_event,
				  uwb_rc_cmd_done, &params);
	if (result)
		return result;

	wait_for_completion(&params.completion);

	if (preply)
		*preply = params.reply;

	if (params.reply_size < 0)
		dev_err(dev, "%s: confirmation event 0x%02x/%04x/%02x "
			"reception failed: %d\n", cmd_name,
			expected_type, expected_event, cmd->bCommandContext,
			(int)params.reply_size);
	return params.reply_size;
}


/**
 * Generic function for issuing commands to the Radio Control Interface
 *
 * @rc:       UWB Radio Control descriptor
 * @cmd_name: Name of the command being issued (for error messages)
 * @cmd:      Pointer to rccb structure containing the command;
 *            normally you embed this structure as the first member of
 *            the full command structure.
 * @cmd_size: Size of the whole command buffer pointed to by @cmd.
 * @reply:    Pointer to the beginning of the confirmation event
 *            buffer. Normally bigger than an 'struct hwarc_rceb'.
 *            You need to fill out reply->bEventType and reply->wEvent (in
 *            cpu order) as the function will use them to verify the
 *            confirmation event.
 * @reply_size: Size of the reply buffer
 *
 * The function checks that the length returned in the reply is at
 * least as big as @reply_size; if not, it will be deemed an error and
 * -EIO returned.
 *
 * @rc needs to be referenced
 */
ssize_t uwb_rc_cmd(struct uwb_rc *rc, const char *cmd_name,
		   struct uwb_rccb *cmd, size_t cmd_size,
		   struct uwb_rceb *reply, size_t reply_size)
{
	struct device *dev = &rc->uwb_dev.dev;
	ssize_t result;

	result = __uwb_rc_cmd(rc, cmd_name,
			      cmd, cmd_size, reply, reply_size,
			      reply->bEventType, reply->wEvent, NULL);

	if (result > 0 && result < reply_size) {
		dev_err(dev, "%s: not enough data returned for decoding reply "
			"(%zu bytes received vs at least %zu needed)\n",
			cmd_name, result, reply_size);
		result = -EIO;
	}
	return result;
}
EXPORT_SYMBOL_GPL(uwb_rc_cmd);


/**
 * Generic function for issuing commands to the Radio Control
 * Interface that return an unknown amount of data
 *
 * @rc:       UWB Radio Control descriptor
 * @cmd_name: Name of the command being issued (for error messages)
 * @cmd:      Pointer to rccb structure containing the command;
 *            normally you embed this structure as the first member of
 *            the full command structure.
 * @cmd_size: Size of the whole command buffer pointed to by @cmd.
 * @expected_type: Expected type in the return event
 * @expected_event: Expected event code in the return event
 * @preply:   Here a pointer to where the event data is received will
 *            be stored. Once done with the data, free with kfree().
 *
 * The function checks that the length returned in the reply is at
 * least as big as a 'struct uwb_rceb *'; if not, it will be deemed an
 * error and -EIO returned.
 *
 * @rc needs to be referenced
 */
ssize_t uwb_rc_vcmd(struct uwb_rc *rc, const char *cmd_name,
		    struct uwb_rccb *cmd, size_t cmd_size,
		    u8 expected_type, u16 expected_event,
		    struct uwb_rceb **preply)
{
	return __uwb_rc_cmd(rc, cmd_name, cmd, cmd_size, NULL, 0,
			    expected_type, expected_event, preply);
}
EXPORT_SYMBOL_GPL(uwb_rc_vcmd);


/**
 * Reset a UWB Host Controller (and all radio settings)
 *
 * @rc:      Host Controller descriptor
 * @returns: 0 if ok, < 0 errno code on error
 *
 * We put the command on kmalloc'ed memory as some arches cannot do
 * USB from the stack. The reply event is copied from an stage buffer,
 * so it can be in the stack. See WUSB1.0[8.6.2.4] for more details.
 */
int uwb_rc_reset(struct uwb_rc *rc)
{
	int result = -ENOMEM;
	struct uwb_rc_evt_confirm reply;
	struct uwb_rccb *cmd;
	size_t cmd_size = sizeof(*cmd);

	mutex_lock(&rc->uwb_dev.mutex);
	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (cmd == NULL)
		goto error_kzalloc;
	cmd->bCommandType = UWB_RC_CET_GENERAL;
	cmd->wCommand = cpu_to_le16(UWB_RC_CMD_RESET);
	reply.rceb.bEventType = UWB_RC_CET_GENERAL;
	reply.rceb.wEvent = UWB_RC_CMD_RESET;
	result = uwb_rc_cmd(rc, "RESET", cmd, cmd_size,
			    &reply.rceb, sizeof(reply));
	if (result < 0)
		goto error_cmd;
	if (reply.bResultCode != UWB_RC_RES_SUCCESS) {
		dev_err(&rc->uwb_dev.dev,
			"RESET: command execution failed: %s (%d)\n",
			uwb_rc_strerror(reply.bResultCode), reply.bResultCode);
		result = -EIO;
	}
error_cmd:
	kfree(cmd);
error_kzalloc:
	mutex_unlock(&rc->uwb_dev.mutex);
	return result;
}

int uwbd_msg_handle_reset(struct uwb_event *evt)
{
	struct uwb_rc *rc = evt->rc;
	int ret;

	dev_info(&rc->uwb_dev.dev, "resetting radio controller\n");
	ret = rc->reset(rc);
	if (ret < 0) {
		dev_err(&rc->uwb_dev.dev, "failed to reset hardware: %d\n", ret);
		goto error;
	}
	return 0;
error:
	/* Nothing can be done except try the reset again. Wait a bit
	   to avoid reset loops during probe() or remove(). */
	msleep(1000);
	uwb_rc_reset_all(rc);
	return ret;
}

/**
 * uwb_rc_reset_all - request a reset of the radio controller and PALs
 * @rc: the radio controller of the hardware device to be reset.
 *
 * The full hardware reset of the radio controller and all the PALs
 * will be scheduled.
 */
void uwb_rc_reset_all(struct uwb_rc *rc)
{
	struct uwb_event *evt;

	evt = kzalloc(sizeof(struct uwb_event), GFP_ATOMIC);
	if (unlikely(evt == NULL))
		return;

	evt->rc = __uwb_rc_get(rc);	/* will be put by uwbd's uwbd_event_handle() */
	evt->ts_jiffies = jiffies;
	evt->type = UWB_EVT_TYPE_MSG;
	evt->message = UWB_EVT_MSG_RESET;

	uwbd_event_queue(evt);
}
EXPORT_SYMBOL_GPL(uwb_rc_reset_all);

void uwb_rc_pre_reset(struct uwb_rc *rc)
{
	rc->stop(rc);
	uwbd_flush(rc);

	uwb_radio_reset_state(rc);
	uwb_rsv_remove_all(rc);
}
EXPORT_SYMBOL_GPL(uwb_rc_pre_reset);

int uwb_rc_post_reset(struct uwb_rc *rc)
{
	int ret;

	ret = rc->start(rc);
	if (ret)
		goto out;
	ret = uwb_rc_mac_addr_set(rc, &rc->uwb_dev.mac_addr);
	if (ret)
		goto out;
	ret = uwb_rc_dev_addr_set(rc, &rc->uwb_dev.dev_addr);
	if (ret)
		goto out;
out:
	return ret;
}
EXPORT_SYMBOL_GPL(uwb_rc_post_reset);
