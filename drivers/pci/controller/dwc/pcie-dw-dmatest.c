// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2022 Rockchip Electronics Co., Ltd.
 */

#include <linux/init.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/err.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>

#include "pcie-dw-dmatest.h"
#include "../rockchip-pcie-dma.h"

static int test_size = 0x20;
module_param_named(size,  test_size, int, 0644);
MODULE_PARM_DESC(size, "each packet size in bytes");

static unsigned int cycles_count = 1;
module_param(cycles_count, uint, 0644);
MODULE_PARM_DESC(cycles_count, "how many erase cycles to do (default 1)");

static unsigned int chn_en = 1;
module_param(chn_en, uint, 0644);
MODULE_PARM_DESC(chn_en, "Each bits for one dma channel, up to 2 channels, (default enable channel 0)");

static unsigned int rw_test = 3;
module_param(rw_test, uint, 0644);
MODULE_PARM_DESC(rw_test, "Read/Write test, 1-read 2-write 3-both(default 3)");

static unsigned long bus_addr = 0x3c000000;
module_param(bus_addr, ulong, 0644);
MODULE_PARM_DESC(bus_addr, "Dmatest chn0 bus_addr(remote), chn1 add offset 0x100000, (default 0x3c000000)");

static unsigned long local_addr = 0x3c000000;
module_param(local_addr, ulong, 0644);
MODULE_PARM_DESC(local_addr, "Dmatest chn0 local_addr(local), chn1 add offset 0x100000, (default 0x3c000000)");

static unsigned int test_dev;
module_param(test_dev, uint, 0644);
MODULE_PARM_DESC(test_dev, "Choose dma_obj device,(default 0)");

static bool is_rc = true;
module_param_named(is_rc, is_rc, bool, 0644);
MODULE_PARM_DESC(is_rc, "Test port is rc(default true)");

#define PCIE_DW_MISC_DMATEST_DEV_MAX	8

#define PCIE_DMA_CHANEL_MAX_NUM		2

struct pcie_dw_dmatest_dev {
	struct dma_trx_obj *obj;

	bool irq_en;
	struct completion rd_done[PCIE_DMA_CHANEL_MAX_NUM];
	struct completion wr_done[PCIE_DMA_CHANEL_MAX_NUM];

	struct mutex rd_lock[PCIE_DMA_CHANEL_MAX_NUM];	/* Corresponding to each read DMA channel */
	struct mutex wr_lock[PCIE_DMA_CHANEL_MAX_NUM];	/* Corresponding to each write DMA channel */

	struct dma_table rd_tbl_buf[PCIE_DMA_CHANEL_MAX_NUM];
	struct dma_table wr_tbl_buf[PCIE_DMA_CHANEL_MAX_NUM];
};

static struct pcie_dw_dmatest_dev s_dmatest_dev[PCIE_DW_MISC_DMATEST_DEV_MAX];
static int cur_dmatest_dev;

static void pcie_dw_dmatest_show(void)
{
	int i;

	for (i = 0; i < PCIE_DW_MISC_DMATEST_DEV_MAX; i++) {
		if (s_dmatest_dev[i].obj)
			dev_info(s_dmatest_dev[i].obj->dev, " test_dev index %d\n", i);
		else
			break;
	}

	dev_info(s_dmatest_dev[test_dev].obj->dev, " is current test_dev\n");
}

static int rk_pcie_dma_wait_for_finished(struct dma_trx_obj *obj, struct dma_table *table)
{
	int ret = 0, timeout_us, i;

	timeout_us = table->buf_size / 100 + 1000; /* 100MB/s for redundant calculate */

	for (i = 0; i < timeout_us; i++) {
		ret = obj->get_dma_status(obj, table->chn, table->dir);
		if (ret == 1) {
			ret = 0;
			break;
		} else if (ret < 0) {
			ret = -EFAULT;
			break;
		}
		udelay(1);
	}

	if (i >= timeout_us || ret) {
		dev_err(obj->dev, "%s timeout\n", __func__);
		if (obj->dma_debug)
			obj->dma_debug(obj, table);
		return -EFAULT;
	}

	return ret;
}

static int rk_pcie_ep_dma_frombus(struct pcie_dw_dmatest_dev *dmatest_dev, u32 chn,
				  u64 local_paddr, u64 bus_paddr, u32 size)
{
	struct dma_table *table;
	struct dma_trx_obj *obj = dmatest_dev->obj;
	int ret;

	if (chn >= PCIE_DMA_CHANEL_MAX_NUM)
		return -1;

	mutex_lock(&dmatest_dev->rd_lock[chn]);

	table = &dmatest_dev->rd_tbl_buf[chn];
	memset(table, 0, sizeof(struct dma_table));

	if (dmatest_dev->irq_en)
		reinit_completion(&dmatest_dev->rd_done[chn]);

	table->buf_size = size;
	table->bus = bus_paddr;
	table->local = local_paddr;
	table->chn = chn;
	table->dir = DMA_FROM_BUS;

	obj->config_dma_func(table);
	obj->start_dma_func(obj, table);

	if (dmatest_dev->irq_en) {
		ret = wait_for_completion_interruptible_timeout(&dmatest_dev->rd_done[chn], HZ);
		if (ret < 0)
			dev_err(obj->dev, "%s interrupted\n", __func__);
		else if (ret == 0)
			dev_err(obj->dev, "%s timed out\n", __func__);
	} else {
		ret = rk_pcie_dma_wait_for_finished(obj, table);
	}
	mutex_unlock(&dmatest_dev->rd_lock[chn]);

	return ret;
}

