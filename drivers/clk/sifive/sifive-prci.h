/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2018-2019 SiFive, Inc.
 * Wesley Terpstra
 * Paul Walmsley
 * Zong Li
 */

#ifndef __SIFIVE_CLK_SIFIVE_PRCI_H
#define __SIFIVE_CLK_SIFIVE_PRCI_H

#include <linux/clk/analogbits-wrpll-cln28hpc.h>
#include <linux/clk-provider.h>
#include <linux/platform_device.h>

/*
 * EXPECTED_CLK_PARENT_COUNT: how many parent clocks this driver expects:
 *     hfclk and rtcclk
 */
#define EXPECTED_CLK_PARENT_COUNT 2

/*
 * Register offsets and bitmasks
 */

/* COREPLLCFG0 */
#define PRCI_COREPLLCFG0_OFFSET		0x4
#define PRCI_COREPLLCFG0_DIVR_SHIFT	0
#define PRCI_COREPLLCFG0_DIVR_MASK	(0x3f << PRCI_COREPLLCFG0_DIVR_SHIFT)
#define PRCI_COREPLLCFG0_DIVF_SHIFT	6
#define PRCI_COREPLLCFG0_DIVF_MASK	(0x1ff << PRCI_COREPLLCFG0_DIVF_SHIFT)
#define PRCI_COREPLLCFG0_DIVQ_SHIFT	15
#define PRCI_COREPLLCFG0_DIVQ_MASK	(0x7 << PRCI_COREPLLCFG0_DIVQ_SHIFT)
#define PRCI_COREPLLCFG0_RANGE_SHIFT	18
#define PRCI_COREPLLCFG0_RANGE_MASK	(0x7 << PRCI_COREPLLCFG0_RANGE_SHIFT)
#define PRCI_COREPLLCFG0_BYPASS_SHIFT	24
#define PRCI_COREPLLCFG0_BYPASS_MASK	(0x1 << PRCI_COREPLLCFG0_BYPASS_SHIFT)
#define PRCI_COREPLLCFG0_FSE_SHIFT	25
#define PRCI_COREPLLCFG0_FSE_MASK	(0x1 << PRCI_COREPLLCFG0_FSE_SHIFT)
#define PRCI_COREPLLCFG0_LOCK_SHIFT	31
#define PRCI_COREPLLCFG0_LOCK_MASK	(0x1 << PRCI_COREPLLCFG0_LOCK_SHIFT)

/* DDRPLLCFG0 */
#define PRCI_DDRPLLCFG0_OFFSET		0xc
#define PRCI_DDRPLLCFG0_DIVR_SHIFT	0
#define PRCI_DDRPLLCFG0_DIVR_MASK	(0x3f << PRCI_DDRPLLCFG0_DIVR_SHIFT)
#define PRCI_DDRPLLCFG0_DIVF_SHIFT	6
#define PRCI_DDRPLLCFG0_DIVF_MASK	(0x1ff << PRCI_DDRPLLCFG0_DIVF_SHIFT)
#define PRCI_DDRPLLCFG0_DIVQ_SHIFT	15
#define PRCI_DDRPLLCFG0_DIVQ_MASK	(0x7 << PRCI_DDRPLLCFG0_DIVQ_SHIFT)
#define PRCI_DDRPLLCFG0_RANGE_SHIFT	18
#define PRCI_DDRPLLCFG0_RANGE_MASK	(0x7 << PRCI_DDRPLLCFG0_RANGE_SHIFT)
#define PRCI_DDRPLLCFG0_BYPASS_SHIFT	24
#define PRCI_DDRPLLCFG0_BYPASS_MASK	(0x1 << PRCI_DDRPLLCFG0_BYPASS_SHIFT)
#define PRCI_DDRPLLCFG0_FSE_SHIFT	25
#define PRCI_DDRPLLCFG0_FSE_MASK	(0x1 << PRCI_DDRPLLCFG0_FSE_SHIFT)
#define PRCI_DDRPLLCFG0_LOCK_SHIFT	31
#define PRCI_DDRPLLCFG0_LOCK_MASK	(0x1 << PRCI_DDRPLLCFG0_LOCK_SHIFT)

