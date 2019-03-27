/* itbl-ops.c
   Copyright 1997, 1999, 2000, 2001, 2002, 2003, 2005, 2006
   Free Software Foundation, Inc.

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

/*======================================================================*/
/*
 * Herein lies the support for dynamic specification of processor
 * instructions and registers.  Mnemonics, values, and formats for each
 * instruction and register are specified in an ascii file consisting of
 * table entries.  The grammar for the table is defined in the document
 * "Processor instruction table specification".
 *
 * Instructions use the gnu assembler syntax, with the addition of
 * allowing mnemonics for register.
 * Eg. "func $2,reg3,0x100,symbol ; comment"
 * 	func - opcode name
 * 	$n - register n
 * 	reg3 - mnemonic for processor's register defined in table
 * 	0xddd..d - immediate value
 * 	symbol - address of label or external symbol
 *
 * First, itbl_parse reads in the table of register and instruction
 * names and formats, and builds a list of entries for each
 * processor/type combination.  lex and yacc are used to parse
 * the entries in the table and call functions defined here to
 * add each entry to our list.
 *
 * Then, when assembling or disassembling, these functions are called to
 * 1) get information on a processor's registers and
 * 2) assemble/disassemble an instruction.
 * To assemble(disassemble) an instruction, the function
 * itbl_assemble(itbl_disassemble) is called to search the list of
 * instruction entries, and if a match is found, uses the format
 * described in the instruction entry structure to complete the action.
 *
 * Eg. Suppose we have a Mips coprocessor "cop3" with data register "d2"
 * and we want to define function "pig" which takes two operands.
 *
 * Given the table entries:
 * 	"p3 insn pig 0x1:24-21 dreg:20-16 immed:15-0"
 * 	"p3 dreg d2 0x2"
 * and that the instruction encoding for coprocessor pz has encoding:
 * 	#define MIPS_ENCODE_COP_NUM(z) ((0x21|(z<<1))<<25)
 * 	#define ITBL_ENCODE_PNUM(pnum) MIPS_ENCODE_COP_NUM(pnum)
 *
 * a structure to describe the instruction might look something like:
 *      struct itbl_entry = {
 *      e_processor processor = e_p3
 *      e_type type = e_insn
 *      char *name = "pig"
 *      uint value = 0x1
 *      uint flags = 0
 *      struct itbl_range range = 24-21
 *      struct itbl_field *field = {
 *              e_type type = e_dreg
 *              struct itbl_range range = 20-16
 *              struct itbl_field *next = {
 *                      e_type type = e_immed
 *                      struct itbl_range range = 15-0
 *                      struct itbl_field *next = 0
 *                      };
 *              };
 *      struct itbl_entry *next = 0
 *      };
 *
 * And the assembler instructions:
 * 	"pig d2,0x100"
 * 	"pig $2,0x100"
 *
 * would both assemble to the hex value:
 * 	"0x4e220100"
 *
 */

#include "as.h"
#include "itbl-ops.h"
#include <itbl-parse.h>

/* #define DEBUG */

#ifdef DEBUG
#include <assert.h>
#define ASSERT(x) assert(x)
#define DBG(x) printf x
#else
#define ASSERT(x)
#define DBG(x)
#endif

#ifndef min
#define min(a,b) (a<b?a:b)
#endif

int itbl_have_entries = 0;

/*======================================================================*/
/* structures for keeping itbl format entries */

struct itbl_range {
  int sbit;			/* mask starting bit position */
  int ebit;			/* mask ending bit position */
};

struct itbl_field {
  e_type type;			/* dreg/creg/greg/immed/symb */
  struct itbl_range range;	/* field's bitfield range within instruction */
  unsigned long flags;		/* field flags */
  struct itbl_field *next;	/* next field in list */
};

/* These structures define the instructions and registers for a processor.
 * If the type is an instruction, the structure defines the format of an
 * instruction where the fields are the list of operands.
 * The flags field below uses the same values as those defined in the
 * gnu assembler and are machine specific.  */
