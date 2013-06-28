/*
 * TX4939 internal RTC driver
 * Based on RBTX49xx patch from CELF patch archive.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * (C) Copyright TOSHIBA CORPORATION 2005-2007
 */
#include <linux/rtc.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/gfp.h>
#include <asm/txx9/tx4939.h>

struct tx4939rtc_plat_data {
	struct rtc_device *rtc;
	struct tx4939_rtc_reg __iomem *rtcreg;
	spinlock_t lock;
};

static struct tx4939rtc_plat_data *get_tx4939rtc_plat_data(struct device *dev)
{
	return platform_get_drvdata(to_platform_device(dev));
}

static int tx4939_rtc_cmd(struct tx4939_rtc_reg __iomem *rtcreg, int cmd)
{
	int i = 0;

	__raw_writel(cmd, &rtcreg->ctl);
	/* This might take 30us (next 32.768KHz clock) */
	while (__raw_readl(&rtcreg->ctl) & TX4939_RTCCTL_BUSY) {
		/* timeout on approx. 100us (@ GBUS200MHz) */
		if (i++ > 200 * 100)
			return -EBUSY;
		cpu_relax();
	}
	return 0;
}

static int tx4939_rtc_set_mmss(struct device *dev, unsigned long secs)
{
	struct tx4939rtc_plat_data *pdata = get_tx4939rtc_plat_data(dev);
	struct tx4939_rtc_reg __iomem *rtcreg = pdata->rtcreg;
	int i, ret;
	unsigned char buf[6];

	buf[0] = 0;
	buf[1] = 0;
	buf[2] = secs;
	buf[3] = secs >> 8;
	buf[4] = secs >> 16;
	buf[5] = secs >> 24;
	spin_lock_irq(&pdata->lock);
	__raw_writel(0, &rtcreg->adr);
	for (i = 0; i < 6; i++)
		__raw_writel(buf[i], &rtcreg->dat);
	ret = tx4939_rtc_cmd(rtcreg,
			     TX4939_RTCCTL_COMMAND_SETTIME |
			     (__raw_readl(&rtcreg->ctl) & TX4939_RTCCTL_ALME));
	spin_unlock_irq(&pdata->lock);
	return ret;
}

static int tx4939_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct tx4939rtc_plat_data *pdata = get_tx4939rtc_plat_data(dev);
	struct tx4939_rtc_reg __iomem *rtcreg = pdata->rtcreg;
	int i, ret;
	unsigned long sec;
	unsigned char buf[6];

	spin_lock_irq(&pdata->lock);
	ret = tx4939_rtc_cmd(rtcreg,
			     TX4939_RTCCTL_COMMAND_GETTIME |
			     (__raw_readl(&rtcreg->ctl) & TX4939_RTCCTL_ALME));
	if (ret) {
		spin_unlock_irq(&pdata->lock);
		return ret;
	}
	__raw_writel(2, &rtcreg->adr);
	for (i = 2; i < 6; i++)
		buf[i] = __raw_readl(&rtcreg->dat);
	spin_unlock_irq(&pdata->lock);
	sec = (buf[5] << 24) | (buf[4] << 16) | (buf[3] << 8) | buf[2];
	rtc_time_to_tm(sec, tm);
	return rtc_valid_tm(tm);
}

