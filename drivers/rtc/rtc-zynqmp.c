// SPDX-License-Identifier: GPL-2.0
/*
 * Xilinx Zynq Ultrascale+ MPSoC Real Time Clock Driver
 *
 * Copyright (C) 2015 Xilinx, Inc.
 *
 */

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>

/* RTC Registers */
#define RTC_SET_TM_WR		0x00
#define RTC_SET_TM_RD		0x04
#define RTC_CALIB_WR		0x08
#define RTC_CALIB_RD		0x0C
#define RTC_CUR_TM		0x10
#define RTC_CUR_TICK		0x14
#define RTC_ALRM		0x18
#define RTC_INT_STS		0x20
#define RTC_INT_MASK		0x24
#define RTC_INT_EN		0x28
#define RTC_INT_DIS		0x2C
#define RTC_CTRL		0x40

#define RTC_FR_EN		BIT(20)
#define RTC_FR_DATSHIFT		16
#define RTC_TICK_MASK		0xFFFF
#define RTC_INT_SEC		BIT(0)
#define RTC_INT_ALRM		BIT(1)
#define RTC_OSC_EN		BIT(24)
#define RTC_BATT_EN		BIT(31)

#define RTC_CALIB_DEF		0x198233
#define RTC_CALIB_MASK		0x1FFFFF
#define RTC_ALRM_MASK          BIT(1)
#define RTC_MSEC               1000

struct xlnx_rtc_dev {
	struct rtc_device	*rtc;
	void __iomem		*reg_base;
	int			alarm_irq;
	int			sec_irq;
	unsigned int		calibval;
};

static int xlnx_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct xlnx_rtc_dev *xrtcdev = dev_get_drvdata(dev);
	unsigned long new_time;

	/*
	 * The value written will be updated after 1 sec into the
	 * seconds read register, so we need to program time +1 sec
	 * to get the correct time on read.
	 */
	new_time = rtc_tm_to_time64(tm) + 1;

	/*
	 * Writing into calibration register will clear the Tick Counter and
	 * force the next second to be signaled exactly in 1 second period
	 */
	xrtcdev->calibval &= RTC_CALIB_MASK;
	writel(xrtcdev->calibval, (xrtcdev->reg_base + RTC_CALIB_WR));

	writel(new_time, xrtcdev->reg_base + RTC_SET_TM_WR);

	/*
	 * Clear the rtc interrupt status register after setting the
	 * time. During a read_time function, the code should read the
	 * RTC_INT_STATUS register and if bit 0 is still 0, it means
	 * that one second has not elapsed yet since RTC was set and
	 * the current time should be read from SET_TIME_READ register;
	 * otherwise, CURRENT_TIME register is read to report the time
	 */
	writel(RTC_INT_SEC, xrtcdev->reg_base + RTC_INT_STS);

	return 0;
}

static int xlnx_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	u32 status;
	unsigned long read_time;
	struct xlnx_rtc_dev *xrtcdev = dev_get_drvdata(dev);

	status = readl(xrtcdev->reg_base + RTC_INT_STS);

	if (status & RTC_INT_SEC) {
		/*
		 * RTC has updated the CURRENT_TIME with the time written into
		 * SET_TIME_WRITE register.
		 */
		read_time = readl(xrtcdev->reg_base + RTC_CUR_TM);
	} else {
		/*
		 * Time written in SET_TIME_WRITE has not yet updated into
		 * the seconds read register, so read the time from the
		 * SET_TIME_WRITE instead of CURRENT_TIME register.
		 * Since we add +1 sec while writing, we need to -1 sec while
		 * reading.
		 */
		read_time = readl(xrtcdev->reg_base + RTC_SET_TM_RD) - 1;
	}
	rtc_time64_to_tm(read_time, tm);

	return 0;
}

static int xlnx_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct xlnx_rtc_dev *xrtcdev = dev_get_drvdata(dev);

	rtc_time64_to_tm(readl(xrtcdev->reg_base + RTC_ALRM), &alrm->time);
	alrm->enabled = readl(xrtcdev->reg_base + RTC_INT_MASK) & RTC_INT_ALRM;

	return 0;
}

static int xlnx_rtc_alarm_irq_enable(struct device *dev, u32 enabled)
{
	struct xlnx_rtc_dev *xrtcdev = dev_get_drvdata(dev);
	unsigned int status;
	ulong timeout;

	timeout = jiffies + msecs_to_jiffies(RTC_MSEC);

	if (enabled) {
		while (1) {
			status = readl(xrtcdev->reg_base + RTC_INT_STS);
			if (!((status & RTC_ALRM_MASK) == RTC_ALRM_MASK))
				break;

			if (time_after_eq(jiffies, timeout)) {
				dev_err(dev, "Time out occur, while clearing alarm status bit\n");
				return -ETIMEDOUT;
			}
			writel(RTC_INT_ALRM, xrtcdev->reg_base + RTC_INT_STS);
		}

		writel(RTC_INT_ALRM, xrtcdev->reg_base + RTC_INT_EN);
	} else {
		writel(RTC_INT_ALRM, xrtcdev->reg_base + RTC_INT_DIS);
	}

	return 0;
}

static int xlnx_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct xlnx_rtc_dev *xrtcdev = dev_get_drvdata(dev);
	unsigned long alarm_time;

	alarm_time = rtc_tm_to_time64(&alrm->time);

	writel((u32)alarm_time, (xrtcdev->reg_base + RTC_ALRM));

	xlnx_rtc_alarm_irq_enable(dev, alrm->enabled);

	return 0;
}

