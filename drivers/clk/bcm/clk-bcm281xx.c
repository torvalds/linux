/*
 * Copyright (C) 2013 Broadcom Corporation
 * Copyright 2013 Linaro Limited
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "clk-kona.h"
#include "dt-bindings/clock/bcm281xx.h"

/* bcm11351 CCU device tree "compatible" strings */
#define BCM11351_DT_ROOT_CCU_COMPAT	"brcm,bcm11351-root-ccu"
#define BCM11351_DT_AON_CCU_COMPAT	"brcm,bcm11351-aon-ccu"
#define BCM11351_DT_HUB_CCU_COMPAT	"brcm,bcm11351-hub-ccu"
#define BCM11351_DT_MASTER_CCU_COMPAT	"brcm,bcm11351-master-ccu"
#define BCM11351_DT_SLAVE_CCU_COMPAT	"brcm,bcm11351-slave-ccu"

/* Root CCU clocks */

static struct peri_clk_data frac_1m_data = {
	.gate		= HW_SW_GATE(0x214, 16, 0, 1),
	.trig		= TRIGGER(0x0e04, 0),
	.div		= FRAC_DIVIDER(0x0e00, 0, 22, 16),
	.clocks		= CLOCKS("ref_crystal"),
};

/* AON CCU clocks */

static struct peri_clk_data hub_timer_data = {
	.gate		= HW_SW_GATE(0x0414, 16, 0, 1),
	.clocks		= CLOCKS("bbl_32k",
				 "frac_1m",
				 "dft_19_5m"),
	.sel		= SELECTOR(0x0a10, 0, 2),
	.trig		= TRIGGER(0x0a40, 4),
};

static struct peri_clk_data pmu_bsc_data = {
	.gate		= HW_SW_GATE(0x0418, 16, 0, 1),
	.clocks		= CLOCKS("ref_crystal",
				 "pmu_bsc_var",
				 "bbl_32k"),
	.sel		= SELECTOR(0x0a04, 0, 2),
	.div		= DIVIDER(0x0a04, 3, 4),
	.trig		= TRIGGER(0x0a40, 0),
};

static struct peri_clk_data pmu_bsc_var_data = {
	.clocks		= CLOCKS("var_312m",
				 "ref_312m"),
	.sel		= SELECTOR(0x0a00, 0, 2),
	.div		= DIVIDER(0x0a00, 4, 5),
	.trig		= TRIGGER(0x0a40, 2),
};

/* Hub CCU clocks */

static struct peri_clk_data tmon_1m_data = {
	.gate		= HW_SW_GATE(0x04a4, 18, 2, 3),
	.clocks		= CLOCKS("ref_crystal",
				 "frac_1m"),
	.sel		= SELECTOR(0x0e74, 0, 2),
	.trig		= TRIGGER(0x0e84, 1),
};

/* Master CCU clocks */

static struct peri_clk_data sdio1_data = {
	.gate		= HW_SW_GATE(0x0358, 18, 2, 3),
	.clocks		= CLOCKS("ref_crystal",
				 "var_52m",
				 "ref_52m",
				 "var_96m",
				 "ref_96m"),
	.sel		= SELECTOR(0x0a28, 0, 3),
	.div		= DIVIDER(0x0a28, 4, 14),
	.trig		= TRIGGER(0x0afc, 9),
};

static struct peri_clk_data sdio2_data = {
	.gate		= HW_SW_GATE(0x035c, 18, 2, 3),
	.clocks		= CLOCKS("ref_crystal",
				 "var_52m",
				 "ref_52m",
				 "var_96m",
				 "ref_96m"),
	.sel		= SELECTOR(0x0a2c, 0, 3),
	.div		= DIVIDER(0x0a2c, 4, 14),
	.trig		= TRIGGER(0x0afc, 10),
};

static struct peri_clk_data sdio3_data = {
	.gate		= HW_SW_GATE(0x0364, 18, 2, 3),
	.clocks		= CLOCKS("ref_crystal",
				 "var_52m",
				 "ref_52m",
				 "var_96m",
				 "ref_96m"),
	.sel		= SELECTOR(0x0a34, 0, 3),
	.div		= DIVIDER(0x0a34, 4, 14),
	.trig		= TRIGGER(0x0afc, 12),
};

