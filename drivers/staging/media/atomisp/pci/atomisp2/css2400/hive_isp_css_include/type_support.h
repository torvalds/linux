/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __TYPE_SUPPORT_H_INCLUDED__
#define __TYPE_SUPPORT_H_INCLUDED__

/**
* @file
* Platform specific types.
*
* Per the DLI spec, types are in "type_support.h" and
* "platform_support.h" is for unclassified/to be refactored
* platform specific definitions.
*/

#define IA_CSS_UINT8_T_BITS						8
#define IA_CSS_UINT16_T_BITS					16
#define IA_CSS_UINT32_T_BITS					32
#define IA_CSS_INT32_T_BITS						32
#define IA_CSS_UINT64_T_BITS					64

#if defined(_MSC_VER)
#include <stdint.h>
/* For ATE compilation define the bool */
#if defined(_ATE_)
#define bool int
#define true 1
#define false 0
#else
#include <stdbool.h>
#endif
#include <stddef.h>
#include <limits.h>
#include <errno.h>
#if defined(_M_X64)
#define HOST_ADDRESS(x) (unsigned long long)(x)
#else
#define HOST_ADDRESS(x) (unsigned long)(x)
#endif

#elif defined(__HIVECC)
#ifndef PIPE_GENERATION
#include <hive/cell_support.h> /* for HAVE_STDINT */
#endif
#define __INDIRECT_STDINT_INCLUDE
#include <stdint/stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <limits.h>
#define HOST_ADDRESS(x) (unsigned long)(x)

#elif defined(__KERNEL__)

#define CHAR_BIT (8)

#include <linux/types.h>
#include <linux/limits.h>
#include <linux/errno.h>
#define HOST_ADDRESS(x) (unsigned long)(x)

#elif defined(__GNUC__)
#ifndef __STDC_LIMIT_MACROS
#define __STDC_LIMIT_MACROS 1
#endif
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <limits.h>
#include <errno.h>
#define HOST_ADDRESS(x) (unsigned long)(x)

#else /* default is for the FIST environment */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <limits.h>
#include <errno.h>
#define HOST_ADDRESS(x) (unsigned long)(x)

#endif

#endif /* __TYPE_SUPPORT_H_INCLUDED__ */
