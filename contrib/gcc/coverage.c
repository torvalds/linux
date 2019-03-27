/* Read and write coverage files, and associated functionality.
   Copyright (C) 1990, 1991, 1992, 1993, 1994, 1996, 1997, 1998, 1999,
   2000, 2001, 2003, 2004, 2005 Free Software Foundation, Inc.
   Contributed by James E. Wilson, UC Berkeley/Cygnus Support;
   based on some ideas from Dain Samples of UC Berkeley.
   Further mangling by Bob Manson, Cygnus Support.
   Further mangled by Nathan Sidwell, CodeSourcery

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */


#define GCOV_LINKAGE

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tm.h"
#include "rtl.h"
#include "tree.h"
#include "flags.h"
#include "output.h"
#include "regs.h"
#include "expr.h"
#include "function.h"
#include "toplev.h"
#include "ggc.h"
#include "coverage.h"
#include "langhooks.h"
#include "hashtab.h"
#include "tree-iterator.h"
#include "cgraph.h"

#include "gcov-io.c"

struct function_list
{
  struct function_list *next;	 /* next function */
  unsigned ident;		 /* function ident */
  unsigned checksum;	         /* function checksum */
  unsigned n_ctrs[GCOV_COUNTERS];/* number of counters.  */
};

/* Counts information for a function.  */
typedef struct counts_entry
{
  /* We hash by  */
  unsigned ident;
  unsigned ctr;

  /* Store  */
  unsigned checksum;
  gcov_type *counts;
  struct gcov_ctr_summary summary;

  /* Workspace */
  struct counts_entry *chain;

} counts_entry_t;

static struct function_list *functions_head = 0;
static struct function_list **functions_tail = &functions_head;
static unsigned no_coverage = 0;

/* Cumulative counter information for whole program.  */
static unsigned prg_ctr_mask; /* Mask of counter types generated.  */
static unsigned prg_n_ctrs[GCOV_COUNTERS]; /* Total counters allocated.  */

/* Counter information for current function.  */
static unsigned fn_ctr_mask; /* Mask of counters used.  */
static unsigned fn_n_ctrs[GCOV_COUNTERS]; /* Counters allocated.  */
static unsigned fn_b_ctrs[GCOV_COUNTERS]; /* Allocation base.  */

/* Name of the output file for coverage output file.  */
static char *bbg_file_name;
static unsigned bbg_file_opened;
static int bbg_function_announced;

/* Name of the count data file.  */
static char *da_file_name;

/* Hash table of count data.  */
static htab_t counts_hash = NULL;

/* Trees representing the counter table arrays.  */
static GTY(()) tree tree_ctr_tables[GCOV_COUNTERS];

/* The names of the counter tables.  Not used if we're
   generating counters at tree level.  */
static GTY(()) rtx ctr_labels[GCOV_COUNTERS];

/* The names of merge functions for counters.  */
static const char *const ctr_merge_functions[GCOV_COUNTERS] = GCOV_MERGE_FUNCTIONS;
static const char *const ctr_names[GCOV_COUNTERS] = GCOV_COUNTER_NAMES;

/* Forward declarations.  */
static hashval_t htab_counts_entry_hash (const void *);
static int htab_counts_entry_eq (const void *, const void *);
static void htab_counts_entry_del (void *);
static void read_counts_file (void);
static unsigned compute_checksum (void);
static unsigned coverage_checksum_string (unsigned, const char *);
static tree build_fn_info_type (unsigned);
static tree build_fn_info_value (const struct function_list *, tree);
static tree build_ctr_info_type (void);
static tree build_ctr_info_value (unsigned, tree);
static tree build_gcov_info (void);
static void create_coverage (void);

/* Return the type node for gcov_type.  */

tree
get_gcov_type (void)
{
  return lang_hooks.types.type_for_size (GCOV_TYPE_SIZE, false);
}

/* Return the type node for gcov_unsigned_t.  */

static tree
get_gcov_unsigned_t (void)
{
  return lang_hooks.types.type_for_size (32, true);
}

static hashval_t
htab_counts_entry_hash (const void *of)
{
  const counts_entry_t *entry = of;

  return entry->ident * GCOV_COUNTERS + entry->ctr;
}

