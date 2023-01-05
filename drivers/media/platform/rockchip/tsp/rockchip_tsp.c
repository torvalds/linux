/*
 * Driver for Rockchip TSP  Controller
 *
 * Copyright (C) 2012-2016 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/dvb/ca.h>
#include <linux/of_device.h>
#include <linux/iopoll.h>

#include <linux/rockchip/cpu.h>
#include <linux/rockchip/grf.h>
#include <linux/rockchip/cpu.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/rk_tsp_api.h>
#include "rockchip_tsp.h"

#define MODE_NAME "rockchip-tsp"

#ifdef RK_TSP_DEBUG
#define TSP_DEBUG(x...) pr_info(x)
#else
#define TSP_DEBUG(x...)
#endif

/* if HZ=300, 3.3ms per */
#define TIME_CNT_PER_SECOND HZ

#define TSP_TIMEOUT_US 1000
#define PACKET_SIZE (188)

/* 132.2M bps max */
#define TS_CYC_BUF_LEN ((PACKET_SIZE * 300 * 300) / HZ)

/* 66.1M bps max */
#define TS_CYC_BUF_LEN_PSI ((PACKET_SIZE * 150 * 300) / HZ)
#define SECTION_CYC_BUF_LEN ((1024 * 24 * 300) / HZ)

#define TSP_TIMER_DELAY (HZ / TIME_CNT_PER_SECOND)
#define TSP_CW_ORDER_NUM_SEL 0
#define TSP_LLP_LEN (1024 * 4)

static struct tsp_dev *tdev;

static void rockchip_ts_filter_config(struct tsp_dev *dev,
				      struct tsp_ctx *ctx);
static void rockchip_demux_dma_config(struct tsp_dev *dev,
				      struct tsp_ctx *ctx);
static void rockchip_enable_pid_irq(struct tsp_dev *dev, int id);
static void rockchip_sec_filter_config(struct tsp_dev *dev,
				       struct tsp_ctx *tctx);
static void grf_field_write(struct tsp_dev *dev, enum grf_fields id,
			    unsigned int val)
{
	const u32 field = dev->pdata->grf_reg_fields[id];
	u16 reg;
	u8 msb, lsb;

	if (!field)
		return;

	reg = (field >> 16) & 0xffff;
	lsb = (field >>  8) & 0xff;
	msb = (field >>  0) & 0xff;

	regmap_write(dev->grf, reg, (val << lsb) | (GENMASK(msb, lsb) << 16));
}

static int  rockchip_tsp_clk_enable(struct tsp_dev *dev)
{
	if (clk_prepare_enable(dev->tsp_aclk)) {
		pr_err("Couldn't enable clock 'aclk'\n");
		return -ENOENT;
	}

	if (clk_prepare_enable(dev->tsp_clk)) {
		pr_err("Couldn't enable clock 'clk'\n");
		return -ENOENT;
	}
	clk_set_rate(dev->tsp_clk, 150 * 1000 * 1000);

	if (clk_prepare_enable(dev->tsp_hclk)) {
		pr_err("Couldn't enable clock 'hclk'\n");
		clk_disable_unprepare(dev->tsp_hclk);
		return -ENOENT;
	}
	clk_set_rate(dev->tsp_hclk, 150 * 1000 * 1000);

	return 0;
}

static void rockchip_tsp_clk_disable(struct tsp_dev *dev)
{
	clk_disable_unprepare(dev->tsp_hclk);
	clk_disable_unprepare(dev->tsp_clk);
	clk_disable_unprepare(dev->tsp_aclk);
}

static uint TSP_RD(struct tsp_dev *dev, uint reg)
{
	uint val;

	val = __raw_readl(((dev)->ioaddr + (reg)));

	return val;
}

static void TSP_WR(struct tsp_dev *dev, uint reg, uint val)
{
	__raw_writel((val), ((dev)->ioaddr + (reg)));
}

static void rockchip_tsp_clear_interrupt(struct tsp_dev *dev)
{
	uint reg_val = 0;

	reg_val = TSP_RD(dev, PTI0_DMA_STS);
	TSP_WR(dev, PTI0_DMA_STS, reg_val);
	reg_val = TSP_RD(dev, PTI0_PID_STS0);
	TSP_WR(dev, PTI0_PID_STS0, reg_val);
	reg_val = TSP_RD(dev, PTI0_PID_STS1);
	TSP_WR(dev, PTI0_PID_STS1, reg_val);
	reg_val = TSP_RD(dev, PTI0_PID_STS2);
	TSP_WR(dev, PTI0_PID_STS2, reg_val);
	reg_val = TSP_RD(dev, PTI0_PID_STS3);
	TSP_WR(dev, PTI0_PID_STS3, reg_val);
}

static void rockchip_tsp_reset_all_channel(struct tsp_dev *dev)
{
	int i;
	uint reg_val = 0;
	int ret;

	for (i = 0; i < 64; i++) {
		/* pid channel clear */
		TSP_WR(dev, PTI0_PID0_CTRL + 4 * i, 1 << 1);

		ret = readl_poll_timeout(dev->ioaddr + PTI0_PID0_CTRL + 4 * i,
					 reg_val, !(reg_val & 0x03), 100,
					 TSP_TIMEOUT_US);
		if (ret)
			dev_err(dev->dev, "failed to clear pid channel\n");
	}
}

