/* symbols.c -symbol table-
   Copyright 1987, 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007
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

/* #define DEBUG_SYMS / * to debug symbol list maintenance.  */

#include "as.h"

#include "safe-ctype.h"
#include "obstack.h"		/* For "symbols.h" */
#include "subsegs.h"

#include "struc-symbol.h"

/* This is non-zero if symbols are case sensitive, which is the
   default.  */
int symbols_case_sensitive = 1;

#ifndef WORKING_DOT_WORD
extern int new_broken_words;
#endif

/* symbol-name => struct symbol pointer */
static struct hash_control *sy_hash;

/* Table of local symbols.  */
static struct hash_control *local_hash;

/* Below are commented in "symbols.h".  */
symbolS *symbol_rootP;
symbolS *symbol_lastP;
symbolS abs_symbol;

#ifdef DEBUG_SYMS
#define debug_verify_symchain verify_symbol_chain
#else
#define debug_verify_symchain(root, last) ((void) 0)
#endif

#define DOLLAR_LABEL_CHAR	'\001'
#define LOCAL_LABEL_CHAR	'\002'

struct obstack notes;
#ifdef USE_UNIQUE
/* The name of an external symbol which is
   used to make weak PE symbol names unique.  */
const char * an_external_name;
#endif

static char *save_symbol_name (const char *);
static void fb_label_init (void);
static long dollar_label_instance (long);
static long fb_label_instance (long);

static void print_binary (FILE *, const char *, expressionS *);
static void report_op_error (symbolS *, symbolS *, symbolS *);

/* Return a pointer to a new symbol.  Die if we can't make a new
   symbol.  Fill in the symbol's values.  Add symbol to end of symbol
   chain.

   This function should be called in the general case of creating a
   symbol.  However, if the output file symbol table has already been
   set, and you are certain that this symbol won't be wanted in the
   output file, you can call symbol_create.  */

symbolS *
symbol_new (const char *name, segT segment, valueT valu, fragS *frag)
{
  symbolS *symbolP = symbol_create (name, segment, valu, frag);

  /* Link to end of symbol chain.  */
  {
    extern int symbol_table_frozen;
    if (symbol_table_frozen)
      abort ();
  }
  symbol_append (symbolP, symbol_lastP, &symbol_rootP, &symbol_lastP);

  return symbolP;
}

/* Save a symbol name on a permanent obstack, and convert it according
   to the object file format.  */

static char *
save_symbol_name (const char *name)
{
  unsigned int name_length;
  char *ret;

  name_length = strlen (name) + 1;	/* +1 for \0.  */
  obstack_grow (&notes, name, name_length);
  ret = obstack_finish (&notes);

#ifdef tc_canonicalize_symbol_name
  ret = tc_canonicalize_symbol_name (ret);
#endif

  if (! symbols_case_sensitive)
    {
      char *s;

      for (s = ret; *s != '\0'; s++)
	*s = TOUPPER (*s);
    }

  return ret;
}

symbolS *
symbol_create (const char *name, /* It is copied, the caller can destroy/modify.  */
	       segT segment,	/* Segment identifier (SEG_<something>).  */
	       valueT valu,	/* Symbol value.  */
	       fragS *frag	/* Associated fragment.  */)
{
  char *preserved_copy_of_name;
  symbolS *symbolP;

  preserved_copy_of_name = save_symbol_name (name);

  symbolP = (symbolS *) obstack_alloc (&notes, sizeof (symbolS));

  /* symbol must be born in some fixed state.  This seems as good as any.  */
  memset (symbolP, 0, sizeof (symbolS));

  symbolP->bsym = bfd_make_empty_symbol (stdoutput);
  if (symbolP->bsym == NULL)
    as_fatal ("bfd_make_empty_symbol: %s", bfd_errmsg (bfd_get_error ()));
  S_SET_NAME (symbolP, preserved_copy_of_name);

  S_SET_SEGMENT (symbolP, segment);
  S_SET_VALUE (symbolP, valu);
  symbol_clear_list_pointers (symbolP);

  symbolP->sy_frag = frag;

  obj_symbol_new_hook (symbolP);

#ifdef tc_symbol_new_hook
  tc_symbol_new_hook (symbolP);
#endif

  return symbolP;
}


/* Local symbol support.  If we can get away with it, we keep only a
   small amount of information for local symbols.  */

static symbolS *local_symbol_convert (struct local_symbol *);

/* Used for statistics.  */

static unsigned long local_symbol_count;
static unsigned long local_symbol_conversion_count;

/* This macro is called with a symbol argument passed by reference.
   It returns whether this is a local symbol.  If necessary, it
   changes its argument to the real symbol.  */

#define LOCAL_SYMBOL_CHECK(s)						\
  (s->bsym == NULL							\
   ? (local_symbol_converted_p ((struct local_symbol *) s)		\
      ? (s = local_symbol_get_real_symbol ((struct local_symbol *) s),	\
	 0)								\
      : 1)								\
   : 0)

/* Create a local symbol and insert it into the local hash table.  */

static struct local_symbol *
local_symbol_make (const char *name, segT section, valueT value, fragS *frag)
{
  char *name_copy;
  struct local_symbol *ret;

  ++local_symbol_count;

  name_copy = save_symbol_name (name);

  ret = (struct local_symbol *) obstack_alloc (&notes, sizeof *ret);
  ret->lsy_marker = NULL;
  ret->lsy_name = name_copy;
  ret->lsy_section = section;
  local_symbol_set_frag (ret, frag);
  ret->lsy_value = value;

  hash_jam (local_hash, name_copy, (PTR) ret);

  return ret;
}

/* Convert a local symbol into a real symbol.  Note that we do not
   reclaim the space used by the local symbol.  */

static symbolS *
local_symbol_convert (struct local_symbol *locsym)
{
  symbolS *ret;

  assert (locsym->lsy_marker == NULL);
  if (local_symbol_converted_p (locsym))
    return local_symbol_get_real_symbol (locsym);

  ++local_symbol_conversion_count;

  ret = symbol_new (locsym->lsy_name, locsym->lsy_section, locsym->lsy_value,
		    local_symbol_get_frag (locsym));

  if (local_symbol_resolved_p (locsym))
    ret->sy_resolved = 1;

  /* Local symbols are always either defined or used.  */
  ret->sy_used = 1;

#ifdef TC_LOCAL_SYMFIELD_CONVERT
  TC_LOCAL_SYMFIELD_CONVERT (locsym, ret);
#endif

  symbol_table_insert (ret);

  local_symbol_mark_converted (locsym);
  local_symbol_set_real_symbol (locsym, ret);

  hash_jam (local_hash, locsym->lsy_name, NULL);

  return ret;
}

/* We have just seen "<name>:".
   Creates a struct symbol unless it already exists.

   Gripes if we are redefining a symbol incompatibly (and ignores it).  */

symbolS *
colon (/* Just seen "x:" - rattle symbols & frags.  */
       const char *sym_name	/* Symbol name, as a cannonical string.  */
       /* We copy this string: OK to alter later.  */)
{
  register symbolS *symbolP;	/* Symbol we are working with.  */

  /* Sun local labels go out of scope whenever a non-local symbol is
     defined.  */
  if (LOCAL_LABELS_DOLLAR
      && !bfd_is_local_label_name (stdoutput, sym_name))
    dollar_label_clear ();

#ifndef WORKING_DOT_WORD
  if (new_broken_words)
    {
      struct broken_word *a;
      int possible_bytes;
      fragS *frag_tmp;
      char *frag_opcode;

      if (now_seg == absolute_section)
	{
	  as_bad (_("cannot define symbol `%s' in absolute section"), sym_name);
	  return NULL;
	}

      possible_bytes = (md_short_jump_size
			+ new_broken_words * md_long_jump_size);

      frag_tmp = frag_now;
      frag_opcode = frag_var (rs_broken_word,
			      possible_bytes,
			      possible_bytes,
			      (relax_substateT) 0,
			      (symbolS *) broken_words,
			      (offsetT) 0,
			      NULL);

      /* We want to store the pointer to where to insert the jump
	 table in the fr_opcode of the rs_broken_word frag.  This
	 requires a little hackery.  */
      while (frag_tmp
	     && (frag_tmp->fr_type != rs_broken_word
		 || frag_tmp->fr_opcode))
	frag_tmp = frag_tmp->fr_next;
      know (frag_tmp);
      frag_tmp->fr_opcode = frag_opcode;
      new_broken_words = 0;

      for (a = broken_words; a && a->dispfrag == 0; a = a->next_broken_word)
	a->dispfrag = frag_tmp;
    }
#endif /* WORKING_DOT_WORD */

  if ((symbolP = symbol_find (sym_name)) != 0)
    {
      S_CLEAR_WEAKREFR (symbolP);
#ifdef RESOLVE_SYMBOL_REDEFINITION
      if (RESOLVE_SYMBOL_REDEFINITION (symbolP))
	return symbolP;
#endif
      /* Now check for undefined symbols.  */
      if (LOCAL_SYMBOL_CHECK (symbolP))
	{
	  struct local_symbol *locsym = (struct local_symbol *) symbolP;

	  if (locsym->lsy_section != undefined_section
	      && (local_symbol_get_frag (locsym) != frag_now
		  || locsym->lsy_section != now_seg
		  || locsym->lsy_value != frag_now_fix ()))
	    {
	      as_bad (_("symbol `%s' is already defined"), sym_name);
	      return symbolP;
	    }

	  locsym->lsy_section = now_seg;
	  local_symbol_set_frag (locsym, frag_now);
	  locsym->lsy_value = frag_now_fix ();
	}
      else if (!(S_IS_DEFINED (symbolP) || symbol_equated_p (symbolP))
	       || S_IS_COMMON (symbolP)
	       || S_IS_VOLATILE (symbolP))
	{
	  if (S_IS_VOLATILE (symbolP))
	    {
	      symbolP = symbol_clone (symbolP, 1);
	      S_SET_VALUE (symbolP, 0);
	      S_CLEAR_VOLATILE (symbolP);
	    }
	  if (S_GET_VALUE (symbolP) == 0)
	    {
	      symbolP->sy_frag = frag_now;
#ifdef OBJ_VMS
	      S_SET_OTHER (symbolP, const_flag);
#endif
	      S_SET_VALUE (symbolP, (valueT) frag_now_fix ());
	      S_SET_SEGMENT (symbolP, now_seg);
#ifdef N_UNDF
	      know (N_UNDF == 0);
#endif /* if we have one, it better be zero.  */

	    }
	  else
	    {
	      /* There are still several cases to check:

		 A .comm/.lcomm symbol being redefined as initialized
		 data is OK

		 A .comm/.lcomm symbol being redefined with a larger
		 size is also OK

		 This only used to be allowed on VMS gas, but Sun cc
		 on the sparc also depends on it.  */

	      if (((!S_IS_DEBUG (symbolP)
		    && (!S_IS_DEFINED (symbolP) || S_IS_COMMON (symbolP))
		    && S_IS_EXTERNAL (symbolP))
		   || S_GET_SEGMENT (symbolP) == bss_section)
		  && (now_seg == data_section
		      || now_seg == bss_section
		      || now_seg == S_GET_SEGMENT (symbolP)))
		{
		  /* Select which of the 2 cases this is.  */
		  if (now_seg != data_section)
		    {
		      /* New .comm for prev .comm symbol.

			 If the new size is larger we just change its
			 value.  If the new size is smaller, we ignore
			 this symbol.  */
		      if (S_GET_VALUE (symbolP)
			  < ((unsigned) frag_now_fix ()))
			{
			  S_SET_VALUE (symbolP, (valueT) frag_now_fix ());
			}
		    }
		  else
		    {
		      /* It is a .comm/.lcomm being converted to initialized
			 data.  */
		      symbolP->sy_frag = frag_now;
#ifdef OBJ_VMS
		      S_SET_OTHER (symbolP, const_flag);
#endif
		      S_SET_VALUE (symbolP, (valueT) frag_now_fix ());
		      S_SET_SEGMENT (symbolP, now_seg);	/* Keep N_EXT bit.  */
		    }
		}
	      else
		{
#if (!defined (OBJ_AOUT) && !defined (OBJ_MAYBE_AOUT) \
     && !defined (OBJ_BOUT) && !defined (OBJ_MAYBE_BOUT))
		  static const char *od_buf = "";
#else
		  char od_buf[100];
		  od_buf[0] = '\0';
		  if (OUTPUT_FLAVOR == bfd_target_aout_flavour)
		    sprintf (od_buf, "%d.%d.",
			     S_GET_OTHER (symbolP),
			     S_GET_DESC (symbolP));
#endif
		  as_bad (_("symbol `%s' is already defined as \"%s\"/%s%ld"),
			    sym_name,
			    segment_name (S_GET_SEGMENT (symbolP)),
			    od_buf,
			    (long) S_GET_VALUE (symbolP));
		}
	    }			/* if the undefined symbol has no value  */
	}
      else
	{
	  /* Don't blow up if the definition is the same.  */
	  if (!(frag_now == symbolP->sy_frag
		&& S_GET_VALUE (symbolP) == frag_now_fix ()
		&& S_GET_SEGMENT (symbolP) == now_seg))
	    {
	      as_bad (_("symbol `%s' is already defined"), sym_name);
	      symbolP = symbol_clone (symbolP, 0);
	    }
	}

    }
  else if (! flag_keep_locals && bfd_is_local_label_name (stdoutput, sym_name))
    {
      symbolP = (symbolS *) local_symbol_make (sym_name, now_seg,
					       (valueT) frag_now_fix (),
					       frag_now);
    }
  else
    {
      symbolP = symbol_new (sym_name, now_seg, (valueT) frag_now_fix (),
			    frag_now);
#ifdef OBJ_VMS
      S_SET_OTHER (symbolP, const_flag);
#endif /* OBJ_VMS */

      symbol_table_insert (symbolP);
    }

  if (mri_common_symbol != NULL)
    {
      /* This symbol is actually being defined within an MRI common
	 section.  This requires special handling.  */
      if (LOCAL_SYMBOL_CHECK (symbolP))
	symbolP = local_symbol_convert ((struct local_symbol *) symbolP);
      symbolP->sy_value.X_op = O_symbol;
      symbolP->sy_value.X_add_symbol = mri_common_symbol;
      symbolP->sy_value.X_add_number = S_GET_VALUE (mri_common_symbol);
      symbolP->sy_frag = &zero_address_frag;
      S_SET_SEGMENT (symbolP, expr_section);
      symbolP->sy_mri_common = 1;
    }

