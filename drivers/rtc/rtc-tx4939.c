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

#define TX4939_RTCCTL_ALME	0x00000080
#define TX4939_RTCCTL_ALMD	0x00000040
#define TX4939_RTCCTL_BUSY	0x00000020

#define TX4939_RTCCTL_COMMAND	0x00000007
#define TX4939_RTCCTL_COMMAND_NOP	0x00000000
#define TX4939_RTCCTL_COMMAND_GETTIME	0x00000001
#define TX4939_RTCCTL_COMMAND_SETTIME	0x00000002
#define TX4939_RTCCTL_COMMAND_GETALARM	0x00000003
#define TX4939_RTCCTL_COMMAND_SETALARM	0x00000004

#define TX4939_RTCTBC_PM	0x00000080
#define TX4939_RTCTBC_COMP	0x0000007f

#define TX4939_RTC_REG_RAMSIZE	0x00000100
#define TX4939_RTC_REG_RWBSIZE	0x00000006

struct tx4939_rtc_reg {
	__u32 ctl;
	__u32 adr;
	__u32 dat;
	__u32 tbc;
};

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
	sec = ((unsigned long)buf[5] << 24) | (buf[4] << 16) |
		(buf[3] << 8) | buf[2];
	rtc_time_to_tm(sec, tm);
	return 0;
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
	sec = ((unsigned long)buf[5] << 24) | (buf[4] << 16) |
		(buf[3] << 8) | buf[2];
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

static int tx4939_nvram_read(void *priv, unsigned int pos, void *val,
			     size_t bytes)
{
	struct tx4939rtc_plat_data *pdata = priv;
	struct tx4939_rtc_reg __iomem *rtcreg = pdata->rtcreg;
	u8 *buf = val;

	spin_lock_irq(&pdata->lock);
	for (; bytes; bytes--) {
		__raw_writel(pos++, &rtcreg->adr);
		*buf++ = __raw_readl(&rtcreg->dat);
	}
	spin_unlock_irq(&pdata->lock);
	return 0;
}

static int tx4939_nvram_write(void *priv, unsigned int pos, void *val,
			      size_t bytes)
{
	struct tx4939rtc_plat_data *pdata = priv;
	struct tx4939_rtc_reg __iomem *rtcreg = pdata->rtcreg;
	u8 *buf = val;

	spin_lock_irq(&pdata->lock);
	for (; bytes; bytes--) {
		__raw_writel(pos++, &rtcreg->adr);
		__raw_writel(*buf++, &rtcreg->dat);
	}
	spin_unlock_irq(&pdata->lock);
	return 0;
}

static int __init tx4939_rtc_probe(struct platform_device *pdev)
{
	struct rtc_device *rtc;
	struct tx4939rtc_plat_data *pdata;
	struct resource *res;
	int irq, ret;
	struct nvmem_config nvmem_cfg = {
		.name = "rv8803_nvram",
		.word_size = 4,
		.stride = 4,
		.size = TX4939_RTC_REG_RAMSIZE,
		.reg_read = tx4939_nvram_read,
		.reg_write = tx4939_nvram_write,
	};

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return -ENODEV;
	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;
	platform_set_drvdata(pdev, pdata);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pdata->rtcreg = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pdata->rtcreg))
		return PTR_ERR(pdata->rtcreg);

	spin_lock_init(&pdata->lock);
	tx4939_rtc_cmd(pdata->rtcreg, TX4939_RTCCTL_COMMAND_NOP);
	if (devm_request_irq(&pdev->dev, irq, tx4939_rtc_interrupt,
			     0, pdev->name, &pdev->dev) < 0)
		return -EBUSY;
	rtc = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	rtc->ops = &tx4939_rtc_ops;
	rtc->nvram_old_abi = true;

	pdata->rtc = rtc;

	nvmem_cfg.priv = pdata;
	ret = rtc_nvmem_register(rtc, &nvmem_cfg);
	if (ret)
		return ret;

	return rtc_register_device(rtc);
}

static int __exit tx4939_rtc_remove(struct platform_device *pdev)
{
	struct tx4939rtc_plat_data *pdata = platform_get_drvdata(pdev);

	spin_lock_irq(&pdata->lock);
	tx4939_rtc_cmd(pdata->rtcreg, TX4939_RTCCTL_COMMAND_NOP);
	spin_unlock_irq(&pdata->lock);
	return 0;
}

static struct platform_driver tx4939_rtc_driver = {
	.remove		= __exit_p(tx4939_rtc_remove),
	.driver		= {
		.name	= "tx4939rtc",
	},
};

module_platform_driver_probe(tx4939_rtc_driver, tx4939_rtc_probe);

MODULE_AUTHOR("Atsushi Nemoto <anemo@mba.ocn.ne.jp>");
MODULE_DESCRIPTION("TX4939 internal RTC driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:tx4939rtc");
