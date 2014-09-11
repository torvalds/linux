/*
 *  Copyright (c) 1996-2001 Vojtech Pavlik
 */

/*
 * Analog joystick and gamepad driver for Linux
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@ucw.cz>, or by paper mail:
 * Vojtech Pavlik, Simunkova 1594, Prague 8, 182 00 Czech Republic
 */

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/gameport.h>
#include <linux/jiffies.h>
#include <linux/timex.h>
#include <linux/timekeeping.h>

#define DRIVER_DESC	"Analog joystick and gamepad driver"

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

static bool use_ktime = true;
module_param(use_ktime, bool, 0400);
MODULE_PARM_DESC(use_ktime, "Use ktime for measuring I/O speed");

/*
 * Option parsing.
 */

#define ANALOG_PORTS		16

static char *js[ANALOG_PORTS];
static unsigned int js_nargs;
static int analog_options[ANALOG_PORTS];
module_param_array_named(map, js, charp, &js_nargs, 0);
MODULE_PARM_DESC(map, "Describes analog joysticks type/capabilities");

/*
 * Times, feature definitions.
 */

#define ANALOG_RUDDER		0x00004
#define ANALOG_THROTTLE		0x00008
#define ANALOG_AXES_STD		0x0000f
#define ANALOG_BTNS_STD		0x000f0

#define ANALOG_BTNS_CHF		0x00100
#define ANALOG_HAT1_CHF		0x00200
#define ANALOG_HAT2_CHF		0x00400
#define ANALOG_HAT_FCS		0x00800
#define ANALOG_HATS_ALL		0x00e00
#define ANALOG_BTN_TL		0x01000
#define ANALOG_BTN_TR		0x02000
#define ANALOG_BTN_TL2		0x04000
#define ANALOG_BTN_TR2		0x08000
#define ANALOG_BTNS_TLR		0x03000
#define ANALOG_BTNS_TLR2	0x0c000
#define ANALOG_BTNS_GAMEPAD	0x0f000

#define ANALOG_HBTN_CHF		0x10000
#define ANALOG_ANY_CHF		0x10700
#define ANALOG_SAITEK		0x20000
#define ANALOG_EXTENSIONS	0x7ff00
#define ANALOG_GAMEPAD		0x80000

#define ANALOG_MAX_TIME		3	/* 3 ms */
#define ANALOG_LOOP_TIME	2000	/* 2 * loop */
#define ANALOG_SAITEK_DELAY	200	/* 200 us */
#define ANALOG_SAITEK_TIME	2000	/* 2000 us */
#define ANALOG_AXIS_TIME	2	/* 2 * refresh */
#define ANALOG_INIT_RETRIES	8	/* 8 times */
#define ANALOG_FUZZ_BITS	2	/* 2 bit more */
#define ANALOG_FUZZ_MAGIC	36	/* 36 u*ms/loop */

#define ANALOG_MAX_NAME_LENGTH  128
#define ANALOG_MAX_PHYS_LENGTH	32

static short analog_axes[] = { ABS_X, ABS_Y, ABS_RUDDER, ABS_THROTTLE };
static short analog_hats[] = { ABS_HAT0X, ABS_HAT0Y, ABS_HAT1X, ABS_HAT1Y, ABS_HAT2X, ABS_HAT2Y };
static short analog_pads[] = { BTN_Y, BTN_Z, BTN_TL, BTN_TR };
static short analog_exts[] = { ANALOG_HAT1_CHF, ANALOG_HAT2_CHF, ANALOG_HAT_FCS };
static short analog_pad_btn[] = { BTN_A, BTN_B, BTN_C, BTN_X, BTN_TL2, BTN_TR2, BTN_SELECT, BTN_START, BTN_MODE, BTN_BASE };
static short analog_joy_btn[] = { BTN_TRIGGER, BTN_THUMB, BTN_TOP, BTN_TOP2, BTN_BASE, BTN_BASE2,
				  BTN_BASE3, BTN_BASE4, BTN_BASE5, BTN_BASE6 };

static unsigned char analog_chf[] = { 0xf, 0x0, 0x1, 0x9, 0x2, 0x4, 0xc, 0x8, 0x3, 0x5, 0xb, 0x7, 0xd, 0xe, 0xa, 0x6 };

struct analog {
	struct input_dev *dev;
	int mask;
	short *buttons;
	char name[ANALOG_MAX_NAME_LENGTH];
	char phys[ANALOG_MAX_PHYS_LENGTH];
};