static int
htab_counts_entry_eq (const void *of1, const void *of2)
{
  const counts_entry_t *entry1 = of1;
  const counts_entry_t *entry2 = of2;

  return entry1->ident == entry2->ident && entry1->ctr == entry2->ctr;
}

static void
htab_counts_entry_del (void *of)
{
  counts_entry_t *entry = of;

  free (entry->counts);
  free (entry);
}

/* Read in the counts file, if available.  */

static void
read_counts_file (void)
{
  gcov_unsigned_t fn_ident = 0;
  gcov_unsigned_t checksum = -1;
  counts_entry_t *summaried = NULL;
  unsigned seen_summary = 0;
  gcov_unsigned_t tag;
  int is_error = 0;

  if (!gcov_open (da_file_name, 1))
    return;

  if (!gcov_magic (gcov_read_unsigned (), GCOV_DATA_MAGIC))
    {
      warning (0, "%qs is not a gcov data file", da_file_name);
      gcov_close ();
      return;
    }
  else if ((tag = gcov_read_unsigned ()) != GCOV_VERSION)
    {
      char v[4], e[4];

      GCOV_UNSIGNED2STRING (v, tag);
      GCOV_UNSIGNED2STRING (e, GCOV_VERSION);

      warning (0, "%qs is version %q.*s, expected version %q.*s",
 	       da_file_name, 4, v, 4, e);
      gcov_close ();
      return;
    }

  /* Read and discard the stamp.  */
  gcov_read_unsigned ();
  
  counts_hash = htab_create (10,
			     htab_counts_entry_hash, htab_counts_entry_eq,
			     htab_counts_entry_del);
  while ((tag = gcov_read_unsigned ()))
    {
      gcov_unsigned_t length;
      gcov_position_t offset;

      length = gcov_read_unsigned ();
      offset = gcov_position ();
      if (tag == GCOV_TAG_FUNCTION)
	{
	  fn_ident = gcov_read_unsigned ();
	  checksum = gcov_read_unsigned ();
	  if (seen_summary)
	    {
	      /* We have already seen a summary, this means that this
		 new function begins a new set of program runs. We
		 must unlink the summaried chain.  */
	      counts_entry_t *entry, *chain;

	      for (entry = summaried; entry; entry = chain)
		{
		  chain = entry->chain;
		  entry->chain = NULL;
		}
	      summaried = NULL;
	      seen_summary = 0;
	    }
	}
      else if (tag == GCOV_TAG_PROGRAM_SUMMARY)
	{
	  counts_entry_t *entry;
	  struct gcov_summary summary;

	  gcov_read_summary (&summary);
	  seen_summary = 1;
	  for (entry = summaried; entry; entry = entry->chain)
	    {
	      struct gcov_ctr_summary *csum = &summary.ctrs[entry->ctr];

	      entry->summary.runs += csum->runs;
	      entry->summary.sum_all += csum->sum_all;
	      if (entry->summary.run_max < csum->run_max)
		entry->summary.run_max = csum->run_max;
	      entry->summary.sum_max += csum->sum_max;
	    }
	}
      else if (GCOV_TAG_IS_COUNTER (tag) && fn_ident)
	{
	  counts_entry_t **slot, *entry, elt;
	  unsigned n_counts = GCOV_TAG_COUNTER_NUM (length);
	  unsigned ix;

	  elt.ident = fn_ident;
	  elt.ctr = GCOV_COUNTER_FOR_TAG (tag);

	  slot = (counts_entry_t **) htab_find_slot
	    (counts_hash, &elt, INSERT);
	  entry = *slot;
	  if (!entry)
	    {
	      *slot = entry = XCNEW (counts_entry_t);
	      entry->ident = elt.ident;
	      entry->ctr = elt.ctr;
	      entry->checksum = checksum;
	      entry->summary.num = n_counts;
	      entry->counts = XCNEWVEC (gcov_type, n_counts);
	    }
	  else if (entry->checksum != checksum)
	    {
	      error ("coverage mismatch for function %u while reading execution counters",
		     fn_ident);
	      error ("checksum is %x instead of %x", entry->checksum, checksum);
	      htab_delete (counts_hash);
	      break;
	    }
	  else if (entry->summary.num != n_counts)
	    {
	      error ("coverage mismatch for function %u while reading execution counters",
		     fn_ident);
	      error ("number of counters is %d instead of %d", entry->summary.num, n_counts);
	      htab_delete (counts_hash);
	      break;
	    }
	  else if (elt.ctr >= GCOV_COUNTERS_SUMMABLE)
	    {
	      error ("cannot merge separate %s counters for function %u",
		     ctr_names[elt.ctr], fn_ident);
	      goto skip_merge;
	    }

	  if (elt.ctr < GCOV_COUNTERS_SUMMABLE
	      /* This should always be true for a just allocated entry,
		 and always false for an existing one. Check this way, in
		 case the gcov file is corrupt.  */
	      && (!entry->chain || summaried != entry))
	    {
	      entry->chain = summaried;
	      summaried = entry;
	    }
	  for (ix = 0; ix != n_counts; ix++)
	    entry->counts[ix] += gcov_read_counter ();
	skip_merge:;
	}
      gcov_sync (offset, length);
      if ((is_error = gcov_is_error ()))
	{
	  error (is_error < 0 ? "%qs has overflowed" : "%qs is corrupted",
		 da_file_name);
	  htab_delete (counts_hash);
	  break;
	}
    }

  gcov_close ();
}