#ifdef tc_frob_label
  tc_frob_label (symbolP);
#endif
#ifdef obj_frob_label
  obj_frob_label (symbolP);
#endif

  return symbolP;
}

/* Die if we can't insert the symbol.  */

void
symbol_table_insert (symbolS *symbolP)
{
  register const char *error_string;

  know (symbolP);
  know (S_GET_NAME (symbolP));

  if (LOCAL_SYMBOL_CHECK (symbolP))
    {
      error_string = hash_jam (local_hash, S_GET_NAME (symbolP),
			       (PTR) symbolP);
      if (error_string != NULL)
	as_fatal (_("inserting \"%s\" into symbol table failed: %s"),
		  S_GET_NAME (symbolP), error_string);
      return;
    }

  if ((error_string = hash_jam (sy_hash, S_GET_NAME (symbolP), (PTR) symbolP)))
    {
      as_fatal (_("inserting \"%s\" into symbol table failed: %s"),
		S_GET_NAME (symbolP), error_string);
    }				/* on error  */
}

/* If a symbol name does not exist, create it as undefined, and insert
   it into the symbol table.  Return a pointer to it.  */

symbolS *
symbol_find_or_make (const char *name)
{
  register symbolS *symbolP;

  symbolP = symbol_find (name);

  if (symbolP == NULL)
    {
      if (! flag_keep_locals && bfd_is_local_label_name (stdoutput, name))
	{
	  symbolP = md_undefined_symbol ((char *) name);
	  if (symbolP != NULL)
	    return symbolP;

	  symbolP = (symbolS *) local_symbol_make (name, undefined_section,
						   (valueT) 0,
						   &zero_address_frag);
	  return symbolP;
	}

      symbolP = symbol_make (name);

      symbol_table_insert (symbolP);
    }				/* if symbol wasn't found */

  return (symbolP);
}

symbolS *
symbol_make (const char *name)
{
  symbolS *symbolP;

  /* Let the machine description default it, e.g. for register names.  */
  symbolP = md_undefined_symbol ((char *) name);

  if (!symbolP)
    symbolP = symbol_new (name, undefined_section, (valueT) 0, &zero_address_frag);

  return (symbolP);
}

symbolS *
symbol_clone (symbolS *orgsymP, int replace)
{
  symbolS *newsymP;
  asymbol *bsymorg, *bsymnew;

  /* Running local_symbol_convert on a clone that's not the one currently
     in local_hash would incorrectly replace the hash entry.  Thus the
     symbol must be converted here.  Note that the rest of the function
     depends on not encountering an unconverted symbol.  */
  if (LOCAL_SYMBOL_CHECK (orgsymP))
    orgsymP = local_symbol_convert ((struct local_symbol *) orgsymP);
  bsymorg = orgsymP->bsym;

  newsymP = obstack_alloc (&notes, sizeof (*newsymP));
  *newsymP = *orgsymP;
  bsymnew = bfd_make_empty_symbol (bfd_asymbol_bfd (bsymorg));
  if (bsymnew == NULL)
    as_fatal ("bfd_make_empty_symbol: %s", bfd_errmsg (bfd_get_error ()));
  newsymP->bsym = bsymnew;
  bsymnew->name = bsymorg->name;
  bsymnew->flags =  bsymorg->flags;
  bsymnew->section =  bsymorg->section;
  bfd_copy_private_symbol_data (bfd_asymbol_bfd (bsymorg), bsymorg,
				bfd_asymbol_bfd (bsymnew), bsymnew);

#ifdef obj_symbol_clone_hook
  obj_symbol_clone_hook (newsymP, orgsymP);
#endif

#ifdef tc_symbol_clone_hook
  tc_symbol_clone_hook (newsymP, orgsymP);
#endif

  if (replace)
    {
      if (symbol_rootP == orgsymP)
	symbol_rootP = newsymP;
      else if (orgsymP->sy_previous)
	{
	  orgsymP->sy_previous->sy_next = newsymP;
	  orgsymP->sy_previous = NULL;
	}
      if (symbol_lastP == orgsymP)
	symbol_lastP = newsymP;
      else if (orgsymP->sy_next)
	orgsymP->sy_next->sy_previous = newsymP;
      orgsymP->sy_previous = orgsymP->sy_next = orgsymP;
      debug_verify_symchain (symbol_rootP, symbol_lastP);

      symbol_table_insert (newsymP);
    }
  else
    newsymP->sy_previous = newsymP->sy_next = newsymP;

  return newsymP;
}

/* Referenced symbols, if they are forward references, need to be cloned
   (without replacing the original) so that the value of the referenced
   symbols at the point of use .  */

#undef symbol_clone_if_forward_ref
symbolS *
symbol_clone_if_forward_ref (symbolS *symbolP, int is_forward)
{
  if (symbolP && !LOCAL_SYMBOL_CHECK (symbolP))
    {
      symbolS *add_symbol = symbolP->sy_value.X_add_symbol;
      symbolS *op_symbol = symbolP->sy_value.X_op_symbol;

      if (symbolP->sy_forward_ref)
	is_forward = 1;

      if (is_forward)
	{
	  /* assign_symbol() clones volatile symbols; pre-existing expressions
	     hold references to the original instance, but want the current
	     value.  Just repeat the lookup.  */
	  if (add_symbol && S_IS_VOLATILE (add_symbol))
	    add_symbol = symbol_find_exact (S_GET_NAME (add_symbol));
	  if (op_symbol && S_IS_VOLATILE (op_symbol))
	    op_symbol = symbol_find_exact (S_GET_NAME (op_symbol));
	}

      /* Re-using sy_resolving here, as this routine cannot get called from
	 symbol resolution code.  */
      if (symbolP->bsym->section == expr_section && !symbolP->sy_resolving)
	{
	  symbolP->sy_resolving = 1;
	  add_symbol = symbol_clone_if_forward_ref (add_symbol, is_forward);
	  op_symbol = symbol_clone_if_forward_ref (op_symbol, is_forward);
	  symbolP->sy_resolving = 0;
	}

      if (symbolP->sy_forward_ref
	  || add_symbol != symbolP->sy_value.X_add_symbol
	  || op_symbol != symbolP->sy_value.X_op_symbol)
	symbolP = symbol_clone (symbolP, 0);

      symbolP->sy_value.X_add_symbol = add_symbol;
      symbolP->sy_value.X_op_symbol = op_symbol;
    }

  return symbolP;
}

symbolS *
symbol_temp_new (segT seg, valueT ofs, fragS *frag)
{
  return symbol_new (FAKE_LABEL_NAME, seg, ofs, frag);
}

symbolS *
symbol_temp_new_now (void)
{
  return symbol_temp_new (now_seg, frag_now_fix (), frag_now);
}

symbolS *
symbol_temp_make (void)
{
  return symbol_make (FAKE_LABEL_NAME);
}

/* Implement symbol table lookup.
   In:	A symbol's name as a string: '\0' can't be part of a symbol name.
   Out:	NULL if the name was not in the symbol table, else the address
   of a struct symbol associated with that name.  */

symbolS *
symbol_find_exact (const char *name)
{
  return symbol_find_exact_noref (name, 0);
}

symbolS *
symbol_find_exact_noref (const char *name, int noref)
{
  struct local_symbol *locsym;
  symbolS* sym;

  locsym = (struct local_symbol *) hash_find (local_hash, name);
  if (locsym != NULL)
    return (symbolS *) locsym;

  sym = ((symbolS *) hash_find (sy_hash, name));

  /* Any references to the symbol, except for the reference in
     .weakref, must clear this flag, such that the symbol does not
     turn into a weak symbol.  Note that we don't have to handle the
     local_symbol case, since a weakrefd is always promoted out of the
     local_symbol table when it is turned into a weak symbol.  */
  if (sym && ! noref)
    S_CLEAR_WEAKREFD (sym);

  return sym;
}

symbolS *
symbol_find (const char *name)
{
  return symbol_find_noref (name, 0);
}

symbolS *
symbol_find_noref (const char *name, int noref)
{
#ifdef tc_canonicalize_symbol_name
  {
    char *copy;
    size_t len = strlen (name) + 1;

    copy = (char *) alloca (len);
    memcpy (copy, name, len);
    name = tc_canonicalize_symbol_name (copy);
  }
#endif

  if (! symbols_case_sensitive)
    {
      char *copy;
      const char *orig;
      unsigned char c;

      orig = name;
      name = copy = (char *) alloca (strlen (name) + 1);

      while ((c = *orig++) != '\0')
	{
	  *copy++ = TOUPPER (c);
	}
      *copy = '\0';
    }

  return symbol_find_exact_noref (name, noref);
}

