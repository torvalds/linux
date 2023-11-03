// SPDX-License-Identifier: GPL-2.0
#include <linux/string.h>
#include <linux/elf.h>
#include <asm/boot_data.h>
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

unsigned long __bootdata_preserved(__kaslr_offset);
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
unsigned long __bootdata(ident_map_size);

u64 __bootdata_preserved(stfle_fac_list[16]);
u64 __bootdata_preserved(alt_stfle_fac_list[16]);
struct oldmem_data __bootdata_preserved(oldmem_data);

struct machine_info machine;

void error(char *x)
{
	sclp_early_printk("\n\n");
	sclp_early_printk(x);
	sclp_early_printk("\n\n -- System halted");

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
	if (test_facility(130))
		machine.has_nx = 1;
}

static void setup_lpp(void)
{
	S390_lowcore.current_pid = 0;
	S390_lowcore.lpp = LPP_MAGIC;
	if (test_facility(40))
		lpp(&S390_lowcore.lpp);
}

#ifdef CONFIG_KERNEL_UNCOMPRESSED
unsigned long mem_safe_offset(void)
{
	return vmlinux.default_lma + vmlinux.image_size + vmlinux.bss_size;
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
	addr = physmem_alloc_top_down(RR_INITRD, size, 0);
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

static void handle_relocs(unsigned long offset)
{
	Elf64_Rela *rela_start, *rela_end, *rela;
	int r_type, r_sym, rc;
	Elf64_Addr loc, val;
	Elf64_Sym *dynsym;

	rela_start = (Elf64_Rela *) vmlinux.rela_dyn_start;
	rela_end = (Elf64_Rela *) vmlinux.rela_dyn_end;
	dynsym = (Elf64_Sym *) vmlinux.dynsym_start;
	for (rela = rela_start; rela < rela_end; rela++) {
		loc = rela->r_offset + offset;
		val = rela->r_addend;
		r_sym = ELF64_R_SYM(rela->r_info);
		if (r_sym) {
			if (dynsym[r_sym].st_shndx != SHN_UNDEF)
				val += dynsym[r_sym].st_value + offset;
		} else {
			/*
			 * 0 == undefined symbol table index (STN_UNDEF),
			 * used for R_390_RELATIVE, only add KASLR offset
			 */
			val += offset;
		}
		r_type = ELF64_R_TYPE(rela->r_info);
		rc = arch_kexec_do_relocs(r_type, (void *) loc, val, 0);
		if (rc)
			error("Unknown relocation type");
	}
}

/*
 * Merge information from several sources into a single ident_map_size value.
 * "ident_map_size" represents the upper limit of physical memory we may ever
 * reach. It might not be all online memory, but also include standby (offline)
 * memory. "ident_map_size" could be lower then actual standby or even online
 * memory present, due to limiting factors. We should never go above this limit.
 * It is the size of our identity mapping.
 *
 * Consider the following factors:
 * 1. max_physmem_end - end of physical memory online or standby.
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
	} else if (ipl_block_valid && is_ipl_block_dump()) {
		__kaslr_enabled = 0;
		if (!sclp_early_get_hsa_size(&hsa_size) && hsa_size)
			ident_map_size = min(ident_map_size, hsa_size);
	}
#endif
}

static unsigned long setup_kernel_memory_layout(void)
{
	unsigned long vmemmap_start;
	unsigned long asce_limit;
	unsigned long rte_size;
	unsigned long pages;
	unsigned long vsize;
	unsigned long vmax;

	pages = ident_map_size / PAGE_SIZE;
	/* vmemmap contains a multiple of PAGES_PER_SECTION struct pages */
	vmemmap_size = SECTION_ALIGN_UP(pages) * sizeof(struct page);

	/* choose kernel address space layout: 4 or 3 levels. */
	vsize = round_up(ident_map_size, _REGION3_SIZE) + vmemmap_size +
		MODULES_LEN + MEMCPY_REAL_SIZE + ABS_LOWCORE_MAP_SIZE;
	vsize = size_add(vsize, vmalloc_size);
	if (IS_ENABLED(CONFIG_KASAN) || (vsize > _REGION2_SIZE)) {
		asce_limit = _REGION1_SIZE;
		rte_size = _REGION2_SIZE;
	} else {
		asce_limit = _REGION2_SIZE;
		rte_size = _REGION3_SIZE;
	}

	/*
	 * Forcing modules and vmalloc area under the ultravisor
	 * secure storage limit, so that any vmalloc allocation
	 * we do could be used to back secure guest storage.
	 */
	vmax = adjust_to_uv_max(asce_limit);
#ifdef CONFIG_KASAN
	/* force vmalloc and modules below kasan shadow */
	vmax = min(vmax, KASAN_SHADOW_START);
#endif
	__memcpy_real_area = round_down(vmax - MEMCPY_REAL_SIZE, PAGE_SIZE);
	__abs_lowcore = round_down(__memcpy_real_area - ABS_LOWCORE_MAP_SIZE,
				   sizeof(struct lowcore));
	MODULES_END = round_down(__abs_lowcore, _SEGMENT_SIZE);
	MODULES_VADDR = MODULES_END - MODULES_LEN;
	VMALLOC_END = MODULES_VADDR;

	/* allow vmalloc area to occupy up to about 1/2 of the rest virtual space left */
	vmalloc_size = min(vmalloc_size, round_down(VMALLOC_END / 2, _REGION3_SIZE));
	VMALLOC_START = VMALLOC_END - vmalloc_size;

	/* split remaining virtual space between 1:1 mapping & vmemmap array */
	pages = VMALLOC_START / (PAGE_SIZE + sizeof(struct page));
	pages = SECTION_ALIGN_UP(pages);
	/* keep vmemmap_start aligned to a top level region table entry */
	vmemmap_start = round_down(VMALLOC_START - pages * sizeof(struct page), rte_size);
	vmemmap_start = min(vmemmap_start, 1UL << MAX_PHYSMEM_BITS);
	/* maximum mappable address as seen by arch_get_mappable_range() */
	max_mappable = vmemmap_start;
	/* make sure identity map doesn't overlay with vmemmap */
	ident_map_size = min(ident_map_size, vmemmap_start);
	vmemmap_size = SECTION_ALIGN_UP(ident_map_size / PAGE_SIZE) * sizeof(struct page);
	/* make sure vmemmap doesn't overlay with vmalloc area */
	VMALLOC_START = max(vmemmap_start + vmemmap_size, VMALLOC_START);
	vmemmap = (struct page *)vmemmap_start;

	return asce_limit;
}

