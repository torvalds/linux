/*
 * OLPC HGPK (XO-1) touchpad PS/2 mouse driver
 *
 * Copyright (c) 2006-2008 One Laptop Per Child
 * Authors:
 *   Zephaniah E. Hull
 *   Andres Salomon <dilinger@debian.org>
 *
 * This driver is partly based on the ALPS driver, which is:
 *
 * Copyright (c) 2003 Neil Brown <neilb@cse.unsw.edu.au>
 * Copyright (c) 2003-2005 Peter Osterlund <petero2@telia.com>
 * Copyright (c) 2004 Dmitry Torokhov <dtor@mail.ru>
 * Copyright (c) 2005 Vojtech Pavlik <vojtech@suse.cz>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * The spec from ALPS is available from
 * <http://wiki.laptop.org/go/Touch_Pad/Tablet>.  It refers to this
 * device as HGPK (Hybrid GS, PT, and Keymatrix).
 *
 * The earliest versions of the device had simultaneous reporting; that
 * was removed.  After that, the device used the Advanced Mode GS/PT streaming
 * stuff.  That turned out to be too buggy to support, so we've finally
 * switched to Mouse Mode (which utilizes only the center 1/3 of the touchpad).
 */

#define DEBUG
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/serio.h>
#include <linux/libps2.h>
#include <linux/delay.h>
#include <asm/olpc.h>

#include "psmouse.h"
#include "hgpk.h"

static int tpdebug;
module_param(tpdebug, int, 0644);
MODULE_PARM_DESC(tpdebug, "enable debugging, dumping packets to KERN_DEBUG.");

static int recalib_delta = 100;
module_param(recalib_delta, int, 0644);
MODULE_PARM_DESC(recalib_delta,
	"packets containing a delta this large will cause a recalibration.");

static int jumpy_delay = 1000;
module_param(jumpy_delay, int, 0644);
MODULE_PARM_DESC(jumpy_delay,
	"delay (ms) before recal after jumpiness detected");

static int spew_delay = 1000;
module_param(spew_delay, int, 0644);
MODULE_PARM_DESC(spew_delay,
	"delay (ms) before recal after packet spew detected");

static int recal_guard_time = 2000;
module_param(recal_guard_time, int, 0644);
MODULE_PARM_DESC(recal_guard_time,
	"interval (ms) during which recal will be restarted if packet received");

static int post_interrupt_delay = 1000;
module_param(post_interrupt_delay, int, 0644);
MODULE_PARM_DESC(post_interrupt_delay,
	"delay (ms) before recal after recal interrupt detected");

/*
 * When the touchpad gets ultra-sensitive, one can keep their finger 1/2"
 * above the pad and still have it send packets.  This causes a jump cursor
 * when one places their finger on the pad.  We can probably detect the
 * jump as we see a large deltas (>= 100px).  In mouse mode, I've been
 * unable to even come close to 100px deltas during normal usage, so I think
 * this threshold is safe.  If a large delta occurs, trigger a recalibration.
 */
static void hgpk_jumpy_hack(struct psmouse *psmouse, int x, int y)
{
	struct hgpk_data *priv = psmouse->private;

	if (abs(x) > recalib_delta || abs(y) > recalib_delta) {
		hgpk_err(psmouse, ">%dpx jump detected (%d,%d)\n",
				recalib_delta, x, y);
		/* My car gets forty rods to the hogshead and that's the
		 * way I likes it! */
		psmouse_queue_work(psmouse, &priv->recalib_wq,
				msecs_to_jiffies(jumpy_delay));
	}
}

/*
 * We have no idea why this particular hardware bug occurs.  The touchpad
 * will randomly start spewing packets without anything touching the
 * pad.  This wouldn't necessarily be bad, but it's indicative of a
 * severely miscalibrated pad; attempting to use the touchpad while it's
 * spewing means the cursor will jump all over the place, and act "drunk".
 *
 * The packets that are spewed tend to all have deltas between -2 and 2, and
 * the cursor will move around without really going very far.  It will
 * tend to end up in the same location; if we tally up the changes over
 * 100 packets, we end up w/ a final delta of close to 0.  This happens
 * pretty regularly when the touchpad is spewing, and is pretty hard to
 * manually trigger (at least for *my* fingers).  So, it makes a perfect
 * scheme for detecting spews.
 */