static int tx4939_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct tx4939rtc_plat_data *pdata = get_tx4939rtc_plat_data(dev);
	struct tx4939_rtc_reg __iomem *rtcreg = pdata->rtcreg;
	int i, ret;
	unsigned long sec;
	unsigned char buf[6];

	if (alrm->time.tm_sec < 0 ||
	    alrm->time.tm_min < 0 ||
	    alrm->time.tm_hour < 0 ||
	    alrm->time.tm_mday < 0 ||
	    alrm->time.tm_mon < 0 ||
	    alrm->time.tm_year < 0)
		return -EINVAL;
	rtc_tm_to_time(&alrm->time, &sec);
	buf[0] = 0;
	buf[1] = 0;
	buf[2] = sec;
	buf[3] = sec >> 8;
	buf[4] = sec >> 16;
	buf[5] = sec >> 24;
	spin_lock_irq(&pdata->lock);
	__raw_writel(0, &rtcreg->adr);
	for (i = 0; i < 6; i++)
		__raw_writel(buf[i], &rtcreg->dat);
	ret = tx4939_rtc_cmd(rtcreg, TX4939_RTCCTL_COMMAND_SETALARM |
			     (alrm->enabled ? TX4939_RTCCTL_ALME : 0));
	spin_unlock_irq(&pdata->lock);
	return ret;
}

static int tx4939_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct tx4939rtc_plat_data *pdata = get_tx4939rtc_plat_data(dev);
	struct tx4939_rtc_reg __iomem *rtcreg = pdata->rtcreg;
	int i, ret;
	unsigned long sec;
	unsigned char buf[6];
	u32 ctl;

	spin_lock_irq(&pdata->lock);
	ret = tx4939_rtc_cmd(rtcreg,
			     TX4939_RTCCTL_COMMAND_GETALARM |
			     (__raw_readl(&rtcreg->ctl) & TX4939_RTCCTL_ALME));
	if (ret) {
		spin_unlock_irq(&pdata->lock);
		return ret;
	}
	__raw_writel(2, &rtcreg->adr);
	for (i = 2; i < 6; i++)
		buf[i] = __raw_readl(&rtcreg->dat);
	ctl = __raw_readl(&rtcreg->ctl);
	alrm->enabled = (ctl & TX4939_RTCCTL_ALME) ? 1 : 0;
	alrm->pending = (ctl & TX4939_RTCCTL_ALMD) ? 1 : 0;
	spin_unlock_irq(&pdata->lock);
	sec = (buf[5] << 24) | (buf[4] << 16) | (buf[3] << 8) | buf[2];
	rtc_time_to_tm(sec, &alrm->time);
	return rtc_valid_tm(&alrm->time);
}

static int tx4939_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct tx4939rtc_plat_data *pdata = get_tx4939rtc_plat_data(dev);

	spin_lock_irq(&pdata->lock);
	tx4939_rtc_cmd(pdata->rtcreg,
		       TX4939_RTCCTL_COMMAND_NOP |
		       (enabled ? TX4939_RTCCTL_ALME : 0));
	spin_unlock_irq(&pdata->lock);
	return 0;
}

static irqreturn_t tx4939_rtc_interrupt(int irq, void *dev_id)
{
	struct tx4939rtc_plat_data *pdata = get_tx4939rtc_plat_data(dev_id);
	struct tx4939_rtc_reg __iomem *rtcreg = pdata->rtcreg;
	unsigned long events = RTC_IRQF;

	spin_lock(&pdata->lock);
	if (__raw_readl(&rtcreg->ctl) & TX4939_RTCCTL_ALMD) {
		events |= RTC_AF;
		tx4939_rtc_cmd(rtcreg, TX4939_RTCCTL_COMMAND_NOP);
	}
	spin_unlock(&pdata->lock);
	if (likely(pdata->rtc))
		rtc_update_irq(pdata->rtc, 1, events);
	return IRQ_HANDLED;
}

static const struct rtc_class_ops tx4939_rtc_ops = {
	.read_time		= tx4939_rtc_read_time,
	.read_alarm		= tx4939_rtc_read_alarm,
	.set_alarm		= tx4939_rtc_set_alarm,
	.set_mmss		= tx4939_rtc_set_mmss,
	.alarm_irq_enable	= tx4939_rtc_alarm_irq_enable,
};

