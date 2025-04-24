// SPDX-License-Identifier: GPL-2.0-only
/*
 * VGIC: KVM DEVICE API
 *
 * Copyright (C) 2015 ARM Ltd.
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */
#include <linux/kvm_host.h>
#include <kvm/arm_vgic.h>
#include <linux/uaccess.h>
#include <asm/kvm_mmu.h>
#include <asm/cputype.h>
#include "vgic.h"

/* common helpers */

int vgic_check_iorange(struct kvm *kvm, phys_addr_t ioaddr,
		       phys_addr_t addr, phys_addr_t alignment,
		       phys_addr_t size)
{
	if (!IS_VGIC_ADDR_UNDEF(ioaddr))
		return -EEXIST;

	if (!IS_ALIGNED(addr, alignment) || !IS_ALIGNED(size, alignment))
		return -EINVAL;

	if (addr + size < addr)
		return -EINVAL;

	if (addr & ~kvm_phys_mask(&kvm->arch.mmu) ||
	    (addr + size) > kvm_phys_size(&kvm->arch.mmu))
		return -E2BIG;

	return 0;
}

static int vgic_check_type(struct kvm *kvm, int type_needed)
{
	if (kvm->arch.vgic.vgic_model != type_needed)
		return -ENODEV;
	else
		return 0;
}

int kvm_set_legacy_vgic_v2_addr(struct kvm *kvm, struct kvm_arm_device_addr *dev_addr)
{
	struct vgic_dist *vgic = &kvm->arch.vgic;
	int r;

	mutex_lock(&kvm->arch.config_lock);
	switch (FIELD_GET(KVM_ARM_DEVICE_TYPE_MASK, dev_addr->id)) {
	case KVM_VGIC_V2_ADDR_TYPE_DIST:
		r = vgic_check_type(kvm, KVM_DEV_TYPE_ARM_VGIC_V2);
		if (!r)
			r = vgic_check_iorange(kvm, vgic->vgic_dist_base, dev_addr->addr,
					       SZ_4K, KVM_VGIC_V2_DIST_SIZE);
		if (!r)
			vgic->vgic_dist_base = dev_addr->addr;
		break;
	case KVM_VGIC_V2_ADDR_TYPE_CPU:
		r = vgic_check_type(kvm, KVM_DEV_TYPE_ARM_VGIC_V2);
		if (!r)
			r = vgic_check_iorange(kvm, vgic->vgic_cpu_base, dev_addr->addr,
					       SZ_4K, KVM_VGIC_V2_CPU_SIZE);
		if (!r)
			vgic->vgic_cpu_base = dev_addr->addr;
		break;
	default:
		r = -ENODEV;
	}

	mutex_unlock(&kvm->arch.config_lock);

	return r;
}

/**
 * kvm_vgic_addr - set or get vgic VM base addresses
 * @kvm:   pointer to the vm struct
 * @attr:  pointer to the attribute being retrieved/updated
 * @write: if true set the address in the VM address space, if false read the
 *          address
 *
 * Set or get the vgic base addresses for the distributor and the virtual CPU
 * interface in the VM physical address space.  These addresses are properties
 * of the emulated core/SoC and therefore user space initially knows this
 * information.
 * Check them for sanity (alignment, double assignment). We can't check for
 * overlapping regions in case of a virtual GICv3 here, since we don't know
 * the number of VCPUs yet, so we defer this check to map_resources().
 */
