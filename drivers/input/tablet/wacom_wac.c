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

#include "wacom_wac.h"
#include "wacom.h"
#include <linux/input/mt.h>
#include <linux/hid.h>

/* resolution for penabled devices */
#define WACOM_PL_RES		20
#define WACOM_PENPRTN_RES	40
#define WACOM_VOLITO_RES	50
#define WACOM_GRAPHIRE_RES	80
#define WACOM_INTUOS_RES	100
#define WACOM_INTUOS3_RES	200

/* Scale factor relating reported contact size to logical contact area.
 * 2^14/pi is a good approximation on Intuos5 and 3rd-gen Bamboo
 */
#define WACOM_CONTACT_AREA_SCALE 2607

static int wacom_penpartner_irq(struct wacom_wac *wacom)
{
	unsigned char *data = wacom->data;
	struct input_dev *input = wacom->input;

	switch (data[0]) {
	case 1:
		if (data[5] & 0x80) {
			wacom->tool[0] = (data[5] & 0x20) ? BTN_TOOL_RUBBER : BTN_TOOL_PEN;
			wacom->id[0] = (data[5] & 0x20) ? ERASER_DEVICE_ID : STYLUS_DEVICE_ID;
			input_report_key(input, wacom->tool[0], 1);
			input_report_abs(input, ABS_MISC, wacom->id[0]); /* report tool id */
			input_report_abs(input, ABS_X, get_unaligned_le16(&data[1]));
			input_report_abs(input, ABS_Y, get_unaligned_le16(&data[3]));
			input_report_abs(input, ABS_PRESSURE, (signed char)data[6] + 127);
			input_report_key(input, BTN_TOUCH, ((signed char)data[6] > -127));
			input_report_key(input, BTN_STYLUS, (data[5] & 0x40));
		} else {
			input_report_key(input, wacom->tool[0], 0);
			input_report_abs(input, ABS_MISC, 0); /* report tool id */
			input_report_abs(input, ABS_PRESSURE, -1);
			input_report_key(input, BTN_TOUCH, 0);
		}
		break;

	case 2:
		input_report_key(input, BTN_TOOL_PEN, 1);
		input_report_abs(input, ABS_MISC, STYLUS_DEVICE_ID); /* report tool id */
		input_report_abs(input, ABS_X, get_unaligned_le16(&data[1]));
		input_report_abs(input, ABS_Y, get_unaligned_le16(&data[3]));
		input_report_abs(input, ABS_PRESSURE, (signed char)data[6] + 127);
		input_report_key(input, BTN_TOUCH, ((signed char)data[6] > -80) && !(data[5] & 0x20));
		input_report_key(input, BTN_STYLUS, (data[5] & 0x40));
		break;

	default:
		dev_dbg(input->dev.parent,
			"%s: received unknown report #%d\n", __func__, data[0]);
		return 0;
        }

	return 1;
}

static int wacom_pl_irq(struct wacom_wac *wacom)
{
	struct wacom_features *features = &wacom->features;
	unsigned char *data = wacom->data;
	struct input_dev *input = wacom->input;
	int prox, pressure;

	if (data[0] != WACOM_REPORT_PENABLED) {
		dev_dbg(input->dev.parent,
			"%s: received unknown report #%d\n", __func__, data[0]);
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
				input_report_key(input, wacom->tool[1], 0);
				input_sync(input);
				wacom->tool[1] = BTN_TOOL_PEN;
				return 0;
			}
		}
		if (wacom->tool[1] != BTN_TOOL_RUBBER) {
			/* Unknown tool selected default to pen tool */
			wacom->tool[1] = BTN_TOOL_PEN;
			wacom->id[0] = STYLUS_DEVICE_ID;
		}
		input_report_key(input, wacom->tool[1], prox); /* report in proximity for tool */
		input_report_abs(input, ABS_MISC, wacom->id[0]); /* report tool id */
		input_report_abs(input, ABS_X, data[3] | (data[2] << 7) | ((data[1] & 0x03) << 14));
		input_report_abs(input, ABS_Y, data[6] | (data[5] << 7) | ((data[4] & 0x03) << 14));
		input_report_abs(input, ABS_PRESSURE, pressure);

		input_report_key(input, BTN_TOUCH, data[4] & 0x08);
		input_report_key(input, BTN_STYLUS, data[4] & 0x10);
		/* Only allow the stylus2 button to be reported for the pen tool. */
		input_report_key(input, BTN_STYLUS2, (wacom->tool[1] == BTN_TOOL_PEN) && (data[4] & 0x20));
	} else {
		/* report proximity-out of a (valid) tool */
		if (wacom->tool[1] != BTN_TOOL_RUBBER) {
			/* Unknown tool selected default to pen tool */
			wacom->tool[1] = BTN_TOOL_PEN;
		}
		input_report_key(input, wacom->tool[1], prox);
	}

	wacom->tool[0] = prox; /* Save proximity state */
	return 1;
}

static int wacom_ptu_irq(struct wacom_wac *wacom)
{
	unsigned char *data = wacom->data;
	struct input_dev *input = wacom->input;

	if (data[0] != WACOM_REPORT_PENABLED) {
		dev_dbg(input->dev.parent,
			"%s: received unknown report #%d\n", __func__, data[0]);
		return 0;
	}

	if (data[1] & 0x04) {
		input_report_key(input, BTN_TOOL_RUBBER, data[1] & 0x20);
		input_report_key(input, BTN_TOUCH, data[1] & 0x08);
		wacom->id[0] = ERASER_DEVICE_ID;
	} else {
		input_report_key(input, BTN_TOOL_PEN, data[1] & 0x20);
		input_report_key(input, BTN_TOUCH, data[1] & 0x01);
		wacom->id[0] = STYLUS_DEVICE_ID;
	}
	input_report_abs(input, ABS_MISC, wacom->id[0]); /* report tool id */
	input_report_abs(input, ABS_X, le16_to_cpup((__le16 *)&data[2]));
	input_report_abs(input, ABS_Y, le16_to_cpup((__le16 *)&data[4]));
	input_report_abs(input, ABS_PRESSURE, le16_to_cpup((__le16 *)&data[6]));
	input_report_key(input, BTN_STYLUS, data[1] & 0x02);
	input_report_key(input, BTN_STYLUS2, data[1] & 0x10);
	return 1;
}

static int wacom_dtu_irq(struct wacom_wac *wacom)
{
	struct wacom_features *features = &wacom->features;
	char *data = wacom->data;
	struct input_dev *input = wacom->input;
	int prox = data[1] & 0x20, pressure;

	dev_dbg(input->dev.parent,
		"%s: received report #%d", __func__, data[0]);

	if (prox) {
		/* Going into proximity select tool */
		wacom->tool[0] = (data[1] & 0x0c) ? BTN_TOOL_RUBBER : BTN_TOOL_PEN;
		if (wacom->tool[0] == BTN_TOOL_PEN)
			wacom->id[0] = STYLUS_DEVICE_ID;
		else
			wacom->id[0] = ERASER_DEVICE_ID;
	}
	input_report_key(input, BTN_STYLUS, data[1] & 0x02);
	input_report_key(input, BTN_STYLUS2, data[1] & 0x10);
	input_report_abs(input, ABS_X, le16_to_cpup((__le16 *)&data[2]));
	input_report_abs(input, ABS_Y, le16_to_cpup((__le16 *)&data[4]));
	pressure = ((data[7] & 0x01) << 8) | data[6];
	if (pressure < 0)
		pressure = features->pressure_max + pressure + 1;
	input_report_abs(input, ABS_PRESSURE, pressure);
	input_report_key(input, BTN_TOUCH, data[1] & 0x05);
	if (!prox) /* out-prox */
		wacom->id[0] = 0;
	input_report_key(input, wacom->tool[0], prox);
	input_report_abs(input, ABS_MISC, wacom->id[0]);
	return 1;
}

static int wacom_graphire_irq(struct wacom_wac *wacom)
{
	struct wacom_features *features = &wacom->features;
	unsigned char *data = wacom->data;
	struct input_dev *input = wacom->input;
	int prox;
	int rw = 0;
	int retval = 0;

	if (data[0] != WACOM_REPORT_PENABLED) {
		dev_dbg(input->dev.parent,
			"%s: received unknown report #%d\n", __func__, data[0]);
		goto exit;
	}

	prox = data[1] & 0x80;
	if (prox || wacom->id[0]) {
		if (prox) {
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
				input_report_key(input, BTN_MIDDLE, data[1] & 0x04);
				/* fall through */

			case 3: /* Mouse without wheel */
				wacom->tool[0] = BTN_TOOL_MOUSE;
				wacom->id[0] = CURSOR_DEVICE_ID;
				break;
			}
		}
		input_report_abs(input, ABS_X, le16_to_cpup((__le16 *)&data[2]));
		input_report_abs(input, ABS_Y, le16_to_cpup((__le16 *)&data[4]));
		if (wacom->tool[0] != BTN_TOOL_MOUSE) {
			input_report_abs(input, ABS_PRESSURE, data[6] | ((data[7] & 0x03) << 8));
			input_report_key(input, BTN_TOUCH, data[1] & 0x01);
			input_report_key(input, BTN_STYLUS, data[1] & 0x02);
			input_report_key(input, BTN_STYLUS2, data[1] & 0x04);
		} else {
			input_report_key(input, BTN_LEFT, data[1] & 0x01);
			input_report_key(input, BTN_RIGHT, data[1] & 0x02);
			if (features->type == WACOM_G4 ||
					features->type == WACOM_MO) {
				input_report_abs(input, ABS_DISTANCE, data[6] & 0x3f);
				rw = (data[7] & 0x04) - (data[7] & 0x03);
			} else {
				input_report_abs(input, ABS_DISTANCE, data[7] & 0x3f);
				rw = -(signed char)data[6];
			}
			input_report_rel(input, REL_WHEEL, rw);
		}

		if (!prox)
			wacom->id[0] = 0;
		input_report_abs(input, ABS_MISC, wacom->id[0]); /* report tool id */
		input_report_key(input, wacom->tool[0], prox);
		input_event(input, EV_MSC, MSC_SERIAL, 1);
		input_sync(input); /* sync last event */
	}

	/* send pad data */
	switch (features->type) {
	case WACOM_G4:
		prox = data[7] & 0xf8;
		if (prox || wacom->id[1]) {
			wacom->id[1] = PAD_DEVICE_ID;
			input_report_key(input, BTN_BACK, (data[7] & 0x40));
			input_report_key(input, BTN_FORWARD, (data[7] & 0x80));
			rw = ((data[7] & 0x18) >> 3) - ((data[7] & 0x20) >> 3);
			input_report_rel(input, REL_WHEEL, rw);
			if (!prox)
				wacom->id[1] = 0;
			input_report_abs(input, ABS_MISC, wacom->id[1]);
			input_event(input, EV_MSC, MSC_SERIAL, 0xf0);
			retval = 1;
		}
		break;

	case WACOM_MO:
		prox = (data[7] & 0xf8) || data[8];
		if (prox || wacom->id[1]) {
			wacom->id[1] = PAD_DEVICE_ID;
			input_report_key(input, BTN_BACK, (data[7] & 0x08));
			input_report_key(input, BTN_LEFT, (data[7] & 0x20));
			input_report_key(input, BTN_FORWARD, (data[7] & 0x10));
			input_report_key(input, BTN_RIGHT, (data[7] & 0x40));
			input_report_abs(input, ABS_WHEEL, (data[8] & 0x7f));
			if (!prox)
				wacom->id[1] = 0;
			input_report_abs(input, ABS_MISC, wacom->id[1]);
			input_event(input, EV_MSC, MSC_SERIAL, 0xf0);
			retval = 1;
		}
		break;
	}
exit:
	return retval;
}

