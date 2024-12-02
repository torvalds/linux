// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Loongson Technology Corporation Limited
 */

#include <linux/kvm_host.h>
#include <asm/kvm_ipi.h>
#include <asm/kvm_vcpu.h>

static void ipi_send(struct kvm *kvm, uint64_t data)
{
	int cpu, action;
	uint32_t status;
	struct kvm_vcpu *vcpu;
	struct kvm_interrupt irq;

	cpu = ((data & 0xffffffff) >> 16) & 0x3ff;
	vcpu = kvm_get_vcpu_by_cpuid(kvm, cpu);
	if (unlikely(vcpu == NULL)) {
		kvm_err("%s: invalid target cpu: %d\n", __func__, cpu);
		return;
	}

	action = BIT(data & 0x1f);
	spin_lock(&vcpu->arch.ipi_state.lock);
	status = vcpu->arch.ipi_state.status;
	vcpu->arch.ipi_state.status |= action;
	spin_unlock(&vcpu->arch.ipi_state.lock);
	if (status == 0) {
		irq.irq = LARCH_INT_IPI;
		kvm_vcpu_ioctl_interrupt(vcpu, &irq);
	}
}

static void ipi_clear(struct kvm_vcpu *vcpu, uint64_t data)
{
	uint32_t status;
	struct kvm_interrupt irq;

	spin_lock(&vcpu->arch.ipi_state.lock);
	vcpu->arch.ipi_state.status &= ~data;
	status = vcpu->arch.ipi_state.status;
	spin_unlock(&vcpu->arch.ipi_state.lock);
	if (status == 0) {
		irq.irq = -LARCH_INT_IPI;
		kvm_vcpu_ioctl_interrupt(vcpu, &irq);
	}
}

static uint64_t read_mailbox(struct kvm_vcpu *vcpu, int offset, int len)
{
	uint64_t data = 0;

	spin_lock(&vcpu->arch.ipi_state.lock);
	data = *(ulong *)((void *)vcpu->arch.ipi_state.buf + (offset - 0x20));
	spin_unlock(&vcpu->arch.ipi_state.lock);

	switch (len) {
	case 1:
		return data & 0xff;
	case 2:
		return data & 0xffff;
	case 4:
		return data & 0xffffffff;
	case 8:
		return data;
	default:
		kvm_err("%s: unknown data len: %d\n", __func__, len);
		return 0;
	}
}

static void write_mailbox(struct kvm_vcpu *vcpu, int offset, uint64_t data, int len)
{
	void *pbuf;

	spin_lock(&vcpu->arch.ipi_state.lock);
	pbuf = (void *)vcpu->arch.ipi_state.buf + (offset - 0x20);

	switch (len) {
	case 1:
		*(unsigned char *)pbuf = (unsigned char)data;
		break;
	case 2:
		*(unsigned short *)pbuf = (unsigned short)data;
		break;
	case 4:
		*(unsigned int *)pbuf = (unsigned int)data;
		break;
	case 8:
		*(unsigned long *)pbuf = (unsigned long)data;
		break;
	default:
		kvm_err("%s: unknown data len: %d\n", __func__, len);
	}
	spin_unlock(&vcpu->arch.ipi_state.lock);
}

static int send_ipi_data(struct kvm_vcpu *vcpu, gpa_t addr, uint64_t data)
{
	int i, ret;
	uint32_t val = 0, mask = 0;

	/*
	 * Bit 27-30 is mask for byte writing.
	 * If the mask is 0, we need not to do anything.
	 */
	if ((data >> 27) & 0xf) {
		/* Read the old val */
		ret = kvm_io_bus_read(vcpu, KVM_IOCSR_BUS, addr, sizeof(val), &val);
		if (unlikely(ret)) {
			kvm_err("%s: : read date from addr %llx failed\n", __func__, addr);
			return ret;
		}
		/* Construct the mask by scanning the bit 27-30 */
		for (i = 0; i < 4; i++) {
			if (data & (BIT(27 + i)))
				mask |= (0xff << (i * 8));
		}
		/* Save the old part of val */
		val &= mask;
	}
	val |= ((uint32_t)(data >> 32) & ~mask);
	ret = kvm_io_bus_write(vcpu, KVM_IOCSR_BUS, addr, sizeof(val), &val);
	if (unlikely(ret))
		kvm_err("%s: : write date to addr %llx failed\n", __func__, addr);

	return ret;
}

