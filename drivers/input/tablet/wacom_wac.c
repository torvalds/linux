/*
 * drivers/input/tablet/wacom_wac.c
 *
 *  USB Wacom tablet support - Wacom specific code
 *
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "wacom.h"
#include "wacom_wac.h"

static int wacom_penpartner_irq(struct wacom_wac *wacom, void *wcombo)
{
	unsigned char *data = wacom->data;

	switch (data[0]) {
		case 1:
			if (data[5] & 0x80) {
				wacom->tool[0] = (data[5] & 0x20) ? BTN_TOOL_RUBBER : BTN_TOOL_PEN;
				wacom->id[0] = (data[5] & 0x20) ? ERASER_DEVICE_ID : STYLUS_DEVICE_ID;
				wacom_report_key(wcombo, wacom->tool[0], 1);
				wacom_report_abs(wcombo, ABS_MISC, wacom->id[0]); /* report tool id */
				wacom_report_abs(wcombo, ABS_X, wacom_le16_to_cpu(&data[1]));
				wacom_report_abs(wcombo, ABS_Y, wacom_le16_to_cpu(&data[3]));
				wacom_report_abs(wcombo, ABS_PRESSURE, (signed char)data[6] + 127);
				wacom_report_key(wcombo, BTN_TOUCH, ((signed char)data[6] > -127));
				wacom_report_key(wcombo, BTN_STYLUS, (data[5] & 0x40));
			} else {
				wacom_report_key(wcombo, wacom->tool[0], 0);
				wacom_report_abs(wcombo, ABS_MISC, 0); /* report tool id */
				wacom_report_abs(wcombo, ABS_PRESSURE, -1);
				wacom_report_key(wcombo, BTN_TOUCH, 0);
			}
			break;
		case 2:
			wacom_report_key(wcombo, BTN_TOOL_PEN, 1);
			wacom_report_abs(wcombo, ABS_MISC, STYLUS_DEVICE_ID); /* report tool id */
			wacom_report_abs(wcombo, ABS_X, wacom_le16_to_cpu(&data[1]));
			wacom_report_abs(wcombo, ABS_Y, wacom_le16_to_cpu(&data[3]));
			wacom_report_abs(wcombo, ABS_PRESSURE, (signed char)data[6] + 127);
			wacom_report_key(wcombo, BTN_TOUCH, ((signed char)data[6] > -80) && !(data[5] & 0x20));
			wacom_report_key(wcombo, BTN_STYLUS, (data[5] & 0x40));
			break;
		default:
			printk(KERN_INFO "wacom_penpartner_irq: received unknown report #%d\n", data[0]);
			return 0;
        }
	return 1;
}

static int wacom_pl_irq(struct wacom_wac *wacom, void *wcombo)
{
	struct wacom_features *features = &wacom->features;
	unsigned char *data = wacom->data;
	int prox, pressure;

	if (data[0] != WACOM_REPORT_PENABLED) {
		dbg("wacom_pl_irq: received unknown report #%d", data[0]);
		return 0;
	}

	prox = data[1] & 0x40;

	if (prox) {
		wacom->id[0] = ERASER_DEVICE_ID;
		pressure = (signed char)((data[7] << 1) | ((data[4] >> 2) & 1));
		if (features->pressure_max > 255)
			pressure = (pressure << 1) | ((data[4] >> 6) & 1);
		pressure += (features->pressure_max + 1) / 2;

		/*
		 * if going from out of proximity into proximity select between the eraser
		 * and the pen based on the state of the stylus2 button, choose eraser if
		 * pressed else choose pen. if not a proximity change from out to in, send
		 * an out of proximity for previous tool then a in for new tool.
		 */
		if (!wacom->tool[0]) {
			/* Eraser bit set for DTF */
			if (data[1] & 0x10)
				wacom->tool[1] = BTN_TOOL_RUBBER;
			else
				/* Going into proximity select tool */
				wacom->tool[1] = (data[4] & 0x20) ? BTN_TOOL_RUBBER : BTN_TOOL_PEN;
		} else {
			/* was entered with stylus2 pressed */
			if (wacom->tool[1] == BTN_TOOL_RUBBER && !(data[4] & 0x20)) {
				/* report out proximity for previous tool */
				wacom_report_key(wcombo, wacom->tool[1], 0);
				wacom_input_sync(wcombo);
				wacom->tool[1] = BTN_TOOL_PEN;
				return 0;
			}
		}
		if (wacom->tool[1] != BTN_TOOL_RUBBER) {
			/* Unknown tool selected default to pen tool */
			wacom->tool[1] = BTN_TOOL_PEN;
			wacom->id[0] = STYLUS_DEVICE_ID;
		}
		wacom_report_key(wcombo, wacom->tool[1], prox); /* report in proximity for tool */
		wacom_report_abs(wcombo, ABS_MISC, wacom->id[0]); /* report tool id */
		wacom_report_abs(wcombo, ABS_X, data[3] | (data[2] << 7) | ((data[1] & 0x03) << 14));
		wacom_report_abs(wcombo, ABS_Y, data[6] | (data[5] << 7) | ((data[4] & 0x03) << 14));
		wacom_report_abs(wcombo, ABS_PRESSURE, pressure);

		wacom_report_key(wcombo, BTN_TOUCH, data[4] & 0x08);
		wacom_report_key(wcombo, BTN_STYLUS, data[4] & 0x10);
		/* Only allow the stylus2 button to be reported for the pen tool. */
		wacom_report_key(wcombo, BTN_STYLUS2, (wacom->tool[1] == BTN_TOOL_PEN) && (data[4] & 0x20));
	} else {
		/* report proximity-out of a (valid) tool */
		if (wacom->tool[1] != BTN_TOOL_RUBBER) {
			/* Unknown tool selected default to pen tool */
			wacom->tool[1] = BTN_TOOL_PEN;
		}
		wacom_report_key(wcombo, wacom->tool[1], prox);
	}

	wacom->tool[0] = prox; /* Save proximity state */
	return 1;
}

static int wacom_ptu_irq(struct wacom_wac *wacom, void *wcombo)
{
	unsigned char *data = wacom->data;

	if (data[0] != WACOM_REPORT_PENABLED) {
		printk(KERN_INFO "wacom_ptu_irq: received unknown report #%d\n", data[0]);
		return 0;
	}

	if (data[1] & 0x04) {
		wacom_report_key(wcombo, BTN_TOOL_RUBBER, data[1] & 0x20);
		wacom_report_key(wcombo, BTN_TOUCH, data[1] & 0x08);
		wacom->id[0] = ERASER_DEVICE_ID;
	} else {
		wacom_report_key(wcombo, BTN_TOOL_PEN, data[1] & 0x20);
		wacom_report_key(wcombo, BTN_TOUCH, data[1] & 0x01);
		wacom->id[0] = STYLUS_DEVICE_ID;
	}
	wacom_report_abs(wcombo, ABS_MISC, wacom->id[0]); /* report tool id */
	wacom_report_abs(wcombo, ABS_X, wacom_le16_to_cpu(&data[2]));
	wacom_report_abs(wcombo, ABS_Y, wacom_le16_to_cpu(&data[4]));
	wacom_report_abs(wcombo, ABS_PRESSURE, wacom_le16_to_cpu(&data[6]));
	wacom_report_key(wcombo, BTN_STYLUS, data[1] & 0x02);
	wacom_report_key(wcombo, BTN_STYLUS2, data[1] & 0x10);
	return 1;
}