struct itbl_entry {
  e_processor processor;	/* processor number */
  e_type type;			/* dreg/creg/greg/insn */
  char *name;			/* mnemionic name for insn/register */
  unsigned long value;		/* opcode/instruction mask/register number */
  unsigned long flags;		/* effects of the instruction */
  struct itbl_range range;	/* bit range within instruction for value */
  struct itbl_field *fields;	/* list of operand definitions (if any) */
  struct itbl_entry *next;	/* next entry */
};

/* local data and structures */

static int itbl_num_opcodes = 0;
/* Array of entries for each processor and entry type */
static struct itbl_entry *entries[e_nprocs][e_ntypes];

/* local prototypes */
static unsigned long build_opcode (struct itbl_entry *e);
static e_type get_type (int yytype);
static e_processor get_processor (int yyproc);
static struct itbl_entry **get_entries (e_processor processor,
					e_type type);
static struct itbl_entry *find_entry_byname (e_processor processor,
					e_type type, char *name);
static struct itbl_entry *find_entry_byval (e_processor processor,
			e_type type, unsigned long val, struct itbl_range *r);
static struct itbl_entry *alloc_entry (e_processor processor,
		e_type type, char *name, unsigned long value);
static unsigned long apply_range (unsigned long value, struct itbl_range r);
static unsigned long extract_range (unsigned long value, struct itbl_range r);
static struct itbl_field *alloc_field (e_type type, int sbit,
					int ebit, unsigned long flags);

/*======================================================================*/
/* Interfaces to the parser */

/* Open the table and use lex and yacc to parse the entries.
 * Return 1 for failure; 0 for success.  */

int
itbl_parse (char *insntbl)
{
  extern FILE *yyin;
  extern int yyparse (void);

  yyin = fopen (insntbl, FOPEN_RT);
  if (yyin == 0)
    {
      printf ("Can't open processor instruction specification file \"%s\"\n",
	      insntbl);
      return 1;
    }

  while (yyparse ())
    ;

  fclose (yyin);
  itbl_have_entries = 1;
  return 0;
}

/* Add a register entry */

struct itbl_entry *
itbl_add_reg (int yyprocessor, int yytype, char *regname,
	      int regnum)
{
  return alloc_entry (get_processor (yyprocessor), get_type (yytype), regname,
		      (unsigned long) regnum);
}

/* Add an instruction entry */

struct itbl_entry *
itbl_add_insn (int yyprocessor, char *name, unsigned long value,
	       int sbit, int ebit, unsigned long flags)
{
  struct itbl_entry *e;
  e = alloc_entry (get_processor (yyprocessor), e_insn, name, value);
  if (e)
    {
      e->range.sbit = sbit;
      e->range.ebit = ebit;
      e->flags = flags;
      itbl_num_opcodes++;
    }
  return e;
}

/* Add an operand to an instruction entry */

struct itbl_field *
itbl_add_operand (struct itbl_entry *e, int yytype, int sbit,
		  int ebit, unsigned long flags)
{
  struct itbl_field *f, **last_f;
  if (!e)
    return 0;
  /* Add to end of fields' list.  */
  f = alloc_field (get_type (yytype), sbit, ebit, flags);
  if (f)
    {
      last_f = &e->fields;
      while (*last_f)
	last_f = &(*last_f)->next;
      *last_f = f;
      f->next = 0;
    }
  return f;
}

/*======================================================================*/
/* Interfaces for assembler and disassembler */

#ifndef STAND_ALONE
static void append_insns_as_macros (void);

/* Initialize for gas.  */

void
itbl_init (void)
{
  struct itbl_entry *e, **es;
  e_processor procn;
  e_type type;

  if (!itbl_have_entries)
    return;

  /* Since register names don't have a prefix, put them in the symbol table so
     they can't be used as symbols.  This simplifies argument parsing as
     we can let gas parse registers for us.  */
  /* Use symbol_create instead of symbol_new so we don't try to
     output registers into the object file's symbol table.  */

  for (type = e_regtype0; type < e_nregtypes; type++)
    for (procn = e_p0; procn < e_nprocs; procn++)
      {
	es = get_entries (procn, type);
	for (e = *es; e; e = e->next)
	  {
	    symbol_table_insert (symbol_create (e->name, reg_section,
						e->value, &zero_address_frag));
	  }
      }
  append_insns_as_macros ();
}