static int rk_pcie_ep_dma_tobus(struct pcie_dw_dmatest_dev *dmatest_dev, u32 chn,
				u64 bus_paddr, u64 local_paddr, u32 size)
{
	struct dma_table *table;
	struct dma_trx_obj *obj = dmatest_dev->obj;
	int ret;

	if (chn >= PCIE_DMA_CHANEL_MAX_NUM)
		return -1;

	mutex_lock(&dmatest_dev->wr_lock[chn]);

	table = &dmatest_dev->wr_tbl_buf[chn];
	memset(table, 0, sizeof(struct dma_table));

	if (dmatest_dev->irq_en)
		reinit_completion(&dmatest_dev->wr_done[chn]);

	table->buf_size = size;
	table->bus = bus_paddr;
	table->local = local_paddr;
	table->chn = chn;
	table->dir = DMA_TO_BUS;

	obj->config_dma_func(table);
	obj->start_dma_func(obj, table);

	if (dmatest_dev->irq_en) {
		ret = wait_for_completion_interruptible_timeout(&dmatest_dev->wr_done[chn], HZ);
		if (ret < 0)
			dev_err(obj->dev, "%s interrupted\n", __func__);
		else if (ret == 0)
			dev_err(obj->dev, "%s timed out\n", __func__);
	} else {
		ret = rk_pcie_dma_wait_for_finished(obj, table);
	}
	mutex_unlock(&dmatest_dev->wr_lock[chn]);

	return ret;
}

static int rk_pcie_rc_dma_frombus(struct pcie_dw_dmatest_dev *dmatest_dev, u32 chn,
				  u64 local_paddr, u64 bus_paddr, u32 size)
{
	return rk_pcie_ep_dma_tobus(dmatest_dev, chn, local_paddr, bus_paddr, size);
}

static int rk_pcie_rc_dma_tobus(struct pcie_dw_dmatest_dev *dmatest_dev, u32 chn,
				u64 bus_paddr, u64 local_paddr, u32 size)
{
	return rk_pcie_ep_dma_frombus(dmatest_dev, chn, bus_paddr, local_paddr, size);
}

static int rk_pcie_dma_interrupt_handler_call_back(struct dma_trx_obj *obj, u32 chn, enum dma_dir dir)
{
	struct pcie_dw_dmatest_dev *dmatest_dev = (struct pcie_dw_dmatest_dev *)obj->priv;

	if (chn >= PCIE_DMA_CHANEL_MAX_NUM)
		return -1;

	if (dir == DMA_FROM_BUS)
		complete(&dmatest_dev->rd_done[chn]);
	else
		complete(&dmatest_dev->wr_done[chn]);

	return 0;
}

struct dma_trx_obj *pcie_dw_dmatest_register(struct device *dev, bool irq_en)
{
	struct dma_trx_obj *obj;
	struct pcie_dw_dmatest_dev *dmatest_dev = &s_dmatest_dev[cur_dmatest_dev];
	int i;

	obj = devm_kzalloc(dev, sizeof(struct dma_trx_obj), GFP_KERNEL);
	if (!obj)
		return ERR_PTR(-ENOMEM);

	obj->dev = dev;
	obj->priv = dmatest_dev;
	obj->cb = rk_pcie_dma_interrupt_handler_call_back;

	/* Save for dmatest */
	dmatest_dev->obj = obj;
	for (i = 0; i < PCIE_DMA_CHANEL_MAX_NUM; i++) {
		init_completion(&dmatest_dev->rd_done[i]);
		init_completion(&dmatest_dev->wr_done[i]);
		mutex_init(&dmatest_dev->rd_lock[i]);
		mutex_init(&dmatest_dev->wr_lock[i]);
	}

	/* Enable IRQ transfer as default */
	dmatest_dev->irq_en = irq_en;
	cur_dmatest_dev++;

	return obj;
}

void pcie_dw_dmatest_unregister(struct dma_trx_obj *obj)
{
	cur_dmatest_dev = 0;
}

int pcie_dw_rc_dma_frombus(struct dma_trx_obj *obj, u32 chn,
				  u64 local_paddr, u64 bus_paddr, u32 size)
{
	struct pcie_dw_dmatest_dev *dmatest_dev = obj->priv;

	return rk_pcie_ep_dma_tobus(dmatest_dev, chn, local_paddr, bus_paddr, size);
}

