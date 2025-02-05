// SPDX-License-Identifier: GPL-2.0
/*
 * Sophgo SG2042 RP clock Driver
 *
 * Copyright (C) 2024 Sophgo Technology Inc.
 * Copyright (C) 2024 Chen Wang <unicorn_wang@outlook.com>
 */

#include <linux/array_size.h>
#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include <dt-bindings/clock/sophgo,sg2042-rpgate.h>

#include "clk-sg2042.h"

#define R_SYSGATE_BEGIN		0x0368
#define R_RP_RXU_CLK_ENABLE	(0x0368 - R_SYSGATE_BEGIN)
#define R_MP0_STATUS_REG	(0x0380 - R_SYSGATE_BEGIN)
#define R_MP0_CONTROL_REG	(0x0384 - R_SYSGATE_BEGIN)
#define R_MP1_STATUS_REG	(0x0388 - R_SYSGATE_BEGIN)
#define R_MP1_CONTROL_REG	(0x038C - R_SYSGATE_BEGIN)
#define R_MP2_STATUS_REG	(0x0390 - R_SYSGATE_BEGIN)
#define R_MP2_CONTROL_REG	(0x0394 - R_SYSGATE_BEGIN)
#define R_MP3_STATUS_REG	(0x0398 - R_SYSGATE_BEGIN)
#define R_MP3_CONTROL_REG	(0x039C - R_SYSGATE_BEGIN)
#define R_MP4_STATUS_REG	(0x03A0 - R_SYSGATE_BEGIN)
#define R_MP4_CONTROL_REG	(0x03A4 - R_SYSGATE_BEGIN)
#define R_MP5_STATUS_REG	(0x03A8 - R_SYSGATE_BEGIN)
#define R_MP5_CONTROL_REG	(0x03AC - R_SYSGATE_BEGIN)
#define R_MP6_STATUS_REG	(0x03B0 - R_SYSGATE_BEGIN)
#define R_MP6_CONTROL_REG	(0x03B4 - R_SYSGATE_BEGIN)
#define R_MP7_STATUS_REG	(0x03B8 - R_SYSGATE_BEGIN)
#define R_MP7_CONTROL_REG	(0x03BC - R_SYSGATE_BEGIN)
#define R_MP8_STATUS_REG	(0x03C0 - R_SYSGATE_BEGIN)
#define R_MP8_CONTROL_REG	(0x03C4 - R_SYSGATE_BEGIN)
#define R_MP9_STATUS_REG	(0x03C8 - R_SYSGATE_BEGIN)
#define R_MP9_CONTROL_REG	(0x03CC - R_SYSGATE_BEGIN)
#define R_MP10_STATUS_REG	(0x03D0 - R_SYSGATE_BEGIN)
#define R_MP10_CONTROL_REG	(0x03D4 - R_SYSGATE_BEGIN)
#define R_MP11_STATUS_REG	(0x03D8 - R_SYSGATE_BEGIN)
#define R_MP11_CONTROL_REG	(0x03DC - R_SYSGATE_BEGIN)
#define R_MP12_STATUS_REG	(0x03E0 - R_SYSGATE_BEGIN)
#define R_MP12_CONTROL_REG	(0x03E4 - R_SYSGATE_BEGIN)
#define R_MP13_STATUS_REG	(0x03E8 - R_SYSGATE_BEGIN)
#define R_MP13_CONTROL_REG	(0x03EC - R_SYSGATE_BEGIN)
#define R_MP14_STATUS_REG	(0x03F0 - R_SYSGATE_BEGIN)
#define R_MP14_CONTROL_REG	(0x03F4 - R_SYSGATE_BEGIN)
#define R_MP15_STATUS_REG	(0x03F8 - R_SYSGATE_BEGIN)
#define R_MP15_CONTROL_REG	(0x03FC - R_SYSGATE_BEGIN)

/**
 * struct sg2042_rpgate_clock - Gate clock for RP(riscv processors) subsystem
 * @hw:			clk_hw for initialization
 * @id:			used to map clk_onecell_data
 * @offset_enable:	offset of gate enable registers
 * @bit_idx:		which bit in the register controls gating of this clock
 */
struct sg2042_rpgate_clock {
	struct clk_hw hw;

	unsigned int id;

	u32 offset_enable;
	u8 bit_idx;
};

