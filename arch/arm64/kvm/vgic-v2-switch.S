/*
 * Copyright (C) 2012,2013 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/linkage.h>
#include <linux/irqchip/arm-gic.h>

#include <asm/assembler.h>
#include <asm/memory.h>
#include <asm/asm-offsets.h>
#include <asm/kvm.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_arm.h>
#include <asm/kvm_mmu.h>

	.text
	.pushsection	.hyp.text, "ax"

/*
 * Save the VGIC CPU state into memory
 * x0: Register pointing to VCPU struct
 * Do not corrupt x1!!!
 */
ENTRY(__save_vgic_v2_state)
__save_vgic_v2_state:
	/* Get VGIC VCTRL base into x2 */
	ldr	x2, [x0, #VCPU_KVM]
	kern_hyp_va	x2
	ldr	x2, [x2, #KVM_VGIC_VCTRL]
	kern_hyp_va	x2
	cbz	x2, 2f		// disabled

	/* Compute the address of struct vgic_cpu */
	add	x3, x0, #VCPU_VGIC_CPU

	/* Save all interesting registers */
	ldr	w4, [x2, #GICH_HCR]
	ldr	w5, [x2, #GICH_VMCR]
	ldr	w6, [x2, #GICH_MISR]
	ldr	w7, [x2, #GICH_EISR0]
	ldr	w8, [x2, #GICH_EISR1]
	ldr	w9, [x2, #GICH_ELRSR0]
	ldr	w10, [x2, #GICH_ELRSR1]
	ldr	w11, [x2, #GICH_APR]
CPU_BE(	rev	w4,  w4  )
CPU_BE(	rev	w5,  w5  )
CPU_BE(	rev	w6,  w6  )
CPU_BE(	rev	w7,  w7  )
CPU_BE(	rev	w8,  w8  )
CPU_BE(	rev	w9,  w9  )
CPU_BE(	rev	w10, w10 )
CPU_BE(	rev	w11, w11 )

	str	w4, [x3, #VGIC_V2_CPU_HCR]
	str	w5, [x3, #VGIC_V2_CPU_VMCR]
	str	w6, [x3, #VGIC_V2_CPU_MISR]
CPU_LE(	str	w7, [x3, #VGIC_V2_CPU_EISR] )
CPU_LE(	str	w8, [x3, #(VGIC_V2_CPU_EISR + 4)] )
CPU_LE(	str	w9, [x3, #VGIC_V2_CPU_ELRSR] )
CPU_LE(	str	w10, [x3, #(VGIC_V2_CPU_ELRSR + 4)] )
CPU_BE(	str	w7, [x3, #(VGIC_V2_CPU_EISR + 4)] )
CPU_BE(	str	w8, [x3, #VGIC_V2_CPU_EISR] )
CPU_BE(	str	w9, [x3, #(VGIC_V2_CPU_ELRSR + 4)] )
CPU_BE(	str	w10, [x3, #VGIC_V2_CPU_ELRSR] )
	str	w11, [x3, #VGIC_V2_CPU_APR]

	/* Clear GICH_HCR */
	str	wzr, [x2, #GICH_HCR]

	/* Save list registers */
	add	x2, x2, #GICH_LR0
	ldr	w4, [x3, #VGIC_CPU_NR_LR]
	add	x3, x3, #VGIC_V2_CPU_LR
1:	ldr	w5, [x2], #4
CPU_BE(	rev	w5, w5 )
	str	w5, [x3], #4
	sub	w4, w4, #1
	cbnz	w4, 1b
2:
	ret
ENDPROC(__save_vgic_v2_state)

/*
 * Restore the VGIC CPU state from memory
 * x0: Register pointing to VCPU struct
 */
ENTRY(__restore_vgic_v2_state)
__restore_vgic_v2_state:
	/* Get VGIC VCTRL base into x2 */
	ldr	x2, [x0, #VCPU_KVM]
	kern_hyp_va	x2
	ldr	x2, [x2, #KVM_VGIC_VCTRL]
	kern_hyp_va	x2
	cbz	x2, 2f		// disabled

	/* Compute the address of struct vgic_cpu */
	add	x3, x0, #VCPU_VGIC_CPU

	/* We only restore a minimal set of registers */
	ldr	w4, [x3, #VGIC_V2_CPU_HCR]
	ldr	w5, [x3, #VGIC_V2_CPU_VMCR]
	ldr	w6, [x3, #VGIC_V2_CPU_APR]
CPU_BE(	rev	w4, w4 )
CPU_BE(	rev	w5, w5 )
CPU_BE(	rev	w6, w6 )

	str	w4, [x2, #GICH_HCR]
	str	w5, [x2, #GICH_VMCR]
	str	w6, [x2, #GICH_APR]

	/* Restore list registers */
	add	x2, x2, #GICH_LR0
	ldr	w4, [x3, #VGIC_CPU_NR_LR]
	add	x3, x3, #VGIC_V2_CPU_LR
1:	ldr	w5, [x3], #4
CPU_BE(	rev	w5, w5 )
	str	w5, [x2], #4
	sub	w4, w4, #1
	cbnz	w4, 1b
2:
	ret
ENDPROC(__restore_vgic_v2_state)

	.popsection
