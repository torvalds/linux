/* coff object file format
   Copyright 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005
   Free Software Foundation, Inc.

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

#define OBJ_HEADER "obj-coff.h"

#include "as.h"
#include "obstack.h"
#include "subsegs.h"

#ifdef TE_PE
#include "coff/pe.h"
#endif

#define streq(a,b)     (strcmp ((a), (b)) == 0)
#define strneq(a,b,n)  (strncmp ((a), (b), (n)) == 0)

/* I think this is probably always correct.  */
#ifndef KEEP_RELOC_INFO
#define KEEP_RELOC_INFO
#endif

/* obj_coff_section will use this macro to set a new section's
   attributes when a directive has no valid flags or the "w" flag is
   used.  This default should be appropriate for most.  */
#ifndef TC_COFF_SECTION_DEFAULT_ATTRIBUTES
#define TC_COFF_SECTION_DEFAULT_ATTRIBUTES (SEC_LOAD | SEC_DATA)
#endif

/* This is used to hold the symbol built by a sequence of pseudo-ops
   from .def and .endef.  */
static symbolS *def_symbol_in_progress;
#ifdef TE_PE
/* PE weak alternate symbols begin with this string.  */
static const char weak_altprefix[] = ".weak.";
#endif /* TE_PE */

typedef struct
  {
    unsigned long chunk_size;
    unsigned long element_size;
    unsigned long size;
    char *data;
    unsigned long pointer;
  }
stack;


/* Stack stuff.  */

static stack *
stack_init (unsigned long chunk_size,
	    unsigned long element_size)
{
  stack *st;

  st = malloc (sizeof (* st));
  if (!st)
    return NULL;
  st->data = malloc (chunk_size);
  if (!st->data)
    {
      free (st);
      return NULL;
    }
  st->pointer = 0;
  st->size = chunk_size;
  st->chunk_size = chunk_size;
  st->element_size = element_size;
  return st;
}

static char *
stack_push (stack *st, char *element)
{
  if (st->pointer + st->element_size >= st->size)
    {
      st->size += st->chunk_size;
      if ((st->data = xrealloc (st->data, st->size)) == NULL)
	return NULL;
    }
  memcpy (st->data + st->pointer, element, st->element_size);
  st->pointer += st->element_size;
  return st->data + st->pointer;
}

static char *
stack_pop (stack *st)
{
  if (st->pointer < st->element_size)
    {
      st->pointer = 0;
      return NULL;
    }
  st->pointer -= st->element_size;
  return st->data + st->pointer;
}

/* Maintain a list of the tagnames of the structures.  */

static struct hash_control *tag_hash;

static void
tag_init (void)
{
  tag_hash = hash_new ();
}

static void
tag_insert (const char *name, symbolS *symbolP)
{
  const char *error_string;

  if ((error_string = hash_jam (tag_hash, name, (char *) symbolP)))
    as_fatal (_("Inserting \"%s\" into structure table failed: %s"),
	      name, error_string);
}

static symbolS *
tag_find (char *name)
{
  return (symbolS *) hash_find (tag_hash, name);
}

static symbolS *
tag_find_or_make (char *name)
{
  symbolS *symbolP;

  if ((symbolP = tag_find (name)) == NULL)
    {
      symbolP = symbol_new (name, undefined_section,
			    0, &zero_address_frag);

      tag_insert (S_GET_NAME (symbolP), symbolP);
      symbol_table_insert (symbolP);
    }

  return symbolP;
}

/* We accept the .bss directive to set the section for backward
   compatibility with earlier versions of gas.  */

static void
obj_coff_bss (int ignore ATTRIBUTE_UNUSED)
{
  if (*input_line_pointer == '\n')
    subseg_new (".bss", get_absolute_expression ());
  else
    s_lcomm (0);
}

#define GET_FILENAME_STRING(X) \
  ((char *) (&((X)->sy_symbol.ost_auxent->x_file.x_n.x_offset))[1])

/* @@ Ick.  */
static segT
fetch_coff_debug_section (void)
{
  static segT debug_section;

  if (!debug_section)
    {
      const asymbol *s;

      s = bfd_make_debug_symbol (stdoutput, NULL, 0);
      assert (s != 0);
      debug_section = s->section;
    }
  return debug_section;
}

void
SA_SET_SYM_ENDNDX (symbolS *sym, symbolS *val)
{
  combined_entry_type *entry, *p;

  entry = &coffsymbol (symbol_get_bfdsym (sym))->native[1];
  p = coffsymbol (symbol_get_bfdsym (val))->native;
  entry->u.auxent.x_sym.x_fcnary.x_fcn.x_endndx.p = p;
  entry->fix_end = 1;
}

static void
SA_SET_SYM_TAGNDX (symbolS *sym, symbolS *val)
{
  combined_entry_type *entry, *p;

  entry = &coffsymbol (symbol_get_bfdsym (sym))->native[1];
  p = coffsymbol (symbol_get_bfdsym (val))->native;
  entry->u.auxent.x_sym.x_tagndx.p = p;
  entry->fix_tag = 1;
}

static int
S_GET_DATA_TYPE (symbolS *sym)
{
  return coffsymbol (symbol_get_bfdsym (sym))->native->u.syment.n_type;
}

int
S_SET_DATA_TYPE (symbolS *sym, int val)
{
  coffsymbol (symbol_get_bfdsym (sym))->native->u.syment.n_type = val;
  return val;
}

int
S_GET_STORAGE_CLASS (symbolS *sym)
{
  return coffsymbol (symbol_get_bfdsym (sym))->native->u.syment.n_sclass;
}

int
S_SET_STORAGE_CLASS (symbolS *sym, int val)
{
  coffsymbol (symbol_get_bfdsym (sym))->native->u.syment.n_sclass = val;
  return val;
}

/* Merge a debug symbol containing debug information into a normal symbol.  */

static void
c_symbol_merge (symbolS *debug, symbolS *normal)
{
  S_SET_DATA_TYPE (normal, S_GET_DATA_TYPE (debug));
  S_SET_STORAGE_CLASS (normal, S_GET_STORAGE_CLASS (debug));

  if (S_GET_NUMBER_AUXILIARY (debug) > S_GET_NUMBER_AUXILIARY (normal))
    /* Take the most we have.  */
    S_SET_NUMBER_AUXILIARY (normal, S_GET_NUMBER_AUXILIARY (debug));

  if (S_GET_NUMBER_AUXILIARY (debug) > 0)
    /* Move all the auxiliary information.  */
    memcpy (SYM_AUXINFO (normal), SYM_AUXINFO (debug),
	    (S_GET_NUMBER_AUXILIARY (debug)
	     * sizeof (*SYM_AUXINFO (debug))));

  /* Move the debug flags.  */
  SF_SET_DEBUG_FIELD (normal, SF_GET_DEBUG_FIELD (debug));
}

