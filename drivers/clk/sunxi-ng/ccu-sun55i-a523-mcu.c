// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Chen-Yu Tsai <wens@csie.org>
 *
 * Based on the A523 CCU driver:
 *   Copyright (C) 2023-2024 Arm Ltd.
 */

#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <dt-bindings/clock/sun55i-a523-mcu-ccu.h>
#include <dt-bindings/reset/sun55i-a523-mcu-ccu.h>

#include "ccu_common.h"
#include "ccu_reset.h"

#include "ccu_div.h"
#include "ccu_gate.h"
#include "ccu_mp.h"
#include "ccu_mult.h"
#include "ccu_nm.h"

static const struct clk_parent_data osc24M[] = {
	{ .fw_name = "hosc" }
};

static const struct clk_parent_data ahb[] = {
	{ .fw_name = "r-ahb" }
};

static const struct clk_parent_data apb[] = {
	{ .fw_name = "r-apb0" }
};

#define SUN55I_A523_PLL_AUDIO1_REG	0x00c
static struct ccu_sdm_setting pll_audio1_sdm_table[] = {
	{ .rate = 2167603200, .pattern = 0xa000a234, .m = 1, .n = 90 }, /* div2->22.5792 */
	{ .rate = 2359296000, .pattern = 0xa0009ba6, .m = 1, .n = 98 }, /* div2->24.576 */
	{ .rate = 1806336000, .pattern = 0xa000872b, .m = 1, .n = 75 }, /* div5->22.576 */
};

static struct ccu_nm pll_audio1_clk = {
	.enable		= BIT(27),
	.lock		= BIT(28),
	.n		= _SUNXI_CCU_MULT_MIN(8, 8, 11),
	.m		= _SUNXI_CCU_DIV(1, 1),
	.sdm		= _SUNXI_CCU_SDM(pll_audio1_sdm_table, BIT(24),
					 0x010, BIT(31)),
	.min_rate	= 180000000U,
	.max_rate	= 3500000000U,
	.common		= {
		.reg		= 0x00c,
		.features	= CCU_FEATURE_SIGMA_DELTA_MOD,
		.hw.init	= CLK_HW_INIT_PARENTS_DATA("pll-audio1",
							   osc24M, &ccu_nm_ops,
							   CLK_SET_RATE_GATE),
	},
};

/*
 * /2 and /5 dividers are actually programmable, but we just use the
 * values from the BSP, since the audio PLL only needs to provide a
 * couple clock rates. This also matches the names given in the manual.
 */
static const struct clk_hw *pll_audio1_div_parents[] = { &pll_audio1_clk.common.hw };
static CLK_FIXED_FACTOR_HWS(pll_audio1_div2_clk, "pll-audio1-div2",
			    pll_audio1_div_parents, 2, 1,
			    CLK_SET_RATE_PARENT);
static CLK_FIXED_FACTOR_HWS(pll_audio1_div5_clk, "pll-audio1-div5",
			    pll_audio1_div_parents, 5, 1,
			    CLK_SET_RATE_PARENT);

static SUNXI_CCU_M_WITH_GATE(audio_out_clk, "audio-out",
			     "pll-audio1-div2", 0x01c,
			     0, 5, BIT(31), CLK_SET_RATE_PARENT);

static const struct clk_parent_data dsp_parents[] = {
	{ .fw_name = "hosc" },
	{ .fw_name = "losc" },
	{ .fw_name = "iosc" },
	/*
	 * The order of the following two parent is from the BSP code. It is
	 * the opposite in the manual. Testing with the DSP is required to
	 * figure out the real order.
	 */
	{ .hw = &pll_audio1_div5_clk.hw },
	{ .hw = &pll_audio1_div2_clk.hw },
	{ .fw_name = "dsp" },
};
static SUNXI_CCU_M_DATA_WITH_MUX_GATE(dsp_clk, "mcu-dsp", dsp_parents, 0x0020,
				      0, 5,	/* M */
				      24, 3,	/* mux */
				      BIT(31),	/* gate */
				      0);

