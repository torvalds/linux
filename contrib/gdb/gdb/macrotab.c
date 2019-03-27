/* C preprocessor macro tables for GDB.
   Copyright 2002 Free Software Foundation, Inc.
   Contributed by Red Hat, Inc.

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
#include "gdb_obstack.h"
#include "splay-tree.h"
#include "symtab.h"
#include "symfile.h"
#include "objfiles.h"
#include "macrotab.h"
#include "gdb_assert.h"
#include "bcache.h"
#include "complaints.h"


/* The macro table structure.  */

struct macro_table
{
  /* The obstack this table's data should be allocated in, or zero if
     we should use xmalloc.  */
  struct obstack *obstack;

  /* The bcache we should use to hold macro names, argument names, and
     definitions, or zero if we should use xmalloc.  */
  struct bcache *bcache;

  /* The main source file for this compilation unit --- the one whose
     name was given to the compiler.  This is the root of the
     #inclusion tree; everything else is #included from here.  */
  struct macro_source_file *main_source;

  /* The table of macro definitions.  This is a splay tree (an ordered
     binary tree that stays balanced, effectively), sorted by macro
     name.  Where a macro gets defined more than once (presumably with
     an #undefinition in between), we sort the definitions by the
     order they would appear in the preprocessor's output.  That is,
     if `a.c' #includes `m.h' and then #includes `n.h', and both
     header files #define X (with an #undef somewhere in between),
     then the definition from `m.h' appears in our splay tree before
     the one from `n.h'.

     The splay tree's keys are `struct macro_key' pointers;
     the values are `struct macro_definition' pointers.

     The splay tree, its nodes, and the keys and values are allocated
     in obstack, if it's non-zero, or with xmalloc otherwise.  The
     macro names, argument names, argument name arrays, and definition
     strings are all allocated in bcache, if non-zero, or with xmalloc
     otherwise.  */
  splay_tree definitions;
};



/* Allocation and freeing functions.  */

/* Allocate SIZE bytes of memory appropriately for the macro table T.
   This just checks whether T has an obstack, or whether its pieces
   should be allocated with xmalloc.  */
static void *
macro_alloc (int size, struct macro_table *t)
{
  if (t->obstack)
    return obstack_alloc (t->obstack, size);
  else
    return xmalloc (size);
}


static void
macro_free (void *object, struct macro_table *t)
{
  gdb_assert (! t->obstack);
  xfree (object);
}


/* If the macro table T has a bcache, then cache the LEN bytes at ADDR
   there, and return the cached copy.  Otherwise, just xmalloc a copy
   of the bytes, and return a pointer to that.  */
static const void *
macro_bcache (struct macro_table *t, const void *addr, int len)
{
  if (t->bcache)
    return bcache (addr, len, t->bcache);
  else
    {
      void *copy = xmalloc (len);
      memcpy (copy, addr, len);
      return copy;
    }
}


/* If the macro table T has a bcache, cache the null-terminated string
   S there, and return a pointer to the cached copy.  Otherwise,
   xmalloc a copy and return that.  */
static const char *
macro_bcache_str (struct macro_table *t, const char *s)
{
  return (char *) macro_bcache (t, s, strlen (s) + 1);
}


/* Free a possibly bcached object OBJ.  That is, if the macro table T
   has a bcache, it's an error; otherwise, xfree OBJ.  */
static void
macro_bcache_free (struct macro_table *t, void *obj)
{
  gdb_assert (! t->bcache);
  xfree (obj);
}



/* Macro tree keys, w/their comparison, allocation, and freeing functions.  */

/* A key in the splay tree.  */
struct macro_key
{
  /* The table we're in.  We only need this in order to free it, since
     the splay tree library's key and value freeing functions require
     that the key or value contain all the information needed to free
     themselves.  */
  struct macro_table *table;

  /* The name of the macro.  This is in the table's bcache, if it has
     one. */
  const char *name;

  /* The source file and line number where the definition's scope
     begins.  This is also the line of the definition itself.  */
  struct macro_source_file *start_file;
  int start_line;