struct analog_port {
	struct gameport *gameport;
	struct analog analog[2];
	unsigned char mask;
	char saitek;
	char cooked;
	int bads;
	int reads;
	int speed;
	int loop;
	int fuzz;
	int axes[4];
	int buttons;
	int initial[4];
	int axtime;
};

/*
 * Time macros.
 */

#ifdef __i386__

#include <linux/i8253.h>

#define GET_TIME(x)	do { if (cpu_has_tsc) rdtscl(x); else x = get_time_pit(); } while (0)
#define DELTA(x,y)	(cpu_has_tsc ? ((y) - (x)) : ((x) - (y) + ((x) < (y) ? PIT_TICK_RATE / HZ : 0)))
#define TIME_NAME	(cpu_has_tsc?"TSC":"PIT")
static unsigned int get_time_pit(void)
{
        unsigned long flags;
        unsigned int count;

        raw_spin_lock_irqsave(&i8253_lock, flags);
        outb_p(0x00, 0x43);
        count = inb_p(0x40);
        count |= inb_p(0x40) << 8;
        raw_spin_unlock_irqrestore(&i8253_lock, flags);

        return count;
}
#elif defined(__x86_64__)
#define GET_TIME(x)	rdtscl(x)
#define DELTA(x,y)	((y)-(x))
#define TIME_NAME	"TSC"
#elif defined(__alpha__) || defined(CONFIG_MN10300) || defined(CONFIG_ARM) || defined(CONFIG_ARM64) || defined(CONFIG_TILE)
#define GET_TIME(x)	do { x = get_cycles(); } while (0)
#define DELTA(x,y)	((y)-(x))
#define TIME_NAME	"get_cycles"
#else
#define FAKE_TIME
static unsigned long analog_faketime = 0;
#define GET_TIME(x)     do { x = analog_faketime++; } while(0)
#define DELTA(x,y)	((y)-(x))
#define TIME_NAME	"Unreliable"
#warning Precise timer not defined for this architecture.
#endif

static inline u64 get_time(void)
{
	if (use_ktime) {
		return ktime_get_ns();
	} else {
		unsigned int x;
		GET_TIME(x);
		return x;
	}
}

static inline unsigned int delta(u64 x, u64 y)
{
	if (use_ktime)
		return y - x;
	else
		return DELTA((unsigned int)x, (unsigned int)y);
}

/*
 * analog_decode() decodes analog joystick data and reports input events.
 */

static void analog_decode(struct analog *analog, int *axes, int *initial, int buttons)
{
	struct input_dev *dev = analog->dev;
	int i, j;

	if (analog->mask & ANALOG_HAT_FCS)
		for (i = 0; i < 4; i++)
			if (axes[3] < ((initial[3] * ((i << 1) + 1)) >> 3)) {
				buttons |= 1 << (i + 14);
				break;
			}

	for (i = j = 0; i < 6; i++)
		if (analog->mask & (0x10 << i))
			input_report_key(dev, analog->buttons[j++], (buttons >> i) & 1);

	if (analog->mask & ANALOG_HBTN_CHF)
		for (i = 0; i < 4; i++)
			input_report_key(dev, analog->buttons[j++], (buttons >> (i + 10)) & 1);

	if (analog->mask & ANALOG_BTN_TL)
		input_report_key(dev, analog_pads[0], axes[2] < (initial[2] >> 1));
	if (analog->mask & ANALOG_BTN_TR)
		input_report_key(dev, analog_pads[1], axes[3] < (initial[3] >> 1));
	if (analog->mask & ANALOG_BTN_TL2)
		input_report_key(dev, analog_pads[2], axes[2] > (initial[2] + (initial[2] >> 1)));
	if (analog->mask & ANALOG_BTN_TR2)
		input_report_key(dev, analog_pads[3], axes[3] > (initial[3] + (initial[3] >> 1)));

	for (i = j = 0; i < 4; i++)
		if (analog->mask & (1 << i))
			input_report_abs(dev, analog_axes[j++], axes[i]);

	for (i = j = 0; i < 3; i++)
		if (analog->mask & analog_exts[i]) {
			input_report_abs(dev, analog_hats[j++],
				((buttons >> ((i << 2) + 7)) & 1) - ((buttons >> ((i << 2) + 9)) & 1));
			input_report_abs(dev, analog_hats[j++],
				((buttons >> ((i << 2) + 8)) & 1) - ((buttons >> ((i << 2) + 6)) & 1));
		}

	input_sync(dev);
}

/*
 * analog_cooked_read() reads analog joystick data.
 */

