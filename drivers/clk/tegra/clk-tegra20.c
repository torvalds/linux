/*
 * Copyright (c) 2012, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/io.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/clk/tegra.h>
#include <linux/delay.h>
#include <dt-bindings/clock/tegra20-car.h>

#include "clk.h"
#include "clk-id.h"

#define OSC_CTRL 0x50
#define OSC_CTRL_OSC_FREQ_MASK (3<<30)
#define OSC_CTRL_OSC_FREQ_13MHZ (0<<30)
#define OSC_CTRL_OSC_FREQ_19_2MHZ (1<<30)
#define OSC_CTRL_OSC_FREQ_12MHZ (2<<30)
#define OSC_CTRL_OSC_FREQ_26MHZ (3<<30)
#define OSC_CTRL_MASK (0x3f2 | OSC_CTRL_OSC_FREQ_MASK)

#define OSC_CTRL_PLL_REF_DIV_MASK (3<<28)
#define OSC_CTRL_PLL_REF_DIV_1		(0<<28)
#define OSC_CTRL_PLL_REF_DIV_2		(1<<28)
#define OSC_CTRL_PLL_REF_DIV_4		(2<<28)

#define OSC_FREQ_DET 0x58
#define OSC_FREQ_DET_TRIG (1<<31)

#define OSC_FREQ_DET_STATUS 0x5c
#define OSC_FREQ_DET_BUSY (1<<31)
#define OSC_FREQ_DET_CNT_MASK 0xFFFF

#define TEGRA20_CLK_PERIPH_BANKS	3

#define PLLS_BASE 0xf0
#define PLLS_MISC 0xf4
#define PLLC_BASE 0x80
#define PLLC_MISC 0x8c
#define PLLM_BASE 0x90
#define PLLM_MISC 0x9c
#define PLLP_BASE 0xa0
#define PLLP_MISC 0xac
#define PLLA_BASE 0xb0
#define PLLA_MISC 0xbc
#define PLLU_BASE 0xc0
#define PLLU_MISC 0xcc
#define PLLD_BASE 0xd0
#define PLLD_MISC 0xdc
#define PLLX_BASE 0xe0
#define PLLX_MISC 0xe4
#define PLLE_BASE 0xe8
#define PLLE_MISC 0xec

#define PLL_BASE_LOCK BIT(27)
#define PLLE_MISC_LOCK BIT(11)

#define PLL_MISC_LOCK_ENABLE 18
#define PLLDU_MISC_LOCK_ENABLE 22
#define PLLE_MISC_LOCK_ENABLE 9

#define PLLC_OUT 0x84
#define PLLM_OUT 0x94
#define PLLP_OUTA 0xa4
#define PLLP_OUTB 0xa8
#define PLLA_OUT 0xb4

#define CCLK_BURST_POLICY 0x20
#define SUPER_CCLK_DIVIDER 0x24
#define SCLK_BURST_POLICY 0x28
#define SUPER_SCLK_DIVIDER 0x2c
#define CLK_SYSTEM_RATE 0x30

#define CCLK_BURST_POLICY_SHIFT	28
#define CCLK_RUN_POLICY_SHIFT	4
#define CCLK_IDLE_POLICY_SHIFT	0
#define CCLK_IDLE_POLICY	1
#define CCLK_RUN_POLICY		2
#define CCLK_BURST_POLICY_PLLX	8

#define CLK_SOURCE_I2S1 0x100
#define CLK_SOURCE_I2S2 0x104
#define CLK_SOURCE_PWM 0x110
#define CLK_SOURCE_SPI 0x114
#define CLK_SOURCE_XIO 0x120
#define CLK_SOURCE_TWC 0x12c
#define CLK_SOURCE_IDE 0x144
#define CLK_SOURCE_HDMI 0x18c
#define CLK_SOURCE_DISP1 0x138
#define CLK_SOURCE_DISP2 0x13c
#define CLK_SOURCE_CSITE 0x1d4
#define CLK_SOURCE_I2C1 0x124
#define CLK_SOURCE_I2C2 0x198
#define CLK_SOURCE_I2C3 0x1b8
#define CLK_SOURCE_DVC 0x128
#define CLK_SOURCE_UARTA 0x178
#define CLK_SOURCE_UARTB 0x17c
#define CLK_SOURCE_UARTC 0x1a0
#define CLK_SOURCE_UARTD 0x1c0
#define CLK_SOURCE_UARTE 0x1c4
#define CLK_SOURCE_EMC 0x19c

#define AUDIO_SYNC_CLK 0x38

/* Tegra CPU clock and reset control regs */
#define TEGRA_CLK_RST_CONTROLLER_CLK_CPU_CMPLX		0x4c
#define TEGRA_CLK_RST_CONTROLLER_RST_CPU_CMPLX_SET	0x340
#define TEGRA_CLK_RST_CONTROLLER_RST_CPU_CMPLX_CLR	0x344

#define CPU_CLOCK(cpu)	(0x1 << (8 + cpu))
#define CPU_RESET(cpu)	(0x1111ul << (cpu))

#ifdef CONFIG_PM_SLEEP
static struct cpu_clk_suspend_context {
	u32 pllx_misc;
	u32 pllx_base;

	u32 cpu_burst;
	u32 clk_csite_src;
	u32 cclk_divider;
} tegra20_cpu_clk_sctx;
#endif

static void __iomem *clk_base;
static void __iomem *pmc_base;

static DEFINE_SPINLOCK(emc_lock);

#define TEGRA_INIT_DATA_MUX(_name, _parents, _offset,	\
			    _clk_num, _gate_flags, _clk_id)	\
	TEGRA_INIT_DATA(_name, NULL, NULL, _parents, _offset,	\
			30, 2, 0, 0, 8, 1, TEGRA_DIVIDER_ROUND_UP,	\
			_clk_num, \
			_gate_flags, _clk_id)

#define TEGRA_INIT_DATA_DIV16(_name, _parents, _offset, \
			      _clk_num, _gate_flags, _clk_id)	\
	TEGRA_INIT_DATA(_name, NULL, NULL, _parents, _offset,	\
			30, 2, 0, 0, 16, 0, TEGRA_DIVIDER_ROUND_UP, \
			_clk_num, _gate_flags,	\
			_clk_id)

#define TEGRA_INIT_DATA_NODIV(_name, _parents, _offset, \
			      _mux_shift, _mux_width, _clk_num, \
			      _gate_flags, _clk_id)			\
	TEGRA_INIT_DATA(_name, NULL, NULL, _parents, _offset,	\
			_mux_shift, _mux_width, 0, 0, 0, 0, 0, \
			_clk_num, _gate_flags,	\
			_clk_id)

static struct clk **clks;

static struct tegra_clk_pll_freq_table pll_c_freq_table[] = {
	{ 12000000, 600000000, 600, 12, 0, 8 },
	{ 13000000, 600000000, 600, 13, 0, 8 },
	{ 19200000, 600000000, 500, 16, 0, 6 },
	{ 26000000, 600000000, 600, 26, 0, 8 },
	{ 0, 0, 0, 0, 0, 0 },
};

static struct tegra_clk_pll_freq_table pll_m_freq_table[] = {
	{ 12000000, 666000000, 666, 12, 0, 8},
	{ 13000000, 666000000, 666, 13, 0, 8},
	{ 19200000, 666000000, 555, 16, 0, 8},
	{ 26000000, 666000000, 666, 26, 0, 8},
	{ 12000000, 600000000, 600, 12, 0, 8},
	{ 13000000, 600000000, 600, 13, 0, 8},
	{ 19200000, 600000000, 375, 12, 0, 6},
	{ 26000000, 600000000, 600, 26, 0, 8},
	{ 0, 0, 0, 0, 0, 0 },
};

