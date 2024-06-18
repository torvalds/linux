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

#ifndef __DC_LINK_DP_IRQ_HANDLER_H__
#define __DC_LINK_DP_IRQ_HANDLER_H__

#include "link.h"
bool dp_parse_link_loss_status(
	struct dc_link *link,
	union hpd_irq_data *hpd_irq_dpcd_data);
bool dp_should_allow_hpd_rx_irq(const struct dc_link *link);
void dp_handle_link_loss(struct dc_link *link);
enum dc_status dp_read_hpd_rx_irq_data(
	struct dc_link *link,
	union hpd_irq_data *irq_data);
bool dp_handle_hpd_rx_irq(struct dc_link *link,
		union hpd_irq_data *out_hpd_irq_dpcd_data, bool *out_link_loss,
		bool defer_handling, bool *has_left_work);
#endif /* __DC_LINK_DP_IRQ_HANDLER_H__ */
