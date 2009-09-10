/*
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  Enhanced CPU detection and feature setting code by Mike Jagdis
 *  and Martin Mares, November 1997.
 */

.text
#include <linux/threads.h>
#include <linux/init.h>
#include <linux/linkage.h>
#include <asm/segment.h>
#include <asm/page_types.h>
#include <asm/pgtable_types.h>
#include <asm/cache.h>
#include <asm/thread_info.h>
#include <asm/asm-offsets.h>
#include <asm/setup.h>
#include <asm/processor-flags.h>
#include <asm/percpu.h>

/* Physical address */
#define pa(X) ((X) - __PAGE_OFFSET)

/*
 * References to members of the new_cpu_data structure.
 */

#define X86		new_cpu_data+CPUINFO_x86
#define X86_VENDOR	new_cpu_data+CPUINFO_x86_vendor
#define X86_MODEL	new_cpu_data+CPUINFO_x86_model
#define X86_MASK	new_cpu_data+CPUINFO_x86_mask
#define X86_HARD_MATH	new_cpu_data+CPUINFO_hard_math
#define X86_CPUID	new_cpu_data+CPUINFO_cpuid_level
#define X86_CAPABILITY	new_cpu_data+CPUINFO_x86_capability
#define X86_VENDOR_ID	new_cpu_data+CPUINFO_x86_vendor_id

/*
 * This is how much memory in addition to the memory covered up to
 * and including _end we need mapped initially.
 * We need:
 *     (KERNEL_IMAGE_SIZE/4096) / 1024 pages (worst case, non PAE)
 *     (KERNEL_IMAGE_SIZE/4096) / 512 + 4 pages (worst case for PAE)
 *
 * Modulo rounding, each megabyte assigned here requires a kilobyte of
 * memory, which is currently unreclaimed.
 *
 * This should be a multiple of a page.
 *
 * KERNEL_IMAGE_SIZE should be greater than pa(_end)
 * and small than max_low_pfn, otherwise will waste some page table entries
 */

#if PTRS_PER_PMD > 1
#define PAGE_TABLE_SIZE(pages) (((pages) / PTRS_PER_PMD) + PTRS_PER_PGD)
#else
#define PAGE_TABLE_SIZE(pages) ((pages) / PTRS_PER_PGD)
#endif

/* Enough space to fit pagetables for the low memory linear map */
MAPPING_BEYOND_END = \
	PAGE_TABLE_SIZE(((1<<32) - __PAGE_OFFSET) >> PAGE_SHIFT) << PAGE_SHIFT

/*
 * Worst-case size of the kernel mapping we need to make:
 * the worst-case size of the kernel itself, plus the extra we need
 * to map for the linear map.
 */
KERNEL_PAGES = (KERNEL_IMAGE_SIZE + MAPPING_BEYOND_END)>>PAGE_SHIFT

INIT_MAP_SIZE = PAGE_TABLE_SIZE(KERNEL_PAGES) * PAGE_SIZE_asm
RESERVE_BRK(pagetables, INIT_MAP_SIZE)

/*
 * 32-bit kernel entrypoint; only used by the boot CPU.  On entry,
 * %esi points to the real-mode code as a 32-bit pointer.
 * CS and DS must be 4 GB flat segments, but we don't depend on
 * any particular GDT layout, because we load our own as soon as we
 * can.
 */
.section .text.head,"ax",@progbits
ENTRY(startup_32)
	/* test KEEP_SEGMENTS flag to see if the bootloader is asking
		us to not reload segments */
	testb $(1<<6), BP_loadflags(%esi)
	jnz 2f

/*
 * Set segments to known values.
 */
	lgdt pa(boot_gdt_descr)
	movl $(__BOOT_DS),%eax
	movl %eax,%ds
	movl %eax,%es
	movl %eax,%fs
	movl %eax,%gs
2:

/*
 * Clear BSS first so that there are no surprises...
 */
	cld
	xorl %eax,%eax
	movl $pa(__bss_start),%edi
	movl $pa(__bss_stop),%ecx
	subl %edi,%ecx
	shrl $2,%ecx
	rep ; stosl