/* Append insns to opcodes table and increase number of opcodes
 * Structure of opcodes table:
 * struct itbl_opcode
 * {
 *   const char *name;
 *   const char *args; 		- string describing the arguments.
 *   unsigned long match; 	- opcode, or ISA level if pinfo=INSN_MACRO
 *   unsigned long mask; 	- opcode mask, or macro id if pinfo=INSN_MACRO
 *   unsigned long pinfo; 	- insn flags, or INSN_MACRO
 * };
 * examples:
 *	{"li",      "t,i",  0x34000000, 0xffe00000, WR_t    },
 *	{"li",      "t,I",  0,    (int) M_LI,   INSN_MACRO  },
 */

static char *form_args (struct itbl_entry *e);
static void
append_insns_as_macros (void)
{
  struct ITBL_OPCODE_STRUCT *new_opcodes, *o;
  struct itbl_entry *e, **es;
  int n, id, size, new_size, new_num_opcodes;

  if (!itbl_have_entries)
    return;

  if (!itbl_num_opcodes)	/* no new instructions to add! */
    {
      return;
    }
  DBG (("previous num_opcodes=%d\n", ITBL_NUM_OPCODES));

  new_num_opcodes = ITBL_NUM_OPCODES + itbl_num_opcodes;
  ASSERT (new_num_opcodes >= itbl_num_opcodes);

  size = sizeof (struct ITBL_OPCODE_STRUCT) * ITBL_NUM_OPCODES;
  ASSERT (size >= 0);
  DBG (("I get=%d\n", size / sizeof (ITBL_OPCODES[0])));

  new_size = sizeof (struct ITBL_OPCODE_STRUCT) * new_num_opcodes;
  ASSERT (new_size > size);

  /* FIXME since ITBL_OPCODES culd be a static table,
		we can't realloc or delete the old memory.  */
  new_opcodes = (struct ITBL_OPCODE_STRUCT *) malloc (new_size);
  if (!new_opcodes)
    {
      printf (_("Unable to allocate memory for new instructions\n"));
      return;
    }
  if (size)			/* copy preexisting opcodes table */
    memcpy (new_opcodes, ITBL_OPCODES, size);

  /* FIXME! some NUMOPCODES are calculated expressions.
		These need to be changed before itbls can be supported.  */

  id = ITBL_NUM_MACROS;		/* begin the next macro id after the last */
  o = &new_opcodes[ITBL_NUM_OPCODES];	/* append macro to opcodes list */
  for (n = e_p0; n < e_nprocs; n++)
    {
      es = get_entries (n, e_insn);
      for (e = *es; e; e = e->next)
	{
	  /* name,    args,   mask,       match,  pinfo
		 * {"li",      "t,i",  0x34000000, 0xffe00000, WR_t    },
		 * {"li",      "t,I",  0,    (int) M_LI,   INSN_MACRO  },
		 * Construct args from itbl_fields.
		*/
	  o->name = e->name;
	  o->args = strdup (form_args (e));
	  o->mask = apply_range (e->value, e->range);
	  /* FIXME how to catch during assembly? */
	  /* mask to identify this insn */
	  o->match = apply_range (e->value, e->range);
	  o->pinfo = 0;

#ifdef USE_MACROS
	  o->mask = id++;	/* FIXME how to catch during assembly? */
	  o->match = 0;		/* for macros, the insn_isa number */
	  o->pinfo = INSN_MACRO;
#endif

	  /* Don't add instructions which caused an error */
	  if (o->args)
	    o++;
	  else
	    new_num_opcodes--;
	}
    }
  ITBL_OPCODES = new_opcodes;
  ITBL_NUM_OPCODES = new_num_opcodes;

  /* FIXME
		At this point, we can free the entries, as they should have
		been added to the assembler's tables.
		Don't free name though, since name is being used by the new
		opcodes table.

		Eventually, we should also free the new opcodes table itself
		on exit.
	*/
}

static char *
form_args (struct itbl_entry *e)
{
  static char s[31];
  char c = 0, *p = s;
  struct itbl_field *f;

  ASSERT (e);
  for (f = e->fields; f; f = f->next)
    {
      switch (f->type)
	{
	case e_dreg:
	  c = 'd';
	  break;
	case e_creg:
	  c = 't';
	  break;
	case e_greg:
	  c = 's';
	  break;
	case e_immed:
	  c = 'i';
	  break;
	case e_addr:
	  c = 'a';
	  break;
	default:
	  c = 0;		/* ignore; unknown field type */
	}
      if (c)
	{
	  if (p != s)
	    *p++ = ',';
	  *p++ = c;
	}
    }
  *p = 0;
  return s;
}
#endif /* !STAND_ALONE */

