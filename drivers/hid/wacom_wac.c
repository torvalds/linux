// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * drivers/input/tablet/wacom_wac.c
 *
 *  USB Wacom tablet support - Wacom specific code
 */

/*
 */

#include "wacom_wac.h"
#include "wacom.h"
#include <linux/input/mt.h>

/* resolution for penabled devices */
#define WACOM_PL_RES		20
#define WACOM_PENPRTN_RES	40
#define WACOM_VOLITO_RES	50
#define WACOM_GRAPHIRE_RES	80
#define WACOM_INTUOS_RES	100
#define WACOM_INTUOS3_RES	200

/* Newer Cintiq and DTU have an offset between tablet and screen areas */
#define WACOM_DTU_OFFSET	200
#define WACOM_CINTIQ_OFFSET	400

/*
 * Scale factor relating reported contact size to logical contact area.
 * 2^14/pi is a good approximation on Intuos5 and 3rd-gen Bamboo
 */
#define WACOM_CONTACT_AREA_SCALE 2607

static bool touch_arbitration = 1;
module_param(touch_arbitration, bool, 0644);
MODULE_PARM_DESC(touch_arbitration, " on (Y) off (N)");

static void wacom_report_numbered_buttons(struct input_dev *input_dev,
				int button_count, int mask);

static int wacom_numbered_button_to_key(int n);

static void wacom_update_led(struct wacom *wacom, int button_count, int mask,
			     int group);
/*
 * Percent of battery capacity for Graphire.
 * 8th value means AC online and show 100% capacity.
 */
static unsigned short batcap_gr[8] = { 1, 15, 25, 35, 50, 70, 100, 100 };

/*
 * Percent of battery capacity for Intuos4 WL, AC has a separate bit.
 */
static unsigned short batcap_i4[8] = { 1, 15, 30, 45, 60, 70, 85, 100 };

static void __wacom_notify_battery(struct wacom_battery *battery,
				   int bat_status, int bat_capacity,
				   bool bat_charging, bool bat_connected,
				   bool ps_connected)
{
	bool changed = battery->bat_status       != bat_status    ||
		       battery->battery_capacity != bat_capacity  ||
		       battery->bat_charging     != bat_charging  ||
		       battery->bat_connected    != bat_connected ||
		       battery->ps_connected     != ps_connected;

	if (changed) {
		battery->bat_status = bat_status;
		battery->battery_capacity = bat_capacity;
		battery->bat_charging = bat_charging;
		battery->bat_connected = bat_connected;
		battery->ps_connected = ps_connected;

		if (battery->battery)
			power_supply_changed(battery->battery);
	}
}

static void wacom_notify_battery(struct wacom_wac *wacom_wac,
	int bat_status, int bat_capacity, bool bat_charging,
	bool bat_connected, bool ps_connected)
{
	struct wacom *wacom = container_of(wacom_wac, struct wacom, wacom_wac);

	__wacom_notify_battery(&wacom->battery, bat_status, bat_capacity,
			       bat_charging, bat_connected, ps_connected);
}

static int wacom_penpartner_irq(struct wacom_wac *wacom)
{
	unsigned char *data = wacom->data;
	struct input_dev *input = wacom->pen_input;

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
	struct input_dev *input = wacom->pen_input;
	int prox, pressure;

	if (data[0] != WACOM_REPORT_PENABLED) {
		dev_dbg(input->dev.parent,
			"%s: received unknown report #%d\n", __func__, data[0]);
		return 0;
	}

	prox = data[1] & 0x40;

	if (!wacom->id[0]) {
		if ((data[0] & 0x10) || (data[4] & 0x20)) {
			wacom->tool[0] = BTN_TOOL_RUBBER;
			wacom->id[0] = ERASER_DEVICE_ID;
		}
		else {
			wacom->tool[0] = BTN_TOOL_PEN;
			wacom->id[0] = STYLUS_DEVICE_ID;
		}
	}

	/* If the eraser is in prox, STYLUS2 is always set. If we
	 * mis-detected the type and notice that STYLUS2 isn't set
	 * then force the eraser out of prox and let the pen in.
	 */
	if (wacom->tool[0] == BTN_TOOL_RUBBER && !(data[4] & 0x20)) {
		input_report_key(input, BTN_TOOL_RUBBER, 0);
		input_report_abs(input, ABS_MISC, 0);
		input_sync(input);
		wacom->tool[0] = BTN_TOOL_PEN;
		wacom->id[0] = STYLUS_DEVICE_ID;
	}

	if (prox) {
		pressure = (signed char)((data[7] << 1) | ((data[4] >> 2) & 1));
		if (features->pressure_max > 255)
			pressure = (pressure << 1) | ((data[4] >> 6) & 1);
		pressure += (features->pressure_max + 1) / 2;

		input_report_abs(input, ABS_X, data[3] | (data[2] << 7) | ((data[1] & 0x03) << 14));
		input_report_abs(input, ABS_Y, data[6] | (data[5] << 7) | ((data[4] & 0x03) << 14));
		input_report_abs(input, ABS_PRESSURE, pressure);

		input_report_key(input, BTN_TOUCH, data[4] & 0x08);
		input_report_key(input, BTN_STYLUS, data[4] & 0x10);
		/* Only allow the stylus2 button to be reported for the pen tool. */
		input_report_key(input, BTN_STYLUS2, (wacom->tool[0] == BTN_TOOL_PEN) && (data[4] & 0x20));
	}

	if (!prox)
		wacom->id[0] = 0;
	input_report_key(input, wacom->tool[0], prox);
	input_report_abs(input, ABS_MISC, wacom->id[0]);
	return 1;
}

static int wacom_ptu_irq(struct wacom_wac *wacom)
{
	unsigned char *data = wacom->data;
	struct input_dev *input = wacom->pen_input;

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
	unsigned char *data = wacom->data;
	struct input_dev *input = wacom->pen_input;
	int prox = data[1] & 0x20;

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
	input_report_abs(input, ABS_PRESSURE, ((data[7] & 0x01) << 8) | data[6]);
	input_report_key(input, BTN_TOUCH, data[1] & 0x05);
	if (!prox) /* out-prox */
		wacom->id[0] = 0;
	input_report_key(input, wacom->tool[0], prox);
	input_report_abs(input, ABS_MISC, wacom->id[0]);
	return 1;
}

static int wacom_dtus_irq(struct wacom_wac *wacom)
{
	char *data = wacom->data;
	struct input_dev *input = wacom->pen_input;
	unsigned short prox, pressure = 0;

	if (data[0] != WACOM_REPORT_DTUS && data[0] != WACOM_REPORT_DTUSPAD) {
		dev_dbg(input->dev.parent,
			"%s: received unknown report #%d", __func__, data[0]);
		return 0;
	} else if (data[0] == WACOM_REPORT_DTUSPAD) {
		input = wacom->pad_input;
		input_report_key(input, BTN_0, (data[1] & 0x01));
		input_report_key(input, BTN_1, (data[1] & 0x02));
		input_report_key(input, BTN_2, (data[1] & 0x04));
		input_report_key(input, BTN_3, (data[1] & 0x08));
		input_report_abs(input, ABS_MISC,
				 data[1] & 0x0f ? PAD_DEVICE_ID : 0);
		return 1;
	} else {
		prox = data[1] & 0x80;
		if (prox) {
			switch ((data[1] >> 3) & 3) {
			case 1: /* Rubber */
				wacom->tool[0] = BTN_TOOL_RUBBER;
				wacom->id[0] = ERASER_DEVICE_ID;
				break;

			case 2: /* Pen */
				wacom->tool[0] = BTN_TOOL_PEN;
				wacom->id[0] = STYLUS_DEVICE_ID;
				break;
			}
		}

		input_report_key(input, BTN_STYLUS, data[1] & 0x20);
		input_report_key(input, BTN_STYLUS2, data[1] & 0x40);
		input_report_abs(input, ABS_X, get_unaligned_be16(&data[3]));
		input_report_abs(input, ABS_Y, get_unaligned_be16(&data[5]));
		pressure = ((data[1] & 0x03) << 8) | (data[2] & 0xff);
		input_report_abs(input, ABS_PRESSURE, pressure);
		input_report_key(input, BTN_TOUCH, pressure > 10);

		if (!prox) /* out-prox */
			wacom->id[0] = 0;
		input_report_key(input, wacom->tool[0], prox);
		input_report_abs(input, ABS_MISC, wacom->id[0]);
		return 1;
	}
}

static int wacom_graphire_irq(struct wacom_wac *wacom)
{
	struct wacom_features *features = &wacom->features;
	unsigned char *data = wacom->data;
	struct input_dev *input = wacom->pen_input;
	struct input_dev *pad_input = wacom->pad_input;
	int battery_capacity, ps_connected;
	int prox;
	int rw = 0;
	int retval = 0;

	if (features->type == GRAPHIRE_BT) {
		if (data[0] != WACOM_REPORT_PENABLED_BT) {
			dev_dbg(input->dev.parent,
				"%s: received unknown report #%d\n", __func__,
				data[0]);
			goto exit;
		}
	} else if (data[0] != WACOM_REPORT_PENABLED) {
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
			if (features->type == GRAPHIRE_BT)
				input_report_abs(input, ABS_PRESSURE, data[6] |
					(((__u16) (data[1] & 0x08)) << 5));
			else
				input_report_abs(input, ABS_PRESSURE, data[6] |
					((data[7] & 0x03) << 8));
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
			} else if (features->type == GRAPHIRE_BT) {
				/* Compute distance between mouse and tablet */
				rw = 44 - (data[6] >> 2);
				rw = clamp_val(rw, 0, 31);
				input_report_abs(input, ABS_DISTANCE, rw);
				if (((data[1] >> 5) & 3) == 2) {
					/* Mouse with wheel */
					input_report_key(input, BTN_MIDDLE,
							data[1] & 0x04);
					rw = (data[6] & 0x01) ? -1 :
						(data[6] & 0x02) ? 1 : 0;
				} else {
					rw = 0;
				}
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
		input_sync(input); /* sync last event */
	}

	/* send pad data */
	switch (features->type) {
	case WACOM_G4:
		prox = data[7] & 0xf8;
		if (prox || wacom->id[1]) {
			wacom->id[1] = PAD_DEVICE_ID;
			input_report_key(pad_input, BTN_BACK, (data[7] & 0x40));
			input_report_key(pad_input, BTN_FORWARD, (data[7] & 0x80));
			rw = ((data[7] & 0x18) >> 3) - ((data[7] & 0x20) >> 3);
			input_report_rel(pad_input, REL_WHEEL, rw);
			if (!prox)
				wacom->id[1] = 0;
			input_report_abs(pad_input, ABS_MISC, wacom->id[1]);
			retval = 1;
		}
		break;

	case WACOM_MO:
		prox = (data[7] & 0xf8) || data[8];
		if (prox || wacom->id[1]) {
			wacom->id[1] = PAD_DEVICE_ID;
			input_report_key(pad_input, BTN_BACK, (data[7] & 0x08));
			input_report_key(pad_input, BTN_LEFT, (data[7] & 0x20));
			input_report_key(pad_input, BTN_FORWARD, (data[7] & 0x10));
			input_report_key(pad_input, BTN_RIGHT, (data[7] & 0x40));
			input_report_abs(pad_input, ABS_WHEEL, (data[8] & 0x7f));
			if (!prox)
				wacom->id[1] = 0;
			input_report_abs(pad_input, ABS_MISC, wacom->id[1]);
			retval = 1;
		}
		break;
	case GRAPHIRE_BT:
		prox = data[7] & 0x03;
		if (prox || wacom->id[1]) {
			wacom->id[1] = PAD_DEVICE_ID;
			input_report_key(pad_input, BTN_0, (data[7] & 0x02));
			input_report_key(pad_input, BTN_1, (data[7] & 0x01));
			if (!prox)
				wacom->id[1] = 0;
			input_report_abs(pad_input, ABS_MISC, wacom->id[1]);
			retval = 1;
		}
		break;
	}

	/* Store current battery capacity and power supply state */
	if (features->type == GRAPHIRE_BT) {
		rw = (data[7] >> 2 & 0x07);
		battery_capacity = batcap_gr[rw];
		ps_connected = rw == 7;
		wacom_notify_battery(wacom, WACOM_POWER_SUPPLY_STATUS_AUTO,
				     battery_capacity, ps_connected, 1,
				     ps_connected);
	}
exit:
	return retval;
}

static void wacom_intuos_schedule_prox_event(struct wacom_wac *wacom_wac)
{
	struct wacom *wacom = container_of(wacom_wac, struct wacom, wacom_wac);
	struct wacom_features *features = &wacom_wac->features;
	struct hid_report *r;
	struct hid_report_enum *re;

	re = &(wacom->hdev->report_enum[HID_FEATURE_REPORT]);
	if (features->type == INTUOSHT2)
		r = re->report_id_hash[WACOM_REPORT_INTUOSHT2_ID];
	else
		r = re->report_id_hash[WACOM_REPORT_INTUOS_ID1];
	if (r) {
		hid_hw_request(wacom->hdev, r, HID_REQ_GET_REPORT);
	}
}

static int wacom_intuos_pad(struct wacom_wac *wacom)
{
	struct wacom_features *features = &wacom->features;
	unsigned char *data = wacom->data;
	struct input_dev *input = wacom->pad_input;
	int i;
	int buttons = 0, nbuttons = features->numbered_buttons;
	int keys = 0, nkeys = 0;
	int ring1 = 0, ring2 = 0;
	int strip1 = 0, strip2 = 0;
	bool prox = false;

	/* pad packets. Works as a second tool and is always in prox */
	if (!(data[0] == WACOM_REPORT_INTUOSPAD || data[0] == WACOM_REPORT_INTUOS5PAD ||
	      data[0] == WACOM_REPORT_CINTIQPAD))
		return 0;

	if (features->type >= INTUOS4S && features->type <= INTUOS4L) {
		buttons = (data[3] << 1) | (data[2] & 0x01);
		ring1 = data[1];
	} else if (features->type == DTK) {
		buttons = data[6];
	} else if (features->type == WACOM_13HD) {
		buttons = (data[4] << 1) | (data[3] & 0x01);
	} else if (features->type == WACOM_24HD) {
		buttons = (data[8] << 8) | data[6];
		ring1 = data[1];
		ring2 = data[2];

		/*
		 * Three "buttons" are available on the 24HD which are
		 * physically implemented as a touchstrip. Each button
		 * is approximately 3 bits wide with a 2 bit spacing.
		 * The raw touchstrip bits are stored at:
		 *    ((data[3] & 0x1f) << 8) | data[4])
		 */
		nkeys = 3;
		keys = ((data[3] & 0x1C) ? 1<<2 : 0) |
		       ((data[4] & 0xE0) ? 1<<1 : 0) |
		       ((data[4] & 0x07) ? 1<<0 : 0);
	} else if (features->type == WACOM_27QHD) {
		nkeys = 3;
		keys = data[2] & 0x07;

		input_report_abs(input, ABS_X, be16_to_cpup((__be16 *)&data[4]));
		input_report_abs(input, ABS_Y, be16_to_cpup((__be16 *)&data[6]));
		input_report_abs(input, ABS_Z, be16_to_cpup((__be16 *)&data[8]));
	} else if (features->type == CINTIQ_HYBRID) {
		/*
		 * Do not send hardware buttons under Android. They
		 * are already sent to the system through GPIO (and
		 * have different meaning).
		 *
		 * d-pad right  -> data[4] & 0x10
		 * d-pad up     -> data[4] & 0x20
		 * d-pad left   -> data[4] & 0x40
		 * d-pad down   -> data[4] & 0x80
		 * d-pad center -> data[3] & 0x01
		 */
		buttons = (data[4] << 1) | (data[3] & 0x01);
	} else if (features->type == CINTIQ_COMPANION_2) {
		/* d-pad right  -> data[2] & 0x10
		 * d-pad up     -> data[2] & 0x20
		 * d-pad left   -> data[2] & 0x40
		 * d-pad down   -> data[2] & 0x80
		 * d-pad center -> data[1] & 0x01
		 */
		buttons = ((data[2] >> 4) << 7) |
		          ((data[1] & 0x04) << 4) |
		          ((data[2] & 0x0F) << 2) |
		          (data[1] & 0x03);
	} else if (features->type >= INTUOS5S && features->type <= INTUOSPL) {
		/*
		 * ExpressKeys on Intuos5/Intuos Pro have a capacitive sensor in
		 * addition to the mechanical switch. Switch data is
		 * stored in data[4], capacitive data in data[5].
		 *
		 * Touch ring mode switch (data[3]) has no capacitive sensor
		 */
		buttons = (data[4] << 1) | (data[3] & 0x01);
		ring1 = data[2];
	} else {
		if (features->type == WACOM_21UX2 || features->type == WACOM_22HD) {
			buttons = (data[8] << 10) | ((data[7] & 0x01) << 9) |
			          (data[6] << 1) | (data[5] & 0x01);

			if (features->type == WACOM_22HD) {
				nkeys = 3;
				keys = data[9] & 0x07;
			}
		} else {
			buttons = ((data[6] & 0x10) << 5)  |
			          ((data[5] & 0x10) << 4)  |
			          ((data[6] & 0x0F) << 4)  |
			          (data[5] & 0x0F);
		}
		strip1 = ((data[1] & 0x1f) << 8) | data[2];
		strip2 = ((data[3] & 0x1f) << 8) | data[4];
	}

	prox = (buttons & ~(~0 << nbuttons)) | (keys & ~(~0 << nkeys)) |
	       (ring1 & 0x80) | (ring2 & 0x80) | strip1 | strip2;

	wacom_report_numbered_buttons(input, nbuttons, buttons);

	for (i = 0; i < nkeys; i++)
		input_report_key(input, KEY_PROG1 + i, keys & (1 << i));

	input_report_abs(input, ABS_RX, strip1);
	input_report_abs(input, ABS_RY, strip2);

	input_report_abs(input, ABS_WHEEL,    (ring1 & 0x80) ? (ring1 & 0x7f) : 0);
	input_report_abs(input, ABS_THROTTLE, (ring2 & 0x80) ? (ring2 & 0x7f) : 0);

	input_report_key(input, wacom->tool[1], prox ? 1 : 0);
	input_report_abs(input, ABS_MISC, prox ? PAD_DEVICE_ID : 0);

	input_event(input, EV_MSC, MSC_SERIAL, 0xffffffff);

	return 1;
}

static int wacom_intuos_id_mangle(int tool_id)
{
	return (tool_id & ~0xFFF) << 4 | (tool_id & 0xFFF);
}

static int wacom_intuos_get_tool_type(int tool_id)
{
	int tool_type;

	switch (tool_id) {
	case 0x812: /* Inking pen */
	case 0x801: /* Intuos3 Inking pen */
	case 0x12802: /* Intuos4/5 Inking Pen */
	case 0x012:
		tool_type = BTN_TOOL_PENCIL;
		break;

	case 0x822: /* Pen */
	case 0x842:
	case 0x852:
	case 0x823: /* Intuos3 Grip Pen */
	case 0x813: /* Intuos3 Classic Pen */
	case 0x885: /* Intuos3 Marker Pen */
	case 0x802: /* Intuos4/5 13HD/24HD General Pen */
	case 0x804: /* Intuos4/5 13HD/24HD Marker Pen */
	case 0x8e2: /* IntuosHT2 pen */
	case 0x022:
	case 0x10804: /* Intuos4/5 13HD/24HD Art Pen */
	case 0x10842: /* MobileStudio Pro Pro Pen slim */
	case 0x14802: /* Intuos4/5 13HD/24HD Classic Pen */
	case 0x16802: /* Cintiq 13HD Pro Pen */
	case 0x18802: /* DTH2242 Pen */
	case 0x10802: /* Intuos4/5 13HD/24HD General Pen */
		tool_type = BTN_TOOL_PEN;
		break;

	case 0x832: /* Stroke pen */
	case 0x032:
		tool_type = BTN_TOOL_BRUSH;
		break;

	case 0x007: /* Mouse 4D and 2D */
	case 0x09c:
	case 0x094:
	case 0x017: /* Intuos3 2D Mouse */
	case 0x806: /* Intuos4 Mouse */
		tool_type = BTN_TOOL_MOUSE;
		break;

	case 0x096: /* Lens cursor */
	case 0x097: /* Intuos3 Lens cursor */
	case 0x006: /* Intuos4 Lens cursor */
		tool_type = BTN_TOOL_LENS;
		break;

	case 0x82a: /* Eraser */
	case 0x84a:
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
	case 0x1480a: /* Intuos4/5 13HD/24HD Classic Pen Eraser */
	case 0x1090a: /* Intuos4/5 13HD/24HD Airbrush Eraser */
	case 0x1080c: /* Intuos4/5 13HD/24HD Art Pen Eraser */
	case 0x1084a: /* MobileStudio Pro Pro Pen slim Eraser */
	case 0x1680a: /* Cintiq 13HD Pro Pen Eraser */
	case 0x1880a: /* DTH2242 Eraser */
	case 0x1080a: /* Intuos4/5 13HD/24HD General Pen Eraser */
		tool_type = BTN_TOOL_RUBBER;
		break;

	case 0xd12:
	case 0x912:
	case 0x112:
	case 0x913: /* Intuos3 Airbrush */
	case 0x902: /* Intuos4/5 13HD/24HD Airbrush */
	case 0x10902: /* Intuos4/5 13HD/24HD Airbrush */
		tool_type = BTN_TOOL_AIRBRUSH;
		break;

	default: /* Unknown tool */
		tool_type = BTN_TOOL_PEN;
		break;
	}
	return tool_type;
}

static void wacom_exit_report(struct wacom_wac *wacom)
{
	struct input_dev *input = wacom->pen_input;
	struct wacom_features *features = &wacom->features;
	unsigned char *data = wacom->data;
	int idx = (features->type == INTUOS) ? (data[1] & 0x01) : 0;

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
}

static int wacom_intuos_inout(struct wacom_wac *wacom)
{
	struct wacom_features *features = &wacom->features;
	unsigned char *data = wacom->data;
	struct input_dev *input = wacom->pen_input;
	int idx = (features->type == INTUOS) ? (data[1] & 0x01) : 0;

	if (!(((data[1] & 0xfc) == 0xc0) ||  /* in prox */
	    ((data[1] & 0xfe) == 0x20) ||    /* in range */
	    ((data[1] & 0xfe) == 0x80)))     /* out prox */
		return 0;

	/* Enter report */
	if ((data[1] & 0xfc) == 0xc0) {
		/* serial number of the tool */
		wacom->serial[idx] = ((data[3] & 0x0f) << 28) +
			(data[4] << 20) + (data[5] << 12) +
			(data[6] << 4) + (data[7] >> 4);

		wacom->id[idx] = (data[2] << 4) | (data[3] >> 4) |
		     ((data[7] & 0x0f) << 16) | ((data[8] & 0xf0) << 8);

		wacom->tool[idx] = wacom_intuos_get_tool_type(wacom->id[idx]);

		wacom->shared->stylus_in_proximity = true;
		return 1;
	}

	/* in Range */
	if ((data[1] & 0xfe) == 0x20) {
		if (features->type != INTUOSHT2)
			wacom->shared->stylus_in_proximity = true;

		/* in Range while exiting */
		if (wacom->reporting_data) {
			input_report_key(input, BTN_TOUCH, 0);
			input_report_abs(input, ABS_PRESSURE, 0);
			input_report_abs(input, ABS_DISTANCE, wacom->features.distance_max);
			return 2;
		}
		return 1;
	}

	/* Exit report */
	if ((data[1] & 0xfe) == 0x80) {
		wacom->shared->stylus_in_proximity = false;
		wacom->reporting_data = false;

		/* don't report exit if we don't know the ID */
		if (!wacom->id[idx])
			return 1;

		wacom_exit_report(wacom);
		return 2;
	}

	return 0;
}