void
c_dot_file_symbol (const char *filename, int appfile ATTRIBUTE_UNUSED)
{
  symbolS *symbolP;

  /* BFD converts filename to a .file symbol with an aux entry.  It
     also handles chaining.  */
  symbolP = symbol_new (filename, bfd_abs_section_ptr, 0, &zero_address_frag);

  S_SET_STORAGE_CLASS (symbolP, C_FILE);
  S_SET_NUMBER_AUXILIARY (symbolP, 1);

  symbol_get_bfdsym (symbolP)->flags = BSF_DEBUGGING;

#ifndef NO_LISTING
  {
    extern int listing;

    if (listing)
      listing_source_file (filename);
  }
#endif

  /* Make sure that the symbol is first on the symbol chain.  */
  if (symbol_rootP != symbolP)
    {
      symbol_remove (symbolP, &symbol_rootP, &symbol_lastP);
      symbol_insert (symbolP, symbol_rootP, &symbol_rootP, &symbol_lastP);
    }
}

/* Line number handling.  */

struct line_no
{
  struct line_no *next;
  fragS *frag;
  alent l;
};

int coff_line_base;

/* Symbol of last function, which we should hang line#s off of.  */
static symbolS *line_fsym;

#define in_function()		(line_fsym != 0)
#define clear_function()	(line_fsym = 0)
#define set_function(F)		(line_fsym = (F), coff_add_linesym (F))


void
coff_obj_symbol_new_hook (symbolS *symbolP)
{
  long   sz = (OBJ_COFF_MAX_AUXENTRIES + 1) * sizeof (combined_entry_type);
  char * s  = xmalloc (sz);

  memset (s, 0, sz);
  coffsymbol (symbol_get_bfdsym (symbolP))->native = (combined_entry_type *) s;

  S_SET_DATA_TYPE (symbolP, T_NULL);
  S_SET_STORAGE_CLASS (symbolP, 0);
  S_SET_NUMBER_AUXILIARY (symbolP, 0);

  if (S_IS_STRING (symbolP))
    SF_SET_STRING (symbolP);

  if (S_IS_LOCAL (symbolP))
    SF_SET_LOCAL (symbolP);
}

void
coff_obj_symbol_clone_hook (symbolS *newsymP, symbolS *orgsymP)
{
  long sz = (OBJ_COFF_MAX_AUXENTRIES + 1) * sizeof (combined_entry_type);
  combined_entry_type * s = xmalloc (sz);

  memcpy (s, coffsymbol (symbol_get_bfdsym (orgsymP))->native, sz);
  coffsymbol (symbol_get_bfdsym (newsymP))->native = s;

  SF_SET (newsymP, SF_GET (orgsymP));
}


/* Handle .ln directives.  */

static symbolS *current_lineno_sym;
static struct line_no *line_nos;
/* FIXME:  Blindly assume all .ln directives will be in the .text section.  */
int coff_n_line_nos;

static void
add_lineno (fragS * frag, addressT offset, int num)
{
  struct line_no * new_line = xmalloc (sizeof (* new_line));

  if (!current_lineno_sym)
    abort ();

#ifndef OBJ_XCOFF
  /* The native aix assembler accepts negative line number.  */

  if (num <= 0)
    {
      /* Zero is used as an end marker in the file.  */
      as_warn (_("Line numbers must be positive integers\n"));
      num = 1;
    }
#endif /* OBJ_XCOFF */
  new_line->next = line_nos;
  new_line->frag = frag;
  new_line->l.line_number = num;
  new_line->l.u.offset = offset;
  line_nos = new_line;
  coff_n_line_nos++;
}

void
coff_add_linesym (symbolS *sym)
{
  if (line_nos)
    {
      coffsymbol (symbol_get_bfdsym (current_lineno_sym))->lineno =
	(alent *) line_nos;
      coff_n_line_nos++;
      line_nos = 0;
    }
  current_lineno_sym = sym;
}

static void
obj_coff_ln (int appline)
{
  int l;

  if (! appline && def_symbol_in_progress != NULL)
    {
      as_warn (_(".ln pseudo-op inside .def/.endef: ignored."));
      demand_empty_rest_of_line ();
      return;
    }

  l = get_absolute_expression ();

  /* If there is no lineno symbol, treat a .ln
     directive as if it were a .appline directive.  */
  if (appline || current_lineno_sym == NULL)
    new_logical_line ((char *) NULL, l - 1);
  else
    add_lineno (frag_now, frag_now_fix (), l);

#ifndef NO_LISTING
  {
    extern int listing;

    if (listing)
      {
	if (! appline)
	  l += coff_line_base - 1;
	listing_source_line (l);
      }
  }
#endif

  demand_empty_rest_of_line ();
}

/* .loc is essentially the same as .ln; parse it for assembler
   compatibility.  */

static void
obj_coff_loc (int ignore ATTRIBUTE_UNUSED)
{
  int lineno;

  /* FIXME: Why do we need this check?  We need it for ECOFF, but why
     do we need it for COFF?  */
  if (now_seg != text_section)
    {
      as_warn (_(".loc outside of .text"));
      demand_empty_rest_of_line ();
      return;
    }

  if (def_symbol_in_progress != NULL)
    {
      as_warn (_(".loc pseudo-op inside .def/.endef: ignored."));
      demand_empty_rest_of_line ();
      return;
    }

  /* Skip the file number.  */
  SKIP_WHITESPACE ();
  get_absolute_expression ();
  SKIP_WHITESPACE ();

  lineno = get_absolute_expression ();

#ifndef NO_LISTING
  {
    extern int listing;

    if (listing)
      {
	lineno += coff_line_base - 1;
	listing_source_line (lineno);
      }
  }
#endif

  demand_empty_rest_of_line ();

  add_lineno (frag_now, frag_now_fix (), lineno);
}

/* Handle the .ident pseudo-op.  */

static void
obj_coff_ident (int ignore ATTRIBUTE_UNUSED)
{
  segT current_seg = now_seg;
  subsegT current_subseg = now_subseg;

#ifdef TE_PE
  {
    segT sec;

    /* We could put it in .comment, but that creates an extra section
       that shouldn't be loaded into memory, which requires linker
       changes...  For now, until proven otherwise, use .rdata.  */
    sec = subseg_new (".rdata$zzz", 0);
    bfd_set_section_flags (stdoutput, sec,
			   ((SEC_ALLOC | SEC_LOAD | SEC_READONLY | SEC_DATA)
			    & bfd_applicable_section_flags (stdoutput)));
  }
#else
  subseg_new (".comment", 0);
#endif

  stringer (1);
  subseg_set (current_seg, current_subseg);
}

/* Handle .def directives.

   One might ask : why can't we symbol_new if the symbol does not
   already exist and fill it with debug information.  Because of
   the C_EFCN special symbol. It would clobber the value of the
   function symbol before we have a chance to notice that it is
   a C_EFCN. And a second reason is that the code is more clear this
   way. (at least I think it is :-).  */

#define SKIP_SEMI_COLON()	while (*input_line_pointer++ != ';')
#define SKIP_WHITESPACES()	while (*input_line_pointer == ' ' || \
				       *input_line_pointer == '\t')  \
                                  input_line_pointer++;