/*
 * Copy bootup parameters out of the way.
 * Note: %esi still has the pointer to the real-mode data.
 * With the kexec as boot loader, parameter segment might be loaded beyond
 * kernel image and might not even be addressable by early boot page tables.
 * (kexec on panic case). Hence copy out the parameters before initializing
 * page tables.
 */
	movl $pa(boot_params),%edi
	movl $(PARAM_SIZE/4),%ecx
	cld
	rep
	movsl
	movl pa(boot_params) + NEW_CL_POINTER,%esi
	andl %esi,%esi
	jz 1f			# No comand line
	movl $pa(boot_command_line),%edi
	movl $(COMMAND_LINE_SIZE/4),%ecx
	rep
	movsl
1:

#ifdef CONFIG_PARAVIRT
	/* This is can only trip for a broken bootloader... */
	cmpw $0x207, pa(boot_params + BP_version)
	jb default_entry

	/* Paravirt-compatible boot parameters.  Look to see what architecture
		we're booting under. */
	movl pa(boot_params + BP_hardware_subarch), %eax
	cmpl $num_subarch_entries, %eax
	jae bad_subarch

	movl pa(subarch_entries)(,%eax,4), %eax
	subl $__PAGE_OFFSET, %eax
	jmp *%eax

bad_subarch:
WEAK(lguest_entry)
WEAK(xen_entry)
	/* Unknown implementation; there's really
	   nothing we can do at this point. */
	ud2a

	__INITDATA

subarch_entries:
	.long default_entry		/* normal x86/PC */
	.long lguest_entry		/* lguest hypervisor */
	.long xen_entry			/* Xen hypervisor */
num_subarch_entries = (. - subarch_entries) / 4
.previous
#endif /* CONFIG_PARAVIRT */

/*
 * Initialize page tables.  This creates a PDE and a set of page
 * tables, which are located immediately beyond __brk_base.  The variable
 * _brk_end is set up to point to the first "safe" location.
 * Mappings are created both at virtual address 0 (identity mapping)
 * and PAGE_OFFSET for up to _end.
 *
 * Note that the stack is not yet set up!
 */
default_entry:
#ifdef CONFIG_X86_PAE

	/*
	 * In PAE mode swapper_pg_dir is statically defined to contain enough
	 * entries to cover the VMSPLIT option (that is the top 1, 2 or 3
	 * entries). The identity mapping is handled by pointing two PGD
	 * entries to the first kernel PMD.
	 *
	 * Note the upper half of each PMD or PTE are always zero at
	 * this stage.
	 */

#define KPMDS (((-__PAGE_OFFSET) >> 30) & 3) /* Number of kernel PMDs */

	xorl %ebx,%ebx				/* %ebx is kept at zero */

	movl $pa(__brk_base), %edi
	movl $pa(swapper_pg_pmd), %edx
	movl $PTE_IDENT_ATTR, %eax
10:
	leal PDE_IDENT_ATTR(%edi),%ecx		/* Create PMD entry */
	movl %ecx,(%edx)			/* Store PMD entry */
						/* Upper half already zero */
	addl $8,%edx
	movl $512,%ecx
11:
	stosl
	xchgl %eax,%ebx
	stosl
	xchgl %eax,%ebx
	addl $0x1000,%eax
	loop 11b

	/*
	 * End condition: we must map up to the end + MAPPING_BEYOND_END.
	 */
	movl $pa(_end) + MAPPING_BEYOND_END + PTE_IDENT_ATTR, %ebp
	cmpl %ebp,%eax
	jb 10b
1:
	addl $__PAGE_OFFSET, %edi
	movl %edi, pa(_brk_end)
	shrl $12, %eax
	movl %eax, pa(max_pfn_mapped)

	/* Do early initialization of the fixmap area */
	movl $pa(swapper_pg_fixmap)+PDE_IDENT_ATTR,%eax
	movl %eax,pa(swapper_pg_pmd+0x1000*KPMDS-8)
