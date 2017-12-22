// SPDX-License-Identifier: GPL-2.0+

#define pr_fmt(fmt) "clk-aspeed: " fmt

#include <linux/clk-provider.h>
#include <linux/mfd/syscon.h>
#include <linux/of_address.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include <dt-bindings/clock/aspeed-clock.h>

#define ASPEED_NUM_CLKS		35

#define ASPEED_STRAP		0x70

/* Keeps track of all clocks */
static struct clk_hw_onecell_data *aspeed_clk_data;

static void __iomem *scu_base;

/**
 * struct aspeed_gate_data - Aspeed gated clocks
 * @clock_idx: bit used to gate this clock in the clock register
 * @reset_idx: bit used to reset this IP in the reset register. -1 if no
 *             reset is required when enabling the clock
 * @name: the clock name
 * @parent_name: the name of the parent clock
 * @flags: standard clock framework flags
 */
struct aspeed_gate_data {
	u8		clock_idx;
	s8		reset_idx;
	const char	*name;
	const char	*parent_name;
	unsigned long	flags;
};

/**
 * struct aspeed_clk_gate - Aspeed specific clk_gate structure
 * @hw:		handle between common and hardware-specific interfaces
 * @reg:	register controlling gate
 * @clock_idx:	bit used to gate this clock in the clock register
 * @reset_idx:	bit used to reset this IP in the reset register. -1 if no
 *		reset is required when enabling the clock
 * @flags:	hardware-specific flags
 * @lock:	register lock
 *
 * Some of the clocks in the Aspeed SoC must be put in reset before enabling.
 * This modified version of clk_gate allows an optional reset bit to be
 * specified.
 */
struct aspeed_clk_gate {
	struct clk_hw	hw;
	struct regmap	*map;
	u8		clock_idx;
	s8		reset_idx;
	u8		flags;
	spinlock_t	*lock;
};

#define to_aspeed_clk_gate(_hw) container_of(_hw, struct aspeed_clk_gate, hw)