static void
obj_coff_def (int what ATTRIBUTE_UNUSED)
{
  char name_end;		/* Char after the end of name.  */
  char *symbol_name;		/* Name of the debug symbol.  */
  char *symbol_name_copy;	/* Temporary copy of the name.  */
  unsigned int symbol_name_length;

  if (def_symbol_in_progress != NULL)
    {
      as_warn (_(".def pseudo-op used inside of .def/.endef: ignored."));
      demand_empty_rest_of_line ();
      return;
    }

  SKIP_WHITESPACES ();

  symbol_name = input_line_pointer;
  name_end = get_symbol_end ();
  symbol_name_length = strlen (symbol_name);
  symbol_name_copy = xmalloc (symbol_name_length + 1);
  strcpy (symbol_name_copy, symbol_name);
#ifdef tc_canonicalize_symbol_name
  symbol_name_copy = tc_canonicalize_symbol_name (symbol_name_copy);
#endif

  /* Initialize the new symbol.  */
  def_symbol_in_progress = symbol_make (symbol_name_copy);
  symbol_set_frag (def_symbol_in_progress, &zero_address_frag);
  S_SET_VALUE (def_symbol_in_progress, 0);

  if (S_IS_STRING (def_symbol_in_progress))
    SF_SET_STRING (def_symbol_in_progress);

  *input_line_pointer = name_end;

  demand_empty_rest_of_line ();
}

unsigned int dim_index;

static void
obj_coff_endef (int ignore ATTRIBUTE_UNUSED)
{
  symbolS *symbolP = NULL;

  dim_index = 0;
  if (def_symbol_in_progress == NULL)
    {
      as_warn (_(".endef pseudo-op used outside of .def/.endef: ignored."));
      demand_empty_rest_of_line ();
      return;
    }

  /* Set the section number according to storage class.  */
  switch (S_GET_STORAGE_CLASS (def_symbol_in_progress))
    {
    case C_STRTAG:
    case C_ENTAG:
    case C_UNTAG:
      SF_SET_TAG (def_symbol_in_progress);
      /* Fall through.  */
    case C_FILE:
    case C_TPDEF:
      SF_SET_DEBUG (def_symbol_in_progress);
      S_SET_SEGMENT (def_symbol_in_progress, fetch_coff_debug_section ());
      break;

    case C_EFCN:
      SF_SET_LOCAL (def_symbol_in_progress);	/* Do not emit this symbol.  */
      /* Fall through.  */
    case C_BLOCK:
      SF_SET_PROCESS (def_symbol_in_progress);	/* Will need processing before writing.  */
      /* Fall through.  */
    case C_FCN:
      {
	const char *name;

	S_SET_SEGMENT (def_symbol_in_progress, text_section);

	name = S_GET_NAME (def_symbol_in_progress);
	if (name[0] == '.' && name[2] == 'f' && name[3] == '\0')
	  {
	    switch (name[1])
	      {
	      case 'b':
		/* .bf */
		if (! in_function ())
		  as_warn (_("`%s' symbol without preceding function"), name);
		/* Will need relocating.  */
		SF_SET_PROCESS (def_symbol_in_progress);
		clear_function ();
		break;
#ifdef TE_PE
	      case 'e':
		/* .ef */
		/* The MS compilers output the actual endline, not the
		   function-relative one... we want to match without
		   changing the assembler input.  */
		SA_SET_SYM_LNNO (def_symbol_in_progress,
				 (SA_GET_SYM_LNNO (def_symbol_in_progress)
				  + coff_line_base));
		break;
#endif
	      }
	  }
      }
      break;

#ifdef C_AUTOARG
    case C_AUTOARG:
#endif /* C_AUTOARG */
    case C_AUTO:
    case C_REG:
    case C_ARG:
    case C_REGPARM:
    case C_FIELD:

    /* According to the COFF documentation:

       http://osr5doc.sco.com:1996/topics/COFF_SectNumFld.html

       A special section number (-2) marks symbolic debugging symbols,
       including structure/union/enumeration tag names, typedefs, and
       the name of the file. A section number of -1 indicates that the
       symbol has a value but is not relocatable. Examples of
       absolute-valued symbols include automatic and register variables,
       function arguments, and .eos symbols.

       But from Ian Lance Taylor:

       http://sources.redhat.com/ml/binutils/2000-08/msg00202.html

       the actual tools all marked them as section -1. So the GNU COFF
       assembler follows historical COFF assemblers.

       However, it causes problems for djgpp

       http://sources.redhat.com/ml/binutils/2000-08/msg00210.html

       By defining STRICTCOFF, a COFF port can make the assembler to
       follow the documented behavior.  */
#ifdef STRICTCOFF
    case C_MOS:
    case C_MOE:
    case C_MOU:
    case C_EOS:
#endif
      SF_SET_DEBUG (def_symbol_in_progress);
      S_SET_SEGMENT (def_symbol_in_progress, absolute_section);
      break;

#ifndef STRICTCOFF
    case C_MOS:
    case C_MOE:
    case C_MOU:
    case C_EOS:
      S_SET_SEGMENT (def_symbol_in_progress, absolute_section);
      break;
#endif

    case C_EXT:
    case C_WEAKEXT:
#ifdef TE_PE
    case C_NT_WEAK:
#endif
    case C_STAT:
    case C_LABEL:
      /* Valid but set somewhere else (s_comm, s_lcomm, colon).  */
      break;

    default:
    case C_USTATIC:
    case C_EXTDEF:
    case C_ULABEL:
      as_warn (_("unexpected storage class %d"),
	       S_GET_STORAGE_CLASS (def_symbol_in_progress));
      break;
    }

  /* Now that we have built a debug symbol, try to find if we should
     merge with an existing symbol or not.  If a symbol is C_EFCN or
     absolute_section or untagged SEG_DEBUG it never merges.  We also
     don't merge labels, which are in a different namespace, nor
     symbols which have not yet been defined since they are typically
     unique, nor do we merge tags with non-tags.  */

  /* Two cases for functions.  Either debug followed by definition or
     definition followed by debug.  For definition first, we will
     merge the debug symbol into the definition.  For debug first, the
     lineno entry MUST point to the definition function or else it
     will point off into space when obj_crawl_symbol_chain() merges
     the debug symbol into the real symbol.  Therefor, let's presume
     the debug symbol is a real function reference.  */

  /* FIXME-SOON If for some reason the definition label/symbol is
     never seen, this will probably leave an undefined symbol at link
     time.  */

  if (S_GET_STORAGE_CLASS (def_symbol_in_progress) == C_EFCN
      || S_GET_STORAGE_CLASS (def_symbol_in_progress) == C_LABEL
      || (streq (bfd_get_section_name (stdoutput,
				       S_GET_SEGMENT (def_symbol_in_progress)),
		 "*DEBUG*")
	  && !SF_GET_TAG (def_symbol_in_progress))
      || S_GET_SEGMENT (def_symbol_in_progress) == absolute_section
      || ! symbol_constant_p (def_symbol_in_progress)
      || (symbolP = symbol_find (S_GET_NAME (def_symbol_in_progress))) == NULL
      || SF_GET_TAG (def_symbol_in_progress) != SF_GET_TAG (symbolP))
    {
      /* If it already is at the end of the symbol list, do nothing */
      if (def_symbol_in_progress != symbol_lastP)
	{
	  symbol_remove (def_symbol_in_progress, &symbol_rootP, &symbol_lastP);
	  symbol_append (def_symbol_in_progress, symbol_lastP, &symbol_rootP,
			 &symbol_lastP);
	}
    }
  else
    {
      /* This symbol already exists, merge the newly created symbol
	 into the old one.  This is not mandatory. The linker can
	 handle duplicate symbols correctly. But I guess that it save
	 a *lot* of space if the assembly file defines a lot of
	 symbols. [loic]  */

      /* The debug entry (def_symbol_in_progress) is merged into the
	 previous definition.  */

      c_symbol_merge (def_symbol_in_progress, symbolP);
      symbol_remove (def_symbol_in_progress, &symbol_rootP, &symbol_lastP);

      def_symbol_in_progress = symbolP;

      if (SF_GET_FUNCTION (def_symbol_in_progress)
	  || SF_GET_TAG (def_symbol_in_progress)
	  || S_GET_STORAGE_CLASS (def_symbol_in_progress) == C_STAT)
	{
	  /* For functions, and tags, and static symbols, the symbol
	     *must* be where the debug symbol appears.  Move the
	     existing symbol to the current place.  */
	  /* If it already is at the end of the symbol list, do nothing.  */
	  if (def_symbol_in_progress != symbol_lastP)
	    {
	      symbol_remove (def_symbol_in_progress, &symbol_rootP, &symbol_lastP);
	      symbol_append (def_symbol_in_progress, symbol_lastP, &symbol_rootP, &symbol_lastP);
	    }
	}
    }

  if (SF_GET_TAG (def_symbol_in_progress))
    {
      symbolS *oldtag;

      oldtag = symbol_find (S_GET_NAME (def_symbol_in_progress));
      if (oldtag == NULL || ! SF_GET_TAG (oldtag))
	tag_insert (S_GET_NAME (def_symbol_in_progress),
		    def_symbol_in_progress);
    }

  if (SF_GET_FUNCTION (def_symbol_in_progress))
    {
      know (sizeof (def_symbol_in_progress) <= sizeof (long));
      set_function (def_symbol_in_progress);
      SF_SET_PROCESS (def_symbol_in_progress);

      if (symbolP == NULL)
	/* That is, if this is the first time we've seen the
	   function.  */
	symbol_table_insert (def_symbol_in_progress);

    }

  def_symbol_in_progress = NULL;
  demand_empty_rest_of_line ();
}

