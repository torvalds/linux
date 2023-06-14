// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2021 Rockchip Electronics Co., Ltd
 *
 * author:
 *	Herman Chen <herman.chen@rock-chips.com>
 */

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <soc/rockchip/pm_domains.h>
#include <soc/rockchip/rockchip_dmc.h>
#include <soc/rockchip/rockchip_iommu.h>

#include "mpp_rkvdec2_link.h"

#include "hack/mpp_rkvdec2_link_hack_rk3568.c"

#define WORK_TIMEOUT_MS		(500)
#define WAIT_TIMEOUT_MS		(2000)
#define RKVDEC2_LINK_HACK_TASK_FLAG	(0xff)

/* vdpu381 link hw info for rk3588 */
struct rkvdec_link_info rkvdec_link_v2_hw_info = {
	.tb_reg_num = 218,
	.tb_reg_next = 0,
	.tb_reg_r = 1,
	.tb_reg_second_en = 8,

	.part_w_num = 6,
	.part_r_num = 2,
	.part_w[0] = {
		.tb_reg_off = 4,
		.reg_start = 8,
		.reg_num = 28,
	},
	.part_w[1] = {
		.tb_reg_off = 32,
		.reg_start = 64,
		.reg_num = 52,
	},
	.part_w[2] = {
		.tb_reg_off = 84,
		.reg_start = 128,
		.reg_num = 16,
	},
	.part_w[3] = {
		.tb_reg_off = 100,
		.reg_start = 160,
		.reg_num = 48,
	},
	.part_w[4] = {
		.tb_reg_off = 148,
		.reg_start = 224,
		.reg_num = 16,
	},
	.part_w[5] = {
		.tb_reg_off = 164,
		.reg_start = 256,
		.reg_num = 16,
	},
	.part_r[0] = {
		.tb_reg_off = 180,
		.reg_start = 224,
		.reg_num = 10,
	},
	.part_r[1] = {
		.tb_reg_off = 190,
		.reg_start = 258,
		.reg_num = 28,
	},
	.tb_reg_int = 180,
	.tb_reg_cycle = 195,
	.hack_setup = 0,
	.reg_status = {
		.dec_num_mask = 0x3fffffff,
		.err_flag_base = 0x010,
		.err_flag_bit = BIT(31),
	},
};

/* vdpu34x link hw info for rk356x */
struct rkvdec_link_info rkvdec_link_rk356x_hw_info = {
	.tb_reg_num = 202,
	.tb_reg_next = 0,
	.tb_reg_r = 1,
	.tb_reg_second_en = 8,

	.part_w_num = 6,
	.part_r_num = 2,
	.part_w[0] = {
		.tb_reg_off = 4,
		.reg_start = 8,
		.reg_num = 20,
	},
	.part_w[1] = {
		.tb_reg_off = 24,
		.reg_start = 64,
		.reg_num = 52,
	},
	.part_w[2] = {
		.tb_reg_off = 76,
		.reg_start = 128,
		.reg_num = 16,
	},
	.part_w[3] = {
		.tb_reg_off = 92,
		.reg_start = 160,
		.reg_num = 40,
	},
	.part_w[4] = {
		.tb_reg_off = 132,
		.reg_start = 224,
		.reg_num = 16,
	},
	.part_w[5] = {
		.tb_reg_off = 148,
		.reg_start = 256,
		.reg_num = 16,
	},
	.part_r[0] = {
		.tb_reg_off = 164,
		.reg_start = 224,
		.reg_num = 10,
	},
	.part_r[1] = {
		.tb_reg_off = 174,
		.reg_start = 258,
		.reg_num = 28,
	},
	.tb_reg_int = 164,
	.tb_reg_cycle = 179,
	.hack_setup = 1,
	.reg_status = {
		.dec_num_mask = 0x3fffffff,
		.err_flag_base = 0x010,
		.err_flag_bit = BIT(31),
	},
};

/* vdpu382 link hw info */
struct rkvdec_link_info rkvdec_link_vdpu382_hw_info = {
	.tb_reg_num = 222,
	.tb_reg_next = 0,
	.tb_reg_r = 1,
	.tb_reg_second_en = 8,

	.part_w_num = 6,
	.part_r_num = 2,
	.part_w[0] = {
		.tb_reg_off = 4,
		.reg_start = 8,
		.reg_num = 28,
	},
	.part_w[1] = {
		.tb_reg_off = 32,
		.reg_start = 64,
		.reg_num = 52,
	},
	.part_w[2] = {
		.tb_reg_off = 84,
		.reg_start = 128,
		.reg_num = 16,
	},
	.part_w[3] = {
		.tb_reg_off = 100,
		.reg_start = 160,
		.reg_num = 48,
	},
	.part_w[4] = {
		.tb_reg_off = 148,
		.reg_start = 224,
		.reg_num = 16,
	},
	.part_w[5] = {
		.tb_reg_off = 164,
		.reg_start = 256,
		.reg_num = 16,
	},
	.part_r[0] = {
		.tb_reg_off = 180,
		.reg_start = 224,
		.reg_num = 12,
	},
	.part_r[1] = {
		.tb_reg_off = 192,
		.reg_start = 258,
		.reg_num = 30,
	},
	.tb_reg_int = 180,
	.hack_setup = 0,
	.tb_reg_cycle = 197,
	.reg_status = {
		.dec_num_mask = 0x000fffff,
		.err_flag_base = 0x024,
		.err_flag_bit = BIT(8),
	},
};

static void rkvdec2_link_free_task(struct kref *ref);
static void rkvdec2_link_timeout_proc(struct work_struct *work_s);
static int rkvdec2_link_iommu_fault_handle(struct iommu_domain *iommu,
					   struct device *iommu_dev,
					   unsigned long iova,
					   int status, void *arg);

static void rkvdec_link_status_update(struct rkvdec_link_dev *dev)
{
	void __iomem *reg_base = dev->reg_base;
	u32 error_ff0, error_ff1;
	u32 enable_ff0, enable_ff1;
	u32 loop_count = 10;
	u32 val;
	struct rkvdec_link_info *link_info = dev->info;
	u32 dec_num_mask = link_info->reg_status.dec_num_mask;
	u32 err_flag_base = link_info->reg_status.err_flag_base;
	u32 err_flag_bit = link_info->reg_status.err_flag_bit;

	error_ff1 = (readl(reg_base + err_flag_base) & err_flag_bit) ? 1 : 0;
	enable_ff1 = readl(reg_base + RKVDEC_LINK_EN_BASE);

	dev->irq_status = readl(reg_base + RKVDEC_LINK_IRQ_BASE);
	dev->iova_curr = readl(reg_base + RKVDEC_LINK_CFG_ADDR_BASE);
	dev->link_mode = readl(reg_base + RKVDEC_LINK_MODE_BASE);
	dev->total = readl(reg_base + RKVDEC_LINK_TOTAL_NUM_BASE);
	dev->iova_next = readl(reg_base + RKVDEC_LINK_NEXT_ADDR_BASE);

	do {
		val = readl(reg_base + RKVDEC_LINK_DEC_NUM_BASE);
		error_ff0 = (readl(reg_base + err_flag_base) & err_flag_bit) ? 1 : 0;
		enable_ff0 = readl(reg_base + RKVDEC_LINK_EN_BASE);

		if (error_ff0 == error_ff1 && enable_ff0 == enable_ff1)
			break;

		error_ff1 = error_ff0;
		enable_ff1 = enable_ff0;
	} while (--loop_count);

	dev->error = error_ff0;
	dev->decoded_status = val;
	dev->decoded = val & dec_num_mask;
	dev->enabled = enable_ff0;

	if (!loop_count)
		dev_info(dev->dev, "reach last 10 count\n");
}

static void rkvdec_link_node_dump(const char *func, struct rkvdec_link_dev *dev)
{
	u32 *table_base = (u32 *)dev->table->vaddr;
	u32 reg_count = dev->link_reg_count;
	u32 iova = (u32)dev->table->iova;
	u32 *reg = NULL;
	u32 i, j;

	for (i = 0; i < dev->task_capacity; i++) {
		reg = table_base + i * reg_count;

		mpp_err("slot %d link config iova %08x:\n", i,
			iova + i * dev->link_node_size);

		for (j = 0; j < reg_count; j++) {
			mpp_err("reg%03d 0x%08x\n", j, reg[j]);
			udelay(100);
		}
	}
}

static void rkvdec_core_reg_dump(const char *func, struct rkvdec_link_dev *dev)
{
	struct mpp_dev *mpp = dev->mpp;
	u32 s = mpp->var->hw_info->reg_start;
	u32 e = mpp->var->hw_info->reg_end;
	u32 i;

	mpp_err("--- dump hardware register ---\n");

	for (i = s; i <= e; i++) {
		u32 reg = i * sizeof(u32);

		mpp_err("reg[%03d]: %04x: 0x%08x\n",
			i, reg, readl_relaxed(mpp->reg_base + reg));
		udelay(100);
	}
}

static void rkvdec_link_reg_dump(const char *func, struct rkvdec_link_dev *dev)
{
	mpp_err("dump link config status from %s\n", func);
	mpp_err("reg 0 %08x - irq status\n", dev->irq_status);
	mpp_err("reg 1 %08x - cfg addr\n", dev->iova_curr);
	mpp_err("reg 2 %08x - link mode\n", dev->link_mode);
	mpp_err("reg 4 %08x - decoded num\n", dev->decoded_status);
	mpp_err("reg 5 %08x - total num\n", dev->total);
	mpp_err("reg 6 %08x - link mode en\n", dev->enabled);
	mpp_err("reg 6 %08x - next ltb addr\n", dev->iova_next);
}

static void rkvdec_link_counter(const char *func, struct rkvdec_link_dev *dev)
{
	mpp_err("dump link counter from %s\n", func);

	mpp_err("task pending %d running %d\n",
		atomic_read(&dev->task_pending), dev->task_running);
}

int rkvdec_link_dump(struct mpp_dev *mpp)
{
	struct rkvdec2_dev *dec = to_rkvdec2_dev(mpp);
	struct rkvdec_link_dev *dev = dec->link_dec;

	rkvdec_link_status_update(dev);
	rkvdec_link_reg_dump(__func__, dev);
	rkvdec_link_counter(__func__, dev);
	rkvdec_core_reg_dump(__func__, dev);
	rkvdec_link_node_dump(__func__, dev);

	return 0;
}

static void rkvdec2_clear_cache(struct mpp_dev *mpp)
{
	/* set cache size */
	u32 reg = RKVDEC_CACHE_PERMIT_CACHEABLE_ACCESS |
		  RKVDEC_CACHE_PERMIT_READ_ALLOCATE;

	if (!mpp_debug_unlikely(DEBUG_CACHE_32B))
		reg |= RKVDEC_CACHE_LINE_SIZE_64_BYTES;

	mpp_write_relaxed(mpp, RKVDEC_REG_CACHE0_SIZE_BASE, reg);
	mpp_write_relaxed(mpp, RKVDEC_REG_CACHE1_SIZE_BASE, reg);
	mpp_write_relaxed(mpp, RKVDEC_REG_CACHE2_SIZE_BASE, reg);

	/* clear cache */
	mpp_write_relaxed(mpp, RKVDEC_REG_CLR_CACHE0_BASE, 1);
	mpp_write_relaxed(mpp, RKVDEC_REG_CLR_CACHE1_BASE, 1);
	mpp_write_relaxed(mpp, RKVDEC_REG_CLR_CACHE2_BASE, 1);
}

static int rkvdec2_link_enqueue(struct rkvdec_link_dev *link_dec,
				struct mpp_task *mpp_task)
{
	void __iomem *reg_base = link_dec->reg_base;
	struct rkvdec2_task *task = to_rkvdec2_task(mpp_task);
	struct mpp_dma_buffer *table = task->table;
	u32 link_en = 0;
	u32 frame_num = 1;
	u32 link_mode;
	u32 timing_en = link_dec->mpp->srv->timing_en;

