// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Western Digital Corporation or its affiliates.
 * Copyright (C) 2022 Ventana Micro Systems Inc.
 *
 * Authors:
 *	Anup Patel <apatel@ventanamicro.com>
 */

#include <linux/kernel.h>
#include <linux/kvm_host.h>
#include <asm/hwcap.h>

DEFINE_STATIC_KEY_FALSE(kvm_riscv_aia_available);

static void aia_set_hvictl(bool ext_irq_pending)
{
	unsigned long hvictl;

	/*
	 * HVICTL.IID == 9 and HVICTL.IPRIO == 0 represents
	 * no interrupt in HVICTL.
	 */

	hvictl = (IRQ_S_EXT << HVICTL_IID_SHIFT) & HVICTL_IID;
	hvictl |= ext_irq_pending;
	csr_write(CSR_HVICTL, hvictl);
}

#ifdef CONFIG_32BIT
void kvm_riscv_vcpu_aia_flush_interrupts(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_aia_csr *csr = &vcpu->arch.aia_context.guest_csr;
	unsigned long mask, val;

	if (!kvm_riscv_aia_available())
		return;

	if (READ_ONCE(vcpu->arch.irqs_pending_mask[1])) {
		mask = xchg_acquire(&vcpu->arch.irqs_pending_mask[1], 0);
		val = READ_ONCE(vcpu->arch.irqs_pending[1]) & mask;

		csr->hviph &= ~mask;
		csr->hviph |= val;
	}
}

void kvm_riscv_vcpu_aia_sync_interrupts(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_aia_csr *csr = &vcpu->arch.aia_context.guest_csr;

	if (kvm_riscv_aia_available())
		csr->vsieh = csr_read(CSR_VSIEH);
}
#endif

bool kvm_riscv_vcpu_aia_has_interrupts(struct kvm_vcpu *vcpu, u64 mask)
{
	unsigned long seip;

	if (!kvm_riscv_aia_available())
		return false;

#ifdef CONFIG_32BIT
	if (READ_ONCE(vcpu->arch.irqs_pending[1]) &
	    (vcpu->arch.aia_context.guest_csr.vsieh & upper_32_bits(mask)))
		return true;
#endif

	seip = vcpu->arch.guest_csr.vsie;
	seip &= (unsigned long)mask;
	seip &= BIT(IRQ_S_EXT);

	if (!kvm_riscv_aia_initialized(vcpu->kvm) || !seip)
		return false;

	return false;
}

void kvm_riscv_vcpu_aia_update_hvip(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_csr *csr = &vcpu->arch.guest_csr;

	if (!kvm_riscv_aia_available())
		return;

#ifdef CONFIG_32BIT
	csr_write(CSR_HVIPH, vcpu->arch.aia_context.guest_csr.hviph);
#endif
	aia_set_hvictl(!!(csr->hvip & BIT(IRQ_VS_EXT)));
}

void kvm_riscv_vcpu_aia_load(struct kvm_vcpu *vcpu, int cpu)
{
	struct kvm_vcpu_aia_csr *csr = &vcpu->arch.aia_context.guest_csr;

	if (!kvm_riscv_aia_available())
		return;

	csr_write(CSR_VSISELECT, csr->vsiselect);
	csr_write(CSR_HVIPRIO1, csr->hviprio1);
	csr_write(CSR_HVIPRIO2, csr->hviprio2);
#ifdef CONFIG_32BIT
	csr_write(CSR_VSIEH, csr->vsieh);
	csr_write(CSR_HVIPH, csr->hviph);
	csr_write(CSR_HVIPRIO1H, csr->hviprio1h);
	csr_write(CSR_HVIPRIO2H, csr->hviprio2h);
#endif
}

void kvm_riscv_vcpu_aia_put(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_aia_csr *csr = &vcpu->arch.aia_context.guest_csr;

	if (!kvm_riscv_aia_available())
		return;

	csr->vsiselect = csr_read(CSR_VSISELECT);
	csr->hviprio1 = csr_read(CSR_HVIPRIO1);
	csr->hviprio2 = csr_read(CSR_HVIPRIO2);
#ifdef CONFIG_32BIT
	csr->vsieh = csr_read(CSR_VSIEH);
	csr->hviph = csr_read(CSR_HVIPH);
	csr->hviprio1h = csr_read(CSR_HVIPRIO1H);
	csr->hviprio2h = csr_read(CSR_HVIPRIO2H);
#endif
}

