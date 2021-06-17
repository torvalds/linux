/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 - Google LLC
 * Author: David Brazdil <dbrazdil@google.com>
 */

#ifndef __ARM64_KVM_S2MPU_H__
#define __ARM64_KVM_S2MPU_H__

#include <linux/bitfield.h>

#define NR_VIDS					8
#define NR_CTX_IDS				8

#define ALL_VIDS_BITMAP				GENMASK(NR_VIDS - 1, 0)

#define REG_NS_INTERRUPT_CLEAR			0x2c
#define REG_NS_VERSION				0x60
#define REG_NS_NUM_CONTEXT			0x100
#define REG_NS_FAULT_STATUS			0x2000
#define REG_NS_FAULT_PA_LOW(vid)		(0x2004 + ((vid) * 0x20))
#define REG_NS_FAULT_PA_HIGH(vid)		(0x2008 + ((vid) * 0x20))
#define REG_NS_FAULT_INFO(vid)			(0x2010 + ((vid) * 0x20))

/* For use with hi_lo_readq_relaxed(). */
#define REG_NS_FAULT_PA_HIGH_LOW(vid)		REG_NS_FAULT_PA_LOW(vid)

#define VERSION_MAJOR_ARCH_VER_MASK		GENMASK(31, 28)
#define VERSION_MINOR_ARCH_VER_MASK		GENMASK(27, 24)
#define VERSION_REV_ARCH_VER_MASK		GENMASK(23, 16)
#define VERSION_RTL_VER_MASK			GENMASK(7, 0)

/* Ignore RTL version in driver version check. */
#define VERSION_CHECK_MASK			(VERSION_MAJOR_ARCH_VER_MASK | \
						 VERSION_MINOR_ARCH_VER_MASK | \
						 VERSION_REV_ARCH_VER_MASK)

#define NUM_CONTEXT_MASK			GENMASK(3, 0)

#define CONTEXT_CFG_VALID_VID_CTX_VALID(ctx)	BIT((4 * (ctx)) + 3)
#define CONTEXT_CFG_VALID_VID_CTX_VID(ctx, vid)	\
		FIELD_PREP(GENMASK((4 * (ctx) + 2), 4 * (ctx)), (vid))

#define NR_FAULT_INFO_REGS			8
#define FAULT_INFO_VID_MASK			GENMASK(26, 24)
#define FAULT_INFO_TYPE_MASK			GENMASK(23, 21)
#define FAULT_INFO_TYPE_CONTEXT			0x4 /* v9 only */
#define FAULT_INFO_TYPE_AP			0x2
#define FAULT_INFO_TYPE_MPTW			0x1
#define FAULT_INFO_RW_BIT			BIT(20)
#define FAULT_INFO_LEN_MASK			GENMASK(19, 16)
#define FAULT_INFO_ID_MASK			GENMASK(15, 0)

enum s2mpu_version {
	S2MPU_VERSION_8 = 0x11000000,
	S2MPU_VERSION_9 = 0x20000000,
};

enum s2mpu_power_state {
	S2MPU_POWER_ALWAYS_ON = 0,
	S2MPU_POWER_ON,
	S2MPU_POWER_OFF,
};

struct s2mpu {
	phys_addr_t pa;
	void __iomem *va;
	u32 version;
	enum s2mpu_power_state power_state;
	u32 power_domain_id;
	u32 context_cfg_valid_vid;
};

extern size_t kvm_nvhe_sym(kvm_hyp_nr_s2mpus);
#define kvm_hyp_nr_s2mpus kvm_nvhe_sym(kvm_hyp_nr_s2mpus)

extern struct s2mpu *kvm_nvhe_sym(kvm_hyp_s2mpus);
#define kvm_hyp_s2mpus kvm_nvhe_sym(kvm_hyp_s2mpus)

#endif /* __ARM64_KVM_S2MPU_H__ */