static void rockchip_tsp_reset_one_channel(struct tsp_dev *dev, int id)
{
	uint reg_val = 0;
	int ret;

	/* pid channel clear */
	TSP_WR(dev, PTI0_PID0_CTRL + 4 * id, 1 << 1);
	if (id < 64 && id >= 0) {
		ret = readl_poll_timeout(dev->ioaddr + PTI0_PID0_CTRL + 4 * id,
					 reg_val, !(reg_val & 0x03), 100,
					 TSP_TIMEOUT_US);
		if (ret)
			dev_err(dev->dev, "failed to clear pid channel\n");
	}
}

static void rockchip_tsp_soft_reset(struct tsp_dev *dev)
{
	uint reg_val = 0;
	int ret;

	/* pti0 soft reset */
	TSP_WR(dev, PTI0_CTRL, 1);

	ret = readl_poll_timeout(dev->ioaddr + PTI0_CTRL,
				 reg_val, !(reg_val & 0x01), 100,
				 TSP_TIMEOUT_US);
	if (ret)
		dev_err(dev->dev, "failed to reset pti0\n");
}

static void rockchip_tsp_gcfg(struct tsp_dev *dev)
{
	int reg_val = 0;

	reg_val |= ((4 << 4) | 0x01);
	TSP_WR(dev, TSP_GCFG,  reg_val);
}

static void rockchip_tsp_grf_config(struct tsp_dev *dev)
{
	if (dev->pdata->soc_type == RK312X) {
		grf_field_write(dev, TSP_D0, 1);
		grf_field_write(dev, TSP_D1, 1);
		grf_field_write(dev, TSP_D2, 1);
		grf_field_write(dev, TSP_D3, 1);
		grf_field_write(dev, TSP_D4, 1);
		grf_field_write(dev, TSP_D5M0, 1);
		grf_field_write(dev, TSP_D6M0, 1);
		grf_field_write(dev, TSP_D7M0, 1);
		grf_field_write(dev, TSP_SYNCM0, 1);
		grf_field_write(dev, TSP_FAIL, 1);
		grf_field_write(dev, TSP_VALID, 1);
		grf_field_write(dev, TSP_CLK, 1);
	} else if (dev->pdata->soc_type == RK3328) {
		grf_field_write(dev, TSP_SYNCM1, 3);
		grf_field_write(dev, TSP_IO_GROUP_SEL, 1);
	}
}

static int rockchip_tsp_hsadc_config(struct tsp_dev *dev)
{
	uint reg_ctrl = 0;

	if (dev->serial_parallel_mode == 0) {
		reg_ctrl = 1 << 21 | 0 << 19 | 0 << 18 | 2 << 16 |
			   1 << 13 | SERIAL_SYNC_VALID_MODE |
			   TSI_SEL | 3 << 4 | 3 << 1;
	} else {
		reg_ctrl = 0 << 19 | 0 << 18 | 2 << 16 |
			   1 << 13 | PARALLEL_SYNC_VALID_MODE |
			   TSI_SEL | 3 << 4 | 3 << 1;
	}

	TSP_WR(dev, PTI0_CTRL, reg_ctrl);

	return 0;
}

static void rockchip_tsp_init(struct tsp_dev *dev)
{
	rockchip_tsp_gcfg(dev);
	rockchip_tsp_clear_interrupt(dev);
	rockchip_tsp_reset_all_channel(dev);
	rockchip_tsp_soft_reset(dev);
	rockchip_tsp_grf_config(dev);
	rockchip_tsp_hsadc_config(dev);
	mutex_init(&dev->ts_mutex);
	dev->tsp_start_descram = 0;
}

static inline int rockchip_find_next_package(const u8 *buf, size_t count)
{
	int pos = 0;

	while (pos < count) {
		if (buf[pos] == 0x47 &&
		    buf[pos + 188] == 0x47 &&
		    buf[pos + 188 + 188] == 0x47)
			break;

		pos++;
	}

	return pos;
}