/* TODO: ask Aspeed about the actual parent data */
static const struct aspeed_gate_data aspeed_gates[] = {
	/*				 clk rst   name			parent	flags */
	[ASPEED_CLK_GATE_ECLK] =	{  0, -1, "eclk-gate",		"eclk",	0 }, /* Video Engine */
	[ASPEED_CLK_GATE_GCLK] =	{  1,  7, "gclk-gate",		NULL,	0 }, /* 2D engine */
	[ASPEED_CLK_GATE_MCLK] =	{  2, -1, "mclk-gate",		"mpll",	CLK_IS_CRITICAL }, /* SDRAM */
	[ASPEED_CLK_GATE_VCLK] =	{  3,  6, "vclk-gate",		NULL,	0 }, /* Video Capture */
	[ASPEED_CLK_GATE_BCLK] =	{  4, 10, "bclk-gate",		"bclk",	0 }, /* PCIe/PCI */
	[ASPEED_CLK_GATE_DCLK] =	{  5, -1, "dclk-gate",		NULL,	0 }, /* DAC */
	[ASPEED_CLK_GATE_REFCLK] =	{  6, -1, "refclk-gate",	"clkin", CLK_IS_CRITICAL },
	[ASPEED_CLK_GATE_USBPORT2CLK] =	{  7,  3, "usb-port2-gate",	NULL,	0 }, /* USB2.0 Host port 2 */
	[ASPEED_CLK_GATE_LCLK] =	{  8,  5, "lclk-gate",		NULL,	0 }, /* LPC */
	[ASPEED_CLK_GATE_USBUHCICLK] =	{  9, 15, "usb-uhci-gate",	NULL,	0 }, /* USB1.1 (requires port 2 enabled) */
	[ASPEED_CLK_GATE_D1CLK] =	{ 10, 13, "d1clk-gate",		NULL,	0 }, /* GFX CRT */
	[ASPEED_CLK_GATE_YCLK] =	{ 13,  4, "yclk-gate",		NULL,	0 }, /* HAC */
	[ASPEED_CLK_GATE_USBPORT1CLK] = { 14, 14, "usb-port1-gate",	NULL,	0 }, /* USB2 hub/USB2 host port 1/USB1.1 dev */
	[ASPEED_CLK_GATE_UART1CLK] =	{ 15, -1, "uart1clk-gate",	"uart",	0 }, /* UART1 */
	[ASPEED_CLK_GATE_UART2CLK] =	{ 16, -1, "uart2clk-gate",	"uart",	0 }, /* UART2 */
	[ASPEED_CLK_GATE_UART5CLK] =	{ 17, -1, "uart5clk-gate",	"uart",	0 }, /* UART5 */
	[ASPEED_CLK_GATE_ESPICLK] =	{ 19, -1, "espiclk-gate",	NULL,	0 }, /* eSPI */
	[ASPEED_CLK_GATE_MAC1CLK] =	{ 20, 11, "mac1clk-gate",	"mac",	0 }, /* MAC1 */
	[ASPEED_CLK_GATE_MAC2CLK] =	{ 21, 12, "mac2clk-gate",	"mac",	0 }, /* MAC2 */
	[ASPEED_CLK_GATE_RSACLK] =	{ 24, -1, "rsaclk-gate",	NULL,	0 }, /* RSA */
	[ASPEED_CLK_GATE_UART3CLK] =	{ 25, -1, "uart3clk-gate",	"uart",	0 }, /* UART3 */
	[ASPEED_CLK_GATE_UART4CLK] =	{ 26, -1, "uart4clk-gate",	"uart",	0 }, /* UART4 */
	[ASPEED_CLK_GATE_SDCLKCLK] =	{ 27, 16, "sdclk-gate",		NULL,	0 }, /* SDIO/SD */
	[ASPEED_CLK_GATE_LHCCLK] =	{ 28, -1, "lhclk-gate",		"lhclk", 0 }, /* LPC master/LPC+ */
};

static void __init aspeed_cc_init(struct device_node *np)
{
	struct regmap *map;
	u32 val;
	int ret;
	int i;

	scu_base = of_iomap(np, 0);
	if (IS_ERR(scu_base))
		return;

	aspeed_clk_data = kzalloc(sizeof(*aspeed_clk_data) +
			sizeof(*aspeed_clk_data->hws) * ASPEED_NUM_CLKS,
			GFP_KERNEL);
	if (!aspeed_clk_data)
		return;

	/*
	 * This way all clocks fetched before the platform device probes,
	 * except those we assign here for early use, will be deferred.
	 */
	for (i = 0; i < ASPEED_NUM_CLKS; i++)
		aspeed_clk_data->hws[i] = ERR_PTR(-EPROBE_DEFER);

	map = syscon_node_to_regmap(np);
	if (IS_ERR(map)) {
		pr_err("no syscon regmap\n");
		return;
	}
	/*
	 * We check that the regmap works on this very first access,
	 * but as this is an MMIO-backed regmap, subsequent regmap
	 * access is not going to fail and we skip error checks from
	 * this point.
	 */
	ret = regmap_read(map, ASPEED_STRAP, &val);
	if (ret) {
		pr_err("failed to read strapping register\n");
		return;
	}

	aspeed_clk_data->num = ASPEED_NUM_CLKS;
	ret = of_clk_add_hw_provider(np, of_clk_hw_onecell_get, aspeed_clk_data);
	if (ret)
		pr_err("failed to add DT provider: %d\n", ret);
};
CLK_OF_DECLARE_DRIVER(aspeed_cc_g5, "aspeed,ast2500-scu", aspeed_cc_init);
CLK_OF_DECLARE_DRIVER(aspeed_cc_g4, "aspeed,ast2400-scu", aspeed_cc_init);
