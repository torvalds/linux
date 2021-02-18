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
	((void __force *)&regs->name)
#define REGISTER(name) \
	{ #name, REGS_ADDR(name) }

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

static struct {
	void					__iomem *start;
	void					__iomem *end;
} lim[2][EDMA_V0_MAX_NR_CH];

struct debugfs_entries {
	const char				*name;
	dma_addr_t				*reg;
};

static int dw_edma_debugfs_u32_get(void *data, u64 *val)
{
	void __iomem *reg = (void __force __iomem *)data;
	if (dw->mf == EDMA_MF_EDMA_LEGACY &&
	    reg >= (void __iomem *)&regs->type.legacy.ch) {
		void __iomem *ptr = &regs->type.legacy.ch;
		u32 viewport_sel = 0;
		unsigned long flags;
		u16 ch;

		for (ch = 0; ch < dw->wr_ch_cnt; ch++)
			if (lim[0][ch].start >= reg && reg < lim[0][ch].end) {
				ptr += (reg - lim[0][ch].start);
				goto legacy_sel_wr;
			}

		for (ch = 0; ch < dw->rd_ch_cnt; ch++)
			if (lim[1][ch].start >= reg && reg < lim[1][ch].end) {
				ptr += (reg - lim[1][ch].start);
				goto legacy_sel_rd;
			}

		return 0;
legacy_sel_rd:
		viewport_sel = BIT(31);
legacy_sel_wr:
		viewport_sel |= FIELD_PREP(EDMA_V0_VIEWPORT_MASK, ch);

		raw_spin_lock_irqsave(&dw->lock, flags);

		writel(viewport_sel, &regs->type.legacy.viewport_sel);
		*val = readl(ptr);

		raw_spin_unlock_irqrestore(&dw->lock, flags);
	} else {
		*val = readl(reg);
	}

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_x32, dw_edma_debugfs_u32_get, NULL, "0x%08llx\n");

static void dw_edma_debugfs_create_x32(const struct debugfs_entries entries[],
				       int nr_entries, struct dentry *dir)
{
	int i;

	for (i = 0; i < nr_entries; i++) {
		if (!debugfs_create_file_unsafe(entries[i].name, 0444, dir,
						entries[i].reg,	&fops_x32))
			break;
	}
}

static void dw_edma_debugfs_regs_ch(struct dw_edma_v0_ch_regs __iomem *regs,
				    struct dentry *dir)
{
	int nr_entries;
	const struct debugfs_entries debugfs_regs[] = {
		REGISTER(ch_control1),
		REGISTER(ch_control2),
		REGISTER(transfer_size),
		REGISTER(sar.lsb),
		REGISTER(sar.msb),
		REGISTER(dar.lsb),
		REGISTER(dar.msb),
		REGISTER(llp.lsb),
		REGISTER(llp.msb),
	};

	nr_entries = ARRAY_SIZE(debugfs_regs);
	dw_edma_debugfs_create_x32(debugfs_regs, nr_entries, dir);
}

static void dw_edma_debugfs_regs_wr(struct dentry *dir)
{
	const struct debugfs_entries debugfs_regs[] = {
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
	const struct debugfs_entries debugfs_unroll_regs[] = {
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
	struct dentry *regs_dir, *ch_dir;
	int nr_entries, i;
	char name[16];

	regs_dir = debugfs_create_dir(WRITE_STR, dir);
	if (!regs_dir)
		return;

	nr_entries = ARRAY_SIZE(debugfs_regs);
	dw_edma_debugfs_create_x32(debugfs_regs, nr_entries, regs_dir);

	if (dw->mf == EDMA_MF_HDMA_COMPAT) {
		nr_entries = ARRAY_SIZE(debugfs_unroll_regs);
		dw_edma_debugfs_create_x32(debugfs_unroll_regs, nr_entries,
					   regs_dir);
	}

	for (i = 0; i < dw->wr_ch_cnt; i++) {
		snprintf(name, sizeof(name), "%s:%d", CHANNEL_STR, i);

		ch_dir = debugfs_create_dir(name, regs_dir);
		if (!ch_dir)
			return;

		dw_edma_debugfs_regs_ch(&regs->type.unroll.ch[i].wr, ch_dir);

		lim[0][i].start = &regs->type.unroll.ch[i].wr;
		lim[0][i].end = &regs->type.unroll.ch[i].padding_1[0];
	}
}

static void dw_edma_debugfs_regs_rd(struct dentry *dir)
{
	const struct debugfs_entries debugfs_regs[] = {
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
	const struct debugfs_entries debugfs_unroll_regs[] = {
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
	struct dentry *regs_dir, *ch_dir;
	int nr_entries, i;
	char name[16];

	regs_dir = debugfs_create_dir(READ_STR, dir);
	if (!regs_dir)
		return;

	nr_entries = ARRAY_SIZE(debugfs_regs);
	dw_edma_debugfs_create_x32(debugfs_regs, nr_entries, regs_dir);

	if (dw->mf == EDMA_MF_HDMA_COMPAT) {
		nr_entries = ARRAY_SIZE(debugfs_unroll_regs);
		dw_edma_debugfs_create_x32(debugfs_unroll_regs, nr_entries,
					   regs_dir);
	}

	for (i = 0; i < dw->rd_ch_cnt; i++) {
		snprintf(name, sizeof(name), "%s:%d", CHANNEL_STR, i);

		ch_dir = debugfs_create_dir(name, regs_dir);
		if (!ch_dir)
			return;

		dw_edma_debugfs_regs_ch(&regs->type.unroll.ch[i].rd, ch_dir);

		lim[1][i].start = &regs->type.unroll.ch[i].rd;
		lim[1][i].end = &regs->type.unroll.ch[i].padding_2[0];
	}
}

static void dw_edma_debugfs_regs(void)
{
	const struct debugfs_entries debugfs_regs[] = {
		REGISTER(ctrl_data_arb_prior),
		REGISTER(ctrl),
	};
	struct dentry *regs_dir;
	int nr_entries;

	regs_dir = debugfs_create_dir(REGISTERS_STR, dw->debugfs);
	if (!regs_dir)
		return;

	nr_entries = ARRAY_SIZE(debugfs_regs);
	dw_edma_debugfs_create_x32(debugfs_regs, nr_entries, regs_dir);

	dw_edma_debugfs_regs_wr(regs_dir);
	dw_edma_debugfs_regs_rd(regs_dir);
}

void dw_edma_v0_debugfs_on(struct dw_edma_chip *chip)
{
	dw = chip->dw;
	if (!dw)
		return;

	regs = dw->rg_region.vaddr;
	if (!regs)
		return;

	dw->debugfs = debugfs_create_dir(dw->name, NULL);
	if (!dw->debugfs)
		return;

	debugfs_create_u32("mf", 0444, dw->debugfs, &dw->mf);
	debugfs_create_u16("wr_ch_cnt", 0444, dw->debugfs, &dw->wr_ch_cnt);
	debugfs_create_u16("rd_ch_cnt", 0444, dw->debugfs, &dw->rd_ch_cnt);

	dw_edma_debugfs_regs();
}

void dw_edma_v0_debugfs_off(struct dw_edma_chip *chip)
{
	dw = chip->dw;
	if (!dw)
		return;

	debugfs_remove_recursive(dw->debugfs);
	dw->debugfs = NULL;
}
