// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025 MediaTek Inc.
 *                    Guangjie Song <guangjie.song@mediatek.com>
 * Copyright (c) 2025 Collabora Ltd.
 *                    Laura Nao <laura.nao@collabora.com>
 */
#include <dt-bindings/clock/mediatek,mt8196-clock.h>

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

#include "clk-gate.h"
#include "clk-mtk.h"

static const struct mtk_gate_regs ovl0_cg_regs = {
	.set_ofs = 0x104,
	.clr_ofs = 0x108,
	.sta_ofs = 0x100,
};

static const struct mtk_gate_regs ovl0_hwv_regs = {
	.set_ofs = 0x0060,
	.clr_ofs = 0x0064,
	.sta_ofs = 0x2c30,
};

static const struct mtk_gate_regs ovl1_cg_regs = {
	.set_ofs = 0x114,
	.clr_ofs = 0x118,
	.sta_ofs = 0x110,
};

static const struct mtk_gate_regs ovl1_hwv_regs = {
	.set_ofs = 0x0068,
	.clr_ofs = 0x006c,
	.sta_ofs = 0x2c34,
};

#define GATE_HWV_OVL0(_id, _name, _parent, _shift) {	\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _parent,			\
		.regs = &ovl0_cg_regs,			\
		.hwv_regs = &ovl0_hwv_regs,		\
		.shift = _shift,			\
		.ops = &mtk_clk_gate_hwv_ops_setclr,	\
		.flags = CLK_OPS_PARENT_ENABLE,		\
	}

#define GATE_HWV_OVL1(_id, _name, _parent, _shift) {	\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _parent,			\
		.regs = &ovl1_cg_regs,			\
		.hwv_regs = &ovl1_hwv_regs,		\
		.shift = _shift,			\
		.ops = &mtk_clk_gate_hwv_ops_setclr,	\
		.flags = CLK_OPS_PARENT_ENABLE,		\
	}

