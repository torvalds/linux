// SPDX-License-Identifier: GPL-2.0-only
/**
 * meson-ir-tx.c - Amlogic Meson IR TX driver
 *
 * Copyright (c) 2021, SberDevices. All Rights Reserved.
 *
 * Author: Viktor Prutyanov <viktor.prutyanov@phystech.edu>
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/of_irq.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <media/rc-core.h>

#define DEVICE_NAME	"Meson IR TX"
#define DRIVER_NAME	"meson-ir-tx"

#define MIRTX_DEFAULT_CARRIER		38000
#define MIRTX_DEFAULT_DUTY_CYCLE	50
#define MIRTX_FIFO_THD			32

#define IRB_MOD_1US_CLK_RATE	1000000

#define IRB_FIFO_LEN	128

#define IRB_ADDR0	0x0
#define IRB_ADDR1	0x4
#define IRB_ADDR2	0x8
#define IRB_ADDR3	0xc

#define IRB_MAX_DELAY	(1 << 10)
#define IRB_DELAY_MASK	(IRB_MAX_DELAY - 1)

/* IRCTRL_IR_BLASTER_ADDR0 */
#define IRB_MOD_CLK(x)		((x) << 12)
#define IRB_MOD_SYS_CLK		0
#define IRB_MOD_XTAL3_CLK	1
#define IRB_MOD_1US_CLK		2
#define IRB_MOD_10US_CLK	3
#define IRB_INIT_HIGH		BIT(2)
#define IRB_ENABLE		BIT(0)

/* IRCTRL_IR_BLASTER_ADDR2 */
#define IRB_MOD_COUNT(lo, hi)	((((lo) - 1) << 16) | ((hi) - 1))

/* IRCTRL_IR_BLASTER_ADDR2 */
#define IRB_WRITE_FIFO	BIT(16)
#define IRB_MOD_ENABLE	BIT(12)
#define IRB_TB_1US	(0x0 << 10)
#define IRB_TB_10US	(0x1 << 10)
#define IRB_TB_100US	(0x2 << 10)
#define IRB_TB_MOD_CLK	(0x3 << 10)

/* IRCTRL_IR_BLASTER_ADDR3 */
#define IRB_FIFO_THD_PENDING	BIT(16)
#define IRB_FIFO_IRQ_ENABLE	BIT(8)

struct meson_irtx {
	struct device *dev;
	void __iomem *reg_base;
	u32 *buf;
	unsigned int buf_len;
	unsigned int buf_head;
	unsigned int carrier;
	unsigned int duty_cycle;
	/* Locks buf */
	spinlock_t lock;
	struct completion completion;
	unsigned long clk_rate;
};

static void meson_irtx_set_mod(struct meson_irtx *ir)
{
	unsigned int cnt = DIV_ROUND_CLOSEST(ir->clk_rate, ir->carrier);
	unsigned int pulse_cnt = DIV_ROUND_CLOSEST(cnt * ir->duty_cycle, 100);
	unsigned int space_cnt = cnt - pulse_cnt;

	dev_dbg(ir->dev, "F_mod = %uHz, T_mod = %luns, duty_cycle = %u%%\n",
		ir->carrier, NSEC_PER_SEC / ir->clk_rate * cnt,
		100 * pulse_cnt / cnt);

	writel(IRB_MOD_COUNT(pulse_cnt, space_cnt),
	       ir->reg_base + IRB_ADDR1);
}

static void meson_irtx_setup(struct meson_irtx *ir, unsigned int clk_nr)
{
	/*
	 * Disable the TX, set modulator clock tick and set initialize
	 * output to be high. Set up carrier frequency and duty cycle. Then
	 * unset initialize output. Enable FIFO interrupt, set FIFO interrupt
	 * threshold. Finally, enable the transmitter back.
	 */
	writel(~IRB_ENABLE & (IRB_MOD_CLK(clk_nr) | IRB_INIT_HIGH),
	       ir->reg_base + IRB_ADDR0);
	meson_irtx_set_mod(ir);
	writel(readl(ir->reg_base + IRB_ADDR0) & ~IRB_INIT_HIGH,
	       ir->reg_base + IRB_ADDR0);
	writel(IRB_FIFO_IRQ_ENABLE | MIRTX_FIFO_THD,
	       ir->reg_base + IRB_ADDR3);
	writel(readl(ir->reg_base + IRB_ADDR0) | IRB_ENABLE,
	       ir->reg_base + IRB_ADDR0);
}

