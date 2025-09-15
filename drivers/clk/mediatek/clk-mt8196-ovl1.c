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

static const struct mtk_gate_regs ovl10_cg_regs = {
	.set_ofs = 0x104,
	.clr_ofs = 0x108,
	.sta_ofs = 0x100,
};

static const struct mtk_gate_regs ovl10_hwv_regs = {
	.set_ofs = 0x0050,
	.clr_ofs = 0x0054,
	.sta_ofs = 0x2c28,
};

static const struct mtk_gate_regs ovl11_cg_regs = {
	.set_ofs = 0x114,
	.clr_ofs = 0x118,
	.sta_ofs = 0x110,
};

static const struct mtk_gate_regs ovl11_hwv_regs = {
	.set_ofs = 0x0058,
	.clr_ofs = 0x005c,
	.sta_ofs = 0x2c2c,
};

#define GATE_HWV_OVL10(_id, _name, _parent, _shift) {	\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _parent,			\
		.regs = &ovl10_cg_regs,			\
		.hwv_regs = &ovl10_hwv_regs,		\
		.shift = _shift,			\
		.ops = &mtk_clk_gate_hwv_ops_setclr,	\
		.flags =  CLK_OPS_PARENT_ENABLE,	\
	}

#define GATE_HWV_OVL11(_id, _name, _parent, _shift) {	\
		.id = _id,				\
		.name = _name,				\
		.parent_name = _parent,			\
		.regs = &ovl11_cg_regs,			\
		.hwv_regs = &ovl11_hwv_regs,		\
		.shift = _shift,			\
		.ops = &mtk_clk_gate_hwv_ops_setclr,	\
		.flags = CLK_OPS_PARENT_ENABLE,		\
	}

