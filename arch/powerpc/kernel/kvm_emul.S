/*
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
 *
 * Copyright SUSE Linux Products GmbH 2010
 *
 * Authors: Alexander Graf <agraf@suse.de>
 */

#include <asm/ppc_asm.h>
#include <asm/kvm_asm.h>
#include <asm/reg.h>
#include <asm/page.h>
#include <asm/asm-offsets.h>

/* Hypercall entry point. Will be patched with device tree instructions. */

.global kvm_hypercall_start
kvm_hypercall_start:
	li	r3, -1
	nop
	nop
	nop
	blr

#define KVM_MAGIC_PAGE		(-4096)

#ifdef CONFIG_64BIT
#define LL64(reg, offs, reg2)	ld	reg, (offs)(reg2)
#define STL64(reg, offs, reg2)	std	reg, (offs)(reg2)
#else
#define LL64(reg, offs, reg2)	lwz	reg, (offs + 4)(reg2)
#define STL64(reg, offs, reg2)	stw	reg, (offs + 4)(reg2)
#endif

#define SCRATCH_SAVE							\
	/* Enable critical section. We are critical if			\
	   shared->critical == r1 */					\
	STL64(r1, KVM_MAGIC_PAGE + KVM_MAGIC_CRITICAL, 0);		\
									\
	/* Save state */						\
	PPC_STL	r31, (KVM_MAGIC_PAGE + KVM_MAGIC_SCRATCH1)(0);		\
	PPC_STL	r30, (KVM_MAGIC_PAGE + KVM_MAGIC_SCRATCH2)(0);		\
	mfcr	r31;							\
	stw	r31, (KVM_MAGIC_PAGE + KVM_MAGIC_SCRATCH3)(0);

#define SCRATCH_RESTORE							\
	/* Restore state */						\
	PPC_LL	r31, (KVM_MAGIC_PAGE + KVM_MAGIC_SCRATCH1)(0);		\
	lwz	r30, (KVM_MAGIC_PAGE + KVM_MAGIC_SCRATCH3)(0);		\
	mtcr	r30;							\
	PPC_LL	r30, (KVM_MAGIC_PAGE + KVM_MAGIC_SCRATCH2)(0);		\
									\
	/* Disable critical section. We are critical if			\
	   shared->critical == r1 and r2 is always != r1 */		\
	STL64(r2, KVM_MAGIC_PAGE + KVM_MAGIC_CRITICAL, 0);
