/*
 * Copyright 2011-2013 Freescale Semiconductor, Inc.
 * Copyright 2011 Linaro Ltd.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/clocksource.h>
#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/opp.h>
#include <linux/phy.h>
#include <linux/regmap.h>
#include <linux/micrel_phy.h>
#include <linux/mfd/syscon.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/system_misc.h>

#include "common.h"
#include "cpuidle.h"
#include "hardware.h"

static u32 chip_revision;

int imx6q_revision(void)
{
	return chip_revision;
}

static void __init imx6q_init_revision(void)
{
	u32 rev = imx_anatop_get_digprog();

	switch (rev & 0xff) {
	case 0:
		chip_revision = IMX_CHIP_REVISION_1_0;
		break;
	case 1:
		chip_revision = IMX_CHIP_REVISION_1_1;
		break;
	case 2:
		chip_revision = IMX_CHIP_REVISION_1_2;
		break;
	default:
		chip_revision = IMX_CHIP_REVISION_UNKNOWN;
	}

	mxc_set_cpu_type(rev >> 16 & 0xff);
}

static void imx6q_restart(char mode, const char *cmd)
{
	struct device_node *np;
	void __iomem *wdog_base;

	np = of_find_compatible_node(NULL, NULL, "fsl,imx6q-wdt");
	wdog_base = of_iomap(np, 0);
	if (!wdog_base)
		goto soft;

	imx_src_prepare_restart();

	/* enable wdog */
	writew_relaxed(1 << 2, wdog_base);
	/* write twice to ensure the request will not get ignored */
	writew_relaxed(1 << 2, wdog_base);

	/* wait for reset to assert ... */
	mdelay(500);

	pr_err("Watchdog reset failed to assert reset\n");

	/* delay to allow the serial port to show the message */
	mdelay(50);

soft:
	/* we'll take a jump through zero as a poor second */
	soft_restart(0);
}

/* For imx6q sabrelite board: set KSZ9021RN RGMII pad skew */
static int ksz9021rn_phy_fixup(struct phy_device *phydev)
{
	if (IS_BUILTIN(CONFIG_PHYLIB)) {
		/* min rx data delay */
		phy_write(phydev, 0x0b, 0x8105);
		phy_write(phydev, 0x0c, 0x0000);

		/* max rx/tx clock delay, min rx/tx control delay */
		phy_write(phydev, 0x0b, 0x8104);
		phy_write(phydev, 0x0c, 0xf0f0);
		phy_write(phydev, 0x0b, 0x104);
	}

	return 0;
}

static void __init imx6q_sabrelite_cko1_setup(void)
{
	struct clk *cko1_sel, *ahb, *cko1;
	unsigned long rate;

	cko1_sel = clk_get_sys(NULL, "cko1_sel");
	ahb = clk_get_sys(NULL, "ahb");
	cko1 = clk_get_sys(NULL, "cko1");
	if (IS_ERR(cko1_sel) || IS_ERR(ahb) || IS_ERR(cko1)) {
		pr_err("cko1 setup failed!\n");
		goto put_clk;
	}
	clk_set_parent(cko1_sel, ahb);
	rate = clk_round_rate(cko1, 16000000);
	clk_set_rate(cko1, rate);
put_clk:
	if (!IS_ERR(cko1_sel))
		clk_put(cko1_sel);
	if (!IS_ERR(ahb))
		clk_put(ahb);
	if (!IS_ERR(cko1))
		clk_put(cko1);
}

static void __init imx6q_sabrelite_init(void)
{
	if (IS_BUILTIN(CONFIG_PHYLIB))
		phy_register_fixup_for_uid(PHY_ID_KSZ9021, MICREL_PHY_ID_MASK,
				ksz9021rn_phy_fixup);
	imx6q_sabrelite_cko1_setup();
}

static void __init imx6q_sabresd_cko1_setup(void)
{
	struct clk *cko1_sel, *pll4, *pll4_post, *cko1;
	unsigned long rate;

	cko1_sel = clk_get_sys(NULL, "cko1_sel");
	pll4 = clk_get_sys(NULL, "pll4_audio");
	pll4_post = clk_get_sys(NULL, "pll4_post_div");
	cko1 = clk_get_sys(NULL, "cko1");
	if (IS_ERR(cko1_sel) || IS_ERR(pll4)
			|| IS_ERR(pll4_post) || IS_ERR(cko1)) {
		pr_err("cko1 setup failed!\n");
		goto put_clk;
	}
	/*
	 * Setting pll4 at 768MHz (24MHz * 32)
	 * So its child clock can get 24MHz easily
	 */
	clk_set_rate(pll4, 768000000);

	clk_set_parent(cko1_sel, pll4_post);
	rate = clk_round_rate(cko1, 24000000);
	clk_set_rate(cko1, rate);
put_clk:
	if (!IS_ERR(cko1_sel))
		clk_put(cko1_sel);
	if (!IS_ERR(pll4_post))
		clk_put(pll4_post);
	if (!IS_ERR(pll4))
		clk_put(pll4);
	if (!IS_ERR(cko1))
		clk_put(cko1);
}

static void __init imx6q_sabresd_init(void)
{
	imx6q_sabresd_cko1_setup();
}