  /* The first source file and line after the definition's scope.
     (That is, the scope does not include this endpoint.)  If end_file
     is zero, then the definition extends to the end of the
     compilation unit.  */
  struct macro_source_file *end_file;
  int end_line;
};


/* Return the #inclusion depth of the source file FILE.  This is the
   number of #inclusions it took to reach this file.  For the main
   source file, the #inclusion depth is zero; for a file it #includes
   directly, the depth would be one; and so on.  */
static int
inclusion_depth (struct macro_source_file *file)
{
  int depth;

  for (depth = 0; file->included_by; depth++)
    file = file->included_by;

  return depth;
}


/* Compare two source locations (from the same compilation unit).
   This is part of the comparison function for the tree of
   definitions.

   LINE1 and LINE2 are line numbers in the source files FILE1 and
   FILE2.  Return a value:
   - less than zero if {LINE,FILE}1 comes before {LINE,FILE}2,
   - greater than zero if {LINE,FILE}1 comes after {LINE,FILE}2, or
   - zero if they are equal.

   When the two locations are in different source files --- perhaps
   one is in a header, while another is in the main source file --- we
   order them by where they would appear in the fully pre-processed
   sources, where all the #included files have been substituted into
   their places.  */
static int
compare_locations (struct macro_source_file *file1, int line1, 
                   struct macro_source_file *file2, int line2)
{
  /* We want to treat positions in an #included file as coming *after*
     the line containing the #include, but *before* the line after the
     include.  As we walk up the #inclusion tree toward the main
     source file, we update fileX and lineX as we go; includedX
     indicates whether the original position was from the #included
     file.  */
  int included1 = 0;
  int included2 = 0;

  /* If a file is zero, that means "end of compilation unit."  Handle
     that specially.  */
  if (! file1)
    {
      if (! file2)
        return 0;
      else
        return 1;
    }
  else if (! file2)
    return -1;

  /* If the two files are not the same, find their common ancestor in
     the #inclusion tree.  */
  if (file1 != file2)
    {
      /* If one file is deeper than the other, walk up the #inclusion
         chain until the two files are at least at the same *depth*.
         Then, walk up both files in synchrony until they're the same
         file.  That file is the common ancestor.  */
      int depth1 = inclusion_depth (file1);
      int depth2 = inclusion_depth (file2);

      /* Only one of these while loops will ever execute in any given
         case.  */
      while (depth1 > depth2)
        {
          line1 = file1->included_at_line;
          file1 = file1->included_by;
          included1 = 1;
          depth1--;
        }
      while (depth2 > depth1)
        {
          line2 = file2->included_at_line;
          file2 = file2->included_by;
          included2 = 1;
          depth2--;
        }

      /* Now both file1 and file2 are at the same depth.  Walk toward
         the root of the tree until we find where the branches meet.  */
      while (file1 != file2)
        {
          line1 = file1->included_at_line;
          file1 = file1->included_by;
          /* At this point, we know that the case the includedX flags
             are trying to deal with won't come up, but we'll just
             maintain them anyway.  */
          included1 = 1;

          line2 = file2->included_at_line;
          file2 = file2->included_by;
          included2 = 1;

          /* Sanity check.  If file1 and file2 are really from the
             same compilation unit, then they should both be part of
             the same tree, and this shouldn't happen.  */
          gdb_assert (file1 && file2);
        }
    }

  /* Now we've got two line numbers in the same file.  */
  if (line1 == line2)
    {
      /* They can't both be from #included files.  Then we shouldn't
         have walked up this far.  */
      gdb_assert (! included1 || ! included2);

      /* Any #included position comes after a non-#included position
         with the same line number in the #including file.  */
      if (included1)
        return 1;
      else if (included2)
        return -1;
      else
        return 0;
    }
  else
    return line1 - line2;
}


/* Compare a macro key KEY against NAME, the source file FILE, and
   line number LINE.

   Sort definitions by name; for two definitions with the same name,
   place the one whose definition comes earlier before the one whose
   definition comes later.

   Return -1, 0, or 1 if key comes before, is identical to, or comes
   after NAME, FILE, and LINE.  */