static int wacom_graphire_irq(struct wacom_wac *wacom, void *wcombo)
{
	struct wacom_features *features = &wacom->features;
	unsigned char *data = wacom->data;
	int x, y, rw;
	static int penData = 0;

	if (data[0] != WACOM_REPORT_PENABLED) {
		dbg("wacom_graphire_irq: received unknown report #%d", data[0]);
		return 0;
	}

	if (data[1] & 0x80) {
		/* in prox and not a pad data */
		penData = 1;

		switch ((data[1] >> 5) & 3) {

			case 0:	/* Pen */
				wacom->tool[0] = BTN_TOOL_PEN;
				wacom->id[0] = STYLUS_DEVICE_ID;
				break;

			case 1: /* Rubber */
				wacom->tool[0] = BTN_TOOL_RUBBER;
				wacom->id[0] = ERASER_DEVICE_ID;
				break;

			case 2: /* Mouse with wheel */
				wacom_report_key(wcombo, BTN_MIDDLE, data[1] & 0x04);
				if (features->type == WACOM_G4 || features->type == WACOM_MO) {
					rw = data[7] & 0x04 ? (data[7] & 0x03)-4 : (data[7] & 0x03);
					wacom_report_rel(wcombo, REL_WHEEL, -rw);
				} else
					wacom_report_rel(wcombo, REL_WHEEL, -(signed char) data[6]);
				/* fall through */

			case 3: /* Mouse without wheel */
				wacom->tool[0] = BTN_TOOL_MOUSE;
				wacom->id[0] = CURSOR_DEVICE_ID;
				wacom_report_key(wcombo, BTN_LEFT, data[1] & 0x01);
				wacom_report_key(wcombo, BTN_RIGHT, data[1] & 0x02);
				if (features->type == WACOM_G4 || features->type == WACOM_MO)
					wacom_report_abs(wcombo, ABS_DISTANCE, data[6] & 0x3f);
				else
					wacom_report_abs(wcombo, ABS_DISTANCE, data[7] & 0x3f);
				break;
		}
		x = wacom_le16_to_cpu(&data[2]);
		y = wacom_le16_to_cpu(&data[4]);
		wacom_report_abs(wcombo, ABS_X, x);
		wacom_report_abs(wcombo, ABS_Y, y);
		if (wacom->tool[0] != BTN_TOOL_MOUSE) {
			wacom_report_abs(wcombo, ABS_PRESSURE, data[6] | ((data[7] & 0x01) << 8));
			wacom_report_key(wcombo, BTN_TOUCH, data[1] & 0x01);
			wacom_report_key(wcombo, BTN_STYLUS, data[1] & 0x02);
			wacom_report_key(wcombo, BTN_STYLUS2, data[1] & 0x04);
		}
		wacom_report_abs(wcombo, ABS_MISC, wacom->id[0]); /* report tool id */
		wacom_report_key(wcombo, wacom->tool[0], 1);
	} else if (wacom->id[0]) {
		wacom_report_abs(wcombo, ABS_X, 0);
		wacom_report_abs(wcombo, ABS_Y, 0);
		if (wacom->tool[0] == BTN_TOOL_MOUSE) {
			wacom_report_key(wcombo, BTN_LEFT, 0);
			wacom_report_key(wcombo, BTN_RIGHT, 0);
			wacom_report_abs(wcombo, ABS_DISTANCE, 0);
		} else {
			wacom_report_abs(wcombo, ABS_PRESSURE, 0);
			wacom_report_key(wcombo, BTN_TOUCH, 0);
			wacom_report_key(wcombo, BTN_STYLUS, 0);
			wacom_report_key(wcombo, BTN_STYLUS2, 0);
		}
		wacom->id[0] = 0;
		wacom_report_abs(wcombo, ABS_MISC, 0); /* reset tool id */
		wacom_report_key(wcombo, wacom->tool[0], 0);
	}

	/* send pad data */
	switch (features->type) {
	    case WACOM_G4:
		if (data[7] & 0xf8) {
			if (penData) {
				wacom_input_sync(wcombo); /* sync last event */
				if (!wacom->id[0])
					penData = 0;
			}
			wacom->id[1] = PAD_DEVICE_ID;
			wacom_report_key(wcombo, BTN_0, (data[7] & 0x40));
			wacom_report_key(wcombo, BTN_4, (data[7] & 0x80));
			rw = ((data[7] & 0x18) >> 3) - ((data[7] & 0x20) >> 3);
			wacom_report_rel(wcombo, REL_WHEEL, rw);
			wacom_report_key(wcombo, BTN_TOOL_FINGER, 0xf0);
			wacom_report_abs(wcombo, ABS_MISC, wacom->id[1]);
			wacom_input_event(wcombo, EV_MSC, MSC_SERIAL, 0xf0);
		} else if (wacom->id[1]) {
			if (penData) {
				wacom_input_sync(wcombo); /* sync last event */
				if (!wacom->id[0])
					penData = 0;
			}
			wacom->id[1] = 0;
			wacom_report_key(wcombo, BTN_0, (data[7] & 0x40));
			wacom_report_key(wcombo, BTN_4, (data[7] & 0x80));
			wacom_report_rel(wcombo, REL_WHEEL, 0);
			wacom_report_key(wcombo, BTN_TOOL_FINGER, 0);
			wacom_report_abs(wcombo, ABS_MISC, 0);
			wacom_input_event(wcombo, EV_MSC, MSC_SERIAL, 0xf0);
		}
		break;
	    case WACOM_MO:
		if ((data[7] & 0xf8) || (data[8] & 0xff)) {
			if (penData) {
				wacom_input_sync(wcombo); /* sync last event */
				if (!wacom->id[0])
					penData = 0;
			}
			wacom->id[1] = PAD_DEVICE_ID;
			wacom_report_key(wcombo, BTN_0, (data[7] & 0x08));
			wacom_report_key(wcombo, BTN_1, (data[7] & 0x20));
			wacom_report_key(wcombo, BTN_4, (data[7] & 0x10));
			wacom_report_key(wcombo, BTN_5, (data[7] & 0x40));
			wacom_report_abs(wcombo, ABS_WHEEL, (data[8] & 0x7f));
			wacom_report_key(wcombo, BTN_TOOL_FINGER, 0xf0);
			wacom_report_abs(wcombo, ABS_MISC, wacom->id[1]);
			wacom_input_event(wcombo, EV_MSC, MSC_SERIAL, 0xf0);
		} else if (wacom->id[1]) {
			if (penData) {
				wacom_input_sync(wcombo); /* sync last event */
				if (!wacom->id[0])
					penData = 0;
			}
			wacom->id[1] = 0;
			wacom_report_key(wcombo, BTN_0, (data[7] & 0x08));
			wacom_report_key(wcombo, BTN_1, (data[7] & 0x20));
			wacom_report_key(wcombo, BTN_4, (data[7] & 0x10));
			wacom_report_key(wcombo, BTN_5, (data[7] & 0x40));
			wacom_report_abs(wcombo, ABS_WHEEL, (data[8] & 0x7f));
			wacom_report_key(wcombo, BTN_TOOL_FINGER, 0);
			wacom_report_abs(wcombo, ABS_MISC, 0);
			wacom_input_event(wcombo, EV_MSC, MSC_SERIAL, 0xf0);
		}
		break;
	}
	return 1;
}

