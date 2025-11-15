// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Loongson Technology Corporation Limited
 */

#include <asm/kvm_eiointc.h>
#include <asm/kvm_vcpu.h>
#include <linux/count_zeros.h>

static void eiointc_set_sw_coreisr(struct loongarch_eiointc *s)
{
	int ipnum, cpu, cpuid, irq;
	struct kvm_vcpu *vcpu;

	for (irq = 0; irq < EIOINTC_IRQS; irq++) {
		ipnum = s->ipmap.reg_u8[irq / 32];
		if (!(s->status & BIT(EIOINTC_ENABLE_INT_ENCODE))) {
			ipnum = count_trailing_zeros(ipnum);
			ipnum = (ipnum >= 0 && ipnum < 4) ? ipnum : 0;
		}

		cpuid = s->coremap.reg_u8[irq];
		vcpu = kvm_get_vcpu_by_cpuid(s->kvm, cpuid);
		if (!vcpu)
			continue;

		cpu = vcpu->vcpu_id;
		if (test_bit(irq, (unsigned long *)s->coreisr.reg_u32[cpu]))
			__set_bit(irq, s->sw_coreisr[cpu][ipnum]);
		else
			__clear_bit(irq, s->sw_coreisr[cpu][ipnum]);
	}
}

static void eiointc_update_irq(struct loongarch_eiointc *s, int irq, int level)
{
	int ipnum, cpu, found;
	struct kvm_vcpu *vcpu;
	struct kvm_interrupt vcpu_irq;

	ipnum = s->ipmap.reg_u8[irq / 32];
	if (!(s->status & BIT(EIOINTC_ENABLE_INT_ENCODE))) {
		ipnum = count_trailing_zeros(ipnum);
		ipnum = (ipnum >= 0 && ipnum < 4) ? ipnum : 0;
	}

	cpu = s->sw_coremap[irq];
	vcpu = kvm_get_vcpu_by_id(s->kvm, cpu);
	if (unlikely(vcpu == NULL)) {
		kvm_err("%s: invalid target cpu: %d\n", __func__, cpu);
		return;
	}

	if (level) {
		/* if not enable return false */
		if (!test_bit(irq, (unsigned long *)s->enable.reg_u32))
			return;
		__set_bit(irq, (unsigned long *)s->coreisr.reg_u32[cpu]);
		found = find_first_bit(s->sw_coreisr[cpu][ipnum], EIOINTC_IRQS);
		__set_bit(irq, s->sw_coreisr[cpu][ipnum]);
	} else {
		__clear_bit(irq, (unsigned long *)s->coreisr.reg_u32[cpu]);
		__clear_bit(irq, s->sw_coreisr[cpu][ipnum]);
		found = find_first_bit(s->sw_coreisr[cpu][ipnum], EIOINTC_IRQS);
	}

	if (found < EIOINTC_IRQS)
		return; /* other irq is handling, needn't update parent irq */

	vcpu_irq.irq = level ? (INT_HWI0 + ipnum) : -(INT_HWI0 + ipnum);
	kvm_vcpu_ioctl_interrupt(vcpu, &vcpu_irq);
}

static inline void eiointc_update_sw_coremap(struct loongarch_eiointc *s,
					int irq, u64 val, u32 len, bool notify)
{
	int i, cpu, cpuid;
	struct kvm_vcpu *vcpu;

	for (i = 0; i < len; i++) {
		cpuid = val & 0xff;
		val = val >> 8;

		if (!(s->status & BIT(EIOINTC_ENABLE_CPU_ENCODE))) {
			cpuid = ffs(cpuid) - 1;
			cpuid = (cpuid >= 4) ? 0 : cpuid;
		}

		vcpu = kvm_get_vcpu_by_cpuid(s->kvm, cpuid);
		if (!vcpu)
			continue;

		cpu = vcpu->vcpu_id;
		if (s->sw_coremap[irq + i] == cpu)
			continue;

		if (notify && test_bit(irq + i, (unsigned long *)s->isr.reg_u8)) {
			/* lower irq at old cpu and raise irq at new cpu */
			eiointc_update_irq(s, irq + i, 0);
			s->sw_coremap[irq + i] = cpu;
			eiointc_update_irq(s, irq + i, 1);
		} else {
			s->sw_coremap[irq + i] = cpu;
		}
	}
}

void eiointc_set_irq(struct loongarch_eiointc *s, int irq, int level)
{
	unsigned long flags;
	unsigned long *isr = (unsigned long *)s->isr.reg_u8;

	spin_lock_irqsave(&s->lock, flags);
	level ? __set_bit(irq, isr) : __clear_bit(irq, isr);
	eiointc_update_irq(s, irq, level);
	spin_unlock_irqrestore(&s->lock, flags);
}