static void rockchip_feed_sec_buf(struct tsp_dev *dev)
{
	struct tsp_ctx *ctx, *ctx_temp;
	unsigned long flags;
	uint data_len, data_len_head, data_len_tail;
	uint phy_base, phy_write;
	u8 *write_addr, *read_addr, *top_addr, *base_addr;
	int index, pid;
	int temp;
	int offset;

	spin_lock_irqsave(&dev->list_lock, flags);
	list_for_each_entry_safe(ctx, ctx_temp, &dev->pid_list, pid_list) {
		index = ctx->index;
		read_addr = ctx->read;
		top_addr = ctx->top;
		base_addr = ctx->base;
		pid = ctx->pid;
		temp = PTI0_PID0_WRITE + index * 16;
		phy_write = TSP_RD(dev, temp);
		temp = PTI0_PID0_BASE + index * 16;
		phy_base = TSP_RD(dev, temp);

		/* get virtual write address */
		offset = phy_write - phy_base;
		write_addr = base_addr + offset;

		if (write_addr > top_addr || write_addr < base_addr)
			dev_err(dev->dev, "%s:%d, write addr is error!",
				__func__, __LINE__);

		if (read_addr > top_addr || read_addr < base_addr)
			dev_err(dev->dev, "%s:%d, read addr is error!",
				__func__, __LINE__);

		if (ctx->filter_type == TSP_SECTION_FILTER) {
			u32 ctrl_val, cfg_val;

			ctrl_val = TSP_RD(dev, PTI0_PID0_CTRL + 4 * index);
			if (!(ctrl_val & 0x01))
				pr_err("PID0_CTRL is error:0x%x, channel id=%d\n",
				       ctrl_val, index);

			cfg_val = TSP_RD(dev, PTI0_PID0_CFG + 20 * index);
			if (cfg_val == 0)
				pr_err("PID0_CFG is error:0x%x, channel_id=%d\n",
				       cfg_val, index);

			if ((!(ctrl_val & 0x01)) || cfg_val == 0)
				rockchip_sec_filter_config(dev, ctx);

			if (write_addr > read_addr) {
				data_len = write_addr - read_addr;
				ctx->get_data_callback(read_addr,
						       data_len, pid);
				read_addr += data_len;
			} else if (write_addr < read_addr) {
				data_len_tail = top_addr - read_addr;

				ctx->get_data_callback(read_addr,
						       data_len_tail, pid);

				data_len_head = write_addr - base_addr;

				ctx->get_data_callback(base_addr,
						       data_len_head, pid);
				read_addr = base_addr + data_len_head;
			}
		}
		ctx->read = read_addr;
	}
	spin_unlock_irqrestore(&dev->list_lock, flags);
}

#define GET_PACKET_SYNC_FROM_DMX
static u8 *rockchip_handle_ts_data(struct tsp_ctx *ctx)
{
	int data_len;
	u8 *read_addr = ctx->read;
	u8 *write_addr = ctx->write;

	data_len = write_addr - read_addr;

#ifdef GET_PACKET_SYNC_FROM_DMX
	if (data_len > 0)
		ctx->get_data_callback(read_addr, data_len, ctx->pid);

	read_addr = write_addr;
#else
	if (data_len >= PACKET_SIZE && read_addr[0] == 0x47) {
		data_len = (data_len / PACKET_SIZE) * PACKET_SIZE;
		ctx->get_data_callback(read_addr, data_len, ctx->pid);
		read_addr += data_len;
	} else {
		uint pos;

		pos = rockchip_find_next_package(read_addr, data_len);
		if (pos < data_len) {
			read_addr += pos;
			data_len -= pos;
			data_len = (data_len / PACKET_SIZE) * PACKET_SIZE;
			ctx->get_data_callback(read_addr, data_len, ctx->pid);
			read_addr += data_len;
		} else {
			read_addr += data_len;
		}
	}
#endif

	return read_addr;
}

static u8 *rockchip_handle_ts_tail(struct tsp_ctx *ctx)
{
	int data_len;
	u8 *base_addr = ctx->base;
	u8 *top_addr = ctx->top;
	u8 *read_addr = ctx->read;

	data_len = top_addr - read_addr;
#ifdef GET_PACKET_SYNC_FROM_DMX
	if (data_len > 0)
		ctx->get_data_callback(read_addr, data_len, ctx->pid);

	read_addr = base_addr;
#else
	if (data_len >= PACKET_SIZE && read_addr[0] == 0x47) {
		data_len = (data_len / PACKET_SIZE) * PACKET_SIZE;
		ctx->get_data_callback(read_addr, data_len, ctx->pid);
		read_addr += data_len;
	} else if (data_len > PACKET_SIZE * 3) {
		uint pos;

		pos = rockchip_find_next_package(read_addr, data_len);
		if (pos < data_len) {
			read_addr += pos;
			data_len -= pos;
			data_len = (data_len / PACKET_SIZE) * PACKET_SIZE;

			ctx->get_data_callback(read_addr, data_len, ctx->pid);
			read_addr += data_len;
		} else {
			read_addr += data_len;
		}
	}
#endif

	return read_addr;
}

static u8 *rockchip_handle_ts_head(struct tsp_ctx *ctx)
{
	int data_len;
	u8 *read_addr;
	u8 *base_addr = ctx->base;
	u8 *write_addr = ctx->write;

	data_len = write_addr - base_addr;
#ifdef GET_PACKET_SYNC_FROM_DMX
	if (data_len > 0)
		ctx->get_data_callback(base_addr, data_len, ctx->pid);

	read_addr = write_addr;
#else
	if (data_len >= PACKET_SIZE && base_addr[0] == 0x47) {
		data_len = (data_len / PACKET_SIZE) * PACKET_SIZE;

		ctx->get_data_callback(base_addr, data_len, ctx->pid);
		read_addr = base_addr;
		read_addr += data_len;
	} else if (data_len > PACKET_SIZE * 3) {
		uint pos;

		pos = rockchip_find_next_package(base_addr, data_len);
		if (pos < data_len) {
			read_addr = base_addr + pos;
			data_len -= pos;
			data_len = (data_len / PACKET_SIZE) * PACKET_SIZE;
			ctx->get_data_callback(read_addr, data_len, ctx->pid);
			read_addr += data_len;
		} else {
			read_addr = base_addr + data_len;
		}
	} else {
		/* data_len_head == 0 */
		read_addr = base_addr;
	}
#endif

	return read_addr;
}

