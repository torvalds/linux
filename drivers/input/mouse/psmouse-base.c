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
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/serio.h>
#include <linux/init.h>
#include <linux/libps2.h>
#include "psmouse.h"
#include "synaptics.h"
#include "logips2pp.h"
#include "alps.h"

#define DRIVER_DESC	"PS/2 mouse driver"

MODULE_AUTHOR("Vojtech Pavlik <vojtech@suse.cz>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

static unsigned int psmouse_max_proto = -1U;
static int psmouse_set_maxproto(const char *val, struct kernel_param *kp);
static int psmouse_get_maxproto(char *buffer, struct kernel_param *kp);
static char *psmouse_proto_abbrev[] = { NULL, "bare", NULL, NULL, NULL, "imps", "exps", NULL, NULL, NULL };
#define param_check_proto_abbrev(name, p)	__param_check(name, p, unsigned int)
#define param_set_proto_abbrev			psmouse_set_maxproto
#define param_get_proto_abbrev			psmouse_get_maxproto
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

static unsigned int psmouse_resetafter;
module_param_named(resetafter, psmouse_resetafter, uint, 0644);
MODULE_PARM_DESC(resetafter, "Reset device after so many bad packets (0 = never).");

PSMOUSE_DEFINE_ATTR(rate);
PSMOUSE_DEFINE_ATTR(resolution);
PSMOUSE_DEFINE_ATTR(resetafter);

__obsolete_setup("psmouse_noext");
__obsolete_setup("psmouse_resolution=");
__obsolete_setup("psmouse_smartscroll=");
__obsolete_setup("psmouse_resetafter=");
__obsolete_setup("psmouse_rate=");

static char *psmouse_protocols[] = { "None", "PS/2", "PS2++", "ThinkPS/2", "GenPS/2", "ImPS/2", "ImExPS/2", "SynPS/2", "AlpsPS/2" };

/*
 * psmouse_process_byte() analyzes the PS/2 data stream and reports
 * relevant events to the input module once full packet has arrived.
 */

static psmouse_ret_t psmouse_process_byte(struct psmouse *psmouse, struct pt_regs *regs)
{
	struct input_dev *dev = &psmouse->dev;
	unsigned char *packet = psmouse->packet;

	if (psmouse->pktcnt < psmouse->pktsize)
		return PSMOUSE_GOOD_DATA;

/*
 * Full packet accumulated, process it
 */

	input_regs(dev, regs);

/*
 * Scroll wheel on IntelliMice, scroll buttons on NetMice
 */

	if (psmouse->type == PSMOUSE_IMPS || psmouse->type == PSMOUSE_GENPS)
		input_report_rel(dev, REL_WHEEL, -(signed char) packet[3]);

/*
 * Scroll wheel and buttons on IntelliMouse Explorer
 */

	if (psmouse->type == PSMOUSE_IMEX) {
		input_report_rel(dev, REL_WHEEL, (int) (packet[3] & 8) - (int) (packet[3] & 7));
		input_report_key(dev, BTN_SIDE, (packet[3] >> 4) & 1);
		input_report_key(dev, BTN_EXTRA, (packet[3] >> 5) & 1);
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

/*
 * psmouse_interrupt() handles incoming characters, either gathering them into
 * packets or passing them to the command routine as command output.
 */

static irqreturn_t psmouse_interrupt(struct serio *serio,
		unsigned char data, unsigned int flags, struct pt_regs *regs)
{
	struct psmouse *psmouse = serio_get_drvdata(serio);
	psmouse_ret_t rc;

	if (psmouse->state == PSMOUSE_IGNORE)
		goto out;

	if (flags & (SERIO_PARITY|SERIO_TIMEOUT)) {
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

	if (psmouse->state == PSMOUSE_INITIALIZING)
		goto out;

	if (psmouse->state == PSMOUSE_ACTIVATED &&
	    psmouse->pktcnt && time_after(jiffies, psmouse->last + HZ/2)) {
		printk(KERN_WARNING "psmouse.c: %s at %s lost synchronization, throwing %d bytes away.\n",
		       psmouse->name, psmouse->phys, psmouse->pktcnt);
		psmouse->pktcnt = 0;
	}

	psmouse->last = jiffies;
	psmouse->packet[psmouse->pktcnt++] = data;

	if (psmouse->packet[0] == PSMOUSE_RET_BAT) {
		if (psmouse->pktcnt == 1)
			goto out;

		if (psmouse->pktcnt == 2) {
			if (psmouse->packet[1] == PSMOUSE_RET_ID) {
				psmouse->state = PSMOUSE_IGNORE;
				serio_reconnect(serio);
				goto out;
			}
			if (psmouse->type == PSMOUSE_SYNAPTICS) {
				/* neither 0xAA nor 0x00 are valid first bytes
				 * for a packet in absolute mode
				 */
				psmouse->pktcnt = 0;
				goto out;
			}
		}
	}

	rc = psmouse->protocol_handler(psmouse, regs);

	switch (rc) {
		case PSMOUSE_BAD_DATA:
			printk(KERN_WARNING "psmouse.c: %s at %s lost sync at byte %d\n",
				psmouse->name, psmouse->phys, psmouse->pktcnt);
			psmouse->pktcnt = 0;

			if (++psmouse->out_of_sync == psmouse->resetafter) {
				psmouse->state = PSMOUSE_IGNORE;
				printk(KERN_NOTICE "psmouse.c: issuing reconnect request\n");
				serio_reconnect(psmouse->ps2dev.serio);
			}
			break;

		case PSMOUSE_FULL_PACKET:
			psmouse->pktcnt = 0;
			if (psmouse->out_of_sync) {
				psmouse->out_of_sync = 0;
				printk(KERN_NOTICE "psmouse.c: %s at %s - driver resynched.\n",
					psmouse->name, psmouse->phys);
			}
			break;

		case PSMOUSE_GOOD_DATA:
			break;
	}
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
static int genius_detect(struct psmouse *psmouse, int set_properties)
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
		set_bit(BTN_EXTRA, psmouse->dev.keybit);
		set_bit(BTN_SIDE, psmouse->dev.keybit);
		set_bit(REL_WHEEL, psmouse->dev.relbit);

		psmouse->vendor = "Genius";
		psmouse->name = "Wheel Mouse";
		psmouse->pktsize = 4;
	}

	return 0;
}

/*
 * IntelliMouse magic init.
 */
static int intellimouse_detect(struct psmouse *psmouse, int set_properties)
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
		set_bit(REL_WHEEL, psmouse->dev.relbit);

		if (!psmouse->vendor) psmouse->vendor = "Generic";
		if (!psmouse->name) psmouse->name = "Wheel Mouse";
		psmouse->pktsize = 4;
	}

	return 0;
}

/*
 * Try IntelliMouse/Explorer magic init.
 */
static int im_explorer_detect(struct psmouse *psmouse, int set_properties)
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

	if (set_properties) {
		set_bit(REL_WHEEL, psmouse->dev.relbit);
		set_bit(BTN_SIDE, psmouse->dev.keybit);
		set_bit(BTN_EXTRA, psmouse->dev.keybit);

		if (!psmouse->vendor) psmouse->vendor = "Generic";
		if (!psmouse->name) psmouse->name = "Explorer Mouse";
		psmouse->pktsize = 4;
	}

	return 0;
}

