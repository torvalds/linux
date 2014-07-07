/*
 * RTC related functions
 */
#include <linux/platform_device.h>
#include <linux/mc146818rtc.h>
#include <linux/acpi.h>
#include <linux/bcd.h>
#include <linux/export.h>
#include <linux/pnp.h>
#include <linux/of.h>

#include <asm/vsyscall.h>
#include <asm/x86_init.h>
#include <asm/time.h>
#include <asm/intel-mid.h>
#include <asm/rtc.h>

#ifdef CONFIG_X86_32
/*
 * This is a special lock that is owned by the CPU and holds the index
 * register we are working with.  It is required for NMI access to the
 * CMOS/RTC registers.  See include/asm-i386/mc146818rtc.h for details.
 */
volatile unsigned long cmos_lock;
EXPORT_SYMBOL(cmos_lock);
#endif /* CONFIG_X86_32 */

/* For two digit years assume time is always after that */
#define CMOS_YEARS_OFFS 2000

DEFINE_SPINLOCK(rtc_lock);
EXPORT_SYMBOL(rtc_lock);

/*
 * In order to set the CMOS clock precisely, set_rtc_mmss has to be
 * called 500 ms after the second nowtime has started, because when
 * nowtime is written into the registers of the CMOS clock, it will
 * jump to the next second precisely 500 ms later. Check the Motorola
 * MC146818A or Dallas DS12887 data sheet for details.
 */
int mach_set_rtc_mmss(const struct timespec *now)
{
	unsigned long nowtime = now->tv_sec;
	struct rtc_time tm;
	int retval = 0;

	rtc_time_to_tm(nowtime, &tm);
	if (!rtc_valid_tm(&tm)) {
		retval = set_rtc_time(&tm);
		if (retval)
			printk(KERN_ERR "%s: RTC write failed with error %d\n",
			       __FUNCTION__, retval);
	} else {
		printk(KERN_ERR
		       "%s: Invalid RTC value: write of %lx to RTC failed\n",
			__FUNCTION__, nowtime);
		retval = -EINVAL;
	}
	return retval;
}

void mach_get_cmos_time(struct timespec *now)
{
	unsigned int status, year, mon, day, hour, min, sec, century = 0;
	unsigned long flags;

	spin_lock_irqsave(&rtc_lock, flags);

	/*
	 * If UIP is clear, then we have >= 244 microseconds before
	 * RTC registers will be updated.  Spec sheet says that this
	 * is the reliable way to read RTC - registers. If UIP is set
	 * then the register access might be invalid.
	 */
	while ((CMOS_READ(RTC_FREQ_SELECT) & RTC_UIP))
		cpu_relax();

	sec = CMOS_READ(RTC_SECONDS);
	min = CMOS_READ(RTC_MINUTES);
	hour = CMOS_READ(RTC_HOURS);
	day = CMOS_READ(RTC_DAY_OF_MONTH);
	mon = CMOS_READ(RTC_MONTH);
	year = CMOS_READ(RTC_YEAR);

#ifdef CONFIG_ACPI
	if (acpi_gbl_FADT.header.revision >= FADT2_REVISION_ID &&
	    acpi_gbl_FADT.century)
		century = CMOS_READ(acpi_gbl_FADT.century);
#endif

	status = CMOS_READ(RTC_CONTROL);
	WARN_ON_ONCE(RTC_ALWAYS_BCD && (status & RTC_DM_BINARY));

	spin_unlock_irqrestore(&rtc_lock, flags);

	if (RTC_ALWAYS_BCD || !(status & RTC_DM_BINARY)) {
		sec = bcd2bin(sec);
		min = bcd2bin(min);
		hour = bcd2bin(hour);
		day = bcd2bin(day);
		mon = bcd2bin(mon);
		year = bcd2bin(year);
	}

	if (century) {
		century = bcd2bin(century);
		year += century * 100;
	} else
		year += CMOS_YEARS_OFFS;

	now->tv_sec = mktime(year, mon, day, hour, min, sec);
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

int update_persistent_clock(struct timespec now)
{
	return x86_platform.set_wallclock(&now);
}

/* not static: needed by APM */
void read_persistent_clock(struct timespec *ts)
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
	static const char * const  const ids[] __initconst =
	    { "PNP0b00", "PNP0b01", "PNP0b02", };
	struct pnp_dev *dev;
	struct pnp_id *id;
	int i;

	pnp_for_each_dev(dev) {
		for (id = dev->id; id; id = id->next) {
			for (i = 0; i < ARRAY_SIZE(ids); i++) {
				if (compare_pnp_id(id, ids[i]) != 0)
					return 0;
			}
		}
	}
#endif
	if (of_have_populated_dt())
		return 0;

	/* Intel MID platforms don't have ioport rtc */
	if (intel_mid_identify_cpu())
		return -ENODEV;

#ifdef CONFIG_ACPI
	if (acpi_gbl_FADT.boot_flags & ACPI_FADT_NO_CMOS_RTC) {
		/* This warning can likely go away again in a year or two. */
		pr_info("ACPI: not registering RTC platform device\n");
		return -ENODEV;
	}
#endif

	platform_device_register(&rtc_device);
	dev_info(&rtc_device.dev,
		 "registered platform RTC device (no PNP device found)\n");

	return 0;
}
device_initcall(add_rtc_cmos);
