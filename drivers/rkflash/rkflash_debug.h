/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#ifndef _RKFLASH_DEBUG_H
#define _RKFLASH_DEBUG_H

#include <linux/kernel.h>

/*
 * Debug control center
 * 1. Set Printing-adding-macro to 1 to allow print code being compiled in.
 * 2. Set variable 'rkflash_debug' to control debug print to enable print.
 */

/*
 * Printing-adding
 */
#define	PRINT_SWI_INFO		1
#define	PRINT_SWI_ERROR		1
#define PRINT_SWI_HEX		1

#define	PRINT_SWI_CON_IO	1
#define	PRINT_SWI_BLK_IO	1

/*
 * Print switch, set var rkflash_debug corresponding bit to 1 if needed.
 * I - info
 * IO - IO request about
 */
#define	PRINT_BIT_CON_IO	BIT(0)
#define	PRINT_BIT_BLK_IO	BIT(4)

__printf(1, 2) int rkflash_print_info(const char *fmt, ...);
__printf(1, 2) int rkflash_print_error(const char *fmt, ...);
void rkflash_print_hex(const char *s, const void *buf, int w, size_t len);

__printf(1, 2) int rkflash_print_dio(const char *fmt, ...);
__printf(1, 2) int rkflash_print_bio(const char *fmt, ...);

#endif

