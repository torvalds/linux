// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2011-2013 Freescale Semiconductor, Inc.
 * Copyright 2011 Linaro Ltd.
 */

#include <linux/clk.h>
#include <linux/irqchip.h>
#include <linux/of_platform.h>
#include <linux/pci.h>
#include <linux/phy.h>
#include <linux/regmap.h>
#include <linux/micrel_phy.h>
#include <linux/mfd/syscon.h>
#include <linux/mfd/syscon/imx6q-iomuxc-gpr.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "common.h"
#include "cpuidle.h"
#include "hardware.h"

/* For imx6q sabrelite board: set KSZ9021RN RGMII pad skew */
static int ksz9021rn_phy_fixup(struct phy_device *phydev)
{
	if (IS_BUILTIN(CONFIG_PHYLIB)) {
		/* min rx data delay */
		phy_write(phydev, MICREL_KSZ9021_EXTREG_CTRL,
			0x8000 | MICREL_KSZ9021_RGMII_RX_DATA_PAD_SCEW);
		phy_write(phydev, MICREL_KSZ9021_EXTREG_DATA_WRITE, 0x0000);

		/* max rx/tx clock delay, min rx/tx control delay */
		phy_write(phydev, MICREL_KSZ9021_EXTREG_CTRL,
			0x8000 | MICREL_KSZ9021_RGMII_CLK_CTRL_PAD_SCEW);
		phy_write(phydev, MICREL_KSZ9021_EXTREG_DATA_WRITE, 0xf0f0);
		phy_write(phydev, MICREL_KSZ9021_EXTREG_CTRL,
			MICREL_KSZ9021_RGMII_CLK_CTRL_PAD_SCEW);
	}

	return 0;
}

/*
 * fixup for PLX PEX8909 bridge to configure GPIO1-7 as output High
 * as they are used for slots1-7 PERST#
 */
static void ventana_pciesw_early_fixup(struct pci_dev *dev)
{
	u32 dw;

	if (!of_machine_is_compatible("gw,ventana"))
		return;

	if (dev->devfn != 0)
		return;

	pci_read_config_dword(dev, 0x62c, &dw);
	dw |= 0xaaa8; // GPIO1-7 outputs
	pci_write_config_dword(dev, 0x62c, dw);

	pci_read_config_dword(dev, 0x644, &dw);
	dw |= 0xfe;   // GPIO1-7 output high
	pci_write_config_dword(dev, 0x644, dw);

	msleep(100);
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_PLX, 0x8609, ventana_pciesw_early_fixup);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_PLX, 0x8606, ventana_pciesw_early_fixup);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_PLX, 0x8604, ventana_pciesw_early_fixup);

static void __init imx6q_enet_phy_init(void)
{
	if (IS_BUILTIN(CONFIG_PHYLIB)) {
		phy_register_fixup_for_uid(PHY_ID_KSZ9021, MICREL_PHY_ID_MASK,
				ksz9021rn_phy_fixup);
	}
}

static void __init imx6q_1588_init(void)
{
	struct device_node *np;
	struct clk *ptp_clk;
	struct clk *enet_ref;
	struct regmap *gpr;
	u32 clksel;

	np = of_find_compatible_node(NULL, NULL, "fsl,imx6q-fec");
	if (!np) {
		pr_warn("%s: failed to find fec node\n", __func__);
		return;
	}

	ptp_clk = of_clk_get(np, 2);
	if (IS_ERR(ptp_clk)) {
		pr_warn("%s: failed to get ptp clock\n", __func__);
		goto put_node;
	}

	enet_ref = clk_get_sys(NULL, "enet_ref");
	if (IS_ERR(enet_ref)) {
		pr_warn("%s: failed to get enet clock\n", __func__);
		goto put_ptp_clk;
	}

	/*
	 * If enet_ref from ANATOP/CCM is the PTP clock source, we need to
	 * set bit IOMUXC_GPR1[21].  Or the PTP clock must be from pad
	 * (external OSC), and we need to clear the bit.
	 */
	clksel = clk_is_match(ptp_clk, enet_ref) ?
				IMX6Q_GPR1_ENET_CLK_SEL_ANATOP :
				IMX6Q_GPR1_ENET_CLK_SEL_PAD;
	gpr = syscon_regmap_lookup_by_compatible("fsl,imx6q-iomuxc-gpr");
	if (!IS_ERR(gpr))
		regmap_update_bits(gpr, IOMUXC_GPR1,
				IMX6Q_GPR1_ENET_CLK_SEL_MASK,
				clksel);
	else
		pr_err("failed to find fsl,imx6q-iomuxc-gpr regmap\n");

	clk_put(enet_ref);
put_ptp_clk:
	clk_put(ptp_clk);
put_node:
	of_node_put(np);
}

