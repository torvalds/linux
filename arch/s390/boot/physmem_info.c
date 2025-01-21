// SPDX-License-Identifier: GPL-2.0
#include <linux/processor.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <asm/physmem_info.h>
#include <asm/stacktrace.h>
#include <asm/boot_data.h>
#include <asm/sparsemem.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <asm/sclp.h>
#include <asm/asm.h>
#include <asm/uv.h>
#include "decompressor.h"
#include "boot.h"

struct physmem_info __bootdata(physmem_info);
static unsigned int physmem_alloc_ranges;
static unsigned long physmem_alloc_pos;

/* up to 256 storage elements, 1020 subincrements each */
#define ENTRIES_EXTENDED_MAX						       \
	(256 * (1020 / 2) * sizeof(struct physmem_range))

static struct physmem_range *__get_physmem_range_ptr(u32 n)
{
	if (n < MEM_INLINED_ENTRIES)
		return &physmem_info.online[n];
	if (unlikely(!physmem_info.online_extended)) {
		physmem_info.online_extended = (struct physmem_range *)physmem_alloc_range(
			RR_MEM_DETECT_EXTENDED, ENTRIES_EXTENDED_MAX, sizeof(long), 0,
			physmem_alloc_pos, true);
	}
	return &physmem_info.online_extended[n - MEM_INLINED_ENTRIES];
}

/*
 * sequential calls to add_physmem_online_range with adjacent memory ranges
 * are merged together into single memory range.
 */
void add_physmem_online_range(u64 start, u64 end)
{
	struct physmem_range *range;

	if (physmem_info.range_count) {
		range = __get_physmem_range_ptr(physmem_info.range_count - 1);
		if (range->end == start) {
			range->end = end;
			return;
		}
	}

	range = __get_physmem_range_ptr(physmem_info.range_count);
	range->start = start;
	range->end = end;
	physmem_info.range_count++;
}

static int __diag260(unsigned long rx1, unsigned long rx2)
{
	unsigned long reg1, reg2, ry;
	union register_pair rx;
	int cc, exception;
	psw_t old;

	rx.even = rx1;
	rx.odd	= rx2;
	ry = 0x10; /* storage configuration */
	exception = 1;
	asm volatile(
		"	mvc	0(16,%[psw_old]),0(%[psw_pgm])\n"
		"	epsw	%[reg1],%[reg2]\n"
		"	st	%[reg1],0(%[psw_pgm])\n"
		"	st	%[reg2],4(%[psw_pgm])\n"
		"	larl	%[reg1],1f\n"
		"	stg	%[reg1],8(%[psw_pgm])\n"
		"	diag	%[rx],%[ry],0x260\n"
		"	lhi	%[exc],0\n"
		"1:	mvc	0(16,%[psw_pgm]),0(%[psw_old])\n"
		CC_IPM(cc)
		: CC_OUT(cc, cc),
		  [exc] "+d" (exception),
		  [reg1] "=&d" (reg1),
		  [reg2] "=&a" (reg2),
		  [ry] "+&d" (ry),
		  "+Q" (get_lowcore()->program_new_psw),
		  "=Q" (old)
		: [rx] "d" (rx.pair),
		  [psw_old] "a" (&old),
		  [psw_pgm] "a" (&get_lowcore()->program_new_psw)
		: CC_CLOBBER_LIST("memory"));
	cc = exception ? -1 : CC_TRANSFORM(cc);
	return cc == 0 ? ry : -1;
}

static int diag260(void)
{
	int rc, i;

	struct {
		unsigned long start;
		unsigned long end;
	} storage_extents[8] __aligned(16); /* VM supports up to 8 extends */

	memset(storage_extents, 0, sizeof(storage_extents));
	rc = __diag260((unsigned long)storage_extents, sizeof(storage_extents));
	if (rc == -1)
		return -1;

	for (i = 0; i < min_t(int, rc, ARRAY_SIZE(storage_extents)); i++)
		add_physmem_online_range(storage_extents[i].start, storage_extents[i].end + 1);
	return 0;
}

#define DIAG500_SC_STOR_LIMIT 4

