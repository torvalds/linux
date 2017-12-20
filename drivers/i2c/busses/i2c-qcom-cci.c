/* Copyright (c) 2012-2016, The Linux Foundation. All rights reserved.
 * Copyright (C) 2017 Linaro Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/i2c.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>

#define CCI_HW_VERSION			0x0
#define CCI_RESET_CMD			0x004
#define CCI_RESET_CMD_MASK		0x0f73f3f7
#define CCI_RESET_CMD_M0_MASK		0x000003f1
#define CCI_RESET_CMD_M1_MASK		0x0003f001
#define CCI_QUEUE_START			0x008
#define CCI_HALT_REQ			0x034
#define CCI_HALT_REQ_I2C_M0_Q0Q1	BIT(0)
#define CCI_HALT_REQ_I2C_M1_Q0Q1	BIT(1)

#define CCI_I2C_Mm_SCL_CTL(m)		(0x100 + 0x100 * (m))
#define CCI_I2C_Mm_SDA_CTL_0(m)		(0x104 + 0x100 * (m))
#define CCI_I2C_Mm_SDA_CTL_1(m)		(0x108 + 0x100 * (m))
#define CCI_I2C_Mm_SDA_CTL_2(m)		(0x10c + 0x100 * (m))
#define CCI_I2C_Mm_MISC_CTL(m)		(0x110 + 0x100 * (m))

#define CCI_I2C_Mm_READ_DATA(m)			(0x118 + 0x100 * (m))
#define CCI_I2C_Mm_READ_BUF_LEVEL(m)		(0x11c + 0x100 * (m))
#define CCI_I2C_Mm_Qn_EXEC_WORD_CNT(m, n)	(0x300 + 0x200 * (m) + 0x100 * (n))
#define CCI_I2C_Mm_Qn_CUR_WORD_CNT(m, n)	(0x304 + 0x200 * (m) + 0x100 * (n))
#define CCI_I2C_Mm_Qn_CUR_CMD(m, n)		(0x308 + 0x200 * (m) + 0x100 * (n))
#define CCI_I2C_Mm_Qn_REPORT_STATUS(m, n)	(0x30c + 0x200 * (m) + 0x100 * (n))
#define CCI_I2C_Mm_Qn_LOAD_DATA(m, n)		(0x310 + 0x200 * (m) + 0x100 * (n))

#define CCI_IRQ_GLOBAL_CLEAR_CMD	0xc00
#define CCI_IRQ_MASK_0			0xc04
#define CCI_IRQ_MASK_0_I2C_M0_RD_DONE		BIT(0)
#define CCI_IRQ_MASK_0_I2C_M0_Q0_REPORT		BIT(4)
#define CCI_IRQ_MASK_0_I2C_M0_Q1_REPORT		BIT(8)
#define CCI_IRQ_MASK_0_I2C_M1_RD_DONE		BIT(12)
#define CCI_IRQ_MASK_0_I2C_M1_Q0_REPORT		BIT(16)
#define CCI_IRQ_MASK_0_I2C_M1_Q1_REPORT		BIT(20)
#define CCI_IRQ_MASK_0_RST_DONE_ACK		BIT(24)
#define CCI_IRQ_MASK_0_I2C_M0_Q0Q1_HALT_ACK	BIT(25)
#define CCI_IRQ_MASK_0_I2C_M1_Q0Q1_HALT_ACK	BIT(26)
#define CCI_IRQ_MASK_0_I2C_M0_ERROR		0x18000ee6
#define CCI_IRQ_MASK_0_I2C_M1_ERROR		0x60ee6000
#define CCI_IRQ_CLEAR_0			0xc08
#define CCI_IRQ_STATUS_0		0xc0c
#define CCI_IRQ_STATUS_0_I2C_M0_RD_DONE		BIT(0)
#define CCI_IRQ_STATUS_0_I2C_M0_Q0_REPORT	BIT(4)
#define CCI_IRQ_STATUS_0_I2C_M0_Q1_REPORT	BIT(8)
#define CCI_IRQ_STATUS_0_I2C_M1_RD_DONE		BIT(12)
#define CCI_IRQ_STATUS_0_I2C_M1_Q0_REPORT	BIT(16)
#define CCI_IRQ_STATUS_0_I2C_M1_Q1_REPORT	BIT(20)
#define CCI_IRQ_STATUS_0_RST_DONE_ACK		BIT(24)
#define CCI_IRQ_STATUS_0_I2C_M0_Q0Q1_HALT_ACK	BIT(25)
#define CCI_IRQ_STATUS_0_I2C_M1_Q0Q1_HALT_ACK	BIT(26)
#define CCI_IRQ_STATUS_0_I2C_M0_ERROR		0x18000ee6
#define CCI_IRQ_STATUS_0_I2C_M1_ERROR		0x60ee6000

#define CCI_TIMEOUT_MS 100
#define NUM_MASTERS 1
#define NUM_QUEUES 2

/* Max number of resources + 1 for a NULL terminator */
#define CCI_RES_MAX 6