#else	/* Not PAE */

page_pde_offset = (__PAGE_OFFSET >> 20);

	movl $pa(__brk_base), %edi
	movl $pa(swapper_pg_dir), %edx
	movl $PTE_IDENT_ATTR, %eax
10:
	leal PDE_IDENT_ATTR(%edi),%ecx		/* Create PDE entry */
	movl %ecx,(%edx)			/* Store identity PDE entry */
	movl %ecx,page_pde_offset(%edx)		/* Store kernel PDE entry */
	addl $4,%edx
	movl $1024, %ecx
11:
	stosl
	addl $0x1000,%eax
	loop 11b
	/*
	 * End condition: we must map up to the end + MAPPING_BEYOND_END.
	 */
	movl $pa(_end) + MAPPING_BEYOND_END + PTE_IDENT_ATTR, %ebp
	cmpl %ebp,%eax
	jb 10b
	addl $__PAGE_OFFSET, %edi
	movl %edi, pa(_brk_end)
	shrl $12, %eax
	movl %eax, pa(max_pfn_mapped)

	/* Do early initialization of the fixmap area */
	movl $pa(swapper_pg_fixmap)+PDE_IDENT_ATTR,%eax
	movl %eax,pa(swapper_pg_dir+0xffc)
#endif
	jmp 3f
/*
 * Non-boot CPU entry point; entered from trampoline.S
 * We can't lgdt here, because lgdt itself uses a data segment, but
 * we know the trampoline has already loaded the boot_gdt for us.
 *
 * If cpu hotplug is not supported then this code can go in init section
 * which will be freed later
 */

__CPUINIT

#ifdef CONFIG_SMP
ENTRY(startup_32_smp)
	cld
	movl $(__BOOT_DS),%eax
	movl %eax,%ds
	movl %eax,%es
	movl %eax,%fs
	movl %eax,%gs
#endif /* CONFIG_SMP */
3:

/*
 *	New page tables may be in 4Mbyte page mode and may
 *	be using the global pages. 
 *
 *	NOTE! If we are on a 486 we may have no cr4 at all!
 *	So we do not try to touch it unless we really have
 *	some bits in it to set.  This won't work if the BSP
 *	implements cr4 but this AP does not -- very unlikely
 *	but be warned!  The same applies to the pse feature
 *	if not equally supported. --macro
 *
 *	NOTE! We have to correct for the fact that we're
 *	not yet offset PAGE_OFFSET..
 */
#define cr4_bits pa(mmu_cr4_features)
	movl cr4_bits,%edx
	andl %edx,%edx
	jz 6f
	movl %cr4,%eax		# Turn on paging options (PSE,PAE,..)
	orl %edx,%eax
	movl %eax,%cr4

	btl $5, %eax		# check if PAE is enabled
	jnc 6f

	/* Check if extended functions are implemented */
	movl $0x80000000, %eax
	cpuid
	cmpl $0x80000000, %eax
	jbe 6f
	mov $0x80000001, %eax
	cpuid
	/* Execute Disable bit supported? */
	btl $20, %edx
	jnc 6f

	/* Setup EFER (Extended Feature Enable Register) */
	movl $0xc0000080, %ecx
	rdmsr

	btsl $11, %eax
	/* Make changes effective */
	wrmsr

6:

/*
 * Enable paging
 */
	movl $pa(swapper_pg_dir),%eax
	movl %eax,%cr3		/* set the page table pointer.. */
	movl %cr0,%eax
	orl  $X86_CR0_PG,%eax
	movl %eax,%cr0		/* ..and set paging (PG) bit */
	ljmp $__BOOT_CS,$1f	/* Clear prefetch and normalize %eip */
1:
	/* Set up the stack pointer */
	lss stack_start,%esp

/*
 * Initialize eflags.  Some BIOS's leave bits like NT set.  This would
 * confuse the debugger if this code is traced.
 * XXX - best to initialize before switching to protected mode.
 */
	pushl $0
	popfl

