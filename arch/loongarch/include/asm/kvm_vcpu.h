/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2023 Loongson Technology Corporation Limited
 */

#ifndef __ASM_LOONGARCH_KVM_VCPU_H__
#define __ASM_LOONGARCH_KVM_VCPU_H__

#include <linux/kvm_host.h>
#include <asm/loongarch.h>

/* Controlled by 0x5 guest estat */
#define CPU_SIP0			BIT(INT_SWI0)
#define CPU_SIP1			BIT(INT_SWI1)
#define CPU_HWI0			BIT(INT_HWI0)
#define CPU_HWI1			BIT(INT_HWI1)
#define CPU_HWI2			BIT(INT_HWI2)
#define CPU_HWI3			BIT(INT_HWI3)
#define CPU_HWI4			BIT(INT_HWI4)
#define CPU_HWI5			BIT(INT_HWI5)
#define CPU_HWI6			BIT(INT_HWI6)
#define CPU_HWI7			BIT(INT_HWI7)
#define CPU_PMU				BIT(INT_PCOV)
#define CPU_TIMER			BIT(INT_TI)
#define CPU_IPI				BIT(INT_IPI)
#define CPU_AVEC			BIT(INT_AVEC)
#define KVM_ESTAT_INTI_MASK		(CPU_SIP0 | CPU_SIP1 | CPU_PMU | CPU_TIMER \
						| CPU_IPI | CPU_AVEC)
#define KVM_ESTAT_EXTI_MASK		(CPU_HWI0 | CPU_HWI1 | CPU_HWI2 | CPU_HWI3 \
						| CPU_HWI4 | CPU_HWI5 | CPU_HWI6 | CPU_HWI7)

/* Controlled by 0x52 guest exception VIP aligned to estat bit 5~12 */
#define VIP_DELTA			(INT_HWI0 - CSR_GINTC_VIP_SHIFT)
#define CPU_IP0				BIT(INT_HWI0 - VIP_DELTA)
#define CPU_IP1				BIT(INT_HWI1 - VIP_DELTA)
#define CPU_IP2				BIT(INT_HWI2 - VIP_DELTA)
#define CPU_IP3				BIT(INT_HWI3 - VIP_DELTA)
#define CPU_IP4				BIT(INT_HWI4 - VIP_DELTA)
#define CPU_IP5				BIT(INT_HWI5 - VIP_DELTA)
#define CPU_IP6				BIT(INT_HWI6 - VIP_DELTA)
#define CPU_IP7				BIT(INT_HWI7 - VIP_DELTA)
#define KVM_GINTC_IRQ_MASK		(CPU_IP0 | CPU_IP1 | CPU_IP2 | CPU_IP3 \
						| CPU_IP4 | CPU_IP5 | CPU_IP6 | CPU_IP7)

#define MNSEC_PER_SEC			(NSEC_PER_SEC >> 20)

/* KVM_IRQ_LINE irq field index values */
#define KVM_LOONGSON_IRQ_TYPE_SHIFT	24
#define KVM_LOONGSON_IRQ_TYPE_MASK	0xff
#define KVM_LOONGSON_IRQ_VCPU_SHIFT	16
#define KVM_LOONGSON_IRQ_VCPU_MASK	0xff
#define KVM_LOONGSON_IRQ_NUM_SHIFT	0
#define KVM_LOONGSON_IRQ_NUM_MASK	0xffff

typedef union loongarch_instruction  larch_inst;
typedef int (*exit_handle_fn)(struct kvm_vcpu *, int);

int  kvm_emu_mmio_read(struct kvm_vcpu *vcpu, larch_inst inst);
int  kvm_emu_mmio_write(struct kvm_vcpu *vcpu, larch_inst inst);
int  kvm_complete_mmio_read(struct kvm_vcpu *vcpu, struct kvm_run *run);
int  kvm_complete_iocsr_read(struct kvm_vcpu *vcpu, struct kvm_run *run);
int  kvm_complete_user_service(struct kvm_vcpu *vcpu, struct kvm_run *run);
int  kvm_emu_idle(struct kvm_vcpu *vcpu);
int  kvm_pending_timer(struct kvm_vcpu *vcpu);
int  kvm_handle_fault(struct kvm_vcpu *vcpu, int fault);
void kvm_deliver_intr(struct kvm_vcpu *vcpu);
void kvm_deliver_exception(struct kvm_vcpu *vcpu);

