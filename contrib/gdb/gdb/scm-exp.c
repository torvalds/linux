/* Scheme/Guile language support routines for GDB, the GNU debugger.

   Copyright 1995, 1996, 2000, 2003 Free Software Foundation, Inc.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#include "defs.h"
#include "symtab.h"
#include "gdbtypes.h"
#include "expression.h"
#include "parser-defs.h"
#include "language.h"
#include "value.h"
#include "c-lang.h"
#include "scm-lang.h"
#include "scm-tags.h"

#define USE_EXPRSTRING 0

static void scm_lreadparen (int);
static int scm_skip_ws (void);
static void scm_read_token (int, int);
static LONGEST scm_istring2number (char *, int, int);
static LONGEST scm_istr2int (char *, int, int);
static void scm_lreadr (int);

static LONGEST
scm_istr2int (char *str, int len, int radix)
{
  int i = 0;
  LONGEST inum = 0;
  int c;
  int sign = 0;

  if (0 >= len)
    return SCM_BOOL_F;		/* zero scm_length */
  switch (str[0])
    {				/* leading sign */
    case '-':
    case '+':
      sign = str[0];
      if (++i == len)
	return SCM_BOOL_F;	/* bad if lone `+' or `-' */
    }
  do
    {
      switch (c = str[i++])
	{
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
	  c = c - '0';
	  goto accumulate;
	case 'A':
	case 'B':
	case 'C':
	case 'D':
	case 'E':
	case 'F':
	  c = c - 'A' + 10;
	  goto accumulate;
	case 'a':
	case 'b':
	case 'c':
	case 'd':
	case 'e':
	case 'f':
	  c = c - 'a' + 10;
	accumulate:
	  if (c >= radix)
	    return SCM_BOOL_F;	/* bad digit for radix */
	  inum *= radix;
	  inum += c;
	  break;
	default:
	  return SCM_BOOL_F;	/* not a digit */
	}
    }
  while (i < len);
  if (sign == '-')
    inum = -inum;
  return SCM_MAKINUM (inum);
}

static LONGEST
scm_istring2number (char *str, int len, int radix)
{
  int i = 0;
  char ex = 0;
  char ex_p = 0, rx_p = 0;	/* Only allow 1 exactness and 1 radix prefix */
#if 0
  SCM res;
#endif
  if (len == 1)
    if (*str == '+' || *str == '-')	/* Catches lone `+' and `-' for speed */
      return SCM_BOOL_F;

  while ((len - i) >= 2 && str[i] == '#' && ++i)
    switch (str[i++])
      {
      case 'b':
      case 'B':
	if (rx_p++)
	  return SCM_BOOL_F;
	radix = 2;
	break;
      case 'o':
      case 'O':
	if (rx_p++)
	  return SCM_BOOL_F;
	radix = 8;
	break;
      case 'd':
      case 'D':
	if (rx_p++)
	  return SCM_BOOL_F;
	radix = 10;
	break;
      case 'x':
      case 'X':
	if (rx_p++)
	  return SCM_BOOL_F;
	radix = 16;
	break;
      case 'i':
      case 'I':
	if (ex_p++)
	  return SCM_BOOL_F;
	ex = 2;
	break;
      case 'e':
      case 'E':
	if (ex_p++)
	  return SCM_BOOL_F;
	ex = 1;
	break;
      default:
	return SCM_BOOL_F;
      }

  switch (ex)
    {
    case 1:
      return scm_istr2int (&str[i], len - i, radix);
    case 0:
      return scm_istr2int (&str[i], len - i, radix);
#if 0
      if NFALSEP
	(res) return res;
#ifdef FLOATS
    case 2:
      return scm_istr2flo (&str[i], len - i, radix);
#endif
#endif
    }
  return SCM_BOOL_F;
}

static void
scm_read_token (int c, int weird)
{
  while (1)
    {
      c = *lexptr++;
      switch (c)
	{
	case '[':
	case ']':
	case '(':
	case ')':
	case '\"':
	case ';':
	case ' ':
	case '\t':
	case '\r':
	case '\f':
	case '\n':
	  if (weird)
	    goto default_case;
	case '\0':		/* End of line */
	eof_case:
	  --lexptr;
	  return;
	case '\\':
	  if (!weird)
	    goto default_case;
	  else
	    {
	      c = *lexptr++;
	      if (c == '\0')
		goto eof_case;
	      else
		goto default_case;
	    }
	case '}':
	  if (!weird)
	    goto default_case;

	  c = *lexptr++;
	  if (c == '#')
	    return;
	  else
	    {
	      --lexptr;
	      c = '}';
	      goto default_case;
	    }

	default:
	default_case:
	  ;
	}
    }
}

static int
scm_skip_ws (void)
{
  int c;
  while (1)
    switch ((c = *lexptr++))
      {
      case '\0':
      goteof:
	return c;
      case ';':
      lp:
	switch ((c = *lexptr++))
	  {
	  case '\0':
	    goto goteof;
	  default:
	    goto lp;
	  case '\n':
	    break;
	  }
      case ' ':
      case '\t':
      case '\r':
      case '\f':
      case '\n':
	break;
      default:
	return c;
      }
}

