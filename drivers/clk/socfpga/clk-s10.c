// SPDX-License-Identifier:	GPL-2.0
/*
 * Copyright (C) 2017, Intel Corporation
 */
#include <linux/slab.h>
#include <linux/clk-provider.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>

#include <dt-bindings/clock/stratix10-clock.h>

#include "stratix10-clk.h"

static const struct clk_parent_data pll_mux[] = {
	{ .fw_name = "osc1",
	  .name = "osc1" },
	{ .fw_name = "cb-intosc-hs-div2-clk",
	  .name = "cb-intosc-hs-div2-clk" },
	{ .fw_name = "f2s-free-clk",
	  .name = "f2s-free-clk" },
};

static const struct clk_parent_data cntr_mux[] = {
	{ .fw_name =  "main_pll",
	  .name = "main_pll", },
	{ .fw_name = "periph_pll",
	  .name = "periph_pll", },
	{ .fw_name = "osc1",
	  .name = "osc1", },
	{ .fw_name = "cb-intosc-hs-div2-clk",
	  .name = "cb-intosc-hs-div2-clk", },
	{ .fw_name = "f2s-free-clk",
	  .name = "f2s-free-clk", },
};

static const struct clk_parent_data boot_mux[] = {
	{ .fw_name = "osc1",
	  .name = "osc1" },
	{ .fw_name = "cb-intosc-hs-div2-clk",
	  .name = "cb-intosc-hs-div2-clk" },
};

static const struct clk_parent_data noc_free_mux[] = {
	{ .fw_name = "main_noc_base_clk",
	  .name = "main_noc_base_clk", },
	{ .fw_name = "peri_noc_base_clk",
	  .name = "peri_noc_base_clk", },
	{ .fw_name = "osc1",
	  .name = "osc1", },
	{ .fw_name = "cb-intosc-hs-div2-clk",
	  .name = "cb-intosc-hs-div2-clk", },
	{ .fw_name = "f2s-free-clk",
	  .name = "f2s-free-clk", },
};

static const struct clk_parent_data emaca_free_mux[] = {
	{ .fw_name = "peri_emaca_clk",
	  .name = "peri_emaca_clk", },
	{ .fw_name = "boot_clk",
	  .name = "boot_clk", },
};

static const struct clk_parent_data emacb_free_mux[] = {
	{ .fw_name = "peri_emacb_clk",
	  .name = "peri_emacb_clk", },
	{ .fw_name = "boot_clk",
	  .name = "boot_clk", },
};

static const struct clk_parent_data emac_ptp_free_mux[] = {
	{ .fw_name = "peri_emac_ptp_clk",
	  .name = "peri_emac_ptp_clk", },
	{ .fw_name = "boot_clk",
	  .name = "boot_clk", },
};

static const struct clk_parent_data gpio_db_free_mux[] = {
	{ .fw_name = "peri_gpio_db_clk",
	  .name = "peri_gpio_db_clk", },
	{ .fw_name = "boot_clk",
	  .name = "boot_clk", },
};

static const struct clk_parent_data sdmmc_free_mux[] = {
	{ .fw_name = "main_sdmmc_clk",
	  .name = "main_sdmmc_clk", },
	{ .fw_name = "boot_clk",
	  .name = "boot_clk", },
};

static const struct clk_parent_data s2f_usr1_free_mux[] = {
	{ .fw_name = "peri_s2f_usr1_clk",
	  .name = "peri_s2f_usr1_clk", },
	{ .fw_name = "boot_clk",
	  .name = "boot_clk", },
};

static const struct clk_parent_data psi_ref_free_mux[] = {
	{ .fw_name = "peri_psi_ref_clk",
	  .name = "peri_psi_ref_clk", },
	{ .fw_name = "boot_clk",
	  .name = "boot_clk", },
};

static const struct clk_parent_data mpu_mux[] = {
	{ .fw_name = "mpu_free_clk",
	  .name = "mpu_free_clk", },
	{ .fw_name = "boot_clk",
	  .name = "boot_clk", },
};

static const struct clk_parent_data s2f_usr0_mux[] = {
	{ .fw_name = "f2s-free-clk",
	  .name = "f2s-free-clk", },
	{ .fw_name = "boot_clk",
	  .name = "boot_clk", },
};

static const struct clk_parent_data emac_mux[] = {
	{ .fw_name = "emaca_free_clk",
	  .name = "emaca_free_clk", },
	{ .fw_name = "emacb_free_clk",
	  .name = "emacb_free_clk", },
};

