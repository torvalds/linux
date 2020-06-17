/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2010 - 2015, Intel Corporation.
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

#ifndef __BAYER_IO_HOST_H
#define __BAYER_IO_HOST_H

#include "ia_css_bayer_io_param.h"
#include "ia_css_bayer_io_types.h"
#include "ia_css_binary.h"
#include "sh_css_internal.h"

void
ia_css_bayer_io_config(
    const struct ia_css_binary     *binary,
    const struct sh_css_binary_args *args);

#endif /*__BAYER_IO_HOST_H */
