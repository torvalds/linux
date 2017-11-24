/* SPDX-License-Identifier: GPL-2.0 */
/*
 * s390 irqchip routines
 *
 * Copyright IBM Corp. 2014
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
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
