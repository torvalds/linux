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

static struct clk_regmap rtc_xtal_clkin = {
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

static const struct meson_clk_dualdiv_param rtc_32k_div_table[] = {
	{ 733, 732, 8, 11, 1 },
	{ /* sentinel */ }
};

static struct clk_regmap rtc_32k_div = {
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
		.table = rtc_32k_div_table,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "rtc_32k_div",
		.ops = &meson_clk_dualdiv_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&rtc_xtal_clkin.hw
		},
		.num_parents = 1,
	},
};

static const struct clk_parent_data rtc_32k_mux_parent_data[] = {
	{ .hw = &rtc_32k_div.hw },
	{ .hw = &rtc_xtal_clkin.hw }
};

static struct clk_regmap rtc_32k_mux = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = RTC_BY_OSCIN_CTRL1,
		.mask = 0x1,
		.shift = 24,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "rtc_32k_mux",
		.ops = &clk_regmap_mux_ops,
		.parent_data = rtc_32k_mux_parent_data,
		.num_parents = ARRAY_SIZE(rtc_32k_mux_parent_data),
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap rtc_32k = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = RTC_BY_OSCIN_CTRL0,
		.bit_idx = 30,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "rtc_32k",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&rtc_32k_mux.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data rtc_clk_mux_parent_data[] = {
	{ .fw_name = "oscin" },
	{ .hw = &rtc_32k.hw },
	{ .fw_name = "pad_osc" }
};

static struct clk_regmap rtc_clk = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = RTC_CTRL,
		.mask = 0x3,
		.shift = 0,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "rtc_clk",
		.ops = &clk_regmap_mux_ops,
		.parent_data = rtc_clk_mux_parent_data,
		.num_parents = ARRAY_SIZE(rtc_clk_mux_parent_data),
		.flags = CLK_SET_RATE_PARENT,
	},
};

#define C3_CLK_GATE(_name, _reg, _bit, _fw_name, _ops, _flags)		\
struct clk_regmap _name = {						\
	.data = &(struct clk_regmap_gate_data){				\
		.offset = (_reg),					\
		.bit_idx = (_bit),					\
	},								\
	.hw.init = &(struct clk_init_data) {				\
		.name = #_name,						\
		.ops = _ops,						\
		.parent_data = &(const struct clk_parent_data) {	\
			.fw_name = #_fw_name,				\
		},							\
		.num_parents = 1,					\
		.flags = (_flags),					\
	},								\
}

#define C3_SYS_GATE(_name, _reg, _bit, _flags)				\
	C3_CLK_GATE(_name, _reg, _bit, sysclk,				\
		    &clk_regmap_gate_ops, _flags)

#define C3_SYS_GATE_RO(_name, _reg, _bit)				\
	C3_CLK_GATE(_name, _reg, _bit, sysclk,				\
		    &clk_regmap_gate_ro_ops, 0)

static C3_SYS_GATE(sys_reset_ctrl,	SYS_CLK_EN0_REG0, 1, 0);
static C3_SYS_GATE(sys_pwr_ctrl,	SYS_CLK_EN0_REG0, 3, 0);
static C3_SYS_GATE(sys_pad_ctrl,	SYS_CLK_EN0_REG0, 4, 0);
static C3_SYS_GATE(sys_ctrl,		SYS_CLK_EN0_REG0, 5, 0);
static C3_SYS_GATE(sys_ts_pll,		SYS_CLK_EN0_REG0, 6, 0);

/*
 * NOTE: sys_dev_arb provides the clock to the ETH and SPICC arbiters that
 * access the AXI bus.
 */
static C3_SYS_GATE(sys_dev_arb,		SYS_CLK_EN0_REG0, 7, 0);

/*
 * FIXME: sys_mmc_pclk provides the clock for the DDR PHY, DDR will only be
 * initialized in bl2, and this clock should not be touched in linux.
 */
static C3_SYS_GATE_RO(sys_mmc_pclk,	SYS_CLK_EN0_REG0, 8);

/*
 * NOTE: sys_cpu_ctrl provides the clock for CPU controller. After clock is
 * disabled, cpu_clk and other key CPU-related configurations cannot take effect.
 */
static C3_SYS_GATE(sys_cpu_ctrl,	SYS_CLK_EN0_REG0, 11, CLK_IS_CRITICAL);
static C3_SYS_GATE(sys_jtag_ctrl,	SYS_CLK_EN0_REG0, 12, 0);
static C3_SYS_GATE(sys_ir_ctrl,		SYS_CLK_EN0_REG0, 13, 0);

/*
 * NOTE: sys_irq_ctrl provides the clock for IRQ controller. The IRQ controller
 * collects and distributes the interrupt signal to the GIC, PWR_CTRL, and
 * AOCPU. If the clock is disabled, interrupt-related functions will occurs an
 * exception.
 */
static C3_SYS_GATE(sys_irq_ctrl,	SYS_CLK_EN0_REG0, 14, CLK_IS_CRITICAL);
static C3_SYS_GATE(sys_msr_clk,		SYS_CLK_EN0_REG0, 15, 0);
static C3_SYS_GATE(sys_rom,		SYS_CLK_EN0_REG0, 16, 0);
static C3_SYS_GATE(sys_uart_f,		SYS_CLK_EN0_REG0, 17, 0);
static C3_SYS_GATE(sys_cpu_apb,		SYS_CLK_EN0_REG0, 18, 0);
static C3_SYS_GATE(sys_rsa,		SYS_CLK_EN0_REG0, 19, 0);
static C3_SYS_GATE(sys_sar_adc,		SYS_CLK_EN0_REG0, 20, 0);
static C3_SYS_GATE(sys_startup,		SYS_CLK_EN0_REG0, 21, 0);
static C3_SYS_GATE(sys_secure,		SYS_CLK_EN0_REG0, 22, 0);
static C3_SYS_GATE(sys_spifc,		SYS_CLK_EN0_REG0, 23, 0);
static C3_SYS_GATE(sys_nna,		SYS_CLK_EN0_REG0, 25, 0);
static C3_SYS_GATE(sys_eth_mac,		SYS_CLK_EN0_REG0, 26, 0);

/*
 * FIXME: sys_gic provides the clock for GIC(Generic Interrupt Controller).
 * After clock is disabled, The GIC cannot work properly. At present, the driver
 * used by our GIC is the public driver in kernel, and there is no management
 * clock in the driver.
 */
static C3_SYS_GATE(sys_gic,		SYS_CLK_EN0_REG0, 27, CLK_IS_CRITICAL);
static C3_SYS_GATE(sys_rama,		SYS_CLK_EN0_REG0, 28, 0);

/*
 * NOTE: sys_big_nic provides the clock to the control bus of the NIC(Network
 * Interface Controller) between multiple devices(CPU, DDR, RAM, ROM, GIC,
 * SPIFC, CAPU, JTAG, EMMC, SDIO, sec_top, USB, Audio, ETH, SPICC) in the
 * system. After clock is disabled, The NIC cannot work.
 */
