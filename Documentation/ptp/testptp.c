/*
 * PTP 1588 clock support - User space test program
 *
 * Copyright (C) 2010 OMICRON electronics GmbH
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#define _GNU_SOURCE
#define __SANE_USERSPACE_TYPES__        /* For PPC64, to get LL64 types */
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/timex.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <linux/ptp_clock.h>

#define DEVICE "/dev/ptp0"

#ifndef ADJ_SETOFFSET
#define ADJ_SETOFFSET 0x0100
#endif

#ifndef CLOCK_INVALID
#define CLOCK_INVALID -1
#endif

/* clock_adjtime is not available in GLIBC < 2.14 */
#if !__GLIBC_PREREQ(2, 14)
#include <sys/syscall.h>
static int clock_adjtime(clockid_t id, struct timex *tx)
{
	return syscall(__NR_clock_adjtime, id, tx);
}
#endif

static clockid_t get_clockid(int fd)
{
#define CLOCKFD 3
#define FD_TO_CLOCKID(fd)	((~(clockid_t) (fd) << 3) | CLOCKFD)

	return FD_TO_CLOCKID(fd);
}

static void handle_alarm(int s)
{
	printf("received signal %d\n", s);
}

static int install_handler(int signum, void (*handler)(int))
{
	struct sigaction action;
	sigset_t mask;

	/* Unblock the signal. */
	sigemptyset(&mask);
	sigaddset(&mask, signum);
	sigprocmask(SIG_UNBLOCK, &mask, NULL);

	/* Install the signal handler. */
	action.sa_handler = handler;
	action.sa_flags = 0;
	sigemptyset(&action.sa_mask);
	sigaction(signum, &action, NULL);

	return 0;
}

static long ppb_to_scaled_ppm(int ppb)
{
	/*
	 * The 'freq' field in the 'struct timex' is in parts per
	 * million, but with a 16 bit binary fractional field.
	 * Instead of calculating either one of
	 *
	 *    scaled_ppm = (ppb / 1000) << 16  [1]
	 *    scaled_ppm = (ppb << 16) / 1000  [2]
	 *
	 * we simply use double precision math, in order to avoid the
	 * truncation in [1] and the possible overflow in [2].
	 */
	return (long) (ppb * 65.536);
}

static int64_t pctns(struct ptp_clock_time *t)
{
	return t->sec * 1000000000LL + t->nsec;
}

static void usage(char *progname)
{
	fprintf(stderr,
		"usage: %s [options]\n"
		" -a val     request a one-shot alarm after 'val' seconds\n"
		" -A val     request a periodic alarm every 'val' seconds\n"
		" -c         query the ptp clock's capabilities\n"
		" -d name    device to open\n"
		" -e val     read 'val' external time stamp events\n"
		" -f val     adjust the ptp clock frequency by 'val' ppb\n"
		" -g         get the ptp clock time\n"
		" -h         prints this message\n"
		" -i val     index for event/trigger\n"
		" -k val     measure the time offset between system and phc clock\n"
		"            for 'val' times (Maximum 25)\n"
		" -l         list the current pin configuration\n"
		" -L pin,val configure pin index 'pin' with function 'val'\n"
		"            the channel index is taken from the '-i' option\n"
		"            'val' specifies the auxiliary function:\n"
		"            0 - none\n"
		"            1 - external time stamp\n"
		"            2 - periodic output\n"
		" -p val     enable output with a period of 'val' nanoseconds\n"
		" -P val     enable or disable (val=1|0) the system clock PPS\n"
		" -s         set the ptp clock time from the system time\n"
		" -S         set the system time from the ptp clock time\n"
		" -t val     shift the ptp clock time by 'val' seconds\n"
		" -T val     set the ptp clock time to 'val' seconds\n",
		progname);
}