static int loongarch_eiointc_read(struct kvm_vcpu *vcpu, struct loongarch_eiointc *s,
				gpa_t addr, unsigned long *val)
{
	int index, ret = 0;
	u64 data = 0;
	gpa_t offset;

	offset = addr - EIOINTC_BASE;
	switch (offset) {
	case EIOINTC_NODETYPE_START ... EIOINTC_NODETYPE_END:
		index = (offset - EIOINTC_NODETYPE_START) >> 3;
		data = s->nodetype.reg_u64[index];
		break;
	case EIOINTC_IPMAP_START ... EIOINTC_IPMAP_END:
		index = (offset - EIOINTC_IPMAP_START) >> 3;
		data = s->ipmap.reg_u64;
		break;
	case EIOINTC_ENABLE_START ... EIOINTC_ENABLE_END:
		index = (offset - EIOINTC_ENABLE_START) >> 3;
		data = s->enable.reg_u64[index];
		break;
	case EIOINTC_BOUNCE_START ... EIOINTC_BOUNCE_END:
		index = (offset - EIOINTC_BOUNCE_START) >> 3;
		data = s->bounce.reg_u64[index];
		break;
	case EIOINTC_COREISR_START ... EIOINTC_COREISR_END:
		index = (offset - EIOINTC_COREISR_START) >> 3;
		data = s->coreisr.reg_u64[vcpu->vcpu_id][index];
		break;
	case EIOINTC_COREMAP_START ... EIOINTC_COREMAP_END:
		index = (offset - EIOINTC_COREMAP_START) >> 3;
		data = s->coremap.reg_u64[index];
		break;
	default:
		ret = -EINVAL;
		break;
	}
	*val = data;

	return ret;
}

static int kvm_eiointc_read(struct kvm_vcpu *vcpu,
			struct kvm_io_device *dev,
			gpa_t addr, int len, void *val)
{
	int ret = -EINVAL;
	unsigned long flags, data, offset;
	struct loongarch_eiointc *eiointc = vcpu->kvm->arch.eiointc;

	if (!eiointc) {
		kvm_err("%s: eiointc irqchip not valid!\n", __func__);
		return -EINVAL;
	}

	if (addr & (len - 1)) {
		kvm_err("%s: eiointc not aligned addr %llx len %d\n", __func__, addr, len);
		return -EINVAL;
	}

	offset = addr & 0x7;
	addr -= offset;
	vcpu->stat.eiointc_read_exits++;
	spin_lock_irqsave(&eiointc->lock, flags);
	ret = loongarch_eiointc_read(vcpu, eiointc, addr, &data);
	spin_unlock_irqrestore(&eiointc->lock, flags);
	if (ret)
		return ret;

	data = data >> (offset * 8);
	switch (len) {
	case 1:
		*(long *)val = (s8)data;
		break;
	case 2:
		*(long *)val = (s16)data;
		break;
	case 4:
		*(long *)val = (s32)data;
		break;
	default:
		*(long *)val = (long)data;
		break;
	}

	return 0;
}

