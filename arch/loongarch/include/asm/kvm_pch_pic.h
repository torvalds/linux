/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 Loongson Technology Corporation Limited
 */

#ifndef __ASM_KVM_PCH_PIC_H
#define __ASM_KVM_PCH_PIC_H

#include <kvm/iodev.h>

struct loongarch_pch_pic {
	spinlock_t lock;
	struct kvm *kvm;
	struct kvm_io_device device;
	uint64_t mask; /* 1:disable irq, 0:enable irq */
	uint64_t htmsi_en; /* 1:msi */
	uint64_t edge; /* 1:edge triggered, 0:level triggered */
	uint64_t auto_ctrl0; /* only use default value 00b */
	uint64_t auto_ctrl1; /* only use default value 00b */
	uint64_t last_intirr; /* edge detection */
	uint64_t irr; /* interrupt request register */
	uint64_t isr; /* interrupt service register */
	uint64_t polarity; /* 0: high level trigger, 1: low level trigger */
	uint8_t  route_entry[64]; /* default value 0, route to int0: eiointc */
	uint8_t  htmsi_vector[64]; /* irq route table for routing to eiointc */
	uint64_t pch_pic_base;
};

int kvm_loongarch_register_pch_pic_device(void);

#endif /* __ASM_KVM_PCH_PIC_H */
