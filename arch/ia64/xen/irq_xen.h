/******************************************************************************
 * arch/ia64/xen/irq_xen.h
 *
 * Copyright (c) 2008 Isaku Yamahata <yamahata at valinux co jp>
 *                    VA Linux Systems Japan K.K.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#ifndef IRQ_XEN_H
#define IRQ_XEN_H

extern void (*late_time_init)(void);
extern char xen_event_callback;
void __init xen_init_IRQ(void);

extern const struct pv_irq_ops xen_irq_ops __initconst;
extern void xen_smp_intr_init(void);
extern void xen_send_ipi(int cpu, int vec);

#endif /* IRQ_XEN_H */
