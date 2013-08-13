/*
 *  Copyright (C) 2012-2013 Altera Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/dw_apb_timer.h>
#include <linux/clk-provider.h>
#include <linux/irqchip.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/of_net.h>
#include <linux/stmmac.h>
#include <linux/phy.h>
#include <linux/micrel_phy.h>
#include <linux/sys_soc.h>

#include <asm/hardware/cache-l2x0.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <asm/pmu.h>

#include "core.h"
#include "socfpga_cti.h"

void __iomem *socfpga_scu_base_addr = ((void __iomem *)(SOCFPGA_SCU_VIRT_BASE));
void __iomem *sys_manager_base_addr;
void __iomem *rst_manager_base_addr;
void __iomem *sdr_ctl_base_addr;
void __iomem *l3regs_base_addr;

void __iomem *clk_mgr_base_addr;
unsigned long cpu1start_addr;

static int stmmac_plat_init(struct platform_device *pdev);

static struct plat_stmmacenet_data stmmacenet0_data = {
	.init = &stmmac_plat_init,
	.bus_id = 0,
};

static struct plat_stmmacenet_data stmmacenet1_data = {
	.init = &stmmac_plat_init,
	.bus_id = 1,
};

#ifdef CONFIG_HW_PERF_EVENTS
static struct arm_pmu_platdata socfpga_pmu_platdata = {
	.handle_irq = socfpga_pmu_handler,
	.init = socfpga_init_cti,
	.start = socfpga_start_cti,
	.stop = socfpga_stop_cti,
};
#endif

static const struct of_dev_auxdata socfpga_auxdata_lookup[] __initconst = {
	OF_DEV_AUXDATA("snps,dwmac-3.70a", 0xff700000, NULL, &stmmacenet0_data),
	OF_DEV_AUXDATA("snps,dwmac-3.70a", 0xff702000, NULL, &stmmacenet1_data),
#ifdef CONFIG_HW_PERF_EVENTS
	OF_DEV_AUXDATA("arm,cortex-a9-pmu", 0, "arm-pmu", &socfpga_pmu_platdata),
#endif
	{ /* sentinel */ }
};

static struct map_desc scu_io_desc __initdata = {
	.virtual	= SOCFPGA_SCU_VIRT_BASE,
	.pfn		= 0, /* run-time */
	.length		= SZ_8K,
	.type		= MT_DEVICE,
};

static struct map_desc uart_io_desc __initdata = {
	.virtual        = 0xfec02000,
	.pfn            = __phys_to_pfn(0xffc02000),
	.length         = SZ_8K,
	.type           = MT_DEVICE,
};

static void __init socfpga_soc_device_init(void)
{
	struct device_node *root;
	struct device_node *sysid_node;
	struct soc_device *soc_dev;
	struct soc_device_attribute *soc_dev_attr;
	void __iomem *sysid_base;
	const char *machine;
	u32 id = SOCFPGA_SYSID_DEFAULT;
	int err;

	root = of_find_node_by_path("/");
	if (!root)
		return;

	err = of_property_read_string(root, "model", &machine);
	if (err)
		return;

	soc_dev_attr = kzalloc(sizeof(*soc_dev_attr), GFP_KERNEL);
	if (!soc_dev_attr)
		return;

	sysid_node = of_find_compatible_node(root, NULL, "ALTR,sysid-1.0");
	if (sysid_node) {
		sysid_base = of_iomap(sysid_node, 0);
		if (sysid_base) {
			/* Use id from Sysid hardware. */
			id = readl(sysid_base + SYSID_ID_REG);
			iounmap(sysid_base);
		}
		of_node_put(sysid_node);
	}

	of_node_put(root);

	soc_dev_attr->soc_id = kasprintf(GFP_KERNEL, "%u", id);
	soc_dev_attr->revision = kasprintf(GFP_KERNEL, "%d",
		SOCFPGA_REVISION_DEFAULT);
	soc_dev_attr->machine = kasprintf(GFP_KERNEL, "%s", machine);
	soc_dev_attr->family = "SOCFPGA";

	soc_dev = soc_device_register(soc_dev_attr);
	if (IS_ERR_OR_NULL(soc_dev)) {
		kfree(soc_dev_attr->soc_id);
		kfree(soc_dev_attr->machine);
		kfree(soc_dev_attr->revision);
		kfree(soc_dev_attr);
		return;
	}

	return;
}

