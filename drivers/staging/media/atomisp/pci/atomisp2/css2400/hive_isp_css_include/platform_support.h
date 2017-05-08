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

#include "storage_class.h"
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/string.h>

/* For definition of hrt_sleep() */
#include "hive_isp_css_custom_host_hrt.h"

#define UINT16_MAX USHRT_MAX
#define UINT32_MAX UINT_MAX
#define UCHAR_MAX  (255)

#define CSS_ALIGN(d, a) d __attribute__((aligned(a)))

/*
 * Put here everything __KERNEL__ specific not covered in
 * "assert_support.h", "math_support.h", etc
 */

#endif /* __PLATFORM_SUPPORT_H_INCLUDED__ */