static const struct clk_parent_data noc_mux[] = {
	{ .fw_name = "noc_free_clk",
	  .name = "noc_free_clk", },
	{ .fw_name = "boot_clk",
	  .name = "boot_clk", },
};

static const struct clk_parent_data mpu_free_mux[] = {
	{ .fw_name = "main_mpu_base_clk",
	  .name = "main_mpu_base_clk", },
	{ .fw_name = "peri_mpu_base_clk",
	  .name = "peri_mpu_base_clk", },
	{ .fw_name = "osc1",
	  .name = "osc1", },
	{ .fw_name = "cb-intosc-hs-div2-clk",
	  .name = "cb-intosc-hs-div2-clk", },
	{ .fw_name = "f2s-free-clk",
	  .name = "f2s-free-clk", },
};

/* clocks in AO (always on) controller */
static const struct stratix10_pll_clock s10_pll_clks[] = {
	{ STRATIX10_BOOT_CLK, "boot_clk", boot_mux, ARRAY_SIZE(boot_mux), 0,
	  0x0},
	{ STRATIX10_MAIN_PLL_CLK, "main_pll", pll_mux, ARRAY_SIZE(pll_mux),
	  0, 0x74},
	{ STRATIX10_PERIPH_PLL_CLK, "periph_pll", pll_mux, ARRAY_SIZE(pll_mux),
	  0, 0xe4},
};

static const struct stratix10_perip_c_clock s10_main_perip_c_clks[] = {
	{ STRATIX10_MAIN_MPU_BASE_CLK, "main_mpu_base_clk", "main_pll", NULL, 1, 0, 0x84},
	{ STRATIX10_MAIN_NOC_BASE_CLK, "main_noc_base_clk", "main_pll", NULL, 1, 0, 0x88},
	{ STRATIX10_PERI_MPU_BASE_CLK, "peri_mpu_base_clk", "periph_pll", NULL, 1, 0,
	  0xF4},
	{ STRATIX10_PERI_NOC_BASE_CLK, "peri_noc_base_clk", "periph_pll", NULL, 1, 0,
	  0xF8},
};

