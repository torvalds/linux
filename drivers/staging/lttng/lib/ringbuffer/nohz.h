#ifndef _LIB_RING_BUFFER_NOHZ_H
#define _LIB_RING_BUFFER_NOHZ_H

/*
 * lib/ringbuffer/nohz.h
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

#ifdef CONFIG_LIB_RING_BUFFER
void lib_ring_buffer_tick_nohz_flush(void);
void lib_ring_buffer_tick_nohz_stop(void);
void lib_ring_buffer_tick_nohz_restart(void);
#else
static inline void lib_ring_buffer_tick_nohz_flush(void)
{
}

static inline void lib_ring_buffer_tick_nohz_stop(void)
{
}

static inline void lib_ring_buffer_tick_nohz_restart(void)
{
}
#endif

#endif /* _LIB_RING_BUFFER_NOHZ_H */
