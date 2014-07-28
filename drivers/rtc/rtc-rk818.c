/*
 *	Real Time Clock driver for  rk818
 *
 *  Author: zhangqing <zhangqing@rock-chips.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/rtc.h>
#include <linux/slab.h>
#include <linux/bcd.h>
#include <linux/interrupt.h>
#include <linux/ioctl.h>
#include <linux/completion.h>
#include <linux/mfd/rk818.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/irqdomain.h>


/* RTC Definitions */
/* RTC_CTRL_REG bitfields */
#define BIT_RTC_CTRL_REG_STOP_RTC_M		0x01
#define BIT_RTC_CTRL_REG_ROUND_30S_M		0x02
#define BIT_RTC_CTRL_REG_AUTO_COMP_M		0x04
#define BIT_RTC_CTRL_REG_MODE_12_24_M		0x08
#define BIT_RTC_CTRL_REG_TEST_MODE_M		0x10
#define BIT_RTC_CTRL_REG_SET_32_COUNTER_M	0x20
#define BIT_RTC_CTRL_REG_GET_TIME_M		0x40
#define BIT_RTC_CTRL_REG_RTC_V_OPT_M		0x80

/* RTC_STATUS_REG bitfields */
#define BIT_RTC_STATUS_REG_RUN_M		0x02
#define BIT_RTC_STATUS_REG_1S_EVENT_M		0x04
#define BIT_RTC_STATUS_REG_1M_EVENT_M		0x08
#define BIT_RTC_STATUS_REG_1H_EVENT_M		0x10
#define BIT_RTC_STATUS_REG_1D_EVENT_M		0x20
#define BIT_RTC_STATUS_REG_ALARM_M		0x40
#define BIT_RTC_STATUS_REG_POWER_UP_M		0x80

/* RTC_INTERRUPTS_REG bitfields */
#define BIT_RTC_INTERRUPTS_REG_EVERY_M		0x03
#define BIT_RTC_INTERRUPTS_REG_IT_TIMER_M	0x04
#define BIT_RTC_INTERRUPTS_REG_IT_ALARM_M	0x08

/* DEVCTRL bitfields */
#define BIT_RTC_PWDN				0x40

/* REG_SECONDS_REG through REG_YEARS_REG is how many registers? */
#define ALL_TIME_REGS				7
#define ALL_ALM_REGS				6


#define RTC_SET_TIME_RETRIES	5
#define RTC_GET_TIME_RETRIES	5


struct rk818_rtc {
	struct rk818 *rk818;
	struct rtc_device *rtc;
	unsigned int alarm_enabled:1;
};

/*
 * Read current time and date in RTC
 */
