// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Western Digital Corporation or its affiliates.
 * Copyright (C) 2022 Ventana Micro Systems Inc.
 *
 * Authors:
 *	Anup Patel <apatel@ventanamicro.com>
 */

#include <linux/bits.h>
#include <linux/irqchip/riscv-imsic.h>
#include <linux/kvm_host.h>
#include <linux/uaccess.h>

static void unlock_vcpus(struct kvm *kvm, int vcpu_lock_idx)
{
	struct kvm_vcpu *tmp_vcpu;

	for (; vcpu_lock_idx >= 0; vcpu_lock_idx--) {
		tmp_vcpu = kvm_get_vcpu(kvm, vcpu_lock_idx);
		mutex_unlock(&tmp_vcpu->mutex);
	}
}

static void unlock_all_vcpus(struct kvm *kvm)
{
	unlock_vcpus(kvm, atomic_read(&kvm->online_vcpus) - 1);
}

static bool lock_all_vcpus(struct kvm *kvm)
{
	struct kvm_vcpu *tmp_vcpu;
	unsigned long c;

	kvm_for_each_vcpu(c, tmp_vcpu, kvm) {
		if (!mutex_trylock(&tmp_vcpu->mutex)) {
			unlock_vcpus(kvm, c - 1);
			return false;
		}
	}

	return true;
}

static int aia_create(struct kvm_device *dev, u32 type)
{
	int ret;
	unsigned long i;
	struct kvm *kvm = dev->kvm;
	struct kvm_vcpu *vcpu;

	if (irqchip_in_kernel(kvm))
		return -EEXIST;

	ret = -EBUSY;
	if (!lock_all_vcpus(kvm))
		return ret;

	kvm_for_each_vcpu(i, vcpu, kvm) {
		if (vcpu->arch.ran_atleast_once)
			goto out_unlock;
	}
	ret = 0;

	kvm->arch.aia.in_kernel = true;

out_unlock:
	unlock_all_vcpus(kvm);
	return ret;
}

static void aia_destroy(struct kvm_device *dev)
{
	kfree(dev);
}

static int aia_config(struct kvm *kvm, unsigned long type,
		      u32 *nr, bool write)
{
	struct kvm_aia *aia = &kvm->arch.aia;

	/* Writes can only be done before irqchip is initialized */
	if (write && kvm_riscv_aia_initialized(kvm))
		return -EBUSY;

	switch (type) {
	case KVM_DEV_RISCV_AIA_CONFIG_MODE:
		if (write) {
			switch (*nr) {
			case KVM_DEV_RISCV_AIA_MODE_EMUL:
				break;
			case KVM_DEV_RISCV_AIA_MODE_HWACCEL:
			case KVM_DEV_RISCV_AIA_MODE_AUTO:
				/*
				 * HW Acceleration and Auto modes only
				 * supported on host with non-zero guest
				 * external interrupts (i.e. non-zero
				 * VS-level IMSIC pages).
				 */
				if (!kvm_riscv_aia_nr_hgei)
					return -EINVAL;
				break;
			default:
				return -EINVAL;
			}
			aia->mode = *nr;
		} else
			*nr = aia->mode;
		break;
	case KVM_DEV_RISCV_AIA_CONFIG_IDS:
		if (write) {
			if ((*nr < KVM_DEV_RISCV_AIA_IDS_MIN) ||
			    (*nr >= KVM_DEV_RISCV_AIA_IDS_MAX) ||
			    ((*nr & KVM_DEV_RISCV_AIA_IDS_MIN) !=
			     KVM_DEV_RISCV_AIA_IDS_MIN) ||
			    (kvm_riscv_aia_max_ids <= *nr))
				return -EINVAL;
			aia->nr_ids = *nr;
		} else
			*nr = aia->nr_ids;
		break;
	case KVM_DEV_RISCV_AIA_CONFIG_SRCS:
		if (write) {
			if ((*nr >= KVM_DEV_RISCV_AIA_SRCS_MAX) ||
			    (*nr >= kvm_riscv_aia_max_ids))
				return -EINVAL;
			aia->nr_sources = *nr;
		} else
			*nr = aia->nr_sources;
		break;
	case KVM_DEV_RISCV_AIA_CONFIG_GROUP_BITS:
		if (write) {
			if (*nr >= KVM_DEV_RISCV_AIA_GROUP_BITS_MAX)
				return -EINVAL;
			aia->nr_group_bits = *nr;
		} else
			*nr = aia->nr_group_bits;
		break;
	case KVM_DEV_RISCV_AIA_CONFIG_GROUP_SHIFT:
		if (write) {
			if ((*nr < KVM_DEV_RISCV_AIA_GROUP_SHIFT_MIN) ||
			    (*nr >= KVM_DEV_RISCV_AIA_GROUP_SHIFT_MAX))
				return -EINVAL;
			aia->nr_group_shift = *nr;
		} else
			*nr = aia->nr_group_shift;
		break;
	case KVM_DEV_RISCV_AIA_CONFIG_HART_BITS:
		if (write) {
			if (*nr >= KVM_DEV_RISCV_AIA_HART_BITS_MAX)
				return -EINVAL;
			aia->nr_hart_bits = *nr;
		} else
			*nr = aia->nr_hart_bits;
		break;
	case KVM_DEV_RISCV_AIA_CONFIG_GUEST_BITS:
		if (write) {
			if (*nr >= KVM_DEV_RISCV_AIA_GUEST_BITS_MAX)
				return -EINVAL;
			aia->nr_guest_bits = *nr;
		} else
			*nr = aia->nr_guest_bits;
		break;
	default:
		return -ENXIO;
	}

	return 0;
}

