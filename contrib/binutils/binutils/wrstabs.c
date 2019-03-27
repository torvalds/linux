/* wrstabs.c -- Output stabs debugging information
   Copyright 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2006, 2007
   Free Software Foundation, Inc.
   Written by Ian Lance Taylor <ian@cygnus.com>.

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

/* This file contains code which writes out stabs debugging
   information.  */

#include "sysdep.h"
#include <assert.h>
#include "bfd.h"
#include "libiberty.h"
#include "safe-ctype.h"
#include "bucomm.h"
#include "debug.h"
#include "budbg.h"
#include "aout/aout64.h"
#include "aout/stab_gnu.h"

/* The size of a stabs symbol.  This presumes 32 bit values.  */

#define STAB_SYMBOL_SIZE (12)

/* An entry in a string hash table.  */

struct string_hash_entry
{
  struct bfd_hash_entry root;
  /* Next string in this table.  */
  struct string_hash_entry *next;
  /* Index in string table.  */
  long index;
  /* Size of type if this is a typedef.  */
  unsigned int size;
};

/* A string hash table.  */

struct string_hash_table
{
  struct bfd_hash_table table;
};

/* The type stack.  Each element on the stack is a string.  */

struct stab_type_stack
{
  /* The next element on the stack.  */
  struct stab_type_stack *next;
  /* This element as a string.  */
  char *string;
  /* The type index of this element.  */
  long index;
  /* The size of the type.  */
  unsigned int size;
  /* Whether type string defines a new type.  */
  bfd_boolean definition;
  /* String defining struct fields.  */
  char *fields;
  /* NULL terminated array of strings defining base classes for a
     class.  */
  char **baseclasses;
  /* String defining class methods.  */
  char *methods;
  /* String defining vtable pointer for a class.  */
  char *vtable;
};

/* This structure is used to keep track of type indices for tagged
   types.  */

struct stab_tag
{
  /* The type index.  */
  long index;
  /* The tag name.  */
  const char *tag;
  /* The kind of type.  This is set to DEBUG_KIND_ILLEGAL when the
     type is defined.  */
  enum debug_type_kind kind;
  /* The size of the struct.  */
  unsigned int size;
};

/* We remember various sorts of type indices.  They are not related,
   but, for convenience, we keep all the information in this
   structure.  */

struct stab_type_cache
{
  /* The void type index.  */
  long void_type;
  /* Signed integer type indices, indexed by size - 1.  */
  long signed_integer_types[8];
  /* Unsigned integer type indices, indexed by size - 1.  */
  long unsigned_integer_types[8];
  /* Floating point types, indexed by size - 1.  */
  long float_types[16];
  /* Pointers to types, indexed by the type index.  */
  long *pointer_types;
  size_t pointer_types_alloc;
  /* Functions returning types, indexed by the type index.  */
  long *function_types;
  size_t function_types_alloc;
  /* References to types, indexed by the type index.  */
  long *reference_types;
  size_t reference_types_alloc;
  /* Struct/union/class type indices, indexed by the struct id.  */
  struct stab_tag *struct_types;
  size_t struct_types_alloc;
};

/* This is the handle passed through debug_write.  */

struct stab_write_handle
{
  /* The BFD.  */
  bfd *abfd;
  /* This buffer holds the symbols.  */
  bfd_byte *symbols;
  size_t symbols_size;
  size_t symbols_alloc;
  /* This is a list of hash table entries for the strings.  */
  struct string_hash_entry *strings;
  /* The last string hash table entry.  */
  struct string_hash_entry *last_string;
  /* The size of the strings.  */
  size_t strings_size;
  /* This hash table eliminates duplicate strings.  */
  struct string_hash_table strhash;
  /* The type stack.  */
  struct stab_type_stack *type_stack;
  /* The next type index.  */
  long type_index;
  /* The type cache.  */
  struct stab_type_cache type_cache;
  /* A mapping from typedef names to type indices.  */
  struct string_hash_table typedef_hash;
  /* If this is not -1, it is the offset to the most recent N_SO
     symbol, and the value of that symbol needs to be set.  */
  long so_offset;
  /* If this is not -1, it is the offset to the most recent N_FUN
     symbol, and the value of that symbol needs to be set.  */
  long fun_offset;
  /* The last text section address seen.  */
  bfd_vma last_text_address;
  /* The block nesting depth.  */
  unsigned int nesting;
  /* The function address.  */
  bfd_vma fnaddr;
  /* A pending LBRAC symbol.  */
  bfd_vma pending_lbrac;
  /* The current line number file name.  */
  const char *lineno_filename;
};

static struct bfd_hash_entry *string_hash_newfunc
  (struct bfd_hash_entry *, struct bfd_hash_table *, const char *);
static bfd_boolean stab_write_symbol
  (struct stab_write_handle *, int, int, bfd_vma, const char *);
static bfd_boolean stab_push_string
  (struct stab_write_handle *, const char *, long, bfd_boolean, unsigned int);
static bfd_boolean stab_push_defined_type
  (struct stab_write_handle *, long, unsigned int);
static char *stab_pop_type (struct stab_write_handle *);
static bfd_boolean stab_modify_type
  (struct stab_write_handle *, int, unsigned int, long **, size_t *);
static long stab_get_struct_index
  (struct stab_write_handle *, const char *, unsigned int,
   enum debug_type_kind, unsigned int *);
static bfd_boolean stab_class_method_var
  (struct stab_write_handle *, const char *, enum debug_visibility,
   bfd_boolean, bfd_boolean, bfd_boolean, bfd_vma, bfd_boolean);
static bfd_boolean stab_start_compilation_unit (void *, const char *);
static bfd_boolean stab_start_source (void *, const char *);
static bfd_boolean stab_empty_type (void *);
static bfd_boolean stab_void_type (void *);
static bfd_boolean stab_int_type (void *, unsigned int, bfd_boolean);
static bfd_boolean stab_float_type (void *, unsigned int);
static bfd_boolean stab_complex_type (void *, unsigned int);
static bfd_boolean stab_bool_type (void *, unsigned int);
static bfd_boolean stab_enum_type
  (void *, const char *, const char **, bfd_signed_vma *);
static bfd_boolean stab_pointer_type (void *);
static bfd_boolean stab_function_type (void *, int, bfd_boolean);
static bfd_boolean stab_reference_type (void *);
static bfd_boolean stab_range_type (void *, bfd_signed_vma, bfd_signed_vma);
static bfd_boolean stab_array_type
  (void *, bfd_signed_vma, bfd_signed_vma, bfd_boolean);
static bfd_boolean stab_set_type (void *, bfd_boolean);
static bfd_boolean stab_offset_type (void *);
static bfd_boolean stab_method_type (void *, bfd_boolean, int, bfd_boolean);
static bfd_boolean stab_const_type (void *);
static bfd_boolean stab_volatile_type (void *);
static bfd_boolean stab_start_struct_type
  (void *, const char *, unsigned int, bfd_boolean, unsigned int);
static bfd_boolean stab_struct_field
  (void *, const char *, bfd_vma, bfd_vma, enum debug_visibility);
static bfd_boolean stab_end_struct_type (void *);
static bfd_boolean stab_start_class_type
  (void *, const char *, unsigned int, bfd_boolean, unsigned int,
   bfd_boolean, bfd_boolean);
static bfd_boolean stab_class_static_member
  (void *, const char *, const char *, enum debug_visibility);
static bfd_boolean stab_class_baseclass
  (void *, bfd_vma, bfd_boolean, enum debug_visibility);
static bfd_boolean stab_class_start_method (void *, const char *);
static bfd_boolean stab_class_method_variant
  (void *, const char *, enum debug_visibility, bfd_boolean, bfd_boolean,
   bfd_vma, bfd_boolean);
