/* tc-arc.c -- Assembler for the ARC
   Copyright 1994, 1995, 1997, 1999, 2000, 2001, 2002, 2003, 2004, 2005,
   2006  Free Software Foundation, Inc.
   Contributed by Doug Evans (dje@cygnus.com).

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
#include "struc-symbol.h"
#include "safe-ctype.h"
#include "subsegs.h"
#include "opcode/arc.h"
#include "../opcodes/arc-ext.h"
#include "elf/arc.h"
#include "dwarf2dbg.h"

const struct suffix_classes
{
  char *name;
  int  len;
} suffixclass[] =
{
  { "SUFFIX_COND|SUFFIX_FLAG",23 },
  { "SUFFIX_FLAG", 11 },
  { "SUFFIX_COND", 11 },
  { "SUFFIX_NONE", 11 }
};

#define MAXSUFFIXCLASS (sizeof (suffixclass) / sizeof (struct suffix_classes))

const struct syntax_classes
{
  char *name;
  int  len;
  int  class;
} syntaxclass[] =
{
  { "SYNTAX_3OP|OP1_MUST_BE_IMM", 26, SYNTAX_3OP|OP1_MUST_BE_IMM|SYNTAX_VALID },
  { "OP1_MUST_BE_IMM|SYNTAX_3OP", 26, OP1_MUST_BE_IMM|SYNTAX_3OP|SYNTAX_VALID },
  { "SYNTAX_2OP|OP1_IMM_IMPLIED", 26, SYNTAX_2OP|OP1_IMM_IMPLIED|SYNTAX_VALID },
  { "OP1_IMM_IMPLIED|SYNTAX_2OP", 26, OP1_IMM_IMPLIED|SYNTAX_2OP|SYNTAX_VALID },
  { "SYNTAX_3OP",                 10, SYNTAX_3OP|SYNTAX_VALID },
  { "SYNTAX_2OP",                 10, SYNTAX_2OP|SYNTAX_VALID }
};

#define MAXSYNTAXCLASS (sizeof (syntaxclass) / sizeof (struct syntax_classes))

/* This array holds the chars that always start a comment.  If the
   pre-processor is disabled, these aren't very useful.  */
const char comment_chars[] = "#;";

/* This array holds the chars that only start a comment at the beginning of
   a line.  If the line seems to have the form '# 123 filename'
   .line and .file directives will appear in the pre-processed output */
/* Note that input_file.c hand checks for '#' at the beginning of the
   first line of the input file.  This is because the compiler outputs
   #NO_APP at the beginning of its output.  */
/* Also note that comments started like this one will always
   work if '/' isn't otherwise defined.  */
const char line_comment_chars[] = "#";

const char line_separator_chars[] = "";

/* Chars that can be used to separate mant from exp in floating point nums.  */
const char EXP_CHARS[] = "eE";

/* Chars that mean this number is a floating point constant
   As in 0f12.456 or 0d1.2345e12.  */
const char FLT_CHARS[] = "rRsSfFdD";

/* Byte order.  */
extern int target_big_endian;
const char *arc_target_format = DEFAULT_TARGET_FORMAT;
static int byte_order = DEFAULT_BYTE_ORDER;

static segT arcext_section;

/* One of bfd_mach_arc_n.  */
static int arc_mach_type = bfd_mach_arc_6;

/* Non-zero if the cpu type has been explicitly specified.  */
static int mach_type_specified_p = 0;

/* Non-zero if opcode tables have been initialized.
   A .option command must appear before any instructions.  */
static int cpu_tables_init_p = 0;

static struct hash_control *arc_suffix_hash = NULL;

const char *md_shortopts = "";

enum options
{
  OPTION_EB = OPTION_MD_BASE,
  OPTION_EL,
  OPTION_ARC5,
  OPTION_ARC6,
  OPTION_ARC7,
  OPTION_ARC8,
  OPTION_ARC
};

struct option md_longopts[] =
{
  { "EB", no_argument, NULL, OPTION_EB },
  { "EL", no_argument, NULL, OPTION_EL },
  { "marc5", no_argument, NULL, OPTION_ARC5 },
  { "pre-v6", no_argument, NULL, OPTION_ARC5 },
  { "marc6", no_argument, NULL, OPTION_ARC6 },
  { "marc7", no_argument, NULL, OPTION_ARC7 },
  { "marc8", no_argument, NULL, OPTION_ARC8 },
  { "marc", no_argument, NULL, OPTION_ARC },
  { NULL, no_argument, NULL, 0 }
};
size_t md_longopts_size = sizeof (md_longopts);

#define IS_SYMBOL_OPERAND(o) \
 ((o) == 'b' || (o) == 'c' || (o) == 's' || (o) == 'o' || (o) == 'O')

struct arc_operand_value *get_ext_suffix (char *s);

/* Invocation line includes a switch not recognized by the base assembler.
   See if it's a processor-specific option.  */

int
md_parse_option (int c, char *arg ATTRIBUTE_UNUSED)
{
  switch (c)
    {
    case OPTION_ARC5:
      arc_mach_type = bfd_mach_arc_5;
      break;
    case OPTION_ARC:
    case OPTION_ARC6:
      arc_mach_type = bfd_mach_arc_6;
      break;
    case OPTION_ARC7:
      arc_mach_type = bfd_mach_arc_7;
      break;
    case OPTION_ARC8:
      arc_mach_type = bfd_mach_arc_8;
      break;
    case OPTION_EB:
      byte_order = BIG_ENDIAN;
      arc_target_format = "elf32-bigarc";
      break;
    case OPTION_EL:
      byte_order = LITTLE_ENDIAN;
      arc_target_format = "elf32-littlearc";
      break;
    default:
      return 0;
    }
  return 1;
}

void
md_show_usage (FILE *stream)
{
  fprintf (stream, "\
ARC Options:\n\
  -marc[5|6|7|8]          select processor variant (default arc%d)\n\
  -EB                     assemble code for a big endian cpu\n\
  -EL                     assemble code for a little endian cpu\n", arc_mach_type + 5);
}

/* This function is called once, at assembler startup time.  It should
   set up all the tables, etc. that the MD part of the assembler will need.
   Opcode selection is deferred until later because we might see a .option
   command.  */

void
md_begin (void)
{
  /* The endianness can be chosen "at the factory".  */
  target_big_endian = byte_order == BIG_ENDIAN;

  if (!bfd_set_arch_mach (stdoutput, bfd_arch_arc, arc_mach_type))
    as_warn ("could not set architecture and machine");

  /* This call is necessary because we need to initialize `arc_operand_map'
     which may be needed before we see the first insn.  */
  arc_opcode_init_tables (arc_get_opcode_mach (arc_mach_type,
					       target_big_endian));
}

/* Initialize the various opcode and operand tables.
   MACH is one of bfd_mach_arc_xxx.  */

