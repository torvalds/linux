/* Assembler interface for targets using CGEN. -*- C -*-
   CGEN: Cpu tools GENerator

   THIS FILE IS MACHINE GENERATED WITH CGEN.
   - the resultant file is machine generated, cgen-asm.in isn't

   Copyright 1996, 1997, 1998, 1999, 2000, 2001, 2005
   Free Software Foundation, Inc.

   This file is part of the GNU Binutils and GDB, the GNU debugger.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

/* ??? Eventually more and more of this stuff can go to cpu-independent files.
   Keep that in mind.  */

#include "sysdep.h"
#include <stdio.h>
#include "ansidecl.h"
#include "bfd.h"
#include "symcat.h"
#include "mep-desc.h"
#include "mep-opc.h"
#include "opintl.h"
#include "xregex.h"
#include "libiberty.h"
#include "safe-ctype.h"

#undef  min
#define min(a,b) ((a) < (b) ? (a) : (b))
#undef  max
#define max(a,b) ((a) > (b) ? (a) : (b))

static const char * parse_insn_normal
  (CGEN_CPU_DESC, const CGEN_INSN *, const char **, CGEN_FIELDS *);

/* -- assembler routines inserted here.  */

/* -- asm.c */

#define CGEN_VALIDATE_INSN_SUPPORTED

       const char * parse_csrn       (CGEN_CPU_DESC, const char **, CGEN_KEYWORD *, long *);
       const char * parse_tpreg      (CGEN_CPU_DESC, const char **, CGEN_KEYWORD *, long *);
       const char * parse_spreg      (CGEN_CPU_DESC, const char **, CGEN_KEYWORD *, long *);
       const char * parse_mep_align  (CGEN_CPU_DESC, const char **, enum cgen_operand_type, long *);
       const char * parse_mep_alignu (CGEN_CPU_DESC, const char **, enum cgen_operand_type, unsigned long *);
static const char * parse_signed16   (CGEN_CPU_DESC, const char **, int, long *);
static const char * parse_unsigned16 (CGEN_CPU_DESC, const char **, int, unsigned long *);
static const char * parse_lo16       (CGEN_CPU_DESC, const char **, int, long *, long);
static const char * parse_unsigned7  (CGEN_CPU_DESC, const char **, enum cgen_operand_type, unsigned long *);
static const char * parse_zero       (CGEN_CPU_DESC, const char **, int, long *);

const char *
parse_csrn (CGEN_CPU_DESC cd, const char **strp,
	    CGEN_KEYWORD *keyword_table, long *field)
{
  const char *err;
  unsigned long value;

  err = cgen_parse_keyword (cd, strp, keyword_table, field);
  if (!err)
    return NULL;

  err = cgen_parse_unsigned_integer (cd, strp, MEP_OPERAND_CSRN_IDX, & value);
  if (err)
    return err;
  *field = value;
  return NULL;
}

/* begin-cop-ip-parse-handlers */
static const char *
parse_fmax_cr (CGEN_CPU_DESC cd,
	const char **strp,
	CGEN_KEYWORD *keyword_table  ATTRIBUTE_UNUSED,
	long *field)
{
  return cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_cr_fmax, field);
}
static const char *
parse_fmax_ccr (CGEN_CPU_DESC cd,
	const char **strp,
	CGEN_KEYWORD *keyword_table  ATTRIBUTE_UNUSED,
	long *field)
{
  return cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_ccr_fmax, field);
}
/* end-cop-ip-parse-handlers */

const char *
parse_tpreg (CGEN_CPU_DESC cd, const char ** strp,
	     CGEN_KEYWORD *keyword_table, long *field)
{
  const char *err;

  err = cgen_parse_keyword (cd, strp, keyword_table, field);
  if (err)
    return err;
  if (*field != 13)
    return _("Only $tp or $13 allowed for this opcode");
  return NULL;
}

const char *
parse_spreg (CGEN_CPU_DESC cd, const char ** strp,
	     CGEN_KEYWORD *keyword_table, long *field)
{
  const char *err;

  err = cgen_parse_keyword (cd, strp, keyword_table, field);
  if (err)
    return err;
  if (*field != 15)
    return _("Only $sp or $15 allowed for this opcode");
  return NULL;
}

const char *
parse_mep_align (CGEN_CPU_DESC cd, const char ** strp,
		 enum cgen_operand_type type, long *field)
{
  long lsbs = 0;
  const char *err;

  switch (type)
    {
    case MEP_OPERAND_PCREL8A2:
    case MEP_OPERAND_PCREL12A2:
    case MEP_OPERAND_PCREL17A2:
    case MEP_OPERAND_PCREL24A2:
    case MEP_OPERAND_CDISP8A2:
    case MEP_OPERAND_CDISP8A4:
    case MEP_OPERAND_CDISP8A8:
      err = cgen_parse_signed_integer   (cd, strp, type, field);
      break;
    case MEP_OPERAND_PCABS24A2:
    case MEP_OPERAND_UDISP7:
    case MEP_OPERAND_UDISP7A2:
    case MEP_OPERAND_UDISP7A4:
    case MEP_OPERAND_UIMM7A4:
    case MEP_OPERAND_ADDR24A4:
      err = cgen_parse_unsigned_integer (cd, strp, type, (unsigned long *) field);
      break;
    default:
      abort();
    }
  if (err)
    return err;
  switch (type)
    {
    case MEP_OPERAND_UDISP7:
      lsbs = 0;
      break;
    case MEP_OPERAND_PCREL8A2:
    case MEP_OPERAND_PCREL12A2:
    case MEP_OPERAND_PCREL17A2:
    case MEP_OPERAND_PCREL24A2:
    case MEP_OPERAND_PCABS24A2:
    case MEP_OPERAND_UDISP7A2:
    case MEP_OPERAND_CDISP8A2:
      lsbs = *field & 1;
      break;
    case MEP_OPERAND_UDISP7A4:
    case MEP_OPERAND_UIMM7A4:
    case MEP_OPERAND_ADDR24A4:
    case MEP_OPERAND_CDISP8A4:
      lsbs = *field & 3;
      break;
    case MEP_OPERAND_CDISP8A8:
      lsbs = *field & 7;
      break;
    default:
      /* Safe assumption?  */
      abort ();
    }
  if (lsbs)
    return "Value is not aligned enough";
  return NULL;
}

