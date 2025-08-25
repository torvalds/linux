// SPDX-License-Identifier: GPL-2.0-only
/*
 * Amlogic C3 Peripherals Clock Controller Driver
 *
 * Copyright (c) 2023 Amlogic, inc.
 * Author: Chuan Liu <chuan.liu@amlogic.com>
 */

#include <linux/clk-provider.h>
#include <linux/platform_device.h>
#include "clk-regmap.h"
#include "clk-dualdiv.h"
#include "meson-clkc-utils.h"
#include <dt-bindings/clock/amlogic,c3-peripherals-clkc.h>

#define RTC_BY_OSCIN_CTRL0			0x8
#define RTC_BY_OSCIN_CTRL1			0xc
#define RTC_CTRL				0x10
#define SYS_CLK_EN0_REG0			0x44
#define SYS_CLK_EN0_REG1			0x48
#define SYS_CLK_EN0_REG2			0x4c
#define CLK12_24_CTRL				0xa8
#define AXI_CLK_EN0				0xac
#define VDIN_MEAS_CLK_CTRL			0xf8
#define VAPB_CLK_CTRL				0xfc
#define MIPIDSI_PHY_CLK_CTRL			0x104
#define GE2D_CLK_CTRL				0x10c
#define ISP0_CLK_CTRL				0x110
#define DEWARPA_CLK_CTRL			0x114
#define VOUTENC_CLK_CTRL			0x118
#define VDEC_CLK_CTRL				0x140
#define VDEC3_CLK_CTRL				0x148
#define TS_CLK_CTRL				0x158
#define ETH_CLK_CTRL				0x164
#define NAND_CLK_CTRL				0x168
#define SD_EMMC_CLK_CTRL			0x16c
#define SPICC_CLK_CTRL				0x174
#define GEN_CLK_CTRL				0x178
#define SAR_CLK_CTRL0				0x17c
#define PWM_CLK_AB_CTRL				0x180
#define PWM_CLK_CD_CTRL				0x184
#define PWM_CLK_EF_CTRL				0x188
#define PWM_CLK_GH_CTRL				0x18c
#define PWM_CLK_IJ_CTRL				0x190
#define PWM_CLK_KL_CTRL				0x194
#define PWM_CLK_MN_CTRL				0x198
#define VC9000E_CLK_CTRL			0x19c
#define SPIFC_CLK_CTRL				0x1a0
#define NNA_CLK_CTRL				0x220

#define C3_COMP_SEL(_name, _reg, _shift, _mask, _pdata) \
	MESON_COMP_SEL(c3_, _name, _reg, _shift, _mask, _pdata, NULL, 0, 0)

#define C3_COMP_DIV(_name, _reg, _shift, _width) \
	MESON_COMP_DIV(c3_, _name, _reg, _shift, _width, 0, CLK_SET_RATE_PARENT)

#define C3_COMP_GATE(_name, _reg, _bit) \
	MESON_COMP_GATE(c3_, _name, _reg, _bit, CLK_SET_RATE_PARENT)

static struct clk_regmap c3_rtc_xtal_clkin = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = RTC_BY_OSCIN_CTRL0,
		.bit_idx = 31,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "rtc_xtal_clkin",
		.ops = &clk_regmap_gate_ops,
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "oscin",
		},
		.num_parents = 1,
	},
};

static const struct meson_clk_dualdiv_param c3_rtc_32k_div_table[] = {
	{ 733, 732, 8, 11, 1 },
	{ /* sentinel */ }
};

static struct clk_regmap c3_rtc_32k_div = {
	.data = &(struct meson_clk_dualdiv_data) {
		.n1 = {
			.reg_off = RTC_BY_OSCIN_CTRL0,
			.shift   = 0,
			.width   = 12,
		},
		.n2 = {
			.reg_off = RTC_BY_OSCIN_CTRL0,
			.shift   = 12,
			.width   = 12,
		},
		.m1 = {
			.reg_off = RTC_BY_OSCIN_CTRL1,
			.shift   = 0,
			.width   = 12,
		},
		.m2 = {
			.reg_off = RTC_BY_OSCIN_CTRL1,
			.shift   = 12,
			.width   = 12,
		},
		.dual = {
			.reg_off = RTC_BY_OSCIN_CTRL0,
			.shift   = 28,
			.width   = 1,
		},
		.table = c3_rtc_32k_div_table,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "rtc_32k_div",
		.ops = &meson_clk_dualdiv_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&c3_rtc_xtal_clkin.hw
		},
		.num_parents = 1,
	},
};

static const struct clk_parent_data c3_rtc_32k_parents[] = {
	{ .hw = &c3_rtc_32k_div.hw },
	{ .hw = &c3_rtc_xtal_clkin.hw }
};

static struct clk_regmap c3_rtc_32k_sel = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = RTC_BY_OSCIN_CTRL1,
		.mask = 0x1,
		.shift = 24,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "rtc_32k_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = c3_rtc_32k_parents,
		.num_parents = ARRAY_SIZE(c3_rtc_32k_parents),
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap c3_rtc_32k = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = RTC_BY_OSCIN_CTRL0,
		.bit_idx = 30,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "rtc_32k",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&c3_rtc_32k_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data c3_rtc_clk_parents[] = {
	{ .fw_name = "oscin" },
	{ .hw = &c3_rtc_32k.hw },
	{ .fw_name = "pad_osc" }
};

static struct clk_regmap c3_rtc_clk = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = RTC_CTRL,
		.mask = 0x3,
		.shift = 0,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "rtc_clk",
		.ops = &clk_regmap_mux_ops,
		.parent_data = c3_rtc_clk_parents,
		.num_parents = ARRAY_SIZE(c3_rtc_clk_parents),
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data c3_sys_pclk_parents = { .fw_name = "sysclk" };

