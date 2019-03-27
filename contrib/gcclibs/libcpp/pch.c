/* Part of CPP library.  (Precompiled header reading/writing.)
   Copyright (C) 2000, 2001, 2002, 2003, 2004, 2005
   Free Software Foundation, Inc.

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2, or (at your option) any
later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  */

#include "config.h"
#include "system.h"
#include "cpplib.h"
#include "internal.h"
#include "hashtab.h"
#include "mkdeps.h"

static int write_macdef (cpp_reader *, cpp_hashnode *, void *);
static int save_idents (cpp_reader *, cpp_hashnode *, void *);
static hashval_t hashmem (const void *, size_t);
static hashval_t cpp_string_hash (const void *);
static int cpp_string_eq (const void *, const void *);
static int count_defs (cpp_reader *, cpp_hashnode *, void *);
static int comp_hashnodes (const void *, const void *);
static int collect_ht_nodes (cpp_reader *, cpp_hashnode *, void *);
static int write_defs (cpp_reader *, cpp_hashnode *, void *);
static int save_macros (cpp_reader *, cpp_hashnode *, void *);

/* This structure represents a macro definition on disk.  */
struct macrodef_struct
{
  unsigned int definition_length;
  unsigned short name_length;
  unsigned short flags;
};

/* This is how we write out a macro definition.
   Suitable for being called by cpp_forall_identifiers.  */

static int
write_macdef (cpp_reader *pfile, cpp_hashnode *hn, void *file_p)
{
  FILE *f = (FILE *) file_p;
  switch (hn->type)
    {
    case NT_VOID:
      if (! (hn->flags & NODE_POISONED))
	return 1;

    case NT_MACRO:
      if ((hn->flags & NODE_BUILTIN))
	return 1;

      {
	struct macrodef_struct s;
	const unsigned char *defn;

	s.name_length = NODE_LEN (hn);
	s.flags = hn->flags & NODE_POISONED;

	if (hn->type == NT_MACRO)
	  {
	    defn = cpp_macro_definition (pfile, hn);
	    s.definition_length = ustrlen (defn);
	  }
	else
	  {
	    defn = NODE_NAME (hn);
	    s.definition_length = s.name_length;
	  }

	if (fwrite (&s, sizeof (s), 1, f) != 1
	    || fwrite (defn, 1, s.definition_length, f) != s.definition_length)
	  {
	    cpp_errno (pfile, CPP_DL_ERROR,
		       "while writing precompiled header");
	    return 0;
	  }
      }
      return 1;

    case NT_ASSERTION:
      /* Not currently implemented.  */
      return 1;

    default:
      abort ();
    }
}

/* This structure records the names of the defined macros.
   It's also used as a callback structure for size_initial_idents
   and save_idents.  */

struct cpp_savedstate
{
  /* A hash table of the defined identifiers.  */
  htab_t definedhash;
  /* The size of the definitions of those identifiers (the size of
     'definedstrs').  */
  size_t hashsize;
  /* Number of definitions */
  size_t n_defs;
  /* Array of definitions.  In cpp_write_pch_deps it is used for sorting.  */
  cpp_hashnode **defs;
  /* Space for the next definition.  Definitions are null-terminated
     strings.  */
  unsigned char *definedstrs;
};

/* Save this identifier into the state: put it in the hash table,
   put the definition in 'definedstrs'.  */

static int
save_idents (cpp_reader *pfile ATTRIBUTE_UNUSED, cpp_hashnode *hn, void *ss_p)
{
  struct cpp_savedstate *const ss = (struct cpp_savedstate *)ss_p;

  if (hn->type != NT_VOID)
    {
      struct cpp_string news;
      void **slot;

      news.len = NODE_LEN (hn);
      news.text= NODE_NAME (hn);
      slot = htab_find_slot (ss->definedhash, &news, INSERT);
      if (*slot == NULL)
	{
	  struct cpp_string *sp;
	  unsigned char *text;

	  sp = XNEW (struct cpp_string);
	  *slot = sp;

	  sp->len = NODE_LEN (hn);
	  sp->text = text = XNEWVEC (unsigned char, NODE_LEN (hn));
	  memcpy (text, NODE_NAME (hn), NODE_LEN (hn));
	}
    }

  return 1;
}

/* Hash some memory in a generic way.  */

