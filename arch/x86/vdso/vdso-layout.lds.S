/*
 * Linker script for vDSO.  This is an ELF shared object prelinked to
 * its virtual address, and with only one read-only segment.
 * This script controls its layout.
 */

SECTIONS
{
#ifdef BUILD_VDSO32
#include <asm/vdso32.h>

	.hpet_sect : {
		hpet_page = . - VDSO_OFFSET(VDSO_HPET_PAGE);
	} :text :hpet_sect

	.vvar_sect : {
		vvar = . - VDSO_OFFSET(VDSO_VVAR_PAGE);

	/* Place all vvars at the offsets in asm/vvar.h. */
#define EMIT_VVAR(name, offset) vvar_ ## name = vvar + offset;
#define __VVAR_KERNEL_LDS
#include <asm/vvar.h>
#undef __VVAR_KERNEL_LDS
#undef EMIT_VVAR
	} :text :vvar_sect
#endif
	. = SIZEOF_HEADERS;

	.hash		: { *(.hash) }			:text
	.gnu.hash	: { *(.gnu.hash) }
	.dynsym		: { *(.dynsym) }
	.dynstr		: { *(.dynstr) }
	.gnu.version	: { *(.gnu.version) }
	.gnu.version_d	: { *(.gnu.version_d) }
	.gnu.version_r	: { *(.gnu.version_r) }

	.note		: { *(.note.*) }		:text	:note

	.eh_frame_hdr	: { *(.eh_frame_hdr) }		:text	:eh_frame_hdr
	.eh_frame	: { KEEP (*(.eh_frame)) }	:text

	.dynamic	: { *(.dynamic) }		:text	:dynamic

	.rodata		: { *(.rodata*) }		:text
	.data		: {
	      *(.data*)
	      *(.sdata*)
	      *(.got.plt) *(.got)
	      *(.gnu.linkonce.d.*)
	      *(.bss*)
	      *(.dynbss*)
	      *(.gnu.linkonce.b.*)
	}

	.altinstructions	: { *(.altinstructions) }
	.altinstr_replacement	: { *(.altinstr_replacement) }

	/*
	 * Align the actual code well away from the non-instruction data.
	 * This is the best thing for the I-cache.
	 */
	. = ALIGN(0x100);

	.text		: { *(.text*) }			:text	=0x90909090
}

/*
 * Very old versions of ld do not recognize this name token; use the constant.
 */
#define PT_GNU_EH_FRAME	0x6474e550

/*
 * We must supply the ELF program headers explicitly to get just one
 * PT_LOAD segment, and set the flags explicitly to make segments read-only.
 */
PHDRS
{
	text		PT_LOAD		FLAGS(5) FILEHDR PHDRS; /* PF_R|PF_X */
	dynamic		PT_DYNAMIC	FLAGS(4);		/* PF_R */
	note		PT_NOTE		FLAGS(4);		/* PF_R */
	eh_frame_hdr	PT_GNU_EH_FRAME;
#ifdef BUILD_VDSO32
	vvar_sect	PT_NULL		FLAGS(4);		/* PF_R */
	hpet_sect	PT_NULL		FLAGS(4);		/* PF_R */
#endif
}
