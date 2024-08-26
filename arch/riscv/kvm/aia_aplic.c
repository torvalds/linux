// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Western Digital Corporation or its affiliates.
 * Copyright (C) 2022 Ventana Micro Systems Inc.
 *
 * Authors:
 *	Anup Patel <apatel@ventanamicro.com>
 */

#include <linux/irqchip/riscv-aplic.h>
#include <linux/kvm_host.h>
#include <linux/math.h>
#include <linux/spinlock.h>
#include <linux/swab.h>
#include <kvm/iodev.h>

struct aplic_irq {
	raw_spinlock_t lock;
	u32 sourcecfg;
	u32 state;
#define APLIC_IRQ_STATE_PENDING		BIT(0)
#define APLIC_IRQ_STATE_ENABLED		BIT(1)
#define APLIC_IRQ_STATE_ENPEND		(APLIC_IRQ_STATE_PENDING | \
					 APLIC_IRQ_STATE_ENABLED)
#define APLIC_IRQ_STATE_INPUT		BIT(8)
	u32 target;
};

struct aplic {
	struct kvm_io_device iodev;

	u32 domaincfg;
	u32 genmsi;

	u32 nr_irqs;
	u32 nr_words;
	struct aplic_irq *irqs;
};

static u32 aplic_read_sourcecfg(struct aplic *aplic, u32 irq)
{
	u32 ret;
	unsigned long flags;
	struct aplic_irq *irqd;

	if (!irq || aplic->nr_irqs <= irq)
		return 0;
	irqd = &aplic->irqs[irq];

	raw_spin_lock_irqsave(&irqd->lock, flags);
	ret = irqd->sourcecfg;
	raw_spin_unlock_irqrestore(&irqd->lock, flags);

	return ret;
}

static void aplic_write_sourcecfg(struct aplic *aplic, u32 irq, u32 val)
{
	unsigned long flags;
	struct aplic_irq *irqd;

	if (!irq || aplic->nr_irqs <= irq)
		return;
	irqd = &aplic->irqs[irq];

	if (val & APLIC_SOURCECFG_D)
		val = 0;
	else
		val &= APLIC_SOURCECFG_SM_MASK;

	raw_spin_lock_irqsave(&irqd->lock, flags);
	irqd->sourcecfg = val;
	raw_spin_unlock_irqrestore(&irqd->lock, flags);
}

static u32 aplic_read_target(struct aplic *aplic, u32 irq)
{
	u32 ret;
	unsigned long flags;
	struct aplic_irq *irqd;

	if (!irq || aplic->nr_irqs <= irq)
		return 0;
	irqd = &aplic->irqs[irq];

	raw_spin_lock_irqsave(&irqd->lock, flags);
	ret = irqd->target;
	raw_spin_unlock_irqrestore(&irqd->lock, flags);

	return ret;
}

static void aplic_write_target(struct aplic *aplic, u32 irq, u32 val)
{
	unsigned long flags;
	struct aplic_irq *irqd;

	if (!irq || aplic->nr_irqs <= irq)
		return;
	irqd = &aplic->irqs[irq];

	val &= APLIC_TARGET_EIID_MASK |
	       (APLIC_TARGET_HART_IDX_MASK << APLIC_TARGET_HART_IDX_SHIFT) |
	       (APLIC_TARGET_GUEST_IDX_MASK << APLIC_TARGET_GUEST_IDX_SHIFT);

	raw_spin_lock_irqsave(&irqd->lock, flags);
	irqd->target = val;
	raw_spin_unlock_irqrestore(&irqd->lock, flags);
}

static bool aplic_read_pending(struct aplic *aplic, u32 irq)
{
	bool ret;
	unsigned long flags;
	struct aplic_irq *irqd;

	if (!irq || aplic->nr_irqs <= irq)
		return false;
	irqd = &aplic->irqs[irq];

	raw_spin_lock_irqsave(&irqd->lock, flags);
	ret = (irqd->state & APLIC_IRQ_STATE_PENDING) ? true : false;
	raw_spin_unlock_irqrestore(&irqd->lock, flags);

	return ret;
}

