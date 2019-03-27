/**
 * \file common.h
 *
 * Common definitions for LDNS
 */

/**
 * a Net::DNS like library for C
 *
 * (c) NLnet Labs, 2004-2006
 *
 * See the file LICENSE for the license
 */

#ifndef LDNS_COMMON_H
#define LDNS_COMMON_H

/*
 * The build configuration that is used in the distributed headers,
 * as detected and determined by the auto configure script.
 */
#define LDNS_BUILD_CONFIG_HAVE_SSL         1
#define LDNS_BUILD_CONFIG_HAVE_INTTYPES_H  1
#define LDNS_BUILD_CONFIG_HAVE_ATTR_FORMAT 1
#define LDNS_BUILD_CONFIG_HAVE_ATTR_UNUSED 1
#define LDNS_BUILD_CONFIG_HAVE_SOCKLEN_T   1
#define LDNS_BUILD_CONFIG_USE_DANE         1
#define LDNS_BUILD_CONFIG_HAVE_B32_PTON    0
#define LDNS_BUILD_CONFIG_HAVE_B32_NTOP    0

/*
 * HAVE_STDBOOL_H is not available when distributed as a library, but no build 
 * configuration variables may be used (like those above) because the header
 * is sometimes only available when using special compiler flags to enable the
 * c99 environment. Because we cannot force the usage of this flag, we have to
 * provide a default type. Below what is suggested by the autoconf manual.
 */
/*@ignore@*/
/* splint barfs on this construct */
#ifndef __bool_true_false_are_defined
# ifdef HAVE_STDBOOL_H
#  include <stdbool.h>
# else
#  ifndef HAVE__BOOL
#   ifdef __cplusplus
typedef bool _Bool;
#   else
#    define _Bool signed char
#   endif
#  endif
#  define bool _Bool
#  define false 0
#  define true 1
#  define __bool_true_false_are_defined 1
# endif
#endif
/*@end@*/

#if LDNS_BUILD_CONFIG_HAVE_ATTR_FORMAT
#define ATTR_FORMAT(archetype, string_index, first_to_check) \
    __attribute__ ((format (archetype, string_index, first_to_check)))
#else /* !LDNS_BUILD_CONFIG_HAVE_ATTR_FORMAT */
#define ATTR_FORMAT(archetype, string_index, first_to_check) /* empty */
#endif /* !LDNS_BUILD_CONFIG_HAVE_ATTR_FORMAT */

#if defined(__cplusplus)
#define ATTR_UNUSED(x)
#elif LDNS_BUILD_CONFIG_HAVE_ATTR_UNUSED
#define ATTR_UNUSED(x)  x __attribute__((unused))
#else /* !LDNS_BUILD_CONFIG_HAVE_ATTR_UNUSED */
#define ATTR_UNUSED(x)  x
#endif /* !LDNS_BUILD_CONFIG_HAVE_ATTR_UNUSED */

#if !LDNS_BUILD_CONFIG_HAVE_SOCKLEN_T
typedef int socklen_t;
#endif

#endif /* LDNS_COMMON_H */
