/*
 * Copyright (c) 2014 MediaTek Inc.
 * Author: Xudong Chen <xudong.chen@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/sched.h>
#include <linux/slab.h>

#define I2C_RS_TRANSFER			(1 << 4)
#define I2C_HS_NACKERR			(1 << 2)
#define I2C_ACKERR			(1 << 1)
#define I2C_TRANSAC_COMP		(1 << 0)
#define I2C_TRANSAC_START		(1 << 0)
#define I2C_RS_MUL_CNFG			(1 << 15)
#define I2C_RS_MUL_TRIG			(1 << 14)
#define I2C_DCM_DISABLE			0x0000
#define I2C_IO_CONFIG_OPEN_DRAIN	0x0003
#define I2C_IO_CONFIG_PUSH_PULL		0x0000
#define I2C_SOFT_RST			0x0001
#define I2C_FIFO_ADDR_CLR		0x0001
#define I2C_DELAY_LEN			0x0002
#define I2C_ST_START_CON		0x8001
#define I2C_FS_START_CON		0x1800
#define I2C_TIME_CLR_VALUE		0x0000
#define I2C_TIME_DEFAULT_VALUE		0x0003
#define I2C_WRRD_TRANAC_VALUE		0x0002
#define I2C_RD_TRANAC_VALUE		0x0001

#define I2C_DMA_CON_TX			0x0000
#define I2C_DMA_CON_RX			0x0001
#define I2C_DMA_START_EN		0x0001
#define I2C_DMA_INT_FLAG_NONE		0x0000
#define I2C_DMA_CLR_FLAG		0x0000
#define I2C_DMA_HARD_RST		0x0002
#define I2C_DMA_4G_MODE			0x0001

#define I2C_DEFAULT_CLK_DIV		5
#define I2C_DEFAULT_SPEED		100000	/* hz */
#define MAX_FS_MODE_SPEED		400000
#define MAX_HS_MODE_SPEED		3400000
#define MAX_SAMPLE_CNT_DIV		8
#define MAX_STEP_CNT_DIV		64
#define MAX_HS_STEP_CNT_DIV		8

#define I2C_CONTROL_RS                  (0x1 << 1)
#define I2C_CONTROL_DMA_EN              (0x1 << 2)
#define I2C_CONTROL_CLK_EXT_EN          (0x1 << 3)
#define I2C_CONTROL_DIR_CHANGE          (0x1 << 4)
#define I2C_CONTROL_ACKERR_DET_EN       (0x1 << 5)
#define I2C_CONTROL_TRANSFER_LEN_CHANGE (0x1 << 6)
#define I2C_CONTROL_WRAPPER             (0x1 << 0)

#define I2C_DRV_NAME		"i2c-mt65xx"

enum DMA_REGS_OFFSET {
	OFFSET_INT_FLAG = 0x0,
	OFFSET_INT_EN = 0x04,
	OFFSET_EN = 0x08,
	OFFSET_RST = 0x0c,
	OFFSET_CON = 0x18,
	OFFSET_TX_MEM_ADDR = 0x1c,
	OFFSET_RX_MEM_ADDR = 0x20,
	OFFSET_TX_LEN = 0x24,
	OFFSET_RX_LEN = 0x28,
	OFFSET_TX_4G_MODE = 0x54,
	OFFSET_RX_4G_MODE = 0x58,
};

enum i2c_trans_st_rs {
	I2C_TRANS_STOP = 0,
	I2C_TRANS_REPEATED_START,
};

enum mtk_trans_op {
	I2C_MASTER_WR = 1,
	I2C_MASTER_RD,
	I2C_MASTER_WRRD,
};

enum I2C_REGS_OFFSET {
	OFFSET_DATA_PORT = 0x0,
	OFFSET_SLAVE_ADDR = 0x04,
	OFFSET_INTR_MASK = 0x08,
	OFFSET_INTR_STAT = 0x0c,
	OFFSET_CONTROL = 0x10,
	OFFSET_TRANSFER_LEN = 0x14,
	OFFSET_TRANSAC_LEN = 0x18,
	OFFSET_DELAY_LEN = 0x1c,
	OFFSET_TIMING = 0x20,
	OFFSET_START = 0x24,
	OFFSET_EXT_CONF = 0x28,
	OFFSET_FIFO_STAT = 0x30,
	OFFSET_FIFO_THRESH = 0x34,
	OFFSET_FIFO_ADDR_CLR = 0x38,
	OFFSET_IO_CONFIG = 0x40,
	OFFSET_RSV_DEBUG = 0x44,
	OFFSET_HS = 0x48,
	OFFSET_SOFTRESET = 0x50,
	OFFSET_DCM_EN = 0x54,
	OFFSET_PATH_DIR = 0x60,
	OFFSET_DEBUGSTAT = 0x64,
	OFFSET_DEBUGCTRL = 0x68,
	OFFSET_TRANSFER_LEN_AUX = 0x6c,
	OFFSET_CLOCK_DIV = 0x70,
};