int pcie_dw_rc_dma_tobus(struct dma_trx_obj *obj, u32 chn,
				u64 bus_paddr, u64 local_paddr, u32 size)
{
	struct pcie_dw_dmatest_dev *dmatest_dev = obj->priv;

	return rk_pcie_ep_dma_frombus(dmatest_dev, chn, bus_paddr, local_paddr, size);
}

static int dma_test(struct pcie_dw_dmatest_dev *dmatest_dev, u32 chn,
		    u64 bus_paddr, u64 local_paddr, u32 size, u32 loop, u8 rd_en, u8 wr_en)
{
	ktime_t start_time;
	ktime_t end_time;
	ktime_t cost_time;
	u32 i;
	long long total_byte;
	long long us = 0;
	struct dma_trx_obj *obj = dmatest_dev->obj;

	/*
	 * Clean the cache to ensure memory consistency. The CPU writes to the normal memory
	 * cache before the transmission is initiated, which may cause IO consistency problems,
	 * such as IO commands.
	 */
	if (rd_en)
		dma_sync_single_for_device(obj->dev, local_paddr, size, DMA_TO_DEVICE);

	start_time = ktime_get();
	for (i = 0; i < loop; i++) {
		if (rd_en) {
			if (is_rc)
				rk_pcie_rc_dma_frombus(dmatest_dev, chn, local_paddr, bus_paddr, size);
			else
				rk_pcie_ep_dma_frombus(dmatest_dev, chn, local_paddr, bus_paddr, size);
			dma_sync_single_for_cpu(obj->dev, local_paddr, size, DMA_FROM_DEVICE);
		}

		if (wr_en) {
			dma_sync_single_for_device(obj->dev, local_paddr, size, DMA_TO_DEVICE);
			if (is_rc)
				rk_pcie_rc_dma_tobus(dmatest_dev, chn, bus_paddr, local_paddr, size);
			else
				rk_pcie_ep_dma_tobus(dmatest_dev, chn, bus_paddr, local_paddr, size);
		}
	}
	end_time = ktime_get();
	cost_time = ktime_sub(end_time, start_time);
	us = ktime_to_us(cost_time);

	total_byte = (wr_en + rd_en) * size * loop; /* 1 rd,1 wr */
	total_byte = total_byte * (1000000 / 1024) / us;
	pr_err("pcie dma %s/%s test (%d+%d)*%d*%d cost %lldus speed:%lldKB/S\n",
	       wr_en ? "wr" : "", rd_en ? "rd" : "", wr_en, rd_en, size, loop, us, total_byte);

	return 0;
}

static int dma_test_ch0(void *p)
{
	dma_test(&s_dmatest_dev[test_dev], 0, bus_addr, local_addr, test_size,
		 cycles_count, rw_test & 0x1, (rw_test & 0x2) >> 1);

	return 0;
}

static int dma_test_ch1(void *p)
{
	/* Test in different area with ch0 */
	if (chn_en == 3)
		dma_test(&s_dmatest_dev[test_dev], 1, bus_addr + test_size, local_addr + test_size, test_size,
			 cycles_count, rw_test & 0x1, (rw_test & 0x2) >> 1);
	else
		dma_test(&s_dmatest_dev[test_dev], 1, bus_addr, local_addr, test_size,
			 cycles_count, rw_test & 0x1, (rw_test & 0x2) >> 1);

	return 0;
}

static int dma_run(void)
{
	if (chn_en == 3) {
		kthread_run(dma_test_ch0, NULL, "dma_test_ch0");
		kthread_run(dma_test_ch1, NULL, "dma_test_ch1");
	} else if (chn_en == 2) {
		dma_test_ch1(NULL);
	} else {
		dma_test_ch0(NULL);
	}

	return 0;
}

static int pcie_dw_dmatest(const char *val, const struct kernel_param *kp)
{
	char tmp[8];

	if (!s_dmatest_dev[0].obj) {
		pr_err("dmatest dev not exits\n");
		kfree(tmp);

		return -1;
	}

	strncpy(tmp, val, 8);
	if (!strncmp(tmp, "run", 3)) {
		dma_run();
	} else if (!strncmp(tmp, "show", 4)) {
		pcie_dw_dmatest_show();
	} else {
		pr_info("input error\n");
	}

	return 0;
}

static const struct kernel_param_ops pcie_dw_dmatest_ops = {
	.set = pcie_dw_dmatest,
	.get = param_get_uint,
};

module_param_cb(dmatest, &pcie_dw_dmatest_ops, &pcie_dw_dmatest, 0644);
MODULE_PARM_DESC(dmatest, "test rockchip pcie dma module");

MODULE_AUTHOR("Jon Lin");
MODULE_LICENSE("GPL");