static int aia_aplic_addr(struct kvm *kvm, u64 *addr, bool write)
{
	struct kvm_aia *aia = &kvm->arch.aia;

	if (write) {
		/* Writes can only be done before irqchip is initialized */
		if (kvm_riscv_aia_initialized(kvm))
			return -EBUSY;

		if (*addr & (KVM_DEV_RISCV_APLIC_ALIGN - 1))
			return -EINVAL;

		aia->aplic_addr = *addr;
	} else
		*addr = aia->aplic_addr;

	return 0;
}

static int aia_imsic_addr(struct kvm *kvm, u64 *addr,
			  unsigned long vcpu_idx, bool write)
{
	struct kvm_vcpu *vcpu;
	struct kvm_vcpu_aia *vcpu_aia;

	vcpu = kvm_get_vcpu(kvm, vcpu_idx);
	if (!vcpu)
		return -EINVAL;
	vcpu_aia = &vcpu->arch.aia_context;

	if (write) {
		/* Writes can only be done before irqchip is initialized */
		if (kvm_riscv_aia_initialized(kvm))
			return -EBUSY;

		if (*addr & (KVM_DEV_RISCV_IMSIC_ALIGN - 1))
			return -EINVAL;
	}

	mutex_lock(&vcpu->mutex);
	if (write)
		vcpu_aia->imsic_addr = *addr;
	else
		*addr = vcpu_aia->imsic_addr;
	mutex_unlock(&vcpu->mutex);

	return 0;
}

static gpa_t aia_imsic_ppn(struct kvm_aia *aia, gpa_t addr)
{
	u32 h, l;
	gpa_t mask = 0;

	h = aia->nr_hart_bits + aia->nr_guest_bits +
	    IMSIC_MMIO_PAGE_SHIFT - 1;
	mask = GENMASK_ULL(h, 0);

	if (aia->nr_group_bits) {
		h = aia->nr_group_bits + aia->nr_group_shift - 1;
		l = aia->nr_group_shift;
		mask |= GENMASK_ULL(h, l);
	}

	return (addr & ~mask) >> IMSIC_MMIO_PAGE_SHIFT;
}

static u32 aia_imsic_hart_index(struct kvm_aia *aia, gpa_t addr)
{
	u32 hart = 0, group = 0;

	if (aia->nr_hart_bits)
		hart = (addr >> (aia->nr_guest_bits + IMSIC_MMIO_PAGE_SHIFT)) &
		       GENMASK_ULL(aia->nr_hart_bits - 1, 0);
	if (aia->nr_group_bits)
		group = (addr >> aia->nr_group_shift) &
			GENMASK_ULL(aia->nr_group_bits - 1, 0);

	return (group << aia->nr_hart_bits) | hart;
}

