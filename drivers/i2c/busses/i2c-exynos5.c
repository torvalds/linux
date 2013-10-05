/*
 * Copyright (C) 2012 Samsung Electronics Co., Ltd.
 *
 * Exynos5 series HS-I2C driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kernel.h>
#include <linux/module.h>

#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/io.h>

#include <asm/irq.h>

#include <mach/hs-iic.h>

#include "i2c-exynos5.h"

#if defined(CONFIG_MACH_ODROIDXU)
    #define EXYNOS5_I2C_TIMEOUT (msecs_to_jiffies(10))
#else
    #define EXYNOS5_I2C_TIMEOUT (msecs_to_jiffies(1000))
#endif   

struct exynos5_i2c {
	unsigned int		suspended:1;

	struct i2c_msg		*msg;
	struct completion	msg_complete;
	unsigned int		msg_byte_ptr;

	unsigned int		irq;

	void __iomem		*regs;
	struct clk		*clk;
	struct device		*dev;
	struct resource		*ioarea;
	struct i2c_adapter	adap;
};

static inline void dump_i2c_register(struct exynos5_i2c *i2c)
{
	dev_err(i2c->dev, "Register dump(suspended : %d)\n"
		": CTL          0x%08x\n"
		": FIFO_CTL     0x%08x\n"
		": TRAILING_CTL 0x%08x\n"
		": CLK_CTL      0x%08x\n"
		": CLK_SLOT     0x%08x\n"
		": INT_EN       0x%08x\n"
		": INT_STAT     0x%08x\n"
		": ERR_STAT     0x%08x\n"
		": FIFO_STAT    0x%08x\n"
		": TXDATA       0x%08x\n"
		": RXDATA       0x%08x\n"
		": CONF         0x%08x\n"
		": AUTO_CONF    0x%08x\n"
		": TIMEOUT      0x%08x\n"
		": MANUAL_CMD   0x%08x\n"
		": TRANS_STAT   0x%08x\n"
		": TIMING_HS1   0x%08x\n"
		": TIMING_HS2   0x%08x\n"
		": TIMING_HS3   0x%08x\n"
		": TIMING_FS1   0x%08x\n"
		": TIMING_FS2   0x%08x\n"
		": TIMING_FS3   0x%08x\n"
		": TIMING_SLA   0x%08x\n"
		": ADDR         0x%08x\n"
		, i2c->suspended
		, readl(i2c->regs + HSI2C_CTL)
		, readl(i2c->regs + HSI2C_FIFO_CTL)
		, readl(i2c->regs + HSI2C_TRAILIG_CTL)
		, readl(i2c->regs + HSI2C_CLK_CTL)
		, readl(i2c->regs + HSI2C_CLK_SLOT)
		, readl(i2c->regs + HSI2C_INT_ENABLE)
		, readl(i2c->regs + HSI2C_INT_STATUS)
		, readl(i2c->regs + HSI2C_ERR_STATUS)
		, readl(i2c->regs + HSI2C_FIFO_STATUS)
		, readl(i2c->regs + HSI2C_TX_DATA)
		, readl(i2c->regs + HSI2C_RX_DATA)
		, readl(i2c->regs + HSI2C_CONF)
		, readl(i2c->regs + HSI2C_AUTO_CONFING)
		, readl(i2c->regs + HSI2C_TIMEOUT)
		, readl(i2c->regs + HSI2C_MANUAL_CMD)
		, readl(i2c->regs + HSI2C_TRANS_STATUS)
		, readl(i2c->regs + HSI2C_TIMING_HS1)
		, readl(i2c->regs + HSI2C_TIMING_HS2)
		, readl(i2c->regs + HSI2C_TIMING_HS3)
		, readl(i2c->regs + HSI2C_TIMING_FS1)
		, readl(i2c->regs + HSI2C_TIMING_FS2)
		, readl(i2c->regs + HSI2C_TIMING_FS3)
		, readl(i2c->regs + HSI2C_TIMING_SLA)
		, readl(i2c->regs + HSI2C_ADDR));
}

static inline void exynos5_i2c_stop(struct exynos5_i2c *i2c)
{
	writel(0, i2c->regs + HSI2C_INT_ENABLE);

	complete(&i2c->msg_complete);
}

static irqreturn_t exynos5_i2c_irq(int irqno, void *dev_id)
{
	struct exynos5_i2c *i2c = dev_id;
	unsigned long tmp;
	unsigned char byte;

	if (i2c->msg->flags & I2C_M_RD) {
		while ((readl(i2c->regs + HSI2C_FIFO_STATUS) &
			0x1000000) == 0) {
			byte = (unsigned char)readl(i2c->regs + HSI2C_RX_DATA);
			i2c->msg->buf[i2c->msg_byte_ptr++] = byte;
		}

		if (i2c->msg_byte_ptr >= i2c->msg->len)
			exynos5_i2c_stop(i2c);
	} else {
		byte = i2c->msg->buf[i2c->msg_byte_ptr++];
		writel(byte, i2c->regs + HSI2C_TX_DATA);

		if (i2c->msg_byte_ptr >= i2c->msg->len)
			exynos5_i2c_stop(i2c);
	}

	tmp = readl(i2c->regs + HSI2C_INT_STATUS);
	writel(tmp, i2c->regs +  HSI2C_INT_STATUS);

	return IRQ_HANDLED;
}

static int exynos5_i2c_init(struct exynos5_i2c *i2c);

static int exynos5_i2c_reset(struct exynos5_i2c *i2c)
{
	unsigned long usi_ctl;

	usi_ctl = readl(i2c->regs + HSI2C_CTL);
	usi_ctl |= (1u << 31);
	writel(usi_ctl, i2c->regs + HSI2C_CTL);
	usi_ctl = readl(i2c->regs + HSI2C_CTL);
	usi_ctl &= ~(1u << 31);
	writel(usi_ctl, i2c->regs + HSI2C_CTL);
	exynos5_i2c_init(i2c);

	return 0;
}

static int exynos5_i2c_xfer_msg(struct exynos5_i2c *i2c,
			      struct i2c_msg *msgs, int num, int stop)
{
	unsigned long timeout;
	unsigned long trans_status;
	unsigned long usi_fifo_stat;
	unsigned long usi_ctl;
	unsigned long i2c_auto_conf;
	unsigned long i2c_timeout;
	unsigned long i2c_addr;
	unsigned long usi_int_en;
	unsigned long usi_fifo_ctl;
	unsigned char byte;
	int ret = 0;
	int operation_mode;
	struct exynos5_platform_i2c *pdata;

	pdata = i2c->dev->platform_data;
	operation_mode = pdata->operation_mode;

	i2c->msg = msgs;
	i2c->msg_byte_ptr = 0;

	INIT_COMPLETION(i2c->msg_complete);

	usi_ctl = readl(i2c->regs + HSI2C_CTL);
	i2c_auto_conf = readl(i2c->regs + HSI2C_AUTO_CONFING);
	i2c_timeout = readl(i2c->regs + HSI2C_TIMEOUT);
	i2c_timeout &= ~HSI2C_TIMEOUT_EN;
	writel(i2c_timeout, i2c->regs + HSI2C_TIMEOUT);

	usi_fifo_ctl = HSI2C_RXFIFO_EN | HSI2C_TXFIFO_EN |
		HSI2C_TXFIFO_TRIGGER_LEVEL | HSI2C_RXFIFO_TRIGGER_LEVEL;
	writel(usi_fifo_ctl, i2c->regs + HSI2C_FIFO_CTL);

	usi_int_en = 0;
	if (msgs->flags & I2C_M_RD) {
		usi_ctl &= ~HSI2C_TXCHON;
		usi_ctl |= HSI2C_RXCHON;

		i2c_auto_conf |= HSI2C_READ_WRITE;

		usi_int_en |= (HSI2C_INT_RX_ALMOSTFULL_EN |
			HSI2C_INT_TRAILING_EN);
	} else {
		usi_ctl &= ~HSI2C_RXCHON;
		usi_ctl |= HSI2C_TXCHON;

		i2c_auto_conf &= ~HSI2C_READ_WRITE;

		usi_int_en |= HSI2C_INT_TX_ALMOSTEMPTY_EN;
	}

	if (stop == 1)
		i2c_auto_conf |= HSI2C_STOP_AFTER_TRANS;
	else
		i2c_auto_conf &= ~HSI2C_STOP_AFTER_TRANS;


	i2c_addr = readl(i2c->regs + HSI2C_ADDR);
	i2c_addr &= ~(0x3ff << 10);
	i2c_addr &= ~(0x3ff << 0);
	i2c_addr &= ~(0xff << 24);
	i2c_addr |= ((msgs->addr & 0x7f) << 10);
	writel(i2c_addr, i2c->regs + HSI2C_ADDR);

	writel(usi_ctl, i2c->regs + HSI2C_CTL);

	i2c_auto_conf &= ~(0xffff);
	i2c_auto_conf |= i2c->msg->len;
	writel(i2c_auto_conf, i2c->regs + HSI2C_AUTO_CONFING);

	i2c_auto_conf = readl(i2c->regs + HSI2C_AUTO_CONFING);
	i2c_auto_conf |= HSI2C_MASTER_RUN;
	writel(i2c_auto_conf, i2c->regs + HSI2C_AUTO_CONFING);
	if (operation_mode != HSI2C_POLLING)
		writel(usi_int_en, i2c->regs + HSI2C_INT_ENABLE);

	ret = -EAGAIN;
	if (msgs->flags & I2C_M_RD) {
		if (operation_mode == HSI2C_POLLING) {
			timeout = jiffies + EXYNOS5_I2C_TIMEOUT;
			while (time_before(jiffies, timeout)){
				if ((readl(i2c->regs + HSI2C_FIFO_STATUS) &
					0x1000000) == 0) {
					byte = (unsigned char)readl
						(i2c->regs + HSI2C_RX_DATA);
					i2c->msg->buf[i2c->msg_byte_ptr++]
						= byte;
				}

				if (i2c->msg_byte_ptr >= i2c->msg->len) {
					ret = 0;
					break;
				}
			}

			if (ret == -EAGAIN) {
				exynos5_i2c_reset(i2c);
				dev_warn(i2c->dev, "rx timeout\n");
				return ret;
			}
		} else {
			timeout = wait_for_completion_timeout
				(&i2c->msg_complete, EXYNOS5_I2C_TIMEOUT);

			if (timeout == 0) {
				exynos5_i2c_reset(i2c);
				dev_warn(i2c->dev, "rx timeout\n");
				return ret;
			}

			ret = 0;
		}
	} else {
		if (operation_mode == HSI2C_POLLING) {
			timeout = jiffies + EXYNOS5_I2C_TIMEOUT;
			while (time_before(jiffies, timeout) &&
				(i2c->msg_byte_ptr < i2c->msg->len)) {
				if ((readl(i2c->regs + HSI2C_FIFO_STATUS)
					& 0x7f) < 64) {
					byte = i2c->msg->buf
						[i2c->msg_byte_ptr++];
					writel(byte,
						i2c->regs + HSI2C_TX_DATA);
				}
			}
		} else {
			timeout = wait_for_completion_timeout
				(&i2c->msg_complete, EXYNOS5_I2C_TIMEOUT);

			if (timeout == 0) {
				exynos5_i2c_reset(i2c);
				dev_warn(i2c->dev, "tx timeout\n");
				return ret;
			}

			timeout = jiffies + timeout;
		}
		while (time_before(jiffies, timeout)) {
			usi_fifo_stat = readl(i2c->regs + HSI2C_FIFO_STATUS);
			trans_status = readl(i2c->regs + HSI2C_TRANS_STATUS);
			if((usi_fifo_stat == HSI2C_FIFO_EMPTY) &&
				((trans_status == 0) ||
				((stop == 0) &&
				(trans_status == 0x20000)))) {
				ret = 0;
				break;
			}
		}
		if (ret == -EAGAIN) {
			exynos5_i2c_reset(i2c);
			dev_warn(i2c->dev, "tx timeout\n");
			return ret;
		}
	}

	return ret;
}

static int exynos5_i2c_xfer(struct i2c_adapter *adap,
			struct i2c_msg *msgs, int num)
{
	struct exynos5_i2c *i2c = (struct exynos5_i2c *)adap->algo_data;
	int retry, i;
	int ret;
	int stop = 0;
	struct i2c_msg *msgs_ptr = msgs;

	if (i2c->suspended) {
		dev_err(i2c->dev, "HS-I2C is not initialzed.\n");
		return -EIO;
	}

	clk_enable(i2c->clk);

	for (retry = 0; retry < adap->retries; retry++) {
		for (i = 0; i < num; i++) {
			if (i == num - 1)
				stop = 1;
			ret = exynos5_i2c_xfer_msg(i2c, msgs_ptr, 1, stop);
			msgs_ptr++;

			if (ret == -EAGAIN) {
				msgs_ptr = msgs;
				stop = 0;
				break;
			}
		}
		if (i == num) {
			clk_disable(i2c->clk);
			return num;
		}

		dev_dbg(i2c->dev, "retrying transfer (%d)\n", retry);

		udelay(100);
	}

	clk_disable(i2c->clk);

	return -EREMOTEIO;
}

static u32 exynos5_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C;
}

static const struct i2c_algorithm exynos5_i2c_algorithm = {
	.master_xfer		= exynos5_i2c_xfer,
	.functionality		= exynos5_i2c_func,
};

static int exynos5_i2c_set_timing(struct exynos5_i2c *i2c, int speed_mode)
{
	unsigned long i2c_timing_s1;
	unsigned long i2c_timing_s2;
	unsigned long i2c_timing_s3;
	unsigned long i2c_timing_sla;
	unsigned int op_clk;
	unsigned int clkin = clk_get_rate(i2c->clk);
	unsigned int n_clkdiv;
	unsigned int t_start_su, t_start_hd;
	unsigned int t_stop_su;
	unsigned int t_data_su, t_data_hd;
	unsigned int t_scl_l, t_scl_h;
	unsigned int t_sr_release;
	unsigned int t_ftl_cycle;
	unsigned int i = 0, utemp0 = 0, utemp1 = 0, utemp2 = 0;
	struct exynos5_platform_i2c *pdata;

	pdata = i2c->dev->platform_data;

	if (speed_mode == HSI2C_HIGH_SPD)
		op_clk = pdata->high_speed;
	else
		op_clk = pdata->fast_speed;

	/* FPCLK / FI2C =
	 * (CLK_DIV + 1) * (TSCLK_L + TSCLK_H + 2) + 8 + 2 * FLT_CYCLE
	 * uTemp0 = (CLK_DIV + 1) * (TSCLK_L + TSCLK_H + 2)
	 * uTemp1 = (TSCLK_L + TSCLK_H + 2)
	 * uTemp2 = TSCLK_L + TSCLK_H
	*/
	t_ftl_cycle = (readl(i2c->regs + HSI2C_CONF) >> 16) & 0x7;
	utemp0 = (clkin / op_clk) - 8 - 2 * t_ftl_cycle;

	/* CLK_DIV max is 256 */
	for (i = 0; i < 256; i++) {
		utemp1 = utemp0 / (i + 1);
		/* SCLK_L/H max is 256 / 2 */
		if (utemp1 < 128) {
			utemp2 = utemp1 - 2;
			break;
		}
	}

	n_clkdiv = i;
	t_scl_l = utemp2 / 2;
	t_scl_h = utemp2 / 2;
	t_start_su = t_scl_l;
	t_start_hd = t_scl_l;
	t_stop_su = t_scl_l;
	t_data_su = t_scl_l / 2;
	t_data_hd = t_scl_l / 2;
	t_sr_release = utemp2;

	i2c_timing_s1 = t_start_su << 24 | t_start_hd << 16 | t_stop_su << 8;
	i2c_timing_s2 = t_data_su << 24 | t_scl_l << 8 | t_scl_h << 0;
	i2c_timing_s3 = n_clkdiv << 16 | t_sr_release << 0;
	i2c_timing_sla = t_data_hd << 0;

	dev_dbg(i2c->dev, "tSTART_SU: %X, tSTART_HD: %X, tSTOP_SU: %X\n",
		t_start_su, t_start_hd, t_stop_su);
	dev_dbg(i2c->dev, "tDATA_SU: %X, tSCL_L: %X, tSCL_H: %X\n",
		t_data_su, t_scl_l, t_scl_h);
	dev_dbg(i2c->dev, "nClkDiv: %X, tSR_RELEASE: %X\n",
		n_clkdiv, t_sr_release);
	dev_dbg(i2c->dev, "tDATA_HD: %X\n", t_data_hd);

	if (speed_mode == HSI2C_HIGH_SPD) {
		writel(i2c_timing_s1, i2c->regs + HSI2C_TIMING_HS1);
		writel(i2c_timing_s2, i2c->regs + HSI2C_TIMING_HS2);
		writel(i2c_timing_s3, i2c->regs + HSI2C_TIMING_HS3);
	}
	else {
		writel(i2c_timing_s1, i2c->regs + HSI2C_TIMING_FS1);
		writel(i2c_timing_s2, i2c->regs + HSI2C_TIMING_FS2);
		writel(i2c_timing_s3, i2c->regs + HSI2C_TIMING_FS3);
	}
	writel(i2c_timing_sla, i2c->regs + HSI2C_TIMING_SLA);

	return 0;
}