static void __init socfpga_scu_map_io(void)
{
	unsigned long base;

	/* Get SCU base */
	asm("mrc p15, 4, %0, c15, c0, 0" : "=r" (base));

	scu_io_desc.pfn = __phys_to_pfn(base);
	iotable_init(&scu_io_desc, 1);
}

static void __init enable_periphs(void)
{
	/* Release all peripherals from reset.*/
	writel(0, rst_manager_base_addr + SOCFPGA_RSTMGR_MODPERRST);
}

#define MICREL_KSZ9021_EXTREG_CTRL 11
#define MICREL_KSZ9021_EXTREG_DATA_WRITE 12
#define MICREL_KSZ9021_RGMII_CLK_CTRL_PAD_SCEW 260
#define MICREL_KSZ9021_RGMII_RX_DATA_PAD_SCEW 261

static int ksz9021rlrn_phy_fixup(struct phy_device *phydev)
{
	if (IS_BUILTIN(CONFIG_PHYLIB)) {
		/* min rx data delay */
		phy_write(phydev, MICREL_KSZ9021_EXTREG_CTRL,
			MICREL_KSZ9021_RGMII_RX_DATA_PAD_SCEW | 0x8000);
		phy_write(phydev, MICREL_KSZ9021_EXTREG_DATA_WRITE, 0x0000);

		/* max rx/tx clock delay, min rx/tx control delay */
		phy_write(phydev, MICREL_KSZ9021_EXTREG_CTRL,
			MICREL_KSZ9021_RGMII_CLK_CTRL_PAD_SCEW | 0x8000);
		phy_write(phydev, MICREL_KSZ9021_EXTREG_DATA_WRITE, 0xa0d0);
		phy_write(phydev, MICREL_KSZ9021_EXTREG_CTRL, 0x104);
	}

	return 0;
}

static int stmmac_plat_init(struct platform_device *pdev)
{
	u32 ctrl, val, shift;
	int phymode;

	if (of_machine_is_compatible("altr,socfpga-vt"))
		return 0;

	phymode = of_get_phy_mode(pdev->dev.of_node);

	switch (phymode) {
	case PHY_INTERFACE_MODE_RGMII:
		val = SYSMGR_EMACGRP_CTRL_PHYSEL_ENUM_RGMII;
		break;
	case PHY_INTERFACE_MODE_MII:
	case PHY_INTERFACE_MODE_GMII:
		val = SYSMGR_EMACGRP_CTRL_PHYSEL_ENUM_GMII_MII;
		break;
	default:
		pr_err("%s bad phy mode %d", __func__, phymode);
		return -EINVAL;
	}

	if (&stmmacenet1_data == pdev->dev.platform_data)
		shift = SYSMGR_EMACGRP_CTRL_PHYSEL_WIDTH;
	else if (&stmmacenet0_data == pdev->dev.platform_data)
		shift = 0;
	else {
		pr_err("%s unexpected platform data pointer\n", __func__);
		return -EINVAL;
	}

	ctrl = readl(sys_manager_base_addr + SYSMGR_EMACGRP_CTRL_OFFSET);
	ctrl &= ~(SYSMGR_EMACGRP_CTRL_PHYSEL_MASK << shift);
	ctrl |= (val << shift);

	writel(ctrl, (sys_manager_base_addr + SYSMGR_EMACGRP_CTRL_OFFSET));

	return 0;
}

static void __init socfpga_map_io(void)
{
	socfpga_scu_map_io();
	iotable_init(&uart_io_desc, 1);
	early_printk("Early printk initialized\n");
}