/* Returns the counters for a particular tag.  */

gcov_type *
get_coverage_counts (unsigned counter, unsigned expected,
		     const struct gcov_ctr_summary **summary)
{
  counts_entry_t *entry, elt;
  gcov_unsigned_t checksum = -1;

  /* No hash table, no counts.  */
  if (!counts_hash)
    {
      static int warned = 0;

      if (!warned++)
	inform ((flag_guess_branch_prob
		 ? "file %s not found, execution counts estimated"
		 : "file %s not found, execution counts assumed to be zero"),
		da_file_name);
      return NULL;
    }

  elt.ident = current_function_funcdef_no + 1;
  elt.ctr = counter;
  entry = htab_find (counts_hash, &elt);
  if (!entry)
    {
      warning (0, "no coverage for function %qs found", IDENTIFIER_POINTER
	       (DECL_ASSEMBLER_NAME (current_function_decl)));
      return 0;
    }

  checksum = compute_checksum ();
  if (entry->checksum != checksum)
    {
      error ("coverage mismatch for function %qs while reading counter %qs",
	     IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (current_function_decl)),
	     ctr_names[counter]);
      error ("checksum is %x instead of %x", entry->checksum, checksum);
      return 0;
    }
  else if (entry->summary.num != expected)
    {
      error ("coverage mismatch for function %qs while reading counter %qs",
	     IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (current_function_decl)),
	     ctr_names[counter]);
      error ("number of counters is %d instead of %d", entry->summary.num, expected);
      return 0;
    }

  if (summary)
    *summary = &entry->summary;

  return entry->counts;
}

/* Allocate NUM counters of type COUNTER. Returns nonzero if the
   allocation succeeded.  */

int
coverage_counter_alloc (unsigned counter, unsigned num)
{
  if (no_coverage)
    return 0;

  if (!num)
    return 1;

  if (!tree_ctr_tables[counter])
    {
      /* Generate and save a copy of this so it can be shared.  Leave
	 the index type unspecified for now; it will be set after all
	 functions have been compiled.  */
      char buf[20];
      tree gcov_type_node = get_gcov_type ();
      tree gcov_type_array_type
        = build_array_type (gcov_type_node, NULL_TREE);
      tree_ctr_tables[counter]
        = build_decl (VAR_DECL, NULL_TREE, gcov_type_array_type);
      TREE_STATIC (tree_ctr_tables[counter]) = 1;
      ASM_GENERATE_INTERNAL_LABEL (buf, "LPBX", counter + 1);
      DECL_NAME (tree_ctr_tables[counter]) = get_identifier (buf);
      DECL_ALIGN (tree_ctr_tables[counter]) = TYPE_ALIGN (gcov_type_node);
    }
  fn_b_ctrs[counter] = fn_n_ctrs[counter];
  fn_n_ctrs[counter] += num;
  fn_ctr_mask |= 1 << counter;
  return 1;
}

