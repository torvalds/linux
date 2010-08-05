/* drivers/i2c/busses/i2c_rk2818.c
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/completion.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/cpufreq.h>
#include <linux/device.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <mach/board.h>
#include <asm/io.h>

#include "i2c-rk2818.h"
#define DRV_NAME	"rk2818_i2c"

#define RK2818_I2C_TIMEOUT		(msecs_to_jiffies(500))

enum rk2818_error {
	RK2818_ERROR_NONE = 0,
	RK2818_ERROR_ARBITR_LOSE,
	RK2818_ERROR_UNKNOWN
};

enum rk2818_event {
	RK2818_EVENT_NONE = 0,
	/* master has received ack(MTX mode) 
	   means that data has been sent to slave.
	 */
	RK2818_EVENT_MTX_RCVD_ACK,	
	/* master needs to send ack to slave(MRX mode) 
	   means that data has been received from slave.
	 */
	RK2818_EVENT_MRX_NEED_ACK,	 
	RK2818_EVENT_MAX
};

struct rk2818_i2c_data {
	struct device			*dev;  
	struct i2c_adapter		adap;
	void __iomem			*regs;
	struct resource			*ioarea;

	unsigned int			suspended:1;
	unsigned long			scl_rate;
	struct clk				*clk;

	unsigned int			mode;

	unsigned int			irq;

	spinlock_t				cmd_lock;
	struct completion		cmd_complete;
	enum rk2818_event		cmd_event;
	enum rk2818_error		cmd_err;

	unsigned int			msg_idx;
	unsigned int			msg_num;
#ifdef CONFIG_CPU_FREQ
		struct notifier_block	freq_transition;
#endif
};

static int rk2818_i2c_init_hw(struct rk2818_i2c_data *i2c);

static inline void rk2818_i2c_disable_irqs(struct rk2818_i2c_data *i2c)
{
	unsigned long tmp;

	tmp = readl(i2c->regs + I2C_IER);
	writel(tmp & IRQ_ALL_DISABLE, i2c->regs + I2C_IER);
}
static inline void rk2818_i2c_enable_irqs(struct rk2818_i2c_data *i2c)
{
	unsigned long tmp;

	tmp = readl(i2c->regs + I2C_IER);
	writel(tmp | IRQ_MST_ENABLE, i2c->regs + I2C_IER);
}