static inline bool report_touch_events(struct wacom_wac *wacom)
{
	return (touch_arbitration ? !wacom->shared->stylus_in_proximity : 1);
}

static inline bool delay_pen_events(struct wacom_wac *wacom)
{
	return (wacom->shared->touch_down && touch_arbitration);
}

static int wacom_intuos_general(struct wacom_wac *wacom)
{
	struct wacom_features *features = &wacom->features;
	unsigned char *data = wacom->data;
	struct input_dev *input = wacom->pen_input;
	int idx = (features->type == INTUOS) ? (data[1] & 0x01) : 0;
	unsigned char type = (data[1] >> 1) & 0x0F;
	unsigned int x, y, distance, t;

	if (data[0] != WACOM_REPORT_PENABLED && data[0] != WACOM_REPORT_CINTIQ &&
		data[0] != WACOM_REPORT_INTUOS_PEN)
		return 0;

	if (delay_pen_events(wacom))
		return 1;

	/* don't report events if we don't know the tool ID */
	if (!wacom->id[idx]) {
		/* but reschedule a read of the current tool */
		wacom_intuos_schedule_prox_event(wacom);
		return 1;
	}

	/*
	 * don't report events for invalid data
	 */
	/* older I4 styli don't work with new Cintiqs */
	if ((!((wacom->id[idx] >> 16) & 0x01) &&
			(features->type == WACOM_21UX2)) ||
	    /* Only large Intuos support Lense Cursor */
	    (wacom->tool[idx] == BTN_TOOL_LENS &&
		(features->type == INTUOS3 ||
		 features->type == INTUOS3S ||
		 features->type == INTUOS4 ||
		 features->type == INTUOS4S ||
		 features->type == INTUOS5 ||
		 features->type == INTUOS5S ||
		 features->type == INTUOSPM ||
		 features->type == INTUOSPS)) ||
	   /* Cintiq doesn't send data when RDY bit isn't set */
	   (features->type == CINTIQ && !(data[1] & 0x40)))
		return 1;

	x = (be16_to_cpup((__be16 *)&data[2]) << 1) | ((data[9] >> 1) & 1);
	y = (be16_to_cpup((__be16 *)&data[4]) << 1) | (data[9] & 1);
	distance = data[9] >> 2;
	if (features->type < INTUOS3S) {
		x >>= 1;
		y >>= 1;
		distance >>= 1;
	}
	if (features->type == INTUOSHT2)
		distance = features->distance_max - distance;
	input_report_abs(input, ABS_X, x);
	input_report_abs(input, ABS_Y, y);
	input_report_abs(input, ABS_DISTANCE, distance);

	switch (type) {
	case 0x00:
	case 0x01:
	case 0x02:
	case 0x03:
		/* general pen packet */
		t = (data[6] << 3) | ((data[7] & 0xC0) >> 5) | (data[1] & 1);
		if (features->pressure_max < 2047)
			t >>= 1;
		input_report_abs(input, ABS_PRESSURE, t);
		if (features->type != INTUOSHT2) {
		    input_report_abs(input, ABS_TILT_X,
				 (((data[7] << 1) & 0x7e) | (data[8] >> 7)) - 64);
		    input_report_abs(input, ABS_TILT_Y, (data[8] & 0x7f) - 64);
		}
		input_report_key(input, BTN_STYLUS, data[1] & 2);
		input_report_key(input, BTN_STYLUS2, data[1] & 4);
		input_report_key(input, BTN_TOUCH, t > 10);
		break;

	case 0x0a:
		/* airbrush second packet */
		input_report_abs(input, ABS_WHEEL,
				(data[6] << 2) | ((data[7] >> 6) & 3));
		input_report_abs(input, ABS_TILT_X,
				 (((data[7] << 1) & 0x7e) | (data[8] >> 7)) - 64);
		input_report_abs(input, ABS_TILT_Y, (data[8] & 0x7f) - 64);
		break;

	case 0x05:
		/* Rotation packet */
		if (features->type >= INTUOS3S) {
			/* I3 marker pen rotation */
			t = (data[6] << 3) | ((data[7] >> 5) & 7);
			t = (data[7] & 0x20) ? ((t > 900) ? ((t-1) / 2 - 1350) :
				((t-1) / 2 + 450)) : (450 - t / 2) ;
			input_report_abs(input, ABS_Z, t);
		} else {
			/* 4D mouse 2nd packet */
			t = (data[6] << 3) | ((data[7] >> 5) & 7);
			input_report_abs(input, ABS_RZ, (data[7] & 0x20) ?
				((t - 1) / 2) : -t / 2);
		}
		break;

	case 0x04:
		/* 4D mouse 1st packet */
		input_report_key(input, BTN_LEFT,   data[8] & 0x01);
		input_report_key(input, BTN_MIDDLE, data[8] & 0x02);
		input_report_key(input, BTN_RIGHT,  data[8] & 0x04);

		input_report_key(input, BTN_SIDE,   data[8] & 0x20);
		input_report_key(input, BTN_EXTRA,  data[8] & 0x10);
		t = (data[6] << 2) | ((data[7] >> 6) & 3);
		input_report_abs(input, ABS_THROTTLE, (data[8] & 0x08) ? -t : t);
		break;

	case 0x06:
		/* I4 mouse */
		input_report_key(input, BTN_LEFT,   data[6] & 0x01);
		input_report_key(input, BTN_MIDDLE, data[6] & 0x02);
		input_report_key(input, BTN_RIGHT,  data[6] & 0x04);
		input_report_rel(input, REL_WHEEL, ((data[7] & 0x80) >> 7)
				 - ((data[7] & 0x40) >> 6));
		input_report_key(input, BTN_SIDE,   data[6] & 0x08);
		input_report_key(input, BTN_EXTRA,  data[6] & 0x10);

		input_report_abs(input, ABS_TILT_X,
			(((data[7] << 1) & 0x7e) | (data[8] >> 7)) - 64);
		input_report_abs(input, ABS_TILT_Y, (data[8] & 0x7f) - 64);
		break;

	case 0x08:
		if (wacom->tool[idx] == BTN_TOOL_MOUSE) {
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
		else if (wacom->tool[idx] == BTN_TOOL_LENS) {
			/* Lens cursor packets */
			input_report_key(input, BTN_LEFT,   data[8] & 0x01);
			input_report_key(input, BTN_MIDDLE, data[8] & 0x02);
			input_report_key(input, BTN_RIGHT,  data[8] & 0x04);
			input_report_key(input, BTN_SIDE,   data[8] & 0x10);
			input_report_key(input, BTN_EXTRA,  data[8] & 0x08);
		}
		break;

	case 0x07:
	case 0x09:
	case 0x0b:
	case 0x0c:
	case 0x0d:
	case 0x0e:
	case 0x0f:
		/* unhandled */
		break;
	}

	input_report_abs(input, ABS_MISC,
			 wacom_intuos_id_mangle(wacom->id[idx])); /* report tool id */
	input_report_key(input, wacom->tool[idx], 1);
	input_event(input, EV_MSC, MSC_SERIAL, wacom->serial[idx]);
	wacom->reporting_data = true;
	return 2;
}

static int wacom_intuos_irq(struct wacom_wac *wacom)
{
	unsigned char *data = wacom->data;
	struct input_dev *input = wacom->pen_input;
	int result;

	if (data[0] != WACOM_REPORT_PENABLED &&
	    data[0] != WACOM_REPORT_INTUOS_ID1 &&
	    data[0] != WACOM_REPORT_INTUOS_ID2 &&
	    data[0] != WACOM_REPORT_INTUOSPAD &&
	    data[0] != WACOM_REPORT_INTUOS_PEN &&
	    data[0] != WACOM_REPORT_CINTIQ &&
	    data[0] != WACOM_REPORT_CINTIQPAD &&
	    data[0] != WACOM_REPORT_INTUOS5PAD) {
		dev_dbg(input->dev.parent,
			"%s: received unknown report #%d\n", __func__, data[0]);
                return 0;
	}

	/* process pad events */
	result = wacom_intuos_pad(wacom);
	if (result)
		return result;

	/* process in/out prox events */
	result = wacom_intuos_inout(wacom);
	if (result)
		return result - 1;

	/* process general packets */
	result = wacom_intuos_general(wacom);
	if (result)
		return result - 1;

	return 0;
}

static int wacom_remote_irq(struct wacom_wac *wacom_wac, size_t len)
{
	unsigned char *data = wacom_wac->data;
	struct input_dev *input;
	struct wacom *wacom = container_of(wacom_wac, struct wacom, wacom_wac);
	struct wacom_remote *remote = wacom->remote;
	int bat_charging, bat_percent, touch_ring_mode;
	__u32 serial;
	int i, index = -1;
	unsigned long flags;

	if (data[0] != WACOM_REPORT_REMOTE) {
		hid_dbg(wacom->hdev, "%s: received unknown report #%d",
			__func__, data[0]);
		return 0;
	}

	serial = data[3] + (data[4] << 8) + (data[5] << 16);
	wacom_wac->id[0] = PAD_DEVICE_ID;

	spin_lock_irqsave(&remote->remote_lock, flags);

	for (i = 0; i < WACOM_MAX_REMOTES; i++) {
		if (remote->remotes[i].serial == serial) {
			index = i;
			break;
		}
	}

	if (index < 0 || !remote->remotes[index].registered)
		goto out;

	input = remote->remotes[index].input;

	input_report_key(input, BTN_0, (data[9] & 0x01));
	input_report_key(input, BTN_1, (data[9] & 0x02));
	input_report_key(input, BTN_2, (data[9] & 0x04));
	input_report_key(input, BTN_3, (data[9] & 0x08));
	input_report_key(input, BTN_4, (data[9] & 0x10));
	input_report_key(input, BTN_5, (data[9] & 0x20));
	input_report_key(input, BTN_6, (data[9] & 0x40));
	input_report_key(input, BTN_7, (data[9] & 0x80));

	input_report_key(input, BTN_8, (data[10] & 0x01));
	input_report_key(input, BTN_9, (data[10] & 0x02));
	input_report_key(input, BTN_A, (data[10] & 0x04));
	input_report_key(input, BTN_B, (data[10] & 0x08));
	input_report_key(input, BTN_C, (data[10] & 0x10));
	input_report_key(input, BTN_X, (data[10] & 0x20));
	input_report_key(input, BTN_Y, (data[10] & 0x40));
	input_report_key(input, BTN_Z, (data[10] & 0x80));

	input_report_key(input, BTN_BASE, (data[11] & 0x01));
	input_report_key(input, BTN_BASE2, (data[11] & 0x02));

	if (data[12] & 0x80)
		input_report_abs(input, ABS_WHEEL, (data[12] & 0x7f) - 1);
	else
		input_report_abs(input, ABS_WHEEL, 0);

	bat_percent = data[7] & 0x7f;
	bat_charging = !!(data[7] & 0x80);

	if (data[9] | data[10] | (data[11] & 0x03) | data[12])
		input_report_abs(input, ABS_MISC, PAD_DEVICE_ID);
	else
		input_report_abs(input, ABS_MISC, 0);

	input_event(input, EV_MSC, MSC_SERIAL, serial);

	input_sync(input);

	/*Which mode select (LED light) is currently on?*/
	touch_ring_mode = (data[11] & 0xC0) >> 6;

	for (i = 0; i < WACOM_MAX_REMOTES; i++) {
		if (remote->remotes[i].serial == serial)
			wacom->led.groups[i].select = touch_ring_mode;
	}

	__wacom_notify_battery(&remote->remotes[index].battery,
				WACOM_POWER_SUPPLY_STATUS_AUTO, bat_percent,
				bat_charging, 1, bat_charging);

out:
	spin_unlock_irqrestore(&remote->remote_lock, flags);
	return 0;
}

static void wacom_remote_status_irq(struct wacom_wac *wacom_wac, size_t len)
{
	struct wacom *wacom = container_of(wacom_wac, struct wacom, wacom_wac);
	unsigned char *data = wacom_wac->data;
	struct wacom_remote *remote = wacom->remote;
	struct wacom_remote_data remote_data;
	unsigned long flags;
	int i, ret;

	if (data[0] != WACOM_REPORT_DEVICE_LIST)
		return;

	memset(&remote_data, 0, sizeof(struct wacom_remote_data));

	for (i = 0; i < WACOM_MAX_REMOTES; i++) {
		int j = i * 6;
		int serial = (data[j+6] << 16) + (data[j+5] << 8) + data[j+4];
		bool connected = data[j+2];

		remote_data.remote[i].serial = serial;
		remote_data.remote[i].connected = connected;
	}

	spin_lock_irqsave(&remote->remote_lock, flags);

	ret = kfifo_in(&remote->remote_fifo, &remote_data, sizeof(remote_data));
	if (ret != sizeof(remote_data)) {
		spin_unlock_irqrestore(&remote->remote_lock, flags);
		hid_err(wacom->hdev, "Can't queue Remote status event.\n");
		return;
	}

	spin_unlock_irqrestore(&remote->remote_lock, flags);

	wacom_schedule_work(wacom_wac, WACOM_WORKER_REMOTE);
}

static int int_dist(int x1, int y1, int x2, int y2)
{
	int x = x2 - x1;
	int y = y2 - y1;

	return int_sqrt(x*x + y*y);
}

static void wacom_intuos_bt_process_data(struct wacom_wac *wacom,
		unsigned char *data)
{
	memcpy(wacom->data, data, 10);
	wacom_intuos_irq(wacom);

	input_sync(wacom->pen_input);
	if (wacom->pad_input)
		input_sync(wacom->pad_input);
}

static int wacom_intuos_bt_irq(struct wacom_wac *wacom, size_t len)
{
	unsigned char data[WACOM_PKGLEN_MAX];
	int i = 1;
	unsigned power_raw, battery_capacity, bat_charging, ps_connected;

	memcpy(data, wacom->data, len);

	switch (data[0]) {
	case 0x04:
		wacom_intuos_bt_process_data(wacom, data + i);
		i += 10;
		/* fall through */
	case 0x03:
		wacom_intuos_bt_process_data(wacom, data + i);
		i += 10;
		wacom_intuos_bt_process_data(wacom, data + i);
		i += 10;
		power_raw = data[i];
		bat_charging = (power_raw & 0x08) ? 1 : 0;
		ps_connected = (power_raw & 0x10) ? 1 : 0;
		battery_capacity = batcap_i4[power_raw & 0x07];
		wacom_notify_battery(wacom, WACOM_POWER_SUPPLY_STATUS_AUTO,
				     battery_capacity, bat_charging,
				     battery_capacity || bat_charging,
				     ps_connected);
		break;
	default:
		dev_dbg(wacom->pen_input->dev.parent,
				"Unknown report: %d,%d size:%zu\n",
				data[0], data[1], len);
		return 0;
	}
	return 0;
}

static int wacom_wac_finger_count_touches(struct wacom_wac *wacom)
{
	struct input_dev *input = wacom->touch_input;
	unsigned touch_max = wacom->features.touch_max;
	int count = 0;
	int i;

	if (!touch_max)
		return 0;

	if (touch_max == 1)
		return test_bit(BTN_TOUCH, input->key) &&
			report_touch_events(wacom);

	for (i = 0; i < input->mt->num_slots; i++) {
		struct input_mt_slot *ps = &input->mt->slots[i];
		int id = input_mt_get_value(ps, ABS_MT_TRACKING_ID);
		if (id >= 0)
			count++;
	}

	return count;
}

static void wacom_intuos_pro2_bt_pen(struct wacom_wac *wacom)
{
	int pen_frame_len, pen_frames;

	struct input_dev *pen_input = wacom->pen_input;
	unsigned char *data = wacom->data;
	int i;

	if (wacom->features.type == INTUOSP2_BT) {
		wacom->serial[0] = get_unaligned_le64(&data[99]);
		wacom->id[0]     = get_unaligned_le16(&data[107]);
		pen_frame_len = 14;
		pen_frames = 7;
	} else {
		wacom->serial[0] = get_unaligned_le64(&data[33]);
		wacom->id[0]     = get_unaligned_le16(&data[41]);
		pen_frame_len = 8;
		pen_frames = 4;
	}

	if (wacom->serial[0] >> 52 == 1) {
		/* Add back in missing bits of ID for non-USI pens */
		wacom->id[0] |= (wacom->serial[0] >> 32) & 0xFFFFF;
	}

	for (i = 0; i < pen_frames; i++) {
		unsigned char *frame = &data[i*pen_frame_len + 1];
		bool valid = frame[0] & 0x80;
		bool prox = frame[0] & 0x40;
		bool range = frame[0] & 0x20;
		bool invert = frame[0] & 0x10;

		if (!valid)
			continue;

		if (!prox) {
			wacom->shared->stylus_in_proximity = false;
			wacom_exit_report(wacom);
			input_sync(pen_input);

			wacom->tool[0] = 0;
			wacom->id[0] = 0;
			wacom->serial[0] = 0;
			return;
		}

		if (range) {
			if (!wacom->tool[0]) { /* first in range */
				/* Going into range select tool */
				if (invert)
					wacom->tool[0] = BTN_TOOL_RUBBER;
				else if (wacom->id[0])
					wacom->tool[0] = wacom_intuos_get_tool_type(wacom->id[0]);
				else
					wacom->tool[0] = BTN_TOOL_PEN;
			}

			input_report_abs(pen_input, ABS_X, get_unaligned_le16(&frame[1]));
			input_report_abs(pen_input, ABS_Y, get_unaligned_le16(&frame[3]));

			if (wacom->features.type == INTUOSP2_BT) {
				/* Fix rotation alignment: userspace expects zero at left */
				int16_t rotation =
					(int16_t)get_unaligned_le16(&frame[9]);
				rotation += 1800/4;

				if (rotation > 899)
					rotation -= 1800;

				input_report_abs(pen_input, ABS_TILT_X,
						 (char)frame[7]);
				input_report_abs(pen_input, ABS_TILT_Y,
						 (char)frame[8]);
				input_report_abs(pen_input, ABS_Z, rotation);
				input_report_abs(pen_input, ABS_WHEEL,
						 get_unaligned_le16(&frame[11]));
			}
		}

		if (wacom->tool[0]) {
			input_report_abs(pen_input, ABS_PRESSURE, get_unaligned_le16(&frame[5]));
			if (wacom->features.type == INTUOSP2_BT) {
				input_report_abs(pen_input, ABS_DISTANCE,
						 range ? frame[13] : wacom->features.distance_max);
			} else {
				input_report_abs(pen_input, ABS_DISTANCE,
						 range ? frame[7] : wacom->features.distance_max);
			}

			input_report_key(pen_input, BTN_TOUCH, frame[0] & 0x09);
			input_report_key(pen_input, BTN_STYLUS, frame[0] & 0x02);
			input_report_key(pen_input, BTN_STYLUS2, frame[0] & 0x04);

			input_report_key(pen_input, wacom->tool[0], prox);
			input_event(pen_input, EV_MSC, MSC_SERIAL, wacom->serial[0]);
			input_report_abs(pen_input, ABS_MISC,
					 wacom_intuos_id_mangle(wacom->id[0])); /* report tool id */
		}

		wacom->shared->stylus_in_proximity = prox;

		input_sync(pen_input);
	}
}

static void wacom_intuos_pro2_bt_touch(struct wacom_wac *wacom)
{
	const int finger_touch_len = 8;
	const int finger_frames = 4;
	const int finger_frame_len = 43;

	struct input_dev *touch_input = wacom->touch_input;
	unsigned char *data = wacom->data;
	int num_contacts_left = 5;
	int i, j;

	for (i = 0; i < finger_frames; i++) {
		unsigned char *frame = &data[i*finger_frame_len + 109];
		int current_num_contacts = frame[0] & 0x7F;
		int contacts_to_send;

		if (!(frame[0] & 0x80))
			continue;

		/*
		 * First packet resets the counter since only the first
		 * packet in series will have non-zero current_num_contacts.
		 */
		if (current_num_contacts)
			wacom->num_contacts_left = current_num_contacts;

		contacts_to_send = min(num_contacts_left, wacom->num_contacts_left);

		for (j = 0; j < contacts_to_send; j++) {
			unsigned char *touch = &frame[j*finger_touch_len + 1];
			int slot = input_mt_get_slot_by_key(touch_input, touch[0]);
			int x = get_unaligned_le16(&touch[2]);
			int y = get_unaligned_le16(&touch[4]);
			int w = touch[6] * input_abs_get_res(touch_input, ABS_MT_POSITION_X);
			int h = touch[7] * input_abs_get_res(touch_input, ABS_MT_POSITION_Y);

			if (slot < 0)
				continue;

			input_mt_slot(touch_input, slot);
			input_mt_report_slot_state(touch_input, MT_TOOL_FINGER, touch[1] & 0x01);
			input_report_abs(touch_input, ABS_MT_POSITION_X, x);
			input_report_abs(touch_input, ABS_MT_POSITION_Y, y);
			input_report_abs(touch_input, ABS_MT_TOUCH_MAJOR, max(w, h));
			input_report_abs(touch_input, ABS_MT_TOUCH_MINOR, min(w, h));
			input_report_abs(touch_input, ABS_MT_ORIENTATION, w > h);
		}

		input_mt_sync_frame(touch_input);

		wacom->num_contacts_left -= contacts_to_send;
		if (wacom->num_contacts_left <= 0) {
			wacom->num_contacts_left = 0;
			wacom->shared->touch_down = wacom_wac_finger_count_touches(wacom);
			input_sync(touch_input);
		}
	}

	if (wacom->num_contacts_left == 0) {
		// Be careful that we don't accidentally call input_sync with
		// only a partial set of fingers of processed
		input_report_switch(touch_input, SW_MUTE_DEVICE, !(data[281] >> 7));
		input_sync(touch_input);
	}

}

static void wacom_intuos_pro2_bt_pad(struct wacom_wac *wacom)
{
	struct input_dev *pad_input = wacom->pad_input;
	unsigned char *data = wacom->data;

	int buttons = data[282] | ((data[281] & 0x40) << 2);
	int ring = data[285] & 0x7F;
	bool ringstatus = data[285] & 0x80;
	bool prox = buttons || ringstatus;

	/* Fix touchring data: userspace expects 0 at left and increasing clockwise */
	ring = 71 - ring;
	ring += 3*72/16;
	if (ring > 71)
		ring -= 72;

	wacom_report_numbered_buttons(pad_input, 9, buttons);

	input_report_abs(pad_input, ABS_WHEEL, ringstatus ? ring : 0);

	input_report_key(pad_input, wacom->tool[1], prox ? 1 : 0);
	input_report_abs(pad_input, ABS_MISC, prox ? PAD_DEVICE_ID : 0);
	input_event(pad_input, EV_MSC, MSC_SERIAL, 0xffffffff);

	input_sync(pad_input);
}

static void wacom_intuos_pro2_bt_battery(struct wacom_wac *wacom)
{
	unsigned char *data = wacom->data;

	bool chg = data[284] & 0x80;
	int battery_status = data[284] & 0x7F;

	wacom_notify_battery(wacom, WACOM_POWER_SUPPLY_STATUS_AUTO,
			     battery_status, chg, 1, chg);
}

static void wacom_intuos_gen3_bt_pad(struct wacom_wac *wacom)
{
	struct input_dev *pad_input = wacom->pad_input;
	unsigned char *data = wacom->data;

	int buttons = data[44];

	wacom_report_numbered_buttons(pad_input, 4, buttons);

	input_report_key(pad_input, wacom->tool[1], buttons ? 1 : 0);
	input_report_abs(pad_input, ABS_MISC, buttons ? PAD_DEVICE_ID : 0);
	input_event(pad_input, EV_MSC, MSC_SERIAL, 0xffffffff);

	input_sync(pad_input);
}

static void wacom_intuos_gen3_bt_battery(struct wacom_wac *wacom)
{
	unsigned char *data = wacom->data;

	bool chg = data[45] & 0x80;
	int battery_status = data[45] & 0x7F;

	wacom_notify_battery(wacom, WACOM_POWER_SUPPLY_STATUS_AUTO,
			     battery_status, chg, 1, chg);
}

static int wacom_intuos_pro2_bt_irq(struct wacom_wac *wacom, size_t len)
{
	unsigned char *data = wacom->data;

	if (data[0] != 0x80 && data[0] != 0x81) {
		dev_dbg(wacom->pen_input->dev.parent,
			"%s: received unknown report #%d\n", __func__, data[0]);
		return 0;
	}

	wacom_intuos_pro2_bt_pen(wacom);
	if (wacom->features.type == INTUOSP2_BT) {
		wacom_intuos_pro2_bt_touch(wacom);
		wacom_intuos_pro2_bt_pad(wacom);
		wacom_intuos_pro2_bt_battery(wacom);
	} else {
		wacom_intuos_gen3_bt_pad(wacom);
		wacom_intuos_gen3_bt_battery(wacom);
	}
	return 0;
}

static int wacom_24hdt_irq(struct wacom_wac *wacom)
{
	struct input_dev *input = wacom->touch_input;
	unsigned char *data = wacom->data;
	int i;
	int current_num_contacts = data[61];
	int contacts_to_send = 0;
	int num_contacts_left = 4; /* maximum contacts per packet */
	int byte_per_packet = WACOM_BYTES_PER_24HDT_PACKET;
	int y_offset = 2;

	if (wacom->features.type == WACOM_27QHDT) {
		current_num_contacts = data[63];
		num_contacts_left = 10;
		byte_per_packet = WACOM_BYTES_PER_QHDTHID_PACKET;
		y_offset = 0;
	}

	/*
	 * First packet resets the counter since only the first
	 * packet in series will have non-zero current_num_contacts.
	 */
	if (current_num_contacts)
		wacom->num_contacts_left = current_num_contacts;

	contacts_to_send = min(num_contacts_left, wacom->num_contacts_left);

	for (i = 0; i < contacts_to_send; i++) {
		int offset = (byte_per_packet * i) + 1;
		bool touch = (data[offset] & 0x1) && report_touch_events(wacom);
		int slot = input_mt_get_slot_by_key(input, data[offset + 1]);

		if (slot < 0)
			continue;
		input_mt_slot(input, slot);
		input_mt_report_slot_state(input, MT_TOOL_FINGER, touch);

		if (touch) {
			int t_x = get_unaligned_le16(&data[offset + 2]);
			int t_y = get_unaligned_le16(&data[offset + 4 + y_offset]);

			input_report_abs(input, ABS_MT_POSITION_X, t_x);
			input_report_abs(input, ABS_MT_POSITION_Y, t_y);

			if (wacom->features.type != WACOM_27QHDT) {
				int c_x = get_unaligned_le16(&data[offset + 4]);
				int c_y = get_unaligned_le16(&data[offset + 8]);
				int w = get_unaligned_le16(&data[offset + 10]);
				int h = get_unaligned_le16(&data[offset + 12]);

				input_report_abs(input, ABS_MT_TOUCH_MAJOR, min(w,h));
				input_report_abs(input, ABS_MT_WIDTH_MAJOR,
						 min(w, h) + int_dist(t_x, t_y, c_x, c_y));
				input_report_abs(input, ABS_MT_WIDTH_MINOR, min(w, h));
				input_report_abs(input, ABS_MT_ORIENTATION, w > h);
			}
		}
	}
	input_mt_sync_frame(input);

	wacom->num_contacts_left -= contacts_to_send;
	if (wacom->num_contacts_left <= 0) {
		wacom->num_contacts_left = 0;
		wacom->shared->touch_down = wacom_wac_finger_count_touches(wacom);
	}
	return 1;
}

static int wacom_mt_touch(struct wacom_wac *wacom)
{
	struct input_dev *input = wacom->touch_input;
	unsigned char *data = wacom->data;
	int i;
	int current_num_contacts = data[2];
	int contacts_to_send = 0;
	int x_offset = 0;

	/* MTTPC does not support Height and Width */
	if (wacom->features.type == MTTPC || wacom->features.type == MTTPC_B)
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
		bool touch = (data[offset] & 0x1) && report_touch_events(wacom);
		int id = get_unaligned_le16(&data[offset + 1]);
		int slot = input_mt_get_slot_by_key(input, id);

		if (slot < 0)
			continue;

		input_mt_slot(input, slot);
		input_mt_report_slot_state(input, MT_TOOL_FINGER, touch);
		if (touch) {
			int x = get_unaligned_le16(&data[offset + x_offset + 7]);
			int y = get_unaligned_le16(&data[offset + x_offset + 9]);
			input_report_abs(input, ABS_MT_POSITION_X, x);
			input_report_abs(input, ABS_MT_POSITION_Y, y);
		}
	}
	input_mt_sync_frame(input);

	wacom->num_contacts_left -= contacts_to_send;
	if (wacom->num_contacts_left <= 0) {
		wacom->num_contacts_left = 0;
		wacom->shared->touch_down = wacom_wac_finger_count_touches(wacom);
	}
	return 1;
}

