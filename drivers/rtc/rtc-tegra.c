/*
 * An RTC driver for the NVIDIA Tegra 200 series internal RTC.
 *
 * Copyright (c) 2010, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/rtc.h>
#include <linux/platform_device.h>

/* set to 1 = busy every eight 32kHz clocks during copy of sec+msec to AHB */
#define TEGRA_RTC_REG_BUSY			0x004
#define TEGRA_RTC_REG_SECONDS			0x008
/* when msec is read, the seconds are buffered into shadow seconds. */
#define TEGRA_RTC_REG_SHADOW_SECONDS		0x00c
#define TEGRA_RTC_REG_MILLI_SECONDS		0x010
#define TEGRA_RTC_REG_SECONDS_ALARM0		0x014
#define TEGRA_RTC_REG_SECONDS_ALARM1		0x018
#define TEGRA_RTC_REG_MILLI_SECONDS_ALARM0	0x01c
#define TEGRA_RTC_REG_INTR_MASK			0x028
/* write 1 bits to clear status bits */
#define TEGRA_RTC_REG_INTR_STATUS		0x02c

/* bits in INTR_MASK */
#define TEGRA_RTC_INTR_MASK_MSEC_CDN_ALARM	(1<<4)
#define TEGRA_RTC_INTR_MASK_SEC_CDN_ALARM	(1<<3)
#define TEGRA_RTC_INTR_MASK_MSEC_ALARM		(1<<2)
#define TEGRA_RTC_INTR_MASK_SEC_ALARM1		(1<<1)
#define TEGRA_RTC_INTR_MASK_SEC_ALARM0		(1<<0)

/* bits in INTR_STATUS */
#define TEGRA_RTC_INTR_STATUS_MSEC_CDN_ALARM	(1<<4)
#define TEGRA_RTC_INTR_STATUS_SEC_CDN_ALARM	(1<<3)
#define TEGRA_RTC_INTR_STATUS_MSEC_ALARM	(1<<2)
#define TEGRA_RTC_INTR_STATUS_SEC_ALARM1	(1<<1)
#define TEGRA_RTC_INTR_STATUS_SEC_ALARM0	(1<<0)

struct tegra_rtc_info {
	struct platform_device	*pdev;
	struct rtc_device	*rtc_dev;
	void __iomem		*rtc_base; /* NULL if not initialized. */
	int			tegra_rtc_irq; /* alarm and periodic irq */
	spinlock_t		tegra_rtc_lock;
};

/* RTC hardware is busy when it is updating its values over AHB once
 * every eight 32kHz clocks (~250uS).
 * outside of these updates the CPU is free to write.
 * CPU is always free to read.
 */
static inline u32 tegra_rtc_check_busy(struct tegra_rtc_info *info)
{
	return readl(info->rtc_base + TEGRA_RTC_REG_BUSY) & 1;
}

/* Wait for hardware to be ready for writing.
 * This function tries to maximize the amount of time before the next update.
 * It does this by waiting for the RTC to become busy with its periodic update,
 * then returning once the RTC first becomes not busy.
 * This periodic update (where the seconds and milliseconds are copied to the
 * AHB side) occurs every eight 32kHz clocks (~250uS).
 * The behavior of this function allows us to make some assumptions without
 * introducing a race, because 250uS is plenty of time to read/write a value.
 */
static int tegra_rtc_wait_while_busy(struct device *dev)
{
	struct tegra_rtc_info *info = dev_get_drvdata(dev);

	int retries = 500; /* ~490 us is the worst case, ~250 us is best. */

	/* first wait for the RTC to become busy. this is when it
	 * posts its updated seconds+msec registers to AHB side. */
	while (tegra_rtc_check_busy(info)) {
		if (!retries--)
			goto retry_failed;
		udelay(1);
	}

	/* now we have about 250 us to manipulate registers */
	return 0;

retry_failed:
	dev_err(dev, "write failed:retry count exceeded.\n");
	return -ETIMEDOUT;
}

