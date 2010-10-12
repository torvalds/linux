/*
 * PS/2 mouse driver
 *
 * Copyright (c) 1999-2002 Vojtech Pavlik
 * Copyright (c) 2003-2004 Dmitry Torokhov
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/serio.h>
#include <linux/init.h>
#include <linux/libps2.h>
#include <linux/mutex.h>

#include "psmouse.h"
#include "synaptics.h"
#include "logips2pp.h"
#include "alps.h"
#include "hgpk.h"
#include "lifebook.h"
#include "trackpoint.h"
#include "touchkit_ps2.h"
#include "elantech.h"
#include "sentelic.h"

#define DRIVER_DESC	"PS/2 mouse driver"

MODULE_AUTHOR("Vojtech Pavlik <vojtech@suse.cz>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

static unsigned int psmouse_max_proto = PSMOUSE_AUTO;
static int psmouse_set_maxproto(const char *val, const struct kernel_param *);
static int psmouse_get_maxproto(char *buffer, const struct kernel_param *kp);
static struct kernel_param_ops param_ops_proto_abbrev = {
	.set = psmouse_set_maxproto,
	.get = psmouse_get_maxproto,
};
#define param_check_proto_abbrev(name, p)	__param_check(name, p, unsigned int)
module_param_named(proto, psmouse_max_proto, proto_abbrev, 0644);
MODULE_PARM_DESC(proto, "Highest protocol extension to probe (bare, imps, exps, any). Useful for KVM switches.");

static unsigned int psmouse_resolution = 200;
module_param_named(resolution, psmouse_resolution, uint, 0644);
MODULE_PARM_DESC(resolution, "Resolution, in dpi.");

static unsigned int psmouse_rate = 100;
module_param_named(rate, psmouse_rate, uint, 0644);
MODULE_PARM_DESC(rate, "Report rate, in reports per second.");

static unsigned int psmouse_smartscroll = 1;
module_param_named(smartscroll, psmouse_smartscroll, bool, 0644);
MODULE_PARM_DESC(smartscroll, "Logitech Smartscroll autorepeat, 1 = enabled (default), 0 = disabled.");

static unsigned int psmouse_resetafter = 5;
module_param_named(resetafter, psmouse_resetafter, uint, 0644);
MODULE_PARM_DESC(resetafter, "Reset device after so many bad packets (0 = never).");

static unsigned int psmouse_resync_time;
module_param_named(resync_time, psmouse_resync_time, uint, 0644);
MODULE_PARM_DESC(resync_time, "How long can mouse stay idle before forcing resync (in seconds, 0 = never).");

PSMOUSE_DEFINE_ATTR(protocol, S_IWUSR | S_IRUGO,
			NULL,
			psmouse_attr_show_protocol, psmouse_attr_set_protocol);
PSMOUSE_DEFINE_ATTR(rate, S_IWUSR | S_IRUGO,
			(void *) offsetof(struct psmouse, rate),
			psmouse_show_int_attr, psmouse_attr_set_rate);
PSMOUSE_DEFINE_ATTR(resolution, S_IWUSR | S_IRUGO,
			(void *) offsetof(struct psmouse, resolution),
			psmouse_show_int_attr, psmouse_attr_set_resolution);
PSMOUSE_DEFINE_ATTR(resetafter, S_IWUSR | S_IRUGO,
			(void *) offsetof(struct psmouse, resetafter),
			psmouse_show_int_attr, psmouse_set_int_attr);
PSMOUSE_DEFINE_ATTR(resync_time, S_IWUSR | S_IRUGO,
			(void *) offsetof(struct psmouse, resync_time),
			psmouse_show_int_attr, psmouse_set_int_attr);

static struct attribute *psmouse_attributes[] = {
	&psmouse_attr_protocol.dattr.attr,
	&psmouse_attr_rate.dattr.attr,
	&psmouse_attr_resolution.dattr.attr,
	&psmouse_attr_resetafter.dattr.attr,
	&psmouse_attr_resync_time.dattr.attr,
	NULL
};

static struct attribute_group psmouse_attribute_group = {
	.attrs	= psmouse_attributes,
};

/*
 * psmouse_mutex protects all operations changing state of mouse
 * (connecting, disconnecting, changing rate or resolution via
 * sysfs). We could use a per-device semaphore but since there
 * rarely more than one PS/2 mouse connected and since semaphore
 * is taken in "slow" paths it is not worth it.
 */
static DEFINE_MUTEX(psmouse_mutex);

static struct workqueue_struct *kpsmoused_wq;

struct psmouse_protocol {
	enum psmouse_type type;
	bool maxproto;
	bool ignore_parity; /* Protocol should ignore parity errors from KBC */
	const char *name;
	const char *alias;
	int (*detect)(struct psmouse *, bool);
	int (*init)(struct psmouse *);
};

/*
 * psmouse_process_byte() analyzes the PS/2 data stream and reports
 * relevant events to the input module once full packet has arrived.
 */

static psmouse_ret_t psmouse_process_byte(struct psmouse *psmouse)
{
	struct input_dev *dev = psmouse->dev;
	unsigned char *packet = psmouse->packet;

	if (psmouse->pktcnt < psmouse->pktsize)
		return PSMOUSE_GOOD_DATA;

/*
 * Full packet accumulated, process it
 */

/*
 * Scroll wheel on IntelliMice, scroll buttons on NetMice
 */

	if (psmouse->type == PSMOUSE_IMPS || psmouse->type == PSMOUSE_GENPS)
		input_report_rel(dev, REL_WHEEL, -(signed char) packet[3]);

/*
 * Scroll wheel and buttons on IntelliMouse Explorer
 */

	if (psmouse->type == PSMOUSE_IMEX) {
		switch (packet[3] & 0xC0) {
		case 0x80: /* vertical scroll on IntelliMouse Explorer 4.0 */
			input_report_rel(dev, REL_WHEEL, (int) (packet[3] & 32) - (int) (packet[3] & 31));
			break;
		case 0x40: /* horizontal scroll on IntelliMouse Explorer 4.0 */
			input_report_rel(dev, REL_HWHEEL, (int) (packet[3] & 32) - (int) (packet[3] & 31));
			break;
		case 0x00:
		case 0xC0:
			input_report_rel(dev, REL_WHEEL, (int) (packet[3] & 8) - (int) (packet[3] & 7));
			input_report_key(dev, BTN_SIDE, (packet[3] >> 4) & 1);
			input_report_key(dev, BTN_EXTRA, (packet[3] >> 5) & 1);
			break;
		}
	}

/*
 * Extra buttons on Genius NewNet 3D
 */

	if (psmouse->type == PSMOUSE_GENPS) {
		input_report_key(dev, BTN_SIDE, (packet[0] >> 6) & 1);
		input_report_key(dev, BTN_EXTRA, (packet[0] >> 7) & 1);
	}

/*
 * Extra button on ThinkingMouse
 */
	if (psmouse->type == PSMOUSE_THINKPS) {
		input_report_key(dev, BTN_EXTRA, (packet[0] >> 3) & 1);
		/* Without this bit of weirdness moving up gives wildly high Y changes. */
		packet[1] |= (packet[0] & 0x40) << 1;
	}

/*
 * Cortron PS2 Trackball reports SIDE button on the 4th bit of the first
 * byte.
 */
	if (psmouse->type == PSMOUSE_CORTRON) {
		input_report_key(dev, BTN_SIDE, (packet[0] >> 3) & 1);
		packet[0] |= 0x08;
	}

/*
 * Generic PS/2 Mouse
 */

	input_report_key(dev, BTN_LEFT,    packet[0]       & 1);
	input_report_key(dev, BTN_MIDDLE, (packet[0] >> 2) & 1);
	input_report_key(dev, BTN_RIGHT,  (packet[0] >> 1) & 1);

	input_report_rel(dev, REL_X, packet[1] ? (int) packet[1] - (int) ((packet[0] << 4) & 0x100) : 0);
	input_report_rel(dev, REL_Y, packet[2] ? (int) ((packet[0] << 3) & 0x100) - (int) packet[2] : 0);

	input_sync(dev);

	return PSMOUSE_FULL_PACKET;
}

