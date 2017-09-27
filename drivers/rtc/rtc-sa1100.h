#ifndef __RTC_SA1100_H__
#define __RTC_SA1100_H__

#include <linux/kernel.h>

struct clk;
struct platform_device;

struct sa1100_rtc {
	spinlock_t		lock;
	void __iomem		*rcnr;
	void __iomem		*rtar;
	void __iomem		*rtsr;
	void __iomem		*rttr;
	int			irq_1hz;
	int			irq_alarm;
	struct rtc_device	*rtc;
	struct clk		*clk;
};

int sa1100_rtc_init(struct platform_device *pdev, struct sa1100_rtc *info);

#endif