static int wacom_intuos_inout(struct wacom_wac *wacom)
{
	struct wacom_features *features = &wacom->features;
	unsigned char *data = wacom->data;
	struct input_dev *input = wacom->input;
	int idx = 0;

	/* tool number */
	if (features->type == INTUOS)
		idx = data[1] & 0x01;

	/* Enter report */
	if ((data[1] & 0xfc) == 0xc0) {
		if (features->quirks == WACOM_QUIRK_MULTI_INPUT)
			wacom->shared->stylus_in_proximity = true;

		/* serial number of the tool */
		wacom->serial[idx] = ((data[3] & 0x0f) << 28) +
			(data[4] << 20) + (data[5] << 12) +
			(data[6] << 4) + (data[7] >> 4);

		wacom->id[idx] = (data[2] << 4) | (data[3] >> 4) |
			((data[7] & 0x0f) << 20) | ((data[8] & 0xf0) << 12);

		switch (wacom->id[idx]) {
		case 0x812: /* Inking pen */
		case 0x801: /* Intuos3 Inking pen */
		case 0x120802: /* Intuos4/5 Inking Pen */
		case 0x012:
			wacom->tool[idx] = BTN_TOOL_PENCIL;
			break;

		case 0x822: /* Pen */
		case 0x842:
		case 0x852:
		case 0x823: /* Intuos3 Grip Pen */
		case 0x813: /* Intuos3 Classic Pen */
		case 0x885: /* Intuos3 Marker Pen */
		case 0x802: /* Intuos4/5 13HD/24HD General Pen */
		case 0x804: /* Intuos4/5 13HD/24HD Marker Pen */
		case 0x022:
		case 0x100804: /* Intuos4/5 13HD/24HD Art Pen */
		case 0x140802: /* Intuos4/5 13HD/24HD Classic Pen */
		case 0x160802: /* Cintiq 13HD Pro Pen */
		case 0x180802: /* DTH2242 Pen */
		case 0x100802: /* Intuos4/5 13HD/24HD General Pen */
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
		case 0x80c: /* Intuos4/5 13HD/24HD Marker Pen Eraser */
		case 0x80a: /* Intuos4/5 13HD/24HD General Pen Eraser */
		case 0x90a: /* Intuos4/5 13HD/24HD Airbrush Eraser */
		case 0x14080a: /* Intuos4/5 13HD/24HD Classic Pen Eraser */
		case 0x10090a: /* Intuos4/5 13HD/24HD Airbrush Eraser */
		case 0x10080c: /* Intuos4/5 13HD/24HD Art Pen Eraser */
		case 0x16080a: /* Cintiq 13HD Pro Pen Eraser */
		case 0x18080a: /* DTH2242 Eraser */
		case 0x10080a: /* Intuos4/5 13HD/24HD General Pen Eraser */
			wacom->tool[idx] = BTN_TOOL_RUBBER;
			break;

		case 0xd12:
		case 0x912:
		case 0x112:
		case 0x913: /* Intuos3 Airbrush */
		case 0x902: /* Intuos4/5 13HD/24HD Airbrush */
		case 0x100902: /* Intuos4/5 13HD/24HD Airbrush */
			wacom->tool[idx] = BTN_TOOL_AIRBRUSH;
			break;

		default: /* Unknown tool */
			wacom->tool[idx] = BTN_TOOL_PEN;
			break;
		}
		return 1;
	}

	/* older I4 styli don't work with new Cintiqs */
	if (!((wacom->id[idx] >> 20) & 0x01) &&
			(features->type == WACOM_21UX2))
		return 1;

	/* Range Report */
	if ((data[1] & 0xfe) == 0x20) {
		input_report_key(input, BTN_TOUCH, 0);
		input_report_abs(input, ABS_PRESSURE, 0);
		input_report_abs(input, ABS_DISTANCE, wacom->features.distance_max);
	}

	/* Exit report */
	if ((data[1] & 0xfe) == 0x80) {
		if (features->quirks == WACOM_QUIRK_MULTI_INPUT)
			wacom->shared->stylus_in_proximity = false;

		/*
		 * Reset all states otherwise we lose the initial states
		 * when in-prox next time
		 */
		input_report_abs(input, ABS_X, 0);
		input_report_abs(input, ABS_Y, 0);
		input_report_abs(input, ABS_DISTANCE, 0);
		input_report_abs(input, ABS_TILT_X, 0);
		input_report_abs(input, ABS_TILT_Y, 0);
		if (wacom->tool[idx] >= BTN_TOOL_MOUSE) {
			input_report_key(input, BTN_LEFT, 0);
			input_report_key(input, BTN_MIDDLE, 0);
			input_report_key(input, BTN_RIGHT, 0);
			input_report_key(input, BTN_SIDE, 0);
			input_report_key(input, BTN_EXTRA, 0);
			input_report_abs(input, ABS_THROTTLE, 0);
			input_report_abs(input, ABS_RZ, 0);
		} else {
			input_report_abs(input, ABS_PRESSURE, 0);
			input_report_key(input, BTN_STYLUS, 0);
			input_report_key(input, BTN_STYLUS2, 0);
			input_report_key(input, BTN_TOUCH, 0);
			input_report_abs(input, ABS_WHEEL, 0);
			if (features->type >= INTUOS3S)
				input_report_abs(input, ABS_Z, 0);
		}
		input_report_key(input, wacom->tool[idx], 0);
		input_report_abs(input, ABS_MISC, 0); /* reset tool id */
		input_event(input, EV_MSC, MSC_SERIAL, wacom->serial[idx]);
		wacom->id[idx] = 0;
		return 2;
	}
	return 0;
}

static void wacom_intuos_general(struct wacom_wac *wacom)
{
	struct wacom_features *features = &wacom->features;
	unsigned char *data = wacom->data;
	struct input_dev *input = wacom->input;
	unsigned int t;

	/* general pen packet */
	if ((data[1] & 0xb8) == 0xa0) {
		t = (data[6] << 2) | ((data[7] >> 6) & 3);
		if (features->type >= INTUOS4S && features->type <= CINTIQ_HYBRID) {
			t = (t << 1) | (data[1] & 1);
		}
		input_report_abs(input, ABS_PRESSURE, t);
		input_report_abs(input, ABS_TILT_X,
				((data[7] << 1) & 0x7e) | (data[8] >> 7));
		input_report_abs(input, ABS_TILT_Y, data[8] & 0x7f);
		input_report_key(input, BTN_STYLUS, data[1] & 2);
		input_report_key(input, BTN_STYLUS2, data[1] & 4);
		input_report_key(input, BTN_TOUCH, t > 10);
	}

	/* airbrush second packet */
	if ((data[1] & 0xbc) == 0xb4) {
		input_report_abs(input, ABS_WHEEL,
				(data[6] << 2) | ((data[7] >> 6) & 3));
		input_report_abs(input, ABS_TILT_X,
				((data[7] << 1) & 0x7e) | (data[8] >> 7));
		input_report_abs(input, ABS_TILT_Y, data[8] & 0x7f);
	}
}