static void
obj_coff_dim (int ignore ATTRIBUTE_UNUSED)
{
  int dim_index;

  if (def_symbol_in_progress == NULL)
    {
      as_warn (_(".dim pseudo-op used outside of .def/.endef: ignored."));
      demand_empty_rest_of_line ();
      return;
    }

  S_SET_NUMBER_AUXILIARY (def_symbol_in_progress, 1);

  for (dim_index = 0; dim_index < DIMNUM; dim_index++)
    {
      SKIP_WHITESPACES ();
      SA_SET_SYM_DIMEN (def_symbol_in_progress, dim_index,
			get_absolute_expression ());

      switch (*input_line_pointer)
	{
	case ',':
	  input_line_pointer++;
	  break;

	default:
	  as_warn (_("badly formed .dim directive ignored"));
	  /* Fall through.  */
	case '\n':
	case ';':
	  dim_index = DIMNUM;
	  break;
	}
    }

  demand_empty_rest_of_line ();
}

static void
obj_coff_line (int ignore ATTRIBUTE_UNUSED)
{
  int this_base;

  if (def_symbol_in_progress == NULL)
    {
      /* Probably stabs-style line?  */
      obj_coff_ln (0);
      return;
    }

  this_base = get_absolute_expression ();
  if (streq (".bf", S_GET_NAME (def_symbol_in_progress)))
    coff_line_base = this_base;

  S_SET_NUMBER_AUXILIARY (def_symbol_in_progress, 1);
  SA_SET_SYM_LNNO (def_symbol_in_progress, this_base);

  demand_empty_rest_of_line ();

#ifndef NO_LISTING
  if (streq (".bf", S_GET_NAME (def_symbol_in_progress)))
    {
      extern int listing;

      if (listing)
	listing_source_line ((unsigned int) this_base);
    }
#endif
}

static void
obj_coff_size (int ignore ATTRIBUTE_UNUSED)
{
  if (def_symbol_in_progress == NULL)
    {
      as_warn (_(".size pseudo-op used outside of .def/.endef ignored."));
      demand_empty_rest_of_line ();
      return;
    }

  S_SET_NUMBER_AUXILIARY (def_symbol_in_progress, 1);
  SA_SET_SYM_SIZE (def_symbol_in_progress, get_absolute_expression ());
  demand_empty_rest_of_line ();
}

static void
obj_coff_scl (int ignore ATTRIBUTE_UNUSED)
{
  if (def_symbol_in_progress == NULL)
    {
      as_warn (_(".scl pseudo-op used outside of .def/.endef ignored."));
      demand_empty_rest_of_line ();
      return;
    }

  S_SET_STORAGE_CLASS (def_symbol_in_progress, get_absolute_expression ());
  demand_empty_rest_of_line ();
}

static void
obj_coff_tag (int ignore ATTRIBUTE_UNUSED)
{
  char *symbol_name;
  char name_end;

  if (def_symbol_in_progress == NULL)
    {
      as_warn (_(".tag pseudo-op used outside of .def/.endef ignored."));
      demand_empty_rest_of_line ();
      return;
    }

  S_SET_NUMBER_AUXILIARY (def_symbol_in_progress, 1);
  symbol_name = input_line_pointer;
  name_end = get_symbol_end ();

#ifdef tc_canonicalize_symbol_name
  symbol_name = tc_canonicalize_symbol_name (symbol_name);
#endif

  /* Assume that the symbol referred to by .tag is always defined.
     This was a bad assumption.  I've added find_or_make. xoxorich.  */
  SA_SET_SYM_TAGNDX (def_symbol_in_progress,
		     tag_find_or_make (symbol_name));
  if (SA_GET_SYM_TAGNDX (def_symbol_in_progress) == 0L)
    as_warn (_("tag not found for .tag %s"), symbol_name);

  SF_SET_TAGGED (def_symbol_in_progress);
  *input_line_pointer = name_end;

  demand_empty_rest_of_line ();
}

static void
obj_coff_type (int ignore ATTRIBUTE_UNUSED)
{
  if (def_symbol_in_progress == NULL)
    {
      as_warn (_(".type pseudo-op used outside of .def/.endef ignored."));
      demand_empty_rest_of_line ();
      return;
    }

  S_SET_DATA_TYPE (def_symbol_in_progress, get_absolute_expression ());

  if (ISFCN (S_GET_DATA_TYPE (def_symbol_in_progress)) &&
      S_GET_STORAGE_CLASS (def_symbol_in_progress) != C_TPDEF)
    SF_SET_FUNCTION (def_symbol_in_progress);

  demand_empty_rest_of_line ();
}