static void xlnx_init_rtc(struct xlnx_rtc_dev *xrtcdev)
{
	u32 rtc_ctrl;

	/* Enable RTC switch to battery when VCC_PSAUX is not available */
	rtc_ctrl = readl(xrtcdev->reg_base + RTC_CTRL);
	rtc_ctrl |= RTC_BATT_EN;
	writel(rtc_ctrl, xrtcdev->reg_base + RTC_CTRL);

	/*
	 * Based on crystal freq of 33.330 KHz
	 * set the seconds counter and enable, set fractions counter
	 * to default value suggested as per design spec
	 * to correct RTC delay in frequency over period of time.
	 */
	xrtcdev->calibval &= RTC_CALIB_MASK;
	writel(xrtcdev->calibval, (xrtcdev->reg_base + RTC_CALIB_WR));
}

static const struct rtc_class_ops xlnx_rtc_ops = {
	.set_time	  = xlnx_rtc_set_time,
	.read_time	  = xlnx_rtc_read_time,
	.read_alarm	  = xlnx_rtc_read_alarm,
	.set_alarm	  = xlnx_rtc_set_alarm,
	.alarm_irq_enable = xlnx_rtc_alarm_irq_enable,
};

static irqreturn_t xlnx_rtc_interrupt(int irq, void *id)
{
	struct xlnx_rtc_dev *xrtcdev = (struct xlnx_rtc_dev *)id;
	unsigned int status;

	status = readl(xrtcdev->reg_base + RTC_INT_STS);
	/* Check if interrupt asserted */
	if (!(status & (RTC_INT_SEC | RTC_INT_ALRM)))
		return IRQ_NONE;

	/* Disable RTC_INT_ALRM interrupt only */
	writel(RTC_INT_ALRM, xrtcdev->reg_base + RTC_INT_DIS);

	if (status & RTC_INT_ALRM)
		rtc_update_irq(xrtcdev->rtc, 1, RTC_IRQF | RTC_AF);

	return IRQ_HANDLED;
}

static int xlnx_rtc_probe(struct platform_device *pdev)
{
	struct xlnx_rtc_dev *xrtcdev;
	int ret;

	xrtcdev = devm_kzalloc(&pdev->dev, sizeof(*xrtcdev), GFP_KERNEL);
	if (!xrtcdev)
		return -ENOMEM;

	platform_set_drvdata(pdev, xrtcdev);

	xrtcdev->rtc = devm_rtc_allocate_device(&pdev->dev);
	if (IS_ERR(xrtcdev->rtc))
		return PTR_ERR(xrtcdev->rtc);

	xrtcdev->rtc->ops = &xlnx_rtc_ops;
	xrtcdev->rtc->range_max = U32_MAX;

	xrtcdev->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(xrtcdev->reg_base))
		return PTR_ERR(xrtcdev->reg_base);

	xrtcdev->alarm_irq = platform_get_irq_byname(pdev, "alarm");
	if (xrtcdev->alarm_irq < 0)
		return xrtcdev->alarm_irq;
	ret = devm_request_irq(&pdev->dev, xrtcdev->alarm_irq,
			       xlnx_rtc_interrupt, 0,
			       dev_name(&pdev->dev), xrtcdev);
	if (ret) {
		dev_err(&pdev->dev, "request irq failed\n");
		return ret;
	}

	xrtcdev->sec_irq = platform_get_irq_byname(pdev, "sec");
	if (xrtcdev->sec_irq < 0)
		return xrtcdev->sec_irq;
	ret = devm_request_irq(&pdev->dev, xrtcdev->sec_irq,
			       xlnx_rtc_interrupt, 0,
			       dev_name(&pdev->dev), xrtcdev);
	if (ret) {
		dev_err(&pdev->dev, "request irq failed\n");
		return ret;
	}

	ret = of_property_read_u32(pdev->dev.of_node, "calibration",
				   &xrtcdev->calibval);
	if (ret)
		xrtcdev->calibval = RTC_CALIB_DEF;

	xlnx_init_rtc(xrtcdev);

	device_init_wakeup(&pdev->dev, 1);

	return rtc_register_device(xrtcdev->rtc);
}

static int xlnx_rtc_remove(struct platform_device *pdev)
{
	xlnx_rtc_alarm_irq_enable(&pdev->dev, 0);
	device_init_wakeup(&pdev->dev, 0);

	return 0;
}

static int __maybe_unused xlnx_rtc_suspend(struct device *dev)
{
	struct xlnx_rtc_dev *xrtcdev = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		enable_irq_wake(xrtcdev->alarm_irq);
	else
		xlnx_rtc_alarm_irq_enable(dev, 0);

	return 0;
}

static int __maybe_unused xlnx_rtc_resume(struct device *dev)
{
	struct xlnx_rtc_dev *xrtcdev = dev_get_drvdata(dev);

	if (device_may_wakeup(dev))
		disable_irq_wake(xrtcdev->alarm_irq);
	else
		xlnx_rtc_alarm_irq_enable(dev, 1);

	return 0;
}

static SIMPLE_DEV_PM_OPS(xlnx_rtc_pm_ops, xlnx_rtc_suspend, xlnx_rtc_resume);

static const struct of_device_id xlnx_rtc_of_match[] = {
	{.compatible = "xlnx,zynqmp-rtc" },
	{ }
};
MODULE_DEVICE_TABLE(of, xlnx_rtc_of_match);

static struct platform_driver xlnx_rtc_driver = {
	.probe		= xlnx_rtc_probe,
	.remove		= xlnx_rtc_remove,
	.driver		= {
		.name	= KBUILD_MODNAME,
		.pm	= &xlnx_rtc_pm_ops,
		.of_match_table	= xlnx_rtc_of_match,
	},
};

module_platform_driver(xlnx_rtc_driver);

MODULE_DESCRIPTION("Xilinx Zynq MPSoC RTC driver");
MODULE_AUTHOR("Xilinx Inc.");
MODULE_LICENSE("GPL v2");