static u32 meson_irtx_prepare_pulse(struct meson_irtx *ir, unsigned int time)
{
	unsigned int delay;
	unsigned int tb = IRB_TB_MOD_CLK;
	unsigned int tb_us = DIV_ROUND_CLOSEST(USEC_PER_SEC, ir->carrier);

	delay = (DIV_ROUND_CLOSEST(time, tb_us) - 1) & IRB_DELAY_MASK;

	return ((IRB_WRITE_FIFO | IRB_MOD_ENABLE) | tb | delay);
}

static u32 meson_irtx_prepare_space(struct meson_irtx *ir, unsigned int time)
{
	unsigned int delay;
	unsigned int tb = IRB_TB_100US;
	unsigned int tb_us = 100;

	if (time <= IRB_MAX_DELAY) {
		tb = IRB_TB_1US;
		tb_us = 1;
	} else if (time <= 10 * IRB_MAX_DELAY) {
		tb = IRB_TB_10US;
		tb_us = 10;
	} else if (time <= 100 * IRB_MAX_DELAY) {
		tb = IRB_TB_100US;
		tb_us = 100;
	}

	delay = (DIV_ROUND_CLOSEST(time, tb_us) - 1) & IRB_DELAY_MASK;

	return ((IRB_WRITE_FIFO & ~IRB_MOD_ENABLE) | tb | delay);
}

static void meson_irtx_send_buffer(struct meson_irtx *ir)
{
	unsigned int nr = 0;
	unsigned int max_fifo_level = IRB_FIFO_LEN - MIRTX_FIFO_THD;

	while (ir->buf_head < ir->buf_len && nr < max_fifo_level) {
		writel(ir->buf[ir->buf_head], ir->reg_base + IRB_ADDR2);

		ir->buf_head++;
		nr++;
	}
}

static bool meson_irtx_check_buf(struct meson_irtx *ir,
				 unsigned int *buf, unsigned int len)
{
	unsigned int i;

	for (i = 0; i < len; i++) {
		unsigned int max_tb_us;
		/*
		 * Max space timebase is 100 us.
		 * Pulse timebase equals to carrier period.
		 */
		if (i % 2 == 0)
			max_tb_us = USEC_PER_SEC / ir->carrier;
		else
			max_tb_us = 100;

		if (buf[i] >= max_tb_us * IRB_MAX_DELAY)
			return false;
	}

	return true;
}

static void meson_irtx_fill_buf(struct meson_irtx *ir, u32 *dst_buf,
				unsigned int *src_buf, unsigned int len)
{
	unsigned int i;

	for (i = 0; i < len; i++) {
		if (i % 2 == 0)
			dst_buf[i] = meson_irtx_prepare_pulse(ir, src_buf[i]);
		else
			dst_buf[i] = meson_irtx_prepare_space(ir, src_buf[i]);
	}
}

static irqreturn_t meson_irtx_irqhandler(int irq, void *data)
{
	unsigned long flags;
	struct meson_irtx *ir = data;

	writel(readl(ir->reg_base + IRB_ADDR3) & ~IRB_FIFO_THD_PENDING,
	       ir->reg_base + IRB_ADDR3);

	if (completion_done(&ir->completion))
		return IRQ_HANDLED;

	spin_lock_irqsave(&ir->lock, flags);
	if (ir->buf_head < ir->buf_len)
		meson_irtx_send_buffer(ir);
	else
		complete(&ir->completion);
	spin_unlock_irqrestore(&ir->lock, flags);

	return IRQ_HANDLED;
}

static int meson_irtx_set_carrier(struct rc_dev *rc, u32 carrier)
{
	struct meson_irtx *ir = rc->priv;

	if (carrier == 0)
		return -EINVAL;

	ir->carrier = carrier;
	meson_irtx_set_mod(ir);

	return 0;
}

static int meson_irtx_set_duty_cycle(struct rc_dev *rc, u32 duty_cycle)
{
	struct meson_irtx *ir = rc->priv;

	ir->duty_cycle = duty_cycle;
	meson_irtx_set_mod(ir);

	return 0;
}

static void meson_irtx_update_buf(struct meson_irtx *ir, u32 *buf,
				  unsigned int len, unsigned int head)
{
	ir->buf = buf;
	ir->buf_len = len;
	ir->buf_head = head;
}