void psmouse_queue_work(struct psmouse *psmouse, struct delayed_work *work,
		unsigned long delay)
{
	queue_delayed_work(kpsmoused_wq, work, delay);
}

/*
 * __psmouse_set_state() sets new psmouse state and resets all flags.
 */

static inline void __psmouse_set_state(struct psmouse *psmouse, enum psmouse_state new_state)
{
	psmouse->state = new_state;
	psmouse->pktcnt = psmouse->out_of_sync_cnt = 0;
	psmouse->ps2dev.flags = 0;
	psmouse->last = jiffies;
}


/*
 * psmouse_set_state() sets new psmouse state and resets all flags and
 * counters while holding serio lock so fighting with interrupt handler
 * is not a concern.
 */

void psmouse_set_state(struct psmouse *psmouse, enum psmouse_state new_state)
{
	serio_pause_rx(psmouse->ps2dev.serio);
	__psmouse_set_state(psmouse, new_state);
	serio_continue_rx(psmouse->ps2dev.serio);
}

/*
 * psmouse_handle_byte() processes one byte of the input data stream
 * by calling corresponding protocol handler.
 */

static int psmouse_handle_byte(struct psmouse *psmouse)
{
	psmouse_ret_t rc = psmouse->protocol_handler(psmouse);

	switch (rc) {
	case PSMOUSE_BAD_DATA:
		if (psmouse->state == PSMOUSE_ACTIVATED) {
			printk(KERN_WARNING "psmouse.c: %s at %s lost sync at byte %d\n",
				psmouse->name, psmouse->phys, psmouse->pktcnt);
			if (++psmouse->out_of_sync_cnt == psmouse->resetafter) {
				__psmouse_set_state(psmouse, PSMOUSE_IGNORE);
				printk(KERN_NOTICE "psmouse.c: issuing reconnect request\n");
				serio_reconnect(psmouse->ps2dev.serio);
				return -1;
			}
		}
		psmouse->pktcnt = 0;
		break;

	case PSMOUSE_FULL_PACKET:
		psmouse->pktcnt = 0;
		if (psmouse->out_of_sync_cnt) {
			psmouse->out_of_sync_cnt = 0;
			printk(KERN_NOTICE "psmouse.c: %s at %s - driver resynched.\n",
				psmouse->name, psmouse->phys);
		}
		break;

	case PSMOUSE_GOOD_DATA:
		break;
	}
	return 0;
}

/*
 * psmouse_interrupt() handles incoming characters, either passing them
 * for normal processing or gathering them as command response.
 */

static irqreturn_t psmouse_interrupt(struct serio *serio,
		unsigned char data, unsigned int flags)
{
	struct psmouse *psmouse = serio_get_drvdata(serio);

	if (psmouse->state == PSMOUSE_IGNORE)
		goto out;

	if (unlikely((flags & SERIO_TIMEOUT) ||
		     ((flags & SERIO_PARITY) && !psmouse->ignore_parity))) {

		if (psmouse->state == PSMOUSE_ACTIVATED)
			printk(KERN_WARNING "psmouse.c: bad data from KBC -%s%s\n",
				flags & SERIO_TIMEOUT ? " timeout" : "",
				flags & SERIO_PARITY ? " bad parity" : "");
		ps2_cmd_aborted(&psmouse->ps2dev);
		goto out;
	}

	if (unlikely(psmouse->ps2dev.flags & PS2_FLAG_ACK))
		if  (ps2_handle_ack(&psmouse->ps2dev, data))
			goto out;

	if (unlikely(psmouse->ps2dev.flags & PS2_FLAG_CMD))
		if  (ps2_handle_response(&psmouse->ps2dev, data))
			goto out;

	if (psmouse->state <= PSMOUSE_RESYNCING)
		goto out;

	if (psmouse->state == PSMOUSE_ACTIVATED &&
	    psmouse->pktcnt && time_after(jiffies, psmouse->last + HZ/2)) {
		printk(KERN_INFO "psmouse.c: %s at %s lost synchronization, throwing %d bytes away.\n",
		       psmouse->name, psmouse->phys, psmouse->pktcnt);
		psmouse->badbyte = psmouse->packet[0];
		__psmouse_set_state(psmouse, PSMOUSE_RESYNCING);
		psmouse_queue_work(psmouse, &psmouse->resync_work, 0);
		goto out;
	}

	psmouse->packet[psmouse->pktcnt++] = data;
/*
 * Check if this is a new device announcement (0xAA 0x00)
 */
	if (unlikely(psmouse->packet[0] == PSMOUSE_RET_BAT && psmouse->pktcnt <= 2)) {
		if (psmouse->pktcnt == 1) {
			psmouse->last = jiffies;
			goto out;
		}

		if (psmouse->packet[1] == PSMOUSE_RET_ID ||
		    (psmouse->type == PSMOUSE_HGPK &&
		     psmouse->packet[1] == PSMOUSE_RET_BAT)) {
			__psmouse_set_state(psmouse, PSMOUSE_IGNORE);
			serio_reconnect(serio);
			goto out;
		}
/*
 * Not a new device, try processing first byte normally
 */
		psmouse->pktcnt = 1;
		if (psmouse_handle_byte(psmouse))
			goto out;

		psmouse->packet[psmouse->pktcnt++] = data;
	}

/*
 * See if we need to force resync because mouse was idle for too long
 */
	if (psmouse->state == PSMOUSE_ACTIVATED &&
	    psmouse->pktcnt == 1 && psmouse->resync_time &&
	    time_after(jiffies, psmouse->last + psmouse->resync_time * HZ)) {
		psmouse->badbyte = psmouse->packet[0];
		__psmouse_set_state(psmouse, PSMOUSE_RESYNCING);
		psmouse_queue_work(psmouse, &psmouse->resync_work, 0);
		goto out;
	}

	psmouse->last = jiffies;
	psmouse_handle_byte(psmouse);

 out:
	return IRQ_HANDLED;
}


