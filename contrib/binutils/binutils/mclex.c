/* mclex.c -- lexer for Windows mc files parser.
   Copyright 2007
   Free Software Foundation, Inc.

   Written by Kai Tietz, Onevision.

   This file is part of GNU Binutils.

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
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

/* This is a lexer used by the Windows rc file parser.
   It basically just recognized a bunch of keywords.  */

#include "sysdep.h"
#include "bfd.h"
#include "bucomm.h"
#include "libiberty.h"
#include "safe-ctype.h"
#include "windmc.h"
#include "mcparse.h"

#include <assert.h>

/* Exported globals.  */
bfd_boolean mclex_want_nl = FALSE;
bfd_boolean mclex_want_line = FALSE;
bfd_boolean mclex_want_filename = FALSE;

/* Local globals.  */
static unichar *input_stream = NULL;
static unichar *input_stream_pos = NULL;
static int input_line = 1;
static const char *input_filename = NULL;

void
mc_set_content (const unichar *src)
{
  if (!src)
    return;
  input_stream = input_stream_pos = unichar_dup (src);
}

void
mc_set_inputfile (const char *name)
{
  if (! name || *name == 0)
    input_filename = "-";
  else
    {
      const char *s1 = strrchr (name, '/');
      const char *s2 = strrchr (name, '\\');

      if (! s1)
	s1 = s2;
      if (s1 && s2 && s1 < s2)
	s1 = s2;
      if (! s1)
	s1 = name;
      else
	s1++;
      s1 = xstrdup (s1);
      input_filename = s1;
    }
}

static void
show_msg (const char *kind, const char *msg, va_list argp)
{
  fprintf (stderr, "In %s at line %d: %s: ", input_filename, input_line, kind);
  vfprintf (stderr, msg, argp);
  fprintf (stderr, ".\n");
}

void
mc_warn (const char *s, ...)
{
  va_list argp;
  va_start (argp, s);
  show_msg ("warning", s, argp);
  va_end (argp);
}

void
mc_fatal (const char *s, ...)
{
  va_list argp;
  va_start (argp, s);
  show_msg ("fatal", s, argp);
  va_end (argp);
  xexit (1);
}


int
yyerror (const char *s, ...)
{
  va_list argp;
  va_start (argp, s);
  show_msg ("parser", s, argp);
  va_end (argp);
  return 1;
}

static unichar *
get_diff (unichar *end, unichar *start)
{
  unichar *ret;
  unichar save = *end;

  *end = 0;
  ret = unichar_dup (start);
  *end = save;
  return ret;
}

static rc_uint_type
parse_digit (unichar ch)
{
  rc_uint_type base = 10, v = 0, c;

  if (ch == '0')
    {
      base = 8;
      switch (input_stream_pos[0])
	{
	case 'x': case 'X': base = 16; input_stream_pos++; break;
	case 'o': case 'O': base = 8; input_stream_pos++; break;
	case 'b': case 'B': base = 2; input_stream_pos++; break;
	}
    }
  else
    v = (rc_uint_type) (ch - '0');

  while ((ch = input_stream_pos[0]) != 0)
    {
      if (ch >= 'A' && ch <= 'F')
	c = (rc_uint_type) (ch - 'A') + 10;
      else if (ch >= 'a' && ch <= 'f')
	c = (rc_uint_type) (ch - 'a') + 10;
      else if (ch >= '0' && ch <= '9')
	c = (rc_uint_type) (ch - '0');
      else
	break;
      v *= base;
      v += c;
      ++input_stream_pos;
    }
  if (input_stream_pos[0] == 'U' || input_stream_pos[0] == 'u')
    input_stream_pos++;
  if (input_stream_pos[0] == 'L' || input_stream_pos[0] == 'l')
    input_stream_pos++;
  if (input_stream_pos[0] == 'L' || input_stream_pos[0] == 'l')
    input_stream_pos++;
  return v;
}

static mc_keyword *keyword_top = NULL;

const mc_keyword *
enum_facility (int e)
{
  mc_keyword *h = keyword_top;

  while (h != NULL)
    {
      while (h && strcmp (h->group_name, "facility") != 0)
	h = h->next;
      if (e == 0)
	return h;
      --e;
      if (h)
	h = h->next;
    }
  return h;
}

