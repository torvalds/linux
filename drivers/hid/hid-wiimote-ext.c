/*
 * HID driver for Nintendo Wiimote extension devices
 * Copyright (c) 2011 David Herrmann
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/atomic.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include "hid-wiimote.h"

struct wiimote_ext {
	struct wiimote_data *wdata;
	struct work_struct worker;
	struct input_dev *input;
	struct input_dev *mp_input;

	atomic_t opened;
	atomic_t mp_opened;
	bool plugged;
	bool mp_plugged;
	bool motionp;
	__u8 ext_type;
	__u16 calib[4][3];
};

enum wiiext_type {
	WIIEXT_NONE,		/* placeholder */
	WIIEXT_CLASSIC,		/* Nintendo classic controller */
	WIIEXT_NUNCHUCK,	/* Nintendo nunchuck controller */
	WIIEXT_BALANCE_BOARD,	/* Nintendo balance board controller */
};

enum wiiext_keys {
	WIIEXT_KEY_C,
	WIIEXT_KEY_Z,
	WIIEXT_KEY_A,
	WIIEXT_KEY_B,
	WIIEXT_KEY_X,
	WIIEXT_KEY_Y,
	WIIEXT_KEY_ZL,
	WIIEXT_KEY_ZR,
	WIIEXT_KEY_PLUS,
	WIIEXT_KEY_MINUS,
	WIIEXT_KEY_HOME,
	WIIEXT_KEY_LEFT,
	WIIEXT_KEY_RIGHT,
	WIIEXT_KEY_UP,
	WIIEXT_KEY_DOWN,
	WIIEXT_KEY_LT,
	WIIEXT_KEY_RT,
	WIIEXT_KEY_COUNT
};

static __u16 wiiext_keymap[] = {
	BTN_C,		/* WIIEXT_KEY_C */
	BTN_Z,		/* WIIEXT_KEY_Z */
	BTN_A,		/* WIIEXT_KEY_A */
	BTN_B,		/* WIIEXT_KEY_B */
	BTN_X,		/* WIIEXT_KEY_X */
	BTN_Y,		/* WIIEXT_KEY_Y */
	BTN_TL2,	/* WIIEXT_KEY_ZL */
	BTN_TR2,	/* WIIEXT_KEY_ZR */
	KEY_NEXT,	/* WIIEXT_KEY_PLUS */
	KEY_PREVIOUS,	/* WIIEXT_KEY_MINUS */
	BTN_MODE,	/* WIIEXT_KEY_HOME */
	KEY_LEFT,	/* WIIEXT_KEY_LEFT */
	KEY_RIGHT,	/* WIIEXT_KEY_RIGHT */
	KEY_UP,		/* WIIEXT_KEY_UP */
	KEY_DOWN,	/* WIIEXT_KEY_DOWN */
	BTN_TL,		/* WIIEXT_KEY_LT */
	BTN_TR,		/* WIIEXT_KEY_RT */
};

/* disable all extensions */
static void ext_disable(struct wiimote_ext *ext)
{
	unsigned long flags;
	__u8 wmem = 0x55;

	if (!wiimote_cmd_acquire(ext->wdata)) {
		wiimote_cmd_write(ext->wdata, 0xa400f0, &wmem, sizeof(wmem));
		wiimote_cmd_release(ext->wdata);
	}

	spin_lock_irqsave(&ext->wdata->state.lock, flags);
	ext->motionp = false;
	ext->ext_type = WIIEXT_NONE;
	wiiproto_req_drm(ext->wdata, WIIPROTO_REQ_NULL);
	spin_unlock_irqrestore(&ext->wdata->state.lock, flags);
}

static bool motionp_read(struct wiimote_ext *ext)
{
	__u8 rmem[2], wmem;
	ssize_t ret;
	bool avail = false;

	if (!atomic_read(&ext->mp_opened))
		return false;

	if (wiimote_cmd_acquire(ext->wdata))
		return false;

	/* initialize motion plus */
	wmem = 0x55;
	ret = wiimote_cmd_write(ext->wdata, 0xa600f0, &wmem, sizeof(wmem));
	if (ret)
		goto error;

	/* read motion plus ID */
	ret = wiimote_cmd_read(ext->wdata, 0xa600fe, rmem, 2);
	if (ret == 2 || rmem[1] == 0x5)
		avail = true;

error:
	wiimote_cmd_release(ext->wdata);
	return avail;
}