static struct peri_clk_data sdio4_data = {
	.gate		= HW_SW_GATE(0x0360, 18, 2, 3),
	.clocks		= CLOCKS("ref_crystal",
				 "var_52m",
				 "ref_52m",
				 "var_96m",
				 "ref_96m"),
	.sel		= SELECTOR(0x0a30, 0, 3),
	.div		= DIVIDER(0x0a30, 4, 14),
	.trig		= TRIGGER(0x0afc, 11),
};

static struct peri_clk_data usb_ic_data = {
	.gate		= HW_SW_GATE(0x0354, 18, 2, 3),
	.clocks		= CLOCKS("ref_crystal",
				 "var_96m",
				 "ref_96m"),
	.div		= FIXED_DIVIDER(2),
	.sel		= SELECTOR(0x0a24, 0, 2),
	.trig		= TRIGGER(0x0afc, 7),
};

/* also called usbh_48m */
static struct peri_clk_data hsic2_48m_data = {
	.gate		= HW_SW_GATE(0x0370, 18, 2, 3),
	.clocks		= CLOCKS("ref_crystal",
				 "var_96m",
				 "ref_96m"),
	.sel		= SELECTOR(0x0a38, 0, 2),
	.div		= FIXED_DIVIDER(2),
	.trig		= TRIGGER(0x0afc, 5),
};

/* also called usbh_12m */
static struct peri_clk_data hsic2_12m_data = {
	.gate		= HW_SW_GATE(0x0370, 20, 4, 5),
	.div		= DIVIDER(0x0a38, 12, 2),
	.clocks		= CLOCKS("ref_crystal",
				 "var_96m",
				 "ref_96m"),
	.pre_div	= FIXED_DIVIDER(2),
	.sel		= SELECTOR(0x0a38, 0, 2),
	.trig		= TRIGGER(0x0afc, 5),
};

/* Slave CCU clocks */

static struct peri_clk_data uartb_data = {
	.gate		= HW_SW_GATE(0x0400, 18, 2, 3),
	.clocks		= CLOCKS("ref_crystal",
				 "var_156m",
				 "ref_156m"),
	.sel		= SELECTOR(0x0a10, 0, 2),
	.div		= FRAC_DIVIDER(0x0a10, 4, 12, 8),
	.trig		= TRIGGER(0x0afc, 2),
};

static struct peri_clk_data uartb2_data = {
	.gate		= HW_SW_GATE(0x0404, 18, 2, 3),
	.clocks		= CLOCKS("ref_crystal",
				 "var_156m",
				 "ref_156m"),
	.sel		= SELECTOR(0x0a14, 0, 2),
	.div		= FRAC_DIVIDER(0x0a14, 4, 12, 8),
	.trig		= TRIGGER(0x0afc, 3),
};

static struct peri_clk_data uartb3_data = {
	.gate		= HW_SW_GATE(0x0408, 18, 2, 3),
	.clocks		= CLOCKS("ref_crystal",
				 "var_156m",
				 "ref_156m"),
	.sel		= SELECTOR(0x0a18, 0, 2),
	.div		= FRAC_DIVIDER(0x0a18, 4, 12, 8),
	.trig		= TRIGGER(0x0afc, 4),
};

static struct peri_clk_data uartb4_data = {
	.gate		= HW_SW_GATE(0x0408, 18, 2, 3),
	.clocks		= CLOCKS("ref_crystal",
				 "var_156m",
				 "ref_156m"),
	.sel		= SELECTOR(0x0a1c, 0, 2),
	.div		= FRAC_DIVIDER(0x0a1c, 4, 12, 8),
	.trig		= TRIGGER(0x0afc, 5),
};

static struct peri_clk_data ssp0_data = {
	.gate		= HW_SW_GATE(0x0410, 18, 2, 3),
	.clocks		= CLOCKS("ref_crystal",
				 "var_104m",
				 "ref_104m",
				 "var_96m",
				 "ref_96m"),
	.sel		= SELECTOR(0x0a20, 0, 3),
	.div		= DIVIDER(0x0a20, 4, 14),
	.trig		= TRIGGER(0x0afc, 6),
};

static struct peri_clk_data ssp2_data = {
	.gate		= HW_SW_GATE(0x0418, 18, 2, 3),
	.clocks		= CLOCKS("ref_crystal",
				 "var_104m",
				 "ref_104m",
				 "var_96m",
				 "ref_96m"),
	.sel		= SELECTOR(0x0a28, 0, 3),
	.div		= DIVIDER(0x0a28, 4, 14),
	.trig		= TRIGGER(0x0afc, 8),
};

