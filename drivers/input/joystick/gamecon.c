// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * NES, SNES, N64, MultiSystem, PSX gamepad driver for Linux
 *
 *  Copyright (c) 1999-2004	Vojtech Pavlik <vojtech@suse.cz>
 *  Copyright (c) 2004		Peter Nelson <rufus-kernel@hackish.org>
 *
 *  Based on the work of:
 *	Andree Borrmann		John Dahlstrom
 *	David Kuder		Nathan Hand
 *	Raphael Assenat
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/parport.h>
#include <linux/input.h>
#include <linux/mutex.h>
#include <linux/slab.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("NES, SNES, N64, MultiSystem, PSX gamepad driver");
MODULE_LICENSE("GPL");

#define GC_MAX_PORTS		3
#define GC_MAX_DEVICES		5

struct gc_config {
	int args[GC_MAX_DEVICES + 1];
	unsigned int nargs;
};

static struct gc_config gc_cfg[GC_MAX_PORTS];

module_param_array_named(map, gc_cfg[0].args, int, &gc_cfg[0].nargs, 0);
MODULE_PARM_DESC(map, "Describes first set of devices (<parport#>,<pad1>,<pad2>,..<pad5>)");
module_param_array_named(map2, gc_cfg[1].args, int, &gc_cfg[1].nargs, 0);
MODULE_PARM_DESC(map2, "Describes second set of devices");
module_param_array_named(map3, gc_cfg[2].args, int, &gc_cfg[2].nargs, 0);
MODULE_PARM_DESC(map3, "Describes third set of devices");

/* see also gs_psx_delay parameter in PSX support section */

enum gc_type {
	GC_NONE = 0,
	GC_SNES,
	GC_NES,
	GC_NES4,
	GC_MULTI,
	GC_MULTI2,
	GC_N64,
	GC_PSX,
	GC_DDR,
	GC_SNESMOUSE,
	GC_MAX
};

#define GC_REFRESH_TIME	HZ/100

struct gc_pad {
	struct input_dev *dev;
	enum gc_type type;
	char phys[32];
};

struct gc {
	struct pardevice *pd;
	struct gc_pad pads[GC_MAX_DEVICES];
	struct timer_list timer;
	int pad_count[GC_MAX];
	int used;
	int parportno;
	struct mutex mutex;
};

struct gc_subdev {
	unsigned int idx;
};

static struct gc *gc_base[3];

static const int gc_status_bit[] = { 0x40, 0x80, 0x20, 0x10, 0x08 };

static const char *gc_names[] = {
	NULL, "SNES pad", "NES pad", "NES FourPort", "Multisystem joystick",
	"Multisystem 2-button joystick", "N64 controller", "PSX controller",
	"PSX DDR controller", "SNES mouse"
};

/*
 * N64 support.
 */

static const unsigned char gc_n64_bytes[] = { 0, 1, 13, 15, 14, 12, 10, 11, 2, 3 };
static const short gc_n64_btn[] = {
	BTN_A, BTN_B, BTN_C, BTN_X, BTN_Y, BTN_Z,
	BTN_TL, BTN_TR, BTN_TRIGGER, BTN_START
};

#define GC_N64_LENGTH		32		/* N64 bit length, not including stop bit */
#define GC_N64_STOP_LENGTH	5		/* Length of encoded stop bit */
#define GC_N64_CMD_00		0x11111111UL
#define GC_N64_CMD_01		0xd1111111UL
#define GC_N64_CMD_03		0xdd111111UL
#define GC_N64_CMD_1b		0xdd1dd111UL
#define GC_N64_CMD_c0		0x111111ddUL
#define GC_N64_CMD_80		0x1111111dUL
#define GC_N64_STOP_BIT		0x1d		/* Encoded stop bit */
#define GC_N64_REQUEST_DATA	GC_N64_CMD_01	/* the request data command */
#define GC_N64_DELAY		133		/* delay between transmit request, and response ready (us) */
#define GC_N64_DWS		3		/* delay between write segments (required for sound playback because of ISA DMA) */
						/* GC_N64_DWS > 24 is known to fail */