static int tegra_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct tegra_rtc_info *info = dev_get_drvdata(dev);
	unsigned long sec, msec;
	unsigned long sl_irq_flags;

	/* RTC hardware copies seconds to shadow seconds when a read
	 * of milliseconds occurs. use a lock to keep other threads out. */
	spin_lock_irqsave(&info->tegra_rtc_lock, sl_irq_flags);

	msec = readl(info->rtc_base + TEGRA_RTC_REG_MILLI_SECONDS);
	sec = readl(info->rtc_base + TEGRA_RTC_REG_SHADOW_SECONDS);

	spin_unlock_irqrestore(&info->tegra_rtc_lock, sl_irq_flags);

	rtc_time_to_tm(sec, tm);

	dev_vdbg(dev, "time read as %lu. %d/%d/%d %d:%02u:%02u\n",
		sec,
		tm->tm_mon + 1,
		tm->tm_mday,
		tm->tm_year + 1900,
		tm->tm_hour,
		tm->tm_min,
		tm->tm_sec
	);

	return 0;
}

static int tegra_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct tegra_rtc_info *info = dev_get_drvdata(dev);
	unsigned long sec;
	int ret;

	/* convert tm to seconds. */
	ret = rtc_valid_tm(tm);
	if (ret)
		return ret;

	rtc_tm_to_time(tm, &sec);

	dev_vdbg(dev, "time set to %lu. %d/%d/%d %d:%02u:%02u\n",
		sec,
		tm->tm_mon+1,
		tm->tm_mday,
		tm->tm_year+1900,
		tm->tm_hour,
		tm->tm_min,
		tm->tm_sec
	);

	/* seconds only written if wait succeeded. */
	ret = tegra_rtc_wait_while_busy(dev);
	if (!ret)
		writel(sec, info->rtc_base + TEGRA_RTC_REG_SECONDS);

	dev_vdbg(dev, "time read back as %d\n",
		readl(info->rtc_base + TEGRA_RTC_REG_SECONDS));

	return ret;
}

static int tegra_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct tegra_rtc_info *info = dev_get_drvdata(dev);
	unsigned long sec;
	unsigned tmp;

	sec = readl(info->rtc_base + TEGRA_RTC_REG_SECONDS_ALARM0);

	if (sec == 0) {
		/* alarm is disabled. */
		alarm->enabled = 0;
		alarm->time.tm_mon = -1;
		alarm->time.tm_mday = -1;
		alarm->time.tm_year = -1;
		alarm->time.tm_hour = -1;
		alarm->time.tm_min = -1;
		alarm->time.tm_sec = -1;
	} else {
		/* alarm is enabled. */
		alarm->enabled = 1;
		rtc_time_to_tm(sec, &alarm->time);
	}

	tmp = readl(info->rtc_base + TEGRA_RTC_REG_INTR_STATUS);
	alarm->pending = (tmp & TEGRA_RTC_INTR_STATUS_SEC_ALARM0) != 0;

	return 0;
}

static int tegra_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct tegra_rtc_info *info = dev_get_drvdata(dev);
	unsigned status;
	unsigned long sl_irq_flags;

	tegra_rtc_wait_while_busy(dev);
	spin_lock_irqsave(&info->tegra_rtc_lock, sl_irq_flags);

	/* read the original value, and OR in the flag. */
	status = readl(info->rtc_base + TEGRA_RTC_REG_INTR_MASK);
	if (enabled)
		status |= TEGRA_RTC_INTR_MASK_SEC_ALARM0; /* set it */
	else
		status &= ~TEGRA_RTC_INTR_MASK_SEC_ALARM0; /* clear it */

	writel(status, info->rtc_base + TEGRA_RTC_REG_INTR_MASK);

	spin_unlock_irqrestore(&info->tegra_rtc_lock, sl_irq_flags);

	return 0;
}