static __u8 ext_read(struct wiimote_ext *ext)
{
	ssize_t ret;
	__u8 buf[24], i, j, offs = 0;
	__u8 rmem[2], wmem;
	__u8 type = WIIEXT_NONE;

	if (!ext->plugged || !atomic_read(&ext->opened))
		return WIIEXT_NONE;

	if (wiimote_cmd_acquire(ext->wdata))
		return WIIEXT_NONE;

	/* initialize extension */
	wmem = 0x55;
	ret = wiimote_cmd_write(ext->wdata, 0xa400f0, &wmem, sizeof(wmem));
	if (!ret) {
		/* disable encryption */
		wmem = 0x0;
		wiimote_cmd_write(ext->wdata, 0xa400fb, &wmem, sizeof(wmem));
	}

	/* read extension ID */
	ret = wiimote_cmd_read(ext->wdata, 0xa400fe, rmem, 2);
	if (ret == 2) {
		if (rmem[0] == 0 && rmem[1] == 0)
			type = WIIEXT_NUNCHUCK;
		else if (rmem[0] == 0x01 && rmem[1] == 0x01)
			type = WIIEXT_CLASSIC;
		else if (rmem[0] == 0x04 && rmem[1] == 0x02)
			type = WIIEXT_BALANCE_BOARD;
	}

	/* get balance board calibration data */
	if (type == WIIEXT_BALANCE_BOARD) {
		ret = wiimote_cmd_read(ext->wdata, 0xa40024, buf, 12);
		ret += wiimote_cmd_read(ext->wdata, 0xa40024 + 12,
					buf + 12, 12);

		if (ret != 24) {
			type = WIIEXT_NONE;
		} else {
			for (i = 0; i < 3; i++) {
				for (j = 0; j < 4; j++) {
					ext->calib[j][i] = buf[offs];
					ext->calib[j][i] <<= 8;
					ext->calib[j][i] |= buf[offs + 1];
					offs += 2;
				}
			}
		}
	}

	wiimote_cmd_release(ext->wdata);

	return type;
}

static void ext_enable(struct wiimote_ext *ext, bool motionp, __u8 ext_type)
{
	unsigned long flags;
	__u8 wmem;
	int ret;

	if (motionp) {
		if (wiimote_cmd_acquire(ext->wdata))
			return;

		if (ext_type == WIIEXT_CLASSIC)
			wmem = 0x07;
		else if (ext_type == WIIEXT_NUNCHUCK)
			wmem = 0x05;
		else
			wmem = 0x04;

		ret = wiimote_cmd_write(ext->wdata, 0xa600fe, &wmem, sizeof(wmem));
		wiimote_cmd_release(ext->wdata);
		if (ret)
			return;
	}

	spin_lock_irqsave(&ext->wdata->state.lock, flags);
	ext->motionp = motionp;
	ext->ext_type = ext_type;
	wiiproto_req_drm(ext->wdata, WIIPROTO_REQ_NULL);
	spin_unlock_irqrestore(&ext->wdata->state.lock, flags);
}

static void wiiext_worker(struct work_struct *work)
{
	struct wiimote_ext *ext = container_of(work, struct wiimote_ext,
									worker);
	bool motionp;
	__u8 ext_type;

	ext_disable(ext);
	motionp = motionp_read(ext);
	ext_type = ext_read(ext);
	ext_enable(ext, motionp, ext_type);
}

/* schedule work only once, otherwise mark for reschedule */
static void wiiext_schedule(struct wiimote_ext *ext)
{
	queue_work(system_nrt_wq, &ext->worker);
}

/*
 * Reacts on extension port events
 * Whenever the driver gets an event from the wiimote that an extension has been
 * plugged or unplugged, this funtion shall be called. It checks what extensions
 * are connected and initializes and activates them.
 * This can be called in atomic context. The initialization is done in a
 * separate worker thread. The state.lock spinlock must be held by the caller.
 */
