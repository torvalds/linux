/* SPDX-License-Identifier: GPL-2.0 */
#include <asm/vdso.h>
#include <asm/vdso/vsyscall.h>
#include <vdso/datapage.h>

/*
 * Linker script for vDSO.  This is an ELF shared object prelinked to
 * its virtual address, and with only one read-only segment.
 * This script controls its layout.
 */

SECTIONS
{
	/*
	 * User/kernel shared data is before the vDSO.  This may be a little
	 * uglier than putting it after the vDSO, but it avoids issues with
	 * non-allocatable things that dangle past the end of the PT_LOAD
	 * segment.
	 */

	VDSO_VVAR_SYMS

	vclock_pages = VDSO_VCLOCK_PAGES_START(vdso_u_data);
	pvclock_page = vclock_pages + VDSO_PAGE_PVCLOCK_OFFSET * PAGE_SIZE;
	hvclock_page = vclock_pages + VDSO_PAGE_HVCLOCK_OFFSET * PAGE_SIZE;

	. = SIZEOF_HEADERS;

	.hash		: { *(.hash) }			:text
	.gnu.hash	: { *(.gnu.hash) }
	.dynsym		: { *(.dynsym) }
	.dynstr		: { *(.dynstr) }
	.gnu.version	: { *(.gnu.version) }
	.gnu.version_d	: { *(.gnu.version_d) }
	.gnu.version_r	: { *(.gnu.version_r) }

	.dynamic	: { *(.dynamic) }		:text	:dynamic

	.rodata		: {
		*(.rodata*)
		*(.data*)
		*(.sdata*)
		*(.got.plt) *(.got)
		*(.gnu.linkonce.d.*)
		*(.bss*)
		*(.dynbss*)
		*(.gnu.linkonce.b.*)
	}						:text

	.note.gnu.property : {
		*(.note.gnu.property)
	}					:text :note :gnu_property
	.note		: {
		*(.note*)
	}					:text :note

	.eh_frame_hdr	: { *(.eh_frame_hdr) }	:text :eh_frame_hdr
	.eh_frame	: {
		KEEP (*(.eh_frame))
		*(.eh_frame.*)
	}					:text

	/*
	 * Text is well-separated from actual data: there's plenty of
	 * stuff that isn't used at runtime in between.
	 */

	.text		: {
		*(.text*)
	}						:text	=0x90909090,



	.altinstructions	: { *(.altinstructions) }	:text
	.altinstr_replacement	: { *(.altinstr_replacement) }	:text

	__ex_table		: { *(__ex_table) }		:text

	/DISCARD/ : {
		*(.discard)
		*(.discard.*)
		*(__bug_table)
	}
}

/*
 * Very old versions of ld do not recognize this name token; use the constant.
 */
#define PT_GNU_EH_FRAME	0x6474e550
#define PT_GNU_STACK	0x6474e551
#define PT_GNU_PROPERTY	0x6474e553

/*
 * We must supply the ELF program headers explicitly to get just one
 * PT_LOAD segment, and set the flags explicitly to make segments read-only.
*/
#define PF_R	FLAGS(4)
#define PF_RW	FLAGS(6)
#define PF_RX	FLAGS(5)

PHDRS
{
	text		PT_LOAD		PF_RX FILEHDR PHDRS;
	dynamic		PT_DYNAMIC	PF_R;
	note		PT_NOTE		PF_R;
	eh_frame_hdr	PT_GNU_EH_FRAME PF_R;
	gnu_stack	PT_GNU_STACK	PF_RW;
	gnu_property	PT_GNU_PROPERTY	PF_R;
}
