/*
 * Linker script for vsyscall DSO.  The vsyscall page is an ELF shared
 * object prelinked to its virtual address. This script controls its layout.
 */

/* This must match <asm/fixmap.h>.  */
VSYSCALL_BASE = 0xffffe000;

SECTIONS
{
  . = VSYSCALL_BASE + SIZEOF_HEADERS;

  .hash           : { *(.hash) }		:text
  .dynsym         : { *(.dynsym) }
  .dynstr         : { *(.dynstr) }
  .gnu.version    : { *(.gnu.version) }
  .gnu.version_d  : { *(.gnu.version_d) }
  .gnu.version_r  : { *(.gnu.version_r) }

  /* This linker script is used both with -r and with -shared.
     For the layouts to match, we need to skip more than enough
     space for the dynamic symbol table et al.  If this amount
     is insufficient, ld -shared will barf.  Just increase it here.  */
  . = VSYSCALL_BASE + 0x400;
  
  .text.vsyscall   : { *(.text.vsyscall) } 	:text =0x90909090

  /* This is an 32bit object and we cannot easily get the offsets
     into the 64bit kernel. Just hardcode them here. This assumes
     that all the stubs don't need more than 0x100 bytes. */
  . = VSYSCALL_BASE + 0x500;

  .text.sigreturn  : { *(.text.sigreturn) }	:text =0x90909090

  . = VSYSCALL_BASE + 0x600;

  .text.rtsigreturn : { *(.text.rtsigreturn) }   :text =0x90909090
	
  .eh_frame_hdr   : { *(.eh_frame_hdr) }	:text :eh_frame_hdr
  .eh_frame       : { KEEP (*(.eh_frame)) }	:text
  .dynamic        : { *(.dynamic) }		:text :dynamic
  .useless        : {
  	*(.got.plt) *(.got)
	*(.data .data.* .gnu.linkonce.d.*)
	*(.dynbss)
	*(.bss .bss.* .gnu.linkonce.b.*)
  }						:text
}

/*
 * We must supply the ELF program headers explicitly to get just one
 * PT_LOAD segment, and set the flags explicitly to make segments read-only.
 */
PHDRS
{
  text PT_LOAD FILEHDR PHDRS FLAGS(5); /* PF_R|PF_X */
  dynamic PT_DYNAMIC FLAGS(4); /* PF_R */
  eh_frame_hdr 0x6474e550; /* PT_GNU_EH_FRAME, but ld doesn't match the name */
}

/*
 * This controls what symbols we export from the DSO.
 */
VERSION
{
  LINUX_2.5 {
    global:
    	__kernel_vsyscall;
    	__kernel_sigreturn;
    	__kernel_rt_sigreturn;

    local: *;
  };
}

/* The ELF entry point can be used to set the AT_SYSINFO value.  */
ENTRY(__kernel_vsyscall);