static int wacom_tpc_mt_touch(struct wacom_wac *wacom)
{
	struct input_dev *input = wacom->touch_input;
	unsigned char *data = wacom->data;
	int i;

	for (i = 0; i < 2; i++) {
		int p = data[1] & (1 << i);
		bool touch = p && report_touch_events(wacom);

		input_mt_slot(input, i);
		input_mt_report_slot_state(input, MT_TOOL_FINGER, touch);
		if (touch) {
			int x = le16_to_cpup((__le16 *)&data[i * 2 + 2]) & 0x7fff;
			int y = le16_to_cpup((__le16 *)&data[i * 2 + 6]) & 0x7fff;

			input_report_abs(input, ABS_MT_POSITION_X, x);
			input_report_abs(input, ABS_MT_POSITION_Y, y);
		}
	}
	input_mt_sync_frame(input);

	/* keep touch state for pen event */
	wacom->shared->touch_down = wacom_wac_finger_count_touches(wacom);

	return 1;
}

static int wacom_tpc_single_touch(struct wacom_wac *wacom, size_t len)
{
	unsigned char *data = wacom->data;
	struct input_dev *input = wacom->touch_input;
	bool prox = report_touch_events(wacom);
	int x = 0, y = 0;

	if (wacom->features.touch_max > 1 || len > WACOM_PKGLEN_TPC2FG)
		return 0;

	if (len == WACOM_PKGLEN_TPC1FG) {
		prox = prox && (data[0] & 0x01);
		x = get_unaligned_le16(&data[1]);
		y = get_unaligned_le16(&data[3]);
	} else if (len == WACOM_PKGLEN_TPC1FG_B) {
		prox = prox && (data[2] & 0x01);
		x = get_unaligned_le16(&data[3]);
		y = get_unaligned_le16(&data[5]);
	} else {
		prox = prox && (data[1] & 0x01);
		x = le16_to_cpup((__le16 *)&data[2]);
		y = le16_to_cpup((__le16 *)&data[4]);
	}

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
	unsigned char *data = wacom->data;
	struct input_dev *input = wacom->pen_input;
	bool prox = data[1] & 0x20;

	if (!wacom->shared->stylus_in_proximity) /* first in prox */
		/* Going into proximity select tool */
		wacom->tool[0] = (data[1] & 0x0c) ? BTN_TOOL_RUBBER : BTN_TOOL_PEN;

	/* keep pen state for touch events */
	wacom->shared->stylus_in_proximity = prox;

	/* send pen events only when touch is up or forced out
	 * or touch arbitration is off
	 */
	if (!delay_pen_events(wacom)) {
		input_report_key(input, BTN_STYLUS, data[1] & 0x02);
		input_report_key(input, BTN_STYLUS2, data[1] & 0x10);
		input_report_abs(input, ABS_X, le16_to_cpup((__le16 *)&data[2]));
		input_report_abs(input, ABS_Y, le16_to_cpup((__le16 *)&data[4]));
		input_report_abs(input, ABS_PRESSURE, ((data[7] & 0x07) << 8) | data[6]);
		input_report_key(input, BTN_TOUCH, data[1] & 0x05);
		input_report_key(input, wacom->tool[0], prox);
		return 1;
	}

	return 0;
}

static int wacom_tpc_irq(struct wacom_wac *wacom, size_t len)
{
	unsigned char *data = wacom->data;

	if (wacom->pen_input) {
		dev_dbg(wacom->pen_input->dev.parent,
			"%s: received report #%d\n", __func__, data[0]);

		if (len == WACOM_PKGLEN_PENABLED ||
		    data[0] == WACOM_REPORT_PENABLED)
			return wacom_tpc_pen(wacom);
	}
	else if (wacom->touch_input) {
		dev_dbg(wacom->touch_input->dev.parent,
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
			case WACOM_REPORT_TPCMT2:
				return wacom_mt_touch(wacom);

			}
		}
	}

	return 0;
}

static int wacom_offset_rotation(struct input_dev *input, struct hid_usage *usage,
				 int value, int num, int denom)
{
	struct input_absinfo *abs = &input->absinfo[usage->code];
	int range = (abs->maximum - abs->minimum + 1);

	value += num*range/denom;
	if (value > abs->maximum)
		value -= range;
	else if (value < abs->minimum)
		value += range;
	return value;
}

int wacom_equivalent_usage(int usage)
{
	if ((usage & HID_USAGE_PAGE) == WACOM_HID_UP_WACOMDIGITIZER) {
		int subpage = (usage & 0xFF00) << 8;
		int subusage = (usage & 0xFF);

		if (subpage == WACOM_HID_SP_PAD ||
		    subpage == WACOM_HID_SP_BUTTON ||
		    subpage == WACOM_HID_SP_DIGITIZER ||
		    subpage == WACOM_HID_SP_DIGITIZERINFO ||
		    usage == WACOM_HID_WD_SENSE ||
		    usage == WACOM_HID_WD_SERIALHI ||
		    usage == WACOM_HID_WD_TOOLTYPE ||
		    usage == WACOM_HID_WD_DISTANCE ||
		    usage == WACOM_HID_WD_TOUCHSTRIP ||
		    usage == WACOM_HID_WD_TOUCHSTRIP2 ||
		    usage == WACOM_HID_WD_TOUCHRING ||
		    usage == WACOM_HID_WD_TOUCHRINGSTATUS ||
		    usage == WACOM_HID_WD_REPORT_VALID) {
			return usage;
		}

		if (subpage == HID_UP_UNDEFINED)
			subpage = HID_UP_DIGITIZER;

		return subpage | subusage;
	}

	if ((usage & HID_USAGE_PAGE) == WACOM_HID_UP_WACOMTOUCH) {
		int subpage = (usage & 0xFF00) << 8;
		int subusage = (usage & 0xFF);

		if (subpage == HID_UP_UNDEFINED)
			subpage = WACOM_HID_SP_DIGITIZER;

		return subpage | subusage;
	}

	return usage;
}

static void wacom_map_usage(struct input_dev *input, struct hid_usage *usage,
		struct hid_field *field, __u8 type, __u16 code, int fuzz)
{
	struct wacom *wacom = input_get_drvdata(input);
	struct wacom_wac *wacom_wac = &wacom->wacom_wac;
	struct wacom_features *features = &wacom_wac->features;
	int fmin = field->logical_minimum;
	int fmax = field->logical_maximum;
	unsigned int equivalent_usage = wacom_equivalent_usage(usage->hid);
	int resolution_code = code;

	if (equivalent_usage == HID_DG_TWIST) {
		resolution_code = ABS_RZ;
	}

	if (equivalent_usage == HID_GD_X) {
		fmin += features->offset_left;
		fmax -= features->offset_right;
	}
	if (equivalent_usage == HID_GD_Y) {
		fmin += features->offset_top;
		fmax -= features->offset_bottom;
	}

	usage->type = type;
	usage->code = code;

	set_bit(type, input->evbit);

	switch (type) {
	case EV_ABS:
		input_set_abs_params(input, code, fmin, fmax, fuzz, 0);
		input_abs_set_res(input, code,
				  hidinput_calc_abs_res(field, resolution_code));
		break;
	case EV_KEY:
		input_set_capability(input, EV_KEY, code);
		break;
	case EV_MSC:
		input_set_capability(input, EV_MSC, code);
		break;
	case EV_SW:
		input_set_capability(input, EV_SW, code);
		break;
	}
}

static void wacom_wac_battery_usage_mapping(struct hid_device *hdev,
		struct hid_field *field, struct hid_usage *usage)
{
	struct wacom *wacom = hid_get_drvdata(hdev);
	struct wacom_wac *wacom_wac = &wacom->wacom_wac;
	struct wacom_features *features = &wacom_wac->features;
	unsigned equivalent_usage = wacom_equivalent_usage(usage->hid);

	switch (equivalent_usage) {
	case HID_DG_BATTERYSTRENGTH:
	case WACOM_HID_WD_BATTERY_LEVEL:
	case WACOM_HID_WD_BATTERY_CHARGING:
		features->quirks |= WACOM_QUIRK_BATTERY;
		break;
	}
}

static void wacom_wac_battery_event(struct hid_device *hdev, struct hid_field *field,
		struct hid_usage *usage, __s32 value)
{
	struct wacom *wacom = hid_get_drvdata(hdev);
	struct wacom_wac *wacom_wac = &wacom->wacom_wac;
	unsigned equivalent_usage = wacom_equivalent_usage(usage->hid);

	switch (equivalent_usage) {
	case HID_DG_BATTERYSTRENGTH:
		if (value == 0) {
			wacom_wac->hid_data.bat_status = POWER_SUPPLY_STATUS_UNKNOWN;
		}
		else {
			value = value * 100 / (field->logical_maximum - field->logical_minimum);
			wacom_wac->hid_data.battery_capacity = value;
			wacom_wac->hid_data.bat_connected = 1;
			wacom_wac->hid_data.bat_status = WACOM_POWER_SUPPLY_STATUS_AUTO;
		}
		break;
	case WACOM_HID_WD_BATTERY_LEVEL:
		value = value * 100 / (field->logical_maximum - field->logical_minimum);
		wacom_wac->hid_data.battery_capacity = value;
		wacom_wac->hid_data.bat_connected = 1;
		wacom_wac->hid_data.bat_status = WACOM_POWER_SUPPLY_STATUS_AUTO;
		break;
	case WACOM_HID_WD_BATTERY_CHARGING:
		wacom_wac->hid_data.bat_charging = value;
		wacom_wac->hid_data.ps_connected = value;
		wacom_wac->hid_data.bat_connected = 1;
		wacom_wac->hid_data.bat_status = WACOM_POWER_SUPPLY_STATUS_AUTO;
		break;
	}
}

static void wacom_wac_battery_pre_report(struct hid_device *hdev,
		struct hid_report *report)
{
	return;
}

static void wacom_wac_battery_report(struct hid_device *hdev,
		struct hid_report *report)
{
	struct wacom *wacom = hid_get_drvdata(hdev);
	struct wacom_wac *wacom_wac = &wacom->wacom_wac;
	struct wacom_features *features = &wacom_wac->features;

	if (features->quirks & WACOM_QUIRK_BATTERY) {
		int status = wacom_wac->hid_data.bat_status;
		int capacity = wacom_wac->hid_data.battery_capacity;
		bool charging = wacom_wac->hid_data.bat_charging;
		bool connected = wacom_wac->hid_data.bat_connected;
		bool powered = wacom_wac->hid_data.ps_connected;

		wacom_notify_battery(wacom_wac, status, capacity, charging,
				     connected, powered);
	}
}

static void wacom_wac_pad_usage_mapping(struct hid_device *hdev,
		struct hid_field *field, struct hid_usage *usage)
{
	struct wacom *wacom = hid_get_drvdata(hdev);
	struct wacom_wac *wacom_wac = &wacom->wacom_wac;
	struct wacom_features *features = &wacom_wac->features;
	struct input_dev *input = wacom_wac->pad_input;
	unsigned equivalent_usage = wacom_equivalent_usage(usage->hid);

	switch (equivalent_usage) {
	case WACOM_HID_WD_ACCELEROMETER_X:
		__set_bit(INPUT_PROP_ACCELEROMETER, input->propbit);
		wacom_map_usage(input, usage, field, EV_ABS, ABS_X, 0);
		features->device_type |= WACOM_DEVICETYPE_PAD;
		break;
	case WACOM_HID_WD_ACCELEROMETER_Y:
		__set_bit(INPUT_PROP_ACCELEROMETER, input->propbit);
		wacom_map_usage(input, usage, field, EV_ABS, ABS_Y, 0);
		features->device_type |= WACOM_DEVICETYPE_PAD;
		break;
	case WACOM_HID_WD_ACCELEROMETER_Z:
		__set_bit(INPUT_PROP_ACCELEROMETER, input->propbit);
		wacom_map_usage(input, usage, field, EV_ABS, ABS_Z, 0);
		features->device_type |= WACOM_DEVICETYPE_PAD;
		break;
	case WACOM_HID_WD_BUTTONCENTER:
	case WACOM_HID_WD_BUTTONHOME:
	case WACOM_HID_WD_BUTTONUP:
	case WACOM_HID_WD_BUTTONDOWN:
	case WACOM_HID_WD_BUTTONLEFT:
	case WACOM_HID_WD_BUTTONRIGHT:
		wacom_map_usage(input, usage, field, EV_KEY,
				wacom_numbered_button_to_key(features->numbered_buttons),
				0);
		features->numbered_buttons++;
		features->device_type |= WACOM_DEVICETYPE_PAD;
		break;
	case WACOM_HID_WD_TOUCHONOFF:
	case WACOM_HID_WD_MUTE_DEVICE:
		/*
		 * This usage, which is used to mute touch events, comes
		 * from the pad packet, but is reported on the touch
		 * interface. Because the touch interface may not have
		 * been created yet, we cannot call wacom_map_usage(). In
		 * order to process this usage when we receive it, we set
		 * the usage type and code directly.
		 */
		wacom_wac->has_mute_touch_switch = true;
		usage->type = EV_SW;
		usage->code = SW_MUTE_DEVICE;
		features->device_type |= WACOM_DEVICETYPE_PAD;
		break;
	case WACOM_HID_WD_TOUCHSTRIP:
		wacom_map_usage(input, usage, field, EV_ABS, ABS_RX, 0);
		features->device_type |= WACOM_DEVICETYPE_PAD;
		break;
	case WACOM_HID_WD_TOUCHSTRIP2:
		wacom_map_usage(input, usage, field, EV_ABS, ABS_RY, 0);
		features->device_type |= WACOM_DEVICETYPE_PAD;
		break;
	case WACOM_HID_WD_TOUCHRING:
		wacom_map_usage(input, usage, field, EV_ABS, ABS_WHEEL, 0);
		features->device_type |= WACOM_DEVICETYPE_PAD;
		break;
	case WACOM_HID_WD_TOUCHRINGSTATUS:
		/*
		 * Only set up type/code association. Completely mapping
		 * this usage may overwrite the axis resolution and range.
		 */
		usage->type = EV_ABS;
		usage->code = ABS_WHEEL;
		set_bit(EV_ABS, input->evbit);
		features->device_type |= WACOM_DEVICETYPE_PAD;
		break;
	case WACOM_HID_WD_BUTTONCONFIG:
		wacom_map_usage(input, usage, field, EV_KEY, KEY_BUTTONCONFIG, 0);
		features->device_type |= WACOM_DEVICETYPE_PAD;
		break;
	case WACOM_HID_WD_ONSCREEN_KEYBOARD:
		wacom_map_usage(input, usage, field, EV_KEY, KEY_ONSCREEN_KEYBOARD, 0);
		features->device_type |= WACOM_DEVICETYPE_PAD;
		break;
	case WACOM_HID_WD_CONTROLPANEL:
		wacom_map_usage(input, usage, field, EV_KEY, KEY_CONTROLPANEL, 0);
		features->device_type |= WACOM_DEVICETYPE_PAD;
		break;
	case WACOM_HID_WD_MODE_CHANGE:
		/* do not overwrite previous data */
		if (!wacom_wac->has_mode_change) {
			wacom_wac->has_mode_change = true;
			wacom_wac->is_direct_mode = true;
		}
		features->device_type |= WACOM_DEVICETYPE_PAD;
		break;
	}

	switch (equivalent_usage & 0xfffffff0) {
	case WACOM_HID_WD_EXPRESSKEY00:
		wacom_map_usage(input, usage, field, EV_KEY,
				wacom_numbered_button_to_key(features->numbered_buttons),
				0);
		features->numbered_buttons++;
		features->device_type |= WACOM_DEVICETYPE_PAD;
		break;
	}
}

static void wacom_wac_pad_event(struct hid_device *hdev, struct hid_field *field,
		struct hid_usage *usage, __s32 value)
{
	struct wacom *wacom = hid_get_drvdata(hdev);
	struct wacom_wac *wacom_wac = &wacom->wacom_wac;
	struct input_dev *input = wacom_wac->pad_input;
	struct wacom_features *features = &wacom_wac->features;
	unsigned equivalent_usage = wacom_equivalent_usage(usage->hid);
	int i;
	bool do_report = false;

	/*
	 * Avoid reporting this event and setting inrange_state if this usage
	 * hasn't been mapped.
	 */
	if (!usage->type && equivalent_usage != WACOM_HID_WD_MODE_CHANGE)
		return;

	if (wacom_equivalent_usage(field->physical) == HID_DG_TABLETFUNCTIONKEY) {
		if (usage->hid != WACOM_HID_WD_TOUCHRING)
			wacom_wac->hid_data.inrange_state |= value;
	}

