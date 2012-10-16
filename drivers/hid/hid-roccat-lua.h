#ifndef __HID_ROCCAT_LUA_H
#define __HID_ROCCAT_LUA_H

/*
 * Copyright (c) 2012 Stefan Achatz <erazor_de@users.sourceforge.net>
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
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
