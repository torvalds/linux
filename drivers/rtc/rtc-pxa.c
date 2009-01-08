/*
 * Real Time Clock interface for XScale PXA27x and PXA3xx
 *
 * Copyright (C) 2008 Robert Jarzmik
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/rtc.h>
#include <linux/seq_file.h>
#include <linux/interrupt.h>
#include <linux/io.h>

#define TIMER_FREQ		CLOCK_TICK_RATE
#define RTC_DEF_DIVIDER		(32768 - 1)
#define RTC_DEF_TRIM		0
#define MAXFREQ_PERIODIC	1000

/*
 * PXA Registers and bits definitions
 */
#define RTSR_PICE	(1 << 15)	/* Periodic interrupt count enable */
#define RTSR_PIALE	(1 << 14)	/* Periodic interrupt Alarm enable */
#define RTSR_PIAL	(1 << 13)	/* Periodic interrupt detected */
#define RTSR_SWALE2	(1 << 11)	/* RTC stopwatch alarm2 enable */
#define RTSR_SWAL2	(1 << 10)	/* RTC stopwatch alarm2 detected */
#define RTSR_SWALE1	(1 << 9)	/* RTC stopwatch alarm1 enable */
#define RTSR_SWAL1	(1 << 8)	/* RTC stopwatch alarm1 detected */
#define RTSR_RDALE2	(1 << 7)	/* RTC alarm2 enable */
#define RTSR_RDAL2	(1 << 6)	/* RTC alarm2 detected */
#define RTSR_RDALE1	(1 << 5)	/* RTC alarm1 enable */
#define RTSR_RDAL1	(1 << 4)	/* RTC alarm1 detected */
#define RTSR_HZE	(1 << 3)	/* HZ interrupt enable */
#define RTSR_ALE	(1 << 2)	/* RTC alarm interrupt enable */
#define RTSR_HZ		(1 << 1)	/* HZ rising-edge detected */
#define RTSR_AL		(1 << 0)	/* RTC alarm detected */
#define RTSR_TRIG_MASK	(RTSR_AL | RTSR_HZ | RTSR_RDAL1 | RTSR_RDAL2\
			 | RTSR_SWAL1 | RTSR_SWAL2)
#define RYxR_YEAR_S	9
#define RYxR_YEAR_MASK	(0xfff << RYxR_YEAR_S)
#define RYxR_MONTH_S	5
#define RYxR_MONTH_MASK	(0xf << RYxR_MONTH_S)
#define RYxR_DAY_MASK	0x1f
#define RDxR_HOUR_S	12
#define RDxR_HOUR_MASK	(0x1f << RDxR_HOUR_S)
#define RDxR_MIN_S	6
#define RDxR_MIN_MASK	(0x3f << RDxR_MIN_S)
#define RDxR_SEC_MASK	0x3f

#define RTSR		0x08
#define RTTR		0x0c
#define RDCR		0x10
#define RYCR		0x14
#define RDAR1		0x18
#define RYAR1		0x1c
#define RTCPICR		0x34
#define PIAR		0x38

#define rtc_readl(pxa_rtc, reg)	\
	__raw_readl((pxa_rtc)->base + (reg))
#define rtc_writel(pxa_rtc, reg, value)	\
	__raw_writel((value), (pxa_rtc)->base + (reg))

struct pxa_rtc {
	struct resource	*ress;
	void __iomem		*base;
	int			irq_1Hz;
	int			irq_Alrm;
	struct rtc_device	*rtc;
	spinlock_t		lock;		/* Protects this structure */
	struct rtc_time		rtc_alarm;
};

static u32 ryxr_calc(struct rtc_time *tm)
{
	return ((tm->tm_year + 1900) << RYxR_YEAR_S)
		| ((tm->tm_mon + 1) << RYxR_MONTH_S)
		| tm->tm_mday;
}

static u32 rdxr_calc(struct rtc_time *tm)
{
	return (tm->tm_hour << RDxR_HOUR_S) | (tm->tm_min << RDxR_MIN_S)
		| tm->tm_sec;
}

static void tm_calc(u32 rycr, u32 rdcr, struct rtc_time *tm)
{
	tm->tm_year = ((rycr & RYxR_YEAR_MASK) >> RYxR_YEAR_S) - 1900;
	tm->tm_mon = (((rycr & RYxR_MONTH_MASK) >> RYxR_MONTH_S)) - 1;
	tm->tm_mday = (rycr & RYxR_DAY_MASK);
	tm->tm_hour = (rdcr & RDxR_HOUR_MASK) >> RDxR_HOUR_S;
	tm->tm_min = (rdcr & RDxR_MIN_MASK) >> RDxR_MIN_S;
	tm->tm_sec = rdcr & RDxR_SEC_MASK;
}