static const struct mtk_gate ovl1_clks[] = {
	/* OVL10 */
	GATE_HWV_OVL10(CLK_OVL1_OVLSYS_CONFIG, "ovl1_ovlsys_config", "disp", 0),
	GATE_HWV_OVL10(CLK_OVL1_OVL_FAKE_ENG0, "ovl1_ovl_fake_eng0", "disp", 1),
	GATE_HWV_OVL10(CLK_OVL1_OVL_FAKE_ENG1, "ovl1_ovl_fake_eng1", "disp", 2),
	GATE_HWV_OVL10(CLK_OVL1_OVL_MUTEX0, "ovl1_ovl_mutex0", "disp", 3),
	GATE_HWV_OVL10(CLK_OVL1_OVL_EXDMA0, "ovl1_ovl_exdma0", "disp", 4),
	GATE_HWV_OVL10(CLK_OVL1_OVL_EXDMA1, "ovl1_ovl_exdma1", "disp", 5),
	GATE_HWV_OVL10(CLK_OVL1_OVL_EXDMA2, "ovl1_ovl_exdma2", "disp", 6),
	GATE_HWV_OVL10(CLK_OVL1_OVL_EXDMA3, "ovl1_ovl_exdma3", "disp", 7),
	GATE_HWV_OVL10(CLK_OVL1_OVL_EXDMA4, "ovl1_ovl_exdma4", "disp", 8),
	GATE_HWV_OVL10(CLK_OVL1_OVL_EXDMA5, "ovl1_ovl_exdma5", "disp", 9),
	GATE_HWV_OVL10(CLK_OVL1_OVL_EXDMA6, "ovl1_ovl_exdma6", "disp", 10),
	GATE_HWV_OVL10(CLK_OVL1_OVL_EXDMA7, "ovl1_ovl_exdma7", "disp", 11),
	GATE_HWV_OVL10(CLK_OVL1_OVL_EXDMA8, "ovl1_ovl_exdma8", "disp", 12),
	GATE_HWV_OVL10(CLK_OVL1_OVL_EXDMA9, "ovl1_ovl_exdma9", "disp", 13),
	GATE_HWV_OVL10(CLK_OVL1_OVL_BLENDER0, "ovl1_ovl_blender0", "disp", 14),
	GATE_HWV_OVL10(CLK_OVL1_OVL_BLENDER1, "ovl1_ovl_blender1", "disp", 15),
	GATE_HWV_OVL10(CLK_OVL1_OVL_BLENDER2, "ovl1_ovl_blender2", "disp", 16),
	GATE_HWV_OVL10(CLK_OVL1_OVL_BLENDER3, "ovl1_ovl_blender3", "disp", 17),
	GATE_HWV_OVL10(CLK_OVL1_OVL_BLENDER4, "ovl1_ovl_blender4", "disp", 18),
	GATE_HWV_OVL10(CLK_OVL1_OVL_BLENDER5, "ovl1_ovl_blender5", "disp", 19),
	GATE_HWV_OVL10(CLK_OVL1_OVL_BLENDER6, "ovl1_ovl_blender6", "disp", 20),
	GATE_HWV_OVL10(CLK_OVL1_OVL_BLENDER7, "ovl1_ovl_blender7", "disp", 21),
	GATE_HWV_OVL10(CLK_OVL1_OVL_BLENDER8, "ovl1_ovl_blender8", "disp", 22),
	GATE_HWV_OVL10(CLK_OVL1_OVL_BLENDER9, "ovl1_ovl_blender9", "disp", 23),
	GATE_HWV_OVL10(CLK_OVL1_OVL_OUTPROC0, "ovl1_ovl_outproc0", "disp", 24),
	GATE_HWV_OVL10(CLK_OVL1_OVL_OUTPROC1, "ovl1_ovl_outproc1", "disp", 25),
	GATE_HWV_OVL10(CLK_OVL1_OVL_OUTPROC2, "ovl1_ovl_outproc2", "disp", 26),
	GATE_HWV_OVL10(CLK_OVL1_OVL_OUTPROC3, "ovl1_ovl_outproc3", "disp", 27),
	GATE_HWV_OVL10(CLK_OVL1_OVL_OUTPROC4, "ovl1_ovl_outproc4", "disp", 28),
	GATE_HWV_OVL10(CLK_OVL1_OVL_OUTPROC5, "ovl1_ovl_outproc5", "disp", 29),
	GATE_HWV_OVL10(CLK_OVL1_OVL_MDP_RSZ0, "ovl1_ovl_mdp_rsz0", "disp", 30),
	GATE_HWV_OVL10(CLK_OVL1_OVL_MDP_RSZ1, "ovl1_ovl_mdp_rsz1", "disp", 31),
	/* OVL11 */
	GATE_HWV_OVL11(CLK_OVL1_OVL_DISP_WDMA0, "ovl1_ovl_disp_wdma0", "disp", 0),
	GATE_HWV_OVL11(CLK_OVL1_OVL_DISP_WDMA1, "ovl1_ovl_disp_wdma1", "disp", 1),
	GATE_HWV_OVL11(CLK_OVL1_OVL_UFBC_WDMA0, "ovl1_ovl_ufbc_wdma0", "disp", 2),
	GATE_HWV_OVL11(CLK_OVL1_OVL_MDP_RDMA0, "ovl1_ovl_mdp_rdma0", "disp", 3),
	GATE_HWV_OVL11(CLK_OVL1_OVL_MDP_RDMA1, "ovl1_ovl_mdp_rdma1", "disp", 4),
	GATE_HWV_OVL11(CLK_OVL1_OVL_BWM0, "ovl1_ovl_bwm0", "disp", 5),
	GATE_HWV_OVL11(CLK_OVL1_DLI0, "ovl1_dli0", "disp", 6),
	GATE_HWV_OVL11(CLK_OVL1_DLI1, "ovl1_dli1", "disp", 7),
	GATE_HWV_OVL11(CLK_OVL1_DLI2, "ovl1_dli2", "disp", 8),
	GATE_HWV_OVL11(CLK_OVL1_DLI3, "ovl1_dli3", "disp", 9),
	GATE_HWV_OVL11(CLK_OVL1_DLI4, "ovl1_dli4", "disp", 10),
	GATE_HWV_OVL11(CLK_OVL1_DLI5, "ovl1_dli5", "disp", 11),
	GATE_HWV_OVL11(CLK_OVL1_DLI6, "ovl1_dli6", "disp", 12),
	GATE_HWV_OVL11(CLK_OVL1_DLI7, "ovl1_dli7", "disp", 13),
	GATE_HWV_OVL11(CLK_OVL1_DLI8, "ovl1_dli8", "disp", 14),
	GATE_HWV_OVL11(CLK_OVL1_DLO0, "ovl1_dlo0", "disp", 15),
	GATE_HWV_OVL11(CLK_OVL1_DLO1, "ovl1_dlo1", "disp", 16),
	GATE_HWV_OVL11(CLK_OVL1_DLO2, "ovl1_dlo2", "disp", 17),
	GATE_HWV_OVL11(CLK_OVL1_DLO3, "ovl1_dlo3", "disp", 18),
	GATE_HWV_OVL11(CLK_OVL1_DLO4, "ovl1_dlo4", "disp", 19),
	GATE_HWV_OVL11(CLK_OVL1_DLO5, "ovl1_dlo5", "disp", 20),
	GATE_HWV_OVL11(CLK_OVL1_DLO6, "ovl1_dlo6", "disp", 21),
	GATE_HWV_OVL11(CLK_OVL1_DLO7, "ovl1_dlo7", "disp", 22),
	GATE_HWV_OVL11(CLK_OVL1_DLO8, "ovl1_dlo8", "disp", 23),
	GATE_HWV_OVL11(CLK_OVL1_DLO9, "ovl1_dlo9", "disp", 24),
	GATE_HWV_OVL11(CLK_OVL1_DLO10, "ovl1_dlo10", "disp", 25),
	GATE_HWV_OVL11(CLK_OVL1_DLO11, "ovl1_dlo11", "disp", 26),
	GATE_HWV_OVL11(CLK_OVL1_DLO12, "ovl1_dlo12", "disp", 27),
	GATE_HWV_OVL11(CLK_OVL1_OVLSYS_RELAY0, "ovl1_ovlsys_relay0", "disp", 28),
	GATE_HWV_OVL11(CLK_OVL1_OVL_INLINEROT0, "ovl1_ovl_inlinerot0", "disp", 29),
	GATE_HWV_OVL11(CLK_OVL1_SMI, "ovl1_smi", "disp", 30),
};

static const struct mtk_clk_desc ovl1_mcd = {
	.clks = ovl1_clks,
	.num_clks = ARRAY_SIZE(ovl1_clks),
};

static const struct platform_device_id clk_mt8196_ovl1_id_table[] = {
	{ .name = "clk-mt8196-ovl1", .driver_data = (kernel_ulong_t)&ovl1_mcd },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, clk_mt8196_ovl1_id_table);

static struct platform_driver clk_mt8196_ovl1_drv = {
	.probe = mtk_clk_pdev_probe,
	.remove = mtk_clk_pdev_remove,
	.driver = {
		.name = "clk-mt8196-ovl1",
	},
	.id_table = clk_mt8196_ovl1_id_table,
};
module_platform_driver(clk_mt8196_ovl1_drv);

MODULE_DESCRIPTION("MediaTek MT8196 ovl1 clocks driver");
MODULE_LICENSE("GPL");
