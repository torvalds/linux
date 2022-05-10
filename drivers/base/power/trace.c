// SPDX-License-Identifier: GPL-2.0
/*
 * drivers/base/power/trace.c
 *
 * Copyright (C) 2006 Linus Torvalds
 *
 * Trace facility for suspend/resume problems, when none of the
 * devices may be working.
 */
#define pr_fmt(fmt) "PM: " fmt

#include <linux/pm-trace.h>
#include <linux/export.h>
#include <linux/rtc.h>
#include <linux/suspend.h>
#include <linux/init.h>

#include <linux/mc146818rtc.h>

#include "power.h"

/*
 * Horrid, horrid, horrid.
 *
 * It turns out that the _only_ piece of hardware that actually
 * keeps its value across a hard boot (and, more importantly, the
 * POST init sequence) is literally the realtime clock.
 *
 * Never mind that an RTC chip has 114 bytes (and often a whole
 * other bank of an additional 128 bytes) of nice SRAM that is
 * _designed_ to keep data - the POST will clear it. So we literally
 * can just use the few bytes of actual time data, which means that
 * we're really limited.
 *
 * It means, for example, that we can't use the seconds at all
 * (since the time between the hang and the boot might be more
 * than a minute), and we'd better not depend on the low bits of
 * the minutes either.
 *
 * There are the wday fields etc, but I wouldn't guarantee those
 * are dependable either. And if the date isn't valid, either the
 * hw or POST will do strange things.
 *
 * So we're left with:
 *  - year: 0-99
 *  - month: 0-11
 *  - day-of-month: 1-28
 *  - hour: 0-23
 *  - min: (0-30)*2
 *
 * Giving us a total range of 0-16128000 (0xf61800), ie less
 * than 24 bits of actual data we can save across reboots.
 *
 * And if your box can't boot in less than three minutes,
 * you're screwed.
 *
 * Now, almost 24 bits of data is pitifully small, so we need
 * to be pretty dense if we want to use it for anything nice.
 * What we do is that instead of saving off nice readable info,
 * we save off _hashes_ of information that we can hopefully
 * regenerate after the reboot.
 *
 * In particular, this means that we might be unlucky, and hit
 * a case where we have a hash collision, and we end up not
 * being able to tell for certain exactly which case happened.
 * But that's hopefully unlikely.
 *
 * What we do is to take the bits we can fit, and split them
 * into three parts (16*997*1009 = 16095568), and use the values
 * for:
 *  - 0-15: user-settable
 *  - 0-996: file + line number
 *  - 0-1008: device
 */
#define USERHASH (16)
#define FILEHASH (997)
#define DEVHASH (1009)

#define DEVSEED (7919)

bool pm_trace_rtc_abused __read_mostly;
EXPORT_SYMBOL_GPL(pm_trace_rtc_abused);

static unsigned int dev_hash_value;

static int set_magic_time(unsigned int user, unsigned int file, unsigned int device)
{
	unsigned int n = user + USERHASH*(file + FILEHASH*device);

	// June 7th, 2006
	static struct rtc_time time = {
		.tm_sec = 0,
		.tm_min = 0,
		.tm_hour = 0,
		.tm_mday = 7,
		.tm_mon = 5,	// June - counting from zero
		.tm_year = 106,
		.tm_wday = 3,
		.tm_yday = 160,
		.tm_isdst = 1
	};

	time.tm_year = (n % 100);
	n /= 100;
	time.tm_mon = (n % 12);
	n /= 12;
	time.tm_mday = (n % 28) + 1;
	n /= 28;
	time.tm_hour = (n % 24);
	n /= 24;
	time.tm_min = (n % 20) * 3;
	n /= 20;
	mc146818_set_time(&time);
	pm_trace_rtc_abused = true;
	return n ? -1 : 0;
}

static unsigned int read_magic_time(void)
{
	struct rtc_time time;
	unsigned int val;

	if (mc146818_get_time(&time) < 0) {
		pr_err("Unable to read current time from RTC\n");
		return 0;
	}

	pr_info("RTC time: %ptRt, date: %ptRd\n", &time, &time);
	val = time.tm_year;				/* 100 years */
	if (val > 100)
		val -= 100;
	val += time.tm_mon * 100;			/* 12 months */
	val += (time.tm_mday-1) * 100 * 12;		/* 28 month-days */
	val += time.tm_hour * 100 * 12 * 28;		/* 24 hours */
	val += (time.tm_min / 3) * 100 * 12 * 28 * 24;	/* 20 3-minute intervals */
	return val;
}

/*
 * This is just the sdbm hash function with a user-supplied
 * seed and final size parameter.
 */
