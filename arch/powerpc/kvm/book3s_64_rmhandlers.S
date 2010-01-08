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
 * Copyright SUSE Linux Products GmbH 2009
 *
 * Authors: Alexander Graf <agraf@suse.de>
 */

#include <asm/ppc_asm.h>
#include <asm/kvm_asm.h>
#include <asm/reg.h>
#include <asm/page.h>
#include <asm/asm-offsets.h>
#include <asm/exception-64s.h>

/*****************************************************************************
 *                                                                           *
 *        Real Mode handlers that need to be in low physical memory          *
 *                                                                           *
 ****************************************************************************/


.macro INTERRUPT_TRAMPOLINE intno

.global kvmppc_trampoline_\intno
kvmppc_trampoline_\intno:

	mtspr	SPRN_SPRG_SCRATCH0, r13		/* Save r13 */

	/*
	 * First thing to do is to find out if we're coming
	 * from a KVM guest or a Linux process.
	 *
	 * To distinguish, we check a magic byte in the PACA
	 */
	mfspr	r13, SPRN_SPRG_PACA		/* r13 = PACA */
	std	r12, PACA_KVM_SCRATCH0(r13)
	mfcr	r12
	stw	r12, PACA_KVM_SCRATCH1(r13)
	lbz	r12, PACA_KVM_IN_GUEST(r13)
	cmpwi	r12, 0
	bne	..kvmppc_handler_hasmagic_\intno
	/* No KVM guest? Then jump back to the Linux handler! */
	lwz	r12, PACA_KVM_SCRATCH1(r13)
	mtcr	r12
	ld	r12, PACA_KVM_SCRATCH0(r13)
	mfspr	r13, SPRN_SPRG_SCRATCH0		/* r13 = original r13 */
	b	kvmppc_resume_\intno		/* Get back original handler */

	/* Now we know we're handling a KVM guest */
..kvmppc_handler_hasmagic_\intno:
	/* Let's store which interrupt we're handling */
	li	r12, \intno

	/* Jump into the SLB exit code that goes to the highmem handler */
	b	kvmppc_handler_trampoline_exit

.endm

INTERRUPT_TRAMPOLINE	BOOK3S_INTERRUPT_SYSTEM_RESET
INTERRUPT_TRAMPOLINE	BOOK3S_INTERRUPT_MACHINE_CHECK
INTERRUPT_TRAMPOLINE	BOOK3S_INTERRUPT_DATA_STORAGE
INTERRUPT_TRAMPOLINE	BOOK3S_INTERRUPT_DATA_SEGMENT
INTERRUPT_TRAMPOLINE	BOOK3S_INTERRUPT_INST_STORAGE
INTERRUPT_TRAMPOLINE	BOOK3S_INTERRUPT_INST_SEGMENT
INTERRUPT_TRAMPOLINE	BOOK3S_INTERRUPT_EXTERNAL
INTERRUPT_TRAMPOLINE	BOOK3S_INTERRUPT_ALIGNMENT
INTERRUPT_TRAMPOLINE	BOOK3S_INTERRUPT_PROGRAM
INTERRUPT_TRAMPOLINE	BOOK3S_INTERRUPT_FP_UNAVAIL
INTERRUPT_TRAMPOLINE	BOOK3S_INTERRUPT_DECREMENTER
INTERRUPT_TRAMPOLINE	BOOK3S_INTERRUPT_SYSCALL
INTERRUPT_TRAMPOLINE	BOOK3S_INTERRUPT_TRACE
INTERRUPT_TRAMPOLINE	BOOK3S_INTERRUPT_PERFMON
INTERRUPT_TRAMPOLINE	BOOK3S_INTERRUPT_ALTIVEC
INTERRUPT_TRAMPOLINE	BOOK3S_INTERRUPT_VSX

/*
 * This trampoline brings us back to a real mode handler
 *
 * Input Registers:
 *
 * R5 = SRR0
 * R6 = SRR1
 * LR = real-mode IP
 *
 */
.global kvmppc_handler_lowmem_trampoline
kvmppc_handler_lowmem_trampoline:

	mtsrr0	r5
	mtsrr1	r6
	blr
kvmppc_handler_lowmem_trampoline_end:

.global kvmppc_trampoline_lowmem
kvmppc_trampoline_lowmem:
	.long kvmppc_handler_lowmem_trampoline - _stext

.global kvmppc_trampoline_enter
kvmppc_trampoline_enter:
	.long kvmppc_handler_trampoline_enter - _stext

#include "book3s_64_slb.S"

