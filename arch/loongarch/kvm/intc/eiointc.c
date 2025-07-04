// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2024 Loongson Technology Corporation Limited
 */

#include <asm/kvm_eiointc.h>
#include <asm/kvm_vcpu.h>
#include <linux/count_zeros.h>

static void eiointc_set_sw_coreisr(struct loongarch_eiointc *s)
{
	int ipnum, cpu, cpuid, irq_index, irq_mask, irq;
	struct kvm_vcpu *vcpu;

	for (irq = 0; irq < EIOINTC_IRQS; irq++) {
		ipnum = s->ipmap.reg_u8[irq / 32];
		if (!(s->status & BIT(EIOINTC_ENABLE_INT_ENCODE))) {
			ipnum = count_trailing_zeros(ipnum);
			ipnum = (ipnum >= 0 && ipnum < 4) ? ipnum : 0;
		}
		irq_index = irq / 32;
		irq_mask = BIT(irq & 0x1f);

		cpuid = s->coremap.reg_u8[irq];
		vcpu = kvm_get_vcpu_by_cpuid(s->kvm, cpuid);
		if (!vcpu)
			continue;

		cpu = vcpu->vcpu_id;
		if (!!(s->coreisr.reg_u32[cpu][irq_index] & irq_mask))
			set_bit(irq, s->sw_coreisr[cpu][ipnum]);
		else
			clear_bit(irq, s->sw_coreisr[cpu][ipnum]);
	}
}