static hashval_t
hashmem (const void *p_p, size_t sz)
{
  const unsigned char *p = (const unsigned char *)p_p;
  size_t i;
  hashval_t h;

  h = 0;
  for (i = 0; i < sz; i++)
    h = h * 67 - (*p++ - 113);
  return h;
}

/* Hash a cpp string for the hashtable machinery.  */

static hashval_t
cpp_string_hash (const void *a_p)
{
  const struct cpp_string *a = (const struct cpp_string *) a_p;
  return hashmem (a->text, a->len);
}

/* Compare two cpp strings for the hashtable machinery.  */

static int
cpp_string_eq (const void *a_p, const void *b_p)
{
  const struct cpp_string *a = (const struct cpp_string *) a_p;
  const struct cpp_string *b = (const struct cpp_string *) b_p;
  return (a->len == b->len
	  && memcmp (a->text, b->text, a->len) == 0);
}

/* Save the current definitions of the cpp_reader for dependency
   checking purposes.  When writing a precompiled header, this should
   be called at the same point in the compilation as cpp_valid_state
   would be called when reading the precompiled header back in.  */

int
cpp_save_state (cpp_reader *r, FILE *f)
{
  /* Save the list of non-void identifiers for the dependency checking.  */
  r->savedstate = XNEW (struct cpp_savedstate);
  r->savedstate->definedhash = htab_create (100, cpp_string_hash,
					    cpp_string_eq, NULL);
  cpp_forall_identifiers (r, save_idents, r->savedstate);

  /* Write out the list of defined identifiers.  */
  cpp_forall_identifiers (r, write_macdef, f);

  return 0;
}

/* Calculate the 'hashsize' field of the saved state.  */

static int
count_defs (cpp_reader *pfile ATTRIBUTE_UNUSED, cpp_hashnode *hn, void *ss_p)
{
  struct cpp_savedstate *const ss = (struct cpp_savedstate *)ss_p;

  switch (hn->type)
    {
    case NT_MACRO:
      if (hn->flags & NODE_BUILTIN)
	return 1;

      /* else fall through.  */

    case NT_VOID:
      {
	struct cpp_string news;
	void **slot;

	news.len = NODE_LEN (hn);
	news.text = NODE_NAME (hn);
	slot = (void **) htab_find (ss->definedhash, &news);
	if (slot == NULL)
	  {
	    ss->hashsize += NODE_LEN (hn) + 1;
	    ss->n_defs += 1;
	  }
      }
      return 1;

    case NT_ASSERTION:
      /* Not currently implemented.  */
      return 1;

    default:
      abort ();
    }
}

/* Collect the identifiers into the state's string table.  */
static int
write_defs (cpp_reader *pfile ATTRIBUTE_UNUSED, cpp_hashnode *hn, void *ss_p)
{
  struct cpp_savedstate *const ss = (struct cpp_savedstate *)ss_p;

  switch (hn->type)
    {
    case NT_MACRO:
      if (hn->flags & NODE_BUILTIN)
	return 1;

      /* else fall through.  */

    case NT_VOID:
      {
	struct cpp_string news;
	void **slot;

	news.len = NODE_LEN (hn);
	news.text = NODE_NAME (hn);
	slot = (void **) htab_find (ss->definedhash, &news);
	if (slot == NULL)
	  {
	    ss->defs[ss->n_defs] = hn;
	    ss->n_defs += 1;
	  }
      }
      return 1;

    case NT_ASSERTION:
      /* Not currently implemented.  */
      return 1;

    default:
      abort ();
    }
}

/* Comparison function for qsort.  The arguments point to pointers of
   type ht_hashnode *.  */
static int
comp_hashnodes (const void *px, const void *py)
{
  cpp_hashnode *x = *(cpp_hashnode **) px;
  cpp_hashnode *y = *(cpp_hashnode **) py;
  return ustrcmp (NODE_NAME (x), NODE_NAME (y));
}

/* Write out the remainder of the dependency information.  This should be
   called after the PCH is ready to be saved.  */