static int wacom_intuos_irq(struct wacom_wac *wacom)
{
	struct wacom_features *features = &wacom->features;
	unsigned char *data = wacom->data;
	struct input_dev *input = wacom->input;
	unsigned int t;
	int idx = 0, result;

	if (data[0] != WACOM_REPORT_PENABLED &&
	    data[0] != WACOM_REPORT_INTUOSREAD &&
	    data[0] != WACOM_REPORT_INTUOSWRITE &&
	    data[0] != WACOM_REPORT_INTUOSPAD &&
	    data[0] != WACOM_REPORT_INTUOS5PAD) {
		dev_dbg(input->dev.parent,
			"%s: received unknown report #%d\n", __func__, data[0]);
                return 0;
	}

	/* tool number */
	if (features->type == INTUOS)
		idx = data[1] & 0x01;

	/* pad packets. Works as a second tool and is always in prox */
	if (data[0] == WACOM_REPORT_INTUOSPAD || data[0] == WACOM_REPORT_INTUOS5PAD) {
		if (features->type >= INTUOS4S && features->type <= INTUOS4L) {
			input_report_key(input, BTN_0, (data[2] & 0x01));
			input_report_key(input, BTN_1, (data[3] & 0x01));
			input_report_key(input, BTN_2, (data[3] & 0x02));
			input_report_key(input, BTN_3, (data[3] & 0x04));
			input_report_key(input, BTN_4, (data[3] & 0x08));
			input_report_key(input, BTN_5, (data[3] & 0x10));
			input_report_key(input, BTN_6, (data[3] & 0x20));
			if (data[1] & 0x80) {
				input_report_abs(input, ABS_WHEEL, (data[1] & 0x7f));
			} else {
				/* Out of proximity, clear wheel value. */
				input_report_abs(input, ABS_WHEEL, 0);
			}
			if (features->type != INTUOS4S) {
				input_report_key(input, BTN_7, (data[3] & 0x40));
				input_report_key(input, BTN_8, (data[3] & 0x80));
			}
			if (data[1] | (data[2] & 0x01) | data[3]) {
				input_report_abs(input, ABS_MISC, PAD_DEVICE_ID);
			} else {
				input_report_abs(input, ABS_MISC, 0);
			}
		} else if (features->type == DTK) {
			input_report_key(input, BTN_0, (data[6] & 0x01));
			input_report_key(input, BTN_1, (data[6] & 0x02));
			input_report_key(input, BTN_2, (data[6] & 0x04));
			input_report_key(input, BTN_3, (data[6] & 0x08));
			input_report_key(input, BTN_4, (data[6] & 0x10));
			input_report_key(input, BTN_5, (data[6] & 0x20));
			if (data[6] & 0x3f) {
				input_report_abs(input, ABS_MISC, PAD_DEVICE_ID);
			} else {
				input_report_abs(input, ABS_MISC, 0);
			}
		} else if (features->type == WACOM_13HD) {
			input_report_key(input, BTN_0, (data[3] & 0x01));
			input_report_key(input, BTN_1, (data[4] & 0x01));
			input_report_key(input, BTN_2, (data[4] & 0x02));
			input_report_key(input, BTN_3, (data[4] & 0x04));
			input_report_key(input, BTN_4, (data[4] & 0x08));
			input_report_key(input, BTN_5, (data[4] & 0x10));
			input_report_key(input, BTN_6, (data[4] & 0x20));
			input_report_key(input, BTN_7, (data[4] & 0x40));
			input_report_key(input, BTN_8, (data[4] & 0x80));
			if ((data[3] & 0x01) | data[4]) {
				input_report_abs(input, ABS_MISC, PAD_DEVICE_ID);
			} else {
				input_report_abs(input, ABS_MISC, 0);
			}
		} else if (features->type == WACOM_24HD) {
			input_report_key(input, BTN_0, (data[6] & 0x01));
			input_report_key(input, BTN_1, (data[6] & 0x02));
			input_report_key(input, BTN_2, (data[6] & 0x04));
			input_report_key(input, BTN_3, (data[6] & 0x08));
			input_report_key(input, BTN_4, (data[6] & 0x10));
			input_report_key(input, BTN_5, (data[6] & 0x20));
			input_report_key(input, BTN_6, (data[6] & 0x40));
			input_report_key(input, BTN_7, (data[6] & 0x80));
			input_report_key(input, BTN_8, (data[8] & 0x01));
			input_report_key(input, BTN_9, (data[8] & 0x02));
			input_report_key(input, BTN_A, (data[8] & 0x04));
			input_report_key(input, BTN_B, (data[8] & 0x08));
			input_report_key(input, BTN_C, (data[8] & 0x10));
			input_report_key(input, BTN_X, (data[8] & 0x20));
			input_report_key(input, BTN_Y, (data[8] & 0x40));
			input_report_key(input, BTN_Z, (data[8] & 0x80));

			/*
			 * Three "buttons" are available on the 24HD which are
			 * physically implemented as a touchstrip. Each button
			 * is approximately 3 bits wide with a 2 bit spacing.
			 * The raw touchstrip bits are stored at:
			 *    ((data[3] & 0x1f) << 8) | data[4])
			 */
			input_report_key(input, KEY_PROG1, data[4] & 0x07);
			input_report_key(input, KEY_PROG2, data[4] & 0xE0);
			input_report_key(input, KEY_PROG3, data[3] & 0x1C);

			if (data[1] & 0x80) {
				input_report_abs(input, ABS_WHEEL, (data[1] & 0x7f));
			} else {
				/* Out of proximity, clear wheel value. */
				input_report_abs(input, ABS_WHEEL, 0);
			}

			if (data[2] & 0x80) {
				input_report_abs(input, ABS_THROTTLE, (data[2] & 0x7f));
			} else {
				/* Out of proximity, clear second wheel value. */
				input_report_abs(input, ABS_THROTTLE, 0);
			}

			if (data[1] | data[2] | (data[3] & 0x1f) | data[4] | data[6] | data[8]) {
				input_report_abs(input, ABS_MISC, PAD_DEVICE_ID);
			} else {
				input_report_abs(input, ABS_MISC, 0);
			}
		} else if (features->type == CINTIQ_HYBRID) {
			/*
			 * Do not send hardware buttons under Android. They
			 * are already sent to the system through GPIO (and
			 * have different meaning).
			 */
			input_report_key(input, BTN_1, (data[4] & 0x01));
			input_report_key(input, BTN_2, (data[4] & 0x02));
			input_report_key(input, BTN_3, (data[4] & 0x04));
			input_report_key(input, BTN_4, (data[4] & 0x08));

			input_report_key(input, BTN_5, (data[4] & 0x10));  /* Right  */
			input_report_key(input, BTN_6, (data[4] & 0x20));  /* Up     */
			input_report_key(input, BTN_7, (data[4] & 0x40));  /* Left   */
			input_report_key(input, BTN_8, (data[4] & 0x80));  /* Down   */
			input_report_key(input, BTN_0, (data[3] & 0x01));  /* Center */
		} else if (features->type >= INTUOS5S && features->type <= INTUOSPL) {
			int i;

			/* Touch ring mode switch has no capacitive sensor */
			input_report_key(input, BTN_0, (data[3] & 0x01));

			/*
			 * ExpressKeys on Intuos5/Intuos Pro have a capacitive sensor in
			 * addition to the mechanical switch. Switch data is
			 * stored in data[4], capacitive data in data[5].
			 */
			for (i = 0; i < 8; i++)
				input_report_key(input, BTN_1 + i, data[4] & (1 << i));

			if (data[2] & 0x80) {
				input_report_abs(input, ABS_WHEEL, (data[2] & 0x7f));
			} else {
				/* Out of proximity, clear wheel value. */
				input_report_abs(input, ABS_WHEEL, 0);
			}

			if (data[2] | (data[3] & 0x01) | data[4] | data[5]) {
				input_report_abs(input, ABS_MISC, PAD_DEVICE_ID);
			} else {
				input_report_abs(input, ABS_MISC, 0);
			}
		} else {
			if (features->type == WACOM_21UX2 || features->type == WACOM_22HD) {
				input_report_key(input, BTN_0, (data[5] & 0x01));
				input_report_key(input, BTN_1, (data[6] & 0x01));
				input_report_key(input, BTN_2, (data[6] & 0x02));
				input_report_key(input, BTN_3, (data[6] & 0x04));
				input_report_key(input, BTN_4, (data[6] & 0x08));
				input_report_key(input, BTN_5, (data[6] & 0x10));
				input_report_key(input, BTN_6, (data[6] & 0x20));
				input_report_key(input, BTN_7, (data[6] & 0x40));
				input_report_key(input, BTN_8, (data[6] & 0x80));
				input_report_key(input, BTN_9, (data[7] & 0x01));
				input_report_key(input, BTN_A, (data[8] & 0x01));
				input_report_key(input, BTN_B, (data[8] & 0x02));
				input_report_key(input, BTN_C, (data[8] & 0x04));
				input_report_key(input, BTN_X, (data[8] & 0x08));
				input_report_key(input, BTN_Y, (data[8] & 0x10));
				input_report_key(input, BTN_Z, (data[8] & 0x20));
				input_report_key(input, BTN_BASE, (data[8] & 0x40));
				input_report_key(input, BTN_BASE2, (data[8] & 0x80));

				if (features->type == WACOM_22HD) {
					input_report_key(input, KEY_PROG1, data[9] & 0x01);
					input_report_key(input, KEY_PROG2, data[9] & 0x02);
					input_report_key(input, KEY_PROG3, data[9] & 0x04);
				}
			} else {
				input_report_key(input, BTN_0, (data[5] & 0x01));
				input_report_key(input, BTN_1, (data[5] & 0x02));
				input_report_key(input, BTN_2, (data[5] & 0x04));
				input_report_key(input, BTN_3, (data[5] & 0x08));
				input_report_key(input, BTN_4, (data[6] & 0x01));
				input_report_key(input, BTN_5, (data[6] & 0x02));
				input_report_key(input, BTN_6, (data[6] & 0x04));
				input_report_key(input, BTN_7, (data[6] & 0x08));
				input_report_key(input, BTN_8, (data[5] & 0x10));
				input_report_key(input, BTN_9, (data[6] & 0x10));
			}
			input_report_abs(input, ABS_RX, ((data[1] & 0x1f) << 8) | data[2]);
			input_report_abs(input, ABS_RY, ((data[3] & 0x1f) << 8) | data[4]);

			if ((data[5] & 0x1f) | data[6] | (data[1] & 0x1f) |
				data[2] | (data[3] & 0x1f) | data[4] | data[8] |
				(data[7] & 0x01)) {
				input_report_abs(input, ABS_MISC, PAD_DEVICE_ID);
			} else {
				input_report_abs(input, ABS_MISC, 0);
			}
		}
		input_event(input, EV_MSC, MSC_SERIAL, 0xffffffff);
                return 1;
	}

	/* process in/out prox events */
	result = wacom_intuos_inout(wacom);
	if (result)
                return result - 1;

	/* don't proceed if we don't know the ID */
	if (!wacom->id[idx])
		return 0;

	/* Only large Intuos support Lense Cursor */
	if (wacom->tool[idx] == BTN_TOOL_LENS &&
	    (features->type == INTUOS3 ||
	     features->type == INTUOS3S ||
	     features->type == INTUOS4 ||
	     features->type == INTUOS4S ||
	     features->type == INTUOS5 ||
	     features->type == INTUOS5S ||
	     features->type == INTUOSPM ||
	     features->type == INTUOSPS)) {

		return 0;
	}

	/* Cintiq doesn't send data when RDY bit isn't set */
	if (features->type == CINTIQ && !(data[1] & 0x40))
                 return 0;

	if (features->type >= INTUOS3S) {
		input_report_abs(input, ABS_X, (data[2] << 9) | (data[3] << 1) | ((data[9] >> 1) & 1));
		input_report_abs(input, ABS_Y, (data[4] << 9) | (data[5] << 1) | (data[9] & 1));
		input_report_abs(input, ABS_DISTANCE, ((data[9] >> 2) & 0x3f));
	} else {
		input_report_abs(input, ABS_X, be16_to_cpup((__be16 *)&data[2]));
		input_report_abs(input, ABS_Y, be16_to_cpup((__be16 *)&data[4]));
		input_report_abs(input, ABS_DISTANCE, ((data[9] >> 3) & 0x1f));
	}

	/* process general packets */
	wacom_intuos_general(wacom);

	/* 4D mouse, 2D mouse, marker pen rotation, tilt mouse, or Lens cursor packets */
	if ((data[1] & 0xbc) == 0xa8 || (data[1] & 0xbe) == 0xb0 || (data[1] & 0xbc) == 0xac) {

		if (data[1] & 0x02) {
			/* Rotation packet */
			if (features->type >= INTUOS3S) {
				/* I3 marker pen rotation */
				t = (data[6] << 3) | ((data[7] >> 5) & 7);
				t = (data[7] & 0x20) ? ((t > 900) ? ((t-1) / 2 - 1350) :
					((t-1) / 2 + 450)) : (450 - t / 2) ;
				input_report_abs(input, ABS_Z, t);
			} else {
				/* 4D mouse rotation packet */
				t = (data[6] << 3) | ((data[7] >> 5) & 7);
				input_report_abs(input, ABS_RZ, (data[7] & 0x20) ?
					((t - 1) / 2) : -t / 2);
			}

		} else if (!(data[1] & 0x10) && features->type < INTUOS3S) {
			/* 4D mouse packet */
			input_report_key(input, BTN_LEFT,   data[8] & 0x01);
			input_report_key(input, BTN_MIDDLE, data[8] & 0x02);
			input_report_key(input, BTN_RIGHT,  data[8] & 0x04);

			input_report_key(input, BTN_SIDE,   data[8] & 0x20);
			input_report_key(input, BTN_EXTRA,  data[8] & 0x10);
			t = (data[6] << 2) | ((data[7] >> 6) & 3);
			input_report_abs(input, ABS_THROTTLE, (data[8] & 0x08) ? -t : t);

		} else if (wacom->tool[idx] == BTN_TOOL_MOUSE) {
			/* I4 mouse */
			if (features->type >= INTUOS4S && features->type <= INTUOSPL) {
				input_report_key(input, BTN_LEFT,   data[6] & 0x01);
				input_report_key(input, BTN_MIDDLE, data[6] & 0x02);
				input_report_key(input, BTN_RIGHT,  data[6] & 0x04);
				input_report_rel(input, REL_WHEEL, ((data[7] & 0x80) >> 7)
						 - ((data[7] & 0x40) >> 6));
				input_report_key(input, BTN_SIDE,   data[6] & 0x08);
				input_report_key(input, BTN_EXTRA,  data[6] & 0x10);

				input_report_abs(input, ABS_TILT_X,
					((data[7] << 1) & 0x7e) | (data[8] >> 7));
				input_report_abs(input, ABS_TILT_Y, data[8] & 0x7f);
			} else {
				/* 2D mouse packet */
				input_report_key(input, BTN_LEFT,   data[8] & 0x04);
				input_report_key(input, BTN_MIDDLE, data[8] & 0x08);
				input_report_key(input, BTN_RIGHT,  data[8] & 0x10);
				input_report_rel(input, REL_WHEEL, (data[8] & 0x01)
						 - ((data[8] & 0x02) >> 1));

				/* I3 2D mouse side buttons */
				if (features->type >= INTUOS3S && features->type <= INTUOS3L) {
					input_report_key(input, BTN_SIDE,   data[8] & 0x40);
					input_report_key(input, BTN_EXTRA,  data[8] & 0x20);
				}
			}
		} else if ((features->type < INTUOS3S || features->type == INTUOS3L ||
				features->type == INTUOS4L || features->type == INTUOS5L ||
				features->type == INTUOSPL) &&
			   wacom->tool[idx] == BTN_TOOL_LENS) {
			/* Lens cursor packets */
			input_report_key(input, BTN_LEFT,   data[8] & 0x01);
			input_report_key(input, BTN_MIDDLE, data[8] & 0x02);
			input_report_key(input, BTN_RIGHT,  data[8] & 0x04);
			input_report_key(input, BTN_SIDE,   data[8] & 0x10);
			input_report_key(input, BTN_EXTRA,  data[8] & 0x08);
		}
	}

	input_report_abs(input, ABS_MISC, wacom->id[idx]); /* report tool id */
	input_report_key(input, wacom->tool[idx], 1);
	input_event(input, EV_MSC, MSC_SERIAL, wacom->serial[idx]);
	return 1;
}

static int int_dist(int x1, int y1, int x2, int y2)
{
	int x = x2 - x1;
	int y = y2 - y1;

	return int_sqrt(x*x + y*y);
}

static int wacom_24hdt_irq(struct wacom_wac *wacom)
{
	struct input_dev *input = wacom->input;
	char *data = wacom->data;
	int i;
	int current_num_contacts = data[61];
	int contacts_to_send = 0;

	/*
	 * First packet resets the counter since only the first
	 * packet in series will have non-zero current_num_contacts.
	 */
	if (current_num_contacts)
		wacom->num_contacts_left = current_num_contacts;

	/* There are at most 4 contacts per packet */
	contacts_to_send = min(4, wacom->num_contacts_left);

	for (i = 0; i < contacts_to_send; i++) {
		int offset = (WACOM_BYTES_PER_24HDT_PACKET * i) + 1;
		bool touch = data[offset] & 0x1 && !wacom->shared->stylus_in_proximity;
		int slot = input_mt_get_slot_by_key(input, data[offset + 1]);

		if (slot < 0)
			continue;
		input_mt_slot(input, slot);
		input_mt_report_slot_state(input, MT_TOOL_FINGER, touch);

		if (touch) {
			int t_x = le16_to_cpup((__le16 *)&data[offset + 2]);
			int c_x = le16_to_cpup((__le16 *)&data[offset + 4]);
			int t_y = le16_to_cpup((__le16 *)&data[offset + 6]);
			int c_y = le16_to_cpup((__le16 *)&data[offset + 8]);
			int w = le16_to_cpup((__le16 *)&data[offset + 10]);
			int h = le16_to_cpup((__le16 *)&data[offset + 12]);

			input_report_abs(input, ABS_MT_POSITION_X, t_x);
			input_report_abs(input, ABS_MT_POSITION_Y, t_y);
			input_report_abs(input, ABS_MT_TOUCH_MAJOR, min(w,h));
			input_report_abs(input, ABS_MT_WIDTH_MAJOR, min(w, h) + int_dist(t_x, t_y, c_x, c_y));
			input_report_abs(input, ABS_MT_WIDTH_MINOR, min(w, h));
			input_report_abs(input, ABS_MT_ORIENTATION, w > h);
		}
	}
	input_mt_report_pointer_emulation(input, true);

	wacom->num_contacts_left -= contacts_to_send;
	if (wacom->num_contacts_left <= 0)
		wacom->num_contacts_left = 0;

	return 1;
}

static int wacom_mt_touch(struct wacom_wac *wacom)
{
	struct input_dev *input = wacom->input;
	char *data = wacom->data;
	int i;
	int current_num_contacts = data[2];
	int contacts_to_send = 0;
	int x_offset = 0;

	/* MTTPC does not support Height and Width */
	if (wacom->features.type == MTTPC)
		x_offset = -4;

	/*
	 * First packet resets the counter since only the first
	 * packet in series will have non-zero current_num_contacts.
	 */
	if (current_num_contacts)
		wacom->num_contacts_left = current_num_contacts;

	/* There are at most 5 contacts per packet */
	contacts_to_send = min(5, wacom->num_contacts_left);

	for (i = 0; i < contacts_to_send; i++) {
		int offset = (WACOM_BYTES_PER_MT_PACKET + x_offset) * i + 3;
		bool touch = data[offset] & 0x1;
		int id = le16_to_cpup((__le16 *)&data[offset + 1]);
		int slot = input_mt_get_slot_by_key(input, id);

		if (slot < 0)
			continue;

		input_mt_slot(input, slot);
		input_mt_report_slot_state(input, MT_TOOL_FINGER, touch);
		if (touch) {
			int x = le16_to_cpup((__le16 *)&data[offset + x_offset + 7]);
			int y = le16_to_cpup((__le16 *)&data[offset + x_offset + 9]);
			input_report_abs(input, ABS_MT_POSITION_X, x);
			input_report_abs(input, ABS_MT_POSITION_Y, y);
		}
	}
	input_mt_report_pointer_emulation(input, true);

	wacom->num_contacts_left -= contacts_to_send;
	if (wacom->num_contacts_left < 0)
		wacom->num_contacts_left = 0;

	return 1;
}