/* Get processor's register name from val */

int
itbl_get_reg_val (char *name, unsigned long *pval)
{
  e_type t;
  e_processor p;

  for (p = e_p0; p < e_nprocs; p++)
    {
      for (t = e_regtype0; t < e_nregtypes; t++)
	{
	  if (itbl_get_val (p, t, name, pval))
	    return 1;
	}
    }
  return 0;
}

char *
itbl_get_name (e_processor processor, e_type type, unsigned long val)
{
  struct itbl_entry *r;
  /* type depends on instruction passed */
  r = find_entry_byval (processor, type, val, 0);
  if (r)
    return r->name;
  else
    return 0;			/* error; invalid operand */
}

/* Get processor's register value from name */

int
itbl_get_val (e_processor processor, e_type type, char *name,
	      unsigned long *pval)
{
  struct itbl_entry *r;
  /* type depends on instruction passed */
  r = find_entry_byname (processor, type, name);
  if (r == NULL)
    return 0;
  *pval = r->value;
  return 1;
}

/* Assemble instruction "name" with operands "s".
 * name - name of instruction
 * s - operands
 * returns - long word for assembled instruction */

unsigned long
itbl_assemble (char *name, char *s)
{
  unsigned long opcode;
  struct itbl_entry *e = NULL;
  struct itbl_field *f;
  char *n;
  int processor;

  if (!name || !*name)
    return 0;			/* error!  must have an opcode name/expr */

  /* find entry in list of instructions for all processors */
  for (processor = 0; processor < e_nprocs; processor++)
    {
      e = find_entry_byname (processor, e_insn, name);
      if (e)
	break;
    }
  if (!e)
    return 0;			/* opcode not in table; invalid instruction */
  opcode = build_opcode (e);

  /* parse opcode's args (if any) */
  for (f = e->fields; f; f = f->next)	/* for each arg, ...  */
    {
      struct itbl_entry *r;
      unsigned long value;
      if (!s || !*s)
	return 0;		/* error - not enough operands */
      n = itbl_get_field (&s);
      /* n should be in form $n or 0xhhh (are symbol names valid?? */
      switch (f->type)
	{
	case e_dreg:
	case e_creg:
	case e_greg:
	  /* Accept either a string name
			 * or '$' followed by the register number */
	  if (*n == '$')
	    {
	      n++;
	      value = strtol (n, 0, 10);
	      /* FIXME! could have "0l"... then what?? */
	      if (value == 0 && *n != '0')
		return 0;	/* error; invalid operand */
	    }
	  else
	    {
	      r = find_entry_byname (e->processor, f->type, n);
	      if (r)
		value = r->value;
	      else
		return 0;	/* error; invalid operand */
	    }
	  break;
	case e_addr:
	  /* use assembler's symbol table to find symbol */
	  /* FIXME!! Do we need this?
				if so, what about relocs??
				my_getExpression (&imm_expr, s);
				return 0;	/-* error; invalid operand *-/
				break;
			*/
	  /* If not a symbol, fall thru to IMMED */
	case e_immed:
	  if (*n == '0' && *(n + 1) == 'x')	/* hex begins 0x...  */
	    {
	      n += 2;
	      value = strtol (n, 0, 16);
	      /* FIXME! could have "0xl"... then what?? */
	    }
	  else
	    {
	      value = strtol (n, 0, 10);
	      /* FIXME! could have "0l"... then what?? */
	      if (value == 0 && *n != '0')
		return 0;	/* error; invalid operand */
	    }
	  break;
	default:
	  return 0;		/* error; invalid field spec */
	}
      opcode |= apply_range (value, f->range);
    }
  if (s && *s)
    return 0;			/* error - too many operands */
  return opcode;		/* done! */
}

/* Disassemble instruction "insn".
 * insn - instruction
 * s - buffer to hold disassembled instruction
 * returns - 1 if succeeded; 0 if failed
 */

