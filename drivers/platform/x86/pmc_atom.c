// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel Atom SoC Power Management Controller Driver
 * Copyright (c) 2014-2015,2017,2022 Intel Corporation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/dmi.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/platform_data/x86/clk-pmc-atom.h>
#include <linux/platform_data/x86/pmc_atom.h>
#include <linux/platform_data/x86/simatic-ipc.h>
#include <linux/platform_device.h>
#include <linux/pci.h>
#include <linux/seq_file.h>

struct pmc_bit_map {
	const char *name;
	u32 bit_mask;
};

struct pmc_reg_map {
	const struct pmc_bit_map *d3_sts_0;
	const struct pmc_bit_map *d3_sts_1;
	const struct pmc_bit_map *func_dis;
	const struct pmc_bit_map *func_dis_2;
	const struct pmc_bit_map *pss;
};

struct pmc_data {
	const struct pmc_reg_map *map;
	const struct pmc_clk *clks;
};

struct pmc_dev {
	u32 base_addr;
	void __iomem *regmap;
	const struct pmc_reg_map *map;
#ifdef CONFIG_DEBUG_FS
	struct dentry *dbgfs_dir;
#endif /* CONFIG_DEBUG_FS */
	bool init;
};

static struct pmc_dev pmc_device;
static u32 acpi_base_addr;

static const struct pmc_clk byt_clks[] = {
	{
		.name = "xtal",
		.freq = 25000000,
		.parent_name = NULL,
	},
	{
		.name = "pll",
		.freq = 19200000,
		.parent_name = "xtal",
	},
	{}
};

static const struct pmc_clk cht_clks[] = {
	{
		.name = "xtal",
		.freq = 19200000,
		.parent_name = NULL,
	},
	{}
};

static const struct pmc_bit_map d3_sts_0_map[] = {
	{"LPSS1_F0_DMA",	BIT_LPSS1_F0_DMA},
	{"LPSS1_F1_PWM1",	BIT_LPSS1_F1_PWM1},
	{"LPSS1_F2_PWM2",	BIT_LPSS1_F2_PWM2},
	{"LPSS1_F3_HSUART1",	BIT_LPSS1_F3_HSUART1},
	{"LPSS1_F4_HSUART2",	BIT_LPSS1_F4_HSUART2},
	{"LPSS1_F5_SPI",	BIT_LPSS1_F5_SPI},
	{"LPSS1_F6_Reserved",	BIT_LPSS1_F6_XXX},
	{"LPSS1_F7_Reserved",	BIT_LPSS1_F7_XXX},
	{"SCC_EMMC",		BIT_SCC_EMMC},
	{"SCC_SDIO",		BIT_SCC_SDIO},
	{"SCC_SDCARD",		BIT_SCC_SDCARD},
	{"SCC_MIPI",		BIT_SCC_MIPI},
	{"HDA",			BIT_HDA},
	{"LPE",			BIT_LPE},
	{"OTG",			BIT_OTG},
	{"USH",			BIT_USH},
	{"GBE",			BIT_GBE},
	{"SATA",		BIT_SATA},
	{"USB_EHCI",		BIT_USB_EHCI},
	{"SEC",			BIT_SEC},
	{"PCIE_PORT0",		BIT_PCIE_PORT0},
	{"PCIE_PORT1",		BIT_PCIE_PORT1},
	{"PCIE_PORT2",		BIT_PCIE_PORT2},
	{"PCIE_PORT3",		BIT_PCIE_PORT3},
	{"LPSS2_F0_DMA",	BIT_LPSS2_F0_DMA},
	{"LPSS2_F1_I2C1",	BIT_LPSS2_F1_I2C1},
	{"LPSS2_F2_I2C2",	BIT_LPSS2_F2_I2C2},
	{"LPSS2_F3_I2C3",	BIT_LPSS2_F3_I2C3},
	{"LPSS2_F3_I2C4",	BIT_LPSS2_F4_I2C4},
	{"LPSS2_F5_I2C5",	BIT_LPSS2_F5_I2C5},
	{"LPSS2_F6_I2C6",	BIT_LPSS2_F6_I2C6},
	{"LPSS2_F7_I2C7",	BIT_LPSS2_F7_I2C7},
	{}
};