struct mtk_i2c_compatible {
	const struct i2c_adapter_quirks *quirks;
	unsigned char pmic_i2c: 1;
	unsigned char dcm: 1;
	unsigned char auto_restart: 1;
	unsigned char aux_len_reg: 1;
	unsigned char support_33bits: 1;
	unsigned char timing_adjust: 1;
};

struct mtk_i2c {
	struct i2c_adapter adap;	/* i2c host adapter */
	struct device *dev;
	struct completion msg_complete;

	/* set in i2c probe */
	void __iomem *base;		/* i2c base addr */
	void __iomem *pdmabase;		/* dma base address*/
	struct clk *clk_main;		/* main clock for i2c bus */
	struct clk *clk_dma;		/* DMA clock for i2c via DMA */
	struct clk *clk_pmic;		/* PMIC clock for i2c from PMIC */
	bool have_pmic;			/* can use i2c pins from PMIC */
	bool use_push_pull;		/* IO config push-pull mode */

	u16 irq_stat;			/* interrupt status */
	unsigned int clk_src_div;
	unsigned int speed_hz;		/* The speed in transfer */
	enum mtk_trans_op op;
	u16 timing_reg;
	u16 high_speed_reg;
	unsigned char auto_restart;
	bool ignore_restart_irq;
	const struct mtk_i2c_compatible *dev_comp;
};

static const struct i2c_adapter_quirks mt6577_i2c_quirks = {
	.flags = I2C_AQ_COMB_WRITE_THEN_READ,
	.max_num_msgs = 1,
	.max_write_len = 255,
	.max_read_len = 255,
	.max_comb_1st_msg_len = 255,
	.max_comb_2nd_msg_len = 31,
};

static const struct i2c_adapter_quirks mt7622_i2c_quirks = {
	.max_num_msgs = 255,
};

static const struct mtk_i2c_compatible mt2712_compat = {
	.pmic_i2c = 0,
	.dcm = 1,
	.auto_restart = 1,
	.aux_len_reg = 1,
	.support_33bits = 1,
	.timing_adjust = 1,
};

static const struct mtk_i2c_compatible mt6577_compat = {
	.quirks = &mt6577_i2c_quirks,
	.pmic_i2c = 0,
	.dcm = 1,
	.auto_restart = 0,
	.aux_len_reg = 0,
	.support_33bits = 0,
	.timing_adjust = 0,
};

static const struct mtk_i2c_compatible mt6589_compat = {
	.quirks = &mt6577_i2c_quirks,
	.pmic_i2c = 1,
	.dcm = 0,
	.auto_restart = 0,
	.aux_len_reg = 0,
	.support_33bits = 0,
	.timing_adjust = 0,
};

static const struct mtk_i2c_compatible mt7622_compat = {
	.quirks = &mt7622_i2c_quirks,
	.pmic_i2c = 0,
	.dcm = 1,
	.auto_restart = 1,
	.aux_len_reg = 1,
	.support_33bits = 0,
	.timing_adjust = 0,
};

static const struct mtk_i2c_compatible mt8173_compat = {
	.pmic_i2c = 0,
	.dcm = 1,
	.auto_restart = 1,
	.aux_len_reg = 1,
	.support_33bits = 1,
	.timing_adjust = 0,
};

static const struct of_device_id mtk_i2c_of_match[] = {
	{ .compatible = "mediatek,mt2712-i2c", .data = &mt2712_compat },
	{ .compatible = "mediatek,mt6577-i2c", .data = &mt6577_compat },
	{ .compatible = "mediatek,mt6589-i2c", .data = &mt6589_compat },
	{ .compatible = "mediatek,mt7622-i2c", .data = &mt7622_compat },
	{ .compatible = "mediatek,mt8173-i2c", .data = &mt8173_compat },
	{}
};
MODULE_DEVICE_TABLE(of, mtk_i2c_of_match);

