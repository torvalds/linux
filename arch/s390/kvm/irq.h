/* SPDX-License-Identifier: GPL-2.0 */
/*
 * s390 irqchip routines
 *
 * Copyright IBM Corp. 2014
 *
 *    Author(s): Cornelia Huck <cornelia.huck@de.ibm.com>
 */
#ifndef __KVM_IRQ_H
#define __KVM_IRQ_H

#include <linux/kvm_host.h>

static inline int irqchip_in_kernel(struct kvm *kvm)
{
	return 1;
}

#endif
