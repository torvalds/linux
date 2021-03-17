/*
 * Linker script for vDSO.  This is an ELF shared object prelinked to
 * its virtual address, and with only one read-only segment.
 * This script controls its layout.
 */

#if defined(BUILD_VDSO64)
# define SHDR_SIZE 64
#elif defined(BUILD_VDSO32)
# define SHDR_SIZE 40
#else
# error unknown VDSO target
#endif

#define NUM_FAKE_SHDRS 7

SECTIONS
{
	/*
	 * User/kernel shared data is before the vDSO.  This may be a little
	 * uglier than putting it after the vDSO, but it avoids issues with
	 * non-allocatable things that dangle past the end of the PT_LOAD
	 * segment. Page size is 8192 for both 64-bit and 32-bit vdso binaries
	 */

	vvar_start = . -8192;
	vvar_data = vvar_start;

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

		/*
		 * Ideally this would live in a C file: kept in here for
		 * compatibility with x86-64.
		 */
		VDSO_FAKE_SECTION_TABLE_START = .;
		. = . + NUM_FAKE_SHDRS * SHDR_SIZE;
		VDSO_FAKE_SECTION_TABLE_END = .;
	}						:text

	.fake_shstrtab	: { *(.fake_shstrtab) }		:text


	.note		: { *(.note.*) }		:text	:note

	.eh_frame_hdr	: { *(.eh_frame_hdr) }		:text	:eh_frame_hdr
	.eh_frame	: { KEEP (*(.eh_frame)) }	:text


	/*
	 * Text is well-separated from actual data: there's plenty of
	 * stuff that isn't used at runtime in between.
	 */

	.text		: { *(.text*) }			:text	=0x90909090,

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
}
