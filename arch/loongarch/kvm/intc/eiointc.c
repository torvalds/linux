// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Loongson Technology Corporation Limited
 */

#include <asm/kvm_eiointc.h>
#include <asm/kvm_vcpu.h>
#include <linux/count_zeros.h>

static int kvm_eiointc_read(struct kvm_vcpu *vcpu,
			struct kvm_io_device *dev,
			gpa_t addr, int len, void *val)
{
	return 0;
}

static int kvm_eiointc_write(struct kvm_vcpu *vcpu,
			struct kvm_io_device *dev,
			gpa_t addr, int len, const void *val)
{
	return 0;
}

static const struct kvm_io_device_ops kvm_eiointc_ops = {
	.read	= kvm_eiointc_read,
	.write	= kvm_eiointc_write,
};

static int kvm_eiointc_virt_read(struct kvm_vcpu *vcpu,
				struct kvm_io_device *dev,
				gpa_t addr, int len, void *val)
{
	return 0;
}

static int kvm_eiointc_virt_write(struct kvm_vcpu *vcpu,
				struct kvm_io_device *dev,
				gpa_t addr, int len, const void *val)
{
	return 0;
}

static const struct kvm_io_device_ops kvm_eiointc_virt_ops = {
	.read	= kvm_eiointc_virt_read,
	.write	= kvm_eiointc_virt_write,
};

static int kvm_eiointc_get_attr(struct kvm_device *dev,
				struct kvm_device_attr *attr)
{
	return 0;
}

static int kvm_eiointc_set_attr(struct kvm_device *dev,
				struct kvm_device_attr *attr)
{
	return 0;
}

static int kvm_eiointc_create(struct kvm_device *dev, u32 type)
{
	int ret;
	struct loongarch_eiointc *s;
	struct kvm_io_device *device, *device1;
	struct kvm *kvm = dev->kvm;

	/* eiointc has been created */
	if (kvm->arch.eiointc)
		return -EINVAL;

	s = kzalloc(sizeof(struct loongarch_eiointc), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	spin_lock_init(&s->lock);
	s->kvm = kvm;

	/*
	 * Initialize IOCSR device
	 */
	device = &s->device;
	kvm_iodevice_init(device, &kvm_eiointc_ops);
	mutex_lock(&kvm->slots_lock);
	ret = kvm_io_bus_register_dev(kvm, KVM_IOCSR_BUS,
			EIOINTC_BASE, EIOINTC_SIZE, device);
	mutex_unlock(&kvm->slots_lock);
	if (ret < 0) {
		kfree(s);
		return ret;
	}

	device1 = &s->device_vext;
	kvm_iodevice_init(device1, &kvm_eiointc_virt_ops);
	ret = kvm_io_bus_register_dev(kvm, KVM_IOCSR_BUS,
			EIOINTC_VIRT_BASE, EIOINTC_VIRT_SIZE, device1);
	if (ret < 0) {
		kvm_io_bus_unregister_dev(kvm, KVM_IOCSR_BUS, &s->device);
		kfree(s);
		return ret;
	}
	kvm->arch.eiointc = s;

	return 0;
}

static void kvm_eiointc_destroy(struct kvm_device *dev)
{
	struct kvm *kvm;
	struct loongarch_eiointc *eiointc;

	if (!dev || !dev->kvm || !dev->kvm->arch.eiointc)
		return;

	kvm = dev->kvm;
	eiointc = kvm->arch.eiointc;
	kvm_io_bus_unregister_dev(kvm, KVM_IOCSR_BUS, &eiointc->device);
	kvm_io_bus_unregister_dev(kvm, KVM_IOCSR_BUS, &eiointc->device_vext);
	kfree(eiointc);
}

static struct kvm_device_ops kvm_eiointc_dev_ops = {
	.name = "kvm-loongarch-eiointc",
	.create = kvm_eiointc_create,
	.destroy = kvm_eiointc_destroy,
	.set_attr = kvm_eiointc_set_attr,
	.get_attr = kvm_eiointc_get_attr,
};

int kvm_loongarch_register_eiointc_device(void)
{
	return kvm_register_device_ops(&kvm_eiointc_dev_ops, KVM_DEV_TYPE_LOONGARCH_EIOINTC);
}