static void
obj_coff_val (int ignore ATTRIBUTE_UNUSED)
{
  if (def_symbol_in_progress == NULL)
    {
      as_warn (_(".val pseudo-op used outside of .def/.endef ignored."));
      demand_empty_rest_of_line ();
      return;
    }

  if (is_name_beginner (*input_line_pointer))
    {
      char *symbol_name = input_line_pointer;
      char name_end = get_symbol_end ();

#ifdef tc_canonicalize_symbol_name
  symbol_name = tc_canonicalize_symbol_name (symbol_name);
#endif
      if (streq (symbol_name, "."))
	{
	  /* If the .val is != from the .def (e.g. statics).  */
	  symbol_set_frag (def_symbol_in_progress, frag_now);
	  S_SET_VALUE (def_symbol_in_progress, (valueT) frag_now_fix ());
	}
      else if (! streq (S_GET_NAME (def_symbol_in_progress), symbol_name))
	{
	  expressionS exp;

	  exp.X_op = O_symbol;
	  exp.X_add_symbol = symbol_find_or_make (symbol_name);
	  exp.X_op_symbol = NULL;
	  exp.X_add_number = 0;
	  symbol_set_value_expression (def_symbol_in_progress, &exp);

	  /* If the segment is undefined when the forward reference is
	     resolved, then copy the segment id from the forward
	     symbol.  */
	  SF_SET_GET_SEGMENT (def_symbol_in_progress);

	  /* FIXME: gcc can generate address expressions here in
	     unusual cases (search for "obscure" in sdbout.c).  We
	     just ignore the offset here, thus generating incorrect
	     debugging information.  We ignore the rest of the line
	     just below.  */
	}
      /* Otherwise, it is the name of a non debug symbol and its value
         will be calculated later.  */
      *input_line_pointer = name_end;
    }
  else
    {
      S_SET_VALUE (def_symbol_in_progress, get_absolute_expression ());
    }

  demand_empty_rest_of_line ();
}

#ifdef TE_PE

/* Return nonzero if name begins with weak alternate symbol prefix.  */

static int
weak_is_altname (const char * name)
{
  return strneq (name, weak_altprefix, sizeof (weak_altprefix) - 1);
}

/* Return the name of the alternate symbol
   name corresponding to a weak symbol's name.  */

static const char *
weak_name2altname (const char * name)
{
  char *alt_name;

  alt_name = xmalloc (sizeof (weak_altprefix) + strlen (name));
  strcpy (alt_name, weak_altprefix);
  return strcat (alt_name, name);
}

/* Return the name of the weak symbol corresponding to an
   alternate symbol.  */

static const char *
weak_altname2name (const char * name)
{
  char * weak_name;
  char * dot;

  assert (weak_is_altname (name));

  weak_name = xstrdup (name + 6);
  if ((dot = strchr (weak_name, '.')))
    *dot = 0;
  return weak_name;
}

/* Make a weak symbol name unique by
   appending the name of an external symbol.  */

static const char *
weak_uniquify (const char * name)
{
  char *ret;
  const char * unique = "";

#ifdef USE_UNIQUE
  if (an_external_name != NULL)
    unique = an_external_name;
#endif
  assert (weak_is_altname (name));

  if (strchr (name + sizeof (weak_altprefix), '.'))
    return name;

  ret = xmalloc (strlen (name) + strlen (unique) + 2);
  strcpy (ret, name);
  strcat (ret, ".");
  strcat (ret, unique);
  return ret;
}

void
pecoff_obj_set_weak_hook (symbolS *symbolP)
{
  symbolS *alternateP;

  /* See _Microsoft Portable Executable and Common Object
     File Format Specification_, section 5.5.3.
     Create a symbol representing the alternate value.
     coff_frob_symbol will set the value of this symbol from
     the value of the weak symbol itself.  */
  S_SET_STORAGE_CLASS (symbolP, C_NT_WEAK);
  S_SET_NUMBER_AUXILIARY (symbolP, 1);
  SA_SET_SYM_FSIZE (symbolP, IMAGE_WEAK_EXTERN_SEARCH_NOLIBRARY);

  alternateP = symbol_find_or_make (weak_name2altname (S_GET_NAME (symbolP)));
  S_SET_EXTERNAL (alternateP);
  S_SET_STORAGE_CLASS (alternateP, C_NT_WEAK);

  SA_SET_SYM_TAGNDX (symbolP, alternateP);
}

void
pecoff_obj_clear_weak_hook (symbolS *symbolP)
{
  symbolS *alternateP;

  S_SET_STORAGE_CLASS (symbolP, 0);
  SA_SET_SYM_FSIZE (symbolP, 0);

  alternateP = symbol_find (weak_name2altname (S_GET_NAME (symbolP)));
  S_CLEAR_EXTERNAL (alternateP);
}

#endif  /* TE_PE */

/* Handle .weak.  This is a GNU extension in formats other than PE. */

