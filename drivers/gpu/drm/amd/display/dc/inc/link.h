/*
 * Copyright 2022 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#ifndef __DC_LINK_H__
#define __DC_LINK_H__

/* FILE POLICY AND INTENDED USAGE:
 *
 * This header declares link functions exposed to dc. All functions must have
 * "link_" as prefix. For example link_run_my_function. This header is strictly
 * private in dc and should never be included in other header files. dc
 * components should include this header in their .c files in order to access
 * functions in link folder. This file should never include any header files in
 * link folder. If there is a need to expose a function declared in one of
 * header files in side link folder, you need to move the function declaration
 * into this file and prefix it with "link_".
 */
#include "core_types.h"
#include "dc_link.h"

struct gpio *link_get_hpd_gpio(struct dc_bios *dcb,
		struct graphics_object_id link_id,
		struct gpio_service *gpio_service);

#endif /* __DC_LINK_HPD_H__ */