const char *
parse_mep_alignu (CGEN_CPU_DESC cd, const char ** strp,
		 enum cgen_operand_type type, unsigned long *field)
{
  return parse_mep_align (cd, strp, type, (long *) field);
}


/* Handle %lo(), %tpoff(), %sdaoff(), %hi(), and other signed
   constants in a signed context.  */

static const char *
parse_signed16 (CGEN_CPU_DESC cd,
		const char **strp,
		int opindex,
		long *valuep)
{
  return parse_lo16 (cd, strp, opindex, valuep, 1);
}

static const char *
parse_lo16 (CGEN_CPU_DESC cd,
	    const char **strp,
	    int opindex,
	    long *valuep,
	    long signedp)
{
  const char *errmsg;
  enum cgen_parse_operand_result result_type;
  bfd_vma value;

  if (strncasecmp (*strp, "%lo(", 4) == 0)
    {
      *strp += 4;
      errmsg = cgen_parse_address (cd, strp, opindex, BFD_RELOC_MEP_LOW16,
				   & result_type, & value);
      if (**strp != ')')
	return _("missing `)'");
      ++*strp;
      if (errmsg == NULL
	  && result_type == CGEN_PARSE_OPERAND_RESULT_NUMBER)
	value &= 0xffff;
      if (signedp)
	*valuep = (long)(short) value;
      else
	*valuep = value;
      return errmsg;
    }

  if (strncasecmp (*strp, "%hi(", 4) == 0)
    {
      *strp += 4;
      errmsg = cgen_parse_address (cd, strp, opindex, BFD_RELOC_MEP_HI16S,
				   & result_type, & value);
      if (**strp != ')')
	return _("missing `)'");
      ++*strp;
      if (errmsg == NULL
	  && result_type == CGEN_PARSE_OPERAND_RESULT_NUMBER)
	value = (value + 0x8000) >> 16;
      *valuep = value;
      return errmsg;
    }

  if (strncasecmp (*strp, "%uhi(", 5) == 0)
    {
      *strp += 5;
      errmsg = cgen_parse_address (cd, strp, opindex, BFD_RELOC_MEP_HI16U,
				   & result_type, & value);
      if (**strp != ')')
	return _("missing `)'");
      ++*strp;
      if (errmsg == NULL
	  && result_type == CGEN_PARSE_OPERAND_RESULT_NUMBER)
	value = value >> 16;
      *valuep = value;
      return errmsg;
    }

  if (strncasecmp (*strp, "%sdaoff(", 8) == 0)
    {
      *strp += 8;
      errmsg = cgen_parse_address (cd, strp, opindex, BFD_RELOC_MEP_GPREL,
				   NULL, & value);
      if (**strp != ')')
	return _("missing `)'");
      ++*strp;
      *valuep = value;
      return errmsg;
    }

  if (strncasecmp (*strp, "%tpoff(", 7) == 0)
    {
      *strp += 7;
      errmsg = cgen_parse_address (cd, strp, opindex, BFD_RELOC_MEP_TPREL,
				   NULL, & value);
      if (**strp != ')')
	return _("missing `)'");
      ++*strp;
      *valuep = value;
      return errmsg;
    }

  if (**strp == '%')
    return _("invalid %function() here");

  return cgen_parse_signed_integer (cd, strp, opindex, valuep);
}

static const char *
parse_unsigned16 (CGEN_CPU_DESC cd,
		  const char **strp,
		  int opindex,
		  unsigned long *valuep)
{
  return parse_lo16 (cd, strp, opindex, (long *) valuep, 0);
}

/* A special case of parse_signed16 which accepts only the value zero.  */

static const char *
parse_zero (CGEN_CPU_DESC cd, const char **strp, int opindex, long *valuep)
{
  const char *errmsg;
  enum cgen_parse_operand_result result_type;
  bfd_vma value;

  /*fprintf(stderr, "dj: signed parse opindex `%s'\n", *strp);*/

  /* Prevent ($ry) from being attempted as an expression on 'sw $rx,($ry)'.
     It will fail and cause ry to be listed as an undefined symbol in the
     listing.  */
  if (strncmp (*strp, "($", 2) == 0)
    return "not zero"; /* any string will do -- will never be seen.  */

  if (strncasecmp (*strp, "%lo(", 4) == 0)
    {
      *strp += 4;
      errmsg = cgen_parse_address (cd, strp, opindex, BFD_RELOC_MEP_LOW16,
				   &result_type, &value);
      if (**strp != ')')
	return "missing `)'";
      ++*strp;
      if (errmsg == NULL
	  && (result_type != CGEN_PARSE_OPERAND_RESULT_NUMBER || value != 0))
	return "not zero"; /* any string will do -- will never be seen.  */
      *valuep = value;
      return errmsg;
    }

  if (strncasecmp (*strp, "%hi(", 4) == 0)
    {
      *strp += 4;
      errmsg = cgen_parse_address (cd, strp, opindex, BFD_RELOC_MEP_HI16S,
				   &result_type, &value);
      if (**strp != ')')
	return "missing `)'";
      ++*strp;
      if (errmsg == NULL
	  && (result_type != CGEN_PARSE_OPERAND_RESULT_NUMBER || value != 0))
	return "not zero"; /* any string will do -- will never be seen.  */
      *valuep = value;
      return errmsg;
    }

  if (strncasecmp (*strp, "%uhi(", 5) == 0)
    {
      *strp += 5;
      errmsg = cgen_parse_address (cd, strp, opindex, BFD_RELOC_MEP_HI16U,
				   &result_type, &value);
      if (**strp != ')')
	return "missing `)'";
      ++*strp;
      if (errmsg == NULL
	  && (result_type != CGEN_PARSE_OPERAND_RESULT_NUMBER || value != 0))
	return "not zero"; /* any string will do -- will never be seen.  */
      *valuep = value;
      return errmsg;
    }

  if (strncasecmp (*strp, "%sdaoff(", 8) == 0)
    {
      *strp += 8;
      errmsg = cgen_parse_address (cd, strp, opindex, BFD_RELOC_MEP_GPREL,
				   &result_type, &value);
      if (**strp != ')')
	return "missing `)'";
      ++*strp;
      if (errmsg == NULL
	  && (result_type != CGEN_PARSE_OPERAND_RESULT_NUMBER || value != 0))
	return "not zero"; /* any string will do -- will never be seen.  */
      *valuep = value;
      return errmsg;
    }

  if (strncasecmp (*strp, "%tpoff(", 7) == 0)
    {
      *strp += 7;
      errmsg = cgen_parse_address (cd, strp, opindex, BFD_RELOC_MEP_TPREL,
				   &result_type, &value);
      if (**strp != ')')
	return "missing `)'";
      ++*strp;
      if (errmsg == NULL
	  && (result_type != CGEN_PARSE_OPERAND_RESULT_NUMBER || value != 0))
	return "not zero"; /* any string will do -- will never be seen.  */
      *valuep = value;
      return errmsg;
    }

  if (**strp == '%')
    return "invalid %function() here";

  errmsg = cgen_parse_address (cd, strp, opindex, BFD_RELOC_NONE,
			       &result_type, &value);
  if (errmsg == NULL
      && (result_type != CGEN_PARSE_OPERAND_RESULT_NUMBER || value != 0))
    return "not zero"; /* any string will do -- will never be seen.  */

  return errmsg;
}

