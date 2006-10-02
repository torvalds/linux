/*
 * $Id: evbug.c,v 1.10 2001/09/25 10:12:07 vojtech Exp $
 *
 *  Copyright (c) 1999-2001 Vojtech Pavlik
 */

/*
 *  Input driver event debug module - dumps all events into syslog
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

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/init.h>
#include <linux/device.h>

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION("Input driver event debug module");
MODULE_LICENSE("GPL");

static char evbug_name[] = "evbug";

static void evbug_event(struct input_handle *handle, unsigned int type, unsigned int code, int value)
{
	printk(KERN_DEBUG "evbug.c: Event. Dev: %s, Type: %d, Code: %d, Value: %d\n",
		handle->dev->phys, type, code, value);
}

static struct input_handle *evbug_connect(struct input_handler *handler, struct input_dev *dev,
					  const struct input_device_id *id)
{
	struct input_handle *handle;

	if (!(handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL)))
		return NULL;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = evbug_name;

	input_open_device(handle);

	printk(KERN_DEBUG "evbug.c: Connected device: \"%s\", %s\n", dev->name, dev->phys);

	return handle;
}

static void evbug_disconnect(struct input_handle *handle)
{
	printk(KERN_DEBUG "evbug.c: Disconnected device: %s\n", handle->dev->phys);

	input_close_device(handle);

	kfree(handle);
}

static const struct input_device_id evbug_ids[] = {
	{ .driver_info = 1 },	/* Matches all devices */
	{ },			/* Terminating zero entry */
};

MODULE_DEVICE_TABLE(input, evbug_ids);

static struct input_handler evbug_handler = {
	.event =	evbug_event,
	.connect =	evbug_connect,
	.disconnect =	evbug_disconnect,
	.name =		"evbug",
	.id_table =	evbug_ids,
};

static int __init evbug_init(void)
{
	return input_register_handler(&evbug_handler);
}

static void __exit evbug_exit(void)
{
	input_unregister_handler(&evbug_handler);
}

module_init(evbug_init);
module_exit(evbug_exit);