static C3_SYS_GATE(sys_big_nic,		SYS_CLK_EN0_REG0, 29, CLK_IS_CRITICAL);
static C3_SYS_GATE(sys_ramb,		SYS_CLK_EN0_REG0, 30, 0);
static C3_SYS_GATE(sys_audio_pclk,	SYS_CLK_EN0_REG0, 31, 0);
static C3_SYS_GATE(sys_pwm_kl,		SYS_CLK_EN0_REG1, 0, 0);
static C3_SYS_GATE(sys_pwm_ij,		SYS_CLK_EN0_REG1, 1, 0);
static C3_SYS_GATE(sys_usb,		SYS_CLK_EN0_REG1, 2, 0);
static C3_SYS_GATE(sys_sd_emmc_a,	SYS_CLK_EN0_REG1, 3, 0);
static C3_SYS_GATE(sys_sd_emmc_c,	SYS_CLK_EN0_REG1, 4, 0);
static C3_SYS_GATE(sys_pwm_ab,		SYS_CLK_EN0_REG1, 5, 0);
static C3_SYS_GATE(sys_pwm_cd,		SYS_CLK_EN0_REG1, 6, 0);
static C3_SYS_GATE(sys_pwm_ef,		SYS_CLK_EN0_REG1, 7, 0);
static C3_SYS_GATE(sys_pwm_gh,		SYS_CLK_EN0_REG1, 8, 0);
static C3_SYS_GATE(sys_spicc_1,		SYS_CLK_EN0_REG1, 9, 0);
static C3_SYS_GATE(sys_spicc_0,		SYS_CLK_EN0_REG1, 10, 0);
static C3_SYS_GATE(sys_uart_a,		SYS_CLK_EN0_REG1, 11, 0);
static C3_SYS_GATE(sys_uart_b,		SYS_CLK_EN0_REG1, 12, 0);
static C3_SYS_GATE(sys_uart_c,		SYS_CLK_EN0_REG1, 13, 0);
static C3_SYS_GATE(sys_uart_d,		SYS_CLK_EN0_REG1, 14, 0);
static C3_SYS_GATE(sys_uart_e,		SYS_CLK_EN0_REG1, 15, 0);
static C3_SYS_GATE(sys_i2c_m_a,		SYS_CLK_EN0_REG1, 16, 0);
static C3_SYS_GATE(sys_i2c_m_b,		SYS_CLK_EN0_REG1, 17, 0);
static C3_SYS_GATE(sys_i2c_m_c,		SYS_CLK_EN0_REG1, 18, 0);
static C3_SYS_GATE(sys_i2c_m_d,		SYS_CLK_EN0_REG1, 19, 0);
static C3_SYS_GATE(sys_i2c_s_a,		SYS_CLK_EN0_REG1, 20, 0);
static C3_SYS_GATE(sys_rtc,		SYS_CLK_EN0_REG1, 21, 0);
static C3_SYS_GATE(sys_ge2d,		SYS_CLK_EN0_REG1, 22, 0);
static C3_SYS_GATE(sys_isp,		SYS_CLK_EN0_REG1, 23, 0);
static C3_SYS_GATE(sys_gpv_isp_nic,	SYS_CLK_EN0_REG1, 24, 0);
static C3_SYS_GATE(sys_gpv_cve_nic,	SYS_CLK_EN0_REG1, 25, 0);
static C3_SYS_GATE(sys_mipi_dsi_host,	SYS_CLK_EN0_REG1, 26, 0);
static C3_SYS_GATE(sys_mipi_dsi_phy,	SYS_CLK_EN0_REG1, 27, 0);
static C3_SYS_GATE(sys_eth_phy,		SYS_CLK_EN0_REG1, 28, 0);
static C3_SYS_GATE(sys_acodec,		SYS_CLK_EN0_REG1, 29, 0);
static C3_SYS_GATE(sys_dwap,		SYS_CLK_EN0_REG1, 30, 0);
static C3_SYS_GATE(sys_dos,		SYS_CLK_EN0_REG1, 31, 0);
static C3_SYS_GATE(sys_cve,		SYS_CLK_EN0_REG2, 0, 0);
static C3_SYS_GATE(sys_vout,		SYS_CLK_EN0_REG2, 1, 0);
static C3_SYS_GATE(sys_vc9000e,		SYS_CLK_EN0_REG2, 2, 0);
static C3_SYS_GATE(sys_pwm_mn,		SYS_CLK_EN0_REG2, 3, 0);
static C3_SYS_GATE(sys_sd_emmc_b,	SYS_CLK_EN0_REG2, 4, 0);

#define C3_AXI_GATE(_name, _reg, _bit, _flags)				\
	C3_CLK_GATE(_name, _reg, _bit, axiclk,				\
		    &clk_regmap_gate_ops, _flags)

/*
 * NOTE: axi_sys_nic provides the clock to the AXI bus of the system NIC. After
 * clock is disabled, The NIC cannot work.
 */
static C3_AXI_GATE(axi_sys_nic,		AXI_CLK_EN0, 2, CLK_IS_CRITICAL);
static C3_AXI_GATE(axi_isp_nic,		AXI_CLK_EN0, 3, 0);
static C3_AXI_GATE(axi_cve_nic,		AXI_CLK_EN0, 4, 0);
static C3_AXI_GATE(axi_ramb,		AXI_CLK_EN0, 5, 0);
static C3_AXI_GATE(axi_rama,		AXI_CLK_EN0, 6, 0);

/*
 * NOTE: axi_cpu_dmc provides the clock to the AXI bus where the CPU accesses
 * the DDR. After clock is disabled, The CPU will not have access to the DDR.
 */
static C3_AXI_GATE(axi_cpu_dmc,		AXI_CLK_EN0, 7, CLK_IS_CRITICAL);
static C3_AXI_GATE(axi_nic,		AXI_CLK_EN0, 8, 0);
static C3_AXI_GATE(axi_dma,		AXI_CLK_EN0, 9, 0);

/*
 * NOTE: axi_mux_nic provides the clock to the NIC's AXI bus for NN(Neural
 * Network) and other devices(CPU, EMMC, SDIO, sec_top, USB, Audio, ETH, SPICC)
 * to access RAM space.
 */
static C3_AXI_GATE(axi_mux_nic,		AXI_CLK_EN0, 10, 0);
static C3_AXI_GATE(axi_cve,		AXI_CLK_EN0, 12, 0);

/*
 * NOTE: axi_dev1_dmc provides the clock for the peripherals(EMMC, SDIO,
 * sec_top, USB, Audio, ETH, SPICC) to access the AXI bus of the DDR.
 */
static C3_AXI_GATE(axi_dev1_dmc,	AXI_CLK_EN0, 13, 0);
static C3_AXI_GATE(axi_dev0_dmc,	AXI_CLK_EN0, 14, 0);
static C3_AXI_GATE(axi_dsp_dmc,		AXI_CLK_EN0, 15, 0);

/*
 * clk_12_24m model
 *
 *          |------|     |-----| clk_12m_24m |-----|
 * xtal---->| gate |---->| div |------------>| pad |
 *          |------|     |-----|             |-----|
 */
static struct clk_regmap clk_12_24m_in = {
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

static struct clk_regmap clk_12_24m = {
	.data = &(struct clk_regmap_div_data) {
		.offset = CLK12_24_CTRL,
		.shift = 10,
		.width = 1,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "clk_12_24m",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&clk_12_24m_in.hw
		},
		.num_parents = 1,
	},
};

/* Fix me: set value 0 will div by 2 like value 1 */
static struct clk_regmap fclk_25m_div = {
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

static struct clk_regmap fclk_25m = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = CLK12_24_CTRL,
		.bit_idx = 12,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "fclk_25m",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&fclk_25m_div.hw
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
static u32 gen_parent_table[] = { 0, 1, 2, 5, 6, 7, 17, 19, 20, 21, 22, 23, 24};

static const struct clk_parent_data gen_parent_data[] = {
	{ .fw_name = "oscin" },
	{ .hw = &rtc_clk.hw },
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

static struct clk_regmap gen_sel = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = GEN_CLK_CTRL,
		.mask = 0x1f,
		.shift = 12,
		.table = gen_parent_table,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "gen_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = gen_parent_data,
		.num_parents = ARRAY_SIZE(gen_parent_data),
	},
};

static struct clk_regmap gen_div = {
	.data = &(struct clk_regmap_div_data) {
		.offset = GEN_CLK_CTRL,
		.shift = 0,
		.width = 11,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "gen_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gen_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap gen = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = GEN_CLK_CTRL,
		.bit_idx = 11,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "gen",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&gen_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data saradc_parent_data[] = {
	{ .fw_name = "oscin" },
	{ .fw_name = "sysclk" }
};

static struct clk_regmap saradc_sel = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = SAR_CLK_CTRL0,
		.mask = 0x1,
		.shift = 9,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "saradc_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = saradc_parent_data,
		.num_parents = ARRAY_SIZE(saradc_parent_data),
	},
};

static struct clk_regmap saradc_div = {
	.data = &(struct clk_regmap_div_data) {
		.offset = SAR_CLK_CTRL0,
		.shift = 0,
		.width = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "saradc_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&saradc_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap saradc = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = SAR_CLK_CTRL0,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "saradc",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&saradc_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data pwm_parent_data[] = {
	{ .fw_name = "oscin" },
	{ .fw_name = "gp1" },
	{ .fw_name = "fdiv4" },
	{ .fw_name = "fdiv3" }
};

#define AML_PWM_CLK_MUX(_name, _reg, _shift) {			\
	.data = &(struct clk_regmap_mux_data) {			\
		.offset = _reg,					\
		.mask = 0x3,					\
		.shift = _shift,				\
	},							\
	.hw.init = &(struct clk_init_data) {			\
		.name = #_name "_sel",				\
		.ops = &clk_regmap_mux_ops,			\
		.parent_data = pwm_parent_data,			\
		.num_parents = ARRAY_SIZE(pwm_parent_data),	\
	},							\
}

#define AML_PWM_CLK_DIV(_name, _reg, _shift) {			\
	.data = &(struct clk_regmap_div_data) {			\
		.offset = _reg,					\
		.shift = _shift,				\
		.width = 8,					\
	},							\
	.hw.init = &(struct clk_init_data) {			\
		.name = #_name "_div",				\
		.ops = &clk_regmap_divider_ops,			\
		.parent_names = (const char *[]) { #_name "_sel" },\
		.num_parents = 1,				\
		.flags = CLK_SET_RATE_PARENT,			\
	},							\
}