static int wacom_intuos_inout(struct wacom_wac *wacom, void *wcombo)
{
	struct wacom_features *features = &wacom->features;
	unsigned char *data = wacom->data;
	int idx = 0;

	/* tool number */
	if (features->type == INTUOS)
		idx = data[1] & 0x01;

	/* Enter report */
	if ((data[1] & 0xfc) == 0xc0) {
		/* serial number of the tool */
		wacom->serial[idx] = ((data[3] & 0x0f) << 28) +
			(data[4] << 20) + (data[5] << 12) +
			(data[6] << 4) + (data[7] >> 4);

		wacom->id[idx] = (data[2] << 4) | (data[3] >> 4);
		switch (wacom->id[idx]) {
			case 0x812: /* Inking pen */
			case 0x801: /* Intuos3 Inking pen */
			case 0x20802: /* Intuos4 Classic Pen */
			case 0x012:
				wacom->tool[idx] = BTN_TOOL_PENCIL;
				break;
			case 0x822: /* Pen */
			case 0x842:
			case 0x852:
			case 0x823: /* Intuos3 Grip Pen */
			case 0x813: /* Intuos3 Classic Pen */
			case 0x885: /* Intuos3 Marker Pen */
			case 0x802: /* Intuos4 Grip Pen Eraser */
			case 0x804: /* Intuos4 Marker Pen */
			case 0x40802: /* Intuos4 Classic Pen */
			case 0x022:
				wacom->tool[idx] = BTN_TOOL_PEN;
				break;
			case 0x832: /* Stroke pen */
			case 0x032:
				wacom->tool[idx] = BTN_TOOL_BRUSH;
				break;
			case 0x007: /* Mouse 4D and 2D */
		        case 0x09c:
			case 0x094:
			case 0x017: /* Intuos3 2D Mouse */
			case 0x806: /* Intuos4 Mouse */
				wacom->tool[idx] = BTN_TOOL_MOUSE;
				break;
			case 0x096: /* Lens cursor */
			case 0x097: /* Intuos3 Lens cursor */
			case 0x006: /* Intuos4 Lens cursor */
				wacom->tool[idx] = BTN_TOOL_LENS;
				break;
			case 0x82a: /* Eraser */
			case 0x85a:
		        case 0x91a:
			case 0xd1a:
			case 0x0fa:
			case 0x82b: /* Intuos3 Grip Pen Eraser */
			case 0x81b: /* Intuos3 Classic Pen Eraser */
			case 0x91b: /* Intuos3 Airbrush Eraser */
			case 0x80c: /* Intuos4 Marker Pen Eraser */
			case 0x80a: /* Intuos4 Grip Pen Eraser */
			case 0x4080a: /* Intuos4 Classic Pen Eraser */
			case 0x90a: /* Intuos4 Airbrush Eraser */
				wacom->tool[idx] = BTN_TOOL_RUBBER;
				break;
			case 0xd12:
			case 0x912:
			case 0x112:
			case 0x913: /* Intuos3 Airbrush */
			case 0x902: /* Intuos4 Airbrush */
				wacom->tool[idx] = BTN_TOOL_AIRBRUSH;
				break;
			default: /* Unknown tool */
				wacom->tool[idx] = BTN_TOOL_PEN;
		}
		return 1;
	}

	/* Exit report */
	if ((data[1] & 0xfe) == 0x80) {
		/*
		 * Reset all states otherwise we lose the initial states
		 * when in-prox next time
		 */
		wacom_report_abs(wcombo, ABS_X, 0);
		wacom_report_abs(wcombo, ABS_Y, 0);
		wacom_report_abs(wcombo, ABS_DISTANCE, 0);
		wacom_report_abs(wcombo, ABS_TILT_X, 0);
		wacom_report_abs(wcombo, ABS_TILT_Y, 0);
		if (wacom->tool[idx] >= BTN_TOOL_MOUSE) {
			wacom_report_key(wcombo, BTN_LEFT, 0);
			wacom_report_key(wcombo, BTN_MIDDLE, 0);
			wacom_report_key(wcombo, BTN_RIGHT, 0);
			wacom_report_key(wcombo, BTN_SIDE, 0);
			wacom_report_key(wcombo, BTN_EXTRA, 0);
			wacom_report_abs(wcombo, ABS_THROTTLE, 0);
			wacom_report_abs(wcombo, ABS_RZ, 0);
		} else {
			wacom_report_abs(wcombo, ABS_PRESSURE, 0);
			wacom_report_key(wcombo, BTN_STYLUS, 0);
			wacom_report_key(wcombo, BTN_STYLUS2, 0);
			wacom_report_key(wcombo, BTN_TOUCH, 0);
			wacom_report_abs(wcombo, ABS_WHEEL, 0);
			if (features->type >= INTUOS3S)
				wacom_report_abs(wcombo, ABS_Z, 0);
		}
		wacom_report_key(wcombo, wacom->tool[idx], 0);
		wacom_report_abs(wcombo, ABS_MISC, 0); /* reset tool id */
		wacom_input_event(wcombo, EV_MSC, MSC_SERIAL, wacom->serial[idx]);
		wacom->id[idx] = 0;
		return 2;
	}
	return 0;
}

static void wacom_intuos_general(struct wacom_wac *wacom, void *wcombo)
{
	struct wacom_features *features = &wacom->features;
	unsigned char *data = wacom->data;
	unsigned int t;

	/* general pen packet */
	if ((data[1] & 0xb8) == 0xa0) {
		t = (data[6] << 2) | ((data[7] >> 6) & 3);
		if (features->type >= INTUOS4S && features->type <= INTUOS4L)
			t = (t << 1) | (data[1] & 1);
		wacom_report_abs(wcombo, ABS_PRESSURE, t);
		wacom_report_abs(wcombo, ABS_TILT_X,
				((data[7] << 1) & 0x7e) | (data[8] >> 7));
		wacom_report_abs(wcombo, ABS_TILT_Y, data[8] & 0x7f);
		wacom_report_key(wcombo, BTN_STYLUS, data[1] & 2);
		wacom_report_key(wcombo, BTN_STYLUS2, data[1] & 4);
		wacom_report_key(wcombo, BTN_TOUCH, t > 10);
	}

	/* airbrush second packet */
	if ((data[1] & 0xbc) == 0xb4) {
		wacom_report_abs(wcombo, ABS_WHEEL,
				(data[6] << 2) | ((data[7] >> 6) & 3));
		wacom_report_abs(wcombo, ABS_TILT_X,
				((data[7] << 1) & 0x7e) | (data[8] >> 7));
		wacom_report_abs(wcombo, ABS_TILT_Y, data[8] & 0x7f);
	}
	return;
}