static void rtsr_clear_bits(struct pxa_rtc *pxa_rtc, u32 mask)
{
	u32 rtsr;

	rtsr = rtc_readl(pxa_rtc, RTSR);
	rtsr &= ~RTSR_TRIG_MASK;
	rtsr &= ~mask;
	rtc_writel(pxa_rtc, RTSR, rtsr);
}

static void rtsr_set_bits(struct pxa_rtc *pxa_rtc, u32 mask)
{
	u32 rtsr;

	rtsr = rtc_readl(pxa_rtc, RTSR);
	rtsr &= ~RTSR_TRIG_MASK;
	rtsr |= mask;
	rtc_writel(pxa_rtc, RTSR, rtsr);
}

static irqreturn_t pxa_rtc_irq(int irq, void *dev_id)
{
	struct platform_device *pdev = to_platform_device(dev_id);
	struct pxa_rtc *pxa_rtc = platform_get_drvdata(pdev);
	u32 rtsr;
	unsigned long events = 0;

	spin_lock(&pxa_rtc->lock);

	/* clear interrupt sources */
	rtsr = rtc_readl(pxa_rtc, RTSR);
	rtc_writel(pxa_rtc, RTSR, rtsr);

	/* temporary disable rtc interrupts */
	rtsr_clear_bits(pxa_rtc, RTSR_RDALE1 | RTSR_PIALE | RTSR_HZE);

	/* clear alarm interrupt if it has occurred */
	if (rtsr & RTSR_RDAL1)
		rtsr &= ~RTSR_RDALE1;

	/* update irq data & counter */
	if (rtsr & RTSR_RDAL1)
		events |= RTC_AF | RTC_IRQF;
	if (rtsr & RTSR_HZ)
		events |= RTC_UF | RTC_IRQF;
	if (rtsr & RTSR_PIAL)
		events |= RTC_PF | RTC_IRQF;

	rtc_update_irq(pxa_rtc->rtc, 1, events);

	/* enable back rtc interrupts */
	rtc_writel(pxa_rtc, RTSR, rtsr & ~RTSR_TRIG_MASK);

	spin_unlock(&pxa_rtc->lock);
	return IRQ_HANDLED;
}

static int pxa_rtc_open(struct device *dev)
{
	struct pxa_rtc *pxa_rtc = dev_get_drvdata(dev);
	int ret;

	ret = request_irq(pxa_rtc->irq_1Hz, pxa_rtc_irq, IRQF_DISABLED,
			  "rtc 1Hz", dev);
	if (ret < 0) {
		dev_err(dev, "can't get irq %i, err %d\n", pxa_rtc->irq_1Hz,
			ret);
		goto err_irq_1Hz;
	}
	ret = request_irq(pxa_rtc->irq_Alrm, pxa_rtc_irq, IRQF_DISABLED,
			  "rtc Alrm", dev);
	if (ret < 0) {
		dev_err(dev, "can't get irq %i, err %d\n", pxa_rtc->irq_Alrm,
			ret);
		goto err_irq_Alrm;
	}

	return 0;

err_irq_Alrm:
	free_irq(pxa_rtc->irq_1Hz, dev);
err_irq_1Hz:
	return ret;
}

static void pxa_rtc_release(struct device *dev)
{
	struct pxa_rtc *pxa_rtc = dev_get_drvdata(dev);

	spin_lock_irq(&pxa_rtc->lock);
	rtsr_clear_bits(pxa_rtc, RTSR_PIALE | RTSR_RDALE1 | RTSR_HZE);
	spin_unlock_irq(&pxa_rtc->lock);

	free_irq(pxa_rtc->irq_Alrm, dev);
	free_irq(pxa_rtc->irq_1Hz, dev);
}

static int pxa_periodic_irq_set_freq(struct device *dev, int freq)
{
	struct pxa_rtc *pxa_rtc = dev_get_drvdata(dev);
	int period_ms;

	if (freq < 1 || freq > MAXFREQ_PERIODIC)
		return -EINVAL;

	period_ms = 1000 / freq;
	rtc_writel(pxa_rtc, PIAR, period_ms);

	return 0;
}

