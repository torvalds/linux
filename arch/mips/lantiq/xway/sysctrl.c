/*
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 *
 *  Copyright (C) 2011-2012 John Crispin <blogic@openwrt.org>
 */

#include <linux/ioport.h>
#include <linux/export.h>
#include <linux/clkdev.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>

#include <lantiq_soc.h>

#include "../clk.h"
#include "../prom.h"

/* clock control register */
#define CGU_IFCCR	0x0018
#define CGU_IFCCR_VR9	0x0024
/* system clock register */
#define CGU_SYS		0x0010
/* pci control register */
#define CGU_PCICR	0x0034
#define CGU_PCICR_VR9	0x0038
/* ephy configuration register */
#define CGU_EPHY	0x10
/* power control register */
#define PMU_PWDCR	0x1C
/* power status register */
#define PMU_PWDSR	0x20
/* power control register */
#define PMU_PWDCR1	0x24
/* power status register */
#define PMU_PWDSR1	0x28
/* power control register */
#define PWDCR(x) ((x) ? (PMU_PWDCR1) : (PMU_PWDCR))
/* power status register */
#define PWDSR(x) ((x) ? (PMU_PWDSR1) : (PMU_PWDSR))

/* clock gates that we can en/disable */
#define PMU_USB0_P	BIT(0)
#define PMU_PCI		BIT(4)
#define PMU_DMA		BIT(5)
#define PMU_USB0	BIT(6)
#define PMU_ASC0	BIT(7)
#define PMU_EPHY	BIT(7)	/* ase */
#define PMU_SPI		BIT(8)
#define PMU_DFE		BIT(9)
#define PMU_EBU		BIT(10)
#define PMU_STP		BIT(11)
#define PMU_GPT		BIT(12)
#define PMU_AHBS	BIT(13) /* vr9 */
#define PMU_FPI		BIT(14)
#define PMU_AHBM	BIT(15)
#define PMU_ASC1	BIT(17)
#define PMU_PPE_QSB	BIT(18)
#define PMU_PPE_SLL01	BIT(19)
#define PMU_PPE_TC	BIT(21)
#define PMU_PPE_EMA	BIT(22)
#define PMU_PPE_DPLUM	BIT(23)
#define PMU_PPE_DPLUS	BIT(24)
#define PMU_USB1_P	BIT(26)
#define PMU_USB1	BIT(27)
#define PMU_SWITCH	BIT(28)
#define PMU_PPE_TOP	BIT(29)
#define PMU_GPHY	BIT(30)
#define PMU_PCIE_CLK	BIT(31)

#define PMU1_PCIE_PHY	BIT(0)
#define PMU1_PCIE_CTL	BIT(1)
#define PMU1_PCIE_PDI	BIT(4)
#define PMU1_PCIE_MSI	BIT(5)

#define pmu_w32(x, y)	ltq_w32((x), pmu_membase + (y))
#define pmu_r32(x)	ltq_r32(pmu_membase + (x))

static void __iomem *pmu_membase;
void __iomem *ltq_cgu_membase;
void __iomem *ltq_ebu_membase;

static u32 ifccr = CGU_IFCCR;
static u32 pcicr = CGU_PCICR;

/* legacy function kept alive to ease clkdev transition */
void ltq_pmu_enable(unsigned int module)
{
	int err = 1000000;

	pmu_w32(pmu_r32(PMU_PWDCR) & ~module, PMU_PWDCR);
	do {} while (--err && (pmu_r32(PMU_PWDSR) & module));

	if (!err)
		panic("activating PMU module failed!");
}
EXPORT_SYMBOL(ltq_pmu_enable);

/* legacy function kept alive to ease clkdev transition */
void ltq_pmu_disable(unsigned int module)
{
	pmu_w32(pmu_r32(PMU_PWDCR) | module, PMU_PWDCR);
}
EXPORT_SYMBOL(ltq_pmu_disable);

