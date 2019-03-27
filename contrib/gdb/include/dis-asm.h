/* Interface between the opcode library and its callers.

   Copyright 2001, 2002, 2003 Free Software Foundation, Inc.
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.
   
   Written by Cygnus Support, 1993.

   The opcode library (libopcodes.a) provides instruction decoders for
   a large variety of instruction sets, callable with an identical
   interface, for making instruction-processing programs more independent
   of the instruction set being processed.  */

#ifndef DIS_ASM_H
#define DIS_ASM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include "bfd.h"

typedef int (*fprintf_ftype) (void *, const char*, ...);

enum dis_insn_type {
  dis_noninsn,			/* Not a valid instruction */
  dis_nonbranch,		/* Not a branch instruction */
  dis_branch,			/* Unconditional branch */
  dis_condbranch,		/* Conditional branch */
  dis_jsr,			/* Jump to subroutine */
  dis_condjsr,			/* Conditional jump to subroutine */
  dis_dref,			/* Data reference instruction */
  dis_dref2			/* Two data references in instruction */
};

/* This struct is passed into the instruction decoding routine, 
   and is passed back out into each callback.  The various fields are used
   for conveying information from your main routine into your callbacks,
   for passing information into the instruction decoders (such as the
   addresses of the callback functions), or for passing information
   back from the instruction decoders to their callers.

   It must be initialized before it is first passed; this can be done
   by hand, or using one of the initialization macros below.  */

typedef struct disassemble_info {
  fprintf_ftype fprintf_func;
  void *stream;
  void *application_data;

  /* Target description.  We could replace this with a pointer to the bfd,
     but that would require one.  There currently isn't any such requirement
     so to avoid introducing one we record these explicitly.  */
  /* The bfd_flavour.  This can be bfd_target_unknown_flavour.  */
  enum bfd_flavour flavour;
  /* The bfd_arch value.  */
  enum bfd_architecture arch;
  /* The bfd_mach value.  */
  unsigned long mach;
  /* Endianness (for bi-endian cpus).  Mono-endian cpus can ignore this.  */
  enum bfd_endian endian;
  /* An arch/mach-specific bitmask of selected instruction subsets, mainly
     for processors with run-time-switchable instruction sets.  The default,
     zero, means that there is no constraint.  CGEN-based opcodes ports
     may use ISA_foo masks.  */
  unsigned long insn_sets;

  /* Some targets need information about the current section to accurately
     display insns.  If this is NULL, the target disassembler function
     will have to make its best guess.  */
  asection *section;

  /* An array of pointers to symbols either at the location being disassembled
     or at the start of the function being disassembled.  The array is sorted
     so that the first symbol is intended to be the one used.  The others are
     present for any misc. purposes.  This is not set reliably, but if it is
     not NULL, it is correct.  */
  asymbol **symbols;
  /* Number of symbols in array.  */
  int num_symbols;

  /* For use by the disassembler.
     The top 16 bits are reserved for public use (and are documented here).
     The bottom 16 bits are for the internal use of the disassembler.  */
  unsigned long flags;
#define INSN_HAS_RELOC	0x80000000
  void *private_data;

  /* Function used to get bytes to disassemble.  MEMADDR is the
     address of the stuff to be disassembled, MYADDR is the address to
     put the bytes in, and LENGTH is the number of bytes to read.
     INFO is a pointer to this struct.
     Returns an errno value or 0 for success.  */
  int (*read_memory_func)
    (bfd_vma memaddr, bfd_byte *myaddr, unsigned int length,
     struct disassemble_info *info);

  /* Function which should be called if we get an error that we can't
     recover from.  STATUS is the errno value from read_memory_func and
     MEMADDR is the address that we were trying to read.  INFO is a
     pointer to this struct.  */
  void (*memory_error_func)
    (int status, bfd_vma memaddr, struct disassemble_info *info);

  /* Function called to print ADDR.  */
  void (*print_address_func)
    (bfd_vma addr, struct disassemble_info *info);

  /* Function called to determine if there is a symbol at the given ADDR.
     If there is, the function returns 1, otherwise it returns 0.
     This is used by ports which support an overlay manager where
     the overlay number is held in the top part of an address.  In
     some circumstances we want to include the overlay number in the
     address, (normally because there is a symbol associated with
     that address), but sometimes we want to mask out the overlay bits.  */
  int (* symbol_at_address_func)
    (bfd_vma addr, struct disassemble_info * info);

  /* Function called to check if a SYMBOL is can be displayed to the user.
     This is used by some ports that want to hide special symbols when
     displaying debugging outout.  */
  bfd_boolean (* symbol_is_valid)
    (asymbol *, struct disassemble_info * info);
    
  /* These are for buffer_read_memory.  */
  bfd_byte *buffer;
  bfd_vma buffer_vma;
  unsigned int buffer_length;

  /* This variable may be set by the instruction decoder.  It suggests
      the number of bytes objdump should display on a single line.  If
      the instruction decoder sets this, it should always set it to
      the same value in order to get reasonable looking output.  */
  int bytes_per_line;

  /* The next two variables control the way objdump displays the raw data.  */
  /* For example, if bytes_per_line is 8 and bytes_per_chunk is 4, the */
  /* output will look like this:
     00:   00000000 00000000
     with the chunks displayed according to "display_endian". */
  int bytes_per_chunk;
  enum bfd_endian display_endian;

  /* Number of octets per incremented target address 
     Normally one, but some DSPs have byte sizes of 16 or 32 bits.  */
  unsigned int octets_per_byte;

  /* Results from instruction decoders.  Not all decoders yet support
     this information.  This info is set each time an instruction is
     decoded, and is only valid for the last such instruction.

     To determine whether this decoder supports this information, set
     insn_info_valid to 0, decode an instruction, then check it.  */

  char insn_info_valid;		/* Branch info has been set. */
  char branch_delay_insns;	/* How many sequential insn's will run before
				   a branch takes effect.  (0 = normal) */
  char data_size;		/* Size of data reference in insn, in bytes */
  enum dis_insn_type insn_type;	/* Type of instruction */
  bfd_vma target;		/* Target address of branch or dref, if known;
				   zero if unknown.  */
  bfd_vma target2;		/* Second target address for dref2 */

  /* Command line options specific to the target disassembler.  */
  char * disassembler_options;

} disassemble_info;