static int pxa_periodic_irq_set_state(struct device *dev, int enabled)
{
	struct pxa_rtc *pxa_rtc = dev_get_drvdata(dev);

	if (enabled)
		rtsr_set_bits(pxa_rtc, RTSR_PIALE | RTSR_PICE);
	else
		rtsr_clear_bits(pxa_rtc, RTSR_PIALE | RTSR_PICE);

	return 0;
}

static int pxa_rtc_ioctl(struct device *dev, unsigned int cmd,
		unsigned long arg)
{
	struct pxa_rtc *pxa_rtc = dev_get_drvdata(dev);
	int ret = 0;

	spin_lock_irq(&pxa_rtc->lock);
	switch (cmd) {
	case RTC_AIE_OFF:
		rtsr_clear_bits(pxa_rtc, RTSR_RDALE1);
		break;
	case RTC_AIE_ON:
		rtsr_set_bits(pxa_rtc, RTSR_RDALE1);
		break;
	case RTC_UIE_OFF:
		rtsr_clear_bits(pxa_rtc, RTSR_HZE);
		break;
	case RTC_UIE_ON:
		rtsr_set_bits(pxa_rtc, RTSR_HZE);
		break;
	default:
		ret = -ENOIOCTLCMD;
	}

	spin_unlock_irq(&pxa_rtc->lock);
	return ret;
}

static int pxa_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct pxa_rtc *pxa_rtc = dev_get_drvdata(dev);
	u32 rycr, rdcr;

	rycr = rtc_readl(pxa_rtc, RYCR);
	rdcr = rtc_readl(pxa_rtc, RDCR);

	tm_calc(rycr, rdcr, tm);
	return 0;
}

static int pxa_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct pxa_rtc *pxa_rtc = dev_get_drvdata(dev);

	rtc_writel(pxa_rtc, RYCR, ryxr_calc(tm));
	rtc_writel(pxa_rtc, RDCR, rdxr_calc(tm));

	return 0;
}

static int pxa_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct pxa_rtc *pxa_rtc = dev_get_drvdata(dev);
	u32 rtsr, ryar, rdar;

	ryar = rtc_readl(pxa_rtc, RYAR1);
	rdar = rtc_readl(pxa_rtc, RDAR1);
	tm_calc(ryar, rdar, &alrm->time);

	rtsr = rtc_readl(pxa_rtc, RTSR);
	alrm->enabled = (rtsr & RTSR_RDALE1) ? 1 : 0;
	alrm->pending = (rtsr & RTSR_RDAL1) ? 1 : 0;
	return 0;
}

static int pxa_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct pxa_rtc *pxa_rtc = dev_get_drvdata(dev);
	u32 rtsr;

	spin_lock_irq(&pxa_rtc->lock);

	rtc_writel(pxa_rtc, RYAR1, ryxr_calc(&alrm->time));
	rtc_writel(pxa_rtc, RDAR1, rdxr_calc(&alrm->time));

	rtsr = rtc_readl(pxa_rtc, RTSR);
	if (alrm->enabled)
		rtsr |= RTSR_RDALE1;
	else
		rtsr &= ~RTSR_RDALE1;
	rtc_writel(pxa_rtc, RTSR, rtsr);

	spin_unlock_irq(&pxa_rtc->lock);

	return 0;
}

static int pxa_rtc_proc(struct device *dev, struct seq_file *seq)
{
	struct pxa_rtc *pxa_rtc = dev_get_drvdata(dev);

	seq_printf(seq, "trim/divider\t: 0x%08x\n", rtc_readl(pxa_rtc, RTTR));
	seq_printf(seq, "update_IRQ\t: %s\n",
		   (rtc_readl(pxa_rtc, RTSR) & RTSR_HZE) ? "yes" : "no");
	seq_printf(seq, "periodic_IRQ\t: %s\n",
		   (rtc_readl(pxa_rtc, RTSR) & RTSR_PIALE) ? "yes" : "no");
	seq_printf(seq, "periodic_freq\t: %u\n", rtc_readl(pxa_rtc, PIAR));

	return 0;
}

static const struct rtc_class_ops pxa_rtc_ops = {
	.open = pxa_rtc_open,
	.release = pxa_rtc_release,
	.ioctl = pxa_rtc_ioctl,
	.read_time = pxa_rtc_read_time,
	.set_time = pxa_rtc_set_time,
	.read_alarm = pxa_rtc_read_alarm,
	.set_alarm = pxa_rtc_set_alarm,
	.proc = pxa_rtc_proc,
	.irq_set_state = pxa_periodic_irq_set_state,
	.irq_set_freq = pxa_periodic_irq_set_freq,
};

