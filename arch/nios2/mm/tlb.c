/*
 * Nios2 TLB handling
 *
 * Copyright (C) 2009, Wind River Systems Inc
 *   Implemented by fredrik.markstrom@gmail.com and ivarholmqvist@gmail.com
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/init.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/pagemap.h>

#include <asm/tlb.h>
#include <asm/mmu_context.h>
#include <asm/pgtable.h>
#include <asm/cpuinfo.h>

#define TLB_INDEX_MASK		\
	((((1UL << (cpuinfo.tlb_ptr_sz - cpuinfo.tlb_num_ways_log2))) - 1) \
		<< PAGE_SHIFT)

static void get_misc_and_pid(unsigned long *misc, unsigned long *pid)
{
	*misc  = RDCTL(CTL_TLBMISC);
	*misc &= (TLBMISC_PID | TLBMISC_WAY);
	*pid  = *misc & TLBMISC_PID;
}

/*
 * This provides a PTEADDR value for addr that will cause a TLB miss
 * (fast TLB miss). TLB invalidation replaces entries with this value.
 */
static unsigned long pteaddr_invalid(unsigned long addr)
{
	return ((addr | 0xC0000000UL) >> PAGE_SHIFT) << 2;
}

/*
 * This one is only used for pages with the global bit set so we don't care
 * much about the ASID.
 */
static void replace_tlb_one_pid(unsigned long addr, unsigned long mmu_pid, unsigned long tlbacc)
{
	unsigned int way;
	unsigned long org_misc, pid_misc;

	/* remember pid/way until we return. */
	get_misc_and_pid(&org_misc, &pid_misc);

	WRCTL(CTL_PTEADDR, (addr >> PAGE_SHIFT) << 2);

	for (way = 0; way < cpuinfo.tlb_num_ways; way++) {
		unsigned long pteaddr;
		unsigned long tlbmisc;
		unsigned long pid;

		tlbmisc = TLBMISC_RD | (way << TLBMISC_WAY_SHIFT);
		WRCTL(CTL_TLBMISC, tlbmisc);

		pteaddr = RDCTL(CTL_PTEADDR);
		if (((pteaddr >> 2) & 0xfffff) != (addr >> PAGE_SHIFT))
			continue;

		tlbmisc = RDCTL(CTL_TLBMISC);
		pid = (tlbmisc >> TLBMISC_PID_SHIFT) & TLBMISC_PID_MASK;
		if (pid != mmu_pid)
			continue;

		tlbmisc = (mmu_pid << TLBMISC_PID_SHIFT) | TLBMISC_WE |
			  (way << TLBMISC_WAY_SHIFT);
		WRCTL(CTL_TLBMISC, tlbmisc);
		if (tlbacc == 0)
			WRCTL(CTL_PTEADDR, pteaddr_invalid(addr));
		WRCTL(CTL_TLBACC, tlbacc);
		/*
		 * There should be only a single entry that maps a
		 * particular {address,pid} so break after a match.
		 */
		break;
	}

	WRCTL(CTL_TLBMISC, org_misc);
}

static void flush_tlb_one_pid(unsigned long addr, unsigned long mmu_pid)
{
	pr_debug("Flush tlb-entry for vaddr=%#lx\n", addr);

	replace_tlb_one_pid(addr, mmu_pid, 0);
}

static void reload_tlb_one_pid(unsigned long addr, unsigned long mmu_pid, pte_t pte)
{
	pr_debug("Reload tlb-entry for vaddr=%#lx\n", addr);

	replace_tlb_one_pid(addr, mmu_pid, pte_val(pte));
}

void flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
			unsigned long end)
{
	unsigned long mmu_pid = get_pid_from_context(&vma->vm_mm->context);

	while (start < end) {
		flush_tlb_one_pid(start, mmu_pid);
		start += PAGE_SIZE;
	}
}

void reload_tlb_page(struct vm_area_struct *vma, unsigned long addr, pte_t pte)
{
	unsigned long mmu_pid = get_pid_from_context(&vma->vm_mm->context);

	reload_tlb_one_pid(addr, mmu_pid, pte);
}

/*
 * This one is only used for pages with the global bit set so we don't care
 * much about the ASID.
 */
static void flush_tlb_one(unsigned long addr)
{
	unsigned int way;
	unsigned long org_misc, pid_misc;

	pr_debug("Flush tlb-entry for vaddr=%#lx\n", addr);

	/* remember pid/way until we return. */
	get_misc_and_pid(&org_misc, &pid_misc);

	WRCTL(CTL_PTEADDR, (addr >> PAGE_SHIFT) << 2);

	for (way = 0; way < cpuinfo.tlb_num_ways; way++) {
		unsigned long pteaddr;
		unsigned long tlbmisc;

		tlbmisc = TLBMISC_RD | (way << TLBMISC_WAY_SHIFT);
		WRCTL(CTL_TLBMISC, tlbmisc);

		pteaddr = RDCTL(CTL_PTEADDR);
		if (((pteaddr >> 2) & 0xfffff) != (addr >> PAGE_SHIFT))
			continue;

		pr_debug("Flush entry by writing way=%dl pid=%ld\n",
			 way, (pid_misc >> TLBMISC_PID_SHIFT));

		tlbmisc = TLBMISC_WE | (way << TLBMISC_WAY_SHIFT);
		WRCTL(CTL_TLBMISC, tlbmisc);
		WRCTL(CTL_PTEADDR, pteaddr_invalid(addr));
		WRCTL(CTL_TLBACC, 0);
	}

	WRCTL(CTL_TLBMISC, org_misc);
}

void flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	while (start < end) {
		flush_tlb_one(start);
		start += PAGE_SIZE;
	}
}

void dump_tlb_line(unsigned long line)
{
	unsigned int way;
	unsigned long org_misc;

	pr_debug("dump tlb-entries for line=%#lx (addr %08lx)\n", line,
		line << (PAGE_SHIFT + cpuinfo.tlb_num_ways_log2));

	/* remember pid/way until we return */
	org_misc = (RDCTL(CTL_TLBMISC) & (TLBMISC_PID | TLBMISC_WAY));

	WRCTL(CTL_PTEADDR, line << 2);

	for (way = 0; way < cpuinfo.tlb_num_ways; way++) {
		unsigned long pteaddr;
		unsigned long tlbmisc;
		unsigned long tlbacc;

		WRCTL(CTL_TLBMISC, TLBMISC_RD | (way << TLBMISC_WAY_SHIFT));
		pteaddr = RDCTL(CTL_PTEADDR);
		tlbmisc = RDCTL(CTL_TLBMISC);
		tlbacc = RDCTL(CTL_TLBACC);

		if ((tlbacc << PAGE_SHIFT) != 0) {
			pr_debug("-- way:%02x vpn:0x%08lx phys:0x%08lx pid:0x%02lx flags:%c%c%c%c%c\n",
				way,
				(pteaddr << (PAGE_SHIFT-2)),
				(tlbacc << PAGE_SHIFT),
				((tlbmisc >> TLBMISC_PID_SHIFT) &
				TLBMISC_PID_MASK),
				(tlbacc & _PAGE_READ ? 'r' : '-'),
				(tlbacc & _PAGE_WRITE ? 'w' : '-'),
				(tlbacc & _PAGE_EXEC ? 'x' : '-'),
				(tlbacc & _PAGE_GLOBAL ? 'g' : '-'),
				(tlbacc & _PAGE_CACHED ? 'c' : '-'));
		}
	}

	WRCTL(CTL_TLBMISC, org_misc);
}

void dump_tlb(void)
{
	unsigned int i;

	for (i = 0; i < cpuinfo.tlb_num_lines; i++)
		dump_tlb_line(i);
}

void flush_tlb_pid(unsigned long mmu_pid)
{
	unsigned long addr = 0;
	unsigned int line;
	unsigned int way;
	unsigned long org_misc, pid_misc;

	/* remember pid/way until we return */
	get_misc_and_pid(&org_misc, &pid_misc);

	for (line = 0; line < cpuinfo.tlb_num_lines; line++) {
		WRCTL(CTL_PTEADDR, pteaddr_invalid(addr));

		for (way = 0; way < cpuinfo.tlb_num_ways; way++) {
			unsigned long tlbmisc;
			unsigned long pid;

			tlbmisc = TLBMISC_RD | (way << TLBMISC_WAY_SHIFT);
			WRCTL(CTL_TLBMISC, tlbmisc);
			tlbmisc = RDCTL(CTL_TLBMISC);
			pid = (tlbmisc >> TLBMISC_PID_SHIFT) & TLBMISC_PID_MASK;
			if (pid != mmu_pid)
				continue;

			tlbmisc = TLBMISC_WE | (way << TLBMISC_WAY_SHIFT);
			WRCTL(CTL_TLBMISC, tlbmisc);
			WRCTL(CTL_TLBACC, 0);
		}

		addr += PAGE_SIZE;
	}

	WRCTL(CTL_TLBMISC, org_misc);
}

/*
 * All entries common to a mm share an asid.  To effectively flush these
 * entries, we just bump the asid.
 */
void flush_tlb_mm(struct mm_struct *mm)
{
	if (current->mm == mm) {
		unsigned long mmu_pid = get_pid_from_context(&mm->context);
		flush_tlb_pid(mmu_pid);
	} else {
		memset(&mm->context, 0, sizeof(mm_context_t));
	}
}

void flush_tlb_all(void)
{
	unsigned long addr = 0;
	unsigned int line;
	unsigned int way;
	unsigned long org_misc, pid_misc;

	/* remember pid/way until we return */
	get_misc_and_pid(&org_misc, &pid_misc);

	/* Start at way 0, way is auto-incremented after each TLBACC write */
	WRCTL(CTL_TLBMISC, TLBMISC_WE);

	/* Map each TLB entry to physcal address 0 with no-access and a
	   bad ptbase */
	for (line = 0; line < cpuinfo.tlb_num_lines; line++) {
		WRCTL(CTL_PTEADDR, pteaddr_invalid(addr));
		for (way = 0; way < cpuinfo.tlb_num_ways; way++)
			WRCTL(CTL_TLBACC, 0);

		addr += PAGE_SIZE;
	}

	/* restore pid/way */
	WRCTL(CTL_TLBMISC, org_misc);
}

void set_mmu_pid(unsigned long pid)
{
	unsigned long tlbmisc;

	tlbmisc = RDCTL(CTL_TLBMISC);
	tlbmisc = (tlbmisc & TLBMISC_WAY);
	tlbmisc |= (pid & TLBMISC_PID_MASK) << TLBMISC_PID_SHIFT;
	WRCTL(CTL_TLBMISC, tlbmisc);
}
