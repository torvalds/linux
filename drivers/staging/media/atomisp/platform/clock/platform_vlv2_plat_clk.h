/*
 * platform_vlv2_plat_clk.h: platform clock driver library header file
 * Copyright (C) 2013 Intel Corporation
 *
 * Author: Asutosh Pathak <asutosh.pathak@intel.com>
 * Author: Chandra Sekhar Anagani <chandra.sekhar.anagani@intel.com>
 * Author: Sergio Aguirre <sergio.a.aguirre.rodriguez@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 */
#ifndef _PLATFORM_VLV2_PLAT_CLK_H_
#define _PLATFORM_VLV2_PLAT_CLK_H_

#include <linux/sfi.h>
#include <asm/intel-mid.h>

extern void __init *vlv2_plat_clk_device_platform_data(
				void *info) __attribute__((weak));
#endif