static int
key_compare (struct macro_key *key,
             const char *name, struct macro_source_file *file, int line)
{
  int names = strcmp (key->name, name);
  if (names)
    return names;

  return compare_locations (key->start_file, key->start_line,
                            file, line);
}


/* The macro tree comparison function, typed for the splay tree
   library's happiness.  */
static int
macro_tree_compare (splay_tree_key untyped_key1,
                    splay_tree_key untyped_key2)
{
  struct macro_key *key1 = (struct macro_key *) untyped_key1;
  struct macro_key *key2 = (struct macro_key *) untyped_key2;

  return key_compare (key1, key2->name, key2->start_file, key2->start_line);
}


/* Construct a new macro key node for a macro in table T whose name is
   NAME, and whose scope starts at LINE in FILE; register the name in
   the bcache.  */
static struct macro_key *
new_macro_key (struct macro_table *t,
               const char *name,
               struct macro_source_file *file,
               int line)
{
  struct macro_key *k = macro_alloc (sizeof (*k), t);

  memset (k, 0, sizeof (*k));
  k->table = t;
  k->name = macro_bcache_str (t, name);
  k->start_file = file;
  k->start_line = line;
  k->end_file = 0;

  return k;
}


static void
macro_tree_delete_key (void *untyped_key)
{
  struct macro_key *key = (struct macro_key *) untyped_key;

  macro_bcache_free (key->table, (char *) key->name);
  macro_free (key, key->table);
}



/* Building and querying the tree of #included files.  */


/* Allocate and initialize a new source file structure.  */
static struct macro_source_file *
new_source_file (struct macro_table *t,
                 const char *filename)
{
  /* Get space for the source file structure itself.  */
  struct macro_source_file *f = macro_alloc (sizeof (*f), t);

  memset (f, 0, sizeof (*f));
  f->table = t;
  f->filename = macro_bcache_str (t, filename);
  f->includes = 0;

  return f;
}


/* Free a source file, and all the source files it #included.  */
static void
free_macro_source_file (struct macro_source_file *src)
{
  struct macro_source_file *child, *next_child;

  /* Free this file's children.  */
  for (child = src->includes; child; child = next_child)
    {
      next_child = child->next_included;
      free_macro_source_file (child);
    }

  macro_bcache_free (src->table, (char *) src->filename);
  macro_free (src, src->table);
}


struct macro_source_file *
macro_set_main (struct macro_table *t,
                const char *filename)
{
  /* You can't change a table's main source file.  What would that do
     to the tree?  */
  gdb_assert (! t->main_source);

  t->main_source = new_source_file (t, filename);

  return t->main_source;
}


struct macro_source_file *
macro_main (struct macro_table *t)
{
  gdb_assert (t->main_source);

  return t->main_source;
}


struct macro_source_file *
macro_include (struct macro_source_file *source,
               int line,
               const char *included)
{
  struct macro_source_file *new;
  struct macro_source_file **link;

  /* Find the right position in SOURCE's `includes' list for the new
     file.  Skip inclusions at earlier lines, until we find one at the
     same line or later --- or until the end of the list.  */
  for (link = &source->includes;
       *link && (*link)->included_at_line < line;
       link = &(*link)->next_included)
    ;

  /* Did we find another file already #included at the same line as
     the new one?  */
  if (*link && line == (*link)->included_at_line)
    {
      /* This means the compiler is emitting bogus debug info.  (GCC
         circa March 2002 did this.)  It also means that the splay
         tree ordering function, macro_tree_compare, will abort,
         because it can't tell which #inclusion came first.  But GDB
         should tolerate bad debug info.  So:

         First, squawk.  */
      complaint (&symfile_complaints,
		 "both `%s' and `%s' allegedly #included at %s:%d", included,
		 (*link)->filename, source->filename, line);

      /* Now, choose a new, unoccupied line number for this
         #inclusion, after the alleged #inclusion line.  */
      while (*link && line == (*link)->included_at_line)
        {
          /* This line number is taken, so try the next line.  */
          line++;
          link = &(*link)->next_included;
        }
    }

  /* At this point, we know that LINE is an unused line number, and
     *LINK points to the entry an #inclusion at that line should
     precede.  */
  new = new_source_file (source->table, included);
  new->included_by = source;
  new->included_at_line = line;
  new->next_included = *link;
  *link = new;

  return new;
}


