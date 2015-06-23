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
#include <asm/msr-index.h>
#include <asm/cpufeature.h>
#include <asm/percpu.h>
#include <asm/nops.h>
#include <asm/bootparam.h>

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

/*
 * Number of possible pages in the lowmem region.
 *
 * We shift 2 by 31 instead of 1 by 32 to the left in order to avoid a
 * gas warning about overflowing shift count when gas has been compiled
 * with only a host target support using a 32-bit type for internal
 * representation.
 */
LOWMEM_PAGES = (((2<<31) - __PAGE_OFFSET) >> PAGE_SHIFT)

/* Enough space to fit pagetables for the low memory linear map */
MAPPING_BEYOND_END = PAGE_TABLE_SIZE(LOWMEM_PAGES) << PAGE_SHIFT

/*
 * Worst-case size of the kernel mapping we need to make:
 * a relocatable kernel can live anywhere in lowmem, so we need to be able
 * to map all of lowmem.
 */
KERNEL_PAGES = LOWMEM_PAGES

INIT_MAP_SIZE = PAGE_TABLE_SIZE(KERNEL_PAGES) * PAGE_SIZE
RESERVE_BRK(pagetables, INIT_MAP_SIZE)

/*
 * 32-bit kernel entrypoint; only used by the boot CPU.  On entry,
 * %esi points to the real-mode code as a 32-bit pointer.
 * CS and DS must be 4 GB flat segments, but we don't depend on
 * any particular GDT layout, because we load our own as soon as we
 * can.
 */
__HEAD
ENTRY(startup_32)
	movl pa(stack_start),%ecx
	
	/* test KEEP_SEGMENTS flag to see if the bootloader is asking
		us to not reload segments */
	testb $KEEP_SEGMENTS, BP_loadflags(%esi)
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
	movl %eax,%ss
2:
	leal -__PAGE_OFFSET(%ecx),%esp

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
	jz 1f			# No command line
	movl $pa(boot_command_line),%edi
	movl $(COMMAND_LINE_SIZE/4),%ecx
	rep
	movsl
1:

#ifdef CONFIG_OLPC
	/* save OFW's pgdir table for later use when calling into OFW */
	movl %cr3, %eax
	movl %eax, pa(olpc_ofw_pgd)
#endif

#ifdef CONFIG_MICROCODE_EARLY
	/* Early load ucode on BSP. */
	call load_ucode_bsp
#endif

/*
 * Initialize page tables.  This creates a PDE and a set of page
 * tables, which are located immediately beyond __brk_base.  The variable
 * _brk_end is set up to point to the first "safe" location.
 * Mappings are created both at virtual address 0 (identity mapping)
 * and PAGE_OFFSET for up to _end.
 */
#ifdef CONFIG_X86_PAE

	/*
	 * In PAE mode initial_page_table is statically defined to contain
	 * enough entries to cover the VMSPLIT option (that is the top 1, 2 or 3
	 * entries). The identity mapping is handled by pointing two PGD entries
	 * to the first kernel PMD.
	 *
	 * Note the upper half of each PMD or PTE are always zero at this stage.
	 */

#define KPMDS (((-__PAGE_OFFSET) >> 30) & 3) /* Number of kernel PMDs */

	xorl %ebx,%ebx				/* %ebx is kept at zero */

	movl $pa(__brk_base), %edi
	movl $pa(initial_pg_pmd), %edx
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
	movl $pa(initial_pg_fixmap)+PDE_IDENT_ATTR,%eax
	movl %eax,pa(initial_pg_pmd+0x1000*KPMDS-8)
#else	/* Not PAE */

page_pde_offset = (__PAGE_OFFSET >> 20);

	movl $pa(__brk_base), %edi
	movl $pa(initial_page_table), %edx
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
	movl $pa(initial_pg_fixmap)+PDE_IDENT_ATTR,%eax
	movl %eax,pa(initial_page_table+0xffc)
#endif

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
	.long default_entry		/* Moorestown MID */
num_subarch_entries = (. - subarch_entries) / 4
.previous
#else
	jmp default_entry
#endif /* CONFIG_PARAVIRT */

#ifdef CONFIG_HOTPLUG_CPU
/*
 * Boot CPU0 entry point. It's called from play_dead(). Everything has been set
 * up already except stack. We just set up stack here. Then call
 * start_secondary().
 */