static void aplic_write_pending(struct aplic *aplic, u32 irq, bool pending)
{
	unsigned long flags, sm;
	struct aplic_irq *irqd;

	if (!irq || aplic->nr_irqs <= irq)
		return;
	irqd = &aplic->irqs[irq];

	raw_spin_lock_irqsave(&irqd->lock, flags);

	sm = irqd->sourcecfg & APLIC_SOURCECFG_SM_MASK;
	if (sm == APLIC_SOURCECFG_SM_INACTIVE)
		goto skip_write_pending;

	if (sm == APLIC_SOURCECFG_SM_LEVEL_HIGH ||
	    sm == APLIC_SOURCECFG_SM_LEVEL_LOW) {
		if (!pending)
			goto skip_write_pending;
		if ((irqd->state & APLIC_IRQ_STATE_INPUT) &&
		    sm == APLIC_SOURCECFG_SM_LEVEL_LOW)
			goto skip_write_pending;
		if (!(irqd->state & APLIC_IRQ_STATE_INPUT) &&
		    sm == APLIC_SOURCECFG_SM_LEVEL_HIGH)
			goto skip_write_pending;
	}

	if (pending)
		irqd->state |= APLIC_IRQ_STATE_PENDING;
	else
		irqd->state &= ~APLIC_IRQ_STATE_PENDING;

skip_write_pending:
	raw_spin_unlock_irqrestore(&irqd->lock, flags);
}

static bool aplic_read_enabled(struct aplic *aplic, u32 irq)
{
	bool ret;
	unsigned long flags;
	struct aplic_irq *irqd;

	if (!irq || aplic->nr_irqs <= irq)
		return false;
	irqd = &aplic->irqs[irq];

	raw_spin_lock_irqsave(&irqd->lock, flags);
	ret = (irqd->state & APLIC_IRQ_STATE_ENABLED) ? true : false;
	raw_spin_unlock_irqrestore(&irqd->lock, flags);

	return ret;
}

static void aplic_write_enabled(struct aplic *aplic, u32 irq, bool enabled)
{
	unsigned long flags;
	struct aplic_irq *irqd;

	if (!irq || aplic->nr_irqs <= irq)
		return;
	irqd = &aplic->irqs[irq];

	raw_spin_lock_irqsave(&irqd->lock, flags);
	if (enabled)
		irqd->state |= APLIC_IRQ_STATE_ENABLED;
	else
		irqd->state &= ~APLIC_IRQ_STATE_ENABLED;
	raw_spin_unlock_irqrestore(&irqd->lock, flags);
}

static bool aplic_read_input(struct aplic *aplic, u32 irq)
{
	u32 sourcecfg, sm, raw_input, irq_inverted;
	struct aplic_irq *irqd;
	unsigned long flags;
	bool ret = false;

	if (!irq || aplic->nr_irqs <= irq)
		return false;
	irqd = &aplic->irqs[irq];

	raw_spin_lock_irqsave(&irqd->lock, flags);

	sourcecfg = irqd->sourcecfg;
	if (sourcecfg & APLIC_SOURCECFG_D)
		goto skip;

	sm = sourcecfg & APLIC_SOURCECFG_SM_MASK;
	if (sm == APLIC_SOURCECFG_SM_INACTIVE)
		goto skip;

	raw_input = (irqd->state & APLIC_IRQ_STATE_INPUT) ? 1 : 0;
	irq_inverted = (sm == APLIC_SOURCECFG_SM_LEVEL_LOW ||
			sm == APLIC_SOURCECFG_SM_EDGE_FALL) ? 1 : 0;
	ret = !!(raw_input ^ irq_inverted);

skip:
	raw_spin_unlock_irqrestore(&irqd->lock, flags);

	return ret;
}

