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
#include <linux/mutex.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("TurboGraFX parallel port interface driver");
MODULE_LICENSE("GPL");

#define TGFX_MAX_PORTS		3
#define TGFX_MAX_DEVICES	7

struct tgfx_config {
	int args[TGFX_MAX_DEVICES + 1];
	unsigned int nargs;
};

static struct tgfx_config tgfx_cfg[TGFX_MAX_PORTS] __initdata;

module_param_array_named(map, tgfx_cfg[0].args, int, &tgfx_cfg[0].nargs, 0);
MODULE_PARM_DESC(map, "Describes first set of devices (<parport#>,<js1>,<js2>,..<js7>");
module_param_array_named(map2, tgfx_cfg[1].args, int, &tgfx_cfg[1].nargs, 0);
MODULE_PARM_DESC(map2, "Describes second set of devices");
module_param_array_named(map3, tgfx_cfg[2].args, int, &tgfx_cfg[2].nargs, 0);
MODULE_PARM_DESC(map3, "Describes third set of devices");

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

static struct tgfx {
	struct pardevice *pd;
	struct timer_list timer;
	struct input_dev *dev[TGFX_MAX_DEVICES];
	char name[TGFX_MAX_DEVICES][64];
	char phys[TGFX_MAX_DEVICES][32];
	int sticks;
	int used;
	struct mutex sem;
} *tgfx_base[TGFX_MAX_PORTS];

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

			dev = tgfx->dev[i];

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
	struct tgfx *tgfx = input_get_drvdata(dev);
	int err;

	err = mutex_lock_interruptible(&tgfx->sem);
	if (err)
		return err;

	if (!tgfx->used++) {
		parport_claim(tgfx->pd);
		parport_write_control(tgfx->pd->port, 0x04);
		mod_timer(&tgfx->timer, jiffies + TGFX_REFRESH_TIME);
	}

	mutex_unlock(&tgfx->sem);
	return 0;
}

static void tgfx_close(struct input_dev *dev)
{
	struct tgfx *tgfx = input_get_drvdata(dev);

	mutex_lock(&tgfx->sem);
	if (!--tgfx->used) {
		del_timer_sync(&tgfx->timer);
		parport_write_control(tgfx->pd->port, 0x00);
		parport_release(tgfx->pd);
	}
	mutex_unlock(&tgfx->sem);
}



/*
 * tgfx_probe() probes for tg gamepads.
 */

static struct tgfx __init *tgfx_probe(int parport, int *n_buttons, int n_devs)
{
	struct tgfx *tgfx;
	struct input_dev *input_dev;
	struct parport *pp;
	struct pardevice *pd;
	int i, j;
	int err;

	pp = parport_find_number(parport);
	if (!pp) {
		printk(KERN_ERR "turbografx.c: no such parport\n");
		err = -EINVAL;
		goto err_out;
	}

	pd = parport_register_device(pp, "turbografx", NULL, NULL, NULL, PARPORT_DEV_EXCL, NULL);
	if (!pd) {
		printk(KERN_ERR "turbografx.c: parport busy already - lp.o loaded?\n");
		err = -EBUSY;
		goto err_put_pp;
	}

	tgfx = kzalloc(sizeof(struct tgfx), GFP_KERNEL);
	if (!tgfx) {
		printk(KERN_ERR "turbografx.c: Not enough memory\n");
		err = -ENOMEM;
		goto err_unreg_pardev;
	}

	mutex_init(&tgfx->sem);
	tgfx->pd = pd;
	init_timer(&tgfx->timer);
	tgfx->timer.data = (long) tgfx;
	tgfx->timer.function = tgfx_timer;

