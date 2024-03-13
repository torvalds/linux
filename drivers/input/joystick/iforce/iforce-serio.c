// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) 2000-2001 Vojtech Pavlik <vojtech@ucw.cz>
 *  Copyright (c) 2001, 2007 Johann Deneux <johann.deneux@gmail.com>
 *
 *  USB/RS232 I-Force joysticks and wheels.
 */

#include <linux/serio.h>
#include "iforce.h"

struct iforce_serio {
	struct iforce iforce;

	struct serio *serio;
	int idx, pkt, len, id;
	u8 csum;
	u8 expect_packet;
	u8 cmd_response[IFORCE_MAX_LENGTH];
	u8 cmd_response_len;
	u8 data_in[IFORCE_MAX_LENGTH];
};

static void iforce_serio_xmit(struct iforce *iforce)
{
	struct iforce_serio *iforce_serio = container_of(iforce,
							 struct iforce_serio,
							 iforce);
	unsigned char cs;
	int i;
	unsigned long flags;

	if (test_and_set_bit(IFORCE_XMIT_RUNNING, iforce->xmit_flags)) {
		set_bit(IFORCE_XMIT_AGAIN, iforce->xmit_flags);
		return;
	}

	spin_lock_irqsave(&iforce->xmit_lock, flags);

again:
	if (iforce->xmit.head == iforce->xmit.tail) {
		iforce_clear_xmit_and_wake(iforce);
		spin_unlock_irqrestore(&iforce->xmit_lock, flags);
		return;
	}

	cs = 0x2b;

	serio_write(iforce_serio->serio, 0x2b);

	serio_write(iforce_serio->serio, iforce->xmit.buf[iforce->xmit.tail]);
	cs ^= iforce->xmit.buf[iforce->xmit.tail];
	XMIT_INC(iforce->xmit.tail, 1);

	for (i=iforce->xmit.buf[iforce->xmit.tail]; i >= 0; --i) {
		serio_write(iforce_serio->serio,
			    iforce->xmit.buf[iforce->xmit.tail]);
		cs ^= iforce->xmit.buf[iforce->xmit.tail];
		XMIT_INC(iforce->xmit.tail, 1);
	}

	serio_write(iforce_serio->serio, cs);

	if (test_and_clear_bit(IFORCE_XMIT_AGAIN, iforce->xmit_flags))
		goto again;

	iforce_clear_xmit_and_wake(iforce);

	spin_unlock_irqrestore(&iforce->xmit_lock, flags);
}

static int iforce_serio_get_id(struct iforce *iforce, u8 id,
			       u8 *response_data, size_t *response_len)
{
	struct iforce_serio *iforce_serio = container_of(iforce,
							 struct iforce_serio,
							 iforce);

	iforce_serio->expect_packet = HI(FF_CMD_QUERY);
	iforce_serio->cmd_response_len = 0;

	iforce_send_packet(iforce, FF_CMD_QUERY, &id);

	wait_event_interruptible_timeout(iforce->wait,
					 !iforce_serio->expect_packet, HZ);

	if (iforce_serio->expect_packet) {
		iforce_serio->expect_packet = 0;
		return -ETIMEDOUT;
	}

	if (iforce_serio->cmd_response[0] != id)
		return -EIO;

	memcpy(response_data, iforce_serio->cmd_response,
	       iforce_serio->cmd_response_len);
	*response_len = iforce_serio->cmd_response_len;

	return 0;
}

static int iforce_serio_start_io(struct iforce *iforce)
{
	/* No special handling required */
	return 0;
}

static void iforce_serio_stop_io(struct iforce *iforce)
{
	//TODO: Wait for the last packets to be sent
}

static const struct iforce_xport_ops iforce_serio_xport_ops = {
	.xmit		= iforce_serio_xmit,
	.get_id		= iforce_serio_get_id,
	.start_io	= iforce_serio_start_io,
	.stop_io	= iforce_serio_stop_io,
};

static void iforce_serio_write_wakeup(struct serio *serio)
{
	struct iforce *iforce = serio_get_drvdata(serio);

	iforce_serio_xmit(iforce);
}