struct macro_source_file *
macro_lookup_inclusion (struct macro_source_file *source, const char *name)
{
  /* Is SOURCE itself named NAME?  */
  if (strcmp (name, source->filename) == 0)
    return source;

  /* The filename in the source structure is probably a full path, but
     NAME could be just the final component of the name.  */
  {
    int name_len = strlen (name);
    int src_name_len = strlen (source->filename);

    /* We do mean < here, and not <=; if the lengths are the same,
       then the strcmp above should have triggered, and we need to
       check for a slash here.  */
    if (name_len < src_name_len
        && source->filename[src_name_len - name_len - 1] == '/'
        && strcmp (name, source->filename + src_name_len - name_len) == 0)
      return source;
  }

  /* It's not us.  Try all our children, and return the lowest.  */
  {
    struct macro_source_file *child;
    struct macro_source_file *best = NULL;
    int best_depth = 0;

    for (child = source->includes; child; child = child->next_included)
      {
        struct macro_source_file *result
          = macro_lookup_inclusion (child, name);

        if (result)
          {
            int result_depth = inclusion_depth (result);

            if (! best || result_depth < best_depth)
              {
                best = result;
                best_depth = result_depth;
              }
          }
      }

    return best;
  }
}



/* Registering and looking up macro definitions.  */


/* Construct a definition for a macro in table T.  Cache all strings,
   and the macro_definition structure itself, in T's bcache.  */
static struct macro_definition *
new_macro_definition (struct macro_table *t,
                      enum macro_kind kind,
                      int argc, const char **argv,
                      const char *replacement)
{
  struct macro_definition *d = macro_alloc (sizeof (*d), t);

  memset (d, 0, sizeof (*d));
  d->table = t;
  d->kind = kind;
  d->replacement = macro_bcache_str (t, replacement);

  if (kind == macro_function_like)
    {
      int i;
      const char **cached_argv;
      int cached_argv_size = argc * sizeof (*cached_argv);

      /* Bcache all the arguments.  */
      cached_argv = alloca (cached_argv_size);
      for (i = 0; i < argc; i++)
        cached_argv[i] = macro_bcache_str (t, argv[i]);

      /* Now bcache the array of argument pointers itself.  */
      d->argv = macro_bcache (t, cached_argv, cached_argv_size);
      d->argc = argc;
    }

  /* We don't bcache the entire definition structure because it's got
     a pointer to the macro table in it; since each compilation unit
     has its own macro table, you'd only get bcache hits for identical
     definitions within a compilation unit, which seems unlikely.

     "So, why do macro definitions have pointers to their macro tables
     at all?"  Well, when the splay tree library wants to free a
     node's value, it calls the value freeing function with nothing
     but the value itself.  It makes the (apparently reasonable)
     assumption that the value carries enough information to free
     itself.  But not all macro tables have bcaches, so not all macro
     definitions would be bcached.  There's no way to tell whether a
     given definition is bcached without knowing which table the
     definition belongs to.  ...  blah.  The thing's only sixteen
     bytes anyway, and we can still bcache the name, args, and
     definition, so we just don't bother bcaching the definition
     structure itself.  */
  return d;
}


/* Free a macro definition.  */
static void
macro_tree_delete_value (void *untyped_definition)
{
  struct macro_definition *d = (struct macro_definition *) untyped_definition;
  struct macro_table *t = d->table;

  if (d->kind == macro_function_like)
    {
      int i;

      for (i = 0; i < d->argc; i++)
        macro_bcache_free (t, (char *) d->argv[i]);
      macro_bcache_free (t, (char **) d->argv);
    }
  
  macro_bcache_free (t, (char *) d->replacement);
  macro_free (d, t);
}


