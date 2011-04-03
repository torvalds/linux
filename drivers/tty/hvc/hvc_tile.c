/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 * Tilera TILE Processor hypervisor console
 */

#include <linux/console.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/types.h>

#include <hv/hypervisor.h>

#include "hvc_console.h"

static int hvc_tile_put_chars(uint32_t vt, const char *buf, int count)
{
	return hv_console_write((HV_VirtAddr)buf, count);
}

static int hvc_tile_get_chars(uint32_t vt, char *buf, int count)
{
	int i, c;

	for (i = 0; i < count; ++i) {
		c = hv_console_read_if_ready();
		if (c < 0)
			break;
		buf[i] = c;
	}

	return i;
}

static const struct hv_ops hvc_tile_get_put_ops = {
	.get_chars = hvc_tile_get_chars,
	.put_chars = hvc_tile_put_chars,
};

static int __init hvc_tile_console_init(void)
{
	extern void disable_early_printk(void);
	hvc_instantiate(0, 0, &hvc_tile_get_put_ops);
	add_preferred_console("hvc", 0, NULL);
	disable_early_printk();
	return 0;
}
console_initcall(hvc_tile_console_init);

static int __init hvc_tile_init(void)
{
	struct hvc_struct *s;
	s = hvc_alloc(0, 0, &hvc_tile_get_put_ops, 128);
	return IS_ERR(s) ? PTR_ERR(s) : 0;
}
device_initcall(hvc_tile_init);