int kvm_riscv_vcpu_aia_get_csr(struct kvm_vcpu *vcpu,
			       unsigned long reg_num,
			       unsigned long *out_val)
{
	struct kvm_vcpu_aia_csr *csr = &vcpu->arch.aia_context.guest_csr;

	if (reg_num >= sizeof(struct kvm_riscv_aia_csr) / sizeof(unsigned long))
		return -EINVAL;

	*out_val = 0;
	if (kvm_riscv_aia_available())
		*out_val = ((unsigned long *)csr)[reg_num];

	return 0;
}

int kvm_riscv_vcpu_aia_set_csr(struct kvm_vcpu *vcpu,
			       unsigned long reg_num,
			       unsigned long val)
{
	struct kvm_vcpu_aia_csr *csr = &vcpu->arch.aia_context.guest_csr;

	if (reg_num >= sizeof(struct kvm_riscv_aia_csr) / sizeof(unsigned long))
		return -EINVAL;

	if (kvm_riscv_aia_available()) {
		((unsigned long *)csr)[reg_num] = val;

#ifdef CONFIG_32BIT
		if (reg_num == KVM_REG_RISCV_CSR_AIA_REG(siph))
			WRITE_ONCE(vcpu->arch.irqs_pending_mask[1], 0);
#endif
	}

	return 0;
}

int kvm_riscv_vcpu_aia_rmw_topei(struct kvm_vcpu *vcpu,
				 unsigned int csr_num,
				 unsigned long *val,
				 unsigned long new_val,
				 unsigned long wr_mask)
{
	/* If AIA not available then redirect trap */
	if (!kvm_riscv_aia_available())
		return KVM_INSN_ILLEGAL_TRAP;

	/* If AIA not initialized then forward to user space */
	if (!kvm_riscv_aia_initialized(vcpu->kvm))
		return KVM_INSN_EXIT_TO_USER_SPACE;

	return kvm_riscv_vcpu_aia_imsic_rmw(vcpu, KVM_RISCV_AIA_IMSIC_TOPEI,
					    val, new_val, wr_mask);
}

/*
 * External IRQ priority always read-only zero. This means default
 * priority order  is always preferred for external IRQs unless
 * HVICTL.IID == 9 and HVICTL.IPRIO != 0
 */
static int aia_irq2bitpos[] = {
0,     8,   -1,   -1,   16,   24,   -1,   -1, /* 0 - 7 */
32,   -1,   -1,   -1,   -1,   40,   48,   56, /* 8 - 15 */
64,   72,   80,   88,   96,  104,  112,  120, /* 16 - 23 */
-1,   -1,   -1,   -1,   -1,   -1,   -1,   -1, /* 24 - 31 */
-1,   -1,   -1,   -1,   -1,   -1,   -1,   -1, /* 32 - 39 */
-1,   -1,   -1,   -1,   -1,   -1,   -1,   -1, /* 40 - 47 */
-1,   -1,   -1,   -1,   -1,   -1,   -1,   -1, /* 48 - 55 */
-1,   -1,   -1,   -1,   -1,   -1,   -1,   -1, /* 56 - 63 */
};

static u8 aia_get_iprio8(struct kvm_vcpu *vcpu, unsigned int irq)
{
	unsigned long hviprio;
	int bitpos = aia_irq2bitpos[irq];

	if (bitpos < 0)
		return 0;

	switch (bitpos / BITS_PER_LONG) {
	case 0:
		hviprio = csr_read(CSR_HVIPRIO1);
		break;
	case 1:
#ifndef CONFIG_32BIT
		hviprio = csr_read(CSR_HVIPRIO2);
		break;
#else
		hviprio = csr_read(CSR_HVIPRIO1H);
		break;
	case 2:
		hviprio = csr_read(CSR_HVIPRIO2);
		break;
	case 3:
		hviprio = csr_read(CSR_HVIPRIO2H);
		break;
#endif
	default:
		return 0;
	}

	return (hviprio >> (bitpos % BITS_PER_LONG)) & TOPI_IPRIO_MASK;
}