static bfd_boolean stab_class_static_method_variant
  (void *, const char *, enum debug_visibility, bfd_boolean, bfd_boolean);
static bfd_boolean stab_class_end_method (void *);
static bfd_boolean stab_end_class_type (void *);
static bfd_boolean stab_typedef_type (void *, const char *);
static bfd_boolean stab_tag_type
  (void *, const char *, unsigned int, enum debug_type_kind);
static bfd_boolean stab_typdef (void *, const char *);
static bfd_boolean stab_tag (void *, const char *);
static bfd_boolean stab_int_constant (void *, const char *, bfd_vma);
static bfd_boolean stab_float_constant (void *, const char *, double);
static bfd_boolean stab_typed_constant (void *, const char *, bfd_vma);
static bfd_boolean stab_variable
  (void *, const char *, enum debug_var_kind, bfd_vma);
static bfd_boolean stab_start_function (void *, const char *, bfd_boolean);
static bfd_boolean stab_function_parameter
  (void *, const char *, enum debug_parm_kind, bfd_vma);
static bfd_boolean stab_start_block (void *, bfd_vma);
static bfd_boolean stab_end_block (void *, bfd_vma);
static bfd_boolean stab_end_function (void *);
static bfd_boolean stab_lineno (void *, const char *, unsigned long, bfd_vma);

static const struct debug_write_fns stab_fns =
{
  stab_start_compilation_unit,
  stab_start_source,
  stab_empty_type,
  stab_void_type,
  stab_int_type,
  stab_float_type,
  stab_complex_type,
  stab_bool_type,
  stab_enum_type,
  stab_pointer_type,
  stab_function_type,
  stab_reference_type,
  stab_range_type,
  stab_array_type,
  stab_set_type,
  stab_offset_type,
  stab_method_type,
  stab_const_type,
  stab_volatile_type,
  stab_start_struct_type,
  stab_struct_field,
  stab_end_struct_type,
  stab_start_class_type,
  stab_class_static_member,
  stab_class_baseclass,
  stab_class_start_method,
  stab_class_method_variant,
  stab_class_static_method_variant,
  stab_class_end_method,
  stab_end_class_type,
  stab_typedef_type,
  stab_tag_type,
  stab_typdef,
  stab_tag,
  stab_int_constant,
  stab_float_constant,
  stab_typed_constant,
  stab_variable,
  stab_start_function,
  stab_function_parameter,
  stab_start_block,
  stab_end_block,
  stab_end_function,
  stab_lineno
};

/* Routine to create an entry in a string hash table.  */

static struct bfd_hash_entry *
string_hash_newfunc (struct bfd_hash_entry *entry,
		     struct bfd_hash_table *table, const char *string)
{
  struct string_hash_entry *ret = (struct string_hash_entry *) entry;

  /* Allocate the structure if it has not already been allocated by a
     subclass.  */
  if (ret == (struct string_hash_entry *) NULL)
    ret = ((struct string_hash_entry *)
	   bfd_hash_allocate (table, sizeof (struct string_hash_entry)));
  if (ret == (struct string_hash_entry *) NULL)
    return NULL;

  /* Call the allocation method of the superclass.  */
  ret = ((struct string_hash_entry *)
	 bfd_hash_newfunc ((struct bfd_hash_entry *) ret, table, string));

  if (ret)
    {
      /* Initialize the local fields.  */
      ret->next = NULL;
      ret->index = -1;
      ret->size = 0;
    }

  return (struct bfd_hash_entry *) ret;
}

/* Look up an entry in a string hash table.  */

#define string_hash_lookup(t, string, create, copy) \
  ((struct string_hash_entry *) \
   bfd_hash_lookup (&(t)->table, (string), (create), (copy)))

/* Add a symbol to the stabs debugging information we are building.  */

static bfd_boolean
stab_write_symbol (struct stab_write_handle *info, int type, int desc,
		   bfd_vma value, const char *string)
{
  bfd_size_type strx;
  bfd_byte sym[STAB_SYMBOL_SIZE];

  if (string == NULL)
    strx = 0;
  else
    {
      struct string_hash_entry *h;

      h = string_hash_lookup (&info->strhash, string, TRUE, TRUE);
      if (h == NULL)
	{
	  non_fatal (_("string_hash_lookup failed: %s"),
		     bfd_errmsg (bfd_get_error ()));
	  return FALSE;
	}
      if (h->index != -1)
	strx = h->index;
      else
	{
	  strx = info->strings_size;
	  h->index = strx;
	  if (info->last_string == NULL)
	    info->strings = h;
	  else
	    info->last_string->next = h;
	  info->last_string = h;
	  info->strings_size += strlen (string) + 1;
	}
    }

  /* This presumes 32 bit values.  */
  bfd_put_32 (info->abfd, strx, sym);
  bfd_put_8 (info->abfd, type, sym + 4);
  bfd_put_8 (info->abfd, 0, sym + 5);
  bfd_put_16 (info->abfd, desc, sym + 6);
  bfd_put_32 (info->abfd, value, sym + 8);

  if (info->symbols_size + STAB_SYMBOL_SIZE > info->symbols_alloc)
    {
      info->symbols_alloc *= 2;
      info->symbols = (bfd_byte *) xrealloc (info->symbols,
					     info->symbols_alloc);
    }

  memcpy (info->symbols + info->symbols_size, sym, STAB_SYMBOL_SIZE);

  info->symbols_size += STAB_SYMBOL_SIZE;

  return TRUE;
}

/* Push a string on to the type stack.  */

static bfd_boolean
stab_push_string (struct stab_write_handle *info, const char *string,
		  long index, bfd_boolean definition, unsigned int size)
{
  struct stab_type_stack *s;

  s = (struct stab_type_stack *) xmalloc (sizeof *s);
  s->string = xstrdup (string);
  s->index = index;
  s->definition = definition;
  s->size = size;

  s->fields = NULL;
  s->baseclasses = NULL;
  s->methods = NULL;
  s->vtable = NULL;

  s->next = info->type_stack;
  info->type_stack = s;

  return TRUE;
}

/* Push a type index which has already been defined.  */

static bfd_boolean
stab_push_defined_type (struct stab_write_handle *info, long index,
			unsigned int size)
{
  char buf[20];

  sprintf (buf, "%ld", index);
  return stab_push_string (info, buf, index, FALSE, size);
}

/* Pop a type off the type stack.  The caller is responsible for
   freeing the string.  */

static char *
stab_pop_type (struct stab_write_handle *info)
{
  struct stab_type_stack *s;
  char *ret;

  s = info->type_stack;
  assert (s != NULL);

  info->type_stack = s->next;

  ret = s->string;

  free (s);

  return ret;
}

/* The general routine to write out stabs in sections debugging
   information.  This accumulates the stabs symbols and the strings in
   two obstacks.  We can't easily write out the information as we go
   along, because we need to know the section sizes before we can
   write out the section contents.  ABFD is the BFD and DHANDLE is the
   handle for the debugging information.  This sets *PSYMS to point to
   the symbols, *PSYMSIZE the size of the symbols, *PSTRINGS to the
   strings, and *PSTRINGSIZE to the size of the strings.  */

