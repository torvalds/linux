// SPDX-License-Identifier: GPL-2.0-only
//
// Copyright (c) 2021 MediaTek Inc.
// Author: Chun-Jie Chen <chun-jie.chen@mediatek.com>

#include "clk-gate.h"
#include "clk-mtk.h"

#include <dt-bindings/clock/mt8195-clk.h>
#include <linux/clk-provider.h>
#include <linux/platform_device.h>

static const struct mtk_gate_regs peri_ao_cg_regs = {
	.set_ofs = 0x10,
	.clr_ofs = 0x14,
	.sta_ofs = 0x18,
};

#define GATE_PERI_AO(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &peri_ao_cg_regs, _shift, &mtk_clk_gate_ops_setclr)

static const struct mtk_gate peri_ao_clks[] = {
	GATE_PERI_AO(CLK_PERI_AO_ETHERNET, "peri_ao_ethernet", "top_axi", 0),
	GATE_PERI_AO(CLK_PERI_AO_ETHERNET_BUS, "peri_ao_ethernet_bus", "top_axi", 1),
	GATE_PERI_AO(CLK_PERI_AO_FLASHIF_BUS, "peri_ao_flashif_bus", "top_axi", 3),
	GATE_PERI_AO(CLK_PERI_AO_FLASHIF_FLASH, "peri_ao_flashif_flash", "top_spinor", 5),
	GATE_PERI_AO(CLK_PERI_AO_SSUSB_1P_BUS, "peri_ao_ssusb_1p_bus", "top_usb_top_1p", 7),
	GATE_PERI_AO(CLK_PERI_AO_SSUSB_1P_XHCI, "peri_ao_ssusb_1p_xhci", "top_ssusb_xhci_1p", 8),
	GATE_PERI_AO(CLK_PERI_AO_SSUSB_2P_BUS, "peri_ao_ssusb_2p_bus", "top_usb_top_2p", 9),
	GATE_PERI_AO(CLK_PERI_AO_SSUSB_2P_XHCI, "peri_ao_ssusb_2p_xhci", "top_ssusb_xhci_2p", 10),
	GATE_PERI_AO(CLK_PERI_AO_SSUSB_3P_BUS, "peri_ao_ssusb_3p_bus", "top_usb_top_3p", 11),
	GATE_PERI_AO(CLK_PERI_AO_SSUSB_3P_XHCI, "peri_ao_ssusb_3p_xhci", "top_ssusb_xhci_3p", 12),
	GATE_PERI_AO(CLK_PERI_AO_SPINFI, "peri_ao_spinfi", "top_spinfi_bclk", 15),
	GATE_PERI_AO(CLK_PERI_AO_ETHERNET_MAC, "peri_ao_ethernet_mac", "top_snps_eth_250m", 16),
	GATE_PERI_AO(CLK_PERI_AO_NFI_H, "peri_ao_nfi_h", "top_axi", 19),
	GATE_PERI_AO(CLK_PERI_AO_FNFI1X, "peri_ao_fnfi1x", "top_nfi1x", 20),
	GATE_PERI_AO(CLK_PERI_AO_PCIE_P0_MEM, "peri_ao_pcie_p0_mem", "mem_466m", 24),
	GATE_PERI_AO(CLK_PERI_AO_PCIE_P1_MEM, "peri_ao_pcie_p1_mem", "mem_466m", 25),
};

static const struct mtk_clk_desc peri_ao_desc = {
	.clks = peri_ao_clks,
	.num_clks = ARRAY_SIZE(peri_ao_clks),
};

static const struct of_device_id of_match_clk_mt8195_peri_ao[] = {
	{
		.compatible = "mediatek,mt8195-pericfg_ao",
		.data = &peri_ao_desc,
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, of_match_clk_mt8195_peri_ao);

static struct platform_driver clk_mt8195_peri_ao_drv = {
	.probe = mtk_clk_simple_probe,
	.remove = mtk_clk_simple_remove,
	.driver = {
		.name = "clk-mt8195-peri_ao",
		.of_match_table = of_match_clk_mt8195_peri_ao,
	},
};
module_platform_driver(clk_mt8195_peri_ao_drv);
MODULE_LICENSE("GPL");