static void __init imx6q_1588_init(void)
{
	struct regmap *gpr;

	gpr = syscon_regmap_lookup_by_compatible("fsl,imx6q-iomuxc-gpr");
	if (!IS_ERR(gpr))
		regmap_update_bits(gpr, 0x4, 1 << 21, 1 << 21);
	else
		pr_err("failed to find fsl,imx6q-iomux-gpr regmap\n");

}
static void __init imx6q_usb_init(void)
{
	imx_anatop_usb_chrg_detect_disable();
}

static void __init imx6q_init_machine(void)
{
	if (of_machine_is_compatible("fsl,imx6q-sabrelite"))
		imx6q_sabrelite_init();
	else if (of_machine_is_compatible("fsl,imx6q-sabresd") ||
			of_machine_is_compatible("fsl,imx6dl-sabresd"))
		imx6q_sabresd_init();

	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);

	imx_anatop_init();
	imx6q_pm_init();
	imx6q_usb_init();
	imx6q_1588_init();
}

#define OCOTP_CFG3			0x440
#define OCOTP_CFG3_SPEED_SHIFT		16
#define OCOTP_CFG3_SPEED_1P2GHZ		0x3

static void __init imx6q_opp_check_1p2ghz(struct device *cpu_dev)
{
	struct device_node *np;
	void __iomem *base;
	u32 val;

	np = of_find_compatible_node(NULL, NULL, "fsl,imx6q-ocotp");
	if (!np) {
		pr_warn("failed to find ocotp node\n");
		return;
	}

	base = of_iomap(np, 0);
	if (!base) {
		pr_warn("failed to map ocotp\n");
		goto put_node;
	}

	val = readl_relaxed(base + OCOTP_CFG3);
	val >>= OCOTP_CFG3_SPEED_SHIFT;
	if ((val & 0x3) != OCOTP_CFG3_SPEED_1P2GHZ)
		if (opp_disable(cpu_dev, 1200000000))
			pr_warn("failed to disable 1.2 GHz OPP\n");

put_node:
	of_node_put(np);
}

static void __init imx6q_opp_init(struct device *cpu_dev)
{
	struct device_node *np;

	np = of_find_node_by_path("/cpus/cpu@0");
	if (!np) {
		pr_warn("failed to find cpu0 node\n");
		return;
	}

	cpu_dev->of_node = np;
	if (of_init_opp_table(cpu_dev)) {
		pr_warn("failed to init OPP table\n");
		goto put_node;
	}

	imx6q_opp_check_1p2ghz(cpu_dev);

put_node:
	of_node_put(np);
}

static struct platform_device imx6q_cpufreq_pdev = {
	.name = "imx6q-cpufreq",
};

static void __init imx6q_init_late(void)
{
	/*
	 * WAIT mode is broken on TO 1.0 and 1.1, so there is no point
	 * to run cpuidle on them.
	 */
	if (imx6q_revision() > IMX_CHIP_REVISION_1_1)
		imx6q_cpuidle_init();

	if (IS_ENABLED(CONFIG_ARM_IMX6Q_CPUFREQ)) {
		imx6q_opp_init(&imx6q_cpufreq_pdev.dev);
		platform_device_register(&imx6q_cpufreq_pdev);
	}
}

static void __init imx6q_map_io(void)
{
	debug_ll_io_init();
	imx_scu_map_io();
}

#ifdef CONFIG_CACHE_L2X0
static void __init imx6q_init_l2cache(void)
{
	void __iomem *l2x0_base;
	struct device_node *np;
	unsigned int val;

	np = of_find_compatible_node(NULL, NULL, "arm,pl310-cache");
	if (!np)
		goto out;

	l2x0_base = of_iomap(np, 0);
	if (!l2x0_base) {
		of_node_put(np);
		goto out;
	}

	/* Configure the L2 PREFETCH and POWER registers */
	val = readl_relaxed(l2x0_base + L2X0_PREFETCH_CTRL);
	val |= 0x70800000;
	writel_relaxed(val, l2x0_base + L2X0_PREFETCH_CTRL);
	val = L2X0_DYNAMIC_CLK_GATING_EN | L2X0_STNDBY_MODE_EN;
	writel_relaxed(val, l2x0_base + L2X0_POWER_CTRL);

	iounmap(l2x0_base);
	of_node_put(np);

out:
	l2x0_of_init(0, ~0UL);
}
#else
static inline void imx6q_init_l2cache(void) {}
#endif

static void __init imx6q_init_irq(void)
{
	imx6q_init_revision();
	imx6q_init_l2cache();
	imx_src_init();
	imx_gpc_init();
	irqchip_init();
}

static void __init imx6q_timer_init(void)
{
	of_clk_init(NULL);
	clocksource_of_init();
	imx_print_silicon_rev(cpu_is_imx6dl() ? "i.MX6DL" : "i.MX6Q",
			      imx6q_revision());
}

static const char *imx6q_dt_compat[] __initdata = {
	"fsl,imx6dl",
	"fsl,imx6q",
	NULL,
};

DT_MACHINE_START(IMX6Q, "Freescale i.MX6 Quad/DualLite (Device Tree)")
	.smp		= smp_ops(imx_smp_ops),
	.map_io		= imx6q_map_io,
	.init_irq	= imx6q_init_irq,
	.init_time	= imx6q_timer_init,
	.init_machine	= imx6q_init_machine,
	.init_late      = imx6q_init_late,
	.dt_compat	= imx6q_dt_compat,
	.restart	= imx6q_restart,
MACHINE_END