static void __init imx6q_axi_init(void)
{
	struct regmap *gpr;
	unsigned int mask;

	gpr = syscon_regmap_lookup_by_compatible("fsl,imx6q-iomuxc-gpr");
	if (!IS_ERR(gpr)) {
		/*
		 * Enable the cacheable attribute of VPU and IPU
		 * AXI transactions.
		 */
		mask = IMX6Q_GPR4_VPU_WR_CACHE_SEL |
			IMX6Q_GPR4_VPU_RD_CACHE_SEL |
			IMX6Q_GPR4_VPU_P_WR_CACHE_VAL |
			IMX6Q_GPR4_VPU_P_RD_CACHE_VAL_MASK |
			IMX6Q_GPR4_IPU_WR_CACHE_CTL |
			IMX6Q_GPR4_IPU_RD_CACHE_CTL;
		regmap_update_bits(gpr, IOMUXC_GPR4, mask, mask);

		/* Increase IPU read QoS priority */
		regmap_update_bits(gpr, IOMUXC_GPR6,
				IMX6Q_GPR6_IPU1_ID00_RD_QOS_MASK |
				IMX6Q_GPR6_IPU1_ID01_RD_QOS_MASK,
				(0xf << 16) | (0x7 << 20));
		regmap_update_bits(gpr, IOMUXC_GPR7,
				IMX6Q_GPR7_IPU2_ID00_RD_QOS_MASK |
				IMX6Q_GPR7_IPU2_ID01_RD_QOS_MASK,
				(0xf << 16) | (0x7 << 20));
	} else {
		pr_warn("failed to find fsl,imx6q-iomuxc-gpr regmap\n");
	}
}

static void __init imx6q_init_machine(void)
{
	if (cpu_is_imx6q() && imx_get_soc_revision() >= IMX_CHIP_REVISION_2_0)
		/*
		 * SoCs that identify as i.MX6Q >= rev 2.0 are really i.MX6QP.
		 * Quirk: i.MX6QP revision = i.MX6Q revision - (1, 0),
		 * e.g. i.MX6QP rev 1.1 identifies as i.MX6Q rev 2.1.
		 */
		imx_print_silicon_rev("i.MX6QP", imx_get_soc_revision() - 0x10);
	else
		imx_print_silicon_rev(cpu_is_imx6dl() ? "i.MX6DL" : "i.MX6Q",
				imx_get_soc_revision());

	imx6q_enet_phy_init();
	imx_anatop_init();
	cpu_is_imx6q() ?  imx6q_pm_init() : imx6dl_pm_init();
	imx6q_1588_init();
	imx6q_axi_init();
}

static void __init imx6q_init_late(void)
{
	/*
	 * WAIT mode is broken on imx6 Dual/Quad revision 1.0 and 1.1 so
	 * there is no point to run cpuidle on them.
	 *
	 * It does work on imx6 Solo/DualLite starting from 1.1
	 */
	if ((cpu_is_imx6q() && imx_get_soc_revision() > IMX_CHIP_REVISION_1_1) ||
	    (cpu_is_imx6dl() && imx_get_soc_revision() > IMX_CHIP_REVISION_1_0))
		imx6q_cpuidle_init();

	if (IS_ENABLED(CONFIG_ARM_IMX6Q_CPUFREQ))
		platform_device_register_simple("imx6q-cpufreq", -1, NULL, 0);
}

static void __init imx6q_map_io(void)
{
	debug_ll_io_init();
	imx_scu_map_io();
}

static void __init imx6q_init_irq(void)
{
	imx_gpc_check_dt();
	imx_init_revision_from_anatop();
	imx_init_l2cache();
	imx_src_init();
	irqchip_init();
	imx6_pm_ccm_init("fsl,imx6q-ccm");
}

static const char * const imx6q_dt_compat[] __initconst = {
	"fsl,imx6dl",
	"fsl,imx6q",
	"fsl,imx6qp",
	NULL,
};

DT_MACHINE_START(IMX6Q, "Freescale i.MX6 Quad/DualLite (Device Tree)")
	.l2c_aux_val 	= 0,
	.l2c_aux_mask	= ~0,
	.smp		= smp_ops(imx_smp_ops),
	.map_io		= imx6q_map_io,
	.init_irq	= imx6q_init_irq,
	.init_machine	= imx6q_init_machine,
	.init_late      = imx6q_init_late,
	.dt_compat	= imx6q_dt_compat,
MACHINE_END
