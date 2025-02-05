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
	int irq, bits;

	/* find each irq by irqs bitmap and update each irq */
	bits = sizeof(irq_mask) * 8;
	irq = find_first_bit((void *)&irq_mask, bits);
	while (irq < bits) {
		pch_pic_update_irq(s, irq, level);
		bitmap_clear((void *)&irq_mask, irq, 1);
		irq = find_first_bit((void *)&irq_mask, bits);
	}
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

/*
 * pch pic register is 64-bit, but it is accessed by 32-bit,
 * so we use high to get whether low or high 32 bits we want
 * to read.
 */
static u32 pch_pic_read_reg(u64 *s, int high)
{
	u64 val = *s;

	/* read the high 32 bits when high is 1 */
	return high ? (u32)(val >> 32) : (u32)val;
}

/*
 * pch pic register is 64-bit, but it is accessed by 32-bit,
 * so we use high to get whether low or high 32 bits we want
 * to write.
 */
static u32 pch_pic_write_reg(u64 *s, int high, u32 v)
{
	u64 val = *s, data = v;

	if (high) {
		/*
		 * Clear val high 32 bits
		 * Write the high 32 bits when the high is 1
		 */
		*s = (val << 32 >> 32) | (data << 32);
		val >>= 32;
	} else
		/*
		 * Clear val low 32 bits
		 * Write the low 32 bits when the high is 0
		 */
		*s = (val >> 32 << 32) | v;

	return (u32)val;
}

static int loongarch_pch_pic_read(struct loongarch_pch_pic *s, gpa_t addr, int len, void *val)
{
	int offset, index, ret = 0;
	u32 data = 0;
	u64 int_id = 0;

	offset = addr - s->pch_pic_base;

	spin_lock(&s->lock);
	switch (offset) {
	case PCH_PIC_INT_ID_START ... PCH_PIC_INT_ID_END:
		/* int id version */
		int_id |= (u64)PCH_PIC_INT_ID_VER << 32;
		/* irq number */
		int_id |= (u64)31 << (32 + 16);
		/* int id value */
		int_id |= PCH_PIC_INT_ID_VAL;
		*(u64 *)val = int_id;
		break;
	case PCH_PIC_MASK_START ... PCH_PIC_MASK_END:
		offset -= PCH_PIC_MASK_START;
		index = offset >> 2;
		/* read mask reg */
		data = pch_pic_read_reg(&s->mask, index);
		*(u32 *)val = data;
		break;
	case PCH_PIC_HTMSI_EN_START ... PCH_PIC_HTMSI_EN_END:
		offset -= PCH_PIC_HTMSI_EN_START;
		index = offset >> 2;
		/* read htmsi enable reg */
		data = pch_pic_read_reg(&s->htmsi_en, index);
		*(u32 *)val = data;
		break;
	case PCH_PIC_EDGE_START ... PCH_PIC_EDGE_END:
		offset -= PCH_PIC_EDGE_START;
		index = offset >> 2;
		/* read edge enable reg */
		data = pch_pic_read_reg(&s->edge, index);
		*(u32 *)val = data;
		break;
	case PCH_PIC_AUTO_CTRL0_START ... PCH_PIC_AUTO_CTRL0_END:
	case PCH_PIC_AUTO_CTRL1_START ... PCH_PIC_AUTO_CTRL1_END:
		/* we only use default mode: fixed interrupt distribution mode */
		*(u32 *)val = 0;
		break;
	case PCH_PIC_ROUTE_ENTRY_START ... PCH_PIC_ROUTE_ENTRY_END:
		/* only route to int0: eiointc */
		*(u8 *)val = 1;
		break;
	case PCH_PIC_HTMSI_VEC_START ... PCH_PIC_HTMSI_VEC_END:
		offset -= PCH_PIC_HTMSI_VEC_START;
		/* read htmsi vector */
		data = s->htmsi_vector[offset];
		*(u8 *)val = data;
		break;
	case PCH_PIC_POLARITY_START ... PCH_PIC_POLARITY_END:
		/* we only use defalut value 0: high level triggered */
		*(u32 *)val = 0;
		break;
	default:
		ret = -EINVAL;
	}
	spin_unlock(&s->lock);

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

	/* statistics of pch pic reading */
	vcpu->kvm->stat.pch_pic_read_exits++;
	ret = loongarch_pch_pic_read(s, addr, len, val);

	return ret;
}