static int analog_cooked_read(struct analog_port *port)
{
	struct gameport *gameport = port->gameport;
	u64 time[4], start, loop, now;
	unsigned int loopout, timeout;
	unsigned char data[4], this, last;
	unsigned long flags;
	int i, j;

	loopout = (ANALOG_LOOP_TIME * port->loop) / 1000;
	timeout = ANALOG_MAX_TIME * port->speed;

	local_irq_save(flags);
	gameport_trigger(gameport);
	now = get_time();
	local_irq_restore(flags);

	start = now;
	this = port->mask;
	i = 0;

	do {
		loop = now;
		last = this;

		local_irq_disable();
		this = gameport_read(gameport) & port->mask;
		now = get_time();
		local_irq_restore(flags);

		if ((last ^ this) && (delta(loop, now) < loopout)) {
			data[i] = last ^ this;
			time[i] = now;
			i++;
		}

	} while (this && (i < 4) && (delta(start, now) < timeout));

	this <<= 4;

	for (--i; i >= 0; i--) {
		this |= data[i];
		for (j = 0; j < 4; j++)
			if (data[i] & (1 << j))
				port->axes[j] = (delta(start, time[i]) << ANALOG_FUZZ_BITS) / port->loop;
	}

	return -(this != port->mask);
}

static int analog_button_read(struct analog_port *port, char saitek, char chf)
{
	unsigned char u;
	int t = 1, i = 0;
	int strobe = gameport_time(port->gameport, ANALOG_SAITEK_TIME);

	u = gameport_read(port->gameport);

	if (!chf) {
		port->buttons = (~u >> 4) & 0xf;
		return 0;
	}

	port->buttons = 0;

	while ((~u & 0xf0) && (i < 16) && t) {
		port->buttons |= 1 << analog_chf[(~u >> 4) & 0xf];
		if (!saitek) return 0;
		udelay(ANALOG_SAITEK_DELAY);
		t = strobe;
		gameport_trigger(port->gameport);
		while (((u = gameport_read(port->gameport)) & port->mask) && t) t--;
		i++;
	}

	return -(!t || (i == 16));
}

/*
 * analog_poll() repeatedly polls the Analog joysticks.
 */

static void analog_poll(struct gameport *gameport)
{
	struct analog_port *port = gameport_get_drvdata(gameport);
	int i;

	char saitek = !!(port->analog[0].mask & ANALOG_SAITEK);
	char chf = !!(port->analog[0].mask & ANALOG_ANY_CHF);

	if (port->cooked) {
		port->bads -= gameport_cooked_read(port->gameport, port->axes, &port->buttons);
		if (chf)
			port->buttons = port->buttons ? (1 << analog_chf[port->buttons]) : 0;
		port->reads++;
	} else {
		if (!port->axtime--) {
			port->bads -= analog_cooked_read(port);
			port->bads -= analog_button_read(port, saitek, chf);
			port->reads++;
			port->axtime = ANALOG_AXIS_TIME - 1;
		} else {
			if (!saitek)
				analog_button_read(port, saitek, chf);
		}
	}

	for (i = 0; i < 2; i++)
		if (port->analog[i].mask)
			analog_decode(port->analog + i, port->axes, port->initial, port->buttons);
}

/*
 * analog_open() is a callback from the input open routine.
 */

static int analog_open(struct input_dev *dev)
{
	struct analog_port *port = input_get_drvdata(dev);

	gameport_start_polling(port->gameport);
	return 0;
}

/*
 * analog_close() is a callback from the input close routine.
 */

static void analog_close(struct input_dev *dev)
{
	struct analog_port *port = input_get_drvdata(dev);

	gameport_stop_polling(port->gameport);
}

/*
 * analog_calibrate_timer() calibrates the timer and computes loop
 * and timeout values for a joystick port.
 */

static void analog_calibrate_timer(struct analog_port *port)
{
	struct gameport *gameport = port->gameport;
	unsigned int i, t, tx;
	u64 t1, t2, t3;
	unsigned long flags;

	if (use_ktime) {
		port->speed = 1000000;
	} else {
		local_irq_save(flags);
		t1 = get_time();
#ifdef FAKE_TIME
		analog_faketime += 830;
#endif
		mdelay(1);
		t2 = get_time();
		t3 = get_time();
		local_irq_restore(flags);

		port->speed = delta(t1, t2) - delta(t2, t3);
	}

	tx = ~0;

	for (i = 0; i < 50; i++) {
		local_irq_save(flags);
		t1 = get_time();
		for (t = 0; t < 50; t++) {
			gameport_read(gameport);
			t2 = get_time();
		}
		t3 = get_time();
		local_irq_restore(flags);
		udelay(i);
		t = delta(t1, t2) - delta(t2, t3);
		if (t < tx) tx = t;
	}

        port->loop = tx / 50;
}