static int kvm_vgic_addr(struct kvm *kvm, struct kvm_device_attr *attr, bool write)
{
	u64 __user *uaddr = (u64 __user *)attr->addr;
	struct vgic_dist *vgic = &kvm->arch.vgic;
	phys_addr_t *addr_ptr, alignment, size;
	u64 undef_value = VGIC_ADDR_UNDEF;
	u64 addr;
	int r;

	/* Reading a redistributor region addr implies getting the index */
	if (write || attr->attr == KVM_VGIC_V3_ADDR_TYPE_REDIST_REGION)
		if (get_user(addr, uaddr))
			return -EFAULT;

	/*
	 * Since we can't hold config_lock while registering the redistributor
	 * iodevs, take the slots_lock immediately.
	 */
	mutex_lock(&kvm->slots_lock);
	switch (attr->attr) {
	case KVM_VGIC_V2_ADDR_TYPE_DIST:
		r = vgic_check_type(kvm, KVM_DEV_TYPE_ARM_VGIC_V2);
		addr_ptr = &vgic->vgic_dist_base;
		alignment = SZ_4K;
		size = KVM_VGIC_V2_DIST_SIZE;
		break;
	case KVM_VGIC_V2_ADDR_TYPE_CPU:
		r = vgic_check_type(kvm, KVM_DEV_TYPE_ARM_VGIC_V2);
		addr_ptr = &vgic->vgic_cpu_base;
		alignment = SZ_4K;
		size = KVM_VGIC_V2_CPU_SIZE;
		break;
	case KVM_VGIC_V3_ADDR_TYPE_DIST:
		r = vgic_check_type(kvm, KVM_DEV_TYPE_ARM_VGIC_V3);
		addr_ptr = &vgic->vgic_dist_base;
		alignment = SZ_64K;
		size = KVM_VGIC_V3_DIST_SIZE;
		break;
	case KVM_VGIC_V3_ADDR_TYPE_REDIST: {
		struct vgic_redist_region *rdreg;

		r = vgic_check_type(kvm, KVM_DEV_TYPE_ARM_VGIC_V3);
		if (r)
			break;
		if (write) {
			r = vgic_v3_set_redist_base(kvm, 0, addr, 0);
			goto out;
		}
		rdreg = list_first_entry_or_null(&vgic->rd_regions,
						 struct vgic_redist_region, list);
		if (!rdreg)
			addr_ptr = &undef_value;
		else
			addr_ptr = &rdreg->base;
		break;
	}
	case KVM_VGIC_V3_ADDR_TYPE_REDIST_REGION:
	{
		struct vgic_redist_region *rdreg;
		u8 index;

		r = vgic_check_type(kvm, KVM_DEV_TYPE_ARM_VGIC_V3);
		if (r)
			break;

		index = addr & KVM_VGIC_V3_RDIST_INDEX_MASK;

		if (write) {
			gpa_t base = addr & KVM_VGIC_V3_RDIST_BASE_MASK;
			u32 count = FIELD_GET(KVM_VGIC_V3_RDIST_COUNT_MASK, addr);
			u8 flags = FIELD_GET(KVM_VGIC_V3_RDIST_FLAGS_MASK, addr);

			if (!count || flags)
				r = -EINVAL;
			else
				r = vgic_v3_set_redist_base(kvm, index,
							    base, count);
			goto out;
		}

		rdreg = vgic_v3_rdist_region_from_index(kvm, index);
		if (!rdreg) {
			r = -ENOENT;
			goto out;
		}

		addr = index;
		addr |= rdreg->base;
		addr |= (u64)rdreg->count << KVM_VGIC_V3_RDIST_COUNT_SHIFT;
		goto out;
	}
	default:
		r = -ENODEV;
	}

	if (r)
		goto out;

	mutex_lock(&kvm->arch.config_lock);
	if (write) {
		r = vgic_check_iorange(kvm, *addr_ptr, addr, alignment, size);
		if (!r)
			*addr_ptr = addr;
	} else {
		addr = *addr_ptr;
	}
	mutex_unlock(&kvm->arch.config_lock);

out:
	mutex_unlock(&kvm->slots_lock);

	if (!r && !write)
		r =  put_user(addr, uaddr);

	return r;
}

static int vgic_set_common_attr(struct kvm_device *dev,
				struct kvm_device_attr *attr)
{
	int r;

