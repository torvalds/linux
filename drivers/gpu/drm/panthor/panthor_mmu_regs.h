/* SPDX-License-Identifier: GPL-2.0 or MIT */
/* Copyright 2026 ARM Limited. All rights reserved. */

#ifndef __PANTHOR_MMU_REGS_H__
#define __PANTHOR_MMU_REGS_H__

/* MMU regs */

#define MMU_INT_BASE					0x2000

/* AS_COMMAND register commands */

#define MMU_AS_BASE					0x2400

#define MMU_AS_SHIFT					6
#define MMU_AS(as)					((as) << MMU_AS_SHIFT)

#define AS_TRANSTAB(as)					(MMU_AS(as) + 0x0)
#define AS_MEMATTR(as)					(MMU_AS(as) + 0x8)
#define   AS_MEMATTR_AARCH64_INNER_ALLOC_IMPL		(2 << 2)
#define   AS_MEMATTR_AARCH64_INNER_ALLOC_EXPL(w, r)	((3 << 2) | \
							 ((w) ? BIT(0) : 0) | \
							 ((r) ? BIT(1) : 0))
#define   AS_MEMATTR_AARCH64_SH_MIDGARD_INNER		(0 << 4)
#define   AS_MEMATTR_AARCH64_SH_CPU_INNER		(1 << 4)
#define   AS_MEMATTR_AARCH64_SH_CPU_INNER_SHADER_COH	(2 << 4)
#define   AS_MEMATTR_AARCH64_SHARED			(0 << 6)
#define   AS_MEMATTR_AARCH64_INNER_OUTER_NC		(1 << 6)
#define   AS_MEMATTR_AARCH64_INNER_OUTER_WB		(2 << 6)
#define   AS_MEMATTR_AARCH64_FAULT			(3 << 6)
#define AS_LOCKADDR(as)					(MMU_AS(as) + 0x10)
#define AS_COMMAND(as)					(MMU_AS(as) + 0x18)
#define   AS_COMMAND_NOP				0
#define   AS_COMMAND_UPDATE				1
#define   AS_COMMAND_LOCK				2
#define   AS_COMMAND_UNLOCK				3
#define   AS_COMMAND_FLUSH_PT				4
#define   AS_COMMAND_FLUSH_MEM				5
#define   AS_LOCK_REGION_MIN_SIZE			(1ULL << 15)
#define AS_FAULTSTATUS(as)				(MMU_AS(as) + 0x1C)
#define  AS_FAULTSTATUS_ACCESS_TYPE_MASK		(0x3 << 8)
#define  AS_FAULTSTATUS_ACCESS_TYPE_ATOMIC		(0x0 << 8)
#define  AS_FAULTSTATUS_ACCESS_TYPE_EX			(0x1 << 8)
#define  AS_FAULTSTATUS_ACCESS_TYPE_READ		(0x2 << 8)
#define  AS_FAULTSTATUS_ACCESS_TYPE_WRITE		(0x3 << 8)
#define AS_FAULTADDRESS(as)				(MMU_AS(as) + 0x20)
#define AS_STATUS(as)					(MMU_AS(as) + 0x28)
#define   AS_STATUS_AS_ACTIVE				BIT(0)
#define AS_TRANSCFG(as)					(MMU_AS(as) + 0x30)
#define   AS_TRANSCFG_ADRMODE_UNMAPPED			(1 << 0)
#define   AS_TRANSCFG_ADRMODE_IDENTITY			(2 << 0)
#define   AS_TRANSCFG_ADRMODE_AARCH64_4K		(6 << 0)
#define   AS_TRANSCFG_ADRMODE_AARCH64_64K		(8 << 0)
#define   AS_TRANSCFG_INA_BITS(x)			((x) << 6)
#define   AS_TRANSCFG_OUTA_BITS(x)			((x) << 14)
#define   AS_TRANSCFG_SL_CONCAT				BIT(22)
#define   AS_TRANSCFG_PTW_MEMATTR_NC			(1 << 24)
#define   AS_TRANSCFG_PTW_MEMATTR_WB			(2 << 24)
#define   AS_TRANSCFG_PTW_SH_NS				(0 << 28)
#define   AS_TRANSCFG_PTW_SH_OS				(2 << 28)
#define   AS_TRANSCFG_PTW_SH_IS				(3 << 28)
#define   AS_TRANSCFG_PTW_RA				BIT(30)
#define   AS_TRANSCFG_DISABLE_HIER_AP			BIT(33)
#define   AS_TRANSCFG_DISABLE_AF_FAULT			BIT(34)
#define   AS_TRANSCFG_WXN				BIT(35)
#define   AS_TRANSCFG_XREADABLE				BIT(36)
#define AS_FAULTEXTRA(as)				(MMU_AS(as) + 0x38)

#endif /* __PANTHOR_MMU_REGS_H__ */
