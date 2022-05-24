// SPDX-License-Identifier: GPL-2.0
#include <linux/string.h>
#include <linux/elf.h>
#include <asm/boot_data.h>
#include <asm/sections.h>
#include <asm/cpu_mf.h>
#include <asm/setup.h>
#include <asm/kasan.h>
#include <asm/kexec.h>
#include <asm/sclp.h>
#include <asm/diag.h>
#include <asm/uv.h>
#include "decompressor.h"
#include "boot.h"
#include "uv.h"

unsigned long __bootdata_preserved(__kaslr_offset);
unsigned long __bootdata(__amode31_base);
unsigned long __bootdata_preserved(VMALLOC_START);
unsigned long __bootdata_preserved(VMALLOC_END);
struct page *__bootdata_preserved(vmemmap);
unsigned long __bootdata_preserved(vmemmap_size);
unsigned long __bootdata_preserved(MODULES_VADDR);
unsigned long __bootdata_preserved(MODULES_END);
unsigned long __bootdata(ident_map_size);
int __bootdata(is_full_image) = 1;
struct initrd_data __bootdata(initrd_data);

u64 __bootdata_preserved(stfle_fac_list[16]);
u64 __bootdata_preserved(alt_stfle_fac_list[16]);
struct oldmem_data __bootdata_preserved(oldmem_data);

void error(char *x)
{
	sclp_early_printk("\n\n");
	sclp_early_printk(x);
	sclp_early_printk("\n\n -- System halted");

	disabled_wait();
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

static void rescue_initrd(unsigned long addr)
{
	if (!IS_ENABLED(CONFIG_BLK_DEV_INITRD))
		return;
	if (!initrd_data.start || !initrd_data.size)
		return;
	if (addr <= initrd_data.start)
		return;
	memmove((void *)addr, (void *)initrd_data.start, initrd_data.size);
	initrd_data.start = addr;
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
 *    Always <= end of the last online memory block (get_mem_detect_end()).
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
		kaslr_enabled = 0;
		ident_map_size = min(ident_map_size, oldmem_data.size);
	} else if (ipl_block_valid && is_ipl_block_dump()) {
		kaslr_enabled = 0;
		if (!sclp_early_get_hsa_size(&hsa_size) && hsa_size)
			ident_map_size = min(ident_map_size, hsa_size);
	}
#endif
}

static void setup_kernel_memory_layout(void)
{
	unsigned long vmemmap_start;
	unsigned long rte_size;
	unsigned long pages;

	pages = ident_map_size / PAGE_SIZE;
	/* vmemmap contains a multiple of PAGES_PER_SECTION struct pages */
	vmemmap_size = SECTION_ALIGN_UP(pages) * sizeof(struct page);

	/* choose kernel address space layout: 4 or 3 levels. */
	vmemmap_start = round_up(ident_map_size, _REGION3_SIZE);
	if (IS_ENABLED(CONFIG_KASAN) ||
	    vmalloc_size > _REGION2_SIZE ||
	    vmemmap_start + vmemmap_size + vmalloc_size + MODULES_LEN >
		    _REGION2_SIZE) {
		MODULES_END = _REGION1_SIZE;
		rte_size = _REGION2_SIZE;
	} else {
		MODULES_END = _REGION2_SIZE;
		rte_size = _REGION3_SIZE;
	}
	/*
	 * forcing modules and vmalloc area under the ultravisor
	 * secure storage limit, so that any vmalloc allocation
	 * we do could be used to back secure guest storage.
	 */
	adjust_to_uv_max(&MODULES_END);
#ifdef CONFIG_KASAN
	/* force vmalloc and modules below kasan shadow */
	MODULES_END = min(MODULES_END, KASAN_SHADOW_START);
#endif
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
	/* vmemmap_start is the future VMEM_MAX_PHYS, make sure it is within MAX_PHYSMEM */
	vmemmap_start = min(vmemmap_start, 1UL << MAX_PHYSMEM_BITS);
	/* make sure identity map doesn't overlay with vmemmap */
	ident_map_size = min(ident_map_size, vmemmap_start);
	vmemmap_size = SECTION_ALIGN_UP(ident_map_size / PAGE_SIZE) * sizeof(struct page);
	/* make sure vmemmap doesn't overlay with vmalloc area */
	VMALLOC_START = max(vmemmap_start + vmemmap_size, VMALLOC_START);
	vmemmap = (struct page *)vmemmap_start;
}

/*
 * This function clears the BSS section of the decompressed Linux kernel and NOT the decompressor's.
 */
static void clear_bss_section(void)
{
	memset((void *)vmlinux.default_lma + vmlinux.image_size, 0, vmlinux.bss_size);
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
	vmlinux.default_lma += offset;
	*(unsigned long *)(&vmlinux.entry) += offset;
	vmlinux.bootdata_off += offset;
	vmlinux.bootdata_preserved_off += offset;
	vmlinux.rela_dyn_start += offset;
	vmlinux.rela_dyn_end += offset;
	vmlinux.dynsym_start += offset;
}

static unsigned long reserve_amode31(unsigned long safe_addr)
{
	__amode31_base = PAGE_ALIGN(safe_addr);
	return safe_addr + vmlinux.amode31_size;
}

void startup_kernel(void)
{
	unsigned long random_lma;
	unsigned long safe_addr;
	void *img;

	initrd_data.start = parmarea.initrd_start;
	initrd_data.size = parmarea.initrd_size;
	oldmem_data.start = parmarea.oldmem_base;
	oldmem_data.size = parmarea.oldmem_size;

	setup_lpp();
	store_ipl_parmblock();
	safe_addr = mem_safe_offset();
	safe_addr = reserve_amode31(safe_addr);
	safe_addr = read_ipl_report(safe_addr);
	uv_query_info();
	rescue_initrd(safe_addr);
	sclp_early_read_info();
	setup_boot_command_line();
	parse_boot_command_line();
	sanitize_prot_virt_host();
	setup_ident_map_size(detect_memory());
	setup_vmalloc_size();
	setup_kernel_memory_layout();

	if (IS_ENABLED(CONFIG_RANDOMIZE_BASE) && kaslr_enabled) {
		random_lma = get_random_base(safe_addr);
		if (random_lma) {
			__kaslr_offset = random_lma - vmlinux.default_lma;
			img = (void *)vmlinux.default_lma;
			offset_vmlinux_info(__kaslr_offset);
		}
	}

	if (!IS_ENABLED(CONFIG_KERNEL_UNCOMPRESSED)) {
		img = decompress_kernel();
		memmove((void *)vmlinux.default_lma, img, vmlinux.image_size);
	} else if (__kaslr_offset)
		memcpy((void *)vmlinux.default_lma, img, vmlinux.image_size);

	clear_bss_section();
	copy_bootdata();
	if (IS_ENABLED(CONFIG_RELOCATABLE))
		handle_relocs(__kaslr_offset);

	if (__kaslr_offset) {
		/*
		 * Save KASLR offset for early dumps, before vmcore_info is set.
		 * Mark as uneven to distinguish from real vmcore_info pointer.
		 */
		S390_lowcore.vmcore_info = __kaslr_offset | 0x1UL;
		/* Clear non-relocated kernel */
		if (IS_ENABLED(CONFIG_KERNEL_UNCOMPRESSED))
			memset(img, 0, vmlinux.image_size);
	}
	vmlinux.entry();
}