static void __init socfpga_sysmgr_init(void)
{
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "altr,sys-mgr");
	if (!np) {
		pr_err("SOCFPGA: Unable to find sys-magr in dtb\n");
		return;
	}

	if (of_property_read_u32(np, "cpu1-start-addr",
			(u32 *) &cpu1start_addr))
		pr_err("SMP: Need cpu1-start-addr in device tree.\n");

	sys_manager_base_addr = of_iomap(np, 0);
	WARN_ON(!sys_manager_base_addr);

	np = of_find_compatible_node(NULL, NULL, "altr,rst-mgr");
	if (!np) {
		pr_err("SOCFPGA: Unable to find rst-mgr in dtb\n");
		return;
	}

	rst_manager_base_addr = of_iomap(np, 0);
	WARN_ON(!rst_manager_base_addr);

	np = of_find_compatible_node(NULL, NULL, "altr,clk-mgr");
	if (!np) {
		pr_err("SOCFPGA: Unable to find clk-mgr\n");
		return;
	}

	clk_mgr_base_addr = of_iomap(np, 0);
	WARN_ON(!clk_mgr_base_addr);

	np = of_find_compatible_node(NULL, NULL, "altr,sdr-ctl");
	if (!np) {
		pr_err("SOCFPGA: Unable to find sdr-ctl\n");
		return;
	}

	sdr_ctl_base_addr = of_iomap(np, 0);
	WARN_ON(!sdr_ctl_base_addr);

	np = of_find_compatible_node(NULL, NULL, "altr,l3regs");
	if (!np) {
		pr_err("SOCFPGA: Unable to find l3regs\n");
		return;
	}

	l3regs_base_addr = of_iomap(np, 0);
	WARN_ON(!l3regs_base_addr);
}

static void __init socfpga_init_irq(void)
{
	irqchip_init();
	socfpga_sysmgr_init();

	of_clk_init(NULL);
	socfpga_init_clocks();
	clocksource_of_init();
}

static void socfpga_cyclone5_restart(char mode, const char *cmd)
{
	u32 temp;

	temp = readl(rst_manager_base_addr + SOCFPGA_RSTMGR_CTRL);

	if (mode == 'h')
		temp |= RSTMGR_CTRL_SWCOLDRSTREQ;
	else
		temp |= RSTMGR_CTRL_SWWARMRSTREQ;
	writel(temp, rst_manager_base_addr + SOCFPGA_RSTMGR_CTRL);
}

static void __init socfpga_cyclone5_init(void)
{
#ifdef CONFIG_CACHE_L2X0
	u32 aux_ctrl = 0;
	aux_ctrl |= (1 << L2X0_AUX_CTRL_SHARE_OVERRIDE_SHIFT) |
			(1 << L2X0_AUX_CTRL_DATA_PREFETCH_SHIFT) |
			(1 << L2X0_AUX_CTRL_INSTR_PREFETCH_SHIFT);
	l2x0_of_init(aux_ctrl, ~0UL);
#endif
	of_platform_populate(NULL, of_default_bus_match_table,
		socfpga_auxdata_lookup, NULL);
	
	enable_periphs();

	socfpga_soc_device_init();
	if (IS_BUILTIN(CONFIG_PHYLIB))
		phy_register_fixup_for_uid(PHY_ID_KSZ9021RLRN,
			MICREL_PHY_ID_MASK, ksz9021rlrn_phy_fixup);
}

static const char *altera_dt_match[] = {
	"altr,socfpga",
	NULL
};

DT_MACHINE_START(SOCFPGA, "Altera SOCFPGA")
	.smp		= smp_ops(socfpga_smp_ops),
	.map_io		= socfpga_map_io,
	.init_irq	= socfpga_init_irq,
	.init_time	= dw_apb_timer_init,
	.init_machine	= socfpga_cyclone5_init,
	.restart	= socfpga_cyclone5_restart,
	.dt_compat	= altera_dt_match,
MACHINE_END