static int mtk_i2c_clock_enable(struct mtk_i2c *i2c)
{
	int ret;

	ret = clk_prepare_enable(i2c->clk_dma);
	if (ret)
		return ret;

	ret = clk_prepare_enable(i2c->clk_main);
	if (ret)
		goto err_main;

	if (i2c->have_pmic) {
		ret = clk_prepare_enable(i2c->clk_pmic);
		if (ret)
			goto err_pmic;
	}
	return 0;

err_pmic:
	clk_disable_unprepare(i2c->clk_main);
err_main:
	clk_disable_unprepare(i2c->clk_dma);

	return ret;
}

static void mtk_i2c_clock_disable(struct mtk_i2c *i2c)
{
	if (i2c->have_pmic)
		clk_disable_unprepare(i2c->clk_pmic);

	clk_disable_unprepare(i2c->clk_main);
	clk_disable_unprepare(i2c->clk_dma);
}

static void mtk_i2c_init_hw(struct mtk_i2c *i2c)
{
	u16 control_reg;

	writew(I2C_SOFT_RST, i2c->base + OFFSET_SOFTRESET);

	/* Set ioconfig */
	if (i2c->use_push_pull)
		writew(I2C_IO_CONFIG_PUSH_PULL, i2c->base + OFFSET_IO_CONFIG);
	else
		writew(I2C_IO_CONFIG_OPEN_DRAIN, i2c->base + OFFSET_IO_CONFIG);

	if (i2c->dev_comp->dcm)
		writew(I2C_DCM_DISABLE, i2c->base + OFFSET_DCM_EN);

	if (i2c->dev_comp->timing_adjust)
		writew(I2C_DEFAULT_CLK_DIV - 1, i2c->base + OFFSET_CLOCK_DIV);

	writew(i2c->timing_reg, i2c->base + OFFSET_TIMING);
	writew(i2c->high_speed_reg, i2c->base + OFFSET_HS);

	/* If use i2c pin from PMIC mt6397 side, need set PATH_DIR first */
	if (i2c->have_pmic)
		writew(I2C_CONTROL_WRAPPER, i2c->base + OFFSET_PATH_DIR);

	control_reg = I2C_CONTROL_ACKERR_DET_EN |
		      I2C_CONTROL_CLK_EXT_EN | I2C_CONTROL_DMA_EN;
	writew(control_reg, i2c->base + OFFSET_CONTROL);
	writew(I2C_DELAY_LEN, i2c->base + OFFSET_DELAY_LEN);

	writel(I2C_DMA_HARD_RST, i2c->pdmabase + OFFSET_RST);
	udelay(50);
	writel(I2C_DMA_CLR_FLAG, i2c->pdmabase + OFFSET_RST);
}

/*
 * Calculate i2c port speed
 *
 * Hardware design:
 * i2c_bus_freq = parent_clk / (clock_div * 2 * sample_cnt * step_cnt)
 * clock_div: fixed in hardware, but may be various in different SoCs
 *
 * The calculation want to pick the highest bus frequency that is still
 * less than or equal to i2c->speed_hz. The calculation try to get
 * sample_cnt and step_cn
 */
static int mtk_i2c_calculate_speed(struct mtk_i2c *i2c, unsigned int clk_src,
				   unsigned int target_speed,
				   unsigned int *timing_step_cnt,
				   unsigned int *timing_sample_cnt)
{
	unsigned int step_cnt;
	unsigned int sample_cnt;
	unsigned int max_step_cnt;
	unsigned int base_sample_cnt = MAX_SAMPLE_CNT_DIV;
	unsigned int base_step_cnt;
	unsigned int opt_div;
	unsigned int best_mul;
	unsigned int cnt_mul;

	if (target_speed > MAX_HS_MODE_SPEED)
		target_speed = MAX_HS_MODE_SPEED;

	if (target_speed > MAX_FS_MODE_SPEED)
		max_step_cnt = MAX_HS_STEP_CNT_DIV;
	else
		max_step_cnt = MAX_STEP_CNT_DIV;

	base_step_cnt = max_step_cnt;
	/* Find the best combination */
	opt_div = DIV_ROUND_UP(clk_src >> 1, target_speed);
	best_mul = MAX_SAMPLE_CNT_DIV * max_step_cnt;

	/* Search for the best pair (sample_cnt, step_cnt) with
	 * 0 < sample_cnt < MAX_SAMPLE_CNT_DIV
	 * 0 < step_cnt < max_step_cnt
	 * sample_cnt * step_cnt >= opt_div
	 * optimizing for sample_cnt * step_cnt being minimal
	 */
	for (sample_cnt = 1; sample_cnt <= MAX_SAMPLE_CNT_DIV; sample_cnt++) {
		step_cnt = DIV_ROUND_UP(opt_div, sample_cnt);
		cnt_mul = step_cnt * sample_cnt;
		if (step_cnt > max_step_cnt)
			continue;

		if (cnt_mul < best_mul) {
			best_mul = cnt_mul;
			base_sample_cnt = sample_cnt;
			base_step_cnt = step_cnt;
			if (best_mul == opt_div)
				break;
		}
	}

	sample_cnt = base_sample_cnt;
	step_cnt = base_step_cnt;

	if ((clk_src / (2 * sample_cnt * step_cnt)) > target_speed) {
		/* In this case, hardware can't support such
		 * low i2c_bus_freq
		 */
		dev_dbg(i2c->dev, "Unsupported speed (%uhz)\n",	target_speed);
		return -EINVAL;
	}

	*timing_step_cnt = step_cnt - 1;
	*timing_sample_cnt = sample_cnt - 1;

	return 0;
}