static const char *
parse_unsigned7 (CGEN_CPU_DESC cd, const char **strp,
		 enum cgen_operand_type opindex, unsigned long *valuep)
{
  const char *errmsg;
  bfd_vma value;

  /* fprintf(stderr, "dj: unsigned7 parse `%s'\n", *strp); */

  if (strncasecmp (*strp, "%tpoff(", 7) == 0)
    {
      int reloc;
      *strp += 7;
      switch (opindex)
	{
	case MEP_OPERAND_UDISP7:
	  reloc = BFD_RELOC_MEP_TPREL7;
	  break;
	case MEP_OPERAND_UDISP7A2:
	  reloc = BFD_RELOC_MEP_TPREL7A2;
	  break;
	case MEP_OPERAND_UDISP7A4:
	  reloc = BFD_RELOC_MEP_TPREL7A4;
	  break;
	default:
	  /* Safe assumption?  */
	  abort (); 
	}
      errmsg = cgen_parse_address (cd, strp, opindex, reloc,
				   NULL, &value);
      if (**strp != ')')
	return "missing `)'";
      ++*strp;
      *valuep = value;
      return errmsg;
    }

  if (**strp == '%')
    return _("invalid %function() here");

  return parse_mep_alignu (cd, strp, opindex, valuep);
}

/* BEGIN LIGHTWEIGHT MACRO PROCESSOR.  */

#define MAXARGS 9

typedef struct
{
  char *name;
  char *expansion;
}  macro;

typedef struct
{
  const char *start;
  int len;
} arg;

macro macros[] =
{
  { "sizeof", "(`1.end + (- `1))"},
  { "startof", "(`1 | 0)" },
  { "align4", "(`1&(~3))"},
/*{ "hi", "(((`1+0x8000)>>16) & 0xffff)" },  */
/*{ "lo", "(`1 & 0xffff)" },  */
/*{ "sdaoff", "((`1-__sdabase) & 0x7f)"},  */
/*{ "tpoff", "((`1-__tpbase) & 0x7f)"},  */
  { 0,0 }
};

static char  * expand_string    (const char *, int);

static const char *
mep_cgen_expand_macros_and_parse_operand
  (CGEN_CPU_DESC, int, const char **, CGEN_FIELDS *);

static char *
str_append (char *dest, const char *input, int len)
{  
  char *new_dest;
  int oldlen;

  if (len == 0)
    return dest;
  /* printf("str_append: <<%s>>, <<%s>>, %d\n", dest, input, len); */
  oldlen = (dest ? strlen(dest) : 0);
  new_dest = realloc (dest, oldlen + len + 1);
  memset (new_dest + oldlen, 0, len + 1);
  return strncat (new_dest, input, len);
}

static macro *
lookup_macro (const char *name)
{
  macro *m;

  for (m = macros; m->name; ++m)
    if (strncmp (m->name, name, strlen(m->name)) == 0)
      return m;

  return 0;
}

static char *
expand_macro (arg *args, int narg, macro *mac)
{
  char *result = 0, *rescanned_result = 0;
  char *e = mac->expansion;
  char *mark = e;
  int arg = 0;

  /*  printf("expanding macro %s with %d args\n", mac->name, narg + 1); */
  while (*e)
    {
      if (*e == '`' && 
	  (*e+1) && 
	  ((*(e + 1) - '1') <= MAXARGS) &&
	  ((*(e + 1) - '1') <= narg))
	{
	  result = str_append (result, mark, e - mark);
	  arg = (*(e + 1) - '1');
	  /* printf("replacing `%d with %s\n", arg+1, args[arg].start); */
	  result = str_append (result, args[arg].start, args[arg].len);
	  ++e;
	  mark = e+1;
	}
      ++e;
    }

  if (mark != e)
    result = str_append (result, mark, e - mark);

  if (result)
    {
      rescanned_result = expand_string (result, 0);
      free (result);
      return rescanned_result;
    }
  else 
    return result;
}

#define IN_TEXT 0
#define IN_ARGS 1

