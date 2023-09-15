// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * rtc-twl.c -- TWL Real Time Clock interface
 *
 * Copyright (C) 2007 MontaVista Software, Inc
 * Author: Alexandre Rusev <source@mvista.com>
 *
 * Based on original TI driver twl4030-rtc.c
 *   Copyright (C) 2006 Texas Instruments, Inc.
 *
 * Based on rtc-omap.c
 *   Copyright (C) 2003 MontaVista Software, Inc.
 *   Author: George G. Davis <gdavis@mvista.com> or <source@mvista.com>
 *   Copyright (C) 2006 David Brownell
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/rtc.h>
#include <linux/bcd.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/of.h>

#include <linux/mfd/twl.h>

enum twl_class {
	TWL_4030 = 0,
	TWL_6030,
};

/*
 * RTC block register offsets (use TWL_MODULE_RTC)
 */
enum {
	REG_SECONDS_REG = 0,
	REG_MINUTES_REG,
	REG_HOURS_REG,
	REG_DAYS_REG,
	REG_MONTHS_REG,
	REG_YEARS_REG,
	REG_WEEKS_REG,

	REG_ALARM_SECONDS_REG,
	REG_ALARM_MINUTES_REG,
	REG_ALARM_HOURS_REG,
	REG_ALARM_DAYS_REG,
	REG_ALARM_MONTHS_REG,
	REG_ALARM_YEARS_REG,

	REG_RTC_CTRL_REG,
	REG_RTC_STATUS_REG,
	REG_RTC_INTERRUPTS_REG,

	REG_RTC_COMP_LSB_REG,
	REG_RTC_COMP_MSB_REG,
};
static const u8 twl4030_rtc_reg_map[] = {
	[REG_SECONDS_REG] = 0x00,
	[REG_MINUTES_REG] = 0x01,
	[REG_HOURS_REG] = 0x02,
	[REG_DAYS_REG] = 0x03,
	[REG_MONTHS_REG] = 0x04,
	[REG_YEARS_REG] = 0x05,
	[REG_WEEKS_REG] = 0x06,

	[REG_ALARM_SECONDS_REG] = 0x07,
	[REG_ALARM_MINUTES_REG] = 0x08,
	[REG_ALARM_HOURS_REG] = 0x09,
	[REG_ALARM_DAYS_REG] = 0x0A,
	[REG_ALARM_MONTHS_REG] = 0x0B,
	[REG_ALARM_YEARS_REG] = 0x0C,

	[REG_RTC_CTRL_REG] = 0x0D,
	[REG_RTC_STATUS_REG] = 0x0E,
	[REG_RTC_INTERRUPTS_REG] = 0x0F,

	[REG_RTC_COMP_LSB_REG] = 0x10,
	[REG_RTC_COMP_MSB_REG] = 0x11,
};
static const u8 twl6030_rtc_reg_map[] = {
	[REG_SECONDS_REG] = 0x00,
	[REG_MINUTES_REG] = 0x01,
	[REG_HOURS_REG] = 0x02,
	[REG_DAYS_REG] = 0x03,
	[REG_MONTHS_REG] = 0x04,
	[REG_YEARS_REG] = 0x05,
	[REG_WEEKS_REG] = 0x06,

	[REG_ALARM_SECONDS_REG] = 0x08,
	[REG_ALARM_MINUTES_REG] = 0x09,
	[REG_ALARM_HOURS_REG] = 0x0A,
	[REG_ALARM_DAYS_REG] = 0x0B,
	[REG_ALARM_MONTHS_REG] = 0x0C,
	[REG_ALARM_YEARS_REG] = 0x0D,

	[REG_RTC_CTRL_REG] = 0x10,
	[REG_RTC_STATUS_REG] = 0x11,
	[REG_RTC_INTERRUPTS_REG] = 0x12,

	[REG_RTC_COMP_LSB_REG] = 0x13,
	[REG_RTC_COMP_MSB_REG] = 0x14,
};

/* RTC_CTRL_REG bitfields */
#define BIT_RTC_CTRL_REG_STOP_RTC_M              0x01
#define BIT_RTC_CTRL_REG_ROUND_30S_M             0x02
#define BIT_RTC_CTRL_REG_AUTO_COMP_M             0x04
#define BIT_RTC_CTRL_REG_MODE_12_24_M            0x08
#define BIT_RTC_CTRL_REG_TEST_MODE_M             0x10
#define BIT_RTC_CTRL_REG_SET_32_COUNTER_M        0x20
#define BIT_RTC_CTRL_REG_GET_TIME_M              0x40
#define BIT_RTC_CTRL_REG_RTC_V_OPT               0x80