const mc_keyword *
enum_severity (int e)
{
  mc_keyword *h = keyword_top;

  while (h != NULL)
    {
      while (h && strcmp (h->group_name, "severity") != 0)
	h = h->next;
      if (e == 0)
	return h;
      --e;
      if (h)
	h = h->next;
    }
  return h;
}

static void
mc_add_keyword_ascii (const char *sz, int rid, const char *grp, rc_uint_type nv, const char *sv)
{
  unichar *usz, *usv = NULL;
  rc_uint_type usz_len;

  unicode_from_codepage (&usz_len, &usz, sz, CP_ACP);
  if (sv)
    unicode_from_codepage (&usz_len, &usv, sv, CP_ACP);
  mc_add_keyword (usz, rid, grp, nv, usv);
}

void
mc_add_keyword (unichar *usz, int rid, const char *grp, rc_uint_type nv, unichar *sv)
{
  mc_keyword *p, *c, *n;
  size_t len = unichar_len (usz);

  c = keyword_top;
  p = NULL;
  while (c != NULL)
    {
      if (c->len > len)
	break;
      if (c->len == len)
	{
	  int e = memcmp (usz, c->usz, len * sizeof (unichar));

	  if (e < 0)
	    break;
	  if (! e)
	    {
	      if (! strcmp (grp, "keyword") || strcmp (c->group_name, grp) != 0)
		fatal (_("Duplicate symbol entered into keyword list."));
	      c->rid = rid;
	      c->nval = nv;
	      c->sval = (!sv ? NULL : unichar_dup (sv));
	      if (! strcmp (grp, "language"))
		{
		  const wind_language_t *lag = wind_find_language_by_id ((unsigned) nv);

		  if (lag == NULL)
		    fatal ("Language ident 0x%lx is not resolvable.\n", (long) nv);
		  memcpy (&c->lang_info, lag, sizeof (*lag));
		}
	      return;
	    }
	}
      c = (p = c)->next;
    }
  n = xmalloc (sizeof (mc_keyword));
  n->next = c;
  n->len = len;
  n->group_name = grp;
  n->usz = usz;
  n->rid = rid;
  n->nval = nv;
  n->sval = (!sv ? NULL : unichar_dup (sv));
  if (! strcmp (grp, "language"))
    {
      const wind_language_t *lag = wind_find_language_by_id ((unsigned) nv);
      if (lag == NULL)
	fatal ("Language ident 0x%lx is not resolvable.\n", (long) nv);
      memcpy (&n->lang_info, lag, sizeof (*lag));
    }
  if (! p)
    keyword_top = n;
  else
    p->next = n;
}

static int
mc_token (const unichar *t, size_t len)
{
  static int was_init = 0;
  mc_keyword *k;

  if (! was_init)
    {
      was_init = 1;
      mc_add_keyword_ascii ("OutputBase", MCOUTPUTBASE, "keyword", 0, NULL);
      mc_add_keyword_ascii ("MessageIdTypedef", MCMESSAGEIDTYPEDEF, "keyword", 0, NULL);
      mc_add_keyword_ascii ("SeverityNames", MCSEVERITYNAMES, "keyword", 0, NULL);
      mc_add_keyword_ascii ("FacilityNames", MCFACILITYNAMES, "keyword", 0, NULL);
      mc_add_keyword_ascii ("LanguageNames", MCLANGUAGENAMES, "keyword", 0, NULL);
      mc_add_keyword_ascii ("MessageId", MCMESSAGEID, "keyword", 0, NULL);
      mc_add_keyword_ascii ("Severity", MCSEVERITY, "keyword", 0, NULL);
      mc_add_keyword_ascii ("Facility", MCFACILITY, "keyword", 0, NULL);
      mc_add_keyword_ascii ("SymbolicName", MCSYMBOLICNAME, "keyword", 0, NULL);
      mc_add_keyword_ascii ("Language", MCLANGUAGE, "keyword", 0, NULL);
      mc_add_keyword_ascii ("Success", MCTOKEN, "severity", 0, NULL);
      mc_add_keyword_ascii ("Informational", MCTOKEN, "severity", 1, NULL);
      mc_add_keyword_ascii ("Warning", MCTOKEN, "severity", 2, NULL);
      mc_add_keyword_ascii ("Error", MCTOKEN, "severity", 3, NULL);
      mc_add_keyword_ascii ("System", MCTOKEN, "facility", 0xff, NULL);
      mc_add_keyword_ascii ("Application", MCTOKEN, "facility", 0xfff, NULL);
      mc_add_keyword_ascii ("English", MCTOKEN, "language", 0x409, "MSG00001");
  }
  k = keyword_top;
  if (!len || !t || *t == 0)
    return -1;
  while (k != NULL)
    {
      if (k->len > len)
	break;
      if (k->len == len)
	{
	  if (! memcmp (k->usz, t, len * sizeof (unichar)))
	    {
	      if (k->rid == MCTOKEN)
		yylval.tok = k;
	      return k->rid;
	    }
	}
      k = k->next;
    }
  return -1;
}

