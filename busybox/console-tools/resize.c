/* vi: set sw=4 ts=4: */
/*
 * resize - set terminal width and height.
 *
 * Copyright 2006 Bernhard Reutner-Fischer
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config RESIZE
//config:	bool "resize (756 bytes)"
//config:	default y
//config:	help
//config:	This program is used to (re)set the width and height of your current
//config:	terminal.
//config:
//config:config FEATURE_RESIZE_PRINT
//config:	bool "Print environment variables"
//config:	default y
//config:	depends on RESIZE
//config:	help
//config:	Prints the newly set size (number of columns and rows) of
//config:	the terminal.
//config:	E.g.:
//config:	COLUMNS=80;LINES=44;export COLUMNS LINES;

//applet:IF_RESIZE(APPLET_NOEXEC(resize, resize, BB_DIR_USR_BIN, BB_SUID_DROP, resize))
/* bb_common_bufsiz1 usage here is safe wrt NOEXEC: not expecting it to be zeroed. */

//kbuild:lib-$(CONFIG_RESIZE) += resize.o

//usage:#define resize_trivial_usage
//usage:       ""
//usage:#define resize_full_usage "\n\n"
//usage:       "Resize the screen"

#include "libbb.h"
#include "common_bufsiz.h"

#define ESC "\033"

#define old_termios_p ((struct termios*)bb_common_bufsiz1)
#define INIT_G() do { setup_common_bufsiz(); } while (0)

static void
onintr(int sig UNUSED_PARAM)
{
	tcsetattr(STDERR_FILENO, TCSANOW, old_termios_p);
	_exit(EXIT_FAILURE);
}

int resize_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int resize_main(int argc UNUSED_PARAM, char **argv UNUSED_PARAM)
{
	struct termios new;
	struct winsize w = { 0, 0, 0, 0 };
	int ret;

	INIT_G();

	/* We use _stderr_ in order to make resize usable
	 * in shell backticks (those redirect stdout away from tty).
	 * NB: other versions of resize open "/dev/tty"
	 * and operate on it - should we do the same?
	 */

	tcgetattr(STDERR_FILENO, old_termios_p); /* fiddle echo */
//TODO: die if the above fails?
	memcpy(&new, old_termios_p, sizeof(new));
	new.c_cflag |= (CLOCAL | CREAD);
	new.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
	bb_signals(0
		+ (1 << SIGINT)
		+ (1 << SIGQUIT)
		+ (1 << SIGTERM)
		+ (1 << SIGALRM)
		, onintr);
	tcsetattr(STDERR_FILENO, TCSANOW, &new);

	/* save_cursor_pos 7
	 * scroll_whole_screen [r
	 * put_cursor_waaaay_off [$x;$yH
	 * get_cursor_pos [6n
	 * restore_cursor_pos 8
	 */
	fprintf(stderr, ESC"7" ESC"[r" ESC"[999;999H" ESC"[6n");
	alarm(3); /* Just in case terminal won't answer */
//BUG: death by signal won't restore termios
	scanf(ESC"[%hu;%huR", &w.ws_row, &w.ws_col);
	fprintf(stderr, ESC"8");

	/* BTW, other versions of resize recalculate w.ws_xpixel, ws.ws_ypixel
	 * by calculating character cell HxW from old values
	 * (gotten via TIOCGWINSZ) and recomputing *pixel values */
	ret = ioctl(STDERR_FILENO, TIOCSWINSZ, &w);

	tcsetattr(STDERR_FILENO, TCSANOW, old_termios_p);

	if (ENABLE_FEATURE_RESIZE_PRINT)
		printf("COLUMNS=%d;LINES=%d;export COLUMNS LINES;\n",
			w.ws_col, w.ws_row);

	return ret;
}