static int wacom_intuos_irq(struct wacom_wac *wacom, void *wcombo)
{
	struct wacom_features *features = &wacom->features;
	unsigned char *data = wacom->data;
	unsigned int t;
	int idx = 0, result;

	if (data[0] != WACOM_REPORT_PENABLED && data[0] != WACOM_REPORT_INTUOSREAD
		&& data[0] != WACOM_REPORT_INTUOSWRITE && data[0] != WACOM_REPORT_INTUOSPAD) {
		dbg("wacom_intuos_irq: received unknown report #%d", data[0]);
                return 0;
	}

	/* tool number */
	if (features->type == INTUOS)
		idx = data[1] & 0x01;

	/* pad packets. Works as a second tool and is always in prox */
	if (data[0] == WACOM_REPORT_INTUOSPAD) {
		/* initiate the pad as a device */
		if (wacom->tool[1] != BTN_TOOL_FINGER)
			wacom->tool[1] = BTN_TOOL_FINGER;

		if (features->type >= INTUOS4S && features->type <= INTUOS4L) {
			wacom_report_key(wcombo, BTN_0, (data[2] & 0x01));
			wacom_report_key(wcombo, BTN_1, (data[3] & 0x01));
			wacom_report_key(wcombo, BTN_2, (data[3] & 0x02));
			wacom_report_key(wcombo, BTN_3, (data[3] & 0x04));
			wacom_report_key(wcombo, BTN_4, (data[3] & 0x08));
			wacom_report_key(wcombo, BTN_5, (data[3] & 0x10));
			wacom_report_key(wcombo, BTN_6, (data[3] & 0x20));
			if (data[1] & 0x80) {
				wacom_report_abs(wcombo, ABS_WHEEL, (data[1] & 0x7f));
			} else {
				/* Out of proximity, clear wheel value. */
				wacom_report_abs(wcombo, ABS_WHEEL, 0);
			}
			if (features->type != INTUOS4S) {
				wacom_report_key(wcombo, BTN_7, (data[3] & 0x40));
				wacom_report_key(wcombo, BTN_8, (data[3] & 0x80));
			}
			if (data[1] | (data[2] & 0x01) | data[3]) {
				wacom_report_key(wcombo, wacom->tool[1], 1);
				wacom_report_abs(wcombo, ABS_MISC, PAD_DEVICE_ID);
			} else {
				wacom_report_key(wcombo, wacom->tool[1], 0);
				wacom_report_abs(wcombo, ABS_MISC, 0);
			}
		} else {
			wacom_report_key(wcombo, BTN_0, (data[5] & 0x01));
			wacom_report_key(wcombo, BTN_1, (data[5] & 0x02));
			wacom_report_key(wcombo, BTN_2, (data[5] & 0x04));
			wacom_report_key(wcombo, BTN_3, (data[5] & 0x08));
			wacom_report_key(wcombo, BTN_4, (data[6] & 0x01));
			wacom_report_key(wcombo, BTN_5, (data[6] & 0x02));
			wacom_report_key(wcombo, BTN_6, (data[6] & 0x04));
			wacom_report_key(wcombo, BTN_7, (data[6] & 0x08));
			wacom_report_key(wcombo, BTN_8, (data[5] & 0x10));
			wacom_report_key(wcombo, BTN_9, (data[6] & 0x10));
			wacom_report_abs(wcombo, ABS_RX, ((data[1] & 0x1f) << 8) | data[2]);
			wacom_report_abs(wcombo, ABS_RY, ((data[3] & 0x1f) << 8) | data[4]);

			if ((data[5] & 0x1f) | (data[6] & 0x1f) | (data[1] & 0x1f) |
				data[2] | (data[3] & 0x1f) | data[4]) {
				wacom_report_key(wcombo, wacom->tool[1], 1);
				wacom_report_abs(wcombo, ABS_MISC, PAD_DEVICE_ID);
			} else {
				wacom_report_key(wcombo, wacom->tool[1], 0);
				wacom_report_abs(wcombo, ABS_MISC, 0);
			}
		}
		wacom_input_event(wcombo, EV_MSC, MSC_SERIAL, 0xffffffff);
                return 1;
	}

	/* process in/out prox events */
	result = wacom_intuos_inout(wacom, wcombo);
	if (result)
                return result-1;

	/* don't proceed if we don't know the ID */
	if (!wacom->id[idx])
		return 0;

	/* Only large Intuos support Lense Cursor */
	if (wacom->tool[idx] == BTN_TOOL_LENS &&
	    (features->type == INTUOS3 ||
	     features->type == INTUOS3S ||
	     features->type == INTUOS4 ||
	     features->type == INTUOS4S)) {

		return 0;
	}

	/* Cintiq doesn't send data when RDY bit isn't set */
	if (features->type == CINTIQ && !(data[1] & 0x40))
                 return 0;

	if (features->type >= INTUOS3S) {
		wacom_report_abs(wcombo, ABS_X, (data[2] << 9) | (data[3] << 1) | ((data[9] >> 1) & 1));
		wacom_report_abs(wcombo, ABS_Y, (data[4] << 9) | (data[5] << 1) | (data[9] & 1));
		wacom_report_abs(wcombo, ABS_DISTANCE, ((data[9] >> 2) & 0x3f));
	} else {
		wacom_report_abs(wcombo, ABS_X, wacom_be16_to_cpu(&data[2]));
		wacom_report_abs(wcombo, ABS_Y, wacom_be16_to_cpu(&data[4]));
		wacom_report_abs(wcombo, ABS_DISTANCE, ((data[9] >> 3) & 0x1f));
	}

	/* process general packets */
	wacom_intuos_general(wacom, wcombo);

	/* 4D mouse, 2D mouse, marker pen rotation, tilt mouse, or Lens cursor packets */
	if ((data[1] & 0xbc) == 0xa8 || (data[1] & 0xbe) == 0xb0 || (data[1] & 0xbc) == 0xac) {

		if (data[1] & 0x02) {
			/* Rotation packet */
			if (features->type >= INTUOS3S) {
				/* I3 marker pen rotation */
				t = (data[6] << 3) | ((data[7] >> 5) & 7);
				t = (data[7] & 0x20) ? ((t > 900) ? ((t-1) / 2 - 1350) :
					((t-1) / 2 + 450)) : (450 - t / 2) ;
				wacom_report_abs(wcombo, ABS_Z, t);
			} else {
				/* 4D mouse rotation packet */
				t = (data[6] << 3) | ((data[7] >> 5) & 7);
				wacom_report_abs(wcombo, ABS_RZ, (data[7] & 0x20) ?
					((t - 1) / 2) : -t / 2);
			}

		} else if (!(data[1] & 0x10) && features->type < INTUOS3S) {
			/* 4D mouse packet */
			wacom_report_key(wcombo, BTN_LEFT,   data[8] & 0x01);
			wacom_report_key(wcombo, BTN_MIDDLE, data[8] & 0x02);
			wacom_report_key(wcombo, BTN_RIGHT,  data[8] & 0x04);

			wacom_report_key(wcombo, BTN_SIDE,   data[8] & 0x20);
			wacom_report_key(wcombo, BTN_EXTRA,  data[8] & 0x10);
			t = (data[6] << 2) | ((data[7] >> 6) & 3);
			wacom_report_abs(wcombo, ABS_THROTTLE, (data[8] & 0x08) ? -t : t);

		} else if (wacom->tool[idx] == BTN_TOOL_MOUSE) {
			/* I4 mouse */
			if (features->type >= INTUOS4S && features->type <= INTUOS4L) {
				wacom_report_key(wcombo, BTN_LEFT,   data[6] & 0x01);
				wacom_report_key(wcombo, BTN_MIDDLE, data[6] & 0x02);
				wacom_report_key(wcombo, BTN_RIGHT,  data[6] & 0x04);
				wacom_report_rel(wcombo, REL_WHEEL, ((data[7] & 0x80) >> 7)
						 - ((data[7] & 0x40) >> 6));
				wacom_report_key(wcombo, BTN_SIDE,   data[6] & 0x08);
				wacom_report_key(wcombo, BTN_EXTRA,  data[6] & 0x10);

				wacom_report_abs(wcombo, ABS_TILT_X,
					((data[7] << 1) & 0x7e) | (data[8] >> 7));
				wacom_report_abs(wcombo, ABS_TILT_Y, data[8] & 0x7f);
			} else {
				/* 2D mouse packet */
				wacom_report_key(wcombo, BTN_LEFT,   data[8] & 0x04);
				wacom_report_key(wcombo, BTN_MIDDLE, data[8] & 0x08);
				wacom_report_key(wcombo, BTN_RIGHT,  data[8] & 0x10);
				wacom_report_rel(wcombo, REL_WHEEL, (data[8] & 0x01)
						 - ((data[8] & 0x02) >> 1));

				/* I3 2D mouse side buttons */
				if (features->type >= INTUOS3S && features->type <= INTUOS3L) {
					wacom_report_key(wcombo, BTN_SIDE,   data[8] & 0x40);
					wacom_report_key(wcombo, BTN_EXTRA,  data[8] & 0x20);
				}
			}
		} else if ((features->type < INTUOS3S || features->type == INTUOS3L ||
				features->type == INTUOS4L) &&
			   wacom->tool[idx] == BTN_TOOL_LENS) {
			/* Lens cursor packets */
			wacom_report_key(wcombo, BTN_LEFT,   data[8] & 0x01);
			wacom_report_key(wcombo, BTN_MIDDLE, data[8] & 0x02);
			wacom_report_key(wcombo, BTN_RIGHT,  data[8] & 0x04);
			wacom_report_key(wcombo, BTN_SIDE,   data[8] & 0x10);
			wacom_report_key(wcombo, BTN_EXTRA,  data[8] & 0x08);
		}
	}

	wacom_report_abs(wcombo, ABS_MISC, wacom->id[idx]); /* report tool id */
	wacom_report_key(wcombo, wacom->tool[idx], 1);
	wacom_input_event(wcombo, EV_MSC, MSC_SERIAL, wacom->serial[idx]);
	return 1;
}