/* scl = pclk/(5 *(rem+1) * 2^(exp+1)) */
static void rk2818_i2c_calcdivisor(unsigned long pclk, 
								unsigned long scl_rate, 
								unsigned long *real_rate,
								unsigned int *rem, unsigned int *exp)
{
	unsigned int calc_rem = 0;
	unsigned int calc_exp = 0;

	for(calc_exp = 0; calc_exp < I2CCDVR_EXP_MAX; calc_exp++)
	{
		calc_rem = pclk / (5 * scl_rate * (1 <<(calc_exp +1)));
		if(calc_rem < I2CCDVR_REM_MAX)
			break;
	}
	if(calc_rem >= I2CCDVR_REM_MAX || calc_exp >= I2CCDVR_EXP_MAX)
	{
		calc_rem = I2CCDVR_REM_MAX - 1;
		calc_exp = I2CCDVR_EXP_MAX - 1;
	}
	*rem = calc_rem;
	*exp = calc_exp;
	*real_rate = pclk/(5 * (calc_rem + 1) * (1 <<(calc_exp +1)));
	return;
}
/* set i2c bus scl rate */
static unsigned long  rk2818_i2c_clockrate(struct rk2818_i2c_data *i2c)
{
	struct rk2818_i2c_platform_data *pdata = i2c->dev->platform_data;
	unsigned int rem = 0, exp = 0;
	unsigned long scl_rate, real_rate = 0, tmp, pclk;
	struct clk *arm_pclk;

	arm_pclk = clk_get_parent(i2c->clk);
	if(IS_ERR(arm_pclk))		
	{
		dev_err(i2c->dev, "cannot get pclk\n");
		return -ENOENT;
	}
	pclk = clk_get_rate(arm_pclk);

	scl_rate = (i2c->scl_rate) ? i2c->scl_rate : ((pdata->scl_rate)? pdata->scl_rate:100000);

	rk2818_i2c_calcdivisor(pclk, scl_rate, &real_rate, &rem, &exp);

	tmp = readl(i2c->regs + I2C_OPR);
	tmp |= exp;
	tmp |= rem<<I2CCDVR_EXP_BITS;	
	writel(tmp, i2c->regs + I2C_OPR);
	if(real_rate > 400000)
		dev_info(i2c->dev, "WARN: PCLK %luKhz, I2C set rate %luKhz, and real rate is %luKhz > 400Khz\n", 
				pclk/1000, scl_rate/1000, real_rate/1000);
	else
		dev_dbg(i2c->dev, " OK: PCLK %luKhz, I2C set rate %luKhz, real rate is %luKhz\n", 
				pclk/1000, scl_rate/1000, real_rate/1000);
	return 0;
}
static int rk2818_event_occurred(struct rk2818_i2c_data *i2c)
{
	unsigned long isr;

	isr = readl(i2c->regs + I2C_ISR);
	if(isr & I2C_ISR_ARBITR_LOSE)
	{
		isr &= ~I2C_ISR_ARBITR_LOSE;
		writel(isr, i2c->regs + I2C_ISR);
		i2c->cmd_err = RK2818_ERROR_ARBITR_LOSE;
		return 1;
	}

	switch(i2c->cmd_event)
	{
		case RK2818_EVENT_MTX_RCVD_ACK:
			if(isr & I2C_ISR_MTX_RCVD_ACK)
			{
				isr &= ~I2C_ISR_MTX_RCVD_ACK;
				writel(isr, i2c->regs + I2C_ISR);
				return 1;
			}
		break;
		case RK2818_EVENT_MRX_NEED_ACK:
			if(isr & I2C_ISR_MRX_NEED_ACK)
			{
				isr &= ~I2C_ISR_MRX_NEED_ACK;
				writel(isr, i2c->regs + I2C_ISR);
				return 1;
			}
			break;
		default:
			break;
	}
	i2c->cmd_err = RK2818_ERROR_UNKNOWN;
	return 0;
}

static irqreturn_t rk2818_i2c_irq(int irq, void *data)
{
	struct rk2818_i2c_data *i2c = (struct rk2818_i2c_data *)data;
	int res;
	
	rk2818_i2c_disable_irqs(i2c);
	spin_lock(&i2c->cmd_lock);
	res = rk2818_event_occurred(i2c);
	if(res || i2c->cmd_err != RK2818_ERROR_NONE)
		complete(&i2c->cmd_complete);
	spin_unlock(&i2c->cmd_lock);
	return IRQ_HANDLED;
}
static int wait_for_completion_poll_timeout(struct rk2818_i2c_data *i2c)
{
	unsigned long timeout = jiffies + RK2818_I2C_TIMEOUT;
	unsigned int time = 10;
	int res;

	while(!time_after(jiffies, timeout))
	{
		dev_dbg(i2c->dev, "%s: time = %d\n", __func__, time);
		res = rk2818_event_occurred(i2c);
		if(res || i2c->cmd_err != RK2818_ERROR_NONE)
			return 1;
		udelay(time);
		time *= 2;
	}
	return 0;

}
static int rk2818_wait_event(struct rk2818_i2c_data *i2c,
					enum rk2818_event mr_event)
{
	int ret = 0;

	if(unlikely(irqs_disabled()))
	{
		dev_err(i2c->dev, "irqs are disabled on this system!\n");
		return -EIO;
	}	
	spin_lock_irq(&i2c->cmd_lock);
	i2c->cmd_err = RK2818_ERROR_NONE;
	i2c->cmd_event = mr_event;

	init_completion(&i2c->cmd_complete);

	spin_unlock_irq(&i2c->cmd_lock);

	rk2818_i2c_enable_irqs(i2c);
	if(i2c->mode == I2C_MODE_IRQ)
	{
		ret = wait_for_completion_interruptible_timeout(&i2c->cmd_complete,
								RK2818_I2C_TIMEOUT);
	}
	else
	{
		ret = wait_for_completion_poll_timeout(i2c);
		dev_dbg(i2c->dev, "%s: ret = %d\n", __func__, ret);
	}
	if(ret < 0)
	{
		dev_err(i2c->dev, "wait_for_completion_interruptible_timeout(): "
								"retrun %d waiting for event %04x\n", ret,
								mr_event);
		return ret;
	}
	if(ret == 0)
	{
		return -ETIMEDOUT;
	}
	return 0;
}