static struct tegra_clk_pll_freq_table pll_p_freq_table[] = {
	{ 12000000, 216000000, 432, 12, 1, 8},
	{ 13000000, 216000000, 432, 13, 1, 8},
	{ 19200000, 216000000, 90,   4, 1, 1},
	{ 26000000, 216000000, 432, 26, 1, 8},
	{ 12000000, 432000000, 432, 12, 0, 8},
	{ 13000000, 432000000, 432, 13, 0, 8},
	{ 19200000, 432000000, 90,   4, 0, 1},
	{ 26000000, 432000000, 432, 26, 0, 8},
	{ 0, 0, 0, 0, 0, 0 },
};

static struct tegra_clk_pll_freq_table pll_a_freq_table[] = {
	{ 28800000, 56448000, 49, 25, 0, 1},
	{ 28800000, 73728000, 64, 25, 0, 1},
	{ 28800000, 24000000,  5,  6, 0, 1},
	{ 0, 0, 0, 0, 0, 0 },
};

static struct tegra_clk_pll_freq_table pll_d_freq_table[] = {
	{ 12000000, 216000000, 216, 12, 0, 4},
	{ 13000000, 216000000, 216, 13, 0, 4},
	{ 19200000, 216000000, 135, 12, 0, 3},
	{ 26000000, 216000000, 216, 26, 0, 4},

	{ 12000000, 594000000, 594, 12, 0, 8},
	{ 13000000, 594000000, 594, 13, 0, 8},
	{ 19200000, 594000000, 495, 16, 0, 8},
	{ 26000000, 594000000, 594, 26, 0, 8},

	{ 12000000, 1000000000, 1000, 12, 0, 12},
	{ 13000000, 1000000000, 1000, 13, 0, 12},
	{ 19200000, 1000000000, 625,  12, 0, 8},
	{ 26000000, 1000000000, 1000, 26, 0, 12},

	{ 0, 0, 0, 0, 0, 0 },
};

static struct tegra_clk_pll_freq_table pll_u_freq_table[] = {
	{ 12000000, 480000000, 960, 12, 0, 0},
	{ 13000000, 480000000, 960, 13, 0, 0},
	{ 19200000, 480000000, 200, 4,  0, 0},
	{ 26000000, 480000000, 960, 26, 0, 0},
	{ 0, 0, 0, 0, 0, 0 },
};

static struct tegra_clk_pll_freq_table pll_x_freq_table[] = {
	/* 1 GHz */
	{ 12000000, 1000000000, 1000, 12, 0, 12},
	{ 13000000, 1000000000, 1000, 13, 0, 12},
	{ 19200000, 1000000000, 625,  12, 0, 8},
	{ 26000000, 1000000000, 1000, 26, 0, 12},

	/* 912 MHz */
	{ 12000000, 912000000,  912,  12, 0, 12},
	{ 13000000, 912000000,  912,  13, 0, 12},
	{ 19200000, 912000000,  760,  16, 0, 8},
	{ 26000000, 912000000,  912,  26, 0, 12},

	/* 816 MHz */
	{ 12000000, 816000000,  816,  12, 0, 12},
	{ 13000000, 816000000,  816,  13, 0, 12},
	{ 19200000, 816000000,  680,  16, 0, 8},
	{ 26000000, 816000000,  816,  26, 0, 12},

	/* 760 MHz */
	{ 12000000, 760000000,  760,  12, 0, 12},
	{ 13000000, 760000000,  760,  13, 0, 12},
	{ 19200000, 760000000,  950,  24, 0, 8},
	{ 26000000, 760000000,  760,  26, 0, 12},

	/* 750 MHz */
	{ 12000000, 750000000,  750,  12, 0, 12},
	{ 13000000, 750000000,  750,  13, 0, 12},
	{ 19200000, 750000000,  625,  16, 0, 8},
	{ 26000000, 750000000,  750,  26, 0, 12},

	/* 608 MHz */
	{ 12000000, 608000000,  608,  12, 0, 12},
	{ 13000000, 608000000,  608,  13, 0, 12},
	{ 19200000, 608000000,  380,  12, 0, 8},
	{ 26000000, 608000000,  608,  26, 0, 12},

	/* 456 MHz */
	{ 12000000, 456000000,  456,  12, 0, 12},
	{ 13000000, 456000000,  456,  13, 0, 12},
	{ 19200000, 456000000,  380,  16, 0, 8},
	{ 26000000, 456000000,  456,  26, 0, 12},

	/* 312 MHz */
	{ 12000000, 312000000,  312,  12, 0, 12},
	{ 13000000, 312000000,  312,  13, 0, 12},
	{ 19200000, 312000000,  260,  16, 0, 8},
	{ 26000000, 312000000,  312,  26, 0, 12},

	{ 0, 0, 0, 0, 0, 0 },
};

static struct tegra_clk_pll_freq_table pll_e_freq_table[] = {
	{ 12000000, 100000000,  200,  24, 0, 0 },
	{ 0, 0, 0, 0, 0, 0 },
};

/* PLL parameters */
static struct tegra_clk_pll_params pll_c_params = {
	.input_min = 2000000,
	.input_max = 31000000,
	.cf_min = 1000000,
	.cf_max = 6000000,
	.vco_min = 20000000,
	.vco_max = 1400000000,
	.base_reg = PLLC_BASE,
	.misc_reg = PLLC_MISC,
	.lock_mask = PLL_BASE_LOCK,
	.lock_enable_bit_idx = PLL_MISC_LOCK_ENABLE,
	.lock_delay = 300,
	.freq_table = pll_c_freq_table,
	.flags = TEGRA_PLL_HAS_CPCON,
};

static struct tegra_clk_pll_params pll_m_params = {
	.input_min = 2000000,
	.input_max = 31000000,
	.cf_min = 1000000,
	.cf_max = 6000000,
	.vco_min = 20000000,
	.vco_max = 1200000000,
	.base_reg = PLLM_BASE,
	.misc_reg = PLLM_MISC,
	.lock_mask = PLL_BASE_LOCK,
	.lock_enable_bit_idx = PLL_MISC_LOCK_ENABLE,
	.lock_delay = 300,
	.freq_table = pll_m_freq_table,
	.flags = TEGRA_PLL_HAS_CPCON,
};

static struct tegra_clk_pll_params pll_p_params = {
	.input_min = 2000000,
	.input_max = 31000000,
	.cf_min = 1000000,
	.cf_max = 6000000,
	.vco_min = 20000000,
	.vco_max = 1400000000,
	.base_reg = PLLP_BASE,
	.misc_reg = PLLP_MISC,
	.lock_mask = PLL_BASE_LOCK,
	.lock_enable_bit_idx = PLL_MISC_LOCK_ENABLE,
	.lock_delay = 300,
	.freq_table = pll_p_freq_table,
	.flags = TEGRA_PLL_FIXED | TEGRA_PLL_HAS_CPCON,
	.fixed_rate =  216000000,
};

static struct tegra_clk_pll_params pll_a_params = {
	.input_min = 2000000,
	.input_max = 31000000,
	.cf_min = 1000000,
	.cf_max = 6000000,
	.vco_min = 20000000,
	.vco_max = 1400000000,
	.base_reg = PLLA_BASE,
	.misc_reg = PLLA_MISC,
	.lock_mask = PLL_BASE_LOCK,
	.lock_enable_bit_idx = PLL_MISC_LOCK_ENABLE,
	.lock_delay = 300,
	.freq_table = pll_a_freq_table,
	.flags = TEGRA_PLL_HAS_CPCON,
};

static struct tegra_clk_pll_params pll_d_params = {
	.input_min = 2000000,
	.input_max = 40000000,
	.cf_min = 1000000,
	.cf_max = 6000000,
	.vco_min = 40000000,
	.vco_max = 1000000000,
	.base_reg = PLLD_BASE,
	.misc_reg = PLLD_MISC,
	.lock_mask = PLL_BASE_LOCK,
	.lock_enable_bit_idx = PLLDU_MISC_LOCK_ENABLE,
	.lock_delay = 1000,
	.freq_table = pll_d_freq_table,
	.flags = TEGRA_PLL_HAS_CPCON,
};