static void
init_opcode_tables (int mach)
{
  int i;
  char *last;

  if ((arc_suffix_hash = hash_new ()) == NULL)
    as_fatal ("virtual memory exhausted");

  if (!bfd_set_arch_mach (stdoutput, bfd_arch_arc, mach))
    as_warn ("could not set architecture and machine");

  /* This initializes a few things in arc-opc.c that we need.
     This must be called before the various arc_xxx_supported fns.  */
  arc_opcode_init_tables (arc_get_opcode_mach (mach, target_big_endian));

  /* Only put the first entry of each equivalently named suffix in the
     table.  */
  last = "";
  for (i = 0; i < arc_suffixes_count; i++)
    {
      if (strcmp (arc_suffixes[i].name, last) != 0)
	hash_insert (arc_suffix_hash, arc_suffixes[i].name, (void *) (arc_suffixes + i));
      last = arc_suffixes[i].name;
    }

  /* Since registers don't have a prefix, we put them in the symbol table so
     they can't be used as symbols.  This also simplifies argument parsing as
     we can let gas parse registers for us.  The recorded register number is
     the address of the register's entry in arc_reg_names.

     If the register name is already in the table, then the existing
     definition is assumed to be from an .ExtCoreRegister pseudo-op.  */

  for (i = 0; i < arc_reg_names_count; i++)
    {
      if (symbol_find (arc_reg_names[i].name))
	continue;
      /* Use symbol_create here instead of symbol_new so we don't try to
	 output registers into the object file's symbol table.  */
      symbol_table_insert (symbol_create (arc_reg_names[i].name,
					  reg_section,
					  (valueT) &arc_reg_names[i],
					  &zero_address_frag));
    }

  /* Tell `.option' it's too late.  */
  cpu_tables_init_p = 1;
}

/* Insert an operand value into an instruction.
   If REG is non-NULL, it is a register number and ignore VAL.  */

static arc_insn
arc_insert_operand (arc_insn insn,
		    const struct arc_operand *operand,
		    int mods,
		    const struct arc_operand_value *reg,
		    offsetT val,
		    char *file,
		    unsigned int line)
{
  if (operand->bits != 32)
    {
      long min, max;
      offsetT test;

      if ((operand->flags & ARC_OPERAND_SIGNED) != 0)
	{
	  if ((operand->flags & ARC_OPERAND_SIGNOPT) != 0)
	    max = (1 << operand->bits) - 1;
	  else
	    max = (1 << (operand->bits - 1)) - 1;
	  min = - (1 << (operand->bits - 1));
	}
      else
	{
	  max = (1 << operand->bits) - 1;
	  min = 0;
	}

      if ((operand->flags & ARC_OPERAND_NEGATIVE) != 0)
	test = - val;
      else
	test = val;

      if (test < (offsetT) min || test > (offsetT) max)
	as_warn_value_out_of_range (_("operand"), test, (offsetT) min, (offsetT) max, file, line);
    }

  if (operand->insert)
    {
      const char *errmsg;

      errmsg = NULL;
      insn = (*operand->insert) (insn, operand, mods, reg, (long) val, &errmsg);
      if (errmsg != (const char *) NULL)
	as_warn (errmsg);
    }
  else
    insn |= (((long) val & ((1 << operand->bits) - 1))
	     << operand->shift);

  return insn;
}

/* We need to keep a list of fixups.  We can't simply generate them as
   we go, because that would require us to first create the frag, and
   that would screw up references to ``.''.  */

struct arc_fixup
{
  /* index into `arc_operands'  */
  int opindex;
  expressionS exp;
};

#define MAX_FIXUPS 5

#define MAX_SUFFIXES 5

/* Compute the reloc type of an expression.
   The possibly modified expression is stored in EXPNEW.

   This is used to convert the expressions generated by the %-op's into
   the appropriate operand type.  It is called for both data in instructions
   (operands) and data outside instructions (variables, debugging info, etc.).

   Currently supported %-ops:

   %st(symbol): represented as "symbol >> 2"
                "st" is short for STatus as in the status register (pc)

   DEFAULT_TYPE is the type to use if no special processing is required.

   DATA_P is non-zero for data or limm values, zero for insn operands.
   Remember that the opcode "insertion fns" cannot be used on data, they're
   only for inserting operands into insns.  They also can't be used for limm
   values as the insertion routines don't handle limm values.  When called for
   insns we return fudged reloc types (real_value - BFD_RELOC_UNUSED).  When
   called for data or limm values we use real reloc types.  */

static int
get_arc_exp_reloc_type (int data_p,
			int default_type,
			expressionS *exp,
			expressionS *expnew)
{
  /* If the expression is "symbol >> 2" we must change it to just "symbol",
     as fix_new_exp can't handle it.  Similarly for (symbol - symbol) >> 2.
     That's ok though.  What's really going on here is that we're using
     ">> 2" as a special syntax for specifying BFD_RELOC_ARC_B26.  */

  if (exp->X_op == O_right_shift
      && exp->X_op_symbol != NULL
      && exp->X_op_symbol->sy_value.X_op == O_constant
      && exp->X_op_symbol->sy_value.X_add_number == 2
      && exp->X_add_number == 0)
    {
      if (exp->X_add_symbol != NULL
	  && (exp->X_add_symbol->sy_value.X_op == O_constant
	      || exp->X_add_symbol->sy_value.X_op == O_symbol))
	{
	  *expnew = *exp;
	  expnew->X_op = O_symbol;
	  expnew->X_op_symbol = NULL;
	  return data_p ? BFD_RELOC_ARC_B26 : arc_operand_map['J'];
	}
      else if (exp->X_add_symbol != NULL
	       && exp->X_add_symbol->sy_value.X_op == O_subtract)
	{
	  *expnew = exp->X_add_symbol->sy_value;
	  return data_p ? BFD_RELOC_ARC_B26 : arc_operand_map['J'];
	}
    }

  *expnew = *exp;
  return default_type;
}

static int
arc_set_ext_seg (void)
{
  if (!arcext_section)
    {
      arcext_section = subseg_new (".arcextmap", 0);
      bfd_set_section_flags (stdoutput, arcext_section,
			     SEC_READONLY | SEC_HAS_CONTENTS);
    }
  else
    subseg_set (arcext_section, 0);
  return 1;
}

