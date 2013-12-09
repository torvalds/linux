#ifndef _LIB_RING_BUFFER_API_H
#define _LIB_RING_BUFFER_API_H

/*
 * lib/ringbuffer/api.h
 *
 * Ring Buffer API.
 *
 * Copyright (C) 2010-2012 Mathieu Desnoyers <mathieu.desnoyers@efficios.com>
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

#include "../../wrapper/ringbuffer/backend.h"
#include "../../wrapper/ringbuffer/frontend.h"
#include "../../wrapper/ringbuffer/vfs.h"

/*
 * ring_buffer_frontend_api.h contains static inline functions that depend on
 * client static inlines. Hence the inclusion of this "api" header only
 * within the client.
 */
#include "../../wrapper/ringbuffer/frontend_api.h"

#endif /* _LIB_RING_BUFFER_API_H */