static struct pdiv_map pllu_p[] = {
	{ .pdiv = 1, .hw_val = 1 },
	{ .pdiv = 2, .hw_val = 0 },
	{ .pdiv = 0, .hw_val = 0 },
};

static struct tegra_clk_pll_params pll_u_params = {
	.input_min = 2000000,
	.input_max = 40000000,
	.cf_min = 1000000,
	.cf_max = 6000000,
	.vco_min = 48000000,
	.vco_max = 960000000,
	.base_reg = PLLU_BASE,
	.misc_reg = PLLU_MISC,
	.lock_mask = PLL_BASE_LOCK,
	.lock_enable_bit_idx = PLLDU_MISC_LOCK_ENABLE,
	.lock_delay = 1000,
	.pdiv_tohw = pllu_p,
	.freq_table = pll_u_freq_table,
	.flags = TEGRA_PLLU | TEGRA_PLL_HAS_CPCON,
};

static struct tegra_clk_pll_params pll_x_params = {
	.input_min = 2000000,
	.input_max = 31000000,
	.cf_min = 1000000,
	.cf_max = 6000000,
	.vco_min = 20000000,
	.vco_max = 1200000000,
	.base_reg = PLLX_BASE,
	.misc_reg = PLLX_MISC,
	.lock_mask = PLL_BASE_LOCK,
	.lock_enable_bit_idx = PLL_MISC_LOCK_ENABLE,
	.lock_delay = 300,
	.freq_table = pll_x_freq_table,
	.flags = TEGRA_PLL_HAS_CPCON,
};

static struct tegra_clk_pll_params pll_e_params = {
	.input_min = 12000000,
	.input_max = 12000000,
	.cf_min = 0,
	.cf_max = 0,
	.vco_min = 0,
	.vco_max = 0,
	.base_reg = PLLE_BASE,
	.misc_reg = PLLE_MISC,
	.lock_mask = PLLE_MISC_LOCK,
	.lock_enable_bit_idx = PLLE_MISC_LOCK_ENABLE,
	.lock_delay = 0,
	.freq_table = pll_e_freq_table,
	.flags = TEGRA_PLL_FIXED,
	.fixed_rate = 100000000,
};

static struct tegra_devclk devclks[] __initdata = {
	{ .con_id = "pll_c", .dt_id = TEGRA20_CLK_PLL_C },
	{ .con_id = "pll_c_out1", .dt_id = TEGRA20_CLK_PLL_C_OUT1 },
	{ .con_id = "pll_p", .dt_id = TEGRA20_CLK_PLL_P },
	{ .con_id = "pll_p_out1", .dt_id = TEGRA20_CLK_PLL_P_OUT1 },
	{ .con_id = "pll_p_out2", .dt_id = TEGRA20_CLK_PLL_P_OUT2 },
	{ .con_id = "pll_p_out3", .dt_id = TEGRA20_CLK_PLL_P_OUT3 },
	{ .con_id = "pll_p_out4", .dt_id = TEGRA20_CLK_PLL_P_OUT4 },
	{ .con_id = "pll_m", .dt_id = TEGRA20_CLK_PLL_M },
	{ .con_id = "pll_m_out1", .dt_id = TEGRA20_CLK_PLL_M_OUT1 },
	{ .con_id = "pll_x", .dt_id = TEGRA20_CLK_PLL_X },
	{ .con_id = "pll_u", .dt_id = TEGRA20_CLK_PLL_U },
	{ .con_id = "pll_d", .dt_id = TEGRA20_CLK_PLL_D },
	{ .con_id = "pll_d_out0", .dt_id = TEGRA20_CLK_PLL_D_OUT0 },
	{ .con_id = "pll_a", .dt_id = TEGRA20_CLK_PLL_A },
	{ .con_id = "pll_a_out0", .dt_id = TEGRA20_CLK_PLL_A_OUT0 },
	{ .con_id = "pll_e", .dt_id = TEGRA20_CLK_PLL_E },
	{ .con_id = "cclk", .dt_id = TEGRA20_CLK_CCLK },
	{ .con_id = "sclk", .dt_id = TEGRA20_CLK_SCLK },
	{ .con_id = "hclk", .dt_id = TEGRA20_CLK_HCLK },
	{ .con_id = "pclk", .dt_id = TEGRA20_CLK_PCLK },
	{ .con_id = "fuse", .dt_id = TEGRA20_CLK_FUSE },
	{ .con_id = "twd", .dt_id = TEGRA20_CLK_TWD },
	{ .con_id = "audio", .dt_id = TEGRA20_CLK_AUDIO },
	{ .con_id = "audio_2x", .dt_id = TEGRA20_CLK_AUDIO_2X },
	{ .dev_id = "tegra20-ac97", .dt_id = TEGRA20_CLK_AC97 },
	{ .dev_id = "tegra-apbdma", .dt_id = TEGRA20_CLK_APBDMA },
	{ .dev_id = "rtc-tegra", .dt_id = TEGRA20_CLK_RTC },
	{ .dev_id = "timer", .dt_id = TEGRA20_CLK_TIMER },
	{ .dev_id = "tegra-kbc", .dt_id = TEGRA20_CLK_KBC },
	{ .con_id = "csus", .dev_id =  "tegra_camera", .dt_id = TEGRA20_CLK_CSUS },
	{ .con_id = "vcp", .dev_id = "tegra-avp", .dt_id = TEGRA20_CLK_VCP },
	{ .con_id = "bsea", .dev_id = "tegra-avp", .dt_id = TEGRA20_CLK_BSEA },
	{ .con_id = "bsev", .dev_id = "tegra-aes", .dt_id = TEGRA20_CLK_BSEV },
	{ .con_id = "emc", .dt_id = TEGRA20_CLK_EMC },
	{ .dev_id = "fsl-tegra-udc", .dt_id = TEGRA20_CLK_USBD },
	{ .dev_id = "tegra-ehci.1", .dt_id = TEGRA20_CLK_USB2 },
	{ .dev_id = "tegra-ehci.2", .dt_id = TEGRA20_CLK_USB3 },
	{ .dev_id = "dsi", .dt_id = TEGRA20_CLK_DSI },
	{ .con_id = "csi", .dev_id = "tegra_camera", .dt_id = TEGRA20_CLK_CSI },
	{ .con_id = "isp", .dev_id = "tegra_camera", .dt_id = TEGRA20_CLK_ISP },
	{ .con_id = "pex", .dt_id = TEGRA20_CLK_PEX },
	{ .con_id = "afi", .dt_id = TEGRA20_CLK_AFI },
	{ .con_id = "cdev1", .dt_id = TEGRA20_CLK_CDEV1 },
	{ .con_id = "cdev2", .dt_id = TEGRA20_CLK_CDEV2 },
	{ .con_id = "clk_32k", .dt_id = TEGRA20_CLK_CLK_32K },
	{ .con_id = "blink", .dt_id = TEGRA20_CLK_BLINK },
	{ .con_id = "clk_m", .dt_id = TEGRA20_CLK_CLK_M },
	{ .con_id = "pll_ref", .dt_id = TEGRA20_CLK_PLL_REF },
	{ .dev_id = "tegra20-i2s.0", .dt_id = TEGRA20_CLK_I2S1 },
	{ .dev_id = "tegra20-i2s.1", .dt_id = TEGRA20_CLK_I2S2 },
	{ .con_id = "spdif_out", .dev_id = "tegra20-spdif", .dt_id = TEGRA20_CLK_SPDIF_OUT },
	{ .con_id = "spdif_in", .dev_id = "tegra20-spdif", .dt_id = TEGRA20_CLK_SPDIF_IN },
	{ .dev_id = "spi_tegra.0", .dt_id = TEGRA20_CLK_SBC1 },
	{ .dev_id = "spi_tegra.1", .dt_id = TEGRA20_CLK_SBC2 },
	{ .dev_id = "spi_tegra.2", .dt_id = TEGRA20_CLK_SBC3 },
	{ .dev_id = "spi_tegra.3", .dt_id = TEGRA20_CLK_SBC4 },
	{ .dev_id = "spi", .dt_id = TEGRA20_CLK_SPI },
	{ .dev_id = "xio", .dt_id = TEGRA20_CLK_XIO },
	{ .dev_id = "twc", .dt_id = TEGRA20_CLK_TWC },
	{ .dev_id = "ide", .dt_id = TEGRA20_CLK_IDE },
	{ .dev_id = "tegra_nand", .dt_id = TEGRA20_CLK_NDFLASH },
	{ .dev_id = "vfir", .dt_id = TEGRA20_CLK_VFIR },
	{ .dev_id = "csite", .dt_id = TEGRA20_CLK_CSITE },
	{ .dev_id = "la", .dt_id = TEGRA20_CLK_LA },
	{ .dev_id = "tegra_w1", .dt_id = TEGRA20_CLK_OWR },
	{ .dev_id = "mipi", .dt_id = TEGRA20_CLK_MIPI },
	{ .dev_id = "vde", .dt_id = TEGRA20_CLK_VDE },
	{ .con_id = "vi", .dev_id =  "tegra_camera", .dt_id = TEGRA20_CLK_VI },
	{ .dev_id = "epp", .dt_id = TEGRA20_CLK_EPP },
	{ .dev_id = "mpe", .dt_id = TEGRA20_CLK_MPE },
	{ .dev_id = "host1x", .dt_id = TEGRA20_CLK_HOST1X },
	{ .dev_id = "3d", .dt_id = TEGRA20_CLK_GR3D },
	{ .dev_id = "2d", .dt_id = TEGRA20_CLK_GR2D },
	{ .dev_id = "tegra-nor", .dt_id = TEGRA20_CLK_NOR },
	{ .dev_id = "sdhci-tegra.0", .dt_id = TEGRA20_CLK_SDMMC1 },
	{ .dev_id = "sdhci-tegra.1", .dt_id = TEGRA20_CLK_SDMMC2 },
	{ .dev_id = "sdhci-tegra.2", .dt_id = TEGRA20_CLK_SDMMC3 },
	{ .dev_id = "sdhci-tegra.3", .dt_id = TEGRA20_CLK_SDMMC4 },
	{ .dev_id = "cve", .dt_id = TEGRA20_CLK_CVE },
	{ .dev_id = "tvo", .dt_id = TEGRA20_CLK_TVO },
	{ .dev_id = "tvdac", .dt_id = TEGRA20_CLK_TVDAC },
	{ .con_id = "vi_sensor", .dev_id = "tegra_camera", .dt_id = TEGRA20_CLK_VI_SENSOR },
	{ .dev_id = "hdmi", .dt_id = TEGRA20_CLK_HDMI },
	{ .con_id = "div-clk", .dev_id = "tegra-i2c.0", .dt_id = TEGRA20_CLK_I2C1 },
	{ .con_id = "div-clk", .dev_id = "tegra-i2c.1", .dt_id = TEGRA20_CLK_I2C2 },
	{ .con_id = "div-clk", .dev_id = "tegra-i2c.2", .dt_id = TEGRA20_CLK_I2C3 },
	{ .con_id = "div-clk", .dev_id = "tegra-i2c.3", .dt_id = TEGRA20_CLK_DVC },
	{ .dev_id = "tegra-pwm", .dt_id = TEGRA20_CLK_PWM },
	{ .dev_id = "tegra_uart.0", .dt_id = TEGRA20_CLK_UARTA },
	{ .dev_id = "tegra_uart.1", .dt_id = TEGRA20_CLK_UARTB },
	{ .dev_id = "tegra_uart.2", .dt_id = TEGRA20_CLK_UARTC },
	{ .dev_id = "tegra_uart.3", .dt_id = TEGRA20_CLK_UARTD },
	{ .dev_id = "tegra_uart.4", .dt_id = TEGRA20_CLK_UARTE },
	{ .dev_id = "tegradc.0", .dt_id = TEGRA20_CLK_DISP1 },
	{ .dev_id = "tegradc.1", .dt_id = TEGRA20_CLK_DISP2 },
};