int main(int argc, char *argv[])
{
	struct ptp_clock_caps caps;
	struct ptp_extts_event event;
	struct ptp_extts_request extts_request;
	struct ptp_perout_request perout_request;
	struct ptp_pin_desc desc;
	struct timespec ts;
	struct timex tx;

	static timer_t timerid;
	struct itimerspec timeout;
	struct sigevent sigevent;

	struct ptp_clock_time *pct;
	struct ptp_sys_offset *sysoff;


	char *progname;
	int i, c, cnt, fd;

	char *device = DEVICE;
	clockid_t clkid;
	int adjfreq = 0x7fffffff;
	int adjtime = 0;
	int capabilities = 0;
	int extts = 0;
	int gettime = 0;
	int index = 0;
	int list_pins = 0;
	int oneshot = 0;
	int pct_offset = 0;
	int n_samples = 0;
	int periodic = 0;
	int perout = -1;
	int pin_index = -1, pin_func;
	int pps = -1;
	int seconds = 0;
	int settime = 0;

	int64_t t1, t2, tp;
	int64_t interval, offset;

	progname = strrchr(argv[0], '/');
	progname = progname ? 1+progname : argv[0];
	while (EOF != (c = getopt(argc, argv, "a:A:cd:e:f:ghi:k:lL:p:P:sSt:T:v"))) {
		switch (c) {
		case 'a':
			oneshot = atoi(optarg);
			break;
		case 'A':
			periodic = atoi(optarg);
			break;
		case 'c':
			capabilities = 1;
			break;
		case 'd':
			device = optarg;
			break;
		case 'e':
			extts = atoi(optarg);
			break;
		case 'f':
			adjfreq = atoi(optarg);
			break;
		case 'g':
			gettime = 1;
			break;
		case 'i':
			index = atoi(optarg);
			break;
		case 'k':
			pct_offset = 1;
			n_samples = atoi(optarg);
			break;
		case 'l':
			list_pins = 1;
			break;
		case 'L':
			cnt = sscanf(optarg, "%d,%d", &pin_index, &pin_func);
			if (cnt != 2) {
				usage(progname);
				return -1;
			}
			break;
		case 'p':
			perout = atoi(optarg);
			break;
		case 'P':
			pps = atoi(optarg);
			break;
		case 's':
			settime = 1;
			break;
		case 'S':
			settime = 2;
			break;
		case 't':
			adjtime = atoi(optarg);
			break;
		case 'T':
			settime = 3;
			seconds = atoi(optarg);
			break;
		case 'h':
			usage(progname);
			return 0;
		case '?':
		default:
			usage(progname);
			return -1;
		}
	}

	fd = open(device, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "opening %s: %s\n", device, strerror(errno));
		return -1;
	}

	clkid = get_clockid(fd);
	if (CLOCK_INVALID == clkid) {
		fprintf(stderr, "failed to read clock id\n");
		return -1;
	}

	if (capabilities) {
		if (ioctl(fd, PTP_CLOCK_GETCAPS, &caps)) {
			perror("PTP_CLOCK_GETCAPS");
		} else {
			printf("capabilities:\n"
			       "  %d maximum frequency adjustment (ppb)\n"
			       "  %d programmable alarms\n"
			       "  %d external time stamp channels\n"
			       "  %d programmable periodic signals\n"
			       "  %d pulse per second\n"
			       "  %d programmable pins\n",
			       caps.max_adj,
			       caps.n_alarm,
			       caps.n_ext_ts,
			       caps.n_per_out,
			       caps.pps,
			       caps.n_pins);
		}
	}

	if (0x7fffffff != adjfreq) {
		memset(&tx, 0, sizeof(tx));
		tx.modes = ADJ_FREQUENCY;
		tx.freq = ppb_to_scaled_ppm(adjfreq);
		if (clock_adjtime(clkid, &tx)) {
			perror("clock_adjtime");
		} else {
			puts("frequency adjustment okay");
		}
	}

	if (adjtime) {
		memset(&tx, 0, sizeof(tx));
		tx.modes = ADJ_SETOFFSET;
		tx.time.tv_sec = adjtime;
		tx.time.tv_usec = 0;
		if (clock_adjtime(clkid, &tx) < 0) {
			perror("clock_adjtime");
		} else {
			puts("time shift okay");
		}
	}

	if (gettime) {
		if (clock_gettime(clkid, &ts)) {
			perror("clock_gettime");
		} else {
			printf("clock time: %ld.%09ld or %s",
			       ts.tv_sec, ts.tv_nsec, ctime(&ts.tv_sec));
		}
	}

	if (settime == 1) {
		clock_gettime(CLOCK_REALTIME, &ts);
		if (clock_settime(clkid, &ts)) {
			perror("clock_settime");
		} else {
			puts("set time okay");
		}
	}

	if (settime == 2) {
		clock_gettime(clkid, &ts);
		if (clock_settime(CLOCK_REALTIME, &ts)) {
			perror("clock_settime");
		} else {
			puts("set time okay");
		}
	}

	if (settime == 3) {
		ts.tv_sec = seconds;
		ts.tv_nsec = 0;
		if (clock_settime(clkid, &ts)) {
			perror("clock_settime");
		} else {
			puts("set time okay");
		}
	}

	if (extts) {
		memset(&extts_request, 0, sizeof(extts_request));
		extts_request.index = index;
		extts_request.flags = PTP_ENABLE_FEATURE;
		if (ioctl(fd, PTP_EXTTS_REQUEST, &extts_request)) {
			perror("PTP_EXTTS_REQUEST");
			extts = 0;
		} else {
			puts("external time stamp request okay");
		}
		for (; extts; extts--) {
			cnt = read(fd, &event, sizeof(event));
			if (cnt != sizeof(event)) {
				perror("read");
				break;
			}
			printf("event index %u at %lld.%09u\n", event.index,
			       event.t.sec, event.t.nsec);
			fflush(stdout);
		}
		/* Disable the feature again. */
		extts_request.flags = 0;
		if (ioctl(fd, PTP_EXTTS_REQUEST, &extts_request)) {
			perror("PTP_EXTTS_REQUEST");
		}
	}

	if (list_pins) {
		int n_pins = 0;
		if (ioctl(fd, PTP_CLOCK_GETCAPS, &caps)) {
			perror("PTP_CLOCK_GETCAPS");
		} else {
			n_pins = caps.n_pins;
		}
		for (i = 0; i < n_pins; i++) {
			desc.index = i;
			if (ioctl(fd, PTP_PIN_GETFUNC, &desc)) {
				perror("PTP_PIN_GETFUNC");
				break;
			}
			printf("name %s index %u func %u chan %u\n",
			       desc.name, desc.index, desc.func, desc.chan);
		}
	}

	if (oneshot) {
		install_handler(SIGALRM, handle_alarm);
		/* Create a timer. */
		sigevent.sigev_notify = SIGEV_SIGNAL;
		sigevent.sigev_signo = SIGALRM;
		if (timer_create(clkid, &sigevent, &timerid)) {
			perror("timer_create");
			return -1;
		}
		/* Start the timer. */
		memset(&timeout, 0, sizeof(timeout));
		timeout.it_value.tv_sec = oneshot;
		if (timer_settime(timerid, 0, &timeout, NULL)) {
			perror("timer_settime");
			return -1;
		}
		pause();
		timer_delete(timerid);
	}

	if (periodic) {
		install_handler(SIGALRM, handle_alarm);
		/* Create a timer. */
		sigevent.sigev_notify = SIGEV_SIGNAL;
		sigevent.sigev_signo = SIGALRM;
		if (timer_create(clkid, &sigevent, &timerid)) {
			perror("timer_create");
			return -1;
		}
		/* Start the timer. */
		memset(&timeout, 0, sizeof(timeout));
		timeout.it_interval.tv_sec = periodic;
		timeout.it_value.tv_sec = periodic;
		if (timer_settime(timerid, 0, &timeout, NULL)) {
			perror("timer_settime");
			return -1;
		}
		while (1) {
			pause();
		}
		timer_delete(timerid);
	}

	if (perout >= 0) {
		if (clock_gettime(clkid, &ts)) {
			perror("clock_gettime");
			return -1;
		}
		memset(&perout_request, 0, sizeof(perout_request));
		perout_request.index = index;
		perout_request.start.sec = ts.tv_sec + 2;
		perout_request.start.nsec = 0;
		perout_request.period.sec = 0;
		perout_request.period.nsec = perout;
		if (ioctl(fd, PTP_PEROUT_REQUEST, &perout_request)) {
			perror("PTP_PEROUT_REQUEST");
		} else {
			puts("periodic output request okay");
		}
	}

	if (pin_index >= 0) {
		memset(&desc, 0, sizeof(desc));
		desc.index = pin_index;
		desc.func = pin_func;
		desc.chan = index;
		if (ioctl(fd, PTP_PIN_SETFUNC, &desc)) {
			perror("PTP_PIN_SETFUNC");
		} else {
			puts("set pin function okay");
		}
	}

	if (pps != -1) {
		int enable = pps ? 1 : 0;
		if (ioctl(fd, PTP_ENABLE_PPS, enable)) {
			perror("PTP_ENABLE_PPS");
		} else {
			puts("pps for system time request okay");
		}
	}

	if (pct_offset) {
		if (n_samples <= 0 || n_samples > 25) {
			puts("n_samples should be between 1 and 25");
			usage(progname);
			return -1;
		}

		sysoff = calloc(1, sizeof(*sysoff));
		if (!sysoff) {
			perror("calloc");
			return -1;
		}
		sysoff->n_samples = n_samples;

		if (ioctl(fd, PTP_SYS_OFFSET, sysoff))
			perror("PTP_SYS_OFFSET");
		else
			puts("system and phc clock time offset request okay");

		pct = &sysoff->ts[0];
		for (i = 0; i < sysoff->n_samples; i++) {
			t1 = pctns(pct+2*i);
			tp = pctns(pct+2*i+1);
			t2 = pctns(pct+2*i+2);
			interval = t2 - t1;
			offset = (t2 + t1) / 2 - tp;

			printf("system time: %lld.%u\n",
				(pct+2*i)->sec, (pct+2*i)->nsec);
			printf("phc    time: %lld.%u\n",
				(pct+2*i+1)->sec, (pct+2*i+1)->nsec);
			printf("system time: %lld.%u\n",
				(pct+2*i+2)->sec, (pct+2*i+2)->nsec);
			printf("system/phc clock time offset is %" PRId64 " ns\n"
			       "system     clock time delay  is %" PRId64 " ns\n",
				offset, interval);
		}

		free(sysoff);
	}

	close(fd);
	return 0;
}
