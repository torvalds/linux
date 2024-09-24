// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) 1998-2001 Vojtech Pavlik
 */

/*
 * Genius Flight 2000 joystick driver for Linux
 */

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/gameport.h>
#include <linux/jiffies.h>

#define DRIVER_DESC	"Genius Flight 2000 joystick driver"

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

#define GF2K_START		400	/* The time we wait for the first bit [400 us] */
#define GF2K_STROBE		40	/* The time we wait for the first bit [40 us] */
#define GF2K_TIMEOUT		4	/* Wait for everything to settle [4 ms] */
#define GF2K_LENGTH		80	/* Max number of triplets in a packet */

/*
 * Genius joystick ids ...
 */

#define GF2K_ID_G09		1
#define GF2K_ID_F30D		2
#define GF2K_ID_F30		3
#define GF2K_ID_F31D		4
#define GF2K_ID_F305		5
#define GF2K_ID_F23P		6
#define GF2K_ID_F31		7
#define GF2K_ID_MAX		7

static char gf2k_length[] = { 40, 40, 40, 40, 40, 40, 40, 40 };
static char gf2k_hat_to_axis[][2] = {{ 0, 0}, { 0,-1}, { 1,-1}, { 1, 0}, { 1, 1}, { 0, 1}, {-1, 1}, {-1, 0}, {-1,-1}};

static char *gf2k_names[] = {"", "Genius G-09D", "Genius F-30D", "Genius F-30", "Genius MaxFighter F-31D",
				"Genius F-30-5", "Genius Flight2000 F-23", "Genius F-31"};
static unsigned char gf2k_hats[] = { 0, 2, 0, 0, 2, 0, 2, 0 };
static unsigned char gf2k_axes[] = { 0, 2, 0, 0, 4, 0, 4, 0 };
static unsigned char gf2k_joys[] = { 0, 0, 0, 0,10, 0, 8, 0 };
static unsigned char gf2k_pads[] = { 0, 6, 0, 0, 0, 0, 0, 0 };
static unsigned char gf2k_lens[] = { 0,18, 0, 0,18, 0,18, 0 };

static unsigned char gf2k_abs[] = { ABS_X, ABS_Y, ABS_THROTTLE, ABS_RUDDER, ABS_GAS, ABS_BRAKE };
static short gf2k_btn_joy[] = { BTN_TRIGGER, BTN_THUMB, BTN_TOP, BTN_TOP2, BTN_BASE, BTN_BASE2, BTN_BASE3, BTN_BASE4 };
static short gf2k_btn_pad[] = { BTN_A, BTN_B, BTN_C, BTN_X, BTN_Y, BTN_Z, BTN_TL, BTN_TR, BTN_TL2, BTN_TR2, BTN_START, BTN_SELECT };


static short gf2k_seq_reset[] = { 240, 340, 0 };
static short gf2k_seq_digital[] = { 590, 320, 860, 0 };

struct gf2k {
	struct gameport *gameport;
	struct input_dev *dev;
	int reads;
	int bads;
	unsigned char id;
	unsigned char length;
	char phys[32];
};

/*
 * gf2k_read_packet() reads a Genius Flight2000 packet.
 */

static int gf2k_read_packet(struct gameport *gameport, int length, char *data)
{
	unsigned char u, v;
	int i;
	unsigned int t, p;
	unsigned long flags;

	t = gameport_time(gameport, GF2K_START);
	p = gameport_time(gameport, GF2K_STROBE);

	i = 0;

	local_irq_save(flags);

	gameport_trigger(gameport);
	v = gameport_read(gameport);

	while (t > 0 && i < length) {
		t--; u = v;
		v = gameport_read(gameport);
		if (v & ~u & 0x10) {
			data[i++] = v >> 5;
			t = p;
		}
	}

	local_irq_restore(flags);

	return i;
}

/*
 * gf2k_trigger_seq() initializes a Genius Flight2000 joystick
 * into digital mode.
 */

