/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 Loongson Technology Corporation Limited
 */

#ifndef __ASM_KVM_PCH_PIC_H
#define __ASM_KVM_PCH_PIC_H

#include <kvm/iodev.h>

#define PCH_PIC_SIZE			0x3e8

#define PCH_PIC_INT_ID_START		0x0
#define PCH_PIC_INT_ID_END		0x7
#define PCH_PIC_MASK_START		0x20
#define PCH_PIC_MASK_END		0x27
#define PCH_PIC_HTMSI_EN_START		0x40
#define PCH_PIC_HTMSI_EN_END		0x47
#define PCH_PIC_EDGE_START		0x60
#define PCH_PIC_EDGE_END		0x67
#define PCH_PIC_CLEAR_START		0x80
#define PCH_PIC_CLEAR_END		0x87
#define PCH_PIC_AUTO_CTRL0_START	0xc0
#define PCH_PIC_AUTO_CTRL0_END		0xc7
#define PCH_PIC_AUTO_CTRL1_START	0xe0
#define PCH_PIC_AUTO_CTRL1_END		0xe7
#define PCH_PIC_ROUTE_ENTRY_START	0x100
#define PCH_PIC_ROUTE_ENTRY_END		0x13f
#define PCH_PIC_HTMSI_VEC_START		0x200
#define PCH_PIC_HTMSI_VEC_END		0x23f
#define PCH_PIC_INT_IRR_START		0x380
#define PCH_PIC_INT_IRR_END		0x38f
#define PCH_PIC_INT_ISR_START		0x3a0
#define PCH_PIC_INT_ISR_END		0x3af
#define PCH_PIC_POLARITY_START		0x3e0
#define PCH_PIC_POLARITY_END		0x3e7
#define PCH_PIC_INT_ID_VAL		0x7000000UL
#define PCH_PIC_INT_ID_VER		0x1UL

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
void pch_pic_set_irq(struct loongarch_pch_pic *s, int irq, int level);
void pch_msi_set_irq(struct kvm *kvm, int irq, int level);

#endif /* __ASM_KVM_PCH_PIC_H */
