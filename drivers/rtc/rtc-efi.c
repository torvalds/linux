// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * rtc-efi: RTC Class Driver for EFI-based systems
 *
 * Copyright (C) 2009 Hewlett-Packard Development Company, L.P.
 *
 * Author: dann frazier <dannf@dannf.org>
 * Based on efirtc.c by Stephane Eranian
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/stringify.h>
#include <linux/time.h>
#include <linux/platform_device.h>
#include <linux/rtc.h>
#include <linux/efi.h>

#define EFI_ISDST (EFI_TIME_ADJUST_DAYLIGHT|EFI_TIME_IN_DAYLIGHT)

/*
 * returns day of the year [0-365]
 */
static inline int
compute_yday(efi_time_t *eft)
{
	/* efi_time_t.month is in the [1-12] so, we need -1 */
	return rtc_year_days(eft->day, eft->month - 1, eft->year);
}

/*
 * returns day of the week [0-6] 0=Sunday
 */
static int
compute_wday(efi_time_t *eft, int yday)
{
	int ndays = eft->year * (365 % 7)
		    + (eft->year - 1) / 4
		    - (eft->year - 1) / 100
		    + (eft->year - 1) / 400
		    + yday;

	/*
	 * 1/1/0000 may or may not have been a Sunday (if it ever existed at
	 * all) but assuming it was makes this calculation work correctly.
	 */
	return ndays % 7;
}

static void
convert_to_efi_time(struct rtc_time *wtime, efi_time_t *eft)
{
	eft->year	= wtime->tm_year + 1900;
	eft->month	= wtime->tm_mon + 1;
	eft->day	= wtime->tm_mday;
	eft->hour	= wtime->tm_hour;
	eft->minute	= wtime->tm_min;
	eft->second	= wtime->tm_sec;
	eft->nanosecond = 0;
	eft->daylight	= wtime->tm_isdst ? EFI_ISDST : 0;
	eft->timezone	= EFI_UNSPECIFIED_TIMEZONE;
}

static bool
convert_from_efi_time(efi_time_t *eft, struct rtc_time *wtime)
{
	memset(wtime, 0, sizeof(*wtime));

	if (eft->second >= 60)
		return false;
	wtime->tm_sec  = eft->second;

	if (eft->minute >= 60)
		return false;
	wtime->tm_min  = eft->minute;

	if (eft->hour >= 24)
		return false;
	wtime->tm_hour = eft->hour;

	if (!eft->day || eft->day > 31)
		return false;
	wtime->tm_mday = eft->day;

	if (!eft->month || eft->month > 12)
		return false;
	wtime->tm_mon  = eft->month - 1;

	if (eft->year < 1900 || eft->year > 9999)
		return false;
	wtime->tm_year = eft->year - 1900;

	/* day in the year [1-365]*/
	wtime->tm_yday = compute_yday(eft);

	/* day of the week [0-6], Sunday=0 */
	wtime->tm_wday = compute_wday(eft, wtime->tm_yday);

	switch (eft->daylight & EFI_ISDST) {
	case EFI_ISDST:
		wtime->tm_isdst = 1;
		break;
	case EFI_TIME_ADJUST_DAYLIGHT:
		wtime->tm_isdst = 0;
		break;
	default:
		wtime->tm_isdst = -1;
	}

	return true;
}

static int efi_read_alarm(struct device *dev, struct rtc_wkalrm *wkalrm)
{
	efi_time_t eft;
	efi_status_t status;

	/*
	 * As of EFI v1.10, this call always returns an unsupported status
	 */
	status = efi.get_wakeup_time((efi_bool_t *)&wkalrm->enabled,
				     (efi_bool_t *)&wkalrm->pending, &eft);

	if (status != EFI_SUCCESS)
		return -EINVAL;

	if (!convert_from_efi_time(&eft, &wkalrm->time))
		return -EIO;

	return rtc_valid_tm(&wkalrm->time);
}

static int efi_set_alarm(struct device *dev, struct rtc_wkalrm *wkalrm)
{
	efi_time_t eft;
	efi_status_t status;

	convert_to_efi_time(&wkalrm->time, &eft);

	/*
	 * XXX Fixme:
	 * As of EFI 0.92 with the firmware I have on my
	 * machine this call does not seem to work quite
	 * right
	 *
	 * As of v1.10, this call always returns an unsupported status
	 */
	status = efi.set_wakeup_time((efi_bool_t)wkalrm->enabled, &eft);

	dev_warn(dev, "write status is %d\n", (int)status);

	return status == EFI_SUCCESS ? 0 : -EINVAL;
}