static struct pmc_bit_map byt_d3_sts_1_map[] = {
	{"SMB",			BIT_SMB},
	{"OTG_SS_PHY",		BIT_OTG_SS_PHY},
	{"USH_SS_PHY",		BIT_USH_SS_PHY},
	{"DFX",			BIT_DFX},
	{}
};

static struct pmc_bit_map cht_d3_sts_1_map[] = {
	{"SMB",			BIT_SMB},
	{"GMM",			BIT_STS_GMM},
	{"ISH",			BIT_STS_ISH},
	{}
};

static struct pmc_bit_map cht_func_dis_2_map[] = {
	{"SMB",			BIT_SMB},
	{"GMM",			BIT_FD_GMM},
	{"ISH",			BIT_FD_ISH},
	{}
};

static const struct pmc_bit_map byt_pss_map[] = {
	{"GBE",			PMC_PSS_BIT_GBE},
	{"SATA",		PMC_PSS_BIT_SATA},
	{"HDA",			PMC_PSS_BIT_HDA},
	{"SEC",			PMC_PSS_BIT_SEC},
	{"PCIE",		PMC_PSS_BIT_PCIE},
	{"LPSS",		PMC_PSS_BIT_LPSS},
	{"LPE",			PMC_PSS_BIT_LPE},
	{"DFX",			PMC_PSS_BIT_DFX},
	{"USH_CTRL",		PMC_PSS_BIT_USH_CTRL},
	{"USH_SUS",		PMC_PSS_BIT_USH_SUS},
	{"USH_VCCS",		PMC_PSS_BIT_USH_VCCS},
	{"USH_VCCA",		PMC_PSS_BIT_USH_VCCA},
	{"OTG_CTRL",		PMC_PSS_BIT_OTG_CTRL},
	{"OTG_VCCS",		PMC_PSS_BIT_OTG_VCCS},
	{"OTG_VCCA_CLK",	PMC_PSS_BIT_OTG_VCCA_CLK},
	{"OTG_VCCA",		PMC_PSS_BIT_OTG_VCCA},
	{"USB",			PMC_PSS_BIT_USB},
	{"USB_SUS",		PMC_PSS_BIT_USB_SUS},
	{}
};

static const struct pmc_bit_map cht_pss_map[] = {
	{"SATA",		PMC_PSS_BIT_SATA},
	{"HDA",			PMC_PSS_BIT_HDA},
	{"SEC",			PMC_PSS_BIT_SEC},
	{"PCIE",		PMC_PSS_BIT_PCIE},
	{"LPSS",		PMC_PSS_BIT_LPSS},
	{"LPE",			PMC_PSS_BIT_LPE},
	{"UFS",			PMC_PSS_BIT_CHT_UFS},
	{"UXD",			PMC_PSS_BIT_CHT_UXD},
	{"UXD_FD",		PMC_PSS_BIT_CHT_UXD_FD},
	{"UX_ENG",		PMC_PSS_BIT_CHT_UX_ENG},
	{"USB_SUS",		PMC_PSS_BIT_CHT_USB_SUS},
	{"GMM",			PMC_PSS_BIT_CHT_GMM},
	{"ISH",			PMC_PSS_BIT_CHT_ISH},
	{"DFX_MASTER",		PMC_PSS_BIT_CHT_DFX_MASTER},
	{"DFX_CLUSTER1",	PMC_PSS_BIT_CHT_DFX_CLUSTER1},
	{"DFX_CLUSTER2",	PMC_PSS_BIT_CHT_DFX_CLUSTER2},
	{"DFX_CLUSTER3",	PMC_PSS_BIT_CHT_DFX_CLUSTER3},
	{"DFX_CLUSTER4",	PMC_PSS_BIT_CHT_DFX_CLUSTER4},
	{"DFX_CLUSTER5",	PMC_PSS_BIT_CHT_DFX_CLUSTER5},
	{}
};