/* Find the splay tree node for the definition of NAME at LINE in
   SOURCE, or zero if there is none.  */
static splay_tree_node
find_definition (const char *name,
                 struct macro_source_file *file,
                 int line)
{
  struct macro_table *t = file->table;
  splay_tree_node n;

  /* Construct a macro_key object, just for the query.  */
  struct macro_key query;

  query.name = name;
  query.start_file = file;
  query.start_line = line;
  query.end_file = NULL;

  n = splay_tree_lookup (t->definitions, (splay_tree_key) &query);
  if (! n)
    {
      /* It's okay for us to do two queries like this: the real work
         of the searching is done when we splay, and splaying the tree
         a second time at the same key is a constant time operation.
         If this still bugs you, you could always just extend the
         splay tree library with a predecessor-or-equal operation, and
         use that.  */
      splay_tree_node pred = splay_tree_predecessor (t->definitions,
                                                     (splay_tree_key) &query);
     
      if (pred)
        {
          /* Make sure this predecessor actually has the right name.
             We just want to search within a given name's definitions.  */
          struct macro_key *found = (struct macro_key *) pred->key;

          if (strcmp (found->name, name) == 0)
            n = pred;
        }
    }

  if (n)
    {
      struct macro_key *found = (struct macro_key *) n->key;

      /* Okay, so this definition has the right name, and its scope
         begins before the given source location.  But does its scope
         end after the given source location?  */
      if (compare_locations (file, line, found->end_file, found->end_line) < 0)
        return n;
      else
        return 0;
    }
  else
    return 0;
}


/* If NAME already has a definition in scope at LINE in SOURCE, return
   the key.  If the old definition is different from the definition
   given by KIND, ARGC, ARGV, and REPLACEMENT, complain, too.
   Otherwise, return zero.  (ARGC and ARGV are meaningless unless KIND
   is `macro_function_like'.)  */
static struct macro_key *
check_for_redefinition (struct macro_source_file *source, int line,
                        const char *name, enum macro_kind kind,
                        int argc, const char **argv,
                        const char *replacement)
{
  splay_tree_node n = find_definition (name, source, line);

  if (n)
    {
      struct macro_key *found_key = (struct macro_key *) n->key;
      struct macro_definition *found_def
        = (struct macro_definition *) n->value;
      int same = 1;

      /* Is this definition the same as the existing one?
         According to the standard, this comparison needs to be done
         on lists of tokens, not byte-by-byte, as we do here.  But
         that's too hard for us at the moment, and comparing
         byte-by-byte will only yield false negatives (i.e., extra
         warning messages), not false positives (i.e., unnoticed
         definition changes).  */
      if (kind != found_def->kind)
        same = 0;
      else if (strcmp (replacement, found_def->replacement))
        same = 0;
      else if (kind == macro_function_like)
        {
          if (argc != found_def->argc)
            same = 0;
          else
            {
              int i;

              for (i = 0; i < argc; i++)
                if (strcmp (argv[i], found_def->argv[i]))
                  same = 0;
            }
        }

      if (! same)
        {
	  complaint (&symfile_complaints,
		     "macro `%s' redefined at %s:%d; original definition at %s:%d",
		     name, source->filename, line,
		     found_key->start_file->filename, found_key->start_line);
        }

      return found_key;
    }
  else
    return 0;
}


void
macro_define_object (struct macro_source_file *source, int line,
                     const char *name, const char *replacement)
{
  struct macro_table *t = source->table;
  struct macro_key *k;
  struct macro_definition *d;

  k = check_for_redefinition (source, line, 
                              name, macro_object_like,
                              0, 0,
                              replacement);

  /* If we're redefining a symbol, and the existing key would be
     identical to our new key, then the splay_tree_insert function
     will try to delete the old definition.  When the definition is
     living on an obstack, this isn't a happy thing.

     Since this only happens in the presence of questionable debug
     info, we just ignore all definitions after the first.  The only
     case I know of where this arises is in GCC's output for
     predefined macros, and all the definitions are the same in that
     case.  */
  if (k && ! key_compare (k, name, source, line))
    return;

  k = new_macro_key (t, name, source, line);
  d = new_macro_definition (t, macro_object_like, 0, 0, replacement);
  splay_tree_insert (t->definitions, (splay_tree_key) k, (splay_tree_value) d);
}


