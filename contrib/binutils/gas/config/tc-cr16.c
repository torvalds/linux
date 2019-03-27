/* tc-cr16.c -- Assembler code for the CR16 CPU core.
   Copyright 2007 Free Software Foundation, Inc.

   Contributed by M R Swami Reddy <MR.Swami.Reddy@nsc.com>

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
   along with GAS; see the file COPYING.  If not, write to the
   Free Software Foundation, 51 Franklin Street - Fifth Floor, Boston,
   MA 02110-1301, USA.  */

#include "as.h"
#include "safe-ctype.h"
#include "dwarf2dbg.h"
#include "opcode/cr16.h"
#include "elf/cr16.h"


/* Word is considered here as a 16-bit unsigned short int.  */
#define WORD_SHIFT  16

/* Register is 2-byte size.  */
#define REG_SIZE   2

/* Maximum size of a single instruction (in words).  */
#define INSN_MAX_SIZE   3

/* Maximum bits which may be set in a `mask16' operand.  */
#define MAX_REGS_IN_MASK16  8

/* Assign a number NUM, shifted by SHIFT bytes, into a location
   pointed by index BYTE of array 'output_opcode'.  */
#define CR16_PRINT(BYTE, NUM, SHIFT)   output_opcode[BYTE] |= (NUM << SHIFT)

/* Operand errors.  */
typedef enum
  {
    OP_LEGAL = 0,       /* Legal operand.  */
    OP_OUT_OF_RANGE,    /* Operand not within permitted range.  */
    OP_NOT_EVEN         /* Operand is Odd number, should be even.  */
  }
op_err;

/* Opcode mnemonics hash table.  */
static struct hash_control *cr16_inst_hash;
/* CR16 registers hash table.  */
static struct hash_control *reg_hash;
/* CR16 register pair hash table.  */
static struct hash_control *regp_hash;
/* CR16 processor registers hash table.  */
static struct hash_control *preg_hash;
/* CR16 processor registers 32 bit hash table.  */
static struct hash_control *pregp_hash;
/* Current instruction we're assembling.  */
const inst *instruction;


static int code_label = 0;

/* Global variables.  */

/* Array to hold an instruction encoding.  */
long output_opcode[2];

/* Nonzero means a relocatable symbol.  */
int relocatable;

/* A copy of the original instruction (used in error messages).  */
char ins_parse[MAX_INST_LEN];

/* The current processed argument number.  */
int cur_arg_num;

/* Generic assembler global variables which must be defined by all targets.  */

/* Characters which always start a comment.  */
const char comment_chars[] = "#";

/* Characters which start a comment at the beginning of a line.  */
const char line_comment_chars[] = "#";

/* This array holds machine specific line separator characters.  */
const char line_separator_chars[] = ";";

/* Chars that can be used to separate mant from exp in floating point nums.  */
const char EXP_CHARS[] = "eE";

/* Chars that mean this number is a floating point constant as in 0f12.456  */
const char FLT_CHARS[] = "f'";

/* Target-specific multicharacter options, not const-declared at usage.  */
const char *md_shortopts = "";
struct option md_longopts[] =
{
  {NULL, no_argument, NULL, 0}
};
size_t md_longopts_size = sizeof (md_longopts);

static void
l_cons (int nbytes)
{
  int c;
  expressionS exp;

#ifdef md_flush_pending_output
    md_flush_pending_output ();
#endif

  if (is_it_end_of_statement ())
    {
      demand_empty_rest_of_line ();
      return;
    }

#ifdef TC_ADDRESS_BYTES
  if (nbytes == 0)
    nbytes = TC_ADDRESS_BYTES ();
#endif

#ifdef md_cons_align
  md_cons_align (nbytes);
#endif

  c = 0;
  do
    {
      unsigned int bits_available = BITS_PER_CHAR * nbytes;
      char *hold = input_line_pointer;

      expression (&exp);

      if (*input_line_pointer == ':')
	{
	  /* Bitfields.  */
	  long value = 0;

	  for (;;)
	    {
	      unsigned long width;

	      if (*input_line_pointer != ':')
		{
		  input_line_pointer = hold;
		  break;
		}
	      if (exp.X_op == O_absent)
		{
		  as_warn (_("using a bit field width of zero"));
		  exp.X_add_number = 0;
		  exp.X_op = O_constant;
		}

	      if (exp.X_op != O_constant)
		{
		  *input_line_pointer = '\0';
		  as_bad (_("field width \"%s\" too complex for a bitfield"), hold);
		  *input_line_pointer = ':';
		  demand_empty_rest_of_line ();
		  return;
		}

	      if ((width = exp.X_add_number) >
		  (unsigned int)(BITS_PER_CHAR * nbytes))
		{
		  as_warn (_("field width %lu too big to fit in %d bytes: truncated to %d bits"), width, nbytes, (BITS_PER_CHAR * nbytes));
		  width = BITS_PER_CHAR * nbytes;
		}                   /* Too big.  */


	      if (width > bits_available)
		{
		  /* FIXME-SOMEDAY: backing up and reparsing is wasteful.  */
		  input_line_pointer = hold;
		  exp.X_add_number = value;
		  break;
		}

	      /* Skip ':'.  */
	      hold = ++input_line_pointer;

	      expression (&exp);
	      if (exp.X_op != O_constant)
		{
		  char cache = *input_line_pointer;

		  *input_line_pointer = '\0';
		  as_bad (_("field value \"%s\" too complex for a bitfield"), hold);
		  *input_line_pointer = cache;
		  demand_empty_rest_of_line ();
		  return;
		}

	      value |= ((~(-1 << width) & exp.X_add_number)
			<< ((BITS_PER_CHAR * nbytes) - bits_available));

	      if ((bits_available -= width) == 0
		  || is_it_end_of_statement ()
		  || *input_line_pointer != ',')
		break;

	      hold = ++input_line_pointer;
	      expression (&exp);
	    }

	  exp.X_add_number = value;
	  exp.X_op = O_constant;
	  exp.X_unsigned = 1;
	}

      if ((*(input_line_pointer) == '@') && (*(input_line_pointer +1) == 'c'))
	code_label = 1;
      emit_expr (&exp, (unsigned int) nbytes);
      ++c;
      if ((*(input_line_pointer) == '@') && (*(input_line_pointer +1) == 'c'))
	{
	  input_line_pointer +=3;
	  break;
	}
    }
  while ((*input_line_pointer++ == ','));

  /* Put terminator back into stream.  */
  input_line_pointer--;

  demand_empty_rest_of_line ();
}


/* This table describes all the machine specific pseudo-ops
   the assembler has to support.  The fields are:
   *** Pseudo-op name without dot.
   *** Function to call to execute this pseudo-op.
   *** Integer arg to pass to the function.  */

const pseudo_typeS md_pseudo_table[] =
{
  /* In CR16 machine, align is in bytes (not a ptwo boundary).  */
  {"align", s_align_bytes, 0},
  {"long", l_cons,  4 },
  {0, 0, 0}
};

/* CR16 relaxation table.  */
const relax_typeS md_relax_table[] =
{
  /* bCC  */
  {0xfa, -0x100, 2, 1},                 /*  8 */
  {0xfffe, -0x10000, 4, 2},             /* 16 */
  {0xfffffe, -0x1000000, 6, 0},         /* 24 */
};

/* Return the bit size for a given operand.  */

static int
get_opbits (operand_type op)
{
  if (op < MAX_OPRD)
    return cr16_optab[op].bit_size;

  return 0;
}

/* Return the argument type of a given operand.  */

static argtype
get_optype (operand_type op)
{
  if (op < MAX_OPRD)
    return cr16_optab[op].arg_type;
  else
    return nullargs;
}

/* Return the flags of a given operand.  */

static int
get_opflags (operand_type op)
{
  if (op < MAX_OPRD)
    return cr16_optab[op].flags;

  return 0;
}

/* Get the cc code.  */

static int
get_cc (char *cc_name)
{
   unsigned int i;

   for (i = 0; i < cr16_num_cc; i++)
     if (strcmp (cc_name, cr16_b_cond_tab[i]) == 0)
       return i;

   return -1;
}

/* Get the core processor register 'reg_name'.  */

static reg
get_register (char *reg_name)
{
  const reg_entry *reg;

  reg = (const reg_entry *) hash_find (reg_hash, reg_name);

  if (reg != NULL)
    return reg->value.reg_val;

  return nullregister;
}
/* Get the core processor register-pair 'reg_name'.  */

static reg
get_register_pair (char *reg_name)
{
  const reg_entry *reg;
  char tmp_rp[16]="\0";

  /* Add '(' and ')' to the reg pair, if its not present.  */
  if (reg_name[0] != '(') 
    {
      tmp_rp[0] = '(';
      strcat (tmp_rp, reg_name);
      strcat (tmp_rp,")");
      reg = (const reg_entry *) hash_find (regp_hash, tmp_rp);
    }
  else
    reg = (const reg_entry *) hash_find (regp_hash, reg_name);

  if (reg != NULL)
    return reg->value.reg_val;

  return nullregister;
} 

/* Get the index register 'reg_name'.  */

