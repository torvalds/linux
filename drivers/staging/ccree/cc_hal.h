/*
 * Copyright (C) 2012-2017 ARM Limited or its affiliates.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/* pseudo cc_hal.h for cc7x_perf_test_driver (to be able to include code from CC drivers) */

#ifndef __CC_HAL_H__
#define __CC_HAL_H__

#include <linux/io.h>

#define READ_REGISTER(_addr) ioread32((_addr))
#define WRITE_REGISTER(_addr, _data)  iowrite32((_data), (_addr))

#define CC_HAL_WRITE_REGISTER(offset, val) WRITE_REGISTER(cc_base + offset, val)
#define CC_HAL_READ_REGISTER(offset) READ_REGISTER(cc_base + offset)

#endif
