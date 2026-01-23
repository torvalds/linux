// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2021 MediaTek Inc.
// Copyright (c) 2025 Collabora Ltd
//                    AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>

#include <linux/clk.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/spmi.h>

#define SWINF_IDLE	0x00
#define SWINF_WFVLDCLR	0x06

#define GET_SWINF(x)	(((x) >> 1) & 0x7)

#define PMIF_CMD_REG_0		0
#define PMIF_CMD_REG		1
#define PMIF_CMD_EXT_REG	2
#define PMIF_CMD_EXT_REG_LONG	3

#define PMIF_DELAY_US   10
#define PMIF_TIMEOUT_US (10 * 1000)

#define PMIF_CHAN_OFFSET 0x5

#define PMIF_MAX_BUSES	2
#define PMIF_MAX_CLKS	3

#define SPMI_OP_ST_BUSY 1

struct ch_reg {
	u32 ch_sta;
	u32 wdata;
	u32 rdata;
	u32 ch_send;
	u32 ch_rdy;
};

struct pmif_data {
	const u32	*regs;
	const u32	*spmimst_regs;
	u32	soc_chan;
	u32	num_spmi_buses;
};

struct pmif_bus {
	void __iomem	*base;
	void __iomem	*spmimst_base;
	struct spmi_controller *ctrl;
	struct clk_bulk_data clks[PMIF_MAX_CLKS];
	size_t nclks;
	raw_spinlock_t	lock;
};

struct pmif {
	struct pmif_bus bus[PMIF_MAX_BUSES];
	struct ch_reg	chan;
	const struct pmif_data *data;
};

static const char * const pmif_clock_names[] = {
	"pmif_sys_ck", "pmif_tmr_ck", "spmimst_clk_mux",
};

enum pmif_regs {
	PMIF_INIT_DONE,
	PMIF_INF_EN,
	PMIF_ARB_EN,
	PMIF_CMDISSUE_EN,
	PMIF_TIMER_CTRL,
	PMIF_SPI_MODE_CTRL,
	PMIF_IRQ_EVENT_EN_0,
	PMIF_IRQ_FLAG_0,
	PMIF_IRQ_CLR_0,
	PMIF_IRQ_EVENT_EN_1,
	PMIF_IRQ_FLAG_1,
	PMIF_IRQ_CLR_1,
	PMIF_IRQ_EVENT_EN_2,
	PMIF_IRQ_FLAG_2,
	PMIF_IRQ_CLR_2,
	PMIF_IRQ_EVENT_EN_3,
	PMIF_IRQ_FLAG_3,
	PMIF_IRQ_CLR_3,
	PMIF_IRQ_EVENT_EN_4,
	PMIF_IRQ_FLAG_4,
	PMIF_IRQ_CLR_4,
	PMIF_WDT_EVENT_EN_0,
	PMIF_WDT_FLAG_0,
	PMIF_WDT_EVENT_EN_1,
	PMIF_WDT_FLAG_1,
	PMIF_SWINF_0_STA,
	PMIF_SWINF_0_WDATA_31_0,
	PMIF_SWINF_0_RDATA_31_0,
	PMIF_SWINF_0_ACC,
	PMIF_SWINF_0_VLD_CLR,
	PMIF_SWINF_1_STA,
	PMIF_SWINF_1_WDATA_31_0,
	PMIF_SWINF_1_RDATA_31_0,
	PMIF_SWINF_1_ACC,
	PMIF_SWINF_1_VLD_CLR,
	PMIF_SWINF_2_STA,
	PMIF_SWINF_2_WDATA_31_0,
	PMIF_SWINF_2_RDATA_31_0,
	PMIF_SWINF_2_ACC,
	PMIF_SWINF_2_VLD_CLR,
	PMIF_SWINF_3_STA,
	PMIF_SWINF_3_WDATA_31_0,
	PMIF_SWINF_3_RDATA_31_0,
	PMIF_SWINF_3_ACC,
	PMIF_SWINF_3_VLD_CLR,
};