static int loongarch_pch_pic_write(struct loongarch_pch_pic *s, gpa_t addr,
					int len, const void *val)
{
	int ret;
	u32 old, data, offset, index;
	u64 irq;

	ret = 0;
	data = *(u32 *)val;
	offset = addr - s->pch_pic_base;

	spin_lock(&s->lock);
	switch (offset) {
	case PCH_PIC_MASK_START ... PCH_PIC_MASK_END:
		offset -= PCH_PIC_MASK_START;
		/* get whether high or low 32 bits we want to write */
		index = offset >> 2;
		old = pch_pic_write_reg(&s->mask, index, data);
		/* enable irq when mask value change to 0 */
		irq = (old & ~data) << (32 * index);
		pch_pic_update_batch_irqs(s, irq, 1);
		/* disable irq when mask value change to 1 */
		irq = (~old & data) << (32 * index);
		pch_pic_update_batch_irqs(s, irq, 0);
		break;
	case PCH_PIC_HTMSI_EN_START ... PCH_PIC_HTMSI_EN_END:
		offset -= PCH_PIC_HTMSI_EN_START;
		index = offset >> 2;
		pch_pic_write_reg(&s->htmsi_en, index, data);
		break;
	case PCH_PIC_EDGE_START ... PCH_PIC_EDGE_END:
		offset -= PCH_PIC_EDGE_START;
		index = offset >> 2;
		/* 1: edge triggered, 0: level triggered */
		pch_pic_write_reg(&s->edge, index, data);
		break;
	case PCH_PIC_CLEAR_START ... PCH_PIC_CLEAR_END:
		offset -= PCH_PIC_CLEAR_START;
		index = offset >> 2;
		/* write 1 to clear edge irq */
		old = pch_pic_read_reg(&s->irr, index);
		/*
		 * get the irq bitmap which is edge triggered and
		 * already set and to be cleared
		 */
		irq = old & pch_pic_read_reg(&s->edge, index) & data;
		/* write irr to the new state where irqs have been cleared */
		pch_pic_write_reg(&s->irr, index, old & ~irq);
		/* update cleared irqs */
		pch_pic_update_batch_irqs(s, irq, 0);
		break;
	case PCH_PIC_AUTO_CTRL0_START ... PCH_PIC_AUTO_CTRL0_END:
		offset -= PCH_PIC_AUTO_CTRL0_START;
		index = offset >> 2;
		/* we only use default mode: fixed interrupt distribution mode */
		pch_pic_write_reg(&s->auto_ctrl0, index, 0);
		break;
	case PCH_PIC_AUTO_CTRL1_START ... PCH_PIC_AUTO_CTRL1_END:
		offset -= PCH_PIC_AUTO_CTRL1_START;
		index = offset >> 2;
		/* we only use default mode: fixed interrupt distribution mode */
		pch_pic_write_reg(&s->auto_ctrl1, index, 0);
		break;
	case PCH_PIC_ROUTE_ENTRY_START ... PCH_PIC_ROUTE_ENTRY_END:
		offset -= PCH_PIC_ROUTE_ENTRY_START;
		/* only route to int0: eiointc */
		s->route_entry[offset] = 1;
		break;
	case PCH_PIC_HTMSI_VEC_START ... PCH_PIC_HTMSI_VEC_END:
		/* route table to eiointc */
		offset -= PCH_PIC_HTMSI_VEC_START;
		s->htmsi_vector[offset] = (u8)data;
		break;
	case PCH_PIC_POLARITY_START ... PCH_PIC_POLARITY_END:
		offset -= PCH_PIC_POLARITY_START;
		index = offset >> 2;
		/* we only use defalut value 0: high level triggered */
		pch_pic_write_reg(&s->polarity, index, 0);
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

	/* statistics of pch pic writing */
	vcpu->kvm->stat.pch_pic_write_exits++;
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

	spin_lock(&s->lock);
	/* write or read value according to is_write */
	if (is_write) {
		if (copy_from_user(p, data, len))
			ret = -EFAULT;
	} else {
		if (copy_to_user(data, p, len))
			ret = -EFAULT;
	}
	spin_unlock(&s->lock);

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
	int ret;
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