static int diag500_storage_limit(unsigned long *max_physmem_end)
{
	unsigned long storage_limit;
	unsigned long reg1, reg2;
	psw_t old;

	asm volatile(
		"	mvc	0(16,%[psw_old]),0(%[psw_pgm])\n"
		"	epsw	%[reg1],%[reg2]\n"
		"	st	%[reg1],0(%[psw_pgm])\n"
		"	st	%[reg2],4(%[psw_pgm])\n"
		"	larl	%[reg1],1f\n"
		"	stg	%[reg1],8(%[psw_pgm])\n"
		"	lghi	1,%[subcode]\n"
		"	lghi	2,0\n"
		"	diag	2,4,0x500\n"
		"1:	mvc	0(16,%[psw_pgm]),0(%[psw_old])\n"
		"	lgr	%[slimit],2\n"
		: [reg1] "=&d" (reg1),
		  [reg2] "=&a" (reg2),
		  [slimit] "=d" (storage_limit),
		  "=Q" (get_lowcore()->program_new_psw),
		  "=Q" (old)
		: [psw_old] "a" (&old),
		  [psw_pgm] "a" (&get_lowcore()->program_new_psw),
		  [subcode] "i" (DIAG500_SC_STOR_LIMIT)
		: "memory", "1", "2");
	if (!storage_limit)
		return -EINVAL;
	/* Convert inclusive end to exclusive end */
	*max_physmem_end = storage_limit + 1;
	return 0;
}

static int tprot(unsigned long addr)
{
	unsigned long reg1, reg2;
	int cc, exception;
	psw_t old;

	exception = 1;
	asm volatile(
		"	mvc	0(16,%[psw_old]),0(%[psw_pgm])\n"
		"	epsw	%[reg1],%[reg2]\n"
		"	st	%[reg1],0(%[psw_pgm])\n"
		"	st	%[reg2],4(%[psw_pgm])\n"
		"	larl	%[reg1],1f\n"
		"	stg	%[reg1],8(%[psw_pgm])\n"
		"	tprot	0(%[addr]),0\n"
		"	lhi	%[exc],0\n"
		"1:	mvc	0(16,%[psw_pgm]),0(%[psw_old])\n"
		CC_IPM(cc)
		: CC_OUT(cc, cc),
		  [exc] "+d" (exception),
		  [reg1] "=&d" (reg1),
		  [reg2] "=&a" (reg2),
		  "=Q" (get_lowcore()->program_new_psw.addr),
		  "=Q" (old)
		: [psw_old] "a" (&old),
		  [psw_pgm] "a" (&get_lowcore()->program_new_psw),
		  [addr] "a" (addr)
		: CC_CLOBBER_LIST("memory"));
	cc = exception ? -EFAULT : CC_TRANSFORM(cc);
	return cc;
}

static unsigned long search_mem_end(void)
{
	unsigned long range = 1 << (MAX_PHYSMEM_BITS - 20); /* in 1MB blocks */
	unsigned long offset = 0;
	unsigned long pivot;

	while (range > 1) {
		range >>= 1;
		pivot = offset + range;
		if (!tprot(pivot << 20))
			offset = pivot;
	}
	return (offset + 1) << 20;
}

unsigned long detect_max_physmem_end(void)
{
	unsigned long max_physmem_end = 0;

	if (!diag500_storage_limit(&max_physmem_end)) {
		physmem_info.info_source = MEM_DETECT_DIAG500_STOR_LIMIT;
	} else if (!sclp_early_get_memsize(&max_physmem_end)) {
		physmem_info.info_source = MEM_DETECT_SCLP_READ_INFO;
	} else {
		max_physmem_end = search_mem_end();
		physmem_info.info_source = MEM_DETECT_BIN_SEARCH;
	}
	return max_physmem_end;
}

void detect_physmem_online_ranges(unsigned long max_physmem_end)
{
	if (!sclp_early_read_storage_info()) {
		physmem_info.info_source = MEM_DETECT_SCLP_STOR_INFO;
	} else if (physmem_info.info_source == MEM_DETECT_DIAG500_STOR_LIMIT) {
		unsigned long online_end;

		if (!sclp_early_get_memsize(&online_end)) {
			physmem_info.info_source = MEM_DETECT_SCLP_READ_INFO;
			add_physmem_online_range(0, online_end);
		}
	} else if (!diag260()) {
		physmem_info.info_source = MEM_DETECT_DIAG260;
	} else if (max_physmem_end) {
		add_physmem_online_range(0, max_physmem_end);
	}
}

void physmem_set_usable_limit(unsigned long limit)
{
	physmem_info.usable = limit;
	physmem_alloc_pos = limit;
}

