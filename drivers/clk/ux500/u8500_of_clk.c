/*
 * Clock definitions for u8500 platform.
 *
 * Copyright (C) 2012 ST-Ericsson SA
 * Author: Ulf Hansson <ulf.hansson@linaro.org>
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/clk-provider.h>
#include <linux/mfd/dbx500-prcmu.h>
#include "clk.h"

#define PRCC_NUM_PERIPH_CLUSTERS 6
#define PRCC_PERIPHS_PER_CLUSTER 32

static struct clk *prcmu_clk[PRCMU_NUM_CLKS];
static struct clk *prcc_pclk[(PRCC_NUM_PERIPH_CLUSTERS + 1) * PRCC_PERIPHS_PER_CLUSTER];
static struct clk *prcc_kclk[(PRCC_NUM_PERIPH_CLUSTERS + 1) * PRCC_PERIPHS_PER_CLUSTER];

#define PRCC_SHOW(clk, base, bit) \
	clk[(base * PRCC_PERIPHS_PER_CLUSTER) + bit]
#define PRCC_PCLK_STORE(clk, base, bit)	\
	prcc_pclk[(base * PRCC_PERIPHS_PER_CLUSTER) + bit] = clk
#define PRCC_KCLK_STORE(clk, base, bit)        \
	prcc_kclk[(base * PRCC_PERIPHS_PER_CLUSTER) + bit] = clk

static struct clk *ux500_twocell_get(struct of_phandle_args *clkspec,
				     void *data)
{
	struct clk **clk_data = data;
	unsigned int base, bit;

	if (clkspec->args_count != 2)
		return  ERR_PTR(-EINVAL);

	base = clkspec->args[0];
	bit = clkspec->args[1];

	if (base != 1 && base != 2 && base != 3 && base != 5 && base != 6) {
		pr_err("%s: invalid PRCC base %d\n", __func__, base);
		return ERR_PTR(-EINVAL);
	}

	return PRCC_SHOW(clk_data, base, bit);
}

/* CLKRST4 is missing making it hard to index things */
enum clkrst_index {
	CLKRST1_INDEX = 0,
	CLKRST2_INDEX,
	CLKRST3_INDEX,
	CLKRST5_INDEX,
	CLKRST6_INDEX,
	CLKRST_MAX,
};

