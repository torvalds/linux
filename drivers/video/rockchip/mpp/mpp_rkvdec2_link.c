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

#define WORK_TIMEOUT_MS		(200)
#define WAIT_TIMEOUT_MS		(500)

#define RKVDEC_MAX_WRITE_PART	6
#define RKVDEC_MAX_READ_PART	2

struct rkvdec_link_part {
	/* register offset of table buffer */
	u32 tb_reg_off;
	/* start idx of task register */
	u32 reg_start;
	/* number of task register */
	u32 reg_num;
};

struct rkvdec_link_info {
	dma_addr_t iova;
	/* total register for link table buffer */
	u32 tb_reg_num;
	/* next link table addr in table buffer */
	u32 tb_reg_next;
	/* current read back addr in table buffer */
	u32 tb_reg_r;
	/* secondary enable in table buffer */
	u32 tb_reg_second_en;
	u32 part_w_num;
	u32 part_r_num;

	struct rkvdec_link_part part_w[RKVDEC_MAX_WRITE_PART];
	struct rkvdec_link_part part_r[RKVDEC_MAX_READ_PART];

	/* interrupt read back in table buffer */
	u32 tb_reg_int;
};

static struct rkvdec_link_info rkvdec_link_v2_hw_info = {
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
};

static void rkvdec_link_status_update(struct rkvdec_link_dev *dev)
{
	void __iomem *reg_base = dev->reg_base;
	u32 error_ff0, error_ff1;
	u32 enable_ff0, enable_ff1;
	u32 loop_count = 10;
	u32 val;

	error_ff1 = (readl(reg_base + RKVDEC_LINK_DEC_NUM_BASE) &
		    RKVDEC_LINK_BIT_DEC_ERROR) ? 1 : 0;
	enable_ff1 = readl(reg_base + RKVDEC_LINK_EN_BASE);

	dev->irq_status = readl(reg_base + RKVDEC_LINK_IRQ_BASE);
	dev->iova_curr = readl(reg_base + RKVDEC_LINK_CFG_ADDR_BASE);
	dev->link_mode = readl(reg_base + RKVDEC_LINK_MODE_BASE);
	dev->total = readl(reg_base + RKVDEC_LINK_TOTAL_NUM_BASE);
	dev->iova_next = readl(reg_base + RKVDEC_LINK_NEXT_ADDR_BASE);

	do {
		val = readl(reg_base + RKVDEC_LINK_DEC_NUM_BASE);
		error_ff0 = (val & RKVDEC_LINK_BIT_DEC_ERROR) ? 1 : 0;
		enable_ff0 = readl(reg_base + RKVDEC_LINK_EN_BASE);

		if (error_ff0 == error_ff1 && enable_ff0 == enable_ff1)
			break;

		error_ff1 = error_ff0;
		enable_ff1 = enable_ff0;
	} while (--loop_count);

	dev->error = error_ff0;
	dev->decoded_status = val;
	dev->decoded = RKVDEC_LINK_GET_DEC_NUM(val);
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

	for (i = 0; i < dev->task_size; i++) {
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

	mpp_err("task write %d read %d send %d recv %d run %d decoded %d total %d\n",
		dev->task_write, dev->task_read, dev->task_send, dev->task_recv,
		dev->task_to_run, dev->task_decoded, dev->task_total);
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

static int rkvdec_link_get_task_write(struct rkvdec_link_dev *dev)
{
	int idx = dev->task_write < dev->task_size ? dev->task_write :
		  dev->task_write - dev->task_size;

	return idx;
}
static int rkvdec_link_inc_task_write(struct rkvdec_link_dev *dev)
{
	int task_write = rkvdec_link_get_task_write(dev);

	dev->task_write++;
	if (dev->task_write >= dev->task_size * 2)
		dev->task_write = 0;

	return task_write;
}
static int rkvdec_link_get_task_read(struct rkvdec_link_dev *dev)
{
	int idx = dev->task_read < dev->task_size ? dev->task_read :
		  dev->task_read - dev->task_size;

	return idx;
}
static int rkvdec_link_inc_task_read(struct rkvdec_link_dev *dev)
{
	int task_read = rkvdec_link_get_task_read(dev);

	dev->task_read++;
	if (dev->task_read >= dev->task_size * 2)
		dev->task_read = 0;

	return task_read;
}
static int rkvdec_link_get_task_hw_queue_length(struct rkvdec_link_dev *dev)
{
	int len;

	if (dev->task_send <= dev->task_recv)
		len = dev->task_send + dev->task_size - dev->task_recv;
	else
		len = dev->task_send - dev->task_recv - dev->task_size;

	return len;
}
static int rkvdec_link_get_task_send(struct rkvdec_link_dev *dev)
{
	int idx = dev->task_send < dev->task_size ? dev->task_send :
		  dev->task_send - dev->task_size;

	return idx;
}
static int rkvdec_link_inc_task_send(struct rkvdec_link_dev *dev)
{
	int task_send = rkvdec_link_get_task_send(dev);

	dev->task_send++;
	if (dev->task_send >= dev->task_size * 2)
		dev->task_send = 0;

	return task_send;
}
static int rkvdec_link_inc_task_recv(struct rkvdec_link_dev *dev)
{
	int task_recv = dev->task_recv;

	dev->task_recv++;
	if (dev->task_recv >= dev->task_size * 2)
		dev->task_recv = 0;

	return task_recv;
}

static int rkvdec_link_get_next_slot(struct rkvdec_link_dev *dev)
{
	int next = -1;

	if (dev->task_write == dev->task_read)
		return next;

	next = rkvdec_link_get_task_write(dev);

	return next;
}

static int rkvdec_link_write_task_to_slot(struct rkvdec_link_dev *dev, int idx,
					  struct mpp_task *mpp_task)
{
	u32 i, off, s, n;
	struct rkvdec_link_part *part;
	struct rkvdec_link_info *info;
	struct mpp_dma_buffer *table;
	struct rkvdec2_task *task;
	int slot_idx;
	u32 *tb_reg;

	if (idx < 0 || idx >= dev->task_size) {
		mpp_err("send invalid task index %d\n", idx);
		return -1;
	}

	info = dev->info;
	part = info->part_w;
	table = dev->table;
	task = to_rkvdec2_task(mpp_task);

	slot_idx = rkvdec_link_inc_task_write(dev);
	if (idx != slot_idx)
		dev_info(dev->dev, "slot index mismatch %d vs %d\n",
			 idx, slot_idx);

	if (task->need_hack) {
		tb_reg = (u32 *)table->vaddr + slot_idx * dev->link_reg_count;

		rkvdec2_3568_hack_fix_link(tb_reg + 4);

		/* setup error mode flag */
		dev->tasks_hw[slot_idx] = NULL;
		dev->task_to_run++;
		dev->task_prepared++;
		slot_idx = rkvdec_link_inc_task_write(dev);
	}

	tb_reg = (u32 *)table->vaddr + slot_idx * dev->link_reg_count;

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

	dev->tasks_hw[slot_idx] = mpp_task;
	task->slot_idx = slot_idx;
	dev->task_to_run++;
	dev->task_prepared++;
	mpp_dbg_link_flow("slot %d write task %d\n", slot_idx,
			  mpp_task->task_id);

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

static int rkvdec_link_send_task_to_hw(struct rkvdec_link_dev *dev,
				       struct mpp_task *mpp_task,
				       int slot_idx, u32 task_to_run,
				       int resend)
{
	void __iomem *reg_base = dev->reg_base;
	struct mpp_dma_buffer *table = dev->table;
	u32 task_total = dev->task_total;
	u32 mode_start = 0;
	u32 val;

	/* write address */
	if (!task_to_run || task_to_run > dev->task_size ||
	    slot_idx < 0 || slot_idx >= dev->task_size) {
		mpp_err("invalid task send cfg at %d count %d\n",
			slot_idx, task_to_run);
		rkvdec_link_counter("error on send", dev);
		return 0;
	}

	val = task_to_run;
	if (!task_total || resend)
		mode_start = 1;

	if (mode_start) {
		u32 iova = table->iova + slot_idx * dev->link_node_size;

		rkvdec2_clear_cache(dev->mpp);
		/* cleanup counter in hardware */
		writel(0, reg_base + RKVDEC_LINK_MODE_BASE);
		/* start config before all registers are set */
		wmb();
		writel(RKVDEC_LINK_BIT_CFG_DONE, reg_base + RKVDEC_LINK_CFG_CTRL_BASE);
		/* write zero count config */
		wmb();
		/* clear counter and enable link mode hardware */
		writel(RKVDEC_LINK_BIT_EN, reg_base + RKVDEC_LINK_EN_BASE);

		dev->task_total = 0;
		dev->task_decoded = 0;

		writel_relaxed(iova, reg_base + RKVDEC_LINK_CFG_ADDR_BASE);
	} else {
		val |= RKVDEC_LINK_BIT_ADD_MODE;
	}

	if (!resend) {
		u32 i;

		for (i = 0; i < task_to_run; i++) {
			int next_idx = rkvdec_link_inc_task_send(dev);
			struct mpp_task *task_ddr = dev->tasks_hw[next_idx];

			if (!task_ddr)
				continue;

			set_bit(TASK_STATE_START, &task_ddr->state);
			schedule_delayed_work(&task_ddr->timeout_work,
					      msecs_to_jiffies(200));
		}
	} else {
		if (task_total)
			dev_info(dev->dev, "resend with total %d\n", task_total);
	}

	/* set link mode */
	writel_relaxed(val, reg_base + RKVDEC_LINK_MODE_BASE);

	/* start config before all registers are set */
	wmb();

	/* configure done */
	writel(RKVDEC_LINK_BIT_CFG_DONE, reg_base + RKVDEC_LINK_CFG_CTRL_BASE);

	mpp_dbg_link_flow("slot %d enable task %d mode %s\n", slot_idx,
			  task_to_run, mode_start ? "start" : "add");
	if (mode_start) {
		/* start hardware before all registers are set */
		wmb();
		/* clear counter and enable link mode hardware */
		writel(RKVDEC_LINK_BIT_EN, reg_base + RKVDEC_LINK_EN_BASE);
	}

	dev->task_total += task_to_run;
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
	int slot_idx = task->slot_idx;
	u32 *tb_reg = (u32 *)(table->vaddr + slot_idx * link_dec->link_node_size);
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

static int rkvdec_link_isr_recv_task(struct mpp_dev *mpp,
				     struct rkvdec_link_dev *link_dec,
				     int count)
{
	struct rkvdec_link_info *info = link_dec->info;
	u32 *table_base = (u32 *)link_dec->table->vaddr;
	int i;

	for (i = 0; i < count; i++) {
		int idx = rkvdec_link_get_task_read(link_dec);
		struct mpp_task *mpp_task = link_dec->tasks_hw[idx];
		struct rkvdec2_task *task = NULL;
		u32 *regs = NULL;
		u32 irq_status = 0;

		if (!mpp_task) {
			regs = table_base + idx * link_dec->link_reg_count;
			mpp_dbg_link_flow("slot %d read  task stuff\n", idx);

			link_dec->stuff_total++;
			if (link_dec->statistic_count &&
			    regs[RKVDEC_LINK_REG_CYCLE_CNT]) {
				link_dec->stuff_cycle_sum +=
					regs[RKVDEC_LINK_REG_CYCLE_CNT];
				link_dec->stuff_cnt++;
				if (link_dec->stuff_cnt >=
				    link_dec->statistic_count) {
					dev_info(
						link_dec->dev, "hw cycle %u\n",
						(u32)(link_dec->stuff_cycle_sum /
						      link_dec->statistic_count));
					link_dec->stuff_cycle_sum = 0;
					link_dec->stuff_cnt = 0;
				}
			}

			if (link_dec->error && (i == (count - 1))) {
				link_dec->stuff_err++;

				irq_status = mpp_read_relaxed(mpp, RKVDEC_REG_INT_EN);
				dev_info(link_dec->dev, "found stuff task error irq %08x %u/%u\n",
					 irq_status, link_dec->stuff_err,
					 link_dec->stuff_total);

				if (link_dec->stuff_on_error) {
					dev_info(link_dec->dev, "stuff task error again %u/%u\n",
						 link_dec->stuff_err,
						 link_dec->stuff_total);
				}

				link_dec->stuff_on_error = 1;
				/* resend task */
				link_dec->decoded--;
			} else {
				link_dec->stuff_on_error = 0;
				rkvdec_link_inc_task_recv(link_dec);
				rkvdec_link_inc_task_read(link_dec);
				link_dec->task_running--;
				link_dec->task_prepared--;
			}

			continue;
		}

		task = to_rkvdec2_task(mpp_task);
		regs = table_base + idx * link_dec->link_reg_count;
		irq_status = regs[info->tb_reg_int];
		mpp_dbg_link_flow("slot %d rd task %d\n", idx,
				  mpp_task->task_id);

		task->irq_status = irq_status ? irq_status : mpp->irq_status;

		cancel_delayed_work_sync(&mpp_task->timeout_work);
		set_bit(TASK_STATE_HANDLE, &mpp_task->state);

		if (link_dec->statistic_count &&
		    regs[RKVDEC_LINK_REG_CYCLE_CNT]) {
			link_dec->task_cycle_sum +=
				regs[RKVDEC_LINK_REG_CYCLE_CNT];
			link_dec->task_cnt++;
			if (link_dec->task_cnt >= link_dec->statistic_count) {
				dev_info(link_dec->dev, "hw cycle %u\n",
					 (u32)(link_dec->task_cycle_sum /
					       link_dec->statistic_count));
				link_dec->task_cycle_sum = 0;
				link_dec->task_cnt = 0;
			}
		}

		rkvdec2_link_finish(mpp, mpp_task);

		set_bit(TASK_STATE_FINISH, &mpp_task->state);

		list_del_init(&mpp_task->queue_link);
		link_dec->task_running--;
		link_dec->task_prepared--;

		rkvdec_link_inc_task_recv(link_dec);
		rkvdec_link_inc_task_read(link_dec);

		if (test_bit(TASK_STATE_ABORT, &mpp_task->state))
			set_bit(TASK_STATE_ABORT_READY, &mpp_task->state);

		set_bit(TASK_STATE_PROC_DONE, &mpp_task->state);
		/* Wake up the GET thread */
		wake_up(&task->wait);
	}

	return 0;
}

static void *rkvdec2_link_prepare(struct mpp_dev *mpp,
				  struct mpp_task *mpp_task)
{
	struct mpp_task *out_task = NULL;
	struct rkvdec2_dev *dec = to_rkvdec2_dev(mpp);
	struct rkvdec_link_dev *link_dec = dec->link_dec;
	int ret = 0;
	int slot_idx;

	mpp_debug_enter();

	slot_idx = rkvdec_link_get_next_slot(link_dec);
	if (slot_idx < 0) {
		mpp_err("capacity %d running %d\n",
			mpp->task_capacity, link_dec->task_running);
		dev_err(link_dec->dev, "no slot to write on get next slot\n");
		goto done;
	}

	ret = rkvdec_link_write_task_to_slot(link_dec, slot_idx, mpp_task);
	if (ret >= 0)
		out_task = mpp_task;
	else
		dev_err(mpp->dev, "no slot to write\n");

done:
	mpp_debug_leave();

	return out_task;
}

static int rkvdec2_link_reset(struct mpp_dev *mpp)
{
	struct rkvdec2_dev *dec = to_rkvdec2_dev(mpp);

	dev_info(mpp->dev, "resetting...\n");

	/* FIXME lock resource lock of the other devices in combo */
	mpp_iommu_down_write(mpp->iommu_info);
	mpp_reset_down_write(mpp->reset_group);
	atomic_set(&mpp->reset_request, 0);

	rockchip_save_qos(mpp->dev);

	mutex_lock(&dec->sip_reset_lock);
	rockchip_dmcfreq_lock();
	sip_smc_vpu_reset(0, 0, 0);
	rockchip_dmcfreq_unlock();
	mutex_unlock(&dec->sip_reset_lock);

	rockchip_restore_qos(mpp->dev);

	/* Note: if the domain does not change, iommu attach will be return
	 * as an empty operation. Therefore, force to close and then open,
	 * will be update the domain. In this way, domain can really attach.
	 */
	mpp_iommu_refresh(mpp->iommu_info, mpp->dev);

	mpp_reset_up_write(mpp->reset_group);
	mpp_iommu_up_write(mpp->iommu_info);

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

static int rkvdec2_link_isr(struct mpp_dev *mpp)
{
	struct rkvdec2_dev *dec = to_rkvdec2_dev(mpp);
	struct rkvdec_link_dev *link_dec = dec->link_dec;
	/* keep irq_status */
	u32 irq_status = link_dec->irq_status;
	u32 prev_dec_num;
	int count = 0;
	u32 len = 0;
	u32 need_reset = atomic_read(&mpp->reset_request);
	u32 task_timeout = link_dec->task_on_timeout;

	mpp_debug_enter();

	disable_irq(mpp->irq);
	rkvdec_link_status_update(link_dec);
	link_dec->irq_status = irq_status;
	prev_dec_num = link_dec->task_decoded;

	if (!link_dec->enabled || task_timeout) {
		u32 val;

		if (task_timeout)
			rkvdec_link_reg_dump("timeout", link_dec);

		val = mpp_read(mpp, 224 * 4);
		if (!(val & BIT(2))) {
			dev_info(mpp->dev, "frame not complete\n");
			link_dec->decoded++;
		}
	}
	count = (int)link_dec->decoded - (int)prev_dec_num;

	/* handle counter wrap */
	if (link_dec->enabled && !count && !need_reset) {
		/* process extra isr when task is processed */
		enable_irq(mpp->irq);
		goto done;
	}

	/* get previous ready task */
	if (count) {
		rkvdec_link_isr_recv_task(mpp, link_dec, count);
		link_dec->task_decoded = link_dec->decoded;
	}

	if (!link_dec->enabled || need_reset)
		goto do_reset;

	enable_irq(mpp->irq);
	goto done;

do_reset:
	/* NOTE: irq may run with reset */
	atomic_inc(&mpp->reset_request);
	rkvdec2_link_reset(mpp);
	link_dec->task_decoded = 0;
	link_dec->task_total = 0;
	enable_irq(mpp->irq);

	if (link_dec->total == link_dec->decoded)
		goto done;

	len = rkvdec_link_get_task_hw_queue_length(link_dec);
	if (len > link_dec->task_size)
		rkvdec_link_counter("invalid len", link_dec);

	if (len) {
		int slot_idx = rkvdec_link_get_task_read(link_dec);
		struct mpp_task *mpp_task = NULL;

		mpp_task = link_dec->tasks_hw[slot_idx];
		rkvdec_link_send_task_to_hw(link_dec, mpp_task,
					    slot_idx, len, 1);
	}

done:
	mpp_debug_leave();

	return IRQ_HANDLED;
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
	link_dec->task_size  = task_capacity;
	link_dec->task_count = 0;
	link_dec->task_write = 0;
	link_dec->task_read  = link_dec->task_size;
	link_dec->task_send  = 0;
	link_dec->task_recv  = link_dec->task_size;

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

	mpp_debug_enter();

	link_dec = devm_kzalloc(dev, sizeof(*link_dec), GFP_KERNEL);
	if (!link_dec) {
		ret = -ENOMEM;
		goto done;
	}

	link_dec->tasks_hw = devm_kzalloc(dev, sizeof(*link_dec->tasks_hw) *
					  mpp->task_capacity, GFP_KERNEL);
	if (!link_dec->tasks_hw) {
		ret = -ENOMEM;
		goto done;
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "link");
	if (res)
		link_dec->info = &rkvdec_link_v2_hw_info;
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

	link_dec->mpp = mpp;
	link_dec->dev = dev;
	atomic_set(&link_dec->task_timeout, 0);
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
			if (link_dec->tasks_hw) {
				devm_kfree(dev, link_dec->tasks_hw);
				link_dec->tasks_hw = NULL;
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

static void rkvdec2_link_trigger_timeout(struct mpp_dev *mpp)
{
	struct rkvdec2_dev *dec = to_rkvdec2_dev(mpp);
	struct rkvdec_link_dev *link_dec = dec->link_dec;

	atomic_inc(&link_dec->task_timeout);
	rkvdec2_link_trigger_work(mpp);
}

static void rkvdec2_link_trigger_irq(struct mpp_dev *mpp)
{
	struct rkvdec2_dev *dec = to_rkvdec2_dev(mpp);
	struct rkvdec_link_dev *link_dec = dec->link_dec;

	link_dec->task_irq++;
	rkvdec2_link_trigger_work(mpp);
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
			link_dec->irq_enabled = 1;
		}
	}
	return 0;
}

static void rkvdec2_link_power_off(struct mpp_dev *mpp)
{
	struct rkvdec2_dev *dec = to_rkvdec2_dev(mpp);
	struct rkvdec_link_dev *link_dec = dec->link_dec;

	if (atomic_xchg(&link_dec->power_enabled, 0)) {
		disable_irq(mpp->irq);
		link_dec->irq_enabled = 0;

		if (mpp->hw_ops->clk_off)
			mpp->hw_ops->clk_off(mpp);

		pm_relax(mpp->dev);
		pm_runtime_put_sync_suspend(mpp->dev);

		link_dec->task_decoded = 0;
		link_dec->task_total = 0;
	}
}

static void rkvdec2_link_timeout_proc(struct work_struct *work_s)
{
	struct mpp_dev *mpp;
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
	rkvdec2_link_trigger_timeout(mpp);
}

static void mpp_taskqueue_scan_pending_abort_task(struct mpp_taskqueue *queue)
{
	struct mpp_task *task, *n;

	mutex_lock(&queue->pending_lock);
	/* Check and pop all timeout task */
	list_for_each_entry_safe(task, n, &queue->pending_list, queue_link) {
		struct mpp_session *session = task->session;

		if (test_bit(TASK_STATE_ABORT, &task->state)) {
			mutex_lock(&session->pending_lock);
			/* wait and signal */
			list_del_init(&task->queue_link);
			mutex_unlock(&session->pending_lock);
			kref_put(&task->ref, rkvdec2_link_free_task);
		}
	}
	mutex_unlock(&queue->pending_lock);
}

static void rkvdec2_link_try_dequeue(struct mpp_dev *mpp)
{
	struct rkvdec2_dev *dec = to_rkvdec2_dev(mpp);
	struct rkvdec_link_dev *link_dec = dec->link_dec;
	struct mpp_task *task;
	struct mpp_taskqueue *queue = mpp->queue;
	int task_irq = link_dec->task_irq;
	int task_irq_prev = link_dec->task_irq_prev;
	int task_timeout = atomic_read(&link_dec->task_timeout);

	if (!link_dec->task_running)
		goto done;

	if (task_timeout != link_dec->task_timeout_prev) {
		dev_info(link_dec->dev, "process task timeout\n");
		atomic_inc(&mpp->reset_request);
		link_dec->task_on_timeout =
			task_timeout - link_dec->task_timeout_prev;
		goto proc;
	}

	if (task_irq == task_irq_prev)
		goto done;

	if (!atomic_read(&link_dec->power_enabled)) {
		dev_info(link_dec->dev, "dequeue on power off\n");
		goto done;
	}

proc:
	task = list_first_entry_or_null(&queue->running_list, struct mpp_task,
					queue_link);
	if (!task) {
		mpp_err("can found task on trydequeue with %d running task\n",
			link_dec->task_running);
		goto done;
	}

	/* Check and process all finished task */
	rkvdec2_link_isr(mpp);

done:
	link_dec->task_irq_prev = task_irq;
	link_dec->task_timeout_prev = task_timeout;
	link_dec->task_on_timeout = 0;

	mpp_taskqueue_scan_pending_abort_task(queue);

	/* TODO: if reset is needed do reset here */
}

static int mpp_task_queue(struct mpp_dev *mpp, struct mpp_task *task)
{
	struct rkvdec2_dev *dec = to_rkvdec2_dev(mpp);
	struct rkvdec_link_dev *link_dec = dec->link_dec;
	u32 task_to_run = 0;
	int slot_idx = 0;

	mpp_debug_enter();

	rkvdec2_link_power_on(mpp);
	mpp_time_record(task);
	mpp_debug(DEBUG_TASK_INFO, "pid %d, start hw %s\n",
		  task->session->pid, dev_name(mpp->dev));

	/* prepare the task for running */
	if (test_and_set_bit(TASK_STATE_PREPARE, &task->state))
		mpp_err("task %d has been prepare twice\n", task->task_id);

	rkvdec2_link_prepare(mpp, task);

	task_to_run = link_dec->task_to_run;
	if (!task_to_run) {
		dev_err(link_dec->dev, "nothing to run\n");
		goto done;
	}

	mpp_reset_down_read(mpp->reset_group);
	link_dec->task_to_run = 0;
	slot_idx = rkvdec_link_get_task_send(link_dec);
	link_dec->task_running += task_to_run;
	rkvdec_link_send_task_to_hw(link_dec, task, slot_idx, task_to_run, 0);

done:
	mpp_debug_leave();

	return 0;
}

irqreturn_t rkvdec2_link_irq_proc(int irq, void *param)
{
	struct mpp_dev *mpp = param;
	int ret = rkvdec2_link_irq(mpp);

	if (!ret)
		rkvdec2_link_trigger_irq(mpp);

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
	kref_put(&task->ref, rkvdec2_link_free_task);

	return 0;
}

int rkvdec2_link_process_task(struct mpp_session *session,
			      struct mpp_task_msgs *msgs)
{
	struct mpp_task *task = NULL;
	struct mpp_dev *mpp = session->mpp;

	task = rkvdec2_alloc_task(session, msgs);
	if (!task) {
		mpp_err("alloc_task failed.\n");
		return -ENOMEM;
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
	struct rkvdec2_task *task;
	int ret;

	mpp_task = mpp_session_get_pending_task(session);
	if (!mpp_task) {
		mpp_err("session %p pending list is empty!\n", session);
		return -EIO;
	}

	task = to_rkvdec2_task(mpp_task);
	ret = wait_event_timeout(task->wait, task_is_done(mpp_task),
				 msecs_to_jiffies(WAIT_TIMEOUT_MS));
	if (ret) {
		ret = rkvdec2_result(mpp, mpp_task, msgs);

		mpp_session_pop_done(session, mpp_task);
	} else {
		mpp_err("task %d:%d statue %lx timeout -> abort\n",
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
	struct rkvdec2_dev *dec = to_rkvdec2_dev(mpp);
	struct rkvdec_link_dev *link_dec = dec->link_dec;
	struct mpp_task *task;
	struct mpp_taskqueue *queue = mpp->queue;

	mpp_debug_enter();

	/*
	 * process timeout and finished task.
	 */
	rkvdec2_link_try_dequeue(mpp);

again:
	if (atomic_read(&mpp->reset_request)) {
		if (link_dec->task_running || link_dec->task_prepared)
			goto done;

		disable_irq(mpp->irq);
		rkvdec2_link_reset(mpp);
		link_dec->task_decoded = 0;
		link_dec->task_total = 0;
		enable_irq(mpp->irq);
	}
	/*
	 * process pending queue to find the task to accept.
	 */
	mutex_lock(&queue->pending_lock);
	task = list_first_entry_or_null(&queue->pending_list, struct mpp_task,
					queue_link);
	mutex_unlock(&queue->pending_lock);
	if (!task)
		goto done;

	if (test_bit(TASK_STATE_ABORT, &task->state)) {
		struct rkvdec2_task *dec_task = to_rkvdec2_task(task);

		mutex_lock(&queue->pending_lock);
		list_del_init(&task->queue_link);

		kref_get(&task->ref);
		set_bit(TASK_STATE_ABORT_READY, &task->state);
		set_bit(TASK_STATE_PROC_DONE, &task->state);

		mutex_unlock(&queue->pending_lock);
		wake_up(&dec_task->wait);
		kref_put(&task->ref, rkvdec2_link_free_task);
		goto again;
	}

	/*
	 * if target device can accept more task send the task to run.
	 */
	if (link_dec->task_running >= link_dec->task_capacity - 2)
		goto done;

	if (mpp_task_queue(mpp, task)) {
		/* failed to run */
		mpp_err("%p failed to process task %p:%d\n",
			mpp, task, task->task_id);
	} else {
		mutex_lock(&queue->pending_lock);
		set_bit(TASK_STATE_RUNNING, &task->state);
		list_move_tail(&task->queue_link, &queue->running_list);
		mutex_unlock(&queue->pending_lock);
		goto again;
	}
done:
	mpp_debug_leave();

	if (link_dec->task_irq != link_dec->task_irq_prev ||
	    atomic_read(&link_dec->task_timeout) != link_dec->task_timeout_prev)
		rkvdec2_link_trigger_work(mpp);

	/* if no task for running power off device */
	{
		u32 all_done = 0;

		mutex_lock(&queue->pending_lock);
		all_done = list_empty(&queue->pending_list);
		mutex_unlock(&queue->pending_lock);

		if (all_done && !link_dec->task_running && !link_dec->task_prepared)
			rkvdec2_link_power_off(mpp);
	}

	mpp_session_cleanup_detach(queue, work_s);
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

int rkvdec2_attach_ccu(struct device *dev, struct rkvdec2_dev *dec)
{
	int ret;
	struct device_node *np;
	struct platform_device *pdev;
	struct rkvdec2_ccu *ccu;
	struct mpp_taskqueue *queue;

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
	queue = dec->mpp.queue;
	if (&dec->mpp != queue->cores[0]) {
		struct mpp_iommu_info *ccu_info, *cur_info;

		/* set the ccu-domain for current device */
		ccu_info = queue->cores[0]->iommu_info;
		cur_info = dec->mpp.iommu_info;
		cur_info->domain = ccu_info->domain;
		mpp_iommu_attach(cur_info);
	}

	dec->ccu = ccu;

	dev_info(dev, "attach ccu as core %d\n", dec->mpp.core_id);
	mpp_debug_enter();

	return 0;
}

static void rkvdec2_ccu_link_timeout_work(struct work_struct *work_s)
{
	struct mpp_dev *mpp;
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
	mpp = task->mpp ? task->mpp : session->mpp;
	mpp_err("task timeout\n");
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

	link_dec->info = &rkvdec_link_v2_hw_info;
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
			mpp = queue->cores[i];
			pm_runtime_get_sync(mpp->dev);
			pm_stay_awake(mpp->dev);
			if (mpp->hw_ops->clk_on)
				mpp->hw_ops->clk_on(mpp);
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
		u32 irq_status = mpp->irq_status;
		u32 timeout_flag = test_bit(TASK_STATE_TIMEOUT, &mpp_task->state);
		u32 abort_flag = test_bit(TASK_STATE_ABORT, &mpp_task->state);

		if (irq_status || timeout_flag || abort_flag) {
			struct rkvdec2_task *task = to_rkvdec2_task(mpp_task);

			set_bit(TASK_STATE_HANDLE, &mpp_task->state);
			cancel_delayed_work(&mpp_task->timeout_work);
			mpp_time_diff(mpp_task);
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

		if (dec->disable_work)
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

#if IS_ENABLED(CONFIG_ROCKCHIP_SIP)
		/* sip reset */
		rockchip_dmcfreq_lock();
		sip_smc_vpu_reset(i, 0, 0);
		rockchip_dmcfreq_unlock();
#else
		rkvdec2_reset(mpp);
#endif
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

int rkvdec2_ccu_iommu_fault_handle(struct iommu_domain *iommu,
				   struct device *iommu_dev,
				   unsigned long iova, int status, void *arg)
{
	u32 i = 0;
	struct mpp_dev *mpp = (struct mpp_dev *)arg;

	mpp_debug_enter();

	atomic_inc(&mpp->queue->reset_request);
	for (i = 0; i < mpp->queue->core_count; i++)
		rk_iommu_mask_irq(mpp->queue->cores[i]->dev);

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
	mpp->irq_status = 0;
	writel_relaxed(dec->core_mask, dec->ccu->reg_base + RKVDEC_CCU_CORE_STA_BASE);
	/* Flush the register before the start the device */
	wmb();
	mpp_write(mpp, RKVDEC_REG_START_EN_BASE, task->reg[reg_en] | RKVDEC_START_EN);

	mpp_debug_leave();

	return 0;
}

static struct mpp_dev *rkvdec2_get_idle_core(struct mpp_taskqueue *queue,
					     struct mpp_task *mpp_task)
{
	u32 i = 0;
	struct rkvdec2_dev *dec = NULL;

	for (i = 0; i < queue->core_count; i++) {
		struct rkvdec2_dev *core = to_rkvdec2_dev(queue->cores[i]);

		if (core->disable_work)
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
		mpp_dbg_core("clear core %d idle\n", mpp_task->core_id);
		return mpp_task->mpp;
	}

	return NULL;
}

static bool rkvdec2_core_working(struct mpp_taskqueue *queue)
{
	u32 i = 0;
	struct rkvdec2_dev *core;
	bool flag = false;

	for (i = 0; i < queue->core_count; i++) {
		core = to_rkvdec2_dev(queue->cores[i]);
		if (core->disable_work)
			continue;
		if (!test_bit(i, &queue->core_idle)) {
			flag = true;
			break;
		}
	}

	return flag;
}

void rkvdec2_soft_ccu_worker(struct kthread_work *work_s)
{
	struct mpp_task *mpp_task;
	struct mpp_dev *mpp = container_of(work_s, struct mpp_dev, work);
	struct mpp_taskqueue *queue = mpp->queue;
	struct rkvdec2_dev *dec = to_rkvdec2_dev(mpp);

	mpp_debug_enter();

	/* process all finished task in running list */
	rkvdec2_soft_ccu_dequeue(queue);

	/* process reset request */
	if (atomic_read(&queue->reset_request)) {
		if (rkvdec2_core_working(queue))
			goto out;
		rkvdec2_ccu_power_on(queue, dec->ccu);
		rkvdec2_soft_ccu_reset(queue, dec->ccu);
	}

get_task:
	/* get one task form pending list */
	mutex_lock(&queue->pending_lock);
	mpp_task = list_first_entry_or_null(&queue->pending_list,
					    struct mpp_task, queue_link);
	mutex_unlock(&queue->pending_lock);
	if (!mpp_task)
		goto done;

	if (test_bit(TASK_STATE_ABORT, &mpp_task->state)) {
		struct rkvdec2_task *dec_task = to_rkvdec2_task(mpp_task);

		mutex_lock(&queue->pending_lock);
		list_del_init(&mpp_task->queue_link);

		kref_get(&mpp_task->ref);
		set_bit(TASK_STATE_ABORT_READY, &mpp_task->state);
		set_bit(TASK_STATE_PROC_DONE, &mpp_task->state);

		mutex_unlock(&queue->pending_lock);
		wake_up(&dec_task->wait);
		kref_put(&mpp_task->ref, rkvdec2_link_free_task);
		goto get_task;
	}
	/* find one core is idle */
	mpp = rkvdec2_get_idle_core(queue, mpp_task);
	if (!mpp)
		goto out;

	/* set session index */
	rkvdec2_set_core_info(mpp_task->reg, mpp_task->session->index);
	/* set rcb buffer */
	mpp_set_rcbbuf(mpp, mpp_task->session, mpp_task);

	/* pending to running */
	mutex_lock(&queue->pending_lock);
	list_move_tail(&mpp_task->queue_link, &queue->running_list);
	mutex_unlock(&queue->pending_lock);
	set_bit(TASK_STATE_RUNNING, &mpp_task->state);

	mpp_time_record(mpp_task);
	mpp_debug(DEBUG_TASK_INFO, "pid %d, start hw %s\n",
		  mpp_task->session->pid, dev_name(mpp->dev));
	set_bit(TASK_STATE_START, &mpp_task->state);
	INIT_DELAYED_WORK(&mpp_task->timeout_work, rkvdec2_ccu_link_timeout_work);
	schedule_delayed_work(&mpp_task->timeout_work, msecs_to_jiffies(WORK_TIMEOUT_MS));
	rkvdec2_ccu_power_on(queue, dec->ccu);
	rkvdec2_soft_ccu_enqueue(mpp, mpp_task);
done:
	if (list_empty(&queue->running_list) &&
	    list_empty(&queue->pending_list))
		rkvdec2_ccu_power_off(queue, dec->ccu);
out:
	/* session detach out of queue */
	mpp_session_cleanup_detach(queue, work_s);

	mpp_debug_leave();
}
