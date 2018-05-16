// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#include <linux/kernel.h>

#include "rkflash_debug.h"
#include "sfc_nor.h"

void rkflash_print_hex(char *s, void *buf, u32 width, u32 len)
{
	print_hex_dump(KERN_WARNING, s, DUMP_PREFIX_OFFSET,
		       16, width, buf, len * width, 0);
}