static int wacom_tpc_mt_touch(struct wacom_wac *wacom)
{
	struct input_dev *input = wacom->input;
	unsigned char *data = wacom->data;
	int contact_with_no_pen_down_count = 0;
	int i;

	for (i = 0; i < 2; i++) {
		int p = data[1] & (1 << i);
		bool touch = p && !wacom->shared->stylus_in_proximity;

		input_mt_slot(input, i);
		input_mt_report_slot_state(input, MT_TOOL_FINGER, touch);
		if (touch) {
			int x = le16_to_cpup((__le16 *)&data[i * 2 + 2]) & 0x7fff;
			int y = le16_to_cpup((__le16 *)&data[i * 2 + 6]) & 0x7fff;

			input_report_abs(input, ABS_MT_POSITION_X, x);
			input_report_abs(input, ABS_MT_POSITION_Y, y);
			contact_with_no_pen_down_count++;
		}
	}
	input_mt_report_pointer_emulation(input, true);

	/* keep touch state for pen event */
	wacom->shared->touch_down = (contact_with_no_pen_down_count > 0);

	return 1;
}

static int wacom_tpc_single_touch(struct wacom_wac *wacom, size_t len)
{
	char *data = wacom->data;
	struct input_dev *input = wacom->input;
	bool prox;
	int x = 0, y = 0;

	if (wacom->features.touch_max > 1 || len > WACOM_PKGLEN_TPC2FG)
		return 0;

	if (!wacom->shared->stylus_in_proximity) {
		if (len == WACOM_PKGLEN_TPC1FG) {
			prox = data[0] & 0x01;
			x = get_unaligned_le16(&data[1]);
			y = get_unaligned_le16(&data[3]);
		} else {
			prox = data[1] & 0x01;
			x = le16_to_cpup((__le16 *)&data[2]);
			y = le16_to_cpup((__le16 *)&data[4]);
		}
	} else
		/* force touch out when pen is in prox */
		prox = 0;

	if (prox) {
		input_report_abs(input, ABS_X, x);
		input_report_abs(input, ABS_Y, y);
	}
	input_report_key(input, BTN_TOUCH, prox);

	/* keep touch state for pen events */
	wacom->shared->touch_down = prox;

	return 1;
}

static int wacom_tpc_pen(struct wacom_wac *wacom)
{
	struct wacom_features *features = &wacom->features;
	char *data = wacom->data;
	struct input_dev *input = wacom->input;
	int pressure;
	bool prox = data[1] & 0x20;

	if (!wacom->shared->stylus_in_proximity) /* first in prox */
		/* Going into proximity select tool */
		wacom->tool[0] = (data[1] & 0x0c) ? BTN_TOOL_RUBBER : BTN_TOOL_PEN;

	/* keep pen state for touch events */
	wacom->shared->stylus_in_proximity = prox;

	/* send pen events only when touch is up or forced out */
	if (!wacom->shared->touch_down) {
		input_report_key(input, BTN_STYLUS, data[1] & 0x02);
		input_report_key(input, BTN_STYLUS2, data[1] & 0x10);
		input_report_abs(input, ABS_X, le16_to_cpup((__le16 *)&data[2]));
		input_report_abs(input, ABS_Y, le16_to_cpup((__le16 *)&data[4]));
		pressure = ((data[7] & 0x01) << 8) | data[6];
		if (pressure < 0)
			pressure = features->pressure_max + pressure + 1;
		input_report_abs(input, ABS_PRESSURE, pressure);
		input_report_key(input, BTN_TOUCH, data[1] & 0x05);
		input_report_key(input, wacom->tool[0], prox);
		return 1;
	}

	return 0;
}

static int wacom_tpc_irq(struct wacom_wac *wacom, size_t len)
{
	char *data = wacom->data;

	dev_dbg(wacom->input->dev.parent,
		"%s: received report #%d\n", __func__, data[0]);

	switch (len) {
	case WACOM_PKGLEN_TPC1FG:
		return wacom_tpc_single_touch(wacom, len);

	case WACOM_PKGLEN_TPC2FG:
		return wacom_tpc_mt_touch(wacom);

	default:
		switch (data[0]) {
		case WACOM_REPORT_TPC1FG:
		case WACOM_REPORT_TPCHID:
		case WACOM_REPORT_TPCST:
		case WACOM_REPORT_TPC1FGE:
			return wacom_tpc_single_touch(wacom, len);

		case WACOM_REPORT_TPCMT:
			return wacom_mt_touch(wacom);

		case WACOM_REPORT_PENABLED:
			return wacom_tpc_pen(wacom);
		}
	}

	return 0;
}

static int wacom_bpt_touch(struct wacom_wac *wacom)
{
	struct wacom_features *features = &wacom->features;
	struct input_dev *input = wacom->input;
	unsigned char *data = wacom->data;
	int i;

	if (data[0] != 0x02)
	    return 0;

	for (i = 0; i < 2; i++) {
		int offset = (data[1] & 0x80) ? (8 * i) : (9 * i);
		bool touch = data[offset + 3] & 0x80;

		/*
		 * Touch events need to be disabled while stylus is
		 * in proximity because user's hand is resting on touchpad
		 * and sending unwanted events.  User expects tablet buttons
		 * to continue working though.
		 */
		touch = touch && !wacom->shared->stylus_in_proximity;

		input_mt_slot(input, i);
		input_mt_report_slot_state(input, MT_TOOL_FINGER, touch);
		if (touch) {
			int x = get_unaligned_be16(&data[offset + 3]) & 0x7ff;
			int y = get_unaligned_be16(&data[offset + 5]) & 0x7ff;
			if (features->quirks & WACOM_QUIRK_BBTOUCH_LOWRES) {
				x <<= 5;
				y <<= 5;
			}
			input_report_abs(input, ABS_MT_POSITION_X, x);
			input_report_abs(input, ABS_MT_POSITION_Y, y);
		}
	}

	input_mt_report_pointer_emulation(input, true);

	input_report_key(input, BTN_LEFT, (data[1] & 0x08) != 0);
	input_report_key(input, BTN_FORWARD, (data[1] & 0x04) != 0);
	input_report_key(input, BTN_BACK, (data[1] & 0x02) != 0);
	input_report_key(input, BTN_RIGHT, (data[1] & 0x01) != 0);

	input_sync(input);

	return 0;
}

static void wacom_bpt3_touch_msg(struct wacom_wac *wacom, unsigned char *data)
{
	struct wacom_features *features = &wacom->features;
	struct input_dev *input = wacom->input;
	bool touch = data[1] & 0x80;
	int slot = input_mt_get_slot_by_key(input, data[0]);

	if (slot < 0)
		return;

	touch = touch && !wacom->shared->stylus_in_proximity;

	input_mt_slot(input, slot);
	input_mt_report_slot_state(input, MT_TOOL_FINGER, touch);

	if (touch) {
		int x = (data[2] << 4) | (data[4] >> 4);
		int y = (data[3] << 4) | (data[4] & 0x0f);
		int width, height;

		if (features->type >= INTUOSPS && features->type <= INTUOSPL) {
			width  = data[5];
			height = data[6];
		} else {
			/*
			 * "a" is a scaled-down area which we assume is
			 * roughly circular and which can be described as:
			 * a=(pi*r^2)/C.
			 */
			int a = data[5];
			int x_res  = input_abs_get_res(input, ABS_X);
			int y_res  = input_abs_get_res(input, ABS_Y);
			width  = 2 * int_sqrt(a * WACOM_CONTACT_AREA_SCALE);
			height = width * y_res / x_res;
		}

		input_report_abs(input, ABS_MT_POSITION_X, x);
		input_report_abs(input, ABS_MT_POSITION_Y, y);
		input_report_abs(input, ABS_MT_TOUCH_MAJOR, width);
		input_report_abs(input, ABS_MT_TOUCH_MINOR, height);
	}
}

static void wacom_bpt3_button_msg(struct wacom_wac *wacom, unsigned char *data)
{
	struct input_dev *input = wacom->input;

	input_report_key(input, BTN_LEFT, (data[1] & 0x08) != 0);
	input_report_key(input, BTN_FORWARD, (data[1] & 0x04) != 0);
	input_report_key(input, BTN_BACK, (data[1] & 0x02) != 0);
	input_report_key(input, BTN_RIGHT, (data[1] & 0x01) != 0);
}

static int wacom_bpt3_touch(struct wacom_wac *wacom)
{
	struct input_dev *input = wacom->input;
	unsigned char *data = wacom->data;
	int count = data[1] & 0x07;
	int i;

	if (data[0] != 0x02)
	    return 0;

	/* data has up to 7 fixed sized 8-byte messages starting at data[2] */
	for (i = 0; i < count; i++) {
		int offset = (8 * i) + 2;
		int msg_id = data[offset];

		if (msg_id >= 2 && msg_id <= 17)
			wacom_bpt3_touch_msg(wacom, data + offset);
		else if (msg_id == 128)
			wacom_bpt3_button_msg(wacom, data + offset);

	}
	input_mt_report_pointer_emulation(input, true);

	input_sync(input);

	return 0;
}

static int wacom_bpt_pen(struct wacom_wac *wacom)
{
	struct input_dev *input = wacom->input;
	unsigned char *data = wacom->data;
	int prox = 0, x = 0, y = 0, p = 0, d = 0, pen = 0, btn1 = 0, btn2 = 0;

	if (data[0] != 0x02)
	    return 0;

	prox = (data[1] & 0x20) == 0x20;

	/*
	 * All reports shared between PEN and RUBBER tool must be
	 * forced to a known starting value (zero) when transitioning to
	 * out-of-prox.
	 *
	 * If not reset then, to userspace, it will look like lost events
	 * if new tool comes in-prox with same values as previous tool sent.
	 *
	 * Hardware does report zero in most out-of-prox cases but not all.
	 */
	if (prox) {
		if (!wacom->shared->stylus_in_proximity) {
			if (data[1] & 0x08) {
				wacom->tool[0] = BTN_TOOL_RUBBER;
				wacom->id[0] = ERASER_DEVICE_ID;
			} else {
				wacom->tool[0] = BTN_TOOL_PEN;
				wacom->id[0] = STYLUS_DEVICE_ID;
			}
			wacom->shared->stylus_in_proximity = true;
		}
		x = le16_to_cpup((__le16 *)&data[2]);
		y = le16_to_cpup((__le16 *)&data[4]);
		p = le16_to_cpup((__le16 *)&data[6]);
		/*
		 * Convert distance from out prox to distance from tablet.
		 * distance will be greater than distance_max once
		 * touching and applying pressure; do not report negative
		 * distance.
		 */
		if (data[8] <= wacom->features.distance_max)
			d = wacom->features.distance_max - data[8];

		pen = data[1] & 0x01;
		btn1 = data[1] & 0x02;
		btn2 = data[1] & 0x04;
	}

	input_report_key(input, BTN_TOUCH, pen);
	input_report_key(input, BTN_STYLUS, btn1);
	input_report_key(input, BTN_STYLUS2, btn2);

	input_report_abs(input, ABS_X, x);
	input_report_abs(input, ABS_Y, y);
	input_report_abs(input, ABS_PRESSURE, p);
	input_report_abs(input, ABS_DISTANCE, d);

	if (!prox) {
		wacom->id[0] = 0;
		wacom->shared->stylus_in_proximity = false;
	}

	input_report_key(input, wacom->tool[0], prox); /* PEN or RUBBER */
	input_report_abs(input, ABS_MISC, wacom->id[0]); /* TOOL ID */

	return 1;
}

static int wacom_bpt_irq(struct wacom_wac *wacom, size_t len)
{
	if (len == WACOM_PKGLEN_BBTOUCH)
		return wacom_bpt_touch(wacom);
	else if (len == WACOM_PKGLEN_BBTOUCH3)
		return wacom_bpt3_touch(wacom);
	else if (len == WACOM_PKGLEN_BBFUN || len == WACOM_PKGLEN_BBPEN)
		return wacom_bpt_pen(wacom);

	return 0;
}

static int wacom_wireless_irq(struct wacom_wac *wacom, size_t len)
{
	unsigned char *data = wacom->data;
	int connected;

	if (len != WACOM_PKGLEN_WIRELESS || data[0] != 0x80)
		return 0;

	connected = data[1] & 0x01;
	if (connected) {
		int pid, battery;

		pid = get_unaligned_be16(&data[6]);
		battery = data[5] & 0x3f;
		if (wacom->pid != pid) {
			wacom->pid = pid;
			wacom_schedule_work(wacom);
		}
		wacom->battery_capacity = battery;
	} else if (wacom->pid != 0) {
		/* disconnected while previously connected */
		wacom->pid = 0;
		wacom_schedule_work(wacom);
		wacom->battery_capacity = 0;
	}

	return 0;
}