static struct tegra_clk tegra20_clks[tegra_clk_max] __initdata = {
	[tegra_clk_spdif_out] = { .dt_id = TEGRA20_CLK_SPDIF_OUT, .present = true },
	[tegra_clk_spdif_in] = { .dt_id = TEGRA20_CLK_SPDIF_IN, .present = true },
	[tegra_clk_sdmmc1] = { .dt_id = TEGRA20_CLK_SDMMC1, .present = true },
	[tegra_clk_sdmmc2] = { .dt_id = TEGRA20_CLK_SDMMC2, .present = true },
	[tegra_clk_sdmmc3] = { .dt_id = TEGRA20_CLK_SDMMC3, .present = true },
	[tegra_clk_sdmmc4] = { .dt_id = TEGRA20_CLK_SDMMC4, .present = true },
	[tegra_clk_la] = { .dt_id = TEGRA20_CLK_LA, .present = true },
	[tegra_clk_csite] = { .dt_id = TEGRA20_CLK_CSITE, .present = true },
	[tegra_clk_vfir] = { .dt_id = TEGRA20_CLK_VFIR, .present = true },
	[tegra_clk_mipi] = { .dt_id = TEGRA20_CLK_MIPI, .present = true },
	[tegra_clk_nor] = { .dt_id = TEGRA20_CLK_NOR, .present = true },
	[tegra_clk_rtc] = { .dt_id = TEGRA20_CLK_RTC, .present = true },
	[tegra_clk_timer] = { .dt_id = TEGRA20_CLK_TIMER, .present = true },
	[tegra_clk_kbc] = { .dt_id = TEGRA20_CLK_KBC, .present = true },
	[tegra_clk_csus] = { .dt_id = TEGRA20_CLK_CSUS, .present = true },
	[tegra_clk_vcp] = { .dt_id = TEGRA20_CLK_VCP, .present = true },
	[tegra_clk_bsea] = { .dt_id = TEGRA20_CLK_BSEA, .present = true },
	[tegra_clk_bsev] = { .dt_id = TEGRA20_CLK_BSEV, .present = true },
	[tegra_clk_usbd] = { .dt_id = TEGRA20_CLK_USBD, .present = true },
	[tegra_clk_usb2] = { .dt_id = TEGRA20_CLK_USB2, .present = true },
	[tegra_clk_usb3] = { .dt_id = TEGRA20_CLK_USB3, .present = true },
	[tegra_clk_csi] = { .dt_id = TEGRA20_CLK_CSI, .present = true },
	[tegra_clk_isp] = { .dt_id = TEGRA20_CLK_ISP, .present = true },
	[tegra_clk_clk_32k] = { .dt_id = TEGRA20_CLK_CLK_32K, .present = true },
	[tegra_clk_blink] = { .dt_id = TEGRA20_CLK_BLINK, .present = true },
	[tegra_clk_hclk] = { .dt_id = TEGRA20_CLK_HCLK, .present = true },
	[tegra_clk_pclk] = { .dt_id = TEGRA20_CLK_PCLK, .present = true },
	[tegra_clk_pll_p_out1] = { .dt_id = TEGRA20_CLK_PLL_P_OUT1, .present = true },
	[tegra_clk_pll_p_out2] = { .dt_id = TEGRA20_CLK_PLL_P_OUT2, .present = true },
	[tegra_clk_pll_p_out3] = { .dt_id = TEGRA20_CLK_PLL_P_OUT3, .present = true },
	[tegra_clk_pll_p_out4] = { .dt_id = TEGRA20_CLK_PLL_P_OUT4, .present = true },
	[tegra_clk_pll_p] = { .dt_id = TEGRA20_CLK_PLL_P, .present = true },
	[tegra_clk_owr] = { .dt_id = TEGRA20_CLK_OWR, .present = true },
	[tegra_clk_sbc1] = { .dt_id = TEGRA20_CLK_SBC1, .present = true },
	[tegra_clk_sbc2] = { .dt_id = TEGRA20_CLK_SBC2, .present = true },
	[tegra_clk_sbc3] = { .dt_id = TEGRA20_CLK_SBC3, .present = true },
	[tegra_clk_sbc4] = { .dt_id = TEGRA20_CLK_SBC4, .present = true },
	[tegra_clk_vde] = { .dt_id = TEGRA20_CLK_VDE, .present = true },
	[tegra_clk_vi] = { .dt_id = TEGRA20_CLK_VI, .present = true },
	[tegra_clk_epp] = { .dt_id = TEGRA20_CLK_EPP, .present = true },
	[tegra_clk_mpe] = { .dt_id = TEGRA20_CLK_MPE, .present = true },
	[tegra_clk_host1x] = { .dt_id = TEGRA20_CLK_HOST1X, .present = true },
	[tegra_clk_gr2d] = { .dt_id = TEGRA20_CLK_GR2D, .present = true },
	[tegra_clk_gr3d] = { .dt_id = TEGRA20_CLK_GR3D, .present = true },
	[tegra_clk_ndflash] = { .dt_id = TEGRA20_CLK_NDFLASH, .present = true },
	[tegra_clk_cve] = { .dt_id = TEGRA20_CLK_CVE, .present = true },
	[tegra_clk_tvo] = { .dt_id = TEGRA20_CLK_TVO, .present = true },
	[tegra_clk_tvdac] = { .dt_id = TEGRA20_CLK_TVDAC, .present = true },
	[tegra_clk_vi_sensor] = { .dt_id = TEGRA20_CLK_VI_SENSOR, .present = true },
	[tegra_clk_afi] = { .dt_id = TEGRA20_CLK_AFI, .present = true },
	[tegra_clk_fuse] = { .dt_id = TEGRA20_CLK_FUSE, .present = true },
	[tegra_clk_kfuse] = { .dt_id = TEGRA20_CLK_KFUSE, .present = true },
};

