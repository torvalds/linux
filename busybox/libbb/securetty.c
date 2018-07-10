/* vi: set sw=4 ts=4: */
/*
 * /etc/securetty checking.
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
#include "libbb.h"

#if ENABLE_FEATURE_SECURETTY && !ENABLE_PAM
int FAST_FUNC is_tty_secure(const char *short_tty)
{
	char *buf = (char*)"/etc/securetty"; /* any non-NULL is ok */
	parser_t *parser = config_open2("/etc/securetty", fopen_for_read);
	while (config_read(parser, &buf, 1, 1, "# \t", PARSE_NORMAL)) {
		if (strcmp(buf, short_tty) == 0)
			break;
		buf = NULL;
	}
	config_close(parser);
	/* buf != NULL here if config file was not found, empty
	 * or line was found which equals short_tty.
	 * In all these cases, we report "this tty is secure".
	 */
	return buf != NULL;
}
#endif
