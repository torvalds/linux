/* vi: set sw=4 ts=4: */
/*
 * bare bones 'talk to modem' program - similar to 'cu -l $device'
 * inspired by mgetty's microcom
 *
 * Copyright (C) 2008 by Vladimir Dronnikov <dronnikov@gmail.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config MICROCOM
//config:	bool "microcom (5.6 kb)"
//config:	default y
//config:	help
//config:	The poor man's minicom utility for chatting with serial port devices.

//applet:IF_MICROCOM(APPLET(microcom, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_MICROCOM) += microcom.o

//usage:#define microcom_trivial_usage
//usage:       "[-d DELAY] [-t TIMEOUT] [-s SPEED] [-X] TTY"
//usage:#define microcom_full_usage "\n\n"
//usage:       "Copy bytes for stdin to TTY and from TTY to stdout\n"
//usage:     "\n	-d	Wait up to DELAY ms for TTY output before sending every"
//usage:     "\n		next byte to it"
//usage:     "\n	-t	Exit if both stdin and TTY are silent for TIMEOUT ms"
//usage:     "\n	-s	Set serial line to SPEED"
//usage:     "\n	-X	Disable special meaning of NUL and Ctrl-X from stdin"

#include "libbb.h"
#include "common_bufsiz.h"

// set raw tty mode
static void xget1(int fd, struct termios *t, struct termios *oldt)
{
	get_termios_and_make_raw(fd, t, oldt, 0
		| TERMIOS_CLEAR_ISIG /* ^C is ASCII char 3, not "interrupt me!" */
		| TERMIOS_RAW_INPUT /* pass all chars verbatim, no special handling or translating CR->NL */
		| TERMIOS_RAW_CRNL  /* dont convert NL<->CR on output too */
	);
}

static int xset1(int fd, struct termios *tio, const char *device)
{
	int ret = tcsetattr(fd, TCSAFLUSH, tio);

	if (ret) {
		bb_perror_msg("can't tcsetattr for %s", device);
	}
	return ret;
}

int microcom_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int microcom_main(int argc UNUSED_PARAM, char **argv)
{
	int sfd;
	int nfd;
	struct pollfd pfd[2];
	struct termios tio0, tiosfd, tio;
	char *device_lock_file;
	enum {
		OPT_X = 1 << 0, // do not respect Ctrl-X, Ctrl-@
		OPT_s = 1 << 1, // baudrate
		OPT_d = 1 << 2, // wait for device response, ms
		OPT_t = 1 << 3, // timeout, ms
	};
	speed_t speed = 9600;
	int delay = -1;
	int timeout = -1;
	unsigned opts;

	// fetch options
	opts = getopt32(argv, "^" "Xs:+d:+t:+" "\0" "=1",
				&speed, &delay, &timeout
	);
//	argc -= optind;
	argv += optind;

	// try to create lock file in /var/lock
	device_lock_file = (char *)bb_basename(argv[0]);
	device_lock_file = xasprintf("/var/lock/LCK..%s", device_lock_file);
	sfd = open(device_lock_file, O_CREAT | O_WRONLY | O_TRUNC | O_EXCL, 0644);
	if (sfd < 0) {
		// device already locked -> bail out
		if (errno == EEXIST)
			bb_perror_msg_and_die("can't create '%s'", device_lock_file);
		// can't create lock -> don't care
		if (ENABLE_FEATURE_CLEAN_UP)
			free(device_lock_file);
		device_lock_file = NULL;
	} else {
		// %4d to make concurrent mgetty (if any) happy.
		// Mgetty treats 4-bytes lock files as binary,
		// not text, PID. Making 5+ char file. Brrr...
		fdprintf(sfd, "%4d\n", getpid());
		close(sfd);
	}

	// setup signals
	bb_signals(0
		+ (1 << SIGHUP)
		+ (1 << SIGINT)
		+ (1 << SIGTERM)
		+ (1 << SIGPIPE)
		, record_signo);

	// error exit code if we fail to open the device
	bb_got_signal = 1;

	// open device
	sfd = open_or_warn(argv[0], O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (sfd < 0)
		goto done;
	fcntl(sfd, F_SETFL, O_RDWR);

	// put device to "raw mode"
	xget1(sfd, &tio, &tiosfd);
	// set device speed
	cfsetspeed(&tio, tty_value_to_baud(speed));
	if (xset1(sfd, &tio, argv[0]))
		goto done;

	// put stdin to "raw mode" (if stdin is a TTY),
	// handle one character at a time
	if (isatty(STDIN_FILENO)) {
		xget1(STDIN_FILENO, &tio, &tio0);
		if (xset1(STDIN_FILENO, &tio, "stdin"))
			goto done;
	}

	// main loop: check with poll(), then read/write bytes across
	pfd[0].fd = sfd;
	pfd[0].events = POLLIN;
	pfd[1].fd = STDIN_FILENO;
	pfd[1].events = POLLIN;

	bb_got_signal = 0;
	nfd = 2;
	// Not safe_poll: we want to exit on signal
	while (!bb_got_signal && poll(pfd, nfd, timeout) > 0) {
		if (nfd > 1 && pfd[1].revents) {
			char c;
			// read from stdin -> write to device
			if (safe_read(STDIN_FILENO, &c, 1) < 1) {
				// don't poll stdin anymore if we got EOF/error
				nfd--;
				goto skip_write;
			}
			// do we need special processing?
			if (!(opts & OPT_X)) {
				// ^@ sends Break
				if (VINTR == c) {
					tcsendbreak(sfd, 0);
					goto skip_write;
				}
				// ^X exits
				if (24 == c)
					break;
			}
			write(sfd, &c, 1);
			if (delay >= 0)
				safe_poll(pfd, 1, delay);
skip_write: ;
		}
		if (pfd[0].revents) {
			ssize_t len;
#define iobuf bb_common_bufsiz1
			setup_common_bufsiz();
			// read from device -> write to stdout
			len = safe_read(sfd, iobuf, COMMON_BUFSIZE);
			if (len > 0)
				full_write(STDOUT_FILENO, iobuf, len);
			else {
				// EOF/error -> bail out
				bb_got_signal = SIGHUP;
				break;
			}
		}
	}

	// restore device mode
	tcsetattr(sfd, TCSAFLUSH, &tiosfd);

	if (isatty(STDIN_FILENO))
		tcsetattr(STDIN_FILENO, TCSAFLUSH, &tio0);

done:
	if (device_lock_file)
		unlink(device_lock_file);

	return bb_got_signal;
}