void rockchip_tsp_reset_regs(struct tsp_dev *dev)
{
	unsigned long flags;
	struct tsp_ctx *ctx, *ctx_temp;

	rockchip_tsp_clear_interrupt(dev);
	rockchip_tsp_soft_reset(dev);

	rockchip_tsp_gcfg(dev);
	rockchip_tsp_grf_config(dev);
	rockchip_tsp_hsadc_config(dev);

	spin_lock_irqsave(&dev->list_lock, flags);
	list_for_each_entry_safe(ctx, ctx_temp, &dev->pid_list, pid_list) {
		if (ctx->base)
			memset(ctx->base, 0, ctx->buf_len);

		if (ctx->filter_type == TSP_TS_FILTER) {
			rockchip_demux_dma_config(dev, ctx);
			rockchip_ts_filter_config(dev, ctx);
		} else if (ctx->filter_type == TSP_SECTION_FILTER) {
			rockchip_demux_dma_config(dev, ctx);
			rockchip_sec_filter_config(dev, ctx);
			rockchip_enable_pid_irq(dev, ctx->index);
		}
	}
	spin_unlock_irqrestore(&dev->list_lock, flags);
}

static void rockchip_feed_ts_buf(struct tsp_dev *dev)
{
	struct tsp_ctx *ctx, *ctx_temp;
	int id;
	int temp;
	u32 phy_base, phy_write;
	int offset;
	u32 ctrl_val, cfg_val, state_val, erro_state_occur;
	unsigned long flags;
#ifdef TSP_DESCRAM_TIMER_CHECK
	int tsp_live_no_data_interval = 1;
#endif
	static int tsp_normal_no_data_interval;

	spin_lock_irqsave(&dev->list_lock, flags);
	list_for_each_entry_safe(ctx, ctx_temp, &dev->pid_list, pid_list) {
		id = ctx->index;
		temp = PTI0_PID0_WRITE + id * 16;
		phy_write = TSP_RD(dev, temp);
		temp = PTI0_PID0_BASE + id * 16;
		phy_base = TSP_RD(dev, temp);

		/* get virtual write address */
		offset = phy_write - phy_base;
		ctx->write = ctx->base + offset;

		if (ctx->write > ctx->top || ctx->write < ctx->base) {
			pr_err("%s:%d, write addr is error!",
			       __func__, __LINE__);
			continue;
		}
		if (ctx->read > ctx->top || ctx->read < ctx->base) {
			pr_err("%s:%d, read addr is error!",
			       __func__, __LINE__);
			continue;
		}

		/*ts*/
		if (ctx->filter_type == TSP_TS_FILTER) {
			ctrl_val = TSP_RD(dev, PTI0_PID0_CTRL + 4 * id);
			if (!(ctrl_val & 0x01))
				pr_err("CTRL is error: 0x%x, channel id = %d\n",
				       ctrl_val, id);
			cfg_val = TSP_RD(dev, PTI0_PID0_CFG + 20 * id);
			if ((cfg_val & 0x30) != 0x30)
				pr_err("CFG is error: 0x%x, channel id = %d\n",
				       cfg_val, id);

			erro_state_occur = 0;
			if ((!(ctrl_val & 0x01)) || ((cfg_val & 0x30) != 0x30))
				erro_state_occur = 1;

			if (id < 32) {
				state_val = TSP_RD(dev, PTI0_PID_STS2);
				if (state_val & (1 << id)) {
					erro_state_occur = 1;
					pr_err("err channel id = %d\n", id);
					state_val |= (1 << id);
					TSP_WR(dev, PTI0_PID_STS2, state_val);
				}
			} else {
				state_val = TSP_RD(dev, PTI0_PID_STS3);
				if (state_val & (1 << (id - 32))) {
					erro_state_occur = 1;
					pr_err("err channel id = %d\n", id);
					state_val |= (1 << (id - 32));
					TSP_WR(dev, PTI0_PID_STS3, state_val);
				}
			}

			if (erro_state_occur != 0) {
				spin_unlock_irqrestore(&dev->list_lock, flags);
				goto error;
			}

			if (ctx->write > ctx->read) {
#ifdef TSP_DESCRAM_TIMER_CHECK
				tsp_live_no_data_interval = 0;
#endif
				tsp_normal_no_data_interval = 0;
				ctx->read = (u8 *)rockchip_handle_ts_data(ctx);
			} else if (ctx->write < ctx->read) {
#ifdef TSP_DESCRAM_TIMER_CHECK
				tsp_live_no_data_interval = 0;
#endif
				tsp_normal_no_data_interval = 0;
				/* tail */
				ctx->read = rockchip_handle_ts_tail(ctx);

				/* head */
				ctx->read = rockchip_handle_ts_head(ctx);
			}
		}
	}
	spin_unlock_irqrestore(&dev->list_lock, flags);

#ifdef TSP_DESCRAM_TIMER_CHECK
	if (tsp_live_no_data_interval != 0 &&
	    dev->tsp_start_descram != 0) {
		goto error;
	}

	if (tsp_normal_no_data_interval++ > TIME_CNT_PER_SECOND &&
	    dev->tsp_start_descram == 0) {
		tsp_normal_no_data_interval = 0;
		goto error;
	}
#else
	if (tsp_normal_no_data_interval++ > TIME_CNT_PER_SECOND) {
		tsp_normal_no_data_interval = 0;
		goto error;
	}
#endif

	return;
error:
	rockchip_tsp_reset_regs(dev);
}