#define GC_N64_POWER_W		0xe2		/* power during write (transmit request) */
#define GC_N64_POWER_R		0xfd		/* power during read */
#define GC_N64_OUT		0x1d		/* output bits to the 4 pads */
						/* Reading the main axes of any N64 pad is known to fail if the corresponding bit */
						/* in GC_N64_OUT is pulled low on the output port (by any routine) for more */
						/* than 123 us */
#define GC_N64_CLOCK		0x02		/* clock bits for read */

/*
 * Used for rumble code.
 */

/* Send encoded command */
static void gc_n64_send_command(struct gc *gc, unsigned long cmd,
				unsigned char target)
{
	struct parport *port = gc->pd->port;
	int i;

	for (i = 0; i < GC_N64_LENGTH; i++) {
		unsigned char data = (cmd >> i) & 1 ? target : 0;
		parport_write_data(port, GC_N64_POWER_W | data);
		udelay(GC_N64_DWS);
	}
}

/* Send stop bit */
static void gc_n64_send_stop_bit(struct gc *gc, unsigned char target)
{
	struct parport *port = gc->pd->port;
	int i;

	for (i = 0; i < GC_N64_STOP_LENGTH; i++) {
		unsigned char data = (GC_N64_STOP_BIT >> i) & 1 ? target : 0;
		parport_write_data(port, GC_N64_POWER_W | data);
		udelay(GC_N64_DWS);
	}
}

/*
 * gc_n64_read_packet() reads an N64 packet.
 * Each pad uses one bit per byte. So all pads connected to this port
 * are read in parallel.
 */

static void gc_n64_read_packet(struct gc *gc, unsigned char *data)
{
	int i;
	unsigned long flags;

/*
 * Request the pad to transmit data
 */

	local_irq_save(flags);
	gc_n64_send_command(gc, GC_N64_REQUEST_DATA, GC_N64_OUT);
	gc_n64_send_stop_bit(gc, GC_N64_OUT);
	local_irq_restore(flags);

/*
 * Wait for the pad response to be loaded into the 33-bit register
 * of the adapter.
 */

	udelay(GC_N64_DELAY);

/*
 * Grab data (ignoring the last bit, which is a stop bit)
 */

	for (i = 0; i < GC_N64_LENGTH; i++) {
		parport_write_data(gc->pd->port, GC_N64_POWER_R);
		udelay(2);
		data[i] = parport_read_status(gc->pd->port);
		parport_write_data(gc->pd->port, GC_N64_POWER_R | GC_N64_CLOCK);
	 }

/*
 * We must wait 200 ms here for the controller to reinitialize before
 * the next read request. No worries as long as gc_read is polled less
 * frequently than this.
 */

}

static void gc_n64_process_packet(struct gc *gc)
{
	unsigned char data[GC_N64_LENGTH];
	struct input_dev *dev;
	int i, j, s;
	signed char x, y;

	gc_n64_read_packet(gc, data);

	for (i = 0; i < GC_MAX_DEVICES; i++) {

		if (gc->pads[i].type != GC_N64)
			continue;

		dev = gc->pads[i].dev;
		s = gc_status_bit[i];

		if (s & ~(data[8] | data[9])) {

			x = y = 0;

			for (j = 0; j < 8; j++) {
				if (data[23 - j] & s)
					x |= 1 << j;
				if (data[31 - j] & s)
					y |= 1 << j;
			}

			input_report_abs(dev, ABS_X,  x);
			input_report_abs(dev, ABS_Y, -y);

			input_report_abs(dev, ABS_HAT0X,
					 !(s & data[6]) - !(s & data[7]));
			input_report_abs(dev, ABS_HAT0Y,
					 !(s & data[4]) - !(s & data[5]));

			for (j = 0; j < 10; j++)
				input_report_key(dev, gc_n64_btn[j],
						 s & data[gc_n64_bytes[j]]);

			input_sync(dev);
		}
	}
}

static int gc_n64_play_effect(struct input_dev *dev, void *data,
			      struct ff_effect *effect)
{
	int i;
	unsigned long flags;
	struct gc *gc = input_get_drvdata(dev);
	struct gc_subdev *sdev = data;
	unsigned char target = 1 << sdev->idx; /* select desired pin */