static int tegra_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alarm)
{
	struct tegra_rtc_info *info = dev_get_drvdata(dev);
	unsigned long sec;

	if (alarm->enabled)
		rtc_tm_to_time(&alarm->time, &sec);
	else
		sec = 0;

	tegra_rtc_wait_while_busy(dev);
	writel(sec, info->rtc_base + TEGRA_RTC_REG_SECONDS_ALARM0);
	dev_vdbg(dev, "alarm read back as %d\n",
		readl(info->rtc_base + TEGRA_RTC_REG_SECONDS_ALARM0));

	/* if successfully written and alarm is enabled ... */
	if (sec) {
		tegra_rtc_alarm_irq_enable(dev, 1);

		dev_vdbg(dev, "alarm set as %lu. %d/%d/%d %d:%02u:%02u\n",
			sec,
			alarm->time.tm_mon+1,
			alarm->time.tm_mday,
			alarm->time.tm_year+1900,
			alarm->time.tm_hour,
			alarm->time.tm_min,
			alarm->time.tm_sec);
	} else {
		/* disable alarm if 0 or write error. */
		dev_vdbg(dev, "alarm disabled\n");
		tegra_rtc_alarm_irq_enable(dev, 0);
	}

	return 0;
}

static int tegra_rtc_proc(struct device *dev, struct seq_file *seq)
{
	if (!dev || !dev->driver)
		return 0;

	return seq_printf(seq, "name\t\t: %s\n", dev_name(dev));
}

static irqreturn_t tegra_rtc_irq_handler(int irq, void *data)
{
	struct device *dev = data;
	struct tegra_rtc_info *info = dev_get_drvdata(dev);
	unsigned long events = 0;
	unsigned status;
	unsigned long sl_irq_flags;

	status = readl(info->rtc_base + TEGRA_RTC_REG_INTR_STATUS);
	if (status) {
		/* clear the interrupt masks and status on any irq. */
		tegra_rtc_wait_while_busy(dev);
		spin_lock_irqsave(&info->tegra_rtc_lock, sl_irq_flags);
		writel(0, info->rtc_base + TEGRA_RTC_REG_INTR_MASK);
		writel(status, info->rtc_base + TEGRA_RTC_REG_INTR_STATUS);
		spin_unlock_irqrestore(&info->tegra_rtc_lock, sl_irq_flags);
	}

	/* check if Alarm */
	if ((status & TEGRA_RTC_INTR_STATUS_SEC_ALARM0))
		events |= RTC_IRQF | RTC_AF;

	/* check if Periodic */
	if ((status & TEGRA_RTC_INTR_STATUS_SEC_CDN_ALARM))
		events |= RTC_IRQF | RTC_PF;

	rtc_update_irq(info->rtc_dev, 1, events);

	return IRQ_HANDLED;
}

static struct rtc_class_ops tegra_rtc_ops = {
	.read_time	= tegra_rtc_read_time,
	.set_time	= tegra_rtc_set_time,
	.read_alarm	= tegra_rtc_read_alarm,
	.set_alarm	= tegra_rtc_set_alarm,
	.proc		= tegra_rtc_proc,
	.alarm_irq_enable = tegra_rtc_alarm_irq_enable,
};

static const struct of_device_id tegra_rtc_dt_match[] = {
	{ .compatible = "nvidia,tegra20-rtc", },
	{}
};
MODULE_DEVICE_TABLE(of, tegra_rtc_dt_match);

static int tegra_rtc_probe(struct platform_device *pdev)
{
	struct tegra_rtc_info *info;
	struct resource *res;
	int ret;

	info = devm_kzalloc(&pdev->dev, sizeof(struct tegra_rtc_info),
		GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev,
			"Unable to allocate resources for device.\n");
		return -EBUSY;
	}

	info->rtc_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(info->rtc_base))
		return PTR_ERR(info->rtc_base);

	info->tegra_rtc_irq = platform_get_irq(pdev, 0);
	if (info->tegra_rtc_irq <= 0)
		return -EBUSY;

	/* set context info. */
	info->pdev = pdev;
	spin_lock_init(&info->tegra_rtc_lock);

	platform_set_drvdata(pdev, info);

	/* clear out the hardware. */
	writel(0, info->rtc_base + TEGRA_RTC_REG_SECONDS_ALARM0);
	writel(0xffffffff, info->rtc_base + TEGRA_RTC_REG_INTR_STATUS);
	writel(0, info->rtc_base + TEGRA_RTC_REG_INTR_MASK);

	device_init_wakeup(&pdev->dev, 1);

	info->rtc_dev = rtc_device_register(
		pdev->name, &pdev->dev, &tegra_rtc_ops, THIS_MODULE);
	if (IS_ERR(info->rtc_dev)) {
		ret = PTR_ERR(info->rtc_dev);
		info->rtc_dev = NULL;
		dev_err(&pdev->dev,
			"Unable to register device (err=%d).\n",
			ret);
		return ret;
	}

	ret = devm_request_irq(&pdev->dev, info->tegra_rtc_irq,
			tegra_rtc_irq_handler, IRQF_TRIGGER_HIGH,
			"rtc alarm", &pdev->dev);
	if (ret) {
		dev_err(&pdev->dev,
			"Unable to request interrupt for device (err=%d).\n",
			ret);
		goto err_dev_unreg;
	}

	dev_notice(&pdev->dev, "Tegra internal Real Time Clock\n");

	return 0;