/* RTC_STATUS_REG bitfields */
#define BIT_RTC_STATUS_REG_RUN_M                 0x02
#define BIT_RTC_STATUS_REG_1S_EVENT_M            0x04
#define BIT_RTC_STATUS_REG_1M_EVENT_M            0x08
#define BIT_RTC_STATUS_REG_1H_EVENT_M            0x10
#define BIT_RTC_STATUS_REG_1D_EVENT_M            0x20
#define BIT_RTC_STATUS_REG_ALARM_M               0x40
#define BIT_RTC_STATUS_REG_POWER_UP_M            0x80

/* RTC_INTERRUPTS_REG bitfields */
#define BIT_RTC_INTERRUPTS_REG_EVERY_M           0x03
#define BIT_RTC_INTERRUPTS_REG_IT_TIMER_M        0x04
#define BIT_RTC_INTERRUPTS_REG_IT_ALARM_M        0x08


/* REG_SECONDS_REG through REG_YEARS_REG is how many registers? */
#define ALL_TIME_REGS		6

/*----------------------------------------------------------------------*/
struct twl_rtc {
	struct device *dev;
	struct rtc_device *rtc;
	u8 *reg_map;
	/*
	 * Cache the value for timer/alarm interrupts register; this is
	 * only changed by callers holding rtc ops lock (or resume).
	 */
	unsigned char rtc_irq_bits;
	bool wake_enabled;
#ifdef CONFIG_PM_SLEEP
	unsigned char irqstat;
#endif
	enum twl_class class;
};

/*
 * Supports 1 byte read from TWL RTC register.
 */
static int twl_rtc_read_u8(struct twl_rtc *twl_rtc, u8 *data, u8 reg)
{
	int ret;

	ret = twl_i2c_read_u8(TWL_MODULE_RTC, data, (twl_rtc->reg_map[reg]));
	if (ret < 0)
		pr_err("Could not read TWL register %X - error %d\n", reg, ret);
	return ret;
}

/*
 * Supports 1 byte write to TWL RTC registers.
 */
static int twl_rtc_write_u8(struct twl_rtc *twl_rtc, u8 data, u8 reg)
{
	int ret;

	ret = twl_i2c_write_u8(TWL_MODULE_RTC, data, (twl_rtc->reg_map[reg]));
	if (ret < 0)
		pr_err("Could not write TWL register %X - error %d\n",
		       reg, ret);
	return ret;
}

/*
 * Enable 1/second update and/or alarm interrupts.
 */
static int set_rtc_irq_bit(struct twl_rtc *twl_rtc, unsigned char bit)
{
	unsigned char val;
	int ret;

	/* if the bit is set, return from here */
	if (twl_rtc->rtc_irq_bits & bit)
		return 0;

	val = twl_rtc->rtc_irq_bits | bit;
	val &= ~BIT_RTC_INTERRUPTS_REG_EVERY_M;
	ret = twl_rtc_write_u8(twl_rtc, val, REG_RTC_INTERRUPTS_REG);
	if (ret == 0)
		twl_rtc->rtc_irq_bits = val;

	return ret;
}

/*
 * Disable update and/or alarm interrupts.
 */
static int mask_rtc_irq_bit(struct twl_rtc *twl_rtc, unsigned char bit)
{
	unsigned char val;
	int ret;

	/* if the bit is clear, return from here */
	if (!(twl_rtc->rtc_irq_bits & bit))
		return 0;

	val = twl_rtc->rtc_irq_bits & ~bit;
	ret = twl_rtc_write_u8(twl_rtc, val, REG_RTC_INTERRUPTS_REG);
	if (ret == 0)
		twl_rtc->rtc_irq_bits = val;

	return ret;
}

static int twl_rtc_alarm_irq_enable(struct device *dev, unsigned enabled)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct twl_rtc *twl_rtc = dev_get_drvdata(dev);
	int irq = platform_get_irq(pdev, 0);
	int ret;

	if (enabled) {
		ret = set_rtc_irq_bit(twl_rtc,
				      BIT_RTC_INTERRUPTS_REG_IT_ALARM_M);
		if (device_can_wakeup(dev) && !twl_rtc->wake_enabled) {
			enable_irq_wake(irq);
			twl_rtc->wake_enabled = true;
		}
	} else {
		ret = mask_rtc_irq_bit(twl_rtc,
				       BIT_RTC_INTERRUPTS_REG_IT_ALARM_M);
		if (twl_rtc->wake_enabled) {
			disable_irq_wake(irq);
			twl_rtc->wake_enabled = false;
		}
	}

	return ret;
}