	if (effect->type == FF_RUMBLE) {
		struct ff_rumble_effect *rumble = &effect->u.rumble;
		unsigned int cmd =
			rumble->strong_magnitude || rumble->weak_magnitude ?
			GC_N64_CMD_01 : GC_N64_CMD_00;

		local_irq_save(flags);

		/* Init Rumble - 0x03, 0x80, 0x01, (34)0x80 */
		gc_n64_send_command(gc, GC_N64_CMD_03, target);
		gc_n64_send_command(gc, GC_N64_CMD_80, target);
		gc_n64_send_command(gc, GC_N64_CMD_01, target);
		for (i = 0; i < 32; i++)
			gc_n64_send_command(gc, GC_N64_CMD_80, target);
		gc_n64_send_stop_bit(gc, target);

		udelay(GC_N64_DELAY);

		/* Now start or stop it - 0x03, 0xc0, 0zx1b, (32)0x01/0x00 */
		gc_n64_send_command(gc, GC_N64_CMD_03, target);
		gc_n64_send_command(gc, GC_N64_CMD_c0, target);
		gc_n64_send_command(gc, GC_N64_CMD_1b, target);
		for (i = 0; i < 32; i++)
			gc_n64_send_command(gc, cmd, target);
		gc_n64_send_stop_bit(gc, target);

		local_irq_restore(flags);

	}

	return 0;
}

static int gc_n64_init_ff(struct input_dev *dev, int i)
{
	struct gc_subdev *sdev;
	int err;

	sdev = kmalloc(sizeof(*sdev), GFP_KERNEL);
	if (!sdev)
		return -ENOMEM;

	sdev->idx = i;

	input_set_capability(dev, EV_FF, FF_RUMBLE);

	err = input_ff_create_memless(dev, sdev, gc_n64_play_effect);
	if (err) {
		kfree(sdev);
		return err;
	}

	return 0;
}

/*
 * NES/SNES support.
 */

#define GC_NES_DELAY		6	/* Delay between bits - 6us */
#define GC_NES_LENGTH		8	/* The NES pads use 8 bits of data */
#define GC_SNES_LENGTH		12	/* The SNES true length is 16, but the
					   last 4 bits are unused */
#define GC_SNESMOUSE_LENGTH	32	/* The SNES mouse uses 32 bits, the first
					   16 bits are equivalent to a gamepad */

#define GC_NES_POWER	0xfc
#define GC_NES_CLOCK	0x01
#define GC_NES_LATCH	0x02

static const unsigned char gc_nes_bytes[] = { 0, 1, 2, 3 };
static const unsigned char gc_snes_bytes[] = { 8, 0, 2, 3, 9, 1, 10, 11 };
static const short gc_snes_btn[] = {
	BTN_A, BTN_B, BTN_SELECT, BTN_START, BTN_X, BTN_Y, BTN_TL, BTN_TR
};

/*
 * gc_nes_read_packet() reads a NES/SNES packet.
 * Each pad uses one bit per byte. So all pads connected to
 * this port are read in parallel.
 */

static void gc_nes_read_packet(struct gc *gc, int length, unsigned char *data)
{
	int i;

	parport_write_data(gc->pd->port, GC_NES_POWER | GC_NES_CLOCK | GC_NES_LATCH);
	udelay(GC_NES_DELAY * 2);
	parport_write_data(gc->pd->port, GC_NES_POWER | GC_NES_CLOCK);

	for (i = 0; i < length; i++) {
		udelay(GC_NES_DELAY);
		parport_write_data(gc->pd->port, GC_NES_POWER);
		data[i] = parport_read_status(gc->pd->port) ^ 0x7f;
		udelay(GC_NES_DELAY);
		parport_write_data(gc->pd->port, GC_NES_POWER | GC_NES_CLOCK);
	}
}