static void
arc_extoper (int opertype)
{
  char *name;
  char *mode;
  char c;
  char *p;
  int imode = 0;
  int number;
  struct arc_ext_operand_value *ext_oper;
  symbolS *symbolP;

  segT old_sec;
  int old_subsec;

  name = input_line_pointer;
  c = get_symbol_end ();
  name = xstrdup (name);

  p = name;
  while (*p)
    {
      *p = TOLOWER (*p);
      p++;
    }

  /* just after name is now '\0'  */
  p = input_line_pointer;
  *p = c;
  SKIP_WHITESPACE ();

  if (*input_line_pointer != ',')
    {
      as_bad ("expected comma after operand name");
      ignore_rest_of_line ();
      free (name);
      return;
    }

  input_line_pointer++;		/* skip ','  */
  number = get_absolute_expression ();

  if (number < 0)
    {
      as_bad ("negative operand number %d", number);
      ignore_rest_of_line ();
      free (name);
      return;
    }

  if (opertype)
    {
      SKIP_WHITESPACE ();

      if (*input_line_pointer != ',')
	{
	  as_bad ("expected comma after register-number");
	  ignore_rest_of_line ();
	  free (name);
	  return;
	}

      input_line_pointer++;		/* skip ','  */
      mode = input_line_pointer;

      if (!strncmp (mode, "r|w", 3))
	{
	  imode = 0;
	  input_line_pointer += 3;
	}
      else
	{
	  if (!strncmp (mode, "r", 1))
	    {
	      imode = ARC_REGISTER_READONLY;
	      input_line_pointer += 1;
	    }
	  else
	    {
	      if (strncmp (mode, "w", 1))
		{
		  as_bad ("invalid mode");
		  ignore_rest_of_line ();
		  free (name);
		  return;
		}
	      else
		{
		  imode = ARC_REGISTER_WRITEONLY;
		  input_line_pointer += 1;
		}
	    }
	}
      SKIP_WHITESPACE ();
      if (1 == opertype)
	{
	  if (*input_line_pointer != ',')
	    {
	      as_bad ("expected comma after register-mode");
	      ignore_rest_of_line ();
	      free (name);
	      return;
	    }

	  input_line_pointer++;		/* skip ','  */

	  if (!strncmp (input_line_pointer, "cannot_shortcut", 15))
	    {
	      imode |= arc_get_noshortcut_flag ();
	      input_line_pointer += 15;
	    }
	  else
	    {
	      if (strncmp (input_line_pointer, "can_shortcut", 12))
		{
		  as_bad ("shortcut designator invalid");
		  ignore_rest_of_line ();
		  free (name);
		  return;
		}
	      else
		{
		  input_line_pointer += 12;
		}
	    }
	}
    }

  if ((opertype == 1) && number > 60)
    {
      as_bad ("core register value (%d) too large", number);
      ignore_rest_of_line ();
      free (name);
      return;
    }

  if ((opertype == 0) && number > 31)
    {
      as_bad ("condition code value (%d) too large", number);
      ignore_rest_of_line ();
      free (name);
      return;
    }

  ext_oper = xmalloc (sizeof (struct arc_ext_operand_value));

  if (opertype)
    {
      /* If the symbol already exists, point it at the new definition.  */
      if ((symbolP = symbol_find (name)))
	{
	  if (S_GET_SEGMENT (symbolP) == reg_section)
	    S_SET_VALUE (symbolP, (valueT) &ext_oper->operand);
	  else
	    {
	      as_bad ("attempt to override symbol: %s", name);
	      ignore_rest_of_line ();
	      free (name);
	      free (ext_oper);
	      return;
	    }
	}
      else
	{
	  /* If its not there, add it.  */
	  symbol_table_insert (symbol_create (name, reg_section,
					      (valueT) &ext_oper->operand,
					      &zero_address_frag));
	}
    }

  ext_oper->operand.name  = name;
  ext_oper->operand.value = number;
  ext_oper->operand.type  = arc_operand_type (opertype);
  ext_oper->operand.flags = imode;

  ext_oper->next = arc_ext_operands;
  arc_ext_operands = ext_oper;

  /* OK, now that we know what this operand is, put a description in
     the arc extension section of the output file.  */

  old_sec    = now_seg;
  old_subsec = now_subseg;

  arc_set_ext_seg ();

  switch (opertype)
    {
    case 0:
      p = frag_more (1);
      *p = 3 + strlen (name) + 1;
      p = frag_more (1);
      *p = EXT_COND_CODE;
      p = frag_more (1);
      *p = number;
      p = frag_more (strlen (name) + 1);
      strcpy (p, name);
      break;
    case 1:
      p = frag_more (1);
      *p = 3 + strlen (name) + 1;
      p = frag_more (1);
      *p = EXT_CORE_REGISTER;
      p = frag_more (1);
      *p = number;
      p = frag_more (strlen (name) + 1);
      strcpy (p, name);
      break;
    case 2:
      p = frag_more (1);
      *p = 6 + strlen (name) + 1;
      p = frag_more (1);
      *p = EXT_AUX_REGISTER;
      p = frag_more (1);
      *p = number >> 24 & 0xff;
      p = frag_more (1);
      *p = number >> 16 & 0xff;
      p = frag_more (1);
      *p = number >>  8 & 0xff;
      p = frag_more (1);
      *p = number       & 0xff;
      p = frag_more (strlen (name) + 1);
      strcpy (p, name);
      break;
    default:
      as_bad ("invalid opertype");
      ignore_rest_of_line ();
      free (name);
      return;
      break;
    }

  subseg_set (old_sec, old_subsec);

  /* Enter all registers into the symbol table.  */

  demand_empty_rest_of_line ();
}

static void
arc_extinst (int ignore ATTRIBUTE_UNUSED)
{
  char syntax[129];
  char *name;
  char *p;
  char c;
  int suffixcode = -1;
  int opcode, subopcode;
  int i;
  int class = 0;
  int name_len;
  struct arc_opcode *ext_op;

  segT old_sec;
  int old_subsec;

  name = input_line_pointer;
  c = get_symbol_end ();
  name = xstrdup (name);
  strcpy (syntax, name);
  name_len = strlen (name);

  /* just after name is now '\0'  */
  p = input_line_pointer;
  *p = c;

  SKIP_WHITESPACE ();

  if (*input_line_pointer != ',')
    {
      as_bad ("expected comma after operand name");
      ignore_rest_of_line ();
      return;
    }

  input_line_pointer++;		/* skip ','  */
  opcode = get_absolute_expression ();

  SKIP_WHITESPACE ();

  if (*input_line_pointer != ',')
    {
      as_bad ("expected comma after opcode");
      ignore_rest_of_line ();
      return;
    }

  input_line_pointer++;		/* skip ','  */
  subopcode = get_absolute_expression ();

  if (subopcode < 0)
    {
      as_bad ("negative subopcode %d", subopcode);
      ignore_rest_of_line ();
      return;
    }

  if (subopcode)
    {
      if (3 != opcode)
	{
	  as_bad ("subcode value found when opcode not equal 0x03");
	  ignore_rest_of_line ();
	  return;
	}
      else
	{
	  if (subopcode < 0x09 || subopcode == 0x3f)
	    {
	      as_bad ("invalid subopcode %d", subopcode);
	      ignore_rest_of_line ();
	      return;
	    }
	}
    }

  SKIP_WHITESPACE ();

  if (*input_line_pointer != ',')
    {
      as_bad ("expected comma after subopcode");
      ignore_rest_of_line ();
      return;
    }

  input_line_pointer++;		/* skip ','  */

  for (i = 0; i < (int) MAXSUFFIXCLASS; i++)
    {
      if (!strncmp (suffixclass[i].name,input_line_pointer, suffixclass[i].len))
	{
	  suffixcode = i;
	  input_line_pointer += suffixclass[i].len;
	  break;
	}
    }

  if (-1 == suffixcode)
    {
      as_bad ("invalid suffix class");
      ignore_rest_of_line ();
      return;
    }

  SKIP_WHITESPACE ();

  if (*input_line_pointer != ',')
    {
      as_bad ("expected comma after suffix class");
      ignore_rest_of_line ();
      return;
    }

  input_line_pointer++;		/* skip ','  */

  for (i = 0; i < (int) MAXSYNTAXCLASS; i++)
    {
      if (!strncmp (syntaxclass[i].name,input_line_pointer, syntaxclass[i].len))
	{
	  class = syntaxclass[i].class;
	  input_line_pointer += syntaxclass[i].len;
	  break;
	}
    }

  if (0 == (SYNTAX_VALID & class))
    {
      as_bad ("invalid syntax class");
      ignore_rest_of_line ();
      return;
    }

  if ((0x3 == opcode) & (class & SYNTAX_3OP))
    {
      as_bad ("opcode 0x3 and SYNTAX_3OP invalid");
      ignore_rest_of_line ();
      return;
    }

  switch (suffixcode)
    {
    case 0:
      strcat (syntax, "%.q%.f ");
      break;
    case 1:
      strcat (syntax, "%.f ");
      break;
    case 2:
      strcat (syntax, "%.q ");
      break;
    case 3:
      strcat (syntax, " ");
      break;
    default:
      as_bad ("unknown suffix class");
      ignore_rest_of_line ();
      return;
      break;
    };

  strcat (syntax, ((opcode == 0x3) ? "%a,%b" : ((class & SYNTAX_3OP) ? "%a,%b,%c" : "%b,%c")));
  if (suffixcode < 2)
    strcat (syntax, "%F");
  strcat (syntax, "%S%L");

  ext_op = xmalloc (sizeof (struct arc_opcode));
  ext_op->syntax = xstrdup (syntax);

  ext_op->mask  = I (-1) | ((0x3 == opcode) ? C (-1) : 0);
  ext_op->value = I (opcode) | ((0x3 == opcode) ? C (subopcode) : 0);
  ext_op->flags = class;
  ext_op->next_asm = arc_ext_opcodes;
  ext_op->next_dis = arc_ext_opcodes;
  arc_ext_opcodes = ext_op;

  /* OK, now that we know what this inst is, put a description in the
     arc extension section of the output file.  */

  old_sec    = now_seg;
  old_subsec = now_subseg;

  arc_set_ext_seg ();

  p = frag_more (1);
  *p = 5 + name_len + 1;
  p = frag_more (1);
  *p = EXT_INSTRUCTION;
  p = frag_more (1);
  *p = opcode;
  p = frag_more (1);
  *p = subopcode;
  p = frag_more (1);
  *p = (class & (OP1_MUST_BE_IMM | OP1_IMM_IMPLIED) ? IGNORE_FIRST_OPD : 0);
  p = frag_more (name_len);
  strncpy (p, syntax, name_len);
  p = frag_more (1);
  *p = '\0';

  subseg_set (old_sec, old_subsec);

  demand_empty_rest_of_line ();
}

