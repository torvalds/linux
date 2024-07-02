// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018-2019 Synopsys, Inc. and/or its affiliates.
 * Synopsys DesignWare eDMA v0 core
 *
 * Author: Gustavo Pimentel <gustavo.pimentel@synopsys.com>
 */

#include <linux/debugfs.h>
#include <linux/bitfield.h>

#include "dw-edma-v0-debugfs.h"
#include "dw-edma-v0-regs.h"
#include "dw-edma-core.h"

#define REGS_ADDR(dw, name)						       \
	({								       \
		struct dw_edma_v0_regs __iomem *__regs = (dw)->chip->reg_base; \
									       \
		(void __iomem *)&__regs->name;				       \
	})

#define REGS_CH_ADDR(dw, name, _dir, _ch)				       \
	({								       \
		struct dw_edma_v0_ch_regs __iomem *__ch_regs;		       \
									       \
		if ((dw)->chip->mf == EDMA_MF_EDMA_LEGACY)		       \
			__ch_regs = REGS_ADDR(dw, type.legacy.ch);	       \
		else if (_dir == EDMA_DIR_READ)				       \
			__ch_regs = REGS_ADDR(dw, type.unroll.ch[_ch].rd);     \
		else							       \
			__ch_regs = REGS_ADDR(dw, type.unroll.ch[_ch].wr);     \
									       \
		(void __iomem *)&__ch_regs->name;			       \
	})

