/*
 * Intel Atom SOC Power Management Controller Driver
 * Copyright (c) 2014, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/device.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/io.h>

#include <asm/pmc_atom.h>

#define	DRIVER_NAME	KBUILD_MODNAME

struct pmc_dev {
	u32 base_addr;
	void __iomem *regmap;
#ifdef CONFIG_DEBUG_FS
	struct dentry *dbgfs_dir;
#endif /* CONFIG_DEBUG_FS */
};

static struct pmc_dev pmc_device;
static u32 acpi_base_addr;

struct pmc_dev_map {
	const char *name;
	u32 bit_mask;
};

static const struct pmc_dev_map dev_map[] = {
	{"0  - LPSS1_F0_DMA",		BIT_LPSS1_F0_DMA},
	{"1  - LPSS1_F1_PWM1",		BIT_LPSS1_F1_PWM1},
	{"2  - LPSS1_F2_PWM2",		BIT_LPSS1_F2_PWM2},
	{"3  - LPSS1_F3_HSUART1",	BIT_LPSS1_F3_HSUART1},
	{"4  - LPSS1_F4_HSUART2",	BIT_LPSS1_F4_HSUART2},
	{"5  - LPSS1_F5_SPI",		BIT_LPSS1_F5_SPI},
	{"6  - LPSS1_F6_Reserved",	BIT_LPSS1_F6_XXX},
	{"7  - LPSS1_F7_Reserved",	BIT_LPSS1_F7_XXX},
	{"8  - SCC_EMMC",		BIT_SCC_EMMC},
	{"9  - SCC_SDIO",		BIT_SCC_SDIO},
	{"10 - SCC_SDCARD",		BIT_SCC_SDCARD},
	{"11 - SCC_MIPI",		BIT_SCC_MIPI},
	{"12 - HDA",			BIT_HDA},
	{"13 - LPE",			BIT_LPE},
	{"14 - OTG",			BIT_OTG},
	{"15 - USH",			BIT_USH},
	{"16 - GBE",			BIT_GBE},
	{"17 - SATA",			BIT_SATA},
	{"18 - USB_EHCI",		BIT_USB_EHCI},
	{"19 - SEC",			BIT_SEC},
	{"20 - PCIE_PORT0",		BIT_PCIE_PORT0},
	{"21 - PCIE_PORT1",		BIT_PCIE_PORT1},
	{"22 - PCIE_PORT2",		BIT_PCIE_PORT2},
	{"23 - PCIE_PORT3",		BIT_PCIE_PORT3},
	{"24 - LPSS2_F0_DMA",		BIT_LPSS2_F0_DMA},
	{"25 - LPSS2_F1_I2C1",		BIT_LPSS2_F1_I2C1},
	{"26 - LPSS2_F2_I2C2",		BIT_LPSS2_F2_I2C2},
	{"27 - LPSS2_F3_I2C3",		BIT_LPSS2_F3_I2C3},
	{"28 - LPSS2_F3_I2C4",		BIT_LPSS2_F4_I2C4},
	{"29 - LPSS2_F5_I2C5",		BIT_LPSS2_F5_I2C5},
	{"30 - LPSS2_F6_I2C6",		BIT_LPSS2_F6_I2C6},
	{"31 - LPSS2_F7_I2C7",		BIT_LPSS2_F7_I2C7},
	{"32 - SMB",			BIT_SMB},
	{"33 - OTG_SS_PHY",		BIT_OTG_SS_PHY},
	{"34 - USH_SS_PHY",		BIT_USH_SS_PHY},
	{"35 - DFX",			BIT_DFX},
};

static inline u32 pmc_reg_read(struct pmc_dev *pmc, int reg_offset)
{
	return readl(pmc->regmap + reg_offset);
}

static inline void pmc_reg_write(struct pmc_dev *pmc, int reg_offset, u32 val)
{
	writel(val, pmc->regmap + reg_offset);
}