static const struct pmc_reg_map byt_reg_map = {
	.d3_sts_0	= d3_sts_0_map,
	.d3_sts_1	= byt_d3_sts_1_map,
	.func_dis	= d3_sts_0_map,
	.func_dis_2	= byt_d3_sts_1_map,
	.pss		= byt_pss_map,
};

static const struct pmc_reg_map cht_reg_map = {
	.d3_sts_0	= d3_sts_0_map,
	.d3_sts_1	= cht_d3_sts_1_map,
	.func_dis	= d3_sts_0_map,
	.func_dis_2	= cht_func_dis_2_map,
	.pss		= cht_pss_map,
};

static const struct pmc_data byt_data = {
	.map = &byt_reg_map,
	.clks = byt_clks,
};

static const struct pmc_data cht_data = {
	.map = &cht_reg_map,
	.clks = cht_clks,
};

static inline u32 pmc_reg_read(struct pmc_dev *pmc, int reg_offset)
{
	return readl(pmc->regmap + reg_offset);
}

static inline void pmc_reg_write(struct pmc_dev *pmc, int reg_offset, u32 val)
{
	writel(val, pmc->regmap + reg_offset);
}

int pmc_atom_read(int offset, u32 *value)
{
	struct pmc_dev *pmc = &pmc_device;

	if (!pmc->init)
		return -ENODEV;

	*value = pmc_reg_read(pmc, offset);
	return 0;
}

static void pmc_power_off(void)
{
	u16	pm1_cnt_port;
	u32	pm1_cnt_value;

	pr_info("Preparing to enter system sleep state S5\n");

	pm1_cnt_port = acpi_base_addr + PM1_CNT;

	pm1_cnt_value = inl(pm1_cnt_port);
	pm1_cnt_value &= ~SLEEP_TYPE_MASK;
	pm1_cnt_value |= SLEEP_TYPE_S5;
	pm1_cnt_value |= SLEEP_ENABLE;

	outl(pm1_cnt_value, pm1_cnt_port);
}

static void pmc_hw_reg_setup(struct pmc_dev *pmc)
{
	/*
	 * Disable PMC S0IX_WAKE_EN events coming from:
	 * - LPC clock run
	 * - GPIO_SUS ored dedicated IRQs
	 * - GPIO_SCORE ored dedicated IRQs
	 * - GPIO_SUS shared IRQ
	 * - GPIO_SCORE shared IRQ
	 */
	pmc_reg_write(pmc, PMC_S0IX_WAKE_EN, (u32)PMC_WAKE_EN_SETTING);
}

#ifdef CONFIG_DEBUG_FS
static void pmc_dev_state_print(struct seq_file *s, int reg_index,
				u32 sts, const struct pmc_bit_map *sts_map,
				u32 fd, const struct pmc_bit_map *fd_map)
{
	int offset = PMC_REG_BIT_WIDTH * reg_index;
	int index;

	for (index = 0; sts_map[index].name; index++) {
		seq_printf(s, "Dev: %-2d - %-32s\tState: %s [%s]\n",
			offset + index, sts_map[index].name,
			fd_map[index].bit_mask & fd ?  "Disabled" : "Enabled ",
			sts_map[index].bit_mask & sts ?  "D3" : "D0");
	}
}