/* Generate a tree to access COUNTER NO.  */

tree
tree_coverage_counter_ref (unsigned counter, unsigned no)
{
  tree gcov_type_node = get_gcov_type ();

  gcc_assert (no < fn_n_ctrs[counter] - fn_b_ctrs[counter]);
  no += prg_n_ctrs[counter] + fn_b_ctrs[counter];

  /* "no" here is an array index, scaled to bytes later.  */
  return build4 (ARRAY_REF, gcov_type_node, tree_ctr_tables[counter],
		 build_int_cst (NULL_TREE, no), NULL, NULL);
}

/* Generate a checksum for a string.  CHKSUM is the current
   checksum.  */

static unsigned
coverage_checksum_string (unsigned chksum, const char *string)
{
  char *dup = NULL;
  char *ptr;

  /* Look for everything that looks if it were produced by
     get_file_function_name and zero out the second part
     that may result from flag_random_seed.  This is not critical
     as the checksums are used only for sanity checking.  */
#define GLOBAL_PREFIX "_GLOBAL__"
#define TRAILING_N "N_"
#define ISCAPXDIGIT(a) (((a) >= '0' && (a) <= '9') || ((a) >= 'A' && (a) <= 'F'))
  if ((ptr = strstr (string, GLOBAL_PREFIX)))
    {
      /* Skip _GLOBAL__. */
      ptr += strlen (GLOBAL_PREFIX);

      /* Skip optional N_ (in case __GLOBAL_N__). */
      if (!strncmp (ptr, TRAILING_N, strlen (TRAILING_N)))
          ptr += strlen (TRAILING_N);
      /* At this point, ptr should point after "_GLOBAL__N_" or "_GLOBAL__". */

      while ((ptr = strchr (ptr, '_')) != NULL)
        {
          int y;
          /* For every "_" in the rest of the string,
             try the follwing pattern matching */

          /* Skip over '_'. */
          ptr++;
#define NDIGITS (8)
          /* Try matching the pattern:
             <8-digit hex>_<8-digit hex>
             The second number is randomly generated
             so we want to mask it out before computing the checksum. */
          for (y = 0; *ptr != 0 && y < NDIGITS; y++, ptr++)
              if (!ISCAPXDIGIT (*ptr))
                  break;
          if (y != NDIGITS || *ptr != '_')
              continue;
          /* Skip over '_' again. */
          ptr++;
          for (y = 0; *ptr != 0 && y < NDIGITS; y++, ptr++)
              if (!ISCAPXDIGIT (*ptr))
                  break;

          if (y == NDIGITS)
            {
              /* We have a match.
                 Duplicate the string and mask out
                 the second 8-digit number. */
              dup = xstrdup (string);
              ptr = dup + (ptr - string);
              for(y = -NDIGITS - 1 ; y < 0; y++)
                {
                  ptr[y] = '0';
                }
              ptr = dup;
              break;
            }
        }
        /* "ptr" should be NULL if we couldn't find the match
           (strchr will return NULL if no match is found),
           or it should point to dup which contains the string
           with the random part masked. */
    }

  chksum = crc32_string (chksum, (ptr) ? ptr : string);

  if (dup)
      free (dup);

  return chksum;
}

/* Compute checksum for the current function.  We generate a CRC32.  */

static unsigned
compute_checksum (void)
{
  expanded_location xloc
    = expand_location (DECL_SOURCE_LOCATION (current_function_decl));
  unsigned chksum = xloc.line;

  chksum = coverage_checksum_string (chksum, xloc.file);
  chksum = coverage_checksum_string
    (chksum, IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (current_function_decl)));

  return chksum;
}

/* Begin output to the graph file for the current function.
   Opens the output file, if not already done. Writes the
   function header, if not already done. Returns nonzero if data
   should be output.  */

