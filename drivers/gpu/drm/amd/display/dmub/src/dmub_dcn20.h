/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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

#ifndef _DMUB_DCN20_H_
#define _DMUB_DCN20_H_

#include "../inc/dmub_types.h"

struct dmub_srv;

/* Hardware functions. */

void dmub_dcn20_init(struct dmub_srv *dmub);

void dmub_dcn20_reset(struct dmub_srv *dmub);

void dmub_dcn20_reset_release(struct dmub_srv *dmub);

void dmub_dcn20_backdoor_load(struct dmub_srv *dmub,
			      const struct dmub_window *cw0,
			      const struct dmub_window *cw1);

void dmub_dcn20_setup_windows(struct dmub_srv *dmub,
			      const struct dmub_window *cw2,
			      const struct dmub_window *cw3,
			      const struct dmub_window *cw4,
				  const struct dmub_window *cw5);

void dmub_dcn20_setup_mailbox(struct dmub_srv *dmub,
			      const struct dmub_region *inbox1);

uint32_t dmub_dcn20_get_inbox1_rptr(struct dmub_srv *dmub);

void dmub_dcn20_set_inbox1_wptr(struct dmub_srv *dmub, uint32_t wptr_offset);

bool dmub_dcn20_is_supported(struct dmub_srv *dmub);

bool dmub_dcn20_is_phy_init(struct dmub_srv *dmub);

#endif /* _DMUB_DCN20_H_ */