#define AML_PWM_CLK_GATE(_name, _reg, _bit) {			\
	.data = &(struct clk_regmap_gate_data) {		\
		.offset = _reg,					\
		.bit_idx = _bit,				\
	},							\
	.hw.init = &(struct clk_init_data) {			\
		.name = #_name,					\
		.ops = &clk_regmap_gate_ops,			\
		.parent_names = (const char *[]) { #_name "_div" },\
		.num_parents = 1,				\
		.flags = CLK_SET_RATE_PARENT,			\
	},							\
}

static struct clk_regmap pwm_a_sel =
	AML_PWM_CLK_MUX(pwm_a, PWM_CLK_AB_CTRL, 9);
static struct clk_regmap pwm_a_div =
	AML_PWM_CLK_DIV(pwm_a, PWM_CLK_AB_CTRL, 0);
static struct clk_regmap pwm_a =
	AML_PWM_CLK_GATE(pwm_a, PWM_CLK_AB_CTRL, 8);

static struct clk_regmap pwm_b_sel =
	AML_PWM_CLK_MUX(pwm_b, PWM_CLK_AB_CTRL, 25);
static struct clk_regmap pwm_b_div =
	AML_PWM_CLK_DIV(pwm_b, PWM_CLK_AB_CTRL, 16);
static struct clk_regmap pwm_b =
	AML_PWM_CLK_GATE(pwm_b, PWM_CLK_AB_CTRL, 24);

static struct clk_regmap pwm_c_sel =
	AML_PWM_CLK_MUX(pwm_c, PWM_CLK_CD_CTRL, 9);
static struct clk_regmap pwm_c_div =
	AML_PWM_CLK_DIV(pwm_c, PWM_CLK_CD_CTRL, 0);
static struct clk_regmap pwm_c =
	AML_PWM_CLK_GATE(pwm_c, PWM_CLK_CD_CTRL, 8);

static struct clk_regmap pwm_d_sel =
	AML_PWM_CLK_MUX(pwm_d, PWM_CLK_CD_CTRL, 25);
static struct clk_regmap pwm_d_div =
	AML_PWM_CLK_DIV(pwm_d, PWM_CLK_CD_CTRL, 16);
static struct clk_regmap pwm_d =
	AML_PWM_CLK_GATE(pwm_d, PWM_CLK_CD_CTRL, 24);

static struct clk_regmap pwm_e_sel =
	AML_PWM_CLK_MUX(pwm_e, PWM_CLK_EF_CTRL, 9);
static struct clk_regmap pwm_e_div =
	AML_PWM_CLK_DIV(pwm_e, PWM_CLK_EF_CTRL, 0);
static struct clk_regmap pwm_e =
	AML_PWM_CLK_GATE(pwm_e, PWM_CLK_EF_CTRL, 8);

static struct clk_regmap pwm_f_sel =
	AML_PWM_CLK_MUX(pwm_f, PWM_CLK_EF_CTRL, 25);
static struct clk_regmap pwm_f_div =
	AML_PWM_CLK_DIV(pwm_f, PWM_CLK_EF_CTRL, 16);
static struct clk_regmap pwm_f =
	AML_PWM_CLK_GATE(pwm_f, PWM_CLK_EF_CTRL, 24);

static struct clk_regmap pwm_g_sel =
	AML_PWM_CLK_MUX(pwm_g, PWM_CLK_GH_CTRL, 9);
static struct clk_regmap pwm_g_div =
	AML_PWM_CLK_DIV(pwm_g, PWM_CLK_GH_CTRL, 0);
static struct clk_regmap pwm_g =
	AML_PWM_CLK_GATE(pwm_g, PWM_CLK_GH_CTRL, 8);

static struct clk_regmap pwm_h_sel =
	AML_PWM_CLK_MUX(pwm_h, PWM_CLK_GH_CTRL, 25);
static struct clk_regmap pwm_h_div =
	AML_PWM_CLK_DIV(pwm_h, PWM_CLK_GH_CTRL, 16);
static struct clk_regmap pwm_h =
	AML_PWM_CLK_GATE(pwm_h, PWM_CLK_GH_CTRL, 24);

static struct clk_regmap pwm_i_sel =
	AML_PWM_CLK_MUX(pwm_i, PWM_CLK_IJ_CTRL, 9);
static struct clk_regmap pwm_i_div =
	AML_PWM_CLK_DIV(pwm_i, PWM_CLK_IJ_CTRL, 0);
static struct clk_regmap pwm_i =
	AML_PWM_CLK_GATE(pwm_i, PWM_CLK_IJ_CTRL, 8);

static struct clk_regmap pwm_j_sel =
	AML_PWM_CLK_MUX(pwm_j, PWM_CLK_IJ_CTRL, 25);
static struct clk_regmap pwm_j_div =
	AML_PWM_CLK_DIV(pwm_j, PWM_CLK_IJ_CTRL, 16);
static struct clk_regmap pwm_j =
	AML_PWM_CLK_GATE(pwm_j, PWM_CLK_IJ_CTRL, 24);

static struct clk_regmap pwm_k_sel =
	AML_PWM_CLK_MUX(pwm_k, PWM_CLK_KL_CTRL, 9);
static struct clk_regmap pwm_k_div =
	AML_PWM_CLK_DIV(pwm_k, PWM_CLK_KL_CTRL, 0);
static struct clk_regmap pwm_k =
	AML_PWM_CLK_GATE(pwm_k, PWM_CLK_KL_CTRL, 8);

static struct clk_regmap pwm_l_sel =
	AML_PWM_CLK_MUX(pwm_l, PWM_CLK_KL_CTRL, 25);
static struct clk_regmap pwm_l_div =
	AML_PWM_CLK_DIV(pwm_l, PWM_CLK_KL_CTRL, 16);
static struct clk_regmap pwm_l =
	AML_PWM_CLK_GATE(pwm_l, PWM_CLK_KL_CTRL, 24);

static struct clk_regmap pwm_m_sel =
	AML_PWM_CLK_MUX(pwm_m, PWM_CLK_MN_CTRL, 9);
static struct clk_regmap pwm_m_div =
	AML_PWM_CLK_DIV(pwm_m, PWM_CLK_MN_CTRL, 0);
static struct clk_regmap pwm_m =
	AML_PWM_CLK_GATE(pwm_m, PWM_CLK_MN_CTRL, 8);

static struct clk_regmap pwm_n_sel =
	AML_PWM_CLK_MUX(pwm_n, PWM_CLK_MN_CTRL, 25);
static struct clk_regmap pwm_n_div =
	AML_PWM_CLK_DIV(pwm_n, PWM_CLK_MN_CTRL, 16);
static struct clk_regmap pwm_n =
	AML_PWM_CLK_GATE(pwm_n, PWM_CLK_MN_CTRL, 24);

static const struct clk_parent_data spicc_parent_data[] = {
	{ .fw_name = "oscin" },
	{ .fw_name = "sysclk" },
	{ .fw_name = "fdiv4" },
	{ .fw_name = "fdiv3" },
	{ .fw_name = "fdiv2" },
	{ .fw_name = "fdiv5" },
	{ .fw_name = "fdiv7" },
	{ .fw_name = "gp1" }
};

static struct clk_regmap spicc_a_sel = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = SPICC_CLK_CTRL,
		.mask = 0x7,
		.shift = 7,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "spicc_a_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = spicc_parent_data,
		.num_parents = ARRAY_SIZE(spicc_parent_data),
	},
};

static struct clk_regmap spicc_a_div = {
	.data = &(struct clk_regmap_div_data) {
		.offset = SPICC_CLK_CTRL,
		.shift = 0,
		.width = 6,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "spicc_a_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&spicc_a_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap spicc_a = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = SPICC_CLK_CTRL,
		.bit_idx = 6,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "spicc_a",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&spicc_a_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap spicc_b_sel = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = SPICC_CLK_CTRL,
		.mask = 0x7,
		.shift = 23,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "spicc_b_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = spicc_parent_data,
		.num_parents = ARRAY_SIZE(spicc_parent_data),
	},
};