/* Standard disassemblers.  Disassemble one instruction at the given
   target address.  Return number of octets processed.  */
typedef int (*disassembler_ftype) (bfd_vma, disassemble_info *);

extern int print_insn_big_mips		(bfd_vma, disassemble_info *);
extern int print_insn_little_mips	(bfd_vma, disassemble_info *);
extern int print_insn_i386		(bfd_vma, disassemble_info *);
extern int print_insn_i386_att		(bfd_vma, disassemble_info *);
extern int print_insn_i386_intel	(bfd_vma, disassemble_info *);
extern int print_insn_ia64		(bfd_vma, disassemble_info *);
extern int print_insn_i370		(bfd_vma, disassemble_info *);
extern int print_insn_m68hc11		(bfd_vma, disassemble_info *);
extern int print_insn_m68hc12		(bfd_vma, disassemble_info *);
extern int print_insn_m68k		(bfd_vma, disassemble_info *);
extern int print_insn_z8001		(bfd_vma, disassemble_info *);
extern int print_insn_z8002		(bfd_vma, disassemble_info *);
extern int print_insn_h8300		(bfd_vma, disassemble_info *);
extern int print_insn_h8300h		(bfd_vma, disassemble_info *);
extern int print_insn_h8300s		(bfd_vma, disassemble_info *);
extern int print_insn_h8500		(bfd_vma, disassemble_info *);
extern int print_insn_alpha		(bfd_vma, disassemble_info *);
extern int print_insn_big_arm		(bfd_vma, disassemble_info *);
extern int print_insn_little_arm	(bfd_vma, disassemble_info *);
extern int print_insn_sparc		(bfd_vma, disassemble_info *);
extern int print_insn_big_a29k		(bfd_vma, disassemble_info *);
extern int print_insn_little_a29k	(bfd_vma, disassemble_info *);
extern int print_insn_avr		(bfd_vma, disassemble_info *);
extern int print_insn_d10v		(bfd_vma, disassemble_info *);
extern int print_insn_d30v		(bfd_vma, disassemble_info *);
extern int print_insn_dlx 		(bfd_vma, disassemble_info *);
extern int print_insn_fr30		(bfd_vma, disassemble_info *);
extern int print_insn_hppa		(bfd_vma, disassemble_info *);
extern int print_insn_i860		(bfd_vma, disassemble_info *);
extern int print_insn_i960		(bfd_vma, disassemble_info *);
extern int print_insn_ip2k		(bfd_vma, disassemble_info *);
extern int print_insn_m32r		(bfd_vma, disassemble_info *);
extern int print_insn_m88k		(bfd_vma, disassemble_info *);
extern int print_insn_mcore		(bfd_vma, disassemble_info *);
extern int print_insn_mmix		(bfd_vma, disassemble_info *);
extern int print_insn_mn10200		(bfd_vma, disassemble_info *);
extern int print_insn_mn10300		(bfd_vma, disassemble_info *);
extern int print_insn_msp430		(bfd_vma, disassemble_info *);
extern int print_insn_ns32k		(bfd_vma, disassemble_info *);
extern int print_insn_openrisc		(bfd_vma, disassemble_info *);
extern int print_insn_big_or32		(bfd_vma, disassemble_info *);
extern int print_insn_little_or32	(bfd_vma, disassemble_info *);
extern int print_insn_pdp11		(bfd_vma, disassemble_info *);
extern int print_insn_pj		(bfd_vma, disassemble_info *);
extern int print_insn_big_powerpc	(bfd_vma, disassemble_info *);
extern int print_insn_little_powerpc	(bfd_vma, disassemble_info *);
extern int print_insn_rs6000		(bfd_vma, disassemble_info *);
extern int print_insn_s390		(bfd_vma, disassemble_info *); 
extern int print_insn_sh		(bfd_vma, disassemble_info *);
extern int print_insn_tic30		(bfd_vma, disassemble_info *);
extern int print_insn_tic4x		(bfd_vma, disassemble_info *);
extern int print_insn_tic54x		(bfd_vma, disassemble_info *);
extern int print_insn_tic80		(bfd_vma, disassemble_info *);
extern int print_insn_v850		(bfd_vma, disassemble_info *);
extern int print_insn_vax		(bfd_vma, disassemble_info *);
extern int print_insn_w65		(bfd_vma, disassemble_info *);
extern int print_insn_xstormy16		(bfd_vma, disassemble_info *);
extern int print_insn_xtensa		(bfd_vma, disassemble_info *);
extern int print_insn_sh64		(bfd_vma, disassemble_info *);
extern int print_insn_sh64x_media	(bfd_vma, disassemble_info *);
extern int print_insn_frv		(bfd_vma, disassemble_info *);
extern int print_insn_iq2000		(bfd_vma, disassemble_info *);