	link_en = readl(reg_base + RKVDEC_LINK_EN_BASE);
	if (!link_en) {
		rkvdec2_clear_cache(link_dec->mpp);
		/* cleanup counter in hardware */
		writel(0, reg_base + RKVDEC_LINK_MODE_BASE);
		/* start config before all registers are set */
		wmb();
		writel(RKVDEC_LINK_BIT_CFG_DONE, reg_base + RKVDEC_LINK_CFG_CTRL_BASE);
		/* write zero count config */
		wmb();
		/* clear counter and enable link mode hardware */
		writel(RKVDEC_LINK_BIT_EN, reg_base + RKVDEC_LINK_EN_BASE);
		writel_relaxed(table->iova, reg_base + RKVDEC_LINK_CFG_ADDR_BASE);
		link_mode = frame_num;
	} else
		link_mode = (frame_num | RKVDEC_LINK_BIT_ADD_MODE);

	/* set link mode */
	writel_relaxed(link_mode, reg_base + RKVDEC_LINK_MODE_BASE);

	/* start config before all registers are set */
	wmb();

	mpp_iommu_flush_tlb(link_dec->mpp->iommu_info);
	mpp_task_run_begin(mpp_task, timing_en, MPP_WORK_TIMEOUT_DELAY);

	link_dec->task_running++;
	/* configure done */
	writel(RKVDEC_LINK_BIT_CFG_DONE, reg_base + RKVDEC_LINK_CFG_CTRL_BASE);
	if (!link_en) {
		/* start hardware before all registers are set */
		wmb();
		/* clear counter and enable link mode hardware */
		writel(RKVDEC_LINK_BIT_EN, reg_base + RKVDEC_LINK_EN_BASE);
	}
	mpp_task_run_end(mpp_task, timing_en);

	return 0;
}

static int rkvdec2_link_finish(struct mpp_dev *mpp, struct mpp_task *mpp_task)
{
	struct rkvdec2_dev *dec = to_rkvdec2_dev(mpp);
	struct rkvdec2_task *task = to_rkvdec2_task(mpp_task);
	struct rkvdec_link_dev *link_dec = dec->link_dec;
	struct mpp_dma_buffer *table = link_dec->table;
	struct rkvdec_link_info *info = link_dec->info;
	struct rkvdec_link_part *part = info->part_r;
	u32 *tb_reg = (u32 *)table->vaddr;
	u32 off, s, n;
	u32 i;

	mpp_debug_enter();

	for (i = 0; i < info->part_r_num; i++) {
		off = part[i].tb_reg_off;
		s = part[i].reg_start;
		n = part[i].reg_num;
		memcpy(&task->reg[s], &tb_reg[off], n * sizeof(u32));
	}
	/* revert hack for irq status */
	task->reg[RKVDEC_REG_INT_EN_INDEX] = task->irq_status;

	mpp_debug_leave();

	return 0;
}

static void *rkvdec2_link_prepare(struct mpp_dev *mpp,
				  struct mpp_task *mpp_task)
{
	struct rkvdec2_dev *dec = to_rkvdec2_dev(mpp);
	struct rkvdec_link_dev *link_dec = dec->link_dec;
	struct mpp_dma_buffer *table = NULL;
	struct rkvdec_link_part *part;
	struct rkvdec2_task *task = to_rkvdec2_task(mpp_task);
	struct rkvdec_link_info *info = link_dec->info;
	u32 i, off, s, n;
	u32 *tb_reg;

	mpp_debug_enter();

	if (test_bit(TASK_STATE_PREPARE, &mpp_task->state)) {
		dev_err(mpp->dev, "task %d has prepared\n", mpp_task->task_index);
		return mpp_task;
	}

	table = list_first_entry_or_null(&link_dec->unused_list, struct mpp_dma_buffer, link);

	if (!table)
		return NULL;

	/* fill regs value */
	tb_reg = (u32 *)table->vaddr;
	part = info->part_w;
	for (i = 0; i < info->part_w_num; i++) {
		off = part[i].tb_reg_off;
		s = part[i].reg_start;
		n = part[i].reg_num;
		memcpy(&tb_reg[off], &task->reg[s], n * sizeof(u32));
	}

	/* setup error mode flag */
	tb_reg[9] |= BIT(18) | BIT(9);
	tb_reg[info->tb_reg_second_en] |= RKVDEC_WAIT_RESET_EN;

	/* memset read registers */
	part = info->part_r;
	for (i = 0; i < info->part_r_num; i++) {
		off = part[i].tb_reg_off;
		n = part[i].reg_num;
		memset(&tb_reg[off], 0, n * sizeof(u32));
	}

	list_move_tail(&table->link, &link_dec->used_list);
	task->table = table;
	set_bit(TASK_STATE_PREPARE, &mpp_task->state);

	mpp_dbg_link("session %d task %d prepare pending %d running %d\n",
		     mpp_task->session->index, mpp_task->task_index,
		     atomic_read(&link_dec->task_pending), link_dec->task_running);
	mpp_debug_leave();

	return mpp_task;
}

static int rkvdec2_link_reset(struct mpp_dev *mpp)
{

	dev_info(mpp->dev, "resetting...\n");

	disable_irq(mpp->irq);
	mpp_iommu_disable_irq(mpp->iommu_info);

	/* FIXME lock resource lock of the other devices in combo */
	mpp_iommu_down_write(mpp->iommu_info);
	mpp_reset_down_write(mpp->reset_group);
	atomic_set(&mpp->reset_request, 0);

	rockchip_save_qos(mpp->dev);

	if (mpp->hw_ops->reset)
		mpp->hw_ops->reset(mpp);

	rockchip_restore_qos(mpp->dev);

	/* Note: if the domain does not change, iommu attach will be return
	 * as an empty operation. Therefore, force to close and then open,
	 * will be update the domain. In this way, domain can really attach.
	 */
	mpp_iommu_refresh(mpp->iommu_info, mpp->dev);

	mpp_reset_up_write(mpp->reset_group);
	mpp_iommu_up_write(mpp->iommu_info);

	enable_irq(mpp->irq);
	mpp_iommu_enable_irq(mpp->iommu_info);
	dev_info(mpp->dev, "reset done\n");

	return 0;
}

static int rkvdec2_link_irq(struct mpp_dev *mpp)
{
	struct rkvdec2_dev *dec = to_rkvdec2_dev(mpp);
	struct rkvdec_link_dev *link_dec = dec->link_dec;
	u32 irq_status = 0;

	if (!atomic_read(&link_dec->power_enabled)) {
		dev_info(link_dec->dev, "irq on power off\n");
		return -1;
	}

	irq_status = readl(link_dec->reg_base + RKVDEC_LINK_IRQ_BASE);

	if (irq_status & RKVDEC_LINK_BIT_IRQ_RAW) {
		u32 enabled = readl(link_dec->reg_base + RKVDEC_LINK_EN_BASE);

		if (!enabled) {
			u32 bus = mpp_read_relaxed(mpp, 273 * 4);

			if (bus & 0x7ffff)
				dev_info(link_dec->dev,
					 "invalid bus status %08x\n", bus);
		}

		link_dec->irq_status = irq_status;
		mpp->irq_status = mpp_read_relaxed(mpp, RKVDEC_REG_INT_EN);

		writel_relaxed(0, link_dec->reg_base + RKVDEC_LINK_IRQ_BASE);
	}

	mpp_debug(DEBUG_IRQ_STATUS | DEBUG_LINK_TABLE, "irq_status: %08x : %08x\n",
		  irq_status, mpp->irq_status);

	return 0;
}

int rkvdec2_link_remove(struct mpp_dev *mpp, struct rkvdec_link_dev *link_dec)
{
	mpp_debug_enter();

	if (link_dec && link_dec->table) {
		mpp_dma_free(link_dec->table);
		link_dec->table = NULL;
	}

	mpp_debug_leave();

	return 0;
}

static int rkvdec2_link_alloc_table(struct mpp_dev *mpp,
				    struct rkvdec_link_dev *link_dec)
{
	int ret;
	struct mpp_dma_buffer *table;
	struct rkvdec_link_info *info = link_dec->info;
	/* NOTE: link table address requires 64 align */
	u32 task_capacity = link_dec->task_capacity;
	u32 link_node_size = ALIGN(info->tb_reg_num * sizeof(u32), 256);
	u32 link_info_size = task_capacity * link_node_size;
	u32 *v_curr;
	u32 io_curr, io_next, io_start;
	u32 offset_r = info->part_r[0].tb_reg_off * sizeof(u32);
	u32 i;

	table = mpp_dma_alloc(mpp->dev, link_info_size);
	if (!table) {
		ret = -ENOMEM;
		goto err_free_node;
	}

	link_dec->link_node_size = link_node_size;
	link_dec->link_reg_count = link_node_size >> 2;
	io_start = table->iova;

	for (i = 0; i < task_capacity; i++) {
		v_curr  = (u32 *)(table->vaddr + i * link_node_size);
		io_curr = io_start + i * link_node_size;
		io_next = (i == task_capacity - 1) ?
			  io_start : io_start + (i + 1) * link_node_size;

		v_curr[info->tb_reg_next] = io_next;
		v_curr[info->tb_reg_r] = io_curr + offset_r;
	}

	link_dec->table	     = table;

	return 0;
err_free_node:
	rkvdec2_link_remove(mpp, link_dec);
	return ret;
}

#ifdef CONFIG_ROCKCHIP_MPP_PROC_FS
int rkvdec2_link_procfs_init(struct mpp_dev *mpp)
{
	struct rkvdec2_dev *dec = to_rkvdec2_dev(mpp);
	struct rkvdec_link_dev *link_dec = dec->link_dec;

	if (!link_dec)
		return 0;

	link_dec->statistic_count = 0;

	if (dec->procfs)
		mpp_procfs_create_u32("statistic_count", 0644,
				      dec->procfs, &link_dec->statistic_count);

	return 0;
}
#else
int rkvdec2_link_procfs_init(struct mpp_dev *mpp)
{
	return 0;
}
#endif

int rkvdec2_link_init(struct platform_device *pdev, struct rkvdec2_dev *dec)
{
	int ret;
	struct resource *res = NULL;
	struct rkvdec_link_dev *link_dec = NULL;
	struct device *dev = &pdev->dev;
	struct mpp_dev *mpp = &dec->mpp;
	struct mpp_dma_buffer *table;
	int i;

	mpp_debug_enter();

	link_dec = devm_kzalloc(dev, sizeof(*link_dec), GFP_KERNEL);
	if (!link_dec) {
		ret = -ENOMEM;
		goto done;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "link");
	if (res)
		link_dec->info = mpp->var->hw_info->link_info;
	else {
		dev_err(dev, "link mode resource not found\n");
		ret = -ENOMEM;
		goto done;
	}

	link_dec->reg_base = devm_ioremap(dev, res->start, resource_size(res));
	if (!link_dec->reg_base) {
		dev_err(dev, "ioremap failed for resource %pR\n", res);
		ret = -ENOMEM;
		goto done;
	}

	link_dec->task_capacity = mpp->task_capacity;
	ret = rkvdec2_link_alloc_table(&dec->mpp, link_dec);
	if (ret)
		goto done;

	/* alloc table pointer array */
	table = devm_kmalloc_array(mpp->dev, mpp->task_capacity,
				   sizeof(*table), GFP_KERNEL | __GFP_ZERO);
	if (!table)
		return -ENOMEM;

	/* init table array */
	link_dec->table_array = table;
	INIT_LIST_HEAD(&link_dec->used_list);
	INIT_LIST_HEAD(&link_dec->unused_list);
	for (i = 0; i < mpp->task_capacity; i++) {
		table[i].iova = link_dec->table->iova + i * link_dec->link_node_size;
		table[i].vaddr = link_dec->table->vaddr + i * link_dec->link_node_size;
		table[i].size = link_dec->link_node_size;
		INIT_LIST_HEAD(&table[i].link);
		list_add_tail(&table[i].link, &link_dec->unused_list);
	}

	if (dec->fix)
		rkvdec2_link_hack_data_setup(dec->fix);

	mpp->fault_handler = rkvdec2_link_iommu_fault_handle;

	link_dec->mpp = mpp;
	link_dec->dev = dev;
	atomic_set(&link_dec->task_timeout, 0);
	atomic_set(&link_dec->task_pending, 0);
	atomic_set(&link_dec->power_enabled, 0);
	link_dec->irq_enabled = 1;

	dec->link_dec = link_dec;
	dev_info(dev, "link mode probe finish\n");

done:
	if (ret) {
		if (link_dec) {
			if (link_dec->reg_base) {
				devm_iounmap(dev, link_dec->reg_base);
				link_dec->reg_base = NULL;
			}
			devm_kfree(dev, link_dec);
			link_dec = NULL;
		}
		dec->link_dec = NULL;
	}
	mpp_debug_leave();

	return ret;
}

