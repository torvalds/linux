// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2016 Imagination Technologies
 * Author: Paul Burton <paul.burton@mips.com>
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
