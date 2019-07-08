/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef __HID_ROCCAT_LUA_H
#define __HID_ROCCAT_LUA_H

/*
 * Copyright (c) 2012 Stefan Achatz <erazor_de@users.sourceforge.net>
 */

/*
 */

#include <linux/types.h>

enum {
	LUA_SIZE_CONTROL = 8,
};

enum lua_commands {
	LUA_COMMAND_CONTROL = 3,
};

struct lua_device {
	struct mutex lua_lock;
};

#endif