static reg
get_index_register (char *reg_name)
{
  const reg_entry *reg;

  reg = (const reg_entry *) hash_find (reg_hash, reg_name);

  if ((reg != NULL)
      && ((reg->value.reg_val == 12) || (reg->value.reg_val == 13)))
    return reg->value.reg_val;

  return nullregister;
}
/* Get the core processor index register-pair 'reg_name'.  */

static reg
get_index_register_pair (char *reg_name)
{
  const reg_entry *reg;

  reg = (const reg_entry *) hash_find (regp_hash, reg_name);

  if (reg != NULL)
    {
      if ((reg->value.reg_val != 1) || (reg->value.reg_val != 7)
	  || (reg->value.reg_val != 9) || (reg->value.reg_val > 10))
	return reg->value.reg_val;

      as_bad (_("Unknown register pair - index relative mode: `%d'"), reg->value.reg_val);
    }

  return nullregister;
}

/* Get the processor register 'preg_name'.  */

static preg
get_pregister (char *preg_name)
{
  const reg_entry *preg;

  preg = (const reg_entry *) hash_find (preg_hash, preg_name);

  if (preg != NULL)
    return preg->value.preg_val;

  return nullpregister;
}

/* Get the processor register 'preg_name 32 bit'.  */

static preg
get_pregisterp (char *preg_name)
{
  const reg_entry *preg;

  preg = (const reg_entry *) hash_find (pregp_hash, preg_name);

  if (preg != NULL)
    return preg->value.preg_val;

  return nullpregister;
}


/* Round up a section size to the appropriate boundary.  */

valueT
md_section_align (segT seg, valueT val)
{
  /* Round .text section to a multiple of 2.  */
  if (seg == text_section)
    return (val + 1) & ~1;
  return val;
}

/* Parse an operand that is machine-specific (remove '*').  */

void
md_operand (expressionS * exp)
{
  char c = *input_line_pointer;

  switch (c)
    {
    case '*':
      input_line_pointer++;
      expression (exp);
      break;
    default:
      break;
    }
}

/* Reset global variables before parsing a new instruction.  */

static void
reset_vars (char *op)
{
  cur_arg_num = relocatable = 0;
  memset (& output_opcode, '\0', sizeof (output_opcode));

  /* Save a copy of the original OP (used in error messages).  */
  strncpy (ins_parse, op, sizeof ins_parse - 1);
  ins_parse [sizeof ins_parse - 1] = 0;
}

/* This macro decides whether a particular reloc is an entry in a
   switch table.  It is used when relaxing, because the linker needs
   to know about all such entries so that it can adjust them if
   necessary.  */

#define SWITCH_TABLE(fix)                                  \
  (   (fix)->fx_addsy != NULL                              \
   && (fix)->fx_subsy != NULL                              \
   && S_GET_SEGMENT ((fix)->fx_addsy) ==                   \
      S_GET_SEGMENT ((fix)->fx_subsy)                      \
   && S_GET_SEGMENT (fix->fx_addsy) != undefined_section   \
   && (   (fix)->fx_r_type == BFD_RELOC_CR16_NUM8          \
       || (fix)->fx_r_type == BFD_RELOC_CR16_NUM16         \
       || (fix)->fx_r_type == BFD_RELOC_CR16_NUM32         \
       || (fix)->fx_r_type == BFD_RELOC_CR16_NUM32a))

/* See whether we need to force a relocation into the output file.
   This is used to force out switch and PC relative relocations when
   relaxing.  */

int
cr16_force_relocation (fixS *fix)
{
  /* REVISIT: Check if the "SWITCH_TABLE (fix)" should be added
     if (generic_force_reloc (fix) || SWITCH_TABLE (fix))  */
  if (generic_force_reloc (fix))
    return 1;

  return 0;
}

/* Record a fixup for a cons expression.  */

void
cr16_cons_fix_new (fragS *frag, int offset, int len, expressionS *exp)
{
  int rtype;
  switch (len)
    {
    default: rtype = BFD_RELOC_NONE; break;
    case 1: rtype = BFD_RELOC_CR16_NUM8 ; break;
    case 2: rtype = BFD_RELOC_CR16_NUM16; break;
    case 4:
      if (code_label)
	{
	  rtype = BFD_RELOC_CR16_NUM32a;
	  code_label = 0;
	}
      else
	rtype = BFD_RELOC_CR16_NUM32;
      break;
    }

  fix_new_exp (frag, offset, len, exp, 0, rtype);
}

/* Generate a relocation entry for a fixup.  */

arelent *
tc_gen_reloc (asection *section ATTRIBUTE_UNUSED, fixS * fixP)
{
  arelent * reloc;

  reloc = xmalloc (sizeof (arelent));
  reloc->sym_ptr_ptr  = xmalloc (sizeof (asymbol *));
  *reloc->sym_ptr_ptr = symbol_get_bfdsym (fixP->fx_addsy);
  reloc->address = fixP->fx_frag->fr_address + fixP->fx_where;
  reloc->addend = fixP->fx_offset;

  if (fixP->fx_subsy != NULL)
    {
      if (SWITCH_TABLE (fixP))
        {
          /* Keep the current difference in the addend.  */
          reloc->addend = (S_GET_VALUE (fixP->fx_addsy)
                           - S_GET_VALUE (fixP->fx_subsy) + fixP->fx_offset);

          switch (fixP->fx_r_type)
            {
	    case BFD_RELOC_CR16_NUM8:
	      fixP->fx_r_type = BFD_RELOC_CR16_NUM8;
	      break;
	    case BFD_RELOC_CR16_NUM16:
	      fixP->fx_r_type = BFD_RELOC_CR16_NUM16;
	      break;
	    case BFD_RELOC_CR16_NUM32:
	      fixP->fx_r_type = BFD_RELOC_CR16_NUM32;
	      break;
	    case BFD_RELOC_CR16_NUM32a:
	      fixP->fx_r_type = BFD_RELOC_CR16_NUM32a;
	      break;
	    default:
	      abort ();
	      break;
            }
        }
      else
        {
          /* We only resolve difference expressions in the same section.  */
          as_bad_where (fixP->fx_file, fixP->fx_line,
                        _("can't resolve `%s' {%s section} - `%s' {%s section}"),
                        fixP->fx_addsy ? S_GET_NAME (fixP->fx_addsy) : "0",
                        segment_name (fixP->fx_addsy
                                      ? S_GET_SEGMENT (fixP->fx_addsy)
                                      : absolute_section),
                        S_GET_NAME (fixP->fx_subsy),
                        segment_name (S_GET_SEGMENT (fixP->fx_addsy)));
        }
    }

  assert ((int) fixP->fx_r_type > 0);
  reloc->howto = bfd_reloc_type_lookup (stdoutput, fixP->fx_r_type);

  if (reloc->howto == NULL)
    {
      as_bad_where (fixP->fx_file, fixP->fx_line,
                    _("internal error: reloc %d (`%s') not supported by object file format"),
                    fixP->fx_r_type,
                    bfd_get_reloc_code_name (fixP->fx_r_type));
      return NULL;
    }
  assert (!fixP->fx_pcrel == !reloc->howto->pc_relative);

  return reloc;
}

/* Prepare machine-dependent frags for relaxation.  */

int
md_estimate_size_before_relax (fragS *fragp, asection *seg)
{
  /* If symbol is undefined or located in a different section,
     select the largest supported relocation.  */
  relax_substateT subtype;
  relax_substateT rlx_state[] = {0, 2};

  for (subtype = 0; subtype < ARRAY_SIZE (rlx_state); subtype += 2)
    {
      if (fragp->fr_subtype == rlx_state[subtype]
          && (!S_IS_DEFINED (fragp->fr_symbol)
              || seg != S_GET_SEGMENT (fragp->fr_symbol)))
        {
          fragp->fr_subtype = rlx_state[subtype + 1];
          break;
        }
    }

  if (fragp->fr_subtype >= ARRAY_SIZE (md_relax_table))
    abort ();

  return md_relax_table[fragp->fr_subtype].rlx_length;
}

void
md_convert_frag (bfd *abfd ATTRIBUTE_UNUSED, asection *sec, fragS *fragP)
{
  /* 'opcode' points to the start of the instruction, whether
     we need to change the instruction's fixed encoding.  */
  bfd_reloc_code_real_type reloc = BFD_RELOC_NONE;

  subseg_change (sec, 0);

  fix_new (fragP, fragP->fr_fix,
           bfd_get_reloc_size (bfd_reloc_type_lookup (stdoutput, reloc)),
           fragP->fr_symbol, fragP->fr_offset, 1, reloc);
  fragP->fr_var = 0;
  fragP->fr_fix += md_relax_table[fragP->fr_subtype].rlx_length;
}

/* Process machine-dependent command line options.  Called once for
   each option on the command line that the machine-independent part of
   GAS does not understand.  */

int
md_parse_option (int c ATTRIBUTE_UNUSED, char *arg ATTRIBUTE_UNUSED)
{
  return 0;
}

/* Machine-dependent usage-output.  */

void
md_show_usage (FILE *stream ATTRIBUTE_UNUSED)
{
  return;
}

/* Turn a string in input_line_pointer into a floating point constant
   of type TYPE, and store the appropriate bytes in *LITP.  The number
   of LITTLENUMS emitted is stored in *SIZEP.  An error message is
   returned, or NULL on OK.  */