static void pmc_power_off(void)
{
	u16	pm1_cnt_port;
	u32	pm1_cnt_value;

	pr_info("Preparing to enter system sleep state S5\n");

	pm1_cnt_port = acpi_base_addr + PM1_CNT;

	pm1_cnt_value = inl(pm1_cnt_port);
	pm1_cnt_value &= SLEEP_TYPE_MASK;
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
static int pmc_dev_state_show(struct seq_file *s, void *unused)
{
	struct pmc_dev *pmc = s->private;
	u32 func_dis, func_dis_2, func_dis_index;
	u32 d3_sts_0, d3_sts_1, d3_sts_index;
	int dev_num, dev_index, reg_index;

	func_dis = pmc_reg_read(pmc, PMC_FUNC_DIS);
	func_dis_2 = pmc_reg_read(pmc, PMC_FUNC_DIS_2);
	d3_sts_0 = pmc_reg_read(pmc, PMC_D3_STS_0);
	d3_sts_1 = pmc_reg_read(pmc, PMC_D3_STS_1);

	dev_num = ARRAY_SIZE(dev_map);

	for (dev_index = 0; dev_index < dev_num; dev_index++) {
		reg_index = dev_index / PMC_REG_BIT_WIDTH;
		if (reg_index) {
			func_dis_index = func_dis_2;
			d3_sts_index = d3_sts_1;
		} else {
			func_dis_index = func_dis;
			d3_sts_index = d3_sts_0;
		}

		seq_printf(s, "Dev: %-32s\tState: %s [%s]\n",
			dev_map[dev_index].name,
			dev_map[dev_index].bit_mask & func_dis_index ?
			"Disabled" : "Enabled ",
			dev_map[dev_index].bit_mask & d3_sts_index ?
			"D3" : "D0");
	}
	return 0;
}

static int pmc_dev_state_open(struct inode *inode, struct file *file)
{
	return single_open(file, pmc_dev_state_show, inode->i_private);
}

static const struct file_operations pmc_dev_state_ops = {
	.open		= pmc_dev_state_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

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

static int pmc_sleep_tmr_open(struct inode *inode, struct file *file)
{
	return single_open(file, pmc_sleep_tmr_show, inode->i_private);
}

static const struct file_operations pmc_sleep_tmr_ops = {
	.open		= pmc_sleep_tmr_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static void pmc_dbgfs_unregister(struct pmc_dev *pmc)
{
	if (!pmc->dbgfs_dir)
		return;

	debugfs_remove_recursive(pmc->dbgfs_dir);
	pmc->dbgfs_dir = NULL;
}

static int pmc_dbgfs_register(struct pmc_dev *pmc, struct pci_dev *pdev)
{
	struct dentry *dir, *f;

	dir = debugfs_create_dir("pmc_atom", NULL);
	if (!dir)
		return -ENOMEM;

	f = debugfs_create_file("dev_state", S_IFREG | S_IRUGO,
				dir, pmc, &pmc_dev_state_ops);
	if (!f) {
		dev_err(&pdev->dev, "dev_states register failed\n");
		goto err;
	}
	f = debugfs_create_file("sleep_state", S_IFREG | S_IRUGO,
				dir, pmc, &pmc_sleep_tmr_ops);
	if (!f) {
		dev_err(&pdev->dev, "sleep_state register failed\n");
		goto err;
	}
	pmc->dbgfs_dir = dir;
	return 0;
err:
	pmc_dbgfs_unregister(pmc);
	return -ENODEV;
}
#else
static int pmc_dbgfs_register(struct pmc_dev *pmc, struct pci_dev *pdev)
{
	return 0;
}
#endif /* CONFIG_DEBUG_FS */

static int pmc_setup_dev(struct pci_dev *pdev)
{
	struct pmc_dev *pmc = &pmc_device;
	int ret;

	/* Obtain ACPI base address */
	pci_read_config_dword(pdev, ACPI_BASE_ADDR_OFFSET, &acpi_base_addr);
	acpi_base_addr &= ACPI_BASE_ADDR_MASK;

	/* Install power off function */
	if (acpi_base_addr != 0 && pm_power_off == NULL)
		pm_power_off = pmc_power_off;

	pci_read_config_dword(pdev, PMC_BASE_ADDR_OFFSET, &pmc->base_addr);
	pmc->base_addr &= PMC_BASE_ADDR_MASK;

	pmc->regmap = ioremap_nocache(pmc->base_addr, PMC_MMIO_REG_LEN);
	if (!pmc->regmap) {
		dev_err(&pdev->dev, "error: ioremap failed\n");
		return -ENOMEM;
	}

	/* PMC hardware registers setup */
	pmc_hw_reg_setup(pmc);

	ret = pmc_dbgfs_register(pmc, pdev);
	if (ret) {
		iounmap(pmc->regmap);
	}

	return ret;
}

/*
 * Data for PCI driver interface
 *
 * This data only exists for exporting the supported
 * PCI ids via MODULE_DEVICE_TABLE.  We do not actually
 * register a pci_driver, because lpc_ich will register
 * a driver on the same PCI id.
 */
static const struct pci_device_id pmc_pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_VLV_PMC) },
	{ 0, },
};

MODULE_DEVICE_TABLE(pci, pmc_pci_ids);

static int __init pmc_atom_init(void)
{
	int err = -ENODEV;
	struct pci_dev *pdev = NULL;
	const struct pci_device_id *ent;

	/* We look for our device - PCU PMC
	 * we assume that there is max. one device.
	 *
	 * We can't use plain pci_driver mechanism,
	 * as the device is really a multiple function device,
	 * main driver that binds to the pci_device is lpc_ich
	 * and have to find & bind to the device this way.
	 */
	for_each_pci_dev(pdev) {
		ent = pci_match_id(pmc_pci_ids, pdev);
		if (ent) {
			err = pmc_setup_dev(pdev);
			goto out;
		}
	}
	/* Device not found. */
out:
	return err;
}

module_init(pmc_atom_init);
/* no module_exit, this driver shouldn't be unloaded */

MODULE_AUTHOR("Aubrey Li <aubrey.li@linux.intel.com>");
MODULE_DESCRIPTION("Intel Atom SOC Power Management Controller Interface");
MODULE_LICENSE("GPL v2");
