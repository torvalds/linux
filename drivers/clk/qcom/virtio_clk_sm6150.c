// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <dt-bindings/clock/qcom,gcc-sm6150.h>
#include <dt-bindings/clock/qcom,scc-sm6150.h>
#include "virtio_clk_common.h"

static const struct virtio_clk_init_data sm6150_gcc_virtio_clocks[] = {
	[GCC_QUPV3_WRAP0_S0_CLK] = {.name = "gcc_qupv3_wrap0_s0_clk",},
	[GCC_QUPV3_WRAP0_S1_CLK] = {.name = "gcc_qupv3_wrap0_s1_clk",},
	[GCC_QUPV3_WRAP0_S2_CLK] = {.name = "gcc_qupv3_wrap0_s2_clk",},
	[GCC_QUPV3_WRAP0_S3_CLK] = {.name = "gcc_qupv3_wrap0_s3_clk",},
	[GCC_QUPV3_WRAP0_S4_CLK] = {.name = "gcc_qupv3_wrap0_s4_clk",},
	[GCC_QUPV3_WRAP0_S5_CLK] = {.name = "gcc_qupv3_wrap0_s5_clk",},
	[GCC_QUPV3_WRAP1_S0_CLK] = {.name = "gcc_qupv3_wrap1_s0_clk",},
	[GCC_QUPV3_WRAP1_S1_CLK] = {.name = "gcc_qupv3_wrap1_s1_clk",},
	[GCC_QUPV3_WRAP1_S2_CLK] = {.name = "gcc_qupv3_wrap1_s2_clk",},
	[GCC_QUPV3_WRAP1_S3_CLK] = {.name = "gcc_qupv3_wrap1_s3_clk",},
	[GCC_QUPV3_WRAP1_S4_CLK] = {.name = "gcc_qupv3_wrap1_s4_clk",},
	[GCC_QUPV3_WRAP1_S5_CLK] = {.name = "gcc_qupv3_wrap1_s5_clk",},
	[GCC_QUPV3_WRAP_0_M_AHB_CLK] = {.name = "gcc_qupv3_wrap_0_m_ahb_clk",},
	[GCC_QUPV3_WRAP_0_S_AHB_CLK] = {.name = "gcc_qupv3_wrap_0_s_ahb_clk",},
	[GCC_QUPV3_WRAP_1_M_AHB_CLK] = {.name = "gcc_qupv3_wrap_1_m_ahb_clk",},
	[GCC_QUPV3_WRAP_1_S_AHB_CLK] = {.name = "gcc_qupv3_wrap_1_s_ahb_clk",},
	[GCC_USB30_PRIM_MASTER_CLK] = {.name = "gcc_usb30_prim_master_clk",},
	[GCC_CFG_NOC_USB3_PRIM_AXI_CLK] = {.name = "gcc_cfg_noc_usb3_prim_axi_clk",},
	[GCC_AGGRE_USB3_PRIM_AXI_CLK] = {.name = "gcc_aggre_usb3_prim_axi_clk",},
	[GCC_USB30_PRIM_MOCK_UTMI_CLK] = {.name = "gcc_usb30_prim_mock_utmi_clk",},
	[GCC_USB30_PRIM_SLEEP_CLK] = {.name = "gcc_usb30_prim_sleep_clk",},
	[GCC_USB3_SEC_CLKREF_CLK] = {.name = "gcc_usb3_sec_clkref_en",},
	[GCC_USB3_PRIM_PHY_AUX_CLK] = {.name = "gcc_usb3_prim_phy_aux_clk",},
	[GCC_USB3_PRIM_PHY_PIPE_CLK] = {.name = "gcc_usb3_prim_phy_pipe_clk",},
	[GCC_USB3_PRIM_CLKREF_CLK] = {.name = "gcc_usb3_prim_clkref_en",},
	[GCC_USB3_PRIM_PHY_COM_AUX_CLK] = {.name = "gcc_usb3_prim_phy_com_aux_clk",},
	[GCC_AHB2PHY_WEST_CLK] = {.name = "gcc_ahb2phy_west_clk",},
	[GCC_USB20_SEC_MASTER_CLK] = {.name = "gcc_usb20_sec_master_clk",},
	[GCC_CFG_NOC_USB2_SEC_AXI_CLK] = {.name = "gcc_cfg_noc_usb2_sec_axi_clk",},
	[GCC_AGGRE_USB2_SEC_AXI_CLK] = {.name = "gcc_aggre_usb2_sec_axi_clk",},
	[GCC_USB20_SEC_SLEEP_CLK] = {.name = "gcc_usb20_sec_sleep_clk",},
	[GCC_USB20_SEC_MOCK_UTMI_CLK] = {.name = "gcc_usb20_sec_mock_utmi_clk",},
	[GCC_PCIE_0_PIPE_CLK] = {.name = "gcc_pcie_0_pipe_clk",},
	[GCC_PCIE_0_AUX_CLK] = {.name = "gcc_pcie_0_aux_clk",},
	[GCC_PCIE_0_CFG_AHB_CLK] = {.name = "gcc_pcie_0_cfg_ahb_clk",},
	[GCC_PCIE_0_MSTR_AXI_CLK] = {.name = "gcc_pcie_0_mstr_axi_clk",},
	[GCC_PCIE_0_SLV_AXI_CLK] = {.name = "gcc_pcie_0_slv_axi_clk",},
	[GCC_PCIE_0_CLKREF_CLK] = {.name = "gcc_pcie_0_clkref_en",},
	[GCC_PCIE_0_SLV_Q2A_AXI_CLK] = {.name = "gcc_pcie_0_slv_q2a_axi_clk",},
	[GCC_PCIE0_PHY_REFGEN_CLK] = {.name = "gcc_pcie0_phy_refgen_clk",},
	[GCC_PCIE_PHY_AUX_CLK] = {.name = "gcc_pcie_phy_aux_clk",},
	[GCC_SDCC2_AHB_CLK] = {.name = "gcc_sdcc2_ahb_clk",},
	[GCC_SDCC2_APPS_CLK] = {.name = "gcc_sdcc2_apps_clk",},
	[GCC_PRNG_AHB_CLK] = {.name = "gcc_prng_ahb_clk",},
	[GCC_SDR_CORE_CLK] = {.name = "gcc_sdr_core_clk",},
	[GCC_SDR_WR0_MEM_CLK] = {.name = "gcc_sdr_wr0_mem_clk",},
	[GCC_SDR_WR1_MEM_CLK] = {.name = "gcc_sdr_wr1_mem_clk",},
	[GCC_SDR_WR2_MEM_CLK] = {.name = "gcc_sdr_wr2_mem_clk",},
	[GCC_SDR_CSR_HCLK] = {.name = "gcc_sdr_csr_hclk",},
	[GCC_SDR_PRI_MI2S_CLK] = {.name = "gcc_sdr_pri_mi2s_clk",},
	[GCC_SDR_SEC_MI2S_CLK] = {.name = "gcc_sdr_sec_mi2s_clk",},
};

