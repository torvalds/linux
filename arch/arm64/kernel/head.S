/*
 * Low-level CPU initialisation
 * Based on arch/arm/kernel/head.S
 *
 * Copyright (C) 1994-2002 Russell King
 * Copyright (C) 2003-2012 ARM Ltd.
 * Authors:	Catalin Marinas <catalin.marinas@arm.com>
 *		Will Deacon <will.deacon@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/linkage.h>
#include <linux/init.h>

#include <asm/assembler.h>
#include <asm/ptrace.h>
#include <asm/asm-offsets.h>
#include <asm/cache.h>
#include <asm/cputype.h>
#include <asm/memory.h>
#include <asm/thread_info.h>
#include <asm/pgtable-hwdef.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/virt.h>

#define KERNEL_RAM_VADDR	(PAGE_OFFSET + TEXT_OFFSET)

#if (KERNEL_RAM_VADDR & 0xfffff) != 0x80000
#error KERNEL_RAM_VADDR must start at 0xXXX80000
#endif

	.macro	pgtbl, ttb0, ttb1, virt_to_phys
	ldr	\ttb1, =swapper_pg_dir
	ldr	\ttb0, =idmap_pg_dir
	add	\ttb1, \ttb1, \virt_to_phys
	add	\ttb0, \ttb0, \virt_to_phys
	.endm

#ifdef CONFIG_ARM64_64K_PAGES
#define BLOCK_SHIFT	PAGE_SHIFT
#define BLOCK_SIZE	PAGE_SIZE
#else
#define BLOCK_SHIFT	SECTION_SHIFT
#define BLOCK_SIZE	SECTION_SIZE
#endif

#define KERNEL_START	KERNEL_RAM_VADDR
#define KERNEL_END	_end

/*
 * Initial memory map attributes.
 */
#ifndef CONFIG_SMP
#define PTE_FLAGS	PTE_TYPE_PAGE | PTE_AF
#define PMD_FLAGS	PMD_TYPE_SECT | PMD_SECT_AF
#else
#define PTE_FLAGS	PTE_TYPE_PAGE | PTE_AF | PTE_SHARED
#define PMD_FLAGS	PMD_TYPE_SECT | PMD_SECT_AF | PMD_SECT_S
#endif

#ifdef CONFIG_ARM64_64K_PAGES
#define MM_MMUFLAGS	PTE_ATTRINDX(MT_NORMAL) | PTE_FLAGS
#else
#define MM_MMUFLAGS	PMD_ATTRINDX(MT_NORMAL) | PMD_FLAGS
#endif

/*
 * Kernel startup entry point.
 * ---------------------------
 *
 * The requirements are:
 *   MMU = off, D-cache = off, I-cache = on or off,
 *   x0 = physical address to the FDT blob.
 *
 * This code is mostly position independent so you call this at
 * __pa(PAGE_OFFSET + TEXT_OFFSET).
 *
 * Note that the callee-saved registers are used for storing variables
 * that are useful before the MMU is enabled. The allocations are described
 * in the entry routines.
 */
	__HEAD

	/*
	 * DO NOT MODIFY. Image header expected by Linux boot-loaders.
	 */
#ifdef CONFIG_EFI
efi_head:
	/*
	 * This add instruction has no meaningful effect except that
	 * its opcode forms the magic "MZ" signature required by UEFI.
	 */
	add	x13, x18, #0x16
	b	stext
#else
	b	stext				// branch to kernel start, magic
	.long	0				// reserved
#endif
	.quad	_kernel_offset_le		// Image load offset from start of RAM, little-endian
	.quad	_kernel_size_le			// Effective size of kernel image, little-endian
	.quad	_kernel_flags_le		// Informative flags, little-endian
	.quad	0				// reserved
	.quad	0				// reserved
	.quad	0				// reserved
	.byte	0x41				// Magic number, "ARM\x64"
	.byte	0x52
	.byte	0x4d
	.byte	0x64
#ifdef CONFIG_EFI
	.long	pe_header - efi_head		// Offset to the PE header.
#else
	.word	0				// reserved
#endif

#ifdef CONFIG_EFI
	.align 3
pe_header:
	.ascii	"PE"
	.short 	0
