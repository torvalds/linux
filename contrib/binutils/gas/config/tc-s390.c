/* tc-s390.c -- Assemble for the S390
   Copyright 2000, 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.
   Contributed by Martin Schwidefsky (schwidefsky@de.ibm.com).

   This file is part of GAS, the GNU Assembler.

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
#include "safe-ctype.h"
#include "subsegs.h"
#include "struc-symbol.h"
#include "dwarf2dbg.h"
#include "dw2gencfi.h"

#include "opcode/s390.h"
#include "elf/s390.h"

/* The default architecture.  */
#ifndef DEFAULT_ARCH
#define DEFAULT_ARCH "s390"
#endif
static char *default_arch = DEFAULT_ARCH;
/* Either 32 or 64, selects file format.  */
static int s390_arch_size = 0;

static unsigned int current_mode_mask = 0;
static unsigned int current_cpu = -1U;

/* Whether to use user friendly register names. Default is TRUE.  */
#ifndef TARGET_REG_NAMES_P
#define TARGET_REG_NAMES_P TRUE
#endif

static bfd_boolean reg_names_p = TARGET_REG_NAMES_P;

/* Set to TRUE if we want to warn about zero base/index registers.  */
static bfd_boolean warn_areg_zero = FALSE;

/* Generic assembler global variables which must be defined by all
   targets.  */

const char comment_chars[] = "#";

/* Characters which start a comment at the beginning of a line.  */
const char line_comment_chars[] = "#";

/* Characters which may be used to separate multiple commands on a
   single line.  */
const char line_separator_chars[] = ";";

/* Characters which are used to indicate an exponent in a floating
   point number.  */
const char EXP_CHARS[] = "eE";

/* Characters which mean that a number is a floating point constant,
   as in 0d1.0.  */
const char FLT_CHARS[] = "dD";

/* The dwarf2 data alignment, adjusted for 32 or 64 bit.  */
int s390_cie_data_alignment;

/* The target specific pseudo-ops which we support.  */

/* Define the prototypes for the pseudo-ops */
static void s390_byte PARAMS ((int));
static void s390_elf_cons PARAMS ((int));
static void s390_bss PARAMS ((int));
static void s390_insn PARAMS ((int));
static void s390_literals PARAMS ((int));

const pseudo_typeS md_pseudo_table[] =
{
  { "align", s_align_bytes, 0 },
  /* Pseudo-ops which must be defined.  */
  { "bss",      s390_bss,       0 },
  { "insn",     s390_insn,      0 },
  /* Pseudo-ops which must be overridden.  */
  { "byte",	s390_byte,	0 },
  { "short",    s390_elf_cons,  2 },
  { "long",	s390_elf_cons,	4 },
  { "quad",     s390_elf_cons,  8 },
  { "ltorg",    s390_literals,  0 },
  { "string",   stringer,       2 },
  { NULL,	NULL,		0 }
};


/* Structure to hold information about predefined registers.  */
struct pd_reg
  {
    char *name;
    int value;
  };

/* List of registers that are pre-defined:

   Each access register has a predefined name of the form:
     a<reg_num> which has the value <reg_num>.

   Each control register has a predefined name of the form:
     c<reg_num> which has the value <reg_num>.

   Each general register has a predefined name of the form:
     r<reg_num> which has the value <reg_num>.

   Each floating point register a has predefined name of the form:
     f<reg_num> which has the value <reg_num>.

   There are individual registers as well:
     sp     has the value 15
     lit    has the value 12

   The table is sorted. Suitable for searching by a binary search.  */

static const struct pd_reg pre_defined_registers[] =
{
  { "a0", 0 },     /* Access registers */
  { "a1", 1 },
  { "a10", 10 },
  { "a11", 11 },
  { "a12", 12 },
  { "a13", 13 },
  { "a14", 14 },
  { "a15", 15 },
  { "a2", 2 },
  { "a3", 3 },
  { "a4", 4 },
  { "a5", 5 },
  { "a6", 6 },
  { "a7", 7 },
  { "a8", 8 },
  { "a9", 9 },

  { "c0", 0 },     /* Control registers */
  { "c1", 1 },
  { "c10", 10 },
  { "c11", 11 },
  { "c12", 12 },
  { "c13", 13 },
  { "c14", 14 },
  { "c15", 15 },
  { "c2", 2 },
  { "c3", 3 },
  { "c4", 4 },
  { "c5", 5 },
  { "c6", 6 },
  { "c7", 7 },
  { "c8", 8 },
  { "c9", 9 },

  { "f0", 0 },     /* Floating point registers */
  { "f1", 1 },
  { "f10", 10 },
  { "f11", 11 },
  { "f12", 12 },
  { "f13", 13 },
  { "f14", 14 },
  { "f15", 15 },
  { "f2", 2 },
  { "f3", 3 },
  { "f4", 4 },
  { "f5", 5 },
  { "f6", 6 },
  { "f7", 7 },
  { "f8", 8 },
  { "f9", 9 },

  { "lit", 13 },   /* Pointer to literal pool */

  { "r0", 0 },     /* General purpose registers */
  { "r1", 1 },
  { "r10", 10 },
  { "r11", 11 },
  { "r12", 12 },
  { "r13", 13 },
  { "r14", 14 },
  { "r15", 15 },
  { "r2", 2 },
  { "r3", 3 },
  { "r4", 4 },
  { "r5", 5 },
  { "r6", 6 },
  { "r7", 7 },
  { "r8", 8 },
  { "r9", 9 },

  { "sp", 15 },   /* Stack pointer */

};

#define REG_NAME_CNT (sizeof (pre_defined_registers) / sizeof (struct pd_reg))

static int reg_name_search
  PARAMS ((const struct pd_reg *, int, const char *));
static bfd_boolean register_name PARAMS ((expressionS *));
static void init_default_arch PARAMS ((void));
static void s390_insert_operand
  PARAMS ((unsigned char *, const struct s390_operand *, offsetT, char *,
	   unsigned int));
static char *md_gather_operands
  PARAMS ((char *, unsigned char *, const struct s390_opcode *));

/* Given NAME, find the register number associated with that name, return
   the integer value associated with the given name or -1 on failure.  */

static int
reg_name_search (regs, regcount, name)
     const struct pd_reg *regs;
     int regcount;
     const char *name;
{
  int middle, low, high;
  int cmp;

  low = 0;
  high = regcount - 1;

  do
    {
      middle = (low + high) / 2;
      cmp = strcasecmp (name, regs[middle].name);
      if (cmp < 0)
	high = middle - 1;
      else if (cmp > 0)
	low = middle + 1;
      else
	return regs[middle].value;
    }
  while (low <= high);

  return -1;
}


/*
 * Summary of register_name().
 *
 * in:	Input_line_pointer points to 1st char of operand.
 *
 * out:	A expressionS.
 *      The operand may have been a register: in this case, X_op == O_register,
 *      X_add_number is set to the register number, and truth is returned.
 *	Input_line_pointer->(next non-blank) char after operand, or is in its
 *      original state.
 */

static bfd_boolean
register_name (expressionP)
     expressionS *expressionP;
{
  int reg_number;
  char *name;
  char *start;
  char c;

  /* Find the spelling of the operand.  */
  start = name = input_line_pointer;
  if (name[0] == '%' && ISALPHA (name[1]))
    name = ++input_line_pointer;
  else
    return FALSE;

  c = get_symbol_end ();
  reg_number = reg_name_search (pre_defined_registers, REG_NAME_CNT, name);

  /* Put back the delimiting char.  */
  *input_line_pointer = c;

  /* Look to see if it's in the register table.  */
  if (reg_number >= 0)
    {
      expressionP->X_op = O_register;
      expressionP->X_add_number = reg_number;

      /* Make the rest nice.  */
      expressionP->X_add_symbol = NULL;
      expressionP->X_op_symbol = NULL;
      return TRUE;
    }

  /* Reset the line as if we had not done anything.  */
  input_line_pointer = start;
  return FALSE;
}

/* Local variables.  */

/* Opformat hash table.  */
static struct hash_control *s390_opformat_hash;

/* Opcode hash table.  */
static struct hash_control *s390_opcode_hash;

/* Flags to set in the elf header */
static flagword s390_flags = 0;

symbolS *GOT_symbol;		/* Pre-defined "_GLOBAL_OFFSET_TABLE_" */

#ifndef WORKING_DOT_WORD
int md_short_jump_size = 4;
int md_long_jump_size = 4;
#endif

const char *md_shortopts = "A:m:kVQ:";
struct option md_longopts[] = {
  {NULL, no_argument, NULL, 0}
};
size_t md_longopts_size = sizeof (md_longopts);

/* Initialize the default opcode arch and word size from the default
   architecture name if not specified by an option.  */
static void
init_default_arch ()
{
  if (strcmp (default_arch, "s390") == 0)
    {
      if (s390_arch_size == 0)
	s390_arch_size = 32;
    }
  else if (strcmp (default_arch, "s390x") == 0)
    {
      if (s390_arch_size == 0)
	s390_arch_size = 64;
    }
  else
    as_fatal ("Invalid default architecture, broken assembler.");

  if (current_mode_mask == 0)
    {
      if (s390_arch_size == 32)
	current_mode_mask = 1 << S390_OPCODE_ESA;
      else
	current_mode_mask = 1 << S390_OPCODE_ZARCH;
    }
  if (current_cpu == -1U)
    {
      if (current_mode_mask == (1 << S390_OPCODE_ESA))
	current_cpu = S390_OPCODE_G5;
      else
	current_cpu = S390_OPCODE_Z900;
    }
}

/* Called by TARGET_FORMAT.  */
const char *
s390_target_format ()
{
  /* We don't get a chance to initialize anything before we're called,
     so handle that now.  */
  init_default_arch ();

  return s390_arch_size == 64 ? "elf64-s390" : "elf32-s390";
}

