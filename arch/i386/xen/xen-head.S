/* Xen-specific pieces of head.S, intended to be included in the right
	place in head.S */

#ifdef CONFIG_XEN

#include <linux/elfnote.h>
#include <asm/boot.h>
#include <xen/interface/elfnote.h>

ENTRY(startup_xen)
	movl %esi,xen_start_info
	cld
	movl $(init_thread_union+THREAD_SIZE),%esp
	jmp xen_start_kernel

.pushsection ".bss.page_aligned"
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
#ifdef CONFIG_X86_PAE
	ELFNOTE(Xen, XEN_ELFNOTE_PAE_MODE,       .asciz "yes")
#else
	ELFNOTE(Xen, XEN_ELFNOTE_PAE_MODE,       .asciz "no")
#endif
	ELFNOTE(Xen, XEN_ELFNOTE_LOADER,         .asciz "generic")

#endif /*CONFIG_XEN */