static char *
expand_string (const char *in, int first_only)
{
  int num_expansions = 0;
  int depth = 0;
  int narg = -1;
  arg args[MAXARGS];
  int state = IN_TEXT;
  const char *mark = in;
  macro *macro = 0;

  char *expansion = 0;
  char *result = 0;

  while (*in)
    {
      switch (state)
	{
	case IN_TEXT:
	  if (*in == '%' && *(in + 1) && (!first_only || num_expansions == 0)) 
	    {	      
	      macro = lookup_macro (in + 1);
	      if (macro)
		{
		  /* printf("entering state %d at '%s'...\n", state, in); */
		  result = str_append (result, mark, in - mark);
		  mark = in;
		  in += 1 + strlen (macro->name);
		  while (*in == ' ') ++in;
		  if (*in != '(')
		    {
		      state = IN_TEXT;		      
		      macro = 0;
		    }
		  else
		    {
		      state = IN_ARGS;
		      narg = 0;
		      args[narg].start = in + 1;
		      args[narg].len = 0;
		      mark = in + 1;	      		      
		    }
		}
	    }
	  break;
	case IN_ARGS:
	  if (depth == 0)
	    {
	      switch (*in)
		{
		case ',':
		  narg++;
		  args[narg].start = (in + 1);
		  args[narg].len = 0;
		  break;
		case ')':
		  state = IN_TEXT;
		  /* printf("entering state %d at '%s'...\n", state, in); */
		  if (macro)
		    {
		      expansion = 0;
		      expansion = expand_macro (args, narg, macro);
		      num_expansions++;
		      if (expansion)
			{
			  result = str_append (result, expansion, strlen (expansion));
			  free (expansion);
			}
		    }
		  else
		    {
		      result = str_append (result, mark, in - mark);
		    }
		  macro = 0;
		  mark = in + 1;
		  break;
		case '(':
		  depth++;
		default:
		  args[narg].len++;
		  break;		  
		}
	    } 
	  else
	    {
	      if (*in == ')')
		depth--;
	      if (narg > -1)
		args[narg].len++;
	    }
	  
	}
      ++in;
    }
  
  if (mark != in)
    result = str_append (result, mark, in - mark);
  
  return result;
}

#undef IN_ARGS
#undef IN_TEXT
#undef MAXARGS


/* END LIGHTWEIGHT MACRO PROCESSOR.  */

const char * mep_cgen_parse_operand
  (CGEN_CPU_DESC, int, const char **, CGEN_FIELDS *);

const char *
mep_cgen_expand_macros_and_parse_operand (CGEN_CPU_DESC cd, int opindex,
					  const char ** strp_in, CGEN_FIELDS * fields)
{
  const char * errmsg = NULL;
  char *str = 0, *hold = 0;
  const char **strp = 0;

  /* Set up a new pointer to macro-expanded string.  */
  str = expand_string (*strp_in, 1);
  /* fprintf (stderr, " expanded <<%s>> to <<%s>>\n", *strp_in, str); */

  hold = str;
  strp = (const char **)(&str);

  errmsg = mep_cgen_parse_operand (cd, opindex, strp, fields);

  /* Now work out the advance.  */
  if (strlen (str) == 0)
    *strp_in += strlen (*strp_in);

  else
    {
      if (strstr (*strp_in, str))
	/* A macro-expansion was pulled off the front.  */
	*strp_in = strstr (*strp_in, str);  
      else
	/* A non-macro-expansion was pulled off the front.  */
	*strp_in += (str - hold); 
    }

  if (hold)
    free (hold);

  return errmsg;
}

#define CGEN_ASM_INIT_HOOK (cd->parse_operand = mep_cgen_expand_macros_and_parse_operand); 

/* -- dis.c */

const char * mep_cgen_parse_operand
  (CGEN_CPU_DESC, int, const char **, CGEN_FIELDS *);

/* Main entry point for operand parsing.

   This function is basically just a big switch statement.  Earlier versions
   used tables to look up the function to use, but
   - if the table contains both assembler and disassembler functions then
     the disassembler contains much of the assembler and vice-versa,
   - there's a lot of inlining possibilities as things grow,
   - using a switch statement avoids the function call overhead.

   This function could be moved into `parse_insn_normal', but keeping it
   separate makes clear the interface between `parse_insn_normal' and each of
   the handlers.  */