char *
md_atof (int type, char *litP, int *sizeP)
{
  int prec;
  int i;
  LITTLENUM_TYPE words[4];
  char *t;

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
          md_number_to_chars (litP, (valueT) words[i], 2);
          litP += 2;
        }
    }
  else
    {
      for (i = 0; i < prec; i++)
        {
          md_number_to_chars (litP, (valueT) words[i], 2);
          litP += 2;
        }
    }

  return NULL;
}

/* Apply a fixS (fixup of an instruction or data that we didn't have
   enough info to complete immediately) to the data in a frag.
   Since linkrelax is nonzero and TC_LINKRELAX_FIXUP is defined to disable
   relaxation of debug sections, this function is called only when
   fixuping relocations of debug sections.  */

void
md_apply_fix (fixS *fixP, valueT *valP, segT seg)
{
  valueT val = * valP;
  char *buf = fixP->fx_frag->fr_literal + fixP->fx_where;
  fixP->fx_offset = 0;

  switch (fixP->fx_r_type)
    {
      case BFD_RELOC_CR16_NUM8:
        bfd_put_8 (stdoutput, (unsigned char) val, buf);
        break;
      case BFD_RELOC_CR16_NUM16:
        bfd_put_16 (stdoutput, val, buf);
        break;
      case BFD_RELOC_CR16_NUM32:
        bfd_put_32 (stdoutput, val, buf);
        break;
      case BFD_RELOC_CR16_NUM32a:
        bfd_put_32 (stdoutput, val, buf);
        break;
      default:
        /* We shouldn't ever get here because linkrelax is nonzero.  */
        abort ();
        break;
    }

  fixP->fx_done = 0;

  if (fixP->fx_addsy == NULL
      && fixP->fx_pcrel == 0)
    fixP->fx_done = 1;

  if (fixP->fx_pcrel == 1
      && fixP->fx_addsy != NULL
      && S_GET_SEGMENT (fixP->fx_addsy) == seg)
    fixP->fx_done = 1;
}

/* The location from which a PC relative jump should be calculated,
   given a PC relative reloc.  */

long
md_pcrel_from (fixS *fixp)
{
  return fixp->fx_frag->fr_address + fixp->fx_where;
}

static void
initialise_reg_hash_table (struct hash_control ** hash_table,
			   const reg_entry * register_table,
			   const unsigned int num_entries)
{
  const reg_entry * reg;
  const char *hashret;

  if ((* hash_table = hash_new ()) == NULL)
    as_fatal (_("Virtual memory exhausted"));

  for (reg = register_table;
       reg < (register_table + num_entries);
       reg++)
    {
      hashret = hash_insert (* hash_table, reg->name, (char *) reg);
      if (hashret)
	as_fatal (_("Internal Error:  Can't hash %s: %s"),
		  reg->name, hashret);
    }
}

/* This function is called once, at assembler startup time.  This should
   set up all the tables, etc that the MD part of the assembler needs.  */

void
md_begin (void)
{
  int i = 0;

  /* Set up a hash table for the instructions.  */
  if ((cr16_inst_hash = hash_new ()) == NULL)
    as_fatal (_("Virtual memory exhausted"));

  while (cr16_instruction[i].mnemonic != NULL)
    {
      const char *hashret;
      const char *mnemonic = cr16_instruction[i].mnemonic;

      hashret = hash_insert (cr16_inst_hash, mnemonic,
			     (char *)(cr16_instruction + i));

      if (hashret != NULL && *hashret != '\0')
        as_fatal (_("Can't hash `%s': %s\n"), cr16_instruction[i].mnemonic,
                  *hashret == 0 ? _("(unknown reason)") : hashret);

      /* Insert unique names into hash table.  The CR16 instruction set
         has many identical opcode names that have different opcodes based
         on the operands.  This hash table then provides a quick index to
         the first opcode with a particular name in the opcode table.  */
      do
        {
          ++i;
        }
      while (cr16_instruction[i].mnemonic != NULL
             && streq (cr16_instruction[i].mnemonic, mnemonic));
    }

  /* Initialize reg_hash hash table.  */
  initialise_reg_hash_table (& reg_hash, cr16_regtab, NUMREGS);
  /* Initialize regp_hash hash table.  */
  initialise_reg_hash_table (& regp_hash, cr16_regptab, NUMREGPS);
  /* Initialize preg_hash hash table.  */
  initialise_reg_hash_table (& preg_hash, cr16_pregtab, NUMPREGS);
  /* Initialize pregp_hash hash table.  */
  initialise_reg_hash_table (& pregp_hash, cr16_pregptab, NUMPREGPS);

  /*  Set linkrelax here to avoid fixups in most sections.  */
  linkrelax = 1;
}

/* Process constants (immediate/absolute)
   and labels (jump targets/Memory locations).  */

static void
process_label_constant (char *str, ins * cr16_ins)
{
  char *saved_input_line_pointer;
  int symbol_with_at = 0;
  int symbol_with_s = 0;
  int symbol_with_m = 0;
  int symbol_with_l = 0;
  argument *cur_arg = cr16_ins->arg + cur_arg_num;  /* Current argument.  */

  saved_input_line_pointer = input_line_pointer;
  input_line_pointer = str;

  expression (&cr16_ins->exp);

  switch (cr16_ins->exp.X_op)
    {
    case O_big:
    case O_absent:
      /* Missing or bad expr becomes absolute 0.  */
      as_bad (_("missing or invalid displacement expression `%s' taken as 0"),
	      str);
      cr16_ins->exp.X_op = O_constant;
      cr16_ins->exp.X_add_number = 0;
      cr16_ins->exp.X_add_symbol = NULL;
      cr16_ins->exp.X_op_symbol = NULL;
      /* Fall through.  */

    case O_constant:
      cur_arg->X_op = O_constant;
      cur_arg->constant = cr16_ins->exp.X_add_number;
      break;

    case O_symbol:
    case O_subtract:
    case O_add:
      cur_arg->X_op = O_symbol;
      cr16_ins->rtype = BFD_RELOC_NONE;
      relocatable = 1;

      if (strneq (input_line_pointer, "@c", 2))
	symbol_with_at = 1;

      if (strneq (input_line_pointer, "@l", 2)
	  || strneq (input_line_pointer, ":l", 2))
	symbol_with_l = 1;

      if (strneq (input_line_pointer, "@m", 2)
	  || strneq (input_line_pointer, ":m", 2))
	symbol_with_m = 1;

      if (strneq (input_line_pointer, "@s", 2)
	  || strneq (input_line_pointer, ":s", 2))
	symbol_with_s = 1;

      switch (cur_arg->type)
        {
	case arg_cr:
	  if (IS_INSN_TYPE (LD_STOR_INS) || IS_INSN_TYPE (CSTBIT_INS))
	    {
	      if (cur_arg->size == 20)
		cr16_ins->rtype = BFD_RELOC_CR16_REGREL20;
	      else
		cr16_ins->rtype = BFD_RELOC_CR16_REGREL20a;
	    }
	  break;

	case arg_crp:
	  if (IS_INSN_TYPE (LD_STOR_INS) || IS_INSN_TYPE (CSTBIT_INS))
	    switch (instruction->size)
	      {
	      case 1:
		switch (cur_arg->size)
		  {
		  case 0:
		    cr16_ins->rtype = BFD_RELOC_CR16_REGREL0;
		    break;
		  case 4:
		    if (IS_INSN_MNEMONIC ("loadb") || IS_INSN_MNEMONIC ("storb"))
		      cr16_ins->rtype = BFD_RELOC_CR16_REGREL4;
		    else
		      cr16_ins->rtype = BFD_RELOC_CR16_REGREL4a;
		    break;
		  default: break;
		  }
		break;
	      case 2:
		cr16_ins->rtype = BFD_RELOC_CR16_REGREL16;
		break;
	      case 3:
		if (cur_arg->size == 20)
		  cr16_ins->rtype = BFD_RELOC_CR16_REGREL20;
		else
		  cr16_ins->rtype = BFD_RELOC_CR16_REGREL20a;
		break;
	      default:
		break;
	      }
	  break;

	case arg_idxr:
	  if (IS_INSN_TYPE (LD_STOR_INS) || IS_INSN_TYPE (CSTBIT_INS))
	    cr16_ins->rtype = BFD_RELOC_CR16_REGREL20;
	  break;

	case arg_idxrp:
	  if (IS_INSN_TYPE (LD_STOR_INS) || IS_INSN_TYPE (CSTBIT_INS))
	    switch (instruction->size)
	      {
	      case 1: cr16_ins->rtype = BFD_RELOC_CR16_REGREL0; break;
	      case 2: cr16_ins->rtype = BFD_RELOC_CR16_REGREL14; break;
	      case 3: cr16_ins->rtype = BFD_RELOC_CR16_REGREL20; break;
	      default: break;
	      }
	  break;

	case arg_c:
	  if (IS_INSN_MNEMONIC ("bal"))
	    cr16_ins->rtype = BFD_RELOC_CR16_DISP24;
	  else if (IS_INSN_TYPE (BRANCH_INS))
	    {
	      if (symbol_with_s)
		cr16_ins->rtype = BFD_RELOC_CR16_DISP8;
	      else if (symbol_with_m)
		cr16_ins->rtype = BFD_RELOC_CR16_DISP16;
	      else
		cr16_ins->rtype = BFD_RELOC_CR16_DISP24;
	    }
	  else if (IS_INSN_TYPE (STOR_IMM_INS) || IS_INSN_TYPE (LD_STOR_INS)
		   || IS_INSN_TYPE (CSTBIT_INS))
	    {
	      if (symbol_with_s)
		as_bad (_("operand %d: illegal use expression: `%s`"), cur_arg_num + 1, str);
	      if (symbol_with_m)
		cr16_ins->rtype = BFD_RELOC_CR16_ABS20;
	      else /* Default to (symbol_with_l) */
		cr16_ins->rtype = BFD_RELOC_CR16_ABS24;
	    }
	  else if (IS_INSN_TYPE (BRANCH_NEQ_INS))
	    cr16_ins->rtype = BFD_RELOC_CR16_DISP4;
          break;

        case arg_ic:
          if (IS_INSN_TYPE (ARITH_INS))
            {
              if (symbol_with_s)
                cr16_ins->rtype = BFD_RELOC_CR16_IMM4;
              else if (symbol_with_m)
                cr16_ins->rtype = BFD_RELOC_CR16_IMM20;
              else if (symbol_with_at)
                cr16_ins->rtype = BFD_RELOC_CR16_IMM32a;
              else /* Default to (symbol_with_l) */
                cr16_ins->rtype = BFD_RELOC_CR16_IMM32;
            }
          else if (IS_INSN_TYPE (ARITH_BYTE_INS))
	    {
	      cr16_ins->rtype = BFD_RELOC_CR16_IMM16;
	    }
          break;
        default:
          break;
	}
      break;

    default:
      cur_arg->X_op = cr16_ins->exp.X_op;
      break;
    }

  input_line_pointer = saved_input_line_pointer;
  return;
}