static int aia_init(struct kvm *kvm)
{
	int ret, i;
	unsigned long idx;
	struct kvm_vcpu *vcpu;
	struct kvm_vcpu_aia *vaia;
	struct kvm_aia *aia = &kvm->arch.aia;
	gpa_t base_ppn = KVM_RISCV_AIA_UNDEF_ADDR;

	/* Irqchip can be initialized only once */
	if (kvm_riscv_aia_initialized(kvm))
		return -EBUSY;

	/* We might be in the middle of creating a VCPU? */
	if (kvm->created_vcpus != atomic_read(&kvm->online_vcpus))
		return -EBUSY;

	/* Number of sources should be less than or equals number of IDs */
	if (aia->nr_ids < aia->nr_sources)
		return -EINVAL;

	/* APLIC base is required for non-zero number of sources */
	if (aia->nr_sources && aia->aplic_addr == KVM_RISCV_AIA_UNDEF_ADDR)
		return -EINVAL;

	/* Initialize APLIC */
	ret = kvm_riscv_aia_aplic_init(kvm);
	if (ret)
		return ret;

	/* Iterate over each VCPU */
	kvm_for_each_vcpu(idx, vcpu, kvm) {
		vaia = &vcpu->arch.aia_context;

		/* IMSIC base is required */
		if (vaia->imsic_addr == KVM_RISCV_AIA_UNDEF_ADDR) {
			ret = -EINVAL;
			goto fail_cleanup_imsics;
		}

		/* All IMSICs should have matching base PPN */
		if (base_ppn == KVM_RISCV_AIA_UNDEF_ADDR)
			base_ppn = aia_imsic_ppn(aia, vaia->imsic_addr);
		if (base_ppn != aia_imsic_ppn(aia, vaia->imsic_addr)) {
			ret = -EINVAL;
			goto fail_cleanup_imsics;
		}

		/* Update HART index of the IMSIC based on IMSIC base */
		vaia->hart_index = aia_imsic_hart_index(aia,
							vaia->imsic_addr);

		/* Initialize IMSIC for this VCPU */
		ret = kvm_riscv_vcpu_aia_imsic_init(vcpu);
		if (ret)
			goto fail_cleanup_imsics;
	}

	/* Set the initialized flag */
	kvm->arch.aia.initialized = true;

	return 0;

fail_cleanup_imsics:
	for (i = idx - 1; i >= 0; i--) {
		vcpu = kvm_get_vcpu(kvm, i);
		if (!vcpu)
			continue;
		kvm_riscv_vcpu_aia_imsic_cleanup(vcpu);
	}
	kvm_riscv_aia_aplic_cleanup(kvm);
	return ret;
}