static int efi_read_time(struct device *dev, struct rtc_time *tm)
{
	efi_status_t status;
	efi_time_t eft;
	efi_time_cap_t cap;

	status = efi.get_time(&eft, &cap);

	if (status != EFI_SUCCESS) {
		/* should never happen */
		dev_err(dev, "can't read time\n");
		return -EINVAL;
	}

	if (!convert_from_efi_time(&eft, tm))
		return -EIO;

	return 0;
}

static int efi_set_time(struct device *dev, struct rtc_time *tm)
{
	efi_status_t status;
	efi_time_t eft;

	convert_to_efi_time(tm, &eft);

	status = efi.set_time(&eft);

	return status == EFI_SUCCESS ? 0 : -EINVAL;
}

static int efi_procfs(struct device *dev, struct seq_file *seq)
{
	efi_time_t      eft, alm;
	efi_time_cap_t  cap;
	efi_bool_t      enabled, pending;

	memset(&eft, 0, sizeof(eft));
	memset(&alm, 0, sizeof(alm));
	memset(&cap, 0, sizeof(cap));

	efi.get_time(&eft, &cap);
	efi.get_wakeup_time(&enabled, &pending, &alm);

	seq_printf(seq,
		   "Time\t\t: %u:%u:%u.%09u\n"
		   "Date\t\t: %u-%u-%u\n"
		   "Daylight\t: %u\n",
		   eft.hour, eft.minute, eft.second, eft.nanosecond,
		   eft.year, eft.month, eft.day,
		   eft.daylight);

	if (eft.timezone == EFI_UNSPECIFIED_TIMEZONE)
		seq_puts(seq, "Timezone\t: unspecified\n");
	else
		/* XXX fixme: convert to string? */
		seq_printf(seq, "Timezone\t: %u\n", eft.timezone);

	seq_printf(seq,
		   "Alarm Time\t: %u:%u:%u.%09u\n"
		   "Alarm Date\t: %u-%u-%u\n"
		   "Alarm Daylight\t: %u\n"
		   "Enabled\t\t: %s\n"
		   "Pending\t\t: %s\n",
		   alm.hour, alm.minute, alm.second, alm.nanosecond,
		   alm.year, alm.month, alm.day,
		   alm.daylight,
		   enabled == 1 ? "yes" : "no",
		   pending == 1 ? "yes" : "no");

	if (eft.timezone == EFI_UNSPECIFIED_TIMEZONE)
		seq_puts(seq, "Timezone\t: unspecified\n");
	else
		/* XXX fixme: convert to string? */
		seq_printf(seq, "Timezone\t: %u\n", alm.timezone);

	/*
	 * now prints the capabilities
	 */
	seq_printf(seq,
		   "Resolution\t: %u\n"
		   "Accuracy\t: %u\n"
		   "SetstoZero\t: %u\n",
		   cap.resolution, cap.accuracy, cap.sets_to_zero);

	return 0;
}

static const struct rtc_class_ops efi_rtc_ops = {
	.read_time	= efi_read_time,
	.set_time	= efi_set_time,
	.read_alarm	= efi_read_alarm,
	.set_alarm	= efi_set_alarm,
	.proc		= efi_procfs,
};

static int __init efi_rtc_probe(struct platform_device *dev)
{
	struct rtc_device *rtc;
	efi_time_t eft;
	efi_time_cap_t cap;

	/* First check if the RTC is usable */
	if (efi.get_time(&eft, &cap) != EFI_SUCCESS)
		return -ENODEV;

	rtc = devm_rtc_device_register(&dev->dev, "rtc-efi", &efi_rtc_ops,
					THIS_MODULE);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	rtc->uie_unsupported = 1;
	platform_set_drvdata(dev, rtc);

	return 0;
}

static struct platform_driver efi_rtc_driver = {
	.driver = {
		.name = "rtc-efi",
	},
};

module_platform_driver_probe(efi_rtc_driver, efi_rtc_probe);

MODULE_ALIAS("platform:rtc-efi");
MODULE_AUTHOR("dann frazier <dannf@dannf.org>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("EFI RTC driver");
MODULE_ALIAS("platform:rtc-efi");