extern disassembler_ftype arc_get_disassembler (void *);
extern disassembler_ftype cris_get_disassembler (bfd *);

extern void print_mips_disassembler_options (FILE *);
extern void print_ppc_disassembler_options (FILE *);
extern void print_arm_disassembler_options (FILE *);
extern void parse_arm_disassembler_option (char *);
extern int get_arm_regname_num_options (void);
extern int set_arm_regname_option (int);
extern int get_arm_regnames (int, const char **, const char **, const char ***);
extern bfd_boolean arm_symbol_is_valid (asymbol *, struct disassemble_info *);

/* Fetch the disassembler for a given BFD, if that support is available.  */
extern disassembler_ftype disassembler (bfd *);

/* Amend the disassemble_info structure as necessary for the target architecture.
   Should only be called after initialising the info->arch field.  */
extern void disassemble_init_for_target (struct disassemble_info * info);

/* Document any target specific options available from the disassembler.  */
extern void disassembler_usage (FILE *);


/* This block of definitions is for particular callers who read instructions
   into a buffer before calling the instruction decoder.  */

/* Here is a function which callers may wish to use for read_memory_func.
   It gets bytes from a buffer.  */
extern int buffer_read_memory
  (bfd_vma, bfd_byte *, unsigned int, struct disassemble_info *);

/* This function goes with buffer_read_memory.
   It prints a message using info->fprintf_func and info->stream.  */
extern void perror_memory (int, bfd_vma, struct disassemble_info *);


/* Just print the address in hex.  This is included for completeness even
   though both GDB and objdump provide their own (to print symbolic
   addresses).  */
extern void generic_print_address
  (bfd_vma, struct disassemble_info *);

/* Always true.  */
extern int generic_symbol_at_address
  (bfd_vma, struct disassemble_info *);

/* Also always true.  */  
extern bfd_boolean generic_symbol_is_valid
  (asymbol *, struct disassemble_info *);
  
/* Method to initialize a disassemble_info struct.  This should be
   called by all applications creating such a struct.  */
extern void init_disassemble_info (struct disassemble_info *info, void *stream,
				   fprintf_ftype fprintf_func);

/* For compatibility with existing code.  */
#define INIT_DISASSEMBLE_INFO(INFO, STREAM, FPRINTF_FUNC) \
  init_disassemble_info (&(INFO), (STREAM), (fprintf_ftype) (FPRINTF_FUNC))
#define INIT_DISASSEMBLE_INFO_NO_ARCH(INFO, STREAM, FPRINTF_FUNC) \
  init_disassemble_info (&(INFO), (STREAM), (fprintf_ftype) (FPRINTF_FUNC))


#ifdef __cplusplus
}
#endif

#endif /* ! defined (DIS_ASM_H) */