bfd_boolean
write_stabs_in_sections_debugging_info (bfd *abfd, void *dhandle,
					bfd_byte **psyms,
					bfd_size_type *psymsize,
					bfd_byte **pstrings,
					bfd_size_type *pstringsize)
{
  struct stab_write_handle info;
  struct string_hash_entry *h;
  bfd_byte *p;

  info.abfd = abfd;

  info.symbols_size = 0;
  info.symbols_alloc = 500;
  info.symbols = (bfd_byte *) xmalloc (info.symbols_alloc);

  info.strings = NULL;
  info.last_string = NULL;
  /* Reserve 1 byte for a null byte.  */
  info.strings_size = 1;

  if (!bfd_hash_table_init (&info.strhash.table, string_hash_newfunc,
			    sizeof (struct string_hash_entry))
      || !bfd_hash_table_init (&info.typedef_hash.table, string_hash_newfunc,
			       sizeof (struct string_hash_entry)))
    {
      non_fatal ("bfd_hash_table_init_failed: %s",
		 bfd_errmsg (bfd_get_error ()));
      return FALSE;
    }

  info.type_stack = NULL;
  info.type_index = 1;
  memset (&info.type_cache, 0, sizeof info.type_cache);
  info.so_offset = -1;
  info.fun_offset = -1;
  info.last_text_address = 0;
  info.nesting = 0;
  info.fnaddr = 0;
  info.pending_lbrac = (bfd_vma) -1;

  /* The initial symbol holds the string size.  */
  if (! stab_write_symbol (&info, 0, 0, 0, (const char *) NULL))
    return FALSE;

  /* Output an initial N_SO symbol.  */
  info.so_offset = info.symbols_size;
  if (! stab_write_symbol (&info, N_SO, 0, 0, bfd_get_filename (abfd)))
    return FALSE;

  if (! debug_write (dhandle, &stab_fns, (void *) &info))
    return FALSE;

  assert (info.pending_lbrac == (bfd_vma) -1);

  /* Output a trailing N_SO.  */
  if (! stab_write_symbol (&info, N_SO, 0, info.last_text_address,
			   (const char *) NULL))
    return FALSE;

  /* Put the string size in the initial symbol.  */
  bfd_put_32 (abfd, info.strings_size, info.symbols + 8);

  *psyms = info.symbols;
  *psymsize = info.symbols_size;

  *pstringsize = info.strings_size;
  *pstrings = (bfd_byte *) xmalloc (info.strings_size);

  p = *pstrings;
  *p++ = '\0';
  for (h = info.strings; h != NULL; h = h->next)
    {
      strcpy ((char *) p, h->root.string);
      p += strlen ((char *) p) + 1;
    }

  return TRUE;
}

/* Start writing out information for a compilation unit.  */

static bfd_boolean
stab_start_compilation_unit (void *p, const char *filename)
{
  struct stab_write_handle *info = (struct stab_write_handle *) p;

  /* We would normally output an N_SO symbol here.  However, that
     would force us to reset all of our type information.  I think we
     will be better off just outputting an N_SOL symbol, and not
     worrying about splitting information between files.  */

  info->lineno_filename = filename;

  return stab_write_symbol (info, N_SOL, 0, 0, filename);
}

/* Start writing out information for a particular source file.  */

static bfd_boolean
stab_start_source (void *p, const char *filename)
{
  struct stab_write_handle *info = (struct stab_write_handle *) p;

  /* FIXME: The symbol's value is supposed to be the text section
     address.  However, we would have to fill it in later, and gdb
     doesn't care, so we don't bother with it.  */

  info->lineno_filename = filename;

  return stab_write_symbol (info, N_SOL, 0, 0, filename);
}

/* Push an empty type.  This shouldn't normally happen.  We just use a
   void type.  */

static bfd_boolean
stab_empty_type (void *p)
{
  struct stab_write_handle *info = (struct stab_write_handle *) p;

  /* We don't call stab_void_type if the type is not yet defined,
     because that might screw up the typedef.  */

  if (info->type_cache.void_type != 0)
    return stab_push_defined_type (info, info->type_cache.void_type, 0);
  else
    {
      long index;
      char buf[40];

      index = info->type_index;
      ++info->type_index;

      sprintf (buf, "%ld=%ld", index, index);

      return stab_push_string (info, buf, index, FALSE, 0);
    }
}

/* Push a void type.  */

static bfd_boolean
stab_void_type (void *p)
{
  struct stab_write_handle *info = (struct stab_write_handle *) p;

  if (info->type_cache.void_type != 0)
    return stab_push_defined_type (info, info->type_cache.void_type, 0);
  else
    {
      long index;
      char buf[40];

      index = info->type_index;
      ++info->type_index;

      info->type_cache.void_type = index;

      sprintf (buf, "%ld=%ld", index, index);

      return stab_push_string (info, buf, index, TRUE, 0);
    }
}

/* Push an integer type.  */

static bfd_boolean
stab_int_type (void *p, unsigned int size, bfd_boolean unsignedp)
{
  struct stab_write_handle *info = (struct stab_write_handle *) p;
  long *cache;

  if (size <= 0 || (size > sizeof (long) && size != 8))
    {
      non_fatal (_("stab_int_type: bad size %u"), size);
      return FALSE;
    }

  if (unsignedp)
    cache = info->type_cache.signed_integer_types;
  else
    cache = info->type_cache.unsigned_integer_types;

  if (cache[size - 1] != 0)
    return stab_push_defined_type (info, cache[size - 1], size);
  else
    {
      long index;
      char buf[100];

      index = info->type_index;
      ++info->type_index;

      cache[size - 1] = index;

      sprintf (buf, "%ld=r%ld;", index, index);
      if (unsignedp)
	{
	  strcat (buf, "0;");
	  if (size < sizeof (long))
	    sprintf (buf + strlen (buf), "%ld;", ((long) 1 << (size * 8)) - 1);
	  else if (size == sizeof (long))
	    strcat (buf, "-1;");
	  else if (size == 8)
	    strcat (buf, "01777777777777777777777;");
	  else
	    abort ();
	}
      else
	{
	  if (size <= sizeof (long))
	    sprintf (buf + strlen (buf), "%ld;%ld;",
		     (long) - ((unsigned long) 1 << (size * 8 - 1)),
		     (long) (((unsigned long) 1 << (size * 8 - 1)) - 1));
	  else if (size == 8)
	    strcat (buf, "01000000000000000000000;0777777777777777777777;");
	  else
	    abort ();
	}

      return stab_push_string (info, buf, index, TRUE, size);
    }
}

/* Push a floating point type.  */

static bfd_boolean
stab_float_type (void *p, unsigned int size)
{
  struct stab_write_handle *info = (struct stab_write_handle *) p;

  if (size > 0
      && size - 1 < (sizeof info->type_cache.float_types
		     / sizeof info->type_cache.float_types[0])
      && info->type_cache.float_types[size - 1] != 0)
    return stab_push_defined_type (info,
				   info->type_cache.float_types[size - 1],
				   size);
  else
    {
      long index;
      char *int_type;
      char buf[50];

      /* Floats are defined as a subrange of int.  */
      if (! stab_int_type (info, 4, FALSE))
	return FALSE;
      int_type = stab_pop_type (info);

      index = info->type_index;
      ++info->type_index;

      if (size > 0
	  && size - 1 < (sizeof info->type_cache.float_types
			 / sizeof info->type_cache.float_types[0]))
	info->type_cache.float_types[size - 1] = index;

      sprintf (buf, "%ld=r%s;%u;0;", index, int_type, size);

      free (int_type);

      return stab_push_string (info, buf, index, TRUE, size);
    }
}

/* Push a complex type.  */

static bfd_boolean
stab_complex_type (void *p, unsigned int size)
{
  struct stab_write_handle *info = (struct stab_write_handle *) p;
  char buf[50];
  long index;

  index = info->type_index;
  ++info->type_index;

  sprintf (buf, "%ld=r%ld;%u;0;", index, index, size);

  return stab_push_string (info, buf, index, TRUE, size * 2);
}

/* Push a bfd_boolean type.  We use an XCOFF predefined type, since gdb
   always recognizes them.  */