static void aia_set_iprio8(struct kvm_vcpu *vcpu, unsigned int irq, u8 prio)
{
	unsigned long hviprio;
	int bitpos = aia_irq2bitpos[irq];

	if (bitpos < 0)
		return;

	switch (bitpos / BITS_PER_LONG) {
	case 0:
		hviprio = csr_read(CSR_HVIPRIO1);
		break;
	case 1:
#ifndef CONFIG_32BIT
		hviprio = csr_read(CSR_HVIPRIO2);
		break;
#else
		hviprio = csr_read(CSR_HVIPRIO1H);
		break;
	case 2:
		hviprio = csr_read(CSR_HVIPRIO2);
		break;
	case 3:
		hviprio = csr_read(CSR_HVIPRIO2H);
		break;
#endif
	default:
		return;
	}

	hviprio &= ~(TOPI_IPRIO_MASK << (bitpos % BITS_PER_LONG));
	hviprio |= (unsigned long)prio << (bitpos % BITS_PER_LONG);

	switch (bitpos / BITS_PER_LONG) {
	case 0:
		csr_write(CSR_HVIPRIO1, hviprio);
		break;
	case 1:
#ifndef CONFIG_32BIT
		csr_write(CSR_HVIPRIO2, hviprio);
		break;
#else
		csr_write(CSR_HVIPRIO1H, hviprio);
		break;
	case 2:
		csr_write(CSR_HVIPRIO2, hviprio);
		break;
	case 3:
		csr_write(CSR_HVIPRIO2H, hviprio);
		break;
#endif
	default:
		return;
	}
}

static int aia_rmw_iprio(struct kvm_vcpu *vcpu, unsigned int isel,
			 unsigned long *val, unsigned long new_val,
			 unsigned long wr_mask)
{
	int i, first_irq, nirqs;
	unsigned long old_val;
	u8 prio;

#ifndef CONFIG_32BIT
	if (isel & 0x1)
		return KVM_INSN_ILLEGAL_TRAP;
#endif

	nirqs = 4 * (BITS_PER_LONG / 32);
	first_irq = (isel - ISELECT_IPRIO0) * 4;

	old_val = 0;
	for (i = 0; i < nirqs; i++) {
		prio = aia_get_iprio8(vcpu, first_irq + i);
		old_val |= (unsigned long)prio << (TOPI_IPRIO_BITS * i);
	}

	if (val)
		*val = old_val;

	if (wr_mask) {
		new_val = (old_val & ~wr_mask) | (new_val & wr_mask);
		for (i = 0; i < nirqs; i++) {
			prio = (new_val >> (TOPI_IPRIO_BITS * i)) &
				TOPI_IPRIO_MASK;
			aia_set_iprio8(vcpu, first_irq + i, prio);
		}
	}

	return KVM_INSN_CONTINUE_NEXT_SEPC;
}

#define IMSIC_FIRST	0x70
#define IMSIC_LAST	0xff
int kvm_riscv_vcpu_aia_rmw_ireg(struct kvm_vcpu *vcpu, unsigned int csr_num,
				unsigned long *val, unsigned long new_val,
				unsigned long wr_mask)
{
	unsigned int isel;

	/* If AIA not available then redirect trap */
	if (!kvm_riscv_aia_available())
		return KVM_INSN_ILLEGAL_TRAP;

	/* First try to emulate in kernel space */
	isel = csr_read(CSR_VSISELECT) & ISELECT_MASK;
	if (isel >= ISELECT_IPRIO0 && isel <= ISELECT_IPRIO15)
		return aia_rmw_iprio(vcpu, isel, val, new_val, wr_mask);
	else if (isel >= IMSIC_FIRST && isel <= IMSIC_LAST &&
		 kvm_riscv_aia_initialized(vcpu->kvm))
		return kvm_riscv_vcpu_aia_imsic_rmw(vcpu, isel, val, new_val,
						    wr_mask);

	/* We can't handle it here so redirect to user space */
	return KVM_INSN_EXIT_TO_USER_SPACE;
}

void kvm_riscv_aia_enable(void)
{
	if (!kvm_riscv_aia_available())
		return;

	aia_set_hvictl(false);
	csr_write(CSR_HVIPRIO1, 0x0);
	csr_write(CSR_HVIPRIO2, 0x0);
#ifdef CONFIG_32BIT
	csr_write(CSR_HVIPH, 0x0);
	csr_write(CSR_HIDELEGH, 0x0);
	csr_write(CSR_HVIPRIO1H, 0x0);
	csr_write(CSR_HVIPRIO2H, 0x0);
#endif
}

void kvm_riscv_aia_disable(void)
{
	if (!kvm_riscv_aia_available())
		return;

	aia_set_hvictl(false);
}

int kvm_riscv_aia_init(void)
{
	if (!riscv_isa_extension_available(NULL, SxAIA))
		return -ENODEV;

	/* Enable KVM AIA support */
	static_branch_enable(&kvm_riscv_aia_available);

	return 0;
}

void kvm_riscv_aia_exit(void)
{
}
