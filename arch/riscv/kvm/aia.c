// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Western Digital Corporation or its affiliates.
 * Copyright (C) 2022 Ventana Micro Systems Inc.
 *
 * Authors:
 *	Anup Patel <apatel@ventanamicro.com>
 */

#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/irq.h>
#include <linux/irqchip/riscv-imsic.h>
#include <linux/irqdomain.h>
#include <linux/kvm_host.h>
#include <linux/percpu.h>
#include <linux/spinlock.h>
#include <asm/cpufeature.h>

struct aia_hgei_control {
	raw_spinlock_t lock;
	unsigned long free_bitmap;
	struct kvm_vcpu *owners[BITS_PER_LONG];
};
static DEFINE_PER_CPU(struct aia_hgei_control, aia_hgei);
static int hgei_parent_irq;

unsigned int kvm_riscv_aia_nr_hgei;
unsigned int kvm_riscv_aia_max_ids;
DEFINE_STATIC_KEY_FALSE(kvm_riscv_aia_available);

static int aia_find_hgei(struct kvm_vcpu *owner)
{
	int i, hgei;
	unsigned long flags;
	struct aia_hgei_control *hgctrl = get_cpu_ptr(&aia_hgei);

	raw_spin_lock_irqsave(&hgctrl->lock, flags);

	hgei = -1;
	for (i = 1; i <= kvm_riscv_aia_nr_hgei; i++) {
		if (hgctrl->owners[i] == owner) {
			hgei = i;
			break;
		}
	}

	raw_spin_unlock_irqrestore(&hgctrl->lock, flags);

	put_cpu_ptr(&aia_hgei);
	return hgei;
}

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
	int hgei;
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

	hgei = aia_find_hgei(vcpu);
	if (hgei > 0)
		return !!(csr_read(CSR_HGEIP) & BIT(hgei));

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
		return -ENOENT;

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
		return -ENOENT;

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

int kvm_riscv_aia_alloc_hgei(int cpu, struct kvm_vcpu *owner,
			     void __iomem **hgei_va, phys_addr_t *hgei_pa)
{
	int ret = -ENOENT;
	unsigned long flags;
	const struct imsic_global_config *gc;
	const struct imsic_local_config *lc;
	struct aia_hgei_control *hgctrl = per_cpu_ptr(&aia_hgei, cpu);

	if (!kvm_riscv_aia_available() || !hgctrl)
		return -ENODEV;

	raw_spin_lock_irqsave(&hgctrl->lock, flags);

	if (hgctrl->free_bitmap) {
		ret = __ffs(hgctrl->free_bitmap);
		hgctrl->free_bitmap &= ~BIT(ret);
		hgctrl->owners[ret] = owner;
	}

	raw_spin_unlock_irqrestore(&hgctrl->lock, flags);

	gc = imsic_get_global_config();
	lc = (gc) ? per_cpu_ptr(gc->local, cpu) : NULL;
	if (lc && ret > 0) {
		if (hgei_va)
			*hgei_va = lc->msi_va + (ret * IMSIC_MMIO_PAGE_SZ);
		if (hgei_pa)
			*hgei_pa = lc->msi_pa + (ret * IMSIC_MMIO_PAGE_SZ);
	}

	return ret;
}

void kvm_riscv_aia_free_hgei(int cpu, int hgei)
{
	unsigned long flags;
	struct aia_hgei_control *hgctrl = per_cpu_ptr(&aia_hgei, cpu);

	if (!kvm_riscv_aia_available() || !hgctrl)
		return;

	raw_spin_lock_irqsave(&hgctrl->lock, flags);

	if (hgei > 0 && hgei <= kvm_riscv_aia_nr_hgei) {
		if (!(hgctrl->free_bitmap & BIT(hgei))) {
			hgctrl->free_bitmap |= BIT(hgei);
			hgctrl->owners[hgei] = NULL;
		}
	}

	raw_spin_unlock_irqrestore(&hgctrl->lock, flags);
}

void kvm_riscv_aia_wakeon_hgei(struct kvm_vcpu *owner, bool enable)
{
	int hgei;

	if (!kvm_riscv_aia_available())
		return;

	hgei = aia_find_hgei(owner);
	if (hgei > 0) {
		if (enable)
			csr_set(CSR_HGEIE, BIT(hgei));
		else
			csr_clear(CSR_HGEIE, BIT(hgei));
	}
}

static irqreturn_t hgei_interrupt(int irq, void *dev_id)
{
	int i;
	unsigned long hgei_mask, flags;
	struct aia_hgei_control *hgctrl = get_cpu_ptr(&aia_hgei);

	hgei_mask = csr_read(CSR_HGEIP) & csr_read(CSR_HGEIE);
	csr_clear(CSR_HGEIE, hgei_mask);

	raw_spin_lock_irqsave(&hgctrl->lock, flags);

	for_each_set_bit(i, &hgei_mask, BITS_PER_LONG) {
		if (hgctrl->owners[i])
			kvm_vcpu_kick(hgctrl->owners[i]);
	}

	raw_spin_unlock_irqrestore(&hgctrl->lock, flags);

	put_cpu_ptr(&aia_hgei);
	return IRQ_HANDLED;
}

