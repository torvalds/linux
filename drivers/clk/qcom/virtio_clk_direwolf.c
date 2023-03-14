// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022-2023, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <dt-bindings/clock/qcom,gcc-direwolf.h>
#include "virtio_clk_common.h"

static const char * const dirwolf_gcc_parent_names_0[] = {
	"pcie_2a_pipe_clk",
	"gcc_pcie_mbist_pll_test_se_clk_src",
	"bi_tcxo",
};

static const char * const dirwolf_gcc_parent_names_1[] = {
	"pcie_2b_pipe_clk",
	"gcc_pcie_mbist_pll_test_se_clk_src",
	"bi_tcxo",
};

static const char * const dirwolf_gcc_parent_names_2[] = {
	"pcie_3a_pipe_clk",
	"gcc_pcie_mbist_pll_test_se_clk_src",
	"bi_tcxo",
};

static const char * const dirwolf_gcc_parent_names_3[] = {
	"pcie_3b_pipe_clk",
	"gcc_pcie_mbist_pll_test_se_clk_src",
	"bi_tcxo",
};

static const char * const dirwolf_gcc_parent_names_4[] = {
	"pcie_4_pipe_clk",
	"gcc_pcie_mbist_pll_test_se_clk_src",
	"bi_tcxo",
};

static const char * const dirwolf_gcc_parent_names_usb_prim[] = {
	"usb3_phy_wrapper_gcc_usb30_pipe_clk",
	"core_bi_pll_test_se",
	"bi_tcxo",
};

static const char * const dirwolf_gcc_parent_names_usb_sec[] = {
	"usb3_uni_phy_sec_gcc_usb30_pipe_clk",
	"core_bi_pll_test_se",
	"bi_tcxo",
};

static const char * const dirwolf_gcc_parent_names_usb_mp0[] = {
	"usb3_uni_phy_mp_gcc_usb30_pipe_0_clk",
	"core_bi_pll_test_se",
	"bi_tcxo",
};

static const char * const dirwolf_gcc_parent_names_usb_mp1[] = {
	"usb3_uni_phy_mp_gcc_usb30_pipe_1_clk",
	"core_bi_pll_test_se",
	"bi_tcxo",
};