static const u32 mt6873_regs[] = {
	[PMIF_INIT_DONE] = 0x0000,
	[PMIF_INF_EN] = 0x0024,
	[PMIF_ARB_EN] = 0x0150,
	[PMIF_CMDISSUE_EN] = 0x03B4,
	[PMIF_TIMER_CTRL] = 0x03E0,
	[PMIF_SPI_MODE_CTRL] = 0x0400,
	[PMIF_IRQ_EVENT_EN_0] = 0x0418,
	[PMIF_IRQ_FLAG_0] = 0x0420,
	[PMIF_IRQ_CLR_0] = 0x0424,
	[PMIF_IRQ_EVENT_EN_1] = 0x0428,
	[PMIF_IRQ_FLAG_1] = 0x0430,
	[PMIF_IRQ_CLR_1] = 0x0434,
	[PMIF_IRQ_EVENT_EN_2] = 0x0438,
	[PMIF_IRQ_FLAG_2] = 0x0440,
	[PMIF_IRQ_CLR_2] = 0x0444,
	[PMIF_IRQ_EVENT_EN_3] = 0x0448,
	[PMIF_IRQ_FLAG_3] = 0x0450,
	[PMIF_IRQ_CLR_3] = 0x0454,
	[PMIF_IRQ_EVENT_EN_4] = 0x0458,
	[PMIF_IRQ_FLAG_4] = 0x0460,
	[PMIF_IRQ_CLR_4] = 0x0464,
	[PMIF_WDT_EVENT_EN_0] = 0x046C,
	[PMIF_WDT_FLAG_0] = 0x0470,
	[PMIF_WDT_EVENT_EN_1] = 0x0474,
	[PMIF_WDT_FLAG_1] = 0x0478,
	[PMIF_SWINF_0_ACC] = 0x0C00,
	[PMIF_SWINF_0_WDATA_31_0] = 0x0C04,
	[PMIF_SWINF_0_RDATA_31_0] = 0x0C14,
	[PMIF_SWINF_0_VLD_CLR] = 0x0C24,
	[PMIF_SWINF_0_STA] = 0x0C28,
	[PMIF_SWINF_1_ACC] = 0x0C40,
	[PMIF_SWINF_1_WDATA_31_0] = 0x0C44,
	[PMIF_SWINF_1_RDATA_31_0] = 0x0C54,
	[PMIF_SWINF_1_VLD_CLR] = 0x0C64,
	[PMIF_SWINF_1_STA] = 0x0C68,
	[PMIF_SWINF_2_ACC] = 0x0C80,
	[PMIF_SWINF_2_WDATA_31_0] = 0x0C84,
	[PMIF_SWINF_2_RDATA_31_0] = 0x0C94,
	[PMIF_SWINF_2_VLD_CLR] = 0x0CA4,
	[PMIF_SWINF_2_STA] = 0x0CA8,
	[PMIF_SWINF_3_ACC] = 0x0CC0,
	[PMIF_SWINF_3_WDATA_31_0] = 0x0CC4,
	[PMIF_SWINF_3_RDATA_31_0] = 0x0CD4,
	[PMIF_SWINF_3_VLD_CLR] = 0x0CE4,
	[PMIF_SWINF_3_STA] = 0x0CE8,
};

static const u32 mt8195_regs[] = {
	[PMIF_INIT_DONE] = 0x0000,
	[PMIF_INF_EN] = 0x0024,
	[PMIF_ARB_EN] = 0x0150,
	[PMIF_CMDISSUE_EN] = 0x03B8,
	[PMIF_TIMER_CTRL] = 0x03E4,
	[PMIF_SPI_MODE_CTRL] = 0x0408,
	[PMIF_IRQ_EVENT_EN_0] = 0x0420,
	[PMIF_IRQ_FLAG_0] = 0x0428,
	[PMIF_IRQ_CLR_0] = 0x042C,
	[PMIF_IRQ_EVENT_EN_1] = 0x0430,
	[PMIF_IRQ_FLAG_1] = 0x0438,
	[PMIF_IRQ_CLR_1] = 0x043C,
	[PMIF_IRQ_EVENT_EN_2] = 0x0440,
	[PMIF_IRQ_FLAG_2] = 0x0448,
	[PMIF_IRQ_CLR_2] = 0x044C,
	[PMIF_IRQ_EVENT_EN_3] = 0x0450,
	[PMIF_IRQ_FLAG_3] = 0x0458,
	[PMIF_IRQ_CLR_3] = 0x045C,
	[PMIF_IRQ_EVENT_EN_4] = 0x0460,
	[PMIF_IRQ_FLAG_4] = 0x0468,
	[PMIF_IRQ_CLR_4] = 0x046C,
	[PMIF_WDT_EVENT_EN_0] = 0x0474,
	[PMIF_WDT_FLAG_0] = 0x0478,
	[PMIF_WDT_EVENT_EN_1] = 0x047C,
	[PMIF_WDT_FLAG_1] = 0x0480,
	[PMIF_SWINF_0_ACC] = 0x0800,
	[PMIF_SWINF_0_WDATA_31_0] = 0x0804,
	[PMIF_SWINF_0_RDATA_31_0] = 0x0814,
	[PMIF_SWINF_0_VLD_CLR] = 0x0824,
	[PMIF_SWINF_0_STA] = 0x0828,
	[PMIF_SWINF_1_ACC] = 0x0840,
	[PMIF_SWINF_1_WDATA_31_0] = 0x0844,
	[PMIF_SWINF_1_RDATA_31_0] = 0x0854,
	[PMIF_SWINF_1_VLD_CLR] = 0x0864,
	[PMIF_SWINF_1_STA] = 0x0868,
	[PMIF_SWINF_2_ACC] = 0x0880,
	[PMIF_SWINF_2_WDATA_31_0] = 0x0884,
	[PMIF_SWINF_2_RDATA_31_0] = 0x0894,
	[PMIF_SWINF_2_VLD_CLR] = 0x08A4,
	[PMIF_SWINF_2_STA] = 0x08A8,
	[PMIF_SWINF_3_ACC] = 0x08C0,
	[PMIF_SWINF_3_WDATA_31_0] = 0x08C4,
	[PMIF_SWINF_3_RDATA_31_0] = 0x08D4,
	[PMIF_SWINF_3_VLD_CLR] = 0x08E4,
	[PMIF_SWINF_3_STA] = 0x08E8,
};

