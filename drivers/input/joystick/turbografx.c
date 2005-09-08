/*
 * $Id: turbografx.c,v 1.14 2002/01/22 20:30:39 vojtech Exp $
 *
 *  Copyright (c) 1998-2001 Vojtech Pavlik
 *
 *  Based on the work of:
 *	Steffen Schwenke
 */

/*
 * TurboGraFX parallel port interface driver for Linux.
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
#include <linux/parport.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("TurboGraFX parallel port interface driver");
MODULE_LICENSE("GPL");

static int tgfx[] __initdata = { -1, 0, 0, 0, 0, 0, 0, 0 };
static int tgfx_nargs __initdata = 0;
module_param_array_named(map, tgfx, int, &tgfx_nargs, 0);
MODULE_PARM_DESC(map, "Describes first set of devices (<parport#>,<js1>,<js2>,..<js7>");

static int tgfx_2[] __initdata = { -1, 0, 0, 0, 0, 0, 0, 0 };
static int tgfx_nargs_2 __initdata = 0;
module_param_array_named(map2, tgfx_2, int, &tgfx_nargs_2, 0);
MODULE_PARM_DESC(map2, "Describes second set of devices");

static int tgfx_3[] __initdata = { -1, 0, 0, 0, 0, 0, 0, 0 };
static int tgfx_nargs_3 __initdata = 0;
module_param_array_named(map3, tgfx_3, int, &tgfx_nargs_3, 0);
MODULE_PARM_DESC(map3, "Describes third set of devices");

__obsolete_setup("tgfx=");
__obsolete_setup("tgfx_2=");
__obsolete_setup("tgfx_3=");

#define TGFX_REFRESH_TIME	HZ/100	/* 10 ms */

#define TGFX_TRIGGER		0x08
#define TGFX_UP			0x10
#define TGFX_DOWN		0x20
#define TGFX_LEFT		0x40
#define TGFX_RIGHT		0x80

#define TGFX_THUMB		0x02
#define TGFX_THUMB2		0x04
#define TGFX_TOP		0x01
#define TGFX_TOP2		0x08

static int tgfx_buttons[] = { BTN_TRIGGER, BTN_THUMB, BTN_THUMB2, BTN_TOP, BTN_TOP2 };
static char *tgfx_name = "TurboGraFX Multisystem joystick";

static struct tgfx {
	struct pardevice *pd;
	struct timer_list timer;
	struct input_dev dev[7];
	char phys[7][32];
	int sticks;
	int used;
	struct semaphore sem;
} *tgfx_base[3];

/*
 * tgfx_timer() reads and analyzes TurboGraFX joystick data.
 */

static void tgfx_timer(unsigned long private)
{
	struct tgfx *tgfx = (void *) private;
	struct input_dev *dev;
	int data1, data2, i;

	for (i = 0; i < 7; i++)
		if (tgfx->sticks & (1 << i)) {

			dev = tgfx->dev + i;

			parport_write_data(tgfx->pd->port, ~(1 << i));
			data1 = parport_read_status(tgfx->pd->port) ^ 0x7f;
			data2 = parport_read_control(tgfx->pd->port) ^ 0x04;	/* CAVEAT parport */

			input_report_abs(dev, ABS_X, !!(data1 & TGFX_RIGHT) - !!(data1 & TGFX_LEFT));
			input_report_abs(dev, ABS_Y, !!(data1 & TGFX_DOWN ) - !!(data1 & TGFX_UP  ));

			input_report_key(dev, BTN_TRIGGER, (data1 & TGFX_TRIGGER));
			input_report_key(dev, BTN_THUMB,   (data2 & TGFX_THUMB  ));
			input_report_key(dev, BTN_THUMB2,  (data2 & TGFX_THUMB2 ));
			input_report_key(dev, BTN_TOP,     (data2 & TGFX_TOP    ));
			input_report_key(dev, BTN_TOP2,    (data2 & TGFX_TOP2   ));

			input_sync(dev);
		}

	mod_timer(&tgfx->timer, jiffies + TGFX_REFRESH_TIME);
}

static int tgfx_open(struct input_dev *dev)
{
	struct tgfx *tgfx = dev->private;
	int err;

	err = down_interruptible(&tgfx->sem);
	if (err)
		return err;

	if (!tgfx->used++) {
		parport_claim(tgfx->pd);
		parport_write_control(tgfx->pd->port, 0x04);
		mod_timer(&tgfx->timer, jiffies + TGFX_REFRESH_TIME);
	}

	up(&tgfx->sem);
	return 0;
}