	switch (attr->group) {
	case KVM_DEV_ARM_VGIC_GRP_ADDR:
		r = kvm_vgic_addr(dev->kvm, attr, true);
		return (r == -ENODEV) ? -ENXIO : r;
	case KVM_DEV_ARM_VGIC_GRP_NR_IRQS: {
		u32 __user *uaddr = (u32 __user *)(long)attr->addr;
		u32 val;
		int ret = 0;

		if (get_user(val, uaddr))
			return -EFAULT;

		/*
		 * We require:
		 * - at least 32 SPIs on top of the 16 SGIs and 16 PPIs
		 * - at most 1024 interrupts
		 * - a multiple of 32 interrupts
		 */
		if (val < (VGIC_NR_PRIVATE_IRQS + 32) ||
		    val > VGIC_MAX_RESERVED ||
		    (val & 31))
			return -EINVAL;

		mutex_lock(&dev->kvm->arch.config_lock);

		/*
		 * Either userspace has already configured NR_IRQS or
		 * the vgic has already been initialized and vgic_init()
		 * supplied a default amount of SPIs.
		 */
		if (dev->kvm->arch.vgic.nr_spis)
			ret = -EBUSY;
		else
			dev->kvm->arch.vgic.nr_spis =
				val - VGIC_NR_PRIVATE_IRQS;

		mutex_unlock(&dev->kvm->arch.config_lock);

		return ret;
	}
	case KVM_DEV_ARM_VGIC_GRP_CTRL: {
		switch (attr->attr) {
		case KVM_DEV_ARM_VGIC_CTRL_INIT:
			mutex_lock(&dev->kvm->arch.config_lock);
			r = vgic_init(dev->kvm);
			mutex_unlock(&dev->kvm->arch.config_lock);
			return r;
		case KVM_DEV_ARM_VGIC_SAVE_PENDING_TABLES:
			/*
			 * OK, this one isn't common at all, but we
			 * want to handle all control group attributes
			 * in a single place.
			 */
			if (vgic_check_type(dev->kvm, KVM_DEV_TYPE_ARM_VGIC_V3))
				return -ENXIO;
			mutex_lock(&dev->kvm->lock);

			if (!lock_all_vcpus(dev->kvm)) {
				mutex_unlock(&dev->kvm->lock);
				return -EBUSY;
			}

			mutex_lock(&dev->kvm->arch.config_lock);
			r = vgic_v3_save_pending_tables(dev->kvm);
			mutex_unlock(&dev->kvm->arch.config_lock);
			unlock_all_vcpus(dev->kvm);
			mutex_unlock(&dev->kvm->lock);
			return r;
		}
		break;
	}
	}

	return -ENXIO;
}

static int vgic_get_common_attr(struct kvm_device *dev,
				struct kvm_device_attr *attr)
{
	int r = -ENXIO;

	switch (attr->group) {
	case KVM_DEV_ARM_VGIC_GRP_ADDR:
		r = kvm_vgic_addr(dev->kvm, attr, false);
		return (r == -ENODEV) ? -ENXIO : r;
	case KVM_DEV_ARM_VGIC_GRP_NR_IRQS: {
		u32 __user *uaddr = (u32 __user *)(long)attr->addr;

		r = put_user(dev->kvm->arch.vgic.nr_spis +
			     VGIC_NR_PRIVATE_IRQS, uaddr);
		break;
	}
	case KVM_DEV_ARM_VGIC_GRP_MAINT_IRQ: {
		u32 __user *uaddr = (u32 __user *)(long)attr->addr;

		r = put_user(dev->kvm->arch.vgic.mi_intid, uaddr);
		break;
	}
	}

	return r;
}

static int vgic_create(struct kvm_device *dev, u32 type)
{
	return kvm_vgic_create(dev->kvm, type);
}

static void vgic_destroy(struct kvm_device *dev)
{
	kfree(dev);
}

int kvm_register_vgic_device(unsigned long type)
{
	int ret = -ENODEV;

	switch (type) {
	case KVM_DEV_TYPE_ARM_VGIC_V2:
		ret = kvm_register_device_ops(&kvm_arm_vgic_v2_ops,
					      KVM_DEV_TYPE_ARM_VGIC_V2);
		break;
	case KVM_DEV_TYPE_ARM_VGIC_V3:
		ret = kvm_register_device_ops(&kvm_arm_vgic_v3_ops,
					      KVM_DEV_TYPE_ARM_VGIC_V3);

		if (ret)
			break;
		ret = kvm_vgic_register_its_device();
		break;
	}

	return ret;
}

int vgic_v2_parse_attr(struct kvm_device *dev, struct kvm_device_attr *attr,
		       struct vgic_reg_attr *reg_attr)
{
	int cpuid = FIELD_GET(KVM_DEV_ARM_VGIC_CPUID_MASK, attr->attr);

	reg_attr->addr = attr->attr & KVM_DEV_ARM_VGIC_OFFSET_MASK;
	reg_attr->vcpu = kvm_get_vcpu_by_id(dev->kvm, cpuid);
	if (!reg_attr->vcpu)
		return -EINVAL;

