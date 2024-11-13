/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 Loongson Technology Corporation Limited
 */

#ifndef __ASM_KVM_EIOINTC_H
#define __ASM_KVM_EIOINTC_H

#include <kvm/iodev.h>

#define EIOINTC_IRQS			256
#define EIOINTC_ROUTE_MAX_VCPUS		256
#define EIOINTC_IRQS_U8_NUMS		(EIOINTC_IRQS / 8)
#define EIOINTC_IRQS_U16_NUMS		(EIOINTC_IRQS_U8_NUMS / 2)
#define EIOINTC_IRQS_U32_NUMS		(EIOINTC_IRQS_U8_NUMS / 4)
#define EIOINTC_IRQS_U64_NUMS		(EIOINTC_IRQS_U8_NUMS / 8)
/* map to ipnum per 32 irqs */
#define EIOINTC_IRQS_NODETYPE_COUNT	16

#define EIOINTC_BASE			0x1400
#define EIOINTC_SIZE			0x900

#define EIOINTC_VIRT_BASE		(0x40000000)
#define EIOINTC_VIRT_SIZE		(0x1000)

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
	union nodetype {
		u64 reg_u64[EIOINTC_IRQS_NODETYPE_COUNT / 4];
		u32 reg_u32[EIOINTC_IRQS_NODETYPE_COUNT / 2];
		u16 reg_u16[EIOINTC_IRQS_NODETYPE_COUNT];
		u8 reg_u8[EIOINTC_IRQS_NODETYPE_COUNT * 2];
	} nodetype;

	/* one bit shows the state of one irq */
	union bounce {
		u64 reg_u64[EIOINTC_IRQS_U64_NUMS];
		u32 reg_u32[EIOINTC_IRQS_U32_NUMS];
		u16 reg_u16[EIOINTC_IRQS_U16_NUMS];
		u8 reg_u8[EIOINTC_IRQS_U8_NUMS];
	} bounce;

	union isr {
		u64 reg_u64[EIOINTC_IRQS_U64_NUMS];
		u32 reg_u32[EIOINTC_IRQS_U32_NUMS];
		u16 reg_u16[EIOINTC_IRQS_U16_NUMS];
		u8 reg_u8[EIOINTC_IRQS_U8_NUMS];
	} isr;
	union coreisr {
		u64 reg_u64[EIOINTC_ROUTE_MAX_VCPUS][EIOINTC_IRQS_U64_NUMS];
		u32 reg_u32[EIOINTC_ROUTE_MAX_VCPUS][EIOINTC_IRQS_U32_NUMS];
		u16 reg_u16[EIOINTC_ROUTE_MAX_VCPUS][EIOINTC_IRQS_U16_NUMS];
		u8 reg_u8[EIOINTC_ROUTE_MAX_VCPUS][EIOINTC_IRQS_U8_NUMS];
	} coreisr;
	union enable {
		u64 reg_u64[EIOINTC_IRQS_U64_NUMS];
		u32 reg_u32[EIOINTC_IRQS_U32_NUMS];
		u16 reg_u16[EIOINTC_IRQS_U16_NUMS];
		u8 reg_u8[EIOINTC_IRQS_U8_NUMS];
	} enable;

	/* use one byte to config ipmap for 32 irqs at once */
	union ipmap {
		u64 reg_u64;
		u32 reg_u32[EIOINTC_IRQS_U32_NUMS / 4];
		u16 reg_u16[EIOINTC_IRQS_U16_NUMS / 4];
		u8 reg_u8[EIOINTC_IRQS_U8_NUMS / 4];
	} ipmap;
	/* use one byte to config coremap for one irq */
	union coremap {
		u64 reg_u64[EIOINTC_IRQS / 8];
		u32 reg_u32[EIOINTC_IRQS / 4];
		u16 reg_u16[EIOINTC_IRQS / 2];
		u8 reg_u8[EIOINTC_IRQS];
	} coremap;

	DECLARE_BITMAP(sw_coreisr[EIOINTC_ROUTE_MAX_VCPUS][LOONGSON_IP_NUM], EIOINTC_IRQS);
	uint8_t  sw_coremap[EIOINTC_IRQS];
};

int kvm_loongarch_register_eiointc_device(void);

#endif /* __ASM_KVM_EIOINTC_H */