static int mtk_i2c_set_speed(struct mtk_i2c *i2c, unsigned int parent_clk)
{
	unsigned int clk_src;
	unsigned int step_cnt;
	unsigned int sample_cnt;
	unsigned int target_speed;
	int ret;

	clk_src = parent_clk / i2c->clk_src_div;
	target_speed = i2c->speed_hz;

	if (target_speed > MAX_FS_MODE_SPEED) {
		/* Set master code speed register */
		ret = mtk_i2c_calculate_speed(i2c, clk_src, MAX_FS_MODE_SPEED,
					      &step_cnt, &sample_cnt);
		if (ret < 0)
			return ret;

		i2c->timing_reg = (sample_cnt << 8) | step_cnt;

		/* Set the high speed mode register */
		ret = mtk_i2c_calculate_speed(i2c, clk_src, target_speed,
					      &step_cnt, &sample_cnt);
		if (ret < 0)
			return ret;

		i2c->high_speed_reg = I2C_TIME_DEFAULT_VALUE |
			(sample_cnt << 12) | (step_cnt << 8);
	} else {
		ret = mtk_i2c_calculate_speed(i2c, clk_src, target_speed,
					      &step_cnt, &sample_cnt);
		if (ret < 0)
			return ret;

		i2c->timing_reg = (sample_cnt << 8) | step_cnt;

		/* Disable the high speed transaction */
		i2c->high_speed_reg = I2C_TIME_CLR_VALUE;
	}

	return 0;
}

static inline u32 mtk_i2c_set_4g_mode(dma_addr_t addr)
{
	return (addr & BIT_ULL(32)) ? I2C_DMA_4G_MODE : I2C_DMA_CLR_FLAG;
}

