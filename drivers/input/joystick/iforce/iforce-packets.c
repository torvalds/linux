// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) 2000-2002 Vojtech Pavlik <vojtech@ucw.cz>
 *  Copyright (c) 2001-2002, 2007 Johann Deneux <johann.deneux@gmail.com>
 *
 *  USB/RS232 I-Force joysticks and wheels.
 */

#include <linux/unaligned.h>
#include "iforce.h"

static struct {
	__s32 x;
	__s32 y;
} iforce_hat_to_axis[16] = {{ 0,-1}, { 1,-1}, { 1, 0}, { 1, 1}, { 0, 1}, {-1, 1}, {-1, 0}, {-1,-1}};


void iforce_dump_packet(struct iforce *iforce, char *msg, u16 cmd, unsigned char *data)
{
	dev_dbg(iforce->dev->dev.parent, "%s %s cmd = %04x, data = %*ph\n",
		__func__, msg, cmd, LO(cmd), data);
}

/*
 * Send a packet of bytes to the device
 */
int iforce_send_packet(struct iforce *iforce, u16 cmd, unsigned char* data)
{
	/* Copy data to buffer */
	int n = LO(cmd);
	int c;
	int empty;
	int head, tail;
	unsigned long flags;

/*
 * Update head and tail of xmit buffer
 */
	spin_lock_irqsave(&iforce->xmit_lock, flags);

	head = iforce->xmit.head;
	tail = iforce->xmit.tail;


	if (CIRC_SPACE(head, tail, XMIT_SIZE) < n+2) {
		dev_warn(&iforce->dev->dev,
			 "not enough space in xmit buffer to send new packet\n");
		spin_unlock_irqrestore(&iforce->xmit_lock, flags);
		return -1;
	}

	empty = head == tail;
	XMIT_INC(iforce->xmit.head, n+2);

/*
 * Store packet in xmit buffer
 */
	iforce->xmit.buf[head] = HI(cmd);
	XMIT_INC(head, 1);
	iforce->xmit.buf[head] = LO(cmd);
	XMIT_INC(head, 1);

	c = CIRC_SPACE_TO_END(head, tail, XMIT_SIZE);
	if (n < c) c=n;

	memcpy(&iforce->xmit.buf[head],
	       data,
	       c);
	if (n != c) {
		memcpy(&iforce->xmit.buf[0],
		       data + c,
		       n - c);
	}
	XMIT_INC(head, n);

	spin_unlock_irqrestore(&iforce->xmit_lock, flags);
/*
 * If necessary, start the transmission
 */
	if (empty)
		iforce->xport_ops->xmit(iforce);

	return 0;
}
EXPORT_SYMBOL(iforce_send_packet);

/* Start or stop an effect */
int iforce_control_playback(struct iforce* iforce, u16 id, unsigned int value)
{
	unsigned char data[3];

	data[0] = LO(id);
	data[1] = (value > 0) ? ((value > 1) ? 0x41 : 0x01) : 0;
	data[2] = LO(value);
	return iforce_send_packet(iforce, FF_CMD_PLAY, data);
}

/* Mark an effect that was being updated as ready. That means it can be updated
 * again */
static int mark_core_as_ready(struct iforce *iforce, unsigned short addr)
{
	int i;

	if (!iforce->dev->ff)
		return 0;

	for (i = 0; i < iforce->dev->ff->max_effects; ++i) {
		if (test_bit(FF_CORE_IS_USED, iforce->core_effects[i].flags) &&
		    (iforce->core_effects[i].mod1_chunk.start == addr ||
		     iforce->core_effects[i].mod2_chunk.start == addr)) {
			clear_bit(FF_CORE_UPDATE, iforce->core_effects[i].flags);
			return 0;
		}
	}
	dev_warn(&iforce->dev->dev, "unused effect %04x updated !!!\n", addr);
	return -1;
}

static void iforce_report_hats_buttons(struct iforce *iforce, u8 *data)
{
	struct input_dev *dev = iforce->dev;
	int i;

	input_report_abs(dev, ABS_HAT0X, iforce_hat_to_axis[data[6] >> 4].x);
	input_report_abs(dev, ABS_HAT0Y, iforce_hat_to_axis[data[6] >> 4].y);

	for (i = 0; iforce->type->btn[i] >= 0; i++)
		input_report_key(dev, iforce->type->btn[i],
				 data[(i >> 3) + 5] & (1 << (i & 7)));

	/* If there are untouched bits left, interpret them as the second hat */
	if (i <= 8) {
		u8 btns = data[6];

		if (test_bit(ABS_HAT1X, dev->absbit)) {
			if (btns & BIT(3))
				input_report_abs(dev, ABS_HAT1X, -1);
			else if (btns & BIT(1))
				input_report_abs(dev, ABS_HAT1X, 1);
			else
				input_report_abs(dev, ABS_HAT1X, 0);
		}

		if (test_bit(ABS_HAT1Y, dev->absbit)) {
			if (btns & BIT(0))
				input_report_abs(dev, ABS_HAT1Y, -1);
			else if (btns & BIT(2))
				input_report_abs(dev, ABS_HAT1Y, 1);
			else
				input_report_abs(dev, ABS_HAT1Y, 0);
		}
	}
}

void iforce_process_packet(struct iforce *iforce,
			   u8 packet_id, u8 *data, size_t len)
{
	struct input_dev *dev = iforce->dev;
	int i, j;

	switch (packet_id) {

	case 0x01:	/* joystick position data */
		input_report_abs(dev, ABS_X,
				 (__s16) get_unaligned_le16(data));
		input_report_abs(dev, ABS_Y,
				 (__s16) get_unaligned_le16(data + 2));
		input_report_abs(dev, ABS_THROTTLE, 255 - data[4]);

		if (len >= 8 && test_bit(ABS_RUDDER ,dev->absbit))
			input_report_abs(dev, ABS_RUDDER, (__s8)data[7]);

		iforce_report_hats_buttons(iforce, data);

		input_sync(dev);
		break;

	case 0x03:	/* wheel position data */
		input_report_abs(dev, ABS_WHEEL,
				 (__s16) get_unaligned_le16(data));
		input_report_abs(dev, ABS_GAS,   255 - data[2]);
		input_report_abs(dev, ABS_BRAKE, 255 - data[3]);

		iforce_report_hats_buttons(iforce, data);

		input_sync(dev);
		break;

	case 0x02:	/* status report */
		input_report_key(dev, BTN_DEAD, data[0] & 0x02);
		input_sync(dev);

		/* Check if an effect was just started or stopped */
		i = data[1] & 0x7f;
		if (data[1] & 0x80) {
			if (!test_and_set_bit(FF_CORE_IS_PLAYED, iforce->core_effects[i].flags)) {
				/* Report play event */
				input_report_ff_status(dev, i, FF_STATUS_PLAYING);
			}
		} else if (test_and_clear_bit(FF_CORE_IS_PLAYED, iforce->core_effects[i].flags)) {
			/* Report stop event */
			input_report_ff_status(dev, i, FF_STATUS_STOPPED);
		}

		for (j = 3; j < len; j += 2)
			mark_core_as_ready(iforce, get_unaligned_le16(data + j));

		break;
	}
}
EXPORT_SYMBOL(iforce_process_packet);