static unsigned long tegra20_clk_measure_input_freq(void)
{
	u32 osc_ctrl = readl_relaxed(clk_base + OSC_CTRL);
	u32 auto_clk_control = osc_ctrl & OSC_CTRL_OSC_FREQ_MASK;
	u32 pll_ref_div = osc_ctrl & OSC_CTRL_PLL_REF_DIV_MASK;
	unsigned long input_freq;

	switch (auto_clk_control) {
	case OSC_CTRL_OSC_FREQ_12MHZ:
		BUG_ON(pll_ref_div != OSC_CTRL_PLL_REF_DIV_1);
		input_freq = 12000000;
		break;
	case OSC_CTRL_OSC_FREQ_13MHZ:
		BUG_ON(pll_ref_div != OSC_CTRL_PLL_REF_DIV_1);
		input_freq = 13000000;
		break;
	case OSC_CTRL_OSC_FREQ_19_2MHZ:
		BUG_ON(pll_ref_div != OSC_CTRL_PLL_REF_DIV_1);
		input_freq = 19200000;
		break;
	case OSC_CTRL_OSC_FREQ_26MHZ:
		BUG_ON(pll_ref_div != OSC_CTRL_PLL_REF_DIV_1);
		input_freq = 26000000;
		break;
	default:
		pr_err("Unexpected clock autodetect value %d",
		       auto_clk_control);
		BUG();
		return 0;
	}

	return input_freq;
}

static unsigned int tegra20_get_pll_ref_div(void)
{
	u32 pll_ref_div = readl_relaxed(clk_base + OSC_CTRL) &
		OSC_CTRL_PLL_REF_DIV_MASK;

	switch (pll_ref_div) {
	case OSC_CTRL_PLL_REF_DIV_1:
		return 1;
	case OSC_CTRL_PLL_REF_DIV_2:
		return 2;
	case OSC_CTRL_PLL_REF_DIV_4:
		return 4;
	default:
		pr_err("Invalied pll ref divider %d\n", pll_ref_div);
		BUG();
	}
	return 0;
}

static void tegra20_pll_init(void)
{
	struct clk *clk;

	/* PLLC */
	clk = tegra_clk_register_pll("pll_c", "pll_ref", clk_base, NULL, 0,
			    &pll_c_params, NULL);
	clks[TEGRA20_CLK_PLL_C] = clk;

	/* PLLC_OUT1 */
	clk = tegra_clk_register_divider("pll_c_out1_div", "pll_c",
				clk_base + PLLC_OUT, 0, TEGRA_DIVIDER_ROUND_UP,
				8, 8, 1, NULL);
	clk = tegra_clk_register_pll_out("pll_c_out1", "pll_c_out1_div",
				clk_base + PLLC_OUT, 1, 0, CLK_SET_RATE_PARENT,
				0, NULL);
	clks[TEGRA20_CLK_PLL_C_OUT1] = clk;

	/* PLLM */
	clk = tegra_clk_register_pll("pll_m", "pll_ref", clk_base, NULL,
			    CLK_IGNORE_UNUSED | CLK_SET_RATE_GATE,
			    &pll_m_params, NULL);
	clks[TEGRA20_CLK_PLL_M] = clk;

	/* PLLM_OUT1 */
	clk = tegra_clk_register_divider("pll_m_out1_div", "pll_m",
				clk_base + PLLM_OUT, 0, TEGRA_DIVIDER_ROUND_UP,
				8, 8, 1, NULL);
	clk = tegra_clk_register_pll_out("pll_m_out1", "pll_m_out1_div",
				clk_base + PLLM_OUT, 1, 0, CLK_IGNORE_UNUSED |
				CLK_SET_RATE_PARENT, 0, NULL);
	clks[TEGRA20_CLK_PLL_M_OUT1] = clk;

	/* PLLX */
	clk = tegra_clk_register_pll("pll_x", "pll_ref", clk_base, NULL, 0,
			    &pll_x_params, NULL);
	clks[TEGRA20_CLK_PLL_X] = clk;

	/* PLLU */
	clk = tegra_clk_register_pll("pll_u", "pll_ref", clk_base, NULL, 0,
			    &pll_u_params, NULL);
	clks[TEGRA20_CLK_PLL_U] = clk;

	/* PLLD */
	clk = tegra_clk_register_pll("pll_d", "pll_ref", clk_base, NULL, 0,
			    &pll_d_params, NULL);
	clks[TEGRA20_CLK_PLL_D] = clk;

	/* PLLD_OUT0 */
	clk = clk_register_fixed_factor(NULL, "pll_d_out0", "pll_d",
					CLK_SET_RATE_PARENT, 1, 2);
	clks[TEGRA20_CLK_PLL_D_OUT0] = clk;

	/* PLLA */
	clk = tegra_clk_register_pll("pll_a", "pll_p_out1", clk_base, NULL, 0,
			    &pll_a_params, NULL);
	clks[TEGRA20_CLK_PLL_A] = clk;

	/* PLLA_OUT0 */
	clk = tegra_clk_register_divider("pll_a_out0_div", "pll_a",
				clk_base + PLLA_OUT, 0, TEGRA_DIVIDER_ROUND_UP,
				8, 8, 1, NULL);
	clk = tegra_clk_register_pll_out("pll_a_out0", "pll_a_out0_div",
				clk_base + PLLA_OUT, 1, 0, CLK_IGNORE_UNUSED |
				CLK_SET_RATE_PARENT, 0, NULL);
	clks[TEGRA20_CLK_PLL_A_OUT0] = clk;

	/* PLLE */
	clk = tegra_clk_register_plle("pll_e", "pll_ref", clk_base, pmc_base,
			     0, &pll_e_params, NULL);
	clks[TEGRA20_CLK_PLL_E] = clk;
}

static const char *cclk_parents[] = { "clk_m", "pll_c", "clk_32k", "pll_m",
				      "pll_p", "pll_p_out4",
				      "pll_p_out3", "clk_d", "pll_x" };
static const char *sclk_parents[] = { "clk_m", "pll_c_out1", "pll_p_out4",
				      "pll_p_out3", "pll_p_out2", "clk_d",
				      "clk_32k", "pll_m_out1" };

