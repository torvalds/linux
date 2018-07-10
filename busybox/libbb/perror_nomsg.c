/* vi: set sw=4 ts=4: */
/*
 * bb_perror_nomsg implementation for busybox
 *
 * Copyright (C) 2003  Manuel Novoa III  <mjn3@codepoet.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */

/* gcc warns about a null format string, therefore we provide
 * modified definition without "attribute (format)"
 * instead of including libbb.h */
//#include "libbb.h"
#include "platform.h"
extern void bb_perror_msg(const char *s, ...) FAST_FUNC;

/* suppress gcc "no previous prototype" warning */
void FAST_FUNC bb_perror_nomsg(void);
void FAST_FUNC bb_perror_nomsg(void)
{
	bb_perror_msg(0);
}