static void gc_nes_process_packet(struct gc *gc)
{
	unsigned char data[GC_SNESMOUSE_LENGTH];
	struct gc_pad *pad;
	struct input_dev *dev;
	int i, j, s, len;
	char x_rel, y_rel;

	len = gc->pad_count[GC_SNESMOUSE] ? GC_SNESMOUSE_LENGTH :
			(gc->pad_count[GC_SNES] ? GC_SNES_LENGTH : GC_NES_LENGTH);

	gc_nes_read_packet(gc, len, data);

	for (i = 0; i < GC_MAX_DEVICES; i++) {

		pad = &gc->pads[i];
		dev = pad->dev;
		s = gc_status_bit[i];

		switch (pad->type) {

		case GC_NES:

			input_report_abs(dev, ABS_X, !(s & data[6]) - !(s & data[7]));
			input_report_abs(dev, ABS_Y, !(s & data[4]) - !(s & data[5]));

			for (j = 0; j < 4; j++)
				input_report_key(dev, gc_snes_btn[j],
						 s & data[gc_nes_bytes[j]]);
			input_sync(dev);
			break;

		case GC_SNES:

			input_report_abs(dev, ABS_X, !(s & data[6]) - !(s & data[7]));
			input_report_abs(dev, ABS_Y, !(s & data[4]) - !(s & data[5]));

			for (j = 0; j < 8; j++)
				input_report_key(dev, gc_snes_btn[j],
						 s & data[gc_snes_bytes[j]]);
			input_sync(dev);
			break;

		case GC_SNESMOUSE:
			/*
			 * The 4 unused bits from SNES controllers appear
			 * to be ID bits so use them to make sure we are
			 * dealing with a mouse.
			 * gamepad is connected. This is important since
			 * my SNES gamepad sends 1's for bits 16-31, which
			 * cause the mouse pointer to quickly move to the
			 * upper left corner of the screen.
			 */
			if (!(s & data[12]) && !(s & data[13]) &&
			    !(s & data[14]) && (s & data[15])) {
				input_report_key(dev, BTN_LEFT, s & data[9]);
				input_report_key(dev, BTN_RIGHT, s & data[8]);

				x_rel = y_rel = 0;
				for (j = 0; j < 7; j++) {
					x_rel <<= 1;
					if (data[25 + j] & s)
						x_rel |= 1;

					y_rel <<= 1;
					if (data[17 + j] & s)
						y_rel |= 1;
				}

				if (x_rel) {
					if (data[24] & s)
						x_rel = -x_rel;
					input_report_rel(dev, REL_X, x_rel);
				}

				if (y_rel) {
					if (data[16] & s)
						y_rel = -y_rel;
					input_report_rel(dev, REL_Y, y_rel);
				}

				input_sync(dev);
			}
			break;

		default:
			break;
		}
	}
}

/*
 * Multisystem joystick support
 */

#define GC_MULTI_LENGTH		5	/* Multi system joystick packet length is 5 */
#define GC_MULTI2_LENGTH	6	/* One more bit for one more button */

/*
 * gc_multi_read_packet() reads a Multisystem joystick packet.
 */

static void gc_multi_read_packet(struct gc *gc, int length, unsigned char *data)
{
	int i;

	for (i = 0; i < length; i++) {
		parport_write_data(gc->pd->port, ~(1 << i));
		data[i] = parport_read_status(gc->pd->port) ^ 0x7f;
	}
}

static void gc_multi_process_packet(struct gc *gc)
{
	unsigned char data[GC_MULTI2_LENGTH];
	int data_len = gc->pad_count[GC_MULTI2] ? GC_MULTI2_LENGTH : GC_MULTI_LENGTH;
	struct gc_pad *pad;
	struct input_dev *dev;
	int i, s;

	gc_multi_read_packet(gc, data_len, data);

	for (i = 0; i < GC_MAX_DEVICES; i++) {
		pad = &gc->pads[i];
		dev = pad->dev;
		s = gc_status_bit[i];

		switch (pad->type) {
		case GC_MULTI2:
			input_report_key(dev, BTN_THUMB, s & data[5]);
			fallthrough;

		case GC_MULTI:
			input_report_abs(dev, ABS_X,
					 !(s & data[2]) - !(s & data[3]));
			input_report_abs(dev, ABS_Y,
					 !(s & data[0]) - !(s & data[1]));
			input_report_key(dev, BTN_TRIGGER, s & data[4]);
			input_sync(dev);
			break;

		default:
			break;
		}
	}
}

/*
 * PSX support
 *
 * See documentation at:
 *	http://www.geocities.co.jp/Playtown/2004/psx/ps_eng.txt	
 *	http://www.gamesx.com/controldata/psxcont/psxcont.htm
 *
 */

#define GC_PSX_DELAY	25		/* 25 usec */
#define GC_PSX_LENGTH	8		/* talk to the controller in bits */
#define GC_PSX_BYTES	6		/* the maximum number of bytes to read off the controller */