/*
 * Kensington ThinkingMouse / ExpertMouse magic init.
 */
static int thinking_detect(struct psmouse *psmouse, int set_properties)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	unsigned char param[2];
	unsigned char seq[] = { 20, 60, 40, 20, 20, 60, 40, 20, 20, 0 };
	int i;

	param[0] = 10;
	ps2_command(ps2dev, param, PSMOUSE_CMD_SETRATE);
	param[0] = 0;
	ps2_command(ps2dev, param, PSMOUSE_CMD_SETRES);
	for (i = 0; seq[i]; i++)
		ps2_command(ps2dev, seq + i, PSMOUSE_CMD_SETRATE);
	ps2_command(ps2dev, param, PSMOUSE_CMD_GETID);

	if (param[0] != 2)
		return -1;

	if (set_properties) {
		set_bit(BTN_EXTRA, psmouse->dev.keybit);

		psmouse->vendor = "Kensington";
		psmouse->name = "ThinkingMouse";
	}

	return 0;
}

/*
 * Bare PS/2 protocol "detection". Always succeeds.
 */
static int ps2bare_detect(struct psmouse *psmouse, int set_properties)
{
	if (!psmouse->vendor) psmouse->vendor = "Generic";
	if (!psmouse->name) psmouse->name = "Mouse";

	return 0;
}

/*
 * psmouse_extensions() probes for any extensions to the basic PS/2 protocol
 * the mouse may have.
 */

