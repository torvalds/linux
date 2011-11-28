#ifndef _LTT_ENDIAN_H
#define _LTT_ENDIAN_H

/*
 * ltt-endian.h
 *
 * Copyright 2010 (c) - Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * Dual LGPL v2.1/GPL v2 license.
 */

#ifdef __KERNEL__
# include <asm/byteorder.h>
# ifdef __BIG_ENDIAN
#  define __BYTE_ORDER __BIG_ENDIAN
# elif defined(__LITTLE_ENDIAN)
#  define __BYTE_ORDER __LITTLE_ENDIAN
# else
#  error "unknown endianness"
# endif
#ifndef __BIG_ENDIAN
# define __BIG_ENDIAN 4321
#endif
#ifndef __LITTLE_ENDIAN
# define __LITTLE_ENDIAN 1234
#endif
#else
# include <endian.h>
#endif

#endif /* _LTT_ENDIAN_H */