enum spmi_regs {
	SPMI_OP_ST_CTRL,
	SPMI_GRP_ID_EN,
	SPMI_OP_ST_STA,
	SPMI_MST_SAMPL,
	SPMI_MST_REQ_EN,
	SPMI_REC_CTRL,
	SPMI_REC0,
	SPMI_REC1,
	SPMI_REC2,
	SPMI_REC3,
	SPMI_REC4,
	SPMI_MST_DBG,

	/* MT8195 spmi regs */
	SPMI_MST_RCS_CTRL,
	SPMI_SLV_3_0_EINT,
	SPMI_SLV_7_4_EINT,
	SPMI_SLV_B_8_EINT,
	SPMI_SLV_F_C_EINT,
	SPMI_REC_CMD_DEC,
	SPMI_DEC_DBG,
};

static const u32 mt6873_spmi_regs[] = {
	[SPMI_OP_ST_CTRL] = 0x0000,
	[SPMI_GRP_ID_EN] = 0x0004,
	[SPMI_OP_ST_STA] = 0x0008,
	[SPMI_MST_SAMPL] = 0x000c,
	[SPMI_MST_REQ_EN] = 0x0010,
	[SPMI_REC_CTRL] = 0x0040,
	[SPMI_REC0] = 0x0044,
	[SPMI_REC1] = 0x0048,
	[SPMI_REC2] = 0x004c,
	[SPMI_REC3] = 0x0050,
	[SPMI_REC4] = 0x0054,
	[SPMI_MST_DBG] = 0x00fc,
};

static const u32 mt8195_spmi_regs[] = {
	[SPMI_OP_ST_CTRL] = 0x0000,
	[SPMI_GRP_ID_EN] = 0x0004,
	[SPMI_OP_ST_STA] = 0x0008,
	[SPMI_MST_SAMPL] = 0x000C,
	[SPMI_MST_REQ_EN] = 0x0010,
	[SPMI_MST_RCS_CTRL] = 0x0014,
	[SPMI_SLV_3_0_EINT] = 0x0020,
	[SPMI_SLV_7_4_EINT] = 0x0024,
	[SPMI_SLV_B_8_EINT] = 0x0028,
	[SPMI_SLV_F_C_EINT] = 0x002C,
	[SPMI_REC_CTRL] = 0x0040,
	[SPMI_REC0] = 0x0044,
	[SPMI_REC1] = 0x0048,
	[SPMI_REC2] = 0x004C,
	[SPMI_REC3] = 0x0050,
	[SPMI_REC4] = 0x0054,
	[SPMI_REC_CMD_DEC] = 0x005C,
	[SPMI_DEC_DBG] = 0x00F8,
	[SPMI_MST_DBG] = 0x00FC,
};

static inline struct pmif *to_mtk_pmif(struct spmi_controller *ctrl)
{
	return dev_get_drvdata(ctrl->dev.parent);
}

static u32 pmif_readl(struct pmif *arb, struct pmif_bus *pbus, enum pmif_regs reg)
{
	return readl(pbus->base + arb->data->regs[reg]);
}