	switch (equivalent_usage) {
	case WACOM_HID_WD_TOUCHRING:
		/*
		 * Userspace expects touchrings to increase in value with
		 * clockwise gestures and have their zero point at the
		 * tablet's left. HID events "should" be clockwise-
		 * increasing and zero at top, though the MobileStudio
		 * Pro and 2nd-gen Intuos Pro don't do this...
		 */
		if (hdev->vendor == 0x56a &&
		    (hdev->product == 0x34d || hdev->product == 0x34e ||  /* MobileStudio Pro */
		     hdev->product == 0x357 || hdev->product == 0x358)) { /* Intuos Pro 2 */
			value = (field->logical_maximum - value);

			if (hdev->product == 0x357 || hdev->product == 0x358)
				value = wacom_offset_rotation(input, usage, value, 3, 16);
			else if (hdev->product == 0x34d || hdev->product == 0x34e)
				value = wacom_offset_rotation(input, usage, value, 1, 2);
		}
		else {
			value = wacom_offset_rotation(input, usage, value, 1, 4);
		}
		do_report = true;
		break;
	case WACOM_HID_WD_TOUCHRINGSTATUS:
		if (!value)
			input_event(input, usage->type, usage->code, 0);
		break;

	case WACOM_HID_WD_MUTE_DEVICE:
	case WACOM_HID_WD_TOUCHONOFF:
		if (wacom_wac->shared->touch_input) {
			bool *is_touch_on = &wacom_wac->shared->is_touch_on;

			if (equivalent_usage == WACOM_HID_WD_MUTE_DEVICE && value)
				*is_touch_on = !(*is_touch_on);
			else if (equivalent_usage == WACOM_HID_WD_TOUCHONOFF)
				*is_touch_on = value;

			input_report_switch(wacom_wac->shared->touch_input,
					    SW_MUTE_DEVICE, !(*is_touch_on));
			input_sync(wacom_wac->shared->touch_input);
		}
		break;

	case WACOM_HID_WD_MODE_CHANGE:
		if (wacom_wac->is_direct_mode != value) {
			wacom_wac->is_direct_mode = value;
			wacom_schedule_work(&wacom->wacom_wac, WACOM_WORKER_MODE_CHANGE);
		}
		break;

	case WACOM_HID_WD_BUTTONCENTER:
		for (i = 0; i < wacom->led.count; i++)
			wacom_update_led(wacom, features->numbered_buttons,
					 value, i);
		 /* fall through*/
	default:
		do_report = true;
		break;
	}

	if (do_report) {
		input_event(input, usage->type, usage->code, value);
		if (value)
			wacom_wac->hid_data.pad_input_event_flag = true;
	}
}

static void wacom_wac_pad_pre_report(struct hid_device *hdev,
		struct hid_report *report)
{
	struct wacom *wacom = hid_get_drvdata(hdev);
	struct wacom_wac *wacom_wac = &wacom->wacom_wac;

	wacom_wac->hid_data.inrange_state = 0;
}

static void wacom_wac_pad_report(struct hid_device *hdev,
		struct hid_report *report, struct hid_field *field)
{
	struct wacom *wacom = hid_get_drvdata(hdev);
	struct wacom_wac *wacom_wac = &wacom->wacom_wac;
	struct input_dev *input = wacom_wac->pad_input;
	bool active = wacom_wac->hid_data.inrange_state != 0;

	/* report prox for expresskey events */
	if (wacom_wac->hid_data.pad_input_event_flag) {
		input_event(input, EV_ABS, ABS_MISC, active ? PAD_DEVICE_ID : 0);
		input_sync(input);
		if (!active)
			wacom_wac->hid_data.pad_input_event_flag = false;
	}
}

static void wacom_wac_pen_usage_mapping(struct hid_device *hdev,
		struct hid_field *field, struct hid_usage *usage)
{
	struct wacom *wacom = hid_get_drvdata(hdev);
	struct wacom_wac *wacom_wac = &wacom->wacom_wac;
	struct wacom_features *features = &wacom_wac->features;
	struct input_dev *input = wacom_wac->pen_input;
	unsigned equivalent_usage = wacom_equivalent_usage(usage->hid);

	switch (equivalent_usage) {
	case HID_GD_X:
		wacom_map_usage(input, usage, field, EV_ABS, ABS_X, 4);
		break;
	case HID_GD_Y:
		wacom_map_usage(input, usage, field, EV_ABS, ABS_Y, 4);
		break;
	case WACOM_HID_WD_DISTANCE:
	case HID_GD_Z:
		wacom_map_usage(input, usage, field, EV_ABS, ABS_DISTANCE, 0);
		break;
	case HID_DG_TIPPRESSURE:
		wacom_map_usage(input, usage, field, EV_ABS, ABS_PRESSURE, 0);
		break;
	case HID_DG_INRANGE:
		wacom_map_usage(input, usage, field, EV_KEY, BTN_TOOL_PEN, 0);
		break;
	case HID_DG_INVERT:
		wacom_map_usage(input, usage, field, EV_KEY,
				BTN_TOOL_RUBBER, 0);
		break;
	case HID_DG_TILT_X:
		wacom_map_usage(input, usage, field, EV_ABS, ABS_TILT_X, 0);
		break;
	case HID_DG_TILT_Y:
		wacom_map_usage(input, usage, field, EV_ABS, ABS_TILT_Y, 0);
		break;
	case HID_DG_TWIST:
		wacom_map_usage(input, usage, field, EV_ABS, ABS_Z, 0);
		break;
	case HID_DG_ERASER:
	case HID_DG_TIPSWITCH:
		wacom_map_usage(input, usage, field, EV_KEY, BTN_TOUCH, 0);
		break;
	case HID_DG_BARRELSWITCH:
		wacom_map_usage(input, usage, field, EV_KEY, BTN_STYLUS, 0);
		break;
	case HID_DG_BARRELSWITCH2:
		wacom_map_usage(input, usage, field, EV_KEY, BTN_STYLUS2, 0);
		break;
	case HID_DG_TOOLSERIALNUMBER:
		features->quirks |= WACOM_QUIRK_TOOLSERIAL;
		wacom_map_usage(input, usage, field, EV_MSC, MSC_SERIAL, 0);
		break;
	case WACOM_HID_WD_SENSE:
		features->quirks |= WACOM_QUIRK_SENSE;
		wacom_map_usage(input, usage, field, EV_KEY, BTN_TOOL_PEN, 0);
		break;
	case WACOM_HID_WD_SERIALHI:
		wacom_map_usage(input, usage, field, EV_ABS, ABS_MISC, 0);

		if (!(features->quirks & WACOM_QUIRK_AESPEN)) {
			set_bit(EV_KEY, input->evbit);
			input_set_capability(input, EV_KEY, BTN_TOOL_PEN);
			input_set_capability(input, EV_KEY, BTN_TOOL_RUBBER);
			input_set_capability(input, EV_KEY, BTN_TOOL_BRUSH);
			input_set_capability(input, EV_KEY, BTN_TOOL_PENCIL);
			input_set_capability(input, EV_KEY, BTN_TOOL_AIRBRUSH);
			if (!(features->device_type & WACOM_DEVICETYPE_DIRECT)) {
				input_set_capability(input, EV_KEY, BTN_TOOL_MOUSE);
				input_set_capability(input, EV_KEY, BTN_TOOL_LENS);
			}
		}
		break;
	case WACOM_HID_WD_FINGERWHEEL:
		wacom_map_usage(input, usage, field, EV_ABS, ABS_WHEEL, 0);
		break;
	}
}

static void wacom_wac_pen_event(struct hid_device *hdev, struct hid_field *field,
		struct hid_usage *usage, __s32 value)
{
	struct wacom *wacom = hid_get_drvdata(hdev);
	struct wacom_wac *wacom_wac = &wacom->wacom_wac;
	struct wacom_features *features = &wacom_wac->features;
	struct input_dev *input = wacom_wac->pen_input;
	unsigned equivalent_usage = wacom_equivalent_usage(usage->hid);

	if (wacom_wac->is_invalid_bt_frame)
		return;

	switch (equivalent_usage) {
	case HID_GD_Z:
		/*
		 * HID_GD_Z "should increase as the control's position is
		 * moved from high to low", while ABS_DISTANCE instead
		 * increases in value as the tool moves from low to high.
		 */
		value = field->logical_maximum - value;
		break;
	case HID_DG_INRANGE:
		wacom_wac->hid_data.inrange_state = value;
		if (!(features->quirks & WACOM_QUIRK_SENSE))
			wacom_wac->hid_data.sense_state = value;
		return;
	case HID_DG_INVERT:
		wacom_wac->hid_data.invert_state = value;
		return;
	case HID_DG_ERASER:
	case HID_DG_TIPSWITCH:
		wacom_wac->hid_data.tipswitch |= value;
		return;
	case HID_DG_BARRELSWITCH:
		wacom_wac->hid_data.barrelswitch = value;
		return;
	case HID_DG_BARRELSWITCH2:
		wacom_wac->hid_data.barrelswitch2 = value;
		return;
	case HID_DG_TOOLSERIALNUMBER:
		if (value) {
			wacom_wac->serial[0] = (wacom_wac->serial[0] & ~0xFFFFFFFFULL);
			wacom_wac->serial[0] |= (__u32)value;
		}
		return;
	case HID_DG_TWIST:
		/*
		 * Userspace expects pen twist to have its zero point when
		 * the buttons/finger is on the tablet's left. HID values
		 * are zero when buttons are toward the top.
		 */
		value = wacom_offset_rotation(input, usage, value, 1, 4);
		break;
	case WACOM_HID_WD_SENSE:
		wacom_wac->hid_data.sense_state = value;
		return;
	case WACOM_HID_WD_SERIALHI:
		if (value) {
			wacom_wac->serial[0] = (wacom_wac->serial[0] & 0xFFFFFFFF);
			wacom_wac->serial[0] |= ((__u64)value) << 32;
			/*
			 * Non-USI EMR devices may contain additional tool type
			 * information here. See WACOM_HID_WD_TOOLTYPE case for
			 * more details.
			 */
			if (value >> 20 == 1) {
				wacom_wac->id[0] |= value & 0xFFFFF;
			}
		}
		return;
	case WACOM_HID_WD_TOOLTYPE:
		/*
		 * Some devices (MobileStudio Pro, and possibly later
		 * devices as well) do not return the complete tool
		 * type in their WACOM_HID_WD_TOOLTYPE usage. Use a
		 * bitwise OR so the complete value can be built
		 * up over time :(
		 */
		wacom_wac->id[0] |= value;
		return;
	case WACOM_HID_WD_OFFSETLEFT:
		if (features->offset_left && value != features->offset_left)
			hid_warn(hdev, "%s: overriding existing left offset "
				 "%d -> %d\n", __func__, value,
				 features->offset_left);
		features->offset_left = value;
		return;
	case WACOM_HID_WD_OFFSETRIGHT:
		if (features->offset_right && value != features->offset_right)
			hid_warn(hdev, "%s: overriding existing right offset "
				 "%d -> %d\n", __func__, value,
				 features->offset_right);
		features->offset_right = value;
		return;
	case WACOM_HID_WD_OFFSETTOP:
		if (features->offset_top && value != features->offset_top)
			hid_warn(hdev, "%s: overriding existing top offset "
				 "%d -> %d\n", __func__, value,
				 features->offset_top);
		features->offset_top = value;
		return;
	case WACOM_HID_WD_OFFSETBOTTOM:
		if (features->offset_bottom && value != features->offset_bottom)
			hid_warn(hdev, "%s: overriding existing bottom offset "
				 "%d -> %d\n", __func__, value,
				 features->offset_bottom);
		features->offset_bottom = value;
		return;
	case WACOM_HID_WD_REPORT_VALID:
		wacom_wac->is_invalid_bt_frame = !value;
		return;
	}

	/* send pen events only when touch is up or forced out
	 * or touch arbitration is off
	 */
	if (!usage->type || delay_pen_events(wacom_wac))
		return;

	/* send pen events only when the pen is in range */
	if (wacom_wac->hid_data.inrange_state)
		input_event(input, usage->type, usage->code, value);
	else if (wacom_wac->shared->stylus_in_proximity && !wacom_wac->hid_data.sense_state)
		input_event(input, usage->type, usage->code, 0);
}

static void wacom_wac_pen_pre_report(struct hid_device *hdev,
		struct hid_report *report)
{
	struct wacom *wacom = hid_get_drvdata(hdev);
	struct wacom_wac *wacom_wac = &wacom->wacom_wac;

	wacom_wac->is_invalid_bt_frame = false;
	return;
}

static void wacom_wac_pen_report(struct hid_device *hdev,
		struct hid_report *report)
{
	struct wacom *wacom = hid_get_drvdata(hdev);
	struct wacom_wac *wacom_wac = &wacom->wacom_wac;
	struct input_dev *input = wacom_wac->pen_input;
	bool range = wacom_wac->hid_data.inrange_state;
	bool sense = wacom_wac->hid_data.sense_state;

	if (wacom_wac->is_invalid_bt_frame)
		return;

	if (!wacom_wac->tool[0] && range) { /* first in range */
		/* Going into range select tool */
		if (wacom_wac->hid_data.invert_state)
			wacom_wac->tool[0] = BTN_TOOL_RUBBER;
		else if (wacom_wac->id[0])
			wacom_wac->tool[0] = wacom_intuos_get_tool_type(wacom_wac->id[0]);
		else
			wacom_wac->tool[0] = BTN_TOOL_PEN;
	}

	/* keep pen state for touch events */
	wacom_wac->shared->stylus_in_proximity = sense;

	if (!delay_pen_events(wacom_wac) && wacom_wac->tool[0]) {
		int id = wacom_wac->id[0];
		int sw_state = wacom_wac->hid_data.barrelswitch |
			       (wacom_wac->hid_data.barrelswitch2 << 1);

		input_report_key(input, BTN_STYLUS, sw_state == 1);
		input_report_key(input, BTN_STYLUS2, sw_state == 2);
		input_report_key(input, BTN_STYLUS3, sw_state == 3);

		/*
		 * Non-USI EMR tools should have their IDs mangled to
		 * match the legacy behavior of wacom_intuos_general
		 */
		if (wacom_wac->serial[0] >> 52 == 1)
			id = wacom_intuos_id_mangle(id);

		/*
		 * To ensure compatibility with xf86-input-wacom, we should
		 * report the BTN_TOOL_* event prior to the ABS_MISC or
		 * MSC_SERIAL events.
		 */
		input_report_key(input, BTN_TOUCH,
				wacom_wac->hid_data.tipswitch);
		input_report_key(input, wacom_wac->tool[0], sense);
		if (wacom_wac->serial[0]) {
			input_event(input, EV_MSC, MSC_SERIAL, wacom_wac->serial[0]);
			input_report_abs(input, ABS_MISC, sense ? id : 0);
		}

		wacom_wac->hid_data.tipswitch = false;

		input_sync(input);
	}

	if (!sense) {
		wacom_wac->tool[0] = 0;
		wacom_wac->id[0] = 0;
		wacom_wac->serial[0] = 0;
	}
}

static void wacom_wac_finger_usage_mapping(struct hid_device *hdev,
		struct hid_field *field, struct hid_usage *usage)
{
	struct wacom *wacom = hid_get_drvdata(hdev);
	struct wacom_wac *wacom_wac = &wacom->wacom_wac;
	struct input_dev *input = wacom_wac->touch_input;
	unsigned touch_max = wacom_wac->features.touch_max;
	unsigned equivalent_usage = wacom_equivalent_usage(usage->hid);

	switch (equivalent_usage) {
	case HID_GD_X:
		if (touch_max == 1)
			wacom_map_usage(input, usage, field, EV_ABS, ABS_X, 4);
		else
			wacom_map_usage(input, usage, field, EV_ABS,
					ABS_MT_POSITION_X, 4);
		break;
	case HID_GD_Y:
		if (touch_max == 1)
			wacom_map_usage(input, usage, field, EV_ABS, ABS_Y, 4);
		else
			wacom_map_usage(input, usage, field, EV_ABS,
					ABS_MT_POSITION_Y, 4);
		break;
	case HID_DG_WIDTH:
	case HID_DG_HEIGHT:
		wacom_map_usage(input, usage, field, EV_ABS, ABS_MT_TOUCH_MAJOR, 0);
		wacom_map_usage(input, usage, field, EV_ABS, ABS_MT_TOUCH_MINOR, 0);
		input_set_abs_params(input, ABS_MT_ORIENTATION, 0, 1, 0, 0);
		break;
	case HID_DG_TIPSWITCH:
		wacom_map_usage(input, usage, field, EV_KEY, BTN_TOUCH, 0);
		break;
	case HID_DG_CONTACTCOUNT:
		wacom_wac->hid_data.cc_report = field->report->id;
		wacom_wac->hid_data.cc_index = field->index;
		wacom_wac->hid_data.cc_value_index = usage->usage_index;
		break;
	case HID_DG_CONTACTID:
		if ((field->logical_maximum - field->logical_minimum) < touch_max) {
			/*
			 * The HID descriptor for G11 sensors leaves logical
			 * maximum set to '1' despite it being a multitouch
			 * device. Override to a sensible number.
			 */
			field->logical_maximum = 255;
		}
		break;
	}
}

static void wacom_wac_finger_slot(struct wacom_wac *wacom_wac,
		struct input_dev *input)
{
	struct hid_data *hid_data = &wacom_wac->hid_data;
	bool mt = wacom_wac->features.touch_max > 1;
	bool prox = hid_data->tipswitch &&
		    report_touch_events(wacom_wac);

	if (wacom_wac->shared->has_mute_touch_switch &&
	    !wacom_wac->shared->is_touch_on) {
		if (!wacom_wac->shared->touch_down)
			return;
		prox = 0;
	}

	wacom_wac->hid_data.num_received++;
	if (wacom_wac->hid_data.num_received > wacom_wac->hid_data.num_expected)
		return;

	if (mt) {
		int slot;

		slot = input_mt_get_slot_by_key(input, hid_data->id);
		input_mt_slot(input, slot);
		input_mt_report_slot_state(input, MT_TOOL_FINGER, prox);
	}
	else {
		input_report_key(input, BTN_TOUCH, prox);
	}

	if (prox) {
		input_report_abs(input, mt ? ABS_MT_POSITION_X : ABS_X,
				 hid_data->x);
		input_report_abs(input, mt ? ABS_MT_POSITION_Y : ABS_Y,
				 hid_data->y);

		if (test_bit(ABS_MT_TOUCH_MAJOR, input->absbit)) {
			input_report_abs(input, ABS_MT_TOUCH_MAJOR, max(hid_data->width, hid_data->height));
			input_report_abs(input, ABS_MT_TOUCH_MINOR, min(hid_data->width, hid_data->height));
			if (hid_data->width != hid_data->height)
				input_report_abs(input, ABS_MT_ORIENTATION, hid_data->width <= hid_data->height ? 0 : 1);
		}
	}
}

static void wacom_wac_finger_event(struct hid_device *hdev,
		struct hid_field *field, struct hid_usage *usage, __s32 value)
{
	struct wacom *wacom = hid_get_drvdata(hdev);
	struct wacom_wac *wacom_wac = &wacom->wacom_wac;
	unsigned equivalent_usage = wacom_equivalent_usage(usage->hid);

	switch (equivalent_usage) {
	case HID_GD_X:
		wacom_wac->hid_data.x = value;
		break;
	case HID_GD_Y:
		wacom_wac->hid_data.y = value;
		break;
	case HID_DG_WIDTH:
		wacom_wac->hid_data.width = value;
		break;
	case HID_DG_HEIGHT:
		wacom_wac->hid_data.height = value;
		break;
	case HID_DG_CONTACTID:
		wacom_wac->hid_data.id = value;
		break;
	case HID_DG_TIPSWITCH:
		wacom_wac->hid_data.tipswitch = value;
		break;
	}


	if (usage->usage_index + 1 == field->report_count) {
		if (equivalent_usage == wacom_wac->hid_data.last_slot_field)
			wacom_wac_finger_slot(wacom_wac, wacom_wac->touch_input);
	}
}

static void wacom_wac_finger_pre_report(struct hid_device *hdev,
		struct hid_report *report)
{
	struct wacom *wacom = hid_get_drvdata(hdev);
	struct wacom_wac *wacom_wac = &wacom->wacom_wac;
	struct hid_data* hid_data = &wacom_wac->hid_data;
	int i;

	for (i = 0; i < report->maxfield; i++) {
		struct hid_field *field = report->field[i];
		int j;

		for (j = 0; j < field->maxusage; j++) {
			struct hid_usage *usage = &field->usage[j];
			unsigned int equivalent_usage =
				wacom_equivalent_usage(usage->hid);

			switch (equivalent_usage) {
			case HID_GD_X:
			case HID_GD_Y:
			case HID_DG_WIDTH:
			case HID_DG_HEIGHT:
			case HID_DG_CONTACTID:
			case HID_DG_INRANGE:
			case HID_DG_INVERT:
			case HID_DG_TIPSWITCH:
				hid_data->last_slot_field = equivalent_usage;
				break;
			case HID_DG_CONTACTCOUNT:
				hid_data->cc_report = report->id;
				hid_data->cc_index = i;
				hid_data->cc_value_index = j;
				break;
			}
		}
	}

	if (hid_data->cc_report != 0 &&
	    hid_data->cc_index >= 0) {
		struct hid_field *field = report->field[hid_data->cc_index];
		int value = field->value[hid_data->cc_value_index];
		if (value)
			hid_data->num_expected = value;
	}
	else {
		hid_data->num_expected = wacom_wac->features.touch_max;
	}
}

static void wacom_wac_finger_report(struct hid_device *hdev,
		struct hid_report *report)
{
	struct wacom *wacom = hid_get_drvdata(hdev);
	struct wacom_wac *wacom_wac = &wacom->wacom_wac;
	struct input_dev *input = wacom_wac->touch_input;
	unsigned touch_max = wacom_wac->features.touch_max;

	/* If more packets of data are expected, give us a chance to
	 * process them rather than immediately syncing a partial
	 * update.
	 */
	if (wacom_wac->hid_data.num_received < wacom_wac->hid_data.num_expected)
		return;

	if (touch_max > 1)
		input_mt_sync_frame(input);

	input_sync(input);
	wacom_wac->hid_data.num_received = 0;

	/* keep touch state for pen event */
	wacom_wac->shared->touch_down = wacom_wac_finger_count_touches(wacom_wac);
}

void wacom_wac_usage_mapping(struct hid_device *hdev,
		struct hid_field *field, struct hid_usage *usage)
{
	struct wacom *wacom = hid_get_drvdata(hdev);
	struct wacom_wac *wacom_wac = &wacom->wacom_wac;
	struct wacom_features *features = &wacom_wac->features;

	if (WACOM_DIRECT_DEVICE(field))
		features->device_type |= WACOM_DEVICETYPE_DIRECT;

	/* usage tests must precede field tests */
	if (WACOM_BATTERY_USAGE(usage))
		wacom_wac_battery_usage_mapping(hdev, field, usage);
	else if (WACOM_PAD_FIELD(field))
		wacom_wac_pad_usage_mapping(hdev, field, usage);
	else if (WACOM_PEN_FIELD(field))
		wacom_wac_pen_usage_mapping(hdev, field, usage);
	else if (WACOM_FINGER_FIELD(field))
		wacom_wac_finger_usage_mapping(hdev, field, usage);
}

