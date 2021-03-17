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

#ifndef __PLATFORM_SUPPORT_H_INCLUDED__
#define __PLATFORM_SUPPORT_H_INCLUDED__

/**
* @file
* Platform specific includes and functionality.
*/

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/string.h>

#define UINT16_MAX USHRT_MAX
#define UINT32_MAX UINT_MAX
#define UCHAR_MAX  (255)

#define CSS_ALIGN(d, a) d __attribute__((aligned(a)))

#endif /* __PLATFORM_SUPPORT_H_INCLUDED__ */