/*
 * psmouse_sliced_command() sends an extended PS/2 command to the mouse
 * using sliced syntax, understood by advanced devices, such as Logitech
 * or Synaptics touchpads. The command is encoded as:
 * 0xE6 0xE8 rr 0xE8 ss 0xE8 tt 0xE8 uu where (rr*64)+(ss*16)+(tt*4)+uu
 * is the command.
 */
int psmouse_sliced_command(struct psmouse *psmouse, unsigned char command)
{
	int i;

	if (ps2_command(&psmouse->ps2dev, NULL, PSMOUSE_CMD_SETSCALE11))
		return -1;

	for (i = 6; i >= 0; i -= 2) {
		unsigned char d = (command >> i) & 3;
		if (ps2_command(&psmouse->ps2dev, &d, PSMOUSE_CMD_SETRES))
			return -1;
	}

	return 0;
}


/*
 * psmouse_reset() resets the mouse into power-on state.
 */
int psmouse_reset(struct psmouse *psmouse)
{
	unsigned char param[2];

	if (ps2_command(&psmouse->ps2dev, param, PSMOUSE_CMD_RESET_BAT))
		return -1;

	if (param[0] != PSMOUSE_RET_BAT && param[1] != PSMOUSE_RET_ID)
		return -1;

	return 0;
}


/*
 * Genius NetMouse magic init.
 */
static int genius_detect(struct psmouse *psmouse, bool set_properties)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	unsigned char param[4];

	param[0] = 3;
	ps2_command(ps2dev, param, PSMOUSE_CMD_SETRES);
	ps2_command(ps2dev,  NULL, PSMOUSE_CMD_SETSCALE11);
	ps2_command(ps2dev,  NULL, PSMOUSE_CMD_SETSCALE11);
	ps2_command(ps2dev,  NULL, PSMOUSE_CMD_SETSCALE11);
	ps2_command(ps2dev, param, PSMOUSE_CMD_GETINFO);

	if (param[0] != 0x00 || param[1] != 0x33 || param[2] != 0x55)
		return -1;

	if (set_properties) {
		__set_bit(BTN_MIDDLE, psmouse->dev->keybit);
		__set_bit(BTN_EXTRA, psmouse->dev->keybit);
		__set_bit(BTN_SIDE, psmouse->dev->keybit);
		__set_bit(REL_WHEEL, psmouse->dev->relbit);

		psmouse->vendor = "Genius";
		psmouse->name = "Mouse";
		psmouse->pktsize = 4;
	}

	return 0;
}

/*
 * IntelliMouse magic init.
 */
static int intellimouse_detect(struct psmouse *psmouse, bool set_properties)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	unsigned char param[2];

	param[0] = 200;
	ps2_command(ps2dev, param, PSMOUSE_CMD_SETRATE);
	param[0] = 100;
	ps2_command(ps2dev, param, PSMOUSE_CMD_SETRATE);
	param[0] =  80;
	ps2_command(ps2dev, param, PSMOUSE_CMD_SETRATE);
	ps2_command(ps2dev, param, PSMOUSE_CMD_GETID);

	if (param[0] != 3)
		return -1;

	if (set_properties) {
		__set_bit(BTN_MIDDLE, psmouse->dev->keybit);
		__set_bit(REL_WHEEL, psmouse->dev->relbit);

		if (!psmouse->vendor)
			psmouse->vendor = "Generic";
		if (!psmouse->name)
			psmouse->name = "Wheel Mouse";
		psmouse->pktsize = 4;
	}

	return 0;
}

/*
 * Try IntelliMouse/Explorer magic init.
 */
static int im_explorer_detect(struct psmouse *psmouse, bool set_properties)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	unsigned char param[2];

	intellimouse_detect(psmouse, 0);

	param[0] = 200;
	ps2_command(ps2dev, param, PSMOUSE_CMD_SETRATE);
	param[0] = 200;
	ps2_command(ps2dev, param, PSMOUSE_CMD_SETRATE);
	param[0] =  80;
	ps2_command(ps2dev, param, PSMOUSE_CMD_SETRATE);
	ps2_command(ps2dev, param, PSMOUSE_CMD_GETID);

	if (param[0] != 4)
		return -1;

/* Magic to enable horizontal scrolling on IntelliMouse 4.0 */
	param[0] = 200;
	ps2_command(ps2dev, param, PSMOUSE_CMD_SETRATE);
	param[0] =  80;
	ps2_command(ps2dev, param, PSMOUSE_CMD_SETRATE);
	param[0] =  40;
	ps2_command(ps2dev, param, PSMOUSE_CMD_SETRATE);

	if (set_properties) {
		__set_bit(BTN_MIDDLE, psmouse->dev->keybit);
		__set_bit(REL_WHEEL, psmouse->dev->relbit);
		__set_bit(REL_HWHEEL, psmouse->dev->relbit);
		__set_bit(BTN_SIDE, psmouse->dev->keybit);
		__set_bit(BTN_EXTRA, psmouse->dev->keybit);

		if (!psmouse->vendor)
			psmouse->vendor = "Generic";
		if (!psmouse->name)
			psmouse->name = "Explorer Mouse";
		psmouse->pktsize = 4;
	}

	return 0;
}

/*
 * Kensington ThinkingMouse / ExpertMouse magic init.
 */
static int thinking_detect(struct psmouse *psmouse, bool set_properties)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	unsigned char param[2];
	static const unsigned char seq[] = { 20, 60, 40, 20, 20, 60, 40, 20, 20 };
	int i;

	param[0] = 10;
	ps2_command(ps2dev, param, PSMOUSE_CMD_SETRATE);
	param[0] = 0;
	ps2_command(ps2dev, param, PSMOUSE_CMD_SETRES);
	for (i = 0; i < ARRAY_SIZE(seq); i++) {
		param[0] = seq[i];
		ps2_command(ps2dev, param, PSMOUSE_CMD_SETRATE);
	}
	ps2_command(ps2dev, param, PSMOUSE_CMD_GETID);

	if (param[0] != 2)
		return -1;

	if (set_properties) {
		__set_bit(BTN_MIDDLE, psmouse->dev->keybit);
		__set_bit(BTN_EXTRA, psmouse->dev->keybit);

		psmouse->vendor = "Kensington";
		psmouse->name = "ThinkingMouse";
	}

	return 0;
}

/*
 * Bare PS/2 protocol "detection". Always succeeds.
 */
static int ps2bare_detect(struct psmouse *psmouse, bool set_properties)
{
	if (set_properties) {
		if (!psmouse->vendor)
			psmouse->vendor = "Generic";
		if (!psmouse->name)
			psmouse->name = "Mouse";

/*
 * We have no way of figuring true number of buttons so let's
 * assume that the device has 3.
 */
		__set_bit(BTN_MIDDLE, psmouse->dev->keybit);
	}

	return 0;
}

/*
 * Cortron PS/2 protocol detection. There's no special way to detect it, so it
 * must be forced by sysfs protocol writing.
 */
static int cortron_detect(struct psmouse *psmouse, bool set_properties)
{
	if (set_properties) {
		psmouse->vendor = "Cortron";
		psmouse->name = "PS/2 Trackball";

		__set_bit(BTN_MIDDLE, psmouse->dev->keybit);
		__set_bit(BTN_SIDE, psmouse->dev->keybit);
	}

	return 0;
}

