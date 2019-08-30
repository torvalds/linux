// SPDX-License-Identifier: GPL-2.0+
// ir-rcmm-decoder.c - A decoder for the RCMM IR protocol
//
// Copyright (C) 2018 by Patrick Lerda <patrick9876@free.fr>

#include "rc-core-priv.h"
#include <linux/module.h>

#define RCMM_UNIT		166667	/* nanosecs */
#define RCMM_PREFIX_PULSE	416666  /* 166666.666666666*2.5 */
#define RCMM_PULSE_0            277777  /* 166666.666666666*(1+2/3) */
#define RCMM_PULSE_1            444444  /* 166666.666666666*(2+2/3) */
#define RCMM_PULSE_2            611111  /* 166666.666666666*(3+2/3) */
#define RCMM_PULSE_3            777778  /* 166666.666666666*(4+2/3) */

enum rcmm_state {
	STATE_INACTIVE,
	STATE_LOW,
	STATE_BUMP,
	STATE_VALUE,
	STATE_FINISHED,
};

static bool rcmm_mode(const struct rcmm_dec *data)
{
	return !((0x000c0000 & data->bits) == 0x000c0000);
}

static int rcmm_miscmode(struct rc_dev *dev, struct rcmm_dec *data)
{
	switch (data->count) {
	case 24:
		if (dev->enabled_protocols & RC_PROTO_BIT_RCMM24) {
			rc_keydown(dev, RC_PROTO_RCMM24, data->bits, 0);
			data->state = STATE_INACTIVE;
			return 0;
		}
		return -1;

	case 12:
		if (dev->enabled_protocols & RC_PROTO_BIT_RCMM12) {
			rc_keydown(dev, RC_PROTO_RCMM12, data->bits, 0);
			data->state = STATE_INACTIVE;
			return 0;
		}
		return -1;
	}

	return -1;
}

/**
 * ir_rcmm_decode() - Decode one RCMM pulse or space
 * @dev:	the struct rc_dev descriptor of the device
 * @ev:		the struct ir_raw_event descriptor of the pulse/space
 *
 * This function returns -EINVAL if the pulse violates the state machine
 */
static int ir_rcmm_decode(struct rc_dev *dev, struct ir_raw_event ev)
{
	struct rcmm_dec *data = &dev->raw->rcmm;
	u32 scancode;
	u8 toggle;
	int value;

	if (!(dev->enabled_protocols & (RC_PROTO_BIT_RCMM32 |
							RC_PROTO_BIT_RCMM24 |
							RC_PROTO_BIT_RCMM12)))
		return 0;

	if (!is_timing_event(ev)) {
		if (ev.reset)
			data->state = STATE_INACTIVE;
		return 0;
	}

	switch (data->state) {
	case STATE_INACTIVE:
		if (!ev.pulse)
			break;

		if (!eq_margin(ev.duration, RCMM_PREFIX_PULSE, RCMM_UNIT / 2))
			break;

		data->state = STATE_LOW;
		data->count = 0;
		data->bits  = 0;
		return 0;

	case STATE_LOW:
		if (ev.pulse)
			break;

		if (!eq_margin(ev.duration, RCMM_PULSE_0, RCMM_UNIT / 2))
			break;

		data->state = STATE_BUMP;
		return 0;

	case STATE_BUMP:
		if (!ev.pulse)
			break;

		if (!eq_margin(ev.duration, RCMM_UNIT, RCMM_UNIT / 2))
			break;

		data->state = STATE_VALUE;
		return 0;

	case STATE_VALUE:
		if (ev.pulse)
			break;

		if (eq_margin(ev.duration, RCMM_PULSE_0, RCMM_UNIT / 2))
			value = 0;
		else if (eq_margin(ev.duration, RCMM_PULSE_1, RCMM_UNIT / 2))
			value = 1;
		else if (eq_margin(ev.duration, RCMM_PULSE_2, RCMM_UNIT / 2))
			value = 2;
		else if (eq_margin(ev.duration, RCMM_PULSE_3, RCMM_UNIT / 2))
			value = 3;
		else
			value = -1;

		if (value == -1) {
			if (!rcmm_miscmode(dev, data))
				return 0;
			break;
		}

		data->bits <<= 2;
		data->bits |= value;

		data->count += 2;

		if (data->count < 32)
			data->state = STATE_BUMP;
		else
			data->state = STATE_FINISHED;

		return 0;

	case STATE_FINISHED:
		if (!ev.pulse)
			break;

		if (!eq_margin(ev.duration, RCMM_UNIT, RCMM_UNIT / 2))
			break;

		if (rcmm_mode(data)) {
			toggle = !!(0x8000 & data->bits);
			scancode = data->bits & ~0x8000;
		} else {
			toggle = 0;
			scancode = data->bits;
		}

		if (dev->enabled_protocols & RC_PROTO_BIT_RCMM32) {
			rc_keydown(dev, RC_PROTO_RCMM32, scancode, toggle);
			data->state = STATE_INACTIVE;
			return 0;
		}

		break;
	}

	data->state = STATE_INACTIVE;
	return -EINVAL;
}

static const int rcmmspace[] = {
	RCMM_PULSE_0,
	RCMM_PULSE_1,
	RCMM_PULSE_2,
	RCMM_PULSE_3,
};

static int ir_rcmm_rawencoder(struct ir_raw_event **ev, unsigned int max,
			      unsigned int n, u32 data)
{
	int i;
	int ret;

	ret = ir_raw_gen_pulse_space(ev, &max, RCMM_PREFIX_PULSE, RCMM_PULSE_0);
	if (ret)
		return ret;

	for (i = n - 2; i >= 0; i -= 2) {
		const unsigned int space = rcmmspace[(data >> i) & 3];

		ret = ir_raw_gen_pulse_space(ev, &max, RCMM_UNIT, space);
		if (ret)
			return ret;
	}

	return ir_raw_gen_pulse_space(ev, &max, RCMM_UNIT, RCMM_PULSE_3 * 2);
}

static int ir_rcmm_encode(enum rc_proto protocol, u32 scancode,
			  struct ir_raw_event *events, unsigned int max)
{
	struct ir_raw_event *e = events;
	int ret;

	switch (protocol) {
	case RC_PROTO_RCMM32:
		ret = ir_rcmm_rawencoder(&e, max, 32, scancode);
		break;
	case RC_PROTO_RCMM24:
		ret = ir_rcmm_rawencoder(&e, max, 24, scancode);
		break;
	case RC_PROTO_RCMM12:
		ret = ir_rcmm_rawencoder(&e, max, 12, scancode);
		break;
	default:
		ret = -EINVAL;
	}

	if (ret < 0)
		return ret;

	return e - events;
}

static struct ir_raw_handler rcmm_handler = {
	.protocols	= RC_PROTO_BIT_RCMM32 |
			  RC_PROTO_BIT_RCMM24 |
			  RC_PROTO_BIT_RCMM12,
	.decode		= ir_rcmm_decode,
	.encode         = ir_rcmm_encode,
	.carrier        = 36000,
	.min_timeout	= RCMM_PULSE_3 + RCMM_UNIT,
};

static int __init ir_rcmm_decode_init(void)
{
	ir_raw_handler_register(&rcmm_handler);

	pr_info("IR RCMM protocol handler initialized\n");
	return 0;
}

static void __exit ir_rcmm_decode_exit(void)
{
	ir_raw_handler_unregister(&rcmm_handler);
}

module_init(ir_rcmm_decode_init);
module_exit(ir_rcmm_decode_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Patrick Lerda");
MODULE_DESCRIPTION("RCMM IR protocol decoder");