#define C3_SYS_PCLK(_name, _reg, _bit, _flags) \
	MESON_PCLK(c3_##_name, _reg, _bit, &c3_sys_pclk_parents, _flags)

#define C3_SYS_PCLK_RO(_name, _reg, _bit) \
	MESON_PCLK_RO(c3_##_name, _reg, _bit, &c3_sys_pclk_parents, 0)

static C3_SYS_PCLK(sys_reset_ctrl,	SYS_CLK_EN0_REG0, 1, 0);
static C3_SYS_PCLK(sys_pwr_ctrl,	SYS_CLK_EN0_REG0, 3, 0);
static C3_SYS_PCLK(sys_pad_ctrl,	SYS_CLK_EN0_REG0, 4, 0);
static C3_SYS_PCLK(sys_ctrl,		SYS_CLK_EN0_REG0, 5, 0);
static C3_SYS_PCLK(sys_ts_pll,		SYS_CLK_EN0_REG0, 6, 0);

/*
 * NOTE: sys_dev_arb provides the clock to the ETH and SPICC arbiters that
 * access the AXI bus.
 */
static C3_SYS_PCLK(sys_dev_arb,		SYS_CLK_EN0_REG0, 7, 0);

/*
 * FIXME: sys_mmc_pclk provides the clock for the DDR PHY, DDR will only be
 * initialized in bl2, and this clock should not be touched in linux.
 */
static C3_SYS_PCLK_RO(sys_mmc_pclk,	SYS_CLK_EN0_REG0, 8);

/*
 * NOTE: sys_cpu_ctrl provides the clock for CPU controller. After clock is
 * disabled, cpu_clk and other key CPU-related configurations cannot take effect.
 */
static C3_SYS_PCLK(sys_cpu_ctrl,	SYS_CLK_EN0_REG0, 11, CLK_IS_CRITICAL);
static C3_SYS_PCLK(sys_jtag_ctrl,	SYS_CLK_EN0_REG0, 12, 0);
static C3_SYS_PCLK(sys_ir_ctrl,		SYS_CLK_EN0_REG0, 13, 0);

/*
 * NOTE: sys_irq_ctrl provides the clock for IRQ controller. The IRQ controller
 * collects and distributes the interrupt signal to the GIC, PWR_CTRL, and
 * AOCPU. If the clock is disabled, interrupt-related functions will occurs an
 * exception.
 */
static C3_SYS_PCLK(sys_irq_ctrl,	SYS_CLK_EN0_REG0, 14, CLK_IS_CRITICAL);
static C3_SYS_PCLK(sys_msr_clk,		SYS_CLK_EN0_REG0, 15, 0);
static C3_SYS_PCLK(sys_rom,		SYS_CLK_EN0_REG0, 16, 0);
static C3_SYS_PCLK(sys_uart_f,		SYS_CLK_EN0_REG0, 17, 0);
static C3_SYS_PCLK(sys_cpu_apb,		SYS_CLK_EN0_REG0, 18, 0);
static C3_SYS_PCLK(sys_rsa,		SYS_CLK_EN0_REG0, 19, 0);
static C3_SYS_PCLK(sys_sar_adc,		SYS_CLK_EN0_REG0, 20, 0);
static C3_SYS_PCLK(sys_startup,		SYS_CLK_EN0_REG0, 21, 0);
static C3_SYS_PCLK(sys_secure,		SYS_CLK_EN0_REG0, 22, 0);
static C3_SYS_PCLK(sys_spifc,		SYS_CLK_EN0_REG0, 23, 0);
static C3_SYS_PCLK(sys_nna,		SYS_CLK_EN0_REG0, 25, 0);
static C3_SYS_PCLK(sys_eth_mac,		SYS_CLK_EN0_REG0, 26, 0);

/*
 * FIXME: sys_gic provides the clock for GIC(Generic Interrupt Controller).
 * After clock is disabled, The GIC cannot work properly. At present, the driver
 * used by our GIC is the public driver in kernel, and there is no management
 * clock in the driver.
 */
static C3_SYS_PCLK(sys_gic,		SYS_CLK_EN0_REG0, 27, CLK_IS_CRITICAL);
static C3_SYS_PCLK(sys_rama,		SYS_CLK_EN0_REG0, 28, 0);

/*
 * NOTE: sys_big_nic provides the clock to the control bus of the NIC(Network
 * Interface Controller) between multiple devices(CPU, DDR, RAM, ROM, GIC,
 * SPIFC, CAPU, JTAG, EMMC, SDIO, sec_top, USB, Audio, ETH, SPICC) in the
 * system. After clock is disabled, The NIC cannot work.
 */
static C3_SYS_PCLK(sys_big_nic,		SYS_CLK_EN0_REG0, 29, CLK_IS_CRITICAL);
static C3_SYS_PCLK(sys_ramb,		SYS_CLK_EN0_REG0, 30, 0);
static C3_SYS_PCLK(sys_audio_pclk,	SYS_CLK_EN0_REG0, 31, 0);
static C3_SYS_PCLK(sys_pwm_kl,		SYS_CLK_EN0_REG1, 0, 0);
static C3_SYS_PCLK(sys_pwm_ij,		SYS_CLK_EN0_REG1, 1, 0);
static C3_SYS_PCLK(sys_usb,		SYS_CLK_EN0_REG1, 2, 0);
static C3_SYS_PCLK(sys_sd_emmc_a,	SYS_CLK_EN0_REG1, 3, 0);
static C3_SYS_PCLK(sys_sd_emmc_c,	SYS_CLK_EN0_REG1, 4, 0);
static C3_SYS_PCLK(sys_pwm_ab,		SYS_CLK_EN0_REG1, 5, 0);
static C3_SYS_PCLK(sys_pwm_cd,		SYS_CLK_EN0_REG1, 6, 0);
static C3_SYS_PCLK(sys_pwm_ef,		SYS_CLK_EN0_REG1, 7, 0);
static C3_SYS_PCLK(sys_pwm_gh,		SYS_CLK_EN0_REG1, 8, 0);
static C3_SYS_PCLK(sys_spicc_1,		SYS_CLK_EN0_REG1, 9, 0);
static C3_SYS_PCLK(sys_spicc_0,		SYS_CLK_EN0_REG1, 10, 0);
static C3_SYS_PCLK(sys_uart_a,		SYS_CLK_EN0_REG1, 11, 0);
static C3_SYS_PCLK(sys_uart_b,		SYS_CLK_EN0_REG1, 12, 0);
static C3_SYS_PCLK(sys_uart_c,		SYS_CLK_EN0_REG1, 13, 0);
static C3_SYS_PCLK(sys_uart_d,		SYS_CLK_EN0_REG1, 14, 0);
static C3_SYS_PCLK(sys_uart_e,		SYS_CLK_EN0_REG1, 15, 0);
static C3_SYS_PCLK(sys_i2c_m_a,		SYS_CLK_EN0_REG1, 16, 0);
static C3_SYS_PCLK(sys_i2c_m_b,		SYS_CLK_EN0_REG1, 17, 0);
static C3_SYS_PCLK(sys_i2c_m_c,		SYS_CLK_EN0_REG1, 18, 0);
static C3_SYS_PCLK(sys_i2c_m_d,		SYS_CLK_EN0_REG1, 19, 0);
static C3_SYS_PCLK(sys_i2c_s_a,		SYS_CLK_EN0_REG1, 20, 0);
static C3_SYS_PCLK(sys_rtc,		SYS_CLK_EN0_REG1, 21, 0);
static C3_SYS_PCLK(sys_ge2d,		SYS_CLK_EN0_REG1, 22, 0);
static C3_SYS_PCLK(sys_isp,		SYS_CLK_EN0_REG1, 23, 0);
static C3_SYS_PCLK(sys_gpv_isp_nic,	SYS_CLK_EN0_REG1, 24, 0);
static C3_SYS_PCLK(sys_gpv_cve_nic,	SYS_CLK_EN0_REG1, 25, 0);
static C3_SYS_PCLK(sys_mipi_dsi_host,	SYS_CLK_EN0_REG1, 26, 0);
static C3_SYS_PCLK(sys_mipi_dsi_phy,	SYS_CLK_EN0_REG1, 27, 0);
static C3_SYS_PCLK(sys_eth_phy,		SYS_CLK_EN0_REG1, 28, 0);
static C3_SYS_PCLK(sys_acodec,		SYS_CLK_EN0_REG1, 29, 0);
static C3_SYS_PCLK(sys_dwap,		SYS_CLK_EN0_REG1, 30, 0);
static C3_SYS_PCLK(sys_dos,		SYS_CLK_EN0_REG1, 31, 0);
static C3_SYS_PCLK(sys_cve,		SYS_CLK_EN0_REG2, 0, 0);
static C3_SYS_PCLK(sys_vout,		SYS_CLK_EN0_REG2, 1, 0);
static C3_SYS_PCLK(sys_vc9000e,		SYS_CLK_EN0_REG2, 2, 0);
static C3_SYS_PCLK(sys_pwm_mn,		SYS_CLK_EN0_REG2, 3, 0);
static C3_SYS_PCLK(sys_sd_emmc_b,	SYS_CLK_EN0_REG2, 4, 0);

static const struct clk_parent_data c3_axi_pclk_parents = { .fw_name = "axiclk" };

#define C3_AXI_PCLK(_name, _reg, _bit, _flags) \
	MESON_PCLK(c3_##_name, _reg, _bit, &c3_axi_pclk_parents, _flags)

/*
 * NOTE: axi_sys_nic provides the clock to the AXI bus of the system NIC. After
 * clock is disabled, The NIC cannot work.
 */
static C3_AXI_PCLK(axi_sys_nic,		AXI_CLK_EN0, 2, CLK_IS_CRITICAL);
static C3_AXI_PCLK(axi_isp_nic,		AXI_CLK_EN0, 3, 0);
static C3_AXI_PCLK(axi_cve_nic,		AXI_CLK_EN0, 4, 0);
static C3_AXI_PCLK(axi_ramb,		AXI_CLK_EN0, 5, 0);
static C3_AXI_PCLK(axi_rama,		AXI_CLK_EN0, 6, 0);

/*
 * NOTE: axi_cpu_dmc provides the clock to the AXI bus where the CPU accesses
 * the DDR. After clock is disabled, The CPU will not have access to the DDR.
 */
static C3_AXI_PCLK(axi_cpu_dmc,		AXI_CLK_EN0, 7, CLK_IS_CRITICAL);
static C3_AXI_PCLK(axi_nic,		AXI_CLK_EN0, 8, 0);
static C3_AXI_PCLK(axi_dma,		AXI_CLK_EN0, 9, 0);

/*
 * NOTE: axi_mux_nic provides the clock to the NIC's AXI bus for NN(Neural
 * Network) and other devices(CPU, EMMC, SDIO, sec_top, USB, Audio, ETH, SPICC)
 * to access RAM space.
 */
static C3_AXI_PCLK(axi_mux_nic,		AXI_CLK_EN0, 10, 0);
static C3_AXI_PCLK(axi_cve,		AXI_CLK_EN0, 12, 0);

/*
 * NOTE: axi_dev1_dmc provides the clock for the peripherals(EMMC, SDIO,
 * sec_top, USB, Audio, ETH, SPICC) to access the AXI bus of the DDR.
 */
static C3_AXI_PCLK(axi_dev1_dmc,	AXI_CLK_EN0, 13, 0);
static C3_AXI_PCLK(axi_dev0_dmc,	AXI_CLK_EN0, 14, 0);
static C3_AXI_PCLK(axi_dsp_dmc,		AXI_CLK_EN0, 15, 0);

/*
 * clk_12_24m model
 *
 *          |------|     |-----| clk_12m_24m |-----|
 * xtal---->| gate |---->| div |------------>| pad |
 *          |------|     |-----|             |-----|
 */
static struct clk_regmap c3_clk_12_24m_in = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = CLK12_24_CTRL,
		.bit_idx = 11,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "clk_12_24m_in",
		.ops = &clk_regmap_gate_ops,
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "xtal_24m",
		},
		.num_parents = 1,
	},
};