/*
 * Gets current TWL RTC time and date parameters.
 *
 * The RTC's time/alarm representation is not what gmtime(3) requires
 * Linux to use:
 *
 *  - Months are 1..12 vs Linux 0-11
 *  - Years are 0..99 vs Linux 1900..N (we assume 21st century)
 */
static int twl_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct twl_rtc *twl_rtc = dev_get_drvdata(dev);
	unsigned char rtc_data[ALL_TIME_REGS];
	int ret;
	u8 save_control;
	u8 rtc_control;

	ret = twl_rtc_read_u8(twl_rtc, &save_control, REG_RTC_CTRL_REG);
	if (ret < 0) {
		dev_err(dev, "%s: reading CTRL_REG, error %d\n", __func__, ret);
		return ret;
	}
	/* for twl6030/32 make sure BIT_RTC_CTRL_REG_GET_TIME_M is clear */
	if (twl_rtc->class == TWL_6030) {
		if (save_control & BIT_RTC_CTRL_REG_GET_TIME_M) {
			save_control &= ~BIT_RTC_CTRL_REG_GET_TIME_M;
			ret = twl_rtc_write_u8(twl_rtc, save_control,
					       REG_RTC_CTRL_REG);
			if (ret < 0) {
				dev_err(dev, "%s clr GET_TIME, error %d\n",
					__func__, ret);
				return ret;
			}
		}
	}

	/* Copy RTC counting registers to static registers or latches */
	rtc_control = save_control | BIT_RTC_CTRL_REG_GET_TIME_M;

	/* for twl6030/32 enable read access to static shadowed registers */
	if (twl_rtc->class == TWL_6030)
		rtc_control |= BIT_RTC_CTRL_REG_RTC_V_OPT;

	ret = twl_rtc_write_u8(twl_rtc, rtc_control, REG_RTC_CTRL_REG);
	if (ret < 0) {
		dev_err(dev, "%s: writing CTRL_REG, error %d\n", __func__, ret);
		return ret;
	}

	ret = twl_i2c_read(TWL_MODULE_RTC, rtc_data,
			(twl_rtc->reg_map[REG_SECONDS_REG]), ALL_TIME_REGS);

	if (ret < 0) {
		dev_err(dev, "%s: reading data, error %d\n", __func__, ret);
		return ret;
	}

	/* for twl6030 restore original state of rtc control register */
	if (twl_rtc->class == TWL_6030) {
		ret = twl_rtc_write_u8(twl_rtc, save_control, REG_RTC_CTRL_REG);
		if (ret < 0) {
			dev_err(dev, "%s: restore CTRL_REG, error %d\n",
				__func__, ret);
			return ret;
		}
	}

	tm->tm_sec = bcd2bin(rtc_data[0]);
	tm->tm_min = bcd2bin(rtc_data[1]);
	tm->tm_hour = bcd2bin(rtc_data[2]);
	tm->tm_mday = bcd2bin(rtc_data[3]);
	tm->tm_mon = bcd2bin(rtc_data[4]) - 1;
	tm->tm_year = bcd2bin(rtc_data[5]) + 100;

	return ret;
}

static int twl_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct twl_rtc *twl_rtc = dev_get_drvdata(dev);
	unsigned char save_control;
	unsigned char rtc_data[ALL_TIME_REGS];
	int ret;

	rtc_data[0] = bin2bcd(tm->tm_sec);
	rtc_data[1] = bin2bcd(tm->tm_min);
	rtc_data[2] = bin2bcd(tm->tm_hour);
	rtc_data[3] = bin2bcd(tm->tm_mday);
	rtc_data[4] = bin2bcd(tm->tm_mon + 1);
	rtc_data[5] = bin2bcd(tm->tm_year - 100);

	/* Stop RTC while updating the TC registers */
	ret = twl_rtc_read_u8(twl_rtc, &save_control, REG_RTC_CTRL_REG);
	if (ret < 0)
		goto out;

	save_control &= ~BIT_RTC_CTRL_REG_STOP_RTC_M;
	ret = twl_rtc_write_u8(twl_rtc, save_control, REG_RTC_CTRL_REG);
	if (ret < 0)
		goto out;

	/* update all the time registers in one shot */
	ret = twl_i2c_write(TWL_MODULE_RTC, rtc_data,
		(twl_rtc->reg_map[REG_SECONDS_REG]), ALL_TIME_REGS);
	if (ret < 0) {
		dev_err(dev, "rtc_set_time error %d\n", ret);
		goto out;
	}

	/* Start back RTC */
	save_control |= BIT_RTC_CTRL_REG_STOP_RTC_M;
	ret = twl_rtc_write_u8(twl_rtc, save_control, REG_RTC_CTRL_REG);

