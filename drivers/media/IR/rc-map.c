/* ir-raw-event.c - handle IR Pulse/Space event
 *
 * Copyright (C) 2010 by Mauro Carvalho Chehab <mchehab@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#include <media/ir-core.h>
#include <linux/spinlock.h>
#include <linux/delay.h>

/* Used to handle IR raw handler extensions */
static LIST_HEAD(rc_map_list);
static DEFINE_SPINLOCK(rc_map_lock);

static struct rc_keymap *seek_rc_map(const char *name)
{
	struct rc_keymap *map = NULL;

	spin_lock(&rc_map_lock);
	list_for_each_entry(map, &rc_map_list, list) {
		if (!strcmp(name, map->map.name)) {
			spin_unlock(&rc_map_lock);
			return map;
		}
	}
	spin_unlock(&rc_map_lock);

	return NULL;
}

struct ir_scancode_table *get_rc_map(const char *name)
{

	struct rc_keymap *map;

	map = seek_rc_map(name);
#ifdef MODULE
	if (!map) {
		int rc = request_module(name);
		if (rc < 0) {
			printk(KERN_ERR "Couldn't load IR keymap %s\n", name);
			return NULL;
		}
		msleep(20);	/* Give some time for IR to register */

		map = seek_rc_map(name);
	}
#endif
	if (!map) {
		printk(KERN_ERR "IR keymap %s not found\n", name);
		return NULL;
	}

	printk(KERN_INFO "Registered IR keymap %s\n", map->map.name);

	return &map->map;
}
EXPORT_SYMBOL_GPL(get_rc_map);

int ir_register_map(struct rc_keymap *map)
{
	spin_lock(&rc_map_lock);
	list_add_tail(&map->list, &rc_map_list);
	spin_unlock(&rc_map_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(ir_register_map);

void ir_unregister_map(struct rc_keymap *map)
{
	spin_lock(&rc_map_lock);
	list_del(&map->list);
	spin_unlock(&rc_map_lock);
}
EXPORT_SYMBOL_GPL(ir_unregister_map);


static struct ir_scancode empty[] = {
	{ 0x2a, KEY_COFFEE },
};

static struct rc_keymap empty_map = {
	.map = {
		.scan    = empty,
		.size    = ARRAY_SIZE(empty),
		.ir_type = IR_TYPE_UNKNOWN,	/* Legacy IR type */
		.name    = RC_MAP_EMPTY,
	}
};

int ir_rcmap_init(void)
{
	return ir_register_map(&empty_map);
}

void ir_rcmap_cleanup(void)
{
	ir_unregister_map(&empty_map);
}