static bfd_boolean
stab_bool_type (void *p, unsigned int size)
{
  struct stab_write_handle *info = (struct stab_write_handle *) p;
  long index;

  switch (size)
    {
    case 1:
      index = -21;
      break;

    case 2:
      index = -22;
      break;

    default:
    case 4:
      index = -16;
      break;

    case 8:
      index = -33;
      break;
    }

  return stab_push_defined_type (info, index, size);
}

/* Push an enum type.  */

static bfd_boolean
stab_enum_type (void *p, const char *tag, const char **names,
		bfd_signed_vma *vals)
{
  struct stab_write_handle *info = (struct stab_write_handle *) p;
  size_t len;
  const char **pn;
  char *buf;
  long index = 0;
  bfd_signed_vma *pv;

  if (names == NULL)
    {
      assert (tag != NULL);

      buf = (char *) xmalloc (10 + strlen (tag));
      sprintf (buf, "xe%s:", tag);
      /* FIXME: The size is just a guess.  */
      if (! stab_push_string (info, buf, 0, FALSE, 4))
	return FALSE;
      free (buf);
      return TRUE;
    }

  len = 10;
  if (tag != NULL)
    len += strlen (tag);
  for (pn = names; *pn != NULL; pn++)
    len += strlen (*pn) + 20;

  buf = (char *) xmalloc (len);

  if (tag == NULL)
    strcpy (buf, "e");
  else
    {
      index = info->type_index;
      ++info->type_index;
      sprintf (buf, "%s:T%ld=e", tag, index);
    }

  for (pn = names, pv = vals; *pn != NULL; pn++, pv++)
    sprintf (buf + strlen (buf), "%s:%ld,", *pn, (long) *pv);
  strcat (buf, ";");

  if (tag == NULL)
    {
      /* FIXME: The size is just a guess.  */
      if (! stab_push_string (info, buf, 0, FALSE, 4))
	return FALSE;
    }
  else
    {
      /* FIXME: The size is just a guess.  */
      if (! stab_write_symbol (info, N_LSYM, 0, 0, buf)
	  || ! stab_push_defined_type (info, index, 4))
	return FALSE;
    }

  free (buf);

  return TRUE;
}

/* Push a modification of the top type on the stack.  Cache the
   results in CACHE and CACHE_ALLOC.  */

static bfd_boolean
stab_modify_type (struct stab_write_handle *info, int mod,
		  unsigned int size, long **cache, size_t *cache_alloc)
{
  long targindex;
  long index;
  char *s, *buf;

  assert (info->type_stack != NULL);
  targindex = info->type_stack->index;

  if (targindex <= 0
      || cache == NULL)
    {
      bfd_boolean definition;

      /* Either the target type has no index, or we aren't caching
         this modifier.  Either way we have no way of recording the
         new type, so we don't bother to define one.  */
      definition = info->type_stack->definition;
      s = stab_pop_type (info);
      buf = (char *) xmalloc (strlen (s) + 2);
      sprintf (buf, "%c%s", mod, s);
      free (s);
      if (! stab_push_string (info, buf, 0, definition, size))
	return FALSE;
      free (buf);
    }
  else
    {
      if ((size_t) targindex >= *cache_alloc)
	{
	  size_t alloc;

	  alloc = *cache_alloc;
	  if (alloc == 0)
	    alloc = 10;
	  while ((size_t) targindex >= alloc)
	    alloc *= 2;
	  *cache = (long *) xrealloc (*cache, alloc * sizeof (long));
	  memset (*cache + *cache_alloc, 0,
		  (alloc - *cache_alloc) * sizeof (long));
	  *cache_alloc = alloc;
	}

      index = (*cache)[targindex];
      if (index != 0 && ! info->type_stack->definition)
	{
	  /* We have already defined a modification of this type, and
             the entry on the type stack is not a definition, so we
             can safely discard it (we may have a definition on the
             stack, even if we already defined a modification, if it
             is a struct which we did not define at the time it was
             referenced).  */
	  free (stab_pop_type (info));
	  if (! stab_push_defined_type (info, index, size))
	    return FALSE;
	}
      else
	{
	  index = info->type_index;
	  ++info->type_index;

	  s = stab_pop_type (info);
	  buf = (char *) xmalloc (strlen (s) + 20);
	  sprintf (buf, "%ld=%c%s", index, mod, s);
	  free (s);

	  (*cache)[targindex] = index;

	  if (! stab_push_string (info, buf, index, TRUE, size))
	    return FALSE;

	  free (buf);
	}
    }

  return TRUE;
}

/* Push a pointer type.  */

static bfd_boolean
stab_pointer_type (void *p)
{
  struct stab_write_handle *info = (struct stab_write_handle *) p;

  /* FIXME: The size should depend upon the architecture.  */
  return stab_modify_type (info, '*', 4, &info->type_cache.pointer_types,
			   &info->type_cache.pointer_types_alloc);
}

/* Push a function type.  */

static bfd_boolean
stab_function_type (void *p, int argcount,
		    bfd_boolean varargs ATTRIBUTE_UNUSED)
{
  struct stab_write_handle *info = (struct stab_write_handle *) p;
  int i;

  /* We have no way to represent the argument types, so we just
     discard them.  However, if they define new types, we must output
     them.  We do this by producing empty typedefs.  */
  for (i = 0; i < argcount; i++)
    {
      if (! info->type_stack->definition)
	free (stab_pop_type (info));
      else
	{
	  char *s, *buf;

	  s = stab_pop_type (info);

	  buf = (char *) xmalloc (strlen (s) + 3);
	  sprintf (buf, ":t%s", s);
	  free (s);

	  if (! stab_write_symbol (info, N_LSYM, 0, 0, buf))
	    return FALSE;

	  free (buf);
	}
    }

  return stab_modify_type (info, 'f', 0, &info->type_cache.function_types,
			   &info->type_cache.function_types_alloc);
}

/* Push a reference type.  */

static bfd_boolean
stab_reference_type (void *p)
{
  struct stab_write_handle *info = (struct stab_write_handle *) p;

  /* FIXME: The size should depend upon the architecture.  */
  return stab_modify_type (info, '&', 4, &info->type_cache.reference_types,
			   &info->type_cache.reference_types_alloc);
}

/* Push a range type.  */

static bfd_boolean
stab_range_type (void *p, bfd_signed_vma low, bfd_signed_vma high)
{
  struct stab_write_handle *info = (struct stab_write_handle *) p;
  bfd_boolean definition;
  unsigned int size;
  char *s, *buf;

  definition = info->type_stack->definition;
  size = info->type_stack->size;

  s = stab_pop_type (info);
  buf = (char *) xmalloc (strlen (s) + 100);
  sprintf (buf, "r%s;%ld;%ld;", s, (long) low, (long) high);
  free (s);

  if (! stab_push_string (info, buf, 0, definition, size))
    return FALSE;

  free (buf);

  return TRUE;
}

/* Push an array type.  */

static bfd_boolean
stab_array_type (void *p, bfd_signed_vma low, bfd_signed_vma high,
		 bfd_boolean stringp)
{
  struct stab_write_handle *info = (struct stab_write_handle *) p;
  bfd_boolean definition;
  unsigned int element_size;
  char *range, *element, *buf;
  long index;
  unsigned int size;

  definition = info->type_stack->definition;
  range = stab_pop_type (info);

  definition = definition || info->type_stack->definition;
  element_size = info->type_stack->size;
  element = stab_pop_type (info);

  buf = (char *) xmalloc (strlen (range) + strlen (element) + 100);

  if (! stringp)
    {
      index = 0;
      *buf = '\0';
    }
  else
    {
      /* We need to define a type in order to include the string
         attribute.  */
      index = info->type_index;
      ++info->type_index;
      definition = TRUE;
      sprintf (buf, "%ld=@S;", index);
    }

  sprintf (buf + strlen (buf), "ar%s;%ld;%ld;%s",
	   range, (long) low, (long) high, element);
  free (range);
  free (element);

  if (high < low)
    size = 0;
  else
    size = element_size * ((high - low) + 1);
  if (! stab_push_string (info, buf, index, definition, size))
    return FALSE;

  free (buf);

  return TRUE;
}

