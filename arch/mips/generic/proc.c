/*
 * Copyright (C) 2016 Imagination Technologies
 * Author: Paul Burton <paul.burton@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/of.h>

#include <asm/bootinfo.h>

const char *get_system_type(void)
{
	const char *str;
	int err;

	err = of_property_read_string(of_root, "model", &str);
	if (!err)
		return str;

	err = of_property_read_string_index(of_root, "compatible", 0, &str);
	if (!err)
		return str;

	return "Unknown";
}