/* Retrieve the opcode image of a given register.
   If the register is illegal for the current instruction,
   issue an error.  */

static int
getreg_image (reg r)
{
  const reg_entry *reg;
  char *reg_name;
  int is_procreg = 0; /* Nonzero means argument should be processor reg.  */

  /* Check whether the register is in registers table.  */
  if (r < MAX_REG)
    reg = cr16_regtab + r;
  else /* Register not found.  */
    {
      as_bad (_("Unknown register: `%d'"), r);
      return 0;
    }

  reg_name = reg->name;

/* Issue a error message when register is illegal.  */
#define IMAGE_ERR \
  as_bad (_("Illegal register (`%s') in Instruction: `%s'"), \
            reg_name, ins_parse);                            \
  break;

  switch (reg->type)
    {
    case CR16_R_REGTYPE:
      if (! is_procreg)
	return reg->image;
      else
	IMAGE_ERR;

    case CR16_P_REGTYPE:
      return reg->image;
      break;

    default:
      IMAGE_ERR;
    }

  return 0;
}

/* Parsing different types of operands
   -> constants             Immediate/Absolute/Relative numbers
   -> Labels                Relocatable symbols
   -> (reg pair base)       Register pair base
   -> (rbase)               Register base
   -> disp(rbase)           Register relative
   -> [rinx]disp(reg pair)  Register index with reg pair mode
   -> disp(rbase,ridx,scl)  Register index mode.  */

static void
set_operand (char *operand, ins * cr16_ins)
{
  char *operandS; /* Pointer to start of sub-opearand.  */
  char *operandE; /* Pointer to end of sub-opearand.  */

  argument *cur_arg = &cr16_ins->arg[cur_arg_num]; /* Current argument.  */

  /* Initialize pointers.  */
  operandS = operandE = operand;

  switch (cur_arg->type)
    {
    case arg_ic:    /* Case $0x18.  */
      operandS++;
    case arg_c:     /* Case 0x18.  */
      /* Set constant.  */
      process_label_constant (operandS, cr16_ins);

      if (cur_arg->type != arg_ic)
        cur_arg->type = arg_c;
      break;

    case arg_icr:   /* Case $0x18(r1).  */
      operandS++;
    case arg_cr:    /* Case 0x18(r1).   */
      /* Set displacement constant.  */
      while (*operandE != '(')
        operandE++;
      *operandE = '\0';
      process_label_constant (operandS, cr16_ins);
      operandS = operandE;
    case arg_rbase: /* Case (r1) or (r1,r0).  */
      operandS++;
      /* Set register base.  */
      while (*operandE != ')')
        operandE++;
      *operandE = '\0';
      if ((cur_arg->r = get_register (operandS)) == nullregister)
         as_bad (_("Illegal register `%s' in Instruction `%s'"),
              operandS, ins_parse);

      /* set the arg->rp, if reg is "r12" or "r13" or "14" or "15" */
      if ((cur_arg->type != arg_rbase)
	  && ((getreg_image (cur_arg->r) == 12)
	      || (getreg_image (cur_arg->r) == 13)
	      || (getreg_image (cur_arg->r) == 14)
	      || (getreg_image (cur_arg->r) == 15)))
         {
           cur_arg->type = arg_crp;
           cur_arg->rp = cur_arg->r;
         }
      break;

    case arg_crp:    /* Case 0x18(r1,r0).   */
      /* Set displacement constant.  */
      while (*operandE != '(')
        operandE++;
      *operandE = '\0';
      process_label_constant (operandS, cr16_ins);
      operandS = operandE;
      operandS++;
      /* Set register pair base.  */
      while (*operandE != ')')
        operandE++;
      *operandE = '\0';
      if ((cur_arg->rp = get_register_pair (operandS)) == nullregister)
         as_bad (_("Illegal register pair `%s' in Instruction `%s'"),
              operandS, ins_parse);
      break;

    case arg_idxr:
      /* Set register pair base.  */
      if ((strchr (operandS,'(') != NULL))
        {
         while ((*operandE != '(') && (! ISSPACE (*operandE)))
           operandE++;
         if ((cur_arg->rp = get_index_register_pair (operandE)) == nullregister)
              as_bad (_("Illegal register pair `%s' in Instruction `%s'"),
                            operandS, ins_parse);
         *operandE++ = '\0';
         cur_arg->type = arg_idxrp;
        }
      else
	cur_arg->rp = -1;

       operandE = operandS;
      /* Set displacement constant.  */
      while (*operandE != ']')
        operandE++;
      process_label_constant (++operandE, cr16_ins);
      *operandE++ = '\0';
      operandE = operandS;

      /* Set index register .  */
      operandS = strchr (operandE,'[');
      if (operandS != NULL)
        { /* Eliminate '[', detach from rest of operand.  */
          *operandS++ = '\0';

          operandE = strchr (operandS, ']');

          if (operandE == NULL)
            as_bad (_("unmatched '['"));
          else
            { /* Eliminate ']' and make sure it was the last thing
                 in the string.  */
              *operandE = '\0';
              if (*(operandE + 1) != '\0')
                as_bad (_("garbage after index spec ignored"));
            }
        }

      if ((cur_arg->i_r = get_index_register (operandS)) == nullregister)
        as_bad (_("Illegal register `%s' in Instruction `%s'"),
                operandS, ins_parse);
      *operandE = '\0';
      *operandS = '\0';
      break;

    default:
      break;
    }
}

/* Parse a single operand.
   operand - Current operand to parse.
   cr16_ins - Current assembled instruction.  */

static void
parse_operand (char *operand, ins * cr16_ins)
{
  int ret_val;
  argument *cur_arg = cr16_ins->arg + cur_arg_num; /* Current argument.  */

  /* Initialize the type to NULL before parsing.  */
  cur_arg->type = nullargs;

  /* Check whether this is a condition code .  */
  if ((IS_INSN_MNEMONIC ("b")) && ((ret_val = get_cc (operand)) != -1))
    {
      cur_arg->type = arg_cc;
      cur_arg->cc = ret_val;
      cur_arg->X_op = O_register;
      return;
    }

  /* Check whether this is a general processor register.  */
  if ((ret_val = get_register (operand)) != nullregister)
    {
      cur_arg->type = arg_r;
      cur_arg->r = ret_val;
      cur_arg->X_op = 0;
      return;
    }

  /* Check whether this is a general processor register pair.  */
  if ((operand[0] == '(')
      && ((ret_val = get_register_pair (operand)) != nullregister))
    {
      cur_arg->type = arg_rp;
      cur_arg->rp = ret_val;
      cur_arg->X_op = O_register;
      return;
    }

  /* Check whether the operand is a processor register.
     For "lprd" and "sprd" instruction, only 32 bit
     processor registers used.  */
  if (!(IS_INSN_MNEMONIC ("lprd") || (IS_INSN_MNEMONIC ("sprd")))
      && ((ret_val = get_pregister (operand)) != nullpregister))
    {
      cur_arg->type = arg_pr;
      cur_arg->pr = ret_val;
      cur_arg->X_op = O_register;
      return;
    }

  /* Check whether this is a processor register - 32 bit.  */
  if ((ret_val = get_pregisterp (operand)) != nullpregister)
    {
      cur_arg->type = arg_prp;
      cur_arg->prp = ret_val;
      cur_arg->X_op = O_register;
      return;
    }

  /* Deal with special characters.  */
  switch (operand[0])
    {
    case '$':
      if (strchr (operand, '(') != NULL)
	cur_arg->type = arg_icr;
      else
	cur_arg->type = arg_ic;
      goto set_params;
      break;

    case '(':
      cur_arg->type = arg_rbase;
      goto set_params;
      break;

    case '[':
      cur_arg->type = arg_idxr;
      goto set_params;
      break;

    default:
      break;
    }

  if (strchr (operand, '(') != NULL)
    {
      if (strchr (operand, ',') != NULL
          && (strchr (operand, ',') > strchr (operand, '(')))
        cur_arg->type = arg_crp;
      else
        cur_arg->type = arg_cr;
    }
  else
    cur_arg->type = arg_c;

/* Parse an operand according to its type.  */
 set_params:
  cur_arg->constant = 0;
  set_operand (operand, cr16_ins);
}