static void aplic_inject_msi(struct kvm *kvm, u32 irq, u32 target)
{
	u32 hart_idx, guest_idx, eiid;

	hart_idx = target >> APLIC_TARGET_HART_IDX_SHIFT;
	hart_idx &= APLIC_TARGET_HART_IDX_MASK;
	guest_idx = target >> APLIC_TARGET_GUEST_IDX_SHIFT;
	guest_idx &= APLIC_TARGET_GUEST_IDX_MASK;
	eiid = target & APLIC_TARGET_EIID_MASK;
	kvm_riscv_aia_inject_msi_by_id(kvm, hart_idx, guest_idx, eiid);
}

static void aplic_update_irq_range(struct kvm *kvm, u32 first, u32 last)
{
	bool inject;
	u32 irq, target;
	unsigned long flags;
	struct aplic_irq *irqd;
	struct aplic *aplic = kvm->arch.aia.aplic_state;

	if (!(aplic->domaincfg & APLIC_DOMAINCFG_IE))
		return;

	for (irq = first; irq <= last; irq++) {
		if (!irq || aplic->nr_irqs <= irq)
			continue;
		irqd = &aplic->irqs[irq];

		raw_spin_lock_irqsave(&irqd->lock, flags);

		inject = false;
		target = irqd->target;
		if ((irqd->state & APLIC_IRQ_STATE_ENPEND) ==
		    APLIC_IRQ_STATE_ENPEND) {
			irqd->state &= ~APLIC_IRQ_STATE_PENDING;
			inject = true;
		}

		raw_spin_unlock_irqrestore(&irqd->lock, flags);

		if (inject)
			aplic_inject_msi(kvm, irq, target);
	}
}

int kvm_riscv_aia_aplic_inject(struct kvm *kvm, u32 source, bool level)
{
	u32 target;
	bool inject = false, ie;
	unsigned long flags;
	struct aplic_irq *irqd;
	struct aplic *aplic = kvm->arch.aia.aplic_state;

	if (!aplic || !source || (aplic->nr_irqs <= source))
		return -ENODEV;
	irqd = &aplic->irqs[source];
	ie = (aplic->domaincfg & APLIC_DOMAINCFG_IE) ? true : false;

	raw_spin_lock_irqsave(&irqd->lock, flags);

	if (irqd->sourcecfg & APLIC_SOURCECFG_D)
		goto skip_unlock;

	switch (irqd->sourcecfg & APLIC_SOURCECFG_SM_MASK) {
	case APLIC_SOURCECFG_SM_EDGE_RISE:
		if (level && !(irqd->state & APLIC_IRQ_STATE_INPUT) &&
		    !(irqd->state & APLIC_IRQ_STATE_PENDING))
			irqd->state |= APLIC_IRQ_STATE_PENDING;
		break;
	case APLIC_SOURCECFG_SM_EDGE_FALL:
		if (!level && (irqd->state & APLIC_IRQ_STATE_INPUT) &&
		    !(irqd->state & APLIC_IRQ_STATE_PENDING))
			irqd->state |= APLIC_IRQ_STATE_PENDING;
		break;
	case APLIC_SOURCECFG_SM_LEVEL_HIGH:
		if (level && !(irqd->state & APLIC_IRQ_STATE_PENDING))
			irqd->state |= APLIC_IRQ_STATE_PENDING;
		break;
	case APLIC_SOURCECFG_SM_LEVEL_LOW:
		if (!level && !(irqd->state & APLIC_IRQ_STATE_PENDING))
			irqd->state |= APLIC_IRQ_STATE_PENDING;
		break;
	}

	if (level)
		irqd->state |= APLIC_IRQ_STATE_INPUT;
	else
		irqd->state &= ~APLIC_IRQ_STATE_INPUT;

	target = irqd->target;
	if (ie && ((irqd->state & APLIC_IRQ_STATE_ENPEND) ==
		   APLIC_IRQ_STATE_ENPEND)) {
		irqd->state &= ~APLIC_IRQ_STATE_PENDING;
		inject = true;
	}

skip_unlock:
	raw_spin_unlock_irqrestore(&irqd->lock, flags);

	if (inject)
		aplic_inject_msi(kvm, source, target);

	return 0;
}