static int rk2818_wait_while_busy(struct rk2818_i2c_data *i2c)
{
	unsigned long timeout = jiffies + RK2818_I2C_TIMEOUT;
	unsigned long lsr;
	unsigned int time = 10;
	dev_dbg(i2c->dev,"wait_while_busy");
	while(!time_after(jiffies, timeout))
	{
		lsr = readl(i2c->regs + I2C_LSR);
		if(!(lsr & I2C_LSR_BUSY))
			return 0;
		udelay(time);
		time *= 2;
	}
	return -ETIMEDOUT;
}

static int rk2818_send_address(struct rk2818_i2c_data *i2c,
						struct i2c_msg *msg)
{
	unsigned long addr_1st;
	//unsigned long addr_2nd;
	unsigned long conr = readl(i2c->regs + I2C_CONR);
	int ret = 0;
	/*
	if(msg->flags & I2C_M_TEN)
		addr_1st = (0xf0 | (((unsigned long) msg->addr & 0x300) >> 7)) & 0xff;
	else
	*/
	addr_1st = ((msg->addr << 1) & 0xff);
	if (msg->flags & I2C_M_RD) 
		addr_1st |= 0x01;
	else
		addr_1st &= (~0x01);

	conr |= I2C_CONR_MTX_MODE;
	conr &= I2C_CONR_ACK;
	writel(conr, i2c->regs + I2C_CONR);
	dev_dbg(i2c->dev, "send addr: %lx\n", addr_1st);
	writel(addr_1st, i2c->regs + I2C_MTXR);
	if(i2c->msg_idx == 0)
	{
		ret = rk2818_wait_while_busy(i2c);
		if(ret != 0)
		{
			dev_err(i2c->dev, "send addr:wait_while_busy\n");
			conr = readl(i2c->regs + I2C_CONR);
			conr |= I2C_CONR_NAK;
			writel(conr, i2c->regs + I2C_CONR);
			writel(I2C_LCMR_STOP, i2c->regs + I2C_LCMR);
			return ret;
		}
	}
	if (msg->flags & I2C_M_RD)
		writel(I2C_LCMR_START|I2C_LCMR_RESUME, i2c->regs + I2C_LCMR);
	else
		writel(I2C_LCMR_START, i2c->regs + I2C_LCMR);
	/*
	if(msg->flags & I2C_M_TEN)
	{
		ret = rk2818_wait_event(i2c, RK2818_EVENT_MTX_RCVD_ACK);
		if(ret != 0)
		{
			return ret;
		}
		addr_2nd = msg->addr & 0xff;if (msg->flags & I2C_M_RD)
		writel(addr_2nd, i2c->regs + I2C_MTXR);
		writel(I2C_LCMR_RESUME, i2c->regs + I2C_LCMR);
		if (msg->flags & I2C_M_RD)
		{
			ret = rk2818_wait_event(i2c, RK2818_EVENT_MTX_RCVD_ACK);
			if(ret != 0)
			{
				return ret;
			}

			writel(addr_1st, i2c->regs + I2C_MTXR);
			writel(I2C_LCMR_RESUME, i2c->regs + I2C_LCMR);
		}

	}
	*/
	ret = rk2818_wait_event(i2c, RK2818_EVENT_MTX_RCVD_ACK);
	if(ret != 0)
		dev_err(i2c->dev, "send addr:wait ack timeout\n");
	return ret;
}

static int rk2818_xfer_msg(struct i2c_adapter *adap, 
						 struct i2c_msg *msg, int stop)
{
	struct rk2818_i2c_data *i2c = (struct rk2818_i2c_data *)adap->algo_data;

	int ret, i;
	unsigned long conr = readl(i2c->regs + I2C_CONR);
	conr |= I2C_CONR_MPORT_ENABLE;
	writel(conr, i2c->regs + I2C_CONR);
	if(msg->len == 0)
	{
		ret = -EINVAL;
		goto exit_disable;
	}

	clk_enable(i2c->clk);