int
cpp_write_pch_deps (cpp_reader *r, FILE *f)
{
  struct macrodef_struct z;
  struct cpp_savedstate *const ss = r->savedstate;
  unsigned char *definedstrs;
  size_t i;

  /* Collect the list of identifiers which have been seen and
     weren't defined to anything previously.  */
  ss->hashsize = 0;
  ss->n_defs = 0;
  cpp_forall_identifiers (r, count_defs, ss);

  ss->defs = XNEWVEC (cpp_hashnode *, ss->n_defs);
  ss->n_defs = 0;
  cpp_forall_identifiers (r, write_defs, ss);

  /* Sort the list, copy it into a buffer, and write it out.  */
  qsort (ss->defs, ss->n_defs, sizeof (cpp_hashnode *), &comp_hashnodes);
  definedstrs = ss->definedstrs = XNEWVEC (unsigned char, ss->hashsize);
  for (i = 0; i < ss->n_defs; ++i)
    {
      size_t len = NODE_LEN (ss->defs[i]);
      memcpy (definedstrs, NODE_NAME (ss->defs[i]), len + 1);
      definedstrs += len + 1;
    }

  memset (&z, 0, sizeof (z));
  z.definition_length = ss->hashsize;
  if (fwrite (&z, sizeof (z), 1, f) != 1
      || fwrite (ss->definedstrs, ss->hashsize, 1, f) != 1)
    {
      cpp_errno (r, CPP_DL_ERROR, "while writing precompiled header");
      return -1;
    }
  free (ss->definedstrs);

  /* Free the saved state.  */
  free (ss);
  r->savedstate = NULL;

  /* Save the next value of __COUNTER__. */
  if (fwrite (&r->counter, sizeof (r->counter), 1, f) != 1)
    {
      cpp_errno (r, CPP_DL_ERROR, "while writing precompiled header");
      return -1;
    }

  return 0;
}

/* Write out the definitions of the preprocessor, in a form suitable for
   cpp_read_state.  */

int
cpp_write_pch_state (cpp_reader *r, FILE *f)
{
  if (!r->deps)
    r->deps = deps_init ();

  if (deps_save (r->deps, f) != 0)
    {
      cpp_errno (r, CPP_DL_ERROR, "while writing precompiled header");
      return -1;
    }

  if (! _cpp_save_file_entries (r, f))
    {
      cpp_errno (r, CPP_DL_ERROR, "while writing precompiled header");
      return -1;
    }

  /* Save the next __COUNTER__ value.  When we include a precompiled header,
     we need to start at the offset we would have if the header had been
     included normally. */
  if (fwrite (&r->counter, sizeof (r->counter), 1, f) != 1)
    {
      cpp_errno (r, CPP_DL_ERROR, "while writing precompiled header");
      return -1;
    }

  return 0;
}


/* Data structure to transform hash table nodes into a sorted list */

struct ht_node_list
{
  /* Array of nodes */
  cpp_hashnode **defs;
  /* Number of nodes in the array */
  size_t n_defs;
  /* Size of the allocated array */
  size_t asize;
};

/* Callback for collecting identifiers from hash table */

static int
collect_ht_nodes (cpp_reader *pfile ATTRIBUTE_UNUSED, cpp_hashnode *hn,
		  void *nl_p)
{
  struct ht_node_list *const nl = (struct ht_node_list *)nl_p;

  if (hn->type != NT_VOID || hn->flags & NODE_POISONED)
    {
      if (nl->n_defs == nl->asize)
        {
          nl->asize *= 2;
          nl->defs = XRESIZEVEC (cpp_hashnode *, nl->defs, nl->asize);
        }

      nl->defs[nl->n_defs] = hn;
      ++nl->n_defs;
    }
  return 1;
}


/* Return nonzero if FD is a precompiled header which is consistent
   with the preprocessor's current definitions.  It will be consistent
   when:

   - anything that was defined just before the PCH was generated
     is defined the same way now; and
   - anything that was not defined then, but is defined now, was not
     used by the PCH.

   NAME is used to print warnings if `warn_invalid_pch' is set in the
   reader's flags.
*/