static const struct clk_parent_data i2s_parents[] = {
	{ .fw_name = "pll-audio0-4x" },
	{ .hw = &pll_audio1_div2_clk.hw },
	{ .hw = &pll_audio1_div5_clk.hw },
};

static SUNXI_CCU_DUALDIV_MUX_GATE(i2s0_clk, "i2s0", i2s_parents, 0x02c,
				  0, 5,		/* M */
				  5, 5,		/* P */
				  24, 3,	/* mux */
				  BIT(31),	/* gate */
				  CLK_SET_RATE_PARENT);
static SUNXI_CCU_DUALDIV_MUX_GATE(i2s1_clk, "i2s1", i2s_parents, 0x030,
				  0, 5,		/* M */
				  5, 5,		/* P */
				  24, 3,	/* mux */
				  BIT(31),	/* gate */
				  CLK_SET_RATE_PARENT);
static SUNXI_CCU_DUALDIV_MUX_GATE(i2s2_clk, "i2s2", i2s_parents, 0x034,
				  0, 5,		/* M */
				  5, 5,		/* P */
				  24, 3,	/* mux */
				  BIT(31),	/* gate */
				  CLK_SET_RATE_PARENT);
static SUNXI_CCU_DUALDIV_MUX_GATE(i2s3_clk, "i2s3", i2s_parents, 0x038,
				  0, 5,		/* M */
				  5, 5,		/* P */
				  24, 3,	/* mux */
				  BIT(31),	/* gate */
				  CLK_SET_RATE_PARENT);