enum cci_i2c_cmd {
	CCI_I2C_SET_PARAM = 1,
	CCI_I2C_WAIT,
	CCI_I2C_WAIT_SYNC,
	CCI_I2C_WAIT_GPIO_EVENT,
	CCI_I2C_TRIG_I2C_EVENT,
	CCI_I2C_LOCK,
	CCI_I2C_UNLOCK,
	CCI_I2C_REPORT,
	CCI_I2C_WRITE,
	CCI_I2C_READ,
	CCI_I2C_WRITE_DISABLE_P,
	CCI_I2C_READ_DISABLE_P,
};

enum {
	I2C_MODE_STANDARD,
	I2C_MODE_FAST,
	I2C_MODE_FAST_PLUS,
};

enum cci_i2c_queue_t {
	QUEUE_0,
	QUEUE_1
};

struct cci_res {
	char *clock[CCI_RES_MAX];
	u32 clock_rate[CCI_RES_MAX];
};

struct hw_params {
	u16 thigh;
	u16 tlow;
	u16 tsu_sto;
	u16 tsu_sta;
	u16 thd_dat;
	u16 thd_sta;
	u16 tbuf;
	u8 scl_stretch_en;
	u16 trdhld;
	u16 tsp;
};

struct cci_clock {
	struct clk *clk;
	const char *name;
	u32 freq;
};

struct cci_master {
	int status;
	bool complete_pending;
	struct completion irq_complete;
};

struct cci {
	struct device *dev;
	struct i2c_adapter adap;
	void __iomem *base;
	u32 irq;
	struct clk_bulk_data *clock;
	u32 *clock_freq;
	int nclocks;
	u16 queue_size[NUM_QUEUES];
	struct cci_master master[NUM_MASTERS];
};

static const struct cci_res res_v1_0_8 = {
	.clock = { "camss_top_ahb",
		   "cci_ahb",
		   "camss_ahb",
		   "cci" },
	.clock_rate = { 0,
			80000000,
			0,
			19200000 }
};

static const struct cci_res res_v1_4_0 = {
	.clock = { "mmss_mmagic_ahb",
		   "camss_top_ahb",
		   "cci_ahb",
		   "camss_ahb",
		   "cci" },
	.clock_rate = { 0,
			0,
			0,
			0,
			37500000 }
};

static const struct hw_params hw_params_v1_0_8[3] = {
	{	/* I2C_MODE_STANDARD */
		.thigh = 78,
		.tlow = 114,
		.tsu_sto = 28,
		.tsu_sta = 28,
		.thd_dat = 10,
		.thd_sta = 77,
		.tbuf = 118,
		.scl_stretch_en = 0,
		.trdhld = 6,
		.tsp = 1
	},
	{	/* I2C_MODE_FAST */
		.thigh = 20,
		.tlow = 28,
		.tsu_sto = 21,
		.tsu_sta = 21,
		.thd_dat = 13,
		.thd_sta = 18,
		.tbuf = 32,
		.scl_stretch_en = 0,
		.trdhld = 6,
		.tsp = 3
	}
};

