/*
 * vrtc.c: Driver for virtual RTC device on Intel MID platform
 *
 * (C) Copyright 2009 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 *
 * Note:
 * VRTC is emulated by system controller firmware, the real HW
 * RTC is located in the PMIC device. SCU FW shadows PMIC RTC
 * in a memory mapped IO space that is visible to the host IA
 * processor.
 *
 * This driver is based on RTC CMOS driver.
 */

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/sfi.h>
#include <linux/platform_device.h>

#include <asm/mrst.h>
#include <asm/mrst-vrtc.h>
#include <asm/time.h>
#include <asm/fixmap.h>

static unsigned char __iomem *vrtc_virt_base;

unsigned char vrtc_cmos_read(unsigned char reg)
{
	unsigned char retval;

	/* vRTC's registers range from 0x0 to 0xD */
	if (reg > 0xd || !vrtc_virt_base)
		return 0xff;

	lock_cmos_prefix(reg);
	retval = __raw_readb(vrtc_virt_base + (reg << 2));
	lock_cmos_suffix(reg);
	return retval;
}
EXPORT_SYMBOL_GPL(vrtc_cmos_read);

void vrtc_cmos_write(unsigned char val, unsigned char reg)
{
	if (reg > 0xd || !vrtc_virt_base)
		return;

	lock_cmos_prefix(reg);
	__raw_writeb(val, vrtc_virt_base + (reg << 2));
	lock_cmos_suffix(reg);
}
EXPORT_SYMBOL_GPL(vrtc_cmos_write);

unsigned long vrtc_get_time(void)
{
	u8 sec, min, hour, mday, mon;
	unsigned long flags;
	u32 year;

	spin_lock_irqsave(&rtc_lock, flags);

	while ((vrtc_cmos_read(RTC_FREQ_SELECT) & RTC_UIP))
		cpu_relax();

	sec = vrtc_cmos_read(RTC_SECONDS);
	min = vrtc_cmos_read(RTC_MINUTES);
	hour = vrtc_cmos_read(RTC_HOURS);
	mday = vrtc_cmos_read(RTC_DAY_OF_MONTH);
	mon = vrtc_cmos_read(RTC_MONTH);
	year = vrtc_cmos_read(RTC_YEAR);

	spin_unlock_irqrestore(&rtc_lock, flags);

	/* vRTC YEAR reg contains the offset to 1972 */
	year += 1972;

	printk(KERN_INFO "vRTC: sec: %d min: %d hour: %d day: %d "
		"mon: %d year: %d\n", sec, min, hour, mday, mon, year);

	return mktime(year, mon, mday, hour, min, sec);
}

int vrtc_set_mmss(unsigned long nowtime)
{
	unsigned long flags;
	struct rtc_time tm;
	int year;
	int retval = 0;

	rtc_time_to_tm(nowtime, &tm);
	if (!rtc_valid_tm(&tm) && tm.tm_year >= 72) {
		/*
		 * tm.year is the number of years since 1900, and the
		 * vrtc need the years since 1972.
		 */
		year = tm.tm_year - 72;
		spin_lock_irqsave(&rtc_lock, flags);
		vrtc_cmos_write(year, RTC_YEAR);
		vrtc_cmos_write(tm.tm_mon, RTC_MONTH);
		vrtc_cmos_write(tm.tm_mday, RTC_DAY_OF_MONTH);
		vrtc_cmos_write(tm.tm_hour, RTC_HOURS);
		vrtc_cmos_write(tm.tm_min, RTC_MINUTES);
		vrtc_cmos_write(tm.tm_sec, RTC_SECONDS);
		spin_unlock_irqrestore(&rtc_lock, flags);
	} else {
		printk(KERN_ERR
		       "%s: Invalid vRTC value: write of %lx to vRTC failed\n",
			__FUNCTION__, nowtime);
		retval = -EINVAL;
	}
	return retval;
}

void __init mrst_rtc_init(void)
{
	unsigned long vrtc_paddr;

	sfi_table_parse(SFI_SIG_MRTC, NULL, NULL, sfi_parse_mrtc);

	vrtc_paddr = sfi_mrtc_array[0].phys_addr;
	if (!sfi_mrtc_num || !vrtc_paddr)
		return;

	vrtc_virt_base = (void __iomem *)set_fixmap_offset_nocache(FIX_LNW_VRTC,
								vrtc_paddr);
	x86_platform.get_wallclock = vrtc_get_time;
	x86_platform.set_wallclock = vrtc_set_mmss;
}

/*
 * The Moorestown platform has a memory mapped virtual RTC device that emulates
 * the programming interface of the RTC.
 */

static struct resource vrtc_resources[] = {
	[0] = {
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.flags	= IORESOURCE_IRQ,
	}
};

static struct platform_device vrtc_device = {
	.name		= "rtc_mrst",
	.id		= -1,
	.resource	= vrtc_resources,
	.num_resources	= ARRAY_SIZE(vrtc_resources),
};

/* Register the RTC device if appropriate */
static int __init mrst_device_create(void)
{
	/* No Moorestown, no device */
	if (!mrst_identify_cpu())
		return -ENODEV;
	/* No timer, no device */
	if (!sfi_mrtc_num)
		return -ENODEV;

	/* iomem resource */
	vrtc_resources[0].start = sfi_mrtc_array[0].phys_addr;
	vrtc_resources[0].end = sfi_mrtc_array[0].phys_addr +
				MRST_VRTC_MAP_SZ;
	/* irq resource */
	vrtc_resources[1].start = sfi_mrtc_array[0].irq;
	vrtc_resources[1].end = sfi_mrtc_array[0].irq;

	return platform_device_register(&vrtc_device);
}

module_init(mrst_device_create);