void wiiext_event(struct wiimote_data *wdata, bool plugged)
{
	if (!wdata->ext)
		return;

	if (wdata->ext->plugged == plugged)
		return;

	wdata->ext->plugged = plugged;

	if (!plugged)
		wdata->ext->mp_plugged = false;

	/*
	 * We need to call wiiext_schedule(wdata->ext) here, however, the
	 * extension initialization logic is not fully understood and so
	 * automatic initialization is not supported, yet.
	 */
}

/*
 * Returns true if the current DRM mode should contain extension data and false
 * if there is no interest in extension data.
 * All supported extensions send 6 byte extension data so any DRM that contains
 * extension bytes is fine.
 * The caller must hold the state.lock spinlock.
 */
bool wiiext_active(struct wiimote_data *wdata)
{
	if (!wdata->ext)
		return false;

	return wdata->ext->motionp || wdata->ext->ext_type;
}

static void handler_motionp(struct wiimote_ext *ext, const __u8 *payload)
{
	__s32 x, y, z;
	bool plugged;

	/*        |   8    7    6    5    4    3 |  2  |  1  |
	 *   -----+------------------------------+-----+-----+
	 *    1   |               Yaw Speed <7:0>            |
	 *    2   |              Roll Speed <7:0>            |
	 *    3   |             Pitch Speed <7:0>            |
	 *   -----+------------------------------+-----+-----+
	 *    4   |       Yaw Speed <13:8>       | Yaw |Pitch|
	 *   -----+------------------------------+-----+-----+
	 *    5   |      Roll Speed <13:8>       |Roll | Ext |
	 *   -----+------------------------------+-----+-----+
	 *    6   |     Pitch Speed <13:8>       |  1  |  0  |
	 *   -----+------------------------------+-----+-----+
	 * The single bits Yaw, Roll, Pitch in the lower right corner specify
	 * whether the wiimote is rotating fast (0) or slow (1). Speed for slow
	 * roation is 440 deg/s and for fast rotation 2000 deg/s. To get a
	 * linear scale we multiply by 2000/440 = ~4.5454 which is 18 for fast
	 * and 9 for slow.
	 * If the wiimote is not rotating the sensor reports 2^13 = 8192.
	 * Ext specifies whether an extension is connected to the motionp.
	 */

	x = payload[0];
	y = payload[1];
	z = payload[2];

	x |= (((__u16)payload[3]) << 6) & 0xff00;
	y |= (((__u16)payload[4]) << 6) & 0xff00;
	z |= (((__u16)payload[5]) << 6) & 0xff00;

	x -= 8192;
	y -= 8192;
	z -= 8192;

	if (!(payload[3] & 0x02))
		x *= 18;
	else
		x *= 9;
	if (!(payload[4] & 0x02))
		y *= 18;
	else
		y *= 9;
	if (!(payload[3] & 0x01))
		z *= 18;
	else
		z *= 9;

	input_report_abs(ext->mp_input, ABS_RX, x);
	input_report_abs(ext->mp_input, ABS_RY, y);
	input_report_abs(ext->mp_input, ABS_RZ, z);
	input_sync(ext->mp_input);

	plugged = payload[5] & 0x01;
	if (plugged != ext->mp_plugged)
		ext->mp_plugged = plugged;
}

