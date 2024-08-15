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

#ifndef __IA_CSS_STREAM_FORMAT_H
#define __IA_CSS_STREAM_FORMAT_H

/* @file
 * This file contains formats usable for ISP streaming input
 */

#include <type_support.h> /* bool */
#include "../../../include/linux/atomisp_platform.h"

unsigned int ia_css_util_input_format_bpp(
    enum atomisp_input_format format,
    bool two_ppc);

#endif /* __ATOMISP_INPUT_FORMAT_H */