/* enable a hw clock */
static int cgu_enable(struct clk *clk)
{
	ltq_cgu_w32(ltq_cgu_r32(ifccr) | clk->bits, ifccr);
	return 0;
}

/* disable a hw clock */
static void cgu_disable(struct clk *clk)
{
	ltq_cgu_w32(ltq_cgu_r32(ifccr) & ~clk->bits, ifccr);
}

/* enable a clock gate */
static int pmu_enable(struct clk *clk)
{
	int retry = 1000000;

	pmu_w32(pmu_r32(PWDCR(clk->module)) & ~clk->bits,
		PWDCR(clk->module));
	do {} while (--retry && (pmu_r32(PWDSR(clk->module)) & clk->bits));

	if (!retry)
		panic("activating PMU module failed!");

	return 0;
}

/* disable a clock gate */
static void pmu_disable(struct clk *clk)
{
	pmu_w32(pmu_r32(PWDCR(clk->module)) | clk->bits,
		PWDCR(clk->module));
}

/* the pci enable helper */
static int pci_enable(struct clk *clk)
{
	unsigned int val = ltq_cgu_r32(ifccr);
	/* set bus clock speed */
	if (of_machine_is_compatible("lantiq,ar9") ||
			of_machine_is_compatible("lantiq,vr9")) {
		val &= ~0x1f00000;
		if (clk->rate == CLOCK_33M)
			val |= 0xe00000;
		else
			val |= 0x700000; /* 62.5M */
	} else {
		val &= ~0xf00000;
		if (clk->rate == CLOCK_33M)
			val |= 0x800000;
		else
			val |= 0x400000; /* 62.5M */
	}
	ltq_cgu_w32(val, ifccr);
	pmu_enable(clk);
	return 0;
}

/* enable the external clock as a source */
static int pci_ext_enable(struct clk *clk)
{
	ltq_cgu_w32(ltq_cgu_r32(ifccr) & ~(1 << 16), ifccr);
	ltq_cgu_w32((1 << 30), pcicr);
	return 0;
}

/* disable the external clock as a source */
static void pci_ext_disable(struct clk *clk)
{
	ltq_cgu_w32(ltq_cgu_r32(ifccr) | (1 << 16), ifccr);
	ltq_cgu_w32((1 << 31) | (1 << 30), pcicr);
}

/* enable a clockout source */
static int clkout_enable(struct clk *clk)
{
	int i;

	/* get the correct rate */
	for (i = 0; i < 4; i++) {
		if (clk->rates[i] == clk->rate) {
			int shift = 14 - (2 * clk->module);
			int enable = 7 - clk->module;
			unsigned int val = ltq_cgu_r32(ifccr);

			val &= ~(3 << shift);
			val |= i << shift;
			val |= enable;
			ltq_cgu_w32(val, ifccr);
			return 0;
		}
	}
	return -1;
}

/* manage the clock gates via PMU */
static void clkdev_add_pmu(const char *dev, const char *con,
					unsigned int module, unsigned int bits)
{
	struct clk *clk = kzalloc(sizeof(struct clk), GFP_KERNEL);

	clk->cl.dev_id = dev;
	clk->cl.con_id = con;
	clk->cl.clk = clk;
	clk->enable = pmu_enable;
	clk->disable = pmu_disable;
	clk->module = module;
	clk->bits = bits;
	clkdev_add(&clk->cl);
}

/* manage the clock generator */
static void clkdev_add_cgu(const char *dev, const char *con,
					unsigned int bits)
{
	struct clk *clk = kzalloc(sizeof(struct clk), GFP_KERNEL);

	clk->cl.dev_id = dev;
	clk->cl.con_id = con;
	clk->cl.clk = clk;
	clk->enable = cgu_enable;
	clk->disable = cgu_disable;
	clk->bits = bits;
	clkdev_add(&clk->cl);
}

/* pci needs its own enable function as the setup is a bit more complex */
static unsigned long valid_pci_rates[] = {CLOCK_33M, CLOCK_62_5M, 0};

