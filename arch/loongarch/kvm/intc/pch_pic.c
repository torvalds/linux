// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Loongson Technology Corporation Limited
 */

#include <asm/kvm_eiointc.h>
#include <asm/kvm_pch_pic.h>
#include <asm/kvm_vcpu.h>
#include <linux/count_zeros.h>

/* update the isr according to irq level and route irq to eiointc */
static void pch_pic_update_irq(struct loongarch_pch_pic *s, int irq, int level)
{
	u64 mask = BIT(irq);

	/*
	 * set isr and route irq to eiointc and
	 * the route table is in htmsi_vector[]
	 */
	if (level) {
		if (mask & s->irr & ~s->mask) {
			s->isr |= mask;
			irq = s->htmsi_vector[irq];
			eiointc_set_irq(s->kvm->arch.eiointc, irq, level);
		}
	} else {
		if (mask & s->isr & ~s->irr) {
			s->isr &= ~mask;
			irq = s->htmsi_vector[irq];
			eiointc_set_irq(s->kvm->arch.eiointc, irq, level);
		}
	}
}

/* update batch irqs, the irq_mask is a bitmap of irqs */
static void pch_pic_update_batch_irqs(struct loongarch_pch_pic *s, u64 irq_mask, int level)
{
	unsigned int irq;
	DECLARE_BITMAP(irqs, 64) = { BITMAP_FROM_U64(irq_mask) };

	for_each_set_bit(irq, irqs, 64)
		pch_pic_update_irq(s, irq, level);
}

/* called when a irq is triggered in pch pic */
void pch_pic_set_irq(struct loongarch_pch_pic *s, int irq, int level)
{
	u64 mask = BIT(irq);

	spin_lock(&s->lock);
	if (level)
		s->irr |= mask; /* set irr */
	else {
		/*
		 * In edge triggered mode, 0 does not mean to clear irq
		 * The irr register variable is cleared when cpu writes to the
		 * PCH_PIC_CLEAR_START address area
		 */
		if (s->edge & mask) {
			spin_unlock(&s->lock);
			return;
		}
		s->irr &= ~mask;
	}
	pch_pic_update_irq(s, irq, level);
	spin_unlock(&s->lock);
}

/* msi irq handler */
void pch_msi_set_irq(struct kvm *kvm, int irq, int level)
{
	eiointc_set_irq(kvm->arch.eiointc, irq, level);
}

static int loongarch_pch_pic_read(struct loongarch_pch_pic *s, gpa_t addr, int len, void *val)
{
	int ret = 0, offset;
	u64 data = 0;
	void *ptemp;

	offset = addr - s->pch_pic_base;
	offset -= offset & 7;

	spin_lock(&s->lock);
	switch (offset) {
	case PCH_PIC_INT_ID_START ... PCH_PIC_INT_ID_END:
		data = s->id.data;
		break;
	case PCH_PIC_MASK_START ... PCH_PIC_MASK_END:
		data = s->mask;
		break;
	case PCH_PIC_HTMSI_EN_START ... PCH_PIC_HTMSI_EN_END:
		/* read htmsi enable reg */
		data = s->htmsi_en;
		break;
	case PCH_PIC_EDGE_START ... PCH_PIC_EDGE_END:
		/* read edge enable reg */
		data = s->edge;
		break;
	case PCH_PIC_AUTO_CTRL0_START ... PCH_PIC_AUTO_CTRL0_END:
	case PCH_PIC_AUTO_CTRL1_START ... PCH_PIC_AUTO_CTRL1_END:
		/* we only use default mode: fixed interrupt distribution mode */
		break;
	case PCH_PIC_ROUTE_ENTRY_START ... PCH_PIC_ROUTE_ENTRY_END:
		/* only route to int0: eiointc */
		ptemp = s->route_entry + (offset - PCH_PIC_ROUTE_ENTRY_START);
		data = *(u64 *)ptemp;
		break;
	case PCH_PIC_HTMSI_VEC_START ... PCH_PIC_HTMSI_VEC_END:
		/* read htmsi vector */
		ptemp = s->htmsi_vector + (offset - PCH_PIC_HTMSI_VEC_START);
		data = *(u64 *)ptemp;
		break;
	case PCH_PIC_POLARITY_START ... PCH_PIC_POLARITY_END:
		data = s->polarity;
		break;
	case PCH_PIC_INT_IRR_START:
		data = s->irr;
		break;
	case PCH_PIC_INT_ISR_START:
		data = s->isr;
		break;
	default:
		ret = -EINVAL;
	}
	spin_unlock(&s->lock);

	if (ret == 0) {
		offset = (addr - s->pch_pic_base) & 7;
		data = data >> (offset * 8);
		memcpy(val, &data, len);
	}

	return ret;
}