/*
 * psmouse_extensions() probes for any extensions to the basic PS/2 protocol
 * the mouse may have.
 */

static int psmouse_extensions(struct psmouse *psmouse,
			      unsigned int max_proto, bool set_properties)
{
	bool synaptics_hardware = false;

/*
 * We always check for lifebook because it does not disturb mouse
 * (it only checks DMI information).
 */
	if (lifebook_detect(psmouse, set_properties) == 0) {
		if (max_proto > PSMOUSE_IMEX) {
			if (!set_properties || lifebook_init(psmouse) == 0)
				return PSMOUSE_LIFEBOOK;
		}
	}

/*
 * Try Kensington ThinkingMouse (we try first, because synaptics probe
 * upsets the thinkingmouse).
 */

	if (max_proto > PSMOUSE_IMEX && thinking_detect(psmouse, set_properties) == 0)
		return PSMOUSE_THINKPS;

/*
 * Try Synaptics TouchPad. Note that probing is done even if Synaptics protocol
 * support is disabled in config - we need to know if it is synaptics so we
 * can reset it properly after probing for intellimouse.
 */
	if (max_proto > PSMOUSE_PS2 && synaptics_detect(psmouse, set_properties) == 0) {
		synaptics_hardware = true;

		if (max_proto > PSMOUSE_IMEX) {
/*
 * Try activating protocol, but check if support is enabled first, since
 * we try detecting Synaptics even when protocol is disabled.
 */
			if (synaptics_supported() &&
			    (!set_properties || synaptics_init(psmouse) == 0)) {
				return PSMOUSE_SYNAPTICS;
			}

/*
 * Some Synaptics touchpads can emulate extended protocols (like IMPS/2).
 * Unfortunately Logitech/Genius probes confuse some firmware versions so
 * we'll have to skip them.
 */
			max_proto = PSMOUSE_IMEX;
		}
/*
 * Make sure that touchpad is in relative mode, gestures (taps) are enabled
 */
		synaptics_reset(psmouse);
	}

/*
 * Try ALPS TouchPad
 */
	if (max_proto > PSMOUSE_IMEX) {
		ps2_command(&psmouse->ps2dev, NULL, PSMOUSE_CMD_RESET_DIS);
		if (alps_detect(psmouse, set_properties) == 0) {
			if (!set_properties || alps_init(psmouse) == 0)
				return PSMOUSE_ALPS;
/*
 * Init failed, try basic relative protocols
 */
			max_proto = PSMOUSE_IMEX;
		}
	}

/*
 * Try OLPC HGPK touchpad.
 */
	if (max_proto > PSMOUSE_IMEX &&
			hgpk_detect(psmouse, set_properties) == 0) {
		if (!set_properties || hgpk_init(psmouse) == 0)
			return PSMOUSE_HGPK;
/*
 * Init failed, try basic relative protocols
 */
		max_proto = PSMOUSE_IMEX;
	}

/*
 * Try Elantech touchpad.
 */
	if (max_proto > PSMOUSE_IMEX &&
			elantech_detect(psmouse, set_properties) == 0) {
		if (!set_properties || elantech_init(psmouse) == 0)
			return PSMOUSE_ELANTECH;
/*
 * Init failed, try basic relative protocols
 */
		max_proto = PSMOUSE_IMEX;
	}


	if (max_proto > PSMOUSE_IMEX) {
		if (genius_detect(psmouse, set_properties) == 0)
			return PSMOUSE_GENPS;

		if (ps2pp_init(psmouse, set_properties) == 0)
			return PSMOUSE_PS2PP;

		if (trackpoint_detect(psmouse, set_properties) == 0)
			return PSMOUSE_TRACKPOINT;

		if (touchkit_ps2_detect(psmouse, set_properties) == 0)
			return PSMOUSE_TOUCHKIT_PS2;
	}

/*
 * Try Finger Sensing Pad. We do it here because its probe upsets
 * Trackpoint devices (causing TP_READ_ID command to time out).
 */
	if (max_proto > PSMOUSE_IMEX) {
		if (fsp_detect(psmouse, set_properties) == 0) {
			if (!set_properties || fsp_init(psmouse) == 0)
				return PSMOUSE_FSP;
/*
 * Init failed, try basic relative protocols
 */
			max_proto = PSMOUSE_IMEX;
		}
	}

/*
 * Reset to defaults in case the device got confused by extended
 * protocol probes. Note that we follow up with full reset because
 * some mice put themselves to sleep when they see PSMOUSE_RESET_DIS.
 */
	ps2_command(&psmouse->ps2dev, NULL, PSMOUSE_CMD_RESET_DIS);
	psmouse_reset(psmouse);

	if (max_proto >= PSMOUSE_IMEX && im_explorer_detect(psmouse, set_properties) == 0)
		return PSMOUSE_IMEX;

	if (max_proto >= PSMOUSE_IMPS && intellimouse_detect(psmouse, set_properties) == 0)
		return PSMOUSE_IMPS;

/*
 * Okay, all failed, we have a standard mouse here. The number of the buttons
 * is still a question, though. We assume 3.
 */
	ps2bare_detect(psmouse, set_properties);

	if (synaptics_hardware) {
/*
 * We detected Synaptics hardware but it did not respond to IMPS/2 probes.
 * We need to reset the touchpad because if there is a track point on the
 * pass through port it could get disabled while probing for protocol
 * extensions.
 */
		psmouse_reset(psmouse);
	}

	return PSMOUSE_PS2;
}

