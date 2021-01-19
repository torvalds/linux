/*
 * Copyright 2020 Advanced Micro Devices, Inc.
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

#include "../dmub_srv.h"
#include "dmub_reg.h"
#include "dmub_dcn20.h"
#include "dmub_dcn30.h"

#include "sienna_cichlid_ip_offset.h"
#include "dcn/dcn_3_0_0_offset.h"
#include "dcn/dcn_3_0_0_sh_mask.h"

#define BASE_INNER(seg) DCN_BASE__INST0_SEG##seg
#define CTX dmub
#define REGS dmub->regs

/* Registers. */

const struct dmub_srv_common_regs dmub_srv_dcn30_regs = {
#define DMUB_SR(reg) REG_OFFSET(reg),
	{ DMUB_COMMON_REGS() },
#undef DMUB_SR

#define DMUB_SF(reg, field) FD_MASK(reg, field),
	{ DMUB_COMMON_FIELDS() },
#undef DMUB_SF

#define DMUB_SF(reg, field) FD_SHIFT(reg, field),
	{ DMUB_COMMON_FIELDS() },
#undef DMUB_SF
};

/* Shared functions. */

static void dmub_dcn30_get_fb_base_offset(struct dmub_srv *dmub,
					  uint64_t *fb_base,
					  uint64_t *fb_offset)
{
	uint32_t tmp;

	if (dmub->fb_base || dmub->fb_offset) {
		*fb_base = dmub->fb_base;
		*fb_offset = dmub->fb_offset;
		return;
	}

	REG_GET(DCN_VM_FB_LOCATION_BASE, FB_BASE, &tmp);
	*fb_base = (uint64_t)tmp << 24;

	REG_GET(DCN_VM_FB_OFFSET, FB_OFFSET, &tmp);
	*fb_offset = (uint64_t)tmp << 24;
}

static inline void dmub_dcn30_translate_addr(const union dmub_addr *addr_in,
					     uint64_t fb_base,
					     uint64_t fb_offset,
					     union dmub_addr *addr_out)
{
	addr_out->quad_part = addr_in->quad_part - fb_base + fb_offset;
}

void dmub_dcn30_backdoor_load(struct dmub_srv *dmub,
			      const struct dmub_window *cw0,
			      const struct dmub_window *cw1)
{
	union dmub_addr offset;
	uint64_t fb_base, fb_offset;

	dmub_dcn30_get_fb_base_offset(dmub, &fb_base, &fb_offset);

	REG_UPDATE(DMCUB_SEC_CNTL, DMCUB_SEC_RESET, 1);

	/* MEM_CTNL read/write space doesn't exist. */

	dmub_dcn30_translate_addr(&cw0->offset, fb_base, fb_offset, &offset);

	REG_WRITE(DMCUB_REGION3_CW0_OFFSET, offset.u.low_part);
	REG_WRITE(DMCUB_REGION3_CW0_OFFSET_HIGH, offset.u.high_part);
	REG_WRITE(DMCUB_REGION3_CW0_BASE_ADDRESS, cw0->region.base);
	REG_SET_2(DMCUB_REGION3_CW0_TOP_ADDRESS, 0,
		  DMCUB_REGION3_CW0_TOP_ADDRESS, cw0->region.top,
		  DMCUB_REGION3_CW0_ENABLE, 1);

	dmub_dcn30_translate_addr(&cw1->offset, fb_base, fb_offset, &offset);

	REG_WRITE(DMCUB_REGION3_CW1_OFFSET, offset.u.low_part);
	REG_WRITE(DMCUB_REGION3_CW1_OFFSET_HIGH, offset.u.high_part);
	REG_WRITE(DMCUB_REGION3_CW1_BASE_ADDRESS, cw1->region.base);
	REG_SET_2(DMCUB_REGION3_CW1_TOP_ADDRESS, 0,
		  DMCUB_REGION3_CW1_TOP_ADDRESS, cw1->region.top,
		  DMCUB_REGION3_CW1_ENABLE, 1);

	REG_UPDATE_2(DMCUB_SEC_CNTL, DMCUB_SEC_RESET, 0, DMCUB_MEM_UNIT_ID,
		     0x20);
}