static int kvm_pch_pic_read(struct kvm_vcpu *vcpu,
			struct kvm_io_device *dev,
			gpa_t addr, int len, void *val)
{
	int ret;
	struct loongarch_pch_pic *s = vcpu->kvm->arch.pch_pic;

	if (!s) {
		kvm_err("%s: pch pic irqchip not valid!\n", __func__);
		return -EINVAL;
	}

	if (addr & (len - 1)) {
		kvm_err("%s: pch pic not aligned addr %llx len %d\n", __func__, addr, len);
		return -EINVAL;
	}

	/* statistics of pch pic reading */
	vcpu->stat.pch_pic_read_exits++;
	ret = loongarch_pch_pic_read(s, addr, len, val);

	return ret;
}

static int loongarch_pch_pic_write(struct loongarch_pch_pic *s, gpa_t addr,
					int len, const void *val)
{
	int ret = 0, offset;
	u64 old, data, mask;
	void *ptemp;

	switch (len) {
	case 1:
		data = *(u8 *)val;
		mask = 0xFF;
		break;
	case 2:
		data = *(u16 *)val;
		mask = USHRT_MAX;
		break;
	case 4:
		data = *(u32 *)val;
		mask = UINT_MAX;
		break;
	case 8:
	default:
		data = *(u64 *)val;
		mask = ULONG_MAX;
		break;
	}

	offset = (addr - s->pch_pic_base) & 7;
	mask = mask << (offset * 8);
	data = data << (offset * 8);
	offset = (addr - s->pch_pic_base) - offset;

	spin_lock(&s->lock);
	switch (offset) {
	case PCH_PIC_MASK_START:
		old = s->mask;
		s->mask = (old & ~mask) | data;
		if (old & ~data)
			pch_pic_update_batch_irqs(s, old & ~data, 1);
		if (~old & data)
			pch_pic_update_batch_irqs(s, ~old & data, 0);
		break;
	case PCH_PIC_HTMSI_EN_START:
		s->htmsi_en = (s->htmsi_en & ~mask) | data;
		break;
	case PCH_PIC_EDGE_START:
		s->edge = (s->edge & ~mask) | data;
		break;
	case PCH_PIC_POLARITY_START:
		s->polarity = (s->polarity & ~mask) | data;
		break;
	case PCH_PIC_CLEAR_START:
		old = s->irr & s->edge & data;
		if (old) {
			s->irr &= ~old;
			pch_pic_update_batch_irqs(s, old, 0);
		}
		break;
	case PCH_PIC_HTMSI_VEC_START ... PCH_PIC_HTMSI_VEC_END:
		ptemp = s->htmsi_vector + (offset - PCH_PIC_HTMSI_VEC_START);
		*(u64 *)ptemp = (*(u64 *)ptemp & ~mask) | data;
		break;
	/* Not implemented */
	case PCH_PIC_AUTO_CTRL0_START:
	case PCH_PIC_AUTO_CTRL1_START:
	case PCH_PIC_ROUTE_ENTRY_START ... PCH_PIC_ROUTE_ENTRY_END:
		break;
	default:
		ret = -EINVAL;
		break;
	}
	spin_unlock(&s->lock);

	return ret;
}