/* Push a set type.  */

static bfd_boolean
stab_set_type (void *p, bfd_boolean bitstringp)
{
  struct stab_write_handle *info = (struct stab_write_handle *) p;
  bfd_boolean definition;
  char *s, *buf;
  long index;

  definition = info->type_stack->definition;

  s = stab_pop_type (info);
  buf = (char *) xmalloc (strlen (s) + 30);

  if (! bitstringp)
    {
      *buf = '\0';
      index = 0;
    }
  else
    {
      /* We need to define a type in order to include the string
         attribute.  */
      index = info->type_index;
      ++info->type_index;
      definition = TRUE;
      sprintf (buf, "%ld=@S;", index);
    }

  sprintf (buf + strlen (buf), "S%s", s);
  free (s);

  if (! stab_push_string (info, buf, index, definition, 0))
    return FALSE;

  free (buf);

  return TRUE;
}

/* Push an offset type.  */

static bfd_boolean
stab_offset_type (void *p)
{
  struct stab_write_handle *info = (struct stab_write_handle *) p;
  bfd_boolean definition;
  char *target, *base, *buf;

  definition = info->type_stack->definition;
  target = stab_pop_type (info);

  definition = definition || info->type_stack->definition;
  base = stab_pop_type (info);

  buf = (char *) xmalloc (strlen (target) + strlen (base) + 3);
  sprintf (buf, "@%s,%s", base, target);
  free (base);
  free (target);

  if (! stab_push_string (info, buf, 0, definition, 0))
    return FALSE;

  free (buf);

  return TRUE;
}

/* Push a method type.  */

static bfd_boolean
stab_method_type (void *p, bfd_boolean domainp, int argcount,
		  bfd_boolean varargs)
{
  struct stab_write_handle *info = (struct stab_write_handle *) p;
  bfd_boolean definition;
  char *domain, *return_type, *buf;
  char **args;
  int i;
  size_t len;

  /* We don't bother with stub method types, because that would
     require a mangler for C++ argument types.  This will waste space
     in the debugging output.  */

  /* We need a domain.  I'm not sure DOMAINP can ever be false,
     anyhow.  */
  if (! domainp)
    {
      if (! stab_empty_type (p))
	return FALSE;
    }

  definition = info->type_stack->definition;
  domain = stab_pop_type (info);

  /* A non-varargs function is indicated by making the last parameter
     type be void.  */

  if (argcount < 0)
    {
      args = NULL;
      argcount = 0;
    }
  else if (argcount == 0)
    {
      if (varargs)
	args = NULL;
      else
	{
	  args = (char **) xmalloc (1 * sizeof (*args));
	  if (! stab_empty_type (p))
	    return FALSE;
	  definition = definition || info->type_stack->definition;
	  args[0] = stab_pop_type (info);
	  argcount = 1;
	}
    }
  else
    {
      args = (char **) xmalloc ((argcount + 1) * sizeof (*args));
      for (i = argcount - 1; i >= 0; i--)
	{
	  definition = definition || info->type_stack->definition;
	  args[i] = stab_pop_type (info);
	}
      if (! varargs)
	{
	  if (! stab_empty_type (p))
	    return FALSE;
	  definition = definition || info->type_stack->definition;
	  args[argcount] = stab_pop_type (info);
	  ++argcount;
	}
    }

  definition = definition || info->type_stack->definition;
  return_type = stab_pop_type (info);

  len = strlen (domain) + strlen (return_type) + 10;
  for (i = 0; i < argcount; i++)
    len += strlen (args[i]);

  buf = (char *) xmalloc (len);

  sprintf (buf, "#%s,%s", domain, return_type);
  free (domain);
  free (return_type);
  for (i = 0; i < argcount; i++)
    {
      strcat (buf, ",");
      strcat (buf, args[i]);
      free (args[i]);
    }
  strcat (buf, ";");

  if (args != NULL)
    free (args);

  if (! stab_push_string (info, buf, 0, definition, 0))
    return FALSE;

  free (buf);

  return TRUE;
}

/* Push a const version of a type.  */

static bfd_boolean
stab_const_type (void *p)
{
  struct stab_write_handle *info = (struct stab_write_handle *) p;

  return stab_modify_type (info, 'k', info->type_stack->size,
			   (long **) NULL, (size_t *) NULL);
}

/* Push a volatile version of a type.  */

static bfd_boolean
stab_volatile_type (void *p)
{
  struct stab_write_handle *info = (struct stab_write_handle *) p;

  return stab_modify_type (info, 'B', info->type_stack->size,
			   (long **) NULL, (size_t *) NULL);
}

/* Get the type index to use for a struct/union/class ID.  This should
   return -1 if it fails.  */

static long
stab_get_struct_index (struct stab_write_handle *info, const char *tag,
		       unsigned int id, enum debug_type_kind kind,
		       unsigned int *psize)
{
  if (id >= info->type_cache.struct_types_alloc)
    {
      size_t alloc;

      alloc = info->type_cache.struct_types_alloc;
      if (alloc == 0)
	alloc = 10;
      while (id >= alloc)
	alloc *= 2;
      info->type_cache.struct_types =
	(struct stab_tag *) xrealloc (info->type_cache.struct_types,
				      alloc * sizeof (struct stab_tag));
      memset ((info->type_cache.struct_types
	       + info->type_cache.struct_types_alloc),
	      0,
	      ((alloc - info->type_cache.struct_types_alloc)
	       * sizeof (struct stab_tag)));
      info->type_cache.struct_types_alloc = alloc;
    }

  if (info->type_cache.struct_types[id].index == 0)
    {
      info->type_cache.struct_types[id].index = info->type_index;
      ++info->type_index;
      info->type_cache.struct_types[id].tag = tag;
      info->type_cache.struct_types[id].kind = kind;
    }

  if (kind == DEBUG_KIND_ILLEGAL)
    {
      /* This is a definition of the struct.  */
      info->type_cache.struct_types[id].kind = kind;
      info->type_cache.struct_types[id].size = *psize;
    }
  else
    *psize = info->type_cache.struct_types[id].size;

  return info->type_cache.struct_types[id].index;
}

/* Start outputting a struct.  We ignore the tag, and handle it in
   stab_tag.  */

static bfd_boolean
stab_start_struct_type (void *p, const char *tag, unsigned int id,
			bfd_boolean structp, unsigned int size)
{
  struct stab_write_handle *info = (struct stab_write_handle *) p;
  long index;
  bfd_boolean definition;
  char *buf;

  buf = (char *) xmalloc (40);

  if (id == 0)
    {
      index = 0;
      *buf = '\0';
      definition = FALSE;
    }
  else
    {
      index = stab_get_struct_index (info, tag, id, DEBUG_KIND_ILLEGAL,
				     &size);
      if (index < 0)
	return FALSE;
      sprintf (buf, "%ld=", index);
      definition = TRUE;
    }

  sprintf (buf + strlen (buf), "%c%u",
	   structp ? 's' : 'u',
	   size);

  if (! stab_push_string (info, buf, index, definition, size))
    return FALSE;

  info->type_stack->fields = (char *) xmalloc (1);
  info->type_stack->fields[0] = '\0';

  return TRUE;
}

/* Add a field to a struct.  */

