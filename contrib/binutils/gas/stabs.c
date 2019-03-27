/* Generic stabs parsing for gas.
   Copyright 1989, 1990, 1991, 1993, 1995, 1996, 1997, 1998, 2000, 2001
   2002, 2003, 2004, 2005 Free Software Foundation, Inc.

This file is part of GAS, the GNU Assembler.

GAS is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2,
or (at your option) any later version.

GAS is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GAS; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
02110-1301, USA.  */

#include "as.h"
#include "obstack.h"
#include "subsegs.h"
#include "ecoff.h"

/* We need this, despite the apparent object format dependency, since
   it defines stab types, which all object formats can use now.  */

#include "aout/stab_gnu.h"

/* Holds whether the assembler is generating stabs line debugging
   information or not.  Potentially used by md_cleanup function.  */

int outputting_stabs_line_debug = 0;

static void s_stab_generic (int, char *, char *);
static void generate_asm_file (int, char *);

/* Allow backends to override the names used for the stab sections.  */
#ifndef STAB_SECTION_NAME
#define STAB_SECTION_NAME ".stab"
#endif

#ifndef STAB_STRING_SECTION_NAME
#define STAB_STRING_SECTION_NAME ".stabstr"
#endif

/* Non-zero if we're in the middle of a .func function, in which case
   stabs_generate_asm_lineno emits function relative line number stabs.
   Otherwise it emits line number stabs with absolute addresses.  Note that
   both cases only apply to assembler code assembled with -gstabs.  */
static int in_dot_func_p;

/* Label at start of current function if in_dot_func_p != 0.  */
static const char *current_function_label;

/*
 * Handle .stabX directives, which used to be open-coded.
 * So much creeping featurism overloaded the semantics that we decided
 * to put all .stabX thinking in one place. Here.
 *
 * We try to make any .stabX directive legal. Other people's AS will often
 * do assembly-time consistency checks: eg assigning meaning to n_type bits
 * and "protecting" you from setting them to certain values. (They also zero
 * certain bits before emitting symbols. Tut tut.)
 *
 * If an expression is not absolute we either gripe or use the relocation
 * information. Other people's assemblers silently forget information they
 * don't need and invent information they need that you didn't supply.
 */

/*
 * Build a string dictionary entry for a .stabX symbol.
 * The symbol is added to the .<secname>str section.
 */

#ifndef SEPARATE_STAB_SECTIONS
#define SEPARATE_STAB_SECTIONS 0
#endif

unsigned int
get_stab_string_offset (const char *string, const char *stabstr_secname)
{
  unsigned int length;
  unsigned int retval;
  segT save_seg;
  subsegT save_subseg;
  segT seg;
  char *p;

  if (! SEPARATE_STAB_SECTIONS)
    abort ();

  length = strlen (string);

  save_seg = now_seg;
  save_subseg = now_subseg;

  /* Create the stab string section.  */
  seg = subseg_new (stabstr_secname, 0);

  retval = seg_info (seg)->stabu.stab_string_size;
  if (retval <= 0)
    {
      /* Make sure the first string is empty.  */
      p = frag_more (1);
      *p = 0;
      retval = seg_info (seg)->stabu.stab_string_size = 1;
      bfd_set_section_flags (stdoutput, seg, SEC_READONLY | SEC_DEBUGGING);
      if (seg->name == stabstr_secname)
	seg->name = xstrdup (stabstr_secname);
    }

  if (length > 0)
    {				/* Ordinary case.  */
      p = frag_more (length + 1);
      strcpy (p, string);

      seg_info (seg)->stabu.stab_string_size += length + 1;
    }
  else
    retval = 0;

  subseg_set (save_seg, save_subseg);

  return retval;
}

#ifdef AOUT_STABS
#ifndef OBJ_PROCESS_STAB
#define OBJ_PROCESS_STAB(SEG,W,S,T,O,D)	aout_process_stab(W,S,T,O,D)
#endif

