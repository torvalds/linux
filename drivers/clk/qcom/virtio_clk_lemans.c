// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <dt-bindings/clock/qcom,gcc-lemans.h>
#include "virtio_clk_common.h"

static const char * const lemans_gcc_parent_names_usb_prim[] = {
	"usb3_phy_wrapper_gcc_usb30_pipe_clk",
	"core_bi_pll_test_se",
	"bi_tcxo",
};

static const char * const lemans_gcc_parent_names_usb_sec[] = {
	"usb3_uni_phy_sec_gcc_usb30_pipe_clk",
	"core_bi_pll_test_se",
	"bi_tcxo",
};

static const char * const lemans_gcc_parent_names_pcie_0[] = {
	"pcie_0_pipe_clk",
	"gcc_pcie_mbist_pll_test_se_clk_src",
	"bi_tcxo",
};

static const char * const lemans_gcc_parent_names_pcie_1[] = {
	"pcie_1_pipe_clk",
	"gcc_pcie_mbist_pll_test_se_clk_src",
	"bi_tcxo",
};

static const struct virtio_clk_init_data lemans_gcc_virtio_clocks[] = {
	[GCC_QUPV3_WRAP0_S0_CLK] = {.name = "gcc_qupv3_wrap0_s0_clk",},
	[GCC_QUPV3_WRAP0_S1_CLK] = {.name = "gcc_qupv3_wrap0_s1_clk",},
	[GCC_QUPV3_WRAP0_S2_CLK] = {.name = "gcc_qupv3_wrap0_s2_clk",},
	[GCC_QUPV3_WRAP0_S3_CLK] = {.name = "gcc_qupv3_wrap0_s3_clk",},
	[GCC_QUPV3_WRAP0_S4_CLK] = {.name = "gcc_qupv3_wrap0_s4_clk",},
	[GCC_QUPV3_WRAP0_S5_CLK] = {.name = "gcc_qupv3_wrap0_s5_clk",},
	[GCC_QUPV3_WRAP0_S6_CLK] = {.name = "gcc_qupv3_wrap0_s6_clk",},
	[GCC_QUPV3_WRAP1_S0_CLK] = {.name = "gcc_qupv3_wrap1_s0_clk",},
	[GCC_QUPV3_WRAP1_S1_CLK] = {.name = "gcc_qupv3_wrap1_s1_clk",},
	[GCC_QUPV3_WRAP1_S2_CLK] = {.name = "gcc_qupv3_wrap1_s2_clk",},
	[GCC_QUPV3_WRAP1_S3_CLK] = {.name = "gcc_qupv3_wrap1_s3_clk",},
	[GCC_QUPV3_WRAP1_S4_CLK] = {.name = "gcc_qupv3_wrap1_s4_clk",},
	[GCC_QUPV3_WRAP1_S5_CLK] = {.name = "gcc_qupv3_wrap1_s5_clk",},
	[GCC_QUPV3_WRAP1_S6_CLK] = {.name = "gcc_qupv3_wrap1_s6_clk",},
	[GCC_QUPV3_WRAP2_S0_CLK] = {.name = "gcc_qupv3_wrap2_s0_clk",},
	[GCC_QUPV3_WRAP2_S1_CLK] = {.name = "gcc_qupv3_wrap2_s1_clk",},
	[GCC_QUPV3_WRAP2_S2_CLK] = {.name = "gcc_qupv3_wrap2_s2_clk",},
	[GCC_QUPV3_WRAP2_S3_CLK] = {.name = "gcc_qupv3_wrap2_s3_clk",},
	[GCC_QUPV3_WRAP2_S4_CLK] = {.name = "gcc_qupv3_wrap2_s4_clk",},
	[GCC_QUPV3_WRAP2_S5_CLK] = {.name = "gcc_qupv3_wrap2_s5_clk",},
	[GCC_QUPV3_WRAP2_S6_CLK] = {.name = "gcc_qupv3_wrap2_s6_clk",},
	[GCC_QUPV3_WRAP3_S0_CLK] = {.name = "gcc_qupv3_wrap3_s0_clk",},
	[GCC_QUPV3_WRAP_0_M_AHB_CLK] = {.name = "gcc_qupv3_wrap_0_m_ahb_clk",},
	[GCC_QUPV3_WRAP_0_S_AHB_CLK] = {.name = "gcc_qupv3_wrap_0_s_ahb_clk",},
	[GCC_QUPV3_WRAP_1_M_AHB_CLK] = {.name = "gcc_qupv3_wrap_1_m_ahb_clk",},
	[GCC_QUPV3_WRAP_1_S_AHB_CLK] = {.name = "gcc_qupv3_wrap_1_s_ahb_clk",},
	[GCC_QUPV3_WRAP_2_M_AHB_CLK] = {.name = "gcc_qupv3_wrap_2_m_ahb_clk",},
	[GCC_QUPV3_WRAP_2_S_AHB_CLK] = {.name = "gcc_qupv3_wrap_2_s_ahb_clk",},
	[GCC_QUPV3_WRAP_3_M_AHB_CLK] = {.name = "gcc_qupv3_wrap_3_m_ahb_clk",},
	[GCC_QUPV3_WRAP_3_S_AHB_CLK] = {.name = "gcc_qupv3_wrap_3_s_ahb_clk",},
	[GCC_USB30_PRIM_MASTER_CLK] = {.name = "gcc_usb30_prim_master_clk",},
	[GCC_CFG_NOC_USB3_PRIM_AXI_CLK] = {.name = "gcc_cfg_noc_usb3_prim_axi_clk",},
	[GCC_AGGRE_USB3_PRIM_AXI_CLK] = {.name = "gcc_aggre_usb3_prim_axi_clk",},
	[GCC_USB30_PRIM_MOCK_UTMI_CLK] = {.name = "gcc_usb30_prim_mock_utmi_clk",},
	[GCC_USB30_PRIM_SLEEP_CLK] = {.name = "gcc_usb30_prim_sleep_clk",},
	[GCC_USB3_PRIM_PHY_AUX_CLK] = {.name = "gcc_usb3_prim_phy_aux_clk",},
	[GCC_USB3_PRIM_PHY_PIPE_CLK] = {.name = "gcc_usb3_prim_phy_pipe_clk",},
	[GCC_USB3_PRIM_PHY_PIPE_CLK_SRC] = {
				.name = "gcc_usb3_prim_phy_pipe_clk_src",
				.parent_names = lemans_gcc_parent_names_usb_prim,
				.num_parents = ARRAY_SIZE(lemans_gcc_parent_names_usb_prim),
				},
	[GCC_USB_CLKREF_EN] = {.name = "gcc_usb_clkref_en",},
	[GCC_USB3_PRIM_PHY_COM_AUX_CLK] = {.name = "gcc_usb3_prim_phy_com_aux_clk",},
	[GCC_USB30_SEC_MASTER_CLK] = {.name = "gcc_usb30_sec_master_clk",},
	[GCC_CFG_NOC_USB3_SEC_AXI_CLK] = {.name = "gcc_cfg_noc_usb3_sec_axi_clk",},
	[GCC_AGGRE_USB3_SEC_AXI_CLK] = {.name = "gcc_aggre_usb3_sec_axi_clk",},
	[GCC_USB30_SEC_MOCK_UTMI_CLK] = {.name = "gcc_usb30_sec_mock_utmi_clk",},
	[GCC_USB30_SEC_SLEEP_CLK] = {.name = "gcc_usb30_sec_sleep_clk",},
	[GCC_USB3_SEC_PHY_AUX_CLK] = {.name = "gcc_usb3_sec_phy_aux_clk",},
	[GCC_USB3_SEC_PHY_PIPE_CLK] = {.name = "gcc_usb3_sec_phy_pipe_clk",},
	[GCC_USB3_SEC_PHY_PIPE_CLK_SRC] = {
				.name = "gcc_usb3_sec_phy_pipe_clk_src",
				.parent_names = lemans_gcc_parent_names_usb_sec,
				.num_parents = ARRAY_SIZE(lemans_gcc_parent_names_usb_sec),
				},
	[GCC_USB3_SEC_PHY_COM_AUX_CLK] = {.name = "gcc_usb3_sec_phy_com_aux_clk",},
	[GCC_USB20_MASTER_CLK] = {.name = "gcc_usb20_master_clk",},
	[GCC_CFG_NOC_USB2_PRIM_AXI_CLK] = {.name = "gcc_cfg_noc_usb2_prim_axi_clk",},
	[GCC_AGGRE_USB2_PRIM_AXI_CLK] = {.name = "gcc_aggre_usb2_prim_axi_clk",},
	[GCC_USB20_MOCK_UTMI_CLK] = {.name = "gcc_usb20_mock_utmi_clk",},
	[GCC_USB20_SLEEP_CLK] = {.name = "gcc_usb20_sleep_clk",},
	[GCC_PCIE_0_PIPE_CLK] = {.name = "gcc_pcie_0_pipe_clk",},
	[GCC_PCIE_0_AUX_CLK] = {.name = "gcc_pcie_0_aux_clk",},
	[GCC_PCIE_0_CFG_AHB_CLK] = {.name = "gcc_pcie_0_cfg_ahb_clk",},
	[GCC_PCIE_0_MSTR_AXI_CLK] = {.name = "gcc_pcie_0_mstr_axi_clk",},
	[GCC_PCIE_0_SLV_AXI_CLK] = {.name = "gcc_pcie_0_slv_axi_clk",},
	[GCC_PCIE_CLKREF_EN] = {.name = "gcc_pcie_clkref_en",},
	[GCC_PCIE_0_SLV_Q2A_AXI_CLK] = {.name = "gcc_pcie_0_slv_q2a_axi_clk",},
	[GCC_PCIE_0_PHY_RCHNG_CLK] = {.name = "gcc_pcie_0_phy_rchng_clk",},
	[GCC_PCIE_0_PHY_AUX_CLK] = {.name = "gcc_pcie_0_phy_aux_clk",},
	[GCC_PCIE_0_PIPEDIV2_CLK] = {.name = "gcc_pcie_0_pipediv2_clk",},
	[GCC_PCIE_0_PIPE_CLK_SRC] = {
				.name = "gcc_pcie_0_pipe_clk_src",
				.parent_names = lemans_gcc_parent_names_pcie_0,
				.num_parents = ARRAY_SIZE(lemans_gcc_parent_names_pcie_0),
				},
	[GCC_PCIE_1_PIPE_CLK] = {.name = "gcc_pcie_1_pipe_clk",},
	[GCC_PCIE_1_AUX_CLK] = {.name = "gcc_pcie_1_aux_clk",},
	[GCC_PCIE_1_CFG_AHB_CLK] = {.name = "gcc_pcie_1_cfg_ahb_clk",},
	[GCC_PCIE_1_MSTR_AXI_CLK] = {.name = "gcc_pcie_1_mstr_axi_clk",},
	[GCC_PCIE_1_SLV_AXI_CLK] = {.name = "gcc_pcie_1_slv_axi_clk",},
	[GCC_PCIE_1_SLV_Q2A_AXI_CLK] = {.name = "gcc_pcie_1_slv_q2a_axi_clk",},
	[GCC_PCIE_1_PHY_RCHNG_CLK] = {.name = "gcc_pcie_1_phy_rchng_clk",},
	[GCC_PCIE_1_PHY_AUX_CLK] = {.name = "gcc_pcie_1_phy_aux_clk",},
	[GCC_PCIE_1_PIPEDIV2_CLK] = {.name = "gcc_pcie_1_pipediv2_clk",},
	[GCC_PCIE_1_PIPE_CLK_SRC] = {
				.name = "gcc_pcie_1_pipe_clk_src",
				.parent_names = lemans_gcc_parent_names_pcie_1,
				.num_parents = ARRAY_SIZE(lemans_gcc_parent_names_pcie_1),
				},
};