#ifdef CONFIG_SMP
	cmpb $0, ready
	jz  1f				/* Initial CPU cleans BSS */
	jmp checkCPUtype
1:
#endif /* CONFIG_SMP */

/*
 * start system 32-bit setup. We need to re-do some of the things done
 * in 16-bit mode for the "real" operations.
 */
	call setup_idt

checkCPUtype:

	movl $-1,X86_CPUID		#  -1 for no CPUID initially

/* check if it is 486 or 386. */
/*
 * XXX - this does a lot of unnecessary setup.  Alignment checks don't
 * apply at our cpl of 0 and the stack ought to be aligned already, and
 * we don't need to preserve eflags.
 */

	movb $3,X86		# at least 386
	pushfl			# push EFLAGS
	popl %eax		# get EFLAGS
	movl %eax,%ecx		# save original EFLAGS
	xorl $0x240000,%eax	# flip AC and ID bits in EFLAGS
	pushl %eax		# copy to EFLAGS
	popfl			# set EFLAGS
	pushfl			# get new EFLAGS
	popl %eax		# put it in eax
	xorl %ecx,%eax		# change in flags
	pushl %ecx		# restore original EFLAGS
	popfl
	testl $0x40000,%eax	# check if AC bit changed
	je is386

	movb $4,X86		# at least 486
	testl $0x200000,%eax	# check if ID bit changed
	je is486

	/* get vendor info */
	xorl %eax,%eax			# call CPUID with 0 -> return vendor ID
	cpuid
	movl %eax,X86_CPUID		# save CPUID level
	movl %ebx,X86_VENDOR_ID		# lo 4 chars
	movl %edx,X86_VENDOR_ID+4	# next 4 chars
	movl %ecx,X86_VENDOR_ID+8	# last 4 chars

	orl %eax,%eax			# do we have processor info as well?
	je is486

	movl $1,%eax		# Use the CPUID instruction to get CPU type
	cpuid
	movb %al,%cl		# save reg for future use
	andb $0x0f,%ah		# mask processor family
	movb %ah,X86
	andb $0xf0,%al		# mask model
	shrb $4,%al
	movb %al,X86_MODEL
	andb $0x0f,%cl		# mask mask revision
	movb %cl,X86_MASK
	movl %edx,X86_CAPABILITY

is486:	movl $0x50022,%ecx	# set AM, WP, NE and MP
	jmp 2f

is386:	movl $2,%ecx		# set MP
2:	movl %cr0,%eax
	andl $0x80000011,%eax	# Save PG,PE,ET
	orl %ecx,%eax
	movl %eax,%cr0

	call check_x87
	lgdt early_gdt_descr
	lidt idt_descr
	ljmp $(__KERNEL_CS),$1f
1:	movl $(__KERNEL_DS),%eax	# reload all the segment registers
	movl %eax,%ss			# after changing gdt.

	movl $(__USER_DS),%eax		# DS/ES contains default USER segment
	movl %eax,%ds
	movl %eax,%es

	movl $(__KERNEL_PERCPU), %eax
	movl %eax,%fs			# set this cpu's percpu

#ifdef CONFIG_CC_STACKPROTECTOR
	/*
	 * The linker can't handle this by relocation.  Manually set
	 * base address in stack canary segment descriptor.
	 */
	cmpb $0,ready
	jne 1f
	movl $per_cpu__gdt_page,%eax
	movl $per_cpu__stack_canary,%ecx
	subl $20, %ecx
	movw %cx, 8 * GDT_ENTRY_STACK_CANARY + 2(%eax)
	shrl $16, %ecx
	movb %cl, 8 * GDT_ENTRY_STACK_CANARY + 4(%eax)
	movb %ch, 8 * GDT_ENTRY_STACK_CANARY + 7(%eax)
1:
#endif
	movl $(__KERNEL_STACK_CANARY),%eax
	movl %eax,%gs

	xorl %eax,%eax			# Clear LDT
	lldt %ax

	cld			# gcc2 wants the direction flag cleared at all times
	pushl $0		# fake return address for unwinder