static int pmc_dev_state_show(struct seq_file *s, void *unused)
{
	struct pmc_dev *pmc = s->private;
	const struct pmc_reg_map *m = pmc->map;
	u32 func_dis, func_dis_2;
	u32 d3_sts_0, d3_sts_1;

	func_dis = pmc_reg_read(pmc, PMC_FUNC_DIS);
	func_dis_2 = pmc_reg_read(pmc, PMC_FUNC_DIS_2);
	d3_sts_0 = pmc_reg_read(pmc, PMC_D3_STS_0);
	d3_sts_1 = pmc_reg_read(pmc, PMC_D3_STS_1);

	/* Low part */
	pmc_dev_state_print(s, 0, d3_sts_0, m->d3_sts_0, func_dis, m->func_dis);

	/* High part */
	pmc_dev_state_print(s, 1, d3_sts_1, m->d3_sts_1, func_dis_2, m->func_dis_2);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(pmc_dev_state);

static int pmc_pss_state_show(struct seq_file *s, void *unused)
{
	struct pmc_dev *pmc = s->private;
	const struct pmc_bit_map *map = pmc->map->pss;
	u32 pss = pmc_reg_read(pmc, PMC_PSS);
	int index;

	for (index = 0; map[index].name; index++) {
		seq_printf(s, "Island: %-2d - %-32s\tState: %s\n",
			index, map[index].name,
			map[index].bit_mask & pss ? "Off" : "On");
	}
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(pmc_pss_state);

static int pmc_sleep_tmr_show(struct seq_file *s, void *unused)
{
	struct pmc_dev *pmc = s->private;
	u64 s0ir_tmr, s0i1_tmr, s0i2_tmr, s0i3_tmr, s0_tmr;

	s0ir_tmr = (u64)pmc_reg_read(pmc, PMC_S0IR_TMR) << PMC_TMR_SHIFT;
	s0i1_tmr = (u64)pmc_reg_read(pmc, PMC_S0I1_TMR) << PMC_TMR_SHIFT;
	s0i2_tmr = (u64)pmc_reg_read(pmc, PMC_S0I2_TMR) << PMC_TMR_SHIFT;
	s0i3_tmr = (u64)pmc_reg_read(pmc, PMC_S0I3_TMR) << PMC_TMR_SHIFT;
	s0_tmr = (u64)pmc_reg_read(pmc, PMC_S0_TMR) << PMC_TMR_SHIFT;

	seq_printf(s, "S0IR Residency:\t%lldus\n", s0ir_tmr);
	seq_printf(s, "S0I1 Residency:\t%lldus\n", s0i1_tmr);
	seq_printf(s, "S0I2 Residency:\t%lldus\n", s0i2_tmr);
	seq_printf(s, "S0I3 Residency:\t%lldus\n", s0i3_tmr);
	seq_printf(s, "S0   Residency:\t%lldus\n", s0_tmr);
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(pmc_sleep_tmr);

static void pmc_dbgfs_register(struct pmc_dev *pmc)
{
	struct dentry *dir;

	dir = debugfs_create_dir("pmc_atom", NULL);

	pmc->dbgfs_dir = dir;

	debugfs_create_file("dev_state", S_IFREG | S_IRUGO, dir, pmc,
			    &pmc_dev_state_fops);
	debugfs_create_file("pss_state", S_IFREG | S_IRUGO, dir, pmc,
			    &pmc_pss_state_fops);
	debugfs_create_file("sleep_state", S_IFREG | S_IRUGO, dir, pmc,
			    &pmc_sleep_tmr_fops);
}
#else
static void pmc_dbgfs_register(struct pmc_dev *pmc)
{
}
#endif /* CONFIG_DEBUG_FS */

static bool pmc_clk_is_critical = true;

static int dmi_callback(const struct dmi_system_id *d)
{
	pr_info("%s: PMC critical clocks quirk enabled\n", d->ident);

	return 1;
}

static int dmi_callback_siemens(const struct dmi_system_id *d)
{
	u32 st_id;

	if (dmi_walk(simatic_ipc_find_dmi_entry_helper, &st_id))
		goto out;

	if (st_id == SIMATIC_IPC_IPC227E || st_id == SIMATIC_IPC_IPC277E)
		return dmi_callback(d);

out:
	pmc_clk_is_critical = false;
	return 1;
}

/*
 * Some systems need one or more of their pmc_plt_clks to be
 * marked as critical.
 */
static const struct dmi_system_id critclk_systems[] = {
	{
		/* pmc_plt_clk0 is used for an external HSIC USB HUB */
		.ident = "MPL CEC1x",
		.callback = dmi_callback,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "MPL AG"),
			DMI_MATCH(DMI_PRODUCT_NAME, "CEC10 Family"),
		},
	},
	{
		/*
		 * Lex System / Lex Computech Co. makes a lot of Bay Trail
		 * based embedded boards which often come with multiple
		 * ethernet controllers using multiple pmc_plt_clks. See:
		 * https://www.lex.com.tw/products/embedded-ipc-board/
		 */
		.ident = "Lex BayTrail",
		.callback = dmi_callback,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Lex BayTrail"),
		},
	},
	{
		/* pmc_plt_clk* - are used for ethernet controllers */
		.ident = "Beckhoff Baytrail",
		.callback = dmi_callback,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Beckhoff Automation"),
			DMI_MATCH(DMI_PRODUCT_FAMILY, "CBxx63"),
		},
	},
	{
		.ident = "SIEMENS AG",
		.callback = dmi_callback_siemens,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "SIEMENS AG"),
		},
	},
	{}
};

