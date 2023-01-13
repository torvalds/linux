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

#define REGS_ADDR(name) \
	((void __iomem *)&regs->name)

#define REGS_CH_ADDR(name, _dir, _ch)					       \
	({								       \
		struct dw_edma_v0_ch_regs __iomem *__ch_regs;		       \
									       \
		if ((dw)->chip->mf == EDMA_MF_EDMA_LEGACY)		       \
			__ch_regs = &regs->type.legacy.ch;		       \
		else if (_dir == EDMA_DIR_READ)				       \
			__ch_regs = &regs->type.unroll.ch[_ch].rd;	       \
		else							       \
			__ch_regs = &regs->type.unroll.ch[_ch].wr;	       \
									       \
		(void __iomem *)&__ch_regs->name;			       \
	})

#define REGISTER(name) \
	{ #name, REGS_ADDR(name) }

#define CTX_REGISTER(name, dir, ch) \
	{ #name, REGS_CH_ADDR(name, dir, ch), dir, ch }

#define WR_REGISTER(name) \
	{ #name, REGS_ADDR(wr_##name) }
#define RD_REGISTER(name) \
	{ #name, REGS_ADDR(rd_##name) }

#define WR_REGISTER_LEGACY(name) \
	{ #name, REGS_ADDR(type.legacy.wr_##name) }
#define RD_REGISTER_LEGACY(name) \
	{ #name, REGS_ADDR(type.legacy.rd_##name) }

#define WR_REGISTER_UNROLL(name) \
	{ #name, REGS_ADDR(type.unroll.wr_##name) }
#define RD_REGISTER_UNROLL(name) \
	{ #name, REGS_ADDR(type.unroll.rd_##name) }

#define WRITE_STR				"write"
#define READ_STR				"read"
#define CHANNEL_STR				"channel"
#define REGISTERS_STR				"registers"

static struct dw_edma				*dw;
static struct dw_edma_v0_regs			__iomem *regs;

struct dw_edma_debugfs_entry {
	const char				*name;
	void __iomem				*reg;
	enum dw_edma_dir			dir;
	u16					ch;
};

static int dw_edma_debugfs_u32_get(void *data, u64 *val)
{
	struct dw_edma_debugfs_entry *entry = data;
	void __iomem *reg = entry->reg;

	if (dw->chip->mf == EDMA_MF_EDMA_LEGACY &&
	    reg >= (void __iomem *)&regs->type.legacy.ch) {
		unsigned long flags;
		u32 viewport_sel;

		viewport_sel = entry->dir == EDMA_DIR_READ ? BIT(31) : 0;
		viewport_sel |= FIELD_PREP(EDMA_V0_VIEWPORT_MASK, entry->ch);

		raw_spin_lock_irqsave(&dw->lock, flags);

		writel(viewport_sel, &regs->type.legacy.viewport_sel);
		*val = readl(reg);

		raw_spin_unlock_irqrestore(&dw->lock, flags);
	} else {
		*val = readl(reg);
	}

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_x32, dw_edma_debugfs_u32_get, NULL, "0x%08llx\n");

static void dw_edma_debugfs_create_x32(const struct dw_edma_debugfs_entry ini[],
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

static void dw_edma_debugfs_regs_ch(enum dw_edma_dir dir, u16 ch,
				    struct dentry *dent)
{
	struct dw_edma_debugfs_entry debugfs_regs[] = {
		CTX_REGISTER(ch_control1, dir, ch),
		CTX_REGISTER(ch_control2, dir, ch),
		CTX_REGISTER(transfer_size, dir, ch),
		CTX_REGISTER(sar.lsb, dir, ch),
		CTX_REGISTER(sar.msb, dir, ch),
		CTX_REGISTER(dar.lsb, dir, ch),
		CTX_REGISTER(dar.msb, dir, ch),
		CTX_REGISTER(llp.lsb, dir, ch),
		CTX_REGISTER(llp.msb, dir, ch),
	};
	int nr_entries;

	nr_entries = ARRAY_SIZE(debugfs_regs);
	dw_edma_debugfs_create_x32(debugfs_regs, nr_entries, dent);
}

static void dw_edma_debugfs_regs_wr(struct dentry *dent)
{
	const struct dw_edma_debugfs_entry debugfs_regs[] = {
		/* eDMA global registers */
		WR_REGISTER(engine_en),
		WR_REGISTER(doorbell),
		WR_REGISTER(ch_arb_weight.lsb),
		WR_REGISTER(ch_arb_weight.msb),
		/* eDMA interrupts registers */
		WR_REGISTER(int_status),
		WR_REGISTER(int_mask),
		WR_REGISTER(int_clear),
		WR_REGISTER(err_status),
		WR_REGISTER(done_imwr.lsb),
		WR_REGISTER(done_imwr.msb),
		WR_REGISTER(abort_imwr.lsb),
		WR_REGISTER(abort_imwr.msb),
		WR_REGISTER(ch01_imwr_data),
		WR_REGISTER(ch23_imwr_data),
		WR_REGISTER(ch45_imwr_data),
		WR_REGISTER(ch67_imwr_data),
		WR_REGISTER(linked_list_err_en),
	};
	const struct dw_edma_debugfs_entry debugfs_unroll_regs[] = {
		/* eDMA channel context grouping */
		WR_REGISTER_UNROLL(engine_chgroup),
		WR_REGISTER_UNROLL(engine_hshake_cnt.lsb),
		WR_REGISTER_UNROLL(engine_hshake_cnt.msb),
		WR_REGISTER_UNROLL(ch0_pwr_en),
		WR_REGISTER_UNROLL(ch1_pwr_en),
		WR_REGISTER_UNROLL(ch2_pwr_en),
		WR_REGISTER_UNROLL(ch3_pwr_en),
		WR_REGISTER_UNROLL(ch4_pwr_en),
		WR_REGISTER_UNROLL(ch5_pwr_en),
		WR_REGISTER_UNROLL(ch6_pwr_en),
		WR_REGISTER_UNROLL(ch7_pwr_en),
	};
	struct dentry *regs_dent, *ch_dent;
	int nr_entries, i;
	char name[16];

	regs_dent = debugfs_create_dir(WRITE_STR, dent);

	nr_entries = ARRAY_SIZE(debugfs_regs);
	dw_edma_debugfs_create_x32(debugfs_regs, nr_entries, regs_dent);

	if (dw->chip->mf == EDMA_MF_HDMA_COMPAT) {
		nr_entries = ARRAY_SIZE(debugfs_unroll_regs);
		dw_edma_debugfs_create_x32(debugfs_unroll_regs, nr_entries,
					   regs_dent);
	}

	for (i = 0; i < dw->wr_ch_cnt; i++) {
		snprintf(name, sizeof(name), "%s:%d", CHANNEL_STR, i);

		ch_dent = debugfs_create_dir(name, regs_dent);

		dw_edma_debugfs_regs_ch(EDMA_DIR_WRITE, i, ch_dent);
	}
}

static void dw_edma_debugfs_regs_rd(struct dentry *dent)
{
	const struct dw_edma_debugfs_entry debugfs_regs[] = {
		/* eDMA global registers */
		RD_REGISTER(engine_en),
		RD_REGISTER(doorbell),
		RD_REGISTER(ch_arb_weight.lsb),
		RD_REGISTER(ch_arb_weight.msb),
		/* eDMA interrupts registers */
		RD_REGISTER(int_status),
		RD_REGISTER(int_mask),
		RD_REGISTER(int_clear),
		RD_REGISTER(err_status.lsb),
		RD_REGISTER(err_status.msb),
		RD_REGISTER(linked_list_err_en),
		RD_REGISTER(done_imwr.lsb),
		RD_REGISTER(done_imwr.msb),
		RD_REGISTER(abort_imwr.lsb),
		RD_REGISTER(abort_imwr.msb),
		RD_REGISTER(ch01_imwr_data),
		RD_REGISTER(ch23_imwr_data),
		RD_REGISTER(ch45_imwr_data),
		RD_REGISTER(ch67_imwr_data),
	};
	const struct dw_edma_debugfs_entry debugfs_unroll_regs[] = {
		/* eDMA channel context grouping */
		RD_REGISTER_UNROLL(engine_chgroup),
		RD_REGISTER_UNROLL(engine_hshake_cnt.lsb),
		RD_REGISTER_UNROLL(engine_hshake_cnt.msb),
		RD_REGISTER_UNROLL(ch0_pwr_en),
		RD_REGISTER_UNROLL(ch1_pwr_en),
		RD_REGISTER_UNROLL(ch2_pwr_en),
		RD_REGISTER_UNROLL(ch3_pwr_en),
		RD_REGISTER_UNROLL(ch4_pwr_en),
		RD_REGISTER_UNROLL(ch5_pwr_en),
		RD_REGISTER_UNROLL(ch6_pwr_en),
		RD_REGISTER_UNROLL(ch7_pwr_en),
	};
	struct dentry *regs_dent, *ch_dent;
	int nr_entries, i;
	char name[16];

	regs_dent = debugfs_create_dir(READ_STR, dent);

	nr_entries = ARRAY_SIZE(debugfs_regs);
	dw_edma_debugfs_create_x32(debugfs_regs, nr_entries, regs_dent);

	if (dw->chip->mf == EDMA_MF_HDMA_COMPAT) {
		nr_entries = ARRAY_SIZE(debugfs_unroll_regs);
		dw_edma_debugfs_create_x32(debugfs_unroll_regs, nr_entries,
					   regs_dent);
	}

	for (i = 0; i < dw->rd_ch_cnt; i++) {
		snprintf(name, sizeof(name), "%s:%d", CHANNEL_STR, i);

		ch_dent = debugfs_create_dir(name, regs_dent);

		dw_edma_debugfs_regs_ch(EDMA_DIR_READ, i, ch_dent);
	}
}

static void dw_edma_debugfs_regs(void)
{
	const struct dw_edma_debugfs_entry debugfs_regs[] = {
		REGISTER(ctrl_data_arb_prior),
		REGISTER(ctrl),
	};
	struct dentry *regs_dent;
	int nr_entries;

	regs_dent = debugfs_create_dir(REGISTERS_STR, dw->debugfs);

	nr_entries = ARRAY_SIZE(debugfs_regs);
	dw_edma_debugfs_create_x32(debugfs_regs, nr_entries, regs_dent);

	dw_edma_debugfs_regs_wr(regs_dent);
	dw_edma_debugfs_regs_rd(regs_dent);
}

void dw_edma_v0_debugfs_on(struct dw_edma *_dw)
{
	if (!debugfs_initialized())
		return;

	dw = _dw;
	if (!dw)
		return;

	regs = dw->chip->reg_base;
	if (!regs)
		return;

	dw->debugfs = debugfs_create_dir(dw->name, NULL);

	debugfs_create_u32("mf", 0444, dw->debugfs, &dw->chip->mf);
	debugfs_create_u16("wr_ch_cnt", 0444, dw->debugfs, &dw->wr_ch_cnt);
	debugfs_create_u16("rd_ch_cnt", 0444, dw->debugfs, &dw->rd_ch_cnt);

	dw_edma_debugfs_regs();
}

void dw_edma_v0_debugfs_off(struct dw_edma *_dw)
{
	dw = _dw;
	if (!dw)
		return;

	debugfs_remove_recursive(dw->debugfs);
	dw->debugfs = NULL;
}