static const struct stratix10_perip_cnt_clock s10_main_perip_cnt_clks[] = {
	{ STRATIX10_MPU_FREE_CLK, "mpu_free_clk", NULL, mpu_free_mux, ARRAY_SIZE(mpu_free_mux),
	   0, 0x48, 0, 0, 0},
	{ STRATIX10_NOC_FREE_CLK, "noc_free_clk", NULL, noc_free_mux, ARRAY_SIZE(noc_free_mux),
	  0, 0x4C, 0, 0, 0},
	{ STRATIX10_MAIN_EMACA_CLK, "main_emaca_clk", "main_noc_base_clk", NULL, 1, 0,
	  0x50, 0, 0, 0},
	{ STRATIX10_MAIN_EMACB_CLK, "main_emacb_clk", "main_noc_base_clk", NULL, 1, 0,
	  0x54, 0, 0, 0},
	{ STRATIX10_MAIN_EMAC_PTP_CLK, "main_emac_ptp_clk", "main_noc_base_clk", NULL, 1, 0,
	  0x58, 0, 0, 0},
	{ STRATIX10_MAIN_GPIO_DB_CLK, "main_gpio_db_clk", "main_noc_base_clk", NULL, 1, 0,
	  0x5C, 0, 0, 0},
	{ STRATIX10_MAIN_SDMMC_CLK, "main_sdmmc_clk", "main_noc_base_clk", NULL, 1, 0,
	  0x60, 0, 0, 0},
	{ STRATIX10_MAIN_S2F_USR0_CLK, "main_s2f_usr0_clk", NULL, cntr_mux, ARRAY_SIZE(cntr_mux),
	  0, 0x64, 0, 0, 0},
	{ STRATIX10_MAIN_S2F_USR1_CLK, "main_s2f_usr1_clk", "main_noc_base_clk", NULL, 1, 0,
	  0x68, 0, 0, 0},
	{ STRATIX10_MAIN_PSI_REF_CLK, "main_psi_ref_clk", "main_noc_base_clk", NULL, 1, 0,
	  0x6C, 0, 0, 0},
	{ STRATIX10_PERI_EMACA_CLK, "peri_emaca_clk", NULL, cntr_mux, ARRAY_SIZE(cntr_mux),
	  0, 0xBC, 0, 0, 0},
	{ STRATIX10_PERI_EMACB_CLK, "peri_emacb_clk", NULL, cntr_mux, ARRAY_SIZE(cntr_mux),
	  0, 0xC0, 0, 0, 0},
	{ STRATIX10_PERI_EMAC_PTP_CLK, "peri_emac_ptp_clk", NULL, cntr_mux, ARRAY_SIZE(cntr_mux),
	  0, 0xC4, 0, 0, 0},
	{ STRATIX10_PERI_GPIO_DB_CLK, "peri_gpio_db_clk", NULL, cntr_mux, ARRAY_SIZE(cntr_mux),
	  0, 0xC8, 0, 0, 0},
	{ STRATIX10_PERI_SDMMC_CLK, "peri_sdmmc_clk", NULL, cntr_mux, ARRAY_SIZE(cntr_mux),
	  0, 0xCC, 0, 0, 0},
	{ STRATIX10_PERI_S2F_USR0_CLK, "peri_s2f_usr0_clk", "peri_noc_base_clk", NULL, 1, 0,
	  0xD0, 0, 0, 0},
	{ STRATIX10_PERI_S2F_USR1_CLK, "peri_s2f_usr1_clk", NULL, cntr_mux, ARRAY_SIZE(cntr_mux),
	  0, 0xD4, 0, 0, 0},
	{ STRATIX10_PERI_PSI_REF_CLK, "peri_psi_ref_clk", "peri_noc_base_clk", NULL, 1, 0,
	  0xD8, 0, 0, 0},
	{ STRATIX10_L4_SYS_FREE_CLK, "l4_sys_free_clk", "noc_free_clk", NULL, 1, 0,
	  0, 4, 0, 0},
	{ STRATIX10_NOC_CLK, "noc_clk", NULL, noc_mux, ARRAY_SIZE(noc_mux),
	  0, 0, 0, 0x3C, 1},
	{ STRATIX10_EMAC_A_FREE_CLK, "emaca_free_clk", NULL, emaca_free_mux, ARRAY_SIZE(emaca_free_mux),
	  0, 0, 2, 0xB0, 0},
	{ STRATIX10_EMAC_B_FREE_CLK, "emacb_free_clk", NULL, emacb_free_mux, ARRAY_SIZE(emacb_free_mux),
	  0, 0, 2, 0xB0, 1},
	{ STRATIX10_EMAC_PTP_FREE_CLK, "emac_ptp_free_clk", NULL, emac_ptp_free_mux,
	  ARRAY_SIZE(emac_ptp_free_mux), 0, 0, 2, 0xB0, 2},
	{ STRATIX10_GPIO_DB_FREE_CLK, "gpio_db_free_clk", NULL, gpio_db_free_mux,
	  ARRAY_SIZE(gpio_db_free_mux), 0, 0, 0, 0xB0, 3},
	{ STRATIX10_SDMMC_FREE_CLK, "sdmmc_free_clk", NULL, sdmmc_free_mux,
	  ARRAY_SIZE(sdmmc_free_mux), 0, 0, 0, 0xB0, 4},
	{ STRATIX10_S2F_USER1_FREE_CLK, "s2f_user1_free_clk", NULL, s2f_usr1_free_mux,
	  ARRAY_SIZE(s2f_usr1_free_mux), 0, 0, 0, 0xB0, 5},
	{ STRATIX10_PSI_REF_FREE_CLK, "psi_ref_free_clk", NULL, psi_ref_free_mux,
	  ARRAY_SIZE(psi_ref_free_mux), 0, 0, 0, 0xB0, 6},
};

