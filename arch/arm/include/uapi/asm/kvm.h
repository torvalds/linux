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

#ifndef __ARM_KVM_H__
#define __ARM_KVM_H__

#include <linux/types.h>
#include <asm/ptrace.h>

#define __KVM_HAVE_GUEST_DEBUG
#define __KVM_HAVE_IRQ_LINE

#define KVM_REG_SIZE(id)						\
	(1U << (((id) & KVM_REG_SIZE_MASK) >> KVM_REG_SIZE_SHIFT))

/* Valid for svc_regs, abt_regs, und_regs, irq_regs in struct kvm_regs */
#define KVM_ARM_SVC_sp		svc_regs[0]
#define KVM_ARM_SVC_lr		svc_regs[1]
#define KVM_ARM_SVC_spsr	svc_regs[2]
#define KVM_ARM_ABT_sp		abt_regs[0]
#define KVM_ARM_ABT_lr		abt_regs[1]
#define KVM_ARM_ABT_spsr	abt_regs[2]
#define KVM_ARM_UND_sp		und_regs[0]
#define KVM_ARM_UND_lr		und_regs[1]
#define KVM_ARM_UND_spsr	und_regs[2]
#define KVM_ARM_IRQ_sp		irq_regs[0]
#define KVM_ARM_IRQ_lr		irq_regs[1]
#define KVM_ARM_IRQ_spsr	irq_regs[2]

/* Valid only for fiq_regs in struct kvm_regs */
#define KVM_ARM_FIQ_r8		fiq_regs[0]
#define KVM_ARM_FIQ_r9		fiq_regs[1]
#define KVM_ARM_FIQ_r10		fiq_regs[2]
#define KVM_ARM_FIQ_fp		fiq_regs[3]
#define KVM_ARM_FIQ_ip		fiq_regs[4]
#define KVM_ARM_FIQ_sp		fiq_regs[5]
#define KVM_ARM_FIQ_lr		fiq_regs[6]
#define KVM_ARM_FIQ_spsr	fiq_regs[7]

struct kvm_regs {
	struct pt_regs usr_regs;	/* R0_usr - R14_usr, PC, CPSR */
	unsigned long svc_regs[3];	/* SP_svc, LR_svc, SPSR_svc */
	unsigned long abt_regs[3];	/* SP_abt, LR_abt, SPSR_abt */
	unsigned long und_regs[3];	/* SP_und, LR_und, SPSR_und */
	unsigned long irq_regs[3];	/* SP_irq, LR_irq, SPSR_irq */
	unsigned long fiq_regs[8];	/* R8_fiq - R14_fiq, SPSR_fiq */
};

/* Supported Processor Types */
#define KVM_ARM_TARGET_CORTEX_A15	0
#define KVM_ARM_TARGET_CORTEX_A7	1
#define KVM_ARM_NUM_TARGETS		2

/* KVM_ARM_SET_DEVICE_ADDR ioctl id encoding */
#define KVM_ARM_DEVICE_TYPE_SHIFT	0
#define KVM_ARM_DEVICE_TYPE_MASK	(0xffff << KVM_ARM_DEVICE_TYPE_SHIFT)
#define KVM_ARM_DEVICE_ID_SHIFT		16
#define KVM_ARM_DEVICE_ID_MASK		(0xffff << KVM_ARM_DEVICE_ID_SHIFT)

/* Supported device IDs */
#define KVM_ARM_DEVICE_VGIC_V2		0

/* Supported VGIC address types  */
#define KVM_VGIC_V2_ADDR_TYPE_DIST	0
#define KVM_VGIC_V2_ADDR_TYPE_CPU	1

#define KVM_VGIC_V2_DIST_SIZE		0x1000
#define KVM_VGIC_V2_CPU_SIZE		0x2000

#define KVM_ARM_VCPU_POWER_OFF		0 /* CPU is started in OFF state */

struct kvm_vcpu_init {
	__u32 target;
	__u32 features[7];
};

struct kvm_sregs {
};

struct kvm_fpu {
};

struct kvm_guest_debug_arch {
};

struct kvm_debug_exit_arch {
};

struct kvm_sync_regs {
};

struct kvm_arch_memory_slot {
};

/* If you need to interpret the index values, here is the key: */
#define KVM_REG_ARM_COPROC_MASK		0x000000000FFF0000
#define KVM_REG_ARM_COPROC_SHIFT	16
#define KVM_REG_ARM_32_OPC2_MASK	0x0000000000000007
#define KVM_REG_ARM_32_OPC2_SHIFT	0
#define KVM_REG_ARM_OPC1_MASK		0x0000000000000078
#define KVM_REG_ARM_OPC1_SHIFT		3
#define KVM_REG_ARM_CRM_MASK		0x0000000000000780
#define KVM_REG_ARM_CRM_SHIFT		7
#define KVM_REG_ARM_32_CRN_MASK		0x0000000000007800
#define KVM_REG_ARM_32_CRN_SHIFT	11