static int pmc_setup_clks(struct pci_dev *pdev, void __iomem *pmc_regmap,
			  const struct pmc_data *pmc_data)
{
	struct platform_device *clkdev;
	struct pmc_clk_data *clk_data;

	clk_data = kzalloc(sizeof(*clk_data), GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;

	clk_data->base = pmc_regmap; /* offset is added by client */
	clk_data->clks = pmc_data->clks;
	if (dmi_check_system(critclk_systems))
		clk_data->critical = pmc_clk_is_critical;

	clkdev = platform_device_register_data(&pdev->dev, "clk-pmc-atom",
					       PLATFORM_DEVID_NONE,
					       clk_data, sizeof(*clk_data));
	if (IS_ERR(clkdev)) {
		kfree(clk_data);
		return PTR_ERR(clkdev);
	}

	kfree(clk_data);

	return 0;
}

static int pmc_setup_dev(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct pmc_dev *pmc = &pmc_device;
	const struct pmc_data *data = (struct pmc_data *)ent->driver_data;
	const struct pmc_reg_map *map = data->map;
	int ret;

	/* Obtain ACPI base address */
	pci_read_config_dword(pdev, ACPI_BASE_ADDR_OFFSET, &acpi_base_addr);
	acpi_base_addr &= ACPI_BASE_ADDR_MASK;

	/* Install power off function */
	if (acpi_base_addr != 0 && pm_power_off == NULL)
		pm_power_off = pmc_power_off;

	pci_read_config_dword(pdev, PMC_BASE_ADDR_OFFSET, &pmc->base_addr);
	pmc->base_addr &= PMC_BASE_ADDR_MASK;

	pmc->regmap = ioremap(pmc->base_addr, PMC_MMIO_REG_LEN);
	if (!pmc->regmap) {
		dev_err(&pdev->dev, "error: ioremap failed\n");
		return -ENOMEM;
	}

	pmc->map = map;

	/* PMC hardware registers setup */
	pmc_hw_reg_setup(pmc);

	pmc_dbgfs_register(pmc);

	/* Register platform clocks - PMC_PLT_CLK [0..5] */
	ret = pmc_setup_clks(pdev, pmc->regmap, data);
	if (ret)
		dev_warn(&pdev->dev, "platform clocks register failed: %d\n",
			 ret);

	pmc->init = true;
	return ret;
}

/* Data for PCI driver interface used by pci_match_id() call below */
static const struct pci_device_id pmc_pci_ids[] = {
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_VLV_PMC), (kernel_ulong_t)&byt_data },
	{ PCI_VDEVICE(INTEL, PCI_DEVICE_ID_CHT_PMC), (kernel_ulong_t)&cht_data },
	{}
};

static int __init pmc_atom_init(void)
{
	struct pci_dev *pdev = NULL;
	const struct pci_device_id *ent;

	/*
	 * We look for our device - PCU PMC.
	 * We assume that there is maximum one device.
	 *
	 * We can't use plain pci_driver mechanism,
	 * as the device is really a multiple function device,
	 * main driver that binds to the pci_device is lpc_ich
	 * and have to find & bind to the device this way.
	 */
	for_each_pci_dev(pdev) {
		ent = pci_match_id(pmc_pci_ids, pdev);
		if (ent)
			return pmc_setup_dev(pdev, ent);
	}
	/* Device not found */
	return -ENODEV;
}

device_initcall(pmc_atom_init);

/*
MODULE_AUTHOR("Aubrey Li <aubrey.li@linux.intel.com>");
MODULE_DESCRIPTION("Intel Atom SoC Power Management Controller Interface");
MODULE_LICENSE("GPL v2");
*/