static void wacom_tpc_finger_in(struct wacom_wac *wacom, void *wcombo, char *data, int idx)
{
	wacom_report_abs(wcombo, ABS_X,
		(data[2 + idx * 2] & 0xff) | ((data[3 + idx * 2] & 0x7f) << 8));
	wacom_report_abs(wcombo, ABS_Y,
		(data[6 + idx * 2] & 0xff) | ((data[7 + idx * 2] & 0x7f) << 8));
	wacom_report_abs(wcombo, ABS_MISC, wacom->id[0]);
	wacom_report_key(wcombo, wacom->tool[idx], 1);
	if (idx)
		wacom_input_event(wcombo, EV_MSC, MSC_SERIAL, 0xf0);
	else
		wacom_report_key(wcombo, BTN_TOUCH, 1);
}

static void wacom_tpc_touch_out(struct wacom_wac *wacom, void *wcombo, int idx)
{
	wacom_report_abs(wcombo, ABS_X, 0);
	wacom_report_abs(wcombo, ABS_Y, 0);
	wacom_report_abs(wcombo, ABS_MISC, 0);
	wacom_report_key(wcombo, wacom->tool[idx], 0);
	if (idx)
		wacom_input_event(wcombo, EV_MSC, MSC_SERIAL, 0xf0);
	else
		wacom_report_key(wcombo, BTN_TOUCH, 0);
	return;
}

static void wacom_tpc_touch_in(struct wacom_wac *wacom, void *wcombo)
{
	char *data = wacom->data;
	struct urb *urb = ((struct wacom_combo *)wcombo)->urb;
	static int firstFinger = 0;
	static int secondFinger = 0;

	wacom->tool[0] = BTN_TOOL_DOUBLETAP;
	wacom->id[0] = TOUCH_DEVICE_ID;
	wacom->tool[1] = BTN_TOOL_TRIPLETAP;

	if (urb->actual_length != WACOM_PKGLEN_TPC1FG) {
		switch (data[0]) {
			case WACOM_REPORT_TPC1FG:
				wacom_report_abs(wcombo, ABS_X, wacom_le16_to_cpu(&data[2]));
				wacom_report_abs(wcombo, ABS_Y, wacom_le16_to_cpu(&data[4]));
				wacom_report_abs(wcombo, ABS_PRESSURE, wacom_le16_to_cpu(&data[6]));
				wacom_report_key(wcombo, BTN_TOUCH, wacom_le16_to_cpu(&data[6]));
				wacom_report_abs(wcombo, ABS_MISC, wacom->id[0]);
				wacom_report_key(wcombo, wacom->tool[0], 1);
				break;
			case WACOM_REPORT_TPC2FG:
				/* keep this byte to send proper out-prox event */
				wacom->id[1] = data[1] & 0x03;

				if (data[1] & 0x01) {
					wacom_tpc_finger_in(wacom, wcombo, data, 0);
					firstFinger = 1;
				} else if (firstFinger) {
					wacom_tpc_touch_out(wacom, wcombo, 0);
				}

				if (data[1] & 0x02) {
					/* sync first finger data */
					if (firstFinger)
						wacom_input_sync(wcombo);

					wacom_tpc_finger_in(wacom, wcombo, data, 1);
					secondFinger = 1;
				} else if (secondFinger) {
					/* sync first finger data */
					if (firstFinger)
						wacom_input_sync(wcombo);

					wacom_tpc_touch_out(wacom, wcombo, 1);
					secondFinger = 0;
				}
				if (!(data[1] & 0x01))
					firstFinger = 0;
				break;
		}
	} else {
		wacom_report_abs(wcombo, ABS_X, wacom_le16_to_cpu(&data[1]));
		wacom_report_abs(wcombo, ABS_Y, wacom_le16_to_cpu(&data[3]));
		wacom_report_key(wcombo, BTN_TOUCH, 1);
		wacom_report_abs(wcombo, ABS_MISC, wacom->id[0]);
		wacom_report_key(wcombo, wacom->tool[0], 1);
	}
	return;
}