int
itbl_disassemble (char *s, unsigned long insn)
{
  e_processor processor;
  struct itbl_entry *e;
  struct itbl_field *f;

  if (!ITBL_IS_INSN (insn))
    return 0;			/* error */
  processor = get_processor (ITBL_DECODE_PNUM (insn));

  /* find entry in list */
  e = find_entry_byval (processor, e_insn, insn, 0);
  if (!e)
    return 0;			/* opcode not in table; invalid instruction */
  strcpy (s, e->name);

  /* Parse insn's args (if any).  */
  for (f = e->fields; f; f = f->next)	/* for each arg, ...  */
    {
      struct itbl_entry *r;
      unsigned long value;

      if (f == e->fields)	/* First operand is preceded by tab.  */
	strcat (s, "\t");
      else			/* ','s separate following operands.  */
	strcat (s, ",");
      value = extract_range (insn, f->range);
      /* n should be in form $n or 0xhhh (are symbol names valid?? */
      switch (f->type)
	{
	case e_dreg:
	case e_creg:
	case e_greg:
	  /* Accept either a string name
	     or '$' followed by the register number.  */
	  r = find_entry_byval (e->processor, f->type, value, &f->range);
	  if (r)
	    strcat (s, r->name);
	  else
	    sprintf (s, "%s$%lu", s, value);
	  break;
	case e_addr:
	  /* Use assembler's symbol table to find symbol.  */
	  /* FIXME!! Do we need this?  If so, what about relocs??  */
	  /* If not a symbol, fall through to IMMED.  */
	case e_immed:
	  sprintf (s, "%s0x%lx", s, value);
	  break;
	default:
	  return 0;		/* error; invalid field spec */
	}
    }
  return 1;			/* Done!  */
}

/*======================================================================*/
/*
 * Local functions for manipulating private structures containing
 * the names and format for the new instructions and registers
 * for each processor.
 */

/* Calculate instruction's opcode and function values from entry */

static unsigned long
build_opcode (struct itbl_entry *e)
{
  unsigned long opcode;

  opcode = apply_range (e->value, e->range);
  opcode |= ITBL_ENCODE_PNUM (e->processor);
  return opcode;
}

/* Calculate absolute value given the relative value and bit position range
 * within the instruction.
 * The range is inclusive where 0 is least significant bit.
 * A range of { 24, 20 } will have a mask of
 * bit   3           2            1
 * pos: 1098 7654 3210 9876 5432 1098 7654 3210
 * bin: 0000 0001 1111 0000 0000 0000 0000 0000
 * hex:    0    1    f    0    0    0    0    0
 * mask: 0x01f00000.
 */

static unsigned long
apply_range (unsigned long rval, struct itbl_range r)
{
  unsigned long mask;
  unsigned long aval;
  int len = MAX_BITPOS - r.sbit;

  ASSERT (r.sbit >= r.ebit);
  ASSERT (MAX_BITPOS >= r.sbit);
  ASSERT (r.ebit >= 0);

  /* create mask by truncating 1s by shifting */
  mask = 0xffffffff << len;
  mask = mask >> len;
  mask = mask >> r.ebit;
  mask = mask << r.ebit;

  aval = (rval << r.ebit) & mask;
  return aval;
}

/* Calculate relative value given the absolute value and bit position range
 * within the instruction.  */

static unsigned long
extract_range (unsigned long aval, struct itbl_range r)
{
  unsigned long mask;
  unsigned long rval;
  int len = MAX_BITPOS - r.sbit;

  /* create mask by truncating 1s by shifting */
  mask = 0xffffffff << len;
  mask = mask >> len;
  mask = mask >> r.ebit;
  mask = mask << r.ebit;

  rval = (aval & mask) >> r.ebit;
  return rval;
}

/* Extract processor's assembly instruction field name from s;
 * forms are "n args" "n,args" or "n" */
/* Return next argument from string pointer "s" and advance s.
 * delimiters are " ,()" */

char *
itbl_get_field (char **S)
{
  static char n[128];
  char *s;
  int len;

  s = *S;
  if (!s || !*s)
    return 0;
  /* FIXME: This is a weird set of delimiters.  */
  len = strcspn (s, " \t,()");
  ASSERT (128 > len + 1);
  strncpy (n, s, len);
  n[len] = 0;
  if (s[len] == '\0')
    s = 0;			/* no more args */
  else
    s += len + 1;		/* advance to next arg */

  *S = s;
  return n;
}