static void tgfx_close(struct input_dev *dev)
{
	struct tgfx *tgfx = dev->private;

	down(&tgfx->sem);
	if (!--tgfx->used) {
		del_timer_sync(&tgfx->timer);
		parport_write_control(tgfx->pd->port, 0x00);
		parport_release(tgfx->pd);
	}
	up(&tgfx->sem);
}

/*
 * tgfx_probe() probes for tg gamepads.
 */

static struct tgfx __init *tgfx_probe(int *config, int nargs)
{
	struct tgfx *tgfx;
	struct parport *pp;
	int i, j;

	if (config[0] < 0)
		return NULL;

	if (nargs < 2) {
		printk(KERN_ERR "turbografx.c: at least one joystick must be specified\n");
		return NULL;
	}

	pp = parport_find_number(config[0]);

	if (!pp) {
		printk(KERN_ERR "turbografx.c: no such parport\n");
		return NULL;
	}

	if (!(tgfx = kzalloc(sizeof(struct tgfx), GFP_KERNEL))) {
		parport_put_port(pp);
		return NULL;
	}

	init_MUTEX(&tgfx->sem);

	tgfx->pd = parport_register_device(pp, "turbografx", NULL, NULL, NULL, PARPORT_DEV_EXCL, NULL);

	parport_put_port(pp);

	if (!tgfx->pd) {
		printk(KERN_ERR "turbografx.c: parport busy already - lp.o loaded?\n");
		kfree(tgfx);
		return NULL;
	}

	init_timer(&tgfx->timer);
	tgfx->timer.data = (long) tgfx;
	tgfx->timer.function = tgfx_timer;

	tgfx->sticks = 0;

	for (i = 0; i < nargs - 1; i++)
		if (config[i+1] > 0 && config[i+1] < 6) {

			tgfx->sticks |= (1 << i);

			tgfx->dev[i].private = tgfx;
			tgfx->dev[i].open = tgfx_open;
			tgfx->dev[i].close = tgfx_close;

			sprintf(tgfx->phys[i], "%s/input0", tgfx->pd->port->name);

			tgfx->dev[i].name = tgfx_name;
			tgfx->dev[i].phys = tgfx->phys[i];
			tgfx->dev[i].id.bustype = BUS_PARPORT;
			tgfx->dev[i].id.vendor = 0x0003;
			tgfx->dev[i].id.product = config[i+1];
			tgfx->dev[i].id.version = 0x0100;

			tgfx->dev[i].evbit[0] = BIT(EV_KEY) | BIT(EV_ABS);
			tgfx->dev[i].absbit[0] = BIT(ABS_X) | BIT(ABS_Y);

			for (j = 0; j < config[i+1]; j++)
				set_bit(tgfx_buttons[j], tgfx->dev[i].keybit);

			tgfx->dev[i].absmin[ABS_X] = -1; tgfx->dev[i].absmax[ABS_X] = 1;
			tgfx->dev[i].absmin[ABS_Y] = -1; tgfx->dev[i].absmax[ABS_Y] = 1;

			input_register_device(tgfx->dev + i);
			printk(KERN_INFO "input: %d-button Multisystem joystick on %s\n",
				config[i+1], tgfx->pd->port->name);
		}

        if (!tgfx->sticks) {
		parport_unregister_device(tgfx->pd);
		kfree(tgfx);
		return NULL;
        }

	return tgfx;
}

static int __init tgfx_init(void)
{
	tgfx_base[0] = tgfx_probe(tgfx, tgfx_nargs);
	tgfx_base[1] = tgfx_probe(tgfx_2, tgfx_nargs_2);
	tgfx_base[2] = tgfx_probe(tgfx_3, tgfx_nargs_3);

	if (tgfx_base[0] || tgfx_base[1] || tgfx_base[2])
		return 0;

	return -ENODEV;
}

static void __exit tgfx_exit(void)
{
	int i, j;

	for (i = 0; i < 3; i++)
		if (tgfx_base[i]) {
			for (j = 0; j < 7; j++)
				if (tgfx_base[i]->sticks & (1 << j))
					input_unregister_device(tgfx_base[i]->dev + j);
		parport_unregister_device(tgfx_base[i]->pd);
	}
}

module_init(tgfx_init);
module_exit(tgfx_exit);