out:
	return ret;
}

/*
 * Gets current TWL RTC alarm time.
 */
static int twl_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	struct twl_rtc *twl_rtc = dev_get_drvdata(dev);
	unsigned char rtc_data[ALL_TIME_REGS];
	int ret;

	ret = twl_i2c_read(TWL_MODULE_RTC, rtc_data,
			twl_rtc->reg_map[REG_ALARM_SECONDS_REG], ALL_TIME_REGS);
	if (ret < 0) {
		dev_err(dev, "rtc_read_alarm error %d\n", ret);
		return ret;
	}

	/* some of these fields may be wildcard/"match all" */
	alm->time.tm_sec = bcd2bin(rtc_data[0]);
	alm->time.tm_min = bcd2bin(rtc_data[1]);
	alm->time.tm_hour = bcd2bin(rtc_data[2]);
	alm->time.tm_mday = bcd2bin(rtc_data[3]);
	alm->time.tm_mon = bcd2bin(rtc_data[4]) - 1;
	alm->time.tm_year = bcd2bin(rtc_data[5]) + 100;

	/* report cached alarm enable state */
	if (twl_rtc->rtc_irq_bits & BIT_RTC_INTERRUPTS_REG_IT_ALARM_M)
		alm->enabled = 1;

	return ret;
}

static int twl_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alm)
{
	struct twl_rtc *twl_rtc = dev_get_drvdata(dev);

	unsigned char alarm_data[ALL_TIME_REGS];
	int ret;

	ret = twl_rtc_alarm_irq_enable(dev, 0);
	if (ret)
		goto out;

	alarm_data[0] = bin2bcd(alm->time.tm_sec);
	alarm_data[1] = bin2bcd(alm->time.tm_min);
	alarm_data[2] = bin2bcd(alm->time.tm_hour);
	alarm_data[3] = bin2bcd(alm->time.tm_mday);
	alarm_data[4] = bin2bcd(alm->time.tm_mon + 1);
	alarm_data[5] = bin2bcd(alm->time.tm_year - 100);

	/* update all the alarm registers in one shot */
	ret = twl_i2c_write(TWL_MODULE_RTC, alarm_data,
			twl_rtc->reg_map[REG_ALARM_SECONDS_REG], ALL_TIME_REGS);
	if (ret) {
		dev_err(dev, "rtc_set_alarm error %d\n", ret);
		goto out;
	}

	if (alm->enabled)
		ret = twl_rtc_alarm_irq_enable(dev, 1);
out:
	return ret;
}

static irqreturn_t twl_rtc_interrupt(int irq, void *data)
{
	struct twl_rtc *twl_rtc = data;
	unsigned long events;
	int ret = IRQ_NONE;
	int res;
	u8 rd_reg;

	res = twl_rtc_read_u8(twl_rtc, &rd_reg, REG_RTC_STATUS_REG);
	if (res)
		goto out;
	/*
	 * Figure out source of interrupt: ALARM or TIMER in RTC_STATUS_REG.
	 * only one (ALARM or RTC) interrupt source may be enabled
	 * at time, we also could check our results
	 * by reading RTS_INTERRUPTS_REGISTER[IT_TIMER,IT_ALARM]
	 */
	if (rd_reg & BIT_RTC_STATUS_REG_ALARM_M)
		events = RTC_IRQF | RTC_AF;
	else
		events = RTC_IRQF | RTC_PF;

	res = twl_rtc_write_u8(twl_rtc, BIT_RTC_STATUS_REG_ALARM_M,
			       REG_RTC_STATUS_REG);
	if (res)
		goto out;

	if (twl_rtc->class == TWL_4030) {
		/* Clear on Read enabled. RTC_IT bit of TWL4030_INT_PWR_ISR1
		 * needs 2 reads to clear the interrupt. One read is done in
		 * do_twl_pwrirq(). Doing the second read, to clear
		 * the bit.
		 *
		 * FIXME the reason PWR_ISR1 needs an extra read is that
		 * RTC_IF retriggered until we cleared REG_ALARM_M above.
		 * But re-reading like this is a bad hack; by doing so we
		 * risk wrongly clearing status for some other IRQ (losing
		 * the interrupt).  Be smarter about handling RTC_UF ...
		 */
		res = twl_i2c_read_u8(TWL4030_MODULE_INT,
			&rd_reg, TWL4030_INT_PWR_ISR1);
		if (res)
			goto out;
	}

	/* Notify RTC core on event */
	rtc_update_irq(twl_rtc->rtc, 1, events);

	ret = IRQ_HANDLED;
out:
	return ret;
}

