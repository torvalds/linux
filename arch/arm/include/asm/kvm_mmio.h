/*
 * Copyright (C) 2012 - Virtual Open Systems and Columbia University
 * Author: Christoffer Dall <c.dall@virtualopensystems.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __ARM_KVM_MMIO_H__
#define __ARM_KVM_MMIO_H__

#include <linux/kvm_host.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_arm.h>

struct kvm_decode {
	unsigned long rt;
	bool sign_extend;
};

/*
 * The in-kernel MMIO emulation code wants to use a copy of run->mmio,
 * which is an anonymous type. Use our own type instead.
 */
struct kvm_exit_mmio {
	phys_addr_t	phys_addr;
	u8		data[8];
	u32		len;
	bool		is_write;
	void		*private;
};

static inline void kvm_prepare_mmio(struct kvm_run *run,
				    struct kvm_exit_mmio *mmio)
{
	run->mmio.phys_addr	= mmio->phys_addr;
	run->mmio.len		= mmio->len;
	run->mmio.is_write	= mmio->is_write;
	memcpy(run->mmio.data, mmio->data, mmio->len);
	run->exit_reason	= KVM_EXIT_MMIO;
}

int kvm_handle_mmio_return(struct kvm_vcpu *vcpu, struct kvm_run *run);
int io_mem_abort(struct kvm_vcpu *vcpu, struct kvm_run *run,
		 phys_addr_t fault_ipa);

#endif	/* __ARM_KVM_MMIO_H__ */
