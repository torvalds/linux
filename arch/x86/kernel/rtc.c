// SPDX-License-Identifier: GPL-2.0
/*
 * RTC related functions
 */
#include <linux/platform_device.h>
#include <linux/mc146818rtc.h>
#include <linux/export.h>
#include <linux/pnp.h>

#include <asm/vsyscall.h>
#include <asm/x86_init.h>
#include <asm/time.h>
#include <asm/intel-mid.h>
#include <asm/setup.h>

#ifdef CONFIG_X86_32
/*
 * This is a special lock that is owned by the CPU and holds the index
 * register we are working with.  It is required for NMI access to the
 * CMOS/RTC registers.  See arch/x86/include/asm/mc146818rtc.h for details.
 */
volatile unsigned long cmos_lock;
EXPORT_SYMBOL(cmos_lock);
#endif /* CONFIG_X86_32 */

DEFINE_SPINLOCK(rtc_lock);
EXPORT_SYMBOL(rtc_lock);

/*
 * In order to set the CMOS clock precisely, mach_set_cmos_time has to be
 * called 500 ms after the second nowtime has started, because when
 * nowtime is written into the registers of the CMOS clock, it will
 * jump to the next second precisely 500 ms later. Check the Motorola
 * MC146818A or Dallas DS12887 data sheet for details.
 */
int mach_set_cmos_time(const struct timespec64 *now)
{
	unsigned long long nowtime = now->tv_sec;
	struct rtc_time tm;
	int retval = 0;

	rtc_time64_to_tm(nowtime, &tm);
	if (!rtc_valid_tm(&tm)) {
		retval = mc146818_set_time(&tm);
		if (retval)
			printk(KERN_ERR "%s: RTC write failed with error %d\n",
			       __func__, retval);
	} else {
		printk(KERN_ERR
		       "%s: Invalid RTC value: write of %llx to RTC failed\n",
			__func__, nowtime);
		retval = -EINVAL;
	}
	return retval;
}

void mach_get_cmos_time(struct timespec64 *now)
{
	struct rtc_time tm;

	/*
	 * If pm_trace abused the RTC as storage, set the timespec to 0,
	 * which tells the caller that this RTC value is unusable.
	 */
	if (!pm_trace_rtc_valid()) {
		now->tv_sec = now->tv_nsec = 0;
		return;
	}

	if (mc146818_get_time(&tm, 1000)) {
		pr_err("Unable to read current time from RTC\n");
		now->tv_sec = now->tv_nsec = 0;
		return;
	}

	now->tv_sec = rtc_tm_to_time64(&tm);
	now->tv_nsec = 0;
}

/* Routines for accessing the CMOS RAM/RTC. */
unsigned char rtc_cmos_read(unsigned char addr)
{
	unsigned char val;

	lock_cmos_prefix(addr);
	outb(addr, RTC_PORT(0));
	val = inb(RTC_PORT(1));
	lock_cmos_suffix(addr);

	return val;
}
EXPORT_SYMBOL(rtc_cmos_read);

void rtc_cmos_write(unsigned char val, unsigned char addr)
{
	lock_cmos_prefix(addr);
	outb(addr, RTC_PORT(0));
	outb(val, RTC_PORT(1));
	lock_cmos_suffix(addr);
}
EXPORT_SYMBOL(rtc_cmos_write);

int update_persistent_clock64(struct timespec64 now)
{
	return x86_platform.set_wallclock(&now);
}

/* not static: needed by APM */
void read_persistent_clock64(struct timespec64 *ts)
{
	x86_platform.get_wallclock(ts);
}


static struct resource rtc_resources[] = {
	[0] = {
		.start	= RTC_PORT(0),
		.end	= RTC_PORT(1),
		.flags	= IORESOURCE_IO,
	},
	[1] = {
		.start	= RTC_IRQ,
		.end	= RTC_IRQ,
		.flags	= IORESOURCE_IRQ,
	}
};

static struct platform_device rtc_device = {
	.name		= "rtc_cmos",
	.id		= -1,
	.resource	= rtc_resources,
	.num_resources	= ARRAY_SIZE(rtc_resources),
};

static __init int add_rtc_cmos(void)
{
#ifdef CONFIG_PNP
	static const char * const ids[] __initconst =
	    { "PNP0b00", "PNP0b01", "PNP0b02", };
	struct pnp_dev *dev;
	int i;

	pnp_for_each_dev(dev) {
		for (i = 0; i < ARRAY_SIZE(ids); i++) {
			if (compare_pnp_id(dev->id, ids[i]) != 0)
				return 0;
		}
	}
#endif
	if (!x86_platform.legacy.rtc)
		return -ENODEV;

	platform_device_register(&rtc_device);
	dev_info(&rtc_device.dev,
		 "registered platform RTC device (no PNP device found)\n");

	return 0;
}
device_initcall(add_rtc_cmos);