static void hgpk_spewing_hack(struct psmouse *psmouse,
			      int l, int r, int x, int y)
{
	struct hgpk_data *priv = psmouse->private;

	/* ignore button press packets; many in a row could trigger
	 * a false-positive! */
	if (l || r)
		return;

	priv->x_tally += x;
	priv->y_tally += y;

	if (++priv->count > 100) {
		if (abs(priv->x_tally) < 3 && abs(priv->y_tally) < 3) {
			hgpk_dbg(psmouse, "packet spew detected (%d,%d)\n",
				 priv->x_tally, priv->y_tally);
			psmouse_queue_work(psmouse, &priv->recalib_wq,
					   msecs_to_jiffies(spew_delay));
		}
		/* reset every 100 packets */
		priv->count = 0;
		priv->x_tally = 0;
		priv->y_tally = 0;
	}
}

/*
 * HGPK Mouse Mode format (standard mouse format, sans middle button)
 *
 * byte 0:	y-over	x-over	y-neg	x-neg	1	0	swr	swl
 * byte 1:	x7	x6	x5	x4	x3	x2	x1	x0
 * byte 2:	y7	y6	y5	y4	y3	y2	y1	y0
 *
 * swr/swl are the left/right buttons.
 * x-neg/y-neg are the x and y delta negative bits
 * x-over/y-over are the x and y overflow bits
 */
static int hgpk_validate_byte(unsigned char *packet)
{
	return (packet[0] & 0x0C) != 0x08;
}

static void hgpk_process_packet(struct psmouse *psmouse)
{
	struct input_dev *dev = psmouse->dev;
	unsigned char *packet = psmouse->packet;
	int x, y, left, right;

	left = packet[0] & 1;
	right = (packet[0] >> 1) & 1;

	x = packet[1] - ((packet[0] << 4) & 0x100);
	y = ((packet[0] << 3) & 0x100) - packet[2];

	hgpk_jumpy_hack(psmouse, x, y);
	hgpk_spewing_hack(psmouse, left, right, x, y);

	if (tpdebug)
		hgpk_dbg(psmouse, "l=%d r=%d x=%d y=%d\n", left, right, x, y);

	input_report_key(dev, BTN_LEFT, left);
	input_report_key(dev, BTN_RIGHT, right);

	input_report_rel(dev, REL_X, x);
	input_report_rel(dev, REL_Y, y);

	input_sync(dev);
}

static psmouse_ret_t hgpk_process_byte(struct psmouse *psmouse)
{
	struct hgpk_data *priv = psmouse->private;

	if (hgpk_validate_byte(psmouse->packet)) {
		hgpk_dbg(psmouse, "%s: (%d) %02x %02x %02x\n",
				__func__, psmouse->pktcnt, psmouse->packet[0],
				psmouse->packet[1], psmouse->packet[2]);
		return PSMOUSE_BAD_DATA;
	}

	if (psmouse->pktcnt >= psmouse->pktsize) {
		hgpk_process_packet(psmouse);
		return PSMOUSE_FULL_PACKET;
	}

	if (priv->recalib_window) {
		if (time_before(jiffies, priv->recalib_window)) {
			/*
			 * ugh, got a packet inside our recalibration
			 * window, schedule another recalibration.
			 */
			hgpk_dbg(psmouse,
				 "packet inside calibration window, "
				 "queueing another recalibration\n");
			psmouse_queue_work(psmouse, &priv->recalib_wq,
					msecs_to_jiffies(post_interrupt_delay));
		}
		priv->recalib_window = 0;
	}

	return PSMOUSE_GOOD_DATA;
}

static int hgpk_force_recalibrate(struct psmouse *psmouse)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	struct hgpk_data *priv = psmouse->private;

	/* C-series touchpads added the recalibrate command */
	if (psmouse->model < HGPK_MODEL_C)
		return 0;

	/* we don't want to race with the irq handler, nor with resyncs */
	psmouse_set_state(psmouse, PSMOUSE_INITIALIZING);

	/* start by resetting the device */
	psmouse_reset(psmouse);

	/* send the recalibrate request */
	if (ps2_command(ps2dev, NULL, 0xf5) ||
	    ps2_command(ps2dev, NULL, 0xf5) ||
	    ps2_command(ps2dev, NULL, 0xe6) ||
	    ps2_command(ps2dev, NULL, 0xf5)) {
		return -1;
	}

	/* according to ALPS, 150mS is required for recalibration */
	msleep(150);

	/* XXX: If a finger is down during this delay, recalibration will
	 * detect capacitance incorrectly.  This is a hardware bug, and
	 * we don't have a good way to deal with it.  The 2s window stuff
	 * (below) is our best option for now.
	 */

	if (ps2_command(ps2dev, NULL, PSMOUSE_CMD_ENABLE))
		return -1;

	psmouse_set_state(psmouse, PSMOUSE_ACTIVATED);

	/* After we recalibrate, we shouldn't get any packets for 2s.  If
	 * we do, it's likely that someone's finger was on the touchpad.
	 * If someone's finger *was* on the touchpad, it's probably
	 * miscalibrated.  So, we should schedule another recalibration
	 */
	priv->recalib_window = jiffies +  msecs_to_jiffies(recal_guard_time);

	return 0;
}