static void handler_nunchuck(struct wiimote_ext *ext, const __u8 *payload)
{
	__s16 x, y, z, bx, by;

	/*   Byte |   8    7 |  6    5 |  4    3 |  2 |  1  |
	 *   -----+----------+---------+---------+----+-----+
	 *    1   |              Button X <7:0>             |
	 *    2   |              Button Y <7:0>             |
	 *   -----+----------+---------+---------+----+-----+
	 *    3   |               Speed X <9:2>             |
	 *    4   |               Speed Y <9:2>             |
	 *    5   |               Speed Z <9:2>             |
	 *   -----+----------+---------+---------+----+-----+
	 *    6   | Z <1:0>  | Y <1:0> | X <1:0> | BC | BZ  |
	 *   -----+----------+---------+---------+----+-----+
	 * Button X/Y is the analog stick. Speed X, Y and Z are the
	 * accelerometer data in the same format as the wiimote's accelerometer.
	 * The 6th byte contains the LSBs of the accelerometer data.
	 * BC and BZ are the C and Z buttons: 0 means pressed
	 *
	 * If reported interleaved with motionp, then the layout changes. The
	 * 5th and 6th byte changes to:
	 *   -----+-----------------------------------+-----+
	 *    5   |            Speed Z <9:3>          | EXT |
	 *   -----+--------+-----+-----+----+----+----+-----+
	 *    6   |Z <2:1> |Y <1>|X <1>| BC | BZ | 0  |  0  |
	 *   -----+--------+-----+-----+----+----+----+-----+
	 * All three accelerometer values lose their LSB. The other data is
	 * still available but slightly moved.
	 *
	 * Center data for button values is 128. Center value for accelerometer
	 * values it 512 / 0x200
	 */

	bx = payload[0];
	by = payload[1];
	bx -= 128;
	by -= 128;

	x = payload[2] << 2;
	y = payload[3] << 2;
	z = payload[4] << 2;

	if (ext->motionp) {
		x |= (payload[5] >> 3) & 0x02;
		y |= (payload[5] >> 4) & 0x02;
		z &= ~0x4;
		z |= (payload[5] >> 5) & 0x06;
	} else {
		x |= (payload[5] >> 2) & 0x03;
		y |= (payload[5] >> 4) & 0x03;
		z |= (payload[5] >> 6) & 0x03;
	}

	x -= 0x200;
	y -= 0x200;
	z -= 0x200;

	input_report_abs(ext->input, ABS_HAT0X, bx);
	input_report_abs(ext->input, ABS_HAT0Y, by);

	input_report_abs(ext->input, ABS_RX, x);
	input_report_abs(ext->input, ABS_RY, y);
	input_report_abs(ext->input, ABS_RZ, z);

	if (ext->motionp) {
		input_report_key(ext->input,
			wiiext_keymap[WIIEXT_KEY_Z], !!(payload[5] & 0x04));
		input_report_key(ext->input,
			wiiext_keymap[WIIEXT_KEY_C], !!(payload[5] & 0x08));
	} else {
		input_report_key(ext->input,
			wiiext_keymap[WIIEXT_KEY_Z], !!(payload[5] & 0x01));
		input_report_key(ext->input,
			wiiext_keymap[WIIEXT_KEY_C], !!(payload[5] & 0x02));
	}

	input_sync(ext->input);
}