void wacom_wac_irq(struct wacom_wac *wacom_wac, size_t len)
{
	bool sync;

	switch (wacom_wac->features.type) {
	case PENPARTNER:
		sync = wacom_penpartner_irq(wacom_wac);
		break;

	case PL:
		sync = wacom_pl_irq(wacom_wac);
		break;

	case WACOM_G4:
	case GRAPHIRE:
	case WACOM_MO:
		sync = wacom_graphire_irq(wacom_wac);
		break;

	case PTU:
		sync = wacom_ptu_irq(wacom_wac);
		break;

	case DTU:
		sync = wacom_dtu_irq(wacom_wac);
		break;

	case INTUOS:
	case INTUOS3S:
	case INTUOS3:
	case INTUOS3L:
	case INTUOS4S:
	case INTUOS4:
	case INTUOS4L:
	case CINTIQ:
	case WACOM_BEE:
	case WACOM_13HD:
	case WACOM_21UX2:
	case WACOM_22HD:
	case WACOM_24HD:
	case DTK:
	case CINTIQ_HYBRID:
		sync = wacom_intuos_irq(wacom_wac);
		break;

	case WACOM_24HDT:
		sync = wacom_24hdt_irq(wacom_wac);
		break;

	case INTUOS5S:
	case INTUOS5:
	case INTUOS5L:
	case INTUOSPS:
	case INTUOSPM:
	case INTUOSPL:
		if (len == WACOM_PKGLEN_BBTOUCH3)
			sync = wacom_bpt3_touch(wacom_wac);
		else
			sync = wacom_intuos_irq(wacom_wac);
		break;

	case TABLETPC:
	case TABLETPCE:
	case TABLETPC2FG:
	case MTSCREEN:
	case MTTPC:
		sync = wacom_tpc_irq(wacom_wac, len);
		break;

	case BAMBOO_PT:
		sync = wacom_bpt_irq(wacom_wac, len);
		break;

	case WIRELESS:
		sync = wacom_wireless_irq(wacom_wac, len);
		break;

	default:
		sync = false;
		break;
	}

	if (sync)
		input_sync(wacom_wac->input);
}

static void wacom_setup_cintiq(struct wacom_wac *wacom_wac)
{
	struct input_dev *input_dev = wacom_wac->input;

	input_set_capability(input_dev, EV_MSC, MSC_SERIAL);

	__set_bit(BTN_TOOL_RUBBER, input_dev->keybit);
	__set_bit(BTN_TOOL_PEN, input_dev->keybit);
	__set_bit(BTN_TOOL_BRUSH, input_dev->keybit);
	__set_bit(BTN_TOOL_PENCIL, input_dev->keybit);
	__set_bit(BTN_TOOL_AIRBRUSH, input_dev->keybit);
	__set_bit(BTN_STYLUS, input_dev->keybit);
	__set_bit(BTN_STYLUS2, input_dev->keybit);

	input_set_abs_params(input_dev, ABS_DISTANCE,
			     0, wacom_wac->features.distance_max, 0, 0);
	input_set_abs_params(input_dev, ABS_WHEEL, 0, 1023, 0, 0);
	input_set_abs_params(input_dev, ABS_TILT_X, 0, 127, 0, 0);
	input_set_abs_params(input_dev, ABS_TILT_Y, 0, 127, 0, 0);
}

static void wacom_setup_intuos(struct wacom_wac *wacom_wac)
{
	struct input_dev *input_dev = wacom_wac->input;

	input_set_capability(input_dev, EV_REL, REL_WHEEL);

	wacom_setup_cintiq(wacom_wac);

	__set_bit(BTN_LEFT, input_dev->keybit);
	__set_bit(BTN_RIGHT, input_dev->keybit);
	__set_bit(BTN_MIDDLE, input_dev->keybit);
	__set_bit(BTN_SIDE, input_dev->keybit);
	__set_bit(BTN_EXTRA, input_dev->keybit);
	__set_bit(BTN_TOOL_MOUSE, input_dev->keybit);
	__set_bit(BTN_TOOL_LENS, input_dev->keybit);

	input_set_abs_params(input_dev, ABS_RZ, -900, 899, 0, 0);
	input_set_abs_params(input_dev, ABS_THROTTLE, -1023, 1023, 0, 0);
}

void wacom_setup_device_quirks(struct wacom_features *features)
{

	/* touch device found but size is not defined. use default */
	if (features->device_type == BTN_TOOL_FINGER && !features->x_max) {
		features->x_max = 1023;
		features->y_max = 1023;
	}

	/* these device have multiple inputs */
	if (features->type >= WIRELESS ||
	    (features->type >= INTUOS5S && features->type <= INTUOSPL) ||
	    (features->oVid && features->oPid))
		features->quirks |= WACOM_QUIRK_MULTI_INPUT;

	/* quirk for bamboo touch with 2 low res touches */
	if (features->type == BAMBOO_PT &&
	    features->pktlen == WACOM_PKGLEN_BBTOUCH) {
		features->x_max <<= 5;
		features->y_max <<= 5;
		features->x_fuzz <<= 5;
		features->y_fuzz <<= 5;
		features->quirks |= WACOM_QUIRK_BBTOUCH_LOWRES;
	}

	if (features->type == WIRELESS) {

		/* monitor never has input and pen/touch have delayed create */
		features->quirks |= WACOM_QUIRK_NO_INPUT;

		/* must be monitor interface if no device_type set */
		if (!features->device_type)
			features->quirks |= WACOM_QUIRK_MONITOR;
	}
}

static void wacom_abs_set_axis(struct input_dev *input_dev,
			       struct wacom_wac *wacom_wac)
{
	struct wacom_features *features = &wacom_wac->features;

	if (features->device_type == BTN_TOOL_PEN) {
		input_set_abs_params(input_dev, ABS_X, 0, features->x_max,
				     features->x_fuzz, 0);
		input_set_abs_params(input_dev, ABS_Y, 0, features->y_max,
				     features->y_fuzz, 0);
		input_set_abs_params(input_dev, ABS_PRESSURE, 0,
			features->pressure_max, features->pressure_fuzz, 0);

		/* penabled devices have fixed resolution for each model */
		input_abs_set_res(input_dev, ABS_X, features->x_resolution);
		input_abs_set_res(input_dev, ABS_Y, features->y_resolution);
	} else {
		if (features->touch_max <= 2) {
			input_set_abs_params(input_dev, ABS_X, 0,
				features->x_max, features->x_fuzz, 0);
			input_set_abs_params(input_dev, ABS_Y, 0,
				features->y_max, features->y_fuzz, 0);
			input_abs_set_res(input_dev, ABS_X,
					  features->x_resolution);
			input_abs_set_res(input_dev, ABS_Y,
					  features->y_resolution);
		}

		if (features->touch_max > 1) {
			input_set_abs_params(input_dev, ABS_MT_POSITION_X, 0,
				features->x_max, features->x_fuzz, 0);
			input_set_abs_params(input_dev, ABS_MT_POSITION_Y, 0,
				features->y_max, features->y_fuzz, 0);
			input_abs_set_res(input_dev, ABS_MT_POSITION_X,
					  features->x_resolution);
			input_abs_set_res(input_dev, ABS_MT_POSITION_Y,
					  features->y_resolution);
		}
	}
}

int wacom_setup_input_capabilities(struct input_dev *input_dev,
				   struct wacom_wac *wacom_wac)
{
	struct wacom_features *features = &wacom_wac->features;
	int i;