/* Once upon a time, symbols were kept in a singly linked list.  At
   least coff needs to be able to rearrange them from time to time, for
   which a doubly linked list is much more convenient.  Loic did these
   as macros which seemed dangerous to me so they're now functions.
   xoxorich.  */

/* Link symbol ADDME after symbol TARGET in the chain.  */

void
symbol_append (symbolS *addme, symbolS *target,
	       symbolS **rootPP, symbolS **lastPP)
{
  if (LOCAL_SYMBOL_CHECK (addme))
    abort ();
  if (target != NULL && LOCAL_SYMBOL_CHECK (target))
    abort ();

  if (target == NULL)
    {
      know (*rootPP == NULL);
      know (*lastPP == NULL);
      addme->sy_next = NULL;
      addme->sy_previous = NULL;
      *rootPP = addme;
      *lastPP = addme;
      return;
    }				/* if the list is empty  */

  if (target->sy_next != NULL)
    {
      target->sy_next->sy_previous = addme;
    }
  else
    {
      know (*lastPP == target);
      *lastPP = addme;
    }				/* if we have a next  */

  addme->sy_next = target->sy_next;
  target->sy_next = addme;
  addme->sy_previous = target;

  debug_verify_symchain (symbol_rootP, symbol_lastP);
}

/* Set the chain pointers of SYMBOL to null.  */

void
symbol_clear_list_pointers (symbolS *symbolP)
{
  if (LOCAL_SYMBOL_CHECK (symbolP))
    abort ();
  symbolP->sy_next = NULL;
  symbolP->sy_previous = NULL;
}

/* Remove SYMBOLP from the list.  */

void
symbol_remove (symbolS *symbolP, symbolS **rootPP, symbolS **lastPP)
{
  if (LOCAL_SYMBOL_CHECK (symbolP))
    abort ();

  if (symbolP == *rootPP)
    {
      *rootPP = symbolP->sy_next;
    }				/* if it was the root  */

  if (symbolP == *lastPP)
    {
      *lastPP = symbolP->sy_previous;
    }				/* if it was the tail  */

  if (symbolP->sy_next != NULL)
    {
      symbolP->sy_next->sy_previous = symbolP->sy_previous;
    }				/* if not last  */

  if (symbolP->sy_previous != NULL)
    {
      symbolP->sy_previous->sy_next = symbolP->sy_next;
    }				/* if not first  */

  debug_verify_symchain (*rootPP, *lastPP);
}

/* Link symbol ADDME before symbol TARGET in the chain.  */

void
symbol_insert (symbolS *addme, symbolS *target,
	       symbolS **rootPP, symbolS **lastPP ATTRIBUTE_UNUSED)
{
  if (LOCAL_SYMBOL_CHECK (addme))
    abort ();
  if (LOCAL_SYMBOL_CHECK (target))
    abort ();

  if (target->sy_previous != NULL)
    {
      target->sy_previous->sy_next = addme;
    }
  else
    {
      know (*rootPP == target);
      *rootPP = addme;
    }				/* if not first  */

  addme->sy_previous = target->sy_previous;
  target->sy_previous = addme;
  addme->sy_next = target;

  debug_verify_symchain (*rootPP, *lastPP);
}

void
verify_symbol_chain (symbolS *rootP, symbolS *lastP)
{
  symbolS *symbolP = rootP;

  if (symbolP == NULL)
    return;

  for (; symbol_next (symbolP) != NULL; symbolP = symbol_next (symbolP))
    {
      assert (symbolP->bsym != NULL);
      assert (symbolP->sy_next->sy_previous == symbolP);
    }

  assert (lastP == symbolP);
}

#ifdef OBJ_COMPLEX_RELC

static int
use_complex_relocs_for (symbolS * symp)
{
  switch (symp->sy_value.X_op)
    {
    case O_constant:
      return 0;

    case O_symbol:
    case O_symbol_rva:
    case O_uminus:
    case O_bit_not:
    case O_logical_not:
      if (  (S_IS_COMMON (symp->sy_value.X_add_symbol)
	   || S_IS_LOCAL (symp->sy_value.X_add_symbol))
	  &&
	      (S_IS_DEFINED (symp->sy_value.X_add_symbol)
	   && S_GET_SEGMENT (symp->sy_value.X_add_symbol) != expr_section))
	return 0;
      break;

    case O_multiply:
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

      if (  (S_IS_COMMON (symp->sy_value.X_add_symbol)
	   || S_IS_LOCAL (symp->sy_value.X_add_symbol))
	  && 
	    (S_IS_COMMON (symp->sy_value.X_op_symbol)
	   || S_IS_LOCAL (symp->sy_value.X_op_symbol))

	  && S_IS_DEFINED (symp->sy_value.X_add_symbol)
	  && S_IS_DEFINED (symp->sy_value.X_op_symbol)
	  && S_GET_SEGMENT (symp->sy_value.X_add_symbol) != expr_section
	  && S_GET_SEGMENT (symp->sy_value.X_op_symbol) != expr_section)
	return 0;
      break;
      
    default:
      break;
    }
  return 1;
}
#endif

static void
report_op_error (symbolS *symp, symbolS *left, symbolS *right)
{
  char *file;
  unsigned int line;
  segT seg_left = S_GET_SEGMENT (left);
  segT seg_right = right ? S_GET_SEGMENT (right) : 0;

  if (expr_symbol_where (symp, &file, &line))
    {
      if (seg_left == undefined_section)
	as_bad_where (file, line,
		      _("undefined symbol `%s' in operation"),
		      S_GET_NAME (left));
      if (seg_right == undefined_section)
	as_bad_where (file, line,
		      _("undefined symbol `%s' in operation"),
		      S_GET_NAME (right));
      if (seg_left != undefined_section
	  && seg_right != undefined_section)
	{
	  if (right)
	    as_bad_where (file, line,
			  _("invalid sections for operation on `%s' and `%s'"),
			  S_GET_NAME (left), S_GET_NAME (right));
	  else
	    as_bad_where (file, line,
			  _("invalid section for operation on `%s'"),
			  S_GET_NAME (left));
	}

    }
  else
    {
      if (seg_left == undefined_section)
	as_bad (_("undefined symbol `%s' in operation setting `%s'"),
		S_GET_NAME (left), S_GET_NAME (symp));
      if (seg_right == undefined_section)
	as_bad (_("undefined symbol `%s' in operation setting `%s'"),
		S_GET_NAME (right), S_GET_NAME (symp));
      if (seg_left != undefined_section
	  && seg_right != undefined_section)
	{
	  if (right)
	    as_bad (_("invalid sections for operation on `%s' and `%s' setting `%s'"),
		    S_GET_NAME (left), S_GET_NAME (right), S_GET_NAME (symp));
	  else
	    as_bad (_("invalid section for operation on `%s' setting `%s'"),
		    S_GET_NAME (left), S_GET_NAME (symp));
	}
    }
}

/* Resolve the value of a symbol.  This is called during the final
   pass over the symbol table to resolve any symbols with complex
   values.  */

