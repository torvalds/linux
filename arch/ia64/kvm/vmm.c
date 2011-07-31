/*
 * vmm.c: vmm module interface with kvm module
 *
 * Copyright (c) 2007, Intel Corporation.
 *
 *  Xiantao Zhang (xiantao.zhang@intel.com)
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
 */


#include<linux/kernel.h>
#include<linux/module.h>
#include<asm/fpswa.h>

#include "vcpu.h"

MODULE_AUTHOR("Intel");
MODULE_LICENSE("GPL");

extern char kvm_ia64_ivt;
extern char kvm_asm_mov_from_ar;
extern char kvm_asm_mov_from_ar_sn2;
extern fpswa_interface_t *vmm_fpswa_interface;

long vmm_sanity = 1;

struct kvm_vmm_info vmm_info = {
	.module			= THIS_MODULE,
	.vmm_entry		= vmm_entry,
	.tramp_entry		= vmm_trampoline,
	.vmm_ivt		= (unsigned long)&kvm_ia64_ivt,
	.patch_mov_ar		= (unsigned long)&kvm_asm_mov_from_ar,
	.patch_mov_ar_sn2	= (unsigned long)&kvm_asm_mov_from_ar_sn2,
};

static int __init  kvm_vmm_init(void)
{

	vmm_fpswa_interface = fpswa_interface;

	/*Register vmm data to kvm side*/
	return kvm_init(&vmm_info, 1024, 0, THIS_MODULE);
}

static void __exit kvm_vmm_exit(void)
{
	kvm_exit();
	return ;
}

void vmm_spin_lock(vmm_spinlock_t *lock)
{
	_vmm_raw_spin_lock(lock);
}

void vmm_spin_unlock(vmm_spinlock_t *lock)
{
	_vmm_raw_spin_unlock(lock);
}

static void vcpu_debug_exit(struct kvm_vcpu *vcpu)
{
	struct exit_ctl_data *p = &vcpu->arch.exit_data;
	long psr;

	local_irq_save(psr);
	p->exit_reason = EXIT_REASON_DEBUG;
	vmm_transition(vcpu);
	local_irq_restore(psr);
}

asmlinkage int printk(const char *fmt, ...)
{
	struct kvm_vcpu *vcpu = current_vcpu;
	va_list args;
	int r;

	memset(vcpu->arch.log_buf, 0, VMM_LOG_LEN);
	va_start(args, fmt);
	r = vsnprintf(vcpu->arch.log_buf, VMM_LOG_LEN, fmt, args);
	va_end(args);
	vcpu_debug_exit(vcpu);
	return r;
}

module_init(kvm_vmm_init)
module_exit(kvm_vmm_exit)