/* Here instead of obj-aout.c because other formats use it too.  */
void
aout_process_stab (what, string, type, other, desc)
     int what;
     const char *string;
     int type, other, desc;
{
  /* Put the stab information in the symbol table.  */
  symbolS *symbol;

  /* Create the symbol now, but only insert it into the symbol chain
     after any symbols mentioned in the value expression get into the
     symbol chain.  This is to avoid "continuation symbols" (where one
     ends in "\" and the debug info is continued in the next .stabs
     directive) from being separated by other random symbols.  */
  symbol = symbol_create (string, undefined_section, 0,
			  &zero_address_frag);
  if (what == 's' || what == 'n')
    {
      /* Pick up the value from the input line.  */
      pseudo_set (symbol);
    }
  else
    {
      /* .stabd sets the name to NULL.  Why?  */
      S_SET_NAME (symbol, NULL);
      symbol_set_frag (symbol, frag_now);
      S_SET_VALUE (symbol, (valueT) frag_now_fix ());
    }

  symbol_append (symbol, symbol_lastP, &symbol_rootP, &symbol_lastP);

  S_SET_TYPE (symbol, type);
  S_SET_OTHER (symbol, other);
  S_SET_DESC (symbol, desc);
}
#endif

/* This can handle different kinds of stabs (s,n,d) and different
   kinds of stab sections.  */