static unsigned int hash_string(unsigned int seed, const char *data, unsigned int mod)
{
	unsigned char c;
	while ((c = *data++) != 0) {
		seed = (seed << 16) + (seed << 6) - seed + c;
	}
	return seed % mod;
}

void set_trace_device(struct device *dev)
{
	dev_hash_value = hash_string(DEVSEED, dev_name(dev), DEVHASH);
}
EXPORT_SYMBOL(set_trace_device);

/*
 * We could just take the "tracedata" index into the .tracedata
 * section instead. Generating a hash of the data gives us a
 * chance to work across kernel versions, and perhaps more
 * importantly it also gives us valid/invalid check (ie we will
 * likely not give totally bogus reports - if the hash matches,
 * it's not any guarantee, but it's a high _likelihood_ that
 * the match is valid).
 */
void generate_pm_trace(const void *tracedata, unsigned int user)
{
	unsigned short lineno = *(unsigned short *)tracedata;
	const char *file = *(const char **)(tracedata + 2);
	unsigned int user_hash_value, file_hash_value;

	if (!x86_platform.legacy.rtc)
		return;

	user_hash_value = user % USERHASH;
	file_hash_value = hash_string(lineno, file, FILEHASH);
	set_magic_time(user_hash_value, file_hash_value, dev_hash_value);
}
EXPORT_SYMBOL(generate_pm_trace);

extern char __tracedata_start[], __tracedata_end[];
static int show_file_hash(unsigned int value)
{
	int match;
	char *tracedata;

	match = 0;
	for (tracedata = __tracedata_start ; tracedata < __tracedata_end ;
			tracedata += 2 + sizeof(unsigned long)) {
		unsigned short lineno = *(unsigned short *)tracedata;
		const char *file = *(const char **)(tracedata + 2);
		unsigned int hash = hash_string(lineno, file, FILEHASH);
		if (hash != value)
			continue;
		pr_info("  hash matches %s:%u\n", file, lineno);
		match++;
	}
	return match;
}

static int show_dev_hash(unsigned int value)
{
	int match = 0;
	struct list_head *entry;

	device_pm_lock();
	entry = dpm_list.prev;
	while (entry != &dpm_list) {
		struct device * dev = to_device(entry);
		unsigned int hash = hash_string(DEVSEED, dev_name(dev), DEVHASH);
		if (hash == value) {
			dev_info(dev, "hash matches\n");
			match++;
		}
		entry = entry->prev;
	}
	device_pm_unlock();
	return match;
}

static unsigned int hash_value_early_read;

int show_trace_dev_match(char *buf, size_t size)
{
	unsigned int value = hash_value_early_read / (USERHASH * FILEHASH);
	int ret = 0;
	struct list_head *entry;

	/*
	 * It's possible that multiple devices will match the hash and we can't
	 * tell which is the culprit, so it's best to output them all.
	 */
	device_pm_lock();
	entry = dpm_list.prev;
	while (size && entry != &dpm_list) {
		struct device *dev = to_device(entry);
		unsigned int hash = hash_string(DEVSEED, dev_name(dev),
						DEVHASH);
		if (hash == value) {
			int len = snprintf(buf, size, "%s\n",
					    dev_driver_string(dev));
			if (len > size)
				len = size;
			buf += len;
			ret += len;
			size -= len;
		}
		entry = entry->prev;
	}
	device_pm_unlock();
	return ret;
}

static int
pm_trace_notify(struct notifier_block *nb, unsigned long mode, void *_unused)
{
	switch (mode) {
	case PM_POST_HIBERNATION:
	case PM_POST_SUSPEND:
		if (pm_trace_rtc_abused) {
			pm_trace_rtc_abused = false;
			pr_warn("Possible incorrect RTC due to pm_trace, please use 'ntpdate' or 'rdate' to reset it.\n");
		}
		break;
	default:
		break;
	}
	return 0;
}

static struct notifier_block pm_trace_nb = {
	.notifier_call = pm_trace_notify,
};

static int __init early_resume_init(void)
{
	if (!x86_platform.legacy.rtc)
		return 0;

	hash_value_early_read = read_magic_time();
	register_pm_notifier(&pm_trace_nb);
	return 0;
}

static int __init late_resume_init(void)
{
	unsigned int val = hash_value_early_read;
	unsigned int user, file, dev;

	if (!x86_platform.legacy.rtc)
		return 0;

	user = val % USERHASH;
	val = val / USERHASH;
	file = val % FILEHASH;
	val = val / FILEHASH;
	dev = val /* % DEVHASH */;

	pr_info("  Magic number: %d:%d:%d\n", user, file, dev);
	show_file_hash(file);
	show_dev_hash(dev);
	return 0;
}

core_initcall(early_resume_init);
late_initcall(late_resume_init);