/*
 * analog_name() constructs a name for an analog joystick.
 */

static void analog_name(struct analog *analog)
{
	snprintf(analog->name, sizeof(analog->name), "Analog %d-axis %d-button",
		 hweight8(analog->mask & ANALOG_AXES_STD),
		 hweight8(analog->mask & ANALOG_BTNS_STD) + !!(analog->mask & ANALOG_BTNS_CHF) * 2 +
		 hweight16(analog->mask & ANALOG_BTNS_GAMEPAD) + !!(analog->mask & ANALOG_HBTN_CHF) * 4);

	if (analog->mask & ANALOG_HATS_ALL)
		snprintf(analog->name, sizeof(analog->name), "%s %d-hat",
			 analog->name, hweight16(analog->mask & ANALOG_HATS_ALL));

	if (analog->mask & ANALOG_HAT_FCS)
		strlcat(analog->name, " FCS", sizeof(analog->name));
	if (analog->mask & ANALOG_ANY_CHF)
		strlcat(analog->name, (analog->mask & ANALOG_SAITEK) ? " Saitek" : " CHF",
			sizeof(analog->name));

	strlcat(analog->name, (analog->mask & ANALOG_GAMEPAD) ? " gamepad": " joystick",
		sizeof(analog->name));
}

/*
 * analog_init_device()
 */

static int analog_init_device(struct analog_port *port, struct analog *analog, int index)
{
	struct input_dev *input_dev;
	int i, j, t, v, w, x, y, z;
	int error;

	analog_name(analog);
	snprintf(analog->phys, sizeof(analog->phys),
		 "%s/input%d", port->gameport->phys, index);
	analog->buttons = (analog->mask & ANALOG_GAMEPAD) ? analog_pad_btn : analog_joy_btn;

	analog->dev = input_dev = input_allocate_device();
	if (!input_dev)
		return -ENOMEM;

	input_dev->name = analog->name;
	input_dev->phys = analog->phys;
	input_dev->id.bustype = BUS_GAMEPORT;
	input_dev->id.vendor = GAMEPORT_ID_VENDOR_ANALOG;
	input_dev->id.product = analog->mask >> 4;
	input_dev->id.version = 0x0100;
	input_dev->dev.parent = &port->gameport->dev;

	input_set_drvdata(input_dev, port);

	input_dev->open = analog_open;
	input_dev->close = analog_close;

	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);

	for (i = j = 0; i < 4; i++)
		if (analog->mask & (1 << i)) {

			t = analog_axes[j];
			x = port->axes[i];
			y = (port->axes[0] + port->axes[1]) >> 1;
			z = y - port->axes[i];
			z = z > 0 ? z : -z;
			v = (x >> 3);
			w = (x >> 3);

			if ((i == 2 || i == 3) && (j == 2 || j == 3) && (z > (y >> 3)))
				x = y;

			if (analog->mask & ANALOG_SAITEK) {
				if (i == 2) x = port->axes[i];
				v = x - (x >> 2);
				w = (x >> 4);
			}

			input_set_abs_params(input_dev, t, v, (x << 1) - v, port->fuzz, w);
			j++;
		}

	for (i = j = 0; i < 3; i++)
		if (analog->mask & analog_exts[i])
			for (x = 0; x < 2; x++) {
				t = analog_hats[j++];
				input_set_abs_params(input_dev, t, -1, 1, 0, 0);
			}

	for (i = j = 0; i < 4; i++)
		if (analog->mask & (0x10 << i))
			set_bit(analog->buttons[j++], input_dev->keybit);

	if (analog->mask & ANALOG_BTNS_CHF)
		for (i = 0; i < 2; i++)
			set_bit(analog->buttons[j++], input_dev->keybit);

	if (analog->mask & ANALOG_HBTN_CHF)
		for (i = 0; i < 4; i++)
			set_bit(analog->buttons[j++], input_dev->keybit);

	for (i = 0; i < 4; i++)
		if (analog->mask & (ANALOG_BTN_TL << i))
			set_bit(analog_pads[i], input_dev->keybit);

	analog_decode(analog, port->axes, port->initial, port->buttons);

	error = input_register_device(analog->dev);
	if (error) {
		input_free_device(analog->dev);
		return error;
	}

	return 0;
}

