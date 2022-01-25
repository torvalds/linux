// SPDX-License-Identifier: GPL-2.0
/*
 * Renesas R-Car V3U System Controller
 *
 * Copyright (C) 2020 Renesas Electronics Corp.
 */

#include <linux/bits.h>
#include <linux/clk/renesas.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/of_address.h>
#include <linux/pm_domain.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <dt-bindings/power/r8a779a0-sysc.h>

#include "rcar-gen4-sysc.h"

static struct rcar_gen4_sysc_area r8a779a0_areas[] __initdata = {
	{ "always-on",	R8A779A0_PD_ALWAYS_ON, -1, PD_ALWAYS_ON },
	{ "a3e0",	R8A779A0_PD_A3E0, R8A779A0_PD_ALWAYS_ON, PD_SCU },
	{ "a3e1",	R8A779A0_PD_A3E1, R8A779A0_PD_ALWAYS_ON, PD_SCU },
	{ "a2e0d0",	R8A779A0_PD_A2E0D0, R8A779A0_PD_A3E0, PD_SCU },
	{ "a2e0d1",	R8A779A0_PD_A2E0D1, R8A779A0_PD_A3E0, PD_SCU },
	{ "a2e1d0",	R8A779A0_PD_A2E1D0, R8A779A0_PD_A3E1, PD_SCU },
	{ "a2e1d1",	R8A779A0_PD_A2E1D1, R8A779A0_PD_A3E1, PD_SCU },
	{ "a1e0d0c0",	R8A779A0_PD_A1E0D0C0, R8A779A0_PD_A2E0D0, PD_CPU_NOCR },
	{ "a1e0d0c1",	R8A779A0_PD_A1E0D0C1, R8A779A0_PD_A2E0D0, PD_CPU_NOCR },
	{ "a1e0d1c0",	R8A779A0_PD_A1E0D1C0, R8A779A0_PD_A2E0D1, PD_CPU_NOCR },
	{ "a1e0d1c1",	R8A779A0_PD_A1E0D1C1, R8A779A0_PD_A2E0D1, PD_CPU_NOCR },
	{ "a1e1d0c0",	R8A779A0_PD_A1E1D0C0, R8A779A0_PD_A2E1D0, PD_CPU_NOCR },
	{ "a1e1d0c1",	R8A779A0_PD_A1E1D0C1, R8A779A0_PD_A2E1D0, PD_CPU_NOCR },
	{ "a1e1d1c0",	R8A779A0_PD_A1E1D1C0, R8A779A0_PD_A2E1D1, PD_CPU_NOCR },
	{ "a1e1d1c1",	R8A779A0_PD_A1E1D1C1, R8A779A0_PD_A2E1D1, PD_CPU_NOCR },
	{ "3dg-a",	R8A779A0_PD_3DG_A, R8A779A0_PD_ALWAYS_ON },
	{ "3dg-b",	R8A779A0_PD_3DG_B, R8A779A0_PD_3DG_A },
	{ "a3vip0",	R8A779A0_PD_A3VIP0, R8A779A0_PD_ALWAYS_ON },
	{ "a3vip1",	R8A779A0_PD_A3VIP1, R8A779A0_PD_ALWAYS_ON },
	{ "a3vip3",	R8A779A0_PD_A3VIP3, R8A779A0_PD_ALWAYS_ON },
	{ "a3vip2",	R8A779A0_PD_A3VIP2, R8A779A0_PD_ALWAYS_ON },
	{ "a3isp01",	R8A779A0_PD_A3ISP01, R8A779A0_PD_ALWAYS_ON },
	{ "a3isp23",	R8A779A0_PD_A3ISP23, R8A779A0_PD_ALWAYS_ON },
	{ "a3ir",	R8A779A0_PD_A3IR, R8A779A0_PD_ALWAYS_ON },
	{ "a2cn0",	R8A779A0_PD_A2CN0, R8A779A0_PD_A3IR },
	{ "a2imp01",	R8A779A0_PD_A2IMP01, R8A779A0_PD_A3IR },
	{ "a2dp0",	R8A779A0_PD_A2DP0, R8A779A0_PD_A3IR },
	{ "a2cv0",	R8A779A0_PD_A2CV0, R8A779A0_PD_A3IR },
	{ "a2cv1",	R8A779A0_PD_A2CV1, R8A779A0_PD_A3IR },
	{ "a2cv4",	R8A779A0_PD_A2CV4, R8A779A0_PD_A3IR },
	{ "a2cv6",	R8A779A0_PD_A2CV6, R8A779A0_PD_A3IR },
	{ "a2cn2",	R8A779A0_PD_A2CN2, R8A779A0_PD_A3IR },
	{ "a2imp23",	R8A779A0_PD_A2IMP23, R8A779A0_PD_A3IR },
	{ "a2dp1",	R8A779A0_PD_A2DP0, R8A779A0_PD_A3IR },
	{ "a2cv2",	R8A779A0_PD_A2CV0, R8A779A0_PD_A3IR },
	{ "a2cv3",	R8A779A0_PD_A2CV1, R8A779A0_PD_A3IR },
	{ "a2cv5",	R8A779A0_PD_A2CV4, R8A779A0_PD_A3IR },
	{ "a2cv7",	R8A779A0_PD_A2CV6, R8A779A0_PD_A3IR },
	{ "a2cn1",	R8A779A0_PD_A2CN1, R8A779A0_PD_A3IR },
	{ "a1cnn0",	R8A779A0_PD_A1CNN0, R8A779A0_PD_A2CN0 },
	{ "a1cnn2",	R8A779A0_PD_A1CNN2, R8A779A0_PD_A2CN2 },
	{ "a1dsp0",	R8A779A0_PD_A1DSP0, R8A779A0_PD_A2CN2 },
	{ "a1cnn1",	R8A779A0_PD_A1CNN1, R8A779A0_PD_A2CN1 },
	{ "a1dsp1",	R8A779A0_PD_A1DSP1, R8A779A0_PD_A2CN1 },
};

const struct rcar_gen4_sysc_info r8a779a0_sysc_info __initconst = {
	.areas = r8a779a0_areas,
	.num_areas = ARRAY_SIZE(r8a779a0_areas),
};