static void rkvdec2_link_free_task(struct kref *ref)
{
	struct mpp_dev *mpp;
	struct mpp_session *session;
	struct mpp_task *task = container_of(ref, struct mpp_task, ref);

	if (!task->session) {
		mpp_err("task %d task->session is null.\n", task->task_id);
		return;
	}
	session = task->session;

	mpp_debug_func(DEBUG_TASK_INFO, "task %d:%d state 0x%lx\n",
		       session->index, task->task_id, task->state);
	if (!session->mpp) {
		mpp_err("session %d session->mpp is null.\n", session->index);
		return;
	}
	mpp = session->mpp;
	list_del_init(&task->queue_link);

	rkvdec2_free_task(session, task);
	/* Decrease reference count */
	atomic_dec(&session->task_count);
	atomic_dec(&mpp->task_count);
}

static void rkvdec2_link_trigger_work(struct mpp_dev *mpp)
{
	kthread_queue_work(&mpp->queue->worker, &mpp->work);
}

static int rkvdec2_link_power_on(struct mpp_dev *mpp)
{
	struct rkvdec2_dev *dec = to_rkvdec2_dev(mpp);
	struct rkvdec_link_dev *link_dec = dec->link_dec;

	if (!atomic_xchg(&link_dec->power_enabled, 1)) {
		if (mpp_iommu_attach(mpp->iommu_info)) {
			dev_err(mpp->dev, "mpp_iommu_attach failed\n");
			return -ENODATA;
		}
		pm_runtime_get_sync(mpp->dev);
		pm_stay_awake(mpp->dev);

		if (mpp->hw_ops->clk_on)
			mpp->hw_ops->clk_on(mpp);

		if (!link_dec->irq_enabled) {
			enable_irq(mpp->irq);
			mpp_iommu_enable_irq(mpp->iommu_info);
			link_dec->irq_enabled = 1;
		}

		mpp_clk_set_rate(&dec->aclk_info, CLK_MODE_ADVANCED);
		mpp_clk_set_rate(&dec->cabac_clk_info, CLK_MODE_ADVANCED);
		mpp_clk_set_rate(&dec->hevc_cabac_clk_info, CLK_MODE_ADVANCED);
		mpp_devfreq_set_core_rate(mpp, CLK_MODE_ADVANCED);
		mpp_iommu_dev_activate(mpp->iommu_info, mpp);
	}
	return 0;
}

static void rkvdec2_link_power_off(struct mpp_dev *mpp)
{
	struct rkvdec2_dev *dec = to_rkvdec2_dev(mpp);
	struct rkvdec_link_dev *link_dec = dec->link_dec;

	if (atomic_xchg(&link_dec->power_enabled, 0)) {
		disable_irq(mpp->irq);
		mpp_iommu_disable_irq(mpp->iommu_info);
		link_dec->irq_enabled = 0;

		if (mpp->hw_ops->clk_off)
			mpp->hw_ops->clk_off(mpp);

		pm_relax(mpp->dev);
		pm_runtime_put_sync_suspend(mpp->dev);

		mpp_clk_set_rate(&dec->aclk_info, CLK_MODE_NORMAL);
		mpp_clk_set_rate(&dec->cabac_clk_info, CLK_MODE_NORMAL);
		mpp_clk_set_rate(&dec->hevc_cabac_clk_info, CLK_MODE_NORMAL);
		mpp_devfreq_set_core_rate(mpp, CLK_MODE_NORMAL);
		mpp_iommu_dev_deactivate(mpp->iommu_info, mpp);
	}
}

static void rkvdec2_link_timeout_proc(struct work_struct *work_s)
{
	struct mpp_dev *mpp;
	struct rkvdec2_dev *dec;
	struct mpp_session *session;
	struct mpp_task *task = container_of(to_delayed_work(work_s),
					     struct mpp_task, timeout_work);

	if (test_and_set_bit(TASK_STATE_HANDLE, &task->state)) {
		mpp_err("task %d state %lx has been handled\n",
			task->task_id, task->state);
		return;
	}

	if (!task->session) {
		mpp_err("task %d session is null.\n", task->task_id);
		return;
	}
	session = task->session;

	if (!session->mpp) {
		mpp_err("task %d:%d mpp is null.\n", session->index,
			task->task_id);
		return;
	}
	mpp = session->mpp;
	set_bit(TASK_STATE_TIMEOUT, &task->state);

	dec = to_rkvdec2_dev(mpp);
	atomic_inc(&dec->link_dec->task_timeout);

	dev_err(mpp->dev, "session %d task %d state %#lx timeout, cnt %d\n",
		session->index, task->task_index, task->state,
		atomic_read(&dec->link_dec->task_timeout));

	rkvdec2_link_trigger_work(mpp);
}

static int rkvdec2_link_iommu_fault_handle(struct iommu_domain *iommu,
					    struct device *iommu_dev,
					    unsigned long iova,
					    int status, void *arg)
{
	struct mpp_dev *mpp = (struct mpp_dev *)arg;
	struct rkvdec2_dev *dec = to_rkvdec2_dev(mpp);
	struct mpp_task *mpp_task = NULL, *n;
	struct mpp_taskqueue *queue;

	dev_err(iommu_dev, "fault addr 0x%08lx status %x arg %p\n",
		iova, status, arg);

	if (!mpp) {
		dev_err(iommu_dev, "pagefault without device to handle\n");
		return 0;
	}
	queue = mpp->queue;
	list_for_each_entry_safe(mpp_task, n, &queue->running_list, queue_link) {
		struct rkvdec_link_info *info = dec->link_dec->info;
		struct rkvdec2_task *task = to_rkvdec2_task(mpp_task);
		u32 *tb_reg = (u32 *)task->table->vaddr;
		u32 irq_status = tb_reg[info->tb_reg_int];

		if (!irq_status) {
			mpp_task_dump_mem_region(mpp, mpp_task);
			break;
		}
	}

	mpp_task_dump_hw_reg(mpp);
	/*
	 * Mask iommu irq, in order for iommu not repeatedly trigger pagefault.
	 * Until the pagefault task finish by hw timeout.
	 */
	rockchip_iommu_mask_irq(mpp->dev);
	dec->mmu_fault = 1;

	return 0;
}

static void rkvdec2_link_resend(struct mpp_dev *mpp)
{
	struct rkvdec2_dev *dec = to_rkvdec2_dev(mpp);
	struct rkvdec_link_dev *link_dec = dec->link_dec;
	struct mpp_taskqueue *queue = mpp->queue;
	struct mpp_task *mpp_task, *n;

	link_dec->task_running = 0;
	list_for_each_entry_safe(mpp_task, n, &queue->running_list, queue_link) {
		dev_err(mpp->dev, "resend task %d\n", mpp_task->task_index);
		cancel_delayed_work_sync(&mpp_task->timeout_work);
		clear_bit(TASK_STATE_TIMEOUT, &mpp_task->state);
		clear_bit(TASK_STATE_HANDLE, &mpp_task->state);
		rkvdec2_link_enqueue(link_dec, mpp_task);
	}
}

static void rkvdec2_link_try_dequeue(struct mpp_dev *mpp)
{
	struct rkvdec2_dev *dec = to_rkvdec2_dev(mpp);
	struct rkvdec_link_dev *link_dec = dec->link_dec;
	struct mpp_taskqueue *queue = mpp->queue;
	struct mpp_task *mpp_task = NULL, *n;
	struct rkvdec_link_info *info = link_dec->info;
	u32 reset_flag = 0;
	u32 iommu_fault = dec->mmu_fault && (mpp->irq_status & RKVDEC_TIMEOUT_STA);
	u32 link_en = atomic_read(&link_dec->power_enabled) ?
		      readl(link_dec->reg_base + RKVDEC_LINK_EN_BASE) : 0;
	u32 force_dequeue = iommu_fault || !link_en;
	u32 dequeue_cnt = 0;

	list_for_each_entry_safe(mpp_task, n, &queue->running_list, queue_link) {
		/*
		 * Because there are multiple tasks enqueue at the same time,
		 * soft timeout may be triggered at the same time, but in reality only
		 * first task is being timeout because of the hardware stuck,
		 * so only process the first task.
		 */
		u32 timeout_flag = dequeue_cnt ? 0 : test_bit(TASK_STATE_TIMEOUT, &mpp_task->state);
		struct rkvdec2_task *task = to_rkvdec2_task(mpp_task);
		u32 *tb_reg = (u32 *)task->table->vaddr;
		u32 abort_flag = test_bit(TASK_STATE_ABORT, &mpp_task->state);
		u32 irq_status = tb_reg[info->tb_reg_int];
		u32 task_done = irq_status || timeout_flag || abort_flag;

		/*
		 * there are some cases will cause hw cannot write reg to ddr:
		 * 1. iommu pagefault
		 * 2. link stop(link_en == 0) because of err task, it is a rk356x issue.
		 * so need force dequeue one task.
		 */
		if (force_dequeue)
			task_done = 1;

		if (!task_done)
			break;

		dequeue_cnt++;
		/* check hack task only for rk356x*/
		if (task->need_hack == RKVDEC2_LINK_HACK_TASK_FLAG) {
			cancel_delayed_work_sync(&mpp_task->timeout_work);
			list_move_tail(&task->table->link, &link_dec->unused_list);
			list_del_init(&mpp_task->queue_link);
			link_dec->task_running--;
			link_dec->hack_task_running--;
			kfree(task);
			mpp_dbg_link("hack running %d irq_status %#08x timeout %d abort %d\n",
				     link_dec->hack_task_running, irq_status,
				     timeout_flag, abort_flag);
			continue;
		}

		/*
		 * if timeout/abort/force dequeue found, reset and stop hw first.
		 */
		if ((timeout_flag || abort_flag || force_dequeue) && !reset_flag) {
			dev_err(mpp->dev, "session %d task %d timeout %d abort %d force_dequeue %d\n",
				mpp_task->session->index, mpp_task->task_index,
				timeout_flag, abort_flag, force_dequeue);
			rkvdec2_link_reset(mpp);
			reset_flag = 1;
			dec->mmu_fault = 0;
			mpp->irq_status = 0;
			force_dequeue = 0;
		}

		cancel_delayed_work_sync(&mpp_task->timeout_work);

		task->irq_status = irq_status;
		mpp_task->hw_cycles = tb_reg[info->tb_reg_cycle];
		mpp_time_diff_with_hw_time(mpp_task, dec->cycle_clk->real_rate_hz);
		rkvdec2_link_finish(mpp, mpp_task);

		list_move_tail(&task->table->link, &link_dec->unused_list);
		list_del_init(&mpp_task->queue_link);

		set_bit(TASK_STATE_HANDLE, &mpp_task->state);
		set_bit(TASK_STATE_PROC_DONE, &mpp_task->state);
		set_bit(TASK_STATE_FINISH, &mpp_task->state);
		set_bit(TASK_STATE_DONE, &mpp_task->state);
		if (test_bit(TASK_STATE_ABORT, &mpp_task->state))
			set_bit(TASK_STATE_ABORT_READY, &mpp_task->state);

		wake_up(&mpp_task->wait);
		kref_put(&mpp_task->ref, rkvdec2_link_free_task);
		link_dec->task_running--;

		mpp_dbg_link("session %d task %d irq_status %#08x timeout %d abort %d\n",
			     mpp_task->session->index, mpp_task->task_index,
			     irq_status, timeout_flag, abort_flag);
		if (irq_status & RKVDEC_INT_ERROR_MASK) {
			dev_err(mpp->dev,
				"session %d task %d irq_status %#08x timeout %u abort %u\n",
				mpp_task->session->index, mpp_task->task_index,
				irq_status, timeout_flag, abort_flag);
			if (!reset_flag)
				atomic_inc(&mpp->reset_request);
		}
	}

	/* resend running task after reset */
	if (reset_flag && !list_empty(&queue->running_list))
		rkvdec2_link_resend(mpp);
}

