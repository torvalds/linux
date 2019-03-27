/* Linker command language support.
   Copyright 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000,
   2001, 2002, 2003, 2004, 2005, 2006, 2007
   Free Software Foundation, Inc.

   This file is part of GLD, the Gnu Linker.

   GLD is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GLD is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GLD; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#include "sysdep.h"
#include "bfd.h"
#include "libiberty.h"
#include "safe-ctype.h"
#include "obstack.h"
#include "bfdlink.h"

#include "ld.h"
#include "ldmain.h"
#include "ldexp.h"
#include "ldlang.h"
#include <ldgram.h>
#include "ldlex.h"
#include "ldmisc.h"
#include "ldctor.h"
#include "ldfile.h"
#include "ldemul.h"
#include "fnmatch.h"
#include "demangle.h"
#include "hashtab.h"

#ifndef offsetof
#define offsetof(TYPE, MEMBER) ((size_t) & (((TYPE*) 0)->MEMBER))
#endif

/* Locals variables.  */
static struct obstack stat_obstack;
static struct obstack map_obstack;

#define obstack_chunk_alloc xmalloc
#define obstack_chunk_free free
static const char *startup_file;
static bfd_boolean placed_commons = FALSE;
static bfd_boolean stripped_excluded_sections = FALSE;
static lang_output_section_statement_type *default_common_section;
static bfd_boolean map_option_f;
static bfd_vma print_dot;
static lang_input_statement_type *first_file;
static const char *current_target;
static const char *output_target;
static lang_statement_list_type statement_list;
static struct bfd_hash_table lang_definedness_table;

/* Forward declarations.  */
static void exp_init_os (etree_type *);
static void init_map_userdata (bfd *, asection *, void *);
static lang_input_statement_type *lookup_name (const char *);
static struct bfd_hash_entry *lang_definedness_newfunc
 (struct bfd_hash_entry *, struct bfd_hash_table *, const char *);
static void insert_undefined (const char *);
static bfd_boolean sort_def_symbol (struct bfd_link_hash_entry *, void *);
static void print_statement (lang_statement_union_type *,
			     lang_output_section_statement_type *);
static void print_statement_list (lang_statement_union_type *,
				  lang_output_section_statement_type *);
static void print_statements (void);
static void print_input_section (asection *);
static bfd_boolean lang_one_common (struct bfd_link_hash_entry *, void *);
static void lang_record_phdrs (void);
static void lang_do_version_exports_section (void);
static void lang_finalize_version_expr_head
  (struct bfd_elf_version_expr_head *);

/* Exported variables.  */
lang_output_section_statement_type *abs_output_section;
lang_statement_list_type lang_output_section_statement;
lang_statement_list_type *stat_ptr = &statement_list;
lang_statement_list_type file_chain = { NULL, NULL };
lang_statement_list_type input_file_chain;
struct bfd_sym_chain entry_symbol = { NULL, NULL };
static const char *entry_symbol_default = "start";
const char *entry_section = ".text";
bfd_boolean entry_from_cmdline;
bfd_boolean lang_has_input_file = FALSE;
bfd_boolean had_output_filename = FALSE;
bfd_boolean lang_float_flag = FALSE;
bfd_boolean delete_output_file_on_failure = FALSE;
struct lang_phdr *lang_phdr_list;
struct lang_nocrossrefs *nocrossref_list;
static struct unique_sections *unique_section_list;
static bfd_boolean ldlang_sysrooted_script = FALSE;

 /* Functions that traverse the linker script and might evaluate
    DEFINED() need to increment this.  */
int lang_statement_iteration = 0;

etree_type *base; /* Relocation base - or null */

/* Return TRUE if the PATTERN argument is a wildcard pattern.
   Although backslashes are treated specially if a pattern contains
   wildcards, we do not consider the mere presence of a backslash to
   be enough to cause the pattern to be treated as a wildcard.
   That lets us handle DOS filenames more naturally.  */
#define wildcardp(pattern) (strpbrk ((pattern), "?*[") != NULL)

