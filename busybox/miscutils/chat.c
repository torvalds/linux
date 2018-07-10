/* vi: set sw=4 ts=4: */
/*
 * bare bones chat utility
 * inspired by ppp's chat
 *
 * Copyright (C) 2008 by Vladimir Dronnikov <dronnikov@gmail.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config CHAT
//config:	bool "chat (6.6 kb)"
//config:	default y
//config:	help
//config:	Simple chat utility.
//config:
//config:config FEATURE_CHAT_NOFAIL
//config:	bool "Enable NOFAIL expect strings"
//config:	depends on CHAT
//config:	default y
//config:	help
//config:	When enabled expect strings which are started with a dash trigger
//config:	no-fail mode. That is when expectation is not met within timeout
//config:	the script is not terminated but sends next SEND string and waits
//config:	for next EXPECT string. This allows to compose far more flexible
//config:	scripts.
//config:
//config:config FEATURE_CHAT_TTY_HIFI
//config:	bool "Force STDIN to be a TTY"
//config:	depends on CHAT
//config:	default n
//config:	help
//config:	Original chat always treats STDIN as a TTY device and sets for it
//config:	so-called raw mode. This option turns on such behaviour.
//config:
//config:config FEATURE_CHAT_IMPLICIT_CR
//config:	bool "Enable implicit Carriage Return"
//config:	depends on CHAT
//config:	default y
//config:	help
//config:	When enabled make chat to terminate all SEND strings with a "\r"
//config:	unless "\c" is met anywhere in the string.
//config:
//config:config FEATURE_CHAT_SWALLOW_OPTS
//config:	bool "Swallow options"
//config:	depends on CHAT
//config:	default y
//config:	help
//config:	Busybox chat require no options. To make it not fail when used
//config:	in place of original chat (which has a bunch of options) turn
//config:	this on.
//config:
//config:config FEATURE_CHAT_SEND_ESCAPES
//config:	bool "Support weird SEND escapes"
//config:	depends on CHAT
//config:	default y
//config:	help
//config:	Original chat uses some escape sequences in SEND arguments which
//config:	are not sent to device but rather performs special actions.
//config:	E.g. "\K" means to send a break sequence to device.
//config:	"\d" delays execution for a second, "\p" -- for a 1/100 of second.
//config:	Before turning this option on think twice: do you really need them?
//config:
//config:config FEATURE_CHAT_VAR_ABORT_LEN
//config:	bool "Support variable-length ABORT conditions"
//config:	depends on CHAT
//config:	default y
//config:	help
//config:	Original chat uses fixed 50-bytes length ABORT conditions. Say N here.
//config:
//config:config FEATURE_CHAT_CLR_ABORT
//config:	bool "Support revoking of ABORT conditions"
//config:	depends on CHAT
//config:	default y
//config:	help
//config:	Support CLR_ABORT directive.

//applet:IF_CHAT(APPLET(chat, BB_DIR_USR_SBIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_CHAT) += chat.o

//usage:#define chat_trivial_usage
//usage:       "EXPECT [SEND [EXPECT [SEND...]]]"
//usage:#define chat_full_usage "\n\n"
//usage:       "Useful for interacting with a modem connected to stdin/stdout.\n"
//usage:       "A script consists of \"expect-send\" argument pairs.\n"
//usage:       "Example:\n"
//usage:       "chat '' ATZ OK ATD123456 CONNECT '' ogin: pppuser word: ppppass '~'"

#include "libbb.h"
#include "common_bufsiz.h"

// default timeout: 45 sec
#define DEFAULT_CHAT_TIMEOUT 45*1000
// max length of "abort string",
// i.e. device reply which causes termination
#define MAX_ABORT_LEN 50

// possible exit codes
enum {
	ERR_OK = 0,     // all's well
	ERR_MEM,        // read too much while expecting
	ERR_IO,         // signalled or I/O error
	ERR_TIMEOUT,    // timed out while expecting
	ERR_ABORT,      // first abort condition was met
//	ERR_ABORT2,     // second abort condition was met
//	...
};

// exit code
#define exitcode bb_got_signal

// trap for critical signals
static void signal_handler(UNUSED_PARAM int signo)
{
	// report I/O error condition
	exitcode = ERR_IO;
}

#if !ENABLE_FEATURE_CHAT_IMPLICIT_CR
#define unescape(s, nocr) unescape(s)
#endif
static size_t unescape(char *s, int *nocr)
{
	char *start = s;
	char *p = s;

	while (*s) {
		char c = *s;
		// do we need special processing?
		// standard escapes + \s for space and \N for \0
		// \c inhibits terminating \r for commands and is noop for expects
		if ('\\' == c) {
			c = *++s;
			if (c) {
#if ENABLE_FEATURE_CHAT_IMPLICIT_CR
				if ('c' == c) {
					*nocr = 1;
					goto next;
				}
#endif
				if ('N' == c) {
					c = '\0';
				} else if ('s' == c) {
					c = ' ';
#if ENABLE_FEATURE_CHAT_NOFAIL
				// unescape leading dash only
				// TODO: and only for expect, not command string
				} else if ('-' == c && (start + 1 == s)) {
					//c = '-';
#endif
				} else {
					c = bb_process_escape_sequence((const char **)&s);
					s--;
				}
			}
		// ^A becomes \001, ^B -- \002 and so on...
		} else if ('^' == c) {
			c = *++s-'@';
		}
		// put unescaped char
		*p++ = c;
#if ENABLE_FEATURE_CHAT_IMPLICIT_CR
 next:
#endif
		// next char
		s++;
	}
	*p = '\0';

	return p - start;
}

int chat_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int chat_main(int argc UNUSED_PARAM, char **argv)
{
	int record_fd = -1;
	bool echo = 0;
	// collection of device replies which cause unconditional termination
	llist_t *aborts = NULL;
	// inactivity period
	int timeout = DEFAULT_CHAT_TIMEOUT;
	// maximum length of abort string
#if ENABLE_FEATURE_CHAT_VAR_ABORT_LEN
	size_t max_abort_len = 0;
#else
#define max_abort_len MAX_ABORT_LEN
#endif
#if ENABLE_FEATURE_CHAT_TTY_HIFI
	struct termios tio0, tio;
#endif
	// directive names
	enum {
		DIR_HANGUP = 0,
		DIR_ABORT,
#if ENABLE_FEATURE_CHAT_CLR_ABORT
		DIR_CLR_ABORT,
#endif
		DIR_TIMEOUT,
		DIR_ECHO,
		DIR_SAY,
		DIR_RECORD,
	};

	// make x* functions fail with correct exitcode
	xfunc_error_retval = ERR_IO;

	// trap vanilla signals to prevent process from being killed suddenly
	bb_signals(0
		+ (1 << SIGHUP)
		+ (1 << SIGINT)
		+ (1 << SIGTERM)
		+ (1 << SIGPIPE)
		, signal_handler);

#if ENABLE_FEATURE_CHAT_TTY_HIFI
//TODO: use set_termios_to_raw()
	tcgetattr(STDIN_FILENO, &tio);
	tio0 = tio;
	cfmakeraw(&tio);
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &tio);
#endif

#if ENABLE_FEATURE_CHAT_SWALLOW_OPTS
	getopt32(argv, "vVsSE");
	argv += optind;
#else
	argv++; // goto first arg
#endif
	// handle chat expect-send pairs
	while (*argv) {
		// directive given? process it
		int key = index_in_strings(
			"HANGUP\0" "ABORT\0"
#if ENABLE_FEATURE_CHAT_CLR_ABORT
			"CLR_ABORT\0"
#endif
			"TIMEOUT\0" "ECHO\0" "SAY\0" "RECORD\0"
			, *argv
		);
		if (key >= 0) {
			bool onoff;
			// cache directive value
			char *arg = *++argv;

			if (!arg) {
#if ENABLE_FEATURE_CHAT_TTY_HIFI
				tcsetattr(STDIN_FILENO, TCSAFLUSH, &tio0);
#endif
				bb_show_usage();
			}
			// OFF -> 0, anything else -> 1
			onoff = (0 != strcmp("OFF", arg));
			// process directive
			if (DIR_HANGUP == key) {
				// turn SIGHUP on/off
				signal(SIGHUP, onoff ? signal_handler : SIG_IGN);
			} else if (DIR_ABORT == key) {
				// append the string to abort conditions
#if ENABLE_FEATURE_CHAT_VAR_ABORT_LEN
				size_t len = strlen(arg);
				if (len > max_abort_len)
					max_abort_len = len;
#endif
				llist_add_to_end(&aborts, arg);
#if ENABLE_FEATURE_CHAT_CLR_ABORT
			} else if (DIR_CLR_ABORT == key) {
				llist_t *l;
				// remove the string from abort conditions
				// N.B. gotta refresh maximum length too...
# if ENABLE_FEATURE_CHAT_VAR_ABORT_LEN
				max_abort_len = 0;
# endif
				for (l = aborts; l; l = l->link) {
# if ENABLE_FEATURE_CHAT_VAR_ABORT_LEN
					size_t len = strlen(l->data);
# endif
					if (strcmp(arg, l->data) == 0) {
						llist_unlink(&aborts, l);
						continue;
					}
# if ENABLE_FEATURE_CHAT_VAR_ABORT_LEN
					if (len > max_abort_len)
						max_abort_len = len;
# endif
				}
#endif
			} else if (DIR_TIMEOUT == key) {
				// set new timeout
				// -1 means OFF
				timeout = atoi(arg) * 1000;
				// 0 means default
				// >0 means value in msecs
				if (!timeout)
					timeout = DEFAULT_CHAT_TIMEOUT;
			} else if (DIR_ECHO == key) {
				// turn echo on/off
				// N.B. echo means dumping device input/output to stderr
				echo = onoff;
			} else if (DIR_RECORD == key) {
				// turn record on/off
				// N.B. record means dumping device input to a file
					// close previous record_fd
				if (record_fd > 0)
					close(record_fd);
				// N.B. do we have to die here on open error?
				record_fd = (onoff) ? xopen(arg, O_WRONLY|O_CREAT|O_TRUNC) : -1;
			} else if (DIR_SAY == key) {
				// just print argument verbatim
				// TODO: should we use full_write() to avoid unistd/stdio conflict?
				bb_error_msg("%s", arg);
			}
			// next, please!
			argv++;
		// ordinary expect-send pair!
		} else {
			//-----------------------
			// do expect
			//-----------------------
			int expect_len;
			size_t buf_len = 0;
			size_t max_len = max_abort_len;

			struct pollfd pfd;
#if ENABLE_FEATURE_CHAT_NOFAIL
			int nofail = 0;
#endif
			char *expect = *argv++;

			// sanity check: shall we really expect something?
			if (!expect)
				goto expect_done;

#if ENABLE_FEATURE_CHAT_NOFAIL
			// if expect starts with -
			if ('-' == *expect) {
				// swallow -
				expect++;
				// and enter nofail mode
				nofail++;
			}
#endif

#ifdef ___TEST___BUF___ // test behaviour with a small buffer
#	undef COMMON_BUFSIZE
#	define COMMON_BUFSIZE 6
#endif
			// expand escape sequences in expect
			expect_len = unescape(expect, &expect_len /*dummy*/);
			if (expect_len > max_len)
				max_len = expect_len;
			// sanity check:
			// we should expect more than nothing but not more than input buffer
			// TODO: later we'll get rid of fixed-size buffer
			if (!expect_len)
				goto expect_done;
			if (max_len >= COMMON_BUFSIZE) {
				exitcode = ERR_MEM;
				goto expect_done;
			}

			// get reply
			pfd.fd = STDIN_FILENO;
			pfd.events = POLLIN;
			while (!exitcode
			    && poll(&pfd, 1, timeout) > 0
			    && (pfd.revents & POLLIN)
			) {
				llist_t *l;
				ssize_t delta;
#define buf bb_common_bufsiz1
				setup_common_bufsiz();

				// read next char from device
				if (safe_read(STDIN_FILENO, buf+buf_len, 1) > 0) {
					// dump device input if RECORD fname
					if (record_fd > 0) {
						full_write(record_fd, buf+buf_len, 1);
					}
					// dump device input if ECHO ON
					if (echo) {
//						if (buf[buf_len] < ' ') {
//							full_write(STDERR_FILENO, "^", 1);
//							buf[buf_len] += '@';
//						}
						full_write(STDERR_FILENO, buf+buf_len, 1);
					}
					buf_len++;
					// move input frame if we've reached higher bound
					if (buf_len > COMMON_BUFSIZE) {
						memmove(buf, buf+buf_len-max_len, max_len);
						buf_len = max_len;
					}
				}
				// N.B. rule of thumb: values being looked for can
				// be found only at the end of input buffer
				// this allows to get rid of strstr() and memmem()

				// TODO: make expect and abort strings processed uniformly
				// abort condition is met? -> bail out
				for (l = aborts, exitcode = ERR_ABORT; l; l = l->link, ++exitcode) {
					size_t len = strlen(l->data);
					delta = buf_len-len;
					if (delta >= 0 && !memcmp(buf+delta, l->data, len))
						goto expect_done;
				}
				exitcode = ERR_OK;

				// expected reply received? -> goto next command
				delta = buf_len - expect_len;
				if (delta >= 0 && !memcmp(buf+delta, expect, expect_len))
					goto expect_done;
#undef buf
			} /* while (have data) */

			// device timed out or unexpected reply received
			exitcode = ERR_TIMEOUT;
 expect_done:
#if ENABLE_FEATURE_CHAT_NOFAIL
			// on success and when in nofail mode
			// we should skip following subsend-subexpect pairs
			if (nofail) {
				if (!exitcode) {
					// find last send before non-dashed expect
					while (*argv && argv[1] && '-' == argv[1][0])
						argv += 2;
					// skip the pair
					// N.B. do we really need this?!
					if (!*argv++ || !*argv++)
						break;
				}
				// nofail mode also clears all but IO errors (or signals)
				if (ERR_IO != exitcode)
					exitcode = ERR_OK;
			}
#endif
			// bail out unless we expected successfully
			if (exitcode)
				break;

			//-----------------------
			// do send
			//-----------------------
			if (*argv) {
#if ENABLE_FEATURE_CHAT_IMPLICIT_CR
				int nocr = 0; // inhibit terminating command with \r
#endif
				char *loaded = NULL; // loaded command
				size_t len;
				char *buf = *argv++;

				// if command starts with @
				// load "real" command from file named after @
				if ('@' == *buf) {
					// skip the @ and any following white-space
					trim(++buf);
					buf = loaded = xmalloc_xopen_read_close(buf, NULL);
				}
				// expand escape sequences in command
				len = unescape(buf, &nocr);

				// send command
				alarm(timeout);
				pfd.fd = STDOUT_FILENO;
				pfd.events = POLLOUT;
				while (len && !exitcode
				    && poll(&pfd, 1, -1) > 0
				    && (pfd.revents & POLLOUT)
				) {
#if ENABLE_FEATURE_CHAT_SEND_ESCAPES
					// "\\d" means 1 sec delay, "\\p" means 0.01 sec delay
					// "\\K" means send BREAK
					char c = *buf;
					if ('\\' == c) {
						c = *++buf;
						if ('d' == c) {
							sleep(1);
							len--;
							continue;
						}
						if ('p' == c) {
							usleep(10000);
							len--;
							continue;
						}
						if ('K' == c) {
							tcsendbreak(STDOUT_FILENO, 0);
							len--;
							continue;
						}
						buf--;
					}
					if (safe_write(STDOUT_FILENO, buf, 1) != 1)
						break;
					len--;
					buf++;
#else
					len -= full_write(STDOUT_FILENO, buf, len);
#endif
				} /* while (can write) */
				alarm(0);

				// report I/O error if there still exists at least one non-sent char
				if (len)
					exitcode = ERR_IO;

				// free loaded command (if any)
				if (loaded)
					free(loaded);
#if ENABLE_FEATURE_CHAT_IMPLICIT_CR
				// or terminate command with \r (if not inhibited)
				else if (!nocr)
					xwrite(STDOUT_FILENO, "\r", 1);
#endif
				// bail out unless we sent command successfully
				if (exitcode)
					break;
			} /* if (*argv) */
		}
	} /* while (*argv) */

#if ENABLE_FEATURE_CHAT_TTY_HIFI
	tcsetattr(STDIN_FILENO, TCSAFLUSH, &tio0);
#endif

	return exitcode;
}
