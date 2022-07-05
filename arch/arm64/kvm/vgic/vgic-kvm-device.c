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

	if (addr & ~kvm_phys_mask(kvm) || addr + size > kvm_phys_size(kvm))
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

/**
 * kvm_vgic_addr - set or get vgic VM base addresses
 * @kvm:   pointer to the vm struct
 * @type:  the VGIC addr type, one of KVM_VGIC_V[23]_ADDR_TYPE_XXX
 * @addr:  pointer to address value
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
int kvm_vgic_addr(struct kvm *kvm, unsigned long type, u64 *addr, bool write)
{
	int r = 0;
	struct vgic_dist *vgic = &kvm->arch.vgic;
	phys_addr_t *addr_ptr, alignment, size;
	u64 undef_value = VGIC_ADDR_UNDEF;

	mutex_lock(&kvm->lock);
	switch (type) {
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
			r = vgic_v3_set_redist_base(kvm, 0, *addr, 0);
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

		index = *addr & KVM_VGIC_V3_RDIST_INDEX_MASK;

		if (write) {
			gpa_t base = *addr & KVM_VGIC_V3_RDIST_BASE_MASK;
			u32 count = (*addr & KVM_VGIC_V3_RDIST_COUNT_MASK)
					>> KVM_VGIC_V3_RDIST_COUNT_SHIFT;
			u8 flags = (*addr & KVM_VGIC_V3_RDIST_FLAGS_MASK)
					>> KVM_VGIC_V3_RDIST_FLAGS_SHIFT;

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

		*addr = index;
		*addr |= rdreg->base;
		*addr |= (u64)rdreg->count << KVM_VGIC_V3_RDIST_COUNT_SHIFT;
		goto out;
	}
	default:
		r = -ENODEV;
	}

	if (r)
		goto out;

	if (write) {
		r = vgic_check_iorange(kvm, *addr_ptr, *addr, alignment, size);
		if (!r)
			*addr_ptr = *addr;
	} else {
		*addr = *addr_ptr;
	}

out:
	mutex_unlock(&kvm->lock);
	return r;
}

static int vgic_set_common_attr(struct kvm_device *dev,
				struct kvm_device_attr *attr)
{
	int r;

	switch (attr->group) {
	case KVM_DEV_ARM_VGIC_GRP_ADDR: {
		u64 __user *uaddr = (u64 __user *)(long)attr->addr;
		u64 addr;
		unsigned long type = (unsigned long)attr->attr;

		if (copy_from_user(&addr, uaddr, sizeof(addr)))
			return -EFAULT;

		r = kvm_vgic_addr(dev->kvm, type, &addr, true);
		return (r == -ENODEV) ? -ENXIO : r;
	}
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

		mutex_lock(&dev->kvm->lock);

		if (vgic_ready(dev->kvm) || dev->kvm->arch.vgic.nr_spis)
			ret = -EBUSY;
		else
			dev->kvm->arch.vgic.nr_spis =
				val - VGIC_NR_PRIVATE_IRQS;

		mutex_unlock(&dev->kvm->lock);

		return ret;
	}
	case KVM_DEV_ARM_VGIC_GRP_CTRL: {
		switch (attr->attr) {
		case KVM_DEV_ARM_VGIC_CTRL_INIT:
			mutex_lock(&dev->kvm->lock);
			r = vgic_init(dev->kvm);
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
	case KVM_DEV_ARM_VGIC_GRP_ADDR: {
		u64 __user *uaddr = (u64 __user *)(long)attr->addr;
		u64 addr;
		unsigned long type = (unsigned long)attr->attr;

		if (copy_from_user(&addr, uaddr, sizeof(addr)))
			return -EFAULT;

		r = kvm_vgic_addr(dev->kvm, type, &addr, false);
		if (r)
			return (r == -ENODEV) ? -ENXIO : r;

		if (copy_to_user(uaddr, &addr, sizeof(addr)))
			return -EFAULT;
		break;
	}
	case KVM_DEV_ARM_VGIC_GRP_NR_IRQS: {
		u32 __user *uaddr = (u32 __user *)(long)attr->addr;

		r = put_user(dev->kvm->arch.vgic.nr_spis +
			     VGIC_NR_PRIVATE_IRQS, uaddr);
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
	int cpuid;

	cpuid = (attr->attr & KVM_DEV_ARM_VGIC_CPUID_MASK) >>
		 KVM_DEV_ARM_VGIC_CPUID_SHIFT;

	if (cpuid >= atomic_read(&dev->kvm->online_vcpus))
		return -EINVAL;

	reg_attr->vcpu = kvm_get_vcpu(dev->kvm, cpuid);
	reg_attr->addr = attr->attr & KVM_DEV_ARM_VGIC_OFFSET_MASK;

	return 0;
}

/* unlocks vcpus from @vcpu_lock_idx and smaller */
static void unlock_vcpus(struct kvm *kvm, int vcpu_lock_idx)
{
	struct kvm_vcpu *tmp_vcpu;

	for (; vcpu_lock_idx >= 0; vcpu_lock_idx--) {
		tmp_vcpu = kvm_get_vcpu(kvm, vcpu_lock_idx);
		mutex_unlock(&tmp_vcpu->mutex);
	}
}