static void pmif_writel(struct pmif *arb, struct pmif_bus *pbus,
			u32 val, enum pmif_regs reg)
{
	writel(val, pbus->base + arb->data->regs[reg]);
}

static void mtk_spmi_writel(struct pmif *arb, struct pmif_bus *pbus,
			    u32 val, enum spmi_regs reg)
{
	writel(val, pbus->spmimst_base + arb->data->spmimst_regs[reg]);
}

static bool pmif_is_fsm_vldclr(struct pmif *arb, struct pmif_bus *pbus)
{
	u32 reg_rdata;

	reg_rdata = pmif_readl(arb, pbus, arb->chan.ch_sta);

	return GET_SWINF(reg_rdata) == SWINF_WFVLDCLR;
}

static int pmif_arb_cmd(struct spmi_controller *ctrl, u8 opc, u8 sid)
{
	struct pmif_bus *pbus = spmi_controller_get_drvdata(ctrl);
	struct pmif *arb = to_mtk_pmif(ctrl);
	u32 rdata, cmd;
	int ret;

	/* Check the opcode */
	if (opc < SPMI_CMD_RESET || opc > SPMI_CMD_WAKEUP)
		return -EINVAL;

	cmd = opc - SPMI_CMD_RESET;

	mtk_spmi_writel(arb, pbus, (cmd << 0x4) | sid, SPMI_OP_ST_CTRL);
	ret = readl_poll_timeout_atomic(pbus->spmimst_base + arb->data->spmimst_regs[SPMI_OP_ST_STA],
					rdata, (rdata & SPMI_OP_ST_BUSY) == SPMI_OP_ST_BUSY,
					PMIF_DELAY_US, PMIF_TIMEOUT_US);
	if (ret < 0)
		dev_err(&ctrl->dev, "timeout, err = %d\n", ret);

	return ret;
}

static int pmif_spmi_read_cmd(struct spmi_controller *ctrl, u8 opc, u8 sid,
			      u16 addr, u8 *buf, size_t len)
{
	struct pmif_bus *pbus = spmi_controller_get_drvdata(ctrl);
	struct pmif *arb = to_mtk_pmif(ctrl);
	struct ch_reg *inf_reg;
	int ret;
	u32 data, cmd;
	unsigned long flags;

	/* Check for argument validation. */
	if (sid & ~0xf) {
		dev_err(&ctrl->dev, "exceed the max slv id\n");
		return -EINVAL;
	}

	if (len > 4) {
		dev_err(&ctrl->dev, "pmif supports 1..4 bytes per trans, but:%zu requested", len);

		return -EINVAL;
	}

	if (opc >= 0x60 && opc <= 0x7f)
		opc = PMIF_CMD_REG;
	else if ((opc >= 0x20 && opc <= 0x2f) || (opc >= 0x38 && opc <= 0x3f))
		opc = PMIF_CMD_EXT_REG_LONG;
	else
		return -EINVAL;

	raw_spin_lock_irqsave(&pbus->lock, flags);
	/* Wait for Software Interface FSM state to be IDLE. */
	inf_reg = &arb->chan;
	ret = readl_poll_timeout_atomic(pbus->base + arb->data->regs[inf_reg->ch_sta],
					data, GET_SWINF(data) == SWINF_IDLE,
					PMIF_DELAY_US, PMIF_TIMEOUT_US);
	if (ret < 0) {
		/* set channel ready if the data has transferred */
		if (pmif_is_fsm_vldclr(arb, pbus))
			pmif_writel(arb, pbus, 1, inf_reg->ch_rdy);
		raw_spin_unlock_irqrestore(&pbus->lock, flags);
		dev_err(&ctrl->dev, "failed to wait for SWINF_IDLE\n");
		return ret;
	}

	/* Send the command. */
	cmd = (opc << 30) | (sid << 24) | ((len - 1) << 16) | addr;
	pmif_writel(arb, pbus, cmd, inf_reg->ch_send);
	raw_spin_unlock_irqrestore(&pbus->lock, flags);

	/*
	 * Wait for Software Interface FSM state to be WFVLDCLR,
	 * read the data and clear the valid flag.
	 */
	ret = readl_poll_timeout_atomic(pbus->base + arb->data->regs[inf_reg->ch_sta],
					data, GET_SWINF(data) == SWINF_WFVLDCLR,
					PMIF_DELAY_US, PMIF_TIMEOUT_US);
	if (ret < 0) {
		dev_err(&ctrl->dev, "failed to wait for SWINF_WFVLDCLR\n");
		return ret;
	}

	data = pmif_readl(arb, pbus, inf_reg->rdata);
	memcpy(buf, &data, len);
	pmif_writel(arb, pbus, 1, inf_reg->ch_rdy);

	return 0;
}

