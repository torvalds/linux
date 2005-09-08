/*
 * NES, SNES, N64, MultiSystem, PSX gamepad driver for Linux
 *
 *  Copyright (c) 1999-2004	Vojtech Pavlik <vojtech@suse.cz>
 *  Copyright (c) 2004		Peter Nelson <rufus-kernel@hackish.org>
 *
 *  Based on the work of:
 *	Andree Borrmann		John Dahlstrom
 *	David Kuder		Nathan Hand
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

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("NES, SNES, N64, MultiSystem, PSX gamepad driver");
MODULE_LICENSE("GPL");

static int gc[] __initdata = { -1, 0, 0, 0, 0, 0 };
static int gc_nargs __initdata = 0;
module_param_array_named(map, gc, int, &gc_nargs, 0);
MODULE_PARM_DESC(map, "Describers first set of devices (<parport#>,<pad1>,<pad2>,..<pad5>)");

static int gc_2[] __initdata = { -1, 0, 0, 0, 0, 0 };
static int gc_nargs_2 __initdata = 0;
module_param_array_named(map2, gc_2, int, &gc_nargs_2, 0);
MODULE_PARM_DESC(map2, "Describers second set of devices");

static int gc_3[] __initdata = { -1, 0, 0, 0, 0, 0 };
static int gc_nargs_3 __initdata = 0;
module_param_array_named(map3, gc_3, int, &gc_nargs_3, 0);
MODULE_PARM_DESC(map3, "Describers third set of devices");

__obsolete_setup("gc=");
__obsolete_setup("gc_2=");
__obsolete_setup("gc_3=");

/* see also gs_psx_delay parameter in PSX support section */

#define GC_SNES		1
#define GC_NES		2
#define GC_NES4		3
#define GC_MULTI	4
#define GC_MULTI2	5
#define GC_N64		6
#define GC_PSX		7
#define GC_DDR		8

#define GC_MAX		8

#define GC_REFRESH_TIME	HZ/100

struct gc {
	struct pardevice *pd;
	struct input_dev dev[5];
	struct timer_list timer;
	unsigned char pads[GC_MAX + 1];
	int used;
	struct semaphore sem;
	char phys[5][32];
};

static struct gc *gc_base[3];

static int gc_status_bit[] = { 0x40, 0x80, 0x20, 0x10, 0x08 };

static char *gc_names[] = { NULL, "SNES pad", "NES pad", "NES FourPort", "Multisystem joystick",
				"Multisystem 2-button joystick", "N64 controller", "PSX controller",
				"PSX DDR controller" };
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

/*
 * NES/SNES support.
 */

#define GC_NES_DELAY	6	/* Delay between bits - 6us */
#define GC_NES_LENGTH	8	/* The NES pads use 8 bits of data */
#define GC_SNES_LENGTH	12	/* The SNES true length is 16, but the last 4 bits are unused */

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

__obsolete_setup("gc_psx_delay=");

static short gc_psx_abs[] = { ABS_X, ABS_Y, ABS_RX, ABS_RY, ABS_HAT0X, ABS_HAT0Y };
static short gc_psx_btn[] = { BTN_TL, BTN_TR, BTN_TL2, BTN_TR2, BTN_A, BTN_B, BTN_X, BTN_Y,
				BTN_START, BTN_SELECT, BTN_THUMBL, BTN_THUMBR };
static short gc_psx_ddr_btn[] = { BTN_0, BTN_1, BTN_2, BTN_3 };

/*
 * gc_psx_command() writes 8bit command and reads 8bit data from
 * the psx pad.
 */

