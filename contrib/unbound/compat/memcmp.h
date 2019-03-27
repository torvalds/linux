/*
 *	memcmp.h: undef memcmp for compat.
 *
 *	Copyright (c) 2012, NLnet Labs. All rights reserved.
 *
 * See LICENSE for the license.
*/
#ifndef COMPAT_MEMCMP_H
#define COMPAT_MEMCMP_H

#ifdef memcmp
/* undef here otherwise autoheader messes it up in config.h */
#  undef memcmp
#endif

#endif /* COMPAT_MEMCMP_H */