int
md_parse_option (c, arg)
     int c;
     char *arg;
{
  switch (c)
    {
      /* -k: Ignore for FreeBSD compatibility.  */
    case 'k':
      break;
    case 'm':
      if (arg != NULL && strcmp (arg, "regnames") == 0)
	reg_names_p = TRUE;

      else if (arg != NULL && strcmp (arg, "no-regnames") == 0)
	reg_names_p = FALSE;

      else if (arg != NULL && strcmp (arg, "warn-areg-zero") == 0)
	warn_areg_zero = TRUE;

      else if (arg != NULL && strcmp (arg, "31") == 0)
	s390_arch_size = 32;

      else if (arg != NULL && strcmp (arg, "64") == 0)
	s390_arch_size = 64;

      else if (arg != NULL && strcmp (arg, "esa") == 0)
	current_mode_mask = 1 << S390_OPCODE_ESA;

      else if (arg != NULL && strcmp (arg, "zarch") == 0)
	current_mode_mask = 1 << S390_OPCODE_ZARCH;

      else if (arg != NULL && strncmp (arg, "arch=", 5) == 0)
	{
	  if (strcmp (arg + 5, "g5") == 0)
	    current_cpu = S390_OPCODE_G5;
	  else if (strcmp (arg + 5, "g6") == 0)
	    current_cpu = S390_OPCODE_G6;
	  else if (strcmp (arg + 5, "z900") == 0)
	    current_cpu = S390_OPCODE_Z900;
	  else if (strcmp (arg + 5, "z990") == 0)
	    current_cpu = S390_OPCODE_Z990;
	  else if (strcmp (arg + 5, "z9-109") == 0)
	    current_cpu = S390_OPCODE_Z9_109;
	  else if (strcmp (arg + 5, "z9-ec") == 0)
	    current_cpu = S390_OPCODE_Z9_EC;
	  else
	    {
	      as_bad (_("invalid switch -m%s"), arg);
	      return 0;
	    }
	}

      else
	{
	  as_bad (_("invalid switch -m%s"), arg);
	  return 0;
	}
      break;

    case 'A':
      /* Option -A is deprecated. Still available for compatibility.  */
      if (arg != NULL && strcmp (arg, "esa") == 0)
	current_cpu = S390_OPCODE_G5;
      else if (arg != NULL && strcmp (arg, "esame") == 0)
	current_cpu = S390_OPCODE_Z900;
      else
	as_bad ("invalid architecture -A%s", arg);
      break;

      /* -V: SVR4 argument to print version ID.  */
    case 'V':
      print_version_id ();
      break;

      /* -Qy, -Qn: SVR4 arguments controlling whether a .comment section
	 should be emitted or not.  FIXME: Not implemented.  */
    case 'Q':
      break;

    default:
      return 0;
    }

  return 1;
}

void
md_show_usage (stream)
     FILE *stream;
{
  fprintf (stream, _("\
        S390 options:\n\
        -mregnames        Allow symbolic names for registers\n\
        -mwarn-areg-zero  Warn about zero base/index registers\n\
        -mno-regnames     Do not allow symbolic names for registers\n\
        -m31              Set file format to 31 bit format\n\
        -m64              Set file format to 64 bit format\n"));
  fprintf (stream, _("\
        -V                print assembler version number\n\
        -Qy, -Qn          ignored\n"));
}

/* This function is called when the assembler starts up.  It is called
   after the options have been parsed and the output file has been
   opened.  */

void
md_begin ()
{
  register const struct s390_opcode *op;
  const struct s390_opcode *op_end;
  bfd_boolean dup_insn = FALSE;
  const char *retval;

  /* Give a warning if the combination -m64-bit and -Aesa is used.  */
  if (s390_arch_size == 64 && current_cpu < S390_OPCODE_Z900)
    as_warn ("The 64 bit file format is used without esame instructions.");

  s390_cie_data_alignment = -s390_arch_size / 8;

  /* Set the ELF flags if desired.  */
  if (s390_flags)
    bfd_set_private_flags (stdoutput, s390_flags);

  /* Insert the opcode formats into a hash table.  */
  s390_opformat_hash = hash_new ();

  op_end = s390_opformats + s390_num_opformats;
  for (op = s390_opformats; op < op_end; op++)
    {
      retval = hash_insert (s390_opformat_hash, op->name, (PTR) op);
      if (retval != (const char *) NULL)
	{
	  as_bad (_("Internal assembler error for instruction format %s"),
		  op->name);
	  dup_insn = TRUE;
	}
    }

  /* Insert the opcodes into a hash table.  */
  s390_opcode_hash = hash_new ();

  op_end = s390_opcodes + s390_num_opcodes;
  for (op = s390_opcodes; op < op_end; op++)
    if (op->min_cpu <= current_cpu)
      {
	retval = hash_insert (s390_opcode_hash, op->name, (PTR) op);
	if (retval != (const char *) NULL)
	  {
	    as_bad (_("Internal assembler error for instruction %s"),
		    op->name);
	    dup_insn = TRUE;
	  }
	while (op < op_end - 1 && strcmp (op->name, op[1].name) == 0)
	  op++;
      }

  if (dup_insn)
    abort ();

  record_alignment (text_section, 2);
  record_alignment (data_section, 2);
  record_alignment (bss_section, 2);

}

/* Called after all assembly has been done.  */
void
s390_md_end ()
{
  if (s390_arch_size == 64)
    bfd_set_arch_mach (stdoutput, bfd_arch_s390, bfd_mach_s390_64);
  else
    bfd_set_arch_mach (stdoutput, bfd_arch_s390, bfd_mach_s390_31);
}

/* Insert an operand value into an instruction.  */

static void
s390_insert_operand (insn, operand, val, file, line)
     unsigned char *insn;
     const struct s390_operand *operand;
     offsetT val;
     char *file;
     unsigned int line;
{
  addressT uval;
  int offset;

  if (operand->flags & (S390_OPERAND_SIGNED|S390_OPERAND_PCREL))
    {
      offsetT min, max;

      max = ((offsetT) 1 << (operand->bits - 1)) - 1;
      min = - ((offsetT) 1 << (operand->bits - 1));
      /* Halve PCREL operands.  */
      if (operand->flags & S390_OPERAND_PCREL)
	val >>= 1;
      /* Check for underflow / overflow.  */
      if (val < min || val > max)
	{
	  const char *err =
	    "operand out of range (%s not between %ld and %ld)";
	  char buf[100];

	  if (operand->flags & S390_OPERAND_PCREL)
	    {
	      val <<= 1;
	      min <<= 1;
	      max <<= 1;
	    }
	  sprint_value (buf, val);
	  if (file == (char *) NULL)
	    as_bad (err, buf, (int) min, (int) max);
	  else
	    as_bad_where (file, line, err, buf, (int) min, (int) max);
	  return;
	}
      /* val is ok, now restrict it to operand->bits bits.  */
      uval = (addressT) val & ((((addressT) 1 << (operand->bits-1)) << 1) - 1);
      /* val is restrict, now check for special case.  */
      if (operand->bits == 20 && operand->shift == 20)
        uval = (uval >> 12) | ((uval & 0xfff) << 8);
    }
  else
    {
      addressT min, max;

      max = (((addressT) 1 << (operand->bits - 1)) << 1) - 1;
      min = (offsetT) 0;
      uval = (addressT) val;
      /* Length x in an instructions has real length x+1.  */
      if (operand->flags & S390_OPERAND_LENGTH)
	uval--;
      /* Check for underflow / overflow.  */
      if (uval < min || uval > max)
	{
	  if (operand->flags & S390_OPERAND_LENGTH)
	    {
	      uval++;
	      min++;
	      max++;
	    }

	  as_bad_value_out_of_range (_("operand"), uval, (offsetT) min, (offsetT) max, file, line);

	  return;
	}
    }

  /* Insert fragments of the operand byte for byte.  */
  offset = operand->shift + operand->bits;
  uval <<= (-offset) & 7;
  insn += (offset - 1) / 8;
  while (uval != 0)
    {
      *insn-- |= uval;
      uval >>= 8;
    }
}

struct map_tls
  {
    char *string;
    int length;
    bfd_reloc_code_real_type reloc;
  };

static bfd_reloc_code_real_type s390_tls_suffix
  PARAMS ((char **, expressionS *));

/* Parse tls marker and return the desired relocation.  */
static bfd_reloc_code_real_type
s390_tls_suffix (str_p, exp_p)
     char **str_p;
     expressionS *exp_p;
{
  static struct map_tls mapping[] =
  {
    { "tls_load", 8, BFD_RELOC_390_TLS_LOAD },
    { "tls_gdcall", 10, BFD_RELOC_390_TLS_GDCALL  },
    { "tls_ldcall", 10, BFD_RELOC_390_TLS_LDCALL  },
    { NULL,  0, BFD_RELOC_UNUSED }
  };
  struct map_tls *ptr;
  char *orig_line;
  char *str;
  char *ident;
  int len;

  str = *str_p;
  if (*str++ != ':')
    return BFD_RELOC_UNUSED;

  ident = str;
  while (ISIDNUM (*str))
    str++;
  len = str - ident;
  if (*str++ != ':')
    return BFD_RELOC_UNUSED;

  orig_line = input_line_pointer;
  input_line_pointer = str;
  expression (exp_p);
  str = input_line_pointer;
  if (&input_line_pointer != str_p)
    input_line_pointer = orig_line;

  if (exp_p->X_op != O_symbol)
    return BFD_RELOC_UNUSED;

  for (ptr = &mapping[0]; ptr->length > 0; ptr++)
    if (len == ptr->length
	&& strncasecmp (ident, ptr->string, ptr->length) == 0)
      {
	/* Found a matching tls suffix.  */
	*str_p = str;
	return ptr->reloc;
      }
  return BFD_RELOC_UNUSED;
}

