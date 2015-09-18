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
#include <linux/irqchip/arm-gic-v3.h>

#include <asm/assembler.h>
#include <asm/memory.h>
#include <asm/asm-offsets.h>
#include <asm/kvm.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_arm.h>

	.text
	.pushsection	.hyp.text, "ax"

/*
 * We store LRs in reverse order to let the CPU deal with streaming
 * access. Use this macro to make it look saner...
 */
#define LR_OFFSET(n)	(VGIC_V3_CPU_LR + (15 - n) * 8)

/*
 * Save the VGIC CPU state into memory
 * x0: Register pointing to VCPU struct
 * Do not corrupt x1!!!
 */
.macro	save_vgic_v3_state
	// Compute the address of struct vgic_cpu
	add	x3, x0, #VCPU_VGIC_CPU

	// Make sure stores to the GIC via the memory mapped interface
	// are now visible to the system register interface
	dsb	st

	// Save all interesting registers
	mrs_s	x5, ICH_VMCR_EL2
	mrs_s	x6, ICH_MISR_EL2
	mrs_s	x7, ICH_EISR_EL2
	mrs_s	x8, ICH_ELSR_EL2

	str	w5, [x3, #VGIC_V3_CPU_VMCR]
	str	w6, [x3, #VGIC_V3_CPU_MISR]
	str	w7, [x3, #VGIC_V3_CPU_EISR]
	str	w8, [x3, #VGIC_V3_CPU_ELRSR]

	msr_s	ICH_HCR_EL2, xzr

	mrs_s	x21, ICH_VTR_EL2
	mvn	w22, w21
	ubfiz	w23, w22, 2, 4	// w23 = (15 - ListRegs) * 4

	adr	x24, 1f
	add	x24, x24, x23
	br	x24

1:
	mrs_s	x20, ICH_LR15_EL2
	mrs_s	x19, ICH_LR14_EL2
	mrs_s	x18, ICH_LR13_EL2
	mrs_s	x17, ICH_LR12_EL2
	mrs_s	x16, ICH_LR11_EL2
	mrs_s	x15, ICH_LR10_EL2
	mrs_s	x14, ICH_LR9_EL2
	mrs_s	x13, ICH_LR8_EL2
	mrs_s	x12, ICH_LR7_EL2
	mrs_s	x11, ICH_LR6_EL2
	mrs_s	x10, ICH_LR5_EL2
	mrs_s	x9, ICH_LR4_EL2
	mrs_s	x8, ICH_LR3_EL2
	mrs_s	x7, ICH_LR2_EL2
	mrs_s	x6, ICH_LR1_EL2
	mrs_s	x5, ICH_LR0_EL2

	adr	x24, 1f
	add	x24, x24, x23
	br	x24

1:
	str	x20, [x3, #LR_OFFSET(15)]
	str	x19, [x3, #LR_OFFSET(14)]
	str	x18, [x3, #LR_OFFSET(13)]
	str	x17, [x3, #LR_OFFSET(12)]
	str	x16, [x3, #LR_OFFSET(11)]
	str	x15, [x3, #LR_OFFSET(10)]
	str	x14, [x3, #LR_OFFSET(9)]
	str	x13, [x3, #LR_OFFSET(8)]
	str	x12, [x3, #LR_OFFSET(7)]
	str	x11, [x3, #LR_OFFSET(6)]
	str	x10, [x3, #LR_OFFSET(5)]
	str	x9, [x3, #LR_OFFSET(4)]
	str	x8, [x3, #LR_OFFSET(3)]
	str	x7, [x3, #LR_OFFSET(2)]
	str	x6, [x3, #LR_OFFSET(1)]
	str	x5, [x3, #LR_OFFSET(0)]

	tbnz	w21, #29, 6f	// 6 bits
	tbz	w21, #30, 5f	// 5 bits
				// 7 bits
	mrs_s	x20, ICH_AP0R3_EL2
	str	w20, [x3, #(VGIC_V3_CPU_AP0R + 3*4)]
	mrs_s	x19, ICH_AP0R2_EL2
	str	w19, [x3, #(VGIC_V3_CPU_AP0R + 2*4)]
6:	mrs_s	x18, ICH_AP0R1_EL2
	str	w18, [x3, #(VGIC_V3_CPU_AP0R + 1*4)]
5:	mrs_s	x17, ICH_AP0R0_EL2
	str	w17, [x3, #VGIC_V3_CPU_AP0R]

	tbnz	w21, #29, 6f	// 6 bits
	tbz	w21, #30, 5f	// 5 bits
				// 7 bits
	mrs_s	x20, ICH_AP1R3_EL2
	str	w20, [x3, #(VGIC_V3_CPU_AP1R + 3*4)]
	mrs_s	x19, ICH_AP1R2_EL2
	str	w19, [x3, #(VGIC_V3_CPU_AP1R + 2*4)]
6:	mrs_s	x18, ICH_AP1R1_EL2
	str	w18, [x3, #(VGIC_V3_CPU_AP1R + 1*4)]
5:	mrs_s	x17, ICH_AP1R0_EL2
	str	w17, [x3, #VGIC_V3_CPU_AP1R]

	// Restore SRE_EL1 access and re-enable SRE at EL1.
	mrs_s	x5, ICC_SRE_EL2
	orr	x5, x5, #ICC_SRE_EL2_ENABLE
	msr_s	ICC_SRE_EL2, x5
	isb
	mov	x5, #1
	msr_s	ICC_SRE_EL1, x5
.endm

/*
 * Restore the VGIC CPU state from memory
 * x0: Register pointing to VCPU struct
 */
.macro	restore_vgic_v3_state
	// Compute the address of struct vgic_cpu
	add	x3, x0, #VCPU_VGIC_CPU

	// Restore all interesting registers
	ldr	w4, [x3, #VGIC_V3_CPU_HCR]
	ldr	w5, [x3, #VGIC_V3_CPU_VMCR]
	ldr	w25, [x3, #VGIC_V3_CPU_SRE]

	msr_s	ICC_SRE_EL1, x25

	// make sure SRE is valid before writing the other registers
	isb

	msr_s	ICH_HCR_EL2, x4
	msr_s	ICH_VMCR_EL2, x5

	mrs_s	x21, ICH_VTR_EL2

	tbnz	w21, #29, 6f	// 6 bits
	tbz	w21, #30, 5f	// 5 bits
				// 7 bits
	ldr	w20, [x3, #(VGIC_V3_CPU_AP1R + 3*4)]
	msr_s	ICH_AP1R3_EL2, x20
	ldr	w19, [x3, #(VGIC_V3_CPU_AP1R + 2*4)]
	msr_s	ICH_AP1R2_EL2, x19
6:	ldr	w18, [x3, #(VGIC_V3_CPU_AP1R + 1*4)]
	msr_s	ICH_AP1R1_EL2, x18
5:	ldr	w17, [x3, #VGIC_V3_CPU_AP1R]
	msr_s	ICH_AP1R0_EL2, x17

	tbnz	w21, #29, 6f	// 6 bits
	tbz	w21, #30, 5f	// 5 bits
				// 7 bits
	ldr	w20, [x3, #(VGIC_V3_CPU_AP0R + 3*4)]
	msr_s	ICH_AP0R3_EL2, x20
	ldr	w19, [x3, #(VGIC_V3_CPU_AP0R + 2*4)]
	msr_s	ICH_AP0R2_EL2, x19
6:	ldr	w18, [x3, #(VGIC_V3_CPU_AP0R + 1*4)]
	msr_s	ICH_AP0R1_EL2, x18
5:	ldr	w17, [x3, #VGIC_V3_CPU_AP0R]
	msr_s	ICH_AP0R0_EL2, x17

	and	w22, w21, #0xf
	mvn	w22, w21
	ubfiz	w23, w22, 2, 4	// w23 = (15 - ListRegs) * 4

	adr	x24, 1f
	add	x24, x24, x23
	br	x24

1:
	ldr	x20, [x3, #LR_OFFSET(15)]
	ldr	x19, [x3, #LR_OFFSET(14)]
	ldr	x18, [x3, #LR_OFFSET(13)]
	ldr	x17, [x3, #LR_OFFSET(12)]
	ldr	x16, [x3, #LR_OFFSET(11)]
	ldr	x15, [x3, #LR_OFFSET(10)]
	ldr	x14, [x3, #LR_OFFSET(9)]
	ldr	x13, [x3, #LR_OFFSET(8)]
	ldr	x12, [x3, #LR_OFFSET(7)]
	ldr	x11, [x3, #LR_OFFSET(6)]
	ldr	x10, [x3, #LR_OFFSET(5)]
	ldr	x9, [x3, #LR_OFFSET(4)]
	ldr	x8, [x3, #LR_OFFSET(3)]
	ldr	x7, [x3, #LR_OFFSET(2)]
	ldr	x6, [x3, #LR_OFFSET(1)]
	ldr	x5, [x3, #LR_OFFSET(0)]

	adr	x24, 1f
	add	x24, x24, x23
	br	x24

1:
	msr_s	ICH_LR15_EL2, x20
	msr_s	ICH_LR14_EL2, x19
	msr_s	ICH_LR13_EL2, x18
	msr_s	ICH_LR12_EL2, x17
	msr_s	ICH_LR11_EL2, x16
	msr_s	ICH_LR10_EL2, x15
	msr_s	ICH_LR9_EL2,  x14
	msr_s	ICH_LR8_EL2,  x13
	msr_s	ICH_LR7_EL2,  x12
	msr_s	ICH_LR6_EL2,  x11
	msr_s	ICH_LR5_EL2,  x10
	msr_s	ICH_LR4_EL2,   x9
	msr_s	ICH_LR3_EL2,   x8
	msr_s	ICH_LR2_EL2,   x7
	msr_s	ICH_LR1_EL2,   x6
	msr_s	ICH_LR0_EL2,   x5

	// Ensure that the above will have reached the
	// (re)distributors. This ensure the guest will read
	// the correct values from the memory-mapped interface.
	isb
	dsb	sy

	// Prevent the guest from touching the GIC system registers
	// if SRE isn't enabled for GICv3 emulation
	cbnz	x25, 1f
	mrs_s	x5, ICC_SRE_EL2
	and	x5, x5, #~ICC_SRE_EL2_ENABLE
	msr_s	ICC_SRE_EL2, x5
1:
.endm

ENTRY(__save_vgic_v3_state)
	save_vgic_v3_state
	ret
ENDPROC(__save_vgic_v3_state)

ENTRY(__restore_vgic_v3_state)
	restore_vgic_v3_state
	ret
ENDPROC(__restore_vgic_v3_state)

ENTRY(__vgic_v3_get_ich_vtr_el2)
	mrs_s	x0, ICH_VTR_EL2
	ret
ENDPROC(__vgic_v3_get_ich_vtr_el2)

	.popsection