void wacom_wac_event(struct hid_device *hdev, struct hid_field *field,
		struct hid_usage *usage, __s32 value)
{
	struct wacom *wacom = hid_get_drvdata(hdev);

	if (wacom->wacom_wac.features.type != HID_GENERIC)
		return;

	if (value > field->logical_maximum || value < field->logical_minimum)
		return;

	/* usage tests must precede field tests */
	if (WACOM_BATTERY_USAGE(usage))
		wacom_wac_battery_event(hdev, field, usage, value);
	else if (WACOM_PAD_FIELD(field) && wacom->wacom_wac.pad_input)
		wacom_wac_pad_event(hdev, field, usage, value);
	else if (WACOM_PEN_FIELD(field) && wacom->wacom_wac.pen_input)
		wacom_wac_pen_event(hdev, field, usage, value);
	else if (WACOM_FINGER_FIELD(field) && wacom->wacom_wac.touch_input)
		wacom_wac_finger_event(hdev, field, usage, value);
}

static void wacom_report_events(struct hid_device *hdev,
				struct hid_report *report, int collection_index,
				int field_index)
{
	int r;

	for (r = field_index; r < report->maxfield; r++) {
		struct hid_field *field;
		unsigned count, n;

		field = report->field[r];
		count = field->report_count;

		if (!(HID_MAIN_ITEM_VARIABLE & field->flags))
			continue;

		for (n = 0 ; n < count; n++) {
			if (field->usage[n].collection_index == collection_index)
				wacom_wac_event(hdev, field, &field->usage[n],
						field->value[n]);
			else
				return;
		}
	}
}

static int wacom_wac_collection(struct hid_device *hdev, struct hid_report *report,
			 int collection_index, struct hid_field *field,
			 int field_index)
{
	struct wacom *wacom = hid_get_drvdata(hdev);

	wacom_report_events(hdev, report, collection_index, field_index);

	/*
	 * Non-input reports may be sent prior to the device being
	 * completely initialized. Since only their events need
	 * to be processed, exit after 'wacom_report_events' has
	 * been called to prevent potential crashes in the report-
	 * processing functions.
	 */
	if (report->type != HID_INPUT_REPORT)
		return -1;

	if (WACOM_PEN_FIELD(field) && wacom->wacom_wac.pen_input)
		wacom_wac_pen_report(hdev, report);
	else if (WACOM_FINGER_FIELD(field) && wacom->wacom_wac.touch_input)
		wacom_wac_finger_report(hdev, report);

	return 0;
}

void wacom_wac_report(struct hid_device *hdev, struct hid_report *report)
{
	struct wacom *wacom = hid_get_drvdata(hdev);
	struct wacom_wac *wacom_wac = &wacom->wacom_wac;
	struct hid_field *field;
	bool pad_in_hid_field = false, pen_in_hid_field = false,
		finger_in_hid_field = false, true_pad = false;
	int r;
	int prev_collection = -1;

	if (wacom_wac->features.type != HID_GENERIC)
		return;

	for (r = 0; r < report->maxfield; r++) {
		field = report->field[r];

		if (WACOM_PAD_FIELD(field))
			pad_in_hid_field = true;
		if (WACOM_PEN_FIELD(field))
			pen_in_hid_field = true;
		if (WACOM_FINGER_FIELD(field))
			finger_in_hid_field = true;
		if (wacom_equivalent_usage(field->physical) == HID_DG_TABLETFUNCTIONKEY)
			true_pad = true;
	}

	wacom_wac_battery_pre_report(hdev, report);

	if (pad_in_hid_field && wacom->wacom_wac.pad_input)
		wacom_wac_pad_pre_report(hdev, report);
	if (pen_in_hid_field && wacom->wacom_wac.pen_input)
		wacom_wac_pen_pre_report(hdev, report);
	if (finger_in_hid_field && wacom->wacom_wac.touch_input)
		wacom_wac_finger_pre_report(hdev, report);

	for (r = 0; r < report->maxfield; r++) {
		field = report->field[r];

		if (field->usage[0].collection_index != prev_collection) {
			if (wacom_wac_collection(hdev, report,
				field->usage[0].collection_index, field, r) < 0)
				return;
			prev_collection = field->usage[0].collection_index;
		}
	}

	wacom_wac_battery_report(hdev, report);

	if (true_pad && wacom->wacom_wac.pad_input)
		wacom_wac_pad_report(hdev, report, field);
}

static int wacom_bpt_touch(struct wacom_wac *wacom)
{
	struct wacom_features *features = &wacom->features;
	struct input_dev *input = wacom->touch_input;
	struct input_dev *pad_input = wacom->pad_input;
	unsigned char *data = wacom->data;
	int i;

	if (data[0] != 0x02)
	    return 0;

	for (i = 0; i < 2; i++) {
		int offset = (data[1] & 0x80) ? (8 * i) : (9 * i);
		bool touch = report_touch_events(wacom)
			   && (data[offset + 3] & 0x80);

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

	input_mt_sync_frame(input);

	input_report_key(pad_input, BTN_LEFT, (data[1] & 0x08) != 0);
	input_report_key(pad_input, BTN_FORWARD, (data[1] & 0x04) != 0);
	input_report_key(pad_input, BTN_BACK, (data[1] & 0x02) != 0);
	input_report_key(pad_input, BTN_RIGHT, (data[1] & 0x01) != 0);
	wacom->shared->touch_down = wacom_wac_finger_count_touches(wacom);

	return 1;
}

static void wacom_bpt3_touch_msg(struct wacom_wac *wacom, unsigned char *data)
{
	struct wacom_features *features = &wacom->features;
	struct input_dev *input = wacom->touch_input;
	bool touch = data[1] & 0x80;
	int slot = input_mt_get_slot_by_key(input, data[0]);

	if (slot < 0)
		return;

	touch = touch && report_touch_events(wacom);

	input_mt_slot(input, slot);
	input_mt_report_slot_state(input, MT_TOOL_FINGER, touch);

	if (touch) {
		int x = (data[2] << 4) | (data[4] >> 4);
		int y = (data[3] << 4) | (data[4] & 0x0f);
		int width, height;

		if (features->type >= INTUOSPS && features->type <= INTUOSHT2) {
			width  = data[5] * 100;
			height = data[6] * 100;
		} else {
			/*
			 * "a" is a scaled-down area which we assume is
			 * roughly circular and which can be described as:
			 * a=(pi*r^2)/C.
			 */
			int a = data[5];
			int x_res = input_abs_get_res(input, ABS_MT_POSITION_X);
			int y_res = input_abs_get_res(input, ABS_MT_POSITION_Y);
			width = 2 * int_sqrt(a * WACOM_CONTACT_AREA_SCALE);
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
	struct input_dev *input = wacom->pad_input;
	struct wacom_features *features = &wacom->features;

	if (features->type == INTUOSHT || features->type == INTUOSHT2) {
		input_report_key(input, BTN_LEFT, (data[1] & 0x02) != 0);
		input_report_key(input, BTN_BACK, (data[1] & 0x08) != 0);
	} else {
		input_report_key(input, BTN_BACK, (data[1] & 0x02) != 0);
		input_report_key(input, BTN_LEFT, (data[1] & 0x08) != 0);
	}
	input_report_key(input, BTN_FORWARD, (data[1] & 0x04) != 0);
	input_report_key(input, BTN_RIGHT, (data[1] & 0x01) != 0);
}

static int wacom_bpt3_touch(struct wacom_wac *wacom)
{
	unsigned char *data = wacom->data;
	int count = data[1] & 0x07;
	int  touch_changed = 0, i;

	if (data[0] != 0x02)
	    return 0;

	/* data has up to 7 fixed sized 8-byte messages starting at data[2] */
	for (i = 0; i < count; i++) {
		int offset = (8 * i) + 2;
		int msg_id = data[offset];

		if (msg_id >= 2 && msg_id <= 17) {
			wacom_bpt3_touch_msg(wacom, data + offset);
			touch_changed++;
		} else if (msg_id == 128)
			wacom_bpt3_button_msg(wacom, data + offset);

	}

	/* only update touch if we actually have a touchpad and touch data changed */
	if (wacom->touch_input && touch_changed) {
		input_mt_sync_frame(wacom->touch_input);
		wacom->shared->touch_down = wacom_wac_finger_count_touches(wacom);
	}

	return 1;
}

static int wacom_bpt_pen(struct wacom_wac *wacom)
{
	struct wacom_features *features = &wacom->features;
	struct input_dev *input = wacom->pen_input;
	unsigned char *data = wacom->data;
	int x = 0, y = 0, p = 0, d = 0;
	bool pen = false, btn1 = false, btn2 = false;
	bool range, prox, rdy;

	if (data[0] != WACOM_REPORT_PENABLED)
	    return 0;

	range = (data[1] & 0x80) == 0x80;
	prox = (data[1] & 0x40) == 0x40;
	rdy = (data[1] & 0x20) == 0x20;

	wacom->shared->stylus_in_proximity = range;
	if (delay_pen_events(wacom))
		return 0;

	if (rdy) {
		p = le16_to_cpup((__le16 *)&data[6]);
		pen = data[1] & 0x01;
		btn1 = data[1] & 0x02;
		btn2 = data[1] & 0x04;
	}
	if (prox) {
		x = le16_to_cpup((__le16 *)&data[2]);
		y = le16_to_cpup((__le16 *)&data[4]);

		if (data[1] & 0x08) {
			wacom->tool[0] = BTN_TOOL_RUBBER;
			wacom->id[0] = ERASER_DEVICE_ID;
		} else {
			wacom->tool[0] = BTN_TOOL_PEN;
			wacom->id[0] = STYLUS_DEVICE_ID;
		}
		wacom->reporting_data = true;
	}
	if (range) {
		/*
		 * Convert distance from out prox to distance from tablet.
		 * distance will be greater than distance_max once
		 * touching and applying pressure; do not report negative
		 * distance.
		 */
		if (data[8] <= features->distance_max)
			d = features->distance_max - data[8];
	} else {
		wacom->id[0] = 0;
	}

	if (wacom->reporting_data) {
		input_report_key(input, BTN_TOUCH, pen);
		input_report_key(input, BTN_STYLUS, btn1);
		input_report_key(input, BTN_STYLUS2, btn2);

		if (prox || !range) {
			input_report_abs(input, ABS_X, x);
			input_report_abs(input, ABS_Y, y);
		}
		input_report_abs(input, ABS_PRESSURE, p);
		input_report_abs(input, ABS_DISTANCE, d);

		input_report_key(input, wacom->tool[0], range); /* PEN or RUBBER */
		input_report_abs(input, ABS_MISC, wacom->id[0]); /* TOOL ID */
	}

	if (!range) {
		wacom->reporting_data = false;
	}

	return 1;
}

static int wacom_bpt_irq(struct wacom_wac *wacom, size_t len)
{
	struct wacom_features *features = &wacom->features;

	if ((features->type == INTUOSHT2) &&
	    (features->device_type & WACOM_DEVICETYPE_PEN))
		return wacom_intuos_irq(wacom);
	else if (len == WACOM_PKGLEN_BBTOUCH)
		return wacom_bpt_touch(wacom);
	else if (len == WACOM_PKGLEN_BBTOUCH3)
		return wacom_bpt3_touch(wacom);
	else if (len == WACOM_PKGLEN_BBFUN || len == WACOM_PKGLEN_BBPEN)
		return wacom_bpt_pen(wacom);

	return 0;
}

static void wacom_bamboo_pad_pen_event(struct wacom_wac *wacom,
		unsigned char *data)
{
	unsigned char prefix;

	/*
	 * We need to reroute the event from the debug interface to the
	 * pen interface.
	 * We need to add the report ID to the actual pen report, so we
	 * temporary overwrite the first byte to prevent having to kzalloc/kfree
	 * and memcpy the report.
	 */
	prefix = data[0];
	data[0] = WACOM_REPORT_BPAD_PEN;

	/*
	 * actually reroute the event.
	 * No need to check if wacom->shared->pen is valid, hid_input_report()
	 * will check for us.
	 */
	hid_input_report(wacom->shared->pen, HID_INPUT_REPORT, data,
			 WACOM_PKGLEN_PENABLED, 1);

	data[0] = prefix;
}

static int wacom_bamboo_pad_touch_event(struct wacom_wac *wacom,
		unsigned char *data)
{
	struct input_dev *input = wacom->touch_input;
	unsigned char *finger_data, prefix;
	unsigned id;
	int x, y;
	bool valid;

	prefix = data[0];

	for (id = 0; id < wacom->features.touch_max; id++) {
		valid = !!(prefix & BIT(id)) &&
			report_touch_events(wacom);

		input_mt_slot(input, id);
		input_mt_report_slot_state(input, MT_TOOL_FINGER, valid);

		if (!valid)
			continue;

		finger_data = data + 1 + id * 3;
		x = finger_data[0] | ((finger_data[1] & 0x0f) << 8);
		y = (finger_data[2] << 4) | (finger_data[1] >> 4);

		input_report_abs(input, ABS_MT_POSITION_X, x);
		input_report_abs(input, ABS_MT_POSITION_Y, y);
	}

	input_mt_sync_frame(input);

	input_report_key(input, BTN_LEFT, prefix & 0x40);
	input_report_key(input, BTN_RIGHT, prefix & 0x80);

	/* keep touch state for pen event */
	wacom->shared->touch_down = !!prefix && report_touch_events(wacom);

	return 1;
}

static int wacom_bamboo_pad_irq(struct wacom_wac *wacom, size_t len)
{
	unsigned char *data = wacom->data;

	if (!((len == WACOM_PKGLEN_BPAD_TOUCH) ||
	      (len == WACOM_PKGLEN_BPAD_TOUCH_USB)) ||
	    (data[0] != WACOM_REPORT_BPAD_TOUCH))
		return 0;

	if (data[1] & 0x01)
		wacom_bamboo_pad_pen_event(wacom, &data[1]);

	if (data[1] & 0x02)
		return wacom_bamboo_pad_touch_event(wacom, &data[9]);

	return 0;
}

static int wacom_wireless_irq(struct wacom_wac *wacom, size_t len)
{
	unsigned char *data = wacom->data;
	int connected;

	if (len != WACOM_PKGLEN_WIRELESS || data[0] != WACOM_REPORT_WL)
		return 0;

	connected = data[1] & 0x01;
	if (connected) {
		int pid, battery, charging;

		if ((wacom->shared->type == INTUOSHT ||
		    wacom->shared->type == INTUOSHT2) &&
		    wacom->shared->touch_input &&
		    wacom->shared->touch_max) {
			input_report_switch(wacom->shared->touch_input,
					SW_MUTE_DEVICE, data[5] & 0x40);
			input_sync(wacom->shared->touch_input);
		}

		pid = get_unaligned_be16(&data[6]);
		battery = (data[5] & 0x3f) * 100 / 31;
		charging = !!(data[5] & 0x80);
		if (wacom->pid != pid) {
			wacom->pid = pid;
			wacom_schedule_work(wacom, WACOM_WORKER_WIRELESS);
		}

		wacom_notify_battery(wacom, WACOM_POWER_SUPPLY_STATUS_AUTO,
				     battery, charging, 1, 0);

	} else if (wacom->pid != 0) {
		/* disconnected while previously connected */
		wacom->pid = 0;
		wacom_schedule_work(wacom, WACOM_WORKER_WIRELESS);
		wacom_notify_battery(wacom, POWER_SUPPLY_STATUS_UNKNOWN, 0, 0, 0, 0);
	}

	return 0;
}

static int wacom_status_irq(struct wacom_wac *wacom_wac, size_t len)
{
	struct wacom *wacom = container_of(wacom_wac, struct wacom, wacom_wac);
	struct wacom_features *features = &wacom_wac->features;
	unsigned char *data = wacom_wac->data;

	if (data[0] != WACOM_REPORT_USB)
		return 0;

	if ((features->type == INTUOSHT ||
	    features->type == INTUOSHT2) &&
	    wacom_wac->shared->touch_input &&
	    features->touch_max) {
		input_report_switch(wacom_wac->shared->touch_input,
				    SW_MUTE_DEVICE, data[8] & 0x40);
		input_sync(wacom_wac->shared->touch_input);
	}

	if (data[9] & 0x02) { /* wireless module is attached */
		int battery = (data[8] & 0x3f) * 100 / 31;
		bool charging = !!(data[8] & 0x80);

		wacom_notify_battery(wacom_wac, WACOM_POWER_SUPPLY_STATUS_AUTO,
				     battery, charging, battery || charging, 1);

		if (!wacom->battery.battery &&
		    !(features->quirks & WACOM_QUIRK_BATTERY)) {
			features->quirks |= WACOM_QUIRK_BATTERY;
			wacom_schedule_work(wacom_wac, WACOM_WORKER_BATTERY);
		}
	}
	else if ((features->quirks & WACOM_QUIRK_BATTERY) &&
		 wacom->battery.battery) {
		features->quirks &= ~WACOM_QUIRK_BATTERY;
		wacom_schedule_work(wacom_wac, WACOM_WORKER_BATTERY);
		wacom_notify_battery(wacom_wac, POWER_SUPPLY_STATUS_UNKNOWN, 0, 0, 0, 0);
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
	case GRAPHIRE_BT:
	case WACOM_MO:
		sync = wacom_graphire_irq(wacom_wac);
		break;

	case PTU:
		sync = wacom_ptu_irq(wacom_wac);
		break;

	case DTU:
		sync = wacom_dtu_irq(wacom_wac);
		break;

	case DTUS:
	case DTUSX:
		sync = wacom_dtus_irq(wacom_wac);
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
	case WACOM_27QHD:
	case DTK:
	case CINTIQ_HYBRID:
	case CINTIQ_COMPANION_2:
		sync = wacom_intuos_irq(wacom_wac);
		break;

	case INTUOS4WL:
		sync = wacom_intuos_bt_irq(wacom_wac, len);
		break;

	case WACOM_24HDT:
	case WACOM_27QHDT:
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
		else if (wacom_wac->data[0] == WACOM_REPORT_USB)
			sync = wacom_status_irq(wacom_wac, len);
		else
			sync = wacom_intuos_irq(wacom_wac);
		break;

	case INTUOSP2_BT:
	case INTUOSHT3_BT:
		sync = wacom_intuos_pro2_bt_irq(wacom_wac, len);
		break;

	case TABLETPC:
	case TABLETPCE:
	case TABLETPC2FG:
	case MTSCREEN:
	case MTTPC:
	case MTTPC_B:
		sync = wacom_tpc_irq(wacom_wac, len);
		break;

	case BAMBOO_PT:
	case BAMBOO_PEN:
	case BAMBOO_TOUCH:
	case INTUOSHT:
	case INTUOSHT2:
		if (wacom_wac->data[0] == WACOM_REPORT_USB)
			sync = wacom_status_irq(wacom_wac, len);
		else
			sync = wacom_bpt_irq(wacom_wac, len);
		break;

	case BAMBOO_PAD:
		sync = wacom_bamboo_pad_irq(wacom_wac, len);
		break;

	case WIRELESS:
		sync = wacom_wireless_irq(wacom_wac, len);
		break;

	case REMOTE:
		sync = false;
		if (wacom_wac->data[0] == WACOM_REPORT_DEVICE_LIST)
			wacom_remote_status_irq(wacom_wac, len);
		else
			sync = wacom_remote_irq(wacom_wac, len);
		break;

	default:
		sync = false;
		break;
	}

	if (sync) {
		if (wacom_wac->pen_input)
			input_sync(wacom_wac->pen_input);
		if (wacom_wac->touch_input)
			input_sync(wacom_wac->touch_input);
		if (wacom_wac->pad_input)
			input_sync(wacom_wac->pad_input);
	}
}

static void wacom_setup_basic_pro_pen(struct wacom_wac *wacom_wac)
{
	struct input_dev *input_dev = wacom_wac->pen_input;

	input_set_capability(input_dev, EV_MSC, MSC_SERIAL);

	__set_bit(BTN_TOOL_PEN, input_dev->keybit);
	__set_bit(BTN_STYLUS, input_dev->keybit);
	__set_bit(BTN_STYLUS2, input_dev->keybit);

	input_set_abs_params(input_dev, ABS_DISTANCE,
			     0, wacom_wac->features.distance_max, wacom_wac->features.distance_fuzz, 0);
}

static void wacom_setup_cintiq(struct wacom_wac *wacom_wac)
{
	struct input_dev *input_dev = wacom_wac->pen_input;
	struct wacom_features *features = &wacom_wac->features;

	wacom_setup_basic_pro_pen(wacom_wac);

	__set_bit(BTN_TOOL_RUBBER, input_dev->keybit);
	__set_bit(BTN_TOOL_BRUSH, input_dev->keybit);
	__set_bit(BTN_TOOL_PENCIL, input_dev->keybit);
	__set_bit(BTN_TOOL_AIRBRUSH, input_dev->keybit);

	input_set_abs_params(input_dev, ABS_WHEEL, 0, 1023, 0, 0);
	input_set_abs_params(input_dev, ABS_TILT_X, -64, 63, features->tilt_fuzz, 0);
	input_abs_set_res(input_dev, ABS_TILT_X, 57);
	input_set_abs_params(input_dev, ABS_TILT_Y, -64, 63, features->tilt_fuzz, 0);
	input_abs_set_res(input_dev, ABS_TILT_Y, 57);
}

static void wacom_setup_intuos(struct wacom_wac *wacom_wac)
{
	struct input_dev *input_dev = wacom_wac->pen_input;

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
	input_abs_set_res(input_dev, ABS_RZ, 287);
	input_set_abs_params(input_dev, ABS_THROTTLE, -1023, 1023, 0, 0);
}

void wacom_setup_device_quirks(struct wacom *wacom)
{
	struct wacom_wac *wacom_wac = &wacom->wacom_wac;
	struct wacom_features *features = &wacom->wacom_wac.features;

	/* The pen and pad share the same interface on most devices */
	if (features->type == GRAPHIRE_BT || features->type == WACOM_G4 ||
	    features->type == DTUS ||
	    (features->type >= INTUOS3S && features->type <= WACOM_MO)) {
		if (features->device_type & WACOM_DEVICETYPE_PEN)
			features->device_type |= WACOM_DEVICETYPE_PAD;
	}

	/* touch device found but size is not defined. use default */
	if (features->device_type & WACOM_DEVICETYPE_TOUCH && !features->x_max) {
		features->x_max = 1023;
		features->y_max = 1023;
	}

	/*
	 * Intuos5/Pro and Bamboo 3rd gen have no useful data about its
	 * touch interface in its HID descriptor. If this is the touch
	 * interface (PacketSize of WACOM_PKGLEN_BBTOUCH3), override the
	 * tablet values.
	 */
	if ((features->type >= INTUOS5S && features->type <= INTUOSPL) ||
		(features->type >= INTUOSHT && features->type <= BAMBOO_PT)) {
		if (features->pktlen == WACOM_PKGLEN_BBTOUCH3) {
			if (features->touch_max)
				features->device_type |= WACOM_DEVICETYPE_TOUCH;
			if (features->type >= INTUOSHT && features->type <= BAMBOO_PT)
				features->device_type |= WACOM_DEVICETYPE_PAD;

			if (features->type == INTUOSHT2) {
				features->x_max = features->x_max / 10;
				features->y_max = features->y_max / 10;
			}
			else {
				features->x_max = 4096;
				features->y_max = 4096;
			}
		}
		else if (features->pktlen == WACOM_PKGLEN_BBTOUCH) {
			features->device_type |= WACOM_DEVICETYPE_PAD;
		}
	}

	/*
	 * Hack for the Bamboo One:
	 * the device presents a PAD/Touch interface as most Bamboos and even
	 * sends ghosts PAD data on it. However, later, we must disable this
	 * ghost interface, and we can not detect it unless we set it here
	 * to WACOM_DEVICETYPE_PAD or WACOM_DEVICETYPE_TOUCH.
	 */
	if (features->type == BAMBOO_PEN &&
	    features->pktlen == WACOM_PKGLEN_BBTOUCH3)
		features->device_type |= WACOM_DEVICETYPE_PAD;

	/*
	 * Raw Wacom-mode pen and touch events both come from interface
	 * 0, whose HID descriptor has an application usage of 0xFF0D
	 * (i.e., WACOM_HID_WD_DIGITIZER). We route pen packets back
	 * out through the HID_GENERIC device created for interface 1,
	 * so rewrite this one to be of type WACOM_DEVICETYPE_TOUCH.
	 */
	if (features->type == BAMBOO_PAD)
		features->device_type = WACOM_DEVICETYPE_TOUCH;

	if (features->type == REMOTE)
		features->device_type = WACOM_DEVICETYPE_PAD;

	if (features->type == INTUOSP2_BT) {
		features->device_type |= WACOM_DEVICETYPE_PEN |
					 WACOM_DEVICETYPE_PAD |
					 WACOM_DEVICETYPE_TOUCH;
		features->quirks |= WACOM_QUIRK_BATTERY;
	}

	if (features->type == INTUOSHT3_BT) {
		features->device_type |= WACOM_DEVICETYPE_PEN |
					 WACOM_DEVICETYPE_PAD;
		features->quirks |= WACOM_QUIRK_BATTERY;
	}

	switch (features->type) {
	case PL:
	case DTU:
	case DTUS:
	case DTUSX:
	case WACOM_21UX2:
	case WACOM_22HD:
	case DTK:
	case WACOM_24HD:
	case WACOM_27QHD:
	case CINTIQ_HYBRID:
	case CINTIQ_COMPANION_2:
	case CINTIQ:
	case WACOM_BEE:
	case WACOM_13HD:
	case WACOM_24HDT:
	case WACOM_27QHDT:
	case TABLETPC:
	case TABLETPCE:
	case TABLETPC2FG:
	case MTSCREEN:
	case MTTPC:
	case MTTPC_B:
		features->device_type |= WACOM_DEVICETYPE_DIRECT;
		break;
	}

	if (wacom->hdev->bus == BUS_BLUETOOTH)
		features->quirks |= WACOM_QUIRK_BATTERY;

	/* quirk for bamboo touch with 2 low res touches */
	if ((features->type == BAMBOO_PT || features->type == BAMBOO_TOUCH) &&
	    features->pktlen == WACOM_PKGLEN_BBTOUCH) {
		features->x_max <<= 5;
		features->y_max <<= 5;
		features->x_fuzz <<= 5;
		features->y_fuzz <<= 5;
		features->quirks |= WACOM_QUIRK_BBTOUCH_LOWRES;
	}

	if (features->type == WIRELESS) {
		if (features->device_type == WACOM_DEVICETYPE_WL_MONITOR) {
			features->quirks |= WACOM_QUIRK_BATTERY;
		}
	}

	if (features->type == REMOTE)
		features->device_type |= WACOM_DEVICETYPE_WL_MONITOR;

	/* HID descriptor for DTK-2451 / DTH-2452 claims to report lots
	 * of things it shouldn't. Lets fix up the damage...
	 */
	if (wacom->hdev->product == 0x382 || wacom->hdev->product == 0x37d) {
		features->quirks &= ~WACOM_QUIRK_TOOLSERIAL;
		__clear_bit(BTN_TOOL_BRUSH, wacom_wac->pen_input->keybit);
		__clear_bit(BTN_TOOL_PENCIL, wacom_wac->pen_input->keybit);
		__clear_bit(BTN_TOOL_AIRBRUSH, wacom_wac->pen_input->keybit);
		__clear_bit(ABS_Z, wacom_wac->pen_input->absbit);
		__clear_bit(ABS_DISTANCE, wacom_wac->pen_input->absbit);
		__clear_bit(ABS_TILT_X, wacom_wac->pen_input->absbit);
		__clear_bit(ABS_TILT_Y, wacom_wac->pen_input->absbit);
		__clear_bit(ABS_WHEEL, wacom_wac->pen_input->absbit);
		__clear_bit(ABS_MISC, wacom_wac->pen_input->absbit);
		__clear_bit(MSC_SERIAL, wacom_wac->pen_input->mscbit);
		__clear_bit(EV_MSC, wacom_wac->pen_input->evbit);
	}
}

int wacom_setup_pen_input_capabilities(struct input_dev *input_dev,
				   struct wacom_wac *wacom_wac)
{
	struct wacom_features *features = &wacom_wac->features;

	input_dev->evbit[0] |= BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);

	if (!(features->device_type & WACOM_DEVICETYPE_PEN))
		return -ENODEV;

	if (features->device_type & WACOM_DEVICETYPE_DIRECT)
		__set_bit(INPUT_PROP_DIRECT, input_dev->propbit);
	else
		__set_bit(INPUT_PROP_POINTER, input_dev->propbit);

	if (features->type == HID_GENERIC) {
		/* setup has already been done; apply otherwise-undetectible quirks */
		input_set_capability(input_dev, EV_KEY, BTN_STYLUS3);
		return 0;
	}

	__set_bit(BTN_TOUCH, input_dev->keybit);
	__set_bit(ABS_MISC, input_dev->absbit);

	input_set_abs_params(input_dev, ABS_X, 0 + features->offset_left,
			     features->x_max - features->offset_right,
			     features->x_fuzz, 0);
	input_set_abs_params(input_dev, ABS_Y, 0 + features->offset_top,
			     features->y_max - features->offset_bottom,
			     features->y_fuzz, 0);
	input_set_abs_params(input_dev, ABS_PRESSURE, 0,
		features->pressure_max, features->pressure_fuzz, 0);

	/* penabled devices have fixed resolution for each model */
	input_abs_set_res(input_dev, ABS_X, features->x_resolution);
	input_abs_set_res(input_dev, ABS_Y, features->y_resolution);

	switch (features->type) {
	case GRAPHIRE_BT:
		__clear_bit(ABS_MISC, input_dev->absbit);
		/* fall through */

	case WACOM_MO:
	case WACOM_G4:
		input_set_abs_params(input_dev, ABS_DISTANCE, 0,
					      features->distance_max,
					      features->distance_fuzz, 0);
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
		break;

	case WACOM_27QHD:
	case WACOM_24HD:
	case DTK:
	case WACOM_22HD:
	case WACOM_21UX2:
	case WACOM_BEE:
	case CINTIQ:
	case WACOM_13HD:
	case CINTIQ_HYBRID:
	case CINTIQ_COMPANION_2:
		input_set_abs_params(input_dev, ABS_Z, -900, 899, 0, 0);
		input_abs_set_res(input_dev, ABS_Z, 287);
		wacom_setup_cintiq(wacom_wac);
		break;

	case INTUOS3:
	case INTUOS3L:
	case INTUOS3S:
	case INTUOS4:
	case INTUOS4WL:
	case INTUOS4L:
	case INTUOS4S:
		input_set_abs_params(input_dev, ABS_Z, -900, 899, 0, 0);
		input_abs_set_res(input_dev, ABS_Z, 287);
		/* fall through */

	case INTUOS:
		wacom_setup_intuos(wacom_wac);
		break;

	case INTUOS5:
	case INTUOS5L:
	case INTUOSPM:
	case INTUOSPL:
	case INTUOS5S:
	case INTUOSPS:
	case INTUOSP2_BT:
		input_set_abs_params(input_dev, ABS_DISTANCE, 0,
				      features->distance_max,
				      features->distance_fuzz, 0);

		input_set_abs_params(input_dev, ABS_Z, -900, 899, 0, 0);
		input_abs_set_res(input_dev, ABS_Z, 287);

		wacom_setup_intuos(wacom_wac);
		break;

	case WACOM_24HDT:
	case WACOM_27QHDT:
	case MTSCREEN:
	case MTTPC:
	case MTTPC_B:
	case TABLETPC2FG:
	case TABLETPC:
	case TABLETPCE:
		__clear_bit(ABS_MISC, input_dev->absbit);
		/* fall through */

	case DTUS:
	case DTUSX:
	case PL:
	case DTU:
		__set_bit(BTN_TOOL_PEN, input_dev->keybit);
		__set_bit(BTN_TOOL_RUBBER, input_dev->keybit);
		__set_bit(BTN_STYLUS, input_dev->keybit);
		__set_bit(BTN_STYLUS2, input_dev->keybit);
		break;

	case PTU:
		__set_bit(BTN_STYLUS2, input_dev->keybit);
		/* fall through */

	case PENPARTNER:
		__set_bit(BTN_TOOL_PEN, input_dev->keybit);
		__set_bit(BTN_TOOL_RUBBER, input_dev->keybit);
		__set_bit(BTN_STYLUS, input_dev->keybit);
		break;

	case INTUOSHT:
	case BAMBOO_PT:
	case BAMBOO_PEN:
	case INTUOSHT2:
	case INTUOSHT3_BT:
		if (features->type == INTUOSHT2 ||
		    features->type == INTUOSHT3_BT) {
			wacom_setup_basic_pro_pen(wacom_wac);
		} else {
			__clear_bit(ABS_MISC, input_dev->absbit);
			__set_bit(BTN_TOOL_PEN, input_dev->keybit);
			__set_bit(BTN_TOOL_RUBBER, input_dev->keybit);
			__set_bit(BTN_STYLUS, input_dev->keybit);
			__set_bit(BTN_STYLUS2, input_dev->keybit);
			input_set_abs_params(input_dev, ABS_DISTANCE, 0,
				      features->distance_max,
				      features->distance_fuzz, 0);
		}
		break;
	case BAMBOO_PAD:
		__clear_bit(ABS_MISC, input_dev->absbit);
		break;
	}
	return 0;
}

int wacom_setup_touch_input_capabilities(struct input_dev *input_dev,
					 struct wacom_wac *wacom_wac)
{
	struct wacom_features *features = &wacom_wac->features;

	input_dev->evbit[0] |= BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);

	if (!(features->device_type & WACOM_DEVICETYPE_TOUCH))
		return -ENODEV;

	if (features->device_type & WACOM_DEVICETYPE_DIRECT)
		__set_bit(INPUT_PROP_DIRECT, input_dev->propbit);
	else
		__set_bit(INPUT_PROP_POINTER, input_dev->propbit);

	if (features->type == HID_GENERIC)
		/* setup has already been done */
		return 0;

	__set_bit(BTN_TOUCH, input_dev->keybit);

	if (features->touch_max == 1) {
		input_set_abs_params(input_dev, ABS_X, 0,
			features->x_max, features->x_fuzz, 0);
		input_set_abs_params(input_dev, ABS_Y, 0,
			features->y_max, features->y_fuzz, 0);
		input_abs_set_res(input_dev, ABS_X,
				  features->x_resolution);
		input_abs_set_res(input_dev, ABS_Y,
				  features->y_resolution);
	}
	else if (features->touch_max > 1) {
		input_set_abs_params(input_dev, ABS_MT_POSITION_X, 0,
			features->x_max, features->x_fuzz, 0);
		input_set_abs_params(input_dev, ABS_MT_POSITION_Y, 0,
			features->y_max, features->y_fuzz, 0);
		input_abs_set_res(input_dev, ABS_MT_POSITION_X,
				  features->x_resolution);
		input_abs_set_res(input_dev, ABS_MT_POSITION_Y,
				  features->y_resolution);
	}

	switch (features->type) {
	case INTUOSP2_BT:
		input_dev->evbit[0] |= BIT_MASK(EV_SW);
		__set_bit(SW_MUTE_DEVICE, input_dev->swbit);

		if (wacom_wac->shared->touch->product == 0x361) {
			input_set_abs_params(input_dev, ABS_MT_POSITION_X,
					     0, 12440, 4, 0);
			input_set_abs_params(input_dev, ABS_MT_POSITION_Y,
					     0, 8640, 4, 0);
		}
		else if (wacom_wac->shared->touch->product == 0x360) {
			input_set_abs_params(input_dev, ABS_MT_POSITION_X,
					     0, 8960, 4, 0);
			input_set_abs_params(input_dev, ABS_MT_POSITION_Y,
					     0, 5920, 4, 0);
		}
		input_abs_set_res(input_dev, ABS_MT_POSITION_X, 40);
		input_abs_set_res(input_dev, ABS_MT_POSITION_Y, 40);

		/* fall through */

	case INTUOS5:
	case INTUOS5L:
	case INTUOSPM:
	case INTUOSPL:
	case INTUOS5S:
	case INTUOSPS:
		input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR, 0, features->x_max, 0, 0);
		input_set_abs_params(input_dev, ABS_MT_TOUCH_MINOR, 0, features->y_max, 0, 0);
		input_mt_init_slots(input_dev, features->touch_max, INPUT_MT_POINTER);
		break;

	case WACOM_24HDT:
		input_set_abs_params(input_dev, ABS_MT_TOUCH_MAJOR, 0, features->x_max, 0, 0);
		input_set_abs_params(input_dev, ABS_MT_WIDTH_MAJOR, 0, features->x_max, 0, 0);
		input_set_abs_params(input_dev, ABS_MT_WIDTH_MINOR, 0, features->y_max, 0, 0);
		input_set_abs_params(input_dev, ABS_MT_ORIENTATION, 0, 1, 0, 0);
		/* fall through */

	case WACOM_27QHDT:
	case MTSCREEN:
	case MTTPC:
	case MTTPC_B:
	case TABLETPC2FG:
		input_mt_init_slots(input_dev, features->touch_max, INPUT_MT_DIRECT);
		/*fall through */

	case TABLETPC:
	case TABLETPCE:
		break;

	case INTUOSHT:
	case INTUOSHT2:
		input_dev->evbit[0] |= BIT_MASK(EV_SW);
		__set_bit(SW_MUTE_DEVICE, input_dev->swbit);
		/* fall through */

	case BAMBOO_PT:
	case BAMBOO_TOUCH:
		if (features->pktlen == WACOM_PKGLEN_BBTOUCH3) {
			input_set_abs_params(input_dev,
				     ABS_MT_TOUCH_MAJOR,
				     0, features->x_max, 0, 0);
			input_set_abs_params(input_dev,
				     ABS_MT_TOUCH_MINOR,
				     0, features->y_max, 0, 0);
		}
		input_mt_init_slots(input_dev, features->touch_max, INPUT_MT_POINTER);
		break;

	case BAMBOO_PAD:
		input_mt_init_slots(input_dev, features->touch_max,
				    INPUT_MT_POINTER);
		__set_bit(BTN_LEFT, input_dev->keybit);
		__set_bit(BTN_RIGHT, input_dev->keybit);
		break;
	}
	return 0;
}

