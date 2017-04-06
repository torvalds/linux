/* Xen-specific pieces of head.S, intended to be included in the right
	place in head.S */

#ifdef CONFIG_XEN

#include <linux/elfnote.h>
#include <linux/init.h>

#include <asm/boot.h>
#include <asm/asm.h>
#include <asm/page_types.h>

#include <xen/interface/elfnote.h>
#include <xen/interface/features.h>
#include <xen/interface/xen.h>
#include <xen/interface/xen-mca.h>
#include <asm/xen/interface.h>

	__INIT
ENTRY(startup_xen)
	cld

	/* Clear .bss */
	xor %eax,%eax
	mov $__bss_start, %_ASM_DI
	mov $__bss_stop, %_ASM_CX
	sub %_ASM_DI, %_ASM_CX
	shr $__ASM_SEL(2, 3), %_ASM_CX
	rep __ASM_SIZE(stos)

	mov %_ASM_SI, xen_start_info
	mov $init_thread_union+THREAD_SIZE, %_ASM_SP

	jmp xen_start_kernel

	__FINIT

.pushsection .text
	.balign PAGE_SIZE
ENTRY(hypercall_page)
	.skip PAGE_SIZE

#define HYPERCALL(n) \
	.equ xen_hypercall_##n, hypercall_page + __HYPERVISOR_##n * 32; \
	.type xen_hypercall_##n, @function; .size xen_hypercall_##n, 32
#include <asm/xen-hypercalls.h>
#undef HYPERCALL

.popsection

	ELFNOTE(Xen, XEN_ELFNOTE_GUEST_OS,       .asciz "linux")
	ELFNOTE(Xen, XEN_ELFNOTE_GUEST_VERSION,  .asciz "2.6")
	ELFNOTE(Xen, XEN_ELFNOTE_XEN_VERSION,    .asciz "xen-3.0")
#ifdef CONFIG_X86_32
	ELFNOTE(Xen, XEN_ELFNOTE_VIRT_BASE,      _ASM_PTR __PAGE_OFFSET)
#else
	ELFNOTE(Xen, XEN_ELFNOTE_VIRT_BASE,      _ASM_PTR __START_KERNEL_map)
	/* Map the p2m table to a 512GB-aligned user address. */
	ELFNOTE(Xen, XEN_ELFNOTE_INIT_P2M,       .quad PGDIR_SIZE)
#endif
	ELFNOTE(Xen, XEN_ELFNOTE_ENTRY,          _ASM_PTR startup_xen)
	ELFNOTE(Xen, XEN_ELFNOTE_HYPERCALL_PAGE, _ASM_PTR hypercall_page)
	ELFNOTE(Xen, XEN_ELFNOTE_FEATURES,
		.ascii "!writable_page_tables|pae_pgdir_above_4gb")
	ELFNOTE(Xen, XEN_ELFNOTE_SUPPORTED_FEATURES,
		.long (1 << XENFEAT_writable_page_tables) | (1 << XENFEAT_dom0))
	ELFNOTE(Xen, XEN_ELFNOTE_PAE_MODE,       .asciz "yes")
	ELFNOTE(Xen, XEN_ELFNOTE_LOADER,         .asciz "generic")
	ELFNOTE(Xen, XEN_ELFNOTE_L1_MFN_VALID,
		.quad _PAGE_PRESENT; .quad _PAGE_PRESENT)
	ELFNOTE(Xen, XEN_ELFNOTE_SUSPEND_CANCEL, .long 1)
	ELFNOTE(Xen, XEN_ELFNOTE_MOD_START_PFN,  .long 1)
	ELFNOTE(Xen, XEN_ELFNOTE_HV_START_LOW,   _ASM_PTR __HYPERVISOR_VIRT_START)
	ELFNOTE(Xen, XEN_ELFNOTE_PADDR_OFFSET,   _ASM_PTR 0)

#endif /*CONFIG_XEN */