/* Parse the various operands. Each operand is then analyzed to fillup
   the fields in the cr16_ins data structure.  */

static void
parse_operands (ins * cr16_ins, char *operands)
{
  char *operandS;            /* Operands string.  */
  char *operandH, *operandT; /* Single operand head/tail pointers.  */
  int allocated = 0;         /* Indicates a new operands string was allocated.*/
  char *operand[MAX_OPERANDS];/* Separating the operands.  */
  int op_num = 0;             /* Current operand number we are parsing.  */
  int bracket_flag = 0;       /* Indicates a bracket '(' was found.  */
  int sq_bracket_flag = 0;    /* Indicates a square bracket '[' was found.  */

  /* Preprocess the list of registers, if necessary.  */
  operandS = operandH = operandT = operands;

  while (*operandT != '\0')
    {
      if (*operandT == ',' && bracket_flag != 1 && sq_bracket_flag != 1)
        {
          *operandT++ = '\0';
          operand[op_num++] = strdup (operandH);
          operandH = operandT;
          continue;
        }

      if (*operandT == ' ')
        as_bad (_("Illegal operands (whitespace): `%s'"), ins_parse);

      if (*operandT == '(')
        bracket_flag = 1;
      else if (*operandT == '[')
        sq_bracket_flag = 1;

      if (*operandT == ')')
        {
          if (bracket_flag)
            bracket_flag = 0;
          else
            as_fatal (_("Missing matching brackets : `%s'"), ins_parse);
        }
      else if (*operandT == ']')
        {
          if (sq_bracket_flag)
            sq_bracket_flag = 0;
          else
            as_fatal (_("Missing matching brackets : `%s'"), ins_parse);
        }

      if (bracket_flag == 1 && *operandT == ')')
        bracket_flag = 0;
      else if (sq_bracket_flag == 1 && *operandT == ']')
        sq_bracket_flag = 0;

      operandT++;
    }

  /* Adding the last operand.  */
  operand[op_num++] = strdup (operandH);
  cr16_ins->nargs = op_num;

  /* Verifying correct syntax of operands (all brackets should be closed).  */
  if (bracket_flag || sq_bracket_flag)
    as_fatal (_("Missing matching brackets : `%s'"), ins_parse);

  /* Now we parse each operand separately.  */
  for (op_num = 0; op_num < cr16_ins->nargs; op_num++)
    {
      cur_arg_num = op_num;
      parse_operand (operand[op_num], cr16_ins);
      free (operand[op_num]);
    }

  if (allocated)
    free (operandS);
}

/* Get the trap index in dispatch table, given its name.
   This routine is used by assembling the 'excp' instruction.  */

static int
gettrap (char *s)
{
  const trap_entry *trap;

  for (trap = cr16_traps; trap < (cr16_traps + NUMTRAPS); trap++)
    if (strcasecmp (trap->name, s) == 0)
      return trap->entry;

  /* To make compatable with CR16 4.1 tools, the below 3-lines of
   * code added. Refer: Development Tracker item #123 */
  for (trap = cr16_traps; trap < (cr16_traps + NUMTRAPS); trap++)
    if (trap->entry  == (unsigned int) atoi (s))
      return trap->entry;

  as_bad (_("Unknown exception: `%s'"), s);
  return 0;
}

/* Top level module where instruction parsing starts.
   cr16_ins - data structure holds some information.
   operands - holds the operands part of the whole instruction.  */

static void
parse_insn (ins *insn, char *operands)
{
  int i;

  /* Handle instructions with no operands.  */
  for (i = 0; cr16_no_op_insn[i] != NULL; i++)
  {
    if (streq (cr16_no_op_insn[i], instruction->mnemonic))
    {
      insn->nargs = 0;
      return;
    }
  }

  /* Handle 'excp' instructions.  */
  if (IS_INSN_MNEMONIC ("excp"))
    {
      insn->nargs = 1;
      insn->arg[0].type = arg_ic;
      insn->arg[0].constant = gettrap (operands);
      insn->arg[0].X_op = O_constant;
      return;
    }

  if (operands != NULL)
    parse_operands (insn, operands);
}

/* bCC instruction requires special handling.  */
static char *
get_b_cc (char * op)
{
  unsigned int i;
  char op1[5];

  for (i = 1; i < strlen (op); i++)
     op1[i-1] = op[i];

  op1[i-1] = '\0';

  for (i = 0; i < cr16_num_cc ; i++)
    if (streq (op1, cr16_b_cond_tab[i]))
      return (char *) cr16_b_cond_tab[i];

   return NULL;
}

/* bCC instruction requires special handling.  */
static int
is_bcc_insn (char * op)
{
  if (!(streq (op, "bal") || streq (op, "beq0b") || streq (op, "bnq0b")
	|| streq (op, "beq0w") || streq (op, "bnq0w")))
    if ((op[0] == 'b') && (get_b_cc (op) != NULL))
      return 1;
  return 0;
}

/* Cinv instruction requires special handling.  */

static int
check_cinv_options (char * operand)
{
  char *p = operand;
  int i_used = 0, u_used = 0, d_used = 0;

  while (*++p != ']')
    {
      if (*p == ',' || *p == ' ')
        continue;

      else if (*p == 'i')
        i_used = 1;
      else if (*p == 'u')
        u_used = 1;
      else if (*p == 'd')
        d_used = 1;
      else
        as_bad (_("Illegal `cinv' parameter: `%c'"), *p);
    }

  return 0;
}

/* Retrieve the opcode image of a given register pair.
   If the register is illegal for the current instruction,
   issue an error.  */

static int
getregp_image (reg r)
{
  const reg_entry *reg;
  char *reg_name;

  /* Check whether the register is in registers table.  */
  if (r < MAX_REG)
    reg = cr16_regptab + r;
  /* Register not found.  */
  else
    {
      as_bad (_("Unknown register pair: `%d'"), r);
      return 0;
    }

  reg_name = reg->name;

/* Issue a error message when register  pair is illegal.  */
#define RPAIR_IMAGE_ERR \
  as_bad (_("Illegal register pair (`%s') in Instruction: `%s'"), \
            reg_name, ins_parse);                                 \
  break;

  switch (reg->type)
    {
    case CR16_RP_REGTYPE:
      return reg->image;
    default:
      RPAIR_IMAGE_ERR;
    }

  return 0;
}

/* Retrieve the opcode image of a given index register pair.
   If the register is illegal for the current instruction,
   issue an error.  */

static int
getidxregp_image (reg r)
{
  const reg_entry *reg;
  char *reg_name;

  /* Check whether the register is in registers table.  */
  if (r < MAX_REG)
    reg = cr16_regptab + r;
  /* Register not found.  */
  else
    {
      as_bad (_("Unknown register pair: `%d'"), r);
      return 0;
    }

  reg_name = reg->name;

/* Issue a error message when register  pair is illegal.  */
#define IDX_RPAIR_IMAGE_ERR \
  as_bad (_("Illegal index register pair (`%s') in Instruction: `%s'"), \
            reg_name, ins_parse);                                       \

  if (reg->type == CR16_RP_REGTYPE)
    {
      switch (reg->image)
	{
	case 0:  return 0; break;
	case 2:  return 1; break;
	case 4:  return 2; break;
	case 6:  return 3; break;
	case 8:  return 4; break;
	case 10: return 5; break;
	case 3:  return 6; break;
	case 5:  return 7; break;
	default:
	  break;
	}
    }

  IDX_RPAIR_IMAGE_ERR;
  return 0;
}

/* Retrieve the opcode image of a given processort register.
   If the register is illegal for the current instruction,
   issue an error.  */
static int
getprocreg_image (reg r)
{
  const reg_entry *reg;
  char *reg_name;

  /* Check whether the register is in registers table.  */
  if (r < MAX_PREG)
    reg = &cr16_pregtab[r - MAX_REG];
  /* Register not found.  */
  else
    {
      as_bad (_("Unknown processor register : `%d'"), r);
      return 0;
    }

  reg_name = reg->name;

/* Issue a error message when register  pair is illegal.  */
#define PROCREG_IMAGE_ERR \
  as_bad (_("Illegal processor register (`%s') in Instruction: `%s'"), \
            reg_name, ins_parse);                                      \
  break;

  switch (reg->type)
    {
    case CR16_P_REGTYPE:
      return reg->image;
    default:
      PROCREG_IMAGE_ERR;
    }

  return 0;
}