static void clkdev_add_pci(void)
{
	struct clk *clk = kzalloc(sizeof(struct clk), GFP_KERNEL);
	struct clk *clk_ext = kzalloc(sizeof(struct clk), GFP_KERNEL);

	/* main pci clock */
	clk->cl.dev_id = "17000000.pci";
	clk->cl.con_id = NULL;
	clk->cl.clk = clk;
	clk->rate = CLOCK_33M;
	clk->rates = valid_pci_rates;
	clk->enable = pci_enable;
	clk->disable = pmu_disable;
	clk->module = 0;
	clk->bits = PMU_PCI;
	clkdev_add(&clk->cl);

	/* use internal/external bus clock */
	clk_ext->cl.dev_id = "17000000.pci";
	clk_ext->cl.con_id = "external";
	clk_ext->cl.clk = clk_ext;
	clk_ext->enable = pci_ext_enable;
	clk_ext->disable = pci_ext_disable;
	clkdev_add(&clk_ext->cl);
}

/* xway socs can generate clocks on gpio pins */
static unsigned long valid_clkout_rates[4][5] = {
	{CLOCK_32_768K, CLOCK_1_536M, CLOCK_2_5M, CLOCK_12M, 0},
	{CLOCK_40M, CLOCK_12M, CLOCK_24M, CLOCK_48M, 0},
	{CLOCK_25M, CLOCK_40M, CLOCK_30M, CLOCK_60M, 0},
	{CLOCK_12M, CLOCK_50M, CLOCK_32_768K, CLOCK_25M, 0},
};

static void clkdev_add_clkout(void)
{
	int i;

	for (i = 0; i < 4; i++) {
		struct clk *clk;
		char *name;

		name = kzalloc(sizeof("clkout0"), GFP_KERNEL);
		sprintf(name, "clkout%d", i);

		clk = kzalloc(sizeof(struct clk), GFP_KERNEL);
		clk->cl.dev_id = "1f103000.cgu";
		clk->cl.con_id = name;
		clk->cl.clk = clk;
		clk->rate = 0;
		clk->rates = valid_clkout_rates[i];
		clk->enable = clkout_enable;
		clk->module = i;
		clkdev_add(&clk->cl);
	}
}