static int meson_irtx_transmit(struct rc_dev *rc, unsigned int *buf,
			       unsigned int len)
{
	unsigned long flags;
	struct meson_irtx *ir = rc->priv;
	u32 *tx_buf;
	int ret = len;

	if (!meson_irtx_check_buf(ir, buf, len))
		return -EINVAL;

	tx_buf = kmalloc_array(len, sizeof(u32), GFP_KERNEL);
	if (!tx_buf)
		return -ENOMEM;

	meson_irtx_fill_buf(ir, tx_buf, buf, len);
	dev_dbg(ir->dev, "TX buffer filled, length = %u\n", len);

	spin_lock_irqsave(&ir->lock, flags);
	meson_irtx_update_buf(ir, tx_buf, len, 0);
	reinit_completion(&ir->completion);
	meson_irtx_send_buffer(ir);
	spin_unlock_irqrestore(&ir->lock, flags);

	if (!wait_for_completion_timeout(&ir->completion,
					 usecs_to_jiffies(IR_MAX_DURATION)))
		ret = -ETIMEDOUT;

	spin_lock_irqsave(&ir->lock, flags);
	kfree(ir->buf);
	meson_irtx_update_buf(ir, NULL, 0, 0);
	spin_unlock_irqrestore(&ir->lock, flags);

	return ret;
}

static int meson_irtx_mod_clock_probe(struct meson_irtx *ir,
				      unsigned int *clk_nr)
{
	struct device_node *np = ir->dev->of_node;
	struct clk *clock;

	if (!np)
		return -ENODEV;

	clock = devm_clk_get(ir->dev, "xtal");
	if (IS_ERR(clock) || clk_prepare_enable(clock))
		return -ENODEV;

	*clk_nr = IRB_MOD_XTAL3_CLK;
	ir->clk_rate = clk_get_rate(clock) / 3;

	if (ir->clk_rate < IRB_MOD_1US_CLK_RATE) {
		*clk_nr = IRB_MOD_1US_CLK;
		ir->clk_rate = IRB_MOD_1US_CLK_RATE;
	}

	dev_info(ir->dev, "F_clk = %luHz\n", ir->clk_rate);

	return 0;
}

static int __init meson_irtx_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct meson_irtx *ir;
	struct rc_dev *rc;
	int irq;
	unsigned int clk_nr;
	int ret;

	ir = devm_kzalloc(dev, sizeof(*ir), GFP_KERNEL);
	if (!ir)
		return -ENOMEM;

	ir->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(ir->reg_base))
		return PTR_ERR(ir->reg_base);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(dev, "no irq resource found\n");
		return -ENODEV;
	}

	ir->dev = dev;
	ir->carrier = MIRTX_DEFAULT_CARRIER;
	ir->duty_cycle = MIRTX_DEFAULT_DUTY_CYCLE;
	init_completion(&ir->completion);
	spin_lock_init(&ir->lock);

	ret = meson_irtx_mod_clock_probe(ir, &clk_nr);
	if (ret) {
		dev_err(dev, "modulator clock setup failed\n");
		return ret;
	}
	meson_irtx_setup(ir, clk_nr);

	ret = devm_request_irq(dev, irq,
			       meson_irtx_irqhandler,
			       IRQF_TRIGGER_RISING,
			       DRIVER_NAME, ir);
	if (ret) {
		dev_err(dev, "irq request failed\n");
		return ret;
	}

	rc = rc_allocate_device(RC_DRIVER_IR_RAW_TX);
	if (!rc)
		return -ENOMEM;

	rc->driver_name = DRIVER_NAME;
	rc->device_name = DEVICE_NAME;
	rc->priv = ir;

	rc->tx_ir = meson_irtx_transmit;
	rc->s_tx_carrier = meson_irtx_set_carrier;
	rc->s_tx_duty_cycle = meson_irtx_set_duty_cycle;

	ret = rc_register_device(rc);
	if (ret < 0) {
		dev_err(dev, "rc_dev registration failed\n");
		rc_free_device(rc);
		return ret;
	}

	platform_set_drvdata(pdev, rc);

	return 0;
}

static int meson_irtx_remove(struct platform_device *pdev)
{
	struct rc_dev *rc = platform_get_drvdata(pdev);

	rc_unregister_device(rc);

	return 0;
}

static const struct of_device_id meson_irtx_dt_match[] = {
	{
		.compatible = "amlogic,meson-g12a-ir-tx",
	},
	{},
};
MODULE_DEVICE_TABLE(of, meson_irtx_dt_match);

static struct platform_driver meson_irtx_pd = {
	.remove = meson_irtx_remove,
	.driver = {
		.name = DRIVER_NAME,
		.owner  = THIS_MODULE,
		.of_match_table = meson_irtx_dt_match,
	},
};

module_platform_driver_probe(meson_irtx_pd, meson_irtx_probe);

MODULE_DESCRIPTION("Meson IR TX driver");
MODULE_AUTHOR("Viktor Prutyanov <viktor.prutyanov@phystech.edu>");
MODULE_LICENSE("GPL");
