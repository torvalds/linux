// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Marvell MMP3 aka PXA2128 aka 88AP2128 support
 *
 *  Copyright (C) 2019 Lubomir Rintel <lkundrak@v3.sk>
 */

#include <asm/mach/arch.h>
#include <asm/hardware/cache-l2x0.h>

#include "common.h"

static const char *const mmp3_dt_board_compat[] __initconst = {
	"marvell,mmp3",
	NULL,
};

DT_MACHINE_START(MMP2_DT, "Marvell MMP3")
	.map_io		= mmp2_map_io,
	.dt_compat	= mmp3_dt_board_compat,
	.l2c_aux_val	= 1 << L310_AUX_CTRL_FWA_SHIFT |
			  L310_AUX_CTRL_DATA_PREFETCH |
			  L310_AUX_CTRL_INSTR_PREFETCH,
	.l2c_aux_mask	= 0xc20fffff,
MACHINE_END