static void eiointc_update_irq(struct loongarch_eiointc *s, int irq, int level)
{
	int ipnum, cpu, found, irq_index, irq_mask;
	struct kvm_vcpu *vcpu;
	struct kvm_interrupt vcpu_irq;

	ipnum = s->ipmap.reg_u8[irq / 32];
	if (!(s->status & BIT(EIOINTC_ENABLE_INT_ENCODE))) {
		ipnum = count_trailing_zeros(ipnum);
		ipnum = (ipnum >= 0 && ipnum < 4) ? ipnum : 0;
	}

	cpu = s->sw_coremap[irq];
	vcpu = kvm_get_vcpu(s->kvm, cpu);
	irq_index = irq / 32;
	irq_mask = BIT(irq & 0x1f);

	if (level) {
		/* if not enable return false */
		if (((s->enable.reg_u32[irq_index]) & irq_mask) == 0)
			return;
		s->coreisr.reg_u32[cpu][irq_index] |= irq_mask;
		found = find_first_bit(s->sw_coreisr[cpu][ipnum], EIOINTC_IRQS);
		set_bit(irq, s->sw_coreisr[cpu][ipnum]);
	} else {
		s->coreisr.reg_u32[cpu][irq_index] &= ~irq_mask;
		clear_bit(irq, s->sw_coreisr[cpu][ipnum]);
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

	level ? set_bit(irq, isr) : clear_bit(irq, isr);
	spin_lock_irqsave(&s->lock, flags);
	eiointc_update_irq(s, irq, level);
	spin_unlock_irqrestore(&s->lock, flags);
}

static inline void eiointc_enable_irq(struct kvm_vcpu *vcpu,
		struct loongarch_eiointc *s, int index, u8 mask, int level)
{
	u8 val;
	int irq;

	val = mask & s->isr.reg_u8[index];
	irq = ffs(val);
	while (irq != 0) {
		/*
		 * enable bit change from 0 to 1,
		 * need to update irq by pending bits
		 */
		eiointc_update_irq(s, irq - 1 + index * 8, level);
		val &= ~BIT(irq - 1);
		irq = ffs(val);
	}
}

static int loongarch_eiointc_readb(struct kvm_vcpu *vcpu, struct loongarch_eiointc *s,
				gpa_t addr, int len, void *val)
{
	int index, ret = 0;
	u8 data = 0;
	gpa_t offset;

	offset = addr - EIOINTC_BASE;
	switch (offset) {
	case EIOINTC_NODETYPE_START ... EIOINTC_NODETYPE_END:
		index = offset - EIOINTC_NODETYPE_START;
		data = s->nodetype.reg_u8[index];
		break;
	case EIOINTC_IPMAP_START ... EIOINTC_IPMAP_END:
		index = offset - EIOINTC_IPMAP_START;
		data = s->ipmap.reg_u8[index];
		break;
	case EIOINTC_ENABLE_START ... EIOINTC_ENABLE_END:
		index = offset - EIOINTC_ENABLE_START;
		data = s->enable.reg_u8[index];
		break;
	case EIOINTC_BOUNCE_START ... EIOINTC_BOUNCE_END:
		index = offset - EIOINTC_BOUNCE_START;
		data = s->bounce.reg_u8[index];
		break;
	case EIOINTC_COREISR_START ... EIOINTC_COREISR_END:
		index = offset - EIOINTC_COREISR_START;
		data = s->coreisr.reg_u8[vcpu->vcpu_id][index];
		break;
	case EIOINTC_COREMAP_START ... EIOINTC_COREMAP_END:
		index = offset - EIOINTC_COREMAP_START;
		data = s->coremap.reg_u8[index];
		break;
	default:
		ret = -EINVAL;
		break;
	}
	*(u8 *)val = data;

	return ret;
}

static int loongarch_eiointc_readw(struct kvm_vcpu *vcpu, struct loongarch_eiointc *s,
				gpa_t addr, int len, void *val)
{
	int index, ret = 0;
	u16 data = 0;
	gpa_t offset;

	offset = addr - EIOINTC_BASE;
	switch (offset) {
	case EIOINTC_NODETYPE_START ... EIOINTC_NODETYPE_END:
		index = (offset - EIOINTC_NODETYPE_START) >> 1;
		data = s->nodetype.reg_u16[index];
		break;
	case EIOINTC_IPMAP_START ... EIOINTC_IPMAP_END:
		index = (offset - EIOINTC_IPMAP_START) >> 1;
		data = s->ipmap.reg_u16[index];
		break;
	case EIOINTC_ENABLE_START ... EIOINTC_ENABLE_END:
		index = (offset - EIOINTC_ENABLE_START) >> 1;
		data = s->enable.reg_u16[index];
		break;
	case EIOINTC_BOUNCE_START ... EIOINTC_BOUNCE_END:
		index = (offset - EIOINTC_BOUNCE_START) >> 1;
		data = s->bounce.reg_u16[index];
		break;
	case EIOINTC_COREISR_START ... EIOINTC_COREISR_END:
		index = (offset - EIOINTC_COREISR_START) >> 1;
		data = s->coreisr.reg_u16[vcpu->vcpu_id][index];
		break;
	case EIOINTC_COREMAP_START ... EIOINTC_COREMAP_END:
		index = (offset - EIOINTC_COREMAP_START) >> 1;
		data = s->coremap.reg_u16[index];
		break;
	default:
		ret = -EINVAL;
		break;
	}
	*(u16 *)val = data;

	return ret;
}

static int loongarch_eiointc_readl(struct kvm_vcpu *vcpu, struct loongarch_eiointc *s,
				gpa_t addr, int len, void *val)
{
	int index, ret = 0;
	u32 data = 0;
	gpa_t offset;

	offset = addr - EIOINTC_BASE;
	switch (offset) {
	case EIOINTC_NODETYPE_START ... EIOINTC_NODETYPE_END:
		index = (offset - EIOINTC_NODETYPE_START) >> 2;
		data = s->nodetype.reg_u32[index];
		break;
	case EIOINTC_IPMAP_START ... EIOINTC_IPMAP_END:
		index = (offset - EIOINTC_IPMAP_START) >> 2;
		data = s->ipmap.reg_u32[index];
		break;
	case EIOINTC_ENABLE_START ... EIOINTC_ENABLE_END:
		index = (offset - EIOINTC_ENABLE_START) >> 2;
		data = s->enable.reg_u32[index];
		break;
	case EIOINTC_BOUNCE_START ... EIOINTC_BOUNCE_END:
		index = (offset - EIOINTC_BOUNCE_START) >> 2;
		data = s->bounce.reg_u32[index];
		break;
	case EIOINTC_COREISR_START ... EIOINTC_COREISR_END:
		index = (offset - EIOINTC_COREISR_START) >> 2;
		data = s->coreisr.reg_u32[vcpu->vcpu_id][index];
		break;
	case EIOINTC_COREMAP_START ... EIOINTC_COREMAP_END:
		index = (offset - EIOINTC_COREMAP_START) >> 2;
		data = s->coremap.reg_u32[index];
		break;
	default:
		ret = -EINVAL;
		break;
	}
	*(u32 *)val = data;

	return ret;
}

static int loongarch_eiointc_readq(struct kvm_vcpu *vcpu, struct loongarch_eiointc *s,
				gpa_t addr, int len, void *val)
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
	*(u64 *)val = data;

	return ret;
}