static int exynos5_i2c_init(struct exynos5_i2c *i2c)
{
	unsigned long usi_ctl = HSI2C_FUNC_MODE_I2C | HSI2C_MASTER;
	unsigned long usi_trailing_ctl = HSI2C_TRAILING_COUNT;
	unsigned long i2c_conf = readl(i2c->regs + HSI2C_CONF);
	struct exynos5_platform_i2c *pdata;

	pdata = i2c->dev->platform_data;

	if (pdata->cfg_gpio)
		pdata->cfg_gpio(to_platform_device(i2c->dev));

	writel(usi_ctl, i2c->regs + HSI2C_CTL);
	writel(usi_trailing_ctl, i2c->regs + HSI2C_TRAILIG_CTL);
	exynos5_i2c_set_timing(i2c, HSI2C_FAST_SPD);
	if (pdata->speed_mode == HSI2C_HIGH_SPD) {
		exynos5_i2c_set_timing(i2c, HSI2C_HIGH_SPD);
		i2c_conf |= (1 << 29);
		writel(i2c_conf, i2c->regs + HSI2C_CONF);
	}

	return 0;
}

static int exynos5_i2c_probe(struct platform_device *pdev)
{
	struct exynos5_i2c *i2c;
	struct exynos5_platform_i2c *pdata;
	struct resource *res;
	int ret;

	pdata = pdev->dev.platform_data;
	if (!pdata) {
		dev_err(&pdev->dev, "no platform data\n");
		return -EINVAL;
	}

	i2c = kzalloc(sizeof(struct exynos5_i2c), GFP_KERNEL);
	if (!i2c) {
		dev_err(&pdev->dev, "no memory for state\n");
		return -ENOMEM;
	}

	strlcpy(i2c->adap.name, "exynos5250-i2c", sizeof(i2c->adap.name));
	i2c->adap.owner   = THIS_MODULE;
	i2c->adap.algo    = &exynos5_i2c_algorithm;
	i2c->adap.retries = 2;
	i2c->adap.class   = I2C_CLASS_HWMON | I2C_CLASS_SPD;

	init_completion(&i2c->msg_complete);

	i2c->dev = &pdev->dev;
	i2c->clk = clk_get(&pdev->dev, "i2c");
	if (IS_ERR(i2c->clk)) {
		dev_err(&pdev->dev, "cannot get clock\n");
		ret = -ENOENT;
		goto err_noclk;
	}

	clk_enable(i2c->clk);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "cannot find HS-I2C IO resource\n");
		ret = -ENOENT;
		goto err_clk;
	}

	i2c->ioarea = request_mem_region(res->start, resource_size(res),
					 pdev->name);

	if (i2c->ioarea == NULL) {
		dev_err(&pdev->dev, "cannot request HS-I2C IO\n");
		ret = -ENXIO;
		goto err_clk;
	}

	i2c->regs = ioremap(res->start, resource_size(res));

	if (i2c->regs == NULL) {
		dev_err(&pdev->dev, "cannot map HS-I2C IO\n");
		ret = -ENXIO;
		goto err_ioarea;
	}

	dev_dbg(&pdev->dev, "registers %p (%p, %p)\n",
		i2c->regs, i2c->ioarea, res);

	i2c->adap.algo_data = i2c;
	i2c->adap.dev.parent = &pdev->dev;

	ret = exynos5_i2c_init(i2c);
	if (ret != 0)
		goto err_iomap;

	i2c->irq = ret = platform_get_irq(pdev, 0);
	if (ret <= 0) {
		dev_err(&pdev->dev, "cannot find HS-I2C IRQ\n");
		goto err_iomap;
	}

	ret = request_irq(i2c->irq, exynos5_i2c_irq, IRQF_DISABLED,
			  dev_name(&pdev->dev), i2c);

	if (ret != 0) {
		dev_err(&pdev->dev, "cannot request HS-I2C IRQ %d\n", i2c->irq);
		goto err_iomap;
	}

	i2c->adap.nr = pdata->bus_number;

	ret = i2c_add_numbered_adapter(&i2c->adap);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to add bus to i2c core\n");
		goto err_irq;
	}

	platform_set_drvdata(pdev, i2c);

	dev_info(&pdev->dev, "%s: Exynos5 HS-I2C adapter\n",
		dev_name(&i2c->adap.dev));
	clk_disable(i2c->clk);
	return 0;

 err_irq:
	free_irq(i2c->irq, i2c);

 err_iomap:
	iounmap(i2c->regs);

 err_ioarea:
	release_resource(i2c->ioarea);
	kfree(i2c->ioarea);

 err_clk:
	clk_disable(i2c->clk);
	clk_put(i2c->clk);

 err_noclk:
	kfree(i2c);
	return ret;
}