const char *
mep_cgen_parse_operand (CGEN_CPU_DESC cd,
			   int opindex,
			   const char ** strp,
			   CGEN_FIELDS * fields)
{
  const char * errmsg = NULL;
  /* Used by scalar operands that still need to be parsed.  */
  long junk ATTRIBUTE_UNUSED;

  switch (opindex)
    {
    case MEP_OPERAND_ADDR24A4 :
      errmsg = parse_mep_alignu (cd, strp, MEP_OPERAND_ADDR24A4, (unsigned long *) (& fields->f_24u8a4n));
      break;
    case MEP_OPERAND_CALLNUM :
      errmsg = cgen_parse_unsigned_integer (cd, strp, MEP_OPERAND_CALLNUM, (unsigned long *) (& fields->f_callnum));
      break;
    case MEP_OPERAND_CCCC :
      errmsg = cgen_parse_unsigned_integer (cd, strp, MEP_OPERAND_CCCC, (unsigned long *) (& fields->f_rm));
      break;
    case MEP_OPERAND_CCRN :
      errmsg = cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_ccr, & fields->f_ccrn);
      break;
    case MEP_OPERAND_CDISP8 :
      errmsg = cgen_parse_signed_integer (cd, strp, MEP_OPERAND_CDISP8, (long *) (& fields->f_8s24));
      break;
    case MEP_OPERAND_CDISP8A2 :
      errmsg = parse_mep_align (cd, strp, MEP_OPERAND_CDISP8A2, (long *) (& fields->f_8s24a2));
      break;
    case MEP_OPERAND_CDISP8A4 :
      errmsg = parse_mep_align (cd, strp, MEP_OPERAND_CDISP8A4, (long *) (& fields->f_8s24a4));
      break;
    case MEP_OPERAND_CDISP8A8 :
      errmsg = parse_mep_align (cd, strp, MEP_OPERAND_CDISP8A8, (long *) (& fields->f_8s24a8));
      break;
    case MEP_OPERAND_CIMM4 :
      errmsg = cgen_parse_unsigned_integer (cd, strp, MEP_OPERAND_CIMM4, (unsigned long *) (& fields->f_rn));
      break;
    case MEP_OPERAND_CIMM5 :
      errmsg = cgen_parse_unsigned_integer (cd, strp, MEP_OPERAND_CIMM5, (unsigned long *) (& fields->f_5u24));
      break;
    case MEP_OPERAND_CODE16 :
      errmsg = cgen_parse_unsigned_integer (cd, strp, MEP_OPERAND_CODE16, (unsigned long *) (& fields->f_16u16));
      break;
    case MEP_OPERAND_CODE24 :
      errmsg = cgen_parse_unsigned_integer (cd, strp, MEP_OPERAND_CODE24, (unsigned long *) (& fields->f_24u4n));
      break;
    case MEP_OPERAND_CP_FLAG :
      errmsg = cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_ccr, & junk);
      break;
    case MEP_OPERAND_CRN :
      errmsg = cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_cr, & fields->f_crn);
      break;
    case MEP_OPERAND_CRN64 :
      errmsg = cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_cr64, & fields->f_crn);
      break;
    case MEP_OPERAND_CRNX :
      errmsg = cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_cr, & fields->f_crnx);
      break;
    case MEP_OPERAND_CRNX64 :
      errmsg = cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_cr64, & fields->f_crnx);
      break;
    case MEP_OPERAND_CSRN :
      errmsg = parse_csrn (cd, strp, & mep_cgen_opval_h_csr, & fields->f_csrn);
      break;
    case MEP_OPERAND_CSRN_IDX :
      errmsg = cgen_parse_unsigned_integer (cd, strp, MEP_OPERAND_CSRN_IDX, (unsigned long *) (& fields->f_csrn));
      break;
    case MEP_OPERAND_DBG :
      errmsg = cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_csr, & junk);
      break;
    case MEP_OPERAND_DEPC :
      errmsg = cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_csr, & junk);
      break;
    case MEP_OPERAND_EPC :
      errmsg = cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_csr, & junk);
      break;
    case MEP_OPERAND_EXC :
      errmsg = cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_csr, & junk);
      break;
    case MEP_OPERAND_FMAX_CCRN :
      errmsg = parse_fmax_ccr (cd, strp, & mep_cgen_opval_h_ccr, & fields->f_fmax_4_4);
      break;
    case MEP_OPERAND_FMAX_FRD :
      errmsg = parse_fmax_cr (cd, strp, & mep_cgen_opval_h_cr, & fields->f_fmax_frd);
      break;
    case MEP_OPERAND_FMAX_FRD_INT :
      errmsg = parse_fmax_cr (cd, strp, & mep_cgen_opval_h_cr, & fields->f_fmax_frd);
      break;
    case MEP_OPERAND_FMAX_FRM :
      errmsg = parse_fmax_cr (cd, strp, & mep_cgen_opval_h_cr, & fields->f_fmax_frm);
      break;
    case MEP_OPERAND_FMAX_FRN :
      errmsg = parse_fmax_cr (cd, strp, & mep_cgen_opval_h_cr, & fields->f_fmax_frn);
      break;
    case MEP_OPERAND_FMAX_FRN_INT :
      errmsg = parse_fmax_cr (cd, strp, & mep_cgen_opval_h_cr, & fields->f_fmax_frn);
      break;
    case MEP_OPERAND_FMAX_RM :
      errmsg = cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_gpr, & fields->f_fmax_rm);
      break;
    case MEP_OPERAND_HI :
      errmsg = cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_csr, & junk);
      break;
    case MEP_OPERAND_LO :
      errmsg = cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_csr, & junk);
      break;
    case MEP_OPERAND_LP :
      errmsg = cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_csr, & junk);
      break;
    case MEP_OPERAND_MB0 :
      errmsg = cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_csr, & junk);
      break;
    case MEP_OPERAND_MB1 :
      errmsg = cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_csr, & junk);
      break;
    case MEP_OPERAND_ME0 :
      errmsg = cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_csr, & junk);
      break;
    case MEP_OPERAND_ME1 :
      errmsg = cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_csr, & junk);
      break;
    case MEP_OPERAND_NPC :
      errmsg = cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_csr, & junk);
      break;
    case MEP_OPERAND_OPT :
      errmsg = cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_csr, & junk);
      break;
    case MEP_OPERAND_PCABS24A2 :
      errmsg = parse_mep_alignu (cd, strp, MEP_OPERAND_PCABS24A2, (unsigned long *) (& fields->f_24u5a2n));
      break;
    case MEP_OPERAND_PCREL12A2 :
      errmsg = parse_mep_align (cd, strp, MEP_OPERAND_PCREL12A2, (long *) (& fields->f_12s4a2));
      break;
    case MEP_OPERAND_PCREL17A2 :
      errmsg = parse_mep_align (cd, strp, MEP_OPERAND_PCREL17A2, (long *) (& fields->f_17s16a2));
      break;
    case MEP_OPERAND_PCREL24A2 :
      errmsg = parse_mep_align (cd, strp, MEP_OPERAND_PCREL24A2, (long *) (& fields->f_24s5a2n));
      break;
    case MEP_OPERAND_PCREL8A2 :
      errmsg = parse_mep_align (cd, strp, MEP_OPERAND_PCREL8A2, (long *) (& fields->f_8s8a2));
      break;
    case MEP_OPERAND_PSW :
      errmsg = cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_csr, & junk);
      break;
    case MEP_OPERAND_R0 :
      errmsg = cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_gpr, & junk);
      break;
    case MEP_OPERAND_R1 :
      errmsg = cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_gpr, & junk);
      break;
    case MEP_OPERAND_RL :
      errmsg = cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_gpr, & fields->f_rl);
      break;
    case MEP_OPERAND_RM :
      errmsg = cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_gpr, & fields->f_rm);
      break;
    case MEP_OPERAND_RMA :
      errmsg = cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_gpr, & fields->f_rm);
      break;
    case MEP_OPERAND_RN :
      errmsg = cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_gpr, & fields->f_rn);
      break;
    case MEP_OPERAND_RN3 :
      errmsg = cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_gpr, & fields->f_rn3);
      break;
    case MEP_OPERAND_RN3C :
      errmsg = cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_gpr, & fields->f_rn3);
      break;
    case MEP_OPERAND_RN3L :
      errmsg = cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_gpr, & fields->f_rn3);
      break;
    case MEP_OPERAND_RN3S :
      errmsg = cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_gpr, & fields->f_rn3);
      break;
    case MEP_OPERAND_RN3UC :
      errmsg = cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_gpr, & fields->f_rn3);
      break;
    case MEP_OPERAND_RN3UL :
      errmsg = cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_gpr, & fields->f_rn3);
      break;
    case MEP_OPERAND_RN3US :
      errmsg = cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_gpr, & fields->f_rn3);
      break;
    case MEP_OPERAND_RNC :
      errmsg = cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_gpr, & fields->f_rn);
      break;
    case MEP_OPERAND_RNL :
      errmsg = cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_gpr, & fields->f_rn);
      break;
    case MEP_OPERAND_RNS :
      errmsg = cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_gpr, & fields->f_rn);
      break;
    case MEP_OPERAND_RNUC :
      errmsg = cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_gpr, & fields->f_rn);
      break;
    case MEP_OPERAND_RNUL :
      errmsg = cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_gpr, & fields->f_rn);
      break;
    case MEP_OPERAND_RNUS :
      errmsg = cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_gpr, & fields->f_rn);
      break;
    case MEP_OPERAND_SAR :
      errmsg = cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_csr, & junk);
      break;
    case MEP_OPERAND_SDISP16 :
      errmsg = parse_signed16 (cd, strp, MEP_OPERAND_SDISP16, (long *) (& fields->f_16s16));
      break;
    case MEP_OPERAND_SIMM16 :
      errmsg = parse_signed16 (cd, strp, MEP_OPERAND_SIMM16, (long *) (& fields->f_16s16));
      break;
    case MEP_OPERAND_SIMM6 :
      errmsg = cgen_parse_signed_integer (cd, strp, MEP_OPERAND_SIMM6, (long *) (& fields->f_6s8));
      break;
    case MEP_OPERAND_SIMM8 :
      errmsg = cgen_parse_signed_integer (cd, strp, MEP_OPERAND_SIMM8, (long *) (& fields->f_8s8));
      break;
    case MEP_OPERAND_SP :
      errmsg = cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_gpr, & junk);
      break;
    case MEP_OPERAND_SPR :
      errmsg = parse_spreg (cd, strp, & mep_cgen_opval_h_gpr, & junk);
      break;
    case MEP_OPERAND_TP :
      errmsg = cgen_parse_keyword (cd, strp, & mep_cgen_opval_h_gpr, & junk);
      break;
    case MEP_OPERAND_TPR :
      errmsg = parse_tpreg (cd, strp, & mep_cgen_opval_h_gpr, & junk);
      break;
    case MEP_OPERAND_UDISP2 :
      errmsg = cgen_parse_signed_integer (cd, strp, MEP_OPERAND_UDISP2, (long *) (& fields->f_2u6));
      break;
    case MEP_OPERAND_UDISP7 :
      errmsg = parse_unsigned7 (cd, strp, MEP_OPERAND_UDISP7, (unsigned long *) (& fields->f_7u9));
      break;
    case MEP_OPERAND_UDISP7A2 :
      errmsg = parse_unsigned7 (cd, strp, MEP_OPERAND_UDISP7A2, (unsigned long *) (& fields->f_7u9a2));
      break;
    case MEP_OPERAND_UDISP7A4 :
      errmsg = parse_unsigned7 (cd, strp, MEP_OPERAND_UDISP7A4, (unsigned long *) (& fields->f_7u9a4));
      break;
    case MEP_OPERAND_UIMM16 :
      errmsg = parse_unsigned16 (cd, strp, MEP_OPERAND_UIMM16, (unsigned long *) (& fields->f_16u16));
      break;
    case MEP_OPERAND_UIMM2 :
      errmsg = cgen_parse_unsigned_integer (cd, strp, MEP_OPERAND_UIMM2, (unsigned long *) (& fields->f_2u10));
      break;
    case MEP_OPERAND_UIMM24 :
      errmsg = cgen_parse_unsigned_integer (cd, strp, MEP_OPERAND_UIMM24, (unsigned long *) (& fields->f_24u8n));
      break;
    case MEP_OPERAND_UIMM3 :
      errmsg = cgen_parse_unsigned_integer (cd, strp, MEP_OPERAND_UIMM3, (unsigned long *) (& fields->f_3u5));
      break;
    case MEP_OPERAND_UIMM4 :
      errmsg = cgen_parse_unsigned_integer (cd, strp, MEP_OPERAND_UIMM4, (unsigned long *) (& fields->f_4u8));
      break;
    case MEP_OPERAND_UIMM5 :
      errmsg = cgen_parse_unsigned_integer (cd, strp, MEP_OPERAND_UIMM5, (unsigned long *) (& fields->f_5u8));
      break;
    case MEP_OPERAND_UIMM7A4 :
      errmsg = parse_mep_alignu (cd, strp, MEP_OPERAND_UIMM7A4, (unsigned long *) (& fields->f_7u9a4));
      break;
    case MEP_OPERAND_ZERO :
      errmsg = parse_zero (cd, strp, MEP_OPERAND_ZERO, (long *) (& junk));
      break;

    default :
      /* xgettext:c-format */
      fprintf (stderr, _("Unrecognized field %d while parsing.\n"), opindex);
      abort ();
  }

  return errmsg;
}

