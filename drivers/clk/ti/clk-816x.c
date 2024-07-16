// SPDX-License-Identifier: GPL-2.0-only

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/clk-provider.h>
#include <linux/clk/ti.h>
#include <dt-bindings/clock/dm816.h>

#include "clock.h"

static const struct omap_clkctrl_reg_data dm816_default_clkctrl_regs[] __initconst = {
	{ DM816_USB_OTG_HS_CLKCTRL, NULL, CLKF_SW_SUP, "sysclk6_ck" },
	{ 0 },
};

static const struct omap_clkctrl_reg_data dm816_alwon_clkctrl_regs[] __initconst = {
	{ DM816_UART1_CLKCTRL, NULL, CLKF_SW_SUP, "sysclk10_ck" },
	{ DM816_UART2_CLKCTRL, NULL, CLKF_SW_SUP, "sysclk10_ck" },
	{ DM816_UART3_CLKCTRL, NULL, CLKF_SW_SUP, "sysclk10_ck" },
	{ DM816_GPIO1_CLKCTRL, NULL, CLKF_SW_SUP, "sysclk6_ck" },
	{ DM816_GPIO2_CLKCTRL, NULL, CLKF_SW_SUP, "sysclk6_ck" },
	{ DM816_I2C1_CLKCTRL, NULL, CLKF_SW_SUP, "sysclk10_ck" },
	{ DM816_I2C2_CLKCTRL, NULL, CLKF_SW_SUP, "sysclk10_ck" },
	{ DM816_TIMER1_CLKCTRL, NULL, CLKF_SW_SUP, "timer1_fck" },
	{ DM816_TIMER2_CLKCTRL, NULL, CLKF_SW_SUP, "timer2_fck" },
	{ DM816_TIMER3_CLKCTRL, NULL, CLKF_SW_SUP, "timer3_fck" },
	{ DM816_TIMER4_CLKCTRL, NULL, CLKF_SW_SUP, "timer4_fck" },
	{ DM816_TIMER5_CLKCTRL, NULL, CLKF_SW_SUP, "timer5_fck" },
	{ DM816_TIMER6_CLKCTRL, NULL, CLKF_SW_SUP, "timer6_fck" },
	{ DM816_TIMER7_CLKCTRL, NULL, CLKF_SW_SUP, "timer7_fck" },
	{ DM816_WD_TIMER_CLKCTRL, NULL, CLKF_SW_SUP | CLKF_NO_IDLEST, "sysclk18_ck" },
	{ DM816_MCSPI1_CLKCTRL, NULL, CLKF_SW_SUP, "sysclk10_ck" },
	{ DM816_MAILBOX_CLKCTRL, NULL, CLKF_SW_SUP, "sysclk6_ck" },
	{ DM816_SPINBOX_CLKCTRL, NULL, CLKF_SW_SUP, "sysclk6_ck" },
	{ DM816_MMC1_CLKCTRL, NULL, CLKF_SW_SUP, "sysclk10_ck" },
	{ DM816_GPMC_CLKCTRL, NULL, CLKF_SW_SUP, "sysclk6_ck" },
	{ DM816_DAVINCI_MDIO_CLKCTRL, NULL, CLKF_SW_SUP | CLKF_NO_IDLEST, "sysclk24_ck" },
	{ DM816_EMAC1_CLKCTRL, NULL, CLKF_SW_SUP | CLKF_NO_IDLEST, "sysclk24_ck" },
	{ DM816_MPU_CLKCTRL, NULL, CLKF_SW_SUP, "sysclk2_ck" },
	{ DM816_RTC_CLKCTRL, NULL, CLKF_SW_SUP | CLKF_NO_IDLEST, "sysclk18_ck" },
	{ DM816_TPCC_CLKCTRL, NULL, CLKF_SW_SUP, "sysclk4_ck" },
	{ DM816_TPTC0_CLKCTRL, NULL, CLKF_SW_SUP, "sysclk4_ck" },
	{ DM816_TPTC1_CLKCTRL, NULL, CLKF_SW_SUP, "sysclk4_ck" },
	{ DM816_TPTC2_CLKCTRL, NULL, CLKF_SW_SUP, "sysclk4_ck" },
	{ DM816_TPTC3_CLKCTRL, NULL, CLKF_SW_SUP, "sysclk4_ck" },
	{ 0 },
};

const struct omap_clkctrl_data dm816_clkctrl_data[] __initconst = {
	{ 0x48180500, dm816_default_clkctrl_regs },
	{ 0x48181400, dm816_alwon_clkctrl_regs },
	{ 0 },
};

static struct ti_dt_clk dm816x_clks[] = {
	DT_CLK(NULL, "sys_clkin", "sys_clkin_ck"),
	DT_CLK(NULL, "timer_sys_ck", "sys_clkin_ck"),
	DT_CLK(NULL, "timer_32k_ck", "sysclk18_ck"),
	DT_CLK(NULL, "timer_ext_ck", "tclkin_ck"),
	{ .node_name = NULL },
};

static const char *enable_init_clks[] = {
	"ddr_pll_clk1",
	"ddr_pll_clk2",
	"ddr_pll_clk3",
	"sysclk6_ck",
};

int __init dm816x_dt_clk_init(void)
{
	ti_dt_clocks_register(dm816x_clks);
	omap2_clk_disable_autoidle_all();
	ti_clk_add_aliases();
	omap2_clk_enable_init_clocks(enable_init_clks,
				     ARRAY_SIZE(enable_init_clks));

	return 0;
}