static irqreturn_t rockchip_tsp_interrupt(int irq, void *dev_id)
{
	uint reg_val;
	struct platform_device *pdev = dev_id;
	struct tsp_dev *dev = platform_get_drvdata(pdev);

	if (irq == dev->tsp_irq) {
		reg_val = TSP_RD(dev, PTI0_PID_STS0);
		if (reg_val != 0) {
			TSP_WR(dev, PTI0_PID_STS0, reg_val);
			queue_work(dev->sec_queue, &dev->sec_work);
		}

		reg_val = TSP_RD(dev, PTI0_PID_STS1);
		if (reg_val != 0) {
			TSP_WR(dev, PTI0_PID_STS1, reg_val);
			queue_work(dev->sec_queue, &dev->sec_work);
		}

		reg_val = TSP_RD(dev, PTI0_DMA_STS);
		if (reg_val & 0x03)
			TSP_WR(dev, PTI0_DMA_STS, reg_val);
	}

	return IRQ_HANDLED;
}

static void rockchip_ts_work(struct work_struct *work)
{
	struct tsp_dev *dev;

	dev = container_of(work, struct tsp_dev, ts_work);

	mutex_lock(&dev->ts_mutex);
	rockchip_feed_ts_buf(dev);
	mutex_unlock(&dev->ts_mutex);
}

static void rockchip_ts_timer_handler(unsigned long data)
{
	struct tsp_dev *dev = (struct tsp_dev *)data;

	queue_work(dev->ts_queue, &dev->ts_work);
	mod_timer(&dev->timer, jiffies + TSP_TIMER_DELAY);
}

static void rockchip_ts_timer(struct tsp_dev *dev)
{
	dev->ts_queue = create_workqueue("ts_wqueue");
	INIT_WORK(&dev->ts_work, rockchip_ts_work);

	/* Register timer */
	setup_timer(&dev->timer, rockchip_ts_timer_handler,
		    (unsigned long)dev);
	mod_timer(&dev->timer, jiffies + TSP_TIMER_DELAY);
}

static void rockchip_sec_work(struct work_struct *work)
{
	struct tsp_dev *dev;

	dev = container_of(work, struct tsp_dev, sec_work);

	mutex_lock(&dev->ts_mutex);
	rockchip_feed_sec_buf(dev);
	mutex_unlock(&dev->ts_mutex);
}

static void rockchip_sec_queue(struct tsp_dev *dev)
{
	dev->sec_queue = create_workqueue("sec_wqueue");
	INIT_WORK(&dev->sec_work, rockchip_sec_work);
}

static void rockchip_demux_dma_config(struct tsp_dev *dev,
				      struct tsp_ctx *ctx)
{
	u32 id = ctx->index;
	u32 dlen = ctx->buf_len;
	u32 dma_buf = ctx->dma_buf;

	TSP_WR(dev, PTI0_PID0_BASE + id * 16, dma_buf);
	TSP_WR(dev, PTI0_PID0_TOP + id * 16, dma_buf + dlen);
	TSP_WR(dev, PTI0_PID0_READ + id * 16, dma_buf);
	TSP_WR(dev, PTI0_PID0_WRITE + id * 16, dma_buf);
}

static int rockchip_demux_alloc(struct tsp_dev *dev, struct tsp_ctx *tctx)
{
	int count = 3;

	while (count--) {
		tctx->base = dma_alloc_writecombine(dev->dev, tctx->buf_len,
						    &tctx->dma_buf, GFP_KERNEL);
		if (tctx->base) {
			memset(tctx->base, 0, tctx->buf_len);
			break;
		}
	}

	if (!tctx->base) {
		pr_err("%s:%d, alloc cyc buf fail!!\n", __func__, __LINE__);
		return -ENOMEM;
	}
	tctx->top = tctx->base + tctx->buf_len;
	tctx->write = tctx->base;
	tctx->read = tctx->base;
	return 0;
}

static void rockchip_demux_free(struct tsp_dev *dev, struct tsp_ctx *ctx)
{
	if (ctx->base) {
		dma_free_writecombine(dev->dev, ctx->buf_len,
				      ctx->base, ctx->dma_buf);
		ctx->base = NULL;
	}
}

