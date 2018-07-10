/*
 * rtcwake -- enter a system sleep state until specified wakeup time.
 *
 * This version was taken from util-linux and scrubbed down for busybox.
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 *
 * This uses cross-platform Linux interfaces to enter a system sleep state,
 * and leave it no later than a specified time.  It uses any RTC framework
 * driver that supports standard driver model wakeup flags.
 *
 * This is normally used like the old "apmsleep" utility, to wake from a
 * suspend state like ACPI S1 (standby) or S3 (suspend-to-RAM).  Most
 * platforms can implement those without analogues of BIOS, APM, or ACPI.
 *
 * On some systems, this can also be used like "nvram-wakeup", waking
 * from states like ACPI S4 (suspend to disk).  Not all systems have
 * persistent media that are appropriate for such suspend modes.
 *
 * The best way to set the system's RTC is so that it holds the current
 * time in UTC.  Use the "-l" flag to tell this program that the system
 * RTC uses a local timezone instead (maybe you dual-boot MS-Windows).
 * That flag should not be needed on systems with adjtime support.
 */
//config:config RTCWAKE
//config:	bool "rtcwake (6.4 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	Enter a system sleep state until specified wakeup time.

//applet:IF_RTCWAKE(APPLET(rtcwake, BB_DIR_USR_SBIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_RTCWAKE) += rtcwake.o

//usage:#define rtcwake_trivial_usage
//usage:       "[-a | -l | -u] [-d DEV] [-m MODE] [-s SEC | -t TIME]"
//usage:#define rtcwake_full_usage "\n\n"
//usage:       "Enter a system sleep state until specified wakeup time\n"
//usage:	IF_LONG_OPTS(
//usage:     "\n	-a,--auto	Read clock mode from adjtime"
//usage:     "\n	-l,--local	Clock is set to local time"
//usage:     "\n	-u,--utc	Clock is set to UTC time"
//usage:     "\n	-d,--device DEV	Specify the RTC device"
//usage:     "\n	-m,--mode MODE	Set sleep state (default: standby)"
//usage:     "\n	-s,--seconds SEC Set timeout in SEC seconds from now"
//usage:     "\n	-t,--time TIME	Set timeout to TIME seconds from epoch"
//usage:	)
//usage:	IF_NOT_LONG_OPTS(
//usage:     "\n	-a	Read clock mode from adjtime"
//usage:     "\n	-l	Clock is set to local time"
//usage:     "\n	-u	Clock is set to UTC time"
//usage:     "\n	-d DEV	Specify the RTC device"
//usage:     "\n	-m MODE	Set sleep state (default: standby)"
//usage:     "\n	-s SEC	Set timeout in SEC seconds from now"
//usage:     "\n	-t TIME	Set timeout to TIME seconds from epoch"
//usage:	)

#include "libbb.h"
#include "rtc_.h"

#define SYS_RTC_PATH   "/sys/class/rtc/%s/device/power/wakeup"
#define SYS_POWER_PATH "/sys/power/state"

static NOINLINE bool may_wakeup(const char *rtcname)
{
	ssize_t ret;
	char buf[128];

	/* strip "/dev/" from the rtcname here */
	rtcname = skip_dev_pfx(rtcname);

	snprintf(buf, sizeof(buf), SYS_RTC_PATH, rtcname);
	ret = open_read_close(buf, buf, sizeof(buf));
	if (ret < 0)
		return false;

	/* wakeup events could be disabled or not supported */
	return is_prefixed_with(buf, "enabled\n") != NULL;
}

static NOINLINE void setup_alarm(int fd, time_t *wakeup, time_t rtc_time)
{
	struct tm *ptm;
	struct linux_rtc_wkalrm wake;

	/* The wakeup time is in POSIX time (more or less UTC).
	 * Ideally RTCs use that same time; but PCs can't do that
	 * if they need to boot MS-Windows.  Messy...
	 *
	 * When running in utc mode this process's timezone is UTC,
	 * so we'll pass a UTC date to the RTC.
	 *
	 * Else mode is local so the time given to the RTC
	 * will instead use the local time zone.
	 */
	ptm = localtime(wakeup);

	wake.time.tm_sec = ptm->tm_sec;
	wake.time.tm_min = ptm->tm_min;
	wake.time.tm_hour = ptm->tm_hour;
	wake.time.tm_mday = ptm->tm_mday;
	wake.time.tm_mon = ptm->tm_mon;
	wake.time.tm_year = ptm->tm_year;
	/* wday, yday, and isdst fields are unused by Linux */
	wake.time.tm_wday = -1;
	wake.time.tm_yday = -1;
	wake.time.tm_isdst = -1;

	/* many rtc alarms only support up to 24 hours from 'now',
	 * so use the "more than 24 hours" request only if we must
	 */
	if ((rtc_time + (24 * 60 * 60)) > *wakeup) {
		xioctl(fd, RTC_ALM_SET, &wake.time);
		xioctl(fd, RTC_AIE_ON, 0);
	} else {
		/* avoid an extra AIE_ON call */
		wake.enabled = 1;
		xioctl(fd, RTC_WKALM_SET, &wake);
	}
}