static int mail_send(struct kvm *kvm, uint64_t data)
{
	int cpu, mailbox, offset;
	struct kvm_vcpu *vcpu;

	cpu = ((data & 0xffffffff) >> 16) & 0x3ff;
	vcpu = kvm_get_vcpu_by_cpuid(kvm, cpu);
	if (unlikely(vcpu == NULL)) {
		kvm_err("%s: invalid target cpu: %d\n", __func__, cpu);
		return -EINVAL;
	}
	mailbox = ((data & 0xffffffff) >> 2) & 0x7;
	offset = IOCSR_IPI_BASE + IOCSR_IPI_BUF_20 + mailbox * 4;

	return send_ipi_data(vcpu, offset, data);
}

static int any_send(struct kvm *kvm, uint64_t data)
{
	int cpu, offset;
	struct kvm_vcpu *vcpu;

	cpu = ((data & 0xffffffff) >> 16) & 0x3ff;
	vcpu = kvm_get_vcpu_by_cpuid(kvm, cpu);
	if (unlikely(vcpu == NULL)) {
		kvm_err("%s: invalid target cpu: %d\n", __func__, cpu);
		return -EINVAL;
	}
	offset = data & 0xffff;

	return send_ipi_data(vcpu, offset, data);
}

static int loongarch_ipi_readl(struct kvm_vcpu *vcpu, gpa_t addr, int len, void *val)
{
	int ret = 0;
	uint32_t offset;
	uint64_t res = 0;

	offset = (uint32_t)(addr & 0x1ff);
	WARN_ON_ONCE(offset & (len - 1));

	switch (offset) {
	case IOCSR_IPI_STATUS:
		spin_lock(&vcpu->arch.ipi_state.lock);
		res = vcpu->arch.ipi_state.status;
		spin_unlock(&vcpu->arch.ipi_state.lock);
		break;
	case IOCSR_IPI_EN:
		spin_lock(&vcpu->arch.ipi_state.lock);
		res = vcpu->arch.ipi_state.en;
		spin_unlock(&vcpu->arch.ipi_state.lock);
		break;
	case IOCSR_IPI_SET:
		res = 0;
		break;
	case IOCSR_IPI_CLEAR:
		res = 0;
		break;
	case IOCSR_IPI_BUF_20 ... IOCSR_IPI_BUF_38 + 7:
		if (offset + len > IOCSR_IPI_BUF_38 + 8) {
			kvm_err("%s: invalid offset or len: offset = %d, len = %d\n",
				__func__, offset, len);
			ret = -EINVAL;
			break;
		}
		res = read_mailbox(vcpu, offset, len);
		break;
	default:
		kvm_err("%s: unknown addr: %llx\n", __func__, addr);
		ret = -EINVAL;
		break;
	}
	*(uint64_t *)val = res;

	return ret;
}