/*
 * Clock initialization macro naming rules:
 * FW: use CLK_HW_INIT_FW_NAME
 */
#define SG2042_GATE_FW(_id, _name, _parent, _flags,	\
		       _r_enable, _bit_idx) {		\
		.hw.init = CLK_HW_INIT_FW_NAME(		\
				_name,			\
				_parent,		\
				NULL,			\
				_flags),		\
		.id = _id,				\
		.offset_enable = _r_enable,		\
		.bit_idx = _bit_idx,			\
	}

/*
 * Gate clocks for RP subsystem (including the MP subsystem), which control
 * registers are defined in SYS_CTRL.
 */
static const struct sg2042_rpgate_clock sg2042_gate_rp[] = {
	/* downstream of clk_gate_rp_cpu_normal about rxu */
	SG2042_GATE_FW(GATE_CLK_RXU0, "clk_gate_rxu0", "rpgate",
		       0, R_RP_RXU_CLK_ENABLE, 0),
	SG2042_GATE_FW(GATE_CLK_RXU1, "clk_gate_rxu1", "rpgate",
		       0, R_RP_RXU_CLK_ENABLE, 1),
	SG2042_GATE_FW(GATE_CLK_RXU2, "clk_gate_rxu2", "rpgate",
		       0, R_RP_RXU_CLK_ENABLE, 2),
	SG2042_GATE_FW(GATE_CLK_RXU3, "clk_gate_rxu3", "rpgate",
		       0, R_RP_RXU_CLK_ENABLE, 3),
	SG2042_GATE_FW(GATE_CLK_RXU4, "clk_gate_rxu4", "rpgate",
		       0, R_RP_RXU_CLK_ENABLE, 4),
	SG2042_GATE_FW(GATE_CLK_RXU5, "clk_gate_rxu5", "rpgate",
		       0, R_RP_RXU_CLK_ENABLE, 5),
	SG2042_GATE_FW(GATE_CLK_RXU6, "clk_gate_rxu6", "rpgate",
		       0, R_RP_RXU_CLK_ENABLE, 6),
	SG2042_GATE_FW(GATE_CLK_RXU7, "clk_gate_rxu7", "rpgate",
		       0, R_RP_RXU_CLK_ENABLE, 7),
	SG2042_GATE_FW(GATE_CLK_RXU8, "clk_gate_rxu8", "rpgate",
		       0, R_RP_RXU_CLK_ENABLE, 8),
	SG2042_GATE_FW(GATE_CLK_RXU9, "clk_gate_rxu9", "rpgate",
		       0, R_RP_RXU_CLK_ENABLE, 9),
	SG2042_GATE_FW(GATE_CLK_RXU10, "clk_gate_rxu10", "rpgate",
		       0, R_RP_RXU_CLK_ENABLE, 10),
	SG2042_GATE_FW(GATE_CLK_RXU11, "clk_gate_rxu11", "rpgate",
		       0, R_RP_RXU_CLK_ENABLE, 11),
	SG2042_GATE_FW(GATE_CLK_RXU12, "clk_gate_rxu12", "rpgate",
		       0, R_RP_RXU_CLK_ENABLE, 12),
	SG2042_GATE_FW(GATE_CLK_RXU13, "clk_gate_rxu13", "rpgate",
		       0, R_RP_RXU_CLK_ENABLE, 13),
	SG2042_GATE_FW(GATE_CLK_RXU14, "clk_gate_rxu14", "rpgate",
		       0, R_RP_RXU_CLK_ENABLE, 14),
	SG2042_GATE_FW(GATE_CLK_RXU15, "clk_gate_rxu15", "rpgate",
		       0, R_RP_RXU_CLK_ENABLE, 15),
	SG2042_GATE_FW(GATE_CLK_RXU16, "clk_gate_rxu16", "rpgate",
		       0, R_RP_RXU_CLK_ENABLE, 16),
	SG2042_GATE_FW(GATE_CLK_RXU17, "clk_gate_rxu17", "rpgate",
		       0, R_RP_RXU_CLK_ENABLE, 17),
	SG2042_GATE_FW(GATE_CLK_RXU18, "clk_gate_rxu18", "rpgate",
		       0, R_RP_RXU_CLK_ENABLE, 18),
	SG2042_GATE_FW(GATE_CLK_RXU19, "clk_gate_rxu19", "rpgate",
		       0, R_RP_RXU_CLK_ENABLE, 19),
	SG2042_GATE_FW(GATE_CLK_RXU20, "clk_gate_rxu20", "rpgate",
		       0, R_RP_RXU_CLK_ENABLE, 20),
	SG2042_GATE_FW(GATE_CLK_RXU21, "clk_gate_rxu21", "rpgate",
		       0, R_RP_RXU_CLK_ENABLE, 21),
	SG2042_GATE_FW(GATE_CLK_RXU22, "clk_gate_rxu22", "rpgate",
		       0, R_RP_RXU_CLK_ENABLE, 22),
	SG2042_GATE_FW(GATE_CLK_RXU23, "clk_gate_rxu23", "rpgate",
		       0, R_RP_RXU_CLK_ENABLE, 23),
	SG2042_GATE_FW(GATE_CLK_RXU24, "clk_gate_rxu24", "rpgate",
		       0, R_RP_RXU_CLK_ENABLE, 24),
	SG2042_GATE_FW(GATE_CLK_RXU25, "clk_gate_rxu25", "rpgate",
		       0, R_RP_RXU_CLK_ENABLE, 25),
	SG2042_GATE_FW(GATE_CLK_RXU26, "clk_gate_rxu26", "rpgate",
		       0, R_RP_RXU_CLK_ENABLE, 26),
	SG2042_GATE_FW(GATE_CLK_RXU27, "clk_gate_rxu27", "rpgate",
		       0, R_RP_RXU_CLK_ENABLE, 27),
	SG2042_GATE_FW(GATE_CLK_RXU28, "clk_gate_rxu28", "rpgate",
		       0, R_RP_RXU_CLK_ENABLE, 28),
	SG2042_GATE_FW(GATE_CLK_RXU29, "clk_gate_rxu29", "rpgate",
		       0, R_RP_RXU_CLK_ENABLE, 29),
	SG2042_GATE_FW(GATE_CLK_RXU30, "clk_gate_rxu30", "rpgate",
		       0, R_RP_RXU_CLK_ENABLE, 30),
	SG2042_GATE_FW(GATE_CLK_RXU31, "clk_gate_rxu31", "rpgate",
		       0, R_RP_RXU_CLK_ENABLE, 31),

	/* downstream of clk_gate_rp_cpu_normal about mp */
	SG2042_GATE_FW(GATE_CLK_MP0, "clk_gate_mp0", "rpgate",
		       CLK_IS_CRITICAL, R_MP0_CONTROL_REG, 0),
	SG2042_GATE_FW(GATE_CLK_MP1, "clk_gate_mp1", "rpgate",
		       CLK_IS_CRITICAL, R_MP1_CONTROL_REG, 0),
	SG2042_GATE_FW(GATE_CLK_MP2, "clk_gate_mp2", "rpgate",
		       CLK_IS_CRITICAL, R_MP2_CONTROL_REG, 0),
	SG2042_GATE_FW(GATE_CLK_MP3, "clk_gate_mp3", "rpgate",
		       CLK_IS_CRITICAL, R_MP3_CONTROL_REG, 0),
	SG2042_GATE_FW(GATE_CLK_MP4, "clk_gate_mp4", "rpgate",
		       CLK_IS_CRITICAL, R_MP4_CONTROL_REG, 0),
	SG2042_GATE_FW(GATE_CLK_MP5, "clk_gate_mp5", "rpgate",
		       CLK_IS_CRITICAL, R_MP5_CONTROL_REG, 0),
	SG2042_GATE_FW(GATE_CLK_MP6, "clk_gate_mp6", "rpgate",
		       CLK_IS_CRITICAL, R_MP6_CONTROL_REG, 0),
	SG2042_GATE_FW(GATE_CLK_MP7, "clk_gate_mp7", "rpgate",
		       CLK_IS_CRITICAL, R_MP7_CONTROL_REG, 0),
	SG2042_GATE_FW(GATE_CLK_MP8, "clk_gate_mp8", "rpgate",
		       CLK_IS_CRITICAL, R_MP8_CONTROL_REG, 0),
	SG2042_GATE_FW(GATE_CLK_MP9, "clk_gate_mp9", "rpgate",
		       CLK_IS_CRITICAL, R_MP9_CONTROL_REG, 0),
	SG2042_GATE_FW(GATE_CLK_MP10, "clk_gate_mp10", "rpgate",
		       CLK_IS_CRITICAL, R_MP10_CONTROL_REG, 0),
	SG2042_GATE_FW(GATE_CLK_MP11, "clk_gate_mp11", "rpgate",
		       CLK_IS_CRITICAL, R_MP11_CONTROL_REG, 0),
	SG2042_GATE_FW(GATE_CLK_MP12, "clk_gate_mp12", "rpgate",
		       CLK_IS_CRITICAL, R_MP12_CONTROL_REG, 0),
	SG2042_GATE_FW(GATE_CLK_MP13, "clk_gate_mp13", "rpgate",
		       CLK_IS_CRITICAL, R_MP13_CONTROL_REG, 0),
	SG2042_GATE_FW(GATE_CLK_MP14, "clk_gate_mp14", "rpgate",
		       CLK_IS_CRITICAL, R_MP14_CONTROL_REG, 0),
	SG2042_GATE_FW(GATE_CLK_MP15, "clk_gate_mp15", "rpgate",
		       CLK_IS_CRITICAL, R_MP15_CONTROL_REG, 0),
};

