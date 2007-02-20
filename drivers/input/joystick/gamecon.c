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

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/parport.h>
#include <linux/input.h>
#include <linux/mutex.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("NES, SNES, N64, MultiSystem, PSX gamepad driver");
MODULE_LICENSE("GPL");

#define GC_MAX_PORTS		3
#define GC_MAX_DEVICES		5

struct gc_config {
	int args[GC_MAX_DEVICES + 1];
	int nargs;
};

static struct gc_config gc[GC_MAX_PORTS] __initdata;

module_param_array_named(map, gc[0].args, int, &gc[0].nargs, 0);
MODULE_PARM_DESC(map, "Describes first set of devices (<parport#>,<pad1>,<pad2>,..<pad5>)");
module_param_array_named(map2, gc[1].args, int, &gc[1].nargs, 0);
MODULE_PARM_DESC(map2, "Describes second set of devices");
module_param_array_named(map3, gc[2].args, int, &gc[2].nargs, 0);
MODULE_PARM_DESC(map3, "Describes third set of devices");

/* see also gs_psx_delay parameter in PSX support section */

#define GC_SNES		1
#define GC_NES		2
#define GC_NES4		3
#define GC_MULTI	4
#define GC_MULTI2	5
#define GC_N64		6
#define GC_PSX		7
#define GC_DDR		8
#define GC_SNESMOUSE	9

#define GC_MAX		9

#define GC_REFRESH_TIME	HZ/100

struct gc {
	struct pardevice *pd;
	struct input_dev *dev[GC_MAX_DEVICES];
	struct timer_list timer;
	unsigned char pads[GC_MAX + 1];
	int used;
	struct mutex mutex;
	char phys[GC_MAX_DEVICES][32];
};

static struct gc *gc_base[3];

static int gc_status_bit[] = { 0x40, 0x80, 0x20, 0x10, 0x08 };

static char *gc_names[] = { NULL, "SNES pad", "NES pad", "NES FourPort", "Multisystem joystick",
				"Multisystem 2-button joystick", "N64 controller", "PSX controller",
				"PSX DDR controller", "SNES mouse" };
/*
 * N64 support.
 */

static unsigned char gc_n64_bytes[] = { 0, 1, 13, 15, 14, 12, 10, 11, 2, 3 };
static short gc_n64_btn[] = { BTN_A, BTN_B, BTN_C, BTN_X, BTN_Y, BTN_Z, BTN_TL, BTN_TR, BTN_TRIGGER, BTN_START };

#define GC_N64_LENGTH		32		/* N64 bit length, not including stop bit */
#define GC_N64_REQUEST_LENGTH	37		/* transmit request sequence is 9 bits long */
#define GC_N64_DELAY		133		/* delay between transmit request, and response ready (us) */
#define GC_N64_REQUEST		0x1dd1111111ULL /* the request data command (encoded for 000000011) */
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
 * gc_n64_read_packet() reads an N64 packet.
 * Each pad uses one bit per byte. So all pads connected to this port are read in parallel.
 */

static void gc_n64_read_packet(struct gc *gc, unsigned char *data)
{
	int i;
	unsigned long flags;

/*
 * Request the pad to transmit data
 */

	local_irq_save(flags);
	for (i = 0; i < GC_N64_REQUEST_LENGTH; i++) {
		parport_write_data(gc->pd->port, GC_N64_POWER_W | ((GC_N64_REQUEST >> i) & 1 ? GC_N64_OUT : 0));
		udelay(GC_N64_DWS);
	}
	local_irq_restore(flags);

/*
 * Wait for the pad response to be loaded into the 33-bit register of the adapter
 */

	udelay(GC_N64_DELAY);

/*
 * Grab data (ignoring the last bit, which is a stop bit)
 */

	for (i = 0; i < GC_N64_LENGTH; i++) {
		parport_write_data(gc->pd->port, GC_N64_POWER_R);
		data[i] = parport_read_status(gc->pd->port);
		parport_write_data(gc->pd->port, GC_N64_POWER_R | GC_N64_CLOCK);
	 }

/*
 * We must wait 200 ms here for the controller to reinitialize before the next read request.
 * No worries as long as gc_read is polled less frequently than this.
 */

}