static int rk818_rtc_readtime(struct device *dev, struct rtc_time *tm)
{
	struct rk818_rtc *rk818_rtc = dev_get_drvdata(dev);
	struct rk818 *rk818 = rk818_rtc->rk818;
	int ret;
	//int count = 0;
	unsigned char rtc_data[ALL_TIME_REGS + 1];
	u8 rtc_ctl;

	/*Dummy read*/	
	ret = rk818_reg_read(rk818, RK818_RTC_CTRL_REG);
	
	/* Has the RTC been programmed? */
	ret = rk818_reg_read(rk818, RK818_RTC_CTRL_REG);
	if (ret < 0) {
		dev_err(dev, "Failed to read RTC control: %d\n", ret);
		return ret;
	}

	rtc_ctl = ret & (~BIT_RTC_CTRL_REG_RTC_V_OPT_M);

	ret = rk818_reg_write(rk818, RK818_RTC_CTRL_REG, rtc_ctl);
	if (ret < 0) {
		dev_err(dev, "Failed to write RTC control: %d\n", ret);
		return ret;
	}

#if 0	
	/* Read twice to make sure we don't read a corrupt, partially
	 * incremented, value.
	 */
	do {
		ret = rk818_bulk_read(rk818, RK818_SECONDS_REG,
				       ALL_TIME_REGS, rtc_data);
		if (ret != 0)
			continue;

		tm->tm_sec = bcd2bin(rtc_data[0]);
		tm->tm_min = bcd2bin(rtc_data[1]);
		tm->tm_hour = bcd2bin(rtc_data[2]) ;
		tm->tm_mday = bcd2bin(rtc_data[3]);
		tm->tm_mon = bcd2bin(rtc_data[4]) - 1;
		tm->tm_year = bcd2bin(rtc_data[5]) + 100;	
		tm->tm_wday = bcd2bin(rtc_data[6]);

		printk( "RTC date/time %4d-%02d-%02d(%d) %02d:%02d:%02d\n",
			1900 + tm->tm_year, tm->tm_mon + 1, tm->tm_mday, tm->tm_wday,
			tm->tm_hour, tm->tm_min, tm->tm_sec);
		
		return ret;

	} while (++count < RTC_GET_TIME_RETRIES);
	dev_err(dev, "Timed out reading current time\n");
#else
	rtc_data[0] = rk818_reg_read(rk818,0x00);
	rtc_data[1] = rk818_reg_read(rk818,0x01);
	rtc_data[2] = rk818_reg_read(rk818,0x02);
	rtc_data[3] = rk818_reg_read(rk818,0x03);
	rtc_data[4] = rk818_reg_read(rk818,0x04);
	rtc_data[5] = rk818_reg_read(rk818,0x05);
	rtc_data[6] = rk818_reg_read(rk818,0x06);
	
	 tm->tm_sec = bcd2bin(rtc_data[0]);
         tm->tm_min = bcd2bin(rtc_data[1]);
         tm->tm_hour = bcd2bin(rtc_data[2]) ;
         tm->tm_mday = bcd2bin(rtc_data[3]);
         tm->tm_mon = bcd2bin(rtc_data[4]) - 1;
         tm->tm_year = bcd2bin(rtc_data[5]) + 100;       
         tm->tm_wday = bcd2bin(rtc_data[6]);

	  dev_dbg(dev, "RTC date/time %4d-%02d-%02d(%d) %02d:%02d:%02d\n",
                        1900 + tm->tm_year, tm->tm_mon + 1, tm->tm_mday, tm->tm_wday,tm->tm_hour , tm->tm_min, tm->tm_sec);

#endif
	return 0;

}

/*
 * Set current time and date in RTC
 */
static int rk818_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct rk818_rtc *rk818_rtc = dev_get_drvdata(dev);
	struct rk818 *rk818 = rk818_rtc->rk818;
	int ret;
	u8 rtc_ctl;	
	unsigned char rtc_data[ALL_TIME_REGS + 1];
	
	rtc_data[0] = bin2bcd(tm->tm_sec);
	rtc_data[1] = bin2bcd(tm->tm_min);
	rtc_data[2] = bin2bcd(tm->tm_hour );
	rtc_data[3] = bin2bcd(tm->tm_mday);
	rtc_data[4] = bin2bcd(tm->tm_mon + 1);
	rtc_data[5] = bin2bcd(tm->tm_year - 100);
	rtc_data[6] = bin2bcd(tm->tm_wday);

	 dev_dbg(dev, "set RTC date/time %4d-%02d-%02d(%d) %02d:%02d:%02d\n",
                        1900 + tm->tm_year, tm->tm_mon + 1, tm->tm_mday, tm->tm_wday,tm->tm_hour , tm->tm_min, tm->tm_sec);

	/*Dummy read*/	
	ret = rk818_reg_read(rk818, RK818_RTC_CTRL_REG);
	
	/* Stop RTC while updating the TC registers */
	ret = rk818_reg_read(rk818, RK818_RTC_CTRL_REG);
	if (ret < 0) {
		dev_err(dev, "Failed to read RTC control: %d\n", ret);
		return ret;
	}
	
	rtc_ctl = ret | (BIT_RTC_CTRL_REG_STOP_RTC_M);

	ret = rk818_reg_write(rk818, RK818_RTC_CTRL_REG, rtc_ctl);
	if (ret < 0) {
		dev_err(dev, "Failed to write RTC control: %d\n", ret);
		return ret;
	}