static int wacom_numbered_button_to_key(int n)
{
	if (n < 10)
		return BTN_0 + n;
	else if (n < 16)
		return BTN_A + (n-10);
	else if (n < 18)
		return BTN_BASE + (n-16);
	else
		return 0;
}

static void wacom_setup_numbered_buttons(struct input_dev *input_dev,
				int button_count)
{
	int i;

	for (i = 0; i < button_count; i++) {
		int key = wacom_numbered_button_to_key(i);

		if (key)
			__set_bit(key, input_dev->keybit);
	}
}

static void wacom_24hd_update_leds(struct wacom *wacom, int mask, int group)
{
	struct wacom_led *led;
	int i;
	bool updated = false;

	/*
	 * 24HD has LED group 1 to the left and LED group 0 to the right.
	 * So group 0 matches the second half of the buttons and thus the mask
	 * needs to be shifted.
	 */
	if (group == 0)
		mask >>= 8;

	for (i = 0; i < 3; i++) {
		led = wacom_led_find(wacom, group, i);
		if (!led) {
			hid_err(wacom->hdev, "can't find LED %d in group %d\n",
				i, group);
			continue;
		}
		if (!updated && mask & BIT(i)) {
			led->held = true;
			led_trigger_event(&led->trigger, LED_FULL);
		} else {
			led->held = false;
		}
	}
}

static bool wacom_is_led_toggled(struct wacom *wacom, int button_count,
				 int mask, int group)
{
	int group_button;

	/*
	 * 21UX2 has LED group 1 to the left and LED group 0
	 * to the right. We need to reverse the group to match this
	 * historical behavior.
	 */
	if (wacom->wacom_wac.features.type == WACOM_21UX2)
		group = 1 - group;

	group_button = group * (button_count/wacom->led.count);

	if (wacom->wacom_wac.features.type == INTUOSP2_BT)
		group_button = 8;

	return mask & (1 << group_button);
}

static void wacom_update_led(struct wacom *wacom, int button_count, int mask,
			     int group)
{
	struct wacom_led *led, *next_led;
	int cur;
	bool pressed;

	if (wacom->wacom_wac.features.type == WACOM_24HD)
		return wacom_24hd_update_leds(wacom, mask, group);

	pressed = wacom_is_led_toggled(wacom, button_count, mask, group);
	cur = wacom->led.groups[group].select;

	led = wacom_led_find(wacom, group, cur);
	if (!led) {
		hid_err(wacom->hdev, "can't find current LED %d in group %d\n",
			cur, group);
		return;
	}

	if (!pressed) {
		led->held = false;
		return;
	}

	if (led->held && pressed)
		return;

	next_led = wacom_led_next(wacom, led);
	if (!next_led) {
		hid_err(wacom->hdev, "can't find next LED in group %d\n",
			group);
		return;
	}
	if (next_led == led)
		return;

	next_led->held = true;
	led_trigger_event(&next_led->trigger,
			  wacom_leds_brightness_get(next_led));
}

static void wacom_report_numbered_buttons(struct input_dev *input_dev,
				int button_count, int mask)
{
	struct wacom *wacom = input_get_drvdata(input_dev);
	int i;

	for (i = 0; i < wacom->led.count; i++)
		wacom_update_led(wacom,  button_count, mask, i);

	for (i = 0; i < button_count; i++) {
		int key = wacom_numbered_button_to_key(i);

		if (key)
			input_report_key(input_dev, key, mask & (1 << i));
	}
}

int wacom_setup_pad_input_capabilities(struct input_dev *input_dev,
				   struct wacom_wac *wacom_wac)
{
	struct wacom_features *features = &wacom_wac->features;

	if ((features->type == HID_GENERIC) && features->numbered_buttons > 0)
		features->device_type |= WACOM_DEVICETYPE_PAD;

	if (!(features->device_type & WACOM_DEVICETYPE_PAD))
		return -ENODEV;

	if (features->type == REMOTE && input_dev == wacom_wac->pad_input)
		return -ENODEV;

	input_dev->evbit[0] |= BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);

	/* kept for making legacy xf86-input-wacom working with the wheels */
	__set_bit(ABS_MISC, input_dev->absbit);

	/* kept for making legacy xf86-input-wacom accepting the pad */
	if (!(input_dev->absinfo && (input_dev->absinfo[ABS_X].minimum ||
	      input_dev->absinfo[ABS_X].maximum)))
		input_set_abs_params(input_dev, ABS_X, 0, 1, 0, 0);
	if (!(input_dev->absinfo && (input_dev->absinfo[ABS_Y].minimum ||
	      input_dev->absinfo[ABS_Y].maximum)))
		input_set_abs_params(input_dev, ABS_Y, 0, 1, 0, 0);

	/* kept for making udev and libwacom accepting the pad */
	__set_bit(BTN_STYLUS, input_dev->keybit);

	wacom_setup_numbered_buttons(input_dev, features->numbered_buttons);

	switch (features->type) {

	case CINTIQ_HYBRID:
	case CINTIQ_COMPANION_2:
	case DTK:
	case DTUS:
	case GRAPHIRE_BT:
		break;

	case WACOM_MO:
		__set_bit(BTN_BACK, input_dev->keybit);
		__set_bit(BTN_LEFT, input_dev->keybit);
		__set_bit(BTN_FORWARD, input_dev->keybit);
		__set_bit(BTN_RIGHT, input_dev->keybit);
		input_set_abs_params(input_dev, ABS_WHEEL, 0, 71, 0, 0);
		break;

	case WACOM_G4:
		__set_bit(BTN_BACK, input_dev->keybit);
		__set_bit(BTN_FORWARD, input_dev->keybit);
		input_set_capability(input_dev, EV_REL, REL_WHEEL);
		break;

	case WACOM_24HD:
		__set_bit(KEY_PROG1, input_dev->keybit);
		__set_bit(KEY_PROG2, input_dev->keybit);
		__set_bit(KEY_PROG3, input_dev->keybit);

		input_set_abs_params(input_dev, ABS_WHEEL, 0, 71, 0, 0);
		input_set_abs_params(input_dev, ABS_THROTTLE, 0, 71, 0, 0);
		break;

	case WACOM_27QHD:
		__set_bit(KEY_PROG1, input_dev->keybit);
		__set_bit(KEY_PROG2, input_dev->keybit);
		__set_bit(KEY_PROG3, input_dev->keybit);
		input_set_abs_params(input_dev, ABS_X, -2048, 2048, 0, 0);
		input_abs_set_res(input_dev, ABS_X, 1024); /* points/g */
		input_set_abs_params(input_dev, ABS_Y, -2048, 2048, 0, 0);
		input_abs_set_res(input_dev, ABS_Y, 1024);
		input_set_abs_params(input_dev, ABS_Z, -2048, 2048, 0, 0);
		input_abs_set_res(input_dev, ABS_Z, 1024);
		__set_bit(INPUT_PROP_ACCELEROMETER, input_dev->propbit);
		break;

	case WACOM_22HD:
		__set_bit(KEY_PROG1, input_dev->keybit);
		__set_bit(KEY_PROG2, input_dev->keybit);
		__set_bit(KEY_PROG3, input_dev->keybit);
		/* fall through */

	case WACOM_21UX2:
	case WACOM_BEE:
	case CINTIQ:
		input_set_abs_params(input_dev, ABS_RX, 0, 4096, 0, 0);
		input_set_abs_params(input_dev, ABS_RY, 0, 4096, 0, 0);
		break;

	case WACOM_13HD:
		input_set_abs_params(input_dev, ABS_WHEEL, 0, 71, 0, 0);
		break;

	case INTUOS3:
	case INTUOS3L:
		input_set_abs_params(input_dev, ABS_RY, 0, 4096, 0, 0);
		/* fall through */

	case INTUOS3S:
		input_set_abs_params(input_dev, ABS_RX, 0, 4096, 0, 0);
		break;

	case INTUOS5:
	case INTUOS5L:
	case INTUOSPM:
	case INTUOSPL:
	case INTUOS5S:
	case INTUOSPS:
	case INTUOSP2_BT:
		input_set_abs_params(input_dev, ABS_WHEEL, 0, 71, 0, 0);
		break;

	case INTUOS4WL:
		/*
		 * For Bluetooth devices, the udev rule does not work correctly
		 * for pads unless we add a stylus capability, which forces
		 * ID_INPUT_TABLET to be set.
		 */
		__set_bit(BTN_STYLUS, input_dev->keybit);
		/* fall through */

	case INTUOS4:
	case INTUOS4L:
	case INTUOS4S:
		input_set_abs_params(input_dev, ABS_WHEEL, 0, 71, 0, 0);
		break;

	case INTUOSHT:
	case BAMBOO_PT:
	case BAMBOO_TOUCH:
	case INTUOSHT2:
		__clear_bit(ABS_MISC, input_dev->absbit);

		__set_bit(BTN_LEFT, input_dev->keybit);
		__set_bit(BTN_FORWARD, input_dev->keybit);
		__set_bit(BTN_BACK, input_dev->keybit);
		__set_bit(BTN_RIGHT, input_dev->keybit);

		break;

	case REMOTE:
		input_set_capability(input_dev, EV_MSC, MSC_SERIAL);
		input_set_abs_params(input_dev, ABS_WHEEL, 0, 71, 0, 0);
		break;

	case INTUOSHT3_BT:
	case HID_GENERIC:
		break;

	default:
		/* no pad supported */
		return -ENODEV;
	}
	return 0;
}

