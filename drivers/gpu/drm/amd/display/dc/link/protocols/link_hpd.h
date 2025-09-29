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


#ifndef __DC_LINK_HPD_H__
#define __DC_LINK_HPD_H__
#include "link_service.h"

enum hpd_source_id get_hpd_line(struct dc_link *link);
/*
 *  Function: program_hpd_filter
 *
 *  @brief
 *     Programs HPD filter on associated HPD line to default values.
 *
 *  @return
 *     true on success, false otherwise
 */
bool program_hpd_filter(const struct dc_link *link);
/* Query hot plug status of USB4 DP tunnel.
 * Returns true if HPD high.
 */
bool dpia_query_hpd_status(struct dc_link *link);
bool query_hpd_status(struct dc_link *link, uint32_t *is_hpd_high);
bool link_get_hpd_state(struct dc_link *link);
struct gpio *link_get_hpd_gpio(struct dc_bios *dcb,
		struct graphics_object_id link_id,
		struct gpio_service *gpio_service);
void link_enable_hpd(const struct dc_link *link);
void link_disable_hpd(const struct dc_link *link);
void link_enable_hpd_filter(struct dc_link *link, bool enable);
#endif /* __DC_LINK_HPD_H__ */
