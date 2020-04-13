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

/* DCN20 register definitions. */

#define DMUB_COMMON_REGS() \
	DMUB_SR(DMCUB_CNTL) \
	DMUB_SR(DMCUB_MEM_CNTL) \
	DMUB_SR(DMCUB_SEC_CNTL) \
	DMUB_SR(DMCUB_INBOX1_BASE_ADDRESS) \
	DMUB_SR(DMCUB_INBOX1_SIZE) \
	DMUB_SR(DMCUB_INBOX1_RPTR) \
	DMUB_SR(DMCUB_INBOX1_WPTR) \
	DMUB_SR(DMCUB_REGION3_CW0_OFFSET) \
	DMUB_SR(DMCUB_REGION3_CW1_OFFSET) \
	DMUB_SR(DMCUB_REGION3_CW2_OFFSET) \
	DMUB_SR(DMCUB_REGION3_CW3_OFFSET) \
	DMUB_SR(DMCUB_REGION3_CW4_OFFSET) \
	DMUB_SR(DMCUB_REGION3_CW5_OFFSET) \
	DMUB_SR(DMCUB_REGION3_CW6_OFFSET) \
	DMUB_SR(DMCUB_REGION3_CW7_OFFSET) \
	DMUB_SR(DMCUB_REGION3_CW0_OFFSET_HIGH) \
	DMUB_SR(DMCUB_REGION3_CW1_OFFSET_HIGH) \
	DMUB_SR(DMCUB_REGION3_CW2_OFFSET_HIGH) \
	DMUB_SR(DMCUB_REGION3_CW3_OFFSET_HIGH) \
	DMUB_SR(DMCUB_REGION3_CW4_OFFSET_HIGH) \
	DMUB_SR(DMCUB_REGION3_CW5_OFFSET_HIGH) \
	DMUB_SR(DMCUB_REGION3_CW6_OFFSET_HIGH) \
	DMUB_SR(DMCUB_REGION3_CW7_OFFSET_HIGH) \
	DMUB_SR(DMCUB_REGION3_CW0_BASE_ADDRESS) \
	DMUB_SR(DMCUB_REGION3_CW1_BASE_ADDRESS) \
	DMUB_SR(DMCUB_REGION3_CW2_BASE_ADDRESS) \
	DMUB_SR(DMCUB_REGION3_CW3_BASE_ADDRESS) \
	DMUB_SR(DMCUB_REGION3_CW4_BASE_ADDRESS) \
	DMUB_SR(DMCUB_REGION3_CW5_BASE_ADDRESS) \
	DMUB_SR(DMCUB_REGION3_CW6_BASE_ADDRESS) \
	DMUB_SR(DMCUB_REGION3_CW7_BASE_ADDRESS) \
	DMUB_SR(DMCUB_REGION3_CW0_TOP_ADDRESS) \
	DMUB_SR(DMCUB_REGION3_CW1_TOP_ADDRESS) \
	DMUB_SR(DMCUB_REGION3_CW2_TOP_ADDRESS) \
	DMUB_SR(DMCUB_REGION3_CW3_TOP_ADDRESS) \
	DMUB_SR(DMCUB_REGION3_CW4_TOP_ADDRESS) \
	DMUB_SR(DMCUB_REGION3_CW5_TOP_ADDRESS) \
	DMUB_SR(DMCUB_REGION3_CW6_TOP_ADDRESS) \
	DMUB_SR(DMCUB_REGION3_CW7_TOP_ADDRESS) \
	DMUB_SR(DMCUB_REGION4_OFFSET) \
	DMUB_SR(DMCUB_REGION4_OFFSET_HIGH) \
	DMUB_SR(DMCUB_REGION4_TOP_ADDRESS) \
	DMUB_SR(DMCUB_SCRATCH0) \
	DMUB_SR(DMCUB_SCRATCH1) \
	DMUB_SR(DMCUB_SCRATCH2) \
	DMUB_SR(DMCUB_SCRATCH3) \
	DMUB_SR(DMCUB_SCRATCH4) \
	DMUB_SR(DMCUB_SCRATCH5) \
	DMUB_SR(DMCUB_SCRATCH6) \
	DMUB_SR(DMCUB_SCRATCH7) \
	DMUB_SR(DMCUB_SCRATCH8) \
	DMUB_SR(DMCUB_SCRATCH9) \
	DMUB_SR(DMCUB_SCRATCH10) \
	DMUB_SR(DMCUB_SCRATCH11) \
	DMUB_SR(DMCUB_SCRATCH12) \
	DMUB_SR(DMCUB_SCRATCH13) \
	DMUB_SR(DMCUB_SCRATCH14) \
	DMUB_SR(DMCUB_SCRATCH15) \
	DMUB_SR(DMCUB_GPINT_DATAIN1) \
	DMUB_SR(CC_DC_PIPE_DIS) \
	DMUB_SR(MMHUBBUB_SOFT_RESET) \
	DMUB_SR(DCN_VM_FB_LOCATION_BASE) \
	DMUB_SR(DCN_VM_FB_OFFSET)