static void
scm_lreadparen (int skipping)
{
  for (;;)
    {
      int c = scm_skip_ws ();
      if (')' == c || ']' == c)
	return;
      --lexptr;
      if (c == '\0')
	error ("missing close paren");
      scm_lreadr (skipping);
    }
}

static void
scm_lreadr (int skipping)
{
  int c, j;
  struct stoken str;
  LONGEST svalue = 0;
tryagain:
  c = *lexptr++;
  switch (c)
    {
    case '\0':
      lexptr--;
      return;
    case '[':
    case '(':
      scm_lreadparen (skipping);
      return;
    case ']':
    case ')':
      error ("unexpected #\\%c", c);
      goto tryagain;
    case '\'':
    case '`':
      str.ptr = lexptr - 1;
      scm_lreadr (skipping);
      if (!skipping)
	{
	  struct value *val = scm_evaluate_string (str.ptr, lexptr - str.ptr);
	  if (!is_scmvalue_type (VALUE_TYPE (val)))
	    error ("quoted scm form yields non-SCM value");
	  svalue = extract_signed_integer (VALUE_CONTENTS (val),
					   TYPE_LENGTH (VALUE_TYPE (val)));
	  goto handle_immediate;
	}
      return;
    case ',':
      c = *lexptr++;
      if ('@' != c)
	lexptr--;
      scm_lreadr (skipping);
      return;
    case '#':
      c = *lexptr++;
      switch (c)
	{
	case '[':
	case '(':
	  scm_lreadparen (skipping);
	  return;
	case 't':
	case 'T':
	  svalue = SCM_BOOL_T;
	  goto handle_immediate;
	case 'f':
	case 'F':
	  svalue = SCM_BOOL_F;
	  goto handle_immediate;
	case 'b':
	case 'B':
	case 'o':
	case 'O':
	case 'd':
	case 'D':
	case 'x':
	case 'X':
	case 'i':
	case 'I':
	case 'e':
	case 'E':
	  lexptr--;
	  c = '#';
	  goto num;
	case '*':		/* bitvector */
	  scm_read_token (c, 0);
	  return;
	case '{':
	  scm_read_token (c, 1);
	  return;
	case '\\':		/* character */
	  c = *lexptr++;
	  scm_read_token (c, 0);
	  return;
	case '|':
	  j = 1;		/* here j is the comment nesting depth */
	lp:
	  c = *lexptr++;
	lpc:
	  switch (c)
	    {
	    case '\0':
	      error ("unbalanced comment");
	    default:
	      goto lp;
	    case '|':
	      if ('#' != (c = *lexptr++))
		goto lpc;
	      if (--j)
		goto lp;
	      break;
	    case '#':
	      if ('|' != (c = *lexptr++))
		goto lpc;
	      ++j;
	      goto lp;
	    }
	  goto tryagain;
	case '.':
	default:
#if 0
	callshrp:
#endif
	  scm_lreadr (skipping);
	  return;
	}
    case '\"':
      while ('\"' != (c = *lexptr++))
	{
	  if (c == '\\')
	    switch (c = *lexptr++)
	      {
	      case '\0':
		error ("non-terminated string literal");
	      case '\n':
		continue;
	      case '0':
	      case 'f':
	      case 'n':
	      case 'r':
	      case 't':
	      case 'a':
	      case 'v':
		break;
	      }
	}
      return;
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
    case '.':
    case '-':
    case '+':
    num:
      {
	str.ptr = lexptr - 1;
	scm_read_token (c, 0);
	if (!skipping)
	  {
	    svalue = scm_istring2number (str.ptr, lexptr - str.ptr, 10);
	    if (svalue != SCM_BOOL_F)
	      goto handle_immediate;
	    goto tok;
	  }
      }
      return;
    case ':':
      scm_read_token ('-', 0);
      return;
#if 0
    do_symbol:
#endif
    default:
      str.ptr = lexptr - 1;
      scm_read_token (c, 0);
    tok:
      if (!skipping)
	{
	  str.length = lexptr - str.ptr;
	  if (str.ptr[0] == '$')
	    {
	      write_dollar_variable (str);
	      return;
	    }
	  write_exp_elt_opcode (OP_NAME);
	  write_exp_string (str);
	  write_exp_elt_opcode (OP_NAME);
	}
      return;
    }
handle_immediate:
  if (!skipping)
    {
      write_exp_elt_opcode (OP_LONG);
      write_exp_elt_type (builtin_type_scm);
      write_exp_elt_longcst (svalue);
      write_exp_elt_opcode (OP_LONG);
    }
}

int
scm_parse (void)
{
  char *start;
  while (*lexptr == ' ')
    lexptr++;
  start = lexptr;
  scm_lreadr (USE_EXPRSTRING);
#if USE_EXPRSTRING
  str.length = lexptr - start;
  str.ptr = start;
  write_exp_elt_opcode (OP_EXPRSTRING);
  write_exp_string (str);
  write_exp_elt_opcode (OP_EXPRSTRING);
#endif
  return 0;
}