static int wacom_tpc_irq(struct wacom_wac *wacom, void *wcombo)
{
	struct wacom_features *features = &wacom->features;
	char *data = wacom->data;
	int prox = 0, pressure, idx = -1;
	static int stylusInProx, touchInProx = 1, touchOut;
	struct urb *urb = ((struct wacom_combo *)wcombo)->urb;

	dbg("wacom_tpc_irq: received report #%d", data[0]);

	if (urb->actual_length == WACOM_PKGLEN_TPC1FG || /* single touch */
	    data[0] == WACOM_REPORT_TPC1FG ||		 /* single touch */
	    data[0] == WACOM_REPORT_TPC2FG) {		 /* 2FG touch */
		if (urb->actual_length == WACOM_PKGLEN_TPC1FG) {  /* with touch */
			prox = data[0] & 0x01;
		} else {  /* with capacity */
			if (data[0] == WACOM_REPORT_TPC1FG)
				/* single touch */
				prox = data[1] & 0x01;
			else
				/* 2FG touch data */
				prox = data[1] & 0x03;
		}

		if (!stylusInProx) { /* stylus not in prox */
			if (prox) {
				if (touchInProx) {
					wacom_tpc_touch_in(wacom, wcombo);
					touchOut = 1;
					return 1;
				}
			} else {
				/* 2FGT out-prox */
				if (data[0] == WACOM_REPORT_TPC2FG) {
					idx = (wacom->id[1] & 0x01) - 1;
					if (idx == 0) {
						wacom_tpc_touch_out(wacom, wcombo, idx);
						/* sync first finger event */
						if (wacom->id[1] & 0x02)
							wacom_input_sync(wcombo);
					}
					idx = (wacom->id[1] & 0x02) - 1;
					if (idx == 1)
						wacom_tpc_touch_out(wacom, wcombo, idx);
				} else /* one finger touch */
					wacom_tpc_touch_out(wacom, wcombo, 0);
				touchOut = 0;
				touchInProx = 1;
				return 1;
			}
		} else if (touchOut || !prox) { /* force touch out-prox */
			wacom_tpc_touch_out(wacom, wcombo, 0);
			touchOut = 0;
			touchInProx = 1;
			return 1;
		}
	} else if (data[0] == WACOM_REPORT_PENABLED) { /* Penabled */
		prox = data[1] & 0x20;

		touchInProx = 0;

		if (prox) { /* in prox */
			if (!wacom->id[0]) {
				/* Going into proximity select tool */
				wacom->tool[0] = (data[1] & 0x0c) ? BTN_TOOL_RUBBER : BTN_TOOL_PEN;
				if (wacom->tool[0] == BTN_TOOL_PEN)
					wacom->id[0] = STYLUS_DEVICE_ID;
				else
					wacom->id[0] = ERASER_DEVICE_ID;
			}
			wacom_report_key(wcombo, BTN_STYLUS, data[1] & 0x02);
			wacom_report_key(wcombo, BTN_STYLUS2, data[1] & 0x10);
			wacom_report_abs(wcombo, ABS_X, wacom_le16_to_cpu(&data[2]));
			wacom_report_abs(wcombo, ABS_Y, wacom_le16_to_cpu(&data[4]));
			pressure = ((data[7] & 0x01) << 8) | data[6];
			if (pressure < 0)
				pressure = features->pressure_max + pressure + 1;
			wacom_report_abs(wcombo, ABS_PRESSURE, pressure);
			wacom_report_key(wcombo, BTN_TOUCH, data[1] & 0x05);
		} else {
			wacom_report_abs(wcombo, ABS_X, 0);
			wacom_report_abs(wcombo, ABS_Y, 0);
			wacom_report_abs(wcombo, ABS_PRESSURE, 0);
			wacom_report_key(wcombo, BTN_STYLUS, 0);
			wacom_report_key(wcombo, BTN_STYLUS2, 0);
			wacom_report_key(wcombo, BTN_TOUCH, 0);
			wacom->id[0] = 0;
			/* pen is out so touch can be enabled now */
			touchInProx = 1;
		}
		wacom_report_key(wcombo, wacom->tool[0], prox);
		wacom_report_abs(wcombo, ABS_MISC, wacom->id[0]);
		stylusInProx = prox;
		return 1;
	}
	return 0;
}

int wacom_wac_irq(struct wacom_wac *wacom_wac, void *wcombo)
{
	switch (wacom_wac->features.type) {
		case PENPARTNER:
			return wacom_penpartner_irq(wacom_wac, wcombo);

		case PL:
			return wacom_pl_irq(wacom_wac, wcombo);

		case WACOM_G4:
		case GRAPHIRE:
		case WACOM_MO:
			return wacom_graphire_irq(wacom_wac, wcombo);

		case PTU:
			return wacom_ptu_irq(wacom_wac, wcombo);

		case INTUOS:
		case INTUOS3S:
		case INTUOS3:
		case INTUOS3L:
		case INTUOS4S:
		case INTUOS4:
		case INTUOS4L:
		case CINTIQ:
		case WACOM_BEE:
			return wacom_intuos_irq(wacom_wac, wcombo);

		case TABLETPC:
		case TABLETPC2FG:
			return wacom_tpc_irq(wacom_wac, wcombo);

		default:
			return 0;
	}
	return 0;
}

void wacom_init_input_dev(struct input_dev *input_dev, struct wacom_wac *wacom_wac)
{
	switch (wacom_wac->features.type) {
		case WACOM_MO:
			input_dev_mo(input_dev, wacom_wac);
		case WACOM_G4:
			input_dev_g4(input_dev, wacom_wac);
			/* fall through */
		case GRAPHIRE:
			input_dev_g(input_dev, wacom_wac);
			break;
		case WACOM_BEE:
			input_dev_bee(input_dev, wacom_wac);
		case INTUOS3:
		case INTUOS3L:
		case CINTIQ:
			input_dev_i3(input_dev, wacom_wac);
			/* fall through */
		case INTUOS3S:
			input_dev_i3s(input_dev, wacom_wac);
			/* fall through */
		case INTUOS:
			input_dev_i(input_dev, wacom_wac);
			break;
		case INTUOS4:
		case INTUOS4L:
			input_dev_i4(input_dev, wacom_wac);
			/* fall through */
		case INTUOS4S:
			input_dev_i4s(input_dev, wacom_wac);
			input_dev_i(input_dev, wacom_wac);
			break;
		case TABLETPC2FG:
			input_dev_tpc2fg(input_dev, wacom_wac);
			/* fall through */
		case TABLETPC:
			input_dev_tpc(input_dev, wacom_wac);
			if (wacom_wac->features.device_type != BTN_TOOL_PEN)
				break;  /* no need to process stylus stuff */

			/* fall through */
		case PL:
		case PTU:
			input_dev_pl(input_dev, wacom_wac);
			/* fall through */
		case PENPARTNER:
			input_dev_pt(input_dev, wacom_wac);
			break;
	}
	return;
}

static const struct wacom_features wacom_features_0x00 =
	{ "Wacom Penpartner",     WACOM_PKGLEN_PENPRTN,    5040,  3780,  255,  0, PENPARTNER };
static const struct wacom_features wacom_features_0x10 =
	{ "Wacom Graphire",       WACOM_PKGLEN_GRAPHIRE,  10206,  7422,  511, 63, GRAPHIRE };
static const struct wacom_features wacom_features_0x11 =
	{ "Wacom Graphire2 4x5",  WACOM_PKGLEN_GRAPHIRE,  10206,  7422,  511, 63, GRAPHIRE };
static const struct wacom_features wacom_features_0x12 =
	{ "Wacom Graphire2 5x7",  WACOM_PKGLEN_GRAPHIRE,  13918, 10206,  511, 63, GRAPHIRE };
static const struct wacom_features wacom_features_0x13 =
	{ "Wacom Graphire3",      WACOM_PKGLEN_GRAPHIRE,  10208,  7424,  511, 63, GRAPHIRE };
static const struct wacom_features wacom_features_0x14 =
	{ "Wacom Graphire3 6x8",  WACOM_PKGLEN_GRAPHIRE,  16704, 12064,  511, 63, GRAPHIRE };
static const struct wacom_features wacom_features_0x15 =
	{ "Wacom Graphire4 4x5",  WACOM_PKGLEN_GRAPHIRE,  10208,  7424,  511, 63, WACOM_G4 };
static const struct wacom_features wacom_features_0x16 =
	{ "Wacom Graphire4 6x8",  WACOM_PKGLEN_GRAPHIRE,  16704, 12064,  511, 63, WACOM_G4 };
static const struct wacom_features wacom_features_0x17 =
	{ "Wacom BambooFun 4x5",  WACOM_PKGLEN_BBFUN,     14760,  9225,  511, 63, WACOM_MO };
