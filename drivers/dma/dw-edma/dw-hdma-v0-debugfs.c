// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 Cai Huoqing
 * Synopsys DesignWare HDMA v0 debugfs
 *
 * Author: Cai Huoqing <cai.huoqing@linux.dev>
 */

#include <linux/debugfs.h>
#include <linux/bitfield.h>

#include "dw-hdma-v0-debugfs.h"
#include "dw-hdma-v0-regs.h"
#include "dw-edma-core.h"

#define REGS_ADDR(dw, name)						       \
	({								       \
		struct dw_hdma_v0_regs __iomem *__regs = (dw)->chip->reg_base; \
									       \
		(void __iomem *)&__regs->name;				       \
	})

#define REGS_CH_ADDR(dw, name, _dir, _ch)				       \
	({								       \
		struct dw_hdma_v0_ch_regs __iomem *__ch_regs;		       \
									       \
		if (_dir == EDMA_DIR_READ)				       \
			__ch_regs = REGS_ADDR(dw, ch[_ch].rd);		       \
		else							       \
			__ch_regs = REGS_ADDR(dw, ch[_ch].wr);		       \
									       \
		(void __iomem *)&__ch_regs->name;			       \
	})

#define CTX_REGISTER(dw, name, dir, ch) \
	{#name, REGS_CH_ADDR(dw, name, dir, ch)}

#define WRITE_STR				"write"
#define READ_STR				"read"
#define CHANNEL_STR				"channel"
#define REGISTERS_STR				"registers"

struct dw_hdma_debugfs_entry {
	const char				*name;
	void __iomem				*reg;
};

static int dw_hdma_debugfs_u32_get(void *data, u64 *val)
{
	struct dw_hdma_debugfs_entry *entry = data;
	void __iomem *reg = entry->reg;

	*val = readl(reg);

	return 0;
}
DEFINE_DEBUGFS_ATTRIBUTE(fops_x32, dw_hdma_debugfs_u32_get, NULL, "0x%08llx\n");

static void dw_hdma_debugfs_create_x32(struct dw_edma *dw,
				       const struct dw_hdma_debugfs_entry ini[],
				       int nr_entries, struct dentry *dent)
{
	struct dw_hdma_debugfs_entry *entries;
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

static void dw_hdma_debugfs_regs_ch(struct dw_edma *dw, enum dw_edma_dir dir,
				    u16 ch, struct dentry *dent)
{
	const struct dw_hdma_debugfs_entry debugfs_regs[] = {
		CTX_REGISTER(dw, ch_en, dir, ch),
		CTX_REGISTER(dw, doorbell, dir, ch),
		CTX_REGISTER(dw, prefetch, dir, ch),
		CTX_REGISTER(dw, handshake, dir, ch),
		CTX_REGISTER(dw, llp.lsb, dir, ch),
		CTX_REGISTER(dw, llp.msb, dir, ch),
		CTX_REGISTER(dw, cycle_sync, dir, ch),
		CTX_REGISTER(dw, transfer_size, dir, ch),
		CTX_REGISTER(dw, sar.lsb, dir, ch),
		CTX_REGISTER(dw, sar.msb, dir, ch),
		CTX_REGISTER(dw, dar.lsb, dir, ch),
		CTX_REGISTER(dw, dar.msb, dir, ch),
		CTX_REGISTER(dw, watermark_en, dir, ch),
		CTX_REGISTER(dw, control1, dir, ch),
		CTX_REGISTER(dw, func_num, dir, ch),
		CTX_REGISTER(dw, qos, dir, ch),
		CTX_REGISTER(dw, ch_stat, dir, ch),
		CTX_REGISTER(dw, int_stat, dir, ch),
		CTX_REGISTER(dw, int_setup, dir, ch),
		CTX_REGISTER(dw, int_clear, dir, ch),
		CTX_REGISTER(dw, msi_stop.lsb, dir, ch),
		CTX_REGISTER(dw, msi_stop.msb, dir, ch),
		CTX_REGISTER(dw, msi_watermark.lsb, dir, ch),
		CTX_REGISTER(dw, msi_watermark.msb, dir, ch),
		CTX_REGISTER(dw, msi_abort.lsb, dir, ch),
		CTX_REGISTER(dw, msi_abort.msb, dir, ch),
		CTX_REGISTER(dw, msi_msgdata, dir, ch),
	};
	int nr_entries = ARRAY_SIZE(debugfs_regs);

	dw_hdma_debugfs_create_x32(dw, debugfs_regs, nr_entries, dent);
}

static void dw_hdma_debugfs_regs_wr(struct dw_edma *dw, struct dentry *dent)
{
	struct dentry *regs_dent, *ch_dent;
	char name[16];
	int i;

	regs_dent = debugfs_create_dir(WRITE_STR, dent);

	for (i = 0; i < dw->wr_ch_cnt; i++) {
		snprintf(name, sizeof(name), "%s:%d", CHANNEL_STR, i);

		ch_dent = debugfs_create_dir(name, regs_dent);

		dw_hdma_debugfs_regs_ch(dw, EDMA_DIR_WRITE, i, ch_dent);
	}
}

static void dw_hdma_debugfs_regs_rd(struct dw_edma *dw, struct dentry *dent)
{
	struct dentry *regs_dent, *ch_dent;
	char name[16];
	int i;

	regs_dent = debugfs_create_dir(READ_STR, dent);

	for (i = 0; i < dw->rd_ch_cnt; i++) {
		snprintf(name, sizeof(name), "%s:%d", CHANNEL_STR, i);

		ch_dent = debugfs_create_dir(name, regs_dent);

		dw_hdma_debugfs_regs_ch(dw, EDMA_DIR_READ, i, ch_dent);
	}
}

static void dw_hdma_debugfs_regs(struct dw_edma *dw)
{
	struct dentry *regs_dent;

	regs_dent = debugfs_create_dir(REGISTERS_STR, dw->dma.dbg_dev_root);

	dw_hdma_debugfs_regs_wr(dw, regs_dent);
	dw_hdma_debugfs_regs_rd(dw, regs_dent);
}

void dw_hdma_v0_debugfs_on(struct dw_edma *dw)
{
	if (!debugfs_initialized())
		return;

	debugfs_create_u32("mf", 0444, dw->dma.dbg_dev_root, &dw->chip->mf);
	debugfs_create_u16("wr_ch_cnt", 0444, dw->dma.dbg_dev_root, &dw->wr_ch_cnt);
	debugfs_create_u16("rd_ch_cnt", 0444, dw->dma.dbg_dev_root, &dw->rd_ch_cnt);

	dw_hdma_debugfs_regs(dw);
}
