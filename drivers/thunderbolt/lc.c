// SPDX-License-Identifier: GPL-2.0
/*
 * Thunderbolt link controller support
 *
 * Copyright (C) 2019, Intel Corporation
 * Author: Mika Westerberg <mika.westerberg@linux.intel.com>
 */

#include "tb.h"

/**
 * tb_lc_read_uuid() - Read switch UUID from link controller common register
 * @sw: Switch whose UUID is read
 * @uuid: UUID is placed here
 */
int tb_lc_read_uuid(struct tb_switch *sw, u32 *uuid)
{
	if (!sw->cap_lc)
		return -EINVAL;
	return tb_sw_read(sw, uuid, TB_CFG_SWITCH, sw->cap_lc + TB_LC_FUSE, 4);
}