static struct clk_regmap c3_clk_12_24m = {
	.data = &(struct clk_regmap_div_data) {
		.offset = CLK12_24_CTRL,
		.shift = 10,
		.width = 1,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "clk_12_24m",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&c3_clk_12_24m_in.hw
		},
		.num_parents = 1,
	},
};

/* Fix me: set value 0 will div by 2 like value 1 */
static struct clk_regmap c3_fclk_25m_div = {
	.data = &(struct clk_regmap_div_data) {
		.offset = CLK12_24_CTRL,
		.shift = 0,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "fclk_25m_div",
		.ops = &clk_regmap_divider_ops,
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "fix",
		},
		.num_parents = 1,
	},
};

static struct clk_regmap c3_fclk_25m = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = CLK12_24_CTRL,
		.bit_idx = 12,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "fclk_25m",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&c3_fclk_25m_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

/*
 * Channel 3(ddr_dpll_pt_clk) is manged by the DDR module; channel 12(cts_msr_clk)
 * is manged by clock measures module. Their hardware are out of clock tree.
 * Channel 4 8 9 10 11 13 14 15 16 18 are not connected.
 */
static u32 c3_gen_parents_val_table[] = { 0, 1, 2, 5, 6, 7, 17, 19, 20, 21, 22, 23, 24};
static const struct clk_parent_data c3_gen_parents[] = {
	{ .fw_name = "oscin" },
	{ .hw = &c3_rtc_clk.hw },
	{ .fw_name = "sysplldiv16" },
	{ .fw_name = "gp0" },
	{ .fw_name = "gp1" },
	{ .fw_name = "hifi" },
	{ .fw_name = "cpudiv16" },
	{ .fw_name = "fdiv2" },
	{ .fw_name = "fdiv2p5" },
	{ .fw_name = "fdiv3" },
	{ .fw_name = "fdiv4" },
	{ .fw_name = "fdiv5" },
	{ .fw_name = "fdiv7" }
};

static struct clk_regmap c3_gen_sel = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = GEN_CLK_CTRL,
		.mask = 0x1f,
		.shift = 12,
		.table = c3_gen_parents_val_table,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "gen_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = c3_gen_parents,
		.num_parents = ARRAY_SIZE(c3_gen_parents),
	},
};