coff_header:
	.short	0xaa64				// AArch64
	.short	2				// nr_sections
	.long	0 				// TimeDateStamp
	.long	0				// PointerToSymbolTable
	.long	1				// NumberOfSymbols
	.short	section_table - optional_header	// SizeOfOptionalHeader
	.short	0x206				// Characteristics.
						// IMAGE_FILE_DEBUG_STRIPPED |
						// IMAGE_FILE_EXECUTABLE_IMAGE |
						// IMAGE_FILE_LINE_NUMS_STRIPPED
optional_header:
	.short	0x20b				// PE32+ format
	.byte	0x02				// MajorLinkerVersion
	.byte	0x14				// MinorLinkerVersion
	.long	_edata - stext			// SizeOfCode
	.long	0				// SizeOfInitializedData
	.long	0				// SizeOfUninitializedData
	.long	efi_stub_entry - efi_head	// AddressOfEntryPoint
	.long	stext - efi_head		// BaseOfCode

extra_header_fields:
	.quad	0				// ImageBase
	.long	0x20				// SectionAlignment
	.long	0x8				// FileAlignment
	.short	0				// MajorOperatingSystemVersion
	.short	0				// MinorOperatingSystemVersion
	.short	0				// MajorImageVersion
	.short	0				// MinorImageVersion
	.short	0				// MajorSubsystemVersion
	.short	0				// MinorSubsystemVersion
	.long	0				// Win32VersionValue

	.long	_edata - efi_head		// SizeOfImage

	// Everything before the kernel image is considered part of the header
	.long	stext - efi_head		// SizeOfHeaders
	.long	0				// CheckSum
	.short	0xa				// Subsystem (EFI application)
	.short	0				// DllCharacteristics
	.quad	0				// SizeOfStackReserve
	.quad	0				// SizeOfStackCommit
	.quad	0				// SizeOfHeapReserve
	.quad	0				// SizeOfHeapCommit
	.long	0				// LoaderFlags
	.long	0x6				// NumberOfRvaAndSizes

	.quad	0				// ExportTable
	.quad	0				// ImportTable
	.quad	0				// ResourceTable
	.quad	0				// ExceptionTable
	.quad	0				// CertificationTable
	.quad	0				// BaseRelocationTable

	// Section table
section_table:

	/*
	 * The EFI application loader requires a relocation section
	 * because EFI applications must be relocatable.  This is a
	 * dummy section as far as we are concerned.
	 */
	.ascii	".reloc"
	.byte	0
	.byte	0			// end of 0 padding of section name
	.long	0
	.long	0
	.long	0			// SizeOfRawData
	.long	0			// PointerToRawData
	.long	0			// PointerToRelocations
	.long	0			// PointerToLineNumbers
	.short	0			// NumberOfRelocations
	.short	0			// NumberOfLineNumbers
	.long	0x42100040		// Characteristics (section flags)


	.ascii	".text"
	.byte	0
	.byte	0
	.byte	0        		// end of 0 padding of section name
	.long	_edata - stext		// VirtualSize
	.long	stext - efi_head	// VirtualAddress
	.long	_edata - stext		// SizeOfRawData
	.long	stext - efi_head	// PointerToRawData

	.long	0		// PointerToRelocations (0 for executables)
	.long	0		// PointerToLineNumbers (0 for executables)
	.short	0		// NumberOfRelocations  (0 for executables)
	.short	0		// NumberOfLineNumbers  (0 for executables)
	.long	0xe0500020	// Characteristics (section flags)
	.align 5
#endif

