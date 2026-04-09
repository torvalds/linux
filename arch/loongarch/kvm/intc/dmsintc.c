// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 Loongson Technology Corporation Limited
 */

#include <linux/kvm_host.h>
#include <asm/kvm_dmsintc.h>
#include <asm/kvm_vcpu.h>

static int kvm_dmsintc_ctrl_access(struct kvm_device *dev,
				   struct kvm_device_attr *attr, bool is_write)
{
	int addr = attr->attr;
	unsigned long cpu_bit, val;
	void __user *data = (void __user *)attr->addr;
	struct loongarch_dmsintc *s = dev->kvm->arch.dmsintc;

	switch (addr) {
	case KVM_DEV_LOONGARCH_DMSINTC_MSG_ADDR_BASE:
		if (is_write) {
			if (copy_from_user(&val, data, sizeof(s->msg_addr_base)))
				return -EFAULT;
			if (s->msg_addr_base)
				return -EFAULT; /* Duplicate setting are not allowed. */
			if ((val & (BIT(AVEC_CPU_SHIFT) - 1)) != 0)
				return -EINVAL;
			s->msg_addr_base = val;
			cpu_bit = find_first_bit((unsigned long *)&(s->msg_addr_base), 64) - AVEC_CPU_SHIFT;
			cpu_bit = min(cpu_bit, AVEC_CPU_BIT);
			s->cpu_mask = GENMASK(cpu_bit - 1, 0) & AVEC_CPU_MASK;
		}
		break;
	case KVM_DEV_LOONGARCH_DMSINTC_MSG_ADDR_SIZE:
		if (is_write) {
			if (copy_from_user(&val, data, sizeof(s->msg_addr_size)))
				return -EFAULT;
			if (s->msg_addr_size)
				return -EFAULT; /*Duplicate setting are not allowed. */
			s->msg_addr_size = val;
		}
		break;
	default:
		kvm_err("%s: unknown dmsintc register, addr = %d\n", __func__, addr);
		return -ENXIO;
	}

	return 0;
}

static int kvm_dmsintc_set_attr(struct kvm_device *dev,
				struct kvm_device_attr *attr)
{
	switch (attr->group) {
	case KVM_DEV_LOONGARCH_DMSINTC_GRP_CTRL:
		return kvm_dmsintc_ctrl_access(dev, attr, true);
	default:
		kvm_err("%s: unknown group (%d)\n", __func__, attr->group);
		return -EINVAL;
	}
}

static int kvm_dmsintc_create(struct kvm_device *dev, u32 type)
{
	struct kvm *kvm;
	struct loongarch_dmsintc *s;

	if (!dev) {
		kvm_err("%s: kvm_device ptr is invalid!\n", __func__);
		return -EINVAL;
	}

	kvm = dev->kvm;
	if (kvm->arch.dmsintc) {
		kvm_err("%s: LoongArch DMSINTC has already been created!\n", __func__);
		return -EINVAL;
	}

	s = kzalloc(sizeof(struct loongarch_dmsintc), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	s->kvm = kvm;
	kvm->arch.dmsintc = s;

	return 0;
}

static void kvm_dmsintc_destroy(struct kvm_device *dev)
{

	if (!dev || !dev->kvm || !dev->kvm->arch.dmsintc)
		return;

	kfree(dev->kvm->arch.dmsintc);
	kfree(dev);
}

static struct kvm_device_ops kvm_dmsintc_dev_ops = {
	.name = "kvm-loongarch-dmsintc",
	.create = kvm_dmsintc_create,
	.destroy = kvm_dmsintc_destroy,
	.set_attr = kvm_dmsintc_set_attr,
};

int kvm_loongarch_register_dmsintc_device(void)
{
	return kvm_register_device_ops(&kvm_dmsintc_dev_ops, KVM_DEV_TYPE_LOONGARCH_DMSINTC);
}
