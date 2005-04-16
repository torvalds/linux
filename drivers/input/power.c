/*
 * $Id: power.c,v 1.10 2001/09/25 09:17:15 vojtech Exp $
 *
 *  Copyright (c) 2001 "Crazy" James Simmons
 *
 *  Input driver Power Management.
 *
 *  Sponsored by Transvirtual Technology.
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
 * Should you need to contact me, the author, you can do so by
 * e-mail - mail your message to <jsimmons@transvirtual.com>.
 */

#include <linux/module.h>
#include <linux/config.h>
#include <linux/input.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <linux/delay.h>
#include <linux/pm.h>

static struct input_handler power_handler;

/*
 * Power management can't be done in a interrupt context. So we have to
 * use keventd.
 */
static int suspend_button_pushed = 0;
static void suspend_button_task_handler(void *data)
{
        udelay(200); /* debounce */
        suspend_button_pushed = 0;
}

static DECLARE_WORK(suspend_button_task, suspend_button_task_handler, NULL);

static void power_event(struct input_handle *handle, unsigned int type,
		        unsigned int code, int down)
{
	struct input_dev *dev = handle->dev;

	printk("Entering power_event\n");

	if (type == EV_PWR) {
		switch (code) {
			case KEY_SUSPEND:
				printk("Powering down entire device\n");

				if (!suspend_button_pushed) {
                			suspend_button_pushed = 1;
                        		schedule_work(&suspend_button_task);
                		}
				break;
			case KEY_POWER:
				/* Hum power down the machine. */
				break;
			default:
				return;
		}
	}

	if (type == EV_KEY) {
		switch (code) {
			case KEY_SUSPEND:
				printk("Powering down input device\n");
				/* This is risky. See pm.h for details. */
				if (dev->state != PM_RESUME)
					dev->state = PM_RESUME;
				else
					dev->state = PM_SUSPEND;
				pm_send(dev->pm_dev, dev->state, dev);
				break;
			case KEY_POWER:
				/* Turn the input device off completely ? */
				break;
			default:
				return;
		}
	}
	return;
}

static struct input_handle *power_connect(struct input_handler *handler,
					  struct input_dev *dev,
					  struct input_device_id *id)
{
	struct input_handle *handle;

	if (!(handle = kmalloc(sizeof(struct input_handle), GFP_KERNEL)))
		return NULL;
	memset(handle, 0, sizeof(struct input_handle));

	handle->dev = dev;
	handle->handler = handler;

	input_open_device(handle);

	printk(KERN_INFO "power.c: Adding power management to input layer\n");
	return handle;
}

static void power_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	kfree(handle);
}

static struct input_device_id power_ids[] = {
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT | INPUT_DEVICE_ID_MATCH_KEYBIT,
		.evbit = { BIT(EV_KEY) },
		.keybit = { [LONG(KEY_SUSPEND)] = BIT(KEY_SUSPEND) }
	},
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT | INPUT_DEVICE_ID_MATCH_KEYBIT,
		.evbit = { BIT(EV_KEY) },
		.keybit = { [LONG(KEY_POWER)] = BIT(KEY_POWER) }
	},
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT,
		.evbit = { BIT(EV_PWR) },
	},
	{ }, 	/* Terminating entry */
};

MODULE_DEVICE_TABLE(input, power_ids);

static struct input_handler power_handler = {
	.event =	power_event,
	.connect =	power_connect,
	.disconnect =	power_disconnect,
	.name =		"power",
	.id_table =	power_ids,
};

static int __init power_init(void)
{
	input_register_handler(&power_handler);
	return 0;
}

static void __exit power_exit(void)
{
	input_unregister_handler(&power_handler);
}

module_init(power_init);
module_exit(power_exit);

MODULE_AUTHOR("James Simmons <jsimmons@transvirtual.com>");
MODULE_DESCRIPTION("Input Power Management driver");
MODULE_LICENSE("GPL");
