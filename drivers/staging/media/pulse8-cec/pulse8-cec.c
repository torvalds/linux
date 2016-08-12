/*
 * Pulse Eight HDMI CEC driver
 *
 * Copyright 2016 Hans Verkuil <hverkuil@xs4all.nl
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version of 2 of the License, or (at your
 * option) any later version. See the file COPYING in the main directory of
 * this archive for more details.
 */

#include <linux/completion.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/workqueue.h>
#include <linux/serio.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/delay.h>

#include <media/cec.h>

MODULE_AUTHOR("Hans Verkuil <hverkuil@xs4all.nl>");
MODULE_DESCRIPTION("Pulse Eight HDMI CEC driver");
MODULE_LICENSE("GPL");

static int debug;
module_param(debug, int, 0644);
MODULE_PARM_DESC(debug, "debug level (0-1)");

enum pulse8_msgcodes {
	MSGCODE_NOTHING = 0,
	MSGCODE_PING,
	MSGCODE_TIMEOUT_ERROR,
	MSGCODE_HIGH_ERROR,
	MSGCODE_LOW_ERROR,
	MSGCODE_FRAME_START,
	MSGCODE_FRAME_DATA,
	MSGCODE_RECEIVE_FAILED,
	MSGCODE_COMMAND_ACCEPTED,	/* 0x08 */
	MSGCODE_COMMAND_REJECTED,
	MSGCODE_SET_ACK_MASK,
	MSGCODE_TRANSMIT,
	MSGCODE_TRANSMIT_EOM,
	MSGCODE_TRANSMIT_IDLETIME,
	MSGCODE_TRANSMIT_ACK_POLARITY,
	MSGCODE_TRANSMIT_LINE_TIMEOUT,
	MSGCODE_TRANSMIT_SUCCEEDED,	/* 0x10 */
	MSGCODE_TRANSMIT_FAILED_LINE,
	MSGCODE_TRANSMIT_FAILED_ACK,
	MSGCODE_TRANSMIT_FAILED_TIMEOUT_DATA,
	MSGCODE_TRANSMIT_FAILED_TIMEOUT_LINE,
	MSGCODE_FIRMWARE_VERSION,
	MSGCODE_START_BOOTLOADER,
	MSGCODE_GET_BUILDDATE,
	MSGCODE_SET_CONTROLLED,		/* 0x18 */
	MSGCODE_GET_AUTO_ENABLED,
	MSGCODE_SET_AUTO_ENABLED,
	MSGCODE_GET_DEFAULT_LOGICAL_ADDRESS,
	MSGCODE_SET_DEFAULT_LOGICAL_ADDRESS,
	MSGCODE_GET_LOGICAL_ADDRESS_MASK,
	MSGCODE_SET_LOGICAL_ADDRESS_MASK,
	MSGCODE_GET_PHYSICAL_ADDRESS,
	MSGCODE_SET_PHYSICAL_ADDRESS,	/* 0x20 */
	MSGCODE_GET_DEVICE_TYPE,
	MSGCODE_SET_DEVICE_TYPE,
	MSGCODE_GET_HDMI_VERSION,
	MSGCODE_SET_HDMI_VERSION,
	MSGCODE_GET_OSD_NAME,
	MSGCODE_SET_OSD_NAME,
	MSGCODE_WRITE_EEPROM,
	MSGCODE_GET_ADAPTER_TYPE,	/* 0x28 */
	MSGCODE_SET_ACTIVE_SOURCE,

	MSGCODE_FRAME_EOM = 0x80,
	MSGCODE_FRAME_ACK = 0x40,
};

#define MSGSTART	0xff
#define MSGEND		0xfe
#define MSGESC		0xfd
#define MSGOFFSET	3

#define DATA_SIZE 256

struct pulse8 {
	struct device *dev;
	struct serio *serio;
	struct cec_adapter *adap;
	struct completion cmd_done;
	struct work_struct work;
	struct cec_msg rx_msg;
	u8 data[DATA_SIZE];
	unsigned int len;
	u8 buf[DATA_SIZE];
	unsigned int idx;
	bool escape;
	bool started;
};

static void pulse8_irq_work_handler(struct work_struct *work)
{
	struct pulse8 *pulse8 =
		container_of(work, struct pulse8, work);

	switch (pulse8->data[0] & 0x3f) {
	case MSGCODE_FRAME_DATA:
		cec_received_msg(pulse8->adap, &pulse8->rx_msg);
		break;
	case MSGCODE_TRANSMIT_SUCCEEDED:
		cec_transmit_done(pulse8->adap, CEC_TX_STATUS_OK,
				  0, 0, 0, 0);
		break;
	case MSGCODE_TRANSMIT_FAILED_LINE:
		cec_transmit_done(pulse8->adap, CEC_TX_STATUS_ARB_LOST,
				  1, 0, 0, 0);
		break;
	case MSGCODE_TRANSMIT_FAILED_ACK:
		cec_transmit_done(pulse8->adap, CEC_TX_STATUS_NACK,
				  0, 1, 0, 0);
		break;
	case MSGCODE_TRANSMIT_FAILED_TIMEOUT_DATA:
	case MSGCODE_TRANSMIT_FAILED_TIMEOUT_LINE:
		cec_transmit_done(pulse8->adap, CEC_TX_STATUS_ERROR,
				  0, 0, 0, 1);
		break;
	}
}