static void gc_psx_command(struct gc *gc, int b, unsigned char data[5])
{
	int i, j, cmd, read;
	for (i = 0; i < 5; i++)
		data[i] = 0;

	for (i = 0; i < GC_PSX_LENGTH; i++, b >>= 1) {
		cmd = (b & 1) ? GC_PSX_COMMAND : 0;
		parport_write_data(gc->pd->port, cmd | GC_PSX_POWER);
		udelay(gc_psx_delay);
		read = parport_read_status(gc->pd->port) ^ 0x80;
		for (j = 0; j < 5; j++)
			data[j] |= (read & gc_status_bit[j] & (gc->pads[GC_PSX] | gc->pads[GC_DDR])) ? (1 << i) : 0;
		parport_write_data(gc->pd->port, cmd | GC_PSX_CLOCK | GC_PSX_POWER);
		udelay(gc_psx_delay);
	}
}

/*
 * gc_psx_read_packet() reads a whole psx packet and returns
 * device identifier code.
 */

static void gc_psx_read_packet(struct gc *gc, unsigned char data[5][GC_PSX_BYTES], unsigned char id[5])
{
	int i, j, max_len = 0;
	unsigned long flags;
	unsigned char data2[5];

	parport_write_data(gc->pd->port, GC_PSX_CLOCK | GC_PSX_SELECT | GC_PSX_POWER);	/* Select pad */
	udelay(gc_psx_delay);
	parport_write_data(gc->pd->port, GC_PSX_CLOCK | GC_PSX_POWER);			/* Deselect, begin command */
	udelay(gc_psx_delay);

	local_irq_save(flags);

	gc_psx_command(gc, 0x01, data2);						/* Access pad */
	gc_psx_command(gc, 0x42, id);							/* Get device ids */
	gc_psx_command(gc, 0, data2);							/* Dump status */

	for (i =0; i < 5; i++)								/* Find the longest pad */
		if((gc_status_bit[i] & (gc->pads[GC_PSX] | gc->pads[GC_DDR]))
			&& (GC_PSX_LEN(id[i]) > max_len)
			&& (GC_PSX_LEN(id[i]) <= GC_PSX_BYTES))
			max_len = GC_PSX_LEN(id[i]);

	for (i = 0; i < max_len; i++) {						/* Read in all the data */
		gc_psx_command(gc, 0, data2);
		for (j = 0; j < 5; j++)
			data[j][i] = data2[j];
	}

	local_irq_restore(flags);

	parport_write_data(gc->pd->port, GC_PSX_CLOCK | GC_PSX_SELECT | GC_PSX_POWER);

	for(i = 0; i < 5; i++)								/* Set id's to the real value */
		id[i] = GC_PSX_ID(id[i]);
}

/*
 * gc_timer() reads and analyzes console pads data.
 */

#define GC_MAX_LENGTH GC_N64_LENGTH