static void
s_stab_generic (int what, char *stab_secname, char *stabstr_secname)
{
  long longint;
  char *string, *saved_string_obstack_end;
  int type;
  int other;
  int desc;

  /* The general format is:
     .stabs "STRING",TYPE,OTHER,DESC,VALUE
     .stabn TYPE,OTHER,DESC,VALUE
     .stabd TYPE,OTHER,DESC
     At this point input_line_pointer points after the pseudo-op and
     any trailing whitespace.  The argument what is one of 's', 'n' or
     'd' indicating which type of .stab this is.  */

  if (what != 's')
    {
      string = "";
      saved_string_obstack_end = 0;
    }
  else
    {
      int length;

      string = demand_copy_C_string (&length);
      /* FIXME: We should probably find some other temporary storage
	 for string, rather than leaking memory if someone else
	 happens to use the notes obstack.  */
      saved_string_obstack_end = notes.next_free;
      SKIP_WHITESPACE ();
      if (*input_line_pointer == ',')
	input_line_pointer++;
      else
	{
	  as_warn (_(".stab%c: missing comma"), what);
	  ignore_rest_of_line ();
	  return;
	}
    }

  if (get_absolute_expression_and_terminator (&longint) != ',')
    {
      as_warn (_(".stab%c: missing comma"), what);
      ignore_rest_of_line ();
      return;
    }
  type = longint;

  if (get_absolute_expression_and_terminator (&longint) != ',')
    {
      as_warn (_(".stab%c: missing comma"), what);
      ignore_rest_of_line ();
      return;
    }
  other = longint;

  desc = get_absolute_expression ();

  if ((desc > 0xffff) || (desc < -0x8000))
    /* This could happen for example with a source file with a huge
       number of lines.  The only cure is to use a different debug
       format, probably DWARF.  */
    as_warn (_(".stab%c: description field '%x' too big, try a different debug format"),
	     what, desc);

  if (what == 's' || what == 'n')
    {
      if (*input_line_pointer != ',')
	{
	  as_warn (_(".stab%c: missing comma"), what);
	  ignore_rest_of_line ();
	  return;
	}
      input_line_pointer++;
      SKIP_WHITESPACE ();
    }

#ifdef TC_PPC
#ifdef OBJ_ELF
  /* Solaris on PowerPC has decided that .stabd can take 4 arguments, so if we were
     given 4 arguments, make it a .stabn */
  else if (what == 'd')
    {
      char *save_location = input_line_pointer;

      SKIP_WHITESPACE ();
      if (*input_line_pointer == ',')
	{
	  input_line_pointer++;
	  what = 'n';
	}
      else
	input_line_pointer = save_location;
    }
#endif /* OBJ_ELF */
#endif /* TC_PPC */

#ifndef NO_LISTING
  if (listing)
    {
      switch (type)
	{
	case N_SLINE:
	  listing_source_line ((unsigned int) desc);
	  break;
	case N_SO:
	case N_SOL:
	  listing_source_file (string);
	  break;
	}
    }
#endif /* ! NO_LISTING */

  /* We have now gathered the type, other, and desc information.  For
     .stabs or .stabn, input_line_pointer is now pointing at the
     value.  */

  if (SEPARATE_STAB_SECTIONS)
    /* Output the stab information in a separate section.  This is used
       at least for COFF and ELF.  */
    {
      segT saved_seg = now_seg;
      subsegT saved_subseg = now_subseg;
      fragS *saved_frag = frag_now;
      valueT dot;
      segT seg;
      unsigned int stroff;
      char *p;

      static segT cached_sec;
      static char *cached_secname;

      dot = frag_now_fix ();

#ifdef md_flush_pending_output
      md_flush_pending_output ();
#endif

      if (cached_secname && !strcmp (cached_secname, stab_secname))
	{
	  seg = cached_sec;
	  subseg_set (seg, 0);
	}
      else
	{
	  seg = subseg_new (stab_secname, 0);
	  if (cached_secname)
	    free (cached_secname);
	  cached_secname = xstrdup (stab_secname);
	  cached_sec = seg;
	}

      if (! seg_info (seg)->hadone)
	{
	  bfd_set_section_flags (stdoutput, seg,
				 SEC_READONLY | SEC_RELOC | SEC_DEBUGGING);
#ifdef INIT_STAB_SECTION
	  INIT_STAB_SECTION (seg);
#endif
	  seg_info (seg)->hadone = 1;
	}

      stroff = get_stab_string_offset (string, stabstr_secname);
      if (what == 's')
	{
	  /* Release the string, if nobody else has used the obstack.  */
	  if (saved_string_obstack_end == notes.next_free)
	    obstack_free (&notes, string);
	}

      /* At least for now, stabs in a special stab section are always
	 output as 12 byte blocks of information.  */
      p = frag_more (8);
      md_number_to_chars (p, (valueT) stroff, 4);
      md_number_to_chars (p + 4, (valueT) type, 1);
      md_number_to_chars (p + 5, (valueT) other, 1);
      md_number_to_chars (p + 6, (valueT) desc, 2);

      if (what == 's' || what == 'n')
	{
	  /* Pick up the value from the input line.  */
	  cons (4);
	  input_line_pointer--;
	}
      else
	{
	  symbolS *symbol;
	  expressionS exp;

	  /* Arrange for a value representing the current location.  */
	  symbol = symbol_temp_new (saved_seg, dot, saved_frag);

	  exp.X_op = O_symbol;
	  exp.X_add_symbol = symbol;
	  exp.X_add_number = 0;

	  emit_expr (&exp, 4);
	}

#ifdef OBJ_PROCESS_STAB
      OBJ_PROCESS_STAB (seg, what, string, type, other, desc);
#endif

      subseg_set (saved_seg, saved_subseg);
    }
  else
    {
#ifdef OBJ_PROCESS_STAB
      OBJ_PROCESS_STAB (0, what, string, type, other, desc);
#else
      abort ();
#endif
    }

  demand_empty_rest_of_line ();
}

/* Regular stab directive.  */

void
s_stab (int what)
{
  s_stab_generic (what, STAB_SECTION_NAME, STAB_STRING_SECTION_NAME);
}

/* "Extended stabs", used in Solaris only now.  */

