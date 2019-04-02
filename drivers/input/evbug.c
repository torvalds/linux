/*
 *  Copyright (c) 1999-2001 Vojtech Pavlik
 */

/*
 *  Input driver event de module - dumps all events into syslog
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
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/init.h>
#include <linux/device.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("Input driver event de module");
MODULE_LICENSE("GPL");

static void ev_event(struct input_handle *handle, unsigned int type, unsigned int code, int value)
{
	printk(KERN_DE pr_fmt("Event. Dev: %s, Type: %d, Code: %d, Value: %d\n"),
	       dev_name(&handle->dev->dev), type, code, value);
}

static int ev_connect(struct input_handler *handler, struct input_dev *dev,
			 const struct input_device_id *id)
{
	struct input_handle *handle;
	int error;

	handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "ev";

	error = input_register_handle(handle);
	if (error)
		goto err_free_handle;

	error = input_open_device(handle);
	if (error)
		goto err_unregister_handle;

	printk(KERN_DE pr_fmt("Connected device: %s (%s at %s)\n"),
	       dev_name(&dev->dev),
	       dev->name ?: "unknown",
	       dev->phys ?: "unknown");

	return 0;

 err_unregister_handle:
	input_unregister_handle(handle);
 err_free_handle:
	kfree(handle);
	return error;
}

static void ev_disconnect(struct input_handle *handle)
{
	printk(KERN_DE pr_fmt("Disconnected device: %s\n"),
	       dev_name(&handle->dev->dev));

	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id ev_ids[] = {
	{ .driver_info = 1 },	/* Matches all devices */
	{ },			/* Terminating zero entry */
};

MODULE_DEVICE_TABLE(input, ev_ids);

static struct input_handler ev_handler = {
	.event =	ev_event,
	.connect =	ev_connect,
	.disconnect =	ev_disconnect,
	.name =		"ev",
	.id_table =	ev_ids,
};

static int __init ev_init(void)
{
	return input_register_handler(&ev_handler);
}

static void __exit ev_exit(void)
{
	input_unregister_handler(&ev_handler);
}

module_init(ev_init);
module_exit(ev_exit);
