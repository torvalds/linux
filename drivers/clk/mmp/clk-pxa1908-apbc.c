// SPDX-License-Identifier: GPL-2.0-only
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>

#include <dt-bindings/clock/marvell,pxa1908.h>

#include "clk.h"

#define APBC_UART0		0x0
#define APBC_UART1		0x4
#define APBC_GPIO		0x8
#define APBC_PWM0		0xc
#define APBC_PWM1		0x10
#define APBC_PWM2		0x14
#define APBC_PWM3		0x18
#define APBC_SSP0		0x1c
#define APBC_SSP1		0x20
#define APBC_IPC_RST		0x24
#define APBC_RTC		0x28
#define APBC_TWSI0		0x2c
#define APBC_KPC		0x30
#define APBC_SWJTAG		0x40
#define APBC_SSP2		0x4c
#define APBC_TWSI1		0x60
#define APBC_THERMAL		0x6c
#define APBC_TWSI3		0x70

#define APBC_NR_CLKS		19

struct pxa1908_clk_unit {
	struct mmp_clk_unit unit;
	void __iomem *base;
};

static DEFINE_SPINLOCK(pwm0_lock);
static DEFINE_SPINLOCK(pwm2_lock);

static DEFINE_SPINLOCK(uart0_lock);
static DEFINE_SPINLOCK(uart1_lock);

static const char * const uart_parent_names[] = {"pll1_117", "uart_pll"};
static const char * const ssp_parent_names[] = {"pll1_d16", "pll1_d48", "pll1_d24", "pll1_d12"};

static struct mmp_param_gate_clk apbc_gate_clks[] = {
	{PXA1908_CLK_TWSI0, "twsi0_clk", "pll1_32", CLK_SET_RATE_PARENT, APBC_TWSI0, 0x7, 3, 0, 0, NULL},
	{PXA1908_CLK_TWSI1, "twsi1_clk", "pll1_32", CLK_SET_RATE_PARENT, APBC_TWSI1, 0x7, 3, 0, 0, NULL},
	{PXA1908_CLK_TWSI3, "twsi3_clk", "pll1_32", CLK_SET_RATE_PARENT, APBC_TWSI3, 0x7, 3, 0, 0, NULL},
	{PXA1908_CLK_GPIO, "gpio_clk", "vctcxo", CLK_SET_RATE_PARENT, APBC_GPIO, 0x7, 3, 0, 0, NULL},
	{PXA1908_CLK_KPC, "kpc_clk", "clk32", CLK_SET_RATE_PARENT, APBC_KPC, 0x7, 3, 0, MMP_CLK_GATE_NEED_DELAY, NULL},
	{PXA1908_CLK_RTC, "rtc_clk", "clk32", CLK_SET_RATE_PARENT, APBC_RTC, 0x87, 0x83, 0, MMP_CLK_GATE_NEED_DELAY, NULL},
	{PXA1908_CLK_PWM0, "pwm0_clk", "pwm01_apb_share", CLK_SET_RATE_PARENT, APBC_PWM0, 0x2, 2, 0, 0, &pwm0_lock},
	{PXA1908_CLK_PWM1, "pwm1_clk", "pwm01_apb_share", CLK_SET_RATE_PARENT, APBC_PWM1, 0x6, 2, 0, 0, NULL},
	{PXA1908_CLK_PWM2, "pwm2_clk", "pwm23_apb_share", CLK_SET_RATE_PARENT, APBC_PWM2, 0x2, 2, 0, 0, NULL},
	{PXA1908_CLK_PWM3, "pwm3_clk", "pwm23_apb_share", CLK_SET_RATE_PARENT, APBC_PWM3, 0x6, 2, 0, 0, NULL},
	{PXA1908_CLK_UART0, "uart0_clk", "uart0_mux", CLK_SET_RATE_PARENT, APBC_UART0, 0x7, 3, 0, 0, &uart0_lock},
	{PXA1908_CLK_UART1, "uart1_clk", "uart1_mux", CLK_SET_RATE_PARENT, APBC_UART1, 0x7, 3, 0, 0, &uart1_lock},
	{PXA1908_CLK_THERMAL, "thermal_clk", NULL, 0, APBC_THERMAL, 0x7, 3, 0, 0, NULL},
	{PXA1908_CLK_IPC_RST, "ipc_clk", NULL, 0, APBC_IPC_RST, 0x7, 3, 0, 0, NULL},
	{PXA1908_CLK_SSP0, "ssp0_clk", "ssp0_mux", 0, APBC_SSP0, 0x7, 3, 0, 0, NULL},
	{PXA1908_CLK_SSP2, "ssp2_clk", "ssp2_mux", 0, APBC_SSP2, 0x7, 3, 0, 0, NULL},
};