#define new_stat(x, y) \
  (x##_type *) new_statement (x##_enum, sizeof (x##_type), y)

#define outside_section_address(q) \
  ((q)->output_offset + (q)->output_section->vma)

#define outside_symbol_address(q) \
  ((q)->value + outside_section_address (q->section))

#define SECTION_NAME_MAP_LENGTH (16)

void *
stat_alloc (size_t size)
{
  return obstack_alloc (&stat_obstack, size);
}

bfd_boolean
unique_section_p (const asection *sec)
{
  struct unique_sections *unam;
  const char *secnam;

  if (link_info.relocatable
      && sec->owner != NULL
      && bfd_is_group_section (sec->owner, sec))
    return TRUE;

  secnam = sec->name;
  for (unam = unique_section_list; unam; unam = unam->next)
    if (wildcardp (unam->name)
	? fnmatch (unam->name, secnam, 0) == 0
	: strcmp (unam->name, secnam) == 0)
      {
	return TRUE;
      }

  return FALSE;
}

/* Generic traversal routines for finding matching sections.  */

/* Try processing a section against a wildcard.  This just calls
   the callback unless the filename exclusion list is present
   and excludes the file.  It's hardly ever present so this
   function is very fast.  */

static void
walk_wild_consider_section (lang_wild_statement_type *ptr,
			    lang_input_statement_type *file,
			    asection *s,
			    struct wildcard_list *sec,
			    callback_t callback,
			    void *data)
{
  bfd_boolean skip = FALSE;
  struct name_list *list_tmp;

  /* Don't process sections from files which were
     excluded.  */
  for (list_tmp = sec->spec.exclude_name_list;
       list_tmp;
       list_tmp = list_tmp->next)
    {
      bfd_boolean is_wildcard = wildcardp (list_tmp->name);
      if (is_wildcard)
	skip = fnmatch (list_tmp->name, file->filename, 0) == 0;
      else
	skip = strcmp (list_tmp->name, file->filename) == 0;

      /* If this file is part of an archive, and the archive is
	 excluded, exclude this file.  */
      if (! skip && file->the_bfd != NULL
	  && file->the_bfd->my_archive != NULL
	  && file->the_bfd->my_archive->filename != NULL)
	{
	  if (is_wildcard)
	    skip = fnmatch (list_tmp->name,
			    file->the_bfd->my_archive->filename,
			    0) == 0;
	  else
	    skip = strcmp (list_tmp->name,
			   file->the_bfd->my_archive->filename) == 0;
	}

      if (skip)
	break;
    }

  if (!skip)
    (*callback) (ptr, sec, s, file, data);
}

/* Lowest common denominator routine that can handle everything correctly,
   but slowly.  */

static void
walk_wild_section_general (lang_wild_statement_type *ptr,
			   lang_input_statement_type *file,
			   callback_t callback,
			   void *data)
{
  asection *s;
  struct wildcard_list *sec;

  for (s = file->the_bfd->sections; s != NULL; s = s->next)
    {
      sec = ptr->section_list;
      if (sec == NULL)
	(*callback) (ptr, sec, s, file, data);

      while (sec != NULL)
	{
	  bfd_boolean skip = FALSE;

	  if (sec->spec.name != NULL)
	    {
	      const char *sname = bfd_get_section_name (file->the_bfd, s);

	      if (wildcardp (sec->spec.name))
		skip = fnmatch (sec->spec.name, sname, 0) != 0;
	      else
		skip = strcmp (sec->spec.name, sname) != 0;
	    }

	  if (!skip)
	    walk_wild_consider_section (ptr, file, s, sec, callback, data);

	  sec = sec->next;
	}
    }
}

/* Routines to find a single section given its name.  If there's more
   than one section with that name, we report that.  */

typedef struct
{
  asection *found_section;
  bfd_boolean multiple_sections_found;
} section_iterator_callback_data;

static bfd_boolean
section_iterator_callback (bfd *bfd ATTRIBUTE_UNUSED, asection *s, void *data)
{
  section_iterator_callback_data *d = data;

  if (d->found_section != NULL)
    {
      d->multiple_sections_found = TRUE;
      return TRUE;
    }

  d->found_section = s;
  return FALSE;
}

static asection *
find_section (lang_input_statement_type *file,
	      struct wildcard_list *sec,
	      bfd_boolean *multiple_sections_found)
{
  section_iterator_callback_data cb_data = { NULL, FALSE };

  bfd_get_section_by_name_if (file->the_bfd, sec->spec.name,
			      section_iterator_callback, &cb_data);
  *multiple_sections_found = cb_data.multiple_sections_found;
  return cb_data.found_section;
}

/* Code for handling simple wildcards without going through fnmatch,
   which can be expensive because of charset translations etc.  */

/* A simple wild is a literal string followed by a single '*',
   where the literal part is at least 4 characters long.  */

static bfd_boolean
is_simple_wild (const char *name)
{
  size_t len = strcspn (name, "*?[");
  return len >= 4 && name[len] == '*' && name[len + 1] == '\0';
}

static bfd_boolean
match_simple_wild (const char *pattern, const char *name)
{
  /* The first four characters of the pattern are guaranteed valid
     non-wildcard characters.  So we can go faster.  */
  if (pattern[0] != name[0] || pattern[1] != name[1]
      || pattern[2] != name[2] || pattern[3] != name[3])
    return FALSE;

  pattern += 4;
  name += 4;
  while (*pattern != '*')
    if (*name++ != *pattern++)
      return FALSE;

  return TRUE;
}

/* Compare sections ASEC and BSEC according to SORT.  */

static int
compare_section (sort_type sort, asection *asec, asection *bsec)
{
  int ret;

  switch (sort)
    {
    default:
      abort ();

    case by_alignment_name:
      ret = (bfd_section_alignment (bsec->owner, bsec)
	     - bfd_section_alignment (asec->owner, asec));
      if (ret)
	break;
      /* Fall through.  */

    case by_name:
      ret = strcmp (bfd_get_section_name (asec->owner, asec),
		    bfd_get_section_name (bsec->owner, bsec));
      break;

    case by_name_alignment:
      ret = strcmp (bfd_get_section_name (asec->owner, asec),
		    bfd_get_section_name (bsec->owner, bsec));
      if (ret)
	break;
      /* Fall through.  */

    case by_alignment:
      ret = (bfd_section_alignment (bsec->owner, bsec)
	     - bfd_section_alignment (asec->owner, asec));
      break;
    }

  return ret;
}

/* Build a Binary Search Tree to sort sections, unlike insertion sort
   used in wild_sort(). BST is considerably faster if the number of
   of sections are large.  */

static lang_section_bst_type **
wild_sort_fast (lang_wild_statement_type *wild,
		struct wildcard_list *sec,
		lang_input_statement_type *file ATTRIBUTE_UNUSED,
		asection *section)
{
  lang_section_bst_type **tree;

  tree = &wild->tree;
  if (!wild->filenames_sorted
      && (sec == NULL || sec->spec.sorted == none))
    {
      /* Append at the right end of tree.  */
      while (*tree)
	tree = &((*tree)->right);
      return tree;
    }

  while (*tree)
    {
      /* Find the correct node to append this section.  */
      if (compare_section (sec->spec.sorted, section, (*tree)->section) < 0)
	tree = &((*tree)->left);
      else
	tree = &((*tree)->right);
    }

  return tree;
}

/* Use wild_sort_fast to build a BST to sort sections.  */

static void
output_section_callback_fast (lang_wild_statement_type *ptr,
			      struct wildcard_list *sec,
			      asection *section,
			      lang_input_statement_type *file,
			      void *output ATTRIBUTE_UNUSED)
{
  lang_section_bst_type *node;
  lang_section_bst_type **tree;

  if (unique_section_p (section))
    return;

  node = xmalloc (sizeof (lang_section_bst_type));
  node->left = 0;
  node->right = 0;
  node->section = section;

  tree = wild_sort_fast (ptr, sec, file, section);
  if (tree != NULL)
    *tree = node;
}

/* Convert a sorted sections' BST back to list form.  */

static void
output_section_callback_tree_to_list (lang_wild_statement_type *ptr,
				      lang_section_bst_type *tree,
				      void *output)
{
  if (tree->left)
    output_section_callback_tree_to_list (ptr, tree->left, output);

  lang_add_section (&ptr->children, tree->section,
		    (lang_output_section_statement_type *) output);

  if (tree->right)
    output_section_callback_tree_to_list (ptr, tree->right, output);

  free (tree);
}

/* Specialized, optimized routines for handling different kinds of
   wildcards */

static void
walk_wild_section_specs1_wild0 (lang_wild_statement_type *ptr,
				lang_input_statement_type *file,
				callback_t callback,
				void *data)
{
  /* We can just do a hash lookup for the section with the right name.
     But if that lookup discovers more than one section with the name
     (should be rare), we fall back to the general algorithm because
     we would otherwise have to sort the sections to make sure they
     get processed in the bfd's order.  */
  bfd_boolean multiple_sections_found;
  struct wildcard_list *sec0 = ptr->handler_data[0];
  asection *s0 = find_section (file, sec0, &multiple_sections_found);

  if (multiple_sections_found)
    walk_wild_section_general (ptr, file, callback, data);
  else if (s0)
    walk_wild_consider_section (ptr, file, s0, sec0, callback, data);
}

static void
walk_wild_section_specs1_wild1 (lang_wild_statement_type *ptr,
				lang_input_statement_type *file,
				callback_t callback,
				void *data)
{
  asection *s;
  struct wildcard_list *wildsec0 = ptr->handler_data[0];

  for (s = file->the_bfd->sections; s != NULL; s = s->next)
    {
      const char *sname = bfd_get_section_name (file->the_bfd, s);
      bfd_boolean skip = !match_simple_wild (wildsec0->spec.name, sname);

      if (!skip)
	walk_wild_consider_section (ptr, file, s, wildsec0, callback, data);
    }
}

static void
walk_wild_section_specs2_wild1 (lang_wild_statement_type *ptr,
				lang_input_statement_type *file,
				callback_t callback,
				void *data)
{
  asection *s;
  struct wildcard_list *sec0 = ptr->handler_data[0];
  struct wildcard_list *wildsec1 = ptr->handler_data[1];
  bfd_boolean multiple_sections_found;
  asection *s0 = find_section (file, sec0, &multiple_sections_found);

  if (multiple_sections_found)
    {
      walk_wild_section_general (ptr, file, callback, data);
      return;
    }

  /* Note that if the section was not found, s0 is NULL and
     we'll simply never succeed the s == s0 test below.  */
  for (s = file->the_bfd->sections; s != NULL; s = s->next)
    {
      /* Recall that in this code path, a section cannot satisfy more
	 than one spec, so if s == s0 then it cannot match
	 wildspec1.  */
      if (s == s0)
	walk_wild_consider_section (ptr, file, s, sec0, callback, data);
      else
	{
	  const char *sname = bfd_get_section_name (file->the_bfd, s);
	  bfd_boolean skip = !match_simple_wild (wildsec1->spec.name, sname);

	  if (!skip)
	    walk_wild_consider_section (ptr, file, s, wildsec1, callback,
					data);
	}
    }
}

static void
walk_wild_section_specs3_wild2 (lang_wild_statement_type *ptr,
				lang_input_statement_type *file,
				callback_t callback,
				void *data)
{
  asection *s;
  struct wildcard_list *sec0 = ptr->handler_data[0];
  struct wildcard_list *wildsec1 = ptr->handler_data[1];
  struct wildcard_list *wildsec2 = ptr->handler_data[2];
  bfd_boolean multiple_sections_found;
  asection *s0 = find_section (file, sec0, &multiple_sections_found);

  if (multiple_sections_found)
    {
      walk_wild_section_general (ptr, file, callback, data);
      return;
    }

  for (s = file->the_bfd->sections; s != NULL; s = s->next)
    {
      if (s == s0)
	walk_wild_consider_section (ptr, file, s, sec0, callback, data);
      else
	{
	  const char *sname = bfd_get_section_name (file->the_bfd, s);
	  bfd_boolean skip = !match_simple_wild (wildsec1->spec.name, sname);

	  if (!skip)
	    walk_wild_consider_section (ptr, file, s, wildsec1, callback, data);
	  else
	    {
	      skip = !match_simple_wild (wildsec2->spec.name, sname);
	      if (!skip)
		walk_wild_consider_section (ptr, file, s, wildsec2, callback,
					    data);
	    }
	}
    }
}

static void
walk_wild_section_specs4_wild2 (lang_wild_statement_type *ptr,
				lang_input_statement_type *file,
				callback_t callback,
				void *data)
{
  asection *s;
  struct wildcard_list *sec0 = ptr->handler_data[0];
  struct wildcard_list *sec1 = ptr->handler_data[1];
  struct wildcard_list *wildsec2 = ptr->handler_data[2];
  struct wildcard_list *wildsec3 = ptr->handler_data[3];
  bfd_boolean multiple_sections_found;
  asection *s0 = find_section (file, sec0, &multiple_sections_found), *s1;

  if (multiple_sections_found)
    {
      walk_wild_section_general (ptr, file, callback, data);
      return;
    }

  s1 = find_section (file, sec1, &multiple_sections_found);
  if (multiple_sections_found)
    {
      walk_wild_section_general (ptr, file, callback, data);
      return;
    }

  for (s = file->the_bfd->sections; s != NULL; s = s->next)
    {
      if (s == s0)
	walk_wild_consider_section (ptr, file, s, sec0, callback, data);
      else
	if (s == s1)
	  walk_wild_consider_section (ptr, file, s, sec1, callback, data);
	else
	  {
	    const char *sname = bfd_get_section_name (file->the_bfd, s);
	    bfd_boolean skip = !match_simple_wild (wildsec2->spec.name,
						   sname);

	    if (!skip)
	      walk_wild_consider_section (ptr, file, s, wildsec2, callback,
					  data);
	    else
	      {
		skip = !match_simple_wild (wildsec3->spec.name, sname);
		if (!skip)
		  walk_wild_consider_section (ptr, file, s, wildsec3,
					      callback, data);
	      }
	  }
    }
}

static void
walk_wild_section (lang_wild_statement_type *ptr,
		   lang_input_statement_type *file,
		   callback_t callback,
		   void *data)
{
  if (file->just_syms_flag)
    return;

  (*ptr->walk_wild_section_handler) (ptr, file, callback, data);
}

/* Returns TRUE when name1 is a wildcard spec that might match
   something name2 can match.  We're conservative: we return FALSE
   only if the prefixes of name1 and name2 are different up to the
   first wildcard character.  */

static bfd_boolean
wild_spec_can_overlap (const char *name1, const char *name2)
{
  size_t prefix1_len = strcspn (name1, "?*[");
  size_t prefix2_len = strcspn (name2, "?*[");
  size_t min_prefix_len;

  /* Note that if there is no wildcard character, then we treat the
     terminating 0 as part of the prefix.  Thus ".text" won't match
     ".text." or ".text.*", for example.  */
  if (name1[prefix1_len] == '\0')
    prefix1_len++;
  if (name2[prefix2_len] == '\0')
    prefix2_len++;

  min_prefix_len = prefix1_len < prefix2_len ? prefix1_len : prefix2_len;

  return memcmp (name1, name2, min_prefix_len) == 0;
}

/* Select specialized code to handle various kinds of wildcard
   statements.  */

static void
analyze_walk_wild_section_handler (lang_wild_statement_type *ptr)
{
  int sec_count = 0;
  int wild_name_count = 0;
  struct wildcard_list *sec;
  int signature;
  int data_counter;

  ptr->walk_wild_section_handler = walk_wild_section_general;
  ptr->handler_data[0] = NULL;
  ptr->handler_data[1] = NULL;
  ptr->handler_data[2] = NULL;
  ptr->handler_data[3] = NULL;
  ptr->tree = NULL;

  /* Count how many wildcard_specs there are, and how many of those
     actually use wildcards in the name.  Also, bail out if any of the
     wildcard names are NULL. (Can this actually happen?
     walk_wild_section used to test for it.)  And bail out if any
     of the wildcards are more complex than a simple string
     ending in a single '*'.  */
  for (sec = ptr->section_list; sec != NULL; sec = sec->next)
    {
      ++sec_count;
      if (sec->spec.name == NULL)
	return;
      if (wildcardp (sec->spec.name))
	{
	  ++wild_name_count;
	  if (!is_simple_wild (sec->spec.name))
	    return;
	}
    }

  /* The zero-spec case would be easy to optimize but it doesn't
     happen in practice.  Likewise, more than 4 specs doesn't
     happen in practice.  */
  if (sec_count == 0 || sec_count > 4)
    return;

  /* Check that no two specs can match the same section.  */
  for (sec = ptr->section_list; sec != NULL; sec = sec->next)
    {
      struct wildcard_list *sec2;
      for (sec2 = sec->next; sec2 != NULL; sec2 = sec2->next)
	{
	  if (wild_spec_can_overlap (sec->spec.name, sec2->spec.name))
	    return;
	}
    }

  signature = (sec_count << 8) + wild_name_count;
  switch (signature)
    {
    case 0x0100:
      ptr->walk_wild_section_handler = walk_wild_section_specs1_wild0;
      break;
    case 0x0101:
      ptr->walk_wild_section_handler = walk_wild_section_specs1_wild1;
      break;
    case 0x0201:
      ptr->walk_wild_section_handler = walk_wild_section_specs2_wild1;
      break;
    case 0x0302:
      ptr->walk_wild_section_handler = walk_wild_section_specs3_wild2;
      break;
    case 0x0402:
      ptr->walk_wild_section_handler = walk_wild_section_specs4_wild2;
      break;
    default:
      return;
    }

  /* Now fill the data array with pointers to the specs, first the
     specs with non-wildcard names, then the specs with wildcard
     names.  It's OK to process the specs in different order from the
     given order, because we've already determined that no section
     will match more than one spec.  */
  data_counter = 0;
  for (sec = ptr->section_list; sec != NULL; sec = sec->next)
    if (!wildcardp (sec->spec.name))
      ptr->handler_data[data_counter++] = sec;
  for (sec = ptr->section_list; sec != NULL; sec = sec->next)
    if (wildcardp (sec->spec.name))
      ptr->handler_data[data_counter++] = sec;
}

/* Handle a wild statement for a single file F.  */

static void
walk_wild_file (lang_wild_statement_type *s,
		lang_input_statement_type *f,
		callback_t callback,
		void *data)
{
  if (f->the_bfd == NULL
      || ! bfd_check_format (f->the_bfd, bfd_archive))
    walk_wild_section (s, f, callback, data);
  else
    {
      bfd *member;

      /* This is an archive file.  We must map each member of the
	 archive separately.  */
      member = bfd_openr_next_archived_file (f->the_bfd, NULL);
      while (member != NULL)
	{
	  /* When lookup_name is called, it will call the add_symbols
	     entry point for the archive.  For each element of the
	     archive which is included, BFD will call ldlang_add_file,
	     which will set the usrdata field of the member to the
	     lang_input_statement.  */
	  if (member->usrdata != NULL)
	    {
	      walk_wild_section (s, member->usrdata, callback, data);
	    }

	  member = bfd_openr_next_archived_file (f->the_bfd, member);
	}
    }
}

static void
walk_wild (lang_wild_statement_type *s, callback_t callback, void *data)
{
  const char *file_spec = s->filename;

  if (file_spec == NULL)
    {
      /* Perform the iteration over all files in the list.  */
      LANG_FOR_EACH_INPUT_STATEMENT (f)
	{
	  walk_wild_file (s, f, callback, data);
	}
    }
  else if (wildcardp (file_spec))
    {
      LANG_FOR_EACH_INPUT_STATEMENT (f)
	{
	  if (fnmatch (file_spec, f->filename, 0) == 0)
	    walk_wild_file (s, f, callback, data);
	}
    }
  else
    {
      lang_input_statement_type *f;

      /* Perform the iteration over a single file.  */
      f = lookup_name (file_spec);
      if (f)
	walk_wild_file (s, f, callback, data);
    }
}

/* lang_for_each_statement walks the parse tree and calls the provided
   function for each node.  */

static void
lang_for_each_statement_worker (void (*func) (lang_statement_union_type *),
				lang_statement_union_type *s)
{
  for (; s != NULL; s = s->header.next)
    {
      func (s);

      switch (s->header.type)
	{
	case lang_constructors_statement_enum:
	  lang_for_each_statement_worker (func, constructor_list.head);
	  break;
	case lang_output_section_statement_enum:
	  lang_for_each_statement_worker
	    (func, s->output_section_statement.children.head);
	  break;
	case lang_wild_statement_enum:
	  lang_for_each_statement_worker (func,
					  s->wild_statement.children.head);
	  break;
	case lang_group_statement_enum:
	  lang_for_each_statement_worker (func,
					  s->group_statement.children.head);
	  break;
	case lang_data_statement_enum:
	case lang_reloc_statement_enum:
	case lang_object_symbols_statement_enum:
	case lang_output_statement_enum:
	case lang_target_statement_enum:
	case lang_input_section_enum:
	case lang_input_statement_enum:
	case lang_assignment_statement_enum:
	case lang_padding_statement_enum:
	case lang_address_statement_enum:
	case lang_fill_statement_enum:
	  break;
	default:
	  FAIL ();
	  break;
	}
    }
}

void
lang_for_each_statement (void (*func) (lang_statement_union_type *))
{
  lang_for_each_statement_worker (func, statement_list.head);
}

/*----------------------------------------------------------------------*/

void
lang_list_init (lang_statement_list_type *list)
{
  list->head = NULL;
  list->tail = &list->head;
}

/* Build a new statement node for the parse tree.  */

static lang_statement_union_type *
new_statement (enum statement_enum type,
	       size_t size,
	       lang_statement_list_type *list)
{
  lang_statement_union_type *new;

  new = stat_alloc (size);
  new->header.type = type;
  new->header.next = NULL;
  lang_statement_append (list, new, &new->header.next);
  return new;
}

/* Build a new input file node for the language.  There are several
   ways in which we treat an input file, eg, we only look at symbols,
   or prefix it with a -l etc.

   We can be supplied with requests for input files more than once;
   they may, for example be split over several lines like foo.o(.text)
   foo.o(.data) etc, so when asked for a file we check that we haven't
   got it already so we don't duplicate the bfd.  */

static lang_input_statement_type *
new_afile (const char *name,
	   lang_input_file_enum_type file_type,
	   const char *target,
	   bfd_boolean add_to_list)
{
  lang_input_statement_type *p;

  if (add_to_list)
    p = new_stat (lang_input_statement, stat_ptr);
  else
    {
      p = stat_alloc (sizeof (lang_input_statement_type));
      p->header.type = lang_input_statement_enum;
      p->header.next = NULL;
    }

  lang_has_input_file = TRUE;
  p->target = target;
  p->sysrooted = FALSE;

  if (file_type == lang_input_file_is_l_enum
      && name[0] == ':' && name[1] != '\0')
    {
      file_type = lang_input_file_is_search_file_enum;
      name = name + 1;
    }

  switch (file_type)
    {
    case lang_input_file_is_symbols_only_enum:
      p->filename = name;
      p->is_archive = FALSE;
      p->real = TRUE;
      p->local_sym_name = name;
      p->just_syms_flag = TRUE;
      p->search_dirs_flag = FALSE;
      break;
    case lang_input_file_is_fake_enum:
      p->filename = name;
      p->is_archive = FALSE;
      p->real = FALSE;
      p->local_sym_name = name;
      p->just_syms_flag = FALSE;
      p->search_dirs_flag = FALSE;
      break;
    case lang_input_file_is_l_enum:
      p->is_archive = TRUE;
      p->filename = name;
      p->real = TRUE;
      p->local_sym_name = concat ("-l", name, NULL);
      p->just_syms_flag = FALSE;
      p->search_dirs_flag = TRUE;
      break;
    case lang_input_file_is_marker_enum:
      p->filename = name;
      p->is_archive = FALSE;
      p->real = FALSE;
      p->local_sym_name = name;
      p->just_syms_flag = FALSE;
      p->search_dirs_flag = TRUE;
      break;
    case lang_input_file_is_search_file_enum:
      p->sysrooted = ldlang_sysrooted_script;
      p->filename = name;
      p->is_archive = FALSE;
      p->real = TRUE;
      p->local_sym_name = name;
      p->just_syms_flag = FALSE;
      p->search_dirs_flag = TRUE;
      break;
    case lang_input_file_is_file_enum:
      p->filename = name;
      p->is_archive = FALSE;
      p->real = TRUE;
      p->local_sym_name = name;
      p->just_syms_flag = FALSE;
      p->search_dirs_flag = FALSE;
      break;
    default:
      FAIL ();
    }
  p->the_bfd = NULL;
  p->asymbols = NULL;
  p->next_real_file = NULL;
  p->next = NULL;
  p->symbol_count = 0;
  p->dynamic = config.dynamic_link;
  p->add_needed = add_needed;
  p->as_needed = as_needed;
  p->whole_archive = whole_archive;
  p->loaded = FALSE;
  lang_statement_append (&input_file_chain,
			 (lang_statement_union_type *) p,
			 &p->next_real_file);
  return p;
}

lang_input_statement_type *
lang_add_input_file (const char *name,
		     lang_input_file_enum_type file_type,
		     const char *target)
{
  return new_afile (name, file_type, target, TRUE);
}

struct out_section_hash_entry
{
  struct bfd_hash_entry root;
  lang_statement_union_type s;
};

/* The hash table.  */

static struct bfd_hash_table output_section_statement_table;

/* Support routines for the hash table used by lang_output_section_find,
   initialize the table, fill in an entry and remove the table.  */

static struct bfd_hash_entry *
output_section_statement_newfunc (struct bfd_hash_entry *entry,
				  struct bfd_hash_table *table,
				  const char *string)
{
  lang_output_section_statement_type **nextp;
  struct out_section_hash_entry *ret;

  if (entry == NULL)
    {
      entry = bfd_hash_allocate (table, sizeof (*ret));
      if (entry == NULL)
	return entry;
    }

  entry = bfd_hash_newfunc (entry, table, string);
  if (entry == NULL)
    return entry;

  ret = (struct out_section_hash_entry *) entry;
  memset (&ret->s, 0, sizeof (ret->s));
  ret->s.header.type = lang_output_section_statement_enum;
  ret->s.output_section_statement.subsection_alignment = -1;
  ret->s.output_section_statement.section_alignment = -1;
  ret->s.output_section_statement.block_value = 1;
  lang_list_init (&ret->s.output_section_statement.children);
  lang_statement_append (stat_ptr, &ret->s, &ret->s.header.next);

  /* For every output section statement added to the list, except the
     first one, lang_output_section_statement.tail points to the "next"
     field of the last element of the list.  */
  if (lang_output_section_statement.head != NULL)
    ret->s.output_section_statement.prev
      = ((lang_output_section_statement_type *)
	 ((char *) lang_output_section_statement.tail
	  - offsetof (lang_output_section_statement_type, next)));

  /* GCC's strict aliasing rules prevent us from just casting the
     address, so we store the pointer in a variable and cast that
     instead.  */
  nextp = &ret->s.output_section_statement.next;
  lang_statement_append (&lang_output_section_statement,
			 &ret->s,
			 (lang_statement_union_type **) nextp);
  return &ret->root;
}

static void
output_section_statement_table_init (void)
{
  if (!bfd_hash_table_init_n (&output_section_statement_table,
			      output_section_statement_newfunc,
			      sizeof (struct out_section_hash_entry),
			      61))
    einfo (_("%P%F: can not create hash table: %E\n"));
}

static void
output_section_statement_table_free (void)
{
  bfd_hash_table_free (&output_section_statement_table);
}

/* Build enough state so that the parser can build its tree.  */

void
lang_init (void)
{
  obstack_begin (&stat_obstack, 1000);

  stat_ptr = &statement_list;

  output_section_statement_table_init ();

  lang_list_init (stat_ptr);

  lang_list_init (&input_file_chain);
  lang_list_init (&lang_output_section_statement);
  lang_list_init (&file_chain);
  first_file = lang_add_input_file (NULL, lang_input_file_is_marker_enum,
				    NULL);
  abs_output_section =
    lang_output_section_statement_lookup (BFD_ABS_SECTION_NAME);

  abs_output_section->bfd_section = bfd_abs_section_ptr;

  /* The value "3" is ad-hoc, somewhat related to the expected number of
     DEFINED expressions in a linker script.  For most default linker
     scripts, there are none.  Why a hash table then?  Well, it's somewhat
     simpler to re-use working machinery than using a linked list in terms
     of code-complexity here in ld, besides the initialization which just
     looks like other code here.  */
  if (!bfd_hash_table_init_n (&lang_definedness_table,
			      lang_definedness_newfunc,
			      sizeof (struct lang_definedness_hash_entry),
			      3))
    einfo (_("%P%F: can not create hash table: %E\n"));
}

void
lang_finish (void)
{
  output_section_statement_table_free ();
}

/*----------------------------------------------------------------------
  A region is an area of memory declared with the
  MEMORY {  name:org=exp, len=exp ... }
  syntax.

  We maintain a list of all the regions here.

  If no regions are specified in the script, then the default is used
  which is created when looked up to be the entire data space.

  If create is true we are creating a region inside a MEMORY block.
  In this case it is probably an error to create a region that has
  already been created.  If we are not inside a MEMORY block it is
  dubious to use an undeclared region name (except DEFAULT_MEMORY_REGION)
  and so we issue a warning.  */

static lang_memory_region_type *lang_memory_region_list;
static lang_memory_region_type **lang_memory_region_list_tail
  = &lang_memory_region_list;

lang_memory_region_type *
lang_memory_region_lookup (const char *const name, bfd_boolean create)
{
  lang_memory_region_type *p;
  lang_memory_region_type *new;

  /* NAME is NULL for LMA memspecs if no region was specified.  */
  if (name == NULL)
    return NULL;

  for (p = lang_memory_region_list; p != NULL; p = p->next)
    if (strcmp (p->name, name) == 0)
      {
	if (create)
	  einfo (_("%P:%S: warning: redeclaration of memory region '%s'\n"),
		 name);
	return p;
      }

  if (!create && strcmp (name, DEFAULT_MEMORY_REGION))
    einfo (_("%P:%S: warning: memory region %s not declared\n"), name);

  new = stat_alloc (sizeof (lang_memory_region_type));

  new->name = xstrdup (name);
  new->next = NULL;
  new->origin = 0;
  new->length = ~(bfd_size_type) 0;
  new->current = 0;
  new->last_os = NULL;
  new->flags = 0;
  new->not_flags = 0;
  new->had_full_message = FALSE;

  *lang_memory_region_list_tail = new;
  lang_memory_region_list_tail = &new->next;

  return new;
}

static lang_memory_region_type *
lang_memory_default (asection *section)
{
  lang_memory_region_type *p;

  flagword sec_flags = section->flags;

  /* Override SEC_DATA to mean a writable section.  */
  if ((sec_flags & (SEC_ALLOC | SEC_READONLY | SEC_CODE)) == SEC_ALLOC)
    sec_flags |= SEC_DATA;

  for (p = lang_memory_region_list; p != NULL; p = p->next)
    {
      if ((p->flags & sec_flags) != 0
	  && (p->not_flags & sec_flags) == 0)
	{
	  return p;
	}
    }
  return lang_memory_region_lookup (DEFAULT_MEMORY_REGION, FALSE);
}

lang_output_section_statement_type *
lang_output_section_find (const char *const name)
{
  struct out_section_hash_entry *entry;
  unsigned long hash;

  entry = ((struct out_section_hash_entry *)
	   bfd_hash_lookup (&output_section_statement_table, name,
			    FALSE, FALSE));
  if (entry == NULL)
    return NULL;

  hash = entry->root.hash;
  do
    {
      if (entry->s.output_section_statement.constraint != -1)
	return &entry->s.output_section_statement;
      entry = (struct out_section_hash_entry *) entry->root.next;
    }
  while (entry != NULL
	 && entry->root.hash == hash
	 && strcmp (name, entry->s.output_section_statement.name) == 0);

  return NULL;
}

static lang_output_section_statement_type *
lang_output_section_statement_lookup_1 (const char *const name, int constraint)
{
  struct out_section_hash_entry *entry;
  struct out_section_hash_entry *last_ent;
  unsigned long hash;

  entry = ((struct out_section_hash_entry *)
	   bfd_hash_lookup (&output_section_statement_table, name,
			    TRUE, FALSE));
  if (entry == NULL)
    {
      einfo (_("%P%F: failed creating section `%s': %E\n"), name);
      return NULL;
    }

  if (entry->s.output_section_statement.name != NULL)
    {
      /* We have a section of this name, but it might not have the correct
	 constraint.  */
      hash = entry->root.hash;
      do
	{
	  if (entry->s.output_section_statement.constraint != -1
	      && (constraint == 0
		  || (constraint == entry->s.output_section_statement.constraint
		      && constraint != SPECIAL)))
	    return &entry->s.output_section_statement;
	  last_ent = entry;
	  entry = (struct out_section_hash_entry *) entry->root.next;
	}
      while (entry != NULL
	     && entry->root.hash == hash
	     && strcmp (name, entry->s.output_section_statement.name) == 0);

      entry
	= ((struct out_section_hash_entry *)
	   output_section_statement_newfunc (NULL,
					     &output_section_statement_table,
					     name));
      if (entry == NULL)
	{
	  einfo (_("%P%F: failed creating section `%s': %E\n"), name);
	  return NULL;
	}
      entry->root = last_ent->root;
      last_ent->root.next = &entry->root;
    }

  entry->s.output_section_statement.name = name;
  entry->s.output_section_statement.constraint = constraint;
  return &entry->s.output_section_statement;
}

lang_output_section_statement_type *
lang_output_section_statement_lookup (const char *const name)
{
  return lang_output_section_statement_lookup_1 (name, 0);
}

/* A variant of lang_output_section_find used by place_orphan.
   Returns the output statement that should precede a new output
   statement for SEC.  If an exact match is found on certain flags,
   sets *EXACT too.  */

lang_output_section_statement_type *
lang_output_section_find_by_flags (const asection *sec,
				   lang_output_section_statement_type **exact,
				   lang_match_sec_type_func match_type)
{
  lang_output_section_statement_type *first, *look, *found;
  flagword flags;

  /* We know the first statement on this list is *ABS*.  May as well
     skip it.  */
  first = &lang_output_section_statement.head->output_section_statement;
  first = first->next;

  /* First try for an exact match.  */
  found = NULL;
  for (look = first; look; look = look->next)
    {
      flags = look->flags;
      if (look->bfd_section != NULL)
	{
	  flags = look->bfd_section->flags;
	  if (match_type && !match_type (output_bfd, look->bfd_section,
					 sec->owner, sec))
	    continue;
	}
      flags ^= sec->flags;
      if (!(flags & (SEC_HAS_CONTENTS | SEC_ALLOC | SEC_LOAD | SEC_READONLY
		     | SEC_CODE | SEC_SMALL_DATA | SEC_THREAD_LOCAL)))
	found = look;
    }
  if (found != NULL)
    {
      if (exact != NULL)
	*exact = found;
      return found;
    }

  if (sec->flags & SEC_CODE)
    {
      /* Try for a rw code section.  */
      for (look = first; look; look = look->next)
	{
	  flags = look->flags;
	  if (look->bfd_section != NULL)
	    {
	      flags = look->bfd_section->flags;
	      if (match_type && !match_type (output_bfd, look->bfd_section,
					     sec->owner, sec))
		continue;
	    }
	  flags ^= sec->flags;
	  if (!(flags & (SEC_HAS_CONTENTS | SEC_ALLOC | SEC_LOAD
			 | SEC_CODE | SEC_SMALL_DATA | SEC_THREAD_LOCAL)))
	    found = look;
	}
    }
  else if (sec->flags & (SEC_READONLY | SEC_THREAD_LOCAL))
    {
      /* .rodata can go after .text, .sdata2 after .rodata.  */
      for (look = first; look; look = look->next)
	{
	  flags = look->flags;
	  if (look->bfd_section != NULL)
	    {
	      flags = look->bfd_section->flags;
	      if (match_type && !match_type (output_bfd, look->bfd_section,
					     sec->owner, sec))
		continue;
	    }
	  flags ^= sec->flags;
	  if (!(flags & (SEC_HAS_CONTENTS | SEC_ALLOC | SEC_LOAD
			 | SEC_READONLY))
	      && !(look->flags & (SEC_SMALL_DATA | SEC_THREAD_LOCAL)))
	    found = look;
	}
    }
  else if (sec->flags & SEC_SMALL_DATA)
    {
      /* .sdata goes after .data, .sbss after .sdata.  */
      for (look = first; look; look = look->next)
	{
	  flags = look->flags;
	  if (look->bfd_section != NULL)
	    {
	      flags = look->bfd_section->flags;
	      if (match_type && !match_type (output_bfd, look->bfd_section,
					     sec->owner, sec))
		continue;
	    }
	  flags ^= sec->flags;
	  if (!(flags & (SEC_HAS_CONTENTS | SEC_ALLOC | SEC_LOAD
			 | SEC_THREAD_LOCAL))
	      || ((look->flags & SEC_SMALL_DATA)
		  && !(sec->flags & SEC_HAS_CONTENTS)))
	    found = look;
	}
    }
  else if (sec->flags & SEC_HAS_CONTENTS)
    {
      /* .data goes after .rodata.  */
      for (look = first; look; look = look->next)
	{
	  flags = look->flags;
	  if (look->bfd_section != NULL)
	    {
	      flags = look->bfd_section->flags;
	      if (match_type && !match_type (output_bfd, look->bfd_section,
					     sec->owner, sec))
		continue;
	    }
	  flags ^= sec->flags;
	  if (!(flags & (SEC_HAS_CONTENTS | SEC_ALLOC | SEC_LOAD
			 | SEC_SMALL_DATA | SEC_THREAD_LOCAL)))
	    found = look;
	}
    }
  else
    {
      /* .bss goes last.  */
      for (look = first; look; look = look->next)
	{
	  flags = look->flags;
	  if (look->bfd_section != NULL)
	    {
	      flags = look->bfd_section->flags;
	      if (match_type && !match_type (output_bfd, look->bfd_section,
					     sec->owner, sec))
		continue;
	    }
	  flags ^= sec->flags;
	  if (!(flags & SEC_ALLOC))
	    found = look;
	}
    }

  if (found || !match_type)
    return found;

  return lang_output_section_find_by_flags (sec, NULL, NULL);
}

/* Find the last output section before given output statement.
   Used by place_orphan.  */

static asection *
output_prev_sec_find (lang_output_section_statement_type *os)
{
  lang_output_section_statement_type *lookup;

  for (lookup = os->prev; lookup != NULL; lookup = lookup->prev)
    {
      if (lookup->constraint == -1)
	continue;

      if (lookup->bfd_section != NULL && lookup->bfd_section->owner != NULL)
	return lookup->bfd_section;
    }

  return NULL;
}

lang_output_section_statement_type *
lang_insert_orphan (asection *s,
		    const char *secname,
		    lang_output_section_statement_type *after,
		    struct orphan_save *place,
		    etree_type *address,
		    lang_statement_list_type *add_child)
{
  lang_statement_list_type *old;
  lang_statement_list_type add;
  const char *ps;
  lang_output_section_statement_type *os;
  lang_output_section_statement_type **os_tail;

  /* Start building a list of statements for this section.
     First save the current statement pointer.  */
  old = stat_ptr;

  /* If we have found an appropriate place for the output section
     statements for this orphan, add them to our own private list,
     inserting them later into the global statement list.  */
  if (after != NULL)
    {
      stat_ptr = &add;
      lang_list_init (stat_ptr);
    }

  if (link_info.relocatable || (s->flags & (SEC_LOAD | SEC_ALLOC)) == 0)
    address = exp_intop (0);

  os_tail = ((lang_output_section_statement_type **)
	     lang_output_section_statement.tail);
  os = lang_enter_output_section_statement (secname, address, 0, NULL, NULL,
					    NULL, 0);

  ps = NULL;
  if (config.build_constructors && *os_tail == os)
    {
      /* If the name of the section is representable in C, then create
	 symbols to mark the start and the end of the section.  */
      for (ps = secname; *ps != '\0'; ps++)
	if (! ISALNUM ((unsigned char) *ps) && *ps != '_')
	  break;
      if (*ps == '\0')
	{
	  char *symname;
	  etree_type *e_align;

	  symname = (char *) xmalloc (ps - secname + sizeof "__start_" + 1);
	  symname[0] = bfd_get_symbol_leading_char (output_bfd);
	  sprintf (symname + (symname[0] != 0), "__start_%s", secname);
	  e_align = exp_unop (ALIGN_K,
			      exp_intop ((bfd_vma) 1 << s->alignment_power));
	  lang_add_assignment (exp_assop ('=', ".", e_align));
	  lang_add_assignment (exp_provide (symname,
					    exp_unop (ABSOLUTE,
						      exp_nameop (NAME, ".")),
					    FALSE));
	}
    }

  if (add_child == NULL)
    add_child = &os->children;
  lang_add_section (add_child, s, os);

  lang_leave_output_section_statement (0, "*default*", NULL, NULL);

  if (ps != NULL && *ps == '\0')
    {
      char *symname;

      /* lang_leave_ouput_section_statement resets stat_ptr.
	 Put stat_ptr back where we want it.  */
      if (after != NULL)
	stat_ptr = &add;

      symname = (char *) xmalloc (ps - secname + sizeof "__stop_" + 1);
      symname[0] = bfd_get_symbol_leading_char (output_bfd);
      sprintf (symname + (symname[0] != 0), "__stop_%s", secname);
      lang_add_assignment (exp_provide (symname,
					exp_nameop (NAME, "."),
					FALSE));
    }

  /* Restore the global list pointer.  */
  if (after != NULL)
    stat_ptr = old;

  if (after != NULL && os->bfd_section != NULL)
    {
      asection *snew, *as;

      snew = os->bfd_section;

      /* Shuffle the bfd section list to make the output file look
	 neater.  This is really only cosmetic.  */
      if (place->section == NULL
	  && after != (&lang_output_section_statement.head
		       ->output_section_statement))
	{
	  asection *bfd_section = after->bfd_section;

	  /* If the output statement hasn't been used to place any input
	     sections (and thus doesn't have an output bfd_section),
	     look for the closest prior output statement having an
	     output section.  */
	  if (bfd_section == NULL)
	    bfd_section = output_prev_sec_find (after);

	  if (bfd_section != NULL && bfd_section != snew)
	    place->section = &bfd_section->next;
	}

      if (place->section == NULL)
	place->section = &output_bfd->sections;

      as = *place->section;

      if (!as)
	{
	  /* Put the section at the end of the list.  */

	  /* Unlink the section.  */
	  bfd_section_list_remove (output_bfd, snew);

	  /* Now tack it back on in the right place.  */
	  bfd_section_list_append (output_bfd, snew);
	}
      else if (as != snew && as->prev != snew)
	{
	  /* Unlink the section.  */
	  bfd_section_list_remove (output_bfd, snew);

	  /* Now tack it back on in the right place.  */
	  bfd_section_list_insert_before (output_bfd, as, snew);
	}

      /* Save the end of this list.  Further ophans of this type will
	 follow the one we've just added.  */
      place->section = &snew->next;

      /* The following is non-cosmetic.  We try to put the output
	 statements in some sort of reasonable order here, because they
	 determine the final load addresses of the orphan sections.
	 In addition, placing output statements in the wrong order may
	 require extra segments.  For instance, given a typical
	 situation of all read-only sections placed in one segment and
	 following that a segment containing all the read-write
	 sections, we wouldn't want to place an orphan read/write
	 section before or amongst the read-only ones.  */
      if (add.head != NULL)
	{
	  lang_output_section_statement_type *newly_added_os;

	  if (place->stmt == NULL)
	    {
	      lang_statement_union_type **where;
	      lang_statement_union_type **assign = NULL;
	      bfd_boolean ignore_first;

	      /* Look for a suitable place for the new statement list.
		 The idea is to skip over anything that might be inside
		 a SECTIONS {} statement in a script, before we find
		 another output_section_statement.  Assignments to "dot"
		 before an output section statement are assumed to
		 belong to it.  An exception to this rule is made for
		 the first assignment to dot, otherwise we might put an
		 orphan before . = . + SIZEOF_HEADERS or similar
		 assignments that set the initial address.  */

	      ignore_first = after == (&lang_output_section_statement.head
				       ->output_section_statement);
	      for (where = &after->header.next;
		   *where != NULL;
		   where = &(*where)->header.next)
		{
		  switch ((*where)->header.type)
		    {
		    case lang_assignment_statement_enum:
		      if (assign == NULL)
			{
			  lang_assignment_statement_type *ass;
			  ass = &(*where)->assignment_statement;
			  if (ass->exp->type.node_class != etree_assert
			      && ass->exp->assign.dst[0] == '.'
			      && ass->exp->assign.dst[1] == 0
			      && !ignore_first)
			    assign = where;
			}
		      ignore_first = FALSE;
		      continue;
		    case lang_wild_statement_enum:
		    case lang_input_section_enum:
		    case lang_object_symbols_statement_enum:
		    case lang_fill_statement_enum:
		    case lang_data_statement_enum:
		    case lang_reloc_statement_enum:
		    case lang_padding_statement_enum:
		    case lang_constructors_statement_enum:
		      assign = NULL;
		      continue;
		    case lang_output_section_statement_enum:
		      if (assign != NULL)
			where = assign;
		    case lang_input_statement_enum:
		    case lang_address_statement_enum:
		    case lang_target_statement_enum:
		    case lang_output_statement_enum:
		    case lang_group_statement_enum:
		    case lang_afile_asection_pair_statement_enum:
		      break;
		    }
		  break;
		}

	      *add.tail = *where;
	      *where = add.head;

	      place->os_tail = &after->next;
	    }
	  else
	    {
	      /* Put it after the last orphan statement we added.  */
	      *add.tail = *place->stmt;
	      *place->stmt = add.head;
	    }

	  /* Fix the global list pointer if we happened to tack our
	     new list at the tail.  */
	  if (*old->tail == add.head)
	    old->tail = add.tail;

	  /* Save the end of this list.  */
	  place->stmt = add.tail;

	  /* Do the same for the list of output section statements.  */
	  newly_added_os = *os_tail;
	  *os_tail = NULL;
	  newly_added_os->prev = (lang_output_section_statement_type *)
	    ((char *) place->os_tail
	     - offsetof (lang_output_section_statement_type, next));
	  newly_added_os->next = *place->os_tail;
	  if (newly_added_os->next != NULL)
	    newly_added_os->next->prev = newly_added_os;
	  *place->os_tail = newly_added_os;
	  place->os_tail = &newly_added_os->next;

	  /* Fixing the global list pointer here is a little different.
	     We added to the list in lang_enter_output_section_statement,
	     trimmed off the new output_section_statment above when
	     assigning *os_tail = NULL, but possibly added it back in
	     the same place when assigning *place->os_tail.  */
	  if (*os_tail == NULL)
	    lang_output_section_statement.tail
	      = (lang_statement_union_type **) os_tail;
	}
    }
  return os;
}

static void
lang_map_flags (flagword flag)
{
  if (flag & SEC_ALLOC)
    minfo ("a");

  if (flag & SEC_CODE)
    minfo ("x");

  if (flag & SEC_READONLY)
    minfo ("r");

  if (flag & SEC_DATA)
    minfo ("w");

  if (flag & SEC_LOAD)
    minfo ("l");
}

void
lang_map (void)
{
  lang_memory_region_type *m;
  bfd_boolean dis_header_printed = FALSE;
  bfd *p;

  LANG_FOR_EACH_INPUT_STATEMENT (file)
    {
      asection *s;

      if ((file->the_bfd->flags & (BFD_LINKER_CREATED | DYNAMIC)) != 0
	  || file->just_syms_flag)
	continue;

      for (s = file->the_bfd->sections; s != NULL; s = s->next)
	if ((s->output_section == NULL
	     || s->output_section->owner != output_bfd)
	    && (s->flags & (SEC_LINKER_CREATED | SEC_KEEP)) == 0)
	  {
	    if (! dis_header_printed)
	      {
		fprintf (config.map_file, _("\nDiscarded input sections\n\n"));
		dis_header_printed = TRUE;
	      }

	    print_input_section (s);
	  }
    }

  minfo (_("\nMemory Configuration\n\n"));
  fprintf (config.map_file, "%-16s %-18s %-18s %s\n",
	   _("Name"), _("Origin"), _("Length"), _("Attributes"));

  for (m = lang_memory_region_list; m != NULL; m = m->next)
    {
      char buf[100];
      int len;

      fprintf (config.map_file, "%-16s ", m->name);

      sprintf_vma (buf, m->origin);
      minfo ("0x%s ", buf);
      len = strlen (buf);
      while (len < 16)
	{
	  print_space ();
	  ++len;
	}

      minfo ("0x%V", m->length);
      if (m->flags || m->not_flags)
	{
#ifndef BFD64
	  minfo ("        ");
#endif
	  if (m->flags)
	    {
	      print_space ();
	      lang_map_flags (m->flags);
	    }

	  if (m->not_flags)
	    {
	      minfo (" !");
	      lang_map_flags (m->not_flags);
	    }
	}

      print_nl ();
    }

  fprintf (config.map_file, _("\nLinker script and memory map\n\n"));

  if (! link_info.reduce_memory_overheads)
    {
      obstack_begin (&map_obstack, 1000);
      for (p = link_info.input_bfds; p != (bfd *) NULL; p = p->link_next)
	bfd_map_over_sections (p, init_map_userdata, 0);
      bfd_link_hash_traverse (link_info.hash, sort_def_symbol, 0);
    }
  print_statements ();
}

static void
init_map_userdata (abfd, sec, data)
     bfd *abfd ATTRIBUTE_UNUSED;
     asection *sec;
     void *data ATTRIBUTE_UNUSED;
{
  fat_section_userdata_type *new_data
    = ((fat_section_userdata_type *) (stat_alloc
				      (sizeof (fat_section_userdata_type))));

  ASSERT (get_userdata (sec) == NULL);
  get_userdata (sec) = new_data;
  new_data->map_symbol_def_tail = &new_data->map_symbol_def_head;
}

static bfd_boolean
sort_def_symbol (hash_entry, info)
     struct bfd_link_hash_entry *hash_entry;
     void *info ATTRIBUTE_UNUSED;
{
  if (hash_entry->type == bfd_link_hash_defined
      || hash_entry->type == bfd_link_hash_defweak)
    {
      struct fat_user_section_struct *ud;
      struct map_symbol_def *def;

      ud = get_userdata (hash_entry->u.def.section);
      if  (! ud)
	{
	  /* ??? What do we have to do to initialize this beforehand?  */
	  /* The first time we get here is bfd_abs_section...  */
	  init_map_userdata (0, hash_entry->u.def.section, 0);
	  ud = get_userdata (hash_entry->u.def.section);
	}
      else if  (!ud->map_symbol_def_tail)
	ud->map_symbol_def_tail = &ud->map_symbol_def_head;

      def = obstack_alloc (&map_obstack, sizeof *def);
      def->entry = hash_entry;
      *(ud->map_symbol_def_tail) = def;
      ud->map_symbol_def_tail = &def->next;
    }
  return TRUE;
}

/* Initialize an output section.  */

static void
init_os (lang_output_section_statement_type *s, asection *isec,
	 flagword flags)
{
  if (s->bfd_section != NULL)
    return;

  if (strcmp (s->name, DISCARD_SECTION_NAME) == 0)
    einfo (_("%P%F: Illegal use of `%s' section\n"), DISCARD_SECTION_NAME);

  s->bfd_section = bfd_get_section_by_name (output_bfd, s->name);
  if (s->bfd_section == NULL)
    s->bfd_section = bfd_make_section_with_flags (output_bfd, s->name,
						  flags);
  if (s->bfd_section == NULL)
    {
      einfo (_("%P%F: output format %s cannot represent section called %s\n"),
	     output_bfd->xvec->name, s->name);
    }
  s->bfd_section->output_section = s->bfd_section;
  s->bfd_section->output_offset = 0;

  if (!link_info.reduce_memory_overheads)
    {
      fat_section_userdata_type *new
	= stat_alloc (sizeof (fat_section_userdata_type));
      memset (new, 0, sizeof (fat_section_userdata_type));
      get_userdata (s->bfd_section) = new;
    }

  /* If there is a base address, make sure that any sections it might
     mention are initialized.  */
  if (s->addr_tree != NULL)
    exp_init_os (s->addr_tree);

  if (s->load_base != NULL)
    exp_init_os (s->load_base);

  /* If supplied an alignment, set it.  */
  if (s->section_alignment != -1)
    s->bfd_section->alignment_power = s->section_alignment;

  if (isec)
    bfd_init_private_section_data (isec->owner, isec,
				   output_bfd, s->bfd_section,
				   &link_info);
}

/* Make sure that all output sections mentioned in an expression are
   initialized.  */

static void
exp_init_os (etree_type *exp)
{
  switch (exp->type.node_class)
    {
    case etree_assign:
    case etree_provide:
      exp_init_os (exp->assign.src);
      break;

    case etree_binary:
      exp_init_os (exp->binary.lhs);
      exp_init_os (exp->binary.rhs);
      break;

    case etree_trinary:
      exp_init_os (exp->trinary.cond);
      exp_init_os (exp->trinary.lhs);
      exp_init_os (exp->trinary.rhs);
      break;

    case etree_assert:
      exp_init_os (exp->assert_s.child);
      break;

    case etree_unary:
      exp_init_os (exp->unary.child);
      break;

    case etree_name:
      switch (exp->type.node_code)
	{
	case ADDR:
	case LOADADDR:
	case SIZEOF:
	  {
	    lang_output_section_statement_type *os;

	    os = lang_output_section_find (exp->name.name);
	    if (os != NULL && os->bfd_section == NULL)
	      init_os (os, NULL, 0);
	  }
	}
      break;

    default:
      break;
    }
}

static void
section_already_linked (bfd *abfd, asection *sec, void *data)
{
  lang_input_statement_type *entry = data;

  /* If we are only reading symbols from this object, then we want to
     discard all sections.  */
  if (entry->just_syms_flag)
    {
      bfd_link_just_syms (abfd, sec, &link_info);
      return;
    }

  if (!(abfd->flags & DYNAMIC))
    bfd_section_already_linked (abfd, sec, &link_info);
}

/* The wild routines.

   These expand statements like *(.text) and foo.o to a list of
   explicit actions, like foo.o(.text), bar.o(.text) and
   foo.o(.text, .data).  */

/* Add SECTION to the output section OUTPUT.  Do this by creating a
   lang_input_section statement which is placed at PTR.  FILE is the
   input file which holds SECTION.  */

void
lang_add_section (lang_statement_list_type *ptr,
		  asection *section,
		  lang_output_section_statement_type *output)
{
  flagword flags = section->flags;
  bfd_boolean discard;

  /* Discard sections marked with SEC_EXCLUDE.  */
  discard = (flags & SEC_EXCLUDE) != 0;

  /* Discard input sections which are assigned to a section named
     DISCARD_SECTION_NAME.  */
  if (strcmp (output->name, DISCARD_SECTION_NAME) == 0)
    discard = TRUE;

  /* Discard debugging sections if we are stripping debugging
     information.  */
  if ((link_info.strip == strip_debugger || link_info.strip == strip_all)
      && (flags & SEC_DEBUGGING) != 0)
    discard = TRUE;

  if (discard)
    {
      if (section->output_section == NULL)
	{
	  /* This prevents future calls from assigning this section.  */
	  section->output_section = bfd_abs_section_ptr;
	}
      return;
    }

  if (section->output_section == NULL)
    {
      bfd_boolean first;
      lang_input_section_type *new;
      flagword flags;

      flags = section->flags;

      /* We don't copy the SEC_NEVER_LOAD flag from an input section
	 to an output section, because we want to be able to include a
	 SEC_NEVER_LOAD section in the middle of an otherwise loaded
	 section (I don't know why we want to do this, but we do).
	 build_link_order in ldwrite.c handles this case by turning
	 the embedded SEC_NEVER_LOAD section into a fill.  */

      flags &= ~ SEC_NEVER_LOAD;

      switch (output->sectype)
	{
	case normal_section:
	case overlay_section:
	  break;
	case noalloc_section:
	  flags &= ~SEC_ALLOC;
	  break;
	case noload_section:
	  flags &= ~SEC_LOAD;
	  flags |= SEC_NEVER_LOAD;
	  break;
	}

      if (output->bfd_section == NULL)
	init_os (output, section, flags);

      first = ! output->bfd_section->linker_has_input;
      output->bfd_section->linker_has_input = 1;

      if (!link_info.relocatable
	  && !stripped_excluded_sections)
	{
	  asection *s = output->bfd_section->map_tail.s;
	  output->bfd_section->map_tail.s = section;
	  section->map_head.s = NULL;
	  section->map_tail.s = s;
	  if (s != NULL)
	    s->map_head.s = section;
	  else
	    output->bfd_section->map_head.s = section;
	}

      /* Add a section reference to the list.  */
      new = new_stat (lang_input_section, ptr);

      new->section = section;
      section->output_section = output->bfd_section;

      /* If final link, don't copy the SEC_LINK_ONCE flags, they've
	 already been processed.  One reason to do this is that on pe
	 format targets, .text$foo sections go into .text and it's odd
	 to see .text with SEC_LINK_ONCE set.  */

      if (! link_info.relocatable)
	flags &= ~ (SEC_LINK_ONCE | SEC_LINK_DUPLICATES);

      /* If this is not the first input section, and the SEC_READONLY
	 flag is not currently set, then don't set it just because the
	 input section has it set.  */

      if (! first && (output->bfd_section->flags & SEC_READONLY) == 0)
	flags &= ~ SEC_READONLY;

      /* Keep SEC_MERGE and SEC_STRINGS only if they are the same.  */
      if (! first
	  && ((output->bfd_section->flags & (SEC_MERGE | SEC_STRINGS))
	      != (flags & (SEC_MERGE | SEC_STRINGS))
	      || ((flags & SEC_MERGE)
		  && output->bfd_section->entsize != section->entsize)))
	{
	  output->bfd_section->flags &= ~ (SEC_MERGE | SEC_STRINGS);
	  flags &= ~ (SEC_MERGE | SEC_STRINGS);
	}

      output->bfd_section->flags |= flags;

      if (flags & SEC_MERGE)
	output->bfd_section->entsize = section->entsize;

      /* If SEC_READONLY is not set in the input section, then clear
	 it from the output section.  */
      if ((section->flags & SEC_READONLY) == 0)
	output->bfd_section->flags &= ~SEC_READONLY;

      /* Copy over SEC_SMALL_DATA.  */
      if (section->flags & SEC_SMALL_DATA)
	output->bfd_section->flags |= SEC_SMALL_DATA;

      if (section->alignment_power > output->bfd_section->alignment_power)
	output->bfd_section->alignment_power = section->alignment_power;

      if (bfd_get_arch (section->owner) == bfd_arch_tic54x
	  && (section->flags & SEC_TIC54X_BLOCK) != 0)
	{
	  output->bfd_section->flags |= SEC_TIC54X_BLOCK;
	  /* FIXME: This value should really be obtained from the bfd...  */
	  output->block_value = 128;
	}
    }
}

/* Handle wildcard sorting.  This returns the lang_input_section which
   should follow the one we are going to create for SECTION and FILE,
   based on the sorting requirements of WILD.  It returns NULL if the
   new section should just go at the end of the current list.  */

static lang_statement_union_type *
wild_sort (lang_wild_statement_type *wild,
	   struct wildcard_list *sec,
	   lang_input_statement_type *file,
	   asection *section)
{
  const char *section_name;
  lang_statement_union_type *l;

  if (!wild->filenames_sorted
      && (sec == NULL || sec->spec.sorted == none))
    return NULL;

  section_name = bfd_get_section_name (file->the_bfd, section);
  for (l = wild->children.head; l != NULL; l = l->header.next)
    {
      lang_input_section_type *ls;

      if (l->header.type != lang_input_section_enum)
	continue;
      ls = &l->input_section;

      /* Sorting by filename takes precedence over sorting by section
	 name.  */

      if (wild->filenames_sorted)
	{
	  const char *fn, *ln;
	  bfd_boolean fa, la;
	  int i;

	  /* The PE support for the .idata section as generated by
	     dlltool assumes that files will be sorted by the name of
	     the archive and then the name of the file within the
	     archive.  */

	  if (file->the_bfd != NULL
	      && bfd_my_archive (file->the_bfd) != NULL)
	    {
	      fn = bfd_get_filename (bfd_my_archive (file->the_bfd));
	      fa = TRUE;
	    }
	  else
	    {
	      fn = file->filename;
	      fa = FALSE;
	    }

	  if (bfd_my_archive (ls->section->owner) != NULL)
	    {
	      ln = bfd_get_filename (bfd_my_archive (ls->section->owner));
	      la = TRUE;
	    }
	  else
	    {
	      ln = ls->section->owner->filename;
	      la = FALSE;
	    }

	  i = strcmp (fn, ln);
	  if (i > 0)
	    continue;
	  else if (i < 0)
	    break;

	  if (fa || la)
	    {
	      if (fa)
		fn = file->filename;
	      if (la)
		ln = ls->section->owner->filename;

	      i = strcmp (fn, ln);
	      if (i > 0)
		continue;
	      else if (i < 0)
		break;
	    }
	}

      /* Here either the files are not sorted by name, or we are
	 looking at the sections for this file.  */

      if (sec != NULL && sec->spec.sorted != none)
	if (compare_section (sec->spec.sorted, section, ls->section) < 0)
	  break;
    }

  return l;
}

/* Expand a wild statement for a particular FILE.  SECTION may be
   NULL, in which case it is a wild card.  */

static void
output_section_callback (lang_wild_statement_type *ptr,
			 struct wildcard_list *sec,
			 asection *section,
			 lang_input_statement_type *file,
			 void *output)
{
  lang_statement_union_type *before;

  /* Exclude sections that match UNIQUE_SECTION_LIST.  */
  if (unique_section_p (section))
    return;

  before = wild_sort (ptr, sec, file, section);

  /* Here BEFORE points to the lang_input_section which
     should follow the one we are about to add.  If BEFORE
     is NULL, then the section should just go at the end
     of the current list.  */

  if (before == NULL)
    lang_add_section (&ptr->children, section,
		      (lang_output_section_statement_type *) output);
  else
    {
      lang_statement_list_type list;
      lang_statement_union_type **pp;

      lang_list_init (&list);
      lang_add_section (&list, section,
			(lang_output_section_statement_type *) output);

      /* If we are discarding the section, LIST.HEAD will
	 be NULL.  */
      if (list.head != NULL)
	{
	  ASSERT (list.head->header.next == NULL);

	  for (pp = &ptr->children.head;
	       *pp != before;
	       pp = &(*pp)->header.next)
	    ASSERT (*pp != NULL);

	  list.head->header.next = *pp;
	  *pp = list.head;
	}
    }
}

/* Check if all sections in a wild statement for a particular FILE
   are readonly.  */

static void
check_section_callback (lang_wild_statement_type *ptr ATTRIBUTE_UNUSED,
			struct wildcard_list *sec ATTRIBUTE_UNUSED,
			asection *section,
			lang_input_statement_type *file ATTRIBUTE_UNUSED,
			void *data)
{
  /* Exclude sections that match UNIQUE_SECTION_LIST.  */
  if (unique_section_p (section))
    return;

  if (section->output_section == NULL && (section->flags & SEC_READONLY) == 0)
    ((lang_output_section_statement_type *) data)->all_input_readonly = FALSE;
}

/* This is passed a file name which must have been seen already and
   added to the statement tree.  We will see if it has been opened
   already and had its symbols read.  If not then we'll read it.  */

static lang_input_statement_type *
lookup_name (const char *name)
{
  lang_input_statement_type *search;

  for (search = (lang_input_statement_type *) input_file_chain.head;
       search != NULL;
       search = (lang_input_statement_type *) search->next_real_file)
    {
      /* Use the local_sym_name as the name of the file that has
	 already been loaded as filename might have been transformed
	 via the search directory lookup mechanism.  */
      const char *filename = search->local_sym_name;

      if (filename != NULL
	  && strcmp (filename, name) == 0)
	break;
    }

  if (search == NULL)
    search = new_afile (name, lang_input_file_is_search_file_enum,
			default_target, FALSE);

  /* If we have already added this file, or this file is not real
     don't add this file.  */
  if (search->loaded || !search->real)
    return search;

  if (! load_symbols (search, NULL))
    return NULL;

  return search;
}

/* Save LIST as a list of libraries whose symbols should not be exported.  */

struct excluded_lib
{
  char *name;
  struct excluded_lib *next;
};
static struct excluded_lib *excluded_libs;

void
add_excluded_libs (const char *list)
{
  const char *p = list, *end;

  while (*p != '\0')
    {
      struct excluded_lib *entry;
      end = strpbrk (p, ",:");
      if (end == NULL)
	end = p + strlen (p);
      entry = xmalloc (sizeof (*entry));
      entry->next = excluded_libs;
      entry->name = xmalloc (end - p + 1);
      memcpy (entry->name, p, end - p);
      entry->name[end - p] = '\0';
      excluded_libs = entry;
      if (*end == '\0')
	break;
      p = end + 1;
    }
}

static void
check_excluded_libs (bfd *abfd)
{
  struct excluded_lib *lib = excluded_libs;

  while (lib)
    {
      int len = strlen (lib->name);
      const char *filename = lbasename (abfd->filename);

      if (strcmp (lib->name, "ALL") == 0)
	{
	  abfd->no_export = TRUE;
	  return;
	}

      if (strncmp (lib->name, filename, len) == 0
	  && (filename[len] == '\0'
	      || (filename[len] == '.' && filename[len + 1] == 'a'
		  && filename[len + 2] == '\0')))
	{
	  abfd->no_export = TRUE;
	  return;
	}

      lib = lib->next;
    }
}

/* Get the symbols for an input file.  */

bfd_boolean
load_symbols (lang_input_statement_type *entry,
	      lang_statement_list_type *place)
{
  char **matching;

  if (entry->loaded)
    return TRUE;

  ldfile_open_file (entry);

  if (! bfd_check_format (entry->the_bfd, bfd_archive)
      && ! bfd_check_format_matches (entry->the_bfd, bfd_object, &matching))
    {
      bfd_error_type err;
      lang_statement_list_type *hold;
      bfd_boolean bad_load = TRUE;
      bfd_boolean save_ldlang_sysrooted_script;
      bfd_boolean save_as_needed, save_add_needed;

      err = bfd_get_error ();

      /* See if the emulation has some special knowledge.  */
      if (ldemul_unrecognized_file (entry))
	return TRUE;

      if (err == bfd_error_file_ambiguously_recognized)
	{
	  char **p;

	  einfo (_("%B: file not recognized: %E\n"), entry->the_bfd);
	  einfo (_("%B: matching formats:"), entry->the_bfd);
	  for (p = matching; *p != NULL; p++)
	    einfo (" %s", *p);
	  einfo ("%F\n");
	}
      else if (err != bfd_error_file_not_recognized
	       || place == NULL)
	  einfo (_("%F%B: file not recognized: %E\n"), entry->the_bfd);
      else
	bad_load = FALSE;

      bfd_close (entry->the_bfd);
      entry->the_bfd = NULL;

      /* Try to interpret the file as a linker script.  */
      ldfile_open_command_file (entry->filename);

      hold = stat_ptr;
      stat_ptr = place;
      save_ldlang_sysrooted_script = ldlang_sysrooted_script;
      ldlang_sysrooted_script = entry->sysrooted;
      save_as_needed = as_needed;
      as_needed = entry->as_needed;
      save_add_needed = add_needed;
      add_needed = entry->add_needed;

      ldfile_assumed_script = TRUE;
      parser_input = input_script;
      /* We want to use the same -Bdynamic/-Bstatic as the one for
	 ENTRY.  */
      config.dynamic_link = entry->dynamic;
      yyparse ();
      ldfile_assumed_script = FALSE;

      ldlang_sysrooted_script = save_ldlang_sysrooted_script;
      as_needed = save_as_needed;
      add_needed = save_add_needed;
      stat_ptr = hold;

      return ! bad_load;
    }

  if (ldemul_recognized_file (entry))
    return TRUE;

  /* We don't call ldlang_add_file for an archive.  Instead, the
     add_symbols entry point will call ldlang_add_file, via the
     add_archive_element callback, for each element of the archive
     which is used.  */
  switch (bfd_get_format (entry->the_bfd))
    {
    default:
      break;

    case bfd_object:
      ldlang_add_file (entry);
      if (trace_files || trace_file_tries)
	info_msg ("%I\n", entry);
      break;

    case bfd_archive:
      check_excluded_libs (entry->the_bfd);

      if (entry->whole_archive)
	{
	  bfd *member = NULL;
	  bfd_boolean loaded = TRUE;

	  for (;;)
	    {
	      member = bfd_openr_next_archived_file (entry->the_bfd, member);

	      if (member == NULL)
		break;

	      if (! bfd_check_format (member, bfd_object))
		{
		  einfo (_("%F%B: member %B in archive is not an object\n"),
			 entry->the_bfd, member);
		  loaded = FALSE;
		}

	      if (! ((*link_info.callbacks->add_archive_element)
		     (&link_info, member, "--whole-archive")))
		abort ();

	      if (! bfd_link_add_symbols (member, &link_info))
		{
		  einfo (_("%F%B: could not read symbols: %E\n"), member);
		  loaded = FALSE;
		}
	    }

	  entry->loaded = loaded;
	  return loaded;
	}
      break;
    }

  if (bfd_link_add_symbols (entry->the_bfd, &link_info))
    entry->loaded = TRUE;
  else
    einfo (_("%F%B: could not read symbols: %E\n"), entry->the_bfd);

  return entry->loaded;
}

/* Handle a wild statement.  S->FILENAME or S->SECTION_LIST or both
   may be NULL, indicating that it is a wildcard.  Separate
   lang_input_section statements are created for each part of the
   expansion; they are added after the wild statement S.  OUTPUT is
   the output section.  */

static void
wild (lang_wild_statement_type *s,
      const char *target ATTRIBUTE_UNUSED,
      lang_output_section_statement_type *output)
{
  struct wildcard_list *sec;

  if (s->handler_data[0]
      && s->handler_data[0]->spec.sorted == by_name
      && !s->filenames_sorted)
    {
      lang_section_bst_type *tree;

      walk_wild (s, output_section_callback_fast, output);

      tree = s->tree;
      if (tree)
	{
	  output_section_callback_tree_to_list (s, tree, output);
	  s->tree = NULL;
	}
    }
  else
    walk_wild (s, output_section_callback, output);

  if (default_common_section == NULL)
    for (sec = s->section_list; sec != NULL; sec = sec->next)
      if (sec->spec.name != NULL && strcmp (sec->spec.name, "COMMON") == 0)
	{
	  /* Remember the section that common is going to in case we
	     later get something which doesn't know where to put it.  */
	  default_common_section = output;
	  break;
	}
}

/* Return TRUE iff target is the sought target.  */

static int
get_target (const bfd_target *target, void *data)
{
  const char *sought = data;

  return strcmp (target->name, sought) == 0;
}

/* Like strcpy() but convert to lower case as well.  */

static void
stricpy (char *dest, char *src)
{
  char c;

  while ((c = *src++) != 0)
    *dest++ = TOLOWER (c);

  *dest = 0;
}

/* Remove the first occurrence of needle (if any) in haystack
   from haystack.  */

static void
strcut (char *haystack, char *needle)
{
  haystack = strstr (haystack, needle);

  if (haystack)
    {
      char *src;

      for (src = haystack + strlen (needle); *src;)
	*haystack++ = *src++;

      *haystack = 0;
    }
}

/* Compare two target format name strings.
   Return a value indicating how "similar" they are.  */

static int
name_compare (char *first, char *second)
{
  char *copy1;
  char *copy2;
  int result;

  copy1 = xmalloc (strlen (first) + 1);
  copy2 = xmalloc (strlen (second) + 1);

  /* Convert the names to lower case.  */
  stricpy (copy1, first);
  stricpy (copy2, second);

  /* Remove size and endian strings from the name.  */
  strcut (copy1, "big");
  strcut (copy1, "little");
  strcut (copy2, "big");
  strcut (copy2, "little");

  /* Return a value based on how many characters match,
     starting from the beginning.   If both strings are
     the same then return 10 * their length.  */
  for (result = 0; copy1[result] == copy2[result]; result++)
    if (copy1[result] == 0)
      {
	result *= 10;
	break;
      }

  free (copy1);
  free (copy2);

  return result;
}

/* Set by closest_target_match() below.  */
static const bfd_target *winner;

/* Scan all the valid bfd targets looking for one that has the endianness
   requirement that was specified on the command line, and is the nearest
   match to the original output target.  */

static int
closest_target_match (const bfd_target *target, void *data)
{
  const bfd_target *original = data;

  if (command_line.endian == ENDIAN_BIG
      && target->byteorder != BFD_ENDIAN_BIG)
    return 0;

  if (command_line.endian == ENDIAN_LITTLE
      && target->byteorder != BFD_ENDIAN_LITTLE)
    return 0;

  /* Must be the same flavour.  */
  if (target->flavour != original->flavour)
    return 0;

  /* If we have not found a potential winner yet, then record this one.  */
  if (winner == NULL)
    {
      winner = target;
      return 0;
    }

  /* Oh dear, we now have two potential candidates for a successful match.
     Compare their names and choose the better one.  */
  if (name_compare (target->name, original->name)
      > name_compare (winner->name, original->name))
    winner = target;

  /* Keep on searching until wqe have checked them all.  */
  return 0;
}

/* Return the BFD target format of the first input file.  */

static char *
get_first_input_target (void)
{
  char *target = NULL;

  LANG_FOR_EACH_INPUT_STATEMENT (s)
    {
      if (s->header.type == lang_input_statement_enum
	  && s->real)
	{
	  ldfile_open_file (s);

	  if (s->the_bfd != NULL
	      && bfd_check_format (s->the_bfd, bfd_object))
	    {
	      target = bfd_get_target (s->the_bfd);

	      if (target != NULL)
		break;
	    }
	}
    }

  return target;
}

const char *
lang_get_output_target (void)
{
  const char *target;

  /* Has the user told us which output format to use?  */
  if (output_target != NULL)
    return output_target;

  /* No - has the current target been set to something other than
     the default?  */
  if (current_target != default_target)
    return current_target;

  /* No - can we determine the format of the first input file?  */
  target = get_first_input_target ();
  if (target != NULL)
    return target;

  /* Failed - use the default output target.  */
  return default_target;
}

/* Open the output file.  */

static bfd *
open_output (const char *name)
{
  bfd *output;

  output_target = lang_get_output_target ();

  /* Has the user requested a particular endianness on the command
     line?  */
  if (command_line.endian != ENDIAN_UNSET)
    {
      const bfd_target *target;
      enum bfd_endian desired_endian;

      /* Get the chosen target.  */
      target = bfd_search_for_target (get_target, (void *) output_target);

      /* If the target is not supported, we cannot do anything.  */
      if (target != NULL)
	{
	  if (command_line.endian == ENDIAN_BIG)
	    desired_endian = BFD_ENDIAN_BIG;
	  else
	    desired_endian = BFD_ENDIAN_LITTLE;

	  /* See if the target has the wrong endianness.  This should
	     not happen if the linker script has provided big and
	     little endian alternatives, but some scrips don't do
	     this.  */
	  if (target->byteorder != desired_endian)
	    {
	      /* If it does, then see if the target provides
		 an alternative with the correct endianness.  */
	      if (target->alternative_target != NULL
		  && (target->alternative_target->byteorder == desired_endian))
		output_target = target->alternative_target->name;
	      else
		{
		  /* Try to find a target as similar as possible to
		     the default target, but which has the desired
		     endian characteristic.  */
		  bfd_search_for_target (closest_target_match,
					 (void *) target);

		  /* Oh dear - we could not find any targets that
		     satisfy our requirements.  */
		  if (winner == NULL)
		    einfo (_("%P: warning: could not find any targets"
			     " that match endianness requirement\n"));
		  else
		    output_target = winner->name;
		}
	    }
	}
    }

  output = bfd_openw (name, output_target);

  if (output == NULL)
    {
      if (bfd_get_error () == bfd_error_invalid_target)
	einfo (_("%P%F: target %s not found\n"), output_target);

      einfo (_("%P%F: cannot open output file %s: %E\n"), name);
    }

  delete_output_file_on_failure = TRUE;

  if (! bfd_set_format (output, bfd_object))
    einfo (_("%P%F:%s: can not make object file: %E\n"), name);
  if (! bfd_set_arch_mach (output,
			   ldfile_output_architecture,
			   ldfile_output_machine))
    einfo (_("%P%F:%s: can not set architecture: %E\n"), name);

  link_info.hash = bfd_link_hash_table_create (output);
  if (link_info.hash == NULL)
    einfo (_("%P%F: can not create hash table: %E\n"));

  bfd_set_gp_size (output, g_switch_value);
  return output;
}

static void
ldlang_open_output (lang_statement_union_type *statement)
{
  switch (statement->header.type)
    {
    case lang_output_statement_enum:
      ASSERT (output_bfd == NULL);
      output_bfd = open_output (statement->output_statement.name);
      ldemul_set_output_arch ();
      if (config.magic_demand_paged && !link_info.relocatable)
	output_bfd->flags |= D_PAGED;
      else
	output_bfd->flags &= ~D_PAGED;
      if (config.text_read_only)
	output_bfd->flags |= WP_TEXT;
      else
	output_bfd->flags &= ~WP_TEXT;
      if (link_info.traditional_format)
	output_bfd->flags |= BFD_TRADITIONAL_FORMAT;
      else
	output_bfd->flags &= ~BFD_TRADITIONAL_FORMAT;
      break;

    case lang_target_statement_enum:
      current_target = statement->target_statement.target;
      break;
    default:
      break;
    }
}

/* Convert between addresses in bytes and sizes in octets.
   For currently supported targets, octets_per_byte is always a power
   of two, so we can use shifts.  */
#define TO_ADDR(X) ((X) >> opb_shift)
#define TO_SIZE(X) ((X) << opb_shift)

/* Support the above.  */
static unsigned int opb_shift = 0;

static void
init_opb (void)
{
  unsigned x = bfd_arch_mach_octets_per_byte (ldfile_output_architecture,
					      ldfile_output_machine);
  opb_shift = 0;
  if (x > 1)
    while ((x & 1) == 0)
      {
	x >>= 1;
	++opb_shift;
      }
  ASSERT (x == 1);
}

/* Open all the input files.  */

static void
open_input_bfds (lang_statement_union_type *s, bfd_boolean force)
{
  for (; s != NULL; s = s->header.next)
    {
      switch (s->header.type)
	{
	case lang_constructors_statement_enum:
	  open_input_bfds (constructor_list.head, force);
	  break;
	case lang_output_section_statement_enum:
	  open_input_bfds (s->output_section_statement.children.head, force);
	  break;
	case lang_wild_statement_enum:
	  /* Maybe we should load the file's symbols.  */
	  if (s->wild_statement.filename
	      && ! wildcardp (s->wild_statement.filename))
	    lookup_name (s->wild_statement.filename);
	  open_input_bfds (s->wild_statement.children.head, force);
	  break;
	case lang_group_statement_enum:
	  {
	    struct bfd_link_hash_entry *undefs;

	    /* We must continually search the entries in the group
	       until no new symbols are added to the list of undefined
	       symbols.  */

	    do
	      {
		undefs = link_info.hash->undefs_tail;
		open_input_bfds (s->group_statement.children.head, TRUE);
	      }
	    while (undefs != link_info.hash->undefs_tail);
	  }
	  break;
	case lang_target_statement_enum:
	  current_target = s->target_statement.target;
	  break;
	case lang_input_statement_enum:
	  if (s->input_statement.real)
	    {
	      lang_statement_list_type add;

	      s->input_statement.target = current_target;

	      /* If we are being called from within a group, and this
		 is an archive which has already been searched, then
		 force it to be researched unless the whole archive
		 has been loaded already.  */
	      if (force
		  && !s->input_statement.whole_archive
		  && s->input_statement.loaded
		  && bfd_check_format (s->input_statement.the_bfd,
				       bfd_archive))
		s->input_statement.loaded = FALSE;

	      lang_list_init (&add);

	      if (! load_symbols (&s->input_statement, &add))
		config.make_executable = FALSE;

	      if (add.head != NULL)
		{
		  *add.tail = s->header.next;
		  s->header.next = add.head;
		}
	    }
	  break;
	default:
	  break;
	}
    }
}

/* Add a symbol to a hash of symbols used in DEFINED (NAME) expressions.  */

void
lang_track_definedness (const char *name)
{
  if (bfd_hash_lookup (&lang_definedness_table, name, TRUE, FALSE) == NULL)
    einfo (_("%P%F: bfd_hash_lookup failed creating symbol %s\n"), name);
}

/* New-function for the definedness hash table.  */

static struct bfd_hash_entry *
lang_definedness_newfunc (struct bfd_hash_entry *entry,
			  struct bfd_hash_table *table ATTRIBUTE_UNUSED,
			  const char *name ATTRIBUTE_UNUSED)
{
  struct lang_definedness_hash_entry *ret
    = (struct lang_definedness_hash_entry *) entry;

  if (ret == NULL)
    ret = (struct lang_definedness_hash_entry *)
      bfd_hash_allocate (table, sizeof (struct lang_definedness_hash_entry));

  if (ret == NULL)
    einfo (_("%P%F: bfd_hash_allocate failed creating symbol %s\n"), name);

  ret->iteration = -1;
  return &ret->root;
}

/* Return the iteration when the definition of NAME was last updated.  A
   value of -1 means that the symbol is not defined in the linker script
   or the command line, but may be defined in the linker symbol table.  */

int
lang_symbol_definition_iteration (const char *name)
{
  struct lang_definedness_hash_entry *defentry
    = (struct lang_definedness_hash_entry *)
    bfd_hash_lookup (&lang_definedness_table, name, FALSE, FALSE);

  /* We've already created this one on the presence of DEFINED in the
     script, so it can't be NULL unless something is borked elsewhere in
     the code.  */
  if (defentry == NULL)
    FAIL ();

  return defentry->iteration;
}

/* Update the definedness state of NAME.  */

void
lang_update_definedness (const char *name, struct bfd_link_hash_entry *h)
{
  struct lang_definedness_hash_entry *defentry
    = (struct lang_definedness_hash_entry *)
    bfd_hash_lookup (&lang_definedness_table, name, FALSE, FALSE);

  /* We don't keep track of symbols not tested with DEFINED.  */
  if (defentry == NULL)
    return;

  /* If the symbol was already defined, and not from an earlier statement
     iteration, don't update the definedness iteration, because that'd
     make the symbol seem defined in the linker script at this point, and
     it wasn't; it was defined in some object.  If we do anyway, DEFINED
     would start to yield false before this point and the construct "sym =
     DEFINED (sym) ? sym : X;" would change sym to X despite being defined
     in an object.  */
  if (h->type != bfd_link_hash_undefined
      && h->type != bfd_link_hash_common
      && h->type != bfd_link_hash_new
      && defentry->iteration == -1)
    return;

  defentry->iteration = lang_statement_iteration;
}

/* Add the supplied name to the symbol table as an undefined reference.
   This is a two step process as the symbol table doesn't even exist at
   the time the ld command line is processed.  First we put the name
   on a list, then, once the output file has been opened, transfer the
   name to the symbol table.  */

typedef struct bfd_sym_chain ldlang_undef_chain_list_type;

#define ldlang_undef_chain_list_head entry_symbol.next

void
ldlang_add_undef (const char *const name)
{
  ldlang_undef_chain_list_type *new =
    stat_alloc (sizeof (ldlang_undef_chain_list_type));

  new->next = ldlang_undef_chain_list_head;
  ldlang_undef_chain_list_head = new;

  new->name = xstrdup (name);

  if (output_bfd != NULL)
    insert_undefined (new->name);
}

/* Insert NAME as undefined in the symbol table.  */

static void
insert_undefined (const char *name)
{
  struct bfd_link_hash_entry *h;

  h = bfd_link_hash_lookup (link_info.hash, name, TRUE, FALSE, TRUE);
  if (h == NULL)
    einfo (_("%P%F: bfd_link_hash_lookup failed: %E\n"));
  if (h->type == bfd_link_hash_new)
    {
      h->type = bfd_link_hash_undefined;
      h->u.undef.abfd = NULL;
      bfd_link_add_undef (link_info.hash, h);
    }
}

/* Run through the list of undefineds created above and place them
   into the linker hash table as undefined symbols belonging to the
   script file.  */

static void
lang_place_undefineds (void)
{
  ldlang_undef_chain_list_type *ptr;

  for (ptr = ldlang_undef_chain_list_head; ptr != NULL; ptr = ptr->next)
    insert_undefined (ptr->name);
}

/* Check for all readonly or some readwrite sections.  */

static void
check_input_sections
  (lang_statement_union_type *s,
   lang_output_section_statement_type *output_section_statement)
{
  for (; s != (lang_statement_union_type *) NULL; s = s->header.next)
    {
      switch (s->header.type)
      {
      case lang_wild_statement_enum:
	walk_wild (&s->wild_statement, check_section_callback,
		   output_section_statement);
	if (! output_section_statement->all_input_readonly)
	  return;
	break;
      case lang_constructors_statement_enum:
	check_input_sections (constructor_list.head,
			      output_section_statement);
	if (! output_section_statement->all_input_readonly)
	  return;
	break;
      case lang_group_statement_enum:
	check_input_sections (s->group_statement.children.head,
			      output_section_statement);
	if (! output_section_statement->all_input_readonly)
	  return;
	break;
      default:
	break;
      }
    }
}

/* Update wildcard statements if needed.  */

static void
update_wild_statements (lang_statement_union_type *s)
{
  struct wildcard_list *sec;

  switch (sort_section)
    {
    default:
      FAIL ();

    case none:
      break;

    case by_name:
    case by_alignment:
      for (; s != NULL; s = s->header.next)
	{
	  switch (s->header.type)
	    {
	    default:
	      break;

	    case lang_wild_statement_enum:
	      sec = s->wild_statement.section_list;
	      for (sec = s->wild_statement.section_list; sec != NULL;
		   sec = sec->next)
		{
		  switch (sec->spec.sorted)
		    {
		    case none:
		      sec->spec.sorted = sort_section;
		      break;
		    case by_name:
		      if (sort_section == by_alignment)
			sec->spec.sorted = by_name_alignment;
		      break;
		    case by_alignment:
		      if (sort_section == by_name)
			sec->spec.sorted = by_alignment_name;
		      break;
		    default:
		      break;
		    }
		}
	      break;

	    case lang_constructors_statement_enum:
	      update_wild_statements (constructor_list.head);
	      break;

	    case lang_output_section_statement_enum:
	      update_wild_statements
		(s->output_section_statement.children.head);
	      break;

	    case lang_group_statement_enum:
	      update_wild_statements (s->group_statement.children.head);
	      break;
	    }
	}
      break;
    }
}

/* Open input files and attach to output sections.  */

static void
map_input_to_output_sections
  (lang_statement_union_type *s, const char *target,
   lang_output_section_statement_type *os)
{
  flagword flags;

  for (; s != NULL; s = s->header.next)
    {
      switch (s->header.type)
	{
	case lang_wild_statement_enum:
	  wild (&s->wild_statement, target, os);
	  break;
	case lang_constructors_statement_enum:
	  map_input_to_output_sections (constructor_list.head,
					target,
					os);
	  break;
	case lang_output_section_statement_enum:
	  if (s->output_section_statement.constraint)
	    {
	      if (s->output_section_statement.constraint != ONLY_IF_RW
		  && s->output_section_statement.constraint != ONLY_IF_RO)
		break;
	      s->output_section_statement.all_input_readonly = TRUE;
	      check_input_sections (s->output_section_statement.children.head,
				    &s->output_section_statement);
	      if ((s->output_section_statement.all_input_readonly
		   && s->output_section_statement.constraint == ONLY_IF_RW)
		  || (!s->output_section_statement.all_input_readonly
		      && s->output_section_statement.constraint == ONLY_IF_RO))
		{
		  s->output_section_statement.constraint = -1;
		  break;
		}
	    }

	  map_input_to_output_sections (s->output_section_statement.children.head,
					target,
					&s->output_section_statement);
	  break;
	case lang_output_statement_enum:
	  break;
	case lang_target_statement_enum:
	  target = s->target_statement.target;
	  break;
	case lang_group_statement_enum:
	  map_input_to_output_sections (s->group_statement.children.head,
					target,
					os);
	  break;
	case lang_data_statement_enum:
	  /* Make sure that any sections mentioned in the expression
	     are initialized.  */
	  exp_init_os (s->data_statement.exp);
	  flags = SEC_HAS_CONTENTS;
	  /* The output section gets contents, and then we inspect for
	     any flags set in the input script which override any ALLOC.  */
	  if (!(os->flags & SEC_NEVER_LOAD))
	    flags |= SEC_ALLOC | SEC_LOAD;
	  if (os->bfd_section == NULL)
	    init_os (os, NULL, flags);
	  else
	    os->bfd_section->flags |= flags;
	  break;
	case lang_input_section_enum:
	  break;
	case lang_fill_statement_enum:
	case lang_object_symbols_statement_enum:
	case lang_reloc_statement_enum:
	case lang_padding_statement_enum:
	case lang_input_statement_enum:
	  if (os != NULL && os->bfd_section == NULL)
	    init_os (os, NULL, 0);
	  break;
	case lang_assignment_statement_enum:
	  if (os != NULL && os->bfd_section == NULL)
	    init_os (os, NULL, 0);

	  /* Make sure that any sections mentioned in the assignment
	     are initialized.  */
	  exp_init_os (s->assignment_statement.exp);
	  break;
	case lang_afile_asection_pair_statement_enum:
	  FAIL ();
	  break;
	case lang_address_statement_enum:
	  /* Mark the specified section with the supplied address.

	     If this section was actually a segment marker, then the
	     directive is ignored if the linker script explicitly
	     processed the segment marker.  Originally, the linker
	     treated segment directives (like -Ttext on the
	     command-line) as section directives.  We honor the
	     section directive semantics for backwards compatibilty;
	     linker scripts that do not specifically check for
	     SEGMENT_START automatically get the old semantics.  */
	  if (!s->address_statement.segment
	      || !s->address_statement.segment->used)
	    {
	      lang_output_section_statement_type *aos
		= (lang_output_section_statement_lookup
		   (s->address_statement.section_name));

	      if (aos->bfd_section == NULL)
		init_os (aos, NULL, 0);
	      aos->addr_tree = s->address_statement.address;
	    }
	  break;
	}
    }
}

/* An output section might have been removed after its statement was
   added.  For example, ldemul_before_allocation can remove dynamic
   sections if they turn out to be not needed.  Clean them up here.  */

void
strip_excluded_output_sections (void)
{
  lang_output_section_statement_type *os;

  /* Run lang_size_sections (if not already done).  */
  if (expld.phase != lang_mark_phase_enum)
    {
      expld.phase = lang_mark_phase_enum;
      expld.dataseg.phase = exp_dataseg_none;
      one_lang_size_sections_pass (NULL, FALSE);
      lang_reset_memory_regions ();
    }

  for (os = &lang_output_section_statement.head->output_section_statement;
       os != NULL;
       os = os->next)
    {
      asection *output_section;
      bfd_boolean exclude;

      if (os->constraint == -1)
	continue;

      output_section = os->bfd_section;
      if (output_section == NULL)
	continue;

      exclude = (output_section->rawsize == 0
		 && (output_section->flags & SEC_KEEP) == 0
		 && !bfd_section_removed_from_list (output_bfd,
						    output_section));

      /* Some sections have not yet been sized, notably .gnu.version,
	 .dynsym, .dynstr and .hash.  These all have SEC_LINKER_CREATED
	 input sections, so don't drop output sections that have such
	 input sections unless they are also marked SEC_EXCLUDE.  */
      if (exclude && output_section->map_head.s != NULL)
	{
	  asection *s;

	  for (s = output_section->map_head.s; s != NULL; s = s->map_head.s)
	    if ((s->flags & SEC_LINKER_CREATED) != 0
		&& (s->flags & SEC_EXCLUDE) == 0)
	      {
		exclude = FALSE;
		break;
	      }
	}

      /* TODO: Don't just junk map_head.s, turn them into link_orders.  */
      output_section->map_head.link_order = NULL;
      output_section->map_tail.link_order = NULL;

      if (exclude)
	{
	  /* We don't set bfd_section to NULL since bfd_section of the
	     removed output section statement may still be used.  */
	  if (!os->section_relative_symbol)
	    os->ignored = TRUE;
	  output_section->flags |= SEC_EXCLUDE;
	  bfd_section_list_remove (output_bfd, output_section);
	  output_bfd->section_count--;
	}
    }

  /* Stop future calls to lang_add_section from messing with map_head
     and map_tail link_order fields.  */
  stripped_excluded_sections = TRUE;
}

static void
print_output_section_statement
  (lang_output_section_statement_type *output_section_statement)
{
  asection *section = output_section_statement->bfd_section;
  int len;

  if (output_section_statement != abs_output_section)
    {
      minfo ("\n%s", output_section_statement->name);

      if (section != NULL)
	{
	  print_dot = section->vma;

	  len = strlen (output_section_statement->name);
	  if (len >= SECTION_NAME_MAP_LENGTH - 1)
	    {
	      print_nl ();
	      len = 0;
	    }
	  while (len < SECTION_NAME_MAP_LENGTH)
	    {
	      print_space ();
	      ++len;
	    }

	  minfo ("0x%V %W", section->vma, section->size);

	  if (section->vma != section->lma)
	    minfo (_(" load address 0x%V"), section->lma);
	}

      print_nl ();
    }

  print_statement_list (output_section_statement->children.head,
			output_section_statement);
}

/* Scan for the use of the destination in the right hand side
   of an expression.  In such cases we will not compute the
   correct expression, since the value of DST that is used on
   the right hand side will be its final value, not its value
   just before this expression is evaluated.  */

static bfd_boolean
scan_for_self_assignment (const char * dst, etree_type * rhs)
{
  if (rhs == NULL || dst == NULL)
    return FALSE;

  switch (rhs->type.node_class)
    {
    case etree_binary:
      return scan_for_self_assignment (dst, rhs->binary.lhs)
	||   scan_for_self_assignment (dst, rhs->binary.rhs);

    case etree_trinary:
      return scan_for_self_assignment (dst, rhs->trinary.lhs)
	||   scan_for_self_assignment (dst, rhs->trinary.rhs);

    case etree_assign:
    case etree_provided:
    case etree_provide:
      if (strcmp (dst, rhs->assign.dst) == 0)
	return TRUE;
      return scan_for_self_assignment (dst, rhs->assign.src);

    case etree_unary:
      return scan_for_self_assignment (dst, rhs->unary.child);

    case etree_value:
      if (rhs->value.str)
	return strcmp (dst, rhs->value.str) == 0;
      return FALSE;

    case etree_name:
      if (rhs->name.name)
	return strcmp (dst, rhs->name.name) == 0;
      return FALSE;

    default:
      break;
    }

  return FALSE;
}


static void
print_assignment (lang_assignment_statement_type *assignment,
		  lang_output_section_statement_type *output_section)
{
  unsigned int i;
  bfd_boolean is_dot;
  bfd_boolean computation_is_valid = TRUE;
  etree_type *tree;

  for (i = 0; i < SECTION_NAME_MAP_LENGTH; i++)
    print_space ();

  if (assignment->exp->type.node_class == etree_assert)
    {
      is_dot = FALSE;
      tree = assignment->exp->assert_s.child;
      computation_is_valid = TRUE;
    }
  else
    {
      const char *dst = assignment->exp->assign.dst;

      is_dot = (dst[0] == '.' && dst[1] == 0);
      tree = assignment->exp->assign.src;
      computation_is_valid = is_dot || (scan_for_self_assignment (dst, tree) == FALSE);
    }

  exp_fold_tree (tree, output_section->bfd_section, &print_dot);
  if (expld.result.valid_p)
    {
      bfd_vma value;

      if (computation_is_valid)
	{
	  value = expld.result.value;

	  if (expld.result.section)
	    value += expld.result.section->vma;

	  minfo ("0x%V", value);
	  if (is_dot)
	    print_dot = value;
	}
      else
	{
	  struct bfd_link_hash_entry *h;

	  h = bfd_link_hash_lookup (link_info.hash, assignment->exp->assign.dst,
				    FALSE, FALSE, TRUE);
	  if (h)
	    {
	      value = h->u.def.value;

	      if (expld.result.section)
	      value += expld.result.section->vma;

	      minfo ("[0x%V]", value);
	    }
	  else
	    minfo ("[unresolved]");
	}
    }
  else
    {
      minfo ("*undef*   ");
#ifdef BFD64
      minfo ("        ");
#endif
    }

  minfo ("                ");
  exp_print_tree (assignment->exp);
  print_nl ();
}

static void
print_input_statement (lang_input_statement_type *statm)
{
  if (statm->filename != NULL)
    {
      fprintf (config.map_file, "LOAD %s\n", statm->filename);
    }
}

/* Print all symbols defined in a particular section.  This is called
   via bfd_link_hash_traverse, or by print_all_symbols.  */

static bfd_boolean
print_one_symbol (struct bfd_link_hash_entry *hash_entry, void *ptr)
{
  asection *sec = ptr;

  if ((hash_entry->type == bfd_link_hash_defined
       || hash_entry->type == bfd_link_hash_defweak)
      && sec == hash_entry->u.def.section)
    {
      int i;

      for (i = 0; i < SECTION_NAME_MAP_LENGTH; i++)
	print_space ();
      minfo ("0x%V   ",
	     (hash_entry->u.def.value
	      + hash_entry->u.def.section->output_offset
	      + hash_entry->u.def.section->output_section->vma));

      minfo ("             %T\n", hash_entry->root.string);
    }

  return TRUE;
}

static void
print_all_symbols (asection *sec)
{
  struct fat_user_section_struct *ud = get_userdata (sec);
  struct map_symbol_def *def;

  if (!ud)
    return;

  *ud->map_symbol_def_tail = 0;
  for (def = ud->map_symbol_def_head; def; def = def->next)
    print_one_symbol (def->entry, sec);
}

/* Print information about an input section to the map file.  */

static void
print_input_section (asection *i)
{
  bfd_size_type size = i->size;
  int len;
  bfd_vma addr;

  init_opb ();

  print_space ();
  minfo ("%s", i->name);

  len = 1 + strlen (i->name);
  if (len >= SECTION_NAME_MAP_LENGTH - 1)
    {
      print_nl ();
      len = 0;
    }
  while (len < SECTION_NAME_MAP_LENGTH)
    {
      print_space ();
      ++len;
    }

  if (i->output_section != NULL && i->output_section->owner == output_bfd)
    addr = i->output_section->vma + i->output_offset;
  else
    {
      addr = print_dot;
      size = 0;
    }

  minfo ("0x%V %W %B\n", addr, TO_ADDR (size), i->owner);

  if (size != i->rawsize && i->rawsize != 0)
    {
      len = SECTION_NAME_MAP_LENGTH + 3;
#ifdef BFD64
      len += 16;
#else
      len += 8;
#endif
      while (len > 0)
	{
	  print_space ();
	  --len;
	}

      minfo (_("%W (size before relaxing)\n"), i->rawsize);
    }

  if (i->output_section != NULL && i->output_section->owner == output_bfd)
    {
      if (link_info.reduce_memory_overheads)
	bfd_link_hash_traverse (link_info.hash, print_one_symbol, i);
      else
	print_all_symbols (i);

      print_dot = addr + TO_ADDR (size);
    }
}

static void
print_fill_statement (lang_fill_statement_type *fill)
{
  size_t size;
  unsigned char *p;
  fputs (" FILL mask 0x", config.map_file);
  for (p = fill->fill->data, size = fill->fill->size; size != 0; p++, size--)
    fprintf (config.map_file, "%02x", *p);
  fputs ("\n", config.map_file);
}

static void
print_data_statement (lang_data_statement_type *data)
{
  int i;
  bfd_vma addr;
  bfd_size_type size;
  const char *name;

  init_opb ();
  for (i = 0; i < SECTION_NAME_MAP_LENGTH; i++)
    print_space ();

  addr = data->output_offset;
  if (data->output_section != NULL)
    addr += data->output_section->vma;

  switch (data->type)
    {
    default:
      abort ();
    case BYTE:
      size = BYTE_SIZE;
      name = "BYTE";
      break;
    case SHORT:
      size = SHORT_SIZE;
      name = "SHORT";
      break;
    case LONG:
      size = LONG_SIZE;
      name = "LONG";
      break;
    case QUAD:
      size = QUAD_SIZE;
      name = "QUAD";
      break;
    case SQUAD:
      size = QUAD_SIZE;
      name = "SQUAD";
      break;
    }

  minfo ("0x%V %W %s 0x%v", addr, size, name, data->value);

  if (data->exp->type.node_class != etree_value)
    {
      print_space ();
      exp_print_tree (data->exp);
    }

  print_nl ();

  print_dot = addr + TO_ADDR (size);
}

/* Print an address statement.  These are generated by options like
   -Ttext.  */

static void
print_address_statement (lang_address_statement_type *address)
{
  minfo (_("Address of section %s set to "), address->section_name);
  exp_print_tree (address->address);
  print_nl ();
}

/* Print a reloc statement.  */

static void
print_reloc_statement (lang_reloc_statement_type *reloc)
{
  int i;
  bfd_vma addr;
  bfd_size_type size;

  init_opb ();
  for (i = 0; i < SECTION_NAME_MAP_LENGTH; i++)
    print_space ();

  addr = reloc->output_offset;
  if (reloc->output_section != NULL)
    addr += reloc->output_section->vma;

  size = bfd_get_reloc_size (reloc->howto);

  minfo ("0x%V %W RELOC %s ", addr, size, reloc->howto->name);

  if (reloc->name != NULL)
    minfo ("%s+", reloc->name);
  else
    minfo ("%s+", reloc->section->name);

  exp_print_tree (reloc->addend_exp);

  print_nl ();

  print_dot = addr + TO_ADDR (size);
}

static void
print_padding_statement (lang_padding_statement_type *s)
{
  int len;
  bfd_vma addr;

  init_opb ();
  minfo (" *fill*");

  len = sizeof " *fill*" - 1;
  while (len < SECTION_NAME_MAP_LENGTH)
    {
      print_space ();
      ++len;
    }

  addr = s->output_offset;
  if (s->output_section != NULL)
    addr += s->output_section->vma;
  minfo ("0x%V %W ", addr, (bfd_vma) s->size);

  if (s->fill->size != 0)
    {
      size_t size;
      unsigned char *p;
      for (p = s->fill->data, size = s->fill->size; size != 0; p++, size--)
	fprintf (config.map_file, "%02x", *p);
    }

  print_nl ();

  print_dot = addr + TO_ADDR (s->size);
}

static void
print_wild_statement (lang_wild_statement_type *w,
		      lang_output_section_statement_type *os)
{
  struct wildcard_list *sec;

  print_space ();

  if (w->filenames_sorted)
    minfo ("SORT(");
  if (w->filename != NULL)
    minfo ("%s", w->filename);
  else
    minfo ("*");
  if (w->filenames_sorted)
    minfo (")");

  minfo ("(");
  for (sec = w->section_list; sec; sec = sec->next)
    {
      if (sec->spec.sorted)
	minfo ("SORT(");
      if (sec->spec.exclude_name_list != NULL)
	{
	  name_list *tmp;
	  minfo ("EXCLUDE_FILE(%s", sec->spec.exclude_name_list->name);
	  for (tmp = sec->spec.exclude_name_list->next; tmp; tmp = tmp->next)
	    minfo (" %s", tmp->name);
	  minfo (") ");
	}
      if (sec->spec.name != NULL)
	minfo ("%s", sec->spec.name);
      else
	minfo ("*");
      if (sec->spec.sorted)
	minfo (")");
      if (sec->next)
	minfo (" ");
    }
  minfo (")");

  print_nl ();

  print_statement_list (w->children.head, os);
}

/* Print a group statement.  */

static void
print_group (lang_group_statement_type *s,
	     lang_output_section_statement_type *os)
{
  fprintf (config.map_file, "START GROUP\n");
  print_statement_list (s->children.head, os);
  fprintf (config.map_file, "END GROUP\n");
}

/* Print the list of statements in S.
   This can be called for any statement type.  */

static void
print_statement_list (lang_statement_union_type *s,
		      lang_output_section_statement_type *os)
{
  while (s != NULL)
    {
      print_statement (s, os);
      s = s->header.next;
    }
}

/* Print the first statement in statement list S.
   This can be called for any statement type.  */

static void
print_statement (lang_statement_union_type *s,
		 lang_output_section_statement_type *os)
{
  switch (s->header.type)
    {
    default:
      fprintf (config.map_file, _("Fail with %d\n"), s->header.type);
      FAIL ();
      break;
    case lang_constructors_statement_enum:
      if (constructor_list.head != NULL)
	{
	  if (constructors_sorted)
	    minfo (" SORT (CONSTRUCTORS)\n");
	  else
	    minfo (" CONSTRUCTORS\n");
	  print_statement_list (constructor_list.head, os);
	}
      break;
    case lang_wild_statement_enum:
      print_wild_statement (&s->wild_statement, os);
      break;
    case lang_address_statement_enum:
      print_address_statement (&s->address_statement);
      break;
    case lang_object_symbols_statement_enum:
      minfo (" CREATE_OBJECT_SYMBOLS\n");
      break;
    case lang_fill_statement_enum:
      print_fill_statement (&s->fill_statement);
      break;
    case lang_data_statement_enum:
      print_data_statement (&s->data_statement);
      break;
    case lang_reloc_statement_enum:
      print_reloc_statement (&s->reloc_statement);
      break;
    case lang_input_section_enum:
      print_input_section (s->input_section.section);
      break;
    case lang_padding_statement_enum:
      print_padding_statement (&s->padding_statement);
      break;
    case lang_output_section_statement_enum:
      print_output_section_statement (&s->output_section_statement);
      break;
    case lang_assignment_statement_enum:
      print_assignment (&s->assignment_statement, os);
      break;
    case lang_target_statement_enum:
      fprintf (config.map_file, "TARGET(%s)\n", s->target_statement.target);
      break;
    case lang_output_statement_enum:
      minfo ("OUTPUT(%s", s->output_statement.name);
      if (output_target != NULL)
	minfo (" %s", output_target);
      minfo (")\n");
      break;
    case lang_input_statement_enum:
      print_input_statement (&s->input_statement);
      break;
    case lang_group_statement_enum:
      print_group (&s->group_statement, os);
      break;
    case lang_afile_asection_pair_statement_enum:
      FAIL ();
      break;
    }
}

static void
print_statements (void)
{
  print_statement_list (statement_list.head, abs_output_section);
}

/* Print the first N statements in statement list S to STDERR.
   If N == 0, nothing is printed.
   If N < 0, the entire list is printed.
   Intended to be called from GDB.  */

void
dprint_statement (lang_statement_union_type *s, int n)
{
  FILE *map_save = config.map_file;

  config.map_file = stderr;

  if (n < 0)
    print_statement_list (s, abs_output_section);
  else
    {
      while (s && --n >= 0)
	{
	  print_statement (s, abs_output_section);
	  s = s->header.next;
	}
    }

  config.map_file = map_save;
}

static void
insert_pad (lang_statement_union_type **ptr,
	    fill_type *fill,
	    unsigned int alignment_needed,
	    asection *output_section,
	    bfd_vma dot)
{
  static fill_type zero_fill = { 1, { 0 } };
  lang_statement_union_type *pad = NULL;

  if (ptr != &statement_list.head)
    pad = ((lang_statement_union_type *)
	   ((char *) ptr - offsetof (lang_statement_union_type, header.next)));
  if (pad != NULL
      && pad->header.type == lang_padding_statement_enum
      && pad->padding_statement.output_section == output_section)
    {
      /* Use the existing pad statement.  */
    }
  else if ((pad = *ptr) != NULL
      && pad->header.type == lang_padding_statement_enum
      && pad->padding_statement.output_section == output_section)
    {
      /* Use the existing pad statement.  */
    }
  else
    {
      /* Make a new padding statement, linked into existing chain.  */
      pad = stat_alloc (sizeof (lang_padding_statement_type));
      pad->header.next = *ptr;
      *ptr = pad;
      pad->header.type = lang_padding_statement_enum;
      pad->padding_statement.output_section = output_section;
      if (fill == NULL)
	fill = &zero_fill;
      pad->padding_statement.fill = fill;
    }
  pad->padding_statement.output_offset = dot - output_section->vma;
  pad->padding_statement.size = alignment_needed;
  output_section->size += alignment_needed;
}

/* Work out how much this section will move the dot point.  */

static bfd_vma
size_input_section
  (lang_statement_union_type **this_ptr,
   lang_output_section_statement_type *output_section_statement,
   fill_type *fill,
   bfd_vma dot)
{
  lang_input_section_type *is = &((*this_ptr)->input_section);
  asection *i = is->section;

  if (!((lang_input_statement_type *) i->owner->usrdata)->just_syms_flag
      && (i->flags & SEC_EXCLUDE) == 0)
    {
      unsigned int alignment_needed;
      asection *o;

      /* Align this section first to the input sections requirement,
	 then to the output section's requirement.  If this alignment
	 is greater than any seen before, then record it too.  Perform
	 the alignment by inserting a magic 'padding' statement.  */

      if (output_section_statement->subsection_alignment != -1)
	i->alignment_power = output_section_statement->subsection_alignment;

      o = output_section_statement->bfd_section;
      if (o->alignment_power < i->alignment_power)
	o->alignment_power = i->alignment_power;

      alignment_needed = align_power (dot, i->alignment_power) - dot;

      if (alignment_needed != 0)
	{
	  insert_pad (this_ptr, fill, TO_SIZE (alignment_needed), o, dot);
	  dot += alignment_needed;
	}

      /* Remember where in the output section this input section goes.  */

      i->output_offset = dot - o->vma;

      /* Mark how big the output section must be to contain this now.  */
      dot += TO_ADDR (i->size);
      o->size = TO_SIZE (dot - o->vma);
    }
  else
    {
      i->output_offset = i->vma - output_section_statement->bfd_section->vma;
    }

  return dot;
}

static int
sort_sections_by_lma (const void *arg1, const void *arg2)
{
  const asection *sec1 = *(const asection **) arg1;
  const asection *sec2 = *(const asection **) arg2;

  if (bfd_section_lma (sec1->owner, sec1)
      < bfd_section_lma (sec2->owner, sec2))
    return -1;
  else if (bfd_section_lma (sec1->owner, sec1)
	   > bfd_section_lma (sec2->owner, sec2))
    return 1;

  return 0;
}

#define IGNORE_SECTION(s) \
  ((s->flags & SEC_NEVER_LOAD) != 0				\
   || (s->flags & SEC_ALLOC) == 0				\
   || ((s->flags & SEC_THREAD_LOCAL) != 0			\
	&& (s->flags & SEC_LOAD) == 0))

/* Check to see if any allocated sections overlap with other allocated
   sections.  This can happen if a linker script specifies the output
   section addresses of the two sections.  */

static void
lang_check_section_addresses (void)
{
  asection *s, *os;
  asection **sections, **spp;
  unsigned int count;
  bfd_vma s_start;
  bfd_vma s_end;
  bfd_vma os_start;
  bfd_vma os_end;
  bfd_size_type amt;

  if (bfd_count_sections (output_bfd) <= 1)
    return;

  amt = bfd_count_sections (output_bfd) * sizeof (asection *);
  sections = xmalloc (amt);

  /* Scan all sections in the output list.  */
  count = 0;
  for (s = output_bfd->sections; s != NULL; s = s->next)
    {
      /* Only consider loadable sections with real contents.  */
      if (IGNORE_SECTION (s) || s->size == 0)
	continue;

      sections[count] = s;
      count++;
    }

  if (count <= 1)
    return;

  qsort (sections, (size_t) count, sizeof (asection *),
	 sort_sections_by_lma);

  spp = sections;
  s = *spp++;
  s_start = bfd_section_lma (output_bfd, s);
  s_end = s_start + TO_ADDR (s->size) - 1;
  for (count--; count; count--)
    {
      /* We must check the sections' LMA addresses not their VMA
	 addresses because overlay sections can have overlapping VMAs
	 but they must have distinct LMAs.  */
      os = s;
      os_start = s_start;
      os_end = s_end;
      s = *spp++;
      s_start = bfd_section_lma (output_bfd, s);
      s_end = s_start + TO_ADDR (s->size) - 1;

      /* Look for an overlap.  */
      if (s_end >= os_start && s_start <= os_end)
	einfo (_("%X%P: section %s [%V -> %V] overlaps section %s [%V -> %V]\n"),
	       s->name, s_start, s_end, os->name, os_start, os_end);
    }

  free (sections);
}

/* Make sure the new address is within the region.  We explicitly permit the
   current address to be at the exact end of the region when the address is
   non-zero, in case the region is at the end of addressable memory and the
   calculation wraps around.  */

static void
os_region_check (lang_output_section_statement_type *os,
		 lang_memory_region_type *region,
		 etree_type *tree,
		 bfd_vma base)
{
  if ((region->current < region->origin
       || (region->current - region->origin > region->length))
      && ((region->current != region->origin + region->length)
	  || base == 0))
    {
      if (tree != NULL)
	{
	  einfo (_("%X%P: address 0x%v of %B section %s"
		   " is not within region %s\n"),
		 region->current,
		 os->bfd_section->owner,
		 os->bfd_section->name,
		 region->name);
	}
      else
	{
	  einfo (_("%X%P: region %s is full (%B section %s)\n"),
		 region->name,
		 os->bfd_section->owner,
		 os->bfd_section->name);
	}
      /* Reset the region pointer.  */
      region->current = region->origin;
    }
}

/* Set the sizes for all the output sections.  */

static bfd_vma
lang_size_sections_1
  (lang_statement_union_type *s,
   lang_output_section_statement_type *output_section_statement,
   lang_statement_union_type **prev,
   fill_type *fill,
   bfd_vma dot,
   bfd_boolean *relax,
   bfd_boolean check_regions)
{
  /* Size up the sections from their constituent parts.  */
  for (; s != NULL; s = s->header.next)
    {
      switch (s->header.type)
	{
	case lang_output_section_statement_enum:
	  {
	    bfd_vma newdot, after;
	    lang_output_section_statement_type *os;
	    lang_memory_region_type *r;

	    os = &s->output_section_statement;
	    if (os->addr_tree != NULL)
	      {
		os->processed_vma = FALSE;
		exp_fold_tree (os->addr_tree, bfd_abs_section_ptr, &dot);

		if (!expld.result.valid_p
		    && expld.phase != lang_mark_phase_enum)
		  einfo (_("%F%S: non constant or forward reference"
			   " address expression for section %s\n"),
			 os->name);

		dot = expld.result.value + expld.result.section->vma;
	      }

	    if (os->bfd_section == NULL)
	      /* This section was removed or never actually created.  */
	      break;

	    /* If this is a COFF shared library section, use the size and
	       address from the input section.  FIXME: This is COFF
	       specific; it would be cleaner if there were some other way
	       to do this, but nothing simple comes to mind.  */
	    if ((bfd_get_flavour (output_bfd) == bfd_target_ecoff_flavour
		 || bfd_get_flavour (output_bfd) == bfd_target_coff_flavour)
		&& (os->bfd_section->flags & SEC_COFF_SHARED_LIBRARY) != 0)
	      {
		asection *input;

		if (os->children.head == NULL
		    || os->children.head->header.next != NULL
		    || (os->children.head->header.type
			!= lang_input_section_enum))
		  einfo (_("%P%X: Internal error on COFF shared library"
			   " section %s\n"), os->name);

		input = os->children.head->input_section.section;
		(void) bfd_set_section_vma (os->bfd_section->owner,
					    os->bfd_section,
					    bfd_section_vma (input->owner,
							     input));
		os->bfd_section->size = input->size;
		break;
	      }

	    newdot = dot;
	    if (bfd_is_abs_section (os->bfd_section))
	      {
		/* No matter what happens, an abs section starts at zero.  */
		ASSERT (os->bfd_section->vma == 0);
	      }
	    else
	      {
		int align;

		if (os->addr_tree == NULL)
		  {
		    /* No address specified for this section, get one
		       from the region specification.  */
		    if (os->region == NULL
			|| ((os->bfd_section->flags & (SEC_ALLOC | SEC_LOAD))
			    && os->region->name[0] == '*'
			    && strcmp (os->region->name,
				       DEFAULT_MEMORY_REGION) == 0))
		      {
			os->region = lang_memory_default (os->bfd_section);
		      }

		    /* If a loadable section is using the default memory
		       region, and some non default memory regions were
		       defined, issue an error message.  */
		    if (!os->ignored
			&& !IGNORE_SECTION (os->bfd_section)
			&& ! link_info.relocatable
			&& check_regions
			&& strcmp (os->region->name,
				   DEFAULT_MEMORY_REGION) == 0
			&& lang_memory_region_list != NULL
			&& (strcmp (lang_memory_region_list->name,
				    DEFAULT_MEMORY_REGION) != 0
			    || lang_memory_region_list->next != NULL)
			&& expld.phase != lang_mark_phase_enum)
		      {
			/* By default this is an error rather than just a
			   warning because if we allocate the section to the
			   default memory region we can end up creating an
			   excessively large binary, or even seg faulting when
			   attempting to perform a negative seek.  See
			   sources.redhat.com/ml/binutils/2003-04/msg00423.html
			   for an example of this.  This behaviour can be
			   overridden by the using the --no-check-sections
			   switch.  */
			if (command_line.check_section_addresses)
			  einfo (_("%P%F: error: no memory region specified"
				   " for loadable section `%s'\n"),
				 bfd_get_section_name (output_bfd,
						       os->bfd_section));
			else
			  einfo (_("%P: warning: no memory region specified"
				   " for loadable section `%s'\n"),
				 bfd_get_section_name (output_bfd,
						       os->bfd_section));
		      }

		    newdot = os->region->current;
		    align = os->bfd_section->alignment_power;
		  }
		else
		  align = os->section_alignment;

		/* Align to what the section needs.  */
		if (align > 0)
		  {
		    bfd_vma savedot = newdot;
		    newdot = align_power (newdot, align);

		    if (newdot != savedot
			&& (config.warn_section_align
			    || os->addr_tree != NULL)
			&& expld.phase != lang_mark_phase_enum)
		      einfo (_("%P: warning: changing start of section"
			       " %s by %lu bytes\n"),
			     os->name, (unsigned long) (newdot - savedot));
		  }

		(void) bfd_set_section_vma (0, os->bfd_section, newdot);

		os->bfd_section->output_offset = 0;
	      }

	    lang_size_sections_1 (os->children.head, os, &os->children.head,
				  os->fill, newdot, relax, check_regions);

	    os->processed_vma = TRUE;

	    if (bfd_is_abs_section (os->bfd_section) || os->ignored)
	      /* Except for some special linker created sections,
		 no output section should change from zero size
		 after strip_excluded_output_sections.  A non-zero
		 size on an ignored section indicates that some
		 input section was not sized early enough.  */
	      ASSERT (os->bfd_section->size == 0);
	    else
	      {
		dot = os->bfd_section->vma;

		/* Put the section within the requested block size, or
		   align at the block boundary.  */
		after = ((dot
			  + TO_ADDR (os->bfd_section->size)
			  + os->block_value - 1)
			 & - (bfd_vma) os->block_value);

		os->bfd_section->size = TO_SIZE (after - os->bfd_section->vma);
	      }

	    /* Set section lma.  */
	    r = os->region;
	    if (r == NULL)
	      r = lang_memory_region_lookup (DEFAULT_MEMORY_REGION, FALSE);

	    if (os->load_base)
	      {
		bfd_vma lma = exp_get_abs_int (os->load_base, 0, "load base");
		os->bfd_section->lma = lma;
	      }
	    else if (os->region != NULL
		     && os->lma_region != NULL
		     && os->lma_region != os->region)
	      {
		bfd_vma lma = os->lma_region->current;

		if (os->section_alignment != -1)
		  lma = align_power (lma, os->section_alignment);
		os->bfd_section->lma = lma;
	      }
	    else if (r->last_os != NULL
		     && (os->bfd_section->flags & SEC_ALLOC) != 0)
	      {
		bfd_vma lma;
		asection *last;

		last = r->last_os->output_section_statement.bfd_section;

		/* A backwards move of dot should be accompanied by
		   an explicit assignment to the section LMA (ie.
		   os->load_base set) because backwards moves can
		   create overlapping LMAs.  */
		if (dot < last->vma
		    && os->bfd_section->size != 0
		    && dot + os->bfd_section->size <= last->vma)
		  {
		    /* If dot moved backwards then leave lma equal to
		       vma.  This is the old default lma, which might
		       just happen to work when the backwards move is
		       sufficiently large.  Nag if this changes anything,
		       so people can fix their linker scripts.  */

		    if (last->vma != last->lma)
		      einfo (_("%P: warning: dot moved backwards before `%s'\n"),
			     os->name);
		  }
		else
		  {
		    /* If this is an overlay, set the current lma to that
		       at the end of the previous section.  */
		    if (os->sectype == overlay_section)
		      lma = last->lma + last->size;

		    /* Otherwise, keep the same lma to vma relationship
		       as the previous section.  */
		    else
		      lma = dot + last->lma - last->vma;

		    if (os->section_alignment != -1)
		      lma = align_power (lma, os->section_alignment);
		    os->bfd_section->lma = lma;
		  }
	      }
	    os->processed_lma = TRUE;

	    if (bfd_is_abs_section (os->bfd_section) || os->ignored)
	      break;

	    /* Keep track of normal sections using the default
	       lma region.  We use this to set the lma for
	       following sections.  Overlays or other linker
	       script assignment to lma might mean that the
	       default lma == vma is incorrect.
	       To avoid warnings about dot moving backwards when using
	       -Ttext, don't start tracking sections until we find one
	       of non-zero size or with lma set differently to vma.  */
	    if (((os->bfd_section->flags & SEC_HAS_CONTENTS) != 0
		 || (os->bfd_section->flags & SEC_THREAD_LOCAL) == 0)
		&& (os->bfd_section->flags & SEC_ALLOC) != 0
		&& (os->bfd_section->size != 0
		    || (r->last_os == NULL
			&& os->bfd_section->vma != os->bfd_section->lma)
		    || (r->last_os != NULL
			&& dot >= (r->last_os->output_section_statement
				   .bfd_section->vma)))
		&& os->lma_region == NULL
		&& !link_info.relocatable)
	      r->last_os = s;

	    /* .tbss sections effectively have zero size.  */
	    if ((os->bfd_section->flags & SEC_HAS_CONTENTS) != 0
		|| (os->bfd_section->flags & SEC_THREAD_LOCAL) == 0
		|| link_info.relocatable)
	      dot += TO_ADDR (os->bfd_section->size);

	    if (os->update_dot_tree != 0)
	      exp_fold_tree (os->update_dot_tree, bfd_abs_section_ptr, &dot);

	    /* Update dot in the region ?
	       We only do this if the section is going to be allocated,
	       since unallocated sections do not contribute to the region's
	       overall size in memory.

	       If the SEC_NEVER_LOAD bit is not set, it will affect the
	       addresses of sections after it. We have to update
	       dot.  */
	    if (os->region != NULL
		&& ((os->bfd_section->flags & SEC_NEVER_LOAD) == 0
		    || (os->bfd_section->flags & (SEC_ALLOC | SEC_LOAD))))
	      {
		os->region->current = dot;

		if (check_regions)
		  /* Make sure the new address is within the region.  */
		  os_region_check (os, os->region, os->addr_tree,
				   os->bfd_section->vma);

		if (os->lma_region != NULL && os->lma_region != os->region)
		  {
		    os->lma_region->current
		      = os->bfd_section->lma + TO_ADDR (os->bfd_section->size);

		    if (check_regions)
		      os_region_check (os, os->lma_region, NULL,
				       os->bfd_section->lma);
		  }
	      }
	  }
	  break;

	case lang_constructors_statement_enum:
	  dot = lang_size_sections_1 (constructor_list.head,
				      output_section_statement,
				      &s->wild_statement.children.head,
				      fill, dot, relax, check_regions);
	  break;

	case lang_data_statement_enum:
	  {
	    unsigned int size = 0;

	    s->data_statement.output_offset =
	      dot - output_section_statement->bfd_section->vma;
	    s->data_statement.output_section =
	      output_section_statement->bfd_section;

	    /* We might refer to provided symbols in the expression, and
	       need to mark them as needed.  */
	    exp_fold_tree (s->data_statement.exp, bfd_abs_section_ptr, &dot);

	    switch (s->data_statement.type)
	      {
	      default:
		abort ();
	      case QUAD:
	      case SQUAD:
		size = QUAD_SIZE;
		break;
	      case LONG:
		size = LONG_SIZE;
		break;
	      case SHORT:
		size = SHORT_SIZE;
		break;
	      case BYTE:
		size = BYTE_SIZE;
		break;
	      }
	    if (size < TO_SIZE ((unsigned) 1))
	      size = TO_SIZE ((unsigned) 1);
	    dot += TO_ADDR (size);
	    output_section_statement->bfd_section->size += size;
	  }
	  break;

	case lang_reloc_statement_enum:
	  {
	    int size;

	    s->reloc_statement.output_offset =
	      dot - output_section_statement->bfd_section->vma;
	    s->reloc_statement.output_section =
	      output_section_statement->bfd_section;
	    size = bfd_get_reloc_size (s->reloc_statement.howto);
	    dot += TO_ADDR (size);
	    output_section_statement->bfd_section->size += size;
	  }
	  break;

	case lang_wild_statement_enum:
	  dot = lang_size_sections_1 (s->wild_statement.children.head,
				      output_section_statement,
				      &s->wild_statement.children.head,
				      fill, dot, relax, check_regions);
	  break;

	case lang_object_symbols_statement_enum:
	  link_info.create_object_symbols_section =
	    output_section_statement->bfd_section;
	  break;

	case lang_output_statement_enum:
	case lang_target_statement_enum:
	  break;

	case lang_input_section_enum:
	  {
	    asection *i;

	    i = (*prev)->input_section.section;
	    if (relax)
	      {
		bfd_boolean again;

		if (! bfd_relax_section (i->owner, i, &link_info, &again))
		  einfo (_("%P%F: can't relax section: %E\n"));
		if (again)
		  *relax = TRUE;
	      }
	    dot = size_input_section (prev, output_section_statement,
				      output_section_statement->fill, dot);
	  }
	  break;

	case lang_input_statement_enum:
	  break;

	case lang_fill_statement_enum:
	  s->fill_statement.output_section =
	    output_section_statement->bfd_section;

	  fill = s->fill_statement.fill;
	  break;

	case lang_assignment_statement_enum:
	  {
	    bfd_vma newdot = dot;
	    etree_type *tree = s->assignment_statement.exp;

	    exp_fold_tree (tree,
			   output_section_statement->bfd_section,
			   &newdot);

	    /* This symbol is relative to this section.  */
	    if ((tree->type.node_class == etree_provided
		 || tree->type.node_class == etree_assign)
		&& (tree->assign.dst [0] != '.'
		    || tree->assign.dst [1] != '\0'))
	      output_section_statement->section_relative_symbol = 1;

	    if (!output_section_statement->ignored)
	      {
		if (output_section_statement == abs_output_section)
		  {
		    /* If we don't have an output section, then just adjust
		       the default memory address.  */
		    lang_memory_region_lookup (DEFAULT_MEMORY_REGION,
					       FALSE)->current = newdot;
		  }
		else if (newdot != dot)
		  {
		    /* Insert a pad after this statement.  We can't
		       put the pad before when relaxing, in case the
		       assignment references dot.  */
		    insert_pad (&s->header.next, fill, TO_SIZE (newdot - dot),
				output_section_statement->bfd_section, dot);

		    /* Don't neuter the pad below when relaxing.  */
		    s = s->header.next;

		    /* If dot is advanced, this implies that the section
		       should have space allocated to it, unless the
		       user has explicitly stated that the section
		       should never be loaded.  */
		    if (!(output_section_statement->flags
			  & (SEC_NEVER_LOAD | SEC_ALLOC)))
		      output_section_statement->bfd_section->flags |= SEC_ALLOC;
		  }
		dot = newdot;
	      }
	  }
	  break;

	case lang_padding_statement_enum:
	  /* If this is the first time lang_size_sections is called,
	     we won't have any padding statements.  If this is the
	     second or later passes when relaxing, we should allow
	     padding to shrink.  If padding is needed on this pass, it
	     will be added back in.  */
	  s->padding_statement.size = 0;

	  /* Make sure output_offset is valid.  If relaxation shrinks
	     the section and this pad isn't needed, it's possible to
	     have output_offset larger than the final size of the
	     section.  bfd_set_section_contents will complain even for
	     a pad size of zero.  */
	  s->padding_statement.output_offset
	    = dot - output_section_statement->bfd_section->vma;
	  break;

	case lang_group_statement_enum:
	  dot = lang_size_sections_1 (s->group_statement.children.head,
				      output_section_statement,
				      &s->group_statement.children.head,
				      fill, dot, relax, check_regions);
	  break;

	default:
	  FAIL ();
	  break;

	  /* We can only get here when relaxing is turned on.  */
	case lang_address_statement_enum:
	  break;
	}
      prev = &s->header.next;
    }
  return dot;
}

/* Callback routine that is used in _bfd_elf_map_sections_to_segments.
   The BFD library has set NEW_SEGMENT to TRUE iff it thinks that
   CURRENT_SECTION and PREVIOUS_SECTION ought to be placed into different
   segments.  We are allowed an opportunity to override this decision.  */

bfd_boolean
ldlang_override_segment_assignment (struct bfd_link_info * info ATTRIBUTE_UNUSED,
				    bfd * abfd ATTRIBUTE_UNUSED,
				    asection * current_section,
				    asection * previous_section,
				    bfd_boolean new_segment)
{
  lang_output_section_statement_type * cur;
  lang_output_section_statement_type * prev;

  /* The checks below are only necessary when the BFD library has decided
     that the two sections ought to be placed into the same segment.  */
  if (new_segment)
    return TRUE;

  /* Paranoia checks.  */
  if (current_section == NULL || previous_section == NULL)
    return new_segment;

  /* Find the memory regions associated with the two sections.
     We call lang_output_section_find() here rather than scanning the list
     of output sections looking for a matching section pointer because if
     we have a large number of sections then a hash lookup is faster.  */
  cur  = lang_output_section_find (current_section->name);
  prev = lang_output_section_find (previous_section->name);

  /* More paranoia.  */
  if (cur == NULL || prev == NULL)
    return new_segment;

  /* If the regions are different then force the sections to live in
     different segments.  See the email thread starting at the following
     URL for the reasons why this is necessary:
     http://sourceware.org/ml/binutils/2007-02/msg00216.html  */
  return cur->region != prev->region;
}

void
one_lang_size_sections_pass (bfd_boolean *relax, bfd_boolean check_regions)
{
  lang_statement_iteration++;
  lang_size_sections_1 (statement_list.head, abs_output_section,
			&statement_list.head, 0, 0, relax, check_regions);
}

void
lang_size_sections (bfd_boolean *relax, bfd_boolean check_regions)
{
  expld.phase = lang_allocating_phase_enum;
  expld.dataseg.phase = exp_dataseg_none;

  one_lang_size_sections_pass (relax, check_regions);
  if (expld.dataseg.phase == exp_dataseg_end_seen
      && link_info.relro && expld.dataseg.relro_end)
    {
      /* If DATA_SEGMENT_ALIGN DATA_SEGMENT_RELRO_END pair was seen, try
	 to put expld.dataseg.relro on a (common) page boundary.  */
      bfd_vma old_min_base, relro_end, maxpage;

      expld.dataseg.phase = exp_dataseg_relro_adjust;
      old_min_base = expld.dataseg.min_base;
      maxpage = expld.dataseg.maxpagesize;
      expld.dataseg.base += (-expld.dataseg.relro_end
			     & (expld.dataseg.pagesize - 1));
      /* Compute the expected PT_GNU_RELRO segment end.  */
      relro_end = (expld.dataseg.relro_end + expld.dataseg.pagesize - 1)
		  & ~(expld.dataseg.pagesize - 1);
      if (old_min_base + maxpage < expld.dataseg.base)
	{
	  expld.dataseg.base -= maxpage;
	  relro_end -= maxpage;
	}
      lang_reset_memory_regions ();
      one_lang_size_sections_pass (relax, check_regions);
      if (expld.dataseg.relro_end > relro_end)
	{
	  /* The alignment of sections between DATA_SEGMENT_ALIGN
	     and DATA_SEGMENT_RELRO_END caused huge padding to be
	     inserted at DATA_SEGMENT_RELRO_END.  Try some other base.  */
	  asection *sec;
	  unsigned int max_alignment_power = 0;

	  /* Find maximum alignment power of sections between
	     DATA_SEGMENT_ALIGN and DATA_SEGMENT_RELRO_END.  */
	  for (sec = output_bfd->sections; sec; sec = sec->next)
	    if (sec->vma >= expld.dataseg.base
		&& sec->vma < expld.dataseg.relro_end
		&& sec->alignment_power > max_alignment_power)
	      max_alignment_power = sec->alignment_power;

	  if (((bfd_vma) 1 << max_alignment_power) < expld.dataseg.pagesize)
	    {
	      if (expld.dataseg.base - (1 << max_alignment_power)
		  < old_min_base)
		expld.dataseg.base += expld.dataseg.pagesize;
	      expld.dataseg.base -= (1 << max_alignment_power);
	      lang_reset_memory_regions ();
	      one_lang_size_sections_pass (relax, check_regions);
	    }
	}
      link_info.relro_start = expld.dataseg.base;
      link_info.relro_end = expld.dataseg.relro_end;
    }
  else if (expld.dataseg.phase == exp_dataseg_end_seen)
    {
      /* If DATA_SEGMENT_ALIGN DATA_SEGMENT_END pair was seen, check whether
	 a page could be saved in the data segment.  */
      bfd_vma first, last;

      first = -expld.dataseg.base & (expld.dataseg.pagesize - 1);
      last = expld.dataseg.end & (expld.dataseg.pagesize - 1);
      if (first && last
	  && ((expld.dataseg.base & ~(expld.dataseg.pagesize - 1))
	      != (expld.dataseg.end & ~(expld.dataseg.pagesize - 1)))
	  && first + last <= expld.dataseg.pagesize)
	{
	  expld.dataseg.phase = exp_dataseg_adjust;
	  lang_reset_memory_regions ();
	  one_lang_size_sections_pass (relax, check_regions);
	}
    }

  expld.phase = lang_final_phase_enum;
}

/* Worker function for lang_do_assignments.  Recursiveness goes here.  */

static bfd_vma
lang_do_assignments_1 (lang_statement_union_type *s,
		       lang_output_section_statement_type *current_os,
		       fill_type *fill,
		       bfd_vma dot)
{
  for (; s != NULL; s = s->header.next)
    {
      switch (s->header.type)
	{
	case lang_constructors_statement_enum:
	  dot = lang_do_assignments_1 (constructor_list.head,
				       current_os, fill, dot);
	  break;

	case lang_output_section_statement_enum:
	  {
	    lang_output_section_statement_type *os;

	    os = &(s->output_section_statement);
	    if (os->bfd_section != NULL && !os->ignored)
	      {
		dot = os->bfd_section->vma;

		lang_do_assignments_1 (os->children.head, os, os->fill, dot);

		/* .tbss sections effectively have zero size.  */
		if ((os->bfd_section->flags & SEC_HAS_CONTENTS) != 0
		    || (os->bfd_section->flags & SEC_THREAD_LOCAL) == 0
		    || link_info.relocatable)
		  dot += TO_ADDR (os->bfd_section->size);
	      }
	  }
	  break;

	case lang_wild_statement_enum:

	  dot = lang_do_assignments_1 (s->wild_statement.children.head,
				       current_os, fill, dot);
	  break;

	case lang_object_symbols_statement_enum:
	case lang_output_statement_enum:
	case lang_target_statement_enum:
	  break;

	case lang_data_statement_enum:
	  exp_fold_tree (s->data_statement.exp, bfd_abs_section_ptr, &dot);
	  if (expld.result.valid_p)
	    s->data_statement.value = (expld.result.value
				       + expld.result.section->vma);
	  else
	    einfo (_("%F%P: invalid data statement\n"));
	  {
	    unsigned int size;
	    switch (s->data_statement.type)
	      {
	      default:
		abort ();
	      case QUAD:
	      case SQUAD:
		size = QUAD_SIZE;
		break;
	      case LONG:
		size = LONG_SIZE;
		break;
	      case SHORT:
		size = SHORT_SIZE;
		break;
	      case BYTE:
		size = BYTE_SIZE;
		break;
	      }
	    if (size < TO_SIZE ((unsigned) 1))
	      size = TO_SIZE ((unsigned) 1);
	    dot += TO_ADDR (size);
	  }
	  break;

	case lang_reloc_statement_enum:
	  exp_fold_tree (s->reloc_statement.addend_exp,
			 bfd_abs_section_ptr, &dot);
	  if (expld.result.valid_p)
	    s->reloc_statement.addend_value = expld.result.value;
	  else
	    einfo (_("%F%P: invalid reloc statement\n"));
	  dot += TO_ADDR (bfd_get_reloc_size (s->reloc_statement.howto));
	  break;

	case lang_input_section_enum:
	  {
	    asection *in = s->input_section.section;

	    if ((in->flags & SEC_EXCLUDE) == 0)
	      dot += TO_ADDR (in->size);
	  }
	  break;

	case lang_input_statement_enum:
	  break;

	case lang_fill_statement_enum:
	  fill = s->fill_statement.fill;
	  break;

	case lang_assignment_statement_enum:
	  exp_fold_tree (s->assignment_statement.exp,
			 current_os->bfd_section,
			 &dot);
	  break;

	case lang_padding_statement_enum:
	  dot += TO_ADDR (s->padding_statement.size);
	  break;

	case lang_group_statement_enum:
	  dot = lang_do_assignments_1 (s->group_statement.children.head,
				       current_os, fill, dot);
	  break;

	default:
	  FAIL ();
	  break;

	case lang_address_statement_enum:
	  break;
	}
    }
  return dot;
}

void
lang_do_assignments (void)
{
  lang_statement_iteration++;
  lang_do_assignments_1 (statement_list.head, abs_output_section, NULL, 0);
}

/* Fix any .startof. or .sizeof. symbols.  When the assemblers see the
   operator .startof. (section_name), it produces an undefined symbol
   .startof.section_name.  Similarly, when it sees
   .sizeof. (section_name), it produces an undefined symbol
   .sizeof.section_name.  For all the output sections, we look for
   such symbols, and set them to the correct value.  */

static void
lang_set_startof (void)
{
  asection *s;

  if (link_info.relocatable)
    return;

  for (s = output_bfd->sections; s != NULL; s = s->next)
    {
      const char *secname;
      char *buf;
      struct bfd_link_hash_entry *h;

      secname = bfd_get_section_name (output_bfd, s);
      buf = xmalloc (10 + strlen (secname));

      sprintf (buf, ".startof.%s", secname);
      h = bfd_link_hash_lookup (link_info.hash, buf, FALSE, FALSE, TRUE);
      if (h != NULL && h->type == bfd_link_hash_undefined)
	{
	  h->type = bfd_link_hash_defined;
	  h->u.def.value = bfd_get_section_vma (output_bfd, s);
	  h->u.def.section = bfd_abs_section_ptr;
	}

      sprintf (buf, ".sizeof.%s", secname);
      h = bfd_link_hash_lookup (link_info.hash, buf, FALSE, FALSE, TRUE);
      if (h != NULL && h->type == bfd_link_hash_undefined)
	{
	  h->type = bfd_link_hash_defined;
	  h->u.def.value = TO_ADDR (s->size);
	  h->u.def.section = bfd_abs_section_ptr;
	}

      free (buf);
    }
}

static void
lang_end (void)
{
  struct bfd_link_hash_entry *h;
  bfd_boolean warn;

  if (link_info.relocatable || link_info.shared)
    warn = FALSE;
  else
    warn = TRUE;

  if (entry_symbol.name == NULL)
    {
      /* No entry has been specified.  Look for the default entry, but
	 don't warn if we don't find it.  */
      entry_symbol.name = entry_symbol_default;
      warn = FALSE;
    }

  h = bfd_link_hash_lookup (link_info.hash, entry_symbol.name,
			    FALSE, FALSE, TRUE);
  if (h != NULL
      && (h->type == bfd_link_hash_defined
	  || h->type == bfd_link_hash_defweak)
      && h->u.def.section->output_section != NULL)
    {
      bfd_vma val;

      val = (h->u.def.value
	     + bfd_get_section_vma (output_bfd,
				    h->u.def.section->output_section)
	     + h->u.def.section->output_offset);
      if (! bfd_set_start_address (output_bfd, val))
	einfo (_("%P%F:%s: can't set start address\n"), entry_symbol.name);
    }
  else
    {
      bfd_vma val;
      const char *send;

      /* We couldn't find the entry symbol.  Try parsing it as a
	 number.  */
      val = bfd_scan_vma (entry_symbol.name, &send, 0);
      if (*send == '\0')
	{
	  if (! bfd_set_start_address (output_bfd, val))
	    einfo (_("%P%F: can't set start address\n"));
	}
      else
	{
	  asection *ts;

	  /* Can't find the entry symbol, and it's not a number.  Use
	     the first address in the text section.  */
	  ts = bfd_get_section_by_name (output_bfd, entry_section);
	  if (ts != NULL)
	    {
	      if (warn)
		einfo (_("%P: warning: cannot find entry symbol %s;"
			 " defaulting to %V\n"),
		       entry_symbol.name,
		       bfd_get_section_vma (output_bfd, ts));
	      if (! bfd_set_start_address (output_bfd,
					   bfd_get_section_vma (output_bfd,
								ts)))
		einfo (_("%P%F: can't set start address\n"));
	    }
	  else
	    {
	      if (warn)
		einfo (_("%P: warning: cannot find entry symbol %s;"
			 " not setting start address\n"),
		       entry_symbol.name);
	    }
	}
    }

  /* Don't bfd_hash_table_free (&lang_definedness_table);
     map file output may result in a call of lang_track_definedness.  */
}

/* This is a small function used when we want to ignore errors from
   BFD.  */

static void
ignore_bfd_errors (const char *s ATTRIBUTE_UNUSED, ...)
{
  /* Don't do anything.  */
}

/* Check that the architecture of all the input files is compatible
   with the output file.  Also call the backend to let it do any
   other checking that is needed.  */

static void
lang_check (void)
{
  lang_statement_union_type *file;
  bfd *input_bfd;
  const bfd_arch_info_type *compatible;

  for (file = file_chain.head; file != NULL; file = file->input_statement.next)
    {
      input_bfd = file->input_statement.the_bfd;
      compatible
	= bfd_arch_get_compatible (input_bfd, output_bfd,
				   command_line.accept_unknown_input_arch);

      /* In general it is not possible to perform a relocatable
	 link between differing object formats when the input
	 file has relocations, because the relocations in the
	 input format may not have equivalent representations in
	 the output format (and besides BFD does not translate
	 relocs for other link purposes than a final link).  */
      if ((link_info.relocatable || link_info.emitrelocations)
	  && (compatible == NULL
	      || bfd_get_flavour (input_bfd) != bfd_get_flavour (output_bfd))
	  && (bfd_get_file_flags (input_bfd) & HAS_RELOC) != 0)
	{
	  einfo (_("%P%F: Relocatable linking with relocations from"
		   " format %s (%B) to format %s (%B) is not supported\n"),
		 bfd_get_target (input_bfd), input_bfd,
		 bfd_get_target (output_bfd), output_bfd);
	  /* einfo with %F exits.  */
	}

      if (compatible == NULL)
	{
	  if (command_line.warn_mismatch)
	    einfo (_("%P%X: %s architecture of input file `%B'"
		     " is incompatible with %s output\n"),
		   bfd_printable_name (input_bfd), input_bfd,
		   bfd_printable_name (output_bfd));
	}
      else if (bfd_count_sections (input_bfd))
	{
	  /* If the input bfd has no contents, it shouldn't set the
	     private data of the output bfd.  */

	  bfd_error_handler_type pfn = NULL;

	  /* If we aren't supposed to warn about mismatched input
	     files, temporarily set the BFD error handler to a
	     function which will do nothing.  We still want to call
	     bfd_merge_private_bfd_data, since it may set up
	     information which is needed in the output file.  */
	  if (! command_line.warn_mismatch)
	    pfn = bfd_set_error_handler (ignore_bfd_errors);
	  if (! bfd_merge_private_bfd_data (input_bfd, output_bfd))
	    {
	      if (command_line.warn_mismatch)
		einfo (_("%P%X: failed to merge target specific data"
			 " of file %B\n"), input_bfd);
	    }
	  if (! command_line.warn_mismatch)
	    bfd_set_error_handler (pfn);
	}
    }
}

/* Look through all the global common symbols and attach them to the
   correct section.  The -sort-common command line switch may be used
   to roughly sort the entries by size.  */

static void
lang_common (void)
{
  if (command_line.inhibit_common_definition)
    return;
  if (link_info.relocatable
      && ! command_line.force_common_definition)
    return;

  if (! config.sort_common)
    bfd_link_hash_traverse (link_info.hash, lang_one_common, NULL);
  else
    {
      int power;

      for (power = 4; power >= 0; power--)
	bfd_link_hash_traverse (link_info.hash, lang_one_common, &power);
    }
}

/* Place one common symbol in the correct section.  */

static bfd_boolean
lang_one_common (struct bfd_link_hash_entry *h, void *info)
{
  unsigned int power_of_two;
  bfd_vma size;
  asection *section;

  if (h->type != bfd_link_hash_common)
    return TRUE;

  size = h->u.c.size;
  power_of_two = h->u.c.p->alignment_power;

  if (config.sort_common
      && power_of_two < (unsigned int) *(int *) info)
    return TRUE;

  section = h->u.c.p->section;

  /* Increase the size of the section to align the common sym.  */
  section->size += ((bfd_vma) 1 << (power_of_two + opb_shift)) - 1;
  section->size &= (- (bfd_vma) 1 << (power_of_two + opb_shift));

  /* Adjust the alignment if necessary.  */
  if (power_of_two > section->alignment_power)
    section->alignment_power = power_of_two;

  /* Change the symbol from common to defined.  */
  h->type = bfd_link_hash_defined;
  h->u.def.section = section;
  h->u.def.value = section->size;

  /* Increase the size of the section.  */
  section->size += size;

  /* Make sure the section is allocated in memory, and make sure that
     it is no longer a common section.  */
  section->flags |= SEC_ALLOC;
  section->flags &= ~SEC_IS_COMMON;

  if (config.map_file != NULL)
    {
      static bfd_boolean header_printed;
      int len;
      char *name;
      char buf[50];

      if (! header_printed)
	{
	  minfo (_("\nAllocating common symbols\n"));
	  minfo (_("Common symbol       size              file\n\n"));
	  header_printed = TRUE;
	}

      name = bfd_demangle (output_bfd, h->root.string,
			   DMGL_ANSI | DMGL_PARAMS);
      if (name == NULL)
	{
	  minfo ("%s", h->root.string);
	  len = strlen (h->root.string);
	}
      else
	{
	  minfo ("%s", name);
	  len = strlen (name);
	  free (name);
	}

      if (len >= 19)
	{
	  print_nl ();
	  len = 0;
	}
      while (len < 20)
	{
	  print_space ();
	  ++len;
	}

      minfo ("0x");
      if (size <= 0xffffffff)
	sprintf (buf, "%lx", (unsigned long) size);
      else
	sprintf_vma (buf, size);
      minfo ("%s", buf);
      len = strlen (buf);

      while (len < 16)
	{
	  print_space ();
	  ++len;
	}

      minfo ("%B\n", section->owner);
    }

  return TRUE;
}

/* Run through the input files and ensure that every input section has
   somewhere to go.  If one is found without a destination then create
   an input request and place it into the statement tree.  */

static void
lang_place_orphans (void)
{
  LANG_FOR_EACH_INPUT_STATEMENT (file)
    {
      asection *s;

      for (s = file->the_bfd->sections; s != NULL; s = s->next)
	{
	  if (s->output_section == NULL)
	    {
	      /* This section of the file is not attached, root
		 around for a sensible place for it to go.  */

	      if (file->just_syms_flag)
		bfd_link_just_syms (file->the_bfd, s, &link_info);
	      else if ((s->flags & SEC_EXCLUDE) != 0)
		s->output_section = bfd_abs_section_ptr;
	      else if (strcmp (s->name, "COMMON") == 0)
		{
		  /* This is a lonely common section which must have
		     come from an archive.  We attach to the section
		     with the wildcard.  */
		  if (! link_info.relocatable
		      || command_line.force_common_definition)
		    {
		      if (default_common_section == NULL)
			{
			  default_common_section =
			    lang_output_section_statement_lookup (".bss");

			}
		      lang_add_section (&default_common_section->children, s,
					default_common_section);
		    }
		}
	      else if (ldemul_place_orphan (s))
		;
	      else
		{
		  lang_output_section_statement_type *os;

		  os = lang_output_section_statement_lookup (s->name);
		  lang_add_section (&os->children, s, os);
		}
	    }
	}
    }
}

void
lang_set_flags (lang_memory_region_type *ptr, const char *flags, int invert)
{
  flagword *ptr_flags;

  ptr_flags = invert ? &ptr->not_flags : &ptr->flags;
  while (*flags)
    {
      switch (*flags)
	{
	case 'A': case 'a':
	  *ptr_flags |= SEC_ALLOC;
	  break;

	case 'R': case 'r':
	  *ptr_flags |= SEC_READONLY;
	  break;

	case 'W': case 'w':
	  *ptr_flags |= SEC_DATA;
	  break;

	case 'X': case 'x':
	  *ptr_flags |= SEC_CODE;
	  break;

	case 'L': case 'l':
	case 'I': case 'i':
	  *ptr_flags |= SEC_LOAD;
	  break;

	default:
	  einfo (_("%P%F: invalid syntax in flags\n"));
	  break;
	}
      flags++;
    }
}

/* Call a function on each input file.  This function will be called
   on an archive, but not on the elements.  */

void
lang_for_each_input_file (void (*func) (lang_input_statement_type *))
{
  lang_input_statement_type *f;

  for (f = (lang_input_statement_type *) input_file_chain.head;
       f != NULL;
       f = (lang_input_statement_type *) f->next_real_file)
    func (f);
}

/* Call a function on each file.  The function will be called on all
   the elements of an archive which are included in the link, but will
   not be called on the archive file itself.  */

void
lang_for_each_file (void (*func) (lang_input_statement_type *))
{
  LANG_FOR_EACH_INPUT_STATEMENT (f)
    {
      func (f);
    }
}

void
ldlang_add_file (lang_input_statement_type *entry)
{
  lang_statement_append (&file_chain,
			 (lang_statement_union_type *) entry,
			 &entry->next);

  /* The BFD linker needs to have a list of all input BFDs involved in
     a link.  */
  ASSERT (entry->the_bfd->link_next == NULL);
  ASSERT (entry->the_bfd != output_bfd);

  *link_info.input_bfds_tail = entry->the_bfd;
  link_info.input_bfds_tail = &entry->the_bfd->link_next;
  entry->the_bfd->usrdata = entry;
  bfd_set_gp_size (entry->the_bfd, g_switch_value);

  /* Look through the sections and check for any which should not be
     included in the link.  We need to do this now, so that we can
     notice when the backend linker tries to report multiple
     definition errors for symbols which are in sections we aren't
     going to link.  FIXME: It might be better to entirely ignore
     symbols which are defined in sections which are going to be
     discarded.  This would require modifying the backend linker for
     each backend which might set the SEC_LINK_ONCE flag.  If we do
     this, we should probably handle SEC_EXCLUDE in the same way.  */

  bfd_map_over_sections (entry->the_bfd, section_already_linked, entry);
}

void
lang_add_output (const char *name, int from_script)
{
  /* Make -o on command line override OUTPUT in script.  */
  if (!had_output_filename || !from_script)
    {
      output_filename = name;
      had_output_filename = TRUE;
    }
}

static lang_output_section_statement_type *current_section;

static int
topower (int x)
{
  unsigned int i = 1;
  int l;

  if (x < 0)
    return -1;

  for (l = 0; l < 32; l++)
    {
      if (i >= (unsigned int) x)
	return l;
      i <<= 1;
    }

  return 0;
}

lang_output_section_statement_type *
lang_enter_output_section_statement (const char *output_section_statement_name,
				     etree_type *address_exp,
				     enum section_type sectype,
				     etree_type *align,
				     etree_type *subalign,
				     etree_type *ebase,
				     int constraint)
{
  lang_output_section_statement_type *os;

   os = lang_output_section_statement_lookup_1 (output_section_statement_name,
						constraint);
   current_section = os;

  /* Make next things chain into subchain of this.  */

  if (os->addr_tree == NULL)
    {
      os->addr_tree = address_exp;
    }
  os->sectype = sectype;
  if (sectype != noload_section)
    os->flags = SEC_NO_FLAGS;
  else
    os->flags = SEC_NEVER_LOAD;
  os->block_value = 1;
  stat_ptr = &os->children;

  os->subsection_alignment =
    topower (exp_get_value_int (subalign, -1, "subsection alignment"));
  os->section_alignment =
    topower (exp_get_value_int (align, -1, "section alignment"));

  os->load_base = ebase;
  return os;
}

void
lang_final (void)
{
  lang_output_statement_type *new;

  new = new_stat (lang_output_statement, stat_ptr);
  new->name = output_filename;
}

/* Reset the current counters in the regions.  */

void
lang_reset_memory_regions (void)
{
  lang_memory_region_type *p = lang_memory_region_list;
  asection *o;
  lang_output_section_statement_type *os;

  for (p = lang_memory_region_list; p != NULL; p = p->next)
    {
      p->current = p->origin;
      p->last_os = NULL;
    }

  for (os = &lang_output_section_statement.head->output_section_statement;
       os != NULL;
       os = os->next)
    {
      os->processed_vma = FALSE;
      os->processed_lma = FALSE;
    }

  for (o = output_bfd->sections; o != NULL; o = o->next)
    {
      /* Save the last size for possible use by bfd_relax_section.  */
      o->rawsize = o->size;
      o->size = 0;
    }
}

/* Worker for lang_gc_sections_1.  */

static void
gc_section_callback (lang_wild_statement_type *ptr,
		     struct wildcard_list *sec ATTRIBUTE_UNUSED,
		     asection *section,
		     lang_input_statement_type *file ATTRIBUTE_UNUSED,
		     void *data ATTRIBUTE_UNUSED)
{
  /* If the wild pattern was marked KEEP, the member sections
     should be as well.  */
  if (ptr->keep_sections)
    section->flags |= SEC_KEEP;
}

/* Iterate over sections marking them against GC.  */

static void
lang_gc_sections_1 (lang_statement_union_type *s)
{
  for (; s != NULL; s = s->header.next)
    {
      switch (s->header.type)
	{
	case lang_wild_statement_enum:
	  walk_wild (&s->wild_statement, gc_section_callback, NULL);
	  break;
	case lang_constructors_statement_enum:
	  lang_gc_sections_1 (constructor_list.head);
	  break;
	case lang_output_section_statement_enum:
	  lang_gc_sections_1 (s->output_section_statement.children.head);
	  break;
	case lang_group_statement_enum:
	  lang_gc_sections_1 (s->group_statement.children.head);
	  break;
	default:
	  break;
	}
    }
}

static void
lang_gc_sections (void)
{
  struct bfd_link_hash_entry *h;
  ldlang_undef_chain_list_type *ulist;

  /* Keep all sections so marked in the link script.  */

  lang_gc_sections_1 (statement_list.head);

  /* Keep all sections containing symbols undefined on the command-line,
     and the section containing the entry symbol.  */

  for (ulist = link_info.gc_sym_list; ulist; ulist = ulist->next)
    {
      h = bfd_link_hash_lookup (link_info.hash, ulist->name,
				FALSE, FALSE, FALSE);

      if (h != NULL
	  && (h->type == bfd_link_hash_defined
	      || h->type == bfd_link_hash_defweak)
	  && ! bfd_is_abs_section (h->u.def.section))
	{
	  h->u.def.section->flags |= SEC_KEEP;
	}
    }

  /* SEC_EXCLUDE is ignored when doing a relocatable link, except in
     the special case of debug info.  (See bfd/stabs.c)
     Twiddle the flag here, to simplify later linker code.  */
  if (link_info.relocatable)
    {
      LANG_FOR_EACH_INPUT_STATEMENT (f)
	{
	  asection *sec;
	  for (sec = f->the_bfd->sections; sec != NULL; sec = sec->next)
	    if ((sec->flags & SEC_DEBUGGING) == 0)
	      sec->flags &= ~SEC_EXCLUDE;
	}
    }

  if (link_info.gc_sections)
    bfd_gc_sections (output_bfd, &link_info);
}

/* Relax all sections until bfd_relax_section gives up.  */

static void
relax_sections (void)
{
  /* Keep relaxing until bfd_relax_section gives up.  */
  bfd_boolean relax_again;

  link_info.relax_trip = -1;
  do
    {
      relax_again = FALSE;
      link_info.relax_trip++;

      /* Note: pe-dll.c does something like this also.  If you find
	 you need to change this code, you probably need to change
	 pe-dll.c also.  DJ  */

      /* Do all the assignments with our current guesses as to
	 section sizes.  */
      lang_do_assignments ();

      /* We must do this after lang_do_assignments, because it uses
	 size.  */
      lang_reset_memory_regions ();

      /* Perform another relax pass - this time we know where the
	 globals are, so can make a better guess.  */
      lang_size_sections (&relax_again, FALSE);
    }
  while (relax_again);
}

void
lang_process (void)
{
  /* Finalize dynamic list.  */
  if (link_info.dynamic_list)
    lang_finalize_version_expr_head (&link_info.dynamic_list->head);

  current_target = default_target;

  /* Open the output file.  */
  lang_for_each_statement (ldlang_open_output);
  init_opb ();

  ldemul_create_output_section_statements ();

  /* Add to the hash table all undefineds on the command line.  */
  lang_place_undefineds ();

  if (!bfd_section_already_linked_table_init ())
    einfo (_("%P%F: Failed to create hash table\n"));

  /* Create a bfd for each input file.  */
  current_target = default_target;
  open_input_bfds (statement_list.head, FALSE);

  link_info.gc_sym_list = &entry_symbol;
  if (entry_symbol.name == NULL)
    link_info.gc_sym_list = ldlang_undef_chain_list_head;

  ldemul_after_open ();

  bfd_section_already_linked_table_free ();

  /* Make sure that we're not mixing architectures.  We call this
     after all the input files have been opened, but before we do any
     other processing, so that any operations merge_private_bfd_data
     does on the output file will be known during the rest of the
     link.  */
  lang_check ();

  /* Handle .exports instead of a version script if we're told to do so.  */
  if (command_line.version_exports_section)
    lang_do_version_exports_section ();

  /* Build all sets based on the information gathered from the input
     files.  */
  ldctor_build_sets ();

  /* Remove unreferenced sections if asked to.  */
  lang_gc_sections ();

  /* Size up the common data.  */
  lang_common ();

  /* Update wild statements.  */
  update_wild_statements (statement_list.head);

  /* Run through the contours of the script and attach input sections
     to the correct output sections.  */
  map_input_to_output_sections (statement_list.head, NULL, NULL);

  /* Find any sections not attached explicitly and handle them.  */
  lang_place_orphans ();

  if (! link_info.relocatable)
    {
      asection *found;

      /* Merge SEC_MERGE sections.  This has to be done after GC of
	 sections, so that GCed sections are not merged, but before
	 assigning dynamic symbols, since removing whole input sections
	 is hard then.  */
      bfd_merge_sections (output_bfd, &link_info);

      /* Look for a text section and set the readonly attribute in it.  */
      found = bfd_get_section_by_name (output_bfd, ".text");

      if (found != NULL)
	{
	  if (config.text_read_only)
	    found->flags |= SEC_READONLY;
	  else
	    found->flags &= ~SEC_READONLY;
	}
    }

  /* Do anything special before sizing sections.  This is where ELF
     and other back-ends size dynamic sections.  */
  ldemul_before_allocation ();

  /* We must record the program headers before we try to fix the
     section positions, since they will affect SIZEOF_HEADERS.  */
  lang_record_phdrs ();

  /* Size up the sections.  */
  lang_size_sections (NULL, !command_line.relax);

  /* Now run around and relax if we can.  */
  if (command_line.relax)
    {
      /* We may need more than one relaxation pass.  */
      int i = link_info.relax_pass;

      /* The backend can use it to determine the current pass.  */
      link_info.relax_pass = 0;

      while (i--)
	{
	  relax_sections ();
	  link_info.relax_pass++;
	}

      /* Final extra sizing to report errors.  */
      lang_do_assignments ();
      lang_reset_memory_regions ();
      lang_size_sections (NULL, TRUE);
    }

  /* See if anything special should be done now we know how big
     everything is.  */
  ldemul_after_allocation ();

  /* Fix any .startof. or .sizeof. symbols.  */
  lang_set_startof ();

  /* Do all the assignments, now that we know the final resting places
     of all the symbols.  */

  lang_do_assignments ();

  ldemul_finish ();

  /* Make sure that the section addresses make sense.  */
  if (! link_info.relocatable
      && command_line.check_section_addresses)
    lang_check_section_addresses ();

  lang_end ();
}

/* EXPORTED TO YACC */

void
lang_add_wild (struct wildcard_spec *filespec,
	       struct wildcard_list *section_list,
	       bfd_boolean keep_sections)
{
  struct wildcard_list *curr, *next;
  lang_wild_statement_type *new;

  /* Reverse the list as the parser puts it back to front.  */
  for (curr = section_list, section_list = NULL;
       curr != NULL;
       section_list = curr, curr = next)
    {
      if (curr->spec.name != NULL && strcmp (curr->spec.name, "COMMON") == 0)
	placed_commons = TRUE;

      next = curr->next;
      curr->next = section_list;
    }

  if (filespec != NULL && filespec->name != NULL)
    {
      if (strcmp (filespec->name, "*") == 0)
	filespec->name = NULL;
      else if (! wildcardp (filespec->name))
	lang_has_input_file = TRUE;
    }

  new = new_stat (lang_wild_statement, stat_ptr);
  new->filename = NULL;
  new->filenames_sorted = FALSE;
  if (filespec != NULL)
    {
      new->filename = filespec->name;
      new->filenames_sorted = filespec->sorted == by_name;
    }
  new->section_list = section_list;
  new->keep_sections = keep_sections;
  lang_list_init (&new->children);
  analyze_walk_wild_section_handler (new);
}

void
lang_section_start (const char *name, etree_type *address,
		    const segment_type *segment)
{
  lang_address_statement_type *ad;

  ad = new_stat (lang_address_statement, stat_ptr);
  ad->section_name = name;
  ad->address = address;
  ad->segment = segment;
}

/* Set the start symbol to NAME.  CMDLINE is nonzero if this is called
   because of a -e argument on the command line, or zero if this is
   called by ENTRY in a linker script.  Command line arguments take
   precedence.  */

void
lang_add_entry (const char *name, bfd_boolean cmdline)
{
  if (entry_symbol.name == NULL
      || cmdline
      || ! entry_from_cmdline)
    {
      entry_symbol.name = name;
      entry_from_cmdline = cmdline;
    }
}

/* Set the default start symbol to NAME.  .em files should use this,
   not lang_add_entry, to override the use of "start" if neither the
   linker script nor the command line specifies an entry point.  NAME
   must be permanently allocated.  */
void
lang_default_entry (const char *name)
{
  entry_symbol_default = name;
}

void
lang_add_target (const char *name)
{
  lang_target_statement_type *new;

  new = new_stat (lang_target_statement, stat_ptr);
  new->target = name;
}

void
lang_add_map (const char *name)
{
  while (*name)
    {
      switch (*name)
	{
	case 'F':
	  map_option_f = TRUE;
	  break;
	}
      name++;
    }
}

void
lang_add_fill (fill_type *fill)
{
  lang_fill_statement_type *new;

  new = new_stat (lang_fill_statement, stat_ptr);
  new->fill = fill;
}

void
lang_add_data (int type, union etree_union *exp)
{
  lang_data_statement_type *new;

  new = new_stat (lang_data_statement, stat_ptr);
  new->exp = exp;
  new->type = type;
}

/* Create a new reloc statement.  RELOC is the BFD relocation type to
   generate.  HOWTO is the corresponding howto structure (we could
   look this up, but the caller has already done so).  SECTION is the
   section to generate a reloc against, or NAME is the name of the
   symbol to generate a reloc against.  Exactly one of SECTION and
   NAME must be NULL.  ADDEND is an expression for the addend.  */

void
lang_add_reloc (bfd_reloc_code_real_type reloc,
		reloc_howto_type *howto,
		asection *section,
		const char *name,
		union etree_union *addend)
{
  lang_reloc_statement_type *p = new_stat (lang_reloc_statement, stat_ptr);

  p->reloc = reloc;
  p->howto = howto;
  p->section = section;
  p->name = name;
  p->addend_exp = addend;

  p->addend_value = 0;
  p->output_section = NULL;
  p->output_offset = 0;
}

lang_assignment_statement_type *
lang_add_assignment (etree_type *exp)
{
  lang_assignment_statement_type *new;

  new = new_stat (lang_assignment_statement, stat_ptr);
  new->exp = exp;
  return new;
}

void
lang_add_attribute (enum statement_enum attribute)
{
  new_statement (attribute, sizeof (lang_statement_header_type), stat_ptr);
}

void
lang_startup (const char *name)
{
  if (startup_file != NULL)
    {
      einfo (_("%P%F: multiple STARTUP files\n"));
    }
  first_file->filename = name;
  first_file->local_sym_name = name;
  first_file->real = TRUE;

  startup_file = name;
}

void
lang_float (bfd_boolean maybe)
{
  lang_float_flag = maybe;
}


/* Work out the load- and run-time regions from a script statement, and
   store them in *LMA_REGION and *REGION respectively.

   MEMSPEC is the name of the run-time region, or the value of
   DEFAULT_MEMORY_REGION if the statement didn't specify one.
   LMA_MEMSPEC is the name of the load-time region, or null if the
   statement didn't specify one.HAVE_LMA_P is TRUE if the statement
   had an explicit load address.

   It is an error to specify both a load region and a load address.  */

static void
lang_get_regions (lang_memory_region_type **region,
		  lang_memory_region_type **lma_region,
		  const char *memspec,
		  const char *lma_memspec,
		  bfd_boolean have_lma,
		  bfd_boolean have_vma)
{
  *lma_region = lang_memory_region_lookup (lma_memspec, FALSE);

  /* If no runtime region or VMA has been specified, but the load region
     has been specified, then use the load region for the runtime region
     as well.  */
  if (lma_memspec != NULL
      && ! have_vma
      && strcmp (memspec, DEFAULT_MEMORY_REGION) == 0)
    *region = *lma_region;
  else
    *region = lang_memory_region_lookup (memspec, FALSE);

  if (have_lma && lma_memspec != 0)
    einfo (_("%X%P:%S: section has both a load address and a load region\n"));
}

void
lang_leave_output_section_statement (fill_type *fill, const char *memspec,
				     lang_output_section_phdr_list *phdrs,
				     const char *lma_memspec)
{
  lang_get_regions (&current_section->region,
		    &current_section->lma_region,
		    memspec, lma_memspec,
		    current_section->load_base != NULL,
		    current_section->addr_tree != NULL);
  current_section->fill = fill;
  current_section->phdrs = phdrs;
  stat_ptr = &statement_list;
}

/* Create an absolute symbol with the given name with the value of the
   address of first byte of the section named.

   If the symbol already exists, then do nothing.  */

void
lang_abs_symbol_at_beginning_of (const char *secname, const char *name)
{
  struct bfd_link_hash_entry *h;

  h = bfd_link_hash_lookup (link_info.hash, name, TRUE, TRUE, TRUE);
  if (h == NULL)
    einfo (_("%P%F: bfd_link_hash_lookup failed: %E\n"));

  if (h->type == bfd_link_hash_new
      || h->type == bfd_link_hash_undefined)
    {
      asection *sec;

      h->type = bfd_link_hash_defined;

      sec = bfd_get_section_by_name (output_bfd, secname);
      if (sec == NULL)
	h->u.def.value = 0;
      else
	h->u.def.value = bfd_get_section_vma (output_bfd, sec);

      h->u.def.section = bfd_abs_section_ptr;
    }
}

/* Create an absolute symbol with the given name with the value of the
   address of the first byte after the end of the section named.

   If the symbol already exists, then do nothing.  */

void
lang_abs_symbol_at_end_of (const char *secname, const char *name)
{
  struct bfd_link_hash_entry *h;

  h = bfd_link_hash_lookup (link_info.hash, name, TRUE, TRUE, TRUE);
  if (h == NULL)
    einfo (_("%P%F: bfd_link_hash_lookup failed: %E\n"));

  if (h->type == bfd_link_hash_new
      || h->type == bfd_link_hash_undefined)
    {
      asection *sec;

      h->type = bfd_link_hash_defined;

      sec = bfd_get_section_by_name (output_bfd, secname);
      if (sec == NULL)
	h->u.def.value = 0;
      else
	h->u.def.value = (bfd_get_section_vma (output_bfd, sec)
			  + TO_ADDR (sec->size));

      h->u.def.section = bfd_abs_section_ptr;
    }
}

void
lang_statement_append (lang_statement_list_type *list,
		       lang_statement_union_type *element,
		       lang_statement_union_type **field)
{
  *(list->tail) = element;
  list->tail = field;
}

/* Set the output format type.  -oformat overrides scripts.  */

void
lang_add_output_format (const char *format,
			const char *big,
			const char *little,
			int from_script)
{
  if (output_target == NULL || !from_script)
    {
      if (command_line.endian == ENDIAN_BIG
	  && big != NULL)
	format = big;
      else if (command_line.endian == ENDIAN_LITTLE
	       && little != NULL)
	format = little;

      output_target = format;
    }
}

/* Enter a group.  This creates a new lang_group_statement, and sets
   stat_ptr to build new statements within the group.  */

void
lang_enter_group (void)
{
  lang_group_statement_type *g;

  g = new_stat (lang_group_statement, stat_ptr);
  lang_list_init (&g->children);
  stat_ptr = &g->children;
}

/* Leave a group.  This just resets stat_ptr to start writing to the
   regular list of statements again.  Note that this will not work if
   groups can occur inside anything else which can adjust stat_ptr,
   but currently they can't.  */

void
lang_leave_group (void)
{
  stat_ptr = &statement_list;
}

/* Add a new program header.  This is called for each entry in a PHDRS
   command in a linker script.  */

void
lang_new_phdr (const char *name,
	       etree_type *type,
	       bfd_boolean filehdr,
	       bfd_boolean phdrs,
	       etree_type *at,
	       etree_type *flags)
{
  struct lang_phdr *n, **pp;

  n = stat_alloc (sizeof (struct lang_phdr));
  n->next = NULL;
  n->name = name;
  n->type = exp_get_value_int (type, 0, "program header type");
  n->filehdr = filehdr;
  n->phdrs = phdrs;
  n->at = at;
  n->flags = flags;

  for (pp = &lang_phdr_list; *pp != NULL; pp = &(*pp)->next)
    ;
  *pp = n;
}

/* Record the program header information in the output BFD.  FIXME: We
   should not be calling an ELF specific function here.  */

static void
lang_record_phdrs (void)
{
  unsigned int alc;
  asection **secs;
  lang_output_section_phdr_list *last;
  struct lang_phdr *l;
  lang_output_section_statement_type *os;

  alc = 10;
  secs = xmalloc (alc * sizeof (asection *));
  last = NULL;

  for (l = lang_phdr_list; l != NULL; l = l->next)
    {
      unsigned int c;
      flagword flags;
      bfd_vma at;

      c = 0;
      for (os = &lang_output_section_statement.head->output_section_statement;
	   os != NULL;
	   os = os->next)
	{
	  lang_output_section_phdr_list *pl;

	  if (os->constraint == -1)
	    continue;

	  pl = os->phdrs;
	  if (pl != NULL)
	    last = pl;
	  else
	    {
	      if (os->sectype == noload_section
		  || os->bfd_section == NULL
		  || (os->bfd_section->flags & SEC_ALLOC) == 0)
		continue;

	      if (last)
		pl = last;
	      else
		{
		  lang_output_section_statement_type * tmp_os;

		  /* If we have not run across a section with a program
		     header assigned to it yet, then scan forwards to find
		     one.  This prevents inconsistencies in the linker's
		     behaviour when a script has specified just a single
		     header and there are sections in that script which are
		     not assigned to it, and which occur before the first
		     use of that header. See here for more details:
		     http://sourceware.org/ml/binutils/2007-02/msg00291.html  */
		  for (tmp_os = os; tmp_os; tmp_os = tmp_os->next)
		    if (tmp_os->phdrs)
		      break;
		  pl = tmp_os->phdrs;
		}
	    }

	  if (os->bfd_section == NULL)
	    continue;

	  for (; pl != NULL; pl = pl->next)
	    {
	      if (strcmp (pl->name, l->name) == 0)
		{
		  if (c >= alc)
		    {
		      alc *= 2;
		      secs = xrealloc (secs, alc * sizeof (asection *));
		    }
		  secs[c] = os->bfd_section;
		  ++c;
		  pl->used = TRUE;
		}
	    }
	}

      if (l->flags == NULL)
	flags = 0;
      else
	flags = exp_get_vma (l->flags, 0, "phdr flags");

      if (l->at == NULL)
	at = 0;
      else
	at = exp_get_vma (l->at, 0, "phdr load address");

      if (! bfd_record_phdr (output_bfd, l->type,
			     l->flags != NULL, flags, l->at != NULL,
			     at, l->filehdr, l->phdrs, c, secs))
	einfo (_("%F%P: bfd_record_phdr failed: %E\n"));
    }

  free (secs);

  /* Make sure all the phdr assignments succeeded.  */
  for (os = &lang_output_section_statement.head->output_section_statement;
       os != NULL;
       os = os->next)
    {
      lang_output_section_phdr_list *pl;

      if (os->constraint == -1
	  || os->bfd_section == NULL)
	continue;

      for (pl = os->phdrs;
	   pl != NULL;
	   pl = pl->next)
	if (! pl->used && strcmp (pl->name, "NONE") != 0)
	  einfo (_("%X%P: section `%s' assigned to non-existent phdr `%s'\n"),
		 os->name, pl->name);
    }
}

/* Record a list of sections which may not be cross referenced.  */

void
lang_add_nocrossref (lang_nocrossref_type *l)
{
  struct lang_nocrossrefs *n;

  n = xmalloc (sizeof *n);
  n->next = nocrossref_list;
  n->list = l;
  nocrossref_list = n;

  /* Set notice_all so that we get informed about all symbols.  */
  link_info.notice_all = TRUE;
}

/* Overlay handling.  We handle overlays with some static variables.  */

/* The overlay virtual address.  */
static etree_type *overlay_vma;
/* And subsection alignment.  */
static etree_type *overlay_subalign;

/* An expression for the maximum section size seen so far.  */
static etree_type *overlay_max;

/* A list of all the sections in this overlay.  */

struct overlay_list {
  struct overlay_list *next;
  lang_output_section_statement_type *os;
};

static struct overlay_list *overlay_list;

/* Start handling an overlay.  */

void
lang_enter_overlay (etree_type *vma_expr, etree_type *subalign)
{
  /* The grammar should prevent nested overlays from occurring.  */
  ASSERT (overlay_vma == NULL
	  && overlay_subalign == NULL
	  && overlay_max == NULL);

  overlay_vma = vma_expr;
  overlay_subalign = subalign;
}

/* Start a section in an overlay.  We handle this by calling
   lang_enter_output_section_statement with the correct VMA.
   lang_leave_overlay sets up the LMA and memory regions.  */

void
lang_enter_overlay_section (const char *name)
{
  struct overlay_list *n;
  etree_type *size;

  lang_enter_output_section_statement (name, overlay_vma, overlay_section,
				       0, overlay_subalign, 0, 0);

  /* If this is the first section, then base the VMA of future
     sections on this one.  This will work correctly even if `.' is
     used in the addresses.  */
  if (overlay_list == NULL)
    overlay_vma = exp_nameop (ADDR, name);

  /* Remember the section.  */
  n = xmalloc (sizeof *n);
  n->os = current_section;
  n->next = overlay_list;
  overlay_list = n;

  size = exp_nameop (SIZEOF, name);

  /* Arrange to work out the maximum section end address.  */
  if (overlay_max == NULL)
    overlay_max = size;
  else
    overlay_max = exp_binop (MAX_K, overlay_max, size);
}

/* Finish a section in an overlay.  There isn't any special to do
   here.  */

void
lang_leave_overlay_section (fill_type *fill,
			    lang_output_section_phdr_list *phdrs)
{
  const char *name;
  char *clean, *s2;
  const char *s1;
  char *buf;

  name = current_section->name;

  /* For now, assume that DEFAULT_MEMORY_REGION is the run-time memory
     region and that no load-time region has been specified.  It doesn't
     really matter what we say here, since lang_leave_overlay will
     override it.  */
  lang_leave_output_section_statement (fill, DEFAULT_MEMORY_REGION, phdrs, 0);

  /* Define the magic symbols.  */

  clean = xmalloc (strlen (name) + 1);
  s2 = clean;
  for (s1 = name; *s1 != '\0'; s1++)
    if (ISALNUM (*s1) || *s1 == '_')
      *s2++ = *s1;
  *s2 = '\0';

  buf = xmalloc (strlen (clean) + sizeof "__load_start_");
  sprintf (buf, "__load_start_%s", clean);
  lang_add_assignment (exp_provide (buf,
				    exp_nameop (LOADADDR, name),
				    FALSE));

  buf = xmalloc (strlen (clean) + sizeof "__load_stop_");
  sprintf (buf, "__load_stop_%s", clean);
  lang_add_assignment (exp_provide (buf,
				    exp_binop ('+',
					       exp_nameop (LOADADDR, name),
					       exp_nameop (SIZEOF, name)),
				    FALSE));

  free (clean);
}

/* Finish an overlay.  If there are any overlay wide settings, this
   looks through all the sections in the overlay and sets them.  */

void
lang_leave_overlay (etree_type *lma_expr,
		    int nocrossrefs,
		    fill_type *fill,
		    const char *memspec,
		    lang_output_section_phdr_list *phdrs,
		    const char *lma_memspec)
{
  lang_memory_region_type *region;
  lang_memory_region_type *lma_region;
  struct overlay_list *l;
  lang_nocrossref_type *nocrossref;

  lang_get_regions (&region, &lma_region,
		    memspec, lma_memspec,
		    lma_expr != NULL, FALSE);

  nocrossref = NULL;

  /* After setting the size of the last section, set '.' to end of the
     overlay region.  */
  if (overlay_list != NULL)
    overlay_list->os->update_dot_tree
      = exp_assop ('=', ".", exp_binop ('+', overlay_vma, overlay_max));

  l = overlay_list;
  while (l != NULL)
    {
      struct overlay_list *next;

      if (fill != NULL && l->os->fill == NULL)
	l->os->fill = fill;

      l->os->region = region;
      l->os->lma_region = lma_region;

      /* The first section has the load address specified in the
	 OVERLAY statement.  The rest are worked out from that.
	 The base address is not needed (and should be null) if
	 an LMA region was specified.  */
      if (l->next == 0)
	{
	  l->os->load_base = lma_expr;
	  l->os->sectype = normal_section;
	}
      if (phdrs != NULL && l->os->phdrs == NULL)
	l->os->phdrs = phdrs;

      if (nocrossrefs)
	{
	  lang_nocrossref_type *nc;

	  nc = xmalloc (sizeof *nc);
	  nc->name = l->os->name;
	  nc->next = nocrossref;
	  nocrossref = nc;
	}

      next = l->next;
      free (l);
      l = next;
    }

  if (nocrossref != NULL)
    lang_add_nocrossref (nocrossref);

  overlay_vma = NULL;
  overlay_list = NULL;
  overlay_max = NULL;
}

/* Version handling.  This is only useful for ELF.  */

/* This global variable holds the version tree that we build.  */

struct bfd_elf_version_tree *lang_elf_version_info;

/* If PREV is NULL, return first version pattern matching particular symbol.
   If PREV is non-NULL, return first version pattern matching particular
   symbol after PREV (previously returned by lang_vers_match).  */

static struct bfd_elf_version_expr *
lang_vers_match (struct bfd_elf_version_expr_head *head,
		 struct bfd_elf_version_expr *prev,
		 const char *sym)
{
  const char *cxx_sym = sym;
  const char *java_sym = sym;
  struct bfd_elf_version_expr *expr = NULL;

  if (head->mask & BFD_ELF_VERSION_CXX_TYPE)
    {
      cxx_sym = cplus_demangle (sym, DMGL_PARAMS | DMGL_ANSI);
      if (!cxx_sym)
	cxx_sym = sym;
    }
  if (head->mask & BFD_ELF_VERSION_JAVA_TYPE)
    {
      java_sym = cplus_demangle (sym, DMGL_JAVA);
      if (!java_sym)
	java_sym = sym;
    }

  if (head->htab && (prev == NULL || prev->symbol))
    {
      struct bfd_elf_version_expr e;

      switch (prev ? prev->mask : 0)
	{
	  case 0:
	    if (head->mask & BFD_ELF_VERSION_C_TYPE)
	      {
		e.symbol = sym;
		expr = htab_find (head->htab, &e);
		while (expr && strcmp (expr->symbol, sym) == 0)
		  if (expr->mask == BFD_ELF_VERSION_C_TYPE)
		    goto out_ret;
		  else
		    expr = expr->next;
	      }
	    /* Fallthrough */
	  case BFD_ELF_VERSION_C_TYPE:
	    if (head->mask & BFD_ELF_VERSION_CXX_TYPE)
	      {
		e.symbol = cxx_sym;
		expr = htab_find (head->htab, &e);
		while (expr && strcmp (expr->symbol, cxx_sym) == 0)
		  if (expr->mask == BFD_ELF_VERSION_CXX_TYPE)
		    goto out_ret;
		  else
		    expr = expr->next;
	      }
	    /* Fallthrough */
	  case BFD_ELF_VERSION_CXX_TYPE:
	    if (head->mask & BFD_ELF_VERSION_JAVA_TYPE)
	      {
		e.symbol = java_sym;
		expr = htab_find (head->htab, &e);
		while (expr && strcmp (expr->symbol, java_sym) == 0)
		  if (expr->mask == BFD_ELF_VERSION_JAVA_TYPE)
		    goto out_ret;
		  else
		    expr = expr->next;
	      }
	    /* Fallthrough */
	  default:
	    break;
	}
    }

  /* Finally, try the wildcards.  */
  if (prev == NULL || prev->symbol)
    expr = head->remaining;
  else
    expr = prev->next;
  for (; expr; expr = expr->next)
    {
      const char *s;

      if (!expr->pattern)
	continue;

      if (expr->pattern[0] == '*' && expr->pattern[1] == '\0')
	break;

      if (expr->mask == BFD_ELF_VERSION_JAVA_TYPE)
	s = java_sym;
      else if (expr->mask == BFD_ELF_VERSION_CXX_TYPE)
	s = cxx_sym;
      else
	s = sym;
      if (fnmatch (expr->pattern, s, 0) == 0)
	break;
    }

out_ret:
  if (cxx_sym != sym)
    free ((char *) cxx_sym);
  if (java_sym != sym)
    free ((char *) java_sym);
  return expr;
}

/* Return NULL if the PATTERN argument is a glob pattern, otherwise,
   return a string pointing to the symbol name.  */

static const char *
realsymbol (const char *pattern)
{
  const char *p;
  bfd_boolean changed = FALSE, backslash = FALSE;
  char *s, *symbol = xmalloc (strlen (pattern) + 1);

  for (p = pattern, s = symbol; *p != '\0'; ++p)
    {
      /* It is a glob pattern only if there is no preceding
	 backslash.  */
      if (! backslash && (*p == '?' || *p == '*' || *p == '['))
	{
	  free (symbol);
	  return NULL;
	}

      if (backslash)
	{
	  /* Remove the preceding backslash.  */
	  *(s - 1) = *p;
	  changed = TRUE;
	}
      else
	*s++ = *p;

      backslash = *p == '\\';
    }

  if (changed)
    {
      *s = '\0';
      return symbol;
    }
  else
    {
      free (symbol);
      return pattern;
    }
}

/* This is called for each variable name or match expression.  NEW is
   the name of the symbol to match, or, if LITERAL_P is FALSE, a glob
   pattern to be matched against symbol names.  */

struct bfd_elf_version_expr *
lang_new_vers_pattern (struct bfd_elf_version_expr *orig,
		       const char *new,
		       const char *lang,
		       bfd_boolean literal_p)
{
  struct bfd_elf_version_expr *ret;

  ret = xmalloc (sizeof *ret);
  ret->next = orig;
  ret->pattern = literal_p ? NULL : new;
  ret->symver = 0;
  ret->script = 0;
  ret->symbol = literal_p ? new : realsymbol (new);

  if (lang == NULL || strcasecmp (lang, "C") == 0)
    ret->mask = BFD_ELF_VERSION_C_TYPE;
  else if (strcasecmp (lang, "C++") == 0)
    ret->mask = BFD_ELF_VERSION_CXX_TYPE;
  else if (strcasecmp (lang, "Java") == 0)
    ret->mask = BFD_ELF_VERSION_JAVA_TYPE;
  else
    {
      einfo (_("%X%P: unknown language `%s' in version information\n"),
	     lang);
      ret->mask = BFD_ELF_VERSION_C_TYPE;
    }

  return ldemul_new_vers_pattern (ret);
}

/* This is called for each set of variable names and match
   expressions.  */

struct bfd_elf_version_tree *
lang_new_vers_node (struct bfd_elf_version_expr *globals,
		    struct bfd_elf_version_expr *locals)
{
  struct bfd_elf_version_tree *ret;

  ret = xcalloc (1, sizeof *ret);
  ret->globals.list = globals;
  ret->locals.list = locals;
  ret->match = lang_vers_match;
  ret->name_indx = (unsigned int) -1;
  return ret;
}

/* This static variable keeps track of version indices.  */

static int version_index;

static hashval_t
version_expr_head_hash (const void *p)
{
  const struct bfd_elf_version_expr *e = p;

  return htab_hash_string (e->symbol);
}

static int
version_expr_head_eq (const void *p1, const void *p2)
{
  const struct bfd_elf_version_expr *e1 = p1;
  const struct bfd_elf_version_expr *e2 = p2;

  return strcmp (e1->symbol, e2->symbol) == 0;
}

static void
lang_finalize_version_expr_head (struct bfd_elf_version_expr_head *head)
{
  size_t count = 0;
  struct bfd_elf_version_expr *e, *next;
  struct bfd_elf_version_expr **list_loc, **remaining_loc;

  for (e = head->list; e; e = e->next)
    {
      if (e->symbol)
	count++;
      head->mask |= e->mask;
    }

  if (count)
    {
      head->htab = htab_create (count * 2, version_expr_head_hash,
				version_expr_head_eq, NULL);
      list_loc = &head->list;
      remaining_loc = &head->remaining;
      for (e = head->list; e; e = next)
	{
	  next = e->next;
	  if (!e->symbol)
	    {
	      *remaining_loc = e;
	      remaining_loc = &e->next;
	    }
	  else
	    {
	      void **loc = htab_find_slot (head->htab, e, INSERT);

	      if (*loc)
		{
		  struct bfd_elf_version_expr *e1, *last;

		  e1 = *loc;
		  last = NULL;
		  do
		    {
		      if (e1->mask == e->mask)
			{
			  last = NULL;
			  break;
			}
		      last = e1;
		      e1 = e1->next;
		    }
		  while (e1 && strcmp (e1->symbol, e->symbol) == 0);

		  if (last == NULL)
		    {
		      /* This is a duplicate.  */
		      /* FIXME: Memory leak.  Sometimes pattern is not
			 xmalloced alone, but in larger chunk of memory.  */
		      /* free (e->symbol); */
		      free (e);
		    }
		  else
		    {
		      e->next = last->next;
		      last->next = e;
		    }
		}
	      else
		{
		  *loc = e;
		  *list_loc = e;
		  list_loc = &e->next;
		}
	    }
	}
      *remaining_loc = NULL;
      *list_loc = head->remaining;
    }
  else
    head->remaining = head->list;
}

/* This is called when we know the name and dependencies of the
   version.  */

void
lang_register_vers_node (const char *name,
			 struct bfd_elf_version_tree *version,
			 struct bfd_elf_version_deps *deps)
{
  struct bfd_elf_version_tree *t, **pp;
  struct bfd_elf_version_expr *e1;

  if (name == NULL)
    name = "";

  if ((name[0] == '\0' && lang_elf_version_info != NULL)
      || (lang_elf_version_info && lang_elf_version_info->name[0] == '\0'))
    {
      einfo (_("%X%P: anonymous version tag cannot be combined"
	       " with other version tags\n"));
      free (version);
      return;
    }

  /* Make sure this node has a unique name.  */
  for (t = lang_elf_version_info; t != NULL; t = t->next)
    if (strcmp (t->name, name) == 0)
      einfo (_("%X%P: duplicate version tag `%s'\n"), name);

  lang_finalize_version_expr_head (&version->globals);
  lang_finalize_version_expr_head (&version->locals);

  /* Check the global and local match names, and make sure there
     aren't any duplicates.  */

  for (e1 = version->globals.list; e1 != NULL; e1 = e1->next)
    {
      for (t = lang_elf_version_info; t != NULL; t = t->next)
	{
	  struct bfd_elf_version_expr *e2;

	  if (t->locals.htab && e1->symbol)
	    {
	      e2 = htab_find (t->locals.htab, e1);
	      while (e2 && strcmp (e1->symbol, e2->symbol) == 0)
		{
		  if (e1->mask == e2->mask)
		    einfo (_("%X%P: duplicate expression `%s'"
			     " in version information\n"), e1->symbol);
		  e2 = e2->next;
		}
	    }
	  else if (!e1->symbol)
	    for (e2 = t->locals.remaining; e2 != NULL; e2 = e2->next)
	      if (strcmp (e1->pattern, e2->pattern) == 0
		  && e1->mask == e2->mask)
		einfo (_("%X%P: duplicate expression `%s'"
			 " in version information\n"), e1->pattern);
	}
    }

  for (e1 = version->locals.list; e1 != NULL; e1 = e1->next)
    {
      for (t = lang_elf_version_info; t != NULL; t = t->next)
	{
	  struct bfd_elf_version_expr *e2;

	  if (t->globals.htab && e1->symbol)
	    {
	      e2 = htab_find (t->globals.htab, e1);
	      while (e2 && strcmp (e1->symbol, e2->symbol) == 0)
		{
		  if (e1->mask == e2->mask)
		    einfo (_("%X%P: duplicate expression `%s'"
			     " in version information\n"),
			   e1->symbol);
		  e2 = e2->next;
		}
	    }
	  else if (!e1->symbol)
	    for (e2 = t->globals.remaining; e2 != NULL; e2 = e2->next)
	      if (strcmp (e1->pattern, e2->pattern) == 0
		  && e1->mask == e2->mask)
		einfo (_("%X%P: duplicate expression `%s'"
			 " in version information\n"), e1->pattern);
	}
    }

  version->deps = deps;
  version->name = name;
  if (name[0] != '\0')
    {
      ++version_index;
      version->vernum = version_index;
    }
  else
    version->vernum = 0;

  for (pp = &lang_elf_version_info; *pp != NULL; pp = &(*pp)->next)
    ;
  *pp = version;
}

/* This is called when we see a version dependency.  */

struct bfd_elf_version_deps *
lang_add_vers_depend (struct bfd_elf_version_deps *list, const char *name)
{
  struct bfd_elf_version_deps *ret;
  struct bfd_elf_version_tree *t;

  ret = xmalloc (sizeof *ret);
  ret->next = list;

  for (t = lang_elf_version_info; t != NULL; t = t->next)
    {
      if (strcmp (t->name, name) == 0)
	{
	  ret->version_needed = t;
	  return ret;
	}
    }

  einfo (_("%X%P: unable to find version dependency `%s'\n"), name);

  return ret;
}

static void
lang_do_version_exports_section (void)
{
  struct bfd_elf_version_expr *greg = NULL, *lreg;

  LANG_FOR_EACH_INPUT_STATEMENT (is)
    {
      asection *sec = bfd_get_section_by_name (is->the_bfd, ".exports");
      char *contents, *p;
      bfd_size_type len;

      if (sec == NULL)
	continue;

      len = sec->size;
      contents = xmalloc (len);
      if (!bfd_get_section_contents (is->the_bfd, sec, contents, 0, len))
	einfo (_("%X%P: unable to read .exports section contents\n"), sec);

      p = contents;
      while (p < contents + len)
	{
	  greg = lang_new_vers_pattern (greg, p, NULL, FALSE);
	  p = strchr (p, '\0') + 1;
	}

      /* Do not free the contents, as we used them creating the regex.  */

      /* Do not include this section in the link.  */
      sec->flags |= SEC_EXCLUDE | SEC_KEEP;
    }

  lreg = lang_new_vers_pattern (NULL, "*", NULL, FALSE);
  lang_register_vers_node (command_line.version_exports_section,
			   lang_new_vers_node (greg, lreg), NULL);
}

void
lang_add_unique (const char *name)
{
  struct unique_sections *ent;

  for (ent = unique_section_list; ent; ent = ent->next)
    if (strcmp (ent->name, name) == 0)
      return;

  ent = xmalloc (sizeof *ent);
  ent->name = xstrdup (name);
  ent->next = unique_section_list;
  unique_section_list = ent;
}

/* Append the list of dynamic symbols to the existing one.  */

void
lang_append_dynamic_list (struct bfd_elf_version_expr *dynamic)
{
  if (link_info.dynamic_list)
    {
      struct bfd_elf_version_expr *tail;
      for (tail = dynamic; tail->next != NULL; tail = tail->next)
	;
      tail->next = link_info.dynamic_list->head.list;
      link_info.dynamic_list->head.list = dynamic;
    }
  else
    {
      struct bfd_elf_dynamic_list *d;

      d = xcalloc (1, sizeof *d);
      d->head.list = dynamic;
      d->match = lang_vers_match;
      link_info.dynamic_list = d;
    }
}

/* Append the list of C++ typeinfo dynamic symbols to the existing
   one.  */

void
lang_append_dynamic_list_cpp_typeinfo (void)
{
  const char * symbols [] =
    {
      "typeinfo name for*",
      "typeinfo for*"
    };
  struct bfd_elf_version_expr *dynamic = NULL;
  unsigned int i;

  for (i = 0; i < ARRAY_SIZE (symbols); i++)
    dynamic = lang_new_vers_pattern (dynamic, symbols [i], "C++",
				     FALSE);

  lang_append_dynamic_list (dynamic);
}

/* Append the list of C++ operator new and delete dynamic symbols to the
   existing one.  */

void
lang_append_dynamic_list_cpp_new (void)
{
  const char * symbols [] =
    {
      "operator new*",
      "operator delete*"
    };
  struct bfd_elf_version_expr *dynamic = NULL;
  unsigned int i;

  for (i = 0; i < ARRAY_SIZE (symbols); i++)
    dynamic = lang_new_vers_pattern (dynamic, symbols [i], "C++",
				     FALSE);

  lang_append_dynamic_list (dynamic);
}