/*
 * analog_init_devices() sets up device-specific values and registers the input devices.
 */

static int analog_init_masks(struct analog_port *port)
{
	int i;
	struct analog *analog = port->analog;
	int max[4];

	if (!port->mask)
		return -1;

	if ((port->mask & 3) != 3 && port->mask != 0xc) {
		printk(KERN_WARNING "analog.c: Unknown joystick device found  "
			"(data=%#x, %s), probably not analog joystick.\n",
			port->mask, port->gameport->phys);
		return -1;
	}


	i = analog_options[0]; /* FIXME !!! - need to specify options for different ports */

	analog[0].mask = i & 0xfffff;

	analog[0].mask &= ~(ANALOG_AXES_STD | ANALOG_HAT_FCS | ANALOG_BTNS_GAMEPAD)
			| port->mask | ((port->mask << 8) & ANALOG_HAT_FCS)
			| ((port->mask << 10) & ANALOG_BTNS_TLR) | ((port->mask << 12) & ANALOG_BTNS_TLR2);

	analog[0].mask &= ~(ANALOG_HAT2_CHF)
			| ((analog[0].mask & ANALOG_HBTN_CHF) ? 0 : ANALOG_HAT2_CHF);

	analog[0].mask &= ~(ANALOG_THROTTLE | ANALOG_BTN_TR | ANALOG_BTN_TR2)
			| ((~analog[0].mask & ANALOG_HAT_FCS) >> 8)
			| ((~analog[0].mask & ANALOG_HAT_FCS) << 2)
			| ((~analog[0].mask & ANALOG_HAT_FCS) << 4);

	analog[0].mask &= ~(ANALOG_THROTTLE | ANALOG_RUDDER)
			| (((~analog[0].mask & ANALOG_BTNS_TLR ) >> 10)
			&  ((~analog[0].mask & ANALOG_BTNS_TLR2) >> 12));

	analog[1].mask = ((i >> 20) & 0xff) | ((i >> 12) & 0xf0000);

	analog[1].mask &= (analog[0].mask & ANALOG_EXTENSIONS) ? ANALOG_GAMEPAD
			: (((ANALOG_BTNS_STD | port->mask) & ~analog[0].mask) | ANALOG_GAMEPAD);

	if (port->cooked) {

		for (i = 0; i < 4; i++) max[i] = port->axes[i] << 1;

		if ((analog[0].mask & 0x7) == 0x7) max[2] = (max[0] + max[1]) >> 1;
		if ((analog[0].mask & 0xb) == 0xb) max[3] = (max[0] + max[1]) >> 1;
		if ((analog[0].mask & ANALOG_BTN_TL) && !(analog[0].mask & ANALOG_BTN_TL2)) max[2] >>= 1;
		if ((analog[0].mask & ANALOG_BTN_TR) && !(analog[0].mask & ANALOG_BTN_TR2)) max[3] >>= 1;
		if ((analog[0].mask & ANALOG_HAT_FCS)) max[3] >>= 1;

		gameport_calibrate(port->gameport, port->axes, max);
	}

	for (i = 0; i < 4; i++)
		port->initial[i] = port->axes[i];

	return -!(analog[0].mask || analog[1].mask);
}

static int analog_init_port(struct gameport *gameport, struct gameport_driver *drv, struct analog_port *port)
{
	int i, t, u, v;

	port->gameport = gameport;

	gameport_set_drvdata(gameport, port);

	if (!gameport_open(gameport, drv, GAMEPORT_MODE_RAW)) {

		analog_calibrate_timer(port);

		gameport_trigger(gameport);
		t = gameport_read(gameport);
		msleep(ANALOG_MAX_TIME);
		port->mask = (gameport_read(gameport) ^ t) & t & 0xf;
		port->fuzz = (port->speed * ANALOG_FUZZ_MAGIC) / port->loop / 1000 + ANALOG_FUZZ_BITS;

		for (i = 0; i < ANALOG_INIT_RETRIES; i++) {
			if (!analog_cooked_read(port))
				break;
			msleep(ANALOG_MAX_TIME);
		}

		u = v = 0;

		msleep(ANALOG_MAX_TIME);
		t = gameport_time(gameport, ANALOG_MAX_TIME * 1000);
		gameport_trigger(gameport);
		while ((gameport_read(port->gameport) & port->mask) && (u < t))
			u++;
		udelay(ANALOG_SAITEK_DELAY);
		t = gameport_time(gameport, ANALOG_SAITEK_TIME);
		gameport_trigger(gameport);
		while ((gameport_read(port->gameport) & port->mask) && (v < t))
			v++;

		if (v < (u >> 1)) { /* FIXME - more than one port */
			analog_options[0] |= /* FIXME - more than one port */
				ANALOG_SAITEK | ANALOG_BTNS_CHF | ANALOG_HBTN_CHF | ANALOG_HAT1_CHF;
			return 0;
		}

		gameport_close(gameport);
	}

	if (!gameport_open(gameport, drv, GAMEPORT_MODE_COOKED)) {

		for (i = 0; i < ANALOG_INIT_RETRIES; i++)
			if (!gameport_cooked_read(gameport, port->axes, &port->buttons))
				break;
		for (i = 0; i < 4; i++)
			if (port->axes[i] != -1)
				port->mask |= 1 << i;

		port->fuzz = gameport->fuzz;
		port->cooked = 1;
		return 0;
	}

	return gameport_open(gameport, drv, GAMEPORT_MODE_RAW);
}

