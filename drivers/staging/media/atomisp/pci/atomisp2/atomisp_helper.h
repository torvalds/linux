/*
 * Support for Medifield PNW Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2010 Intel Corporation. All Rights Reserved.
 *
 * Copyright (c) 2010 Silicon Hive www.siliconhive.com.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 */
#ifndef _atomisp_helper_h_
#define _atomisp_helper_h_
extern void __iomem *atomisp_io_base;

static inline void __iomem *atomisp_get_io_virt_addr(unsigned int address)
{
	void __iomem *ret = atomisp_io_base + (address & 0x003FFFFF);
	return ret;
}
#endif