static const struct wacom_features wacom_features_0x18 =
	{ "Wacom BambooFun 6x8",  WACOM_PKGLEN_BBFUN,     21648, 13530,  511, 63, WACOM_MO };
static const struct wacom_features wacom_features_0x19 =
	{ "Wacom Bamboo1 Medium", WACOM_PKGLEN_GRAPHIRE,  16704, 12064,  511, 63, GRAPHIRE };
static const struct wacom_features wacom_features_0x60 =
	{ "Wacom Volito",         WACOM_PKGLEN_GRAPHIRE,   5104,  3712,  511, 63, GRAPHIRE };
static const struct wacom_features wacom_features_0x61 =
	{ "Wacom PenStation2",    WACOM_PKGLEN_GRAPHIRE,   3250,  2320,  255, 63, GRAPHIRE };
static const struct wacom_features wacom_features_0x62 =
	{ "Wacom Volito2 4x5",    WACOM_PKGLEN_GRAPHIRE,   5104,  3712,  511, 63, GRAPHIRE };
static const struct wacom_features wacom_features_0x63 =
	{ "Wacom Volito2 2x3",    WACOM_PKGLEN_GRAPHIRE,   3248,  2320,  511, 63, GRAPHIRE };
static const struct wacom_features wacom_features_0x64 =
	{ "Wacom PenPartner2",    WACOM_PKGLEN_GRAPHIRE,   3250,  2320,  511, 63, GRAPHIRE };
static const struct wacom_features wacom_features_0x65 =
	{ "Wacom Bamboo",         WACOM_PKGLEN_BBFUN,     14760,  9225,  511, 63, WACOM_MO };
static const struct wacom_features wacom_features_0x69 =
	{ "Wacom Bamboo1",        WACOM_PKGLEN_GRAPHIRE,   5104,  3712,  511, 63, GRAPHIRE };
static const struct wacom_features wacom_features_0x20 =
	{ "Wacom Intuos 4x5",     WACOM_PKGLEN_INTUOS,    12700, 10600, 1023, 31, INTUOS };
static const struct wacom_features wacom_features_0x21 =
	{ "Wacom Intuos 6x8",     WACOM_PKGLEN_INTUOS,    20320, 16240, 1023, 31, INTUOS };
static const struct wacom_features wacom_features_0x22 =
	{ "Wacom Intuos 9x12",    WACOM_PKGLEN_INTUOS,    30480, 24060, 1023, 31, INTUOS };
static const struct wacom_features wacom_features_0x23 =
	{ "Wacom Intuos 12x12",   WACOM_PKGLEN_INTUOS,    30480, 31680, 1023, 31, INTUOS };
static const struct wacom_features wacom_features_0x24 =
	{ "Wacom Intuos 12x18",   WACOM_PKGLEN_INTUOS,    45720, 31680, 1023, 31, INTUOS };
static const struct wacom_features wacom_features_0x30 =
	{ "Wacom PL400",          WACOM_PKGLEN_GRAPHIRE,   5408,  4056,  255,  0, PL };
static const struct wacom_features wacom_features_0x31 =
	{ "Wacom PL500",          WACOM_PKGLEN_GRAPHIRE,   6144,  4608,  255,  0, PL };
static const struct wacom_features wacom_features_0x32 =
	{ "Wacom PL600",          WACOM_PKGLEN_GRAPHIRE,   6126,  4604,  255,  0, PL };
static const struct wacom_features wacom_features_0x33 =
	{ "Wacom PL600SX",        WACOM_PKGLEN_GRAPHIRE,   6260,  5016,  255,  0, PL };
static const struct wacom_features wacom_features_0x34 =
	{ "Wacom PL550",          WACOM_PKGLEN_GRAPHIRE,   6144,  4608,  511,  0, PL };
static const struct wacom_features wacom_features_0x35 =
	{ "Wacom PL800",          WACOM_PKGLEN_GRAPHIRE,   7220,  5780,  511,  0, PL };
static const struct wacom_features wacom_features_0x37 =
	{ "Wacom PL700",          WACOM_PKGLEN_GRAPHIRE,   6758,  5406,  511,  0, PL };
static const struct wacom_features wacom_features_0x38 =
	{ "Wacom PL510",          WACOM_PKGLEN_GRAPHIRE,   6282,  4762,  511,  0, PL };
static const struct wacom_features wacom_features_0x39 =
	{ "Wacom DTU710",         WACOM_PKGLEN_GRAPHIRE,  34080, 27660,  511,  0, PL };
static const struct wacom_features wacom_features_0xC4 =
	{ "Wacom DTF521",         WACOM_PKGLEN_GRAPHIRE,   6282,  4762,  511,  0, PL };
static const struct wacom_features wacom_features_0xC0 =
	{ "Wacom DTF720",         WACOM_PKGLEN_GRAPHIRE,   6858,  5506,  511,  0, PL };
static const struct wacom_features wacom_features_0xC2 =
	{ "Wacom DTF720a",        WACOM_PKGLEN_GRAPHIRE,   6858,  5506,  511,  0, PL };
static const struct wacom_features wacom_features_0x03 =
	{ "Wacom Cintiq Partner", WACOM_PKGLEN_GRAPHIRE,  20480, 15360,  511,  0, PTU };
static const struct wacom_features wacom_features_0x41 =
	{ "Wacom Intuos2 4x5",    WACOM_PKGLEN_INTUOS,    12700, 10600, 1023, 31, INTUOS };
static const struct wacom_features wacom_features_0x42 =
	{ "Wacom Intuos2 6x8",    WACOM_PKGLEN_INTUOS,    20320, 16240, 1023, 31, INTUOS };
static const struct wacom_features wacom_features_0x43 =
	{ "Wacom Intuos2 9x12",   WACOM_PKGLEN_INTUOS,    30480, 24060, 1023, 31, INTUOS };
static const struct wacom_features wacom_features_0x44 =
	{ "Wacom Intuos2 12x12",  WACOM_PKGLEN_INTUOS,    30480, 31680, 1023, 31, INTUOS };
static const struct wacom_features wacom_features_0x45 =
	{ "Wacom Intuos2 12x18",  WACOM_PKGLEN_INTUOS,    45720, 31680, 1023, 31, INTUOS };
static const struct wacom_features wacom_features_0xB0 =
	{ "Wacom Intuos3 4x5",    WACOM_PKGLEN_INTUOS,    25400, 20320, 1023, 63, INTUOS3S };
static const struct wacom_features wacom_features_0xB1 =
	{ "Wacom Intuos3 6x8",    WACOM_PKGLEN_INTUOS,    40640, 30480, 1023, 63, INTUOS3 };
static const struct wacom_features wacom_features_0xB2 =
	{ "Wacom Intuos3 9x12",   WACOM_PKGLEN_INTUOS,    60960, 45720, 1023, 63, INTUOS3 };
static const struct wacom_features wacom_features_0xB3 =
	{ "Wacom Intuos3 12x12",  WACOM_PKGLEN_INTUOS,    60960, 60960, 1023, 63, INTUOS3L };
static const struct wacom_features wacom_features_0xB4 =
	{ "Wacom Intuos3 12x19",  WACOM_PKGLEN_INTUOS,    97536, 60960, 1023, 63, INTUOS3L };