static irqreturn_t iforce_serio_irq(struct serio *serio,
				    unsigned char data, unsigned int flags)
{
	struct iforce_serio *iforce_serio = serio_get_drvdata(serio);
	struct iforce *iforce = &iforce_serio->iforce;

	if (!iforce_serio->pkt) {
		if (data == 0x2b)
			iforce_serio->pkt = 1;
		goto out;
	}

	if (!iforce_serio->id) {
		if (data > 3 && data != 0xff)
			iforce_serio->pkt = 0;
		else
			iforce_serio->id = data;
		goto out;
	}

	if (!iforce_serio->len) {
		if (data > IFORCE_MAX_LENGTH) {
			iforce_serio->pkt = 0;
			iforce_serio->id = 0;
		} else {
			iforce_serio->len = data;
		}
		goto out;
	}

	if (iforce_serio->idx < iforce_serio->len) {
		iforce_serio->data_in[iforce_serio->idx++] = data;
		iforce_serio->csum += data;
		goto out;
	}

	if (iforce_serio->idx == iforce_serio->len) {
		/* Handle command completion */
		if (iforce_serio->expect_packet == iforce_serio->id) {
			iforce_serio->expect_packet = 0;
			memcpy(iforce_serio->cmd_response,
			       iforce_serio->data_in, IFORCE_MAX_LENGTH);
			iforce_serio->cmd_response_len = iforce_serio->len;

			/* Signal that command is done */
			wake_up_all(&iforce->wait);
		} else if (likely(iforce->type)) {
			iforce_process_packet(iforce, iforce_serio->id,
					      iforce_serio->data_in,
					      iforce_serio->len);
		}

		iforce_serio->pkt = 0;
		iforce_serio->id  = 0;
		iforce_serio->len = 0;
		iforce_serio->idx = 0;
		iforce_serio->csum = 0;
	}
out:
	return IRQ_HANDLED;
}

static int iforce_serio_connect(struct serio *serio, struct serio_driver *drv)
{
	struct iforce_serio *iforce_serio;
	int err;

	iforce_serio = kzalloc(sizeof(*iforce_serio), GFP_KERNEL);
	if (!iforce_serio)
		return -ENOMEM;

	iforce_serio->iforce.xport_ops = &iforce_serio_xport_ops;

	iforce_serio->serio = serio;
	serio_set_drvdata(serio, iforce_serio);

	err = serio_open(serio, drv);
	if (err)
		goto fail1;

	err = iforce_init_device(&serio->dev, BUS_RS232, &iforce_serio->iforce);
	if (err)
		goto fail2;

	return 0;

 fail2:	serio_close(serio);
 fail1:	serio_set_drvdata(serio, NULL);
	kfree(iforce_serio);
	return err;
}

static void iforce_serio_disconnect(struct serio *serio)
{
	struct iforce_serio *iforce_serio = serio_get_drvdata(serio);

	input_unregister_device(iforce_serio->iforce.dev);
	serio_close(serio);
	serio_set_drvdata(serio, NULL);
	kfree(iforce_serio);
}

static const struct serio_device_id iforce_serio_ids[] = {
	{
		.type	= SERIO_RS232,
		.proto	= SERIO_IFORCE,
		.id	= SERIO_ANY,
		.extra	= SERIO_ANY,
	},
	{ 0 }
};

MODULE_DEVICE_TABLE(serio, iforce_serio_ids);

struct serio_driver iforce_serio_drv = {
	.driver		= {
		.name	= "iforce",
	},
	.description	= "RS232 I-Force joysticks and wheels driver",
	.id_table	= iforce_serio_ids,
	.write_wakeup	= iforce_serio_write_wakeup,
	.interrupt	= iforce_serio_irq,
	.connect	= iforce_serio_connect,
	.disconnect	= iforce_serio_disconnect,
};

module_serio_driver(iforce_serio_drv);

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>, Johann Deneux <johann.deneux@gmail.com>");
MODULE_DESCRIPTION("RS232 I-Force joysticks and wheels driver");
MODULE_LICENSE("GPL");
