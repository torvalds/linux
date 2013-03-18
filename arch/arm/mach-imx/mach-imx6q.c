/*
 * Copyright 2011 Freescale Semiconductor, Inc.
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
#include <linux/clkdev.h>
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
#include <asm/smp_twd.h>
#include <asm/hardware/cache-l2x0.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/mach/time.h>
#include <asm/system_misc.h>

#include "common.h"
#include "cpuidle.h"
#include "hardware.h"

#define IMX6Q_ANALOG_DIGPROG	0x260

static int imx6q_revision(void)
{
	struct device_node *np;
	void __iomem *base;
	static u32 rev;

	if (!rev) {
		np = of_find_compatible_node(NULL, NULL, "fsl,imx6q-anatop");
		if (!np)
			return IMX_CHIP_REVISION_UNKNOWN;
		base = of_iomap(np, 0);
		if (!base) {
			of_node_put(np);
			return IMX_CHIP_REVISION_UNKNOWN;
		}
		rev =  readl_relaxed(base + IMX6Q_ANALOG_DIGPROG);
		iounmap(base);
		of_node_put(np);
	}

	switch (rev & 0xff) {
	case 0:
		return IMX_CHIP_REVISION_1_0;
	case 1:
		return IMX_CHIP_REVISION_1_1;
	case 2:
		return IMX_CHIP_REVISION_1_2;
	default:
		return IMX_CHIP_REVISION_UNKNOWN;
	}
}

void imx6q_restart(char mode, const char *cmd)
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
	struct regmap *anatop;

#define HW_ANADIG_USB1_CHRG_DETECT		0x000001b0
#define HW_ANADIG_USB2_CHRG_DETECT		0x00000210

#define BM_ANADIG_USB_CHRG_DETECT_EN_B		0x00100000
#define BM_ANADIG_USB_CHRG_DETECT_CHK_CHRG_B	0x00080000

	anatop = syscon_regmap_lookup_by_compatible("fsl,imx6q-anatop");
	if (!IS_ERR(anatop)) {
		/*
		 * The external charger detector needs to be disabled,
		 * or the signal at DP will be poor
		 */
		regmap_write(anatop, HW_ANADIG_USB1_CHRG_DETECT,
				BM_ANADIG_USB_CHRG_DETECT_EN_B
				| BM_ANADIG_USB_CHRG_DETECT_CHK_CHRG_B);
		regmap_write(anatop, HW_ANADIG_USB2_CHRG_DETECT,
				BM_ANADIG_USB_CHRG_DETECT_EN_B |
				BM_ANADIG_USB_CHRG_DETECT_CHK_CHRG_B);
	} else {
		pr_warn("failed to find fsl,imx6q-anatop regmap\n");
	}
}

static void __init imx6q_init_machine(void)
{
	if (of_machine_is_compatible("fsl,imx6q-sabrelite"))
		imx6q_sabrelite_init();

	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);

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

struct platform_device imx6q_cpufreq_pdev = {
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

static void __init imx6q_init_irq(void)
{
	l2x0_of_init(0, ~0UL);
	imx_src_init();
	imx_gpc_init();
	irqchip_init();
}

static void __init imx6q_timer_init(void)
{
	mx6q_clocks_init();
	twd_local_timer_of_register();
	imx_print_silicon_rev("i.MX6Q", imx6q_revision());
}

static const char *imx6q_dt_compat[] __initdata = {
	"fsl,imx6q",
	NULL,
};

DT_MACHINE_START(IMX6Q, "Freescale i.MX6 Quad (Device Tree)")
	.smp		= smp_ops(imx_smp_ops),
	.map_io		= imx6q_map_io,
	.init_irq	= imx6q_init_irq,
	.init_time	= imx6q_timer_init,
	.init_machine	= imx6q_init_machine,
	.init_late      = imx6q_init_late,
	.dt_compat	= imx6q_dt_compat,
	.restart	= imx6q_restart,
MACHINE_END
