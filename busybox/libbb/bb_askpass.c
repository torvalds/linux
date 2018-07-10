/* vi: set sw=4 ts=4: */
/*
 * Ask for a password
 *
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"

/* do nothing signal handler */
static void askpass_timeout(int UNUSED_PARAM ignore)
{
}

char* FAST_FUNC bb_ask_noecho(int fd, int timeout, const char *prompt)
{
#define MAX_LINE 0xfff
	char *ret;
	int i;
	struct sigaction sa, oldsa;
	struct termios tio, oldtio;

	tcflush(fd, TCIFLUSH);
	/* Was buggy: was printing prompt *before* flushing input,
	 * which was upsetting "expect" based scripts of some users.
	 */
	fputs(prompt, stdout);
	fflush_all();

	tcgetattr(fd, &oldtio);
	tio = oldtio;
	/* Switch off echo. ECHOxyz meaning:
	 * ECHO    echo input chars
	 * ECHOE   echo BS-SP-BS on erase character
	 * ECHOK   echo kill char specially, not as ^c (ECHOKE controls how exactly)
	 * ECHOKE  erase all input via BS-SP-BS on kill char (else go to next line)
	 * ECHOCTL Echo ctrl chars as ^c (else echo verbatim:
	 *         e.g. up arrow emits "ESC-something" and thus moves cursor up!)
	 * ECHONL  Echo NL even if ECHO is not set
	 * ECHOPRT On erase, echo erased chars
	 *         [qwe<BS><BS><BS> input looks like "qwe\ewq/" on screen]
	 */
	tio.c_lflag &= ~(ECHO|ECHOE|ECHOK|ECHONL);
	tcsetattr(fd, TCSANOW, &tio);

	memset(&sa, 0, sizeof(sa));
	/* sa.sa_flags = 0; - no SA_RESTART! */
	/* SIGINT and SIGALRM will interrupt reads below */
	sa.sa_handler = askpass_timeout;
	sigaction(SIGINT, &sa, &oldsa);
	if (timeout) {
		sigaction_set(SIGALRM, &sa);
		alarm(timeout);
	}

	ret = NULL;
	i = 0;
	while (1) {
		int r;

		/* User input is uber-slow, no need to optimize reallocs.
		 * Grow it on every char.
		 */
		ret = xrealloc(ret, i + 2);
		r = read(fd, &ret[i], 1);

		if ((i == 0 && r == 0) /* EOF (^D) with no password */
		 || r < 0 /* read is interrupted by timeout or ^C */
		) {
			ret[i] = '\0'; /* paranoia */
			nuke_str(ret); /* paranoia */
			free(ret);
			ret = NULL;
			break;
		}

		if (r == 0 /* EOF */
		 || ret[i] == '\r' || ret[i] == '\n' /* EOL */
		 || ++i == MAX_LINE /* line limit */
		) {
			ret[i] = '\0';
			break;
		}
	}

	if (timeout) {
		alarm(0);
	}
	sigaction_set(SIGINT, &oldsa);
	tcsetattr(fd, TCSANOW, &oldtio);
	bb_putchar('\n');
	fflush_all();
	return ret;
}
char* FAST_FUNC bb_ask_noecho_stdin(const char *prompt)
{
	return bb_ask_noecho(STDIN_FILENO, 0, prompt);
}