static int mtk_i2c_do_transfer(struct mtk_i2c *i2c, struct i2c_msg *msgs,
			       int num, int left_num)
{
	u16 addr_reg;
	u16 start_reg;
	u16 control_reg;
	u16 restart_flag = 0;
	u32 reg_4g_mode;
	u8 *dma_rd_buf = NULL;
	u8 *dma_wr_buf = NULL;
	dma_addr_t rpaddr = 0;
	dma_addr_t wpaddr = 0;
	int ret;

	i2c->irq_stat = 0;

	if (i2c->auto_restart)
		restart_flag = I2C_RS_TRANSFER;

	reinit_completion(&i2c->msg_complete);

	control_reg = readw(i2c->base + OFFSET_CONTROL) &
			~(I2C_CONTROL_DIR_CHANGE | I2C_CONTROL_RS);
	if ((i2c->speed_hz > 400000) || (left_num >= 1))
		control_reg |= I2C_CONTROL_RS;

	if (i2c->op == I2C_MASTER_WRRD)
		control_reg |= I2C_CONTROL_DIR_CHANGE | I2C_CONTROL_RS;

	writew(control_reg, i2c->base + OFFSET_CONTROL);

	/* set start condition */
	if (i2c->speed_hz <= 100000)
		writew(I2C_ST_START_CON, i2c->base + OFFSET_EXT_CONF);
	else
		writew(I2C_FS_START_CON, i2c->base + OFFSET_EXT_CONF);

	addr_reg = i2c_8bit_addr_from_msg(msgs);
	writew(addr_reg, i2c->base + OFFSET_SLAVE_ADDR);

	/* Clear interrupt status */
	writew(restart_flag | I2C_HS_NACKERR | I2C_ACKERR |
	       I2C_TRANSAC_COMP, i2c->base + OFFSET_INTR_STAT);
	writew(I2C_FIFO_ADDR_CLR, i2c->base + OFFSET_FIFO_ADDR_CLR);

	/* Enable interrupt */
	writew(restart_flag | I2C_HS_NACKERR | I2C_ACKERR |
	       I2C_TRANSAC_COMP, i2c->base + OFFSET_INTR_MASK);

	/* Set transfer and transaction len */
	if (i2c->op == I2C_MASTER_WRRD) {
		if (i2c->dev_comp->aux_len_reg) {
			writew(msgs->len, i2c->base + OFFSET_TRANSFER_LEN);
			writew((msgs + 1)->len, i2c->base +
			       OFFSET_TRANSFER_LEN_AUX);
		} else {
			writew(msgs->len | ((msgs + 1)->len) << 8,
			       i2c->base + OFFSET_TRANSFER_LEN);
		}
		writew(I2C_WRRD_TRANAC_VALUE, i2c->base + OFFSET_TRANSAC_LEN);
	} else {
		writew(msgs->len, i2c->base + OFFSET_TRANSFER_LEN);
		writew(num, i2c->base + OFFSET_TRANSAC_LEN);
	}

	/* Prepare buffer data to start transfer */
	if (i2c->op == I2C_MASTER_RD) {
		writel(I2C_DMA_INT_FLAG_NONE, i2c->pdmabase + OFFSET_INT_FLAG);
		writel(I2C_DMA_CON_RX, i2c->pdmabase + OFFSET_CON);

		dma_rd_buf = i2c_get_dma_safe_msg_buf(msgs, 0);
		if (!dma_rd_buf)
			return -ENOMEM;

		rpaddr = dma_map_single(i2c->dev, dma_rd_buf,
					msgs->len, DMA_FROM_DEVICE);
		if (dma_mapping_error(i2c->dev, rpaddr)) {
			i2c_put_dma_safe_msg_buf(dma_rd_buf, msgs, false);

			return -ENOMEM;
		}

		if (i2c->dev_comp->support_33bits) {
			reg_4g_mode = mtk_i2c_set_4g_mode(rpaddr);
			writel(reg_4g_mode, i2c->pdmabase + OFFSET_RX_4G_MODE);
		}

		writel((u32)rpaddr, i2c->pdmabase + OFFSET_RX_MEM_ADDR);
		writel(msgs->len, i2c->pdmabase + OFFSET_RX_LEN);
	} else if (i2c->op == I2C_MASTER_WR) {
		writel(I2C_DMA_INT_FLAG_NONE, i2c->pdmabase + OFFSET_INT_FLAG);
		writel(I2C_DMA_CON_TX, i2c->pdmabase + OFFSET_CON);

		dma_wr_buf = i2c_get_dma_safe_msg_buf(msgs, 0);
		if (!dma_wr_buf)
			return -ENOMEM;

		wpaddr = dma_map_single(i2c->dev, dma_wr_buf,
					msgs->len, DMA_TO_DEVICE);
		if (dma_mapping_error(i2c->dev, wpaddr)) {
			i2c_put_dma_safe_msg_buf(dma_wr_buf, msgs, false);

			return -ENOMEM;
		}

		if (i2c->dev_comp->support_33bits) {
			reg_4g_mode = mtk_i2c_set_4g_mode(wpaddr);
			writel(reg_4g_mode, i2c->pdmabase + OFFSET_TX_4G_MODE);
		}

		writel((u32)wpaddr, i2c->pdmabase + OFFSET_TX_MEM_ADDR);
		writel(msgs->len, i2c->pdmabase + OFFSET_TX_LEN);
	} else {
		writel(I2C_DMA_CLR_FLAG, i2c->pdmabase + OFFSET_INT_FLAG);
		writel(I2C_DMA_CLR_FLAG, i2c->pdmabase + OFFSET_CON);

		dma_wr_buf = i2c_get_dma_safe_msg_buf(msgs, 0);
		if (!dma_wr_buf)
			return -ENOMEM;

		wpaddr = dma_map_single(i2c->dev, dma_wr_buf,
					msgs->len, DMA_TO_DEVICE);
		if (dma_mapping_error(i2c->dev, wpaddr)) {
			i2c_put_dma_safe_msg_buf(dma_wr_buf, msgs, false);

			return -ENOMEM;
		}

		dma_rd_buf = i2c_get_dma_safe_msg_buf((msgs + 1), 0);
		if (!dma_rd_buf) {
			dma_unmap_single(i2c->dev, wpaddr,
					 msgs->len, DMA_TO_DEVICE);

			i2c_put_dma_safe_msg_buf(dma_wr_buf, msgs, false);

			return -ENOMEM;
		}

		rpaddr = dma_map_single(i2c->dev, dma_rd_buf,
					(msgs + 1)->len,
					DMA_FROM_DEVICE);
		if (dma_mapping_error(i2c->dev, rpaddr)) {
			dma_unmap_single(i2c->dev, wpaddr,
					 msgs->len, DMA_TO_DEVICE);

			i2c_put_dma_safe_msg_buf(dma_wr_buf, msgs, false);
			i2c_put_dma_safe_msg_buf(dma_rd_buf, (msgs + 1), false);

			return -ENOMEM;
		}

		if (i2c->dev_comp->support_33bits) {
			reg_4g_mode = mtk_i2c_set_4g_mode(wpaddr);
			writel(reg_4g_mode, i2c->pdmabase + OFFSET_TX_4G_MODE);

			reg_4g_mode = mtk_i2c_set_4g_mode(rpaddr);
			writel(reg_4g_mode, i2c->pdmabase + OFFSET_RX_4G_MODE);
		}

		writel((u32)wpaddr, i2c->pdmabase + OFFSET_TX_MEM_ADDR);
		writel((u32)rpaddr, i2c->pdmabase + OFFSET_RX_MEM_ADDR);
		writel(msgs->len, i2c->pdmabase + OFFSET_TX_LEN);
		writel((msgs + 1)->len, i2c->pdmabase + OFFSET_RX_LEN);
	}

	writel(I2C_DMA_START_EN, i2c->pdmabase + OFFSET_EN);

	if (!i2c->auto_restart) {
		start_reg = I2C_TRANSAC_START;
	} else {
		start_reg = I2C_TRANSAC_START | I2C_RS_MUL_TRIG;
		if (left_num >= 1)
			start_reg |= I2C_RS_MUL_CNFG;
	}
	writew(start_reg, i2c->base + OFFSET_START);

	ret = wait_for_completion_timeout(&i2c->msg_complete,
					  i2c->adap.timeout);

	/* Clear interrupt mask */
	writew(~(restart_flag | I2C_HS_NACKERR | I2C_ACKERR |
	       I2C_TRANSAC_COMP), i2c->base + OFFSET_INTR_MASK);

	if (i2c->op == I2C_MASTER_WR) {
		dma_unmap_single(i2c->dev, wpaddr,
				 msgs->len, DMA_TO_DEVICE);

		i2c_put_dma_safe_msg_buf(dma_wr_buf, msgs, true);
	} else if (i2c->op == I2C_MASTER_RD) {
		dma_unmap_single(i2c->dev, rpaddr,
				 msgs->len, DMA_FROM_DEVICE);

		i2c_put_dma_safe_msg_buf(dma_rd_buf, msgs, true);
	} else {
		dma_unmap_single(i2c->dev, wpaddr, msgs->len,
				 DMA_TO_DEVICE);
		dma_unmap_single(i2c->dev, rpaddr, (msgs + 1)->len,
				 DMA_FROM_DEVICE);

		i2c_put_dma_safe_msg_buf(dma_wr_buf, msgs, true);
		i2c_put_dma_safe_msg_buf(dma_rd_buf, (msgs + 1), true);
	}

	if (ret == 0) {
		dev_dbg(i2c->dev, "addr: %x, transfer timeout\n", msgs->addr);
		mtk_i2c_init_hw(i2c);
		return -ETIMEDOUT;
	}

	completion_done(&i2c->msg_complete);

	if (i2c->irq_stat & (I2C_HS_NACKERR | I2C_ACKERR)) {
		dev_dbg(i2c->dev, "addr: %x, transfer ACK error\n", msgs->addr);
		mtk_i2c_init_hw(i2c);
		return -ENXIO;
	}

	return 0;
}