static void handler_classic(struct wiimote_ext *ext, const __u8 *payload)
{
	__s8 rx, ry, lx, ly, lt, rt;

	/*   Byte |  8  |  7  |  6  |  5  |  4  |  3  |  2  |  1  |
	 *   -----+-----+-----+-----+-----+-----+-----+-----+-----+
	 *    1   | RX <5:4>  |              LX <5:0>             |
	 *    2   | RX <3:2>  |              LY <5:0>             |
	 *   -----+-----+-----+-----+-----------------------------+
	 *    3   |RX<1>| LT <5:4>  |         RY <5:1>            |
	 *   -----+-----+-----------+-----------------------------+
	 *    4   |     LT <3:1>    |         RT <5:1>            |
	 *   -----+-----+-----+-----+-----+-----+-----+-----+-----+
	 *    5   | BDR | BDD | BLT | B-  | BH  | B+  | BRT |  1  |
	 *   -----+-----+-----+-----+-----+-----+-----+-----+-----+
	 *    6   | BZL | BB  | BY  | BA  | BX  | BZR | BDL | BDU |
	 *   -----+-----+-----+-----+-----+-----+-----+-----+-----+
	 * All buttons are 0 if pressed
	 * RX and RY are right analog stick
	 * LX and LY are left analog stick
	 * LT is left trigger, RT is right trigger
	 * BLT is 0 if left trigger is fully pressed
	 * BRT is 0 if right trigger is fully pressed
	 * BDR, BDD, BDL, BDU form the D-Pad with right, down, left, up buttons
	 * BZL is left Z button and BZR is right Z button
	 * B-, BH, B+ are +, HOME and - buttons
	 * BB, BY, BA, BX are A, B, X, Y buttons
	 * LSB of RX, RY, LT, and RT are not transmitted and always 0.
	 *
	 * With motionp enabled it changes slightly to this:
	 *   Byte |  8  |  7  |  6  |  5  |  4  |  3  |  2  |  1  |
	 *   -----+-----+-----+-----+-----+-----+-----+-----+-----+
	 *    1   | RX <4:3>  |          LX <5:1>           | BDU |
	 *    2   | RX <2:1>  |          LY <5:1>           | BDL |
	 *   -----+-----+-----+-----+-----------------------+-----+
	 *    3   |RX<0>| LT <4:3>  |         RY <4:0>            |
	 *   -----+-----+-----------+-----------------------------+
	 *    4   |     LT <2:0>    |         RT <4:0>            |
	 *   -----+-----+-----+-----+-----+-----+-----+-----+-----+
	 *    5   | BDR | BDD | BLT | B-  | BH  | B+  | BRT | EXT |
	 *   -----+-----+-----+-----+-----+-----+-----+-----+-----+
	 *    6   | BZL | BB  | BY  | BA  | BX  | BZR |  0  |  0  |
	 *   -----+-----+-----+-----+-----+-----+-----+-----+-----+
	 * Only the LSBs of LX and LY are lost. BDU and BDL are moved, the rest
	 * is the same as before.
	 */

	if (ext->motionp) {
		lx = payload[0] & 0x3e;
		ly = payload[0] & 0x3e;
	} else {
		lx = payload[0] & 0x3f;
		ly = payload[0] & 0x3f;
	}

	rx = (payload[0] >> 3) & 0x14;
	rx |= (payload[1] >> 5) & 0x06;
	rx |= (payload[2] >> 7) & 0x01;
	ry = payload[2] & 0x1f;

	rt = payload[3] & 0x1f;
	lt = (payload[2] >> 2) & 0x18;
	lt |= (payload[3] >> 5) & 0x07;

	rx <<= 1;
	ry <<= 1;
	rt <<= 1;
	lt <<= 1;

	input_report_abs(ext->input, ABS_HAT1X, lx - 0x20);
	input_report_abs(ext->input, ABS_HAT1Y, ly - 0x20);
	input_report_abs(ext->input, ABS_HAT2X, rx - 0x20);
	input_report_abs(ext->input, ABS_HAT2Y, ry - 0x20);
	input_report_abs(ext->input, ABS_HAT3X, rt - 0x20);
	input_report_abs(ext->input, ABS_HAT3Y, lt - 0x20);

	input_report_key(ext->input, wiiext_keymap[WIIEXT_KEY_RIGHT],
							!!(payload[4] & 0x80));
	input_report_key(ext->input, wiiext_keymap[WIIEXT_KEY_DOWN],
							!!(payload[4] & 0x40));
	input_report_key(ext->input, wiiext_keymap[WIIEXT_KEY_LT],
							!!(payload[4] & 0x20));
	input_report_key(ext->input, wiiext_keymap[WIIEXT_KEY_MINUS],
							!!(payload[4] & 0x10));
	input_report_key(ext->input, wiiext_keymap[WIIEXT_KEY_HOME],
							!!(payload[4] & 0x08));
	input_report_key(ext->input, wiiext_keymap[WIIEXT_KEY_PLUS],
							!!(payload[4] & 0x04));
	input_report_key(ext->input, wiiext_keymap[WIIEXT_KEY_RT],
							!!(payload[4] & 0x02));
	input_report_key(ext->input, wiiext_keymap[WIIEXT_KEY_ZL],
							!!(payload[5] & 0x80));
	input_report_key(ext->input, wiiext_keymap[WIIEXT_KEY_B],
							!!(payload[5] & 0x40));
	input_report_key(ext->input, wiiext_keymap[WIIEXT_KEY_Y],
							!!(payload[5] & 0x20));
	input_report_key(ext->input, wiiext_keymap[WIIEXT_KEY_A],
							!!(payload[5] & 0x10));
	input_report_key(ext->input, wiiext_keymap[WIIEXT_KEY_X],
							!!(payload[5] & 0x08));
	input_report_key(ext->input, wiiext_keymap[WIIEXT_KEY_ZR],
							!!(payload[5] & 0x04));

	if (ext->motionp) {
		input_report_key(ext->input, wiiext_keymap[WIIEXT_KEY_UP],
							!!(payload[0] & 0x01));
		input_report_key(ext->input, wiiext_keymap[WIIEXT_KEY_LEFT],
							!!(payload[1] & 0x01));
	} else {
		input_report_key(ext->input, wiiext_keymap[WIIEXT_KEY_UP],
							!!(payload[5] & 0x01));
		input_report_key(ext->input, wiiext_keymap[WIIEXT_KEY_LEFT],
							!!(payload[5] & 0x02));
	}

	input_sync(ext->input);
}