#define GC_PSX_MOUSE	1		/* Mouse */
#define GC_PSX_NEGCON	2		/* NegCon */
#define GC_PSX_NORMAL	4		/* Digital / Analog or Rumble in Digital mode  */
#define GC_PSX_ANALOG	5		/* Analog in Analog mode / Rumble in Green mode */
#define GC_PSX_RUMBLE	7		/* Rumble in Red mode */

#define GC_PSX_CLOCK	0x04		/* Pin 4 */
#define GC_PSX_COMMAND	0x01		/* Pin 2 */
#define GC_PSX_POWER	0xf8		/* Pins 5-9 */
#define GC_PSX_SELECT	0x02		/* Pin 3 */

#define GC_PSX_ID(x)	((x) >> 4)	/* High nibble is device type */
#define GC_PSX_LEN(x)	(((x) & 0xf) << 1)	/* Low nibble is length in bytes/2 */

static int gc_psx_delay = GC_PSX_DELAY;
module_param_named(psx_delay, gc_psx_delay, uint, 0);
MODULE_PARM_DESC(psx_delay, "Delay when accessing Sony PSX controller (usecs)");

static const short gc_psx_abs[] = {
	ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_HAT0X, ABS_HAT0Y
};
static const short gc_psx_btn[] = {
	BTN_TL, BTN_TR, BTN_TL2, BTN_TR2, BTN_A, BTN_B, BTN_X, BTN_Y,
	BTN_START, BTN_SELECT, BTN_THUMBL, BTN_THUMBR
};
static const short gc_psx_ddr_btn[] = { BTN_0, BTN_1, BTN_2, BTN_3 };

/*
 * gc_psx_command() writes 8bit command and reads 8bit data from
 * the psx pad.
 */

static void gc_psx_command(struct gc *gc, int b, unsigned char *data)
{
	struct parport *port = gc->pd->port;
	int i, j, cmd, read;

	memset(data, 0, GC_MAX_DEVICES);

	for (i = 0; i < GC_PSX_LENGTH; i++, b >>= 1) {
		cmd = (b & 1) ? GC_PSX_COMMAND : 0;
		parport_write_data(port, cmd | GC_PSX_POWER);
		udelay(gc_psx_delay);

		read = parport_read_status(port) ^ 0x80;

		for (j = 0; j < GC_MAX_DEVICES; j++) {
			struct gc_pad *pad = &gc->pads[j];

			if (pad->type == GC_PSX || pad->type == GC_DDR)
				data[j] |= (read & gc_status_bit[j]) ? (1 << i) : 0;
		}

		parport_write_data(gc->pd->port, cmd | GC_PSX_CLOCK | GC_PSX_POWER);
		udelay(gc_psx_delay);
	}
}

/*
 * gc_psx_read_packet() reads a whole psx packet and returns
 * device identifier code.
 */

static void gc_psx_read_packet(struct gc *gc,
			       unsigned char data[GC_MAX_DEVICES][GC_PSX_BYTES],
			       unsigned char id[GC_MAX_DEVICES])
{
	int i, j, max_len = 0;
	unsigned long flags;
	unsigned char data2[GC_MAX_DEVICES];

	/* Select pad */
	parport_write_data(gc->pd->port, GC_PSX_CLOCK | GC_PSX_SELECT | GC_PSX_POWER);
	udelay(gc_psx_delay);
	/* Deselect, begin command */
	parport_write_data(gc->pd->port, GC_PSX_CLOCK | GC_PSX_POWER);
	udelay(gc_psx_delay);

	local_irq_save(flags);

	gc_psx_command(gc, 0x01, data2);	/* Access pad */
	gc_psx_command(gc, 0x42, id);		/* Get device ids */
	gc_psx_command(gc, 0, data2);		/* Dump status */

	/* Find the longest pad */
	for (i = 0; i < GC_MAX_DEVICES; i++) {
		struct gc_pad *pad = &gc->pads[i];

		if ((pad->type == GC_PSX || pad->type == GC_DDR) &&
		    GC_PSX_LEN(id[i]) > max_len &&
		    GC_PSX_LEN(id[i]) <= GC_PSX_BYTES) {
			max_len = GC_PSX_LEN(id[i]);
		}
	}

	/* Read in all the data */
	for (i = 0; i < max_len; i++) {
		gc_psx_command(gc, 0, data2);
		for (j = 0; j < GC_MAX_DEVICES; j++)
			data[j][i] = data2[j];
	}

	local_irq_restore(flags);

	parport_write_data(gc->pd->port, GC_PSX_CLOCK | GC_PSX_SELECT | GC_PSX_POWER);

	/* Set id's to the real value */
	for (i = 0; i < GC_MAX_DEVICES; i++)
		id[i] = GC_PSX_ID(id[i]);
}

