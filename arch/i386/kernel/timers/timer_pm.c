/*
 * (C) Dominik Brodowski <linux@brodo.de> 2003
 *
 * Driver to use the Power Management Timer (PMTMR) available in some
 * southbridges as primary timing source for the Linux kernel.
 *
 * Based on parts of linux/drivers/acpi/hardware/hwtimer.c, timer_pit.c,
 * timer_hpet.c, and on Arjan van de Ven's implementation for 2.4.
 *
 * This file is licensed under the GPL v2.
 */


#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <asm/types.h>
#include <asm/timer.h>
#include <asm/smp.h>
#include <asm/io.h>
#include <asm/arch_hooks.h>

#include <linux/timex.h>
#include "mach_timer.h"

/* Number of PMTMR ticks expected during calibration run */
#define PMTMR_TICKS_PER_SEC 3579545
#define PMTMR_EXPECTED_RATE \
  ((CALIBRATE_LATCH * (PMTMR_TICKS_PER_SEC >> 10)) / (CLOCK_TICK_RATE>>10))


/* The I/O port the PMTMR resides at.
 * The location is detected during setup_arch(),
 * in arch/i386/acpi/boot.c */
u32 pmtmr_ioport = 0;


/* value of the Power timer at last timer interrupt */
static u32 offset_tick;
static u32 offset_delay;

static unsigned long long monotonic_base;
static seqlock_t monotonic_lock = SEQLOCK_UNLOCKED;

#define ACPI_PM_MASK 0xFFFFFF /* limit it to 24 bits */

static int pmtmr_need_workaround __read_mostly = 1;

/*helper function to safely read acpi pm timesource*/
static inline u32 read_pmtmr(void)
{
	if (pmtmr_need_workaround) {
		u32 v1, v2, v3;

		/* It has been reported that because of various broken
		 * chipsets (ICH4, PIIX4 and PIIX4E) where the ACPI PM time
		 * source is not latched, so you must read it multiple
		 * times to insure a safe value is read.
		 */
		do {
			v1 = inl(pmtmr_ioport);
			v2 = inl(pmtmr_ioport);
			v3 = inl(pmtmr_ioport);
		} while ((v1 > v2 && v1 < v3) || (v2 > v3 && v2 < v1)
			 || (v3 > v1 && v3 < v2));

		/* mask the output to 24 bits */
		return v2 & ACPI_PM_MASK;
	}

	return inl(pmtmr_ioport) & ACPI_PM_MASK;
}


/*
 * Some boards have the PMTMR running way too fast. We check
 * the PMTMR rate against PIT channel 2 to catch these cases.
 */
static int verify_pmtmr_rate(void)
{
	u32 value1, value2;
	unsigned long count, delta;

	mach_prepare_counter();
	value1 = read_pmtmr();
	mach_countup(&count);
	value2 = read_pmtmr();
	delta = (value2 - value1) & ACPI_PM_MASK;

	/* Check that the PMTMR delta is within 5% of what we expect */
	if (delta < (PMTMR_EXPECTED_RATE * 19) / 20 ||
	    delta > (PMTMR_EXPECTED_RATE * 21) / 20) {
		printk(KERN_INFO "PM-Timer running at invalid rate: %lu%% of normal - aborting.\n", 100UL * delta / PMTMR_EXPECTED_RATE);
		return -1;
	}

	return 0;
}


static int init_pmtmr(char* override)
{
	u32 value1, value2;
	unsigned int i;

 	if (override[0] && strncmp(override,"pmtmr",5))
		return -ENODEV;

	if (!pmtmr_ioport)
		return -ENODEV;

	/* we use the TSC for delay_pmtmr, so make sure it exists */
	if (!cpu_has_tsc)
		return -ENODEV;

	/* "verify" this timing source */
	value1 = read_pmtmr();
	for (i = 0; i < 10000; i++) {
		value2 = read_pmtmr();
		if (value2 == value1)
			continue;
		if (value2 > value1)
			goto pm_good;
		if ((value2 < value1) && ((value2) < 0xFFF))
			goto pm_good;
		printk(KERN_INFO "PM-Timer had inconsistent results: 0x%#x, 0x%#x - aborting.\n", value1, value2);
		return -EINVAL;
	}
	printk(KERN_INFO "PM-Timer had no reasonable result: 0x%#x - aborting.\n", value1);
	return -ENODEV;

pm_good:
	if (verify_pmtmr_rate() != 0)
		return -ENODEV;

	init_cpu_khz();
	return 0;
}

static inline u32 cyc2us(u32 cycles)
{
	/* The Power Management Timer ticks at 3.579545 ticks per microsecond.
	 * 1 / PM_TIMER_FREQUENCY == 0.27936511 =~ 286/1024 [error: 0.024%]
	 *
	 * Even with HZ = 100, delta is at maximum 35796 ticks, so it can
	 * easily be multiplied with 286 (=0x11E) without having to fear
	 * u32 overflows.
	 */
	cycles *= 286;
	return (cycles >> 10);
}

/*
 * this gets called during each timer interrupt
 *   - Called while holding the writer xtime_lock
 */