ENTRY(start_cpu0)
	movl stack_start, %ecx
	movl %ecx, %esp
	jmp  *(initial_code)
ENDPROC(start_cpu0)
#endif

/*
 * Non-boot CPU entry point; entered from trampoline.S
 * We can't lgdt here, because lgdt itself uses a data segment, but
 * we know the trampoline has already loaded the boot_gdt for us.
 *
 * If cpu hotplug is not supported then this code can go in init section
 * which will be freed later
 */
ENTRY(startup_32_smp)
	cld
	movl $(__BOOT_DS),%eax
	movl %eax,%ds
	movl %eax,%es
	movl %eax,%fs
	movl %eax,%gs
	movl pa(stack_start),%ecx
	movl %eax,%ss
	leal -__PAGE_OFFSET(%ecx),%esp

#ifdef CONFIG_MICROCODE_EARLY
	/* Early load ucode on AP. */
	call load_ucode_ap
#endif


default_entry:
#define CR0_STATE	(X86_CR0_PE | X86_CR0_MP | X86_CR0_ET | \
			 X86_CR0_NE | X86_CR0_WP | X86_CR0_AM | \
			 X86_CR0_PG)
	movl $(CR0_STATE & ~X86_CR0_PG),%eax
	movl %eax,%cr0

/*
 * We want to start out with EFLAGS unambiguously cleared. Some BIOSes leave
 * bits like NT set. This would confuse the debugger if this code is traced. So
 * initialize them properly now before switching to protected mode. That means
 * DF in particular (even though we have cleared it earlier after copying the
 * command line) because GCC expects it.
 */
	pushl $0
	popfl

/*
 * New page tables may be in 4Mbyte page mode and may be using the global pages.
 *
 * NOTE! If we are on a 486 we may have no cr4 at all! Specifically, cr4 exists
 * if and only if CPUID exists and has flags other than the FPU flag set.
 */
	movl $-1,pa(X86_CPUID)		# preset CPUID level
	movl $X86_EFLAGS_ID,%ecx
	pushl %ecx
	popfl				# set EFLAGS=ID
	pushfl
	popl %eax			# get EFLAGS
	testl $X86_EFLAGS_ID,%eax	# did EFLAGS.ID remained set?
	jz enable_paging		# hw disallowed setting of ID bit
					# which means no CPUID and no CR4

	xorl %eax,%eax
	cpuid
	movl %eax,pa(X86_CPUID)		# save largest std CPUID function

	movl $1,%eax
	cpuid
	andl $~1,%edx			# Ignore CPUID.FPU
	jz enable_paging		# No flags or only CPUID.FPU = no CR4

	movl pa(mmu_cr4_features),%eax
	movl %eax,%cr4

	testb $X86_CR4_PAE, %al		# check if PAE is enabled
	jz enable_paging

	/* Check if extended functions are implemented */
	movl $0x80000000, %eax
	cpuid
	/* Value must be in the range 0x80000001 to 0x8000ffff */
	subl $0x80000001, %eax
	cmpl $(0x8000ffff-0x80000001), %eax
	ja enable_paging

	/* Clear bogus XD_DISABLE bits */
	call verify_cpu

	mov $0x80000001, %eax
	cpuid
	/* Execute Disable bit supported? */
	btl $(X86_FEATURE_NX & 31), %edx
	jnc enable_paging

	/* Setup EFER (Extended Feature Enable Register) */
	movl $MSR_EFER, %ecx
	rdmsr

	btsl $_EFER_NX, %eax
	/* Make changes effective */
	wrmsr

enable_paging:

/*
 * Enable paging
 */
	movl $pa(initial_page_table), %eax
	movl %eax,%cr3		/* set the page table pointer.. */
	movl $CR0_STATE,%eax
	movl %eax,%cr0		/* ..and set paging (PG) bit */
	ljmp $__BOOT_CS,$1f	/* Clear prefetch and normalize %eip */
1:
	/* Shift the stack pointer to a virtual address */
	addl $__PAGE_OFFSET, %esp

/*
 * start system 32-bit setup. We need to re-do some of the things done
 * in 16-bit mode for the "real" operations.
 */
	movl setup_once_ref,%eax
	andl %eax,%eax
	jz 1f				# Did we do this already?
	call *%eax