static DEFINE_SPINLOCK(sg2042_clk_lock);

static int sg2042_clk_register_rpgates(struct device *dev,
				       struct sg2042_clk_data *clk_data,
				       const struct sg2042_rpgate_clock gate_clks[],
				       int num_gate_clks)
{
	const struct sg2042_rpgate_clock *gate;
	struct clk_hw *hw;
	int i, ret = 0;

	for (i = 0; i < num_gate_clks; i++) {
		gate = &gate_clks[i];
		hw = devm_clk_hw_register_gate_parent_data
			(dev,
			 gate->hw.init->name,
			 gate->hw.init->parent_data,
			 gate->hw.init->flags,
			 clk_data->iobase + gate->offset_enable,
			 gate->bit_idx,
			 0,
			 &sg2042_clk_lock);
		if (IS_ERR(hw)) {
			pr_err("failed to register clock %s\n", gate->hw.init->name);
			ret = PTR_ERR(hw);
			break;
		}

		clk_data->onecell_data.hws[gate->id] = hw;
	}

	return ret;
}

static int sg2042_init_clkdata(struct platform_device *pdev,
			       int num_clks,
			       struct sg2042_clk_data **pp_clk_data)
{
	struct sg2042_clk_data *clk_data;

	clk_data = devm_kzalloc(&pdev->dev,
				struct_size(clk_data, onecell_data.hws, num_clks),
				GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;

	clk_data->iobase = devm_platform_ioremap_resource(pdev, 0);
	if (WARN_ON(IS_ERR(clk_data->iobase)))
		return PTR_ERR(clk_data->iobase);

	clk_data->onecell_data.num = num_clks;

	*pp_clk_data = clk_data;

	return 0;
}

static int sg2042_rpgate_probe(struct platform_device *pdev)
{
	struct sg2042_clk_data *clk_data = NULL;
	int num_clks;
	int ret;

	num_clks = ARRAY_SIZE(sg2042_gate_rp);

	ret = sg2042_init_clkdata(pdev, num_clks, &clk_data);
	if (ret)
		goto error_out;

	ret = sg2042_clk_register_rpgates(&pdev->dev, clk_data, sg2042_gate_rp,
					  num_clks);
	if (ret)
		goto error_out;

	return devm_of_clk_add_hw_provider(&pdev->dev,
					   of_clk_hw_onecell_get,
					   &clk_data->onecell_data);

error_out:
	pr_err("%s failed error number %d\n", __func__, ret);
	return ret;
}

static const struct of_device_id sg2042_rpgate_match[] = {
	{ .compatible = "sophgo,sg2042-rpgate" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sg2042_rpgate_match);

static struct platform_driver sg2042_rpgate_driver = {
	.probe = sg2042_rpgate_probe,
	.driver = {
		.name = "clk-sophgo-sg2042-rpgate",
		.of_match_table = sg2042_rpgate_match,
		.suppress_bind_attrs = true,
	},
};
module_platform_driver(sg2042_rpgate_driver);

MODULE_AUTHOR("Chen Wang");
MODULE_DESCRIPTION("Sophgo SG2042 rp subsystem clock driver");
MODULE_LICENSE("GPL");
