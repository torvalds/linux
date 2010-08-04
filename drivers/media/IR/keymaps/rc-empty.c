/* empty.h - Keytable for empty Remote Controller
 *
 * keymap imported from ir-keymaps.c
 *
 * Copyright (c) 2010 by Mauro Carvalho Chehab <mchehab@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <media/rc-map.h>

/* empty keytable, can be used as placeholder for not-yet created keytables */

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

static int __init init_rc_map_empty(void)
{
	return ir_register_map(&empty_map);
}

static void __exit exit_rc_map_empty(void)
{
	ir_unregister_map(&empty_map);
}

module_init(init_rc_map_empty)
module_exit(exit_rc_map_empty)

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@redhat.com>");