#define REGISTER(dw, name) \
	{ dw, #name, REGS_ADDR(dw, name) }

#define CTX_REGISTER(dw, name, dir, ch) \
	{ dw, #name, REGS_CH_ADDR(dw, name, dir, ch), dir, ch }

#define WR_REGISTER(dw, name) \
	{ dw, #name, REGS_ADDR(dw, wr_##name) }
#define RD_REGISTER(dw, name) \
	{ dw, #name, REGS_ADDR(dw, rd_##name) }

#define WR_REGISTER_LEGACY(dw, name) \
	{ dw, #name, REGS_ADDR(dw, type.legacy.wr_##name) }
#define RD_REGISTER_LEGACY(name) \
	{ dw, #name, REGS_ADDR(dw, type.legacy.rd_##name) }

#define WR_REGISTER_UNROLL(dw, name) \
	{ dw, #name, REGS_ADDR(dw, type.unroll.wr_##name) }
#define RD_REGISTER_UNROLL(dw, name) \
	{ dw, #name, REGS_ADDR(dw, type.unroll.rd_##name) }

#define WRITE_STR				"write"
#define READ_STR				"read"
#define CHANNEL_STR				"channel"
#define REGISTERS_STR				"registers"

struct dw_edma_debugfs_entry {
	struct dw_edma				*dw;
	const char				*name;
	void __iomem				*reg;
	enum dw_edma_dir			dir;
	u16					ch;
};

static int dw_edma_debugfs_u32_get(void *data, u64 *val)
{
	struct dw_edma_debugfs_entry *entry = data;
	struct dw_edma *dw = entry->dw;
	void __iomem *reg = entry->reg;

	if (dw->chip->mf == EDMA_MF_EDMA_LEGACY &&
	    reg >= REGS_ADDR(dw, type.legacy.ch)) {
		unsigned long flags;
		u32 viewport_sel;

		viewport_sel = entry->dir == EDMA_DIR_READ ? BIT(31) : 0;
		viewport_sel |= FIELD_PREP(EDMA_V0_VIEWPORT_MASK, entry->ch);

		raw_spin_lock_irqsave(&dw->lock, flags);

		writel(viewport_sel, REGS_ADDR(dw, type.legacy.viewport_sel));
		*val = readl(reg);

		raw_spin_unlock_irqrestore(&dw->lock, flags);
	} else {
		*val = readl(reg);
	}

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_x32, dw_edma_debugfs_u32_get, NULL, "0x%08llx\n");

static void dw_edma_debugfs_create_x32(struct dw_edma *dw,
				       const struct dw_edma_debugfs_entry ini[],
				       int nr_entries, struct dentry *dent)
{
	struct dw_edma_debugfs_entry *entries;
	int i;

	entries = devm_kcalloc(dw->chip->dev, nr_entries, sizeof(*entries),
			       GFP_KERNEL);
	if (!entries)
		return;

	for (i = 0; i < nr_entries; i++) {
		entries[i] = ini[i];

		debugfs_create_file_unsafe(entries[i].name, 0444, dent,
					   &entries[i], &fops_x32);
	}
}

static void dw_edma_debugfs_regs_ch(struct dw_edma *dw, enum dw_edma_dir dir,
				    u16 ch, struct dentry *dent)
{
	struct dw_edma_debugfs_entry debugfs_regs[] = {
		CTX_REGISTER(dw, ch_control1, dir, ch),
		CTX_REGISTER(dw, ch_control2, dir, ch),
		CTX_REGISTER(dw, transfer_size, dir, ch),
		CTX_REGISTER(dw, sar.lsb, dir, ch),
		CTX_REGISTER(dw, sar.msb, dir, ch),
		CTX_REGISTER(dw, dar.lsb, dir, ch),
		CTX_REGISTER(dw, dar.msb, dir, ch),
		CTX_REGISTER(dw, llp.lsb, dir, ch),
		CTX_REGISTER(dw, llp.msb, dir, ch),
	};
	int nr_entries;

	nr_entries = ARRAY_SIZE(debugfs_regs);
	dw_edma_debugfs_create_x32(dw, debugfs_regs, nr_entries, dent);
}

static noinline_for_stack void
dw_edma_debugfs_regs_wr(struct dw_edma *dw, struct dentry *dent)
{
	const struct dw_edma_debugfs_entry debugfs_regs[] = {
		/* eDMA global registers */
		WR_REGISTER(dw, engine_en),
		WR_REGISTER(dw, doorbell),
		WR_REGISTER(dw, ch_arb_weight.lsb),
		WR_REGISTER(dw, ch_arb_weight.msb),
		/* eDMA interrupts registers */
		WR_REGISTER(dw, int_status),
		WR_REGISTER(dw, int_mask),
		WR_REGISTER(dw, int_clear),
		WR_REGISTER(dw, err_status),
		WR_REGISTER(dw, done_imwr.lsb),
		WR_REGISTER(dw, done_imwr.msb),
		WR_REGISTER(dw, abort_imwr.lsb),
		WR_REGISTER(dw, abort_imwr.msb),
		WR_REGISTER(dw, ch01_imwr_data),
		WR_REGISTER(dw, ch23_imwr_data),
		WR_REGISTER(dw, ch45_imwr_data),
		WR_REGISTER(dw, ch67_imwr_data),
		WR_REGISTER(dw, linked_list_err_en),
	};
	const struct dw_edma_debugfs_entry debugfs_unroll_regs[] = {
		/* eDMA channel context grouping */
		WR_REGISTER_UNROLL(dw, engine_chgroup),
		WR_REGISTER_UNROLL(dw, engine_hshake_cnt.lsb),
		WR_REGISTER_UNROLL(dw, engine_hshake_cnt.msb),
		WR_REGISTER_UNROLL(dw, ch0_pwr_en),
		WR_REGISTER_UNROLL(dw, ch1_pwr_en),
		WR_REGISTER_UNROLL(dw, ch2_pwr_en),
		WR_REGISTER_UNROLL(dw, ch3_pwr_en),
		WR_REGISTER_UNROLL(dw, ch4_pwr_en),
		WR_REGISTER_UNROLL(dw, ch5_pwr_en),
		WR_REGISTER_UNROLL(dw, ch6_pwr_en),
		WR_REGISTER_UNROLL(dw, ch7_pwr_en),
	};
	struct dentry *regs_dent, *ch_dent;
	int nr_entries, i;
	char name[32];

	regs_dent = debugfs_create_dir(WRITE_STR, dent);

	nr_entries = ARRAY_SIZE(debugfs_regs);
	dw_edma_debugfs_create_x32(dw, debugfs_regs, nr_entries, regs_dent);

	if (dw->chip->mf == EDMA_MF_HDMA_COMPAT) {
		nr_entries = ARRAY_SIZE(debugfs_unroll_regs);
		dw_edma_debugfs_create_x32(dw, debugfs_unroll_regs, nr_entries,
					   regs_dent);
	}

	for (i = 0; i < dw->wr_ch_cnt; i++) {
		snprintf(name, sizeof(name), "%s:%d", CHANNEL_STR, i);

		ch_dent = debugfs_create_dir(name, regs_dent);

		dw_edma_debugfs_regs_ch(dw, EDMA_DIR_WRITE, i, ch_dent);
	}
}

static noinline_for_stack void dw_edma_debugfs_regs_rd(struct dw_edma *dw,
						       struct dentry *dent)
{
	const struct dw_edma_debugfs_entry debugfs_regs[] = {
		/* eDMA global registers */
		RD_REGISTER(dw, engine_en),
		RD_REGISTER(dw, doorbell),
		RD_REGISTER(dw, ch_arb_weight.lsb),
		RD_REGISTER(dw, ch_arb_weight.msb),
		/* eDMA interrupts registers */
		RD_REGISTER(dw, int_status),
		RD_REGISTER(dw, int_mask),
		RD_REGISTER(dw, int_clear),
		RD_REGISTER(dw, err_status.lsb),
		RD_REGISTER(dw, err_status.msb),
		RD_REGISTER(dw, linked_list_err_en),
		RD_REGISTER(dw, done_imwr.lsb),
		RD_REGISTER(dw, done_imwr.msb),
		RD_REGISTER(dw, abort_imwr.lsb),
		RD_REGISTER(dw, abort_imwr.msb),
		RD_REGISTER(dw, ch01_imwr_data),
		RD_REGISTER(dw, ch23_imwr_data),
		RD_REGISTER(dw, ch45_imwr_data),
		RD_REGISTER(dw, ch67_imwr_data),
	};
	const struct dw_edma_debugfs_entry debugfs_unroll_regs[] = {
		/* eDMA channel context grouping */
		RD_REGISTER_UNROLL(dw, engine_chgroup),
		RD_REGISTER_UNROLL(dw, engine_hshake_cnt.lsb),
		RD_REGISTER_UNROLL(dw, engine_hshake_cnt.msb),
		RD_REGISTER_UNROLL(dw, ch0_pwr_en),
		RD_REGISTER_UNROLL(dw, ch1_pwr_en),
		RD_REGISTER_UNROLL(dw, ch2_pwr_en),
		RD_REGISTER_UNROLL(dw, ch3_pwr_en),
		RD_REGISTER_UNROLL(dw, ch4_pwr_en),
		RD_REGISTER_UNROLL(dw, ch5_pwr_en),
		RD_REGISTER_UNROLL(dw, ch6_pwr_en),
		RD_REGISTER_UNROLL(dw, ch7_pwr_en),
	};
	struct dentry *regs_dent, *ch_dent;
	int nr_entries, i;
	char name[32];

	regs_dent = debugfs_create_dir(READ_STR, dent);

	nr_entries = ARRAY_SIZE(debugfs_regs);
	dw_edma_debugfs_create_x32(dw, debugfs_regs, nr_entries, regs_dent);

	if (dw->chip->mf == EDMA_MF_HDMA_COMPAT) {
		nr_entries = ARRAY_SIZE(debugfs_unroll_regs);
		dw_edma_debugfs_create_x32(dw, debugfs_unroll_regs, nr_entries,
					   regs_dent);
	}

	for (i = 0; i < dw->rd_ch_cnt; i++) {
		snprintf(name, sizeof(name), "%s:%d", CHANNEL_STR, i);

		ch_dent = debugfs_create_dir(name, regs_dent);

		dw_edma_debugfs_regs_ch(dw, EDMA_DIR_READ, i, ch_dent);
	}
}

static void dw_edma_debugfs_regs(struct dw_edma *dw)
{
	const struct dw_edma_debugfs_entry debugfs_regs[] = {
		REGISTER(dw, ctrl_data_arb_prior),
		REGISTER(dw, ctrl),
	};
	struct dentry *regs_dent;
	int nr_entries;

	regs_dent = debugfs_create_dir(REGISTERS_STR, dw->dma.dbg_dev_root);

	nr_entries = ARRAY_SIZE(debugfs_regs);
	dw_edma_debugfs_create_x32(dw, debugfs_regs, nr_entries, regs_dent);

	dw_edma_debugfs_regs_wr(dw, regs_dent);
	dw_edma_debugfs_regs_rd(dw, regs_dent);
}

void dw_edma_v0_debugfs_on(struct dw_edma *dw)
{
	if (!debugfs_initialized())
		return;

	debugfs_create_u32("mf", 0444, dw->dma.dbg_dev_root, &dw->chip->mf);
	debugfs_create_u16("wr_ch_cnt", 0444, dw->dma.dbg_dev_root, &dw->wr_ch_cnt);
	debugfs_create_u16("rd_ch_cnt", 0444, dw->dma.dbg_dev_root, &dw->rd_ch_cnt);

	dw_edma_debugfs_regs(dw);
}