void
s_xstab (int what)
{
  int length;
  char *stab_secname, *stabstr_secname;
  static char *saved_secname, *saved_strsecname;

  /* @@ MEMORY LEAK: This allocates a copy of the string, but in most
     cases it will be the same string, so we could release the storage
     back to the obstack it came from.  */
  stab_secname = demand_copy_C_string (&length);
  SKIP_WHITESPACE ();
  if (*input_line_pointer == ',')
    input_line_pointer++;
  else
    {
      as_bad (_("comma missing in .xstabs"));
      ignore_rest_of_line ();
      return;
    }

  /* To get the name of the stab string section, simply add "str" to
     the stab section name.  */
  if (saved_secname == 0 || strcmp (saved_secname, stab_secname))
    {
      stabstr_secname = (char *) xmalloc (strlen (stab_secname) + 4);
      strcpy (stabstr_secname, stab_secname);
      strcat (stabstr_secname, "str");
      if (saved_secname)
	{
	  free (saved_secname);
	  free (saved_strsecname);
	}
      saved_secname = stab_secname;
      saved_strsecname = stabstr_secname;
    }
  s_stab_generic (what, saved_secname, saved_strsecname);
}

#ifdef S_SET_DESC

/* Frob invented at RMS' request. Set the n_desc of a symbol.  */

void
s_desc (ignore)
     int ignore ATTRIBUTE_UNUSED;
{
  char *name;
  char c;
  char *p;
  symbolS *symbolP;
  int temp;

  name = input_line_pointer;
  c = get_symbol_end ();
  p = input_line_pointer;
  *p = c;
  SKIP_WHITESPACE ();
  if (*input_line_pointer != ',')
    {
      *p = 0;
      as_bad (_("expected comma after \"%s\""), name);
      *p = c;
      ignore_rest_of_line ();
    }
  else
    {
      input_line_pointer++;
      temp = get_absolute_expression ();
      *p = 0;
      symbolP = symbol_find_or_make (name);
      *p = c;
      S_SET_DESC (symbolP, temp);
    }
  demand_empty_rest_of_line ();
}				/* s_desc() */

#endif /* defined (S_SET_DESC) */

/* Generate stabs debugging information to denote the main source file.  */

void
stabs_generate_asm_file (void)
{
  char *file;
  unsigned int lineno;

  as_where (&file, &lineno);
  if (use_gnu_debug_info_extensions)
    {
      char *dir, *dir2;

      dir = getpwd ();
      dir2 = alloca (strlen (dir) + 2);
      sprintf (dir2, "%s%s", dir, "/");
      generate_asm_file (N_SO, dir2);
    }
  generate_asm_file (N_SO, file);
}

/* Generate stabs debugging information to denote the source file.
   TYPE is one of N_SO, N_SOL.  */

static void
generate_asm_file (int type, char *file)
{
  static char *last_file;
  static int label_count;
  char *hold;
  char sym[30];
  char *buf;
  char *tmp = file;
  char *endp = file + strlen (file);
  char *bufp;

  if (last_file != NULL
      && strcmp (last_file, file) == 0)
    return;

  /* Rather than try to do this in some efficient fashion, we just
     generate a string and then parse it again.  That lets us use the
     existing stabs hook, which expect to see a string, rather than
     inventing new ones.  */
  hold = input_line_pointer;

  sprintf (sym, "%sF%d", FAKE_LABEL_NAME, label_count);
  ++label_count;

  /* Allocate enough space for the file name (possibly extended with
     doubled up backslashes), the symbol name, and the other characters
     that make up a stabs file directive.  */
  bufp = buf = xmalloc (2 * strlen (file) + strlen (sym) + 12);

  *bufp++ = '"';

  while (tmp < endp)
    {
      char *bslash = strchr (tmp, '\\');
      size_t len = (bslash) ? (size_t) (bslash - tmp + 1) : strlen (tmp);

      /* Double all backslashes, since demand_copy_C_string (used by
	 s_stab to extract the part in quotes) will try to replace them as
	 escape sequences.  backslash may appear in a filespec.  */
      strncpy (bufp, tmp, len);

      tmp += len;
      bufp += len;

      if (bslash != NULL)
	*bufp++ = '\\';
    }

  sprintf (bufp, "\",%d,0,0,%s\n", type, sym);

  input_line_pointer = buf;
  s_stab ('s');
  colon (sym);

  if (last_file != NULL)
    free (last_file);
  last_file = xstrdup (file);

  free (buf);

  input_line_pointer = hold;
}