static void gc_n64_process_packet(struct gc *gc)
{
	unsigned char data[GC_N64_LENGTH];
	signed char axes[2];
	struct input_dev *dev;
	int i, j, s;

	gc_n64_read_packet(gc, data);

	for (i = 0; i < GC_MAX_DEVICES; i++) {

		dev = gc->dev[i];
		if (!dev)
			continue;

		s = gc_status_bit[i];

		if (s & gc->pads[GC_N64] & ~(data[8] | data[9])) {

			axes[0] = axes[1] = 0;

			for (j = 0; j < 8; j++) {
				if (data[23 - j] & s)
					axes[0] |= 1 << j;
				if (data[31 - j] & s)
					axes[1] |= 1 << j;
			}

			input_report_abs(dev, ABS_X,  axes[0]);
			input_report_abs(dev, ABS_Y, -axes[1]);

			input_report_abs(dev, ABS_HAT0X, !(s & data[6]) - !(s & data[7]));
			input_report_abs(dev, ABS_HAT0Y, !(s & data[4]) - !(s & data[5]));

			for (j = 0; j < 10; j++)
				input_report_key(dev, gc_n64_btn[j], s & data[gc_n64_bytes[j]]);

			input_sync(dev);
		}
	}
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

static unsigned char gc_nes_bytes[] = { 0, 1, 2, 3 };
static unsigned char gc_snes_bytes[] = { 8, 0, 2, 3, 9, 1, 10, 11 };
static short gc_snes_btn[] = { BTN_A, BTN_B, BTN_SELECT, BTN_START, BTN_X, BTN_Y, BTN_TL, BTN_TR };

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
	struct input_dev *dev;
	int i, j, s, len;
	char x_rel, y_rel;

	len = gc->pads[GC_SNESMOUSE] ? GC_SNESMOUSE_LENGTH :
			(gc->pads[GC_SNES] ? GC_SNES_LENGTH : GC_NES_LENGTH);

	gc_nes_read_packet(gc, len, data);

	for (i = 0; i < GC_MAX_DEVICES; i++) {

		dev = gc->dev[i];
		if (!dev)
			continue;

		s = gc_status_bit[i];

		if (s & (gc->pads[GC_NES] | gc->pads[GC_SNES])) {
			input_report_abs(dev, ABS_X, !(s & data[6]) - !(s & data[7]));
			input_report_abs(dev, ABS_Y, !(s & data[4]) - !(s & data[5]));
		}

		if (s & gc->pads[GC_NES])
			for (j = 0; j < 4; j++)
				input_report_key(dev, gc_snes_btn[j], s & data[gc_nes_bytes[j]]);

		if (s & gc->pads[GC_SNES])
			for (j = 0; j < 8; j++)
				input_report_key(dev, gc_snes_btn[j], s & data[gc_snes_bytes[j]]);

		if (s & gc->pads[GC_SNESMOUSE]) {
			/*
			 * The 4 unused bits from SNES controllers appear to be ID bits
			 * so use them to make sure iwe are dealing with a mouse.
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
			}
		}
		input_sync(dev);
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
	struct input_dev *dev;
	int i, s;

	gc_multi_read_packet(gc, gc->pads[GC_MULTI2] ? GC_MULTI2_LENGTH : GC_MULTI_LENGTH, data);

	for (i = 0; i < GC_MAX_DEVICES; i++) {

		dev = gc->dev[i];
		if (!dev)
			continue;

		s = gc_status_bit[i];

		if (s & (gc->pads[GC_MULTI] | gc->pads[GC_MULTI2])) {
			input_report_abs(dev, ABS_X,  !(s & data[2]) - !(s & data[3]));
			input_report_abs(dev, ABS_Y,  !(s & data[0]) - !(s & data[1]));
			input_report_key(dev, BTN_TRIGGER, s & data[4]);
		}

		if (s & gc->pads[GC_MULTI2])
			input_report_key(dev, BTN_THUMB, s & data[5]);

		input_sync(dev);
	}
}

/*
 * PSX support
 *
 * See documentation at:
 *	http://www.dim.com/~mackys/psxmemcard/ps-eng2.txt
 *	http://www.gamesx.com/controldata/psxcont/psxcont.htm
 *	ftp://milano.usal.es/pablo/
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

static short gc_psx_abs[] = { ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_HAT0X, ABS_HAT0Y };
static short gc_psx_btn[] = { BTN_TL, BTN_TR, BTN_TL2, BTN_TR2, BTN_A, BTN_B, BTN_X, BTN_Y,
				BTN_START, BTN_SELECT, BTN_THUMBL, BTN_THUMBR };
static short gc_psx_ddr_btn[] = { BTN_0, BTN_1, BTN_2, BTN_3 };

/*
 * gc_psx_command() writes 8bit command and reads 8bit data from
 * the psx pad.
 */

static void gc_psx_command(struct gc *gc, int b, unsigned char data[GC_MAX_DEVICES])
{
	int i, j, cmd, read;

	for (i = 0; i < GC_MAX_DEVICES; i++)
		data[i] = 0;

	for (i = 0; i < GC_PSX_LENGTH; i++, b >>= 1) {
		cmd = (b & 1) ? GC_PSX_COMMAND : 0;
		parport_write_data(gc->pd->port, cmd | GC_PSX_POWER);
		udelay(gc_psx_delay);
		read = parport_read_status(gc->pd->port) ^ 0x80;
		for (j = 0; j < GC_MAX_DEVICES; j++)
			data[j] |= (read & gc_status_bit[j] & (gc->pads[GC_PSX] | gc->pads[GC_DDR])) ? (1 << i) : 0;
		parport_write_data(gc->pd->port, cmd | GC_PSX_CLOCK | GC_PSX_POWER);
		udelay(gc_psx_delay);
	}
}

/*
 * gc_psx_read_packet() reads a whole psx packet and returns
 * device identifier code.
 */

static void gc_psx_read_packet(struct gc *gc, unsigned char data[GC_MAX_DEVICES][GC_PSX_BYTES],
			       unsigned char id[GC_MAX_DEVICES])
{
	int i, j, max_len = 0;
	unsigned long flags;
	unsigned char data2[GC_MAX_DEVICES];

	parport_write_data(gc->pd->port, GC_PSX_CLOCK | GC_PSX_SELECT | GC_PSX_POWER);	/* Select pad */
	udelay(gc_psx_delay);
	parport_write_data(gc->pd->port, GC_PSX_CLOCK | GC_PSX_POWER);			/* Deselect, begin command */
	udelay(gc_psx_delay);

	local_irq_save(flags);

	gc_psx_command(gc, 0x01, data2);						/* Access pad */
	gc_psx_command(gc, 0x42, id);							/* Get device ids */
	gc_psx_command(gc, 0, data2);							/* Dump status */

	for (i =0; i < GC_MAX_DEVICES; i++)								/* Find the longest pad */
		if((gc_status_bit[i] & (gc->pads[GC_PSX] | gc->pads[GC_DDR]))
			&& (GC_PSX_LEN(id[i]) > max_len)
			&& (GC_PSX_LEN(id[i]) <= GC_PSX_BYTES))
			max_len = GC_PSX_LEN(id[i]);

	for (i = 0; i < max_len; i++) {						/* Read in all the data */
		gc_psx_command(gc, 0, data2);
		for (j = 0; j < GC_MAX_DEVICES; j++)
			data[j][i] = data2[j];
	}

	local_irq_restore(flags);

	parport_write_data(gc->pd->port, GC_PSX_CLOCK | GC_PSX_SELECT | GC_PSX_POWER);

	for(i = 0; i < GC_MAX_DEVICES; i++)								/* Set id's to the real value */
		id[i] = GC_PSX_ID(id[i]);
}

static void gc_psx_process_packet(struct gc *gc)
{
	unsigned char data[GC_MAX_DEVICES][GC_PSX_BYTES];
	unsigned char id[GC_MAX_DEVICES];
	struct input_dev *dev;
	int i, j;

	gc_psx_read_packet(gc, data, id);

	for (i = 0; i < GC_MAX_DEVICES; i++) {

		dev = gc->dev[i];
		if (!dev)
			continue;

		switch (id[i]) {

			case GC_PSX_RUMBLE:

				input_report_key(dev, BTN_THUMBL, ~data[i][0] & 0x04);
				input_report_key(dev, BTN_THUMBR, ~data[i][0] & 0x02);

			case GC_PSX_NEGCON:
			case GC_PSX_ANALOG:

				if (gc->pads[GC_DDR] & gc_status_bit[i]) {
					for(j = 0; j < 4; j++)
						input_report_key(dev, gc_psx_ddr_btn[j], ~data[i][0] & (0x10 << j));
				} else {
					for (j = 0; j < 4; j++)
						input_report_abs(dev, gc_psx_abs[j + 2], data[i][j + 2]);

					input_report_abs(dev, ABS_X, 128 + !(data[i][0] & 0x20) * 127 - !(data[i][0] & 0x80) * 128);
					input_report_abs(dev, ABS_Y, 128 + !(data[i][0] & 0x40) * 127 - !(data[i][0] & 0x10) * 128);
				}

				for (j = 0; j < 8; j++)
					input_report_key(dev, gc_psx_btn[j], ~data[i][1] & (1 << j));

				input_report_key(dev, BTN_START,  ~data[i][0] & 0x08);
				input_report_key(dev, BTN_SELECT, ~data[i][0] & 0x01);

				input_sync(dev);

				break;

			case GC_PSX_NORMAL:
				if (gc->pads[GC_DDR] & gc_status_bit[i]) {
					for(j = 0; j < 4; j++)
						input_report_key(dev, gc_psx_ddr_btn[j], ~data[i][0] & (0x10 << j));
				} else {
					input_report_abs(dev, ABS_X, 128 + !(data[i][0] & 0x20) * 127 - !(data[i][0] & 0x80) * 128);
					input_report_abs(dev, ABS_Y, 128 + !(data[i][0] & 0x40) * 127 - !(data[i][0] & 0x10) * 128);

					/* for some reason if the extra axes are left unset they drift */
					/* for (j = 0; j < 4; j++)
						input_report_abs(dev, gc_psx_abs[j + 2], 128);
					 * This needs to be debugged properly,
					 * maybe fuzz processing needs to be done in input_sync()
					 *				 --vojtech
					 */
				}

				for (j = 0; j < 8; j++)
					input_report_key(dev, gc_psx_btn[j], ~data[i][1] & (1 << j));

				input_report_key(dev, BTN_START,  ~data[i][0] & 0x08);
				input_report_key(dev, BTN_SELECT, ~data[i][0] & 0x01);

				input_sync(dev);

				break;

			case 0: /* not a pad, ignore */
				break;
		}
	}
}

/*
 * gc_timer() initiates reads of console pads data.
 */

static void gc_timer(unsigned long private)
{
	struct gc *gc = (void *) private;

/*
 * N64 pads - must be read first, any read confuses them for 200 us
 */

	if (gc->pads[GC_N64])
		gc_n64_process_packet(gc);

/*
 * NES and SNES pads or mouse
 */

	if (gc->pads[GC_NES] || gc->pads[GC_SNES] || gc->pads[GC_SNESMOUSE])
		gc_nes_process_packet(gc);

/*
 * Multi and Multi2 joysticks
 */

	if (gc->pads[GC_MULTI] || gc->pads[GC_MULTI2])
		gc_multi_process_packet(gc);

/*
 * PSX controllers
 */

	if (gc->pads[GC_PSX] || gc->pads[GC_DDR])
		gc_psx_process_packet(gc);

	mod_timer(&gc->timer, jiffies + GC_REFRESH_TIME);
}

static int gc_open(struct input_dev *dev)
{
	struct gc *gc = dev->private;
	int err;

	err = mutex_lock_interruptible(&gc->mutex);
	if (err)
		return err;

	if (!gc->used++) {
		parport_claim(gc->pd);
		parport_write_control(gc->pd->port, 0x04);
		mod_timer(&gc->timer, jiffies + GC_REFRESH_TIME);
	}

	mutex_unlock(&gc->mutex);
	return 0;
}

static void gc_close(struct input_dev *dev)
{
	struct gc *gc = dev->private;

	mutex_lock(&gc->mutex);
	if (!--gc->used) {
		del_timer_sync(&gc->timer);
		parport_write_control(gc->pd->port, 0x00);
		parport_release(gc->pd);
	}
	mutex_unlock(&gc->mutex);
}

static int __init gc_setup_pad(struct gc *gc, int idx, int pad_type)
{
	struct input_dev *input_dev;
	int i;

	if (!pad_type)
		return 0;

	if (pad_type < 1 || pad_type > GC_MAX) {
		printk(KERN_WARNING "gamecon.c: Pad type %d unknown\n", pad_type);
		return -EINVAL;
	}

	gc->dev[idx] = input_dev = input_allocate_device();
	if (!input_dev) {
		printk(KERN_ERR "gamecon.c: Not enough memory for input device\n");
		return -ENOMEM;
	}

	input_dev->name = gc_names[pad_type];
	input_dev->phys = gc->phys[idx];
	input_dev->id.bustype = BUS_PARPORT;
	input_dev->id.vendor = 0x0001;
	input_dev->id.product = pad_type;
	input_dev->id.version = 0x0100;
	input_dev->private = gc;

	input_dev->open = gc_open;
	input_dev->close = gc_close;

	if (pad_type != GC_SNESMOUSE) {
		input_dev->evbit[0] = BIT(EV_KEY) | BIT(EV_ABS);

		for (i = 0; i < 2; i++)
			input_set_abs_params(input_dev, ABS_X + i, -1, 1, 0, 0);
	} else
		input_dev->evbit[0] = BIT(EV_KEY) | BIT(EV_REL);

	gc->pads[0] |= gc_status_bit[idx];
	gc->pads[pad_type] |= gc_status_bit[idx];

	switch (pad_type) {

		case GC_N64:
			for (i = 0; i < 10; i++)
				set_bit(gc_n64_btn[i], input_dev->keybit);

			for (i = 0; i < 2; i++) {
				input_set_abs_params(input_dev, ABS_X + i, -127, 126, 0, 2);
				input_set_abs_params(input_dev, ABS_HAT0X + i, -1, 1, 0, 0);
			}

			break;

		case GC_SNESMOUSE:
			set_bit(BTN_LEFT, input_dev->keybit);
			set_bit(BTN_RIGHT, input_dev->keybit);
			set_bit(REL_X, input_dev->relbit);
			set_bit(REL_Y, input_dev->relbit);
			break;

		case GC_SNES:
			for (i = 4; i < 8; i++)
				set_bit(gc_snes_btn[i], input_dev->keybit);
		case GC_NES:
			for (i = 0; i < 4; i++)
				set_bit(gc_snes_btn[i], input_dev->keybit);
			break;

		case GC_MULTI2:
			set_bit(BTN_THUMB, input_dev->keybit);
		case GC_MULTI:
			set_bit(BTN_TRIGGER, input_dev->keybit);
			break;

		case GC_PSX:
			for (i = 0; i < 6; i++)
				input_set_abs_params(input_dev, gc_psx_abs[i], 4, 252, 0, 2);
			for (i = 0; i < 12; i++)
				set_bit(gc_psx_btn[i], input_dev->keybit);

			break;

		case GC_DDR:
			for (i = 0; i < 4; i++)
				set_bit(gc_psx_ddr_btn[i], input_dev->keybit);
			for (i = 0; i < 12; i++)
				set_bit(gc_psx_btn[i], input_dev->keybit);

			break;
	}

	return 0;
}

static struct gc __init *gc_probe(int parport, int *pads, int n_pads)
{
	struct gc *gc;
	struct parport *pp;
	struct pardevice *pd;
	int i;
	int err;

	pp = parport_find_number(parport);
	if (!pp) {
		printk(KERN_ERR "gamecon.c: no such parport\n");
		err = -EINVAL;
		goto err_out;
	}

	pd = parport_register_device(pp, "gamecon", NULL, NULL, NULL, PARPORT_DEV_EXCL, NULL);
	if (!pd) {
		printk(KERN_ERR "gamecon.c: parport busy already - lp.o loaded?\n");
		err = -EBUSY;
		goto err_put_pp;
	}

	gc = kzalloc(sizeof(struct gc), GFP_KERNEL);
	if (!gc) {
		printk(KERN_ERR "gamecon.c: Not enough memory\n");
		err = -ENOMEM;
		goto err_unreg_pardev;
	}

	mutex_init(&gc->mutex);
	gc->pd = pd;
	init_timer(&gc->timer);
	gc->timer.data = (long) gc;
	gc->timer.function = gc_timer;

	for (i = 0; i < n_pads && i < GC_MAX_DEVICES; i++) {
		if (!pads[i])
			continue;

		snprintf(gc->phys[i], sizeof(gc->phys[i]),
			 "%s/input%d", gc->pd->port->name, i);
		err = gc_setup_pad(gc, i, pads[i]);
		if (err)
			goto err_unreg_devs;

		err = input_register_device(gc->dev[i]);
		if (err)
			goto err_free_dev;
	}

	if (!gc->pads[0]) {
		printk(KERN_ERR "gamecon.c: No valid devices specified\n");
		err = -EINVAL;
		goto err_free_gc;
	}

	parport_put_port(pp);
	return gc;

 err_free_dev:
	input_free_device(gc->dev[i]);
 err_unreg_devs:
	while (--i >= 0)
		if (gc->dev[i])
			input_unregister_device(gc->dev[i]);
 err_free_gc:
	kfree(gc);
 err_unreg_pardev:
	parport_unregister_device(pd);
 err_put_pp:
	parport_put_port(pp);
 err_out:
	return ERR_PTR(err);
}

static void gc_remove(struct gc *gc)
{
	int i;

	for (i = 0; i < GC_MAX_DEVICES; i++)
		if (gc->dev[i])
			input_unregister_device(gc->dev[i]);
	parport_unregister_device(gc->pd);
	kfree(gc);
}

static int __init gc_init(void)
{
	int i;
	int have_dev = 0;
	int err = 0;

	for (i = 0; i < GC_MAX_PORTS; i++) {
		if (gc[i].nargs == 0 || gc[i].args[0] < 0)
			continue;

		if (gc[i].nargs < 2) {
			printk(KERN_ERR "gamecon.c: at least one device must be specified\n");
			err = -EINVAL;
			break;
		}

		gc_base[i] = gc_probe(gc[i].args[0], gc[i].args + 1, gc[i].nargs - 1);
		if (IS_ERR(gc_base[i])) {
			err = PTR_ERR(gc_base[i]);
			break;
		}

		have_dev = 1;
	}

	if (err) {
		while (--i >= 0)
			if (gc_base[i])
				gc_remove(gc_base[i]);
		return err;
	}

	return have_dev ? 0 : -ENODEV;
}

static void __exit gc_exit(void)
{
	int i;

	for (i = 0; i < GC_MAX_PORTS; i++)
		if (gc_base[i])
			gc_remove(gc_base[i]);
}

module_init(gc_init);
module_exit(gc_exit);