#define ARM_CP15_REG_SHIFT_MASK(x,n) \
	(((x) << KVM_REG_ARM_ ## n ## _SHIFT) & KVM_REG_ARM_ ## n ## _MASK)

#define __ARM_CP15_REG(op1,crn,crm,op2) \
	(KVM_REG_ARM | (15 << KVM_REG_ARM_COPROC_SHIFT) | \
	ARM_CP15_REG_SHIFT_MASK(op1, OPC1) | \
	ARM_CP15_REG_SHIFT_MASK(crn, 32_CRN) | \
	ARM_CP15_REG_SHIFT_MASK(crm, CRM) | \
	ARM_CP15_REG_SHIFT_MASK(op2, 32_OPC2))

#define ARM_CP15_REG32(...) (__ARM_CP15_REG(__VA_ARGS__) | KVM_REG_SIZE_U32)

#define __ARM_CP15_REG64(op1,crm) \
	(__ARM_CP15_REG(op1, 0, crm, 0) | KVM_REG_SIZE_U64)
#define ARM_CP15_REG64(...) __ARM_CP15_REG64(__VA_ARGS__)

#define KVM_REG_ARM_TIMER_CTL		ARM_CP15_REG32(0, 14, 3, 1)
#define KVM_REG_ARM_TIMER_CNT		ARM_CP15_REG64(1, 14) 
#define KVM_REG_ARM_TIMER_CVAL		ARM_CP15_REG64(3, 14) 

/* Normal registers are mapped as coprocessor 16. */
#define KVM_REG_ARM_CORE		(0x0010 << KVM_REG_ARM_COPROC_SHIFT)
#define KVM_REG_ARM_CORE_REG(name)	(offsetof(struct kvm_regs, name) / 4)

/* Some registers need more space to represent values. */
#define KVM_REG_ARM_DEMUX		(0x0011 << KVM_REG_ARM_COPROC_SHIFT)
#define KVM_REG_ARM_DEMUX_ID_MASK	0x000000000000FF00
#define KVM_REG_ARM_DEMUX_ID_SHIFT	8
#define KVM_REG_ARM_DEMUX_ID_CCSIDR	(0x00 << KVM_REG_ARM_DEMUX_ID_SHIFT)
#define KVM_REG_ARM_DEMUX_VAL_MASK	0x00000000000000FF
#define KVM_REG_ARM_DEMUX_VAL_SHIFT	0

/* VFP registers: we could overload CP10 like ARM does, but that's ugly. */
#define KVM_REG_ARM_VFP			(0x0012 << KVM_REG_ARM_COPROC_SHIFT)
#define KVM_REG_ARM_VFP_MASK		0x000000000000FFFF
#define KVM_REG_ARM_VFP_BASE_REG	0x0
#define KVM_REG_ARM_VFP_FPSID		0x1000
#define KVM_REG_ARM_VFP_FPSCR		0x1001
#define KVM_REG_ARM_VFP_MVFR1		0x1006
#define KVM_REG_ARM_VFP_MVFR0		0x1007
#define KVM_REG_ARM_VFP_FPEXC		0x1008
#define KVM_REG_ARM_VFP_FPINST		0x1009
#define KVM_REG_ARM_VFP_FPINST2		0x100A

/* Device Control API: ARM VGIC */
#define KVM_DEV_ARM_VGIC_GRP_ADDR	0

/* KVM_IRQ_LINE irq field index values */
#define KVM_ARM_IRQ_TYPE_SHIFT		24
#define KVM_ARM_IRQ_TYPE_MASK		0xff
#define KVM_ARM_IRQ_VCPU_SHIFT		16
#define KVM_ARM_IRQ_VCPU_MASK		0xff
#define KVM_ARM_IRQ_NUM_SHIFT		0
#define KVM_ARM_IRQ_NUM_MASK		0xffff

/* irq_type field */
#define KVM_ARM_IRQ_TYPE_CPU		0
#define KVM_ARM_IRQ_TYPE_SPI		1
#define KVM_ARM_IRQ_TYPE_PPI		2

/* out-of-kernel GIC cpu interrupt injection irq_number field */
#define KVM_ARM_IRQ_CPU_IRQ		0
#define KVM_ARM_IRQ_CPU_FIQ		1

/* Highest supported SPI, from VGIC_NR_IRQS */
#define KVM_ARM_IRQ_GIC_MAX		127

/* PSCI interface */
#define KVM_PSCI_FN_BASE		0x95c1ba5e
#define KVM_PSCI_FN(n)			(KVM_PSCI_FN_BASE + (n))

#define KVM_PSCI_FN_CPU_SUSPEND		KVM_PSCI_FN(0)
#define KVM_PSCI_FN_CPU_OFF		KVM_PSCI_FN(1)
#define KVM_PSCI_FN_CPU_ON		KVM_PSCI_FN(2)
#define KVM_PSCI_FN_MIGRATE		KVM_PSCI_FN(3)

#define KVM_PSCI_RET_SUCCESS		0
#define KVM_PSCI_RET_NI			((unsigned long)-1)
#define KVM_PSCI_RET_INVAL		((unsigned long)-2)
#define KVM_PSCI_RET_DENIED		((unsigned long)-3)

#endif /* __ARM_KVM_H__ */
