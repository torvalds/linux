// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#include <linux/kernel.h>

#include "rkflash_debug.h"
#include "sfc_nor.h"
#include "typedef.h"

void rknand_print_hex(char *s, void *buf, int width, int len)
{
	print_hex_dump(KERN_WARNING, s, DUMP_PREFIX_OFFSET,
		       16, width, buf, len * width, 0);
}

