/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2008 David Gibson, IBM Corporation
 * Copyright (C) 2012 Regents of the University of California
 * Copyright (C) 2017 SiFive
 */

#include <linux/console.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/types.h>

#include <asm/sbi.h>

#include "hvc_console.h"

static ssize_t hvc_sbi_tty_put(uint32_t vtermno, const u8 *buf, size_t count)
{
	size_t i;

	for (i = 0; i < count; i++)
		sbi_console_putchar(buf[i]);

	return i;
}

static ssize_t hvc_sbi_tty_get(uint32_t vtermno, u8 *buf, size_t count)
{
	size_t i;
	int c;

	for (i = 0; i < count; i++) {
		c = sbi_console_getchar();
		if (c < 0)
			break;
		buf[i] = c;
	}

	return i;
}

static const struct hv_ops hvc_sbi_v01_ops = {
	.get_chars = hvc_sbi_tty_get,
	.put_chars = hvc_sbi_tty_put,
};

static ssize_t hvc_sbi_dbcn_tty_put(uint32_t vtermno, const u8 *buf, size_t count)
{
	return sbi_debug_console_write(buf, count);
}

static ssize_t hvc_sbi_dbcn_tty_get(uint32_t vtermno, u8 *buf, size_t count)
{
	return sbi_debug_console_read(buf, count);
}

static const struct hv_ops hvc_sbi_dbcn_ops = {
	.put_chars = hvc_sbi_dbcn_tty_put,
	.get_chars = hvc_sbi_dbcn_tty_get,
};

static int __init hvc_sbi_init(void)
{
	int err;

	if (sbi_debug_console_available) {
		err = PTR_ERR_OR_ZERO(hvc_alloc(0, 0, &hvc_sbi_dbcn_ops, 256));
		if (err)
			return err;
		hvc_instantiate(0, 0, &hvc_sbi_dbcn_ops);
	} else if (IS_ENABLED(CONFIG_RISCV_SBI_V01)) {
		err = PTR_ERR_OR_ZERO(hvc_alloc(0, 0, &hvc_sbi_v01_ops, 256));
		if (err)
			return err;
		hvc_instantiate(0, 0, &hvc_sbi_v01_ops);
	} else {
		return -ENODEV;
	}

	return 0;
}
device_initcall(hvc_sbi_init);
