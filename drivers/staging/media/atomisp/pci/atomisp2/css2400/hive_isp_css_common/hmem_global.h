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

#ifndef __HMEM_GLOBAL_H_INCLUDED__
#define __HMEM_GLOBAL_H_INCLUDED__

#include <type_support.h>

#define IS_HMEM_VERSION_1

#include "isp.h"

/*
#define ISP_HIST_ADDRESS_BITS                  12
#define ISP_HIST_ALIGNMENT                     4
#define ISP_HIST_COMP_IN_PREC                  12
#define ISP_HIST_DEPTH                         1024
#define ISP_HIST_WIDTH                         24
#define ISP_HIST_COMPONENTS                    4
*/
#define ISP_HIST_ALIGNMENT_LOG2		2

#define HMEM_SIZE_LOG2		(ISP_HIST_ADDRESS_BITS-ISP_HIST_ALIGNMENT_LOG2)
#define HMEM_SIZE			ISP_HIST_DEPTH

#define HMEM_UNIT_SIZE		(HMEM_SIZE/ISP_HIST_COMPONENTS)
#define HMEM_UNIT_COUNT		ISP_HIST_COMPONENTS

#define HMEM_RANGE_LOG2		ISP_HIST_WIDTH
#define HMEM_RANGE			(1UL<<HMEM_RANGE_LOG2)

typedef uint32_t			hmem_data_t;

#endif /* __HMEM_GLOBAL_H_INCLUDED__ */