void dmub_dcn30_setup_windows(struct dmub_srv *dmub,
			      const struct dmub_window *cw2,
			      const struct dmub_window *cw3,
			      const struct dmub_window *cw4,
			      const struct dmub_window *cw5,
			      const struct dmub_window *cw6)
{
	union dmub_addr offset;

	/* sienna_cichlid  has hardwired virtual addressing for CW2-CW7 */

	offset = cw2->offset;

	if (cw2->region.base != cw2->region.top) {
		REG_WRITE(DMCUB_REGION3_CW2_OFFSET, offset.u.low_part);
		REG_WRITE(DMCUB_REGION3_CW2_OFFSET_HIGH, offset.u.high_part);
		REG_WRITE(DMCUB_REGION3_CW2_BASE_ADDRESS, cw2->region.base);
		REG_SET_2(DMCUB_REGION3_CW2_TOP_ADDRESS, 0,
			  DMCUB_REGION3_CW2_TOP_ADDRESS, cw2->region.top,
			  DMCUB_REGION3_CW2_ENABLE, 1);
	} else {
		REG_WRITE(DMCUB_REGION3_CW2_OFFSET, 0);
		REG_WRITE(DMCUB_REGION3_CW2_OFFSET_HIGH, 0);
		REG_WRITE(DMCUB_REGION3_CW2_BASE_ADDRESS, 0);
		REG_WRITE(DMCUB_REGION3_CW2_TOP_ADDRESS, 0);
	}

	offset = cw3->offset;

	REG_WRITE(DMCUB_REGION3_CW3_OFFSET, offset.u.low_part);
	REG_WRITE(DMCUB_REGION3_CW3_OFFSET_HIGH, offset.u.high_part);
	REG_WRITE(DMCUB_REGION3_CW3_BASE_ADDRESS, cw3->region.base);
	REG_SET_2(DMCUB_REGION3_CW3_TOP_ADDRESS, 0,
		  DMCUB_REGION3_CW3_TOP_ADDRESS, cw3->region.top,
		  DMCUB_REGION3_CW3_ENABLE, 1);

	offset = cw4->offset;

	/* New firmware can support CW4. */
	if (dmub_dcn20_use_cached_inbox(dmub)) {
		REG_WRITE(DMCUB_REGION3_CW4_OFFSET, offset.u.low_part);
		REG_WRITE(DMCUB_REGION3_CW4_OFFSET_HIGH, offset.u.high_part);
		REG_WRITE(DMCUB_REGION3_CW4_BASE_ADDRESS, cw4->region.base);
		REG_SET_2(DMCUB_REGION3_CW4_TOP_ADDRESS, 0,
			  DMCUB_REGION3_CW4_TOP_ADDRESS, cw4->region.top,
			  DMCUB_REGION3_CW4_ENABLE, 1);
	} else {
		REG_WRITE(DMCUB_REGION4_OFFSET, offset.u.low_part);
		REG_WRITE(DMCUB_REGION4_OFFSET_HIGH, offset.u.high_part);
		REG_SET_2(DMCUB_REGION4_TOP_ADDRESS, 0,
			  DMCUB_REGION4_TOP_ADDRESS,
			  cw4->region.top - cw4->region.base - 1,
			  DMCUB_REGION4_ENABLE, 1);
	}

	offset = cw5->offset;

	REG_WRITE(DMCUB_REGION3_CW5_OFFSET, offset.u.low_part);
	REG_WRITE(DMCUB_REGION3_CW5_OFFSET_HIGH, offset.u.high_part);
	REG_WRITE(DMCUB_REGION3_CW5_BASE_ADDRESS, cw5->region.base);
	REG_SET_2(DMCUB_REGION3_CW5_TOP_ADDRESS, 0,
		  DMCUB_REGION3_CW5_TOP_ADDRESS, cw5->region.top,
		  DMCUB_REGION3_CW5_ENABLE, 1);

	offset = cw6->offset;

	REG_WRITE(DMCUB_REGION3_CW6_OFFSET, offset.u.low_part);
	REG_WRITE(DMCUB_REGION3_CW6_OFFSET_HIGH, offset.u.high_part);
	REG_WRITE(DMCUB_REGION3_CW6_BASE_ADDRESS, cw6->region.base);
	REG_SET_2(DMCUB_REGION3_CW6_TOP_ADDRESS, 0,
		  DMCUB_REGION3_CW6_TOP_ADDRESS, cw6->region.top,
		  DMCUB_REGION3_CW6_ENABLE, 1);
}