#if 0	
	/* update all the time registers in one shot */
	ret = rk818_bulk_write(rk818, RK818_SECONDS_REG,
				       ALL_TIME_REGS, rtc_data);
	if (ret < 0) {
		dev_err(dev, "Failed to read RTC times: %d\n", ret);
		return ret;
	}
#else
	rk818_reg_write(rk818,0x00,rtc_data[0]);
	rk818_reg_write(rk818,0x01,rtc_data[1]);
	rk818_reg_write(rk818,0x02,rtc_data[2]);
	rk818_reg_write(rk818,0x03,rtc_data[3]);
	rk818_reg_write(rk818,0x04,rtc_data[4]);
	rk818_reg_write(rk818,0x05,rtc_data[5]);
	rk818_reg_write(rk818,0x06,rtc_data[6]);

#endif	
	/*Dummy read*/	
	ret = rk818_reg_read(rk818, RK818_RTC_CTRL_REG);
	
	/* Start RTC again */
	ret = rk818_reg_read(rk818, RK818_RTC_CTRL_REG);
	if (ret < 0) {
		dev_err(dev, "Failed to read RTC control: %d\n", ret);
		return ret;
	}
	
	rtc_ctl = ret &(~ BIT_RTC_CTRL_REG_STOP_RTC_M);

	ret = rk818_reg_write(rk818, RK818_RTC_CTRL_REG, rtc_ctl);
	if (ret < 0) {
		dev_err(dev, "Failed to write RTC control: %d\n", ret);
		return ret;
	}

	return 0;
}

/*
 * Read alarm time and date in RTC
 */
static int rk818_rtc_readalarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct rk818_rtc *rk818_rtc = dev_get_drvdata(dev);
	int ret;
	unsigned char alrm_data[ALL_ALM_REGS + 1];
#if 0
	ret = rk818_bulk_read(rk818_rtc->rk818, RK818_ALARM_SECONDS_REG,
			       ALL_ALM_REGS, alrm_data);
	if (ret != 0) {
		dev_err(dev, "Failed to read alarm time: %d\n", ret);
		return ret;
	}
#else
	alrm_data[0] = rk818_reg_read(rk818_rtc->rk818,0x08);
        alrm_data[1] = rk818_reg_read(rk818_rtc->rk818,0x09);
        alrm_data[2] = rk818_reg_read(rk818_rtc->rk818,0x0a);
        alrm_data[3] = rk818_reg_read(rk818_rtc->rk818,0x0b);
        alrm_data[4] = rk818_reg_read(rk818_rtc->rk818,0x0c);
        alrm_data[5] = rk818_reg_read(rk818_rtc->rk818,0x0d);

	
#endif
	/* some of these fields may be wildcard/"match all" */
	alrm->time.tm_sec = bcd2bin(alrm_data[0]);
	alrm->time.tm_min = bcd2bin(alrm_data[1]);
	alrm->time.tm_hour = bcd2bin(alrm_data[2]);
	alrm->time.tm_mday = bcd2bin(alrm_data[3]);
	alrm->time.tm_mon = bcd2bin(alrm_data[4]) - 1;
	alrm->time.tm_year = bcd2bin(alrm_data[5]) + 100;

	ret = rk818_reg_read(rk818_rtc->rk818, RK818_RTC_INT_REG);
	if (ret < 0) {
		dev_err(dev, "Failed to read RTC control: %d\n", ret);
		return ret;
	}
  	dev_dbg(dev,"alrm read RTC date/time %4d-%02d-%02d(%d) %02d:%02d:%02d\n",
                        1900 + alrm->time.tm_year, alrm->time.tm_mon + 1, alrm->time.tm_mday, alrm->time.tm_wday, alrm->time.tm_hour, alrm->time.tm_min, alrm->time.tm_sec);



	if (ret & BIT_RTC_INTERRUPTS_REG_IT_ALARM_M)
		alrm->enabled = 1;
	else
		alrm->enabled = 0;

	return 0;
}