/* Structure used to hold suffixes.  */
typedef enum
  {
    ELF_SUFFIX_NONE = 0,
    ELF_SUFFIX_GOT,
    ELF_SUFFIX_PLT,
    ELF_SUFFIX_GOTENT,
    ELF_SUFFIX_GOTOFF,
    ELF_SUFFIX_GOTPLT,
    ELF_SUFFIX_PLTOFF,
    ELF_SUFFIX_TLS_GD,
    ELF_SUFFIX_TLS_GOTIE,
    ELF_SUFFIX_TLS_IE,
    ELF_SUFFIX_TLS_LDM,
    ELF_SUFFIX_TLS_LDO,
    ELF_SUFFIX_TLS_LE
  }
elf_suffix_type;

struct map_bfd
  {
    char *string;
    int length;
    elf_suffix_type suffix;
  };

static elf_suffix_type s390_elf_suffix PARAMS ((char **, expressionS *));
static int s390_exp_compare PARAMS ((expressionS *exp1, expressionS *exp2));
static elf_suffix_type s390_lit_suffix
  PARAMS ((char **, expressionS *, elf_suffix_type));


/* Parse @got/@plt/@gotoff. and return the desired relocation.  */
static elf_suffix_type
s390_elf_suffix (str_p, exp_p)
     char **str_p;
     expressionS *exp_p;
{
  static struct map_bfd mapping[] =
  {
    { "got", 3, ELF_SUFFIX_GOT  },
    { "got12", 5, ELF_SUFFIX_GOT  },
    { "plt", 3, ELF_SUFFIX_PLT  },
    { "gotent", 6, ELF_SUFFIX_GOTENT },
    { "gotoff", 6, ELF_SUFFIX_GOTOFF },
    { "gotplt", 6, ELF_SUFFIX_GOTPLT },
    { "pltoff", 6, ELF_SUFFIX_PLTOFF },
    { "tlsgd", 5, ELF_SUFFIX_TLS_GD },
    { "gotntpoff", 9, ELF_SUFFIX_TLS_GOTIE },
    { "indntpoff", 9, ELF_SUFFIX_TLS_IE },
    { "tlsldm", 6, ELF_SUFFIX_TLS_LDM },
    { "dtpoff", 6, ELF_SUFFIX_TLS_LDO },
    { "ntpoff", 6, ELF_SUFFIX_TLS_LE },
    { NULL,  0, ELF_SUFFIX_NONE }
  };

  struct map_bfd *ptr;
  char *str = *str_p;
  char *ident;
  int len;

  if (*str++ != '@')
    return ELF_SUFFIX_NONE;

  ident = str;
  while (ISALNUM (*str))
    str++;
  len = str - ident;

  for (ptr = &mapping[0]; ptr->length > 0; ptr++)
    if (len == ptr->length
	&& strncasecmp (ident, ptr->string, ptr->length) == 0)
      {
	if (exp_p->X_add_number != 0)
	  as_warn (_("identifier+constant@%s means identifier@%s+constant"),
		   ptr->string, ptr->string);
	/* Now check for identifier@suffix+constant.  */
	if (*str == '-' || *str == '+')
	  {
	    char *orig_line = input_line_pointer;
	    expressionS new_exp;

	    input_line_pointer = str;
	    expression (&new_exp);

	    switch (new_exp.X_op)
	      {
	      case O_constant: /* X_add_number (a constant expression).  */
		exp_p->X_add_number += new_exp.X_add_number;
		str = input_line_pointer;
		break;
	      case O_symbol:   /* X_add_symbol + X_add_number.  */
		/* this case is used for e.g. xyz@PLT+.Label.  */
		exp_p->X_add_number += new_exp.X_add_number;
		exp_p->X_op_symbol = new_exp.X_add_symbol;
		exp_p->X_op = O_add;
		str = input_line_pointer;
		break;
	      case O_uminus:   /* (- X_add_symbol) + X_add_number.  */
		/* this case is used for e.g. xyz@PLT-.Label.  */
		exp_p->X_add_number += new_exp.X_add_number;
		exp_p->X_op_symbol = new_exp.X_add_symbol;
		exp_p->X_op = O_subtract;
		str = input_line_pointer;
		break;
	      default:
		break;
	      }

	    /* If s390_elf_suffix has not been called with
	       &input_line_pointer as first parameter, we have
	       clobbered the input_line_pointer. We have to
	       undo that.  */
	    if (&input_line_pointer != str_p)
	      input_line_pointer = orig_line;
	  }
	*str_p = str;
	return ptr->suffix;
      }

  return BFD_RELOC_UNUSED;
}

/* Structure used to hold a literal pool entry.  */
struct s390_lpe
  {
    struct s390_lpe *next;
    expressionS ex;
    FLONUM_TYPE floatnum;     /* used if X_op == O_big && X_add_number <= 0 */
    LITTLENUM_TYPE bignum[4]; /* used if X_op == O_big && X_add_number > 0  */
    int nbytes;
    bfd_reloc_code_real_type reloc;
    symbolS *sym;
  };

static struct s390_lpe *lpe_free_list = NULL;
static struct s390_lpe *lpe_list = NULL;
static struct s390_lpe *lpe_list_tail = NULL;
static symbolS *lp_sym = NULL;
static int lp_count = 0;
static int lpe_count = 0;

static int
s390_exp_compare (exp1, exp2)
     expressionS *exp1;
     expressionS *exp2;
{
  if (exp1->X_op != exp2->X_op)
    return 0;

  switch (exp1->X_op)
    {
    case O_constant:   /* X_add_number must be equal.  */
    case O_register:
      return exp1->X_add_number == exp2->X_add_number;

    case O_big:
      as_bad (_("Can't handle O_big in s390_exp_compare"));

    case O_symbol:     /* X_add_symbol & X_add_number must be equal.  */
    case O_symbol_rva:
    case O_uminus:
    case O_bit_not:
    case O_logical_not:
      return (exp1->X_add_symbol == exp2->X_add_symbol)
	&&   (exp1->X_add_number == exp2->X_add_number);

    case O_multiply:   /* X_add_symbol,X_op_symbol&X_add_number must be equal.  */
    case O_divide:
    case O_modulus:
    case O_left_shift:
    case O_right_shift:
    case O_bit_inclusive_or:
    case O_bit_or_not:
    case O_bit_exclusive_or:
    case O_bit_and:
    case O_add:
    case O_subtract:
    case O_eq:
    case O_ne:
    case O_lt:
    case O_le:
    case O_ge:
    case O_gt:
    case O_logical_and:
    case O_logical_or:
      return (exp1->X_add_symbol == exp2->X_add_symbol)
	&&   (exp1->X_op_symbol  == exp2->X_op_symbol)
	&&   (exp1->X_add_number == exp2->X_add_number);
    default:
      return 0;
    }
}

/* Test for @lit and if its present make an entry in the literal pool and
   modify the current expression to be an offset into the literal pool.  */
static elf_suffix_type
s390_lit_suffix (str_p, exp_p, suffix)
     char **str_p;
     expressionS *exp_p;
     elf_suffix_type suffix;
{
  bfd_reloc_code_real_type reloc;
  char tmp_name[64];
  char *str = *str_p;
  char *ident;
  struct s390_lpe *lpe;
  int nbytes, len;

  if (*str++ != ':')
    return suffix;       /* No modification.  */

  /* We look for a suffix of the form "@lit1", "@lit2", "@lit4" or "@lit8".  */
  ident = str;
  while (ISALNUM (*str))
    str++;
  len = str - ident;
  if (len != 4 || strncasecmp (ident, "lit", 3) != 0
      || (ident[3]!='1' && ident[3]!='2' && ident[3]!='4' && ident[3]!='8'))
    return suffix;      /* no modification */
  nbytes = ident[3] - '0';

  reloc = BFD_RELOC_UNUSED;
  if (suffix == ELF_SUFFIX_GOT)
    {
      if (nbytes == 2)
	reloc = BFD_RELOC_390_GOT16;
      else if (nbytes == 4)
	reloc = BFD_RELOC_32_GOT_PCREL;
      else if (nbytes == 8)
	reloc = BFD_RELOC_390_GOT64;
    }
  else if (suffix == ELF_SUFFIX_PLT)
    {
      if (nbytes == 4)
	reloc = BFD_RELOC_390_PLT32;
      else if (nbytes == 8)
	reloc = BFD_RELOC_390_PLT64;
    }

  if (suffix != ELF_SUFFIX_NONE && reloc == BFD_RELOC_UNUSED)
    as_bad (_("Invalid suffix for literal pool entry"));

  /* Search the pool if the new entry is a duplicate.  */
  if (exp_p->X_op == O_big)
    {
      /* Special processing for big numbers.  */
      for (lpe = lpe_list; lpe != NULL; lpe = lpe->next)
	{
	  if (lpe->ex.X_op == O_big)
	    {
	      if (exp_p->X_add_number <= 0 && lpe->ex.X_add_number <= 0)
		{
		  if (memcmp (&generic_floating_point_number, &lpe->floatnum,
			      sizeof (FLONUM_TYPE)) == 0)
		    break;
		}
	      else if (exp_p->X_add_number == lpe->ex.X_add_number)
		{
		  if (memcmp (generic_bignum, lpe->bignum,
			      sizeof (LITTLENUM_TYPE)*exp_p->X_add_number) == 0)
		    break;
		}
	    }
	}
    }
  else
    {
      /* Processing for 'normal' data types.  */
      for (lpe = lpe_list; lpe != NULL; lpe = lpe->next)
	if (lpe->nbytes == nbytes && lpe->reloc == reloc
	    && s390_exp_compare (exp_p, &lpe->ex) != 0)
	  break;
    }

  if (lpe == NULL)
    {
      /* A new literal.  */
      if (lpe_free_list != NULL)
	{
	  lpe = lpe_free_list;
	  lpe_free_list = lpe_free_list->next;
	}
      else
	{
	  lpe = (struct s390_lpe *) xmalloc (sizeof (struct s390_lpe));
	}

      lpe->ex = *exp_p;

      if (exp_p->X_op == O_big)
	{
	  if (exp_p->X_add_number <= 0)
	    lpe->floatnum = generic_floating_point_number;
	  else if (exp_p->X_add_number <= 4)
	    memcpy (lpe->bignum, generic_bignum,
		    exp_p->X_add_number * sizeof (LITTLENUM_TYPE));
	  else
	    as_bad (_("Big number is too big"));
	}

      lpe->nbytes = nbytes;
      lpe->reloc = reloc;
      /* Literal pool name defined ?  */
      if (lp_sym == NULL)
	{
	  sprintf (tmp_name, ".L\001%i", lp_count);
	  lp_sym = symbol_make (tmp_name);
	}

      /* Make name for literal pool entry.  */
      sprintf (tmp_name, ".L\001%i\002%i", lp_count, lpe_count);
      lpe_count++;
      lpe->sym = symbol_make (tmp_name);

      /* Add to literal pool list.  */
      lpe->next = NULL;
      if (lpe_list_tail != NULL)
	{
	  lpe_list_tail->next = lpe;
	  lpe_list_tail = lpe;
	}
      else
	lpe_list = lpe_list_tail = lpe;
    }

  /* Now change exp_p to the offset into the literal pool.
     Thats the expression: .L^Ax^By-.L^Ax   */
  exp_p->X_add_symbol = lpe->sym;
  exp_p->X_op_symbol = lp_sym;
  exp_p->X_op = O_subtract;
  exp_p->X_add_number = 0;

  *str_p = str;

  /* We change the suffix type to ELF_SUFFIX_NONE, because
     the difference of two local labels is just a number.  */
  return ELF_SUFFIX_NONE;
}

