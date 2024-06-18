// SPDX-License-Identifier: GPL-2.0 OR MIT
/* Copyright 2017-2019 Qiang Yu <yuq825@gmail.com> */

#include <linux/iopoll.h>
#include <linux/device.h>

#include "lima_device.h"
#include "lima_pmu.h"
#include "lima_regs.h"

#define pmu_write(reg, data) writel(data, ip->iomem + reg)
#define pmu_read(reg) readl(ip->iomem + reg)

static int lima_pmu_wait_cmd(struct lima_ip *ip)
{
	struct lima_device *dev = ip->dev;
	int err;
	u32 v;

	err = readl_poll_timeout(ip->iomem + LIMA_PMU_INT_RAWSTAT,
				 v, v & LIMA_PMU_INT_CMD_MASK,
				 100, 100000);
	if (err) {
		dev_err(dev->dev, "%s timeout wait pmu cmd\n",
			lima_ip_name(ip));
		return err;
	}

	pmu_write(LIMA_PMU_INT_CLEAR, LIMA_PMU_INT_CMD_MASK);
	return 0;
}

static u32 lima_pmu_get_ip_mask(struct lima_ip *ip)
{
	struct lima_device *dev = ip->dev;
	u32 ret = 0;
	int i;

	ret |= LIMA_PMU_POWER_GP0_MASK;

	if (dev->id == lima_gpu_mali400) {
		ret |= LIMA_PMU_POWER_L2_MASK;
		for (i = 0; i < 4; i++) {
			if (dev->ip[lima_ip_pp0 + i].present)
				ret |= LIMA_PMU_POWER_PP_MASK(i);
		}
	} else {
		if (dev->ip[lima_ip_pp0].present)
			ret |= LIMA450_PMU_POWER_PP0_MASK;
		for (i = lima_ip_pp1; i <= lima_ip_pp3; i++) {
			if (dev->ip[i].present) {
				ret |= LIMA450_PMU_POWER_PP13_MASK;
				break;
			}
		}
		for (i = lima_ip_pp4; i <= lima_ip_pp7; i++) {
			if (dev->ip[i].present) {
				ret |= LIMA450_PMU_POWER_PP47_MASK;
				break;
			}
		}
	}

	return ret;
}

static int lima_pmu_hw_init(struct lima_ip *ip)
{
	int err;
	u32 stat;

	pmu_write(LIMA_PMU_INT_MASK, 0);

	/* If this value is too low, when in high GPU clk freq,
	 * GPU will be in unstable state.
	 */
	pmu_write(LIMA_PMU_SW_DELAY, 0xffff);

	/* status reg 1=off 0=on */
	stat = pmu_read(LIMA_PMU_STATUS);

	/* power up all ip */
	if (stat) {
		pmu_write(LIMA_PMU_POWER_UP, stat);
		err = lima_pmu_wait_cmd(ip);
		if (err)
			return err;
	}
	return 0;
}

static void lima_pmu_hw_fini(struct lima_ip *ip)
{
	u32 stat;

	if (!ip->data.mask)
		ip->data.mask = lima_pmu_get_ip_mask(ip);

	stat = ~pmu_read(LIMA_PMU_STATUS) & ip->data.mask;
	if (stat) {
		pmu_write(LIMA_PMU_POWER_DOWN, stat);

		/* Don't wait for interrupt on Mali400 if all domains are
		 * powered off because the HW won't generate an interrupt
		 * in this case.
		 */
		if (ip->dev->id == lima_gpu_mali400)
			pmu_write(LIMA_PMU_INT_CLEAR, LIMA_PMU_INT_CMD_MASK);
		else
			lima_pmu_wait_cmd(ip);
	}
}

int lima_pmu_resume(struct lima_ip *ip)
{
	return lima_pmu_hw_init(ip);
}

void lima_pmu_suspend(struct lima_ip *ip)
{
	lima_pmu_hw_fini(ip);
}

int lima_pmu_init(struct lima_ip *ip)
{
	return lima_pmu_hw_init(ip);
}

void lima_pmu_fini(struct lima_ip *ip)
{
	lima_pmu_hw_fini(ip);
}
