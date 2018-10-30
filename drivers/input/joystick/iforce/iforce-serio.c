/*
 *  Copyright (c) 2000-2001 Vojtech Pavlik <vojtech@ucw.cz>
 *  Copyright (c) 2001, 2007 Johann Deneux <johann.deneux@gmail.com>
 *
 *  USB/RS232 I-Force joysticks and wheels.
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include "iforce.h"

void iforce_serial_xmit(struct iforce *iforce)
{
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
		clear_bit(IFORCE_XMIT_RUNNING, iforce->xmit_flags);
		spin_unlock_irqrestore(&iforce->xmit_lock, flags);
		return;
	}

	cs = 0x2b;

	serio_write(iforce->serio, 0x2b);

	serio_write(iforce->serio, iforce->xmit.buf[iforce->xmit.tail]);
	cs ^= iforce->xmit.buf[iforce->xmit.tail];
	XMIT_INC(iforce->xmit.tail, 1);

	for (i=iforce->xmit.buf[iforce->xmit.tail]; i >= 0; --i) {
		serio_write(iforce->serio, iforce->xmit.buf[iforce->xmit.tail]);
		cs ^= iforce->xmit.buf[iforce->xmit.tail];
		XMIT_INC(iforce->xmit.tail, 1);
	}

	serio_write(iforce->serio, cs);

	if (test_and_clear_bit(IFORCE_XMIT_AGAIN, iforce->xmit_flags))
		goto again;

	clear_bit(IFORCE_XMIT_RUNNING, iforce->xmit_flags);

	spin_unlock_irqrestore(&iforce->xmit_lock, flags);
}

static void iforce_serio_write_wakeup(struct serio *serio)
{
	struct iforce *iforce = serio_get_drvdata(serio);

	iforce_serial_xmit(iforce);
}

static irqreturn_t iforce_serio_irq(struct serio *serio,
		unsigned char data, unsigned int flags)
{
	struct iforce *iforce = serio_get_drvdata(serio);

	if (!iforce->pkt) {
		if (data == 0x2b)
			iforce->pkt = 1;
		goto out;
	}

	if (!iforce->id) {
		if (data > 3 && data != 0xff)
			iforce->pkt = 0;
		else
			iforce->id = data;
		goto out;
	}

	if (!iforce->len) {
		if (data > IFORCE_MAX_LENGTH) {
			iforce->pkt = 0;
			iforce->id = 0;
		} else {
			iforce->len = data;
		}
		goto out;
	}

	if (iforce->idx < iforce->len) {
		iforce->csum += iforce->data[iforce->idx++] = data;
		goto out;
	}

	if (iforce->idx == iforce->len) {
		iforce_process_packet(iforce, (iforce->id << 8) | iforce->idx, iforce->data);
		iforce->pkt = 0;
		iforce->id  = 0;
		iforce->len = 0;
		iforce->idx = 0;
		iforce->csum = 0;
	}
out:
	return IRQ_HANDLED;
}

static int iforce_serio_connect(struct serio *serio, struct serio_driver *drv)
{
	struct iforce *iforce;
	int err;

	iforce = kzalloc(sizeof(struct iforce), GFP_KERNEL);
	if (!iforce)
		return -ENOMEM;

	iforce->bus = IFORCE_232;
	iforce->serio = serio;

	serio_set_drvdata(serio, iforce);

	err = serio_open(serio, drv);
	if (err)
		goto fail1;

	err = iforce_init_device(iforce);
	if (err)
		goto fail2;

	return 0;

 fail2:	serio_close(serio);
 fail1:	serio_set_drvdata(serio, NULL);
	kfree(iforce);
	return err;
}

static void iforce_serio_disconnect(struct serio *serio)
{
	struct iforce *iforce = serio_get_drvdata(serio);

	input_unregister_device(iforce->dev);
	serio_close(serio);
	serio_set_drvdata(serio, NULL);
	kfree(iforce);
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