	input_dev->evbit[0] |= BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);

	__set_bit(BTN_TOUCH, input_dev->keybit);
	__set_bit(ABS_MISC, input_dev->absbit);

	wacom_abs_set_axis(input_dev, wacom_wac);

	switch (wacom_wac->features.type) {
	case WACOM_MO:
		input_set_abs_params(input_dev, ABS_WHEEL, 0, 71, 0, 0);
		/* fall through */

	case WACOM_G4:
		input_set_capability(input_dev, EV_MSC, MSC_SERIAL);

		__set_bit(BTN_BACK, input_dev->keybit);
		__set_bit(BTN_FORWARD, input_dev->keybit);
		/* fall through */

	case GRAPHIRE:
		input_set_capability(input_dev, EV_REL, REL_WHEEL);

		__set_bit(BTN_LEFT, input_dev->keybit);
		__set_bit(BTN_RIGHT, input_dev->keybit);
		__set_bit(BTN_MIDDLE, input_dev->keybit);

		__set_bit(BTN_TOOL_RUBBER, input_dev->keybit);
		__set_bit(BTN_TOOL_PEN, input_dev->keybit);
		__set_bit(BTN_TOOL_MOUSE, input_dev->keybit);
		__set_bit(BTN_STYLUS, input_dev->keybit);
		__set_bit(BTN_STYLUS2, input_dev->keybit);

		__set_bit(INPUT_PROP_POINTER, input_dev->propbit);
		break;

	case WACOM_24HD:
		__set_bit(BTN_A, input_dev->keybit);
		__set_bit(BTN_B, input_dev->keybit);
		__set_bit(BTN_C, input_dev->keybit);
		__set_bit(BTN_X, input_dev->keybit);
		__set_bit(BTN_Y, input_dev->keybit);
		__set_bit(BTN_Z, input_dev->keybit);

		for (i = 6; i < 10; i++)
			__set_bit(BTN_0 + i, input_dev->keybit);

		__set_bit(KEY_PROG1, input_dev->keybit);
		__set_bit(KEY_PROG2, input_dev->keybit);
		__set_bit(KEY_PROG3, input_dev->keybit);

		input_set_abs_params(input_dev, ABS_Z, -900, 899, 0, 0);
		input_set_abs_params(input_dev, ABS_THROTTLE, 0, 71, 0, 0);
		/* fall through */

	case DTK:
		for (i = 0; i < 6; i++)
			__set_bit(BTN_0 + i, input_dev->keybit);

		__set_bit(INPUT_PROP_DIRECT, input_dev->propbit);

		wacom_setup_cintiq(wacom_wac);
		break;

	case WACOM_22HD:
		__set_bit(KEY_PROG1, input_dev->keybit);
		__set_bit(KEY_PROG2, input_dev->keybit);
		__set_bit(KEY_PROG3, input_dev->keybit);
		/* fall through */

	case WACOM_21UX2:
		__set_bit(BTN_A, input_dev->keybit);
		__set_bit(BTN_B, input_dev->keybit);
		__set_bit(BTN_C, input_dev->keybit);
		__set_bit(BTN_X, input_dev->keybit);
		__set_bit(BTN_Y, input_dev->keybit);
		__set_bit(BTN_Z, input_dev->keybit);
		__set_bit(BTN_BASE, input_dev->keybit);
		__set_bit(BTN_BASE2, input_dev->keybit);
		/* fall through */

	case WACOM_BEE:
		__set_bit(BTN_8, input_dev->keybit);
		__set_bit(BTN_9, input_dev->keybit);
		/* fall through */

	case CINTIQ:
		for (i = 0; i < 8; i++)
			__set_bit(BTN_0 + i, input_dev->keybit);

		input_set_abs_params(input_dev, ABS_RX, 0, 4096, 0, 0);
		input_set_abs_params(input_dev, ABS_RY, 0, 4096, 0, 0);
		input_set_abs_params(input_dev, ABS_Z, -900, 899, 0, 0);

		__set_bit(INPUT_PROP_DIRECT, input_dev->propbit);

		wacom_setup_cintiq(wacom_wac);
		break;

	case WACOM_13HD:
		for (i = 0; i < 9; i++)
			__set_bit(BTN_0 + i, input_dev->keybit);

		input_set_abs_params(input_dev, ABS_Z, -900, 899, 0, 0);
		__set_bit(INPUT_PROP_DIRECT, input_dev->propbit);
		wacom_setup_cintiq(wacom_wac);
		break;

	case INTUOS3:
	case INTUOS3L:
		__set_bit(BTN_4, input_dev->keybit);
		__set_bit(BTN_5, input_dev->keybit);
		__set_bit(BTN_6, input_dev->keybit);
		__set_bit(BTN_7, input_dev->keybit);

		input_set_abs_params(input_dev, ABS_RY, 0, 4096, 0, 0);
		/* fall through */

	case INTUOS3S:
		__set_bit(BTN_0, input_dev->keybit);
		__set_bit(BTN_1, input_dev->keybit);
		__set_bit(BTN_2, input_dev->keybit);
		__set_bit(BTN_3, input_dev->keybit);

		input_set_abs_params(input_dev, ABS_RX, 0, 4096, 0, 0);
		input_set_abs_params(input_dev, ABS_Z, -900, 899, 0, 0);
		/* fall through */

	case INTUOS:
		__set_bit(INPUT_PROP_POINTER, input_dev->propbit);

		wacom_setup_intuos(wacom_wac);
		break;

	case INTUOS5:
	case INTUOS5L:
	case INTUOSPM:
	case INTUOSPL:
		if (features->device_type == BTN_TOOL_PEN) {
			__set_bit(BTN_7, input_dev->keybit);
			__set_bit(BTN_8, input_dev->keybit);
		}
		/* fall through */

	case INTUOS5S:
	case INTUOSPS:
		__set_bit(INPUT_PROP_POINTER, input_dev->propbit);

		if (features->device_type == BTN_TOOL_PEN) {
			for (i = 0; i < 7; i++)
				__set_bit(BTN_0 + i, input_dev->keybit);

			input_set_abs_params(input_dev, ABS_DISTANCE, 0,
					      features->distance_max,
					      0, 0);

			input_set_abs_params(input_dev, ABS_Z, -900, 899, 0, 0);

			wacom_setup_intuos(wacom_wac);
		} else if (features->device_type == BTN_TOOL_FINGER) {
			__clear_bit(ABS_MISC, input_dev->absbit);

			input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR,
			                     0, features->x_max, 0, 0);
			input_set_abs_params(input_dev, ABS_MT_TOUCH_MINOR,
			                     0, features->y_max, 0, 0);
			input_mt_init_slots(input_dev, features->touch_max, INPUT_MT_POINTER);
		}
		break;

	case INTUOS4:
	case INTUOS4L:
		__set_bit(BTN_7, input_dev->keybit);
		__set_bit(BTN_8, input_dev->keybit);
		/* fall through */

	case INTUOS4S:
		for (i = 0; i < 7; i++)
			__set_bit(BTN_0 + i, input_dev->keybit);

		input_set_abs_params(input_dev, ABS_Z, -900, 899, 0, 0);
		wacom_setup_intuos(wacom_wac);

		__set_bit(INPUT_PROP_POINTER, input_dev->propbit);
		break;

	case WACOM_24HDT:
		if (features->device_type == BTN_TOOL_FINGER) {
			input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR, 0, features->x_max, 0, 0);
			input_set_abs_params(input_dev, ABS_MT_WIDTH_MAJOR, 0, features->x_max, 0, 0);
			input_set_abs_params(input_dev, ABS_MT_WIDTH_MINOR, 0, features->y_max, 0, 0);
			input_set_abs_params(input_dev, ABS_MT_ORIENTATION, 0, 1, 0, 0);
		}
		/* fall through */

	case MTSCREEN:
	case MTTPC:
	case TABLETPC2FG:
		if (features->device_type == BTN_TOOL_FINGER) {
			unsigned int flags = INPUT_MT_DIRECT;

			if (wacom_wac->features.type == TABLETPC2FG)
				flags = 0;

			input_mt_init_slots(input_dev, features->touch_max, flags);
		}
		/* fall through */

	case TABLETPC:
	case TABLETPCE:
		__clear_bit(ABS_MISC, input_dev->absbit);

		__set_bit(INPUT_PROP_DIRECT, input_dev->propbit);

		if (features->device_type != BTN_TOOL_PEN)
			break;  /* no need to process stylus stuff */

		/* fall through */

	case PL:
	case DTU:
		__set_bit(BTN_TOOL_PEN, input_dev->keybit);
		__set_bit(BTN_TOOL_RUBBER, input_dev->keybit);
		__set_bit(BTN_STYLUS, input_dev->keybit);
		__set_bit(BTN_STYLUS2, input_dev->keybit);

		__set_bit(INPUT_PROP_DIRECT, input_dev->propbit);
		break;

	case PTU:
		__set_bit(BTN_STYLUS2, input_dev->keybit);
		/* fall through */

	case PENPARTNER:
		__set_bit(BTN_TOOL_PEN, input_dev->keybit);
		__set_bit(BTN_TOOL_RUBBER, input_dev->keybit);
		__set_bit(BTN_STYLUS, input_dev->keybit);

		__set_bit(INPUT_PROP_POINTER, input_dev->propbit);
		break;

	case BAMBOO_PT:
		__clear_bit(ABS_MISC, input_dev->absbit);

		__set_bit(INPUT_PROP_POINTER, input_dev->propbit);

		if (features->device_type == BTN_TOOL_FINGER) {
			unsigned int flags = INPUT_MT_POINTER;

			__set_bit(BTN_LEFT, input_dev->keybit);
			__set_bit(BTN_FORWARD, input_dev->keybit);
			__set_bit(BTN_BACK, input_dev->keybit);
			__set_bit(BTN_RIGHT, input_dev->keybit);

			if (features->pktlen == WACOM_PKGLEN_BBTOUCH3) {
				input_set_abs_params(input_dev,
						     ABS_MT_TOUCH_MAJOR,
						     0, features->x_max, 0, 0);
				input_set_abs_params(input_dev,
						     ABS_MT_TOUCH_MINOR,
						     0, features->y_max, 0, 0);
			} else {
				__set_bit(BTN_TOOL_FINGER, input_dev->keybit);
				__set_bit(BTN_TOOL_DOUBLETAP, input_dev->keybit);
				flags = 0;
			}
			input_mt_init_slots(input_dev, features->touch_max, flags);
		} else if (features->device_type == BTN_TOOL_PEN) {
			__set_bit(BTN_TOOL_RUBBER, input_dev->keybit);
			__set_bit(BTN_TOOL_PEN, input_dev->keybit);
			__set_bit(BTN_STYLUS, input_dev->keybit);
			__set_bit(BTN_STYLUS2, input_dev->keybit);
			input_set_abs_params(input_dev, ABS_DISTANCE, 0,
					      features->distance_max,
					      0, 0);
		}
		break;

	case CINTIQ_HYBRID:
		__set_bit(BTN_1, input_dev->keybit);
		__set_bit(BTN_2, input_dev->keybit);
		__set_bit(BTN_3, input_dev->keybit);
		__set_bit(BTN_4, input_dev->keybit);

		__set_bit(BTN_5, input_dev->keybit);
		__set_bit(BTN_6, input_dev->keybit);
		__set_bit(BTN_7, input_dev->keybit);
		__set_bit(BTN_8, input_dev->keybit);
		__set_bit(BTN_0, input_dev->keybit);

		input_set_abs_params(input_dev, ABS_Z, -900, 899, 0, 0);
		__set_bit(INPUT_PROP_DIRECT, input_dev->propbit);

		wacom_setup_cintiq(wacom_wac);
		break;
	}
	return 0;
}

static const struct wacom_features wacom_features_0x00 =
	{ "Wacom Penpartner",     WACOM_PKGLEN_PENPRTN,    5040,  3780,  255,
	  0, PENPARTNER, WACOM_PENPRTN_RES, WACOM_PENPRTN_RES };
static const struct wacom_features wacom_features_0x10 =
	{ "Wacom Graphire",       WACOM_PKGLEN_GRAPHIRE,  10206,  7422,  511,
	  63, GRAPHIRE, WACOM_GRAPHIRE_RES, WACOM_GRAPHIRE_RES };
static const struct wacom_features wacom_features_0x11 =
	{ "Wacom Graphire2 4x5",  WACOM_PKGLEN_GRAPHIRE,  10206,  7422,  511,
	  63, GRAPHIRE, WACOM_GRAPHIRE_RES, WACOM_GRAPHIRE_RES };
static const struct wacom_features wacom_features_0x12 =
	{ "Wacom Graphire2 5x7",  WACOM_PKGLEN_GRAPHIRE,  13918, 10206,  511,
	  63, GRAPHIRE, WACOM_GRAPHIRE_RES, WACOM_GRAPHIRE_RES };
static const struct wacom_features wacom_features_0x13 =
	{ "Wacom Graphire3",      WACOM_PKGLEN_GRAPHIRE,  10208,  7424,  511,
	  63, GRAPHIRE, WACOM_GRAPHIRE_RES, WACOM_GRAPHIRE_RES };
static const struct wacom_features wacom_features_0x14 =
	{ "Wacom Graphire3 6x8",  WACOM_PKGLEN_GRAPHIRE,  16704, 12064,  511,
	  63, GRAPHIRE, WACOM_GRAPHIRE_RES, WACOM_GRAPHIRE_RES };
static const struct wacom_features wacom_features_0x15 =
	{ "Wacom Graphire4 4x5",  WACOM_PKGLEN_GRAPHIRE,  10208,  7424,  511,
	  63, WACOM_G4, WACOM_GRAPHIRE_RES, WACOM_GRAPHIRE_RES };
static const struct wacom_features wacom_features_0x16 =
	{ "Wacom Graphire4 6x8",  WACOM_PKGLEN_GRAPHIRE,  16704, 12064,  511,
	  63, WACOM_G4, WACOM_GRAPHIRE_RES, WACOM_GRAPHIRE_RES };
