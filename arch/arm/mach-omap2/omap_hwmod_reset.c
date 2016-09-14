/*
 * OMAP IP block custom reset and preprogramming stubs
 *
 * Copyright (C) 2012 Texas Instruments, Inc.
 * Paul Walmsley
 *
 * A small number of IP blocks need custom reset and preprogramming
 * functions.  The stubs in this file provide a standard way for the
 * hwmod code to call these functions, which are to be located under
 * drivers/.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */
#include <linux/kernel.h>
#include <linux/errno.h>

#include <sound/aess.h>

#include "omap_hwmod.h"
#include "common.h"

#define OMAP_RTC_STATUS_REG	0x44
#define OMAP_RTC_KICK0_REG	0x6c
#define OMAP_RTC_KICK1_REG	0x70

#define OMAP_RTC_KICK0_VALUE	0x83E70B13
#define OMAP_RTC_KICK1_VALUE	0x95A4F1E0
#define OMAP_RTC_STATUS_BUSY	BIT(0)
#define OMAP_RTC_MAX_READY_TIME	50

/**
 * omap_hwmod_aess_preprogram - enable AESS internal autogating
 * @oh: struct omap_hwmod *
 *
 * The AESS will not IdleAck to the PRCM until its internal autogating
 * is enabled.  Since internal autogating is disabled by default after
 * AESS reset, we must enable autogating after the hwmod code resets
 * the AESS.  Returns 0.
 */
int omap_hwmod_aess_preprogram(struct omap_hwmod *oh)
{
	void __iomem *va;

	va = omap_hwmod_get_mpu_rt_va(oh);
	if (!va)
		return -EINVAL;

	aess_enable_autogating(va);

	return 0;
}

/**
 * omap_rtc_wait_not_busy - Wait for the RTC BUSY flag
 * @oh: struct omap_hwmod *
 *
 * For updating certain RTC registers, the MPU must wait
 * for the BUSY status in OMAP_RTC_STATUS_REG to become zero.
 * Once the BUSY status is zero, there is a 15 microseconds access
 * period in which the MPU can program.
 */
static void omap_rtc_wait_not_busy(struct omap_hwmod *oh)
{
	int i;

	/* BUSY may stay active for 1/32768 second (~30 usec) */
	omap_test_timeout(omap_hwmod_read(oh, OMAP_RTC_STATUS_REG)
			  & OMAP_RTC_STATUS_BUSY, OMAP_RTC_MAX_READY_TIME, i);
	/* now we have ~15 microseconds to read/write various registers */
}

/**
 * omap_hwmod_rtc_unlock - Unlock the Kicker mechanism.
 * @oh: struct omap_hwmod *
 *
 * RTC IP have kicker feature. This prevents spurious writes to its registers.
 * In order to write into any of the RTC registers, KICK values has te be
 * written in respective KICK registers. This is needed for hwmod to write into
 * sysconfig register.
 */
void omap_hwmod_rtc_unlock(struct omap_hwmod *oh)
{
	local_irq_disable();
	omap_rtc_wait_not_busy(oh);
	omap_hwmod_write(OMAP_RTC_KICK0_VALUE, oh, OMAP_RTC_KICK0_REG);
	omap_hwmod_write(OMAP_RTC_KICK1_VALUE, oh, OMAP_RTC_KICK1_REG);
	local_irq_enable();
}

/**
 * omap_hwmod_rtc_lock - Lock the Kicker mechanism.
 * @oh: struct omap_hwmod *
 *
 * RTC IP have kicker feature. This prevents spurious writes to its registers.
 * Once the RTC registers are written, KICK mechanism needs to be locked,
 * in order to prevent any spurious writes. This function locks back the RTC
 * registers once hwmod completes its write into sysconfig register.
 */
void omap_hwmod_rtc_lock(struct omap_hwmod *oh)
{
	local_irq_disable();
	omap_rtc_wait_not_busy(oh);
	omap_hwmod_write(0x0, oh, OMAP_RTC_KICK0_REG);
	omap_hwmod_write(0x0, oh, OMAP_RTC_KICK1_REG);
	local_irq_enable();
}