static const struct psmouse_protocol psmouse_protocols[] = {
	{
		.type		= PSMOUSE_PS2,
		.name		= "PS/2",
		.alias		= "bare",
		.maxproto	= true,
		.ignore_parity	= true,
		.detect		= ps2bare_detect,
	},
#ifdef CONFIG_MOUSE_PS2_LOGIPS2PP
	{
		.type		= PSMOUSE_PS2PP,
		.name		= "PS2++",
		.alias		= "logitech",
		.detect		= ps2pp_init,
	},
#endif
	{
		.type		= PSMOUSE_THINKPS,
		.name		= "ThinkPS/2",
		.alias		= "thinkps",
		.detect		= thinking_detect,
	},
	{
		.type		= PSMOUSE_GENPS,
		.name		= "GenPS/2",
		.alias		= "genius",
		.detect		= genius_detect,
	},
	{
		.type		= PSMOUSE_IMPS,
		.name		= "ImPS/2",
		.alias		= "imps",
		.maxproto	= true,
		.ignore_parity	= true,
		.detect		= intellimouse_detect,
	},
	{
		.type		= PSMOUSE_IMEX,
		.name		= "ImExPS/2",
		.alias		= "exps",
		.maxproto	= true,
		.ignore_parity	= true,
		.detect		= im_explorer_detect,
	},
#ifdef CONFIG_MOUSE_PS2_SYNAPTICS
	{
		.type		= PSMOUSE_SYNAPTICS,
		.name		= "SynPS/2",
		.alias		= "synaptics",
		.detect		= synaptics_detect,
		.init		= synaptics_init,
	},
#endif
#ifdef CONFIG_MOUSE_PS2_ALPS
	{
		.type		= PSMOUSE_ALPS,
		.name		= "AlpsPS/2",
		.alias		= "alps",
		.detect		= alps_detect,
		.init		= alps_init,
	},
#endif
#ifdef CONFIG_MOUSE_PS2_LIFEBOOK
	{
		.type		= PSMOUSE_LIFEBOOK,
		.name		= "LBPS/2",
		.alias		= "lifebook",
		.init		= lifebook_init,
	},
#endif
#ifdef CONFIG_MOUSE_PS2_TRACKPOINT
	{
		.type		= PSMOUSE_TRACKPOINT,
		.name		= "TPPS/2",
		.alias		= "trackpoint",
		.detect		= trackpoint_detect,
	},
#endif
#ifdef CONFIG_MOUSE_PS2_TOUCHKIT
	{
		.type		= PSMOUSE_TOUCHKIT_PS2,
		.name		= "touchkitPS/2",
		.alias		= "touchkit",
		.detect		= touchkit_ps2_detect,
	},
#endif
#ifdef CONFIG_MOUSE_PS2_OLPC
	{
		.type		= PSMOUSE_HGPK,
		.name		= "OLPC HGPK",
		.alias		= "hgpk",
		.detect		= hgpk_detect,
	},
#endif
#ifdef CONFIG_MOUSE_PS2_ELANTECH
	{
		.type		= PSMOUSE_ELANTECH,
		.name		= "ETPS/2",
		.alias		= "elantech",
		.detect		= elantech_detect,
		.init		= elantech_init,
	},
#endif
#ifdef CONFIG_MOUSE_PS2_SENTELIC
	{
		.type		= PSMOUSE_FSP,
		.name		= "FSPPS/2",
		.alias		= "fsp",
		.detect		= fsp_detect,
		.init		= fsp_init,
	},
#endif
	{
		.type		= PSMOUSE_CORTRON,
		.name		= "CortronPS/2",
		.alias		= "cortps",
		.detect		= cortron_detect,
	},
	{
		.type		= PSMOUSE_AUTO,
		.name		= "auto",
		.alias		= "any",
		.maxproto	= true,
	},
};

static const struct psmouse_protocol *psmouse_protocol_by_type(enum psmouse_type type)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(psmouse_protocols); i++)
		if (psmouse_protocols[i].type == type)
			return &psmouse_protocols[i];

	WARN_ON(1);
	return &psmouse_protocols[0];
}

static const struct psmouse_protocol *psmouse_protocol_by_name(const char *name, size_t len)
{
	const struct psmouse_protocol *p;
	int i;

	for (i = 0; i < ARRAY_SIZE(psmouse_protocols); i++) {
		p = &psmouse_protocols[i];

		if ((strlen(p->name) == len && !strncmp(p->name, name, len)) ||
		    (strlen(p->alias) == len && !strncmp(p->alias, name, len)))
			return &psmouse_protocols[i];
	}

	return NULL;
}


/*
 * psmouse_probe() probes for a PS/2 mouse.
 */

static int psmouse_probe(struct psmouse *psmouse)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	unsigned char param[2];

/*
 * First, we check if it's a mouse. It should send 0x00 or 0x03
 * in case of an IntelliMouse in 4-byte mode or 0x04 for IM Explorer.
 * Sunrex K8561 IR Keyboard/Mouse reports 0xff on second and subsequent
 * ID queries, probably due to a firmware bug.
 */

	param[0] = 0xa5;
	if (ps2_command(ps2dev, param, PSMOUSE_CMD_GETID))
		return -1;

	if (param[0] != 0x00 && param[0] != 0x03 &&
	    param[0] != 0x04 && param[0] != 0xff)
		return -1;

/*
 * Then we reset and disable the mouse so that it doesn't generate events.
 */

	if (ps2_command(ps2dev, NULL, PSMOUSE_CMD_RESET_DIS))
		printk(KERN_WARNING "psmouse.c: Failed to reset mouse on %s\n", ps2dev->serio->phys);

	return 0;
}

/*
 * Here we set the mouse resolution.
 */

void psmouse_set_resolution(struct psmouse *psmouse, unsigned int resolution)
{
	static const unsigned char params[] = { 0, 1, 2, 2, 3 };
	unsigned char p;

	if (resolution == 0 || resolution > 200)
		resolution = 200;

	p = params[resolution / 50];
	ps2_command(&psmouse->ps2dev, &p, PSMOUSE_CMD_SETRES);
	psmouse->resolution = 25 << p;
}

/*
 * Here we set the mouse report rate.
 */

static void psmouse_set_rate(struct psmouse *psmouse, unsigned int rate)
{
	static const unsigned char rates[] = { 200, 100, 80, 60, 40, 20, 10, 0 };
	unsigned char r;
	int i = 0;

	while (rates[i] > rate) i++;
	r = rates[i];
	ps2_command(&psmouse->ps2dev, &r, PSMOUSE_CMD_SETRATE);
	psmouse->rate = r;
}

/*
 * psmouse_initialize() initializes the mouse to a sane state.
 */

static void psmouse_initialize(struct psmouse *psmouse)
{
/*
 * We set the mouse report rate, resolution and scaling.
 */

	if (psmouse_max_proto != PSMOUSE_PS2) {
		psmouse->set_rate(psmouse, psmouse->rate);
		psmouse->set_resolution(psmouse, psmouse->resolution);
		ps2_command(&psmouse->ps2dev, NULL, PSMOUSE_CMD_SETSCALE11);
	}
}

/*
 * psmouse_activate() enables the mouse so that we get motion reports from it.
 */

static void psmouse_activate(struct psmouse *psmouse)
{
	if (ps2_command(&psmouse->ps2dev, NULL, PSMOUSE_CMD_ENABLE))
		printk(KERN_WARNING "psmouse.c: Failed to enable mouse on %s\n",
			psmouse->ps2dev.serio->phys);

	psmouse_set_state(psmouse, PSMOUSE_ACTIVATED);
}


/*
 * psmouse_deactivate() puts the mouse into poll mode so that we don't get motion
 * reports from it unless we explicitly request it.
 */

static void psmouse_deactivate(struct psmouse *psmouse)
{
	if (ps2_command(&psmouse->ps2dev, NULL, PSMOUSE_CMD_DISABLE))
		printk(KERN_WARNING "psmouse.c: Failed to deactivate mouse on %s\n",
			psmouse->ps2dev.serio->phys);

	psmouse_set_state(psmouse, PSMOUSE_CMD_MODE);
}

/*
 * psmouse_poll() - default poll hanlder. Everyone except for ALPS uses it.
 */

static int psmouse_poll(struct psmouse *psmouse)
{
	return ps2_command(&psmouse->ps2dev, psmouse->packet,
			   PSMOUSE_CMD_POLL | (psmouse->pktsize << 8));
}


/*
 * psmouse_resync() attempts to re-validate current protocol.
 */