static void
obj_coff_weak (int ignore ATTRIBUTE_UNUSED)
{
  char *name;
  int c;
  symbolS *symbolP;

  do
    {
      name = input_line_pointer;
      c = get_symbol_end ();
      if (*name == 0)
	{
	  as_warn (_("badly formed .weak directive ignored"));
	  ignore_rest_of_line ();
	  return;
	}
      c = 0;
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

void
coff_obj_read_begin_hook (void)
{
  /* These had better be the same.  Usually 18 bytes.  */
  know (sizeof (SYMENT) == sizeof (AUXENT));
  know (SYMESZ == AUXESZ);
  tag_init ();
}

symbolS *coff_last_function;
#ifndef OBJ_XCOFF
static symbolS *coff_last_bf;
#endif

void
coff_frob_symbol (symbolS *symp, int *punt)
{
  static symbolS *last_tagP;
  static stack *block_stack;
  static symbolS *set_end;
  symbolS *next_set_end = NULL;

  if (symp == &abs_symbol)
    {
      *punt = 1;
      return;
    }

  if (current_lineno_sym)
    coff_add_linesym (NULL);

  if (!block_stack)
    block_stack = stack_init (512, sizeof (symbolS*));

#ifdef TE_PE
  if (S_GET_STORAGE_CLASS (symp) == C_NT_WEAK
      && ! S_IS_WEAK (symp)
      && weak_is_altname (S_GET_NAME (symp)))
    {
      /* This is a weak alternate symbol.  All processing of
	 PECOFFweak symbols is done here, through the alternate.  */
      symbolS *weakp = symbol_find_noref (weak_altname2name
					  (S_GET_NAME (symp)), 1);

      assert (weakp);
      assert (S_GET_NUMBER_AUXILIARY (weakp) == 1);

      if (! S_IS_WEAK (weakp))
	{
	  /* The symbol was turned from weak to strong.  Discard altname.  */
	  *punt = 1;
	  return;
	}
      else if (symbol_equated_p (weakp))
	{
	  /* The weak symbol has an alternate specified; symp is unneeded.  */
	  S_SET_STORAGE_CLASS (weakp, C_NT_WEAK);
	  SA_SET_SYM_TAGNDX (weakp,
	    symbol_get_value_expression (weakp)->X_add_symbol);

	  S_CLEAR_EXTERNAL (symp);
	  *punt = 1;
	  return;
	}
      else
	{
	  /* The weak symbol has been assigned an alternate value.
             Copy this value to symp, and set symp as weakp's alternate.  */
	  if (S_GET_STORAGE_CLASS (weakp) != C_NT_WEAK)
	    {
	      S_SET_STORAGE_CLASS (symp, S_GET_STORAGE_CLASS (weakp));
	      S_SET_STORAGE_CLASS (weakp, C_NT_WEAK);
	    }

	  if (S_IS_DEFINED (weakp))
	    {
	      /* This is a defined weak symbol.  Copy value information
	         from the weak symbol itself to the alternate symbol.  */
	      symbol_set_value_expression (symp,
					   symbol_get_value_expression (weakp));
	      symbol_set_frag (symp, symbol_get_frag (weakp));
	      S_SET_SEGMENT (symp, S_GET_SEGMENT (weakp));
	    }
	  else
	    {
	      /* This is an undefined weak symbol.
		 Define the alternate symbol to zero.  */
	      S_SET_VALUE (symp, 0);
	      S_SET_SEGMENT (symp, absolute_section);
	    }

	  S_SET_NAME (symp, weak_uniquify (S_GET_NAME (symp)));
	  S_SET_STORAGE_CLASS (symp, C_EXT);

	  S_SET_VALUE (weakp, 0);
	  S_SET_SEGMENT (weakp, undefined_section);
	}
    }
#else /* TE_PE */
  if (S_IS_WEAK (symp))
    S_SET_STORAGE_CLASS (symp, C_WEAKEXT);
#endif /* TE_PE */

  if (!S_IS_DEFINED (symp)
      && !S_IS_WEAK (symp)
      && S_GET_STORAGE_CLASS (symp) != C_STAT)
    S_SET_STORAGE_CLASS (symp, C_EXT);

  if (!SF_GET_DEBUG (symp))
    {
      symbolS * real;

      if (!SF_GET_LOCAL (symp)
	  && !SF_GET_STATICS (symp)
	  && S_GET_STORAGE_CLASS (symp) != C_LABEL
	  && symbol_constant_p (symp)
	  && (real = symbol_find_noref (S_GET_NAME (symp), 1))
	  && S_GET_STORAGE_CLASS (real) == C_NULL
	  && real != symp)
	{
	  c_symbol_merge (symp, real);
	  *punt = 1;
	  return;
	}

      if (!S_IS_DEFINED (symp) && !SF_GET_LOCAL (symp))
	{
	  assert (S_GET_VALUE (symp) == 0);
	  if (S_IS_WEAKREFD (symp))
	    *punt = 1;
	  else
	    S_SET_EXTERNAL (symp);
	}
      else if (S_GET_STORAGE_CLASS (symp) == C_NULL)
	{
	  if (S_GET_SEGMENT (symp) == text_section
	      && symp != seg_info (text_section)->sym)
	    S_SET_STORAGE_CLASS (symp, C_LABEL);
	  else
	    S_SET_STORAGE_CLASS (symp, C_STAT);
	}

      if (SF_GET_PROCESS (symp))
	{
	  if (S_GET_STORAGE_CLASS (symp) == C_BLOCK)
	    {
	      if (streq (S_GET_NAME (symp), ".bb"))
		stack_push (block_stack, (char *) &symp);
	      else
		{
		  symbolS *begin;

		  begin = *(symbolS **) stack_pop (block_stack);
		  if (begin == 0)
		    as_warn (_("mismatched .eb"));
		  else
		    next_set_end = begin;
		}
	    }

	  if (coff_last_function == 0 && SF_GET_FUNCTION (symp))
	    {
	      union internal_auxent *auxp;

	      coff_last_function = symp;
	      if (S_GET_NUMBER_AUXILIARY (symp) < 1)
		S_SET_NUMBER_AUXILIARY (symp, 1);
	      auxp = SYM_AUXENT (symp);
	      memset (auxp->x_sym.x_fcnary.x_ary.x_dimen, 0,
		      sizeof (auxp->x_sym.x_fcnary.x_ary.x_dimen));
	    }

	  if (S_GET_STORAGE_CLASS (symp) == C_EFCN)
	    {
	      if (coff_last_function == 0)
		as_fatal (_("C_EFCN symbol for %s out of scope"),
			  S_GET_NAME (symp));
	      SA_SET_SYM_FSIZE (coff_last_function,
				(long) (S_GET_VALUE (symp)
					- S_GET_VALUE (coff_last_function)));
	      next_set_end = coff_last_function;
	      coff_last_function = 0;
	    }
	}

      if (S_IS_EXTERNAL (symp))
	S_SET_STORAGE_CLASS (symp, C_EXT);
      else if (SF_GET_LOCAL (symp))
	*punt = 1;

      if (SF_GET_FUNCTION (symp))
	symbol_get_bfdsym (symp)->flags |= BSF_FUNCTION;
    }

  /* Double check weak symbols.  */
  if (S_IS_WEAK (symp) && S_IS_COMMON (symp))
    as_bad (_("Symbol `%s' can not be both weak and common"),
	    S_GET_NAME (symp));

  if (SF_GET_TAG (symp))
    last_tagP = symp;
  else if (S_GET_STORAGE_CLASS (symp) == C_EOS)
    next_set_end = last_tagP;

#ifdef OBJ_XCOFF
  /* This is pretty horrible, but we have to set *punt correctly in
     order to call SA_SET_SYM_ENDNDX correctly.  */
  if (! symbol_used_in_reloc_p (symp)
      && ((symbol_get_bfdsym (symp)->flags & BSF_SECTION_SYM) != 0
	  || (! (S_IS_EXTERNAL (symp) || S_IS_WEAK (symp))
	      && ! symbol_get_tc (symp)->output
	      && S_GET_STORAGE_CLASS (symp) != C_FILE)))
    *punt = 1;
#endif

  if (set_end != (symbolS *) NULL
      && ! *punt
      && ((symbol_get_bfdsym (symp)->flags & BSF_NOT_AT_END) != 0
	  || (S_IS_DEFINED (symp)
	      && ! S_IS_COMMON (symp)
	      && (! S_IS_EXTERNAL (symp) || SF_GET_FUNCTION (symp)))))
    {
      SA_SET_SYM_ENDNDX (set_end, symp);
      set_end = NULL;
    }

  if (next_set_end != NULL)
    {
      if (set_end != NULL)
	as_warn ("Warning: internal error: forgetting to set endndx of %s",
		 S_GET_NAME (set_end));
      set_end = next_set_end;
    }

#ifndef OBJ_XCOFF
  if (! *punt
      && S_GET_STORAGE_CLASS (symp) == C_FCN
      && streq (S_GET_NAME (symp), ".bf"))
    {
      if (coff_last_bf != NULL)
	SA_SET_SYM_ENDNDX (coff_last_bf, symp);
      coff_last_bf = symp;
    }
#endif
  if (coffsymbol (symbol_get_bfdsym (symp))->lineno)
    {
      int i;
      struct line_no *lptr;
      alent *l;

      lptr = (struct line_no *) coffsymbol (symbol_get_bfdsym (symp))->lineno;
      for (i = 0; lptr; lptr = lptr->next)
	i++;
      lptr = (struct line_no *) coffsymbol (symbol_get_bfdsym (symp))->lineno;

      /* We need i entries for line numbers, plus 1 for the first
	 entry which BFD will override, plus 1 for the last zero
	 entry (a marker for BFD).  */
      l = xmalloc ((i + 2) * sizeof (* l));
      coffsymbol (symbol_get_bfdsym (symp))->lineno = l;
      l[i + 1].line_number = 0;
      l[i + 1].u.sym = NULL;
      for (; i > 0; i--)
	{
	  if (lptr->frag)
	    lptr->l.u.offset += lptr->frag->fr_address / OCTETS_PER_BYTE;
	  l[i] = lptr->l;
	  lptr = lptr->next;
	}
    }
}

void
coff_adjust_section_syms (bfd *abfd ATTRIBUTE_UNUSED,
			  asection *sec,
			  void * x ATTRIBUTE_UNUSED)
{
  symbolS *secsym;
  segment_info_type *seginfo = seg_info (sec);
  int nlnno, nrelocs = 0;

  /* RS/6000 gas creates a .debug section manually in ppc_frob_file in
     tc-ppc.c.  Do not get confused by it.  */
  if (seginfo == NULL)
    return;

  if (streq (sec->name, ".text"))
    nlnno = coff_n_line_nos;
  else
    nlnno = 0;
  {
    /* @@ Hope that none of the fixups expand to more than one reloc
       entry...  */
    fixS *fixp = seginfo->fix_root;
    while (fixp)
      {
	if (! fixp->fx_done)
	  nrelocs++;
	fixp = fixp->fx_next;
      }
  }
  if (bfd_get_section_size (sec) == 0
      && nrelocs == 0
      && nlnno == 0
      && sec != text_section
      && sec != data_section
      && sec != bss_section)
    return;

  secsym = section_symbol (sec);
  /* This is an estimate; we'll plug in the real value using
     SET_SECTION_RELOCS later */
  SA_SET_SCN_NRELOC (secsym, nrelocs);
  SA_SET_SCN_NLINNO (secsym, nlnno);
}

void
coff_frob_file_after_relocs (void)
{
  bfd_map_over_sections (stdoutput, coff_adjust_section_syms, NULL);
}

/* Implement the .section pseudo op:
  	.section name {, "flags"}
                  ^         ^
                  |         +--- optional flags: 'b' for bss
                  |                              'i' for info
                  +-- section name               'l' for lib
                                                 'n' for noload
                                                 'o' for over
                                                 'w' for data
  						 'd' (apparently m88k for data)
                                                 'x' for text
  						 'r' for read-only data
  						 's' for shared data (PE)
   But if the argument is not a quoted string, treat it as a
   subsegment number.

   Note the 'a' flag is silently ignored.  This allows the same
   .section directive to be parsed in both ELF and COFF formats.  */

void
obj_coff_section (int ignore ATTRIBUTE_UNUSED)
{
  /* Strip out the section name.  */
  char *section_name;
  char c;
  char *name;
  unsigned int exp;
  flagword flags, oldflags;
  asection *sec;

  if (flag_mri)
    {
      char type;

      s_mri_sect (&type);
      return;
    }

  section_name = input_line_pointer;
  c = get_symbol_end ();

  name = xmalloc (input_line_pointer - section_name + 1);
  strcpy (name, section_name);

  *input_line_pointer = c;

  SKIP_WHITESPACE ();

  exp = 0;
  flags = SEC_NO_FLAGS;

  if (*input_line_pointer == ',')
    {
      ++input_line_pointer;
      SKIP_WHITESPACE ();
      if (*input_line_pointer != '"')
	exp = get_absolute_expression ();
      else
	{
	  unsigned char attr;
	  int readonly_removed = 0;
	  int load_removed = 0;

	  while (attr = *++input_line_pointer,
		 attr != '"'
		 && ! is_end_of_line[attr])
	    {
	      switch (attr)
		{
		case 'b':
		  /* Uninitialised data section.  */
		  flags |= SEC_ALLOC;
		  flags &=~ SEC_LOAD;
		  break;

		case 'n':
		  /* Section not loaded.  */
		  flags &=~ SEC_LOAD;
		  flags |= SEC_NEVER_LOAD;
		  load_removed = 1;
		  break;

		case 's':
		  /* Shared section.  */
		  flags |= SEC_COFF_SHARED;
		  /* Fall through.  */
		case 'd':
		  /* Data section.  */
		  flags |= SEC_DATA;
		  if (! load_removed)
		    flags |= SEC_LOAD;
		  flags &=~ SEC_READONLY;
		  break;

		case 'w':
		  /* Writable section.  */
		  flags &=~ SEC_READONLY;
		  readonly_removed = 1;
		  break;

		case 'a':
		  /* Ignore.  Here for compatibility with ELF.  */
		  break;

		case 'r': /* Read-only section.  Implies a data section.  */
		  readonly_removed = 0;
		  /* Fall through.  */
		case 'x': /* Executable section.  */
		  /* If we are setting the 'x' attribute or if the 'r'
		     attribute is being used to restore the readonly status
		     of a code section (eg "wxr") then set the SEC_CODE flag,
		     otherwise set the SEC_DATA flag.  */
		  flags |= (attr == 'x' || (flags & SEC_CODE) ? SEC_CODE : SEC_DATA);
		  if (! load_removed)
		    flags |= SEC_LOAD;
		  /* Note - the READONLY flag is set here, even for the 'x'
		     attribute in order to be compatible with the MSVC
		     linker.  */
		  if (! readonly_removed)
		    flags |= SEC_READONLY;
		  break;

		case 'i': /* STYP_INFO */
		case 'l': /* STYP_LIB */
		case 'o': /* STYP_OVER */
		  as_warn (_("unsupported section attribute '%c'"), attr);
		  break;

		default:
		  as_warn (_("unknown section attribute '%c'"), attr);
		  break;
		}
	    }
	  if (attr == '"')
	    ++input_line_pointer;
	}
    }

  sec = subseg_new (name, (subsegT) exp);

  oldflags = bfd_get_section_flags (stdoutput, sec);
  if (oldflags == SEC_NO_FLAGS)
    {
      /* Set section flags for a new section just created by subseg_new.
         Provide a default if no flags were parsed.  */
      if (flags == SEC_NO_FLAGS)
	flags = TC_COFF_SECTION_DEFAULT_ATTRIBUTES;

#ifdef COFF_LONG_SECTION_NAMES
      /* Add SEC_LINK_ONCE and SEC_LINK_DUPLICATES_DISCARD to .gnu.linkonce
         sections so adjust_reloc_syms in write.c will correctly handle
         relocs which refer to non-local symbols in these sections.  */
      if (strneq (name, ".gnu.linkonce", sizeof (".gnu.linkonce") - 1))
	flags |= SEC_LINK_ONCE | SEC_LINK_DUPLICATES_DISCARD;
#endif

      if (! bfd_set_section_flags (stdoutput, sec, flags))
	as_warn (_("error setting flags for \"%s\": %s"),
		 bfd_section_name (stdoutput, sec),
		 bfd_errmsg (bfd_get_error ()));
    }
  else if (flags != SEC_NO_FLAGS)
    {
      /* This section's attributes have already been set.  Warn if the
         attributes don't match.  */
      flagword matchflags = (SEC_ALLOC | SEC_LOAD | SEC_READONLY | SEC_CODE
			     | SEC_DATA | SEC_COFF_SHARED | SEC_NEVER_LOAD);
      if ((flags ^ oldflags) & matchflags)
	as_warn (_("Ignoring changed section attributes for %s"), name);
    }

  demand_empty_rest_of_line ();
}

void
coff_adjust_symtab (void)
{
  if (symbol_rootP == NULL
      || S_GET_STORAGE_CLASS (symbol_rootP) != C_FILE)
    c_dot_file_symbol ("fake", 0);
}

void
coff_frob_section (segT sec)
{
  segT strsec;
  char *p;
  fragS *fragp;
  bfd_vma size, n_entries, mask;
  bfd_vma align_power = (bfd_vma)sec->alignment_power + OCTETS_PER_BYTE_POWER;

  /* The COFF back end in BFD requires that all section sizes be
     rounded up to multiples of the corresponding section alignments,
     supposedly because standard COFF has no other way of encoding alignment
     for sections.  If your COFF flavor has a different way of encoding
     section alignment, then skip this step, as TICOFF does.  */
  size = bfd_get_section_size (sec);
  mask = ((bfd_vma) 1 << align_power) - 1;
#if !defined(TICOFF)
  if (size & mask)
    {
      bfd_vma new_size;
      fragS *last;

      new_size = (size + mask) & ~mask;
      bfd_set_section_size (stdoutput, sec, new_size);

      /* If the size had to be rounded up, add some padding in
         the last non-empty frag.  */
      fragp = seg_info (sec)->frchainP->frch_root;
      last = seg_info (sec)->frchainP->frch_last;
      while (fragp->fr_next != last)
	fragp = fragp->fr_next;
      last->fr_address = size;
      fragp->fr_offset += new_size - size;
    }
#endif

  /* If the section size is non-zero, the section symbol needs an aux
     entry associated with it, indicating the size.  We don't know
     all the values yet; coff_frob_symbol will fill them in later.  */
#ifndef TICOFF
  if (size != 0
      || sec == text_section
      || sec == data_section
      || sec == bss_section)
#endif
    {
      symbolS *secsym = section_symbol (sec);

      S_SET_STORAGE_CLASS (secsym, C_STAT);
      S_SET_NUMBER_AUXILIARY (secsym, 1);
      SF_SET_STATICS (secsym);
      SA_SET_SCN_SCNLEN (secsym, size);
    }

  /* FIXME: These should be in a "stabs.h" file, or maybe as.h.  */
#ifndef STAB_SECTION_NAME
#define STAB_SECTION_NAME ".stab"
#endif
#ifndef STAB_STRING_SECTION_NAME
#define STAB_STRING_SECTION_NAME ".stabstr"
#endif
  if (! streq (STAB_STRING_SECTION_NAME, sec->name))
    return;

  strsec = sec;
  sec = subseg_get (STAB_SECTION_NAME, 0);
  /* size is already rounded up, since other section will be listed first */
  size = bfd_get_section_size (strsec);

  n_entries = bfd_get_section_size (sec) / 12 - 1;

  /* Find first non-empty frag.  It should be large enough.  */
  fragp = seg_info (sec)->frchainP->frch_root;
  while (fragp && fragp->fr_fix == 0)
    fragp = fragp->fr_next;
  assert (fragp != 0 && fragp->fr_fix >= 12);

  /* Store the values.  */
  p = fragp->fr_literal;
  bfd_h_put_16 (stdoutput, n_entries, (bfd_byte *) p + 6);
  bfd_h_put_32 (stdoutput, size, (bfd_byte *) p + 8);
}

void
obj_coff_init_stab_section (segT seg)
{
  char *file;
  char *p;
  char *stabstr_name;
  unsigned int stroff;

  /* Make space for this first symbol.  */
  p = frag_more (12);
  /* Zero it out.  */
  memset (p, 0, 12);
  as_where (&file, (unsigned int *) NULL);
  stabstr_name = xmalloc (strlen (seg->name) + 4);
  strcpy (stabstr_name, seg->name);
  strcat (stabstr_name, "str");
  stroff = get_stab_string_offset (file, stabstr_name);
  know (stroff == 1);
  md_number_to_chars (p, stroff, 4);
}

#ifdef DEBUG
const char *
s_get_name (symbolS *s)
{
  return ((s == NULL) ? "(NULL)" : S_GET_NAME (s));
}

void
symbol_dump (void)
{
  symbolS *symbolP;

  for (symbolP = symbol_rootP; symbolP; symbolP = symbol_next (symbolP))
    printf (_("0x%lx: \"%s\" type = %ld, class = %d, segment = %d\n"),
	    (unsigned long) symbolP,
	    S_GET_NAME (symbolP),
	    (long) S_GET_DATA_TYPE (symbolP),
	    S_GET_STORAGE_CLASS (symbolP),
	    (int) S_GET_SEGMENT (symbolP));
}

#endif /* DEBUG */

const pseudo_typeS coff_pseudo_table[] =
{
  {"ABORT", s_abort, 0},
  {"appline", obj_coff_ln, 1},
  /* We accept the .bss directive for backward compatibility with
     earlier versions of gas.  */
  {"bss", obj_coff_bss, 0},
  {"def", obj_coff_def, 0},
  {"dim", obj_coff_dim, 0},
  {"endef", obj_coff_endef, 0},
  {"ident", obj_coff_ident, 0},
  {"line", obj_coff_line, 0},
  {"ln", obj_coff_ln, 0},
  {"scl", obj_coff_scl, 0},
  {"sect", obj_coff_section, 0},
  {"sect.s", obj_coff_section, 0},
  {"section", obj_coff_section, 0},
  {"section.s", obj_coff_section, 0},
  /* FIXME: We ignore the MRI short attribute.  */
  {"size", obj_coff_size, 0},
  {"tag", obj_coff_tag, 0},
  {"type", obj_coff_type, 0},
  {"val", obj_coff_val, 0},
  {"version", s_ignore, 0},
  {"loc", obj_coff_loc, 0},
  {"optim", s_ignore, 0},	/* For sun386i cc (?) */
  {"weak", obj_coff_weak, 0},
#if defined TC_TIC4X
  /* The tic4x uses sdef instead of def.  */
  {"sdef", obj_coff_def, 0},
#endif
  {NULL, NULL, 0}
};


/* Support for a COFF emulation.  */

static void
coff_pop_insert (void)
{
  pop_insert (coff_pseudo_table);
}

static int
coff_separate_stab_sections (void)
{
  return 1;
}

const struct format_ops coff_format_ops =
{
  bfd_target_coff_flavour,
  0,	/* dfl_leading_underscore */
  1,	/* emit_section_symbols */
  0,    /* begin */
  c_dot_file_symbol,
  coff_frob_symbol,
  0,	/* frob_file */
  0,	/* frob_file_before_adjust */
  0,	/* frob_file_before_fix */
  coff_frob_file_after_relocs,
  0,	/* s_get_size */
  0,	/* s_set_size */
  0,	/* s_get_align */
  0,	/* s_set_align */
  0,	/* s_get_other */
  0,	/* s_set_other */
  0,	/* s_get_desc */
  0,	/* s_set_desc */
  0,	/* s_get_type */
  0,	/* s_set_type */
  0,	/* copy_symbol_attributes */
  0,	/* generate_asm_lineno */
  0,	/* process_stab */
  coff_separate_stab_sections,
  obj_coff_init_stab_section,
  0,	/* sec_sym_ok_for_reloc */
  coff_pop_insert,
  0,	/* ecoff_set_ext */
  coff_obj_read_begin_hook,
  coff_obj_symbol_new_hook
};