	return 0;
}

/**
 * vgic_v2_attr_regs_access - allows user space to access VGIC v2 state
 *
 * @dev:      kvm device handle
 * @attr:     kvm device attribute
 * @is_write: true if userspace is writing a register
 */
static int vgic_v2_attr_regs_access(struct kvm_device *dev,
				    struct kvm_device_attr *attr,
				    bool is_write)
{
	u32 __user *uaddr = (u32 __user *)(unsigned long)attr->addr;
	struct vgic_reg_attr reg_attr;
	gpa_t addr;
	struct kvm_vcpu *vcpu;
	int ret;
	u32 val;

	ret = vgic_v2_parse_attr(dev, attr, &reg_attr);
	if (ret)
		return ret;

	vcpu = reg_attr.vcpu;
	addr = reg_attr.addr;

	if (is_write)
		if (get_user(val, uaddr))
			return -EFAULT;

	mutex_lock(&dev->kvm->lock);

	if (!lock_all_vcpus(dev->kvm)) {
		mutex_unlock(&dev->kvm->lock);
		return -EBUSY;
	}

	mutex_lock(&dev->kvm->arch.config_lock);

	ret = vgic_init(dev->kvm);
	if (ret)
		goto out;

	switch (attr->group) {
	case KVM_DEV_ARM_VGIC_GRP_CPU_REGS:
		ret = vgic_v2_cpuif_uaccess(vcpu, is_write, addr, &val);
		break;
	case KVM_DEV_ARM_VGIC_GRP_DIST_REGS:
		ret = vgic_v2_dist_uaccess(vcpu, is_write, addr, &val);
		break;
	default:
		ret = -EINVAL;
		break;
	}

out:
	mutex_unlock(&dev->kvm->arch.config_lock);
	unlock_all_vcpus(dev->kvm);
	mutex_unlock(&dev->kvm->lock);

	if (!ret && !is_write)
		ret = put_user(val, uaddr);

	return ret;
}

static int vgic_v2_set_attr(struct kvm_device *dev,
			    struct kvm_device_attr *attr)
{
	switch (attr->group) {
	case KVM_DEV_ARM_VGIC_GRP_DIST_REGS:
	case KVM_DEV_ARM_VGIC_GRP_CPU_REGS:
		return vgic_v2_attr_regs_access(dev, attr, true);
	default:
		return vgic_set_common_attr(dev, attr);
	}
}

static int vgic_v2_get_attr(struct kvm_device *dev,
			    struct kvm_device_attr *attr)
{
	switch (attr->group) {
	case KVM_DEV_ARM_VGIC_GRP_DIST_REGS:
	case KVM_DEV_ARM_VGIC_GRP_CPU_REGS:
		return vgic_v2_attr_regs_access(dev, attr, false);
	default:
		return vgic_get_common_attr(dev, attr);
	}
}

static int vgic_v2_has_attr(struct kvm_device *dev,
			    struct kvm_device_attr *attr)
{
	switch (attr->group) {
	case KVM_DEV_ARM_VGIC_GRP_ADDR:
		switch (attr->attr) {
		case KVM_VGIC_V2_ADDR_TYPE_DIST:
		case KVM_VGIC_V2_ADDR_TYPE_CPU:
			return 0;
		}
		break;
	case KVM_DEV_ARM_VGIC_GRP_DIST_REGS:
	case KVM_DEV_ARM_VGIC_GRP_CPU_REGS:
		return vgic_v2_has_attr_regs(dev, attr);
	case KVM_DEV_ARM_VGIC_GRP_NR_IRQS:
		return 0;
	case KVM_DEV_ARM_VGIC_GRP_CTRL:
		switch (attr->attr) {
		case KVM_DEV_ARM_VGIC_CTRL_INIT:
			return 0;
		}
	}
	return -ENXIO;
}

struct kvm_device_ops kvm_arm_vgic_v2_ops = {
	.name = "kvm-arm-vgic-v2",
	.create = vgic_create,
	.destroy = vgic_destroy,
	.set_attr = vgic_v2_set_attr,
	.get_attr = vgic_v2_get_attr,
	.has_attr = vgic_v2_has_attr,
};

