/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 *
 * Copyright (C) 2014 by Fugro Intersite B.V. <m.stam@fugro.nl>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"

void FAST_FUNC bb_logenv_override(void)
{
	const char* mode = getenv("LOGGING");

	if (!mode)
		return;

	if (strcmp(mode, "none") == 0)
		logmode = LOGMODE_NONE;
#if ENABLE_FEATURE_SYSLOG
	else if (strcmp(mode, "syslog") == 0)
		logmode = LOGMODE_SYSLOG;
#endif
}
