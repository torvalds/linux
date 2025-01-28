// SPDX-License-Identifier: GPL-2.0
/*
 * RZ/V2H System controller (SYS) driver
 *
 * Copyright (C) 2025 Renesas Electronics Corp.
 */

#include <linux/bits.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/io.h>

#include "rz-sysc.h"

static const struct rz_sysc_soc_id_init_data rzv2h_sys_soc_id_init_data __initconst = {
	.family = "RZ/V2H",
	.id = 0x847a447,
	.devid_offset = 0x304,
	.revision_mask = GENMASK(31, 28),
	.specific_id_mask = GENMASK(27, 0),
};

const struct rz_sysc_init_data rzv2h_sys_init_data = {
	.soc_id_init_data = &rzv2h_sys_soc_id_init_data,
};