static void tegra20_super_clk_init(void)
{
	struct clk *clk;

	/* CCLK */
	clk = tegra_clk_register_super_mux("cclk", cclk_parents,
			      ARRAY_SIZE(cclk_parents), CLK_SET_RATE_PARENT,
			      clk_base + CCLK_BURST_POLICY, 0, 4, 0, 0, NULL);
	clks[TEGRA20_CLK_CCLK] = clk;

	/* SCLK */
	clk = tegra_clk_register_super_mux("sclk", sclk_parents,
			      ARRAY_SIZE(sclk_parents), CLK_SET_RATE_PARENT,
			      clk_base + SCLK_BURST_POLICY, 0, 4, 0, 0, NULL);
	clks[TEGRA20_CLK_SCLK] = clk;

	/* twd */
	clk = clk_register_fixed_factor(NULL, "twd", "cclk", 0, 1, 4);
	clks[TEGRA20_CLK_TWD] = clk;
}

static const char *audio_parents[] = {"spdif_in", "i2s1", "i2s2", "unused",
				      "pll_a_out0", "unused", "unused",
				      "unused"};

static void __init tegra20_audio_clk_init(void)
{
	struct clk *clk;

	/* audio */
	clk = clk_register_mux(NULL, "audio_mux", audio_parents,
				ARRAY_SIZE(audio_parents),
				CLK_SET_RATE_NO_REPARENT,
				clk_base + AUDIO_SYNC_CLK, 0, 3, 0, NULL);
	clk = clk_register_gate(NULL, "audio", "audio_mux", 0,
				clk_base + AUDIO_SYNC_CLK, 4,
				CLK_GATE_SET_TO_DISABLE, NULL);
	clks[TEGRA20_CLK_AUDIO] = clk;

	/* audio_2x */
	clk = clk_register_fixed_factor(NULL, "audio_doubler", "audio",
					CLK_SET_RATE_PARENT, 2, 1);
	clk = tegra_clk_register_periph_gate("audio_2x", "audio_doubler",
				    TEGRA_PERIPH_NO_RESET, clk_base,
				    CLK_SET_RATE_PARENT, 89,
				    periph_clk_enb_refcnt);
	clks[TEGRA20_CLK_AUDIO_2X] = clk;

}

static const char *i2s1_parents[] = {"pll_a_out0", "audio_2x", "pll_p",
				     "clk_m"};
static const char *i2s2_parents[] = {"pll_a_out0", "audio_2x", "pll_p",
				     "clk_m"};
static const char *pwm_parents[] = {"pll_p", "pll_c", "audio", "clk_m",
				    "clk_32k"};
static const char *mux_pllpcm_clkm[] = {"pll_p", "pll_c", "pll_m", "clk_m"};
static const char *mux_pllpdc_clkm[] = {"pll_p", "pll_d_out0", "pll_c",
					"clk_m"};
static const char *mux_pllmcp_clkm[] = {"pll_m", "pll_c", "pll_p", "clk_m"};

static struct tegra_periph_init_data tegra_periph_clk_list[] = {
	TEGRA_INIT_DATA_MUX("i2s1", i2s1_parents,     CLK_SOURCE_I2S1,   11, TEGRA_PERIPH_ON_APB, TEGRA20_CLK_I2S1),
	TEGRA_INIT_DATA_MUX("i2s2", i2s2_parents,     CLK_SOURCE_I2S2,   18, TEGRA_PERIPH_ON_APB, TEGRA20_CLK_I2S2),
	TEGRA_INIT_DATA_MUX("spi",   mux_pllpcm_clkm,   CLK_SOURCE_SPI,   43, TEGRA_PERIPH_ON_APB, TEGRA20_CLK_SPI),
	TEGRA_INIT_DATA_MUX("xio",   mux_pllpcm_clkm,   CLK_SOURCE_XIO,   45, 0, TEGRA20_CLK_XIO),
	TEGRA_INIT_DATA_MUX("twc",   mux_pllpcm_clkm,   CLK_SOURCE_TWC,   16, TEGRA_PERIPH_ON_APB, TEGRA20_CLK_TWC),
	TEGRA_INIT_DATA_MUX("ide",   mux_pllpcm_clkm,   CLK_SOURCE_XIO,   25, 0, TEGRA20_CLK_IDE),
	TEGRA_INIT_DATA_DIV16("dvc", mux_pllpcm_clkm,   CLK_SOURCE_DVC,   47, TEGRA_PERIPH_ON_APB, TEGRA20_CLK_DVC),
	TEGRA_INIT_DATA_DIV16("i2c1", mux_pllpcm_clkm,   CLK_SOURCE_I2C1,   12, TEGRA_PERIPH_ON_APB, TEGRA20_CLK_I2C1),
	TEGRA_INIT_DATA_DIV16("i2c2", mux_pllpcm_clkm,   CLK_SOURCE_I2C2,   54, TEGRA_PERIPH_ON_APB, TEGRA20_CLK_I2C2),
	TEGRA_INIT_DATA_DIV16("i2c3", mux_pllpcm_clkm,   CLK_SOURCE_I2C3,   67, TEGRA_PERIPH_ON_APB, TEGRA20_CLK_I2C3),
	TEGRA_INIT_DATA_MUX("hdmi", mux_pllpdc_clkm,   CLK_SOURCE_HDMI,   51, 0, TEGRA20_CLK_HDMI),
	TEGRA_INIT_DATA("pwm", NULL, NULL, pwm_parents,     CLK_SOURCE_PWM,   28, 3, 0, 0, 8, 1, 0, 17, TEGRA_PERIPH_ON_APB, TEGRA20_CLK_PWM),
};

static struct tegra_periph_init_data tegra_periph_nodiv_clk_list[] = {
	TEGRA_INIT_DATA_NODIV("uarta",	mux_pllpcm_clkm, CLK_SOURCE_UARTA, 30, 2, 6,   TEGRA_PERIPH_ON_APB, TEGRA20_CLK_UARTA),
	TEGRA_INIT_DATA_NODIV("uartb",	mux_pllpcm_clkm, CLK_SOURCE_UARTB, 30, 2, 7,   TEGRA_PERIPH_ON_APB, TEGRA20_CLK_UARTB),
	TEGRA_INIT_DATA_NODIV("uartc",	mux_pllpcm_clkm, CLK_SOURCE_UARTC, 30, 2, 55,  TEGRA_PERIPH_ON_APB, TEGRA20_CLK_UARTC),
	TEGRA_INIT_DATA_NODIV("uartd",	mux_pllpcm_clkm, CLK_SOURCE_UARTD, 30, 2, 65,  TEGRA_PERIPH_ON_APB, TEGRA20_CLK_UARTD),
	TEGRA_INIT_DATA_NODIV("uarte",	mux_pllpcm_clkm, CLK_SOURCE_UARTE, 30, 2, 66,  TEGRA_PERIPH_ON_APB, TEGRA20_CLK_UARTE),
	TEGRA_INIT_DATA_NODIV("disp1",	mux_pllpdc_clkm, CLK_SOURCE_DISP1, 30, 2, 27,  0, TEGRA20_CLK_DISP1),
	TEGRA_INIT_DATA_NODIV("disp2",	mux_pllpdc_clkm, CLK_SOURCE_DISP2, 30, 2, 26,  0, TEGRA20_CLK_DISP2),
};