/* DDRPLLCFG1 */
#define PRCI_DDRPLLCFG1_OFFSET		0x10
#define PRCI_DDRPLLCFG1_CKE_SHIFT	24
#define PRCI_DDRPLLCFG1_CKE_MASK	(0x1 << PRCI_DDRPLLCFG1_CKE_SHIFT)

/* GEMGXLPLLCFG0 */
#define PRCI_GEMGXLPLLCFG0_OFFSET	0x1c
#define PRCI_GEMGXLPLLCFG0_DIVR_SHIFT	0
#define PRCI_GEMGXLPLLCFG0_DIVR_MASK	(0x3f << PRCI_GEMGXLPLLCFG0_DIVR_SHIFT)
#define PRCI_GEMGXLPLLCFG0_DIVF_SHIFT	6
#define PRCI_GEMGXLPLLCFG0_DIVF_MASK	(0x1ff << PRCI_GEMGXLPLLCFG0_DIVF_SHIFT)
#define PRCI_GEMGXLPLLCFG0_DIVQ_SHIFT	15
#define PRCI_GEMGXLPLLCFG0_DIVQ_MASK	(0x7 << PRCI_GEMGXLPLLCFG0_DIVQ_SHIFT)
#define PRCI_GEMGXLPLLCFG0_RANGE_SHIFT	18
#define PRCI_GEMGXLPLLCFG0_RANGE_MASK	(0x7 << PRCI_GEMGXLPLLCFG0_RANGE_SHIFT)
#define PRCI_GEMGXLPLLCFG0_BYPASS_SHIFT	24
#define PRCI_GEMGXLPLLCFG0_BYPASS_MASK	(0x1 << PRCI_GEMGXLPLLCFG0_BYPASS_SHIFT)
#define PRCI_GEMGXLPLLCFG0_FSE_SHIFT	25
#define PRCI_GEMGXLPLLCFG0_FSE_MASK	(0x1 << PRCI_GEMGXLPLLCFG0_FSE_SHIFT)
#define PRCI_GEMGXLPLLCFG0_LOCK_SHIFT	31
#define PRCI_GEMGXLPLLCFG0_LOCK_MASK	(0x1 << PRCI_GEMGXLPLLCFG0_LOCK_SHIFT)

/* GEMGXLPLLCFG1 */
#define PRCI_GEMGXLPLLCFG1_OFFSET	0x20
#define PRCI_GEMGXLPLLCFG1_CKE_SHIFT	24
#define PRCI_GEMGXLPLLCFG1_CKE_MASK	(0x1 << PRCI_GEMGXLPLLCFG1_CKE_SHIFT)

/* CORECLKSEL */
#define PRCI_CORECLKSEL_OFFSET			0x24
#define PRCI_CORECLKSEL_CORECLKSEL_SHIFT	0
#define PRCI_CORECLKSEL_CORECLKSEL_MASK					\
		(0x1 << PRCI_CORECLKSEL_CORECLKSEL_SHIFT)

/* DEVICESRESETREG */
#define PRCI_DEVICESRESETREG_OFFSET				0x28
#define PRCI_DEVICESRESETREG_DDR_CTRL_RST_N_SHIFT		0
#define PRCI_DEVICESRESETREG_DDR_CTRL_RST_N_MASK			\
		(0x1 << PRCI_DEVICESRESETREG_DDR_CTRL_RST_N_SHIFT)
#define PRCI_DEVICESRESETREG_DDR_AXI_RST_N_SHIFT		1
#define PRCI_DEVICESRESETREG_DDR_AXI_RST_N_MASK				\
		(0x1 << PRCI_DEVICESRESETREG_DDR_AXI_RST_N_SHIFT)
#define PRCI_DEVICESRESETREG_DDR_AHB_RST_N_SHIFT		2
#define PRCI_DEVICESRESETREG_DDR_AHB_RST_N_MASK				\
		(0x1 << PRCI_DEVICESRESETREG_DDR_AHB_RST_N_SHIFT)
#define PRCI_DEVICESRESETREG_DDR_PHY_RST_N_SHIFT		3
#define PRCI_DEVICESRESETREG_DDR_PHY_RST_N_MASK				\
		(0x1 << PRCI_DEVICESRESETREG_DDR_PHY_RST_N_SHIFT)
