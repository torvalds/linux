/*
 * irq.c: API for in kernel interrupt controller
 * Copyright (c) 2007, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 * Authors:
 *   Yaozu (Eddie) Dong <Eddie.dong@intel.com>
 *
 */

#include <linux/module.h>

#include "kvm.h"
#include "irq.h"

/*
 * check if there is pending interrupt without
 * intack.
 */
int kvm_cpu_has_interrupt(struct kvm_vcpu *v)
{
	struct kvm_pic *s = pic_irqchip(v->kvm);

	if (s->output)	/* PIC */
		return 1;
	/*
	 * TODO: APIC
	 */
	return 0;
}
EXPORT_SYMBOL_GPL(kvm_cpu_has_interrupt);

/*
 * Read pending interrupt vector and intack.
 */
int kvm_cpu_get_interrupt(struct kvm_vcpu *v)
{
	struct kvm_pic *s = pic_irqchip(v->kvm);
	int vector;

	s->output = 0;
	vector = kvm_pic_read_irq(s);
	if (vector != -1)
		return vector;
	/*
	 * TODO: APIC
	 */
	return -1;
}
EXPORT_SYMBOL_GPL(kvm_cpu_get_interrupt);
