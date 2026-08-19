// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2023 Loongson Technology Corporation Limited
 */

#include <linux/err.h>
#include <linux/errno.h>
#include <asm/kvm_csr.h>
#include <asm/kvm_vcpu.h>
#include <asm/kvm_dmsintc.h>

static void kvm_irq_deliver(struct kvm_vcpu *vcpu, unsigned long mask)
{
	unsigned long irq;
	unsigned long old, new;

	if (mask & CPU_AVEC)
		dmsintc_inject_irq(vcpu);

	irq = mask & KVM_ESTAT_INTI_MASK;
	if (irq) {
		old = kvm_read_hw_gcsr(LOONGARCH_CSR_TVAL);
		set_gcsr_estat(irq);
		new = kvm_read_hw_gcsr(LOONGARCH_CSR_TVAL);

		/* Inject TI if TVAL inverted */
		if (new > old)
			set_gcsr_estat(CPU_TIMER);
	}

	irq = (mask >> VIP_DELTA) & KVM_GINTC_IRQ_MASK;
	if (irq)
		set_csr_gintc(irq);
}

static void kvm_irq_clear(struct kvm_vcpu *vcpu, unsigned long mask)
{
	unsigned long irq;
	unsigned long old, new;

	irq = mask & KVM_ESTAT_INTI_MASK;
	if (irq) {
		old = kvm_read_hw_gcsr(LOONGARCH_CSR_TVAL);
		clear_gcsr_estat(irq);
		new = kvm_read_hw_gcsr(LOONGARCH_CSR_TVAL);

		/* Inject TI if TVAL inverted */
		if (new > old)
			set_gcsr_estat(CPU_TIMER);
	}

	irq = (mask >> VIP_DELTA) & KVM_GINTC_IRQ_MASK;
	if (irq)
		clear_csr_gintc(irq);
}

void kvm_deliver_intr(struct kvm_vcpu *vcpu)
{
	unsigned long mask;

	mask = READ_ONCE(vcpu->arch.irq_clear);
	if (mask) {
		mask = xchg_relaxed(&vcpu->arch.irq_clear, 0);
		kvm_irq_clear(vcpu, mask);
	}

	mask = READ_ONCE(vcpu->arch.irq_pending);
	if (mask) {
		mask = xchg_relaxed(&vcpu->arch.irq_pending, 0);
		kvm_irq_deliver(vcpu, mask);
	}
}

int kvm_pending_timer(struct kvm_vcpu *vcpu)
{
	return test_bit(INT_TI, &vcpu->arch.irq_pending);
}

/*
 * Only support illegal instruction or illegal Address Error exception,
 * Other exceptions are injected by hardware in kvm mode
 */
static void _kvm_deliver_exception(struct kvm_vcpu *vcpu,
				unsigned int code, unsigned int subcode)
{
	unsigned long val, vec_size;

	/*
	 * BADV is added for EXCCODE_ADE exception
	 *  Use PC register (GVA address) if it is instruction exeception
	 *  Else use BADV from host side (GPA address) for data exeception
	 */
	if (code == EXCCODE_ADE) {
		if (subcode == EXSUBCODE_ADEF)
			val = vcpu->arch.pc;
		else
			val = vcpu->arch.badv;
		kvm_write_hw_gcsr(LOONGARCH_CSR_BADV, val);
	}

	/* Set exception instruction */
	kvm_write_hw_gcsr(LOONGARCH_CSR_BADI, vcpu->arch.badi);

	/*
	 * Save CRMD in PRMD
	 * Set IRQ disabled and PLV0 with CRMD
	 */
	val = kvm_read_hw_gcsr(LOONGARCH_CSR_CRMD);
	kvm_write_hw_gcsr(LOONGARCH_CSR_PRMD, val);
	val = val & ~(CSR_CRMD_PLV | CSR_CRMD_IE);
	kvm_write_hw_gcsr(LOONGARCH_CSR_CRMD, val);

	/* Set exception PC address */
	kvm_write_hw_gcsr(LOONGARCH_CSR_ERA, vcpu->arch.pc);

	/*
	 * Set exception code
	 * Exception and interrupt can be inject at the same time
	 * Hardware will handle exception first and then extern interrupt
	 * Exception code is Ecode in ESTAT[16:21]
	 * Interrupt code in ESTAT[0:12]
	 */
	val = kvm_read_hw_gcsr(LOONGARCH_CSR_ESTAT);
	val = (val & ~CSR_ESTAT_EXC) | code;
	kvm_write_hw_gcsr(LOONGARCH_CSR_ESTAT, val);

	/* Calculate expcetion entry address */
	val = kvm_read_hw_gcsr(LOONGARCH_CSR_ECFG);
	vec_size = (val & CSR_ECFG_VS) >> CSR_ECFG_VS_SHIFT;
	if (vec_size)
		vec_size = (1 << vec_size) * 4;
	val =  kvm_read_hw_gcsr(LOONGARCH_CSR_EENTRY);
	vcpu->arch.pc = val + code * vec_size;
}

void kvm_deliver_exception(struct kvm_vcpu *vcpu)
{
	unsigned int code;
	unsigned long *pending = &vcpu->arch.exception_pending;

	if (*pending) {
		code = __ffs(*pending);
		_kvm_deliver_exception(vcpu, code, vcpu->arch.esubcode);
		*pending = 0;
		vcpu->arch.esubcode = 0;
	}
}