int
coverage_begin_output (void)
{
  if (no_coverage)
    return 0;

  if (!bbg_function_announced)
    {
      expanded_location xloc
	= expand_location (DECL_SOURCE_LOCATION (current_function_decl));
      unsigned long offset;

      if (!bbg_file_opened)
	{
	  if (!gcov_open (bbg_file_name, -1))
	    error ("cannot open %s", bbg_file_name);
	  else
	    {
	      gcov_write_unsigned (GCOV_NOTE_MAGIC);
	      gcov_write_unsigned (GCOV_VERSION);
	      gcov_write_unsigned (local_tick);
	    }
	  bbg_file_opened = 1;
	}

      /* Announce function */
      offset = gcov_write_tag (GCOV_TAG_FUNCTION);
      gcov_write_unsigned (current_function_funcdef_no + 1);
      gcov_write_unsigned (compute_checksum ());
      gcov_write_string (IDENTIFIER_POINTER
			 (DECL_ASSEMBLER_NAME (current_function_decl)));
      gcov_write_string (xloc.file);
      gcov_write_unsigned (xloc.line);
      gcov_write_length (offset);

      bbg_function_announced = 1;
    }
  return !gcov_is_error ();
}

/* Finish coverage data for the current function. Verify no output
   error has occurred.  Save function coverage counts.  */

void
coverage_end_function (void)
{
  unsigned i;

  if (bbg_file_opened > 1 && gcov_is_error ())
    {
      warning (0, "error writing %qs", bbg_file_name);
      bbg_file_opened = -1;
    }

  if (fn_ctr_mask)
    {
      struct function_list *item;

      item = XNEW (struct function_list);

      *functions_tail = item;
      functions_tail = &item->next;

      item->next = 0;
      item->ident = current_function_funcdef_no + 1;
      item->checksum = compute_checksum ();
      for (i = 0; i != GCOV_COUNTERS; i++)
	{
	  item->n_ctrs[i] = fn_n_ctrs[i];
	  prg_n_ctrs[i] += fn_n_ctrs[i];
	  fn_n_ctrs[i] = fn_b_ctrs[i] = 0;
	}
      prg_ctr_mask |= fn_ctr_mask;
      fn_ctr_mask = 0;
    }
  bbg_function_announced = 0;
}

/* Creates the gcov_fn_info RECORD_TYPE.  */

static tree
build_fn_info_type (unsigned int counters)
{
  tree type = lang_hooks.types.make_type (RECORD_TYPE);
  tree field, fields;
  tree array_type;

  /* ident */
  fields = build_decl (FIELD_DECL, NULL_TREE, get_gcov_unsigned_t ());

  /* checksum */
  field = build_decl (FIELD_DECL, NULL_TREE, get_gcov_unsigned_t ());
  TREE_CHAIN (field) = fields;
  fields = field;

  array_type = build_int_cst (NULL_TREE, counters - 1);
  array_type = build_index_type (array_type);
  array_type = build_array_type (get_gcov_unsigned_t (), array_type);

  /* counters */
  field = build_decl (FIELD_DECL, NULL_TREE, array_type);
  TREE_CHAIN (field) = fields;
  fields = field;

  finish_builtin_struct (type, "__gcov_fn_info", fields, NULL_TREE);

  return type;
}

/* Creates a CONSTRUCTOR for a gcov_fn_info. FUNCTION is
   the function being processed and TYPE is the gcov_fn_info
   RECORD_TYPE.  */

static tree
build_fn_info_value (const struct function_list *function, tree type)
{
  tree value = NULL_TREE;
  tree fields = TYPE_FIELDS (type);
  unsigned ix;
  tree array_value = NULL_TREE;

  /* ident */
  value = tree_cons (fields, build_int_cstu (get_gcov_unsigned_t (),
					     function->ident), value);
  fields = TREE_CHAIN (fields);

  /* checksum */
  value = tree_cons (fields, build_int_cstu (get_gcov_unsigned_t (),
					     function->checksum), value);
  fields = TREE_CHAIN (fields);

  /* counters */
  for (ix = 0; ix != GCOV_COUNTERS; ix++)
    if (prg_ctr_mask & (1 << ix))
      {
	tree counters = build_int_cstu (get_gcov_unsigned_t (),
					function->n_ctrs[ix]);

	array_value = tree_cons (NULL_TREE, counters, array_value);
      }

  /* FIXME: use build_constructor directly.  */
  array_value = build_constructor_from_list (TREE_TYPE (fields),
					     nreverse (array_value));
  value = tree_cons (fields, array_value, value);

  /* FIXME: use build_constructor directly.  */
  value = build_constructor_from_list (type, nreverse (value));

  return value;
}