static int mtk_i2c_transfer(struct i2c_adapter *adap,
			    struct i2c_msg msgs[], int num)
{
	int ret;
	int left_num = num;
	struct mtk_i2c *i2c = i2c_get_adapdata(adap);

	ret = mtk_i2c_clock_enable(i2c);
	if (ret)
		return ret;

	i2c->auto_restart = i2c->dev_comp->auto_restart;

	/* checking if we can skip restart and optimize using WRRD mode */
	if (i2c->auto_restart && num == 2) {
		if (!(msgs[0].flags & I2C_M_RD) && (msgs[1].flags & I2C_M_RD) &&
		    msgs[0].addr == msgs[1].addr) {
			i2c->auto_restart = 0;
		}
	}

	if (i2c->auto_restart && num >= 2 && i2c->speed_hz > MAX_FS_MODE_SPEED)
		/* ignore the first restart irq after the master code,
		 * otherwise the first transfer will be discarded.
		 */
		i2c->ignore_restart_irq = true;
	else
		i2c->ignore_restart_irq = false;

	while (left_num--) {
		if (!msgs->buf) {
			dev_dbg(i2c->dev, "data buffer is NULL.\n");
			ret = -EINVAL;
			goto err_exit;
		}

		if (msgs->flags & I2C_M_RD)
			i2c->op = I2C_MASTER_RD;
		else
			i2c->op = I2C_MASTER_WR;

		if (!i2c->auto_restart) {
			if (num > 1) {
				/* combined two messages into one transaction */
				i2c->op = I2C_MASTER_WRRD;
				left_num--;
			}
		}

		/* always use DMA mode. */
		ret = mtk_i2c_do_transfer(i2c, msgs, num, left_num);
		if (ret < 0)
			goto err_exit;

		msgs++;
	}
	/* the return value is number of executed messages */
	ret = num;

err_exit:
	mtk_i2c_clock_disable(i2c);
	return ret;
}

