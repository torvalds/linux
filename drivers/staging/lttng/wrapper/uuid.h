#ifndef _LTTNG_WRAPPER_UUID_H
#define _LTTNG_WRAPPER_UUID_H

/*
 * wrapper/uuid.h
 *
 * Copyright (C) 2011-2012 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
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

#include <linux/version.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35))
#include <linux/uuid.h>
#else

#include <linux/random.h>

typedef struct {
	__u8 b[16];
} uuid_le;

static inline
void uuid_le_gen(uuid_le *u)
{
	generate_random_uuid(u->b);
}

#endif
#endif /* _LTTNG_WRAPPER_UUID_H */