/*
 * This kills power to the touchpad; according to ALPS, current consumption
 * goes down to 50uA after running this.  To turn power back on, we drive
 * MS-DAT low.
 */
static int hgpk_toggle_power(struct psmouse *psmouse, int enable)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	int timeo;

	/* Added on D-series touchpads */
	if (psmouse->model < HGPK_MODEL_D)
		return 0;

	if (enable) {
		psmouse_set_state(psmouse, PSMOUSE_INITIALIZING);

		/*
		 * Sending a byte will drive MS-DAT low; this will wake up
		 * the controller.  Once we get an ACK back from it, it
		 * means we can continue with the touchpad re-init.  ALPS
		 * tells us that 1s should be long enough, so set that as
		 * the upper bound.
		 */
		for (timeo = 20; timeo > 0; timeo--) {
			if (!ps2_sendbyte(&psmouse->ps2dev,
					PSMOUSE_CMD_DISABLE, 20))
				break;
			msleep(50);
		}

		psmouse_reset(psmouse);

		/* should be all set, enable the touchpad */
		ps2_command(&psmouse->ps2dev, NULL, PSMOUSE_CMD_ENABLE);
		psmouse_set_state(psmouse, PSMOUSE_ACTIVATED);

	} else {
		hgpk_dbg(psmouse, "Powering off touchpad.\n");
		psmouse_set_state(psmouse, PSMOUSE_IGNORE);

		if (ps2_command(ps2dev, NULL, 0xec) ||
		    ps2_command(ps2dev, NULL, 0xec) ||
		    ps2_command(ps2dev, NULL, 0xea)) {
			return -1;
		}

		/* probably won't see an ACK, the touchpad will be off */
		ps2_sendbyte(&psmouse->ps2dev, 0xec, 20);
	}

	return 0;
}

static int hgpk_poll(struct psmouse *psmouse)
{
	/* We can't poll, so always return failure. */
	return -1;
}

static int hgpk_reconnect(struct psmouse *psmouse)
{
	/* During suspend/resume the ps2 rails remain powered.  We don't want
	 * to do a reset because it's flush data out of buffers; however,
	 * earlier prototypes (B1) had some brokenness that required a reset. */
	if (olpc_board_at_least(olpc_board(0xb2)))
		if (psmouse->ps2dev.serio->dev.power.power_state.event !=
				PM_EVENT_ON)
			return 0;

	psmouse_reset(psmouse);

	return 0;
}

static ssize_t hgpk_show_powered(struct psmouse *psmouse, void *data, char *buf)
{
	struct hgpk_data *priv = psmouse->private;

	return sprintf(buf, "%d\n", priv->powered);
}

static ssize_t hgpk_set_powered(struct psmouse *psmouse, void *data,
				const char *buf, size_t count)
{
	struct hgpk_data *priv = psmouse->private;
	unsigned long value;
	int err;

	err = strict_strtoul(buf, 10, &value);
	if (err || value > 1)
		return -EINVAL;

	if (value != priv->powered) {
		/*
		 * hgpk_toggle_power will deal w/ state so
		 * we're not racing w/ irq
		 */
		err = hgpk_toggle_power(psmouse, value);
		if (!err)
			priv->powered = value;
	}

	return err ? err : count;
}

__PSMOUSE_DEFINE_ATTR(powered, S_IWUSR | S_IRUGO, NULL,
		      hgpk_show_powered, hgpk_set_powered, false);

static ssize_t hgpk_trigger_recal_show(struct psmouse *psmouse,
		void *data, char *buf)
{
	return -EINVAL;
}

static ssize_t hgpk_trigger_recal(struct psmouse *psmouse, void *data,
				const char *buf, size_t count)
{
	struct hgpk_data *priv = psmouse->private;
	unsigned long value;
	int err;

	err = strict_strtoul(buf, 10, &value);
	if (err || value != 1)
		return -EINVAL;

	/*
	 * We queue work instead of doing recalibration right here
	 * to avoid adding locking to to hgpk_force_recalibrate()
	 * since workqueue provides serialization.
	 */
	psmouse_queue_work(psmouse, &priv->recalib_wq, 0);
	return count;
}

