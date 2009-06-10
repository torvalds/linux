/*
 * Cache control for MicroBlaze cache memories
 *
 * Copyright (C) 2007-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2007-2009 PetaLogix
 * Copyright (C) 2007 John Williams <john.williams@petalogix.com>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file COPYING in the main directory of this
 * archive for more details.
 */

#include <asm/cacheflush.h>
#include <linux/cache.h>
#include <asm/cpuinfo.h>

/* Exported functions */

void _enable_icache(void)
{
	if (cpuinfo.use_icache) {
#if CONFIG_XILINX_MICROBLAZE0_USE_MSR_INSTR
		__asm__ __volatile__ ("					\
				msrset	r0, %0;				\
				nop; "					\
				:					\
				: "i" (MSR_ICE)				\
				: "memory");
#else
		__asm__ __volatile__ ("					\
				mfs	r12, rmsr;			\
				nop;					\
				ori	r12, r12, %0;			\
				mts	rmsr, r12;			\
				nop; "					\
				:					\
				: "i" (MSR_ICE)				\
				: "memory", "r12");
#endif
	}
}

void _disable_icache(void)
{
	if (cpuinfo.use_icache) {
#if CONFIG_XILINX_MICROBLAZE0_USE_MSR_INSTR
		__asm__ __volatile__ ("					\
				msrclr r0, %0;				\
				nop; "					\
				:					\
				: "i" (MSR_ICE)				\
				: "memory");
#else
		__asm__ __volatile__ ("					\
				mfs	r12, rmsr;			\
				nop;					\
				andi	r12, r12, ~%0;			\
				mts	rmsr, r12;			\
				nop; "					\
				:					\
				: "i" (MSR_ICE)				\
				: "memory", "r12");
#endif
	}
}

void _invalidate_icache(unsigned int addr)
{
	if (cpuinfo.use_icache) {
		__asm__ __volatile__ ("					\
				wic	%0, r0"				\
				:					\
				: "r" (addr));
	}
}

void _enable_dcache(void)
{
	if (cpuinfo.use_dcache) {
#if CONFIG_XILINX_MICROBLAZE0_USE_MSR_INSTR
		__asm__ __volatile__ ("					\
				msrset	r0, %0;				\
				nop; "					\
				:					\
				: "i" (MSR_DCE)				\
				: "memory");
#else
		__asm__ __volatile__ ("					\
				mfs	r12, rmsr;			\
				nop;					\
				ori	r12, r12, %0;			\
				mts	rmsr, r12;			\
				nop; "					\
				:					\
				: "i" (MSR_DCE)			\
				: "memory", "r12");
#endif
	}
}

void _disable_dcache(void)
{
#if CONFIG_XILINX_MICROBLAZE0_USE_MSR_INSTR
		__asm__ __volatile__ ("					\
				msrclr	r0, %0;				\
				nop; "					\
				:					\
				: "i" (MSR_DCE)			\
				: "memory");
#else
		__asm__ __volatile__ ("					\
				mfs	r12, rmsr;			\
				nop;					\
				andi	r12, r12, ~%0;			\
				mts	rmsr, r12;			\
				nop; "					\
				:					\
				: "i" (MSR_DCE)			\
				: "memory", "r12");
#endif
}

void _invalidate_dcache(unsigned int addr)
{
		__asm__ __volatile__ ("					\
				wdc	%0, r0"				\
				:					\
				: "r" (addr));
}

void __invalidate_icache_all(void)
{
	unsigned int i;
	unsigned flags;

	if (cpuinfo.use_icache) {
		local_irq_save(flags);
		__disable_icache();

		/* Just loop through cache size and invalidate, no need to add
			CACHE_BASE address */
		for (i = 0; i < cpuinfo.icache_size;
			i += cpuinfo.icache_line)
				__invalidate_icache(i);

		__enable_icache();
		local_irq_restore(flags);
	}
}

void __invalidate_icache_range(unsigned long start, unsigned long end)
{
	unsigned int i;
	unsigned flags;
	unsigned int align;

	if (cpuinfo.use_icache) {
		/*
		 * No need to cover entire cache range,
		 * just cover cache footprint
		 */
		end = min(start + cpuinfo.icache_size, end);
		align = ~(cpuinfo.icache_line - 1);
		start &= align; /* Make sure we are aligned */
		/* Push end up to the next cache line */
		end = ((end & align) + cpuinfo.icache_line);

		local_irq_save(flags);
		__disable_icache();

		for (i = start; i < end; i += cpuinfo.icache_line)
			__invalidate_icache(i);

		__enable_icache();
		local_irq_restore(flags);
	}
}

void __invalidate_icache_page(struct vm_area_struct *vma, struct page *page)
{
	__invalidate_icache_all();
}

void __invalidate_icache_user_range(struct vm_area_struct *vma,
				struct page *page, unsigned long adr,
				int len)
{
	__invalidate_icache_all();
}

void __invalidate_cache_sigtramp(unsigned long addr)
{
	__invalidate_icache_range(addr, addr + 8);
}

void __invalidate_dcache_all(void)
{
	unsigned int i;
	unsigned flags;

	if (cpuinfo.use_dcache) {
		local_irq_save(flags);
		__disable_dcache();

		/*
		 * Just loop through cache size and invalidate,
		 * no need to add CACHE_BASE address
		 */
		for (i = 0; i < cpuinfo.dcache_size;
			i += cpuinfo.dcache_line)
				__invalidate_dcache(i);

		__enable_dcache();
		local_irq_restore(flags);
	}
}

void __invalidate_dcache_range(unsigned long start, unsigned long end)
{
	unsigned int i;
	unsigned flags;
	unsigned int align;

	if (cpuinfo.use_dcache) {
		/*
		 * No need to cover entire cache range,
		 * just cover cache footprint
		 */
		end = min(start + cpuinfo.dcache_size, end);
		align = ~(cpuinfo.dcache_line - 1);
		start &= align; /* Make sure we are aligned */
		/* Push end up to the next cache line */
		end = ((end & align) + cpuinfo.dcache_line);
		local_irq_save(flags);
		__disable_dcache();

		for (i = start; i < end; i += cpuinfo.dcache_line)
			__invalidate_dcache(i);

		__enable_dcache();
		local_irq_restore(flags);
	}
}

void __invalidate_dcache_page(struct vm_area_struct *vma, struct page *page)
{
	__invalidate_dcache_all();
}

void __invalidate_dcache_user_range(struct vm_area_struct *vma,
				struct page *page, unsigned long adr,
				int len)
{
	__invalidate_dcache_all();
}