int
yylex (void)
{
  unichar *start_token;
  unichar ch;

  if (! input_stream_pos)
    {
      fatal ("Input stream not setuped.\n");
      return -1;
    }
  if (mclex_want_line)
    {
      start_token = input_stream_pos;
      if (input_stream_pos[0] == '.'
	  && (input_stream_pos[1] == '\n'
	      || (input_stream_pos[1] == '\r' && input_stream_pos[2] == '\n')))
      {
	mclex_want_line = FALSE;
	while (input_stream_pos[0] != 0 && input_stream_pos[0] != '\n')
	  ++input_stream_pos;
	if (input_stream_pos[0] == '\n')
	  ++input_stream_pos;
	return MCENDLINE;
      }
      while (input_stream_pos[0] != 0 && input_stream_pos[0] != '\n')
	++input_stream_pos;
      if (input_stream_pos[0] == '\n')
	++input_stream_pos;
      yylval.ustr = get_diff (input_stream_pos, start_token);
      return MCLINE;
    }
  while ((ch = input_stream_pos[0]) <= 0x20)
    {
      if (ch == 0)
	return -1;
      ++input_stream_pos;
      if (ch == '\n')
	input_line += 1;
      if (mclex_want_nl && ch == '\n')
	{
	  mclex_want_nl = FALSE;
	  return NL;
	}
    }
  start_token = input_stream_pos;
  ++input_stream_pos;
  if (mclex_want_filename)
    {
      mclex_want_filename = FALSE;
      if (ch == '"')
	{
	  start_token++;
	  while ((ch = input_stream_pos[0]) != 0)
	    {
	      if (ch == '"')
		break;
	      ++input_stream_pos;
	    }
	  yylval.ustr = get_diff (input_stream_pos, start_token);
	  if (ch == '"')
	    ++input_stream_pos;
	}
      else
	{
	  while ((ch = input_stream_pos[0]) != 0)
	    {
	      if (ch <= 0x20 || ch == ')')
		break;
	      ++input_stream_pos;
	    }
	  yylval.ustr = get_diff (input_stream_pos, start_token);
	}
      return MCFILENAME;
    }
  switch (ch)
  {
  case ';':
    ++start_token;
    while (input_stream_pos[0] != '\n' && input_stream_pos[0] != 0)
      ++input_stream_pos;
    if (input_stream_pos[0] == '\n')
      input_stream_pos++;
    yylval.ustr = get_diff (input_stream_pos, start_token);
    return MCCOMMENT;
  case '=':
    return '=';
  case '(':
    return '(';
  case ')':
    return ')';
  case '+':
    return '+';
  case ':':
    return ':';
  case '0': case '1': case '2': case '3': case '4':
  case '5': case '6': case '7': case '8': case '9':
    yylval.ival = parse_digit (ch);
    return MCNUMBER;
  default:
    if (ch >= 0x40)
      {
	int ret;
	while (input_stream_pos[0] >= 0x40 || (input_stream_pos[0] >= '0' && input_stream_pos[0] <= '9'))
	  ++input_stream_pos;
	ret = mc_token (start_token, (size_t) (input_stream_pos - start_token));
	if (ret != -1)
	  return ret;
	yylval.ustr = get_diff (input_stream_pos, start_token);
	return MCIDENT;
      }
    yyerror ("illegal character 0x%x.", ch);
  }
  return -1;
}
