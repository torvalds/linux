#ifndef _LTTNG_WRAPPER_COMPILER_H
#define _LTTNG_WRAPPER_COMPILER_H

/*
 * wrapper/compiler.h
 *
 * Copyright (C) 2013 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; only
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <linux/compiler.h>

/*
 * Don't allow compiling with buggy compiler.
 */

#ifdef GCC_VERSION

/*
 * http://gcc.gnu.org/bugzilla/show_bug.cgi?id=58854
 */
# ifdef __ARMEL__
#  if GCC_VERSION >= 40800 && GCC_VERSION <= 40802
#   error Your gcc version produces clobbered frame accesses
#  endif
# endif
#endif

#endif /* _LTTNG_WRAPPER_COMPILER_H */
