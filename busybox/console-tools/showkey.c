/* vi: set sw=4 ts=4: */
/*
 * shows keys pressed. inspired by kbd package
 *
 * Copyright (C) 2008 by Vladimir Dronnikov <dronnikov@gmail.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config SHOWKEY
//config:	bool "showkey (4.7 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	Shows keys pressed.

//applet:IF_SHOWKEY(APPLET(showkey, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_SHOWKEY) += showkey.o

//usage:#define showkey_trivial_usage
//usage:       "[-a | -k | -s]"
//usage:#define showkey_full_usage "\n\n"
//usage:       "Show keys pressed\n"
//usage:     "\n	-a	Display decimal/octal/hex values of the keys"
//usage:     "\n	-k	Display interpreted keycodes (default)"
//usage:     "\n	-s	Display raw scan-codes"

#include "libbb.h"
#include <linux/kd.h>


struct globals {
	int kbmode;
	struct termios tio, tio0;
};
#define G (*ptr_to_globals)
#define kbmode (G.kbmode)
#define tio    (G.tio)
#define tio0   (G.tio0)
#define INIT_G() do { \
	SET_PTR_TO_GLOBALS(xzalloc(sizeof(G))); \
} while (0)


// set raw tty mode
// also used by microcom
// libbb candidates?
static void xget1(struct termios *t, struct termios *oldt)
{
	tcgetattr(STDIN_FILENO, oldt);
	*t = *oldt;
	cfmakeraw(t);
}

static void xset1(struct termios *t)
{
	int ret = tcsetattr(STDIN_FILENO, TCSAFLUSH, t);
	if (ret) {
		bb_perror_msg("can't tcsetattr for stdin");
	}
}

int showkey_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int showkey_main(int argc UNUSED_PARAM, char **argv)
{
	enum {
		OPT_a = (1<<0), // display the decimal/octal/hex values of the keys
		OPT_k = (1<<1), // display only the interpreted keycodes (default)
		OPT_s = (1<<2), // display only the raw scan-codes
	};

	INIT_G();

	// FIXME: aks are all mutually exclusive
	getopt32(argv, "aks");

	// prepare for raw mode
	xget1(&tio, &tio0);
	// put stdin in raw mode
	xset1(&tio);

#define press_keys "Press any keys, program terminates %s:\r\n\n"

	if (option_mask32 & OPT_a) {
		// just read stdin char by char
		unsigned char c;

		printf(press_keys, "on EOF (ctrl-D)");

		// read and show byte values
		while (1 == read(STDIN_FILENO, &c, 1)) {
			printf("%3u 0%03o 0x%02x\r\n", c, c, c);
			if (04 /*CTRL-D*/ == c)
				break;
		}
	} else {
		// we assume a PC keyboard
		xioctl(STDIN_FILENO, KDGKBMODE, &kbmode);
		printf("Keyboard mode was %s.\r\n\n",
			kbmode == K_RAW ? "RAW" :
				(kbmode == K_XLATE ? "XLATE" :
					(kbmode == K_MEDIUMRAW ? "MEDIUMRAW" :
						(kbmode == K_UNICODE ? "UNICODE" : "UNKNOWN")))
		);

		// set raw keyboard mode
		xioctl(STDIN_FILENO, KDSKBMODE, (void *)(ptrdiff_t)((option_mask32 & OPT_k) ? K_MEDIUMRAW : K_RAW));

		// we should exit on any signal; signals should interrupt read
		bb_signals_recursive_norestart(BB_FATAL_SIGS, record_signo);

		// inform user that program ends after time of inactivity
		printf(press_keys, "10s after last keypress");

		// read and show scancodes
		while (!bb_got_signal) {
			char buf[18];
			int i, n;

			// setup 10s watchdog
			alarm(10);

			// read scancodes
			n = read(STDIN_FILENO, buf, sizeof(buf));
			i = 0;
			while (i < n) {
				if (option_mask32 & OPT_s) {
					// show raw scancodes
					printf("0x%02x ", buf[i++]);
				} else {
					// show interpreted scancodes (default)
					char c = buf[i];
					int kc;
					if (i+2 < n
					 && (c & 0x7f) == 0
					 && (buf[i+1] & 0x80) != 0
					 && (buf[i+2] & 0x80) != 0
					) {
						kc = ((buf[i+1] & 0x7f) << 7) | (buf[i+2] & 0x7f);
						i += 3;
					} else {
						kc = (c & 0x7f);
						i++;
					}
					printf("keycode %3u %s", kc, (c & 0x80) ? "release" : "press");
				}
			}
			puts("\r");
		}

		// restore keyboard mode
		xioctl(STDIN_FILENO, KDSKBMODE, (void *)(ptrdiff_t)kbmode);
	}

	// restore console settings
	xset1(&tio0);

	return EXIT_SUCCESS;
}