static struct mmp_param_mux_clk apbc_mux_clks[] = {
	{0, "uart0_mux", uart_parent_names, ARRAY_SIZE(uart_parent_names), CLK_SET_RATE_PARENT, APBC_UART0, 4, 3, 0, &uart0_lock},
	{0, "uart1_mux", uart_parent_names, ARRAY_SIZE(uart_parent_names), CLK_SET_RATE_PARENT, APBC_UART1, 4, 3, 0, &uart1_lock},
	{0, "ssp0_mux", ssp_parent_names, ARRAY_SIZE(ssp_parent_names), 0, APBC_SSP0, 4, 3, 0, NULL},
	{0, "ssp2_mux", ssp_parent_names, ARRAY_SIZE(ssp_parent_names), 0, APBC_SSP2, 4, 3, 0, NULL},
};

static void pxa1908_apb_periph_clk_init(struct pxa1908_clk_unit *pxa_unit)
{
	struct mmp_clk_unit *unit = &pxa_unit->unit;
	struct clk *clk;

	mmp_clk_register_gate(NULL, "pwm01_apb_share", "pll1_d48",
			CLK_SET_RATE_PARENT,
			pxa_unit->base + APBC_PWM0,
			0x5, 1, 0, 0, &pwm0_lock);
	mmp_clk_register_gate(NULL, "pwm23_apb_share", "pll1_d48",
			CLK_SET_RATE_PARENT,
			pxa_unit->base + APBC_PWM2,
			0x5, 1, 0, 0, &pwm2_lock);
	clk = mmp_clk_register_apbc("swjtag", NULL,
			pxa_unit->base + APBC_SWJTAG, 10, 0, NULL);
	mmp_clk_add(unit, PXA1908_CLK_SWJTAG, clk);
	mmp_register_mux_clks(unit, apbc_mux_clks, pxa_unit->base,
			ARRAY_SIZE(apbc_mux_clks));
	mmp_register_gate_clks(unit, apbc_gate_clks, pxa_unit->base,
			ARRAY_SIZE(apbc_gate_clks));
}

static int pxa1908_apbc_probe(struct platform_device *pdev)
{
	struct pxa1908_clk_unit *pxa_unit;

	pxa_unit = devm_kzalloc(&pdev->dev, sizeof(*pxa_unit), GFP_KERNEL);
	if (IS_ERR(pxa_unit))
		return PTR_ERR(pxa_unit);

	pxa_unit->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pxa_unit->base))
		return PTR_ERR(pxa_unit->base);

	mmp_clk_init(pdev->dev.of_node, &pxa_unit->unit, APBC_NR_CLKS);

	pxa1908_apb_periph_clk_init(pxa_unit);

	return 0;
}

static const struct of_device_id pxa1908_apbc_match_table[] = {
	{ .compatible = "marvell,pxa1908-apbc" },
	{ }
};
MODULE_DEVICE_TABLE(of, pxa1908_apbc_match_table);

static struct platform_driver pxa1908_apbc_driver = {
	.probe = pxa1908_apbc_probe,
	.driver = {
		.name = "pxa1908-apbc",
		.of_match_table = pxa1908_apbc_match_table
	}
};
module_platform_driver(pxa1908_apbc_driver);

MODULE_AUTHOR("Duje Mihanović <duje.mihanovic@skole.hr>");
MODULE_DESCRIPTION("Marvell PXA1908 APBC Clock Driver");
MODULE_LICENSE("GPL");