static void gf2k_trigger_seq(struct gameport *gameport, short *seq)
{

	unsigned long flags;
	int i, t;

        local_irq_save(flags);

	i = 0;
        do {
		gameport_trigger(gameport);
		t = gameport_time(gameport, GF2K_TIMEOUT * 1000);
		while ((gameport_read(gameport) & 1) && t) t--;
                udelay(seq[i]);
        } while (seq[++i]);

	gameport_trigger(gameport);

	local_irq_restore(flags);
}

/*
 * js_sw_get_bits() composes bits from the triplet buffer into a __u64.
 * Parameter 'pos' is bit number inside packet where to start at, 'num' is number
 * of bits to be read, 'shift' is offset in the resulting __u64 to start at, bits
 * is number of bits per triplet.
 */

#define GB(p,n,s)	gf2k_get_bits(data, p, n, s)

static int gf2k_get_bits(unsigned char *buf, int pos, int num, int shift)
{
	__u64 data = 0;
	int i;

	for (i = 0; i < num / 3 + 2; i++)
		data |= buf[pos / 3 + i] << (i * 3);
	data >>= pos % 3;
	data &= (1 << num) - 1;
	data <<= shift;

	return data;
}

static void gf2k_read(struct gf2k *gf2k, unsigned char *data)
{
	struct input_dev *dev = gf2k->dev;
	int i, t;

	for (i = 0; i < 4 && i < gf2k_axes[gf2k->id]; i++)
		input_report_abs(dev, gf2k_abs[i], GB(i<<3,8,0) | GB(i+46,1,8) | GB(i+50,1,9));

	for (i = 0; i < 2 && i < gf2k_axes[gf2k->id] - 4; i++)
		input_report_abs(dev, gf2k_abs[i], GB(i*9+60,8,0) | GB(i+54,1,9));

	t = GB(40,4,0);

	for (i = 0; i < gf2k_hats[gf2k->id]; i++)
		input_report_abs(dev, ABS_HAT0X + i, gf2k_hat_to_axis[t][i]);

	t = GB(44,2,0) | GB(32,8,2) | GB(78,2,10);

	for (i = 0; i < gf2k_joys[gf2k->id]; i++)
		input_report_key(dev, gf2k_btn_joy[i], (t >> i) & 1);

	for (i = 0; i < gf2k_pads[gf2k->id]; i++)
		input_report_key(dev, gf2k_btn_pad[i], (t >> i) & 1);

	input_sync(dev);
}

/*
 * gf2k_poll() reads and analyzes Genius joystick data.
 */

static void gf2k_poll(struct gameport *gameport)
{
	struct gf2k *gf2k = gameport_get_drvdata(gameport);
	unsigned char data[GF2K_LENGTH];

	gf2k->reads++;

	if (gf2k_read_packet(gf2k->gameport, gf2k_length[gf2k->id], data) < gf2k_length[gf2k->id])
		gf2k->bads++;
	else
		gf2k_read(gf2k, data);
}

static int gf2k_open(struct input_dev *dev)
{
	struct gf2k *gf2k = input_get_drvdata(dev);

	gameport_start_polling(gf2k->gameport);
	return 0;
}

static void gf2k_close(struct input_dev *dev)
{
	struct gf2k *gf2k = input_get_drvdata(dev);

	gameport_stop_polling(gf2k->gameport);
}

/*
 * gf2k_connect() probes for Genius id joysticks.
 */