static ssize_t tx4939_rtc_nvram_read(struct file *filp, struct kobject *kobj,
				     struct bin_attribute *bin_attr,
				     char *buf, loff_t pos, size_t size)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct tx4939rtc_plat_data *pdata = get_tx4939rtc_plat_data(dev);
	struct tx4939_rtc_reg __iomem *rtcreg = pdata->rtcreg;
	ssize_t count;

	spin_lock_irq(&pdata->lock);
	for (count = 0; size > 0 && pos < TX4939_RTC_REG_RAMSIZE;
	     count++, size--) {
		__raw_writel(pos++, &rtcreg->adr);
		*buf++ = __raw_readl(&rtcreg->dat);
	}
	spin_unlock_irq(&pdata->lock);
	return count;
}

static ssize_t tx4939_rtc_nvram_write(struct file *filp, struct kobject *kobj,
				      struct bin_attribute *bin_attr,
				      char *buf, loff_t pos, size_t size)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct tx4939rtc_plat_data *pdata = get_tx4939rtc_plat_data(dev);
	struct tx4939_rtc_reg __iomem *rtcreg = pdata->rtcreg;
	ssize_t count;

	spin_lock_irq(&pdata->lock);
	for (count = 0; size > 0 && pos < TX4939_RTC_REG_RAMSIZE;
	     count++, size--) {
		__raw_writel(pos++, &rtcreg->adr);
		__raw_writel(*buf++, &rtcreg->dat);
	}
	spin_unlock_irq(&pdata->lock);
	return count;
}

static struct bin_attribute tx4939_rtc_nvram_attr = {
	.attr = {
		.name = "nvram",
		.mode = S_IRUGO | S_IWUSR,
	},
	.size = TX4939_RTC_REG_RAMSIZE,
	.read = tx4939_rtc_nvram_read,
	.write = tx4939_rtc_nvram_write,
};

static int __init tx4939_rtc_probe(struct platform_device *pdev)
{
	struct rtc_device *rtc;
	struct tx4939rtc_plat_data *pdata;
	struct resource *res;
	int irq, ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;
	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return -ENODEV;
	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;
	platform_set_drvdata(pdev, pdata);

	if (!devm_request_mem_region(&pdev->dev, res->start,
				     resource_size(res), pdev->name))
		return -EBUSY;
	pdata->rtcreg = devm_ioremap(&pdev->dev, res->start,
				     resource_size(res));
	if (!pdata->rtcreg)
		return -EBUSY;

	spin_lock_init(&pdata->lock);
	tx4939_rtc_cmd(pdata->rtcreg, TX4939_RTCCTL_COMMAND_NOP);
	if (devm_request_irq(&pdev->dev, irq, tx4939_rtc_interrupt,
			     0, pdev->name, &pdev->dev) < 0)
		return -EBUSY;
	rtc = devm_rtc_device_register(&pdev->dev, pdev->name,
				  &tx4939_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);
	pdata->rtc = rtc;
	ret = sysfs_create_bin_file(&pdev->dev.kobj, &tx4939_rtc_nvram_attr);

	return ret;
}

static int __exit tx4939_rtc_remove(struct platform_device *pdev)
{
	struct tx4939rtc_plat_data *pdata = platform_get_drvdata(pdev);

	sysfs_remove_bin_file(&pdev->dev.kobj, &tx4939_rtc_nvram_attr);
	spin_lock_irq(&pdata->lock);
	tx4939_rtc_cmd(pdata->rtcreg, TX4939_RTCCTL_COMMAND_NOP);
	spin_unlock_irq(&pdata->lock);
	return 0;
}

static struct platform_driver tx4939_rtc_driver = {
	.remove		= __exit_p(tx4939_rtc_remove),
	.driver		= {
		.name	= "tx4939rtc",
		.owner	= THIS_MODULE,
	},
};

module_platform_driver_probe(tx4939_rtc_driver, tx4939_rtc_probe);

MODULE_AUTHOR("Atsushi Nemoto <anemo@mba.ocn.ne.jp>");
MODULE_DESCRIPTION("TX4939 internal RTC driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:tx4939rtc");