/* Generate stabs debugging information for the current line.  This is
   used to produce debugging information for an assembler file.  */

void
stabs_generate_asm_lineno (void)
{
  static int label_count;
  char *hold;
  char *file;
  unsigned int lineno;
  char *buf;
  char sym[30];
  /* Remember the last file/line and avoid duplicates.  */
  static unsigned int prev_lineno = -1;
  static char *prev_file = NULL;

  /* Rather than try to do this in some efficient fashion, we just
     generate a string and then parse it again.  That lets us use the
     existing stabs hook, which expect to see a string, rather than
     inventing new ones.  */

  hold = input_line_pointer;

  as_where (&file, &lineno);

  /* Don't emit sequences of stabs for the same line.  */
  if (prev_file == NULL)
    {
      /* First time thru.  */
      prev_file = xstrdup (file);
      prev_lineno = lineno;
    }
  else if (lineno == prev_lineno
	   && strcmp (file, prev_file) == 0)
    {
      /* Same file/line as last time.  */
      return;
    }
  else
    {
      /* Remember file/line for next time.  */
      prev_lineno = lineno;
      if (strcmp (file, prev_file) != 0)
	{
	  free (prev_file);
	  prev_file = xstrdup (file);
	}
    }

  /* Let the world know that we are in the middle of generating a
     piece of stabs line debugging information.  */
  outputting_stabs_line_debug = 1;

  generate_asm_file (N_SOL, file);

  sprintf (sym, "%sL%d", FAKE_LABEL_NAME, label_count);
  ++label_count;

  if (in_dot_func_p)
    {
      buf = (char *) alloca (100 + strlen (current_function_label));
      sprintf (buf, "%d,0,%d,%s-%s\n", N_SLINE, lineno,
	       sym, current_function_label);
    }
  else
    {
      buf = (char *) alloca (100);
      sprintf (buf, "%d,0,%d,%s\n", N_SLINE, lineno, sym);
    }
  input_line_pointer = buf;
  s_stab ('n');
  colon (sym);

  input_line_pointer = hold;
  outputting_stabs_line_debug = 0;
}

/* Emit a function stab.
   All assembler functions are assumed to have return type `void'.  */

void
stabs_generate_asm_func (const char *funcname, const char *startlabname)
{
  static int void_emitted_p;
  char *hold = input_line_pointer;
  char *buf;
  char *file;
  unsigned int lineno;

  if (! void_emitted_p)
    {
      input_line_pointer = "\"void:t1=1\",128,0,0,0";
      s_stab ('s');
      void_emitted_p = 1;
    }

  as_where (&file, &lineno);
  asprintf (&buf, "\"%s:F1\",%d,0,%d,%s",
	    funcname, N_FUN, lineno + 1, startlabname);
  input_line_pointer = buf;
  s_stab ('s');
  free (buf);

  input_line_pointer = hold;
  current_function_label = xstrdup (startlabname);
  in_dot_func_p = 1;
}

/* Emit a stab to record the end of a function.  */

void
stabs_generate_asm_endfunc (const char *funcname ATTRIBUTE_UNUSED,
			    const char *startlabname)
{
  static int label_count;
  char *hold = input_line_pointer;
  char *buf;
  char sym[30];

  sprintf (sym, "%sendfunc%d", FAKE_LABEL_NAME, label_count);
  ++label_count;
  colon (sym);

  asprintf (&buf, "\"\",%d,0,0,%s-%s", N_FUN, sym, startlabname);
  input_line_pointer = buf;
  s_stab ('s');
  free (buf);

  input_line_pointer = hold;
  in_dot_func_p = 0;
  current_function_label = NULL;
}