valueT
resolve_symbol_value (symbolS *symp)
{
  int resolved;
  valueT final_val = 0;
  segT final_seg;

  if (LOCAL_SYMBOL_CHECK (symp))
    {
      struct local_symbol *locsym = (struct local_symbol *) symp;

      final_val = locsym->lsy_value;
      if (local_symbol_resolved_p (locsym))
	return final_val;

      final_val += local_symbol_get_frag (locsym)->fr_address / OCTETS_PER_BYTE;

      if (finalize_syms)
	{
	  locsym->lsy_value = final_val;
	  local_symbol_mark_resolved (locsym);
	}

      return final_val;
    }

  if (symp->sy_resolved)
    {
      if (symp->sy_value.X_op == O_constant)
	return (valueT) symp->sy_value.X_add_number;
      else
	return 0;
    }

  resolved = 0;
  final_seg = S_GET_SEGMENT (symp);

  if (symp->sy_resolving)
    {
      if (finalize_syms)
	as_bad (_("symbol definition loop encountered at `%s'"),
		S_GET_NAME (symp));
      final_val = 0;
      resolved = 1;
    }
#ifdef OBJ_COMPLEX_RELC
  else if (final_seg == expr_section
	   && use_complex_relocs_for (symp))
    {
      symbolS * relc_symbol = NULL;
      char * relc_symbol_name = NULL;

      relc_symbol_name = symbol_relc_make_expr (& symp->sy_value);

      /* For debugging, print out conversion input & output.  */
#ifdef DEBUG_SYMS
      print_expr (& symp->sy_value);
      if (relc_symbol_name)
	fprintf (stderr, "-> relc symbol: %s\n", relc_symbol_name);
#endif

      if (relc_symbol_name != NULL)
	relc_symbol = symbol_new (relc_symbol_name, undefined_section,
				  0, & zero_address_frag);

      if (relc_symbol == NULL)
	{
	  as_bad (_("cannot convert expression symbol %s to complex relocation"),
		  S_GET_NAME (symp));
	  resolved = 0;
	}
      else
	{
	  symbol_table_insert (relc_symbol);

 	  /* S_CLEAR_EXTERNAL (relc_symbol); */
	  if (symp->bsym->flags & BSF_SRELC)
	    relc_symbol->bsym->flags |= BSF_SRELC;
	  else
	    relc_symbol->bsym->flags |= BSF_RELC;	  
	  /* symp->bsym->flags |= BSF_RELC; */
	  copy_symbol_attributes (symp, relc_symbol);
	  symp->sy_value.X_op = O_symbol;
	  symp->sy_value.X_add_symbol = relc_symbol;
	  symp->sy_value.X_add_number = 0;
	  resolved = 1;
	}

      final_seg = undefined_section;
      goto exit_dont_set_value;
    }
#endif
  else
    {
      symbolS *add_symbol, *op_symbol;
      offsetT left, right;
      segT seg_left, seg_right;
      operatorT op;
      int move_seg_ok;

      symp->sy_resolving = 1;

      /* Help out with CSE.  */
      add_symbol = symp->sy_value.X_add_symbol;
      op_symbol = symp->sy_value.X_op_symbol;
      final_val = symp->sy_value.X_add_number;
      op = symp->sy_value.X_op;

      switch (op)
	{
	default:
	  BAD_CASE (op);
	  break;

	case O_absent:
	  final_val = 0;
	  /* Fall through.  */

	case O_constant:
	  final_val += symp->sy_frag->fr_address / OCTETS_PER_BYTE;
	  if (final_seg == expr_section)
	    final_seg = absolute_section;
	  /* Fall through.  */

	case O_register:
	  resolved = 1;
	  break;

	case O_symbol:
	case O_symbol_rva:
	  left = resolve_symbol_value (add_symbol);
	  seg_left = S_GET_SEGMENT (add_symbol);
	  if (finalize_syms)
	    symp->sy_value.X_op_symbol = NULL;

	do_symbol:
	  if (S_IS_WEAKREFR (symp))
	    {
	      assert (final_val == 0);
	      if (S_IS_WEAKREFR (add_symbol))
		{
		  assert (add_symbol->sy_value.X_op == O_symbol
			  && add_symbol->sy_value.X_add_number == 0);
		  add_symbol = add_symbol->sy_value.X_add_symbol;
		  assert (! S_IS_WEAKREFR (add_symbol));
		  symp->sy_value.X_add_symbol = add_symbol;
		}
	    }

	  if (symp->sy_mri_common)
	    {
	      /* This is a symbol inside an MRI common section.  The
		 relocation routines are going to handle it specially.
		 Don't change the value.  */
	      resolved = symbol_resolved_p (add_symbol);
	      break;
	    }

	  if (finalize_syms && final_val == 0)
	    {
	      if (LOCAL_SYMBOL_CHECK (add_symbol))
		add_symbol = local_symbol_convert ((struct local_symbol *)
						   add_symbol);
	      copy_symbol_attributes (symp, add_symbol);
	    }

	  /* If we have equated this symbol to an undefined or common
	     symbol, keep X_op set to O_symbol, and don't change
	     X_add_number.  This permits the routine which writes out
	     relocation to detect this case, and convert the
	     relocation to be against the symbol to which this symbol
	     is equated.  */
	  if (! S_IS_DEFINED (add_symbol)
#if defined (OBJ_COFF) && defined (TE_PE)
	      || S_IS_WEAK (add_symbol)
#endif
	      || S_IS_COMMON (add_symbol))
	    {
	      if (finalize_syms)
		{
		  symp->sy_value.X_op = O_symbol;
		  symp->sy_value.X_add_symbol = add_symbol;
		  symp->sy_value.X_add_number = final_val;
		  /* Use X_op_symbol as a flag.  */
		  symp->sy_value.X_op_symbol = add_symbol;
		  final_seg = seg_left;
		}
	      final_val = 0;
	      resolved = symbol_resolved_p (add_symbol);
	      symp->sy_resolving = 0;
	      goto exit_dont_set_value;
	    }
	  else if (finalize_syms
		   && ((final_seg == expr_section && seg_left != expr_section)
		       || symbol_shadow_p (symp)))
	    {
	      /* If the symbol is an expression symbol, do similarly
		 as for undefined and common syms above.  Handles
		 "sym +/- expr" where "expr" cannot be evaluated
		 immediately, and we want relocations to be against
		 "sym", eg. because it is weak.  */
	      symp->sy_value.X_op = O_symbol;
	      symp->sy_value.X_add_symbol = add_symbol;
	      symp->sy_value.X_add_number = final_val;
	      symp->sy_value.X_op_symbol = add_symbol;
	      final_seg = seg_left;
	      final_val += symp->sy_frag->fr_address + left;
	      resolved = symbol_resolved_p (add_symbol);
	      symp->sy_resolving = 0;
	      goto exit_dont_set_value;
	    }
	  else
	    {
	      final_val += symp->sy_frag->fr_address + left;
	      if (final_seg == expr_section || final_seg == undefined_section)
		final_seg = seg_left;
	    }

	  resolved = symbol_resolved_p (add_symbol);
	  if (S_IS_WEAKREFR (symp))
	    goto exit_dont_set_value;
	  break;

	case O_uminus:
	case O_bit_not:
	case O_logical_not:
	  left = resolve_symbol_value (add_symbol);
	  seg_left = S_GET_SEGMENT (add_symbol);

	  /* By reducing these to the relevant dyadic operator, we get
	     	!S -> S == 0 	permitted on anything,
		-S -> 0 - S 	only permitted on absolute
		~S -> S ^ ~0 	only permitted on absolute  */
	  if (op != O_logical_not && seg_left != absolute_section
	      && finalize_syms)
	    report_op_error (symp, add_symbol, NULL);

	  if (final_seg == expr_section || final_seg == undefined_section)
	    final_seg = absolute_section;

	  if (op == O_uminus)
	    left = -left;
	  else if (op == O_logical_not)
	    left = !left;
	  else
	    left = ~left;

	  final_val += left + symp->sy_frag->fr_address;

	  resolved = symbol_resolved_p (add_symbol);
	  break;

	case O_multiply:
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
	  left = resolve_symbol_value (add_symbol);
	  right = resolve_symbol_value (op_symbol);
	  seg_left = S_GET_SEGMENT (add_symbol);
	  seg_right = S_GET_SEGMENT (op_symbol);

	  /* Simplify addition or subtraction of a constant by folding the
	     constant into X_add_number.  */
	  if (op == O_add)
	    {
	      if (seg_right == absolute_section)
		{
		  final_val += right;
		  goto do_symbol;
		}
	      else if (seg_left == absolute_section)
		{
		  final_val += left;
		  add_symbol = op_symbol;
		  left = right;
		  seg_left = seg_right;
		  goto do_symbol;
		}
	    }
	  else if (op == O_subtract)
	    {
	      if (seg_right == absolute_section)
		{
		  final_val -= right;
		  goto do_symbol;
		}
	    }

	  move_seg_ok = 1;
	  /* Equality and non-equality tests are permitted on anything.
	     Subtraction, and other comparison operators are permitted if
	     both operands are in the same section.  Otherwise, both
	     operands must be absolute.  We already handled the case of
	     addition or subtraction of a constant above.  This will
	     probably need to be changed for an object file format which
	     supports arbitrary expressions, such as IEEE-695.  */
	  if (!(seg_left == absolute_section
		   && seg_right == absolute_section)
	      && !(op == O_eq || op == O_ne)
	      && !((op == O_subtract
		    || op == O_lt || op == O_le || op == O_ge || op == O_gt)
		   && seg_left == seg_right
		   && (seg_left != undefined_section
		       || add_symbol == op_symbol)))
	    {
	      /* Don't emit messages unless we're finalizing the symbol value,
		 otherwise we may get the same message multiple times.  */
	      if (finalize_syms)
		report_op_error (symp, add_symbol, op_symbol);
	      /* However do not move the symbol into the absolute section
		 if it cannot currently be resolved - this would confuse
		 other parts of the assembler into believing that the
		 expression had been evaluated to zero.  */
	      else
		move_seg_ok = 0;
	    }

	  if (move_seg_ok
	      && (final_seg == expr_section || final_seg == undefined_section))
	    final_seg = absolute_section;

	  /* Check for division by zero.  */
	  if ((op == O_divide || op == O_modulus) && right == 0)
	    {
	      /* If seg_right is not absolute_section, then we've
		 already issued a warning about using a bad symbol.  */
	      if (seg_right == absolute_section && finalize_syms)
		{
		  char *file;
		  unsigned int line;

		  if (expr_symbol_where (symp, &file, &line))
		    as_bad_where (file, line, _("division by zero"));
		  else
		    as_bad (_("division by zero when setting `%s'"),
			    S_GET_NAME (symp));
		}

	      right = 1;
	    }

	  switch (symp->sy_value.X_op)
	    {
	    case O_multiply:		left *= right; break;
	    case O_divide:		left /= right; break;
	    case O_modulus:		left %= right; break;
	    case O_left_shift:		left <<= right; break;
	    case O_right_shift:		left >>= right; break;
	    case O_bit_inclusive_or:	left |= right; break;
	    case O_bit_or_not:		left |= ~right; break;
	    case O_bit_exclusive_or:	left ^= right; break;
	    case O_bit_and:		left &= right; break;
	    case O_add:			left += right; break;
	    case O_subtract:		left -= right; break;
	    case O_eq:
	    case O_ne:
	      left = (left == right && seg_left == seg_right
		      && (seg_left != undefined_section
			  || add_symbol == op_symbol)
		      ? ~ (offsetT) 0 : 0);
	      if (symp->sy_value.X_op == O_ne)
		left = ~left;
	      break;
	    case O_lt:	left = left <  right ? ~ (offsetT) 0 : 0; break;
	    case O_le:	left = left <= right ? ~ (offsetT) 0 : 0; break;
	    case O_ge:	left = left >= right ? ~ (offsetT) 0 : 0; break;
	    case O_gt:	left = left >  right ? ~ (offsetT) 0 : 0; break;
	    case O_logical_and:	left = left && right; break;
	    case O_logical_or:	left = left || right; break;
	    default:		abort ();
	    }

	  final_val += symp->sy_frag->fr_address + left;
	  if (final_seg == expr_section || final_seg == undefined_section)
	    {
	      if (seg_left == undefined_section
		  || seg_right == undefined_section)
		final_seg = undefined_section;
	      else if (seg_left == absolute_section)
		final_seg = seg_right;
	      else
		final_seg = seg_left;
	    }
	  resolved = (symbol_resolved_p (add_symbol)
		      && symbol_resolved_p (op_symbol));
	  break;

	case O_big:
	case O_illegal:
	  /* Give an error (below) if not in expr_section.  We don't
	     want to worry about expr_section symbols, because they
	     are fictional (they are created as part of expression
	     resolution), and any problems may not actually mean
	     anything.  */
	  break;
	}

      symp->sy_resolving = 0;
    }

  if (finalize_syms)
    S_SET_VALUE (symp, final_val);

exit_dont_set_value:
  /* Always set the segment, even if not finalizing the value.
     The segment is used to determine whether a symbol is defined.  */
    S_SET_SEGMENT (symp, final_seg);

  /* Don't worry if we can't resolve an expr_section symbol.  */
  if (finalize_syms)
    {
      if (resolved)
	symp->sy_resolved = 1;
      else if (S_GET_SEGMENT (symp) != expr_section)
	{
	  as_bad (_("can't resolve value for symbol `%s'"),
		  S_GET_NAME (symp));
	  symp->sy_resolved = 1;
	}
    }

  return final_val;
}

static void resolve_local_symbol (const char *, PTR);

/* A static function passed to hash_traverse.  */

static void
resolve_local_symbol (const char *key ATTRIBUTE_UNUSED, PTR value)
{
  if (value != NULL)
    resolve_symbol_value (value);
}

/* Resolve all local symbols.  */

void
resolve_local_symbol_values (void)
{
  hash_traverse (local_hash, resolve_local_symbol);
}

/* Obtain the current value of a symbol without changing any
   sub-expressions used.  */

int
snapshot_symbol (symbolS **symbolPP, valueT *valueP, segT *segP, fragS **fragPP)
{
  symbolS *symbolP = *symbolPP;

  if (LOCAL_SYMBOL_CHECK (symbolP))
    {
      struct local_symbol *locsym = (struct local_symbol *) symbolP;

      *valueP = locsym->lsy_value;
      *segP = locsym->lsy_section;
      *fragPP = local_symbol_get_frag (locsym);
    }
  else
    {
      expressionS expr = symbolP->sy_value;

      if (!symbolP->sy_resolved && expr.X_op != O_illegal)
	{
	  int resolved;

	  if (symbolP->sy_resolving)
	    return 0;
	  symbolP->sy_resolving = 1;
	  resolved = resolve_expression (&expr);
	  symbolP->sy_resolving = 0;
	  if (!resolved)
	    return 0;

	  switch (expr.X_op)
	    {
	    case O_constant:
	    case O_register:
	      if (!symbol_equated_p (symbolP))
		break;
	      /* Fall thru.  */
	    case O_symbol:
	    case O_symbol_rva:
	      symbolP = expr.X_add_symbol;
	      break;
	    default:
	      return 0;
	    }
	}

      /* Never change a defined symbol.  */
      if (symbolP->bsym->section == undefined_section
	  || symbolP->bsym->section == expr_section)
	*symbolPP = symbolP;
      *valueP = expr.X_add_number;
      *segP = symbolP->bsym->section;
      *fragPP = symbolP->sy_frag;

      if (*segP == expr_section)
	switch (expr.X_op)
	  {
	  case O_constant: *segP = absolute_section; break;
	  case O_register: *segP = reg_section; break;
	  default: break;
	  }
    }

  return 1;
}