static bfd_boolean
stab_struct_field (void *p, const char *name, bfd_vma bitpos,
		   bfd_vma bitsize, enum debug_visibility visibility)
{
  struct stab_write_handle *info = (struct stab_write_handle *) p;
  bfd_boolean definition;
  unsigned int size;
  char *s, *n;
  const char *vis;

  definition = info->type_stack->definition;
  size = info->type_stack->size;
  s = stab_pop_type (info);

  /* Add this field to the end of the current struct fields, which is
     currently on the top of the stack.  */

  assert (info->type_stack->fields != NULL);
  n = (char *) xmalloc (strlen (info->type_stack->fields)
			+ strlen (name)
			+ strlen (s)
			+ 50);

  switch (visibility)
    {
    default:
      abort ();

    case DEBUG_VISIBILITY_PUBLIC:
      vis = "";
      break;

    case DEBUG_VISIBILITY_PRIVATE:
      vis = "/0";
      break;

    case DEBUG_VISIBILITY_PROTECTED:
      vis = "/1";
      break;
    }

  if (bitsize == 0)
    {
      bitsize = size * 8;
      if (bitsize == 0)
	non_fatal (_("%s: warning: unknown size for field `%s' in struct"),
		   bfd_get_filename (info->abfd), name);
    }

  sprintf (n, "%s%s:%s%s,%ld,%ld;", info->type_stack->fields, name, vis, s,
	   (long) bitpos, (long) bitsize);

  free (info->type_stack->fields);
  info->type_stack->fields = n;

  if (definition)
    info->type_stack->definition = TRUE;

  return TRUE;
}

/* Finish up a struct.  */

static bfd_boolean
stab_end_struct_type (void *p)
{
  struct stab_write_handle *info = (struct stab_write_handle *) p;
  bfd_boolean definition;
  long index;
  unsigned int size;
  char *fields, *first, *buf;

  assert (info->type_stack != NULL && info->type_stack->fields != NULL);

  definition = info->type_stack->definition;
  index = info->type_stack->index;
  size = info->type_stack->size;
  fields = info->type_stack->fields;
  first = stab_pop_type (info);

  buf = (char *) xmalloc (strlen (first) + strlen (fields) + 2);
  sprintf (buf, "%s%s;", first, fields);
  free (first);
  free (fields);

  if (! stab_push_string (info, buf, index, definition, size))
    return FALSE;

  free (buf);

  return TRUE;
}

/* Start outputting a class.  */

static bfd_boolean
stab_start_class_type (void *p, const char *tag, unsigned int id, bfd_boolean structp, unsigned int size, bfd_boolean vptr, bfd_boolean ownvptr)
{
  struct stab_write_handle *info = (struct stab_write_handle *) p;
  bfd_boolean definition;
  char *vstring;

  if (! vptr || ownvptr)
    {
      definition = FALSE;
      vstring = NULL;
    }
  else
    {
      definition = info->type_stack->definition;
      vstring = stab_pop_type (info);
    }

  if (! stab_start_struct_type (p, tag, id, structp, size))
    return FALSE;

  if (vptr)
    {
      char *vtable;

      if (ownvptr)
	{
	  assert (info->type_stack->index > 0);
	  vtable = (char *) xmalloc (20);
	  sprintf (vtable, "~%%%ld", info->type_stack->index);
	}
      else
	{
	  vtable = (char *) xmalloc (strlen (vstring) + 3);
	  sprintf (vtable, "~%%%s", vstring);
	  free (vstring);
	}

      info->type_stack->vtable = vtable;
    }

  if (definition)
    info->type_stack->definition = TRUE;

  return TRUE;
}

/* Add a static member to the class on the type stack.  */

static bfd_boolean
stab_class_static_member (void *p, const char *name, const char *physname,
			  enum debug_visibility visibility)
{
  struct stab_write_handle *info = (struct stab_write_handle *) p;
  bfd_boolean definition;
  char *s, *n;
  const char *vis;

  definition = info->type_stack->definition;
  s = stab_pop_type (info);

  /* Add this field to the end of the current struct fields, which is
     currently on the top of the stack.  */

  assert (info->type_stack->fields != NULL);
  n = (char *) xmalloc (strlen (info->type_stack->fields)
			+ strlen (name)
			+ strlen (s)
			+ strlen (physname)
			+ 10);

  switch (visibility)
    {
    default:
      abort ();

    case DEBUG_VISIBILITY_PUBLIC:
      vis = "";
      break;

    case DEBUG_VISIBILITY_PRIVATE:
      vis = "/0";
      break;

    case DEBUG_VISIBILITY_PROTECTED:
      vis = "/1";
      break;
    }

  sprintf (n, "%s%s:%s%s:%s;", info->type_stack->fields, name, vis, s,
	   physname);

  free (info->type_stack->fields);
  info->type_stack->fields = n;

  if (definition)
    info->type_stack->definition = TRUE;

  return TRUE;
}

/* Add a base class to the class on the type stack.  */

static bfd_boolean
stab_class_baseclass (void *p, bfd_vma bitpos, bfd_boolean virtual,
		      enum debug_visibility visibility)
{
  struct stab_write_handle *info = (struct stab_write_handle *) p;
  bfd_boolean definition;
  char *s;
  char *buf;
  unsigned int c;
  char **baseclasses;

  definition = info->type_stack->definition;
  s = stab_pop_type (info);

  /* Build the base class specifier.  */

  buf = (char *) xmalloc (strlen (s) + 25);
  buf[0] = virtual ? '1' : '0';
  switch (visibility)
    {
    default:
      abort ();

    case DEBUG_VISIBILITY_PRIVATE:
      buf[1] = '0';
      break;

    case DEBUG_VISIBILITY_PROTECTED:
      buf[1] = '1';
      break;

    case DEBUG_VISIBILITY_PUBLIC:
      buf[1] = '2';
      break;
    }

  sprintf (buf + 2, "%ld,%s;", (long) bitpos, s);
  free (s);

  /* Add the new baseclass to the existing ones.  */

  assert (info->type_stack != NULL && info->type_stack->fields != NULL);

  if (info->type_stack->baseclasses == NULL)
    c = 0;
  else
    {
      c = 0;
      while (info->type_stack->baseclasses[c] != NULL)
	++c;
    }

  baseclasses = (char **) xrealloc (info->type_stack->baseclasses,
				    (c + 2) * sizeof (*baseclasses));
  baseclasses[c] = buf;
  baseclasses[c + 1] = NULL;

  info->type_stack->baseclasses = baseclasses;

  if (definition)
    info->type_stack->definition = TRUE;

  return TRUE;
}

/* Start adding a method to the class on the type stack.  */

static bfd_boolean
stab_class_start_method (void *p, const char *name)
{
  struct stab_write_handle *info = (struct stab_write_handle *) p;
  char *m;

  assert (info->type_stack != NULL && info->type_stack->fields != NULL);

  if (info->type_stack->methods == NULL)
    {
      m = (char *) xmalloc (strlen (name) + 3);
      *m = '\0';
    }
  else
    {
      m = (char *) xrealloc (info->type_stack->methods,
			     (strlen (info->type_stack->methods)
			      + strlen (name)
			      + 4));
    }

  sprintf (m + strlen (m), "%s::", name);

  info->type_stack->methods = m;

  return TRUE;
}

/* Add a variant, either static or not, to the current method.  */