static void
arc_common (int localScope)
{
  char *name;
  char c;
  char *p;
  int align, size;
  symbolS *symbolP;

  name = input_line_pointer;
  c = get_symbol_end ();
  /* just after name is now '\0'  */
  p = input_line_pointer;
  *p = c;
  SKIP_WHITESPACE ();

  if (*input_line_pointer != ',')
    {
      as_bad ("expected comma after symbol name");
      ignore_rest_of_line ();
      return;
    }

  input_line_pointer++;		/* skip ','  */
  size = get_absolute_expression ();

  if (size < 0)
    {
      as_bad ("negative symbol length");
      ignore_rest_of_line ();
      return;
    }

  *p = 0;
  symbolP = symbol_find_or_make (name);
  *p = c;

  if (S_IS_DEFINED (symbolP) && ! S_IS_COMMON (symbolP))
    {
      as_bad ("ignoring attempt to re-define symbol");
      ignore_rest_of_line ();
      return;
    }
  if (((int) S_GET_VALUE (symbolP) != 0) \
      && ((int) S_GET_VALUE (symbolP) != size))
    {
      as_warn ("length of symbol \"%s\" already %ld, ignoring %d",
	       S_GET_NAME (symbolP), (long) S_GET_VALUE (symbolP), size);
    }
  assert (symbolP->sy_frag == &zero_address_frag);

  /* Now parse the alignment field.  This field is optional for
     local and global symbols. Default alignment is zero.  */
  if (*input_line_pointer == ',')
    {
      input_line_pointer++;
      align = get_absolute_expression ();
      if (align < 0)
	{
	  align = 0;
	  as_warn ("assuming symbol alignment of zero");
	}
    }
  else
    align = 0;

  if (localScope != 0)
    {
      segT old_sec;
      int old_subsec;
      char *pfrag;

      old_sec    = now_seg;
      old_subsec = now_subseg;
      record_alignment (bss_section, align);
      subseg_set (bss_section, 0);  /* ??? subseg_set (bss_section, 1); ???  */

      if (align)
	/* Do alignment.  */
	frag_align (align, 0, 0);

      /* Detach from old frag.  */
      if (S_GET_SEGMENT (symbolP) == bss_section)
	symbolP->sy_frag->fr_symbol = NULL;

      symbolP->sy_frag = frag_now;
      pfrag = frag_var (rs_org, 1, 1, (relax_substateT) 0, symbolP,
			(offsetT) size, (char *) 0);
      *pfrag = 0;

      S_SET_SIZE       (symbolP, size);
      S_SET_SEGMENT    (symbolP, bss_section);
      S_CLEAR_EXTERNAL (symbolP);
      symbolP->local = 1;
      subseg_set (old_sec, old_subsec);
    }
  else
    {
      S_SET_VALUE    (symbolP, (valueT) size);
      S_SET_ALIGN    (symbolP, align);
      S_SET_EXTERNAL (symbolP);
      S_SET_SEGMENT  (symbolP, bfd_com_section_ptr);
    }

  symbolP->bsym->flags |= BSF_OBJECT;

  demand_empty_rest_of_line ();
}

/* Select the cpu we're assembling for.  */

static void
arc_option (int ignore ATTRIBUTE_UNUSED)
{
  extern int arc_get_mach (char *);
  int mach;
  char c;
  char *cpu;

  cpu = input_line_pointer;
  c = get_symbol_end ();
  mach = arc_get_mach (cpu);
  *input_line_pointer = c;

  /* If an instruction has already been seen, it's too late.  */
  if (cpu_tables_init_p)
    {
      as_bad ("\".option\" directive must appear before any instructions");
      ignore_rest_of_line ();
      return;
    }

  if (mach == -1)
    goto bad_cpu;

  if (mach_type_specified_p && mach != arc_mach_type)
    {
      as_bad ("\".option\" directive conflicts with initial definition");
      ignore_rest_of_line ();
      return;
    }
  else
    {
      /* The cpu may have been selected on the command line.  */
      if (mach != arc_mach_type)
	as_warn ("\".option\" directive overrides command-line (default) value");
      arc_mach_type = mach;
      if (!bfd_set_arch_mach (stdoutput, bfd_arch_arc, mach))
	as_fatal ("could not set architecture and machine");
      mach_type_specified_p = 1;
    }
  demand_empty_rest_of_line ();
  return;

 bad_cpu:
  as_bad ("invalid identifier for \".option\"");
  ignore_rest_of_line ();
}

/* Turn a string in input_line_pointer into a floating point constant
   of type TYPE, and store the appropriate bytes in *LITP.  The number
   of LITTLENUMS emitted is stored in *SIZEP.  An error message is
   returned, or NULL on OK.  */

/* Equal to MAX_PRECISION in atof-ieee.c  */
#define MAX_LITTLENUMS 6

char *
md_atof (int type, char *litP, int *sizeP)
{
  int prec;
  LITTLENUM_TYPE words[MAX_LITTLENUMS];
  LITTLENUM_TYPE *wordP;
  char *t;

  switch (type)
    {
    case 'f':
    case 'F':
      prec = 2;
      break;

    case 'd':
    case 'D':
      prec = 4;
      break;

    default:
      *sizeP = 0;
      return "bad call to md_atof";
    }

  t = atof_ieee (input_line_pointer, type, words);
  if (t)
    input_line_pointer = t;
  *sizeP = prec * sizeof (LITTLENUM_TYPE);
  for (wordP = words; prec--;)
    {
      md_number_to_chars (litP, (valueT) (*wordP++), sizeof (LITTLENUM_TYPE));
      litP += sizeof (LITTLENUM_TYPE);
    }

  return NULL;
}