1:

/*
 * Check if it is 486
 */
	movb $4,X86			# at least 486
	cmpl $-1,X86_CPUID
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

is486:
	movl $0x50022,%ecx	# set AM, WP, NE and MP
	movl %cr0,%eax
	andl $0x80000011,%eax	# Save PG,PE,ET
	orl %ecx,%eax
	movl %eax,%cr0

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

	movl $(__KERNEL_STACK_CANARY),%eax
	movl %eax,%gs

	xorl %eax,%eax			# Clear LDT
	lldt %ax

	pushl $0		# fake return address for unwinder
	jmp *(initial_code)

#include "verify_cpu.S"

/*
 *  setup_once
 *
 *  The setup work we only want to run on the BSP.
 *
 *  Warning: %esi is live across this function.
 */
__INIT
setup_once:
	/*
	 * Set up a idt with 256 interrupt gates that push zero if there
	 * is no error code and then jump to early_idt_handler_common.
	 * It doesn't actually load the idt - that needs to be done on
	 * each CPU. Interrupts are enabled elsewhere, when we can be
	 * relatively sure everything is ok.
	 */

	movl $idt_table,%edi
	movl $early_idt_handler_array,%eax
	movl $NUM_EXCEPTION_VECTORS,%ecx
1:
	movl %eax,(%edi)
	movl %eax,4(%edi)
	/* interrupt gate, dpl=0, present */
	movl $(0x8E000000 + __KERNEL_CS),2(%edi)
	addl $EARLY_IDT_HANDLER_SIZE,%eax
	addl $8,%edi
	loop 1b

	movl $256 - NUM_EXCEPTION_VECTORS,%ecx
	movl $ignore_int,%edx
	movl $(__KERNEL_CS << 16),%eax
	movw %dx,%ax		/* selector = 0x0010 = cs */
	movw $0x8E00,%dx	/* interrupt gate - dpl=0, present */
2:
	movl %eax,(%edi)
	movl %edx,4(%edi)
	addl $8,%edi
	loop 2b

#ifdef CONFIG_CC_STACKPROTECTOR
	/*
	 * Configure the stack canary. The linker can't handle this by
	 * relocation.  Manually set base address in stack canary
	 * segment descriptor.
	 */
	movl $gdt_page,%eax
	movl $stack_canary,%ecx
	movw %cx, 8 * GDT_ENTRY_STACK_CANARY + 2(%eax)
	shrl $16, %ecx
	movb %cl, 8 * GDT_ENTRY_STACK_CANARY + 4(%eax)
	movb %ch, 8 * GDT_ENTRY_STACK_CANARY + 7(%eax)
#endif

	andl $0,setup_once_ref	/* Once is enough, thanks */
	ret

ENTRY(early_idt_handler_array)
	# 36(%esp) %eflags
	# 32(%esp) %cs
	# 28(%esp) %eip
	# 24(%rsp) error code
	i = 0
	.rept NUM_EXCEPTION_VECTORS
	.ifeq (EXCEPTION_ERRCODE_MASK >> i) & 1
	pushl $0		# Dummy error code, to make stack frame uniform
	.endif
	pushl $i		# 20(%esp) Vector number
	jmp early_idt_handler_common
	i = i + 1
	.fill early_idt_handler_array + i*EARLY_IDT_HANDLER_SIZE - ., 1, 0xcc
	.endr
ENDPROC(early_idt_handler_array)
	
early_idt_handler_common:
	/*
	 * The stack is the hardware frame, an error code or zero, and the
	 * vector number.
	 */
	cld

	cmpl $2,(%esp)		# X86_TRAP_NMI
	je is_nmi		# Ignore NMI

	cmpl $2,%ss:early_recursion_flag
	je hlt_loop
	incl %ss:early_recursion_flag

	push %eax		# 16(%esp)
	push %ecx		# 12(%esp)
	push %edx		#  8(%esp)
	push %ds		#  4(%esp)
	push %es		#  0(%esp)
	movl $(__KERNEL_DS),%eax
	movl %eax,%ds
	movl %eax,%es

	cmpl $(__KERNEL_CS),32(%esp)
	jne 10f

	leal 28(%esp),%eax	# Pointer to %eip
	call early_fixup_exception
	andl %eax,%eax
	jnz ex_entry		/* found an exception entry */