/* Like normal .long/.short/.word, except support @got, etc.
   clobbers input_line_pointer, checks end-of-line.  */
static void
s390_elf_cons (nbytes)
     register int nbytes;	/* 1=.byte, 2=.word, 4=.long */
{
  expressionS exp;
  elf_suffix_type suffix;

  if (is_it_end_of_statement ())
    {
      demand_empty_rest_of_line ();
      return;
    }

  do
    {
      expression (&exp);

      if (exp.X_op == O_symbol
	  && *input_line_pointer == '@'
	  && (suffix = s390_elf_suffix (&input_line_pointer, &exp)) != ELF_SUFFIX_NONE)
	{
	  bfd_reloc_code_real_type reloc;
	  reloc_howto_type *reloc_howto;
	  int size;
	  char *where;

	  if (nbytes == 2)
	    {
	      static bfd_reloc_code_real_type tab2[] =
		{
		  BFD_RELOC_UNUSED, 		/* ELF_SUFFIX_NONE  */
		  BFD_RELOC_390_GOT16,		/* ELF_SUFFIX_GOT  */
		  BFD_RELOC_UNUSED,		/* ELF_SUFFIX_PLT  */
		  BFD_RELOC_UNUSED,		/* ELF_SUFFIX_GOTENT  */
		  BFD_RELOC_16_GOTOFF,		/* ELF_SUFFIX_GOTOFF  */
		  BFD_RELOC_UNUSED,		/* ELF_SUFFIX_GOTPLT  */
		  BFD_RELOC_390_PLTOFF16,	/* ELF_SUFFIX_PLTOFF  */
		  BFD_RELOC_UNUSED,		/* ELF_SUFFIX_TLS_GD  */
		  BFD_RELOC_UNUSED,		/* ELF_SUFFIX_TLS_GOTIE  */
		  BFD_RELOC_UNUSED,		/* ELF_SUFFIX_TLS_IE  */
		  BFD_RELOC_UNUSED,		/* ELF_SUFFIX_TLS_LDM  */
		  BFD_RELOC_UNUSED,		/* ELF_SUFFIX_TLS_LDO  */
		  BFD_RELOC_UNUSED		/* ELF_SUFFIX_TLS_LE  */
		};
	      reloc = tab2[suffix];
	    }
	  else if (nbytes == 4)
	    {
	      static bfd_reloc_code_real_type tab4[] =
		{
		  BFD_RELOC_UNUSED, 		/* ELF_SUFFIX_NONE  */
		  BFD_RELOC_32_GOT_PCREL,	/* ELF_SUFFIX_GOT  */
		  BFD_RELOC_390_PLT32,		/* ELF_SUFFIX_PLT  */
		  BFD_RELOC_UNUSED,		/* ELF_SUFFIX_GOTENT  */
		  BFD_RELOC_32_GOTOFF,		/* ELF_SUFFIX_GOTOFF  */
		  BFD_RELOC_390_GOTPLT32,	/* ELF_SUFFIX_GOTPLT  */
		  BFD_RELOC_390_PLTOFF32,	/* ELF_SUFFIX_PLTOFF  */
		  BFD_RELOC_390_TLS_GD32,	/* ELF_SUFFIX_TLS_GD  */
		  BFD_RELOC_390_TLS_GOTIE32,	/* ELF_SUFFIX_TLS_GOTIE  */
		  BFD_RELOC_390_TLS_IE32,	/* ELF_SUFFIX_TLS_IE  */
		  BFD_RELOC_390_TLS_LDM32,	/* ELF_SUFFIX_TLS_LDM  */
		  BFD_RELOC_390_TLS_LDO32,	/* ELF_SUFFIX_TLS_LDO  */
		  BFD_RELOC_390_TLS_LE32	/* ELF_SUFFIX_TLS_LE  */
		};
	      reloc = tab4[suffix];
	    }
	  else if (nbytes == 8)
	    {
	      static bfd_reloc_code_real_type tab8[] =
		{
		  BFD_RELOC_UNUSED, 		/* ELF_SUFFIX_NONE  */
		  BFD_RELOC_390_GOT64,		/* ELF_SUFFIX_GOT  */
		  BFD_RELOC_390_PLT64,		/* ELF_SUFFIX_PLT  */
		  BFD_RELOC_UNUSED,		/* ELF_SUFFIX_GOTENT  */
		  BFD_RELOC_390_GOTOFF64,	/* ELF_SUFFIX_GOTOFF  */
		  BFD_RELOC_390_GOTPLT64,	/* ELF_SUFFIX_GOTPLT  */
		  BFD_RELOC_390_PLTOFF64,	/* ELF_SUFFIX_PLTOFF  */
		  BFD_RELOC_390_TLS_GD64,	/* ELF_SUFFIX_TLS_GD  */
		  BFD_RELOC_390_TLS_GOTIE64,	/* ELF_SUFFIX_TLS_GOTIE  */
		  BFD_RELOC_390_TLS_IE64,	/* ELF_SUFFIX_TLS_IE  */
		  BFD_RELOC_390_TLS_LDM64,	/* ELF_SUFFIX_TLS_LDM  */
		  BFD_RELOC_390_TLS_LDO64,	/* ELF_SUFFIX_TLS_LDO  */
		  BFD_RELOC_390_TLS_LE64	/* ELF_SUFFIX_TLS_LE  */
		};
	      reloc = tab8[suffix];
	    }
	  else
	    reloc = BFD_RELOC_UNUSED;

	  if (reloc != BFD_RELOC_UNUSED
	      && (reloc_howto = bfd_reloc_type_lookup (stdoutput, reloc)))
	    {
	      size = bfd_get_reloc_size (reloc_howto);
	      if (size > nbytes)
		as_bad (_("%s relocations do not fit in %d bytes"),
			reloc_howto->name, nbytes);
	      where = frag_more (nbytes);
	      md_number_to_chars (where, 0, size);
	      /* To make fixup_segment do the pc relative conversion the
		 pcrel parameter on the fix_new_exp call needs to be FALSE.  */
	      fix_new_exp (frag_now, where - frag_now->fr_literal,
			   size, &exp, FALSE, reloc);
	    }
	  else
	    as_bad (_("relocation not applicable"));
	}
      else
	emit_expr (&exp, (unsigned int) nbytes);
    }
  while (*input_line_pointer++ == ',');

  input_line_pointer--;		/* Put terminator back into stream.  */
  demand_empty_rest_of_line ();
}

/* We need to keep a list of fixups.  We can't simply generate them as
   we go, because that would require us to first create the frag, and
   that would screw up references to ``.''.  */

struct s390_fixup
  {
    expressionS exp;
    int opindex;
    bfd_reloc_code_real_type reloc;
  };

#define MAX_INSN_FIXUPS (4)

/* This routine is called for each instruction to be assembled.  */

