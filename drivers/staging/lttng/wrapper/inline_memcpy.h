/*
 * wrapper/inline_memcpy.h
 *
 * Copyright (C) 2010-2011 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * Dual LGPL v2.1/GPL v2 license.
 */

#if !defined(__HAVE_ARCH_INLINE_MEMCPY) && !defined(inline_memcpy)
#define inline_memcpy memcpy
#endif