static void handler_balance_board(struct wiimote_ext *ext, const __u8 *payload)
{
	__s32 val[4], tmp;
	unsigned int i;

	/*   Byte |  8  7  6  5  4  3  2  1  |
	 *   -----+--------------------------+
	 *    1   |    Top Right <15:8>      |
	 *    2   |    Top Right  <7:0>      |
	 *   -----+--------------------------+
	 *    3   | Bottom Right <15:8>      |
	 *    4   | Bottom Right  <7:0>      |
	 *   -----+--------------------------+
	 *    5   |     Top Left <15:8>      |
	 *    6   |     Top Left  <7:0>      |
	 *   -----+--------------------------+
	 *    7   |  Bottom Left <15:8>      |
	 *    8   |  Bottom Left  <7:0>      |
	 *   -----+--------------------------+
	 *
	 * These values represent the weight-measurements of the Wii-balance
	 * board with 16bit precision.
	 *
	 * The balance-board is never reported interleaved with motionp.
	 */

	val[0] = payload[0];
	val[0] <<= 8;
	val[0] |= payload[1];

	val[1] = payload[2];
	val[1] <<= 8;
	val[1] |= payload[3];

	val[2] = payload[4];
	val[2] <<= 8;
	val[2] |= payload[5];

	val[3] = payload[6];
	val[3] <<= 8;
	val[3] |= payload[7];

	/* apply calibration data */
	for (i = 0; i < 4; i++) {
		if (val[i] < ext->calib[i][1]) {
			tmp = val[i] - ext->calib[i][0];
			tmp *= 1700;
			tmp /= ext->calib[i][1] - ext->calib[i][0];
		} else {
			tmp = val[i] - ext->calib[i][1];
			tmp *= 1700;
			tmp /= ext->calib[i][2] - ext->calib[i][1];
			tmp += 1700;
		}
		val[i] = tmp;
	}

	input_report_abs(ext->input, ABS_HAT0X, val[0]);
	input_report_abs(ext->input, ABS_HAT0Y, val[1]);
	input_report_abs(ext->input, ABS_HAT1X, val[2]);
	input_report_abs(ext->input, ABS_HAT1Y, val[3]);

	input_sync(ext->input);
}

/* call this with state.lock spinlock held */
void wiiext_handle(struct wiimote_data *wdata, const __u8 *payload)
{
	struct wiimote_ext *ext = wdata->ext;

	if (!ext)
		return;

	if (ext->motionp && (payload[5] & 0x02)) {
		handler_motionp(ext, payload);
	} else if (ext->ext_type == WIIEXT_NUNCHUCK) {
		handler_nunchuck(ext, payload);
	} else if (ext->ext_type == WIIEXT_CLASSIC) {
		handler_classic(ext, payload);
	} else if (ext->ext_type == WIIEXT_BALANCE_BOARD) {
		handler_balance_board(ext, payload);
	}
}