static const struct hw_params hw_params_v1_4_0[3] = {
	{	/* I2C_MODE_STANDARD */
		.thigh = 201,
		.tlow = 174,
		.tsu_sto = 204,
		.tsu_sta = 231,
		.thd_dat = 22,
		.thd_sta = 162,
		.tbuf = 227,
		.scl_stretch_en = 0,
		.trdhld = 6,
		.tsp = 3
	},
	{	/* I2C_MODE_FAST */
		.thigh = 38,
		.tlow = 56,
		.tsu_sto = 40,
		.tsu_sta = 40,
		.thd_dat = 22,
		.thd_sta = 35,
		.tbuf = 62,
		.scl_stretch_en = 0,
		.trdhld = 6,
		.tsp = 3
	},
	{	/* I2C_MODE_FAST_PLUS */
		.thigh = 16,
		.tlow = 22,
		.tsu_sto = 17,
		.tsu_sta = 18,
		.thd_dat = 16,
		.thd_sta = 15,
		.tbuf = 24,
		.scl_stretch_en = 0,
		.trdhld = 3,
		.tsp = 3
	}
};

static const u16 queue_0_size_v1_0_8 = 64;
static const u16 queue_1_size_v1_0_8 = 16;

static const u16 queue_0_size_v1_4_0 = 64;
static const u16 queue_1_size_v1_4_0 = 16;

/**
 * cci_clock_set_rate() - Set clock frequency rates
 * @nclocks: Number of clocks
 * @clock: Clock array
 * @clock_freq: Clock frequency rate array
 * @dev: Device
 *
 * Return 0 on success or a negative error code otherwise
 */
int cci_clock_set_rate(int nclocks, struct clk_bulk_data *clock,
		       u32 *clock_freq, struct device *dev)
{
	int i;

	for (i = 0; i < nclocks; i++)
		if (clock_freq[i]) {
			long rate;
			int ret;

			rate = clk_round_rate(clock[i].clk, clock_freq[i]);
			if (rate < 0) {
				dev_err(dev, "clk round rate failed: %ld\n",
					rate);
				return rate;
			}

			ret = clk_set_rate(clock[i].clk, clock_freq[i]);
			if (ret < 0) {
				dev_err(dev, "clk set rate failed: %d\n", ret);
				return ret;
			}
		}

	return 0;
}

static irqreturn_t cci_isr(int irq, void *dev)
{
	struct cci *cci = dev;
	u32 reset = 0;
	u32 val;

	val = readl(cci->base + CCI_IRQ_STATUS_0);
	writel(val, cci->base + CCI_IRQ_CLEAR_0);
	writel(0x1, cci->base + CCI_IRQ_GLOBAL_CLEAR_CMD);

	if (val & CCI_IRQ_STATUS_0_RST_DONE_ACK) {
		if (cci->master[0].complete_pending) {
			cci->master[0].complete_pending = false;
			complete(&cci->master[0].irq_complete);
		}

		if (cci->master[1].complete_pending) {
			cci->master[1].complete_pending = false;
			complete(&cci->master[1].irq_complete);
		}
	}

	if (val & CCI_IRQ_STATUS_0_I2C_M0_RD_DONE ||
			val & CCI_IRQ_STATUS_0_I2C_M0_Q0_REPORT ||
			val & CCI_IRQ_STATUS_0_I2C_M0_Q1_REPORT) {
		cci->master[0].status = 0;
		complete(&cci->master[0].irq_complete);
	}

	if (val & CCI_IRQ_STATUS_0_I2C_M1_RD_DONE ||
			val & CCI_IRQ_STATUS_0_I2C_M1_Q0_REPORT ||
			val & CCI_IRQ_STATUS_0_I2C_M1_Q1_REPORT) {
		cci->master[1].status = 0;
		complete(&cci->master[1].irq_complete);
	}

	if (unlikely(val & CCI_IRQ_STATUS_0_I2C_M0_Q0Q1_HALT_ACK)) {
		cci->master[0].complete_pending = true;
		reset = CCI_RESET_CMD_M0_MASK;
	}

	if (unlikely(val & CCI_IRQ_STATUS_0_I2C_M1_Q0Q1_HALT_ACK)) {
		cci->master[1].complete_pending = true;
		reset = CCI_RESET_CMD_M1_MASK;
	}

	if (unlikely(reset))
		writel(reset, cci->base + CCI_RESET_CMD);

	if (unlikely(val & CCI_IRQ_STATUS_0_I2C_M0_ERROR)) {
		dev_err_ratelimited(cci->dev, "Master 0 error 0x%08x\n", val);
		cci->master[0].status = -EIO;
		writel(CCI_HALT_REQ_I2C_M0_Q0Q1, cci->base + CCI_HALT_REQ);
	}

	if (unlikely(val & CCI_IRQ_STATUS_0_I2C_M1_ERROR)) {
		dev_err_ratelimited(cci->dev, "Master 1 error 0x%08x\n", val);
		cci->master[1].status = -EIO;
		writel(CCI_HALT_REQ_I2C_M1_Q0Q1, cci->base + CCI_HALT_REQ);
	}

	return IRQ_HANDLED;
}

