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

#include <linux/linkage.h>
#include <linux/const.h>
#include <asm/unified.h>
#include <asm/page.h>
#include <asm/asm-offsets.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_arm.h>

	.text

__kvm_hyp_code_start:
	.globl __kvm_hyp_code_start

/********************************************************************
 * Flush per-VMID TLBs
 */
ENTRY(__kvm_flush_vm_context)
	bx	lr
ENDPROC(__kvm_flush_vm_context)

/********************************************************************
 *  Hypervisor world-switch code
 */
ENTRY(__kvm_vcpu_run)
	bx	lr

/********************************************************************
 *  Call function in Hyp mode
 *
 *
 * u64 kvm_call_hyp(void *hypfn, ...);
 *
 * This is not really a variadic function in the classic C-way and care must
 * be taken when calling this to ensure parameters are passed in registers
 * only, since the stack will change between the caller and the callee.
 *
 * Call the function with the first argument containing a pointer to the
 * function you wish to call in Hyp mode, and subsequent arguments will be
 * passed as r0, r1, and r2 (a maximum of 3 arguments in addition to the
 * function pointer can be passed).  The function being called must be mapped
 * in Hyp mode (see init_hyp_mode in arch/arm/kvm/arm.c).  Return values are
 * passed in r0 and r1.
 *
 * The calling convention follows the standard AAPCS:
 *   r0 - r3: caller save
 *   r12:     caller save
 *   rest:    callee save
 */
ENTRY(kvm_call_hyp)
	hvc	#0
	bx	lr

/********************************************************************
 * Hypervisor exception vector and handlers
 */

	.align 5
__kvm_hyp_vector:
	.globl __kvm_hyp_vector
	nop

__kvm_hyp_code_end:
	.globl	__kvm_hyp_code_end