static void gc_psx_report_one(struct gc_pad *pad, unsigned char psx_type,
			      unsigned char *data)
{
	struct input_dev *dev = pad->dev;
	int i;

	switch (psx_type) {

	case GC_PSX_RUMBLE:

		input_report_key(dev, BTN_THUMBL, ~data[0] & 0x04);
		input_report_key(dev, BTN_THUMBR, ~data[0] & 0x02);
		fallthrough;

	case GC_PSX_NEGCON:
	case GC_PSX_ANALOG:

		if (pad->type == GC_DDR) {
			for (i = 0; i < 4; i++)
				input_report_key(dev, gc_psx_ddr_btn[i],
						 ~data[0] & (0x10 << i));
		} else {
			for (i = 0; i < 4; i++)
				input_report_abs(dev, gc_psx_abs[i + 2],
						 data[i + 2]);

			input_report_abs(dev, ABS_X,
				!!(data[0] & 0x80) * 128 + !(data[0] & 0x20) * 127);
			input_report_abs(dev, ABS_Y,
				!!(data[0] & 0x10) * 128 + !(data[0] & 0x40) * 127);
		}

		for (i = 0; i < 8; i++)
			input_report_key(dev, gc_psx_btn[i], ~data[1] & (1 << i));

		input_report_key(dev, BTN_START,  ~data[0] & 0x08);
		input_report_key(dev, BTN_SELECT, ~data[0] & 0x01);

		input_sync(dev);

		break;

	case GC_PSX_NORMAL:

		if (pad->type == GC_DDR) {
			for (i = 0; i < 4; i++)
				input_report_key(dev, gc_psx_ddr_btn[i],
						 ~data[0] & (0x10 << i));
		} else {
			input_report_abs(dev, ABS_X,
				!!(data[0] & 0x80) * 128 + !(data[0] & 0x20) * 127);
			input_report_abs(dev, ABS_Y,
				!!(data[0] & 0x10) * 128 + !(data[0] & 0x40) * 127);

			/*
			 * For some reason if the extra axes are left unset
			 * they drift.
			 * for (i = 0; i < 4; i++)
				input_report_abs(dev, gc_psx_abs[i + 2], 128);
			 * This needs to be debugged properly,
			 * maybe fuzz processing needs to be done
			 * in input_sync()
			 *				 --vojtech
			 */
		}

		for (i = 0; i < 8; i++)
			input_report_key(dev, gc_psx_btn[i], ~data[1] & (1 << i));

		input_report_key(dev, BTN_START,  ~data[0] & 0x08);
		input_report_key(dev, BTN_SELECT, ~data[0] & 0x01);

		input_sync(dev);

		break;

	default: /* not a pad, ignore */
		break;
	}
}

static void gc_psx_process_packet(struct gc *gc)
{
	unsigned char data[GC_MAX_DEVICES][GC_PSX_BYTES];
	unsigned char id[GC_MAX_DEVICES];
	struct gc_pad *pad;
	int i;

	gc_psx_read_packet(gc, data, id);

	for (i = 0; i < GC_MAX_DEVICES; i++) {
		pad = &gc->pads[i];
		if (pad->type == GC_PSX || pad->type == GC_DDR)
			gc_psx_report_one(pad, id[i], data[i]);
	}
}

/*
 * gc_timer() initiates reads of console pads data.
 */

