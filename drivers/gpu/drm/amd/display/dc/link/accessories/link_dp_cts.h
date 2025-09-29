/*
 * Copyright 2023 Advanced Micro Devices, Inc.
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
#ifndef __LINK_DP_CTS_H__
#define __LINK_DP_CTS_H__
#include "link_service.h"
void dp_handle_automated_test(struct dc_link *link);
bool dp_set_test_pattern(
		struct dc_link *link,
		enum dp_test_pattern test_pattern,
		enum dp_test_pattern_color_space test_pattern_color_space,
		const struct link_training_settings *p_link_settings,
		const unsigned char *p_custom_pattern,
		unsigned int cust_pattern_size);
void dp_set_preferred_link_settings(struct dc *dc,
		struct dc_link_settings *link_setting,
		struct dc_link *link);
void dp_set_preferred_training_settings(struct dc *dc,
		struct dc_link_settings *link_setting,
		struct dc_link_training_overrides *lt_overrides,
		struct dc_link *link,
		bool skip_immediate_retrain);
#endif /* __LINK_DP_CTS_H__ */