static const struct virtio_clk_init_data direwolf_gcc_virtio_clocks[] = {
	[GCC_QUPV3_WRAP0_S0_CLK] = {.name = "gcc_qupv3_wrap0_s0_clk",},
	[GCC_QUPV3_WRAP0_S1_CLK] = {.name = "gcc_qupv3_wrap0_s1_clk",},
	[GCC_QUPV3_WRAP0_S2_CLK] = {.name = "gcc_qupv3_wrap0_s2_clk",},
	[GCC_QUPV3_WRAP0_S3_CLK] = {.name = "gcc_qupv3_wrap0_s3_clk",},
	[GCC_QUPV3_WRAP0_S4_CLK] = {.name = "gcc_qupv3_wrap0_s4_clk",},
	[GCC_QUPV3_WRAP0_S5_CLK] = {.name = "gcc_qupv3_wrap0_s5_clk",},
	[GCC_QUPV3_WRAP0_S6_CLK] = {.name = "gcc_qupv3_wrap0_s6_clk",},
	[GCC_QUPV3_WRAP0_S7_CLK] = {.name = "gcc_qupv3_wrap0_s7_clk",},
	[GCC_QUPV3_WRAP1_S0_CLK] = {.name = "gcc_qupv3_wrap1_s0_clk",},
	[GCC_QUPV3_WRAP1_S1_CLK] = {.name = "gcc_qupv3_wrap1_s1_clk",},
	[GCC_QUPV3_WRAP1_S2_CLK] = {.name = "gcc_qupv3_wrap1_s2_clk",},
	[GCC_QUPV3_WRAP1_S3_CLK] = {.name = "gcc_qupv3_wrap1_s3_clk",},
	[GCC_QUPV3_WRAP1_S4_CLK] = {.name = "gcc_qupv3_wrap1_s4_clk",},
	[GCC_QUPV3_WRAP1_S5_CLK] = {.name = "gcc_qupv3_wrap1_s5_clk",},
	[GCC_QUPV3_WRAP1_S6_CLK] = {.name = "gcc_qupv3_wrap1_s6_clk",},
	[GCC_QUPV3_WRAP1_S7_CLK] = {.name = "gcc_qupv3_wrap1_s7_clk",},
	[GCC_QUPV3_WRAP2_S0_CLK] = {.name = "gcc_qupv3_wrap2_s0_clk",},
	[GCC_QUPV3_WRAP2_S1_CLK] = {.name = "gcc_qupv3_wrap2_s1_clk",},
	[GCC_QUPV3_WRAP2_S2_CLK] = {.name = "gcc_qupv3_wrap2_s2_clk",},
	[GCC_QUPV3_WRAP2_S3_CLK] = {.name = "gcc_qupv3_wrap2_s3_clk",},
	[GCC_QUPV3_WRAP2_S4_CLK] = {.name = "gcc_qupv3_wrap2_s4_clk",},
	[GCC_QUPV3_WRAP2_S5_CLK] = {.name = "gcc_qupv3_wrap2_s5_clk",},
	[GCC_QUPV3_WRAP2_S6_CLK] = {.name = "gcc_qupv3_wrap2_s6_clk",},
	[GCC_QUPV3_WRAP2_S7_CLK] = {.name = "gcc_qupv3_wrap2_s7_clk",},
	[GCC_QUPV3_WRAP_0_M_AHB_CLK] = {.name = "gcc_qupv3_wrap_0_m_ahb_clk",},
	[GCC_QUPV3_WRAP_0_S_AHB_CLK] = {.name = "gcc_qupv3_wrap_0_s_ahb_clk",},
	[GCC_QUPV3_WRAP_1_M_AHB_CLK] = {.name = "gcc_qupv3_wrap_1_m_ahb_clk",},
	[GCC_QUPV3_WRAP_1_S_AHB_CLK] = {.name = "gcc_qupv3_wrap_1_s_ahb_clk",},
	[GCC_QUPV3_WRAP_2_M_AHB_CLK] = {.name = "gcc_qupv3_wrap_2_m_ahb_clk",},
	[GCC_QUPV3_WRAP_2_S_AHB_CLK] = {.name = "gcc_qupv3_wrap_2_s_ahb_clk",},
	[GCC_USB30_PRIM_MASTER_CLK] = {.name = "gcc_usb30_prim_master_clk",},
	[GCC_CFG_NOC_USB3_PRIM_AXI_CLK] = {.name = "gcc_cfg_noc_usb3_prim_axi_clk",},
	[GCC_AGGRE_USB3_PRIM_AXI_CLK] = {.name = "gcc_aggre_usb3_prim_axi_clk",},
	[GCC_USB30_PRIM_MOCK_UTMI_CLK] = {.name = "gcc_usb30_prim_mock_utmi_clk",},
	[GCC_USB30_PRIM_SLEEP_CLK] = {.name = "gcc_usb30_prim_sleep_clk",},
	[GCC_USB3_PRIM_PHY_AUX_CLK] = {.name = "gcc_usb3_prim_phy_aux_clk",},
	[GCC_USB3_PRIM_PHY_PIPE_CLK] = {.name = "gcc_usb3_prim_phy_pipe_clk",},
	[GCC_USB3_PRIM_PHY_PIPE_CLK_SRC] = {
				.name = "gcc_usb3_prim_phy_pipe_clk_src",
				.parent_names = dirwolf_gcc_parent_names_usb_prim,
				.num_parents = ARRAY_SIZE(dirwolf_gcc_parent_names_usb_prim),
				},
	[GCC_USB3_PRIM_PHY_COM_AUX_CLK] = {.name = "gcc_usb3_prim_phy_com_aux_clk",},
	[GCC_USB4_EUD_CLKREF_CLK] = {.name = "gcc_usb4_eud_clkref_en",},
	[GCC_USB30_SEC_MASTER_CLK] = {.name = "gcc_usb30_sec_master_clk",},
	[GCC_CFG_NOC_USB3_SEC_AXI_CLK] = {.name = "gcc_cfg_noc_usb3_sec_axi_clk",},
	[GCC_AGGRE_USB3_SEC_AXI_CLK] = {.name = "gcc_aggre_usb3_sec_axi_clk",},
	[GCC_USB30_SEC_MOCK_UTMI_CLK] = {.name = "gcc_usb30_sec_mock_utmi_clk",},
	[GCC_USB30_SEC_SLEEP_CLK] = {.name = "gcc_usb30_sec_sleep_clk",},
	[GCC_USB3_SEC_PHY_AUX_CLK] = {.name = "gcc_usb3_sec_phy_aux_clk",},
	[GCC_USB3_SEC_PHY_PIPE_CLK] = {.name = "gcc_usb3_sec_phy_pipe_clk",},
	[GCC_USB3_SEC_PHY_PIPE_CLK_SRC] = {
				.name = "gcc_usb3_sec_phy_pipe_clk_src",
				.parent_names = dirwolf_gcc_parent_names_usb_sec,
				.num_parents = ARRAY_SIZE(dirwolf_gcc_parent_names_usb_sec),
				},
	[GCC_USB3_SEC_PHY_COM_AUX_CLK] = {.name = "gcc_usb3_sec_phy_com_aux_clk",},
	[GCC_USB4_CLKREF_CLK] = {.name = "gcc_usb4_clkref_en",},
	[GCC_USB30_MP_MASTER_CLK] = {.name = "gcc_usb30_mp_master_clk",},
	[GCC_CFG_NOC_USB3_MP_AXI_CLK] = {.name = "gcc_cfg_noc_usb3_mp_axi_clk",},
	[GCC_AGGRE_USB3_MP_AXI_CLK] = {.name = "gcc_aggre_usb3_mp_axi_clk",},
	[GCC_USB30_MP_MOCK_UTMI_CLK] = {.name = "gcc_usb30_mp_mock_utmi_clk",},
	[GCC_USB30_MP_SLEEP_CLK] = {.name = "gcc_usb30_mp_sleep_clk",},
	[GCC_AGGRE_USB_NOC_AXI_CLK] = {.name = "gcc_aggre_usb_noc_axi_clk",},
	[GCC_AGGRE_USB_NOC_NORTH_AXI_CLK] = {.name = "gcc_aggre_usb_noc_north_axi_clk",},
	[GCC_AGGRE_USB_NOC_SOUTH_AXI_CLK] = {.name = "gcc_aggre_usb_noc_south_axi_clk",},
	[GCC_SYS_NOC_USB_AXI_CLK] = {.name = "gcc_sys_noc_usb_axi_clk",},
	[GCC_USB2_HS0_CLKREF_CLK] = {.name = "gcc_usb2_hs0_clkref_en",},
	[GCC_USB2_HS1_CLKREF_CLK] = {.name = "gcc_usb2_hs1_clkref_en",},
	[GCC_USB2_HS2_CLKREF_CLK] = {.name = "gcc_usb2_hs2_clkref_en",},
	[GCC_USB2_HS3_CLKREF_CLK] = {.name = "gcc_usb2_hs3_clkref_en",},
	[GCC_USB3_MP_PHY_AUX_CLK] = {.name = "gcc_usb3_mp_phy_aux_clk",},
	[GCC_USB3_MP_PHY_PIPE_0_CLK] = {.name = "gcc_usb3_mp_phy_pipe_0_clk",},
	[GCC_USB3_MP_PHY_PIPE_0_CLK_SRC] = {
				.name = "gcc_usb3_mp_phy_pipe_0_clk_src",
				.parent_names = dirwolf_gcc_parent_names_usb_mp0,
				.num_parents = ARRAY_SIZE(dirwolf_gcc_parent_names_usb_mp0),
				},
	[GCC_USB3_MP0_CLKREF_CLK] = {.name = "gcc_usb3_mp0_clkref_en",},
	[GCC_USB3_MP_PHY_COM_AUX_CLK] = {.name = "gcc_usb3_mp_phy_com_aux_clk",},
	[GCC_USB3_MP_PHY_PIPE_1_CLK] = {.name = "gcc_usb3_mp_phy_pipe_1_clk",},
	[GCC_USB3_MP_PHY_PIPE_1_CLK_SRC] = {
				.name = "gcc_usb3_mp_phy_pipe_1_clk_src",
				.parent_names = dirwolf_gcc_parent_names_usb_mp1,
				.num_parents = ARRAY_SIZE(dirwolf_gcc_parent_names_usb_mp1),
				},
	[GCC_USB3_MP1_CLKREF_CLK] = {.name = "gcc_usb3_mp1_clkref_en",},
	[GCC_SDCC2_AHB_CLK] = {.name = "gcc_sdcc2_ahb_clk",},
	[GCC_SDCC2_APPS_CLK] = {.name = "gcc_sdcc2_apps_clk",},
	[GCC_PCIE_2A_PIPE_CLK] = { .name = "gcc_pcie_2a_pipe_clk",},
	[GCC_PCIE_2A_PIPE_CLK_SRC] = {
				.name = "gcc_pcie_2a_pipe_clk_src",
				.parent_names = dirwolf_gcc_parent_names_0,
				.num_parents = ARRAY_SIZE(dirwolf_gcc_parent_names_0),
				},
	[GCC_PCIE_2A_AUX_CLK] = {.name = "gcc_pcie_2a_aux_clk",},
	[GCC_PCIE_2A_CFG_AHB_CLK] = {.name = "gcc_pcie_2a_cfg_ahb_clk",},
	[GCC_PCIE_2A_MSTR_AXI_CLK] = {.name = "gcc_pcie_2a_mstr_axi_clk",},
	[GCC_PCIE_2A_SLV_AXI_CLK] = {.name = "gcc_pcie_2a_slv_axi_clk",},
	[GCC_PCIE_2A2B_CLKREF_CLK] = {.name = "gcc_pcie_2a2b_clkref_en",},
	[GCC_PCIE_2A_SLV_Q2A_AXI_CLK] = {.name = "gcc_pcie_2a_slv_q2a_axi_clk",},
	[GCC_PCIE2A_PHY_RCHNG_CLK] = {.name = "gcc_pcie2a_phy_rchng_clk",},
	[GCC_PCIE_2A_PIPEDIV2_CLK] = {.name = "gcc_pcie_2a_pipediv2_clk",},
	[GCC_PCIE_2B_PIPE_CLK] = { .name = "gcc_pcie_2b_pipe_clk",},
	[GCC_PCIE_2B_PIPE_CLK_SRC] = {
				.name = "gcc_pcie_2b_pipe_clk_src",
				.parent_names = dirwolf_gcc_parent_names_1,
				.num_parents = ARRAY_SIZE(dirwolf_gcc_parent_names_1),
				},
	[GCC_PCIE_2B_AUX_CLK] = {.name = "gcc_pcie_2b_aux_clk",},
	[GCC_PCIE_2B_CFG_AHB_CLK] = {.name = "gcc_pcie_2b_cfg_ahb_clk",},
	[GCC_PCIE_2B_MSTR_AXI_CLK] = {.name = "gcc_pcie_2b_mstr_axi_clk",},
	[GCC_PCIE_2B_SLV_AXI_CLK] = {.name = "gcc_pcie_2b_slv_axi_clk",},
	[GCC_PCIE_2B_SLV_Q2A_AXI_CLK] = {.name = "gcc_pcie_2b_slv_q2a_axi_clk",},
	[GCC_PCIE2B_PHY_RCHNG_CLK] = {.name = "gcc_pcie2b_phy_rchng_clk",},
	[GCC_PCIE_2B_PIPEDIV2_CLK] = {.name = "gcc_pcie_2b_pipediv2_clk",},
	[GCC_PCIE_3A_PIPE_CLK] = { .name = "gcc_pcie_3a_pipe_clk",},
	[GCC_PCIE_3A_PIPE_CLK_SRC] = {
				.name = "gcc_pcie_3a_pipe_clk_src",
				.parent_names = dirwolf_gcc_parent_names_2,
				.num_parents = ARRAY_SIZE(dirwolf_gcc_parent_names_2),
				},
	[GCC_PCIE_3A_AUX_CLK] = {.name = "gcc_pcie_3a_aux_clk",},
	[GCC_PCIE_3A_CFG_AHB_CLK] = {.name = "gcc_pcie_3a_cfg_ahb_clk",},
	[GCC_PCIE_3A_MSTR_AXI_CLK] = {.name = "gcc_pcie_3a_mstr_axi_clk",},
	[GCC_PCIE_3A3B_CLKREF_CLK] = {.name = "gcc_pcie_3a3b_clkref_en",},
	[GCC_PCIE_3A_SLV_AXI_CLK] = {.name = "gcc_pcie_3a_slv_axi_clk",},
	[GCC_PCIE_3A_SLV_Q2A_AXI_CLK] = {.name = "gcc_pcie_3a_slv_q2a_axi_clk",},
	[GCC_PCIE3A_PHY_RCHNG_CLK] = {.name = "gcc_pcie3a_phy_rchng_clk",},
	[GCC_PCIE_3A_PIPEDIV2_CLK] = {.name = "gcc_pcie_3a_pipediv2_clk",},
	[GCC_PCIE_3B_PIPE_CLK] = { .name = "gcc_pcie_3b_pipe_clk",},
	[GCC_PCIE_3B_PIPE_CLK_SRC] = {
				.name = "gcc_pcie_3b_pipe_clk_src",
				.parent_names = dirwolf_gcc_parent_names_3,
				.num_parents = ARRAY_SIZE(dirwolf_gcc_parent_names_3),
				},
	[GCC_PCIE_3B_AUX_CLK] = {.name = "gcc_pcie_3b_aux_clk",},
	[GCC_PCIE_3B_CFG_AHB_CLK] = {.name = "gcc_pcie_3b_cfg_ahb_clk",},
	[GCC_PCIE_3B_MSTR_AXI_CLK] = {.name = "gcc_pcie_3b_mstr_axi_clk",},
	[GCC_PCIE_3B_SLV_AXI_CLK] = {.name = "gcc_pcie_3b_slv_axi_clk",},
	[GCC_PCIE_3B_SLV_Q2A_AXI_CLK] = {.name = "gcc_pcie_3b_slv_q2a_axi_clk",},
	[GCC_PCIE3B_PHY_RCHNG_CLK] = {.name = "gcc_pcie3b_phy_rchng_clk",},
	[GCC_PCIE_3B_PIPEDIV2_CLK] = {.name = "gcc_pcie_3b_pipediv2_clk",},
	[GCC_PCIE_4_PIPE_CLK] = {.name = "gcc_pcie_4_pipe_clk",},
	[GCC_PCIE_4_PIPE_CLK_SRC] = {
				.name = "gcc_pcie_4_pipe_clk_src",
				.parent_names = dirwolf_gcc_parent_names_4,
				.num_parents = ARRAY_SIZE(dirwolf_gcc_parent_names_4),
				},
	[GCC_PCIE_4_AUX_CLK] = {.name = "gcc_pcie_4_aux_clk",},
	[GCC_PCIE_4_CFG_AHB_CLK] = {.name = "gcc_pcie_4_cfg_ahb_clk",},
	[GCC_PCIE_4_MSTR_AXI_CLK] = {.name = "gcc_pcie_4_mstr_axi_clk",},
	[GCC_PCIE_4_SLV_AXI_CLK] = {.name = "gcc_pcie_4_slv_axi_clk",},
	[GCC_PCIE_4_CLKREF_CLK] = {.name = "gcc_pcie_4_clkref_en",},
	[GCC_PCIE_4_SLV_Q2A_AXI_CLK] = {.name = "gcc_pcie_4_slv_q2a_axi_clk",},
	[GCC_PCIE4_PHY_RCHNG_CLK] = {.name = "gcc_pcie4_phy_rchng_clk",},
	[GCC_DDRSS_PCIE_SF_TBU_CLK] = {.name = "gcc_ddrss_pcie_sf_tbu_clk",},
	[GCC_AGGRE_NOC_PCIE_4_AXI_CLK] = {.name = "gcc_aggre_noc_pcie_4_axi_clk",},
	[GCC_AGGRE_NOC_PCIE_SOUTH_SF_AXI_CLK] = {.name = "gcc_aggre_noc_pcie_south_sf_axi_clk",},
	[GCC_CNOC_PCIE4_QX_CLK] = {.name = "gcc_cnoc_pcie4_qx_clk",},
	[GCC_PCIE_4_PIPEDIV2_CLK] = {.name = "gcc_pcie_4_pipediv2_clk",},
	[GCC_UFS_1_CARD_CLKREF_CLK] = {.name = "gcc_ufs_1_card_clkref_en",},
	[GCC_UFS_CARD_PHY_AUX_CLK] = {.name = "gcc_ufs_card_phy_aux_clk",},
	[GCC_UFS_REF_CLKREF_CLK] = {.name = "gcc_ufs_ref_clkref_en",},
	[GCC_UFS_CARD_AXI_CLK] = {.name = "gcc_ufs_card_axi_clk",},
	[GCC_AGGRE_UFS_CARD_AXI_CLK] = {.name = "gcc_aggre_ufs_card_axi_clk",},
	[GCC_UFS_CARD_AHB_CLK] = {.name = "gcc_ufs_card_ahb_clk",},
	[GCC_UFS_CARD_UNIPRO_CORE_CLK] = {.name = "gcc_ufs_card_unipro_core_clk",},
	[GCC_UFS_CARD_ICE_CORE_CLK] = {.name = "gcc_ufs_card_ice_core_clk",},
	[GCC_UFS_CARD_TX_SYMBOL_0_CLK] = {.name = "gcc_ufs_card_tx_symbol_0_clk",},
	[GCC_UFS_CARD_RX_SYMBOL_0_CLK] = {.name = "gcc_ufs_card_rx_symbol_0_clk",},
	[GCC_UFS_CARD_RX_SYMBOL_1_CLK] = {.name = "gcc_ufs_card_rx_symbol_1_clk",},
	[GCC_EMAC1_AXI_CLK] = {.name = "gcc_emac1_axi_clk",},
	[GCC_EMAC1_SLV_AHB_CLK] = {.name = "gcc_emac1_slv_ahb_clk",},
	[GCC_EMAC1_PTP_CLK] = {.name = "gcc_emac1_ptp_clk",},
	[GCC_EMAC1_RGMII_CLK] = {.name = "gcc_emac1_rgmii_clk",},
};

