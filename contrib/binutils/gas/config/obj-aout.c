/* a.out object file format
   Copyright 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1999, 2000,
   2001, 2002, 2003, 2004, 2005 Free Software Foundation, Inc.

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

#define OBJ_HEADER "obj-aout.h"

#include "as.h"
#undef NO_RELOC
#include "aout/aout64.h"
#include "obstack.h"

void
obj_aout_frob_symbol (symbolS *sym, int *punt ATTRIBUTE_UNUSED)
{
  flagword flags;
  asection *sec;
  int desc, type, other;

  flags = symbol_get_bfdsym (sym)->flags;
  desc = aout_symbol (symbol_get_bfdsym (sym))->desc;
  type = aout_symbol (symbol_get_bfdsym (sym))->type;
  other = aout_symbol (symbol_get_bfdsym (sym))->other;
  sec = S_GET_SEGMENT (sym);

  /* Only frob simple symbols this way right now.  */
  if (! (type & ~ (N_TYPE | N_EXT)))
    {
      if (type == (N_UNDF | N_EXT)
	  && sec == &bfd_abs_section)
	{
	  sec = bfd_und_section_ptr;
	  S_SET_SEGMENT (sym, sec);
	}

      if ((type & N_TYPE) != N_INDR
	  && (type & N_TYPE) != N_SETA
	  && (type & N_TYPE) != N_SETT
	  && (type & N_TYPE) != N_SETD
	  && (type & N_TYPE) != N_SETB
	  && type != N_WARNING
	  && (sec == &bfd_abs_section
	      || sec == &bfd_und_section))
	return;
      if (flags & BSF_EXPORT)
	type |= N_EXT;

      switch (type & N_TYPE)
	{
	case N_SETA:
	case N_SETT:
	case N_SETD:
	case N_SETB:
	  /* Set the debugging flag for constructor symbols so that
	     BFD leaves them alone.  */
	  symbol_get_bfdsym (sym)->flags |= BSF_DEBUGGING;

	  /* You can't put a common symbol in a set.  The way a set
	     element works is that the symbol has a definition and a
	     name, and the linker adds the definition to the set of
	     that name.  That does not work for a common symbol,
	     because the linker can't tell which common symbol the
	     user means.  FIXME: Using as_bad here may be
	     inappropriate, since the user may want to force a
	     particular type without regard to the semantics of sets;
	     on the other hand, we certainly don't want anybody to be
	     mislead into thinking that their code will work.  */
	  if (S_IS_COMMON (sym))
	    as_bad (_("Attempt to put a common symbol into set %s"),
		    S_GET_NAME (sym));
	  /* Similarly, you can't put an undefined symbol in a set.  */
	  else if (! S_IS_DEFINED (sym))
	    as_bad (_("Attempt to put an undefined symbol into set %s"),
		    S_GET_NAME (sym));

	  break;
	case N_INDR:
	  /* Put indirect symbols in the indirect section.  */
	  S_SET_SEGMENT (sym, bfd_ind_section_ptr);
	  symbol_get_bfdsym (sym)->flags |= BSF_INDIRECT;
	  if (type & N_EXT)
	    {
	      symbol_get_bfdsym (sym)->flags |= BSF_EXPORT;
	      symbol_get_bfdsym (sym)->flags &=~ BSF_LOCAL;
	    }
	  break;
	case N_WARNING:
	  /* Mark warning symbols.  */
	  symbol_get_bfdsym (sym)->flags |= BSF_WARNING;
	  break;
	}
    }
  else
    symbol_get_bfdsym (sym)->flags |= BSF_DEBUGGING;

  aout_symbol (symbol_get_bfdsym (sym))->type = type;

  /* Double check weak symbols.  */
  if (S_IS_WEAK (sym) && S_IS_COMMON (sym))
    as_bad (_("Symbol `%s' can not be both weak and common"),
	    S_GET_NAME (sym));
}

void
obj_aout_frob_file_before_fix (void)
{
  /* Relocation processing may require knowing the VMAs of the sections.
     Since writing to a section will cause the BFD back end to compute the
     VMAs, fake it out here....  */
  bfd_byte b = 0;
  bfd_boolean x = TRUE;
  if (bfd_section_size (stdoutput, text_section) != 0)
    x = bfd_set_section_contents (stdoutput, text_section, &b, (file_ptr) 0,
				  (bfd_size_type) 1);
  else if (bfd_section_size (stdoutput, data_section) != 0)
    x = bfd_set_section_contents (stdoutput, data_section, &b, (file_ptr) 0,
				  (bfd_size_type) 1);

  assert (x);
}

static void
obj_aout_line (int ignore ATTRIBUTE_UNUSED)
{
  /* Assume delimiter is part of expression.
     BSD4.2 as fails with delightful bug, so we
     are not being incompatible here.  */
  new_logical_line ((char *) NULL, (int) (get_absolute_expression ()));
  demand_empty_rest_of_line ();
}

/* Handle .weak.  This is a GNU extension.  */