static ssize_t wiiext_show(struct device *dev, struct device_attribute *attr,
								char *buf)
{
	struct wiimote_data *wdata = dev_to_wii(dev);
	__u8 type = WIIEXT_NONE;
	bool motionp = false;
	unsigned long flags;

	spin_lock_irqsave(&wdata->state.lock, flags);
	if (wdata->ext) {
		motionp = wdata->ext->motionp;
		type = wdata->ext->ext_type;
	}
	spin_unlock_irqrestore(&wdata->state.lock, flags);

	if (type == WIIEXT_NUNCHUCK) {
		if (motionp)
			return sprintf(buf, "motionp+nunchuck\n");
		else
			return sprintf(buf, "nunchuck\n");
	} else if (type == WIIEXT_CLASSIC) {
		if (motionp)
			return sprintf(buf, "motionp+classic\n");
		else
			return sprintf(buf, "classic\n");
	} else if (type == WIIEXT_BALANCE_BOARD) {
		if (motionp)
			return sprintf(buf, "motionp+balanceboard\n");
		else
			return sprintf(buf, "balanceboard\n");
	} else {
		if (motionp)
			return sprintf(buf, "motionp\n");
		else
			return sprintf(buf, "none\n");
	}
}

static DEVICE_ATTR(extension, S_IRUGO, wiiext_show, NULL);

static int wiiext_input_open(struct input_dev *dev)
{
	struct wiimote_ext *ext = input_get_drvdata(dev);
	int ret;

	ret = hid_hw_open(ext->wdata->hdev);
	if (ret)
		return ret;

	atomic_inc(&ext->opened);
	wiiext_schedule(ext);

	return 0;
}

static void wiiext_input_close(struct input_dev *dev)
{
	struct wiimote_ext *ext = input_get_drvdata(dev);

	atomic_dec(&ext->opened);
	wiiext_schedule(ext);
	hid_hw_close(ext->wdata->hdev);
}

static int wiiext_mp_open(struct input_dev *dev)
{
	struct wiimote_ext *ext = input_get_drvdata(dev);
	int ret;

	ret = hid_hw_open(ext->wdata->hdev);
	if (ret)
		return ret;

	atomic_inc(&ext->mp_opened);
	wiiext_schedule(ext);

	return 0;
}

static void wiiext_mp_close(struct input_dev *dev)
{
	struct wiimote_ext *ext = input_get_drvdata(dev);

	atomic_dec(&ext->mp_opened);
	wiiext_schedule(ext);
	hid_hw_close(ext->wdata->hdev);
}