static int rk818_rtc_stop_alarm(struct rk818_rtc *rk818_rtc)
{
	rk818_rtc->alarm_enabled = 0;

	return rk818_clear_bits(rk818_rtc->rk818, RK818_RTC_INT_REG,
			       BIT_RTC_INTERRUPTS_REG_IT_ALARM_M);

}

static int rk818_rtc_start_alarm(struct rk818_rtc *rk818_rtc)
{
	rk818_rtc->alarm_enabled = 1;

	return rk818_set_bits(rk818_rtc->rk818, RK818_RTC_INT_REG,
			       BIT_RTC_INTERRUPTS_REG_IT_ALARM_M,BIT_RTC_INTERRUPTS_REG_IT_ALARM_M);

}

static int rk818_rtc_setalarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct rk818_rtc *rk818_rtc = dev_get_drvdata(dev);
	int ret;
	unsigned char alrm_data[ALL_TIME_REGS + 1];
	
	ret = rk818_rtc_stop_alarm(rk818_rtc);
	if (ret < 0) {
		dev_err(dev, "Failed to stop alarm: %d\n", ret);
		return ret;
	}

	 dev_dbg(dev,"alrm set RTC date/time %4d-%02d-%02d(%d) %02d:%02d:%02d\n",
                        1900 + alrm->time.tm_year, alrm->time.tm_mon + 1, alrm->time.tm_mday, alrm->time.tm_wday, alrm->time.tm_hour, alrm->time.tm_min, alrm->time.tm_sec);

	alrm_data[0] = bin2bcd(alrm->time.tm_sec);
	alrm_data[1] = bin2bcd(alrm->time.tm_min);
	alrm_data[2] = bin2bcd(alrm->time.tm_hour );
	alrm_data[3] = bin2bcd(alrm->time.tm_mday);
	alrm_data[4] = bin2bcd(alrm->time.tm_mon + 1);
	alrm_data[5] = bin2bcd(alrm->time.tm_year - 100);
#if 0
	ret = rk818_bulk_write(rk818_rtc->rk818, RK818_ALARM_SECONDS_REG,
			       ALL_ALM_REGS, alrm_data);
	if (ret != 0) {
		dev_err(dev, "Failed to read alarm time: %d\n", ret);
		return ret;
	}
#else
	 rk818_reg_write(rk818_rtc->rk818,0x08,alrm_data[0]);
        rk818_reg_write(rk818_rtc->rk818,0x09,alrm_data[1]);
        rk818_reg_write(rk818_rtc->rk818,0x0a,alrm_data[2]);
        rk818_reg_write(rk818_rtc->rk818,0x0b,alrm_data[3]);
        rk818_reg_write(rk818_rtc->rk818,0x0c,alrm_data[4]);
        rk818_reg_write(rk818_rtc->rk818,0x0d,alrm_data[5]);