static void cci_halt(struct cci *cci)
{
	unsigned long time;
	u32 val = CCI_HALT_REQ_I2C_M0_Q0Q1 | CCI_HALT_REQ_I2C_M1_Q0Q1;

	cci->master[0].complete_pending = true;
	writel(val, cci->base + CCI_HALT_REQ);
	time = wait_for_completion_timeout(
				&cci->master[0].irq_complete,
				msecs_to_jiffies(CCI_TIMEOUT_MS));
	if (!time)
		dev_err(cci->dev, "CCI halt timeout\n");
}

static int cci_reset(struct cci *cci)
{
	unsigned long time;

	cci->master[0].complete_pending = true;
	writel(CCI_RESET_CMD_MASK, cci->base + CCI_RESET_CMD);
	time = wait_for_completion_timeout(
				&cci->master[0].irq_complete,
				msecs_to_jiffies(CCI_TIMEOUT_MS));
	if (!time) {
		dev_err(cci->dev, "CCI reset timeout\n");
		return -ETIMEDOUT;
	}

	return 0;
}

static int cci_init(struct cci *cci, const struct hw_params *hw)
{
	u32 val = CCI_IRQ_MASK_0_I2C_M0_RD_DONE |
			CCI_IRQ_MASK_0_I2C_M0_Q0_REPORT |
			CCI_IRQ_MASK_0_I2C_M0_Q1_REPORT |
			CCI_IRQ_MASK_0_I2C_M1_RD_DONE |
			CCI_IRQ_MASK_0_I2C_M1_Q0_REPORT |
			CCI_IRQ_MASK_0_I2C_M1_Q1_REPORT |
			CCI_IRQ_MASK_0_RST_DONE_ACK |
			CCI_IRQ_MASK_0_I2C_M0_Q0Q1_HALT_ACK |
			CCI_IRQ_MASK_0_I2C_M1_Q0Q1_HALT_ACK |
			CCI_IRQ_MASK_0_I2C_M0_ERROR |
			CCI_IRQ_MASK_0_I2C_M1_ERROR;
	int i;

	writel(val, cci->base + CCI_IRQ_MASK_0);

	for (i = 0; i < NUM_MASTERS; i++) {
		val = hw->thigh << 16 | hw->tlow;
		writel(val, cci->base + CCI_I2C_Mm_SCL_CTL(i));

		val = hw->tsu_sto << 16 | hw->tsu_sta;
		writel(val, cci->base + CCI_I2C_Mm_SDA_CTL_0(i));

		val = hw->thd_dat << 16 | hw->thd_sta;
		writel(val, cci->base + CCI_I2C_Mm_SDA_CTL_1(i));

		val = hw->tbuf;
		writel(val, cci->base + CCI_I2C_Mm_SDA_CTL_2(i));

		val = hw->scl_stretch_en << 8 | hw->trdhld << 4 | hw->tsp;
		writel(val, cci->base + CCI_I2C_Mm_MISC_CTL(i));
	}

	return 0;
}