static const struct clk_parent_data i2s3_asrc_parents[] = {
	{ .fw_name = "pll-periph0-300m" },
	{ .hw = &pll_audio1_div2_clk.hw },
	{ .hw = &pll_audio1_div5_clk.hw },
};
static SUNXI_CCU_DUALDIV_MUX_GATE(i2s3_asrc_clk, "i2s3-asrc",
				  i2s3_asrc_parents, 0x03c,
				  0, 5,		/* M */
				  5, 5,		/* P */
				  24, 3,	/* mux */
				  BIT(31),	/* gate */
				  CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE_DATA(bus_i2s0_clk, "bus-i2s0", apb, 0x040, BIT(0), 0);
static SUNXI_CCU_GATE_DATA(bus_i2s1_clk, "bus-i2s1", apb, 0x040, BIT(1), 0);
static SUNXI_CCU_GATE_DATA(bus_i2s2_clk, "bus-i2s2", apb, 0x040, BIT(2), 0);
static SUNXI_CCU_GATE_DATA(bus_i2s3_clk, "bus-i2s3", apb, 0x040, BIT(3), 0);

static const struct clk_parent_data audio_parents[] = {
	{ .fw_name = "pll-audio0-4x" },
	{ .hw = &pll_audio1_div2_clk.hw },
	{ .hw = &pll_audio1_div5_clk.hw },
};
static SUNXI_CCU_DUALDIV_MUX_GATE(spdif_tx_clk, "spdif-tx",
				  audio_parents, 0x044,
				  0, 5,		/* M */
				  5, 5,		/* P */
				  24, 3,	/* mux */
				  BIT(31),	/* gate */
				  CLK_SET_RATE_PARENT);
static SUNXI_CCU_DUALDIV_MUX_GATE(spdif_rx_clk, "spdif-rx",
				  i2s3_asrc_parents, 0x048,
				  0, 5,		/* M */
				  5, 5,		/* P */
				  24, 3,	/* mux */
				  BIT(31),	/* gate */
				  CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE_DATA(bus_spdif_clk, "bus-spdif", apb, 0x04c, BIT(0), 0);

static SUNXI_CCU_DUALDIV_MUX_GATE(dmic_clk, "dmic", audio_parents, 0x050,
				  0, 5,		/* M */
				  5, 5,		/* P */
				  24, 3,	/* mux */
				  BIT(31),	/* gate */
				  CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE_DATA(bus_dmic_clk, "bus-dmic", apb, 0x054, BIT(0), 0);

static SUNXI_CCU_DUALDIV_MUX_GATE(audio_dac_clk, "audio-dac",
				  audio_parents, 0x058,
				  0, 5,		/* M */
				  5, 5,		/* P */
				  24, 3,	/* mux */
				  BIT(31),	/* gate */
				  CLK_SET_RATE_PARENT);
static SUNXI_CCU_DUALDIV_MUX_GATE(audio_adc_clk, "audio-adc",
				  audio_parents, 0x05c,
				  0, 5,		/* M */
				  5, 5,		/* P */
				  24, 3,	/* mux */
				  BIT(31),	/* gate */
				  CLK_SET_RATE_PARENT);

static SUNXI_CCU_GATE_DATA(bus_audio_codec_clk, "bus-audio-codec",
			   apb, 0x060, BIT(0), 0);

static SUNXI_CCU_GATE_DATA(bus_dsp_msgbox_clk, "bus-dsp-msgbox",
			   ahb, 0x068, BIT(0), 0);
static SUNXI_CCU_GATE_DATA(bus_dsp_cfg_clk, "bus-dsp-cfg",
			   apb, 0x06c, BIT(0), 0);

static SUNXI_CCU_GATE_DATA(bus_npu_hclk, "bus-npu-hclk", ahb, 0x070, BIT(1), 0);
static SUNXI_CCU_GATE_DATA(bus_npu_aclk, "bus-npu-aclk", ahb, 0x070, BIT(2), 0);

static const struct clk_parent_data timer_parents[] = {
	{ .fw_name = "hosc" },
	{ .fw_name = "losc" },
	{ .fw_name = "iosc" },
	{ .fw_name = "r-ahb" }
};
static SUNXI_CCU_P_DATA_WITH_MUX_GATE(mcu_timer0_clk, "mcu-timer0", timer_parents,
				      0x074,
				      1, 3,	/* P */
				      4, 2,	/* mux */
				      BIT(0),	/* gate */
				      0);
static SUNXI_CCU_P_DATA_WITH_MUX_GATE(mcu_timer1_clk, "mcu-timer1", timer_parents,
				      0x078,
				      1, 3,	/* P */
				      4, 2,	/* mux */
				      BIT(0),	/* gate */
				      0);
static SUNXI_CCU_P_DATA_WITH_MUX_GATE(mcu_timer2_clk, "mcu-timer2", timer_parents,
				      0x07c,
				      1, 3,	/* P */
				      4, 2,	/* mux */
				      BIT(0),	/* gate */
				      0);
static SUNXI_CCU_P_DATA_WITH_MUX_GATE(mcu_timer3_clk, "mcu-timer3", timer_parents,
				      0x080,
				      1, 3,	/* P */
				      4, 2,	/* mux */
				      BIT(0),	/* gate */
				      0);
static SUNXI_CCU_P_DATA_WITH_MUX_GATE(mcu_timer4_clk, "mcu-timer4", timer_parents,
				      0x084,
				      1, 3,	/* P */
				      4, 2,	/* mux */
				      BIT(0),	/* gate */
				      0);
static SUNXI_CCU_P_DATA_WITH_MUX_GATE(mcu_timer5_clk, "mcu-timer5", timer_parents,
				      0x088,
				      1, 3,	/* P */
				      4, 2,	/* mux */
				      BIT(0),	/* gate */
				      0);
static SUNXI_CCU_GATE_DATA(bus_mcu_timer_clk, "bus-mcu-timer", ahb, 0x08c, BIT(0), 0);
static SUNXI_CCU_GATE_DATA(bus_mcu_dma_clk, "bus-mcu-dma", ahb, 0x104, BIT(0), 0);
/* tzma* only found in BSP code. */
static SUNXI_CCU_GATE_DATA(tzma0_clk, "tzma0", ahb, 0x108, BIT(0), 0);
static SUNXI_CCU_GATE_DATA(tzma1_clk, "tzma1", ahb, 0x10c, BIT(0), 0);
/* parent is a guess as this block is not shown in the system bus tree diagram */
static SUNXI_CCU_GATE_DATA(bus_pubsram_clk, "bus-pubsram", ahb, 0x114, BIT(0), 0);

/*
 * user manual has "mbus" clock as parent of both clocks below,
 * but this makes more sense, since BSP MCU DMA controller has
 * reference to both of them, likely needing both enabled.
 */
static SUNXI_CCU_GATE_FW(mbus_mcu_clk, "mbus-mcu", "mbus", 0x11c, BIT(1), 0);
static SUNXI_CCU_GATE_HW(mbus_mcu_dma_clk, "mbus-mcu-dma",
			 &mbus_mcu_clk.common.hw, 0x11c, BIT(0), 0);

static const struct clk_parent_data riscv_pwm_parents[] = {
	{ .fw_name = "hosc" },
	{ .fw_name = "losc" },
	{ .fw_name = "iosc" },
};

static SUNXI_CCU_MUX_DATA_WITH_GATE(riscv_clk, "riscv",
				    riscv_pwm_parents, 0x120,
				    27, 3, BIT(31), 0);
/* Parents are guesses as these two blocks are not shown in the system bus tree diagram */
static SUNXI_CCU_GATE_DATA(bus_riscv_cfg_clk, "bus-riscv-cfg", ahb,
			   0x124, BIT(0), 0);
static SUNXI_CCU_GATE_DATA(bus_riscv_msgbox_clk, "bus-riscv-msgbox", ahb,
			   0x128, BIT(0), 0);

static SUNXI_CCU_MUX_DATA_WITH_GATE(mcu_pwm0_clk, "mcu-pwm0",
				    riscv_pwm_parents, 0x130,
				    24, 3, BIT(31), 0);
static SUNXI_CCU_GATE_DATA(bus_mcu_pwm0_clk, "bus-mcu-pwm0", apb,
			   0x134, BIT(0), 0);

/*
 * Contains all clocks that are controlled by a hardware register. They
 * have a (sunxi) .common member, which needs to be initialised by the common
 * sunxi CCU code, to be filled with the MMIO base address and the shared lock.
 */
static struct ccu_common *sun55i_a523_mcu_ccu_clks[] = {
	&pll_audio1_clk.common,
	&audio_out_clk.common,
	&dsp_clk.common,
	&i2s0_clk.common,
	&i2s1_clk.common,
	&i2s2_clk.common,
	&i2s3_clk.common,
	&i2s3_asrc_clk.common,
	&bus_i2s0_clk.common,
	&bus_i2s1_clk.common,
	&bus_i2s2_clk.common,
	&bus_i2s3_clk.common,
	&spdif_tx_clk.common,
	&spdif_rx_clk.common,
	&bus_spdif_clk.common,
	&dmic_clk.common,
	&bus_dmic_clk.common,
	&audio_dac_clk.common,
	&audio_adc_clk.common,
	&bus_audio_codec_clk.common,
	&bus_dsp_msgbox_clk.common,
	&bus_dsp_cfg_clk.common,
	&bus_npu_aclk.common,
	&bus_npu_hclk.common,
	&mcu_timer0_clk.common,
	&mcu_timer1_clk.common,
	&mcu_timer2_clk.common,
	&mcu_timer3_clk.common,
	&mcu_timer4_clk.common,
	&mcu_timer5_clk.common,
	&bus_mcu_timer_clk.common,
	&bus_mcu_dma_clk.common,
	&tzma0_clk.common,
	&tzma1_clk.common,
	&bus_pubsram_clk.common,
	&mbus_mcu_dma_clk.common,
	&mbus_mcu_clk.common,
	&riscv_clk.common,
	&bus_riscv_cfg_clk.common,
	&bus_riscv_msgbox_clk.common,
	&mcu_pwm0_clk.common,
	&bus_mcu_pwm0_clk.common,
};

static struct clk_hw_onecell_data sun55i_a523_mcu_hw_clks = {
	.hws	= {
		[CLK_MCU_PLL_AUDIO1]		= &pll_audio1_clk.common.hw,
		[CLK_MCU_PLL_AUDIO1_DIV2]	= &pll_audio1_div2_clk.hw,
		[CLK_MCU_PLL_AUDIO1_DIV5]	= &pll_audio1_div5_clk.hw,
		[CLK_MCU_AUDIO_OUT]		= &audio_out_clk.common.hw,
		[CLK_MCU_DSP]			= &dsp_clk.common.hw,
		[CLK_MCU_I2S0]			= &i2s0_clk.common.hw,
		[CLK_MCU_I2S1]			= &i2s1_clk.common.hw,
		[CLK_MCU_I2S2]			= &i2s2_clk.common.hw,
		[CLK_MCU_I2S3]			= &i2s3_clk.common.hw,
		[CLK_MCU_I2S3_ASRC]		= &i2s3_asrc_clk.common.hw,
		[CLK_BUS_MCU_I2S0]		= &bus_i2s0_clk.common.hw,
		[CLK_BUS_MCU_I2S1]		= &bus_i2s1_clk.common.hw,
		[CLK_BUS_MCU_I2S2]		= &bus_i2s2_clk.common.hw,
		[CLK_BUS_MCU_I2S3]		= &bus_i2s3_clk.common.hw,
		[CLK_MCU_SPDIF_TX]		= &spdif_tx_clk.common.hw,
		[CLK_MCU_SPDIF_RX]		= &spdif_rx_clk.common.hw,
		[CLK_BUS_MCU_SPDIF]		= &bus_spdif_clk.common.hw,
		[CLK_MCU_DMIC]			= &dmic_clk.common.hw,
		[CLK_BUS_MCU_DMIC]		= &bus_dmic_clk.common.hw,
		[CLK_MCU_AUDIO_CODEC_DAC]	= &audio_dac_clk.common.hw,
		[CLK_MCU_AUDIO_CODEC_ADC]	= &audio_adc_clk.common.hw,
		[CLK_BUS_MCU_AUDIO_CODEC]	= &bus_audio_codec_clk.common.hw,
		[CLK_BUS_MCU_DSP_MSGBOX]	= &bus_dsp_msgbox_clk.common.hw,
		[CLK_BUS_MCU_DSP_CFG]		= &bus_dsp_cfg_clk.common.hw,
		[CLK_BUS_MCU_NPU_HCLK]		= &bus_npu_hclk.common.hw,
		[CLK_BUS_MCU_NPU_ACLK]		= &bus_npu_aclk.common.hw,
		[CLK_MCU_TIMER0]		= &mcu_timer0_clk.common.hw,
		[CLK_MCU_TIMER1]		= &mcu_timer1_clk.common.hw,
		[CLK_MCU_TIMER2]		= &mcu_timer2_clk.common.hw,
		[CLK_MCU_TIMER3]		= &mcu_timer3_clk.common.hw,
		[CLK_MCU_TIMER4]		= &mcu_timer4_clk.common.hw,
		[CLK_MCU_TIMER5]		= &mcu_timer5_clk.common.hw,
		[CLK_BUS_MCU_TIMER]		= &bus_mcu_timer_clk.common.hw,
		[CLK_BUS_MCU_DMA]		= &bus_mcu_dma_clk.common.hw,
		[CLK_MCU_TZMA0]			= &tzma0_clk.common.hw,
		[CLK_MCU_TZMA1]			= &tzma1_clk.common.hw,
		[CLK_BUS_MCU_PUBSRAM]		= &bus_pubsram_clk.common.hw,
		[CLK_MCU_MBUS_DMA]		= &mbus_mcu_dma_clk.common.hw,
		[CLK_MCU_MBUS]			= &mbus_mcu_clk.common.hw,
		[CLK_MCU_RISCV]			= &riscv_clk.common.hw,
		[CLK_BUS_MCU_RISCV_CFG]		= &bus_riscv_cfg_clk.common.hw,
		[CLK_BUS_MCU_RISCV_MSGBOX]	= &bus_riscv_msgbox_clk.common.hw,
		[CLK_MCU_PWM0]			= &mcu_pwm0_clk.common.hw,
		[CLK_BUS_MCU_PWM0]		= &bus_mcu_pwm0_clk.common.hw,
	},
	.num	= CLK_BUS_MCU_PWM0 + 1,
};

static struct ccu_reset_map sun55i_a523_mcu_ccu_resets[] = {
	[RST_BUS_MCU_I2S0]		= { 0x0040, BIT(16) },
	[RST_BUS_MCU_I2S1]		= { 0x0040, BIT(17) },
	[RST_BUS_MCU_I2S2]		= { 0x0040, BIT(18) },
	[RST_BUS_MCU_I2S3]		= { 0x0040, BIT(19) },
	[RST_BUS_MCU_SPDIF]		= { 0x004c, BIT(16) },
	[RST_BUS_MCU_DMIC]		= { 0x0054, BIT(16) },
	[RST_BUS_MCU_AUDIO_CODEC]	= { 0x0060, BIT(16) },
	[RST_BUS_MCU_DSP_MSGBOX]	= { 0x0068, BIT(16) },
	[RST_BUS_MCU_DSP_CFG]		= { 0x006c, BIT(16) },
	[RST_BUS_MCU_NPU]		= { 0x0070, BIT(16) },
	[RST_BUS_MCU_TIMER]		= { 0x008c, BIT(16) },
	/* dsp and dsp_debug resets only found in BSP code. */
	[RST_BUS_MCU_DSP_DEBUG]		= { 0x0100, BIT(16) },
	[RST_BUS_MCU_DSP]		= { 0x0100, BIT(17) },
	[RST_BUS_MCU_DMA]		= { 0x0104, BIT(16) },
	[RST_BUS_MCU_PUBSRAM]		= { 0x0114, BIT(16) },
	[RST_BUS_MCU_RISCV_CFG]		= { 0x0124, BIT(16) },
	[RST_BUS_MCU_RISCV_DEBUG]	= { 0x0124, BIT(17) },
	[RST_BUS_MCU_RISCV_CORE]	= { 0x0124, BIT(18) },
	[RST_BUS_MCU_RISCV_MSGBOX]	= { 0x0128, BIT(16) },
	[RST_BUS_MCU_PWM0]		= { 0x0134, BIT(16) },
};

static const struct sunxi_ccu_desc sun55i_a523_mcu_ccu_desc = {
	.ccu_clks	= sun55i_a523_mcu_ccu_clks,
	.num_ccu_clks	= ARRAY_SIZE(sun55i_a523_mcu_ccu_clks),

	.hw_clks	= &sun55i_a523_mcu_hw_clks,

	.resets		= sun55i_a523_mcu_ccu_resets,
	.num_resets	= ARRAY_SIZE(sun55i_a523_mcu_ccu_resets),
};

static int sun55i_a523_mcu_ccu_probe(struct platform_device *pdev)
{
	void __iomem *reg;
	u32 val;
	int ret;

	reg = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(reg))
		return PTR_ERR(reg);

	val = readl(reg + SUN55I_A523_PLL_AUDIO1_REG);

	/*
	 * The PLL clock code does not model all bits, for instance it does
	 * not support a separate enable and gate bit. We present the
	 * gate bit(27) as the enable bit, but then have to set the
	 * PLL Enable, LDO Enable, and Lock Enable bits on all PLLs here.
	 */
	val |= BIT(31) | BIT(30) | BIT(29);

	/* Enforce p1 = 5, p0 = 2 (the default) for PLL_AUDIO1 */
	val &= ~(GENMASK(22, 20) | GENMASK(18, 16));
	val |= (4 << 20) | (1 << 16);

	writel(val, reg + SUN55I_A523_PLL_AUDIO1_REG);

	ret = devm_sunxi_ccu_probe(&pdev->dev, reg, &sun55i_a523_mcu_ccu_desc);
	if (ret)
		return ret;

	return 0;
}

static const struct of_device_id sun55i_a523_mcu_ccu_ids[] = {
	{ .compatible = "allwinner,sun55i-a523-mcu-ccu" },
	{ }
};

static struct platform_driver sun55i_a523_mcu_ccu_driver = {
	.probe	= sun55i_a523_mcu_ccu_probe,
	.driver	= {
		.name			= "sun55i-a523-mcu-ccu",
		.suppress_bind_attrs	= true,
		.of_match_table		= sun55i_a523_mcu_ccu_ids,
	},
};
module_platform_driver(sun55i_a523_mcu_ccu_driver);

MODULE_IMPORT_NS("SUNXI_CCU");
MODULE_DESCRIPTION("Support for the Allwinner A523 MCU CCU");
MODULE_LICENSE("GPL");