#endif
	if (alrm->enabled) {
		ret = rk818_rtc_start_alarm(rk818_rtc);
		if (ret < 0) {
			dev_err(dev, "Failed to start alarm: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static int rk818_rtc_alarm_irq_enable(struct device *dev,
				       unsigned int enabled)
{
	struct rk818_rtc *rk818_rtc = dev_get_drvdata(dev);

	if (enabled)
		return rk818_rtc_start_alarm(rk818_rtc);
	else
		return rk818_rtc_stop_alarm(rk818_rtc);
}

static int rk818_rtc_update_irq_enable(struct device *dev,
				       unsigned int enabled)
{
	struct rk818_rtc *rk818_rtc = dev_get_drvdata(dev);

	if (enabled)
		return rk818_set_bits(rk818_rtc->rk818, RK818_RTC_INT_REG,
			       BIT_RTC_INTERRUPTS_REG_IT_TIMER_M,BIT_RTC_INTERRUPTS_REG_IT_TIMER_M);
	else
		return rk818_clear_bits(rk818_rtc->rk818, RK818_RTC_INT_REG,
			       BIT_RTC_INTERRUPTS_REG_IT_TIMER_M);
}

/*
 * We will just handle setting the frequency and make use the framework for
 * reading the periodic interupts.
 *
 * @freq: Current periodic IRQ freq:
 * bit 0: every second
 * bit 1: every minute
 * bit 2: every hour
 * bit 3: every day
 */
#if 0
static int rk818_rtc_irq_set_freq(struct device *dev, int freq)
{	
	struct rk818_rtc *rk818_rtc = dev_get_drvdata(dev);
	int ret;	
	u8 rtc_ctl;	
	
	if (freq < 0 || freq > 3)
		return -EINVAL;

	ret = rk818_reg_read(rk818_rtc->rk818, RK818_RTC_INT_REG);
	if (ret < 0) {
		dev_err(dev, "Failed to read RTC interrupt: %d\n", ret);
		return ret;
	}
	
	rtc_ctl = ret | freq;
	
	ret = rk818_reg_write(rk818_rtc->rk818, RK818_RTC_INT_REG, rtc_ctl);
	if (ret < 0) {
		dev_err(dev, "Failed to write RTC control: %d\n", ret);
		return ret;
	}
	
	return ret;
}
#endif

static irqreturn_t rk818_alm_irq(int irq, void *data)
{
	struct rk818_rtc *rk818_rtc = data;
	int ret;
	u8 rtc_ctl;
	
	/*Dummy read -- mandatory for status register*/
	ret = rk818_reg_read(rk818_rtc->rk818, RK818_RTC_STATUS_REG);
	if (ret < 0) {
		printk("%s:Failed to read RTC status: %d\n", __func__, ret);
		return ret;
	}
		
	ret = rk818_reg_read(rk818_rtc->rk818, RK818_RTC_STATUS_REG);
	if (ret < 0) {
		printk("%s:Failed to read RTC status: %d\n", __func__, ret);
		return ret;
	}
	rtc_ctl = ret&0xff;

	//The alarm interrupt keeps its low level, until the micro-controller write 1 in the ALARM bit of the RTC_STATUS_REG register.	
	ret = rk818_reg_write(rk818_rtc->rk818, RK818_RTC_STATUS_REG,rtc_ctl);
	if (ret < 0) {
		printk("%s:Failed to read RTC status: %d\n", __func__, ret);
		return ret;
	}
	
	rtc_update_irq(rk818_rtc->rtc, 1, RTC_IRQF | RTC_AF);
	
	printk("%s:irq=%d,rtc_ctl=0x%x\n",__func__,irq,rtc_ctl);
	return IRQ_HANDLED;
}

static irqreturn_t rk818_per_irq(int irq, void *data)
{
	struct rk818_rtc *rk818_rtc = data;
	
	rtc_update_irq(rk818_rtc->rtc, 1, RTC_IRQF | RTC_UF);

	//printk("%s:irq=%d\n",__func__,irq);
	return IRQ_HANDLED;
}

static const struct rtc_class_ops rk818_rtc_ops = {
	.read_time = rk818_rtc_readtime,
	//.set_mmss = rk818_rtc_set_mmss,
	.set_time = rk818_rtc_set_time,
	.read_alarm = rk818_rtc_readalarm,
	.set_alarm = rk818_rtc_setalarm,
	.alarm_irq_enable = rk818_rtc_alarm_irq_enable,
	//.update_irq_enable = rk818_rtc_update_irq_enable,
	//.irq_set_freq = rk818_rtc_irq_set_freq,
};

#ifdef CONFIG_PM
/* Turn off the alarm if it should not be a wake source. */
static int rk818_rtc_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct rk818_rtc *rk818_rtc = dev_get_drvdata(&pdev->dev);
	int ret;
	
	if (rk818_rtc->alarm_enabled && device_may_wakeup(&pdev->dev))
		ret = rk818_rtc_start_alarm(rk818_rtc);
	else
		ret = rk818_rtc_stop_alarm(rk818_rtc);

	if (ret < 0)
		dev_err(&pdev->dev, "Failed to update RTC alarm: %d\n", ret);

	return 0;
}

/* Enable the alarm if it should be enabled (in case it was disabled to
 * prevent use as a wake source).
 */
static int rk818_rtc_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct rk818_rtc *rk818_rtc = dev_get_drvdata(&pdev->dev);
	int ret;

	if (rk818_rtc->alarm_enabled) {
		ret = rk818_rtc_start_alarm(rk818_rtc);
		if (ret < 0)
			dev_err(&pdev->dev,
				"Failed to restart RTC alarm: %d\n", ret);
	}

	return 0;
}

