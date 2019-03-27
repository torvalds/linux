/* tc-mips.c -- assemble code for a MIPS chip.
   Copyright 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002,
   2003, 2004, 2005, 2006 Free Software Foundation, Inc.
   Contributed by the OSF and Ralph Campbell.
   Written by Keith Knowles and Ralph Campbell, working independently.
   Modified for ECOFF and R4000 support by Ian Lance Taylor of Cygnus
   Support.

   This file is part of GAS.

   GAS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GAS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GAS; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#include "as.h"
#include "config.h"
#include "subsegs.h"
#include "safe-ctype.h"

#include "opcode/mips.h"
#include "itbl-ops.h"
#include "dwarf2dbg.h"
#include "dw2gencfi.h"

#ifdef DEBUG
#define DBG(x) printf x
#else
#define DBG(x)
#endif

#ifdef OBJ_MAYBE_ELF
/* Clean up namespace so we can include obj-elf.h too.  */
static int mips_output_flavor (void);
static int mips_output_flavor (void) { return OUTPUT_FLAVOR; }
#undef OBJ_PROCESS_STAB
#undef OUTPUT_FLAVOR
#undef S_GET_ALIGN
#undef S_GET_SIZE
#undef S_SET_ALIGN
#undef S_SET_SIZE
#undef obj_frob_file
#undef obj_frob_file_after_relocs
#undef obj_frob_symbol
#undef obj_pop_insert
#undef obj_sec_sym_ok_for_reloc
#undef OBJ_COPY_SYMBOL_ATTRIBUTES

#include "obj-elf.h"
/* Fix any of them that we actually care about.  */
#undef OUTPUT_FLAVOR
#define OUTPUT_FLAVOR mips_output_flavor()
#endif

#if defined (OBJ_ELF)
#include "elf/mips.h"
#endif

#ifndef ECOFF_DEBUGGING
#define NO_ECOFF_DEBUGGING
#define ECOFF_DEBUGGING 0
#endif

int mips_flag_mdebug = -1;

/* Control generation of .pdr sections.  Off by default on IRIX: the native
   linker doesn't know about and discards them, but relocations against them
   remain, leading to rld crashes.  */
#ifdef TE_IRIX
int mips_flag_pdr = FALSE;
#else
int mips_flag_pdr = TRUE;
#endif

/* Control generation of error message for unsupported instructions in
   Octeon. Octeon does not have floating point, and all the instructions
   that use floating point registers are not allowed in Elf targets but 
   are allowed in Linux targets by default.  */
#ifdef OCTEON_ERROR_ON_UNSUPPORTED
static int octeon_error_on_unsupported = 1;
#else
static int octeon_error_on_unsupported = 0;
#endif

/* Control generation of Octeon/MIPS unaligned load/store instructions.
   For ELF target, default to Octeon load/store instructions.
   For Linux target, default to MIPS load/store instructions.  */
#ifdef OCTEON_USE_UNALIGN
static int octeon_use_unalign = 1;
#else
static int octeon_use_unalign = 0;
#endif

#include "ecoff.h"

#if defined (OBJ_ELF) || defined (OBJ_MAYBE_ELF)
static char *mips_regmask_frag;
#endif

#define ZERO 0
#define AT  1
#define TREG 24
#define PIC_CALL_REG 25
#define KT0 26
#define KT1 27
#define GP  28
#define SP  29
#define FP  30
#define RA  31

#define ILLEGAL_REG (32)

/* Allow override of standard little-endian ECOFF format.  */

#ifndef ECOFF_LITTLE_FORMAT
#define ECOFF_LITTLE_FORMAT "ecoff-littlemips"
#endif

extern int target_big_endian;

/* The name of the readonly data section.  */
#define RDATA_SECTION_NAME (OUTPUT_FLAVOR == bfd_target_ecoff_flavour \
			    ? ".rdata" \
			    : OUTPUT_FLAVOR == bfd_target_coff_flavour \
			    ? ".rdata" \
			    : OUTPUT_FLAVOR == bfd_target_elf_flavour \
			    ? ".rodata" \
			    : (abort (), ""))

/* Information about an instruction, including its format, operands
   and fixups.  */
struct mips_cl_insn
{
  /* The opcode's entry in mips_opcodes or mips16_opcodes.  */
  const struct mips_opcode *insn_mo;

  /* True if this is a mips16 instruction and if we want the extended
     form of INSN_MO.  */
  bfd_boolean use_extend;

  /* The 16-bit extension instruction to use when USE_EXTEND is true.  */
  unsigned short extend;

  /* The 16-bit or 32-bit bitstring of the instruction itself.  This is
     a copy of INSN_MO->match with the operands filled in.  */
  unsigned long insn_opcode;

  /* The frag that contains the instruction.  */
  struct frag *frag;

  /* The offset into FRAG of the first instruction byte.  */
  long where;

  /* The relocs associated with the instruction, if any.  */
  fixS *fixp[3];

  /* True if this entry cannot be moved from its current position.  */
  unsigned int fixed_p : 1;

  /* True if this instruction occurred in a .set noreorder block.  */
  unsigned int noreorder_p : 1;

  /* True for mips16 instructions that jump to an absolute address.  */
  unsigned int mips16_absolute_jump_p : 1;
};

/* The ABI to use.  */
enum mips_abi_level
{
  NO_ABI = 0,
  O32_ABI,
  O64_ABI,
  N32_ABI,
  N64_ABI,
  EABI_ABI
};

/* MIPS ABI we are using for this output file.  */
static enum mips_abi_level mips_abi = NO_ABI;

/* Whether or not we have code that can call pic code.  */
int mips_abicalls = FALSE;

/* Whether or not we have code which can be put into a shared
   library.  */
static bfd_boolean mips_in_shared = TRUE;

/* This is the set of options which may be modified by the .set
   pseudo-op.  We use a struct so that .set push and .set pop are more
   reliable.  */

struct mips_set_options
{
  /* MIPS ISA (Instruction Set Architecture) level.  This is set to -1
     if it has not been initialized.  Changed by `.set mipsN', and the
     -mipsN command line option, and the default CPU.  */
  int isa;
  /* Enabled Application Specific Extensions (ASEs).  These are set to -1
     if they have not been initialized.  Changed by `.set <asename>', by
     command line options, and based on the default architecture.  */
  int ase_mips3d;
  int ase_mdmx;
  int ase_smartmips;
  int ase_dsp;
  int ase_dspr2;
  int ase_mt;
  /* Whether we are assembling for the mips16 processor.  0 if we are
     not, 1 if we are, and -1 if the value has not been initialized.
     Changed by `.set mips16' and `.set nomips16', and the -mips16 and
     -nomips16 command line options, and the default CPU.  */
  int mips16;
  /* Non-zero if we should not reorder instructions.  Changed by `.set
     reorder' and `.set noreorder'.  */
  int noreorder;
  /* Non-zero if we should not permit the $at ($1) register to be used
     in instructions.  Changed by `.set at' and `.set noat'.  */
  int noat;
  /* Non-zero if we should warn when a macro instruction expands into
     more than one machine instruction.  Changed by `.set nomacro' and
     `.set macro'.  */
  int warn_about_macros;
  /* Non-zero if we should not move instructions.  Changed by `.set
     move', `.set volatile', `.set nomove', and `.set novolatile'.  */
  int nomove;
  /* Non-zero if we should not optimize branches by moving the target
     of the branch into the delay slot.  Actually, we don't perform
     this optimization anyhow.  Changed by `.set bopt' and `.set
     nobopt'.  */
  int nobopt;
  /* Non-zero if we should not autoextend mips16 instructions.
     Changed by `.set autoextend' and `.set noautoextend'.  */
  int noautoextend;
  /* Restrict general purpose registers and floating point registers
     to 32 bit.  This is initially determined when -mgp32 or -mfp32
     is passed but can changed if the assembler code uses .set mipsN.  */
  int gp32;
  int fp32;
  /* MIPS architecture (CPU) type.  Changed by .set arch=FOO, the -march
     command line option, and the default CPU.  */
  int arch;
  /* True if ".set sym32" is in effect.  */
  bfd_boolean sym32;
};

/* True if -mgp32 was passed.  */
static int file_mips_gp32 = -1;

/* True if -mfp32 was passed.  */
static int file_mips_fp32 = -1;

/* This is the struct we use to hold the current set of options.  Note
   that we must set the isa field to ISA_UNKNOWN and the ASE fields to
   -1 to indicate that they have not been initialized.  */

static struct mips_set_options mips_opts =
{
  ISA_UNKNOWN, -1, -1, 0, -1, -1, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0, CPU_UNKNOWN, FALSE
};

/* These variables are filled in with the masks of registers used.
   The object format code reads them and puts them in the appropriate
   place.  */
unsigned long mips_gprmask;
unsigned long mips_cprmask[4];

/* MIPS ISA we are using for this output file.  */
static int file_mips_isa = ISA_UNKNOWN;

/* True if -mips16 was passed or implied by arguments passed on the
   command line (e.g., by -march).  */
static int file_ase_mips16;

#define ISA_SUPPORTS_MIPS16E (mips_opts.isa == ISA_MIPS32		\
			      || mips_opts.isa == ISA_MIPS32R2		\
			      || mips_opts.isa == ISA_MIPS64		\
			      || mips_opts.isa == ISA_MIPS64R2)

/* True if -mips3d was passed or implied by arguments passed on the
   command line (e.g., by -march).  */
static int file_ase_mips3d;

/* True if -mdmx was passed or implied by arguments passed on the
   command line (e.g., by -march).  */
static int file_ase_mdmx;

/* True if -msmartmips was passed or implied by arguments passed on the
   command line (e.g., by -march).  */
static int file_ase_smartmips;

#define ISA_SUPPORTS_SMARTMIPS (mips_opts.isa == ISA_MIPS32		\
				|| mips_opts.isa == ISA_MIPS32R2)

/* True if -mdsp was passed or implied by arguments passed on the
   command line (e.g., by -march).  */
static int file_ase_dsp;

#define ISA_SUPPORTS_DSP_ASE (mips_opts.isa == ISA_MIPS32R2		\
			      || mips_opts.isa == ISA_MIPS64R2)

#define ISA_SUPPORTS_DSP64_ASE (mips_opts.isa == ISA_MIPS64R2)

/* True if -mdspr2 was passed or implied by arguments passed on the
   command line (e.g., by -march).  */
static int file_ase_dspr2;

#define ISA_SUPPORTS_DSPR2_ASE (mips_opts.isa == ISA_MIPS32R2		\
			        || mips_opts.isa == ISA_MIPS64R2)

/* True if -mmt was passed or implied by arguments passed on the
   command line (e.g., by -march).  */
static int file_ase_mt;

#define ISA_SUPPORTS_MT_ASE (mips_opts.isa == ISA_MIPS32R2		\
			     || mips_opts.isa == ISA_MIPS64R2)

/* The argument of the -march= flag.  The architecture we are assembling.  */
static int file_mips_arch = CPU_UNKNOWN;
static const char *mips_arch_string;

/* The argument of the -mtune= flag.  The architecture for which we
   are optimizing.  */
static int mips_tune = CPU_UNKNOWN;
static const char *mips_tune_string;

/* True when generating 32-bit code for a 64-bit processor.  */
static int mips_32bitmode = 0;

/* True if the given ABI requires 32-bit registers.  */
#define ABI_NEEDS_32BIT_REGS(ABI) ((ABI) == O32_ABI)

/* Likewise 64-bit registers.  */
#define ABI_NEEDS_64BIT_REGS(ABI)	\
  ((ABI) == N32_ABI 			\
   || (ABI) == N64_ABI			\
   || (ABI) == O64_ABI)

/*  Return true if ISA supports 64 bit wide gp registers.  */
#define ISA_HAS_64BIT_REGS(ISA)		\
  ((ISA) == ISA_MIPS3			\
   || (ISA) == ISA_MIPS4		\
   || (ISA) == ISA_MIPS5		\
   || (ISA) == ISA_MIPS64		\
   || (ISA) == ISA_MIPS64R2)

/*  Return true if ISA supports 64 bit wide float registers.  */
#define ISA_HAS_64BIT_FPRS(ISA)		\
  ((ISA) == ISA_MIPS3			\
   || (ISA) == ISA_MIPS4		\
   || (ISA) == ISA_MIPS5		\
   || (ISA) == ISA_MIPS32R2		\
   || (ISA) == ISA_MIPS64		\
   || (ISA) == ISA_MIPS64R2)

/* Return true if ISA supports 64-bit right rotate (dror et al.)
   instructions.  */
#define ISA_HAS_DROR(ISA)		\
  ((ISA) == ISA_MIPS64R2)

/* Return true if ISA supports 32-bit right rotate (ror et al.)
   instructions.  */
#define ISA_HAS_ROR(ISA)		\
  ((ISA) == ISA_MIPS32R2		\
   || (ISA) == ISA_MIPS64R2		\
   || mips_opts.ase_smartmips)

/* Return true if ISA supports ins instructions. */
#define ISA_HAS_INS(ISA)		\
  ((ISA) == ISA_MIPS32R2		\
  || (ISA) == ISA_MIPS64R2)

/* Return true if ISA supports single-precision floats in odd registers.  */
#define ISA_HAS_ODD_SINGLE_FPR(ISA)	\
  ((ISA) == ISA_MIPS32			\
   || (ISA) == ISA_MIPS32R2		\
   || (ISA) == ISA_MIPS64		\
   || (ISA) == ISA_MIPS64R2)

/* Return true if ISA supports move to/from high part of a 64-bit
   floating-point register. */
#define ISA_HAS_MXHC1(ISA)		\
  ((ISA) == ISA_MIPS32R2		\
   || (ISA) == ISA_MIPS64R2)

#define HAVE_32BIT_GPRS		                   \
    (mips_opts.gp32 || !ISA_HAS_64BIT_REGS (mips_opts.isa))

#define HAVE_32BIT_FPRS                            \
    (mips_opts.fp32 || !ISA_HAS_64BIT_FPRS (mips_opts.isa))

#define HAVE_64BIT_GPRS (!HAVE_32BIT_GPRS)
#define HAVE_64BIT_FPRS (!HAVE_32BIT_FPRS)

#define HAVE_NEWABI (mips_abi == N32_ABI || mips_abi == N64_ABI)

#define HAVE_64BIT_OBJECTS (mips_abi == N64_ABI)

/* True if relocations are stored in-place.  */
#define HAVE_IN_PLACE_ADDENDS (!HAVE_NEWABI)

/* The ABI-derived address size.  */
#define HAVE_64BIT_ADDRESSES \
  (HAVE_64BIT_GPRS && (mips_abi == EABI_ABI || mips_abi == N64_ABI))
#define HAVE_32BIT_ADDRESSES (!HAVE_64BIT_ADDRESSES)

/* The size of symbolic constants (i.e., expressions of the form
   "SYMBOL" or "SYMBOL + OFFSET").  */
#define HAVE_32BIT_SYMBOLS \
  (HAVE_32BIT_ADDRESSES || !HAVE_64BIT_OBJECTS || mips_opts.sym32)
#define HAVE_64BIT_SYMBOLS (!HAVE_32BIT_SYMBOLS)

/* Addresses are loaded in different ways, depending on the address size
   in use.  The n32 ABI Documentation also mandates the use of additions
   with overflow checking, but existing implementations don't follow it.  */
#define ADDRESS_ADD_INSN						\
   (HAVE_32BIT_ADDRESSES ? "addu" : "daddu")

#define ADDRESS_ADDI_INSN						\
   (HAVE_32BIT_ADDRESSES ? "addiu" : "daddiu")

#define ADDRESS_LOAD_INSN						\
   (HAVE_32BIT_ADDRESSES ? "lw" : "ld")

#define ADDRESS_STORE_INSN						\
   (HAVE_32BIT_ADDRESSES ? "sw" : "sd")

/* Return true if the given CPU supports the MIPS16 ASE.  */
#define CPU_HAS_MIPS16(cpu)						\
   (strncmp (TARGET_CPU, "mips16", sizeof ("mips16") - 1) == 0		\
    || strncmp (TARGET_CANONICAL, "mips-lsi-elf", sizeof ("mips-lsi-elf") - 1) == 0)

/* True if CPU has a dror instruction.  */
#define CPU_HAS_DROR(CPU)	((CPU) == CPU_VR5400 || (CPU) == CPU_VR5500)

/* True if CPU has a ror instruction.  */
#define CPU_HAS_ROR(CPU)	CPU_HAS_DROR (CPU)

/* True if mflo and mfhi can be immediately followed by instructions
   which write to the HI and LO registers.

   According to MIPS specifications, MIPS ISAs I, II, and III need
   (at least) two instructions between the reads of HI/LO and
   instructions which write them, and later ISAs do not.  Contradicting
   the MIPS specifications, some MIPS IV processor user manuals (e.g.
   the UM for the NEC Vr5000) document needing the instructions between
   HI/LO reads and writes, as well.  Therefore, we declare only MIPS32,
   MIPS64 and later ISAs to have the interlocks, plus any specific
   earlier-ISA CPUs for which CPU documentation declares that the
   instructions are really interlocked.  */
#define hilo_interlocks \
  (mips_opts.isa == ISA_MIPS32                        \
   || mips_opts.isa == ISA_MIPS32R2                   \
   || mips_opts.isa == ISA_MIPS64                     \
   || mips_opts.isa == ISA_MIPS64R2                   \
   || mips_opts.arch == CPU_R4010                     \
   || mips_opts.arch == CPU_R10000                    \
   || mips_opts.arch == CPU_R12000                    \
   || mips_opts.arch == CPU_RM7000                    \
   || mips_opts.arch == CPU_VR5500                    \
   )

/* Whether the processor uses hardware interlocks to protect reads
   from the GPRs after they are loaded from memory, and thus does not
   require nops to be inserted.  This applies to instructions marked
   INSN_LOAD_MEMORY_DELAY.  These nops are only required at MIPS ISA
   level I.  */
#define gpr_interlocks \
  (mips_opts.isa != ISA_MIPS1  \
   || mips_opts.arch == CPU_R3900)

/* Whether the processor uses hardware interlocks to avoid delays
   required by coprocessor instructions, and thus does not require
   nops to be inserted.  This applies to instructions marked
   INSN_LOAD_COPROC_DELAY, INSN_COPROC_MOVE_DELAY, and to delays
   between instructions marked INSN_WRITE_COND_CODE and ones marked
   INSN_READ_COND_CODE.  These nops are only required at MIPS ISA
   levels I, II, and III.  */
/* Itbl support may require additional care here.  */
#define cop_interlocks                                \
  ((mips_opts.isa != ISA_MIPS1                        \
    && mips_opts.isa != ISA_MIPS2                     \
    && mips_opts.isa != ISA_MIPS3)                    \
   || mips_opts.arch == CPU_R4300                     \
   )

/* Whether the processor uses hardware interlocks to protect reads
   from coprocessor registers after they are loaded from memory, and
   thus does not require nops to be inserted.  This applies to
   instructions marked INSN_COPROC_MEMORY_DELAY.  These nops are only
   requires at MIPS ISA level I.  */
#define cop_mem_interlocks (mips_opts.isa != ISA_MIPS1)

/* Is this a mfhi or mflo instruction?  */
#define MF_HILO_INSN(PINFO) \
          ((PINFO & INSN_READ_HI) || (PINFO & INSN_READ_LO))

/* MIPS PIC level.  */

enum mips_pic_level mips_pic;

/* 1 if we should generate 32 bit offsets from the $gp register in
   SVR4_PIC mode.  Currently has no meaning in other modes.  */
static int mips_big_got = 0;

/* 1 if trap instructions should used for overflow rather than break
   instructions.  */
static int mips_trap = 0;

/* 1 if double width floating point constants should not be constructed
   by assembling two single width halves into two single width floating
   point registers which just happen to alias the double width destination
   register.  On some architectures this aliasing can be disabled by a bit
   in the status register, and the setting of this bit cannot be determined
   automatically at assemble time.  */
static int mips_disable_float_construction;

/* Non-zero if any .set noreorder directives were used.  */

static int mips_any_noreorder;

/* Non-zero if nops should be inserted when the register referenced in
   an mfhi/mflo instruction is read in the next two instructions.  */
static int mips_7000_hilo_fix;

/* The size of objects in the small data section.  */
static unsigned int g_switch_value = 8;
/* Whether the -G option was used.  */
static int g_switch_seen = 0;

#define N_RMASK 0xc4
#define N_VFP   0xd4

/* If we can determine in advance that GP optimization won't be
   possible, we can skip the relaxation stuff that tries to produce
   GP-relative references.  This makes delay slot optimization work
   better.

   This function can only provide a guess, but it seems to work for
   gcc output.  It needs to guess right for gcc, otherwise gcc
   will put what it thinks is a GP-relative instruction in a branch
   delay slot.

   I don't know if a fix is needed for the SVR4_PIC mode.  I've only
   fixed it for the non-PIC mode.  KR 95/04/07  */
static int nopic_need_relax (symbolS *, int);

/* handle of the OPCODE hash table */
static struct hash_control *op_hash = NULL;

/* The opcode hash table we use for the mips16.  */
static struct hash_control *mips16_op_hash = NULL;

/* This array holds the chars that always start a comment.  If the
    pre-processor is disabled, these aren't very useful */
const char comment_chars[] = "#";

/* This array holds the chars that only start a comment at the beginning of
   a line.  If the line seems to have the form '# 123 filename'
   .line and .file directives will appear in the pre-processed output */
/* Note that input_file.c hand checks for '#' at the beginning of the
   first line of the input file.  This is because the compiler outputs
   #NO_APP at the beginning of its output.  */
/* Also note that C style comments are always supported.  */
const char line_comment_chars[] = "#";

/* This array holds machine specific line separator characters.  */
const char line_separator_chars[] = ";";

/* Chars that can be used to separate mant from exp in floating point nums */
const char EXP_CHARS[] = "eE";

/* Chars that mean this number is a floating point constant */
/* As in 0f12.456 */
/* or    0d1.2345e12 */
const char FLT_CHARS[] = "rRsSfFdDxXpP";

/* Also be aware that MAXIMUM_NUMBER_OF_CHARS_FOR_FLOAT may have to be
   changed in read.c .  Ideally it shouldn't have to know about it at all,
   but nothing is ideal around here.
 */

static char *insn_error;

static int auto_align = 1;

/* When outputting SVR4 PIC code, the assembler needs to know the
   offset in the stack frame from which to restore the $gp register.
   This is set by the .cprestore pseudo-op, and saved in this
   variable.  */
static offsetT mips_cprestore_offset = -1;

/* Similar for NewABI PIC code, where $gp is callee-saved.  NewABI has some
   more optimizations, it can use a register value instead of a memory-saved
   offset and even an other register than $gp as global pointer.  */
static offsetT mips_cpreturn_offset = -1;
static int mips_cpreturn_register = -1;
static int mips_gp_register = GP;
static int mips_gprel_offset = 0;

/* Whether mips_cprestore_offset has been set in the current function
   (or whether it has already been warned about, if not).  */
static int mips_cprestore_valid = 0;

/* This is the register which holds the stack frame, as set by the
   .frame pseudo-op.  This is needed to implement .cprestore.  */
static int mips_frame_reg = SP;

/* Whether mips_frame_reg has been set in the current function
   (or whether it has already been warned about, if not).  */
static int mips_frame_reg_valid = 0;

/* To output NOP instructions correctly, we need to keep information
   about the previous two instructions.  */

/* Whether we are optimizing.  The default value of 2 means to remove
   unneeded NOPs and swap branch instructions when possible.  A value
   of 1 means to not swap branches.  A value of 0 means to always
   insert NOPs.  */
static int mips_optimize = 2;

/* Debugging level.  -g sets this to 2.  -gN sets this to N.  -g0 is
   equivalent to seeing no -g option at all.  */
static int mips_debug = 0;

/* The maximum number of NOPs needed to avoid the VR4130 mflo/mfhi errata.  */
#define MAX_VR4130_NOPS 4

/* The maximum number of NOPs needed to fill delay slots.  */
#define MAX_DELAY_NOPS 2

/* The maximum number of NOPs needed for any purpose.  */
#define MAX_NOPS 4

/* A list of previous instructions, with index 0 being the most recent.
   We need to look back MAX_NOPS instructions when filling delay slots
   or working around processor errata.  We need to look back one
   instruction further if we're thinking about using history[0] to
   fill a branch delay slot.  */
static struct mips_cl_insn history[1 + MAX_NOPS];

/* Nop instructions used by emit_nop.  */
static struct mips_cl_insn nop_insn, mips16_nop_insn;

/* The appropriate nop for the current mode.  */
#define NOP_INSN (mips_opts.mips16 ? &mips16_nop_insn : &nop_insn)

/* If this is set, it points to a frag holding nop instructions which
   were inserted before the start of a noreorder section.  If those
   nops turn out to be unnecessary, the size of the frag can be
   decreased.  */
static fragS *prev_nop_frag;

/* The number of nop instructions we created in prev_nop_frag.  */
static int prev_nop_frag_holds;

/* The number of nop instructions that we know we need in
   prev_nop_frag.  */
static int prev_nop_frag_required;

/* The number of instructions we've seen since prev_nop_frag.  */
static int prev_nop_frag_since;

/* For ECOFF and ELF, relocations against symbols are done in two
   parts, with a HI relocation and a LO relocation.  Each relocation
   has only 16 bits of space to store an addend.  This means that in
   order for the linker to handle carries correctly, it must be able
   to locate both the HI and the LO relocation.  This means that the
   relocations must appear in order in the relocation table.

   In order to implement this, we keep track of each unmatched HI
   relocation.  We then sort them so that they immediately precede the
   corresponding LO relocation.  */

struct mips_hi_fixup
{
  /* Next HI fixup.  */
  struct mips_hi_fixup *next;
  /* This fixup.  */
  fixS *fixp;
  /* The section this fixup is in.  */
  segT seg;
};

/* The list of unmatched HI relocs.  */

static struct mips_hi_fixup *mips_hi_fixup_list;

/* The frag containing the last explicit relocation operator.
   Null if explicit relocations have not been used.  */

static fragS *prev_reloc_op_frag;

/* Map normal MIPS register numbers to mips16 register numbers.  */

#define X ILLEGAL_REG
static const int mips32_to_16_reg_map[] =
{
  X, X, 2, 3, 4, 5, 6, 7,
  X, X, X, X, X, X, X, X,
  0, 1, X, X, X, X, X, X,
  X, X, X, X, X, X, X, X
};
#undef X

/* Map mips16 register numbers to normal MIPS register numbers.  */

static const unsigned int mips16_to_32_reg_map[] =
{
  16, 17, 2, 3, 4, 5, 6, 7
};

/* Classifies the kind of instructions we're interested in when
   implementing -mfix-vr4120.  */
enum fix_vr4120_class {
  FIX_VR4120_MACC,
  FIX_VR4120_DMACC,
  FIX_VR4120_MULT,
  FIX_VR4120_DMULT,
  FIX_VR4120_DIV,
  FIX_VR4120_MTHILO,
  NUM_FIX_VR4120_CLASSES
};

/* Given two FIX_VR4120_* values X and Y, bit Y of element X is set if
   there must be at least one other instruction between an instruction
   of type X and an instruction of type Y.  */
static unsigned int vr4120_conflicts[NUM_FIX_VR4120_CLASSES];

/* True if -mfix-vr4120 is in force.  */
static int mips_fix_vr4120;

/* ...likewise -mfix-vr4130.  */
static int mips_fix_vr4130;

/* We don't relax branches by default, since this causes us to expand
   `la .l2 - .l1' if there's a branch between .l1 and .l2, because we
   fail to compute the offset before expanding the macro to the most
   efficient expansion.  */

static int mips_relax_branch;

/* The expansion of many macros depends on the type of symbol that
   they refer to.  For example, when generating position-dependent code,
   a macro that refers to a symbol may have two different expansions,
   one which uses GP-relative addresses and one which uses absolute
   addresses.  When generating SVR4-style PIC, a macro may have
   different expansions for local and global symbols.

   We handle these situations by generating both sequences and putting
   them in variant frags.  In position-dependent code, the first sequence
   will be the GP-relative one and the second sequence will be the
   absolute one.  In SVR4 PIC, the first sequence will be for global
   symbols and the second will be for local symbols.

   The frag's "subtype" is RELAX_ENCODE (FIRST, SECOND), where FIRST and
   SECOND are the lengths of the two sequences in bytes.  These fields
   can be extracted using RELAX_FIRST() and RELAX_SECOND().  In addition,
   the subtype has the following flags:

   RELAX_USE_SECOND
	Set if it has been decided that we should use the second
	sequence instead of the first.

   RELAX_SECOND_LONGER
	Set in the first variant frag if the macro's second implementation
	is longer than its first.  This refers to the macro as a whole,
	not an individual relaxation.

   RELAX_NOMACRO
	Set in the first variant frag if the macro appeared in a .set nomacro
	block and if one alternative requires a warning but the other does not.

   RELAX_DELAY_SLOT
	Like RELAX_NOMACRO, but indicates that the macro appears in a branch
	delay slot.

   The frag's "opcode" points to the first fixup for relaxable code.

   Relaxable macros are generated using a sequence such as:

      relax_start (SYMBOL);
      ... generate first expansion ...
      relax_switch ();
      ... generate second expansion ...
      relax_end ();

   The code and fixups for the unwanted alternative are discarded
   by md_convert_frag.  */
#define RELAX_ENCODE(FIRST, SECOND) (((FIRST) << 8) | (SECOND))

#define RELAX_FIRST(X) (((X) >> 8) & 0xff)
#define RELAX_SECOND(X) ((X) & 0xff)
#define RELAX_USE_SECOND 0x10000
#define RELAX_SECOND_LONGER 0x20000
#define RELAX_NOMACRO 0x40000
#define RELAX_DELAY_SLOT 0x80000

/* Branch without likely bit.  If label is out of range, we turn:

 	beq reg1, reg2, label
	delay slot

   into

        bne reg1, reg2, 0f
        nop
        j label
     0: delay slot

   with the following opcode replacements:

	beq <-> bne
	blez <-> bgtz
	bltz <-> bgez
	bc1f <-> bc1t

	bltzal <-> bgezal  (with jal label instead of j label)

   Even though keeping the delay slot instruction in the delay slot of
   the branch would be more efficient, it would be very tricky to do
   correctly, because we'd have to introduce a variable frag *after*
   the delay slot instruction, and expand that instead.  Let's do it
   the easy way for now, even if the branch-not-taken case now costs
   one additional instruction.  Out-of-range branches are not supposed
   to be common, anyway.

   Branch likely.  If label is out of range, we turn:

	beql reg1, reg2, label
	delay slot (annulled if branch not taken)

   into

        beql reg1, reg2, 1f
        nop
        beql $0, $0, 2f
        nop
     1: j[al] label
        delay slot (executed only if branch taken)
     2:

   It would be possible to generate a shorter sequence by losing the
   likely bit, generating something like:

	bne reg1, reg2, 0f
	nop
	j[al] label
	delay slot (executed only if branch taken)
     0:

	beql -> bne
	bnel -> beq
	blezl -> bgtz
	bgtzl -> blez
	bltzl -> bgez
	bgezl -> bltz
	bc1fl -> bc1t
	bc1tl -> bc1f

	bltzall -> bgezal  (with jal label instead of j label)
	bgezall -> bltzal  (ditto)


   but it's not clear that it would actually improve performance.  */
#define RELAX_BRANCH_ENCODE(uncond, likely, link, toofar) \
  ((relax_substateT) \
   (0xc0000000 \
    | ((toofar) ? 1 : 0) \
    | ((link) ? 2 : 0) \
    | ((likely) ? 4 : 0) \
    | ((uncond) ? 8 : 0)))
#define RELAX_BRANCH_P(i) (((i) & 0xf0000000) == 0xc0000000)
#define RELAX_BRANCH_UNCOND(i) (((i) & 8) != 0)
#define RELAX_BRANCH_LIKELY(i) (((i) & 4) != 0)
#define RELAX_BRANCH_LINK(i) (((i) & 2) != 0)
#define RELAX_BRANCH_TOOFAR(i) (((i) & 1) != 0)

/* For mips16 code, we use an entirely different form of relaxation.
   mips16 supports two versions of most instructions which take
   immediate values: a small one which takes some small value, and a
   larger one which takes a 16 bit value.  Since branches also follow
   this pattern, relaxing these values is required.

   We can assemble both mips16 and normal MIPS code in a single
   object.  Therefore, we need to support this type of relaxation at
   the same time that we support the relaxation described above.  We
   use the high bit of the subtype field to distinguish these cases.

   The information we store for this type of relaxation is the
   argument code found in the opcode file for this relocation, whether
   the user explicitly requested a small or extended form, and whether
   the relocation is in a jump or jal delay slot.  That tells us the
   size of the value, and how it should be stored.  We also store
   whether the fragment is considered to be extended or not.  We also
   store whether this is known to be a branch to a different section,
   whether we have tried to relax this frag yet, and whether we have
   ever extended a PC relative fragment because of a shift count.  */
#define RELAX_MIPS16_ENCODE(type, small, ext, dslot, jal_dslot)	\
  (0x80000000							\
   | ((type) & 0xff)						\
   | ((small) ? 0x100 : 0)					\
   | ((ext) ? 0x200 : 0)					\
   | ((dslot) ? 0x400 : 0)					\
   | ((jal_dslot) ? 0x800 : 0))
#define RELAX_MIPS16_P(i) (((i) & 0xc0000000) == 0x80000000)
#define RELAX_MIPS16_TYPE(i) ((i) & 0xff)
#define RELAX_MIPS16_USER_SMALL(i) (((i) & 0x100) != 0)
#define RELAX_MIPS16_USER_EXT(i) (((i) & 0x200) != 0)
#define RELAX_MIPS16_DSLOT(i) (((i) & 0x400) != 0)
#define RELAX_MIPS16_JAL_DSLOT(i) (((i) & 0x800) != 0)
#define RELAX_MIPS16_EXTENDED(i) (((i) & 0x1000) != 0)
#define RELAX_MIPS16_MARK_EXTENDED(i) ((i) | 0x1000)
#define RELAX_MIPS16_CLEAR_EXTENDED(i) ((i) &~ 0x1000)
#define RELAX_MIPS16_LONG_BRANCH(i) (((i) & 0x2000) != 0)
#define RELAX_MIPS16_MARK_LONG_BRANCH(i) ((i) | 0x2000)
#define RELAX_MIPS16_CLEAR_LONG_BRANCH(i) ((i) &~ 0x2000)

/* Is the given value a sign-extended 32-bit value?  */
#define IS_SEXT_32BIT_NUM(x)						\
  (((x) &~ (offsetT) 0x7fffffff) == 0					\
   || (((x) &~ (offsetT) 0x7fffffff) == ~ (offsetT) 0x7fffffff))

/* Is the given value a sign-extended 16-bit value?  */
#define IS_SEXT_16BIT_NUM(x)						\
  (((x) &~ (offsetT) 0x7fff) == 0					\
   || (((x) &~ (offsetT) 0x7fff) == ~ (offsetT) 0x7fff))

/* Is the given value a zero-extended 32-bit value?  Or a negated one?  */
#define IS_ZEXT_32BIT_NUM(x)						\
  (((x) &~ (offsetT) 0xffffffff) == 0					\
   || (((x) &~ (offsetT) 0xffffffff) == ~ (offsetT) 0xffffffff))

/* Replace bits MASK << SHIFT of STRUCT with the equivalent bits in
   VALUE << SHIFT.  VALUE is evaluated exactly once.  */
#define INSERT_BITS(STRUCT, VALUE, MASK, SHIFT) \
  (STRUCT) = (((STRUCT) & ~((MASK) << (SHIFT))) \
	      | (((VALUE) & (MASK)) << (SHIFT)))

/* Extract bits MASK << SHIFT from STRUCT and shift them right
   SHIFT places.  */
#define EXTRACT_BITS(STRUCT, MASK, SHIFT) \
  (((STRUCT) >> (SHIFT)) & (MASK))

/* Change INSN's opcode so that the operand given by FIELD has value VALUE.
   INSN is a mips_cl_insn structure and VALUE is evaluated exactly once.

   include/opcode/mips.h specifies operand fields using the macros
   OP_MASK_<FIELD> and OP_SH_<FIELD>.  The MIPS16 equivalents start
   with "MIPS16OP" instead of "OP".  */
#define INSERT_OPERAND(FIELD, INSN, VALUE) \
  INSERT_BITS ((INSN).insn_opcode, VALUE, OP_MASK_##FIELD, OP_SH_##FIELD)
#define MIPS16_INSERT_OPERAND(FIELD, INSN, VALUE) \
  INSERT_BITS ((INSN).insn_opcode, VALUE, \
		MIPS16OP_MASK_##FIELD, MIPS16OP_SH_##FIELD)

/* Extract the operand given by FIELD from mips_cl_insn INSN.  */
#define EXTRACT_OPERAND(FIELD, INSN) \
  EXTRACT_BITS ((INSN).insn_opcode, OP_MASK_##FIELD, OP_SH_##FIELD)
#define MIPS16_EXTRACT_OPERAND(FIELD, INSN) \
  EXTRACT_BITS ((INSN).insn_opcode, \
		MIPS16OP_MASK_##FIELD, \
		MIPS16OP_SH_##FIELD)

/* Global variables used when generating relaxable macros.  See the
   comment above RELAX_ENCODE for more details about how relaxation
   is used.  */
static struct {
  /* 0 if we're not emitting a relaxable macro.
     1 if we're emitting the first of the two relaxation alternatives.
     2 if we're emitting the second alternative.  */
  int sequence;

  /* The first relaxable fixup in the current frag.  (In other words,
     the first fixup that refers to relaxable code.)  */
  fixS *first_fixup;

  /* sizes[0] says how many bytes of the first alternative are stored in
     the current frag.  Likewise sizes[1] for the second alternative.  */
  unsigned int sizes[2];

  /* The symbol on which the choice of sequence depends.  */
  symbolS *symbol;
} mips_relax;

/* Global variables used to decide whether a macro needs a warning.  */
static struct {
  /* True if the macro is in a branch delay slot.  */
  bfd_boolean delay_slot_p;

  /* For relaxable macros, sizes[0] is the length of the first alternative
     in bytes and sizes[1] is the length of the second alternative.
     For non-relaxable macros, both elements give the length of the
     macro in bytes.  */
  unsigned int sizes[2];

  /* The first variant frag for this macro.  */
  fragS *first_frag;
} mips_macro_warning;

/* Prototypes for static functions.  */

#define internalError()							\
    as_fatal (_("internal Error, line %d, %s"), __LINE__, __FILE__)

enum mips_regclass { MIPS_GR_REG, MIPS_FP_REG, MIPS16_REG };

static void append_insn
  (struct mips_cl_insn *ip, expressionS *p, bfd_reloc_code_real_type *r);
static void mips_no_prev_insn (void);
static void mips16_macro_build
  (expressionS *, const char *, const char *, va_list);
static void load_register (int, expressionS *, int);
static void macro_start (void);
static void macro_end (void);
static void macro (struct mips_cl_insn * ip);
static void mips16_macro (struct mips_cl_insn * ip);
#ifdef LOSING_COMPILER
static void macro2 (struct mips_cl_insn * ip);
#endif
static void mips_ip (char *str, struct mips_cl_insn * ip);
static void mips16_ip (char *str, struct mips_cl_insn * ip);
static void mips16_immed
  (char *, unsigned int, int, offsetT, bfd_boolean, bfd_boolean, bfd_boolean,
   unsigned long *, bfd_boolean *, unsigned short *);
static size_t my_getSmallExpression
  (expressionS *, bfd_reloc_code_real_type *, char *);
static void my_getExpression (expressionS *, char *);
static void s_align (int);
static void s_change_sec (int);
static void s_change_section (int);
static void s_cons (int);
static void s_float_cons (int);
static void s_mips_globl (int);
static void s_option (int);
static void s_mipsset (int);
static void s_abicalls (int);
static void s_cpload (int);
static void s_cpsetup (int);
static void s_cplocal (int);
static void s_cprestore (int);
static void s_cpreturn (int);
static void s_dtprelword (int);
static void s_dtpreldword (int);
static void s_gpvalue (int);
static void s_gpword (int);
static void s_gpdword (int);
static void s_cpadd (int);
static void s_insn (int);
static void md_obj_begin (void);
static void md_obj_end (void);
static void s_mips_ent (int);
static void s_mips_end (int);
static void s_mips_frame (int);
static void s_mips_mask (int reg_type);
static void s_mips_stab (int);
static void s_mips_weakext (int);
static void s_mips_file (int);
static void s_mips_loc (int);
static bfd_boolean pic_need_relax (symbolS *, asection *);
static int relaxed_branch_length (fragS *, asection *, int);
static int validate_mips_insn (const struct mips_opcode *);

/* Table and functions used to map between CPU/ISA names, and
   ISA levels, and CPU numbers.  */

struct mips_cpu_info
{
  const char *name;           /* CPU or ISA name.  */
  int flags;                  /* ASEs available, or ISA flag.  */
  int isa;                    /* ISA level.  */
  int cpu;                    /* CPU number (default CPU if ISA).  */
};

#define MIPS_CPU_IS_ISA		0x0001	/* Is this an ISA?  (If 0, a CPU.) */
#define MIPS_CPU_ASE_SMARTMIPS	0x0002	/* CPU implements SmartMIPS ASE */
#define MIPS_CPU_ASE_DSP	0x0004	/* CPU implements DSP ASE */
#define MIPS_CPU_ASE_MT		0x0008	/* CPU implements MT ASE */
#define MIPS_CPU_ASE_MIPS3D	0x0010	/* CPU implements MIPS-3D ASE */
#define MIPS_CPU_ASE_MDMX	0x0020	/* CPU implements MDMX ASE */
#define MIPS_CPU_ASE_DSPR2	0x0040	/* CPU implements DSP R2 ASE */

static const struct mips_cpu_info *mips_parse_cpu (const char *, const char *);
static const struct mips_cpu_info *mips_cpu_info_from_isa (int);
static const struct mips_cpu_info *mips_cpu_info_from_arch (int);

/* Pseudo-op table.

   The following pseudo-ops from the Kane and Heinrich MIPS book
   should be defined here, but are currently unsupported: .alias,
   .galive, .gjaldef, .gjrlive, .livereg, .noalias.

   The following pseudo-ops from the Kane and Heinrich MIPS book are
   specific to the type of debugging information being generated, and
   should be defined by the object format: .aent, .begin, .bend,
   .bgnb, .end, .endb, .ent, .fmask, .frame, .loc, .mask, .verstamp,
   .vreg.

   The following pseudo-ops from the Kane and Heinrich MIPS book are
   not MIPS CPU specific, but are also not specific to the object file
   format.  This file is probably the best place to define them, but
   they are not currently supported: .asm0, .endr, .lab, .struct.  */

static const pseudo_typeS mips_pseudo_table[] =
{
  /* MIPS specific pseudo-ops.  */
  {"option", s_option, 0},
  {"set", s_mipsset, 0},
  {"rdata", s_change_sec, 'r'},
  {"sdata", s_change_sec, 's'},
  {"livereg", s_ignore, 0},
  {"abicalls", s_abicalls, 0},
  {"cpload", s_cpload, 0},
  {"cpsetup", s_cpsetup, 0},
  {"cplocal", s_cplocal, 0},
  {"cprestore", s_cprestore, 0},
  {"cpreturn", s_cpreturn, 0},
  {"dtprelword", s_dtprelword, 0},
  {"dtpreldword", s_dtpreldword, 0},
  {"gpvalue", s_gpvalue, 0},
  {"gpword", s_gpword, 0},
  {"gpdword", s_gpdword, 0},
  {"cpadd", s_cpadd, 0},
  {"insn", s_insn, 0},

  /* Relatively generic pseudo-ops that happen to be used on MIPS
     chips.  */
  {"asciiz", stringer, 1},
  {"bss", s_change_sec, 'b'},
  {"err", s_err, 0},
  {"half", s_cons, 1},
  {"dword", s_cons, 3},
  {"weakext", s_mips_weakext, 0},
  {"origin", s_org, 0},
  {"repeat", s_rept, 0},

  /* These pseudo-ops are defined in read.c, but must be overridden
     here for one reason or another.  */
  {"align", s_align, 0},
  {"byte", s_cons, 0},
  {"data", s_change_sec, 'd'},
  {"double", s_float_cons, 'd'},
  {"float", s_float_cons, 'f'},
  {"globl", s_mips_globl, 0},
  {"global", s_mips_globl, 0},
  {"hword", s_cons, 1},
  {"int", s_cons, 2},
  {"long", s_cons, 2},
  {"octa", s_cons, 4},
  {"quad", s_cons, 3},
  {"section", s_change_section, 0},
  {"short", s_cons, 1},
  {"single", s_float_cons, 'f'},
  {"stabn", s_mips_stab, 'n'},
  {"text", s_change_sec, 't'},
  {"word", s_cons, 2},

  { "extern", ecoff_directive_extern, 0},

  { NULL, NULL, 0 },
};

static const pseudo_typeS mips_nonecoff_pseudo_table[] =
{
  /* These pseudo-ops should be defined by the object file format.
     However, a.out doesn't support them, so we have versions here.  */
  {"aent", s_mips_ent, 1},
  {"bgnb", s_ignore, 0},
  {"end", s_mips_end, 0},
  {"endb", s_ignore, 0},
  {"ent", s_mips_ent, 0},
  {"file", s_mips_file, 0},
  {"fmask", s_mips_mask, 'F'},
  {"frame", s_mips_frame, 0},
  {"loc", s_mips_loc, 0},
  {"mask", s_mips_mask, 'R'},
  {"verstamp", s_ignore, 0},
  { NULL, NULL, 0 },
};

extern void pop_insert (const pseudo_typeS *);

void
mips_pop_insert (void)
{
  pop_insert (mips_pseudo_table);
  if (! ECOFF_DEBUGGING)
    pop_insert (mips_nonecoff_pseudo_table);
}

/* Symbols labelling the current insn.  */

struct insn_label_list
{
  struct insn_label_list *next;
  symbolS *label;
};

static struct insn_label_list *free_insn_labels;
#define label_list tc_segment_info_data

static void mips_clear_insn_labels (void);

static inline void
mips_clear_insn_labels (void)
{
  register struct insn_label_list **pl;
  segment_info_type *si;

  if (now_seg)
    {
      for (pl = &free_insn_labels; *pl != NULL; pl = &(*pl)->next)
	;
      
      si = seg_info (now_seg);
      *pl = si->label_list;
      si->label_list = NULL;
    }
}


static char *expr_end;

/* Expressions which appear in instructions.  These are set by
   mips_ip.  */

static expressionS imm_expr;
static expressionS imm2_expr;
static expressionS offset_expr;

/* Relocs associated with imm_expr and offset_expr.  */

static bfd_reloc_code_real_type imm_reloc[3]
  = {BFD_RELOC_UNUSED, BFD_RELOC_UNUSED, BFD_RELOC_UNUSED};
static bfd_reloc_code_real_type offset_reloc[3]
  = {BFD_RELOC_UNUSED, BFD_RELOC_UNUSED, BFD_RELOC_UNUSED};

/* These are set by mips16_ip if an explicit extension is used.  */

static bfd_boolean mips16_small, mips16_ext;

#ifdef OBJ_ELF
/* The pdr segment for per procedure frame/regmask info.  Not used for
   ECOFF debugging.  */

static segT pdr_seg;
#endif

/* The default target format to use.  */

const char *
mips_target_format (void)
{
  switch (OUTPUT_FLAVOR)
    {
    case bfd_target_ecoff_flavour:
      return target_big_endian ? "ecoff-bigmips" : ECOFF_LITTLE_FORMAT;
    case bfd_target_coff_flavour:
      return "pe-mips";
    case bfd_target_elf_flavour:
#ifdef TE_VXWORKS
      if (!HAVE_64BIT_OBJECTS && !HAVE_NEWABI)
	return (target_big_endian
		? "elf32-bigmips-vxworks"
		: "elf32-littlemips-vxworks");
#endif
#ifdef TE_TMIPS
      /* This is traditional mips.  */
      return (target_big_endian
	      ? (HAVE_64BIT_OBJECTS
		 ? "elf64-tradbigmips"
		 : (HAVE_NEWABI
		    ? "elf32-ntradbigmips" : "elf32-tradbigmips"))
	      : (HAVE_64BIT_OBJECTS
		 ? "elf64-tradlittlemips"
		 : (HAVE_NEWABI
		    ? "elf32-ntradlittlemips" : "elf32-tradlittlemips")));
#else
      return (target_big_endian
	      ? (HAVE_64BIT_OBJECTS
		 ? "elf64-bigmips"
		 : (HAVE_NEWABI
		    ? "elf32-nbigmips" : "elf32-bigmips"))
	      : (HAVE_64BIT_OBJECTS
		 ? "elf64-littlemips"
		 : (HAVE_NEWABI
		    ? "elf32-nlittlemips" : "elf32-littlemips")));
#endif
    default:
      abort ();
      return NULL;
    }
}

/* Return the length of instruction INSN.  */

static inline unsigned int
insn_length (const struct mips_cl_insn *insn)
{
  if (!mips_opts.mips16)
    return 4;
  return insn->mips16_absolute_jump_p || insn->use_extend ? 4 : 2;
}

/* Initialise INSN from opcode entry MO.  Leave its position unspecified.  */

static void
create_insn (struct mips_cl_insn *insn, const struct mips_opcode *mo)
{
  size_t i;

  insn->insn_mo = mo;
  insn->use_extend = FALSE;
  insn->extend = 0;
  insn->insn_opcode = mo->match;
  insn->frag = NULL;
  insn->where = 0;
  for (i = 0; i < ARRAY_SIZE (insn->fixp); i++)
    insn->fixp[i] = NULL;
  insn->fixed_p = (mips_opts.noreorder > 0);
  insn->noreorder_p = (mips_opts.noreorder > 0);
  insn->mips16_absolute_jump_p = 0;
}

/* Install INSN at the location specified by its "frag" and "where" fields.  */

static void
install_insn (const struct mips_cl_insn *insn)
{
  char *f = insn->frag->fr_literal + insn->where;
  if (!mips_opts.mips16)
    md_number_to_chars (f, insn->insn_opcode, 4);
  else if (insn->mips16_absolute_jump_p)
    {
      md_number_to_chars (f, insn->insn_opcode >> 16, 2);
      md_number_to_chars (f + 2, insn->insn_opcode & 0xffff, 2);
    }
  else
    {
      if (insn->use_extend)
	{
	  md_number_to_chars (f, 0xf000 | insn->extend, 2);
	  f += 2;
	}
      md_number_to_chars (f, insn->insn_opcode, 2);
    }
}

/* Move INSN to offset WHERE in FRAG.  Adjust the fixups accordingly
   and install the opcode in the new location.  */

static void
move_insn (struct mips_cl_insn *insn, fragS *frag, long where)
{
  size_t i;

  insn->frag = frag;
  insn->where = where;
  for (i = 0; i < ARRAY_SIZE (insn->fixp); i++)
    if (insn->fixp[i] != NULL)
      {
	insn->fixp[i]->fx_frag = frag;
	insn->fixp[i]->fx_where = where;
      }
  install_insn (insn);
}

/* Add INSN to the end of the output.  */

static void
add_fixed_insn (struct mips_cl_insn *insn)
{
  char *f = frag_more (insn_length (insn));
  move_insn (insn, frag_now, f - frag_now->fr_literal);
}

/* Start a variant frag and move INSN to the start of the variant part,
   marking it as fixed.  The other arguments are as for frag_var.  */

static void
add_relaxed_insn (struct mips_cl_insn *insn, int max_chars, int var,
		  relax_substateT subtype, symbolS *symbol, offsetT offset)
{
  frag_grow (max_chars);
  move_insn (insn, frag_now, frag_more (0) - frag_now->fr_literal);
  insn->fixed_p = 1;
  frag_var (rs_machine_dependent, max_chars, var,
	    subtype, symbol, offset, NULL);
}

/* Insert N copies of INSN into the history buffer, starting at
   position FIRST.  Neither FIRST nor N need to be clipped.  */

static void
insert_into_history (unsigned int first, unsigned int n,
		     const struct mips_cl_insn *insn)
{
  if (mips_relax.sequence != 2)
    {
      unsigned int i;

      for (i = ARRAY_SIZE (history); i-- > first;)
	if (i >= first + n)
	  history[i] = history[i - n];
	else
	  history[i] = *insn;
    }
}

/* Emit a nop instruction, recording it in the history buffer.  */

static void
emit_nop (void)
{
  add_fixed_insn (NOP_INSN);
  insert_into_history (0, 1, NOP_INSN);
}

/* Initialize vr4120_conflicts.  There is a bit of duplication here:
   the idea is to make it obvious at a glance that each errata is
   included.  */

static void
init_vr4120_conflicts (void)
{
#define CONFLICT(FIRST, SECOND) \
    vr4120_conflicts[FIX_VR4120_##FIRST] |= 1 << FIX_VR4120_##SECOND

  /* Errata 21 - [D]DIV[U] after [D]MACC */
  CONFLICT (MACC, DIV);
  CONFLICT (DMACC, DIV);

  /* Errata 23 - Continuous DMULT[U]/DMACC instructions.  */
  CONFLICT (DMULT, DMULT);
  CONFLICT (DMULT, DMACC);
  CONFLICT (DMACC, DMULT);
  CONFLICT (DMACC, DMACC);

  /* Errata 24 - MT{LO,HI} after [D]MACC */
  CONFLICT (MACC, MTHILO);
  CONFLICT (DMACC, MTHILO);

  /* VR4181A errata MD(1): "If a MULT, MULTU, DMULT or DMULTU
     instruction is executed immediately after a MACC or DMACC
     instruction, the result of [either instruction] is incorrect."  */
  CONFLICT (MACC, MULT);
  CONFLICT (MACC, DMULT);
  CONFLICT (DMACC, MULT);
  CONFLICT (DMACC, DMULT);

  /* VR4181A errata MD(4): "If a MACC or DMACC instruction is
     executed immediately after a DMULT, DMULTU, DIV, DIVU,
     DDIV or DDIVU instruction, the result of the MACC or
     DMACC instruction is incorrect.".  */
  CONFLICT (DMULT, MACC);
  CONFLICT (DMULT, DMACC);
  CONFLICT (DIV, MACC);
  CONFLICT (DIV, DMACC);

#undef CONFLICT
}

struct regname {
  const char *name;
  unsigned int num;
};

#define RTYPE_MASK	0x1ff00
#define RTYPE_NUM	0x00100
#define RTYPE_FPU	0x00200
#define RTYPE_FCC	0x00400
#define RTYPE_VEC	0x00800
#define RTYPE_GP	0x01000
#define RTYPE_CP0	0x02000
#define RTYPE_PC	0x04000
#define RTYPE_ACC	0x08000
#define RTYPE_CCC	0x10000
#define RNUM_MASK	0x000ff
#define RWARN		0x80000

#define GENERIC_REGISTER_NUMBERS \
    {"$0",	RTYPE_NUM | 0},  \
    {"$1",	RTYPE_NUM | 1},  \
    {"$2",	RTYPE_NUM | 2},  \
    {"$3",	RTYPE_NUM | 3},  \
    {"$4",	RTYPE_NUM | 4},  \
    {"$5",	RTYPE_NUM | 5},  \
    {"$6",	RTYPE_NUM | 6},  \
    {"$7",	RTYPE_NUM | 7},  \
    {"$8",	RTYPE_NUM | 8},  \
    {"$9",	RTYPE_NUM | 9},  \
    {"$10",	RTYPE_NUM | 10}, \
    {"$11",	RTYPE_NUM | 11}, \
    {"$12",	RTYPE_NUM | 12}, \
    {"$13",	RTYPE_NUM | 13}, \
    {"$14",	RTYPE_NUM | 14}, \
    {"$15",	RTYPE_NUM | 15}, \
    {"$16",	RTYPE_NUM | 16}, \
    {"$17",	RTYPE_NUM | 17}, \
    {"$18",	RTYPE_NUM | 18}, \
    {"$19",	RTYPE_NUM | 19}, \
    {"$20",	RTYPE_NUM | 20}, \
    {"$21",	RTYPE_NUM | 21}, \
    {"$22",	RTYPE_NUM | 22}, \
    {"$23",	RTYPE_NUM | 23}, \
    {"$24",	RTYPE_NUM | 24}, \
    {"$25",	RTYPE_NUM | 25}, \
    {"$26",	RTYPE_NUM | 26}, \
    {"$27",	RTYPE_NUM | 27}, \
    {"$28",	RTYPE_NUM | 28}, \
    {"$29",	RTYPE_NUM | 29}, \
    {"$30",	RTYPE_NUM | 30}, \
    {"$31",	RTYPE_NUM | 31} 

#define FPU_REGISTER_NAMES       \
    {"$f0",	RTYPE_FPU | 0},  \
    {"$f1",	RTYPE_FPU | 1},  \
    {"$f2",	RTYPE_FPU | 2},  \
    {"$f3",	RTYPE_FPU | 3},  \
    {"$f4",	RTYPE_FPU | 4},  \
    {"$f5",	RTYPE_FPU | 5},  \
    {"$f6",	RTYPE_FPU | 6},  \
    {"$f7",	RTYPE_FPU | 7},  \
    {"$f8",	RTYPE_FPU | 8},  \
    {"$f9",	RTYPE_FPU | 9},  \
    {"$f10",	RTYPE_FPU | 10}, \
    {"$f11",	RTYPE_FPU | 11}, \
    {"$f12",	RTYPE_FPU | 12}, \
    {"$f13",	RTYPE_FPU | 13}, \
    {"$f14",	RTYPE_FPU | 14}, \
    {"$f15",	RTYPE_FPU | 15}, \
    {"$f16",	RTYPE_FPU | 16}, \
    {"$f17",	RTYPE_FPU | 17}, \
    {"$f18",	RTYPE_FPU | 18}, \
    {"$f19",	RTYPE_FPU | 19}, \
    {"$f20",	RTYPE_FPU | 20}, \
    {"$f21",	RTYPE_FPU | 21}, \
    {"$f22",	RTYPE_FPU | 22}, \
    {"$f23",	RTYPE_FPU | 23}, \
    {"$f24",	RTYPE_FPU | 24}, \
    {"$f25",	RTYPE_FPU | 25}, \
    {"$f26",	RTYPE_FPU | 26}, \
    {"$f27",	RTYPE_FPU | 27}, \
    {"$f28",	RTYPE_FPU | 28}, \
    {"$f29",	RTYPE_FPU | 29}, \
    {"$f30",	RTYPE_FPU | 30}, \
    {"$f31",	RTYPE_FPU | 31}

#define FPU_CONDITION_CODE_NAMES \
    {"$fcc0",	RTYPE_FCC | 0},  \
    {"$fcc1",	RTYPE_FCC | 1},  \
    {"$fcc2",	RTYPE_FCC | 2},  \
    {"$fcc3",	RTYPE_FCC | 3},  \
    {"$fcc4",	RTYPE_FCC | 4},  \
    {"$fcc5",	RTYPE_FCC | 5},  \
    {"$fcc6",	RTYPE_FCC | 6},  \
    {"$fcc7",	RTYPE_FCC | 7}

#define COPROC_CONDITION_CODE_NAMES         \
    {"$cc0",	RTYPE_FCC | RTYPE_CCC | 0}, \
    {"$cc1",	RTYPE_FCC | RTYPE_CCC | 1}, \
    {"$cc2",	RTYPE_FCC | RTYPE_CCC | 2}, \
    {"$cc3",	RTYPE_FCC | RTYPE_CCC | 3}, \
    {"$cc4",	RTYPE_FCC | RTYPE_CCC | 4}, \
    {"$cc5",	RTYPE_FCC | RTYPE_CCC | 5}, \
    {"$cc6",	RTYPE_FCC | RTYPE_CCC | 6}, \
    {"$cc7",	RTYPE_FCC | RTYPE_CCC | 7}

#define N32N64_SYMBOLIC_REGISTER_NAMES \
    {"$a4",	RTYPE_GP | 8},  \
    {"$a5",	RTYPE_GP | 9},  \
    {"$a6",	RTYPE_GP | 10}, \
    {"$a7",	RTYPE_GP | 11}, \
    {"$ta0",	RTYPE_GP | 8},  /* alias for $a4 */ \
    {"$ta1",	RTYPE_GP | 9},  /* alias for $a5 */ \
    {"$ta2",	RTYPE_GP | 10}, /* alias for $a6 */ \
    {"$ta3",	RTYPE_GP | 11}, /* alias for $a7 */ \
    {"$t0",	RTYPE_GP | 12}, \
    {"$t1",	RTYPE_GP | 13}, \
    {"$t2",	RTYPE_GP | 14}, \
    {"$t3",	RTYPE_GP | 15}

#define O32_SYMBOLIC_REGISTER_NAMES \
    {"$t0",	RTYPE_GP | 8},  \
    {"$t1",	RTYPE_GP | 9},  \
    {"$t2",	RTYPE_GP | 10}, \
    {"$t3",	RTYPE_GP | 11}, \
    {"$t4",	RTYPE_GP | 12}, \
    {"$t5",	RTYPE_GP | 13}, \
    {"$t6",	RTYPE_GP | 14}, \
    {"$t7",	RTYPE_GP | 15}, \
    {"$ta0",	RTYPE_GP | 12}, /* alias for $t4 */ \
    {"$ta1",	RTYPE_GP | 13}, /* alias for $t5 */ \
    {"$ta2",	RTYPE_GP | 14}, /* alias for $t6 */ \
    {"$ta3",	RTYPE_GP | 15}  /* alias for $t7 */ 

/* Remaining symbolic register names */
#define SYMBOLIC_REGISTER_NAMES \
    {"$zero",	RTYPE_GP | 0},  \
    {"$at",	RTYPE_GP | 1},  \
    {"$AT",	RTYPE_GP | 1},  \
    {"$v0",	RTYPE_GP | 2},  \
    {"$v1",	RTYPE_GP | 3},  \
    {"$a0",	RTYPE_GP | 4},  \
    {"$a1",	RTYPE_GP | 5},  \
    {"$a2",	RTYPE_GP | 6},  \
    {"$a3",	RTYPE_GP | 7},  \
    {"$s0",	RTYPE_GP | 16}, \
    {"$s1",	RTYPE_GP | 17}, \
    {"$s2",	RTYPE_GP | 18}, \
    {"$s3",	RTYPE_GP | 19}, \
    {"$s4",	RTYPE_GP | 20}, \
    {"$s5",	RTYPE_GP | 21}, \
    {"$s6",	RTYPE_GP | 22}, \
    {"$s7",	RTYPE_GP | 23}, \
    {"$t8",	RTYPE_GP | 24}, \
    {"$t9",	RTYPE_GP | 25}, \
    {"$k0",	RTYPE_GP | 26}, \
    {"$kt0",	RTYPE_GP | 26}, \
    {"$k1",	RTYPE_GP | 27}, \
    {"$kt1",	RTYPE_GP | 27}, \
    {"$gp",	RTYPE_GP | 28}, \
    {"$sp",	RTYPE_GP | 29}, \
    {"$s8",	RTYPE_GP | 30}, \
    {"$fp",	RTYPE_GP | 30}, \
    {"$ra",	RTYPE_GP | 31}

#define MIPS16_SPECIAL_REGISTER_NAMES \
    {"$pc",	RTYPE_PC | 0}

#define MDMX_VECTOR_REGISTER_NAMES \
    /* {"$v0",	RTYPE_VEC | 0},  clash with REG 2 above */ \
    /* {"$v1",	RTYPE_VEC | 1},  clash with REG 3 above */ \
    {"$v2",	RTYPE_VEC | 2},  \
    {"$v3",	RTYPE_VEC | 3},  \
    {"$v4",	RTYPE_VEC | 4},  \
    {"$v5",	RTYPE_VEC | 5},  \
    {"$v6",	RTYPE_VEC | 6},  \
    {"$v7",	RTYPE_VEC | 7},  \
    {"$v8",	RTYPE_VEC | 8},  \
    {"$v9",	RTYPE_VEC | 9},  \
    {"$v10",	RTYPE_VEC | 10}, \
    {"$v11",	RTYPE_VEC | 11}, \
    {"$v12",	RTYPE_VEC | 12}, \
    {"$v13",	RTYPE_VEC | 13}, \
    {"$v14",	RTYPE_VEC | 14}, \
    {"$v15",	RTYPE_VEC | 15}, \
    {"$v16",	RTYPE_VEC | 16}, \
    {"$v17",	RTYPE_VEC | 17}, \
    {"$v18",	RTYPE_VEC | 18}, \
    {"$v19",	RTYPE_VEC | 19}, \
    {"$v20",	RTYPE_VEC | 20}, \
    {"$v21",	RTYPE_VEC | 21}, \
    {"$v22",	RTYPE_VEC | 22}, \
    {"$v23",	RTYPE_VEC | 23}, \
    {"$v24",	RTYPE_VEC | 24}, \
    {"$v25",	RTYPE_VEC | 25}, \
    {"$v26",	RTYPE_VEC | 26}, \
    {"$v27",	RTYPE_VEC | 27}, \
    {"$v28",	RTYPE_VEC | 28}, \
    {"$v29",	RTYPE_VEC | 29}, \
    {"$v30",	RTYPE_VEC | 30}, \
    {"$v31",	RTYPE_VEC | 31}

#define MIPS_DSP_ACCUMULATOR_NAMES \
    {"$ac0",	RTYPE_ACC | 0}, \
    {"$ac1",	RTYPE_ACC | 1}, \
    {"$ac2",	RTYPE_ACC | 2}, \
    {"$ac3",	RTYPE_ACC | 3}

static const struct regname reg_names[] = {
  GENERIC_REGISTER_NUMBERS,
  FPU_REGISTER_NAMES,
  FPU_CONDITION_CODE_NAMES,
  COPROC_CONDITION_CODE_NAMES,

  /* The $txx registers depends on the abi,
     these will be added later into the symbol table from
     one of the tables below once mips_abi is set after 
     parsing of arguments from the command line. */
  SYMBOLIC_REGISTER_NAMES,

  MIPS16_SPECIAL_REGISTER_NAMES,
  MDMX_VECTOR_REGISTER_NAMES,
  MIPS_DSP_ACCUMULATOR_NAMES,
  {0, 0}
};

static const struct regname reg_names_o32[] = {
  O32_SYMBOLIC_REGISTER_NAMES,
  {0, 0}
};

static const struct regname reg_names_n32n64[] = {
  N32N64_SYMBOLIC_REGISTER_NAMES,
  {0, 0}
};

static int
reg_lookup (char **s, unsigned int types, unsigned int *regnop)
{
  symbolS *symbolP;
  char *e;
  char save_c;
  int reg = -1;

  /* Find end of name.  */
  e = *s;
  if (is_name_beginner (*e))
    ++e;
  while (is_part_of_name (*e))
    ++e;

  /* Terminate name.  */
  save_c = *e;
  *e = '\0';

  /* Look for a register symbol.  */
  if ((symbolP = symbol_find (*s)) && S_GET_SEGMENT (symbolP) == reg_section)
    {
      int r = S_GET_VALUE (symbolP);
      if (r & types)
	reg = r & RNUM_MASK;
      else if ((types & RTYPE_VEC) && (r & ~1) == (RTYPE_GP | 2))
	/* Convert GP reg $v0/1 to MDMX reg $v0/1!  */
	reg = (r & RNUM_MASK) - 2;
    }
  /* Else see if this is a register defined in an itbl entry.  */
  else if ((types & RTYPE_GP) && itbl_have_entries)
    {
      char *n = *s;
      unsigned long r;

      if (*n == '$')
	++n;
      if (itbl_get_reg_val (n, &r))
	reg = r & RNUM_MASK;
    }

  /* Advance to next token if a register was recognised.  */
  if (reg >= 0)
    *s = e;
  else if (types & RWARN)
    as_warn ("Unrecognized register name `%s'", *s);

  *e = save_c;
  if (regnop)
    *regnop = reg;
  return reg >= 0;
}

/* This function is called once, at assembler startup time.  It should set up
   all the tables, etc. that the MD part of the assembler will need.  */

void
md_begin (void)
{
  const char *retval = NULL;
  int i = 0;
  int broken = 0;

  if (mips_pic != NO_PIC)
    {
      if (g_switch_seen && g_switch_value != 0)
	as_bad (_("-G may not be used in position-independent code"));
      g_switch_value = 0;
    }

  if (! bfd_set_arch_mach (stdoutput, bfd_arch_mips, file_mips_arch))
    as_warn (_("Could not set architecture and machine"));

  op_hash = hash_new ();

  for (i = 0; i < NUMOPCODES;)
    {
      const char *name = mips_opcodes[i].name;

      retval = hash_insert (op_hash, name, (void *) &mips_opcodes[i]);
      if (retval != NULL)
	{
	  fprintf (stderr, _("internal error: can't hash `%s': %s\n"),
		   mips_opcodes[i].name, retval);
	  /* Probably a memory allocation problem?  Give up now.  */
	  as_fatal (_("Broken assembler.  No assembly attempted."));
	}
      do
	{
	  if (mips_opcodes[i].pinfo != INSN_MACRO)
	    {
	      if (!validate_mips_insn (&mips_opcodes[i]))
		broken = 1;
	      if (nop_insn.insn_mo == NULL && strcmp (name, "nop") == 0)
		{
		  create_insn (&nop_insn, mips_opcodes + i);
		  nop_insn.fixed_p = 1;
		}
	    }
	  ++i;
	}
      while ((i < NUMOPCODES) && !strcmp (mips_opcodes[i].name, name));
    }

  mips16_op_hash = hash_new ();

  i = 0;
  while (i < bfd_mips16_num_opcodes)
    {
      const char *name = mips16_opcodes[i].name;

      retval = hash_insert (mips16_op_hash, name, (void *) &mips16_opcodes[i]);
      if (retval != NULL)
	as_fatal (_("internal: can't hash `%s': %s"),
		  mips16_opcodes[i].name, retval);
      do
	{
	  if (mips16_opcodes[i].pinfo != INSN_MACRO
	      && ((mips16_opcodes[i].match & mips16_opcodes[i].mask)
		  != mips16_opcodes[i].match))
	    {
	      fprintf (stderr, _("internal error: bad mips16 opcode: %s %s\n"),
		       mips16_opcodes[i].name, mips16_opcodes[i].args);
	      broken = 1;
	    }
	  if (mips16_nop_insn.insn_mo == NULL && strcmp (name, "nop") == 0)
	    {
	      create_insn (&mips16_nop_insn, mips16_opcodes + i);
	      mips16_nop_insn.fixed_p = 1;
	    }
	  ++i;
	}
      while (i < bfd_mips16_num_opcodes
	     && strcmp (mips16_opcodes[i].name, name) == 0);
    }

  if (broken)
    as_fatal (_("Broken assembler.  No assembly attempted."));

  /* We add all the general register names to the symbol table.  This
     helps us detect invalid uses of them.  */
  for (i = 0; reg_names[i].name; i++) 
    symbol_table_insert (symbol_new (reg_names[i].name, reg_section,
				     reg_names[i].num, // & RNUM_MASK,
				     &zero_address_frag));
  if (HAVE_NEWABI)
    for (i = 0; reg_names_n32n64[i].name; i++) 
      symbol_table_insert (symbol_new (reg_names_n32n64[i].name, reg_section,
				       reg_names_n32n64[i].num, // & RNUM_MASK,
				       &zero_address_frag));
  else
    for (i = 0; reg_names_o32[i].name; i++) 
      symbol_table_insert (symbol_new (reg_names_o32[i].name, reg_section,
				       reg_names_o32[i].num, // & RNUM_MASK,
				       &zero_address_frag));

  mips_no_prev_insn ();

  mips_gprmask = 0;
  mips_cprmask[0] = 0;
  mips_cprmask[1] = 0;
  mips_cprmask[2] = 0;
  mips_cprmask[3] = 0;

  /* set the default alignment for the text section (2**2) */
  record_alignment (text_section, 2);

  bfd_set_gp_size (stdoutput, g_switch_value);

#ifdef OBJ_ELF
  if (IS_ELF)
    {
      /* On a native system other than VxWorks, sections must be aligned
	 to 16 byte boundaries.  When configured for an embedded ELF
	 target, we don't bother.  */
      if (strcmp (TARGET_OS, "elf") != 0
	  && strcmp (TARGET_OS, "vxworks") != 0)
	{
	  (void) bfd_set_section_alignment (stdoutput, text_section, 4);
	  (void) bfd_set_section_alignment (stdoutput, data_section, 4);
	  (void) bfd_set_section_alignment (stdoutput, bss_section, 4);
	}

      /* Create a .reginfo section for register masks and a .mdebug
	 section for debugging information.  */
      {
	segT seg;
	subsegT subseg;
	flagword flags;
	segT sec;

	seg = now_seg;
	subseg = now_subseg;

	/* The ABI says this section should be loaded so that the
	   running program can access it.  However, we don't load it
	   if we are configured for an embedded target */
	flags = SEC_READONLY | SEC_DATA;
	if (strcmp (TARGET_OS, "elf") != 0)
	  flags |= SEC_ALLOC | SEC_LOAD;

	if (mips_abi != N64_ABI)
	  {
	    sec = subseg_new (".reginfo", (subsegT) 0);

	    bfd_set_section_flags (stdoutput, sec, flags);
	    bfd_set_section_alignment (stdoutput, sec, HAVE_NEWABI ? 3 : 2);

	    mips_regmask_frag = frag_more (sizeof (Elf32_External_RegInfo));
	  }
	else
	  {
	    /* The 64-bit ABI uses a .MIPS.options section rather than
               .reginfo section.  */
	    sec = subseg_new (".MIPS.options", (subsegT) 0);
	    bfd_set_section_flags (stdoutput, sec, flags);
	    bfd_set_section_alignment (stdoutput, sec, 3);

	    /* Set up the option header.  */
	    {
	      Elf_Internal_Options opthdr;
	      char *f;

	      opthdr.kind = ODK_REGINFO;
	      opthdr.size = (sizeof (Elf_External_Options)
			     + sizeof (Elf64_External_RegInfo));
	      opthdr.section = 0;
	      opthdr.info = 0;
	      f = frag_more (sizeof (Elf_External_Options));
	      bfd_mips_elf_swap_options_out (stdoutput, &opthdr,
					     (Elf_External_Options *) f);

	      mips_regmask_frag = frag_more (sizeof (Elf64_External_RegInfo));
	    }
	  }

	if (ECOFF_DEBUGGING)
	  {
	    sec = subseg_new (".mdebug", (subsegT) 0);
	    (void) bfd_set_section_flags (stdoutput, sec,
					  SEC_HAS_CONTENTS | SEC_READONLY);
	    (void) bfd_set_section_alignment (stdoutput, sec, 2);
	  }
	else if (mips_flag_pdr)
	  {
	    pdr_seg = subseg_new (".pdr", (subsegT) 0);
	    (void) bfd_set_section_flags (stdoutput, pdr_seg,
					  SEC_READONLY | SEC_RELOC
					  | SEC_DEBUGGING);
	    (void) bfd_set_section_alignment (stdoutput, pdr_seg, 2);
	  }

	subseg_set (seg, subseg);
      }
    }
#endif /* OBJ_ELF */

  if (! ECOFF_DEBUGGING)
    md_obj_begin ();

  if (mips_fix_vr4120)
    init_vr4120_conflicts ();
}

void
md_mips_end (void)
{
  if (! ECOFF_DEBUGGING)
    md_obj_end ();
}

void
md_assemble (char *str)
{
  struct mips_cl_insn insn;
  bfd_reloc_code_real_type unused_reloc[3]
    = {BFD_RELOC_UNUSED, BFD_RELOC_UNUSED, BFD_RELOC_UNUSED};

  imm_expr.X_op = O_absent;
  imm2_expr.X_op = O_absent;
  offset_expr.X_op = O_absent;
  imm_reloc[0] = BFD_RELOC_UNUSED;
  imm_reloc[1] = BFD_RELOC_UNUSED;
  imm_reloc[2] = BFD_RELOC_UNUSED;
  offset_reloc[0] = BFD_RELOC_UNUSED;
  offset_reloc[1] = BFD_RELOC_UNUSED;
  offset_reloc[2] = BFD_RELOC_UNUSED;

  if (mips_opts.mips16)
    mips16_ip (str, &insn);
  else
    {
      mips_ip (str, &insn);
      DBG ((_("returned from mips_ip(%s) insn_opcode = 0x%x\n"),
	    str, insn.insn_opcode));
    }

  if (insn_error)
    {
      as_bad ("%s `%s'", insn_error, str);
      return;
    }

  if (insn.insn_mo->pinfo == INSN_MACRO)
    {
      macro_start ();
      if (mips_opts.mips16)
	mips16_macro (&insn);
      else
	macro (&insn);
      macro_end ();
    }
  else
    {
      if (imm_expr.X_op != O_absent)
	append_insn (&insn, &imm_expr, imm_reloc);
      else if (offset_expr.X_op != O_absent)
	append_insn (&insn, &offset_expr, offset_reloc);
      else
	append_insn (&insn, NULL, unused_reloc);
    }
}

/* Return true if the given relocation might need a matching %lo().
   This is only "might" because SVR4 R_MIPS_GOT16 relocations only
   need a matching %lo() when applied to local symbols.  */

static inline bfd_boolean
reloc_needs_lo_p (bfd_reloc_code_real_type reloc)
{
  return (HAVE_IN_PLACE_ADDENDS
	  && (reloc == BFD_RELOC_HI16_S
	      || reloc == BFD_RELOC_MIPS16_HI16_S
	      /* VxWorks R_MIPS_GOT16 relocs never need a matching %lo();
		 all GOT16 relocations evaluate to "G".  */
	      || (reloc == BFD_RELOC_MIPS_GOT16 && mips_pic != VXWORKS_PIC)));
}

/* Return true if the given fixup is followed by a matching R_MIPS_LO16
   relocation.  */

static inline bfd_boolean
fixup_has_matching_lo_p (fixS *fixp)
{
  return (fixp->fx_next != NULL
	  && (fixp->fx_next->fx_r_type == BFD_RELOC_LO16
	     || fixp->fx_next->fx_r_type == BFD_RELOC_MIPS16_LO16)
	  && fixp->fx_addsy == fixp->fx_next->fx_addsy
	  && fixp->fx_offset == fixp->fx_next->fx_offset);
}

/* See whether instruction IP reads register REG.  CLASS is the type
   of register.  */

static int
insn_uses_reg (const struct mips_cl_insn *ip, unsigned int reg,
	       enum mips_regclass class)
{
  if (class == MIPS16_REG)
    {
      assert (mips_opts.mips16);
      reg = mips16_to_32_reg_map[reg];
      class = MIPS_GR_REG;
    }

  /* Don't report on general register ZERO, since it never changes.  */
  if (class == MIPS_GR_REG && reg == ZERO)
    return 0;

  if (class == MIPS_FP_REG)
    {
      assert (! mips_opts.mips16);
      /* If we are called with either $f0 or $f1, we must check $f0.
	 This is not optimal, because it will introduce an unnecessary
	 NOP between "lwc1 $f0" and "swc1 $f1".  To fix this we would
	 need to distinguish reading both $f0 and $f1 or just one of
	 them.  Note that we don't have to check the other way,
	 because there is no instruction that sets both $f0 and $f1
	 and requires a delay.  */
      if ((ip->insn_mo->pinfo & INSN_READ_FPR_S)
	  && ((EXTRACT_OPERAND (FS, *ip) & ~(unsigned) 1)
	      == (reg &~ (unsigned) 1)))
	return 1;
      if ((ip->insn_mo->pinfo & INSN_READ_FPR_T)
	  && ((EXTRACT_OPERAND (FT, *ip) & ~(unsigned) 1)
	      == (reg &~ (unsigned) 1)))
	return 1;
    }
  else if (! mips_opts.mips16)
    {
      if ((ip->insn_mo->pinfo & INSN_READ_GPR_S)
	  && EXTRACT_OPERAND (RS, *ip) == reg)
	return 1;
      if ((ip->insn_mo->pinfo & INSN_READ_GPR_T)
	  && EXTRACT_OPERAND (RT, *ip) == reg)
	return 1;
    }
  else
    {
      if ((ip->insn_mo->pinfo & MIPS16_INSN_READ_X)
	  && mips16_to_32_reg_map[MIPS16_EXTRACT_OPERAND (RX, *ip)] == reg)
	return 1;
      if ((ip->insn_mo->pinfo & MIPS16_INSN_READ_Y)
	  && mips16_to_32_reg_map[MIPS16_EXTRACT_OPERAND (RY, *ip)] == reg)
	return 1;
      if ((ip->insn_mo->pinfo & MIPS16_INSN_READ_Z)
	  && (mips16_to_32_reg_map[MIPS16_EXTRACT_OPERAND (MOVE32Z, *ip)]
	      == reg))
	return 1;
      if ((ip->insn_mo->pinfo & MIPS16_INSN_READ_T) && reg == TREG)
	return 1;
      if ((ip->insn_mo->pinfo & MIPS16_INSN_READ_SP) && reg == SP)
	return 1;
      if ((ip->insn_mo->pinfo & MIPS16_INSN_READ_31) && reg == RA)
	return 1;
      if ((ip->insn_mo->pinfo & MIPS16_INSN_READ_GPR_X)
	  && MIPS16_EXTRACT_OPERAND (REGR32, *ip) == reg)
	return 1;
    }

  return 0;
}

/* This function returns true if modifying a register requires a
   delay.  */

static int
reg_needs_delay (unsigned int reg)
{
  unsigned long prev_pinfo;

  prev_pinfo = history[0].insn_mo->pinfo;
  if (! mips_opts.noreorder
      && (((prev_pinfo & INSN_LOAD_MEMORY_DELAY)
	   && ! gpr_interlocks)
	  || ((prev_pinfo & INSN_LOAD_COPROC_DELAY)
	      && ! cop_interlocks)))
    {
      /* A load from a coprocessor or from memory.  All load delays
	 delay the use of general register rt for one instruction.  */
      /* Itbl support may require additional care here.  */
      know (prev_pinfo & INSN_WRITE_GPR_T);
      if (reg == EXTRACT_OPERAND (RT, history[0]))
	return 1;
    }

  return 0;
}

/* Move all labels in insn_labels to the current insertion point.  */

static void
mips_move_labels (void)
{
  segment_info_type *si = seg_info (now_seg);
  struct insn_label_list *l;
  valueT val;

  for (l = si->label_list; l != NULL; l = l->next)
    {
      assert (S_GET_SEGMENT (l->label) == now_seg);
      symbol_set_frag (l->label, frag_now);
      val = (valueT) frag_now_fix ();
      /* mips16 text labels are stored as odd.  */
      if (mips_opts.mips16)
	++val;
      S_SET_VALUE (l->label, val);
    }
}

static bfd_boolean
s_is_linkonce (symbolS *sym, segT from_seg)
{
  bfd_boolean linkonce = FALSE;
  segT symseg = S_GET_SEGMENT (sym);

  if (symseg != from_seg && !S_IS_LOCAL (sym))
    {
      if ((bfd_get_section_flags (stdoutput, symseg) & SEC_LINK_ONCE))
	linkonce = TRUE;
#ifdef OBJ_ELF
      /* The GNU toolchain uses an extension for ELF: a section
	 beginning with the magic string .gnu.linkonce is a
	 linkonce section.  */
      if (strncmp (segment_name (symseg), ".gnu.linkonce",
		   sizeof ".gnu.linkonce" - 1) == 0)
	linkonce = TRUE;
#endif
    }
  return linkonce;
}

/* Mark instruction labels in mips16 mode.  This permits the linker to
   handle them specially, such as generating jalx instructions when
   needed.  We also make them odd for the duration of the assembly, in
   order to generate the right sort of code.  We will make them even
   in the adjust_symtab routine, while leaving them marked.  This is
   convenient for the debugger and the disassembler.  The linker knows
   to make them odd again.  */

static void
mips16_mark_labels (void)
{
  segment_info_type *si = seg_info (now_seg);
  struct insn_label_list *l;

  if (!mips_opts.mips16)
    return;

  for (l = si->label_list; l != NULL; l = l->next)
   {
      symbolS *label = l->label;

#if defined(OBJ_ELF) || defined(OBJ_MAYBE_ELF)
      if (IS_ELF)
	S_SET_OTHER (label, STO_MIPS16);
#endif
      if ((S_GET_VALUE (label) & 1) == 0
	/* Don't adjust the address if the label is global or weak, or
	   in a link-once section, since we'll be emitting symbol reloc
	   references to it which will be patched up by the linker, and
	   the final value of the symbol may or may not be MIPS16.  */
	  && ! S_IS_WEAK (label)
	  && ! S_IS_EXTERNAL (label)
	  && ! s_is_linkonce (label, now_seg))
	S_SET_VALUE (label, S_GET_VALUE (label) | 1);
    }
}

/* End the current frag.  Make it a variant frag and record the
   relaxation info.  */

static void
relax_close_frag (void)
{
  mips_macro_warning.first_frag = frag_now;
  frag_var (rs_machine_dependent, 0, 0,
	    RELAX_ENCODE (mips_relax.sizes[0], mips_relax.sizes[1]),
	    mips_relax.symbol, 0, (char *) mips_relax.first_fixup);

  memset (&mips_relax.sizes, 0, sizeof (mips_relax.sizes));
  mips_relax.first_fixup = 0;
}

/* Start a new relaxation sequence whose expansion depends on SYMBOL.
   See the comment above RELAX_ENCODE for more details.  */

static void
relax_start (symbolS *symbol)
{
  assert (mips_relax.sequence == 0);
  mips_relax.sequence = 1;
  mips_relax.symbol = symbol;
}

/* Start generating the second version of a relaxable sequence.
   See the comment above RELAX_ENCODE for more details.  */

static void
relax_switch (void)
{
  assert (mips_relax.sequence == 1);
  mips_relax.sequence = 2;
}

/* End the current relaxable sequence.  */

static void
relax_end (void)
{
  assert (mips_relax.sequence == 2);
  relax_close_frag ();
  mips_relax.sequence = 0;
}

/* Classify an instruction according to the FIX_VR4120_* enumeration.
   Return NUM_FIX_VR4120_CLASSES if the instruction isn't affected
   by VR4120 errata.  */

static unsigned int
classify_vr4120_insn (const char *name)
{
  if (strncmp (name, "macc", 4) == 0)
    return FIX_VR4120_MACC;
  if (strncmp (name, "dmacc", 5) == 0)
    return FIX_VR4120_DMACC;
  if (strncmp (name, "mult", 4) == 0)
    return FIX_VR4120_MULT;
  if (strncmp (name, "dmult", 5) == 0)
    return FIX_VR4120_DMULT;
  if (strstr (name, "div"))
    return FIX_VR4120_DIV;
  if (strcmp (name, "mtlo") == 0 || strcmp (name, "mthi") == 0)
    return FIX_VR4120_MTHILO;
  return NUM_FIX_VR4120_CLASSES;
}

/* Return the number of instructions that must separate INSN1 and INSN2,
   where INSN1 is the earlier instruction.  Return the worst-case value
   for any INSN2 if INSN2 is null.  */

static unsigned int
insns_between (const struct mips_cl_insn *insn1,
	       const struct mips_cl_insn *insn2)
{
  unsigned long pinfo1, pinfo2;

  /* This function needs to know which pinfo flags are set for INSN2
     and which registers INSN2 uses.  The former is stored in PINFO2 and
     the latter is tested via INSN2_USES_REG.  If INSN2 is null, PINFO2
     will have every flag set and INSN2_USES_REG will always return true.  */
  pinfo1 = insn1->insn_mo->pinfo;
  pinfo2 = insn2 ? insn2->insn_mo->pinfo : ~0U;

#define INSN2_USES_REG(REG, CLASS) \
   (insn2 == NULL || insn_uses_reg (insn2, REG, CLASS))

  /* For most targets, write-after-read dependencies on the HI and LO
     registers must be separated by at least two instructions.  */
  if (!hilo_interlocks)
    {
      if ((pinfo1 & INSN_READ_LO) && (pinfo2 & INSN_WRITE_LO))
	return 2;
      if ((pinfo1 & INSN_READ_HI) && (pinfo2 & INSN_WRITE_HI))
	return 2;
    }

  /* If we're working around r7000 errata, there must be two instructions
     between an mfhi or mflo and any instruction that uses the result.  */
  if (mips_7000_hilo_fix
      && MF_HILO_INSN (pinfo1)
      && INSN2_USES_REG (EXTRACT_OPERAND (RD, *insn1), MIPS_GR_REG))
    return 2;

  /* If working around VR4120 errata, check for combinations that need
     a single intervening instruction.  */
  if (mips_fix_vr4120)
    {
      unsigned int class1, class2;

      class1 = classify_vr4120_insn (insn1->insn_mo->name);
      if (class1 != NUM_FIX_VR4120_CLASSES && vr4120_conflicts[class1] != 0)
	{
	  if (insn2 == NULL)
	    return 1;
	  class2 = classify_vr4120_insn (insn2->insn_mo->name);
	  if (vr4120_conflicts[class1] & (1 << class2))
	    return 1;
	}
    }

  if (!mips_opts.mips16)
    {
      /* Check for GPR or coprocessor load delays.  All such delays
	 are on the RT register.  */
      /* Itbl support may require additional care here.  */
      if ((!gpr_interlocks && (pinfo1 & INSN_LOAD_MEMORY_DELAY))
	  || (!cop_interlocks && (pinfo1 & INSN_LOAD_COPROC_DELAY)))
	{
	  know (pinfo1 & INSN_WRITE_GPR_T);
	  if (INSN2_USES_REG (EXTRACT_OPERAND (RT, *insn1), MIPS_GR_REG))
	    return 1;
	}

      /* Check for generic coprocessor hazards.

	 This case is not handled very well.  There is no special
	 knowledge of CP0 handling, and the coprocessors other than
	 the floating point unit are not distinguished at all.  */
      /* Itbl support may require additional care here. FIXME!
	 Need to modify this to include knowledge about
	 user specified delays!  */
      else if ((!cop_interlocks && (pinfo1 & INSN_COPROC_MOVE_DELAY))
	       || (!cop_mem_interlocks && (pinfo1 & INSN_COPROC_MEMORY_DELAY)))
	{
	  /* Handle cases where INSN1 writes to a known general coprocessor
	     register.  There must be a one instruction delay before INSN2
	     if INSN2 reads that register, otherwise no delay is needed.  */
	  if (pinfo1 & INSN_WRITE_FPR_T)
	    {
	      if (INSN2_USES_REG (EXTRACT_OPERAND (FT, *insn1), MIPS_FP_REG))
		return 1;
	    }
	  else if (pinfo1 & INSN_WRITE_FPR_S)
	    {
	      if (INSN2_USES_REG (EXTRACT_OPERAND (FS, *insn1), MIPS_FP_REG))
		return 1;
	    }
	  else
	    {
	      /* Read-after-write dependencies on the control registers
		 require a two-instruction gap.  */
	      if ((pinfo1 & INSN_WRITE_COND_CODE)
		  && (pinfo2 & INSN_READ_COND_CODE))
		return 2;

	      /* We don't know exactly what INSN1 does.  If INSN2 is
		 also a coprocessor instruction, assume there must be
		 a one instruction gap.  */
	      if (pinfo2 & INSN_COP)
		return 1;
	    }
	}

      /* Check for read-after-write dependencies on the coprocessor
	 control registers in cases where INSN1 does not need a general
	 coprocessor delay.  This means that INSN1 is a floating point
	 comparison instruction.  */
      /* Itbl support may require additional care here.  */
      else if (!cop_interlocks
	       && (pinfo1 & INSN_WRITE_COND_CODE)
	       && (pinfo2 & INSN_READ_COND_CODE))
	return 1;
    }

#undef INSN2_USES_REG

  return 0;
}

/* Return the number of nops that would be needed to work around the
   VR4130 mflo/mfhi errata if instruction INSN immediately followed
   the MAX_VR4130_NOPS instructions described by HISTORY.  */

static int
nops_for_vr4130 (const struct mips_cl_insn *history,
		 const struct mips_cl_insn *insn)
{
  int i, j, reg;

  /* Check if the instruction writes to HI or LO.  MTHI and MTLO
     are not affected by the errata.  */
  if (insn != 0
      && ((insn->insn_mo->pinfo & (INSN_WRITE_HI | INSN_WRITE_LO)) == 0
	  || strcmp (insn->insn_mo->name, "mtlo") == 0
	  || strcmp (insn->insn_mo->name, "mthi") == 0))
    return 0;

  /* Search for the first MFLO or MFHI.  */
  for (i = 0; i < MAX_VR4130_NOPS; i++)
    if (!history[i].noreorder_p && MF_HILO_INSN (history[i].insn_mo->pinfo))
      {
	/* Extract the destination register.  */
	if (mips_opts.mips16)
	  reg = mips16_to_32_reg_map[MIPS16_EXTRACT_OPERAND (RX, history[i])];
	else
	  reg = EXTRACT_OPERAND (RD, history[i]);

	/* No nops are needed if INSN reads that register.  */
	if (insn != NULL && insn_uses_reg (insn, reg, MIPS_GR_REG))
	  return 0;

	/* ...or if any of the intervening instructions do.  */
	for (j = 0; j < i; j++)
	  if (insn_uses_reg (&history[j], reg, MIPS_GR_REG))
	    return 0;

	return MAX_VR4130_NOPS - i;
      }
  return 0;
}

/* Return the number of nops that would be needed if instruction INSN
   immediately followed the MAX_NOPS instructions given by HISTORY,
   where HISTORY[0] is the most recent instruction.  If INSN is null,
   return the worse-case number of nops for any instruction.  */

static int
nops_for_insn (const struct mips_cl_insn *history,
	       const struct mips_cl_insn *insn)
{
  int i, nops, tmp_nops;

  nops = 0;
  for (i = 0; i < MAX_DELAY_NOPS; i++)
    if (!history[i].noreorder_p)
      {
	tmp_nops = insns_between (history + i, insn) - i;
	if (tmp_nops > nops)
	  nops = tmp_nops;
      }

  if (mips_fix_vr4130)
    {
      tmp_nops = nops_for_vr4130 (history, insn);
      if (tmp_nops > nops)
	nops = tmp_nops;
    }

  return nops;
}

/* The variable arguments provide NUM_INSNS extra instructions that
   might be added to HISTORY.  Return the largest number of nops that
   would be needed after the extended sequence.  */

static int
nops_for_sequence (int num_insns, const struct mips_cl_insn *history, ...)
{
  va_list args;
  struct mips_cl_insn buffer[MAX_NOPS];
  struct mips_cl_insn *cursor;
  int nops;

  va_start (args, history);
  cursor = buffer + num_insns;
  memcpy (cursor, history, (MAX_NOPS - num_insns) * sizeof (*cursor));
  while (cursor > buffer)
    *--cursor = *va_arg (args, const struct mips_cl_insn *);

  nops = nops_for_insn (buffer, NULL);
  va_end (args);
  return nops;
}

/* Like nops_for_insn, but if INSN is a branch, take into account the
   worst-case delay for the branch target.  */

static int
nops_for_insn_or_target (const struct mips_cl_insn *history,
			 const struct mips_cl_insn *insn)
{
  int nops, tmp_nops;

  nops = nops_for_insn (history, insn);
  if (insn->insn_mo->pinfo & (INSN_UNCOND_BRANCH_DELAY
			      | INSN_COND_BRANCH_DELAY
			      | INSN_COND_BRANCH_LIKELY))
    {
      tmp_nops = nops_for_sequence (2, history, insn, NOP_INSN);
      if (tmp_nops > nops)
	nops = tmp_nops;
    }
  else if (mips_opts.mips16 && (insn->insn_mo->pinfo & MIPS16_INSN_BRANCH))
    {
      tmp_nops = nops_for_sequence (1, history, insn);
      if (tmp_nops > nops)
	nops = tmp_nops;
    }
  return nops;
}

/* Output an instruction.  IP is the instruction information.
   ADDRESS_EXPR is an operand of the instruction to be used with
   RELOC_TYPE.  */

static void
append_insn (struct mips_cl_insn *ip, expressionS *address_expr,
	     bfd_reloc_code_real_type *reloc_type)
{
  unsigned long prev_pinfo, pinfo;
  relax_stateT prev_insn_frag_type = 0;
  bfd_boolean relaxed_branch = FALSE;
  segment_info_type *si = seg_info (now_seg);

  /* Mark instruction labels in mips16 mode.  */
  mips16_mark_labels ();

  prev_pinfo = history[0].insn_mo->pinfo;
  pinfo = ip->insn_mo->pinfo;

  if (mips_relax.sequence != 2 && !mips_opts.noreorder)
    {
      /* There are a lot of optimizations we could do that we don't.
	 In particular, we do not, in general, reorder instructions.
	 If you use gcc with optimization, it will reorder
	 instructions and generally do much more optimization then we
	 do here; repeating all that work in the assembler would only
	 benefit hand written assembly code, and does not seem worth
	 it.  */
      int nops = (mips_optimize == 0
		  ? nops_for_insn (history, NULL)
		  : nops_for_insn_or_target (history, ip));
      if (nops > 0)
	{
	  fragS *old_frag;
	  unsigned long old_frag_offset;
	  int i;

	  old_frag = frag_now;
	  old_frag_offset = frag_now_fix ();

	  for (i = 0; i < nops; i++)
	    emit_nop ();

	  if (listing)
	    {
	      listing_prev_line ();
	      /* We may be at the start of a variant frag.  In case we
                 are, make sure there is enough space for the frag
                 after the frags created by listing_prev_line.  The
                 argument to frag_grow here must be at least as large
                 as the argument to all other calls to frag_grow in
                 this file.  We don't have to worry about being in the
                 middle of a variant frag, because the variants insert
                 all needed nop instructions themselves.  */
	      frag_grow (40);
	    }

	  mips_move_labels ();

#ifndef NO_ECOFF_DEBUGGING
	  if (ECOFF_DEBUGGING)
	    ecoff_fix_loc (old_frag, old_frag_offset);
#endif
	}
    }
  else if (mips_relax.sequence != 2 && prev_nop_frag != NULL)
    {
      /* Work out how many nops in prev_nop_frag are needed by IP.  */
      int nops = nops_for_insn_or_target (history, ip);
      assert (nops <= prev_nop_frag_holds);

      /* Enforce NOPS as a minimum.  */
      if (nops > prev_nop_frag_required)
	prev_nop_frag_required = nops;

      if (prev_nop_frag_holds == prev_nop_frag_required)
	{
	  /* Settle for the current number of nops.  Update the history
	     accordingly (for the benefit of any future .set reorder code).  */
	  prev_nop_frag = NULL;
	  insert_into_history (prev_nop_frag_since,
			       prev_nop_frag_holds, NOP_INSN);
	}
      else
	{
	  /* Allow this instruction to replace one of the nops that was
	     tentatively added to prev_nop_frag.  */
	  prev_nop_frag->fr_fix -= mips_opts.mips16 ? 2 : 4;
	  prev_nop_frag_holds--;
	  prev_nop_frag_since++;
	}
    }

#ifdef OBJ_ELF
  /* The value passed to dwarf2_emit_insn is the distance between
     the beginning of the current instruction and the address that
     should be recorded in the debug tables.  For MIPS16 debug info
     we want to use ISA-encoded addresses, so we pass -1 for an
     address higher by one than the current.  */
  dwarf2_emit_insn (mips_opts.mips16 ? -1 : 0);
#endif

  /* Record the frag type before frag_var.  */
  if (history[0].frag)
    prev_insn_frag_type = history[0].frag->fr_type;

  if (address_expr
      && *reloc_type == BFD_RELOC_16_PCREL_S2
      && (pinfo & INSN_UNCOND_BRANCH_DELAY || pinfo & INSN_COND_BRANCH_DELAY
	  || pinfo & INSN_COND_BRANCH_LIKELY)
      && mips_relax_branch
      /* Don't try branch relaxation within .set nomacro, or within
	 .set noat if we use $at for PIC computations.  If it turns
	 out that the branch was out-of-range, we'll get an error.  */
      && !mips_opts.warn_about_macros
      && !(mips_opts.noat && mips_pic != NO_PIC)
      && !mips_opts.mips16)
    {
      relaxed_branch = TRUE;
      add_relaxed_insn (ip, (relaxed_branch_length
			     (NULL, NULL,
			      (pinfo & INSN_UNCOND_BRANCH_DELAY) ? -1
			      : (pinfo & INSN_COND_BRANCH_LIKELY) ? 1
			      : 0)), 4,
			RELAX_BRANCH_ENCODE
			(pinfo & INSN_UNCOND_BRANCH_DELAY,
			 pinfo & INSN_COND_BRANCH_LIKELY,
			 pinfo & INSN_WRITE_GPR_31,
			 0),
			address_expr->X_add_symbol,
			address_expr->X_add_number);
      *reloc_type = BFD_RELOC_UNUSED;
    }
  else if (*reloc_type > BFD_RELOC_UNUSED)
    {
      /* We need to set up a variant frag.  */
      assert (mips_opts.mips16 && address_expr != NULL);
      add_relaxed_insn (ip, 4, 0,
			RELAX_MIPS16_ENCODE
			(*reloc_type - BFD_RELOC_UNUSED,
			 mips16_small, mips16_ext,
			 prev_pinfo & INSN_UNCOND_BRANCH_DELAY,
			 history[0].mips16_absolute_jump_p),
			make_expr_symbol (address_expr), 0);
    }
  else if (mips_opts.mips16
	   && ! ip->use_extend
	   && *reloc_type != BFD_RELOC_MIPS16_JMP)
    {
      if ((pinfo & INSN_UNCOND_BRANCH_DELAY) == 0)
	/* Make sure there is enough room to swap this instruction with
	   a following jump instruction.  */
	frag_grow (6);
      add_fixed_insn (ip);
    }
  else
    {
      if (mips_opts.mips16
	  && mips_opts.noreorder
	  && (prev_pinfo & INSN_UNCOND_BRANCH_DELAY) != 0)
	as_warn (_("extended instruction in delay slot"));

      if (mips_relax.sequence)
	{
	  /* If we've reached the end of this frag, turn it into a variant
	     frag and record the information for the instructions we've
	     written so far.  */
	  if (frag_room () < 4)
	    relax_close_frag ();
	  mips_relax.sizes[mips_relax.sequence - 1] += 4;
	}

      if (mips_relax.sequence != 2)
	mips_macro_warning.sizes[0] += 4;
      if (mips_relax.sequence != 1)
	mips_macro_warning.sizes[1] += 4;

      if (mips_opts.mips16)
	{
	  ip->fixed_p = 1;
	  ip->mips16_absolute_jump_p = (*reloc_type == BFD_RELOC_MIPS16_JMP);
	}
      add_fixed_insn (ip);
    }

  if (address_expr != NULL && *reloc_type <= BFD_RELOC_UNUSED)
    {
      if (address_expr->X_op == O_constant)
	{
	  unsigned int tmp;

	  switch (*reloc_type)
	    {
	    case BFD_RELOC_32:
	      ip->insn_opcode |= address_expr->X_add_number;
	      break;

	    case BFD_RELOC_MIPS_HIGHEST:
	      tmp = (address_expr->X_add_number + 0x800080008000ull) >> 48;
	      ip->insn_opcode |= tmp & 0xffff;
	      break;

	    case BFD_RELOC_MIPS_HIGHER:
	      tmp = (address_expr->X_add_number + 0x80008000ull) >> 32;
	      ip->insn_opcode |= tmp & 0xffff;
	      break;

	    case BFD_RELOC_HI16_S:
	      tmp = (address_expr->X_add_number + 0x8000) >> 16;
	      ip->insn_opcode |= tmp & 0xffff;
	      break;

	    case BFD_RELOC_HI16:
	      ip->insn_opcode |= (address_expr->X_add_number >> 16) & 0xffff;
	      break;

	    case BFD_RELOC_UNUSED:
	    case BFD_RELOC_LO16:
	    case BFD_RELOC_MIPS_GOT_DISP:
	      ip->insn_opcode |= address_expr->X_add_number & 0xffff;
	      break;

	    case BFD_RELOC_MIPS_JMP:
	      if ((address_expr->X_add_number & 3) != 0)
		as_bad (_("jump to misaligned address (0x%lx)"),
			(unsigned long) address_expr->X_add_number);
	      ip->insn_opcode |= (address_expr->X_add_number >> 2) & 0x3ffffff;
	      break;

	    case BFD_RELOC_MIPS16_JMP:
	      if ((address_expr->X_add_number & 3) != 0)
		as_bad (_("jump to misaligned address (0x%lx)"),
			(unsigned long) address_expr->X_add_number);
	      ip->insn_opcode |=
		(((address_expr->X_add_number & 0x7c0000) << 3)
		 | ((address_expr->X_add_number & 0xf800000) >> 7)
		 | ((address_expr->X_add_number & 0x3fffc) >> 2));
	      break;

	    case BFD_RELOC_16_PCREL_S2:
	      if ((address_expr->X_add_number & 3) != 0)
		as_bad (_("branch to misaligned address (0x%lx)"),
			(unsigned long) address_expr->X_add_number);
	      if (mips_relax_branch)
		goto need_reloc;
	      if ((address_expr->X_add_number + 0x20000) & ~0x3ffff)
		as_bad (_("branch address range overflow (0x%lx)"),
			(unsigned long) address_expr->X_add_number);
	      ip->insn_opcode |= (address_expr->X_add_number >> 2) & 0xffff;
	      break;

	    default:
	      internalError ();
	    }
	}
      else if (*reloc_type < BFD_RELOC_UNUSED)
	need_reloc:
	{
	  reloc_howto_type *howto;
	  int i;

	  /* In a compound relocation, it is the final (outermost)
	     operator that determines the relocated field.  */
	  for (i = 1; i < 3; i++)
	    if (reloc_type[i] == BFD_RELOC_UNUSED)
	      break;

	  howto = bfd_reloc_type_lookup (stdoutput, reloc_type[i - 1]);
	  ip->fixp[0] = fix_new_exp (ip->frag, ip->where,
				     bfd_get_reloc_size (howto),
				     address_expr,
				     reloc_type[0] == BFD_RELOC_16_PCREL_S2,
				     reloc_type[0]);

	  /* Tag symbols that have a R_MIPS16_26 relocation against them.  */
	  if (reloc_type[0] == BFD_RELOC_MIPS16_JMP
	      && ip->fixp[0]->fx_addsy)
	    *symbol_get_tc (ip->fixp[0]->fx_addsy) = 1;

	  /* These relocations can have an addend that won't fit in
	     4 octets for 64bit assembly.  */
	  if (HAVE_64BIT_GPRS
	      && ! howto->partial_inplace
	      && (reloc_type[0] == BFD_RELOC_16
		  || reloc_type[0] == BFD_RELOC_32
		  || reloc_type[0] == BFD_RELOC_MIPS_JMP
		  || reloc_type[0] == BFD_RELOC_HI16_S
		  || reloc_type[0] == BFD_RELOC_LO16
		  || reloc_type[0] == BFD_RELOC_GPREL16
		  || reloc_type[0] == BFD_RELOC_MIPS_LITERAL
		  || reloc_type[0] == BFD_RELOC_GPREL32
		  || reloc_type[0] == BFD_RELOC_64
		  || reloc_type[0] == BFD_RELOC_CTOR
		  || reloc_type[0] == BFD_RELOC_MIPS_SUB
		  || reloc_type[0] == BFD_RELOC_MIPS_HIGHEST
		  || reloc_type[0] == BFD_RELOC_MIPS_HIGHER
		  || reloc_type[0] == BFD_RELOC_MIPS_SCN_DISP
		  || reloc_type[0] == BFD_RELOC_MIPS_REL16
		  || reloc_type[0] == BFD_RELOC_MIPS_RELGOT
		  || reloc_type[0] == BFD_RELOC_MIPS16_GPREL
		  || reloc_type[0] == BFD_RELOC_MIPS16_HI16_S
		  || reloc_type[0] == BFD_RELOC_MIPS16_LO16))
	    ip->fixp[0]->fx_no_overflow = 1;

	  if (mips_relax.sequence)
	    {
	      if (mips_relax.first_fixup == 0)
		mips_relax.first_fixup = ip->fixp[0];
	    }
	  else if (reloc_needs_lo_p (*reloc_type))
	    {
	      struct mips_hi_fixup *hi_fixup;

	      /* Reuse the last entry if it already has a matching %lo.  */
	      hi_fixup = mips_hi_fixup_list;
	      if (hi_fixup == 0
		  || !fixup_has_matching_lo_p (hi_fixup->fixp))
		{
		  hi_fixup = ((struct mips_hi_fixup *)
			      xmalloc (sizeof (struct mips_hi_fixup)));
		  hi_fixup->next = mips_hi_fixup_list;
		  mips_hi_fixup_list = hi_fixup;
		}
	      hi_fixup->fixp = ip->fixp[0];
	      hi_fixup->seg = now_seg;
	    }

	  /* Add fixups for the second and third relocations, if given.
	     Note that the ABI allows the second relocation to be
	     against RSS_UNDEF, RSS_GP, RSS_GP0 or RSS_LOC.  At the
	     moment we only use RSS_UNDEF, but we could add support
	     for the others if it ever becomes necessary.  */
	  for (i = 1; i < 3; i++)
	    if (reloc_type[i] != BFD_RELOC_UNUSED)
	      {
		ip->fixp[i] = fix_new (ip->frag, ip->where,
				       ip->fixp[0]->fx_size, NULL, 0,
				       FALSE, reloc_type[i]);

		/* Use fx_tcbit to mark compound relocs.  */
		ip->fixp[0]->fx_tcbit = 1;
		ip->fixp[i]->fx_tcbit = 1;
	      }
	}
    }
  install_insn (ip);

  /* Update the register mask information.  */
  if (! mips_opts.mips16)
    {
      if (pinfo & INSN_WRITE_GPR_D)
	mips_gprmask |= 1 << EXTRACT_OPERAND (RD, *ip);
      if ((pinfo & (INSN_WRITE_GPR_T | INSN_READ_GPR_T)) != 0)
	mips_gprmask |= 1 << EXTRACT_OPERAND (RT, *ip);
      if (pinfo & INSN_READ_GPR_S)
	mips_gprmask |= 1 << EXTRACT_OPERAND (RS, *ip);
      if (pinfo & INSN_WRITE_GPR_31)
	mips_gprmask |= 1 << RA;
      if (pinfo & INSN_WRITE_FPR_D)
	mips_cprmask[1] |= 1 << EXTRACT_OPERAND (FD, *ip);
      if ((pinfo & (INSN_WRITE_FPR_S | INSN_READ_FPR_S)) != 0)
	mips_cprmask[1] |= 1 << EXTRACT_OPERAND (FS, *ip);
      if ((pinfo & (INSN_WRITE_FPR_T | INSN_READ_FPR_T)) != 0)
	mips_cprmask[1] |= 1 << EXTRACT_OPERAND (FT, *ip);
      if ((pinfo & INSN_READ_FPR_R) != 0)
	mips_cprmask[1] |= 1 << EXTRACT_OPERAND (FR, *ip);
      if (pinfo & INSN_COP)
	{
	  /* We don't keep enough information to sort these cases out.
	     The itbl support does keep this information however, although
	     we currently don't support itbl fprmats as part of the cop
	     instruction.  May want to add this support in the future.  */
	}
      /* Never set the bit for $0, which is always zero.  */
      mips_gprmask &= ~(1 << 0);
    }
  else
    {
      if (pinfo & (MIPS16_INSN_WRITE_X | MIPS16_INSN_READ_X))
	mips_gprmask |= 1 << MIPS16_EXTRACT_OPERAND (RX, *ip);
      if (pinfo & (MIPS16_INSN_WRITE_Y | MIPS16_INSN_READ_Y))
	mips_gprmask |= 1 << MIPS16_EXTRACT_OPERAND (RY, *ip);
      if (pinfo & MIPS16_INSN_WRITE_Z)
	mips_gprmask |= 1 << MIPS16_EXTRACT_OPERAND (RZ, *ip);
      if (pinfo & (MIPS16_INSN_WRITE_T | MIPS16_INSN_READ_T))
	mips_gprmask |= 1 << TREG;
      if (pinfo & (MIPS16_INSN_WRITE_SP | MIPS16_INSN_READ_SP))
	mips_gprmask |= 1 << SP;
      if (pinfo & (MIPS16_INSN_WRITE_31 | MIPS16_INSN_READ_31))
	mips_gprmask |= 1 << RA;
      if (pinfo & MIPS16_INSN_WRITE_GPR_Y)
	mips_gprmask |= 1 << MIPS16OP_EXTRACT_REG32R (ip->insn_opcode);
      if (pinfo & MIPS16_INSN_READ_Z)
	mips_gprmask |= 1 << MIPS16_EXTRACT_OPERAND (MOVE32Z, *ip);
      if (pinfo & MIPS16_INSN_READ_GPR_X)
	mips_gprmask |= 1 << MIPS16_EXTRACT_OPERAND (REGR32, *ip);
    }

  if (mips_relax.sequence != 2 && !mips_opts.noreorder)
    {
      /* Filling the branch delay slot is more complex.  We try to
	 switch the branch with the previous instruction, which we can
	 do if the previous instruction does not set up a condition
	 that the branch tests and if the branch is not itself the
	 target of any branch.  */
      if ((pinfo & INSN_UNCOND_BRANCH_DELAY)
	  || (pinfo & INSN_COND_BRANCH_DELAY))
	{
	  if (mips_optimize < 2
	      /* If we have seen .set volatile or .set nomove, don't
		 optimize.  */
	      || mips_opts.nomove != 0
	      /* We can't swap if the previous instruction's position
		 is fixed.  */
	      || history[0].fixed_p
	      /* If the previous previous insn was in a .set
		 noreorder, we can't swap.  Actually, the MIPS
		 assembler will swap in this situation.  However, gcc
		 configured -with-gnu-as will generate code like
		   .set noreorder
		   lw	$4,XXX
		   .set	reorder
		   INSN
		   bne	$4,$0,foo
		 in which we can not swap the bne and INSN.  If gcc is
		 not configured -with-gnu-as, it does not output the
		 .set pseudo-ops.  */
	      || history[1].noreorder_p
	      /* If the branch is itself the target of a branch, we
		 can not swap.  We cheat on this; all we check for is
		 whether there is a label on this instruction.  If
		 there are any branches to anything other than a
		 label, users must use .set noreorder.  */
	      || si->label_list != NULL
	      /* If the previous instruction is in a variant frag
		 other than this branch's one, we cannot do the swap.
		 This does not apply to the mips16, which uses variant
		 frags for different purposes.  */
	      || (! mips_opts.mips16
		  && prev_insn_frag_type == rs_machine_dependent)
	      /* Check for conflicts between the branch and the instructions
		 before the candidate delay slot.  */
	      || nops_for_insn (history + 1, ip) > 0
	      /* Check for conflicts between the swapped sequence and the
		 target of the branch.  */
	      || nops_for_sequence (2, history + 1, ip, history) > 0
	      /* We do not swap with a trap instruction, since it
		 complicates trap handlers to have the trap
		 instruction be in a delay slot.  */
	      || (prev_pinfo & INSN_TRAP)
	      /* If the branch reads a register that the previous
		 instruction sets, we can not swap.  */
	      || (! mips_opts.mips16
		  && (prev_pinfo & INSN_WRITE_GPR_T)
		  && insn_uses_reg (ip, EXTRACT_OPERAND (RT, history[0]),
				    MIPS_GR_REG))
	      || (! mips_opts.mips16
		  && (prev_pinfo & INSN_WRITE_GPR_D)
		  && insn_uses_reg (ip, EXTRACT_OPERAND (RD, history[0]),
				    MIPS_GR_REG))
	      || (mips_opts.mips16
		  && (((prev_pinfo & MIPS16_INSN_WRITE_X)
		       && (insn_uses_reg
			   (ip, MIPS16_EXTRACT_OPERAND (RX, history[0]),
			    MIPS16_REG)))
		      || ((prev_pinfo & MIPS16_INSN_WRITE_Y)
			  && (insn_uses_reg
			      (ip, MIPS16_EXTRACT_OPERAND (RY, history[0]),
			       MIPS16_REG)))
		      || ((prev_pinfo & MIPS16_INSN_WRITE_Z)
			  && (insn_uses_reg
			      (ip, MIPS16_EXTRACT_OPERAND (RZ, history[0]),
			       MIPS16_REG)))
		      || ((prev_pinfo & MIPS16_INSN_WRITE_T)
			  && insn_uses_reg (ip, TREG, MIPS_GR_REG))
		      || ((prev_pinfo & MIPS16_INSN_WRITE_31)
			  && insn_uses_reg (ip, RA, MIPS_GR_REG))
		      || ((prev_pinfo & MIPS16_INSN_WRITE_GPR_Y)
			  && insn_uses_reg (ip,
					    MIPS16OP_EXTRACT_REG32R
					      (history[0].insn_opcode),
					    MIPS_GR_REG))))
	      /* If the branch writes a register that the previous
		 instruction sets, we can not swap (we know that
		 branches write only to RD or to $31).  */
	      || (! mips_opts.mips16
		  && (prev_pinfo & INSN_WRITE_GPR_T)
		  && (((pinfo & INSN_WRITE_GPR_D)
		       && (EXTRACT_OPERAND (RT, history[0])
			   == EXTRACT_OPERAND (RD, *ip)))
		      || ((pinfo & INSN_WRITE_GPR_31)
			  && EXTRACT_OPERAND (RT, history[0]) == RA)))
	      || (! mips_opts.mips16
		  && (prev_pinfo & INSN_WRITE_GPR_D)
		  && (((pinfo & INSN_WRITE_GPR_D)
		       && (EXTRACT_OPERAND (RD, history[0])
			   == EXTRACT_OPERAND (RD, *ip)))
		      || ((pinfo & INSN_WRITE_GPR_31)
			  && EXTRACT_OPERAND (RD, history[0]) == RA)))
	      || (mips_opts.mips16
		  && (pinfo & MIPS16_INSN_WRITE_31)
		  && ((prev_pinfo & MIPS16_INSN_WRITE_31)
		      || ((prev_pinfo & MIPS16_INSN_WRITE_GPR_Y)
			  && (MIPS16OP_EXTRACT_REG32R (history[0].insn_opcode)
			      == RA))))
	      /* If the branch writes a register that the previous
		 instruction reads, we can not swap (we know that
		 branches only write to RD or to $31).  */
	      || (! mips_opts.mips16
		  && (pinfo & INSN_WRITE_GPR_D)
		  && insn_uses_reg (&history[0],
				    EXTRACT_OPERAND (RD, *ip),
				    MIPS_GR_REG))
	      || (! mips_opts.mips16
		  && (pinfo & INSN_WRITE_GPR_31)
		  && insn_uses_reg (&history[0], RA, MIPS_GR_REG))
	      || (mips_opts.mips16
		  && (pinfo & MIPS16_INSN_WRITE_31)
		  && insn_uses_reg (&history[0], RA, MIPS_GR_REG))
	      /* If one instruction sets a condition code and the
                 other one uses a condition code, we can not swap.  */
	      || ((pinfo & INSN_READ_COND_CODE)
		  && (prev_pinfo & INSN_WRITE_COND_CODE))
	      || ((pinfo & INSN_WRITE_COND_CODE)
		  && (prev_pinfo & INSN_READ_COND_CODE))
	      /* If the previous instruction uses the PC, we can not
                 swap.  */
	      || (mips_opts.mips16
		  && (prev_pinfo & MIPS16_INSN_READ_PC))
	      /* If the previous instruction had a fixup in mips16
                 mode, we can not swap.  This normally means that the
                 previous instruction was a 4 byte branch anyhow.  */
	      || (mips_opts.mips16 && history[0].fixp[0])
	      /* If the previous instruction is a sync, sync.l, or
		 sync.p, we can not swap.  */
	      || (prev_pinfo & INSN_SYNC))
	    {
	      if (mips_opts.mips16
		  && (pinfo & INSN_UNCOND_BRANCH_DELAY)
		  && (pinfo & (MIPS16_INSN_READ_X | MIPS16_INSN_READ_31))
		  && ISA_SUPPORTS_MIPS16E)
		{
		  /* Convert MIPS16 jr/jalr into a "compact" jump.  */
		  ip->insn_opcode |= 0x0080;
		  install_insn (ip);
		  insert_into_history (0, 1, ip);
		} 
	      else
		{
		  /* We could do even better for unconditional branches to
		     portions of this object file; we could pick up the
		     instruction at the destination, put it in the delay
		     slot, and bump the destination address.  */
		  insert_into_history (0, 1, ip);
		  emit_nop ();
		}
		
	      if (mips_relax.sequence)
		mips_relax.sizes[mips_relax.sequence - 1] += 4;
	    }
	  else
	    {
	      /* It looks like we can actually do the swap.  */
	      struct mips_cl_insn delay = history[0];
	      if (mips_opts.mips16)
		{
		  know (delay.frag == ip->frag);
                  move_insn (ip, delay.frag, delay.where);
		  move_insn (&delay, ip->frag, ip->where + insn_length (ip));
		}
	      else if (relaxed_branch)
		{
		  /* Add the delay slot instruction to the end of the
		     current frag and shrink the fixed part of the
		     original frag.  If the branch occupies the tail of
		     the latter, move it backwards to cover the gap.  */
		  delay.frag->fr_fix -= 4;
		  if (delay.frag == ip->frag)
		    move_insn (ip, ip->frag, ip->where - 4);
		  add_fixed_insn (&delay);
		}
	      else
		{
		  move_insn (&delay, ip->frag, ip->where);
		  move_insn (ip, history[0].frag, history[0].where);
		}
	      history[0] = *ip;
	      delay.fixed_p = 1;
	      insert_into_history (0, 1, &delay);
	    }

	  /* If that was an unconditional branch, forget the previous
	     insn information.  */
	  if (pinfo & INSN_UNCOND_BRANCH_DELAY)
	    mips_no_prev_insn ();
	}
      else if (pinfo & INSN_COND_BRANCH_LIKELY)
	{
	  /* We don't yet optimize a branch likely.  What we should do
	     is look at the target, copy the instruction found there
	     into the delay slot, and increment the branch to jump to
	     the next instruction.  */
	  insert_into_history (0, 1, ip);
	  emit_nop ();
	}
      else
	insert_into_history (0, 1, ip);
    }
  else
    insert_into_history (0, 1, ip);

  /* We just output an insn, so the next one doesn't have a label.  */
  mips_clear_insn_labels ();
}

/* Forget that there was any previous instruction or label.  */

static void
mips_no_prev_insn (void)
{
  prev_nop_frag = NULL;
  insert_into_history (0, ARRAY_SIZE (history), NOP_INSN);
  mips_clear_insn_labels ();
}

/* This function must be called before we emit something other than
   instructions.  It is like mips_no_prev_insn except that it inserts
   any NOPS that might be needed by previous instructions.  */

void
mips_emit_delays (void)
{
  if (! mips_opts.noreorder)
    {
      int nops = nops_for_insn (history, NULL);
      if (nops > 0)
	{
	  while (nops-- > 0)
	    add_fixed_insn (NOP_INSN);
	  mips_move_labels ();
	}
    }
  mips_no_prev_insn ();
}

/* Start a (possibly nested) noreorder block.  */

static void
start_noreorder (void)
{
  if (mips_opts.noreorder == 0)
    {
      unsigned int i;
      int nops;

      /* None of the instructions before the .set noreorder can be moved.  */
      for (i = 0; i < ARRAY_SIZE (history); i++)
	history[i].fixed_p = 1;

      /* Insert any nops that might be needed between the .set noreorder
	 block and the previous instructions.  We will later remove any
	 nops that turn out not to be needed.  */
      nops = nops_for_insn (history, NULL);
      if (nops > 0)
	{
	  if (mips_optimize != 0)
	    {
	      /* Record the frag which holds the nop instructions, so
                 that we can remove them if we don't need them.  */
	      frag_grow (mips_opts.mips16 ? nops * 2 : nops * 4);
	      prev_nop_frag = frag_now;
	      prev_nop_frag_holds = nops;
	      prev_nop_frag_required = 0;
	      prev_nop_frag_since = 0;
	    }

	  for (; nops > 0; --nops)
	    add_fixed_insn (NOP_INSN);

	  /* Move on to a new frag, so that it is safe to simply
	     decrease the size of prev_nop_frag.  */
	  frag_wane (frag_now);
	  frag_new (0);
	  mips_move_labels ();
	}
      mips16_mark_labels ();
      mips_clear_insn_labels ();
    }
  mips_opts.noreorder++;
  mips_any_noreorder = 1;
}

/* End a nested noreorder block.  */

static void
end_noreorder (void)
{
  mips_opts.noreorder--;
  if (mips_opts.noreorder == 0 && prev_nop_frag != NULL)
    {
      /* Commit to inserting prev_nop_frag_required nops and go back to
	 handling nop insertion the .set reorder way.  */
      prev_nop_frag->fr_fix -= ((prev_nop_frag_holds - prev_nop_frag_required)
				* (mips_opts.mips16 ? 2 : 4));
      insert_into_history (prev_nop_frag_since,
			   prev_nop_frag_required, NOP_INSN);
      prev_nop_frag = NULL;
    }
}

/* Set up global variables for the start of a new macro.  */

static void
macro_start (void)
{
  memset (&mips_macro_warning.sizes, 0, sizeof (mips_macro_warning.sizes));
  mips_macro_warning.delay_slot_p = (mips_opts.noreorder
				     && (history[0].insn_mo->pinfo
					 & (INSN_UNCOND_BRANCH_DELAY
					    | INSN_COND_BRANCH_DELAY
					    | INSN_COND_BRANCH_LIKELY)) != 0);
}

/* Given that a macro is longer than 4 bytes, return the appropriate warning
   for it.  Return null if no warning is needed.  SUBTYPE is a bitmask of
   RELAX_DELAY_SLOT and RELAX_NOMACRO.  */

static const char *
macro_warning (relax_substateT subtype)
{
  if (subtype & RELAX_DELAY_SLOT)
    return _("Macro instruction expanded into multiple instructions"
	     " in a branch delay slot");
  else if (subtype & RELAX_NOMACRO)
    return _("Macro instruction expanded into multiple instructions");
  else
    return 0;
}

/* Finish up a macro.  Emit warnings as appropriate.  */

static void
macro_end (void)
{
  if (mips_macro_warning.sizes[0] > 4 || mips_macro_warning.sizes[1] > 4)
    {
      relax_substateT subtype;

      /* Set up the relaxation warning flags.  */
      subtype = 0;
      if (mips_macro_warning.sizes[1] > mips_macro_warning.sizes[0])
	subtype |= RELAX_SECOND_LONGER;
      if (mips_opts.warn_about_macros)
	subtype |= RELAX_NOMACRO;
      if (mips_macro_warning.delay_slot_p)
	subtype |= RELAX_DELAY_SLOT;

      if (mips_macro_warning.sizes[0] > 4 && mips_macro_warning.sizes[1] > 4)
	{
	  /* Either the macro has a single implementation or both
	     implementations are longer than 4 bytes.  Emit the
	     warning now.  */
	  const char *msg = macro_warning (subtype);
	  if (msg != 0)
	    as_warn ("%s", msg);
	}
      else
	{
	  /* One implementation might need a warning but the other
	     definitely doesn't.  */
	  mips_macro_warning.first_frag->fr_subtype |= subtype;
	}
    }
}

/* Read a macro's relocation codes from *ARGS and store them in *R.
   The first argument in *ARGS will be either the code for a single
   relocation or -1 followed by the three codes that make up a
   composite relocation.  */

static void
macro_read_relocs (va_list *args, bfd_reloc_code_real_type *r)
{
  int i, next;

  next = va_arg (*args, int);
  if (next >= 0)
    r[0] = (bfd_reloc_code_real_type) next;
  else
    for (i = 0; i < 3; i++)
      r[i] = (bfd_reloc_code_real_type) va_arg (*args, int);
}

/* Build an instruction created by a macro expansion.  This is passed
   a pointer to the count of instructions created so far, an
   expression, the name of the instruction to build, an operand format
   string, and corresponding arguments.  */

static void
macro_build (expressionS *ep, const char *name, const char *fmt, ...)
{
  const struct mips_opcode *mo;
  struct mips_cl_insn insn;
  bfd_reloc_code_real_type r[3];
  va_list args;

  va_start (args, fmt);

  if (mips_opts.mips16)
    {
      mips16_macro_build (ep, name, fmt, args);
      va_end (args);
      return;
    }

  r[0] = BFD_RELOC_UNUSED;
  r[1] = BFD_RELOC_UNUSED;
  r[2] = BFD_RELOC_UNUSED;
  mo = (struct mips_opcode *) hash_find (op_hash, name);
  assert (mo);
  assert (strcmp (name, mo->name) == 0);

  while (1)
    {
      /* Search until we get a match for NAME.  It is assumed here that
	 macros will never generate MDMX, MIPS-3D, or MT instructions.  */
      if (strcmp (fmt, mo->args) == 0
	  && mo->pinfo != INSN_MACRO
	  && OPCODE_IS_MEMBER (mo,
			       (mips_opts.isa
				| (mips_opts.mips16 ? INSN_MIPS16 : 0)
				| (mips_opts.ase_dsp ? INSN_DSP : 0)
				| ((mips_opts.ase_dsp && ISA_SUPPORTS_DSP64_ASE)
				   ? INSN_DSP64 : 0)
				| (mips_opts.ase_dspr2 ? INSN_DSPR2 : 0)
				| (mips_opts.ase_smartmips ? INSN_SMARTMIPS : 0)),
			       mips_opts.arch)
	  && (mips_opts.arch != CPU_R4650 || (mo->pinfo & FP_D) == 0))
	break;

      ++mo;
      assert (mo->name);
      assert (strcmp (name, mo->name) == 0);
    }

  create_insn (&insn, mo);
  for (;;)
    {
      switch (*fmt++)
	{
	case '\0':
	  break;

	case ',':
	case '(':
	case ')':
	  continue;

	case '+':
	  switch (*fmt++)
	    {
	    case 'A':
	    case 'E':
	      INSERT_OPERAND (SHAMT, insn, va_arg (args, int));
	      continue;

	    case 'B':
	    case 'F':
	      /* Note that in the macro case, these arguments are already
		 in MSB form.  (When handling the instruction in the
		 non-macro case, these arguments are sizes from which
		 MSB values must be calculated.)  */
	      INSERT_OPERAND (INSMSB, insn, va_arg (args, int));
	      continue;

	    case 'C':
	    case 'G':
	    case 'H':
	      /* Note that in the macro case, these arguments are already
		 in MSBD form.  (When handling the instruction in the
		 non-macro case, these arguments are sizes from which
		 MSBD values must be calculated.)  */
	      INSERT_OPERAND (EXTMSBD, insn, va_arg (args, int));
	      continue;

	    default:
	      internalError ();
	    }
	  continue;

	case '2':
	  INSERT_OPERAND (BP, insn, va_arg (args, int));
	  continue;

	case 't':
	case 'w':
	case 'E':
	  INSERT_OPERAND (RT, insn, va_arg (args, int));
	  continue;

	case 'c':
	  INSERT_OPERAND (CODE, insn, va_arg (args, int));
	  continue;

	case 'T':
	case 'W':
	  INSERT_OPERAND (FT, insn, va_arg (args, int));
	  continue;

	case 'd':
	case 'G':
	case 'K':
	  INSERT_OPERAND (RD, insn, va_arg (args, int));
	  continue;

	case 'U':
	  {
	    int tmp = va_arg (args, int);

	    INSERT_OPERAND (RT, insn, tmp);
	    INSERT_OPERAND (RD, insn, tmp);
	    continue;
	  }

	case 'V':
	case 'S':
	  INSERT_OPERAND (FS, insn, va_arg (args, int));
	  continue;

	case 'z':
	  continue;

	case '<':
	  INSERT_OPERAND (SHAMT, insn, va_arg (args, int));
	  continue;

	case 'D':
	  INSERT_OPERAND (FD, insn, va_arg (args, int));
	  continue;

	case 'B':
	  INSERT_OPERAND (CODE20, insn, va_arg (args, int));
	  continue;

	case 'J':
	  INSERT_OPERAND (CODE19, insn, va_arg (args, int));
	  continue;

	case 'q':
	  INSERT_OPERAND (CODE2, insn, va_arg (args, int));
	  continue;

	case 'b':
	case 's':
	case 'r':
	case 'v':
	  INSERT_OPERAND (RS, insn, va_arg (args, int));
	  continue;

	case 'i':
	case 'j':
	case 'o':
	  macro_read_relocs (&args, r);
	  assert (*r == BFD_RELOC_GPREL16
		  || *r == BFD_RELOC_MIPS_LITERAL
		  || *r == BFD_RELOC_MIPS_HIGHER
		  || *r == BFD_RELOC_HI16_S
		  || *r == BFD_RELOC_LO16
		  || *r == BFD_RELOC_MIPS_GOT16
		  || *r == BFD_RELOC_MIPS_CALL16
		  || *r == BFD_RELOC_MIPS_GOT_DISP
		  || *r == BFD_RELOC_MIPS_GOT_PAGE
		  || *r == BFD_RELOC_MIPS_GOT_OFST
		  || *r == BFD_RELOC_MIPS_GOT_LO16
		  || *r == BFD_RELOC_MIPS_CALL_LO16);
	  continue;

	case 'u':
	  macro_read_relocs (&args, r);
	  assert (ep != NULL
		  && (ep->X_op == O_constant
		      || (ep->X_op == O_symbol
			  && (*r == BFD_RELOC_MIPS_HIGHEST
			      || *r == BFD_RELOC_HI16_S
			      || *r == BFD_RELOC_HI16
			      || *r == BFD_RELOC_GPREL16
			      || *r == BFD_RELOC_MIPS_GOT_HI16
			      || *r == BFD_RELOC_MIPS_CALL_HI16))));
	  continue;

	case 'p':
	  assert (ep != NULL);

	  /*
	   * This allows macro() to pass an immediate expression for
	   * creating short branches without creating a symbol.
	   *
	   * We don't allow branch relaxation for these branches, as
	   * they should only appear in ".set nomacro" anyway.
	   */
	  if (ep->X_op == O_constant)
	    {
	      if ((ep->X_add_number & 3) != 0)
		as_bad (_("branch to misaligned address (0x%lx)"),
			(unsigned long) ep->X_add_number);
	      if ((ep->X_add_number + 0x20000) & ~0x3ffff)
		as_bad (_("branch address range overflow (0x%lx)"),
			(unsigned long) ep->X_add_number);
	      insn.insn_opcode |= (ep->X_add_number >> 2) & 0xffff;
	      ep = NULL;
	    }
	  else
	    *r = BFD_RELOC_16_PCREL_S2;
	  continue;

	case 'a':
	  assert (ep != NULL);
	  *r = BFD_RELOC_MIPS_JMP;
	  continue;

	case 'C':
	  INSERT_OPERAND (COPZ, insn, va_arg (args, unsigned long));
	  continue;

	case 'k':
	  INSERT_OPERAND (CACHE, insn, va_arg (args, unsigned long));
	  continue;

	default:
	  internalError ();
	}
      break;
    }
  va_end (args);
  assert (*r == BFD_RELOC_UNUSED ? ep == NULL : ep != NULL);

  append_insn (&insn, ep, r);
}

static void
mips16_macro_build (expressionS *ep, const char *name, const char *fmt,
		    va_list args)
{
  struct mips_opcode *mo;
  struct mips_cl_insn insn;
  bfd_reloc_code_real_type r[3]
    = {BFD_RELOC_UNUSED, BFD_RELOC_UNUSED, BFD_RELOC_UNUSED};

  mo = (struct mips_opcode *) hash_find (mips16_op_hash, name);
  assert (mo);
  assert (strcmp (name, mo->name) == 0);

  while (strcmp (fmt, mo->args) != 0 || mo->pinfo == INSN_MACRO)
    {
      ++mo;
      assert (mo->name);
      assert (strcmp (name, mo->name) == 0);
    }

  create_insn (&insn, mo);
  for (;;)
    {
      int c;

      c = *fmt++;
      switch (c)
	{
	case '\0':
	  break;

	case ',':
	case '(':
	case ')':
	  continue;

	case 'y':
	case 'w':
	  MIPS16_INSERT_OPERAND (RY, insn, va_arg (args, int));
	  continue;

	case 'x':
	case 'v':
	  MIPS16_INSERT_OPERAND (RX, insn, va_arg (args, int));
	  continue;

	case 'z':
	  MIPS16_INSERT_OPERAND (RZ, insn, va_arg (args, int));
	  continue;

	case 'Z':
	  MIPS16_INSERT_OPERAND (MOVE32Z, insn, va_arg (args, int));
	  continue;

	case '0':
	case 'S':
	case 'P':
	case 'R':
	  continue;

	case 'X':
	  MIPS16_INSERT_OPERAND (REGR32, insn, va_arg (args, int));
	  continue;

	case 'Y':
	  {
	    int regno;

	    regno = va_arg (args, int);
	    regno = ((regno & 7) << 2) | ((regno & 0x18) >> 3);
	    MIPS16_INSERT_OPERAND (REG32R, insn, regno);
	  }
	  continue;

	case '<':
	case '>':
	case '4':
	case '5':
	case 'H':
	case 'W':
	case 'D':
	case 'j':
	case '8':
	case 'V':
	case 'C':
	case 'U':
	case 'k':
	case 'K':
	case 'p':
	case 'q':
	  {
	    assert (ep != NULL);

	    if (ep->X_op != O_constant)
	      *r = (int) BFD_RELOC_UNUSED + c;
	    else
	      {
		mips16_immed (NULL, 0, c, ep->X_add_number, FALSE, FALSE,
			      FALSE, &insn.insn_opcode, &insn.use_extend,
			      &insn.extend);
		ep = NULL;
		*r = BFD_RELOC_UNUSED;
	      }
	  }
	  continue;

	case '6':
	  MIPS16_INSERT_OPERAND (IMM6, insn, va_arg (args, int));
	  continue;
	}

      break;
    }

  assert (*r == BFD_RELOC_UNUSED ? ep == NULL : ep != NULL);

  append_insn (&insn, ep, r);
}

/*
 * Sign-extend 32-bit mode constants that have bit 31 set and all
 * higher bits unset.
 */
static void
normalize_constant_expr (expressionS *ex)
{
  if (ex->X_op == O_constant
      && IS_ZEXT_32BIT_NUM (ex->X_add_number))
    ex->X_add_number = (((ex->X_add_number & 0xffffffff) ^ 0x80000000)
			- 0x80000000);
}

/*
 * Sign-extend 32-bit mode address offsets that have bit 31 set and
 * all higher bits unset.
 */
static void
normalize_address_expr (expressionS *ex)
{
  if (((ex->X_op == O_constant && HAVE_32BIT_ADDRESSES)
	|| (ex->X_op == O_symbol && HAVE_32BIT_SYMBOLS))
      && IS_ZEXT_32BIT_NUM (ex->X_add_number))
    ex->X_add_number = (((ex->X_add_number & 0xffffffff) ^ 0x80000000)
			- 0x80000000);
}

/*
 * Generate a "jalr" instruction with a relocation hint to the called
 * function.  This occurs in NewABI PIC code.
 */
static void
macro_build_jalr (expressionS *ep)
{
  char *f = NULL;

  if (HAVE_NEWABI)
    {
      frag_grow (8);
      f = frag_more (0);
    }
  macro_build (NULL, "jalr", "d,s", RA, PIC_CALL_REG);
  if (HAVE_NEWABI)
    fix_new_exp (frag_now, f - frag_now->fr_literal,
		 4, ep, FALSE, BFD_RELOC_MIPS_JALR);
}

/*
 * Generate a "lui" instruction.
 */
static void
macro_build_lui (expressionS *ep, int regnum)
{
  expressionS high_expr;
  const struct mips_opcode *mo;
  struct mips_cl_insn insn;
  bfd_reloc_code_real_type r[3]
    = {BFD_RELOC_UNUSED, BFD_RELOC_UNUSED, BFD_RELOC_UNUSED};
  const char *name = "lui";
  const char *fmt = "t,u";

  assert (! mips_opts.mips16);

  high_expr = *ep;

  if (high_expr.X_op == O_constant)
    {
      /* We can compute the instruction now without a relocation entry.  */
      high_expr.X_add_number = ((high_expr.X_add_number + 0x8000)
				>> 16) & 0xffff;
      *r = BFD_RELOC_UNUSED;
    }
  else
    {
      assert (ep->X_op == O_symbol);
      /* _gp_disp is a special case, used from s_cpload.
	 __gnu_local_gp is used if mips_no_shared.  */
      assert (mips_pic == NO_PIC
	      || (! HAVE_NEWABI
		  && strcmp (S_GET_NAME (ep->X_add_symbol), "_gp_disp") == 0)
	      || (! mips_in_shared
		  && strcmp (S_GET_NAME (ep->X_add_symbol),
                             "__gnu_local_gp") == 0));
      *r = BFD_RELOC_HI16_S;
    }

  mo = hash_find (op_hash, name);
  assert (strcmp (name, mo->name) == 0);
  assert (strcmp (fmt, mo->args) == 0);
  create_insn (&insn, mo);

  insn.insn_opcode = insn.insn_mo->match;
  INSERT_OPERAND (RT, insn, regnum);
  if (*r == BFD_RELOC_UNUSED)
    {
      insn.insn_opcode |= high_expr.X_add_number;
      append_insn (&insn, NULL, r);
    }
  else
    append_insn (&insn, &high_expr, r);
}

/* Generate a sequence of instructions to do a load or store from a constant
   offset off of a base register (breg) into/from a target register (treg),
   using AT if necessary.  */
static void
macro_build_ldst_constoffset (expressionS *ep, const char *op,
			      int treg, int breg, int dbl)
{
  assert (ep->X_op == O_constant);

  /* Sign-extending 32-bit constants makes their handling easier.  */
  if (!dbl)
    normalize_constant_expr (ep);

  /* Right now, this routine can only handle signed 32-bit constants.  */
  if (! IS_SEXT_32BIT_NUM(ep->X_add_number + 0x8000))
    as_warn (_("operand overflow"));

  if (IS_SEXT_16BIT_NUM(ep->X_add_number))
    {
      /* Signed 16-bit offset will fit in the op.  Easy!  */
      macro_build (ep, op, "t,o(b)", treg, BFD_RELOC_LO16, breg);
    }
  else
    {
      /* 32-bit offset, need multiple instructions and AT, like:
	   lui      $tempreg,const_hi       (BFD_RELOC_HI16_S)
	   addu     $tempreg,$tempreg,$breg
           <op>     $treg,const_lo($tempreg)   (BFD_RELOC_LO16)
         to handle the complete offset.  */
      macro_build_lui (ep, AT);
      macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t", AT, AT, breg);
      macro_build (ep, op, "t,o(b)", treg, BFD_RELOC_LO16, AT);

      if (mips_opts.noat)
	as_bad (_("Macro used $at after \".set noat\""));
    }
}

/*			set_at()
 * Generates code to set the $at register to true (one)
 * if reg is less than the immediate expression.
 */
static void
set_at (int reg, int unsignedp)
{
  if (imm_expr.X_op == O_constant
      && imm_expr.X_add_number >= -0x8000
      && imm_expr.X_add_number < 0x8000)
    macro_build (&imm_expr, unsignedp ? "sltiu" : "slti", "t,r,j",
		 AT, reg, BFD_RELOC_LO16);
  else
    {
      load_register (AT, &imm_expr, HAVE_64BIT_GPRS);
      macro_build (NULL, unsignedp ? "sltu" : "slt", "d,v,t", AT, reg, AT);
    }
}

/* Warn if an expression is not a constant.  */

static void
check_absolute_expr (struct mips_cl_insn *ip, expressionS *ex)
{
  if (ex->X_op == O_big)
    as_bad (_("unsupported large constant"));
  else if (ex->X_op != O_constant)
    as_bad (_("Instruction %s requires absolute expression"),
	    ip->insn_mo->name);

  if (HAVE_32BIT_GPRS)
    normalize_constant_expr (ex);
}

/* Count the leading zeroes by performing a binary chop. This is a
   bulky bit of source, but performance is a LOT better for the
   majority of values than a simple loop to count the bits:
       for (lcnt = 0; (lcnt < 32); lcnt++)
         if ((v) & (1 << (31 - lcnt)))
           break;
  However it is not code size friendly, and the gain will drop a bit
  on certain cached systems.
*/
#define COUNT_TOP_ZEROES(v)             \
  (((v) & ~0xffff) == 0                 \
   ? ((v) & ~0xff) == 0                 \
     ? ((v) & ~0xf) == 0                \
       ? ((v) & ~0x3) == 0              \
         ? ((v) & ~0x1) == 0            \
           ? !(v)                       \
             ? 32                       \
             : 31                       \
           : 30                         \
         : ((v) & ~0x7) == 0            \
           ? 29                         \
           : 28                         \
       : ((v) & ~0x3f) == 0             \
         ? ((v) & ~0x1f) == 0           \
           ? 27                         \
           : 26                         \
         : ((v) & ~0x7f) == 0           \
           ? 25                         \
           : 24                         \
     : ((v) & ~0xfff) == 0              \
       ? ((v) & ~0x3ff) == 0            \
         ? ((v) & ~0x1ff) == 0          \
           ? 23                         \
           : 22                         \
         : ((v) & ~0x7ff) == 0          \
           ? 21                         \
           : 20                         \
       : ((v) & ~0x3fff) == 0           \
         ? ((v) & ~0x1fff) == 0         \
           ? 19                         \
           : 18                         \
         : ((v) & ~0x7fff) == 0         \
           ? 17                         \
           : 16                         \
   : ((v) & ~0xffffff) == 0             \
     ? ((v) & ~0xfffff) == 0            \
       ? ((v) & ~0x3ffff) == 0          \
         ? ((v) & ~0x1ffff) == 0        \
           ? 15                         \
           : 14                         \
         : ((v) & ~0x7ffff) == 0        \
           ? 13                         \
           : 12                         \
       : ((v) & ~0x3fffff) == 0         \
         ? ((v) & ~0x1fffff) == 0       \
           ? 11                         \
           : 10                         \
         : ((v) & ~0x7fffff) == 0       \
           ? 9                          \
           : 8                          \
     : ((v) & ~0xfffffff) == 0          \
       ? ((v) & ~0x3ffffff) == 0        \
         ? ((v) & ~0x1ffffff) == 0      \
           ? 7                          \
           : 6                          \
         : ((v) & ~0x7ffffff) == 0      \
           ? 5                          \
           : 4                          \
       : ((v) & ~0x3fffffff) == 0       \
         ? ((v) & ~0x1fffffff) == 0     \
           ? 3                          \
           : 2                          \
         : ((v) & ~0x7fffffff) == 0     \
           ? 1                          \
           : 0)

/*			load_register()
 *  This routine generates the least number of instructions necessary to load
 *  an absolute expression value into a register.
 */
static void
load_register (int reg, expressionS *ep, int dbl)
{
  int freg;
  expressionS hi32, lo32;

  if (ep->X_op != O_big)
    {
      assert (ep->X_op == O_constant);

      /* Sign-extending 32-bit constants makes their handling easier.  */
      if (!dbl)
	normalize_constant_expr (ep);

      if (IS_SEXT_16BIT_NUM (ep->X_add_number))
	{
	  /* We can handle 16 bit signed values with an addiu to
	     $zero.  No need to ever use daddiu here, since $zero and
	     the result are always correct in 32 bit mode.  */
	  macro_build (ep, "addiu", "t,r,j", reg, 0, BFD_RELOC_LO16);
	  return;
	}
      else if (ep->X_add_number >= 0 && ep->X_add_number < 0x10000)
	{
	  /* We can handle 16 bit unsigned values with an ori to
             $zero.  */
	  macro_build (ep, "ori", "t,r,i", reg, 0, BFD_RELOC_LO16);
	  return;
	}
      else if ((IS_SEXT_32BIT_NUM (ep->X_add_number)))
	{
	  /* 32 bit values require an lui.  */
	  macro_build (ep, "lui", "t,u", reg, BFD_RELOC_HI16);
	  if ((ep->X_add_number & 0xffff) != 0)
	    macro_build (ep, "ori", "t,r,i", reg, reg, BFD_RELOC_LO16);
	  return;
	}
    }

  /* The value is larger than 32 bits.  */

  if (!dbl || HAVE_32BIT_GPRS)
    {
      char value[32];

      sprintf_vma (value, ep->X_add_number);
      as_bad (_("Number (0x%s) larger than 32 bits"), value);
      macro_build (ep, "addiu", "t,r,j", reg, 0, BFD_RELOC_LO16);
      return;
    }

  if (ep->X_op != O_big)
    {
      hi32 = *ep;
      hi32.X_add_number = (valueT) hi32.X_add_number >> 16;
      hi32.X_add_number = (valueT) hi32.X_add_number >> 16;
      hi32.X_add_number &= 0xffffffff;
      lo32 = *ep;
      lo32.X_add_number &= 0xffffffff;
    }
  else
    {
      assert (ep->X_add_number > 2);
      if (ep->X_add_number == 3)
	generic_bignum[3] = 0;
      else if (ep->X_add_number > 4)
	as_bad (_("Number larger than 64 bits"));
      lo32.X_op = O_constant;
      lo32.X_add_number = generic_bignum[0] + (generic_bignum[1] << 16);
      hi32.X_op = O_constant;
      hi32.X_add_number = generic_bignum[2] + (generic_bignum[3] << 16);
    }

  if (hi32.X_add_number == 0)
    freg = 0;
  else
    {
      int shift, bit;
      unsigned long hi, lo;

      if (hi32.X_add_number == (offsetT) 0xffffffff)
	{
	  if ((lo32.X_add_number & 0xffff8000) == 0xffff8000)
	    {
	      macro_build (&lo32, "addiu", "t,r,j", reg, 0, BFD_RELOC_LO16);
	      return;
	    }
	  if (lo32.X_add_number & 0x80000000)
	    {
	      macro_build (&lo32, "lui", "t,u", reg, BFD_RELOC_HI16);
	      if (lo32.X_add_number & 0xffff)
		macro_build (&lo32, "ori", "t,r,i", reg, reg, BFD_RELOC_LO16);
	      return;
	    }
	}

      /* Check for 16bit shifted constant.  We know that hi32 is
         non-zero, so start the mask on the first bit of the hi32
         value.  */
      shift = 17;
      do
	{
	  unsigned long himask, lomask;

	  if (shift < 32)
	    {
	      himask = 0xffff >> (32 - shift);
	      lomask = (0xffff << shift) & 0xffffffff;
	    }
	  else
	    {
	      himask = 0xffff << (shift - 32);
	      lomask = 0;
	    }
	  if ((hi32.X_add_number & ~(offsetT) himask) == 0
	      && (lo32.X_add_number & ~(offsetT) lomask) == 0)
	    {
	      expressionS tmp;

	      tmp.X_op = O_constant;
	      if (shift < 32)
		tmp.X_add_number = ((hi32.X_add_number << (32 - shift))
				    | (lo32.X_add_number >> shift));
	      else
		tmp.X_add_number = hi32.X_add_number >> (shift - 32);
	      macro_build (&tmp, "ori", "t,r,i", reg, 0, BFD_RELOC_LO16);
	      macro_build (NULL, (shift >= 32) ? "dsll32" : "dsll", "d,w,<",
			   reg, reg, (shift >= 32) ? shift - 32 : shift);
	      return;
	    }
	  ++shift;
	}
      while (shift <= (64 - 16));

      /* Find the bit number of the lowest one bit, and store the
         shifted value in hi/lo.  */
      hi = (unsigned long) (hi32.X_add_number & 0xffffffff);
      lo = (unsigned long) (lo32.X_add_number & 0xffffffff);
      if (lo != 0)
	{
	  bit = 0;
	  while ((lo & 1) == 0)
	    {
	      lo >>= 1;
	      ++bit;
	    }
	  lo |= (hi & (((unsigned long) 1 << bit) - 1)) << (32 - bit);
	  hi >>= bit;
	}
      else
	{
	  bit = 32;
	  while ((hi & 1) == 0)
	    {
	      hi >>= 1;
	      ++bit;
	    }
	  lo = hi;
	  hi = 0;
	}

      /* Optimize if the shifted value is a (power of 2) - 1.  */
      if ((hi == 0 && ((lo + 1) & lo) == 0)
	  || (lo == 0xffffffff && ((hi + 1) & hi) == 0))
	{
	  shift = COUNT_TOP_ZEROES ((unsigned int) hi32.X_add_number);
	  if (shift != 0)
	    {
	      expressionS tmp;

	      /* This instruction will set the register to be all
                 ones.  */
	      tmp.X_op = O_constant;
	      tmp.X_add_number = (offsetT) -1;
	      macro_build (&tmp, "addiu", "t,r,j", reg, 0, BFD_RELOC_LO16);
	      if (bit != 0)
		{
		  bit += shift;
		  macro_build (NULL, (bit >= 32) ? "dsll32" : "dsll", "d,w,<",
			       reg, reg, (bit >= 32) ? bit - 32 : bit);
		}
	      macro_build (NULL, (shift >= 32) ? "dsrl32" : "dsrl", "d,w,<",
			   reg, reg, (shift >= 32) ? shift - 32 : shift);
	      return;
	    }
	}

      /* Sign extend hi32 before calling load_register, because we can
         generally get better code when we load a sign extended value.  */
      if ((hi32.X_add_number & 0x80000000) != 0)
	hi32.X_add_number |= ~(offsetT) 0xffffffff;
      load_register (reg, &hi32, 0);
      freg = reg;
    }
  if ((lo32.X_add_number & 0xffff0000) == 0)
    {
      if (freg != 0)
	{
	  macro_build (NULL, "dsll32", "d,w,<", reg, freg, 0);
	  freg = reg;
	}
    }
  else
    {
      expressionS mid16;

      if ((freg == 0) && (lo32.X_add_number == (offsetT) 0xffffffff))
	{
	  macro_build (&lo32, "lui", "t,u", reg, BFD_RELOC_HI16);
	  macro_build (NULL, "dsrl32", "d,w,<", reg, reg, 0);
	  return;
	}

      if (freg != 0)
	{
	  macro_build (NULL, "dsll", "d,w,<", reg, freg, 16);
	  freg = reg;
	}
      mid16 = lo32;
      mid16.X_add_number >>= 16;
      macro_build (&mid16, "ori", "t,r,i", reg, freg, BFD_RELOC_LO16);
      macro_build (NULL, "dsll", "d,w,<", reg, reg, 16);
      freg = reg;
    }
  if ((lo32.X_add_number & 0xffff) != 0)
    macro_build (&lo32, "ori", "t,r,i", reg, freg, BFD_RELOC_LO16);
}

static inline void
load_delay_nop (void)
{
  if (!gpr_interlocks)
    macro_build (NULL, "nop", "");
}

/* Load an address into a register.  */

static void
load_address (int reg, expressionS *ep, int *used_at)
{
  if (ep->X_op != O_constant
      && ep->X_op != O_symbol)
    {
      as_bad (_("expression too complex"));
      ep->X_op = O_constant;
    }

  if (ep->X_op == O_constant)
    {
      load_register (reg, ep, HAVE_64BIT_ADDRESSES);
      return;
    }

  if (mips_pic == NO_PIC)
    {
      /* If this is a reference to a GP relative symbol, we want
	   addiu	$reg,$gp,<sym>		(BFD_RELOC_GPREL16)
	 Otherwise we want
	   lui		$reg,<sym>		(BFD_RELOC_HI16_S)
	   addiu	$reg,$reg,<sym>		(BFD_RELOC_LO16)
	 If we have an addend, we always use the latter form.

	 With 64bit address space and a usable $at we want
	   lui		$reg,<sym>		(BFD_RELOC_MIPS_HIGHEST)
	   lui		$at,<sym>		(BFD_RELOC_HI16_S)
	   daddiu	$reg,<sym>		(BFD_RELOC_MIPS_HIGHER)
	   daddiu	$at,<sym>		(BFD_RELOC_LO16)
	   dsll32	$reg,0
	   daddu	$reg,$reg,$at

	 If $at is already in use, we use a path which is suboptimal
	 on superscalar processors.
	   lui		$reg,<sym>		(BFD_RELOC_MIPS_HIGHEST)
	   daddiu	$reg,<sym>		(BFD_RELOC_MIPS_HIGHER)
	   dsll		$reg,16
	   daddiu	$reg,<sym>		(BFD_RELOC_HI16_S)
	   dsll		$reg,16
	   daddiu	$reg,<sym>		(BFD_RELOC_LO16)

	 For GP relative symbols in 64bit address space we can use
	 the same sequence as in 32bit address space.  */
      if (HAVE_64BIT_SYMBOLS)
	{
	  if ((valueT) ep->X_add_number <= MAX_GPREL_OFFSET
	      && !nopic_need_relax (ep->X_add_symbol, 1))
	    {
	      relax_start (ep->X_add_symbol);
	      macro_build (ep, ADDRESS_ADDI_INSN, "t,r,j", reg,
			   mips_gp_register, BFD_RELOC_GPREL16);
	      relax_switch ();
	    }

	  if (*used_at == 0 && !mips_opts.noat)
	    {
	      macro_build (ep, "lui", "t,u", reg, BFD_RELOC_MIPS_HIGHEST);
	      macro_build (ep, "lui", "t,u", AT, BFD_RELOC_HI16_S);
	      macro_build (ep, "daddiu", "t,r,j", reg, reg,
			   BFD_RELOC_MIPS_HIGHER);
	      macro_build (ep, "daddiu", "t,r,j", AT, AT, BFD_RELOC_LO16);
	      macro_build (NULL, "dsll32", "d,w,<", reg, reg, 0);
	      macro_build (NULL, "daddu", "d,v,t", reg, reg, AT);
	      *used_at = 1;
	    }
	  else
	    {
	      macro_build (ep, "lui", "t,u", reg, BFD_RELOC_MIPS_HIGHEST);
	      macro_build (ep, "daddiu", "t,r,j", reg, reg,
			   BFD_RELOC_MIPS_HIGHER);
	      macro_build (NULL, "dsll", "d,w,<", reg, reg, 16);
	      macro_build (ep, "daddiu", "t,r,j", reg, reg, BFD_RELOC_HI16_S);
	      macro_build (NULL, "dsll", "d,w,<", reg, reg, 16);
	      macro_build (ep, "daddiu", "t,r,j", reg, reg, BFD_RELOC_LO16);
	    }

	  if (mips_relax.sequence)
	    relax_end ();
	}
      else
	{
	  if ((valueT) ep->X_add_number <= MAX_GPREL_OFFSET
	      && !nopic_need_relax (ep->X_add_symbol, 1))
	    {
	      relax_start (ep->X_add_symbol);
	      macro_build (ep, ADDRESS_ADDI_INSN, "t,r,j", reg,
			   mips_gp_register, BFD_RELOC_GPREL16);
	      relax_switch ();
	    }
	  macro_build_lui (ep, reg);
	  macro_build (ep, ADDRESS_ADDI_INSN, "t,r,j",
		       reg, reg, BFD_RELOC_LO16);
	  if (mips_relax.sequence)
	    relax_end ();
	}
    }
  else if (!mips_big_got)
    {
      expressionS ex;

      /* If this is a reference to an external symbol, we want
	   lw		$reg,<sym>($gp)		(BFD_RELOC_MIPS_GOT16)
	 Otherwise we want
	   lw		$reg,<sym>($gp)		(BFD_RELOC_MIPS_GOT16)
	   nop
	   addiu	$reg,$reg,<sym>		(BFD_RELOC_LO16)
	 If there is a constant, it must be added in after.

	 If we have NewABI, we want
	   lw		$reg,<sym+cst>($gp)	(BFD_RELOC_MIPS_GOT_DISP)
         unless we're referencing a global symbol with a non-zero
         offset, in which case cst must be added separately.  */
      if (HAVE_NEWABI)
	{
	  if (ep->X_add_number)
	    {
	      ex.X_add_number = ep->X_add_number;
	      ep->X_add_number = 0;
	      relax_start (ep->X_add_symbol);
	      macro_build (ep, ADDRESS_LOAD_INSN, "t,o(b)", reg,
			   BFD_RELOC_MIPS_GOT_DISP, mips_gp_register);
	      if (ex.X_add_number < -0x8000 || ex.X_add_number >= 0x8000)
		as_bad (_("PIC code offset overflow (max 16 signed bits)"));
	      ex.X_op = O_constant;
	      macro_build (&ex, ADDRESS_ADDI_INSN, "t,r,j",
			   reg, reg, BFD_RELOC_LO16);
	      ep->X_add_number = ex.X_add_number;
	      relax_switch ();
	    }
	  macro_build (ep, ADDRESS_LOAD_INSN, "t,o(b)", reg,
		       BFD_RELOC_MIPS_GOT_DISP, mips_gp_register);
	  if (mips_relax.sequence)
	    relax_end ();
	}
      else
	{
	  ex.X_add_number = ep->X_add_number;
	  ep->X_add_number = 0;
	  macro_build (ep, ADDRESS_LOAD_INSN, "t,o(b)", reg,
		       BFD_RELOC_MIPS_GOT16, mips_gp_register);
	  load_delay_nop ();
	  relax_start (ep->X_add_symbol);
	  relax_switch ();
	  macro_build (ep, ADDRESS_ADDI_INSN, "t,r,j", reg, reg,
		       BFD_RELOC_LO16);
	  relax_end ();

	  if (ex.X_add_number != 0)
	    {
	      if (ex.X_add_number < -0x8000 || ex.X_add_number >= 0x8000)
		as_bad (_("PIC code offset overflow (max 16 signed bits)"));
	      ex.X_op = O_constant;
	      macro_build (&ex, ADDRESS_ADDI_INSN, "t,r,j",
			   reg, reg, BFD_RELOC_LO16);
	    }
	}
    }
  else if (mips_big_got)
    {
      expressionS ex;

      /* This is the large GOT case.  If this is a reference to an
	 external symbol, we want
	   lui		$reg,<sym>		(BFD_RELOC_MIPS_GOT_HI16)
	   addu		$reg,$reg,$gp
	   lw		$reg,<sym>($reg)	(BFD_RELOC_MIPS_GOT_LO16)

	 Otherwise, for a reference to a local symbol in old ABI, we want
	   lw		$reg,<sym>($gp)		(BFD_RELOC_MIPS_GOT16)
	   nop
	   addiu	$reg,$reg,<sym>		(BFD_RELOC_LO16)
	 If there is a constant, it must be added in after.

	 In the NewABI, for local symbols, with or without offsets, we want:
	   lw		$reg,<sym>($gp)		(BFD_RELOC_MIPS_GOT_PAGE)
	   addiu	$reg,$reg,<sym>		(BFD_RELOC_MIPS_GOT_OFST)
      */
      if (HAVE_NEWABI)
	{
	  ex.X_add_number = ep->X_add_number;
	  ep->X_add_number = 0;
	  relax_start (ep->X_add_symbol);
	  macro_build (ep, "lui", "t,u", reg, BFD_RELOC_MIPS_GOT_HI16);
	  macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
		       reg, reg, mips_gp_register);
	  macro_build (ep, ADDRESS_LOAD_INSN, "t,o(b)",
		       reg, BFD_RELOC_MIPS_GOT_LO16, reg);
	  if (ex.X_add_number < -0x8000 || ex.X_add_number >= 0x8000)
	    as_bad (_("PIC code offset overflow (max 16 signed bits)"));
	  else if (ex.X_add_number)
	    {
	      ex.X_op = O_constant;
	      macro_build (&ex, ADDRESS_ADDI_INSN, "t,r,j", reg, reg,
			   BFD_RELOC_LO16);
	    }

	  ep->X_add_number = ex.X_add_number;
	  relax_switch ();
	  macro_build (ep, ADDRESS_LOAD_INSN, "t,o(b)", reg,
		       BFD_RELOC_MIPS_GOT_PAGE, mips_gp_register);
	  macro_build (ep, ADDRESS_ADDI_INSN, "t,r,j", reg, reg,
		       BFD_RELOC_MIPS_GOT_OFST);
	  relax_end ();
	}
      else
	{
	  ex.X_add_number = ep->X_add_number;
	  ep->X_add_number = 0;
	  relax_start (ep->X_add_symbol);
	  macro_build (ep, "lui", "t,u", reg, BFD_RELOC_MIPS_GOT_HI16);
	  macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
		       reg, reg, mips_gp_register);
	  macro_build (ep, ADDRESS_LOAD_INSN, "t,o(b)",
		       reg, BFD_RELOC_MIPS_GOT_LO16, reg);
	  relax_switch ();
	  if (reg_needs_delay (mips_gp_register))
	    {
	      /* We need a nop before loading from $gp.  This special
		 check is required because the lui which starts the main
		 instruction stream does not refer to $gp, and so will not
		 insert the nop which may be required.  */
	      macro_build (NULL, "nop", "");
	    }
	  macro_build (ep, ADDRESS_LOAD_INSN, "t,o(b)", reg,
		       BFD_RELOC_MIPS_GOT16, mips_gp_register);
	  load_delay_nop ();
	  macro_build (ep, ADDRESS_ADDI_INSN, "t,r,j", reg, reg,
		       BFD_RELOC_LO16);
	  relax_end ();

	  if (ex.X_add_number != 0)
	    {
	      if (ex.X_add_number < -0x8000 || ex.X_add_number >= 0x8000)
		as_bad (_("PIC code offset overflow (max 16 signed bits)"));
	      ex.X_op = O_constant;
	      macro_build (&ex, ADDRESS_ADDI_INSN, "t,r,j", reg, reg,
			   BFD_RELOC_LO16);
	    }
	}
    }
  else
    abort ();

  if (mips_opts.noat && *used_at == 1)
    as_bad (_("Macro used $at after \".set noat\""));
}

/* Move the contents of register SOURCE into register DEST.  */

static void
move_register (int dest, int source)
{
  macro_build (NULL, HAVE_32BIT_GPRS ? "addu" : "daddu", "d,v,t",
	       dest, source, 0);
}

/* Emit an SVR4 PIC sequence to load address LOCAL into DEST, where
   LOCAL is the sum of a symbol and a 16-bit or 32-bit displacement.
   The two alternatives are:

   Global symbol		Local sybmol
   -------------		------------
   lw DEST,%got(SYMBOL)		lw DEST,%got(SYMBOL + OFFSET)
   ...				...
   addiu DEST,DEST,OFFSET	addiu DEST,DEST,%lo(SYMBOL + OFFSET)

   load_got_offset emits the first instruction and add_got_offset
   emits the second for a 16-bit offset or add_got_offset_hilo emits
   a sequence to add a 32-bit offset using a scratch register.  */

static void
load_got_offset (int dest, expressionS *local)
{
  expressionS global;

  global = *local;
  global.X_add_number = 0;

  relax_start (local->X_add_symbol);
  macro_build (&global, ADDRESS_LOAD_INSN, "t,o(b)", dest,
	       BFD_RELOC_MIPS_GOT16, mips_gp_register);
  relax_switch ();
  macro_build (local, ADDRESS_LOAD_INSN, "t,o(b)", dest,
	       BFD_RELOC_MIPS_GOT16, mips_gp_register);
  relax_end ();
}

static void
add_got_offset (int dest, expressionS *local)
{
  expressionS global;

  global.X_op = O_constant;
  global.X_op_symbol = NULL;
  global.X_add_symbol = NULL;
  global.X_add_number = local->X_add_number;

  relax_start (local->X_add_symbol);
  macro_build (&global, ADDRESS_ADDI_INSN, "t,r,j",
	       dest, dest, BFD_RELOC_LO16);
  relax_switch ();
  macro_build (local, ADDRESS_ADDI_INSN, "t,r,j", dest, dest, BFD_RELOC_LO16);
  relax_end ();
}

static void
add_got_offset_hilo (int dest, expressionS *local, int tmp)
{
  expressionS global;
  int hold_mips_optimize;

  global.X_op = O_constant;
  global.X_op_symbol = NULL;
  global.X_add_symbol = NULL;
  global.X_add_number = local->X_add_number;

  relax_start (local->X_add_symbol);
  load_register (tmp, &global, HAVE_64BIT_ADDRESSES);
  relax_switch ();
  /* Set mips_optimize around the lui instruction to avoid
     inserting an unnecessary nop after the lw.  */
  hold_mips_optimize = mips_optimize;
  mips_optimize = 2;
  macro_build_lui (&global, tmp);
  mips_optimize = hold_mips_optimize;
  macro_build (local, ADDRESS_ADDI_INSN, "t,r,j", tmp, tmp, BFD_RELOC_LO16);
  relax_end ();

  macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t", dest, dest, tmp);
}

/*
 *			Build macros
 *   This routine implements the seemingly endless macro or synthesized
 * instructions and addressing modes in the mips assembly language. Many
 * of these macros are simple and are similar to each other. These could
 * probably be handled by some kind of table or grammar approach instead of
 * this verbose method. Others are not simple macros but are more like
 * optimizing code generation.
 *   One interesting optimization is when several store macros appear
 * consecutively that would load AT with the upper half of the same address.
 * The ensuing load upper instructions are ommited. This implies some kind
 * of global optimization. We currently only optimize within a single macro.
 *   For many of the load and store macros if the address is specified as a
 * constant expression in the first 64k of memory (ie ld $2,0x4000c) we
 * first load register 'at' with zero and use it as the base register. The
 * mips assembler simply uses register $zero. Just one tiny optimization
 * we're missing.
 */
static void
macro (struct mips_cl_insn *ip)
{
  int treg, sreg, dreg, breg;
  int tempreg;
  int mask;
  int used_at = 0;
  expressionS expr1;
  const char *s;
  const char *s2;
  const char *fmt;
  int likely = 0;
  int dbl = 0;
  int coproc = 0;
  int lr = 0;
  int imm = 0;
  int call = 0;
  int off;
  offsetT maxnum;
  bfd_reloc_code_real_type r;
  int hold_mips_optimize;

  assert (! mips_opts.mips16);

  treg = (ip->insn_opcode >> 16) & 0x1f;
  dreg = (ip->insn_opcode >> 11) & 0x1f;
  sreg = breg = (ip->insn_opcode >> 21) & 0x1f;
  mask = ip->insn_mo->mask;

  expr1.X_op = O_constant;
  expr1.X_op_symbol = NULL;
  expr1.X_add_symbol = NULL;
  expr1.X_add_number = 1;

  switch (mask)
    {
    case M_DABS:
      dbl = 1;
    case M_ABS:
      /* bgez $a0,.+12
	 move v0,$a0
	 sub v0,$zero,$a0
	 */

      start_noreorder ();

      expr1.X_add_number = 8;
      macro_build (&expr1, "bgez", "s,p", sreg);
      if (dreg == sreg)
	macro_build (NULL, "nop", "", 0);
      else
	move_register (dreg, sreg);
      macro_build (NULL, dbl ? "dsub" : "sub", "d,v,t", dreg, 0, sreg);

      end_noreorder ();
      break;

    case M_ADD_I:
      s = "addi";
      s2 = "add";
      goto do_addi;
    case M_ADDU_I:
      s = "addiu";
      s2 = "addu";
      goto do_addi;
    case M_DADD_I:
      dbl = 1;
      s = "daddi";
      s2 = "dadd";
      goto do_addi;
    case M_DADDU_I:
      dbl = 1;
      s = "daddiu";
      s2 = "daddu";
    do_addi:
      if (imm_expr.X_op == O_constant
	  && imm_expr.X_add_number >= -0x8000
	  && imm_expr.X_add_number < 0x8000)
	{
	  macro_build (&imm_expr, s, "t,r,j", treg, sreg, BFD_RELOC_LO16);
	  break;
	}
      used_at = 1;
      load_register (AT, &imm_expr, dbl);
      macro_build (NULL, s2, "d,v,t", treg, sreg, AT);
      break;

    case M_AND_I:
      s = "andi";
      s2 = "and";
      goto do_bit;
    case M_OR_I:
      s = "ori";
      s2 = "or";
      goto do_bit;
    case M_NOR_I:
      s = "";
      s2 = "nor";
      goto do_bit;
    case M_XOR_I:
      s = "xori";
      s2 = "xor";
    do_bit:
      if (imm_expr.X_op == O_constant
	  && imm_expr.X_add_number >= 0
	  && imm_expr.X_add_number < 0x10000)
	{
	  if (mask != M_NOR_I)
	    macro_build (&imm_expr, s, "t,r,i", treg, sreg, BFD_RELOC_LO16);
	  else
	    {
	      macro_build (&imm_expr, "ori", "t,r,i",
			   treg, sreg, BFD_RELOC_LO16);
	      macro_build (NULL, "nor", "d,v,t", treg, treg, 0);
	    }
	  break;
	}

      used_at = 1;
      load_register (AT, &imm_expr, HAVE_64BIT_GPRS);
      macro_build (NULL, s2, "d,v,t", treg, sreg, AT);
      break;

    case M_BALIGN:
      switch (imm_expr.X_add_number)
	{
	case 0:
	  macro_build (NULL, "nop", "");
	  break;
	case 2:
	  macro_build (NULL, "packrl.ph", "d,s,t", treg, treg, sreg);
	  break;
	default:
	  macro_build (NULL, "balign", "t,s,2", treg, sreg,
		       (int)imm_expr.X_add_number);
	  break;
	}
      break;

    case M_BEQ_I:
      s = "beq";
      goto beq_i;
    case M_BEQL_I:
      s = "beql";
      likely = 1;
      goto beq_i;
    case M_BNE_I:
      s = "bne";
      goto beq_i;
    case M_BNEL_I:
      s = "bnel";
      likely = 1;
    beq_i:
      if (imm_expr.X_op == O_constant && imm_expr.X_add_number == 0)
	{
	  macro_build (&offset_expr, s, "s,t,p", sreg, 0);
	  break;
	}
      used_at = 1;
      load_register (AT, &imm_expr, HAVE_64BIT_GPRS);
      macro_build (&offset_expr, s, "s,t,p", sreg, AT);
      break;

    case M_BGEL:
      likely = 1;
    case M_BGE:
      if (treg == 0)
	{
	  macro_build (&offset_expr, likely ? "bgezl" : "bgez", "s,p", sreg);
	  break;
	}
      if (sreg == 0)
	{
	  macro_build (&offset_expr, likely ? "blezl" : "blez", "s,p", treg);
	  break;
	}
      used_at = 1;
      macro_build (NULL, "slt", "d,v,t", AT, sreg, treg);
      macro_build (&offset_expr, likely ? "beql" : "beq", "s,t,p", AT, 0);
      break;

    case M_BGTL_I:
      likely = 1;
    case M_BGT_I:
      /* check for > max integer */
      maxnum = 0x7fffffff;
      if (HAVE_64BIT_GPRS && sizeof (maxnum) > 4)
	{
	  maxnum <<= 16;
	  maxnum |= 0xffff;
	  maxnum <<= 16;
	  maxnum |= 0xffff;
	}
      if (imm_expr.X_op == O_constant
	  && imm_expr.X_add_number >= maxnum
	  && (HAVE_32BIT_GPRS || sizeof (maxnum) > 4))
	{
	do_false:
	  /* result is always false */
	  if (! likely)
	    macro_build (NULL, "nop", "", 0);
	  else
	    macro_build (&offset_expr, "bnel", "s,t,p", 0, 0);
	  break;
	}
      if (imm_expr.X_op != O_constant)
	as_bad (_("Unsupported large constant"));
      ++imm_expr.X_add_number;
      /* FALLTHROUGH */
    case M_BGE_I:
    case M_BGEL_I:
      if (mask == M_BGEL_I)
	likely = 1;
      if (imm_expr.X_op == O_constant && imm_expr.X_add_number == 0)
	{
	  macro_build (&offset_expr, likely ? "bgezl" : "bgez", "s,p", sreg);
	  break;
	}
      if (imm_expr.X_op == O_constant && imm_expr.X_add_number == 1)
	{
	  macro_build (&offset_expr, likely ? "bgtzl" : "bgtz", "s,p", sreg);
	  break;
	}
      maxnum = 0x7fffffff;
      if (HAVE_64BIT_GPRS && sizeof (maxnum) > 4)
	{
	  maxnum <<= 16;
	  maxnum |= 0xffff;
	  maxnum <<= 16;
	  maxnum |= 0xffff;
	}
      maxnum = - maxnum - 1;
      if (imm_expr.X_op == O_constant
	  && imm_expr.X_add_number <= maxnum
	  && (HAVE_32BIT_GPRS || sizeof (maxnum) > 4))
	{
	do_true:
	  /* result is always true */
	  as_warn (_("Branch %s is always true"), ip->insn_mo->name);
	  macro_build (&offset_expr, "b", "p");
	  break;
	}
      used_at = 1;
      set_at (sreg, 0);
      macro_build (&offset_expr, likely ? "beql" : "beq", "s,t,p", AT, 0);
      break;

    case M_BGEUL:
      likely = 1;
    case M_BGEU:
      if (treg == 0)
	goto do_true;
      if (sreg == 0)
	{
	  macro_build (&offset_expr, likely ? "beql" : "beq",
		       "s,t,p", 0, treg);
	  break;
	}
      used_at = 1;
      macro_build (NULL, "sltu", "d,v,t", AT, sreg, treg);
      macro_build (&offset_expr, likely ? "beql" : "beq", "s,t,p", AT, 0);
      break;

    case M_BGTUL_I:
      likely = 1;
    case M_BGTU_I:
      if (sreg == 0
	  || (HAVE_32BIT_GPRS
	      && imm_expr.X_op == O_constant
	      && imm_expr.X_add_number == (offsetT) 0xffffffff))
	goto do_false;
      if (imm_expr.X_op != O_constant)
	as_bad (_("Unsupported large constant"));
      ++imm_expr.X_add_number;
      /* FALLTHROUGH */
    case M_BGEU_I:
    case M_BGEUL_I:
      if (mask == M_BGEUL_I)
	likely = 1;
      if (imm_expr.X_op == O_constant && imm_expr.X_add_number == 0)
	goto do_true;
      if (imm_expr.X_op == O_constant && imm_expr.X_add_number == 1)
	{
	  macro_build (&offset_expr, likely ? "bnel" : "bne",
		       "s,t,p", sreg, 0);
	  break;
	}
      used_at = 1;
      set_at (sreg, 1);
      macro_build (&offset_expr, likely ? "beql" : "beq", "s,t,p", AT, 0);
      break;

    case M_BGTL:
      likely = 1;
    case M_BGT:
      if (treg == 0)
	{
	  macro_build (&offset_expr, likely ? "bgtzl" : "bgtz", "s,p", sreg);
	  break;
	}
      if (sreg == 0)
	{
	  macro_build (&offset_expr, likely ? "bltzl" : "bltz", "s,p", treg);
	  break;
	}
      used_at = 1;
      macro_build (NULL, "slt", "d,v,t", AT, treg, sreg);
      macro_build (&offset_expr, likely ? "bnel" : "bne", "s,t,p", AT, 0);
      break;

    case M_BGTUL:
      likely = 1;
    case M_BGTU:
      if (treg == 0)
	{
	  macro_build (&offset_expr, likely ? "bnel" : "bne",
		       "s,t,p", sreg, 0);
	  break;
	}
      if (sreg == 0)
	goto do_false;
      used_at = 1;
      macro_build (NULL, "sltu", "d,v,t", AT, treg, sreg);
      macro_build (&offset_expr, likely ? "bnel" : "bne", "s,t,p", AT, 0);
      break;

    case M_BLEL:
      likely = 1;
    case M_BLE:
      if (treg == 0)
	{
	  macro_build (&offset_expr, likely ? "blezl" : "blez", "s,p", sreg);
	  break;
	}
      if (sreg == 0)
	{
	  macro_build (&offset_expr, likely ? "bgezl" : "bgez", "s,p", treg);
	  break;
	}
      used_at = 1;
      macro_build (NULL, "slt", "d,v,t", AT, treg, sreg);
      macro_build (&offset_expr, likely ? "beql" : "beq", "s,t,p", AT, 0);
      break;

    case M_BLEL_I:
      likely = 1;
    case M_BLE_I:
      maxnum = 0x7fffffff;
      if (HAVE_64BIT_GPRS && sizeof (maxnum) > 4)
	{
	  maxnum <<= 16;
	  maxnum |= 0xffff;
	  maxnum <<= 16;
	  maxnum |= 0xffff;
	}
      if (imm_expr.X_op == O_constant
	  && imm_expr.X_add_number >= maxnum
	  && (HAVE_32BIT_GPRS || sizeof (maxnum) > 4))
	goto do_true;
      if (imm_expr.X_op != O_constant)
	as_bad (_("Unsupported large constant"));
      ++imm_expr.X_add_number;
      /* FALLTHROUGH */
    case M_BLT_I:
    case M_BLTL_I:
      if (mask == M_BLTL_I)
	likely = 1;
      if (imm_expr.X_op == O_constant && imm_expr.X_add_number == 0)
	{
	  macro_build (&offset_expr, likely ? "bltzl" : "bltz", "s,p", sreg);
	  break;
	}
      if (imm_expr.X_op == O_constant && imm_expr.X_add_number == 1)
	{
	  macro_build (&offset_expr, likely ? "blezl" : "blez", "s,p", sreg);
	  break;
	}
      used_at = 1;
      set_at (sreg, 0);
      macro_build (&offset_expr, likely ? "bnel" : "bne", "s,t,p", AT, 0);
      break;

    case M_BLEUL:
      likely = 1;
    case M_BLEU:
      if (treg == 0)
	{
	  macro_build (&offset_expr, likely ? "beql" : "beq",
		       "s,t,p", sreg, 0);
	  break;
	}
      if (sreg == 0)
	goto do_true;
      used_at = 1;
      macro_build (NULL, "sltu", "d,v,t", AT, treg, sreg);
      macro_build (&offset_expr, likely ? "beql" : "beq", "s,t,p", AT, 0);
      break;

    case M_BLEUL_I:
      likely = 1;
    case M_BLEU_I:
      if (sreg == 0
	  || (HAVE_32BIT_GPRS
	      && imm_expr.X_op == O_constant
	      && imm_expr.X_add_number == (offsetT) 0xffffffff))
	goto do_true;
      if (imm_expr.X_op != O_constant)
	as_bad (_("Unsupported large constant"));
      ++imm_expr.X_add_number;
      /* FALLTHROUGH */
    case M_BLTU_I:
    case M_BLTUL_I:
      if (mask == M_BLTUL_I)
	likely = 1;
      if (imm_expr.X_op == O_constant && imm_expr.X_add_number == 0)
	goto do_false;
      if (imm_expr.X_op == O_constant && imm_expr.X_add_number == 1)
	{
	  macro_build (&offset_expr, likely ? "beql" : "beq",
		       "s,t,p", sreg, 0);
	  break;
	}
      used_at = 1;
      set_at (sreg, 1);
      macro_build (&offset_expr, likely ? "bnel" : "bne", "s,t,p", AT, 0);
      break;

    case M_BLTL:
      likely = 1;
    case M_BLT:
      if (treg == 0)
	{
	  macro_build (&offset_expr, likely ? "bltzl" : "bltz", "s,p", sreg);
	  break;
	}
      if (sreg == 0)
	{
	  macro_build (&offset_expr, likely ? "bgtzl" : "bgtz", "s,p", treg);
	  break;
	}
      used_at = 1;
      macro_build (NULL, "slt", "d,v,t", AT, sreg, treg);
      macro_build (&offset_expr, likely ? "bnel" : "bne", "s,t,p", AT, 0);
      break;

    case M_BLTUL:
      likely = 1;
    case M_BLTU:
      if (treg == 0)
	goto do_false;
      if (sreg == 0)
	{
	  macro_build (&offset_expr, likely ? "bnel" : "bne",
		       "s,t,p", 0, treg);
	  break;
	}
      used_at = 1;
      macro_build (NULL, "sltu", "d,v,t", AT, sreg, treg);
      macro_build (&offset_expr, likely ? "bnel" : "bne", "s,t,p", AT, 0);
      break;

    case M_DEXT:
      {
	unsigned long pos;
	unsigned long size;

        if (imm_expr.X_op != O_constant || imm2_expr.X_op != O_constant)
	  {
	    as_bad (_("Unsupported large constant"));
	    pos = size = 1;
	  }
	else
	  {
	    pos = (unsigned long) imm_expr.X_add_number;
	    size = (unsigned long) imm2_expr.X_add_number;
	  }

	if (pos > 63)
	  {
	    as_bad (_("Improper position (%lu)"), pos);
	    pos = 1;
	  }
        if (size == 0 || size > 64
	    || (pos + size - 1) > 63)
	  {
	    as_bad (_("Improper extract size (%lu, position %lu)"),
		    size, pos);
	    size = 1;
	  }

	if (size <= 32 && pos < 32)
	  {
	    s = "dext";
	    fmt = "t,r,+A,+C";
	  }
	else if (size <= 32)
	  {
	    s = "dextu";
	    fmt = "t,r,+E,+H";
	  }
	else
	  {
	    s = "dextm";
	    fmt = "t,r,+A,+G";
	  }
	macro_build ((expressionS *) NULL, s, fmt, treg, sreg, pos, size - 1);
      }
      break;

    case M_DINS:
      {
	unsigned long pos;
	unsigned long size;

        if (imm_expr.X_op != O_constant || imm2_expr.X_op != O_constant)
	  {
	    as_bad (_("Unsupported large constant"));
	    pos = size = 1;
	  }
	else
	  {
	    pos = (unsigned long) imm_expr.X_add_number;
	    size = (unsigned long) imm2_expr.X_add_number;
	  }

	if (pos > 63)
	  {
	    as_bad (_("Improper position (%lu)"), pos);
	    pos = 1;
	  }
        if (size == 0 || size > 64
	    || (pos + size - 1) > 63)
	  {
	    as_bad (_("Improper insert size (%lu, position %lu)"),
		    size, pos);
	    size = 1;
	  }

	if (pos < 32 && (pos + size - 1) < 32)
	  {
	    s = "dins";
	    fmt = "t,r,+A,+B";
	  }
	else if (pos >= 32)
	  {
	    s = "dinsu";
	    fmt = "t,r,+E,+F";
	  }
	else
	  {
	    s = "dinsm";
	    fmt = "t,r,+A,+F";
	  }
	macro_build ((expressionS *) NULL, s, fmt, treg, sreg, pos,
		     pos + size - 1);
      }
      break;

    case M_DDIV_3:
      dbl = 1;
    case M_DIV_3:
      s = "mflo";
      goto do_div3;
    case M_DREM_3:
      dbl = 1;
    case M_REM_3:
      s = "mfhi";
    do_div3:
      if (treg == 0)
	{
	  as_warn (_("Divide by zero."));
	  if (mips_trap)
	    macro_build (NULL, "teq", "s,t,q", 0, 0, 7);
	  else
	    macro_build (NULL, "break", "c", 7);
	  break;
	}

      start_noreorder ();
      if (mips_trap)
	{
	  macro_build (NULL, "teq", "s,t,q", treg, 0, 7);
	  macro_build (NULL, dbl ? "ddiv" : "div", "z,s,t", sreg, treg);
	}
      else
	{
	  expr1.X_add_number = 8;
	  macro_build (&expr1, "bne", "s,t,p", treg, 0);
	  macro_build (NULL, dbl ? "ddiv" : "div", "z,s,t", sreg, treg);
	  macro_build (NULL, "break", "c", 7);
	}
      expr1.X_add_number = -1;
      used_at = 1;
      load_register (AT, &expr1, dbl);
      expr1.X_add_number = mips_trap ? (dbl ? 12 : 8) : (dbl ? 20 : 16);
      macro_build (&expr1, "bne", "s,t,p", treg, AT);
      if (dbl)
	{
	  expr1.X_add_number = 1;
	  load_register (AT, &expr1, dbl);
	  macro_build (NULL, "dsll32", "d,w,<", AT, AT, 31);
	}
      else
	{
	  expr1.X_add_number = 0x80000000;
	  macro_build (&expr1, "lui", "t,u", AT, BFD_RELOC_HI16);
	}
      if (mips_trap)
	{
	  macro_build (NULL, "teq", "s,t,q", sreg, AT, 6);
	  /* We want to close the noreorder block as soon as possible, so
	     that later insns are available for delay slot filling.  */
	  end_noreorder ();
	}
      else
	{
	  expr1.X_add_number = 8;
	  macro_build (&expr1, "bne", "s,t,p", sreg, AT);
	  macro_build (NULL, "nop", "", 0);

	  /* We want to close the noreorder block as soon as possible, so
	     that later insns are available for delay slot filling.  */
	  end_noreorder ();

	  macro_build (NULL, "break", "c", 6);
	}
      macro_build (NULL, s, "d", dreg);
      break;

    case M_DIV_3I:
      s = "div";
      s2 = "mflo";
      goto do_divi;
    case M_DIVU_3I:
      s = "divu";
      s2 = "mflo";
      goto do_divi;
    case M_REM_3I:
      s = "div";
      s2 = "mfhi";
      goto do_divi;
    case M_REMU_3I:
      s = "divu";
      s2 = "mfhi";
      goto do_divi;
    case M_DDIV_3I:
      dbl = 1;
      s = "ddiv";
      s2 = "mflo";
      goto do_divi;
    case M_DDIVU_3I:
      dbl = 1;
      s = "ddivu";
      s2 = "mflo";
      goto do_divi;
    case M_DREM_3I:
      dbl = 1;
      s = "ddiv";
      s2 = "mfhi";
      goto do_divi;
    case M_DREMU_3I:
      dbl = 1;
      s = "ddivu";
      s2 = "mfhi";
    do_divi:
      if (imm_expr.X_op == O_constant && imm_expr.X_add_number == 0)
	{
	  as_warn (_("Divide by zero."));
	  if (mips_trap)
	    macro_build (NULL, "teq", "s,t,q", 0, 0, 7);
	  else
	    macro_build (NULL, "break", "c", 7);
	  break;
	}
      if (imm_expr.X_op == O_constant && imm_expr.X_add_number == 1)
	{
	  if (strcmp (s2, "mflo") == 0)
	    move_register (dreg, sreg);
	  else
	    move_register (dreg, 0);
	  break;
	}
      if (imm_expr.X_op == O_constant
	  && imm_expr.X_add_number == -1
	  && s[strlen (s) - 1] != 'u')
	{
	  if (strcmp (s2, "mflo") == 0)
	    {
	      macro_build (NULL, dbl ? "dneg" : "neg", "d,w", dreg, sreg);
	    }
	  else
	    move_register (dreg, 0);
	  break;
	}

      used_at = 1;
      load_register (AT, &imm_expr, dbl);
      macro_build (NULL, s, "z,s,t", sreg, AT);
      macro_build (NULL, s2, "d", dreg);
      break;

    case M_DIVU_3:
      s = "divu";
      s2 = "mflo";
      goto do_divu3;
    case M_REMU_3:
      s = "divu";
      s2 = "mfhi";
      goto do_divu3;
    case M_DDIVU_3:
      s = "ddivu";
      s2 = "mflo";
      goto do_divu3;
    case M_DREMU_3:
      s = "ddivu";
      s2 = "mfhi";
    do_divu3:
      start_noreorder ();
      if (mips_trap)
	{
	  macro_build (NULL, "teq", "s,t,q", treg, 0, 7);
	  macro_build (NULL, s, "z,s,t", sreg, treg);
	  /* We want to close the noreorder block as soon as possible, so
	     that later insns are available for delay slot filling.  */
	  end_noreorder ();
	}
      else
	{
	  expr1.X_add_number = 8;
	  macro_build (&expr1, "bne", "s,t,p", treg, 0);
	  macro_build (NULL, s, "z,s,t", sreg, treg);

	  /* We want to close the noreorder block as soon as possible, so
	     that later insns are available for delay slot filling.  */
	  end_noreorder ();
	  macro_build (NULL, "break", "c", 7);
	}
      macro_build (NULL, s2, "d", dreg);
      break;

    case M_DLCA_AB:
      dbl = 1;
    case M_LCA_AB:
      call = 1;
      goto do_la;
    case M_DLA_AB:
      dbl = 1;
    case M_LA_AB:
    do_la:
      /* Load the address of a symbol into a register.  If breg is not
	 zero, we then add a base register to it.  */

      if (dbl && HAVE_32BIT_GPRS)
	as_warn (_("dla used to load 32-bit register"));

      if (! dbl && HAVE_64BIT_OBJECTS)
	as_warn (_("la used to load 64-bit address"));

      if (offset_expr.X_op == O_constant
	  && offset_expr.X_add_number >= -0x8000
	  && offset_expr.X_add_number < 0x8000)
	{
	  macro_build (&offset_expr, ADDRESS_ADDI_INSN,
		       "t,r,j", treg, sreg, BFD_RELOC_LO16);
	  break;
	}

      if (!mips_opts.noat && (treg == breg))
	{
	  tempreg = AT;
	  used_at = 1;
	}
      else
	{
	  tempreg = treg;
	}

      if (offset_expr.X_op != O_symbol
	  && offset_expr.X_op != O_constant)
	{
	  as_bad (_("expression too complex"));
	  offset_expr.X_op = O_constant;
	}

      if (offset_expr.X_op == O_constant)
	load_register (tempreg, &offset_expr, HAVE_64BIT_ADDRESSES);
      else if (mips_pic == NO_PIC)
	{
	  /* If this is a reference to a GP relative symbol, we want
	       addiu	$tempreg,$gp,<sym>	(BFD_RELOC_GPREL16)
	     Otherwise we want
	       lui	$tempreg,<sym>		(BFD_RELOC_HI16_S)
	       addiu	$tempreg,$tempreg,<sym>	(BFD_RELOC_LO16)
	     If we have a constant, we need two instructions anyhow,
	     so we may as well always use the latter form.

	     With 64bit address space and a usable $at we want
	       lui	$tempreg,<sym>		(BFD_RELOC_MIPS_HIGHEST)
	       lui	$at,<sym>		(BFD_RELOC_HI16_S)
	       daddiu	$tempreg,<sym>		(BFD_RELOC_MIPS_HIGHER)
	       daddiu	$at,<sym>		(BFD_RELOC_LO16)
	       dsll32	$tempreg,0
	       daddu	$tempreg,$tempreg,$at

	     If $at is already in use, we use a path which is suboptimal
	     on superscalar processors.
	       lui	$tempreg,<sym>		(BFD_RELOC_MIPS_HIGHEST)
	       daddiu	$tempreg,<sym>		(BFD_RELOC_MIPS_HIGHER)
	       dsll	$tempreg,16
	       daddiu	$tempreg,<sym>		(BFD_RELOC_HI16_S)
	       dsll	$tempreg,16
	       daddiu	$tempreg,<sym>		(BFD_RELOC_LO16)

	     For GP relative symbols in 64bit address space we can use
	     the same sequence as in 32bit address space.  */
	  if (HAVE_64BIT_SYMBOLS)
	    {
	      if ((valueT) offset_expr.X_add_number <= MAX_GPREL_OFFSET
		  && !nopic_need_relax (offset_expr.X_add_symbol, 1))
		{
		  relax_start (offset_expr.X_add_symbol);
		  macro_build (&offset_expr, ADDRESS_ADDI_INSN, "t,r,j",
			       tempreg, mips_gp_register, BFD_RELOC_GPREL16);
		  relax_switch ();
		}

	      if (used_at == 0 && !mips_opts.noat)
		{
		  macro_build (&offset_expr, "lui", "t,u",
			       tempreg, BFD_RELOC_MIPS_HIGHEST);
		  macro_build (&offset_expr, "lui", "t,u",
			       AT, BFD_RELOC_HI16_S);
		  macro_build (&offset_expr, "daddiu", "t,r,j",
			       tempreg, tempreg, BFD_RELOC_MIPS_HIGHER);
		  macro_build (&offset_expr, "daddiu", "t,r,j",
			       AT, AT, BFD_RELOC_LO16);
		  macro_build (NULL, "dsll32", "d,w,<", tempreg, tempreg, 0);
		  macro_build (NULL, "daddu", "d,v,t", tempreg, tempreg, AT);
		  used_at = 1;
		}
	      else
		{
		  macro_build (&offset_expr, "lui", "t,u",
			       tempreg, BFD_RELOC_MIPS_HIGHEST);
		  macro_build (&offset_expr, "daddiu", "t,r,j",
			       tempreg, tempreg, BFD_RELOC_MIPS_HIGHER);
		  macro_build (NULL, "dsll", "d,w,<", tempreg, tempreg, 16);
		  macro_build (&offset_expr, "daddiu", "t,r,j",
			       tempreg, tempreg, BFD_RELOC_HI16_S);
		  macro_build (NULL, "dsll", "d,w,<", tempreg, tempreg, 16);
		  macro_build (&offset_expr, "daddiu", "t,r,j",
			       tempreg, tempreg, BFD_RELOC_LO16);
		}

	      if (mips_relax.sequence)
		relax_end ();
	    }
	  else
	    {
	      if ((valueT) offset_expr.X_add_number <= MAX_GPREL_OFFSET
		  && !nopic_need_relax (offset_expr.X_add_symbol, 1))
		{
		  relax_start (offset_expr.X_add_symbol);
		  macro_build (&offset_expr, ADDRESS_ADDI_INSN, "t,r,j",
			       tempreg, mips_gp_register, BFD_RELOC_GPREL16);
		  relax_switch ();
		}
	      if (!IS_SEXT_32BIT_NUM (offset_expr.X_add_number))
		as_bad (_("offset too large"));
	      macro_build_lui (&offset_expr, tempreg);
	      macro_build (&offset_expr, ADDRESS_ADDI_INSN, "t,r,j",
			   tempreg, tempreg, BFD_RELOC_LO16);
	      if (mips_relax.sequence)
		relax_end ();
	    }
	}
      else if (!mips_big_got && !HAVE_NEWABI)
	{
	  int lw_reloc_type = (int) BFD_RELOC_MIPS_GOT16;

	  /* If this is a reference to an external symbol, and there
	     is no constant, we want
	       lw	$tempreg,<sym>($gp)	(BFD_RELOC_MIPS_GOT16)
	     or for lca or if tempreg is PIC_CALL_REG
	       lw	$tempreg,<sym>($gp)	(BFD_RELOC_MIPS_CALL16)
	     For a local symbol, we want
	       lw	$tempreg,<sym>($gp)	(BFD_RELOC_MIPS_GOT16)
	       nop
	       addiu	$tempreg,$tempreg,<sym>	(BFD_RELOC_LO16)

	     If we have a small constant, and this is a reference to
	     an external symbol, we want
	       lw	$tempreg,<sym>($gp)	(BFD_RELOC_MIPS_GOT16)
	       nop
	       addiu	$tempreg,$tempreg,<constant>
	     For a local symbol, we want the same instruction
	     sequence, but we output a BFD_RELOC_LO16 reloc on the
	     addiu instruction.

	     If we have a large constant, and this is a reference to
	     an external symbol, we want
	       lw	$tempreg,<sym>($gp)	(BFD_RELOC_MIPS_GOT16)
	       lui	$at,<hiconstant>
	       addiu	$at,$at,<loconstant>
	       addu	$tempreg,$tempreg,$at
	     For a local symbol, we want the same instruction
	     sequence, but we output a BFD_RELOC_LO16 reloc on the
	     addiu instruction.
	   */

	  if (offset_expr.X_add_number == 0)
	    {
	      if (mips_pic == SVR4_PIC
		  && breg == 0
		  && (call || tempreg == PIC_CALL_REG))
		lw_reloc_type = (int) BFD_RELOC_MIPS_CALL16;

	      relax_start (offset_expr.X_add_symbol);
	      macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)", tempreg,
			   lw_reloc_type, mips_gp_register);
	      if (breg != 0)
		{
		  /* We're going to put in an addu instruction using
		     tempreg, so we may as well insert the nop right
		     now.  */
		  load_delay_nop ();
		}
	      relax_switch ();
	      macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)",
			   tempreg, BFD_RELOC_MIPS_GOT16, mips_gp_register);
	      load_delay_nop ();
	      macro_build (&offset_expr, ADDRESS_ADDI_INSN, "t,r,j",
			   tempreg, tempreg, BFD_RELOC_LO16);
	      relax_end ();
	      /* FIXME: If breg == 0, and the next instruction uses
		 $tempreg, then if this variant case is used an extra
		 nop will be generated.  */
	    }
	  else if (offset_expr.X_add_number >= -0x8000
		   && offset_expr.X_add_number < 0x8000)
	    {
	      load_got_offset (tempreg, &offset_expr);
	      load_delay_nop ();
	      add_got_offset (tempreg, &offset_expr);
	    }
	  else
	    {
	      expr1.X_add_number = offset_expr.X_add_number;
	      offset_expr.X_add_number =
		((offset_expr.X_add_number + 0x8000) & 0xffff) - 0x8000;
	      load_got_offset (tempreg, &offset_expr);
	      offset_expr.X_add_number = expr1.X_add_number;
	      /* If we are going to add in a base register, and the
		 target register and the base register are the same,
		 then we are using AT as a temporary register.  Since
		 we want to load the constant into AT, we add our
		 current AT (from the global offset table) and the
		 register into the register now, and pretend we were
		 not using a base register.  */
	      if (breg == treg)
		{
		  load_delay_nop ();
		  macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
			       treg, AT, breg);
		  breg = 0;
		  tempreg = treg;
		}
	      add_got_offset_hilo (tempreg, &offset_expr, AT);
	      used_at = 1;
	    }
	}
      else if (!mips_big_got && HAVE_NEWABI)
	{
	  int add_breg_early = 0;

	  /* If this is a reference to an external, and there is no
	     constant, or local symbol (*), with or without a
	     constant, we want
	       lw	$tempreg,<sym>($gp)	(BFD_RELOC_MIPS_GOT_DISP)
	     or for lca or if tempreg is PIC_CALL_REG
	       lw	$tempreg,<sym>($gp)	(BFD_RELOC_MIPS_CALL16)

	     If we have a small constant, and this is a reference to
	     an external symbol, we want
	       lw	$tempreg,<sym>($gp)	(BFD_RELOC_MIPS_GOT_DISP)
	       addiu	$tempreg,$tempreg,<constant>

	     If we have a large constant, and this is a reference to
	     an external symbol, we want
	       lw	$tempreg,<sym>($gp)	(BFD_RELOC_MIPS_GOT_DISP)
	       lui	$at,<hiconstant>
	       addiu	$at,$at,<loconstant>
	       addu	$tempreg,$tempreg,$at

	     (*) Other assemblers seem to prefer GOT_PAGE/GOT_OFST for
	     local symbols, even though it introduces an additional
	     instruction.  */

	  if (offset_expr.X_add_number)
	    {
	      expr1.X_add_number = offset_expr.X_add_number;
	      offset_expr.X_add_number = 0;

	      relax_start (offset_expr.X_add_symbol);
	      macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)", tempreg,
			   BFD_RELOC_MIPS_GOT_DISP, mips_gp_register);

	      if (expr1.X_add_number >= -0x8000
		  && expr1.X_add_number < 0x8000)
		{
		  macro_build (&expr1, ADDRESS_ADDI_INSN, "t,r,j",
			       tempreg, tempreg, BFD_RELOC_LO16);
		}
	      else if (IS_SEXT_32BIT_NUM (expr1.X_add_number + 0x8000))
		{
		  int dreg;

		  /* If we are going to add in a base register, and the
		     target register and the base register are the same,
		     then we are using AT as a temporary register.  Since
		     we want to load the constant into AT, we add our
		     current AT (from the global offset table) and the
		     register into the register now, and pretend we were
		     not using a base register.  */
		  if (breg != treg)
		    dreg = tempreg;
		  else
		    {
		      assert (tempreg == AT);
		      macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
				   treg, AT, breg);
		      dreg = treg;
		      add_breg_early = 1;
		    }

		  load_register (AT, &expr1, HAVE_64BIT_ADDRESSES);
		  macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
			       dreg, dreg, AT);

		  used_at = 1;
		}
	      else
		as_bad (_("PIC code offset overflow (max 32 signed bits)"));

	      relax_switch ();
	      offset_expr.X_add_number = expr1.X_add_number;

	      macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)", tempreg,
			   BFD_RELOC_MIPS_GOT_DISP, mips_gp_register);
	      if (add_breg_early)
		{
		  macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
			       treg, tempreg, breg);
		  breg = 0;
		  tempreg = treg;
		}
	      relax_end ();
	    }
	  else if (breg == 0 && (call || tempreg == PIC_CALL_REG))
	    {
	      relax_start (offset_expr.X_add_symbol);
	      macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)", tempreg,
			   BFD_RELOC_MIPS_CALL16, mips_gp_register);
	      relax_switch ();
	      macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)", tempreg,
			   BFD_RELOC_MIPS_GOT_DISP, mips_gp_register);
	      relax_end ();
	    }
	  else
	    {
	      macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)", tempreg,
			   BFD_RELOC_MIPS_GOT_DISP, mips_gp_register);
	    }
	}
      else if (mips_big_got && !HAVE_NEWABI)
	{
	  int gpdelay;
	  int lui_reloc_type = (int) BFD_RELOC_MIPS_GOT_HI16;
	  int lw_reloc_type = (int) BFD_RELOC_MIPS_GOT_LO16;
	  int local_reloc_type = (int) BFD_RELOC_MIPS_GOT16;

	  /* This is the large GOT case.  If this is a reference to an
	     external symbol, and there is no constant, we want
	       lui	$tempreg,<sym>		(BFD_RELOC_MIPS_GOT_HI16)
	       addu	$tempreg,$tempreg,$gp
	       lw	$tempreg,<sym>($tempreg) (BFD_RELOC_MIPS_GOT_LO16)
	     or for lca or if tempreg is PIC_CALL_REG
	       lui	$tempreg,<sym>		(BFD_RELOC_MIPS_CALL_HI16)
	       addu	$tempreg,$tempreg,$gp
	       lw	$tempreg,<sym>($tempreg) (BFD_RELOC_MIPS_CALL_LO16)
	     For a local symbol, we want
	       lw	$tempreg,<sym>($gp)	(BFD_RELOC_MIPS_GOT16)
	       nop
	       addiu	$tempreg,$tempreg,<sym>	(BFD_RELOC_LO16)

	     If we have a small constant, and this is a reference to
	     an external symbol, we want
	       lui	$tempreg,<sym>		(BFD_RELOC_MIPS_GOT_HI16)
	       addu	$tempreg,$tempreg,$gp
	       lw	$tempreg,<sym>($tempreg) (BFD_RELOC_MIPS_GOT_LO16)
	       nop
	       addiu	$tempreg,$tempreg,<constant>
	     For a local symbol, we want
	       lw	$tempreg,<sym>($gp)	(BFD_RELOC_MIPS_GOT16)
	       nop
	       addiu	$tempreg,$tempreg,<constant> (BFD_RELOC_LO16)

	     If we have a large constant, and this is a reference to
	     an external symbol, we want
	       lui	$tempreg,<sym>		(BFD_RELOC_MIPS_GOT_HI16)
	       addu	$tempreg,$tempreg,$gp
	       lw	$tempreg,<sym>($tempreg) (BFD_RELOC_MIPS_GOT_LO16)
	       lui	$at,<hiconstant>
	       addiu	$at,$at,<loconstant>
	       addu	$tempreg,$tempreg,$at
	     For a local symbol, we want
	       lw	$tempreg,<sym>($gp)	(BFD_RELOC_MIPS_GOT16)
	       lui	$at,<hiconstant>
	       addiu	$at,$at,<loconstant>	(BFD_RELOC_LO16)
	       addu	$tempreg,$tempreg,$at
	  */

	  expr1.X_add_number = offset_expr.X_add_number;
	  offset_expr.X_add_number = 0;
	  relax_start (offset_expr.X_add_symbol);
	  gpdelay = reg_needs_delay (mips_gp_register);
	  if (expr1.X_add_number == 0 && breg == 0
	      && (call || tempreg == PIC_CALL_REG))
	    {
	      lui_reloc_type = (int) BFD_RELOC_MIPS_CALL_HI16;
	      lw_reloc_type = (int) BFD_RELOC_MIPS_CALL_LO16;
	    }
	  macro_build (&offset_expr, "lui", "t,u", tempreg, lui_reloc_type);
	  macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
		       tempreg, tempreg, mips_gp_register);
	  macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)",
		       tempreg, lw_reloc_type, tempreg);
	  if (expr1.X_add_number == 0)
	    {
	      if (breg != 0)
		{
		  /* We're going to put in an addu instruction using
		     tempreg, so we may as well insert the nop right
		     now.  */
		  load_delay_nop ();
		}
	    }
	  else if (expr1.X_add_number >= -0x8000
		   && expr1.X_add_number < 0x8000)
	    {
	      load_delay_nop ();
	      macro_build (&expr1, ADDRESS_ADDI_INSN, "t,r,j",
			   tempreg, tempreg, BFD_RELOC_LO16);
	    }
	  else
	    {
	      int dreg;

	      /* If we are going to add in a base register, and the
		 target register and the base register are the same,
		 then we are using AT as a temporary register.  Since
		 we want to load the constant into AT, we add our
		 current AT (from the global offset table) and the
		 register into the register now, and pretend we were
		 not using a base register.  */
	      if (breg != treg)
		dreg = tempreg;
	      else
		{
		  assert (tempreg == AT);
		  load_delay_nop ();
		  macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
			       treg, AT, breg);
		  dreg = treg;
		}

	      load_register (AT, &expr1, HAVE_64BIT_ADDRESSES);
	      macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t", dreg, dreg, AT);

	      used_at = 1;
	    }
	  offset_expr.X_add_number =
	    ((expr1.X_add_number + 0x8000) & 0xffff) - 0x8000;
	  relax_switch ();

	  if (gpdelay)
	    {
	      /* This is needed because this instruction uses $gp, but
		 the first instruction on the main stream does not.  */
	      macro_build (NULL, "nop", "");
	    }

	  macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)", tempreg,
		       local_reloc_type, mips_gp_register);
	  if (expr1.X_add_number >= -0x8000
	      && expr1.X_add_number < 0x8000)
	    {
	      load_delay_nop ();
	      macro_build (&offset_expr, ADDRESS_ADDI_INSN, "t,r,j",
			   tempreg, tempreg, BFD_RELOC_LO16);
	      /* FIXME: If add_number is 0, and there was no base
		 register, the external symbol case ended with a load,
		 so if the symbol turns out to not be external, and
		 the next instruction uses tempreg, an unnecessary nop
		 will be inserted.  */
	    }
	  else
	    {
	      if (breg == treg)
		{
		  /* We must add in the base register now, as in the
		     external symbol case.  */
		  assert (tempreg == AT);
		  load_delay_nop ();
		  macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
			       treg, AT, breg);
		  tempreg = treg;
		  /* We set breg to 0 because we have arranged to add
		     it in in both cases.  */
		  breg = 0;
		}

	      macro_build_lui (&expr1, AT);
	      macro_build (&offset_expr, ADDRESS_ADDI_INSN, "t,r,j",
			   AT, AT, BFD_RELOC_LO16);
	      macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
			   tempreg, tempreg, AT);
	      used_at = 1;
	    }
	  relax_end ();
	}
      else if (mips_big_got && HAVE_NEWABI)
	{
	  int lui_reloc_type = (int) BFD_RELOC_MIPS_GOT_HI16;
	  int lw_reloc_type = (int) BFD_RELOC_MIPS_GOT_LO16;
	  int add_breg_early = 0;

	  /* This is the large GOT case.  If this is a reference to an
	     external symbol, and there is no constant, we want
	       lui	$tempreg,<sym>		(BFD_RELOC_MIPS_GOT_HI16)
	       add	$tempreg,$tempreg,$gp
	       lw	$tempreg,<sym>($tempreg) (BFD_RELOC_MIPS_GOT_LO16)
	     or for lca or if tempreg is PIC_CALL_REG
	       lui	$tempreg,<sym>		(BFD_RELOC_MIPS_CALL_HI16)
	       add	$tempreg,$tempreg,$gp
	       lw	$tempreg,<sym>($tempreg) (BFD_RELOC_MIPS_CALL_LO16)

	     If we have a small constant, and this is a reference to
	     an external symbol, we want
	       lui	$tempreg,<sym>		(BFD_RELOC_MIPS_GOT_HI16)
	       add	$tempreg,$tempreg,$gp
	       lw	$tempreg,<sym>($tempreg) (BFD_RELOC_MIPS_GOT_LO16)
	       addi	$tempreg,$tempreg,<constant>

	     If we have a large constant, and this is a reference to
	     an external symbol, we want
	       lui	$tempreg,<sym>		(BFD_RELOC_MIPS_GOT_HI16)
	       addu	$tempreg,$tempreg,$gp
	       lw	$tempreg,<sym>($tempreg) (BFD_RELOC_MIPS_GOT_LO16)
	       lui	$at,<hiconstant>
	       addi	$at,$at,<loconstant>
	       add	$tempreg,$tempreg,$at

	     If we have NewABI, and we know it's a local symbol, we want
	       lw	$reg,<sym>($gp)		(BFD_RELOC_MIPS_GOT_PAGE)
	       addiu	$reg,$reg,<sym>		(BFD_RELOC_MIPS_GOT_OFST)
	     otherwise we have to resort to GOT_HI16/GOT_LO16.  */

	  relax_start (offset_expr.X_add_symbol);

	  expr1.X_add_number = offset_expr.X_add_number;
	  offset_expr.X_add_number = 0;

	  if (expr1.X_add_number == 0 && breg == 0
	      && (call || tempreg == PIC_CALL_REG))
	    {
	      lui_reloc_type = (int) BFD_RELOC_MIPS_CALL_HI16;
	      lw_reloc_type = (int) BFD_RELOC_MIPS_CALL_LO16;
	    }
	  macro_build (&offset_expr, "lui", "t,u", tempreg, lui_reloc_type);
	  macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
		       tempreg, tempreg, mips_gp_register);
	  macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)",
		       tempreg, lw_reloc_type, tempreg);

	  if (expr1.X_add_number == 0)
	    ;
	  else if (expr1.X_add_number >= -0x8000
		   && expr1.X_add_number < 0x8000)
	    {
	      macro_build (&expr1, ADDRESS_ADDI_INSN, "t,r,j",
			   tempreg, tempreg, BFD_RELOC_LO16);
	    }
	  else if (IS_SEXT_32BIT_NUM (expr1.X_add_number + 0x8000))
	    {
	      int dreg;

	      /* If we are going to add in a base register, and the
		 target register and the base register are the same,
		 then we are using AT as a temporary register.  Since
		 we want to load the constant into AT, we add our
		 current AT (from the global offset table) and the
		 register into the register now, and pretend we were
		 not using a base register.  */
	      if (breg != treg)
		dreg = tempreg;
	      else
		{
		  assert (tempreg == AT);
		  macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
			       treg, AT, breg);
		  dreg = treg;
		  add_breg_early = 1;
		}

	      load_register (AT, &expr1, HAVE_64BIT_ADDRESSES);
	      macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t", dreg, dreg, AT);

	      used_at = 1;
	    }
	  else
	    as_bad (_("PIC code offset overflow (max 32 signed bits)"));

	  relax_switch ();
	  offset_expr.X_add_number = expr1.X_add_number;
	  macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)", tempreg,
		       BFD_RELOC_MIPS_GOT_PAGE, mips_gp_register);
	  macro_build (&offset_expr, ADDRESS_ADDI_INSN, "t,r,j", tempreg,
		       tempreg, BFD_RELOC_MIPS_GOT_OFST);
	  if (add_breg_early)
	    {
	      macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
			   treg, tempreg, breg);
	      breg = 0;
	      tempreg = treg;
	    }
	  relax_end ();
	}
      else
	abort ();

      if (breg != 0)
	macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t", treg, tempreg, breg);
      break;

    case M_J_A:
      /* The j instruction may not be used in PIC code, since it
	 requires an absolute address.  We convert it to a b
	 instruction.  */
      if (mips_pic == NO_PIC)
	macro_build (&offset_expr, "j", "a");
      else
	macro_build (&offset_expr, "b", "p");
      break;

      /* The jal instructions must be handled as macros because when
	 generating PIC code they expand to multi-instruction
	 sequences.  Normally they are simple instructions.  */
    case M_JAL_1:
      dreg = RA;
      /* Fall through.  */
    case M_JAL_2:
      if (mips_pic == NO_PIC)
	macro_build (NULL, "jalr", "d,s", dreg, sreg);
      else
	{
	  if (sreg != PIC_CALL_REG)
	    as_warn (_("MIPS PIC call to register other than $25"));

	  macro_build (NULL, "jalr", "d,s", dreg, sreg);
	  if (mips_pic == SVR4_PIC && !HAVE_NEWABI)
	    {
	      if (mips_cprestore_offset < 0)
		as_warn (_("No .cprestore pseudo-op used in PIC code"));
	      else
		{
		  if (! mips_frame_reg_valid)
		    {
		      as_warn (_("No .frame pseudo-op used in PIC code"));
		      /* Quiet this warning.  */
		      mips_frame_reg_valid = 1;
		    }
		  if (! mips_cprestore_valid)
		    {
		      as_warn (_("No .cprestore pseudo-op used in PIC code"));
		      /* Quiet this warning.  */
		      mips_cprestore_valid = 1;
		    }
		  expr1.X_add_number = mips_cprestore_offset;
  		  macro_build_ldst_constoffset (&expr1, ADDRESS_LOAD_INSN,
						mips_gp_register,
						mips_frame_reg,
						HAVE_64BIT_ADDRESSES);
		}
	    }
	}

      break;

    case M_JAL_A:
      if (mips_pic == NO_PIC)
	macro_build (&offset_expr, "jal", "a");
      else if (mips_pic == SVR4_PIC)
	{
	  /* If this is a reference to an external symbol, and we are
	     using a small GOT, we want
	       lw	$25,<sym>($gp)		(BFD_RELOC_MIPS_CALL16)
	       nop
	       jalr	$ra,$25
	       nop
	       lw	$gp,cprestore($sp)
	     The cprestore value is set using the .cprestore
	     pseudo-op.  If we are using a big GOT, we want
	       lui	$25,<sym>		(BFD_RELOC_MIPS_CALL_HI16)
	       addu	$25,$25,$gp
	       lw	$25,<sym>($25)		(BFD_RELOC_MIPS_CALL_LO16)
	       nop
	       jalr	$ra,$25
	       nop
	       lw	$gp,cprestore($sp)
	     If the symbol is not external, we want
	       lw	$25,<sym>($gp)		(BFD_RELOC_MIPS_GOT16)
	       nop
	       addiu	$25,$25,<sym>		(BFD_RELOC_LO16)
	       jalr	$ra,$25
	       nop
	       lw $gp,cprestore($sp)

	     For NewABI, we use the same CALL16 or CALL_HI16/CALL_LO16
	     sequences above, minus nops, unless the symbol is local,
	     which enables us to use GOT_PAGE/GOT_OFST (big got) or
	     GOT_DISP.  */
	  if (HAVE_NEWABI)
	    {
	      if (! mips_big_got)
		{
		  relax_start (offset_expr.X_add_symbol);
		  macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)",
			       PIC_CALL_REG, BFD_RELOC_MIPS_CALL16,
			       mips_gp_register);
		  relax_switch ();
		  macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)",
			       PIC_CALL_REG, BFD_RELOC_MIPS_GOT_DISP,
			       mips_gp_register);
		  relax_end ();
		}
	      else
		{
		  relax_start (offset_expr.X_add_symbol);
		  macro_build (&offset_expr, "lui", "t,u", PIC_CALL_REG,
			       BFD_RELOC_MIPS_CALL_HI16);
		  macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t", PIC_CALL_REG,
			       PIC_CALL_REG, mips_gp_register);
		  macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)",
			       PIC_CALL_REG, BFD_RELOC_MIPS_CALL_LO16,
			       PIC_CALL_REG);
		  relax_switch ();
		  macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)",
			       PIC_CALL_REG, BFD_RELOC_MIPS_GOT_PAGE,
			       mips_gp_register);
		  macro_build (&offset_expr, ADDRESS_ADDI_INSN, "t,r,j",
			       PIC_CALL_REG, PIC_CALL_REG,
			       BFD_RELOC_MIPS_GOT_OFST);
		  relax_end ();
		}

	      macro_build_jalr (&offset_expr);
	    }
	  else
	    {
	      relax_start (offset_expr.X_add_symbol);
	      if (! mips_big_got)
		{
		  macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)",
			       PIC_CALL_REG, BFD_RELOC_MIPS_CALL16,
			       mips_gp_register);
		  load_delay_nop ();
		  relax_switch ();
		}
	      else
		{
		  int gpdelay;

		  gpdelay = reg_needs_delay (mips_gp_register);
		  macro_build (&offset_expr, "lui", "t,u", PIC_CALL_REG,
			       BFD_RELOC_MIPS_CALL_HI16);
		  macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t", PIC_CALL_REG,
			       PIC_CALL_REG, mips_gp_register);
		  macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)",
			       PIC_CALL_REG, BFD_RELOC_MIPS_CALL_LO16,
			       PIC_CALL_REG);
		  load_delay_nop ();
		  relax_switch ();
		  if (gpdelay)
		    macro_build (NULL, "nop", "");
		}
	      macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)",
			   PIC_CALL_REG, BFD_RELOC_MIPS_GOT16,
			   mips_gp_register);
	      load_delay_nop ();
	      macro_build (&offset_expr, ADDRESS_ADDI_INSN, "t,r,j",
			   PIC_CALL_REG, PIC_CALL_REG, BFD_RELOC_LO16);
	      relax_end ();
	      macro_build_jalr (&offset_expr);

	      if (mips_cprestore_offset < 0)
		as_warn (_("No .cprestore pseudo-op used in PIC code"));
	      else
		{
		  if (! mips_frame_reg_valid)
		    {
		      as_warn (_("No .frame pseudo-op used in PIC code"));
		      /* Quiet this warning.  */
		      mips_frame_reg_valid = 1;
		    }
		  if (! mips_cprestore_valid)
		    {
		      as_warn (_("No .cprestore pseudo-op used in PIC code"));
		      /* Quiet this warning.  */
		      mips_cprestore_valid = 1;
		    }
		  if (mips_opts.noreorder)
		    macro_build (NULL, "nop", "");
		  expr1.X_add_number = mips_cprestore_offset;
  		  macro_build_ldst_constoffset (&expr1, ADDRESS_LOAD_INSN,
						mips_gp_register,
						mips_frame_reg,
						HAVE_64BIT_ADDRESSES);
		}
	    }
	}
      else if (mips_pic == VXWORKS_PIC)
	as_bad (_("Non-PIC jump used in PIC library"));
      else
	abort ();

      break;

    case M_LB_AB:
      s = "lb";
      goto ld;
    case M_LBU_AB:
      s = "lbu";
      goto ld;
    case M_LH_AB:
      s = "lh";
      goto ld;
    case M_LHU_AB:
      s = "lhu";
      goto ld;
    case M_LW_AB:
      s = "lw";
      goto ld;
    case M_LWC0_AB:
      s = "lwc0";
      /* Itbl support may require additional care here.  */
      coproc = 1;
      goto ld;
    case M_LWC1_AB:
      s = "lwc1";
      /* Itbl support may require additional care here.  */
      coproc = 1;
      goto ld;
    case M_LWC2_AB:
      s = "lwc2";
      /* Itbl support may require additional care here.  */
      coproc = 1;
      goto ld;
    case M_LWC3_AB:
      s = "lwc3";
      /* Itbl support may require additional care here.  */
      coproc = 1;
      goto ld;
    case M_LWL_AB:
      s = "lwl";
      lr = 1;
      goto ld;
    case M_LWR_AB:
      s = "lwr";
      lr = 1;
      goto ld;
    case M_LDC1_AB:
      if (mips_opts.arch == CPU_R4650)
	{
	  as_bad (_("opcode not supported on this processor"));
	  break;
	}
      s = "ldc1";
      /* Itbl support may require additional care here.  */
      coproc = 1;
      goto ld;
    case M_LDC2_AB:
      s = "ldc2";
      /* Itbl support may require additional care here.  */
      coproc = 1;
      goto ld;
    case M_LDC3_AB:
      s = "ldc3";
      /* Itbl support may require additional care here.  */
      coproc = 1;
      goto ld;
    case M_LDL_AB:
      s = "ldl";
      lr = 1;
      goto ld;
    case M_LDR_AB:
      s = "ldr";
      lr = 1;
      goto ld;
    case M_LL_AB:
      s = "ll";
      goto ld;
    case M_LLD_AB:
      s = "lld";
      goto ld;
    case M_LWU_AB:
      s = "lwu";
    ld:
      if (mips_opts.arch == CPU_OCTEON
	   && octeon_error_on_unsupported
           && (mask == M_LDC1_AB || mask == M_LDC2_AB || mask == M_LDC3_AB
               || mask == M_L_DOB || mask == M_L_DAB
               || mask == M_LI_D || mask == M_LI_DD
               || mask == M_LI_S || mask == M_LI_SS))
        {
          as_bad (_("opcode not implemented in Octeon `%s'"), ip->insn_mo->name);
          return;
        }
      if (breg == treg || coproc || lr)
	{
	  tempreg = AT;
	  used_at = 1;
	}
      else
	{
	  tempreg = treg;
	}
      goto ld_st;
    case M_SB_AB:
      s = "sb";
      goto st;
    case M_SH_AB:
      s = "sh";
      goto st;
    case M_SW_AB:
      s = "sw";
      goto st;
    case M_SWC0_AB:
      s = "swc0";
      /* Itbl support may require additional care here.  */
      coproc = 1;
      goto st;
    case M_SWC1_AB:
      s = "swc1";
      /* Itbl support may require additional care here.  */
      coproc = 1;
      goto st;
    case M_SWC2_AB:
      s = "swc2";
      /* Itbl support may require additional care here.  */
      coproc = 1;
      goto st;
    case M_SWC3_AB:
      s = "swc3";
      /* Itbl support may require additional care here.  */
      coproc = 1;
      goto st;
    case M_SWL_AB:
      s = "swl";
      goto st;
    case M_SWR_AB:
      s = "swr";
      goto st;
    case M_SC_AB:
      s = "sc";
      goto st;
    case M_SCD_AB:
      s = "scd";
      goto st;
    case M_CACHE_AB:
      s = "cache";
      goto st;
    case M_SDC1_AB:
      if (mips_opts.arch == CPU_R4650)
	{
	  as_bad (_("opcode not supported on this processor"));
	  break;
	}
      s = "sdc1";
      coproc = 1;
      /* Itbl support may require additional care here.  */
      goto st;
    case M_SDC2_AB:
      s = "sdc2";
      /* Itbl support may require additional care here.  */
      coproc = 1;
      goto st;
    case M_SDC3_AB:
      s = "sdc3";
      /* Itbl support may require additional care here.  */
      coproc = 1;
      goto st;
    case M_SDL_AB:
      s = "sdl";
      goto st;
    case M_SDR_AB:
      s = "sdr";
    st:
      if (mips_opts.arch == CPU_OCTEON
	  && octeon_error_on_unsupported
          && (mask == M_SWC0_AB || mask == M_SWC1_AB || mask == M_SWC2_AB
              || mask == M_SDC1_AB || mask == M_SDC2_AB || mask == M_SDC3_AB
              || mask == M_S_DAB || mask == M_S_DOB))
        {
          as_bad (_("opcode not implemented in Octeon `%s'"), ip->insn_mo->name);
          return;
        }
      tempreg = AT;
      used_at = 1;
    ld_st:
      /* Itbl support may require additional care here.  */
      if (mask == M_LWC1_AB
	  || mask == M_SWC1_AB
	  || mask == M_LDC1_AB
	  || mask == M_SDC1_AB
	  || mask == M_L_DAB
	  || mask == M_S_DAB)
	fmt = "T,o(b)";
      else if (mask == M_CACHE_AB)
	fmt = "k,o(b)";
      else if (coproc)
	fmt = "E,o(b)";
      else
	fmt = "t,o(b)";

      if (offset_expr.X_op != O_constant
	  && offset_expr.X_op != O_symbol)
	{
	  as_bad (_("expression too complex"));
	  offset_expr.X_op = O_constant;
	}

      if (HAVE_32BIT_ADDRESSES
	  && !IS_SEXT_32BIT_NUM (offset_expr.X_add_number))
	{
	  char value [32];

	  sprintf_vma (value, offset_expr.X_add_number);
	  as_bad (_("Number (0x%s) larger than 32 bits"), value);
	}

      /* A constant expression in PIC code can be handled just as it
	 is in non PIC code.  */
      if (offset_expr.X_op == O_constant)
	{
	  expr1.X_add_number = ((offset_expr.X_add_number + 0x8000)
				& ~(bfd_vma) 0xffff);
	  normalize_address_expr (&expr1);
	  load_register (tempreg, &expr1, HAVE_64BIT_ADDRESSES);
	  if (breg != 0)
	    macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
			 tempreg, tempreg, breg);
	  macro_build (&offset_expr, s, fmt, treg, BFD_RELOC_LO16, tempreg);
	}
      else if (mips_pic == NO_PIC)
	{
	  /* If this is a reference to a GP relative symbol, and there
	     is no base register, we want
	       <op>	$treg,<sym>($gp)	(BFD_RELOC_GPREL16)
	     Otherwise, if there is no base register, we want
	       lui	$tempreg,<sym>		(BFD_RELOC_HI16_S)
	       <op>	$treg,<sym>($tempreg)	(BFD_RELOC_LO16)
	     If we have a constant, we need two instructions anyhow,
	     so we always use the latter form.

	     If we have a base register, and this is a reference to a
	     GP relative symbol, we want
	       addu	$tempreg,$breg,$gp
	       <op>	$treg,<sym>($tempreg)	(BFD_RELOC_GPREL16)
	     Otherwise we want
	       lui	$tempreg,<sym>		(BFD_RELOC_HI16_S)
	       addu	$tempreg,$tempreg,$breg
	       <op>	$treg,<sym>($tempreg)	(BFD_RELOC_LO16)
	     With a constant we always use the latter case.

	     With 64bit address space and no base register and $at usable,
	     we want
	       lui	$tempreg,<sym>		(BFD_RELOC_MIPS_HIGHEST)
	       lui	$at,<sym>		(BFD_RELOC_HI16_S)
	       daddiu	$tempreg,<sym>		(BFD_RELOC_MIPS_HIGHER)
	       dsll32	$tempreg,0
	       daddu	$tempreg,$at
	       <op>	$treg,<sym>($tempreg)	(BFD_RELOC_LO16)
	     If we have a base register, we want
	       lui	$tempreg,<sym>		(BFD_RELOC_MIPS_HIGHEST)
	       lui	$at,<sym>		(BFD_RELOC_HI16_S)
	       daddiu	$tempreg,<sym>		(BFD_RELOC_MIPS_HIGHER)
	       daddu	$at,$breg
	       dsll32	$tempreg,0
	       daddu	$tempreg,$at
	       <op>	$treg,<sym>($tempreg)	(BFD_RELOC_LO16)

	     Without $at we can't generate the optimal path for superscalar
	     processors here since this would require two temporary registers.
	       lui	$tempreg,<sym>		(BFD_RELOC_MIPS_HIGHEST)
	       daddiu	$tempreg,<sym>		(BFD_RELOC_MIPS_HIGHER)
	       dsll	$tempreg,16
	       daddiu	$tempreg,<sym>		(BFD_RELOC_HI16_S)
	       dsll	$tempreg,16
	       <op>	$treg,<sym>($tempreg)	(BFD_RELOC_LO16)
	     If we have a base register, we want
	       lui	$tempreg,<sym>		(BFD_RELOC_MIPS_HIGHEST)
	       daddiu	$tempreg,<sym>		(BFD_RELOC_MIPS_HIGHER)
	       dsll	$tempreg,16
	       daddiu	$tempreg,<sym>		(BFD_RELOC_HI16_S)
	       dsll	$tempreg,16
	       daddu	$tempreg,$tempreg,$breg
	       <op>	$treg,<sym>($tempreg)	(BFD_RELOC_LO16)

	     For GP relative symbols in 64bit address space we can use
	     the same sequence as in 32bit address space.  */
	  if (HAVE_64BIT_SYMBOLS)
	    {
	      if ((valueT) offset_expr.X_add_number <= MAX_GPREL_OFFSET
		  && !nopic_need_relax (offset_expr.X_add_symbol, 1))
		{
		  relax_start (offset_expr.X_add_symbol);
		  if (breg == 0)
		    {
		      macro_build (&offset_expr, s, fmt, treg,
				   BFD_RELOC_GPREL16, mips_gp_register);
		    }
		  else
		    {
		      macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
				   tempreg, breg, mips_gp_register);
		      macro_build (&offset_expr, s, fmt, treg,
				   BFD_RELOC_GPREL16, tempreg);
		    }
		  relax_switch ();
		}

	      if (used_at == 0 && !mips_opts.noat)
		{
		  macro_build (&offset_expr, "lui", "t,u", tempreg,
			       BFD_RELOC_MIPS_HIGHEST);
		  macro_build (&offset_expr, "lui", "t,u", AT,
			       BFD_RELOC_HI16_S);
		  macro_build (&offset_expr, "daddiu", "t,r,j", tempreg,
			       tempreg, BFD_RELOC_MIPS_HIGHER);
		  if (breg != 0)
		    macro_build (NULL, "daddu", "d,v,t", AT, AT, breg);
		  macro_build (NULL, "dsll32", "d,w,<", tempreg, tempreg, 0);
		  macro_build (NULL, "daddu", "d,v,t", tempreg, tempreg, AT);
		  macro_build (&offset_expr, s, fmt, treg, BFD_RELOC_LO16,
			       tempreg);
		  used_at = 1;
		}
	      else
		{
		  macro_build (&offset_expr, "lui", "t,u", tempreg,
			       BFD_RELOC_MIPS_HIGHEST);
		  macro_build (&offset_expr, "daddiu", "t,r,j", tempreg,
			       tempreg, BFD_RELOC_MIPS_HIGHER);
		  macro_build (NULL, "dsll", "d,w,<", tempreg, tempreg, 16);
		  macro_build (&offset_expr, "daddiu", "t,r,j", tempreg,
			       tempreg, BFD_RELOC_HI16_S);
		  macro_build (NULL, "dsll", "d,w,<", tempreg, tempreg, 16);
		  if (breg != 0)
		    macro_build (NULL, "daddu", "d,v,t",
				 tempreg, tempreg, breg);
		  macro_build (&offset_expr, s, fmt, treg,
			       BFD_RELOC_LO16, tempreg);
		}

	      if (mips_relax.sequence)
		relax_end ();
	      break;
	    }

	  if (breg == 0)
	    {
	      if ((valueT) offset_expr.X_add_number <= MAX_GPREL_OFFSET
		  && !nopic_need_relax (offset_expr.X_add_symbol, 1))
		{
		  relax_start (offset_expr.X_add_symbol);
		  macro_build (&offset_expr, s, fmt, treg, BFD_RELOC_GPREL16,
			       mips_gp_register);
		  relax_switch ();
		}
	      macro_build_lui (&offset_expr, tempreg);
	      macro_build (&offset_expr, s, fmt, treg,
			   BFD_RELOC_LO16, tempreg);
	      if (mips_relax.sequence)
		relax_end ();
	    }
	  else
	    {
	      if ((valueT) offset_expr.X_add_number <= MAX_GPREL_OFFSET
		  && !nopic_need_relax (offset_expr.X_add_symbol, 1))
		{
		  relax_start (offset_expr.X_add_symbol);
		  macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
			       tempreg, breg, mips_gp_register);
		  macro_build (&offset_expr, s, fmt, treg,
			       BFD_RELOC_GPREL16, tempreg);
		  relax_switch ();
		}
	      macro_build_lui (&offset_expr, tempreg);
	      macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
			   tempreg, tempreg, breg);
	      macro_build (&offset_expr, s, fmt, treg,
			   BFD_RELOC_LO16, tempreg);
	      if (mips_relax.sequence)
		relax_end ();
	    }
	}
      else if (!mips_big_got)
	{
	  int lw_reloc_type = (int) BFD_RELOC_MIPS_GOT16;

	  /* If this is a reference to an external symbol, we want
	       lw	$tempreg,<sym>($gp)	(BFD_RELOC_MIPS_GOT16)
	       nop
	       <op>	$treg,0($tempreg)
	     Otherwise we want
	       lw	$tempreg,<sym>($gp)	(BFD_RELOC_MIPS_GOT16)
	       nop
	       addiu	$tempreg,$tempreg,<sym>	(BFD_RELOC_LO16)
	       <op>	$treg,0($tempreg)

	     For NewABI, we want
	       lw	$tempreg,<sym>($gp)	(BFD_RELOC_MIPS_GOT_PAGE)
	       <op>	$treg,<sym>($tempreg)   (BFD_RELOC_MIPS_GOT_OFST)

	     If there is a base register, we add it to $tempreg before
	     the <op>.  If there is a constant, we stick it in the
	     <op> instruction.  We don't handle constants larger than
	     16 bits, because we have no way to load the upper 16 bits
	     (actually, we could handle them for the subset of cases
	     in which we are not using $at).  */
	  assert (offset_expr.X_op == O_symbol);
	  if (HAVE_NEWABI)
	    {
	      macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)", tempreg,
			   BFD_RELOC_MIPS_GOT_PAGE, mips_gp_register);
	      if (breg != 0)
		macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
			     tempreg, tempreg, breg);
	      macro_build (&offset_expr, s, fmt, treg,
			   BFD_RELOC_MIPS_GOT_OFST, tempreg);
	      break;
	    }
	  expr1.X_add_number = offset_expr.X_add_number;
	  offset_expr.X_add_number = 0;
	  if (expr1.X_add_number < -0x8000
	      || expr1.X_add_number >= 0x8000)
	    as_bad (_("PIC code offset overflow (max 16 signed bits)"));
	  macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)", tempreg,
		       lw_reloc_type, mips_gp_register);
	  load_delay_nop ();
	  relax_start (offset_expr.X_add_symbol);
	  relax_switch ();
	  macro_build (&offset_expr, ADDRESS_ADDI_INSN, "t,r,j", tempreg,
		       tempreg, BFD_RELOC_LO16);
	  relax_end ();
	  if (breg != 0)
	    macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
			 tempreg, tempreg, breg);
	  macro_build (&expr1, s, fmt, treg, BFD_RELOC_LO16, tempreg);
	}
      else if (mips_big_got && !HAVE_NEWABI)
	{
	  int gpdelay;

	  /* If this is a reference to an external symbol, we want
	       lui	$tempreg,<sym>		(BFD_RELOC_MIPS_GOT_HI16)
	       addu	$tempreg,$tempreg,$gp
	       lw	$tempreg,<sym>($tempreg) (BFD_RELOC_MIPS_GOT_LO16)
	       <op>	$treg,0($tempreg)
	     Otherwise we want
	       lw	$tempreg,<sym>($gp)	(BFD_RELOC_MIPS_GOT16)
	       nop
	       addiu	$tempreg,$tempreg,<sym>	(BFD_RELOC_LO16)
	       <op>	$treg,0($tempreg)
	     If there is a base register, we add it to $tempreg before
	     the <op>.  If there is a constant, we stick it in the
	     <op> instruction.  We don't handle constants larger than
	     16 bits, because we have no way to load the upper 16 bits
	     (actually, we could handle them for the subset of cases
	     in which we are not using $at).  */
	  assert (offset_expr.X_op == O_symbol);
	  expr1.X_add_number = offset_expr.X_add_number;
	  offset_expr.X_add_number = 0;
	  if (expr1.X_add_number < -0x8000
	      || expr1.X_add_number >= 0x8000)
	    as_bad (_("PIC code offset overflow (max 16 signed bits)"));
	  gpdelay = reg_needs_delay (mips_gp_register);
	  relax_start (offset_expr.X_add_symbol);
	  macro_build (&offset_expr, "lui", "t,u", tempreg,
		       BFD_RELOC_MIPS_GOT_HI16);
	  macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t", tempreg, tempreg,
		       mips_gp_register);
	  macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)", tempreg,
		       BFD_RELOC_MIPS_GOT_LO16, tempreg);
	  relax_switch ();
	  if (gpdelay)
	    macro_build (NULL, "nop", "");
	  macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)", tempreg,
		       BFD_RELOC_MIPS_GOT16, mips_gp_register);
	  load_delay_nop ();
	  macro_build (&offset_expr, ADDRESS_ADDI_INSN, "t,r,j", tempreg,
		       tempreg, BFD_RELOC_LO16);
	  relax_end ();

	  if (breg != 0)
	    macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
			 tempreg, tempreg, breg);
	  macro_build (&expr1, s, fmt, treg, BFD_RELOC_LO16, tempreg);
	}
      else if (mips_big_got && HAVE_NEWABI)
	{
	  /* If this is a reference to an external symbol, we want
	       lui	$tempreg,<sym>		(BFD_RELOC_MIPS_GOT_HI16)
	       add	$tempreg,$tempreg,$gp
	       lw	$tempreg,<sym>($tempreg) (BFD_RELOC_MIPS_GOT_LO16)
	       <op>	$treg,<ofst>($tempreg)
	     Otherwise, for local symbols, we want:
	       lw	$tempreg,<sym>($gp)	(BFD_RELOC_MIPS_GOT_PAGE)
	       <op>	$treg,<sym>($tempreg)   (BFD_RELOC_MIPS_GOT_OFST)  */
	  assert (offset_expr.X_op == O_symbol);
	  expr1.X_add_number = offset_expr.X_add_number;
	  offset_expr.X_add_number = 0;
	  if (expr1.X_add_number < -0x8000
	      || expr1.X_add_number >= 0x8000)
	    as_bad (_("PIC code offset overflow (max 16 signed bits)"));
	  relax_start (offset_expr.X_add_symbol);
	  macro_build (&offset_expr, "lui", "t,u", tempreg,
		       BFD_RELOC_MIPS_GOT_HI16);
	  macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t", tempreg, tempreg,
		       mips_gp_register);
	  macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)", tempreg,
		       BFD_RELOC_MIPS_GOT_LO16, tempreg);
	  if (breg != 0)
	    macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
			 tempreg, tempreg, breg);
	  macro_build (&expr1, s, fmt, treg, BFD_RELOC_LO16, tempreg);

	  relax_switch ();
	  offset_expr.X_add_number = expr1.X_add_number;
	  macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)", tempreg,
		       BFD_RELOC_MIPS_GOT_PAGE, mips_gp_register);
	  if (breg != 0)
	    macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
			 tempreg, tempreg, breg);
	  macro_build (&offset_expr, s, fmt, treg,
		       BFD_RELOC_MIPS_GOT_OFST, tempreg);
	  relax_end ();
	}
      else
	abort ();

      break;

    case M_LI:
    case M_LI_S:
      load_register (treg, &imm_expr, 0);
      break;

    case M_DLI:
      load_register (treg, &imm_expr, 1);
      break;

    case M_LI_SS:
      if (imm_expr.X_op == O_constant)
	{
	  used_at = 1;
	  load_register (AT, &imm_expr, 0);
	  macro_build (NULL, "mtc1", "t,G", AT, treg);
	  break;
	}
      else
	{
	  assert (offset_expr.X_op == O_symbol
		  && strcmp (segment_name (S_GET_SEGMENT
					   (offset_expr.X_add_symbol)),
			     ".lit4") == 0
		  && offset_expr.X_add_number == 0);
	  macro_build (&offset_expr, "lwc1", "T,o(b)", treg,
		       BFD_RELOC_MIPS_LITERAL, mips_gp_register);
	  break;
	}

    case M_LI_D:
      /* Check if we have a constant in IMM_EXPR.  If the GPRs are 64 bits
         wide, IMM_EXPR is the entire value.  Otherwise IMM_EXPR is the high
         order 32 bits of the value and the low order 32 bits are either
         zero or in OFFSET_EXPR.  */
      if (imm_expr.X_op == O_constant || imm_expr.X_op == O_big)
	{
	  if (HAVE_64BIT_GPRS)
	    load_register (treg, &imm_expr, 1);
	  else
	    {
	      int hreg, lreg;

	      if (target_big_endian)
		{
		  hreg = treg;
		  lreg = treg + 1;
		}
	      else
		{
		  hreg = treg + 1;
		  lreg = treg;
		}

	      if (hreg <= 31)
		load_register (hreg, &imm_expr, 0);
	      if (lreg <= 31)
		{
		  if (offset_expr.X_op == O_absent)
		    move_register (lreg, 0);
		  else
		    {
		      assert (offset_expr.X_op == O_constant);
		      load_register (lreg, &offset_expr, 0);
		    }
		}
	    }
	  break;
	}

      /* We know that sym is in the .rdata section.  First we get the
	 upper 16 bits of the address.  */
      if (mips_pic == NO_PIC)
	{
	  macro_build_lui (&offset_expr, AT);
	  used_at = 1;
	}
      else
	{
	  macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)", AT,
		       BFD_RELOC_MIPS_GOT16, mips_gp_register);
	  used_at = 1;
	}

      /* Now we load the register(s).  */
      if (HAVE_64BIT_GPRS)
	{
	  used_at = 1;
	  macro_build (&offset_expr, "ld", "t,o(b)", treg, BFD_RELOC_LO16, AT);
	}
      else
	{
	  used_at = 1;
	  macro_build (&offset_expr, "lw", "t,o(b)", treg, BFD_RELOC_LO16, AT);
	  if (treg != RA)
	    {
	      /* FIXME: How in the world do we deal with the possible
		 overflow here?  */
	      offset_expr.X_add_number += 4;
	      macro_build (&offset_expr, "lw", "t,o(b)",
			   treg + 1, BFD_RELOC_LO16, AT);
	    }
	}
      break;

    case M_LI_DD:
      /* Check if we have a constant in IMM_EXPR.  If the FPRs are 64 bits
         wide, IMM_EXPR is the entire value and the GPRs are known to be 64
         bits wide as well.  Otherwise IMM_EXPR is the high order 32 bits of
         the value and the low order 32 bits are either zero or in
         OFFSET_EXPR.  */
      if (imm_expr.X_op == O_constant || imm_expr.X_op == O_big)
	{
	  used_at = 1;
	  load_register (AT, &imm_expr, HAVE_64BIT_FPRS);
	  if (HAVE_64BIT_FPRS)
	    {
	      assert (HAVE_64BIT_GPRS);
	      macro_build (NULL, "dmtc1", "t,S", AT, treg);
	    }
	  else
	    {
	      macro_build (NULL, "mtc1", "t,G", AT, treg + 1);
	      if (offset_expr.X_op == O_absent)
		macro_build (NULL, "mtc1", "t,G", 0, treg);
	      else
		{
		  assert (offset_expr.X_op == O_constant);
		  load_register (AT, &offset_expr, 0);
		  macro_build (NULL, "mtc1", "t,G", AT, treg);
		}
	    }
	  break;
	}

      assert (offset_expr.X_op == O_symbol
	      && offset_expr.X_add_number == 0);
      s = segment_name (S_GET_SEGMENT (offset_expr.X_add_symbol));
      if (strcmp (s, ".lit8") == 0)
	{
	  if (mips_opts.isa != ISA_MIPS1)
	    {
	      macro_build (&offset_expr, "ldc1", "T,o(b)", treg,
			   BFD_RELOC_MIPS_LITERAL, mips_gp_register);
	      break;
	    }
	  breg = mips_gp_register;
	  r = BFD_RELOC_MIPS_LITERAL;
	  goto dob;
	}
      else
	{
	  assert (strcmp (s, RDATA_SECTION_NAME) == 0);
	  used_at = 1;
	  if (mips_pic != NO_PIC)
	    macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)", AT,
			 BFD_RELOC_MIPS_GOT16, mips_gp_register);
	  else
	    {
	      /* FIXME: This won't work for a 64 bit address.  */
	      macro_build_lui (&offset_expr, AT);
	    }

	  if (mips_opts.isa != ISA_MIPS1)
	    {
	      macro_build (&offset_expr, "ldc1", "T,o(b)",
			   treg, BFD_RELOC_LO16, AT);
	      break;
	    }
	  breg = AT;
	  r = BFD_RELOC_LO16;
	  goto dob;
	}

    case M_L_DOB:
      if (mips_opts.arch == CPU_R4650)
	{
	  as_bad (_("opcode not supported on this processor"));
	  break;
	}
      /* Even on a big endian machine $fn comes before $fn+1.  We have
	 to adjust when loading from memory.  */
      r = BFD_RELOC_LO16;
    dob:
      assert (mips_opts.isa == ISA_MIPS1);
      macro_build (&offset_expr, "lwc1", "T,o(b)",
		   target_big_endian ? treg + 1 : treg, r, breg);
      /* FIXME: A possible overflow which I don't know how to deal
	 with.  */
      offset_expr.X_add_number += 4;
      macro_build (&offset_expr, "lwc1", "T,o(b)",
		   target_big_endian ? treg : treg + 1, r, breg);
      break;

    case M_L_DAB:
      /*
       * The MIPS assembler seems to check for X_add_number not
       * being double aligned and generating:
       *	lui	at,%hi(foo+1)
       *	addu	at,at,v1
       *	addiu	at,at,%lo(foo+1)
       *	lwc1	f2,0(at)
       *	lwc1	f3,4(at)
       * But, the resulting address is the same after relocation so why
       * generate the extra instruction?
       */
      if (mips_opts.arch == CPU_R4650)
	{
	  as_bad (_("opcode not supported on this processor"));
	  break;
	}
      /* Itbl support may require additional care here.  */
      coproc = 1;
      if (mips_opts.isa != ISA_MIPS1)
	{
	  s = "ldc1";
	  goto ld;
	}

      s = "lwc1";
      fmt = "T,o(b)";
      goto ldd_std;

    case M_S_DAB:
      if (mips_opts.arch == CPU_R4650)
	{
	  as_bad (_("opcode not supported on this processor"));
	  break;
	}

      if (mips_opts.isa != ISA_MIPS1)
	{
	  s = "sdc1";
	  goto st;
	}

      s = "swc1";
      fmt = "T,o(b)";
      /* Itbl support may require additional care here.  */
      coproc = 1;
      goto ldd_std;

    case M_LD_AB:
      if (HAVE_64BIT_GPRS)
	{
	  s = "ld";
	  goto ld;
	}

      s = "lw";
      fmt = "t,o(b)";
      goto ldd_std;

    case M_SD_AB:
      if (HAVE_64BIT_GPRS)
	{
	  s = "sd";
	  goto st;
	}

      s = "sw";
      fmt = "t,o(b)";

    ldd_std:
      if (offset_expr.X_op != O_symbol
	  && offset_expr.X_op != O_constant)
	{
	  as_bad (_("expression too complex"));
	  offset_expr.X_op = O_constant;
	}

      if (HAVE_32BIT_ADDRESSES
	  && !IS_SEXT_32BIT_NUM (offset_expr.X_add_number))
	{
	  char value [32];

	  sprintf_vma (value, offset_expr.X_add_number);
	  as_bad (_("Number (0x%s) larger than 32 bits"), value);
	}

      /* Even on a big endian machine $fn comes before $fn+1.  We have
	 to adjust when loading from memory.  We set coproc if we must
	 load $fn+1 first.  */
      /* Itbl support may require additional care here.  */
      if (! target_big_endian)
	coproc = 0;

      if (mips_pic == NO_PIC
	  || offset_expr.X_op == O_constant)
	{
	  /* If this is a reference to a GP relative symbol, we want
	       <op>	$treg,<sym>($gp)	(BFD_RELOC_GPREL16)
	       <op>	$treg+1,<sym>+4($gp)	(BFD_RELOC_GPREL16)
	     If we have a base register, we use this
	       addu	$at,$breg,$gp
	       <op>	$treg,<sym>($at)	(BFD_RELOC_GPREL16)
	       <op>	$treg+1,<sym>+4($at)	(BFD_RELOC_GPREL16)
	     If this is not a GP relative symbol, we want
	       lui	$at,<sym>		(BFD_RELOC_HI16_S)
	       <op>	$treg,<sym>($at)	(BFD_RELOC_LO16)
	       <op>	$treg+1,<sym>+4($at)	(BFD_RELOC_LO16)
	     If there is a base register, we add it to $at after the
	     lui instruction.  If there is a constant, we always use
	     the last case.  */
	  if (offset_expr.X_op == O_symbol
	      && (valueT) offset_expr.X_add_number <= MAX_GPREL_OFFSET
	      && !nopic_need_relax (offset_expr.X_add_symbol, 1))
	    {
	      relax_start (offset_expr.X_add_symbol);
	      if (breg == 0)
		{
		  tempreg = mips_gp_register;
		}
	      else
		{
		  macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
			       AT, breg, mips_gp_register);
		  tempreg = AT;
		  used_at = 1;
		}

	      /* Itbl support may require additional care here.  */
	      macro_build (&offset_expr, s, fmt, coproc ? treg + 1 : treg,
			   BFD_RELOC_GPREL16, tempreg);
	      offset_expr.X_add_number += 4;

	      /* Set mips_optimize to 2 to avoid inserting an
                 undesired nop.  */
	      hold_mips_optimize = mips_optimize;
	      mips_optimize = 2;
	      /* Itbl support may require additional care here.  */
	      macro_build (&offset_expr, s, fmt, coproc ? treg : treg + 1,
			   BFD_RELOC_GPREL16, tempreg);
	      mips_optimize = hold_mips_optimize;

	      relax_switch ();

	      /* We just generated two relocs.  When tc_gen_reloc
		 handles this case, it will skip the first reloc and
		 handle the second.  The second reloc already has an
		 extra addend of 4, which we added above.  We must
		 subtract it out, and then subtract another 4 to make
		 the first reloc come out right.  The second reloc
		 will come out right because we are going to add 4 to
		 offset_expr when we build its instruction below.

		 If we have a symbol, then we don't want to include
		 the offset, because it will wind up being included
		 when we generate the reloc.  */

	      if (offset_expr.X_op == O_constant)
		offset_expr.X_add_number -= 8;
	      else
		{
		  offset_expr.X_add_number = -4;
		  offset_expr.X_op = O_constant;
		}
	    }
	  used_at = 1;
	  macro_build_lui (&offset_expr, AT);
	  if (breg != 0)
	    macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t", AT, breg, AT);
	  /* Itbl support may require additional care here.  */
	  macro_build (&offset_expr, s, fmt, coproc ? treg + 1 : treg,
		       BFD_RELOC_LO16, AT);
	  /* FIXME: How do we handle overflow here?  */
	  offset_expr.X_add_number += 4;
	  /* Itbl support may require additional care here.  */
	  macro_build (&offset_expr, s, fmt, coproc ? treg : treg + 1,
		       BFD_RELOC_LO16, AT);
	  if (mips_relax.sequence)
	    relax_end ();
	}
      else if (!mips_big_got)
	{
	  /* If this is a reference to an external symbol, we want
	       lw	$at,<sym>($gp)		(BFD_RELOC_MIPS_GOT16)
	       nop
	       <op>	$treg,0($at)
	       <op>	$treg+1,4($at)
	     Otherwise we want
	       lw	$at,<sym>($gp)		(BFD_RELOC_MIPS_GOT16)
	       nop
	       <op>	$treg,<sym>($at)	(BFD_RELOC_LO16)
	       <op>	$treg+1,<sym>+4($at)	(BFD_RELOC_LO16)
	     If there is a base register we add it to $at before the
	     lwc1 instructions.  If there is a constant we include it
	     in the lwc1 instructions.  */
	  used_at = 1;
	  expr1.X_add_number = offset_expr.X_add_number;
	  if (expr1.X_add_number < -0x8000
	      || expr1.X_add_number >= 0x8000 - 4)
	    as_bad (_("PIC code offset overflow (max 16 signed bits)"));
	  load_got_offset (AT, &offset_expr);
	  load_delay_nop ();
	  if (breg != 0)
	    macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t", AT, breg, AT);

	  /* Set mips_optimize to 2 to avoid inserting an undesired
             nop.  */
	  hold_mips_optimize = mips_optimize;
	  mips_optimize = 2;

	  /* Itbl support may require additional care here.  */
	  relax_start (offset_expr.X_add_symbol);
	  macro_build (&expr1, s, fmt, coproc ? treg + 1 : treg,
		       BFD_RELOC_LO16, AT);
	  expr1.X_add_number += 4;
	  macro_build (&expr1, s, fmt, coproc ? treg : treg + 1,
		       BFD_RELOC_LO16, AT);
	  relax_switch ();
	  macro_build (&offset_expr, s, fmt, coproc ? treg + 1 : treg,
		       BFD_RELOC_LO16, AT);
	  offset_expr.X_add_number += 4;
	  macro_build (&offset_expr, s, fmt, coproc ? treg : treg + 1,
		       BFD_RELOC_LO16, AT);
	  relax_end ();

	  mips_optimize = hold_mips_optimize;
	}
      else if (mips_big_got)
	{
	  int gpdelay;

	  /* If this is a reference to an external symbol, we want
	       lui	$at,<sym>		(BFD_RELOC_MIPS_GOT_HI16)
	       addu	$at,$at,$gp
	       lw	$at,<sym>($at)		(BFD_RELOC_MIPS_GOT_LO16)
	       nop
	       <op>	$treg,0($at)
	       <op>	$treg+1,4($at)
	     Otherwise we want
	       lw	$at,<sym>($gp)		(BFD_RELOC_MIPS_GOT16)
	       nop
	       <op>	$treg,<sym>($at)	(BFD_RELOC_LO16)
	       <op>	$treg+1,<sym>+4($at)	(BFD_RELOC_LO16)
	     If there is a base register we add it to $at before the
	     lwc1 instructions.  If there is a constant we include it
	     in the lwc1 instructions.  */
	  used_at = 1;
	  expr1.X_add_number = offset_expr.X_add_number;
	  offset_expr.X_add_number = 0;
	  if (expr1.X_add_number < -0x8000
	      || expr1.X_add_number >= 0x8000 - 4)
	    as_bad (_("PIC code offset overflow (max 16 signed bits)"));
	  gpdelay = reg_needs_delay (mips_gp_register);
	  relax_start (offset_expr.X_add_symbol);
	  macro_build (&offset_expr, "lui", "t,u",
		       AT, BFD_RELOC_MIPS_GOT_HI16);
	  macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t",
		       AT, AT, mips_gp_register);
	  macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)",
		       AT, BFD_RELOC_MIPS_GOT_LO16, AT);
	  load_delay_nop ();
	  if (breg != 0)
	    macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t", AT, breg, AT);
	  /* Itbl support may require additional care here.  */
	  macro_build (&expr1, s, fmt, coproc ? treg + 1 : treg,
		       BFD_RELOC_LO16, AT);
	  expr1.X_add_number += 4;

	  /* Set mips_optimize to 2 to avoid inserting an undesired
             nop.  */
	  hold_mips_optimize = mips_optimize;
	  mips_optimize = 2;
	  /* Itbl support may require additional care here.  */
	  macro_build (&expr1, s, fmt, coproc ? treg : treg + 1,
		       BFD_RELOC_LO16, AT);
	  mips_optimize = hold_mips_optimize;
	  expr1.X_add_number -= 4;

	  relax_switch ();
	  offset_expr.X_add_number = expr1.X_add_number;
	  if (gpdelay)
	    macro_build (NULL, "nop", "");
	  macro_build (&offset_expr, ADDRESS_LOAD_INSN, "t,o(b)", AT,
		       BFD_RELOC_MIPS_GOT16, mips_gp_register);
	  load_delay_nop ();
	  if (breg != 0)
	    macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t", AT, breg, AT);
	  /* Itbl support may require additional care here.  */
	  macro_build (&offset_expr, s, fmt, coproc ? treg + 1 : treg,
		       BFD_RELOC_LO16, AT);
	  offset_expr.X_add_number += 4;

	  /* Set mips_optimize to 2 to avoid inserting an undesired
             nop.  */
	  hold_mips_optimize = mips_optimize;
	  mips_optimize = 2;
	  /* Itbl support may require additional care here.  */
	  macro_build (&offset_expr, s, fmt, coproc ? treg : treg + 1,
		       BFD_RELOC_LO16, AT);
	  mips_optimize = hold_mips_optimize;
	  relax_end ();
	}
      else
	abort ();

      break;

    case M_LD_OB:
      s = "lw";
      goto sd_ob;
    case M_SD_OB:
      s = "sw";
    sd_ob:
      assert (HAVE_32BIT_ADDRESSES);
      macro_build (&offset_expr, s, "t,o(b)", treg, BFD_RELOC_LO16, breg);
      offset_expr.X_add_number += 4;
      macro_build (&offset_expr, s, "t,o(b)", treg + 1, BFD_RELOC_LO16, breg);
      break;

    case M_SAA_AB:
      s = "saa";
      goto saa_saad;

    case M_SAAD_AB:
      s = "saad";

      saa_saad:
      /* The "saa/saad" instructions are new in CN58XX. These instructions
	 do not specify offset. When invoked with address or symbol, then
	 load the address or value of symbol in a register using the dla macro
	 into AT, and pass the register for emitting "saa/saad" instruction. 
	 This will get expanded to

	    dla AT, constant/label
	    saa/saad $treg,(AT)  */
      {
	char *name = "dla";
	char *fmt = "t,A(b)";
	const struct mips_opcode *mo;
  	struct mips_cl_insn insn;

	mo = hash_find (op_hash, name);
	assert (strcmp (name, mo->name) == 0);
	assert (strcmp (fmt, mo->args) == 0);
	create_insn (&insn, mo);
  
	insn.insn_opcode = insn.insn_mo->match;

	used_at = 1;
	INSERT_OPERAND (RT, insn, AT);
	if (breg)
	  INSERT_OPERAND (RS, insn, breg);

	/* The address part is forwarded through the global offset_expr. */
	macro (&insn);

	macro_build (NULL, s, "t,(b)", treg, AT);
	break;
     }
 
   /* New code added to support COPZ instructions.
      This code builds table entries out of the macros in mip_opcodes.
      R4000 uses interlocks to handle coproc delays.
      Other chips (like the R3000) require nops to be inserted for delays.

      FIXME: Currently, we require that the user handle delays.
      In order to fill delay slots for non-interlocked chips,
      we must have a way to specify delays based on the coprocessor.
      Eg. 4 cycles if load coproc reg from memory, 1 if in cache, etc.
      What are the side-effects of the cop instruction?
      What cache support might we have and what are its effects?
      Both coprocessor & memory require delays. how long???
      What registers are read/set/modified?

      If an itbl is provided to interpret cop instructions,
      this knowledge can be encoded in the itbl spec.  */

    case M_COP0:
      s = "c0";
      goto copz;
    case M_COP1:
      s = "c1";
      goto copz;
    case M_COP2:
      s = "c2";
      goto copz;
    case M_COP3:
      s = "c3";
    copz:
      if (!strcmp (s,"c2") && mips_opts.arch == CPU_OCTEON
	  && octeon_error_on_unsupported)
        {
          as_bad (_("opcode not implemented in Octeon `%s'"), ip->insn_mo->name);
          return;
        }
      /* For now we just do C (same as Cz).  The parameter will be
         stored in insn_opcode by mips_ip.  */
      macro_build (NULL, s, "C", ip->insn_opcode);
      break;

    case M_MOVE:
      move_register (dreg, sreg);
      break;

#ifdef LOSING_COMPILER
    default:
      /* Try and see if this is a new itbl instruction.
         This code builds table entries out of the macros in mip_opcodes.
         FIXME: For now we just assemble the expression and pass it's
         value along as a 32-bit immediate.
         We may want to have the assembler assemble this value,
         so that we gain the assembler's knowledge of delay slots,
         symbols, etc.
         Would it be more efficient to use mask (id) here? */
      if (itbl_have_entries
	  && (immed_expr = itbl_assemble (ip->insn_mo->name, "")))
	{
	  s = ip->insn_mo->name;
	  s2 = "cop3";
	  coproc = ITBL_DECODE_PNUM (immed_expr);;
	  macro_build (&immed_expr, s, "C");
	  break;
	}
      macro2 (ip);
      break;
    }
  if (mips_opts.noat && used_at)
    as_bad (_("Macro used $at after \".set noat\""));
}

static void
macro2 (struct mips_cl_insn *ip)
{
  int treg, sreg, dreg, breg;
  int tempreg;
  int mask;
  int used_at;
  expressionS expr1;
  const char *s;
  const char *s2;
  const char *fmt;
  int likely = 0;
  int dbl = 0;
  int coproc = 0;
  int lr = 0;
  int imm = 0;
  int off;
  offsetT maxnum;
  bfd_reloc_code_real_type r;

  treg = (ip->insn_opcode >> 16) & 0x1f;
  dreg = (ip->insn_opcode >> 11) & 0x1f;
  sreg = breg = (ip->insn_opcode >> 21) & 0x1f;
  mask = ip->insn_mo->mask;

  expr1.X_op = O_constant;
  expr1.X_op_symbol = NULL;
  expr1.X_add_symbol = NULL;
  expr1.X_add_number = 1;

  switch (mask)
    {
#endif /* LOSING_COMPILER */

    case M_DMUL:
      dbl = 1;
    case M_MUL:
      macro_build (NULL, dbl ? "dmultu" : "multu", "s,t", sreg, treg);
      macro_build (NULL, "mflo", "d", dreg);
      break;

    case M_DMUL_I:
      dbl = 1;
    case M_MUL_I:
      /* The MIPS assembler some times generates shifts and adds.  I'm
	 not trying to be that fancy. GCC should do this for us
	 anyway.  */
      used_at = 1;
      load_register (AT, &imm_expr, dbl);
      macro_build (NULL, dbl ? "dmult" : "mult", "s,t", sreg, AT);
      macro_build (NULL, "mflo", "d", dreg);
      break;

    case M_DMULO_I:
      dbl = 1;
    case M_MULO_I:
      imm = 1;
      goto do_mulo;

    case M_DMULO:
      dbl = 1;
    case M_MULO:
    do_mulo:
      start_noreorder ();
      used_at = 1;
      if (imm)
	load_register (AT, &imm_expr, dbl);
      macro_build (NULL, dbl ? "dmult" : "mult", "s,t", sreg, imm ? AT : treg);
      macro_build (NULL, "mflo", "d", dreg);
      macro_build (NULL, dbl ? "dsra32" : "sra", "d,w,<", dreg, dreg, RA);
      macro_build (NULL, "mfhi", "d", AT);
      if (mips_trap)
	macro_build (NULL, "tne", "s,t,q", dreg, AT, 6);
      else
	{
	  expr1.X_add_number = 8;
	  macro_build (&expr1, "beq", "s,t,p", dreg, AT);
	  macro_build (NULL, "nop", "", 0);
	  macro_build (NULL, "break", "c", 6);
	}
      end_noreorder ();
      macro_build (NULL, "mflo", "d", dreg);
      break;

    case M_DMULOU_I:
      dbl = 1;
    case M_MULOU_I:
      imm = 1;
      goto do_mulou;

    case M_DMULOU:
      dbl = 1;
    case M_MULOU:
    do_mulou:
      start_noreorder ();
      used_at = 1;
      if (imm)
	load_register (AT, &imm_expr, dbl);
      macro_build (NULL, dbl ? "dmultu" : "multu", "s,t",
		   sreg, imm ? AT : treg);
      macro_build (NULL, "mfhi", "d", AT);
      macro_build (NULL, "mflo", "d", dreg);
      if (mips_trap)
	macro_build (NULL, "tne", "s,t,q", AT, 0, 6);
      else
	{
	  expr1.X_add_number = 8;
	  macro_build (&expr1, "beq", "s,t,p", AT, 0);
	  macro_build (NULL, "nop", "", 0);
	  macro_build (NULL, "break", "c", 6);
	}
      end_noreorder ();
      break;

    case M_DROL:
      if (ISA_HAS_DROR (mips_opts.isa) || CPU_HAS_DROR (mips_opts.arch))
	{
	  if (dreg == sreg)
	    {
	      tempreg = AT;
	      used_at = 1;
	    }
	  else
	    {
	      tempreg = dreg;
	    }
	  macro_build (NULL, "dnegu", "d,w", tempreg, treg);
	  macro_build (NULL, "drorv", "d,t,s", dreg, sreg, tempreg);
	  break;
	}
      used_at = 1;
      macro_build (NULL, "dsubu", "d,v,t", AT, 0, treg);
      macro_build (NULL, "dsrlv", "d,t,s", AT, sreg, AT);
      macro_build (NULL, "dsllv", "d,t,s", dreg, sreg, treg);
      macro_build (NULL, "or", "d,v,t", dreg, dreg, AT);
      break;

    case M_ROL:
      if (ISA_HAS_ROR (mips_opts.isa) || CPU_HAS_ROR (mips_opts.arch))
	{
	  if (dreg == sreg)
	    {
	      tempreg = AT;
	      used_at = 1;
	    }
	  else
	    {
	      tempreg = dreg;
	    }
	  macro_build (NULL, "negu", "d,w", tempreg, treg);
	  macro_build (NULL, "rorv", "d,t,s", dreg, sreg, tempreg);
	  break;
	}
      used_at = 1;
      macro_build (NULL, "subu", "d,v,t", AT, 0, treg);
      macro_build (NULL, "srlv", "d,t,s", AT, sreg, AT);
      macro_build (NULL, "sllv", "d,t,s", dreg, sreg, treg);
      macro_build (NULL, "or", "d,v,t", dreg, dreg, AT);
      break;

    case M_DROL_I:
      {
	unsigned int rot;
	char *l, *r;

	if (imm_expr.X_op != O_constant)
	  as_bad (_("Improper rotate count"));
	rot = imm_expr.X_add_number & 0x3f;
	if (ISA_HAS_DROR (mips_opts.isa) || CPU_HAS_DROR (mips_opts.arch))
	  {
	    rot = (64 - rot) & 0x3f;
	    if (rot >= 32)
	      macro_build (NULL, "dror32", "d,w,<", dreg, sreg, rot - 32);
	    else
	      macro_build (NULL, "dror", "d,w,<", dreg, sreg, rot);
	    break;
	  }
	if (rot == 0)
	  {
	    macro_build (NULL, "dsrl", "d,w,<", dreg, sreg, 0);
	    break;
	  }
	l = (rot < 0x20) ? "dsll" : "dsll32";
	r = ((0x40 - rot) < 0x20) ? "dsrl" : "dsrl32";
	rot &= 0x1f;
	used_at = 1;
	macro_build (NULL, l, "d,w,<", AT, sreg, rot);
	macro_build (NULL, r, "d,w,<", dreg, sreg, (0x20 - rot) & 0x1f);
	macro_build (NULL, "or", "d,v,t", dreg, dreg, AT);
      }
      break;

    case M_ROL_I:
      {
	unsigned int rot;

	if (imm_expr.X_op != O_constant)
	  as_bad (_("Improper rotate count"));
	rot = imm_expr.X_add_number & 0x1f;
	if (ISA_HAS_ROR (mips_opts.isa) || CPU_HAS_ROR (mips_opts.arch))
	  {
	    macro_build (NULL, "ror", "d,w,<", dreg, sreg, (32 - rot) & 0x1f);
	    break;
	  }
	if (rot == 0)
	  {
	    macro_build (NULL, "srl", "d,w,<", dreg, sreg, 0);
	    break;
	  }
	used_at = 1;
	macro_build (NULL, "sll", "d,w,<", AT, sreg, rot);
	macro_build (NULL, "srl", "d,w,<", dreg, sreg, (0x20 - rot) & 0x1f);
	macro_build (NULL, "or", "d,v,t", dreg, dreg, AT);
      }
      break;

    case M_DROR:
      if (ISA_HAS_DROR (mips_opts.isa) || CPU_HAS_DROR (mips_opts.arch))
	{
	  macro_build (NULL, "drorv", "d,t,s", dreg, sreg, treg);
	  break;
	}
      used_at = 1;
      macro_build (NULL, "dsubu", "d,v,t", AT, 0, treg);
      macro_build (NULL, "dsllv", "d,t,s", AT, sreg, AT);
      macro_build (NULL, "dsrlv", "d,t,s", dreg, sreg, treg);
      macro_build (NULL, "or", "d,v,t", dreg, dreg, AT);
      break;

    case M_ROR:
      if (ISA_HAS_ROR (mips_opts.isa) || CPU_HAS_ROR (mips_opts.arch))
	{
	  macro_build (NULL, "rorv", "d,t,s", dreg, sreg, treg);
	  break;
	}
      used_at = 1;
      macro_build (NULL, "subu", "d,v,t", AT, 0, treg);
      macro_build (NULL, "sllv", "d,t,s", AT, sreg, AT);
      macro_build (NULL, "srlv", "d,t,s", dreg, sreg, treg);
      macro_build (NULL, "or", "d,v,t", dreg, dreg, AT);
      break;

    case M_DROR_I:
      {
	unsigned int rot;
	char *l, *r;

	if (imm_expr.X_op != O_constant)
	  as_bad (_("Improper rotate count"));
	rot = imm_expr.X_add_number & 0x3f;
	if (ISA_HAS_DROR (mips_opts.isa) || CPU_HAS_DROR (mips_opts.arch))
	  {
	    if (rot >= 32)
	      macro_build (NULL, "dror32", "d,w,<", dreg, sreg, rot - 32);
	    else
	      macro_build (NULL, "dror", "d,w,<", dreg, sreg, rot);
	    break;
	  }
	if (rot == 0)
	  {
	    macro_build (NULL, "dsrl", "d,w,<", dreg, sreg, 0);
	    break;
	  }
	r = (rot < 0x20) ? "dsrl" : "dsrl32";
	l = ((0x40 - rot) < 0x20) ? "dsll" : "dsll32";
	rot &= 0x1f;
	used_at = 1;
	macro_build (NULL, r, "d,w,<", AT, sreg, rot);
	macro_build (NULL, l, "d,w,<", dreg, sreg, (0x20 - rot) & 0x1f);
	macro_build (NULL, "or", "d,v,t", dreg, dreg, AT);
      }
      break;

    case M_ROR_I:
      {
	unsigned int rot;

	if (imm_expr.X_op != O_constant)
	  as_bad (_("Improper rotate count"));
	rot = imm_expr.X_add_number & 0x1f;
	if (ISA_HAS_ROR (mips_opts.isa) || CPU_HAS_ROR (mips_opts.arch))
	  {
	    macro_build (NULL, "ror", "d,w,<", dreg, sreg, rot);
	    break;
	  }
	if (rot == 0)
	  {
	    macro_build (NULL, "srl", "d,w,<", dreg, sreg, 0);
	    break;
	  }
	used_at = 1;
	macro_build (NULL, "srl", "d,w,<", AT, sreg, rot);
	macro_build (NULL, "sll", "d,w,<", dreg, sreg, (0x20 - rot) & 0x1f);
	macro_build (NULL, "or", "d,v,t", dreg, dreg, AT);
      }
      break;

    case M_S_DOB:
      if (mips_opts.arch == CPU_R4650)
	{
	  as_bad (_("opcode not supported on this processor"));
	  break;
	}
      assert (mips_opts.isa == ISA_MIPS1);
      /* Even on a big endian machine $fn comes before $fn+1.  We have
	 to adjust when storing to memory.  */
      macro_build (&offset_expr, "swc1", "T,o(b)",
		   target_big_endian ? treg + 1 : treg, BFD_RELOC_LO16, breg);
      offset_expr.X_add_number += 4;
      macro_build (&offset_expr, "swc1", "T,o(b)",
		   target_big_endian ? treg : treg + 1, BFD_RELOC_LO16, breg);
      break;

    case M_SEQ:
      if (sreg == 0)
	macro_build (&expr1, "sltiu", "t,r,j", dreg, treg, BFD_RELOC_LO16);
      else if (treg == 0)
	macro_build (&expr1, "sltiu", "t,r,j", dreg, sreg, BFD_RELOC_LO16);
      else
	{
	  macro_build (NULL, "xor", "d,v,t", dreg, sreg, treg);
	  macro_build (&expr1, "sltiu", "t,r,j", dreg, dreg, BFD_RELOC_LO16);
	}
      break;

    case M_SEQ_I:
      if (imm_expr.X_op == O_constant && imm_expr.X_add_number == 0)
	{
	  macro_build (&expr1, "sltiu", "t,r,j", dreg, sreg, BFD_RELOC_LO16);
	  break;
	}
      if (sreg == 0)
	{
	  as_warn (_("Instruction %s: result is always false"),
		   ip->insn_mo->name);
	  move_register (dreg, 0);
	  break;
	}
      if (imm_expr.X_op == O_constant
	  && imm_expr.X_add_number >= 0
	  && imm_expr.X_add_number < 0x10000)
	{
	  macro_build (&imm_expr, "xori", "t,r,i", dreg, sreg, BFD_RELOC_LO16);
	}
      else if (imm_expr.X_op == O_constant
	       && imm_expr.X_add_number > -0x8000
	       && imm_expr.X_add_number < 0)
	{
	  imm_expr.X_add_number = -imm_expr.X_add_number;
	  macro_build (&imm_expr, HAVE_32BIT_GPRS ? "addiu" : "daddiu",
		       "t,r,j", dreg, sreg, BFD_RELOC_LO16);
	}
      else
	{
	  load_register (AT, &imm_expr, HAVE_64BIT_GPRS);
	  macro_build (NULL, "xor", "d,v,t", dreg, sreg, AT);
	  used_at = 1;
	}
      macro_build (&expr1, "sltiu", "t,r,j", dreg, dreg, BFD_RELOC_LO16);
      break;

    case M_SGE:		/* sreg >= treg <==> not (sreg < treg) */
      s = "slt";
      goto sge;
    case M_SGEU:
      s = "sltu";
    sge:
      macro_build (NULL, s, "d,v,t", dreg, sreg, treg);
      macro_build (&expr1, "xori", "t,r,i", dreg, dreg, BFD_RELOC_LO16);
      break;

    case M_SGE_I:		/* sreg >= I <==> not (sreg < I) */
    case M_SGEU_I:
      if (imm_expr.X_op == O_constant
	  && imm_expr.X_add_number >= -0x8000
	  && imm_expr.X_add_number < 0x8000)
	{
	  macro_build (&imm_expr, mask == M_SGE_I ? "slti" : "sltiu", "t,r,j",
		       dreg, sreg, BFD_RELOC_LO16);
	}
      else
	{
	  load_register (AT, &imm_expr, HAVE_64BIT_GPRS);
	  macro_build (NULL, mask == M_SGE_I ? "slt" : "sltu", "d,v,t",
		       dreg, sreg, AT);
	  used_at = 1;
	}
      macro_build (&expr1, "xori", "t,r,i", dreg, dreg, BFD_RELOC_LO16);
      break;

    case M_SGT:		/* sreg > treg  <==>  treg < sreg */
      s = "slt";
      goto sgt;
    case M_SGTU:
      s = "sltu";
    sgt:
      macro_build (NULL, s, "d,v,t", dreg, treg, sreg);
      break;

    case M_SGT_I:		/* sreg > I  <==>  I < sreg */
      s = "slt";
      goto sgti;
    case M_SGTU_I:
      s = "sltu";
    sgti:
      used_at = 1;
      load_register (AT, &imm_expr, HAVE_64BIT_GPRS);
      macro_build (NULL, s, "d,v,t", dreg, AT, sreg);
      break;

    case M_SLE:	/* sreg <= treg  <==>  treg >= sreg  <==>  not (treg < sreg) */
      s = "slt";
      goto sle;
    case M_SLEU:
      s = "sltu";
    sle:
      macro_build (NULL, s, "d,v,t", dreg, treg, sreg);
      macro_build (&expr1, "xori", "t,r,i", dreg, dreg, BFD_RELOC_LO16);
      break;

    case M_SLE_I:	/* sreg <= I <==> I >= sreg <==> not (I < sreg) */
      s = "slt";
      goto slei;
    case M_SLEU_I:
      s = "sltu";
    slei:
      used_at = 1;
      load_register (AT, &imm_expr, HAVE_64BIT_GPRS);
      macro_build (NULL, s, "d,v,t", dreg, AT, sreg);
      macro_build (&expr1, "xori", "t,r,i", dreg, dreg, BFD_RELOC_LO16);
      break;

    case M_SLT_I:
      if (imm_expr.X_op == O_constant
	  && imm_expr.X_add_number >= -0x8000
	  && imm_expr.X_add_number < 0x8000)
	{
	  macro_build (&imm_expr, "slti", "t,r,j", dreg, sreg, BFD_RELOC_LO16);
	  break;
	}
      used_at = 1;
      load_register (AT, &imm_expr, HAVE_64BIT_GPRS);
      macro_build (NULL, "slt", "d,v,t", dreg, sreg, AT);
      break;

    case M_SLTU_I:
      if (imm_expr.X_op == O_constant
	  && imm_expr.X_add_number >= -0x8000
	  && imm_expr.X_add_number < 0x8000)
	{
	  macro_build (&imm_expr, "sltiu", "t,r,j", dreg, sreg,
		       BFD_RELOC_LO16);
	  break;
	}
      used_at = 1;
      load_register (AT, &imm_expr, HAVE_64BIT_GPRS);
      macro_build (NULL, "sltu", "d,v,t", dreg, sreg, AT);
      break;

    case M_SNE:
      if (sreg == 0)
	macro_build (NULL, "sltu", "d,v,t", dreg, 0, treg);
      else if (treg == 0)
	macro_build (NULL, "sltu", "d,v,t", dreg, 0, sreg);
      else
	{
	  macro_build (NULL, "xor", "d,v,t", dreg, sreg, treg);
	  macro_build (NULL, "sltu", "d,v,t", dreg, 0, dreg);
	}
      break;

    case M_SNE_I:
      if (imm_expr.X_op == O_constant && imm_expr.X_add_number == 0)
	{
	  macro_build (NULL, "sltu", "d,v,t", dreg, 0, sreg);
	  break;
	}
      if (sreg == 0)
	{
	  as_warn (_("Instruction %s: result is always true"),
		   ip->insn_mo->name);
	  macro_build (&expr1, HAVE_32BIT_GPRS ? "addiu" : "daddiu", "t,r,j",
		       dreg, 0, BFD_RELOC_LO16);
	  break;
	}
      if (imm_expr.X_op == O_constant
	  && imm_expr.X_add_number >= 0
	  && imm_expr.X_add_number < 0x10000)
	{
	  macro_build (&imm_expr, "xori", "t,r,i", dreg, sreg, BFD_RELOC_LO16);
	}
      else if (imm_expr.X_op == O_constant
	       && imm_expr.X_add_number > -0x8000
	       && imm_expr.X_add_number < 0)
	{
	  imm_expr.X_add_number = -imm_expr.X_add_number;
	  macro_build (&imm_expr, HAVE_32BIT_GPRS ? "addiu" : "daddiu",
		       "t,r,j", dreg, sreg, BFD_RELOC_LO16);
	}
      else
	{
	  load_register (AT, &imm_expr, HAVE_64BIT_GPRS);
	  macro_build (NULL, "xor", "d,v,t", dreg, sreg, AT);
	  used_at = 1;
	}
      macro_build (NULL, "sltu", "d,v,t", dreg, 0, dreg);
      break;

    case M_DSUB_I:
      dbl = 1;
    case M_SUB_I:
      if (imm_expr.X_op == O_constant
	  && imm_expr.X_add_number > -0x8000
	  && imm_expr.X_add_number <= 0x8000)
	{
	  imm_expr.X_add_number = -imm_expr.X_add_number;
	  macro_build (&imm_expr, dbl ? "daddi" : "addi", "t,r,j",
		       dreg, sreg, BFD_RELOC_LO16);
	  break;
	}
      used_at = 1;
      load_register (AT, &imm_expr, dbl);
      macro_build (NULL, dbl ? "dsub" : "sub", "d,v,t", dreg, sreg, AT);
      break;

    case M_DSUBU_I:
      dbl = 1;
    case M_SUBU_I:
      if (imm_expr.X_op == O_constant
	  && imm_expr.X_add_number > -0x8000
	  && imm_expr.X_add_number <= 0x8000)
	{
	  imm_expr.X_add_number = -imm_expr.X_add_number;
	  macro_build (&imm_expr, dbl ? "daddiu" : "addiu", "t,r,j",
		       dreg, sreg, BFD_RELOC_LO16);
	  break;
	}
      used_at = 1;
      load_register (AT, &imm_expr, dbl);
      macro_build (NULL, dbl ? "dsubu" : "subu", "d,v,t", dreg, sreg, AT);
      break;

    case M_TEQ_I:
      s = "teq";
      goto trap;
    case M_TGE_I:
      s = "tge";
      goto trap;
    case M_TGEU_I:
      s = "tgeu";
      goto trap;
    case M_TLT_I:
      s = "tlt";
      goto trap;
    case M_TLTU_I:
      s = "tltu";
      goto trap;
    case M_TNE_I:
      s = "tne";
    trap:
      used_at = 1;
      load_register (AT, &imm_expr, HAVE_64BIT_GPRS);
      macro_build (NULL, s, "s,t", sreg, AT);
      break;

    case M_TRUNCWS:
    case M_TRUNCWD:
      if (mips_opts.arch == CPU_OCTEON && octeon_error_on_unsupported)
        {
          as_bad (_("opcode not implemented in Octeon `%s'"), ip->insn_mo->name);
          return;
        }
      assert (mips_opts.isa == ISA_MIPS1);
      used_at = 1;
      sreg = (ip->insn_opcode >> 11) & 0x1f;	/* floating reg */
      dreg = (ip->insn_opcode >> 06) & 0x1f;	/* floating reg */

      /*
       * Is the double cfc1 instruction a bug in the mips assembler;
       * or is there a reason for it?
       */
      start_noreorder ();
      macro_build (NULL, "cfc1", "t,G", treg, RA);
      macro_build (NULL, "cfc1", "t,G", treg, RA);
      macro_build (NULL, "nop", "");
      expr1.X_add_number = 3;
      macro_build (&expr1, "ori", "t,r,i", AT, treg, BFD_RELOC_LO16);
      expr1.X_add_number = 2;
      macro_build (&expr1, "xori", "t,r,i", AT, AT, BFD_RELOC_LO16);
      macro_build (NULL, "ctc1", "t,G", AT, RA);
      macro_build (NULL, "nop", "");
      macro_build (NULL, mask == M_TRUNCWD ? "cvt.w.d" : "cvt.w.s", "D,S",
		   dreg, sreg);
      macro_build (NULL, "ctc1", "t,G", treg, RA);
      macro_build (NULL, "nop", "");
      end_noreorder ();
      break;

    case M_ULH:
      s = "lb";
      goto ulh;
    case M_ULHU:
      s = "lbu";
    ulh:
      used_at = 1;
      if (offset_expr.X_add_number >= 0x7fff)
	as_bad (_("operand overflow"));
      /* Expand the ulh to "lb, lbu, ins" instead of "lb, lbu, sll, ori". */
      if (! target_big_endian)
	++offset_expr.X_add_number;
      macro_build (&offset_expr, s, "t,o(b)", AT, BFD_RELOC_LO16, breg);
      if (! target_big_endian)
	--offset_expr.X_add_number;
      else
	++offset_expr.X_add_number;
      macro_build (&offset_expr, "lbu", "t,o(b)", treg, BFD_RELOC_LO16, breg);
      if (ISA_HAS_INS (mips_opts.isa))
	macro_build (NULL, "ins", "t,r,+A,+B", treg, AT, 8, 31);
      else
	{
          macro_build (NULL, "sll", "d,w,<", AT, AT, 8);
          macro_build (NULL, "or", "d,v,t", treg, treg, AT);
	}
      break;

    case M_ULD:
      s = "ldl";
      s2 = "ldr";
      off = 7;
      goto ulw;
    case M_ULW:
      s = "lwl";
      s2 = "lwr";
      off = 3;
    ulw:
      if (offset_expr.X_add_number >= 0x8000 - off)
	as_bad (_("operand overflow"));
      if (treg != breg)
	tempreg = treg;
      else
	{
	  used_at = 1;
	  tempreg = AT;
	}

      /* For small variables the compiler uses gp_rel to load the value of
	 the variables. While parsing instructions "uld $2,%gp_rel(var)($28)"
	 the offset_reloc[0] is set to BFD_RELOC_GPREL16. Use this relocation
	 type while emitting instructions otherwise use BFD_RELOC_LO16.  */
      if (offset_reloc[0] == BFD_RELOC_UNUSED)
	offset_reloc[0] = BFD_RELOC_LO16;

      if (octeon_use_unalign && mips_opts.arch == CPU_OCTEON)
	{
	  /* Reset used_at as tempreg is not used while generating Octeon 
	     unaligned load/store.  */
	  used_at = 0;
	  macro_build (&offset_expr, (mask == M_ULW ? "ulw" : "uld"), "t,o(b)",
		       treg, offset_reloc[0], breg);
	  break;
	}
	  
      if (! target_big_endian)
	offset_expr.X_add_number += off;
      macro_build (&offset_expr, s, "t,o(b)", tempreg, offset_reloc[0], breg);
      if (! target_big_endian)
	offset_expr.X_add_number -= off;
      else
	offset_expr.X_add_number += off;
      macro_build (&offset_expr, s2, "t,o(b)", tempreg, offset_reloc[0], breg);

      /* If necessary, move the result in tempreg the final destination.  */
      if (treg == tempreg)
        break;
      /* Protect second load's delay slot.  */
      load_delay_nop ();
      move_register (treg, tempreg);
      break;

    case M_ULD_A:
      s = "ldl";
      s2 = "ldr";
      off = 7;
      goto ulwa;
    case M_ULW_A:
      s = "lwl";
      s2 = "lwr";
      off = 3;
    ulwa:
      used_at = 1;
      load_address (AT, &offset_expr, &used_at);
      if (breg != 0)
	macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t", AT, AT, breg);

      /* For small variables the compiler uses gp_rel to load the value of
	 the variables. While parsing instructions "uld $2,%gp_rel(var)($28)"
	 the offset_reloc[0] is set to BFD_RELOC_GPREL16. Use this relocation
	 type while emitting instructions otherwise use BFD_RELOC_LO16.  */
      if (offset_reloc[0] == BFD_RELOC_UNUSED)
	offset_reloc[0] = BFD_RELOC_LO16;

      if (octeon_use_unalign && mips_opts.arch == CPU_OCTEON)
	{
	  macro_build (&offset_expr, (mask == M_ULW_A ? "ulw" : "uld"), 
		       "t,o(b)", treg, offset_reloc[0], AT);
	  break;
	}

      if (! target_big_endian)
	expr1.X_add_number = off;
      else
	expr1.X_add_number = 0;
      macro_build (&expr1, s, "t,o(b)", treg, offset_reloc[0], AT);
      if (! target_big_endian)
	expr1.X_add_number = 0;
      else
	expr1.X_add_number = off;
      macro_build (&expr1, s2, "t,o(b)", treg, offset_reloc[0], AT);
      break;

    case M_ULH_A:
    case M_ULHU_A:
      used_at = 1;
      load_address (AT, &offset_expr, &used_at);
      if (breg != 0)
	macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t", AT, AT, breg);

      if (ISA_HAS_INS (mips_opts.isa))
	{
	  if (target_big_endian)
	    expr1.X_add_number = 1;
	  else
	    expr1.X_add_number = 0;
	  macro_build (&expr1, "lbu", "t,o(b)", treg, BFD_RELOC_LO16, AT);
	  if (target_big_endian)
	    expr1.X_add_number = 0;
	  else
	    expr1.X_add_number = 1;
	  macro_build (&expr1, mask == M_ULH_A ? "lb" : "lbu", "t,o(b)",
		       AT, BFD_RELOC_LO16, AT);
	  macro_build (NULL, "ins", "t,r,+A,+B", treg, AT, 8, 31);
	  break;
	}
      if (target_big_endian)
	expr1.X_add_number = 0;
      macro_build (&expr1, mask == M_ULH_A ? "lb" : "lbu", "t,o(b)",
		   treg, BFD_RELOC_LO16, AT);
      if (target_big_endian)
	expr1.X_add_number = 1;
      else
	expr1.X_add_number = 0;
      macro_build (&expr1, "lbu", "t,o(b)", AT, BFD_RELOC_LO16, AT);
      macro_build (NULL, "sll", "d,w,<", treg, treg, 8);
      macro_build (NULL, "or", "d,v,t", treg, treg, AT);
      break;

    case M_USH:
      used_at = 1;
      if (offset_expr.X_add_number >= 0x7fff)
	as_bad (_("operand overflow"));
      if (target_big_endian)
	++offset_expr.X_add_number;
      macro_build (&offset_expr, "sb", "t,o(b)", treg, BFD_RELOC_LO16, breg);
      macro_build (NULL, "srl", "d,w,<", AT, treg, 8);
      if (target_big_endian)
	--offset_expr.X_add_number;
      else
	++offset_expr.X_add_number;
      macro_build (&offset_expr, "sb", "t,o(b)", AT, BFD_RELOC_LO16, breg);
      break;

    case M_USD:
      s = "sdl";
      s2 = "sdr";
      off = 7;
      goto usw;
    case M_USW:
      s = "swl";
      s2 = "swr";
      off = 3;
    usw:
      if (offset_expr.X_add_number >= 0x8000 - off)
	as_bad (_("operand overflow"));

      /* For small variables the compiler uses gp_rel to load the value of
	 the variables. While parsing instructions "uld $2,%gp_rel(var)($28)"
	 the offset_reloc[0] is set to BFD_RELOC_GPREL16. Use this relocation
	 type while emitting instructions otherwise use BFD_RELOC_LO16.  */
      if (offset_reloc[0] == BFD_RELOC_UNUSED)
	offset_reloc[0] = BFD_RELOC_LO16;

      if (octeon_use_unalign && mips_opts.arch == CPU_OCTEON)
	{
	  macro_build (&offset_expr, (mask == M_USD ? "usd" : "usw"), 
		       "t,o(b)", treg, offset_reloc[0], breg);
	  break;
	}
      if (! target_big_endian)
	offset_expr.X_add_number += off;
      macro_build (&offset_expr, s, "t,o(b)", treg, offset_reloc[0], breg);
      if (! target_big_endian)
	offset_expr.X_add_number -= off;
      else
	offset_expr.X_add_number += off;
      macro_build (&offset_expr, s2, "t,o(b)", treg, offset_reloc[0], breg);
      break;

    case M_USD_A:
      s = "sdl";
      s2 = "sdr";
      off = 7;
      goto uswa;
    case M_USW_A:
      s = "swl";
      s2 = "swr";
      off = 3;
    uswa:
      used_at = 1;
      load_address (AT, &offset_expr, &used_at);
      if (breg != 0)
	macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t", AT, AT, breg);

      /* For small variables the compiler uses gp_rel to load the value of
	 the variables. While parsing instructions "uld $2,%gp_rel(var)($28)"
	 the offset_reloc[0] is set to BFD_RELOC_GPREL16. Use this relocation
	 type while emitting instructions otherwise use BFD_RELOC_LO16.  */
      if (offset_reloc[0] == BFD_RELOC_UNUSED)
	offset_reloc[0] = BFD_RELOC_LO16;

      if (octeon_use_unalign && mips_opts.arch == CPU_OCTEON)
	{
	  macro_build (&offset_expr, (mask == M_USW_A ? "usw" : "usd"), 
		       "t,o(b)", treg, offset_reloc[0], AT);
	  break;
	}
      if (! target_big_endian)
	expr1.X_add_number = off;
      else
	expr1.X_add_number = 0;
      macro_build (&expr1, s, "t,o(b)", treg, offset_reloc[0], AT);
      if (! target_big_endian)
	expr1.X_add_number = 0;
      else
	expr1.X_add_number = off;
      macro_build (&expr1, s2, "t,o(b)", treg, offset_reloc[0], AT);
      break;

    case M_USH_A:
      used_at = 1;
      load_address (AT, &offset_expr, &used_at);
      if (breg != 0)
	macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t", AT, AT, breg);
      if (! target_big_endian)
	expr1.X_add_number = 0;
      macro_build (&expr1, "sb", "t,o(b)", treg, BFD_RELOC_LO16, AT);
      macro_build (NULL, "srl", "d,w,<", treg, treg, 8);
      if (! target_big_endian)
	expr1.X_add_number = 1;
      else
	expr1.X_add_number = 0;
      macro_build (&expr1, "sb", "t,o(b)", treg, BFD_RELOC_LO16, AT);
      if (! target_big_endian)
	expr1.X_add_number = 0;
      else
	expr1.X_add_number = 1;
      macro_build (&expr1, "lbu", "t,o(b)", AT, BFD_RELOC_LO16, AT);
      macro_build (NULL, "sll", "d,w,<", treg, treg, 8);
      macro_build (NULL, "or", "d,v,t", treg, treg, AT);
      break;

    default:
      /* FIXME: Check if this is one of the itbl macros, since they
	 are added dynamically.  */
      as_bad (_("Macro %s not implemented yet"), ip->insn_mo->name);
      break;
    }
  if (mips_opts.noat && used_at)
    as_bad (_("Macro used $at after \".set noat\""));
}

/* Implement macros in mips16 mode.  */

static void
mips16_macro (struct mips_cl_insn *ip)
{
  int mask;
  int xreg, yreg, zreg, tmp;
  expressionS expr1;
  int dbl;
  const char *s, *s2, *s3;

  mask = ip->insn_mo->mask;

  xreg = MIPS16_EXTRACT_OPERAND (RX, *ip);
  yreg = MIPS16_EXTRACT_OPERAND (RY, *ip);
  zreg = MIPS16_EXTRACT_OPERAND (RZ, *ip);

  expr1.X_op = O_constant;
  expr1.X_op_symbol = NULL;
  expr1.X_add_symbol = NULL;
  expr1.X_add_number = 1;

  dbl = 0;

  switch (mask)
    {
    default:
      internalError ();

    case M_DDIV_3:
      dbl = 1;
    case M_DIV_3:
      s = "mflo";
      goto do_div3;
    case M_DREM_3:
      dbl = 1;
    case M_REM_3:
      s = "mfhi";
    do_div3:
      start_noreorder ();
      macro_build (NULL, dbl ? "ddiv" : "div", "0,x,y", xreg, yreg);
      expr1.X_add_number = 2;
      macro_build (&expr1, "bnez", "x,p", yreg);
      macro_build (NULL, "break", "6", 7);

      /* FIXME: The normal code checks for of -1 / -0x80000000 here,
         since that causes an overflow.  We should do that as well,
         but I don't see how to do the comparisons without a temporary
         register.  */
      end_noreorder ();
      macro_build (NULL, s, "x", zreg);
      break;

    case M_DIVU_3:
      s = "divu";
      s2 = "mflo";
      goto do_divu3;
    case M_REMU_3:
      s = "divu";
      s2 = "mfhi";
      goto do_divu3;
    case M_DDIVU_3:
      s = "ddivu";
      s2 = "mflo";
      goto do_divu3;
    case M_DREMU_3:
      s = "ddivu";
      s2 = "mfhi";
    do_divu3:
      start_noreorder ();
      macro_build (NULL, s, "0,x,y", xreg, yreg);
      expr1.X_add_number = 2;
      macro_build (&expr1, "bnez", "x,p", yreg);
      macro_build (NULL, "break", "6", 7);
      end_noreorder ();
      macro_build (NULL, s2, "x", zreg);
      break;

    case M_DMUL:
      dbl = 1;
    case M_MUL:
      macro_build (NULL, dbl ? "dmultu" : "multu", "x,y", xreg, yreg);
      macro_build (NULL, "mflo", "x", zreg);
      break;

    case M_DSUBU_I:
      dbl = 1;
      goto do_subu;
    case M_SUBU_I:
    do_subu:
      if (imm_expr.X_op != O_constant)
	as_bad (_("Unsupported large constant"));
      imm_expr.X_add_number = -imm_expr.X_add_number;
      macro_build (&imm_expr, dbl ? "daddiu" : "addiu", "y,x,4", yreg, xreg);
      break;

    case M_SUBU_I_2:
      if (imm_expr.X_op != O_constant)
	as_bad (_("Unsupported large constant"));
      imm_expr.X_add_number = -imm_expr.X_add_number;
      macro_build (&imm_expr, "addiu", "x,k", xreg);
      break;

    case M_DSUBU_I_2:
      if (imm_expr.X_op != O_constant)
	as_bad (_("Unsupported large constant"));
      imm_expr.X_add_number = -imm_expr.X_add_number;
      macro_build (&imm_expr, "daddiu", "y,j", yreg);
      break;

    case M_BEQ:
      s = "cmp";
      s2 = "bteqz";
      goto do_branch;
    case M_BNE:
      s = "cmp";
      s2 = "btnez";
      goto do_branch;
    case M_BLT:
      s = "slt";
      s2 = "btnez";
      goto do_branch;
    case M_BLTU:
      s = "sltu";
      s2 = "btnez";
      goto do_branch;
    case M_BLE:
      s = "slt";
      s2 = "bteqz";
      goto do_reverse_branch;
    case M_BLEU:
      s = "sltu";
      s2 = "bteqz";
      goto do_reverse_branch;
    case M_BGE:
      s = "slt";
      s2 = "bteqz";
      goto do_branch;
    case M_BGEU:
      s = "sltu";
      s2 = "bteqz";
      goto do_branch;
    case M_BGT:
      s = "slt";
      s2 = "btnez";
      goto do_reverse_branch;
    case M_BGTU:
      s = "sltu";
      s2 = "btnez";

    do_reverse_branch:
      tmp = xreg;
      xreg = yreg;
      yreg = tmp;

    do_branch:
      macro_build (NULL, s, "x,y", xreg, yreg);
      macro_build (&offset_expr, s2, "p");
      break;

    case M_BEQ_I:
      s = "cmpi";
      s2 = "bteqz";
      s3 = "x,U";
      goto do_branch_i;
    case M_BNE_I:
      s = "cmpi";
      s2 = "btnez";
      s3 = "x,U";
      goto do_branch_i;
    case M_BLT_I:
      s = "slti";
      s2 = "btnez";
      s3 = "x,8";
      goto do_branch_i;
    case M_BLTU_I:
      s = "sltiu";
      s2 = "btnez";
      s3 = "x,8";
      goto do_branch_i;
    case M_BLE_I:
      s = "slti";
      s2 = "btnez";
      s3 = "x,8";
      goto do_addone_branch_i;
    case M_BLEU_I:
      s = "sltiu";
      s2 = "btnez";
      s3 = "x,8";
      goto do_addone_branch_i;
    case M_BGE_I:
      s = "slti";
      s2 = "bteqz";
      s3 = "x,8";
      goto do_branch_i;
    case M_BGEU_I:
      s = "sltiu";
      s2 = "bteqz";
      s3 = "x,8";
      goto do_branch_i;
    case M_BGT_I:
      s = "slti";
      s2 = "bteqz";
      s3 = "x,8";
      goto do_addone_branch_i;
    case M_BGTU_I:
      s = "sltiu";
      s2 = "bteqz";
      s3 = "x,8";

    do_addone_branch_i:
      if (imm_expr.X_op != O_constant)
	as_bad (_("Unsupported large constant"));
      ++imm_expr.X_add_number;

    do_branch_i:
      macro_build (&imm_expr, s, s3, xreg);
      macro_build (&offset_expr, s2, "p");
      break;

    case M_ABS:
      expr1.X_add_number = 0;
      macro_build (&expr1, "slti", "x,8", yreg);
      if (xreg != yreg)
	move_register (xreg, yreg);
      expr1.X_add_number = 2;
      macro_build (&expr1, "bteqz", "p");
      macro_build (NULL, "neg", "x,w", xreg, xreg);
    }
}

/* For consistency checking, verify that all bits are specified either
   by the match/mask part of the instruction definition, or by the
   operand list.  */
static int
validate_mips_insn (const struct mips_opcode *opc)
{
  const char *p = opc->args;
  char c;
  unsigned long used_bits = opc->mask;

  if ((used_bits & opc->match) != opc->match)
    {
      as_bad (_("internal: bad mips opcode (mask error): %s %s"),
	      opc->name, opc->args);
      return 0;
    }
#define USE_BITS(mask,shift)	(used_bits |= ((mask) << (shift)))
  while (*p)
    switch (c = *p++)
      {
      case ',': break;
      case '(': break;
      case ')': break;
      case '^': USE_BITS (OP_MASK_BITIND,       OP_SH_BITIND);   break;
      case '~': USE_BITS (OP_MASK_BITIND,       OP_SH_BITIND);   break;
      case '+':
    	switch (c = *p++)
	  {
	  case '1': USE_BITS (OP_MASK_UDI1,     OP_SH_UDI1); 	break;
	  case '2': USE_BITS (OP_MASK_UDI2,	OP_SH_UDI2); 	break;
	  case '3': USE_BITS (OP_MASK_UDI3,	OP_SH_UDI3); 	break;
	  case '4': USE_BITS (OP_MASK_UDI4,	OP_SH_UDI4); 	break;
	  case 'A': USE_BITS (OP_MASK_SHAMT,	OP_SH_SHAMT);	break;
	  case 'B': USE_BITS (OP_MASK_INSMSB,	OP_SH_INSMSB);	break;
	  case 'C': USE_BITS (OP_MASK_EXTMSBD,	OP_SH_EXTMSBD);	break;
	  case 'D': USE_BITS (OP_MASK_RD,	OP_SH_RD);
		    USE_BITS (OP_MASK_SEL,	OP_SH_SEL);	break;
	  case 'E': USE_BITS (OP_MASK_SHAMT,	OP_SH_SHAMT);	break;
	  case 'F': USE_BITS (OP_MASK_INSMSB,	OP_SH_INSMSB);	break;
	  case 'G': USE_BITS (OP_MASK_EXTMSBD,	OP_SH_EXTMSBD);	break;
	  case 'H': USE_BITS (OP_MASK_EXTMSBD,	OP_SH_EXTMSBD);	break;
	  case 'I': break;
	  case 't': USE_BITS (OP_MASK_RT,	OP_SH_RT);	break;
	  case 'T': USE_BITS (OP_MASK_RT,	OP_SH_RT);
		    USE_BITS (OP_MASK_SEL,	OP_SH_SEL);	break;
	  default:
	    as_bad (_("internal: bad mips opcode (unknown extension operand type `+%c'): %s %s"),
		    c, opc->name, opc->args);
	    return 0;
	  }
	break;
      case '<': USE_BITS (OP_MASK_SHAMT,	OP_SH_SHAMT);	break;
      case '>':	USE_BITS (OP_MASK_SHAMT,	OP_SH_SHAMT);	break;
      case 'A': break;
      case 'B': USE_BITS (OP_MASK_CODE20,       OP_SH_CODE20);  break;
      case 'C':	USE_BITS (OP_MASK_COPZ,		OP_SH_COPZ);	break;
      case 'D':	USE_BITS (OP_MASK_FD,		OP_SH_FD);	break;
      case 'E':	USE_BITS (OP_MASK_RT,		OP_SH_RT);	break;
      case 'F': break;
      case 'G':	USE_BITS (OP_MASK_RD,		OP_SH_RD);	break;
      case 'H': USE_BITS (OP_MASK_SEL,		OP_SH_SEL);	break;
      case 'I': break;
      case 'J': USE_BITS (OP_MASK_CODE19,       OP_SH_CODE19);  break;
      case 'K':	USE_BITS (OP_MASK_RD,		OP_SH_RD);	break;
      case 'L': break;
      case 'M':	USE_BITS (OP_MASK_CCC,		OP_SH_CCC);	break;
      case 'N':	USE_BITS (OP_MASK_BCC,		OP_SH_BCC);	break;
      case 'O':	USE_BITS (OP_MASK_ALN,		OP_SH_ALN);	break;
      case 'Q':	USE_BITS (OP_MASK_VSEL,		OP_SH_VSEL);
		USE_BITS (OP_MASK_FT,		OP_SH_FT);	break;
      case 'R':	USE_BITS (OP_MASK_FR,		OP_SH_FR);	break;
      case 'S':	USE_BITS (OP_MASK_FS,		OP_SH_FS);	break;
      case 'T':	USE_BITS (OP_MASK_FT,		OP_SH_FT);	break;
      case 'V':	USE_BITS (OP_MASK_FS,		OP_SH_FS);	break;
      case 'W':	USE_BITS (OP_MASK_FT,		OP_SH_FT);	break;
      case 'X':	USE_BITS (OP_MASK_FD,		OP_SH_FD);	break;
      case 'Y':	USE_BITS (OP_MASK_FS,		OP_SH_FS);	break;
      case 'Z':	USE_BITS (OP_MASK_FT,		OP_SH_FT);	break;
      case 'a':	USE_BITS (OP_MASK_TARGET,	OP_SH_TARGET);	break;
      case 'b':	USE_BITS (OP_MASK_RS,		OP_SH_RS);	break;
      case 'c':	USE_BITS (OP_MASK_CODE,		OP_SH_CODE);	break;
      case 'd':	USE_BITS (OP_MASK_RD,		OP_SH_RD);	break;
      case 'f': break;
      case 'h':	USE_BITS (OP_MASK_PREFX,	OP_SH_PREFX);	break;
      case 'i':	USE_BITS (OP_MASK_IMMEDIATE,	OP_SH_IMMEDIATE); break;
      case 'j':	USE_BITS (OP_MASK_DELTA,	OP_SH_DELTA);	break;
      case 'k':	USE_BITS (OP_MASK_CACHE,	OP_SH_CACHE);	break;
      case 'l': break;
      case 'o': USE_BITS (OP_MASK_DELTA,	OP_SH_DELTA);	break;
      case 'p':	USE_BITS (OP_MASK_DELTA,	OP_SH_DELTA);	break;
      case 'q':	USE_BITS (OP_MASK_CODE2,	OP_SH_CODE2);	break;
      case 'r': USE_BITS (OP_MASK_RS,		OP_SH_RS);	break;
      case 's':	USE_BITS (OP_MASK_RS,		OP_SH_RS);	break;
      case 't':	USE_BITS (OP_MASK_RT,		OP_SH_RT);	break;
      case 'u':	USE_BITS (OP_MASK_IMMEDIATE,	OP_SH_IMMEDIATE); break;
      case 'v':	USE_BITS (OP_MASK_RS,		OP_SH_RS);	break;
      case 'w':	USE_BITS (OP_MASK_RT,		OP_SH_RT);	break;
      case 'x': break;
      case 'y': USE_BITS (OP_MASK_CODE2,        OP_SH_CODE2);   break;
      case 'z': break;
      case 'P': USE_BITS (OP_MASK_PERFREG,	OP_SH_PERFREG);	break;
      case 'U': USE_BITS (OP_MASK_RD,           OP_SH_RD);
	        USE_BITS (OP_MASK_RT,           OP_SH_RT);	break;
      case 'e': USE_BITS (OP_MASK_VECBYTE,	OP_SH_VECBYTE);	break;
      case '%': USE_BITS (OP_MASK_VECALIGN,	OP_SH_VECALIGN); break;
      case '[': break;
      case ']': break;
      case '2': USE_BITS (OP_MASK_BP,		OP_SH_BP);	break;
      case '3': USE_BITS (OP_MASK_SA3,  	OP_SH_SA3);	break;
      case '4': USE_BITS (OP_MASK_SA4,  	OP_SH_SA4);	break;
      case '5': USE_BITS (OP_MASK_IMM8, 	OP_SH_IMM8);	break;
      case '6': USE_BITS (OP_MASK_RS,		OP_SH_RS);	break;
      case '7': USE_BITS (OP_MASK_DSPACC,	OP_SH_DSPACC);	break;
      case '8': USE_BITS (OP_MASK_WRDSP,	OP_SH_WRDSP);	break;
      case '9': USE_BITS (OP_MASK_DSPACC_S,	OP_SH_DSPACC_S);break;
      case '0': USE_BITS (OP_MASK_DSPSFT,	OP_SH_DSPSFT);	break;
      case '\'': USE_BITS (OP_MASK_RDDSP,	OP_SH_RDDSP);	break;
      case ':': USE_BITS (OP_MASK_DSPSFT_7,	OP_SH_DSPSFT_7);break;
      case '@': USE_BITS (OP_MASK_IMM10,	OP_SH_IMM10);	break;
      case '!': USE_BITS (OP_MASK_MT_U,		OP_SH_MT_U);	break;
      case '$': USE_BITS (OP_MASK_MT_H,		OP_SH_MT_H);	break;
      case '*': USE_BITS (OP_MASK_MTACC_T,	OP_SH_MTACC_T);	break;
      case '&': USE_BITS (OP_MASK_MTACC_D,	OP_SH_MTACC_D);	break;
      case 'g': USE_BITS (OP_MASK_RD,		OP_SH_RD);	break;
      default:
	as_bad (_("internal: bad mips opcode (unknown operand type `%c'): %s %s"),
		c, opc->name, opc->args);
	return 0;
      }
#undef USE_BITS
  if (used_bits != 0xffffffff)
    {
      as_bad (_("internal: bad mips opcode (bits 0x%lx undefined): %s %s"),
	      ~used_bits & 0xffffffff, opc->name, opc->args);
      return 0;
    }
  return 1;
}

/* UDI immediates.  */
struct mips_immed {
  char		type;
  unsigned int	shift;
  unsigned long	mask;
  const char *	desc;
};

static const struct mips_immed mips_immed[] = {
  { '1',	OP_SH_UDI1,	OP_MASK_UDI1,		0},
  { '2',	OP_SH_UDI2,	OP_MASK_UDI2,		0},
  { '3',	OP_SH_UDI3,	OP_MASK_UDI3,		0},
  { '4',	OP_SH_UDI4,	OP_MASK_UDI4,		0},
  { 0,0,0,0 }
};

/* Check whether an odd floating-point register is allowed.  */
static int
mips_oddfpreg_ok (const struct mips_opcode *insn, int argnum)
{
  const char *s = insn->name;

  if (insn->pinfo == INSN_MACRO)
    /* Let a macro pass, we'll catch it later when it is expanded.  */
    return 1;

  if (ISA_HAS_ODD_SINGLE_FPR (mips_opts.isa))
    {
      /* Allow odd registers for single-precision ops.  */
      switch (insn->pinfo & (FP_S | FP_D))
	{
	case FP_S:
	case 0:
	  return 1;	/* both single precision - ok */
	case FP_D:
	  return 0;	/* both double precision - fail */
	default:
	  break;
	}

      /* Cvt.w.x and cvt.x.w allow an odd register for a 'w' or 's' operand.  */
      s = strchr (insn->name, '.');
      if (argnum == 2)
	s = s != NULL ? strchr (s + 1, '.') : NULL;
      return (s != NULL && (s[1] == 'w' || s[1] == 's'));
    } 

  /* Single-precision coprocessor loads and moves are OK too.  */
  if ((insn->pinfo & FP_S)
      && (insn->pinfo & (INSN_COPROC_MEMORY_DELAY | INSN_STORE_MEMORY
			 | INSN_LOAD_COPROC_DELAY | INSN_COPROC_MOVE_DELAY)))
    return 1;

  return 0;
}

/* This routine assembles an instruction into its binary format.  As a
   side effect, it sets one of the global variables imm_reloc or
   offset_reloc to the type of relocation to do if one of the operands
   is an address expression.  */

static void
mips_ip (char *str, struct mips_cl_insn *ip)
{
  char *s;
  const char *args;
  char c = 0;
  struct mips_opcode *insn;
  char *argsStart;
  unsigned int regno;
  unsigned int lastregno = 0;
  unsigned int lastpos = 0;
  unsigned int limlo, limhi;
  char *s_reset;
  char save_c = 0;
  offsetT min_range, max_range;
  int argnum;
  unsigned int rtype;

  insn_error = NULL;

  /* If the instruction contains a '.', we first try to match an instruction
     including the '.'.  Then we try again without the '.'.  */
  insn = NULL;
  for (s = str; *s != '\0' && !ISSPACE (*s); ++s)
    continue;

  /* If we stopped on whitespace, then replace the whitespace with null for
     the call to hash_find.  Save the character we replaced just in case we
     have to re-parse the instruction.  */
  if (ISSPACE (*s))
    {
      save_c = *s;
      *s++ = '\0';
    }

  insn = (struct mips_opcode *) hash_find (op_hash, str);

  /* If we didn't find the instruction in the opcode table, try again, but
     this time with just the instruction up to, but not including the
     first '.'.  */
  if (insn == NULL)
    {
      /* Restore the character we overwrite above (if any).  */
      if (save_c)
	*(--s) = save_c;

      /* Scan up to the first '.' or whitespace.  */
      for (s = str;
	   *s != '\0' && *s != '.' && !ISSPACE (*s);
	   ++s)
	continue;

      /* If we did not find a '.', then we can quit now.  */
      if (*s != '.')
	{
	  insn_error = "unrecognized opcode";
	  return;
	}

      /* Lookup the instruction in the hash table.  */
      *s++ = '\0';
      if ((insn = (struct mips_opcode *) hash_find (op_hash, str)) == NULL)
	{
	  insn_error = "unrecognized opcode";
	  return;
	}
    }

  argsStart = s;
  for (;;)
    {
      bfd_boolean ok;

      assert (strcmp (insn->name, str) == 0);

      if (OPCODE_IS_MEMBER (insn,
			    (mips_opts.isa
			     /* We don't check for mips_opts.mips16 here since
			        we want to allow jalx if -mips16 was specified
			        on the command line.  */
			     | (file_ase_mips16 ? INSN_MIPS16 : 0)
	      		     | (mips_opts.ase_mdmx ? INSN_MDMX : 0)
	      		     | (mips_opts.ase_dsp ? INSN_DSP : 0)
	      		     | ((mips_opts.ase_dsp && ISA_SUPPORTS_DSP64_ASE)
				? INSN_DSP64 : 0)
	      		     | (mips_opts.ase_dspr2 ? INSN_DSPR2 : 0)
	      		     | (mips_opts.ase_mt ? INSN_MT : 0)
			     | (mips_opts.ase_mips3d ? INSN_MIPS3D : 0)
			     | (mips_opts.ase_smartmips ? INSN_SMARTMIPS : 0)),
			    mips_opts.arch))
	ok = TRUE;
      else
	ok = FALSE;

      if (insn->pinfo != INSN_MACRO)
	{
	  if (mips_opts.arch == CPU_R4650 && (insn->pinfo & FP_D) != 0)
	    ok = FALSE;

	  if (mips_opts.arch == CPU_OCTEON
	      && octeon_error_on_unsupported
	      && ((insn->pinfo & FP_D) != 0
	          || (insn->pinfo & FP_S) !=0
	          || strcmp (insn->name, "prefx") == 0))
	    {
	      insn_error = "opcode not implemented in Octeon";
	      return;
	    }

	  if (mips_opts.arch == CPU_OCTEON
	      && octeon_error_on_unsupported
	      && (strcmp (insn->name, "swc2") == 0
		  || strcmp (insn->name, "lwc2") == 0
		  || strcmp (insn->name, "sdc2") == 0
		  || strcmp (insn->name, "ldc2") == 0
		  || strcmp (insn->name, "bc2f") == 0
		  || strcmp (insn->name, "bc2t") == 0
		  || strcmp (insn->name, "mfc2") == 0
		  || strcmp (insn->name, "mtc2") == 0
		  || strcmp (insn->name, "ctc2") == 0
		  || strcmp (insn->name, "cfc2") == 0
		  || strcmp (insn->name, "mfhc2") == 0
		  || strcmp (insn->name, "mthc2") == 0))
	    {
	      insn_error = "opcode not implemented in Octeon";
	      return;
	    }

	  /* Issue a warning message for Octeon unaligned load/store 
	     instructions used when octeon_use_unalign is not set.  */
	  if (mips_opts.arch == CPU_OCTEON && ! octeon_use_unalign
              && (strcmp (insn->name, "ulw") == 0
                  || strcmp (insn->name, "uld") == 0
                  || strcmp (insn->name, "usw") == 0
                  || strcmp (insn->name, "usd") == 0))
            {
              static char buf[120];
              sprintf (buf, _("Octeon specific unaligned load/store instructions are not allowed with -mno-octeon-useun"));
              insn_error = buf;
              return;
            }

	  /* Issue a warning message for MIPS unaligned load/store 
	     instructions used when octeon_use_unalign is set.  */
          if (mips_opts.arch == CPU_OCTEON && octeon_use_unalign
              && (strcmp (insn->name, "lwl") == 0
                  || strcmp (insn->name, "lwr") == 0
                  || strcmp (insn->name, "ldl") == 0
                  || strcmp (insn->name, "ldr") == 0
                  || strcmp (insn->name, "sdl") == 0
                  || strcmp (insn->name, "sdr") == 0
                  || strcmp (insn->name, "swr") == 0
                  || strcmp (insn->name, "swl") == 0))
            {
              static char buf[100];
              sprintf (buf, _("Unaligned load/store instructions are not allowed with -mocteon-useun"));
              insn_error = buf;
              return;
            }
	}

      /* Octeon has its own version of dmtc2/dmfc2 instructions, error on 
	 other formats.  */
      if (mips_opts.arch == CPU_OCTEON 
	  && (strcmp (insn->name, "dmtc2") == 0
	      || strcmp (insn->name, "dmfc2") == 0)
	  && (insn->membership & INSN_OCTEON) != INSN_OCTEON)
	{
	  static char buf[100];
	  sprintf (buf,
		   _("opcode not supported in %s"),
		     mips_cpu_info_from_arch (mips_opts.arch)->name);
	  insn_error = buf;
	  ok = FALSE;
	}

      if (! ok)
	{
	  if (insn + 1 < &mips_opcodes[NUMOPCODES]
	      && strcmp (insn->name, insn[1].name) == 0)
	    {
	      ++insn;
	      continue;
	    }
	  else
	    {
	      if (!insn_error)
		{
		  static char buf[100];
		  sprintf (buf,
			   _("opcode not supported on this processor: %s (%s)"),
			   mips_cpu_info_from_arch (mips_opts.arch)->name,
			   mips_cpu_info_from_isa (mips_opts.isa)->name);
		  insn_error = buf;
		}
	      if (save_c)
		*(--s) = save_c;
	      return;
	    }
	}

      create_insn (ip, insn);
      insn_error = NULL;
      argnum = 1;
      for (args = insn->args;; ++args)
	{
	  int is_mdmx;

	  s += strspn (s, " \t");
	  is_mdmx = 0;
	  switch (*args)
	    {
	    case '\0':		/* end of args */
	      if (*s == '\0')
		return;
	      break;

	    case '2': /* dsp 2-bit unsigned immediate in bit 11 */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      if ((unsigned long) imm_expr.X_add_number != 1
		  && (unsigned long) imm_expr.X_add_number != 3)
		{
		  as_bad (_("BALIGN immediate not 1 or 3 (%lu)"),
			  (unsigned long) imm_expr.X_add_number);
		}
	      INSERT_OPERAND (BP, *ip, imm_expr.X_add_number);
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

	    case '3': /* dsp 3-bit unsigned immediate in bit 21 */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      if (imm_expr.X_add_number & ~OP_MASK_SA3)
		{
		  as_bad (_("DSP immediate not in range 0..%d (%lu)"),
			  OP_MASK_SA3, (unsigned long) imm_expr.X_add_number);
		}
	      INSERT_OPERAND (SA3, *ip, imm_expr.X_add_number);
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

	    case '4': /* dsp 4-bit unsigned immediate in bit 21 */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      if (imm_expr.X_add_number & ~OP_MASK_SA4)
		{
		  as_bad (_("DSP immediate not in range 0..%d (%lu)"),
			  OP_MASK_SA4, (unsigned long) imm_expr.X_add_number);
		}
	      INSERT_OPERAND (SA4, *ip, imm_expr.X_add_number);
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

	    case '5': /* dsp 8-bit unsigned immediate in bit 16 */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      if (imm_expr.X_add_number & ~OP_MASK_IMM8)
		{
		  as_bad (_("DSP immediate not in range 0..%d (%lu)"),
			  OP_MASK_IMM8, (unsigned long) imm_expr.X_add_number);
		}
	      INSERT_OPERAND (IMM8, *ip, imm_expr.X_add_number);
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

	    case '6': /* dsp 5-bit unsigned immediate in bit 21 */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      if (imm_expr.X_add_number & ~OP_MASK_RS)
		{
		  as_bad (_("DSP immediate not in range 0..%d (%lu)"),
			  OP_MASK_RS, (unsigned long) imm_expr.X_add_number);
		}
	      INSERT_OPERAND (RS, *ip, imm_expr.X_add_number);
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

	    case '7': /* four dsp accumulators in bits 11,12 */ 
	      if (s[0] == '$' && s[1] == 'a' && s[2] == 'c' &&
		  s[3] >= '0' && s[3] <= '3')
		{
		  regno = s[3] - '0';
		  s += 4;
		  INSERT_OPERAND (DSPACC, *ip, regno);
		  continue;
		}
	      else
		as_bad (_("Invalid dsp acc register"));
	      break;

	    case '8': /* dsp 6-bit unsigned immediate in bit 11 */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      if (imm_expr.X_add_number & ~OP_MASK_WRDSP)
		{
		  as_bad (_("DSP immediate not in range 0..%d (%lu)"),
			  OP_MASK_WRDSP,
			  (unsigned long) imm_expr.X_add_number);
		}
	      INSERT_OPERAND (WRDSP, *ip, imm_expr.X_add_number);
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

	    case '9': /* four dsp accumulators in bits 21,22 */
	      if (s[0] == '$' && s[1] == 'a' && s[2] == 'c' &&
		  s[3] >= '0' && s[3] <= '3')
		{
		  regno = s[3] - '0';
		  s += 4;
		  INSERT_OPERAND (DSPACC_S, *ip, regno);
		  continue;
		}
	      else
		as_bad (_("Invalid dsp acc register"));
	      break;

	    case '0': /* dsp 6-bit signed immediate in bit 20 */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      min_range = -((OP_MASK_DSPSFT + 1) >> 1);
	      max_range = ((OP_MASK_DSPSFT + 1) >> 1) - 1;
	      if (imm_expr.X_add_number < min_range ||
		  imm_expr.X_add_number > max_range)
		{
		  as_bad (_("DSP immediate not in range %ld..%ld (%ld)"),
			  (long) min_range, (long) max_range,
			  (long) imm_expr.X_add_number);
		}
	      INSERT_OPERAND (DSPSFT, *ip, imm_expr.X_add_number);
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

	    case '\'': /* dsp 6-bit unsigned immediate in bit 16 */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      if (imm_expr.X_add_number & ~OP_MASK_RDDSP)
		{
		  as_bad (_("DSP immediate not in range 0..%d (%lu)"),
			  OP_MASK_RDDSP,
			  (unsigned long) imm_expr.X_add_number);
		}
	      INSERT_OPERAND (RDDSP, *ip, imm_expr.X_add_number);
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

	    case ':': /* dsp 7-bit signed immediate in bit 19 */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      min_range = -((OP_MASK_DSPSFT_7 + 1) >> 1);
	      max_range = ((OP_MASK_DSPSFT_7 + 1) >> 1) - 1;
	      if (imm_expr.X_add_number < min_range ||
		  imm_expr.X_add_number > max_range)
		{
		  as_bad (_("DSP immediate not in range %ld..%ld (%ld)"),
			  (long) min_range, (long) max_range,
			  (long) imm_expr.X_add_number);
		}
	      INSERT_OPERAND (DSPSFT_7, *ip, imm_expr.X_add_number);
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

	    case '@': /* dsp 10-bit signed immediate in bit 16 */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      min_range = -((OP_MASK_IMM10 + 1) >> 1);
	      max_range = ((OP_MASK_IMM10 + 1) >> 1) - 1;
	      if (imm_expr.X_add_number < min_range ||
		  imm_expr.X_add_number > max_range)
		{
		  as_bad (_("DSP immediate not in range %ld..%ld (%ld)"),
			  (long) min_range, (long) max_range,
			  (long) imm_expr.X_add_number);
		}
	      INSERT_OPERAND (IMM10, *ip, imm_expr.X_add_number);
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

            case '!': /* MT usermode flag bit.  */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      if (imm_expr.X_add_number & ~OP_MASK_MT_U)
		as_bad (_("MT usermode bit not 0 or 1 (%lu)"),
			(unsigned long) imm_expr.X_add_number);
	      INSERT_OPERAND (MT_U, *ip, imm_expr.X_add_number);
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

            case '$': /* MT load high flag bit.  */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      if (imm_expr.X_add_number & ~OP_MASK_MT_H)
		as_bad (_("MT load high bit not 0 or 1 (%lu)"),
			(unsigned long) imm_expr.X_add_number);
	      INSERT_OPERAND (MT_H, *ip, imm_expr.X_add_number);
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

	    case '*': /* four dsp accumulators in bits 18,19 */ 
	      if (s[0] == '$' && s[1] == 'a' && s[2] == 'c' &&
		  s[3] >= '0' && s[3] <= '3')
		{
		  regno = s[3] - '0';
		  s += 4;
		  INSERT_OPERAND (MTACC_T, *ip, regno);
		  continue;
		}
	      else
		as_bad (_("Invalid dsp/smartmips acc register"));
	      break;

	    case '&': /* four dsp accumulators in bits 13,14 */ 
	      if (s[0] == '$' && s[1] == 'a' && s[2] == 'c' &&
		  s[3] >= '0' && s[3] <= '3')
		{
		  regno = s[3] - '0';
		  s += 4;
		  INSERT_OPERAND (MTACC_D, *ip, regno);
		  continue;
		}
	      else
		as_bad (_("Invalid dsp/smartmips acc register"));
	      break;

	    case ',':
	      ++argnum;
	      if (*s++ == *args)
		continue;
	      s--;
	      switch (*++args)
		{
		case 'r':
		case 'v':
		  INSERT_OPERAND (RS, *ip, lastregno);
		  continue;

		case 'w':
		  INSERT_OPERAND (RT, *ip, lastregno);
		  continue;

		case 'W':
		  INSERT_OPERAND (FT, *ip, lastregno);
		  continue;

		case 'V':
		  INSERT_OPERAND (FS, *ip, lastregno);
		  continue;
		}
	      break;

	    case '(':
	      /* Handle optional base register.
		 Either the base register is omitted or
		 we must have a left paren.  */
	      /* This is dependent on the next operand specifier
		 is a base register specification.  */
	      assert (args[1] == 'b' || args[1] == '5'
		      || args[1] == '-' || args[1] == '4');
	      if (*s == '\0')
		return;

	    case ')':		/* these must match exactly */
	    case '[':
	    case ']':
	      if (*s++ == *args)
		continue;
	      break;

	    case '+':		/* Opcode extension character.  */
	      switch (*++args)
		{
		case '1':	/* UDI immediates.  */
		case '2':
		case '3':
		case '4':
		  {
		    const struct mips_immed *imm = mips_immed;

		    while (imm->type && imm->type != *args)
		      ++imm;
		    if (! imm->type)
		      internalError ();
		    my_getExpression (&imm_expr, s);
		    check_absolute_expr (ip, &imm_expr);
		    if ((unsigned long) imm_expr.X_add_number & ~imm->mask)
		      {
		        as_warn (_("Illegal %s number (%lu, 0x%lx)"),
				 imm->desc ? imm->desc : ip->insn_mo->name,
				 (unsigned long) imm_expr.X_add_number,
				 (unsigned long) imm_expr.X_add_number);
			      imm_expr.X_add_number &= imm->mask;
		      }
		    ip->insn_opcode |= ((unsigned long) imm_expr.X_add_number
					<< imm->shift);
		    imm_expr.X_op = O_absent;
		    s = expr_end;
		  }
		  continue;
		  
		case 'A':		/* ins/ext position, becomes LSB.  */
		  limlo = 0;
		  limhi = 31;
		  goto do_lsb;
		case 'E':
		  limlo = 32;
		  limhi = 63;
		  goto do_lsb;
do_lsb:
		  my_getExpression (&imm_expr, s);
		  check_absolute_expr (ip, &imm_expr);
		  if ((unsigned long) imm_expr.X_add_number < limlo
		      || (unsigned long) imm_expr.X_add_number > limhi)
		    {
		      as_bad (_("Improper position (%lu)"),
			      (unsigned long) imm_expr.X_add_number);
		      imm_expr.X_add_number = limlo;
		    }
		  lastpos = imm_expr.X_add_number;
		  INSERT_OPERAND (SHAMT, *ip, imm_expr.X_add_number);
		  imm_expr.X_op = O_absent;
		  s = expr_end;
		  continue;

		case 'B':		/* ins size, becomes MSB.  */
		  limlo = 1;
		  limhi = 32;
		  goto do_msb;
		case 'F':
		  limlo = 33;
		  limhi = 64;
		  goto do_msb;
do_msb:
		  my_getExpression (&imm_expr, s);
		  check_absolute_expr (ip, &imm_expr);
		  /* Check for negative input so that small negative numbers
		     will not succeed incorrectly.  The checks against
		     (pos+size) transitively check "size" itself,
		     assuming that "pos" is reasonable.  */
		  if ((long) imm_expr.X_add_number < 0
		      || ((unsigned long) imm_expr.X_add_number
			  + lastpos) < limlo
		      || ((unsigned long) imm_expr.X_add_number
			  + lastpos) > limhi)
		    {
		      as_bad (_("Improper insert size (%lu, position %lu)"),
			      (unsigned long) imm_expr.X_add_number,
			      (unsigned long) lastpos);
		      imm_expr.X_add_number = limlo - lastpos;
		    }
		  INSERT_OPERAND (INSMSB, *ip,
				 lastpos + imm_expr.X_add_number - 1);
		  imm_expr.X_op = O_absent;
		  s = expr_end;
		  continue;

		case 'C':		/* ext size, becomes MSBD.  */
		  limlo = 1;
		  limhi = 32;
		  goto do_msbd;
		case 'G':
		  limlo = 33;
		  limhi = 64;
		  goto do_msbd;
		case 'H':
		  limlo = 33;
		  limhi = 64;
		  goto do_msbd;
do_msbd:
		  my_getExpression (&imm_expr, s);
		  check_absolute_expr (ip, &imm_expr);
		  /* Check for negative input so that small negative numbers
		     will not succeed incorrectly.  The checks against
		     (pos+size) transitively check "size" itself,
		     assuming that "pos" is reasonable.  */
		  if ((long) imm_expr.X_add_number < 0
		      || ((unsigned long) imm_expr.X_add_number
			  + lastpos) < limlo
		      || ((unsigned long) imm_expr.X_add_number
			  + lastpos) > limhi)
		    {
		      as_bad (_("Improper extract size (%lu, position %lu)"),
			      (unsigned long) imm_expr.X_add_number,
			      (unsigned long) lastpos);
		      imm_expr.X_add_number = limlo - lastpos;
		    }
		  INSERT_OPERAND (EXTMSBD, *ip, imm_expr.X_add_number - 1);
		  imm_expr.X_op = O_absent;
		  s = expr_end;
		  continue;

		case 'D':
		  /* +D is for disassembly only; never match.  */
		  break;

		case 'I':
		  /* "+I" is like "I", except that imm2_expr is used.  */
		  my_getExpression (&imm2_expr, s);
		  if (imm2_expr.X_op != O_big
		      && imm2_expr.X_op != O_constant)
		  insn_error = _("absolute expression required");
		  if (HAVE_32BIT_GPRS)
		    normalize_constant_expr (&imm2_expr);
		  s = expr_end;
		  continue;

		case 'T': /* Coprocessor register.  */
		  /* +T is for disassembly only; never match.  */
		  break;

		case 't': /* Coprocessor register number.  */
		  if (s[0] == '$' && ISDIGIT (s[1]))
		    {
		      ++s;
		      regno = 0;
		      do
		        {
			  regno *= 10;
			  regno += *s - '0';
			  ++s;
			}
		      while (ISDIGIT (*s));
		      if (regno > 31)
			as_bad (_("Invalid register number (%d)"), regno);
		      else
			{
			  INSERT_OPERAND (RT, *ip, regno);
			  continue;
			}
		    }
		  else
		    as_bad (_("Invalid coprocessor 0 register number"));
		  break;

		default:
		  as_bad (_("internal: bad mips opcode (unknown extension operand type `+%c'): %s %s"),
		    *args, insn->name, insn->args);
		  /* Further processing is fruitless.  */
		  return;
		}
	      break;

	    case '<':		/* must be at least one digit */
	      /*
	       * According to the manual, if the shift amount is greater
	       * than 31 or less than 0, then the shift amount should be
	       * mod 32.  In reality the mips assembler issues an error.
	       * We issue a warning and mask out all but the low 5 bits.
	       */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      if ((unsigned long) imm_expr.X_add_number > 31)
		as_warn (_("Improper shift amount (%lu)"),
			 (unsigned long) imm_expr.X_add_number);
	      INSERT_OPERAND (SHAMT, *ip, imm_expr.X_add_number);
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

	    case '>':		/* shift amount minus 32 */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      if ((unsigned long) imm_expr.X_add_number < 32
		  || (unsigned long) imm_expr.X_add_number > 63)
		break;
	      INSERT_OPERAND (SHAMT, *ip, imm_expr.X_add_number - 32);
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

             case '^':           /* must be at least one digit */
	      /* Decode 5-bits of bbit0/1's bit index amount. If the value is 
		 greater than 31, issue a warning and mask out all but the low 
		 5 bits.  */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      if ((unsigned long) imm_expr.X_add_number > 31)
		{
		  as_warn (_("Improper bit index amount (%lu)"),
			   (unsigned long) imm_expr.X_add_number);
		  imm_expr.X_add_number &= OP_MASK_BITIND;
		}
	      ip->insn_opcode |= imm_expr.X_add_number << OP_SH_BITIND;
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;
 
            case '~':           /* bit index minus 32 */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      if ((unsigned long) imm_expr.X_add_number < 32
	          || (unsigned long) imm_expr.X_add_number > 63)
	        break;
	      ip->insn_opcode |= (imm_expr.X_add_number - 32) << OP_SH_BITIND;
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

	    case 'k':		/* cache code */
	    case 'h':		/* prefx code */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      if ((unsigned long) imm_expr.X_add_number > 31)
		as_warn (_("Invalid value for `%s' (%lu)"),
			 ip->insn_mo->name,
			 (unsigned long) imm_expr.X_add_number);
	      if (*args == 'k')
		INSERT_OPERAND (CACHE, *ip, imm_expr.X_add_number);
	      else
		INSERT_OPERAND (PREFX, *ip, imm_expr.X_add_number);
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

	    case 'c':		/* break code */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      if ((unsigned long) imm_expr.X_add_number > OP_MASK_CODE)
		as_warn (_("Code for %s not in range 0..1023 (%lu)"),
			 ip->insn_mo->name,
			 (unsigned long) imm_expr.X_add_number);
	      INSERT_OPERAND (CODE, *ip, imm_expr.X_add_number);
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

	    case 'q':		/* lower break code */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      if ((unsigned long) imm_expr.X_add_number > OP_MASK_CODE2)
		as_warn (_("Lower code for %s not in range 0..1023 (%lu)"),
			 ip->insn_mo->name,
			 (unsigned long) imm_expr.X_add_number);
	      INSERT_OPERAND (CODE2, *ip, imm_expr.X_add_number);
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

	    case 'y':
	      /* Decode 10-bits of seqi/snei's signed constant offset. Issue 
		 a warning message if the value is not within the range.  */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      if (((unsigned long) imm_expr.X_add_number + 0x200) > 1023)
		{
		  as_warn (_("Illegal 10-bit signed constant (%lu)"),
			   (unsigned long) imm_expr.X_add_number);
		 	   imm_expr.X_add_number &= OP_MASK_CODE2;
		}
	      ip->insn_opcode |= (imm_expr.X_add_number & OP_MASK_CODE2)
				  << OP_SH_CODE2;
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

	    case 'B':           /* 20-bit syscall/break code.  */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      if ((unsigned long) imm_expr.X_add_number > OP_MASK_CODE20)
		as_warn (_("Code for %s not in range 0..1048575 (%lu)"),
			 ip->insn_mo->name,
			 (unsigned long) imm_expr.X_add_number);
	      INSERT_OPERAND (CODE20, *ip, imm_expr.X_add_number);
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

	    case 'C':           /* Coprocessor code */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      if ((unsigned long) imm_expr.X_add_number > OP_MASK_COPZ)
		{
		  as_warn (_("Coproccesor code > 25 bits (%lu)"),
			   (unsigned long) imm_expr.X_add_number);
		  imm_expr.X_add_number &= OP_MASK_COPZ;
		}
	      INSERT_OPERAND (COPZ, *ip, imm_expr.X_add_number);
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

	    case 'J':           /* 19-bit wait code.  */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      if ((unsigned long) imm_expr.X_add_number > OP_MASK_CODE19)
		{
		  as_warn (_("Illegal 19-bit code (%lu)"),
			   (unsigned long) imm_expr.X_add_number);
		  imm_expr.X_add_number &= OP_MASK_CODE19;
		}
	      INSERT_OPERAND (CODE19, *ip, imm_expr.X_add_number);
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

	    case 'P':		/* Performance register.  */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      if (imm_expr.X_add_number != 0 && imm_expr.X_add_number != 1)
		as_warn (_("Invalid performance register (%lu)"),
			 (unsigned long) imm_expr.X_add_number);
	      INSERT_OPERAND (PERFREG, *ip, imm_expr.X_add_number);
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

	    case 'G':		/* Coprocessor destination register.  */
	      if (((ip->insn_opcode >> OP_SH_OP) & OP_MASK_OP) == OP_OP_COP0)
		ok = reg_lookup (&s, RTYPE_NUM | RTYPE_CP0, &regno);
	      else
		ok = reg_lookup (&s, RTYPE_NUM | RTYPE_GP, &regno);
	      INSERT_OPERAND (RD, *ip, regno);
	      if (ok) 
		{
		  lastregno = regno;
		  continue;
		}
	      else
		break;

	    case 'b':		/* base register */
	    case 'd':		/* destination register */
	    case 's':		/* source register */
	    case 't':		/* target register */
	    case 'r':		/* both target and source */
	    case 'v':		/* both dest and source */
	    case 'w':		/* both dest and target */
	    case 'E':		/* coprocessor target register */
	    case 'K':		/* 'rdhwr' destination register */
	    case 'x':		/* ignore register name */
	    case 'z':		/* must be zero register */
	    case 'U':           /* destination register (clo/clz).  */
	    case 'g':		/* coprocessor destination register */
	      s_reset = s;	      
	      if (*args == 'E' || *args == 'K')
		ok = reg_lookup (&s, RTYPE_NUM, &regno);
	      else
		{
		  ok = reg_lookup (&s, RTYPE_NUM | RTYPE_GP, &regno);
		  if (regno == AT && ! mips_opts.noat)
		    as_warn ("Used $at without \".set noat\"");
		}
	      if (ok)
		{
		  c = *args;
		  if (*s == ' ')
		    ++s;
		  if (args[1] != *s)
		    {
		      if (c == 'r' || c == 'v' || c == 'w')
			{
			  regno = lastregno;
			  s = s_reset;
			  ++args;
			}
		    }
		  /* 'z' only matches $0.  */
		  if (c == 'z' && regno != 0)
		    break;

	/* Now that we have assembled one operand, we use the args string
	 * to figure out where it goes in the instruction.  */
		  switch (c)
		    {
		    case 'r':
		    case 's':
		    case 'v':
		    case 'b':
		      INSERT_OPERAND (RS, *ip, regno);
		      break;
		    case 'd':
		    case 'G':
		    case 'K':
		    case 'g':
		      INSERT_OPERAND (RD, *ip, regno);
		      break;
		    case 'U':
		      INSERT_OPERAND (RD, *ip, regno);
		      INSERT_OPERAND (RT, *ip, regno);
		      break;
		    case 'w':
		    case 't':
		    case 'E':
		      INSERT_OPERAND (RT, *ip, regno);
		      break;
		    case 'x':
		      /* This case exists because on the r3000 trunc
			 expands into a macro which requires a gp
			 register.  On the r6000 or r4000 it is
			 assembled into a single instruction which
			 ignores the register.  Thus the insn version
			 is MIPS_ISA2 and uses 'x', and the macro
			 version is MIPS_ISA1 and uses 't'.  */
		      break;
		    case 'z':
		      /* This case is for the div instruction, which
			 acts differently if the destination argument
			 is $0.  This only matches $0, and is checked
			 outside the switch.  */
		      break;
		    case 'D':
		      /* Itbl operand; not yet implemented. FIXME ?? */
		      break;
		      /* What about all other operands like 'i', which
			 can be specified in the opcode table? */
		    }
		  lastregno = regno;
		  continue;
		}
	      switch (*args++)
		{
		case 'r':
		case 'v':
		  INSERT_OPERAND (RS, *ip, lastregno);
		  continue;
		case 'w':
		  INSERT_OPERAND (RT, *ip, lastregno);
		  continue;
		}
	      break;

	    case 'O':		/* MDMX alignment immediate constant.  */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      if ((unsigned long) imm_expr.X_add_number > OP_MASK_ALN)
		as_warn ("Improper align amount (%ld), using low bits",
			 (long) imm_expr.X_add_number);
	      INSERT_OPERAND (ALN, *ip, imm_expr.X_add_number);
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

	    case 'Q':		/* MDMX vector, element sel, or const.  */
	      if (s[0] != '$')
		{
		  /* MDMX Immediate.  */
		  my_getExpression (&imm_expr, s);
		  check_absolute_expr (ip, &imm_expr);
		  if ((unsigned long) imm_expr.X_add_number > OP_MASK_FT)
		    as_warn (_("Invalid MDMX Immediate (%ld)"),
			     (long) imm_expr.X_add_number);
		  INSERT_OPERAND (FT, *ip, imm_expr.X_add_number);
		  if (ip->insn_opcode & (OP_MASK_VSEL << OP_SH_VSEL))
		    ip->insn_opcode |= MDMX_FMTSEL_IMM_QH << OP_SH_VSEL;
		  else
		    ip->insn_opcode |= MDMX_FMTSEL_IMM_OB << OP_SH_VSEL;
		  imm_expr.X_op = O_absent;
		  s = expr_end;
		  continue;
		}
	      /* Not MDMX Immediate.  Fall through.  */
	    case 'X':           /* MDMX destination register.  */
	    case 'Y':           /* MDMX source register.  */
	    case 'Z':           /* MDMX target register.  */
	      is_mdmx = 1;
	    case 'D':		/* floating point destination register */
	    case 'S':		/* floating point source register */
	    case 'T':		/* floating point target register */
	    case 'R':		/* floating point source register */
	    case 'V':
	    case 'W':
	      rtype = RTYPE_FPU;
	      if (is_mdmx
		  || (mips_opts.ase_mdmx
		      && (ip->insn_mo->pinfo & FP_D)
		      && (ip->insn_mo->pinfo & (INSN_COPROC_MOVE_DELAY
						| INSN_COPROC_MEMORY_DELAY
						| INSN_LOAD_COPROC_DELAY
						| INSN_LOAD_MEMORY_DELAY
						| INSN_STORE_MEMORY))))
		rtype |= RTYPE_VEC;
	      s_reset = s;
	      if (mips_opts.arch == CPU_OCTEON && octeon_error_on_unsupported)
		{
		  insn_error = "opcode not implemented in Octeon";
		  return;
		}
	      if (reg_lookup (&s, rtype, &regno))
		{
		  if ((regno & 1) != 0
		      && HAVE_32BIT_FPRS
		      && ! mips_oddfpreg_ok (ip->insn_mo, argnum))
		    as_warn (_("Float register should be even, was %d"),
			     regno);

		  c = *args;
		  if (*s == ' ')
		    ++s;
		  if (args[1] != *s)
		    {
		      if (c == 'V' || c == 'W')
			{
			  regno = lastregno;
			  s = s_reset;
			  ++args;
			}
		    }
		  switch (c)
		    {
		    case 'D':
		    case 'X':
		      INSERT_OPERAND (FD, *ip, regno);
		      break;
		    case 'V':
		    case 'S':
		    case 'Y':
		      INSERT_OPERAND (FS, *ip, regno);
		      break;
		    case 'Q':
		      /* This is like 'Z', but also needs to fix the MDMX
			 vector/scalar select bits.  Note that the
			 scalar immediate case is handled above.  */
		      if (*s == '[')
			{
			  int is_qh = (ip->insn_opcode & (1 << OP_SH_VSEL));
			  int max_el = (is_qh ? 3 : 7);
			  s++;
			  my_getExpression(&imm_expr, s);
			  check_absolute_expr (ip, &imm_expr);
			  s = expr_end;
			  if (imm_expr.X_add_number > max_el)
			    as_bad(_("Bad element selector %ld"),
				   (long) imm_expr.X_add_number);
			  imm_expr.X_add_number &= max_el;
			  ip->insn_opcode |= (imm_expr.X_add_number
					      << (OP_SH_VSEL +
						  (is_qh ? 2 : 1)));
			  imm_expr.X_op = O_absent;
			  if (*s != ']')
			    as_warn(_("Expecting ']' found '%s'"), s);
			  else
			    s++;
			}
		      else
                        {
                          if (ip->insn_opcode & (OP_MASK_VSEL << OP_SH_VSEL))
                            ip->insn_opcode |= (MDMX_FMTSEL_VEC_QH
						<< OP_SH_VSEL);
			  else
			    ip->insn_opcode |= (MDMX_FMTSEL_VEC_OB <<
						OP_SH_VSEL);
			}
                      /* Fall through */
		    case 'W':
		    case 'T':
		    case 'Z':
		      INSERT_OPERAND (FT, *ip, regno);
		      break;
		    case 'R':
		      INSERT_OPERAND (FR, *ip, regno);
		      break;
		    }
		  lastregno = regno;
		  continue;
		}

	      switch (*args++)
		{
		case 'V':
		  INSERT_OPERAND (FS, *ip, lastregno);
		  continue;
		case 'W':
		  INSERT_OPERAND (FT, *ip, lastregno);
		  continue;
		}
	      break;

	    case 'I':
	      my_getExpression (&imm_expr, s);
	      if (imm_expr.X_op != O_big
		  && imm_expr.X_op != O_constant)
		insn_error = _("absolute expression required");
	      if (HAVE_32BIT_GPRS)
		normalize_constant_expr (&imm_expr);
	      s = expr_end;
	      continue;

	    case 'A':
	      my_getExpression (&offset_expr, s);
	      normalize_address_expr (&offset_expr);
	      *imm_reloc = BFD_RELOC_32;
	      s = expr_end;
	      continue;

	    case 'F':
	    case 'L':
	    case 'f':
	    case 'l':
	      {
		int f64;
		int using_gprs;
		char *save_in;
		char *err;
		unsigned char temp[8];
		int len;
		unsigned int length;
		segT seg;
		subsegT subseg;
		char *p;

		/* These only appear as the last operand in an
		   instruction, and every instruction that accepts
		   them in any variant accepts them in all variants.
		   This means we don't have to worry about backing out
		   any changes if the instruction does not match.

		   The difference between them is the size of the
		   floating point constant and where it goes.  For 'F'
		   and 'L' the constant is 64 bits; for 'f' and 'l' it
		   is 32 bits.  Where the constant is placed is based
		   on how the MIPS assembler does things:
		    F -- .rdata
		    L -- .lit8
		    f -- immediate value
		    l -- .lit4

		    The .lit4 and .lit8 sections are only used if
		    permitted by the -G argument.

		    The code below needs to know whether the target register
		    is 32 or 64 bits wide.  It relies on the fact 'f' and
		    'F' are used with GPR-based instructions and 'l' and
		    'L' are used with FPR-based instructions.  */

		f64 = *args == 'F' || *args == 'L';
		using_gprs = *args == 'F' || *args == 'f';

		save_in = input_line_pointer;
		input_line_pointer = s;
		err = md_atof (f64 ? 'd' : 'f', (char *) temp, &len);
		length = len;
		s = input_line_pointer;
		input_line_pointer = save_in;
		if (err != NULL && *err != '\0')
		  {
		    as_bad (_("Bad floating point constant: %s"), err);
		    memset (temp, '\0', sizeof temp);
		    length = f64 ? 8 : 4;
		  }

		assert (length == (unsigned) (f64 ? 8 : 4));

		if (*args == 'f'
		    || (*args == 'l'
			&& (g_switch_value < 4
			    || (temp[0] == 0 && temp[1] == 0)
			    || (temp[2] == 0 && temp[3] == 0))))
		  {
		    imm_expr.X_op = O_constant;
		    if (! target_big_endian)
		      imm_expr.X_add_number = bfd_getl32 (temp);
		    else
		      imm_expr.X_add_number = bfd_getb32 (temp);
		  }
		else if (length > 4
			 && ! mips_disable_float_construction
			 /* Constants can only be constructed in GPRs and
			    copied to FPRs if the GPRs are at least as wide
			    as the FPRs.  Force the constant into memory if
			    we are using 64-bit FPRs but the GPRs are only
			    32 bits wide.  */
			 && (using_gprs
			     || ! (HAVE_64BIT_FPRS && HAVE_32BIT_GPRS))
			 && ((temp[0] == 0 && temp[1] == 0)
			     || (temp[2] == 0 && temp[3] == 0))
			 && ((temp[4] == 0 && temp[5] == 0)
			     || (temp[6] == 0 && temp[7] == 0)))
		  {
		    /* The value is simple enough to load with a couple of
                       instructions.  If using 32-bit registers, set
                       imm_expr to the high order 32 bits and offset_expr to
                       the low order 32 bits.  Otherwise, set imm_expr to
                       the entire 64 bit constant.  */
		    if (using_gprs ? HAVE_32BIT_GPRS : HAVE_32BIT_FPRS)
		      {
			imm_expr.X_op = O_constant;
			offset_expr.X_op = O_constant;
			if (! target_big_endian)
			  {
			    imm_expr.X_add_number = bfd_getl32 (temp + 4);
			    offset_expr.X_add_number = bfd_getl32 (temp);
			  }
			else
			  {
			    imm_expr.X_add_number = bfd_getb32 (temp);
			    offset_expr.X_add_number = bfd_getb32 (temp + 4);
			  }
			if (offset_expr.X_add_number == 0)
			  offset_expr.X_op = O_absent;
		      }
		    else if (sizeof (imm_expr.X_add_number) > 4)
		      {
			imm_expr.X_op = O_constant;
			if (! target_big_endian)
			  imm_expr.X_add_number = bfd_getl64 (temp);
			else
			  imm_expr.X_add_number = bfd_getb64 (temp);
		      }
		    else
		      {
			imm_expr.X_op = O_big;
			imm_expr.X_add_number = 4;
			if (! target_big_endian)
			  {
			    generic_bignum[0] = bfd_getl16 (temp);
			    generic_bignum[1] = bfd_getl16 (temp + 2);
			    generic_bignum[2] = bfd_getl16 (temp + 4);
			    generic_bignum[3] = bfd_getl16 (temp + 6);
			  }
			else
			  {
			    generic_bignum[0] = bfd_getb16 (temp + 6);
			    generic_bignum[1] = bfd_getb16 (temp + 4);
			    generic_bignum[2] = bfd_getb16 (temp + 2);
			    generic_bignum[3] = bfd_getb16 (temp);
			  }
		      }
		  }
		else
		  {
		    const char *newname;
		    segT new_seg;

		    /* Switch to the right section.  */
		    seg = now_seg;
		    subseg = now_subseg;
		    switch (*args)
		      {
		      default: /* unused default case avoids warnings.  */
		      case 'L':
			newname = RDATA_SECTION_NAME;
			if (g_switch_value >= 8)
			  newname = ".lit8";
			break;
		      case 'F':
			newname = RDATA_SECTION_NAME;
			break;
		      case 'l':
			assert (g_switch_value >= 4);
			newname = ".lit4";
			break;
		      }
		    new_seg = subseg_new (newname, (subsegT) 0);
		    if (IS_ELF)
		      bfd_set_section_flags (stdoutput, new_seg,
					     (SEC_ALLOC
					      | SEC_LOAD
					      | SEC_READONLY
					      | SEC_DATA));
		    frag_align (*args == 'l' ? 2 : 3, 0, 0);
		    if (IS_ELF && strcmp (TARGET_OS, "elf") != 0)
		      record_alignment (new_seg, 4);
		    else
		      record_alignment (new_seg, *args == 'l' ? 2 : 3);
		    if (seg == now_seg)
		      as_bad (_("Can't use floating point insn in this section"));

		    /* Set the argument to the current address in the
		       section.  */
		    offset_expr.X_op = O_symbol;
		    offset_expr.X_add_symbol =
		      symbol_new ("L0\001", now_seg,
				  (valueT) frag_now_fix (), frag_now);
		    offset_expr.X_add_number = 0;

		    /* Put the floating point number into the section.  */
		    p = frag_more ((int) length);
		    memcpy (p, temp, length);

		    /* Switch back to the original section.  */
		    subseg_set (seg, subseg);
		  }
	      }
	      continue;

	    case 'i':		/* 16 bit unsigned immediate */
	    case 'j':		/* 16 bit signed immediate */
	      *imm_reloc = BFD_RELOC_LO16;
	      if (my_getSmallExpression (&imm_expr, imm_reloc, s) == 0)
		{
		  int more;
		  offsetT minval, maxval;

		  more = (insn + 1 < &mips_opcodes[NUMOPCODES]
			  && strcmp (insn->name, insn[1].name) == 0);

		  /* If the expression was written as an unsigned number,
		     only treat it as signed if there are no more
		     alternatives.  */
		  if (more
		      && *args == 'j'
		      && sizeof (imm_expr.X_add_number) <= 4
		      && imm_expr.X_op == O_constant
		      && imm_expr.X_add_number < 0
		      && imm_expr.X_unsigned
		      && HAVE_64BIT_GPRS)
		    break;

		  /* For compatibility with older assemblers, we accept
		     0x8000-0xffff as signed 16-bit numbers when only
		     signed numbers are allowed.  */
		  if (*args == 'i')
		    minval = 0, maxval = 0xffff;
		  else if (more)
		    minval = -0x8000, maxval = 0x7fff;
		  else
		    minval = -0x8000, maxval = 0xffff;

		  if (imm_expr.X_op != O_constant
		      || imm_expr.X_add_number < minval
		      || imm_expr.X_add_number > maxval)
		    {
		      if (more)
			break;
		      if (imm_expr.X_op == O_constant
			  || imm_expr.X_op == O_big)
			as_bad (_("expression out of range"));
		    }
		}
	      s = expr_end;
	      continue;

	    case 'o':		/* 16 bit offset */
	      /* Check whether there is only a single bracketed expression
		 left.  If so, it must be the base register and the
		 constant must be zero.  */
	      if (*s == '(' && strchr (s + 1, '(') == 0)
		{
		  offset_expr.X_op = O_constant;
		  offset_expr.X_add_number = 0;
		  continue;
		}

	      /* If this value won't fit into a 16 bit offset, then go
		 find a macro that will generate the 32 bit offset
		 code pattern.  */
	      if (my_getSmallExpression (&offset_expr, offset_reloc, s) == 0
		  && (offset_expr.X_op != O_constant
		      || offset_expr.X_add_number >= 0x8000
		      || offset_expr.X_add_number < -0x8000))
		break;

	      s = expr_end;
	      continue;

	    case 'p':		/* pc relative offset */
	      *offset_reloc = BFD_RELOC_16_PCREL_S2;
	      my_getExpression (&offset_expr, s);
	      s = expr_end;
	      continue;

	    case 'u':		/* upper 16 bits */
	      if (my_getSmallExpression (&imm_expr, imm_reloc, s) == 0
		  && imm_expr.X_op == O_constant
		  && (imm_expr.X_add_number < 0
		      || imm_expr.X_add_number >= 0x10000))
		as_bad (_("lui expression not in range 0..65535"));
	      s = expr_end;
	      continue;

	    case 'a':		/* 26 bit address */
	      my_getExpression (&offset_expr, s);
	      s = expr_end;
	      *offset_reloc = BFD_RELOC_MIPS_JMP;
	      continue;

	    case 'N':		/* 3 bit branch condition code */
	    case 'M':		/* 3 bit compare condition code */
	      rtype = RTYPE_CCC;
	      if (ip->insn_mo->pinfo & (FP_D| FP_S))
		rtype |= RTYPE_FCC;
	      if (!reg_lookup (&s, rtype, &regno))
		break;
	      if ((strcmp(str + strlen(str) - 3, ".ps") == 0
		   || strcmp(str + strlen(str) - 5, "any2f") == 0
		   || strcmp(str + strlen(str) - 5, "any2t") == 0)
		  && (regno & 1) != 0)
		as_warn(_("Condition code register should be even for %s, was %d"),
			str, regno);
	      if ((strcmp(str + strlen(str) - 5, "any4f") == 0
		   || strcmp(str + strlen(str) - 5, "any4t") == 0)
		  && (regno & 3) != 0)
		as_warn(_("Condition code register should be 0 or 4 for %s, was %d"),
			str, regno);
	      if (*args == 'N')
		INSERT_OPERAND (BCC, *ip, regno);
	      else
		INSERT_OPERAND (CCC, *ip, regno);
	      continue;

	    case 'H':
	      if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
		s += 2;
	      if (ISDIGIT (*s))
		{
		  c = 0;
		  do
		    {
		      c *= 10;
		      c += *s - '0';
		      ++s;
		    }
		  while (ISDIGIT (*s));
		}
	      else
		c = 8; /* Invalid sel value.  */

	      if (c > 7)
		as_bad (_("invalid coprocessor sub-selection value (0-7)"));
	      ip->insn_opcode |= c;
	      continue;

	    case 'e':
	      /* Must be at least one digit.  */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);

	      if ((unsigned long) imm_expr.X_add_number
		  > (unsigned long) OP_MASK_VECBYTE)
		{
		  as_bad (_("bad byte vector index (%ld)"),
			   (long) imm_expr.X_add_number);
		  imm_expr.X_add_number = 0;
		}

	      INSERT_OPERAND (VECBYTE, *ip, imm_expr.X_add_number);
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

	    case '%':
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);

	      if ((unsigned long) imm_expr.X_add_number
		  > (unsigned long) OP_MASK_VECALIGN)
		{
		  as_bad (_("bad byte vector index (%ld)"),
			   (long) imm_expr.X_add_number);
		  imm_expr.X_add_number = 0;
		}

	      INSERT_OPERAND (VECALIGN, *ip, imm_expr.X_add_number);
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

	    default:
	      as_bad (_("bad char = '%c'\n"), *args);
	      internalError ();
	    }
	  break;
	}
      /* Args don't match.  */
      if (insn + 1 < &mips_opcodes[NUMOPCODES] &&
	  !strcmp (insn->name, insn[1].name))
	{
	  ++insn;
	  s = argsStart;
	  insn_error = _("illegal operands");
	  continue;
	}
      if (save_c)
	*(--s) = save_c;
      insn_error = _("illegal operands");
      return;
    }
}

#define SKIP_SPACE_TABS(S) { while (*(S) == ' ' || *(S) == '\t') ++(S); }

/* This routine assembles an instruction into its binary format when
   assembling for the mips16.  As a side effect, it sets one of the
   global variables imm_reloc or offset_reloc to the type of
   relocation to do if one of the operands is an address expression.
   It also sets mips16_small and mips16_ext if the user explicitly
   requested a small or extended instruction.  */

static void
mips16_ip (char *str, struct mips_cl_insn *ip)
{
  char *s;
  const char *args;
  struct mips_opcode *insn;
  char *argsstart;
  unsigned int regno;
  unsigned int lastregno = 0;
  char *s_reset;
  size_t i;

  insn_error = NULL;

  mips16_small = FALSE;
  mips16_ext = FALSE;

  for (s = str; ISLOWER (*s); ++s)
    ;
  switch (*s)
    {
    case '\0':
      break;

    case ' ':
      *s++ = '\0';
      break;

    case '.':
      if (s[1] == 't' && s[2] == ' ')
	{
	  *s = '\0';
	  mips16_small = TRUE;
	  s += 3;
	  break;
	}
      else if (s[1] == 'e' && s[2] == ' ')
	{
	  *s = '\0';
	  mips16_ext = TRUE;
	  s += 3;
	  break;
	}
      /* Fall through.  */
    default:
      insn_error = _("unknown opcode");
      return;
    }

  if (mips_opts.noautoextend && ! mips16_ext)
    mips16_small = TRUE;

  if ((insn = (struct mips_opcode *) hash_find (mips16_op_hash, str)) == NULL)
    {
      insn_error = _("unrecognized opcode");
      return;
    }

  argsstart = s;
  for (;;)
    {
      bfd_boolean ok;

      assert (strcmp (insn->name, str) == 0);

      if (OPCODE_IS_MEMBER (insn, mips_opts.isa, mips_opts.arch))
	ok = TRUE;
      else
	ok = FALSE;

      if (! ok)
	{
	  if (insn + 1 < &mips16_opcodes[bfd_mips16_num_opcodes]
	      && strcmp (insn->name, insn[1].name) == 0)
	    {
	      ++insn;
	      continue;
	    }
	  else
	    {
	      if (!insn_error)
		{
		  static char buf[100];
		  sprintf (buf,
			   _("opcode not supported on this processor: %s (%s)"),
			   mips_cpu_info_from_arch (mips_opts.arch)->name,
			   mips_cpu_info_from_isa (mips_opts.isa)->name);
		  insn_error = buf;
		}
	      return;
	    }
	}

      create_insn (ip, insn);
      imm_expr.X_op = O_absent;
      imm_reloc[0] = BFD_RELOC_UNUSED;
      imm_reloc[1] = BFD_RELOC_UNUSED;
      imm_reloc[2] = BFD_RELOC_UNUSED;
      imm2_expr.X_op = O_absent;
      offset_expr.X_op = O_absent;
      offset_reloc[0] = BFD_RELOC_UNUSED;
      offset_reloc[1] = BFD_RELOC_UNUSED;
      offset_reloc[2] = BFD_RELOC_UNUSED;
      for (args = insn->args; 1; ++args)
	{
	  int c;

	  if (*s == ' ')
	    ++s;

	  /* In this switch statement we call break if we did not find
             a match, continue if we did find a match, or return if we
             are done.  */

	  c = *args;
	  switch (c)
	    {
	    case '\0':
	      if (*s == '\0')
		{
		  /* Stuff the immediate value in now, if we can.  */
		  if (imm_expr.X_op == O_constant
		      && *imm_reloc > BFD_RELOC_UNUSED
		      && insn->pinfo != INSN_MACRO)
		    {
		      valueT tmp;

		      switch (*offset_reloc)
			{
			  case BFD_RELOC_MIPS16_HI16_S:
			    tmp = (imm_expr.X_add_number + 0x8000) >> 16;
			    break;

			  case BFD_RELOC_MIPS16_HI16:
			    tmp = imm_expr.X_add_number >> 16;
			    break;

			  case BFD_RELOC_MIPS16_LO16:
			    tmp = ((imm_expr.X_add_number + 0x8000) & 0xffff)
				  - 0x8000;
			    break;

			  case BFD_RELOC_UNUSED:
			    tmp = imm_expr.X_add_number;
			    break;

			  default:
			    internalError ();
			}
		      *offset_reloc = BFD_RELOC_UNUSED;

		      mips16_immed (NULL, 0, *imm_reloc - BFD_RELOC_UNUSED,
				    tmp, TRUE, mips16_small,
				    mips16_ext, &ip->insn_opcode,
				    &ip->use_extend, &ip->extend);
		      imm_expr.X_op = O_absent;
		      *imm_reloc = BFD_RELOC_UNUSED;
		    }

		  return;
		}
	      break;

	    case ',':
	      if (*s++ == c)
		continue;
	      s--;
	      switch (*++args)
		{
		case 'v':
		  MIPS16_INSERT_OPERAND (RX, *ip, lastregno);
		  continue;
		case 'w':
		  MIPS16_INSERT_OPERAND (RY, *ip, lastregno);
		  continue;
		}
	      break;

	    case '(':
	    case ')':
	      if (*s++ == c)
		continue;
	      break;

	    case 'v':
	    case 'w':
	      if (s[0] != '$')
		{
		  if (c == 'v')
		    MIPS16_INSERT_OPERAND (RX, *ip, lastregno);
		  else
		    MIPS16_INSERT_OPERAND (RY, *ip, lastregno);
		  ++args;
		  continue;
		}
	      /* Fall through.  */
	    case 'x':
	    case 'y':
	    case 'z':
	    case 'Z':
	    case '0':
	    case 'S':
	    case 'R':
	    case 'X':
	    case 'Y':
  	      s_reset = s;
	      if (!reg_lookup (&s, RTYPE_NUM | RTYPE_GP, &regno))
		{
		  if (c == 'v' || c == 'w')
		    {
		      if (c == 'v')
			MIPS16_INSERT_OPERAND (RX, *ip, lastregno);
		      else
			MIPS16_INSERT_OPERAND (RY, *ip, lastregno);
		      ++args;
		      continue;
		    }
		  break;
		}

	      if (*s == ' ')
		++s;
	      if (args[1] != *s)
		{
		  if (c == 'v' || c == 'w')
		    {
		      regno = mips16_to_32_reg_map[lastregno];
		      s = s_reset;
		      ++args;
		    }
		}

	      switch (c)
		{
		case 'x':
		case 'y':
		case 'z':
		case 'v':
		case 'w':
		case 'Z':
		  regno = mips32_to_16_reg_map[regno];
		  break;

		case '0':
		  if (regno != 0)
		    regno = ILLEGAL_REG;
		  break;

		case 'S':
		  if (regno != SP)
		    regno = ILLEGAL_REG;
		  break;

		case 'R':
		  if (regno != RA)
		    regno = ILLEGAL_REG;
		  break;

		case 'X':
		case 'Y':
		  if (regno == AT && ! mips_opts.noat)
		    as_warn (_("used $at without \".set noat\""));
		  break;

		default:
		  internalError ();
		}

	      if (regno == ILLEGAL_REG)
		break;

	      switch (c)
		{
		case 'x':
		case 'v':
		  MIPS16_INSERT_OPERAND (RX, *ip, regno);
		  break;
		case 'y':
		case 'w':
		  MIPS16_INSERT_OPERAND (RY, *ip, regno);
		  break;
		case 'z':
		  MIPS16_INSERT_OPERAND (RZ, *ip, regno);
		  break;
		case 'Z':
		  MIPS16_INSERT_OPERAND (MOVE32Z, *ip, regno);
		case '0':
		case 'S':
		case 'R':
		  break;
		case 'X':
		  MIPS16_INSERT_OPERAND (REGR32, *ip, regno);
		  break;
		case 'Y':
		  regno = ((regno & 7) << 2) | ((regno & 0x18) >> 3);
		  MIPS16_INSERT_OPERAND (REG32R, *ip, regno);
		  break;
		default:
		  internalError ();
		}

	      lastregno = regno;
	      continue;

	    case 'P':
	      if (strncmp (s, "$pc", 3) == 0)
		{
		  s += 3;
		  continue;
		}
	      break;

	    case '5':
	    case 'H':
	    case 'W':
	    case 'D':
	    case 'j':
	    case 'V':
	    case 'C':
	    case 'U':
	    case 'k':
	    case 'K':
	      i = my_getSmallExpression (&imm_expr, imm_reloc, s);
	      if (i > 0)
		{
		  if (imm_expr.X_op != O_constant)
		    {
		      mips16_ext = TRUE;
		      ip->use_extend = TRUE;
		      ip->extend = 0;
		    }
		  else
		    {
		      /* We need to relax this instruction.  */
		      *offset_reloc = *imm_reloc;
		      *imm_reloc = (int) BFD_RELOC_UNUSED + c;
		    }
		  s = expr_end;
		  continue;
		}
	      *imm_reloc = BFD_RELOC_UNUSED;
	      /* Fall through.  */
	    case '<':
	    case '>':
	    case '[':
	    case ']':
	    case '4':
	    case '8':
	      my_getExpression (&imm_expr, s);
	      if (imm_expr.X_op == O_register)
		{
		  /* What we thought was an expression turned out to
                     be a register.  */

		  if (s[0] == '(' && args[1] == '(')
		    {
		      /* It looks like the expression was omitted
			 before a register indirection, which means
			 that the expression is implicitly zero.  We
			 still set up imm_expr, so that we handle
			 explicit extensions correctly.  */
		      imm_expr.X_op = O_constant;
		      imm_expr.X_add_number = 0;
		      *imm_reloc = (int) BFD_RELOC_UNUSED + c;
		      continue;
		    }

		  break;
		}

	      /* We need to relax this instruction.  */
	      *imm_reloc = (int) BFD_RELOC_UNUSED + c;
	      s = expr_end;
	      continue;

	    case 'p':
	    case 'q':
	    case 'A':
	    case 'B':
	    case 'E':
	      /* We use offset_reloc rather than imm_reloc for the PC
                 relative operands.  This lets macros with both
                 immediate and address operands work correctly.  */
	      my_getExpression (&offset_expr, s);

	      if (offset_expr.X_op == O_register)
		break;

	      /* We need to relax this instruction.  */
	      *offset_reloc = (int) BFD_RELOC_UNUSED + c;
	      s = expr_end;
	      continue;

	    case '6':		/* break code */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      if ((unsigned long) imm_expr.X_add_number > 63)
		as_warn (_("Invalid value for `%s' (%lu)"),
			 ip->insn_mo->name,
			 (unsigned long) imm_expr.X_add_number);
	      MIPS16_INSERT_OPERAND (IMM6, *ip, imm_expr.X_add_number);
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

	    case 'a':		/* 26 bit address */
	      my_getExpression (&offset_expr, s);
	      s = expr_end;
	      *offset_reloc = BFD_RELOC_MIPS16_JMP;
	      ip->insn_opcode <<= 16;
	      continue;

	    case 'l':		/* register list for entry macro */
	    case 'L':		/* register list for exit macro */
	      {
		int mask;

		if (c == 'l')
		  mask = 0;
		else
		  mask = 7 << 3;
		while (*s != '\0')
		  {
		    unsigned int freg, reg1, reg2;

		    while (*s == ' ' || *s == ',')
		      ++s;
		    if (reg_lookup (&s, RTYPE_GP | RTYPE_NUM, &reg1))
		      freg = 0;
		    else if (reg_lookup (&s, RTYPE_FPU, &reg1))
		      freg = 1;
		    else
		      {
			as_bad (_("can't parse register list"));
			break;
		      }
		    if (*s == ' ')
		      ++s;
		    if (*s != '-')
		      reg2 = reg1;
		    else
		      {
			++s;
			if (!reg_lookup (&s, freg ? RTYPE_FPU 
					 : (RTYPE_GP | RTYPE_NUM), &reg2))
			  {
			    as_bad (_("invalid register list"));
			    break;
			  }
		      }
		    if (freg && reg1 == 0 && reg2 == 0 && c == 'L')
		      {
			mask &= ~ (7 << 3);
			mask |= 5 << 3;
		      }
		    else if (freg && reg1 == 0 && reg2 == 1 && c == 'L')
		      {
			mask &= ~ (7 << 3);
			mask |= 6 << 3;
		      }
		    else if (reg1 == 4 && reg2 >= 4 && reg2 <= 7 && c != 'L')
		      mask |= (reg2 - 3) << 3;
		    else if (reg1 == 16 && reg2 >= 16 && reg2 <= 17)
		      mask |= (reg2 - 15) << 1;
		    else if (reg1 == RA && reg2 == RA)
		      mask |= 1;
		    else
		      {
			as_bad (_("invalid register list"));
			break;
		      }
		  }
		/* The mask is filled in in the opcode table for the
                   benefit of the disassembler.  We remove it before
                   applying the actual mask.  */
		ip->insn_opcode &= ~ ((7 << 3) << MIPS16OP_SH_IMM6);
		ip->insn_opcode |= mask << MIPS16OP_SH_IMM6;
	      }
	    continue;

	    case 'm':		/* Register list for save insn.  */
	    case 'M':		/* Register list for restore insn.  */
	      {
		int opcode = 0;
		int framesz = 0, seen_framesz = 0;
		int args = 0, statics = 0, sregs = 0;

		while (*s != '\0')
		  {
		    unsigned int reg1, reg2;

		    SKIP_SPACE_TABS (s);
		    while (*s == ',')
		      ++s;
		    SKIP_SPACE_TABS (s);

		    my_getExpression (&imm_expr, s);
		    if (imm_expr.X_op == O_constant)
		      {
			/* Handle the frame size.  */
			if (seen_framesz)
			  {
			    as_bad (_("more than one frame size in list"));
			    break;
			  }
			seen_framesz = 1;
			framesz = imm_expr.X_add_number;
			imm_expr.X_op = O_absent;
			s = expr_end;
			continue;
		      }

		    if (! reg_lookup (&s, RTYPE_GP | RTYPE_NUM, &reg1))
		      {
			as_bad (_("can't parse register list"));
			break;
		      }

		    while (*s == ' ')
		      ++s;

		    if (*s != '-')
		      reg2 = reg1;
		    else
		      {
			++s;
			if (! reg_lookup (&s, RTYPE_GP | RTYPE_NUM, &reg2)
			    || reg2 < reg1)
			  {
			    as_bad (_("can't parse register list"));
			    break;
			  }
		      }

		    while (reg1 <= reg2)
		      {
			if (reg1 >= 4 && reg1 <= 7)
			  {
			    if (!seen_framesz)
				/* args $a0-$a3 */
				args |= 1 << (reg1 - 4);
			    else
				/* statics $a0-$a3 */
				statics |= 1 << (reg1 - 4);
			  }
			else if ((reg1 >= 16 && reg1 <= 23) || reg1 == 30)
			  {
			    /* $s0-$s8 */
			    sregs |= 1 << ((reg1 == 30) ? 8 : (reg1 - 16));
			  }
			else if (reg1 == 31)
			  {
			    /* Add $ra to insn.  */
			    opcode |= 0x40;
			  }
			else
			  {
			    as_bad (_("unexpected register in list"));
			    break;
			  }
			if (++reg1 == 24)
			  reg1 = 30;
		      }
		  }

		/* Encode args/statics combination.  */
		if (args & statics)
		  as_bad (_("arg/static registers overlap"));
		else if (args == 0xf)
		  /* All $a0-$a3 are args.  */
		  opcode |= MIPS16_ALL_ARGS << 16;
		else if (statics == 0xf)
		  /* All $a0-$a3 are statics.  */
		  opcode |= MIPS16_ALL_STATICS << 16;
		else 
		  {
		    int narg = 0, nstat = 0;

		    /* Count arg registers.  */
		    while (args & 0x1)
		      {
			args >>= 1;
			narg++;
		      }
		    if (args != 0)
		      as_bad (_("invalid arg register list"));

		    /* Count static registers.  */
		    while (statics & 0x8)
		      {
			statics = (statics << 1) & 0xf;
			nstat++;
		      }
		    if (statics != 0) 
		      as_bad (_("invalid static register list"));

		    /* Encode args/statics.  */
		    opcode |= ((narg << 2) | nstat) << 16;
		  }

		/* Encode $s0/$s1.  */
		if (sregs & (1 << 0))		/* $s0 */
		  opcode |= 0x20;
		if (sregs & (1 << 1))		/* $s1 */
		  opcode |= 0x10;
		sregs >>= 2;

		if (sregs != 0)
		  {
		    /* Count regs $s2-$s8.  */
		    int nsreg = 0;
		    while (sregs & 1)
		      {
			sregs >>= 1;
			nsreg++;
		      }
		    if (sregs != 0)
		      as_bad (_("invalid static register list"));
		    /* Encode $s2-$s8. */
		    opcode |= nsreg << 24;
		  }

		/* Encode frame size.  */
		if (!seen_framesz)
		  as_bad (_("missing frame size"));
		else if ((framesz & 7) != 0 || framesz < 0
			 || framesz > 0xff * 8)
		  as_bad (_("invalid frame size"));
		else if (framesz != 128 || (opcode >> 16) != 0)
		  {
		    framesz /= 8;
		    opcode |= (((framesz & 0xf0) << 16)
			     | (framesz & 0x0f));
		  }

		/* Finally build the instruction.  */
		if ((opcode >> 16) != 0 || framesz == 0)
		  {
		    ip->use_extend = TRUE;
		    ip->extend = opcode >> 16;
		  }
		ip->insn_opcode |= opcode & 0x7f;
	      }
	    continue;

	    case 'e':		/* extend code */
	      my_getExpression (&imm_expr, s);
	      check_absolute_expr (ip, &imm_expr);
	      if ((unsigned long) imm_expr.X_add_number > 0x7ff)
		{
		  as_warn (_("Invalid value for `%s' (%lu)"),
			   ip->insn_mo->name,
			   (unsigned long) imm_expr.X_add_number);
		  imm_expr.X_add_number &= 0x7ff;
		}
	      ip->insn_opcode |= imm_expr.X_add_number;
	      imm_expr.X_op = O_absent;
	      s = expr_end;
	      continue;

	    default:
	      internalError ();
	    }
	  break;
	}

      /* Args don't match.  */
      if (insn + 1 < &mips16_opcodes[bfd_mips16_num_opcodes] &&
	  strcmp (insn->name, insn[1].name) == 0)
	{
	  ++insn;
	  s = argsstart;
	  continue;
	}

      insn_error = _("illegal operands");

      return;
    }
}

/* This structure holds information we know about a mips16 immediate
   argument type.  */

struct mips16_immed_operand
{
  /* The type code used in the argument string in the opcode table.  */
  int type;
  /* The number of bits in the short form of the opcode.  */
  int nbits;
  /* The number of bits in the extended form of the opcode.  */
  int extbits;
  /* The amount by which the short form is shifted when it is used;
     for example, the sw instruction has a shift count of 2.  */
  int shift;
  /* The amount by which the short form is shifted when it is stored
     into the instruction code.  */
  int op_shift;
  /* Non-zero if the short form is unsigned.  */
  int unsp;
  /* Non-zero if the extended form is unsigned.  */
  int extu;
  /* Non-zero if the value is PC relative.  */
  int pcrel;
};

/* The mips16 immediate operand types.  */

static const struct mips16_immed_operand mips16_immed_operands[] =
{
  { '<',  3,  5, 0, MIPS16OP_SH_RZ,   1, 1, 0 },
  { '>',  3,  5, 0, MIPS16OP_SH_RX,   1, 1, 0 },
  { '[',  3,  6, 0, MIPS16OP_SH_RZ,   1, 1, 0 },
  { ']',  3,  6, 0, MIPS16OP_SH_RX,   1, 1, 0 },
  { '4',  4, 15, 0, MIPS16OP_SH_IMM4, 0, 0, 0 },
  { '5',  5, 16, 0, MIPS16OP_SH_IMM5, 1, 0, 0 },
  { 'H',  5, 16, 1, MIPS16OP_SH_IMM5, 1, 0, 0 },
  { 'W',  5, 16, 2, MIPS16OP_SH_IMM5, 1, 0, 0 },
  { 'D',  5, 16, 3, MIPS16OP_SH_IMM5, 1, 0, 0 },
  { 'j',  5, 16, 0, MIPS16OP_SH_IMM5, 0, 0, 0 },
  { '8',  8, 16, 0, MIPS16OP_SH_IMM8, 1, 0, 0 },
  { 'V',  8, 16, 2, MIPS16OP_SH_IMM8, 1, 0, 0 },
  { 'C',  8, 16, 3, MIPS16OP_SH_IMM8, 1, 0, 0 },
  { 'U',  8, 16, 0, MIPS16OP_SH_IMM8, 1, 1, 0 },
  { 'k',  8, 16, 0, MIPS16OP_SH_IMM8, 0, 0, 0 },
  { 'K',  8, 16, 3, MIPS16OP_SH_IMM8, 0, 0, 0 },
  { 'p',  8, 16, 0, MIPS16OP_SH_IMM8, 0, 0, 1 },
  { 'q', 11, 16, 0, MIPS16OP_SH_IMM8, 0, 0, 1 },
  { 'A',  8, 16, 2, MIPS16OP_SH_IMM8, 1, 0, 1 },
  { 'B',  5, 16, 3, MIPS16OP_SH_IMM5, 1, 0, 1 },
  { 'E',  5, 16, 2, MIPS16OP_SH_IMM5, 1, 0, 1 }
};

#define MIPS16_NUM_IMMED \
  (sizeof mips16_immed_operands / sizeof mips16_immed_operands[0])

/* Handle a mips16 instruction with an immediate value.  This or's the
   small immediate value into *INSN.  It sets *USE_EXTEND to indicate
   whether an extended value is needed; if one is needed, it sets
   *EXTEND to the value.  The argument type is TYPE.  The value is VAL.
   If SMALL is true, an unextended opcode was explicitly requested.
   If EXT is true, an extended opcode was explicitly requested.  If
   WARN is true, warn if EXT does not match reality.  */

static void
mips16_immed (char *file, unsigned int line, int type, offsetT val,
	      bfd_boolean warn, bfd_boolean small, bfd_boolean ext,
	      unsigned long *insn, bfd_boolean *use_extend,
	      unsigned short *extend)
{
  const struct mips16_immed_operand *op;
  int mintiny, maxtiny;
  bfd_boolean needext;

  op = mips16_immed_operands;
  while (op->type != type)
    {
      ++op;
      assert (op < mips16_immed_operands + MIPS16_NUM_IMMED);
    }

  if (op->unsp)
    {
      if (type == '<' || type == '>' || type == '[' || type == ']')
	{
	  mintiny = 1;
	  maxtiny = 1 << op->nbits;
	}
      else
	{
	  mintiny = 0;
	  maxtiny = (1 << op->nbits) - 1;
	}
    }
  else
    {
      mintiny = - (1 << (op->nbits - 1));
      maxtiny = (1 << (op->nbits - 1)) - 1;
    }

  /* Branch offsets have an implicit 0 in the lowest bit.  */
  if (type == 'p' || type == 'q')
    val /= 2;

  if ((val & ((1 << op->shift) - 1)) != 0
      || val < (mintiny << op->shift)
      || val > (maxtiny << op->shift))
    needext = TRUE;
  else
    needext = FALSE;

  if (warn && ext && ! needext)
    as_warn_where (file, line,
		   _("extended operand requested but not required"));
  if (small && needext)
    as_bad_where (file, line, _("invalid unextended operand value"));

  if (small || (! ext && ! needext))
    {
      int insnval;

      *use_extend = FALSE;
      insnval = ((val >> op->shift) & ((1 << op->nbits) - 1));
      insnval <<= op->op_shift;
      *insn |= insnval;
    }
  else
    {
      long minext, maxext;
      int extval;

      if (op->extu)
	{
	  minext = 0;
	  maxext = (1 << op->extbits) - 1;
	}
      else
	{
	  minext = - (1 << (op->extbits - 1));
	  maxext = (1 << (op->extbits - 1)) - 1;
	}
      if (val < minext || val > maxext)
	as_bad_where (file, line,
		      _("operand value out of range for instruction"));

      *use_extend = TRUE;
      if (op->extbits == 16)
	{
	  extval = ((val >> 11) & 0x1f) | (val & 0x7e0);
	  val &= 0x1f;
	}
      else if (op->extbits == 15)
	{
	  extval = ((val >> 11) & 0xf) | (val & 0x7f0);
	  val &= 0xf;
	}
      else
	{
	  extval = ((val & 0x1f) << 6) | (val & 0x20);
	  val = 0;
	}

      *extend = (unsigned short) extval;
      *insn |= val;
    }
}

struct percent_op_match
{
  const char *str;
  bfd_reloc_code_real_type reloc;
};

static const struct percent_op_match mips_percent_op[] =
{
  {"%lo", BFD_RELOC_LO16},
#ifdef OBJ_ELF
  {"%call_hi", BFD_RELOC_MIPS_CALL_HI16},
  {"%call_lo", BFD_RELOC_MIPS_CALL_LO16},
  {"%call16", BFD_RELOC_MIPS_CALL16},
  {"%got_disp", BFD_RELOC_MIPS_GOT_DISP},
  {"%got_page", BFD_RELOC_MIPS_GOT_PAGE},
  {"%got_ofst", BFD_RELOC_MIPS_GOT_OFST},
  {"%got_hi", BFD_RELOC_MIPS_GOT_HI16},
  {"%got_lo", BFD_RELOC_MIPS_GOT_LO16},
  {"%got", BFD_RELOC_MIPS_GOT16},
  {"%gp_rel", BFD_RELOC_GPREL16},
  {"%half", BFD_RELOC_16},
  {"%highest", BFD_RELOC_MIPS_HIGHEST},
  {"%higher", BFD_RELOC_MIPS_HIGHER},
  {"%neg", BFD_RELOC_MIPS_SUB},
  {"%tlsgd", BFD_RELOC_MIPS_TLS_GD},
  {"%tlsldm", BFD_RELOC_MIPS_TLS_LDM},
  {"%dtprel_hi", BFD_RELOC_MIPS_TLS_DTPREL_HI16},
  {"%dtprel_lo", BFD_RELOC_MIPS_TLS_DTPREL_LO16},
  {"%tprel_hi", BFD_RELOC_MIPS_TLS_TPREL_HI16},
  {"%tprel_lo", BFD_RELOC_MIPS_TLS_TPREL_LO16},
  {"%gottprel", BFD_RELOC_MIPS_TLS_GOTTPREL},
#endif
  {"%hi", BFD_RELOC_HI16_S}
};

static const struct percent_op_match mips16_percent_op[] =
{
  {"%lo", BFD_RELOC_MIPS16_LO16},
  {"%gprel", BFD_RELOC_MIPS16_GPREL},
  {"%hi", BFD_RELOC_MIPS16_HI16_S}
};


/* Return true if *STR points to a relocation operator.  When returning true,
   move *STR over the operator and store its relocation code in *RELOC.
   Leave both *STR and *RELOC alone when returning false.  */

static bfd_boolean
parse_relocation (char **str, bfd_reloc_code_real_type *reloc)
{
  const struct percent_op_match *percent_op;
  size_t limit, i;

  if (mips_opts.mips16)
    {
      percent_op = mips16_percent_op;
      limit = ARRAY_SIZE (mips16_percent_op);
    }
  else
    {
      percent_op = mips_percent_op;
      limit = ARRAY_SIZE (mips_percent_op);
    }

  for (i = 0; i < limit; i++)
    if (strncasecmp (*str, percent_op[i].str, strlen (percent_op[i].str)) == 0)
      {
	int len = strlen (percent_op[i].str);

	if (!ISSPACE ((*str)[len]) && (*str)[len] != '(')
	  continue;

	*str += strlen (percent_op[i].str);
	*reloc = percent_op[i].reloc;

	/* Check whether the output BFD supports this relocation.
	   If not, issue an error and fall back on something safe.  */
	if (!bfd_reloc_type_lookup (stdoutput, percent_op[i].reloc))
	  {
	    as_bad ("relocation %s isn't supported by the current ABI",
		    percent_op[i].str);
	    *reloc = BFD_RELOC_UNUSED;
	  }
	return TRUE;
      }
  return FALSE;
}


/* Parse string STR as a 16-bit relocatable operand.  Store the
   expression in *EP and the relocations in the array starting
   at RELOC.  Return the number of relocation operators used.

   On exit, EXPR_END points to the first character after the expression.  */

static size_t
my_getSmallExpression (expressionS *ep, bfd_reloc_code_real_type *reloc,
		       char *str)
{
  bfd_reloc_code_real_type reversed_reloc[3];
  size_t reloc_index, i;
  int crux_depth, str_depth;
  char *crux;

  /* Search for the start of the main expression, recoding relocations
     in REVERSED_RELOC.  End the loop with CRUX pointing to the start
     of the main expression and with CRUX_DEPTH containing the number
     of open brackets at that point.  */
  reloc_index = -1;
  str_depth = 0;
  do
    {
      reloc_index++;
      crux = str;
      crux_depth = str_depth;

      /* Skip over whitespace and brackets, keeping count of the number
	 of brackets.  */
      while (*str == ' ' || *str == '\t' || *str == '(')
	if (*str++ == '(')
	  str_depth++;
    }
  while (*str == '%'
	 && reloc_index < (HAVE_NEWABI ? 3 : 1)
	 && parse_relocation (&str, &reversed_reloc[reloc_index]));

  my_getExpression (ep, crux);
  str = expr_end;

  /* Match every open bracket.  */
  while (crux_depth > 0 && (*str == ')' || *str == ' ' || *str == '\t'))
    if (*str++ == ')')
      crux_depth--;

  if (crux_depth > 0)
    as_bad ("unclosed '('");

  expr_end = str;

  if (reloc_index != 0)
    {
      prev_reloc_op_frag = frag_now;
      for (i = 0; i < reloc_index; i++)
	reloc[i] = reversed_reloc[reloc_index - 1 - i];
    }

  return reloc_index;
}

static void
my_getExpression (expressionS *ep, char *str)
{
  char *save_in;
  valueT val;

  save_in = input_line_pointer;
  input_line_pointer = str;
  expression (ep);
  expr_end = input_line_pointer;
  input_line_pointer = save_in;

  /* If we are in mips16 mode, and this is an expression based on `.',
     then we bump the value of the symbol by 1 since that is how other
     text symbols are handled.  We don't bother to handle complex
     expressions, just `.' plus or minus a constant.  */
  if (mips_opts.mips16
      && ep->X_op == O_symbol
      && strcmp (S_GET_NAME (ep->X_add_symbol), FAKE_LABEL_NAME) == 0
      && S_GET_SEGMENT (ep->X_add_symbol) == now_seg
      && symbol_get_frag (ep->X_add_symbol) == frag_now
      && symbol_constant_p (ep->X_add_symbol)
      && (val = S_GET_VALUE (ep->X_add_symbol)) == frag_now_fix ())
    S_SET_VALUE (ep->X_add_symbol, val + 1);
}

/* Turn a string in input_line_pointer into a floating point constant
   of type TYPE, and store the appropriate bytes in *LITP.  The number
   of LITTLENUMS emitted is stored in *SIZEP.  An error message is
   returned, or NULL on OK.  */

char *
md_atof (int type, char *litP, int *sizeP)
{
  int prec;
  LITTLENUM_TYPE words[4];
  char *t;
  int i;

  switch (type)
    {
    case 'f':
      prec = 2;
      break;

    case 'd':
      prec = 4;
      break;

    default:
      *sizeP = 0;
      return _("bad call to md_atof");
    }

  t = atof_ieee (input_line_pointer, type, words);
  if (t)
    input_line_pointer = t;

  *sizeP = prec * 2;

  if (! target_big_endian)
    {
      for (i = prec - 1; i >= 0; i--)
	{
	  md_number_to_chars (litP, words[i], 2);
	  litP += 2;
	}
    }
  else
    {
      for (i = 0; i < prec; i++)
	{
	  md_number_to_chars (litP, words[i], 2);
	  litP += 2;
	}
    }

  return NULL;
}

void
md_number_to_chars (char *buf, valueT val, int n)
{
  if (target_big_endian)
    number_to_chars_bigendian (buf, val, n);
  else
    number_to_chars_littleendian (buf, val, n);
}

#ifdef OBJ_ELF
static int support_64bit_objects(void)
{
  const char **list, **l;
  int yes;

  list = bfd_target_list ();
  for (l = list; *l != NULL; l++)
#ifdef TE_TMIPS
    /* This is traditional mips */
    if (strcmp (*l, "elf64-tradbigmips") == 0
	|| strcmp (*l, "elf64-tradlittlemips") == 0)
#else
    if (strcmp (*l, "elf64-bigmips") == 0
	|| strcmp (*l, "elf64-littlemips") == 0)
#endif
      break;
  yes = (*l != NULL);
  free (list);
  return yes;
}
#endif /* OBJ_ELF */

const char *md_shortopts = "O::g::G:";

struct option md_longopts[] =
{
  /* Options which specify architecture.  */
#define OPTION_ARCH_BASE    (OPTION_MD_BASE)
#define OPTION_MARCH (OPTION_ARCH_BASE + 0)
  {"march", required_argument, NULL, OPTION_MARCH},
#define OPTION_MTUNE (OPTION_ARCH_BASE + 1)
  {"mtune", required_argument, NULL, OPTION_MTUNE},
#define OPTION_MIPS1 (OPTION_ARCH_BASE + 2)
  {"mips0", no_argument, NULL, OPTION_MIPS1},
  {"mips1", no_argument, NULL, OPTION_MIPS1},
#define OPTION_MIPS2 (OPTION_ARCH_BASE + 3)
  {"mips2", no_argument, NULL, OPTION_MIPS2},
#define OPTION_MIPS3 (OPTION_ARCH_BASE + 4)
  {"mips3", no_argument, NULL, OPTION_MIPS3},
#define OPTION_MIPS4 (OPTION_ARCH_BASE + 5)
  {"mips4", no_argument, NULL, OPTION_MIPS4},
#define OPTION_MIPS5 (OPTION_ARCH_BASE + 6)
  {"mips5", no_argument, NULL, OPTION_MIPS5},
#define OPTION_MIPS32 (OPTION_ARCH_BASE + 7)
  {"mips32", no_argument, NULL, OPTION_MIPS32},
#define OPTION_MIPS64 (OPTION_ARCH_BASE + 8)
  {"mips64", no_argument, NULL, OPTION_MIPS64},
#define OPTION_MIPS32R2 (OPTION_ARCH_BASE + 9)
  {"mips32r2", no_argument, NULL, OPTION_MIPS32R2},
#define OPTION_MIPS64R2 (OPTION_ARCH_BASE + 10)
  {"mips64r2", no_argument, NULL, OPTION_MIPS64R2},

  /* Options which specify Application Specific Extensions (ASEs).  */
#define OPTION_ASE_BASE (OPTION_ARCH_BASE + 11)
#define OPTION_MIPS16 (OPTION_ASE_BASE + 0)
  {"mips16", no_argument, NULL, OPTION_MIPS16},
#define OPTION_NO_MIPS16 (OPTION_ASE_BASE + 1)
  {"no-mips16", no_argument, NULL, OPTION_NO_MIPS16},
#define OPTION_MIPS3D (OPTION_ASE_BASE + 2)
  {"mips3d", no_argument, NULL, OPTION_MIPS3D},
#define OPTION_NO_MIPS3D (OPTION_ASE_BASE + 3)
  {"no-mips3d", no_argument, NULL, OPTION_NO_MIPS3D},
#define OPTION_MDMX (OPTION_ASE_BASE + 4)
  {"mdmx", no_argument, NULL, OPTION_MDMX},
#define OPTION_NO_MDMX (OPTION_ASE_BASE + 5)
  {"no-mdmx", no_argument, NULL, OPTION_NO_MDMX},
#define OPTION_DSP (OPTION_ASE_BASE + 6)
  {"mdsp", no_argument, NULL, OPTION_DSP},
#define OPTION_NO_DSP (OPTION_ASE_BASE + 7)
  {"mno-dsp", no_argument, NULL, OPTION_NO_DSP},
#define OPTION_MT (OPTION_ASE_BASE + 8)
  {"mmt", no_argument, NULL, OPTION_MT},
#define OPTION_NO_MT (OPTION_ASE_BASE + 9)
  {"mno-mt", no_argument, NULL, OPTION_NO_MT},
#define OPTION_SMARTMIPS (OPTION_ASE_BASE + 10)
  {"msmartmips", no_argument, NULL, OPTION_SMARTMIPS},
#define OPTION_NO_SMARTMIPS (OPTION_ASE_BASE + 11)
  {"mno-smartmips", no_argument, NULL, OPTION_NO_SMARTMIPS},
#define OPTION_DSPR2 (OPTION_ASE_BASE + 12)
  {"mdspr2", no_argument, NULL, OPTION_DSPR2},
#define OPTION_NO_DSPR2 (OPTION_ASE_BASE + 13)
  {"mno-dspr2", no_argument, NULL, OPTION_NO_DSPR2},

  /* Old-style architecture options.  Don't add more of these.  */
#define OPTION_COMPAT_ARCH_BASE (OPTION_ASE_BASE + 14)
#define OPTION_M4650 (OPTION_COMPAT_ARCH_BASE + 0)
  {"m4650", no_argument, NULL, OPTION_M4650},
#define OPTION_NO_M4650 (OPTION_COMPAT_ARCH_BASE + 1)
  {"no-m4650", no_argument, NULL, OPTION_NO_M4650},
#define OPTION_M4010 (OPTION_COMPAT_ARCH_BASE + 2)
  {"m4010", no_argument, NULL, OPTION_M4010},
#define OPTION_NO_M4010 (OPTION_COMPAT_ARCH_BASE + 3)
  {"no-m4010", no_argument, NULL, OPTION_NO_M4010},
#define OPTION_M4100 (OPTION_COMPAT_ARCH_BASE + 4)
  {"m4100", no_argument, NULL, OPTION_M4100},
#define OPTION_NO_M4100 (OPTION_COMPAT_ARCH_BASE + 5)
  {"no-m4100", no_argument, NULL, OPTION_NO_M4100},
#define OPTION_M3900 (OPTION_COMPAT_ARCH_BASE + 6)
  {"m3900", no_argument, NULL, OPTION_M3900},
#define OPTION_NO_M3900 (OPTION_COMPAT_ARCH_BASE + 7)
  {"no-m3900", no_argument, NULL, OPTION_NO_M3900},

  /* Options which enable bug fixes.  */
#define OPTION_FIX_BASE    (OPTION_COMPAT_ARCH_BASE + 8)
#define OPTION_M7000_HILO_FIX (OPTION_FIX_BASE + 0)
  {"mfix7000", no_argument, NULL, OPTION_M7000_HILO_FIX},
#define OPTION_MNO_7000_HILO_FIX (OPTION_FIX_BASE + 1)
  {"no-fix-7000", no_argument, NULL, OPTION_MNO_7000_HILO_FIX},
  {"mno-fix7000", no_argument, NULL, OPTION_MNO_7000_HILO_FIX},
#define OPTION_FIX_VR4120 (OPTION_FIX_BASE + 2)
#define OPTION_NO_FIX_VR4120 (OPTION_FIX_BASE + 3)
  {"mfix-vr4120",    no_argument, NULL, OPTION_FIX_VR4120},
  {"mno-fix-vr4120", no_argument, NULL, OPTION_NO_FIX_VR4120},
#define OPTION_FIX_VR4130 (OPTION_FIX_BASE + 4)
#define OPTION_NO_FIX_VR4130 (OPTION_FIX_BASE + 5)
  {"mfix-vr4130",    no_argument, NULL, OPTION_FIX_VR4130},
  {"mno-fix-vr4130", no_argument, NULL, OPTION_NO_FIX_VR4130},

  /* Miscellaneous options.  */
#define OPTION_MISC_BASE (OPTION_FIX_BASE + 6)
#define OPTION_TRAP (OPTION_MISC_BASE + 0)
  {"trap", no_argument, NULL, OPTION_TRAP},
  {"no-break", no_argument, NULL, OPTION_TRAP},
#define OPTION_BREAK (OPTION_MISC_BASE + 1)
  {"break", no_argument, NULL, OPTION_BREAK},
  {"no-trap", no_argument, NULL, OPTION_BREAK},
#define OPTION_EB (OPTION_MISC_BASE + 2)
  {"EB", no_argument, NULL, OPTION_EB},
#define OPTION_EL (OPTION_MISC_BASE + 3)
  {"EL", no_argument, NULL, OPTION_EL},
#define OPTION_FP32 (OPTION_MISC_BASE + 4)
  {"mfp32", no_argument, NULL, OPTION_FP32},
#define OPTION_GP32 (OPTION_MISC_BASE + 5)
  {"mgp32", no_argument, NULL, OPTION_GP32},
#define OPTION_CONSTRUCT_FLOATS (OPTION_MISC_BASE + 6)
  {"construct-floats", no_argument, NULL, OPTION_CONSTRUCT_FLOATS},
#define OPTION_NO_CONSTRUCT_FLOATS (OPTION_MISC_BASE + 7)
  {"no-construct-floats", no_argument, NULL, OPTION_NO_CONSTRUCT_FLOATS},
#define OPTION_FP64 (OPTION_MISC_BASE + 8)
  {"mfp64", no_argument, NULL, OPTION_FP64},
#define OPTION_GP64 (OPTION_MISC_BASE + 9)
  {"mgp64", no_argument, NULL, OPTION_GP64},
#define OPTION_RELAX_BRANCH (OPTION_MISC_BASE + 10)
#define OPTION_NO_RELAX_BRANCH (OPTION_MISC_BASE + 11)
  {"relax-branch", no_argument, NULL, OPTION_RELAX_BRANCH},
  {"no-relax-branch", no_argument, NULL, OPTION_NO_RELAX_BRANCH},
#define OPTION_MSHARED (OPTION_MISC_BASE + 12)
#define OPTION_MNO_SHARED (OPTION_MISC_BASE + 13)
  {"mshared", no_argument, NULL, OPTION_MSHARED},
  {"mno-shared", no_argument, NULL, OPTION_MNO_SHARED},
#define OPTION_MSYM32 (OPTION_MISC_BASE + 14)
#define OPTION_MNO_SYM32 (OPTION_MISC_BASE + 15)
  {"msym32", no_argument, NULL, OPTION_MSYM32},
  {"mno-sym32", no_argument, NULL, OPTION_MNO_SYM32},

  /* ELF-specific options.  */
#ifdef OBJ_ELF
#define OPTION_ELF_BASE    (OPTION_MISC_BASE + 16)
#define OPTION_CALL_SHARED (OPTION_ELF_BASE + 0)
  {"KPIC",        no_argument, NULL, OPTION_CALL_SHARED},
  {"call_shared", no_argument, NULL, OPTION_CALL_SHARED},
#define OPTION_NON_SHARED  (OPTION_ELF_BASE + 1)
  {"non_shared",  no_argument, NULL, OPTION_NON_SHARED},
#define OPTION_XGOT        (OPTION_ELF_BASE + 2)
  {"xgot",        no_argument, NULL, OPTION_XGOT},
#define OPTION_MABI        (OPTION_ELF_BASE + 3)
  {"mabi", required_argument, NULL, OPTION_MABI},
#define OPTION_32 	   (OPTION_ELF_BASE + 4)
  {"32",          no_argument, NULL, OPTION_32},
#define OPTION_N32 	   (OPTION_ELF_BASE + 5)
  {"n32",         no_argument, NULL, OPTION_N32},
#define OPTION_64          (OPTION_ELF_BASE + 6)
  {"64",          no_argument, NULL, OPTION_64},
#define OPTION_MDEBUG      (OPTION_ELF_BASE + 7)
  {"mdebug", no_argument, NULL, OPTION_MDEBUG},
#define OPTION_NO_MDEBUG   (OPTION_ELF_BASE + 8)
  {"no-mdebug", no_argument, NULL, OPTION_NO_MDEBUG},
#define OPTION_PDR	   (OPTION_ELF_BASE + 9)
  {"mpdr", no_argument, NULL, OPTION_PDR},
#define OPTION_NO_PDR	   (OPTION_ELF_BASE + 10)
  {"mno-pdr", no_argument, NULL, OPTION_NO_PDR},
#define OPTION_MVXWORKS_PIC (OPTION_ELF_BASE + 11)
  {"mvxworks-pic", no_argument, NULL, OPTION_MVXWORKS_PIC},
#endif /* OBJ_ELF */

#define OPTION_MOCTEON_UNSUPPORTED (OPTION_MISC_BASE + 28)
#define OPTION_NO_MOCTEON_UNSUPPORTED (OPTION_MISC_BASE + 29)
  {"mocteon-unsupported", no_argument, NULL, OPTION_MOCTEON_UNSUPPORTED},
  {"mno-octeon-unsupported", no_argument, NULL, OPTION_NO_MOCTEON_UNSUPPORTED},

#define OPTION_MOCTEON_USEUN (OPTION_MISC_BASE + 30)
#define OPTION_NO_MOCTEON_USEUN (OPTION_MISC_BASE + 31)
  {"mocteon-useun", no_argument, NULL, OPTION_MOCTEON_USEUN},
  {"mno-octeon-useun", no_argument, NULL, OPTION_NO_MOCTEON_USEUN},

  {NULL, no_argument, NULL, 0}
};
size_t md_longopts_size = sizeof (md_longopts);

/* Set STRING_PTR (either &mips_arch_string or &mips_tune_string) to
   NEW_VALUE.  Warn if another value was already specified.  Note:
   we have to defer parsing the -march and -mtune arguments in order
   to handle 'from-abi' correctly, since the ABI might be specified
   in a later argument.  */

static void
mips_set_option_string (const char **string_ptr, const char *new_value)
{
  if (*string_ptr != 0 && strcasecmp (*string_ptr, new_value) != 0)
    as_warn (_("A different %s was already specified, is now %s"),
	     string_ptr == &mips_arch_string ? "-march" : "-mtune",
	     new_value);

  *string_ptr = new_value;
}

int
md_parse_option (int c, char *arg)
{
  switch (c)
    {
    case OPTION_CONSTRUCT_FLOATS:
      mips_disable_float_construction = 0;
      break;

    case OPTION_NO_CONSTRUCT_FLOATS:
      mips_disable_float_construction = 1;
      break;

    case OPTION_TRAP:
      mips_trap = 1;
      break;

    case OPTION_BREAK:
      mips_trap = 0;
      break;

    case OPTION_EB:
      target_big_endian = 1;
      break;

    case OPTION_EL:
      target_big_endian = 0;
      break;

    case OPTION_MOCTEON_UNSUPPORTED:
      octeon_error_on_unsupported = 1;
      break;

    case OPTION_NO_MOCTEON_UNSUPPORTED:
      octeon_error_on_unsupported = 0;
      break;

    case OPTION_MOCTEON_USEUN:
      octeon_use_unalign = 1;
      break;

    case OPTION_NO_MOCTEON_USEUN:
      octeon_use_unalign = 0;
      break;

    case 'O':
      if (arg && arg[0] == '0')
	mips_optimize = 1;
      else
	mips_optimize = 2;
      break;

    case 'g':
      if (arg == NULL)
	mips_debug = 2;
      else
	mips_debug = atoi (arg);
      break;

    case OPTION_MIPS1:
      file_mips_isa = ISA_MIPS1;
      break;

    case OPTION_MIPS2:
      file_mips_isa = ISA_MIPS2;
      break;

    case OPTION_MIPS3:
      file_mips_isa = ISA_MIPS3;
      break;

    case OPTION_MIPS4:
      file_mips_isa = ISA_MIPS4;
      break;

    case OPTION_MIPS5:
      file_mips_isa = ISA_MIPS5;
      break;

    case OPTION_MIPS32:
      file_mips_isa = ISA_MIPS32;
      break;

    case OPTION_MIPS32R2:
      file_mips_isa = ISA_MIPS32R2;
      break;

    case OPTION_MIPS64R2:
      file_mips_isa = ISA_MIPS64R2;
      break;

    case OPTION_MIPS64:
      file_mips_isa = ISA_MIPS64;
      break;

    case OPTION_MTUNE:
      mips_set_option_string (&mips_tune_string, arg);
      break;

    case OPTION_MARCH:
      mips_set_option_string (&mips_arch_string, arg);
      break;

    case OPTION_M4650:
      mips_set_option_string (&mips_arch_string, "4650");
      mips_set_option_string (&mips_tune_string, "4650");
      break;

    case OPTION_NO_M4650:
      break;

    case OPTION_M4010:
      mips_set_option_string (&mips_arch_string, "4010");
      mips_set_option_string (&mips_tune_string, "4010");
      break;

    case OPTION_NO_M4010:
      break;

    case OPTION_M4100:
      mips_set_option_string (&mips_arch_string, "4100");
      mips_set_option_string (&mips_tune_string, "4100");
      break;

    case OPTION_NO_M4100:
      break;

    case OPTION_M3900:
      mips_set_option_string (&mips_arch_string, "3900");
      mips_set_option_string (&mips_tune_string, "3900");
      break;

    case OPTION_NO_M3900:
      break;

    case OPTION_MDMX:
      mips_opts.ase_mdmx = 1;
      break;

    case OPTION_NO_MDMX:
      mips_opts.ase_mdmx = 0;
      break;

    case OPTION_DSP:
      mips_opts.ase_dsp = 1;
      mips_opts.ase_dspr2 = 0;
      break;

    case OPTION_NO_DSP:
      mips_opts.ase_dsp = 0;
      mips_opts.ase_dspr2 = 0;
      break;

    case OPTION_DSPR2:
      mips_opts.ase_dspr2 = 1;
      mips_opts.ase_dsp = 1;
      break;

    case OPTION_NO_DSPR2:
      mips_opts.ase_dspr2 = 0;
      mips_opts.ase_dsp = 0;
      break;

    case OPTION_MT:
      mips_opts.ase_mt = 1;
      break;

    case OPTION_NO_MT:
      mips_opts.ase_mt = 0;
      break;

    case OPTION_MIPS16:
      mips_opts.mips16 = 1;
      mips_no_prev_insn ();
      break;

    case OPTION_NO_MIPS16:
      mips_opts.mips16 = 0;
      mips_no_prev_insn ();
      break;

    case OPTION_MIPS3D:
      mips_opts.ase_mips3d = 1;
      break;

    case OPTION_NO_MIPS3D:
      mips_opts.ase_mips3d = 0;
      break;

    case OPTION_SMARTMIPS:
      mips_opts.ase_smartmips = 1;
      break;

    case OPTION_NO_SMARTMIPS:
      mips_opts.ase_smartmips = 0;
      break;

    case OPTION_FIX_VR4120:
      mips_fix_vr4120 = 1;
      break;

    case OPTION_NO_FIX_VR4120:
      mips_fix_vr4120 = 0;
      break;

    case OPTION_FIX_VR4130:
      mips_fix_vr4130 = 1;
      break;

    case OPTION_NO_FIX_VR4130:
      mips_fix_vr4130 = 0;
      break;

    case OPTION_RELAX_BRANCH:
      mips_relax_branch = 1;
      break;

    case OPTION_NO_RELAX_BRANCH:
      mips_relax_branch = 0;
      break;

    case OPTION_MSHARED:
      mips_in_shared = TRUE;
      break;

    case OPTION_MNO_SHARED:
      mips_in_shared = FALSE;
      break;

    case OPTION_MSYM32:
      mips_opts.sym32 = TRUE;
      break;

    case OPTION_MNO_SYM32:
      mips_opts.sym32 = FALSE;
      break;

#ifdef OBJ_ELF
      /* When generating ELF code, we permit -KPIC and -call_shared to
	 select SVR4_PIC, and -non_shared to select no PIC.  This is
	 intended to be compatible with Irix 5.  */
    case OPTION_CALL_SHARED:
      if (!IS_ELF)
	{
	  as_bad (_("-call_shared is supported only for ELF format"));
	  return 0;
	}
      mips_pic = SVR4_PIC;
      mips_abicalls = TRUE;
      break;

    case OPTION_NON_SHARED:
      if (!IS_ELF)
	{
	  as_bad (_("-non_shared is supported only for ELF format"));
	  return 0;
	}
      mips_pic = NO_PIC;
      mips_abicalls = FALSE;
      break;

      /* The -xgot option tells the assembler to use 32 bit offsets
         when accessing the got in SVR4_PIC mode.  It is for Irix
         compatibility.  */
    case OPTION_XGOT:
      mips_big_got = 1;
      break;
#endif /* OBJ_ELF */

    case 'G':
      g_switch_value = atoi (arg);
      g_switch_seen = 1;
      break;

#ifdef OBJ_ELF
      /* The -32, -n32 and -64 options are shortcuts for -mabi=32, -mabi=n32
	 and -mabi=64.  */
    case OPTION_32:
      if (!IS_ELF)
	{
	  as_bad (_("-32 is supported for ELF format only"));
	  return 0;
	}
      mips_abi = O32_ABI;
      break;

    case OPTION_N32:
      if (!IS_ELF)
	{
	  as_bad (_("-n32 is supported for ELF format only"));
	  return 0;
	}
      mips_abi = N32_ABI;
      break;

    case OPTION_64:
      if (!IS_ELF)
	{
	  as_bad (_("-64 is supported for ELF format only"));
	  return 0;
	}
      mips_abi = N64_ABI;
      if (!support_64bit_objects())
	as_fatal (_("No compiled in support for 64 bit object file format"));
      break;
#endif /* OBJ_ELF */

    case OPTION_GP32:
      file_mips_gp32 = 1;
      break;

    case OPTION_GP64:
      file_mips_gp32 = 0;
      break;

    case OPTION_FP32:
      file_mips_fp32 = 1;
      break;

    case OPTION_FP64:
      file_mips_fp32 = 0;
      break;

#ifdef OBJ_ELF
    case OPTION_MABI:
      if (!IS_ELF)
	{
	  as_bad (_("-mabi is supported for ELF format only"));
	  return 0;
	}
      if (strcmp (arg, "32") == 0)
	mips_abi = O32_ABI;
      else if (strcmp (arg, "o64") == 0)
	mips_abi = O64_ABI;
      else if (strcmp (arg, "n32") == 0)
	mips_abi = N32_ABI;
      else if (strcmp (arg, "64") == 0)
	{
	  mips_abi = N64_ABI;
	  if (! support_64bit_objects())
	    as_fatal (_("No compiled in support for 64 bit object file "
			"format"));
	}
      else if (strcmp (arg, "eabi") == 0)
	mips_abi = EABI_ABI;
      else
	{
	  as_fatal (_("invalid abi -mabi=%s"), arg);
	  return 0;
	}
      break;
#endif /* OBJ_ELF */

    case OPTION_M7000_HILO_FIX:
      mips_7000_hilo_fix = TRUE;
      break;

    case OPTION_MNO_7000_HILO_FIX:
      mips_7000_hilo_fix = FALSE;
      break;

#ifdef OBJ_ELF
    case OPTION_MDEBUG:
      mips_flag_mdebug = TRUE;
      break;

    case OPTION_NO_MDEBUG:
      mips_flag_mdebug = FALSE;
      break;

    case OPTION_PDR:
      mips_flag_pdr = TRUE;
      break;

    case OPTION_NO_PDR:
      mips_flag_pdr = FALSE;
      break;

    case OPTION_MVXWORKS_PIC:
      mips_pic = VXWORKS_PIC;
      break;
#endif /* OBJ_ELF */

    default:
      return 0;
    }

  return 1;
}

/* Set up globals to generate code for the ISA or processor
   described by INFO.  */

static void
mips_set_architecture (const struct mips_cpu_info *info)
{
  if (info != 0)
    {
      file_mips_arch = info->cpu;
      mips_opts.arch = info->cpu;
      mips_opts.isa = info->isa;
    }
}


/* Likewise for tuning.  */

static void
mips_set_tune (const struct mips_cpu_info *info)
{
  if (info != 0)
    mips_tune = info->cpu;
}


void
mips_after_parse_args (void)
{
  const struct mips_cpu_info *arch_info = 0;
  const struct mips_cpu_info *tune_info = 0;

  /* GP relative stuff not working for PE */
  if (strncmp (TARGET_OS, "pe", 2) == 0)
    {
      if (g_switch_seen && g_switch_value != 0)
	as_bad (_("-G not supported in this configuration."));
      g_switch_value = 0;
    }

  if (mips_abi == NO_ABI)
    mips_abi = MIPS_DEFAULT_ABI;

  /* The following code determines the architecture and register size.
     Similar code was added to GCC 3.3 (see override_options() in
     config/mips/mips.c).  The GAS and GCC code should be kept in sync
     as much as possible.  */

  if (mips_arch_string != 0)
    arch_info = mips_parse_cpu ("-march", mips_arch_string);

  if (file_mips_isa != ISA_UNKNOWN)
    {
      /* Handle -mipsN.  At this point, file_mips_isa contains the
	 ISA level specified by -mipsN, while arch_info->isa contains
	 the -march selection (if any).  */
      if (arch_info != 0)
	{
	  /* -march takes precedence over -mipsN, since it is more descriptive.
	     There's no harm in specifying both as long as the ISA levels
	     are the same.  */
	  if (file_mips_isa != arch_info->isa)
	    as_bad (_("-%s conflicts with the other architecture options, which imply -%s"),
		    mips_cpu_info_from_isa (file_mips_isa)->name,
		    mips_cpu_info_from_isa (arch_info->isa)->name);
	}
      else
	arch_info = mips_cpu_info_from_isa (file_mips_isa);
    }

  if (arch_info == 0)
    arch_info = mips_parse_cpu ("default CPU", MIPS_CPU_STRING_DEFAULT);

  if (ABI_NEEDS_64BIT_REGS (mips_abi) && !ISA_HAS_64BIT_REGS (arch_info->isa))
    as_bad ("-march=%s is not compatible with the selected ABI",
	    arch_info->name);

  mips_set_architecture (arch_info);

  /* Optimize for file_mips_arch, unless -mtune selects a different processor.  */
  if (mips_tune_string != 0)
    tune_info = mips_parse_cpu ("-mtune", mips_tune_string);

  if (tune_info == 0)
    mips_set_tune (arch_info);
  else
    mips_set_tune (tune_info);

  if (file_mips_gp32 >= 0)
    {
      /* The user specified the size of the integer registers.  Make sure
	 it agrees with the ABI and ISA.  */
      if (file_mips_gp32 == 0 && !ISA_HAS_64BIT_REGS (mips_opts.isa))
	as_bad (_("-mgp64 used with a 32-bit processor"));
      else if (file_mips_gp32 == 1 && ABI_NEEDS_64BIT_REGS (mips_abi))
	as_bad (_("-mgp32 used with a 64-bit ABI"));
      else if (file_mips_gp32 == 0 && ABI_NEEDS_32BIT_REGS (mips_abi))
	as_bad (_("-mgp64 used with a 32-bit ABI"));
    }
  else
    {
      /* Infer the integer register size from the ABI and processor.
	 Restrict ourselves to 32-bit registers if that's all the
	 processor has, or if the ABI cannot handle 64-bit registers.  */
      file_mips_gp32 = (ABI_NEEDS_32BIT_REGS (mips_abi)
			|| !ISA_HAS_64BIT_REGS (mips_opts.isa));
    }

  switch (file_mips_fp32)
    {
    default:
    case -1:
      /* No user specified float register size.
	 ??? GAS treats single-float processors as though they had 64-bit
	 float registers (although it complains when double-precision
	 instructions are used).  As things stand, saying they have 32-bit
	 registers would lead to spurious "register must be even" messages.
	 So here we assume float registers are never smaller than the
	 integer ones.  */
      if (file_mips_gp32 == 0)
	/* 64-bit integer registers implies 64-bit float registers.  */
	file_mips_fp32 = 0;
      else if ((mips_opts.ase_mips3d > 0 || mips_opts.ase_mdmx > 0)
	       && ISA_HAS_64BIT_FPRS (mips_opts.isa))
	/* -mips3d and -mdmx imply 64-bit float registers, if possible.  */
	file_mips_fp32 = 0;
      else
	/* 32-bit float registers.  */
	file_mips_fp32 = 1;
      break;

    /* The user specified the size of the float registers.  Check if it
       agrees with the ABI and ISA.  */
    case 0:
      if (!ISA_HAS_64BIT_FPRS (mips_opts.isa))
	as_bad (_("-mfp64 used with a 32-bit fpu"));
      else if (ABI_NEEDS_32BIT_REGS (mips_abi)
	       && !ISA_HAS_MXHC1 (mips_opts.isa))
	as_warn (_("-mfp64 used with a 32-bit ABI"));
      break;
    case 1:
      if (ABI_NEEDS_64BIT_REGS (mips_abi))
	as_warn (_("-mfp32 used with a 64-bit ABI"));
      break;
    }

  /* End of GCC-shared inference code.  */

  /* This flag is set when we have a 64-bit capable CPU but use only
     32-bit wide registers.  Note that EABI does not use it.  */
  if (ISA_HAS_64BIT_REGS (mips_opts.isa)
      && ((mips_abi == NO_ABI && file_mips_gp32 == 1)
	  || mips_abi == O32_ABI))
    mips_32bitmode = 1;

  if (mips_opts.isa == ISA_MIPS1 && mips_trap)
    as_bad (_("trap exception not supported at ISA 1"));

  /* If the selected architecture includes support for ASEs, enable
     generation of code for them.  */
  if (mips_opts.mips16 == -1)
    mips_opts.mips16 = (CPU_HAS_MIPS16 (file_mips_arch)) ? 1 : 0;
  if (mips_opts.ase_mips3d == -1)
    mips_opts.ase_mips3d = ((arch_info->flags & MIPS_CPU_ASE_MIPS3D)
			    && file_mips_fp32 == 0) ? 1 : 0;
  if (mips_opts.ase_mips3d && file_mips_fp32 == 1)
    as_bad (_("-mfp32 used with -mips3d"));

  if (mips_opts.ase_mdmx == -1)
    mips_opts.ase_mdmx = ((arch_info->flags & MIPS_CPU_ASE_MDMX)
			  && file_mips_fp32 == 0) ? 1 : 0;
  if (mips_opts.ase_mdmx && file_mips_fp32 == 1)
    as_bad (_("-mfp32 used with -mdmx"));

  if (mips_opts.ase_smartmips == -1)
    mips_opts.ase_smartmips = (arch_info->flags & MIPS_CPU_ASE_SMARTMIPS) ? 1 : 0;
  if (mips_opts.ase_smartmips && !ISA_SUPPORTS_SMARTMIPS)
      as_warn ("%s ISA does not support SmartMIPS", 
	       mips_cpu_info_from_isa (mips_opts.isa)->name);

  if (mips_opts.ase_dsp == -1)
    mips_opts.ase_dsp = (arch_info->flags & MIPS_CPU_ASE_DSP) ? 1 : 0;
  if (mips_opts.ase_dsp && !ISA_SUPPORTS_DSP_ASE)
      as_warn ("%s ISA does not support DSP ASE", 
	       mips_cpu_info_from_isa (mips_opts.isa)->name);

  if (mips_opts.ase_dspr2 == -1)
    {
      mips_opts.ase_dspr2 = (arch_info->flags & MIPS_CPU_ASE_DSPR2) ? 1 : 0;
      mips_opts.ase_dsp = (arch_info->flags & MIPS_CPU_ASE_DSP) ? 1 : 0;
    }
  if (mips_opts.ase_dspr2 && !ISA_SUPPORTS_DSPR2_ASE)
      as_warn ("%s ISA does not support DSP R2 ASE",
	       mips_cpu_info_from_isa (mips_opts.isa)->name);

  if (mips_opts.ase_mt == -1)
    mips_opts.ase_mt = (arch_info->flags & MIPS_CPU_ASE_MT) ? 1 : 0;
  if (mips_opts.ase_mt && !ISA_SUPPORTS_MT_ASE)
      as_warn ("%s ISA does not support MT ASE",
	       mips_cpu_info_from_isa (mips_opts.isa)->name);

  file_mips_isa = mips_opts.isa;
  file_ase_mips16 = mips_opts.mips16;
  file_ase_mips3d = mips_opts.ase_mips3d;
  file_ase_mdmx = mips_opts.ase_mdmx;
  file_ase_smartmips = mips_opts.ase_smartmips;
  file_ase_dsp = mips_opts.ase_dsp;
  file_ase_dspr2 = mips_opts.ase_dspr2;
  file_ase_mt = mips_opts.ase_mt;
  mips_opts.gp32 = file_mips_gp32;
  mips_opts.fp32 = file_mips_fp32;

  if (mips_flag_mdebug < 0)
    {
#ifdef OBJ_MAYBE_ECOFF
      if (OUTPUT_FLAVOR == bfd_target_ecoff_flavour)
	mips_flag_mdebug = 1;
      else
#endif /* OBJ_MAYBE_ECOFF */
	mips_flag_mdebug = 0;
    }
}

void
mips_init_after_args (void)
{
  /* initialize opcodes */
  bfd_mips_num_opcodes = bfd_mips_num_builtin_opcodes;
  mips_opcodes = (struct mips_opcode *) mips_builtin_opcodes;
}

long
md_pcrel_from (fixS *fixP)
{
  valueT addr = fixP->fx_where + fixP->fx_frag->fr_address;
  switch (fixP->fx_r_type)
    {
    case BFD_RELOC_16_PCREL_S2:
    case BFD_RELOC_MIPS_JMP:
      /* Return the address of the delay slot.  */
      return addr + 4;
    default:
      /* We have no relocation type for PC relative MIPS16 instructions.  */
      if (fixP->fx_addsy && S_GET_SEGMENT (fixP->fx_addsy) != now_seg)
	as_bad_where (fixP->fx_file, fixP->fx_line,
		      _("PC relative MIPS16 instruction references a different section"));
      return addr;
    }
}

/* This is called before the symbol table is processed.  In order to
   work with gcc when using mips-tfile, we must keep all local labels.
   However, in other cases, we want to discard them.  If we were
   called with -g, but we didn't see any debugging information, it may
   mean that gcc is smuggling debugging information through to
   mips-tfile, in which case we must generate all local labels.  */

void
mips_frob_file_before_adjust (void)
{
#ifndef NO_ECOFF_DEBUGGING
  if (ECOFF_DEBUGGING
      && mips_debug != 0
      && ! ecoff_debugging_seen)
    flag_keep_locals = 1;
#endif
}

/* Sort any unmatched HI16 and GOT16 relocs so that they immediately precede
   the corresponding LO16 reloc.  This is called before md_apply_fix and
   tc_gen_reloc.  Unmatched relocs can only be generated by use of explicit
   relocation operators.

   For our purposes, a %lo() expression matches a %got() or %hi()
   expression if:

      (a) it refers to the same symbol; and
      (b) the offset applied in the %lo() expression is no lower than
	  the offset applied in the %got() or %hi().

   (b) allows us to cope with code like:

	lui	$4,%hi(foo)
	lh	$4,%lo(foo+2)($4)

   ...which is legal on RELA targets, and has a well-defined behaviour
   if the user knows that adding 2 to "foo" will not induce a carry to
   the high 16 bits.

   When several %lo()s match a particular %got() or %hi(), we use the
   following rules to distinguish them:

     (1) %lo()s with smaller offsets are a better match than %lo()s with
         higher offsets.

     (2) %lo()s with no matching %got() or %hi() are better than those
         that already have a matching %got() or %hi().

     (3) later %lo()s are better than earlier %lo()s.

   These rules are applied in order.

   (1) means, among other things, that %lo()s with identical offsets are
   chosen if they exist.

   (2) means that we won't associate several high-part relocations with
   the same low-part relocation unless there's no alternative.  Having
   several high parts for the same low part is a GNU extension; this rule
   allows careful users to avoid it.

   (3) is purely cosmetic.  mips_hi_fixup_list is is in reverse order,
   with the last high-part relocation being at the front of the list.
   It therefore makes sense to choose the last matching low-part
   relocation, all other things being equal.  It's also easier
   to code that way.  */

void
mips_frob_file (void)
{
  struct mips_hi_fixup *l;

  for (l = mips_hi_fixup_list; l != NULL; l = l->next)
    {
      segment_info_type *seginfo;
      bfd_boolean matched_lo_p;
      fixS **hi_pos, **lo_pos, **pos;

      assert (reloc_needs_lo_p (l->fixp->fx_r_type));

      /* If a GOT16 relocation turns out to be against a global symbol,
	 there isn't supposed to be a matching LO.  */
      if (l->fixp->fx_r_type == BFD_RELOC_MIPS_GOT16
	  && !pic_need_relax (l->fixp->fx_addsy, l->seg))
	continue;

      /* Check quickly whether the next fixup happens to be a matching %lo.  */
      if (fixup_has_matching_lo_p (l->fixp))
	continue;

      seginfo = seg_info (l->seg);

      /* Set HI_POS to the position of this relocation in the chain.
	 Set LO_POS to the position of the chosen low-part relocation.
	 MATCHED_LO_P is true on entry to the loop if *POS is a low-part
	 relocation that matches an immediately-preceding high-part
	 relocation.  */
      hi_pos = NULL;
      lo_pos = NULL;
      matched_lo_p = FALSE;
      for (pos = &seginfo->fix_root; *pos != NULL; pos = &(*pos)->fx_next)
	{
	  if (*pos == l->fixp)
	    hi_pos = pos;

	  if (((*pos)->fx_r_type == BFD_RELOC_LO16
	       || (*pos)->fx_r_type == BFD_RELOC_MIPS16_LO16)
	      && (*pos)->fx_addsy == l->fixp->fx_addsy
	      && (*pos)->fx_offset >= l->fixp->fx_offset
	      && (lo_pos == NULL
		  || (*pos)->fx_offset < (*lo_pos)->fx_offset
		  || (!matched_lo_p
		      && (*pos)->fx_offset == (*lo_pos)->fx_offset)))
	    lo_pos = pos;

	  matched_lo_p = (reloc_needs_lo_p ((*pos)->fx_r_type)
			  && fixup_has_matching_lo_p (*pos));
	}

      /* If we found a match, remove the high-part relocation from its
	 current position and insert it before the low-part relocation.
	 Make the offsets match so that fixup_has_matching_lo_p()
	 will return true.

	 We don't warn about unmatched high-part relocations since some
	 versions of gcc have been known to emit dead "lui ...%hi(...)"
	 instructions.  */
      if (lo_pos != NULL)
	{
	  l->fixp->fx_offset = (*lo_pos)->fx_offset;
	  if (l->fixp->fx_next != *lo_pos)
	    {
	      *hi_pos = l->fixp->fx_next;
	      l->fixp->fx_next = *lo_pos;
	      *lo_pos = l->fixp;
	    }
	}
    }
}

/* We may have combined relocations without symbols in the N32/N64 ABI.
   We have to prevent gas from dropping them.  */

int
mips_force_relocation (fixS *fixp)
{
  if (generic_force_reloc (fixp))
    return 1;

  if (HAVE_NEWABI
      && S_GET_SEGMENT (fixp->fx_addsy) == bfd_abs_section_ptr
      && (fixp->fx_r_type == BFD_RELOC_MIPS_SUB
	  || fixp->fx_r_type == BFD_RELOC_HI16_S
	  || fixp->fx_r_type == BFD_RELOC_LO16))
    return 1;

  return 0;
}

/* Apply a fixup to the object file.  */

void
md_apply_fix (fixS *fixP, valueT *valP, segT seg ATTRIBUTE_UNUSED)
{
  bfd_byte *buf;
  long insn;
  reloc_howto_type *howto;

  /* We ignore generic BFD relocations we don't know about.  */
  howto = bfd_reloc_type_lookup (stdoutput, fixP->fx_r_type);
  if (! howto)
    return;

  assert (fixP->fx_size == 4
	  || fixP->fx_r_type == BFD_RELOC_16
	  || fixP->fx_r_type == BFD_RELOC_64
	  || fixP->fx_r_type == BFD_RELOC_CTOR
	  || fixP->fx_r_type == BFD_RELOC_MIPS_SUB
	  || fixP->fx_r_type == BFD_RELOC_VTABLE_INHERIT
	  || fixP->fx_r_type == BFD_RELOC_VTABLE_ENTRY
	  || fixP->fx_r_type == BFD_RELOC_MIPS_TLS_DTPREL64);

  buf = (bfd_byte *) (fixP->fx_frag->fr_literal + fixP->fx_where);

  assert (!fixP->fx_pcrel || fixP->fx_r_type == BFD_RELOC_16_PCREL_S2);

  /* Don't treat parts of a composite relocation as done.  There are two
     reasons for this:

     (1) The second and third parts will be against 0 (RSS_UNDEF) but
	 should nevertheless be emitted if the first part is.

     (2) In normal usage, composite relocations are never assembly-time
	 constants.  The easiest way of dealing with the pathological
	 exceptions is to generate a relocation against STN_UNDEF and
	 leave everything up to the linker.  */
  if (fixP->fx_addsy == NULL && !fixP->fx_pcrel && fixP->fx_tcbit == 0)
    fixP->fx_done = 1;

  switch (fixP->fx_r_type)
    {
    case BFD_RELOC_MIPS_TLS_GD:
    case BFD_RELOC_MIPS_TLS_LDM:
    case BFD_RELOC_MIPS_TLS_DTPREL32:
    case BFD_RELOC_MIPS_TLS_DTPREL64:
    case BFD_RELOC_MIPS_TLS_DTPREL_HI16:
    case BFD_RELOC_MIPS_TLS_DTPREL_LO16:
    case BFD_RELOC_MIPS_TLS_GOTTPREL:
    case BFD_RELOC_MIPS_TLS_TPREL_HI16:
    case BFD_RELOC_MIPS_TLS_TPREL_LO16:
      S_SET_THREAD_LOCAL (fixP->fx_addsy);
      /* fall through */

    case BFD_RELOC_MIPS_JMP:
    case BFD_RELOC_MIPS_SHIFT5:
    case BFD_RELOC_MIPS_SHIFT6:
    case BFD_RELOC_MIPS_GOT_DISP:
    case BFD_RELOC_MIPS_GOT_PAGE:
    case BFD_RELOC_MIPS_GOT_OFST:
    case BFD_RELOC_MIPS_SUB:
    case BFD_RELOC_MIPS_INSERT_A:
    case BFD_RELOC_MIPS_INSERT_B:
    case BFD_RELOC_MIPS_DELETE:
    case BFD_RELOC_MIPS_HIGHEST:
    case BFD_RELOC_MIPS_HIGHER:
    case BFD_RELOC_MIPS_SCN_DISP:
    case BFD_RELOC_MIPS_REL16:
    case BFD_RELOC_MIPS_RELGOT:
    case BFD_RELOC_MIPS_JALR:
    case BFD_RELOC_HI16:
    case BFD_RELOC_HI16_S:
    case BFD_RELOC_GPREL16:
    case BFD_RELOC_MIPS_LITERAL:
    case BFD_RELOC_MIPS_CALL16:
    case BFD_RELOC_MIPS_GOT16:
    case BFD_RELOC_GPREL32:
    case BFD_RELOC_MIPS_GOT_HI16:
    case BFD_RELOC_MIPS_GOT_LO16:
    case BFD_RELOC_MIPS_CALL_HI16:
    case BFD_RELOC_MIPS_CALL_LO16:
    case BFD_RELOC_MIPS16_GPREL:
    case BFD_RELOC_MIPS16_HI16:
    case BFD_RELOC_MIPS16_HI16_S:
    case BFD_RELOC_MIPS16_JMP:
      /* Nothing needed to do.  The value comes from the reloc entry.  */
      break;

    case BFD_RELOC_64:
      /* This is handled like BFD_RELOC_32, but we output a sign
         extended value if we are only 32 bits.  */
      if (fixP->fx_done)
	{
	  if (8 <= sizeof (valueT))
	    md_number_to_chars ((char *) buf, *valP, 8);
	  else
	    {
	      valueT hiv;

	      if ((*valP & 0x80000000) != 0)
		hiv = 0xffffffff;
	      else
		hiv = 0;
	      md_number_to_chars ((char *)(buf + (target_big_endian ? 4 : 0)),
				  *valP, 4);
	      md_number_to_chars ((char *)(buf + (target_big_endian ? 0 : 4)),
				  hiv, 4);
	    }
	}
      break;

    case BFD_RELOC_RVA:
    case BFD_RELOC_32:
    case BFD_RELOC_16:
      /* If we are deleting this reloc entry, we must fill in the
	 value now.  This can happen if we have a .word which is not
	 resolved when it appears but is later defined.  */
      if (fixP->fx_done)
	md_number_to_chars ((char *) buf, *valP, fixP->fx_size);
      break;

    case BFD_RELOC_LO16:
    case BFD_RELOC_MIPS16_LO16:
      /* FIXME: Now that embedded-PIC is gone, some of this code/comment
	 may be safe to remove, but if so it's not obvious.  */
      /* When handling an embedded PIC switch statement, we can wind
	 up deleting a LO16 reloc.  See the 'o' case in mips_ip.  */
      if (fixP->fx_done)
	{
	  if (*valP + 0x8000 > 0xffff)
	    as_bad_where (fixP->fx_file, fixP->fx_line,
			  _("relocation overflow"));
	  if (target_big_endian)
	    buf += 2;
	  md_number_to_chars ((char *) buf, *valP, 2);
	}
      break;

    case BFD_RELOC_16_PCREL_S2:
      if ((*valP & 0x3) != 0)
	as_bad_where (fixP->fx_file, fixP->fx_line,
		      _("Branch to misaligned address (%lx)"), (long) *valP);

      /* We need to save the bits in the instruction since fixup_segment()
	 might be deleting the relocation entry (i.e., a branch within
	 the current segment).  */
      if (! fixP->fx_done)
	break;

      /* Update old instruction data.  */
      if (target_big_endian)
	insn = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
      else
	insn = (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | buf[0];

      if (*valP + 0x20000 <= 0x3ffff)
	{
	  insn |= (*valP >> 2) & 0xffff;
	  md_number_to_chars ((char *) buf, insn, 4);
	}
      else if (mips_pic == NO_PIC
	       && fixP->fx_done
	       && fixP->fx_frag->fr_address >= text_section->vma
	       && (fixP->fx_frag->fr_address
		   < text_section->vma + bfd_get_section_size (text_section))
	       && ((insn & 0xffff0000) == 0x10000000	 /* beq $0,$0 */
		   || (insn & 0xffff0000) == 0x04010000	 /* bgez $0 */
		   || (insn & 0xffff0000) == 0x04110000)) /* bgezal $0 */
	{
	  /* The branch offset is too large.  If this is an
             unconditional branch, and we are not generating PIC code,
             we can convert it to an absolute jump instruction.  */
	  if ((insn & 0xffff0000) == 0x04110000)	 /* bgezal $0 */
	    insn = 0x0c000000;	/* jal */
	  else
	    insn = 0x08000000;	/* j */
	  fixP->fx_r_type = BFD_RELOC_MIPS_JMP;
	  fixP->fx_done = 0;
	  fixP->fx_addsy = section_symbol (text_section);
	  *valP += md_pcrel_from (fixP);
	  md_number_to_chars ((char *) buf, insn, 4);
	}
      else
	{
	  /* If we got here, we have branch-relaxation disabled,
	     and there's nothing we can do to fix this instruction
	     without turning it into a longer sequence.  */
	  as_bad_where (fixP->fx_file, fixP->fx_line,
			_("Branch out of range"));
	}
      break;

    case BFD_RELOC_VTABLE_INHERIT:
      fixP->fx_done = 0;
      if (fixP->fx_addsy
          && !S_IS_DEFINED (fixP->fx_addsy)
          && !S_IS_WEAK (fixP->fx_addsy))
        S_SET_WEAK (fixP->fx_addsy);
      break;

    case BFD_RELOC_VTABLE_ENTRY:
      fixP->fx_done = 0;
      break;

    default:
      internalError ();
    }

  /* Remember value for tc_gen_reloc.  */
  fixP->fx_addnumber = *valP;
}

static symbolS *
get_symbol (void)
{
  int c;
  char *name;
  symbolS *p;

  name = input_line_pointer;
  c = get_symbol_end ();
  p = (symbolS *) symbol_find_or_make (name);
  *input_line_pointer = c;
  return p;
}

/* Align the current frag to a given power of two.  The MIPS assembler
   also automatically adjusts any preceding label.  */

static void
mips_align (int to, int fill, symbolS *label)
{
  mips_emit_delays ();
  frag_align (to, fill, 0);
  record_alignment (now_seg, to);
  if (label != NULL)
    {
      assert (S_GET_SEGMENT (label) == now_seg);
      symbol_set_frag (label, frag_now);
      S_SET_VALUE (label, (valueT) frag_now_fix ());
    }
}

/* Align to a given power of two.  .align 0 turns off the automatic
   alignment used by the data creating pseudo-ops.  */

static void
s_align (int x ATTRIBUTE_UNUSED)
{
  int temp;
  long temp_fill;
  long max_alignment = 15;

  /* o Note that the assembler pulls down any immediately preceding label
       to the aligned address.
     o It's not documented but auto alignment is reinstated by
       a .align pseudo instruction.
     o Note also that after auto alignment is turned off the mips assembler
       issues an error on attempt to assemble an improperly aligned data item.
       We don't.  */

  temp = get_absolute_expression ();
  if (temp > max_alignment)
    as_bad (_("Alignment too large: %d. assumed."), temp = max_alignment);
  else if (temp < 0)
    {
      as_warn (_("Alignment negative: 0 assumed."));
      temp = 0;
    }
  if (*input_line_pointer == ',')
    {
      ++input_line_pointer;
      temp_fill = get_absolute_expression ();
    }
  else
    temp_fill = 0;
  if (temp)
    {
      segment_info_type *si = seg_info (now_seg);
      struct insn_label_list *l = si->label_list;
      /* Auto alignment should be switched on by next section change.  */
      auto_align = 1;
      mips_align (temp, (int) temp_fill, l != NULL ? l->label : NULL);
    }
  else
    {
      auto_align = 0;
    }

  demand_empty_rest_of_line ();
}

static void
s_change_sec (int sec)
{
  segT seg;

#ifdef OBJ_ELF
  /* The ELF backend needs to know that we are changing sections, so
     that .previous works correctly.  We could do something like check
     for an obj_section_change_hook macro, but that might be confusing
     as it would not be appropriate to use it in the section changing
     functions in read.c, since obj-elf.c intercepts those.  FIXME:
     This should be cleaner, somehow.  */
  if (IS_ELF)
    obj_elf_section_change_hook ();
#endif

  mips_emit_delays ();
  switch (sec)
    {
    case 't':
      s_text (0);
      break;
    case 'd':
      s_data (0);
      break;
    case 'b':
      subseg_set (bss_section, (subsegT) get_absolute_expression ());
      demand_empty_rest_of_line ();
      break;

    case 'r':
      seg = subseg_new (RDATA_SECTION_NAME,
			(subsegT) get_absolute_expression ());
      if (IS_ELF)
	{
	  bfd_set_section_flags (stdoutput, seg, (SEC_ALLOC | SEC_LOAD
						  | SEC_READONLY | SEC_RELOC
						  | SEC_DATA));
	  if (strcmp (TARGET_OS, "elf") != 0)
	    record_alignment (seg, 4);
	}
      demand_empty_rest_of_line ();
      break;

    case 's':
      seg = subseg_new (".sdata", (subsegT) get_absolute_expression ());
      if (IS_ELF)
	{
	  bfd_set_section_flags (stdoutput, seg,
				 SEC_ALLOC | SEC_LOAD | SEC_RELOC | SEC_DATA);
	  if (strcmp (TARGET_OS, "elf") != 0)
	    record_alignment (seg, 4);
	}
      demand_empty_rest_of_line ();
      break;
    }

  auto_align = 1;
}

void
s_change_section (int ignore ATTRIBUTE_UNUSED)
{
#ifdef OBJ_ELF
  char *section_name;
  char c;
  char next_c = 0;
  int section_type;
  int section_flag;
  int section_entry_size;
  int section_alignment;

  if (!IS_ELF)
    return;

  section_name = input_line_pointer;
  c = get_symbol_end ();
  if (c)
    next_c = *(input_line_pointer + 1);

  /* Do we have .section Name<,"flags">?  */
  if (c != ',' || (c == ',' && next_c == '"'))
    {
      /* just after name is now '\0'.  */
      *input_line_pointer = c;
      input_line_pointer = section_name;
      obj_elf_section (ignore);
      return;
    }
  input_line_pointer++;

  /* Do we have .section Name<,type><,flag><,entry_size><,alignment>  */
  if (c == ',')
    section_type = get_absolute_expression ();
  else
    section_type = 0;
  if (*input_line_pointer++ == ',')
    section_flag = get_absolute_expression ();
  else
    section_flag = 0;
  if (*input_line_pointer++ == ',')
    section_entry_size = get_absolute_expression ();
  else
    section_entry_size = 0;
  if (*input_line_pointer++ == ',')
    section_alignment = get_absolute_expression ();
  else
    section_alignment = 0;

  section_name = xstrdup (section_name);

  /* When using the generic form of .section (as implemented by obj-elf.c),
     there's no way to set the section type to SHT_MIPS_DWARF.  Users have
     traditionally had to fall back on the more common @progbits instead.

     There's nothing really harmful in this, since bfd will correct
     SHT_PROGBITS to SHT_MIPS_DWARF before writing out the file.  But it
     means that, for backwards compatibility, the special_section entries
     for dwarf sections must use SHT_PROGBITS rather than SHT_MIPS_DWARF.

     Even so, we shouldn't force users of the MIPS .section syntax to
     incorrectly label the sections as SHT_PROGBITS.  The best compromise
     seems to be to map SHT_MIPS_DWARF to SHT_PROGBITS before calling the
     generic type-checking code.  */
  if (section_type == SHT_MIPS_DWARF)
    section_type = SHT_PROGBITS;

  obj_elf_change_section (section_name, section_type, section_flag,
			  section_entry_size, 0, 0, 0);

  if (now_seg->name != section_name)
    free (section_name);
#endif /* OBJ_ELF */
}

void
mips_enable_auto_align (void)
{
  auto_align = 1;
}

static void
s_cons (int log_size)
{
  segment_info_type *si = seg_info (now_seg);
  struct insn_label_list *l = si->label_list;
  symbolS *label;

  label = l != NULL ? l->label : NULL;
  mips_emit_delays ();
  if (log_size > 0 && auto_align)
    mips_align (log_size, 0, label);
  mips_clear_insn_labels ();
  cons (1 << log_size);
}

static void
s_float_cons (int type)
{
  segment_info_type *si = seg_info (now_seg);
  struct insn_label_list *l = si->label_list;
  symbolS *label;

  label = l != NULL ? l->label : NULL;

  mips_emit_delays ();

  if (auto_align)
    {
      if (type == 'd')
	mips_align (3, 0, label);
      else
	mips_align (2, 0, label);
    }

  mips_clear_insn_labels ();

  float_cons (type);
}

/* Handle .globl.  We need to override it because on Irix 5 you are
   permitted to say
       .globl foo .text
   where foo is an undefined symbol, to mean that foo should be
   considered to be the address of a function.  */

static void
s_mips_globl (int x ATTRIBUTE_UNUSED)
{
  char *name;
  int c;
  symbolS *symbolP;
  flagword flag;

  do
    {
      name = input_line_pointer;
      c = get_symbol_end ();
      symbolP = symbol_find_or_make (name);
      S_SET_EXTERNAL (symbolP);

      *input_line_pointer = c;
      SKIP_WHITESPACE ();

      /* On Irix 5, every global symbol that is not explicitly labelled as
         being a function is apparently labelled as being an object.  */
      flag = BSF_OBJECT;

      if (!is_end_of_line[(unsigned char) *input_line_pointer]
	  && (*input_line_pointer != ','))
	{
	  char *secname;
	  asection *sec;

	  secname = input_line_pointer;
	  c = get_symbol_end ();
	  sec = bfd_get_section_by_name (stdoutput, secname);
	  if (sec == NULL)
	    as_bad (_("%s: no such section"), secname);
	  *input_line_pointer = c;

	  if (sec != NULL && (sec->flags & SEC_CODE) != 0)
	    flag = BSF_FUNCTION;
	}

      symbol_get_bfdsym (symbolP)->flags |= flag;

      c = *input_line_pointer;
      if (c == ',')
	{
	  input_line_pointer++;
	  SKIP_WHITESPACE ();
	  if (is_end_of_line[(unsigned char) *input_line_pointer])
	    c = '\n';
	}
    }
  while (c == ',');

  demand_empty_rest_of_line ();
}

static void
s_option (int x ATTRIBUTE_UNUSED)
{
  char *opt;
  char c;

  opt = input_line_pointer;
  c = get_symbol_end ();

  if (*opt == 'O')
    {
      /* FIXME: What does this mean?  */
    }
  else if (strncmp (opt, "pic", 3) == 0)
    {
      int i;

      i = atoi (opt + 3);
      if (i == 0)
	mips_pic = NO_PIC;
      else if (i == 2)
	{
	mips_pic = SVR4_PIC;
	  mips_abicalls = TRUE;
	}
      else
	as_bad (_(".option pic%d not supported"), i);

      if (mips_pic == SVR4_PIC)
	{
	  if (g_switch_seen && g_switch_value != 0)
	    as_warn (_("-G may not be used with SVR4 PIC code"));
	  g_switch_value = 0;
	  bfd_set_gp_size (stdoutput, 0);
	}
    }
  else
    as_warn (_("Unrecognized option \"%s\""), opt);

  *input_line_pointer = c;
  demand_empty_rest_of_line ();
}

/* This structure is used to hold a stack of .set values.  */

struct mips_option_stack
{
  struct mips_option_stack *next;
  struct mips_set_options options;
};

static struct mips_option_stack *mips_opts_stack;

/* Handle the .set pseudo-op.  */

static void
s_mipsset (int x ATTRIBUTE_UNUSED)
{
  char *name = input_line_pointer, ch;

  while (!is_end_of_line[(unsigned char) *input_line_pointer])
    ++input_line_pointer;
  ch = *input_line_pointer;
  *input_line_pointer = '\0';

  if (strcmp (name, "reorder") == 0)
    {
      if (mips_opts.noreorder)
	end_noreorder ();
    }
  else if (strcmp (name, "noreorder") == 0)
    {
      if (!mips_opts.noreorder)
	start_noreorder ();
    }
  else if (strcmp (name, "at") == 0)
    {
      mips_opts.noat = 0;
    }
  else if (strcmp (name, "noat") == 0)
    {
      mips_opts.noat = 1;
    }
  else if (strcmp (name, "macro") == 0)
    {
      mips_opts.warn_about_macros = 0;
    }
  else if (strcmp (name, "nomacro") == 0)
    {
      if (mips_opts.noreorder == 0)
	as_bad (_("`noreorder' must be set before `nomacro'"));
      mips_opts.warn_about_macros = 1;
    }
  else if (strcmp (name, "move") == 0 || strcmp (name, "novolatile") == 0)
    {
      mips_opts.nomove = 0;
    }
  else if (strcmp (name, "nomove") == 0 || strcmp (name, "volatile") == 0)
    {
      mips_opts.nomove = 1;
    }
  else if (strcmp (name, "bopt") == 0)
    {
      mips_opts.nobopt = 0;
    }
  else if (strcmp (name, "nobopt") == 0)
    {
      mips_opts.nobopt = 1;
    }
  else if (strcmp (name, "gp=default") == 0)
    mips_opts.gp32 = file_mips_gp32;
  else if (strcmp (name, "gp=32") == 0)
    mips_opts.gp32 = 1;
  else if (strcmp (name, "gp=64") == 0)
    {
      if (!ISA_HAS_64BIT_REGS (mips_opts.isa))
	as_warn ("%s isa does not support 64-bit registers",
		 mips_cpu_info_from_isa (mips_opts.isa)->name);
      mips_opts.gp32 = 0;
    }
  else if (strcmp (name, "fp=default") == 0)
    mips_opts.fp32 = file_mips_fp32;
  else if (strcmp (name, "fp=32") == 0)
    mips_opts.fp32 = 1;
  else if (strcmp (name, "fp=64") == 0)
    {
      if (!ISA_HAS_64BIT_FPRS (mips_opts.isa))
	as_warn ("%s isa does not support 64-bit floating point registers",
		 mips_cpu_info_from_isa (mips_opts.isa)->name);
      mips_opts.fp32 = 0;
    }
  else if (strcmp (name, "mips16") == 0
	   || strcmp (name, "MIPS-16") == 0)
    mips_opts.mips16 = 1;
  else if (strcmp (name, "nomips16") == 0
	   || strcmp (name, "noMIPS-16") == 0)
    mips_opts.mips16 = 0;
  else if (strcmp (name, "smartmips") == 0)
    {
      if (!ISA_SUPPORTS_SMARTMIPS)
	as_warn ("%s ISA does not support SmartMIPS ASE", 
		 mips_cpu_info_from_isa (mips_opts.isa)->name);
      mips_opts.ase_smartmips = 1;
    }
  else if (strcmp (name, "nosmartmips") == 0)
    mips_opts.ase_smartmips = 0;
  else if (strcmp (name, "mips3d") == 0)
    mips_opts.ase_mips3d = 1;
  else if (strcmp (name, "nomips3d") == 0)
    mips_opts.ase_mips3d = 0;
  else if (strcmp (name, "mdmx") == 0)
    mips_opts.ase_mdmx = 1;
  else if (strcmp (name, "nomdmx") == 0)
    mips_opts.ase_mdmx = 0;
  else if (strcmp (name, "dsp") == 0)
    {
      if (!ISA_SUPPORTS_DSP_ASE)
	as_warn ("%s ISA does not support DSP ASE", 
		 mips_cpu_info_from_isa (mips_opts.isa)->name);
      mips_opts.ase_dsp = 1;
      mips_opts.ase_dspr2 = 0;
    }
  else if (strcmp (name, "nodsp") == 0)
    {
      mips_opts.ase_dsp = 0;
      mips_opts.ase_dspr2 = 0;
    }
  else if (strcmp (name, "dspr2") == 0)
    {
      if (!ISA_SUPPORTS_DSPR2_ASE)
	as_warn ("%s ISA does not support DSP R2 ASE",
		 mips_cpu_info_from_isa (mips_opts.isa)->name);
      mips_opts.ase_dspr2 = 1;
      mips_opts.ase_dsp = 1;
    }
  else if (strcmp (name, "nodspr2") == 0)
    {
      mips_opts.ase_dspr2 = 0;
      mips_opts.ase_dsp = 0;
    }
  else if (strcmp (name, "mt") == 0)
    {
      if (!ISA_SUPPORTS_MT_ASE)
	as_warn ("%s ISA does not support MT ASE", 
		 mips_cpu_info_from_isa (mips_opts.isa)->name);
      mips_opts.ase_mt = 1;
    }
  else if (strcmp (name, "nomt") == 0)
    mips_opts.ase_mt = 0;
  else if (strncmp (name, "mips", 4) == 0 || strncmp (name, "arch=", 5) == 0)
    {
      int reset = 0;

      /* Permit the user to change the ISA and architecture on the fly.
	 Needless to say, misuse can cause serious problems.  */
      if (strcmp (name, "mips0") == 0 || strcmp (name, "arch=default") == 0)
	{
	  reset = 1;
	  mips_opts.isa = file_mips_isa;
	  mips_opts.arch = file_mips_arch;
	}
      else if (strncmp (name, "arch=", 5) == 0)
	{
	  const struct mips_cpu_info *p;

	  p = mips_parse_cpu("internal use", name + 5);
	  if (!p)
	    as_bad (_("unknown architecture %s"), name + 5);
	  else
	    {
	      mips_opts.arch = p->cpu;
	      mips_opts.isa = p->isa;
	    }
	}
      else if (strncmp (name, "mips", 4) == 0)
	{
	  const struct mips_cpu_info *p;

	  p = mips_parse_cpu("internal use", name);
	  if (!p)
	    as_bad (_("unknown ISA level %s"), name + 4);
	  else
	    {
	      mips_opts.arch = p->cpu;
	      mips_opts.isa = p->isa;
	    }
	}
      else
	as_bad (_("unknown ISA or architecture %s"), name);

      switch (mips_opts.isa)
	{
	case  0:
	  break;
	case ISA_MIPS1:
	case ISA_MIPS2:
	case ISA_MIPS32:
	case ISA_MIPS32R2:
	  mips_opts.gp32 = 1;
	  mips_opts.fp32 = 1;
	  break;
	case ISA_MIPS3:
	case ISA_MIPS4:
	case ISA_MIPS5:
	case ISA_MIPS64:
	case ISA_MIPS64R2:
	  mips_opts.gp32 = 0;
	  mips_opts.fp32 = 0;
	  break;
	default:
	  as_bad (_("unknown ISA level %s"), name + 4);
	  break;
	}
      if (reset)
	{
	  mips_opts.gp32 = file_mips_gp32;
	  mips_opts.fp32 = file_mips_fp32;
	}
    }
  else if (strcmp (name, "autoextend") == 0)
    mips_opts.noautoextend = 0;
  else if (strcmp (name, "noautoextend") == 0)
    mips_opts.noautoextend = 1;
  else if (strcmp (name, "push") == 0)
    {
      struct mips_option_stack *s;

      s = (struct mips_option_stack *) xmalloc (sizeof *s);
      s->next = mips_opts_stack;
      s->options = mips_opts;
      mips_opts_stack = s;
    }
  else if (strcmp (name, "pop") == 0)
    {
      struct mips_option_stack *s;

      s = mips_opts_stack;
      if (s == NULL)
	as_bad (_(".set pop with no .set push"));
      else
	{
	  /* If we're changing the reorder mode we need to handle
             delay slots correctly.  */
	  if (s->options.noreorder && ! mips_opts.noreorder)
	    start_noreorder ();
	  else if (! s->options.noreorder && mips_opts.noreorder)
	    end_noreorder ();

	  mips_opts = s->options;
	  mips_opts_stack = s->next;
	  free (s);
	}
    }
  else if (strcmp (name, "sym32") == 0)
    mips_opts.sym32 = TRUE;
  else if (strcmp (name, "nosym32") == 0)
    mips_opts.sym32 = FALSE;
  else if (strchr (name, ','))
    {
      /* Generic ".set" directive; use the generic handler.  */
      *input_line_pointer = ch;
      input_line_pointer = name;
      s_set (0);
      return;
    }
  else
    {
      as_warn (_("Tried to set unrecognized symbol: %s\n"), name);
    }
  *input_line_pointer = ch;
  demand_empty_rest_of_line ();
}

/* Handle the .abicalls pseudo-op.  I believe this is equivalent to
   .option pic2.  It means to generate SVR4 PIC calls.  */

static void
s_abicalls (int ignore ATTRIBUTE_UNUSED)
{
  mips_pic = SVR4_PIC;
  mips_abicalls = TRUE;

  if (g_switch_seen && g_switch_value != 0)
    as_warn (_("-G may not be used with SVR4 PIC code"));
  g_switch_value = 0;

  bfd_set_gp_size (stdoutput, 0);
  demand_empty_rest_of_line ();
}

/* Handle the .cpload pseudo-op.  This is used when generating SVR4
   PIC code.  It sets the $gp register for the function based on the
   function address, which is in the register named in the argument.
   This uses a relocation against _gp_disp, which is handled specially
   by the linker.  The result is:
	lui	$gp,%hi(_gp_disp)
	addiu	$gp,$gp,%lo(_gp_disp)
	addu	$gp,$gp,.cpload argument
   The .cpload argument is normally $25 == $t9.

   The -mno-shared option changes this to:
	lui	$gp,%hi(__gnu_local_gp)
	addiu	$gp,$gp,%lo(__gnu_local_gp)
   and the argument is ignored.  This saves an instruction, but the
   resulting code is not position independent; it uses an absolute
   address for __gnu_local_gp.  Thus code assembled with -mno-shared
   can go into an ordinary executable, but not into a shared library.  */

static void
s_cpload (int ignore ATTRIBUTE_UNUSED)
{
  expressionS ex;
  int reg;
  int in_shared;

  /* If we are not generating SVR4 PIC code, or if this is NewABI code,
     .cpload is ignored.  */
  if (mips_pic != SVR4_PIC || HAVE_NEWABI)
    {
      s_ignore (0);
      return;
    }

  /* .cpload should be in a .set noreorder section.  */
  if (mips_opts.noreorder == 0)
    as_warn (_(".cpload not in noreorder section"));

  reg = tc_get_register (0);

  /* If we need to produce a 64-bit address, we are better off using
     the default instruction sequence.  */
  in_shared = mips_in_shared || HAVE_64BIT_SYMBOLS;

  ex.X_op = O_symbol;
  ex.X_add_symbol = symbol_find_or_make (in_shared ? "_gp_disp" :
                                         "__gnu_local_gp");
  ex.X_op_symbol = NULL;
  ex.X_add_number = 0;

  /* In ELF, this symbol is implicitly an STT_OBJECT symbol.  */
  symbol_get_bfdsym (ex.X_add_symbol)->flags |= BSF_OBJECT;

  macro_start ();
  macro_build_lui (&ex, mips_gp_register);
  macro_build (&ex, "addiu", "t,r,j", mips_gp_register,
	       mips_gp_register, BFD_RELOC_LO16);
  if (in_shared)
    macro_build (NULL, "addu", "d,v,t", mips_gp_register,
		 mips_gp_register, reg);
  macro_end ();

  demand_empty_rest_of_line ();
}

/* Handle the .cpsetup pseudo-op defined for NewABI PIC code.  The syntax is:
     .cpsetup $reg1, offset|$reg2, label

   If offset is given, this results in:
     sd		$gp, offset($sp)
     lui	$gp, %hi(%neg(%gp_rel(label)))
     addiu	$gp, $gp, %lo(%neg(%gp_rel(label)))
     daddu	$gp, $gp, $reg1

   If $reg2 is given, this results in:
     daddu	$reg2, $gp, $0
     lui	$gp, %hi(%neg(%gp_rel(label)))
     addiu	$gp, $gp, %lo(%neg(%gp_rel(label)))
     daddu	$gp, $gp, $reg1
   $reg1 is normally $25 == $t9.

   The -mno-shared option replaces the last three instructions with
	lui	$gp,%hi(_gp)
	addiu	$gp,$gp,%lo(_gp)  */

static void
s_cpsetup (int ignore ATTRIBUTE_UNUSED)
{
  expressionS ex_off;
  expressionS ex_sym;
  int reg1;

  /* If we are not generating SVR4 PIC code, .cpsetup is ignored.
     We also need NewABI support.  */
  if (mips_pic != SVR4_PIC || ! HAVE_NEWABI)
    {
      s_ignore (0);
      return;
    }

  reg1 = tc_get_register (0);
  SKIP_WHITESPACE ();
  if (*input_line_pointer != ',')
    {
      as_bad (_("missing argument separator ',' for .cpsetup"));
      return;
    }
  else
    ++input_line_pointer;
  SKIP_WHITESPACE ();
  if (*input_line_pointer == '$')
    {
      mips_cpreturn_register = tc_get_register (0);
      mips_cpreturn_offset = -1;
    }
  else
    {
      mips_cpreturn_offset = get_absolute_expression ();
      mips_cpreturn_register = -1;
    }
  SKIP_WHITESPACE ();
  if (*input_line_pointer != ',')
    {
      as_bad (_("missing argument separator ',' for .cpsetup"));
      return;
    }
  else
    ++input_line_pointer;
  SKIP_WHITESPACE ();
  expression (&ex_sym);

  macro_start ();
  if (mips_cpreturn_register == -1)
    {
      ex_off.X_op = O_constant;
      ex_off.X_add_symbol = NULL;
      ex_off.X_op_symbol = NULL;
      ex_off.X_add_number = mips_cpreturn_offset;

      macro_build (&ex_off, "sd", "t,o(b)", mips_gp_register,
		   BFD_RELOC_LO16, SP);
    }
  else
    macro_build (NULL, "daddu", "d,v,t", mips_cpreturn_register,
		 mips_gp_register, 0);

  if (mips_in_shared || HAVE_64BIT_SYMBOLS)
    {
      macro_build (&ex_sym, "lui", "t,u", mips_gp_register,
		   -1, BFD_RELOC_GPREL16, BFD_RELOC_MIPS_SUB,
		   BFD_RELOC_HI16_S);

      macro_build (&ex_sym, "addiu", "t,r,j", mips_gp_register,
		   mips_gp_register, -1, BFD_RELOC_GPREL16,
		   BFD_RELOC_MIPS_SUB, BFD_RELOC_LO16);

      macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t", mips_gp_register,
		   mips_gp_register, reg1);
    }
  else
    {
      expressionS ex;

      ex.X_op = O_symbol;
      ex.X_add_symbol = symbol_find_or_make ("__gnu_local_gp");
      ex.X_op_symbol = NULL;
      ex.X_add_number = 0;

      /* In ELF, this symbol is implicitly an STT_OBJECT symbol.  */
      symbol_get_bfdsym (ex.X_add_symbol)->flags |= BSF_OBJECT;

      macro_build_lui (&ex, mips_gp_register);
      macro_build (&ex, "addiu", "t,r,j", mips_gp_register,
		   mips_gp_register, BFD_RELOC_LO16);
    }

  macro_end ();

  demand_empty_rest_of_line ();
}

static void
s_cplocal (int ignore ATTRIBUTE_UNUSED)
{
  /* If we are not generating SVR4 PIC code, or if this is not NewABI code,
     .cplocal is ignored.  */
  if (mips_pic != SVR4_PIC || ! HAVE_NEWABI)
    {
      s_ignore (0);
      return;
    }

  mips_gp_register = tc_get_register (0);
  demand_empty_rest_of_line ();
}

/* Handle the .cprestore pseudo-op.  This stores $gp into a given
   offset from $sp.  The offset is remembered, and after making a PIC
   call $gp is restored from that location.  */

static void
s_cprestore (int ignore ATTRIBUTE_UNUSED)
{
  expressionS ex;

  /* If we are not generating SVR4 PIC code, or if this is NewABI code,
     .cprestore is ignored.  */
  if (mips_pic != SVR4_PIC || HAVE_NEWABI)
    {
      s_ignore (0);
      return;
    }

  mips_cprestore_offset = get_absolute_expression ();
  mips_cprestore_valid = 1;

  ex.X_op = O_constant;
  ex.X_add_symbol = NULL;
  ex.X_op_symbol = NULL;
  ex.X_add_number = mips_cprestore_offset;

  macro_start ();
  macro_build_ldst_constoffset (&ex, ADDRESS_STORE_INSN, mips_gp_register,
				SP, HAVE_64BIT_ADDRESSES);
  macro_end ();

  demand_empty_rest_of_line ();
}

/* Handle the .cpreturn pseudo-op defined for NewABI PIC code. If an offset
   was given in the preceding .cpsetup, it results in:
     ld		$gp, offset($sp)

   If a register $reg2 was given there, it results in:
     daddu	$gp, $reg2, $0  */

static void
s_cpreturn (int ignore ATTRIBUTE_UNUSED)
{
  expressionS ex;

  /* If we are not generating SVR4 PIC code, .cpreturn is ignored.
     We also need NewABI support.  */
  if (mips_pic != SVR4_PIC || ! HAVE_NEWABI)
    {
      s_ignore (0);
      return;
    }

  macro_start ();
  if (mips_cpreturn_register == -1)
    {
      ex.X_op = O_constant;
      ex.X_add_symbol = NULL;
      ex.X_op_symbol = NULL;
      ex.X_add_number = mips_cpreturn_offset;

      macro_build (&ex, "ld", "t,o(b)", mips_gp_register, BFD_RELOC_LO16, SP);
    }
  else
    macro_build (NULL, "daddu", "d,v,t", mips_gp_register,
		 mips_cpreturn_register, 0);
  macro_end ();

  demand_empty_rest_of_line ();
}

/* Handle the .dtprelword and .dtpreldword pseudo-ops.  They generate
   a 32-bit or 64-bit DTP-relative relocation (BYTES says which) for
   use in DWARF debug information.  */

static void
s_dtprel_internal (size_t bytes)
{
  expressionS ex;
  char *p;

  expression (&ex);

  if (ex.X_op != O_symbol)
    {
      as_bad (_("Unsupported use of %s"), (bytes == 8
					   ? ".dtpreldword"
					   : ".dtprelword"));
      ignore_rest_of_line ();
    }

  p = frag_more (bytes);
  md_number_to_chars (p, 0, bytes);
  fix_new_exp (frag_now, p - frag_now->fr_literal, bytes, &ex, FALSE,
	       (bytes == 8
		? BFD_RELOC_MIPS_TLS_DTPREL64
		: BFD_RELOC_MIPS_TLS_DTPREL32));

  demand_empty_rest_of_line ();
}

/* Handle .dtprelword.  */

static void
s_dtprelword (int ignore ATTRIBUTE_UNUSED)
{
  s_dtprel_internal (4);
}

/* Handle .dtpreldword.  */

static void
s_dtpreldword (int ignore ATTRIBUTE_UNUSED)
{
  s_dtprel_internal (8);
}

/* Handle the .gpvalue pseudo-op.  This is used when generating NewABI PIC
   code.  It sets the offset to use in gp_rel relocations.  */

static void
s_gpvalue (int ignore ATTRIBUTE_UNUSED)
{
  /* If we are not generating SVR4 PIC code, .gpvalue is ignored.
     We also need NewABI support.  */
  if (mips_pic != SVR4_PIC || ! HAVE_NEWABI)
    {
      s_ignore (0);
      return;
    }

  mips_gprel_offset = get_absolute_expression ();

  demand_empty_rest_of_line ();
}

/* Handle the .gpword pseudo-op.  This is used when generating PIC
   code.  It generates a 32 bit GP relative reloc.  */

static void
s_gpword (int ignore ATTRIBUTE_UNUSED)
{
  segment_info_type *si;
  struct insn_label_list *l;
  symbolS *label;
  expressionS ex;
  char *p;

  /* When not generating PIC code, this is treated as .word.  */
  if (mips_pic != SVR4_PIC)
    {
      s_cons (2);
      return;
    }

  si = seg_info (now_seg);
  l = si->label_list;
  label = l != NULL ? l->label : NULL;
  mips_emit_delays ();
  if (auto_align)
    mips_align (2, 0, label);
  mips_clear_insn_labels ();

  expression (&ex);

  if (ex.X_op != O_symbol || ex.X_add_number != 0)
    {
      as_bad (_("Unsupported use of .gpword"));
      ignore_rest_of_line ();
    }

  p = frag_more (4);
  md_number_to_chars (p, 0, 4);
  fix_new_exp (frag_now, p - frag_now->fr_literal, 4, &ex, FALSE,
	       BFD_RELOC_GPREL32);

  demand_empty_rest_of_line ();
}

static void
s_gpdword (int ignore ATTRIBUTE_UNUSED)
{
  segment_info_type *si;
  struct insn_label_list *l;
  symbolS *label;
  expressionS ex;
  char *p;

  /* When not generating PIC code, this is treated as .dword.  */
  if (mips_pic != SVR4_PIC)
    {
      s_cons (3);
      return;
    }

  si = seg_info (now_seg);
  l = si->label_list;
  label = l != NULL ? l->label : NULL;
  mips_emit_delays ();
  if (auto_align)
    mips_align (3, 0, label);
  mips_clear_insn_labels ();

  expression (&ex);

  if (ex.X_op != O_symbol || ex.X_add_number != 0)
    {
      as_bad (_("Unsupported use of .gpdword"));
      ignore_rest_of_line ();
    }

  p = frag_more (8);
  md_number_to_chars (p, 0, 8);
  fix_new_exp (frag_now, p - frag_now->fr_literal, 4, &ex, FALSE,
	       BFD_RELOC_GPREL32)->fx_tcbit = 1;

  /* GPREL32 composed with 64 gives a 64-bit GP offset.  */
  fix_new (frag_now, p - frag_now->fr_literal, 8, NULL, 0,
	   FALSE, BFD_RELOC_64)->fx_tcbit = 1;

  demand_empty_rest_of_line ();
}

/* Handle the .cpadd pseudo-op.  This is used when dealing with switch
   tables in SVR4 PIC code.  */

static void
s_cpadd (int ignore ATTRIBUTE_UNUSED)
{
  int reg;

  /* This is ignored when not generating SVR4 PIC code.  */
  if (mips_pic != SVR4_PIC)
    {
      s_ignore (0);
      return;
    }

  /* Add $gp to the register named as an argument.  */
  macro_start ();
  reg = tc_get_register (0);
  macro_build (NULL, ADDRESS_ADD_INSN, "d,v,t", reg, reg, mips_gp_register);
  macro_end ();

  demand_empty_rest_of_line ();
}

/* Handle the .insn pseudo-op.  This marks instruction labels in
   mips16 mode.  This permits the linker to handle them specially,
   such as generating jalx instructions when needed.  We also make
   them odd for the duration of the assembly, in order to generate the
   right sort of code.  We will make them even in the adjust_symtab
   routine, while leaving them marked.  This is convenient for the
   debugger and the disassembler.  The linker knows to make them odd
   again.  */

static void
s_insn (int ignore ATTRIBUTE_UNUSED)
{
  mips16_mark_labels ();

  demand_empty_rest_of_line ();
}

/* Handle a .stabn directive.  We need these in order to mark a label
   as being a mips16 text label correctly.  Sometimes the compiler
   will emit a label, followed by a .stabn, and then switch sections.
   If the label and .stabn are in mips16 mode, then the label is
   really a mips16 text label.  */

static void
s_mips_stab (int type)
{
  if (type == 'n')
    mips16_mark_labels ();

  s_stab (type);
}

/* Handle the .weakext pseudo-op as defined in Kane and Heinrich.  */

static void
s_mips_weakext (int ignore ATTRIBUTE_UNUSED)
{
  char *name;
  int c;
  symbolS *symbolP;
  expressionS exp;

  name = input_line_pointer;
  c = get_symbol_end ();
  symbolP = symbol_find_or_make (name);
  S_SET_WEAK (symbolP);
  *input_line_pointer = c;

  SKIP_WHITESPACE ();

  if (! is_end_of_line[(unsigned char) *input_line_pointer])
    {
      if (S_IS_DEFINED (symbolP))
	{
	  as_bad ("ignoring attempt to redefine symbol %s",
		  S_GET_NAME (symbolP));
	  ignore_rest_of_line ();
	  return;
	}

      if (*input_line_pointer == ',')
	{
	  ++input_line_pointer;
	  SKIP_WHITESPACE ();
	}

      expression (&exp);
      if (exp.X_op != O_symbol)
	{
	  as_bad ("bad .weakext directive");
	  ignore_rest_of_line ();
	  return;
	}
      symbol_set_value_expression (symbolP, &exp);
    }

  demand_empty_rest_of_line ();
}

/* Parse a register string into a number.  Called from the ECOFF code
   to parse .frame.  The argument is non-zero if this is the frame
   register, so that we can record it in mips_frame_reg.  */

int
tc_get_register (int frame)
{
  unsigned int reg;

  SKIP_WHITESPACE ();
  if (! reg_lookup (&input_line_pointer, RWARN | RTYPE_NUM | RTYPE_GP, &reg))
    reg = 0;
  if (frame)
    {
      mips_frame_reg = reg != 0 ? reg : SP;
      mips_frame_reg_valid = 1;
      mips_cprestore_valid = 0;
    }
  return reg;
}

valueT
md_section_align (asection *seg, valueT addr)
{
  int align = bfd_get_section_alignment (stdoutput, seg);

  if (IS_ELF)
    {
      /* We don't need to align ELF sections to the full alignment.
	 However, Irix 5 may prefer that we align them at least to a 16
	 byte boundary.  We don't bother to align the sections if we
	 are targeted for an embedded system.  */
      if (strcmp (TARGET_OS, "elf") == 0)
        return addr;
      if (align > 4)
        align = 4;
    }

  return ((addr + (1 << align) - 1) & (-1 << align));
}

/* Utility routine, called from above as well.  If called while the
   input file is still being read, it's only an approximation.  (For
   example, a symbol may later become defined which appeared to be
   undefined earlier.)  */

static int
nopic_need_relax (symbolS *sym, int before_relaxing)
{
  if (sym == 0)
    return 0;

  if (g_switch_value > 0)
    {
      const char *symname;
      int change;

      /* Find out whether this symbol can be referenced off the $gp
	 register.  It can be if it is smaller than the -G size or if
	 it is in the .sdata or .sbss section.  Certain symbols can
	 not be referenced off the $gp, although it appears as though
	 they can.  */
      symname = S_GET_NAME (sym);
      if (symname != (const char *) NULL
	  && (strcmp (symname, "eprol") == 0
	      || strcmp (symname, "etext") == 0
	      || strcmp (symname, "_gp") == 0
	      || strcmp (symname, "edata") == 0
	      || strcmp (symname, "_fbss") == 0
	      || strcmp (symname, "_fdata") == 0
	      || strcmp (symname, "_ftext") == 0
	      || strcmp (symname, "end") == 0
	      || strcmp (symname, "_gp_disp") == 0))
	change = 1;
      else if ((! S_IS_DEFINED (sym) || S_IS_COMMON (sym))
	       && (0
#ifndef NO_ECOFF_DEBUGGING
		   || (symbol_get_obj (sym)->ecoff_extern_size != 0
		       && (symbol_get_obj (sym)->ecoff_extern_size
			   <= g_switch_value))
#endif
		   /* We must defer this decision until after the whole
		      file has been read, since there might be a .extern
		      after the first use of this symbol.  */
		   || (before_relaxing
#ifndef NO_ECOFF_DEBUGGING
		       && symbol_get_obj (sym)->ecoff_extern_size == 0
#endif
		       && S_GET_VALUE (sym) == 0)
		   || (S_GET_VALUE (sym) != 0
		       && S_GET_VALUE (sym) <= g_switch_value)))
	change = 0;
      else
	{
	  const char *segname;

	  segname = segment_name (S_GET_SEGMENT (sym));
	  assert (strcmp (segname, ".lit8") != 0
		  && strcmp (segname, ".lit4") != 0);
	  change = (strcmp (segname, ".sdata") != 0
		    && strcmp (segname, ".sbss") != 0
		    && strncmp (segname, ".sdata.", 7) != 0
		    && strncmp (segname, ".sbss.", 6) != 0
		    && strncmp (segname, ".gnu.linkonce.sb.", 17) != 0
		    && strncmp (segname, ".gnu.linkonce.s.", 16) != 0);
	}
      return change;
    }
  else
    /* We are not optimizing for the $gp register.  */
    return 1;
}


/* Return true if the given symbol should be considered local for SVR4 PIC.  */

static bfd_boolean
pic_need_relax (symbolS *sym, asection *segtype)
{
  asection *symsec;

  /* Handle the case of a symbol equated to another symbol.  */
  while (symbol_equated_reloc_p (sym))
    {
      symbolS *n;

      /* It's possible to get a loop here in a badly written program.  */
      n = symbol_get_value_expression (sym)->X_add_symbol;
      if (n == sym)
	break;
      sym = n;
    }

  if (symbol_section_p (sym))
    return TRUE;

  symsec = S_GET_SEGMENT (sym);

  /* This must duplicate the test in adjust_reloc_syms.  */
  return (symsec != &bfd_und_section
	  && symsec != &bfd_abs_section
	  && !bfd_is_com_section (symsec)
	  && !s_is_linkonce (sym, segtype)
#ifdef OBJ_ELF
	  /* A global or weak symbol is treated as external.  */
	  && (!IS_ELF || (! S_IS_WEAK (sym) && ! S_IS_EXTERNAL (sym)))
#endif
	  );
}


/* Given a mips16 variant frag FRAGP, return non-zero if it needs an
   extended opcode.  SEC is the section the frag is in.  */

static int
mips16_extended_frag (fragS *fragp, asection *sec, long stretch)
{
  int type;
  const struct mips16_immed_operand *op;
  offsetT val;
  int mintiny, maxtiny;
  segT symsec;
  fragS *sym_frag;

  if (RELAX_MIPS16_USER_SMALL (fragp->fr_subtype))
    return 0;
  if (RELAX_MIPS16_USER_EXT (fragp->fr_subtype))
    return 1;

  type = RELAX_MIPS16_TYPE (fragp->fr_subtype);
  op = mips16_immed_operands;
  while (op->type != type)
    {
      ++op;
      assert (op < mips16_immed_operands + MIPS16_NUM_IMMED);
    }

  if (op->unsp)
    {
      if (type == '<' || type == '>' || type == '[' || type == ']')
	{
	  mintiny = 1;
	  maxtiny = 1 << op->nbits;
	}
      else
	{
	  mintiny = 0;
	  maxtiny = (1 << op->nbits) - 1;
	}
    }
  else
    {
      mintiny = - (1 << (op->nbits - 1));
      maxtiny = (1 << (op->nbits - 1)) - 1;
    }

  sym_frag = symbol_get_frag (fragp->fr_symbol);
  val = S_GET_VALUE (fragp->fr_symbol);
  symsec = S_GET_SEGMENT (fragp->fr_symbol);

  if (op->pcrel)
    {
      addressT addr;

      /* We won't have the section when we are called from
         mips_relax_frag.  However, we will always have been called
         from md_estimate_size_before_relax first.  If this is a
         branch to a different section, we mark it as such.  If SEC is
         NULL, and the frag is not marked, then it must be a branch to
         the same section.  */
      if (sec == NULL)
	{
	  if (RELAX_MIPS16_LONG_BRANCH (fragp->fr_subtype))
	    return 1;
	}
      else
	{
	  /* Must have been called from md_estimate_size_before_relax.  */
	  if (symsec != sec)
	    {
	      fragp->fr_subtype =
		RELAX_MIPS16_MARK_LONG_BRANCH (fragp->fr_subtype);

	      /* FIXME: We should support this, and let the linker
                 catch branches and loads that are out of range.  */
	      as_bad_where (fragp->fr_file, fragp->fr_line,
			    _("unsupported PC relative reference to different section"));

	      return 1;
	    }
	  if (fragp != sym_frag && sym_frag->fr_address == 0)
	    /* Assume non-extended on the first relaxation pass.
	       The address we have calculated will be bogus if this is
	       a forward branch to another frag, as the forward frag
	       will have fr_address == 0.  */
	    return 0;
	}

      /* In this case, we know for sure that the symbol fragment is in
	 the same section.  If the relax_marker of the symbol fragment
	 differs from the relax_marker of this fragment, we have not
	 yet adjusted the symbol fragment fr_address.  We want to add
	 in STRETCH in order to get a better estimate of the address.
	 This particularly matters because of the shift bits.  */
      if (stretch != 0
	  && sym_frag->relax_marker != fragp->relax_marker)
	{
	  fragS *f;

	  /* Adjust stretch for any alignment frag.  Note that if have
             been expanding the earlier code, the symbol may be
             defined in what appears to be an earlier frag.  FIXME:
             This doesn't handle the fr_subtype field, which specifies
             a maximum number of bytes to skip when doing an
             alignment.  */
	  for (f = fragp; f != NULL && f != sym_frag; f = f->fr_next)
	    {
	      if (f->fr_type == rs_align || f->fr_type == rs_align_code)
		{
		  if (stretch < 0)
		    stretch = - ((- stretch)
				 & ~ ((1 << (int) f->fr_offset) - 1));
		  else
		    stretch &= ~ ((1 << (int) f->fr_offset) - 1);
		  if (stretch == 0)
		    break;
		}
	    }
	  if (f != NULL)
	    val += stretch;
	}

      addr = fragp->fr_address + fragp->fr_fix;

      /* The base address rules are complicated.  The base address of
         a branch is the following instruction.  The base address of a
         PC relative load or add is the instruction itself, but if it
         is in a delay slot (in which case it can not be extended) use
         the address of the instruction whose delay slot it is in.  */
      if (type == 'p' || type == 'q')
	{
	  addr += 2;

	  /* If we are currently assuming that this frag should be
	     extended, then, the current address is two bytes
	     higher.  */
	  if (RELAX_MIPS16_EXTENDED (fragp->fr_subtype))
	    addr += 2;

	  /* Ignore the low bit in the target, since it will be set
             for a text label.  */
	  if ((val & 1) != 0)
	    --val;
	}
      else if (RELAX_MIPS16_JAL_DSLOT (fragp->fr_subtype))
	addr -= 4;
      else if (RELAX_MIPS16_DSLOT (fragp->fr_subtype))
	addr -= 2;

      val -= addr & ~ ((1 << op->shift) - 1);

      /* Branch offsets have an implicit 0 in the lowest bit.  */
      if (type == 'p' || type == 'q')
	val /= 2;

      /* If any of the shifted bits are set, we must use an extended
         opcode.  If the address depends on the size of this
         instruction, this can lead to a loop, so we arrange to always
         use an extended opcode.  We only check this when we are in
         the main relaxation loop, when SEC is NULL.  */
      if ((val & ((1 << op->shift) - 1)) != 0 && sec == NULL)
	{
	  fragp->fr_subtype =
	    RELAX_MIPS16_MARK_LONG_BRANCH (fragp->fr_subtype);
	  return 1;
	}

      /* If we are about to mark a frag as extended because the value
         is precisely maxtiny + 1, then there is a chance of an
         infinite loop as in the following code:
	     la	$4,foo
	     .skip	1020
	     .align	2
	   foo:
	 In this case when the la is extended, foo is 0x3fc bytes
	 away, so the la can be shrunk, but then foo is 0x400 away, so
	 the la must be extended.  To avoid this loop, we mark the
	 frag as extended if it was small, and is about to become
	 extended with a value of maxtiny + 1.  */
      if (val == ((maxtiny + 1) << op->shift)
	  && ! RELAX_MIPS16_EXTENDED (fragp->fr_subtype)
	  && sec == NULL)
	{
	  fragp->fr_subtype =
	    RELAX_MIPS16_MARK_LONG_BRANCH (fragp->fr_subtype);
	  return 1;
	}
    }
  else if (symsec != absolute_section && sec != NULL)
    as_bad_where (fragp->fr_file, fragp->fr_line, _("unsupported relocation"));

  if ((val & ((1 << op->shift) - 1)) != 0
      || val < (mintiny << op->shift)
      || val > (maxtiny << op->shift))
    return 1;
  else
    return 0;
}

/* Compute the length of a branch sequence, and adjust the
   RELAX_BRANCH_TOOFAR bit accordingly.  If FRAGP is NULL, the
   worst-case length is computed, with UPDATE being used to indicate
   whether an unconditional (-1), branch-likely (+1) or regular (0)
   branch is to be computed.  */
static int
relaxed_branch_length (fragS *fragp, asection *sec, int update)
{
  bfd_boolean toofar;
  int length;

  if (fragp
      && S_IS_DEFINED (fragp->fr_symbol)
      && sec == S_GET_SEGMENT (fragp->fr_symbol))
    {
      addressT addr;
      offsetT val;

      val = S_GET_VALUE (fragp->fr_symbol) + fragp->fr_offset;

      addr = fragp->fr_address + fragp->fr_fix + 4;

      val -= addr;

      toofar = val < - (0x8000 << 2) || val >= (0x8000 << 2);
    }
  else if (fragp)
    /* If the symbol is not defined or it's in a different segment,
       assume the user knows what's going on and emit a short
       branch.  */
    toofar = FALSE;
  else
    toofar = TRUE;

  if (fragp && update && toofar != RELAX_BRANCH_TOOFAR (fragp->fr_subtype))
    fragp->fr_subtype
      = RELAX_BRANCH_ENCODE (RELAX_BRANCH_UNCOND (fragp->fr_subtype),
			     RELAX_BRANCH_LIKELY (fragp->fr_subtype),
			     RELAX_BRANCH_LINK (fragp->fr_subtype),
			     toofar);

  length = 4;
  if (toofar)
    {
      if (fragp ? RELAX_BRANCH_LIKELY (fragp->fr_subtype) : (update > 0))
	length += 8;

      if (mips_pic != NO_PIC)
	{
	  /* Additional space for PIC loading of target address.  */
	  length += 8;
	  if (mips_opts.isa == ISA_MIPS1)
	    /* Additional space for $at-stabilizing nop.  */
	    length += 4;
	}

      /* If branch is conditional.  */
      if (fragp ? !RELAX_BRANCH_UNCOND (fragp->fr_subtype) : (update >= 0))
	length += 8;
    }

  return length;
}

/* Estimate the size of a frag before relaxing.  Unless this is the
   mips16, we are not really relaxing here, and the final size is
   encoded in the subtype information.  For the mips16, we have to
   decide whether we are using an extended opcode or not.  */

int
md_estimate_size_before_relax (fragS *fragp, asection *segtype)
{
  int change;

  if (RELAX_BRANCH_P (fragp->fr_subtype))
    {

      fragp->fr_var = relaxed_branch_length (fragp, segtype, FALSE);

      return fragp->fr_var;
    }

  if (RELAX_MIPS16_P (fragp->fr_subtype))
    /* We don't want to modify the EXTENDED bit here; it might get us
       into infinite loops.  We change it only in mips_relax_frag().  */
    return (RELAX_MIPS16_EXTENDED (fragp->fr_subtype) ? 4 : 2);

  if (mips_pic == NO_PIC)
    change = nopic_need_relax (fragp->fr_symbol, 0);
  else if (mips_pic == SVR4_PIC)
    change = pic_need_relax (fragp->fr_symbol, segtype);
  else if (mips_pic == VXWORKS_PIC)
    /* For vxworks, GOT16 relocations never have a corresponding LO16.  */
    change = 0;
  else
    abort ();

  if (change)
    {
      fragp->fr_subtype |= RELAX_USE_SECOND;
      return -RELAX_FIRST (fragp->fr_subtype);
    }
  else
    return -RELAX_SECOND (fragp->fr_subtype);
}

/* This is called to see whether a reloc against a defined symbol
   should be converted into a reloc against a section.  */

int
mips_fix_adjustable (fixS *fixp)
{
  if (fixp->fx_r_type == BFD_RELOC_VTABLE_INHERIT
      || fixp->fx_r_type == BFD_RELOC_VTABLE_ENTRY)
    return 0;

  if (fixp->fx_addsy == NULL)
    return 1;

  /* If symbol SYM is in a mergeable section, relocations of the form
     SYM + 0 can usually be made section-relative.  The mergeable data
     is then identified by the section offset rather than by the symbol.

     However, if we're generating REL LO16 relocations, the offset is split
     between the LO16 and parterning high part relocation.  The linker will
     need to recalculate the complete offset in order to correctly identify
     the merge data.

     The linker has traditionally not looked for the parterning high part
     relocation, and has thus allowed orphaned R_MIPS_LO16 relocations to be
     placed anywhere.  Rather than break backwards compatibility by changing
     this, it seems better not to force the issue, and instead keep the
     original symbol.  This will work with either linker behavior.  */
  if ((fixp->fx_r_type == BFD_RELOC_LO16
       || fixp->fx_r_type == BFD_RELOC_MIPS16_LO16
       || reloc_needs_lo_p (fixp->fx_r_type))
      && HAVE_IN_PLACE_ADDENDS
      && (S_GET_SEGMENT (fixp->fx_addsy)->flags & SEC_MERGE) != 0)
    return 0;

#ifdef OBJ_ELF
  /* R_MIPS16_26 relocations against non-MIPS16 functions might resolve
     to a floating-point stub.  The same is true for non-R_MIPS16_26
     relocations against MIPS16 functions; in this case, the stub becomes
     the function's canonical address.

     Floating-point stubs are stored in unique .mips16.call.* or
     .mips16.fn.* sections.  If a stub T for function F is in section S,
     the first relocation in section S must be against F; this is how the
     linker determines the target function.  All relocations that might
     resolve to T must also be against F.  We therefore have the following
     restrictions, which are given in an intentionally-redundant way:

       1. We cannot reduce R_MIPS16_26 relocations against non-MIPS16
	  symbols.

       2. We cannot reduce a stub's relocations against non-MIPS16 symbols
	  if that stub might be used.

       3. We cannot reduce non-R_MIPS16_26 relocations against MIPS16
	  symbols.

       4. We cannot reduce a stub's relocations against MIPS16 symbols if
	  that stub might be used.

     There is a further restriction:

       5. We cannot reduce R_MIPS16_26 relocations against MIPS16 symbols
	  on targets with in-place addends; the relocation field cannot
	  encode the low bit.

     For simplicity, we deal with (3)-(5) by not reducing _any_ relocation
     against a MIPS16 symbol.

     We deal with (1)-(2) by saying that, if there's a R_MIPS16_26
     relocation against some symbol R, no relocation against R may be
     reduced.  (Note that this deals with (2) as well as (1) because
     relocations against global symbols will never be reduced on ELF
     targets.)  This approach is a little simpler than trying to detect
     stub sections, and gives the "all or nothing" per-symbol consistency
     that we have for MIPS16 symbols.  */
  if (IS_ELF
      && fixp->fx_subsy == NULL
      && (S_GET_OTHER (fixp->fx_addsy) == STO_MIPS16
	  || *symbol_get_tc (fixp->fx_addsy)))
    return 0;
#endif

  return 1;
}

/* Translate internal representation of relocation info to BFD target
   format.  */

arelent **
tc_gen_reloc (asection *section ATTRIBUTE_UNUSED, fixS *fixp)
{
  static arelent *retval[4];
  arelent *reloc;
  bfd_reloc_code_real_type code;

  memset (retval, 0, sizeof(retval));
  reloc = retval[0] = (arelent *) xcalloc (1, sizeof (arelent));
  reloc->sym_ptr_ptr = (asymbol **) xmalloc (sizeof (asymbol *));
  *reloc->sym_ptr_ptr = symbol_get_bfdsym (fixp->fx_addsy);
  reloc->address = fixp->fx_frag->fr_address + fixp->fx_where;

  if (fixp->fx_pcrel)
    {
      assert (fixp->fx_r_type == BFD_RELOC_16_PCREL_S2);

      /* At this point, fx_addnumber is "symbol offset - pcrel address".
	 Relocations want only the symbol offset.  */
      reloc->addend = fixp->fx_addnumber + reloc->address;
      if (!IS_ELF)
	{
	  /* A gruesome hack which is a result of the gruesome gas
	     reloc handling.  What's worse, for COFF (as opposed to
	     ECOFF), we might need yet another copy of reloc->address.
	     See bfd_install_relocation.  */
	  reloc->addend += reloc->address;
	}
    }
  else
    reloc->addend = fixp->fx_addnumber;

  /* Since the old MIPS ELF ABI uses Rel instead of Rela, encode the vtable
     entry to be used in the relocation's section offset.  */
  if (! HAVE_NEWABI && fixp->fx_r_type == BFD_RELOC_VTABLE_ENTRY)
    {
      reloc->address = reloc->addend;
      reloc->addend = 0;
    }

  code = fixp->fx_r_type;

  reloc->howto = bfd_reloc_type_lookup (stdoutput, code);
  if (reloc->howto == NULL)
    {
      as_bad_where (fixp->fx_file, fixp->fx_line,
		    _("Can not represent %s relocation in this object file format"),
		    bfd_get_reloc_code_name (code));
      retval[0] = NULL;
    }

  return retval;
}

/* Relax a machine dependent frag.  This returns the amount by which
   the current size of the frag should change.  */

int
mips_relax_frag (asection *sec, fragS *fragp, long stretch)
{
  if (RELAX_BRANCH_P (fragp->fr_subtype))
    {
      offsetT old_var = fragp->fr_var;

      fragp->fr_var = relaxed_branch_length (fragp, sec, TRUE);

      return fragp->fr_var - old_var;
    }

  if (! RELAX_MIPS16_P (fragp->fr_subtype))
    return 0;

  if (mips16_extended_frag (fragp, NULL, stretch))
    {
      if (RELAX_MIPS16_EXTENDED (fragp->fr_subtype))
	return 0;
      fragp->fr_subtype = RELAX_MIPS16_MARK_EXTENDED (fragp->fr_subtype);
      return 2;
    }
  else
    {
      if (! RELAX_MIPS16_EXTENDED (fragp->fr_subtype))
	return 0;
      fragp->fr_subtype = RELAX_MIPS16_CLEAR_EXTENDED (fragp->fr_subtype);
      return -2;
    }

  return 0;
}

/* Convert a machine dependent frag.  */

void
md_convert_frag (bfd *abfd ATTRIBUTE_UNUSED, segT asec, fragS *fragp)
{
  if (RELAX_BRANCH_P (fragp->fr_subtype))
    {
      bfd_byte *buf;
      unsigned long insn;
      expressionS exp;
      fixS *fixp;

      buf = (bfd_byte *)fragp->fr_literal + fragp->fr_fix;

      if (target_big_endian)
	insn = bfd_getb32 (buf);
      else
	insn = bfd_getl32 (buf);

      if (!RELAX_BRANCH_TOOFAR (fragp->fr_subtype))
	{
	  /* We generate a fixup instead of applying it right now
	     because, if there are linker relaxations, we're going to
	     need the relocations.  */
	  exp.X_op = O_symbol;
	  exp.X_add_symbol = fragp->fr_symbol;
	  exp.X_add_number = fragp->fr_offset;

	  fixp = fix_new_exp (fragp, buf - (bfd_byte *)fragp->fr_literal,
			      4, &exp, TRUE, BFD_RELOC_16_PCREL_S2);
	  fixp->fx_file = fragp->fr_file;
	  fixp->fx_line = fragp->fr_line;

	  md_number_to_chars ((char *) buf, insn, 4);
	  buf += 4;
	}
      else
	{
	  int i;

	  as_warn_where (fragp->fr_file, fragp->fr_line,
			 _("relaxed out-of-range branch into a jump"));

	  if (RELAX_BRANCH_UNCOND (fragp->fr_subtype))
	    goto uncond;

	  if (!RELAX_BRANCH_LIKELY (fragp->fr_subtype))
	    {
	      /* Reverse the branch.  */
	      switch ((insn >> 28) & 0xf)
		{
		case 4:
		  /* bc[0-3][tf]l? and bc1any[24][ft] instructions can
		     have the condition reversed by tweaking a single
		     bit, and their opcodes all have 0x4???????.  */
		  assert ((insn & 0xf1000000) == 0x41000000);
		  insn ^= 0x00010000;
		  break;

		case 0:
		  /* bltz	0x04000000	bgez	0x04010000
		     bltzal	0x04100000	bgezal	0x04110000  */
		  assert ((insn & 0xfc0e0000) == 0x04000000);
		  insn ^= 0x00010000;
		  break;

		case 1:
		  /* beq	0x10000000	bne	0x14000000
		     blez	0x18000000	bgtz	0x1c000000  */
		  insn ^= 0x04000000;
		  break;

		default:
		  abort ();
		}
	    }

	  if (RELAX_BRANCH_LINK (fragp->fr_subtype))
	    {
	      /* Clear the and-link bit.  */
	      assert ((insn & 0xfc1c0000) == 0x04100000);

	      /* bltzal		0x04100000	bgezal	0x04110000
		 bltzall	0x04120000	bgezall	0x04130000  */
	      insn &= ~0x00100000;
	    }

	  /* Branch over the branch (if the branch was likely) or the
	     full jump (not likely case).  Compute the offset from the
	     current instruction to branch to.  */
	  if (RELAX_BRANCH_LIKELY (fragp->fr_subtype))
	    i = 16;
	  else
	    {
	      /* How many bytes in instructions we've already emitted?  */
	      i = buf - (bfd_byte *)fragp->fr_literal - fragp->fr_fix;
	      /* How many bytes in instructions from here to the end?  */
	      i = fragp->fr_var - i;
	    }
	  /* Convert to instruction count.  */
	  i >>= 2;
	  /* Branch counts from the next instruction.  */
	  i--;
	  insn |= i;
	  /* Branch over the jump.  */
	  md_number_to_chars ((char *) buf, insn, 4);
	  buf += 4;

	  /* nop */
	  md_number_to_chars ((char *) buf, 0, 4);
	  buf += 4;

	  if (RELAX_BRANCH_LIKELY (fragp->fr_subtype))
	    {
	      /* beql $0, $0, 2f */
	      insn = 0x50000000;
	      /* Compute the PC offset from the current instruction to
		 the end of the variable frag.  */
	      /* How many bytes in instructions we've already emitted?  */
	      i = buf - (bfd_byte *)fragp->fr_literal - fragp->fr_fix;
	      /* How many bytes in instructions from here to the end?  */
	      i = fragp->fr_var - i;
	      /* Convert to instruction count.  */
	      i >>= 2;
	      /* Don't decrement i, because we want to branch over the
		 delay slot.  */

	      insn |= i;
	      md_number_to_chars ((char *) buf, insn, 4);
	      buf += 4;

	      md_number_to_chars ((char *) buf, 0, 4);
	      buf += 4;
	    }

	uncond:
	  if (mips_pic == NO_PIC)
	    {
	      /* j or jal.  */
	      insn = (RELAX_BRANCH_LINK (fragp->fr_subtype)
		      ? 0x0c000000 : 0x08000000);
	      exp.X_op = O_symbol;
	      exp.X_add_symbol = fragp->fr_symbol;
	      exp.X_add_number = fragp->fr_offset;

	      fixp = fix_new_exp (fragp, buf - (bfd_byte *)fragp->fr_literal,
				  4, &exp, FALSE, BFD_RELOC_MIPS_JMP);
	      fixp->fx_file = fragp->fr_file;
	      fixp->fx_line = fragp->fr_line;

	      md_number_to_chars ((char *) buf, insn, 4);
	      buf += 4;
	    }
	  else
	    {
	      /* lw/ld $at, <sym>($gp)  R_MIPS_GOT16 */
	      insn = HAVE_64BIT_ADDRESSES ? 0xdf810000 : 0x8f810000;
	      exp.X_op = O_symbol;
	      exp.X_add_symbol = fragp->fr_symbol;
	      exp.X_add_number = fragp->fr_offset;

	      if (fragp->fr_offset)
		{
		  exp.X_add_symbol = make_expr_symbol (&exp);
		  exp.X_add_number = 0;
		}

	      fixp = fix_new_exp (fragp, buf - (bfd_byte *)fragp->fr_literal,
				  4, &exp, FALSE, BFD_RELOC_MIPS_GOT16);
	      fixp->fx_file = fragp->fr_file;
	      fixp->fx_line = fragp->fr_line;

	      md_number_to_chars ((char *) buf, insn, 4);
	      buf += 4;

	      if (mips_opts.isa == ISA_MIPS1)
		{
		  /* nop */
		  md_number_to_chars ((char *) buf, 0, 4);
		  buf += 4;
		}

	      /* d/addiu $at, $at, <sym>  R_MIPS_LO16 */
	      insn = HAVE_64BIT_ADDRESSES ? 0x64210000 : 0x24210000;

	      fixp = fix_new_exp (fragp, buf - (bfd_byte *)fragp->fr_literal,
				  4, &exp, FALSE, BFD_RELOC_LO16);
	      fixp->fx_file = fragp->fr_file;
	      fixp->fx_line = fragp->fr_line;

	      md_number_to_chars ((char *) buf, insn, 4);
	      buf += 4;

	      /* j(al)r $at.  */
	      if (RELAX_BRANCH_LINK (fragp->fr_subtype))
		insn = 0x0020f809;
	      else
		insn = 0x00200008;

	      md_number_to_chars ((char *) buf, insn, 4);
	      buf += 4;
	    }
	}

      assert (buf == (bfd_byte *)fragp->fr_literal
	      + fragp->fr_fix + fragp->fr_var);

      fragp->fr_fix += fragp->fr_var;

      return;
    }

  if (RELAX_MIPS16_P (fragp->fr_subtype))
    {
      int type;
      const struct mips16_immed_operand *op;
      bfd_boolean small, ext;
      offsetT val;
      bfd_byte *buf;
      unsigned long insn;
      bfd_boolean use_extend;
      unsigned short extend;

      type = RELAX_MIPS16_TYPE (fragp->fr_subtype);
      op = mips16_immed_operands;
      while (op->type != type)
	++op;

      if (RELAX_MIPS16_EXTENDED (fragp->fr_subtype))
	{
	  small = FALSE;
	  ext = TRUE;
	}
      else
	{
	  small = TRUE;
	  ext = FALSE;
	}

      resolve_symbol_value (fragp->fr_symbol);
      val = S_GET_VALUE (fragp->fr_symbol);
      if (op->pcrel)
	{
	  addressT addr;

	  addr = fragp->fr_address + fragp->fr_fix;

	  /* The rules for the base address of a PC relative reloc are
             complicated; see mips16_extended_frag.  */
	  if (type == 'p' || type == 'q')
	    {
	      addr += 2;
	      if (ext)
		addr += 2;
	      /* Ignore the low bit in the target, since it will be
                 set for a text label.  */
	      if ((val & 1) != 0)
		--val;
	    }
	  else if (RELAX_MIPS16_JAL_DSLOT (fragp->fr_subtype))
	    addr -= 4;
	  else if (RELAX_MIPS16_DSLOT (fragp->fr_subtype))
	    addr -= 2;

	  addr &= ~ (addressT) ((1 << op->shift) - 1);
	  val -= addr;

	  /* Make sure the section winds up with the alignment we have
             assumed.  */
	  if (op->shift > 0)
	    record_alignment (asec, op->shift);
	}

      if (ext
	  && (RELAX_MIPS16_JAL_DSLOT (fragp->fr_subtype)
	      || RELAX_MIPS16_DSLOT (fragp->fr_subtype)))
	as_warn_where (fragp->fr_file, fragp->fr_line,
		       _("extended instruction in delay slot"));

      buf = (bfd_byte *) (fragp->fr_literal + fragp->fr_fix);

      if (target_big_endian)
	insn = bfd_getb16 (buf);
      else
	insn = bfd_getl16 (buf);

      mips16_immed (fragp->fr_file, fragp->fr_line, type, val,
		    RELAX_MIPS16_USER_EXT (fragp->fr_subtype),
		    small, ext, &insn, &use_extend, &extend);

      if (use_extend)
	{
	  md_number_to_chars ((char *) buf, 0xf000 | extend, 2);
	  fragp->fr_fix += 2;
	  buf += 2;
	}

      md_number_to_chars ((char *) buf, insn, 2);
      fragp->fr_fix += 2;
      buf += 2;
    }
  else
    {
      int first, second;
      fixS *fixp;

      first = RELAX_FIRST (fragp->fr_subtype);
      second = RELAX_SECOND (fragp->fr_subtype);
      fixp = (fixS *) fragp->fr_opcode;

      /* Possibly emit a warning if we've chosen the longer option.  */
      if (((fragp->fr_subtype & RELAX_USE_SECOND) != 0)
	  == ((fragp->fr_subtype & RELAX_SECOND_LONGER) != 0))
	{
	  const char *msg = macro_warning (fragp->fr_subtype);
	  if (msg != 0)
	    as_warn_where (fragp->fr_file, fragp->fr_line, "%s", msg);
	}

      /* Go through all the fixups for the first sequence.  Disable them
	 (by marking them as done) if we're going to use the second
	 sequence instead.  */
      while (fixp
	     && fixp->fx_frag == fragp
	     && fixp->fx_where < fragp->fr_fix - second)
	{
	  if (fragp->fr_subtype & RELAX_USE_SECOND)
	    fixp->fx_done = 1;
	  fixp = fixp->fx_next;
	}

      /* Go through the fixups for the second sequence.  Disable them if
	 we're going to use the first sequence, otherwise adjust their
	 addresses to account for the relaxation.  */
      while (fixp && fixp->fx_frag == fragp)
	{
	  if (fragp->fr_subtype & RELAX_USE_SECOND)
	    fixp->fx_where -= first;
	  else
	    fixp->fx_done = 1;
	  fixp = fixp->fx_next;
	}

      /* Now modify the frag contents.  */
      if (fragp->fr_subtype & RELAX_USE_SECOND)
	{
	  char *start;

	  start = fragp->fr_literal + fragp->fr_fix - first - second;
	  memmove (start, start + first, second);
	  fragp->fr_fix -= first;
	}
      else
	fragp->fr_fix -= second;
    }
}

#ifdef OBJ_ELF

/* This function is called after the relocs have been generated.
   We've been storing mips16 text labels as odd.  Here we convert them
   back to even for the convenience of the debugger.  */

void
mips_frob_file_after_relocs (void)
{
  asymbol **syms;
  unsigned int count, i;

  if (!IS_ELF)
    return;

  syms = bfd_get_outsymbols (stdoutput);
  count = bfd_get_symcount (stdoutput);
  for (i = 0; i < count; i++, syms++)
    {
      if (elf_symbol (*syms)->internal_elf_sym.st_other == STO_MIPS16
	  && ((*syms)->value & 1) != 0)
	{
	  (*syms)->value &= ~1;
	  /* If the symbol has an odd size, it was probably computed
	     incorrectly, so adjust that as well.  */
	  if ((elf_symbol (*syms)->internal_elf_sym.st_size & 1) != 0)
	    ++elf_symbol (*syms)->internal_elf_sym.st_size;
	}
    }
}

#endif

/* This function is called whenever a label is defined.  It is used
   when handling branch delays; if a branch has a label, we assume we
   can not move it.  */

void
mips_define_label (symbolS *sym)
{
  segment_info_type *si = seg_info (now_seg);
  struct insn_label_list *l;

  if (free_insn_labels == NULL)
    l = (struct insn_label_list *) xmalloc (sizeof *l);
  else
    {
      l = free_insn_labels;
      free_insn_labels = l->next;
    }

  l->label = sym;
  l->next = si->label_list;
  si->label_list = l;

#ifdef OBJ_ELF
  dwarf2_emit_label (sym);
#endif
}

#if defined (OBJ_ELF) || defined (OBJ_MAYBE_ELF)

/* Some special processing for a MIPS ELF file.  */

void
mips_elf_final_processing (void)
{
  /* Write out the register information.  */
  if (mips_abi != N64_ABI)
    {
      Elf32_RegInfo s;

      s.ri_gprmask = mips_gprmask;
      s.ri_cprmask[0] = mips_cprmask[0];
      s.ri_cprmask[1] = mips_cprmask[1];
      s.ri_cprmask[2] = mips_cprmask[2];
      s.ri_cprmask[3] = mips_cprmask[3];
      /* The gp_value field is set by the MIPS ELF backend.  */

      bfd_mips_elf32_swap_reginfo_out (stdoutput, &s,
				       ((Elf32_External_RegInfo *)
					mips_regmask_frag));
    }
  else
    {
      Elf64_Internal_RegInfo s;

      s.ri_gprmask = mips_gprmask;
      s.ri_pad = 0;
      s.ri_cprmask[0] = mips_cprmask[0];
      s.ri_cprmask[1] = mips_cprmask[1];
      s.ri_cprmask[2] = mips_cprmask[2];
      s.ri_cprmask[3] = mips_cprmask[3];
      /* The gp_value field is set by the MIPS ELF backend.  */

      bfd_mips_elf64_swap_reginfo_out (stdoutput, &s,
				       ((Elf64_External_RegInfo *)
					mips_regmask_frag));
    }

  /* Set the MIPS ELF flag bits.  FIXME: There should probably be some
     sort of BFD interface for this.  */
  if (mips_any_noreorder)
    elf_elfheader (stdoutput)->e_flags |= EF_MIPS_NOREORDER;
  if (mips_pic != NO_PIC)
    {
    elf_elfheader (stdoutput)->e_flags |= EF_MIPS_PIC;
      elf_elfheader (stdoutput)->e_flags |= EF_MIPS_CPIC;
    }
  if (mips_abicalls)
    elf_elfheader (stdoutput)->e_flags |= EF_MIPS_CPIC;

  /* Set MIPS ELF flags for ASEs.  */
  /* We may need to define a new flag for DSP ASE, and set this flag when
     file_ase_dsp is true.  */
  /* Same for DSP R2.  */
  /* We may need to define a new flag for MT ASE, and set this flag when
     file_ase_mt is true.  */
  if (file_ase_mips16)
    elf_elfheader (stdoutput)->e_flags |= EF_MIPS_ARCH_ASE_M16;
#if 0 /* XXX FIXME */
  if (file_ase_mips3d)
    elf_elfheader (stdoutput)->e_flags |= ???;
#endif
  if (file_ase_mdmx)
    elf_elfheader (stdoutput)->e_flags |= EF_MIPS_ARCH_ASE_MDMX;

  /* Set the MIPS ELF ABI flags.  */
  if (mips_abi == O32_ABI && USE_E_MIPS_ABI_O32)
    elf_elfheader (stdoutput)->e_flags |= E_MIPS_ABI_O32;
  else if (mips_abi == O64_ABI)
    elf_elfheader (stdoutput)->e_flags |= E_MIPS_ABI_O64;
  else if (mips_abi == EABI_ABI)
    {
      if (!file_mips_gp32)
	elf_elfheader (stdoutput)->e_flags |= E_MIPS_ABI_EABI64;
      else
	elf_elfheader (stdoutput)->e_flags |= E_MIPS_ABI_EABI32;
    }
  else if (mips_abi == N32_ABI)
    elf_elfheader (stdoutput)->e_flags |= EF_MIPS_ABI2;

  /* Nothing to do for N64_ABI.  */

  if (mips_32bitmode)
    elf_elfheader (stdoutput)->e_flags |= EF_MIPS_32BITMODE;

#if 0 /* XXX FIXME */
  /* 32 bit code with 64 bit FP registers.  */
  if (!file_mips_fp32 && ABI_NEEDS_32BIT_REGS (mips_abi))
    elf_elfheader (stdoutput)->e_flags |= ???;
#endif
}

#endif /* OBJ_ELF || OBJ_MAYBE_ELF */

typedef struct proc {
  symbolS *func_sym;
  symbolS *func_end_sym;
  unsigned long reg_mask;
  unsigned long reg_offset;
  unsigned long fpreg_mask;
  unsigned long fpreg_offset;
  unsigned long frame_offset;
  unsigned long frame_reg;
  unsigned long pc_reg;
} procS;

static procS cur_proc;
static procS *cur_proc_ptr;
static int numprocs;

/* Fill in an rs_align_code fragment.  */

void
mips_handle_align (fragS *fragp)
{
  if (fragp->fr_type != rs_align_code)
    return;

  if (mips_opts.mips16)
    {
      static const unsigned char be_nop[] = { 0x65, 0x00 };
      static const unsigned char le_nop[] = { 0x00, 0x65 };

      int bytes;
      char *p;

      bytes = fragp->fr_next->fr_address - fragp->fr_address - fragp->fr_fix;
      p = fragp->fr_literal + fragp->fr_fix;

      if (bytes & 1)
	{
	  *p++ = 0;
	  fragp->fr_fix++;
	}

      memcpy (p, (target_big_endian ? be_nop : le_nop), 2);
      fragp->fr_var = 2;
    }

  /* For mips32, a nop is a zero, which we trivially get by doing nothing.  */
}

static void
md_obj_begin (void)
{
}

static void
md_obj_end (void)
{
  /* Check for premature end, nesting errors, etc.  */
  if (cur_proc_ptr)
    as_warn (_("missing .end at end of assembly"));
}

static long
get_number (void)
{
  int negative = 0;
  long val = 0;

  if (*input_line_pointer == '-')
    {
      ++input_line_pointer;
      negative = 1;
    }
  if (!ISDIGIT (*input_line_pointer))
    as_bad (_("expected simple number"));
  if (input_line_pointer[0] == '0')
    {
      if (input_line_pointer[1] == 'x')
	{
	  input_line_pointer += 2;
	  while (ISXDIGIT (*input_line_pointer))
	    {
	      val <<= 4;
	      val |= hex_value (*input_line_pointer++);
	    }
	  return negative ? -val : val;
	}
      else
	{
	  ++input_line_pointer;
	  while (ISDIGIT (*input_line_pointer))
	    {
	      val <<= 3;
	      val |= *input_line_pointer++ - '0';
	    }
	  return negative ? -val : val;
	}
    }
  if (!ISDIGIT (*input_line_pointer))
    {
      printf (_(" *input_line_pointer == '%c' 0x%02x\n"),
	      *input_line_pointer, *input_line_pointer);
      as_warn (_("invalid number"));
      return -1;
    }
  while (ISDIGIT (*input_line_pointer))
    {
      val *= 10;
      val += *input_line_pointer++ - '0';
    }
  return negative ? -val : val;
}

/* The .file directive; just like the usual .file directive, but there
   is an initial number which is the ECOFF file index.  In the non-ECOFF
   case .file implies DWARF-2.  */

static void
s_mips_file (int x ATTRIBUTE_UNUSED)
{
  static int first_file_directive = 0;

  if (ECOFF_DEBUGGING)
    {
      get_number ();
      s_app_file (0);
    }
  else
    {
      char *filename;

      filename = dwarf2_directive_file (0);

      /* Versions of GCC up to 3.1 start files with a ".file"
	 directive even for stabs output.  Make sure that this
	 ".file" is handled.  Note that you need a version of GCC
         after 3.1 in order to support DWARF-2 on MIPS.  */
      if (filename != NULL && ! first_file_directive)
	{
	  (void) new_logical_line (filename, -1);
	  s_app_file_string (filename, 0);
	}
      first_file_directive = 1;
    }
}

/* The .loc directive, implying DWARF-2.  */

static void
s_mips_loc (int x ATTRIBUTE_UNUSED)
{
  if (!ECOFF_DEBUGGING)
    dwarf2_directive_loc (0);
}

/* The .end directive.  */

static void
s_mips_end (int x ATTRIBUTE_UNUSED)
{
  symbolS *p;

  /* Following functions need their own .frame and .cprestore directives.  */
  mips_frame_reg_valid = 0;
  mips_cprestore_valid = 0;

  if (!is_end_of_line[(unsigned char) *input_line_pointer])
    {
      p = get_symbol ();
      demand_empty_rest_of_line ();
    }
  else
    p = NULL;

  if ((bfd_get_section_flags (stdoutput, now_seg) & SEC_CODE) == 0)
    as_warn (_(".end not in text section"));

  if (!cur_proc_ptr)
    {
      as_warn (_(".end directive without a preceding .ent directive."));
      demand_empty_rest_of_line ();
      return;
    }

  if (p != NULL)
    {
      assert (S_GET_NAME (p));
      if (strcmp (S_GET_NAME (p), S_GET_NAME (cur_proc_ptr->func_sym)))
	as_warn (_(".end symbol does not match .ent symbol."));

      if (debug_type == DEBUG_STABS)
	stabs_generate_asm_endfunc (S_GET_NAME (p),
				    S_GET_NAME (p));
    }
  else
    as_warn (_(".end directive missing or unknown symbol"));

#ifdef OBJ_ELF
  /* Create an expression to calculate the size of the function.  */
  if (p && cur_proc_ptr)
    {
      OBJ_SYMFIELD_TYPE *obj = symbol_get_obj (p);
      expressionS *exp = xmalloc (sizeof (expressionS));

      obj->size = exp;
      exp->X_op = O_subtract;
      exp->X_add_symbol = symbol_temp_new_now ();
      exp->X_op_symbol = p;
      exp->X_add_number = 0;

      cur_proc_ptr->func_end_sym = exp->X_add_symbol;
    }

  /* Generate a .pdr section.  */
  if (IS_ELF && !ECOFF_DEBUGGING && mips_flag_pdr)
    {
      segT saved_seg = now_seg;
      subsegT saved_subseg = now_subseg;
      valueT dot;
      expressionS exp;
      char *fragp;

      dot = frag_now_fix ();

#ifdef md_flush_pending_output
      md_flush_pending_output ();
#endif

      assert (pdr_seg);
      subseg_set (pdr_seg, 0);

      /* Write the symbol.  */
      exp.X_op = O_symbol;
      exp.X_add_symbol = p;
      exp.X_add_number = 0;
      emit_expr (&exp, 4);

      fragp = frag_more (7 * 4);

      md_number_to_chars (fragp, cur_proc_ptr->reg_mask, 4);
      md_number_to_chars (fragp + 4, cur_proc_ptr->reg_offset, 4);
      md_number_to_chars (fragp + 8, cur_proc_ptr->fpreg_mask, 4);
      md_number_to_chars (fragp + 12, cur_proc_ptr->fpreg_offset, 4);
      md_number_to_chars (fragp + 16, cur_proc_ptr->frame_offset, 4);
      md_number_to_chars (fragp + 20, cur_proc_ptr->frame_reg, 4);
      md_number_to_chars (fragp + 24, cur_proc_ptr->pc_reg, 4);

      subseg_set (saved_seg, saved_subseg);
    }
#endif /* OBJ_ELF */

  cur_proc_ptr = NULL;
}

/* The .aent and .ent directives.  */

static void
s_mips_ent (int aent)
{
  symbolS *symbolP;

  symbolP = get_symbol ();
  if (*input_line_pointer == ',')
    ++input_line_pointer;
  SKIP_WHITESPACE ();
  if (ISDIGIT (*input_line_pointer)
      || *input_line_pointer == '-')
    get_number ();

  if ((bfd_get_section_flags (stdoutput, now_seg) & SEC_CODE) == 0)
    as_warn (_(".ent or .aent not in text section."));

  if (!aent && cur_proc_ptr)
    as_warn (_("missing .end"));

  if (!aent)
    {
      /* This function needs its own .frame and .cprestore directives.  */
      mips_frame_reg_valid = 0;
      mips_cprestore_valid = 0;

      cur_proc_ptr = &cur_proc;
      memset (cur_proc_ptr, '\0', sizeof (procS));

      cur_proc_ptr->func_sym = symbolP;

      symbol_get_bfdsym (symbolP)->flags |= BSF_FUNCTION;

      ++numprocs;

      if (debug_type == DEBUG_STABS)
        stabs_generate_asm_func (S_GET_NAME (symbolP),
				 S_GET_NAME (symbolP));
    }

  demand_empty_rest_of_line ();
}

/* The .frame directive. If the mdebug section is present (IRIX 5 native)
   then ecoff.c (ecoff_directive_frame) is used. For embedded targets,
   s_mips_frame is used so that we can set the PDR information correctly.
   We can't use the ecoff routines because they make reference to the ecoff
   symbol table (in the mdebug section).  */

static void
s_mips_frame (int ignore ATTRIBUTE_UNUSED)
{
#ifdef OBJ_ELF
  if (IS_ELF && !ECOFF_DEBUGGING)
    {
      long val;

      if (cur_proc_ptr == (procS *) NULL)
	{
	  as_warn (_(".frame outside of .ent"));
	  demand_empty_rest_of_line ();
	  return;
	}

      cur_proc_ptr->frame_reg = tc_get_register (1);

      SKIP_WHITESPACE ();
      if (*input_line_pointer++ != ','
	  || get_absolute_expression_and_terminator (&val) != ',')
	{
	  as_warn (_("Bad .frame directive"));
	  --input_line_pointer;
	  demand_empty_rest_of_line ();
	  return;
	}

      cur_proc_ptr->frame_offset = val;
      cur_proc_ptr->pc_reg = tc_get_register (0);

      demand_empty_rest_of_line ();
    }
  else
#endif /* OBJ_ELF */
    s_ignore (ignore);
}

/* The .fmask and .mask directives. If the mdebug section is present
   (IRIX 5 native) then ecoff.c (ecoff_directive_mask) is used. For
   embedded targets, s_mips_mask is used so that we can set the PDR
   information correctly. We can't use the ecoff routines because they
   make reference to the ecoff symbol table (in the mdebug section).  */

static void
s_mips_mask (int reg_type)
{
#ifdef OBJ_ELF
  if (IS_ELF && !ECOFF_DEBUGGING)
    {
      long mask, off;

      if (cur_proc_ptr == (procS *) NULL)
	{
	  as_warn (_(".mask/.fmask outside of .ent"));
	  demand_empty_rest_of_line ();
	  return;
	}

      if (get_absolute_expression_and_terminator (&mask) != ',')
	{
	  as_warn (_("Bad .mask/.fmask directive"));
	  --input_line_pointer;
	  demand_empty_rest_of_line ();
	  return;
	}

      off = get_absolute_expression ();

      if (reg_type == 'F')
	{
	  cur_proc_ptr->fpreg_mask = mask;
	  cur_proc_ptr->fpreg_offset = off;
	}
      else
	{
	  cur_proc_ptr->reg_mask = mask;
	  cur_proc_ptr->reg_offset = off;
	}

      demand_empty_rest_of_line ();
    }
  else
#endif /* OBJ_ELF */
    s_ignore (reg_type);
}

/* A table describing all the processors gas knows about.  Names are
   matched in the order listed.

   To ease comparison, please keep this table in the same order as
   gcc's mips_cpu_info_table[].  */
static const struct mips_cpu_info mips_cpu_info_table[] =
{
  /* Entries for generic ISAs */
  { "mips1",          MIPS_CPU_IS_ISA,		ISA_MIPS1,      CPU_R3000 },
  { "mips2",          MIPS_CPU_IS_ISA,		ISA_MIPS2,      CPU_R6000 },
  { "mips3",          MIPS_CPU_IS_ISA,		ISA_MIPS3,      CPU_R4000 },
  { "mips4",          MIPS_CPU_IS_ISA,		ISA_MIPS4,      CPU_R8000 },
  { "mips5",          MIPS_CPU_IS_ISA,		ISA_MIPS5,      CPU_MIPS5 },
  { "mips32",         MIPS_CPU_IS_ISA,		ISA_MIPS32,     CPU_MIPS32 },
  { "mips32r2",       MIPS_CPU_IS_ISA,		ISA_MIPS32R2,   CPU_MIPS32R2 },
  { "mips64",         MIPS_CPU_IS_ISA,		ISA_MIPS64,     CPU_MIPS64 },
  { "mips64r2",       MIPS_CPU_IS_ISA,		ISA_MIPS64R2,   CPU_MIPS64R2 },

  /* MIPS I */
  { "r3000",          0,			ISA_MIPS1,      CPU_R3000 },
  { "r2000",          0,			ISA_MIPS1,      CPU_R3000 },
  { "r3900",          0,			ISA_MIPS1,      CPU_R3900 },

  /* MIPS II */
  { "r6000",          0,			ISA_MIPS2,      CPU_R6000 },

  /* MIPS III */
  { "r4000",          0,			ISA_MIPS3,      CPU_R4000 },
  { "r4010",          0,			ISA_MIPS2,      CPU_R4010 },
  { "vr4100",         0,			ISA_MIPS3,      CPU_VR4100 },
  { "vr4111",         0,			ISA_MIPS3,      CPU_R4111 },
  { "vr4120",         0,			ISA_MIPS3,      CPU_VR4120 },
  { "vr4130",         0,			ISA_MIPS3,      CPU_VR4120 },
  { "vr4181",         0,			ISA_MIPS3,      CPU_R4111 },
  { "vr4300",         0,			ISA_MIPS3,      CPU_R4300 },
  { "r4400",          0,			ISA_MIPS3,      CPU_R4400 },
  { "r4600",          0,			ISA_MIPS3,      CPU_R4600 },
  { "orion",          0,			ISA_MIPS3,      CPU_R4600 },
  { "r4650",          0,			ISA_MIPS3,      CPU_R4650 },

  /* MIPS IV */
  { "r8000",          0,			ISA_MIPS4,      CPU_R8000 },
  { "r10000",         0,			ISA_MIPS4,      CPU_R10000 },
  { "r12000",         0,			ISA_MIPS4,      CPU_R12000 },
  { "vr5000",         0,			ISA_MIPS4,      CPU_R5000 },
  { "vr5400",         0,			ISA_MIPS4,      CPU_VR5400 },
  { "vr5500",         0,			ISA_MIPS4,      CPU_VR5500 },
  { "rm5200",         0,			ISA_MIPS4,      CPU_R5000 },
  { "rm5230",         0,			ISA_MIPS4,      CPU_R5000 },
  { "rm5231",         0,			ISA_MIPS4,      CPU_R5000 },
  { "rm5261",         0,			ISA_MIPS4,      CPU_R5000 },
  { "rm5721",         0,			ISA_MIPS4,      CPU_R5000 },
  { "rm7000",         0,			ISA_MIPS4,      CPU_RM7000 },
  { "rm9000",         0,			ISA_MIPS4,      CPU_RM9000 },

  /* MIPS 32 */
  { "4kc",            0,			ISA_MIPS32,	CPU_MIPS32 },
  { "4km",            0,			ISA_MIPS32,	CPU_MIPS32 },
  { "4kp",            0,			ISA_MIPS32,	CPU_MIPS32 },
  { "4ksc",           MIPS_CPU_ASE_SMARTMIPS,	ISA_MIPS32,	CPU_MIPS32 },

  /* MIPS 32 Release 2 */
  { "4kec",           0,			ISA_MIPS32R2,   CPU_MIPS32R2 },
  { "4kem",           0,			ISA_MIPS32R2,   CPU_MIPS32R2 },
  { "4kep",           0,			ISA_MIPS32R2,   CPU_MIPS32R2 },
  { "4ksd",           MIPS_CPU_ASE_SMARTMIPS,	ISA_MIPS32R2,   CPU_MIPS32R2 },
  { "m4k",            0,			ISA_MIPS32R2,   CPU_MIPS32R2 },
  { "m4kp",           0,			ISA_MIPS32R2,   CPU_MIPS32R2 },
  { "24kc",           0,			ISA_MIPS32R2,   CPU_MIPS32R2 },
  { "24kf",           0,			ISA_MIPS32R2,   CPU_MIPS32R2 },
  { "24kx",           0,			ISA_MIPS32R2,   CPU_MIPS32R2 },
  /* 24KE is a 24K with DSP ASE, other ASEs are optional.  */
  { "24kec",          MIPS_CPU_ASE_DSP,		ISA_MIPS32R2,	CPU_MIPS32R2 },
  { "24kef",          MIPS_CPU_ASE_DSP,		ISA_MIPS32R2,	CPU_MIPS32R2 },
  { "24kex",          MIPS_CPU_ASE_DSP,		ISA_MIPS32R2,	CPU_MIPS32R2 },
  /* 34K is a 24K with DSP and MT ASE, other ASEs are optional.  */
  { "34kc",           MIPS_CPU_ASE_DSP | MIPS_CPU_ASE_MT,
						ISA_MIPS32R2,	CPU_MIPS32R2 },
  { "34kf",           MIPS_CPU_ASE_DSP | MIPS_CPU_ASE_MT,
						ISA_MIPS32R2,	CPU_MIPS32R2 },
  { "34kx",           MIPS_CPU_ASE_DSP | MIPS_CPU_ASE_MT,
						ISA_MIPS32R2,	CPU_MIPS32R2 },
  /* 74K with DSP and DSPR2 ASE, other ASEs are optional.  */
  { "74kc",           MIPS_CPU_ASE_DSP | MIPS_CPU_ASE_DSPR2,
						ISA_MIPS32R2,	CPU_MIPS32R2 },
  { "74kf",           MIPS_CPU_ASE_DSP | MIPS_CPU_ASE_DSPR2,
						ISA_MIPS32R2,	CPU_MIPS32R2 },
  { "74kx",           MIPS_CPU_ASE_DSP | MIPS_CPU_ASE_DSPR2,
						ISA_MIPS32R2,	CPU_MIPS32R2 },

  /* MIPS 64 */
  { "5kc",            0,			ISA_MIPS64,	CPU_MIPS64 },
  { "5kf",            0,			ISA_MIPS64,	CPU_MIPS64 },
  { "20kc",           MIPS_CPU_ASE_MIPS3D,	ISA_MIPS64,	CPU_MIPS64 },
  { "25kf",           MIPS_CPU_ASE_MIPS3D,	ISA_MIPS64,     CPU_MIPS64 },

  /* MIPS 64 Release 2 */

  /* Broadcom SB-1 CPU core */
  { "sb1",            MIPS_CPU_ASE_MIPS3D | MIPS_CPU_ASE_MDMX,
						ISA_MIPS64,	CPU_SB1 },
  /* Broadcom SB-1A CPU core */
  { "sb1a",           MIPS_CPU_ASE_MIPS3D | MIPS_CPU_ASE_MDMX,
						ISA_MIPS64,	CPU_SB1 },

  /* Cavium Networks Octeon CPU core */
  { "octeon",         0,      ISA_MIPS64R2,   CPU_OCTEON },
  { "octeon+",        0,      ISA_MIPS64R2,   CPU_OCTEON },

  /* End marker */
  { NULL, 0, 0, 0 }
};


/* Return true if GIVEN is the same as CANONICAL, or if it is CANONICAL
   with a final "000" replaced by "k".  Ignore case.

   Note: this function is shared between GCC and GAS.  */

static bfd_boolean
mips_strict_matching_cpu_name_p (const char *canonical, const char *given)
{
  while (*given != 0 && TOLOWER (*given) == TOLOWER (*canonical))
    given++, canonical++;

  return ((*given == 0 && *canonical == 0)
	  || (strcmp (canonical, "000") == 0 && strcasecmp (given, "k") == 0));
}


/* Return true if GIVEN matches CANONICAL, where GIVEN is a user-supplied
   CPU name.  We've traditionally allowed a lot of variation here.

   Note: this function is shared between GCC and GAS.  */

static bfd_boolean
mips_matching_cpu_name_p (const char *canonical, const char *given)
{
  /* First see if the name matches exactly, or with a final "000"
     turned into "k".  */
  if (mips_strict_matching_cpu_name_p (canonical, given))
    return TRUE;

  /* If not, try comparing based on numerical designation alone.
     See if GIVEN is an unadorned number, or 'r' followed by a number.  */
  if (TOLOWER (*given) == 'r')
    given++;
  if (!ISDIGIT (*given))
    return FALSE;

  /* Skip over some well-known prefixes in the canonical name,
     hoping to find a number there too.  */
  if (TOLOWER (canonical[0]) == 'v' && TOLOWER (canonical[1]) == 'r')
    canonical += 2;
  else if (TOLOWER (canonical[0]) == 'r' && TOLOWER (canonical[1]) == 'm')
    canonical += 2;
  else if (TOLOWER (canonical[0]) == 'r')
    canonical += 1;

  return mips_strict_matching_cpu_name_p (canonical, given);
}


/* Parse an option that takes the name of a processor as its argument.
   OPTION is the name of the option and CPU_STRING is the argument.
   Return the corresponding processor enumeration if the CPU_STRING is
   recognized, otherwise report an error and return null.

   A similar function exists in GCC.  */

static const struct mips_cpu_info *
mips_parse_cpu (const char *option, const char *cpu_string)
{
  const struct mips_cpu_info *p;

  /* 'from-abi' selects the most compatible architecture for the given
     ABI: MIPS I for 32-bit ABIs and MIPS III for 64-bit ABIs.  For the
     EABIs, we have to decide whether we're using the 32-bit or 64-bit
     version.  Look first at the -mgp options, if given, otherwise base
     the choice on MIPS_DEFAULT_64BIT.

     Treat NO_ABI like the EABIs.  One reason to do this is that the
     plain 'mips' and 'mips64' configs have 'from-abi' as their default
     architecture.  This code picks MIPS I for 'mips' and MIPS III for
     'mips64', just as we did in the days before 'from-abi'.  */
  if (strcasecmp (cpu_string, "from-abi") == 0)
    {
      if (ABI_NEEDS_32BIT_REGS (mips_abi))
	return mips_cpu_info_from_isa (ISA_MIPS1);

      if (ABI_NEEDS_64BIT_REGS (mips_abi))
	return mips_cpu_info_from_isa (ISA_MIPS3);

      if (file_mips_gp32 >= 0)
	return mips_cpu_info_from_isa (file_mips_gp32 ? ISA_MIPS1 : ISA_MIPS3);

      return mips_cpu_info_from_isa (MIPS_DEFAULT_64BIT
				     ? ISA_MIPS3
				     : ISA_MIPS1);
    }

  /* 'default' has traditionally been a no-op.  Probably not very useful.  */
  if (strcasecmp (cpu_string, "default") == 0)
    return 0;

  for (p = mips_cpu_info_table; p->name != 0; p++)
    if (mips_matching_cpu_name_p (p->name, cpu_string))
      return p;

  as_bad ("Bad value (%s) for %s", cpu_string, option);
  return 0;
}

/* Return the canonical processor information for ISA (a member of the
   ISA_MIPS* enumeration).  */

static const struct mips_cpu_info *
mips_cpu_info_from_isa (int isa)
{
  int i;

  for (i = 0; mips_cpu_info_table[i].name != NULL; i++)
    if ((mips_cpu_info_table[i].flags & MIPS_CPU_IS_ISA)
	&& isa == mips_cpu_info_table[i].isa)
      return (&mips_cpu_info_table[i]);

  return NULL;
}

static const struct mips_cpu_info *
mips_cpu_info_from_arch (int arch)
{
  int i;

  for (i = 0; mips_cpu_info_table[i].name != NULL; i++)
    if (arch == mips_cpu_info_table[i].cpu)
      return (&mips_cpu_info_table[i]);

  return NULL;
}

static void
show (FILE *stream, const char *string, int *col_p, int *first_p)
{
  if (*first_p)
    {
      fprintf (stream, "%24s", "");
      *col_p = 24;
    }
  else
    {
      fprintf (stream, ", ");
      *col_p += 2;
    }

  if (*col_p + strlen (string) > 72)
    {
      fprintf (stream, "\n%24s", "");
      *col_p = 24;
    }

  fprintf (stream, "%s", string);
  *col_p += strlen (string);

  *first_p = 0;
}

void
md_show_usage (FILE *stream)
{
  int column, first;
  size_t i;

  fprintf (stream, _("\
MIPS options:\n\
-EB			generate big endian output\n\
-EL			generate little endian output\n\
-g, -g2			do not remove unneeded NOPs or swap branches\n\
-G NUM			allow referencing objects up to NUM bytes\n\
			implicitly with the gp register [default 8]\n"));
  fprintf (stream, _("\
-mips1			generate MIPS ISA I instructions\n\
-mips2			generate MIPS ISA II instructions\n\
-mips3			generate MIPS ISA III instructions\n\
-mips4			generate MIPS ISA IV instructions\n\
-mips5                  generate MIPS ISA V instructions\n\
-mips32                 generate MIPS32 ISA instructions\n\
-mips32r2               generate MIPS32 release 2 ISA instructions\n\
-mips64                 generate MIPS64 ISA instructions\n\
-mips64r2               generate MIPS64 release 2 ISA instructions\n\
-march=CPU/-mtune=CPU	generate code/schedule for CPU, where CPU is one of:\n"));

  first = 1;

  for (i = 0; mips_cpu_info_table[i].name != NULL; i++)
    show (stream, mips_cpu_info_table[i].name, &column, &first);
  show (stream, "from-abi", &column, &first);
  fputc ('\n', stream);

  fprintf (stream, _("\
-mCPU			equivalent to -march=CPU -mtune=CPU. Deprecated.\n\
-no-mCPU		don't generate code specific to CPU.\n\
			For -mCPU and -no-mCPU, CPU must be one of:\n"));

  first = 1;

  show (stream, "3900", &column, &first);
  show (stream, "4010", &column, &first);
  show (stream, "4100", &column, &first);
  show (stream, "4650", &column, &first);
  fputc ('\n', stream);

  fprintf (stream, _("\
-mips16			generate mips16 instructions\n\
-no-mips16		do not generate mips16 instructions\n"));
  fprintf (stream, _("\
-msmartmips		generate smartmips instructions\n\
-mno-smartmips		do not generate smartmips instructions\n"));  
  fprintf (stream, _("\
-mdsp			generate DSP instructions\n\
-mno-dsp		do not generate DSP instructions\n"));
  fprintf (stream, _("\
-mdspr2			generate DSP R2 instructions\n\
-mno-dspr2		do not generate DSP R2 instructions\n"));
  fprintf (stream, _("\
-mmt			generate MT instructions\n\
-mno-mt			do not generate MT instructions\n"));
  fprintf (stream, _("\
-mfix-vr4120		work around certain VR4120 errata\n\
-mfix-vr4130		work around VR4130 mflo/mfhi errata\n\
-mgp32			use 32-bit GPRs, regardless of the chosen ISA\n\
-mfp32			use 32-bit FPRs, regardless of the chosen ISA\n\
-msym32			assume all symbols have 32-bit values\n\
-O0			remove unneeded NOPs, do not swap branches\n\
-O			remove unneeded NOPs and swap branches\n\
--[no-]construct-floats [dis]allow floating point values to be constructed\n\
--trap, --no-break	trap exception on div by 0 and mult overflow\n\
--break, --no-trap	break exception on div by 0 and mult overflow\n"));
#ifdef OBJ_ELF
  fprintf (stream, _("\
-KPIC, -call_shared	generate SVR4 position independent code\n\
-mvxworks-pic		generate VxWorks position independent code\n\
-non_shared		do not generate position independent code\n\
-xgot			assume a 32 bit GOT\n\
-mpdr, -mno-pdr		enable/disable creation of .pdr sections\n\
-mshared, -mno-shared   disable/enable .cpload optimization for\n\
                        position dependent (non shared) code\n\
-mabi=ABI		create ABI conformant object file for:\n"));

  first = 1;

  show (stream, "32", &column, &first);
  show (stream, "o64", &column, &first);
  show (stream, "n32", &column, &first);
  show (stream, "64", &column, &first);
  show (stream, "eabi", &column, &first);

  fputc ('\n', stream);

  fprintf (stream, _("\
-32			create o32 ABI object file (default)\n\
-n32			create n32 ABI object file\n\
-64			create 64 ABI object file\n"));
#endif
  fprintf (stream, _("\
-mocteon-unsupported    error on unsupported Octeon instructions\n\
-mno-octeon-unsupported do not error on unsupported Octeon instructions\n"));
  fprintf (stream, _("\
-mocteon-useun    generate Octeon unaligned load/store instructions\n\
-mno-octeon-useun generate MIPS unaligned load/store instructions\n"));
}

int
mips_dwarf2_addr_size (void)
{
  if (HAVE_64BIT_SYMBOLS)
    return 8;
  else
    return 4;
}

/* Standard calling conventions leave the CFA at SP on entry.  */
void
mips_cfi_frame_initial_instructions (void)
{
  cfi_add_CFA_def_cfa_register (SP);
}

int
tc_mips_regname_to_dw2regnum (char *regname)
{
  unsigned int regnum = -1;
  unsigned int reg;

  if (reg_lookup (&regname, RTYPE_GP | RTYPE_NUM, &reg))
    regnum = reg;

  return regnum;
}