/* bring up all register ranges that we need for basic system control */
void __init ltq_soc_init(void)
{
	struct resource res_pmu, res_cgu, res_ebu;
	struct device_node *np_pmu =
			of_find_compatible_node(NULL, NULL, "lantiq,pmu-xway");
	struct device_node *np_cgu =
			of_find_compatible_node(NULL, NULL, "lantiq,cgu-xway");
	struct device_node *np_ebu =
			of_find_compatible_node(NULL, NULL, "lantiq,ebu-xway");

	/* check if all the core register ranges are available */
	if (!np_pmu || !np_cgu || !np_ebu)
		panic("Failed to load core nodes from devicetree");

	if (of_address_to_resource(np_pmu, 0, &res_pmu) ||
			of_address_to_resource(np_cgu, 0, &res_cgu) ||
			of_address_to_resource(np_ebu, 0, &res_ebu))
		panic("Failed to get core resources");

	if ((request_mem_region(res_pmu.start, resource_size(&res_pmu),
				res_pmu.name) < 0) ||
		(request_mem_region(res_cgu.start, resource_size(&res_cgu),
				res_cgu.name) < 0) ||
		(request_mem_region(res_ebu.start, resource_size(&res_ebu),
				res_ebu.name) < 0))
		pr_err("Failed to request core reources");

	pmu_membase = ioremap_nocache(res_pmu.start, resource_size(&res_pmu));
	ltq_cgu_membase = ioremap_nocache(res_cgu.start,
						resource_size(&res_cgu));
	ltq_ebu_membase = ioremap_nocache(res_ebu.start,
						resource_size(&res_ebu));
	if (!pmu_membase || !ltq_cgu_membase || !ltq_ebu_membase)
		panic("Failed to remap core resources");

	/* make sure to unprotect the memory region where flash is located */
	ltq_ebu_w32(ltq_ebu_r32(LTQ_EBU_BUSCON0) & ~EBU_WRDIS, LTQ_EBU_BUSCON0);

	/* add our generic xway clocks */
	clkdev_add_pmu("10000000.fpi", NULL, 0, PMU_FPI);
	clkdev_add_pmu("1e100400.serial", NULL, 0, PMU_ASC0);
	clkdev_add_pmu("1e100a00.gptu", NULL, 0, PMU_GPT);
	clkdev_add_pmu("1e100bb0.stp", NULL, 0, PMU_STP);
	clkdev_add_pmu("1e104100.dma", NULL, 0, PMU_DMA);
	clkdev_add_pmu("1e100800.spi", NULL, 0, PMU_SPI);
	clkdev_add_pmu("1e105300.ebu", NULL, 0, PMU_EBU);
	clkdev_add_clkout();

	/* add the soc dependent clocks */
	if (of_machine_is_compatible("lantiq,vr9")) {
		ifccr = CGU_IFCCR_VR9;
		pcicr = CGU_PCICR_VR9;
	} else {
		clkdev_add_pmu("1e180000.etop", NULL, 0, PMU_PPE);
	}

	if (!of_machine_is_compatible("lantiq,ase")) {
		clkdev_add_pmu("1e100c00.serial", NULL, 0, PMU_ASC1);
		clkdev_add_pci();
	}

	if (of_machine_is_compatible("lantiq,ase")) {
		if (ltq_cgu_r32(CGU_SYS) & (1 << 5))
			clkdev_add_static(CLOCK_266M, CLOCK_133M,
						CLOCK_133M, CLOCK_266M);
		else
			clkdev_add_static(CLOCK_133M, CLOCK_133M,
						CLOCK_133M, CLOCK_133M);
		clkdev_add_cgu("1e180000.etop", "ephycgu", CGU_EPHY),
		clkdev_add_pmu("1e180000.etop", "ephy", 0, PMU_EPHY);
	} else if (of_machine_is_compatible("lantiq,vr9")) {
		clkdev_add_static(ltq_vr9_cpu_hz(), ltq_vr9_fpi_hz(),
				ltq_vr9_fpi_hz(), ltq_vr9_pp32_hz());
		clkdev_add_pmu("1d900000.pcie", "phy", 1, PMU1_PCIE_PHY);
		clkdev_add_pmu("1d900000.pcie", "bus", 0, PMU_PCIE_CLK);
		clkdev_add_pmu("1d900000.pcie", "msi", 1, PMU1_PCIE_MSI);
		clkdev_add_pmu("1d900000.pcie", "pdi", 1, PMU1_PCIE_PDI);
		clkdev_add_pmu("1d900000.pcie", "ctl", 1, PMU1_PCIE_CTL);
		clkdev_add_pmu("1d900000.pcie", "ahb", 0, PMU_AHBM | PMU_AHBS);
		clkdev_add_pmu("1e108000.eth", NULL, 0,
				PMU_SWITCH | PMU_PPE_DPLUS | PMU_PPE_DPLUM |
				PMU_PPE_EMA | PMU_PPE_TC | PMU_PPE_SLL01 |
				PMU_PPE_QSB | PMU_PPE_TOP);
		clkdev_add_pmu("1f203000.rcu", "gphy", 0, PMU_GPHY);
	} else if (of_machine_is_compatible("lantiq,ar9")) {
		clkdev_add_static(ltq_ar9_cpu_hz(), ltq_ar9_fpi_hz(),
				ltq_ar9_fpi_hz(), CLOCK_250M);
		clkdev_add_pmu("1e180000.etop", "switch", 0, PMU_SWITCH);
	} else {
		clkdev_add_static(ltq_danube_cpu_hz(), ltq_danube_fpi_hz(),
				ltq_danube_fpi_hz(), ltq_danube_pp32_hz());
	}
}