static int kvm_pch_pic_write(struct kvm_vcpu *vcpu,
			struct kvm_io_device *dev,
			gpa_t addr, int len, const void *val)
{
	int ret;
	struct loongarch_pch_pic *s = vcpu->kvm->arch.pch_pic;

	if (!s) {
		kvm_err("%s: pch pic irqchip not valid!\n", __func__);
		return -EINVAL;
	}

	if (addr & (len - 1)) {
		kvm_err("%s: pch pic not aligned addr %llx len %d\n", __func__, addr, len);
		return -EINVAL;
	}

	/* statistics of pch pic writing */
	vcpu->stat.pch_pic_write_exits++;
	ret = loongarch_pch_pic_write(s, addr, len, val);

	return ret;
}

static const struct kvm_io_device_ops kvm_pch_pic_ops = {
	.read	= kvm_pch_pic_read,
	.write	= kvm_pch_pic_write,
};

static int kvm_pch_pic_init(struct kvm_device *dev, u64 addr)
{
	int ret;
	struct kvm *kvm = dev->kvm;
	struct kvm_io_device *device;
	struct loongarch_pch_pic *s = dev->kvm->arch.pch_pic;

	s->pch_pic_base = addr;
	device = &s->device;
	/* init device by pch pic writing and reading ops */
	kvm_iodevice_init(device, &kvm_pch_pic_ops);
	mutex_lock(&kvm->slots_lock);
	/* register pch pic device */
	ret = kvm_io_bus_register_dev(kvm, KVM_MMIO_BUS, addr, PCH_PIC_SIZE, device);
	mutex_unlock(&kvm->slots_lock);

	return (ret < 0) ? -EFAULT : 0;
}

/* used by user space to get or set pch pic registers */
static int kvm_pch_pic_regs_access(struct kvm_device *dev,
				struct kvm_device_attr *attr,
				bool is_write)
{
	char buf[8];
	int addr, offset, len = 8, ret = 0;
	void __user *data;
	void *p = NULL;
	struct loongarch_pch_pic *s;

	s = dev->kvm->arch.pch_pic;
	addr = attr->attr;
	data = (void __user *)attr->addr;

	/* get pointer to pch pic register by addr */
	switch (addr) {
	case PCH_PIC_MASK_START:
		p = &s->mask;
		break;
	case PCH_PIC_HTMSI_EN_START:
		p = &s->htmsi_en;
		break;
	case PCH_PIC_EDGE_START:
		p = &s->edge;
		break;
	case PCH_PIC_AUTO_CTRL0_START:
		p = &s->auto_ctrl0;
		break;
	case PCH_PIC_AUTO_CTRL1_START:
		p = &s->auto_ctrl1;
		break;
	case PCH_PIC_ROUTE_ENTRY_START ... PCH_PIC_ROUTE_ENTRY_END:
		offset = addr - PCH_PIC_ROUTE_ENTRY_START;
		p = &s->route_entry[offset];
		len = 1;
		break;
	case PCH_PIC_HTMSI_VEC_START ... PCH_PIC_HTMSI_VEC_END:
		offset = addr - PCH_PIC_HTMSI_VEC_START;
		p = &s->htmsi_vector[offset];
		len = 1;
		break;
	case PCH_PIC_INT_IRR_START:
		p = &s->irr;
		break;
	case PCH_PIC_INT_ISR_START:
		p = &s->isr;
		break;
	case PCH_PIC_POLARITY_START:
		p = &s->polarity;
		break;
	default:
		return -EINVAL;
	}

	if (is_write) {
		if (copy_from_user(buf, data, len))
			return -EFAULT;
	}

	spin_lock(&s->lock);
	if (is_write)
		memcpy(p, buf, len);
	else
		memcpy(buf, p, len);
	spin_unlock(&s->lock);

	if (!is_write) {
		if (copy_to_user(data, buf, len))
			return -EFAULT;
	}

	return ret;
}

static int kvm_pch_pic_get_attr(struct kvm_device *dev,
				struct kvm_device_attr *attr)
{
	switch (attr->group) {
	case KVM_DEV_LOONGARCH_PCH_PIC_GRP_REGS:
		return kvm_pch_pic_regs_access(dev, attr, false);
	default:
		return -EINVAL;
	}
}

static int kvm_pch_pic_set_attr(struct kvm_device *dev,
				struct kvm_device_attr *attr)
{
	u64 addr;
	void __user *uaddr = (void __user *)(long)attr->addr;

