/*
 * VGIC system registers handling functions for AArch32 mode
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kvm.h>
#include <linux/kvm_host.h>
#include <asm/kvm_emulate.h>
#include "vgic.h"

int vgic_v3_has_cpu_sysregs_attr(struct kvm_vcpu *vcpu, bool is_write, u64 id,
				 u64 *reg)
{
	/*
	 * TODO: Implement for AArch32
	 */
	return -ENXIO;
}

int vgic_v3_cpu_sysregs_uaccess(struct kvm_vcpu *vcpu, bool is_write, u64 id,
				u64 *reg)
{
	/*
	 * TODO: Implement for AArch32
	 */
	return -ENXIO;
}