/* Write a value out to the object file, using the appropriate
   endianness.  */

void
md_number_to_chars (char *buf, valueT val, int n)
{
  if (target_big_endian)
    number_to_chars_bigendian (buf, val, n);
  else
    number_to_chars_littleendian (buf, val, n);
}

/* Round up a section size to the appropriate boundary.  */

valueT
md_section_align (segT segment, valueT size)
{
  int align = bfd_get_section_alignment (stdoutput, segment);

  return ((size + (1 << align) - 1) & (-1 << align));
}

/* We don't have any form of relaxing.  */

int
md_estimate_size_before_relax (fragS *fragp ATTRIBUTE_UNUSED,
			       asection *seg ATTRIBUTE_UNUSED)
{
  as_fatal (_("md_estimate_size_before_relax\n"));
  return 1;
}

/* Convert a machine dependent frag.  We never generate these.  */

void
md_convert_frag (bfd *abfd ATTRIBUTE_UNUSED,
		 asection *sec ATTRIBUTE_UNUSED,
		 fragS *fragp ATTRIBUTE_UNUSED)
{
  as_fatal (_("md_convert_frag\n"));
}

static void
arc_code_symbol (expressionS *expressionP)
{
  if (expressionP->X_op == O_symbol && expressionP->X_add_number == 0)
    {
      expressionS two;

      expressionP->X_op = O_right_shift;
      expressionP->X_add_symbol->sy_value.X_op = O_constant;
      two.X_op = O_constant;
      two.X_add_symbol = two.X_op_symbol = NULL;
      two.X_add_number = 2;
      expressionP->X_op_symbol = make_expr_symbol (&two);
    }
  /* Allow %st(sym1-sym2)  */
  else if (expressionP->X_op == O_subtract
	   && expressionP->X_add_symbol != NULL
	   && expressionP->X_op_symbol != NULL
	   && expressionP->X_add_number == 0)
    {
      expressionS two;

      expressionP->X_add_symbol = make_expr_symbol (expressionP);
      expressionP->X_op = O_right_shift;
      two.X_op = O_constant;
      two.X_add_symbol = two.X_op_symbol = NULL;
      two.X_add_number = 2;
      expressionP->X_op_symbol = make_expr_symbol (&two);
    }
  else
    as_bad ("expression too complex code symbol");
}

/* Parse an operand that is machine-specific.

   The ARC has a special %-op to adjust addresses so they're usable in
   branches.  The "st" is short for the STatus register.
   ??? Later expand this to take a flags value too.

   ??? We can't create new expression types so we map the %-op's onto the
   existing syntax.  This means that the user could use the chosen syntax
   to achieve the same effect.  */

void
md_operand (expressionS *expressionP)
{
  char *p = input_line_pointer;

  if (*p != '%')
    return;

  if (strncmp (p, "%st(", 4) == 0)
    {
      input_line_pointer += 4;
      expression (expressionP);
      if (*input_line_pointer != ')')
	{
	  as_bad ("missing ')' in %%-op");
	  return;
	}
      ++input_line_pointer;
      arc_code_symbol (expressionP);
    }
  else
    {
      /* It could be a register.  */
      int i, l;
      struct arc_ext_operand_value *ext_oper = arc_ext_operands;
      p++;

      while (ext_oper)
	{
	  l = strlen (ext_oper->operand.name);
	  if (!strncmp (p, ext_oper->operand.name, l) && !ISALNUM (*(p + l)))
	    {
	      input_line_pointer += l + 1;
	      expressionP->X_op = O_register;
	      expressionP->X_add_number = (offsetT) &ext_oper->operand;
	      return;
	    }
	  ext_oper = ext_oper->next;
	}
      for (i = 0; i < arc_reg_names_count; i++)
	{
	  l = strlen (arc_reg_names[i].name);
	  if (!strncmp (p, arc_reg_names[i].name, l) && !ISALNUM (*(p + l)))
	    {
	      input_line_pointer += l + 1;
	      expressionP->X_op = O_register;
	      expressionP->X_add_number = (offsetT) &arc_reg_names[i];
	      break;
	    }
	}
    }
}

/* We have no need to default values of symbols.
   We could catch register names here, but that is handled by inserting
   them all in the symbol table to begin with.  */

symbolS *
md_undefined_symbol (char *name ATTRIBUTE_UNUSED)
{
  return 0;
}

/* Functions concerning expressions.  */

/* Parse a .byte, .word, etc. expression.

   Values for the status register are specified with %st(label).
   `label' will be right shifted by 2.  */

void
arc_parse_cons_expression (expressionS *exp,
			   unsigned int nbytes ATTRIBUTE_UNUSED)
{
  char *p = input_line_pointer;
  int code_symbol_fix = 0;

  for (; ! is_end_of_line[(unsigned char) *p]; p++)
    if (*p == '@' && !strncmp (p, "@h30", 4))
      {
	code_symbol_fix = 1;
	strcpy (p, ";   ");
      }
  expression_and_evaluate (exp);
  if (code_symbol_fix)
    {
      arc_code_symbol (exp);
      input_line_pointer = p;
    }
}

/* Record a fixup for a cons expression.  */

void
arc_cons_fix_new (fragS *frag,
		  int where,
		  int nbytes,
		  expressionS *exp)
{
  if (nbytes == 4)
    {
      int reloc_type;
      expressionS exptmp;

      /* This may be a special ARC reloc (eg: %st()).  */
      reloc_type = get_arc_exp_reloc_type (1, BFD_RELOC_32, exp, &exptmp);
      fix_new_exp (frag, where, nbytes, &exptmp, 0, reloc_type);
    }
  else
    {
      fix_new_exp (frag, where, nbytes, exp, 0,
		   nbytes == 2 ? BFD_RELOC_16
		   : nbytes == 8 ? BFD_RELOC_64
		   : BFD_RELOC_32);
    }
}

/* Functions concerning relocs.  */

/* The location from which a PC relative jump should be calculated,
   given a PC relative reloc.  */

long
md_pcrel_from (fixS *fixP)
{
  /* Return the address of the delay slot.  */
  return fixP->fx_frag->fr_address + fixP->fx_where + fixP->fx_size;
}

/* Apply a fixup to the object code.  This is called for all the
   fixups we generated by the call to fix_new_exp, above.  In the call
   above we used a reloc code which was the largest legal reloc code
   plus the operand index.  Here we undo that to recover the operand
   index.  At this point all symbol values should be fully resolved,
   and we attempt to completely resolve the reloc.  If we can not do
   that, we determine the correct reloc code and put it back in the fixup.  */