static const struct wacom_features wacom_features_0x00 =
	{ "Wacom Penpartner", 5040, 3780, 255, 0,
	  PENPARTNER, WACOM_PENPRTN_RES, WACOM_PENPRTN_RES };
static const struct wacom_features wacom_features_0x10 =
	{ "Wacom Graphire", 10206, 7422, 511, 63,
	  GRAPHIRE, WACOM_GRAPHIRE_RES, WACOM_GRAPHIRE_RES };
static const struct wacom_features wacom_features_0x81 =
	{ "Wacom Graphire BT", 16704, 12064, 511, 32,
	  GRAPHIRE_BT, WACOM_GRAPHIRE_RES, WACOM_GRAPHIRE_RES, 2 };
static const struct wacom_features wacom_features_0x11 =
	{ "Wacom Graphire2 4x5", 10206, 7422, 511, 63,
	  GRAPHIRE, WACOM_GRAPHIRE_RES, WACOM_GRAPHIRE_RES };
static const struct wacom_features wacom_features_0x12 =
	{ "Wacom Graphire2 5x7", 13918, 10206, 511, 63,
	  GRAPHIRE, WACOM_GRAPHIRE_RES, WACOM_GRAPHIRE_RES };
static const struct wacom_features wacom_features_0x13 =
	{ "Wacom Graphire3", 10208, 7424, 511, 63,
	  GRAPHIRE, WACOM_GRAPHIRE_RES, WACOM_GRAPHIRE_RES };
static const struct wacom_features wacom_features_0x14 =
	{ "Wacom Graphire3 6x8", 16704, 12064, 511, 63,
	  GRAPHIRE, WACOM_GRAPHIRE_RES, WACOM_GRAPHIRE_RES };
static const struct wacom_features wacom_features_0x15 =
	{ "Wacom Graphire4 4x5", 10208, 7424, 511, 63,
	  WACOM_G4, WACOM_GRAPHIRE_RES, WACOM_GRAPHIRE_RES };
static const struct wacom_features wacom_features_0x16 =
	{ "Wacom Graphire4 6x8", 16704, 12064, 511, 63,
	  WACOM_G4, WACOM_GRAPHIRE_RES, WACOM_GRAPHIRE_RES };