static void rockchip_ts_filter_config(struct tsp_dev *dev, struct tsp_ctx *ctx)
{
	int pid = ctx->pid;
	u32 id = ctx->index;
	u32 reg_val;
	u32 ctrl_reg;

	TSP_WR(dev, PTI0_PID0_CTRL + 4 * id, pid << 3 | 0 << 2 | 1);
	TSP_WR(dev, PTI0_PID0_CFG + 20 * id, 3 << 4);

	/* cw order num & descram on */
	reg_val = TSP_RD(dev, PTI0_PID0_CTRL + 4 * id);
	ctrl_reg = reg_val | (TSP_CW_ORDER_NUM_SEL << 16) | (0 << 2);
	TSP_WR(dev, PTI0_PID0_CTRL + 4 * id, ctrl_reg);
}

static void rockchip_sec_filter_config(struct tsp_dev *dev,
				       struct tsp_ctx *tctx)
{
	int pid = tctx->pid;
	u32 id = tctx->index;
	u32 cfg = 0;
	u32 filter0 = 0;
	u32 filter1 = 0;
	u8 byte0 = tctx->filter_byte[0];
	u8 byte3 = tctx->filter_byte[3];
	u8 byte4 = tctx->filter_byte[4];
	u8 byte5 = tctx->filter_byte[5];
	u8 byte6 = tctx->filter_byte[6];
	u8 byte7 = tctx->filter_byte[7];
	int use_hardware_filter = 0;

	if (use_hardware_filter != 0) {
		if (tctx->filter_mask[0] == 0xFF) {
			cfg |= (1 << 16);
			filter0 |= byte0;
		}

		if (tctx->filter_mask[3] == 0xFF) {
			cfg |= (1 << 17);
			filter0 |= (byte3 << 8);
		}

		if (tctx->filter_mask[4] == 0xFF) {
			cfg |= (1 << 18);
			filter0 |= (byte4 << 16);
		}

		if (tctx->filter_mask[5] == 0xFF) {
			cfg |= (1 << 19);
			filter0 |= byte5 << 24;
		}

		if (tctx->filter_mask[6] == 0xFF) {
			cfg |= (1 << 20);
			filter1 |= byte6;
		}

		if (tctx->filter_mask[7] == 0xFF) {
			cfg |= (1 << 21);
			filter1 |= byte7 << 8;
		}
	}

	cfg |= (2 << 8);

	TSP_WR(dev, PTI0_PID0_CTRL + 4 * id, pid << 3 | 1);
	TSP_WR(dev, PTI0_PID0_CFG + 20 * id, cfg);
	TSP_WR(dev, PTI0_PID0_FILT_0 + 20 * id, filter0);
	TSP_WR(dev, PTI0_PID0_FILT_1 + 20 * id, filter1);
	TSP_WR(dev, PTI0_PID0_FILT_2 + 20 * id, 0);
	TSP_WR(dev, PTI0_PID0_FILT_3 + 20 * id, 0);
}

static void rockchip_enable_pid_irq(struct tsp_dev *dev, int id)
{
	u32 val;

	if (id < 32) {
		val = TSP_RD(dev, PTI0_PID_INT_ENA0);
		TSP_WR(dev, PTI0_PID_INT_ENA0, val | (1 << id));
		val = TSP_RD(dev, PTI0_PID_INT_ENA2);
		TSP_WR(dev, PTI0_PID_INT_ENA2, val | (1 << id));

	} else {
		val = TSP_RD(dev, PTI0_PID_INT_ENA1);
		TSP_WR(dev, PTI0_PID_INT_ENA1, val | (1 << (id - 32)));
		val = TSP_RD(dev, PTI0_PID_INT_ENA3);
		TSP_WR(dev, PTI0_PID_INT_ENA3, val | (1 << (id - 32)));
	}
}

static void rockchip_init_pid_list(struct tsp_dev *dev)
{
	INIT_LIST_HEAD(&dev->pid_list);
	spin_lock_init(&dev->list_lock);
}

static void rockchip_exit_pid_list(struct tsp_dev *dev)
{
	struct tsp_ctx *ctx, *ctx_temp;
	unsigned long flags;

	spin_lock_irqsave(&dev->list_lock, flags);
	list_for_each_entry_safe(ctx, ctx_temp,
				 &dev->pid_list, pid_list) {
		list_del(&ctx->pid_list);
		rockchip_tsp_reset_one_channel(dev, ctx->index);
		rockchip_demux_free(dev, ctx);
		kfree(ctx);
		ctx = NULL;
	}
	spin_unlock_irqrestore(&dev->list_lock, flags);
}

int rockchip_tsp_open(void)
{
	int ret = 0;
	struct tsp_dev *dev = tdev;

	if (dev->is_open != 0)
		return 0;

	dev->is_open = 1;

	rockchip_tsp_clk_enable(dev);
	rockchip_init_pid_list(dev);
	rockchip_tsp_init(dev);
	rockchip_ts_timer(dev);
	rockchip_sec_queue(dev);

	return ret;
}

int rockchip_tsp_close(void)
{
	struct tsp_dev *dev = tdev;

	if (dev->is_open == 0)
		return 0;

	del_timer_sync(&dev->timer);
	rockchip_tsp_clear_interrupt(dev);
	rockchip_tsp_reset_all_channel(dev);
	rockchip_tsp_soft_reset(dev);
	rockchip_tsp_clk_disable(dev);

	if (dev->ts_queue) {
		destroy_workqueue(dev->ts_queue);
		dev->ts_queue = NULL;
	}

	if (dev->sec_queue) {
		destroy_workqueue(dev->sec_queue);
		dev->sec_queue = NULL;
	}

	rockchip_exit_pid_list(dev);
	dev->is_open = 0;

	return 0;
}

