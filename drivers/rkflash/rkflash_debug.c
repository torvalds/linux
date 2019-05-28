// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/slab.h>

#include "rkflash_debug.h"

static unsigned int rkflash_debug;

__printf(1, 2) int rkflash_print_dio(const char *fmt, ...)
{
	int nret = 0;
#if PRINT_SWI_CON_IO
	if (rkflash_debug & PRINT_BIT_CON_IO)  {
		va_list args;

		if (!fmt)
			return nret;

		va_start(args, fmt);
		nret = vprintk(fmt, args);
		va_end(args);
	}
#endif
	return nret;
}

__printf(1, 2) int rkflash_print_bio(const char *fmt, ...)
{
	int nret = 0;
#if PRINT_SWI_BLK_IO
	if (rkflash_debug & PRINT_BIT_BLK_IO)  {
		va_list args;

		if (!fmt)
			return nret;

		va_start(args, fmt);
		nret = vprintk(fmt, args);
		va_end(args);
	}
#endif
	return nret;
}

__printf(1, 2) int rkflash_print_info(const char *fmt, ...)
{
	int nret = 0;
#if PRINT_SWI_INFO
	va_list args;

	if (!fmt)
		return nret;

	va_start(args, fmt);
	nret = vprintk(fmt, args);
	va_end(args);
#endif
	return nret;
}

__printf(1, 2) int rkflash_print_error(const char *fmt, ...)
{
	int nret = 0;
#if PRINT_SWI_ERROR
	va_list args;

	if (!fmt)
		return nret;

	va_start(args, fmt);
	nret = vprintk(fmt, args);
	va_end(args);
#endif
	return nret;
}

void rkflash_print_hex(const char *s, const void *buf, int w, size_t len)
{
#if PRINT_SWI_ERROR
	return print_hex_dump(KERN_WARNING, s, DUMP_PREFIX_OFFSET, 4, w,
			      buf, (len) * w, 0);
#endif
}

static int set_val(const char *val, const struct kernel_param *kp)
{
	char *tmp = kzalloc(8, GFP_KERNEL);

	strncpy(tmp, val, 8);
	if (!strncmp(tmp, "0", 1)) {
		rkflash_debug = 0;
	} else if (!strncmp(tmp, "blk_io", 6)) {
		rkflash_debug |= PRINT_BIT_BLK_IO;
	} else if (!strncmp(tmp, "con_io", 6)) {
		rkflash_debug |= PRINT_BIT_CON_IO;
	} else {
		pr_info("input error, support 0, blk_io, con_io\n");
		rkflash_debug = 0;
	}
	kfree(tmp);

	return 0;
}

static struct kernel_param_ops rkflash_debug_param_ops = {
	.set = set_val,
	.get = param_get_uint,
};

module_param_cb(rkflash_debug, &rkflash_debug_param_ops, &rkflash_debug, 0644);
MODULE_PARM_DESC(rkflash_debug, "config rkflash_debug module");