/* Retrieve the opcode image of a given processort register.
   If the register is illegal for the current instruction,
   issue an error.  */
static int
getprocregp_image (reg r)
{
  const reg_entry *reg;
  char *reg_name;
  int pregptab_disp = 0;

  /* Check whether the register is in registers table.  */
  if (r < MAX_PREG)
    {
      r = r - MAX_REG;
      switch (r)
        {
	case 4: pregptab_disp = 1;  break;
	case 6: pregptab_disp = 2;  break;
	case 8:
	case 9:
	case 10:
	  pregptab_disp = 3;  break;
	case 12:
	  pregptab_disp = 4;  break;
	case 14:
	  pregptab_disp = 5;  break;
	default: break;
        }
      reg = &cr16_pregptab[r - pregptab_disp];
    }
  /* Register not found.  */
  else
    {
      as_bad (_("Unknown processor register (32 bit) : `%d'"), r);
      return 0;
    }

  reg_name = reg->name;

/* Issue a error message when register  pair is illegal.  */
#define PROCREGP_IMAGE_ERR \
  as_bad (_("Illegal 32 bit - processor register (`%s') in Instruction: `%s'"),\
            reg_name, ins_parse);                                              \
  break;

  switch (reg->type)
    {
    case CR16_P_REGTYPE:
      return reg->image;
    default:
      PROCREGP_IMAGE_ERR;
    }

  return 0;
}

/* Routine used to represent integer X using NBITS bits.  */

static long
getconstant (long x, int nbits)
{
  /* The following expression avoids overflow if
     'nbits' is the number of bits in 'bfd_vma'.  */
  return (x & ((((1 << (nbits - 1)) - 1) << 1) | 1));
}

/* Print a constant value to 'output_opcode':
   ARG holds the operand's type and value.
   SHIFT represents the location of the operand to be print into.
   NBITS determines the size (in bits) of the constant.  */

static void
print_constant (int nbits, int shift, argument *arg)
{
  unsigned long mask = 0;

  long constant = getconstant (arg->constant, nbits);

  switch (nbits)
    {
    case 32:
    case 28:
      /* mask the upper part of the constant, that is, the bits
	 going to the lowest byte of output_opcode[0].
	 The upper part of output_opcode[1] is always filled,
	 therefore it is always masked with 0xFFFF.  */
      mask = (1 << (nbits - 16)) - 1;
      /* Divide the constant between two consecutive words :
	 0        1         2         3
	 +---------+---------+---------+---------+
	 |         | X X X X | x X x X |         |
	 +---------+---------+---------+---------+
	 output_opcode[0]    output_opcode[1]     */

      CR16_PRINT (0, (constant >> WORD_SHIFT) & mask, 0);
      CR16_PRINT (1, (constant & 0xFFFF), WORD_SHIFT);
      break;

    case 21:
      if ((nbits == 21) && (IS_INSN_TYPE (LD_STOR_INS))) nbits = 20;
    case 24:
    case 22:
    case 20:
      /* mask the upper part of the constant, that is, the bits
	 going to the lowest byte of output_opcode[0].
	 The upper part of output_opcode[1] is always filled,
	 therefore it is always masked with 0xFFFF.  */
      mask = (1 << (nbits - 16)) - 1;
      /* Divide the constant between two consecutive words :
	 0        1         2          3
	 +---------+---------+---------+---------+
	 |         | X X X X | - X - X |         |
	 +---------+---------+---------+---------+
	 output_opcode[0]    output_opcode[1]     */

      if ((instruction->size > 2) && (shift == WORD_SHIFT))
	{
	  if (arg->type == arg_idxrp)
	    {
	      CR16_PRINT (0, ((constant >> WORD_SHIFT) & mask) << 8, 0);
	      CR16_PRINT (1, (constant & 0xFFFF), WORD_SHIFT);
	    }
	  else
	    {
	      CR16_PRINT (0, (((((constant >> WORD_SHIFT) & mask) << 8) & 0x0f00) | ((((constant >> WORD_SHIFT) & mask) >> 4) & 0xf)),0);
	      CR16_PRINT (1, (constant & 0xFFFF), WORD_SHIFT);
	    }
	}
      else
	CR16_PRINT (0, constant, shift);
      break;

    case 14:
      if (arg->type == arg_idxrp)
	{
	  if (instruction->size == 2)
	    {
	      CR16_PRINT (0, ((constant)&0xf), shift);         // 0-3 bits
	      CR16_PRINT (0, ((constant>>4)&0x3), (shift+20)); // 4-5 bits
	      CR16_PRINT (0, ((constant>>6)&0x3), (shift+14)); // 6-7 bits
	      CR16_PRINT (0, ((constant>>8)&0x3f), (shift+8)); // 8-13 bits
	    }
	  else
	    CR16_PRINT (0, constant, shift);
	}
      break;

    case 16:
    case 12:
      /* When instruction size is 3 and 'shift' is 16, a 16-bit constant is
	 always filling the upper part of output_opcode[1]. If we mistakenly
	 write it to output_opcode[0], the constant prefix (that is, 'match')
	 will be overriden.
	 0        1         2         3
	 +---------+---------+---------+---------+
	 | 'match' |         | X X X X |         |
	 +---------+---------+---------+---------+
	 output_opcode[0]    output_opcode[1]     */

      if ((instruction->size > 2) && (shift == WORD_SHIFT))
	CR16_PRINT (1, constant, WORD_SHIFT);
      else
	CR16_PRINT (0, constant, shift);
      break;

    case 8:
      CR16_PRINT (0, ((constant/2)&0xf), shift);
      CR16_PRINT (0, ((constant/2)>>4),  (shift+8));
      break;

    default:
      CR16_PRINT (0, constant,  shift);
      break;
    }
}

/* Print an operand to 'output_opcode', which later on will be
   printed to the object file:
   ARG holds the operand's type, size and value.
   SHIFT represents the printing location of operand.
   NBITS determines the size (in bits) of a constant operand.  */

static void
print_operand (int nbits, int shift, argument *arg)
{
  switch (arg->type)
    {
    case arg_cc:
      CR16_PRINT (0, arg->cc, shift);
      break;

    case arg_r:
      CR16_PRINT (0, getreg_image (arg->r), shift);
      break;

    case arg_rp:
      CR16_PRINT (0, getregp_image (arg->rp), shift);
      break;

    case arg_pr:
      CR16_PRINT (0, getprocreg_image (arg->pr), shift);
      break;

    case arg_prp:
      CR16_PRINT (0, getprocregp_image (arg->prp), shift);
      break;

    case arg_idxrp:
      /*    16      12      8    6      0
            +-----------------------------+
            | r_index | disp  | rp_base   |
            +-----------------------------+          */

      if (instruction->size == 3)
	{
	  CR16_PRINT (0, getidxregp_image (arg->rp), 0);
	  if (getreg_image (arg->i_r) == 12)
	    CR16_PRINT (0, 0, 3);
	  else
	    CR16_PRINT (0, 1, 3);
	}
      else
	{
	  CR16_PRINT (0, getidxregp_image (arg->rp), 16);
	  if (getreg_image (arg->i_r) == 12)
	    CR16_PRINT (0, 0, 19);
	  else
	    CR16_PRINT (0, 1, 19);
	}
      print_constant (nbits, shift, arg);
      break;

    case arg_idxr:
      if (getreg_image (arg->i_r) == 12)
	if (IS_INSN_MNEMONIC ("cbitb") || IS_INSN_MNEMONIC ("sbitb")
	    || IS_INSN_MNEMONIC ("tbitb"))
	  CR16_PRINT (0, 0, 23);
	else CR16_PRINT (0, 0, 24);
      else
	if (IS_INSN_MNEMONIC ("cbitb") || IS_INSN_MNEMONIC ("sbitb")
	    || IS_INSN_MNEMONIC ("tbitb"))
	  CR16_PRINT (0, 1, 23);
	else CR16_PRINT (0, 1, 24);

      print_constant (nbits, shift, arg);
      break;

    case arg_ic:
    case arg_c:
      print_constant (nbits, shift, arg);
      break;

    case arg_rbase:
      CR16_PRINT (0, getreg_image (arg->r), shift);
      break;

    case arg_cr:
      print_constant (nbits, shift , arg);
      /* Add the register argument to the output_opcode.  */
      CR16_PRINT (0, getreg_image (arg->r), (shift+16));
      break;

    case arg_crp:
      print_constant (nbits, shift , arg);
      if (instruction->size > 1)
	CR16_PRINT (0, getregp_image (arg->rp), (shift + 16));
      else if (IS_INSN_TYPE (LD_STOR_INS) || (IS_INSN_TYPE (CSTBIT_INS)))
	{
	  if (instruction->size == 2)
	    CR16_PRINT (0, getregp_image (arg->rp), (shift - 8));
	  else if (instruction->size == 1)
	    CR16_PRINT (0, getregp_image (arg->rp), 16);
	}
      else
	CR16_PRINT (0, getregp_image (arg->rp), shift);
      break;

    default:
      break;
    }
}

/* Retrieve the number of operands for the current assembled instruction.  */