10:
#ifdef CONFIG_PRINTK
	xorl %eax,%eax
	movw %ax,2(%esp)	/* clean up the segment values on some cpus */
	movw %ax,6(%esp)
	movw %ax,34(%esp)
	leal  40(%esp),%eax
	pushl %eax		/* %esp before the exception */
	pushl %ebx
	pushl %ebp
	pushl %esi
	pushl %edi
	movl %cr2,%eax
	pushl %eax
	pushl (20+6*4)(%esp)	/* trapno */
	pushl $fault_msg
	call printk
#endif
	call dump_stack
hlt_loop:
	hlt
	jmp hlt_loop

ex_entry:
	pop %es
	pop %ds
	pop %edx
	pop %ecx
	pop %eax
	decl %ss:early_recursion_flag
is_nmi:
	addl $8,%esp		/* drop vector number and error code */
	iret
ENDPROC(early_idt_handler_common)

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
ENDPROC(ignore_int)
__INITDATA
	.align 4
early_recursion_flag:
	.long 0

__REFDATA
	.align 4
ENTRY(initial_code)
	.long i386_start_kernel
ENTRY(setup_once_ref)
	.long setup_once

/*
 * BSS section
 */
__PAGE_ALIGNED_BSS
	.align PAGE_SIZE
#ifdef CONFIG_X86_PAE
initial_pg_pmd:
	.fill 1024*KPMDS,4,0
#else
ENTRY(initial_page_table)
	.fill 1024,4,0
#endif
initial_pg_fixmap:
	.fill 1024,4,0
ENTRY(empty_zero_page)
	.fill 4096,1,0
ENTRY(swapper_pg_dir)
	.fill 1024,4,0

/*
 * This starts the data section.
 */
#ifdef CONFIG_X86_PAE
__PAGE_ALIGNED_DATA
	/* Page-aligned for the benefit of paravirt? */
	.align PAGE_SIZE
ENTRY(initial_page_table)
	.long	pa(initial_pg_pmd+PGD_IDENT_ATTR),0	/* low identity map */
# if KPMDS == 3
	.long	pa(initial_pg_pmd+PGD_IDENT_ATTR),0
	.long	pa(initial_pg_pmd+PGD_IDENT_ATTR+0x1000),0
	.long	pa(initial_pg_pmd+PGD_IDENT_ATTR+0x2000),0
# elif KPMDS == 2
	.long	0,0
	.long	pa(initial_pg_pmd+PGD_IDENT_ATTR),0
	.long	pa(initial_pg_pmd+PGD_IDENT_ATTR+0x1000),0
# elif KPMDS == 1
	.long	0,0
	.long	0,0
	.long	pa(initial_pg_pmd+PGD_IDENT_ATTR),0
# else
#  error "Kernel PMDs should be 1, 2 or 3"
# endif
	.align PAGE_SIZE		/* needs to be page-sized too */
#endif

.data
.balign 4
ENTRY(stack_start)
	.long init_thread_union+THREAD_SIZE

__INITRODATA
int_msg:
	.asciz "Unknown interrupt or fault at: %p %p %p\n"

fault_msg:
/* fault info: */
	.ascii "BUG: Int %d: CR2 %p\n"
/* regs pushed in early_idt_handler: */
	.ascii "     EDI %p  ESI %p  EBP %p  EBX %p\n"
	.ascii "     ESP %p   ES %p   DS %p\n"
	.ascii "     EDX %p  ECX %p  EAX %p\n"
/* fault frame: */
	.ascii "     vec %p  err %p  EIP %p   CS %p  flg %p\n"
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

	.data
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
	.long gdt_page			/* Overwritten for secondary CPUs */

/*
 * The boot_gdt must mirror the equivalent in setup.S and is
 * used only for booting.
 */
	.align L1_CACHE_BYTES
ENTRY(boot_gdt)
	.fill GDT_ENTRY_BOOT_CS,8,0
	.quad 0x00cf9a000000ffff	/* kernel 4GB code at 0x00000000 */
	.quad 0x00cf92000000ffff	/* kernel 4GB data at 0x00000000 */
