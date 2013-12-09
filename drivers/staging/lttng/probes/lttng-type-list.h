/*
 * lttng-type-list.h
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

/* Type list, used to create metadata */

/* Enumerations */
TRACE_EVENT_ENUM(hrtimer_mode,
        V(HRTIMER_MODE_ABS),
        V(HRTIMER_MODE_REL),
        V(HRTIMER_MODE_PINNED),
        V(HRTIMER_MODE_ABS_PINNED),
        V(HRTIMER_MODE_REL_PINNED),
	R(HRTIMER_MODE_UNDEFINED, 0x04, 0x20),	/* Example (to remove) */
)

TRACE_EVENT_TYPE(hrtimer_mode, enum, unsigned char)
