// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Loongson-3 Virtual IPI interrupt support.
 *
 * Copyright (C) 2019  Loongson Technologies, Inc.  All rights reserved.
 *
 * Authors: Chen Zhu <zhuchen@loongson.cn>
 * Authors: Huacai Chen <chenhc@lemote.com>
 */

#include <linux/kvm_host.h>

#include "interrupt.h"

#define IPI_BASE            0x3ff01000ULL

#define CORE0_STATUS_OFF       0x000
#define CORE0_EN_OFF           0x004
#define CORE0_SET_OFF          0x008
#define CORE0_CLEAR_OFF        0x00c
#define CORE0_BUF_20           0x020
#define CORE0_BUF_28           0x028
#define CORE0_BUF_30           0x030
#define CORE0_BUF_38           0x038

#define CORE1_STATUS_OFF       0x100
#define CORE1_EN_OFF           0x104
#define CORE1_SET_OFF          0x108
#define CORE1_CLEAR_OFF        0x10c
#define CORE1_BUF_20           0x120
#define CORE1_BUF_28           0x128
#define CORE1_BUF_30           0x130
#define CORE1_BUF_38           0x138

#define CORE2_STATUS_OFF       0x200
#define CORE2_EN_OFF           0x204
#define CORE2_SET_OFF          0x208
#define CORE2_CLEAR_OFF        0x20c
#define CORE2_BUF_20           0x220
#define CORE2_BUF_28           0x228
#define CORE2_BUF_30           0x230
#define CORE2_BUF_38           0x238

#define CORE3_STATUS_OFF       0x300
#define CORE3_EN_OFF           0x304
#define CORE3_SET_OFF          0x308
#define CORE3_CLEAR_OFF        0x30c
#define CORE3_BUF_20           0x320
#define CORE3_BUF_28           0x328
#define CORE3_BUF_30           0x330
#define CORE3_BUF_38           0x338

static int loongson_vipi_read(struct loongson_kvm_ipi *ipi,
				gpa_t addr, int len, void *val)
{
	uint32_t core = (addr >> 8) & 3;
	uint32_t node = (addr >> 44) & 3;
	uint32_t id = core + node * 4;
	uint64_t offset = addr & 0xff;
	void *pbuf;
	struct ipi_state *s = &(ipi->ipistate[id]);

	BUG_ON(offset & (len - 1));

	switch (offset) {
	case CORE0_STATUS_OFF:
		*(uint64_t *)val = s->status;
		break;

	case CORE0_EN_OFF:
		*(uint64_t *)val = s->en;
		break;

	case CORE0_SET_OFF:
		*(uint64_t *)val = 0;
		break;

	case CORE0_CLEAR_OFF:
		*(uint64_t *)val = 0;
		break;

	case CORE0_BUF_20 ... CORE0_BUF_38:
		pbuf = (void *)s->buf + (offset - 0x20);
		if (len == 8)
			*(uint64_t *)val = *(uint64_t *)pbuf;
		else /* Assume len == 4 */
			*(uint32_t *)val = *(uint32_t *)pbuf;
		break;

	default:
		pr_notice("%s with unknown addr %llx\n", __func__, addr);
		break;
	}

	return 0;
}

static int loongson_vipi_write(struct loongson_kvm_ipi *ipi,
				gpa_t addr, int len, const void *val)
{
	uint32_t core = (addr >> 8) & 3;
	uint32_t node = (addr >> 44) & 3;
	uint32_t id = core + node * 4;
	uint64_t data, offset = addr & 0xff;
	void *pbuf;
	struct kvm *kvm = ipi->kvm;
	struct kvm_mips_interrupt irq;
	struct ipi_state *s = &(ipi->ipistate[id]);

	data = *(uint64_t *)val;
	BUG_ON(offset & (len - 1));

	switch (offset) {
	case CORE0_STATUS_OFF:
		break;

	case CORE0_EN_OFF:
		s->en = data;
		break;

	case CORE0_SET_OFF:
		s->status |= data;
		irq.cpu = id;
		irq.irq = 6;
		kvm_vcpu_ioctl_interrupt(kvm_get_vcpu(kvm, id), &irq);
		break;

	case CORE0_CLEAR_OFF:
		s->status &= ~data;
		if (!s->status) {
			irq.cpu = id;
			irq.irq = -6;
			kvm_vcpu_ioctl_interrupt(kvm_get_vcpu(kvm, id), &irq);
		}
		break;

	case CORE0_BUF_20 ... CORE0_BUF_38:
		pbuf = (void *)s->buf + (offset - 0x20);
		if (len == 8)
			*(uint64_t *)pbuf = (uint64_t)data;
		else /* Assume len == 4 */
			*(uint32_t *)pbuf = (uint32_t)data;
		break;

	default:
		pr_notice("%s with unknown addr %llx\n", __func__, addr);
		break;
	}

	return 0;
}

static int kvm_ipi_read(struct kvm_vcpu *vcpu, struct kvm_io_device *dev,
			gpa_t addr, int len, void *val)
{
	unsigned long flags;
	struct loongson_kvm_ipi *ipi;
	struct ipi_io_device *ipi_device;

	ipi_device = container_of(dev, struct ipi_io_device, device);
	ipi = ipi_device->ipi;

	spin_lock_irqsave(&ipi->lock, flags);
	loongson_vipi_read(ipi, addr, len, val);
	spin_unlock_irqrestore(&ipi->lock, flags);

	return 0;
}

static int kvm_ipi_write(struct kvm_vcpu *vcpu, struct kvm_io_device *dev,
			gpa_t addr, int len, const void *val)
{
	unsigned long flags;
	struct loongson_kvm_ipi *ipi;
	struct ipi_io_device *ipi_device;

	ipi_device = container_of(dev, struct ipi_io_device, device);
	ipi = ipi_device->ipi;

	spin_lock_irqsave(&ipi->lock, flags);
	loongson_vipi_write(ipi, addr, len, val);
	spin_unlock_irqrestore(&ipi->lock, flags);

	return 0;
}

static const struct kvm_io_device_ops kvm_ipi_ops = {
	.read     = kvm_ipi_read,
	.write    = kvm_ipi_write,
};

void kvm_init_loongson_ipi(struct kvm *kvm)
{
	int i;
	unsigned long addr;
	struct loongson_kvm_ipi *s;
	struct kvm_io_device *device;

	s = &kvm->arch.ipi;
	s->kvm = kvm;
	spin_lock_init(&s->lock);

	/*
	 * Initialize IPI device
	 */
	for (i = 0; i < 4; i++) {
		device = &s->dev_ipi[i].device;
		kvm_iodevice_init(device, &kvm_ipi_ops);
		addr = (((unsigned long)i) << 44) + IPI_BASE;
		mutex_lock(&kvm->slots_lock);
		kvm_io_bus_register_dev(kvm, KVM_MMIO_BUS, addr, 0x400, device);
		mutex_unlock(&kvm->slots_lock);
		s->dev_ipi[i].ipi = s;
		s->dev_ipi[i].node_id = i;
	}
}