static const char * const direwolf_gcc_virtio_resets[] = {
	[GCC_QUSB2PHY_PRIM_BCR] = "gcc_qusb2phy_prim_bcr",
	[GCC_QUSB2PHY_SEC_BCR] = "gcc_qusb2phy_sec_bcr",
	[GCC_USB2_PHY_SEC_BCR] = "gcc_usb2_phy_sec_bcr",
	[GCC_USB30_PRIM_BCR] = "gcc_usb30_prim_master_clk",
	[GCC_USB30_SEC_BCR] = "gcc_usb30_sec_master_clk",
	[GCC_USB30_MP_BCR] = "gcc_usb30_mp_master_clk",
	[GCC_QUSB2PHY_HS0_MP_BCR] = "gcc_qusb2phy_hs0_mp_bcr",
	[GCC_QUSB2PHY_HS1_MP_BCR] = "gcc_qusb2phy_hs1_mp_bcr",
	[GCC_QUSB2PHY_HS2_MP_BCR] = "gcc_qusb2phy_hs2_mp_bcr",
	[GCC_QUSB2PHY_HS3_MP_BCR] = "gcc_qusb2phy_hs3_mp_bcr",
	[GCC_USB4_DP_PHY_PRIM_BCR] = "gcc_usb4_dp_phy_prim_bcr",
	[GCC_USB3_PHY_PRIM_BCR] = "gcc_usb3_phy_prim_bcr",
	[GCC_USB4_1_DP_PHY_PRIM_BCR] = "gcc_usb4_1_dp_phy_prim_bcr",
	[GCC_USB3_PHY_SEC_BCR] = "gcc_usb3_phy_sec_bcr",
	[GCC_USB3_UNIPHY_MP0_BCR] = "gcc_usb3_uniphy_mp0_bcr",
	[GCC_USB3UNIPHY_PHY_MP0_BCR] = "gcc_usb3uniphy_phy_mp0_bcr",
	[GCC_USB3_UNIPHY_MP1_BCR] = "gcc_usb3_uniphy_mp1_bcr",
	[GCC_USB3UNIPHY_PHY_MP1_BCR] = "gcc_usb3uniphy_phy_mp1_bcr",
	[GCC_PCIE_2A_BCR] = "gcc_pcie_2a_bcr",
	[GCC_PCIE_2A_PHY_BCR] = "gcc_pcie_2a_phy_bcr",
	[GCC_PCIE_2A_PHY_NOCSR_COM_PHY_BCR] = "gcc_pcie_2a_phy_nocsr_com_phy_bcr",
	[GCC_PCIE_2B_BCR] = "gcc_pcie_2b_bcr",
	[GCC_PCIE_2B_PHY_BCR] = "gcc_pcie_2b_phy_bcr",
	[GCC_PCIE_2B_PHY_NOCSR_COM_PHY_BCR] = "gcc_pcie_2b_phy_nocsr_com_phy_bcr",
	[GCC_PCIE_3A_BCR] = "gcc_pcie_3a_bcr",
	[GCC_PCIE_3A_PHY_BCR] = "gcc_pcie_3a_phy_bcr",
	[GCC_PCIE_3A_PHY_NOCSR_COM_PHY_BCR] = "gcc_pcie_3a_phy_nocsr_com_phy_bcr",
	[GCC_PCIE_3B_BCR] = "gcc_pcie_3b_bcr",
	[GCC_PCIE_3B_PHY_BCR] = "gcc_pcie_3b_phy_bcr",
	[GCC_PCIE_3B_PHY_NOCSR_COM_PHY_BCR] = "gcc_pcie_3b_phy_nocsr_com_phy_bcr",
	[GCC_PCIE_4_BCR] = "gcc_pcie_4_bcr",
	[GCC_PCIE_4_PHY_BCR] = "gcc_pcie_4_phy_bcr",
	[GCC_PCIE_4_PHY_NOCSR_COM_PHY_BCR] = "gcc_pcie_4_phy_nocsr_com_phy_bcr",
	[GCC_UFS_CARD_BCR] = "gcc_ufs_card_bcr",
};

const struct clk_virtio_desc clk_virtio_direwolf_gcc = {
	.clks = direwolf_gcc_virtio_clocks,
	.num_clks = ARRAY_SIZE(direwolf_gcc_virtio_clocks),
	.reset_names = direwolf_gcc_virtio_resets,
	.num_resets = ARRAY_SIZE(direwolf_gcc_virtio_resets),
};
EXPORT_SYMBOL(clk_virtio_direwolf_gcc);

MODULE_LICENSE("GPL");