/* Creates the gcov_ctr_info RECORD_TYPE.  */

static tree
build_ctr_info_type (void)
{
  tree type = lang_hooks.types.make_type (RECORD_TYPE);
  tree field, fields = NULL_TREE;
  tree gcov_ptr_type = build_pointer_type (get_gcov_type ());
  tree gcov_merge_fn_type;

  /* counters */
  field = build_decl (FIELD_DECL, NULL_TREE, get_gcov_unsigned_t ());
  TREE_CHAIN (field) = fields;
  fields = field;

  /* values */
  field = build_decl (FIELD_DECL, NULL_TREE, gcov_ptr_type);
  TREE_CHAIN (field) = fields;
  fields = field;

  /* merge */
  gcov_merge_fn_type =
    build_function_type_list (void_type_node,
			      gcov_ptr_type, get_gcov_unsigned_t (),
			      NULL_TREE);
  field = build_decl (FIELD_DECL, NULL_TREE,
		      build_pointer_type (gcov_merge_fn_type));
  TREE_CHAIN (field) = fields;
  fields = field;

  finish_builtin_struct (type, "__gcov_ctr_info", fields, NULL_TREE);

  return type;
}

/* Creates a CONSTRUCTOR for a gcov_ctr_info. COUNTER is
   the counter being processed and TYPE is the gcov_ctr_info
   RECORD_TYPE.  */

static tree
build_ctr_info_value (unsigned int counter, tree type)
{
  tree value = NULL_TREE;
  tree fields = TYPE_FIELDS (type);
  tree fn;

  /* counters */
  value = tree_cons (fields,
		     build_int_cstu (get_gcov_unsigned_t (),
				     prg_n_ctrs[counter]),
		     value);
  fields = TREE_CHAIN (fields);

  if (prg_n_ctrs[counter])
    {
      tree array_type;

      array_type = build_int_cstu (get_gcov_unsigned_t (),
				   prg_n_ctrs[counter] - 1);
      array_type = build_index_type (array_type);
      array_type = build_array_type (TREE_TYPE (TREE_TYPE (fields)),
				     array_type);

      TREE_TYPE (tree_ctr_tables[counter]) = array_type;
      DECL_SIZE (tree_ctr_tables[counter]) = TYPE_SIZE (array_type);
      DECL_SIZE_UNIT (tree_ctr_tables[counter]) = TYPE_SIZE_UNIT (array_type);
      assemble_variable (tree_ctr_tables[counter], 0, 0, 0);

      value = tree_cons (fields,
			 build1 (ADDR_EXPR, TREE_TYPE (fields), 
					    tree_ctr_tables[counter]),
			 value);
    }
  else
    value = tree_cons (fields, null_pointer_node, value);
  fields = TREE_CHAIN (fields);

  fn = build_decl (FUNCTION_DECL,
		   get_identifier (ctr_merge_functions[counter]),
		   TREE_TYPE (TREE_TYPE (fields)));
  DECL_EXTERNAL (fn) = 1;
  TREE_PUBLIC (fn) = 1;
  DECL_ARTIFICIAL (fn) = 1;
  TREE_NOTHROW (fn) = 1;
  value = tree_cons (fields,
		     build1 (ADDR_EXPR, TREE_TYPE (fields), fn),
		     value);

  /* FIXME: use build_constructor directly.  */
  value = build_constructor_from_list (type, nreverse (value));

  return value;
}

/* Creates the gcov_info RECORD_TYPE and initializer for it. Returns a
   CONSTRUCTOR.  */