static const struct wacom_features wacom_features_0x17 =
	{ "Wacom BambooFun 4x5",  WACOM_PKGLEN_BBFUN,     14760,  9225,  511,
	  63, WACOM_MO, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x18 =
	{ "Wacom BambooFun 6x8",  WACOM_PKGLEN_BBFUN,     21648, 13530,  511,
	  63, WACOM_MO, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x19 =
	{ "Wacom Bamboo1 Medium", WACOM_PKGLEN_GRAPHIRE,  16704, 12064,  511,
	  63, GRAPHIRE, WACOM_GRAPHIRE_RES, WACOM_GRAPHIRE_RES };
static const struct wacom_features wacom_features_0x60 =
	{ "Wacom Volito",         WACOM_PKGLEN_GRAPHIRE,   5104,  3712,  511,
	  63, GRAPHIRE, WACOM_VOLITO_RES, WACOM_VOLITO_RES };
static const struct wacom_features wacom_features_0x61 =
	{ "Wacom PenStation2",    WACOM_PKGLEN_GRAPHIRE,   3250,  2320,  255,
	  63, GRAPHIRE, WACOM_VOLITO_RES, WACOM_VOLITO_RES };
static const struct wacom_features wacom_features_0x62 =
	{ "Wacom Volito2 4x5",    WACOM_PKGLEN_GRAPHIRE,   5104,  3712,  511,
	  63, GRAPHIRE, WACOM_VOLITO_RES, WACOM_VOLITO_RES };
static const struct wacom_features wacom_features_0x63 =
	{ "Wacom Volito2 2x3",    WACOM_PKGLEN_GRAPHIRE,   3248,  2320,  511,
	  63, GRAPHIRE, WACOM_VOLITO_RES, WACOM_VOLITO_RES };
static const struct wacom_features wacom_features_0x64 =
	{ "Wacom PenPartner2",    WACOM_PKGLEN_GRAPHIRE,   3250,  2320,  511,
	  63, GRAPHIRE, WACOM_VOLITO_RES, WACOM_VOLITO_RES };
static const struct wacom_features wacom_features_0x65 =
	{ "Wacom Bamboo",         WACOM_PKGLEN_BBFUN,     14760,  9225,  511,
	  63, WACOM_MO, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x69 =
	{ "Wacom Bamboo1",        WACOM_PKGLEN_GRAPHIRE,   5104,  3712,  511,
	  63, GRAPHIRE, WACOM_PENPRTN_RES, WACOM_PENPRTN_RES };
static const struct wacom_features wacom_features_0x6A =
	{ "Wacom Bamboo1 4x6",    WACOM_PKGLEN_GRAPHIRE,  14760,  9225, 1023,
	  63, GRAPHIRE, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x6B =
	{ "Wacom Bamboo1 5x8",    WACOM_PKGLEN_GRAPHIRE,  21648, 13530, 1023,
	  63, GRAPHIRE, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x20 =
	{ "Wacom Intuos 4x5",     WACOM_PKGLEN_INTUOS,    12700, 10600, 1023,
	  31, INTUOS, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x21 =
	{ "Wacom Intuos 6x8",     WACOM_PKGLEN_INTUOS,    20320, 16240, 1023,
	  31, INTUOS, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x22 =
	{ "Wacom Intuos 9x12",    WACOM_PKGLEN_INTUOS,    30480, 24060, 1023,
	  31, INTUOS, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x23 =
	{ "Wacom Intuos 12x12",   WACOM_PKGLEN_INTUOS,    30480, 31680, 1023,
	  31, INTUOS, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x24 =
	{ "Wacom Intuos 12x18",   WACOM_PKGLEN_INTUOS,    45720, 31680, 1023,
	  31, INTUOS, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x30 =
	{ "Wacom PL400",          WACOM_PKGLEN_GRAPHIRE,   5408,  4056,  255,
	  0, PL, WACOM_PL_RES, WACOM_PL_RES };
static const struct wacom_features wacom_features_0x31 =
	{ "Wacom PL500",          WACOM_PKGLEN_GRAPHIRE,   6144,  4608,  255,
	  0, PL, WACOM_PL_RES, WACOM_PL_RES };
static const struct wacom_features wacom_features_0x32 =
	{ "Wacom PL600",          WACOM_PKGLEN_GRAPHIRE,   6126,  4604,  255,
	  0, PL, WACOM_PL_RES, WACOM_PL_RES };
static const struct wacom_features wacom_features_0x33 =
	{ "Wacom PL600SX",        WACOM_PKGLEN_GRAPHIRE,   6260,  5016,  255,
	  0, PL, WACOM_PL_RES, WACOM_PL_RES };
static const struct wacom_features wacom_features_0x34 =
	{ "Wacom PL550",          WACOM_PKGLEN_GRAPHIRE,   6144,  4608,  511,
	  0, PL, WACOM_PL_RES, WACOM_PL_RES };
static const struct wacom_features wacom_features_0x35 =
	{ "Wacom PL800",          WACOM_PKGLEN_GRAPHIRE,   7220,  5780,  511,
	  0, PL, WACOM_PL_RES, WACOM_PL_RES };
static const struct wacom_features wacom_features_0x37 =
	{ "Wacom PL700",          WACOM_PKGLEN_GRAPHIRE,   6758,  5406,  511,
	  0, PL, WACOM_PL_RES, WACOM_PL_RES };
static const struct wacom_features wacom_features_0x38 =
	{ "Wacom PL510",          WACOM_PKGLEN_GRAPHIRE,   6282,  4762,  511,
	  0, PL, WACOM_PL_RES, WACOM_PL_RES };
static const struct wacom_features wacom_features_0x39 =
	{ "Wacom DTU710",         WACOM_PKGLEN_GRAPHIRE,  34080, 27660,  511,
	  0, PL, WACOM_PL_RES, WACOM_PL_RES };
static const struct wacom_features wacom_features_0xC4 =
	{ "Wacom DTF521",         WACOM_PKGLEN_GRAPHIRE,   6282,  4762,  511,
	  0, PL, WACOM_PL_RES, WACOM_PL_RES };
static const struct wacom_features wacom_features_0xC0 =
	{ "Wacom DTF720",         WACOM_PKGLEN_GRAPHIRE,   6858,  5506,  511,
	  0, PL, WACOM_PL_RES, WACOM_PL_RES };
static const struct wacom_features wacom_features_0xC2 =
	{ "Wacom DTF720a",        WACOM_PKGLEN_GRAPHIRE,   6858,  5506,  511,
	  0, PL, WACOM_PL_RES, WACOM_PL_RES };
static const struct wacom_features wacom_features_0x03 =
	{ "Wacom Cintiq Partner", WACOM_PKGLEN_GRAPHIRE,  20480, 15360,  511,
	  0, PTU, WACOM_PL_RES, WACOM_PL_RES };
static const struct wacom_features wacom_features_0x41 =
	{ "Wacom Intuos2 4x5",    WACOM_PKGLEN_INTUOS,    12700, 10600, 1023,
	  31, INTUOS, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x42 =
	{ "Wacom Intuos2 6x8",    WACOM_PKGLEN_INTUOS,    20320, 16240, 1023,
	  31, INTUOS, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x43 =
	{ "Wacom Intuos2 9x12",   WACOM_PKGLEN_INTUOS,    30480, 24060, 1023,
	  31, INTUOS, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x44 =
	{ "Wacom Intuos2 12x12",  WACOM_PKGLEN_INTUOS,    30480, 31680, 1023,
	  31, INTUOS, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x45 =
	{ "Wacom Intuos2 12x18",  WACOM_PKGLEN_INTUOS,    45720, 31680, 1023,
	  31, INTUOS, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0xB0 =
	{ "Wacom Intuos3 4x5",    WACOM_PKGLEN_INTUOS,    25400, 20320, 1023,
	  63, INTUOS3S, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES };
static const struct wacom_features wacom_features_0xB1 =
	{ "Wacom Intuos3 6x8",    WACOM_PKGLEN_INTUOS,    40640, 30480, 1023,
	  63, INTUOS3, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES };
static const struct wacom_features wacom_features_0xB2 =
	{ "Wacom Intuos3 9x12",   WACOM_PKGLEN_INTUOS,    60960, 45720, 1023,
	  63, INTUOS3, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES };
static const struct wacom_features wacom_features_0xB3 =
	{ "Wacom Intuos3 12x12",  WACOM_PKGLEN_INTUOS,    60960, 60960, 1023,
	  63, INTUOS3L, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES };
static const struct wacom_features wacom_features_0xB4 =
	{ "Wacom Intuos3 12x19",  WACOM_PKGLEN_INTUOS,    97536, 60960, 1023,
	  63, INTUOS3L, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES };
static const struct wacom_features wacom_features_0xB5 =
	{ "Wacom Intuos3 6x11",   WACOM_PKGLEN_INTUOS,    54204, 31750, 1023,
	  63, INTUOS3, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES };
static const struct wacom_features wacom_features_0xB7 =
	{ "Wacom Intuos3 4x6",    WACOM_PKGLEN_INTUOS,    31496, 19685, 1023,
	  63, INTUOS3S, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES };
static const struct wacom_features wacom_features_0xB8 =
	{ "Wacom Intuos4 4x6",    WACOM_PKGLEN_INTUOS,    31496, 19685, 2047,
	  63, INTUOS4S, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES };
static const struct wacom_features wacom_features_0xB9 =
	{ "Wacom Intuos4 6x9",    WACOM_PKGLEN_INTUOS,    44704, 27940, 2047,
	  63, INTUOS4, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES };
static const struct wacom_features wacom_features_0xBA =
	{ "Wacom Intuos4 8x13",   WACOM_PKGLEN_INTUOS,    65024, 40640, 2047,
	  63, INTUOS4L, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES };
static const struct wacom_features wacom_features_0xBB =
	{ "Wacom Intuos4 12x19",  WACOM_PKGLEN_INTUOS,    97536, 60960, 2047,
	  63, INTUOS4L, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES };
static const struct wacom_features wacom_features_0xBC =
	{ "Wacom Intuos4 WL",     WACOM_PKGLEN_INTUOS,    40640, 25400, 2047,
	  63, INTUOS4, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES };
static const struct wacom_features wacom_features_0x26 =
	{ "Wacom Intuos5 touch S", WACOM_PKGLEN_INTUOS,  31496, 19685, 2047,
	  63, INTUOS5S, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES,
	  .touch_max = 16 };
static const struct wacom_features wacom_features_0x27 =
	{ "Wacom Intuos5 touch M", WACOM_PKGLEN_INTUOS,  44704, 27940, 2047,
	  63, INTUOS5, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES,
	  .touch_max = 16 };
static const struct wacom_features wacom_features_0x28 =
	{ "Wacom Intuos5 touch L", WACOM_PKGLEN_INTUOS, 65024, 40640, 2047,
	  63, INTUOS5L, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES,
	  .touch_max = 16 };
static const struct wacom_features wacom_features_0x29 =
	{ "Wacom Intuos5 S", WACOM_PKGLEN_INTUOS,  31496, 19685, 2047,
	  63, INTUOS5S, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES };
static const struct wacom_features wacom_features_0x2A =
	{ "Wacom Intuos5 M", WACOM_PKGLEN_INTUOS,  44704, 27940, 2047,
	  63, INTUOS5, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES };
static const struct wacom_features wacom_features_0x314 =
	{ "Wacom Intuos Pro S", WACOM_PKGLEN_INTUOS,  31496, 19685, 2047,
	  63, INTUOSPS, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES,
	  .touch_max = 16 };
static const struct wacom_features wacom_features_0x315 =
	{ "Wacom Intuos Pro M", WACOM_PKGLEN_INTUOS,  44704, 27940, 2047,
	  63, INTUOSPM, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES,
	  .touch_max = 16 };
static const struct wacom_features wacom_features_0x317 =
	{ "Wacom Intuos Pro L", WACOM_PKGLEN_INTUOS,  65024, 40640, 2047,
	  63, INTUOSPL, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES,
	  .touch_max = 16 };
static const struct wacom_features wacom_features_0xF4 =
	{ "Wacom Cintiq 24HD",       WACOM_PKGLEN_INTUOS,   104480, 65600, 2047,
	  63, WACOM_24HD, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES };
static const struct wacom_features wacom_features_0xF8 =
	{ "Wacom Cintiq 24HD touch", WACOM_PKGLEN_INTUOS,   104480, 65600, 2047, /* Pen */
	  63, WACOM_24HD, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES,
	  .oVid = USB_VENDOR_ID_WACOM, .oPid = 0xf6 };
static const struct wacom_features wacom_features_0xF6 =
	{ "Wacom Cintiq 24HD touch", .type = WACOM_24HDT, /* Touch */
	  .oVid = USB_VENDOR_ID_WACOM, .oPid = 0xf8, .touch_max = 10 };
static const struct wacom_features wacom_features_0x3F =
	{ "Wacom Cintiq 21UX",    WACOM_PKGLEN_INTUOS,    87200, 65600, 1023,
	  63, CINTIQ, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES };
static const struct wacom_features wacom_features_0xC5 =
	{ "Wacom Cintiq 20WSX",   WACOM_PKGLEN_INTUOS,    86680, 54180, 1023,
	  63, WACOM_BEE, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES };
static const struct wacom_features wacom_features_0xC6 =
	{ "Wacom Cintiq 12WX",    WACOM_PKGLEN_INTUOS,    53020, 33440, 1023,
	  63, WACOM_BEE, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES };
static const struct wacom_features wacom_features_0x304 =
	{ "Wacom Cintiq 13HD",    WACOM_PKGLEN_INTUOS,    59552, 33848, 1023,
	  63, WACOM_13HD, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES };
static const struct wacom_features wacom_features_0xC7 =
	{ "Wacom DTU1931",        WACOM_PKGLEN_GRAPHIRE,  37832, 30305,  511,
	  0, PL, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0xCE =
	{ "Wacom DTU2231",        WACOM_PKGLEN_GRAPHIRE,  47864, 27011,  511,
	  0, DTU, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0xF0 =
	{ "Wacom DTU1631",        WACOM_PKGLEN_GRAPHIRE,  34623, 19553,  511,
	  0, DTU, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x57 =
	{ "Wacom DTK2241",        WACOM_PKGLEN_INTUOS,    95840, 54260, 2047,
	  63, DTK, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES};
static const struct wacom_features wacom_features_0x59 = /* Pen */
	{ "Wacom DTH2242",        WACOM_PKGLEN_INTUOS,    95840, 54260, 2047,
	  63, DTK, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES,
	  .oVid = USB_VENDOR_ID_WACOM, .oPid = 0x5D };
static const struct wacom_features wacom_features_0x5D = /* Touch */
	{ "Wacom DTH2242",       .type = WACOM_24HDT,
	  .oVid = USB_VENDOR_ID_WACOM, .oPid = 0x59, .touch_max = 10 };
static const struct wacom_features wacom_features_0xCC =
	{ "Wacom Cintiq 21UX2",   WACOM_PKGLEN_INTUOS,    87200, 65600, 2047,
	  63, WACOM_21UX2, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES };
static const struct wacom_features wacom_features_0xFA =
	{ "Wacom Cintiq 22HD",    WACOM_PKGLEN_INTUOS,    95840, 54260, 2047,
	  63, WACOM_22HD, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES };
static const struct wacom_features wacom_features_0x5B =
	{ "Wacom Cintiq 22HDT", WACOM_PKGLEN_INTUOS,      95840, 54260, 2047,
	  63, WACOM_22HD, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES,
	  .oVid = USB_VENDOR_ID_WACOM, .oPid = 0x5e };
static const struct wacom_features wacom_features_0x5E =
	{ "Wacom Cintiq 22HDT", .type = WACOM_24HDT,
	  .oVid = USB_VENDOR_ID_WACOM, .oPid = 0x5b, .touch_max = 10 };
static const struct wacom_features wacom_features_0x90 =
	{ "Wacom ISDv4 90",       WACOM_PKGLEN_GRAPHIRE,  26202, 16325,  255,
	  0, TABLETPC, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x93 =
	{ "Wacom ISDv4 93",       WACOM_PKGLEN_GRAPHIRE,  26202, 16325,  255,
	  0, TABLETPC, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x97 =
	{ "Wacom ISDv4 97",       WACOM_PKGLEN_GRAPHIRE,  26202, 16325,  511,
	  0, TABLETPC, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x9A =
	{ "Wacom ISDv4 9A",       WACOM_PKGLEN_GRAPHIRE,  26202, 16325,  255,
	  0, TABLETPC, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x9F =
	{ "Wacom ISDv4 9F",       WACOM_PKGLEN_GRAPHIRE,  26202, 16325,  255,
	  0, TABLETPC, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0xE2 =
	{ "Wacom ISDv4 E2",       WACOM_PKGLEN_TPC2FG,    26202, 16325,  255,
	  0, TABLETPC2FG, WACOM_INTUOS_RES, WACOM_INTUOS_RES,
	  .touch_max = 2 };
static const struct wacom_features wacom_features_0xE3 =
	{ "Wacom ISDv4 E3",       WACOM_PKGLEN_TPC2FG,    26202, 16325,  255,
	  0, TABLETPC2FG, WACOM_INTUOS_RES, WACOM_INTUOS_RES,
	  .touch_max = 2 };
static const struct wacom_features wacom_features_0xE5 =
	{ "Wacom ISDv4 E5",       WACOM_PKGLEN_MTOUCH,    26202, 16325,  255,
	  0, MTSCREEN, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0xE6 =
	{ "Wacom ISDv4 E6",       WACOM_PKGLEN_TPC2FG,    27760, 15694,  255,
	  0, TABLETPC2FG, WACOM_INTUOS_RES, WACOM_INTUOS_RES,
	  .touch_max = 2 };
static const struct wacom_features wacom_features_0xEC =
	{ "Wacom ISDv4 EC",       WACOM_PKGLEN_GRAPHIRE,  25710, 14500,  255,
	  0, TABLETPC,    WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0xED =
	{ "Wacom ISDv4 ED",       WACOM_PKGLEN_GRAPHIRE,  26202, 16325,  255,
	  0, TABLETPCE, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0xEF =
	{ "Wacom ISDv4 EF",       WACOM_PKGLEN_GRAPHIRE,  26202, 16325,  255,
	  0, TABLETPC, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x100 =
	{ "Wacom ISDv4 100",      WACOM_PKGLEN_MTTPC,     26202, 16325,  255,
	  0, MTTPC, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x101 =
	{ "Wacom ISDv4 101",      WACOM_PKGLEN_MTTPC,     26202, 16325,  255,
	  0, MTTPC, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x10D =
	{ "Wacom ISDv4 10D",      WACOM_PKGLEN_MTTPC,     26202, 16325,  255,
	  0, MTTPC, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x10E =
	{ "Wacom ISDv4 10E",      WACOM_PKGLEN_MTTPC,     27760, 15694,  255,
	  0, MTTPC, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x10F =
	{ "Wacom ISDv4 10F",      WACOM_PKGLEN_MTTPC,     27760, 15694,  255,
	  0, MTTPC, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x4001 =
	{ "Wacom ISDv4 4001",      WACOM_PKGLEN_MTTPC,     26202, 16325,  255,
	  0, MTTPC, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x47 =
	{ "Wacom Intuos2 6x8",    WACOM_PKGLEN_INTUOS,    20320, 16240, 1023,
	  31, INTUOS, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x84 =
	{ "Wacom Wireless Receiver", WACOM_PKGLEN_WIRELESS, 0, 0, 0,
	  0, WIRELESS, 0, 0, .touch_max = 16 };
static const struct wacom_features wacom_features_0xD0 =
	{ "Wacom Bamboo 2FG",     WACOM_PKGLEN_BBFUN,     14720,  9200, 1023,
	  31, BAMBOO_PT, WACOM_INTUOS_RES, WACOM_INTUOS_RES,
	  .touch_max = 2 };
static const struct wacom_features wacom_features_0xD1 =
	{ "Wacom Bamboo 2FG 4x5", WACOM_PKGLEN_BBFUN,     14720,  9200, 1023,
	  31, BAMBOO_PT, WACOM_INTUOS_RES, WACOM_INTUOS_RES,
	  .touch_max = 2 };
static const struct wacom_features wacom_features_0xD2 =
	{ "Wacom Bamboo Craft",   WACOM_PKGLEN_BBFUN,     14720,  9200, 1023,
	  31, BAMBOO_PT, WACOM_INTUOS_RES, WACOM_INTUOS_RES,
	  .touch_max = 2 };
static const struct wacom_features wacom_features_0xD3 =
	{ "Wacom Bamboo 2FG 6x8", WACOM_PKGLEN_BBFUN,     21648, 13700, 1023,
	  31, BAMBOO_PT, WACOM_INTUOS_RES, WACOM_INTUOS_RES,
	  .touch_max = 2 };
static const struct wacom_features wacom_features_0xD4 =
	{ "Wacom Bamboo Pen",     WACOM_PKGLEN_BBFUN,     14720,  9200, 1023,
	  31, BAMBOO_PT, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0xD5 =
	{ "Wacom Bamboo Pen 6x8",     WACOM_PKGLEN_BBFUN, 21648, 13700, 1023,
	  31, BAMBOO_PT, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0xD6 =
	{ "Wacom BambooPT 2FG 4x5", WACOM_PKGLEN_BBFUN,   14720,  9200, 1023,
	  31, BAMBOO_PT, WACOM_INTUOS_RES, WACOM_INTUOS_RES,
	  .touch_max = 2 };
static const struct wacom_features wacom_features_0xD7 =
	{ "Wacom BambooPT 2FG Small", WACOM_PKGLEN_BBFUN, 14720,  9200, 1023,
	  31, BAMBOO_PT, WACOM_INTUOS_RES, WACOM_INTUOS_RES,
	  .touch_max = 2 };
static const struct wacom_features wacom_features_0xD8 =
	{ "Wacom Bamboo Comic 2FG", WACOM_PKGLEN_BBFUN,   21648, 13700, 1023,
	  31, BAMBOO_PT, WACOM_INTUOS_RES, WACOM_INTUOS_RES,
	  .touch_max = 2 };
static const struct wacom_features wacom_features_0xDA =
	{ "Wacom Bamboo 2FG 4x5 SE", WACOM_PKGLEN_BBFUN,  14720,  9200, 1023,
	  31, BAMBOO_PT, WACOM_INTUOS_RES, WACOM_INTUOS_RES,
	  .touch_max = 2 };
static const struct wacom_features wacom_features_0xDB =
	{ "Wacom Bamboo 2FG 6x8 SE", WACOM_PKGLEN_BBFUN,  21648, 13700, 1023,
	  31, BAMBOO_PT, WACOM_INTUOS_RES, WACOM_INTUOS_RES,
	  .touch_max = 2 };
static const struct wacom_features wacom_features_0xDD =
        { "Wacom Bamboo Connect", WACOM_PKGLEN_BBPEN,     14720,  9200, 1023,
          31, BAMBOO_PT, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0xDE =
        { "Wacom Bamboo 16FG 4x5", WACOM_PKGLEN_BBPEN,    14720,  9200, 1023,
	  31, BAMBOO_PT, WACOM_INTUOS_RES, WACOM_INTUOS_RES,
	  .touch_max = 16 };
static const struct wacom_features wacom_features_0xDF =
        { "Wacom Bamboo 16FG 6x8", WACOM_PKGLEN_BBPEN,    21648, 13700, 1023,
	  31, BAMBOO_PT, WACOM_INTUOS_RES, WACOM_INTUOS_RES,
	  .touch_max = 16 };
static const struct wacom_features wacom_features_0x300 =
	{ "Wacom Bamboo One S",    WACOM_PKGLEN_BBPEN,    14720,  9225, 1023,
	  31, BAMBOO_PT, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x301 =
	{ "Wacom Bamboo One M",    WACOM_PKGLEN_BBPEN,    21648, 13530, 1023,
	  31, BAMBOO_PT, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x6004 =
	{ "ISD-V4",               WACOM_PKGLEN_GRAPHIRE,  12800,  8000,  255,
	  0, TABLETPC, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x0307 =
	{ "Wacom ISDv5 307", WACOM_PKGLEN_INTUOS,  59552,  33848, 2047,
	  63, CINTIQ_HYBRID, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES,
	  .oVid = USB_VENDOR_ID_WACOM, .oPid = 0x309 };
static const struct wacom_features wacom_features_0x0309 =
	{ "Wacom ISDv5 309", .type = WACOM_24HDT, /* Touch */
	  .oVid = USB_VENDOR_ID_WACOM, .oPid = 0x0307, .touch_max = 10 };

#define USB_DEVICE_WACOM(prod)					\
	USB_DEVICE(USB_VENDOR_ID_WACOM, prod),			\
	.driver_info = (kernel_ulong_t)&wacom_features_##prod

#define USB_DEVICE_DETAILED(prod, class, sub, proto)			\
	USB_DEVICE_AND_INTERFACE_INFO(USB_VENDOR_ID_WACOM, prod, class,	\
				      sub, proto),			\
	.driver_info = (kernel_ulong_t)&wacom_features_##prod

#define USB_DEVICE_LENOVO(prod)					\
	USB_DEVICE(USB_VENDOR_ID_LENOVO, prod),			\
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
	{ USB_DEVICE_WACOM(0x6A) },
	{ USB_DEVICE_WACOM(0x6B) },
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
	{ USB_DEVICE_WACOM(0x57) },
	{ USB_DEVICE_WACOM(0x59) },
	{ USB_DEVICE_DETAILED(0x5D, USB_CLASS_HID, 0, 0) },
	{ USB_DEVICE_WACOM(0x5B) },
	{ USB_DEVICE_DETAILED(0x5E, USB_CLASS_HID, 0, 0) },
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
	{ USB_DEVICE_WACOM(0xBC) },
	{ USB_DEVICE_WACOM(0x26) },
	{ USB_DEVICE_WACOM(0x27) },
	{ USB_DEVICE_WACOM(0x28) },
	{ USB_DEVICE_WACOM(0x29) },
	{ USB_DEVICE_WACOM(0x2A) },
	{ USB_DEVICE_WACOM(0x3F) },
	{ USB_DEVICE_WACOM(0xC5) },
	{ USB_DEVICE_WACOM(0xC6) },
	{ USB_DEVICE_WACOM(0xC7) },
	/*
	 * DTU-2231 has two interfaces on the same configuration,
	 * only one is used.
	 */
	{ USB_DEVICE_DETAILED(0xCE, USB_CLASS_HID,
			      USB_INTERFACE_SUBCLASS_BOOT,
			      USB_INTERFACE_PROTOCOL_MOUSE) },
	{ USB_DEVICE_WACOM(0x84) },
	{ USB_DEVICE_WACOM(0xD0) },
	{ USB_DEVICE_WACOM(0xD1) },
	{ USB_DEVICE_WACOM(0xD2) },
	{ USB_DEVICE_WACOM(0xD3) },
	{ USB_DEVICE_WACOM(0xD4) },
	{ USB_DEVICE_WACOM(0xD5) },
	{ USB_DEVICE_WACOM(0xD6) },
	{ USB_DEVICE_WACOM(0xD7) },
	{ USB_DEVICE_WACOM(0xD8) },
	{ USB_DEVICE_WACOM(0xDA) },
	{ USB_DEVICE_WACOM(0xDB) },
	{ USB_DEVICE_WACOM(0xDD) },
	{ USB_DEVICE_WACOM(0xDE) },
	{ USB_DEVICE_WACOM(0xDF) },
	{ USB_DEVICE_WACOM(0xF0) },
	{ USB_DEVICE_WACOM(0xCC) },
	{ USB_DEVICE_WACOM(0x90) },
	{ USB_DEVICE_WACOM(0x93) },
	{ USB_DEVICE_WACOM(0x97) },
	{ USB_DEVICE_WACOM(0x9A) },
	{ USB_DEVICE_WACOM(0x9F) },
	{ USB_DEVICE_WACOM(0xE2) },
	{ USB_DEVICE_WACOM(0xE3) },
	{ USB_DEVICE_WACOM(0xE5) },
	{ USB_DEVICE_WACOM(0xE6) },
	{ USB_DEVICE_WACOM(0xEC) },
	{ USB_DEVICE_WACOM(0xED) },
	{ USB_DEVICE_WACOM(0xEF) },
	{ USB_DEVICE_WACOM(0x100) },
	{ USB_DEVICE_WACOM(0x101) },
	{ USB_DEVICE_WACOM(0x10D) },
	{ USB_DEVICE_WACOM(0x10E) },
	{ USB_DEVICE_WACOM(0x10F) },
	{ USB_DEVICE_WACOM(0x300) },
	{ USB_DEVICE_WACOM(0x301) },
	{ USB_DEVICE_WACOM(0x304) },
	{ USB_DEVICE_DETAILED(0x314, USB_CLASS_HID, 0, 0) },
	{ USB_DEVICE_DETAILED(0x315, USB_CLASS_HID, 0, 0) },
	{ USB_DEVICE_DETAILED(0x317, USB_CLASS_HID, 0, 0) },
	{ USB_DEVICE_WACOM(0x4001) },
	{ USB_DEVICE_WACOM(0x47) },
	{ USB_DEVICE_WACOM(0xF4) },
	{ USB_DEVICE_WACOM(0xF8) },
	{ USB_DEVICE_DETAILED(0xF6, USB_CLASS_HID, 0, 0) },
	{ USB_DEVICE_WACOM(0xFA) },
	{ USB_DEVICE_WACOM(0x0307) },
	{ USB_DEVICE_DETAILED(0x0309, USB_CLASS_HID, 0, 0) },
	{ USB_DEVICE_LENOVO(0x6004) },
	{ }
};
MODULE_DEVICE_TABLE(usb, wacom_ids);
