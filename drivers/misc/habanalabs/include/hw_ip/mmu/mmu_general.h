/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2016-2020 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef INCLUDE_MMU_GENERAL_H_
#define INCLUDE_MMU_GENERAL_H_

#define PAGE_SHIFT_4KB			12
#define PAGE_SHIFT_2MB			21
#define PAGE_SIZE_2MB			(_AC(1, UL) << PAGE_SHIFT_2MB)
#define PAGE_SIZE_4KB			(_AC(1, UL) << PAGE_SHIFT_4KB)

#define PAGE_PRESENT_MASK		0x0000000000001ull
#define SWAP_OUT_MASK			0x0000000000004ull
#define LAST_MASK			0x0000000000800ull
#define FLAGS_MASK			0x0000000000FFFull

#define MMU_ARCH_5_HOPS			5

#define HOP_PHYS_ADDR_MASK		(~FLAGS_MASK)

#define HL_PTE_SIZE			sizeof(u64)

/* definitions for HOP with 512 PTE entries */
#define HOP_PTE_ENTRIES_512		512
#define HOP_TABLE_SIZE_512_PTE		(HOP_PTE_ENTRIES_512 * HL_PTE_SIZE)
#define HOP0_512_PTE_TABLES_TOTAL_SIZE	(HOP_TABLE_SIZE_512_PTE * MAX_ASID)

#define MMU_HOP0_PA43_12_SHIFT		12
#define MMU_HOP0_PA49_44_SHIFT		(12 + 32)

#define MMU_CONFIG_TIMEOUT_USEC		2000 /* 2 ms */

enum mmu_hop_num {
	MMU_HOP0,
	MMU_HOP1,
	MMU_HOP2,
	MMU_HOP3,
	MMU_HOP4,
	MMU_HOP5,
	MMU_HOP_MAX,
};

#endif /* INCLUDE_MMU_GENERAL_H_ */