static const struct stratix10_gate_clock s10_gate_clks[] = {
	{ STRATIX10_MPU_CLK, "mpu_clk", NULL, mpu_mux, ARRAY_SIZE(mpu_mux), 0, 0x30,
	  0, 0, 0, 0, 0x3C, 0, 0},
	{ STRATIX10_MPU_PERIPH_CLK, "mpu_periph_clk", "mpu_clk", NULL, 1, 0, 0x30,
	  0, 0, 0, 0, 0, 0, 4},
	{ STRATIX10_MPU_L2RAM_CLK, "mpu_l2ram_clk", "mpu_clk", NULL, 1, 0, 0x30,
	  0, 0, 0, 0, 0, 0, 2},
	{ STRATIX10_L4_MAIN_CLK, "l4_main_clk", "noc_clk", NULL, 1, 0, 0x30,
	  1, 0x70, 0, 2, 0, 0, 0},
	{ STRATIX10_L4_MP_CLK, "l4_mp_clk", "noc_clk", NULL, 1, 0, 0x30,
	  2, 0x70, 8, 2, 0, 0, 0},
	{ STRATIX10_L4_SP_CLK, "l4_sp_clk", "noc_clk", NULL, 1, CLK_IS_CRITICAL, 0x30,
	  3, 0x70, 16, 2, 0, 0, 0},
	{ STRATIX10_CS_AT_CLK, "cs_at_clk", "noc_clk", NULL, 1, 0, 0x30,
	  4, 0x70, 24, 2, 0, 0, 0},
	{ STRATIX10_CS_TRACE_CLK, "cs_trace_clk", "noc_clk", NULL, 1, 0, 0x30,
	  4, 0x70, 26, 2, 0, 0, 0},
	{ STRATIX10_CS_PDBG_CLK, "cs_pdbg_clk", "cs_at_clk", NULL, 1, 0, 0x30,
	  4, 0x70, 28, 1, 0, 0, 0},
	{ STRATIX10_CS_TIMER_CLK, "cs_timer_clk", "noc_clk", NULL, 1, 0, 0x30,
	  5, 0, 0, 0, 0, 0, 0},
	{ STRATIX10_S2F_USER0_CLK, "s2f_user0_clk", NULL, s2f_usr0_mux, ARRAY_SIZE(s2f_usr0_mux), 0, 0x30,
	  6, 0, 0, 0, 0, 0, 0},
	{ STRATIX10_EMAC0_CLK, "emac0_clk", NULL, emac_mux, ARRAY_SIZE(emac_mux), 0, 0xA4,
	  0, 0, 0, 0, 0xDC, 26, 0},
	{ STRATIX10_EMAC1_CLK, "emac1_clk", NULL, emac_mux, ARRAY_SIZE(emac_mux), 0, 0xA4,
	  1, 0, 0, 0, 0xDC, 27, 0},
	{ STRATIX10_EMAC2_CLK, "emac2_clk", NULL, emac_mux, ARRAY_SIZE(emac_mux), 0, 0xA4,
	  2, 0, 0, 0, 0xDC, 28, 0},
	{ STRATIX10_EMAC_PTP_CLK, "emac_ptp_clk", "emac_ptp_free_clk", NULL, 1, 0, 0xA4,
	  3, 0, 0, 0, 0, 0, 0},
	{ STRATIX10_GPIO_DB_CLK, "gpio_db_clk", "gpio_db_free_clk", NULL, 1, 0, 0xA4,
	  4, 0xE0, 0, 16, 0, 0, 0},
	{ STRATIX10_SDMMC_CLK, "sdmmc_clk", "sdmmc_free_clk", NULL, 1, 0, 0xA4,
	  5, 0, 0, 0, 0, 0, 4},
	{ STRATIX10_S2F_USER1_CLK, "s2f_user1_clk", "s2f_user1_free_clk", NULL, 1, 0, 0xA4,
	  6, 0, 0, 0, 0, 0, 0},
	{ STRATIX10_PSI_REF_CLK, "psi_ref_clk", "psi_ref_free_clk", NULL, 1, 0, 0xA4,
	  7, 0, 0, 0, 0, 0, 0},
	{ STRATIX10_USB_CLK, "usb_clk", "l4_mp_clk", NULL, 1, 0, 0xA4,
	  8, 0, 0, 0, 0, 0, 0},
	{ STRATIX10_SPI_M_CLK, "spi_m_clk", "l4_mp_clk", NULL, 1, 0, 0xA4,
	  9, 0, 0, 0, 0, 0, 0},
	{ STRATIX10_NAND_X_CLK, "nand_x_clk", "l4_mp_clk", NULL, 1, 0, 0xA4,
	  10, 0, 0, 0, 0, 0, 0},
	{ STRATIX10_NAND_CLK, "nand_clk", "nand_x_clk", NULL, 1, 0, 0xA4,
	  10, 0, 0, 0, 0, 0, 4},
	{ STRATIX10_NAND_ECC_CLK, "nand_ecc_clk", "nand_x_clk", NULL, 1, 0, 0xA4,
	  10, 0, 0, 0, 0, 0, 4},
};

static int s10_clk_register_c_perip(const struct stratix10_perip_c_clock *clks,
				    int nums, struct stratix10_clock_data *data)
{
	struct clk *clk;
	void __iomem *base = data->base;
	int i;

	for (i = 0; i < nums; i++) {
		clk = s10_register_periph(&clks[i], base);
		if (IS_ERR(clk)) {
			pr_err("%s: failed to register clock %s\n",
			       __func__, clks[i].name);
			continue;
		}
		data->clk_data.clks[clks[i].id] = clk;
	}
	return 0;
}