static const char * const lemans_gcc_virtio_resets[] = {
	[GCC_USB30_PRIM_BCR] = "gcc_usb30_prim_master_clk",
	[GCC_USB2_PHY_PRIM_BCR] = "gcc_usb2_phy_prim_bcr",
	[GCC_USB3_PHY_PRIM_BCR] = "gcc_usb3_phy_prim_bcr",
	[GCC_USB3PHY_PHY_PRIM_BCR] = "gcc_usb3phy_phy_prim_bcr",
	[GCC_USB30_SEC_BCR] = "gcc_usb30_sec_master_clk",
	[GCC_USB2_PHY_SEC_BCR] = "gcc_usb2_phy_sec_bcr",
	[GCC_USB3_PHY_SEC_BCR] = "gcc_usb3_phy_sec_bcr",
	[GCC_USB3PHY_PHY_SEC_BCR] = "gcc_usb3phy_phy_sec_bcr",
	[GCC_USB3_PHY_TERT_BCR] = "gcc_usb3_phy_tert_bcr",
	[GCC_USB20_PRIM_BCR] = "gcc_usb20_master_clk",
	[GCC_PCIE_0_BCR] = "gcc_pcie_0_bcr",
	[GCC_PCIE_0_PHY_BCR] = "gcc_pcie_0_phy_bcr",
	[GCC_PCIE_1_BCR] = "gcc_pcie_1_bcr",
	[GCC_PCIE_1_PHY_BCR] = "gcc_pcie_1_phy_bcr",
};

const struct clk_virtio_desc clk_virtio_lemans_gcc = {
	.clks = lemans_gcc_virtio_clocks,
	.num_clks = ARRAY_SIZE(lemans_gcc_virtio_clocks),
	.reset_names = lemans_gcc_virtio_resets,
	.num_resets = ARRAY_SIZE(lemans_gcc_virtio_resets),
};
EXPORT_SYMBOL(clk_virtio_lemans_gcc);

MODULE_LICENSE("GPL");
