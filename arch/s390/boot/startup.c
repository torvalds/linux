// SPDX-License-Identifier: GPL-2.0
#define boot_fmt(fmt) "startup: " fmt
#include <linux/string.h>
#include <linux/elf.h>
#include <asm/page-states.h>
#include <asm/boot_data.h>
#include <asm/extmem.h>
#include <asm/sections.h>
#include <asm/maccess.h>
#include <asm/cpu_mf.h>
#include <asm/setup.h>
#include <asm/kasan.h>
#include <asm/kexec.h>
#include <asm/sclp.h>
#include <asm/diag.h>
#include <asm/uv.h>
#include <asm/abs_lowcore.h>
#include <asm/physmem_info.h>
#include "decompressor.h"
#include "boot.h"
#include "uv.h"

struct vm_layout __bootdata_preserved(vm_layout);
unsigned long __bootdata_preserved(__abs_lowcore);
unsigned long __bootdata_preserved(__memcpy_real_area);
pte_t *__bootdata_preserved(memcpy_real_ptep);
unsigned long __bootdata_preserved(VMALLOC_START);
unsigned long __bootdata_preserved(VMALLOC_END);
struct page *__bootdata_preserved(vmemmap);
unsigned long __bootdata_preserved(vmemmap_size);
unsigned long __bootdata_preserved(MODULES_VADDR);
unsigned long __bootdata_preserved(MODULES_END);
unsigned long __bootdata_preserved(max_mappable);
unsigned long __bootdata_preserved(page_noexec_mask);
unsigned long __bootdata_preserved(segment_noexec_mask);
unsigned long __bootdata_preserved(region_noexec_mask);
int __bootdata_preserved(relocate_lowcore);

u64 __bootdata_preserved(stfle_fac_list[16]);
struct oldmem_data __bootdata_preserved(oldmem_data);

struct machine_info machine;

void error(char *x)
{
	boot_emerg("%s\n", x);
	boot_emerg(" -- System halted\n");
	disabled_wait();
}

static void detect_facilities(void)
{
	if (test_facility(8)) {
		machine.has_edat1 = 1;
		local_ctl_set_bit(0, CR0_EDAT_BIT);
	}
	if (test_facility(78))
		machine.has_edat2 = 1;
	page_noexec_mask = -1UL;
	segment_noexec_mask = -1UL;
	region_noexec_mask = -1UL;
	if (!test_facility(130)) {
		page_noexec_mask &= ~_PAGE_NOEXEC;
		segment_noexec_mask &= ~_SEGMENT_ENTRY_NOEXEC;
		region_noexec_mask &= ~_REGION_ENTRY_NOEXEC;
	}
}

static int cmma_test_essa(void)
{
	unsigned long reg1, reg2, tmp = 0;
	int rc = 1;
	psw_t old;

	/* Test ESSA_GET_STATE */
	asm volatile(
		"	mvc	0(16,%[psw_old]),0(%[psw_pgm])\n"
		"	epsw	%[reg1],%[reg2]\n"
		"	st	%[reg1],0(%[psw_pgm])\n"
		"	st	%[reg2],4(%[psw_pgm])\n"
		"	larl	%[reg1],1f\n"
		"	stg	%[reg1],8(%[psw_pgm])\n"
		"	.insn	rrf,0xb9ab0000,%[tmp],%[tmp],%[cmd],0\n"
		"	la	%[rc],0\n"
		"1:	mvc	0(16,%[psw_pgm]),0(%[psw_old])\n"
		: [reg1] "=&d" (reg1),
		  [reg2] "=&a" (reg2),
		  [rc] "+&d" (rc),
		  [tmp] "+&d" (tmp),
		  "+Q" (get_lowcore()->program_new_psw),
		  "=Q" (old)
		: [psw_old] "a" (&old),
		  [psw_pgm] "a" (&get_lowcore()->program_new_psw),
		  [cmd] "i" (ESSA_GET_STATE)
		: "cc", "memory");
	return rc;
}

static void cmma_init(void)
{
	if (!cmma_flag)
		return;
	if (cmma_test_essa()) {
		cmma_flag = 0;
		return;
	}
	if (test_facility(147))
		cmma_flag = 2;
}

static void setup_lpp(void)
{
	get_lowcore()->current_pid = 0;
	get_lowcore()->lpp = LPP_MAGIC;
	if (test_facility(40))
		lpp(&get_lowcore()->lpp);
}