static const struct wacom_features wacom_features_0xB5 =
	{ "Wacom Intuos3 6x11",   WACOM_PKGLEN_INTUOS,    54204, 31750, 1023, 63, INTUOS3 };
static const struct wacom_features wacom_features_0xB7 =
	{ "Wacom Intuos3 4x6",    WACOM_PKGLEN_INTUOS,    31496, 19685, 1023, 63, INTUOS3S };
static const struct wacom_features wacom_features_0xB8 =
	{ "Wacom Intuos4 4x6",    WACOM_PKGLEN_INTUOS,    31496, 19685, 2047, 63, INTUOS4S };
static const struct wacom_features wacom_features_0xB9 =
	{ "Wacom Intuos4 6x9",    WACOM_PKGLEN_INTUOS,    44704, 27940, 2047, 63, INTUOS4 };
static const struct wacom_features wacom_features_0xBA =
	{ "Wacom Intuos4 8x13",   WACOM_PKGLEN_INTUOS,    65024, 40640, 2047, 63, INTUOS4L };
static const struct wacom_features wacom_features_0xBB =
	{ "Wacom Intuos4 12x19",  WACOM_PKGLEN_INTUOS,    97536, 60960, 2047, 63, INTUOS4L };
static const struct wacom_features wacom_features_0x3F =
	{ "Wacom Cintiq 21UX",    WACOM_PKGLEN_INTUOS,    87200, 65600, 1023, 63, CINTIQ };
static const struct wacom_features wacom_features_0xC5 =
	{ "Wacom Cintiq 20WSX",   WACOM_PKGLEN_INTUOS,    86680, 54180, 1023, 63, WACOM_BEE };
static const struct wacom_features wacom_features_0xC6 =
	{ "Wacom Cintiq 12WX",    WACOM_PKGLEN_INTUOS,    53020, 33440, 1023, 63, WACOM_BEE };
static const struct wacom_features wacom_features_0xC7 =
	{ "Wacom DTU1931",        WACOM_PKGLEN_GRAPHIRE,  37832, 30305,  511,  0, PL };
static const struct wacom_features wacom_features_0x90 =
	{ "Wacom ISDv4 90",       WACOM_PKGLEN_GRAPHIRE,  26202, 16325,  255,  0, TABLETPC };
static const struct wacom_features wacom_features_0x93 =
	{ "Wacom ISDv4 93",       WACOM_PKGLEN_GRAPHIRE,  26202, 16325,  255,  0, TABLETPC };
static const struct wacom_features wacom_features_0x9A =
	{ "Wacom ISDv4 9A",       WACOM_PKGLEN_GRAPHIRE,  26202, 16325,  255,  0, TABLETPC };
static const struct wacom_features wacom_features_0x9F =
	{ "Wacom ISDv4 9F",       WACOM_PKGLEN_GRAPHIRE,  26202, 16325,  255,  0, TABLETPC };
static const struct wacom_features wacom_features_0xE2 =
	{ "Wacom ISDv4 E2",       WACOM_PKGLEN_TPC2FG,    26202, 16325,  255,  0, TABLETPC2FG };
static const struct wacom_features wacom_features_0xE3 =
	{ "Wacom ISDv4 E3",       WACOM_PKGLEN_TPC2FG,    26202, 16325,  255,  0, TABLETPC2FG };
static const struct wacom_features wacom_features_0x47 =
	{ "Wacom Intuos2 6x8",    WACOM_PKGLEN_INTUOS,    20320, 16240, 1023, 31, INTUOS };

#define USB_DEVICE_WACOM(prod)					\
	USB_DEVICE(USB_VENDOR_ID_WACOM, prod),			\
	.driver_info = (kernel_ulong_t)&wacom_features_##prod

const struct usb_device_id wacom_ids[] = {
	{ USB_DEVICE_WACOM(0x00) },
	{ USB_DEVICE_WACOM(0x10) },
	{ USB_DEVICE_WACOM(0x11) },
	{ USB_DEVICE_WACOM(0x12) },
	{ USB_DEVICE_WACOM(0x13) },
	{ USB_DEVICE_WACOM(0x14) },
	{ USB_DEVICE_WACOM(0x15) },
	{ USB_DEVICE_WACOM(0x16) },
	{ USB_DEVICE_WACOM(0x17) },
	{ USB_DEVICE_WACOM(0x18) },
	{ USB_DEVICE_WACOM(0x19) },
	{ USB_DEVICE_WACOM(0x60) },
	{ USB_DEVICE_WACOM(0x61) },
	{ USB_DEVICE_WACOM(0x62) },
	{ USB_DEVICE_WACOM(0x63) },
	{ USB_DEVICE_WACOM(0x64) },
	{ USB_DEVICE_WACOM(0x65) },
	{ USB_DEVICE_WACOM(0x69) },
	{ USB_DEVICE_WACOM(0x20) },
	{ USB_DEVICE_WACOM(0x21) },
	{ USB_DEVICE_WACOM(0x22) },
	{ USB_DEVICE_WACOM(0x23) },
	{ USB_DEVICE_WACOM(0x24) },
	{ USB_DEVICE_WACOM(0x30) },
	{ USB_DEVICE_WACOM(0x31) },
	{ USB_DEVICE_WACOM(0x32) },
	{ USB_DEVICE_WACOM(0x33) },
	{ USB_DEVICE_WACOM(0x34) },
	{ USB_DEVICE_WACOM(0x35) },
	{ USB_DEVICE_WACOM(0x37) },
	{ USB_DEVICE_WACOM(0x38) },
	{ USB_DEVICE_WACOM(0x39) },
	{ USB_DEVICE_WACOM(0xC4) },
	{ USB_DEVICE_WACOM(0xC0) },
	{ USB_DEVICE_WACOM(0xC2) },
	{ USB_DEVICE_WACOM(0x03) },
	{ USB_DEVICE_WACOM(0x41) },
	{ USB_DEVICE_WACOM(0x42) },
	{ USB_DEVICE_WACOM(0x43) },
	{ USB_DEVICE_WACOM(0x44) },
	{ USB_DEVICE_WACOM(0x45) },
	{ USB_DEVICE_WACOM(0xB0) },
	{ USB_DEVICE_WACOM(0xB1) },
	{ USB_DEVICE_WACOM(0xB2) },
	{ USB_DEVICE_WACOM(0xB3) },
	{ USB_DEVICE_WACOM(0xB4) },
	{ USB_DEVICE_WACOM(0xB5) },
	{ USB_DEVICE_WACOM(0xB7) },
	{ USB_DEVICE_WACOM(0xB8) },
	{ USB_DEVICE_WACOM(0xB9) },
	{ USB_DEVICE_WACOM(0xBA) },
	{ USB_DEVICE_WACOM(0xBB) },
	{ USB_DEVICE_WACOM(0x3F) },
	{ USB_DEVICE_WACOM(0xC5) },
	{ USB_DEVICE_WACOM(0xC6) },
	{ USB_DEVICE_WACOM(0xC7) },
	{ USB_DEVICE_WACOM(0x90) },
	{ USB_DEVICE_WACOM(0x93) },
	{ USB_DEVICE_WACOM(0x9A) },
	{ USB_DEVICE_WACOM(0x9F) },
	{ USB_DEVICE_WACOM(0xE2) },
	{ USB_DEVICE_WACOM(0xE3) },
	{ USB_DEVICE_WACOM(0x47) },
	{ }
};
MODULE_DEVICE_TABLE(usb, wacom_ids);
