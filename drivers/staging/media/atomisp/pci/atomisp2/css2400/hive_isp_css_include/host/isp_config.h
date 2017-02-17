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

#ifndef __ISP_CONFIG_H_INCLUDED__
#define __ISP_CONFIG_H_INCLUDED__

#if defined(ISP2400) || defined(ISP2401)
#include "isp2400_config.h"
#else
#error "Please define a core {ISP2400, ISP2401}"
#endif

#endif /* __ISP_CONFIG_H_INCLUDED__ */