static const struct rtc_class_ops twl_rtc_ops = {
	.read_time	= twl_rtc_read_time,
	.set_time	= twl_rtc_set_time,
	.read_alarm	= twl_rtc_read_alarm,
	.set_alarm	= twl_rtc_set_alarm,
	.alarm_irq_enable = twl_rtc_alarm_irq_enable,
};

static int twl_nvram_read(void *priv, unsigned int offset, void *val,
			  size_t bytes)
{
	return twl_i2c_read((long)priv, val, offset, bytes);
}

static int twl_nvram_write(void *priv, unsigned int offset, void *val,
			   size_t bytes)
{
	return twl_i2c_write((long)priv, val, offset, bytes);
}

/*----------------------------------------------------------------------*/

static int twl_rtc_probe(struct platform_device *pdev)
{
	struct twl_rtc *twl_rtc;
	struct nvmem_config nvmem_cfg;
	struct device_node *np = pdev->dev.of_node;
	int ret = -EINVAL;
	int irq = platform_get_irq(pdev, 0);
	u8 rd_reg;

	if (!np) {
		dev_err(&pdev->dev, "no DT info\n");
		return -EINVAL;
	}

	if (irq <= 0)
		return ret;

	twl_rtc = devm_kzalloc(&pdev->dev, sizeof(*twl_rtc), GFP_KERNEL);
	if (!twl_rtc)
		return -ENOMEM;

	if (twl_class_is_4030()) {
		twl_rtc->class = TWL_4030;
		twl_rtc->reg_map = (u8 *)twl4030_rtc_reg_map;
	} else if (twl_class_is_6030()) {
		twl_rtc->class = TWL_6030;
		twl_rtc->reg_map = (u8 *)twl6030_rtc_reg_map;
	} else {
		dev_err(&pdev->dev, "TWL Class not supported.\n");
		return -EINVAL;
	}

	ret = twl_rtc_read_u8(twl_rtc, &rd_reg, REG_RTC_STATUS_REG);
	if (ret < 0)
		return ret;

	if (rd_reg & BIT_RTC_STATUS_REG_POWER_UP_M)
		dev_warn(&pdev->dev, "Power up reset detected.\n");

	if (rd_reg & BIT_RTC_STATUS_REG_ALARM_M)
		dev_warn(&pdev->dev, "Pending Alarm interrupt detected.\n");

	/* Clear RTC Power up reset and pending alarm interrupts */
	ret = twl_rtc_write_u8(twl_rtc, rd_reg, REG_RTC_STATUS_REG);
	if (ret < 0)
		return ret;

	if (twl_rtc->class == TWL_6030) {
		twl6030_interrupt_unmask(TWL6030_RTC_INT_MASK,
			REG_INT_MSK_LINE_A);
		twl6030_interrupt_unmask(TWL6030_RTC_INT_MASK,
			REG_INT_MSK_STS_A);
	}

	ret = twl_rtc_write_u8(twl_rtc, BIT_RTC_CTRL_REG_STOP_RTC_M,
			       REG_RTC_CTRL_REG);
	if (ret < 0)
		return ret;

	/* ensure interrupts are disabled, bootloaders can be strange */
	ret = twl_rtc_write_u8(twl_rtc, 0, REG_RTC_INTERRUPTS_REG);
	if (ret < 0)
		dev_warn(&pdev->dev, "unable to disable interrupt\n");

	/* init cached IRQ enable bits */
	ret = twl_rtc_read_u8(twl_rtc, &twl_rtc->rtc_irq_bits,
			      REG_RTC_INTERRUPTS_REG);
	if (ret < 0)
		return ret;

	platform_set_drvdata(pdev, twl_rtc);
	device_init_wakeup(&pdev->dev, 1);

	twl_rtc->rtc = devm_rtc_device_register(&pdev->dev, pdev->name,
					&twl_rtc_ops, THIS_MODULE);
	if (IS_ERR(twl_rtc->rtc))
		return PTR_ERR(twl_rtc->rtc);

	ret = devm_request_threaded_irq(&pdev->dev, irq, NULL,
					twl_rtc_interrupt,
					IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					dev_name(&twl_rtc->rtc->dev), twl_rtc);
	if (ret < 0) {
		dev_err(&pdev->dev, "IRQ is not free.\n");
		return ret;
	}

	memset(&nvmem_cfg, 0, sizeof(nvmem_cfg));
	nvmem_cfg.name = "twl-secured-";
	nvmem_cfg.type = NVMEM_TYPE_BATTERY_BACKED;
	nvmem_cfg.reg_read = twl_nvram_read,
	nvmem_cfg.reg_write = twl_nvram_write,
	nvmem_cfg.word_size = 1;
	nvmem_cfg.stride = 1;
	if (twl_class_is_4030()) {
		/* 20 bytes SECURED_REG area */
		nvmem_cfg.size = 20;
		nvmem_cfg.priv = (void *)TWL_MODULE_SECURED_REG;
		devm_rtc_nvmem_register(twl_rtc->rtc, &nvmem_cfg);
		/* 8 bytes BACKUP area */
		nvmem_cfg.name = "twl-backup-";
		nvmem_cfg.size = 8;
		nvmem_cfg.priv = (void *)TWL4030_MODULE_BACKUP;
		devm_rtc_nvmem_register(twl_rtc->rtc, &nvmem_cfg);
	} else {
		/* 8 bytes SECURED_REG area */
		nvmem_cfg.size = 8;
		nvmem_cfg.priv = (void *)TWL_MODULE_SECURED_REG;
		devm_rtc_nvmem_register(twl_rtc->rtc, &nvmem_cfg);
	}

	return 0;
}

