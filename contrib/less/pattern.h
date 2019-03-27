/*
 * Copyright (C) 1984-2017  Mark Nudelman
 *
 * You may distribute under the terms of either the GNU General Public
 * License or the Less License, as specified in the README file.
 *
 * For more information, see the README file.
 */

#if HAVE_GNU_REGEX
#define __USE_GNU 1
#include <regex.h>
#define PATTERN_TYPE          struct re_pattern_buffer *
#define CLEAR_PATTERN(name)   name = NULL
#endif

#if HAVE_POSIX_REGCOMP
#include <regex.h>
#ifdef REG_EXTENDED
extern int less_is_more;
#define REGCOMP_FLAG    (less_is_more ? 0 : REG_EXTENDED)
#else
#define REGCOMP_FLAG    0
#endif
#define PATTERN_TYPE          regex_t *
#define CLEAR_PATTERN(name)   name = NULL
#endif

#if HAVE_PCRE
#include <pcre.h>
#define PATTERN_TYPE          pcre *
#define CLEAR_PATTERN(name)   name = NULL
#endif

#if HAVE_RE_COMP
char *re_comp LESSPARAMS ((char*));
int re_exec LESSPARAMS ((char*));
#define PATTERN_TYPE          int
#define CLEAR_PATTERN(name)   name = 0
#endif

#if HAVE_REGCMP
char *regcmp LESSPARAMS ((char*));
char *regex LESSPARAMS ((char**, char*));
extern char *__loc1;
#define PATTERN_TYPE          char **
#define CLEAR_PATTERN(name)   name = NULL
#endif

#if HAVE_V8_REGCOMP
#include "regexp.h"
extern int reg_show_error;
#define PATTERN_TYPE          struct regexp *
#define CLEAR_PATTERN(name)   name = NULL
#endif

#if NO_REGEX
#define PATTERN_TYPE          void *
#define CLEAR_PATTERN(name)   
#endif