/*
 * This function clears the BSS section of the decompressed Linux kernel and NOT the decompressor's.
 */
static void clear_bss_section(unsigned long vmlinux_lma)
{
	memset((void *)vmlinux_lma + vmlinux.image_size, 0, vmlinux.bss_size);
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

static void offset_vmlinux_info(unsigned long offset)
{
	*(unsigned long *)(&vmlinux.entry) += offset;
	vmlinux.bootdata_off += offset;
	vmlinux.bootdata_preserved_off += offset;
	vmlinux.rela_dyn_start += offset;
	vmlinux.rela_dyn_end += offset;
	vmlinux.dynsym_start += offset;
	vmlinux.init_mm_off += offset;
	vmlinux.swapper_pg_dir_off += offset;
	vmlinux.invalid_pg_dir_off += offset;
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
	unsigned long max_physmem_end;
	unsigned long vmlinux_lma = 0;
	unsigned long amode31_lma = 0;
	unsigned long asce_limit;
	unsigned long safe_addr;
	void *img;
	psw_t psw;

	setup_lpp();
	safe_addr = mem_safe_offset();

	/*
	 * Reserve decompressor memory together with decompression heap, buffer and
	 * memory which might be occupied by uncompressed kernel at default 1Mb
	 * position (if KASLR is off or failed).
	 */
	physmem_reserve(RR_DECOMPRESSOR, 0, safe_addr);
	if (IS_ENABLED(CONFIG_BLK_DEV_INITRD) && parmarea.initrd_size)
		physmem_reserve(RR_INITRD, parmarea.initrd_start, parmarea.initrd_size);
	oldmem_data.start = parmarea.oldmem_base;
	oldmem_data.size = parmarea.oldmem_size;

	store_ipl_parmblock();
	read_ipl_report();
	uv_query_info();
	sclp_early_read_info();
	setup_boot_command_line();
	parse_boot_command_line();
	detect_facilities();
	sanitize_prot_virt_host();
	max_physmem_end = detect_max_physmem_end();
	setup_ident_map_size(max_physmem_end);
	setup_vmalloc_size();
	asce_limit = setup_kernel_memory_layout();
	/* got final ident_map_size, physmem allocations could be performed now */
	physmem_set_usable_limit(ident_map_size);
	detect_physmem_online_ranges(max_physmem_end);
	save_ipl_cert_comp_list();
	rescue_initrd(safe_addr, ident_map_size);

	if (kaslr_enabled()) {
		vmlinux_lma = randomize_within_range(vmlinux.image_size + vmlinux.bss_size,
						     THREAD_SIZE, vmlinux.default_lma,
						     ident_map_size);
		if (vmlinux_lma) {
			__kaslr_offset = vmlinux_lma - vmlinux.default_lma;
			offset_vmlinux_info(__kaslr_offset);
		}
	}
	vmlinux_lma = vmlinux_lma ?: vmlinux.default_lma;
	physmem_reserve(RR_VMLINUX, vmlinux_lma, vmlinux.image_size + vmlinux.bss_size);

	if (!IS_ENABLED(CONFIG_KERNEL_UNCOMPRESSED)) {
		img = decompress_kernel();
		memmove((void *)vmlinux_lma, img, vmlinux.image_size);
	} else if (__kaslr_offset) {
		img = (void *)vmlinux.default_lma;
		memmove((void *)vmlinux_lma, img, vmlinux.image_size);
		memset(img, 0, vmlinux.image_size);
	}

	/* vmlinux decompression is done, shrink reserved low memory */
	physmem_reserve(RR_DECOMPRESSOR, 0, (unsigned long)_decompressor_end);
	if (kaslr_enabled())
		amode31_lma = randomize_within_range(vmlinux.amode31_size, PAGE_SIZE, 0, SZ_2G);
	amode31_lma = amode31_lma ?: vmlinux.default_lma - vmlinux.amode31_size;
	physmem_reserve(RR_AMODE31, amode31_lma, vmlinux.amode31_size);

	/*
	 * The order of the following operations is important:
	 *
	 * - handle_relocs() must follow clear_bss_section() to establish static
	 *   memory references to data in .bss to be used by setup_vmem()
	 *   (i.e init_mm.pgd)
	 *
	 * - setup_vmem() must follow handle_relocs() to be able using
	 *   static memory references to data in .bss (i.e init_mm.pgd)
	 *
	 * - copy_bootdata() must follow setup_vmem() to propagate changes to
	 *   bootdata made by setup_vmem()
	 */
	clear_bss_section(vmlinux_lma);
	handle_relocs(__kaslr_offset);
	setup_vmem(asce_limit);
	copy_bootdata();

	/*
	 * Save KASLR offset for early dumps, before vmcore_info is set.
	 * Mark as uneven to distinguish from real vmcore_info pointer.
	 */
	S390_lowcore.vmcore_info = __kaslr_offset ? __kaslr_offset | 0x1UL : 0;

	/*
	 * Jump to the decompressed kernel entry point and switch DAT mode on.
	 */
	psw.addr = vmlinux.entry;
	psw.mask = PSW_KERNEL_BITS;
	__load_psw(psw);
}