static int kvm_eiointc_read(struct kvm_vcpu *vcpu,
			struct kvm_io_device *dev,
			gpa_t addr, int len, void *val)
{
	int ret = -EINVAL;
	unsigned long flags;
	struct loongarch_eiointc *eiointc = vcpu->kvm->arch.eiointc;

	if (!eiointc) {
		kvm_err("%s: eiointc irqchip not valid!\n", __func__);
		return -EINVAL;
	}

	if (addr & (len - 1)) {
		kvm_err("%s: eiointc not aligned addr %llx len %d\n", __func__, addr, len);
		return -EINVAL;
	}

	vcpu->kvm->stat.eiointc_read_exits++;
	spin_lock_irqsave(&eiointc->lock, flags);
	switch (len) {
	case 1:
		ret = loongarch_eiointc_readb(vcpu, eiointc, addr, len, val);
		break;
	case 2:
		ret = loongarch_eiointc_readw(vcpu, eiointc, addr, len, val);
		break;
	case 4:
		ret = loongarch_eiointc_readl(vcpu, eiointc, addr, len, val);
		break;
	case 8:
		ret = loongarch_eiointc_readq(vcpu, eiointc, addr, len, val);
		break;
	default:
		WARN_ONCE(1, "%s: Abnormal address access: addr 0x%llx, size %d\n",
						__func__, addr, len);
	}
	spin_unlock_irqrestore(&eiointc->lock, flags);

	return ret;
}