static int exynos5_i2c_remove(struct platform_device *pdev)
{
	struct exynos5_i2c *i2c = platform_get_drvdata(pdev);

	i2c_del_adapter(&i2c->adap);
	free_irq(i2c->irq, i2c);

	clk_disable(i2c->clk);
	clk_put(i2c->clk);

	iounmap(i2c->regs);

	release_resource(i2c->ioarea);
	kfree(i2c->ioarea);
	kfree(i2c);

	return 0;
}

#ifdef CONFIG_PM
static int exynos5_i2c_suspend_noirq(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct exynos5_i2c *i2c = platform_get_drvdata(pdev);

	i2c_lock_adapter(&i2c->adap);
	i2c->suspended = 1;
	i2c_unlock_adapter(&i2c->adap);

	return 0;
}

static int exynos5_i2c_resume_noirq(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct exynos5_i2c *i2c = platform_get_drvdata(pdev);

	i2c_lock_adapter(&i2c->adap);
	clk_enable(i2c->clk);
	exynos5_i2c_init(i2c);
	clk_disable(i2c->clk);
	i2c->suspended = 0;
	i2c_unlock_adapter(&i2c->adap);

	return 0;
}

static const struct dev_pm_ops exynos5_i2c_dev_pm_ops = {
	.suspend_noirq = exynos5_i2c_suspend_noirq,
	.resume_noirq = exynos5_i2c_resume_noirq,
};