static int aia_set_attr(struct kvm_device *dev, struct kvm_device_attr *attr)
{
	u32 nr;
	u64 addr;
	int nr_vcpus, r = -ENXIO;
	unsigned long v, type = (unsigned long)attr->attr;
	void __user *uaddr = (void __user *)(long)attr->addr;

	switch (attr->group) {
	case KVM_DEV_RISCV_AIA_GRP_CONFIG:
		if (copy_from_user(&nr, uaddr, sizeof(nr)))
			return -EFAULT;

		mutex_lock(&dev->kvm->lock);
		r = aia_config(dev->kvm, type, &nr, true);
		mutex_unlock(&dev->kvm->lock);

		break;

	case KVM_DEV_RISCV_AIA_GRP_ADDR:
		if (copy_from_user(&addr, uaddr, sizeof(addr)))
			return -EFAULT;

		nr_vcpus = atomic_read(&dev->kvm->online_vcpus);
		mutex_lock(&dev->kvm->lock);
		if (type == KVM_DEV_RISCV_AIA_ADDR_APLIC)
			r = aia_aplic_addr(dev->kvm, &addr, true);
		else if (type < KVM_DEV_RISCV_AIA_ADDR_IMSIC(nr_vcpus))
			r = aia_imsic_addr(dev->kvm, &addr,
			    type - KVM_DEV_RISCV_AIA_ADDR_IMSIC(0), true);
		mutex_unlock(&dev->kvm->lock);

		break;

	case KVM_DEV_RISCV_AIA_GRP_CTRL:
		switch (type) {
		case KVM_DEV_RISCV_AIA_CTRL_INIT:
			mutex_lock(&dev->kvm->lock);
			r = aia_init(dev->kvm);
			mutex_unlock(&dev->kvm->lock);
			break;
		}

		break;
	case KVM_DEV_RISCV_AIA_GRP_APLIC:
		if (copy_from_user(&nr, uaddr, sizeof(nr)))
			return -EFAULT;

		mutex_lock(&dev->kvm->lock);
		r = kvm_riscv_aia_aplic_set_attr(dev->kvm, type, nr);
		mutex_unlock(&dev->kvm->lock);

		break;
	case KVM_DEV_RISCV_AIA_GRP_IMSIC:
		if (copy_from_user(&v, uaddr, sizeof(v)))
			return -EFAULT;

		mutex_lock(&dev->kvm->lock);
		r = kvm_riscv_aia_imsic_rw_attr(dev->kvm, type, true, &v);
		mutex_unlock(&dev->kvm->lock);

		break;
	}

	return r;
}

static int aia_get_attr(struct kvm_device *dev, struct kvm_device_attr *attr)
{
	u32 nr;
	u64 addr;
	int nr_vcpus, r = -ENXIO;
	void __user *uaddr = (void __user *)(long)attr->addr;
	unsigned long v, type = (unsigned long)attr->attr;

	switch (attr->group) {
	case KVM_DEV_RISCV_AIA_GRP_CONFIG:
		if (copy_from_user(&nr, uaddr, sizeof(nr)))
			return -EFAULT;

		mutex_lock(&dev->kvm->lock);
		r = aia_config(dev->kvm, type, &nr, false);
		mutex_unlock(&dev->kvm->lock);
		if (r)
			return r;

		if (copy_to_user(uaddr, &nr, sizeof(nr)))
			return -EFAULT;

		break;
	case KVM_DEV_RISCV_AIA_GRP_ADDR:
		if (copy_from_user(&addr, uaddr, sizeof(addr)))
			return -EFAULT;

		nr_vcpus = atomic_read(&dev->kvm->online_vcpus);
		mutex_lock(&dev->kvm->lock);
		if (type == KVM_DEV_RISCV_AIA_ADDR_APLIC)
			r = aia_aplic_addr(dev->kvm, &addr, false);
		else if (type < KVM_DEV_RISCV_AIA_ADDR_IMSIC(nr_vcpus))
			r = aia_imsic_addr(dev->kvm, &addr,
			    type - KVM_DEV_RISCV_AIA_ADDR_IMSIC(0), false);
		mutex_unlock(&dev->kvm->lock);
		if (r)
			return r;

		if (copy_to_user(uaddr, &addr, sizeof(addr)))
			return -EFAULT;

		break;
	case KVM_DEV_RISCV_AIA_GRP_APLIC:
		if (copy_from_user(&nr, uaddr, sizeof(nr)))
			return -EFAULT;

		mutex_lock(&dev->kvm->lock);
		r = kvm_riscv_aia_aplic_get_attr(dev->kvm, type, &nr);
		mutex_unlock(&dev->kvm->lock);
		if (r)
			return r;

		if (copy_to_user(uaddr, &nr, sizeof(nr)))
			return -EFAULT;

		break;
	case KVM_DEV_RISCV_AIA_GRP_IMSIC:
		if (copy_from_user(&v, uaddr, sizeof(v)))
			return -EFAULT;

		mutex_lock(&dev->kvm->lock);
		r = kvm_riscv_aia_imsic_rw_attr(dev->kvm, type, false, &v);
		mutex_unlock(&dev->kvm->lock);
		if (r)
			return r;

		if (copy_to_user(uaddr, &v, sizeof(v)))
			return -EFAULT;

		break;
	}

	return r;
}