/*
 * Disable all TWL RTC module interrupts.
 * Sets status flag to free.
 */
static void twl_rtc_remove(struct platform_device *pdev)
{
	struct twl_rtc *twl_rtc = platform_get_drvdata(pdev);

	/* leave rtc running, but disable irqs */
	mask_rtc_irq_bit(twl_rtc, BIT_RTC_INTERRUPTS_REG_IT_ALARM_M);
	mask_rtc_irq_bit(twl_rtc, BIT_RTC_INTERRUPTS_REG_IT_TIMER_M);
	if (twl_rtc->class == TWL_6030) {
		twl6030_interrupt_mask(TWL6030_RTC_INT_MASK,
			REG_INT_MSK_LINE_A);
		twl6030_interrupt_mask(TWL6030_RTC_INT_MASK,
			REG_INT_MSK_STS_A);
	}
}

static void twl_rtc_shutdown(struct platform_device *pdev)
{
	struct twl_rtc *twl_rtc = platform_get_drvdata(pdev);

	/* mask timer interrupts, but leave alarm interrupts on to enable
	   power-on when alarm is triggered */
	mask_rtc_irq_bit(twl_rtc, BIT_RTC_INTERRUPTS_REG_IT_TIMER_M);
}

#ifdef CONFIG_PM_SLEEP
static int twl_rtc_suspend(struct device *dev)
{
	struct twl_rtc *twl_rtc = dev_get_drvdata(dev);

	twl_rtc->irqstat = twl_rtc->rtc_irq_bits;

	mask_rtc_irq_bit(twl_rtc, BIT_RTC_INTERRUPTS_REG_IT_TIMER_M);
	return 0;
}

static int twl_rtc_resume(struct device *dev)
{
	struct twl_rtc *twl_rtc = dev_get_drvdata(dev);

	set_rtc_irq_bit(twl_rtc, twl_rtc->irqstat);
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(twl_rtc_pm_ops, twl_rtc_suspend, twl_rtc_resume);

static const struct of_device_id twl_rtc_of_match[] = {
	{.compatible = "ti,twl4030-rtc", },
	{ },
};
MODULE_DEVICE_TABLE(of, twl_rtc_of_match);

static struct platform_driver twl4030rtc_driver = {
	.probe		= twl_rtc_probe,
	.remove_new	= twl_rtc_remove,
	.shutdown	= twl_rtc_shutdown,
	.driver		= {
		.name		= "twl_rtc",
		.pm		= &twl_rtc_pm_ops,
		.of_match_table = twl_rtc_of_match,
	},
};

module_platform_driver(twl4030rtc_driver);

MODULE_AUTHOR("Texas Instruments, MontaVista Software");
MODULE_LICENSE("GPL");