static void gc_timer(unsigned long private)
{
	struct gc *gc = (void *) private;
	struct input_dev *dev = gc->dev;
	unsigned char data[GC_MAX_LENGTH];
	unsigned char data_psx[5][GC_PSX_BYTES];
	int i, j, s;

/*
 * N64 pads - must be read first, any read confuses them for 200 us
 */

	if (gc->pads[GC_N64]) {

		gc_n64_read_packet(gc, data);

		for (i = 0; i < 5; i++) {

			s = gc_status_bit[i];

			if (s & gc->pads[GC_N64] & ~(data[8] | data[9])) {

				signed char axes[2];
				axes[0] = axes[1] = 0;

				for (j = 0; j < 8; j++) {
					if (data[23 - j] & s) axes[0] |= 1 << j;
					if (data[31 - j] & s) axes[1] |= 1 << j;
				}

				input_report_abs(dev + i, ABS_X,  axes[0]);
				input_report_abs(dev + i, ABS_Y, -axes[1]);

				input_report_abs(dev + i, ABS_HAT0X, !(s & data[6]) - !(s & data[7]));
				input_report_abs(dev + i, ABS_HAT0Y, !(s & data[4]) - !(s & data[5]));

				for (j = 0; j < 10; j++)
					input_report_key(dev + i, gc_n64_btn[j], s & data[gc_n64_bytes[j]]);

				input_sync(dev + i);
			}
		}
	}

/*
 * NES and SNES pads
 */

	if (gc->pads[GC_NES] || gc->pads[GC_SNES]) {

		gc_nes_read_packet(gc, gc->pads[GC_SNES] ? GC_SNES_LENGTH : GC_NES_LENGTH, data);

		for (i = 0; i < 5; i++) {

			s = gc_status_bit[i];

			if (s & (gc->pads[GC_NES] | gc->pads[GC_SNES])) {
				input_report_abs(dev + i, ABS_X, !(s & data[6]) - !(s & data[7]));
				input_report_abs(dev + i, ABS_Y, !(s & data[4]) - !(s & data[5]));
			}

			if (s & gc->pads[GC_NES])
				for (j = 0; j < 4; j++)
					input_report_key(dev + i, gc_snes_btn[j], s & data[gc_nes_bytes[j]]);

			if (s & gc->pads[GC_SNES])
				for (j = 0; j < 8; j++)
					input_report_key(dev + i, gc_snes_btn[j], s & data[gc_snes_bytes[j]]);

			input_sync(dev + i);
		}
	}

/*
 * Multi and Multi2 joysticks
 */

	if (gc->pads[GC_MULTI] || gc->pads[GC_MULTI2]) {

		gc_multi_read_packet(gc, gc->pads[GC_MULTI2] ? GC_MULTI2_LENGTH : GC_MULTI_LENGTH, data);

		for (i = 0; i < 5; i++) {

			s = gc_status_bit[i];

			if (s & (gc->pads[GC_MULTI] | gc->pads[GC_MULTI2])) {
				input_report_abs(dev + i, ABS_X,  !(s & data[2]) - !(s & data[3]));
				input_report_abs(dev + i, ABS_Y,  !(s & data[0]) - !(s & data[1]));
				input_report_key(dev + i, BTN_TRIGGER, s & data[4]);
			}

			if (s & gc->pads[GC_MULTI2])
				input_report_key(dev + i, BTN_THUMB, s & data[5]);

			input_sync(dev + i);
		}
	}

/*
 * PSX controllers
 */

	if (gc->pads[GC_PSX] || gc->pads[GC_DDR]) {

		gc_psx_read_packet(gc, data_psx, data);

		for (i = 0; i < 5; i++) {
			switch (data[i]) {

				case GC_PSX_RUMBLE:

					input_report_key(dev + i, BTN_THUMBL, ~data_psx[i][0] & 0x04);
					input_report_key(dev + i, BTN_THUMBR, ~data_psx[i][0] & 0x02);

				case GC_PSX_NEGCON:
				case GC_PSX_ANALOG:

					if(gc->pads[GC_DDR] & gc_status_bit[i]) {
						for(j = 0; j < 4; j++)
							input_report_key(dev + i, gc_psx_ddr_btn[j], ~data_psx[i][0] & (0x10 << j));
					} else {
						for (j = 0; j < 4; j++)
							input_report_abs(dev + i, gc_psx_abs[j+2], data_psx[i][j + 2]);

						input_report_abs(dev + i, ABS_X, 128 + !(data_psx[i][0] & 0x20) * 127 - !(data_psx[i][0] & 0x80) * 128);
						input_report_abs(dev + i, ABS_Y, 128 + !(data_psx[i][0] & 0x40) * 127 - !(data_psx[i][0] & 0x10) * 128);
					}

					for (j = 0; j < 8; j++)
						input_report_key(dev + i, gc_psx_btn[j], ~data_psx[i][1] & (1 << j));

					input_report_key(dev + i, BTN_START,  ~data_psx[i][0] & 0x08);
					input_report_key(dev + i, BTN_SELECT, ~data_psx[i][0] & 0x01);

					input_sync(dev + i);

					break;

				case GC_PSX_NORMAL:
					if(gc->pads[GC_DDR] & gc_status_bit[i]) {
						for(j = 0; j < 4; j++)
							input_report_key(dev + i, gc_psx_ddr_btn[j], ~data_psx[i][0] & (0x10 << j));
					} else {
						input_report_abs(dev + i, ABS_X, 128 + !(data_psx[i][0] & 0x20) * 127 - !(data_psx[i][0] & 0x80) * 128);
						input_report_abs(dev + i, ABS_Y, 128 + !(data_psx[i][0] & 0x40) * 127 - !(data_psx[i][0] & 0x10) * 128);

						/* for some reason if the extra axes are left unset they drift */
						/* for (j = 0; j < 4; j++)
							input_report_abs(dev + i, gc_psx_abs[j+2], 128);
						 * This needs to be debugged properly,
						 * maybe fuzz processing needs to be done in input_sync()
						 *				 --vojtech
						 */
					}

					for (j = 0; j < 8; j++)
						input_report_key(dev + i, gc_psx_btn[j], ~data_psx[i][1] & (1 << j));

					input_report_key(dev + i, BTN_START,  ~data_psx[i][0] & 0x08);
					input_report_key(dev + i, BTN_SELECT, ~data_psx[i][0] & 0x01);

					input_sync(dev + i);

					break;

				case 0: /* not a pad, ignore */
					break;
			}
		}
	}

	mod_timer(&gc->timer, jiffies + GC_REFRESH_TIME);
}