static int cci_run_queue(struct cci *cci, u8 master, u8 queue)
{
	unsigned long time;
	u32 val;
	int ret;

	val = readl(cci->base + CCI_I2C_Mm_Qn_CUR_WORD_CNT(master, queue));
	writel(val, cci->base + CCI_I2C_Mm_Qn_EXEC_WORD_CNT(master, queue));

	val = BIT(master * 2 + queue);
	writel(val, cci->base + CCI_QUEUE_START);

	time = wait_for_completion_timeout(&cci->master[master].irq_complete,
					   msecs_to_jiffies(CCI_TIMEOUT_MS));
	if (!time) {
		dev_err(cci->dev, "master %d queue %d timeout\n",
			master, queue);

		cci_halt(cci);

		return -ETIMEDOUT;
	}

	ret = cci->master[master].status;
	if (ret < 0)
		dev_err(cci->dev, "master %d queue %d error %d\n",
			master, queue, ret);

	return ret;
}

static int cci_validate_queue(struct cci *cci, u8 master, u8 queue)
{
	int ret = 0;
	u32 val;

	val = readl(cci->base + CCI_I2C_Mm_Qn_CUR_WORD_CNT(master, queue));

	if (val == cci->queue_size[queue])
		return -EINVAL;

	if (val) {
		val = CCI_I2C_REPORT | BIT(8);
		writel(val, cci->base + CCI_I2C_Mm_Qn_LOAD_DATA(master, queue));

		ret = cci_run_queue(cci, master, queue);
	}

	return ret;
}

static int cci_i2c_read(struct cci *cci, u16 addr, u8 *buf, u16 len)
{
	u8 master = 0;
	u8 queue = QUEUE_1;
	u32 val;
	u32 words_read, words_exp;
	int i, index;
	bool first;
	int ret;

	/*
	 * Call validate queue to make sure queue is empty before starting.
	 * This is to avoid overflow / underflow of queue.
	 */
	ret = cci_validate_queue(cci, master, queue);
	if (ret < 0)
		return ret;

	val = CCI_I2C_SET_PARAM | (addr & 0x7f) << 4;
	writel(val, cci->base + CCI_I2C_Mm_Qn_LOAD_DATA(master, queue));

	val = CCI_I2C_READ | len << 4;
	writel(val, cci->base + CCI_I2C_Mm_Qn_LOAD_DATA(master, queue));

	ret = cci_run_queue(cci, master, queue);
	if (ret < 0)
		return ret;

	words_read = readl(cci->base + CCI_I2C_Mm_READ_BUF_LEVEL(master));
	words_exp = len / 4 + 1;
	if (words_read != words_exp) {
		dev_err(cci->dev, "words read = %d, words expected = %d\n",
			words_read, words_exp);
		return -EIO;
	}

	index = 0;
	first = true;
	do {
		val = readl(cci->base + CCI_I2C_Mm_READ_DATA(master));

		for (i = 0; i < 4 && index < len; i++) {
			if (first) {
				first = false;
				continue;
			}
			buf[index++] = (val >> (i * 8)) & 0xff;
		}
	} while (--words_read);

	return 0;
}