void unlock_all_vcpus(struct kvm *kvm)
{
	unlock_vcpus(kvm, atomic_read(&kvm->online_vcpus) - 1);
}

/* Returns true if all vcpus were locked, false otherwise */
bool lock_all_vcpus(struct kvm *kvm)
{
	struct kvm_vcpu *tmp_vcpu;
	unsigned long c;

	/*
	 * Any time a vcpu is run, vcpu_load is called which tries to grab the
	 * vcpu->mutex.  By grabbing the vcpu->mutex of all VCPUs we ensure
	 * that no other VCPUs are run and fiddle with the vgic state while we
	 * access it.
	 */
	kvm_for_each_vcpu(c, tmp_vcpu, kvm) {
		if (!mutex_trylock(&tmp_vcpu->mutex)) {
			unlock_vcpus(kvm, c - 1);
			return false;
		}
	}

	return true;
}

/**
 * vgic_v2_attr_regs_access - allows user space to access VGIC v2 state
 *
 * @dev:      kvm device handle
 * @attr:     kvm device attribute
 * @reg:      address the value is read or written
 * @is_write: true if userspace is writing a register
 */
static int vgic_v2_attr_regs_access(struct kvm_device *dev,
				    struct kvm_device_attr *attr,
				    u32 *reg, bool is_write)
{
	struct vgic_reg_attr reg_attr;
	gpa_t addr;
	struct kvm_vcpu *vcpu;
	int ret;

	ret = vgic_v2_parse_attr(dev, attr, &reg_attr);
	if (ret)
		return ret;

	vcpu = reg_attr.vcpu;
	addr = reg_attr.addr;

	mutex_lock(&dev->kvm->lock);

	ret = vgic_init(dev->kvm);
	if (ret)
		goto out;

	if (!lock_all_vcpus(dev->kvm)) {
		ret = -EBUSY;
		goto out;
	}

	switch (attr->group) {
	case KVM_DEV_ARM_VGIC_GRP_CPU_REGS:
		ret = vgic_v2_cpuif_uaccess(vcpu, is_write, addr, reg);
		break;
	case KVM_DEV_ARM_VGIC_GRP_DIST_REGS:
		ret = vgic_v2_dist_uaccess(vcpu, is_write, addr, reg);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	unlock_all_vcpus(dev->kvm);
out:
	mutex_unlock(&dev->kvm->lock);
	return ret;
}

static int vgic_v2_set_attr(struct kvm_device *dev,
			    struct kvm_device_attr *attr)
{
	int ret;

	ret = vgic_set_common_attr(dev, attr);
	if (ret != -ENXIO)
		return ret;

	switch (attr->group) {
	case KVM_DEV_ARM_VGIC_GRP_DIST_REGS:
	case KVM_DEV_ARM_VGIC_GRP_CPU_REGS: {
		u32 __user *uaddr = (u32 __user *)(long)attr->addr;
		u32 reg;

		if (get_user(reg, uaddr))
			return -EFAULT;

		return vgic_v2_attr_regs_access(dev, attr, &reg, true);
	}
	}

	return -ENXIO;
}

static int vgic_v2_get_attr(struct kvm_device *dev,
			    struct kvm_device_attr *attr)
{
	int ret;

	ret = vgic_get_common_attr(dev, attr);
	if (ret != -ENXIO)
		return ret;

	switch (attr->group) {
	case KVM_DEV_ARM_VGIC_GRP_DIST_REGS:
	case KVM_DEV_ARM_VGIC_GRP_CPU_REGS: {
		u32 __user *uaddr = (u32 __user *)(long)attr->addr;
		u32 reg = 0;

		ret = vgic_v2_attr_regs_access(dev, attr, &reg, false);
		if (ret)
			return ret;
		return put_user(reg, uaddr);
	}
	}

	return -ENXIO;
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
 * @reg:      address the value is read or written, NULL for sysregs
 * @is_write: true if userspace is writing a register
 */
static int vgic_v3_attr_regs_access(struct kvm_device *dev,
				    struct kvm_device_attr *attr,
				    u64 *reg, bool is_write)
{
	struct vgic_reg_attr reg_attr;
	gpa_t addr;
	struct kvm_vcpu *vcpu;
	int ret;
	u32 tmp32;

	ret = vgic_v3_parse_attr(dev, attr, &reg_attr);
	if (ret)
		return ret;

	vcpu = reg_attr.vcpu;
	addr = reg_attr.addr;

	mutex_lock(&dev->kvm->lock);

	if (unlikely(!vgic_initialized(dev->kvm))) {
		ret = -EBUSY;
		goto out;
	}

	if (!lock_all_vcpus(dev->kvm)) {
		ret = -EBUSY;
		goto out;
	}

	switch (attr->group) {
	case KVM_DEV_ARM_VGIC_GRP_DIST_REGS:
		if (is_write)
			tmp32 = *reg;

		ret = vgic_v3_dist_uaccess(vcpu, is_write, addr, &tmp32);
		if (!is_write)
			*reg = tmp32;
		break;
	case KVM_DEV_ARM_VGIC_GRP_REDIST_REGS:
		if (is_write)
			tmp32 = *reg;

		ret = vgic_v3_redist_uaccess(vcpu, is_write, addr, &tmp32);
		if (!is_write)
			*reg = tmp32;
		break;
	case KVM_DEV_ARM_VGIC_GRP_CPU_SYSREGS:
		ret = vgic_v3_cpu_sysregs_uaccess(vcpu, attr, is_write);
		break;
	case KVM_DEV_ARM_VGIC_GRP_LEVEL_INFO: {
		unsigned int info, intid;

		info = (attr->attr & KVM_DEV_ARM_VGIC_LINE_LEVEL_INFO_MASK) >>
			KVM_DEV_ARM_VGIC_LINE_LEVEL_INFO_SHIFT;
		if (info == VGIC_LEVEL_INFO_LINE_LEVEL) {
			if (is_write)
				tmp32 = *reg;
			intid = attr->attr &
				KVM_DEV_ARM_VGIC_LINE_LEVEL_INTID_MASK;
			ret = vgic_v3_line_level_info_uaccess(vcpu, is_write,
							      intid, &tmp32);
			if (!is_write)
				*reg = tmp32;
		} else {
			ret = -EINVAL;
		}
		break;
	}
	default:
		ret = -EINVAL;
		break;
	}

	unlock_all_vcpus(dev->kvm);
out:
	mutex_unlock(&dev->kvm->lock);
	return ret;
}

static int vgic_v3_set_attr(struct kvm_device *dev,
			    struct kvm_device_attr *attr)
{
	int ret;

	ret = vgic_set_common_attr(dev, attr);
	if (ret != -ENXIO)
		return ret;

	switch (attr->group) {
	case KVM_DEV_ARM_VGIC_GRP_DIST_REGS:
	case KVM_DEV_ARM_VGIC_GRP_REDIST_REGS: {
		u32 __user *uaddr = (u32 __user *)(long)attr->addr;
		u32 tmp32;
		u64 reg;

		if (get_user(tmp32, uaddr))
			return -EFAULT;

		reg = tmp32;
		return vgic_v3_attr_regs_access(dev, attr, &reg, true);
	}
	case KVM_DEV_ARM_VGIC_GRP_CPU_SYSREGS:
		return vgic_v3_attr_regs_access(dev, attr, NULL, true);
	case KVM_DEV_ARM_VGIC_GRP_LEVEL_INFO: {
		u32 __user *uaddr = (u32 __user *)(long)attr->addr;
		u64 reg;
		u32 tmp32;

		if (get_user(tmp32, uaddr))
			return -EFAULT;

		reg = tmp32;
		return vgic_v3_attr_regs_access(dev, attr, &reg, true);
	}
	case KVM_DEV_ARM_VGIC_GRP_CTRL: {
		int ret;

		switch (attr->attr) {
		case KVM_DEV_ARM_VGIC_SAVE_PENDING_TABLES:
			mutex_lock(&dev->kvm->lock);

			if (!lock_all_vcpus(dev->kvm)) {
				mutex_unlock(&dev->kvm->lock);
				return -EBUSY;
			}
			ret = vgic_v3_save_pending_tables(dev->kvm);
			unlock_all_vcpus(dev->kvm);
			mutex_unlock(&dev->kvm->lock);
			return ret;
		}
		break;
	}
	}
	return -ENXIO;
}

static int vgic_v3_get_attr(struct kvm_device *dev,
			    struct kvm_device_attr *attr)
{
	int ret;

	ret = vgic_get_common_attr(dev, attr);
	if (ret != -ENXIO)
		return ret;

	switch (attr->group) {
	case KVM_DEV_ARM_VGIC_GRP_DIST_REGS:
	case KVM_DEV_ARM_VGIC_GRP_REDIST_REGS: {
		u32 __user *uaddr = (u32 __user *)(long)attr->addr;
		u64 reg;
		u32 tmp32;

		ret = vgic_v3_attr_regs_access(dev, attr, &reg, false);
		if (ret)
			return ret;
		tmp32 = reg;
		return put_user(tmp32, uaddr);
	}
	case KVM_DEV_ARM_VGIC_GRP_CPU_SYSREGS:
		return vgic_v3_attr_regs_access(dev, attr, NULL, false);
	case KVM_DEV_ARM_VGIC_GRP_LEVEL_INFO: {
		u32 __user *uaddr = (u32 __user *)(long)attr->addr;
		u64 reg;
		u32 tmp32;

		ret = vgic_v3_attr_regs_access(dev, attr, &reg, false);
		if (ret)
			return ret;
		tmp32 = reg;
		return put_user(tmp32, uaddr);
	}
	}
	return -ENXIO;
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