void
md_apply_fix (fixS *fixP, valueT * valP, segT seg)
{
  valueT value = * valP;

  if (fixP->fx_addsy == (symbolS *) NULL)
    fixP->fx_done = 1;

  else if (fixP->fx_pcrel)
    {
      /* Hack around bfd_install_relocation brain damage.  */
      if (S_GET_SEGMENT (fixP->fx_addsy) != seg)
	value += md_pcrel_from (fixP);
    }

  /* We can't actually support subtracting a symbol.  */
  if (fixP->fx_subsy != NULL)
    as_bad_where (fixP->fx_file, fixP->fx_line, _("expression too complex"));

  if ((int) fixP->fx_r_type >= (int) BFD_RELOC_UNUSED)
    {
      int opindex;
      const struct arc_operand *operand;
      char *where;
      arc_insn insn;

      opindex = (int) fixP->fx_r_type - (int) BFD_RELOC_UNUSED;

      operand = &arc_operands[opindex];

      /* Fetch the instruction, insert the fully resolved operand
	 value, and stuff the instruction back again.  */
      where = fixP->fx_frag->fr_literal + fixP->fx_where;
      if (target_big_endian)
	insn = bfd_getb32 ((unsigned char *) where);
      else
	insn = bfd_getl32 ((unsigned char *) where);
      insn = arc_insert_operand (insn, operand, -1, NULL, (offsetT) value,
				 fixP->fx_file, fixP->fx_line);
      if (target_big_endian)
	bfd_putb32 ((bfd_vma) insn, (unsigned char *) where);
      else
	bfd_putl32 ((bfd_vma) insn, (unsigned char *) where);

      if (fixP->fx_done)
	/* Nothing else to do here.  */
	return;

      /* Determine a BFD reloc value based on the operand information.
	 We are only prepared to turn a few of the operands into relocs.
	 !!! Note that we can't handle limm values here.  Since we're using
	 implicit addends the addend must be inserted into the instruction,
	 however, the opcode insertion routines currently do nothing with
	 limm values.  */
      if (operand->fmt == 'B')
	{
	  assert ((operand->flags & ARC_OPERAND_RELATIVE_BRANCH) != 0
		  && operand->bits == 20
		  && operand->shift == 7);
	  fixP->fx_r_type = BFD_RELOC_ARC_B22_PCREL;
	}
      else if (operand->fmt == 'J')
	{
	  assert ((operand->flags & ARC_OPERAND_ABSOLUTE_BRANCH) != 0
		  && operand->bits == 24
		  && operand->shift == 32);
	  fixP->fx_r_type = BFD_RELOC_ARC_B26;
	}
      else if (operand->fmt == 'L')
	{
	  assert ((operand->flags & ARC_OPERAND_LIMM) != 0
		  && operand->bits == 32
		  && operand->shift == 32);
	  fixP->fx_r_type = BFD_RELOC_32;
	}
      else
	{
	  as_bad_where (fixP->fx_file, fixP->fx_line,
			"unresolved expression that must be resolved");
	  fixP->fx_done = 1;
	  return;
	}
    }
  else
    {
      switch (fixP->fx_r_type)
	{
	case BFD_RELOC_8:
	  md_number_to_chars (fixP->fx_frag->fr_literal + fixP->fx_where,
			      value, 1);
	  break;
	case BFD_RELOC_16:
	  md_number_to_chars (fixP->fx_frag->fr_literal + fixP->fx_where,
			      value, 2);
	  break;
	case BFD_RELOC_32:
	  md_number_to_chars (fixP->fx_frag->fr_literal + fixP->fx_where,
			      value, 4);
	  break;
	case BFD_RELOC_ARC_B26:
	  /* If !fixP->fx_done then `value' is an implicit addend.
	     We must shift it right by 2 in this case as well because the
	     linker performs the relocation and then adds this in (as opposed
	     to adding this in and then shifting right by 2).  */
	  value >>= 2;
	  md_number_to_chars (fixP->fx_frag->fr_literal + fixP->fx_where,
			      value, 4);
	  break;
	default:
	  abort ();
	}
    }
}

/* Translate internal representation of relocation info to BFD target
   format.  */

arelent *
tc_gen_reloc (asection *section ATTRIBUTE_UNUSED,
	      fixS *fixP)
{
  arelent *reloc;

  reloc = xmalloc (sizeof (arelent));
  reloc->sym_ptr_ptr = xmalloc (sizeof (asymbol *));

  *reloc->sym_ptr_ptr = symbol_get_bfdsym (fixP->fx_addsy);
  reloc->address = fixP->fx_frag->fr_address + fixP->fx_where;
  reloc->howto = bfd_reloc_type_lookup (stdoutput, fixP->fx_r_type);
  if (reloc->howto == (reloc_howto_type *) NULL)
    {
      as_bad_where (fixP->fx_file, fixP->fx_line,
		    "internal error: can't export reloc type %d (`%s')",
		    fixP->fx_r_type,
		    bfd_get_reloc_code_name (fixP->fx_r_type));
      return NULL;
    }

  assert (!fixP->fx_pcrel == !reloc->howto->pc_relative);

  /* Set addend to account for PC being advanced one insn before the
     target address is computed.  */

  reloc->addend = (fixP->fx_pcrel ? -4 : 0);

  return reloc;
}

const pseudo_typeS md_pseudo_table[] =
{
  { "align", s_align_bytes, 0 }, /* Defaulting is invalid (0).  */
  { "comm", arc_common, 0 },
  { "common", arc_common, 0 },
  { "lcomm", arc_common, 1 },
  { "lcommon", arc_common, 1 },
  { "2byte", cons, 2 },
  { "half", cons, 2 },
  { "short", cons, 2 },
  { "3byte", cons, 3 },
  { "4byte", cons, 4 },
  { "word", cons, 4 },
  { "option", arc_option, 0 },
  { "cpu", arc_option, 0 },
  { "block", s_space, 0 },
  { "extcondcode", arc_extoper, 0 },
  { "extcoreregister", arc_extoper, 1 },
  { "extauxregister", arc_extoper, 2 },
  { "extinstruction", arc_extinst, 0 },
  { NULL, 0, 0 },
};

/* This routine is called for each instruction to be assembled.  */