int
cpp_valid_state (cpp_reader *r, const char *name, int fd)
{
  struct macrodef_struct m;
  size_t namebufsz = 256;
  unsigned char *namebuf = XNEWVEC (unsigned char, namebufsz);
  unsigned char *undeftab = NULL;
  struct ht_node_list nl = { 0, 0, 0 };
  unsigned char *first, *last;
  unsigned int i;
  unsigned int counter;
  
  /* Read in the list of identifiers that must be defined
     Check that they are defined in the same way.  */
  for (;;)
    {
      cpp_hashnode *h;
      const unsigned char *newdefn;

      if (read (fd, &m, sizeof (m)) != sizeof (m))
	goto error;

      if (m.name_length == 0)
	break;

      /* If this file is already preprocessed, there won't be any
	 macros defined, and that's OK.  */
      if (CPP_OPTION (r, preprocessed))
	{
	  if (lseek (fd, m.definition_length, SEEK_CUR) == -1)
	    goto error;
	  continue;
	}

      if (m.definition_length > namebufsz)
	{
	  free (namebuf);
	  namebufsz = m.definition_length + 256;
	  namebuf = XNEWVEC (unsigned char, namebufsz);
	}

      if ((size_t)read (fd, namebuf, m.definition_length)
	  != m.definition_length)
	goto error;

      h = cpp_lookup (r, namebuf, m.name_length);
      if (m.flags & NODE_POISONED
	  || h->type != NT_MACRO
	  || h->flags & NODE_POISONED)
	{
	  if (CPP_OPTION (r, warn_invalid_pch))
	    cpp_error (r, CPP_DL_WARNING_SYSHDR,
		       "%s: not used because `%.*s' not defined",
		       name, m.name_length, namebuf);
	  goto fail;
	}

      newdefn = cpp_macro_definition (r, h);

      if (m.definition_length != ustrlen (newdefn)
	  || memcmp (namebuf, newdefn, m.definition_length) != 0)
	{
	  if (CPP_OPTION (r, warn_invalid_pch))
	    cpp_error (r, CPP_DL_WARNING_SYSHDR,
	       "%s: not used because `%.*s' defined as `%s' not `%.*s'",
		       name, m.name_length, namebuf, newdefn + m.name_length,
		       m.definition_length - m.name_length,
		       namebuf +  m.name_length);
	  goto fail;
	}
    }
  free (namebuf);
  namebuf = NULL;

  /* Read in the list of identifiers that must not be defined.
     Check that they really aren't.  */
  undeftab = XNEWVEC (unsigned char, m.definition_length);
  if ((size_t) read (fd, undeftab, m.definition_length) != m.definition_length)
    goto error;

  /* Collect identifiers from the current hash table.  */
  nl.n_defs = 0;
  nl.asize = 10;
  nl.defs = XNEWVEC (cpp_hashnode *, nl.asize);
  cpp_forall_identifiers (r, &collect_ht_nodes, &nl);
  qsort (nl.defs, nl.n_defs, sizeof (cpp_hashnode *), &comp_hashnodes);

  /* Loop through nl.defs and undeftab, both of which are sorted lists.
     There should be no matches.  */
  first = undeftab;
  last = undeftab + m.definition_length;
  i = 0;

  while (first < last && i < nl.n_defs)
    {
      int cmp = ustrcmp (first, NODE_NAME (nl.defs[i]));

      if (cmp < 0)
 	first += ustrlen (first) + 1;
      else if (cmp > 0)
 	++i;
      else
	{
	  if (CPP_OPTION (r, warn_invalid_pch))
	    cpp_error (r, CPP_DL_WARNING_SYSHDR,
		       "%s: not used because `%s' is defined",
		       name, first);
	  goto fail;
	}
    }

  free(nl.defs);
  nl.defs = NULL;
  free (undeftab);
  undeftab = NULL;

  /* Read in the next value of __COUNTER__.
     Check that (a) __COUNTER__ was not used in the pch or (b) __COUNTER__
     has not been used in this translation unit. */
  if (read (fd, &counter, sizeof (counter)) != sizeof (counter))
    goto error;
  if (counter && r->counter)
    {
      if (CPP_OPTION (r, warn_invalid_pch))
	cpp_error (r, CPP_DL_WARNING_SYSHDR, 
		   "%s: not used because `__COUNTER__' is invalid",
		   name);
	goto fail;
    }

  /* We win!  */
  return 0;

 error:
  cpp_errno (r, CPP_DL_ERROR, "while reading precompiled header");
  return -1;

 fail:
  if (namebuf != NULL)
    free (namebuf);
  if (undeftab != NULL)
    free (undeftab);
  if (nl.defs != NULL)
    free (nl.defs);
  return 1;
}

/* Save all the existing macros.  */

struct save_macro_data
{
  uchar **defns;
  size_t count;
  size_t array_size;
  char **saved_pragmas;
};