static char *
md_gather_operands (str, insn, opcode)
     char *str;
     unsigned char *insn;
     const struct s390_opcode *opcode;
{
  struct s390_fixup fixups[MAX_INSN_FIXUPS];
  const struct s390_operand *operand;
  const unsigned char *opindex_ptr;
  expressionS ex;
  elf_suffix_type suffix;
  bfd_reloc_code_real_type reloc;
  int skip_optional;
  int parentheses;
  char *f;
  int fc, i;

  while (ISSPACE (*str))
    str++;

  parentheses = 0;
  skip_optional = 0;

  /* Gather the operands.  */
  fc = 0;
  for (opindex_ptr = opcode->operands; *opindex_ptr != 0; opindex_ptr++)
    {
      char *hold;

      operand = s390_operands + *opindex_ptr;

      if (skip_optional && (operand->flags & S390_OPERAND_INDEX))
	{
	  /* We do an early skip. For D(X,B) constructions the index
	     register is skipped (X is optional). For D(L,B) the base
	     register will be the skipped operand, because L is NOT
	     optional.  */
	  skip_optional = 0;
	  continue;
	}

      /* Gather the operand.  */
      hold = input_line_pointer;
      input_line_pointer = str;

      /* Parse the operand.  */
      if (! register_name (&ex))
	expression (&ex);

      str = input_line_pointer;
      input_line_pointer = hold;

      /* Write the operand to the insn.  */
      if (ex.X_op == O_illegal)
	as_bad (_("illegal operand"));
      else if (ex.X_op == O_absent)
	as_bad (_("missing operand"));
      else if (ex.X_op == O_register || ex.X_op == O_constant)
	{
	  s390_lit_suffix (&str, &ex, ELF_SUFFIX_NONE);

	  if (ex.X_op != O_register && ex.X_op != O_constant)
	    {
	      /* We need to generate a fixup for the
		 expression returned by s390_lit_suffix.  */
	      if (fc >= MAX_INSN_FIXUPS)
		as_fatal (_("too many fixups"));
	      fixups[fc].exp = ex;
	      fixups[fc].opindex = *opindex_ptr;
	      fixups[fc].reloc = BFD_RELOC_UNUSED;
	      ++fc;
	    }
	  else
	    {
	      if ((operand->flags & S390_OPERAND_INDEX)
		  && ex.X_add_number == 0
		  && warn_areg_zero)
		as_warn ("index register specified but zero");
	      if ((operand->flags & S390_OPERAND_BASE)
		  && ex.X_add_number == 0
		  && warn_areg_zero)
		as_warn ("base register specified but zero");
	      s390_insert_operand (insn, operand, ex.X_add_number, NULL, 0);
	    }
	}
      else
	{
	  suffix = s390_elf_suffix (&str, &ex);
	  suffix = s390_lit_suffix (&str, &ex, suffix);
	  reloc = BFD_RELOC_UNUSED;

	  if (suffix == ELF_SUFFIX_GOT)
	    {
	      if ((operand->flags & S390_OPERAND_DISP) &&
		  (operand->bits == 12))
		reloc = BFD_RELOC_390_GOT12;
	      else if ((operand->flags & S390_OPERAND_DISP) &&
		       (operand->bits == 20))
		reloc = BFD_RELOC_390_GOT20;
	      else if ((operand->flags & S390_OPERAND_SIGNED)
		       && (operand->bits == 16))
		reloc = BFD_RELOC_390_GOT16;
	      else if ((operand->flags & S390_OPERAND_PCREL)
		       && (operand->bits == 32))
		reloc = BFD_RELOC_390_GOTENT;
	    }
	  else if (suffix == ELF_SUFFIX_PLT)
	    {
	      if ((operand->flags & S390_OPERAND_PCREL)
		  && (operand->bits == 16))
		reloc = BFD_RELOC_390_PLT16DBL;
	      else if ((operand->flags & S390_OPERAND_PCREL)
		       && (operand->bits == 32))
		reloc = BFD_RELOC_390_PLT32DBL;
	    }
	  else if (suffix == ELF_SUFFIX_GOTENT)
	    {
	      if ((operand->flags & S390_OPERAND_PCREL)
		  && (operand->bits == 32))
		reloc = BFD_RELOC_390_GOTENT;
	    }
	  else if (suffix == ELF_SUFFIX_GOTOFF)
	    {
	      if ((operand->flags & S390_OPERAND_SIGNED)
		  && (operand->bits == 16))
		reloc = BFD_RELOC_16_GOTOFF;
	    }
	  else if (suffix == ELF_SUFFIX_PLTOFF)
	    {
	      if ((operand->flags & S390_OPERAND_SIGNED)
		  && (operand->bits == 16))
		reloc = BFD_RELOC_390_PLTOFF16;
	    }
	  else if (suffix == ELF_SUFFIX_GOTPLT)
	    {
	      if ((operand->flags & S390_OPERAND_DISP)
		  && (operand->bits == 12))
		reloc = BFD_RELOC_390_GOTPLT12;
	      else if ((operand->flags & S390_OPERAND_SIGNED)
		       && (operand->bits == 16))
		reloc = BFD_RELOC_390_GOTPLT16;
	      else if ((operand->flags & S390_OPERAND_PCREL)
		       && (operand->bits == 32))
		reloc = BFD_RELOC_390_GOTPLTENT;
	    }
	  else if (suffix == ELF_SUFFIX_TLS_GOTIE)
	    {
	      if ((operand->flags & S390_OPERAND_DISP)
		  && (operand->bits == 12))
		reloc = BFD_RELOC_390_TLS_GOTIE12;
	      else if ((operand->flags & S390_OPERAND_DISP)
		       && (operand->bits == 20))
		reloc = BFD_RELOC_390_TLS_GOTIE20;
	    }
	  else if (suffix == ELF_SUFFIX_TLS_IE)
	    {
	      if ((operand->flags & S390_OPERAND_PCREL)
		       && (operand->bits == 32))
		reloc = BFD_RELOC_390_TLS_IEENT;
	    }

	  if (suffix != ELF_SUFFIX_NONE && reloc == BFD_RELOC_UNUSED)
	    as_bad (_("invalid operand suffix"));
	  /* We need to generate a fixup of type 'reloc' for this
	     expression.  */
	  if (fc >= MAX_INSN_FIXUPS)
	    as_fatal (_("too many fixups"));
	  fixups[fc].exp = ex;
	  fixups[fc].opindex = *opindex_ptr;
	  fixups[fc].reloc = reloc;
	  ++fc;
	}

      /* Check the next character. The call to expression has advanced
	 str past any whitespace.  */
      if (operand->flags & S390_OPERAND_DISP)
	{
	  /* After a displacement a block in parentheses can start.  */
	  if (*str != '(')
	    {
	      /* Check if parenthesized block can be skipped. If the next
		 operand is neiter an optional operand nor a base register
		 then we have a syntax error.  */
	      operand = s390_operands + *(++opindex_ptr);
	      if (!(operand->flags & (S390_OPERAND_INDEX|S390_OPERAND_BASE)))
		as_bad (_("syntax error; missing '(' after displacement"));

	      /* Ok, skip all operands until S390_OPERAND_BASE.  */
	      while (!(operand->flags & S390_OPERAND_BASE))
		operand = s390_operands + *(++opindex_ptr);

	      /* If there is a next operand it must be separated by a comma.  */
	      if (opindex_ptr[1] != '\0')
		{
		  if (*str != ',')
		    {
		      while (opindex_ptr[1] != '\0')
			{
			  operand = s390_operands + *(++opindex_ptr);
			  if (operand->flags & S390_OPERAND_OPTIONAL)
			    continue;
			  as_bad (_("syntax error; expected ,"));
			  break;
			}
		    }
		  else
		    str++;
		}
	    }
	  else
	    {
	      /* We found an opening parentheses.  */
	      str++;
	      for (f = str; *f != '\0'; f++)
		if (*f == ',' || *f == ')')
		  break;
	      /* If there is no comma until the closing parentheses OR
		 there is a comma right after the opening parentheses,
		 we have to skip optional operands.  */
	      if (*f == ',' && f == str)
		{
		  /* comma directly after '(' ? */
		  skip_optional = 1;
		  str++;
		}
	      else
		skip_optional = (*f != ',');
	    }
	}
      else if (operand->flags & S390_OPERAND_BASE)
	{
	  /* After the base register the parenthesed block ends.  */
	  if (*str++ != ')')
	    as_bad (_("syntax error; missing ')' after base register"));
	  skip_optional = 0;
	  /* If there is a next operand it must be separated by a comma.  */
	  if (opindex_ptr[1] != '\0')
	    {
	      if (*str != ',')
		{
		  while (opindex_ptr[1] != '\0')
		    {
		      operand = s390_operands + *(++opindex_ptr);
		      if (operand->flags & S390_OPERAND_OPTIONAL)
			continue;
		      as_bad (_("syntax error; expected ,"));
		      break;
		    }
		}
	      else
		str++;
	    }
	}
      else
	{
	  /* We can find an 'early' closing parentheses in e.g. D(L) instead
	     of D(L,B).  In this case the base register has to be skipped.  */
	  if (*str == ')')
	    {
	      operand = s390_operands + *(++opindex_ptr);

	      if (!(operand->flags & S390_OPERAND_BASE))
		as_bad (_("syntax error; ')' not allowed here"));
	      str++;
	    }
	  /* If there is a next operand it must be separated by a comma.  */
	  if (opindex_ptr[1] != '\0')
	    {
	      if (*str != ',')
		{
		  while (opindex_ptr[1] != '\0')
		    {
		      operand = s390_operands + *(++opindex_ptr);
		      if (operand->flags & S390_OPERAND_OPTIONAL)
			continue;
		      as_bad (_("syntax error; expected ,"));
		      break;
		    }
		}
	      else
		str++;
	    }
	}
    }

  while (ISSPACE (*str))
    ++str;

  /* Check for tls instruction marker.  */
  reloc = s390_tls_suffix (&str, &ex);
  if (reloc != BFD_RELOC_UNUSED)
    {
      /* We need to generate a fixup of type 'reloc' for this
	 instruction.  */
      if (fc >= MAX_INSN_FIXUPS)
	as_fatal (_("too many fixups"));
      fixups[fc].exp = ex;
      fixups[fc].opindex = -1;
      fixups[fc].reloc = reloc;
      ++fc;
    }

  if (*str != '\0')
    {
      char *linefeed;

      if ((linefeed = strchr (str, '\n')) != NULL)
	*linefeed = '\0';
      as_bad (_("junk at end of line: `%s'"), str);
      if (linefeed != NULL)
	*linefeed = '\n';
    }

  /* Write out the instruction.  */
  f = frag_more (opcode->oplen);
  memcpy (f, insn, opcode->oplen);
  dwarf2_emit_insn (opcode->oplen);

  /* Create any fixups.  At this point we do not use a
     bfd_reloc_code_real_type, but instead just use the
     BFD_RELOC_UNUSED plus the operand index.  This lets us easily
     handle fixups for any operand type, although that is admittedly
     not a very exciting feature.  We pick a BFD reloc type in
     md_apply_fix.  */
  for (i = 0; i < fc; i++)
    {

      if (fixups[i].opindex < 0)
	{
	  /* Create tls instruction marker relocation.  */
	  fix_new_exp (frag_now, f - frag_now->fr_literal, opcode->oplen,
		       &fixups[i].exp, 0, fixups[i].reloc);
	  continue;
	}

      operand = s390_operands + fixups[i].opindex;

      if (fixups[i].reloc != BFD_RELOC_UNUSED)
	{
	  reloc_howto_type *reloc_howto;
	  fixS *fixP;
	  int size;

	  reloc_howto = bfd_reloc_type_lookup (stdoutput, fixups[i].reloc);
	  if (!reloc_howto)
	    abort ();

	  size = bfd_get_reloc_size (reloc_howto);

	  if (size < 1 || size > 4)
	    abort ();

	  fixP = fix_new_exp (frag_now,
			      f - frag_now->fr_literal + (operand->shift/8),
			      size, &fixups[i].exp, reloc_howto->pc_relative,
			      fixups[i].reloc);
	  /* Turn off overflow checking in fixup_segment. This is necessary
	     because fixup_segment will signal an overflow for large 4 byte
	     quantities for GOT12 relocations.  */
	  if (   fixups[i].reloc == BFD_RELOC_390_GOT12
	      || fixups[i].reloc == BFD_RELOC_390_GOT20
	      || fixups[i].reloc == BFD_RELOC_390_GOT16)
	    fixP->fx_no_overflow = 1;
	}
      else
	fix_new_exp (frag_now, f - frag_now->fr_literal, 4, &fixups[i].exp,
		     (operand->flags & S390_OPERAND_PCREL) != 0,
		     ((bfd_reloc_code_real_type)
		      (fixups[i].opindex + (int) BFD_RELOC_UNUSED)));
    }
  return str;
}