/* Dollar labels look like a number followed by a dollar sign.  Eg, "42$".
   They are *really* local.  That is, they go out of scope whenever we see a
   label that isn't local.  Also, like fb labels, there can be multiple
   instances of a dollar label.  Therefor, we name encode each instance with
   the instance number, keep a list of defined symbols separate from the real
   symbol table, and we treat these buggers as a sparse array.  */

static long *dollar_labels;
static long *dollar_label_instances;
static char *dollar_label_defines;
static unsigned long dollar_label_count;
static unsigned long dollar_label_max;

int
dollar_label_defined (long label)
{
  long *i;

  know ((dollar_labels != NULL) || (dollar_label_count == 0));

  for (i = dollar_labels; i < dollar_labels + dollar_label_count; ++i)
    if (*i == label)
      return dollar_label_defines[i - dollar_labels];

  /* If we get here, label isn't defined.  */
  return 0;
}

static long
dollar_label_instance (long label)
{
  long *i;

  know ((dollar_labels != NULL) || (dollar_label_count == 0));

  for (i = dollar_labels; i < dollar_labels + dollar_label_count; ++i)
    if (*i == label)
      return (dollar_label_instances[i - dollar_labels]);

  /* If we get here, we haven't seen the label before.
     Therefore its instance count is zero.  */
  return 0;
}

void
dollar_label_clear (void)
{
  memset (dollar_label_defines, '\0', (unsigned int) dollar_label_count);
}

#define DOLLAR_LABEL_BUMP_BY 10

void
define_dollar_label (long label)
{
  long *i;

  for (i = dollar_labels; i < dollar_labels + dollar_label_count; ++i)
    if (*i == label)
      {
	++dollar_label_instances[i - dollar_labels];
	dollar_label_defines[i - dollar_labels] = 1;
	return;
      }

  /* If we get to here, we don't have label listed yet.  */

  if (dollar_labels == NULL)
    {
      dollar_labels = (long *) xmalloc (DOLLAR_LABEL_BUMP_BY * sizeof (long));
      dollar_label_instances = (long *) xmalloc (DOLLAR_LABEL_BUMP_BY * sizeof (long));
      dollar_label_defines = xmalloc (DOLLAR_LABEL_BUMP_BY);
      dollar_label_max = DOLLAR_LABEL_BUMP_BY;
      dollar_label_count = 0;
    }
  else if (dollar_label_count == dollar_label_max)
    {
      dollar_label_max += DOLLAR_LABEL_BUMP_BY;
      dollar_labels = (long *) xrealloc ((char *) dollar_labels,
					 dollar_label_max * sizeof (long));
      dollar_label_instances = (long *) xrealloc ((char *) dollar_label_instances,
					  dollar_label_max * sizeof (long));
      dollar_label_defines = xrealloc (dollar_label_defines, dollar_label_max);
    }				/* if we needed to grow  */

  dollar_labels[dollar_label_count] = label;
  dollar_label_instances[dollar_label_count] = 1;
  dollar_label_defines[dollar_label_count] = 1;
  ++dollar_label_count;
}

/* Caller must copy returned name: we re-use the area for the next name.

   The mth occurence of label n: is turned into the symbol "Ln^Am"
   where n is the label number and m is the instance number. "L" makes
   it a label discarded unless debugging and "^A"('\1') ensures no
   ordinary symbol SHOULD get the same name as a local label
   symbol. The first "4:" is "L4^A1" - the m numbers begin at 1.

   fb labels get the same treatment, except that ^B is used in place
   of ^A.  */

char *				/* Return local label name.  */
dollar_label_name (register long n,	/* we just saw "n$:" : n a number.  */
		   register int augend	/* 0 for current instance, 1 for new instance.  */)
{
  long i;
  /* Returned to caller, then copied.  Used for created names ("4f").  */
  static char symbol_name_build[24];
  register char *p;
  register char *q;
  char symbol_name_temporary[20];	/* Build up a number, BACKWARDS.  */

  know (n >= 0);
  know (augend == 0 || augend == 1);
  p = symbol_name_build;
#ifdef LOCAL_LABEL_PREFIX
  *p++ = LOCAL_LABEL_PREFIX;
#endif
  *p++ = 'L';

  /* Next code just does sprintf( {}, "%d", n);  */
  /* Label number.  */
  q = symbol_name_temporary;
  for (*q++ = 0, i = n; i; ++q)
    {
      *q = i % 10 + '0';
      i /= 10;
    }
  while ((*p = *--q) != '\0')
    ++p;

  *p++ = DOLLAR_LABEL_CHAR;		/* ^A  */

  /* Instance number.  */
  q = symbol_name_temporary;
  for (*q++ = 0, i = dollar_label_instance (n) + augend; i; ++q)
    {
      *q = i % 10 + '0';
      i /= 10;
    }
  while ((*p++ = *--q) != '\0')
	;;

  /* The label, as a '\0' ended string, starts at symbol_name_build.  */
  return symbol_name_build;
}

/* Somebody else's idea of local labels. They are made by "n:" where n
   is any decimal digit. Refer to them with
    "nb" for previous (backward) n:
   or "nf" for next (forward) n:.

   We do a little better and let n be any number, not just a single digit, but
   since the other guy's assembler only does ten, we treat the first ten
   specially.

   Like someone else's assembler, we have one set of local label counters for
   entire assembly, not one set per (sub)segment like in most assemblers. This
   implies that one can refer to a label in another segment, and indeed some
   crufty compilers have done just that.

   Since there could be a LOT of these things, treat them as a sparse
   array.  */

#define FB_LABEL_SPECIAL (10)

static long fb_low_counter[FB_LABEL_SPECIAL];
static long *fb_labels;
static long *fb_label_instances;
static long fb_label_count;
static long fb_label_max;

/* This must be more than FB_LABEL_SPECIAL.  */
#define FB_LABEL_BUMP_BY (FB_LABEL_SPECIAL + 6)

static void
fb_label_init (void)
{
  memset ((void *) fb_low_counter, '\0', sizeof (fb_low_counter));
}

/* Add one to the instance number of this fb label.  */

void
fb_label_instance_inc (long label)
{
  long *i;

  if (label < FB_LABEL_SPECIAL)
    {
      ++fb_low_counter[label];
      return;
    }

  if (fb_labels != NULL)
    {
      for (i = fb_labels + FB_LABEL_SPECIAL;
	   i < fb_labels + fb_label_count; ++i)
	{
	  if (*i == label)
	    {
	      ++fb_label_instances[i - fb_labels];
	      return;
	    }			/* if we find it  */
	}			/* for each existing label  */
    }

  /* If we get to here, we don't have label listed yet.  */

  if (fb_labels == NULL)
    {
      fb_labels = (long *) xmalloc (FB_LABEL_BUMP_BY * sizeof (long));
      fb_label_instances = (long *) xmalloc (FB_LABEL_BUMP_BY * sizeof (long));
      fb_label_max = FB_LABEL_BUMP_BY;
      fb_label_count = FB_LABEL_SPECIAL;

    }
  else if (fb_label_count == fb_label_max)
    {
      fb_label_max += FB_LABEL_BUMP_BY;
      fb_labels = (long *) xrealloc ((char *) fb_labels,
				     fb_label_max * sizeof (long));
      fb_label_instances = (long *) xrealloc ((char *) fb_label_instances,
					      fb_label_max * sizeof (long));
    }				/* if we needed to grow  */

  fb_labels[fb_label_count] = label;
  fb_label_instances[fb_label_count] = 1;
  ++fb_label_count;
}

static long
fb_label_instance (long label)
{
  long *i;

  if (label < FB_LABEL_SPECIAL)
    {
      return (fb_low_counter[label]);
    }

  if (fb_labels != NULL)
    {
      for (i = fb_labels + FB_LABEL_SPECIAL;
	   i < fb_labels + fb_label_count; ++i)
	{
	  if (*i == label)
	    {
	      return (fb_label_instances[i - fb_labels]);
	    }			/* if we find it  */
	}			/* for each existing label  */
    }

  /* We didn't find the label, so this must be a reference to the
     first instance.  */
  return 0;
}

/* Caller must copy returned name: we re-use the area for the next name.

   The mth occurence of label n: is turned into the symbol "Ln^Bm"
   where n is the label number and m is the instance number. "L" makes
   it a label discarded unless debugging and "^B"('\2') ensures no
   ordinary symbol SHOULD get the same name as a local label
   symbol. The first "4:" is "L4^B1" - the m numbers begin at 1.

   dollar labels get the same treatment, except that ^A is used in
   place of ^B.  */

char *				/* Return local label name.  */
fb_label_name (long n,	/* We just saw "n:", "nf" or "nb" : n a number.  */
	       long augend	/* 0 for nb, 1 for n:, nf.  */)
{
  long i;
  /* Returned to caller, then copied.  Used for created names ("4f").  */
  static char symbol_name_build[24];
  register char *p;
  register char *q;
  char symbol_name_temporary[20];	/* Build up a number, BACKWARDS.  */

  know (n >= 0);
#ifdef TC_MMIX
  know ((unsigned long) augend <= 2 /* See mmix_fb_label.  */);
#else
  know ((unsigned long) augend <= 1);
#endif
  p = symbol_name_build;
#ifdef LOCAL_LABEL_PREFIX
  *p++ = LOCAL_LABEL_PREFIX;
#endif
  *p++ = 'L';

  /* Next code just does sprintf( {}, "%d", n);  */
  /* Label number.  */
  q = symbol_name_temporary;
  for (*q++ = 0, i = n; i; ++q)
    {
      *q = i % 10 + '0';
      i /= 10;
    }
  while ((*p = *--q) != '\0')
    ++p;

  *p++ = LOCAL_LABEL_CHAR;		/* ^B  */

  /* Instance number.  */
  q = symbol_name_temporary;
  for (*q++ = 0, i = fb_label_instance (n) + augend; i; ++q)
    {
      *q = i % 10 + '0';
      i /= 10;
    }
  while ((*p++ = *--q) != '\0')
	;;

  /* The label, as a '\0' ended string, starts at symbol_name_build.  */
  return (symbol_name_build);
}

/* Decode name that may have been generated by foo_label_name() above.
   If the name wasn't generated by foo_label_name(), then return it
   unaltered.  This is used for error messages.  */

char *
decode_local_label_name (char *s)
{
  char *p;
  char *symbol_decode;
  int label_number;
  int instance_number;
  char *type;
  const char *message_format;
  int index = 0;

#ifdef LOCAL_LABEL_PREFIX
  if (s[index] == LOCAL_LABEL_PREFIX)
    ++index;
#endif

  if (s[index] != 'L')
    return s;

  for (label_number = 0, p = s + index + 1; ISDIGIT (*p); ++p)
    label_number = (10 * label_number) + *p - '0';

  if (*p == DOLLAR_LABEL_CHAR)
    type = "dollar";
  else if (*p == LOCAL_LABEL_CHAR)
    type = "fb";
  else
    return s;

  for (instance_number = 0, p++; ISDIGIT (*p); ++p)
    instance_number = (10 * instance_number) + *p - '0';

  message_format = _("\"%d\" (instance number %d of a %s label)");
  symbol_decode = obstack_alloc (&notes, strlen (message_format) + 30);
  sprintf (symbol_decode, message_format, label_number, instance_number, type);

  return symbol_decode;
}

/* Get the value of a symbol.  */