static void psmouse_resync(struct work_struct *work)
{
	struct psmouse *parent = NULL, *psmouse =
		container_of(work, struct psmouse, resync_work.work);
	struct serio *serio = psmouse->ps2dev.serio;
	psmouse_ret_t rc = PSMOUSE_GOOD_DATA;
	bool failed = false, enabled = false;
	int i;

	mutex_lock(&psmouse_mutex);

	if (psmouse->state != PSMOUSE_RESYNCING)
		goto out;

	if (serio->parent && serio->id.type == SERIO_PS_PSTHRU) {
		parent = serio_get_drvdata(serio->parent);
		psmouse_deactivate(parent);
	}

/*
 * Some mice don't ACK commands sent while they are in the middle of
 * transmitting motion packet. To avoid delay we use ps2_sendbyte()
 * instead of ps2_command() which would wait for 200ms for an ACK
 * that may never come.
 * As an additional quirk ALPS touchpads may not only forget to ACK
 * disable command but will stop reporting taps, so if we see that
 * mouse at least once ACKs disable we will do full reconnect if ACK
 * is missing.
 */
	psmouse->num_resyncs++;

	if (ps2_sendbyte(&psmouse->ps2dev, PSMOUSE_CMD_DISABLE, 20)) {
		if (psmouse->num_resyncs < 3 || psmouse->acks_disable_command)
			failed = true;
	} else
		psmouse->acks_disable_command = true;

/*
 * Poll the mouse. If it was reset the packet will be shorter than
 * psmouse->pktsize and ps2_command will fail. We do not expect and
 * do not handle scenario when mouse "upgrades" its protocol while
 * disconnected since it would require additional delay. If we ever
 * see a mouse that does it we'll adjust the code.
 */
	if (!failed) {
		if (psmouse->poll(psmouse))
			failed = true;
		else {
			psmouse_set_state(psmouse, PSMOUSE_CMD_MODE);
			for (i = 0; i < psmouse->pktsize; i++) {
				psmouse->pktcnt++;
				rc = psmouse->protocol_handler(psmouse);
				if (rc != PSMOUSE_GOOD_DATA)
					break;
			}
			if (rc != PSMOUSE_FULL_PACKET)
				failed = true;
			psmouse_set_state(psmouse, PSMOUSE_RESYNCING);
		}
	}
/*
 * Now try to enable mouse. We try to do that even if poll failed and also
 * repeat our attempts 5 times, otherwise we may be left out with disabled
 * mouse.
 */
	for (i = 0; i < 5; i++) {
		if (!ps2_command(&psmouse->ps2dev, NULL, PSMOUSE_CMD_ENABLE)) {
			enabled = true;
			break;
		}
		msleep(200);
	}

	if (!enabled) {
		printk(KERN_WARNING "psmouse.c: failed to re-enable mouse on %s\n",
			psmouse->ps2dev.serio->phys);
		failed = true;
	}

	if (failed) {
		psmouse_set_state(psmouse, PSMOUSE_IGNORE);
		printk(KERN_INFO "psmouse.c: resync failed, issuing reconnect request\n");
		serio_reconnect(serio);
	} else
		psmouse_set_state(psmouse, PSMOUSE_ACTIVATED);

	if (parent)
		psmouse_activate(parent);
 out:
	mutex_unlock(&psmouse_mutex);
}

/*
 * psmouse_cleanup() resets the mouse into power-on state.
 */

static void psmouse_cleanup(struct serio *serio)
{
	struct psmouse *psmouse = serio_get_drvdata(serio);
	struct psmouse *parent = NULL;

	mutex_lock(&psmouse_mutex);

	if (serio->parent && serio->id.type == SERIO_PS_PSTHRU) {
		parent = serio_get_drvdata(serio->parent);
		psmouse_deactivate(parent);
	}

	psmouse_set_state(psmouse, PSMOUSE_INITIALIZING);

	/*
	 * Disable stream mode so cleanup routine can proceed undisturbed.
	 */
	if (ps2_command(&psmouse->ps2dev, NULL, PSMOUSE_CMD_DISABLE))
		printk(KERN_WARNING "psmouse.c: Failed to disable mouse on %s\n",
			psmouse->ps2dev.serio->phys);

	if (psmouse->cleanup)
		psmouse->cleanup(psmouse);

/*
 * Reset the mouse to defaults (bare PS/2 protocol).
 */
	ps2_command(&psmouse->ps2dev, NULL, PSMOUSE_CMD_RESET_DIS);

/*
 * Some boxes, such as HP nx7400, get terribly confused if mouse
 * is not fully enabled before suspending/shutting down.
 */
	ps2_command(&psmouse->ps2dev, NULL, PSMOUSE_CMD_ENABLE);

	if (parent) {
		if (parent->pt_deactivate)
			parent->pt_deactivate(parent);

		psmouse_activate(parent);
	}

	mutex_unlock(&psmouse_mutex);
}

/*
 * psmouse_disconnect() closes and frees.
 */

static void psmouse_disconnect(struct serio *serio)
{
	struct psmouse *psmouse, *parent = NULL;

	psmouse = serio_get_drvdata(serio);

	sysfs_remove_group(&serio->dev.kobj, &psmouse_attribute_group);

	mutex_lock(&psmouse_mutex);

	psmouse_set_state(psmouse, PSMOUSE_CMD_MODE);

	/* make sure we don't have a resync in progress */
	mutex_unlock(&psmouse_mutex);
	flush_workqueue(kpsmoused_wq);
	mutex_lock(&psmouse_mutex);

	if (serio->parent && serio->id.type == SERIO_PS_PSTHRU) {
		parent = serio_get_drvdata(serio->parent);
		psmouse_deactivate(parent);
	}

	if (psmouse->disconnect)
		psmouse->disconnect(psmouse);

	if (parent && parent->pt_deactivate)
		parent->pt_deactivate(parent);

	psmouse_set_state(psmouse, PSMOUSE_IGNORE);

	serio_close(serio);
	serio_set_drvdata(serio, NULL);
	input_unregister_device(psmouse->dev);
	kfree(psmouse);

	if (parent)
		psmouse_activate(parent);

	mutex_unlock(&psmouse_mutex);
}