/* This routine is called for each instruction to be assembled.  */

void
md_assemble (str)
     char *str;
{
  const struct s390_opcode *opcode;
  unsigned char insn[6];
  char *s;

  /* Get the opcode.  */
  for (s = str; *s != '\0' && ! ISSPACE (*s); s++)
    ;
  if (*s != '\0')
    *s++ = '\0';

  /* Look up the opcode in the hash table.  */
  opcode = (struct s390_opcode *) hash_find (s390_opcode_hash, str);
  if (opcode == (const struct s390_opcode *) NULL)
    {
      as_bad (_("Unrecognized opcode: `%s'"), str);
      return;
    }
  else if (!(opcode->modes & current_mode_mask))
    {
      as_bad ("Opcode %s not available in this mode", str);
      return;
    }
  memcpy (insn, opcode->opcode, sizeof (insn));
  md_gather_operands (s, insn, opcode);
}

#ifndef WORKING_DOT_WORD
/* Handle long and short jumps. We don't support these */
void
md_create_short_jump (ptr, from_addr, to_addr, frag, to_symbol)
     char *ptr;
     addressT from_addr, to_addr;
     fragS *frag;
     symbolS *to_symbol;
{
  abort ();
}

void
md_create_long_jump (ptr, from_addr, to_addr, frag, to_symbol)
     char *ptr;
     addressT from_addr, to_addr;
     fragS *frag;
     symbolS *to_symbol;
{
  abort ();
}
#endif

void
s390_bss (ignore)
     int ignore ATTRIBUTE_UNUSED;
{
  /* We don't support putting frags in the BSS segment, we fake it
     by marking in_bss, then looking at s_skip for clues.  */

  subseg_set (bss_section, 0);
  demand_empty_rest_of_line ();
}

/* Pseudo-op handling.  */

void
s390_insn (ignore)
     int ignore ATTRIBUTE_UNUSED;
{
  expressionS exp;
  const struct s390_opcode *opformat;
  unsigned char insn[6];
  char *s;

  /* Get the opcode format.  */
  s = input_line_pointer;
  while (*s != '\0' && *s != ',' && ! ISSPACE (*s))
    s++;
  if (*s != ',')
    as_bad (_("Invalid .insn format\n"));
  *s++ = '\0';

  /* Look up the opcode in the hash table.  */
  opformat = (struct s390_opcode *)
    hash_find (s390_opformat_hash, input_line_pointer);
  if (opformat == (const struct s390_opcode *) NULL)
    {
      as_bad (_("Unrecognized opcode format: `%s'"), input_line_pointer);
      return;
    }
  input_line_pointer = s;
  expression (&exp);
  if (exp.X_op == O_constant)
    {
      if (   (   opformat->oplen == 6
	      && (addressT) exp.X_add_number < (1ULL << 48))
	  || (   opformat->oplen == 4
	      && (addressT) exp.X_add_number < (1ULL << 32))
	  || (   opformat->oplen == 2
	      && (addressT) exp.X_add_number < (1ULL << 16)))
	md_number_to_chars ((char *) insn, exp.X_add_number, opformat->oplen);
      else
	as_bad (_("Invalid .insn format\n"));
    }
  else if (exp.X_op == O_big)
    {
      if (exp.X_add_number > 0
	  && opformat->oplen == 6
	  && generic_bignum[3] == 0)
	{
	  md_number_to_chars ((char *) insn, generic_bignum[2], 2);
	  md_number_to_chars ((char *) &insn[2], generic_bignum[1], 2);
	  md_number_to_chars ((char *) &insn[4], generic_bignum[0], 2);
	}
      else
	as_bad (_("Invalid .insn format\n"));
    }
  else
    as_bad (_("second operand of .insn not a constant\n"));

  if (strcmp (opformat->name, "e") != 0 && *input_line_pointer++ != ',')
    as_bad (_("missing comma after insn constant\n"));

  if ((s = strchr (input_line_pointer, '\n')) != NULL)
    *s = '\0';
  input_line_pointer = md_gather_operands (input_line_pointer, insn,
					   opformat);
  if (s != NULL)
    *s = '\n';
  demand_empty_rest_of_line ();
}

/* The .byte pseudo-op.  This is similar to the normal .byte
   pseudo-op, but it can also take a single ASCII string.  */

static void
s390_byte (ignore)
     int ignore ATTRIBUTE_UNUSED;
{
  if (*input_line_pointer != '\"')
    {
      cons (1);
      return;
    }

  /* Gather characters.  A real double quote is doubled.  Unusual
     characters are not permitted.  */
  ++input_line_pointer;
  while (1)
    {
      char c;

      c = *input_line_pointer++;

      if (c == '\"')
	{
	  if (*input_line_pointer != '\"')
	    break;
	  ++input_line_pointer;
	}

      FRAG_APPEND_1_CHAR (c);
    }

  demand_empty_rest_of_line ();
}

/* The .ltorg pseudo-op.This emits all literals defined since the last
   .ltorg or the invocation of gas. Literals are defined with the
   @lit suffix.  */

static void
s390_literals (ignore)
     int ignore ATTRIBUTE_UNUSED;
{
  struct s390_lpe *lpe;

  if (lp_sym == NULL || lpe_count == 0)
    return;     /* Nothing to be done.  */

  /* Emit symbol for start of literal pool.  */
  S_SET_SEGMENT (lp_sym, now_seg);
  S_SET_VALUE (lp_sym, (valueT) frag_now_fix ());
  lp_sym->sy_frag = frag_now;

  while (lpe_list)
    {
      lpe = lpe_list;
      lpe_list = lpe_list->next;
      S_SET_SEGMENT (lpe->sym, now_seg);
      S_SET_VALUE (lpe->sym, (valueT) frag_now_fix ());
      lpe->sym->sy_frag = frag_now;

      /* Emit literal pool entry.  */
      if (lpe->reloc != BFD_RELOC_UNUSED)
	{
	  reloc_howto_type *reloc_howto =
	    bfd_reloc_type_lookup (stdoutput, lpe->reloc);
	  int size = bfd_get_reloc_size (reloc_howto);
	  char *where;

	  if (size > lpe->nbytes)
	    as_bad (_("%s relocations do not fit in %d bytes"),
		    reloc_howto->name, lpe->nbytes);
	  where = frag_more (lpe->nbytes);
	  md_number_to_chars (where, 0, size);
	  fix_new_exp (frag_now, where - frag_now->fr_literal,
		       size, &lpe->ex, reloc_howto->pc_relative, lpe->reloc);
	}
      else
	{
	  if (lpe->ex.X_op == O_big)
	    {
	      if (lpe->ex.X_add_number <= 0)
		generic_floating_point_number = lpe->floatnum;
	      else
		memcpy (generic_bignum, lpe->bignum,
			lpe->ex.X_add_number * sizeof (LITTLENUM_TYPE));
	    }
	  emit_expr (&lpe->ex, lpe->nbytes);
	}

      lpe->next = lpe_free_list;
      lpe_free_list = lpe;
    }
  lpe_list_tail = NULL;
  lp_sym = NULL;
  lp_count++;
  lpe_count = 0;
}

/* Turn a string in input_line_pointer into a floating point constant
   of type type, and store the appropriate bytes in *litp.  The number
   of LITTLENUMS emitted is stored in *sizep .  An error message is
   returned, or NULL on OK.  */

char *
md_atof (type, litp, sizep)
     int type;
     char *litp;
     int *sizep;
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
      *sizep = 0;
      return "bad call to md_atof";
    }

  t = atof_ieee (input_line_pointer, type, words);
  if (t)
    input_line_pointer = t;

  *sizep = prec * 2;

  for (i = 0; i < prec; i++)
    {
      md_number_to_chars (litp, (valueT) words[i], 2);
      litp += 2;
    }

  return NULL;
}

/* Align a section (I don't know why this is machine dependent).  */

valueT
md_section_align (seg, addr)
     asection *seg;
     valueT addr;
{
  int align = bfd_get_section_alignment (stdoutput, seg);

  return ((addr + (1 << align) - 1) & (-1 << align));
}

/* We don't have any form of relaxing.  */

int
md_estimate_size_before_relax (fragp, seg)
     fragS *fragp ATTRIBUTE_UNUSED;
     asection *seg ATTRIBUTE_UNUSED;
{
  abort ();
  return 0;
}

/* Convert a machine dependent frag.  We never generate these.  */

void
md_convert_frag (abfd, sec, fragp)
     bfd *abfd ATTRIBUTE_UNUSED;
     asection *sec ATTRIBUTE_UNUSED;
     fragS *fragp ATTRIBUTE_UNUSED;
{
  abort ();
}