static u32 aplic_read_input_word(struct aplic *aplic, u32 word)
{
	u32 i, ret = 0;

	for (i = 0; i < 32; i++)
		ret |= aplic_read_input(aplic, word * 32 + i) ? BIT(i) : 0;

	return ret;
}

static u32 aplic_read_pending_word(struct aplic *aplic, u32 word)
{
	u32 i, ret = 0;

	for (i = 0; i < 32; i++)
		ret |= aplic_read_pending(aplic, word * 32 + i) ? BIT(i) : 0;

	return ret;
}

static void aplic_write_pending_word(struct aplic *aplic, u32 word,
				     u32 val, bool pending)
{
	u32 i;

	for (i = 0; i < 32; i++) {
		if (val & BIT(i))
			aplic_write_pending(aplic, word * 32 + i, pending);
	}
}

static u32 aplic_read_enabled_word(struct aplic *aplic, u32 word)
{
	u32 i, ret = 0;

	for (i = 0; i < 32; i++)
		ret |= aplic_read_enabled(aplic, word * 32 + i) ? BIT(i) : 0;

	return ret;
}

static void aplic_write_enabled_word(struct aplic *aplic, u32 word,
				     u32 val, bool enabled)
{
	u32 i;

	for (i = 0; i < 32; i++) {
		if (val & BIT(i))
			aplic_write_enabled(aplic, word * 32 + i, enabled);
	}
}

static int aplic_mmio_read_offset(struct kvm *kvm, gpa_t off, u32 *val32)
{
	u32 i;
	struct aplic *aplic = kvm->arch.aia.aplic_state;

	if ((off & 0x3) != 0)
		return -EOPNOTSUPP;

	if (off == APLIC_DOMAINCFG) {
		*val32 = APLIC_DOMAINCFG_RDONLY |
			 aplic->domaincfg | APLIC_DOMAINCFG_DM;
	} else if ((off >= APLIC_SOURCECFG_BASE) &&
		 (off < (APLIC_SOURCECFG_BASE + (aplic->nr_irqs - 1) * 4))) {
		i = ((off - APLIC_SOURCECFG_BASE) >> 2) + 1;
		*val32 = aplic_read_sourcecfg(aplic, i);
	} else if ((off >= APLIC_SETIP_BASE) &&
		   (off < (APLIC_SETIP_BASE + aplic->nr_words * 4))) {
		i = (off - APLIC_SETIP_BASE) >> 2;
		*val32 = aplic_read_pending_word(aplic, i);
	} else if (off == APLIC_SETIPNUM) {
		*val32 = 0;
	} else if ((off >= APLIC_CLRIP_BASE) &&
		   (off < (APLIC_CLRIP_BASE + aplic->nr_words * 4))) {
		i = (off - APLIC_CLRIP_BASE) >> 2;
		*val32 = aplic_read_input_word(aplic, i);
	} else if (off == APLIC_CLRIPNUM) {
		*val32 = 0;
	} else if ((off >= APLIC_SETIE_BASE) &&
		   (off < (APLIC_SETIE_BASE + aplic->nr_words * 4))) {
		i = (off - APLIC_SETIE_BASE) >> 2;
		*val32 = aplic_read_enabled_word(aplic, i);
	} else if (off == APLIC_SETIENUM) {
		*val32 = 0;
	} else if ((off >= APLIC_CLRIE_BASE) &&
		   (off < (APLIC_CLRIE_BASE + aplic->nr_words * 4))) {
		*val32 = 0;
	} else if (off == APLIC_CLRIENUM) {
		*val32 = 0;
	} else if (off == APLIC_SETIPNUM_LE) {
		*val32 = 0;
	} else if (off == APLIC_SETIPNUM_BE) {
		*val32 = 0;
	} else if (off == APLIC_GENMSI) {
		*val32 = aplic->genmsi;
	} else if ((off >= APLIC_TARGET_BASE) &&
		   (off < (APLIC_TARGET_BASE + (aplic->nr_irqs - 1) * 4))) {
		i = ((off - APLIC_TARGET_BASE) >> 2) + 1;
		*val32 = aplic_read_target(aplic, i);
	} else
		return -ENODEV;

	return 0;
}

