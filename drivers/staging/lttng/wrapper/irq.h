#ifndef _LTTNG_WRAPPER_IRQ_H
#define _LTTNG_WRAPPER_IRQ_H

/*
 * wrapper/irq.h
 *
 * wrapper around linux/irq.h.
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

#include <linux/version.h>

/*
 * Starting from the 3.12 Linux kernel, all architectures use the
 * generic hard irqs system. More details can be seen at commit
 * 0244ad004a54e39308d495fee0a2e637f8b5c317 in the Linux kernel GIT.
 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,12,0) \
	|| defined(CONFIG_GENERIC_HARDIRQS))
# define CONFIG_LTTNG_HAS_LIST_IRQ
#endif

#endif /* _LTTNG_WRAPPER_IRQ_H */