static int aia_has_attr(struct kvm_device *dev, struct kvm_device_attr *attr)
{
	int nr_vcpus;

	switch (attr->group) {
	case KVM_DEV_RISCV_AIA_GRP_CONFIG:
		switch (attr->attr) {
		case KVM_DEV_RISCV_AIA_CONFIG_MODE:
		case KVM_DEV_RISCV_AIA_CONFIG_IDS:
		case KVM_DEV_RISCV_AIA_CONFIG_SRCS:
		case KVM_DEV_RISCV_AIA_CONFIG_GROUP_BITS:
		case KVM_DEV_RISCV_AIA_CONFIG_GROUP_SHIFT:
		case KVM_DEV_RISCV_AIA_CONFIG_HART_BITS:
		case KVM_DEV_RISCV_AIA_CONFIG_GUEST_BITS:
			return 0;
		}
		break;
	case KVM_DEV_RISCV_AIA_GRP_ADDR:
		nr_vcpus = atomic_read(&dev->kvm->online_vcpus);
		if (attr->attr == KVM_DEV_RISCV_AIA_ADDR_APLIC)
			return 0;
		else if (attr->attr < KVM_DEV_RISCV_AIA_ADDR_IMSIC(nr_vcpus))
			return 0;
		break;
	case KVM_DEV_RISCV_AIA_GRP_CTRL:
		switch (attr->attr) {
		case KVM_DEV_RISCV_AIA_CTRL_INIT:
			return 0;
		}
		break;
	case KVM_DEV_RISCV_AIA_GRP_APLIC:
		return kvm_riscv_aia_aplic_has_attr(dev->kvm, attr->attr);
	case KVM_DEV_RISCV_AIA_GRP_IMSIC:
		return kvm_riscv_aia_imsic_has_attr(dev->kvm, attr->attr);
	}

	return -ENXIO;
}

struct kvm_device_ops kvm_riscv_aia_device_ops = {
	.name = "kvm-riscv-aia",
	.create = aia_create,
	.destroy = aia_destroy,
	.set_attr = aia_set_attr,
	.get_attr = aia_get_attr,
	.has_attr = aia_has_attr,
};

int kvm_riscv_vcpu_aia_update(struct kvm_vcpu *vcpu)
{
	/* Proceed only if AIA was initialized successfully */
	if (!kvm_riscv_aia_initialized(vcpu->kvm))
		return 1;

	/* Update the IMSIC HW state before entering guest mode */
	return kvm_riscv_vcpu_aia_imsic_update(vcpu);
}

void kvm_riscv_vcpu_aia_reset(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_aia_csr *csr = &vcpu->arch.aia_context.guest_csr;
	struct kvm_vcpu_aia_csr *reset_csr =
				&vcpu->arch.aia_context.guest_reset_csr;

	if (!kvm_riscv_aia_available())
		return;
	memcpy(csr, reset_csr, sizeof(*csr));

	/* Proceed only if AIA was initialized successfully */
	if (!kvm_riscv_aia_initialized(vcpu->kvm))
		return;

	/* Reset the IMSIC context */
	kvm_riscv_vcpu_aia_imsic_reset(vcpu);
}

int kvm_riscv_vcpu_aia_init(struct kvm_vcpu *vcpu)
{
	struct kvm_vcpu_aia *vaia = &vcpu->arch.aia_context;

	if (!kvm_riscv_aia_available())
		return 0;

	/*
	 * We don't do any memory allocations over here because these
	 * will be done after AIA device is initialized by the user-space.
	 *
	 * Refer, aia_init() implementation for more details.
	 */

	/* Initialize default values in AIA vcpu context */
	vaia->imsic_addr = KVM_RISCV_AIA_UNDEF_ADDR;
	vaia->hart_index = vcpu->vcpu_idx;

	return 0;
}