static int psmouse_switch_protocol(struct psmouse *psmouse,
				   const struct psmouse_protocol *proto)
{
	const struct psmouse_protocol *selected_proto;
	struct input_dev *input_dev = psmouse->dev;

	input_dev->dev.parent = &psmouse->ps2dev.serio->dev;

	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REL);
	input_dev->keybit[BIT_WORD(BTN_MOUSE)] =
				BIT_MASK(BTN_LEFT) | BIT_MASK(BTN_RIGHT);
	input_dev->relbit[0] = BIT_MASK(REL_X) | BIT_MASK(REL_Y);

	psmouse->set_rate = psmouse_set_rate;
	psmouse->set_resolution = psmouse_set_resolution;
	psmouse->poll = psmouse_poll;
	psmouse->protocol_handler = psmouse_process_byte;
	psmouse->pktsize = 3;

	if (proto && (proto->detect || proto->init)) {
		if (proto->detect && proto->detect(psmouse, true) < 0)
			return -1;

		if (proto->init && proto->init(psmouse) < 0)
			return -1;

		psmouse->type = proto->type;
		selected_proto = proto;
	} else {
		psmouse->type = psmouse_extensions(psmouse,
						   psmouse_max_proto, true);
		selected_proto = psmouse_protocol_by_type(psmouse->type);
	}

	psmouse->ignore_parity = selected_proto->ignore_parity;

	/*
	 * If mouse's packet size is 3 there is no point in polling the
	 * device in hopes to detect protocol reset - we won't get less
	 * than 3 bytes response anyhow.
	 */
	if (psmouse->pktsize == 3)
		psmouse->resync_time = 0;

	/*
	 * Some smart KVMs fake response to POLL command returning just
	 * 3 bytes and messing up our resync logic, so if initial poll
	 * fails we won't try polling the device anymore. Hopefully
	 * such KVM will maintain initially selected protocol.
	 */
	if (psmouse->resync_time && psmouse->poll(psmouse))
		psmouse->resync_time = 0;

	snprintf(psmouse->devname, sizeof(psmouse->devname), "%s %s %s",
		 selected_proto->name, psmouse->vendor, psmouse->name);

	input_dev->name = psmouse->devname;
	input_dev->phys = psmouse->phys;
	input_dev->id.bustype = BUS_I8042;
	input_dev->id.vendor = 0x0002;
	input_dev->id.product = psmouse->type;
	input_dev->id.version = psmouse->model;

	return 0;
}

/*
 * psmouse_connect() is a callback from the serio module when
 * an unhandled serio port is found.
 */
static int psmouse_connect(struct serio *serio, struct serio_driver *drv)
{
	struct psmouse *psmouse, *parent = NULL;
	struct input_dev *input_dev;
	int retval = 0, error = -ENOMEM;

	mutex_lock(&psmouse_mutex);

	/*
	 * If this is a pass-through port deactivate parent so the device
	 * connected to this port can be successfully identified
	 */
	if (serio->parent && serio->id.type == SERIO_PS_PSTHRU) {
		parent = serio_get_drvdata(serio->parent);
		psmouse_deactivate(parent);
	}

	psmouse = kzalloc(sizeof(struct psmouse), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!psmouse || !input_dev)
		goto err_free;

	ps2_init(&psmouse->ps2dev, serio);
	INIT_DELAYED_WORK(&psmouse->resync_work, psmouse_resync);
	psmouse->dev = input_dev;
	snprintf(psmouse->phys, sizeof(psmouse->phys), "%s/input0", serio->phys);

	psmouse_set_state(psmouse, PSMOUSE_INITIALIZING);

	serio_set_drvdata(serio, psmouse);

	error = serio_open(serio, drv);
	if (error)
		goto err_clear_drvdata;

	if (psmouse_probe(psmouse) < 0) {
		error = -ENODEV;
		goto err_close_serio;
	}

	psmouse->rate = psmouse_rate;
	psmouse->resolution = psmouse_resolution;
	psmouse->resetafter = psmouse_resetafter;
	psmouse->resync_time = parent ? 0 : psmouse_resync_time;
	psmouse->smartscroll = psmouse_smartscroll;

	psmouse_switch_protocol(psmouse, NULL);

	psmouse_set_state(psmouse, PSMOUSE_CMD_MODE);
	psmouse_initialize(psmouse);

	error = input_register_device(psmouse->dev);
	if (error)
		goto err_protocol_disconnect;

	if (parent && parent->pt_activate)
		parent->pt_activate(parent);

	error = sysfs_create_group(&serio->dev.kobj, &psmouse_attribute_group);
	if (error)
		goto err_pt_deactivate;

	psmouse_activate(psmouse);

 out:
	/* If this is a pass-through port the parent needs to be re-activated */
	if (parent)
		psmouse_activate(parent);

	mutex_unlock(&psmouse_mutex);
	return retval;

 err_pt_deactivate:
	if (parent && parent->pt_deactivate)
		parent->pt_deactivate(parent);
	input_unregister_device(psmouse->dev);
	input_dev = NULL; /* so we don't try to free it below */
 err_protocol_disconnect:
	if (psmouse->disconnect)
		psmouse->disconnect(psmouse);
	psmouse_set_state(psmouse, PSMOUSE_IGNORE);
 err_close_serio:
	serio_close(serio);
 err_clear_drvdata:
	serio_set_drvdata(serio, NULL);
 err_free:
	input_free_device(input_dev);
	kfree(psmouse);

	retval = error;
	goto out;
}


static int psmouse_reconnect(struct serio *serio)
{
	struct psmouse *psmouse = serio_get_drvdata(serio);
	struct psmouse *parent = NULL;
	struct serio_driver *drv = serio->drv;
	unsigned char type;
	int rc = -1;

	if (!drv || !psmouse) {
		printk(KERN_DEBUG "psmouse: reconnect request, but serio is disconnected, ignoring...\n");
		return -1;
	}

	mutex_lock(&psmouse_mutex);

	if (serio->parent && serio->id.type == SERIO_PS_PSTHRU) {
		parent = serio_get_drvdata(serio->parent);
		psmouse_deactivate(parent);
	}

	psmouse_set_state(psmouse, PSMOUSE_INITIALIZING);

	if (psmouse->reconnect) {
		if (psmouse->reconnect(psmouse))
			goto out;
	} else {
		psmouse_reset(psmouse);

		if (psmouse_probe(psmouse) < 0)
			goto out;

		type = psmouse_extensions(psmouse, psmouse_max_proto, false);
		if (psmouse->type != type)
			goto out;
	}

	/* ok, the device type (and capabilities) match the old one,
	 * we can continue using it, complete intialization
	 */
	psmouse_set_state(psmouse, PSMOUSE_CMD_MODE);

	psmouse_initialize(psmouse);

	if (parent && parent->pt_activate)
		parent->pt_activate(parent);

	psmouse_activate(psmouse);
	rc = 0;

out:
	/* If this is a pass-through port the parent waits to be activated */
	if (parent)
		psmouse_activate(parent);

	mutex_unlock(&psmouse_mutex);
	return rc;
}

static struct serio_device_id psmouse_serio_ids[] = {
	{
		.type	= SERIO_8042,
		.proto	= SERIO_ANY,
		.id	= SERIO_ANY,
		.extra	= SERIO_ANY,
	},
	{
		.type	= SERIO_PS_PSTHRU,
		.proto	= SERIO_ANY,
		.id	= SERIO_ANY,
		.extra	= SERIO_ANY,
	},
	{ 0 }
};

MODULE_DEVICE_TABLE(serio, psmouse_serio_ids);

static struct serio_driver psmouse_drv = {
	.driver		= {
		.name	= "psmouse",
	},
	.description	= DRIVER_DESC,
	.id_table	= psmouse_serio_ids,
	.interrupt	= psmouse_interrupt,
	.connect	= psmouse_connect,
	.reconnect	= psmouse_reconnect,
	.disconnect	= psmouse_disconnect,
	.cleanup	= psmouse_cleanup,
};