static int pmif_spmi_write_cmd(struct spmi_controller *ctrl, u8 opc, u8 sid,
			       u16 addr, const u8 *buf, size_t len)
{
	struct pmif_bus *pbus = spmi_controller_get_drvdata(ctrl);
	struct pmif *arb = to_mtk_pmif(ctrl);
	struct ch_reg *inf_reg;
	int ret;
	u32 data, wdata, cmd;
	unsigned long flags;

	/* Check for argument validation. */
	if (unlikely(sid & ~0xf)) {
		dev_err(&ctrl->dev, "exceed the max slv id\n");
		return -EINVAL;
	}

	if (len > 4) {
		dev_err(&ctrl->dev, "pmif supports 1..4 bytes per trans, but:%zu requested", len);

		return -EINVAL;
	}

	/* Check the opcode */
	if (opc >= 0x40 && opc <= 0x5F)
		opc = PMIF_CMD_REG;
	else if ((opc <= 0xF) || (opc >= 0x30 && opc <= 0x37))
		opc = PMIF_CMD_EXT_REG_LONG;
	else if (opc >= 0x80)
		opc = PMIF_CMD_REG_0;
	else
		return -EINVAL;

	/* Set the write data. */
	memcpy(&wdata, buf, len);

	raw_spin_lock_irqsave(&pbus->lock, flags);
	/* Wait for Software Interface FSM state to be IDLE. */
	inf_reg = &arb->chan;
	ret = readl_poll_timeout_atomic(pbus->base + arb->data->regs[inf_reg->ch_sta],
					data, GET_SWINF(data) == SWINF_IDLE,
					PMIF_DELAY_US, PMIF_TIMEOUT_US);
	if (ret < 0) {
		/* set channel ready if the data has transferred */
		if (pmif_is_fsm_vldclr(arb, pbus))
			pmif_writel(arb, pbus, 1, inf_reg->ch_rdy);
		raw_spin_unlock_irqrestore(&pbus->lock, flags);
		dev_err(&ctrl->dev, "failed to wait for SWINF_IDLE\n");
		return ret;
	}

	pmif_writel(arb, pbus, wdata, inf_reg->wdata);

	/* Send the command. */
	cmd = (opc << 30) | BIT(29) | (sid << 24) | ((len - 1) << 16) | addr;
	pmif_writel(arb, pbus, cmd, inf_reg->ch_send);
	raw_spin_unlock_irqrestore(&pbus->lock, flags);

	return 0;
}

static const struct pmif_data mt6873_pmif_arb = {
	.regs = mt6873_regs,
	.spmimst_regs = mt6873_spmi_regs,
	.soc_chan = 2,
};

static const struct pmif_data mt8195_pmif_arb = {
	.regs = mt8195_regs,
	.spmimst_regs = mt8195_spmi_regs,
	.soc_chan = 2,
};

static int mtk_spmi_bus_probe(struct platform_device *pdev,
			      struct device_node *node,
			      const struct pmif_data *pdata,
			      struct pmif_bus *pbus)
{
	struct spmi_controller *ctrl;
	int err, idx, bus_id, i;

	if (pdata->num_spmi_buses > 1)
		bus_id = of_alias_get_id(node, "spmi");
	else
		bus_id = 0;

	if (bus_id < 0)
		return dev_err_probe(&pdev->dev, bus_id,
				     "Cannot find SPMI Bus alias ID\n");

	ctrl = devm_spmi_controller_alloc(&pdev->dev, sizeof(*pbus));
	if (IS_ERR(ctrl))
		return PTR_ERR(ctrl);

	pbus = spmi_controller_get_drvdata(ctrl);
	pbus->ctrl = ctrl;

	idx = of_property_match_string(node, "reg-names", "pmif");
	if (idx < 0)
		return -EINVAL;

	pbus->base = devm_of_iomap(&pdev->dev, node, idx, NULL);
	if (IS_ERR(pbus->base))
		return PTR_ERR(pbus->base);

	idx = of_property_match_string(node, "reg-names", "spmimst");
	if (idx < 0)
		return -EINVAL;

	pbus->spmimst_base = devm_of_iomap(&pdev->dev, node, idx, NULL);
	if (IS_ERR(pbus->spmimst_base))
		return PTR_ERR(pbus->spmimst_base);