static void gc_timer(struct timer_list *t)
{
	struct gc *gc = timer_container_of(gc, t, timer);

/*
 * N64 pads - must be read first, any read confuses them for 200 us
 */

	if (gc->pad_count[GC_N64])
		gc_n64_process_packet(gc);

/*
 * NES and SNES pads or mouse
 */

	if (gc->pad_count[GC_NES] ||
	    gc->pad_count[GC_SNES] ||
	    gc->pad_count[GC_SNESMOUSE]) {
		gc_nes_process_packet(gc);
	}

/*
 * Multi and Multi2 joysticks
 */

	if (gc->pad_count[GC_MULTI] || gc->pad_count[GC_MULTI2])
		gc_multi_process_packet(gc);

/*
 * PSX controllers
 */

	if (gc->pad_count[GC_PSX] || gc->pad_count[GC_DDR])
		gc_psx_process_packet(gc);

	mod_timer(&gc->timer, jiffies + GC_REFRESH_TIME);
}

static int gc_open(struct input_dev *dev)
{
	struct gc *gc = input_get_drvdata(dev);

	scoped_guard(mutex_intr, &gc->mutex) {
		if (!gc->used++) {
			parport_claim(gc->pd);
			parport_write_control(gc->pd->port, 0x04);
			mod_timer(&gc->timer, jiffies + GC_REFRESH_TIME);
		}

		return 0;
	}

	return -EINTR;
}

static void gc_close(struct input_dev *dev)
{
	struct gc *gc = input_get_drvdata(dev);

	guard(mutex)(&gc->mutex);

	if (!--gc->used) {
		timer_delete_sync(&gc->timer);
		parport_write_control(gc->pd->port, 0x00);
		parport_release(gc->pd);
	}
}

static int gc_setup_pad(struct gc *gc, int idx, int pad_type)
{
	struct gc_pad *pad = &gc->pads[idx];
	struct input_dev *input_dev;
	int i;
	int err;

	if (pad_type < 1 || pad_type >= GC_MAX) {
		pr_err("Pad type %d unknown\n", pad_type);
		return -EINVAL;
	}

	pad->dev = input_dev = input_allocate_device();
	if (!input_dev) {
		pr_err("Not enough memory for input device\n");
		return -ENOMEM;
	}

	pad->type = pad_type;

	snprintf(pad->phys, sizeof(pad->phys),
		 "%s/input%d", gc->pd->port->name, idx);

	input_dev->name = gc_names[pad_type];
	input_dev->phys = pad->phys;
	input_dev->id.bustype = BUS_PARPORT;
	input_dev->id.vendor = 0x0001;
	input_dev->id.product = pad_type;
	input_dev->id.version = 0x0100;

	input_set_drvdata(input_dev, gc);

	input_dev->open = gc_open;
	input_dev->close = gc_close;

	if (pad_type != GC_SNESMOUSE) {
		input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);

		for (i = 0; i < 2; i++)
			input_set_abs_params(input_dev, ABS_X + i, -1, 1, 0, 0);
	} else
		input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_REL);

	gc->pad_count[pad_type]++;

	switch (pad_type) {

	case GC_N64:
		for (i = 0; i < 10; i++)
			input_set_capability(input_dev, EV_KEY, gc_n64_btn[i]);

		for (i = 0; i < 2; i++) {
			input_set_abs_params(input_dev, ABS_X + i, -127, 126, 0, 2);
			input_set_abs_params(input_dev, ABS_HAT0X + i, -1, 1, 0, 0);
		}

		err = gc_n64_init_ff(input_dev, idx);
		if (err) {
			pr_warn("Failed to initiate rumble for N64 device %d\n",
				idx);
			goto err_free_dev;
		}

		break;

	case GC_SNESMOUSE:
		input_set_capability(input_dev, EV_KEY, BTN_LEFT);
		input_set_capability(input_dev, EV_KEY, BTN_RIGHT);
		input_set_capability(input_dev, EV_REL, REL_X);
		input_set_capability(input_dev, EV_REL, REL_Y);
		break;

	case GC_SNES:
		for (i = 4; i < 8; i++)
			input_set_capability(input_dev, EV_KEY, gc_snes_btn[i]);
		fallthrough;

	case GC_NES:
		for (i = 0; i < 4; i++)
			input_set_capability(input_dev, EV_KEY, gc_snes_btn[i]);
		break;

	case GC_MULTI2:
		input_set_capability(input_dev, EV_KEY, BTN_THUMB);
		fallthrough;

	case GC_MULTI:
		input_set_capability(input_dev, EV_KEY, BTN_TRIGGER);
		break;

	case GC_PSX:
		for (i = 0; i < 6; i++)
			input_set_abs_params(input_dev,
					     gc_psx_abs[i], 4, 252, 0, 2);
		for (i = 0; i < 12; i++)
			input_set_capability(input_dev, EV_KEY, gc_psx_btn[i]);
		break;

		break;

	case GC_DDR:
		for (i = 0; i < 4; i++)
			input_set_capability(input_dev, EV_KEY,
					     gc_psx_ddr_btn[i]);
		for (i = 0; i < 12; i++)
			input_set_capability(input_dev, EV_KEY, gc_psx_btn[i]);

		break;
	}

	err = input_register_device(pad->dev);
	if (err)
		goto err_free_dev;

	return 0;

