/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#ifndef _RKFLASH_DEBUG_H
#define _RKFLASH_DEBUG_H

/*
 * Print switch, set to 1 if needed
 * I - info
 * E - error
 * HEX - multiline print
 */

#define	PRINT_SWI_SFC_I		0
#define	PRINT_SWI_SFC_E		1
#define PRINT_SWI_SFC_HEX	1

/*
 * Test switch
 */

#if (PRINT_SWI_SFC_I)
#define PRINT_SFC_I(...) pr_info(__VA_ARGS__)
#else
#define PRINT_SFC_I(...)
#endif

#if (PRINT_SWI_SFC_E)
#define PRINT_SFC_E(...) pr_err(__VA_ARGS__)
#else
#define PRINT_SFC_E(...)
#endif

#if (PRINT_SWI_SFC_HEX)
#define PRINT_SFC_HEX(s, buf, width, len)\
		rkflash_print_hex(s, buf, width, len)
#else
#define PRINT_SFC_HEX(s, buf, width, len)
#endif

void rkflash_print_hex(char *s, void *buf, u32 width, u32 len);

#endif