static struct clk_regmap spicc_b_div = {
	.data = &(struct clk_regmap_div_data) {
		.offset = SPICC_CLK_CTRL,
		.shift = 16,
		.width = 6,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "spicc_b_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&spicc_b_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap spicc_b = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = SPICC_CLK_CTRL,
		.bit_idx = 22,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "spicc_b",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&spicc_b_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data spifc_parent_data[] = {
	{ .fw_name = "gp0" },
	{ .fw_name = "fdiv2" },
	{ .fw_name = "fdiv3" },
	{ .fw_name = "fdiv2p5" },
	{ .fw_name = "hifi" },
	{ .fw_name = "fdiv4" },
	{ .fw_name = "fdiv5" },
	{ .fw_name = "fdiv7" }
};

static struct clk_regmap spifc_sel = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = SPIFC_CLK_CTRL,
		.mask = 0x7,
		.shift = 9,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "spifc_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = spifc_parent_data,
		.num_parents = ARRAY_SIZE(spifc_parent_data),
	},
};

static struct clk_regmap spifc_div = {
	.data = &(struct clk_regmap_div_data) {
		.offset = SPIFC_CLK_CTRL,
		.shift = 0,
		.width = 7,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "spifc_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&spifc_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap spifc = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = SPIFC_CLK_CTRL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "spifc",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&spifc_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data emmc_parent_data[] = {
	{ .fw_name = "oscin" },
	{ .fw_name = "fdiv2" },
	{ .fw_name = "fdiv3" },
	{ .fw_name = "hifi" },
	{ .fw_name = "fdiv2p5" },
	{ .fw_name = "fdiv4" },
	{ .fw_name = "gp1" },
	{ .fw_name = "gp0" }
};

static struct clk_regmap sd_emmc_a_sel = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = SD_EMMC_CLK_CTRL,
		.mask = 0x7,
		.shift = 9,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "sd_emmc_a_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = emmc_parent_data,
		.num_parents = ARRAY_SIZE(emmc_parent_data),
	},
};

static struct clk_regmap sd_emmc_a_div = {
	.data = &(struct clk_regmap_div_data) {
		.offset = SD_EMMC_CLK_CTRL,
		.shift = 0,
		.width = 7,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "sd_emmc_a_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&sd_emmc_a_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap sd_emmc_a = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = SD_EMMC_CLK_CTRL,
		.bit_idx = 7,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "sd_emmc_a",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&sd_emmc_a_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap sd_emmc_b_sel = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = SD_EMMC_CLK_CTRL,
		.mask = 0x7,
		.shift = 25,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "sd_emmc_b_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = emmc_parent_data,
		.num_parents = ARRAY_SIZE(emmc_parent_data),
	},
};

static struct clk_regmap sd_emmc_b_div = {
	.data = &(struct clk_regmap_div_data) {
		.offset = SD_EMMC_CLK_CTRL,
		.shift = 16,
		.width = 7,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "sd_emmc_b_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&sd_emmc_b_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap sd_emmc_b = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = SD_EMMC_CLK_CTRL,
		.bit_idx = 23,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "sd_emmc_b",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&sd_emmc_b_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap sd_emmc_c_sel = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = NAND_CLK_CTRL,
		.mask = 0x7,
		.shift = 9,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "sd_emmc_c_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = emmc_parent_data,
		.num_parents = ARRAY_SIZE(emmc_parent_data),
	},
};

static struct clk_regmap sd_emmc_c_div = {
	.data = &(struct clk_regmap_div_data) {
		.offset = NAND_CLK_CTRL,
		.shift = 0,
		.width = 7,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "sd_emmc_c_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&sd_emmc_c_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap sd_emmc_c = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = NAND_CLK_CTRL,
		.bit_idx = 7,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "sd_emmc_c",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&sd_emmc_c_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap ts_div = {
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

static struct clk_regmap ts = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = TS_CLK_CTRL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "ts",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&ts_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data eth_parent = {
	.fw_name = "fdiv2",
};

static struct clk_fixed_factor eth_125m_div = {
	.mult = 1,
	.div = 8,
	.hw.init = &(struct clk_init_data) {
		.name = "eth_125m_div",
		.ops = &clk_fixed_factor_ops,
		.parent_data = &eth_parent,
		.num_parents = 1,
	},
};

static struct clk_regmap eth_125m = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = ETH_CLK_CTRL,
		.bit_idx = 7,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "eth_125m",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&eth_125m_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap eth_rmii_div = {
	.data = &(struct clk_regmap_div_data) {
		.offset = ETH_CLK_CTRL,
		.shift = 0,
		.width = 7,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "eth_rmii_div",
		.ops = &clk_regmap_divider_ops,
		.parent_data = &eth_parent,
		.num_parents = 1,
	},
};

static struct clk_regmap eth_rmii = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = ETH_CLK_CTRL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "eth_rmii",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&eth_rmii_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data mipi_dsi_meas_parent_data[] = {
	{ .fw_name = "oscin" },
	{ .fw_name = "fdiv4" },
	{ .fw_name = "fdiv3" },
	{ .fw_name = "fdiv5" },
	{ .fw_name = "gp1" },
	{ .fw_name = "gp0" },
	{ .fw_name = "fdiv2" },
	{ .fw_name = "fdiv7" }
};

static struct clk_regmap mipi_dsi_meas_sel = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = VDIN_MEAS_CLK_CTRL,
		.mask = 0x7,
		.shift = 21,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "mipi_dsi_meas_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = mipi_dsi_meas_parent_data,
		.num_parents = ARRAY_SIZE(mipi_dsi_meas_parent_data),
	},
};

static struct clk_regmap mipi_dsi_meas_div = {
	.data = &(struct clk_regmap_div_data) {
		.offset = VDIN_MEAS_CLK_CTRL,
		.shift = 12,
		.width = 7,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "mipi_dsi_meas_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&mipi_dsi_meas_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap mipi_dsi_meas = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = VDIN_MEAS_CLK_CTRL,
		.bit_idx = 20,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "mipi_dsi_meas",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&mipi_dsi_meas_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data dsi_phy_parent_data[] = {
	{ .fw_name = "gp1" },
	{ .fw_name = "gp0" },
	{ .fw_name = "hifi" },
	{ .fw_name = "fdiv3" },
	{ .fw_name = "fdiv2" },
	{ .fw_name = "fdiv2p5" },
	{ .fw_name = "fdiv4" },
	{ .fw_name = "fdiv7" }
};

static struct clk_regmap dsi_phy_sel = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = MIPIDSI_PHY_CLK_CTRL,
		.mask = 0x7,
		.shift = 12,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "dsi_phy_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = dsi_phy_parent_data,
		.num_parents = ARRAY_SIZE(dsi_phy_parent_data),
	},
};

static struct clk_regmap dsi_phy_div = {
	.data = &(struct clk_regmap_div_data) {
		.offset = MIPIDSI_PHY_CLK_CTRL,
		.shift = 0,
		.width = 7,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "dsi_phy_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&dsi_phy_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap dsi_phy = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = MIPIDSI_PHY_CLK_CTRL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "dsi_phy",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&dsi_phy_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data vout_mclk_parent_data[] = {
	{ .fw_name = "fdiv2p5" },
	{ .fw_name = "fdiv3" },
	{ .fw_name = "fdiv4" },
	{ .fw_name = "fdiv5" },
	{ .fw_name = "gp0" },
	{ .fw_name = "hifi" },
	{ .fw_name = "gp1" },
	{ .fw_name = "fdiv7" }
};

static struct clk_regmap vout_mclk_sel = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = VOUTENC_CLK_CTRL,
		.mask = 0x7,
		.shift = 9,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vout_mclk_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = vout_mclk_parent_data,
		.num_parents = ARRAY_SIZE(vout_mclk_parent_data),
	},
};

static struct clk_regmap vout_mclk_div = {
	.data = &(struct clk_regmap_div_data) {
		.offset = VOUTENC_CLK_CTRL,
		.shift = 0,
		.width = 7,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vout_mclk_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&vout_mclk_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap vout_mclk = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = VOUTENC_CLK_CTRL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vout_mclk",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&vout_mclk_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data vout_enc_parent_data[] = {
	{ .fw_name = "gp1" },
	{ .fw_name = "fdiv3" },
	{ .fw_name = "fdiv4" },
	{ .fw_name = "fdiv5" },
	{ .fw_name = "gp0" },
	{ .fw_name = "hifi" },
	{ .fw_name = "fdiv2p5" },
	{ .fw_name = "fdiv7" }
};

static struct clk_regmap vout_enc_sel = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = VOUTENC_CLK_CTRL,
		.mask = 0x7,
		.shift = 25,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vout_enc_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = vout_enc_parent_data,
		.num_parents = ARRAY_SIZE(vout_enc_parent_data),
	},
};

