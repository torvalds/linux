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

#ifndef __VAMEM_GLOBAL_H_INCLUDED__
#define __VAMEM_GLOBAL_H_INCLUDED__

#include <type_support.h>

#define IS_VAMEM_VERSION_2

/* (log) stepsize of linear interpolation */
#define VAMEM_INTERP_STEP_LOG2	4
#define VAMEM_INTERP_STEP		BIT(VAMEM_INTERP_STEP_LOG2)
/* (physical) size of the tables */
#define VAMEM_TABLE_UNIT_SIZE	((1 << (ISP_VAMEM_ADDRESS_BITS - VAMEM_INTERP_STEP_LOG2)) + 1)
/* (logical) size of the tables */
#define VAMEM_TABLE_UNIT_STEP	((VAMEM_TABLE_UNIT_SIZE - 1) << 1)
/* Number of tables */
#define VAMEM_TABLE_UNIT_COUNT	(ISP_VAMEM_DEPTH / VAMEM_TABLE_UNIT_STEP)

typedef u16				vamem_data_t;

#endif /* __VAMEM_GLOBAL_H_INCLUDED__ */