static int aplic_mmio_read(struct kvm_vcpu *vcpu, struct kvm_io_device *dev,
			   gpa_t addr, int len, void *val)
{
	if (len != 4)
		return -EOPNOTSUPP;

	return aplic_mmio_read_offset(vcpu->kvm,
				      addr - vcpu->kvm->arch.aia.aplic_addr,
				      val);
}

static int aplic_mmio_write_offset(struct kvm *kvm, gpa_t off, u32 val32)
{
	u32 i;
	struct aplic *aplic = kvm->arch.aia.aplic_state;

	if ((off & 0x3) != 0)
		return -EOPNOTSUPP;

	if (off == APLIC_DOMAINCFG) {
		/* Only IE bit writeable */
		aplic->domaincfg = val32 & APLIC_DOMAINCFG_IE;
	} else if ((off >= APLIC_SOURCECFG_BASE) &&
		 (off < (APLIC_SOURCECFG_BASE + (aplic->nr_irqs - 1) * 4))) {
		i = ((off - APLIC_SOURCECFG_BASE) >> 2) + 1;
		aplic_write_sourcecfg(aplic, i, val32);
	} else if ((off >= APLIC_SETIP_BASE) &&
		   (off < (APLIC_SETIP_BASE + aplic->nr_words * 4))) {
		i = (off - APLIC_SETIP_BASE) >> 2;
		aplic_write_pending_word(aplic, i, val32, true);
	} else if (off == APLIC_SETIPNUM) {
		aplic_write_pending(aplic, val32, true);
	} else if ((off >= APLIC_CLRIP_BASE) &&
		   (off < (APLIC_CLRIP_BASE + aplic->nr_words * 4))) {
		i = (off - APLIC_CLRIP_BASE) >> 2;
		aplic_write_pending_word(aplic, i, val32, false);
	} else if (off == APLIC_CLRIPNUM) {
		aplic_write_pending(aplic, val32, false);
	} else if ((off >= APLIC_SETIE_BASE) &&
		   (off < (APLIC_SETIE_BASE + aplic->nr_words * 4))) {
		i = (off - APLIC_SETIE_BASE) >> 2;
		aplic_write_enabled_word(aplic, i, val32, true);
	} else if (off == APLIC_SETIENUM) {
		aplic_write_enabled(aplic, val32, true);
	} else if ((off >= APLIC_CLRIE_BASE) &&
		   (off < (APLIC_CLRIE_BASE + aplic->nr_words * 4))) {
		i = (off - APLIC_CLRIE_BASE) >> 2;
		aplic_write_enabled_word(aplic, i, val32, false);
	} else if (off == APLIC_CLRIENUM) {
		aplic_write_enabled(aplic, val32, false);
	} else if (off == APLIC_SETIPNUM_LE) {
		aplic_write_pending(aplic, val32, true);
	} else if (off == APLIC_SETIPNUM_BE) {
		aplic_write_pending(aplic, __swab32(val32), true);
	} else if (off == APLIC_GENMSI) {
		aplic->genmsi = val32 & ~(APLIC_TARGET_GUEST_IDX_MASK <<
					  APLIC_TARGET_GUEST_IDX_SHIFT);
		kvm_riscv_aia_inject_msi_by_id(kvm,
				val32 >> APLIC_TARGET_HART_IDX_SHIFT, 0,
				val32 & APLIC_TARGET_EIID_MASK);
	} else if ((off >= APLIC_TARGET_BASE) &&
		   (off < (APLIC_TARGET_BASE + (aplic->nr_irqs - 1) * 4))) {
		i = ((off - APLIC_TARGET_BASE) >> 2) + 1;
		aplic_write_target(aplic, i, val32);
	} else
		return -ENODEV;

	aplic_update_irq_range(kvm, 1, aplic->nr_irqs - 1);

	return 0;
}

static int aplic_mmio_write(struct kvm_vcpu *vcpu, struct kvm_io_device *dev,
			    gpa_t addr, int len, const void *val)
{
	if (len != 4)
		return -EOPNOTSUPP;

	return aplic_mmio_write_offset(vcpu->kvm,
				       addr - vcpu->kvm->arch.aia.aplic_addr,
				       *((const u32 *)val));
}