static struct peri_clk_data bsc1_data = {
	.gate		= HW_SW_GATE(0x0458, 18, 2, 3),
	.clocks		= CLOCKS("ref_crystal",
				 "var_104m",
				 "ref_104m",
				 "var_13m",
				 "ref_13m"),
	.sel		= SELECTOR(0x0a64, 0, 3),
	.trig		= TRIGGER(0x0afc, 23),
};

static struct peri_clk_data bsc2_data = {
	.gate		= HW_SW_GATE(0x045c, 18, 2, 3),
	.clocks	= CLOCKS("ref_crystal",
				 "var_104m",
				 "ref_104m",
				 "var_13m",
				 "ref_13m"),
	.sel		= SELECTOR(0x0a68, 0, 3),
	.trig		= TRIGGER(0x0afc, 24),
};

static struct peri_clk_data bsc3_data = {
	.gate		= HW_SW_GATE(0x0484, 18, 2, 3),
	.clocks		= CLOCKS("ref_crystal",
				 "var_104m",
				 "ref_104m",
				 "var_13m",
				 "ref_13m"),
	.sel		= SELECTOR(0x0a84, 0, 3),
	.trig		= TRIGGER(0x0b00, 2),
};

static struct peri_clk_data pwm_data = {
	.gate		= HW_SW_GATE(0x0468, 18, 2, 3),
	.clocks		= CLOCKS("ref_crystal",
				 "var_104m"),
	.sel		= SELECTOR(0x0a70, 0, 2),
	.div		= DIVIDER(0x0a70, 4, 3),
	.trig		= TRIGGER(0x0afc, 15),
};

/*
 * CCU setup routines
 *
 * These are called from kona_dt_ccu_setup() to initialize the array
 * of clocks provided by the CCU.  Once allocated, the entries in
 * the array are initialized by calling kona_clk_setup() with the
 * initialization data for each clock.  They return 0 if successful
 * or an error code otherwise.
 */
static int __init bcm281xx_root_ccu_clks_setup(struct ccu_data *ccu)
{
	struct clk **clks;
	size_t count = BCM281XX_ROOT_CCU_CLOCK_COUNT;

	clks = kzalloc(count * sizeof(*clks), GFP_KERNEL);
	if (!clks) {
		pr_err("%s: failed to allocate root clocks\n", __func__);
		return -ENOMEM;
	}
	ccu->data.clks = clks;
	ccu->data.clk_num = count;

	PERI_CLK_SETUP(clks, ccu, BCM281XX_ROOT_CCU_FRAC_1M, frac_1m);

	return 0;
}

static int __init bcm281xx_aon_ccu_clks_setup(struct ccu_data *ccu)
{
	struct clk **clks;
	size_t count = BCM281XX_AON_CCU_CLOCK_COUNT;

	clks = kzalloc(count * sizeof(*clks), GFP_KERNEL);
	if (!clks) {
		pr_err("%s: failed to allocate aon clocks\n", __func__);
		return -ENOMEM;
	}
	ccu->data.clks = clks;
	ccu->data.clk_num = count;

	PERI_CLK_SETUP(clks, ccu, BCM281XX_AON_CCU_HUB_TIMER, hub_timer);
	PERI_CLK_SETUP(clks, ccu, BCM281XX_AON_CCU_PMU_BSC, pmu_bsc);
	PERI_CLK_SETUP(clks, ccu, BCM281XX_AON_CCU_PMU_BSC_VAR, pmu_bsc_var);

	return 0;
}

static int __init bcm281xx_hub_ccu_clks_setup(struct ccu_data *ccu)
{
	struct clk **clks;
	size_t count = BCM281XX_HUB_CCU_CLOCK_COUNT;

	clks = kzalloc(count * sizeof(*clks), GFP_KERNEL);
	if (!clks) {
		pr_err("%s: failed to allocate hub clocks\n", __func__);
		return -ENOMEM;
	}
	ccu->data.clks = clks;
	ccu->data.clk_num = count;

	PERI_CLK_SETUP(clks, ccu, BCM281XX_HUB_CCU_TMON_1M, tmon_1m);

	return 0;
}