void kvm_own_fpu(struct kvm_vcpu *vcpu);
void kvm_lose_fpu(struct kvm_vcpu *vcpu);
void kvm_save_fpu(struct loongarch_fpu *fpu);
void kvm_restore_fpu(struct loongarch_fpu *fpu);
void kvm_restore_fcsr(struct loongarch_fpu *fpu);

#ifdef CONFIG_CPU_HAS_LSX
int kvm_own_lsx(struct kvm_vcpu *vcpu);
void kvm_save_lsx(struct loongarch_fpu *fpu);
void kvm_restore_lsx(struct loongarch_fpu *fpu);
#else
static inline int kvm_own_lsx(struct kvm_vcpu *vcpu) { return -EINVAL; }
static inline void kvm_save_lsx(struct loongarch_fpu *fpu) { }
static inline void kvm_restore_lsx(struct loongarch_fpu *fpu) { }
#endif

#ifdef CONFIG_CPU_HAS_LASX
int kvm_own_lasx(struct kvm_vcpu *vcpu);
void kvm_save_lasx(struct loongarch_fpu *fpu);
void kvm_restore_lasx(struct loongarch_fpu *fpu);
#else
static inline int kvm_own_lasx(struct kvm_vcpu *vcpu) { return -EINVAL; }
static inline void kvm_save_lasx(struct loongarch_fpu *fpu) { }
static inline void kvm_restore_lasx(struct loongarch_fpu *fpu) { }
#endif

#ifdef CONFIG_CPU_HAS_LBT
int kvm_own_lbt(struct kvm_vcpu *vcpu);
#else
static inline int kvm_own_lbt(struct kvm_vcpu *vcpu) { return -EINVAL; }
#endif

void kvm_init_timer(struct kvm_vcpu *vcpu, unsigned long hz);
void kvm_save_timer(struct kvm_vcpu *vcpu);
void kvm_restore_timer(struct kvm_vcpu *vcpu);

int kvm_vcpu_ioctl_interrupt(struct kvm_vcpu *vcpu, struct kvm_interrupt *irq);
struct kvm_vcpu *kvm_get_vcpu_by_cpuid(struct kvm *kvm, int cpuid);

/*
 * Loongarch KVM guest interrupt handling
 */
static inline void kvm_queue_irq(struct kvm_vcpu *vcpu, unsigned int irq)
{
	set_bit(irq, &vcpu->arch.irq_pending);
	clear_bit(irq, &vcpu->arch.irq_clear);
}

static inline void kvm_dequeue_irq(struct kvm_vcpu *vcpu, unsigned int irq)
{
	clear_bit(irq, &vcpu->arch.irq_pending);
	set_bit(irq, &vcpu->arch.irq_clear);
}

static inline int kvm_queue_exception(struct kvm_vcpu *vcpu,
			unsigned int code, unsigned int subcode)
{
	/* only one exception can be injected */
	if (!vcpu->arch.exception_pending) {
		set_bit(code, &vcpu->arch.exception_pending);
		vcpu->arch.esubcode = subcode;
		return 0;
	} else
		return -1;
}

static inline unsigned long kvm_read_reg(struct kvm_vcpu *vcpu, int num)
{
	return vcpu->arch.gprs[num];
}

static inline void kvm_write_reg(struct kvm_vcpu *vcpu, int num, unsigned long val)
{
	vcpu->arch.gprs[num] = val;
}

static inline bool kvm_pvtime_supported(void)
{
	return !!sched_info_on();
}

static inline bool kvm_guest_has_pv_feature(struct kvm_vcpu *vcpu, unsigned int feature)
{
	return vcpu->kvm->arch.pv_features & BIT(feature);
}

#endif /* __ASM_LOONGARCH_KVM_VCPU_H__ */