valueT
S_GET_VALUE (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    return resolve_symbol_value (s);

  if (!s->sy_resolved)
    {
      valueT val = resolve_symbol_value (s);
      if (!finalize_syms)
	return val;
    }
  if (S_IS_WEAKREFR (s))
    return S_GET_VALUE (s->sy_value.X_add_symbol);

  if (s->sy_value.X_op != O_constant)
    {
      if (! s->sy_resolved
	  || s->sy_value.X_op != O_symbol
	  || (S_IS_DEFINED (s) && ! S_IS_COMMON (s)))
	as_bad (_("attempt to get value of unresolved symbol `%s'"),
		S_GET_NAME (s));
    }
  return (valueT) s->sy_value.X_add_number;
}

/* Set the value of a symbol.  */

void
S_SET_VALUE (symbolS *s, valueT val)
{
  if (LOCAL_SYMBOL_CHECK (s))
    {
      ((struct local_symbol *) s)->lsy_value = val;
      return;
    }

  s->sy_value.X_op = O_constant;
  s->sy_value.X_add_number = (offsetT) val;
  s->sy_value.X_unsigned = 0;
  S_CLEAR_WEAKREFR (s);
}

void
copy_symbol_attributes (symbolS *dest, symbolS *src)
{
  if (LOCAL_SYMBOL_CHECK (dest))
    dest = local_symbol_convert ((struct local_symbol *) dest);
  if (LOCAL_SYMBOL_CHECK (src))
    src = local_symbol_convert ((struct local_symbol *) src);

  /* In an expression, transfer the settings of these flags.
     The user can override later, of course.  */
#define COPIED_SYMFLAGS	(BSF_FUNCTION | BSF_OBJECT)
  dest->bsym->flags |= src->bsym->flags & COPIED_SYMFLAGS;

#ifdef OBJ_COPY_SYMBOL_ATTRIBUTES
  OBJ_COPY_SYMBOL_ATTRIBUTES (dest, src);
#endif

#ifdef TC_COPY_SYMBOL_ATTRIBUTES
  TC_COPY_SYMBOL_ATTRIBUTES (dest, src);
#endif
}

int
S_IS_FUNCTION (symbolS *s)
{
  flagword flags;

  if (LOCAL_SYMBOL_CHECK (s))
    return 0;

  flags = s->bsym->flags;

  return (flags & BSF_FUNCTION) != 0;
}

int
S_IS_EXTERNAL (symbolS *s)
{
  flagword flags;

  if (LOCAL_SYMBOL_CHECK (s))
    return 0;

  flags = s->bsym->flags;

  /* Sanity check.  */
  if ((flags & BSF_LOCAL) && (flags & BSF_GLOBAL))
    abort ();

  return (flags & BSF_GLOBAL) != 0;
}

int
S_IS_WEAK (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    return 0;
  /* Conceptually, a weakrefr is weak if the referenced symbol is.  We
     could probably handle a WEAKREFR as always weak though.  E.g., if
     the referenced symbol has lost its weak status, there's no reason
     to keep handling the weakrefr as if it was weak.  */
  if (S_IS_WEAKREFR (s))
    return S_IS_WEAK (s->sy_value.X_add_symbol);
  return (s->bsym->flags & BSF_WEAK) != 0;
}

int
S_IS_WEAKREFR (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    return 0;
  return s->sy_weakrefr != 0;
}

int
S_IS_WEAKREFD (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    return 0;
  return s->sy_weakrefd != 0;
}

int
S_IS_COMMON (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    return 0;
  return bfd_is_com_section (s->bsym->section);
}

int
S_IS_DEFINED (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    return ((struct local_symbol *) s)->lsy_section != undefined_section;
  return s->bsym->section != undefined_section;
}


#ifndef EXTERN_FORCE_RELOC
#define EXTERN_FORCE_RELOC IS_ELF
#endif

/* Return true for symbols that should not be reduced to section
   symbols or eliminated from expressions, because they may be
   overridden by the linker.  */
int
S_FORCE_RELOC (symbolS *s, int strict)
{
  if (LOCAL_SYMBOL_CHECK (s))
    return ((struct local_symbol *) s)->lsy_section == undefined_section;

  return ((strict
	   && ((s->bsym->flags & BSF_WEAK) != 0
	       || (EXTERN_FORCE_RELOC
		   && (s->bsym->flags & BSF_GLOBAL) != 0)))
	  || s->bsym->section == undefined_section
	  || bfd_is_com_section (s->bsym->section));
}

int
S_IS_DEBUG (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    return 0;
  if (s->bsym->flags & BSF_DEBUGGING)
    return 1;
  return 0;
}

int
S_IS_LOCAL (symbolS *s)
{
  flagword flags;
  const char *name;

  if (LOCAL_SYMBOL_CHECK (s))
    return 1;

  flags = s->bsym->flags;

  /* Sanity check.  */
  if ((flags & BSF_LOCAL) && (flags & BSF_GLOBAL))
    abort ();

  if (bfd_get_section (s->bsym) == reg_section)
    return 1;

  if (flag_strip_local_absolute
      /* Keep BSF_FILE symbols in order to allow debuggers to identify
	 the source file even when the object file is stripped.  */
      && (flags & (BSF_GLOBAL | BSF_FILE)) == 0
      && bfd_get_section (s->bsym) == absolute_section)
    return 1;

  name = S_GET_NAME (s);
  return (name != NULL
	  && ! S_IS_DEBUG (s)
	  && (strchr (name, DOLLAR_LABEL_CHAR)
	      || strchr (name, LOCAL_LABEL_CHAR)
	      || (! flag_keep_locals
		  && (bfd_is_local_label (stdoutput, s->bsym)
		      || (flag_mri
			  && name[0] == '?'
			  && name[1] == '?')))));
}

int
S_IS_STABD (symbolS *s)
{
  return S_GET_NAME (s) == 0;
}

int
S_IS_VOLATILE (const symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    return 0;
  return s->sy_volatile;
}

int
S_IS_FORWARD_REF (const symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    return 0;
  return s->sy_forward_ref;
}

const char *
S_GET_NAME (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    return ((struct local_symbol *) s)->lsy_name;
  return s->bsym->name;
}

segT
S_GET_SEGMENT (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    return ((struct local_symbol *) s)->lsy_section;
  return s->bsym->section;
}

void
S_SET_SEGMENT (symbolS *s, segT seg)
{
  /* Don't reassign section symbols.  The direct reason is to prevent seg
     faults assigning back to const global symbols such as *ABS*, but it
     shouldn't happen anyway.  */

  if (LOCAL_SYMBOL_CHECK (s))
    {
      if (seg == reg_section)
	s = local_symbol_convert ((struct local_symbol *) s);
      else
	{
	  ((struct local_symbol *) s)->lsy_section = seg;
	  return;
	}
    }

  if (s->bsym->flags & BSF_SECTION_SYM)
    {
      if (s->bsym->section != seg)
	abort ();
    }
  else
    s->bsym->section = seg;
}

void
S_SET_EXTERNAL (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    s = local_symbol_convert ((struct local_symbol *) s);
  if ((s->bsym->flags & BSF_WEAK) != 0)
    {
      /* Let .weak override .global.  */
      return;
    }
  if (s->bsym->flags & BSF_SECTION_SYM)
    {
      char * file;
      unsigned int line;

      /* Do not reassign section symbols.  */
      as_where (& file, & line);
      as_warn_where (file, line,
		     _("section symbols are already global"));
      return;
    }
  s->bsym->flags |= BSF_GLOBAL;
  s->bsym->flags &= ~(BSF_LOCAL | BSF_WEAK);

#ifdef USE_UNIQUE
  if (! an_external_name && S_GET_NAME(s)[0] != '.')
    an_external_name = S_GET_NAME (s);
#endif
}

void
S_CLEAR_EXTERNAL (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    return;
  if ((s->bsym->flags & BSF_WEAK) != 0)
    {
      /* Let .weak override.  */
      return;
    }
  s->bsym->flags |= BSF_LOCAL;
  s->bsym->flags &= ~(BSF_GLOBAL | BSF_WEAK);
}

void
S_SET_WEAK (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    s = local_symbol_convert ((struct local_symbol *) s);
#ifdef obj_set_weak_hook
  obj_set_weak_hook (s);
#endif
  s->bsym->flags |= BSF_WEAK;
  s->bsym->flags &= ~(BSF_GLOBAL | BSF_LOCAL);
}

void
S_SET_WEAKREFR (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    s = local_symbol_convert ((struct local_symbol *) s);
  s->sy_weakrefr = 1;
  /* If the alias was already used, make sure we mark the target as
     used as well, otherwise it might be dropped from the symbol
     table.  This may have unintended side effects if the alias is
     later redirected to another symbol, such as keeping the unused
     previous target in the symbol table.  Since it will be weak, it's
     not a big deal.  */
  if (s->sy_used)
    symbol_mark_used (s->sy_value.X_add_symbol);
}

void
S_CLEAR_WEAKREFR (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    return;
  s->sy_weakrefr = 0;
}

void
S_SET_WEAKREFD (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    s = local_symbol_convert ((struct local_symbol *) s);
  s->sy_weakrefd = 1;
  S_SET_WEAK (s);
}

void
S_CLEAR_WEAKREFD (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    return;
  if (s->sy_weakrefd)
    {
      s->sy_weakrefd = 0;
      /* If a weakref target symbol is weak, then it was never
	 referenced directly before, not even in a .global directive,
	 so decay it to local.  If it remains undefined, it will be
	 later turned into a global, like any other undefined
	 symbol.  */
      if (s->bsym->flags & BSF_WEAK)
	{
#ifdef obj_clear_weak_hook
	  obj_clear_weak_hook (s);
#endif
	  s->bsym->flags &= ~BSF_WEAK;
	  s->bsym->flags |= BSF_LOCAL;
	}
    }
}

void
S_SET_THREAD_LOCAL (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    s = local_symbol_convert ((struct local_symbol *) s);
  if (bfd_is_com_section (s->bsym->section)
      && (s->bsym->flags & BSF_THREAD_LOCAL) != 0)
    return;
  s->bsym->flags |= BSF_THREAD_LOCAL;
  if ((s->bsym->flags & BSF_FUNCTION) != 0)
    as_bad (_("Accessing function `%s' as thread-local object"),
	    S_GET_NAME (s));
  else if (! bfd_is_und_section (s->bsym->section)
	   && (s->bsym->section->flags & SEC_THREAD_LOCAL) == 0)
    as_bad (_("Accessing `%s' as thread-local object"),
	    S_GET_NAME (s));
}

void
S_SET_NAME (symbolS *s, const char *name)
{
  if (LOCAL_SYMBOL_CHECK (s))
    {
      ((struct local_symbol *) s)->lsy_name = name;
      return;
    }
  s->bsym->name = name;
}

void
S_SET_VOLATILE (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    s = local_symbol_convert ((struct local_symbol *) s);
  s->sy_volatile = 1;
}

void
S_CLEAR_VOLATILE (symbolS *s)
{
  if (!LOCAL_SYMBOL_CHECK (s))
    s->sy_volatile = 0;
}

void
S_SET_FORWARD_REF (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    s = local_symbol_convert ((struct local_symbol *) s);
  s->sy_forward_ref = 1;
}

/* Return the previous symbol in a chain.  */

symbolS *
symbol_previous (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    abort ();
  return s->sy_previous;
}

/* Return the next symbol in a chain.  */

symbolS *
symbol_next (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    abort ();
  return s->sy_next;
}

/* Return a pointer to the value of a symbol as an expression.  */

expressionS *
symbol_get_value_expression (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    s = local_symbol_convert ((struct local_symbol *) s);
  return &s->sy_value;
}

/* Set the value of a symbol to an expression.  */