static int mpp_task_queue(struct mpp_dev *mpp, struct mpp_task *mpp_task)
{
	struct rkvdec2_dev *dec = to_rkvdec2_dev(mpp);
	struct rkvdec_link_dev *link_dec = dec->link_dec;
	struct mpp_taskqueue *queue = mpp->queue;
	struct rkvdec2_task *task = to_rkvdec2_task(mpp_task);

	mpp_debug_enter();

	rkvdec2_link_power_on(mpp);

	/* hack for rk356x */
	if (task->need_hack) {
		u32 *tb_reg;
		struct mpp_dma_buffer *table;
		struct rkvdec2_task *hack_task;
		struct rkvdec_link_info *info = link_dec->info;

		/* need reserved 2 unused task for need hack task */
		if (link_dec->task_running > (link_dec->task_capacity - 2))
			return -EBUSY;

		table = list_first_entry_or_null(&link_dec->unused_list,
						 struct mpp_dma_buffer,
						 link);
		if (!table)
			return -EBUSY;

		hack_task = kzalloc(sizeof(*hack_task), GFP_KERNEL);

		if (!hack_task)
			return -ENOMEM;

		mpp_task_init(mpp_task->session, &hack_task->mpp_task);
		INIT_DELAYED_WORK(&hack_task->mpp_task.timeout_work,
					rkvdec2_link_timeout_proc);

		tb_reg = (u32 *)table->vaddr;
		memset(tb_reg + info->part_r[0].tb_reg_off, 0, info->part_r[0].reg_num);
		rkvdec2_3568_hack_fix_link(tb_reg + 4);
		list_move_tail(&table->link, &link_dec->used_list);
		hack_task->table = table;
		hack_task->need_hack = RKVDEC2_LINK_HACK_TASK_FLAG;
		rkvdec2_link_enqueue(link_dec, &hack_task->mpp_task);
		mpp_taskqueue_pending_to_run(queue, &hack_task->mpp_task);
		link_dec->hack_task_running++;
		mpp_dbg_link("hack task send to hw, hack running %d\n",
			     link_dec->hack_task_running);
	}

	/* process normal */
	if (!rkvdec2_link_prepare(mpp, mpp_task))
		return -EBUSY;

	rkvdec2_link_enqueue(link_dec, mpp_task);

	set_bit(TASK_STATE_RUNNING, &mpp_task->state);
	atomic_dec(&link_dec->task_pending);
	mpp_taskqueue_pending_to_run(queue, mpp_task);

	mpp_dbg_link("session %d task %d send to hw pending %d running %d\n",
		     mpp_task->session->index, mpp_task->task_index,
		     atomic_read(&link_dec->task_pending), link_dec->task_running);
	mpp_debug_leave();

	return 0;
}

irqreturn_t rkvdec2_link_irq_proc(int irq, void *param)
{
	struct mpp_dev *mpp = param;
	int ret = rkvdec2_link_irq(mpp);

	if (!ret)
		rkvdec2_link_trigger_work(mpp);

	return IRQ_HANDLED;
}

static struct mpp_task *
mpp_session_get_pending_task(struct mpp_session *session)
{
	struct mpp_task *task = NULL;

	mutex_lock(&session->pending_lock);
	task = list_first_entry_or_null(&session->pending_list, struct mpp_task,
					pending_link);
	mutex_unlock(&session->pending_lock);

	return task;
}

static int task_is_done(struct mpp_task *task)
{
	return test_bit(TASK_STATE_PROC_DONE, &task->state);
}

static int mpp_session_pop_pending(struct mpp_session *session,
				   struct mpp_task *task)
{
	mutex_lock(&session->pending_lock);
	list_del_init(&task->pending_link);
	mutex_unlock(&session->pending_lock);
	kref_put(&task->ref, rkvdec2_link_free_task);

	return 0;
}

static int mpp_session_pop_done(struct mpp_session *session,
				struct mpp_task *task)
{
	set_bit(TASK_STATE_DONE, &task->state);

	return 0;
}

int rkvdec2_link_process_task(struct mpp_session *session,
			      struct mpp_task_msgs *msgs)
{
	struct mpp_task *task = NULL;
	struct mpp_dev *mpp = session->mpp;
	struct rkvdec_link_info *link_info = mpp->var->hw_info->link_info;
	struct rkvdec2_dev *dec = to_rkvdec2_dev(mpp);
	struct rkvdec_link_dev *link_dec = dec->link_dec;

	task = rkvdec2_alloc_task(session, msgs);
	if (!task) {
		mpp_err("alloc_task failed.\n");
		return -ENOMEM;
	}

	if (link_info->hack_setup) {
		u32 fmt;
		struct rkvdec2_task *dec_task = NULL;

		dec_task = to_rkvdec2_task(task);
		fmt = RKVDEC_GET_FORMAT(dec_task->reg[RKVDEC_REG_FORMAT_INDEX]);
		dec_task->need_hack = (fmt == RKVDEC_FMT_H264D);
	}

	kref_init(&task->ref);
	atomic_set(&task->abort_request, 0);
	task->task_index = atomic_fetch_inc(&mpp->task_index);
	task->task_id = atomic_fetch_inc(&mpp->queue->task_id);
	INIT_DELAYED_WORK(&task->timeout_work, rkvdec2_link_timeout_proc);

	atomic_inc(&session->task_count);

	kref_get(&task->ref);
	mutex_lock(&session->pending_lock);
	list_add_tail(&task->pending_link, &session->pending_list);
	mutex_unlock(&session->pending_lock);

	kref_get(&task->ref);
	mutex_lock(&mpp->queue->pending_lock);
	list_add_tail(&task->queue_link, &mpp->queue->pending_list);
	mutex_unlock(&mpp->queue->pending_lock);
	atomic_inc(&link_dec->task_pending);

	/* push current task to queue */
	atomic_inc(&mpp->task_count);
	set_bit(TASK_STATE_PENDING, &task->state);
	/* trigger current queue to run task */
	rkvdec2_link_trigger_work(mpp);
	kref_put(&task->ref, rkvdec2_link_free_task);

	return 0;
}

int rkvdec2_link_wait_result(struct mpp_session *session,
			     struct mpp_task_msgs *msgs)
{
	struct mpp_dev *mpp = session->mpp;
	struct mpp_task *mpp_task;
	int ret;

	mpp_task = mpp_session_get_pending_task(session);
	if (!mpp_task) {
		mpp_err("session %p pending list is empty!\n", session);
		return -EIO;
	}

	ret = wait_event_timeout(mpp_task->wait, task_is_done(mpp_task),
				 msecs_to_jiffies(WAIT_TIMEOUT_MS));
	if (ret) {
		ret = rkvdec2_result(mpp, mpp_task, msgs);

		mpp_session_pop_done(session, mpp_task);
	} else {
		mpp_err("task %d:%d state %lx timeout -> abort\n",
			session->index, mpp_task->task_id, mpp_task->state);

		atomic_inc(&mpp_task->abort_request);
		set_bit(TASK_STATE_ABORT, &mpp_task->state);
	}

	mpp_session_pop_pending(session, mpp_task);
	return ret;
}

void rkvdec2_link_worker(struct kthread_work *work_s)
{
	struct mpp_dev *mpp = container_of(work_s, struct mpp_dev, work);
	struct mpp_task *task;
	struct mpp_taskqueue *queue = mpp->queue;
	u32 all_done;

	mpp_debug_enter();

	/* dequeue running task */
	rkvdec2_link_try_dequeue(mpp);

	/* process reset */
	if (atomic_read(&mpp->reset_request)) {
		rkvdec2_link_reset(mpp);
		/* resend running task after reset */
		if (!list_empty(&queue->running_list))
			rkvdec2_link_resend(mpp);
	}

again:
	/* get pending task to process */
	mutex_lock(&queue->pending_lock);
	task = list_first_entry_or_null(&queue->pending_list, struct mpp_task,
					queue_link);
	mutex_unlock(&queue->pending_lock);
	if (!task)
		goto done;

	/* check abort task */
	if (atomic_read(&task->abort_request)) {
		mutex_lock(&queue->pending_lock);
		list_del_init(&task->queue_link);

		set_bit(TASK_STATE_ABORT_READY, &task->state);
		set_bit(TASK_STATE_PROC_DONE, &task->state);

		mutex_unlock(&queue->pending_lock);
		wake_up(&task->wait);
		kref_put(&task->ref, rkvdec2_link_free_task);
		goto again;
	}

	/* queue task to hw */
	if (!mpp_task_queue(mpp, task))
		goto again;

done:

	/* if no task in pending and running list, power off device */
	mutex_lock(&queue->pending_lock);
	all_done = list_empty(&queue->pending_list) && list_empty(&queue->running_list);
	mutex_unlock(&queue->pending_lock);

	if (all_done)
		rkvdec2_link_power_off(mpp);

	mpp_session_cleanup_detach(queue, work_s);

	mpp_debug_leave();
}

void rkvdec2_link_session_deinit(struct mpp_session *session)
{
	struct mpp_dev *mpp = session->mpp;

	mpp_debug_enter();

	rkvdec2_free_session(session);

	if (session->dma) {
		mpp_dbg_session("session %d destroy dma\n", session->index);
		mpp_iommu_down_write(mpp->iommu_info);
		mpp_dma_session_destroy(session->dma);
		mpp_iommu_up_write(mpp->iommu_info);
		session->dma = NULL;
	}
	if (session->srv) {
		struct mpp_service *srv = session->srv;

		mutex_lock(&srv->session_lock);
		list_del_init(&session->service_link);
		mutex_unlock(&srv->session_lock);
	}
	list_del_init(&session->session_link);

	mpp_dbg_session("session %d release\n", session->index);

	mpp_debug_leave();
}

#define RKVDEC2_1080P_PIXELS	(1920*1080)
#define RKVDEC2_4K_PIXELS	(4096*2304)
#define RKVDEC2_8K_PIXELS	(7680*4320)
#define RKVDEC2_CCU_TIMEOUT_20MS	(0xefffff)
#define RKVDEC2_CCU_TIMEOUT_50MS	(0x2cfffff)
#define RKVDEC2_CCU_TIMEOUT_100MS	(0x4ffffff)

static u32 rkvdec2_ccu_get_timeout_threshold(struct rkvdec2_task *task)
{
	u32 pixels = task->pixels;

	if (pixels < RKVDEC2_1080P_PIXELS)
		return RKVDEC2_CCU_TIMEOUT_20MS;
	else if (pixels < RKVDEC2_4K_PIXELS)
		return RKVDEC2_CCU_TIMEOUT_50MS;
	else
		return RKVDEC2_CCU_TIMEOUT_100MS;
}

int rkvdec2_attach_ccu(struct device *dev, struct rkvdec2_dev *dec)
{
	int ret;
	struct device_node *np;
	struct platform_device *pdev;
	struct rkvdec2_ccu *ccu;

	mpp_debug_enter();

	np = of_parse_phandle(dev->of_node, "rockchip,ccu", 0);
	if (!np || !of_device_is_available(np))
		return -ENODEV;

	pdev = of_find_device_by_node(np);
	of_node_put(np);
	if (!pdev)
		return -ENODEV;

	ccu = platform_get_drvdata(pdev);
	if (!ccu)
		return -ENOMEM;

	ret = of_property_read_u32(dev->of_node, "rockchip,core-mask", &dec->core_mask);
	if (ret)
		return ret;
	dev_info(dev, "core_mask=%08x\n", dec->core_mask);

	/* if not the main-core, then attach the main core domain to current */
	if (dec->mpp.core_id != 0) {
		struct mpp_taskqueue *queue;
		struct mpp_iommu_info *ccu_info, *cur_info;

		queue = dec->mpp.queue;
		/* set the ccu-domain for current device */
		ccu_info = queue->cores[0]->iommu_info;
		cur_info = dec->mpp.iommu_info;
		if (cur_info)
			cur_info->domain = ccu_info->domain;
		mpp_iommu_attach(cur_info);
	}

	dec->ccu = ccu;

	dev_info(dev, "attach ccu as core %d\n", dec->mpp.core_id);
	mpp_debug_enter();

	return 0;
}