static int aia_hgei_init(void)
{
	int cpu, rc;
	struct irq_domain *domain;
	struct aia_hgei_control *hgctrl;

	/* Initialize per-CPU guest external interrupt line management */
	for_each_possible_cpu(cpu) {
		hgctrl = per_cpu_ptr(&aia_hgei, cpu);
		raw_spin_lock_init(&hgctrl->lock);
		if (kvm_riscv_aia_nr_hgei) {
			hgctrl->free_bitmap =
				BIT(kvm_riscv_aia_nr_hgei + 1) - 1;
			hgctrl->free_bitmap &= ~BIT(0);
		} else
			hgctrl->free_bitmap = 0;
	}

	/* Find INTC irq domain */
	domain = irq_find_matching_fwnode(riscv_get_intc_hwnode(),
					  DOMAIN_BUS_ANY);
	if (!domain) {
		kvm_err("unable to find INTC domain\n");
		return -ENOENT;
	}

	/* Map per-CPU SGEI interrupt from INTC domain */
	hgei_parent_irq = irq_create_mapping(domain, IRQ_S_GEXT);
	if (!hgei_parent_irq) {
		kvm_err("unable to map SGEI IRQ\n");
		return -ENOMEM;
	}

	/* Request per-CPU SGEI interrupt */
	rc = request_percpu_irq(hgei_parent_irq, hgei_interrupt,
				"riscv-kvm", &aia_hgei);
	if (rc) {
		kvm_err("failed to request SGEI IRQ\n");
		return rc;
	}

	return 0;
}

static void aia_hgei_exit(void)
{
	/* Free per-CPU SGEI interrupt */
	free_percpu_irq(hgei_parent_irq, &aia_hgei);
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

	/* Enable per-CPU SGEI interrupt */
	enable_percpu_irq(hgei_parent_irq,
			  irq_get_trigger_type(hgei_parent_irq));
	csr_set(CSR_HIE, BIT(IRQ_S_GEXT));
	/* Enable IRQ filtering for overflow interrupt only if sscofpmf is present */
	if (__riscv_isa_extension_available(NULL, RISCV_ISA_EXT_SSCOFPMF))
		csr_set(CSR_HVIEN, BIT(IRQ_PMU_OVF));
}

void kvm_riscv_aia_disable(void)
{
	int i;
	unsigned long flags;
	struct kvm_vcpu *vcpu;
	struct aia_hgei_control *hgctrl;

	if (!kvm_riscv_aia_available())
		return;
	hgctrl = get_cpu_ptr(&aia_hgei);

	if (__riscv_isa_extension_available(NULL, RISCV_ISA_EXT_SSCOFPMF))
		csr_clear(CSR_HVIEN, BIT(IRQ_PMU_OVF));
	/* Disable per-CPU SGEI interrupt */
	csr_clear(CSR_HIE, BIT(IRQ_S_GEXT));
	disable_percpu_irq(hgei_parent_irq);

	aia_set_hvictl(false);

	raw_spin_lock_irqsave(&hgctrl->lock, flags);

	for (i = 0; i <= kvm_riscv_aia_nr_hgei; i++) {
		vcpu = hgctrl->owners[i];
		if (!vcpu)
			continue;

		/*
		 * We release hgctrl->lock before notifying IMSIC
		 * so that we don't have lock ordering issues.
		 */
		raw_spin_unlock_irqrestore(&hgctrl->lock, flags);

		/* Notify IMSIC */
		kvm_riscv_vcpu_aia_imsic_release(vcpu);

		/*
		 * Wakeup VCPU if it was blocked so that it can
		 * run on other HARTs
		 */
		if (csr_read(CSR_HGEIE) & BIT(i)) {
			csr_clear(CSR_HGEIE, BIT(i));
			kvm_vcpu_kick(vcpu);
		}

		raw_spin_lock_irqsave(&hgctrl->lock, flags);
	}

	raw_spin_unlock_irqrestore(&hgctrl->lock, flags);

	put_cpu_ptr(&aia_hgei);
}

int kvm_riscv_aia_init(void)
{
	int rc;
	const struct imsic_global_config *gc;

	if (!riscv_isa_extension_available(NULL, SxAIA))
		return -ENODEV;
	gc = imsic_get_global_config();

	/* Figure-out number of bits in HGEIE */
	csr_write(CSR_HGEIE, -1UL);
	kvm_riscv_aia_nr_hgei = fls_long(csr_read(CSR_HGEIE));
	csr_write(CSR_HGEIE, 0);
	if (kvm_riscv_aia_nr_hgei)
		kvm_riscv_aia_nr_hgei--;

	/*
	 * Number of usable HGEI lines should be minimum of per-HART
	 * IMSIC guest files and number of bits in HGEIE
	 */
	if (gc)
		kvm_riscv_aia_nr_hgei = min((ulong)kvm_riscv_aia_nr_hgei,
					    BIT(gc->guest_index_bits) - 1);
	else
		kvm_riscv_aia_nr_hgei = 0;

	/* Find number of guest MSI IDs */
	kvm_riscv_aia_max_ids = IMSIC_MAX_ID;
	if (gc && kvm_riscv_aia_nr_hgei)
		kvm_riscv_aia_max_ids = gc->nr_guest_ids + 1;

	/* Initialize guest external interrupt line management */
	rc = aia_hgei_init();
	if (rc)
		return rc;

	/* Register device operations */
	rc = kvm_register_device_ops(&kvm_riscv_aia_device_ops,
				     KVM_DEV_TYPE_RISCV_AIA);
	if (rc) {
		aia_hgei_exit();
		return rc;
	}

	/* Enable KVM AIA support */
	static_branch_enable(&kvm_riscv_aia_available);

	return 0;
}

void kvm_riscv_aia_exit(void)
{
	if (!kvm_riscv_aia_available())
		return;

	/* Unregister device operations */
	kvm_unregister_device_ops(KVM_DEV_TYPE_RISCV_AIA);

	/* Cleanup the HGEI state */
	aia_hgei_exit();
}