int vgic_v3_parse_attr(struct kvm_device *dev, struct kvm_device_attr *attr,
		       struct vgic_reg_attr *reg_attr)
{
	unsigned long vgic_mpidr, mpidr_reg;

	/*
	 * For KVM_DEV_ARM_VGIC_GRP_DIST_REGS group,
	 * attr might not hold MPIDR. Hence assume vcpu0.
	 */
	if (attr->group != KVM_DEV_ARM_VGIC_GRP_DIST_REGS) {
		vgic_mpidr = (attr->attr & KVM_DEV_ARM_VGIC_V3_MPIDR_MASK) >>
			      KVM_DEV_ARM_VGIC_V3_MPIDR_SHIFT;

		mpidr_reg = VGIC_TO_MPIDR(vgic_mpidr);
		reg_attr->vcpu = kvm_mpidr_to_vcpu(dev->kvm, mpidr_reg);
	} else {
		reg_attr->vcpu = kvm_get_vcpu(dev->kvm, 0);
	}

	if (!reg_attr->vcpu)
		return -EINVAL;

	reg_attr->addr = attr->attr & KVM_DEV_ARM_VGIC_OFFSET_MASK;

	return 0;
}

/*
 * vgic_v3_attr_regs_access - allows user space to access VGIC v3 state
 *
 * @dev:      kvm device handle
 * @attr:     kvm device attribute
 * @is_write: true if userspace is writing a register
 */
static int vgic_v3_attr_regs_access(struct kvm_device *dev,
				    struct kvm_device_attr *attr,
				    bool is_write)
{
	struct vgic_reg_attr reg_attr;
	gpa_t addr;
	struct kvm_vcpu *vcpu;
	bool uaccess, post_init = true;
	u32 val;
	int ret;

	ret = vgic_v3_parse_attr(dev, attr, &reg_attr);
	if (ret)
		return ret;

	vcpu = reg_attr.vcpu;
	addr = reg_attr.addr;

	switch (attr->group) {
	case KVM_DEV_ARM_VGIC_GRP_CPU_SYSREGS:
		/* Sysregs uaccess is performed by the sysreg handling code */
		uaccess = false;
		break;
	case KVM_DEV_ARM_VGIC_GRP_MAINT_IRQ:
		post_init = false;
		fallthrough;
	default:
		uaccess = true;
	}

	if (uaccess && is_write) {
		u32 __user *uaddr = (u32 __user *)(unsigned long)attr->addr;
		if (get_user(val, uaddr))
			return -EFAULT;
	}

	mutex_lock(&dev->kvm->lock);

	if (!lock_all_vcpus(dev->kvm)) {
		mutex_unlock(&dev->kvm->lock);
		return -EBUSY;
	}

	mutex_lock(&dev->kvm->arch.config_lock);

	if (post_init != vgic_initialized(dev->kvm)) {
		ret = -EBUSY;
		goto out;
	}

	switch (attr->group) {
	case KVM_DEV_ARM_VGIC_GRP_DIST_REGS:
		ret = vgic_v3_dist_uaccess(vcpu, is_write, addr, &val);
		break;
	case KVM_DEV_ARM_VGIC_GRP_REDIST_REGS:
		ret = vgic_v3_redist_uaccess(vcpu, is_write, addr, &val);
		break;
	case KVM_DEV_ARM_VGIC_GRP_CPU_SYSREGS:
		ret = vgic_v3_cpu_sysregs_uaccess(vcpu, attr, is_write);
		break;
	case KVM_DEV_ARM_VGIC_GRP_LEVEL_INFO: {
		unsigned int info, intid;

		info = (attr->attr & KVM_DEV_ARM_VGIC_LINE_LEVEL_INFO_MASK) >>
			KVM_DEV_ARM_VGIC_LINE_LEVEL_INFO_SHIFT;
		if (info == VGIC_LEVEL_INFO_LINE_LEVEL) {
			intid = attr->attr &
				KVM_DEV_ARM_VGIC_LINE_LEVEL_INTID_MASK;
			ret = vgic_v3_line_level_info_uaccess(vcpu, is_write,
							      intid, &val);
		} else {
			ret = -EINVAL;
		}
		break;
	}
	case KVM_DEV_ARM_VGIC_GRP_MAINT_IRQ:
		if (!is_write) {
			val = dev->kvm->arch.vgic.mi_intid;
			ret = 0;
			break;
		}

		ret = -EINVAL;
		if ((val < VGIC_NR_PRIVATE_IRQS) && (val >= VGIC_NR_SGIS)) {
			dev->kvm->arch.vgic.mi_intid = val;
			ret = 0;
		}
		break;
	default:
		ret = -EINVAL;
		break;
	}

out:
	mutex_unlock(&dev->kvm->arch.config_lock);
	unlock_all_vcpus(dev->kvm);
	mutex_unlock(&dev->kvm->lock);

	if (!ret && uaccess && !is_write) {
		u32 __user *uaddr = (u32 __user *)(unsigned long)attr->addr;
		ret = put_user(val, uaddr);
	}

	return ret;
}