static bfd_boolean
stab_class_method_var (struct stab_write_handle *info, const char *physname,
		       enum debug_visibility visibility,
		       bfd_boolean staticp, bfd_boolean constp,
		       bfd_boolean volatilep, bfd_vma voffset,
		       bfd_boolean contextp)
{
  bfd_boolean definition;
  char *type;
  char *context = NULL;
  char visc, qualc, typec;

  definition = info->type_stack->definition;
  type = stab_pop_type (info);

  if (contextp)
    {
      definition = definition || info->type_stack->definition;
      context = stab_pop_type (info);
    }

  assert (info->type_stack != NULL && info->type_stack->methods != NULL);

  switch (visibility)
    {
    default:
      abort ();

    case DEBUG_VISIBILITY_PRIVATE:
      visc = '0';
      break;

    case DEBUG_VISIBILITY_PROTECTED:
      visc = '1';
      break;

    case DEBUG_VISIBILITY_PUBLIC:
      visc = '2';
      break;
    }

  if (constp)
    {
      if (volatilep)
	qualc = 'D';
      else
	qualc = 'B';
    }
  else
    {
      if (volatilep)
	qualc = 'C';
      else
	qualc = 'A';
    }

  if (staticp)
    typec = '?';
  else if (! contextp)
    typec = '.';
  else
    typec = '*';

  info->type_stack->methods =
    (char *) xrealloc (info->type_stack->methods,
		       (strlen (info->type_stack->methods)
			+ strlen (type)
			+ strlen (physname)
			+ (contextp ? strlen (context) : 0)
			+ 40));

  sprintf (info->type_stack->methods + strlen (info->type_stack->methods),
	   "%s:%s;%c%c%c", type, physname, visc, qualc, typec);
  free (type);

  if (contextp)
    {
      sprintf (info->type_stack->methods + strlen (info->type_stack->methods),
	       "%ld;%s;", (long) voffset, context);
      free (context);
    }

  if (definition)
    info->type_stack->definition = TRUE;

  return TRUE;
}

/* Add a variant to the current method.  */

static bfd_boolean
stab_class_method_variant (void *p, const char *physname,
			   enum debug_visibility visibility,
			   bfd_boolean constp, bfd_boolean volatilep,
			   bfd_vma voffset, bfd_boolean contextp)
{
  struct stab_write_handle *info = (struct stab_write_handle *) p;

  return stab_class_method_var (info, physname, visibility, FALSE, constp,
				volatilep, voffset, contextp);
}

/* Add a static variant to the current method.  */

static bfd_boolean
stab_class_static_method_variant (void *p, const char *physname,
				  enum debug_visibility visibility,
				  bfd_boolean constp, bfd_boolean volatilep)
{
  struct stab_write_handle *info = (struct stab_write_handle *) p;

  return stab_class_method_var (info, physname, visibility, TRUE, constp,
				volatilep, 0, FALSE);
}

/* Finish up a method.  */

static bfd_boolean
stab_class_end_method (void *p)
{
  struct stab_write_handle *info = (struct stab_write_handle *) p;

  assert (info->type_stack != NULL && info->type_stack->methods != NULL);

  /* We allocated enough room on info->type_stack->methods to add the
     trailing semicolon.  */
  strcat (info->type_stack->methods, ";");

  return TRUE;
}

/* Finish up a class.  */

static bfd_boolean
stab_end_class_type (void *p)
{
  struct stab_write_handle *info = (struct stab_write_handle *) p;
  size_t len;
  unsigned int i = 0;
  char *buf;

  assert (info->type_stack != NULL && info->type_stack->fields != NULL);

  /* Work out the size we need to allocate for the class definition.  */

  len = (strlen (info->type_stack->string)
	 + strlen (info->type_stack->fields)
	 + 10);
  if (info->type_stack->baseclasses != NULL)
    {
      len += 20;
      for (i = 0; info->type_stack->baseclasses[i] != NULL; i++)
	len += strlen (info->type_stack->baseclasses[i]);
    }
  if (info->type_stack->methods != NULL)
    len += strlen (info->type_stack->methods);
  if (info->type_stack->vtable != NULL)
    len += strlen (info->type_stack->vtable);

  /* Build the class definition.  */

  buf = (char *) xmalloc (len);

  strcpy (buf, info->type_stack->string);

  if (info->type_stack->baseclasses != NULL)
    {
      sprintf (buf + strlen (buf), "!%u,", i);
      for (i = 0; info->type_stack->baseclasses[i] != NULL; i++)
	{
	  strcat (buf, info->type_stack->baseclasses[i]);
	  free (info->type_stack->baseclasses[i]);
	}
      free (info->type_stack->baseclasses);
      info->type_stack->baseclasses = NULL;
    }

  strcat (buf, info->type_stack->fields);
  free (info->type_stack->fields);
  info->type_stack->fields = NULL;

  if (info->type_stack->methods != NULL)
    {
      strcat (buf, info->type_stack->methods);
      free (info->type_stack->methods);
      info->type_stack->methods = NULL;
    }

  strcat (buf, ";");

  if (info->type_stack->vtable != NULL)
    {
      strcat (buf, info->type_stack->vtable);
      free (info->type_stack->vtable);
      info->type_stack->vtable = NULL;
    }

  /* Replace the string on the top of the stack with the complete
     class definition.  */
  free (info->type_stack->string);
  info->type_stack->string = buf;

  return TRUE;
}

/* Push a typedef which was previously defined.  */

static bfd_boolean
stab_typedef_type (void *p, const char *name)
{
  struct stab_write_handle *info = (struct stab_write_handle *) p;
  struct string_hash_entry *h;

  h = string_hash_lookup (&info->typedef_hash, name, FALSE, FALSE);
  assert (h != NULL && h->index > 0);

  return stab_push_defined_type (info, h->index, h->size);
}

/* Push a struct, union or class tag.  */

static bfd_boolean
stab_tag_type (void *p, const char *name, unsigned int id,
	       enum debug_type_kind kind)
{
  struct stab_write_handle *info = (struct stab_write_handle *) p;
  long index;
  unsigned int size = 0;

  index = stab_get_struct_index (info, name, id, kind, &size);
  if (index < 0)
    return FALSE;

  return stab_push_defined_type (info, index, size);
}

/* Define a typedef.  */

static bfd_boolean
stab_typdef (void *p, const char *name)
{
  struct stab_write_handle *info = (struct stab_write_handle *) p;
  long index;
  unsigned int size;
  char *s, *buf;
  struct string_hash_entry *h;

  index = info->type_stack->index;
  size = info->type_stack->size;
  s = stab_pop_type (info);

  buf = (char *) xmalloc (strlen (name) + strlen (s) + 20);

  if (index > 0)
    sprintf (buf, "%s:t%s", name, s);
  else
    {
      index = info->type_index;
      ++info->type_index;
      sprintf (buf, "%s:t%ld=%s", name, index, s);
    }

  free (s);

  if (! stab_write_symbol (info, N_LSYM, 0, 0, buf))
    return FALSE;

  free (buf);

  h = string_hash_lookup (&info->typedef_hash, name, TRUE, FALSE);
  if (h == NULL)
    {
      non_fatal (_("string_hash_lookup failed: %s"),
		 bfd_errmsg (bfd_get_error ()));
      return FALSE;
    }

  /* I don't think we care about redefinitions.  */

  h->index = index;
  h->size = size;

  return TRUE;
}

/* Define a tag.  */

static bfd_boolean
stab_tag (void *p, const char *tag)
{
  struct stab_write_handle *info = (struct stab_write_handle *) p;
  char *s, *buf;

  s = stab_pop_type (info);

  buf = (char *) xmalloc (strlen (tag) + strlen (s) + 3);

  sprintf (buf, "%s:T%s", tag, s);
  free (s);

  if (! stab_write_symbol (info, N_LSYM, 0, 0, buf))
    return FALSE;

  free (buf);

  return TRUE;
}

/* Define an integer constant.  */

static bfd_boolean
stab_int_constant (void *p, const char *name, bfd_vma val)
{
  struct stab_write_handle *info = (struct stab_write_handle *) p;
  char *buf;

  buf = (char *) xmalloc (strlen (name) + 20);
  sprintf (buf, "%s:c=i%ld", name, (long) val);

  if (! stab_write_symbol (info, N_LSYM, 0, 0, buf))
    return FALSE;

  free (buf);

  return TRUE;
}

