// SPDX-License-Identifier: GPL-2.0-only

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clk/ti.h>
#include <linux/of_platform.h>
#include <dt-bindings/clock/dm814.h>

#include "clock.h"

static const struct omap_clkctrl_reg_data dm814_default_clkctrl_regs[] __initconst = {
	{ DM814_USB_OTG_HS_CLKCTRL, NULL, CLKF_SW_SUP, "pll260dcoclkldo" },
	{ 0 },
};

static const struct omap_clkctrl_reg_data dm814_alwon_clkctrl_regs[] __initconst = {
	{ DM814_UART1_CLKCTRL, NULL, CLKF_SW_SUP, "sysclk10_ck" },
	{ DM814_UART2_CLKCTRL, NULL, CLKF_SW_SUP, "sysclk10_ck" },
	{ DM814_UART3_CLKCTRL, NULL, CLKF_SW_SUP, "sysclk10_ck" },
	{ DM814_GPIO1_CLKCTRL, NULL, CLKF_SW_SUP, "sysclk6_ck" },
	{ DM814_GPIO2_CLKCTRL, NULL, CLKF_SW_SUP, "sysclk6_ck" },
	{ DM814_I2C1_CLKCTRL, NULL, CLKF_SW_SUP, "sysclk10_ck" },
	{ DM814_I2C2_CLKCTRL, NULL, CLKF_SW_SUP, "sysclk10_ck" },
	{ DM814_WD_TIMER_CLKCTRL, NULL, CLKF_SW_SUP | CLKF_NO_IDLEST, "sysclk18_ck" },
	{ DM814_MCSPI1_CLKCTRL, NULL, CLKF_SW_SUP, "sysclk10_ck" },
	{ DM814_GPMC_CLKCTRL, NULL, CLKF_SW_SUP, "sysclk6_ck" },
	{ DM814_CPGMAC0_CLKCTRL, NULL, CLKF_SW_SUP, "cpsw_125mhz_gclk" },
	{ DM814_MPU_CLKCTRL, NULL, CLKF_SW_SUP, "mpu_ck" },
	{ DM814_RTC_CLKCTRL, NULL, CLKF_SW_SUP | CLKF_NO_IDLEST, "sysclk18_ck" },
	{ DM814_TPCC_CLKCTRL, NULL, CLKF_SW_SUP, "sysclk4_ck" },
	{ DM814_TPTC0_CLKCTRL, NULL, CLKF_SW_SUP, "sysclk4_ck" },
	{ DM814_TPTC1_CLKCTRL, NULL, CLKF_SW_SUP, "sysclk4_ck" },
	{ DM814_TPTC2_CLKCTRL, NULL, CLKF_SW_SUP, "sysclk4_ck" },
	{ DM814_TPTC3_CLKCTRL, NULL, CLKF_SW_SUP, "sysclk4_ck" },
	{ DM814_MMC1_CLKCTRL, NULL, CLKF_SW_SUP, "sysclk8_ck" },
	{ DM814_MMC2_CLKCTRL, NULL, CLKF_SW_SUP, "sysclk8_ck" },
	{ DM814_MMC3_CLKCTRL, NULL, CLKF_SW_SUP, "sysclk8_ck" },
	{ 0 },
};

const struct omap_clkctrl_data dm814_clkctrl_data[] __initconst = {
	{ 0x48180500, dm814_default_clkctrl_regs },
	{ 0x48181400, dm814_alwon_clkctrl_regs },
	{ 0 },
};

static struct ti_dt_clk dm814_clks[] = {
	DT_CLK(NULL, "timer_sys_ck", "devosc_ck"),
	{ .node_name = NULL },
};

static bool timer_clocks_initialized;

static int __init dm814x_adpll_early_init(void)
{
	struct device_node *np;

	if (!timer_clocks_initialized)
		return -ENODEV;

	np = of_find_node_by_name(NULL, "pllss");
	if (!np) {
		pr_err("Could not find node for plls\n");
		return -ENODEV;
	}

	of_platform_populate(np, NULL, NULL, NULL);

	return 0;
}
core_initcall(dm814x_adpll_early_init);

static const char * const init_clocks[] = {
	"pll040clkout",		/* MPU 481c5040.adpll.clkout */
	"pll290clkout",		/* DDR 481c5290.adpll.clkout */
};

static int __init dm814x_adpll_enable_init_clocks(void)
{
	int i, err;

	if (!timer_clocks_initialized)
		return -ENODEV;

	for (i = 0; i < ARRAY_SIZE(init_clocks); i++) {
		struct clk *clock;

		clock = clk_get(NULL, init_clocks[i]);
		if (WARN(IS_ERR(clock), "could not find init clock %s\n",
			 init_clocks[i]))
			continue;
		err = clk_prepare_enable(clock);
		if (WARN(err, "could not enable init clock %s\n",
			 init_clocks[i]))
			continue;
	}

	return 0;
}
postcore_initcall(dm814x_adpll_enable_init_clocks);

int __init dm814x_dt_clk_init(void)
{
	ti_dt_clocks_register(dm814_clks);
	omap2_clk_disable_autoidle_all();
	ti_clk_add_aliases();
	omap2_clk_enable_init_clocks(NULL, 0);
	timer_clocks_initialized = true;

	return 0;
}
