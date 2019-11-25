// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 * Author: Wendell Lin <wendell.lin@mediatek.com>
 */

#include <linux/clk-provider.h>
#include <linux/platform_device.h>
#include <dt-bindings/clock/mt6779-clk.h>

#include "clk-mtk.h"
#include "clk-gate.h"

static const struct mtk_gate_regs cam_cg_regs = {
	.set_ofs = 0x0004,
	.clr_ofs = 0x0008,
	.sta_ofs = 0x0000,
};

#define GATE_CAM(_id, _name, _parent, _shift)			\
	GATE_MTK(_id, _name, _parent, &cam_cg_regs, _shift,	\
		&mtk_clk_gate_ops_setclr)

static const struct mtk_gate cam_clks[] = {
	GATE_CAM(CLK_CAM_LARB10, "camsys_larb10", "cam_sel", 0),
	GATE_CAM(CLK_CAM_DFP_VAD, "camsys_dfp_vad", "cam_sel", 1),
	GATE_CAM(CLK_CAM_LARB11, "camsys_larb11", "cam_sel", 2),
	GATE_CAM(CLK_CAM_LARB9, "camsys_larb9", "cam_sel", 3),
	GATE_CAM(CLK_CAM_CAM, "camsys_cam", "cam_sel", 6),
	GATE_CAM(CLK_CAM_CAMTG, "camsys_camtg", "cam_sel", 7),
	GATE_CAM(CLK_CAM_SENINF, "camsys_seninf", "cam_sel", 8),
	GATE_CAM(CLK_CAM_CAMSV0, "camsys_camsv0", "cam_sel", 9),
	GATE_CAM(CLK_CAM_CAMSV1, "camsys_camsv1", "cam_sel", 10),
	GATE_CAM(CLK_CAM_CAMSV2, "camsys_camsv2", "cam_sel", 11),
	GATE_CAM(CLK_CAM_CAMSV3, "camsys_camsv3", "cam_sel", 12),
	GATE_CAM(CLK_CAM_CCU, "camsys_ccu", "cam_sel", 13),
	GATE_CAM(CLK_CAM_FAKE_ENG, "camsys_fake_eng", "cam_sel", 14),
};

static const struct of_device_id of_match_clk_mt6779_cam[] = {
	{ .compatible = "mediatek,mt6779-camsys", },
	{}
};

static int clk_mt6779_cam_probe(struct platform_device *pdev)
{
	struct clk_onecell_data *clk_data;
	struct device_node *node = pdev->dev.of_node;

	clk_data = mtk_alloc_clk_data(CLK_CAM_NR_CLK);

	mtk_clk_register_gates(node, cam_clks, ARRAY_SIZE(cam_clks),
			       clk_data);

	return of_clk_add_provider(node, of_clk_src_onecell_get, clk_data);
}

static struct platform_driver clk_mt6779_cam_drv = {
	.probe = clk_mt6779_cam_probe,
	.driver = {
		.name = "clk-mt6779-cam",
		.of_match_table = of_match_clk_mt6779_cam,
	},
};

builtin_platform_driver(clk_mt6779_cam_drv);