static int gc_open(struct input_dev *dev)
{
	struct gc *gc = dev->private;
	int err;

	err = down_interruptible(&gc->sem);
	if (err)
		return err;

	if (!gc->used++) {
		parport_claim(gc->pd);
		parport_write_control(gc->pd->port, 0x04);
		mod_timer(&gc->timer, jiffies + GC_REFRESH_TIME);
	}

	up(&gc->sem);
	return 0;
}

static void gc_close(struct input_dev *dev)
{
	struct gc *gc = dev->private;

	down(&gc->sem);
	if (!--gc->used) {
		del_timer_sync(&gc->timer);
		parport_write_control(gc->pd->port, 0x00);
		parport_release(gc->pd);
	}
	up(&gc->sem);
}

static struct gc __init *gc_probe(int *config, int nargs)
{
	struct gc *gc;
	struct parport *pp;
	int i, j;

	if (config[0] < 0)
		return NULL;

	if (nargs < 2) {
		printk(KERN_ERR "gamecon.c: at least one device must be specified\n");
		return NULL;
	}

	pp = parport_find_number(config[0]);

	if (!pp) {
		printk(KERN_ERR "gamecon.c: no such parport\n");
		return NULL;
	}

	if (!(gc = kzalloc(sizeof(struct gc), GFP_KERNEL))) {
		parport_put_port(pp);
		return NULL;
	}

	init_MUTEX(&gc->sem);

	gc->pd = parport_register_device(pp, "gamecon", NULL, NULL, NULL, PARPORT_DEV_EXCL, NULL);

	parport_put_port(pp);

	if (!gc->pd) {
		printk(KERN_ERR "gamecon.c: parport busy already - lp.o loaded?\n");
		kfree(gc);
		return NULL;
	}

	parport_claim(gc->pd);

	init_timer(&gc->timer);
	gc->timer.data = (long) gc;
	gc->timer.function = gc_timer;