cgen_parse_fn * const mep_cgen_parse_handlers[] = 
{
  parse_insn_normal,
};

void
mep_cgen_init_asm (CGEN_CPU_DESC cd)
{
  mep_cgen_init_opcode_table (cd);
  mep_cgen_init_ibld_table (cd);
  cd->parse_handlers = & mep_cgen_parse_handlers[0];
  cd->parse_operand = mep_cgen_parse_operand;
#ifdef CGEN_ASM_INIT_HOOK
CGEN_ASM_INIT_HOOK
#endif
}



/* Regex construction routine.

   This translates an opcode syntax string into a regex string,
   by replacing any non-character syntax element (such as an
   opcode) with the pattern '.*'

   It then compiles the regex and stores it in the opcode, for
   later use by mep_cgen_assemble_insn

   Returns NULL for success, an error message for failure.  */

char * 
mep_cgen_build_insn_regex (CGEN_INSN *insn)
{  
  CGEN_OPCODE *opc = (CGEN_OPCODE *) CGEN_INSN_OPCODE (insn);
  const char *mnem = CGEN_INSN_MNEMONIC (insn);
  char rxbuf[CGEN_MAX_RX_ELEMENTS];
  char *rx = rxbuf;
  const CGEN_SYNTAX_CHAR_TYPE *syn;
  int reg_err;

  syn = CGEN_SYNTAX_STRING (CGEN_OPCODE_SYNTAX (opc));

  /* Mnemonics come first in the syntax string.  */
  if (! CGEN_SYNTAX_MNEMONIC_P (* syn))
    return _("missing mnemonic in syntax string");
  ++syn;

  /* Generate a case sensitive regular expression that emulates case
     insensitive matching in the "C" locale.  We cannot generate a case
     insensitive regular expression because in Turkish locales, 'i' and 'I'
     are not equal modulo case conversion.  */

  /* Copy the literal mnemonic out of the insn.  */
  for (; *mnem; mnem++)
    {
      char c = *mnem;

      if (ISALPHA (c))
	{
	  *rx++ = '[';
	  *rx++ = TOLOWER (c);
	  *rx++ = TOUPPER (c);
	  *rx++ = ']';
	}
      else
	*rx++ = c;
    }

  /* Copy any remaining literals from the syntax string into the rx.  */
  for(; * syn != 0 && rx <= rxbuf + (CGEN_MAX_RX_ELEMENTS - 7 - 4); ++syn)
    {
      if (CGEN_SYNTAX_CHAR_P (* syn)) 
	{
	  char c = CGEN_SYNTAX_CHAR (* syn);

	  switch (c) 
	    {
	      /* Escape any regex metacharacters in the syntax.  */
	    case '.': case '[': case '\\': 
	    case '*': case '^': case '$': 

#ifdef CGEN_ESCAPE_EXTENDED_REGEX
	    case '?': case '{': case '}': 
	    case '(': case ')': case '*':
	    case '|': case '+': case ']':
#endif
	      *rx++ = '\\';
	      *rx++ = c;
	      break;

	    default:
	      if (ISALPHA (c))
		{
		  *rx++ = '[';
		  *rx++ = TOLOWER (c);
		  *rx++ = TOUPPER (c);
		  *rx++ = ']';
		}
	      else
		*rx++ = c;
	      break;
	    }
	}
      else
	{
	  /* Replace non-syntax fields with globs.  */
	  *rx++ = '.';
	  *rx++ = '*';
	}
    }

  /* Trailing whitespace ok.  */
  * rx++ = '['; 
  * rx++ = ' '; 
  * rx++ = '\t'; 
  * rx++ = ']'; 
  * rx++ = '*'; 

  /* But anchor it after that.  */
  * rx++ = '$'; 
  * rx = '\0';

  CGEN_INSN_RX (insn) = xmalloc (sizeof (regex_t));
  reg_err = regcomp ((regex_t *) CGEN_INSN_RX (insn), rxbuf, REG_NOSUB);

  if (reg_err == 0) 
    return NULL;
  else
    {
      static char msg[80];

      regerror (reg_err, (regex_t *) CGEN_INSN_RX (insn), msg, 80);
      regfree ((regex_t *) CGEN_INSN_RX (insn));
      free (CGEN_INSN_RX (insn));
      (CGEN_INSN_RX (insn)) = NULL;
      return msg;
    }
}


