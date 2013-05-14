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

/* Used as illegal PHYS_ADDR for TLB mappings
 */
/* FIXME: ((1UL << DATA_ADDR_WIDTH) - 1)
 */
#define MAX_PHYS_ADDR 0

/* bit definitions for TLBMISC register */
#define PID_SHIFT	4
#define PID_MASK	((1UL << cpuinfo.tlb_pid_num_bits) - 1)

#define WAY_SHIFT 20
#define WAY_MASK  0xf

/*
 * All entries common to a mm share an asid.  To effectively flush these
 * entries, we just bump the asid.
 */
void flush_tlb_mm(struct mm_struct *mm)
{
	if (current->mm == mm)
		flush_tlb_all();
	else
		memset(&mm->context, 0, sizeof(mm_context_t));
}

/*
 * This one is only used for pages with the global bit set so we don't care
 * much about the ASID.
 *
 * FIXME: This is proken, to prove it mmap a read/write-page, mprotect it to
 *        read only then write to it, the tlb-entry will not be flushed.
 *
 */
void flush_tlb_one_pid(unsigned long addr, unsigned long mmu_pid)
{
	unsigned int way;
	unsigned long org_misc;

	pr_debug("Flush tlb-entry for vaddr=%#lx\n", addr);

	/* remember pid/way until we return.
	 * CHECKME: is there a race here when writing org_misc back? */
	org_misc = (RDCTL(CTL_TLBMISC) & ((PID_MASK << PID_SHIFT) |
		(WAY_MASK << WAY_SHIFT)));

	WRCTL(CTL_PTEADDR, (addr >> PAGE_SHIFT) << 2);

	for (way = 0; way < cpuinfo.tlb_num_ways; way++) {
		unsigned long pteaddr;
		unsigned long tlbmisc;
		unsigned long pid;

		WRCTL(CTL_TLBMISC, (1UL << 19) | (way << 20));
		pteaddr = RDCTL(CTL_PTEADDR);
		tlbmisc = RDCTL(CTL_TLBMISC);
		pid = (tlbmisc >> PID_SHIFT) & PID_MASK;
		if (((((pteaddr >> 2) & 0xfffff)) == (addr >> PAGE_SHIFT)) &&
				pid == mmu_pid) {
			unsigned long vaddr = CONFIG_IO_REGION_BASE +
				((PAGE_SIZE * cpuinfo.tlb_num_lines) * way) +
				(addr & TLB_INDEX_MASK);
			pr_debug("Flush entry by writing %#lx way=%dl pid=%ld\n",
				vaddr, way, pid);

			WRCTL(CTL_PTEADDR, (vaddr >> 12) << 2);
			WRCTL(CTL_TLBMISC, (1UL << 18) | (way << 20) |
				(pid << PID_SHIFT));
			WRCTL(CTL_TLBACC, (MAX_PHYS_ADDR >> PAGE_SHIFT));
		}
	}

	WRCTL(CTL_TLBMISC, org_misc);
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

void flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	while (start < end) {
		flush_tlb_one(start);
		start += PAGE_SIZE;
	}
}

/*
 * This one is only used for pages with the global bit set so we don't care
 * much about the ASID.
 */
void flush_tlb_one(unsigned long addr)
{
	unsigned int way;
	unsigned long pid, org_misc;

	pr_debug("Flush tlb-entry for vaddr=%#lx\n", addr);

	/* remember pid/way until we return.
	* CHECKME: is there a race here when writing org_misc back? */
	org_misc = (RDCTL(CTL_TLBMISC) & ((PID_MASK << PID_SHIFT) |
		(WAY_MASK << WAY_SHIFT)));

	WRCTL(CTL_PTEADDR, (addr >> PAGE_SHIFT) << 2);

	for (way = 0; way < cpuinfo.tlb_num_ways; way++) {
		unsigned long pteaddr;
		unsigned long tlbmisc;

		WRCTL(CTL_TLBMISC, (1UL << 19) | (way << 20));
		pteaddr = RDCTL(CTL_PTEADDR);
		tlbmisc = RDCTL(CTL_TLBMISC);

		if ((((pteaddr >> 2) & 0xfffff)) == (addr >> PAGE_SHIFT)) {
			unsigned long vaddr = CONFIG_IO_REGION_BASE +
				((PAGE_SIZE * cpuinfo.tlb_num_lines) * way) +
				(addr & TLB_INDEX_MASK);

			pid = (tlbmisc >> PID_SHIFT) & PID_MASK;
			pr_debug("Flush entry by writing %#lx way=%dl pid=%ld\n",
				vaddr, way, pid);

			WRCTL(CTL_PTEADDR, (vaddr >> 12) << 2);
			WRCTL(CTL_TLBMISC, (1UL << 18) | (way << 20) |
				(pid << PID_SHIFT));
			WRCTL(CTL_TLBACC, (MAX_PHYS_ADDR >> PAGE_SHIFT));
		}
	}

	WRCTL(CTL_TLBMISC, org_misc);
}