	for (i = 0; i < n_devs; i++) {
		if (n_buttons[i] < 1)
			continue;

		if (n_buttons[i] > 6) {
			printk(KERN_ERR "turbografx.c: Invalid number of buttons %d\n", n_buttons[i]);
			err = -EINVAL;
			goto err_unreg_devs;
		}

		tgfx->dev[i] = input_dev = input_allocate_device();
		if (!input_dev) {
			printk(KERN_ERR "turbografx.c: Not enough memory for input device\n");
			err = -ENOMEM;
			goto err_unreg_devs;
		}

		tgfx->sticks |= (1 << i);
		snprintf(tgfx->name[i], sizeof(tgfx->name[i]),
			 "TurboGraFX %d-button Multisystem joystick", n_buttons[i]);
		snprintf(tgfx->phys[i], sizeof(tgfx->phys[i]),
			 "%s/input%d", tgfx->pd->port->name, i);

		input_dev->name = tgfx->name[i];
		input_dev->phys = tgfx->phys[i];
		input_dev->id.bustype = BUS_PARPORT;
		input_dev->id.vendor = 0x0003;
		input_dev->id.product = n_buttons[i];
		input_dev->id.version = 0x0100;

		input_set_drvdata(input_dev, tgfx);

		input_dev->open = tgfx_open;
		input_dev->close = tgfx_close;

		input_dev->evbit[0] = BIT(EV_KEY) | BIT(EV_ABS);
		input_set_abs_params(input_dev, ABS_X, -1, 1, 0, 0);
		input_set_abs_params(input_dev, ABS_Y, -1, 1, 0, 0);

		for (j = 0; j < n_buttons[i]; j++)
			set_bit(tgfx_buttons[j], input_dev->keybit);

		err = input_register_device(tgfx->dev[i]);
		if (err)
			goto err_free_dev;
	}

        if (!tgfx->sticks) {
		printk(KERN_ERR "turbografx.c: No valid devices specified\n");
		err = -EINVAL;
		goto err_free_tgfx;
        }

	return tgfx;

 err_free_dev:
	input_free_device(tgfx->dev[i]);
 err_unreg_devs:
	while (--i >= 0)
		if (tgfx->dev[i])
			input_unregister_device(tgfx->dev[i]);
 err_free_tgfx:
	kfree(tgfx);
 err_unreg_pardev:
	parport_unregister_device(pd);
 err_put_pp:
	parport_put_port(pp);
 err_out:
	return ERR_PTR(err);
}

static void tgfx_remove(struct tgfx *tgfx)
{
	int i;

	for (i = 0; i < TGFX_MAX_DEVICES; i++)
		if (tgfx->dev[i])
			input_unregister_device(tgfx->dev[i]);
	parport_unregister_device(tgfx->pd);
	kfree(tgfx);
}

static int __init tgfx_init(void)
{
	int i;
	int have_dev = 0;
	int err = 0;

	for (i = 0; i < TGFX_MAX_PORTS; i++) {
		if (tgfx_cfg[i].nargs == 0 || tgfx_cfg[i].args[0] < 0)
			continue;

		if (tgfx_cfg[i].nargs < 2) {
			printk(KERN_ERR "turbografx.c: at least one joystick must be specified\n");
			err = -EINVAL;
			break;
		}

		tgfx_base[i] = tgfx_probe(tgfx_cfg[i].args[0],
					  tgfx_cfg[i].args + 1,
					  tgfx_cfg[i].nargs - 1);
		if (IS_ERR(tgfx_base[i])) {
			err = PTR_ERR(tgfx_base[i]);
			break;
		}

		have_dev = 1;
	}

	if (err) {
		while (--i >= 0)
			if (tgfx_base[i])
				tgfx_remove(tgfx_base[i]);
		return err;
	}

	return have_dev ? 0 : -ENODEV;
}

static void __exit tgfx_exit(void)
{
	int i;

	for (i = 0; i < TGFX_MAX_PORTS; i++)
		if (tgfx_base[i])
			tgfx_remove(tgfx_base[i]);
}

module_init(tgfx_init);
module_exit(tgfx_exit);