#ifdef CONFIG_SMP
	movb ready, %cl
	movb $1, ready
	cmpb $0,%cl		# the first CPU calls start_kernel
	je   1f
	movl (stack_start), %esp
1:
#endif /* CONFIG_SMP */
	jmp *(initial_code)

/*
 * We depend on ET to be correct. This checks for 287/387.
 */
check_x87:
	movb $0,X86_HARD_MATH
	clts
	fninit
	fstsw %ax
	cmpb $0,%al
	je 1f
	movl %cr0,%eax		/* no coprocessor: have to set bits */
	xorl $4,%eax		/* set EM */
	movl %eax,%cr0
	ret
	ALIGN
1:	movb $1,X86_HARD_MATH
	.byte 0xDB,0xE4		/* fsetpm for 287, ignored by 387 */
	ret

/*
 *  setup_idt
 *
 *  sets up a idt with 256 entries pointing to
 *  ignore_int, interrupt gates. It doesn't actually load
 *  idt - that can be done only after paging has been enabled
 *  and the kernel moved to PAGE_OFFSET. Interrupts
 *  are enabled elsewhere, when we can be relatively
 *  sure everything is ok.
 *
 *  Warning: %esi is live across this function.
 */
setup_idt:
	lea ignore_int,%edx
	movl $(__KERNEL_CS << 16),%eax
	movw %dx,%ax		/* selector = 0x0010 = cs */
	movw $0x8E00,%dx	/* interrupt gate - dpl=0, present */

	lea idt_table,%edi
	mov $256,%ecx
rp_sidt:
	movl %eax,(%edi)
	movl %edx,4(%edi)
	addl $8,%edi
	dec %ecx
	jne rp_sidt

.macro	set_early_handler handler,trapno
	lea \handler,%edx
	movl $(__KERNEL_CS << 16),%eax
	movw %dx,%ax
	movw $0x8E00,%dx	/* interrupt gate - dpl=0, present */
	lea idt_table,%edi
	movl %eax,8*\trapno(%edi)
	movl %edx,8*\trapno+4(%edi)
.endm

	set_early_handler handler=early_divide_err,trapno=0
	set_early_handler handler=early_illegal_opcode,trapno=6
	set_early_handler handler=early_protection_fault,trapno=13
	set_early_handler handler=early_page_fault,trapno=14

	ret

early_divide_err:
	xor %edx,%edx
	pushl $0	/* fake errcode */
	jmp early_fault

early_illegal_opcode:
	movl $6,%edx
	pushl $0	/* fake errcode */
	jmp early_fault

early_protection_fault:
	movl $13,%edx
	jmp early_fault

early_page_fault:
	movl $14,%edx
	jmp early_fault

early_fault:
	cld
#ifdef CONFIG_PRINTK
	pusha
	movl $(__KERNEL_DS),%eax
	movl %eax,%ds
	movl %eax,%es
	cmpl $2,early_recursion_flag
	je hlt_loop
	incl early_recursion_flag
	movl %cr2,%eax
	pushl %eax
	pushl %edx		/* trapno */
	pushl $fault_msg
	call printk
#endif
	call dump_stack
hlt_loop:
	hlt
	jmp hlt_loop

/* This is the default interrupt "handler" :-) */
	ALIGN
ignore_int:
	cld
#ifdef CONFIG_PRINTK
	pushl %eax
	pushl %ecx
	pushl %edx
	pushl %es
	pushl %ds
	movl $(__KERNEL_DS),%eax
	movl %eax,%ds
	movl %eax,%es
	cmpl $2,early_recursion_flag
	je hlt_loop
	incl early_recursion_flag
	pushl 16(%esp)
	pushl 24(%esp)
	pushl 32(%esp)
	pushl 40(%esp)
	pushl $int_msg
	call printk

	call dump_stack

	addl $(5*4),%esp
	popl %ds
	popl %es
	popl %edx
	popl %ecx
	popl %eax
#endif
	iret

	__REFDATA
.align 4
ENTRY(initial_code)
	.long i386_start_kernel

/*
 * BSS section
 */