#ifdef CONFIG_KERNEL_UNCOMPRESSED
static unsigned long mem_safe_offset(void)
{
	return (unsigned long)_compressed_start;
}

static void deploy_kernel(void *output)
{
	void *uncompressed_start = (void *)_compressed_start;

	if (output == uncompressed_start)
		return;
	memmove(output, uncompressed_start, vmlinux.image_size);
	memset(uncompressed_start, 0, vmlinux.image_size);
}
#endif

static void rescue_initrd(unsigned long min, unsigned long max)
{
	unsigned long old_addr, addr, size;

	if (!IS_ENABLED(CONFIG_BLK_DEV_INITRD))
		return;
	if (!get_physmem_reserved(RR_INITRD, &addr, &size))
		return;
	if (addr >= min && addr + size <= max)
		return;
	old_addr = addr;
	physmem_free(RR_INITRD);
	addr = physmem_alloc_or_die(RR_INITRD, size, 0);
	memmove((void *)addr, (void *)old_addr, size);
}

static void copy_bootdata(void)
{
	if (__boot_data_end - __boot_data_start != vmlinux.bootdata_size)
		error(".boot.data section size mismatch");
	memcpy((void *)vmlinux.bootdata_off, __boot_data_start, vmlinux.bootdata_size);
	if (__boot_data_preserved_end - __boot_data_preserved_start != vmlinux.bootdata_preserved_size)
		error(".boot.preserved.data section size mismatch");
	memcpy((void *)vmlinux.bootdata_preserved_off, __boot_data_preserved_start, vmlinux.bootdata_preserved_size);
}

static void kaslr_adjust_relocs(unsigned long min_addr, unsigned long max_addr,
				unsigned long offset, unsigned long phys_offset)
{
	int *reloc;
	long loc;

	/* Adjust R_390_64 relocations */
	for (reloc = (int *)__vmlinux_relocs_64_start; reloc < (int *)__vmlinux_relocs_64_end; reloc++) {
		loc = (long)*reloc + phys_offset;
		if (loc < min_addr || loc > max_addr)
			error("64-bit relocation outside of kernel!\n");
		*(u64 *)loc += offset;
	}
}

static void kaslr_adjust_got(unsigned long offset)
{
	u64 *entry;

	/*
	 * Adjust GOT entries, except for ones for undefined weak symbols
	 * that resolved to zero. This also skips the first three reserved
	 * entries on s390x that are zero.
	 */
	for (entry = (u64 *)vmlinux.got_start; entry < (u64 *)vmlinux.got_end; entry++) {
		if (*entry)
			*entry += offset;
	}
}

/*
 * Merge information from several sources into a single ident_map_size value.
 * "ident_map_size" represents the upper limit of physical memory we may ever
 * reach. It might not be all online memory, but also include standby (offline)
 * memory or memory areas reserved for other means (e.g., memory devices such as
 * virtio-mem).
 *
 * "ident_map_size" could be lower then actual standby/reserved or even online
 * memory present, due to limiting factors. We should never go above this limit.
 * It is the size of our identity mapping.
 *
 * Consider the following factors:
 * 1. max_physmem_end - end of physical memory online, standby or reserved.
 *    Always >= end of the last online memory range (get_physmem_online_end()).
 * 2. CONFIG_MAX_PHYSMEM_BITS - the maximum size of physical memory the
 *    kernel is able to support.
 * 3. "mem=" kernel command line option which limits physical memory usage.
 * 4. OLDMEM_BASE which is a kdump memory limit when the kernel is executed as
 *    crash kernel.
 * 5. "hsa" size which is a memory limit when the kernel is executed during
 *    zfcp/nvme dump.
 */
static void setup_ident_map_size(unsigned long max_physmem_end)
{
	unsigned long hsa_size;

	ident_map_size = max_physmem_end;
	if (memory_limit)
		ident_map_size = min(ident_map_size, memory_limit);
	ident_map_size = min(ident_map_size, 1UL << MAX_PHYSMEM_BITS);

#ifdef CONFIG_CRASH_DUMP
	if (oldmem_data.start) {
		__kaslr_enabled = 0;
		ident_map_size = min(ident_map_size, oldmem_data.size);
		boot_debug("kdump memory limit:  0x%016lx\n", oldmem_data.size);
	} else if (ipl_block_valid && is_ipl_block_dump()) {
		__kaslr_enabled = 0;
		if (!sclp_early_get_hsa_size(&hsa_size) && hsa_size) {
			ident_map_size = min(ident_map_size, hsa_size);
			boot_debug("Stand-alone dump limit: 0x%016lx\n", hsa_size);
		}
	}
#endif
	boot_debug("Identity map size:   0x%016lx\n", ident_map_size);
}