void
symbol_set_value_expression (symbolS *s, const expressionS *exp)
{
  if (LOCAL_SYMBOL_CHECK (s))
    s = local_symbol_convert ((struct local_symbol *) s);
  s->sy_value = *exp;
  S_CLEAR_WEAKREFR (s);
}

/* Return a pointer to the X_add_number component of a symbol.  */

offsetT *
symbol_X_add_number (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    return (offsetT *) &((struct local_symbol *) s)->lsy_value;

  return &s->sy_value.X_add_number;
}

/* Set the value of SYM to the current position in the current segment.  */

void
symbol_set_value_now (symbolS *sym)
{
  S_SET_SEGMENT (sym, now_seg);
  S_SET_VALUE (sym, frag_now_fix ());
  symbol_set_frag (sym, frag_now);
}

/* Set the frag of a symbol.  */

void
symbol_set_frag (symbolS *s, fragS *f)
{
  if (LOCAL_SYMBOL_CHECK (s))
    {
      local_symbol_set_frag ((struct local_symbol *) s, f);
      return;
    }
  s->sy_frag = f;
  S_CLEAR_WEAKREFR (s);
}

/* Return the frag of a symbol.  */

fragS *
symbol_get_frag (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    return local_symbol_get_frag ((struct local_symbol *) s);
  return s->sy_frag;
}

/* Mark a symbol as having been used.  */

void
symbol_mark_used (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    return;
  s->sy_used = 1;
  if (S_IS_WEAKREFR (s))
    symbol_mark_used (s->sy_value.X_add_symbol);
}

/* Clear the mark of whether a symbol has been used.  */

void
symbol_clear_used (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    s = local_symbol_convert ((struct local_symbol *) s);
  s->sy_used = 0;
}

/* Return whether a symbol has been used.  */

int
symbol_used_p (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    return 1;
  return s->sy_used;
}

/* Mark a symbol as having been used in a reloc.  */

void
symbol_mark_used_in_reloc (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    s = local_symbol_convert ((struct local_symbol *) s);
  s->sy_used_in_reloc = 1;
}

/* Clear the mark of whether a symbol has been used in a reloc.  */

void
symbol_clear_used_in_reloc (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    return;
  s->sy_used_in_reloc = 0;
}

/* Return whether a symbol has been used in a reloc.  */

int
symbol_used_in_reloc_p (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    return 0;
  return s->sy_used_in_reloc;
}

/* Mark a symbol as an MRI common symbol.  */

void
symbol_mark_mri_common (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    s = local_symbol_convert ((struct local_symbol *) s);
  s->sy_mri_common = 1;
}

/* Clear the mark of whether a symbol is an MRI common symbol.  */

void
symbol_clear_mri_common (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    return;
  s->sy_mri_common = 0;
}

/* Return whether a symbol is an MRI common symbol.  */

int
symbol_mri_common_p (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    return 0;
  return s->sy_mri_common;
}

/* Mark a symbol as having been written.  */

void
symbol_mark_written (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    return;
  s->written = 1;
}

/* Clear the mark of whether a symbol has been written.  */

void
symbol_clear_written (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    return;
  s->written = 0;
}

/* Return whether a symbol has been written.  */

int
symbol_written_p (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    return 0;
  return s->written;
}

/* Mark a symbol has having been resolved.  */

void
symbol_mark_resolved (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    {
      local_symbol_mark_resolved ((struct local_symbol *) s);
      return;
    }
  s->sy_resolved = 1;
}

/* Return whether a symbol has been resolved.  */

int
symbol_resolved_p (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    return local_symbol_resolved_p ((struct local_symbol *) s);
  return s->sy_resolved;
}

/* Return whether a symbol is a section symbol.  */

int
symbol_section_p (symbolS *s ATTRIBUTE_UNUSED)
{
  if (LOCAL_SYMBOL_CHECK (s))
    return 0;
  return (s->bsym->flags & BSF_SECTION_SYM) != 0;
}

/* Return whether a symbol is equated to another symbol.  */

int
symbol_equated_p (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    return 0;
  return s->sy_value.X_op == O_symbol;
}

/* Return whether a symbol is equated to another symbol, and should be
   treated specially when writing out relocs.  */

int
symbol_equated_reloc_p (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    return 0;
  /* X_op_symbol, normally not used for O_symbol, is set by
     resolve_symbol_value to flag expression syms that have been
     equated.  */
  return (s->sy_value.X_op == O_symbol
#if defined (OBJ_COFF) && defined (TE_PE)
	  && ! S_IS_WEAK (s)
#endif
	  && ((s->sy_resolved && s->sy_value.X_op_symbol != NULL)
	      || ! S_IS_DEFINED (s)
	      || S_IS_COMMON (s)));
}

/* Return whether a symbol has a constant value.  */

int
symbol_constant_p (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    return 1;
  return s->sy_value.X_op == O_constant;
}

/* Return whether a symbol was cloned and thus removed from the global
   symbol list.  */

int
symbol_shadow_p (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    return 0;
  return s->sy_next == s;
}

/* Return the BFD symbol for a symbol.  */

asymbol *
symbol_get_bfdsym (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    s = local_symbol_convert ((struct local_symbol *) s);
  return s->bsym;
}

/* Set the BFD symbol for a symbol.  */

void
symbol_set_bfdsym (symbolS *s, asymbol *bsym)
{
  if (LOCAL_SYMBOL_CHECK (s))
    s = local_symbol_convert ((struct local_symbol *) s);
  /* Usually, it is harmless to reset a symbol to a BFD section
     symbol. For example, obj_elf_change_section sets the BFD symbol
     of an old symbol with the newly created section symbol. But when
     we have multiple sections with the same name, the newly created
     section may have the same name as an old section. We check if the
     old symbol has been already marked as a section symbol before
     resetting it.  */
  if ((s->bsym->flags & BSF_SECTION_SYM) == 0)
    s->bsym = bsym;
  /* else XXX - What do we do now ?  */
}

#ifdef OBJ_SYMFIELD_TYPE

/* Get a pointer to the object format information for a symbol.  */

OBJ_SYMFIELD_TYPE *
symbol_get_obj (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    s = local_symbol_convert ((struct local_symbol *) s);
  return &s->sy_obj;
}

/* Set the object format information for a symbol.  */

void
symbol_set_obj (symbolS *s, OBJ_SYMFIELD_TYPE *o)
{
  if (LOCAL_SYMBOL_CHECK (s))
    s = local_symbol_convert ((struct local_symbol *) s);
  s->sy_obj = *o;
}

#endif /* OBJ_SYMFIELD_TYPE */

#ifdef TC_SYMFIELD_TYPE

/* Get a pointer to the processor information for a symbol.  */

TC_SYMFIELD_TYPE *
symbol_get_tc (symbolS *s)
{
  if (LOCAL_SYMBOL_CHECK (s))
    s = local_symbol_convert ((struct local_symbol *) s);
  return &s->sy_tc;
}

/* Set the processor information for a symbol.  */

void
symbol_set_tc (symbolS *s, TC_SYMFIELD_TYPE *o)
{
  if (LOCAL_SYMBOL_CHECK (s))
    s = local_symbol_convert ((struct local_symbol *) s);
  s->sy_tc = *o;
}

#endif /* TC_SYMFIELD_TYPE */

void
symbol_begin (void)
{
  symbol_lastP = NULL;
  symbol_rootP = NULL;		/* In case we have 0 symbols (!!)  */
  sy_hash = hash_new ();
  local_hash = hash_new ();

  memset ((char *) (&abs_symbol), '\0', sizeof (abs_symbol));
#if defined (EMIT_SECTION_SYMBOLS) || !defined (RELOC_REQUIRES_SYMBOL)
  abs_symbol.bsym = bfd_abs_section.symbol;
#endif
  abs_symbol.sy_value.X_op = O_constant;
  abs_symbol.sy_frag = &zero_address_frag;

  if (LOCAL_LABELS_FB)
    fb_label_init ();
}

int indent_level;

/* Maximum indent level.
   Available for modification inside a gdb session.  */
static int max_indent_level = 8;

void
print_symbol_value_1 (FILE *file, symbolS *sym)
{
  const char *name = S_GET_NAME (sym);
  if (!name || !name[0])
    name = "(unnamed)";
  fprintf (file, "sym %lx %s", (unsigned long) sym, name);

  if (LOCAL_SYMBOL_CHECK (sym))
    {
      struct local_symbol *locsym = (struct local_symbol *) sym;
      if (local_symbol_get_frag (locsym) != &zero_address_frag
	  && local_symbol_get_frag (locsym) != NULL)
	fprintf (file, " frag %lx", (long) local_symbol_get_frag (locsym));
      if (local_symbol_resolved_p (locsym))
	fprintf (file, " resolved");
      fprintf (file, " local");
    }
  else
    {
      if (sym->sy_frag != &zero_address_frag)
	fprintf (file, " frag %lx", (long) sym->sy_frag);
      if (sym->written)
	fprintf (file, " written");
      if (sym->sy_resolved)
	fprintf (file, " resolved");
      else if (sym->sy_resolving)
	fprintf (file, " resolving");
      if (sym->sy_used_in_reloc)
	fprintf (file, " used-in-reloc");
      if (sym->sy_used)
	fprintf (file, " used");
      if (S_IS_LOCAL (sym))
	fprintf (file, " local");
      if (S_IS_EXTERNAL (sym))
	fprintf (file, " extern");
      if (S_IS_WEAK (sym))
	fprintf (file, " weak");
      if (S_IS_DEBUG (sym))
	fprintf (file, " debug");
      if (S_IS_DEFINED (sym))
	fprintf (file, " defined");
    }
  if (S_IS_WEAKREFR (sym))
    fprintf (file, " weakrefr");
  if (S_IS_WEAKREFD (sym))
    fprintf (file, " weakrefd");
  fprintf (file, " %s", segment_name (S_GET_SEGMENT (sym)));
  if (symbol_resolved_p (sym))
    {
      segT s = S_GET_SEGMENT (sym);

      if (s != undefined_section
	  && s != expr_section)
	fprintf (file, " %lx", (long) S_GET_VALUE (sym));
    }
  else if (indent_level < max_indent_level
	   && S_GET_SEGMENT (sym) != undefined_section)
    {
      indent_level++;
      fprintf (file, "\n%*s<", indent_level * 4, "");
      if (LOCAL_SYMBOL_CHECK (sym))
	fprintf (file, "constant %lx",
		 (long) ((struct local_symbol *) sym)->lsy_value);
      else
	print_expr_1 (file, &sym->sy_value);
      fprintf (file, ">");
      indent_level--;
    }
  fflush (file);
}

void
print_symbol_value (symbolS *sym)
{
  indent_level = 0;
  print_symbol_value_1 (stderr, sym);
  fprintf (stderr, "\n");
}

static void
print_binary (FILE *file, const char *name, expressionS *exp)
{
  indent_level++;
  fprintf (file, "%s\n%*s<", name, indent_level * 4, "");
  print_symbol_value_1 (file, exp->X_add_symbol);
  fprintf (file, ">\n%*s<", indent_level * 4, "");
  print_symbol_value_1 (file, exp->X_op_symbol);
  fprintf (file, ">");
  indent_level--;
}

