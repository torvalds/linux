/* SPDX-License-Identifier: GPL-2.0 */
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

#define CHAR_BIT (8)

#include <linux/types.h>
#include <linux/limits.h>
#include <linux/errno.h>
#define HOST_ADDRESS(x) (unsigned long)(x)

#endif /* __TYPE_SUPPORT_H_INCLUDED__ */
