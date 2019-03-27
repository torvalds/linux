/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2014 Garrett D'Amore <garrett@damore.org>
 *
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */


#ifndef	_LIBINTL_H
#define	_LIBINTL_H

#include <sys/isa_defs.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * wchar_t is a built-in type in standard C++ and as such is not
 * defined here when using standard C++. However, the GNU compiler
 * fixincludes utility nonetheless creates its own version of this
 * header for use by gcc and g++. In that version it adds a redundant
 * guard for __cplusplus. To avoid the creation of a gcc/g++ specific
 * header we need to include the following magic comment:
 *
 * we must use the C++ compiler's type
 *
 * The above comment should not be removed or changed until GNU
 * gcc/fixinc/inclhack.def is updated to bypass this header.
 */
#if !defined(__cplusplus) || (__cplusplus < 199711L && !defined(__GNUG__))
#ifndef _WCHAR_T
#define	_WCHAR_T
#if defined(_LP64)
typedef int	wchar_t;
#else
typedef long	wchar_t;
#endif
#endif	/* !_WCHAR_T */
#endif	/* !defined(__cplusplus) ... */

#define	TEXTDOMAINMAX	256

#define	__GNU_GETTEXT_SUPPORTED_REVISION(m)	\
	((((m) == 0) || ((m) == 1)) ? 1 : -1)

extern char *dcgettext(const char *, const char *, const int);
extern char *dgettext(const char *, const char *);
extern char *gettext(const char *);
extern char *textdomain(const char *);
extern char *bindtextdomain(const char *, const char *);

/*
 * LI18NUX 2000 Globalization Specification Version 1.0
 * with Amendment 2
 */
extern char *dcngettext(const char *, const char *,
	const char *, unsigned long int, int);
extern char *dngettext(const char *, const char *,
	const char *, unsigned long int);
extern char *ngettext(const char *, const char *, unsigned long int);
extern char *bind_textdomain_codeset(const char *, const char *);

/* Word handling functions --- requires dynamic linking */
/* Warning: these are experimental and subject to change. */
extern int wdinit(void);
extern int wdchkind(wchar_t);
extern int wdbindf(wchar_t, wchar_t, int);
extern wchar_t *wddelim(wchar_t, wchar_t, int);
extern wchar_t mcfiller(void);
extern int mcwrap(void);

#ifdef	__cplusplus
}
#endif

#endif /* _LIBINTL_H */
