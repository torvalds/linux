// SPDX-License-Identifier: GPL-2.0
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

u64 __bootdata_preserved(stfle_fac_list[16]);
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
		  [tmp] "=&d" (tmp),
		  "+Q" (S390_lowcore.program_new_psw),
		  "=Q" (old)
		: [psw_old] "a" (&old),
		  [psw_pgm] "a" (&S390_lowcore.program_new_psw),
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
	S390_lowcore.current_pid = 0;
	S390_lowcore.lpp = LPP_MAGIC;
	if (test_facility(40))
		lpp(&S390_lowcore.lpp);
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
		*(u64 *)loc += offset - __START_KERNEL;
	}
}

static void kaslr_adjust_got(unsigned long offset)
{
	u64 *entry;

	/*
	 * Even without -fPIE, Clang still uses a global offset table for some
	 * reason. Adjust the GOT entries.
	 */
	for (entry = (u64 *)vmlinux.got_start; entry < (u64 *)vmlinux.got_end; entry++)
		*entry += offset - __START_KERNEL;
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
	BUILD_BUG_ON(!IS_ALIGNED(__START_KERNEL, THREAD_SIZE));
	BUILD_BUG_ON(!IS_ALIGNED(__NO_KASLR_START_KERNEL, THREAD_SIZE));
	BUILD_BUG_ON(__NO_KASLR_END_KERNEL > _REGION1_SIZE);
	vsize = get_vmem_size(ident_map_size, vmemmap_size, vmalloc_size, _REGION3_SIZE);
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
#ifdef CONFIG_KASAN
	BUILD_BUG_ON(__NO_KASLR_END_KERNEL > KASAN_SHADOW_START);
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
	} else if (vmax < __NO_KASLR_END_KERNEL || vsize > __NO_KASLR_END_KERNEL) {
		kernel_start = round_down(vmax - kernel_size, THREAD_SIZE);
		decompressor_printk("The kernel base address is forced to %lx\n", kernel_start);
	} else {
		kernel_start = __NO_KASLR_START_KERNEL;
	}
	__kaslr_offset = kernel_start;

	MODULES_END = round_down(kernel_start, _SEGMENT_SIZE);
	MODULES_VADDR = MODULES_END - MODULES_LEN;
	VMALLOC_END = MODULES_VADDR;

	/* allow vmalloc area to occupy up to about 1/2 of the rest virtual space left */
	vsize = (VMALLOC_END - FIXMAP_SIZE) / 2;
	vsize = round_down(vsize, _SEGMENT_SIZE);
	vmalloc_size = min(vmalloc_size, vsize);
	VMALLOC_START = VMALLOC_END - vmalloc_size;

	__memcpy_real_area = round_down(VMALLOC_START - MEMCPY_REAL_SIZE, PAGE_SIZE);
	__abs_lowcore = round_down(__memcpy_real_area - ABS_LOWCORE_MAP_SIZE,
				   sizeof(struct lowcore));

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
	__identity_base = round_down(vmemmap_start - max_mappable, rte_size);

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
#ifdef CONFIG_KASAN
	vmlinux.kasan_early_shadow_page_off += offset;
	vmlinux.kasan_early_shadow_pte_off += offset;
	vmlinux.kasan_early_shadow_pmd_off += offset;
	vmlinux.kasan_early_shadow_pud_off += offset;
	vmlinux.kasan_early_shadow_p4d_off += offset;
#endif
}

static void fixup_vmlinux_info(void)
{
	vmlinux.entry -= __START_KERNEL;
	kaslr_adjust_vmlinux_info(-__START_KERNEL);
}

void startup_kernel(void)
{
	unsigned long kernel_size = vmlinux.image_size + vmlinux.bss_size;
	unsigned long nokaslr_offset_phys = mem_safe_offset();
	unsigned long amode31_lma = 0;
	unsigned long max_physmem_end;
	unsigned long asce_limit;
	unsigned long safe_addr;
	psw_t psw;

	fixup_vmlinux_info();
	setup_lpp();
	safe_addr = PAGE_ALIGN(nokaslr_offset_phys + kernel_size);

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

	store_ipl_parmblock();
	read_ipl_report();
	uv_query_info();
	sclp_early_read_info();
	setup_boot_command_line();
	parse_boot_command_line();
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

	if (kaslr_enabled())
		__kaslr_offset_phys = randomize_within_range(kernel_size, THREAD_SIZE, 0, ident_map_size);
	if (!__kaslr_offset_phys)
		__kaslr_offset_phys = nokaslr_offset_phys;
	kaslr_adjust_vmlinux_info(__kaslr_offset_phys);
	physmem_reserve(RR_VMLINUX, __kaslr_offset_phys, kernel_size);
	deploy_kernel((void *)__kaslr_offset_phys);

	/* vmlinux decompression is done, shrink reserved low memory */
	physmem_reserve(RR_DECOMPRESSOR, 0, (unsigned long)_decompressor_end);

	/*
	 * In case KASLR is enabled the randomized location of .amode31
	 * section might overlap with .vmlinux.relocs section. To avoid that
	 * the below randomize_within_range() could have been called with
	 * __vmlinux_relocs_64_end as the lower range address. However,
	 * .amode31 section is written to by the decompressed kernel - at
	 * that time the contents of .vmlinux.relocs is not needed anymore.
	 * Conversly, .vmlinux.relocs is read only by the decompressor, even
	 * before the kernel started. Therefore, in case the two sections
	 * overlap there is no risk of corrupting any data.
	 */
	if (kaslr_enabled())
		amode31_lma = randomize_within_range(vmlinux.amode31_size, PAGE_SIZE, 0, SZ_2G);
	if (!amode31_lma)
		amode31_lma = __kaslr_offset_phys - vmlinux.amode31_size;
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
	clear_bss_section(__kaslr_offset_phys);
	kaslr_adjust_relocs(__kaslr_offset_phys, __kaslr_offset_phys + vmlinux.image_size,
			    __kaslr_offset, __kaslr_offset_phys);
	kaslr_adjust_got(__kaslr_offset);
	setup_vmem(__kaslr_offset, __kaslr_offset + kernel_size, asce_limit);
	copy_bootdata();

	/*
	 * Save KASLR offset for early dumps, before vmcore_info is set.
	 * Mark as uneven to distinguish from real vmcore_info pointer.
	 */
	S390_lowcore.vmcore_info = __kaslr_offset_phys ? __kaslr_offset_phys | 0x1UL : 0;

	/*
	 * Jump to the decompressed kernel entry point and switch DAT mode on.
	 */
	psw.addr = __kaslr_offset + vmlinux.entry;
	psw.mask = PSW_KERNEL_BITS;
	__load_psw(psw);
}