	ret = rk2818_send_address(i2c, msg);
	if(ret != 0)
	{
		dev_err(i2c->dev, "send addr error\n");
		goto exit_disable;
	}
	if(msg->flags & I2C_M_RD)
	{	
		conr = readl(i2c->regs + I2C_CONR);
		conr &= I2C_CONR_MRX_MODE;
		writel(conr, i2c->regs + I2C_CONR);

		for(i = 0; i < msg->len; i++)
		{
			writel(I2C_LCMR_RESUME, i2c->regs + I2C_LCMR);
			ret = rk2818_wait_event(i2c, RK2818_EVENT_MRX_NEED_ACK);
			if(ret != 0)
			{
				dev_err(i2c->dev, "read data timeout\n");
				goto exit_disable;
			}
			msg->buf[i] = (uint8_t)readl(i2c->regs + I2C_MRXR);
			dev_dbg(i2c->dev, "receive data=%u\n",msg->buf[i]);
			if(i == msg->len - 1)
			{
				conr = readl(i2c->regs + I2C_CONR);
				conr &= I2C_CONR_ACK;
				writel(conr, i2c->regs + I2C_CONR);
			}
		}
	}
	else
	{
		conr = readl(i2c->regs + I2C_CONR);
		conr |= I2C_CONR_MTX_MODE;
		writel(conr, i2c->regs + I2C_CONR);
		for(i = 0; i < msg->len; i++)
		{
			writel(msg->buf[i], i2c->regs + I2C_MTXR);
			dev_dbg(i2c->dev, "send data =%u\n", msg->buf[i]);
			conr = readl(i2c->regs + I2C_CONR);
			conr |= I2C_CONR_NAK;
			writel(conr, i2c->regs + I2C_CONR);
			writel(I2C_LCMR_RESUME, i2c->regs + I2C_LCMR);

			ret = rk2818_wait_event(i2c, RK2818_EVENT_MTX_RCVD_ACK);
			if(ret != 0)
			{
				dev_err(i2c->dev, "send data timeout\n");
				goto exit_disable;
			}
		}
		
	}
	if(i == msg->len)
	{
		conr = readl(i2c->regs + I2C_CONR);
		conr |= I2C_CONR_NAK;
		writel(conr, i2c->regs + I2C_CONR);
		if(!stop)
			return 0;
		if(msg->flags & I2C_M_TEN)
			writel(I2C_LCMR_START|I2C_LCMR_RESUME , i2c->regs + I2C_LCMR);
		else
			writel(I2C_LCMR_STOP|I2C_LCMR_RESUME , i2c->regs + I2C_LCMR);
		if(i2c->msg_idx >= i2c->msg_num - 1)
		{
			ret = rk2818_wait_while_busy(i2c);
	
			if(ret != 0)
			{
				dev_err(i2c->dev, "tx success wait bus busy time out\n");
				goto exit_disable;
			}
		}
	}
exit_disable:
	conr = readl(i2c->regs + I2C_CONR);
	conr &= I2C_CONR_MPORT_DISABLE;
	writel(conr, i2c->regs + I2C_CONR);
	clk_disable(i2c->clk);
	return ret;

}

static int rk2818_i2c_xfer(struct i2c_adapter *adap,
			struct i2c_msg *msgs, int num)
{
	int ret = -1;
	int i,retry;
	struct rk2818_i2c_data *i2c = (struct rk2818_i2c_data *)adap->algo_data;
	
	if(i2c->suspended ==1)
		return -EIO;
	if(msgs[0].scl_rate <= 400000 && msgs[0].scl_rate > 0)
	{
		i2c->scl_rate = msgs[0].scl_rate;
	}
	else
	{
		//dev_info(i2c->dev, "Scl_rate(%uKhz) is failed to change[0 -- 400Khz],  current rate(%luKhz)\n",
		//		msgs[0].scl_rate/1000, i2c->scl_rate/1000);
		i2c->scl_rate = 400000;
	}

	ret = rk2818_i2c_init_hw(i2c);
	if(ret < 0)
		return ret;
	i2c->msg_num = num;
	for (i = 0; i < num; i++) 
	{
		i2c->msg_idx = i;
		for(retry = 0; retry < adap->retries; retry ++)
		{
			ret = rk2818_xfer_msg(adap, &msgs[i], (i == (num - 1)));
			if(ret == 0)
				break;
		}
		if (ret != 0)
		{
			num = ret;
			dev_err(i2c->dev, "rk2818_xfer_msg error, ret = %d\n", ret);
			break;
		}
	}
	return num;
}