static int cci_i2c_write(struct cci *cci, u16 addr, u8 *buf, u16 len)
{
	u8 master = 0;
	u8 queue = QUEUE_0;
	u8 load[12] = { 0 };
	int i, j;
	u32 val;
	int ret;

	/*
	 * Call validate queue to make sure queue is empty before starting.
	 * This is to avoid overflow / underflow of queue.
	 */
	ret = cci_validate_queue(cci, master, queue);
	if (ret < 0)
		return ret;

	val = CCI_I2C_SET_PARAM | (addr & 0x7f) << 4;
	writel(val, cci->base + CCI_I2C_Mm_Qn_LOAD_DATA(master, queue));

	i = 0;
	load[i++] = CCI_I2C_WRITE | len << 4;

	for (j = 0; j < len; j++)
		load[i++] = buf[j];

	for (j = 0; j < i; j += 4) {
		val = load[j];
		val |= load[j + 1] << 8;
		val |= load[j + 2] << 16;
		val |= load[j + 3] << 24;
		writel(val, cci->base + CCI_I2C_Mm_Qn_LOAD_DATA(master, queue));
	}

	val = CCI_I2C_REPORT | BIT(8);
	writel(val, cci->base + CCI_I2C_Mm_Qn_LOAD_DATA(master, queue));

	return cci_run_queue(cci, master, queue);
}

static int cci_xfer(struct i2c_adapter *adap, struct i2c_msg msgs[], int num)
{
	struct cci *cci = i2c_get_adapdata(adap);
	int i;
	int ret = 0;

	for (i = 0; i < num; i++) {
		if (msgs[i].flags & I2C_M_RD)
			ret = cci_i2c_read(cci, msgs[i].addr, msgs[i].buf,
					   msgs[i].len);
		else
			ret = cci_i2c_write(cci, msgs[i].addr, msgs[i].buf,
					    msgs[i].len);

		if (ret < 0) {
			dev_err(cci->dev, "cci i2c xfer error %d", ret);
			break;
		}
	}

	if (!ret)
		ret = num;

	return ret;
}

static u32 cci_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C;
}

static const struct i2c_algorithm cci_algo = {
	.master_xfer	= cci_xfer,
	.functionality	= cci_func,
};

static const struct i2c_adapter_quirks cci_quirks_v1_0_8 = {
	.max_write_len = 10,
	.max_read_len = 12,
};

static const struct i2c_adapter_quirks cci_quirks_v1_4_0 = {
	.max_write_len = 11,
	.max_read_len = 12,
};

/**
 * cci_probe() - Probe CCI platform device
 * @pdev: Pointer to CCI platform device
 *
 * Return 0 on success or a negative error code on failure
 */