void dump_tlb_line(unsigned long line)
{
	unsigned int way;
	unsigned long org_misc;

	pr_debug("dump tlb-entries for line=%#lx (addr %08lx)\n", line,
		line << (PAGE_SHIFT + cpuinfo.tlb_num_ways_log2));

	/* remember pid/way until we return */
	org_misc = (RDCTL(CTL_TLBMISC) & ((PID_MASK << PID_SHIFT) |
			(WAY_MASK << WAY_SHIFT)));

	WRCTL(CTL_PTEADDR, line << 2);

	for (way = 0; way < cpuinfo.tlb_num_ways; way++) {
		unsigned long pteaddr;
		unsigned long tlbmisc;
		unsigned long tlbacc;

		WRCTL(CTL_TLBMISC, (1UL << 19) | (way << 20));
		pteaddr = RDCTL(CTL_PTEADDR);
		tlbmisc = RDCTL(CTL_TLBMISC);
		tlbacc = RDCTL(CTL_TLBACC);

		if ((tlbacc << PAGE_SHIFT) != (MAX_PHYS_ADDR & PAGE_MASK)) {
			pr_debug("-- way:%02x vpn:0x%08lx phys:0x%08lx pid:0x%02lx flags:%c%c%c%c%c\n",
				way,
				(pteaddr << (PAGE_SHIFT-2)),
				(tlbacc << PAGE_SHIFT),
				(tlbmisc >> PID_SHIFT) & PID_MASK,
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

void flush_tlb_pid(unsigned long pid)
{
	unsigned int line;
	unsigned int way;
	unsigned long org_misc;

	/* remember pid/way until we return */
	org_misc = (RDCTL(CTL_TLBMISC) & ((PID_MASK << PID_SHIFT) |
		(WAY_MASK << WAY_SHIFT)));

	for (line = 0; line < cpuinfo.tlb_num_lines; line++) {
		/* FIXME: << TLB_WAY_BITS should probably not be here */
		WRCTL(CTL_PTEADDR, line << 2);

		for (way = 0; way < cpuinfo.tlb_num_ways; way++) {
			unsigned long pteaddr;
			unsigned long tlbmisc;
			unsigned long tlbacc;

			WRCTL(CTL_TLBMISC, (1UL << 19) | (way << 20));
			pteaddr = RDCTL(CTL_PTEADDR);
			tlbmisc = RDCTL(CTL_TLBMISC);
			tlbacc = RDCTL(CTL_TLBACC);

			if (((tlbmisc>>PID_SHIFT)&PID_MASK) == pid) {
				WRCTL(CTL_TLBMISC, (1UL << 18) | (way << 20) |
					(pid << PID_SHIFT));
				WRCTL(CTL_TLBACC,
					(MAX_PHYS_ADDR >> PAGE_SHIFT));
			}
		}

		WRCTL(CTL_TLBMISC, org_misc);
	}
}

void flush_tlb_all(void)
{
	int i;
	unsigned long vaddr = CONFIG_IO_REGION_BASE;
	unsigned int way;
	unsigned long org_misc;

	/* remember pid/way */
	org_misc = (RDCTL(CTL_TLBMISC) & ((PID_MASK << PID_SHIFT) |
		(WAY_MASK << WAY_SHIFT)));

	/* Map each TLB entry to physcal address 0 with no-access and a
	   bad ptbase */
	for (way = 0; way < cpuinfo.tlb_num_ways; way++) {
		for (i = 0; i < cpuinfo.tlb_num_lines; i++) {
			WRCTL(CTL_PTEADDR, ((vaddr) >> PAGE_SHIFT) << 2);
			WRCTL(CTL_TLBMISC, (1<<18) | (way << 20));
			WRCTL(CTL_TLBACC, (MAX_PHYS_ADDR >> PAGE_SHIFT));
			vaddr += 1UL << 12;
		}
	}

	/* restore pid/way */
	WRCTL(CTL_TLBMISC, org_misc);
}

void set_mmu_pid(unsigned long pid)
{
	WRCTL(CTL_TLBMISC, (RDCTL(CTL_TLBMISC) & (WAY_MASK << WAY_SHIFT)) |
		((pid & PID_MASK) << PID_SHIFT));
}