static void rkvdec2_ccu_timeout_work(struct work_struct *work_s)
{
	struct mpp_dev *mpp;
	struct mpp_task *task = container_of(to_delayed_work(work_s),
					     struct mpp_task, timeout_work);

	if (test_and_set_bit(TASK_STATE_HANDLE, &task->state)) {
		mpp_err("task %d state %lx has been handled\n",
			task->task_id, task->state);
		return;
	}

	if (!task->session) {
		mpp_err("task %d session is null.\n", task->task_id);
		return;
	}
	mpp = mpp_get_task_used_device(task, task->session);
	mpp_err("%s, task %d state %#lx timeout\n", dev_name(mpp->dev),
		task->task_index, task->state);
	set_bit(TASK_STATE_TIMEOUT, &task->state);
	atomic_inc(&mpp->reset_request);
	atomic_inc(&mpp->queue->reset_request);
	kthread_queue_work(&mpp->queue->worker, &mpp->work);
}

int rkvdec2_ccu_link_init(struct platform_device *pdev, struct rkvdec2_dev *dec)
{
	struct resource *res;
	struct rkvdec_link_dev *link_dec;
	struct device *dev = &pdev->dev;

	mpp_debug_enter();

	/* link structure */
	link_dec = devm_kzalloc(dev, sizeof(*link_dec), GFP_KERNEL);
	if (!link_dec)
		return -ENOMEM;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "link");
	if (!res)
		return -ENOMEM;

	link_dec->info = dec->mpp.var->hw_info->link_info;
	link_dec->reg_base = devm_ioremap(dev, res->start, resource_size(res));
	if (!link_dec->reg_base) {
		dev_err(dev, "ioremap failed for resource %pR\n", res);
		return -ENOMEM;
	}

	dec->link_dec = link_dec;

	mpp_debug_leave();

	return 0;
}

static int rkvdec2_ccu_power_on(struct mpp_taskqueue *queue,
				struct rkvdec2_ccu *ccu)
{
	if (!atomic_xchg(&ccu->power_enabled, 1)) {
		u32 i;
		struct mpp_dev *mpp;

		/* ccu pd and clk on */
		pm_runtime_get_sync(ccu->dev);
		pm_stay_awake(ccu->dev);
		mpp_clk_safe_enable(ccu->aclk_info.clk);
		/* core pd and clk on */
		for (i = 0; i < queue->core_count; i++) {
			struct rkvdec2_dev *dec;

			mpp = queue->cores[i];
			dec = to_rkvdec2_dev(mpp);
			pm_runtime_get_sync(mpp->dev);
			pm_stay_awake(mpp->dev);
			if (mpp->hw_ops->clk_on)
				mpp->hw_ops->clk_on(mpp);

			mpp_clk_set_rate(&dec->aclk_info, CLK_MODE_NORMAL);
			mpp_clk_set_rate(&dec->cabac_clk_info, CLK_MODE_NORMAL);
			mpp_clk_set_rate(&dec->hevc_cabac_clk_info, CLK_MODE_NORMAL);
			mpp_devfreq_set_core_rate(mpp, CLK_MODE_NORMAL);
			mpp_iommu_dev_activate(mpp->iommu_info, mpp);
		}
		mpp_debug(DEBUG_CCU, "power on\n");
	}

	return 0;
}

static int rkvdec2_ccu_power_off(struct mpp_taskqueue *queue,
				 struct rkvdec2_ccu *ccu)
{
	if (atomic_xchg(&ccu->power_enabled, 0)) {
		u32 i;
		struct mpp_dev *mpp;

		/* ccu pd and clk off */
		mpp_clk_safe_disable(ccu->aclk_info.clk);
		pm_relax(ccu->dev);
		pm_runtime_mark_last_busy(ccu->dev);
		pm_runtime_put_autosuspend(ccu->dev);
		/* core pd and clk off */
		for (i = 0; i < queue->core_count; i++) {
			mpp = queue->cores[i];

			if (mpp->hw_ops->clk_off)
				mpp->hw_ops->clk_off(mpp);
			pm_relax(mpp->dev);
			pm_runtime_mark_last_busy(mpp->dev);
			pm_runtime_put_autosuspend(mpp->dev);
			mpp_iommu_dev_deactivate(mpp->iommu_info, mpp);
		}
		mpp_debug(DEBUG_CCU, "power off\n");
	}

	return 0;
}

static int rkvdec2_soft_ccu_dequeue(struct mpp_taskqueue *queue)
{
	struct mpp_task *mpp_task = NULL, *n;

	mpp_debug_enter();

	list_for_each_entry_safe(mpp_task, n,
				 &queue->running_list,
				 queue_link) {
		struct mpp_dev *mpp = mpp_get_task_used_device(mpp_task, mpp_task->session);
		struct rkvdec2_dev *dec = to_rkvdec2_dev(mpp);
		u32 irq_status = mpp->irq_status;
		u32 timeout_flag = test_bit(TASK_STATE_TIMEOUT, &mpp_task->state);
		u32 abort_flag = test_bit(TASK_STATE_ABORT, &mpp_task->state);
		u32 timing_en = mpp->srv->timing_en;

		if (irq_status || timeout_flag || abort_flag) {
			struct rkvdec2_task *task = to_rkvdec2_task(mpp_task);

			if (timing_en) {
				mpp_task->on_irq = ktime_get();
				set_bit(TASK_TIMING_IRQ, &mpp_task->state);

				mpp_task->on_cancel_timeout = mpp_task->on_irq;
				set_bit(TASK_TIMING_TO_CANCEL, &mpp_task->state);

				mpp_task->on_isr = mpp_task->on_irq;
				set_bit(TASK_TIMING_ISR, &mpp_task->state);
			}

			set_bit(TASK_STATE_HANDLE, &mpp_task->state);
			cancel_delayed_work(&mpp_task->timeout_work);
			mpp_task->hw_cycles = mpp_read(mpp, RKVDEC_PERF_WORKING_CNT);
			mpp_time_diff_with_hw_time(mpp_task, dec->cycle_clk->real_rate_hz);
			task->irq_status = irq_status;
			mpp_debug(DEBUG_IRQ_CHECK, "irq_status=%08x, timeout=%u, abort=%u\n",
				  irq_status, timeout_flag, abort_flag);
			if (irq_status && mpp->dev_ops->finish)
				mpp->dev_ops->finish(mpp, mpp_task);
			else
				task->reg[RKVDEC_REG_INT_EN_INDEX] = RKVDEC_TIMEOUT_STA;

			set_bit(TASK_STATE_FINISH, &mpp_task->state);
			set_bit(TASK_STATE_DONE, &mpp_task->state);

			set_bit(mpp->core_id, &queue->core_idle);
			mpp_dbg_core("set core %d idle %lx\n", mpp->core_id, queue->core_idle);
			/* Wake up the GET thread */
			wake_up(&mpp_task->wait);
			/* free task */
			list_del_init(&mpp_task->queue_link);
			kref_put(&mpp_task->ref, mpp_free_task);
		} else {
			/* NOTE: break when meet not finish */
			break;
		}
	}

	mpp_debug_leave();
	return 0;
}

static int rkvdec2_soft_ccu_reset(struct mpp_taskqueue *queue,
				  struct rkvdec2_ccu *ccu)
{
	int i;

	for (i = queue->core_count - 1; i >= 0; i--) {
		u32 val;

		struct mpp_dev *mpp = queue->cores[i];
		struct rkvdec2_dev *dec = to_rkvdec2_dev(mpp);

		if (mpp->disable)
			continue;

		dev_info(mpp->dev, "resetting...\n");
		disable_hardirq(mpp->irq);

		/* foce idle, disconnect core and ccu */
		writel(dec->core_mask, ccu->reg_base + RKVDEC_CCU_CORE_IDLE_BASE);

		/* soft reset */
		mpp_write(mpp, RKVDEC_REG_IMPORTANT_BASE, RKVDEC_SOFTREST_EN);
		udelay(5);
		val = mpp_read(mpp, RKVDEC_REG_INT_EN);
		if (!(val & RKVDEC_SOFT_RESET_READY))
			mpp_err("soft reset fail, int %08x\n", val);
		mpp_write(mpp, RKVDEC_REG_INT_EN, 0);

		/* check bus idle */
		val = mpp_read(mpp, RKVDEC_REG_DEBUG_INT_BASE);
		if (!(val & RKVDEC_BIT_BUS_IDLE))
			mpp_err("bus busy\n");

		if (IS_REACHABLE(CONFIG_ROCKCHIP_SIP)) {
			/* sip reset */
			rockchip_dmcfreq_lock();
			sip_smc_vpu_reset(i, 0, 0);
			rockchip_dmcfreq_unlock();
		} else {
			rkvdec2_reset(mpp);
		}
		/* clear error mask */
		writel(dec->core_mask & RKVDEC_CCU_CORE_RW_MASK,
		       ccu->reg_base + RKVDEC_CCU_CORE_ERR_BASE);
		/* connect core and ccu */
		writel(dec->core_mask & RKVDEC_CCU_CORE_RW_MASK,
		       ccu->reg_base + RKVDEC_CCU_CORE_IDLE_BASE);
		mpp_iommu_refresh(mpp->iommu_info, mpp->dev);
		atomic_set(&mpp->reset_request, 0);

		enable_irq(mpp->irq);
		dev_info(mpp->dev, "reset done\n");
	}
	atomic_set(&queue->reset_request, 0);

	return 0;
}

void *rkvdec2_ccu_alloc_task(struct mpp_session *session,
			     struct mpp_task_msgs *msgs)
{
	int ret;
	struct rkvdec2_task *task;

	task = kzalloc(sizeof(*task), GFP_KERNEL);
	if (!task)
		return NULL;

	ret = rkvdec2_task_init(session->mpp, session, task, msgs);
	if (ret) {
		kfree(task);
		return NULL;
	}

	return &task->mpp_task;
}

static void rkvdec2_ccu_check_pagefault_info(struct mpp_dev *mpp)
{
	u32 i = 0;

	for (i = 0; i < mpp->queue->core_count; i++) {
		struct mpp_dev *core = mpp->queue->cores[i];
		struct rkvdec2_dev *dec = to_rkvdec2_dev(core);
		void __iomem *mmu_base = dec->mmu_base;
		u32 mmu0_st;
		u32 mmu1_st;
		u32 mmu0_pta;
		u32 mmu1_pta;

		if (!mmu_base)
			return;

		#define FAULT_STATUS 0x7e2
		rkvdec2_ccu_power_on(mpp->queue, dec->ccu);

		mmu0_st = readl(mmu_base + 0x4);
		mmu1_st = readl(mmu_base + 0x44);
		mmu0_pta = readl(mmu_base + 0xc);
		mmu1_pta = readl(mmu_base + 0x4c);

		dec->mmu0_st = mmu0_st;
		dec->mmu1_st = mmu1_st;
		dec->mmu0_pta = mmu0_pta;
		dec->mmu1_pta = mmu1_pta;

		pr_err("core %d mmu0 %08x %08x mm1 %08x %08x\n",
			core->core_id, mmu0_st, mmu0_pta, mmu1_st, mmu1_pta);
		if ((mmu0_st & FAULT_STATUS) || (mmu1_st & FAULT_STATUS) ||
		    mmu0_pta || mmu1_pta) {
			dec->fault_iova = readl(dec->link_dec->reg_base + 0x4);
			dec->mmu_fault = 1;
			pr_err("core %d fault iova %08x\n", core->core_id, dec->fault_iova);
			rockchip_iommu_mask_irq(core->dev);
		} else {
			dec->mmu_fault = 0;
			dec->fault_iova = 0;
		}
	}
}