static irqreturn_t pulse8_interrupt(struct serio *serio, unsigned char data,
				    unsigned int flags)
{
	struct pulse8 *pulse8 = serio_get_drvdata(serio);

	if (!pulse8->started && data != MSGSTART)
		return IRQ_HANDLED;
	if (data == MSGESC) {
		pulse8->escape = true;
		return IRQ_HANDLED;
	}
	if (pulse8->escape) {
		data += MSGOFFSET;
		pulse8->escape = false;
	} else if (data == MSGEND) {
		struct cec_msg *msg = &pulse8->rx_msg;

		if (debug)
			dev_info(pulse8->dev, "received: %*ph\n",
				 pulse8->idx, pulse8->buf);
		pulse8->data[0] = pulse8->buf[0];
		switch (pulse8->buf[0] & 0x3f) {
		case MSGCODE_FRAME_START:
			msg->len = 1;
			msg->msg[0] = pulse8->buf[1];
			break;
		case MSGCODE_FRAME_DATA:
			if (msg->len == CEC_MAX_MSG_SIZE)
				break;
			msg->msg[msg->len++] = pulse8->buf[1];
			if (pulse8->buf[0] & MSGCODE_FRAME_EOM)
				schedule_work(&pulse8->work);
			break;
		case MSGCODE_TRANSMIT_SUCCEEDED:
		case MSGCODE_TRANSMIT_FAILED_LINE:
		case MSGCODE_TRANSMIT_FAILED_ACK:
		case MSGCODE_TRANSMIT_FAILED_TIMEOUT_DATA:
		case MSGCODE_TRANSMIT_FAILED_TIMEOUT_LINE:
			schedule_work(&pulse8->work);
			break;
		case MSGCODE_TIMEOUT_ERROR:
			break;
		case MSGCODE_COMMAND_ACCEPTED:
		case MSGCODE_COMMAND_REJECTED:
		default:
			if (pulse8->idx == 0)
				break;
			memcpy(pulse8->data, pulse8->buf, pulse8->idx);
			pulse8->len = pulse8->idx;
			complete(&pulse8->cmd_done);
			break;
		}
		pulse8->idx = 0;
		pulse8->started = false;
		return IRQ_HANDLED;
	} else if (data == MSGSTART) {
		pulse8->idx = 0;
		pulse8->started = true;
		return IRQ_HANDLED;
	}

	if (pulse8->idx >= DATA_SIZE) {
		dev_dbg(pulse8->dev,
			"throwing away %d bytes of garbage\n", pulse8->idx);
		pulse8->idx = 0;
	}
	pulse8->buf[pulse8->idx++] = data;
	return IRQ_HANDLED;
}

static void pulse8_disconnect(struct serio *serio)
{
	struct pulse8 *pulse8 = serio_get_drvdata(serio);

	cec_unregister_adapter(pulse8->adap);
	dev_info(&serio->dev, "disconnected\n");
	serio_close(serio);
	serio_set_drvdata(serio, NULL);
	kfree(pulse8);
}

static int pulse8_send(struct serio *serio, const u8 *command, u8 cmd_len)
{
	int err = 0;

	err = serio_write(serio, MSGSTART);
	if (err)
		return err;
	for (; !err && cmd_len; command++, cmd_len--) {
		if (*command >= MSGESC) {
			err = serio_write(serio, MSGESC);
			if (!err)
				err = serio_write(serio, *command - MSGOFFSET);
		} else {
			err = serio_write(serio, *command);
		}
	}
	if (!err)
		err = serio_write(serio, 0xfe);

	return err;
}

