/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2021 - Google LLC
 * Author: David Brazdil <dbrazdil@google.com>
 */

#ifndef __ARM64_KVM_S2MPU_H__
#define __ARM64_KVM_S2MPU_H__

#include <linux/bitfield.h>

#define S2MPU_MMIO_SIZE				SZ_64K

#define NR_VIDS					8
#define NR_CTX_IDS				8

#define ALL_VIDS_BITMAP				GENMASK(NR_VIDS - 1, 0)

#define REG_NS_CTRL0				0x0
#define REG_NS_CTRL1				0x4
#define REG_NS_CFG				0x10
#define REG_NS_INTERRUPT_ENABLE_PER_VID_SET	0x20
#define REG_NS_INTERRUPT_CLEAR			0x2c
#define REG_NS_VERSION				0x60
#define REG_NS_NUM_CONTEXT			0x100
#define REG_NS_CONTEXT_CFG_VALID_VID		0x104
#define REG_NS_ALL_INVALIDATION			0x1000
#define REG_NS_FAULT_STATUS			0x2000
#define REG_NS_FAULT_PA_LOW(vid)		(0x2004 + ((vid) * 0x20))
#define REG_NS_FAULT_PA_HIGH(vid)		(0x2008 + ((vid) * 0x20))
#define REG_NS_FAULT_INFO(vid)			(0x2010 + ((vid) * 0x20))
#define REG_NS_L1ENTRY_ATTR(vid, gb)		(0x4004 + ((vid) * 0x200) + ((gb) * 0x8))

#define CTRL0_ENABLE				BIT(0)
#define CTRL0_INTERRUPT_ENABLE			BIT(1)
#define CTRL0_FAULT_RESP_TYPE_SLVERR		BIT(2) /* for v8 */
#define CTRL0_FAULT_RESP_TYPE_DECERR		BIT(2) /* for v9 */

#define CTRL1_DISABLE_CHK_S1L1PTW		BIT(0)
#define CTRL1_DISABLE_CHK_S1L2PTW		BIT(1)
#define CTRL1_ENABLE_PAGE_SIZE_AWARENESS	BIT(2)
#define CTRL1_DISABLE_CHK_USER_MATCHED_REQ	BIT(3)

#define CFG_MPTW_CACHE_OVERRIDE			BIT(0)
#define CFG_MPTW_QOS_OVERRIDE			BIT(8)
#define CFG_MPTW_SHAREABLE			BIT(16)

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

#define INVALIDATION_INVALIDATE			BIT(0)

#define NR_FAULT_INFO_REGS			8
#define FAULT_INFO_VID_MASK			GENMASK(26, 24)
#define FAULT_INFO_TYPE_MASK			GENMASK(23, 21)
#define FAULT_INFO_TYPE_CONTEXT			0x4 /* v9 only */
#define FAULT_INFO_TYPE_AP			0x2
#define FAULT_INFO_TYPE_MPTW			0x1
#define FAULT_INFO_RW_BIT			BIT(20)
#define FAULT_INFO_LEN_MASK			GENMASK(19, 16)
#define FAULT_INFO_ID_MASK			GENMASK(15, 0)

#define L1ENTRY_ATTR_PROT(prot)			FIELD_PREP(GENMASK(2, 1), prot)
#define L1ENTRY_ATTR_1G(prot)			L1ENTRY_ATTR_PROT(prot)

#define NR_GIGABYTES				64
#define RO_GIGABYTES_FIRST			4
#define RO_GIGABYTES_LAST			33

/*
 * Iterate over S2MPU gigabyte regions. Skip those that cannot be modified
 * (the MMIO registers are read only, with reset value MPT_PROT_NONE).
 */
#define for_each_gb_in_range(i, first, last) \
	for ((i) = (first); (i) <= (last) && (i) < NR_GIGABYTES; \
	     (i) = (((i) + 1 == RO_GIGABYTES_FIRST) ? RO_GIGABYTES_LAST : (i)) + 1)

#define for_each_gb(i)			for_each_gb_in_range(i, 0, NR_GIGABYTES - 1)
#define for_each_vid(i)			for ((i) = 0; (i) < NR_VIDS; (i)++)
#define for_each_gb_and_vid(gb, vid)	for_each_vid((vid)) for_each_gb((gb))

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

enum mpt_prot {
	MPT_PROT_NONE	= 0,
	MPT_PROT_R	= BIT(0),
	MPT_PROT_W	= BIT(1),
	MPT_PROT_RW	= MPT_PROT_R | MPT_PROT_W,
	MPT_PROT_MASK	= MPT_PROT_RW,
};

extern size_t kvm_nvhe_sym(kvm_hyp_nr_s2mpus);
#define kvm_hyp_nr_s2mpus kvm_nvhe_sym(kvm_hyp_nr_s2mpus)

extern struct s2mpu *kvm_nvhe_sym(kvm_hyp_s2mpus);
#define kvm_hyp_s2mpus kvm_nvhe_sym(kvm_hyp_s2mpus)

#endif /* __ARM64_KVM_S2MPU_H__ */