static void __init tegra20_periph_clk_init(void)
{
	struct tegra_periph_init_data *data;
	struct clk *clk;
	int i;

	/* ac97 */
	clk = tegra_clk_register_periph_gate("ac97", "pll_a_out0",
				    TEGRA_PERIPH_ON_APB,
				    clk_base, 0, 3, periph_clk_enb_refcnt);
	clks[TEGRA20_CLK_AC97] = clk;

	/* apbdma */
	clk = tegra_clk_register_periph_gate("apbdma", "pclk", 0, clk_base,
				    0, 34, periph_clk_enb_refcnt);
	clks[TEGRA20_CLK_APBDMA] = clk;

	/* emc */
	clk = clk_register_mux(NULL, "emc_mux", mux_pllmcp_clkm,
			       ARRAY_SIZE(mux_pllmcp_clkm),
			       CLK_SET_RATE_NO_REPARENT,
			       clk_base + CLK_SOURCE_EMC,
			       30, 2, 0, &emc_lock);
	clk = tegra_clk_register_periph_gate("emc", "emc_mux", 0, clk_base, 0,
				    57, periph_clk_enb_refcnt);
	clks[TEGRA20_CLK_EMC] = clk;

	clk = tegra_clk_register_mc("mc", "emc_mux", clk_base + CLK_SOURCE_EMC,
				    &emc_lock);
	clks[TEGRA20_CLK_MC] = clk;

	/* dsi */
	clk = tegra_clk_register_periph_gate("dsi", "pll_d", 0, clk_base, 0,
				    48, periph_clk_enb_refcnt);
	clk_register_clkdev(clk, NULL, "dsi");
	clks[TEGRA20_CLK_DSI] = clk;

	/* pex */
	clk = tegra_clk_register_periph_gate("pex", "clk_m", 0, clk_base, 0, 70,
				    periph_clk_enb_refcnt);
	clks[TEGRA20_CLK_PEX] = clk;

	/* cdev1 */
	clk = clk_register_fixed_rate(NULL, "cdev1_fixed", NULL, CLK_IS_ROOT,
				      26000000);
	clk = tegra_clk_register_periph_gate("cdev1", "cdev1_fixed", 0,
				    clk_base, 0, 94, periph_clk_enb_refcnt);
	clks[TEGRA20_CLK_CDEV1] = clk;

	/* cdev2 */
	clk = clk_register_fixed_rate(NULL, "cdev2_fixed", NULL, CLK_IS_ROOT,
				      26000000);
	clk = tegra_clk_register_periph_gate("cdev2", "cdev2_fixed", 0,
				    clk_base, 0, 93, periph_clk_enb_refcnt);
	clks[TEGRA20_CLK_CDEV2] = clk;

	for (i = 0; i < ARRAY_SIZE(tegra_periph_clk_list); i++) {
		data = &tegra_periph_clk_list[i];
		clk = tegra_clk_register_periph(data->name, data->p.parent_names,
				data->num_parents, &data->periph,
				clk_base, data->offset, data->flags);
		clks[data->clk_id] = clk;
	}

	for (i = 0; i < ARRAY_SIZE(tegra_periph_nodiv_clk_list); i++) {
		data = &tegra_periph_nodiv_clk_list[i];
		clk = tegra_clk_register_periph_nodiv(data->name,
					data->p.parent_names,
					data->num_parents, &data->periph,
					clk_base, data->offset);
		clks[data->clk_id] = clk;
	}

	tegra_periph_clk_init(clk_base, pmc_base, tegra20_clks, &pll_p_params);
}

static void __init tegra20_osc_clk_init(void)
{
	struct clk *clk;
	unsigned long input_freq;
	unsigned int pll_ref_div;

	input_freq = tegra20_clk_measure_input_freq();

	/* clk_m */
	clk = clk_register_fixed_rate(NULL, "clk_m", NULL, CLK_IS_ROOT |
				      CLK_IGNORE_UNUSED, input_freq);
	clks[TEGRA20_CLK_CLK_M] = clk;

	/* pll_ref */
	pll_ref_div = tegra20_get_pll_ref_div();
	clk = clk_register_fixed_factor(NULL, "pll_ref", "clk_m",
					CLK_SET_RATE_PARENT, 1, pll_ref_div);
	clks[TEGRA20_CLK_PLL_REF] = clk;
}

/* Tegra20 CPU clock and reset control functions */
static void tegra20_wait_cpu_in_reset(u32 cpu)
{
	unsigned int reg;

	do {
		reg = readl(clk_base +
			    TEGRA_CLK_RST_CONTROLLER_RST_CPU_CMPLX_SET);
		cpu_relax();
	} while (!(reg & (1 << cpu)));	/* check CPU been reset or not */

	return;
}

static void tegra20_put_cpu_in_reset(u32 cpu)
{
	writel(CPU_RESET(cpu),
	       clk_base + TEGRA_CLK_RST_CONTROLLER_RST_CPU_CMPLX_SET);
	dmb();
}

static void tegra20_cpu_out_of_reset(u32 cpu)
{
	writel(CPU_RESET(cpu),
	       clk_base + TEGRA_CLK_RST_CONTROLLER_RST_CPU_CMPLX_CLR);
	wmb();
}

static void tegra20_enable_cpu_clock(u32 cpu)
{
	unsigned int reg;

	reg = readl(clk_base + TEGRA_CLK_RST_CONTROLLER_CLK_CPU_CMPLX);
	writel(reg & ~CPU_CLOCK(cpu),
	       clk_base + TEGRA_CLK_RST_CONTROLLER_CLK_CPU_CMPLX);
	barrier();
	reg = readl(clk_base + TEGRA_CLK_RST_CONTROLLER_CLK_CPU_CMPLX);
}

static void tegra20_disable_cpu_clock(u32 cpu)
{
	unsigned int reg;

	reg = readl(clk_base + TEGRA_CLK_RST_CONTROLLER_CLK_CPU_CMPLX);
	writel(reg | CPU_CLOCK(cpu),
	       clk_base + TEGRA_CLK_RST_CONTROLLER_CLK_CPU_CMPLX);
}

#ifdef CONFIG_PM_SLEEP
static bool tegra20_cpu_rail_off_ready(void)
{
	unsigned int cpu_rst_status;

	cpu_rst_status = readl(clk_base +
			       TEGRA_CLK_RST_CONTROLLER_RST_CPU_CMPLX_SET);

	return !!(cpu_rst_status & 0x2);
}

static void tegra20_cpu_clock_suspend(void)
{
	/* switch coresite to clk_m, save off original source */
	tegra20_cpu_clk_sctx.clk_csite_src =
				readl(clk_base + CLK_SOURCE_CSITE);
	writel(3<<30, clk_base + CLK_SOURCE_CSITE);

	tegra20_cpu_clk_sctx.cpu_burst =
				readl(clk_base + CCLK_BURST_POLICY);
	tegra20_cpu_clk_sctx.pllx_base =
				readl(clk_base + PLLX_BASE);
	tegra20_cpu_clk_sctx.pllx_misc =
				readl(clk_base + PLLX_MISC);
	tegra20_cpu_clk_sctx.cclk_divider =
				readl(clk_base + SUPER_CCLK_DIVIDER);
}

static void tegra20_cpu_clock_resume(void)
{
	unsigned int reg, policy;

	/* Is CPU complex already running on PLLX? */
	reg = readl(clk_base + CCLK_BURST_POLICY);
	policy = (reg >> CCLK_BURST_POLICY_SHIFT) & 0xF;

	if (policy == CCLK_IDLE_POLICY)
		reg = (reg >> CCLK_IDLE_POLICY_SHIFT) & 0xF;
	else if (policy == CCLK_RUN_POLICY)
		reg = (reg >> CCLK_RUN_POLICY_SHIFT) & 0xF;
	else
		BUG();

	if (reg != CCLK_BURST_POLICY_PLLX) {
		/* restore PLLX settings if CPU is on different PLL */
		writel(tegra20_cpu_clk_sctx.pllx_misc,
					clk_base + PLLX_MISC);
		writel(tegra20_cpu_clk_sctx.pllx_base,
					clk_base + PLLX_BASE);

		/* wait for PLL stabilization if PLLX was enabled */
		if (tegra20_cpu_clk_sctx.pllx_base & (1 << 30))
			udelay(300);
	}

	/*
	 * Restore original burst policy setting for calls resulting from CPU
	 * LP2 in idle or system suspend.
	 */
	writel(tegra20_cpu_clk_sctx.cclk_divider,
					clk_base + SUPER_CCLK_DIVIDER);
	writel(tegra20_cpu_clk_sctx.cpu_burst,
					clk_base + CCLK_BURST_POLICY);

	writel(tegra20_cpu_clk_sctx.clk_csite_src,
					clk_base + CLK_SOURCE_CSITE);
}
#endif