static int vgic_v3_set_attr(struct kvm_device *dev,
			    struct kvm_device_attr *attr)
{
	switch (attr->group) {
	case KVM_DEV_ARM_VGIC_GRP_DIST_REGS:
	case KVM_DEV_ARM_VGIC_GRP_REDIST_REGS:
	case KVM_DEV_ARM_VGIC_GRP_CPU_SYSREGS:
	case KVM_DEV_ARM_VGIC_GRP_LEVEL_INFO:
	case KVM_DEV_ARM_VGIC_GRP_MAINT_IRQ:
		return vgic_v3_attr_regs_access(dev, attr, true);
	default:
		return vgic_set_common_attr(dev, attr);
	}
}

static int vgic_v3_get_attr(struct kvm_device *dev,
			    struct kvm_device_attr *attr)
{
	switch (attr->group) {
	case KVM_DEV_ARM_VGIC_GRP_DIST_REGS:
	case KVM_DEV_ARM_VGIC_GRP_REDIST_REGS:
	case KVM_DEV_ARM_VGIC_GRP_CPU_SYSREGS:
	case KVM_DEV_ARM_VGIC_GRP_LEVEL_INFO:
	case KVM_DEV_ARM_VGIC_GRP_MAINT_IRQ:
		return vgic_v3_attr_regs_access(dev, attr, false);
	default:
		return vgic_get_common_attr(dev, attr);
	}
}

static int vgic_v3_has_attr(struct kvm_device *dev,
			    struct kvm_device_attr *attr)
{
	switch (attr->group) {
	case KVM_DEV_ARM_VGIC_GRP_ADDR:
		switch (attr->attr) {
		case KVM_VGIC_V3_ADDR_TYPE_DIST:
		case KVM_VGIC_V3_ADDR_TYPE_REDIST:
		case KVM_VGIC_V3_ADDR_TYPE_REDIST_REGION:
			return 0;
		}
		break;
	case KVM_DEV_ARM_VGIC_GRP_DIST_REGS:
	case KVM_DEV_ARM_VGIC_GRP_REDIST_REGS:
	case KVM_DEV_ARM_VGIC_GRP_CPU_SYSREGS:
		return vgic_v3_has_attr_regs(dev, attr);
	case KVM_DEV_ARM_VGIC_GRP_NR_IRQS:
	case KVM_DEV_ARM_VGIC_GRP_MAINT_IRQ:
		return 0;
	case KVM_DEV_ARM_VGIC_GRP_LEVEL_INFO: {
		if (((attr->attr & KVM_DEV_ARM_VGIC_LINE_LEVEL_INFO_MASK) >>
		      KVM_DEV_ARM_VGIC_LINE_LEVEL_INFO_SHIFT) ==
		      VGIC_LEVEL_INFO_LINE_LEVEL)
			return 0;
		break;
	}
	case KVM_DEV_ARM_VGIC_GRP_CTRL:
		switch (attr->attr) {
		case KVM_DEV_ARM_VGIC_CTRL_INIT:
			return 0;
		case KVM_DEV_ARM_VGIC_SAVE_PENDING_TABLES:
			return 0;
		}
	}
	return -ENXIO;
}

struct kvm_device_ops kvm_arm_vgic_v3_ops = {
	.name = "kvm-arm-vgic-v3",
	.create = vgic_create,
	.destroy = vgic_destroy,
	.set_attr = vgic_v3_set_attr,
	.get_attr = vgic_v3_get_attr,
	.has_attr = vgic_v3_has_attr,
};
