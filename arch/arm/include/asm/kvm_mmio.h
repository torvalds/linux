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

void kvm_mmio_write_buf(void *buf, unsigned int len, unsigned long data);
unsigned long kvm_mmio_read_buf(const void *buf, unsigned int len);

int kvm_handle_mmio_return(struct kvm_vcpu *vcpu, struct kvm_run *run);
int io_mem_abort(struct kvm_vcpu *vcpu, struct kvm_run *run,
		 phys_addr_t fault_ipa);

#endif	/* __ARM_KVM_MMIO_H__ */