static int pulse8_send_and_wait(struct pulse8 *pulse8,
				const u8 *cmd, u8 cmd_len, u8 response, u8 size)
{
	int err;

	/*dev_info(pulse8->dev, "transmit: %*ph\n", cmd_len, cmd);*/
	init_completion(&pulse8->cmd_done);

	err = pulse8_send(pulse8->serio, cmd, cmd_len);
	if (err)
		return err;

	if (!wait_for_completion_timeout(&pulse8->cmd_done, HZ))
		return -ETIMEDOUT;
	if ((pulse8->data[0] & 0x3f) == MSGCODE_COMMAND_REJECTED &&
	    cmd[0] != MSGCODE_SET_CONTROLLED &&
	    cmd[0] != MSGCODE_SET_AUTO_ENABLED &&
	    cmd[0] != MSGCODE_GET_BUILDDATE) {
		u8 cmd_sc[2];

		cmd_sc[0] = MSGCODE_SET_CONTROLLED;
		cmd_sc[1] = 1;
		err = pulse8_send_and_wait(pulse8, cmd_sc, 2,
					   MSGCODE_COMMAND_ACCEPTED, 1);
		if (err)
			return err;
		init_completion(&pulse8->cmd_done);

		err = pulse8_send(pulse8->serio, cmd, cmd_len);
		if (err)
			return err;

		if (!wait_for_completion_timeout(&pulse8->cmd_done, HZ))
			return -ETIMEDOUT;
	}
	if (response &&
	    ((pulse8->data[0] & 0x3f) != response || pulse8->len < size + 1)) {
		dev_info(pulse8->dev, "transmit: failed %02x\n",
			 pulse8->data[0] & 0x3f);
		return -EIO;
	}
	return 0;
}

static int pulse8_setup(struct pulse8 *pulse8, struct serio *serio)
{
	u8 *data = pulse8->data + 1;
	unsigned int count = 0;
	unsigned int vers = 0;
	u8 cmd[2];
	int err;

	cmd[0] = MSGCODE_PING;
	err = pulse8_send_and_wait(pulse8, cmd, 1,
				   MSGCODE_COMMAND_ACCEPTED, 0);
	cmd[0] = MSGCODE_FIRMWARE_VERSION;
	if (!err)
		err = pulse8_send_and_wait(pulse8, cmd, 1, cmd[0], 2);
	if (err)
		return err;

	vers = (data[0] << 8) | data[1];

	dev_info(pulse8->dev, "Firmware version %04x\n", vers);
	if (vers < 2)
		return 0;

	cmd[0] = MSGCODE_GET_BUILDDATE;
	if (!err)
		err = pulse8_send_and_wait(pulse8, cmd, 1, cmd[0], 4);
	if (!err) {
		time_t date = (data[0] << 24) | (data[1] << 16) |
			(data[2] << 8) | data[3];
		struct tm tm;

		time_to_tm(date, 0, &tm);

		dev_info(pulse8->dev, "Firmware build date %04ld.%02d.%02d %02d:%02d:%02d\n",
			 tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
			 tm.tm_hour, tm.tm_min, tm.tm_sec);
	}

	do {
		if (count)
			msleep(500);
		cmd[0] = MSGCODE_SET_AUTO_ENABLED;
		cmd[1] = 0;
		err = pulse8_send_and_wait(pulse8, cmd, 2,
					   MSGCODE_COMMAND_ACCEPTED, 1);
		if (err && count == 0) {
			dev_info(pulse8->dev, "No Auto Enabled supported\n");
			return 0;
		}

		cmd[0] = MSGCODE_GET_AUTO_ENABLED;
		if (!err)
			err = pulse8_send_and_wait(pulse8, cmd, 1, cmd[0], 1);
		if (!err && !data[0]) {
			cmd[0] = MSGCODE_WRITE_EEPROM;
			err = pulse8_send_and_wait(pulse8, cmd, 1,
						   MSGCODE_COMMAND_ACCEPTED, 1);
			cmd[0] = MSGCODE_GET_AUTO_ENABLED;
			if (!err)
				err = pulse8_send_and_wait(pulse8, cmd, 1,
							   cmd[0], 1);
		}
	} while (!err && data[0] && count++ < 5);

	if (!err && data[0])
		err = -EIO;

	return err;
}

static int pulse8_cec_adap_enable(struct cec_adapter *adap, bool enable)
{
	struct pulse8 *pulse8 = adap->priv;
	u8 cmd[16];
	int err;

	cmd[0] = MSGCODE_SET_CONTROLLED;
	cmd[1] = enable;
	err = pulse8_send_and_wait(pulse8, cmd, 2,
				   MSGCODE_COMMAND_ACCEPTED, 1);
	return enable ? err : 0;
}

static int pulse8_cec_adap_log_addr(struct cec_adapter *adap, u8 log_addr)
{
	struct pulse8 *pulse8 = adap->priv;
	u16 mask = 0;
	u8 cmd[3];
	int err;

	if (log_addr != CEC_LOG_ADDR_INVALID)
		mask = 1 << log_addr;
	cmd[0] = MSGCODE_SET_ACK_MASK;
	cmd[1] = mask >> 8;
	cmd[2] = mask & 0xff;
	err = pulse8_send_and_wait(pulse8, cmd, 3,
				   MSGCODE_COMMAND_ACCEPTED, 0);
	if (mask == 0)
		return 0;
	return err;
}