static int __init bcm281xx_master_ccu_clks_setup(struct ccu_data *ccu)
{
	struct clk **clks;
	size_t count = BCM281XX_MASTER_CCU_CLOCK_COUNT;

	clks = kzalloc(count * sizeof(*clks), GFP_KERNEL);
	if (!clks) {
		pr_err("%s: failed to allocate master clocks\n", __func__);
		return -ENOMEM;
	}
	ccu->data.clks = clks;
	ccu->data.clk_num = count;

	PERI_CLK_SETUP(clks, ccu, BCM281XX_MASTER_CCU_SDIO1, sdio1);
	PERI_CLK_SETUP(clks, ccu, BCM281XX_MASTER_CCU_SDIO2, sdio2);
	PERI_CLK_SETUP(clks, ccu, BCM281XX_MASTER_CCU_SDIO3, sdio3);
	PERI_CLK_SETUP(clks, ccu, BCM281XX_MASTER_CCU_SDIO4, sdio4);
	PERI_CLK_SETUP(clks, ccu, BCM281XX_MASTER_CCU_USB_IC, usb_ic);
	PERI_CLK_SETUP(clks, ccu, BCM281XX_MASTER_CCU_HSIC2_48M, hsic2_48m);
	PERI_CLK_SETUP(clks, ccu, BCM281XX_MASTER_CCU_HSIC2_12M, hsic2_12m);

	return 0;
}

static int __init bcm281xx_slave_ccu_clks_setup(struct ccu_data *ccu)
{
	struct clk **clks;
	size_t count = BCM281XX_SLAVE_CCU_CLOCK_COUNT;

	clks = kzalloc(count * sizeof(*clks), GFP_KERNEL);
	if (!clks) {
		pr_err("%s: failed to allocate slave clocks\n", __func__);
		return -ENOMEM;
	}
	ccu->data.clks = clks;
	ccu->data.clk_num = count;

	PERI_CLK_SETUP(clks, ccu, BCM281XX_SLAVE_CCU_UARTB, uartb);
	PERI_CLK_SETUP(clks, ccu, BCM281XX_SLAVE_CCU_UARTB2, uartb2);
	PERI_CLK_SETUP(clks, ccu, BCM281XX_SLAVE_CCU_UARTB3, uartb3);
	PERI_CLK_SETUP(clks, ccu, BCM281XX_SLAVE_CCU_UARTB4, uartb4);
	PERI_CLK_SETUP(clks, ccu, BCM281XX_SLAVE_CCU_SSP0, ssp0);
	PERI_CLK_SETUP(clks, ccu, BCM281XX_SLAVE_CCU_SSP2, ssp2);
	PERI_CLK_SETUP(clks, ccu, BCM281XX_SLAVE_CCU_BSC1, bsc1);
	PERI_CLK_SETUP(clks, ccu, BCM281XX_SLAVE_CCU_BSC2, bsc2);
	PERI_CLK_SETUP(clks, ccu, BCM281XX_SLAVE_CCU_BSC3, bsc3);
	PERI_CLK_SETUP(clks, ccu, BCM281XX_SLAVE_CCU_PWM, pwm);

	return 0;
}

/* Device tree match table callback functions */

static void __init kona_dt_root_ccu_setup(struct device_node *node)
{
	kona_dt_ccu_setup(node, bcm281xx_root_ccu_clks_setup);
}

static void __init kona_dt_aon_ccu_setup(struct device_node *node)
{
	kona_dt_ccu_setup(node, bcm281xx_aon_ccu_clks_setup);
}

static void __init kona_dt_hub_ccu_setup(struct device_node *node)
{
	kona_dt_ccu_setup(node, bcm281xx_hub_ccu_clks_setup);
}

static void __init kona_dt_master_ccu_setup(struct device_node *node)
{
	kona_dt_ccu_setup(node, bcm281xx_master_ccu_clks_setup);
}

static void __init kona_dt_slave_ccu_setup(struct device_node *node)
{
	kona_dt_ccu_setup(node, bcm281xx_slave_ccu_clks_setup);
}

CLK_OF_DECLARE(bcm11351_root_ccu, BCM11351_DT_ROOT_CCU_COMPAT,
			kona_dt_root_ccu_setup);
CLK_OF_DECLARE(bcm11351_aon_ccu, BCM11351_DT_AON_CCU_COMPAT,
			kona_dt_aon_ccu_setup);
CLK_OF_DECLARE(bcm11351_hub_ccu, BCM11351_DT_HUB_CCU_COMPAT,
			kona_dt_hub_ccu_setup);
CLK_OF_DECLARE(bcm11351_master_ccu, BCM11351_DT_MASTER_CCU_COMPAT,
			kona_dt_master_ccu_setup);
CLK_OF_DECLARE(bcm11351_slave_ccu, BCM11351_DT_SLAVE_CCU_COMPAT,
			kona_dt_slave_ccu_setup);