static struct clk_regmap c3_gen_div = {
	.data = &(struct clk_regmap_div_data) {
		.offset = GEN_CLK_CTRL,
		.shift = 0,
		.width = 11,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "gen_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&c3_gen_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap c3_gen = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = GEN_CLK_CTRL,
		.bit_idx = 11,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "gen",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&c3_gen_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data c3_saradc_parents[] = {
	{ .fw_name = "oscin" },
	{ .fw_name = "sysclk" }
};

static C3_COMP_SEL(saradc, SAR_CLK_CTRL0, 9, 0x1, c3_saradc_parents);
static C3_COMP_DIV(saradc, SAR_CLK_CTRL0, 0, 8);
static C3_COMP_GATE(saradc, SAR_CLK_CTRL0, 8);

static const struct clk_parent_data c3_pwm_parents[] = {
	{ .fw_name = "oscin" },
	{ .fw_name = "gp1" },
	{ .fw_name = "fdiv4" },
	{ .fw_name = "fdiv3" }
};

static C3_COMP_SEL(pwm_a, PWM_CLK_AB_CTRL, 9, 0x3, c3_pwm_parents);
static C3_COMP_DIV(pwm_a, PWM_CLK_AB_CTRL, 0, 8);
static C3_COMP_GATE(pwm_a, PWM_CLK_AB_CTRL, 8);

static C3_COMP_SEL(pwm_b, PWM_CLK_AB_CTRL, 25, 0x3, c3_pwm_parents);
static C3_COMP_DIV(pwm_b, PWM_CLK_AB_CTRL, 16, 8);
static C3_COMP_GATE(pwm_b, PWM_CLK_AB_CTRL, 24);

static C3_COMP_SEL(pwm_c, PWM_CLK_CD_CTRL, 9, 0x3, c3_pwm_parents);
static C3_COMP_DIV(pwm_c, PWM_CLK_CD_CTRL, 0, 8);
static C3_COMP_GATE(pwm_c, PWM_CLK_CD_CTRL, 8);

static C3_COMP_SEL(pwm_d, PWM_CLK_CD_CTRL, 25, 0x3, c3_pwm_parents);
static C3_COMP_DIV(pwm_d, PWM_CLK_CD_CTRL, 16, 8);
static C3_COMP_GATE(pwm_d, PWM_CLK_CD_CTRL, 24);

static C3_COMP_SEL(pwm_e, PWM_CLK_EF_CTRL, 9, 0x3, c3_pwm_parents);
static C3_COMP_DIV(pwm_e, PWM_CLK_EF_CTRL, 0, 8);
static C3_COMP_GATE(pwm_e, PWM_CLK_EF_CTRL, 8);

static C3_COMP_SEL(pwm_f, PWM_CLK_EF_CTRL, 25, 0x3, c3_pwm_parents);
static C3_COMP_DIV(pwm_f, PWM_CLK_EF_CTRL, 16, 8);
static C3_COMP_GATE(pwm_f, PWM_CLK_EF_CTRL, 24);

static C3_COMP_SEL(pwm_g, PWM_CLK_GH_CTRL, 9, 0x3, c3_pwm_parents);
static C3_COMP_DIV(pwm_g, PWM_CLK_GH_CTRL, 0, 8);
static C3_COMP_GATE(pwm_g, PWM_CLK_GH_CTRL, 8);

static C3_COMP_SEL(pwm_h, PWM_CLK_GH_CTRL, 25, 0x3, c3_pwm_parents);
static C3_COMP_DIV(pwm_h, PWM_CLK_GH_CTRL, 16, 8);
static C3_COMP_GATE(pwm_h, PWM_CLK_GH_CTRL, 24);

static C3_COMP_SEL(pwm_i, PWM_CLK_IJ_CTRL, 9, 0x3, c3_pwm_parents);
static C3_COMP_DIV(pwm_i, PWM_CLK_IJ_CTRL, 0, 8);
static C3_COMP_GATE(pwm_i, PWM_CLK_IJ_CTRL, 8);

static C3_COMP_SEL(pwm_j, PWM_CLK_IJ_CTRL, 25, 0x3, c3_pwm_parents);
static C3_COMP_DIV(pwm_j, PWM_CLK_IJ_CTRL, 16, 8);
static C3_COMP_GATE(pwm_j, PWM_CLK_IJ_CTRL, 24);

static C3_COMP_SEL(pwm_k, PWM_CLK_KL_CTRL, 9, 0x3, c3_pwm_parents);
static C3_COMP_DIV(pwm_k, PWM_CLK_KL_CTRL, 0, 8);
static C3_COMP_GATE(pwm_k, PWM_CLK_KL_CTRL, 8);

static C3_COMP_SEL(pwm_l, PWM_CLK_KL_CTRL, 25, 0x3, c3_pwm_parents);
static C3_COMP_DIV(pwm_l, PWM_CLK_KL_CTRL, 16, 8);
static C3_COMP_GATE(pwm_l, PWM_CLK_KL_CTRL, 24);

static C3_COMP_SEL(pwm_m, PWM_CLK_MN_CTRL, 9, 0x3, c3_pwm_parents);
static C3_COMP_DIV(pwm_m, PWM_CLK_MN_CTRL, 0, 8);
static C3_COMP_GATE(pwm_m, PWM_CLK_MN_CTRL, 8);

static C3_COMP_SEL(pwm_n, PWM_CLK_MN_CTRL, 25, 0x3, c3_pwm_parents);
static C3_COMP_DIV(pwm_n, PWM_CLK_MN_CTRL, 16, 8);
static C3_COMP_GATE(pwm_n, PWM_CLK_MN_CTRL, 24);

static const struct clk_parent_data c3_spicc_parents[] = {
	{ .fw_name = "oscin" },
	{ .fw_name = "sysclk" },
	{ .fw_name = "fdiv4" },
	{ .fw_name = "fdiv3" },
	{ .fw_name = "fdiv2" },
	{ .fw_name = "fdiv5" },
	{ .fw_name = "fdiv7" },
	{ .fw_name = "gp1" }
};

static C3_COMP_SEL(spicc_a, SPICC_CLK_CTRL, 7, 0x7, c3_spicc_parents);
static C3_COMP_DIV(spicc_a, SPICC_CLK_CTRL, 0, 6);
static C3_COMP_GATE(spicc_a, SPICC_CLK_CTRL,  6);

static C3_COMP_SEL(spicc_b, SPICC_CLK_CTRL, 23, 0x7, c3_spicc_parents);
static C3_COMP_DIV(spicc_b, SPICC_CLK_CTRL, 16, 6);
static C3_COMP_GATE(spicc_b, SPICC_CLK_CTRL, 22);

static const struct clk_parent_data c3_spifc_parents[] = {
	{ .fw_name = "gp0" },
	{ .fw_name = "fdiv2" },
	{ .fw_name = "fdiv3" },
	{ .fw_name = "fdiv2p5" },
	{ .fw_name = "hifi" },
	{ .fw_name = "fdiv4" },
	{ .fw_name = "fdiv5" },
	{ .fw_name = "fdiv7" }
};

static C3_COMP_SEL(spifc, SPIFC_CLK_CTRL, 9, 0x7, c3_spifc_parents);
static C3_COMP_DIV(spifc, SPIFC_CLK_CTRL, 0, 7);
static C3_COMP_GATE(spifc, SPIFC_CLK_CTRL,  8);

static const struct clk_parent_data c3_sd_emmc_parents[] = {
	{ .fw_name = "oscin" },
	{ .fw_name = "fdiv2" },
	{ .fw_name = "fdiv3" },
	{ .fw_name = "hifi" },
	{ .fw_name = "fdiv2p5" },
	{ .fw_name = "fdiv4" },
	{ .fw_name = "gp1" },
	{ .fw_name = "gp0" }
};

static C3_COMP_SEL(sd_emmc_a, SD_EMMC_CLK_CTRL, 9, 0x7, c3_sd_emmc_parents);
static C3_COMP_DIV(sd_emmc_a, SD_EMMC_CLK_CTRL, 0, 7);
static C3_COMP_GATE(sd_emmc_a, SD_EMMC_CLK_CTRL,  7);

static C3_COMP_SEL(sd_emmc_b, SD_EMMC_CLK_CTRL, 25, 0x7, c3_sd_emmc_parents);
static C3_COMP_DIV(sd_emmc_b, SD_EMMC_CLK_CTRL, 16, 7);
static C3_COMP_GATE(sd_emmc_b, SD_EMMC_CLK_CTRL, 23);

static C3_COMP_SEL(sd_emmc_c, NAND_CLK_CTRL, 9, 0x7, c3_sd_emmc_parents);
static C3_COMP_DIV(sd_emmc_c, NAND_CLK_CTRL, 0, 7);
static C3_COMP_GATE(sd_emmc_c, NAND_CLK_CTRL, 7);

static struct clk_regmap c3_ts_div = {
	.data = &(struct clk_regmap_div_data) {
		.offset = TS_CLK_CTRL,
		.shift = 0,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "ts_div",
		.ops = &clk_regmap_divider_ops,
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "oscin",
		},
		.num_parents = 1,
	},
};

static struct clk_regmap c3_ts = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = TS_CLK_CTRL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "ts",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&c3_ts_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data c3_eth_parents = {
	.fw_name = "fdiv2",
};

static struct clk_fixed_factor c3_eth_125m_div = {
	.mult = 1,
	.div = 8,
	.hw.init = &(struct clk_init_data) {
		.name = "eth_125m_div",
		.ops = &clk_fixed_factor_ops,
		.parent_data = &c3_eth_parents,
		.num_parents = 1,
	},
};

static struct clk_regmap c3_eth_125m = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = ETH_CLK_CTRL,
		.bit_idx = 7,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "eth_125m",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&c3_eth_125m_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap c3_eth_rmii_div = {
	.data = &(struct clk_regmap_div_data) {
		.offset = ETH_CLK_CTRL,
		.shift = 0,
		.width = 7,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "eth_rmii_div",
		.ops = &clk_regmap_divider_ops,
		.parent_data = &c3_eth_parents,
		.num_parents = 1,
	},
};

static struct clk_regmap c3_eth_rmii = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = ETH_CLK_CTRL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "eth_rmii",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&c3_eth_rmii_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data c3_mipi_dsi_meas_parents[] = {
	{ .fw_name = "oscin" },
	{ .fw_name = "fdiv4" },
	{ .fw_name = "fdiv3" },
	{ .fw_name = "fdiv5" },
	{ .fw_name = "gp1" },
	{ .fw_name = "gp0" },
	{ .fw_name = "fdiv2" },
	{ .fw_name = "fdiv7" }
};

static C3_COMP_SEL(mipi_dsi_meas, VDIN_MEAS_CLK_CTRL, 21, 0x7, c3_mipi_dsi_meas_parents);
static C3_COMP_DIV(mipi_dsi_meas, VDIN_MEAS_CLK_CTRL, 12, 7);
static C3_COMP_GATE(mipi_dsi_meas, VDIN_MEAS_CLK_CTRL, 20);

static const struct clk_parent_data c3_dsi_phy_parents[] = {
	{ .fw_name = "gp1" },
	{ .fw_name = "gp0" },
	{ .fw_name = "hifi" },
	{ .fw_name = "fdiv3" },
	{ .fw_name = "fdiv2" },
	{ .fw_name = "fdiv2p5" },
	{ .fw_name = "fdiv4" },
	{ .fw_name = "fdiv7" }
};

static C3_COMP_SEL(dsi_phy, MIPIDSI_PHY_CLK_CTRL, 12, 0x7, c3_dsi_phy_parents);
static C3_COMP_DIV(dsi_phy, MIPIDSI_PHY_CLK_CTRL, 0, 7);
static C3_COMP_GATE(dsi_phy, MIPIDSI_PHY_CLK_CTRL, 8);

static const struct clk_parent_data c3_vout_mclk_parents[] = {
	{ .fw_name = "fdiv2p5" },
	{ .fw_name = "fdiv3" },
	{ .fw_name = "fdiv4" },
	{ .fw_name = "fdiv5" },
	{ .fw_name = "gp0" },
	{ .fw_name = "hifi" },
	{ .fw_name = "gp1" },
	{ .fw_name = "fdiv7" }
};

static C3_COMP_SEL(vout_mclk, VOUTENC_CLK_CTRL, 9, 0x7, c3_vout_mclk_parents);
static C3_COMP_DIV(vout_mclk, VOUTENC_CLK_CTRL, 0, 7);
static C3_COMP_GATE(vout_mclk, VOUTENC_CLK_CTRL, 8);

static const struct clk_parent_data c3_vout_enc_parents[] = {
	{ .fw_name = "gp1" },
	{ .fw_name = "fdiv3" },
	{ .fw_name = "fdiv4" },
	{ .fw_name = "fdiv5" },
	{ .fw_name = "gp0" },
	{ .fw_name = "hifi" },
	{ .fw_name = "fdiv2p5" },
	{ .fw_name = "fdiv7" }
};

static C3_COMP_SEL(vout_enc, VOUTENC_CLK_CTRL, 25, 0x7, c3_vout_enc_parents);
static C3_COMP_DIV(vout_enc, VOUTENC_CLK_CTRL, 16, 7);
static C3_COMP_GATE(vout_enc, VOUTENC_CLK_CTRL, 24);

static const struct clk_parent_data c3_hcodec_pre_parents[] = {
	{ .fw_name = "fdiv2p5" },
	{ .fw_name = "fdiv3" },
	{ .fw_name = "fdiv4" },
	{ .fw_name = "fdiv5" },
	{ .fw_name = "fdiv7" },
	{ .fw_name = "hifi" },
	{ .fw_name = "gp0" },
	{ .fw_name = "oscin" }
};

static C3_COMP_SEL(hcodec_0,  VDEC_CLK_CTRL, 9, 0x7, c3_hcodec_pre_parents);
static C3_COMP_DIV(hcodec_0,  VDEC_CLK_CTRL, 0, 7);
static C3_COMP_GATE(hcodec_0,  VDEC_CLK_CTRL, 8);

static C3_COMP_SEL(hcodec_1, VDEC3_CLK_CTRL, 9, 0x7, c3_hcodec_pre_parents);
static C3_COMP_DIV(hcodec_1, VDEC3_CLK_CTRL, 0, 7);
static C3_COMP_GATE(hcodec_1, VDEC3_CLK_CTRL, 8);

static const struct clk_parent_data c3_hcodec_parents[] = {
	{ .hw = &c3_hcodec_0.hw },
	{ .hw = &c3_hcodec_1.hw }
};

static struct clk_regmap c3_hcodec = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = VDEC3_CLK_CTRL,
		.mask = 0x1,
		.shift = 15,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "hcodec",
		.ops = &clk_regmap_mux_ops,
		.parent_data = c3_hcodec_parents,
		.num_parents = ARRAY_SIZE(c3_hcodec_parents),
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data c3_vc9000e_parents[] = {
	{ .fw_name = "oscin" },
	{ .fw_name = "fdiv4" },
	{ .fw_name = "fdiv3" },
	{ .fw_name = "fdiv5" },
	{ .fw_name = "fdiv7" },
	{ .fw_name = "fdiv2p5" },
	{ .fw_name = "hifi" },
	{ .fw_name = "gp0" }
};

static C3_COMP_SEL(vc9000e_aclk, VC9000E_CLK_CTRL, 9, 0x7, c3_vc9000e_parents);
static C3_COMP_DIV(vc9000e_aclk, VC9000E_CLK_CTRL, 0, 7);
static C3_COMP_GATE(vc9000e_aclk, VC9000E_CLK_CTRL, 8);

static C3_COMP_SEL(vc9000e_core, VC9000E_CLK_CTRL, 25, 0x7, c3_vc9000e_parents);
static C3_COMP_DIV(vc9000e_core, VC9000E_CLK_CTRL, 16, 7);
static C3_COMP_GATE(vc9000e_core, VC9000E_CLK_CTRL, 24);

static const struct clk_parent_data c3_csi_phy_parents[] = {
	{ .fw_name = "fdiv2p5" },
	{ .fw_name = "fdiv3" },
	{ .fw_name = "fdiv4" },
	{ .fw_name = "fdiv5" },
	{ .fw_name = "gp0" },
	{ .fw_name = "hifi" },
	{ .fw_name = "gp1" },
	{ .fw_name = "oscin" }
};

static C3_COMP_SEL(csi_phy0, ISP0_CLK_CTRL, 25, 0x7, c3_csi_phy_parents);
static C3_COMP_DIV(csi_phy0, ISP0_CLK_CTRL, 16, 7);
static C3_COMP_GATE(csi_phy0, ISP0_CLK_CTRL, 24);

static const struct clk_parent_data c3_dewarpa_parents[] = {
	{ .fw_name = "fdiv2p5" },
	{ .fw_name = "fdiv3" },
	{ .fw_name = "fdiv4" },
	{ .fw_name = "fdiv5" },
	{ .fw_name = "gp0" },
	{ .fw_name = "hifi" },
	{ .fw_name = "gp1" },
	{ .fw_name = "fdiv7" }
};

static C3_COMP_SEL(dewarpa, DEWARPA_CLK_CTRL, 9, 0x7, c3_dewarpa_parents);
static C3_COMP_DIV(dewarpa, DEWARPA_CLK_CTRL, 0, 7);
static C3_COMP_GATE(dewarpa, DEWARPA_CLK_CTRL, 8);

static const struct clk_parent_data c3_isp_parents[] = {
	{ .fw_name = "fdiv2p5" },
	{ .fw_name = "fdiv3" },
	{ .fw_name = "fdiv4" },
	{ .fw_name = "fdiv5" },
	{ .fw_name = "gp0" },
	{ .fw_name = "hifi" },
	{ .fw_name = "gp1" },
	{ .fw_name = "oscin" }
};

static C3_COMP_SEL(isp0, ISP0_CLK_CTRL, 9, 0x7, c3_isp_parents);
static C3_COMP_DIV(isp0, ISP0_CLK_CTRL, 0, 7);
static C3_COMP_GATE(isp0, ISP0_CLK_CTRL, 8);

static const struct clk_parent_data c3_nna_core_parents[] = {
	{ .fw_name = "oscin" },
	{ .fw_name = "fdiv2p5" },
	{ .fw_name = "fdiv4" },
	{ .fw_name = "fdiv3" },
	{ .fw_name = "fdiv5" },
	{ .fw_name = "fdiv2" },
	{ .fw_name = "gp1" },
	{ .fw_name = "hifi" }
};

static C3_COMP_SEL(nna_core, NNA_CLK_CTRL, 9, 0x7, c3_nna_core_parents);
static C3_COMP_DIV(nna_core, NNA_CLK_CTRL, 0, 7);
static C3_COMP_GATE(nna_core, NNA_CLK_CTRL, 8);

static const struct clk_parent_data c3_ge2d_parents[] = {
	{ .fw_name = "oscin" },
	{ .fw_name = "fdiv2p5" },
	{ .fw_name = "fdiv3" },
	{ .fw_name = "fdiv4" },
	{ .fw_name = "hifi" },
	{ .fw_name = "fdiv5" },
	{ .fw_name = "gp0" },
	{ .hw = &c3_rtc_clk.hw }
};

static C3_COMP_SEL(ge2d, GE2D_CLK_CTRL, 9, 0x7, c3_ge2d_parents);
static C3_COMP_DIV(ge2d, GE2D_CLK_CTRL, 0, 7);
static C3_COMP_GATE(ge2d, GE2D_CLK_CTRL, 8);

static const struct clk_parent_data c3_vapb_parents[] = {
	{ .fw_name = "fdiv2p5" },
	{ .fw_name = "fdiv3" },
	{ .fw_name = "fdiv4" },
	{ .fw_name = "fdiv5" },
	{ .fw_name = "gp0" },
	{ .fw_name = "hifi" },
	{ .fw_name = "gp1" },
	{ .fw_name = "oscin" },
};

static C3_COMP_SEL(vapb, VAPB_CLK_CTRL, 9, 0x7, c3_vapb_parents);
static C3_COMP_DIV(vapb, VAPB_CLK_CTRL, 0, 7);
static C3_COMP_GATE(vapb, VAPB_CLK_CTRL, 8);

static struct clk_hw *c3_peripherals_hw_clks[] = {
	[CLKID_RTC_XTAL_CLKIN]		= &c3_rtc_xtal_clkin.hw,
	[CLKID_RTC_32K_DIV]		= &c3_rtc_32k_div.hw,
	[CLKID_RTC_32K_MUX]		= &c3_rtc_32k_sel.hw,
	[CLKID_RTC_32K]			= &c3_rtc_32k.hw,
	[CLKID_RTC_CLK]			= &c3_rtc_clk.hw,
	[CLKID_SYS_RESET_CTRL]		= &c3_sys_reset_ctrl.hw,
	[CLKID_SYS_PWR_CTRL]		= &c3_sys_pwr_ctrl.hw,
	[CLKID_SYS_PAD_CTRL]		= &c3_sys_pad_ctrl.hw,
	[CLKID_SYS_CTRL]		= &c3_sys_ctrl.hw,
	[CLKID_SYS_TS_PLL]		= &c3_sys_ts_pll.hw,
	[CLKID_SYS_DEV_ARB]		= &c3_sys_dev_arb.hw,
	[CLKID_SYS_MMC_PCLK]		= &c3_sys_mmc_pclk.hw,
	[CLKID_SYS_CPU_CTRL]		= &c3_sys_cpu_ctrl.hw,
	[CLKID_SYS_JTAG_CTRL]		= &c3_sys_jtag_ctrl.hw,
	[CLKID_SYS_IR_CTRL]		= &c3_sys_ir_ctrl.hw,
	[CLKID_SYS_IRQ_CTRL]		= &c3_sys_irq_ctrl.hw,
	[CLKID_SYS_MSR_CLK]		= &c3_sys_msr_clk.hw,
	[CLKID_SYS_ROM]			= &c3_sys_rom.hw,
	[CLKID_SYS_UART_F]		= &c3_sys_uart_f.hw,
	[CLKID_SYS_CPU_ARB]		= &c3_sys_cpu_apb.hw,
	[CLKID_SYS_RSA]			= &c3_sys_rsa.hw,
	[CLKID_SYS_SAR_ADC]		= &c3_sys_sar_adc.hw,
	[CLKID_SYS_STARTUP]		= &c3_sys_startup.hw,
	[CLKID_SYS_SECURE]		= &c3_sys_secure.hw,
	[CLKID_SYS_SPIFC]		= &c3_sys_spifc.hw,
	[CLKID_SYS_NNA]			= &c3_sys_nna.hw,
	[CLKID_SYS_ETH_MAC]		= &c3_sys_eth_mac.hw,
	[CLKID_SYS_GIC]			= &c3_sys_gic.hw,
	[CLKID_SYS_RAMA]		= &c3_sys_rama.hw,
	[CLKID_SYS_BIG_NIC]		= &c3_sys_big_nic.hw,
	[CLKID_SYS_RAMB]		= &c3_sys_ramb.hw,
	[CLKID_SYS_AUDIO_PCLK]		= &c3_sys_audio_pclk.hw,
	[CLKID_SYS_PWM_KL]		= &c3_sys_pwm_kl.hw,
	[CLKID_SYS_PWM_IJ]		= &c3_sys_pwm_ij.hw,
	[CLKID_SYS_USB]			= &c3_sys_usb.hw,
	[CLKID_SYS_SD_EMMC_A]		= &c3_sys_sd_emmc_a.hw,
	[CLKID_SYS_SD_EMMC_C]		= &c3_sys_sd_emmc_c.hw,
	[CLKID_SYS_PWM_AB]		= &c3_sys_pwm_ab.hw,
	[CLKID_SYS_PWM_CD]		= &c3_sys_pwm_cd.hw,
	[CLKID_SYS_PWM_EF]		= &c3_sys_pwm_ef.hw,
	[CLKID_SYS_PWM_GH]		= &c3_sys_pwm_gh.hw,
	[CLKID_SYS_SPICC_1]		= &c3_sys_spicc_1.hw,
	[CLKID_SYS_SPICC_0]		= &c3_sys_spicc_0.hw,
	[CLKID_SYS_UART_A]		= &c3_sys_uart_a.hw,
	[CLKID_SYS_UART_B]		= &c3_sys_uart_b.hw,
	[CLKID_SYS_UART_C]		= &c3_sys_uart_c.hw,
	[CLKID_SYS_UART_D]		= &c3_sys_uart_d.hw,
	[CLKID_SYS_UART_E]		= &c3_sys_uart_e.hw,
	[CLKID_SYS_I2C_M_A]		= &c3_sys_i2c_m_a.hw,
	[CLKID_SYS_I2C_M_B]		= &c3_sys_i2c_m_b.hw,
	[CLKID_SYS_I2C_M_C]		= &c3_sys_i2c_m_c.hw,
	[CLKID_SYS_I2C_M_D]		= &c3_sys_i2c_m_d.hw,
	[CLKID_SYS_I2S_S_A]		= &c3_sys_i2c_s_a.hw,
	[CLKID_SYS_RTC]			= &c3_sys_rtc.hw,
	[CLKID_SYS_GE2D]		= &c3_sys_ge2d.hw,
	[CLKID_SYS_ISP]			= &c3_sys_isp.hw,
	[CLKID_SYS_GPV_ISP_NIC]		= &c3_sys_gpv_isp_nic.hw,
	[CLKID_SYS_GPV_CVE_NIC]		= &c3_sys_gpv_cve_nic.hw,
	[CLKID_SYS_MIPI_DSI_HOST]	= &c3_sys_mipi_dsi_host.hw,
	[CLKID_SYS_MIPI_DSI_PHY]	= &c3_sys_mipi_dsi_phy.hw,
	[CLKID_SYS_ETH_PHY]		= &c3_sys_eth_phy.hw,
	[CLKID_SYS_ACODEC]		= &c3_sys_acodec.hw,
	[CLKID_SYS_DWAP]		= &c3_sys_dwap.hw,
	[CLKID_SYS_DOS]			= &c3_sys_dos.hw,
	[CLKID_SYS_CVE]			= &c3_sys_cve.hw,
	[CLKID_SYS_VOUT]		= &c3_sys_vout.hw,
	[CLKID_SYS_VC9000E]		= &c3_sys_vc9000e.hw,
	[CLKID_SYS_PWM_MN]		= &c3_sys_pwm_mn.hw,
	[CLKID_SYS_SD_EMMC_B]		= &c3_sys_sd_emmc_b.hw,
	[CLKID_AXI_SYS_NIC]		= &c3_axi_sys_nic.hw,
	[CLKID_AXI_ISP_NIC]		= &c3_axi_isp_nic.hw,
	[CLKID_AXI_CVE_NIC]		= &c3_axi_cve_nic.hw,
	[CLKID_AXI_RAMB]		= &c3_axi_ramb.hw,
	[CLKID_AXI_RAMA]		= &c3_axi_rama.hw,
	[CLKID_AXI_CPU_DMC]		= &c3_axi_cpu_dmc.hw,
	[CLKID_AXI_NIC]			= &c3_axi_nic.hw,
	[CLKID_AXI_DMA]			= &c3_axi_dma.hw,
	[CLKID_AXI_MUX_NIC]		= &c3_axi_mux_nic.hw,
	[CLKID_AXI_CVE]			= &c3_axi_cve.hw,
	[CLKID_AXI_DEV1_DMC]		= &c3_axi_dev1_dmc.hw,
	[CLKID_AXI_DEV0_DMC]		= &c3_axi_dev0_dmc.hw,
	[CLKID_AXI_DSP_DMC]		= &c3_axi_dsp_dmc.hw,
	[CLKID_12_24M_IN]		= &c3_clk_12_24m_in.hw,
	[CLKID_12M_24M]			= &c3_clk_12_24m.hw,
	[CLKID_FCLK_25M_DIV]		= &c3_fclk_25m_div.hw,
	[CLKID_FCLK_25M]		= &c3_fclk_25m.hw,
	[CLKID_GEN_SEL]			= &c3_gen_sel.hw,
	[CLKID_GEN_DIV]			= &c3_gen_div.hw,
	[CLKID_GEN]			= &c3_gen.hw,
	[CLKID_SARADC_SEL]		= &c3_saradc_sel.hw,
	[CLKID_SARADC_DIV]		= &c3_saradc_div.hw,
	[CLKID_SARADC]			= &c3_saradc.hw,
	[CLKID_PWM_A_SEL]		= &c3_pwm_a_sel.hw,
	[CLKID_PWM_A_DIV]		= &c3_pwm_a_div.hw,
	[CLKID_PWM_A]			= &c3_pwm_a.hw,
	[CLKID_PWM_B_SEL]		= &c3_pwm_b_sel.hw,
	[CLKID_PWM_B_DIV]		= &c3_pwm_b_div.hw,
	[CLKID_PWM_B]			= &c3_pwm_b.hw,
	[CLKID_PWM_C_SEL]		= &c3_pwm_c_sel.hw,
	[CLKID_PWM_C_DIV]		= &c3_pwm_c_div.hw,
	[CLKID_PWM_C]			= &c3_pwm_c.hw,
	[CLKID_PWM_D_SEL]		= &c3_pwm_d_sel.hw,
	[CLKID_PWM_D_DIV]		= &c3_pwm_d_div.hw,
	[CLKID_PWM_D]			= &c3_pwm_d.hw,
	[CLKID_PWM_E_SEL]		= &c3_pwm_e_sel.hw,
	[CLKID_PWM_E_DIV]		= &c3_pwm_e_div.hw,
	[CLKID_PWM_E]			= &c3_pwm_e.hw,
	[CLKID_PWM_F_SEL]		= &c3_pwm_f_sel.hw,
	[CLKID_PWM_F_DIV]		= &c3_pwm_f_div.hw,
	[CLKID_PWM_F]			= &c3_pwm_f.hw,
	[CLKID_PWM_G_SEL]		= &c3_pwm_g_sel.hw,
	[CLKID_PWM_G_DIV]		= &c3_pwm_g_div.hw,
	[CLKID_PWM_G]			= &c3_pwm_g.hw,
	[CLKID_PWM_H_SEL]		= &c3_pwm_h_sel.hw,
	[CLKID_PWM_H_DIV]		= &c3_pwm_h_div.hw,
	[CLKID_PWM_H]			= &c3_pwm_h.hw,
	[CLKID_PWM_I_SEL]		= &c3_pwm_i_sel.hw,
	[CLKID_PWM_I_DIV]		= &c3_pwm_i_div.hw,
	[CLKID_PWM_I]			= &c3_pwm_i.hw,
	[CLKID_PWM_J_SEL]		= &c3_pwm_j_sel.hw,
	[CLKID_PWM_J_DIV]		= &c3_pwm_j_div.hw,
	[CLKID_PWM_J]			= &c3_pwm_j.hw,
	[CLKID_PWM_K_SEL]		= &c3_pwm_k_sel.hw,
	[CLKID_PWM_K_DIV]		= &c3_pwm_k_div.hw,
	[CLKID_PWM_K]			= &c3_pwm_k.hw,
	[CLKID_PWM_L_SEL]		= &c3_pwm_l_sel.hw,
	[CLKID_PWM_L_DIV]		= &c3_pwm_l_div.hw,
	[CLKID_PWM_L]			= &c3_pwm_l.hw,
	[CLKID_PWM_M_SEL]		= &c3_pwm_m_sel.hw,
	[CLKID_PWM_M_DIV]		= &c3_pwm_m_div.hw,
	[CLKID_PWM_M]			= &c3_pwm_m.hw,
	[CLKID_PWM_N_SEL]		= &c3_pwm_n_sel.hw,
	[CLKID_PWM_N_DIV]		= &c3_pwm_n_div.hw,
	[CLKID_PWM_N]			= &c3_pwm_n.hw,
	[CLKID_SPICC_A_SEL]		= &c3_spicc_a_sel.hw,
	[CLKID_SPICC_A_DIV]		= &c3_spicc_a_div.hw,
	[CLKID_SPICC_A]			= &c3_spicc_a.hw,
	[CLKID_SPICC_B_SEL]		= &c3_spicc_b_sel.hw,
	[CLKID_SPICC_B_DIV]		= &c3_spicc_b_div.hw,
	[CLKID_SPICC_B]			= &c3_spicc_b.hw,
	[CLKID_SPIFC_SEL]		= &c3_spifc_sel.hw,
	[CLKID_SPIFC_DIV]		= &c3_spifc_div.hw,
	[CLKID_SPIFC]			= &c3_spifc.hw,
	[CLKID_SD_EMMC_A_SEL]		= &c3_sd_emmc_a_sel.hw,
	[CLKID_SD_EMMC_A_DIV]		= &c3_sd_emmc_a_div.hw,
	[CLKID_SD_EMMC_A]		= &c3_sd_emmc_a.hw,
	[CLKID_SD_EMMC_B_SEL]		= &c3_sd_emmc_b_sel.hw,
	[CLKID_SD_EMMC_B_DIV]		= &c3_sd_emmc_b_div.hw,
	[CLKID_SD_EMMC_B]		= &c3_sd_emmc_b.hw,
	[CLKID_SD_EMMC_C_SEL]		= &c3_sd_emmc_c_sel.hw,
	[CLKID_SD_EMMC_C_DIV]		= &c3_sd_emmc_c_div.hw,
	[CLKID_SD_EMMC_C]		= &c3_sd_emmc_c.hw,
	[CLKID_TS_DIV]			= &c3_ts_div.hw,
	[CLKID_TS]			= &c3_ts.hw,
	[CLKID_ETH_125M_DIV]		= &c3_eth_125m_div.hw,
	[CLKID_ETH_125M]		= &c3_eth_125m.hw,
	[CLKID_ETH_RMII_DIV]		= &c3_eth_rmii_div.hw,
	[CLKID_ETH_RMII]		= &c3_eth_rmii.hw,
	[CLKID_MIPI_DSI_MEAS_SEL]	= &c3_mipi_dsi_meas_sel.hw,
	[CLKID_MIPI_DSI_MEAS_DIV]	= &c3_mipi_dsi_meas_div.hw,
	[CLKID_MIPI_DSI_MEAS]		= &c3_mipi_dsi_meas.hw,
	[CLKID_DSI_PHY_SEL]		= &c3_dsi_phy_sel.hw,
	[CLKID_DSI_PHY_DIV]		= &c3_dsi_phy_div.hw,
	[CLKID_DSI_PHY]			= &c3_dsi_phy.hw,
	[CLKID_VOUT_MCLK_SEL]		= &c3_vout_mclk_sel.hw,
	[CLKID_VOUT_MCLK_DIV]		= &c3_vout_mclk_div.hw,
	[CLKID_VOUT_MCLK]		= &c3_vout_mclk.hw,
	[CLKID_VOUT_ENC_SEL]		= &c3_vout_enc_sel.hw,
	[CLKID_VOUT_ENC_DIV]		= &c3_vout_enc_div.hw,
	[CLKID_VOUT_ENC]		= &c3_vout_enc.hw,
	[CLKID_HCODEC_0_SEL]		= &c3_hcodec_0_sel.hw,
	[CLKID_HCODEC_0_DIV]		= &c3_hcodec_0_div.hw,
	[CLKID_HCODEC_0]		= &c3_hcodec_0.hw,
	[CLKID_HCODEC_1_SEL]		= &c3_hcodec_1_sel.hw,
	[CLKID_HCODEC_1_DIV]		= &c3_hcodec_1_div.hw,
	[CLKID_HCODEC_1]		= &c3_hcodec_1.hw,
	[CLKID_HCODEC]			= &c3_hcodec.hw,
	[CLKID_VC9000E_ACLK_SEL]	= &c3_vc9000e_aclk_sel.hw,
	[CLKID_VC9000E_ACLK_DIV]	= &c3_vc9000e_aclk_div.hw,
	[CLKID_VC9000E_ACLK]		= &c3_vc9000e_aclk.hw,
	[CLKID_VC9000E_CORE_SEL]	= &c3_vc9000e_core_sel.hw,
	[CLKID_VC9000E_CORE_DIV]	= &c3_vc9000e_core_div.hw,
	[CLKID_VC9000E_CORE]		= &c3_vc9000e_core.hw,
	[CLKID_CSI_PHY0_SEL]		= &c3_csi_phy0_sel.hw,
	[CLKID_CSI_PHY0_DIV]		= &c3_csi_phy0_div.hw,
	[CLKID_CSI_PHY0]		= &c3_csi_phy0.hw,
	[CLKID_DEWARPA_SEL]		= &c3_dewarpa_sel.hw,
	[CLKID_DEWARPA_DIV]		= &c3_dewarpa_div.hw,
	[CLKID_DEWARPA]			= &c3_dewarpa.hw,
	[CLKID_ISP0_SEL]		= &c3_isp0_sel.hw,
	[CLKID_ISP0_DIV]		= &c3_isp0_div.hw,
	[CLKID_ISP0]			= &c3_isp0.hw,
	[CLKID_NNA_CORE_SEL]		= &c3_nna_core_sel.hw,
	[CLKID_NNA_CORE_DIV]		= &c3_nna_core_div.hw,
	[CLKID_NNA_CORE]		= &c3_nna_core.hw,
	[CLKID_GE2D_SEL]		= &c3_ge2d_sel.hw,
	[CLKID_GE2D_DIV]		= &c3_ge2d_div.hw,
	[CLKID_GE2D]			= &c3_ge2d.hw,
	[CLKID_VAPB_SEL]		= &c3_vapb_sel.hw,
	[CLKID_VAPB_DIV]		= &c3_vapb_div.hw,
	[CLKID_VAPB]			= &c3_vapb.hw,
};

static const struct meson_clkc_data c3_peripherals_clkc_data = {
	.hw_clks = {
		.hws = c3_peripherals_hw_clks,
		.num = ARRAY_SIZE(c3_peripherals_hw_clks),
	},
};

static const struct of_device_id c3_peripherals_clkc_match_table[] = {
	{
		.compatible = "amlogic,c3-peripherals-clkc",
		.data = &c3_peripherals_clkc_data,
	},
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, c3_peripherals_clkc_match_table);

static struct platform_driver c3_peripherals_clkc_driver = {
	.probe		= meson_clkc_mmio_probe,
	.driver		= {
		.name	= "c3-peripherals-clkc",
		.of_match_table = c3_peripherals_clkc_match_table,
	},
};
module_platform_driver(c3_peripherals_clkc_driver);

MODULE_DESCRIPTION("Amlogic C3 Peripherals Clock Controller driver");
MODULE_AUTHOR("Chuan Liu <chuan.liu@amlogic.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("CLK_MESON");