static int
get_number_of_operands (void)
{
  int i;

  for (i = 0; instruction->operands[i].op_type && i < MAX_OPERANDS; i++)
    ;
  return i;
}

/* Verify that the number NUM can be represented in BITS bits (that is,
   within its permitted range), based on the instruction's FLAGS.
   If UPDATE is nonzero, update the value of NUM if necessary.
   Return OP_LEGAL upon success, actual error type upon failure.  */

static op_err
check_range (long *num, int bits, int unsigned flags, int update)
{
  long min, max;
  int retval = OP_LEGAL;
  long value = *num;

  if (bits == 0 && value > 0) return OP_OUT_OF_RANGE;

  /* For hosts witah longs bigger than 32-bits make sure that the top
     bits of a 32-bit negative value read in by the parser are set,
     so that the correct comparisons are made.  */
  if (value & 0x80000000)
    value |= (-1L << 31);


  /* Verify operand value is even.  */
  if (flags & OP_EVEN)
    {
      if (value % 2)
        return OP_NOT_EVEN;
    }

  if (flags & OP_DEC)
    {
      value -= 1;
      if (update)
        *num = value;
    }

  if (flags & OP_SHIFT)
    {
      value >>= 1;
      if (update)
        *num = value;
    }
  else if (flags & OP_SHIFT_DEC)
    {
      value = (value >> 1) - 1;
      if (update)
        *num = value;
    }

  if (flags & OP_ABS20)
    {
      if (value > 0xEFFFF)
        return OP_OUT_OF_RANGE;
    }

  if (flags & OP_ESC)
    {
      if (value == 0xB || value == 0x9)
        return OP_OUT_OF_RANGE;
      else if (value == -1)
	{
	  if (update)
	    *num = 9;
	  return retval;
	}
    }

  if (flags & OP_ESC1)
    {
      if (value > 13)
        return OP_OUT_OF_RANGE;
    }

   if (flags & OP_SIGNED)
     {
       max = (1 << (bits - 1)) - 1;
       min = - (1 << (bits - 1));
       if ((value > max) || (value < min))
         retval = OP_OUT_OF_RANGE;
     }
   else if (flags & OP_UNSIGNED)
     {
       max = ((((1 << (bits - 1)) - 1) << 1) | 1);
       min = 0;
       if (((unsigned long) value > (unsigned long) max)
            || ((unsigned long) value < (unsigned long) min))
         retval = OP_OUT_OF_RANGE;
     }
   else if (flags & OP_NEG)
     {
       max = - 1;
       min = - ((1 << (bits - 1))-1);
       if ((value > max) || (value < min))
         retval = OP_OUT_OF_RANGE;
     }
   return retval;
}

/* Bunch of error checkings.
   The checks are made after a matching instruction was found.  */

static void
warn_if_needed (ins *insn)
{
  /* If the post-increment address mode is used and the load/store
     source register is the same as rbase, the result of the
     instruction is undefined.  */
  if (IS_INSN_TYPE (LD_STOR_INS_INC))
    {
      /* Enough to verify that one of the arguments is a simple reg.  */
      if ((insn->arg[0].type == arg_r) || (insn->arg[1].type == arg_r))
        if (insn->arg[0].r == insn->arg[1].r)
          as_bad (_("Same src/dest register is used (`r%d'), result is undefined"), insn->arg[0].r);
    }

  if (IS_INSN_MNEMONIC ("pop")
      || IS_INSN_MNEMONIC ("push")
      || IS_INSN_MNEMONIC ("popret"))
    {
      unsigned int count = insn->arg[0].constant, reg_val;

      /* Check if count operand caused to save/retrive the RA twice
	 to generate warning message.  */
     if (insn->nargs > 2)
       {
         reg_val = getreg_image (insn->arg[1].r);

         if (   ((reg_val == 9) &&  (count > 7))
	     || ((reg_val == 10) && (count > 6))
	     || ((reg_val == 11) && (count > 5))
	     || ((reg_val == 12) && (count > 4))
	     || ((reg_val == 13) && (count > 2))
	     || ((reg_val == 14) && (count > 0)))
           as_warn (_("RA register is saved twice."));

         /* Check if the third operand is "RA" or "ra" */
         if (!(((insn->arg[2].r) == ra) || ((insn->arg[2].r) == RA)))
           as_bad (_("`%s' Illegal use of registers."), ins_parse);
       }

      if (insn->nargs > 1)
       {
         reg_val = getreg_image (insn->arg[1].r);

         /* If register is a register pair ie r12/r13/r14 in operand1, then
            the count constant should be validated.  */
         if (((reg_val == 11) && (count > 7))
	     || ((reg_val == 12) && (count > 6))
	     || ((reg_val == 13) && (count > 4))
	     || ((reg_val == 14) && (count > 2))
	     || ((reg_val == 15) && (count > 0)))
           as_bad (_("`%s' Illegal count-register combination."), ins_parse);
       }
     else
       {
         /* Check if the operand is "RA" or "ra" */
         if (!(((insn->arg[0].r) == ra) || ((insn->arg[0].r) == RA)))
           as_bad (_("`%s' Illegal use of register."), ins_parse);
       }
    }

  /* Some instruction assume the stack pointer as rptr operand.
     Issue an error when the register to be loaded is also SP.  */
  if (instruction->flags & NO_SP)
    {
      if (getreg_image (insn->arg[1].r) == getreg_image (sp))
        as_bad (_("`%s' has undefined result"), ins_parse);
    }

  /* If the rptr register is specified as one of the registers to be loaded,
     the final contents of rptr are undefined. Thus, we issue an error.  */
  if (instruction->flags & NO_RPTR)
    {
      if ((1 << getreg_image (insn->arg[0].r)) & insn->arg[1].constant)
        as_bad (_("Same src/dest register is used (`r%d'),result is undefined"),
                  getreg_image (insn->arg[0].r));
    }
}

/* In some cases, we need to adjust the instruction pointer although a
   match was already found. Here, we gather all these cases.
   Returns 1 if instruction pointer was adjusted, otherwise 0.  */

static int
adjust_if_needed (ins *insn ATTRIBUTE_UNUSED)
{
  int ret_value = 0;

  if ((IS_INSN_TYPE (CSTBIT_INS)) || (IS_INSN_TYPE (LD_STOR_INS)))
    {
      if ((instruction->operands[0].op_type == abs24)
           && ((insn->arg[0].constant) > 0xF00000))
        {
          insn->arg[0].constant &= 0xFFFFF;
          instruction--;
          ret_value = 1;
        }
    }

  return ret_value;
}

/* Assemble a single instruction:
   INSN is already parsed (that is, all operand values and types are set).
   For instruction to be assembled, we need to find an appropriate template in
   the instruction table, meeting the following conditions:
    1: Has the same number of operands.
    2: Has the same operand types.
    3: Each operand size is sufficient to represent the instruction's values.
   Returns 1 upon success, 0 upon failure.  */