static int loongarch_ipi_writel(struct kvm_vcpu *vcpu, gpa_t addr, int len, const void *val)
{
	int ret = 0;
	uint64_t data;
	uint32_t offset;

	data = *(uint64_t *)val;

	offset = (uint32_t)(addr & 0x1ff);
	WARN_ON_ONCE(offset & (len - 1));

	switch (offset) {
	case IOCSR_IPI_STATUS:
		ret = -EINVAL;
		break;
	case IOCSR_IPI_EN:
		spin_lock(&vcpu->arch.ipi_state.lock);
		vcpu->arch.ipi_state.en = data;
		spin_unlock(&vcpu->arch.ipi_state.lock);
		break;
	case IOCSR_IPI_SET:
		ret = -EINVAL;
		break;
	case IOCSR_IPI_CLEAR:
		/* Just clear the status of the current vcpu */
		ipi_clear(vcpu, data);
		break;
	case IOCSR_IPI_BUF_20 ... IOCSR_IPI_BUF_38 + 7:
		if (offset + len > IOCSR_IPI_BUF_38 + 8) {
			kvm_err("%s: invalid offset or len: offset = %d, len = %d\n",
				__func__, offset, len);
			ret = -EINVAL;
			break;
		}
		write_mailbox(vcpu, offset, data, len);
		break;
	case IOCSR_IPI_SEND:
		ipi_send(vcpu->kvm, data);
		break;
	case IOCSR_MAIL_SEND:
		ret = mail_send(vcpu->kvm, *(uint64_t *)val);
		break;
	case IOCSR_ANY_SEND:
		ret = any_send(vcpu->kvm, *(uint64_t *)val);
		break;
	default:
		kvm_err("%s: unknown addr: %llx\n", __func__, addr);
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int kvm_ipi_read(struct kvm_vcpu *vcpu,
			struct kvm_io_device *dev,
			gpa_t addr, int len, void *val)
{
	int ret;
	struct loongarch_ipi *ipi;

	ipi = vcpu->kvm->arch.ipi;
	if (!ipi) {
		kvm_err("%s: ipi irqchip not valid!\n", __func__);
		return -EINVAL;
	}
	ipi->kvm->stat.ipi_read_exits++;
	ret = loongarch_ipi_readl(vcpu, addr, len, val);

	return ret;
}

static int kvm_ipi_write(struct kvm_vcpu *vcpu,
			struct kvm_io_device *dev,
			gpa_t addr, int len, const void *val)
{
	int ret;
	struct loongarch_ipi *ipi;

	ipi = vcpu->kvm->arch.ipi;
	if (!ipi) {
		kvm_err("%s: ipi irqchip not valid!\n", __func__);
		return -EINVAL;
	}
	ipi->kvm->stat.ipi_write_exits++;
	ret = loongarch_ipi_writel(vcpu, addr, len, val);

	return ret;
}

static const struct kvm_io_device_ops kvm_ipi_ops = {
	.read	= kvm_ipi_read,
	.write	= kvm_ipi_write,
};

static int kvm_ipi_regs_access(struct kvm_device *dev,
				struct kvm_device_attr *attr,
				bool is_write)
{
	int len = 4;
	int cpu, addr;
	uint64_t val;
	void *p = NULL;
	struct kvm_vcpu *vcpu;

	cpu = (attr->attr >> 16) & 0x3ff;
	addr = attr->attr & 0xff;

	vcpu = kvm_get_vcpu(dev->kvm, cpu);
	if (unlikely(vcpu == NULL)) {
		kvm_err("%s: invalid target cpu: %d\n", __func__, cpu);
		return -EINVAL;
	}

	switch (addr) {
	case IOCSR_IPI_STATUS:
		p = &vcpu->arch.ipi_state.status;
		break;
	case IOCSR_IPI_EN:
		p = &vcpu->arch.ipi_state.en;
		break;
	case IOCSR_IPI_SET:
		p = &vcpu->arch.ipi_state.set;
		break;
	case IOCSR_IPI_CLEAR:
		p = &vcpu->arch.ipi_state.clear;
		break;
	case IOCSR_IPI_BUF_20:
		p = &vcpu->arch.ipi_state.buf[0];
		len = 8;
		break;
	case IOCSR_IPI_BUF_28:
		p = &vcpu->arch.ipi_state.buf[1];
		len = 8;
		break;
	case IOCSR_IPI_BUF_30:
		p = &vcpu->arch.ipi_state.buf[2];
		len = 8;
		break;
	case IOCSR_IPI_BUF_38:
		p = &vcpu->arch.ipi_state.buf[3];
		len = 8;
		break;
	default:
		kvm_err("%s: unknown ipi register, addr = %d\n", __func__, addr);
		return -EINVAL;
	}

	if (is_write) {
		if (len == 4) {
			if (get_user(val, (uint32_t __user *)attr->addr))
				return -EFAULT;
			*(uint32_t *)p = (uint32_t)val;
		} else if (len == 8) {
			if (get_user(val, (uint64_t __user *)attr->addr))
				return -EFAULT;
			*(uint64_t *)p = val;
		}
	} else {
		if (len == 4) {
			val = *(uint32_t *)p;
			return put_user(val, (uint32_t __user *)attr->addr);
		} else if (len == 8) {
			val = *(uint64_t *)p;
			return put_user(val, (uint64_t __user *)attr->addr);
		}
	}

	return 0;
}

static int kvm_ipi_get_attr(struct kvm_device *dev,
			struct kvm_device_attr *attr)
{
	switch (attr->group) {
	case KVM_DEV_LOONGARCH_IPI_GRP_REGS:
		return kvm_ipi_regs_access(dev, attr, false);
	default:
		kvm_err("%s: unknown group (%d)\n", __func__, attr->group);
		return -EINVAL;
	}
}

static int kvm_ipi_set_attr(struct kvm_device *dev,
			struct kvm_device_attr *attr)
{
	switch (attr->group) {
	case KVM_DEV_LOONGARCH_IPI_GRP_REGS:
		return kvm_ipi_regs_access(dev, attr, true);
	default:
		kvm_err("%s: unknown group (%d)\n", __func__, attr->group);
		return -EINVAL;
	}
}

static int kvm_ipi_create(struct kvm_device *dev, u32 type)
{
	int ret;
	struct kvm *kvm;
	struct kvm_io_device *device;
	struct loongarch_ipi *s;

	if (!dev) {
		kvm_err("%s: kvm_device ptr is invalid!\n", __func__);
		return -EINVAL;
	}

	kvm = dev->kvm;
	if (kvm->arch.ipi) {
		kvm_err("%s: LoongArch IPI has already been created!\n", __func__);
		return -EINVAL;
	}

	s = kzalloc(sizeof(struct loongarch_ipi), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	spin_lock_init(&s->lock);
	s->kvm = kvm;

	/*
	 * Initialize IOCSR device
	 */
	device = &s->device;
	kvm_iodevice_init(device, &kvm_ipi_ops);
	mutex_lock(&kvm->slots_lock);
	ret = kvm_io_bus_register_dev(kvm, KVM_IOCSR_BUS, IOCSR_IPI_BASE, IOCSR_IPI_SIZE, device);
	mutex_unlock(&kvm->slots_lock);
	if (ret < 0) {
		kvm_err("%s: Initialize IOCSR dev failed, ret = %d\n", __func__, ret);
		goto err;
	}

	kvm->arch.ipi = s;
	return 0;

err:
	kfree(s);
	return -EFAULT;
}

static void kvm_ipi_destroy(struct kvm_device *dev)
{
	struct kvm *kvm;
	struct loongarch_ipi *ipi;

	if (!dev || !dev->kvm || !dev->kvm->arch.ipi)
		return;

	kvm = dev->kvm;
	ipi = kvm->arch.ipi;
	kvm_io_bus_unregister_dev(kvm, KVM_IOCSR_BUS, &ipi->device);
	kfree(ipi);
}

static struct kvm_device_ops kvm_ipi_dev_ops = {
	.name = "kvm-loongarch-ipi",
	.create = kvm_ipi_create,
	.destroy = kvm_ipi_destroy,
	.set_attr = kvm_ipi_set_attr,
	.get_attr = kvm_ipi_get_attr,
};

int kvm_loongarch_register_ipi_device(void)
{
	return kvm_register_device_ops(&kvm_ipi_dev_ops, KVM_DEV_TYPE_LOONGARCH_IPI);
}