void rockchip_tsp_stop_channel(struct rockchip_tsp_channel_info *info)
{
	int pid, index;
	unsigned long flags;
	struct tsp_ctx *ctx, *ctx_temp, *ctx_to_free[64];
	struct tsp_dev *dev = tdev;
	int i = 0, cnt = 0;

	for (i = 0; i < 64; i++)
		ctx_to_free[i] = NULL;

	if (info->pid > 0x1FFF || info->index >= 64)
		return;

	mutex_lock(&dev->ts_mutex);
	pid = info->pid;
	index = info->index;
	rockchip_tsp_reset_one_channel(dev, index);

	spin_lock_irqsave(&dev->list_lock, flags);
	list_for_each_entry_safe(ctx, ctx_temp,
				 &dev->pid_list, pid_list) {
		if (ctx->pid == pid && ctx->index == index) {
			list_del(&ctx->pid_list);
			ctx_to_free[cnt] = ctx;
			cnt++;
		}
	}
	spin_unlock_irqrestore(&dev->list_lock, flags);

	for (i = 0; i < 64; i++) {
		if (ctx_to_free[i]) {
			rockchip_demux_free(dev, ctx_to_free[i]);
			kfree(ctx_to_free[i]);
			ctx_to_free[i] = NULL;
		}
	}
	mutex_unlock(&dev->ts_mutex);
}

int rockchip_tsp_start_channel(struct rockchip_tsp_channel_info *info)
{
	struct tsp_ctx *ctx;
	struct tsp_dev *dev = tdev;
	unsigned long flags;
	int ret = 0;
	int type;

	if (!info) {
		pr_err("channel_info is null!!\n");
		return -ENODEV;
	}

	if (info->pid > 0x1FFF || info->index >= 64)
		return -EINVAL;

	mutex_lock(&dev->ts_mutex);
	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx) {
		ret = -ENOMEM;
		goto out;
	}
	ctx->pid = info->pid;
	ctx->index = info->index;
	ctx->get_data_callback = info->get_data_callback;
	type = info->type;

	if (type == TSP_DMX_TYPE_TS) {
		ctx->filter_type = TSP_TS_FILTER;
		if (info->type == TSP_DMX_TYPE_SEC)
			ctx->buf_len = TS_CYC_BUF_LEN_PSI;
		else
			ctx->buf_len = TS_CYC_BUF_LEN;

		ret = rockchip_demux_alloc(dev, ctx);
		if (ret)
			goto out;

		rockchip_demux_dma_config(dev, ctx);
		rockchip_ts_filter_config(dev, ctx);
		spin_lock_irqsave(&dev->list_lock, flags);
		list_add(&ctx->pid_list, &dev->pid_list);
		spin_unlock_irqrestore(&dev->list_lock, flags);
	} else if (type == TSP_DMX_TYPE_SEC) {
		ctx->filter_type = TSP_SECTION_FILTER;
		ctx->buf_len = SECTION_CYC_BUF_LEN;
		memcpy(ctx->filter_byte, info->filter_value,
		       TSP_DMX_FILTER_SIZE);
		memcpy(ctx->filter_mask, info->filter_mask,
		       TSP_DMX_FILTER_SIZE);

		ret = rockchip_demux_alloc(dev, ctx);
		if (ret)
			goto out;

		rockchip_demux_dma_config(dev, ctx);
		rockchip_sec_filter_config(dev, ctx);
		rockchip_enable_pid_irq(dev, ctx->index);
		spin_lock_irqsave(&dev->list_lock, flags);
		list_add(&ctx->pid_list, &dev->pid_list);
		spin_unlock_irqrestore(&dev->list_lock, flags);
	}

out:
	if (ret)
		kfree(ctx);
	mutex_unlock(&dev->ts_mutex);
	return ret;
}