static irqreturn_t mtk_i2c_irq(int irqno, void *dev_id)
{
	struct mtk_i2c *i2c = dev_id;
	u16 restart_flag = 0;
	u16 intr_stat;

	if (i2c->auto_restart)
		restart_flag = I2C_RS_TRANSFER;

	intr_stat = readw(i2c->base + OFFSET_INTR_STAT);
	writew(intr_stat, i2c->base + OFFSET_INTR_STAT);

	/*
	 * when occurs ack error, i2c controller generate two interrupts
	 * first is the ack error interrupt, then the complete interrupt
	 * i2c->irq_stat need keep the two interrupt value.
	 */
	i2c->irq_stat |= intr_stat;

	if (i2c->ignore_restart_irq && (i2c->irq_stat & restart_flag)) {
		i2c->ignore_restart_irq = false;
		i2c->irq_stat = 0;
		writew(I2C_RS_MUL_CNFG | I2C_RS_MUL_TRIG | I2C_TRANSAC_START,
		       i2c->base + OFFSET_START);
	} else {
		if (i2c->irq_stat & (I2C_TRANSAC_COMP | restart_flag))
			complete(&i2c->msg_complete);
	}

	return IRQ_HANDLED;
}

static u32 mtk_i2c_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm mtk_i2c_algorithm = {
	.master_xfer = mtk_i2c_transfer,
	.functionality = mtk_i2c_functionality,
};

static int mtk_i2c_parse_dt(struct device_node *np, struct mtk_i2c *i2c)
{
	int ret;

	ret = of_property_read_u32(np, "clock-frequency", &i2c->speed_hz);
	if (ret < 0)
		i2c->speed_hz = I2C_DEFAULT_SPEED;

	ret = of_property_read_u32(np, "clock-div", &i2c->clk_src_div);
	if (ret < 0)
		return ret;

	if (i2c->clk_src_div == 0)
		return -EINVAL;

	i2c->have_pmic = of_property_read_bool(np, "mediatek,have-pmic");
	i2c->use_push_pull =
		of_property_read_bool(np, "mediatek,use-push-pull");

	return 0;
}

