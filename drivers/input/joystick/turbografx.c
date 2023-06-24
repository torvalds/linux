// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) 1998-2001 Vojtech Pavlik
 *
 *  Based on the work of:
 *	Steffen Schwenke
 */

/*
 * TurboGraFX parallel port interface driver for Linux.
 */

#include <linux/kernel.h>
#include <linux/parport.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/slab.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("TurboGraFX parallel port interface driver");
MODULE_LICENSE("GPL");

#define TGFX_MAX_PORTS		3
#define TGFX_MAX_DEVICES	7

struct tgfx_config {
	int args[TGFX_MAX_DEVICES + 1];
	unsigned int nargs;
};

static struct tgfx_config tgfx_cfg[TGFX_MAX_PORTS];

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
	int parportno;
	struct mutex sem;
} *tgfx_base[TGFX_MAX_PORTS];

/*
 * tgfx_timer() reads and analyzes TurboGraFX joystick data.
 */

static void tgfx_timer(struct timer_list *t)
{
	struct tgfx *tgfx = from_timer(tgfx, t, timer);
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

static void tgfx_attach(struct parport *pp)
{
	struct tgfx *tgfx;
	struct input_dev *input_dev;
	struct pardevice *pd;
	int i, j, port_idx;
	int *n_buttons, n_devs;
	struct pardev_cb tgfx_parport_cb;

	for (port_idx = 0; port_idx < TGFX_MAX_PORTS; port_idx++) {
		if (tgfx_cfg[port_idx].nargs == 0 ||
		    tgfx_cfg[port_idx].args[0] < 0)
			continue;
		if (tgfx_cfg[port_idx].args[0] == pp->number)
			break;
	}

	if (port_idx == TGFX_MAX_PORTS) {
		pr_debug("Not using parport%d.\n", pp->number);
		return;
	}
	n_buttons = tgfx_cfg[port_idx].args + 1;
	n_devs = tgfx_cfg[port_idx].nargs - 1;

	memset(&tgfx_parport_cb, 0, sizeof(tgfx_parport_cb));
	tgfx_parport_cb.flags = PARPORT_FLAG_EXCL;

	pd = parport_register_dev_model(pp, "turbografx", &tgfx_parport_cb,
					port_idx);
	if (!pd) {
		pr_err("parport busy already - lp.o loaded?\n");
		return;
	}

	tgfx = kzalloc(sizeof(struct tgfx), GFP_KERNEL);
	if (!tgfx) {
		printk(KERN_ERR "turbografx.c: Not enough memory\n");
		goto err_unreg_pardev;
	}

	mutex_init(&tgfx->sem);
	tgfx->pd = pd;
	tgfx->parportno = pp->number;
	timer_setup(&tgfx->timer, tgfx_timer, 0);

	for (i = 0; i < n_devs; i++) {
		if (n_buttons[i] < 1)
			continue;

		if (n_buttons[i] > ARRAY_SIZE(tgfx_buttons)) {
			printk(KERN_ERR "turbografx.c: Invalid number of buttons %d\n", n_buttons[i]);
			goto err_unreg_devs;
		}

		tgfx->dev[i] = input_dev = input_allocate_device();
		if (!input_dev) {
			printk(KERN_ERR "turbografx.c: Not enough memory for input device\n");
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

		input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
		input_set_abs_params(input_dev, ABS_X, -1, 1, 0, 0);
		input_set_abs_params(input_dev, ABS_Y, -1, 1, 0, 0);

		for (j = 0; j < n_buttons[i]; j++)
			set_bit(tgfx_buttons[j], input_dev->keybit);

		if (input_register_device(tgfx->dev[i]))
			goto err_free_dev;
	}

        if (!tgfx->sticks) {
		printk(KERN_ERR "turbografx.c: No valid devices specified\n");
		goto err_free_tgfx;
        }

	tgfx_base[port_idx] = tgfx;
	return;

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
}

static void tgfx_detach(struct parport *port)
{
	int i;
	struct tgfx *tgfx;

	for (i = 0; i < TGFX_MAX_PORTS; i++) {
		if (tgfx_base[i] && tgfx_base[i]->parportno == port->number)
			break;
	}

	if (i == TGFX_MAX_PORTS)
		return;

	tgfx = tgfx_base[i];
	tgfx_base[i] = NULL;

	for (i = 0; i < TGFX_MAX_DEVICES; i++)
		if (tgfx->dev[i])
			input_unregister_device(tgfx->dev[i]);
	parport_unregister_device(tgfx->pd);
	kfree(tgfx);
}

static struct parport_driver tgfx_parport_driver = {
	.name = "turbografx",
	.match_port = tgfx_attach,
	.detach = tgfx_detach,
	.devmodel = true,
};

static int __init tgfx_init(void)
{
	int i;
	int have_dev = 0;

	for (i = 0; i < TGFX_MAX_PORTS; i++) {
		if (tgfx_cfg[i].nargs == 0 || tgfx_cfg[i].args[0] < 0)
			continue;

		if (tgfx_cfg[i].nargs < 2) {
			printk(KERN_ERR "turbografx.c: at least one joystick must be specified\n");
			return -EINVAL;
		}

		have_dev = 1;
	}

	if (!have_dev)
		return -ENODEV;

	return parport_register_driver(&tgfx_parport_driver);
}

static void __exit tgfx_exit(void)
{
	parport_unregister_driver(&tgfx_parport_driver);
}

module_init(tgfx_init);
module_exit(tgfx_exit);