int rkvdec2_ccu_iommu_fault_handle(struct iommu_domain *iommu,
				   struct device *iommu_dev,
				   unsigned long iova, int status, void *arg)
{
	struct mpp_dev *mpp = (struct mpp_dev *)arg;

	mpp_debug_enter();

	rkvdec2_ccu_check_pagefault_info(mpp);

	mpp->queue->iommu_fault = 1;
	atomic_inc(&mpp->queue->reset_request);
	kthread_queue_work(&mpp->queue->worker, &mpp->work);

	mpp_debug_leave();

	return 0;
}

irqreturn_t rkvdec2_soft_ccu_irq(int irq, void *param)
{
	struct mpp_dev *mpp = param;
	u32 irq_status = mpp_read_relaxed(mpp, RKVDEC_REG_INT_EN);

	if (irq_status & RKVDEC_IRQ_RAW) {
		mpp_debug(DEBUG_IRQ_STATUS, "irq_status=%08x\n", irq_status);
		if (irq_status & RKVDEC_INT_ERROR_MASK) {
			atomic_inc(&mpp->reset_request);
			atomic_inc(&mpp->queue->reset_request);
		}
		mpp_write(mpp, RKVDEC_REG_INT_EN, 0);
		mpp->irq_status = irq_status;
		kthread_queue_work(&mpp->queue->worker, &mpp->work);
		return IRQ_HANDLED;
	}
	return IRQ_NONE;
}

static inline int rkvdec2_set_core_info(u32 *reg, int idx)
{
	u32 val = (idx << 16) & RKVDEC_REG_FILM_IDX_MASK;

	reg[RKVDEC_REG_CORE_CTRL_INDEX] &= ~RKVDEC_REG_FILM_IDX_MASK;

	reg[RKVDEC_REG_CORE_CTRL_INDEX] |= val;

	return 0;
}

static int rkvdec2_soft_ccu_enqueue(struct mpp_dev *mpp, struct mpp_task *mpp_task)
{
	u32 i, reg_en, reg;
	struct rkvdec2_dev *dec = to_rkvdec2_dev(mpp);
	struct rkvdec2_task *task = to_rkvdec2_task(mpp_task);
	u32 timing_en = mpp->srv->timing_en;

	mpp_debug_enter();

	/* set reg for link */
	reg = RKVDEC_LINK_BIT_CORE_WORK_MODE | RKVDEC_LINK_BIT_CCU_WORK_MODE;
	writel_relaxed(reg, dec->link_dec->reg_base + RKVDEC_LINK_IRQ_BASE);

	/* set reg for ccu */
	writel_relaxed(RKVDEC_CCU_BIT_WORK_EN, dec->ccu->reg_base + RKVDEC_CCU_WORK_BASE);
	writel_relaxed(RKVDEC_CCU_BIT_WORK_MODE, dec->ccu->reg_base + RKVDEC_CCU_WORK_MODE_BASE);
	writel_relaxed(dec->core_mask, dec->ccu->reg_base + RKVDEC_CCU_CORE_WORK_BASE);

	/* set cache size */
	reg = RKVDEC_CACHE_PERMIT_CACHEABLE_ACCESS |
		  RKVDEC_CACHE_PERMIT_READ_ALLOCATE;
	if (!mpp_debug_unlikely(DEBUG_CACHE_32B))
		reg |= RKVDEC_CACHE_LINE_SIZE_64_BYTES;

	mpp_write_relaxed(mpp, RKVDEC_REG_CACHE0_SIZE_BASE, reg);
	mpp_write_relaxed(mpp, RKVDEC_REG_CACHE1_SIZE_BASE, reg);
	mpp_write_relaxed(mpp, RKVDEC_REG_CACHE2_SIZE_BASE, reg);
	/* clear cache */
	mpp_write_relaxed(mpp, RKVDEC_REG_CLR_CACHE0_BASE, 1);
	mpp_write_relaxed(mpp, RKVDEC_REG_CLR_CACHE1_BASE, 1);
	mpp_write_relaxed(mpp, RKVDEC_REG_CLR_CACHE2_BASE, 1);

	mpp_iommu_flush_tlb(mpp->iommu_info);
	/* disable multicore pu/colmv offset req timeout reset */
	task->reg[RKVDEC_REG_EN_MODE_SET] |= BIT(1);
	task->reg[RKVDEC_REG_TIMEOUT_THRESHOLD] = rkvdec2_ccu_get_timeout_threshold(task);
	/* set registers for hardware */
	reg_en = mpp_task->hw_info->reg_en;
	for (i = 0; i < task->w_req_cnt; i++) {
		int s, e;
		struct mpp_request *req = &task->w_reqs[i];

		s = req->offset / sizeof(u32);
		e = s + req->size / sizeof(u32);
		mpp_write_req(mpp, task->reg, s, e, reg_en);
	}
	/* init current task */
	mpp->cur_task = mpp_task;

	mpp_task_run_begin(mpp_task, timing_en, MPP_WORK_TIMEOUT_DELAY);

	mpp->irq_status = 0;
	writel_relaxed(dec->core_mask, dec->ccu->reg_base + RKVDEC_CCU_CORE_STA_BASE);
	/* Flush the register before the start the device */
	wmb();
	mpp_write(mpp, RKVDEC_REG_START_EN_BASE, task->reg[reg_en] | RKVDEC_START_EN);

	mpp_task_run_end(mpp_task, timing_en);

	mpp_debug_leave();

	return 0;
}

static struct mpp_dev *rkvdec2_get_idle_core(struct mpp_taskqueue *queue,
					     struct mpp_task *mpp_task)
{
	u32 i = 0;
	struct rkvdec2_dev *dec = NULL;

	for (i = 0; i < queue->core_count; i++) {
		struct mpp_dev *mpp = queue->cores[i];
		struct rkvdec2_dev *core = to_rkvdec2_dev(mpp);

		if (mpp->disable)
			continue;

		if (test_bit(i, &queue->core_idle)) {
			if (!dec) {
				dec = core;
				continue;
			}
			/* set the less work core */
			if (core->task_index < dec->task_index)
				dec = core;
		}
	}
	/* if get core */
	if (dec) {
		mpp_task->mpp = &dec->mpp;
		mpp_task->core_id = dec->mpp.core_id;
		clear_bit(mpp_task->core_id, &queue->core_idle);
		dec->task_index++;
		atomic_inc(&dec->mpp.task_count);
		mpp_dbg_core("clear core %d idle\n", mpp_task->core_id);
		return mpp_task->mpp;
	}

	return NULL;
}

static bool rkvdec2_core_working(struct mpp_taskqueue *queue)
{
	struct mpp_dev *mpp;
	bool flag = false;
	u32 i = 0;

	for (i = 0; i < queue->core_count; i++) {
		mpp = queue->cores[i];
		if (mpp->disable)
			continue;
		if (!test_bit(i, &queue->core_idle)) {
			flag = true;
			break;
		}
	}

	return flag;
}

static int rkvdec2_ccu_link_session_detach(struct mpp_dev *mpp,
					   struct mpp_taskqueue *queue)
{
	mutex_lock(&queue->session_lock);
	while (atomic_read(&queue->detach_count)) {
		struct mpp_session *session = NULL;

		session = list_first_entry_or_null(&queue->session_detach,
						   struct mpp_session,
						   session_link);
		if (session) {
			list_del_init(&session->session_link);
			atomic_dec(&queue->detach_count);
		}

		mutex_unlock(&queue->session_lock);

		if (session) {
			mpp_dbg_session("%s detach count %d\n", dev_name(mpp->dev),
					atomic_read(&queue->detach_count));
			mpp_session_deinit(session);
		}

		mutex_lock(&queue->session_lock);
	}
	mutex_unlock(&queue->session_lock);

	return 0;
}

void rkvdec2_soft_ccu_worker(struct kthread_work *work_s)
{
	struct mpp_task *mpp_task;
	struct mpp_dev *mpp = container_of(work_s, struct mpp_dev, work);
	struct mpp_taskqueue *queue = mpp->queue;
	struct rkvdec2_dev *dec = to_rkvdec2_dev(mpp);
	u32 timing_en = mpp->srv->timing_en;

	mpp_debug_enter();

	/* 1. process all finished task in running list */
	rkvdec2_soft_ccu_dequeue(queue);

	/* 2. process reset request */
	if (atomic_read(&queue->reset_request)) {
		if (!rkvdec2_core_working(queue)) {
			rkvdec2_ccu_power_on(queue, dec->ccu);
			rkvdec2_soft_ccu_reset(queue, dec->ccu);
		}
	}

	/* 3. process pending task */
	while (1) {
		if (atomic_read(&queue->reset_request))
			break;
		/* get one task form pending list */
		mutex_lock(&queue->pending_lock);
		mpp_task = list_first_entry_or_null(&queue->pending_list,
						struct mpp_task, queue_link);
		mutex_unlock(&queue->pending_lock);
		if (!mpp_task)
			break;

		if (test_bit(TASK_STATE_ABORT, &mpp_task->state)) {
			mutex_lock(&queue->pending_lock);
			list_del_init(&mpp_task->queue_link);

			set_bit(TASK_STATE_ABORT_READY, &mpp_task->state);
			set_bit(TASK_STATE_PROC_DONE, &mpp_task->state);

			mutex_unlock(&queue->pending_lock);
			wake_up(&mpp_task->wait);
			kref_put(&mpp_task->ref, rkvdec2_link_free_task);
			continue;
		}
		/* find one core is idle */
		mpp = rkvdec2_get_idle_core(queue, mpp_task);
		if (!mpp)
			break;

		if (timing_en) {
			mpp_task->on_run = ktime_get();
			set_bit(TASK_TIMING_RUN, &mpp_task->state);
		}

		/* set session index */
		rkvdec2_set_core_info(mpp_task->reg, mpp_task->session->index);
		/* set rcb buffer */
		mpp_set_rcbbuf(mpp, mpp_task->session, mpp_task);

		INIT_DELAYED_WORK(&mpp_task->timeout_work, rkvdec2_ccu_timeout_work);
		rkvdec2_ccu_power_on(queue, dec->ccu);
		rkvdec2_soft_ccu_enqueue(mpp, mpp_task);
		/* pending to running */
		mpp_taskqueue_pending_to_run(queue, mpp_task);
		set_bit(TASK_STATE_RUNNING, &mpp_task->state);
	}

	/* 4. poweroff when running and pending list are empty */
	if (list_empty(&queue->running_list) &&
	    list_empty(&queue->pending_list))
		rkvdec2_ccu_power_off(queue, dec->ccu);

	/* 5. check session detach out of queue */
	rkvdec2_ccu_link_session_detach(mpp, queue);

	mpp_debug_leave();
}

int rkvdec2_ccu_alloc_table(struct rkvdec2_dev *dec,
			    struct rkvdec_link_dev *link_dec)
{
	int ret, i;
	struct mpp_dma_buffer *table;
	struct mpp_dev *mpp = &dec->mpp;

	mpp_debug_enter();

	/* alloc table pointer array */
	table = devm_kmalloc_array(mpp->dev, mpp->task_capacity,
				   sizeof(*table), GFP_KERNEL | __GFP_ZERO);
	if (!table)
		return -ENOMEM;

	/* alloc table buffer */
	ret = rkvdec2_link_alloc_table(mpp, link_dec);
	if (ret)
		return ret;

	/* init table array */
	dec->ccu->table_array = table;
	for (i = 0; i < mpp->task_capacity; i++) {
		table[i].iova = link_dec->table->iova + i * link_dec->link_node_size;
		table[i].vaddr = link_dec->table->vaddr + i * link_dec->link_node_size;
		table[i].size = link_dec->link_node_size;
		INIT_LIST_HEAD(&table[i].link);
		list_add_tail(&table[i].link, &dec->ccu->unused_list);
	}