#define FIXMAP_SIZE	round_up(MEMCPY_REAL_SIZE + ABS_LOWCORE_MAP_SIZE, sizeof(struct lowcore))

static unsigned long get_vmem_size(unsigned long identity_size,
				   unsigned long vmemmap_size,
				   unsigned long vmalloc_size,
				   unsigned long rte_size)
{
	unsigned long max_mappable, vsize;

	max_mappable = max(identity_size, MAX_DCSS_ADDR);
	vsize = round_up(SZ_2G + max_mappable, rte_size) +
		round_up(vmemmap_size, rte_size) +
		FIXMAP_SIZE + MODULES_LEN + KASLR_LEN;
	if (IS_ENABLED(CONFIG_KMSAN))
		vsize += MODULES_LEN * 2;
	return size_add(vsize, vmalloc_size);
}

static unsigned long setup_kernel_memory_layout(unsigned long kernel_size)
{
	unsigned long vmemmap_start;
	unsigned long kernel_start;
	unsigned long asce_limit;
	unsigned long rte_size;
	unsigned long pages;
	unsigned long vsize;
	unsigned long vmax;

	pages = ident_map_size / PAGE_SIZE;
	/* vmemmap contains a multiple of PAGES_PER_SECTION struct pages */
	vmemmap_size = SECTION_ALIGN_UP(pages) * sizeof(struct page);

	/* choose kernel address space layout: 4 or 3 levels. */
	BUILD_BUG_ON(!IS_ALIGNED(TEXT_OFFSET, THREAD_SIZE));
	BUILD_BUG_ON(!IS_ALIGNED(__NO_KASLR_START_KERNEL, THREAD_SIZE));
	BUILD_BUG_ON(__NO_KASLR_END_KERNEL > _REGION1_SIZE);
	vsize = get_vmem_size(ident_map_size, vmemmap_size, vmalloc_size, _REGION3_SIZE);
	boot_debug("vmem size estimated: 0x%016lx\n", vsize);
	if (IS_ENABLED(CONFIG_KASAN) || __NO_KASLR_END_KERNEL > _REGION2_SIZE ||
	    (vsize > _REGION2_SIZE && kaslr_enabled())) {
		asce_limit = _REGION1_SIZE;
		if (__NO_KASLR_END_KERNEL > _REGION2_SIZE) {
			rte_size = _REGION2_SIZE;
			vsize = get_vmem_size(ident_map_size, vmemmap_size, vmalloc_size, _REGION2_SIZE);
		} else {
			rte_size = _REGION3_SIZE;
		}
	} else {
		asce_limit = _REGION2_SIZE;
		rte_size = _REGION3_SIZE;
	}

	/*
	 * Forcing modules and vmalloc area under the ultravisor
	 * secure storage limit, so that any vmalloc allocation
	 * we do could be used to back secure guest storage.
	 *
	 * Assume the secure storage limit always exceeds _REGION2_SIZE,
	 * otherwise asce_limit and rte_size would have been adjusted.
	 */
	vmax = adjust_to_uv_max(asce_limit);
	boot_debug("%d level paging       0x%016lx vmax\n", vmax == _REGION1_SIZE ? 4 : 3, vmax);
#ifdef CONFIG_KASAN
	BUILD_BUG_ON(__NO_KASLR_END_KERNEL > KASAN_SHADOW_START);
	boot_debug("KASAN shadow area:   0x%016lx-0x%016lx\n", KASAN_SHADOW_START, KASAN_SHADOW_END);
	/* force vmalloc and modules below kasan shadow */
	vmax = min(vmax, KASAN_SHADOW_START);
#endif
	vsize = min(vsize, vmax);
	if (kaslr_enabled()) {
		unsigned long kernel_end, kaslr_len, slots, pos;

		kaslr_len = max(KASLR_LEN, vmax - vsize);
		slots = DIV_ROUND_UP(kaslr_len - kernel_size, THREAD_SIZE);
		if (get_random(slots, &pos))
			pos = 0;
		kernel_end = vmax - pos * THREAD_SIZE;
		kernel_start = round_down(kernel_end - kernel_size, THREAD_SIZE);
		boot_debug("Randomization range: 0x%016lx-0x%016lx\n", vmax - kaslr_len, vmax);
		boot_debug("kernel image:        0x%016lx-0x%016lx (kaslr)\n", kernel_start,
			   kernel_size + kernel_size);
	} else if (vmax < __NO_KASLR_END_KERNEL || vsize > __NO_KASLR_END_KERNEL) {
		kernel_start = round_down(vmax - kernel_size, THREAD_SIZE);
		boot_debug("kernel image:        0x%016lx-0x%016lx (constrained)\n", kernel_start,
			   kernel_start + kernel_size);
	} else {
		kernel_start = __NO_KASLR_START_KERNEL;
		boot_debug("kernel image:        0x%016lx-0x%016lx (nokaslr)\n", kernel_start,
			   kernel_start + kernel_size);
	}
	__kaslr_offset = kernel_start;
	boot_debug("__kaslr_offset:      0x%016lx\n", __kaslr_offset);

	MODULES_END = round_down(kernel_start, _SEGMENT_SIZE);
	MODULES_VADDR = MODULES_END - MODULES_LEN;
	VMALLOC_END = MODULES_VADDR;
	if (IS_ENABLED(CONFIG_KMSAN))
		VMALLOC_END -= MODULES_LEN * 2;
	boot_debug("modules area:        0x%016lx-0x%016lx\n", MODULES_VADDR, MODULES_END);

	/* allow vmalloc area to occupy up to about 1/2 of the rest virtual space left */
	vsize = (VMALLOC_END - FIXMAP_SIZE) / 2;
	vsize = round_down(vsize, _SEGMENT_SIZE);
	vmalloc_size = min(vmalloc_size, vsize);
	if (IS_ENABLED(CONFIG_KMSAN)) {
		/* take 2/3 of vmalloc area for KMSAN shadow and origins */
		vmalloc_size = round_down(vmalloc_size / 3, _SEGMENT_SIZE);
		VMALLOC_END -= vmalloc_size * 2;
	}
	VMALLOC_START = VMALLOC_END - vmalloc_size;
	boot_debug("vmalloc area:        0x%016lx-0x%016lx\n", VMALLOC_START, VMALLOC_END);

	__memcpy_real_area = round_down(VMALLOC_START - MEMCPY_REAL_SIZE, PAGE_SIZE);
	boot_debug("memcpy real area:    0x%016lx-0x%016lx\n", __memcpy_real_area,
		   __memcpy_real_area + MEMCPY_REAL_SIZE);
	__abs_lowcore = round_down(__memcpy_real_area - ABS_LOWCORE_MAP_SIZE,
				   sizeof(struct lowcore));
	boot_debug("abs lowcore:         0x%016lx-0x%016lx\n", __abs_lowcore,
		   __abs_lowcore + ABS_LOWCORE_MAP_SIZE);

	/* split remaining virtual space between 1:1 mapping & vmemmap array */
	pages = __abs_lowcore / (PAGE_SIZE + sizeof(struct page));
	pages = SECTION_ALIGN_UP(pages);
	/* keep vmemmap_start aligned to a top level region table entry */
	vmemmap_start = round_down(__abs_lowcore - pages * sizeof(struct page), rte_size);
	/* make sure identity map doesn't overlay with vmemmap */
	ident_map_size = min(ident_map_size, vmemmap_start);
	vmemmap_size = SECTION_ALIGN_UP(ident_map_size / PAGE_SIZE) * sizeof(struct page);
	/* make sure vmemmap doesn't overlay with absolute lowcore area */
	if (vmemmap_start + vmemmap_size > __abs_lowcore) {
		vmemmap_size = SECTION_ALIGN_DOWN(ident_map_size / PAGE_SIZE) * sizeof(struct page);
		ident_map_size = vmemmap_size / sizeof(struct page) * PAGE_SIZE;
	}
	vmemmap = (struct page *)vmemmap_start;
	/* maximum address for which linear mapping could be created (DCSS, memory) */
	BUILD_BUG_ON(MAX_DCSS_ADDR > (1UL << MAX_PHYSMEM_BITS));
	max_mappable = max(ident_map_size, MAX_DCSS_ADDR);
	max_mappable = min(max_mappable, vmemmap_start);
#ifdef CONFIG_RANDOMIZE_IDENTITY_BASE
	__identity_base = round_down(vmemmap_start - max_mappable, rte_size);
#endif
	boot_debug("identity map:        0x%016lx-0x%016lx\n", __identity_base,
		   __identity_base + ident_map_size);

	return asce_limit;
}

