/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 Loongson Technology Corporation Limited
 */

#ifndef __ASM_KVM_EIOINTC_H
#define __ASM_KVM_EIOINTC_H

#include <kvm/iodev.h>

#define EIOINTC_IRQS			256
#define EIOINTC_ROUTE_MAX_VCPUS		256
#define EIOINTC_IRQS_U64_NUMS		(EIOINTC_IRQS / 64)
/* map to ipnum per 32 irqs */
#define EIOINTC_IRQS_NODETYPE_COUNT	16

#define EIOINTC_BASE			0x1400
#define EIOINTC_SIZE			0x900

#define EIOINTC_NODETYPE_START		0xa0
#define EIOINTC_NODETYPE_END		0xbf
#define EIOINTC_IPMAP_START		0xc0
#define EIOINTC_IPMAP_END		0xc7
#define EIOINTC_ENABLE_START		0x200
#define EIOINTC_ENABLE_END		0x21f
#define EIOINTC_BOUNCE_START		0x280
#define EIOINTC_BOUNCE_END		0x29f
#define EIOINTC_ISR_START		0x300
#define EIOINTC_ISR_END			0x31f
#define EIOINTC_COREISR_START		0x400
#define EIOINTC_COREISR_END		0x41f
#define EIOINTC_COREMAP_START		0x800
#define EIOINTC_COREMAP_END		0x8ff

#define EIOINTC_VIRT_BASE		(0x40000000)
#define EIOINTC_VIRT_SIZE		(0x1000)

#define EIOINTC_VIRT_FEATURES		(0x0)
#define EIOINTC_HAS_VIRT_EXTENSION	(0)
#define EIOINTC_HAS_ENABLE_OPTION	(1)
#define EIOINTC_HAS_INT_ENCODE		(2)
#define EIOINTC_HAS_CPU_ENCODE		(3)
#define EIOINTC_VIRT_HAS_FEATURES	((1U << EIOINTC_HAS_VIRT_EXTENSION) \
					| (1U << EIOINTC_HAS_ENABLE_OPTION) \
					| (1U << EIOINTC_HAS_INT_ENCODE)    \
					| (1U << EIOINTC_HAS_CPU_ENCODE))
#define EIOINTC_VIRT_CONFIG		(0x4)
#define EIOINTC_ENABLE			(1)
#define EIOINTC_ENABLE_INT_ENCODE	(2)
#define EIOINTC_ENABLE_CPU_ENCODE	(3)

#define LOONGSON_IP_NUM			8

struct loongarch_eiointc {
	spinlock_t lock;
	struct kvm *kvm;
	struct kvm_io_device device;
	struct kvm_io_device device_vext;
	uint32_t num_cpu;
	uint32_t features;
	uint32_t status;

	/* hardware state */
	u64 nodetype[EIOINTC_IRQS_NODETYPE_COUNT / 4];

	/* one bit shows the state of one irq */
	u64 bounce[EIOINTC_IRQS_U64_NUMS];
	u64 isr[EIOINTC_IRQS_U64_NUMS];
	u64 coreisr[EIOINTC_ROUTE_MAX_VCPUS][EIOINTC_IRQS_U64_NUMS];
	u64 enable[EIOINTC_IRQS_U64_NUMS];

	/* use one byte to config ipmap for 32 irqs at once */
	u64 ipmap;
	/* use one byte to config coremap for one irq */
	u64 coremap[EIOINTC_IRQS / 8];

	DECLARE_BITMAP(sw_coreisr[EIOINTC_ROUTE_MAX_VCPUS][LOONGSON_IP_NUM], EIOINTC_IRQS);
	uint8_t  sw_coremap[EIOINTC_IRQS];
};

int kvm_loongarch_register_eiointc_device(void);
void eiointc_set_irq(struct loongarch_eiointc *s, int irq, int level);

#endif /* __ASM_KVM_EIOINTC_H */
