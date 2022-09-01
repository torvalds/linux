/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef __ASM_TLB_H
#define __ASM_TLB_H

#include <linux/mm_types.h>
#include <asm/cpu-features.h>
#include <asm/loongarch.h>

/*
 * TLB Invalidate Flush
 */
static inline void tlbclr(void)
{
	__asm__ __volatile__("tlbclr");
}

static inline void tlbflush(void)
{
	__asm__ __volatile__("tlbflush");
}

/*
 * TLB R/W operations.
 */
static inline void tlb_probe(void)
{
	__asm__ __volatile__("tlbsrch");
}

static inline void tlb_read(void)
{
	__asm__ __volatile__("tlbrd");
}

static inline void tlb_write_indexed(void)
{
	__asm__ __volatile__("tlbwr");
}

static inline void tlb_write_random(void)
{
	__asm__ __volatile__("tlbfill");
}

enum invtlb_ops {
	/* Invalid all tlb */
	INVTLB_ALL = 0x0,
	/* Invalid current tlb */
	INVTLB_CURRENT_ALL = 0x1,
	/* Invalid all global=1 lines in current tlb */
	INVTLB_CURRENT_GTRUE = 0x2,
	/* Invalid all global=0 lines in current tlb */
	INVTLB_CURRENT_GFALSE = 0x3,
	/* Invalid global=0 and matched asid lines in current tlb */
	INVTLB_GFALSE_AND_ASID = 0x4,
	/* Invalid addr with global=0 and matched asid in current tlb */
	INVTLB_ADDR_GFALSE_AND_ASID = 0x5,
	/* Invalid addr with global=1 or matched asid in current tlb */
	INVTLB_ADDR_GTRUE_OR_ASID = 0x6,
	/* Invalid matched gid in guest tlb */
	INVGTLB_GID = 0x9,
	/* Invalid global=1, matched gid in guest tlb */
	INVGTLB_GID_GTRUE = 0xa,
	/* Invalid global=0, matched gid in guest tlb */
	INVGTLB_GID_GFALSE = 0xb,
	/* Invalid global=0, matched gid and asid in guest tlb */
	INVGTLB_GID_GFALSE_ASID = 0xc,
	/* Invalid global=0 , matched gid, asid and addr in guest tlb */
	INVGTLB_GID_GFALSE_ASID_ADDR = 0xd,
	/* Invalid global=1 , matched gid, asid and addr in guest tlb */
	INVGTLB_GID_GTRUE_ASID_ADDR = 0xe,
	/* Invalid all gid gva-->gpa guest tlb */
	INVGTLB_ALLGID_GVA_TO_GPA = 0x10,
	/* Invalid all gid gpa-->hpa tlb */
	INVTLB_ALLGID_GPA_TO_HPA = 0x11,
	/* Invalid all gid tlb, including  gva-->gpa and gpa-->hpa */
	INVTLB_ALLGID = 0x12,
	/* Invalid matched gid gva-->gpa guest tlb */
	INVGTLB_GID_GVA_TO_GPA = 0x13,
	/* Invalid matched gid gpa-->hpa tlb */
	INVTLB_GID_GPA_TO_HPA = 0x14,
	/* Invalid matched gid tlb,including gva-->gpa and gpa-->hpa */
	INVTLB_GID_ALL = 0x15,
	/* Invalid matched gid and addr gpa-->hpa tlb */
	INVTLB_GID_ADDR = 0x16,
};

/*
 * invtlb op info addr
 * (0x1 << 26) | (0x24 << 20) | (0x13 << 15) |
 * (addr << 10) | (info << 5) | op
 */
static inline void invtlb(u32 op, u32 info, u64 addr)
{
	__asm__ __volatile__(
		"parse_r addr,%0\n\t"
		"parse_r info,%1\n\t"
		".word ((0x6498000) | (addr << 10) | (info << 5) | %2)\n\t"
		:
		: "r"(addr), "r"(info), "i"(op)
		:
		);
}

static inline void invtlb_addr(u32 op, u32 info, u64 addr)
{
	__asm__ __volatile__(
		"parse_r addr,%0\n\t"
		".word ((0x6498000) | (addr << 10) | (0 << 5) | %1)\n\t"
		:
		: "r"(addr), "i"(op)
		:
		);
}

static inline void invtlb_info(u32 op, u32 info, u64 addr)
{
	__asm__ __volatile__(
		"parse_r info,%0\n\t"
		".word ((0x6498000) | (0 << 10) | (info << 5) | %1)\n\t"
		:
		: "r"(info), "i"(op)
		:
		);
}

static inline void invtlb_all(u32 op, u32 info, u64 addr)
{
	__asm__ __volatile__(
		".word ((0x6498000) | (0 << 10) | (0 << 5) | %0)\n\t"
		:
		: "i"(op)
		:
		);
}

#define __tlb_remove_tlb_entry(tlb, ptep, address) do { } while (0)

static void tlb_flush(struct mmu_gather *tlb);

#define tlb_flush tlb_flush
#include <asm-generic/tlb.h>

static inline void tlb_flush(struct mmu_gather *tlb)
{
	struct vm_area_struct vma;

	vma.vm_mm = tlb->mm;
	vma.vm_flags = 0;
	if (tlb->fullmm) {
		flush_tlb_mm(tlb->mm);
		return;
	}

	flush_tlb_range(&vma, tlb->start, tlb->end);
}

extern void handle_tlb_load(void);
extern void handle_tlb_store(void);
extern void handle_tlb_modify(void);
extern void handle_tlb_refill(void);
extern void handle_tlb_protect(void);

extern void dump_tlb_all(void);
extern void dump_tlb_regs(void);

#endif /* __ASM_TLB_H */