/*
 * This function clears the BSS section of the decompressed Linux kernel and NOT the decompressor's.
 */
static void clear_bss_section(unsigned long kernel_start)
{
	memset((void *)kernel_start + vmlinux.image_size, 0, vmlinux.bss_size);
}

/*
 * Set vmalloc area size to an 8th of (potential) physical memory
 * size, unless size has been set by kernel command line parameter.
 */
static void setup_vmalloc_size(void)
{
	unsigned long size;

	if (vmalloc_size_set)
		return;
	size = round_up(ident_map_size / 8, _SEGMENT_SIZE);
	vmalloc_size = max(size, vmalloc_size);
}

static void kaslr_adjust_vmlinux_info(long offset)
{
	vmlinux.bootdata_off += offset;
	vmlinux.bootdata_preserved_off += offset;
	vmlinux.got_start += offset;
	vmlinux.got_end += offset;
	vmlinux.init_mm_off += offset;
	vmlinux.swapper_pg_dir_off += offset;
	vmlinux.invalid_pg_dir_off += offset;
	vmlinux.alt_instructions += offset;
	vmlinux.alt_instructions_end += offset;
#ifdef CONFIG_KASAN
	vmlinux.kasan_early_shadow_page_off += offset;
	vmlinux.kasan_early_shadow_pte_off += offset;
	vmlinux.kasan_early_shadow_pmd_off += offset;
	vmlinux.kasan_early_shadow_pud_off += offset;
	vmlinux.kasan_early_shadow_p4d_off += offset;
#endif
}