static int loongarch_eiointc_writeb(struct kvm_vcpu *vcpu,
				struct loongarch_eiointc *s,
				gpa_t addr, int len, const void *val)
{
	int index, irq, bits, ret = 0;
	u8 cpu;
	u8 data, old_data;
	u8 coreisr, old_coreisr;
	gpa_t offset;

	data = *(u8 *)val;
	offset = addr - EIOINTC_BASE;

	switch (offset) {
	case EIOINTC_NODETYPE_START ... EIOINTC_NODETYPE_END:
		index = (offset - EIOINTC_NODETYPE_START);
		s->nodetype.reg_u8[index] = data;
		break;
	case EIOINTC_IPMAP_START ... EIOINTC_IPMAP_END:
		/*
		 * ipmap cannot be set at runtime, can be set only at the beginning
		 * of irqchip driver, need not update upper irq level
		 */
		index = (offset - EIOINTC_IPMAP_START);
		s->ipmap.reg_u8[index] = data;
		break;
	case EIOINTC_ENABLE_START ... EIOINTC_ENABLE_END:
		index = (offset - EIOINTC_ENABLE_START);
		old_data = s->enable.reg_u8[index];
		s->enable.reg_u8[index] = data;
		/*
		 * 1: enable irq.
		 * update irq when isr is set.
		 */
		data = s->enable.reg_u8[index] & ~old_data & s->isr.reg_u8[index];
		eiointc_enable_irq(vcpu, s, index, data, 1);
		/*
		 * 0: disable irq.
		 * update irq when isr is set.
		 */
		data = ~s->enable.reg_u8[index] & old_data & s->isr.reg_u8[index];
		eiointc_enable_irq(vcpu, s, index, data, 0);
		break;
	case EIOINTC_BOUNCE_START ... EIOINTC_BOUNCE_END:
		/* do not emulate hw bounced irq routing */
		index = offset - EIOINTC_BOUNCE_START;
		s->bounce.reg_u8[index] = data;
		break;
	case EIOINTC_COREISR_START ... EIOINTC_COREISR_END:
		index = (offset - EIOINTC_COREISR_START);
		/* use attrs to get current cpu index */
		cpu = vcpu->vcpu_id;
		coreisr = data;
		old_coreisr = s->coreisr.reg_u8[cpu][index];
		/* write 1 to clear interrupt */
		s->coreisr.reg_u8[cpu][index] = old_coreisr & ~coreisr;
		coreisr &= old_coreisr;
		bits = sizeof(data) * 8;
		irq = find_first_bit((void *)&coreisr, bits);
		while (irq < bits) {
			eiointc_update_irq(s, irq + index * bits, 0);
			bitmap_clear((void *)&coreisr, irq, 1);
			irq = find_first_bit((void *)&coreisr, bits);
		}
		break;
	case EIOINTC_COREMAP_START ... EIOINTC_COREMAP_END:
		irq = offset - EIOINTC_COREMAP_START;
		index = irq;
		s->coremap.reg_u8[index] = data;
		eiointc_update_sw_coremap(s, irq, data, sizeof(data), true);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int loongarch_eiointc_writew(struct kvm_vcpu *vcpu,
				struct loongarch_eiointc *s,
				gpa_t addr, int len, const void *val)
{
	int i, index, irq, bits, ret = 0;
	u8 cpu;
	u16 data, old_data;
	u16 coreisr, old_coreisr;
	gpa_t offset;

	data = *(u16 *)val;
	offset = addr - EIOINTC_BASE;

	switch (offset) {
	case EIOINTC_NODETYPE_START ... EIOINTC_NODETYPE_END:
		index = (offset - EIOINTC_NODETYPE_START) >> 1;
		s->nodetype.reg_u16[index] = data;
		break;
	case EIOINTC_IPMAP_START ... EIOINTC_IPMAP_END:
		/*
		 * ipmap cannot be set at runtime, can be set only at the beginning
		 * of irqchip driver, need not update upper irq level
		 */
		index = (offset - EIOINTC_IPMAP_START) >> 1;
		s->ipmap.reg_u16[index] = data;
		break;
	case EIOINTC_ENABLE_START ... EIOINTC_ENABLE_END:
		index = (offset - EIOINTC_ENABLE_START) >> 1;
		old_data = s->enable.reg_u16[index];
		s->enable.reg_u16[index] = data;
		/*
		 * 1: enable irq.
		 * update irq when isr is set.
		 */
		data = s->enable.reg_u16[index] & ~old_data & s->isr.reg_u16[index];
		for (i = 0; i < sizeof(data); i++) {
			u8 mask = (data >> (i * 8)) & 0xff;
			eiointc_enable_irq(vcpu, s, index * 2 + i, mask, 1);
		}
		/*
		 * 0: disable irq.
		 * update irq when isr is set.
		 */
		data = ~s->enable.reg_u16[index] & old_data & s->isr.reg_u16[index];
		for (i = 0; i < sizeof(data); i++) {
			u8 mask = (data >> (i * 8)) & 0xff;
			eiointc_enable_irq(vcpu, s, index * 2 + i, mask, 0);
		}
		break;
	case EIOINTC_BOUNCE_START ... EIOINTC_BOUNCE_END:
		/* do not emulate hw bounced irq routing */
		index = (offset - EIOINTC_BOUNCE_START) >> 1;
		s->bounce.reg_u16[index] = data;
		break;
	case EIOINTC_COREISR_START ... EIOINTC_COREISR_END:
		index = (offset - EIOINTC_COREISR_START) >> 1;
		/* use attrs to get current cpu index */
		cpu = vcpu->vcpu_id;
		coreisr = data;
		old_coreisr = s->coreisr.reg_u16[cpu][index];
		/* write 1 to clear interrupt */
		s->coreisr.reg_u16[cpu][index] = old_coreisr & ~coreisr;
		coreisr &= old_coreisr;
		bits = sizeof(data) * 8;
		irq = find_first_bit((void *)&coreisr, bits);
		while (irq < bits) {
			eiointc_update_irq(s, irq + index * bits, 0);
			bitmap_clear((void *)&coreisr, irq, 1);
			irq = find_first_bit((void *)&coreisr, bits);
		}
		break;
	case EIOINTC_COREMAP_START ... EIOINTC_COREMAP_END:
		irq = offset - EIOINTC_COREMAP_START;
		index = irq >> 1;
		s->coremap.reg_u16[index] = data;
		eiointc_update_sw_coremap(s, irq, data, sizeof(data), true);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int loongarch_eiointc_writel(struct kvm_vcpu *vcpu,
				struct loongarch_eiointc *s,
				gpa_t addr, int len, const void *val)
{
	int i, index, irq, bits, ret = 0;
	u8 cpu;
	u32 data, old_data;
	u32 coreisr, old_coreisr;
	gpa_t offset;

	data = *(u32 *)val;
	offset = addr - EIOINTC_BASE;

	switch (offset) {
	case EIOINTC_NODETYPE_START ... EIOINTC_NODETYPE_END:
		index = (offset - EIOINTC_NODETYPE_START) >> 2;
		s->nodetype.reg_u32[index] = data;
		break;
	case EIOINTC_IPMAP_START ... EIOINTC_IPMAP_END:
		/*
		 * ipmap cannot be set at runtime, can be set only at the beginning
		 * of irqchip driver, need not update upper irq level
		 */
		index = (offset - EIOINTC_IPMAP_START) >> 2;
		s->ipmap.reg_u32[index] = data;
		break;
	case EIOINTC_ENABLE_START ... EIOINTC_ENABLE_END:
		index = (offset - EIOINTC_ENABLE_START) >> 2;
		old_data = s->enable.reg_u32[index];
		s->enable.reg_u32[index] = data;
		/*
		 * 1: enable irq.
		 * update irq when isr is set.
		 */
		data = s->enable.reg_u32[index] & ~old_data & s->isr.reg_u32[index];
		for (i = 0; i < sizeof(data); i++) {
			u8 mask = (data >> (i * 8)) & 0xff;
			eiointc_enable_irq(vcpu, s, index * 4 + i, mask, 1);
		}
		/*
		 * 0: disable irq.
		 * update irq when isr is set.
		 */
		data = ~s->enable.reg_u32[index] & old_data & s->isr.reg_u32[index];
		for (i = 0; i < sizeof(data); i++) {
			u8 mask = (data >> (i * 8)) & 0xff;
			eiointc_enable_irq(vcpu, s, index * 4 + i, mask, 0);
		}
		break;
	case EIOINTC_BOUNCE_START ... EIOINTC_BOUNCE_END:
		/* do not emulate hw bounced irq routing */
		index = (offset - EIOINTC_BOUNCE_START) >> 2;
		s->bounce.reg_u32[index] = data;
		break;
	case EIOINTC_COREISR_START ... EIOINTC_COREISR_END:
		index = (offset - EIOINTC_COREISR_START) >> 2;
		/* use attrs to get current cpu index */
		cpu = vcpu->vcpu_id;
		coreisr = data;
		old_coreisr = s->coreisr.reg_u32[cpu][index];
		/* write 1 to clear interrupt */
		s->coreisr.reg_u32[cpu][index] = old_coreisr & ~coreisr;
		coreisr &= old_coreisr;
		bits = sizeof(data) * 8;
		irq = find_first_bit((void *)&coreisr, bits);
		while (irq < bits) {
			eiointc_update_irq(s, irq + index * bits, 0);
			bitmap_clear((void *)&coreisr, irq, 1);
			irq = find_first_bit((void *)&coreisr, bits);
		}
		break;
	case EIOINTC_COREMAP_START ... EIOINTC_COREMAP_END:
		irq = offset - EIOINTC_COREMAP_START;
		index = irq >> 2;
		s->coremap.reg_u32[index] = data;
		eiointc_update_sw_coremap(s, irq, data, sizeof(data), true);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int loongarch_eiointc_writeq(struct kvm_vcpu *vcpu,
				struct loongarch_eiointc *s,
				gpa_t addr, int len, const void *val)
{
	int i, index, irq, bits, ret = 0;
	u8 cpu;
	u64 data, old_data;
	u64 coreisr, old_coreisr;
	gpa_t offset;

	data = *(u64 *)val;
	offset = addr - EIOINTC_BASE;

	switch (offset) {
	case EIOINTC_NODETYPE_START ... EIOINTC_NODETYPE_END:
		index = (offset - EIOINTC_NODETYPE_START) >> 3;
		s->nodetype.reg_u64[index] = data;
		break;
	case EIOINTC_IPMAP_START ... EIOINTC_IPMAP_END:
		/*
		 * ipmap cannot be set at runtime, can be set only at the beginning
		 * of irqchip driver, need not update upper irq level
		 */
		index = (offset - EIOINTC_IPMAP_START) >> 3;
		s->ipmap.reg_u64 = data;
		break;
	case EIOINTC_ENABLE_START ... EIOINTC_ENABLE_END:
		index = (offset - EIOINTC_ENABLE_START) >> 3;
		old_data = s->enable.reg_u64[index];
		s->enable.reg_u64[index] = data;
		/*
		 * 1: enable irq.
		 * update irq when isr is set.
		 */
		data = s->enable.reg_u64[index] & ~old_data & s->isr.reg_u64[index];
		for (i = 0; i < sizeof(data); i++) {
			u8 mask = (data >> (i * 8)) & 0xff;
			eiointc_enable_irq(vcpu, s, index * 8 + i, mask, 1);
		}
		/*
		 * 0: disable irq.
		 * update irq when isr is set.
		 */
		data = ~s->enable.reg_u64[index] & old_data & s->isr.reg_u64[index];
		for (i = 0; i < sizeof(data); i++) {
			u8 mask = (data >> (i * 8)) & 0xff;
			eiointc_enable_irq(vcpu, s, index * 8 + i, mask, 0);
		}
		break;
	case EIOINTC_BOUNCE_START ... EIOINTC_BOUNCE_END:
		/* do not emulate hw bounced irq routing */
		index = (offset - EIOINTC_BOUNCE_START) >> 3;
		s->bounce.reg_u64[index] = data;
		break;
	case EIOINTC_COREISR_START ... EIOINTC_COREISR_END:
		index = (offset - EIOINTC_COREISR_START) >> 3;
		/* use attrs to get current cpu index */
		cpu = vcpu->vcpu_id;
		coreisr = data;
		old_coreisr = s->coreisr.reg_u64[cpu][index];
		/* write 1 to clear interrupt */
		s->coreisr.reg_u64[cpu][index] = old_coreisr & ~coreisr;
		coreisr &= old_coreisr;
		bits = sizeof(data) * 8;
		irq = find_first_bit((void *)&coreisr, bits);
		while (irq < bits) {
			eiointc_update_irq(s, irq + index * bits, 0);
			bitmap_clear((void *)&coreisr, irq, 1);
			irq = find_first_bit((void *)&coreisr, bits);
		}
		break;
	case EIOINTC_COREMAP_START ... EIOINTC_COREMAP_END:
		irq = offset - EIOINTC_COREMAP_START;
		index = irq >> 3;
		s->coremap.reg_u64[index] = data;
		eiointc_update_sw_coremap(s, irq, data, sizeof(data), true);
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
	unsigned long flags;
	struct loongarch_eiointc *eiointc = vcpu->kvm->arch.eiointc;

	if (!eiointc) {
		kvm_err("%s: eiointc irqchip not valid!\n", __func__);
		return -EINVAL;
	}

	if (addr & (len - 1)) {
		kvm_err("%s: eiointc not aligned addr %llx len %d\n", __func__, addr, len);
		return -EINVAL;
	}

	vcpu->kvm->stat.eiointc_write_exits++;
	spin_lock_irqsave(&eiointc->lock, flags);
	switch (len) {
	case 1:
		ret = loongarch_eiointc_writeb(vcpu, eiointc, addr, len, val);
		break;
	case 2:
		ret = loongarch_eiointc_writew(vcpu, eiointc, addr, len, val);
		break;
	case 4:
		ret = loongarch_eiointc_writel(vcpu, eiointc, addr, len, val);
		break;
	case 8:
		ret = loongarch_eiointc_writeq(vcpu, eiointc, addr, len, val);
		break;
	default:
		WARN_ONCE(1, "%s: Abnormal address access: addr 0x%llx, size %d\n",
						__func__, addr, len);
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
	spin_lock_irqsave(&s->lock, flags);
	switch (type) {
	case KVM_DEV_LOONGARCH_EXTIOI_CTRL_INIT_NUM_CPU:
		if (copy_from_user(&val, data, 4))
			ret = -EFAULT;
		else {
			if (val >= EIOINTC_ROUTE_MAX_VCPUS)
				ret = -EINVAL;
			else
				s->num_cpu = val;
		}
		break;
	case KVM_DEV_LOONGARCH_EXTIOI_CTRL_INIT_FEATURE:
		if (copy_from_user(&s->features, data, 4))
			ret = -EFAULT;
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
					bool is_write)
{
	int addr, cpu, offset, ret = 0;
	unsigned long flags;
	void *p = NULL;
	void __user *data;
	struct loongarch_eiointc *s;

	s = dev->kvm->arch.eiointc;
	addr = attr->attr;
	cpu = addr >> 16;
	addr &= 0xffff;
	data = (void __user *)attr->addr;
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
	if (is_write) {
		if (copy_from_user(p, data, 4))
			ret = -EFAULT;
	} else {
		if (copy_to_user(data, p, 4))
			ret = -EFAULT;
	}
	spin_unlock_irqrestore(&s->lock, flags);

	return ret;
}

static int kvm_eiointc_sw_status_access(struct kvm_device *dev,
					struct kvm_device_attr *attr,
					bool is_write)
{
	int addr, ret = 0;
	unsigned long flags;
	void *p = NULL;
	void __user *data;
	struct loongarch_eiointc *s;

	s = dev->kvm->arch.eiointc;
	addr = attr->attr;
	addr &= 0xffff;

	data = (void __user *)attr->addr;
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
	if (is_write) {
		if (copy_from_user(p, data, 4))
			ret = -EFAULT;
	} else {
		if (copy_to_user(data, p, 4))
			ret = -EFAULT;
	}
	spin_unlock_irqrestore(&s->lock, flags);

	return ret;
}

static int kvm_eiointc_get_attr(struct kvm_device *dev,
				struct kvm_device_attr *attr)
{
	switch (attr->group) {
	case KVM_DEV_LOONGARCH_EXTIOI_GRP_REGS:
		return kvm_eiointc_regs_access(dev, attr, false);
	case KVM_DEV_LOONGARCH_EXTIOI_GRP_SW_STATUS:
		return kvm_eiointc_sw_status_access(dev, attr, false);
	default:
		return -EINVAL;
	}
}

static int kvm_eiointc_set_attr(struct kvm_device *dev,
				struct kvm_device_attr *attr)
{
	switch (attr->group) {
	case KVM_DEV_LOONGARCH_EXTIOI_GRP_CTRL:
		return kvm_eiointc_ctrl_access(dev, attr);
	case KVM_DEV_LOONGARCH_EXTIOI_GRP_REGS:
		return kvm_eiointc_regs_access(dev, attr, true);
	case KVM_DEV_LOONGARCH_EXTIOI_GRP_SW_STATUS:
		return kvm_eiointc_sw_status_access(dev, attr, true);
	default:
		return -EINVAL;
	}
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