__PSMOUSE_DEFINE_ATTR(recalibrate, S_IWUSR | S_IRUGO, NULL,
		      hgpk_trigger_recal_show, hgpk_trigger_recal, false);

static void hgpk_disconnect(struct psmouse *psmouse)
{
	struct hgpk_data *priv = psmouse->private;

	device_remove_file(&psmouse->ps2dev.serio->dev,
			   &psmouse_attr_powered.dattr);

	if (psmouse->model >= HGPK_MODEL_C)
		device_remove_file(&psmouse->ps2dev.serio->dev,
				   &psmouse_attr_recalibrate.dattr);

	psmouse_reset(psmouse);
	kfree(priv);
}

static void hgpk_recalib_work(struct work_struct *work)
{
	struct delayed_work *w = to_delayed_work(work);
	struct hgpk_data *priv = container_of(w, struct hgpk_data, recalib_wq);
	struct psmouse *psmouse = priv->psmouse;

	hgpk_dbg(psmouse, "recalibrating touchpad..\n");

	if (hgpk_force_recalibrate(psmouse))
		hgpk_err(psmouse, "recalibration failed!\n");
}

static int hgpk_register(struct psmouse *psmouse)
{
	int err;

	/* register handlers */
	psmouse->protocol_handler = hgpk_process_byte;
	psmouse->poll = hgpk_poll;
	psmouse->disconnect = hgpk_disconnect;
	psmouse->reconnect = hgpk_reconnect;
	psmouse->pktsize = 3;

	/* Disable the idle resync. */
	psmouse->resync_time = 0;
	/* Reset after a lot of bad bytes. */
	psmouse->resetafter = 1024;

	err = device_create_file(&psmouse->ps2dev.serio->dev,
				 &psmouse_attr_powered.dattr);
	if (err) {
		hgpk_err(psmouse, "Failed creating 'powered' sysfs node\n");
		return err;
	}

	/* C-series touchpads added the recalibrate command */
	if (psmouse->model >= HGPK_MODEL_C) {
		err = device_create_file(&psmouse->ps2dev.serio->dev,
					 &psmouse_attr_recalibrate.dattr);
		if (err) {
			hgpk_err(psmouse,
				"Failed creating 'recalibrate' sysfs node\n");
			device_remove_file(&psmouse->ps2dev.serio->dev,
					&psmouse_attr_powered.dattr);
			return err;
		}
	}

	return 0;
}

int hgpk_init(struct psmouse *psmouse)
{
	struct hgpk_data *priv;
	int err = -ENOMEM;

	priv = kzalloc(sizeof(struct hgpk_data), GFP_KERNEL);
	if (!priv)
		goto alloc_fail;

	psmouse->private = priv;
	priv->psmouse = psmouse;
	priv->powered = true;
	INIT_DELAYED_WORK(&priv->recalib_wq, hgpk_recalib_work);

	err = psmouse_reset(psmouse);
	if (err)
		goto init_fail;

	err = hgpk_register(psmouse);
	if (err)
		goto init_fail;

	return 0;

init_fail:
	kfree(priv);
alloc_fail:
	return err;
}

static enum hgpk_model_t hgpk_get_model(struct psmouse *psmouse)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	unsigned char param[3];

	/* E7, E7, E7, E9 gets us a 3 byte identifier */
	if (ps2_command(ps2dev,  NULL, PSMOUSE_CMD_SETSCALE21) ||
	    ps2_command(ps2dev,  NULL, PSMOUSE_CMD_SETSCALE21) ||
	    ps2_command(ps2dev,  NULL, PSMOUSE_CMD_SETSCALE21) ||
	    ps2_command(ps2dev, param, PSMOUSE_CMD_GETINFO)) {
		return -EIO;
	}

	hgpk_dbg(psmouse, "ID: %02x %02x %02x\n", param[0], param[1], param[2]);

	/* HGPK signature: 0x67, 0x00, 0x<model> */
	if (param[0] != 0x67 || param[1] != 0x00)
		return -ENODEV;

	hgpk_info(psmouse, "OLPC touchpad revision 0x%x\n", param[2]);

	return param[2];
}

int hgpk_detect(struct psmouse *psmouse, bool set_properties)
{
	int version;

	version = hgpk_get_model(psmouse);
	if (version < 0)
		return version;

	if (set_properties) {
		psmouse->vendor = "ALPS";
		psmouse->name = "HGPK";
		psmouse->model = version;
	}

	return 0;
}