/* Unconditionally disable the alarm */
static int rk818_rtc_freeze(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct rk818_rtc *rk818_rtc = dev_get_drvdata(&pdev->dev);
	int ret;
	
	ret = rk818_rtc_stop_alarm(rk818_rtc);
	if (ret < 0)
		dev_err(&pdev->dev, "Failed to stop RTC alarm: %d\n", ret);

	return 0;
}
#else
#define rk818_rtc_suspend NULL
#define rk818_rtc_resume NULL
#define rk818_rtc_freeze NULL
#endif
extern struct rk818 *g_rk818;
struct platform_device *rk818_pdev;
struct rtc_time rk818_tm_def = {	//	2012.1.1 12:00:00 Saturday
			.tm_wday = 6,
			.tm_year = 112,
			.tm_mon = 0,
			.tm_mday = 1,
			.tm_hour = 12,
			.tm_min = 0,
			.tm_sec = 0,
};

static int rk818_rtc_probe(struct platform_device *pdev)
{
	struct rk818 *rk818 = dev_get_drvdata(pdev->dev.parent);
	struct rk818_rtc *rk818_rtc;
	struct rtc_time tm;
	int per_irq;
	int alm_irq;
	int ret = 0;
	u8 rtc_ctl;

	printk("%s,line=%d\n", __func__,__LINE__);

	rk818_rtc = devm_kzalloc(&pdev->dev,sizeof(*rk818_rtc), GFP_KERNEL);
	if (rk818_rtc == NULL)
		return -ENOMEM;

	platform_set_drvdata(pdev, rk818_rtc);
	rk818_rtc->rk818 = rk818;
	
	/* Take rtc out of reset */
	/*
	ret = rk818_reg_read(rk818, RK818_DEVCTRL);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to read RK818_DEVCTRL: %d\n", ret);
		return ret;
	}
	
	if(ret & BIT_RTC_PWDN)
	{
		rtc_ctl = ret & (~BIT_RTC_PWDN);

		ret = rk818_reg_write(rk818, RK818_DEVCTRL, rtc_ctl);
		if (ret < 0) {
			dev_err(&pdev->dev, "Failed to write RTC control: %d\n", ret);
			return ret;
		}
	}
	*/
	/*start rtc default*/
	ret = rk818_reg_read(rk818, RK818_RTC_CTRL_REG);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to read RTC control: %d\n", ret);
		return ret;
	}
	rtc_ctl = ret & (~BIT_RTC_CTRL_REG_STOP_RTC_M);

	ret = rk818_reg_write(rk818, RK818_RTC_CTRL_REG, rtc_ctl);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to write RTC control: %d\n", ret);
			return ret;
		}
	
	ret = rk818_reg_read(rk818, RK818_RTC_STATUS_REG);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to read RTC status: %d\n", ret);
		return ret;
	}
	rk818_reg_write(rk818,RK818_RTC_STATUS_REG,0xfe);	
	/*set init time*/

	ret = rk818_rtc_readtime(&pdev->dev, &tm);
	if (ret<0)
	{
		dev_err(&pdev->dev, "Failed to read RTC time\n");
		return ret;
	}

	ret = rtc_valid_tm(&tm);
	if (ret) {
	dev_err(&pdev->dev,"invalid date/time and init time\n");
		rk818_rtc_set_time(&pdev->dev, &rk818_tm_def); // 2012-01-01 12:00:00
//		DBG( "set RTC date/time %4d-%02d-%02d(%d) %02d:%02d:%02d\n",1900 + tm_def.tm_year, tm_def.tm_mon + 1, tm_def.tm_mday, tm_def.tm_wday,tm_def.tm_hour, tm_def.tm_min, tm_def.tm_sec);
	}

	device_init_wakeup(&pdev->dev, 1);

	rk818_rtc->rtc = rtc_device_register("rk818", &pdev->dev,
					      &rk818_rtc_ops, THIS_MODULE);
	if (IS_ERR(rk818_rtc->rtc)) {
		ret = PTR_ERR(rk818_rtc->rtc);
		goto err;
	}
	
	per_irq = irq_create_mapping(rk818->irq_domain, RK818_IRQ_RTC_PERIOD);
	alm_irq = irq_create_mapping(rk818->irq_domain, RK818_IRQ_RTC_ALARM);	

	/*request rtc and alarm irq of rk818*/
	ret = devm_request_threaded_irq(rk818->dev,per_irq, NULL, rk818_per_irq,
				   IRQF_TRIGGER_RISING, "RTC period",
				   rk818_rtc);
	if (ret != 0) {
		dev_err(&pdev->dev, "Failed to request periodic IRQ %d: %d\n",
			per_irq, ret);
	}

	ret = devm_request_threaded_irq(rk818->dev,alm_irq, NULL, rk818_alm_irq,
				   IRQF_TRIGGER_RISING, "RTC alarm",
				   rk818_rtc);
	if (ret != 0) {
		dev_err(&pdev->dev, "Failed to request alarm IRQ %d: %d\n",
			alm_irq, ret);
	}

	//for rtc irq test
	/*
	rk818_set_bits(rk818_rtc->rk818, RK818_RTC_STATUS_REG,(0x1<< 6),(0x1 <<6));
	rk818_set_bits(rk818_rtc->rk818, RK818_RTC_INT_REG,0x0c,0x0c);
	rk818_set_bits(rk818_rtc->rk818,RK818_INT_STS_REG1,(0x3 << 5),(0x3 <<5));
	rk818_set_bits(rk818_rtc->rk818, RK818_INT_STS_MSK_REG1,(0x3 <<5),0);
*/