void startup_kernel(void)
{
	unsigned long vmlinux_size = vmlinux.image_size + vmlinux.bss_size;
	unsigned long nokaslr_text_lma, text_lma = 0, amode31_lma = 0;
	unsigned long kernel_size = TEXT_OFFSET + vmlinux_size;
	unsigned long kaslr_large_page_offset;
	unsigned long max_physmem_end;
	unsigned long asce_limit;
	unsigned long safe_addr;
	psw_t psw;

	setup_lpp();
	store_ipl_parmblock();
	uv_query_info();
	setup_boot_command_line();
	parse_boot_command_line();

	/*
	 * Non-randomized kernel physical start address must be _SEGMENT_SIZE
	 * aligned (see blow).
	 */
	nokaslr_text_lma = ALIGN(mem_safe_offset(), _SEGMENT_SIZE);
	safe_addr = PAGE_ALIGN(nokaslr_text_lma + vmlinux_size);

	/*
	 * Reserve decompressor memory together with decompression heap,
	 * buffer and memory which might be occupied by uncompressed kernel
	 * (if KASLR is off or failed).
	 */
	physmem_reserve(RR_DECOMPRESSOR, 0, safe_addr);
	if (IS_ENABLED(CONFIG_BLK_DEV_INITRD) && parmarea.initrd_size)
		physmem_reserve(RR_INITRD, parmarea.initrd_start, parmarea.initrd_size);
	oldmem_data.start = parmarea.oldmem_base;
	oldmem_data.size = parmarea.oldmem_size;

	read_ipl_report();
	sclp_early_read_info();
	detect_facilities();
	cmma_init();
	sanitize_prot_virt_host();
	max_physmem_end = detect_max_physmem_end();
	setup_ident_map_size(max_physmem_end);
	setup_vmalloc_size();
	asce_limit = setup_kernel_memory_layout(kernel_size);
	/* got final ident_map_size, physmem allocations could be performed now */
	physmem_set_usable_limit(ident_map_size);
	detect_physmem_online_ranges(max_physmem_end);
	save_ipl_cert_comp_list();
	rescue_initrd(safe_addr, ident_map_size);

	/*
	 * __kaslr_offset_phys must be _SEGMENT_SIZE aligned, so the lower
	 * 20 bits (the offset within a large page) are zero. Copy the last
	 * 20 bits of __kaslr_offset, which is THREAD_SIZE aligned, to
	 * __kaslr_offset_phys.
	 *
	 * With this the last 20 bits of __kaslr_offset_phys and __kaslr_offset
	 * are identical, which is required to allow for large mappings of the
	 * kernel image.
	 */
	kaslr_large_page_offset = __kaslr_offset & ~_SEGMENT_MASK;
	if (kaslr_enabled()) {
		unsigned long size = vmlinux_size + kaslr_large_page_offset;

		text_lma = randomize_within_range(size, _SEGMENT_SIZE, TEXT_OFFSET, ident_map_size);
	}
	if (!text_lma)
		text_lma = nokaslr_text_lma;
	text_lma |= kaslr_large_page_offset;

	/*
	 * [__kaslr_offset_phys..__kaslr_offset_phys + TEXT_OFFSET] region is
	 * never accessed via the kernel image mapping as per the linker script:
	 *
	 *	. = TEXT_OFFSET;
	 *
	 * Therefore, this region could be used for something else and does
	 * not need to be reserved. See how it is skipped in setup_vmem().
	 */
	__kaslr_offset_phys = text_lma - TEXT_OFFSET;
	kaslr_adjust_vmlinux_info(__kaslr_offset_phys);
	physmem_reserve(RR_VMLINUX, text_lma, vmlinux_size);
	deploy_kernel((void *)text_lma);

	/* vmlinux decompression is done, shrink reserved low memory */
	physmem_reserve(RR_DECOMPRESSOR, 0, (unsigned long)_decompressor_end);

	/*
	 * In case KASLR is enabled the randomized location of .amode31
	 * section might overlap with .vmlinux.relocs section. To avoid that
	 * the below randomize_within_range() could have been called with
	 * __vmlinux_relocs_64_end as the lower range address. However,
	 * .amode31 section is written to by the decompressed kernel - at
	 * that time the contents of .vmlinux.relocs is not needed anymore.
	 * Conversely, .vmlinux.relocs is read only by the decompressor, even
	 * before the kernel started. Therefore, in case the two sections
	 * overlap there is no risk of corrupting any data.
	 */
	if (kaslr_enabled()) {
		unsigned long amode31_min;

		amode31_min = (unsigned long)_decompressor_end;
		amode31_lma = randomize_within_range(vmlinux.amode31_size, PAGE_SIZE, amode31_min, SZ_2G);
	}
	if (!amode31_lma)
		amode31_lma = text_lma - vmlinux.amode31_size;
	physmem_reserve(RR_AMODE31, amode31_lma, vmlinux.amode31_size);

	/*
	 * The order of the following operations is important:
	 *
	 * - kaslr_adjust_relocs() must follow clear_bss_section() to establish
	 *   static memory references to data in .bss to be used by setup_vmem()
	 *   (i.e init_mm.pgd)
	 *
	 * - setup_vmem() must follow kaslr_adjust_relocs() to be able using
	 *   static memory references to data in .bss (i.e init_mm.pgd)
	 *
	 * - copy_bootdata() must follow setup_vmem() to propagate changes
	 *   to bootdata made by setup_vmem()
	 */
	clear_bss_section(text_lma);
	kaslr_adjust_relocs(text_lma, text_lma + vmlinux.image_size,
			    __kaslr_offset, __kaslr_offset_phys);
	kaslr_adjust_got(__kaslr_offset);
	setup_vmem(__kaslr_offset, __kaslr_offset + kernel_size, asce_limit);
	dump_physmem_reserved();
	copy_bootdata();
	__apply_alternatives((struct alt_instr *)_vmlinux_info.alt_instructions,
			     (struct alt_instr *)_vmlinux_info.alt_instructions_end,
			     ALT_CTX_EARLY);

	/*
	 * Save KASLR offset for early dumps, before vmcore_info is set.
	 * Mark as uneven to distinguish from real vmcore_info pointer.
	 */
	get_lowcore()->vmcore_info = __kaslr_offset_phys ? __kaslr_offset_phys | 0x1UL : 0;

	/*
	 * Jump to the decompressed kernel entry point and switch DAT mode on.
	 */
	psw.addr = __kaslr_offset + vmlinux.entry;
	psw.mask = PSW_KERNEL_BITS;
	boot_debug("Starting kernel at:  0x%016lx\n", psw.addr);
	__load_psw(psw);
}