void
md_assemble (char *str)
{
  const struct arc_opcode *opcode;
  const struct arc_opcode *std_opcode;
  struct arc_opcode *ext_opcode;
  char *start;
  const char *last_errmsg = 0;
  arc_insn insn;
  static int init_tables_p = 0;

  /* Opcode table initialization is deferred until here because we have to
     wait for a possible .option command.  */
  if (!init_tables_p)
    {
      init_opcode_tables (arc_mach_type);
      init_tables_p = 1;
    }

  /* Skip leading white space.  */
  while (ISSPACE (*str))
    str++;

  /* The instructions are stored in lists hashed by the first letter (though
     we needn't care how they're hashed).  Get the first in the list.  */

  ext_opcode = arc_ext_opcodes;
  std_opcode = arc_opcode_lookup_asm (str);

  /* Keep looking until we find a match.  */
  start = str;
  for (opcode = (ext_opcode ? ext_opcode : std_opcode);
       opcode != NULL;
       opcode = (ARC_OPCODE_NEXT_ASM (opcode)
		 ? ARC_OPCODE_NEXT_ASM (opcode)
		 : (ext_opcode ? ext_opcode = NULL, std_opcode : NULL)))
    {
      int past_opcode_p, fc, num_suffixes;
      int fix_up_at = 0;
      char *syn;
      struct arc_fixup fixups[MAX_FIXUPS];
      /* Used as a sanity check.  If we need a limm reloc, make sure we ask
	 for an extra 4 bytes from frag_more.  */
      int limm_reloc_p;
      int ext_suffix_p;
      const struct arc_operand_value *insn_suffixes[MAX_SUFFIXES];

      /* Is this opcode supported by the selected cpu?  */
      if (! arc_opcode_supported (opcode))
	continue;

      /* Scan the syntax string.  If it doesn't match, try the next one.  */
      arc_opcode_init_insert ();
      insn = opcode->value;
      fc = 0;
      past_opcode_p = 0;
      num_suffixes = 0;
      limm_reloc_p = 0;
      ext_suffix_p = 0;

      /* We don't check for (*str != '\0') here because we want to parse
	 any trailing fake arguments in the syntax string.  */
      for (str = start, syn = opcode->syntax; *syn != '\0';)
	{
	  int mods;
	  const struct arc_operand *operand;

	  /* Non operand chars must match exactly.  */
	  if (*syn != '%' || *++syn == '%')
	    {
	     if (*str == *syn)
		{
		  if (*syn == ' ')
		    past_opcode_p = 1;
		  ++syn;
		  ++str;
		}
	      else
		break;
	      continue;
	    }

	  /* We have an operand.  Pick out any modifiers.  */
	  mods = 0;
	  while (ARC_MOD_P (arc_operands[arc_operand_map[(int) *syn]].flags))
	    {
	      mods |= arc_operands[arc_operand_map[(int) *syn]].flags & ARC_MOD_BITS;
	      ++syn;
	    }
	  operand = arc_operands + arc_operand_map[(int) *syn];
	  if (operand->fmt == 0)
	    as_fatal ("unknown syntax format character `%c'", *syn);

	  if (operand->flags & ARC_OPERAND_FAKE)
	    {
	      const char *errmsg = NULL;
	      if (operand->insert)
		{
		  insn = (*operand->insert) (insn, operand, mods, NULL, 0, &errmsg);
		  if (errmsg != (const char *) NULL)
		    {
		      last_errmsg = errmsg;
		      if (operand->flags & ARC_OPERAND_ERROR)
			{
			  as_bad (errmsg);
			  return;
			}
		      else if (operand->flags & ARC_OPERAND_WARN)
			as_warn (errmsg);
		      break;
		    }
		  if (limm_reloc_p
		      && (operand->flags && operand->flags & ARC_OPERAND_LIMM)
		      && (operand->flags &
			  (ARC_OPERAND_ABSOLUTE_BRANCH | ARC_OPERAND_ADDRESS)))
		    {
		      fixups[fix_up_at].opindex = arc_operand_map[operand->fmt];
		    }
		}
	      ++syn;
	    }
	  /* Are we finished with suffixes?  */
	  else if (!past_opcode_p)
	    {
	      int found;
	      char c;
	      char *s, *t;
	      const struct arc_operand_value *suf, *suffix_end;
	      const struct arc_operand_value *suffix = NULL;

	      if (!(operand->flags & ARC_OPERAND_SUFFIX))
		abort ();

	      /* If we're at a space in the input string, we want to skip the
		 remaining suffixes.  There may be some fake ones though, so
		 just go on to try the next one.  */
	      if (*str == ' ')
		{
		  ++syn;
		  continue;
		}

	      s = str;
	      if (mods & ARC_MOD_DOT)
		{
		  if (*s != '.')
		    break;
		  ++s;
		}
	      else
		{
		  /* This can happen in "b.nd foo" and we're currently looking
		     for "%q" (ie: a condition code suffix).  */
		  if (*s == '.')
		    {
		      ++syn;
		      continue;
		    }
		}

	      /* Pick the suffix out and look it up via the hash table.  */
	      for (t = s; *t && ISALNUM (*t); ++t)
		continue;
	      c = *t;
	      *t = '\0';
	      if ((suf = get_ext_suffix (s)))
		ext_suffix_p = 1;
	      else
		suf = hash_find (arc_suffix_hash, s);
	      if (!suf)
		{
		  /* This can happen in "blle foo" and we're currently using
		     the template "b%q%.n %j".  The "bl" insn occurs later in
		     the table so "lle" isn't an illegal suffix.  */
		  *t = c;
		  break;
		}

	      /* Is it the right type?  Note that the same character is used
		 several times, so we have to examine all of them.  This is
		 relatively efficient as equivalent entries are kept
		 together.  If it's not the right type, don't increment `str'
		 so we try the next one in the series.  */
	      found = 0;
	      if (ext_suffix_p && arc_operands[suf->type].fmt == *syn)
		{
		  /* Insert the suffix's value into the insn.  */
		  *t = c;
		  if (operand->insert)
		    insn = (*operand->insert) (insn, operand,
					       mods, NULL, suf->value,
					       NULL);
		  else
		    insn |= suf->value << operand->shift;
		  suffix = suf;
		  str = t;
		  found = 1;
		}
	      else
		{
		  *t = c;
		  suffix_end = arc_suffixes + arc_suffixes_count;
		  for (suffix = suf;
		       suffix < suffix_end && strcmp (suffix->name, suf->name) == 0;
		       ++suffix)
		    {
		      if (arc_operands[suffix->type].fmt == *syn)
			{
			  /* Insert the suffix's value into the insn.  */
			  if (operand->insert)
			    insn = (*operand->insert) (insn, operand,
						       mods, NULL, suffix->value,
						       NULL);
			  else
			    insn |= suffix->value << operand->shift;

			  str = t;
			  found = 1;
			  break;
			}
		    }
		}
	      ++syn;
	      if (!found)
		/* Wrong type.  Just go on to try next insn entry.  */
		;
	      else
		{
		  if (num_suffixes == MAX_SUFFIXES)
		    as_bad ("too many suffixes");
		  else
		    insn_suffixes[num_suffixes++] = suffix;
		}
	    }
	  else
	    /* This is either a register or an expression of some kind.  */
	    {
	      char *hold;
	      const struct arc_operand_value *reg = NULL;
	      long value = 0;
	      expressionS exp;

	      if (operand->flags & ARC_OPERAND_SUFFIX)
		abort ();

	      /* Is there anything left to parse?
		 We don't check for this at the top because we want to parse
		 any trailing fake arguments in the syntax string.  */
	      if (is_end_of_line[(unsigned char) *str])
		break;

	      /* Parse the operand.  */
	      hold = input_line_pointer;
	      input_line_pointer = str;
	      expression (&exp);
	      str = input_line_pointer;
	      input_line_pointer = hold;

	      if (exp.X_op == O_illegal)
		as_bad ("illegal operand");
	      else if (exp.X_op == O_absent)
		as_bad ("missing operand");
	      else if (exp.X_op == O_constant)
		value = exp.X_add_number;
	      else if (exp.X_op == O_register)
		reg = (struct arc_operand_value *) exp.X_add_number;
#define IS_REG_DEST_OPERAND(o) ((o) == 'a')
	      else if (IS_REG_DEST_OPERAND (*syn))
		as_bad ("symbol as destination register");
	      else
		{
		  if (!strncmp (str, "@h30", 4))
		    {
		      arc_code_symbol (&exp);
		      str += 4;
		    }
		  /* We need to generate a fixup for this expression.  */
		  if (fc >= MAX_FIXUPS)
		    as_fatal ("too many fixups");
		  fixups[fc].exp = exp;
		  /* We don't support shimm relocs. break here to force
		     the assembler to output a limm.  */
#define IS_REG_SHIMM_OFFSET(o) ((o) == 'd')
		  if (IS_REG_SHIMM_OFFSET (*syn))
		    break;
		  /* If this is a register constant (IE: one whose
		     register value gets stored as 61-63) then this
		     must be a limm.  */
		  /* ??? This bit could use some cleaning up.
		     Referencing the format chars like this goes
		     against style.  */
		  if (IS_SYMBOL_OPERAND (*syn))
		    {
		      const char *junk;
		      limm_reloc_p = 1;
		      /* Save this, we don't yet know what reloc to use.  */
		      fix_up_at = fc;
		      /* Tell insert_reg we need a limm.  This is
			 needed because the value at this point is
			 zero, a shimm.  */
		      /* ??? We need a cleaner interface than this.  */
		      (*arc_operands[arc_operand_map['Q']].insert)
			(insn, operand, mods, reg, 0L, &junk);
		    }
		  else
		    fixups[fc].opindex = arc_operand_map[(int) *syn];
		  ++fc;
		  value = 0;
		}

	      /* Insert the register or expression into the instruction.  */
	      if (operand->insert)
		{
		  const char *errmsg = NULL;
		  insn = (*operand->insert) (insn, operand, mods,
					     reg, (long) value, &errmsg);
		  if (errmsg != (const char *) NULL)
		    {
		      last_errmsg = errmsg;
		      if (operand->flags & ARC_OPERAND_ERROR)
			{
			  as_bad (errmsg);
			  return;
			}
		      else if (operand->flags & ARC_OPERAND_WARN)
			as_warn (errmsg);
		      break;
		    }
		}
	      else
		insn |= (value & ((1 << operand->bits) - 1)) << operand->shift;

	      ++syn;
	    }
	}

      /* If we're at the end of the syntax string, we're done.  */
      /* FIXME: try to move this to a separate function.  */
      if (*syn == '\0')
	{
	  int i;
	  char *f;
	  long limm, limm_p;

	  /* For the moment we assume a valid `str' can only contain blanks
	     now.  IE: We needn't try again with a longer version of the
	     insn and it is assumed that longer versions of insns appear
	     before shorter ones (eg: lsr r2,r3,1 vs lsr r2,r3).  */

	  while (ISSPACE (*str))
	    ++str;

	  if (!is_end_of_line[(unsigned char) *str])
	    as_bad ("junk at end of line: `%s'", str);

	  /* Is there a limm value?  */
	  limm_p = arc_opcode_limm_p (&limm);

	  /* Perform various error and warning tests.  */

	  {
	    static int in_delay_slot_p = 0;
	    static int prev_insn_needs_cc_nop_p = 0;
	    /* delay slot type seen */
	    int delay_slot_type = ARC_DELAY_NONE;
	    /* conditional execution flag seen */
	    int conditional = 0;
	    /* 1 if condition codes are being set */
	    int cc_set_p = 0;
	    /* 1 if conditional branch, including `b' "branch always" */
	    int cond_branch_p = opcode->flags & ARC_OPCODE_COND_BRANCH;

	    for (i = 0; i < num_suffixes; ++i)
	      {
		switch (arc_operands[insn_suffixes[i]->type].fmt)
		  {
		  case 'n':
		    delay_slot_type = insn_suffixes[i]->value;
		    break;
		  case 'q':
		    conditional = insn_suffixes[i]->value;
		    break;
		  case 'f':
		    cc_set_p = 1;
		    break;
		  }
	      }

	    /* Putting an insn with a limm value in a delay slot is supposed to
	       be legal, but let's warn the user anyway.  Ditto for 8 byte
	       jumps with delay slots.  */
	    if (in_delay_slot_p && limm_p)
	      as_warn ("8 byte instruction in delay slot");
	    if (delay_slot_type != ARC_DELAY_NONE
		&& limm_p && arc_insn_not_jl (insn)) /* except for jl  addr */
	      as_warn ("8 byte jump instruction with delay slot");
	    in_delay_slot_p = (delay_slot_type != ARC_DELAY_NONE) && !limm_p;

	    /* Warn when a conditional branch immediately follows a set of
	       the condition codes.  Note that this needn't be done if the
	       insn that sets the condition codes uses a limm.  */
	    if (cond_branch_p && conditional != 0 /* 0 = "always" */
		&& prev_insn_needs_cc_nop_p && arc_mach_type == bfd_mach_arc_5)
	      as_warn ("conditional branch follows set of flags");
	    prev_insn_needs_cc_nop_p =
	      /* FIXME: ??? not required:
		 (delay_slot_type != ARC_DELAY_NONE) &&  */
	      cc_set_p && !limm_p;
	  }

	  /* Write out the instruction.
	     It is important to fetch enough space in one call to `frag_more'.
	     We use (f - frag_now->fr_literal) to compute where we are and we
	     don't want frag_now to change between calls.  */
	  if (limm_p)
	    {
	      f = frag_more (8);
	      md_number_to_chars (f, insn, 4);
	      md_number_to_chars (f + 4, limm, 4);
	      dwarf2_emit_insn (8);
	    }
	  else if (limm_reloc_p)
	    /* We need a limm reloc, but the tables think we don't.  */
	    abort ();
	  else
	    {
	      f = frag_more (4);
	      md_number_to_chars (f, insn, 4);
	      dwarf2_emit_insn (4);
	    }

	  /* Create any fixups.  */
	  for (i = 0; i < fc; ++i)
	    {
	      int op_type, reloc_type;
	      expressionS exptmp;
	      const struct arc_operand *operand;

	      /* Create a fixup for this operand.
		 At this point we do not use a bfd_reloc_code_real_type for
		 operands residing in the insn, but instead just use the
		 operand index.  This lets us easily handle fixups for any
		 operand type, although that is admittedly not a very exciting
		 feature.  We pick a BFD reloc type in md_apply_fix.

		 Limm values (4 byte immediate "constants") must be treated
		 normally because they're not part of the actual insn word
		 and thus the insertion routines don't handle them.  */

	      if (arc_operands[fixups[i].opindex].flags & ARC_OPERAND_LIMM)
		{
		  /* Modify the fixup addend as required by the cpu.  */
		  fixups[i].exp.X_add_number += arc_limm_fixup_adjust (insn);
		  op_type = fixups[i].opindex;
		  /* FIXME: can we add this data to the operand table?  */
		  if (op_type == arc_operand_map['L']
		      || op_type == arc_operand_map['s']
		      || op_type == arc_operand_map['o']
		      || op_type == arc_operand_map['O'])
		    reloc_type = BFD_RELOC_32;
		  else if (op_type == arc_operand_map['J'])
		    reloc_type = BFD_RELOC_ARC_B26;
		  else
		    abort ();
		  reloc_type = get_arc_exp_reloc_type (1, reloc_type,
						       &fixups[i].exp,
						       &exptmp);
		}
	      else
		{
		  op_type = get_arc_exp_reloc_type (0, fixups[i].opindex,
						    &fixups[i].exp, &exptmp);
		  reloc_type = op_type + (int) BFD_RELOC_UNUSED;
		}
	      operand = &arc_operands[op_type];
	      fix_new_exp (frag_now,
			   ((f - frag_now->fr_literal)
			    + (operand->flags & ARC_OPERAND_LIMM ? 4 : 0)), 4,
			   &exptmp,
			   (operand->flags & ARC_OPERAND_RELATIVE_BRANCH) != 0,
			   (bfd_reloc_code_real_type) reloc_type);
	    }
	  return;
	}
    }

  if (NULL == last_errmsg)
    as_bad ("bad instruction `%s'", start);
  else
    as_bad (last_errmsg);
}
