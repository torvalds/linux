/* ir-raw-event.c - handle IR Pulse/Space event
 *
 * Copyright (C) 2010 by Mauro Carvalho Chehab <mchehab@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#include <media/ir-core.h>

/* Start time: 4.5 ms  */
#define MIN_START_TIME	3900000
#define MAX_START_TIME	5100000

/* Pulse time: 560 us  */
#define MIN_PULSE_TIME	460000
#define MAX_PULSE_TIME	660000

/* Bit 1 space time: 2.25ms-560 us */
#define MIN_BIT1_TIME	1490000
#define MAX_BIT1_TIME	1890000

/* Bit 0 space time: 1.12ms-560 us */
#define MIN_BIT0_TIME	360000
#define MAX_BIT0_TIME	760000

/**
 * __ir_nec_decode() - Decode one NEC pulsecode
 * @input_dev:	the struct input_dev descriptor of the device
 * @evs:	event array with type/duration of pulse/space
 * @len:	length of the array
 * @pos:	position to start seeking for a code
 * This function returns the decoded ircode or -EINVAL if no pulse got decoded
 */
static int __ir_nec_decode(struct input_dev *input_dev,
			   struct ir_raw_event *evs,
			   int len, int *pos)
{
	int count = -1;
	int ircode = 0, not_code = 0;

	/* Be sure that the first event is an start one and is a pulse */
	for (; *pos < len; (*pos)++) {
		if (evs[*pos].type & (IR_START_EVENT | IR_PULSE))
			break;
	}
	(*pos)++;	/* First event doesn't contain data */

	if (*pos >= len)
		return 0;

	/* First space should have 4.5 ms otherwise is not NEC protocol */
	if ((evs[*pos].delta.tv_nsec < MIN_START_TIME) |
	    (evs[*pos].delta.tv_nsec > MAX_START_TIME) |
	    (evs[*pos].type != IR_SPACE))
		goto err;

	/*
	 * FIXME: need to implement the repeat sequence
	 */

	count = 0;
	for ((*pos)++; *pos < len; (*pos)++) {
		int bit;

		if ((evs[*pos].delta.tv_nsec < MIN_PULSE_TIME) |
		    (evs[*pos].delta.tv_nsec > MAX_PULSE_TIME) |
		    (evs[*pos].type != IR_PULSE))
			goto err;

		if (++*pos >= len)
			goto err;
		if (evs[*pos].type != IR_SPACE)
			goto err;

		if ((evs[*pos].delta.tv_nsec > MIN_BIT1_TIME) &&
		    (evs[*pos].delta.tv_nsec < MAX_BIT1_TIME))
			bit = 1;
		else if ((evs[*pos].delta.tv_nsec > MIN_BIT0_TIME) &&
			 (evs[*pos].delta.tv_nsec < MAX_BIT0_TIME))
			bit = 0;
		else
			goto err;

		if (bit) {
			int shift = count;
			/* Address first, then command */
			if (shift < 8) {
				shift += 8;
				ircode |= 1 << shift;
			} else if (shift < 16) {
				not_code |= 1 << shift;
			} else if (shift < 24) {
				shift -= 16;
				ircode |= 1 << shift;
			} else {
				shift -= 24;
				not_code |= 1 << shift;
			}
		}
		if (++count == 32)
			break;
	}

	/*
	 * Fixme: may need to accept Extended NEC protocol?
	 */
	if ((ircode & ~not_code) != ircode) {
		IR_dprintk(1, "NEC checksum error: code 0x%04x, not-code 0x%04x\n",
			   ircode, not_code);
		return -EINVAL;
	}

	IR_dprintk(1, "NEC scancode 0x%04x\n", ircode);
	ir_keydown(input_dev, ircode);
	ir_keyup(input_dev);

	return ircode;
err:
	IR_dprintk(1, "NEC decoded failed at bit %d while decoding %luus time\n",
		   count, (evs[*pos].delta.tv_nsec + 500) / 1000);

	return -EINVAL;
}

/**
 * __ir_nec_decode() - Decodes all NEC pulsecodes on a given array
 * @input_dev:	the struct input_dev descriptor of the device
 * @evs:	event array with type/duration of pulse/space
 * @len:	length of the array
 * This function returns the number of decoded pulses or -EINVAL if no
 * pulse got decoded
 */
int ir_nec_decode(struct input_dev *input_dev,
			   struct ir_raw_event *evs,
			   int len)
{
	int pos = 0;
	int rc = 0;

	while (pos < len) {
		if (__ir_nec_decode(input_dev, evs, len, &pos) >= 0)
			rc++;
	}

	if (!rc)
		return -EINVAL;
	return rc;
}

EXPORT_SYMBOL_GPL(ir_nec_decode);