static void u8500_clk_init(struct device_node *np)
{
	struct prcmu_fw_version *fw_version;
	struct device_node *child = NULL;
	const char *sgaclk_parent = NULL;
	struct clk *clk, *rtc_clk, *twd_clk;
	u32 bases[CLKRST_MAX];
	int i;

	for (i = 0; i < ARRAY_SIZE(bases); i++) {
		struct resource r;

		if (of_address_to_resource(np, i, &r))
			/* Not much choice but to continue */
			pr_err("failed to get CLKRST %d base address\n",
			       i + 1);
		bases[i] = r.start;
	}

	/* Clock sources */
	clk = clk_reg_prcmu_gate("soc0_pll", NULL, PRCMU_PLLSOC0,
				CLK_IGNORE_UNUSED);
	prcmu_clk[PRCMU_PLLSOC0] = clk;

	clk = clk_reg_prcmu_gate("soc1_pll", NULL, PRCMU_PLLSOC1,
				CLK_IGNORE_UNUSED);
	prcmu_clk[PRCMU_PLLSOC1] = clk;

	clk = clk_reg_prcmu_gate("ddr_pll", NULL, PRCMU_PLLDDR,
				CLK_IGNORE_UNUSED);
	prcmu_clk[PRCMU_PLLDDR] = clk;

	/* FIXME: Add sys, ulp and int clocks here. */

	rtc_clk = clk_register_fixed_rate(NULL, "rtc32k", "NULL",
				CLK_IGNORE_UNUSED,
				32768);

	/* PRCMU clocks */
	fw_version = prcmu_get_fw_version();
	if (fw_version != NULL) {
		switch (fw_version->project) {
		case PRCMU_FW_PROJECT_U8500_C2:
		case PRCMU_FW_PROJECT_U8520:
		case PRCMU_FW_PROJECT_U8420:
			sgaclk_parent = "soc0_pll";
			break;
		default:
			break;
		}
	}

	if (sgaclk_parent)
		clk = clk_reg_prcmu_gate("sgclk", sgaclk_parent,
					PRCMU_SGACLK, 0);
	else
		clk = clk_reg_prcmu_gate("sgclk", NULL, PRCMU_SGACLK, 0);
	prcmu_clk[PRCMU_SGACLK] = clk;

	clk = clk_reg_prcmu_gate("uartclk", NULL, PRCMU_UARTCLK, 0);
	prcmu_clk[PRCMU_UARTCLK] = clk;

	clk = clk_reg_prcmu_gate("msp02clk", NULL, PRCMU_MSP02CLK, 0);
	prcmu_clk[PRCMU_MSP02CLK] = clk;

	clk = clk_reg_prcmu_gate("msp1clk", NULL, PRCMU_MSP1CLK, 0);
	prcmu_clk[PRCMU_MSP1CLK] = clk;

	clk = clk_reg_prcmu_gate("i2cclk", NULL, PRCMU_I2CCLK, 0);
	prcmu_clk[PRCMU_I2CCLK] = clk;

	clk = clk_reg_prcmu_gate("slimclk", NULL, PRCMU_SLIMCLK, 0);
	prcmu_clk[PRCMU_SLIMCLK] = clk;

	clk = clk_reg_prcmu_gate("per1clk", NULL, PRCMU_PER1CLK, 0);
	prcmu_clk[PRCMU_PER1CLK] = clk;

	clk = clk_reg_prcmu_gate("per2clk", NULL, PRCMU_PER2CLK, 0);
	prcmu_clk[PRCMU_PER2CLK] = clk;

	clk = clk_reg_prcmu_gate("per3clk", NULL, PRCMU_PER3CLK, 0);
	prcmu_clk[PRCMU_PER3CLK] = clk;

	clk = clk_reg_prcmu_gate("per5clk", NULL, PRCMU_PER5CLK, 0);
	prcmu_clk[PRCMU_PER5CLK] = clk;

	clk = clk_reg_prcmu_gate("per6clk", NULL, PRCMU_PER6CLK, 0);
	prcmu_clk[PRCMU_PER6CLK] = clk;

	clk = clk_reg_prcmu_gate("per7clk", NULL, PRCMU_PER7CLK, 0);
	prcmu_clk[PRCMU_PER7CLK] = clk;

	clk = clk_reg_prcmu_scalable("lcdclk", NULL, PRCMU_LCDCLK, 0,
				CLK_SET_RATE_GATE);
	prcmu_clk[PRCMU_LCDCLK] = clk;

	clk = clk_reg_prcmu_opp_gate("bmlclk", NULL, PRCMU_BMLCLK, 0);
	prcmu_clk[PRCMU_BMLCLK] = clk;

	clk = clk_reg_prcmu_scalable("hsitxclk", NULL, PRCMU_HSITXCLK, 0,
				CLK_SET_RATE_GATE);
	prcmu_clk[PRCMU_HSITXCLK] = clk;

	clk = clk_reg_prcmu_scalable("hsirxclk", NULL, PRCMU_HSIRXCLK, 0,
				CLK_SET_RATE_GATE);
	prcmu_clk[PRCMU_HSIRXCLK] = clk;

	clk = clk_reg_prcmu_scalable("hdmiclk", NULL, PRCMU_HDMICLK, 0,
				CLK_SET_RATE_GATE);
	prcmu_clk[PRCMU_HDMICLK] = clk;

	clk = clk_reg_prcmu_gate("apeatclk", NULL, PRCMU_APEATCLK, 0);
	prcmu_clk[PRCMU_APEATCLK] = clk;

	clk = clk_reg_prcmu_scalable("apetraceclk", NULL, PRCMU_APETRACECLK, 0,
				CLK_SET_RATE_GATE);
	prcmu_clk[PRCMU_APETRACECLK] = clk;

	clk = clk_reg_prcmu_gate("mcdeclk", NULL, PRCMU_MCDECLK, 0);
	prcmu_clk[PRCMU_MCDECLK] = clk;

	clk = clk_reg_prcmu_opp_gate("ipi2cclk", NULL, PRCMU_IPI2CCLK, 0);
	prcmu_clk[PRCMU_IPI2CCLK] = clk;

	clk = clk_reg_prcmu_gate("dsialtclk", NULL, PRCMU_DSIALTCLK, 0);
	prcmu_clk[PRCMU_DSIALTCLK] = clk;

	clk = clk_reg_prcmu_gate("dmaclk", NULL, PRCMU_DMACLK, 0);
	prcmu_clk[PRCMU_DMACLK] = clk;

	clk = clk_reg_prcmu_gate("b2r2clk", NULL, PRCMU_B2R2CLK, 0);
	prcmu_clk[PRCMU_B2R2CLK] = clk;

	clk = clk_reg_prcmu_scalable("tvclk", NULL, PRCMU_TVCLK, 0,
				CLK_SET_RATE_GATE);
	prcmu_clk[PRCMU_TVCLK] = clk;

	clk = clk_reg_prcmu_gate("sspclk", NULL, PRCMU_SSPCLK, 0);
	prcmu_clk[PRCMU_SSPCLK] = clk;

	clk = clk_reg_prcmu_gate("rngclk", NULL, PRCMU_RNGCLK, 0);
	prcmu_clk[PRCMU_RNGCLK] = clk;

	clk = clk_reg_prcmu_gate("uiccclk", NULL, PRCMU_UICCCLK, 0);
	prcmu_clk[PRCMU_UICCCLK] = clk;

	clk = clk_reg_prcmu_gate("timclk", NULL, PRCMU_TIMCLK, 0);
	prcmu_clk[PRCMU_TIMCLK] = clk;

	clk = clk_reg_prcmu_gate("ab8500_sysclk", NULL, PRCMU_SYSCLK, 0);
	prcmu_clk[PRCMU_SYSCLK] = clk;

	clk = clk_reg_prcmu_opp_volt_scalable("sdmmcclk", NULL, PRCMU_SDMMCCLK,
					100000000, CLK_SET_RATE_GATE);
	prcmu_clk[PRCMU_SDMMCCLK] = clk;

	clk = clk_reg_prcmu_scalable("dsi_pll", "hdmiclk",
				PRCMU_PLLDSI, 0, CLK_SET_RATE_GATE);
	prcmu_clk[PRCMU_PLLDSI] = clk;

	clk = clk_reg_prcmu_scalable("dsi0clk", "dsi_pll",
				PRCMU_DSI0CLK, 0, CLK_SET_RATE_GATE);
	prcmu_clk[PRCMU_DSI0CLK] = clk;

	clk = clk_reg_prcmu_scalable("dsi1clk", "dsi_pll",
				PRCMU_DSI1CLK, 0, CLK_SET_RATE_GATE);
	prcmu_clk[PRCMU_DSI1CLK] = clk;

	clk = clk_reg_prcmu_scalable("dsi0escclk", "tvclk",
				PRCMU_DSI0ESCCLK, 0, CLK_SET_RATE_GATE);
	prcmu_clk[PRCMU_DSI0ESCCLK] = clk;

	clk = clk_reg_prcmu_scalable("dsi1escclk", "tvclk",
				PRCMU_DSI1ESCCLK, 0, CLK_SET_RATE_GATE);
	prcmu_clk[PRCMU_DSI1ESCCLK] = clk;

	clk = clk_reg_prcmu_scalable("dsi2escclk", "tvclk",
				PRCMU_DSI2ESCCLK, 0, CLK_SET_RATE_GATE);
	prcmu_clk[PRCMU_DSI2ESCCLK] = clk;

	clk = clk_reg_prcmu_scalable_rate("armss", NULL,
				PRCMU_ARMSS, 0, CLK_IGNORE_UNUSED);
	prcmu_clk[PRCMU_ARMSS] = clk;

	twd_clk = clk_register_fixed_factor(NULL, "smp_twd", "armss",
				CLK_IGNORE_UNUSED, 1, 2);

	/*
	 * FIXME: Add special handled PRCMU clocks here:
	 * 1. clkout0yuv, use PRCMU as parent + need regulator + pinctrl.
	 * 2. ab9540_clkout1yuv, see clkout0yuv
	 */

	/* PRCC P-clocks */
	clk = clk_reg_prcc_pclk("p1_pclk0", "per1clk", bases[CLKRST1_INDEX],
				BIT(0), 0);
	PRCC_PCLK_STORE(clk, 1, 0);

	clk = clk_reg_prcc_pclk("p1_pclk1", "per1clk", bases[CLKRST1_INDEX],
				BIT(1), 0);
	PRCC_PCLK_STORE(clk, 1, 1);

	clk = clk_reg_prcc_pclk("p1_pclk2", "per1clk", bases[CLKRST1_INDEX],
				BIT(2), 0);
	PRCC_PCLK_STORE(clk, 1, 2);

	clk = clk_reg_prcc_pclk("p1_pclk3", "per1clk", bases[CLKRST1_INDEX],
				BIT(3), 0);
	PRCC_PCLK_STORE(clk, 1, 3);

	clk = clk_reg_prcc_pclk("p1_pclk4", "per1clk", bases[CLKRST1_INDEX],
				BIT(4), 0);
	PRCC_PCLK_STORE(clk, 1, 4);

	clk = clk_reg_prcc_pclk("p1_pclk5", "per1clk", bases[CLKRST1_INDEX],
				BIT(5), 0);
	PRCC_PCLK_STORE(clk, 1, 5);

	clk = clk_reg_prcc_pclk("p1_pclk6", "per1clk", bases[CLKRST1_INDEX],
				BIT(6), 0);
	PRCC_PCLK_STORE(clk, 1, 6);

	clk = clk_reg_prcc_pclk("p1_pclk7", "per1clk", bases[CLKRST1_INDEX],
				BIT(7), 0);
	PRCC_PCLK_STORE(clk, 1, 7);

	clk = clk_reg_prcc_pclk("p1_pclk8", "per1clk", bases[CLKRST1_INDEX],
				BIT(8), 0);
	PRCC_PCLK_STORE(clk, 1, 8);

	clk = clk_reg_prcc_pclk("p1_pclk9", "per1clk", bases[CLKRST1_INDEX],
				BIT(9), 0);
	PRCC_PCLK_STORE(clk, 1, 9);

	clk = clk_reg_prcc_pclk("p1_pclk10", "per1clk", bases[CLKRST1_INDEX],
				BIT(10), 0);
	PRCC_PCLK_STORE(clk, 1, 10);

	clk = clk_reg_prcc_pclk("p1_pclk11", "per1clk", bases[CLKRST1_INDEX],
				BIT(11), 0);
	PRCC_PCLK_STORE(clk, 1, 11);

	clk = clk_reg_prcc_pclk("p2_pclk0", "per2clk", bases[CLKRST2_INDEX],
				BIT(0), 0);
	PRCC_PCLK_STORE(clk, 2, 0);

	clk = clk_reg_prcc_pclk("p2_pclk1", "per2clk", bases[CLKRST2_INDEX],
				BIT(1), 0);
	PRCC_PCLK_STORE(clk, 2, 1);

	clk = clk_reg_prcc_pclk("p2_pclk2", "per2clk", bases[CLKRST2_INDEX],
				BIT(2), 0);
	PRCC_PCLK_STORE(clk, 2, 2);

	clk = clk_reg_prcc_pclk("p2_pclk3", "per2clk", bases[CLKRST2_INDEX],
				BIT(3), 0);
	PRCC_PCLK_STORE(clk, 2, 3);

	clk = clk_reg_prcc_pclk("p2_pclk4", "per2clk", bases[CLKRST2_INDEX],
				BIT(4), 0);
	PRCC_PCLK_STORE(clk, 2, 4);

	clk = clk_reg_prcc_pclk("p2_pclk5", "per2clk", bases[CLKRST2_INDEX],
				BIT(5), 0);
	PRCC_PCLK_STORE(clk, 2, 5);

	clk = clk_reg_prcc_pclk("p2_pclk6", "per2clk", bases[CLKRST2_INDEX],
				BIT(6), 0);
	PRCC_PCLK_STORE(clk, 2, 6);

	clk = clk_reg_prcc_pclk("p2_pclk7", "per2clk", bases[CLKRST2_INDEX],
				BIT(7), 0);
	PRCC_PCLK_STORE(clk, 2, 7);

	clk = clk_reg_prcc_pclk("p2_pclk8", "per2clk", bases[CLKRST2_INDEX],
				BIT(8), 0);
	PRCC_PCLK_STORE(clk, 2, 8);

	clk = clk_reg_prcc_pclk("p2_pclk9", "per2clk", bases[CLKRST2_INDEX],
				BIT(9), 0);
	PRCC_PCLK_STORE(clk, 2, 9);

	clk = clk_reg_prcc_pclk("p2_pclk10", "per2clk", bases[CLKRST2_INDEX],
				BIT(10), 0);
	PRCC_PCLK_STORE(clk, 2, 10);

	clk = clk_reg_prcc_pclk("p2_pclk11", "per2clk", bases[CLKRST2_INDEX],
				BIT(11), 0);
	PRCC_PCLK_STORE(clk, 2, 11);

	clk = clk_reg_prcc_pclk("p2_pclk12", "per2clk", bases[CLKRST2_INDEX],
				BIT(12), 0);
	PRCC_PCLK_STORE(clk, 2, 12);

	clk = clk_reg_prcc_pclk("p3_pclk0", "per3clk", bases[CLKRST3_INDEX],
				BIT(0), 0);
	PRCC_PCLK_STORE(clk, 3, 0);

	clk = clk_reg_prcc_pclk("p3_pclk1", "per3clk", bases[CLKRST3_INDEX],
				BIT(1), 0);
	PRCC_PCLK_STORE(clk, 3, 1);

	clk = clk_reg_prcc_pclk("p3_pclk2", "per3clk", bases[CLKRST3_INDEX],
				BIT(2), 0);
	PRCC_PCLK_STORE(clk, 3, 2);

	clk = clk_reg_prcc_pclk("p3_pclk3", "per3clk", bases[CLKRST3_INDEX],
				BIT(3), 0);
	PRCC_PCLK_STORE(clk, 3, 3);

	clk = clk_reg_prcc_pclk("p3_pclk4", "per3clk", bases[CLKRST3_INDEX],
				BIT(4), 0);
	PRCC_PCLK_STORE(clk, 3, 4);

	clk = clk_reg_prcc_pclk("p3_pclk5", "per3clk", bases[CLKRST3_INDEX],
				BIT(5), 0);
	PRCC_PCLK_STORE(clk, 3, 5);

	clk = clk_reg_prcc_pclk("p3_pclk6", "per3clk", bases[CLKRST3_INDEX],
				BIT(6), 0);
	PRCC_PCLK_STORE(clk, 3, 6);

	clk = clk_reg_prcc_pclk("p3_pclk7", "per3clk", bases[CLKRST3_INDEX],
				BIT(7), 0);
	PRCC_PCLK_STORE(clk, 3, 7);

	clk = clk_reg_prcc_pclk("p3_pclk8", "per3clk", bases[CLKRST3_INDEX],
				BIT(8), 0);
	PRCC_PCLK_STORE(clk, 3, 8);

	clk = clk_reg_prcc_pclk("p5_pclk0", "per5clk", bases[CLKRST5_INDEX],
				BIT(0), 0);
	PRCC_PCLK_STORE(clk, 5, 0);

	clk = clk_reg_prcc_pclk("p5_pclk1", "per5clk", bases[CLKRST5_INDEX],
				BIT(1), 0);
	PRCC_PCLK_STORE(clk, 5, 1);

	clk = clk_reg_prcc_pclk("p6_pclk0", "per6clk", bases[CLKRST6_INDEX],
				BIT(0), 0);
	PRCC_PCLK_STORE(clk, 6, 0);

	clk = clk_reg_prcc_pclk("p6_pclk1", "per6clk", bases[CLKRST6_INDEX],
				BIT(1), 0);
	PRCC_PCLK_STORE(clk, 6, 1);

	clk = clk_reg_prcc_pclk("p6_pclk2", "per6clk", bases[CLKRST6_INDEX],
				BIT(2), 0);
	PRCC_PCLK_STORE(clk, 6, 2);

	clk = clk_reg_prcc_pclk("p6_pclk3", "per6clk", bases[CLKRST6_INDEX],
				BIT(3), 0);
	PRCC_PCLK_STORE(clk, 6, 3);

	clk = clk_reg_prcc_pclk("p6_pclk4", "per6clk", bases[CLKRST6_INDEX],
				BIT(4), 0);
	PRCC_PCLK_STORE(clk, 6, 4);

	clk = clk_reg_prcc_pclk("p6_pclk5", "per6clk", bases[CLKRST6_INDEX],
				BIT(5), 0);
	PRCC_PCLK_STORE(clk, 6, 5);

	clk = clk_reg_prcc_pclk("p6_pclk6", "per6clk", bases[CLKRST6_INDEX],
				BIT(6), 0);
	PRCC_PCLK_STORE(clk, 6, 6);

	clk = clk_reg_prcc_pclk("p6_pclk7", "per6clk", bases[CLKRST6_INDEX],
				BIT(7), 0);
	PRCC_PCLK_STORE(clk, 6, 7);

	/* PRCC K-clocks
	 *
	 * FIXME: Some drivers requires PERPIH[n| to be automatically enabled
	 * by enabling just the K-clock, even if it is not a valid parent to
	 * the K-clock. Until drivers get fixed we might need some kind of
	 * "parent muxed join".
	 */

	/* Periph1 */
	clk = clk_reg_prcc_kclk("p1_uart0_kclk", "uartclk",
			bases[CLKRST1_INDEX], BIT(0), CLK_SET_RATE_GATE);
	PRCC_KCLK_STORE(clk, 1, 0);

	clk = clk_reg_prcc_kclk("p1_uart1_kclk", "uartclk",
			bases[CLKRST1_INDEX], BIT(1), CLK_SET_RATE_GATE);
	PRCC_KCLK_STORE(clk, 1, 1);

	clk = clk_reg_prcc_kclk("p1_i2c1_kclk", "i2cclk",
			bases[CLKRST1_INDEX], BIT(2), CLK_SET_RATE_GATE);
	PRCC_KCLK_STORE(clk, 1, 2);

	clk = clk_reg_prcc_kclk("p1_msp0_kclk", "msp02clk",
			bases[CLKRST1_INDEX], BIT(3), CLK_SET_RATE_GATE);
	PRCC_KCLK_STORE(clk, 1, 3);

	clk = clk_reg_prcc_kclk("p1_msp1_kclk", "msp1clk",
			bases[CLKRST1_INDEX], BIT(4), CLK_SET_RATE_GATE);
	PRCC_KCLK_STORE(clk, 1, 4);

	clk = clk_reg_prcc_kclk("p1_sdi0_kclk", "sdmmcclk",
			bases[CLKRST1_INDEX], BIT(5), CLK_SET_RATE_GATE);
	PRCC_KCLK_STORE(clk, 1, 5);

	clk = clk_reg_prcc_kclk("p1_i2c2_kclk", "i2cclk",
			bases[CLKRST1_INDEX], BIT(6), CLK_SET_RATE_GATE);
	PRCC_KCLK_STORE(clk, 1, 6);

	clk = clk_reg_prcc_kclk("p1_slimbus0_kclk", "slimclk",
			bases[CLKRST1_INDEX], BIT(8), CLK_SET_RATE_GATE);
	PRCC_KCLK_STORE(clk, 1, 8);

	clk = clk_reg_prcc_kclk("p1_i2c4_kclk", "i2cclk",
			bases[CLKRST1_INDEX], BIT(9), CLK_SET_RATE_GATE);
	PRCC_KCLK_STORE(clk, 1, 9);

	clk = clk_reg_prcc_kclk("p1_msp3_kclk", "msp1clk",
			bases[CLKRST1_INDEX], BIT(10), CLK_SET_RATE_GATE);
	PRCC_KCLK_STORE(clk, 1, 10);

	/* Periph2 */
	clk = clk_reg_prcc_kclk("p2_i2c3_kclk", "i2cclk",
			bases[CLKRST2_INDEX], BIT(0), CLK_SET_RATE_GATE);
	PRCC_KCLK_STORE(clk, 2, 0);

	clk = clk_reg_prcc_kclk("p2_sdi4_kclk", "sdmmcclk",
			bases[CLKRST2_INDEX], BIT(2), CLK_SET_RATE_GATE);
	PRCC_KCLK_STORE(clk, 2, 2);

	clk = clk_reg_prcc_kclk("p2_msp2_kclk", "msp02clk",
			bases[CLKRST2_INDEX], BIT(3), CLK_SET_RATE_GATE);
	PRCC_KCLK_STORE(clk, 2, 3);

	clk = clk_reg_prcc_kclk("p2_sdi1_kclk", "sdmmcclk",
			bases[CLKRST2_INDEX], BIT(4), CLK_SET_RATE_GATE);
	PRCC_KCLK_STORE(clk, 2, 4);

	clk = clk_reg_prcc_kclk("p2_sdi3_kclk", "sdmmcclk",
			bases[CLKRST2_INDEX], BIT(5), CLK_SET_RATE_GATE);
	PRCC_KCLK_STORE(clk, 2, 5);

	/* Note that rate is received from parent. */
	clk = clk_reg_prcc_kclk("p2_ssirx_kclk", "hsirxclk",
			bases[CLKRST2_INDEX], BIT(6),
			CLK_SET_RATE_GATE|CLK_SET_RATE_PARENT);
	PRCC_KCLK_STORE(clk, 2, 6);

	clk = clk_reg_prcc_kclk("p2_ssitx_kclk", "hsitxclk",
			bases[CLKRST2_INDEX], BIT(7),
			CLK_SET_RATE_GATE|CLK_SET_RATE_PARENT);
	PRCC_KCLK_STORE(clk, 2, 7);

	/* Periph3 */
	clk = clk_reg_prcc_kclk("p3_ssp0_kclk", "sspclk",
			bases[CLKRST3_INDEX], BIT(1), CLK_SET_RATE_GATE);
	PRCC_KCLK_STORE(clk, 3, 1);

	clk = clk_reg_prcc_kclk("p3_ssp1_kclk", "sspclk",
			bases[CLKRST3_INDEX], BIT(2), CLK_SET_RATE_GATE);
	PRCC_KCLK_STORE(clk, 3, 2);

	clk = clk_reg_prcc_kclk("p3_i2c0_kclk", "i2cclk",
			bases[CLKRST3_INDEX], BIT(3), CLK_SET_RATE_GATE);
	PRCC_KCLK_STORE(clk, 3, 3);

	clk = clk_reg_prcc_kclk("p3_sdi2_kclk", "sdmmcclk",
			bases[CLKRST3_INDEX], BIT(4), CLK_SET_RATE_GATE);
	PRCC_KCLK_STORE(clk, 3, 4);

	clk = clk_reg_prcc_kclk("p3_ske_kclk", "rtc32k",
			bases[CLKRST3_INDEX], BIT(5), CLK_SET_RATE_GATE);
	PRCC_KCLK_STORE(clk, 3, 5);

	clk = clk_reg_prcc_kclk("p3_uart2_kclk", "uartclk",
			bases[CLKRST3_INDEX], BIT(6), CLK_SET_RATE_GATE);
	PRCC_KCLK_STORE(clk, 3, 6);

	clk = clk_reg_prcc_kclk("p3_sdi5_kclk", "sdmmcclk",
			bases[CLKRST3_INDEX], BIT(7), CLK_SET_RATE_GATE);
	PRCC_KCLK_STORE(clk, 3, 7);

	/* Periph6 */
	clk = clk_reg_prcc_kclk("p3_rng_kclk", "rngclk",
			bases[CLKRST6_INDEX], BIT(0), CLK_SET_RATE_GATE);
	PRCC_KCLK_STORE(clk, 6, 0);

	for_each_child_of_node(np, child) {
		static struct clk_onecell_data clk_data;

		if (!of_node_cmp(child->name, "prcmu-clock")) {
			clk_data.clks = prcmu_clk;
			clk_data.clk_num = ARRAY_SIZE(prcmu_clk);
			of_clk_add_provider(child, of_clk_src_onecell_get, &clk_data);
		}
		if (!of_node_cmp(child->name, "prcc-periph-clock"))
			of_clk_add_provider(child, ux500_twocell_get, prcc_pclk);

		if (!of_node_cmp(child->name, "prcc-kernel-clock"))
			of_clk_add_provider(child, ux500_twocell_get, prcc_kclk);

		if (!of_node_cmp(child->name, "rtc32k-clock"))
			of_clk_add_provider(child, of_clk_src_simple_get, rtc_clk);

		if (!of_node_cmp(child->name, "smp-twd-clock"))
			of_clk_add_provider(child, of_clk_src_simple_get, twd_clk);
	}
}
CLK_OF_DECLARE(u8500_clks, "stericsson,u8500-clks", u8500_clk_init);