err_dev_unreg:
	rtc_device_unregister(info->rtc_dev);

	return ret;
}

static int tegra_rtc_remove(struct platform_device *pdev)
{
	struct tegra_rtc_info *info = platform_get_drvdata(pdev);

	rtc_device_unregister(info->rtc_dev);

	platform_set_drvdata(pdev, NULL);

	return 0;
}

#ifdef CONFIG_PM
static int tegra_rtc_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct device *dev = &pdev->dev;
	struct tegra_rtc_info *info = platform_get_drvdata(pdev);

	tegra_rtc_wait_while_busy(dev);

	/* only use ALARM0 as a wake source. */
	writel(0xffffffff, info->rtc_base + TEGRA_RTC_REG_INTR_STATUS);
	writel(TEGRA_RTC_INTR_STATUS_SEC_ALARM0,
		info->rtc_base + TEGRA_RTC_REG_INTR_MASK);

	dev_vdbg(dev, "alarm sec = %d\n",
		readl(info->rtc_base + TEGRA_RTC_REG_SECONDS_ALARM0));

	dev_vdbg(dev, "Suspend (device_may_wakeup=%d) irq:%d\n",
		device_may_wakeup(dev), info->tegra_rtc_irq);

	/* leave the alarms on as a wake source. */
	if (device_may_wakeup(dev))
		enable_irq_wake(info->tegra_rtc_irq);

	return 0;
}

static int tegra_rtc_resume(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct tegra_rtc_info *info = platform_get_drvdata(pdev);

	dev_vdbg(dev, "Resume (device_may_wakeup=%d)\n",
		device_may_wakeup(dev));
	/* alarms were left on as a wake source, turn them off. */
	if (device_may_wakeup(dev))
		disable_irq_wake(info->tegra_rtc_irq);

	return 0;
}
#endif

static void tegra_rtc_shutdown(struct platform_device *pdev)
{
	dev_vdbg(&pdev->dev, "disabling interrupts.\n");
	tegra_rtc_alarm_irq_enable(&pdev->dev, 0);
}

MODULE_ALIAS("platform:tegra_rtc");
static struct platform_driver tegra_rtc_driver = {
	.remove		= tegra_rtc_remove,
	.shutdown	= tegra_rtc_shutdown,
	.driver		= {
		.name	= "tegra_rtc",
		.owner	= THIS_MODULE,
		.of_match_table = tegra_rtc_dt_match,
	},
#ifdef CONFIG_PM
	.suspend	= tegra_rtc_suspend,
	.resume		= tegra_rtc_resume,
#endif
};

static int __init tegra_rtc_init(void)
{
	return platform_driver_probe(&tegra_rtc_driver, tegra_rtc_probe);
}
module_init(tegra_rtc_init);

static void __exit tegra_rtc_exit(void)
{
	platform_driver_unregister(&tegra_rtc_driver);
}
module_exit(tegra_rtc_exit);

MODULE_AUTHOR("Jon Mayo <jmayo@nvidia.com>");
MODULE_DESCRIPTION("driver for Tegra internal RTC");
MODULE_LICENSE("GPL");