/* Define a floating point constant.  */

static bfd_boolean
stab_float_constant (void *p, const char *name, double val)
{
  struct stab_write_handle *info = (struct stab_write_handle *) p;
  char *buf;

  buf = (char *) xmalloc (strlen (name) + 20);
  sprintf (buf, "%s:c=f%g", name, val);

  if (! stab_write_symbol (info, N_LSYM, 0, 0, buf))
    return FALSE;

  free (buf);

  return TRUE;
}

/* Define a typed constant.  */

static bfd_boolean
stab_typed_constant (void *p, const char *name, bfd_vma val)
{
  struct stab_write_handle *info = (struct stab_write_handle *) p;
  char *s, *buf;

  s = stab_pop_type (info);

  buf = (char *) xmalloc (strlen (name) + strlen (s) + 20);
  sprintf (buf, "%s:c=e%s,%ld", name, s, (long) val);
  free (s);

  if (! stab_write_symbol (info, N_LSYM, 0, 0, buf))
    return FALSE;

  free (buf);

  return TRUE;
}

/* Record a variable.  */

static bfd_boolean
stab_variable (void *p, const char *name, enum debug_var_kind kind,
	       bfd_vma val)
{
  struct stab_write_handle *info = (struct stab_write_handle *) p;
  char *s, *buf;
  int stab_type;
  const char *kindstr;

  s = stab_pop_type (info);

  switch (kind)
    {
    default:
      abort ();

    case DEBUG_GLOBAL:
      stab_type = N_GSYM;
      kindstr = "G";
      break;

    case DEBUG_STATIC:
      stab_type = N_STSYM;
      kindstr = "S";
      break;

    case DEBUG_LOCAL_STATIC:
      stab_type = N_STSYM;
      kindstr = "V";
      break;

    case DEBUG_LOCAL:
      stab_type = N_LSYM;
      kindstr = "";

      /* Make sure that this is a type reference or definition.  */
      if (! ISDIGIT (*s))
	{
	  char *n;
	  long index;

	  index = info->type_index;
	  ++info->type_index;
	  n = (char *) xmalloc (strlen (s) + 20);
	  sprintf (n, "%ld=%s", index, s);
	  free (s);
	  s = n;
	}
      break;

    case DEBUG_REGISTER:
      stab_type = N_RSYM;
      kindstr = "r";
      break;
    }

  buf = (char *) xmalloc (strlen (name) + strlen (s) + 3);
  sprintf (buf, "%s:%s%s", name, kindstr, s);
  free (s);

  if (! stab_write_symbol (info, stab_type, 0, val, buf))
    return FALSE;

  free (buf);

  return TRUE;
}

/* Start outputting a function.  */

static bfd_boolean
stab_start_function (void *p, const char *name, bfd_boolean globalp)
{
  struct stab_write_handle *info = (struct stab_write_handle *) p;
  char *rettype, *buf;

  assert (info->nesting == 0 && info->fun_offset == -1);

  rettype = stab_pop_type (info);

  buf = (char *) xmalloc (strlen (name) + strlen (rettype) + 3);
  sprintf (buf, "%s:%c%s", name,
	   globalp ? 'F' : 'f',
	   rettype);

  /* We don't know the value now, so we set it in start_block.  */
  info->fun_offset = info->symbols_size;

  if (! stab_write_symbol (info, N_FUN, 0, 0, buf))
    return FALSE;

  free (buf);

  return TRUE;
}

/* Output a function parameter.  */

static bfd_boolean
stab_function_parameter (void *p, const char *name, enum debug_parm_kind kind, bfd_vma val)
{
  struct stab_write_handle *info = (struct stab_write_handle *) p;
  char *s, *buf;
  int stab_type;
  char kindc;

  s = stab_pop_type (info);

  switch (kind)
    {
    default:
      abort ();

    case DEBUG_PARM_STACK:
      stab_type = N_PSYM;
      kindc = 'p';
      break;

    case DEBUG_PARM_REG:
      stab_type = N_RSYM;
      kindc = 'P';
      break;

    case DEBUG_PARM_REFERENCE:
      stab_type = N_PSYM;
      kindc = 'v';
      break;

    case DEBUG_PARM_REF_REG:
      stab_type = N_RSYM;
      kindc = 'a';
      break;
    }

  buf = (char *) xmalloc (strlen (name) + strlen (s) + 3);
  sprintf (buf, "%s:%c%s", name, kindc, s);
  free (s);

  if (! stab_write_symbol (info, stab_type, 0, val, buf))
    return FALSE;

  free (buf);

  return TRUE;
}

/* Start a block.  */

static bfd_boolean
stab_start_block (void *p, bfd_vma addr)
{
  struct stab_write_handle *info = (struct stab_write_handle *) p;

  /* Fill in any slots which have been waiting for the first known
     text address.  */

  if (info->so_offset != -1)
    {
      bfd_put_32 (info->abfd, addr, info->symbols + info->so_offset + 8);
      info->so_offset = -1;
    }

  if (info->fun_offset != -1)
    {
      bfd_put_32 (info->abfd, addr, info->symbols + info->fun_offset + 8);
      info->fun_offset = -1;
    }

  ++info->nesting;

  /* We will be called with a top level block surrounding the
     function, but stabs information does not output that block, so we
     ignore it.  */

  if (info->nesting == 1)
    {
      info->fnaddr = addr;
      return TRUE;
    }

  /* We have to output the LBRAC symbol after any variables which are
     declared inside the block.  We postpone the LBRAC until the next
     start_block or end_block.  */

  /* If we have postponed an LBRAC, output it now.  */
  if (info->pending_lbrac != (bfd_vma) -1)
    {
      if (! stab_write_symbol (info, N_LBRAC, 0, info->pending_lbrac,
			       (const char *) NULL))
	return FALSE;
    }

  /* Remember the address and output it later.  */

  info->pending_lbrac = addr - info->fnaddr;

  return TRUE;
}

/* End a block.  */

static bfd_boolean
stab_end_block (void *p, bfd_vma addr)
{
  struct stab_write_handle *info = (struct stab_write_handle *) p;

  if (addr > info->last_text_address)
    info->last_text_address = addr;

  /* If we have postponed an LBRAC, output it now.  */
  if (info->pending_lbrac != (bfd_vma) -1)
    {
      if (! stab_write_symbol (info, N_LBRAC, 0, info->pending_lbrac,
			       (const char *) NULL))
	return FALSE;
      info->pending_lbrac = (bfd_vma) -1;
    }

  assert (info->nesting > 0);

  --info->nesting;

  /* We ignore the outermost block.  */
  if (info->nesting == 0)
    return TRUE;

  return stab_write_symbol (info, N_RBRAC, 0, addr - info->fnaddr,
			    (const char *) NULL);
}

/* End a function.  */

static bfd_boolean
stab_end_function (void *p ATTRIBUTE_UNUSED)
{
  return TRUE;
}

/* Output a line number.  */

static bfd_boolean
stab_lineno (void *p, const char *file, unsigned long lineno, bfd_vma addr)
{
  struct stab_write_handle *info = (struct stab_write_handle *) p;

  assert (info->lineno_filename != NULL);

  if (addr > info->last_text_address)
    info->last_text_address = addr;

  if (strcmp (file, info->lineno_filename) != 0)
    {
      if (! stab_write_symbol (info, N_SOL, 0, addr, file))
	return FALSE;
      info->lineno_filename = file;
    }

  return stab_write_symbol (info, N_SLINE, lineno, addr - info->fnaddr,
			    (const char *) NULL);
}