static void mark_offset_pmtmr(void)
{
	u32 lost, delta, last_offset;
	static int first_run = 1;
	last_offset = offset_tick;

	write_seqlock(&monotonic_lock);

	offset_tick = read_pmtmr();

	/* calculate tick interval */
	delta = (offset_tick - last_offset) & ACPI_PM_MASK;

	/* convert to usecs */
	delta = cyc2us(delta);

	/* update the monotonic base value */
	monotonic_base += delta * NSEC_PER_USEC;
	write_sequnlock(&monotonic_lock);

	/* convert to ticks */
	delta += offset_delay;
	lost = delta / (USEC_PER_SEC / HZ);
	offset_delay = delta % (USEC_PER_SEC / HZ);


	/* compensate for lost ticks */
	if (lost >= 2)
		jiffies_64 += lost - 1;

	/* don't calculate delay for first run,
	   or if we've got less then a tick */
	if (first_run || (lost < 1)) {
		first_run = 0;
		offset_delay = 0;
	}
}

static int pmtmr_resume(void)
{
	write_seqlock(&monotonic_lock);
	/* Assume this is the last mark offset time */
	offset_tick = read_pmtmr();
	write_sequnlock(&monotonic_lock);
	return 0;
}

static unsigned long long monotonic_clock_pmtmr(void)
{
	u32 last_offset, this_offset;
	unsigned long long base, ret;
	unsigned seq;


	/* atomically read monotonic base & last_offset */
	do {
		seq = read_seqbegin(&monotonic_lock);
		last_offset = offset_tick;
		base = monotonic_base;
	} while (read_seqretry(&monotonic_lock, seq));

	/* Read the pmtmr */
	this_offset =  read_pmtmr();

	/* convert to nanoseconds */
	ret = (this_offset - last_offset) & ACPI_PM_MASK;
	ret = base + (cyc2us(ret) * NSEC_PER_USEC);
	return ret;
}

static void delay_pmtmr(unsigned long loops)
{
	unsigned long bclock, now;

	rdtscl(bclock);
	do
	{
		rep_nop();
		rdtscl(now);
	} while ((now-bclock) < loops);
}


/*
 * get the offset (in microseconds) from the last call to mark_offset()
 *	- Called holding a reader xtime_lock
 */
static unsigned long get_offset_pmtmr(void)
{
	u32 now, offset, delta = 0;

	offset = offset_tick;
	now = read_pmtmr();
	delta = (now - offset)&ACPI_PM_MASK;

	return (unsigned long) offset_delay + cyc2us(delta);
}


/* acpi timer_opts struct */
static struct timer_opts timer_pmtmr = {
	.name			= "pmtmr",
	.mark_offset		= mark_offset_pmtmr,
	.get_offset		= get_offset_pmtmr,
	.monotonic_clock 	= monotonic_clock_pmtmr,
	.delay 			= delay_pmtmr,
	.read_timer 		= read_timer_tsc,
	.resume			= pmtmr_resume,
};

struct init_timer_opts __initdata timer_pmtmr_init = {
	.init = init_pmtmr,
	.opts = &timer_pmtmr,
};

#ifdef CONFIG_PCI
/*
 * PIIX4 Errata:
 *
 * The power management timer may return improper results when read.
 * Although the timer value settles properly after incrementing,
 * while incrementing there is a 3 ns window every 69.8 ns where the
 * timer value is indeterminate (a 4.2% chance that the data will be
 * incorrect when read). As a result, the ACPI free running count up
 * timer specification is violated due to erroneous reads.
 */
static int __init pmtmr_bug_check(void)
{
	static struct pci_device_id gray_list[] __initdata = {
		/* these chipsets may have bug. */
		{ PCI_DEVICE(PCI_VENDOR_ID_INTEL,
				PCI_DEVICE_ID_INTEL_82801DB_0) },
		{ },
	};
	struct pci_dev *dev;
	int pmtmr_has_bug = 0;
	u8 rev;

	if (cur_timer != &timer_pmtmr || !pmtmr_need_workaround)
		return 0;

	dev = pci_get_device(PCI_VENDOR_ID_INTEL,
			     PCI_DEVICE_ID_INTEL_82371AB_3, NULL);
	if (dev) {
		pci_read_config_byte(dev, PCI_REVISION_ID, &rev);
		/* the bug has been fixed in PIIX4M */
		if (rev < 3) {
			printk(KERN_WARNING "* Found PM-Timer Bug on this "
				"chipset. Due to workarounds for a bug,\n"
				"* this time source is slow.  Consider trying "
				"other time sources (clock=)\n");
			pmtmr_has_bug = 1;
		}
		pci_dev_put(dev);
	}

	if (pci_dev_present(gray_list)) {
		printk(KERN_WARNING "* This chipset may have PM-Timer Bug.  Due"
			" to workarounds for a bug,\n"
			"* this time source is slow. If you are sure your timer"
			" does not have\n"
			"* this bug, please use \"pmtmr_good\" to disable the "
			"workaround\n");
		pmtmr_has_bug = 1;
	}

	if (!pmtmr_has_bug)
		pmtmr_need_workaround = 0;

	return 0;
}
device_initcall(pmtmr_bug_check);
#endif

static int __init pmtr_good_setup(char *__str)
{
	pmtmr_need_workaround = 0;
	return 1;
}
__setup("pmtmr_good", pmtr_good_setup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Dominik Brodowski <linux@brodo.de>");
MODULE_DESCRIPTION("Power Management Timer (PMTMR) as primary timing source for x86");