symbolS *
md_undefined_symbol (name)
     char *name;
{
  if (*name == '_' && *(name + 1) == 'G'
      && strcmp (name, "_GLOBAL_OFFSET_TABLE_") == 0)
    {
      if (!GOT_symbol)
	{
	  if (symbol_find (name))
	    as_bad (_("GOT already in symbol table"));
	  GOT_symbol = symbol_new (name, undefined_section,
				   (valueT) 0, &zero_address_frag);
	}
      return GOT_symbol;
    }
  return 0;
}

/* Functions concerning relocs.  */

/* The location from which a PC relative jump should be calculated,
   given a PC relative reloc.  */

long
md_pcrel_from_section (fixp, sec)
     fixS *fixp;
     segT sec ATTRIBUTE_UNUSED;
{
  return fixp->fx_frag->fr_address + fixp->fx_where;
}

/* Here we decide which fixups can be adjusted to make them relative to
   the beginning of the section instead of the symbol.  Basically we need
   to make sure that the dynamic relocations are done correctly, so in
   some cases we force the original symbol to be used.  */
int
tc_s390_fix_adjustable (fixP)
     fixS *fixP;
{
  /* Don't adjust references to merge sections.  */
  if ((S_GET_SEGMENT (fixP->fx_addsy)->flags & SEC_MERGE) != 0)
    return 0;
  /* adjust_reloc_syms doesn't know about the GOT.  */
  if (   fixP->fx_r_type == BFD_RELOC_16_GOTOFF
      || fixP->fx_r_type == BFD_RELOC_32_GOTOFF
      || fixP->fx_r_type == BFD_RELOC_390_GOTOFF64
      || fixP->fx_r_type == BFD_RELOC_390_PLTOFF16
      || fixP->fx_r_type == BFD_RELOC_390_PLTOFF32
      || fixP->fx_r_type == BFD_RELOC_390_PLTOFF64
      || fixP->fx_r_type == BFD_RELOC_390_PLT16DBL
      || fixP->fx_r_type == BFD_RELOC_390_PLT32
      || fixP->fx_r_type == BFD_RELOC_390_PLT32DBL
      || fixP->fx_r_type == BFD_RELOC_390_PLT64
      || fixP->fx_r_type == BFD_RELOC_390_GOT12
      || fixP->fx_r_type == BFD_RELOC_390_GOT20
      || fixP->fx_r_type == BFD_RELOC_390_GOT16
      || fixP->fx_r_type == BFD_RELOC_32_GOT_PCREL
      || fixP->fx_r_type == BFD_RELOC_390_GOT64
      || fixP->fx_r_type == BFD_RELOC_390_GOTENT
      || fixP->fx_r_type == BFD_RELOC_390_GOTPLT12
      || fixP->fx_r_type == BFD_RELOC_390_GOTPLT16
      || fixP->fx_r_type == BFD_RELOC_390_GOTPLT20
      || fixP->fx_r_type == BFD_RELOC_390_GOTPLT32
      || fixP->fx_r_type == BFD_RELOC_390_GOTPLT64
      || fixP->fx_r_type == BFD_RELOC_390_GOTPLTENT
      || fixP->fx_r_type == BFD_RELOC_390_TLS_LOAD
      || fixP->fx_r_type == BFD_RELOC_390_TLS_GDCALL
      || fixP->fx_r_type == BFD_RELOC_390_TLS_LDCALL
      || fixP->fx_r_type == BFD_RELOC_390_TLS_GD32
      || fixP->fx_r_type == BFD_RELOC_390_TLS_GD64
      || fixP->fx_r_type == BFD_RELOC_390_TLS_GOTIE12
      || fixP->fx_r_type == BFD_RELOC_390_TLS_GOTIE20
      || fixP->fx_r_type == BFD_RELOC_390_TLS_GOTIE32
      || fixP->fx_r_type == BFD_RELOC_390_TLS_GOTIE64
      || fixP->fx_r_type == BFD_RELOC_390_TLS_LDM32
      || fixP->fx_r_type == BFD_RELOC_390_TLS_LDM64
      || fixP->fx_r_type == BFD_RELOC_390_TLS_IE32
      || fixP->fx_r_type == BFD_RELOC_390_TLS_IE64
      || fixP->fx_r_type == BFD_RELOC_390_TLS_IEENT
      || fixP->fx_r_type == BFD_RELOC_390_TLS_LE32
      || fixP->fx_r_type == BFD_RELOC_390_TLS_LE64
      || fixP->fx_r_type == BFD_RELOC_390_TLS_LDO32
      || fixP->fx_r_type == BFD_RELOC_390_TLS_LDO64
      || fixP->fx_r_type == BFD_RELOC_390_TLS_DTPMOD
      || fixP->fx_r_type == BFD_RELOC_390_TLS_DTPOFF
      || fixP->fx_r_type == BFD_RELOC_390_TLS_TPOFF
      || fixP->fx_r_type == BFD_RELOC_VTABLE_INHERIT
      || fixP->fx_r_type == BFD_RELOC_VTABLE_ENTRY)
    return 0;
  return 1;
}

/* Return true if we must always emit a reloc for a type and false if
   there is some hope of resolving it at assembly time.  */
int
tc_s390_force_relocation (fixp)
     struct fix *fixp;
{
  /* Ensure we emit a relocation for every reference to the global
     offset table or to the procedure link table.  */
  switch (fixp->fx_r_type)
    {
    case BFD_RELOC_390_GOT12:
    case BFD_RELOC_390_GOT20:
    case BFD_RELOC_32_GOT_PCREL:
    case BFD_RELOC_32_GOTOFF:
    case BFD_RELOC_390_GOTOFF64:
    case BFD_RELOC_390_PLTOFF16:
    case BFD_RELOC_390_PLTOFF32:
    case BFD_RELOC_390_PLTOFF64:
    case BFD_RELOC_390_GOTPC:
    case BFD_RELOC_390_GOT16:
    case BFD_RELOC_390_GOTPCDBL:
    case BFD_RELOC_390_GOT64:
    case BFD_RELOC_390_GOTENT:
    case BFD_RELOC_390_PLT32:
    case BFD_RELOC_390_PLT16DBL:
    case BFD_RELOC_390_PLT32DBL:
    case BFD_RELOC_390_PLT64:
    case BFD_RELOC_390_GOTPLT12:
    case BFD_RELOC_390_GOTPLT16:
    case BFD_RELOC_390_GOTPLT20:
    case BFD_RELOC_390_GOTPLT32:
    case BFD_RELOC_390_GOTPLT64:
    case BFD_RELOC_390_GOTPLTENT:
      return 1;
    default:
      break;;
    }

  return generic_force_reloc (fixp);
}

/* Apply a fixup to the object code.  This is called for all the
   fixups we generated by the call to fix_new_exp, above.  In the call
   above we used a reloc code which was the largest legal reloc code
   plus the operand index.  Here we undo that to recover the operand
   index.  At this point all symbol values should be fully resolved,
   and we attempt to completely resolve the reloc.  If we can not do
   that, we determine the correct reloc code and put it back in the
   fixup.  */

