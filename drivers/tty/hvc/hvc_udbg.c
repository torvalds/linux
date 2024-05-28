// SPDX-License-Identifier: GPL-2.0+
/*
 * udbg interface to hvc_console.c
 *
 * (C) Copyright David Gibson, IBM Corporation 2008.
 */

#include <linux/console.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/types.h>
#include <linux/irq.h>

#include <asm/udbg.h>

#include "hvc_console.h"

static struct hvc_struct *hvc_udbg_dev;

static ssize_t hvc_udbg_put(uint32_t vtermno, const u8 *buf, size_t count)
{
	size_t i;

	for (i = 0; i < count && udbg_putc; i++)
		udbg_putc(buf[i]);

	return i;
}

static ssize_t hvc_udbg_get(uint32_t vtermno, u8 *buf, size_t count)
{
	size_t i;
	int c;

	if (!udbg_getc_poll)
		return 0;

	for (i = 0; i < count; i++) {
		if ((c = udbg_getc_poll()) == -1)
			break;
		buf[i] = c;
	}

	return i;
}

static const struct hv_ops hvc_udbg_ops = {
	.get_chars = hvc_udbg_get,
	.put_chars = hvc_udbg_put,
};

static int __init hvc_udbg_init(void)
{
	struct hvc_struct *hp;

	if (!udbg_putc)
		return -ENODEV;

	BUG_ON(hvc_udbg_dev);

	hp = hvc_alloc(0, 0, &hvc_udbg_ops, 16);
	if (IS_ERR(hp))
		return PTR_ERR(hp);

	hvc_udbg_dev = hp;

	return 0;
}
device_initcall(hvc_udbg_init);

static int __init hvc_udbg_console_init(void)
{
	if (!udbg_putc)
		return -ENODEV;

	hvc_instantiate(0, 0, &hvc_udbg_ops);
	add_preferred_console("hvc", 0, NULL);

	return 0;
}
console_initcall(hvc_udbg_console_init);
