/* vi: set sw=4 ts=4: */
/*
 * warn_ignoring_args implementation for busybox
 *
 * Copyright (C) 2003  Manuel Novoa III  <mjn3@codepoet.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"

#if ENABLE_DESKTOP
void FAST_FUNC bb_warn_ignoring_args(char *arg)
{
	if (arg) {
		bb_error_msg("ignoring all arguments");
	}
}
#endif