/* Default insn parser.

   The syntax string is scanned and operands are parsed and stored in FIELDS.
   Relocs are queued as we go via other callbacks.

   ??? Note that this is currently an all-or-nothing parser.  If we fail to
   parse the instruction, we return 0 and the caller will start over from
   the beginning.  Backtracking will be necessary in parsing subexpressions,
   but that can be handled there.  Not handling backtracking here may get
   expensive in the case of the m68k.  Deal with later.

   Returns NULL for success, an error message for failure.  */

static const char *
parse_insn_normal (CGEN_CPU_DESC cd,
		   const CGEN_INSN *insn,
		   const char **strp,
		   CGEN_FIELDS *fields)
{
  /* ??? Runtime added insns not handled yet.  */
  const CGEN_SYNTAX *syntax = CGEN_INSN_SYNTAX (insn);
  const char *str = *strp;
  const char *errmsg;
  const char *p;
  const CGEN_SYNTAX_CHAR_TYPE * syn;
#ifdef CGEN_MNEMONIC_OPERANDS
  /* FIXME: wip */
  int past_opcode_p;
#endif

  /* For now we assume the mnemonic is first (there are no leading operands).
     We can parse it without needing to set up operand parsing.
     GAS's input scrubber will ensure mnemonics are lowercase, but we may
     not be called from GAS.  */
  p = CGEN_INSN_MNEMONIC (insn);
  while (*p && TOLOWER (*p) == TOLOWER (*str))
    ++p, ++str;

  if (* p)
    return _("unrecognized instruction");

#ifndef CGEN_MNEMONIC_OPERANDS
  if (* str && ! ISSPACE (* str))
    return _("unrecognized instruction");
#endif

  CGEN_INIT_PARSE (cd);
  cgen_init_parse_operand (cd);
#ifdef CGEN_MNEMONIC_OPERANDS
  past_opcode_p = 0;
#endif

  /* We don't check for (*str != '\0') here because we want to parse
     any trailing fake arguments in the syntax string.  */
  syn = CGEN_SYNTAX_STRING (syntax);

  /* Mnemonics come first for now, ensure valid string.  */
  if (! CGEN_SYNTAX_MNEMONIC_P (* syn))
    abort ();

  ++syn;

  while (* syn != 0)
    {
      /* Non operand chars must match exactly.  */
      if (CGEN_SYNTAX_CHAR_P (* syn))
	{
	  /* FIXME: While we allow for non-GAS callers above, we assume the
	     first char after the mnemonic part is a space.  */
	  /* FIXME: We also take inappropriate advantage of the fact that
	     GAS's input scrubber will remove extraneous blanks.  */
	  if (TOLOWER (*str) == TOLOWER (CGEN_SYNTAX_CHAR (* syn)))
	    {
#ifdef CGEN_MNEMONIC_OPERANDS
	      if (CGEN_SYNTAX_CHAR(* syn) == ' ')
		past_opcode_p = 1;
#endif
	      ++ syn;
	      ++ str;
	    }
	  else if (*str)
	    {
	      /* Syntax char didn't match.  Can't be this insn.  */
	      static char msg [80];

	      /* xgettext:c-format */
	      sprintf (msg, _("syntax error (expected char `%c', found `%c')"),
		       CGEN_SYNTAX_CHAR(*syn), *str);
	      return msg;
	    }
	  else
	    {
	      /* Ran out of input.  */
	      static char msg [80];

	      /* xgettext:c-format */
	      sprintf (msg, _("syntax error (expected char `%c', found end of instruction)"),
		       CGEN_SYNTAX_CHAR(*syn));
	      return msg;
	    }
	  continue;
	}

      /* We have an operand of some sort.  */
      errmsg = cd->parse_operand (cd, CGEN_SYNTAX_FIELD (*syn),
					  &str, fields);
      if (errmsg)
	return errmsg;

      /* Done with this operand, continue with next one.  */
      ++ syn;
    }

  /* If we're at the end of the syntax string, we're done.  */
  if (* syn == 0)
    {
      /* FIXME: For the moment we assume a valid `str' can only contain
	 blanks now.  IE: We needn't try again with a longer version of
	 the insn and it is assumed that longer versions of insns appear
	 before shorter ones (eg: lsr r2,r3,1 vs lsr r2,r3).  */
      while (ISSPACE (* str))
	++ str;

      if (* str != '\0')
	return _("junk at end of line"); /* FIXME: would like to include `str' */

      return NULL;
    }

  /* We couldn't parse it.  */
  return _("unrecognized instruction");
}

