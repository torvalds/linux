/* vi: set sw=4 ts=4: */
/*
 * The Rdate command will ask a time server for the RFC 868 time
 * and optionally set the system time.
 *
 * by Sterling Huxley <sterling@europa.com>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config RDATE
//config:	bool "rdate (6 kb)"
//config:	default y
//config:	help
//config:	The rdate utility allows you to synchronize the date and time of your
//config:	system clock with the date and time of a remote networked system using
//config:	the RFC868 protocol, which is built into the inetd daemon on most
//config:	systems.

//applet:IF_RDATE(APPLET(rdate, BB_DIR_USR_SBIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_RDATE) += rdate.o

//usage:#define rdate_trivial_usage
//usage:       "[-s/-p] HOST"
//usage:#define rdate_full_usage "\n\n"
//usage:       "Set and print time from HOST using RFC 868\n"
//usage:     "\n	-s	Only set system time"
//usage:     "\n	-p	Only print time"

#include "libbb.h"

enum { RFC_868_BIAS = 2208988800UL };

static void socket_timeout(int sig UNUSED_PARAM)
{
	bb_error_msg_and_die("timeout connecting to time server");
}

static time_t askremotedate(const char *host)
{
	uint32_t nett;
	int fd;

	/* Timeout for dead or inaccessible servers */
	alarm(10);
	signal(SIGALRM, socket_timeout);

	fd = create_and_connect_stream_or_die(host, bb_lookup_std_port("time", "tcp", 37));

	if (safe_read(fd, &nett, 4) != 4)    /* read time from server */
		bb_error_msg_and_die("%s: %s", host, "short read");
	if (ENABLE_FEATURE_CLEAN_UP)
		close(fd);

	/* Convert from network byte order to local byte order.
	 * RFC 868 time is seconds since 1900-01-01 00:00 GMT.
	 * RFC 868 time 2,208,988,800 corresponds to 1970-01-01 00:00 GMT.
	 * Subtract the RFC 868 time to get Linux epoch.
	 */
	nett = ntohl(nett) - RFC_868_BIAS;

	if (sizeof(time_t) > 4) {
		/* Now we have 32-bit lsb of a wider time_t
		 * Imagine that  nett =   0x00000001,
		 * current time  cur = 0x123ffffffff.
		 * Assuming our time is not some 40 years off,
		 * remote time must be 0x12400000001.
		 * Need to adjust our time by (int32_t)(nett - cur).
		 */
		time_t cur = time(NULL);
		int32_t adjust = (int32_t)(nett - (uint32_t)cur);
		return cur + adjust;
	}
	/* This is not going to work, but what can we do */
	return (time_t)nett;
}

int rdate_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int rdate_main(int argc UNUSED_PARAM, char **argv)
{
	time_t remote_time;
	unsigned flags;

	flags = getopt32(argv, "^" "sp" "\0" "-1");

	remote_time = askremotedate(argv[optind]);

	/* Manpages of various Unixes are confusing. What happens is:
	 * (no opts) set and print time
	 * -s: set time ("do not print the time")
	 * -p: print time ("do not set, just print the remote time")
	 * -sp: print time (that's what we do, not sure this is right)
	 */

	if (!(flags & 2)) { /* no -p (-s may be present) */
		if (time(NULL) == remote_time)
			bb_error_msg("current time matches remote time");
		else
			if (stime(&remote_time) < 0)
				bb_perror_msg_and_die("can't set time of day");
	}

	if (flags != 1) /* not lone -s */
		printf("%s", ctime(&remote_time));

	return EXIT_SUCCESS;
}