.section ".bss.page_aligned","wa"
	.align PAGE_SIZE_asm
#ifdef CONFIG_X86_PAE
swapper_pg_pmd:
	.fill 1024*KPMDS,4,0
#else
ENTRY(swapper_pg_dir)
	.fill 1024,4,0
#endif
swapper_pg_fixmap:
	.fill 1024,4,0
ENTRY(empty_zero_page)
	.fill 4096,1,0

/*
 * This starts the data section.
 */
#ifdef CONFIG_X86_PAE
.section ".data.page_aligned","wa"
	/* Page-aligned for the benefit of paravirt? */
	.align PAGE_SIZE_asm
ENTRY(swapper_pg_dir)
	.long	pa(swapper_pg_pmd+PGD_IDENT_ATTR),0	/* low identity map */
# if KPMDS == 3
	.long	pa(swapper_pg_pmd+PGD_IDENT_ATTR),0
	.long	pa(swapper_pg_pmd+PGD_IDENT_ATTR+0x1000),0
	.long	pa(swapper_pg_pmd+PGD_IDENT_ATTR+0x2000),0
# elif KPMDS == 2
	.long	0,0
	.long	pa(swapper_pg_pmd+PGD_IDENT_ATTR),0
	.long	pa(swapper_pg_pmd+PGD_IDENT_ATTR+0x1000),0
# elif KPMDS == 1
	.long	0,0
	.long	0,0
	.long	pa(swapper_pg_pmd+PGD_IDENT_ATTR),0
# else
#  error "Kernel PMDs should be 1, 2 or 3"
# endif
	.align PAGE_SIZE_asm		/* needs to be page-sized too */
#endif

.data
ENTRY(stack_start)
	.long init_thread_union+THREAD_SIZE
	.long __BOOT_DS

ready:	.byte 0

early_recursion_flag:
	.long 0

int_msg:
	.asciz "Unknown interrupt or fault at: %p %p %p\n"

fault_msg:
/* fault info: */
	.ascii "BUG: Int %d: CR2 %p\n"
/* pusha regs: */
	.ascii "     EDI %p  ESI %p  EBP %p  ESP %p\n"
	.ascii "     EBX %p  EDX %p  ECX %p  EAX %p\n"
/* fault frame: */
	.ascii "     err %p  EIP %p   CS %p  flg %p\n"
	.ascii "Stack: %p %p %p %p %p %p %p %p\n"
	.ascii "       %p %p %p %p %p %p %p %p\n"
	.asciz "       %p %p %p %p %p %p %p %p\n"

#include "../../x86/xen/xen-head.S"

/*
 * The IDT and GDT 'descriptors' are a strange 48-bit object
 * only used by the lidt and lgdt instructions. They are not
 * like usual segment descriptors - they consist of a 16-bit
 * segment size, and 32-bit linear address value:
 */

.globl boot_gdt_descr
.globl idt_descr

	ALIGN
# early boot GDT descriptor (must use 1:1 address mapping)
	.word 0				# 32 bit align gdt_desc.address
boot_gdt_descr:
	.word __BOOT_DS+7
	.long boot_gdt - __PAGE_OFFSET

	.word 0				# 32-bit align idt_desc.address
idt_descr:
	.word IDT_ENTRIES*8-1		# idt contains 256 entries
	.long idt_table

# boot GDT descriptor (later on used by CPU#0):
	.word 0				# 32 bit align gdt_desc.address
ENTRY(early_gdt_descr)
	.word GDT_ENTRIES*8-1
	.long per_cpu__gdt_page		/* Overwritten for secondary CPUs */

/*
 * The boot_gdt must mirror the equivalent in setup.S and is
 * used only for booting.
 */
	.align L1_CACHE_BYTES
ENTRY(boot_gdt)
	.fill GDT_ENTRY_BOOT_CS,8,0
	.quad 0x00cf9a000000ffff	/* kernel 4GB code at 0x00000000 */
	.quad 0x00cf92000000ffff	/* kernel 4GB data at 0x00000000 */