	for (i = 0; i < nargs - 1; i++) {

		if (!config[i + 1])
			continue;

		if (config[i + 1] < 1 || config[i + 1] > GC_MAX) {
			printk(KERN_WARNING "gamecon.c: Pad type %d unknown\n", config[i + 1]);
			continue;
		}

                gc->dev[i].private = gc;
                gc->dev[i].open = gc_open;
                gc->dev[i].close = gc_close;

                gc->dev[i].evbit[0] = BIT(EV_KEY) | BIT(EV_ABS);

		for (j = 0; j < 2; j++) {
			set_bit(ABS_X + j, gc->dev[i].absbit);
			gc->dev[i].absmin[ABS_X + j] = -1;
			gc->dev[i].absmax[ABS_X + j] =  1;
		}

		gc->pads[0] |= gc_status_bit[i];
		gc->pads[config[i + 1]] |= gc_status_bit[i];

		switch(config[i + 1]) {

			case GC_N64:
				for (j = 0; j < 10; j++)
					set_bit(gc_n64_btn[j], gc->dev[i].keybit);

				for (j = 0; j < 2; j++) {
					set_bit(ABS_X + j, gc->dev[i].absbit);
					gc->dev[i].absmin[ABS_X + j] = -127;
					gc->dev[i].absmax[ABS_X + j] =  126;
					gc->dev[i].absflat[ABS_X + j] = 2;
					set_bit(ABS_HAT0X + j, gc->dev[i].absbit);
					gc->dev[i].absmin[ABS_HAT0X + j] = -1;
					gc->dev[i].absmax[ABS_HAT0X + j] =  1;
				}

				break;

			case GC_SNES:
				for (j = 4; j < 8; j++)
					set_bit(gc_snes_btn[j], gc->dev[i].keybit);
			case GC_NES:
				for (j = 0; j < 4; j++)
					set_bit(gc_snes_btn[j], gc->dev[i].keybit);
				break;

			case GC_MULTI2:
				set_bit(BTN_THUMB, gc->dev[i].keybit);
			case GC_MULTI:
				set_bit(BTN_TRIGGER, gc->dev[i].keybit);
				break;

			case GC_PSX:
			case GC_DDR:
				if(config[i + 1] == GC_DDR) {
					for (j = 0; j < 4; j++)
						set_bit(gc_psx_ddr_btn[j], gc->dev[i].keybit);
				} else {
					for (j = 0; j < 6; j++) {
						set_bit(gc_psx_abs[j], gc->dev[i].absbit);
						gc->dev[i].absmin[gc_psx_abs[j]] = 4;
						gc->dev[i].absmax[gc_psx_abs[j]] = 252;
						gc->dev[i].absflat[gc_psx_abs[j]] = 2;
					}
				}

				for (j = 0; j < 12; j++)
					set_bit(gc_psx_btn[j], gc->dev[i].keybit);

				break;
		}

		sprintf(gc->phys[i], "%s/input%d", gc->pd->port->name, i);

                gc->dev[i].name = gc_names[config[i + 1]];
		gc->dev[i].phys = gc->phys[i];
                gc->dev[i].id.bustype = BUS_PARPORT;
                gc->dev[i].id.vendor = 0x0001;
                gc->dev[i].id.product = config[i + 1];
                gc->dev[i].id.version = 0x0100;
	}

	parport_release(gc->pd);

	if (!gc->pads[0]) {
		parport_unregister_device(gc->pd);
		kfree(gc);
		return NULL;
	}

	for (i = 0; i < 5; i++)
		if (gc->pads[0] & gc_status_bit[i]) {
			input_register_device(gc->dev + i);
			printk(KERN_INFO "input: %s on %s\n", gc->dev[i].name, gc->pd->port->name);
		}

	return gc;
}

static int __init gc_init(void)
{
	gc_base[0] = gc_probe(gc, gc_nargs);
	gc_base[1] = gc_probe(gc_2, gc_nargs_2);
	gc_base[2] = gc_probe(gc_3, gc_nargs_3);

	if (gc_base[0] || gc_base[1] || gc_base[2])
		return 0;

	return -ENODEV;
}

static void __exit gc_exit(void)
{
	int i, j;

	for (i = 0; i < 3; i++)
		if (gc_base[i]) {
			for (j = 0; j < 5; j++)
				if (gc_base[i]->pads[0] & gc_status_bit[j])
					input_unregister_device(gc_base[i]->dev + j);
			parport_unregister_device(gc_base[i]->pd);
		}
}

module_init(gc_init);
module_exit(gc_exit);
