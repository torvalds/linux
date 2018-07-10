/*
 * Utility routines.
 *
 * Copyright (C) 2006 Denys Vlasenko
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */

/* We do not include libbb.h - #define makedev() is there! */
#include "platform.h"

/* Different Unixes want different headers for makedev */
#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) \
 || defined(__APPLE__)
# include <sys/types.h>
#else
# include <features.h>
# include <sys/sysmacros.h>
#endif

#ifdef __GLIBC__
/* At least glibc has horrendously large inline for this, so wrap it. */
/* uclibc people please check - do we need "&& !__UCLIBC__" above? */

/* Suppress gcc "no previous prototype" warning */
unsigned long long FAST_FUNC bb_makedev(unsigned major, unsigned minor);
unsigned long long FAST_FUNC bb_makedev(unsigned major, unsigned minor)
{
	return makedev(major, minor);
}
#endif