/* Main entry point.
   This routine is called for each instruction to be assembled.
   STR points to the insn to be assembled.
   We assume all necessary tables have been initialized.
   The assembled instruction, less any fixups, is stored in BUF.
   Remember that if CGEN_INT_INSN_P then BUF is an int and thus the value
   still needs to be converted to target byte order, otherwise BUF is an array
   of bytes in target byte order.
   The result is a pointer to the insn's entry in the opcode table,
   or NULL if an error occured (an error message will have already been
   printed).

   Note that when processing (non-alias) macro-insns,
   this function recurses.

   ??? It's possible to make this cpu-independent.
   One would have to deal with a few minor things.
   At this point in time doing so would be more of a curiosity than useful
   [for example this file isn't _that_ big], but keeping the possibility in
   mind helps keep the design clean.  */

const CGEN_INSN *
mep_cgen_assemble_insn (CGEN_CPU_DESC cd,
			   const char *str,
			   CGEN_FIELDS *fields,
			   CGEN_INSN_BYTES_PTR buf,
			   char **errmsg)
{
  const char *start;
  CGEN_INSN_LIST *ilist;
  const char *parse_errmsg = NULL;
  const char *insert_errmsg = NULL;
  int recognized_mnemonic = 0;

  /* Skip leading white space.  */
  while (ISSPACE (* str))
    ++ str;

  /* The instructions are stored in hashed lists.
     Get the first in the list.  */
  ilist = CGEN_ASM_LOOKUP_INSN (cd, str);

  /* Keep looking until we find a match.  */
  start = str;
  for ( ; ilist != NULL ; ilist = CGEN_ASM_NEXT_INSN (ilist))
    {
      const CGEN_INSN *insn = ilist->insn;
      recognized_mnemonic = 1;

#ifdef CGEN_VALIDATE_INSN_SUPPORTED 
      /* Not usually needed as unsupported opcodes
	 shouldn't be in the hash lists.  */
      /* Is this insn supported by the selected cpu?  */
      if (! mep_cgen_insn_supported (cd, insn))
	continue;
#endif
      /* If the RELAXED attribute is set, this is an insn that shouldn't be
	 chosen immediately.  Instead, it is used during assembler/linker
	 relaxation if possible.  */
      if (CGEN_INSN_ATTR_VALUE (insn, CGEN_INSN_RELAXED) != 0)
	continue;

      str = start;

      /* Skip this insn if str doesn't look right lexically.  */
      if (CGEN_INSN_RX (insn) != NULL &&
	  regexec ((regex_t *) CGEN_INSN_RX (insn), str, 0, NULL, 0) == REG_NOMATCH)
	continue;

      /* Allow parse/insert handlers to obtain length of insn.  */
      CGEN_FIELDS_BITSIZE (fields) = CGEN_INSN_BITSIZE (insn);

      parse_errmsg = CGEN_PARSE_FN (cd, insn) (cd, insn, & str, fields);
      if (parse_errmsg != NULL)
	continue;

      /* ??? 0 is passed for `pc'.  */
      insert_errmsg = CGEN_INSERT_FN (cd, insn) (cd, insn, fields, buf,
						 (bfd_vma) 0);
      if (insert_errmsg != NULL)
        continue;

      /* It is up to the caller to actually output the insn and any
         queued relocs.  */
      return insn;
    }

  {
    static char errbuf[150];
#ifdef CGEN_VERBOSE_ASSEMBLER_ERRORS
    const char *tmp_errmsg;

    /* If requesting verbose error messages, use insert_errmsg.
       Failing that, use parse_errmsg.  */
    tmp_errmsg = (insert_errmsg ? insert_errmsg :
		  parse_errmsg ? parse_errmsg :
		  recognized_mnemonic ?
		  _("unrecognized form of instruction") :
		  _("unrecognized instruction"));

    if (strlen (start) > 50)
      /* xgettext:c-format */
      sprintf (errbuf, "%s `%.50s...'", tmp_errmsg, start);
    else 
      /* xgettext:c-format */
      sprintf (errbuf, "%s `%.50s'", tmp_errmsg, start);
#else
    if (strlen (start) > 50)
      /* xgettext:c-format */
      sprintf (errbuf, _("bad instruction `%.50s...'"), start);
    else 
      /* xgettext:c-format */
      sprintf (errbuf, _("bad instruction `%.50s'"), start);
#endif
      
    *errmsg = errbuf;
    return NULL;
  }
}