static int __init pxa_rtc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pxa_rtc *pxa_rtc;
	int ret;
	u32 rttr;

	pxa_rtc = kzalloc(sizeof(struct pxa_rtc), GFP_KERNEL);
	if (!pxa_rtc)
		return -ENOMEM;

	spin_lock_init(&pxa_rtc->lock);
	platform_set_drvdata(pdev, pxa_rtc);

	ret = -ENXIO;
	pxa_rtc->ress = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!pxa_rtc->ress) {
		dev_err(dev, "No I/O memory resource defined\n");
		goto err_ress;
	}

	pxa_rtc->irq_1Hz = platform_get_irq(pdev, 0);
	if (pxa_rtc->irq_1Hz < 0) {
		dev_err(dev, "No 1Hz IRQ resource defined\n");
		goto err_ress;
	}
	pxa_rtc->irq_Alrm = platform_get_irq(pdev, 1);
	if (pxa_rtc->irq_Alrm < 0) {
		dev_err(dev, "No alarm IRQ resource defined\n");
		goto err_ress;
	}

	ret = -ENOMEM;
	pxa_rtc->base = ioremap(pxa_rtc->ress->start,
				resource_size(pxa_rtc->ress));
	if (!pxa_rtc->base) {
		dev_err(&pdev->dev, "Unable to map pxa RTC I/O memory\n");
		goto err_map;
	}

	/*
	 * If the clock divider is uninitialized then reset it to the
	 * default value to get the 1Hz clock.
	 */
	if (rtc_readl(pxa_rtc, RTTR) == 0) {
		rttr = RTC_DEF_DIVIDER + (RTC_DEF_TRIM << 16);
		rtc_writel(pxa_rtc, RTTR, rttr);
		dev_warn(dev, "warning: initializing default clock"
			 " divider/trim value\n");
	}

	rtsr_clear_bits(pxa_rtc, RTSR_PIALE | RTSR_RDALE1 | RTSR_HZE);

	pxa_rtc->rtc = rtc_device_register("pxa-rtc", &pdev->dev, &pxa_rtc_ops,
					   THIS_MODULE);
	ret = PTR_ERR(pxa_rtc->rtc);
	if (IS_ERR(pxa_rtc->rtc)) {
		dev_err(dev, "Failed to register RTC device -> %d\n", ret);
		goto err_rtc_reg;
	}

	device_init_wakeup(dev, 1);

	return 0;

err_rtc_reg:
	 iounmap(pxa_rtc->base);
err_ress:
err_map:
	kfree(pxa_rtc);
	return ret;
}

static int __exit pxa_rtc_remove(struct platform_device *pdev)
{
	struct pxa_rtc *pxa_rtc = platform_get_drvdata(pdev);

	rtc_device_unregister(pxa_rtc->rtc);

	spin_lock_irq(&pxa_rtc->lock);
	iounmap(pxa_rtc->base);
	spin_unlock_irq(&pxa_rtc->lock);

	kfree(pxa_rtc);

	return 0;
}

#ifdef CONFIG_PM
static int pxa_rtc_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct pxa_rtc *pxa_rtc = platform_get_drvdata(pdev);

	if (device_may_wakeup(&pdev->dev))
		enable_irq_wake(pxa_rtc->irq_Alrm);
	return 0;
}

static int pxa_rtc_resume(struct platform_device *pdev)
{
	struct pxa_rtc *pxa_rtc = platform_get_drvdata(pdev);

	if (device_may_wakeup(&pdev->dev))
		disable_irq_wake(pxa_rtc->irq_Alrm);
	return 0;
}
#else
#define pxa_rtc_suspend	NULL
#define pxa_rtc_resume	NULL
#endif

static struct platform_driver pxa_rtc_driver = {
	.remove		= __exit_p(pxa_rtc_remove),
	.suspend	= pxa_rtc_suspend,
	.resume		= pxa_rtc_resume,
	.driver		= {
		.name		= "pxa-rtc",
	},
};

static int __init pxa_rtc_init(void)
{
	if (cpu_is_pxa27x() || cpu_is_pxa3xx())
		return platform_driver_probe(&pxa_rtc_driver, pxa_rtc_probe);

	return -ENODEV;
}

static void __exit pxa_rtc_exit(void)
{
	platform_driver_unregister(&pxa_rtc_driver);
}

module_init(pxa_rtc_init);
module_exit(pxa_rtc_exit);

MODULE_AUTHOR("Robert Jarzmik");
MODULE_DESCRIPTION("PXA27x/PXA3xx Realtime Clock Driver (RTC)");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:pxa-rtc");