static int gf2k_connect(struct gameport *gameport, struct gameport_driver *drv)
{
	struct gf2k *gf2k;
	struct input_dev *input_dev;
	unsigned char data[GF2K_LENGTH];
	int i, err;

	gf2k = kzalloc(sizeof(*gf2k), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!gf2k || !input_dev) {
		err = -ENOMEM;
		goto fail1;
	}

	gf2k->gameport = gameport;
	gf2k->dev = input_dev;

	gameport_set_drvdata(gameport, gf2k);

	err = gameport_open(gameport, drv, GAMEPORT_MODE_RAW);
	if (err)
		goto fail1;

	gf2k_trigger_seq(gameport, gf2k_seq_reset);

	msleep(GF2K_TIMEOUT);

	gf2k_trigger_seq(gameport, gf2k_seq_digital);

	msleep(GF2K_TIMEOUT);

	if (gf2k_read_packet(gameport, GF2K_LENGTH, data) < 12) {
		err = -ENODEV;
		goto fail2;
	}

	if (!(gf2k->id = GB(7,2,0) | GB(3,3,2) | GB(0,3,5))) {
		err = -ENODEV;
		goto fail2;
	}

#ifdef RESET_WORKS
	if ((gf2k->id != (GB(19,2,0) | GB(15,3,2) | GB(12,3,5))) &&
	    (gf2k->id != (GB(31,2,0) | GB(27,3,2) | GB(24,3,5)))) {
		err = -ENODEV;
		goto fail2;
	}
#else
	gf2k->id = 6;
#endif

	if (gf2k->id > GF2K_ID_MAX || !gf2k_axes[gf2k->id]) {
		printk(KERN_WARNING "gf2k.c: Not yet supported joystick on %s. [id: %d type:%s]\n",
			gameport->phys, gf2k->id, gf2k->id > GF2K_ID_MAX ? "Unknown" : gf2k_names[gf2k->id]);
		err = -ENODEV;
		goto fail2;
	}

	gameport_set_poll_handler(gameport, gf2k_poll);
	gameport_set_poll_interval(gameport, 20);

	snprintf(gf2k->phys, sizeof(gf2k->phys), "%s/input0", gameport->phys);

	gf2k->length = gf2k_lens[gf2k->id];

	input_dev->name = gf2k_names[gf2k->id];
	input_dev->phys = gf2k->phys;
	input_dev->id.bustype = BUS_GAMEPORT;
	input_dev->id.vendor = GAMEPORT_ID_VENDOR_GENIUS;
	input_dev->id.product = gf2k->id;
	input_dev->id.version = 0x0100;
	input_dev->dev.parent = &gameport->dev;

	input_set_drvdata(input_dev, gf2k);

	input_dev->open = gf2k_open;
	input_dev->close = gf2k_close;

	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);

	for (i = 0; i < gf2k_axes[gf2k->id]; i++)
		set_bit(gf2k_abs[i], input_dev->absbit);

	for (i = 0; i < gf2k_hats[gf2k->id]; i++)
		input_set_abs_params(input_dev, ABS_HAT0X + i, -1, 1, 0, 0);

	for (i = 0; i < gf2k_joys[gf2k->id]; i++)
		set_bit(gf2k_btn_joy[i], input_dev->keybit);

	for (i = 0; i < gf2k_pads[gf2k->id]; i++)
		set_bit(gf2k_btn_pad[i], input_dev->keybit);

	gf2k_read_packet(gameport, gf2k->length, data);
	gf2k_read(gf2k, data);

	for (i = 0; i < gf2k_axes[gf2k->id]; i++) {
		int max = i < 2 ?
			input_abs_get_val(input_dev, gf2k_abs[i]) * 2 :
			input_abs_get_val(input_dev, gf2k_abs[0]) +
				input_abs_get_val(input_dev, gf2k_abs[1]);
		int flat = i < 2 ? 24 : 0;

		input_set_abs_params(input_dev, gf2k_abs[i],
				     32, max - 32, 8, flat);
	}

	err = input_register_device(gf2k->dev);
	if (err)
		goto fail2;

	return 0;

 fail2:	gameport_close(gameport);
 fail1:	gameport_set_drvdata(gameport, NULL);
	input_free_device(input_dev);
	kfree(gf2k);
	return err;
}

static void gf2k_disconnect(struct gameport *gameport)
{
	struct gf2k *gf2k = gameport_get_drvdata(gameport);

	input_unregister_device(gf2k->dev);
	gameport_close(gameport);
	gameport_set_drvdata(gameport, NULL);
	kfree(gf2k);
}

static struct gameport_driver gf2k_drv = {
	.driver		= {
		.name	= "gf2k",
	},
	.description	= DRIVER_DESC,
	.connect	= gf2k_connect,
	.disconnect	= gf2k_disconnect,
};

module_gameport_driver(gf2k_drv);