static tree
build_gcov_info (void)
{
  unsigned n_ctr_types, ix;
  tree type, const_type;
  tree fn_info_type, fn_info_value = NULL_TREE;
  tree fn_info_ptr_type;
  tree ctr_info_type, ctr_info_ary_type, ctr_info_value = NULL_TREE;
  tree field, fields = NULL_TREE;
  tree value = NULL_TREE;
  tree filename_string;
  char *filename;
  int filename_len;
  unsigned n_fns;
  const struct function_list *fn;
  tree string_type;

  /* Count the number of active counters.  */
  for (n_ctr_types = 0, ix = 0; ix != GCOV_COUNTERS; ix++)
    if (prg_ctr_mask & (1 << ix))
      n_ctr_types++;

  type = lang_hooks.types.make_type (RECORD_TYPE);
  const_type = build_qualified_type (type, TYPE_QUAL_CONST);

  /* Version ident */
  field = build_decl (FIELD_DECL, NULL_TREE, get_gcov_unsigned_t ());
  TREE_CHAIN (field) = fields;
  fields = field;
  value = tree_cons (field, build_int_cstu (TREE_TYPE (field), GCOV_VERSION),
		     value);

  /* next -- NULL */
  field = build_decl (FIELD_DECL, NULL_TREE, build_pointer_type (const_type));
  TREE_CHAIN (field) = fields;
  fields = field;
  value = tree_cons (field, null_pointer_node, value);

  /* stamp */
  field = build_decl (FIELD_DECL, NULL_TREE, get_gcov_unsigned_t ());
  TREE_CHAIN (field) = fields;
  fields = field;
  value = tree_cons (field, build_int_cstu (TREE_TYPE (field), local_tick),
		     value);

  /* Filename */
  string_type = build_pointer_type (build_qualified_type (char_type_node,
						    TYPE_QUAL_CONST));
  field = build_decl (FIELD_DECL, NULL_TREE, string_type);
  TREE_CHAIN (field) = fields;
  fields = field;
  filename = getpwd ();
  filename = (filename && da_file_name[0] != '/'
	      ? concat (filename, "/", da_file_name, NULL)
	      : da_file_name);
  filename_len = strlen (filename);
  filename_string = build_string (filename_len + 1, filename);
  if (filename != da_file_name)
    free (filename);
  TREE_TYPE (filename_string) = build_array_type
    (char_type_node, build_index_type
     (build_int_cst (NULL_TREE, filename_len)));
  value = tree_cons (field, build1 (ADDR_EXPR, string_type, filename_string),
		     value);

  /* Build the fn_info type and initializer.  */
  fn_info_type = build_fn_info_type (n_ctr_types);
  fn_info_ptr_type = build_pointer_type (build_qualified_type
					 (fn_info_type, TYPE_QUAL_CONST));
  for (fn = functions_head, n_fns = 0; fn; fn = fn->next, n_fns++)
    fn_info_value = tree_cons (NULL_TREE,
			       build_fn_info_value (fn, fn_info_type),
			       fn_info_value);
  if (n_fns)
    {
      tree array_type;

      array_type = build_index_type (build_int_cst (NULL_TREE, n_fns - 1));
      array_type = build_array_type (fn_info_type, array_type);

      /* FIXME: use build_constructor directly.  */
      fn_info_value = build_constructor_from_list (array_type,
						   nreverse (fn_info_value));
      fn_info_value = build1 (ADDR_EXPR, fn_info_ptr_type, fn_info_value);
    }
  else
    fn_info_value = null_pointer_node;

  /* number of functions */
  field = build_decl (FIELD_DECL, NULL_TREE, get_gcov_unsigned_t ());
  TREE_CHAIN (field) = fields;
  fields = field;
  value = tree_cons (field,
		     build_int_cstu (get_gcov_unsigned_t (), n_fns),
		     value);

  /* fn_info table */
  field = build_decl (FIELD_DECL, NULL_TREE, fn_info_ptr_type);
  TREE_CHAIN (field) = fields;
  fields = field;
  value = tree_cons (field, fn_info_value, value);

  /* counter_mask */
  field = build_decl (FIELD_DECL, NULL_TREE, get_gcov_unsigned_t ());
  TREE_CHAIN (field) = fields;
  fields = field;
  value = tree_cons (field,
		     build_int_cstu (get_gcov_unsigned_t (), prg_ctr_mask),
		     value);

  /* counters */
  ctr_info_type = build_ctr_info_type ();
  ctr_info_ary_type = build_index_type (build_int_cst (NULL_TREE,
						       n_ctr_types));
  ctr_info_ary_type = build_array_type (ctr_info_type, ctr_info_ary_type);
  for (ix = 0; ix != GCOV_COUNTERS; ix++)
    if (prg_ctr_mask & (1 << ix))
      ctr_info_value = tree_cons (NULL_TREE,
				  build_ctr_info_value (ix, ctr_info_type),
				  ctr_info_value);
  /* FIXME: use build_constructor directly.  */
  ctr_info_value = build_constructor_from_list (ctr_info_ary_type,
				                nreverse (ctr_info_value));

  field = build_decl (FIELD_DECL, NULL_TREE, ctr_info_ary_type);
  TREE_CHAIN (field) = fields;
  fields = field;
  value = tree_cons (field, ctr_info_value, value);

  finish_builtin_struct (type, "__gcov_info", fields, NULL_TREE);

  /* FIXME: use build_constructor directly.  */
  value = build_constructor_from_list (type, nreverse (value));

  return value;
}