static const char * const sm6150_gcc_virtio_resets[] = {
	[GCC_QUSB2PHY_PRIM_BCR] = "gcc_qusb2phy_prim_bcr",
	[GCC_QUSB2PHY_SEC_BCR] = "gcc_qusb2phy_sec_bcr",
	[GCC_USB30_PRIM_BCR] = "gcc_usb30_prim_master_clk",
	[GCC_USB2_PHY_SEC_BCR] = "gcc_usb2_phy_sec_bcr",
	[GCC_USB3_DP_PHY_SEC_BCR] = "gcc_usb3_dp_phy_sec_bcr",
	[GCC_USB3PHY_PHY_SEC_BCR] = "gcc_usb3phy_phy_sec_bcr",
	[GCC_USB20_SEC_BCR] = "gcc_usb20_sec_master_clk",
	[GCC_USB3_PHY_PRIM_SP0_BCR] = "gcc_usb3_phy_prim_sp0_bcr",
	[GCC_USB3PHY_PHY_PRIM_SP0_BCR] = "gcc_usb3phy_phy_prim_sp0_bcr",
	[GCC_PCIE_0_BCR] = "gcc_pcie_0_mstr_axi_clk",
	[GCC_PCIE_0_PHY_BCR] = "gcc_pcie_0_phy_bcr",
};

const struct clk_virtio_desc clk_virtio_sm6150_gcc = {
	.clks = sm6150_gcc_virtio_clocks,
	.num_clks = ARRAY_SIZE(sm6150_gcc_virtio_clocks),
	.reset_names = sm6150_gcc_virtio_resets,
	.num_resets = ARRAY_SIZE(sm6150_gcc_virtio_resets),
};
EXPORT_SYMBOL(clk_virtio_sm6150_gcc);

static const struct virtio_clk_init_data sm6150_scc_virtio_clocks[] = {
	[SCC_QUPV3_SE0_CLK] = {.name = "scc_qupv3_se0_clk",},
	[SCC_QUPV3_SE1_CLK] = {.name = "scc_qupv3_se1_clk",},
	[SCC_QUPV3_SE2_CLK] = {.name = "scc_qupv3_se2_clk",},
	[SCC_QUPV3_SE3_CLK] = {.name = "scc_qupv3_se3_clk",},
	[SCC_QUPV3_SE4_CLK] = {.name = "scc_qupv3_se4_clk",},
	[SCC_QUPV3_SE5_CLK] = {.name = "scc_qupv3_se5_clk",},
	[SCC_QUPV3_M_HCLK_CLK] = {.name = "scc_qupv3_m_hclk_clk",},
	[SCC_QUPV3_S_HCLK_CLK] = {.name = "scc_qupv3_s_hclk_clk",},
};

const struct clk_virtio_desc clk_virtio_sm6150_scc = {
	.clks = sm6150_scc_virtio_clocks,
	.num_clks = ARRAY_SIZE(sm6150_scc_virtio_clocks),
};
EXPORT_SYMBOL(clk_virtio_sm6150_scc);

MODULE_LICENSE("GPL");
