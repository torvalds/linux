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
extern unsigned int rkflash_debug;

#define	PRINT_SWI_SFC_I		1
#define	PRINT_SWI_SFC_E		1
#define PRINT_SWI_SFC_HEX	1

#define	PRINT_SWI_NANDC_I	1
#define	PRINT_SWI_NANDC_E	1
#define PRINT_SWI_NANDC_HEX	1

/*
 * Print switch, set var rkflash_debug corresponding bit to
 * 1 if needed.
 * I - info
 * E - error
 * HEX - multiline print
 */
#define	PRINT_BIT_SFC_I		BIT(0)
#define	PRINT_BIT_SFC_E		BIT(1)
#define PRINT_BIT_SFC_HEX	BIT(2)
#define	PRINT_BIT_SFC\
		(PRINT_BIT_SFC_I | PRINT_BIT_SFC_E | PRINT_BIT_SFC_HEX)

#define	PRINT_BIT_NANDC_I	BIT(4)
#define	PRINT_BIT_NANDC_E	BIT(5)
#define PRINT_BIT_NANDC_HEX	BIT(5)
#define	PRINT_BIT_NANDC\
		(PRINT_BIT_NANDC_I | PRINT_BIT_NANDC_E | PRINT_BIT_NANDC_HEX)

#if (PRINT_SWI_SFC_I)
#define PRINT_SFC_I(...) {if (rkflash_debug & PRINT_BIT_SFC_I)\
				pr_info(__VA_ARGS__); }
#else
#define PRINT_SFC_I(...)
#endif

#if (PRINT_SWI_SFC_E)
#define PRINT_SFC_E(...) {if (rkflash_debug & PRINT_BIT_SFC_E)\
				pr_info(__VA_ARGS__); }
#else
#define PRINT_SFC_E(...)
#endif

#if (PRINT_SWI_SFC_HEX)
#define PRINT_SFC_HEX(s, buf, width, len)\
		rkflash_print_hex(s, buf, width, len)
#else
#define PRINT_SFC_HEX(s, buf, width, len)
#endif

#if (PRINT_SWI_NANDC_I)
#define PRINT_NANDC_I(...) {if (rkflash_debug & PRINT_BIT_NANDC_I)\
				pr_info(__VA_ARGS__); }
#else
#define PRINT_NANDC_I(...)
#endif

#if (PRINT_SWI_NANDC_E)
#define PRINT_NANDC_E(...) {if (rkflash_debug & PRINT_BIT_NANDC_I)\
				pr_info(__VA_ARGS__); }
#else
#define PRINT_NANDC_E(...)
#endif

#if (PRINT_SWI_NANDC_HEX)
#define PRINT_NANDC_HEX(s, buf, width, len)\
		(rkflash_print_hex(s, buf, width, len))
#else
#define PRINT_NANDC_HEX(s, buf, width, len)
#endif

#define rkflash_print_hex(s, buf, w, len)\
		print_hex_dump(KERN_WARNING, s, DUMP_PREFIX_OFFSET,\
			       4, w, buf, (len) * w, 0)

#endif