static int pulse8_cec_adap_transmit(struct cec_adapter *adap, u8 attempts,
				    u32 signal_free_time, struct cec_msg *msg)
{
	struct pulse8 *pulse8 = adap->priv;
	u8 cmd[2];
	unsigned int i;
	int err;

	cmd[0] = MSGCODE_TRANSMIT_IDLETIME;
	cmd[1] = signal_free_time;
	err = pulse8_send_and_wait(pulse8, cmd, 2,
				   MSGCODE_COMMAND_ACCEPTED, 1);
	cmd[0] = MSGCODE_TRANSMIT_ACK_POLARITY;
	cmd[1] = cec_msg_is_broadcast(msg);
	if (!err)
		err = pulse8_send_and_wait(pulse8, cmd, 2,
					   MSGCODE_COMMAND_ACCEPTED, 1);
	cmd[0] = msg->len == 1 ? MSGCODE_TRANSMIT_EOM : MSGCODE_TRANSMIT;
	cmd[1] = msg->msg[0];
	if (!err)
		err = pulse8_send_and_wait(pulse8, cmd, 2,
					   MSGCODE_COMMAND_ACCEPTED, 1);
	if (!err && msg->len > 1) {
		cmd[0] = msg->len == 2 ? MSGCODE_TRANSMIT_EOM :
					 MSGCODE_TRANSMIT;
		cmd[1] = msg->msg[1];
		err = pulse8_send_and_wait(pulse8, cmd, 2,
					   MSGCODE_COMMAND_ACCEPTED, 1);
		for (i = 0; !err && i + 2 < msg->len; i++) {
			cmd[0] = (i + 2 == msg->len - 1) ?
				MSGCODE_TRANSMIT_EOM : MSGCODE_TRANSMIT;
			cmd[1] = msg->msg[i + 2];
			err = pulse8_send_and_wait(pulse8, cmd, 2,
						   MSGCODE_COMMAND_ACCEPTED, 1);
		}
	}

	return err;
}

static int pulse8_received(struct cec_adapter *adap, struct cec_msg *msg)
{
	return -ENOMSG;
}

static const struct cec_adap_ops pulse8_cec_adap_ops = {
	.adap_enable = pulse8_cec_adap_enable,
	.adap_log_addr = pulse8_cec_adap_log_addr,
	.adap_transmit = pulse8_cec_adap_transmit,
	.received = pulse8_received,
};

static int pulse8_connect(struct serio *serio, struct serio_driver *drv)
{
	u32 caps = CEC_CAP_TRANSMIT | CEC_CAP_LOG_ADDRS | CEC_CAP_PHYS_ADDR |
		CEC_CAP_PASSTHROUGH | CEC_CAP_RC | CEC_CAP_MONITOR_ALL;
	struct pulse8 *pulse8;
	int err = -ENOMEM;

	pulse8 = kzalloc(sizeof(*pulse8), GFP_KERNEL);

	if (!pulse8)
		return -ENOMEM;

	pulse8->serio = serio;
	pulse8->adap = cec_allocate_adapter(&pulse8_cec_adap_ops, pulse8,
		"HDMI CEC", caps, 1, &serio->dev);
	err = PTR_ERR_OR_ZERO(pulse8->adap);
	if (err < 0)
		goto free_device;

	pulse8->dev = &serio->dev;
	serio_set_drvdata(serio, pulse8);
	INIT_WORK(&pulse8->work, pulse8_irq_work_handler);

	err = serio_open(serio, drv);
	if (err)
		goto delete_adap;

	err = pulse8_setup(pulse8, serio);
	if (err)
		goto close_serio;

	err = cec_register_adapter(pulse8->adap);
	if (err < 0)
		goto close_serio;

	pulse8->dev = &pulse8->adap->devnode.dev;
	return 0;

close_serio:
	serio_close(serio);
delete_adap:
	cec_delete_adapter(pulse8->adap);
	serio_set_drvdata(serio, NULL);
free_device:
	kfree(pulse8);
	return err;
}

static struct serio_device_id pulse8_serio_ids[] = {
	{
		.type	= SERIO_RS232,
		.proto	= SERIO_PULSE8_CEC,
		.id	= SERIO_ANY,
		.extra	= SERIO_ANY,
	},
	{ 0 }
};

MODULE_DEVICE_TABLE(serio, pulse8_serio_ids);

static struct serio_driver pulse8_drv = {
	.driver		= {
		.name	= "pulse8-cec",
	},
	.description	= "Pulse Eight HDMI CEC driver",
	.id_table	= pulse8_serio_ids,
	.interrupt	= pulse8_interrupt,
	.connect	= pulse8_connect,
	.disconnect	= pulse8_disconnect,
};

module_serio_driver(pulse8_drv);