static struct tegra_cpu_car_ops tegra20_cpu_car_ops = {
	.wait_for_reset	= tegra20_wait_cpu_in_reset,
	.put_in_reset	= tegra20_put_cpu_in_reset,
	.out_of_reset	= tegra20_cpu_out_of_reset,
	.enable_clock	= tegra20_enable_cpu_clock,
	.disable_clock	= tegra20_disable_cpu_clock,
#ifdef CONFIG_PM_SLEEP
	.rail_off_ready = tegra20_cpu_rail_off_ready,
	.suspend	= tegra20_cpu_clock_suspend,
	.resume		= tegra20_cpu_clock_resume,
#endif
};

static struct tegra_clk_init_table init_table[] __initdata = {
	{TEGRA20_CLK_PLL_P, TEGRA20_CLK_CLK_MAX, 216000000, 1},
	{TEGRA20_CLK_PLL_P_OUT1, TEGRA20_CLK_CLK_MAX, 28800000, 1},
	{TEGRA20_CLK_PLL_P_OUT2, TEGRA20_CLK_CLK_MAX, 48000000, 1},
	{TEGRA20_CLK_PLL_P_OUT3, TEGRA20_CLK_CLK_MAX, 72000000, 1},
	{TEGRA20_CLK_PLL_P_OUT4, TEGRA20_CLK_CLK_MAX, 24000000, 1},
	{TEGRA20_CLK_PLL_C, TEGRA20_CLK_CLK_MAX, 600000000, 1},
	{TEGRA20_CLK_PLL_C_OUT1, TEGRA20_CLK_CLK_MAX, 120000000, 1},
	{TEGRA20_CLK_SCLK, TEGRA20_CLK_PLL_C_OUT1, 0, 1},
	{TEGRA20_CLK_HCLK, TEGRA20_CLK_CLK_MAX, 0, 1},
	{TEGRA20_CLK_PCLK, TEGRA20_CLK_CLK_MAX, 60000000, 1},
	{TEGRA20_CLK_CSITE, TEGRA20_CLK_CLK_MAX, 0, 1},
	{TEGRA20_CLK_EMC, TEGRA20_CLK_CLK_MAX, 0, 1},
	{TEGRA20_CLK_CCLK, TEGRA20_CLK_CLK_MAX, 0, 1},
	{TEGRA20_CLK_UARTA, TEGRA20_CLK_PLL_P, 0, 0},
	{TEGRA20_CLK_UARTB, TEGRA20_CLK_PLL_P, 0, 0},
	{TEGRA20_CLK_UARTC, TEGRA20_CLK_PLL_P, 0, 0},
	{TEGRA20_CLK_UARTD, TEGRA20_CLK_PLL_P, 0, 0},
	{TEGRA20_CLK_UARTE, TEGRA20_CLK_PLL_P, 0, 0},
	{TEGRA20_CLK_PLL_A, TEGRA20_CLK_CLK_MAX, 56448000, 1},
	{TEGRA20_CLK_PLL_A_OUT0, TEGRA20_CLK_CLK_MAX, 11289600, 1},
	{TEGRA20_CLK_CDEV1, TEGRA20_CLK_CLK_MAX, 0, 1},
	{TEGRA20_CLK_BLINK, TEGRA20_CLK_CLK_MAX, 32768, 1},
	{TEGRA20_CLK_I2S1, TEGRA20_CLK_PLL_A_OUT0, 11289600, 0},
	{TEGRA20_CLK_I2S2, TEGRA20_CLK_PLL_A_OUT0, 11289600, 0},
	{TEGRA20_CLK_SDMMC1, TEGRA20_CLK_PLL_P, 48000000, 0},
	{TEGRA20_CLK_SDMMC3, TEGRA20_CLK_PLL_P, 48000000, 0},
	{TEGRA20_CLK_SDMMC4, TEGRA20_CLK_PLL_P, 48000000, 0},
	{TEGRA20_CLK_SPI, TEGRA20_CLK_PLL_P, 20000000, 0},
	{TEGRA20_CLK_SBC1, TEGRA20_CLK_PLL_P, 100000000, 0},
	{TEGRA20_CLK_SBC2, TEGRA20_CLK_PLL_P, 100000000, 0},
	{TEGRA20_CLK_SBC3, TEGRA20_CLK_PLL_P, 100000000, 0},
	{TEGRA20_CLK_SBC4, TEGRA20_CLK_PLL_P, 100000000, 0},
	{TEGRA20_CLK_HOST1X, TEGRA20_CLK_PLL_C, 150000000, 0},
	{TEGRA20_CLK_DISP1, TEGRA20_CLK_PLL_P, 600000000, 0},
	{TEGRA20_CLK_DISP2, TEGRA20_CLK_PLL_P, 600000000, 0},
	{TEGRA20_CLK_GR2D, TEGRA20_CLK_PLL_C, 300000000, 0},
	{TEGRA20_CLK_GR3D, TEGRA20_CLK_PLL_C, 300000000, 0},
	{TEGRA20_CLK_CLK_MAX, TEGRA20_CLK_CLK_MAX, 0, 0}, /* This MUST be the last entry */
};

static void __init tegra20_clock_apply_init_table(void)
{
	tegra_init_from_table(init_table, clks, TEGRA20_CLK_CLK_MAX);
}

/*
 * Some clocks may be used by different drivers depending on the board
 * configuration.  List those here to register them twice in the clock lookup
 * table under two names.
 */
static struct tegra_clk_duplicate tegra_clk_duplicates[] = {
	TEGRA_CLK_DUPLICATE(TEGRA20_CLK_USBD,   "utmip-pad",    NULL),
	TEGRA_CLK_DUPLICATE(TEGRA20_CLK_USBD,   "tegra-ehci.0", NULL),
	TEGRA_CLK_DUPLICATE(TEGRA20_CLK_USBD,   "tegra-otg",    NULL),
	TEGRA_CLK_DUPLICATE(TEGRA20_CLK_CCLK,   NULL,           "cpu"),
	TEGRA_CLK_DUPLICATE(TEGRA20_CLK_CLK_MAX, NULL, NULL), /* Must be the last entry */
};

static const struct of_device_id pmc_match[] __initconst = {
	{ .compatible = "nvidia,tegra20-pmc" },
	{},
};

static void __init tegra20_clock_init(struct device_node *np)
{
	struct device_node *node;

	clk_base = of_iomap(np, 0);
	if (!clk_base) {
		pr_err("Can't map CAR registers\n");
		BUG();
	}

	node = of_find_matching_node(NULL, pmc_match);
	if (!node) {
		pr_err("Failed to find pmc node\n");
		BUG();
	}

	pmc_base = of_iomap(node, 0);
	if (!pmc_base) {
		pr_err("Can't map pmc registers\n");
		BUG();
	}

	clks = tegra_clk_init(clk_base, TEGRA20_CLK_CLK_MAX,
				TEGRA20_CLK_PERIPH_BANKS);
	if (!clks)
		return;

	tegra20_osc_clk_init();
	tegra_fixed_clk_init(tegra20_clks);
	tegra20_pll_init();
	tegra20_super_clk_init();
	tegra_super_clk_gen4_init(clk_base, pmc_base, tegra20_clks, NULL);
	tegra20_periph_clk_init();
	tegra20_audio_clk_init();
	tegra_pmc_clk_init(pmc_base, tegra20_clks);

	tegra_init_dup_clks(tegra_clk_duplicates, clks, TEGRA20_CLK_CLK_MAX);

	tegra_add_of_provider(np);
	tegra_register_devclks(devclks, ARRAY_SIZE(devclks));

	tegra_clk_apply_init_table = tegra20_clock_apply_init_table;

	tegra_cpu_car_ops = &tegra20_cpu_car_ops;
}
CLK_OF_DECLARE(tegra20, "nvidia,tegra20-car", tegra20_clock_init);
