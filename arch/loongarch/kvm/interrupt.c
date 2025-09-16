// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020-2023 Loongson Technology Corporation Limited
 */

#include <linux/err.h>
#include <linux/errno.h>
#include <asm/kvm_csr.h>
#include <asm/kvm_vcpu.h>

static unsigned int priority_to_irq[EXCCODE_INT_NUM] = {
	[INT_TI]	= CPU_TIMER,
	[INT_IPI]	= CPU_IPI,
	[INT_SWI0]	= CPU_SIP0,
	[INT_SWI1]	= CPU_SIP1,
	[INT_HWI0]	= CPU_IP0,
	[INT_HWI1]	= CPU_IP1,
	[INT_HWI2]	= CPU_IP2,
	[INT_HWI3]	= CPU_IP3,
	[INT_HWI4]	= CPU_IP4,
	[INT_HWI5]	= CPU_IP5,
	[INT_HWI6]	= CPU_IP6,
	[INT_HWI7]	= CPU_IP7,
};

static int kvm_irq_deliver(struct kvm_vcpu *vcpu, unsigned int priority)
{
	unsigned int irq = 0;

	clear_bit(priority, &vcpu->arch.irq_pending);
	if (priority < EXCCODE_INT_NUM)
		irq = priority_to_irq[priority];

	switch (priority) {
	case INT_TI:
	case INT_IPI:
	case INT_SWI0:
	case INT_SWI1:
		set_gcsr_estat(irq);
		break;

	case INT_HWI0 ... INT_HWI7:
		set_csr_gintc(irq);
		break;

	default:
		break;
	}

	return 1;
}

static int kvm_irq_clear(struct kvm_vcpu *vcpu, unsigned int priority)
{
	unsigned int irq = 0;

	clear_bit(priority, &vcpu->arch.irq_clear);
	if (priority < EXCCODE_INT_NUM)
		irq = priority_to_irq[priority];

	switch (priority) {
	case INT_TI:
	case INT_IPI:
	case INT_SWI0:
	case INT_SWI1:
		clear_gcsr_estat(irq);
		break;

	case INT_HWI0 ... INT_HWI7:
		clear_csr_gintc(irq);
		break;

	default:
		break;
	}

	return 1;
}

void kvm_deliver_intr(struct kvm_vcpu *vcpu)
{
	unsigned int priority;
	unsigned long *pending = &vcpu->arch.irq_pending;
	unsigned long *pending_clr = &vcpu->arch.irq_clear;

	for_each_set_bit(priority, pending_clr, INT_IPI + 1)
		kvm_irq_clear(vcpu, priority);

	for_each_set_bit(priority, pending, INT_IPI + 1)
		kvm_irq_deliver(vcpu, priority);
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