/* Save the definition of a single macro, so that it will persist
   across a PCH restore.  Because macro data is in GCed memory, which
   will be blown away by PCH, it must be temporarily copied to
   malloced memory.  (The macros will refer to identifier nodes which
   are also GCed and so on, so the copying is done by turning them
   into self-contained strings.)  The assumption is that most macro
   definitions will come from the PCH file, not from the compilation
   before the PCH file is loaded, so it doesn't matter that this is
   a little expensive.

   It would reduce the cost even further if macros defined in the PCH
   file were not saved in this way, but this is not done (yet), except
   for builtins, and for #assert by default.  */

static int
save_macros (cpp_reader *r, cpp_hashnode *h, void *data_p)
{
  struct save_macro_data *data = (struct save_macro_data *)data_p;
  if (h->type != NT_VOID
      && (h->flags & NODE_BUILTIN) == 0)
    {
      if (data->count == data->array_size)
	{
	  data->array_size *= 2;
	  data->defns = XRESIZEVEC (uchar *, data->defns, (data->array_size));
	}

      switch (h->type)
	{
	case NT_ASSERTION:
	  /* Not currently implemented.  */
	  return 1;

	case NT_MACRO:
	  {
	    const uchar * defn = cpp_macro_definition (r, h);
	    size_t defnlen = ustrlen (defn);

	    data->defns[data->count] = (uchar *) xmemdup (defn, defnlen,
                                                          defnlen + 2);
	    data->defns[data->count][defnlen] = '\n';
	  }
	  break;

	default:
	  abort ();
	}
      data->count++;
    }
  return 1;
}

/* Prepare to restore the state, by saving the currently-defined
   macros in 'data'.  */

void
cpp_prepare_state (cpp_reader *r, struct save_macro_data **data)
{
  struct save_macro_data *d = XNEW (struct save_macro_data);

  d->array_size = 512;
  d->defns = XNEWVEC (uchar *, d->array_size);
  d->count = 0;
  cpp_forall_identifiers (r, save_macros, d);
  d->saved_pragmas = _cpp_save_pragma_names (r);
  *data = d;
}

/* Given a precompiled header that was previously determined to be valid,
   apply all its definitions (and undefinitions) to the current state.
   DEPNAME is passed to deps_restore.  */

int
cpp_read_state (cpp_reader *r, const char *name, FILE *f,
		struct save_macro_data *data)
{
  size_t i;
  struct lexer_state old_state;
  unsigned int counter;

  /* Restore spec_nodes, which will be full of references to the old
     hashtable entries and so will now be invalid.  */
  {
    struct spec_nodes *s = &r->spec_nodes;
    s->n_defined	= cpp_lookup (r, DSC("defined"));
    s->n_true		= cpp_lookup (r, DSC("true"));
    s->n_false		= cpp_lookup (r, DSC("false"));
    s->n__VA_ARGS__     = cpp_lookup (r, DSC("__VA_ARGS__"));
  }

  old_state = r->state;
  r->state.in_directive = 1;
  r->state.prevent_expansion = 1;
  r->state.angled_headers = 0;

  /* Run through the carefully-saved macros, insert them.  */
  for (i = 0; i < data->count; i++)
    {
      cpp_hashnode *h;
      size_t namelen;
      uchar *defn;

      namelen = ustrcspn (data->defns[i], "( \n");
      h = cpp_lookup (r, data->defns[i], namelen);
      defn = data->defns[i] + namelen;

      /* The PCH file is valid, so we know that if there is a definition
	 from the PCH file it must be the same as the one we had
	 originally, and so do not need to restore it.  */
      if (h->type == NT_VOID)
	{
	  if (cpp_push_buffer (r, defn, ustrchr (defn, '\n') - defn, true)
	      != NULL)
	    {
	      _cpp_clean_line (r);
	      if (!_cpp_create_definition (r, h))
		abort ();
	      _cpp_pop_buffer (r);
	    }
	  else
	    abort ();
	}

      free (data->defns[i]);
    }
  r->state = old_state;

  _cpp_restore_pragma_names (r, data->saved_pragmas);

  free (data);

  if (deps_restore (r->deps, f, CPP_OPTION (r, restore_pch_deps) ? name : NULL)
      != 0)
    goto error;

  if (! _cpp_read_file_entries (r, f))
    goto error;

  if (fread (&counter, sizeof (counter), 1, f) != 1)
    goto error;

  if (!r->counter)
    r->counter = counter;

  return 0;

 error:
  cpp_errno (r, CPP_DL_ERROR, "while reading precompiled header");
  return -1;
}