#define DMUB_COMMON_FIELDS() \
	DMUB_SF(DMCUB_CNTL, DMCUB_ENABLE) \
	DMUB_SF(DMCUB_CNTL, DMCUB_SOFT_RESET) \
	DMUB_SF(DMCUB_CNTL, DMCUB_TRACEPORT_EN) \
	DMUB_SF(DMCUB_MEM_CNTL, DMCUB_MEM_READ_SPACE) \
	DMUB_SF(DMCUB_MEM_CNTL, DMCUB_MEM_WRITE_SPACE) \
	DMUB_SF(DMCUB_SEC_CNTL, DMCUB_SEC_RESET) \
	DMUB_SF(DMCUB_SEC_CNTL, DMCUB_MEM_UNIT_ID) \
	DMUB_SF(DMCUB_REGION3_CW0_TOP_ADDRESS, DMCUB_REGION3_CW0_TOP_ADDRESS) \
	DMUB_SF(DMCUB_REGION3_CW0_TOP_ADDRESS, DMCUB_REGION3_CW0_ENABLE) \
	DMUB_SF(DMCUB_REGION3_CW1_TOP_ADDRESS, DMCUB_REGION3_CW1_TOP_ADDRESS) \
	DMUB_SF(DMCUB_REGION3_CW1_TOP_ADDRESS, DMCUB_REGION3_CW1_ENABLE) \
	DMUB_SF(DMCUB_REGION3_CW2_TOP_ADDRESS, DMCUB_REGION3_CW2_TOP_ADDRESS) \
	DMUB_SF(DMCUB_REGION3_CW2_TOP_ADDRESS, DMCUB_REGION3_CW2_ENABLE) \
	DMUB_SF(DMCUB_REGION3_CW3_TOP_ADDRESS, DMCUB_REGION3_CW3_TOP_ADDRESS) \
	DMUB_SF(DMCUB_REGION3_CW3_TOP_ADDRESS, DMCUB_REGION3_CW3_ENABLE) \
	DMUB_SF(DMCUB_REGION3_CW4_TOP_ADDRESS, DMCUB_REGION3_CW4_TOP_ADDRESS) \
	DMUB_SF(DMCUB_REGION3_CW4_TOP_ADDRESS, DMCUB_REGION3_CW4_ENABLE) \
	DMUB_SF(DMCUB_REGION3_CW5_TOP_ADDRESS, DMCUB_REGION3_CW5_TOP_ADDRESS) \
	DMUB_SF(DMCUB_REGION3_CW5_TOP_ADDRESS, DMCUB_REGION3_CW5_ENABLE) \
	DMUB_SF(DMCUB_REGION3_CW6_TOP_ADDRESS, DMCUB_REGION3_CW6_TOP_ADDRESS) \
	DMUB_SF(DMCUB_REGION3_CW6_TOP_ADDRESS, DMCUB_REGION3_CW6_ENABLE) \
	DMUB_SF(DMCUB_REGION3_CW7_TOP_ADDRESS, DMCUB_REGION3_CW7_TOP_ADDRESS) \
	DMUB_SF(DMCUB_REGION3_CW7_TOP_ADDRESS, DMCUB_REGION3_CW7_ENABLE) \
	DMUB_SF(DMCUB_REGION4_TOP_ADDRESS, DMCUB_REGION4_TOP_ADDRESS) \
	DMUB_SF(DMCUB_REGION4_TOP_ADDRESS, DMCUB_REGION4_ENABLE) \
	DMUB_SF(CC_DC_PIPE_DIS, DC_DMCUB_ENABLE) \
	DMUB_SF(MMHUBBUB_SOFT_RESET, DMUIF_SOFT_RESET) \
	DMUB_SF(DCN_VM_FB_LOCATION_BASE, FB_BASE) \
	DMUB_SF(DCN_VM_FB_OFFSET, FB_OFFSET)

struct dmub_srv_common_reg_offset {
#define DMUB_SR(reg) uint32_t reg;
	DMUB_COMMON_REGS()
#undef DMUB_SR
};

struct dmub_srv_common_reg_shift {
#define DMUB_SF(reg, field) uint8_t reg##__##field;
	DMUB_COMMON_FIELDS()
#undef DMUB_SF
};

struct dmub_srv_common_reg_mask {
#define DMUB_SF(reg, field) uint32_t reg##__##field;
	DMUB_COMMON_FIELDS()
#undef DMUB_SF
};

struct dmub_srv_common_regs {
	const struct dmub_srv_common_reg_offset offset;
	const struct dmub_srv_common_reg_mask mask;
	const struct dmub_srv_common_reg_shift shift;
};

extern const struct dmub_srv_common_regs dmub_srv_dcn20_regs;

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
			      const struct dmub_window *cw5,
			      const struct dmub_window *cw6);

void dmub_dcn20_setup_mailbox(struct dmub_srv *dmub,
			      const struct dmub_region *inbox1);

uint32_t dmub_dcn20_get_inbox1_rptr(struct dmub_srv *dmub);

void dmub_dcn20_set_inbox1_wptr(struct dmub_srv *dmub, uint32_t wptr_offset);

bool dmub_dcn20_is_hw_init(struct dmub_srv *dmub);

bool dmub_dcn20_is_supported(struct dmub_srv *dmub);

void dmub_dcn20_set_gpint(struct dmub_srv *dmub,
			  union dmub_gpint_data_register reg);

bool dmub_dcn20_is_gpint_acked(struct dmub_srv *dmub,
			       union dmub_gpint_data_register reg);

uint32_t dmub_dcn20_get_gpint_response(struct dmub_srv *dmub);

#endif /* _DMUB_DCN20_H_ */