static int mtk_i2c_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct mtk_i2c *i2c;
	struct clk *clk;
	struct resource *res;
	int irq;

	i2c = devm_kzalloc(&pdev->dev, sizeof(*i2c), GFP_KERNEL);
	if (!i2c)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	i2c->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(i2c->base))
		return PTR_ERR(i2c->base);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	i2c->pdmabase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(i2c->pdmabase))
		return PTR_ERR(i2c->pdmabase);

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0)
		return irq;

	init_completion(&i2c->msg_complete);

	i2c->dev_comp = of_device_get_match_data(&pdev->dev);
	i2c->adap.dev.of_node = pdev->dev.of_node;
	i2c->dev = &pdev->dev;
	i2c->adap.dev.parent = &pdev->dev;
	i2c->adap.owner = THIS_MODULE;
	i2c->adap.algo = &mtk_i2c_algorithm;
	i2c->adap.quirks = i2c->dev_comp->quirks;
	i2c->adap.timeout = 2 * HZ;
	i2c->adap.retries = 1;

	ret = mtk_i2c_parse_dt(pdev->dev.of_node, i2c);
	if (ret)
		return -EINVAL;

	if (i2c->dev_comp->timing_adjust)
		i2c->clk_src_div *= I2C_DEFAULT_CLK_DIV;

	if (i2c->have_pmic && !i2c->dev_comp->pmic_i2c)
		return -EINVAL;

	i2c->clk_main = devm_clk_get(&pdev->dev, "main");
	if (IS_ERR(i2c->clk_main)) {
		dev_err(&pdev->dev, "cannot get main clock\n");
		return PTR_ERR(i2c->clk_main);
	}

	i2c->clk_dma = devm_clk_get(&pdev->dev, "dma");
	if (IS_ERR(i2c->clk_dma)) {
		dev_err(&pdev->dev, "cannot get dma clock\n");
		return PTR_ERR(i2c->clk_dma);
	}

	clk = i2c->clk_main;
	if (i2c->have_pmic) {
		i2c->clk_pmic = devm_clk_get(&pdev->dev, "pmic");
		if (IS_ERR(i2c->clk_pmic)) {
			dev_err(&pdev->dev, "cannot get pmic clock\n");
			return PTR_ERR(i2c->clk_pmic);
		}
		clk = i2c->clk_pmic;
	}

	strlcpy(i2c->adap.name, I2C_DRV_NAME, sizeof(i2c->adap.name));

	ret = mtk_i2c_set_speed(i2c, clk_get_rate(clk));
	if (ret) {
		dev_err(&pdev->dev, "Failed to set the speed.\n");
		return -EINVAL;
	}

	if (i2c->dev_comp->support_33bits) {
		ret = dma_set_mask(&pdev->dev, DMA_BIT_MASK(33));
		if (ret) {
			dev_err(&pdev->dev, "dma_set_mask return error.\n");
			return ret;
		}
	}

	ret = mtk_i2c_clock_enable(i2c);
	if (ret) {
		dev_err(&pdev->dev, "clock enable failed!\n");
		return ret;
	}
	mtk_i2c_init_hw(i2c);
	mtk_i2c_clock_disable(i2c);

	ret = devm_request_irq(&pdev->dev, irq, mtk_i2c_irq,
			       IRQF_TRIGGER_NONE, I2C_DRV_NAME, i2c);
	if (ret < 0) {
		dev_err(&pdev->dev,
			"Request I2C IRQ %d fail\n", irq);
		return ret;
	}

	i2c_set_adapdata(&i2c->adap, i2c);
	ret = i2c_add_adapter(&i2c->adap);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, i2c);

	return 0;
}

static int mtk_i2c_remove(struct platform_device *pdev)
{
	struct mtk_i2c *i2c = platform_get_drvdata(pdev);

	i2c_del_adapter(&i2c->adap);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int mtk_i2c_resume(struct device *dev)
{
	int ret;
	struct mtk_i2c *i2c = dev_get_drvdata(dev);

	ret = mtk_i2c_clock_enable(i2c);
	if (ret) {
		dev_err(dev, "clock enable failed!\n");
		return ret;
	}

	mtk_i2c_init_hw(i2c);

	mtk_i2c_clock_disable(i2c);

	return 0;
}
#endif

static const struct dev_pm_ops mtk_i2c_pm = {
	SET_SYSTEM_SLEEP_PM_OPS(NULL, mtk_i2c_resume)
};

static struct platform_driver mtk_i2c_driver = {
	.probe = mtk_i2c_probe,
	.remove = mtk_i2c_remove,
	.driver = {
		.name = I2C_DRV_NAME,
		.pm = &mtk_i2c_pm,
		.of_match_table = of_match_ptr(mtk_i2c_of_match),
	},
};

module_platform_driver(mtk_i2c_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MediaTek I2C Bus Driver");
MODULE_AUTHOR("Xudong Chen <xudong.chen@mediatek.com>");