static u32 rk2818_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_10BIT_ADDR;
}

static const struct i2c_algorithm rk2818_i2c_algorithm = {
	.master_xfer		= rk2818_i2c_xfer,
	.functionality		= rk2818_i2c_func,
};



#ifdef CONFIG_CPU_FREQ

#define freq_to_i2c(_n) container_of(_n, struct rk2818_i2c_data, freq_transition)

static int rk2818_i2c_cpufreq_transition(struct notifier_block *nb,
					  unsigned long val, void *data)
{
	struct rk2818_i2c_data *i2c = freq_to_i2c(nb);
	unsigned long flags;
	int delta_f;
	int ret = 0;

	delta_f = clk_get_rate(i2c->clk) - i2c->scl_rate;

	if ((val == CPUFREQ_POSTCHANGE && delta_f < 0) ||
	    (val == CPUFREQ_PRECHANGE && delta_f > 0)) 
	{
		spin_lock_irqsave(&i2c->cmd_lock, flags);
		ret = rk2818_i2c_clockrate(i2c);
		spin_unlock_irqrestore(&i2c->cmd_lock, flags);
	}

	return ret;
}

static inline int rk2818_i2c_register_cpufreq(struct rk2818_i2c_data *i2c)
{
	i2c->freq_transition.notifier_call = rk2818_i2c_cpufreq_transition;

	return cpufreq_register_notifier(&i2c->freq_transition,
					 CPUFREQ_TRANSITION_NOTIFIER);
}

static inline void rk2818_i2c_unregister_cpufreq(struct rk2818_i2c_data *i2c)
{
	cpufreq_unregister_notifier(&i2c->freq_transition,
				    CPUFREQ_TRANSITION_NOTIFIER);
}

#else
static inline int rk2818_i2c_register_cpufreq(struct rk2818_i2c_data *i2c)
{
	return 0;
}

static inline void rk2818_i2c_unregister_cpufreq(struct rk2818_i2c_data *i2c)
{
	return;
}
#endif


static int rk2818_i2c_init_hw(struct rk2818_i2c_data *i2c)
{
	unsigned long lcmr = 0x00000000;

	unsigned long opr = readl(i2c->regs + I2C_OPR);
	opr |= I2C_OPR_RESET_STATUS;
	writel(opr, i2c->regs + I2C_OPR);
	
	writel(lcmr, i2c->regs + I2C_LCMR);

	rk2818_i2c_disable_irqs(i2c);

	if (rk2818_i2c_clockrate(i2c) < 0) {
		dev_err(i2c->dev, "cannot meet bus clkrate required\n");
		return -EINVAL;
	}

	opr = readl(i2c->regs + I2C_OPR);
	//opr &= ~I2C_OPR_RESET_STATUS;
	opr |= I2C_OPR_CORE_ENABLE;
	writel(opr, i2c->regs + I2C_OPR);

	return 0;
}