#define RTCWAKE_OPT_AUTO         0x01
#define RTCWAKE_OPT_LOCAL        0x02
#define RTCWAKE_OPT_UTC          0x04
#define RTCWAKE_OPT_DEVICE       0x08
#define RTCWAKE_OPT_SUSPEND_MODE 0x10
#define RTCWAKE_OPT_SECONDS      0x20
#define RTCWAKE_OPT_TIME         0x40

int rtcwake_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int rtcwake_main(int argc UNUSED_PARAM, char **argv)
{
	unsigned opt;
	const char *rtcname = NULL;
	const char *suspend = "standby";
	const char *opt_seconds;
	const char *opt_time;

	time_t rtc_time;
	time_t sys_time;
	time_t alarm_time = alarm_time;
	unsigned seconds = seconds; /* for compiler */
	int utc = -1;
	int fd;

#if ENABLE_LONG_OPTS
	static const char rtcwake_longopts[] ALIGN1 =
		"auto\0"    No_argument "a"
		"local\0"   No_argument "l"
		"utc\0"     No_argument "u"
		"device\0"  Required_argument "d"
		"mode\0"    Required_argument "m"
		"seconds\0" Required_argument "s"
		"time\0"    Required_argument "t"
		;
#endif
	opt = getopt32long(argv,
			/* Must have -s or -t, exclusive */
			"^alud:m:s:t:" "\0" "s:t:s--t:t--s", rtcwake_longopts,
			&rtcname, &suspend, &opt_seconds, &opt_time);

	/* this is the default
	if (opt & RTCWAKE_OPT_AUTO)
		utc = -1;
	*/
	if (opt & (RTCWAKE_OPT_UTC | RTCWAKE_OPT_LOCAL))
		utc = opt & RTCWAKE_OPT_UTC;
	if (opt & RTCWAKE_OPT_SECONDS) {
		/* alarm time, seconds-to-sleep (relative) */
		seconds = xatou(opt_seconds);
	} else {
		/* RTCWAKE_OPT_TIME */
		/* alarm time, time_t (absolute, seconds since 1/1 1970 UTC) */
		if (sizeof(alarm_time) <= sizeof(long))
			alarm_time = xatol(opt_time);
		else
			alarm_time = xatoll(opt_time);
	}

	if (utc == -1)
		utc = rtc_adjtime_is_utc();

	/* the rtcname is relative to /dev */
	xchdir("/dev");

	/* this RTC must exist and (if we'll sleep) be wakeup-enabled */
	fd = rtc_xopen(&rtcname, O_RDONLY);

	if (strcmp(suspend, "on") != 0)
		if (!may_wakeup(rtcname))
			bb_error_msg_and_die("%s not enabled for wakeup events", rtcname);

	/* relative or absolute alarm time, normalized to time_t */
	sys_time = time(NULL);
	{
		struct tm tm_time;
		rtc_read_tm(&tm_time, fd);
		rtc_time = rtc_tm2time(&tm_time, utc);
	}

	if (opt & RTCWAKE_OPT_TIME) {
		/* Correct for RTC<->system clock difference */
		alarm_time += rtc_time - sys_time;
		if (alarm_time < rtc_time)
			/*
			 * Compat message text.
			 * I'd say "RTC time is already ahead of ..." instead.
			 */
			bb_error_msg_and_die("time doesn't go backward to %s", ctime(&alarm_time));
	} else
		alarm_time = rtc_time + seconds + 1;

	setup_alarm(fd, &alarm_time, rtc_time);
	sync();
#if 0 /*debug*/
	printf("sys_time: %s", ctime(&sys_time));
	printf("rtc_time: %s", ctime(&rtc_time));
#endif
	printf("wakeup from \"%s\" at %s", suspend, ctime(&alarm_time));
	fflush_all();
	usleep(10 * 1000);

	if (strcmp(suspend, "on") != 0)
		xopen_xwrite_close(SYS_POWER_PATH, suspend);
	else {
		/* "fake" suspend ... we'll do the delay ourselves */
		unsigned long data;

		do {
			ssize_t ret = safe_read(fd, &data, sizeof(data));
			if (ret < 0) {
				bb_perror_msg("rtc read");
				break;
			}
		} while (!(data & RTC_AF));
	}

	xioctl(fd, RTC_AIE_OFF, 0);

	if (ENABLE_FEATURE_CLEAN_UP)
		close(fd);

	return EXIT_SUCCESS;
}