/* Write out the structure which libgcov uses to locate all the
   counters.  The structures used here must match those defined in
   gcov-io.h.  Write out the constructor to call __gcov_init.  */

static void
create_coverage (void)
{
  tree gcov_info, gcov_init, body, t;
  char name_buf[32];

  no_coverage = 1; /* Disable any further coverage.  */

  if (!prg_ctr_mask)
    return;

  t = build_gcov_info ();

  gcov_info = build_decl (VAR_DECL, NULL_TREE, TREE_TYPE (t));
  TREE_STATIC (gcov_info) = 1;
  ASM_GENERATE_INTERNAL_LABEL (name_buf, "LPBX", 0);
  DECL_NAME (gcov_info) = get_identifier (name_buf);
  DECL_INITIAL (gcov_info) = t;

  /* Build structure.  */
  assemble_variable (gcov_info, 0, 0, 0);

  /* Build a decl for __gcov_init.  */
  t = build_pointer_type (TREE_TYPE (gcov_info));
  t = build_function_type_list (void_type_node, t, NULL);
  t = build_decl (FUNCTION_DECL, get_identifier ("__gcov_init"), t);
  TREE_PUBLIC (t) = 1;
  DECL_EXTERNAL (t) = 1;
  gcov_init = t;

  /* Generate a call to __gcov_init(&gcov_info).  */
  body = NULL;
  t = build_fold_addr_expr (gcov_info);
  t = tree_cons (NULL, t, NULL);
  t = build_function_call_expr (gcov_init, t);
  append_to_statement_list (t, &body);

  /* Generate a constructor to run it.  */
  cgraph_build_static_cdtor ('I', body, DEFAULT_INIT_PRIORITY);
}

/* Perform file-level initialization. Read in data file, generate name
   of graph file.  */

void
coverage_init (const char *filename)
{
  int len = strlen (filename);

  /* Name of da file.  */
  da_file_name = XNEWVEC (char, len + strlen (GCOV_DATA_SUFFIX) + 1);
  strcpy (da_file_name, filename);
  strcat (da_file_name, GCOV_DATA_SUFFIX);

  /* Name of bbg file.  */
  bbg_file_name = XNEWVEC (char, len + strlen (GCOV_NOTE_SUFFIX) + 1);
  strcpy (bbg_file_name, filename);
  strcat (bbg_file_name, GCOV_NOTE_SUFFIX);

  read_counts_file ();
}

/* Performs file-level cleanup.  Close graph file, generate coverage
   variables and constructor.  */

void
coverage_finish (void)
{
  create_coverage ();
  if (bbg_file_opened)
    {
      int error = gcov_close ();

      if (error)
	unlink (bbg_file_name);
      if (!local_tick)
	/* Only remove the da file, if we cannot stamp it. If we can
	   stamp it, libgcov will DTRT.  */
	unlink (da_file_name);
    }
}

#include "gt-coverage.h"