	pbus->nclks = ARRAY_SIZE(pmif_clock_names);
	for (i = 0; i < pbus->nclks; i++) {
		pbus->clks[i].id = pmif_clock_names[i];
		pbus->clks[i].clk = of_clk_get_by_name(node, pbus->clks[i].id);
		if (IS_ERR(pbus->clks[i].clk))
			return PTR_ERR(pbus->clks[i].clk);
	}

	err = clk_bulk_prepare_enable(pbus->nclks, pbus->clks);
	if (err)
		goto err_put_clks;

	ctrl->cmd = pmif_arb_cmd;
	ctrl->read_cmd = pmif_spmi_read_cmd;
	ctrl->write_cmd = pmif_spmi_write_cmd;
	ctrl->dev.of_node = node;
	dev_set_name(&ctrl->dev, "spmi-%d", bus_id);

	raw_spin_lock_init(&pbus->lock);

	err = spmi_controller_add(ctrl);
	if (err)
		goto err_domain_remove;

	pbus->ctrl = ctrl;

	return 0;

err_domain_remove:
	clk_bulk_disable_unprepare(pbus->nclks, pbus->clks);
err_put_clks:
	clk_bulk_put(pbus->nclks, pbus->clks);
	return err;
}

static int mtk_spmi_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	struct pmif *arb;
	u32 chan_offset;
	u8 cur_bus = 0;
	int ret;

	arb = devm_kzalloc(&pdev->dev, sizeof(*arb), GFP_KERNEL);
	if (!arb)
		return -ENOMEM;

	arb->data = device_get_match_data(&pdev->dev);
	if (!arb->data) {
		dev_err(&pdev->dev, "Cannot get drv_data\n");
		return -EINVAL;
	}

	platform_set_drvdata(pdev, arb);

	if (!arb->data->num_spmi_buses) {
		ret = mtk_spmi_bus_probe(pdev, node, arb->data, &arb->bus[cur_bus]);
		if (ret)
			return ret;
	} else {
		for_each_available_child_of_node_scoped(node, child) {
			if (!of_node_name_eq(child, "spmi"))
				continue;

			ret = mtk_spmi_bus_probe(pdev, child, arb->data,
						 &arb->bus[cur_bus]);
			if (ret)
				return ret;
			cur_bus++;
		}
	}

	chan_offset = PMIF_CHAN_OFFSET * arb->data->soc_chan;
	arb->chan.ch_sta = PMIF_SWINF_0_STA + chan_offset;
	arb->chan.wdata = PMIF_SWINF_0_WDATA_31_0 + chan_offset;
	arb->chan.rdata = PMIF_SWINF_0_RDATA_31_0 + chan_offset;
	arb->chan.ch_send = PMIF_SWINF_0_ACC + chan_offset;
	arb->chan.ch_rdy = PMIF_SWINF_0_VLD_CLR + chan_offset;

	return 0;
}

static void mtk_spmi_remove(struct platform_device *pdev)
{
	struct pmif *arb = platform_get_drvdata(pdev);
	int i;

	for (i = 0; i < PMIF_MAX_BUSES; i++) {
		struct pmif_bus *pbus = &arb->bus[i];

		if (!pbus->ctrl)
			continue;

		spmi_controller_remove(pbus->ctrl);
		clk_bulk_disable_unprepare(pbus->nclks, pbus->clks);
		clk_bulk_put(pbus->nclks, pbus->clks);
	}
}

static const struct of_device_id mtk_spmi_match_table[] = {
	{
		.compatible = "mediatek,mt6873-spmi",
		.data = &mt6873_pmif_arb,
	}, {
		.compatible = "mediatek,mt8195-spmi",
		.data = &mt8195_pmif_arb,
	}, {
		/* sentinel */
	},
};
MODULE_DEVICE_TABLE(of, mtk_spmi_match_table);

static struct platform_driver mtk_spmi_driver = {
	.driver		= {
		.name	= "spmi-mtk",
		.of_match_table = mtk_spmi_match_table,
	},
	.probe		= mtk_spmi_probe,
	.remove		= mtk_spmi_remove,
};
module_platform_driver(mtk_spmi_driver);

MODULE_AUTHOR("AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>");
MODULE_AUTHOR("Hsin-Hsiung Wang <hsin-hsiung.wang@mediatek.com>");
MODULE_DESCRIPTION("MediaTek SPMI Driver");
MODULE_LICENSE("GPL");