void kvm_riscv_vcpu_aia_deinit(struct kvm_vcpu *vcpu)
{
	/* Proceed only if AIA was initialized successfully */
	if (!kvm_riscv_aia_initialized(vcpu->kvm))
		return;

	/* Cleanup IMSIC context */
	kvm_riscv_vcpu_aia_imsic_cleanup(vcpu);
}

int kvm_riscv_aia_inject_msi_by_id(struct kvm *kvm, u32 hart_index,
				   u32 guest_index, u32 iid)
{
	unsigned long idx;
	struct kvm_vcpu *vcpu;

	/* Proceed only if AIA was initialized successfully */
	if (!kvm_riscv_aia_initialized(kvm))
		return -EBUSY;

	/* Inject MSI to matching VCPU */
	kvm_for_each_vcpu(idx, vcpu, kvm) {
		if (vcpu->arch.aia_context.hart_index == hart_index)
			return kvm_riscv_vcpu_aia_imsic_inject(vcpu,
							       guest_index,
							       0, iid);
	}

	return 0;
}

int kvm_riscv_aia_inject_msi(struct kvm *kvm, struct kvm_msi *msi)
{
	gpa_t tppn, ippn;
	unsigned long idx;
	struct kvm_vcpu *vcpu;
	u32 g, toff, iid = msi->data;
	struct kvm_aia *aia = &kvm->arch.aia;
	gpa_t target = (((gpa_t)msi->address_hi) << 32) | msi->address_lo;

	/* Proceed only if AIA was initialized successfully */
	if (!kvm_riscv_aia_initialized(kvm))
		return -EBUSY;

	/* Convert target address to target PPN */
	tppn = target >> IMSIC_MMIO_PAGE_SHIFT;

	/* Extract and clear Guest ID from target PPN */
	g = tppn & (BIT(aia->nr_guest_bits) - 1);
	tppn &= ~((gpa_t)(BIT(aia->nr_guest_bits) - 1));

	/* Inject MSI to matching VCPU */
	kvm_for_each_vcpu(idx, vcpu, kvm) {
		ippn = vcpu->arch.aia_context.imsic_addr >>
					IMSIC_MMIO_PAGE_SHIFT;
		if (ippn == tppn) {
			toff = target & (IMSIC_MMIO_PAGE_SZ - 1);
			return kvm_riscv_vcpu_aia_imsic_inject(vcpu, g,
							       toff, iid);
		}
	}

	return 0;
}

int kvm_riscv_aia_inject_irq(struct kvm *kvm, unsigned int irq, bool level)
{
	/* Proceed only if AIA was initialized successfully */
	if (!kvm_riscv_aia_initialized(kvm))
		return -EBUSY;

	/* Inject interrupt level change in APLIC */
	return kvm_riscv_aia_aplic_inject(kvm, irq, level);
}

void kvm_riscv_aia_init_vm(struct kvm *kvm)
{
	struct kvm_aia *aia = &kvm->arch.aia;

	if (!kvm_riscv_aia_available())
		return;

	/*
	 * We don't do any memory allocations over here because these
	 * will be done after AIA device is initialized by the user-space.
	 *
	 * Refer, aia_init() implementation for more details.
	 */

	/* Initialize default values in AIA global context */
	aia->mode = (kvm_riscv_aia_nr_hgei) ?
		KVM_DEV_RISCV_AIA_MODE_AUTO : KVM_DEV_RISCV_AIA_MODE_EMUL;
	aia->nr_ids = kvm_riscv_aia_max_ids - 1;
	aia->nr_sources = 0;
	aia->nr_group_bits = 0;
	aia->nr_group_shift = KVM_DEV_RISCV_AIA_GROUP_SHIFT_MIN;
	aia->nr_hart_bits = 0;
	aia->nr_guest_bits = 0;
	aia->aplic_addr = KVM_RISCV_AIA_UNDEF_ADDR;
}

void kvm_riscv_aia_destroy_vm(struct kvm *kvm)
{
	/* Proceed only if AIA was initialized successfully */
	if (!kvm_riscv_aia_initialized(kvm))
		return;

	/* Cleanup APLIC context */
	kvm_riscv_aia_aplic_cleanup(kvm);
}