static struct clk_regmap vout_enc_div = {
	.data = &(struct clk_regmap_div_data) {
		.offset = VOUTENC_CLK_CTRL,
		.shift = 16,
		.width = 7,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vout_enc_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&vout_enc_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap vout_enc = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = VOUTENC_CLK_CTRL,
		.bit_idx = 24,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vout_enc",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&vout_enc_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data hcodec_pre_parent_data[] = {
	{ .fw_name = "fdiv2p5" },
	{ .fw_name = "fdiv3" },
	{ .fw_name = "fdiv4" },
	{ .fw_name = "fdiv5" },
	{ .fw_name = "fdiv7" },
	{ .fw_name = "hifi" },
	{ .fw_name = "gp0" },
	{ .fw_name = "oscin" }
};

static struct clk_regmap hcodec_0_sel = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = VDEC_CLK_CTRL,
		.mask = 0x7,
		.shift = 9,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "hcodec_0_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = hcodec_pre_parent_data,
		.num_parents = ARRAY_SIZE(hcodec_pre_parent_data),
	},
};

static struct clk_regmap hcodec_0_div = {
	.data = &(struct clk_regmap_div_data) {
		.offset = VDEC_CLK_CTRL,
		.shift = 0,
		.width = 7,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "hcodec_0_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&hcodec_0_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap hcodec_0 = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = VDEC_CLK_CTRL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "hcodec_0",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&hcodec_0_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap hcodec_1_sel = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = VDEC3_CLK_CTRL,
		.mask = 0x7,
		.shift = 9,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "hcodec_1_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = hcodec_pre_parent_data,
		.num_parents = ARRAY_SIZE(hcodec_pre_parent_data),
	},
};

static struct clk_regmap hcodec_1_div = {
	.data = &(struct clk_regmap_div_data) {
		.offset = VDEC3_CLK_CTRL,
		.shift = 0,
		.width = 7,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "hcodec_1_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&hcodec_1_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap hcodec_1 = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = VDEC3_CLK_CTRL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "hcodec_1",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&hcodec_1_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data hcodec_parent_data[] = {
	{ .hw = &hcodec_0.hw },
	{ .hw = &hcodec_1.hw }
};

static struct clk_regmap hcodec = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = VDEC3_CLK_CTRL,
		.mask = 0x1,
		.shift = 15,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "hcodec",
		.ops = &clk_regmap_mux_ops,
		.parent_data = hcodec_parent_data,
		.num_parents = ARRAY_SIZE(hcodec_parent_data),
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data vc9000e_parent_data[] = {
	{ .fw_name = "oscin" },
	{ .fw_name = "fdiv4" },
	{ .fw_name = "fdiv3" },
	{ .fw_name = "fdiv5" },
	{ .fw_name = "fdiv7" },
	{ .fw_name = "fdiv2p5" },
	{ .fw_name = "hifi" },
	{ .fw_name = "gp0" }
};

static struct clk_regmap vc9000e_aclk_sel = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = VC9000E_CLK_CTRL,
		.mask = 0x7,
		.shift = 9,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vc9000e_aclk_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = vc9000e_parent_data,
		.num_parents = ARRAY_SIZE(vc9000e_parent_data),
	},
};

static struct clk_regmap vc9000e_aclk_div = {
	.data = &(struct clk_regmap_div_data) {
		.offset = VC9000E_CLK_CTRL,
		.shift = 0,
		.width = 7,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vc9000e_aclk_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&vc9000e_aclk_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap vc9000e_aclk = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = VC9000E_CLK_CTRL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vc9000e_aclk",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&vc9000e_aclk_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap vc9000e_core_sel = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = VC9000E_CLK_CTRL,
		.mask = 0x7,
		.shift = 25,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vc9000e_core_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = vc9000e_parent_data,
		.num_parents = ARRAY_SIZE(vc9000e_parent_data),
	},
};

static struct clk_regmap vc9000e_core_div = {
	.data = &(struct clk_regmap_div_data) {
		.offset = VC9000E_CLK_CTRL,
		.shift = 16,
		.width = 7,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vc9000e_core_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&vc9000e_core_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap vc9000e_core = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = VC9000E_CLK_CTRL,
		.bit_idx = 24,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vc9000e_core",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&vc9000e_core_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data csi_phy_parent_data[] = {
	{ .fw_name = "fdiv2p5" },
	{ .fw_name = "fdiv3" },
	{ .fw_name = "fdiv4" },
	{ .fw_name = "fdiv5" },
	{ .fw_name = "gp0" },
	{ .fw_name = "hifi" },
	{ .fw_name = "gp1" },
	{ .fw_name = "oscin" }
};

static struct clk_regmap csi_phy0_sel = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = ISP0_CLK_CTRL,
		.mask = 0x7,
		.shift = 25,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "csi_phy0_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = csi_phy_parent_data,
		.num_parents = ARRAY_SIZE(csi_phy_parent_data),
	},
};

static struct clk_regmap csi_phy0_div = {
	.data = &(struct clk_regmap_div_data) {
		.offset = ISP0_CLK_CTRL,
		.shift = 16,
		.width = 7,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "csi_phy0_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&csi_phy0_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap csi_phy0 = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = ISP0_CLK_CTRL,
		.bit_idx = 24,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "csi_phy0",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&csi_phy0_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data dewarpa_parent_data[] = {
	{ .fw_name = "fdiv2p5" },
	{ .fw_name = "fdiv3" },
	{ .fw_name = "fdiv4" },
	{ .fw_name = "fdiv5" },
	{ .fw_name = "gp0" },
	{ .fw_name = "hifi" },
	{ .fw_name = "gp1" },
	{ .fw_name = "fdiv7" }
};

static struct clk_regmap dewarpa_sel = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = DEWARPA_CLK_CTRL,
		.mask = 0x7,
		.shift = 9,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "dewarpa_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = dewarpa_parent_data,
		.num_parents = ARRAY_SIZE(dewarpa_parent_data),
	},
};

static struct clk_regmap dewarpa_div = {
	.data = &(struct clk_regmap_div_data) {
		.offset = DEWARPA_CLK_CTRL,
		.shift = 0,
		.width = 7,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "dewarpa_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&dewarpa_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap dewarpa = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = DEWARPA_CLK_CTRL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "dewarpa",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&dewarpa_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data isp_parent_data[] = {
	{ .fw_name = "fdiv2p5" },
	{ .fw_name = "fdiv3" },
	{ .fw_name = "fdiv4" },
	{ .fw_name = "fdiv5" },
	{ .fw_name = "gp0" },
	{ .fw_name = "hifi" },
	{ .fw_name = "gp1" },
	{ .fw_name = "oscin" }
};

static struct clk_regmap isp0_sel = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = ISP0_CLK_CTRL,
		.mask = 0x7,
		.shift = 9,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "isp0_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = isp_parent_data,
		.num_parents = ARRAY_SIZE(isp_parent_data),
	},
};

static struct clk_regmap isp0_div = {
	.data = &(struct clk_regmap_div_data) {
		.offset = ISP0_CLK_CTRL,
		.shift = 0,
		.width = 7,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "isp0_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&isp0_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap isp0 = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = ISP0_CLK_CTRL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "isp0",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&isp0_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data nna_core_parent_data[] = {
	{ .fw_name = "oscin" },
	{ .fw_name = "fdiv2p5" },
	{ .fw_name = "fdiv4" },
	{ .fw_name = "fdiv3" },
	{ .fw_name = "fdiv5" },
	{ .fw_name = "fdiv2" },
	{ .fw_name = "gp1" },
	{ .fw_name = "hifi" }
};

static struct clk_regmap nna_core_sel = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = NNA_CLK_CTRL,
		.mask = 0x7,
		.shift = 9,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "nna_core_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = nna_core_parent_data,
		.num_parents = ARRAY_SIZE(nna_core_parent_data),
	},
};

static struct clk_regmap nna_core_div = {
	.data = &(struct clk_regmap_div_data) {
		.offset = NNA_CLK_CTRL,
		.shift = 0,
		.width = 7,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "nna_core_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&nna_core_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap nna_core = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = NNA_CLK_CTRL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "nna_core",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&nna_core_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data ge2d_parent_data[] = {
	{ .fw_name = "oscin" },
	{ .fw_name = "fdiv2p5" },
	{ .fw_name = "fdiv3" },
	{ .fw_name = "fdiv4" },
	{ .fw_name = "hifi" },
	{ .fw_name = "fdiv5" },
	{ .fw_name = "gp0" },
	{ .hw = &rtc_clk.hw }
};

static struct clk_regmap ge2d_sel = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = GE2D_CLK_CTRL,
		.mask = 0x7,
		.shift = 9,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "ge2d_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = ge2d_parent_data,
		.num_parents = ARRAY_SIZE(ge2d_parent_data),
	},
};