void
macro_define_function (struct macro_source_file *source, int line,
                       const char *name, int argc, const char **argv,
                       const char *replacement)
{
  struct macro_table *t = source->table;
  struct macro_key *k;
  struct macro_definition *d;

  k = check_for_redefinition (source, line,
                              name, macro_function_like,
                              argc, argv,
                              replacement);

  /* See comments about duplicate keys in macro_define_object.  */
  if (k && ! key_compare (k, name, source, line))
    return;

  /* We should also check here that all the argument names in ARGV are
     distinct.  */

  k = new_macro_key (t, name, source, line);
  d = new_macro_definition (t, macro_function_like, argc, argv, replacement);
  splay_tree_insert (t->definitions, (splay_tree_key) k, (splay_tree_value) d);
}


void
macro_undef (struct macro_source_file *source, int line,
             const char *name)
{
  splay_tree_node n = find_definition (name, source, line);

  if (n)
    {
      /* This function is the only place a macro's end-of-scope
         location gets set to anything other than "end of the
         compilation unit" (i.e., end_file is zero).  So if this macro
         already has its end-of-scope set, then we're probably seeing
         a second #undefinition for the same #definition.  */
      struct macro_key *key = (struct macro_key *) n->key;

      if (key->end_file)
        {
	  complaint (&symfile_complaints,
		     "macro '%s' is #undefined twice, at %s:%d and %s:%d", name,
		     source->filename, line, key->end_file->filename,
		     key->end_line);
        }

      /* Whatever the case, wipe out the old ending point, and 
         make this the ending point.  */
      key->end_file = source;
      key->end_line = line;
    }
  else
    {
      /* According to the ISO C standard, an #undef for a symbol that
         has no macro definition in scope is ignored.  So we should
         ignore it too.  */
#if 0
      complaint (&symfile_complaints,
		 "no definition for macro `%s' in scope to #undef at %s:%d",
		 name, source->filename, line);
#endif
    }
}


struct macro_definition *
macro_lookup_definition (struct macro_source_file *source,
                         int line, const char *name)
{
  splay_tree_node n = find_definition (name, source, line);

  if (n)
    return (struct macro_definition *) n->value;
  else
    return 0;
}


struct macro_source_file *
macro_definition_location (struct macro_source_file *source,
                           int line,
                           const char *name,
                           int *definition_line)
{
  splay_tree_node n = find_definition (name, source, line);

  if (n)
    {
      struct macro_key *key = (struct macro_key *) n->key;
      *definition_line = key->start_line;
      return key->start_file;
    }
  else
    return 0;
}



/* Creating and freeing macro tables.  */


struct macro_table *
new_macro_table (struct obstack *obstack,
                 struct bcache *b)
{
  struct macro_table *t;

  /* First, get storage for the `struct macro_table' itself.  */
  if (obstack)
    t = obstack_alloc (obstack, sizeof (*t));
  else
    t = xmalloc (sizeof (*t));

  memset (t, 0, sizeof (*t));
  t->obstack = obstack;
  t->bcache = b;
  t->main_source = NULL;
  t->definitions = (splay_tree_new_with_allocator
                    (macro_tree_compare,
                     ((splay_tree_delete_key_fn) macro_tree_delete_key),
                     ((splay_tree_delete_value_fn) macro_tree_delete_value),
                     ((splay_tree_allocate_fn) macro_alloc),
                     ((splay_tree_deallocate_fn) macro_free),
                     t));
  
  return t;
}


void
free_macro_table (struct macro_table *table)
{
  /* Free the source file tree.  */
  free_macro_source_file (table->main_source);

  /* Free the table of macro definitions.  */
  splay_tree_delete (table->definitions);
}
