/* SPDX-License-Identifier: GPL-2.0 */
extern void register_percpu_irq(ia64_vector vec, irq_handler_t handler,
				unsigned long flags, const char *name);