static int s10_clk_register_cnt_perip(const struct stratix10_perip_cnt_clock *clks,
				      int nums, struct stratix10_clock_data *data)
{
	struct clk *clk;
	void __iomem *base = data->base;
	int i;

	for (i = 0; i < nums; i++) {
		clk = s10_register_cnt_periph(&clks[i], base);
		if (IS_ERR(clk)) {
			pr_err("%s: failed to register clock %s\n",
			       __func__, clks[i].name);
			continue;
		}
		data->clk_data.clks[clks[i].id] = clk;
	}

	return 0;
}

static int s10_clk_register_gate(const struct stratix10_gate_clock *clks,
				 int nums, struct stratix10_clock_data *data)
{
	struct clk *clk;
	void __iomem *base = data->base;
	int i;

	for (i = 0; i < nums; i++) {
		clk = s10_register_gate(&clks[i], base);
		if (IS_ERR(clk)) {
			pr_err("%s: failed to register clock %s\n",
			       __func__, clks[i].name);
			continue;
		}
		data->clk_data.clks[clks[i].id] = clk;
	}

	return 0;
}

static int s10_clk_register_pll(const struct stratix10_pll_clock *clks,
				 int nums, struct stratix10_clock_data *data)
{
	struct clk *clk;
	void __iomem *base = data->base;
	int i;

	for (i = 0; i < nums; i++) {
		clk = s10_register_pll(&clks[i], base);
		if (IS_ERR(clk)) {
			pr_err("%s: failed to register clock %s\n",
			       __func__, clks[i].name);
			continue;
		}
		data->clk_data.clks[clks[i].id] = clk;
	}

	return 0;
}

static struct stratix10_clock_data *__socfpga_s10_clk_init(struct platform_device *pdev,
						    int nr_clks)
{
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct stratix10_clock_data *clk_data;
	struct clk **clk_table;
	struct resource *res;
	void __iomem *base;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base)) {
		pr_err("%s: failed to map clock registers\n", __func__);
		return ERR_CAST(base);
	}

	clk_data = devm_kzalloc(dev, sizeof(*clk_data), GFP_KERNEL);
	if (!clk_data)
		return ERR_PTR(-ENOMEM);

	clk_data->base = base;
	clk_table = devm_kcalloc(dev, nr_clks, sizeof(*clk_table), GFP_KERNEL);
	if (!clk_table)
		return ERR_PTR(-ENOMEM);

	clk_data->clk_data.clks = clk_table;
	clk_data->clk_data.clk_num = nr_clks;
	of_clk_add_provider(np, of_clk_src_onecell_get, &clk_data->clk_data);
	return clk_data;
}

static int s10_clkmgr_init(struct platform_device *pdev)
{
	struct stratix10_clock_data *clk_data;

	clk_data = __socfpga_s10_clk_init(pdev, STRATIX10_NUM_CLKS);
	if (IS_ERR(clk_data))
		return PTR_ERR(clk_data);

	s10_clk_register_pll(s10_pll_clks, ARRAY_SIZE(s10_pll_clks), clk_data);

	s10_clk_register_c_perip(s10_main_perip_c_clks,
				 ARRAY_SIZE(s10_main_perip_c_clks), clk_data);

	s10_clk_register_cnt_perip(s10_main_perip_cnt_clks,
				   ARRAY_SIZE(s10_main_perip_cnt_clks),
				   clk_data);

	s10_clk_register_gate(s10_gate_clks, ARRAY_SIZE(s10_gate_clks),
			      clk_data);
	return 0;
}

static int s10_clkmgr_probe(struct platform_device *pdev)
{
	return	s10_clkmgr_init(pdev);
}

static const struct of_device_id stratix10_clkmgr_match_table[] = {
	{ .compatible = "intel,stratix10-clkmgr",
	  .data = s10_clkmgr_init },
	{ }
};

static struct platform_driver stratix10_clkmgr_driver = {
	.probe		= s10_clkmgr_probe,
	.driver		= {
		.name	= "stratix10-clkmgr",
		.suppress_bind_attrs = true,
		.of_match_table = stratix10_clkmgr_match_table,
	},
};

static int __init s10_clk_init(void)
{
	return platform_driver_register(&stratix10_clkmgr_driver);
}
core_initcall(s10_clk_init);