err_free_dev:
	input_free_device(pad->dev);
	pad->dev = NULL;
	return err;
}

static void gc_attach(struct parport *pp)
{
	struct gc *gc;
	struct pardevice *pd;
	int i, port_idx;
	int count = 0;
	int *pads, n_pads;
	struct pardev_cb gc_parport_cb;

	for (port_idx = 0; port_idx < GC_MAX_PORTS; port_idx++) {
		if (gc_cfg[port_idx].nargs == 0 || gc_cfg[port_idx].args[0] < 0)
			continue;

		if (gc_cfg[port_idx].args[0] == pp->number)
			break;
	}

	if (port_idx == GC_MAX_PORTS) {
		pr_debug("Not using parport%d.\n", pp->number);
		return;
	}
	pads = gc_cfg[port_idx].args + 1;
	n_pads = gc_cfg[port_idx].nargs - 1;

	memset(&gc_parport_cb, 0, sizeof(gc_parport_cb));
	gc_parport_cb.flags = PARPORT_FLAG_EXCL;

	pd = parport_register_dev_model(pp, "gamecon", &gc_parport_cb,
					port_idx);
	if (!pd) {
		pr_err("parport busy already - lp.o loaded?\n");
		return;
	}

	gc = kzalloc(sizeof(*gc), GFP_KERNEL);
	if (!gc) {
		pr_err("Not enough memory\n");
		goto err_unreg_pardev;
	}

	mutex_init(&gc->mutex);
	gc->pd = pd;
	gc->parportno = pp->number;
	timer_setup(&gc->timer, gc_timer, 0);

	for (i = 0; i < n_pads && i < GC_MAX_DEVICES; i++) {
		if (!pads[i])
			continue;

		if (gc_setup_pad(gc, i, pads[i]))
			goto err_unreg_devs;

		count++;
	}

	if (count == 0) {
		pr_err("No valid devices specified\n");
		goto err_free_gc;
	}

	gc_base[port_idx] = gc;
	return;

 err_unreg_devs:
	while (--i >= 0)
		if (gc->pads[i].dev)
			input_unregister_device(gc->pads[i].dev);
 err_free_gc:
	kfree(gc);
 err_unreg_pardev:
	parport_unregister_device(pd);
}

static void gc_detach(struct parport *port)
{
	int i;
	struct gc *gc;

	for (i = 0; i < GC_MAX_PORTS; i++) {
		if (gc_base[i] && gc_base[i]->parportno == port->number)
			break;
	}

	if (i == GC_MAX_PORTS)
		return;

	gc = gc_base[i];
	gc_base[i] = NULL;

	for (i = 0; i < GC_MAX_DEVICES; i++)
		if (gc->pads[i].dev)
			input_unregister_device(gc->pads[i].dev);
	parport_unregister_device(gc->pd);
	kfree(gc);
}

static struct parport_driver gc_parport_driver = {
	.name = "gamecon",
	.match_port = gc_attach,
	.detach = gc_detach,
};

static int __init gc_init(void)
{
	int i;
	int have_dev = 0;

	for (i = 0; i < GC_MAX_PORTS; i++) {
		if (gc_cfg[i].nargs == 0 || gc_cfg[i].args[0] < 0)
			continue;

		if (gc_cfg[i].nargs < 2) {
			pr_err("at least one device must be specified\n");
			return -EINVAL;
		}

		have_dev = 1;
	}

	if (!have_dev)
		return -ENODEV;

	return parport_register_driver(&gc_parport_driver);
}

static void __exit gc_exit(void)
{
	parport_unregister_driver(&gc_parport_driver);
}

module_init(gc_init);
module_exit(gc_exit);