ssize_t psmouse_attr_show_helper(struct device *dev, struct device_attribute *devattr,
				 char *buf)
{
	struct serio *serio = to_serio_port(dev);
	struct psmouse_attribute *attr = to_psmouse_attr(devattr);
	struct psmouse *psmouse;

	psmouse = serio_get_drvdata(serio);

	return attr->show(psmouse, attr->data, buf);
}

ssize_t psmouse_attr_set_helper(struct device *dev, struct device_attribute *devattr,
				const char *buf, size_t count)
{
	struct serio *serio = to_serio_port(dev);
	struct psmouse_attribute *attr = to_psmouse_attr(devattr);
	struct psmouse *psmouse, *parent = NULL;
	int retval;

	retval = mutex_lock_interruptible(&psmouse_mutex);
	if (retval)
		goto out;

	psmouse = serio_get_drvdata(serio);

	if (attr->protect) {
		if (psmouse->state == PSMOUSE_IGNORE) {
			retval = -ENODEV;
			goto out_unlock;
		}

		if (serio->parent && serio->id.type == SERIO_PS_PSTHRU) {
			parent = serio_get_drvdata(serio->parent);
			psmouse_deactivate(parent);
		}

		psmouse_deactivate(psmouse);
	}

	retval = attr->set(psmouse, attr->data, buf, count);

	if (attr->protect) {
		if (retval != -ENODEV)
			psmouse_activate(psmouse);

		if (parent)
			psmouse_activate(parent);
	}

 out_unlock:
	mutex_unlock(&psmouse_mutex);
 out:
	return retval;
}

static ssize_t psmouse_show_int_attr(struct psmouse *psmouse, void *offset, char *buf)
{
	unsigned int *field = (unsigned int *)((char *)psmouse + (size_t)offset);

	return sprintf(buf, "%u\n", *field);
}

static ssize_t psmouse_set_int_attr(struct psmouse *psmouse, void *offset, const char *buf, size_t count)
{
	unsigned int *field = (unsigned int *)((char *)psmouse + (size_t)offset);
	unsigned long value;

	if (strict_strtoul(buf, 10, &value))
		return -EINVAL;

	if ((unsigned int)value != value)
		return -EINVAL;

	*field = value;

	return count;
}

static ssize_t psmouse_attr_show_protocol(struct psmouse *psmouse, void *data, char *buf)
{
	return sprintf(buf, "%s\n", psmouse_protocol_by_type(psmouse->type)->name);
}

static ssize_t psmouse_attr_set_protocol(struct psmouse *psmouse, void *data, const char *buf, size_t count)
{
	struct serio *serio = psmouse->ps2dev.serio;
	struct psmouse *parent = NULL;
	struct input_dev *old_dev, *new_dev;
	const struct psmouse_protocol *proto, *old_proto;
	int error;
	int retry = 0;

	proto = psmouse_protocol_by_name(buf, count);
	if (!proto)
		return -EINVAL;

	if (psmouse->type == proto->type)
		return count;

	new_dev = input_allocate_device();
	if (!new_dev)
		return -ENOMEM;

	while (serio->child) {
		if (++retry > 3) {
			printk(KERN_WARNING
				"psmouse: failed to destroy child port, "
				"protocol change aborted.\n");
			input_free_device(new_dev);
			return -EIO;
		}

		mutex_unlock(&psmouse_mutex);
		serio_unregister_child_port(serio);
		mutex_lock(&psmouse_mutex);

		if (serio->drv != &psmouse_drv) {
			input_free_device(new_dev);
			return -ENODEV;
		}

		if (psmouse->type == proto->type) {
			input_free_device(new_dev);
			return count; /* switched by other thread */
		}
	}

	if (serio->parent && serio->id.type == SERIO_PS_PSTHRU) {
		parent = serio_get_drvdata(serio->parent);
		if (parent->pt_deactivate)
			parent->pt_deactivate(parent);
	}

	old_dev = psmouse->dev;
	old_proto = psmouse_protocol_by_type(psmouse->type);

	if (psmouse->disconnect)
		psmouse->disconnect(psmouse);

	psmouse_set_state(psmouse, PSMOUSE_IGNORE);

	psmouse->dev = new_dev;
	psmouse_set_state(psmouse, PSMOUSE_INITIALIZING);

	if (psmouse_switch_protocol(psmouse, proto) < 0) {
		psmouse_reset(psmouse);
		/* default to PSMOUSE_PS2 */
		psmouse_switch_protocol(psmouse, &psmouse_protocols[0]);
	}

	psmouse_initialize(psmouse);
	psmouse_set_state(psmouse, PSMOUSE_CMD_MODE);

	error = input_register_device(psmouse->dev);
	if (error) {
		if (psmouse->disconnect)
			psmouse->disconnect(psmouse);

		psmouse_set_state(psmouse, PSMOUSE_IGNORE);
		input_free_device(new_dev);
		psmouse->dev = old_dev;
		psmouse_set_state(psmouse, PSMOUSE_INITIALIZING);
		psmouse_switch_protocol(psmouse, old_proto);
		psmouse_initialize(psmouse);
		psmouse_set_state(psmouse, PSMOUSE_CMD_MODE);

		return error;
	}

	input_unregister_device(old_dev);

	if (parent && parent->pt_activate)
		parent->pt_activate(parent);

	return count;
}

static ssize_t psmouse_attr_set_rate(struct psmouse *psmouse, void *data, const char *buf, size_t count)
{
	unsigned long value;

	if (strict_strtoul(buf, 10, &value))
		return -EINVAL;

	psmouse->set_rate(psmouse, value);
	return count;
}

static ssize_t psmouse_attr_set_resolution(struct psmouse *psmouse, void *data, const char *buf, size_t count)
{
	unsigned long value;

	if (strict_strtoul(buf, 10, &value))
		return -EINVAL;

	psmouse->set_resolution(psmouse, value);
	return count;
}


static int psmouse_set_maxproto(const char *val, const struct kernel_param *kp)
{
	const struct psmouse_protocol *proto;

	if (!val)
		return -EINVAL;

	proto = psmouse_protocol_by_name(val, strlen(val));

	if (!proto || !proto->maxproto)
		return -EINVAL;

	*((unsigned int *)kp->arg) = proto->type;

	return 0;
}

static int psmouse_get_maxproto(char *buffer, const struct kernel_param *kp)
{
	int type = *((unsigned int *)kp->arg);

	return sprintf(buffer, "%s", psmouse_protocol_by_type(type)->name);
}

static int __init psmouse_init(void)
{
	int err;

	lifebook_module_init();
	synaptics_module_init();

	kpsmoused_wq = create_singlethread_workqueue("kpsmoused");
	if (!kpsmoused_wq) {
		printk(KERN_ERR "psmouse: failed to create kpsmoused workqueue\n");
		return -ENOMEM;
	}

	err = serio_register_driver(&psmouse_drv);
	if (err)
		destroy_workqueue(kpsmoused_wq);

	return err;
}

static void __exit psmouse_exit(void)
{
	serio_unregister_driver(&psmouse_drv);
	destroy_workqueue(kpsmoused_wq);
}

module_init(psmouse_init);
module_exit(psmouse_exit);
