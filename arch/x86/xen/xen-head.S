/* Xen-specific pieces of head.S, intended to be included in the right
	place in head.S */

#ifdef CONFIG_XEN

#include <linux/elfnote.h>
#include <linux/init.h>
#include <asm/boot.h>
#include <xen/interface/elfnote.h>
#include <asm/xen/interface.h>

	__INIT
ENTRY(startup_xen)
	movl %esi,xen_start_info
	cld
	movl $(init_thread_union+THREAD_SIZE),%esp
	jmp xen_start_kernel

	__FINIT

.pushsection .text
	.align PAGE_SIZE_asm
ENTRY(hypercall_page)
	.skip 0x1000
.popsection

	ELFNOTE(Xen, XEN_ELFNOTE_GUEST_OS,       .asciz "linux")
	ELFNOTE(Xen, XEN_ELFNOTE_GUEST_VERSION,  .asciz "2.6")
	ELFNOTE(Xen, XEN_ELFNOTE_XEN_VERSION,    .asciz "xen-3.0")
	ELFNOTE(Xen, XEN_ELFNOTE_VIRT_BASE,      .long  __PAGE_OFFSET)
	ELFNOTE(Xen, XEN_ELFNOTE_ENTRY,          .long  startup_xen)
	ELFNOTE(Xen, XEN_ELFNOTE_HYPERCALL_PAGE, .long  hypercall_page)
	ELFNOTE(Xen, XEN_ELFNOTE_FEATURES,       .asciz "!writable_page_tables|pae_pgdir_above_4gb")
	ELFNOTE(Xen, XEN_ELFNOTE_PAE_MODE,       .asciz "yes")
	ELFNOTE(Xen, XEN_ELFNOTE_LOADER,         .asciz "generic")
	ELFNOTE(Xen, XEN_ELFNOTE_L1_MFN_VALID,
		.quad _PAGE_PRESENT; .quad _PAGE_PRESENT)
	ELFNOTE(Xen, XEN_ELFNOTE_SUSPEND_CANCEL, .long 1)
	ELFNOTE(Xen, XEN_ELFNOTE_HV_START_LOW,   .long __HYPERVISOR_VIRT_START)

#endif /*CONFIG_XEN */