//	enable_irq_wake(alm_irq); // so rk818 alarm irq can wake up system
	rk818_pdev = pdev;
	
	printk("%s:ok\n",__func__);
	
	return 0;

err:
	return ret;
}

static int  rk818_rtc_remove(struct platform_device *pdev)
{
	struct rk818_rtc *rk818_rtc = platform_get_drvdata(pdev);
	int per_irq = rk818_rtc->rk818->irq_base + RK818_IRQ_RTC_PERIOD;
	int alm_irq = rk818_rtc->rk818->irq_base + RK818_IRQ_RTC_ALARM;

	free_irq(alm_irq, rk818_rtc);
	free_irq(per_irq, rk818_rtc);
	rtc_device_unregister(rk818_rtc->rtc);

	return 0;
}

static const struct dev_pm_ops rk818_rtc_pm_ops = {
	.suspend = rk818_rtc_suspend,
	.resume = rk818_rtc_resume,

	.freeze = rk818_rtc_freeze,
	.thaw = rk818_rtc_resume,
	.restore = rk818_rtc_resume,

	.poweroff = rk818_rtc_suspend,
};

static struct platform_driver rk818_rtc_driver = {
	.probe = rk818_rtc_probe,
	.remove = rk818_rtc_remove,
	.driver = {
		.name = "rk818-rtc",
		.pm = &rk818_rtc_pm_ops,
	},
};