ENTRY(stext)
	mov	x21, x0				// x21=FDT
	bl	el2_setup			// Drop to EL1, w20=cpu_boot_mode
	bl	__calc_phys_offset		// x24=PHYS_OFFSET, x28=PHYS_OFFSET-PAGE_OFFSET
	bl	set_cpu_boot_mode_flag
	mrs	x22, midr_el1			// x22=cpuid
	mov	x0, x22
	bl	lookup_processor_type
	mov	x23, x0				// x23=current cpu_table
	cbz	x23, __error_p			// invalid processor (x23=0)?
	bl	__vet_fdt
	bl	__create_page_tables		// x25=TTBR0, x26=TTBR1
	/*
	 * The following calls CPU specific code in a position independent
	 * manner. See arch/arm64/mm/proc.S for details. x23 = base of
	 * cpu_info structure selected by lookup_processor_type above.
	 * On return, the CPU will be ready for the MMU to be turned on and
	 * the TCR will have been set.
	 */
	ldr	x27, __switch_data		// address to jump to after
						// MMU has been enabled
	adr	lr, __enable_mmu		// return (PIC) address
	ldr	x12, [x23, #CPU_INFO_SETUP]
	add	x12, x12, x28			// __virt_to_phys
	br	x12				// initialise processor
ENDPROC(stext)

/*
 * If we're fortunate enough to boot at EL2, ensure that the world is
 * sane before dropping to EL1.
 *
 * Returns either BOOT_CPU_MODE_EL1 or BOOT_CPU_MODE_EL2 in x20 if
 * booted in EL1 or EL2 respectively.
 */
ENTRY(el2_setup)
	mrs	x0, CurrentEL
	cmp	x0, #CurrentEL_EL2
	b.ne	1f
	mrs	x0, sctlr_el2
CPU_BE(	orr	x0, x0, #(1 << 25)	)	// Set the EE bit for EL2
CPU_LE(	bic	x0, x0, #(1 << 25)	)	// Clear the EE bit for EL2
	msr	sctlr_el2, x0
	b	2f
1:	mrs	x0, sctlr_el1
CPU_BE(	orr	x0, x0, #(3 << 24)	)	// Set the EE and E0E bits for EL1
CPU_LE(	bic	x0, x0, #(3 << 24)	)	// Clear the EE and E0E bits for EL1
	msr	sctlr_el1, x0
	mov	w20, #BOOT_CPU_MODE_EL1		// This cpu booted in EL1
	isb
	ret

	/* Hyp configuration. */
2:	mov	x0, #(1 << 31)			// 64-bit EL1
	msr	hcr_el2, x0

	/* Generic timers. */
	mrs	x0, cnthctl_el2
	orr	x0, x0, #3			// Enable EL1 physical timers
	msr	cnthctl_el2, x0
	msr	cntvoff_el2, xzr		// Clear virtual offset

	/* Populate ID registers. */
	mrs	x0, midr_el1
	mrs	x1, mpidr_el1
	msr	vpidr_el2, x0
	msr	vmpidr_el2, x1

	/* sctlr_el1 */
	mov	x0, #0x0800			// Set/clear RES{1,0} bits
CPU_BE(	movk	x0, #0x33d0, lsl #16	)	// Set EE and E0E on BE systems
CPU_LE(	movk	x0, #0x30d0, lsl #16	)	// Clear EE and E0E on LE systems
	msr	sctlr_el1, x0

	/* Coprocessor traps. */
	mov	x0, #0x33ff
	msr	cptr_el2, x0			// Disable copro. traps to EL2

#ifdef CONFIG_COMPAT
	msr	hstr_el2, xzr			// Disable CP15 traps to EL2
#endif

	/* Stage-2 translation */
	msr	vttbr_el2, xzr

	/* Hypervisor stub */
	adr	x0, __hyp_stub_vectors
	msr	vbar_el2, x0

	/* spsr */
	mov	x0, #(PSR_F_BIT | PSR_I_BIT | PSR_A_BIT | PSR_D_BIT |\
		      PSR_MODE_EL1h)
	msr	spsr_el2, x0
	msr	elr_el2, lr
	mov	w20, #BOOT_CPU_MODE_EL2		// This CPU booted in EL2
	eret
ENDPROC(el2_setup)

/*
 * Sets the __boot_cpu_mode flag depending on the CPU boot mode passed
 * in x20. See arch/arm64/include/asm/virt.h for more info.
 */
ENTRY(set_cpu_boot_mode_flag)
	ldr	x1, =__boot_cpu_mode		// Compute __boot_cpu_mode
	add	x1, x1, x28
	cmp	w20, #BOOT_CPU_MODE_EL2
	b.ne	1f
	add	x1, x1, #4
1:	str	w20, [x1]			// This CPU has booted in EL1
	dmb	sy
	dc	ivac, x1			// Invalidate potentially stale cache line
	ret
ENDPROC(set_cpu_boot_mode_flag)

/*
 * We need to find out the CPU boot mode long after boot, so we need to
 * store it in a writable variable.
 *
 * This is not in .bss, because we set it sufficiently early that the boot-time
 * zeroing of .bss would clobber it.
 */
	.pushsection	.data..cacheline_aligned
ENTRY(__boot_cpu_mode)
	.align	L1_CACHE_SHIFT
	.long	BOOT_CPU_MODE_EL2
	.long	0
	.popsection

	.align	3
2:	.quad	.
	.quad	PAGE_OFFSET

#ifdef CONFIG_SMP
	.align	3
1:	.quad	.
	.quad	secondary_holding_pen_release

	/*
	 * This provides a "holding pen" for platforms to hold all secondary
	 * cores are held until we're ready for them to initialise.
	 */
ENTRY(secondary_holding_pen)
	bl	el2_setup			// Drop to EL1, w20=cpu_boot_mode
	bl	__calc_phys_offset		// x24=PHYS_OFFSET, x28=PHYS_OFFSET-PAGE_OFFSET
	bl	set_cpu_boot_mode_flag
	mrs	x0, mpidr_el1
	ldr     x1, =MPIDR_HWID_BITMASK
	and	x0, x0, x1
	adr	x1, 1b
	ldp	x2, x3, [x1]
	sub	x1, x1, x2
	add	x3, x3, x1
pen:	ldr	x4, [x3]
	cmp	x4, x0
	b.eq	secondary_startup
	wfe
	b	pen
ENDPROC(secondary_holding_pen)

	/*
	 * Secondary entry point that jumps straight into the kernel. Only to
	 * be used where CPUs are brought online dynamically by the kernel.
	 */
ENTRY(secondary_entry)
	bl	el2_setup			// Drop to EL1
	bl	__calc_phys_offset		// x24=PHYS_OFFSET, x28=PHYS_OFFSET-PAGE_OFFSET
	bl	set_cpu_boot_mode_flag
	b	secondary_startup
ENDPROC(secondary_entry)

ENTRY(secondary_startup)
	/*
	 * Common entry point for secondary CPUs.
	 */
	mrs	x22, midr_el1			// x22=cpuid
	mov	x0, x22
	bl	lookup_processor_type
	mov	x23, x0				// x23=current cpu_table
	cbz	x23, __error_p			// invalid processor (x23=0)?

	pgtbl	x25, x26, x28			// x25=TTBR0, x26=TTBR1
	ldr	x12, [x23, #CPU_INFO_SETUP]
	add	x12, x12, x28			// __virt_to_phys
	blr	x12				// initialise processor

	ldr	x21, =secondary_data
	ldr	x27, =__secondary_switched	// address to jump to after enabling the MMU
	b	__enable_mmu
ENDPROC(secondary_startup)

ENTRY(__secondary_switched)
	ldr	x0, [x21]			// get secondary_data.stack
	mov	sp, x0
	mov	x29, #0
	b	secondary_start_kernel
ENDPROC(__secondary_switched)
#endif	/* CONFIG_SMP */

/*
 * Setup common bits before finally enabling the MMU. Essentially this is just
 * loading the page table pointer and vector base registers.
 *
 * On entry to this code, x0 must contain the SCTLR_EL1 value for turning on
 * the MMU.
 */
__enable_mmu:
	ldr	x5, =vectors
	msr	vbar_el1, x5
	msr	ttbr0_el1, x25			// load TTBR0
	msr	ttbr1_el1, x26			// load TTBR1
	isb
	b	__turn_mmu_on
ENDPROC(__enable_mmu)

/*
 * Enable the MMU. This completely changes the structure of the visible memory
 * space. You will not be able to trace execution through this.
 *
 *  x0  = system control register
 *  x27 = *virtual* address to jump to upon completion
 *
 * other registers depend on the function called upon completion
 *
 * We align the entire function to the smallest power of two larger than it to
 * ensure it fits within a single block map entry. Otherwise were PHYS_OFFSET
 * close to the end of a 512MB or 1GB block we might require an additional
 * table to map the entire function.
 */
	.align	4
__turn_mmu_on:
	msr	sctlr_el1, x0
	isb
	br	x27
ENDPROC(__turn_mmu_on)

/*
 * Calculate the start of physical memory.
 */
__calc_phys_offset:
	adr	x0, 1f
	ldp	x1, x2, [x0]
	sub	x28, x0, x1			// x28 = PHYS_OFFSET - PAGE_OFFSET
	add	x24, x2, x28			// x24 = PHYS_OFFSET
	ret
ENDPROC(__calc_phys_offset)

	.align 3
1:	.quad	.
	.quad	PAGE_OFFSET

/*
 * Macro to populate the PGD for the corresponding block entry in the next
 * level (tbl) for the given virtual address.
 *
 * Preserves:	pgd, tbl, virt
 * Corrupts:	tmp1, tmp2
 */
	.macro	create_pgd_entry, pgd, tbl, virt, tmp1, tmp2
	lsr	\tmp1, \virt, #PGDIR_SHIFT
	and	\tmp1, \tmp1, #PTRS_PER_PGD - 1	// PGD index
	orr	\tmp2, \tbl, #3			// PGD entry table type
	str	\tmp2, [\pgd, \tmp1, lsl #3]
	.endm

/*
 * Macro to populate block entries in the page table for the start..end
 * virtual range (inclusive).
 *
 * Preserves:	tbl, flags
 * Corrupts:	phys, start, end, pstate
 */
	.macro	create_block_map, tbl, flags, phys, start, end
	lsr	\phys, \phys, #BLOCK_SHIFT
	lsr	\start, \start, #BLOCK_SHIFT
	and	\start, \start, #PTRS_PER_PTE - 1	// table index
	orr	\phys, \flags, \phys, lsl #BLOCK_SHIFT	// table entry
	lsr	\end, \end, #BLOCK_SHIFT
	and	\end, \end, #PTRS_PER_PTE - 1		// table end index
9999:	str	\phys, [\tbl, \start, lsl #3]		// store the entry
	add	\start, \start, #1			// next entry
	add	\phys, \phys, #BLOCK_SIZE		// next block
	cmp	\start, \end
	b.ls	9999b
	.endm

/*
 * Setup the initial page tables. We only setup the barest amount which is
 * required to get the kernel running. The following sections are required:
 *   - identity mapping to enable the MMU (low address, TTBR0)
 *   - first few MB of the kernel linear mapping to jump to once the MMU has
 *     been enabled, including the FDT blob (TTBR1)
 *   - pgd entry for fixed mappings (TTBR1)
 */
__create_page_tables:
	pgtbl	x25, x26, x28			// idmap_pg_dir and swapper_pg_dir addresses
	mov	x27, lr

	/*
	 * Invalidate the idmap and swapper page tables to avoid potential
	 * dirty cache lines being evicted.
	 */
	mov	x0, x25
	add	x1, x26, #SWAPPER_DIR_SIZE
	bl	__inval_cache_range

	/*
	 * Clear the idmap and swapper page tables.
	 */
	mov	x0, x25
	add	x6, x26, #SWAPPER_DIR_SIZE
1:	stp	xzr, xzr, [x0], #16
	stp	xzr, xzr, [x0], #16
	stp	xzr, xzr, [x0], #16
	stp	xzr, xzr, [x0], #16
	cmp	x0, x6
	b.lo	1b

	ldr	x7, =MM_MMUFLAGS

	/*
	 * Create the identity mapping.
	 */
	add	x0, x25, #PAGE_SIZE		// section table address
	ldr	x3, =KERNEL_START
	add	x3, x3, x28			// __pa(KERNEL_START)
	create_pgd_entry x25, x0, x3, x5, x6
	ldr	x6, =KERNEL_END
	mov	x5, x3				// __pa(KERNEL_START)
	add	x6, x6, x28			// __pa(KERNEL_END)
	create_block_map x0, x7, x3, x5, x6

	/*
	 * Map the kernel image (starting with PHYS_OFFSET).
	 */
	add	x0, x26, #PAGE_SIZE		// section table address
	mov	x5, #PAGE_OFFSET
	create_pgd_entry x26, x0, x5, x3, x6
	ldr	x6, =KERNEL_END
	mov	x3, x24				// phys offset
	create_block_map x0, x7, x3, x5, x6

	/*
	 * Map the FDT blob (maximum 2MB; must be within 512MB of
	 * PHYS_OFFSET).
	 */
	mov	x3, x21				// FDT phys address
	and	x3, x3, #~((1 << 21) - 1)	// 2MB aligned
	mov	x6, #PAGE_OFFSET
	sub	x5, x3, x24			// subtract PHYS_OFFSET
	tst	x5, #~((1 << 29) - 1)		// within 512MB?
	csel	x21, xzr, x21, ne		// zero the FDT pointer
	b.ne	1f
	add	x5, x5, x6			// __va(FDT blob)
	add	x6, x5, #1 << 21		// 2MB for the FDT blob
	sub	x6, x6, #1			// inclusive range
	create_block_map x0, x7, x3, x5, x6
1:
	/*
	 * Create the pgd entry for the fixed mappings.
	 */
	ldr	x5, =FIXADDR_TOP		// Fixed mapping virtual address
	add	x0, x26, #2 * PAGE_SIZE		// section table address
	create_pgd_entry x26, x0, x5, x6, x7

	/*
	 * Since the page tables have been populated with non-cacheable
	 * accesses (MMU disabled), invalidate the idmap and swapper page
	 * tables again to remove any speculatively loaded cache lines.
	 */
	mov	x0, x25
	add	x1, x26, #SWAPPER_DIR_SIZE
	bl	__inval_cache_range

	mov	lr, x27
	ret
ENDPROC(__create_page_tables)
	.ltorg

	.align	3
	.type	__switch_data, %object
__switch_data:
	.quad	__mmap_switched
	.quad	__bss_start			// x6
	.quad	__bss_stop			// x7
	.quad	processor_id			// x4
	.quad	__fdt_pointer			// x5
	.quad	memstart_addr			// x6
	.quad	init_thread_union + THREAD_START_SP // sp

/*
 * The following fragment of code is executed with the MMU on in MMU mode, and
 * uses absolute addresses; this is not position independent.
 */
__mmap_switched:
	adr	x3, __switch_data + 8

	ldp	x6, x7, [x3], #16
1:	cmp	x6, x7
	b.hs	2f
	str	xzr, [x6], #8			// Clear BSS
	b	1b
2:
	ldp	x4, x5, [x3], #16
	ldr	x6, [x3], #8
	ldr	x16, [x3]
	mov	sp, x16
	str	x22, [x4]			// Save processor ID
	str	x21, [x5]			// Save FDT pointer
	str	x24, [x6]			// Save PHYS_OFFSET
	mov	x29, #0
	b	start_kernel
ENDPROC(__mmap_switched)

/*
 * Exception handling. Something went wrong and we can't proceed. We ought to
 * tell the user, but since we don't have any guarantee that we're even
 * running on the right architecture, we do virtually nothing.
 */
__error_p:
ENDPROC(__error_p)

__error:
1:	nop
	b	1b
ENDPROC(__error)

/*
 * This function gets the processor ID in w0 and searches the cpu_table[] for
 * a match. It returns a pointer to the struct cpu_info it found. The
 * cpu_table[] must end with an empty (all zeros) structure.
 *
 * This routine can be called via C code and it needs to work with the MMU
 * both disabled and enabled (the offset is calculated automatically).
 */
ENTRY(lookup_processor_type)
	adr	x1, __lookup_processor_type_data
	ldp	x2, x3, [x1]
	sub	x1, x1, x2			// get offset between VA and PA
	add	x3, x3, x1			// convert VA to PA
1:
	ldp	w5, w6, [x3]			// load cpu_id_val and cpu_id_mask
	cbz	w5, 2f				// end of list?
	and	w6, w6, w0
	cmp	w5, w6
	b.eq	3f
	add	x3, x3, #CPU_INFO_SZ
	b	1b
2:
	mov	x3, #0				// unknown processor
3:
	mov	x0, x3
	ret
ENDPROC(lookup_processor_type)

	.align	3
	.type	__lookup_processor_type_data, %object
__lookup_processor_type_data:
	.quad	.
	.quad	cpu_table
	.size	__lookup_processor_type_data, . - __lookup_processor_type_data

/*
 * Determine validity of the x21 FDT pointer.
 * The dtb must be 8-byte aligned and live in the first 512M of memory.
 */
__vet_fdt:
	tst	x21, #0x7
	b.ne	1f
	cmp	x21, x24
	b.lt	1f
	mov	x0, #(1 << 29)
	add	x0, x0, x24
	cmp	x21, x0
	b.ge	1f
	ret
1:
	mov	x21, #0
	ret
ENDPROC(__vet_fdt)