static int cci_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	const struct cci_res *res;
	const struct hw_params *hw;
	struct cci *cci;
	struct resource *r;
	int ret = 0;
	u8 mode;
	u32 val;
	int i;

	cci = devm_kzalloc(dev, sizeof(*cci), GFP_KERNEL);
	if (!cci)
		return -ENOMEM;

	cci->dev = dev;
	platform_set_drvdata(pdev, cci);

	if (of_device_is_compatible(dev->of_node, "qcom,cci-v1.0.8")) {
		res = &res_v1_0_8;
		hw = hw_params_v1_0_8;
		cci->queue_size[0] = queue_0_size_v1_0_8;
		cci->queue_size[1] = queue_1_size_v1_0_8;
		cci->adap.quirks = &cci_quirks_v1_0_8;
	} else if (of_device_is_compatible(dev->of_node, "qcom,cci-v1.4.0")) {
		res = &res_v1_4_0;
		hw = hw_params_v1_4_0;
		cci->queue_size[0] = queue_0_size_v1_4_0;
		cci->queue_size[1] = queue_1_size_v1_4_0;
		cci->adap.quirks = &cci_quirks_v1_4_0;
	} else {
		return -EINVAL;
	}

	cci->adap.algo = &cci_algo;
	cci->adap.dev.parent = cci->dev;
	cci->adap.dev.of_node = dev->of_node;
	i2c_set_adapdata(&cci->adap, cci);

	strlcpy(cci->adap.name, "Qualcomm Camera Control Interface",
		sizeof(cci->adap.name));

	mode = I2C_MODE_STANDARD;
	ret = of_property_read_u32(pdev->dev.of_node, "clock-frequency", &val);
	if (!ret) {
		if (val == 400000)
			mode = I2C_MODE_FAST;
		else if (val == 1000000)
			mode = I2C_MODE_FAST_PLUS;
	}

	/* Memory */

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	cci->base = devm_ioremap_resource(dev, r);
	if (IS_ERR(cci->base)) {
		dev_err(dev, "could not map memory\n");
		return PTR_ERR(cci->base);
	}

	/* Interrupt */

	cci->irq = platform_get_irq(pdev, 0);
	if (cci->irq < 0) {
		dev_err(dev, "missing IRQ\n");
		return cci->irq;
	}

	ret = devm_request_irq(dev, cci->irq, cci_isr,
			       IRQF_TRIGGER_RISING, dev_name(dev), cci);
	if (ret < 0) {
		dev_err(dev, "request_irq failed, ret: %d\n", ret);
		return ret;
	}

	disable_irq(cci->irq);

	/* Clocks */

	cci->nclocks = 0;
	while (res->clock[cci->nclocks])
		cci->nclocks++;

	cci->clock = devm_kzalloc(dev, cci->nclocks *
				  sizeof(*cci->clock), GFP_KERNEL);
	if (!cci->clock)
		return -ENOMEM;

	cci->clock_freq = devm_kzalloc(dev, cci->nclocks *
				       sizeof(*cci->clock_freq), GFP_KERNEL);
	if (!cci->clock_freq)
		return -ENOMEM;

	for (i = 0; i < cci->nclocks; i++) {
		struct clk_bulk_data *clock = &cci->clock[i];

		clock->clk = devm_clk_get(dev, res->clock[i]);
		if (IS_ERR(clock->clk))
			return PTR_ERR(clock->clk);

		clock->id = res->clock[i];
		cci->clock_freq[i] = res->clock_rate[i];
	}

	ret = cci_clock_set_rate(cci->nclocks, cci->clock,
				 cci->clock_freq, dev);
	if (ret < 0)
		return ret;

	ret = clk_bulk_prepare_enable(cci->nclocks, cci->clock);
	if (ret < 0)
		return ret;

	val = readl_relaxed(cci->base + CCI_HW_VERSION);
	dev_dbg(dev, "%s: CCI HW version = 0x%08x", __func__, val);

	init_completion(&cci->master[0].irq_complete);
	init_completion(&cci->master[1].irq_complete);

	enable_irq(cci->irq);

	ret = cci_reset(cci);
	if (ret < 0)
		goto error;

	ret = cci_init(cci, &hw[mode]);
	if (ret < 0)
		goto error;

	ret = i2c_add_adapter(&cci->adap);
	if (ret < 0)
		goto error;

	return 0;

error:
	clk_bulk_disable_unprepare(cci->nclocks, cci->clock);

	return ret;
}

/**
 * cci_remove() - Remove CCI platform device
 * @pdev: Pointer to CCI platform device
 *
 * Always returns 0.
 */
static int cci_remove(struct platform_device *pdev)
{
	struct cci *cci = platform_get_drvdata(pdev);

	disable_irq(cci->irq);
	clk_bulk_disable_unprepare(cci->nclocks, cci->clock);

	i2c_del_adapter(&cci->adap);

	return 0;
}

static const struct of_device_id cci_dt_match[] = {
	{ .compatible = "qcom,cci-v1.0.8" },
	{ .compatible = "qcom,cci-v1.4.0" },
	{}
};
MODULE_DEVICE_TABLE(of, cci_dt_match);

static struct platform_driver qcom_cci_driver = {
	.probe  = cci_probe,
	.remove = cci_remove,
	.driver = {
		.name = "i2c-qcom-cci",
		.of_match_table = cci_dt_match,
	},
};

module_platform_driver(qcom_cci_driver);

MODULE_DESCRIPTION("Qualcomm Camera Control Interface driver");
MODULE_AUTHOR("Todor Tomov <todor.tomov@linaro.org>");
MODULE_LICENSE("GPL v2");