static int
assemble_insn (char *mnemonic, ins *insn)
{
  /* Type of each operand in the current template.  */
  argtype cur_type[MAX_OPERANDS];
  /* Size (in bits) of each operand in the current template.  */
  unsigned int cur_size[MAX_OPERANDS];
  /* Flags of each operand in the current template.  */
  unsigned int cur_flags[MAX_OPERANDS];
  /* Instruction type to match.  */
  unsigned int ins_type;
  /* Boolean flag to mark whether a match was found.  */
  int match = 0;
  int i;
  /* Nonzero if an instruction with same number of operands was found.  */
  int found_same_number_of_operands = 0;
  /* Nonzero if an instruction with same argument types was found.  */
  int found_same_argument_types = 0;
  /* Nonzero if a constant was found within the required range.  */
  int found_const_within_range  = 0;
  /* Argument number of an operand with invalid type.  */
  int invalid_optype = -1;
  /* Argument number of an operand with invalid constant value.  */
  int invalid_const  = -1;
  /* Operand error (used for issuing various constant error messages).  */
  op_err op_error, const_err = OP_LEGAL;

/* Retrieve data (based on FUNC) for each operand of a given instruction.  */
#define GET_CURRENT_DATA(FUNC, ARRAY)                           \
  for (i = 0; i < insn->nargs; i++)                             \
    ARRAY[i] = FUNC (instruction->operands[i].op_type)

#define GET_CURRENT_TYPE    GET_CURRENT_DATA (get_optype, cur_type)
#define GET_CURRENT_SIZE    GET_CURRENT_DATA (get_opbits, cur_size)
#define GET_CURRENT_FLAGS   GET_CURRENT_DATA (get_opflags, cur_flags)

  /* Instruction has no operands -> only copy the constant opcode.   */
  if (insn->nargs == 0)
    {
      output_opcode[0] = BIN (instruction->match, instruction->match_bits);
      return 1;
    }

  /* In some case, same mnemonic can appear with different instruction types.
     For example, 'storb' is supported with 3 different types :
     LD_STOR_INS, LD_STOR_INS_INC, STOR_IMM_INS.
     We assume that when reaching this point, the instruction type was
     pre-determined. We need to make sure that the type stays the same
     during a search for matching instruction.  */
  ins_type = CR16_INS_TYPE (instruction->flags);

  while (/* Check that match is still not found.  */
         match != 1
         /* Check we didn't get to end of table.  */
         && instruction->mnemonic != NULL
         /* Check that the actual mnemonic is still available.  */
         && IS_INSN_MNEMONIC (mnemonic)
         /* Check that the instruction type wasn't changed.  */
         && IS_INSN_TYPE (ins_type))
    {
      /* Check whether number of arguments is legal.  */
      if (get_number_of_operands () != insn->nargs)
        goto next_insn;
      found_same_number_of_operands = 1;

      /* Initialize arrays with data of each operand in current template.  */
      GET_CURRENT_TYPE;
      GET_CURRENT_SIZE;
      GET_CURRENT_FLAGS;

      /* Check for type compatibility.  */
      for (i = 0; i < insn->nargs; i++)
        {
          if (cur_type[i] != insn->arg[i].type)
            {
              if (invalid_optype == -1)
                invalid_optype = i + 1;
              goto next_insn;
            }
        }
      found_same_argument_types = 1;

      for (i = 0; i < insn->nargs; i++)
        {
          /* If 'bal' instruction size is '2' and reg operand is not 'ra'
             then goto next instruction.  */
          if (IS_INSN_MNEMONIC ("bal") && (i == 0)
	      && (instruction->size == 2) && (insn->arg[i].rp != 14))
            goto next_insn;

          /* If 'storb' instruction with 'sp' reg and 16-bit disp of
           * reg-pair, leads to undifined trap, so this should use
           * 20-bit disp of reg-pair.  */
          if (IS_INSN_MNEMONIC ("storb") && (instruction->size == 2)
	      && (insn->arg[i].r == 15) && (insn->arg[i + 1].type == arg_crp))
            goto next_insn;

          /* Only check range - don't update the constant's value, since the
             current instruction may not be the last we try to match.
             The constant's value will be updated later, right before printing
             it to the object file.  */
          if ((insn->arg[i].X_op == O_constant)
              && (op_error = check_range (&insn->arg[i].constant, cur_size[i],
                                          cur_flags[i], 0)))
            {
              if (invalid_const == -1)
                {
                  invalid_const = i + 1;
                  const_err = op_error;
                }
              goto next_insn;
            }
          /* For symbols, we make sure the relocation size (which was already
             determined) is sufficient.  */
          else if ((insn->arg[i].X_op == O_symbol)
                   && ((bfd_reloc_type_lookup (stdoutput, insn->rtype))->bitsize
		       > cur_size[i]))
                  goto next_insn;
        }
      found_const_within_range = 1;

      /* If we got till here -> Full match is found.  */
      match = 1;
      break;

/* Try again with next instruction.  */
next_insn:
      instruction++;
    }

  if (!match)
    {
      /* We haven't found a match - instruction can't be assembled.  */
      if (!found_same_number_of_operands)
        as_bad (_("Incorrect number of operands"));
      else if (!found_same_argument_types)
        as_bad (_("Illegal type of operand (arg %d)"), invalid_optype);
      else if (!found_const_within_range)
        {
          switch (const_err)
            {
	    case OP_OUT_OF_RANGE:
	      as_bad (_("Operand out of range (arg %d)"), invalid_const);
	      break;
	    case OP_NOT_EVEN:
	      as_bad (_("Operand has odd displacement (arg %d)"), invalid_const);
	      break;
	    default:
	      as_bad (_("Illegal operand (arg %d)"), invalid_const);
	      break;
            }
        }

       return 0;
    }
  else
    /* Full match - print the encoding to output file.  */
    {
      /* Make further checkings (such that couldn't be made earlier).
         Warn the user if necessary.  */
      warn_if_needed (insn);

      /* Check whether we need to adjust the instruction pointer.  */
      if (adjust_if_needed (insn))
        /* If instruction pointer was adjusted, we need to update
           the size of the current template operands.  */
        GET_CURRENT_SIZE;

      for (i = 0; i < insn->nargs; i++)
        {
          int j = instruction->flags & REVERSE_MATCH ?
                  i == 0 ? 1 :
                  i == 1 ? 0 : i :
                  i;

          /* This time, update constant value before printing it.  */
            if ((insn->arg[j].X_op == O_constant)
               && (check_range (&insn->arg[j].constant, cur_size[j],
                                cur_flags[j], 1) != OP_LEGAL))
              as_fatal (_("Illegal operand (arg %d)"), j+1);
        }

      /* First, copy the instruction's opcode.  */
      output_opcode[0] = BIN (instruction->match, instruction->match_bits);

      for (i = 0; i < insn->nargs; i++)
        {
         /* For BAL (ra),disp17 instuction only. And also set the
            DISP24a relocation type.  */
         if (IS_INSN_MNEMONIC ("bal") && (instruction->size == 2) && i == 0)
           {
             insn->rtype = BFD_RELOC_CR16_DISP24a;
             continue;
           }
          cur_arg_num = i;
          print_operand (cur_size[i], instruction->operands[i].shift,
                         &insn->arg[i]);
        }
    }

  return 1;
}

/* Print the instruction.
   Handle also cases where the instruction is relaxable/relocatable.  */

static void
print_insn (ins *insn)
{
  unsigned int i, j, insn_size;
  char *this_frag;
  unsigned short words[4];
  int addr_mod;

  /* Arrange the insn encodings in a WORD size array.  */
  for (i = 0, j = 0; i < 2; i++)
    {
      words[j++] = (output_opcode[i] >> 16) & 0xFFFF;
      words[j++] = output_opcode[i] & 0xFFFF;
    }

    insn_size = instruction->size;
    this_frag = frag_more (insn_size * 2);

    /* Handle relocation.  */
    if ((relocatable) && (insn->rtype != BFD_RELOC_NONE))
      {
         reloc_howto_type *reloc_howto;
         int size;

         reloc_howto = bfd_reloc_type_lookup (stdoutput, insn->rtype);

         if (!reloc_howto)
           abort ();

         size = bfd_get_reloc_size (reloc_howto);

         if (size < 1 || size > 4)
           abort ();

         fix_new_exp (frag_now, this_frag - frag_now->fr_literal,
                      size, &insn->exp, reloc_howto->pc_relative,
                      insn->rtype);
      }

  /* Verify a 2-byte code alignment.  */
  addr_mod = frag_now_fix () & 1;
  if (frag_now->has_code && frag_now->insn_addr != addr_mod)
    as_bad (_("instruction address is not a multiple of 2"));
  frag_now->insn_addr = addr_mod;
  frag_now->has_code = 1;

  /* Write the instruction encoding to frag.  */
  for (i = 0; i < insn_size; i++)
    {
      md_number_to_chars (this_frag, (valueT) words[i], 2);
      this_frag += 2;
    }
}

/* This is the guts of the machine-dependent assembler.  OP points to a
   machine dependent instruction.  This function is supposed to emit
   the frags/bytes it assembles to.  */

void
md_assemble (char *op)
{
  ins cr16_ins;
  char *param, param1[32];
  char c;

  /* Reset global variables for a new instruction.  */
  reset_vars (op);

  /* Strip the mnemonic.  */
  for (param = op; *param != 0 && !ISSPACE (*param); param++)
    ;
  c = *param;
  *param++ = '\0';

  /* bCC instuctions and adjust the mnemonic by adding extra white spaces.  */
  if (is_bcc_insn (op))
    {
      strcpy (param1, get_b_cc (op));
      op = "b";
      strcat (param1,",");
      strcat (param1, param);
      param = (char *) &param1;
    }

  /* Checking the cinv options and adjust the mnemonic by removing the
     extra white spaces.  */
  if (streq ("cinv", op))
    {
     /* Validate the cinv options.  */
      check_cinv_options (param);
      strcat (op, param);
    }

  /* MAPPING - SHIFT INSN, if imm4/imm16 positive values
     lsh[b/w] imm4/imm6, reg ==> ashu[b/w] imm4/imm16, reg
     as CR16 core doesn't support lsh[b/w] right shift operaions.  */
  if ((streq ("lshb", op) || streq ("lshw", op) || streq ("lshd", op))
      && (param [0] == '$'))
    {
      strcpy (param1, param);
      /* Find the instruction.  */
      instruction = (const inst *) hash_find (cr16_inst_hash, op);
       parse_operands (&cr16_ins, param1);
      if (((&cr16_ins)->arg[0].type == arg_ic)
	  && ((&cr16_ins)->arg[0].constant >= 0))
        {
           if (streq ("lshb", op))
	     op = "ashub";
           else if (streq ("lshd", op))
	     op = "ashud";
	   else
	     op = "ashuw";
        }
    }

  /* Find the instruction.  */
  instruction = (const inst *) hash_find (cr16_inst_hash, op);
  if (instruction == NULL)
    {
      as_bad (_("Unknown opcode: `%s'"), op);
      return;
    }

  /* Tie dwarf2 debug info to the address at the start of the insn.  */
  dwarf2_emit_insn (0);

  /* Parse the instruction's operands.  */
  parse_insn (&cr16_ins, param);

  /* Assemble the instruction - return upon failure.  */
  if (assemble_insn (op, &cr16_ins) == 0)
    return;

  /* Print the instruction.  */
  print_insn (&cr16_ins);
}