static struct kvm_io_device_ops aplic_iodoev_ops = {
	.read = aplic_mmio_read,
	.write = aplic_mmio_write,
};

int kvm_riscv_aia_aplic_set_attr(struct kvm *kvm, unsigned long type, u32 v)
{
	int rc;

	if (!kvm->arch.aia.aplic_state)
		return -ENODEV;

	rc = aplic_mmio_write_offset(kvm, type, v);
	if (rc)
		return rc;

	return 0;
}

int kvm_riscv_aia_aplic_get_attr(struct kvm *kvm, unsigned long type, u32 *v)
{
	int rc;

	if (!kvm->arch.aia.aplic_state)
		return -ENODEV;

	rc = aplic_mmio_read_offset(kvm, type, v);
	if (rc)
		return rc;

	return 0;
}

int kvm_riscv_aia_aplic_has_attr(struct kvm *kvm, unsigned long type)
{
	int rc;
	u32 val;

	if (!kvm->arch.aia.aplic_state)
		return -ENODEV;

	rc = aplic_mmio_read_offset(kvm, type, &val);
	if (rc)
		return rc;

	return 0;
}

int kvm_riscv_aia_aplic_init(struct kvm *kvm)
{
	int i, ret = 0;
	struct aplic *aplic;

	/* Do nothing if we have zero sources */
	if (!kvm->arch.aia.nr_sources)
		return 0;

	/* Allocate APLIC global state */
	aplic = kzalloc(sizeof(*aplic), GFP_KERNEL);
	if (!aplic)
		return -ENOMEM;
	kvm->arch.aia.aplic_state = aplic;

	/* Setup APLIC IRQs */
	aplic->nr_irqs = kvm->arch.aia.nr_sources + 1;
	aplic->nr_words = DIV_ROUND_UP(aplic->nr_irqs, 32);
	aplic->irqs = kcalloc(aplic->nr_irqs,
			      sizeof(*aplic->irqs), GFP_KERNEL);
	if (!aplic->irqs) {
		ret = -ENOMEM;
		goto fail_free_aplic;
	}
	for (i = 0; i < aplic->nr_irqs; i++)
		raw_spin_lock_init(&aplic->irqs[i].lock);

	/* Setup IO device */
	kvm_iodevice_init(&aplic->iodev, &aplic_iodoev_ops);
	mutex_lock(&kvm->slots_lock);
	ret = kvm_io_bus_register_dev(kvm, KVM_MMIO_BUS,
				      kvm->arch.aia.aplic_addr,
				      KVM_DEV_RISCV_APLIC_SIZE,
				      &aplic->iodev);
	mutex_unlock(&kvm->slots_lock);
	if (ret)
		goto fail_free_aplic_irqs;

	/* Setup default IRQ routing */
	ret = kvm_riscv_setup_default_irq_routing(kvm, aplic->nr_irqs);
	if (ret)
		goto fail_unreg_iodev;

	return 0;

fail_unreg_iodev:
	mutex_lock(&kvm->slots_lock);
	kvm_io_bus_unregister_dev(kvm, KVM_MMIO_BUS, &aplic->iodev);
	mutex_unlock(&kvm->slots_lock);
fail_free_aplic_irqs:
	kfree(aplic->irqs);
fail_free_aplic:
	kvm->arch.aia.aplic_state = NULL;
	kfree(aplic);
	return ret;
}

void kvm_riscv_aia_aplic_cleanup(struct kvm *kvm)
{
	struct aplic *aplic = kvm->arch.aia.aplic_state;

	if (!aplic)
		return;

	mutex_lock(&kvm->slots_lock);
	kvm_io_bus_unregister_dev(kvm, KVM_MMIO_BUS, &aplic->iodev);
	mutex_unlock(&kvm->slots_lock);

	kfree(aplic->irqs);

	kvm->arch.aia.aplic_state = NULL;
	kfree(aplic);
}