#define PRCI_DEVICESRESETREG_GEMGXL_RST_N_SHIFT			5
#define PRCI_DEVICESRESETREG_GEMGXL_RST_N_MASK				\
		(0x1 << PRCI_DEVICESRESETREG_GEMGXL_RST_N_SHIFT)
#define PRCI_DEVICESRESETREG_CHIPLINK_RST_N_SHIFT		6
#define PRCI_DEVICESRESETREG_CHIPLINK_RST_N_MASK			\
		(0x1 << PRCI_DEVICESRESETREG_CHIPLINK_RST_N_SHIFT)

/* CLKMUXSTATUSREG */
#define PRCI_CLKMUXSTATUSREG_OFFSET				0x2c
#define PRCI_CLKMUXSTATUSREG_TLCLKSEL_STATUS_SHIFT		1
#define PRCI_CLKMUXSTATUSREG_TLCLKSEL_STATUS_MASK			\
		(0x1 << PRCI_CLKMUXSTATUSREG_TLCLKSEL_STATUS_SHIFT)

/*
 * Private structures
 */

/**
 * struct __prci_data - per-device-instance data
 * @va: base virtual address of the PRCI IP block
 * @hw_clks: encapsulates struct clk_hw records
 *
 * PRCI per-device instance data
 */
struct __prci_data {
	void __iomem *va;
	struct clk_hw_onecell_data hw_clks;
};

/**
 * struct __prci_wrpll_data - WRPLL configuration and integration data
 * @c: WRPLL current configuration record
 * @enable_bypass: fn ptr to code to bypass the WRPLL (if applicable; else NULL)
 * @disable_bypass: fn ptr to code to not bypass the WRPLL (or NULL)
 * @cfg0_offs: WRPLL CFG0 register offset (in bytes) from the PRCI base address
 *
 * @enable_bypass and @disable_bypass are used for WRPLL instances
 * that contain a separate external glitchless clock mux downstream
 * from the PLL.  The WRPLL internal bypass mux is not glitchless.
 */
struct __prci_wrpll_data {
	struct wrpll_cfg c;
	void (*enable_bypass)(struct __prci_data *pd);
	void (*disable_bypass)(struct __prci_data *pd);
	u8 cfg0_offs;
};

/**
 * struct __prci_clock - describes a clock device managed by PRCI
 * @name: user-readable clock name string - should match the manual
 * @parent_name: parent name for this clock
 * @ops: struct clk_ops for the Linux clock framework to use for control
 * @hw: Linux-private clock data
 * @pwd: WRPLL-specific data, associated with this clock (if not NULL)
 * @pd: PRCI-specific data associated with this clock (if not NULL)
 *
 * PRCI clock data.  Used by the PRCI driver to register PRCI-provided
 * clocks to the Linux clock infrastructure.
 */
struct __prci_clock {
	const char *name;
	const char *parent_name;
	const struct clk_ops *ops;
	struct clk_hw hw;
	struct __prci_wrpll_data *pwd;
	struct __prci_data *pd;
};

#define clk_hw_to_prci_clock(pwd) container_of(pwd, struct __prci_clock, hw)

/*
 * struct prci_clk_desc - describes the information of clocks of each SoCs
 * @clks: point to a array of __prci_clock
 * @num_clks: the number of element of clks
 */
struct prci_clk_desc {
	struct __prci_clock *clks;
	size_t num_clks;
};

/* Core clock mux control */
void sifive_prci_coreclksel_use_hfclk(struct __prci_data *pd);
void sifive_prci_coreclksel_use_corepll(struct __prci_data *pd);

/* Linux clock framework integration */
long sifive_prci_wrpll_round_rate(struct clk_hw *hw, unsigned long rate,
				  unsigned long *parent_rate);
int sifive_prci_wrpll_set_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long parent_rate);
unsigned long sifive_prci_wrpll_recalc_rate(struct clk_hw *hw,
					    unsigned long parent_rate);
unsigned long sifive_prci_tlclksel_recalc_rate(struct clk_hw *hw,
					       unsigned long parent_rate);

#endif /* __SIFIVE_CLK_SIFIVE_PRCI_H */