static int psmouse_extensions(struct psmouse *psmouse,
			      unsigned int max_proto, int set_properties)
{
	int synaptics_hardware = 0;

/*
 * Try Kensington ThinkingMouse (we try first, because synaptics probe
 * upsets the thinkingmouse).
 */

	if (max_proto > PSMOUSE_IMEX && thinking_detect(psmouse, set_properties) == 0)
		return PSMOUSE_THINKPS;

/*
 * Try Synaptics TouchPad
 */
	if (max_proto > PSMOUSE_PS2 && synaptics_detect(psmouse, set_properties) == 0) {
		synaptics_hardware = 1;

		if (max_proto > PSMOUSE_IMEX) {
			if (!set_properties || synaptics_init(psmouse) == 0)
				return PSMOUSE_SYNAPTICS;
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

	if (max_proto > PSMOUSE_IMEX && genius_detect(psmouse, set_properties) == 0)
		return PSMOUSE_GENPS;

	if (max_proto > PSMOUSE_IMEX && ps2pp_init(psmouse, set_properties) == 0)
		return PSMOUSE_PS2PP;

/*
 * Reset to defaults in case the device got confused by extended
 * protocol probes.
 */
	ps2_command(&psmouse->ps2dev, NULL, PSMOUSE_CMD_RESET_DIS);

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
		ps2_command(&psmouse->ps2dev, NULL, PSMOUSE_CMD_RESET_DIS);
	}

	return PSMOUSE_PS2;
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
	unsigned char params[] = { 0, 1, 2, 2, 3 };

	if (resolution == 0 || resolution > 200)
		resolution = 200;

	ps2_command(&psmouse->ps2dev, &params[resolution / 50], PSMOUSE_CMD_SETRES);
	psmouse->resolution = 25 << params[resolution / 50];
}

/*
 * Here we set the mouse report rate.
 */

static void psmouse_set_rate(struct psmouse *psmouse, unsigned int rate)
{
	unsigned char rates[] = { 200, 100, 80, 60, 40, 20, 10, 0 };
	int i = 0;

	while (rates[i] > rate) i++;
	ps2_command(&psmouse->ps2dev, &rates[i], PSMOUSE_CMD_SETRATE);
	psmouse->rate = rates[i];
}

/*
 * psmouse_initialize() initializes the mouse to a sane state.
 */

static void psmouse_initialize(struct psmouse *psmouse)
{
/*
 * We set the mouse into streaming mode.
 */

	ps2_command(&psmouse->ps2dev, NULL, PSMOUSE_CMD_SETSTREAM);

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
 * psmouse_set_state() sets new psmouse state and resets all flags and
 * counters while holding serio lock so fighting with interrupt handler
 * is not a concern.
 */

static void psmouse_set_state(struct psmouse *psmouse, enum psmouse_state new_state)
{
	serio_pause_rx(psmouse->ps2dev.serio);
	psmouse->state = new_state;
	psmouse->pktcnt = psmouse->out_of_sync = 0;
	psmouse->ps2dev.flags = 0;
	serio_continue_rx(psmouse->ps2dev.serio);
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
 * reports from it unless we explicitely request it.
 */

static void psmouse_deactivate(struct psmouse *psmouse)
{
	if (ps2_command(&psmouse->ps2dev, NULL, PSMOUSE_CMD_DISABLE))
		printk(KERN_WARNING "psmouse.c: Failed to deactivate mouse on %s\n",
			psmouse->ps2dev.serio->phys);

	psmouse_set_state(psmouse, PSMOUSE_CMD_MODE);
}


/*
 * psmouse_cleanup() resets the mouse into power-on state.
 */

static void psmouse_cleanup(struct serio *serio)
{
	struct psmouse *psmouse = serio_get_drvdata(serio);

	psmouse_reset(psmouse);
}

/*
 * psmouse_disconnect() closes and frees.
 */

static void psmouse_disconnect(struct serio *serio)
{
	struct psmouse *psmouse, *parent;

	device_remove_file(&serio->dev, &psmouse_attr_rate);
	device_remove_file(&serio->dev, &psmouse_attr_resolution);
	device_remove_file(&serio->dev, &psmouse_attr_resetafter);

	psmouse = serio_get_drvdata(serio);
	psmouse_set_state(psmouse, PSMOUSE_CMD_MODE);

	if (serio->parent && serio->id.type == SERIO_PS_PSTHRU) {
		parent = serio_get_drvdata(serio->parent);
		if (parent->pt_deactivate)
			parent->pt_deactivate(parent);
	}

	if (psmouse->disconnect)
		psmouse->disconnect(psmouse);

	psmouse_set_state(psmouse, PSMOUSE_IGNORE);

	input_unregister_device(&psmouse->dev);
	serio_close(serio);
	serio_set_drvdata(serio, NULL);
	kfree(psmouse);
}

/*
 * psmouse_connect() is a callback from the serio module when
 * an unhandled serio port is found.
 */
static int psmouse_connect(struct serio *serio, struct serio_driver *drv)
{
	struct psmouse *psmouse, *parent = NULL;
	int retval;

	/*
	 * If this is a pass-through port deactivate parent so the device
	 * connected to this port can be successfully identified
	 */
	if (serio->parent && serio->id.type == SERIO_PS_PSTHRU) {
		parent = serio_get_drvdata(serio->parent);
		psmouse_deactivate(parent);
	}

	if (!(psmouse = kmalloc(sizeof(struct psmouse), GFP_KERNEL))) {
		retval = -ENOMEM;
		goto out;
	}

	memset(psmouse, 0, sizeof(struct psmouse));

	ps2_init(&psmouse->ps2dev, serio);
	sprintf(psmouse->phys, "%s/input0", serio->phys);
	psmouse->dev.evbit[0] = BIT(EV_KEY) | BIT(EV_REL);
	psmouse->dev.keybit[LONG(BTN_MOUSE)] = BIT(BTN_LEFT) | BIT(BTN_MIDDLE) | BIT(BTN_RIGHT);
	psmouse->dev.relbit[0] = BIT(REL_X) | BIT(REL_Y);
	psmouse->dev.private = psmouse;
	psmouse->dev.dev = &serio->dev;
	psmouse_set_state(psmouse, PSMOUSE_INITIALIZING);

	serio_set_drvdata(serio, psmouse);

	retval = serio_open(serio, drv);
	if (retval) {
		serio_set_drvdata(serio, NULL);
		kfree(psmouse);
		goto out;
	}

	if (psmouse_probe(psmouse) < 0) {
		serio_close(serio);
		serio_set_drvdata(serio, NULL);
		kfree(psmouse);
		retval = -ENODEV;
		goto out;
	}

	psmouse->rate = psmouse_rate;
	psmouse->resolution = psmouse_resolution;
	psmouse->resetafter = psmouse_resetafter;
	psmouse->smartscroll = psmouse_smartscroll;
	psmouse->set_rate = psmouse_set_rate;
	psmouse->set_resolution = psmouse_set_resolution;
	psmouse->protocol_handler = psmouse_process_byte;
	psmouse->pktsize = 3;

	psmouse->type = psmouse_extensions(psmouse, psmouse_max_proto, 1);

	sprintf(psmouse->devname, "%s %s %s",
		psmouse_protocols[psmouse->type], psmouse->vendor, psmouse->name);

	psmouse->dev.name = psmouse->devname;
	psmouse->dev.phys = psmouse->phys;
	psmouse->dev.id.bustype = BUS_I8042;
	psmouse->dev.id.vendor = 0x0002;
	psmouse->dev.id.product = psmouse->type;
	psmouse->dev.id.version = psmouse->model;

	input_register_device(&psmouse->dev);

	printk(KERN_INFO "input: %s on %s\n", psmouse->devname, serio->phys);

	psmouse_set_state(psmouse, PSMOUSE_CMD_MODE);

	psmouse_initialize(psmouse);

	if (parent && parent->pt_activate)
		parent->pt_activate(parent);

	device_create_file(&serio->dev, &psmouse_attr_rate);
	device_create_file(&serio->dev, &psmouse_attr_resolution);
	device_create_file(&serio->dev, &psmouse_attr_resetafter);

	psmouse_activate(psmouse);

	retval = 0;

out:
	/* If this is a pass-through port the parent awaits to be activated */
	if (parent)
		psmouse_activate(parent);

	return retval;
}


static int psmouse_reconnect(struct serio *serio)
{
	struct psmouse *psmouse = serio_get_drvdata(serio);
	struct psmouse *parent = NULL;
	struct serio_driver *drv = serio->drv;
	int rc = -1;

	if (!drv || !psmouse) {
		printk(KERN_DEBUG "psmouse: reconnect request, but serio is disconnected, ignoring...\n");
		return -1;
	}

	if (serio->parent && serio->id.type == SERIO_PS_PSTHRU) {
		parent = serio_get_drvdata(serio->parent);
		psmouse_deactivate(parent);
	}

	psmouse_set_state(psmouse, PSMOUSE_INITIALIZING);

	if (psmouse->reconnect) {
		if (psmouse->reconnect(psmouse))
			goto out;
	} else if (psmouse_probe(psmouse) < 0 ||
		   psmouse->type != psmouse_extensions(psmouse, psmouse_max_proto, 0))
		goto out;

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

ssize_t psmouse_attr_show_helper(struct device *dev, char *buf,
				 ssize_t (*handler)(struct psmouse *, char *))
{
	struct serio *serio = to_serio_port(dev);
	int retval;

	retval = serio_pin_driver(serio);
	if (retval)
		return retval;

	if (serio->drv != &psmouse_drv) {
		retval = -ENODEV;
		goto out;
	}

	retval = handler(serio_get_drvdata(serio), buf);

out:
	serio_unpin_driver(serio);
	return retval;
}

ssize_t psmouse_attr_set_helper(struct device *dev, const char *buf, size_t count,
				ssize_t (*handler)(struct psmouse *, const char *, size_t))
{
	struct serio *serio = to_serio_port(dev);
	struct psmouse *psmouse = serio_get_drvdata(serio);
	struct psmouse *parent = NULL;
	int retval;

	retval = serio_pin_driver(serio);
	if (retval)
		return retval;

	if (serio->drv != &psmouse_drv) {
		retval = -ENODEV;
		goto out;
	}

	if (serio->parent && serio->id.type == SERIO_PS_PSTHRU) {
		parent = serio_get_drvdata(serio->parent);
		psmouse_deactivate(parent);
	}
	psmouse_deactivate(psmouse);

	retval = handler(psmouse, buf, count);

	psmouse_activate(psmouse);
	if (parent)
		psmouse_activate(parent);

out:
	serio_unpin_driver(serio);
	return retval;
}

static ssize_t psmouse_attr_show_rate(struct psmouse *psmouse, char *buf)
{
	return sprintf(buf, "%d\n", psmouse->rate);
}

static ssize_t psmouse_attr_set_rate(struct psmouse *psmouse, const char *buf, size_t count)
{
	unsigned long value;
	char *rest;

	value = simple_strtoul(buf, &rest, 10);
	if (*rest)
		return -EINVAL;

	psmouse->set_rate(psmouse, value);
	return count;
}

static ssize_t psmouse_attr_show_resolution(struct psmouse *psmouse, char *buf)
{
	return sprintf(buf, "%d\n", psmouse->resolution);
}

static ssize_t psmouse_attr_set_resolution(struct psmouse *psmouse, const char *buf, size_t count)
{
	unsigned long value;
	char *rest;

	value = simple_strtoul(buf, &rest, 10);
	if (*rest)
		return -EINVAL;

	psmouse->set_resolution(psmouse, value);
	return count;
}

static ssize_t psmouse_attr_show_resetafter(struct psmouse *psmouse, char *buf)
{
	return sprintf(buf, "%d\n", psmouse->resetafter);
}

static ssize_t psmouse_attr_set_resetafter(struct psmouse *psmouse, const char *buf, size_t count)
{
	unsigned long value;
	char *rest;

	value = simple_strtoul(buf, &rest, 10);
	if (*rest)
		return -EINVAL;

	psmouse->resetafter = value;
	return count;
}

static int psmouse_set_maxproto(const char *val, struct kernel_param *kp)
{
	int i;

	if (!val)
		return -EINVAL;

	if (!strncmp(val, "any", 3)) {
		*((unsigned int *)kp->arg) = -1U;
		return 0;
	}

	for (i = 0; i < ARRAY_SIZE(psmouse_proto_abbrev); i++) {
		if (!psmouse_proto_abbrev[i])
			continue;

		if (!strncmp(val, psmouse_proto_abbrev[i], strlen(psmouse_proto_abbrev[i]))) {
			*((unsigned int *)kp->arg) = i;
			return 0;
		}
	}

	return -EINVAL;					\
}

static int psmouse_get_maxproto(char *buffer, struct kernel_param *kp)
{
	return sprintf(buffer, "%s\n",
			psmouse_max_proto < ARRAY_SIZE(psmouse_proto_abbrev) ?
				psmouse_proto_abbrev[psmouse_max_proto] : "any");
}

static int __init psmouse_init(void)
{
	serio_register_driver(&psmouse_drv);
	return 0;
}

static void __exit psmouse_exit(void)
{
	serio_unregister_driver(&psmouse_drv);
}

module_init(psmouse_init);
module_exit(psmouse_exit);