static int rockchip_tsp_probe(struct platform_device *pdev)
{
	int err = -ENODEV;
	struct tsp_dev *pdata;
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct device_node *np = dev->of_node;

	/* get I/O memory resource */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(dev, "failed to get I/O memory resource\n");
		return -ENODEV;
	}

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	/* request I/O memory */
	if (!devm_request_mem_region(dev, res->start,
				     resource_size(res), pdev->name)) {
		dev_err(dev, "request I/O memory fial\n");
		return -EBUSY;
	}
	pdata->ioaddr = devm_ioremap(dev, res->start, resource_size(res));

	pdata->tsp_aclk = clk_get(dev, "aclk_tsp");
	if (IS_ERR(pdata->tsp_aclk)) {
		dev_err(dev, "failed to find tsp aclk source\n");
		err = PTR_ERR(pdata->tsp_aclk);
		return err;
	}

	pdata->tsp_hclk = clk_get(dev, "hclk_tsp");
	if (IS_ERR(pdata->tsp_hclk)) {
		dev_err(dev, "failed to find tsp hclk source\n");
		err = PTR_ERR(pdata->tsp_hclk);
		return err;
	}

	pdata->tsp_clk = clk_get(dev, "clk_tsp");
	if (IS_ERR(pdata->tsp_clk)) {
		dev_err(dev, "failed to find tsp clk source\n");
		err = PTR_ERR(pdata->tsp_clk);
		return err;
	}

	pdata->grf = syscon_regmap_lookup_by_phandle(np, "rockchip,grf");
	if (IS_ERR(pdata->grf)) {
		pdata->grf = NULL;
		dev_err(dev, "failed to find tsp grf base.\n");
	}

	pdata->tsp_irq = platform_get_irq_byname(pdev, "irq_tsp");
	if (pdata->tsp_irq < 0) {
		err = pdata->tsp_irq;
		dev_warn(dev, "tsp interrupt is not available.\n");
		return err;
	}

	err = devm_request_irq(dev, pdata->tsp_irq,
			       rockchip_tsp_interrupt,
			       IRQF_SHARED, pdev->name, pdev);
	if (err < 0) {
		dev_warn(dev, "tsp interrupt is not available.\n");
		return err;
	}

	of_property_read_u32(dev->of_node, "serial_parallel_mode",
			     &pdata->serial_parallel_mode);

	pdata->dev = dev;
	pdata->pdata = of_device_get_match_data(dev);
	platform_set_drvdata(pdev, pdata);
	tdev = pdata;
	tdev->is_open = 0;

	dev_info(dev, "rk tsp driver registered\n");

	return 0;
}

static int rockchip_tsp_remove(struct platform_device *pdev)
{
	struct tsp_dev *pdata = platform_get_drvdata(pdev);

	if (!pdata)
		return -ENODEV;

	tdev = NULL;
	platform_set_drvdata(pdev, NULL);
	return 0;
}

#ifdef CONFIG_OF
static const u32 rk312x_grf_reg_fields[MAX_FIELDS] = {
	[TSP_D0] = GRF_REG_FIELD(0xec, 0, 0),
	[TSP_D1] = GRF_REG_FIELD(0xec, 2, 2),
	[TSP_D2] = GRF_REG_FIELD(0xec, 4, 4),
	[TSP_D3] = GRF_REG_FIELD(0xec, 6, 6),
	[TSP_D4] = GRF_REG_FIELD(0xec, 8, 8),
	[TSP_D5M0] = GRF_REG_FIELD(0xec, 10, 10),
	[TSP_D6M0] = GRF_REG_FIELD(0xec, 12, 12),
	[TSP_D7M0] = GRF_REG_FIELD(0xec, 14, 14),
	[TSP_SYNCM0] = GRF_REG_FIELD(0xf0, 0, 0),
	[TSP_FAIL] = GRF_REG_FIELD(0xf0, 2, 2),
	[TSP_VALID] = GRF_REG_FIELD(0xf0, 4, 4),
	[TSP_CLK] = GRF_REG_FIELD(0xf0, 6, 6),
};

static const struct rockchip_tsp_plat_data rk312x_socdata = {
	.soc_type = RK312X,
	.grf_reg_fields = rk312x_grf_reg_fields,
};

static const u32 rk3228_grf_reg_fields[MAX_FIELDS] = {
};

static const struct rockchip_tsp_plat_data rk3228_socdata = {
	.soc_type = RK3228,
	.grf_reg_fields = rk3228_grf_reg_fields,
};

static const u32 rk3328_grf_reg_fields[MAX_FIELDS] = {
	[TSP_SYNCM1] = GRF_REG_FIELD(0x28, 0, 2),
	[TSP_IO_GROUP_SEL] = GRF_REG_FIELD(0x50, 8, 8),
};

static const struct rockchip_tsp_plat_data rk3328_socdata = {
	.soc_type = RK3328,
	.grf_reg_fields = rk3328_grf_reg_fields,
};

static const struct of_device_id rockchip_tsp_dt_match[] = {
	{ .compatible = "rockchip,rk312x-tsp", .data = &rk312x_socdata, },
	{ .compatible = "rockchip,rk3228-tsp", .data = &rk3228_socdata, },
	{ .compatible = "rockchip,rk3328-tsp", .data = &rk3328_socdata, },
	{ },
};
MODULE_DEVICE_TABLE(of, rockchip_tsp_dt_match);
#endif /* CONFIG_OF */

static struct platform_driver rk_tsp_driver = {
	.probe	= rockchip_tsp_probe,
	.remove	= rockchip_tsp_remove,
	.driver	= {
		.name	= MODE_NAME,
		.of_match_table = of_match_ptr(rockchip_tsp_dt_match),
	},
};

static int __init rockchip_tsp_mod_init(void)
{
	return platform_driver_register(&rk_tsp_driver);
}

static void __exit rockchip_tsp_mod_exit(void)
{
	platform_driver_unregister(&rk_tsp_driver);
}

subsys_initcall(rockchip_tsp_mod_init);
module_exit(rockchip_tsp_mod_exit);

MODULE_DESCRIPTION("Rockchip Transport Stream Processing hw support.");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Jerry Xu <xbl@rock-chips.com>");
MODULE_ALIAS("platform:" MODE_NAME);