static int analog_connect(struct gameport *gameport, struct gameport_driver *drv)
{
	struct analog_port *port;
	int i;
	int err;

	if (!(port = kzalloc(sizeof(struct analog_port), GFP_KERNEL)))
		return - ENOMEM;

	err = analog_init_port(gameport, drv, port);
	if (err)
		goto fail1;

	err = analog_init_masks(port);
	if (err)
		goto fail2;

	gameport_set_poll_handler(gameport, analog_poll);
	gameport_set_poll_interval(gameport, 10);

	for (i = 0; i < 2; i++)
		if (port->analog[i].mask) {
			err = analog_init_device(port, port->analog + i, i);
			if (err)
				goto fail3;
		}

	return 0;

 fail3: while (--i >= 0)
		if (port->analog[i].mask)
			input_unregister_device(port->analog[i].dev);
 fail2:	gameport_close(gameport);
 fail1:	gameport_set_drvdata(gameport, NULL);
	kfree(port);
	return err;
}

static void analog_disconnect(struct gameport *gameport)
{
	struct analog_port *port = gameport_get_drvdata(gameport);
	int i;

	for (i = 0; i < 2; i++)
		if (port->analog[i].mask)
			input_unregister_device(port->analog[i].dev);
	gameport_close(gameport);
	gameport_set_drvdata(gameport, NULL);
	printk(KERN_INFO "analog.c: %d out of %d reads (%d%%) on %s failed\n",
		port->bads, port->reads, port->reads ? (port->bads * 100 / port->reads) : 0,
		port->gameport->phys);
	kfree(port);
}

struct analog_types {
	char *name;
	int value;
};

static struct analog_types analog_types[] = {
	{ "none",	0x00000000 },
	{ "auto",	0x000000ff },
	{ "2btn",	0x0000003f },
	{ "y-joy",	0x0cc00033 },
	{ "y-pad",	0x8cc80033 },
	{ "fcs",	0x000008f7 },
	{ "chf",	0x000002ff },
	{ "fullchf",	0x000007ff },
	{ "gamepad",	0x000830f3 },
	{ "gamepad8",	0x0008f0f3 },
	{ NULL, 0 }
};

static void analog_parse_options(void)
{
	int i, j;
	char *end;

	for (i = 0; i < js_nargs; i++) {

		for (j = 0; analog_types[j].name; j++)
			if (!strcmp(analog_types[j].name, js[i])) {
				analog_options[i] = analog_types[j].value;
				break;
			}
		if (analog_types[j].name) continue;

		analog_options[i] = simple_strtoul(js[i], &end, 0);
		if (end != js[i]) continue;

		analog_options[i] = 0xff;
		if (!strlen(js[i])) continue;

		printk(KERN_WARNING "analog.c: Bad config for port %d - \"%s\"\n", i, js[i]);
	}

	for (; i < ANALOG_PORTS; i++)
		analog_options[i] = 0xff;
}

/*
 * The gameport device structure.
 */

static struct gameport_driver analog_drv = {
	.driver		= {
		.name	= "analog",
	},
	.description	= DRIVER_DESC,
	.connect	= analog_connect,
	.disconnect	= analog_disconnect,
};

static int __init analog_init(void)
{
	analog_parse_options();
	return gameport_register_driver(&analog_drv);
}

static void __exit analog_exit(void)
{
	gameport_unregister_driver(&analog_drv);
}

module_init(analog_init);
module_exit(analog_exit);