void
print_expr_1 (FILE *file, expressionS *exp)
{
  fprintf (file, "expr %lx ", (long) exp);
  switch (exp->X_op)
    {
    case O_illegal:
      fprintf (file, "illegal");
      break;
    case O_absent:
      fprintf (file, "absent");
      break;
    case O_constant:
      fprintf (file, "constant %lx", (long) exp->X_add_number);
      break;
    case O_symbol:
      indent_level++;
      fprintf (file, "symbol\n%*s<", indent_level * 4, "");
      print_symbol_value_1 (file, exp->X_add_symbol);
      fprintf (file, ">");
    maybe_print_addnum:
      if (exp->X_add_number)
	fprintf (file, "\n%*s%lx", indent_level * 4, "",
		 (long) exp->X_add_number);
      indent_level--;
      break;
    case O_register:
      fprintf (file, "register #%d", (int) exp->X_add_number);
      break;
    case O_big:
      fprintf (file, "big");
      break;
    case O_uminus:
      fprintf (file, "uminus -<");
      indent_level++;
      print_symbol_value_1 (file, exp->X_add_symbol);
      fprintf (file, ">");
      goto maybe_print_addnum;
    case O_bit_not:
      fprintf (file, "bit_not");
      break;
    case O_multiply:
      print_binary (file, "multiply", exp);
      break;
    case O_divide:
      print_binary (file, "divide", exp);
      break;
    case O_modulus:
      print_binary (file, "modulus", exp);
      break;
    case O_left_shift:
      print_binary (file, "lshift", exp);
      break;
    case O_right_shift:
      print_binary (file, "rshift", exp);
      break;
    case O_bit_inclusive_or:
      print_binary (file, "bit_ior", exp);
      break;
    case O_bit_exclusive_or:
      print_binary (file, "bit_xor", exp);
      break;
    case O_bit_and:
      print_binary (file, "bit_and", exp);
      break;
    case O_eq:
      print_binary (file, "eq", exp);
      break;
    case O_ne:
      print_binary (file, "ne", exp);
      break;
    case O_lt:
      print_binary (file, "lt", exp);
      break;
    case O_le:
      print_binary (file, "le", exp);
      break;
    case O_ge:
      print_binary (file, "ge", exp);
      break;
    case O_gt:
      print_binary (file, "gt", exp);
      break;
    case O_logical_and:
      print_binary (file, "logical_and", exp);
      break;
    case O_logical_or:
      print_binary (file, "logical_or", exp);
      break;
    case O_add:
      indent_level++;
      fprintf (file, "add\n%*s<", indent_level * 4, "");
      print_symbol_value_1 (file, exp->X_add_symbol);
      fprintf (file, ">\n%*s<", indent_level * 4, "");
      print_symbol_value_1 (file, exp->X_op_symbol);
      fprintf (file, ">");
      goto maybe_print_addnum;
    case O_subtract:
      indent_level++;
      fprintf (file, "subtract\n%*s<", indent_level * 4, "");
      print_symbol_value_1 (file, exp->X_add_symbol);
      fprintf (file, ">\n%*s<", indent_level * 4, "");
      print_symbol_value_1 (file, exp->X_op_symbol);
      fprintf (file, ">");
      goto maybe_print_addnum;
    default:
      fprintf (file, "{unknown opcode %d}", (int) exp->X_op);
      break;
    }
  fflush (stdout);
}

void
print_expr (expressionS *exp)
{
  print_expr_1 (stderr, exp);
  fprintf (stderr, "\n");
}

void
symbol_print_statistics (FILE *file)
{
  hash_print_statistics (file, "symbol table", sy_hash);
  hash_print_statistics (file, "mini local symbol table", local_hash);
  fprintf (file, "%lu mini local symbols created, %lu converted\n",
	   local_symbol_count, local_symbol_conversion_count);
}

#ifdef OBJ_COMPLEX_RELC

/* Convert given symbol to a new complex-relocation symbol name.  This
   may be a recursive function, since it might be called for non-leaf
   nodes (plain symbols) in the expression tree.  The caller owns the
   returning string, so should free it eventually.  Errors are
   indicated via as_bad and a NULL return value.  The given symbol
   is marked with sy_used_in_reloc.  */

char *
symbol_relc_make_sym (symbolS * sym)
{
  char * terminal = NULL;
  const char * sname;
  char typetag;
  int sname_len;

  assert (sym != NULL);

  /* Recurse to symbol_relc_make_expr if this symbol
     is defined as an expression or a plain value.  */
  if (   S_GET_SEGMENT (sym) == expr_section
      || S_GET_SEGMENT (sym) == absolute_section)
    return symbol_relc_make_expr (& sym->sy_value);

  /* This may be a "fake symbol" L0\001, referring to ".".
     Write out a special null symbol to refer to this position.  */
  if (! strcmp (S_GET_NAME (sym), FAKE_LABEL_NAME))
    return xstrdup (".");

  /* We hope this is a plain leaf symbol.  Construct the encoding
     as {S,s}II...:CCCCCCC....
     where 'S'/'s' means section symbol / plain symbol
     III is decimal for the symbol name length
     CCC is the symbol name itself.  */
  symbol_mark_used_in_reloc (sym);

  sname = S_GET_NAME (sym);
  sname_len = strlen (sname);
  typetag = symbol_section_p (sym) ? 'S' : 's';

  terminal = xmalloc (1 /* S or s */
		      + 8 /* sname_len in decimal */
		      + 1 /* _ spacer */
		      + sname_len /* name itself */
		      + 1 /* \0 */ );

  sprintf (terminal, "%c%d:%s", typetag, sname_len, sname);
  return terminal;
}

/* Convert given value to a new complex-relocation symbol name.  This
   is a non-recursive function, since it is be called for leaf nodes
   (plain values) in the expression tree.  The caller owns the
   returning string, so should free() it eventually.  No errors.  */

char *
symbol_relc_make_value (offsetT val)
{
  char * terminal = xmalloc (28);  /* Enough for long long.  */

  terminal[0] = '#';
  sprintf_vma (& terminal[1], val);
  return terminal;
}

/* Convert given expression to a new complex-relocation symbol name.
   This is a recursive function, since it traverses the entire given
   expression tree.  The caller owns the returning string, so should
   free() it eventually.  Errors are indicated via as_bad() and a NULL
   return value.  */

char *
symbol_relc_make_expr (expressionS * exp)
{
  char * opstr = NULL; /* Operator prefix string.  */
  int    arity = 0;    /* Arity of this operator.  */
  char * operands[3];  /* Up to three operands.  */
  char * concat_string = NULL;

  operands[0] = operands[1] = operands[2] = NULL;

  assert (exp != NULL);

  /* Match known operators -> fill in opstr, arity, operands[] and fall
     through to construct subexpression fragments; may instead return 
     string directly for leaf nodes.  */

  /* See expr.h for the meaning of all these enums.  Many operators 
     have an unnatural arity (X_add_number implicitly added).  The
     conversion logic expands them to explicit "+" subexpressions.   */

  switch (exp->X_op)
    {
    default:
      as_bad ("Unknown expression operator (enum %d)", exp->X_op);
      break;

      /* Leaf nodes.  */
    case O_constant:
      return symbol_relc_make_value (exp->X_add_number);

    case O_symbol:
      if (exp->X_add_number) 
	{ 
	  arity = 2; 
	  opstr = "+"; 
	  operands[0] = symbol_relc_make_sym (exp->X_add_symbol);
	  operands[1] = symbol_relc_make_value (exp->X_add_number);
	  break;
	}
      else
	return symbol_relc_make_sym (exp->X_add_symbol);

      /* Helper macros for nesting nodes.  */

#define HANDLE_XADD_OPT1(str_) 						\
      if (exp->X_add_number)						\
        {								\
          arity = 2;							\
          opstr = "+:" str_;						\
          operands[0] = symbol_relc_make_sym (exp->X_add_symbol);	\
          operands[1] = symbol_relc_make_value (exp->X_add_number);	\
          break;							\
        }								\
      else								\
        {								\
          arity = 1;							\
          opstr = str_;							\
          operands[0] = symbol_relc_make_sym (exp->X_add_symbol);	\
        }								\
      break
      
#define HANDLE_XADD_OPT2(str_) 						\
      if (exp->X_add_number)						\
        {								\
          arity = 3;							\
          opstr = "+:" str_;						\
          operands[0] = symbol_relc_make_sym (exp->X_add_symbol);	\
          operands[1] = symbol_relc_make_sym (exp->X_op_symbol);	\
          operands[2] = symbol_relc_make_value (exp->X_add_number);	\
        }								\
      else								\
        {								\
          arity = 2;							\
          opstr = str_;							\
          operands[0] = symbol_relc_make_sym (exp->X_add_symbol);	\
          operands[1] = symbol_relc_make_sym (exp->X_op_symbol);	\
        } 								\
      break

      /* Nesting nodes.  */

    case O_uminus:       	HANDLE_XADD_OPT1 ("0-");
    case O_bit_not:      	HANDLE_XADD_OPT1 ("~");
    case O_logical_not:  	HANDLE_XADD_OPT1 ("!");
    case O_multiply:     	HANDLE_XADD_OPT2 ("*");
    case O_divide:       	HANDLE_XADD_OPT2 ("/");
    case O_modulus:      	HANDLE_XADD_OPT2 ("%");
    case O_left_shift:   	HANDLE_XADD_OPT2 ("<<");
    case O_right_shift:  	HANDLE_XADD_OPT2 (">>");
    case O_bit_inclusive_or:	HANDLE_XADD_OPT2 ("|");
    case O_bit_exclusive_or:	HANDLE_XADD_OPT2 ("^");
    case O_bit_and:      	HANDLE_XADD_OPT2 ("&");
    case O_add:          	HANDLE_XADD_OPT2 ("+");
    case O_subtract:     	HANDLE_XADD_OPT2 ("-");
    case O_eq:           	HANDLE_XADD_OPT2 ("==");
    case O_ne:           	HANDLE_XADD_OPT2 ("!=");
    case O_lt:           	HANDLE_XADD_OPT2 ("<");
    case O_le:           	HANDLE_XADD_OPT2 ("<=");
    case O_ge:           	HANDLE_XADD_OPT2 (">=");
    case O_gt:           	HANDLE_XADD_OPT2 (">");
    case O_logical_and:  	HANDLE_XADD_OPT2 ("&&");
    case O_logical_or:   	HANDLE_XADD_OPT2 ("||");
    }

  /* Validate & reject early.  */
  if (arity >= 1 && ((operands[0] == NULL) || (strlen (operands[0]) == 0)))
    opstr = NULL;
  if (arity >= 2 && ((operands[1] == NULL) || (strlen (operands[1]) == 0)))
    opstr = NULL;
  if (arity >= 3 && ((operands[2] == NULL) || (strlen (operands[2]) == 0)))
    opstr = NULL;

  if (opstr == NULL)
    concat_string = NULL;
  else
    {
      /* Allocate new string; include inter-operand padding gaps etc.  */
      concat_string = xmalloc (strlen (opstr) 
			       + 1
			       + (arity >= 1 ? (strlen (operands[0]) + 1 ) : 0)
			       + (arity >= 2 ? (strlen (operands[1]) + 1 ) : 0)
			       + (arity >= 3 ? (strlen (operands[2]) + 0 ) : 0)
			       + 1);
      assert (concat_string != NULL);
      
      /* Format the thing.  */
      sprintf (concat_string, 
	       (arity == 0 ? "%s" :
		arity == 1 ? "%s:%s" :
		arity == 2 ? "%s:%s:%s" :
		/* arity == 3 */ "%s:%s:%s:%s"),
	       opstr, operands[0], operands[1], operands[2]);
    }

  /* Free operand strings (not opstr).  */
  if (arity >= 1) xfree (operands[0]);
  if (arity >= 2) xfree (operands[1]);
  if (arity >= 3) xfree (operands[2]);

  return concat_string;
}

#endif