	return 0;
}

static void rkvdec2_dump_ccu(struct rkvdec2_ccu *ccu)
{
	u32 i;

	for (i = 0; i < 10; i++)
		mpp_err("ccu:reg[%d]=%08x\n", i, readl(ccu->reg_base + 4 * i));

	for (i = 16; i < 22; i++)
		mpp_err("ccu:reg[%d]=%08x\n", i, readl(ccu->reg_base + 4 * i));
}

static void rkvdec2_dump_link(struct rkvdec2_dev *dec)
{
	u32 i;

	for (i = 0; i < 10; i++)
		mpp_err("link:reg[%d]=%08x\n", i, readl(dec->link_dec->reg_base + 4 * i));
}

static void rkvdec2_dump_core(struct mpp_dev *mpp, struct rkvdec2_task *task)
{
	u32 j;

	if (task) {
		for (j = 0; j < 273; j++)
			mpp_err("reg[%d]=%08x, %08x\n", j, mpp_read(mpp, j*4), task->reg[j]);
	} else {
		for (j = 0; j < 273; j++)
			mpp_err("reg[%d]=%08x\n", j, mpp_read(mpp, j*4));
	}
}

irqreturn_t rkvdec2_hard_ccu_irq(int irq, void *param)
{
	u32 irq_status;
	struct mpp_dev *mpp = param;
	struct rkvdec2_dev *dec = to_rkvdec2_dev(mpp);

	irq_status = readl(dec->link_dec->reg_base + RKVDEC_LINK_IRQ_BASE);
	dec->ccu->ccu_core_work_mode = readl(dec->ccu->reg_base + RKVDEC_CCU_CORE_WORK_BASE);
	if (irq_status & RKVDEC_LINK_BIT_IRQ_RAW) {
		dec->link_dec->irq_status = irq_status;
		mpp->irq_status = mpp_read(mpp, RKVDEC_REG_INT_EN);
		mpp_debug(DEBUG_IRQ_STATUS, "core %d link_irq=%08x, core_irq=%08x\n",
			  mpp->core_id, irq_status, mpp->irq_status);

		writel(irq_status & 0xfffff0ff,
		       dec->link_dec->reg_base + RKVDEC_LINK_IRQ_BASE);

		kthread_queue_work(&mpp->queue->worker, &mpp->work);
		return IRQ_HANDLED;
	}

	return IRQ_NONE;
}

static int rkvdec2_hard_ccu_finish(struct rkvdec_link_info *hw, struct rkvdec2_task *task)
{
	u32 i, off, s, n;
	struct rkvdec_link_part *part = hw->part_r;
	u32 *tb_reg = (u32 *)task->table->vaddr;

	mpp_debug_enter();

	for (i = 0; i < hw->part_r_num; i++) {
		off = part[i].tb_reg_off;
		s = part[i].reg_start;
		n = part[i].reg_num;
		memcpy(&task->reg[s], &tb_reg[off], n * sizeof(u32));
	}
	/* revert hack for irq status */
	task->reg[RKVDEC_REG_INT_EN_INDEX] = task->irq_status;

	mpp_debug_leave();

	return 0;
}

static int rkvdec2_hard_ccu_dequeue(struct mpp_taskqueue *queue,
				    struct rkvdec2_ccu *ccu,
				    struct rkvdec_link_info *hw)
{
	struct mpp_task *mpp_task = NULL, *n;
	u32 dump_reg = 0;
	u32 dequeue_none = 0;

	mpp_debug_enter();
	list_for_each_entry_safe(mpp_task, n, &queue->running_list, queue_link) {
		u32 timeout_flag = test_bit(TASK_STATE_TIMEOUT, &mpp_task->state);
		u32 abort_flag = test_bit(TASK_STATE_ABORT, &mpp_task->state);
		struct rkvdec2_task *task = to_rkvdec2_task(mpp_task);
		u32 *tb_reg = (u32 *)task->table->vaddr;
		u32 irq_status = tb_reg[hw->tb_reg_int];
		u32 ccu_decoded_num, ccu_total_dec_num;

		ccu_decoded_num = readl(ccu->reg_base + RKVDEC_CCU_DEC_NUM_BASE);
		ccu_total_dec_num = readl(ccu->reg_base + RKVDEC_CCU_TOTAL_NUM_BASE);
		mpp_debug(DEBUG_IRQ_CHECK,
			  "session %d task %d w:h[%d %d] err %d irq_status %08x timeout=%u abort=%u iova %08x next %08x ccu[%d %d]\n",
			  mpp_task->session->index, mpp_task->task_index, task->width,
			  task->height, !!(irq_status & RKVDEC_INT_ERROR_MASK), irq_status,
			  timeout_flag, abort_flag, (u32)task->table->iova,
			  ((u32 *)task->table->vaddr)[hw->tb_reg_next],
			  ccu_decoded_num, ccu_total_dec_num);

		if (irq_status || timeout_flag || abort_flag) {
			struct rkvdec2_dev *dec = to_rkvdec2_dev(queue->cores[0]);

			set_bit(TASK_STATE_HANDLE, &mpp_task->state);
			cancel_delayed_work(&mpp_task->timeout_work);
			mpp_task->hw_cycles = tb_reg[hw->tb_reg_cycle];
			mpp_time_diff_with_hw_time(mpp_task, dec->cycle_clk->real_rate_hz);
			task->irq_status = irq_status;

			if (irq_status)
				rkvdec2_hard_ccu_finish(hw, task);

			set_bit(TASK_STATE_FINISH, &mpp_task->state);
			set_bit(TASK_STATE_DONE, &mpp_task->state);

			if (timeout_flag && !dump_reg && mpp_debug_unlikely(DEBUG_DUMP_ERR_REG)) {
				u32 i;

				mpp_err("###### ccu #####\n");
				rkvdec2_dump_ccu(ccu);
				for (i = 0; i < queue->core_count; i++) {
					mpp_err("###### core %d #####\n", i);
					rkvdec2_dump_link(to_rkvdec2_dev(queue->cores[i]));
					rkvdec2_dump_core(queue->cores[i], task);
				}
				dump_reg = 1;
			}
			list_move_tail(&task->table->link, &ccu->unused_list);
			/* free task */
			list_del_init(&mpp_task->queue_link);
			/* Wake up the GET thread */
			wake_up(&mpp_task->wait);
			if ((irq_status & RKVDEC_INT_ERROR_MASK) || timeout_flag) {
				pr_err("session %d task %d irq_status %08x timeout=%u abort=%u\n",
					mpp_task->session->index, mpp_task->task_index,
					irq_status, timeout_flag, abort_flag);
				atomic_inc(&queue->reset_request);
			}

			kref_put(&mpp_task->ref, mpp_free_task);
		} else {
			dequeue_none++;
			/*
			 * there are only 2 cores,
			 * if dequeue not finish task more than 2,
			 * means the others task still not get run by hw, can break early.
			 */
			if (dequeue_none > 2)
				break;
		}
	}

	mpp_debug_leave();
	return 0;
}

static int rkvdec2_hard_ccu_reset(struct mpp_taskqueue *queue, struct rkvdec2_ccu *ccu)
{
	int i = 0;

	mpp_debug_enter();

	/* reset and active core */
	for (i = 0; i < queue->core_count; i++) {
		u32 val = 0;
		struct mpp_dev *mpp = queue->cores[i];
		struct rkvdec2_dev *dec = to_rkvdec2_dev(mpp);

		if (mpp->disable)
			continue;
		dev_info(mpp->dev, "resetting...\n");
		disable_hardirq(mpp->irq);
		/* force idle */
		writel(dec->core_mask, ccu->reg_base + RKVDEC_CCU_CORE_IDLE_BASE);
		writel(0, ccu->reg_base + RKVDEC_CCU_WORK_BASE);

		{
			/* soft reset */
			u32 val;

			mpp_write(mpp, RKVDEC_REG_IMPORTANT_BASE, RKVDEC_SOFTREST_EN);
			udelay(5);
			val = mpp_read(mpp, RKVDEC_REG_INT_EN);
			if (!(val & RKVDEC_SOFT_RESET_READY))
				mpp_err("soft reset fail, int %08x\n", val);

			// /* cru reset */
			// dev_info(mpp->dev, "cru reset\n");
			// rkvdec2_reset(mpp);
		}
#if IS_ENABLED(CONFIG_ROCKCHIP_SIP)
		rockchip_dmcfreq_lock();
		sip_smc_vpu_reset(i, 0, 0);
		rockchip_dmcfreq_unlock();
#else
		rkvdec2_reset(mpp);
#endif
		mpp_iommu_refresh(mpp->iommu_info, mpp->dev);
		enable_irq(mpp->irq);
		atomic_set(&mpp->reset_request, 0);
		val = mpp_read_relaxed(mpp, 272*4);
		dev_info(mpp->dev, "reset done, idle %d\n", (val & 1));
	}
	/* reset ccu */
	mpp_safe_reset(ccu->rst_a);
	udelay(5);
	mpp_safe_unreset(ccu->rst_a);

	mpp_debug_leave();
	return 0;
}

static struct mpp_task *
rkvdec2_hard_ccu_prepare(struct mpp_task *mpp_task,
			 struct rkvdec2_ccu *ccu, struct rkvdec_link_info *hw)
{
	u32 i, off, s, n;
	u32 *tb_reg;
	struct mpp_dma_buffer *table = NULL;
	struct rkvdec_link_part *part;
	struct rkvdec2_task *task = to_rkvdec2_task(mpp_task);

	mpp_debug_enter();

	if (test_bit(TASK_STATE_PREPARE, &mpp_task->state))
		return mpp_task;

	/* ensure that cur table iova points to the next link table*/
	{
		struct mpp_dma_buffer *table0 = NULL, *table1 = NULL, *n;

		list_for_each_entry_safe(table, n, &ccu->unused_list, link) {
			if (!table0) {
				table0 = table;
				continue;
			}
			if (!table1)
				table1 = table;
			break;
		}
		if (!table0 || !table1)
			return NULL;
		((u32 *)table0->vaddr)[hw->tb_reg_next] = table1->iova;
		table = table0;
	}

	/* set session idx */
	rkvdec2_set_core_info(task->reg, mpp_task->session->index);
	tb_reg = (u32 *)table->vaddr;
	part = hw->part_w;

	/* disable multicore pu/colmv offset req timeout reset */
	task->reg[RKVDEC_REG_EN_MODE_SET] |= BIT(1);
	task->reg[RKVDEC_REG_TIMEOUT_THRESHOLD] = rkvdec2_ccu_get_timeout_threshold(task);

	for (i = 0; i < hw->part_w_num; i++) {
		off = part[i].tb_reg_off;
		s = part[i].reg_start;
		n = part[i].reg_num;
		memcpy(&tb_reg[off], &task->reg[s], n * sizeof(u32));
	}

	/* memset read registers */
	part = hw->part_r;
	for (i = 0; i < hw->part_r_num; i++) {
		off = part[i].tb_reg_off;
		n = part[i].reg_num;
		memset(&tb_reg[off], 0, n * sizeof(u32));
	}
	list_move_tail(&table->link, &ccu->used_list);
	task->table = table;
	set_bit(TASK_STATE_PREPARE, &mpp_task->state);
	mpp_dbg_ccu("session %d task %d iova %08x next %08x\n",
		    mpp_task->session->index, mpp_task->task_index, (u32)task->table->iova,
		    ((u32 *)task->table->vaddr)[hw->tb_reg_next]);

	mpp_debug_leave();

	return mpp_task;
}