#define EXYNOS5_DEV_PM_OPS (&exynos5_i2c_dev_pm_ops)
#else
#define EXYNOS5_DEV_PM_OPS NULL
#endif

static struct platform_device_id exynos5_driver_ids[] = {
	{
		.name		= "exynos5-hs-i2c",
		.driver_data	= 0,
	}, { },
};
MODULE_DEVICE_TABLE(platform, exynos5_driver_ids);

static struct platform_driver exynos5_i2c_driver = {
	.probe		= exynos5_i2c_probe,
	.remove		= exynos5_i2c_remove,
	.id_table	= exynos5_driver_ids,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "exynos5-i2c",
		.pm	= EXYNOS5_DEV_PM_OPS,
	},
};

static int __init i2c_adap_exynos5_init(void)
{
	return platform_driver_register(&exynos5_i2c_driver);
}
subsys_initcall(i2c_adap_exynos5_init);

static void __exit i2c_adap_exynos5_exit(void)
{
	platform_driver_unregister(&exynos5_i2c_driver);
}
module_exit(i2c_adap_exynos5_exit);

MODULE_DESCRIPTION("Exynos5 HS-I2C Bus driver");
MODULE_AUTHOR("Taekgyun Ko, <taeggyun.ko@samsung.com>");
MODULE_LICENSE("GPL");
