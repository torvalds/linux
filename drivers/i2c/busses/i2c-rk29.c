/* drivers/i2c/busses/i2c_rk29.c
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
#include <linux/wakelock.h>
#include <linux/slab.h>
#include <mach/board.h>
#include <asm/io.h>

#include "i2c-rk29.h"
#define DRV_NAME	"rk29_i2c"
#define RETRY_NUM	1
									
#define RK29_UDELAY_TIME(scl_rate)			((400*1000)/(scl_rate))
/*max ACK delay time = RK29_I2C_ACK_TIMEOUT_COUNT * RK29_UDELAY_TIME(scl_rate)   us */
#define RK29_I2C_ACK_TIMEOUT_COUNT			(100 * 1000)
/*max STOP delay time = RK29_I2C_STOP_TIMEOUT_COUNT * RK29_UDELAY_TIME(scl_rate)   us */
#define RK29_I2C_STOP_TIMEOUT_COUNT			70//1000
/*max START delay time = RK29_I2C_START_TIMEOUT_COUNT * RK29_UDELAY_TIME(scl_rate)   us */
#define RK29_I2C_START_TIMEOUT_COUNT		1000



#if 0
#define i2c_dbg(dev, format, arg...)		\
	dev_printk(KERN_INFO , dev , format , ## arg)
#define i2c_err(dev, format, arg...)		\
		dev_printk(KERN_ERR , dev , format , ## arg)

#else
#define i2c_dbg(dev, format, arg...)
#define i2c_err(dev, format, arg...)	

#endif

enum rk29_error {
	RK29_ERROR_NONE = 0,
	RK29_ERROR_ARBITR_LOSE,
	RK29_ERROR_UNKNOWN
};

enum rk29_event {
	RK29_EVENT_NONE = 0,
	/* master has received ack(MTX mode) 
	   means that data has been sent to slave.
	 */
	RK29_EVENT_MTX_RCVD_ACK,	
	/* master needs to send ack to slave(MRX mode) 
	   means that data has been received from slave.
	 */
	RK29_EVENT_MRX_NEED_ACK,	 
	RK29_EVENT_MAX
};

struct rk29_i2c_data {
	struct device			*dev;  
	struct i2c_adapter		adap;
	void __iomem			*regs;
	struct resource			*ioarea;

	unsigned int			ack_timeout;    //unit: us
	unsigned int			udelay_time;    //unit: us

	unsigned int			suspended:1;
	unsigned long			scl_rate;
	unsigned long			i2c_rate;
	struct clk				*clk;

	unsigned int			mode;
	int 					retry;

	int 					poll_status;

	unsigned int			irq;

	spinlock_t				cmd_lock;
	struct completion		cmd_complete;
	enum rk29_event		cmd_event;
	enum rk29_error		cmd_err;

	unsigned int			msg_idx;
	unsigned int			msg_num;
	int						udelay;
	int (*io_init)(void);
#ifdef CONFIG_CPU_FREQ
		struct notifier_block	freq_transition;
#endif	
};

static struct wake_lock idlelock; /* only for i2c0 */

static void rk29_set_ack(struct rk29_i2c_data *i2c)
{
	unsigned long conr = readl(i2c->regs + I2C_CONR);

	conr &= I2C_CONR_ACK;
	writel(conr,i2c->regs + I2C_CONR);
	return;
}
static void rk29_set_nak(struct rk29_i2c_data *i2c)
{
	unsigned long conr = readl(i2c->regs + I2C_CONR);

	conr |= I2C_CONR_NAK;
	writel(conr,i2c->regs + I2C_CONR);
	return;
}


static inline void rk29_i2c_disable_irqs(struct rk29_i2c_data *i2c)
{
	unsigned long tmp;

	tmp = readl(i2c->regs + I2C_IER);
	writel(tmp & IRQ_ALL_DISABLE, i2c->regs + I2C_IER);
}
static inline void rk29_i2c_enable_irqs(struct rk29_i2c_data *i2c)
{
	unsigned long tmp;

	tmp = readl(i2c->regs + I2C_IER);
	writel(tmp | IRQ_MST_ENABLE, i2c->regs + I2C_IER);
}

/* scl = pclk/(5 *(rem+1) * 2^(exp+1)) */
static void rk29_i2c_calcdivisor(unsigned long pclk, 
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
static void  rk29_i2c_clockrate(struct rk29_i2c_data *i2c)
{

	struct rk29_i2c_platform_data *pdata = i2c->dev->platform_data;
	unsigned int rem = 0, exp = 0;
	unsigned long scl_rate, real_rate = 0, tmp;

	i2c->i2c_rate = clk_get_rate(i2c->clk);

	scl_rate = (i2c->scl_rate) ? i2c->scl_rate : ((pdata->scl_rate)? pdata->scl_rate:100000);

	rk29_i2c_calcdivisor(i2c->i2c_rate, scl_rate, &real_rate, &rem, &exp);

	tmp = readl(i2c->regs + I2C_OPR);
	tmp &= ~0x3f;
	tmp |= exp;
	tmp |= rem<<I2CCDVR_EXP_BITS;	
	writel(tmp, i2c->regs + I2C_OPR);
	if(real_rate > 400000)
		dev_warn(i2c->dev, "i2c_rate[%luKhz], scl_rate[%luKhz], real_rate[%luKhz] > 400Khz\n", 
				i2c->i2c_rate/1000, scl_rate/1000, real_rate/1000);
	else
		i2c_dbg(i2c->dev, "i2c_rate[%luKhz], scl_rate[%luKhz], real_rate[%luKhz]\n", 
				i2c->i2c_rate/1000, scl_rate/1000, real_rate/1000);
	return;
}
static int rk29_event_occurred(struct rk29_i2c_data *i2c)
{
	unsigned long isr, lsr;

	isr = readl(i2c->regs + I2C_ISR);
	lsr = readl(i2c->regs + I2C_LSR);
	i2c_dbg(i2c->dev,"event occurred, isr = %lx, lsr = %lx\n", isr, lsr);
	if(isr & I2C_ISR_ARBITR_LOSE)
	{
		writel(0, i2c->regs + I2C_ISR);
		i2c->cmd_err = RK29_ERROR_ARBITR_LOSE;
		i2c_err(i2c->dev, "<error>arbitration loss\n");
		return 0;
	}

	switch(i2c->cmd_event)
	{
		case RK29_EVENT_MTX_RCVD_ACK:
			if(isr & I2C_ISR_MTX_RCVD_ACK)
			{
				isr &= ~I2C_ISR_MTX_RCVD_ACK;
				writel(isr, i2c->regs + I2C_ISR);
				return 1;
			}
		break;
		case RK29_EVENT_MRX_NEED_ACK:
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
	writel(0, i2c->regs + I2C_ISR);
	i2c->cmd_err = RK29_ERROR_UNKNOWN;
	return 0;
}

static irqreturn_t rk29_i2c_irq(int irq, void *data)
{
	struct rk29_i2c_data *i2c = (struct rk29_i2c_data *)data;
	int res;
	
	rk29_i2c_disable_irqs(i2c);
	spin_lock(&i2c->cmd_lock);
	res = rk29_event_occurred(i2c);
	if(res)
	{
		if(i2c->mode == I2C_MODE_IRQ)
			complete(&i2c->cmd_complete);
		else
			i2c->poll_status = 1;
	}
	spin_unlock(&i2c->cmd_lock);
	return IRQ_HANDLED;
}
static int wait_for_completion_poll_timeout(struct rk29_i2c_data *i2c)
{
	int tmo = RK29_I2C_ACK_TIMEOUT_COUNT;
	
	while(--tmo)
	{
		if(i2c->poll_status == 1)
			return 1;
		udelay(i2c->udelay_time);
	}
	return 0;
}
static int rk29_wait_event(struct rk29_i2c_data *i2c,
					enum rk29_event mr_event)
{
	int ret = 0;

	if(unlikely(irqs_disabled()))
	{
		i2c_err(i2c->dev, "irqs are disabled on this system!\n");
		return -EIO;
	}
	i2c->cmd_err = RK29_ERROR_NONE;
	i2c->cmd_event = mr_event;
	rk29_i2c_enable_irqs(i2c);
	if(i2c->mode == I2C_MODE_IRQ)
	{
		ret = wait_for_completion_interruptible_timeout(&i2c->cmd_complete,
								usecs_to_jiffies(i2c->ack_timeout));
	}
	else
	{
		i2c->poll_status = 0;
		ret = wait_for_completion_poll_timeout(i2c);
	}
	if(ret < 0)
	{
		i2c_err(i2c->dev, "i2c wait for event %04x, retrun %d \n", mr_event, ret);
		return ret;
	}
	if(ret == 0)
	{
		i2c_err(i2c->dev, "i2c wait for envent timeout, but not return -ETIMEDOUT\n");
		return 0;
		//return -ETIMEDOUT;
	}
	return 0;
}

static void rk29_i2c_stop(struct rk29_i2c_data *i2c)
{
	int tmo = RK29_I2C_STOP_TIMEOUT_COUNT;

	writel(I2C_LCMR_STOP|I2C_LCMR_RESUME, i2c->regs + I2C_LCMR);
	while(--tmo && !(readl(i2c->regs + I2C_LCMR) & I2C_LCMR_STOP))
	{
		udelay(i2c->udelay_time);
	}
	writel(0, i2c->regs + I2C_ISR);
	rk29_i2c_disable_irqs(i2c);
	
	udelay(tmo);
	return;
}
static void rk29_wait_while_busy(struct rk29_i2c_data *i2c)
{
	int tmo = RK29_I2C_START_TIMEOUT_COUNT;
	
	while(--tmo && (readl(i2c->regs + I2C_LSR) & I2C_LSR_BUSY))
	{
		udelay(i2c->udelay_time);
	}
	return;
}

static int rk29_send_2nd_addr(struct rk29_i2c_data *i2c,
						struct i2c_msg *msg, int start)
{
	int ret = 0;
	unsigned long lsr;
	unsigned long addr_2nd = msg->addr & 0xff;

	i2c_dbg(i2c->dev, "i2c send addr_2nd: %lx\n", addr_2nd);	
	writel(addr_2nd, i2c->regs + I2C_MTXR);
	if(i2c->mode == I2C_MODE_IRQ)
		INIT_COMPLETION(i2c->cmd_complete);
	writel(I2C_LCMR_RESUME, i2c->regs + I2C_LCMR);
	rk29_set_ack(i2c);
	
	if((ret = rk29_wait_event(i2c, RK29_EVENT_MTX_RCVD_ACK)) != 0)
	{
		i2c_err(i2c->dev, "after sent addr_2nd, i2c wait for ACK timeout\n");
		return ret;
	}
	lsr = readl(i2c->regs + I2C_LSR);
	if((lsr & I2C_LSR_RCV_NAK) && !(msg->flags & I2C_M_IGNORE_NAK))
		return -EINVAL;
	return ret;
}
static int rk29_send_address(struct rk29_i2c_data *i2c,
						struct i2c_msg *msg, int start)
{
	unsigned long addr_1st;
	unsigned long conr,lsr;
	int ret = 0;
	
	if(msg->flags & I2C_M_TEN)
		addr_1st = (0xf0 | (((unsigned long) msg->addr & 0x300) >> 7)) & 0xff;
	else
		addr_1st = ((msg->addr << 1) & 0xff);
	
	if (msg->flags & I2C_M_RD) 
		addr_1st |= 0x01;
	else
		addr_1st &= (~0x01);

	if(start)
		rk29_wait_while_busy(i2c);
	
	writel(0, i2c->regs + I2C_ISR);
	conr = readl(i2c->regs + I2C_CONR);
	conr |= I2C_CONR_MTX_MODE;
	conr |= I2C_CONR_MPORT_ENABLE;
	writel(conr, i2c->regs + I2C_CONR);

	i2c_dbg(i2c->dev, "i2c send addr_1st: %lx\n", addr_1st);
	writel(addr_1st, i2c->regs + I2C_MTXR);
	rk29_set_ack(i2c);
	if(i2c->mode == I2C_MODE_IRQ)
		INIT_COMPLETION(i2c->cmd_complete);
	writel(I2C_LCMR_START|I2C_LCMR_RESUME, i2c->regs + I2C_LCMR);

	if((ret = rk29_wait_event(i2c, RK29_EVENT_MTX_RCVD_ACK)) != 0)
	{
		i2c_err(i2c->dev, "after sent addr_1st, i2c wait for ACK timeout\n");
		return ret;
	}
	lsr = readl(i2c->regs + I2C_LSR);
	if((lsr & I2C_LSR_RCV_NAK) && !(msg->flags & I2C_M_IGNORE_NAK))
	{
		dev_info(i2c->dev, "addr: 0x%x receive no ack\n", msg->addr);
		return -EAGAIN;
	}
	if(start && (msg->flags & I2C_M_TEN))
		ret = rk29_send_2nd_addr(i2c, msg, start);
	return ret;
}

static int rk29_i2c_send_msg(struct rk29_i2c_data *i2c, struct i2c_msg *msg)
{
	int i, ret = 0;
	unsigned long conr, lsr;
	
	conr = readl(i2c->regs + I2C_CONR);
	conr |= I2C_CONR_MTX_MODE;
	//conr |= I2C_CONR_MPORT_ENABLE;
	writel(conr, i2c->regs + I2C_CONR);
	
	for(i = 0; i < msg->len; i++)
	{
		i2c_dbg(i2c->dev, "i2c send buf[%d]: %x\n", i, msg->buf[i]);	
		writel(msg->buf[i], i2c->regs + I2C_MTXR);
		rk29_set_ack(i2c);
		if(i2c->mode == I2C_MODE_IRQ)
			INIT_COMPLETION(i2c->cmd_complete);
		writel(I2C_LCMR_RESUME, i2c->regs + I2C_LCMR);

		if((ret = rk29_wait_event(i2c, RK29_EVENT_MTX_RCVD_ACK)) != 0)
			return ret;
		lsr = readl(i2c->regs + I2C_LSR);
		if((lsr & I2C_LSR_RCV_NAK) && (i != msg->len -1) && !(msg->flags & I2C_M_IGNORE_NAK))
			return -EINVAL;
		udelay(i2c->udelay);

	}
	return ret;
}
static int rk29_i2c_recv_msg(struct rk29_i2c_data *i2c, struct i2c_msg *msg)
{
	int i, ret = 0;
	unsigned long conr;

	conr = readl(i2c->regs + I2C_CONR);
	conr &= I2C_CONR_MRX_MODE;
	//conr |= I2C_CONR_MPORT_ENABLE;
	writel(conr, i2c->regs + I2C_CONR);
	
	for(i = 0; i < msg->len; i++)
	{
		if(i2c->mode == I2C_MODE_IRQ)
			INIT_COMPLETION(i2c->cmd_complete);
		writel(I2C_LCMR_RESUME, i2c->regs + I2C_LCMR);
		if((ret = rk29_wait_event(i2c, RK29_EVENT_MRX_NEED_ACK)) != 0)
			return ret;
		msg->buf[i] = (uint8_t)readl(i2c->regs + I2C_MRXR);

		if( i == msg->len -1)
			rk29_set_nak(i2c);
		else
			rk29_set_ack(i2c);
		udelay(i2c->udelay);
		i2c_dbg(i2c->dev, "i2c recv >>>>>>>>>>>> buf[%d]: %x\n", i, msg->buf[i]);
	}
	return ret;
}
static int rk29_xfer_msg(struct i2c_adapter *adap, 
						 struct i2c_msg *msg, int start, int stop)
{
	struct rk29_i2c_data *i2c = (struct rk29_i2c_data *)adap->algo_data;
	int ret = 0;
	
	if(msg->len == 0)
	{
		ret = -EINVAL;
		i2c_err(i2c->dev, "<error>msg->len = %d\n", msg->len);
		goto exit;
	}
	if(msg->flags & I2C_M_NEED_DELAY)
		i2c->udelay = msg->udelay;
	else
		i2c->udelay = 0;
	if((ret = rk29_send_address(i2c, msg, start))!= 0)
	{
		rk29_set_nak(i2c);
		i2c_err(i2c->dev, "<error>rk29_send_address timeout\n");
		goto exit;
	}
	if(msg->flags & I2C_M_RD)
	{
		if(msg->flags & I2C_M_REG8_DIRECT)
		{
			struct i2c_msg msg1 = *msg;
			struct i2c_msg msg2 = *msg;
			msg1.len = 1;
			msg2.len = msg->len - 1;
			msg2.buf = msg->buf + 1;

			if((ret = rk29_i2c_send_msg(i2c, &msg1)) != 0)
				i2c_err(i2c->dev, "<error>rk29_i2c_send_msg timeout\n");
			if((ret = rk29_i2c_recv_msg(i2c, &msg2)) != 0)
			{
				i2c_err(i2c->dev, "<error>rk29_i2c_recv_msg timeout\n");
				goto exit;
			}
			
		}
		else if((ret = rk29_i2c_recv_msg(i2c, msg)) != 0)
		{
			i2c_err(i2c->dev, "<error>rk29_i2c_recv_msg timeout\n");
			goto exit;
		}
	}
	else
	{
		if((ret = rk29_i2c_send_msg(i2c, msg)) != 0)
		{
			rk29_set_nak(i2c);
			i2c_err(i2c->dev, "<error>rk29_i2c_send_msg timeout\n");
			goto exit;
		}
	}
	
exit:	
	if(stop || ret < 0)
	{
		rk29_i2c_stop(i2c);			
	}
	return ret;

}

static int rk29_i2c_xfer(struct i2c_adapter *adap,
			struct i2c_msg *msgs, int num)
{
	int ret = -1;
	int i;
	struct rk29_i2c_data *i2c = (struct rk29_i2c_data *)adap->algo_data;

	//int retry = i2c->retry;
	/*
	if(i2c->suspended ==1)
		return -EIO;
	*/
	// 400k > scl_rate > 10k
	if(msgs[0].scl_rate <= 400000 && msgs[0].scl_rate >= 10000)
		i2c->scl_rate = msgs[0].scl_rate;
	else if(msgs[0].scl_rate > 400000){
		dev_info(i2c->dev, "Warning: msg[0].scl_rate( = %dKhz) is too high!",
			msgs[0].scl_rate/1000);
		i2c->scl_rate = 400000;	
	}
	else{
		dev_info(i2c->dev, "Warning: msg[0].scl_rate( = %dKhz) is too low!",
			msgs[0].scl_rate/1000);
		i2c->scl_rate = 10000;
	}
	rk29_i2c_clockrate(i2c);

	i2c->udelay_time = RK29_UDELAY_TIME(i2c->scl_rate);
	i2c->ack_timeout = RK29_I2C_ACK_TIMEOUT_COUNT * i2c->udelay_time;

	if (adap->nr == 0)
		wake_lock(&idlelock);

	for (i = 0; i < num; i++) 
	{
		ret = rk29_xfer_msg(adap, &msgs[i], (i == 0), (i == (num - 1)));
		if (ret != 0)
		{
			num = ret;
			i2c_err(i2c->dev, "rk29_xfer_msg error, ret = %d\n", ret);
			break;
		}
	}

	if (adap->nr == 0)
		wake_unlock(&idlelock);

	/*
	if( --retry && num < 0)
	{
		udelay(10000 * 1000/i2c->scl_rate);
		goto retry;
	}
	*/
	if(num < 0)
		dev_err(i2c->dev, "i2c transfer err, client address is 0x%x [20110106]\n", msgs[0].addr);
	return num;
}

static u32 rk29_i2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_10BIT_ADDR;
}

static const struct i2c_algorithm rk29_i2c_algorithm = {
	.master_xfer		= rk29_i2c_xfer,
	.functionality		= rk29_i2c_func,
};

int i2c_suspended(struct i2c_adapter *adap)
{
	struct rk29_i2c_data *i2c = (struct rk29_i2c_data *)adap->algo_data;
	if(adap->nr > 1)
		return 1;
	if(i2c == NULL)
		return 1;
	return i2c->suspended;
}
EXPORT_SYMBOL(i2c_suspended);

static void rk29_i2c_deinit_hw(struct rk29_i2c_data *i2c)
{
	unsigned long opr = readl(i2c->regs + I2C_OPR);

	opr &= ~I2C_OPR_RESET_STATUS;
	writel(opr, i2c->regs);
	return;
}
static void rk29_i2c_init_hw(struct rk29_i2c_data *i2c)
{
	unsigned long opr = readl(i2c->regs + I2C_OPR);
	
	opr |= I2C_OPR_RESET_STATUS;
	writel(opr, i2c->regs + I2C_OPR);

	udelay(10);
	opr = readl(i2c->regs + I2C_OPR);
	opr &= ~I2C_OPR_RESET_STATUS;
	writel(opr, i2c->regs + I2C_OPR);
	
	rk29_i2c_clockrate(i2c); 

	rk29_i2c_disable_irqs(i2c);
	writel(0, i2c->regs + I2C_LCMR);
	writel(0, i2c->regs + I2C_LCMR);
	
	opr = readl(i2c->regs + I2C_OPR);
	opr |= I2C_OPR_CORE_ENABLE;
	writel(opr, i2c->regs + I2C_OPR);

	return;
}

#ifdef CONFIG_CPU_FREQ

#define freq_to_i2c(_n) container_of(_n, struct rk29_i2c_data, freq_transition)

static int rk29_i2c_cpufreq_transition(struct notifier_block *nb,
					  unsigned long val, void *data)
{
	struct rk29_i2c_data *i2c = freq_to_i2c(nb);
	unsigned long flags;
	int delta_f;
	delta_f = clk_get_rate(i2c->clk) - i2c->i2c_rate;

	if ((val == CPUFREQ_POSTCHANGE && delta_f < 0) ||
	    (val == CPUFREQ_PRECHANGE && delta_f > 0)) 
	{
		spin_lock_irqsave(&i2c->cmd_lock, flags);
		rk29_i2c_clockrate(i2c);
		spin_unlock_irqrestore(&i2c->cmd_lock, flags);
	}
	return 0;
}

static inline int rk29_i2c_register_cpufreq(struct rk29_i2c_data *i2c)
{
	if (i2c->adap.nr != 0)
		return 0;
	i2c->freq_transition.notifier_call = rk29_i2c_cpufreq_transition;

	return cpufreq_register_notifier(&i2c->freq_transition,
					 CPUFREQ_TRANSITION_NOTIFIER);
}

static inline void rk29_i2c_unregister_cpufreq(struct rk29_i2c_data *i2c)
{
	if (i2c->adap.nr != 0)
		return;
	cpufreq_unregister_notifier(&i2c->freq_transition,
				    CPUFREQ_TRANSITION_NOTIFIER);
}

#else
static inline int rk29_i2c_register_cpufreq(struct rk29_i2c_data *i2c)
{
	return 0;
}

static inline void rk29_i2c_unregister_cpufreq(struct rk29_i2c_data *i2c)
{
	return;
}
#endif

static int rk29_i2c_probe(struct platform_device *pdev)
{
	struct rk29_i2c_data *i2c;
	struct rk29_i2c_platform_data *pdata = NULL;
	struct resource *res;
	int ret;

	pdata = pdev->dev.platform_data;
	if (!pdata) 
	{
		i2c_err(&pdev->dev, "<error>no platform data\n");
		return -EINVAL;
	}
	i2c = kzalloc(sizeof(struct rk29_i2c_data), GFP_KERNEL);
	if (!i2c) 
	{
		i2c_err(&pdev->dev, "<error>no memory for state\n");
		return -ENOMEM;
	}
	init_completion(&i2c->cmd_complete);
	i2c->retry = RETRY_NUM;
	i2c->mode = pdata->mode;
	i2c->scl_rate = (pdata->scl_rate) ? pdata->scl_rate : 100000;

	strlcpy(i2c->adap.name, DRV_NAME, sizeof(i2c->adap.name));
	i2c->adap.owner   	= THIS_MODULE;
	i2c->adap.algo    	= &rk29_i2c_algorithm;
	i2c->adap.class   	= I2C_CLASS_HWMON;
	i2c->adap.nr		= pdata->bus_num;
    i2c->adap.retries   = 3;
    i2c->adap.timeout   = msecs_to_jiffies(500);

	spin_lock_init(&i2c->cmd_lock);

	i2c->dev = &pdev->dev;
	
	i2c->clk = clk_get(&pdev->dev, "i2c");
	if (IS_ERR(i2c->clk)) {
		i2c_err(&pdev->dev, "<error>cannot get clock\n");
		ret = -ENOENT;
		goto err_noclk;
	}

	clk_enable(i2c->clk);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		i2c_err(&pdev->dev, "<error>cannot find IO resource\n");
		ret = -ENOENT;
		goto err_clk;
	}

	i2c->ioarea = request_mem_region(res->start, res->end - res->start + 1,
					 pdev->name);

	if (i2c->ioarea == NULL) {
		i2c_err(&pdev->dev, "<error>cannot request IO\n");
		ret = -ENXIO;
		goto err_clk;
	}

	i2c->regs = ioremap(res->start, res->end - res->start + 1);

	if (i2c->regs == NULL) {
		i2c_err(&pdev->dev, "<error>annot map IO\n");
		ret = -ENXIO;
		goto err_ioarea;
	}
	i2c->adap.algo_data = i2c;
	i2c->adap.dev.parent = &pdev->dev;

	if(pdata->io_init)
	{
		i2c->io_init = pdata->io_init;
		pdata->io_init();
	} 

	i2c->irq = ret = platform_get_irq(pdev, 0);
	if (ret <= 0) {
		i2c_err(&pdev->dev, "cannot find IRQ\n");
		goto err_iomap;
	}
	ret = request_irq(i2c->irq, rk29_i2c_irq, IRQF_DISABLED,
			  	dev_name(&pdev->dev), i2c);

	if (ret != 0) {
		i2c_err(&pdev->dev, "cannot claim IRQ %d\n", i2c->irq);
		goto err_iomap;
	}
	ret = rk29_i2c_register_cpufreq(i2c);
	if (ret < 0) {
		i2c_err(&pdev->dev, "failed to register cpufreq notifier\n");
		goto err_irq;
	}

	ret = i2c_add_numbered_adapter(&i2c->adap);
	if (ret < 0) {
		i2c_err(&pdev->dev, "failed to add bus to i2c core\n");
		goto err_cpufreq;
	}

	platform_set_drvdata(pdev, i2c);
	rk29_i2c_init_hw(i2c);
	
	dev_info(&pdev->dev, "%s: RK29 I2C adapter\n", dev_name(&i2c->adap.dev));
	return 0;

 err_cpufreq:
	rk29_i2c_unregister_cpufreq(i2c);

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


static int rk29_i2c_remove(struct platform_device *pdev)
{
	struct rk29_i2c_data *i2c = platform_get_drvdata(pdev);


	rk29_i2c_deinit_hw(i2c);
	rk29_i2c_unregister_cpufreq(i2c);

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
static int rk29_i2c_suspend_noirq(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct rk29_i2c_data *i2c = platform_get_drvdata(pdev);

	i2c->suspended = 1;
	return 0;
}

static int rk29_i2c_resume_noirq(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct rk29_i2c_data *i2c = platform_get_drvdata(pdev);

	i2c->suspended = 0;
	rk29_i2c_init_hw(i2c);

	return 0;
}

static struct dev_pm_ops rk29_i2c_pm_ops = {
	.suspend_noirq	= rk29_i2c_suspend_noirq,
	.resume_noirq	= rk29_i2c_resume_noirq,
};
#endif

static struct platform_driver rk29_i2c_driver = {
	.probe		= rk29_i2c_probe,
	.remove		= rk29_i2c_remove,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= DRV_NAME,
#ifdef CONFIG_PM
		.pm	= &rk29_i2c_pm_ops,
#endif
	},
};

static int __init rk29_i2c_adap_init(void)
{
	wake_lock_init(&idlelock, WAKE_LOCK_IDLE, "i2c0");
	return platform_driver_register(&rk29_i2c_driver);
}

static void __exit rk29_i2c_adap_exit(void)
{
	platform_driver_unregister(&rk29_i2c_driver);
}

subsys_initcall(rk29_i2c_adap_init);
module_exit(rk29_i2c_adap_exit);

MODULE_DESCRIPTION("Driver for RK29 I2C Bus");
MODULE_AUTHOR("kfx, kfx@rock-chips.com");
MODULE_LICENSE("GPL");