static void die_oom(unsigned long size, unsigned long align, unsigned long min, unsigned long max)
{
	unsigned long start, end, total_mem = 0, total_reserved_mem = 0;
	struct reserved_range *range;
	enum reserved_range_type t;
	int i;

	boot_printk("Linux version %s\n", kernel_version);
	if (!is_prot_virt_guest() && early_command_line[0])
		boot_printk("Kernel command line: %s\n", early_command_line);
	boot_printk("Out of memory allocating %lx bytes %lx aligned in range %lx:%lx\n",
		    size, align, min, max);
	boot_printk("Reserved memory ranges:\n");
	for_each_physmem_reserved_range(t, range, &start, &end) {
		boot_printk("%016lx %016lx %s\n", start, end, get_rr_type_name(t));
		total_reserved_mem += end - start;
	}
	boot_printk("Usable online memory ranges (info source: %s [%x]):\n",
		    get_physmem_info_source(), physmem_info.info_source);
	for_each_physmem_usable_range(i, &start, &end) {
		boot_printk("%016lx %016lx\n", start, end);
		total_mem += end - start;
	}
	boot_printk("Usable online memory total: %lx Reserved: %lx Free: %lx\n",
		    total_mem, total_reserved_mem,
		    total_mem > total_reserved_mem ? total_mem - total_reserved_mem : 0);
	print_stacktrace(current_frame_address());
	boot_printk("\n\n -- System halted\n");
	disabled_wait();
}

void physmem_reserve(enum reserved_range_type type, unsigned long addr, unsigned long size)
{
	physmem_info.reserved[type].start = addr;
	physmem_info.reserved[type].end = addr + size;
}

void physmem_free(enum reserved_range_type type)
{
	physmem_info.reserved[type].start = 0;
	physmem_info.reserved[type].end = 0;
}

static bool __physmem_alloc_intersects(unsigned long addr, unsigned long size,
				       unsigned long *intersection_start)
{
	unsigned long res_addr, res_size;
	int t;

	for (t = 0; t < RR_MAX; t++) {
		if (!get_physmem_reserved(t, &res_addr, &res_size))
			continue;
		if (intersects(addr, size, res_addr, res_size)) {
			*intersection_start = res_addr;
			return true;
		}
	}
	return ipl_report_certs_intersects(addr, size, intersection_start);
}

static unsigned long __physmem_alloc_range(unsigned long size, unsigned long align,
					   unsigned long min, unsigned long max,
					   unsigned int from_ranges, unsigned int *ranges_left,
					   bool die_on_oom)
{
	unsigned int nranges = from_ranges ?: physmem_info.range_count;
	unsigned long range_start, range_end;
	unsigned long intersection_start;
	unsigned long addr, pos = max;

	align = max(align, 8UL);
	while (nranges) {
		__get_physmem_range(nranges - 1, &range_start, &range_end, false);
		pos = min(range_end, pos);

		if (round_up(min, align) + size > pos)
			break;
		addr = round_down(pos - size, align);
		if (range_start > addr) {
			nranges--;
			continue;
		}
		if (__physmem_alloc_intersects(addr, size, &intersection_start)) {
			pos = intersection_start;
			continue;
		}

		if (ranges_left)
			*ranges_left = nranges;
		return addr;
	}
	if (die_on_oom)
		die_oom(size, align, min, max);
	return 0;
}

unsigned long physmem_alloc_range(enum reserved_range_type type, unsigned long size,
				  unsigned long align, unsigned long min, unsigned long max,
				  bool die_on_oom)
{
	unsigned long addr;

	max = min(max, physmem_alloc_pos);
	addr = __physmem_alloc_range(size, align, min, max, 0, NULL, die_on_oom);
	if (addr)
		physmem_reserve(type, addr, size);
	return addr;
}

unsigned long physmem_alloc_top_down(enum reserved_range_type type, unsigned long size,
				     unsigned long align)
{
	struct reserved_range *range = &physmem_info.reserved[type];
	struct reserved_range *new_range;
	unsigned int ranges_left;
	unsigned long addr;

	addr = __physmem_alloc_range(size, align, 0, physmem_alloc_pos, physmem_alloc_ranges,
				     &ranges_left, true);
	/* if not a consecutive allocation of the same type or first allocation */
	if (range->start != addr + size) {
		if (range->end) {
			physmem_alloc_pos = __physmem_alloc_range(
				sizeof(struct reserved_range), 0, 0, physmem_alloc_pos,
				physmem_alloc_ranges, &ranges_left, true);
			new_range = (struct reserved_range *)physmem_alloc_pos;
			*new_range = *range;
			range->chain = new_range;
			addr = __physmem_alloc_range(size, align, 0, physmem_alloc_pos,
						     ranges_left, &ranges_left, true);
		}
		range->end = addr + size;
	}
	range->start = addr;
	physmem_alloc_pos = addr;
	physmem_alloc_ranges = ranges_left;
	return addr;
}

unsigned long get_physmem_alloc_pos(void)
{
	return physmem_alloc_pos;
}