/* Search entries for a given processor and type
 * to find one matching the name "n".
 * Return a pointer to the entry */

static struct itbl_entry *
find_entry_byname (e_processor processor,
		   e_type type, char *n)
{
  struct itbl_entry *e, **es;

  es = get_entries (processor, type);
  for (e = *es; e; e = e->next)	/* for each entry, ...  */
    {
      if (!strcmp (e->name, n))
	return e;
    }
  return 0;
}

/* Search entries for a given processor and type
 * to find one matching the value "val" for the range "r".
 * Return a pointer to the entry.
 * This function is used for disassembling fields of an instruction.
 */

static struct itbl_entry *
find_entry_byval (e_processor processor, e_type type,
		  unsigned long val, struct itbl_range *r)
{
  struct itbl_entry *e, **es;
  unsigned long eval;

  es = get_entries (processor, type);
  for (e = *es; e; e = e->next)	/* for each entry, ...  */
    {
      if (processor != e->processor)
	continue;
      /* For insns, we might not know the range of the opcode,
	 * so a range of 0 will allow this routine to match against
	 * the range of the entry to be compared with.
	 * This could cause ambiguities.
	 * For operands, we get an extracted value and a range.
	 */
      /* if range is 0, mask val against the range of the compared entry.  */
      if (r == 0)		/* if no range passed, must be whole 32-bits
			 * so create 32-bit value from entry's range */
	{
	  eval = apply_range (e->value, e->range);
	  val &= apply_range (0xffffffff, e->range);
	}
      else if ((r->sbit == e->range.sbit && r->ebit == e->range.ebit)
	       || (e->range.sbit == 0 && e->range.ebit == 0))
	{
	  eval = apply_range (e->value, *r);
	  val = apply_range (val, *r);
	}
      else
	continue;
      if (val == eval)
	return e;
    }
  return 0;
}

/* Return a pointer to the list of entries for a given processor and type.  */

static struct itbl_entry **
get_entries (e_processor processor, e_type type)
{
  return &entries[processor][type];
}

/* Return an integral value for the processor passed from yyparse.  */

static e_processor
get_processor (int yyproc)
{
  /* translate from yacc's processor to enum */
  if (yyproc >= e_p0 && yyproc < e_nprocs)
    return (e_processor) yyproc;
  return e_invproc;		/* error; invalid processor */
}

/* Return an integral value for the entry type passed from yyparse.  */

static e_type
get_type (int yytype)
{
  switch (yytype)
    {
      /* translate from yacc's type to enum */
    case INSN:
      return e_insn;
    case DREG:
      return e_dreg;
    case CREG:
      return e_creg;
    case GREG:
      return e_greg;
    case ADDR:
      return e_addr;
    case IMMED:
      return e_immed;
    default:
      return e_invtype;		/* error; invalid type */
    }
}

/* Allocate and initialize an entry */

static struct itbl_entry *
alloc_entry (e_processor processor, e_type type,
	     char *name, unsigned long value)
{
  struct itbl_entry *e, **es;
  if (!name)
    return 0;
  e = (struct itbl_entry *) malloc (sizeof (struct itbl_entry));
  if (e)
    {
      memset (e, 0, sizeof (struct itbl_entry));
      e->name = (char *) malloc (sizeof (strlen (name)) + 1);
      if (e->name)
	strcpy (e->name, name);
      e->processor = processor;
      e->type = type;
      e->value = value;
      es = get_entries (e->processor, e->type);
      e->next = *es;
      *es = e;
    }
  return e;
}

/* Allocate and initialize an entry's field */

static struct itbl_field *
alloc_field (e_type type, int sbit, int ebit,
	     unsigned long flags)
{
  struct itbl_field *f;
  f = (struct itbl_field *) malloc (sizeof (struct itbl_field));
  if (f)
    {
      memset (f, 0, sizeof (struct itbl_field));
      f->type = type;
      f->range.sbit = sbit;
      f->range.ebit = ebit;
      f->flags = flags;
    }
  return f;
}