static const struct wacom_features wacom_features_0x17 =
	{ "Wacom BambooFun 4x5", 14760, 9225, 511, 63,
	  WACOM_MO, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x18 =
	{ "Wacom BambooFun 6x8", 21648, 13530, 511, 63,
	  WACOM_MO, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x19 =
	{ "Wacom Bamboo1 Medium", 16704, 12064, 511, 63,
	  GRAPHIRE, WACOM_GRAPHIRE_RES, WACOM_GRAPHIRE_RES };
static const struct wacom_features wacom_features_0x60 =
	{ "Wacom Volito", 5104, 3712, 511, 63,
	  GRAPHIRE, WACOM_VOLITO_RES, WACOM_VOLITO_RES };
static const struct wacom_features wacom_features_0x61 =
	{ "Wacom PenStation2", 3250, 2320, 255, 63,
	  GRAPHIRE, WACOM_VOLITO_RES, WACOM_VOLITO_RES };
static const struct wacom_features wacom_features_0x62 =
	{ "Wacom Volito2 4x5", 5104, 3712, 511, 63,
	  GRAPHIRE, WACOM_VOLITO_RES, WACOM_VOLITO_RES };
static const struct wacom_features wacom_features_0x63 =
	{ "Wacom Volito2 2x3", 3248, 2320, 511, 63,
	  GRAPHIRE, WACOM_VOLITO_RES, WACOM_VOLITO_RES };
static const struct wacom_features wacom_features_0x64 =
	{ "Wacom PenPartner2", 3250, 2320, 511, 63,
	  GRAPHIRE, WACOM_VOLITO_RES, WACOM_VOLITO_RES };
static const struct wacom_features wacom_features_0x65 =
	{ "Wacom Bamboo", 14760, 9225, 511, 63,
	  WACOM_MO, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x69 =
	{ "Wacom Bamboo1", 5104, 3712, 511, 63,
	  GRAPHIRE, WACOM_PENPRTN_RES, WACOM_PENPRTN_RES };
static const struct wacom_features wacom_features_0x6A =
	{ "Wacom Bamboo1 4x6", 14760, 9225, 1023, 63,
	  GRAPHIRE, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x6B =
	{ "Wacom Bamboo1 5x8", 21648, 13530, 1023, 63,
	  GRAPHIRE, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x20 =
	{ "Wacom Intuos 4x5", 12700, 10600, 1023, 31,
	  INTUOS, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x21 =
	{ "Wacom Intuos 6x8", 20320, 16240, 1023, 31,
	  INTUOS, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x22 =
	{ "Wacom Intuos 9x12", 30480, 24060, 1023, 31,
	  INTUOS, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x23 =
	{ "Wacom Intuos 12x12", 30480, 31680, 1023, 31,
	  INTUOS, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x24 =
	{ "Wacom Intuos 12x18", 45720, 31680, 1023, 31,
	  INTUOS, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x30 =
	{ "Wacom PL400", 5408, 4056, 255, 0,
	  PL, WACOM_PL_RES, WACOM_PL_RES };
static const struct wacom_features wacom_features_0x31 =
	{ "Wacom PL500", 6144, 4608, 255, 0,
	  PL, WACOM_PL_RES, WACOM_PL_RES };
static const struct wacom_features wacom_features_0x32 =
	{ "Wacom PL600", 6126, 4604, 255, 0,
	  PL, WACOM_PL_RES, WACOM_PL_RES };
static const struct wacom_features wacom_features_0x33 =
	{ "Wacom PL600SX", 6260, 5016, 255, 0,
	  PL, WACOM_PL_RES, WACOM_PL_RES };
static const struct wacom_features wacom_features_0x34 =
	{ "Wacom PL550", 6144, 4608, 511, 0,
	  PL, WACOM_PL_RES, WACOM_PL_RES };
static const struct wacom_features wacom_features_0x35 =
	{ "Wacom PL800", 7220, 5780, 511, 0,
	  PL, WACOM_PL_RES, WACOM_PL_RES };
static const struct wacom_features wacom_features_0x37 =
	{ "Wacom PL700", 6758, 5406, 511, 0,
	  PL, WACOM_PL_RES, WACOM_PL_RES };
static const struct wacom_features wacom_features_0x38 =
	{ "Wacom PL510", 6282, 4762, 511, 0,
	  PL, WACOM_PL_RES, WACOM_PL_RES };
static const struct wacom_features wacom_features_0x39 =
	{ "Wacom DTU710", 34080, 27660, 511, 0,
	  PL, WACOM_PL_RES, WACOM_PL_RES };
static const struct wacom_features wacom_features_0xC4 =
	{ "Wacom DTF521", 6282, 4762, 511, 0,
	  PL, WACOM_PL_RES, WACOM_PL_RES };
static const struct wacom_features wacom_features_0xC0 =
	{ "Wacom DTF720", 6858, 5506, 511, 0,
	  PL, WACOM_PL_RES, WACOM_PL_RES };
static const struct wacom_features wacom_features_0xC2 =
	{ "Wacom DTF720a", 6858, 5506, 511, 0,
	  PL, WACOM_PL_RES, WACOM_PL_RES };
static const struct wacom_features wacom_features_0x03 =
	{ "Wacom Cintiq Partner", 20480, 15360, 511, 0,
	  PTU, WACOM_PL_RES, WACOM_PL_RES };
static const struct wacom_features wacom_features_0x41 =
	{ "Wacom Intuos2 4x5", 12700, 10600, 1023, 31,
	  INTUOS, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x42 =
	{ "Wacom Intuos2 6x8", 20320, 16240, 1023, 31,
	  INTUOS, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x43 =
	{ "Wacom Intuos2 9x12", 30480, 24060, 1023, 31,
	  INTUOS, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x44 =
	{ "Wacom Intuos2 12x12", 30480, 31680, 1023, 31,
	  INTUOS, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x45 =
	{ "Wacom Intuos2 12x18", 45720, 31680, 1023, 31,
	  INTUOS, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0xB0 =
	{ "Wacom Intuos3 4x5", 25400, 20320, 1023, 63,
	  INTUOS3S, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES, 4 };
static const struct wacom_features wacom_features_0xB1 =
	{ "Wacom Intuos3 6x8", 40640, 30480, 1023, 63,
	  INTUOS3, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES, 8 };
static const struct wacom_features wacom_features_0xB2 =
	{ "Wacom Intuos3 9x12", 60960, 45720, 1023, 63,
	  INTUOS3, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES, 8 };
static const struct wacom_features wacom_features_0xB3 =
	{ "Wacom Intuos3 12x12", 60960, 60960, 1023, 63,
	  INTUOS3L, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES, 8 };
static const struct wacom_features wacom_features_0xB4 =
	{ "Wacom Intuos3 12x19", 97536, 60960, 1023, 63,
	  INTUOS3L, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES, 8 };
static const struct wacom_features wacom_features_0xB5 =
	{ "Wacom Intuos3 6x11", 54204, 31750, 1023, 63,
	  INTUOS3, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES, 8 };
static const struct wacom_features wacom_features_0xB7 =
	{ "Wacom Intuos3 4x6", 31496, 19685, 1023, 63,
	  INTUOS3S, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES, 4 };
static const struct wacom_features wacom_features_0xB8 =
	{ "Wacom Intuos4 4x6", 31496, 19685, 2047, 63,
	  INTUOS4S, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES, 7 };
static const struct wacom_features wacom_features_0xB9 =
	{ "Wacom Intuos4 6x9", 44704, 27940, 2047, 63,
	  INTUOS4, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES, 9 };
static const struct wacom_features wacom_features_0xBA =
	{ "Wacom Intuos4 8x13", 65024, 40640, 2047, 63,
	  INTUOS4L, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES, 9 };
static const struct wacom_features wacom_features_0xBB =
	{ "Wacom Intuos4 12x19", 97536, 60960, 2047, 63,
	  INTUOS4L, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES, 9 };
static const struct wacom_features wacom_features_0xBC =
	{ "Wacom Intuos4 WL", 40640, 25400, 2047, 63,
	  INTUOS4, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES, 9 };
static const struct wacom_features wacom_features_0xBD =
	{ "Wacom Intuos4 WL", 40640, 25400, 2047, 63,
	  INTUOS4WL, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES, 9 };
static const struct wacom_features wacom_features_0x26 =
	{ "Wacom Intuos5 touch S", 31496, 19685, 2047, 63,
	  INTUOS5S, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES, 7, .touch_max = 16 };
static const struct wacom_features wacom_features_0x27 =
	{ "Wacom Intuos5 touch M", 44704, 27940, 2047, 63,
	  INTUOS5, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES, 9, .touch_max = 16 };
static const struct wacom_features wacom_features_0x28 =
	{ "Wacom Intuos5 touch L", 65024, 40640, 2047, 63,
	  INTUOS5L, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES, 9, .touch_max = 16 };
static const struct wacom_features wacom_features_0x29 =
	{ "Wacom Intuos5 S", 31496, 19685, 2047, 63,
	  INTUOS5S, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES, 7 };
static const struct wacom_features wacom_features_0x2A =
	{ "Wacom Intuos5 M", 44704, 27940, 2047, 63,
	  INTUOS5, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES, 9 };
static const struct wacom_features wacom_features_0x314 =
	{ "Wacom Intuos Pro S", 31496, 19685, 2047, 63,
	  INTUOSPS, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES, 7, .touch_max = 16,
	  .check_for_hid_type = true, .hid_type = HID_TYPE_USBNONE };
static const struct wacom_features wacom_features_0x315 =
	{ "Wacom Intuos Pro M", 44704, 27940, 2047, 63,
	  INTUOSPM, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES, 9, .touch_max = 16,
	  .check_for_hid_type = true, .hid_type = HID_TYPE_USBNONE };
static const struct wacom_features wacom_features_0x317 =
	{ "Wacom Intuos Pro L", 65024, 40640, 2047, 63,
	  INTUOSPL, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES, 9, .touch_max = 16,
	  .check_for_hid_type = true, .hid_type = HID_TYPE_USBNONE };
static const struct wacom_features wacom_features_0xF4 =
	{ "Wacom Cintiq 24HD", 104480, 65600, 2047, 63,
	  WACOM_24HD, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES, 16,
	  WACOM_CINTIQ_OFFSET, WACOM_CINTIQ_OFFSET,
	  WACOM_CINTIQ_OFFSET, WACOM_CINTIQ_OFFSET };
static const struct wacom_features wacom_features_0xF8 =
	{ "Wacom Cintiq 24HD touch", 104480, 65600, 2047, 63, /* Pen */
	  WACOM_24HD, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES, 16,
	  WACOM_CINTIQ_OFFSET, WACOM_CINTIQ_OFFSET,
	  WACOM_CINTIQ_OFFSET, WACOM_CINTIQ_OFFSET,
	  .oVid = USB_VENDOR_ID_WACOM, .oPid = 0xf6 };
static const struct wacom_features wacom_features_0xF6 =
	{ "Wacom Cintiq 24HD touch", .type = WACOM_24HDT, /* Touch */
	  .oVid = USB_VENDOR_ID_WACOM, .oPid = 0xf8, .touch_max = 10,
	  .check_for_hid_type = true, .hid_type = HID_TYPE_USBNONE };
static const struct wacom_features wacom_features_0x32A =
	{ "Wacom Cintiq 27QHD", 120140, 67920, 2047, 63,
	  WACOM_27QHD, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES, 0,
	  WACOM_CINTIQ_OFFSET, WACOM_CINTIQ_OFFSET,
	  WACOM_CINTIQ_OFFSET, WACOM_CINTIQ_OFFSET };
static const struct wacom_features wacom_features_0x32B =
	{ "Wacom Cintiq 27QHD touch", 120140, 67920, 2047, 63,
	  WACOM_27QHD, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES, 0,
	  WACOM_CINTIQ_OFFSET, WACOM_CINTIQ_OFFSET,
	  WACOM_CINTIQ_OFFSET, WACOM_CINTIQ_OFFSET,
	  .oVid = USB_VENDOR_ID_WACOM, .oPid = 0x32C };
static const struct wacom_features wacom_features_0x32C =
	{ "Wacom Cintiq 27QHD touch", .type = WACOM_27QHDT,
	  .oVid = USB_VENDOR_ID_WACOM, .oPid = 0x32B, .touch_max = 10 };
static const struct wacom_features wacom_features_0x3F =
	{ "Wacom Cintiq 21UX", 87200, 65600, 1023, 63,
	  CINTIQ, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES, 8 };
static const struct wacom_features wacom_features_0xC5 =
	{ "Wacom Cintiq 20WSX", 86680, 54180, 1023, 63,
	  WACOM_BEE, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES, 10 };
static const struct wacom_features wacom_features_0xC6 =
	{ "Wacom Cintiq 12WX", 53020, 33440, 1023, 63,
	  WACOM_BEE, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES, 10 };
static const struct wacom_features wacom_features_0x304 =
	{ "Wacom Cintiq 13HD", 59552, 33848, 1023, 63,
	  WACOM_13HD, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES, 9,
	  WACOM_CINTIQ_OFFSET, WACOM_CINTIQ_OFFSET,
	  WACOM_CINTIQ_OFFSET, WACOM_CINTIQ_OFFSET };
static const struct wacom_features wacom_features_0x333 =
	{ "Wacom Cintiq 13HD touch", 59552, 33848, 2047, 63,
	  WACOM_13HD, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES, 9,
	  WACOM_CINTIQ_OFFSET, WACOM_CINTIQ_OFFSET,
	  WACOM_CINTIQ_OFFSET, WACOM_CINTIQ_OFFSET,
	  .oVid = USB_VENDOR_ID_WACOM, .oPid = 0x335 };
static const struct wacom_features wacom_features_0x335 =
	{ "Wacom Cintiq 13HD touch", .type = WACOM_24HDT, /* Touch */
	  .oVid = USB_VENDOR_ID_WACOM, .oPid = 0x333, .touch_max = 10,
	  .check_for_hid_type = true, .hid_type = HID_TYPE_USBNONE };
static const struct wacom_features wacom_features_0xC7 =
	{ "Wacom DTU1931", 37832, 30305, 511, 0,
	  PL, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0xCE =
	{ "Wacom DTU2231", 47864, 27011, 511, 0,
	  DTU, WACOM_INTUOS_RES, WACOM_INTUOS_RES,
	  .check_for_hid_type = true, .hid_type = HID_TYPE_USBMOUSE };
static const struct wacom_features wacom_features_0xF0 =
	{ "Wacom DTU1631", 34623, 19553, 511, 0,
	  DTU, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0xFB =
	{ "Wacom DTU1031", 22096, 13960, 511, 0,
	  DTUS, WACOM_INTUOS_RES, WACOM_INTUOS_RES, 4,
	  WACOM_DTU_OFFSET, WACOM_DTU_OFFSET,
	  WACOM_DTU_OFFSET, WACOM_DTU_OFFSET };
static const struct wacom_features wacom_features_0x32F =
	{ "Wacom DTU1031X", 22672, 12928, 511, 0,
	  DTUSX, WACOM_INTUOS_RES, WACOM_INTUOS_RES, 0,
	  WACOM_DTU_OFFSET, WACOM_DTU_OFFSET,
	  WACOM_DTU_OFFSET, WACOM_DTU_OFFSET };
static const struct wacom_features wacom_features_0x336 =
	{ "Wacom DTU1141", 23672, 13403, 1023, 0,
	  DTUS, WACOM_INTUOS_RES, WACOM_INTUOS_RES, 4,
	  WACOM_DTU_OFFSET, WACOM_DTU_OFFSET,
	  WACOM_DTU_OFFSET, WACOM_DTU_OFFSET };
static const struct wacom_features wacom_features_0x57 =
	{ "Wacom DTK2241", 95840, 54260, 2047, 63,
	  DTK, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES, 6,
	  WACOM_CINTIQ_OFFSET, WACOM_CINTIQ_OFFSET,
	  WACOM_CINTIQ_OFFSET, WACOM_CINTIQ_OFFSET };
static const struct wacom_features wacom_features_0x59 = /* Pen */
	{ "Wacom DTH2242", 95840, 54260, 2047, 63,
	  DTK, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES, 6,
	  WACOM_CINTIQ_OFFSET, WACOM_CINTIQ_OFFSET,
	  WACOM_CINTIQ_OFFSET, WACOM_CINTIQ_OFFSET,
	  .oVid = USB_VENDOR_ID_WACOM, .oPid = 0x5D };
static const struct wacom_features wacom_features_0x5D = /* Touch */
	{ "Wacom DTH2242",       .type = WACOM_24HDT,
	  .oVid = USB_VENDOR_ID_WACOM, .oPid = 0x59, .touch_max = 10,
	  .check_for_hid_type = true, .hid_type = HID_TYPE_USBNONE };
static const struct wacom_features wacom_features_0xCC =
	{ "Wacom Cintiq 21UX2", 87200, 65600, 2047, 63,
	  WACOM_21UX2, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES, 18,
	  WACOM_CINTIQ_OFFSET, WACOM_CINTIQ_OFFSET,
	  WACOM_CINTIQ_OFFSET, WACOM_CINTIQ_OFFSET };
static const struct wacom_features wacom_features_0xFA =
	{ "Wacom Cintiq 22HD", 95840, 54260, 2047, 63,
	  WACOM_22HD, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES, 18,
	  WACOM_CINTIQ_OFFSET, WACOM_CINTIQ_OFFSET,
	  WACOM_CINTIQ_OFFSET, WACOM_CINTIQ_OFFSET };
static const struct wacom_features wacom_features_0x5B =
	{ "Wacom Cintiq 22HDT", 95840, 54260, 2047, 63,
	  WACOM_22HD, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES, 18,
	  WACOM_CINTIQ_OFFSET, WACOM_CINTIQ_OFFSET,
	  WACOM_CINTIQ_OFFSET, WACOM_CINTIQ_OFFSET,
	  .oVid = USB_VENDOR_ID_WACOM, .oPid = 0x5e };
static const struct wacom_features wacom_features_0x5E =
	{ "Wacom Cintiq 22HDT", .type = WACOM_24HDT,
	  .oVid = USB_VENDOR_ID_WACOM, .oPid = 0x5b, .touch_max = 10,
	  .check_for_hid_type = true, .hid_type = HID_TYPE_USBNONE };
static const struct wacom_features wacom_features_0x90 =
	{ "Wacom ISDv4 90", 26202, 16325, 255, 0,
	  TABLETPC, WACOM_INTUOS_RES, WACOM_INTUOS_RES }; /* Pen-only */
static const struct wacom_features wacom_features_0x93 =
	{ "Wacom ISDv4 93", 26202, 16325, 255, 0,
	  TABLETPC, WACOM_INTUOS_RES, WACOM_INTUOS_RES, .touch_max = 1 };
static const struct wacom_features wacom_features_0x97 =
	{ "Wacom ISDv4 97", 26202, 16325, 511, 0,
	  TABLETPC, WACOM_INTUOS_RES, WACOM_INTUOS_RES }; /* Pen-only */
static const struct wacom_features wacom_features_0x9A =
	{ "Wacom ISDv4 9A", 26202, 16325, 255, 0,
	  TABLETPC, WACOM_INTUOS_RES, WACOM_INTUOS_RES, .touch_max = 1 };
static const struct wacom_features wacom_features_0x9F =
	{ "Wacom ISDv4 9F", 26202, 16325, 255, 0,
	  TABLETPC, WACOM_INTUOS_RES, WACOM_INTUOS_RES, .touch_max = 1 };
static const struct wacom_features wacom_features_0xE2 =
	{ "Wacom ISDv4 E2", 26202, 16325, 255, 0,
	  TABLETPC2FG, WACOM_INTUOS_RES, WACOM_INTUOS_RES, .touch_max = 2 };
static const struct wacom_features wacom_features_0xE3 =
	{ "Wacom ISDv4 E3", 26202, 16325, 255, 0,
	  TABLETPC2FG, WACOM_INTUOS_RES, WACOM_INTUOS_RES, .touch_max = 2 };
static const struct wacom_features wacom_features_0xE5 =
	{ "Wacom ISDv4 E5", 26202, 16325, 255, 0,
	  MTSCREEN, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0xE6 =
	{ "Wacom ISDv4 E6", 27760, 15694, 255, 0,
	  TABLETPC2FG, WACOM_INTUOS_RES, WACOM_INTUOS_RES, .touch_max = 2 };
static const struct wacom_features wacom_features_0xEC =
	{ "Wacom ISDv4 EC", 25710, 14500, 255, 0,
	  TABLETPC,    WACOM_INTUOS_RES, WACOM_INTUOS_RES }; /* Pen-only */
static const struct wacom_features wacom_features_0xED =
	{ "Wacom ISDv4 ED", 26202, 16325, 255, 0,
	  TABLETPCE, WACOM_INTUOS_RES, WACOM_INTUOS_RES, .touch_max = 1 };
static const struct wacom_features wacom_features_0xEF =
	{ "Wacom ISDv4 EF", 26202, 16325, 255, 0,
	  TABLETPC, WACOM_INTUOS_RES, WACOM_INTUOS_RES }; /* Pen-only */
static const struct wacom_features wacom_features_0x100 =
	{ "Wacom ISDv4 100", 26202, 16325, 255, 0,
	  MTTPC, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x101 =
	{ "Wacom ISDv4 101", 26202, 16325, 255, 0,
	  MTTPC, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x10D =
	{ "Wacom ISDv4 10D", 26202, 16325, 255, 0,
	  MTTPC, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x10E =
	{ "Wacom ISDv4 10E", 27760, 15694, 255, 0,
	  MTTPC, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x10F =
	{ "Wacom ISDv4 10F", 27760, 15694, 255, 0,
	  MTTPC, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x116 =
	{ "Wacom ISDv4 116", 26202, 16325, 255, 0,
	  TABLETPCE, WACOM_INTUOS_RES, WACOM_INTUOS_RES, .touch_max = 1 };
static const struct wacom_features wacom_features_0x12C =
	{ "Wacom ISDv4 12C", 27848, 15752, 2047, 0,
	  TABLETPC, WACOM_INTUOS_RES, WACOM_INTUOS_RES }; /* Pen-only */
static const struct wacom_features wacom_features_0x4001 =
	{ "Wacom ISDv4 4001", 26202, 16325, 255, 0,
	  MTTPC, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x4004 =
	{ "Wacom ISDv4 4004", 11060, 6220, 255, 0,
	  MTTPC_B, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x5000 =
	{ "Wacom ISDv4 5000", 27848, 15752, 1023, 0,
	  MTTPC_B, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x5002 =
	{ "Wacom ISDv4 5002", 29576, 16724, 1023, 0,
	  MTTPC_B, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x47 =
	{ "Wacom Intuos2 6x8", 20320, 16240, 1023, 31,
	  INTUOS, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x84 =
	{ "Wacom Wireless Receiver", .type = WIRELESS, .touch_max = 16 };
static const struct wacom_features wacom_features_0xD0 =
	{ "Wacom Bamboo 2FG", 14720, 9200, 1023, 31,
	  BAMBOO_TOUCH, WACOM_INTUOS_RES, WACOM_INTUOS_RES, .touch_max = 2 };
static const struct wacom_features wacom_features_0xD1 =
	{ "Wacom Bamboo 2FG 4x5", 14720, 9200, 1023, 31,
	  BAMBOO_PT, WACOM_INTUOS_RES, WACOM_INTUOS_RES, .touch_max = 2 };
static const struct wacom_features wacom_features_0xD2 =
	{ "Wacom Bamboo Craft", 14720, 9200, 1023, 31,
	  BAMBOO_PT, WACOM_INTUOS_RES, WACOM_INTUOS_RES, .touch_max = 2 };
static const struct wacom_features wacom_features_0xD3 =
	{ "Wacom Bamboo 2FG 6x8", 21648, 13700, 1023, 31,
	  BAMBOO_PT, WACOM_INTUOS_RES, WACOM_INTUOS_RES, .touch_max = 2 };
static const struct wacom_features wacom_features_0xD4 =
	{ "Wacom Bamboo Pen", 14720, 9200, 1023, 31,
	  BAMBOO_PEN, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0xD5 =
	{ "Wacom Bamboo Pen 6x8", 21648, 13700, 1023, 31,
	  BAMBOO_PEN, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0xD6 =
	{ "Wacom BambooPT 2FG 4x5", 14720, 9200, 1023, 31,
	  BAMBOO_PT, WACOM_INTUOS_RES, WACOM_INTUOS_RES, .touch_max = 2 };
static const struct wacom_features wacom_features_0xD7 =
	{ "Wacom BambooPT 2FG Small", 14720, 9200, 1023, 31,
	  BAMBOO_PT, WACOM_INTUOS_RES, WACOM_INTUOS_RES, .touch_max = 2 };
static const struct wacom_features wacom_features_0xD8 =
	{ "Wacom Bamboo Comic 2FG", 21648, 13700, 1023, 31,
	  BAMBOO_PT, WACOM_INTUOS_RES, WACOM_INTUOS_RES, .touch_max = 2 };
static const struct wacom_features wacom_features_0xDA =
	{ "Wacom Bamboo 2FG 4x5 SE", 14720, 9200, 1023, 31,
	  BAMBOO_PT, WACOM_INTUOS_RES, WACOM_INTUOS_RES, .touch_max = 2 };
static const struct wacom_features wacom_features_0xDB =
	{ "Wacom Bamboo 2FG 6x8 SE", 21648, 13700, 1023, 31,
	  BAMBOO_PT, WACOM_INTUOS_RES, WACOM_INTUOS_RES, .touch_max = 2 };
static const struct wacom_features wacom_features_0xDD =
        { "Wacom Bamboo Connect", 14720, 9200, 1023, 31,
          BAMBOO_PT, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0xDE =
        { "Wacom Bamboo 16FG 4x5", 14720, 9200, 1023, 31,
	  BAMBOO_PT, WACOM_INTUOS_RES, WACOM_INTUOS_RES, .touch_max = 16 };
static const struct wacom_features wacom_features_0xDF =
        { "Wacom Bamboo 16FG 6x8", 21648, 13700, 1023, 31,
	  BAMBOO_PT, WACOM_INTUOS_RES, WACOM_INTUOS_RES, .touch_max = 16 };
static const struct wacom_features wacom_features_0x300 =
	{ "Wacom Bamboo One S", 14720, 9225, 1023, 31,
	  BAMBOO_PEN, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x301 =
	{ "Wacom Bamboo One M", 21648, 13530, 1023, 31,
	  BAMBOO_PEN, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x302 =
	{ "Wacom Intuos PT S", 15200, 9500, 1023, 31,
	  INTUOSHT, WACOM_INTUOS_RES, WACOM_INTUOS_RES, .touch_max = 16,
	  .check_for_hid_type = true, .hid_type = HID_TYPE_USBNONE };
static const struct wacom_features wacom_features_0x303 =
	{ "Wacom Intuos PT M", 21600, 13500, 1023, 31,
	  INTUOSHT, WACOM_INTUOS_RES, WACOM_INTUOS_RES, .touch_max = 16,
	  .check_for_hid_type = true, .hid_type = HID_TYPE_USBNONE };
static const struct wacom_features wacom_features_0x30E =
	{ "Wacom Intuos S", 15200, 9500, 1023, 31,
	  INTUOSHT, WACOM_INTUOS_RES, WACOM_INTUOS_RES,
	  .check_for_hid_type = true, .hid_type = HID_TYPE_USBNONE };
static const struct wacom_features wacom_features_0x6004 =
	{ "ISD-V4", 12800, 8000, 255, 0,
	  TABLETPC, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x307 =
	{ "Wacom ISDv5 307", 59552, 33848, 2047, 63,
	  CINTIQ_HYBRID, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES, 9,
	  WACOM_CINTIQ_OFFSET, WACOM_CINTIQ_OFFSET,
	  WACOM_CINTIQ_OFFSET, WACOM_CINTIQ_OFFSET,
	  .oVid = USB_VENDOR_ID_WACOM, .oPid = 0x309 };
static const struct wacom_features wacom_features_0x309 =
	{ "Wacom ISDv5 309", .type = WACOM_24HDT, /* Touch */
	  .oVid = USB_VENDOR_ID_WACOM, .oPid = 0x0307, .touch_max = 10,
	  .check_for_hid_type = true, .hid_type = HID_TYPE_USBNONE };
static const struct wacom_features wacom_features_0x30A =
	{ "Wacom ISDv5 30A", 59552, 33848, 2047, 63,
	  CINTIQ_HYBRID, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES, 9,
	  WACOM_CINTIQ_OFFSET, WACOM_CINTIQ_OFFSET,
	  WACOM_CINTIQ_OFFSET, WACOM_CINTIQ_OFFSET,
	  .oVid = USB_VENDOR_ID_WACOM, .oPid = 0x30C };
static const struct wacom_features wacom_features_0x30C =
	{ "Wacom ISDv5 30C", .type = WACOM_24HDT, /* Touch */
	  .oVid = USB_VENDOR_ID_WACOM, .oPid = 0x30A, .touch_max = 10,
	  .check_for_hid_type = true, .hid_type = HID_TYPE_USBNONE };
static const struct wacom_features wacom_features_0x318 =
	{ "Wacom USB Bamboo PAD", 4095, 4095, /* Touch */
	  .type = BAMBOO_PAD, 35, 48, .touch_max = 4 };
static const struct wacom_features wacom_features_0x319 =
	{ "Wacom Wireless Bamboo PAD", 4095, 4095, /* Touch */
	  .type = BAMBOO_PAD, 35, 48, .touch_max = 4 };
static const struct wacom_features wacom_features_0x325 =
	{ "Wacom ISDv5 325", 59552, 33848, 2047, 63,
	  CINTIQ_COMPANION_2, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES, 11,
	  WACOM_CINTIQ_OFFSET, WACOM_CINTIQ_OFFSET,
	  WACOM_CINTIQ_OFFSET, WACOM_CINTIQ_OFFSET,
	  .oVid = USB_VENDOR_ID_WACOM, .oPid = 0x326 };
static const struct wacom_features wacom_features_0x326 = /* Touch */
	{ "Wacom ISDv5 326", .type = HID_GENERIC, .oVid = USB_VENDOR_ID_WACOM,
	  .oPid = 0x325 };
static const struct wacom_features wacom_features_0x323 =
	{ "Wacom Intuos P M", 21600, 13500, 1023, 31,
	  INTUOSHT, WACOM_INTUOS_RES, WACOM_INTUOS_RES,
	  .check_for_hid_type = true, .hid_type = HID_TYPE_USBNONE };
static const struct wacom_features wacom_features_0x331 =
	{ "Wacom Express Key Remote", .type = REMOTE,
	  .numbered_buttons = 18, .check_for_hid_type = true,
	  .hid_type = HID_TYPE_USBNONE };
static const struct wacom_features wacom_features_0x33B =
	{ "Wacom Intuos S 2", 15200, 9500, 2047, 63,
	  INTUOSHT2, WACOM_INTUOS_RES, WACOM_INTUOS_RES,
	  .check_for_hid_type = true, .hid_type = HID_TYPE_USBNONE };
static const struct wacom_features wacom_features_0x33C =
	{ "Wacom Intuos PT S 2", 15200, 9500, 2047, 63,
	  INTUOSHT2, WACOM_INTUOS_RES, WACOM_INTUOS_RES, .touch_max = 16,
	  .check_for_hid_type = true, .hid_type = HID_TYPE_USBNONE };
static const struct wacom_features wacom_features_0x33D =
	{ "Wacom Intuos P M 2", 21600, 13500, 2047, 63,
	  INTUOSHT2, WACOM_INTUOS_RES, WACOM_INTUOS_RES,
	  .check_for_hid_type = true, .hid_type = HID_TYPE_USBNONE };
static const struct wacom_features wacom_features_0x33E =
	{ "Wacom Intuos PT M 2", 21600, 13500, 2047, 63,
	  INTUOSHT2, WACOM_INTUOS_RES, WACOM_INTUOS_RES, .touch_max = 16,
	  .check_for_hid_type = true, .hid_type = HID_TYPE_USBNONE };
static const struct wacom_features wacom_features_0x343 =
	{ "Wacom DTK1651", 34816, 19759, 1023, 0,
	  DTUS, WACOM_INTUOS_RES, WACOM_INTUOS_RES, 4,
	  WACOM_DTU_OFFSET, WACOM_DTU_OFFSET,
	  WACOM_DTU_OFFSET, WACOM_DTU_OFFSET };
static const struct wacom_features wacom_features_0x360 =
	{ "Wacom Intuos Pro M", 44800, 29600, 8191, 63,
	  INTUOSP2_BT, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES, 9, .touch_max = 10 };
static const struct wacom_features wacom_features_0x361 =
	{ "Wacom Intuos Pro L", 62200, 43200, 8191, 63,
	  INTUOSP2_BT, WACOM_INTUOS3_RES, WACOM_INTUOS3_RES, 9, .touch_max = 10 };
static const struct wacom_features wacom_features_0x377 =
	{ "Wacom Intuos BT S", 15200, 9500, 4095, 63,
	  INTUOSHT3_BT, WACOM_INTUOS_RES, WACOM_INTUOS_RES, 4 };
static const struct wacom_features wacom_features_0x379 =
	{ "Wacom Intuos BT M", 21600, 13500, 4095, 63,
	  INTUOSHT3_BT, WACOM_INTUOS_RES, WACOM_INTUOS_RES, 4 };
static const struct wacom_features wacom_features_0x37A =
	{ "Wacom One by Wacom S", 15200, 9500, 2047, 63,
	  BAMBOO_PEN, WACOM_INTUOS_RES, WACOM_INTUOS_RES };
static const struct wacom_features wacom_features_0x37B =
	{ "Wacom One by Wacom M", 21600, 13500, 2047, 63,
	  BAMBOO_PEN, WACOM_INTUOS_RES, WACOM_INTUOS_RES };

static const struct wacom_features wacom_features_HID_ANY_ID =
	{ "Wacom HID", .type = HID_GENERIC, .oVid = HID_ANY_ID, .oPid = HID_ANY_ID };

#define USB_DEVICE_WACOM(prod)						\
	HID_DEVICE(BUS_USB, HID_GROUP_WACOM, USB_VENDOR_ID_WACOM, prod),\
	.driver_data = (kernel_ulong_t)&wacom_features_##prod

#define BT_DEVICE_WACOM(prod)						\
	HID_DEVICE(BUS_BLUETOOTH, HID_GROUP_WACOM, USB_VENDOR_ID_WACOM, prod),\
	.driver_data = (kernel_ulong_t)&wacom_features_##prod

#define I2C_DEVICE_WACOM(prod)						\
	HID_DEVICE(BUS_I2C, HID_GROUP_WACOM, USB_VENDOR_ID_WACOM, prod),\
	.driver_data = (kernel_ulong_t)&wacom_features_##prod

#define USB_DEVICE_LENOVO(prod)					\
	HID_USB_DEVICE(USB_VENDOR_ID_LENOVO, prod),			\
	.driver_data = (kernel_ulong_t)&wacom_features_##prod

const struct hid_device_id wacom_ids[] = {
	{ USB_DEVICE_WACOM(0x00) },
	{ USB_DEVICE_WACOM(0x03) },
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
	{ USB_DEVICE_WACOM(0x20) },
	{ USB_DEVICE_WACOM(0x21) },
	{ USB_DEVICE_WACOM(0x22) },
	{ USB_DEVICE_WACOM(0x23) },
	{ USB_DEVICE_WACOM(0x24) },
	{ USB_DEVICE_WACOM(0x26) },
	{ USB_DEVICE_WACOM(0x27) },
	{ USB_DEVICE_WACOM(0x28) },
	{ USB_DEVICE_WACOM(0x29) },
	{ USB_DEVICE_WACOM(0x2A) },
	{ USB_DEVICE_WACOM(0x30) },
	{ USB_DEVICE_WACOM(0x31) },
	{ USB_DEVICE_WACOM(0x32) },
	{ USB_DEVICE_WACOM(0x33) },
	{ USB_DEVICE_WACOM(0x34) },
	{ USB_DEVICE_WACOM(0x35) },
	{ USB_DEVICE_WACOM(0x37) },
	{ USB_DEVICE_WACOM(0x38) },
	{ USB_DEVICE_WACOM(0x39) },
	{ USB_DEVICE_WACOM(0x3F) },
	{ USB_DEVICE_WACOM(0x41) },
	{ USB_DEVICE_WACOM(0x42) },
	{ USB_DEVICE_WACOM(0x43) },
	{ USB_DEVICE_WACOM(0x44) },
	{ USB_DEVICE_WACOM(0x45) },
	{ USB_DEVICE_WACOM(0x47) },
	{ USB_DEVICE_WACOM(0x57) },
	{ USB_DEVICE_WACOM(0x59) },
	{ USB_DEVICE_WACOM(0x5B) },
	{ USB_DEVICE_WACOM(0x5D) },
	{ USB_DEVICE_WACOM(0x5E) },
	{ USB_DEVICE_WACOM(0x60) },
	{ USB_DEVICE_WACOM(0x61) },
	{ USB_DEVICE_WACOM(0x62) },
	{ USB_DEVICE_WACOM(0x63) },
	{ USB_DEVICE_WACOM(0x64) },
	{ USB_DEVICE_WACOM(0x65) },
	{ USB_DEVICE_WACOM(0x69) },
	{ USB_DEVICE_WACOM(0x6A) },
	{ USB_DEVICE_WACOM(0x6B) },
	{ BT_DEVICE_WACOM(0x81) },
	{ USB_DEVICE_WACOM(0x84) },
	{ USB_DEVICE_WACOM(0x90) },
	{ USB_DEVICE_WACOM(0x93) },
	{ USB_DEVICE_WACOM(0x97) },
	{ USB_DEVICE_WACOM(0x9A) },
	{ USB_DEVICE_WACOM(0x9F) },
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
	{ BT_DEVICE_WACOM(0xBD) },
	{ USB_DEVICE_WACOM(0xC0) },
	{ USB_DEVICE_WACOM(0xC2) },
	{ USB_DEVICE_WACOM(0xC4) },
	{ USB_DEVICE_WACOM(0xC5) },
	{ USB_DEVICE_WACOM(0xC6) },
	{ USB_DEVICE_WACOM(0xC7) },
	{ USB_DEVICE_WACOM(0xCC) },
	{ USB_DEVICE_WACOM(0xCE) },
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
	{ USB_DEVICE_WACOM(0xE2) },
	{ USB_DEVICE_WACOM(0xE3) },
	{ USB_DEVICE_WACOM(0xE5) },
	{ USB_DEVICE_WACOM(0xE6) },
	{ USB_DEVICE_WACOM(0xEC) },
	{ USB_DEVICE_WACOM(0xED) },
	{ USB_DEVICE_WACOM(0xEF) },
	{ USB_DEVICE_WACOM(0xF0) },
	{ USB_DEVICE_WACOM(0xF4) },
	{ USB_DEVICE_WACOM(0xF6) },
	{ USB_DEVICE_WACOM(0xF8) },
	{ USB_DEVICE_WACOM(0xFA) },
	{ USB_DEVICE_WACOM(0xFB) },
	{ USB_DEVICE_WACOM(0x100) },
	{ USB_DEVICE_WACOM(0x101) },
	{ USB_DEVICE_WACOM(0x10D) },
	{ USB_DEVICE_WACOM(0x10E) },
	{ USB_DEVICE_WACOM(0x10F) },
	{ USB_DEVICE_WACOM(0x116) },
	{ USB_DEVICE_WACOM(0x12C) },
	{ USB_DEVICE_WACOM(0x300) },
	{ USB_DEVICE_WACOM(0x301) },
	{ USB_DEVICE_WACOM(0x302) },
	{ USB_DEVICE_WACOM(0x303) },
	{ USB_DEVICE_WACOM(0x304) },
	{ USB_DEVICE_WACOM(0x307) },
	{ USB_DEVICE_WACOM(0x309) },
	{ USB_DEVICE_WACOM(0x30A) },
	{ USB_DEVICE_WACOM(0x30C) },
	{ USB_DEVICE_WACOM(0x30E) },
	{ USB_DEVICE_WACOM(0x314) },
	{ USB_DEVICE_WACOM(0x315) },
	{ USB_DEVICE_WACOM(0x317) },
	{ USB_DEVICE_WACOM(0x318) },
	{ USB_DEVICE_WACOM(0x319) },
	{ USB_DEVICE_WACOM(0x323) },
	{ USB_DEVICE_WACOM(0x325) },
	{ USB_DEVICE_WACOM(0x326) },
	{ USB_DEVICE_WACOM(0x32A) },
	{ USB_DEVICE_WACOM(0x32B) },
	{ USB_DEVICE_WACOM(0x32C) },
	{ USB_DEVICE_WACOM(0x32F) },
	{ USB_DEVICE_WACOM(0x331) },
	{ USB_DEVICE_WACOM(0x333) },
	{ USB_DEVICE_WACOM(0x335) },
	{ USB_DEVICE_WACOM(0x336) },
	{ USB_DEVICE_WACOM(0x33B) },
	{ USB_DEVICE_WACOM(0x33C) },
	{ USB_DEVICE_WACOM(0x33D) },
	{ USB_DEVICE_WACOM(0x33E) },
	{ USB_DEVICE_WACOM(0x343) },
	{ BT_DEVICE_WACOM(0x360) },
	{ BT_DEVICE_WACOM(0x361) },
	{ BT_DEVICE_WACOM(0x377) },
	{ BT_DEVICE_WACOM(0x379) },
	{ USB_DEVICE_WACOM(0x37A) },
	{ USB_DEVICE_WACOM(0x37B) },
	{ USB_DEVICE_WACOM(0x4001) },
	{ USB_DEVICE_WACOM(0x4004) },
	{ USB_DEVICE_WACOM(0x5000) },
	{ USB_DEVICE_WACOM(0x5002) },
	{ USB_DEVICE_LENOVO(0x6004) },

	{ USB_DEVICE_WACOM(HID_ANY_ID) },
	{ I2C_DEVICE_WACOM(HID_ANY_ID) },
	{ BT_DEVICE_WACOM(HID_ANY_ID) },
	{ }
};
MODULE_DEVICE_TABLE(hid, wacom_ids);