static ssize_t rtc_rk818_test_write(struct file *file, 
			const char __user *buf, size_t count, loff_t *offset)
{
	char nr_buf[8];
	int nr = 0, ret;
	struct platform_device *pdev;	
	struct rtc_time tm;
	struct rtc_wkalrm alrm;
	struct rk818_rtc *rk818_rtc;
	
	if(count > 3)
		return -EFAULT;
	ret = copy_from_user(nr_buf, buf, count);
	if(ret < 0)
		return -EFAULT;

	sscanf(nr_buf, "%d", &nr);
	if(nr > 5 || nr < 0)
	{
		printk("%s:data is error\n",__func__);
		return -EFAULT;
	}

	if(!rk818_pdev)
		return -EFAULT;
	else
		pdev = rk818_pdev;

	
	rk818_rtc = dev_get_drvdata(&pdev->dev);
	
	//test rtc time
	if(nr == 0)
	{	
		tm.tm_wday = 6;
		tm.tm_year = 111;
		tm.tm_mon = 0;
		tm.tm_mday = 1;
		tm.tm_hour = 12;
		tm.tm_min = 0;
		tm.tm_sec = 0;
	
		ret = rk818_rtc_set_time(&pdev->dev, &tm); // 2011-01-01 12:00:00
		if (ret)
		{
			dev_err(&pdev->dev, "Failed to set RTC time\n");
			return -EFAULT;
		}

	}
	
	/*set init time*/
	ret = rk818_rtc_readtime(&pdev->dev, &tm);
	if (ret)
		dev_err(&pdev->dev, "Failed to read RTC time\n");
	else
		dev_info(&pdev->dev, "RTC date/time %4d-%02d-%02d(%d) %02d:%02d:%02d\n",
			1900 + tm.tm_year, tm.tm_mon + 1, tm.tm_mday, tm.tm_wday,
			tm.tm_hour, tm.tm_min, tm.tm_sec);
		
	if(!ret)
	printk("%s:ok\n",__func__);
	else
	printk("%s:error\n",__func__);
	

	//test rtc alarm
	if(nr == 2)
	{
		//2000-01-01 00:00:30
		if(tm.tm_sec < 30)
		{
			alrm.time.tm_sec = tm.tm_sec+30;	
			alrm.time.tm_min = tm.tm_min;
		}
		else
		{
			alrm.time.tm_sec = tm.tm_sec-30;
			alrm.time.tm_min = tm.tm_min+1;
		}
		alrm.time.tm_hour = tm.tm_hour;
		alrm.time.tm_mday = tm.tm_mday;
		alrm.time.tm_mon = tm.tm_mon;
		alrm.time.tm_year = tm.tm_year;		
		rk818_rtc_alarm_irq_enable(&pdev->dev, 1);
		rk818_rtc_setalarm(&pdev->dev, &alrm);

		dev_info(&pdev->dev, "Set alarm %4d-%02d-%02d(%d) %02d:%02d:%02d\n",
				1900 + alrm.time.tm_year, alrm.time.tm_mon + 1, alrm.time.tm_mday, alrm.time.tm_wday,
				alrm.time.tm_hour, alrm.time.tm_min, alrm.time.tm_sec);
	}

	
	if(nr == 3)
	{	
		ret = rk818_reg_read(rk818_rtc->rk818, RK818_RTC_STATUS_REG);
		if (ret < 0) {
			printk("%s:Failed to read RTC status: %d\n", __func__, ret);
			return ret;
		}
		printk("%s:ret=0x%x\n",__func__,ret&0xff);

		ret = rk818_reg_write(rk818_rtc->rk818, RK818_RTC_STATUS_REG, ret&0xff);
		if (ret < 0) {
			printk("%s:Failed to read RTC status: %d\n", __func__, ret);
			return ret;
		}
	}

	if(nr == 4)
	rk818_rtc_update_irq_enable(&pdev->dev, 1);

	if(nr == 5)
	rk818_rtc_update_irq_enable(&pdev->dev, 0);
	
	return count;
}

static const struct file_operations rtc_rk818_test_fops = {
	.write = rtc_rk818_test_write,
};

static struct miscdevice rtc_rk818_test_misc = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "rtc_rk818_test",
	.fops = &rtc_rk818_test_fops,
};


static int __init rk818_rtc_init(void)
{
	misc_register(&rtc_rk818_test_misc);
	return platform_driver_register(&rk818_rtc_driver);
}
subsys_initcall_sync(rk818_rtc_init);

static void __exit rk818_rtc_exit(void)
{	
        misc_deregister(&rtc_rk818_test_misc);
	platform_driver_unregister(&rk818_rtc_driver);
}
module_exit(rk818_rtc_exit);

MODULE_DESCRIPTION("RTC driver for the rk818 series PMICs");
MODULE_AUTHOR("ZHANGQING <zhanqging@rock-chips.com>");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:rk818-rtc");
