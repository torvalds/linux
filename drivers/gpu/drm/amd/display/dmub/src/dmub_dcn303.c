// SPDX-License-Identifier: MIT
/*
 * Copyright (C) 2021 Advanced Micro Devices, Inc.
 *
 * Authors: AMD
 */

#include "../dmub_srv.h"
#include "dmub_reg.h"
#include "dmub_dcn303.h"

#include "sienna_cichlid_ip_offset.h"
#include "dcn/dcn_3_0_3_offset.h"
#include "dcn/dcn_3_0_3_sh_mask.h"

#define BASE_INNER(seg) DCN_BASE__INST0_SEG##seg
#define CTX dmub
#define REGS dmub->regs

/* Registers. */

const struct dmub_srv_common_regs dmub_srv_dcn303_regs = {
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