static int loongarch_eiointc_write(struct kvm_vcpu *vcpu,
				struct loongarch_eiointc *s,
				gpa_t addr, u64 value, u64 field_mask)
{
	int index, irq, ret = 0;
	u8 cpu;
	u64 data, old, mask;
	gpa_t offset;

	offset = addr & 7;
	mask = field_mask << (offset * 8);
	data = (value & field_mask) << (offset * 8);

	addr -= offset;
	offset = addr - EIOINTC_BASE;

	switch (offset) {
	case EIOINTC_NODETYPE_START ... EIOINTC_NODETYPE_END:
		index = (offset - EIOINTC_NODETYPE_START) >> 3;
		old = s->nodetype.reg_u64[index];
		s->nodetype.reg_u64[index] = (old & ~mask) | data;
		break;
	case EIOINTC_IPMAP_START ... EIOINTC_IPMAP_END:
		/*
		 * ipmap cannot be set at runtime, can be set only at the beginning
		 * of irqchip driver, need not update upper irq level
		 */
		old = s->ipmap.reg_u64;
		s->ipmap.reg_u64 = (old & ~mask) | data;
		break;
	case EIOINTC_ENABLE_START ... EIOINTC_ENABLE_END:
		index = (offset - EIOINTC_ENABLE_START) >> 3;
		old = s->enable.reg_u64[index];
		s->enable.reg_u64[index] = (old & ~mask) | data;
		/*
		 * 1: enable irq.
		 * update irq when isr is set.
		 */
		data = s->enable.reg_u64[index] & ~old & s->isr.reg_u64[index];
		while (data) {
			irq = __ffs(data);
			eiointc_update_irq(s, irq + index * 64, 1);
			data &= ~BIT_ULL(irq);
		}
		/*
		 * 0: disable irq.
		 * update irq when isr is set.
		 */
		data = ~s->enable.reg_u64[index] & old & s->isr.reg_u64[index];
		while (data) {
			irq = __ffs(data);
			eiointc_update_irq(s, irq + index * 64, 0);
			data &= ~BIT_ULL(irq);
		}
		break;
	case EIOINTC_BOUNCE_START ... EIOINTC_BOUNCE_END:
		/* do not emulate hw bounced irq routing */
		index = (offset - EIOINTC_BOUNCE_START) >> 3;
		old = s->bounce.reg_u64[index];
		s->bounce.reg_u64[index] = (old & ~mask) | data;
		break;
	case EIOINTC_COREISR_START ... EIOINTC_COREISR_END:
		index = (offset - EIOINTC_COREISR_START) >> 3;
		/* use attrs to get current cpu index */
		cpu = vcpu->vcpu_id;
		old = s->coreisr.reg_u64[cpu][index];
		/* write 1 to clear interrupt */
		s->coreisr.reg_u64[cpu][index] = old & ~data;
		data &= old;
		while (data) {
			irq = __ffs(data);
			eiointc_update_irq(s, irq + index * 64, 0);
			data &= ~BIT_ULL(irq);
		}
		break;
	case EIOINTC_COREMAP_START ... EIOINTC_COREMAP_END:
		index = (offset - EIOINTC_COREMAP_START) >> 3;
		old = s->coremap.reg_u64[index];
		s->coremap.reg_u64[index] = (old & ~mask) | data;
		data = s->coremap.reg_u64[index];
		eiointc_update_sw_coremap(s, index * 8, data, sizeof(data), true);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int kvm_eiointc_write(struct kvm_vcpu *vcpu,
			struct kvm_io_device *dev,
			gpa_t addr, int len, const void *val)
{
	int ret = -EINVAL;
	unsigned long flags, value;
	struct loongarch_eiointc *eiointc = vcpu->kvm->arch.eiointc;

	if (!eiointc) {
		kvm_err("%s: eiointc irqchip not valid!\n", __func__);
		return -EINVAL;
	}

	if (addr & (len - 1)) {
		kvm_err("%s: eiointc not aligned addr %llx len %d\n", __func__, addr, len);
		return -EINVAL;
	}

	vcpu->stat.eiointc_write_exits++;
	spin_lock_irqsave(&eiointc->lock, flags);
	switch (len) {
	case 1:
		value = *(unsigned char *)val;
		ret = loongarch_eiointc_write(vcpu, eiointc, addr, value, 0xFF);
		break;
	case 2:
		value = *(unsigned short *)val;
		ret = loongarch_eiointc_write(vcpu, eiointc, addr, value, USHRT_MAX);
		break;
	case 4:
		value = *(unsigned int *)val;
		ret = loongarch_eiointc_write(vcpu, eiointc, addr, value, UINT_MAX);
		break;
	default:
		value = *(unsigned long *)val;
		ret = loongarch_eiointc_write(vcpu, eiointc, addr, value, ULONG_MAX);
		break;
	}
	spin_unlock_irqrestore(&eiointc->lock, flags);

	return ret;
}

static const struct kvm_io_device_ops kvm_eiointc_ops = {
	.read	= kvm_eiointc_read,
	.write	= kvm_eiointc_write,
};

static int kvm_eiointc_virt_read(struct kvm_vcpu *vcpu,
				struct kvm_io_device *dev,
				gpa_t addr, int len, void *val)
{
	unsigned long flags;
	u32 *data = val;
	struct loongarch_eiointc *eiointc = vcpu->kvm->arch.eiointc;

	if (!eiointc) {
		kvm_err("%s: eiointc irqchip not valid!\n", __func__);
		return -EINVAL;
	}

	addr -= EIOINTC_VIRT_BASE;
	spin_lock_irqsave(&eiointc->lock, flags);
	switch (addr) {
	case EIOINTC_VIRT_FEATURES:
		*data = eiointc->features;
		break;
	case EIOINTC_VIRT_CONFIG:
		*data = eiointc->status;
		break;
	default:
		break;
	}
	spin_unlock_irqrestore(&eiointc->lock, flags);

	return 0;
}

static int kvm_eiointc_virt_write(struct kvm_vcpu *vcpu,
				struct kvm_io_device *dev,
				gpa_t addr, int len, const void *val)
{
	int ret = 0;
	unsigned long flags;
	u32 value = *(u32 *)val;
	struct loongarch_eiointc *eiointc = vcpu->kvm->arch.eiointc;

	if (!eiointc) {
		kvm_err("%s: eiointc irqchip not valid!\n", __func__);
		return -EINVAL;
	}

	addr -= EIOINTC_VIRT_BASE;
	spin_lock_irqsave(&eiointc->lock, flags);
	switch (addr) {
	case EIOINTC_VIRT_FEATURES:
		ret = -EPERM;
		break;
	case EIOINTC_VIRT_CONFIG:
		/*
		 * eiointc features can only be set at disabled status
		 */
		if ((eiointc->status & BIT(EIOINTC_ENABLE)) && value) {
			ret = -EPERM;
			break;
		}
		eiointc->status = value & eiointc->features;
		break;
	default:
		break;
	}
	spin_unlock_irqrestore(&eiointc->lock, flags);

	return ret;
}

static const struct kvm_io_device_ops kvm_eiointc_virt_ops = {
	.read	= kvm_eiointc_virt_read,
	.write	= kvm_eiointc_virt_write,
};

static int kvm_eiointc_ctrl_access(struct kvm_device *dev,
					struct kvm_device_attr *attr)
{
	int ret = 0;
	unsigned long flags;
	unsigned long type = (unsigned long)attr->attr;
	u32 i, start_irq, val;
	void __user *data;
	struct loongarch_eiointc *s = dev->kvm->arch.eiointc;

	data = (void __user *)attr->addr;
	switch (type) {
	case KVM_DEV_LOONGARCH_EXTIOI_CTRL_INIT_NUM_CPU:
	case KVM_DEV_LOONGARCH_EXTIOI_CTRL_INIT_FEATURE:
		if (copy_from_user(&val, data, 4))
			return -EFAULT;
		break;
	default:
		break;
	}

	spin_lock_irqsave(&s->lock, flags);
	switch (type) {
	case KVM_DEV_LOONGARCH_EXTIOI_CTRL_INIT_NUM_CPU:
		if (val > EIOINTC_ROUTE_MAX_VCPUS)
			ret = -EINVAL;
		else
			s->num_cpu = val;
		break;
	case KVM_DEV_LOONGARCH_EXTIOI_CTRL_INIT_FEATURE:
		s->features = val;
		if (!(s->features & BIT(EIOINTC_HAS_VIRT_EXTENSION)))
			s->status |= BIT(EIOINTC_ENABLE);
		break;
	case KVM_DEV_LOONGARCH_EXTIOI_CTRL_LOAD_FINISHED:
		eiointc_set_sw_coreisr(s);
		for (i = 0; i < (EIOINTC_IRQS / 4); i++) {
			start_irq = i * 4;
			eiointc_update_sw_coremap(s, start_irq,
					s->coremap.reg_u32[i], sizeof(u32), false);
		}
		break;
	default:
		break;
	}
	spin_unlock_irqrestore(&s->lock, flags);

	return ret;
}

static int kvm_eiointc_regs_access(struct kvm_device *dev,
					struct kvm_device_attr *attr,
					bool is_write, int *data)
{
	int addr, cpu, offset, ret = 0;
	unsigned long flags;
	void *p = NULL;
	struct loongarch_eiointc *s;

	s = dev->kvm->arch.eiointc;
	addr = attr->attr;
	cpu = addr >> 16;
	addr &= 0xffff;
	switch (addr) {
	case EIOINTC_NODETYPE_START ... EIOINTC_NODETYPE_END:
		offset = (addr - EIOINTC_NODETYPE_START) / 4;
		p = &s->nodetype.reg_u32[offset];
		break;
	case EIOINTC_IPMAP_START ... EIOINTC_IPMAP_END:
		offset = (addr - EIOINTC_IPMAP_START) / 4;
		p = &s->ipmap.reg_u32[offset];
		break;
	case EIOINTC_ENABLE_START ... EIOINTC_ENABLE_END:
		offset = (addr - EIOINTC_ENABLE_START) / 4;
		p = &s->enable.reg_u32[offset];
		break;
	case EIOINTC_BOUNCE_START ... EIOINTC_BOUNCE_END:
		offset = (addr - EIOINTC_BOUNCE_START) / 4;
		p = &s->bounce.reg_u32[offset];
		break;
	case EIOINTC_ISR_START ... EIOINTC_ISR_END:
		offset = (addr - EIOINTC_ISR_START) / 4;
		p = &s->isr.reg_u32[offset];
		break;
	case EIOINTC_COREISR_START ... EIOINTC_COREISR_END:
		if (cpu >= s->num_cpu)
			return -EINVAL;

		offset = (addr - EIOINTC_COREISR_START) / 4;
		p = &s->coreisr.reg_u32[cpu][offset];
		break;
	case EIOINTC_COREMAP_START ... EIOINTC_COREMAP_END:
		offset = (addr - EIOINTC_COREMAP_START) / 4;
		p = &s->coremap.reg_u32[offset];
		break;
	default:
		kvm_err("%s: unknown eiointc register, addr = %d\n", __func__, addr);
		return -EINVAL;
	}

	spin_lock_irqsave(&s->lock, flags);
	if (is_write)
		memcpy(p, data, 4);
	else
		memcpy(data, p, 4);
	spin_unlock_irqrestore(&s->lock, flags);

	return ret;
}

static int kvm_eiointc_sw_status_access(struct kvm_device *dev,
					struct kvm_device_attr *attr,
					bool is_write, int *data)
{
	int addr, ret = 0;
	unsigned long flags;
	void *p = NULL;
	struct loongarch_eiointc *s;

	s = dev->kvm->arch.eiointc;
	addr = attr->attr;
	addr &= 0xffff;

	switch (addr) {
	case KVM_DEV_LOONGARCH_EXTIOI_SW_STATUS_NUM_CPU:
		if (is_write)
			return ret;

		p = &s->num_cpu;
		break;
	case KVM_DEV_LOONGARCH_EXTIOI_SW_STATUS_FEATURE:
		if (is_write)
			return ret;

		p = &s->features;
		break;
	case KVM_DEV_LOONGARCH_EXTIOI_SW_STATUS_STATE:
		p = &s->status;
		break;
	default:
		kvm_err("%s: unknown eiointc register, addr = %d\n", __func__, addr);
		return -EINVAL;
	}
	spin_lock_irqsave(&s->lock, flags);
	if (is_write)
		memcpy(p, data, 4);
	else
		memcpy(data, p, 4);
	spin_unlock_irqrestore(&s->lock, flags);

	return ret;
}

static int kvm_eiointc_get_attr(struct kvm_device *dev,
				struct kvm_device_attr *attr)
{
	int ret, data;

	switch (attr->group) {
	case KVM_DEV_LOONGARCH_EXTIOI_GRP_REGS:
		ret = kvm_eiointc_regs_access(dev, attr, false, &data);
		if (ret)
			return ret;

		if (copy_to_user((void __user *)attr->addr, &data, 4))
			ret = -EFAULT;

		return ret;
	case KVM_DEV_LOONGARCH_EXTIOI_GRP_SW_STATUS:
		ret = kvm_eiointc_sw_status_access(dev, attr, false, &data);
		if (ret)
			return ret;

		if (copy_to_user((void __user *)attr->addr, &data, 4))
			ret = -EFAULT;

		return ret;
	default:
		return -EINVAL;
	}
}

static int kvm_eiointc_set_attr(struct kvm_device *dev,
				struct kvm_device_attr *attr)
{
	int data;

	switch (attr->group) {
	case KVM_DEV_LOONGARCH_EXTIOI_GRP_CTRL:
		return kvm_eiointc_ctrl_access(dev, attr);
	case KVM_DEV_LOONGARCH_EXTIOI_GRP_REGS:
		if (copy_from_user(&data, (void __user *)attr->addr, 4))
			return -EFAULT;

		return kvm_eiointc_regs_access(dev, attr, true, &data);
	case KVM_DEV_LOONGARCH_EXTIOI_GRP_SW_STATUS:
		if (copy_from_user(&data, (void __user *)attr->addr, 4))
			return -EFAULT;

		return kvm_eiointc_sw_status_access(dev, attr, true, &data);
	default:
		return -EINVAL;
	}
}

static int kvm_eiointc_create(struct kvm_device *dev, u32 type)
{
	int ret;
	struct loongarch_eiointc *s;
	struct kvm_io_device *device;
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

	device = &s->device_vext;
	kvm_iodevice_init(device, &kvm_eiointc_virt_ops);
	ret = kvm_io_bus_register_dev(kvm, KVM_IOCSR_BUS,
			EIOINTC_VIRT_BASE, EIOINTC_VIRT_SIZE, device);
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
