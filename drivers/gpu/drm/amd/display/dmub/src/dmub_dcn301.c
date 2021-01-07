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
#include "dmub_dcn301.h"

#include "dcn/dcn_3_0_1_offset.h"
#include "dcn/dcn_3_0_1_sh_mask.h"
#include "vangogh_ip_offset.h"

#define BASE_INNER(seg) DCN_BASE__INST0_SEG##seg
#define CTX dmub
#define REGS dmub->regs

/* Registers. */

const struct dmub_srv_common_regs dmub_srv_dcn301_regs = {
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