void
md_apply_fix (fixP, valP, seg)
     fixS *fixP;
     valueT *valP;
     segT seg ATTRIBUTE_UNUSED;
{
  char *where;
  valueT value = *valP;

  where = fixP->fx_frag->fr_literal + fixP->fx_where;

  if (fixP->fx_subsy != NULL)
    as_bad_where (fixP->fx_file, fixP->fx_line,
		  "cannot emit relocation %s against subsy symbol %s",
		  bfd_get_reloc_code_name (fixP->fx_r_type),
		  S_GET_NAME (fixP->fx_subsy));

  if (fixP->fx_addsy != NULL)
    {
      if (fixP->fx_pcrel)
	value += fixP->fx_frag->fr_address + fixP->fx_where;
    }
  else
    fixP->fx_done = 1;

  if ((int) fixP->fx_r_type >= (int) BFD_RELOC_UNUSED)
    {
      const struct s390_operand *operand;
      int opindex;

      opindex = (int) fixP->fx_r_type - (int) BFD_RELOC_UNUSED;
      operand = &s390_operands[opindex];

      if (fixP->fx_done)
	{
	  /* Insert the fully resolved operand value.  */
	  s390_insert_operand ((unsigned char *) where, operand,
			       (offsetT) value, fixP->fx_file, fixP->fx_line);
	  return;
	}

      /* Determine a BFD reloc value based on the operand information.
	 We are only prepared to turn a few of the operands into
	 relocs.  */
      fixP->fx_offset = value;
      if (operand->bits == 12 && operand->shift == 20)
	{
	  fixP->fx_size = 2;
	  fixP->fx_where += 2;
	  fixP->fx_r_type = BFD_RELOC_390_12;
	}
      else if (operand->bits == 12 && operand->shift == 36)
	{
	  fixP->fx_size = 2;
	  fixP->fx_where += 4;
	  fixP->fx_r_type = BFD_RELOC_390_12;
	}
      else if (operand->bits == 20 && operand->shift == 20)
	{
	  fixP->fx_size = 2;
	  fixP->fx_where += 2;
	  fixP->fx_r_type = BFD_RELOC_390_20;
	}
      else if (operand->bits == 8 && operand->shift == 8)
	{
	  fixP->fx_size = 1;
	  fixP->fx_where += 1;
	  fixP->fx_r_type = BFD_RELOC_8;
	}
      else if (operand->bits == 16 && operand->shift == 16)
	{
	  fixP->fx_size = 2;
	  fixP->fx_where += 2;
	  if (operand->flags & S390_OPERAND_PCREL)
	    {
	      fixP->fx_r_type = BFD_RELOC_390_PC16DBL;
	      fixP->fx_offset += 2;
	    }
	  else
	    fixP->fx_r_type = BFD_RELOC_16;
	}
      else if (operand->bits == 32 && operand->shift == 16
	       && (operand->flags & S390_OPERAND_PCREL))
	{
	  fixP->fx_size = 4;
	  fixP->fx_where += 2;
	  fixP->fx_offset += 2;
	  fixP->fx_r_type = BFD_RELOC_390_PC32DBL;
	}
      else
	{
	  char *sfile;
	  unsigned int sline;

	  /* Use expr_symbol_where to see if this is an expression
	     symbol.  */
	  if (expr_symbol_where (fixP->fx_addsy, &sfile, &sline))
	    as_bad_where (fixP->fx_file, fixP->fx_line,
			  _("unresolved expression that must be resolved"));
	  else
	    as_bad_where (fixP->fx_file, fixP->fx_line,
			  _("unsupported relocation type"));
	  fixP->fx_done = 1;
	  return;
	}
    }
  else
    {
      switch (fixP->fx_r_type)
	{
	case BFD_RELOC_8:
	  if (fixP->fx_pcrel)
	    abort ();
	  if (fixP->fx_done)
	    md_number_to_chars (where, value, 1);
	  break;
	case BFD_RELOC_390_12:
	case BFD_RELOC_390_GOT12:
	case BFD_RELOC_390_GOTPLT12:
	  if (fixP->fx_done)
	    {
	      unsigned short mop;

	      mop = bfd_getb16 ((unsigned char *) where);
	      mop |= (unsigned short) (value & 0xfff);
	      bfd_putb16 ((bfd_vma) mop, (unsigned char *) where);
	    }
	  break;

	case BFD_RELOC_390_20:
	case BFD_RELOC_390_GOT20:
	case BFD_RELOC_390_GOTPLT20:
	  if (fixP->fx_done)
	    {
	      unsigned int mop;
	      mop = bfd_getb32 ((unsigned char *) where);
	      mop |= (unsigned int) ((value & 0xfff) << 8 |
				     (value & 0xff000) >> 12);
	      bfd_putb32 ((bfd_vma) mop, (unsigned char *) where);
	    } 
	  break;

	case BFD_RELOC_16:
	case BFD_RELOC_GPREL16:
	case BFD_RELOC_16_GOT_PCREL:
	case BFD_RELOC_16_GOTOFF:
	  if (fixP->fx_pcrel)
	    as_bad_where (fixP->fx_file, fixP->fx_line,
			  "cannot emit PC relative %s relocation%s%s",
			  bfd_get_reloc_code_name (fixP->fx_r_type),
			  fixP->fx_addsy != NULL ? " against " : "",
			  (fixP->fx_addsy != NULL
			   ? S_GET_NAME (fixP->fx_addsy)
			   : ""));
	  if (fixP->fx_done)
	    md_number_to_chars (where, value, 2);
	  break;
	case BFD_RELOC_390_GOT16:
	case BFD_RELOC_390_PLTOFF16:
	case BFD_RELOC_390_GOTPLT16:
	  if (fixP->fx_done)
	    md_number_to_chars (where, value, 2);
	  break;
	case BFD_RELOC_390_PC16DBL:
	case BFD_RELOC_390_PLT16DBL:
	  value += 2;
	  if (fixP->fx_done)
	    md_number_to_chars (where, (offsetT) value >> 1, 2);
	  break;

	case BFD_RELOC_32:
	  if (fixP->fx_pcrel)
	    fixP->fx_r_type = BFD_RELOC_32_PCREL;
	  else
	    fixP->fx_r_type = BFD_RELOC_32;
	  if (fixP->fx_done)
	    md_number_to_chars (where, value, 4);
	  break;
	case BFD_RELOC_32_PCREL:
	case BFD_RELOC_32_BASEREL:
	  fixP->fx_r_type = BFD_RELOC_32_PCREL;
	  if (fixP->fx_done)
	    md_number_to_chars (where, value, 4);
	  break;
	case BFD_RELOC_32_GOT_PCREL:
	case BFD_RELOC_390_PLTOFF32:
	case BFD_RELOC_390_PLT32:
	case BFD_RELOC_390_GOTPLT32:
	  if (fixP->fx_done)
	    md_number_to_chars (where, value, 4);
	  break;
	case BFD_RELOC_390_PC32DBL:
	case BFD_RELOC_390_PLT32DBL:
	case BFD_RELOC_390_GOTPCDBL:
	case BFD_RELOC_390_GOTENT:
	case BFD_RELOC_390_GOTPLTENT:
	  value += 2;
	  if (fixP->fx_done)
	    md_number_to_chars (where, (offsetT) value >> 1, 4);
	  break;

	case BFD_RELOC_32_GOTOFF:
	  if (fixP->fx_done)
	    md_number_to_chars (where, value, sizeof (int));
	  break;

	case BFD_RELOC_390_GOTOFF64:
	  if (fixP->fx_done)
	    md_number_to_chars (where, value, 8);
	  break;

	case BFD_RELOC_390_GOT64:
	case BFD_RELOC_390_PLTOFF64:
	case BFD_RELOC_390_PLT64:
	case BFD_RELOC_390_GOTPLT64:
	  if (fixP->fx_done)
	    md_number_to_chars (where, value, 8);
	  break;

	case BFD_RELOC_64:
	  if (fixP->fx_pcrel)
	    fixP->fx_r_type = BFD_RELOC_64_PCREL;
	  else
	    fixP->fx_r_type = BFD_RELOC_64;
	  if (fixP->fx_done)
	    md_number_to_chars (where, value, 8);
	  break;

	case BFD_RELOC_64_PCREL:
	  fixP->fx_r_type = BFD_RELOC_64_PCREL;
	  if (fixP->fx_done)
	    md_number_to_chars (where, value, 8);
	  break;

	case BFD_RELOC_VTABLE_INHERIT:
	case BFD_RELOC_VTABLE_ENTRY:
	  fixP->fx_done = 0;
	  return;

	case BFD_RELOC_390_TLS_LOAD:
	case BFD_RELOC_390_TLS_GDCALL:
	case BFD_RELOC_390_TLS_LDCALL:
	case BFD_RELOC_390_TLS_GD32:
	case BFD_RELOC_390_TLS_GD64:
	case BFD_RELOC_390_TLS_GOTIE12:
	case BFD_RELOC_390_TLS_GOTIE20:
	case BFD_RELOC_390_TLS_GOTIE32:
	case BFD_RELOC_390_TLS_GOTIE64:
	case BFD_RELOC_390_TLS_LDM32:
	case BFD_RELOC_390_TLS_LDM64:
	case BFD_RELOC_390_TLS_IE32:
	case BFD_RELOC_390_TLS_IE64:
	case BFD_RELOC_390_TLS_LE32:
	case BFD_RELOC_390_TLS_LE64:
	case BFD_RELOC_390_TLS_LDO32:
	case BFD_RELOC_390_TLS_LDO64:
	case BFD_RELOC_390_TLS_DTPMOD:
	case BFD_RELOC_390_TLS_DTPOFF:
	case BFD_RELOC_390_TLS_TPOFF:
	  S_SET_THREAD_LOCAL (fixP->fx_addsy);
	  /* Fully resolved at link time.  */
	  break;
	case BFD_RELOC_390_TLS_IEENT:
	  /* Fully resolved at link time.  */
	  S_SET_THREAD_LOCAL (fixP->fx_addsy);
	  value += 2;
	  break;

	default:
	  {
	    const char *reloc_name = bfd_get_reloc_code_name (fixP->fx_r_type);

	    if (reloc_name != NULL)
	      fprintf (stderr, "Gas failure, reloc type %s\n", reloc_name);
	    else
	      fprintf (stderr, "Gas failure, reloc type #%i\n", fixP->fx_r_type);
	    fflush (stderr);
	    abort ();
	  }
	}

      fixP->fx_offset = value;
    }
}

/* Generate a reloc for a fixup.  */

arelent *
tc_gen_reloc (seg, fixp)
     asection *seg ATTRIBUTE_UNUSED;
     fixS *fixp;
{
  bfd_reloc_code_real_type code;
  arelent *reloc;

  code = fixp->fx_r_type;
  if (GOT_symbol && fixp->fx_addsy == GOT_symbol)
    {
      if (   (s390_arch_size == 32 && code == BFD_RELOC_32_PCREL)
	  || (s390_arch_size == 64 && code == BFD_RELOC_64_PCREL))
	code = BFD_RELOC_390_GOTPC;
      if (code == BFD_RELOC_390_PC32DBL)
	code = BFD_RELOC_390_GOTPCDBL;
    }

  reloc = (arelent *) xmalloc (sizeof (arelent));
  reloc->sym_ptr_ptr = (asymbol **) xmalloc (sizeof (asymbol *));
  *reloc->sym_ptr_ptr = symbol_get_bfdsym (fixp->fx_addsy);
  reloc->address = fixp->fx_frag->fr_address + fixp->fx_where;
  reloc->howto = bfd_reloc_type_lookup (stdoutput, code);
  if (reloc->howto == NULL)
    {
      as_bad_where (fixp->fx_file, fixp->fx_line,
		    _("cannot represent relocation type %s"),
		    bfd_get_reloc_code_name (code));
      /* Set howto to a garbage value so that we can keep going.  */
      reloc->howto = bfd_reloc_type_lookup (stdoutput, BFD_RELOC_32);
      assert (reloc->howto != NULL);
    }
  reloc->addend = fixp->fx_offset;

  return reloc;
}

void
s390_cfi_frame_initial_instructions ()
{
  cfi_add_CFA_def_cfa (15, s390_arch_size == 64 ? 160 : 96);
}

int
tc_s390_regname_to_dw2regnum (char *regname)
{
  int regnum = -1;

  if (regname[0] != 'c' && regname[0] != 'a')
    {
      regnum = reg_name_search (pre_defined_registers, REG_NAME_CNT, regname);
      if (regname[0] == 'f' && regnum != -1)
        regnum += 16;
    }
  else if (strcmp (regname, "ap") == 0)
    regnum = 32;
  else if (strcmp (regname, "cc") == 0)
    regnum = 33;
  return regnum;
}