	switch (attr->group) {
	case KVM_DEV_LOONGARCH_PCH_PIC_GRP_CTRL:
		switch (attr->attr) {
		case KVM_DEV_LOONGARCH_PCH_PIC_CTRL_INIT:
			if (copy_from_user(&addr, uaddr, sizeof(addr)))
				return -EFAULT;

			if (!dev->kvm->arch.pch_pic) {
				kvm_err("%s: please create pch_pic irqchip first!\n", __func__);
				return -ENODEV;
			}

			return kvm_pch_pic_init(dev, addr);
		default:
			kvm_err("%s: unknown group (%d) attr (%lld)\n", __func__, attr->group,
					attr->attr);
			return -EINVAL;
		}
	case KVM_DEV_LOONGARCH_PCH_PIC_GRP_REGS:
		return kvm_pch_pic_regs_access(dev, attr, true);
	default:
		return -EINVAL;
	}
}

static int kvm_setup_default_irq_routing(struct kvm *kvm)
{
	int i, ret;
	u32 nr = KVM_IRQCHIP_NUM_PINS;
	struct kvm_irq_routing_entry *entries;

	entries = kcalloc(nr, sizeof(*entries), GFP_KERNEL);
	if (!entries)
		return -ENOMEM;

	for (i = 0; i < nr; i++) {
		entries[i].gsi = i;
		entries[i].type = KVM_IRQ_ROUTING_IRQCHIP;
		entries[i].u.irqchip.irqchip = 0;
		entries[i].u.irqchip.pin = i;
	}
	ret = kvm_set_irq_routing(kvm, entries, nr, 0);
	kfree(entries);

	return ret;
}

static int kvm_pch_pic_create(struct kvm_device *dev, u32 type)
{
	int i, ret, irq_num;
	struct kvm *kvm = dev->kvm;
	struct loongarch_pch_pic *s;

	/* pch pic should not has been created */
	if (kvm->arch.pch_pic)
		return -EINVAL;

	ret = kvm_setup_default_irq_routing(kvm);
	if (ret)
		return -ENOMEM;

	s = kzalloc(sizeof(struct loongarch_pch_pic), GFP_KERNEL);
	if (!s)
		return -ENOMEM;

	/*
	 * Interrupt controller identification register 1
	 *   Bit 24-31 Interrupt Controller ID
	 * Interrupt controller identification register 2
	 *   Bit  0-7  Interrupt Controller version number
	 *   Bit 16-23 The number of interrupt sources supported
	 */
	irq_num = 32;
	s->mask = -1UL;
	s->id.desc.id = PCH_PIC_INT_ID_VAL;
	s->id.desc.version = PCH_PIC_INT_ID_VER;
	s->id.desc.irq_num = irq_num - 1;
	for (i = 0; i < irq_num; i++) {
		s->route_entry[i] = 1;
		s->htmsi_vector[i] = i;
	}
	spin_lock_init(&s->lock);
	s->kvm = kvm;
	kvm->arch.pch_pic = s;

	return 0;
}

static void kvm_pch_pic_destroy(struct kvm_device *dev)
{
	struct kvm *kvm;
	struct loongarch_pch_pic *s;

	if (!dev || !dev->kvm || !dev->kvm->arch.pch_pic)
		return;

	kvm = dev->kvm;
	s = kvm->arch.pch_pic;
	/* unregister pch pic device and free it's memory */
	kvm_io_bus_unregister_dev(kvm, KVM_MMIO_BUS, &s->device);
	kfree(s);
}

static struct kvm_device_ops kvm_pch_pic_dev_ops = {
	.name = "kvm-loongarch-pch-pic",
	.create = kvm_pch_pic_create,
	.destroy = kvm_pch_pic_destroy,
	.set_attr = kvm_pch_pic_set_attr,
	.get_attr = kvm_pch_pic_get_attr,
};

int kvm_loongarch_register_pch_pic_device(void)
{
	return kvm_register_device_ops(&kvm_pch_pic_dev_ops, KVM_DEV_TYPE_LOONGARCH_PCHPIC);
}
