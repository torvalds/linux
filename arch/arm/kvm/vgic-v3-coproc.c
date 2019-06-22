// SPDX-License-Identifier: GPL-2.0-only
/*
 * VGIC system registers handling functions for AArch32 mode
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