static int rkvdec2_ccu_link_fix_rcb_regs(struct rkvdec2_dev *dec)
{
	int ret = 0;
	u32 i, val;
	u32 reg, reg_idx, rcb_size, rcb_offset;

	if (!dec->rcb_iova && !dec->rcb_info_count)
		goto done;
	/* check whether fixed */
	val = readl(dec->link_dec->reg_base + RKVDEC_LINK_IRQ_BASE);
	if (val & RKVDEC_CCU_BIT_FIX_RCB)
		goto done;
	/* set registers */
	rcb_offset = 0;
	for (i = 0; i < dec->rcb_info_count; i += 2) {
		reg_idx = dec->rcb_infos[i];
		rcb_size = dec->rcb_infos[i + 1];
		mpp_debug(DEBUG_SRAM_INFO,
			  "rcb: reg %u size %u offset %u sram_size %u rcb_size %u\n",
			  reg_idx, rcb_size, rcb_offset, dec->sram_size, dec->rcb_size);
		if ((rcb_offset + rcb_size) > dec->rcb_size) {
			mpp_err("rcb: reg[%u] set failed.\n", reg_idx);
			ret = -ENOMEM;
			goto done;
		}
		reg = dec->rcb_iova + rcb_offset;
		mpp_write(&dec->mpp, reg_idx * sizeof(u32), reg);
		rcb_offset += rcb_size;
	}

	val |= RKVDEC_CCU_BIT_FIX_RCB;
	writel(val, dec->link_dec->reg_base + RKVDEC_LINK_IRQ_BASE);
done:
	return ret;
}

static int rkvdec2_hard_ccu_enqueue(struct rkvdec2_ccu *ccu,
				    struct mpp_task *mpp_task,
				    struct mpp_taskqueue *queue,
				    struct mpp_dev *mpp)
{
	u32 ccu_en, work_mode, link_mode;
	struct rkvdec2_task *task = to_rkvdec2_task(mpp_task);
	u32 timing_en = mpp->srv->timing_en;

	mpp_debug_enter();

	if (test_bit(TASK_STATE_START, &mpp_task->state))
		goto done;

	ccu_en = readl(ccu->reg_base + RKVDEC_CCU_WORK_BASE);
	mpp_dbg_ccu("ccu_en=%d\n", ccu_en);
	if (!ccu_en) {
		u32 i;

		/* set work mode */
		work_mode = 0;
		for (i = 0; i < queue->core_count; i++) {
			u32 val;
			struct mpp_dev *core = queue->cores[i];
			struct rkvdec2_dev *dec = to_rkvdec2_dev(core);

			if (mpp->disable)
				continue;
			work_mode |= dec->core_mask;
			rkvdec2_ccu_link_fix_rcb_regs(dec);
			/* control by ccu */
			val = readl(dec->link_dec->reg_base + RKVDEC_LINK_IRQ_BASE);
			val |= RKVDEC_LINK_BIT_CCU_WORK_MODE;
			writel(val, dec->link_dec->reg_base + RKVDEC_LINK_IRQ_BASE);
		}
		writel(work_mode, ccu->reg_base + RKVDEC_CCU_CORE_WORK_BASE);
		ccu->ccu_core_work_mode = readl(ccu->reg_base + RKVDEC_CCU_CORE_WORK_BASE);
		mpp_dbg_ccu("ccu_work_mode=%08x, ccu_work_status=%08x\n",
			    readl(ccu->reg_base + RKVDEC_CCU_CORE_WORK_BASE),
			    readl(ccu->reg_base + RKVDEC_CCU_CORE_STA_BASE));

		/* set auto gating */
		writel(RKVDEC_CCU_BIT_AUTOGATE, ccu->reg_base + RKVDEC_CCU_CTRL_BASE);
		/* link start base */
		writel(task->table->iova, ccu->reg_base + RKVDEC_CCU_CFG_ADDR_BASE);
		/* enable link */
		writel(RKVDEC_CCU_BIT_WORK_EN, ccu->reg_base + RKVDEC_CCU_WORK_BASE);
	}

	/* set link mode */
	link_mode = ccu_en ? RKVDEC_CCU_BIT_ADD_MODE : 0;
	writel(link_mode | RKVDEC_LINK_ADD_CFG_NUM, ccu->reg_base + RKVDEC_CCU_LINK_MODE_BASE);

	/* flush tlb before starting hardware */
	mpp_iommu_flush_tlb(mpp->iommu_info);
	/* wmb */
	wmb();
	INIT_DELAYED_WORK(&mpp_task->timeout_work, rkvdec2_ccu_timeout_work);
	mpp_task_run_begin(mpp_task, timing_en, MPP_WORK_TIMEOUT_DELAY);
	/* configure done */
	writel(RKVDEC_CCU_BIT_CFG_DONE, ccu->reg_base + RKVDEC_CCU_CFG_DONE_BASE);
	mpp_task_run_end(mpp_task, timing_en);

	/* pending to running */
	set_bit(TASK_STATE_RUNNING, &mpp_task->state);
	mpp_taskqueue_pending_to_run(queue, mpp_task);
	mpp_dbg_ccu("session %d task %d iova=%08x task->state=%lx link_mode=%08x\n",
		    mpp_task->session->index, mpp_task->task_index,
		    (u32)task->table->iova, mpp_task->state,
		    readl(ccu->reg_base + RKVDEC_CCU_LINK_MODE_BASE));
done:
	mpp_debug_leave();

	return 0;
}

static void rkvdec2_hard_ccu_handle_pagefault_task(struct rkvdec2_dev *dec,
						   struct mpp_task *mpp_task)
{
	struct rkvdec2_task *task = to_rkvdec2_task(mpp_task);

	mpp_dbg_ccu("session %d task %d w:h[%d %d] pagefault mmu0[%08x %08x] mmu1[%08x %08x] fault_iova %08x\n",
		    mpp_task->session->index, mpp_task->task_index,
		    task->width, task->height, dec->mmu0_st, dec->mmu0_pta,
		    dec->mmu1_st, dec->mmu1_pta, dec->fault_iova);

	set_bit(TASK_STATE_HANDLE, &mpp_task->state);
	task->irq_status |= BIT(4);
	cancel_delayed_work(&mpp_task->timeout_work);
	rkvdec2_hard_ccu_finish(dec->link_dec->info, task);
	set_bit(TASK_STATE_FINISH, &mpp_task->state);
	set_bit(TASK_STATE_DONE, &mpp_task->state);
	list_move_tail(&task->table->link, &dec->ccu->unused_list);
	list_del_init(&mpp_task->queue_link);
	/* Wake up the GET thread */
	wake_up(&mpp_task->wait);
	kref_put(&mpp_task->ref, mpp_free_task);
	dec->mmu_fault = 0;
	dec->fault_iova = 0;
}

static void rkvdec2_hard_ccu_pagefault_proc(struct mpp_taskqueue *queue)
{
	struct mpp_task *loop = NULL, *n;

	list_for_each_entry_safe(loop, n, &queue->running_list, queue_link) {
		struct rkvdec2_task *task = to_rkvdec2_task(loop);
		u32 iova = (u32)task->table->iova;
		u32 i;

		for (i = 0; i < queue->core_count; i++) {
			struct mpp_dev *core = queue->cores[i];
			struct rkvdec2_dev *dec = to_rkvdec2_dev(core);

			if (!dec->mmu_fault || dec->fault_iova != iova)
				continue;
			rkvdec2_hard_ccu_handle_pagefault_task(dec, loop);
		}
	}
}

static void rkvdec2_hard_ccu_resend_tasks(struct mpp_dev *mpp, struct mpp_taskqueue *queue)
{
	struct rkvdec2_task *task_pre = NULL;
	struct mpp_task *loop = NULL, *n;
	struct rkvdec2_dev *dec = to_rkvdec2_dev(mpp);

	/* re sort running list */
	list_for_each_entry_safe(loop, n, &queue->running_list, queue_link) {
		struct rkvdec2_task *task = to_rkvdec2_task(loop);
		u32 *tb_reg = (u32 *)task->table->vaddr;
		u32 irq_status = tb_reg[dec->link_dec->info->tb_reg_int];

		if (!irq_status) {
			if (task_pre) {
				tb_reg = (u32 *)task_pre->table->vaddr;
				tb_reg[dec->link_dec->info->tb_reg_next] = task->table->iova;
			}
			task_pre = task;
		}
	}

	if (task_pre) {
		struct mpp_dma_buffer *tbl;
		u32 *tb_reg;

		tbl = list_first_entry_or_null(&dec->ccu->unused_list,
				struct mpp_dma_buffer, link);
		WARN_ON(!tbl);
		if (tbl) {
			tb_reg = (u32 *)task_pre->table->vaddr;
			tb_reg[dec->link_dec->info->tb_reg_next] = tbl->iova;
		}
	}

	/* resend */
	list_for_each_entry_safe(loop, n, &queue->running_list, queue_link) {
		struct rkvdec2_task *task = to_rkvdec2_task(loop);
		u32 *tb_reg = (u32 *)task->table->vaddr;
		u32 irq_status = tb_reg[dec->link_dec->info->tb_reg_int];

		mpp_dbg_ccu("reback: session %d task %d iova %08x next %08x irq_status 0x%08x\n",
				loop->session->index, loop->task_index, (u32)task->table->iova,
				tb_reg[dec->link_dec->info->tb_reg_next], irq_status);

		if (!irq_status) {
			cancel_delayed_work(&loop->timeout_work);
			clear_bit(TASK_STATE_START, &loop->state);
			rkvdec2_hard_ccu_enqueue(dec->ccu, loop, queue, mpp);
		}
	}
}

void rkvdec2_hard_ccu_worker(struct kthread_work *work_s)
{
	struct mpp_task *mpp_task;
	struct mpp_dev *mpp = container_of(work_s, struct mpp_dev, work);
	struct mpp_taskqueue *queue = mpp->queue;
	struct rkvdec2_dev *dec = to_rkvdec2_dev(mpp);

	mpp_debug_enter();

	/* 1. process all finished task in running list */
	rkvdec2_hard_ccu_dequeue(queue, dec->ccu, dec->link_dec->info);

	/* 2. process reset request */
	if (atomic_read(&queue->reset_request) &&
	    (list_empty(&queue->running_list) || !dec->ccu->ccu_core_work_mode)) {
		/*
		 * cancel running list timeout work to avoid
		 * sw timeout causeby reset long time
		 */
		struct mpp_task *loop = NULL, *n;

		list_for_each_entry_safe(loop, n, &queue->running_list, queue_link) {
			cancel_delayed_work(&loop->timeout_work);
		}
		/* reset process */
		rkvdec2_hard_ccu_reset(queue, dec->ccu);
		atomic_set(&queue->reset_request, 0);
		/* if iommu pagefault, find the fault task and drop it */
		if (queue->iommu_fault) {
			rkvdec2_hard_ccu_pagefault_proc(queue);
			queue->iommu_fault = 0;
		}

		/* relink running task iova in list, and resend them to hw */
		if (!list_empty(&queue->running_list))
			rkvdec2_hard_ccu_resend_tasks(mpp, queue);
	}

	/* 3. process pending task */
	while (1) {
		if (atomic_read(&queue->reset_request))
			break;

		/* get one task form pending list */
		mutex_lock(&queue->pending_lock);
		mpp_task = list_first_entry_or_null(&queue->pending_list,
						struct mpp_task, queue_link);
		mutex_unlock(&queue->pending_lock);

		if (!mpp_task)
			break;
		if (test_bit(TASK_STATE_ABORT, &mpp_task->state)) {
			mutex_lock(&queue->pending_lock);
			list_del_init(&mpp_task->queue_link);
			mutex_unlock(&queue->pending_lock);
			kref_put(&mpp_task->ref, mpp_free_task);
			continue;
		}

		mpp_task = rkvdec2_hard_ccu_prepare(mpp_task, dec->ccu, dec->link_dec->info);
		if (!mpp_task)
			break;

		rkvdec2_ccu_power_on(queue, dec->ccu);
		rkvdec2_hard_ccu_enqueue(dec->ccu, mpp_task, queue, mpp);
	}

	/* 4. poweroff when running and pending list are empty */
	mutex_lock(&queue->pending_lock);
	if (list_empty(&queue->running_list) &&
	    list_empty(&queue->pending_list))
		rkvdec2_ccu_power_off(queue, dec->ccu);
	mutex_unlock(&queue->pending_lock);

	/* 5. check session detach out of queue */
	mpp_session_cleanup_detach(queue, work_s);

	mpp_debug_leave();
}