static void
obj_aout_weak (int ignore ATTRIBUTE_UNUSED)
{
  char *name;
  int c;
  symbolS *symbolP;

  do
    {
      name = input_line_pointer;
      c = get_symbol_end ();
      symbolP = symbol_find_or_make (name);
      *input_line_pointer = c;
      SKIP_WHITESPACE ();
      S_SET_WEAK (symbolP);
      if (c == ',')
	{
	  input_line_pointer++;
	  SKIP_WHITESPACE ();
	  if (*input_line_pointer == '\n')
	    c = '\n';
	}
    }
  while (c == ',');
  demand_empty_rest_of_line ();
}

/* Handle .type.  On {Net,Open}BSD, this is used to set the n_other field,
   which is then apparently used when doing dynamic linking.  Older
   versions of gas ignored the .type pseudo-op, so we also ignore it if
   we can't parse it.  */

static void
obj_aout_type (int ignore ATTRIBUTE_UNUSED)
{
  char *name;
  int c;
  symbolS *sym;

  name = input_line_pointer;
  c = get_symbol_end ();
  sym = symbol_find_or_make (name);
  *input_line_pointer = c;
  SKIP_WHITESPACE ();
  if (*input_line_pointer == ',')
    {
      ++input_line_pointer;
      SKIP_WHITESPACE ();
      if (*input_line_pointer == '@')
	{
	  ++input_line_pointer;
	  if (strncmp (input_line_pointer, "object", 6) == 0)
	    S_SET_OTHER (sym, 1);
	  else if (strncmp (input_line_pointer, "function", 8) == 0)
	    S_SET_OTHER (sym, 2);
	}
    }

  /* Ignore everything else on the line.  */
  s_ignore (0);
}

/* Support for an AOUT emulation.  */

static void
aout_pop_insert (void)
{
  pop_insert (aout_pseudo_table);
}

static int
obj_aout_s_get_other (symbolS *sym)
{
  return aout_symbol (symbol_get_bfdsym (sym))->other;
}

static void
obj_aout_s_set_other (symbolS *sym, int o)
{
  aout_symbol (symbol_get_bfdsym (sym))->other = o;
}

static int
obj_aout_sec_sym_ok_for_reloc (asection *sec ATTRIBUTE_UNUSED)
{
  return obj_sec_sym_ok_for_reloc (sec);
}

static void
obj_aout_process_stab (segT seg ATTRIBUTE_UNUSED,
		       int w,
		       const char *s,
		       int t,
		       int o,
		       int d)
{
  aout_process_stab (w, s, t, o, d);
}

static int
obj_aout_s_get_desc (symbolS *sym)
{
  return aout_symbol (symbol_get_bfdsym (sym))->desc;
}

static void
obj_aout_s_set_desc (symbolS *sym, int d)
{
  aout_symbol (symbol_get_bfdsym (sym))->desc = d;
}

static int
obj_aout_s_get_type (symbolS *sym)
{
  return aout_symbol (symbol_get_bfdsym (sym))->type;
}

static void
obj_aout_s_set_type (symbolS *sym, int t)
{
  aout_symbol (symbol_get_bfdsym (sym))->type = t;
}

static int
obj_aout_separate_stab_sections (void)
{
  return 0;
}

/* When changed, make sure these table entries match the single-format
   definitions in obj-aout.h.  */

const struct format_ops aout_format_ops =
{
  bfd_target_aout_flavour,
  1,	/* dfl_leading_underscore.  */
  0,	/* emit_section_symbols.  */
  0,	/* begin.  */
  0,	/* app_file.  */
  obj_aout_frob_symbol,
  0,	/* frob_file.  */
  0,	/* frob_file_before_adjust.  */
  obj_aout_frob_file_before_fix,
  0,	/* frob_file_after_relocs.  */
  0,	/* s_get_size.  */
  0,	/* s_set_size.  */
  0,	/* s_get_align.  */
  0,	/* s_set_align.  */
  obj_aout_s_get_other,
  obj_aout_s_set_other,
  obj_aout_s_get_desc,
  obj_aout_s_set_desc,
  obj_aout_s_get_type,
  obj_aout_s_set_type,
  0,	/* copy_symbol_attributes.  */
  0,	/* generate_asm_lineno.  */
  obj_aout_process_stab,
  obj_aout_separate_stab_sections,
  0,	/* init_stab_section.  */
  obj_aout_sec_sym_ok_for_reloc,
  aout_pop_insert,
  0,	/* ecoff_set_ext.  */
  0,	/* read_begin_hook.  */
  0 	/* symbol_new_hook.  */
};

const pseudo_typeS aout_pseudo_table[] =
{
  {"line", obj_aout_line, 0},	/* Source code line number.  */
  {"ln", obj_aout_line, 0},	/* COFF line number that we use anyway.  */

  {"weak", obj_aout_weak, 0},	/* Mark symbol as weak.  */

  {"type", obj_aout_type, 0},

  /* coff debug pseudos (ignored) */
  {"def", s_ignore, 0},
  {"dim", s_ignore, 0},
  {"endef", s_ignore, 0},
  {"ident", s_ignore, 0},
  {"line", s_ignore, 0},
  {"ln", s_ignore, 0},
  {"scl", s_ignore, 0},
  {"size", s_ignore, 0},
  {"tag", s_ignore, 0},
  {"val", s_ignore, 0},
  {"version", s_ignore, 0},

  {"optim", s_ignore, 0},	/* For sun386i cc (?).  */

  /* other stuff */
  {"ABORT", s_abort, 0},

  {NULL, NULL, 0}
};