/* Initializes the extension driver of a wiimote */
int wiiext_init(struct wiimote_data *wdata)
{
	struct wiimote_ext *ext;
	unsigned long flags;
	int ret, i;

	ext = kzalloc(sizeof(*ext), GFP_KERNEL);
	if (!ext)
		return -ENOMEM;

	ext->wdata = wdata;
	INIT_WORK(&ext->worker, wiiext_worker);

	ext->input = input_allocate_device();
	if (!ext->input) {
		ret = -ENOMEM;
		goto err_input;
	}

	input_set_drvdata(ext->input, ext);
	ext->input->open = wiiext_input_open;
	ext->input->close = wiiext_input_close;
	ext->input->dev.parent = &wdata->hdev->dev;
	ext->input->id.bustype = wdata->hdev->bus;
	ext->input->id.vendor = wdata->hdev->vendor;
	ext->input->id.product = wdata->hdev->product;
	ext->input->id.version = wdata->hdev->version;
	ext->input->name = WIIMOTE_NAME " Extension";

	set_bit(EV_KEY, ext->input->evbit);
	for (i = 0; i < WIIEXT_KEY_COUNT; ++i)
		set_bit(wiiext_keymap[i], ext->input->keybit);

	set_bit(EV_ABS, ext->input->evbit);
	set_bit(ABS_HAT0X, ext->input->absbit);
	set_bit(ABS_HAT0Y, ext->input->absbit);
	set_bit(ABS_HAT1X, ext->input->absbit);
	set_bit(ABS_HAT1Y, ext->input->absbit);
	set_bit(ABS_HAT2X, ext->input->absbit);
	set_bit(ABS_HAT2Y, ext->input->absbit);
	set_bit(ABS_HAT3X, ext->input->absbit);
	set_bit(ABS_HAT3Y, ext->input->absbit);
	input_set_abs_params(ext->input, ABS_HAT0X, -120, 120, 2, 4);
	input_set_abs_params(ext->input, ABS_HAT0Y, -120, 120, 2, 4);
	input_set_abs_params(ext->input, ABS_HAT1X, -30, 30, 1, 1);
	input_set_abs_params(ext->input, ABS_HAT1Y, -30, 30, 1, 1);
	input_set_abs_params(ext->input, ABS_HAT2X, -30, 30, 1, 1);
	input_set_abs_params(ext->input, ABS_HAT2Y, -30, 30, 1, 1);
	input_set_abs_params(ext->input, ABS_HAT3X, -30, 30, 1, 1);
	input_set_abs_params(ext->input, ABS_HAT3Y, -30, 30, 1, 1);
	set_bit(ABS_RX, ext->input->absbit);
	set_bit(ABS_RY, ext->input->absbit);
	set_bit(ABS_RZ, ext->input->absbit);
	input_set_abs_params(ext->input, ABS_RX, -500, 500, 2, 4);
	input_set_abs_params(ext->input, ABS_RY, -500, 500, 2, 4);
	input_set_abs_params(ext->input, ABS_RZ, -500, 500, 2, 4);

	ret = input_register_device(ext->input);
	if (ret) {
		input_free_device(ext->input);
		goto err_input;
	}

	ext->mp_input = input_allocate_device();
	if (!ext->mp_input) {
		ret = -ENOMEM;
		goto err_mp;
	}

	input_set_drvdata(ext->mp_input, ext);
	ext->mp_input->open = wiiext_mp_open;
	ext->mp_input->close = wiiext_mp_close;
	ext->mp_input->dev.parent = &wdata->hdev->dev;
	ext->mp_input->id.bustype = wdata->hdev->bus;
	ext->mp_input->id.vendor = wdata->hdev->vendor;
	ext->mp_input->id.product = wdata->hdev->product;
	ext->mp_input->id.version = wdata->hdev->version;
	ext->mp_input->name = WIIMOTE_NAME " Motion+";

	set_bit(EV_ABS, ext->mp_input->evbit);
	set_bit(ABS_RX, ext->mp_input->absbit);
	set_bit(ABS_RY, ext->mp_input->absbit);
	set_bit(ABS_RZ, ext->mp_input->absbit);
	input_set_abs_params(ext->mp_input, ABS_RX, -160000, 160000, 4, 8);
	input_set_abs_params(ext->mp_input, ABS_RY, -160000, 160000, 4, 8);
	input_set_abs_params(ext->mp_input, ABS_RZ, -160000, 160000, 4, 8);

	ret = input_register_device(ext->mp_input);
	if (ret) {
		input_free_device(ext->mp_input);
		goto err_mp;
	}

	ret = device_create_file(&wdata->hdev->dev, &dev_attr_extension);
	if (ret)
		goto err_dev;

	spin_lock_irqsave(&wdata->state.lock, flags);
	wdata->ext = ext;
	spin_unlock_irqrestore(&wdata->state.lock, flags);

	return 0;

err_dev:
	input_unregister_device(ext->mp_input);
err_mp:
	input_unregister_device(ext->input);
err_input:
	kfree(ext);
	return ret;
}

/* Deinitializes the extension driver of a wiimote */
void wiiext_deinit(struct wiimote_data *wdata)
{
	struct wiimote_ext *ext = wdata->ext;
	unsigned long flags;

	if (!ext)
		return;

	/*
	 * We first unset wdata->ext to avoid further input from the wiimote
	 * core. The worker thread does not access this pointer so it is not
	 * affected by this.
	 * We kill the worker after this so it does not get respawned during
	 * deinitialization.
	 */

	spin_lock_irqsave(&wdata->state.lock, flags);
	wdata->ext = NULL;
	spin_unlock_irqrestore(&wdata->state.lock, flags);

	device_remove_file(&wdata->hdev->dev, &dev_attr_extension);
	input_unregister_device(ext->mp_input);
	input_unregister_device(ext->input);

	cancel_work_sync(&ext->worker);
	kfree(ext);
}