static int rk2818_i2c_probe(struct platform_device *pdev)
{
	struct rk2818_i2c_data *i2c;
	struct rk2818_i2c_platform_data *pdata;
	struct resource *res;
	int ret;

	pdata = pdev->dev.platform_data;
	if (!pdata) 
	{
		dev_err(&pdev->dev, "no platform data\n");
		return -EINVAL;
	}
	i2c = kzalloc(sizeof(struct rk2818_i2c_data), GFP_KERNEL);
	if (!i2c) 
	{
		dev_err(&pdev->dev, "no memory for state\n");
		return -ENOMEM;
	}
	i2c->mode = pdata->mode;
	i2c->scl_rate = (pdata->scl_rate) ? pdata->scl_rate : 100000;

	strlcpy(i2c->adap.name, DRV_NAME, sizeof(i2c->adap.name));
	i2c->adap.owner   	= THIS_MODULE;
	i2c->adap.algo    	= &rk2818_i2c_algorithm;
	i2c->adap.class   	= I2C_CLASS_HWMON;
	spin_lock_init(&i2c->cmd_lock);

	i2c->dev = &pdev->dev;
	
	i2c->clk = clk_get(&pdev->dev, "i2c");
	if (IS_ERR(i2c->clk)) {
		dev_err(&pdev->dev, "cannot get clock\n");
		ret = -ENOENT;
		goto err_noclk;
	}

	dev_dbg(&pdev->dev, "clock source %p\n", i2c->clk);

	clk_enable(i2c->clk);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "cannot find IO resource\n");
		ret = -ENOENT;
		goto err_clk;
	}

	i2c->ioarea = request_mem_region(res->start, res->end - res->start + 1,
					 pdev->name);

	if (i2c->ioarea == NULL) {
		dev_err(&pdev->dev, "cannot request IO\n");
		ret = -ENXIO;
		goto err_clk;
	}

	i2c->regs = ioremap(res->start, res->end - res->start + 1);

	if (i2c->regs == NULL) {
		dev_err(&pdev->dev, "cannot map IO\n");
		ret = -ENXIO;
		goto err_ioarea;
	}

	dev_dbg(&pdev->dev, "registers %p (%p, %p)\n",
		i2c->regs, i2c->ioarea, res);

	i2c->adap.algo_data = i2c;
	i2c->adap.retries = 4;
	i2c->adap.dev.parent = &pdev->dev;

	if(pdata->cfg_gpio)
		pdata->cfg_gpio(pdev);
	ret = rk2818_i2c_init_hw(i2c);
	if (ret != 0)
		goto err_iomap;

	i2c->irq = ret = platform_get_irq(pdev, 0);
	if (ret <= 0) {
		dev_err(&pdev->dev, "cannot find IRQ\n");
		goto err_iomap;
	}
	if(i2c->mode == I2C_MODE_IRQ)
	{
		ret = request_irq(i2c->irq, rk2818_i2c_irq, IRQF_DISABLED,
			  	dev_name(&pdev->dev), i2c);

		if (ret != 0) {
			dev_err(&pdev->dev, "cannot claim IRQ %d\n", i2c->irq);
			goto err_iomap;
		}
	}
	ret = rk2818_i2c_register_cpufreq(i2c);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to register cpufreq notifier\n");
		goto err_irq;
	}

	i2c->adap.nr = pdata->bus_num;
	ret = i2c_add_numbered_adapter(&i2c->adap);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to add bus to i2c core\n");
		goto err_cpufreq;
	}

	platform_set_drvdata(pdev, i2c);

	dev_info(&pdev->dev, "%s: RK2818 I2C adapter\n", dev_name(&i2c->adap.dev));
	return 0;

 err_cpufreq:
	rk2818_i2c_unregister_cpufreq(i2c);

 err_irq:
 	if(i2c->mode == I2C_MODE_IRQ)
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


static int rk2818_i2c_remove(struct platform_device *pdev)
{
	struct rk2818_i2c_data *i2c = platform_get_drvdata(pdev);

	rk2818_i2c_unregister_cpufreq(i2c);

	i2c_del_adapter(&i2c->adap);
	 if(i2c->mode == I2C_MODE_IRQ)
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
static int rk2818_i2c_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct rk2818_i2c_data *i2c = platform_get_drvdata(pdev);

	i2c->suspended = 1;
	return 0;
}
static int rk2818_i2c_resume(struct platform_device *pdev)
{
	struct rk2818_i2c_data *i2c = platform_get_drvdata(pdev);

	i2c->suspended = 0;
	rk2818_i2c_init_hw(i2c);

	return 0;
}
#else
#define rk2818_i2c_suspend		NULL
#define rk2818_i2c_resume		NULL
#endif


static struct platform_driver rk2818_i2c_driver = {
	.probe		= rk2818_i2c_probe,
	.remove		= rk2818_i2c_remove,
	.suspend	= rk2818_i2c_suspend,
	.resume		= rk2818_i2c_resume,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= DRV_NAME,
	},
};

static int __init rk2818_i2c_adap_init(void)
{
	return platform_driver_register(&rk2818_i2c_driver);
}

static void __exit rk2818_i2c_adap_exit(void)
{
	platform_driver_unregister(&rk2818_i2c_driver);
}

subsys_initcall(rk2818_i2c_adap_init);
module_exit(rk2818_i2c_adap_exit);

MODULE_DESCRIPTION("Driver for RK2818 I2C Bus");
MODULE_AUTHOR("kfx, kfx@rock-chips.com");
MODULE_LICENSE("GPL");