static struct clk_regmap ge2d_div = {
	.data = &(struct clk_regmap_div_data) {
		.offset = GE2D_CLK_CTRL,
		.shift = 0,
		.width = 7,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "ge2d_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&ge2d_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap ge2d = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = GE2D_CLK_CTRL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "ge2d",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&ge2d_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static const struct clk_parent_data vapb_parent_data[] = {
	{ .fw_name = "fdiv2p5" },
	{ .fw_name = "fdiv3" },
	{ .fw_name = "fdiv4" },
	{ .fw_name = "fdiv5" },
	{ .fw_name = "gp0" },
	{ .fw_name = "hifi" },
	{ .fw_name = "gp1" },
	{ .fw_name = "oscin" },
};

static struct clk_regmap vapb_sel = {
	.data = &(struct clk_regmap_mux_data) {
		.offset = VAPB_CLK_CTRL,
		.mask = 0x7,
		.shift = 9,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vapb_sel",
		.ops = &clk_regmap_mux_ops,
		.parent_data = vapb_parent_data,
		.num_parents = ARRAY_SIZE(vapb_parent_data),
	},
};

static struct clk_regmap vapb_div = {
	.data = &(struct clk_regmap_div_data) {
		.offset = VAPB_CLK_CTRL,
		.shift = 0,
		.width = 7,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vapb_div",
		.ops = &clk_regmap_divider_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&vapb_sel.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_regmap vapb = {
	.data = &(struct clk_regmap_gate_data) {
		.offset = VAPB_CLK_CTRL,
		.bit_idx = 8,
	},
	.hw.init = &(struct clk_init_data) {
		.name = "vapb",
		.ops = &clk_regmap_gate_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&vapb_div.hw
		},
		.num_parents = 1,
		.flags = CLK_SET_RATE_PARENT,
	},
};

static struct clk_hw *c3_periphs_hw_clks[] = {
	[CLKID_RTC_XTAL_CLKIN]		= &rtc_xtal_clkin.hw,
	[CLKID_RTC_32K_DIV]		= &rtc_32k_div.hw,
	[CLKID_RTC_32K_MUX]		= &rtc_32k_mux.hw,
	[CLKID_RTC_32K]			= &rtc_32k.hw,
	[CLKID_RTC_CLK]			= &rtc_clk.hw,
	[CLKID_SYS_RESET_CTRL]		= &sys_reset_ctrl.hw,
	[CLKID_SYS_PWR_CTRL]		= &sys_pwr_ctrl.hw,
	[CLKID_SYS_PAD_CTRL]		= &sys_pad_ctrl.hw,
	[CLKID_SYS_CTRL]		= &sys_ctrl.hw,
	[CLKID_SYS_TS_PLL]		= &sys_ts_pll.hw,
	[CLKID_SYS_DEV_ARB]		= &sys_dev_arb.hw,
	[CLKID_SYS_MMC_PCLK]		= &sys_mmc_pclk.hw,
	[CLKID_SYS_CPU_CTRL]		= &sys_cpu_ctrl.hw,
	[CLKID_SYS_JTAG_CTRL]		= &sys_jtag_ctrl.hw,
	[CLKID_SYS_IR_CTRL]		= &sys_ir_ctrl.hw,
	[CLKID_SYS_IRQ_CTRL]		= &sys_irq_ctrl.hw,
	[CLKID_SYS_MSR_CLK]		= &sys_msr_clk.hw,
	[CLKID_SYS_ROM]			= &sys_rom.hw,
	[CLKID_SYS_UART_F]		= &sys_uart_f.hw,
	[CLKID_SYS_CPU_ARB]		= &sys_cpu_apb.hw,
	[CLKID_SYS_RSA]			= &sys_rsa.hw,
	[CLKID_SYS_SAR_ADC]		= &sys_sar_adc.hw,
	[CLKID_SYS_STARTUP]		= &sys_startup.hw,
	[CLKID_SYS_SECURE]		= &sys_secure.hw,
	[CLKID_SYS_SPIFC]		= &sys_spifc.hw,
	[CLKID_SYS_NNA]			= &sys_nna.hw,
	[CLKID_SYS_ETH_MAC]		= &sys_eth_mac.hw,
	[CLKID_SYS_GIC]			= &sys_gic.hw,
	[CLKID_SYS_RAMA]		= &sys_rama.hw,
	[CLKID_SYS_BIG_NIC]		= &sys_big_nic.hw,
	[CLKID_SYS_RAMB]		= &sys_ramb.hw,
	[CLKID_SYS_AUDIO_PCLK]		= &sys_audio_pclk.hw,
	[CLKID_SYS_PWM_KL]		= &sys_pwm_kl.hw,
	[CLKID_SYS_PWM_IJ]		= &sys_pwm_ij.hw,
	[CLKID_SYS_USB]			= &sys_usb.hw,
	[CLKID_SYS_SD_EMMC_A]		= &sys_sd_emmc_a.hw,
	[CLKID_SYS_SD_EMMC_C]		= &sys_sd_emmc_c.hw,
	[CLKID_SYS_PWM_AB]		= &sys_pwm_ab.hw,
	[CLKID_SYS_PWM_CD]		= &sys_pwm_cd.hw,
	[CLKID_SYS_PWM_EF]		= &sys_pwm_ef.hw,
	[CLKID_SYS_PWM_GH]		= &sys_pwm_gh.hw,
	[CLKID_SYS_SPICC_1]		= &sys_spicc_1.hw,
	[CLKID_SYS_SPICC_0]		= &sys_spicc_0.hw,
	[CLKID_SYS_UART_A]		= &sys_uart_a.hw,
	[CLKID_SYS_UART_B]		= &sys_uart_b.hw,
	[CLKID_SYS_UART_C]		= &sys_uart_c.hw,
	[CLKID_SYS_UART_D]		= &sys_uart_d.hw,
	[CLKID_SYS_UART_E]		= &sys_uart_e.hw,
	[CLKID_SYS_I2C_M_A]		= &sys_i2c_m_a.hw,
	[CLKID_SYS_I2C_M_B]		= &sys_i2c_m_b.hw,
	[CLKID_SYS_I2C_M_C]		= &sys_i2c_m_c.hw,
	[CLKID_SYS_I2C_M_D]		= &sys_i2c_m_d.hw,
	[CLKID_SYS_I2S_S_A]		= &sys_i2c_s_a.hw,
	[CLKID_SYS_RTC]			= &sys_rtc.hw,
	[CLKID_SYS_GE2D]		= &sys_ge2d.hw,
	[CLKID_SYS_ISP]			= &sys_isp.hw,
	[CLKID_SYS_GPV_ISP_NIC]		= &sys_gpv_isp_nic.hw,
	[CLKID_SYS_GPV_CVE_NIC]		= &sys_gpv_cve_nic.hw,
	[CLKID_SYS_MIPI_DSI_HOST]	= &sys_mipi_dsi_host.hw,
	[CLKID_SYS_MIPI_DSI_PHY]	= &sys_mipi_dsi_phy.hw,
	[CLKID_SYS_ETH_PHY]		= &sys_eth_phy.hw,
	[CLKID_SYS_ACODEC]		= &sys_acodec.hw,
	[CLKID_SYS_DWAP]		= &sys_dwap.hw,
	[CLKID_SYS_DOS]			= &sys_dos.hw,
	[CLKID_SYS_CVE]			= &sys_cve.hw,
	[CLKID_SYS_VOUT]		= &sys_vout.hw,
	[CLKID_SYS_VC9000E]		= &sys_vc9000e.hw,
	[CLKID_SYS_PWM_MN]		= &sys_pwm_mn.hw,
	[CLKID_SYS_SD_EMMC_B]		= &sys_sd_emmc_b.hw,
	[CLKID_AXI_SYS_NIC]		= &axi_sys_nic.hw,
	[CLKID_AXI_ISP_NIC]		= &axi_isp_nic.hw,
	[CLKID_AXI_CVE_NIC]		= &axi_cve_nic.hw,
	[CLKID_AXI_RAMB]		= &axi_ramb.hw,
	[CLKID_AXI_RAMA]		= &axi_rama.hw,
	[CLKID_AXI_CPU_DMC]		= &axi_cpu_dmc.hw,
	[CLKID_AXI_NIC]			= &axi_nic.hw,
	[CLKID_AXI_DMA]			= &axi_dma.hw,
	[CLKID_AXI_MUX_NIC]		= &axi_mux_nic.hw,
	[CLKID_AXI_CVE]			= &axi_cve.hw,
	[CLKID_AXI_DEV1_DMC]		= &axi_dev1_dmc.hw,
	[CLKID_AXI_DEV0_DMC]		= &axi_dev0_dmc.hw,
	[CLKID_AXI_DSP_DMC]		= &axi_dsp_dmc.hw,
	[CLKID_12_24M_IN]		= &clk_12_24m_in.hw,
	[CLKID_12M_24M]			= &clk_12_24m.hw,
	[CLKID_FCLK_25M_DIV]		= &fclk_25m_div.hw,
	[CLKID_FCLK_25M]		= &fclk_25m.hw,
	[CLKID_GEN_SEL]			= &gen_sel.hw,
	[CLKID_GEN_DIV]			= &gen_div.hw,
	[CLKID_GEN]			= &gen.hw,
	[CLKID_SARADC_SEL]		= &saradc_sel.hw,
	[CLKID_SARADC_DIV]		= &saradc_div.hw,
	[CLKID_SARADC]			= &saradc.hw,
	[CLKID_PWM_A_SEL]		= &pwm_a_sel.hw,
	[CLKID_PWM_A_DIV]		= &pwm_a_div.hw,
	[CLKID_PWM_A]			= &pwm_a.hw,
	[CLKID_PWM_B_SEL]		= &pwm_b_sel.hw,
	[CLKID_PWM_B_DIV]		= &pwm_b_div.hw,
	[CLKID_PWM_B]			= &pwm_b.hw,
	[CLKID_PWM_C_SEL]		= &pwm_c_sel.hw,
	[CLKID_PWM_C_DIV]		= &pwm_c_div.hw,
	[CLKID_PWM_C]			= &pwm_c.hw,
	[CLKID_PWM_D_SEL]		= &pwm_d_sel.hw,
	[CLKID_PWM_D_DIV]		= &pwm_d_div.hw,
	[CLKID_PWM_D]			= &pwm_d.hw,
	[CLKID_PWM_E_SEL]		= &pwm_e_sel.hw,
	[CLKID_PWM_E_DIV]		= &pwm_e_div.hw,
	[CLKID_PWM_E]			= &pwm_e.hw,
	[CLKID_PWM_F_SEL]		= &pwm_f_sel.hw,
	[CLKID_PWM_F_DIV]		= &pwm_f_div.hw,
	[CLKID_PWM_F]			= &pwm_f.hw,
	[CLKID_PWM_G_SEL]		= &pwm_g_sel.hw,
	[CLKID_PWM_G_DIV]		= &pwm_g_div.hw,
	[CLKID_PWM_G]			= &pwm_g.hw,
	[CLKID_PWM_H_SEL]		= &pwm_h_sel.hw,
	[CLKID_PWM_H_DIV]		= &pwm_h_div.hw,
	[CLKID_PWM_H]			= &pwm_h.hw,
	[CLKID_PWM_I_SEL]		= &pwm_i_sel.hw,
	[CLKID_PWM_I_DIV]		= &pwm_i_div.hw,
	[CLKID_PWM_I]			= &pwm_i.hw,
	[CLKID_PWM_J_SEL]		= &pwm_j_sel.hw,
	[CLKID_PWM_J_DIV]		= &pwm_j_div.hw,
	[CLKID_PWM_J]			= &pwm_j.hw,
	[CLKID_PWM_K_SEL]		= &pwm_k_sel.hw,
	[CLKID_PWM_K_DIV]		= &pwm_k_div.hw,
	[CLKID_PWM_K]			= &pwm_k.hw,
	[CLKID_PWM_L_SEL]		= &pwm_l_sel.hw,
	[CLKID_PWM_L_DIV]		= &pwm_l_div.hw,
	[CLKID_PWM_L]			= &pwm_l.hw,
	[CLKID_PWM_M_SEL]		= &pwm_m_sel.hw,
	[CLKID_PWM_M_DIV]		= &pwm_m_div.hw,
	[CLKID_PWM_M]			= &pwm_m.hw,
	[CLKID_PWM_N_SEL]		= &pwm_n_sel.hw,
	[CLKID_PWM_N_DIV]		= &pwm_n_div.hw,
	[CLKID_PWM_N]			= &pwm_n.hw,
	[CLKID_SPICC_A_SEL]		= &spicc_a_sel.hw,
	[CLKID_SPICC_A_DIV]		= &spicc_a_div.hw,
	[CLKID_SPICC_A]			= &spicc_a.hw,
	[CLKID_SPICC_B_SEL]		= &spicc_b_sel.hw,
	[CLKID_SPICC_B_DIV]		= &spicc_b_div.hw,
	[CLKID_SPICC_B]			= &spicc_b.hw,
	[CLKID_SPIFC_SEL]		= &spifc_sel.hw,
	[CLKID_SPIFC_DIV]		= &spifc_div.hw,
	[CLKID_SPIFC]			= &spifc.hw,
	[CLKID_SD_EMMC_A_SEL]		= &sd_emmc_a_sel.hw,
	[CLKID_SD_EMMC_A_DIV]		= &sd_emmc_a_div.hw,
	[CLKID_SD_EMMC_A]		= &sd_emmc_a.hw,
	[CLKID_SD_EMMC_B_SEL]		= &sd_emmc_b_sel.hw,
	[CLKID_SD_EMMC_B_DIV]		= &sd_emmc_b_div.hw,
	[CLKID_SD_EMMC_B]		= &sd_emmc_b.hw,
	[CLKID_SD_EMMC_C_SEL]		= &sd_emmc_c_sel.hw,
	[CLKID_SD_EMMC_C_DIV]		= &sd_emmc_c_div.hw,
	[CLKID_SD_EMMC_C]		= &sd_emmc_c.hw,
	[CLKID_TS_DIV]			= &ts_div.hw,
	[CLKID_TS]			= &ts.hw,
	[CLKID_ETH_125M_DIV]		= &eth_125m_div.hw,
	[CLKID_ETH_125M]		= &eth_125m.hw,
	[CLKID_ETH_RMII_DIV]		= &eth_rmii_div.hw,
	[CLKID_ETH_RMII]		= &eth_rmii.hw,
	[CLKID_MIPI_DSI_MEAS_SEL]	= &mipi_dsi_meas_sel.hw,
	[CLKID_MIPI_DSI_MEAS_DIV]	= &mipi_dsi_meas_div.hw,
	[CLKID_MIPI_DSI_MEAS]		= &mipi_dsi_meas.hw,
	[CLKID_DSI_PHY_SEL]		= &dsi_phy_sel.hw,
	[CLKID_DSI_PHY_DIV]		= &dsi_phy_div.hw,
	[CLKID_DSI_PHY]			= &dsi_phy.hw,
	[CLKID_VOUT_MCLK_SEL]		= &vout_mclk_sel.hw,
	[CLKID_VOUT_MCLK_DIV]		= &vout_mclk_div.hw,
	[CLKID_VOUT_MCLK]		= &vout_mclk.hw,
	[CLKID_VOUT_ENC_SEL]		= &vout_enc_sel.hw,
	[CLKID_VOUT_ENC_DIV]		= &vout_enc_div.hw,
	[CLKID_VOUT_ENC]		= &vout_enc.hw,
	[CLKID_HCODEC_0_SEL]		= &hcodec_0_sel.hw,
	[CLKID_HCODEC_0_DIV]		= &hcodec_0_div.hw,
	[CLKID_HCODEC_0]		= &hcodec_0.hw,
	[CLKID_HCODEC_1_SEL]		= &hcodec_1_sel.hw,
	[CLKID_HCODEC_1_DIV]		= &hcodec_1_div.hw,
	[CLKID_HCODEC_1]		= &hcodec_1.hw,
	[CLKID_HCODEC]			= &hcodec.hw,
	[CLKID_VC9000E_ACLK_SEL]	= &vc9000e_aclk_sel.hw,
	[CLKID_VC9000E_ACLK_DIV]	= &vc9000e_aclk_div.hw,
	[CLKID_VC9000E_ACLK]		= &vc9000e_aclk.hw,
	[CLKID_VC9000E_CORE_SEL]	= &vc9000e_core_sel.hw,
	[CLKID_VC9000E_CORE_DIV]	= &vc9000e_core_div.hw,
	[CLKID_VC9000E_CORE]		= &vc9000e_core.hw,
	[CLKID_CSI_PHY0_SEL]		= &csi_phy0_sel.hw,
	[CLKID_CSI_PHY0_DIV]		= &csi_phy0_div.hw,
	[CLKID_CSI_PHY0]		= &csi_phy0.hw,
	[CLKID_DEWARPA_SEL]		= &dewarpa_sel.hw,
	[CLKID_DEWARPA_DIV]		= &dewarpa_div.hw,
	[CLKID_DEWARPA]			= &dewarpa.hw,
	[CLKID_ISP0_SEL]		= &isp0_sel.hw,
	[CLKID_ISP0_DIV]		= &isp0_div.hw,
	[CLKID_ISP0]			= &isp0.hw,
	[CLKID_NNA_CORE_SEL]		= &nna_core_sel.hw,
	[CLKID_NNA_CORE_DIV]		= &nna_core_div.hw,
	[CLKID_NNA_CORE]		= &nna_core.hw,
	[CLKID_GE2D_SEL]		= &ge2d_sel.hw,
	[CLKID_GE2D_DIV]		= &ge2d_div.hw,
	[CLKID_GE2D]			= &ge2d.hw,
	[CLKID_VAPB_SEL]		= &vapb_sel.hw,
	[CLKID_VAPB_DIV]		= &vapb_div.hw,
	[CLKID_VAPB]			= &vapb.hw,
};

/* Convenience table to populate regmap in .probe */
static struct clk_regmap *const c3_periphs_clk_regmaps[] = {
	&rtc_xtal_clkin,
	&rtc_32k_div,
	&rtc_32k_mux,
	&rtc_32k,
	&rtc_clk,
	&sys_reset_ctrl,
	&sys_pwr_ctrl,
	&sys_pad_ctrl,
	&sys_ctrl,
	&sys_ts_pll,
	&sys_dev_arb,
	&sys_mmc_pclk,
	&sys_cpu_ctrl,
	&sys_jtag_ctrl,
	&sys_ir_ctrl,
	&sys_irq_ctrl,
	&sys_msr_clk,
	&sys_rom,
	&sys_uart_f,
	&sys_cpu_apb,
	&sys_rsa,
	&sys_sar_adc,
	&sys_startup,
	&sys_secure,
	&sys_spifc,
	&sys_nna,
	&sys_eth_mac,
	&sys_gic,
	&sys_rama,
	&sys_big_nic,
	&sys_ramb,
	&sys_audio_pclk,
	&sys_pwm_kl,
	&sys_pwm_ij,
	&sys_usb,
	&sys_sd_emmc_a,
	&sys_sd_emmc_c,
	&sys_pwm_ab,
	&sys_pwm_cd,
	&sys_pwm_ef,
	&sys_pwm_gh,
	&sys_spicc_1,
	&sys_spicc_0,
	&sys_uart_a,
	&sys_uart_b,
	&sys_uart_c,
	&sys_uart_d,
	&sys_uart_e,
	&sys_i2c_m_a,
	&sys_i2c_m_b,
	&sys_i2c_m_c,
	&sys_i2c_m_d,
	&sys_i2c_s_a,
	&sys_rtc,
	&sys_ge2d,
	&sys_isp,
	&sys_gpv_isp_nic,
	&sys_gpv_cve_nic,
	&sys_mipi_dsi_host,
	&sys_mipi_dsi_phy,
	&sys_eth_phy,
	&sys_acodec,
	&sys_dwap,
	&sys_dos,
	&sys_cve,
	&sys_vout,
	&sys_vc9000e,
	&sys_pwm_mn,
	&sys_sd_emmc_b,
	&axi_sys_nic,
	&axi_isp_nic,
	&axi_cve_nic,
	&axi_ramb,
	&axi_rama,
	&axi_cpu_dmc,
	&axi_nic,
	&axi_dma,
	&axi_mux_nic,
	&axi_cve,
	&axi_dev1_dmc,
	&axi_dev0_dmc,
	&axi_dsp_dmc,
	&clk_12_24m_in,
	&clk_12_24m,
	&fclk_25m_div,
	&fclk_25m,
	&gen_sel,
	&gen_div,
	&gen,
	&saradc_sel,
	&saradc_div,
	&saradc,
	&pwm_a_sel,
	&pwm_a_div,
	&pwm_a,
	&pwm_b_sel,
	&pwm_b_div,
	&pwm_b,
	&pwm_c_sel,
	&pwm_c_div,
	&pwm_c,
	&pwm_d_sel,
	&pwm_d_div,
	&pwm_d,
	&pwm_e_sel,
	&pwm_e_div,
	&pwm_e,
	&pwm_f_sel,
	&pwm_f_div,
	&pwm_f,
	&pwm_g_sel,
	&pwm_g_div,
	&pwm_g,
	&pwm_h_sel,
	&pwm_h_div,
	&pwm_h,
	&pwm_i_sel,
	&pwm_i_div,
	&pwm_i,
	&pwm_j_sel,
	&pwm_j_div,
	&pwm_j,
	&pwm_k_sel,
	&pwm_k_div,
	&pwm_k,
	&pwm_l_sel,
	&pwm_l_div,
	&pwm_l,
	&pwm_m_sel,
	&pwm_m_div,
	&pwm_m,
	&pwm_n_sel,
	&pwm_n_div,
	&pwm_n,
	&spicc_a_sel,
	&spicc_a_div,
	&spicc_a,
	&spicc_b_sel,
	&spicc_b_div,
	&spicc_b,
	&spifc_sel,
	&spifc_div,
	&spifc,
	&sd_emmc_a_sel,
	&sd_emmc_a_div,
	&sd_emmc_a,
	&sd_emmc_b_sel,
	&sd_emmc_b_div,
	&sd_emmc_b,
	&sd_emmc_c_sel,
	&sd_emmc_c_div,
	&sd_emmc_c,
	&ts_div,
	&ts,
	&eth_125m,
	&eth_rmii_div,
	&eth_rmii,
	&mipi_dsi_meas_sel,
	&mipi_dsi_meas_div,
	&mipi_dsi_meas,
	&dsi_phy_sel,
	&dsi_phy_div,
	&dsi_phy,
	&vout_mclk_sel,
	&vout_mclk_div,
	&vout_mclk,
	&vout_enc_sel,
	&vout_enc_div,
	&vout_enc,
	&hcodec_0_sel,
	&hcodec_0_div,
	&hcodec_0,
	&hcodec_1_sel,
	&hcodec_1_div,
	&hcodec_1,
	&hcodec,
	&vc9000e_aclk_sel,
	&vc9000e_aclk_div,
	&vc9000e_aclk,
	&vc9000e_core_sel,
	&vc9000e_core_div,
	&vc9000e_core,
	&csi_phy0_sel,
	&csi_phy0_div,
	&csi_phy0,
	&dewarpa_sel,
	&dewarpa_div,
	&dewarpa,
	&isp0_sel,
	&isp0_div,
	&isp0,
	&nna_core_sel,
	&nna_core_div,
	&nna_core,
	&ge2d_sel,
	&ge2d_div,
	&ge2d,
	&vapb_sel,
	&vapb_div,
	&vapb,
};

static const struct regmap_config clkc_regmap_config = {
	.reg_bits       = 32,
	.val_bits       = 32,
	.reg_stride     = 4,
	.max_register   = NNA_CLK_CTRL,
};

static struct meson_clk_hw_data c3_periphs_clks = {
	.hws = c3_periphs_hw_clks,
	.num = ARRAY_SIZE(c3_periphs_hw_clks),
};

static int c3_peripherals_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct regmap *regmap;
	void __iomem *base;
	int clkid, ret, i;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	regmap = devm_regmap_init_mmio(dev, base, &clkc_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	/* Populate regmap for the regmap backed clocks */
	for (i = 0; i < ARRAY_SIZE(c3_periphs_clk_regmaps); i++)
		c3_periphs_clk_regmaps[i]->map = regmap;

	for (clkid = 0; clkid < c3_periphs_clks.num; clkid++) {
		/* array might be sparse */
		if (!c3_periphs_clks.hws[clkid])
			continue;

		ret = devm_clk_hw_register(dev, c3_periphs_clks.hws[clkid]);
		if (ret) {
			dev_err(dev, "Clock registration failed\n");
			return ret;
		}
	}

	return devm_of_clk_add_hw_provider(dev, meson_clk_hw_get,
					   &c3_periphs_clks);
}

static const struct of_device_id c3_peripherals_clkc_match_table[] = {
	{
		.compatible = "amlogic,c3-peripherals-clkc",
	},
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, c3_peripherals_clkc_match_table);

static struct platform_driver c3_peripherals_driver = {
	.probe		= c3_peripherals_probe,
	.driver		= {
		.name	= "c3-peripherals-clkc",
		.of_match_table = c3_peripherals_clkc_match_table,
	},
};
module_platform_driver(c3_peripherals_driver);

MODULE_DESCRIPTION("Amlogic C3 Peripherals Clock Controller driver");
MODULE_AUTHOR("Chuan Liu <chuan.liu@amlogic.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("CLK_MESON");