static const struct mtk_gate ovl_clks[] = {
	/* OVL0 */
	GATE_HWV_OVL0(CLK_OVLSYS_CONFIG, "ovlsys_config", "disp", 0),
	GATE_HWV_OVL0(CLK_OVL_FAKE_ENG0, "ovl_fake_eng0", "disp", 1),
	GATE_HWV_OVL0(CLK_OVL_FAKE_ENG1, "ovl_fake_eng1", "disp", 2),
	GATE_HWV_OVL0(CLK_OVL_MUTEX0, "ovl_mutex0", "disp", 3),
	GATE_HWV_OVL0(CLK_OVL_EXDMA0, "ovl_exdma0", "disp", 4),
	GATE_HWV_OVL0(CLK_OVL_EXDMA1, "ovl_exdma1", "disp", 5),
	GATE_HWV_OVL0(CLK_OVL_EXDMA2, "ovl_exdma2", "disp", 6),
	GATE_HWV_OVL0(CLK_OVL_EXDMA3, "ovl_exdma3", "disp", 7),
	GATE_HWV_OVL0(CLK_OVL_EXDMA4, "ovl_exdma4", "disp", 8),
	GATE_HWV_OVL0(CLK_OVL_EXDMA5, "ovl_exdma5", "disp", 9),
	GATE_HWV_OVL0(CLK_OVL_EXDMA6, "ovl_exdma6", "disp", 10),
	GATE_HWV_OVL0(CLK_OVL_EXDMA7, "ovl_exdma7", "disp", 11),
	GATE_HWV_OVL0(CLK_OVL_EXDMA8, "ovl_exdma8", "disp", 12),
	GATE_HWV_OVL0(CLK_OVL_EXDMA9, "ovl_exdma9", "disp", 13),
	GATE_HWV_OVL0(CLK_OVL_BLENDER0, "ovl_blender0", "disp", 14),
	GATE_HWV_OVL0(CLK_OVL_BLENDER1, "ovl_blender1", "disp", 15),
	GATE_HWV_OVL0(CLK_OVL_BLENDER2, "ovl_blender2", "disp", 16),
	GATE_HWV_OVL0(CLK_OVL_BLENDER3, "ovl_blender3", "disp", 17),
	GATE_HWV_OVL0(CLK_OVL_BLENDER4, "ovl_blender4", "disp", 18),
	GATE_HWV_OVL0(CLK_OVL_BLENDER5, "ovl_blender5", "disp", 19),
	GATE_HWV_OVL0(CLK_OVL_BLENDER6, "ovl_blender6", "disp", 20),
	GATE_HWV_OVL0(CLK_OVL_BLENDER7, "ovl_blender7", "disp", 21),
	GATE_HWV_OVL0(CLK_OVL_BLENDER8, "ovl_blender8", "disp", 22),
	GATE_HWV_OVL0(CLK_OVL_BLENDER9, "ovl_blender9", "disp", 23),
	GATE_HWV_OVL0(CLK_OVL_OUTPROC0, "ovl_outproc0", "disp", 24),
	GATE_HWV_OVL0(CLK_OVL_OUTPROC1, "ovl_outproc1", "disp", 25),
	GATE_HWV_OVL0(CLK_OVL_OUTPROC2, "ovl_outproc2", "disp", 26),
	GATE_HWV_OVL0(CLK_OVL_OUTPROC3, "ovl_outproc3", "disp", 27),
	GATE_HWV_OVL0(CLK_OVL_OUTPROC4, "ovl_outproc4", "disp", 28),
	GATE_HWV_OVL0(CLK_OVL_OUTPROC5, "ovl_outproc5", "disp", 29),
	GATE_HWV_OVL0(CLK_OVL_MDP_RSZ0, "ovl_mdp_rsz0", "disp", 30),
	GATE_HWV_OVL0(CLK_OVL_MDP_RSZ1, "ovl_mdp_rsz1", "disp", 31),
	/* OVL1 */
	GATE_HWV_OVL1(CLK_OVL_DISP_WDMA0, "ovl_disp_wdma0", "disp", 0),
	GATE_HWV_OVL1(CLK_OVL_DISP_WDMA1, "ovl_disp_wdma1", "disp", 1),
	GATE_HWV_OVL1(CLK_OVL_UFBC_WDMA0, "ovl_ufbc_wdma0", "disp", 2),
	GATE_HWV_OVL1(CLK_OVL_MDP_RDMA0, "ovl_mdp_rdma0", "disp", 3),
	GATE_HWV_OVL1(CLK_OVL_MDP_RDMA1, "ovl_mdp_rdma1", "disp", 4),
	GATE_HWV_OVL1(CLK_OVL_BWM0, "ovl_bwm0", "disp", 5),
	GATE_HWV_OVL1(CLK_OVL_DLI0, "ovl_dli0", "disp", 6),
	GATE_HWV_OVL1(CLK_OVL_DLI1, "ovl_dli1", "disp", 7),
	GATE_HWV_OVL1(CLK_OVL_DLI2, "ovl_dli2", "disp", 8),
	GATE_HWV_OVL1(CLK_OVL_DLI3, "ovl_dli3", "disp", 9),
	GATE_HWV_OVL1(CLK_OVL_DLI4, "ovl_dli4", "disp", 10),
	GATE_HWV_OVL1(CLK_OVL_DLI5, "ovl_dli5", "disp", 11),
	GATE_HWV_OVL1(CLK_OVL_DLI6, "ovl_dli6", "disp", 12),
	GATE_HWV_OVL1(CLK_OVL_DLI7, "ovl_dli7", "disp", 13),
	GATE_HWV_OVL1(CLK_OVL_DLI8, "ovl_dli8", "disp", 14),
	GATE_HWV_OVL1(CLK_OVL_DLO0, "ovl_dlo0", "disp", 15),
	GATE_HWV_OVL1(CLK_OVL_DLO1, "ovl_dlo1", "disp", 16),
	GATE_HWV_OVL1(CLK_OVL_DLO2, "ovl_dlo2", "disp", 17),
	GATE_HWV_OVL1(CLK_OVL_DLO3, "ovl_dlo3", "disp", 18),
	GATE_HWV_OVL1(CLK_OVL_DLO4, "ovl_dlo4", "disp", 19),
	GATE_HWV_OVL1(CLK_OVL_DLO5, "ovl_dlo5", "disp", 20),
	GATE_HWV_OVL1(CLK_OVL_DLO6, "ovl_dlo6", "disp", 21),
	GATE_HWV_OVL1(CLK_OVL_DLO7, "ovl_dlo7", "disp", 22),
	GATE_HWV_OVL1(CLK_OVL_DLO8, "ovl_dlo8", "disp", 23),
	GATE_HWV_OVL1(CLK_OVL_DLO9, "ovl_dlo9", "disp", 24),
	GATE_HWV_OVL1(CLK_OVL_DLO10, "ovl_dlo10", "disp", 25),
	GATE_HWV_OVL1(CLK_OVL_DLO11, "ovl_dlo11", "disp", 26),
	GATE_HWV_OVL1(CLK_OVL_DLO12, "ovl_dlo12", "disp", 27),
	GATE_HWV_OVL1(CLK_OVLSYS_RELAY0, "ovlsys_relay0", "disp", 28),
	GATE_HWV_OVL1(CLK_OVL_INLINEROT0, "ovl_inlinerot0", "disp", 29),
	GATE_HWV_OVL1(CLK_OVL_SMI, "ovl_smi", "disp", 30),
};

static const struct mtk_clk_desc ovl_mcd = {
	.clks = ovl_clks,
	.num_clks = ARRAY_SIZE(ovl_clks),
};

static const struct platform_device_id clk_mt8196_ovl0_id_table[] = {
	{ .name = "clk-mt8196-ovl0", .driver_data = (kernel_ulong_t)&ovl_mcd },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, clk_mt8196_ovl0_id_table);

static struct platform_driver clk_mt8196_ovl0_drv = {
	.probe = mtk_clk_pdev_probe,
	.remove = mtk_clk_pdev_remove,
	.driver = {
		.name = "clk-mt8196-ovl0",
	},
	.id_table = clk_mt8196_ovl0_id_table,
};
module_platform_driver(clk_mt8196_ovl0_drv);

MODULE_DESCRIPTION("MediaTek MT8196 ovl0 clocks driver");
MODULE_LICENSE("GPL");
