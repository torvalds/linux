/* Support routines for decoding "stabs" debugging information format.

   Copyright 1986, 1987, 1988, 1989, 1990, 1991, 1992, 1993, 1994,
   1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004 Free
   Software Foundation, Inc.

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

/* Support routines for reading and decoding debugging information in
   the "stabs" format.  This format is used with many systems that use
   the a.out object file format, as well as some systems that use
   COFF or ELF where the stabs data is placed in a special section.
   Avoid placing any object file format specific code in this file. */

#include "defs.h"
#include "gdb_string.h"
#include "bfd.h"
#include "gdb_obstack.h"
#include "symtab.h"
#include "gdbtypes.h"
#include "expression.h"
#include "symfile.h"
#include "objfiles.h"
#include "aout/stab_gnu.h"	/* We always use GNU stabs, not native */
#include "libaout.h"
#include "aout/aout64.h"
#include "gdb-stabs.h"
#include "buildsym.h"
#include "complaints.h"
#include "demangle.h"
#include "language.h"
#include "doublest.h"
#include "cp-abi.h"
#include "cp-support.h"

#include <ctype.h>

/* Ask stabsread.h to define the vars it normally declares `extern'.  */
#define	EXTERN
/**/
#include "stabsread.h"		/* Our own declarations */
#undef	EXTERN

extern void _initialize_stabsread (void);

/* The routines that read and process a complete stabs for a C struct or 
   C++ class pass lists of data member fields and lists of member function
   fields in an instance of a field_info structure, as defined below.
   This is part of some reorganization of low level C++ support and is
   expected to eventually go away... (FIXME) */

struct field_info
  {
    struct nextfield
      {
	struct nextfield *next;

	/* This is the raw visibility from the stab.  It is not checked
	   for being one of the visibilities we recognize, so code which
	   examines this field better be able to deal.  */
	int visibility;

	struct field field;
      }
     *list;
    struct next_fnfieldlist
      {
	struct next_fnfieldlist *next;
	struct fn_fieldlist fn_fieldlist;
      }
     *fnlist;
  };

static void
read_one_struct_field (struct field_info *, char **, char *,
		       struct type *, struct objfile *);

static struct type *dbx_alloc_type (int[2], struct objfile *);

static long read_huge_number (char **, int, int *);

static struct type *error_type (char **, struct objfile *);

static void
patch_block_stabs (struct pending *, struct pending_stabs *,
		   struct objfile *);

static void fix_common_block (struct symbol *, int);

static int read_type_number (char **, int *);

static struct type *read_type (char **, struct objfile *);

static struct type *read_range_type (char **, int[2], struct objfile *);

static struct type *read_sun_builtin_type (char **, int[2], struct objfile *);

static struct type *read_sun_floating_type (char **, int[2],
					    struct objfile *);

static struct type *read_enum_type (char **, struct type *, struct objfile *);

static struct type *rs6000_builtin_type (int);

static int
read_member_functions (struct field_info *, char **, struct type *,
		       struct objfile *);

static int
read_struct_fields (struct field_info *, char **, struct type *,
		    struct objfile *);

static int
read_baseclasses (struct field_info *, char **, struct type *,
		  struct objfile *);

static int
read_tilde_fields (struct field_info *, char **, struct type *,
		   struct objfile *);

static int attach_fn_fields_to_type (struct field_info *, struct type *);

static int attach_fields_to_type (struct field_info *, struct type *,
				  struct objfile *);

static struct type *read_struct_type (char **, struct type *,
                                      enum type_code,
				      struct objfile *);

static struct type *read_array_type (char **, struct type *,
				     struct objfile *);

static struct field *read_args (char **, int, struct objfile *, int *, int *);

static void add_undefined_type (struct type *);

static int
read_cpp_abbrev (struct field_info *, char **, struct type *,
		 struct objfile *);

static char *find_name_end (char *name);

static int process_reference (char **string);

void stabsread_clear_cache (void);

static const char vptr_name[] = "_vptr$";
static const char vb_name[] = "_vb$";

/* Define this as 1 if a pcc declaration of a char or short argument
   gives the correct address.  Otherwise assume pcc gives the
   address of the corresponding int, which is not the same on a
   big-endian machine.  */

#if !defined (BELIEVE_PCC_PROMOTION)
#define BELIEVE_PCC_PROMOTION 0
#endif

static void
invalid_cpp_abbrev_complaint (const char *arg1)
{
  complaint (&symfile_complaints, "invalid C++ abbreviation `%s'", arg1);
}

static void
reg_value_complaint (int arg1, int arg2, const char *arg3)
{
  complaint (&symfile_complaints,
	     "register number %d too large (max %d) in symbol %s", arg1, arg2,
	     arg3);
}

static void
stabs_general_complaint (const char *arg1)
{
  complaint (&symfile_complaints, "%s", arg1);
}

/* Make a list of forward references which haven't been defined.  */

static struct type **undef_types;
static int undef_types_allocated;
static int undef_types_length;
static struct symbol *current_symbol = NULL;

/* Check for and handle cretinous stabs symbol name continuation!  */
#define STABS_CONTINUE(pp,objfile)				\
  do {							\
    if (**(pp) == '\\' || (**(pp) == '?' && (*(pp))[1] == '\0')) \
      *(pp) = next_symbol_text (objfile);	\
  } while (0)


/* Look up a dbx type-number pair.  Return the address of the slot
   where the type for that number-pair is stored.
   The number-pair is in TYPENUMS.

   This can be used for finding the type associated with that pair
   or for associating a new type with the pair.  */

static struct type **
dbx_lookup_type (int typenums[2])
{
  int filenum = typenums[0];
  int index = typenums[1];
  unsigned old_len;
  int real_filenum;
  struct header_file *f;
  int f_orig_length;

  if (filenum == -1)		/* -1,-1 is for temporary types.  */
    return 0;

  if (filenum < 0 || filenum >= n_this_object_header_files)
    {
      complaint (&symfile_complaints,
		 "Invalid symbol data: type number (%d,%d) out of range at symtab pos %d.",
		 filenum, index, symnum);
      goto error_return;
    }

  if (filenum == 0)
    {
      if (index < 0)
	{
	  /* Caller wants address of address of type.  We think
	     that negative (rs6k builtin) types will never appear as
	     "lvalues", (nor should they), so we stuff the real type
	     pointer into a temp, and return its address.  If referenced,
	     this will do the right thing.  */
	  static struct type *temp_type;

	  temp_type = rs6000_builtin_type (index);
	  return &temp_type;
	}

      /* Type is defined outside of header files.
         Find it in this object file's type vector.  */
      if (index >= type_vector_length)
	{
	  old_len = type_vector_length;
	  if (old_len == 0)
	    {
	      type_vector_length = INITIAL_TYPE_VECTOR_LENGTH;
	      type_vector = (struct type **)
		xmalloc (type_vector_length * sizeof (struct type *));
	    }
	  while (index >= type_vector_length)
	    {
	      type_vector_length *= 2;
	    }
	  type_vector = (struct type **)
	    xrealloc ((char *) type_vector,
		      (type_vector_length * sizeof (struct type *)));
	  memset (&type_vector[old_len], 0,
		  (type_vector_length - old_len) * sizeof (struct type *));
	}
      return (&type_vector[index]);
    }
  else
    {
      real_filenum = this_object_header_files[filenum];

      if (real_filenum >= N_HEADER_FILES (current_objfile))
	{
	  struct type *temp_type;
	  struct type **temp_type_p;

	  warning ("GDB internal error: bad real_filenum");

	error_return:
	  temp_type = init_type (TYPE_CODE_ERROR, 0, 0, NULL, NULL);
	  temp_type_p = (struct type **) xmalloc (sizeof (struct type *));
	  *temp_type_p = temp_type;
	  return temp_type_p;
	}

      f = HEADER_FILES (current_objfile) + real_filenum;

      f_orig_length = f->length;
      if (index >= f_orig_length)
	{
	  while (index >= f->length)
	    {
	      f->length *= 2;
	    }
	  f->vector = (struct type **)
	    xrealloc ((char *) f->vector, f->length * sizeof (struct type *));
	  memset (&f->vector[f_orig_length], 0,
		  (f->length - f_orig_length) * sizeof (struct type *));
	}
      return (&f->vector[index]);
    }
}

/* Make sure there is a type allocated for type numbers TYPENUMS
   and return the type object.
   This can create an empty (zeroed) type object.
   TYPENUMS may be (-1, -1) to return a new type object that is not
   put into the type vector, and so may not be referred to by number. */

static struct type *
dbx_alloc_type (int typenums[2], struct objfile *objfile)
{
  struct type **type_addr;

  if (typenums[0] == -1)
    {
      return (alloc_type (objfile));
    }

  type_addr = dbx_lookup_type (typenums);

  /* If we are referring to a type not known at all yet,
     allocate an empty type for it.
     We will fill it in later if we find out how.  */
  if (*type_addr == 0)
    {
      *type_addr = alloc_type (objfile);
    }

  return (*type_addr);
}

/* for all the stabs in a given stab vector, build appropriate types 
   and fix their symbols in given symbol vector. */

static void
patch_block_stabs (struct pending *symbols, struct pending_stabs *stabs,
		   struct objfile *objfile)
{
  int ii;
  char *name;
  char *pp;
  struct symbol *sym;

  if (stabs)
    {

      /* for all the stab entries, find their corresponding symbols and 
         patch their types! */

      for (ii = 0; ii < stabs->count; ++ii)
	{
	  name = stabs->stab[ii];
	  pp = (char *) strchr (name, ':');
	  while (pp[1] == ':')
	    {
	      pp += 2;
	      pp = (char *) strchr (pp, ':');
	    }
	  sym = find_symbol_in_list (symbols, name, pp - name);
	  if (!sym)
	    {
	      /* FIXME-maybe: it would be nice if we noticed whether
	         the variable was defined *anywhere*, not just whether
	         it is defined in this compilation unit.  But neither
	         xlc or GCC seem to need such a definition, and until
	         we do psymtabs (so that the minimal symbols from all
	         compilation units are available now), I'm not sure
	         how to get the information.  */

	      /* On xcoff, if a global is defined and never referenced,
	         ld will remove it from the executable.  There is then
	         a N_GSYM stab for it, but no regular (C_EXT) symbol.  */
	      sym = (struct symbol *)
		obstack_alloc (&objfile->objfile_obstack,
			       sizeof (struct symbol));

	      memset (sym, 0, sizeof (struct symbol));
	      SYMBOL_DOMAIN (sym) = VAR_DOMAIN;
	      SYMBOL_CLASS (sym) = LOC_OPTIMIZED_OUT;
	      DEPRECATED_SYMBOL_NAME (sym) =
		obsavestring (name, pp - name, &objfile->objfile_obstack);
	      pp += 2;
	      if (*(pp - 1) == 'F' || *(pp - 1) == 'f')
		{
		  /* I don't think the linker does this with functions,
		     so as far as I know this is never executed.
		     But it doesn't hurt to check.  */
		  SYMBOL_TYPE (sym) =
		    lookup_function_type (read_type (&pp, objfile));
		}
	      else
		{
		  SYMBOL_TYPE (sym) = read_type (&pp, objfile);
		}
	      add_symbol_to_list (sym, &global_symbols);
	    }
	  else
	    {
	      pp += 2;
	      if (*(pp - 1) == 'F' || *(pp - 1) == 'f')
		{
		  SYMBOL_TYPE (sym) =
		    lookup_function_type (read_type (&pp, objfile));
		}
	      else
		{
		  SYMBOL_TYPE (sym) = read_type (&pp, objfile);
		}
	    }
	}
    }
}


/* Read a number by which a type is referred to in dbx data,
   or perhaps read a pair (FILENUM, TYPENUM) in parentheses.
   Just a single number N is equivalent to (0,N).
   Return the two numbers by storing them in the vector TYPENUMS.
   TYPENUMS will then be used as an argument to dbx_lookup_type.

   Returns 0 for success, -1 for error.  */

static int
read_type_number (char **pp, int *typenums)
{
  int nbits;
  if (**pp == '(')
    {
      (*pp)++;
      typenums[0] = read_huge_number (pp, ',', &nbits);
      if (nbits != 0)
	return -1;
      typenums[1] = read_huge_number (pp, ')', &nbits);
      if (nbits != 0)
	return -1;
    }
  else
    {
      typenums[0] = 0;
      typenums[1] = read_huge_number (pp, 0, &nbits);
      if (nbits != 0)
	return -1;
    }
  return 0;
}


#define VISIBILITY_PRIVATE	'0'	/* Stabs character for private field */
#define VISIBILITY_PROTECTED	'1'	/* Stabs character for protected fld */
#define VISIBILITY_PUBLIC	'2'	/* Stabs character for public field */
#define VISIBILITY_IGNORE	'9'	/* Optimized out or zero length */

/* Structure for storing pointers to reference definitions for fast lookup 
   during "process_later". */

struct ref_map
{
  char *stabs;
  CORE_ADDR value;
  struct symbol *sym;
};

#define MAX_CHUNK_REFS 100
#define REF_CHUNK_SIZE (MAX_CHUNK_REFS * sizeof (struct ref_map))
#define REF_MAP_SIZE(ref_chunk) ((ref_chunk) * REF_CHUNK_SIZE)

static struct ref_map *ref_map;

/* Ptr to free cell in chunk's linked list. */
static int ref_count = 0;

/* Number of chunks malloced. */
static int ref_chunk = 0;

/* This file maintains a cache of stabs aliases found in the symbol
   table. If the symbol table changes, this cache must be cleared
   or we are left holding onto data in invalid obstacks. */
void
stabsread_clear_cache (void)
{
  ref_count = 0;
  ref_chunk = 0;
}

/* Create array of pointers mapping refids to symbols and stab strings.
   Add pointers to reference definition symbols and/or their values as we 
   find them, using their reference numbers as our index. 
   These will be used later when we resolve references. */
void
ref_add (int refnum, struct symbol *sym, char *stabs, CORE_ADDR value)
{
  if (ref_count == 0)
    ref_chunk = 0;
  if (refnum >= ref_count)
    ref_count = refnum + 1;
  if (ref_count > ref_chunk * MAX_CHUNK_REFS)
    {
      int new_slots = ref_count - ref_chunk * MAX_CHUNK_REFS;
      int new_chunks = new_slots / MAX_CHUNK_REFS + 1;
      ref_map = (struct ref_map *)
	xrealloc (ref_map, REF_MAP_SIZE (ref_chunk + new_chunks));
      memset (ref_map + ref_chunk * MAX_CHUNK_REFS, 0, new_chunks * REF_CHUNK_SIZE);
      ref_chunk += new_chunks;
    }
  ref_map[refnum].stabs = stabs;
  ref_map[refnum].sym = sym;
  ref_map[refnum].value = value;
}

/* Return defined sym for the reference REFNUM.  */
struct symbol *
ref_search (int refnum)
{
  if (refnum < 0 || refnum > ref_count)
    return 0;
  return ref_map[refnum].sym;
}

/* Parse a reference id in STRING and return the resulting
   reference number.  Move STRING beyond the reference id.  */

static int
process_reference (char **string)
{
  char *p;
  int refnum = 0;

  if (**string != '#')
    return 0;

  /* Advance beyond the initial '#'.  */
  p = *string + 1;

  /* Read number as reference id. */
  while (*p && isdigit (*p))
    {
      refnum = refnum * 10 + *p - '0';
      p++;
    }
  *string = p;
  return refnum;
}

/* If STRING defines a reference, store away a pointer to the reference 
   definition for later use.  Return the reference number.  */

int
symbol_reference_defined (char **string)
{
  char *p = *string;
  int refnum = 0;

  refnum = process_reference (&p);

  /* Defining symbols end in '=' */
  if (*p == '=')
    {
      /* Symbol is being defined here. */
      *string = p + 1;
      return refnum;
    }
  else
    {
      /* Must be a reference.   Either the symbol has already been defined,
         or this is a forward reference to it.  */
      *string = p;
      return -1;
    }
}

struct symbol *
define_symbol (CORE_ADDR valu, char *string, int desc, int type,
	       struct objfile *objfile)
{
  struct symbol *sym;
  char *p = (char *) find_name_end (string);
  int deftype;
  int synonym = 0;
  int i;

  /* We would like to eliminate nameless symbols, but keep their types.
     E.g. stab entry ":t10=*2" should produce a type 10, which is a pointer
     to type 2, but, should not create a symbol to address that type. Since
     the symbol will be nameless, there is no way any user can refer to it. */

  int nameless;

  /* Ignore syms with empty names.  */
  if (string[0] == 0)
    return 0;

  /* Ignore old-style symbols from cc -go  */
  if (p == 0)
    return 0;

  while (p[1] == ':')
    {
      p += 2;
      p = strchr (p, ':');
    }

  /* If a nameless stab entry, all we need is the type, not the symbol.
     e.g. ":t10=*2" or a nameless enum like " :T16=ered:0,green:1,blue:2,;" */
  nameless = (p == string || ((string[0] == ' ') && (string[1] == ':')));

  current_symbol = sym = (struct symbol *)
    obstack_alloc (&objfile->objfile_obstack, sizeof (struct symbol));
  memset (sym, 0, sizeof (struct symbol));

  switch (type & N_TYPE)
    {
    case N_TEXT:
      SYMBOL_SECTION (sym) = SECT_OFF_TEXT (objfile);
      break;
    case N_DATA:
      SYMBOL_SECTION (sym) = SECT_OFF_DATA (objfile);
      break;
    case N_BSS:
      SYMBOL_SECTION (sym) = SECT_OFF_BSS (objfile);
      break;
    }

  if (processing_gcc_compilation)
    {
      /* GCC 2.x puts the line number in desc.  SunOS apparently puts in the
         number of bytes occupied by a type or object, which we ignore.  */
      SYMBOL_LINE (sym) = desc;
    }
  else
    {
      SYMBOL_LINE (sym) = 0;	/* unknown */
    }

  if (is_cplus_marker (string[0]))
    {
      /* Special GNU C++ names.  */
      switch (string[1])
	{
	case 't':
	  DEPRECATED_SYMBOL_NAME (sym) = obsavestring ("this", strlen ("this"),
					    &objfile->objfile_obstack);
	  break;

	case 'v':		/* $vtbl_ptr_type */
	  /* Was: DEPRECATED_SYMBOL_NAME (sym) = "vptr"; */
	  goto normal;

	case 'e':
	  DEPRECATED_SYMBOL_NAME (sym) = obsavestring ("eh_throw", strlen ("eh_throw"),
					    &objfile->objfile_obstack);
	  break;

	case '_':
	  /* This was an anonymous type that was never fixed up.  */
	  goto normal;

#ifdef STATIC_TRANSFORM_NAME
	case 'X':
	  /* SunPRO (3.0 at least) static variable encoding.  */
	  goto normal;
#endif

	default:
	  complaint (&symfile_complaints, "Unknown C++ symbol name `%s'",
		     string);
	  goto normal;		/* Do *something* with it */
	}
    }
  else
    {
    normal:
      SYMBOL_LANGUAGE (sym) = current_subfile->language;
      SYMBOL_SET_NAMES (sym, string, p - string, objfile);
    }
  p++;

  /* Determine the type of name being defined.  */
#if 0
  /* Getting GDB to correctly skip the symbol on an undefined symbol
     descriptor and not ever dump core is a very dodgy proposition if
     we do things this way.  I say the acorn RISC machine can just
     fix their compiler.  */
  /* The Acorn RISC machine's compiler can put out locals that don't
     start with "234=" or "(3,4)=", so assume anything other than the
     deftypes we know how to handle is a local.  */
  if (!strchr ("cfFGpPrStTvVXCR", *p))
#else
  if (isdigit (*p) || *p == '(' || *p == '-')
#endif
    deftype = 'l';
  else
    deftype = *p++;

  switch (deftype)
    {
    case 'c':
      /* c is a special case, not followed by a type-number.
         SYMBOL:c=iVALUE for an integer constant symbol.
         SYMBOL:c=rVALUE for a floating constant symbol.
         SYMBOL:c=eTYPE,INTVALUE for an enum constant symbol.
         e.g. "b:c=e6,0" for "const b = blob1"
         (where type 6 is defined by "blobs:t6=eblob1:0,blob2:1,;").  */
      if (*p != '=')
	{
	  SYMBOL_CLASS (sym) = LOC_CONST;
	  SYMBOL_TYPE (sym) = error_type (&p, objfile);
	  SYMBOL_DOMAIN (sym) = VAR_DOMAIN;
	  add_symbol_to_list (sym, &file_symbols);
	  return sym;
	}
      ++p;
      switch (*p++)
	{
	case 'r':
	  {
	    double d = atof (p);
	    char *dbl_valu;

	    /* FIXME-if-picky-about-floating-accuracy: Should be using
	       target arithmetic to get the value.  real.c in GCC
	       probably has the necessary code.  */

	    /* FIXME: lookup_fundamental_type is a hack.  We should be
	       creating a type especially for the type of float constants.
	       Problem is, what type should it be?

	       Also, what should the name of this type be?  Should we
	       be using 'S' constants (see stabs.texinfo) instead?  */

	    SYMBOL_TYPE (sym) = lookup_fundamental_type (objfile,
							 FT_DBL_PREC_FLOAT);
	    dbl_valu = (char *)
	      obstack_alloc (&objfile->objfile_obstack,
			     TYPE_LENGTH (SYMBOL_TYPE (sym)));
	    store_typed_floating (dbl_valu, SYMBOL_TYPE (sym), d);
	    SYMBOL_VALUE_BYTES (sym) = dbl_valu;
	    SYMBOL_CLASS (sym) = LOC_CONST_BYTES;
	  }
	  break;
	case 'i':
	  {
	    /* Defining integer constants this way is kind of silly,
	       since 'e' constants allows the compiler to give not
	       only the value, but the type as well.  C has at least
	       int, long, unsigned int, and long long as constant
	       types; other languages probably should have at least
	       unsigned as well as signed constants.  */

	    /* We just need one int constant type for all objfiles.
	       It doesn't depend on languages or anything (arguably its
	       name should be a language-specific name for a type of
	       that size, but I'm inclined to say that if the compiler
	       wants a nice name for the type, it can use 'e').  */
	    static struct type *int_const_type;

	    /* Yes, this is as long as a *host* int.  That is because we
	       use atoi.  */
	    if (int_const_type == NULL)
	      int_const_type =
		init_type (TYPE_CODE_INT,
			   sizeof (int) * HOST_CHAR_BIT / TARGET_CHAR_BIT, 0,
			   "integer constant",
			     (struct objfile *) NULL);
	    SYMBOL_TYPE (sym) = int_const_type;
	    SYMBOL_VALUE (sym) = atoi (p);
	    SYMBOL_CLASS (sym) = LOC_CONST;
	  }
	  break;
	case 'e':
	  /* SYMBOL:c=eTYPE,INTVALUE for a constant symbol whose value
	     can be represented as integral.
	     e.g. "b:c=e6,0" for "const b = blob1"
	     (where type 6 is defined by "blobs:t6=eblob1:0,blob2:1,;").  */
	  {
	    SYMBOL_CLASS (sym) = LOC_CONST;
	    SYMBOL_TYPE (sym) = read_type (&p, objfile);

	    if (*p != ',')
	      {
		SYMBOL_TYPE (sym) = error_type (&p, objfile);
		break;
	      }
	    ++p;

	    /* If the value is too big to fit in an int (perhaps because
	       it is unsigned), or something like that, we silently get
	       a bogus value.  The type and everything else about it is
	       correct.  Ideally, we should be using whatever we have
	       available for parsing unsigned and long long values,
	       however.  */
	    SYMBOL_VALUE (sym) = atoi (p);
	  }
	  break;
	default:
	  {
	    SYMBOL_CLASS (sym) = LOC_CONST;
	    SYMBOL_TYPE (sym) = error_type (&p, objfile);
	  }
	}
      SYMBOL_DOMAIN (sym) = VAR_DOMAIN;
      add_symbol_to_list (sym, &file_symbols);
      return sym;

    case 'C':
      /* The name of a caught exception.  */
      SYMBOL_TYPE (sym) = read_type (&p, objfile);
      SYMBOL_CLASS (sym) = LOC_LABEL;
      SYMBOL_DOMAIN (sym) = VAR_DOMAIN;
      SYMBOL_VALUE_ADDRESS (sym) = valu;
      add_symbol_to_list (sym, &local_symbols);
      break;

    case 'f':
      /* A static function definition.  */
      SYMBOL_TYPE (sym) = read_type (&p, objfile);
      SYMBOL_CLASS (sym) = LOC_BLOCK;
      SYMBOL_DOMAIN (sym) = VAR_DOMAIN;
      add_symbol_to_list (sym, &file_symbols);
      /* fall into process_function_types.  */

    process_function_types:
      /* Function result types are described as the result type in stabs.
         We need to convert this to the function-returning-type-X type
         in GDB.  E.g. "int" is converted to "function returning int".  */
      if (TYPE_CODE (SYMBOL_TYPE (sym)) != TYPE_CODE_FUNC)
	SYMBOL_TYPE (sym) = lookup_function_type (SYMBOL_TYPE (sym));

      /* All functions in C++ have prototypes.  Stabs does not offer an
         explicit way to identify prototyped or unprototyped functions,
         but both GCC and Sun CC emit stabs for the "call-as" type rather
         than the "declared-as" type for unprototyped functions, so
         we treat all functions as if they were prototyped.  This is used
         primarily for promotion when calling the function from GDB.  */
      TYPE_FLAGS (SYMBOL_TYPE (sym)) |= TYPE_FLAG_PROTOTYPED;

      /* fall into process_prototype_types */

    process_prototype_types:
      /* Sun acc puts declared types of arguments here.  */
      if (*p == ';')
	{
	  struct type *ftype = SYMBOL_TYPE (sym);
	  int nsemi = 0;
	  int nparams = 0;
	  char *p1 = p;

	  /* Obtain a worst case guess for the number of arguments
	     by counting the semicolons.  */
	  while (*p1)
	    {
	      if (*p1++ == ';')
		nsemi++;
	    }

	  /* Allocate parameter information fields and fill them in. */
	  TYPE_FIELDS (ftype) = (struct field *)
	    TYPE_ALLOC (ftype, nsemi * sizeof (struct field));
	  while (*p++ == ';')
	    {
	      struct type *ptype;

	      /* A type number of zero indicates the start of varargs.
	         FIXME: GDB currently ignores vararg functions.  */
	      if (p[0] == '0' && p[1] == '\0')
		break;
	      ptype = read_type (&p, objfile);

	      /* The Sun compilers mark integer arguments, which should
	         be promoted to the width of the calling conventions, with
	         a type which references itself. This type is turned into
	         a TYPE_CODE_VOID type by read_type, and we have to turn
	         it back into builtin_type_int here.
	         FIXME: Do we need a new builtin_type_promoted_int_arg ?  */
	      if (TYPE_CODE (ptype) == TYPE_CODE_VOID)
		ptype = builtin_type_int;
	      TYPE_FIELD_TYPE (ftype, nparams) = ptype;
	      TYPE_FIELD_ARTIFICIAL (ftype, nparams++) = 0;
	    }
	  TYPE_NFIELDS (ftype) = nparams;
	  TYPE_FLAGS (ftype) |= TYPE_FLAG_PROTOTYPED;
	}
      break;

    case 'F':
      /* A global function definition.  */
      SYMBOL_TYPE (sym) = read_type (&p, objfile);
      SYMBOL_CLASS (sym) = LOC_BLOCK;
      SYMBOL_DOMAIN (sym) = VAR_DOMAIN;
      add_symbol_to_list (sym, &global_symbols);
      goto process_function_types;

    case 'G':
      /* For a class G (global) symbol, it appears that the
         value is not correct.  It is necessary to search for the
         corresponding linker definition to find the value.
         These definitions appear at the end of the namelist.  */
      SYMBOL_TYPE (sym) = read_type (&p, objfile);
      SYMBOL_CLASS (sym) = LOC_STATIC;
      SYMBOL_DOMAIN (sym) = VAR_DOMAIN;
      /* Don't add symbol references to global_sym_chain.
         Symbol references don't have valid names and wont't match up with
         minimal symbols when the global_sym_chain is relocated.
         We'll fixup symbol references when we fixup the defining symbol.  */
      if (DEPRECATED_SYMBOL_NAME (sym) && DEPRECATED_SYMBOL_NAME (sym)[0] != '#')
	{
	  i = hashname (DEPRECATED_SYMBOL_NAME (sym));
	  SYMBOL_VALUE_CHAIN (sym) = global_sym_chain[i];
	  global_sym_chain[i] = sym;
	}
      add_symbol_to_list (sym, &global_symbols);
      break;

      /* This case is faked by a conditional above,
         when there is no code letter in the dbx data.
         Dbx data never actually contains 'l'.  */
    case 's':
    case 'l':
      SYMBOL_TYPE (sym) = read_type (&p, objfile);
      SYMBOL_CLASS (sym) = LOC_LOCAL;
      SYMBOL_VALUE (sym) = valu;
      SYMBOL_DOMAIN (sym) = VAR_DOMAIN;
      add_symbol_to_list (sym, &local_symbols);
      break;

    case 'p':
      if (*p == 'F')
	/* pF is a two-letter code that means a function parameter in Fortran.
	   The type-number specifies the type of the return value.
	   Translate it into a pointer-to-function type.  */
	{
	  p++;
	  SYMBOL_TYPE (sym)
	    = lookup_pointer_type
	    (lookup_function_type (read_type (&p, objfile)));
	}
      else
	SYMBOL_TYPE (sym) = read_type (&p, objfile);

      SYMBOL_CLASS (sym) = LOC_ARG;
      SYMBOL_VALUE (sym) = valu;
      SYMBOL_DOMAIN (sym) = VAR_DOMAIN;
      add_symbol_to_list (sym, &local_symbols);

      if (TARGET_BYTE_ORDER != BFD_ENDIAN_BIG)
	{
	  /* On little-endian machines, this crud is never necessary,
	     and, if the extra bytes contain garbage, is harmful.  */
	  break;
	}

      /* If it's gcc-compiled, if it says `short', believe it.  */
      if (processing_gcc_compilation || BELIEVE_PCC_PROMOTION)
	break;

      if (!BELIEVE_PCC_PROMOTION)
	{
	  /* This is the signed type which arguments get promoted to.  */
	  static struct type *pcc_promotion_type;
	  /* This is the unsigned type which arguments get promoted to.  */
	  static struct type *pcc_unsigned_promotion_type;

	  /* Call it "int" because this is mainly C lossage.  */
	  if (pcc_promotion_type == NULL)
	    pcc_promotion_type =
	      init_type (TYPE_CODE_INT, TARGET_INT_BIT / TARGET_CHAR_BIT,
			 0, "int", NULL);

	  if (pcc_unsigned_promotion_type == NULL)
	    pcc_unsigned_promotion_type =
	      init_type (TYPE_CODE_INT, TARGET_INT_BIT / TARGET_CHAR_BIT,
			 TYPE_FLAG_UNSIGNED, "unsigned int", NULL);

	  if (BELIEVE_PCC_PROMOTION_TYPE)
	    {
	      /* This is defined on machines (e.g. sparc) where we
	         should believe the type of a PCC 'short' argument,
	         but shouldn't believe the address (the address is the
	         address of the corresponding int).

	         My guess is that this correction, as opposed to
	         changing the parameter to an 'int' (as done below,
	         for PCC on most machines), is the right thing to do
	         on all machines, but I don't want to risk breaking
	         something that already works.  On most PCC machines,
	         the sparc problem doesn't come up because the calling
	         function has to zero the top bytes (not knowing
	         whether the called function wants an int or a short),
	         so there is little practical difference between an
	         int and a short (except perhaps what happens when the
	         GDB user types "print short_arg = 0x10000;").

	         Hacked for SunOS 4.1 by gnu@cygnus.com.  In 4.1, the
	         compiler actually produces the correct address (we
	         don't need to fix it up).  I made this code adapt so
	         that it will offset the symbol if it was pointing at
	         an int-aligned location and not otherwise.  This way
	         you can use the same gdb for 4.0.x and 4.1 systems.

	         If the parameter is shorter than an int, and is
	         integral (e.g. char, short, or unsigned equivalent),
	         and is claimed to be passed on an integer boundary,
	         don't believe it!  Offset the parameter's address to
	         the tail-end of that integer.  */

	      if (TYPE_LENGTH (SYMBOL_TYPE (sym)) < TYPE_LENGTH (pcc_promotion_type)
		  && TYPE_CODE (SYMBOL_TYPE (sym)) == TYPE_CODE_INT
	      && 0 == SYMBOL_VALUE (sym) % TYPE_LENGTH (pcc_promotion_type))
		{
		  SYMBOL_VALUE (sym) += TYPE_LENGTH (pcc_promotion_type)
		    - TYPE_LENGTH (SYMBOL_TYPE (sym));
		}
	      break;
	    }
	  else
	    {
	      /* If PCC says a parameter is a short or a char,
	         it is really an int.  */
	      if (TYPE_LENGTH (SYMBOL_TYPE (sym)) < TYPE_LENGTH (pcc_promotion_type)
		  && TYPE_CODE (SYMBOL_TYPE (sym)) == TYPE_CODE_INT)
		{
		  SYMBOL_TYPE (sym) =
		    TYPE_UNSIGNED (SYMBOL_TYPE (sym))
		    ? pcc_unsigned_promotion_type
		    : pcc_promotion_type;
		}
	      break;
	    }
	}

    case 'P':
      /* acc seems to use P to declare the prototypes of functions that
         are referenced by this file.  gdb is not prepared to deal
         with this extra information.  FIXME, it ought to.  */
      if (type == N_FUN)
	{
	  SYMBOL_TYPE (sym) = read_type (&p, objfile);
	  goto process_prototype_types;
	}
      /*FALLTHROUGH */

    case 'R':
      /* Parameter which is in a register.  */
      SYMBOL_TYPE (sym) = read_type (&p, objfile);
      SYMBOL_CLASS (sym) = LOC_REGPARM;
      SYMBOL_VALUE (sym) = STAB_REG_TO_REGNUM (valu);
      if (SYMBOL_VALUE (sym) >= NUM_REGS + NUM_PSEUDO_REGS)
	{
	  reg_value_complaint (SYMBOL_VALUE (sym),
			       NUM_REGS + NUM_PSEUDO_REGS,
			       SYMBOL_PRINT_NAME (sym));
	  SYMBOL_VALUE (sym) = SP_REGNUM;	/* Known safe, though useless */
	}
      SYMBOL_DOMAIN (sym) = VAR_DOMAIN;
      add_symbol_to_list (sym, &local_symbols);
      break;

    case 'r':
      /* Register variable (either global or local).  */
      SYMBOL_TYPE (sym) = read_type (&p, objfile);
      SYMBOL_CLASS (sym) = LOC_REGISTER;
      SYMBOL_VALUE (sym) = STAB_REG_TO_REGNUM (valu);
      if (SYMBOL_VALUE (sym) >= NUM_REGS + NUM_PSEUDO_REGS)
	{
	  reg_value_complaint (SYMBOL_VALUE (sym),
			       NUM_REGS + NUM_PSEUDO_REGS,
			       SYMBOL_PRINT_NAME (sym));
	  SYMBOL_VALUE (sym) = SP_REGNUM;	/* Known safe, though useless */
	}
      SYMBOL_DOMAIN (sym) = VAR_DOMAIN;
      if (within_function)
	{
	  /* Sun cc uses a pair of symbols, one 'p' and one 'r', with
	     the same name to represent an argument passed in a
	     register.  GCC uses 'P' for the same case.  So if we find
	     such a symbol pair we combine it into one 'P' symbol.
	     For Sun cc we need to do this regardless of
	     stabs_argument_has_addr, because the compiler puts out
	     the 'p' symbol even if it never saves the argument onto
	     the stack.

	     On most machines, we want to preserve both symbols, so
	     that we can still get information about what is going on
	     with the stack (VAX for computing args_printed, using
	     stack slots instead of saved registers in backtraces,
	     etc.).

	     Note that this code illegally combines
	     main(argc) struct foo argc; { register struct foo argc; }
	     but this case is considered pathological and causes a warning
	     from a decent compiler.  */

	  if (local_symbols
	      && local_symbols->nsyms > 0
	      && gdbarch_stabs_argument_has_addr (current_gdbarch,
						  SYMBOL_TYPE (sym)))
	    {
	      struct symbol *prev_sym;
	      prev_sym = local_symbols->symbol[local_symbols->nsyms - 1];
	      if ((SYMBOL_CLASS (prev_sym) == LOC_REF_ARG
		   || SYMBOL_CLASS (prev_sym) == LOC_ARG)
		  && strcmp (DEPRECATED_SYMBOL_NAME (prev_sym),
			     DEPRECATED_SYMBOL_NAME (sym)) == 0)
		{
		  SYMBOL_CLASS (prev_sym) = LOC_REGPARM;
		  /* Use the type from the LOC_REGISTER; that is the type
		     that is actually in that register.  */
		  SYMBOL_TYPE (prev_sym) = SYMBOL_TYPE (sym);
		  SYMBOL_VALUE (prev_sym) = SYMBOL_VALUE (sym);
		  sym = prev_sym;
		  break;
		}
	    }
	  add_symbol_to_list (sym, &local_symbols);
	}
      else
	add_symbol_to_list (sym, &file_symbols);
      break;

    case 'S':
      /* Static symbol at top level of file */
      SYMBOL_TYPE (sym) = read_type (&p, objfile);
      SYMBOL_CLASS (sym) = LOC_STATIC;
      SYMBOL_VALUE_ADDRESS (sym) = valu;
#ifdef STATIC_TRANSFORM_NAME
      if (IS_STATIC_TRANSFORM_NAME (DEPRECATED_SYMBOL_NAME (sym)))
	{
	  struct minimal_symbol *msym;
	  msym = lookup_minimal_symbol (DEPRECATED_SYMBOL_NAME (sym), NULL, objfile);
	  if (msym != NULL)
	    {
	      DEPRECATED_SYMBOL_NAME (sym) = STATIC_TRANSFORM_NAME (DEPRECATED_SYMBOL_NAME (sym));
	      SYMBOL_VALUE_ADDRESS (sym) = SYMBOL_VALUE_ADDRESS (msym);
	    }
	}
#endif
      SYMBOL_DOMAIN (sym) = VAR_DOMAIN;
      add_symbol_to_list (sym, &file_symbols);
      break;

    case 't':
      /* Typedef */
      SYMBOL_TYPE (sym) = read_type (&p, objfile);

      /* For a nameless type, we don't want a create a symbol, thus we
         did not use `sym'. Return without further processing. */
      if (nameless)
	return NULL;

      SYMBOL_CLASS (sym) = LOC_TYPEDEF;
      SYMBOL_VALUE (sym) = valu;
      SYMBOL_DOMAIN (sym) = VAR_DOMAIN;
      /* C++ vagaries: we may have a type which is derived from
         a base type which did not have its name defined when the
         derived class was output.  We fill in the derived class's
         base part member's name here in that case.  */
      if (TYPE_NAME (SYMBOL_TYPE (sym)) != NULL)
	if ((TYPE_CODE (SYMBOL_TYPE (sym)) == TYPE_CODE_STRUCT
	     || TYPE_CODE (SYMBOL_TYPE (sym)) == TYPE_CODE_UNION)
	    && TYPE_N_BASECLASSES (SYMBOL_TYPE (sym)))
	  {
	    int j;
	    for (j = TYPE_N_BASECLASSES (SYMBOL_TYPE (sym)) - 1; j >= 0; j--)
	      if (TYPE_BASECLASS_NAME (SYMBOL_TYPE (sym), j) == 0)
		TYPE_BASECLASS_NAME (SYMBOL_TYPE (sym), j) =
		  type_name_no_tag (TYPE_BASECLASS (SYMBOL_TYPE (sym), j));
	  }

      if (TYPE_NAME (SYMBOL_TYPE (sym)) == NULL)
	{
	  /* gcc-2.6 or later (when using -fvtable-thunks)
	     emits a unique named type for a vtable entry.
	     Some gdb code depends on that specific name. */
	  extern const char vtbl_ptr_name[];

	  if ((TYPE_CODE (SYMBOL_TYPE (sym)) == TYPE_CODE_PTR
	       && strcmp (DEPRECATED_SYMBOL_NAME (sym), vtbl_ptr_name))
	      || TYPE_CODE (SYMBOL_TYPE (sym)) == TYPE_CODE_FUNC)
	    {
	      /* If we are giving a name to a type such as "pointer to
	         foo" or "function returning foo", we better not set
	         the TYPE_NAME.  If the program contains "typedef char
	         *caddr_t;", we don't want all variables of type char
	         * to print as caddr_t.  This is not just a
	         consequence of GDB's type management; PCC and GCC (at
	         least through version 2.4) both output variables of
	         either type char * or caddr_t with the type number
	         defined in the 't' symbol for caddr_t.  If a future
	         compiler cleans this up it GDB is not ready for it
	         yet, but if it becomes ready we somehow need to
	         disable this check (without breaking the PCC/GCC2.4
	         case).

	         Sigh.

	         Fortunately, this check seems not to be necessary
	         for anything except pointers or functions.  */
              /* ezannoni: 2000-10-26. This seems to apply for
		 versions of gcc older than 2.8. This was the original
		 problem: with the following code gdb would tell that
		 the type for name1 is caddr_t, and func is char()
	         typedef char *caddr_t;
		 char *name2;
		 struct x
		 {
		 char *name1;
		 } xx;
		 char *func()
		 {
		 }
		 main () {}
		 */

	      /* Pascal accepts names for pointer types. */
	      if (current_subfile->language == language_pascal)
		{
		  TYPE_NAME (SYMBOL_TYPE (sym)) = DEPRECATED_SYMBOL_NAME (sym);
          	}
	    }
	  else
	    TYPE_NAME (SYMBOL_TYPE (sym)) = DEPRECATED_SYMBOL_NAME (sym);
	}

      add_symbol_to_list (sym, &file_symbols);
      break;

    case 'T':
      /* Struct, union, or enum tag.  For GNU C++, this can be be followed
         by 't' which means we are typedef'ing it as well.  */
      synonym = *p == 't';

      if (synonym)
	p++;

      SYMBOL_TYPE (sym) = read_type (&p, objfile);
 
      /* For a nameless type, we don't want a create a symbol, thus we
         did not use `sym'. Return without further processing. */
      if (nameless)
	return NULL;

      SYMBOL_CLASS (sym) = LOC_TYPEDEF;
      SYMBOL_VALUE (sym) = valu;
      SYMBOL_DOMAIN (sym) = STRUCT_DOMAIN;
      if (TYPE_TAG_NAME (SYMBOL_TYPE (sym)) == 0)
	TYPE_TAG_NAME (SYMBOL_TYPE (sym))
	  = obconcat (&objfile->objfile_obstack, "", "", DEPRECATED_SYMBOL_NAME (sym));
      add_symbol_to_list (sym, &file_symbols);

      if (synonym)
	{
	  /* Clone the sym and then modify it. */
	  struct symbol *typedef_sym = (struct symbol *)
	  obstack_alloc (&objfile->objfile_obstack, sizeof (struct symbol));
	  *typedef_sym = *sym;
	  SYMBOL_CLASS (typedef_sym) = LOC_TYPEDEF;
	  SYMBOL_VALUE (typedef_sym) = valu;
	  SYMBOL_DOMAIN (typedef_sym) = VAR_DOMAIN;
	  if (TYPE_NAME (SYMBOL_TYPE (sym)) == 0)
	    TYPE_NAME (SYMBOL_TYPE (sym))
	      = obconcat (&objfile->objfile_obstack, "", "", DEPRECATED_SYMBOL_NAME (sym));
	  add_symbol_to_list (typedef_sym, &file_symbols);
	}
      break;

    case 'V':
      /* Static symbol of local scope */
      SYMBOL_TYPE (sym) = read_type (&p, objfile);
      SYMBOL_CLASS (sym) = LOC_STATIC;
      SYMBOL_VALUE_ADDRESS (sym) = valu;
#ifdef STATIC_TRANSFORM_NAME
      if (IS_STATIC_TRANSFORM_NAME (DEPRECATED_SYMBOL_NAME (sym)))
	{
	  struct minimal_symbol *msym;
	  msym = lookup_minimal_symbol (DEPRECATED_SYMBOL_NAME (sym), NULL, objfile);
	  if (msym != NULL)
	    {
	      DEPRECATED_SYMBOL_NAME (sym) = STATIC_TRANSFORM_NAME (DEPRECATED_SYMBOL_NAME (sym));
	      SYMBOL_VALUE_ADDRESS (sym) = SYMBOL_VALUE_ADDRESS (msym);
	    }
	}
#endif
      SYMBOL_DOMAIN (sym) = VAR_DOMAIN;
	add_symbol_to_list (sym, &local_symbols);
      break;

    case 'v':
      /* Reference parameter */
      SYMBOL_TYPE (sym) = read_type (&p, objfile);
      SYMBOL_CLASS (sym) = LOC_REF_ARG;
      SYMBOL_VALUE (sym) = valu;
      SYMBOL_DOMAIN (sym) = VAR_DOMAIN;
      add_symbol_to_list (sym, &local_symbols);
      break;

    case 'a':
      /* Reference parameter which is in a register.  */
      SYMBOL_TYPE (sym) = read_type (&p, objfile);
      SYMBOL_CLASS (sym) = LOC_REGPARM_ADDR;
      SYMBOL_VALUE (sym) = STAB_REG_TO_REGNUM (valu);
      if (SYMBOL_VALUE (sym) >= NUM_REGS + NUM_PSEUDO_REGS)
	{
	  reg_value_complaint (SYMBOL_VALUE (sym),
			       NUM_REGS + NUM_PSEUDO_REGS,
			       SYMBOL_PRINT_NAME (sym));
	  SYMBOL_VALUE (sym) = SP_REGNUM;	/* Known safe, though useless */
	}
      SYMBOL_DOMAIN (sym) = VAR_DOMAIN;
      add_symbol_to_list (sym, &local_symbols);
      break;

    case 'X':
      /* This is used by Sun FORTRAN for "function result value".
         Sun claims ("dbx and dbxtool interfaces", 2nd ed)
         that Pascal uses it too, but when I tried it Pascal used
         "x:3" (local symbol) instead.  */
      SYMBOL_TYPE (sym) = read_type (&p, objfile);
      SYMBOL_CLASS (sym) = LOC_LOCAL;
      SYMBOL_VALUE (sym) = valu;
      SYMBOL_DOMAIN (sym) = VAR_DOMAIN;
      add_symbol_to_list (sym, &local_symbols);
      break;

    default:
      SYMBOL_TYPE (sym) = error_type (&p, objfile);
      SYMBOL_CLASS (sym) = LOC_CONST;
      SYMBOL_VALUE (sym) = 0;
      SYMBOL_DOMAIN (sym) = VAR_DOMAIN;
      add_symbol_to_list (sym, &file_symbols);
      break;
    }

  /* Some systems pass variables of certain types by reference instead
     of by value, i.e. they will pass the address of a structure (in a
     register or on the stack) instead of the structure itself.  */

  if (gdbarch_stabs_argument_has_addr (current_gdbarch, SYMBOL_TYPE (sym))
      && (SYMBOL_CLASS (sym) == LOC_REGPARM || SYMBOL_CLASS (sym) == LOC_ARG))
    {
      /* We have to convert LOC_REGPARM to LOC_REGPARM_ADDR (for
         variables passed in a register).  */
      if (SYMBOL_CLASS (sym) == LOC_REGPARM)
	SYMBOL_CLASS (sym) = LOC_REGPARM_ADDR;
      /* Likewise for converting LOC_ARG to LOC_REF_ARG (for the 7th
	 and subsequent arguments on SPARC, for example).  */
      else if (SYMBOL_CLASS (sym) == LOC_ARG)
	SYMBOL_CLASS (sym) = LOC_REF_ARG;
    }

  return sym;
}

/* Skip rest of this symbol and return an error type.

   General notes on error recovery:  error_type always skips to the
   end of the symbol (modulo cretinous dbx symbol name continuation).
   Thus code like this:

   if (*(*pp)++ != ';')
   return error_type (pp, objfile);

   is wrong because if *pp starts out pointing at '\0' (typically as the
   result of an earlier error), it will be incremented to point to the
   start of the next symbol, which might produce strange results, at least
   if you run off the end of the string table.  Instead use

   if (**pp != ';')
   return error_type (pp, objfile);
   ++*pp;

   or

   if (**pp != ';')
   foo = error_type (pp, objfile);
   else
   ++*pp;

   And in case it isn't obvious, the point of all this hair is so the compiler
   can define new types and new syntaxes, and old versions of the
   debugger will be able to read the new symbol tables.  */

static struct type *
error_type (char **pp, struct objfile *objfile)
{
  complaint (&symfile_complaints, "couldn't parse type; debugger out of date?");
  while (1)
    {
      /* Skip to end of symbol.  */
      while (**pp != '\0')
	{
	  (*pp)++;
	}

      /* Check for and handle cretinous dbx symbol name continuation!  */
      if ((*pp)[-1] == '\\' || (*pp)[-1] == '?')
	{
	  *pp = next_symbol_text (objfile);
	}
      else
	{
	  break;
	}
    }
  return (builtin_type_error);
}


/* Read type information or a type definition; return the type.  Even
   though this routine accepts either type information or a type
   definition, the distinction is relevant--some parts of stabsread.c
   assume that type information starts with a digit, '-', or '(' in
   deciding whether to call read_type.  */

static struct type *
read_type (char **pp, struct objfile *objfile)
{
  struct type *type = 0;
  struct type *type1;
  int typenums[2];
  char type_descriptor;

  /* Size in bits of type if specified by a type attribute, or -1 if
     there is no size attribute.  */
  int type_size = -1;

  /* Used to distinguish string and bitstring from char-array and set. */
  int is_string = 0;

  /* Used to distinguish vector from array. */
  int is_vector = 0;

  /* Read type number if present.  The type number may be omitted.
     for instance in a two-dimensional array declared with type
     "ar1;1;10;ar1;1;10;4".  */
  if ((**pp >= '0' && **pp <= '9')
      || **pp == '('
      || **pp == '-')
    {
      if (read_type_number (pp, typenums) != 0)
	return error_type (pp, objfile);

      if (**pp != '=')
        {
          /* Type is not being defined here.  Either it already
             exists, or this is a forward reference to it.
             dbx_alloc_type handles both cases.  */
          type = dbx_alloc_type (typenums, objfile);

          /* If this is a forward reference, arrange to complain if it
             doesn't get patched up by the time we're done
             reading.  */
          if (TYPE_CODE (type) == TYPE_CODE_UNDEF)
            add_undefined_type (type);

          return type;
        }

      /* Type is being defined here.  */
      /* Skip the '='.
         Also skip the type descriptor - we get it below with (*pp)[-1].  */
      (*pp) += 2;
    }
  else
    {
      /* 'typenums=' not present, type is anonymous.  Read and return
         the definition, but don't put it in the type vector.  */
      typenums[0] = typenums[1] = -1;
      (*pp)++;
    }

again:
  type_descriptor = (*pp)[-1];
  switch (type_descriptor)
    {
    case 'x':
      {
	enum type_code code;

	/* Used to index through file_symbols.  */
	struct pending *ppt;
	int i;

	/* Name including "struct", etc.  */
	char *type_name;

	{
	  char *from, *to, *p, *q1, *q2;

	  /* Set the type code according to the following letter.  */
	  switch ((*pp)[0])
	    {
	    case 's':
	      code = TYPE_CODE_STRUCT;
	      break;
	    case 'u':
	      code = TYPE_CODE_UNION;
	      break;
	    case 'e':
	      code = TYPE_CODE_ENUM;
	      break;
	    default:
	      {
		/* Complain and keep going, so compilers can invent new
		   cross-reference types.  */
		complaint (&symfile_complaints,
			   "Unrecognized cross-reference type `%c'", (*pp)[0]);
		code = TYPE_CODE_STRUCT;
		break;
	      }
	    }

	  q1 = strchr (*pp, '<');
	  p = strchr (*pp, ':');
	  if (p == NULL)
	    return error_type (pp, objfile);
	  if (q1 && p > q1 && p[1] == ':')
	    {
	      int nesting_level = 0;
	      for (q2 = q1; *q2; q2++)
		{
		  if (*q2 == '<')
		    nesting_level++;
		  else if (*q2 == '>')
		    nesting_level--;
		  else if (*q2 == ':' && nesting_level == 0)
		    break;
		}
	      p = q2;
	      if (*p != ':')
		return error_type (pp, objfile);
	    }
	  to = type_name =
	    (char *) obstack_alloc (&objfile->objfile_obstack, p - *pp + 1);

	  /* Copy the name.  */
	  from = *pp + 1;
	  while (from < p)
	    *to++ = *from++;
	  *to = '\0';

	  /* Set the pointer ahead of the name which we just read, and
	     the colon.  */
	  *pp = from + 1;
	}

        /* If this type has already been declared, then reuse the same
           type, rather than allocating a new one.  This saves some
           memory.  */

	for (ppt = file_symbols; ppt; ppt = ppt->next)
	  for (i = 0; i < ppt->nsyms; i++)
	    {
	      struct symbol *sym = ppt->symbol[i];

	      if (SYMBOL_CLASS (sym) == LOC_TYPEDEF
		  && SYMBOL_DOMAIN (sym) == STRUCT_DOMAIN
		  && (TYPE_CODE (SYMBOL_TYPE (sym)) == code)
		  && strcmp (DEPRECATED_SYMBOL_NAME (sym), type_name) == 0)
		{
		  obstack_free (&objfile->objfile_obstack, type_name);
		  type = SYMBOL_TYPE (sym);
	          if (typenums[0] != -1)
	            *dbx_lookup_type (typenums) = type;
		  return type;
		}
	    }

	/* Didn't find the type to which this refers, so we must
	   be dealing with a forward reference.  Allocate a type
	   structure for it, and keep track of it so we can
	   fill in the rest of the fields when we get the full
	   type.  */
	type = dbx_alloc_type (typenums, objfile);
	TYPE_CODE (type) = code;
	TYPE_TAG_NAME (type) = type_name;
	INIT_CPLUS_SPECIFIC (type);
	TYPE_FLAGS (type) |= TYPE_FLAG_STUB;

	add_undefined_type (type);
	return type;
      }

    case '-':			/* RS/6000 built-in type */
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
    case '(':
      (*pp)--;

      /* We deal with something like t(1,2)=(3,4)=... which
         the Lucid compiler and recent gcc versions (post 2.7.3) use. */

      /* Allocate and enter the typedef type first.
         This handles recursive types. */
      type = dbx_alloc_type (typenums, objfile);
      TYPE_CODE (type) = TYPE_CODE_TYPEDEF;
      {
	struct type *xtype = read_type (pp, objfile);
	if (type == xtype)
	  {
	    /* It's being defined as itself.  That means it is "void".  */
	    TYPE_CODE (type) = TYPE_CODE_VOID;
	    TYPE_LENGTH (type) = 1;
	  }
	else if (type_size >= 0 || is_string)
	  {
	    /* This is the absolute wrong way to construct types.  Every
	       other debug format has found a way around this problem and
	       the related problems with unnecessarily stubbed types;
	       someone motivated should attempt to clean up the issue
	       here as well.  Once a type pointed to has been created it
	       should not be modified.

               Well, it's not *absolutely* wrong.  Constructing recursive
               types (trees, linked lists) necessarily entails modifying
               types after creating them.  Constructing any loop structure
               entails side effects.  The Dwarf 2 reader does handle this
               more gracefully (it never constructs more than once
               instance of a type object, so it doesn't have to copy type
               objects wholesale), but it still mutates type objects after
               other folks have references to them.

               Keep in mind that this circularity/mutation issue shows up
               at the source language level, too: C's "incomplete types",
               for example.  So the proper cleanup, I think, would be to
               limit GDB's type smashing to match exactly those required
               by the source language.  So GDB could have a
               "complete_this_type" function, but never create unnecessary
               copies of a type otherwise.  */
	    replace_type (type, xtype);
	    TYPE_NAME (type) = NULL;
	    TYPE_TAG_NAME (type) = NULL;
	  }
	else
	  {
	    TYPE_FLAGS (type) |= TYPE_FLAG_TARGET_STUB;
	    TYPE_TARGET_TYPE (type) = xtype;
	  }
      }
      break;

      /* In the following types, we must be sure to overwrite any existing
         type that the typenums refer to, rather than allocating a new one
         and making the typenums point to the new one.  This is because there
         may already be pointers to the existing type (if it had been
         forward-referenced), and we must change it to a pointer, function,
         reference, or whatever, *in-place*.  */

    case '*':			/* Pointer to another type */
      type1 = read_type (pp, objfile);
      type = make_pointer_type (type1, dbx_lookup_type (typenums));
      break;

    case '&':			/* Reference to another type */
      type1 = read_type (pp, objfile);
      type = make_reference_type (type1, dbx_lookup_type (typenums));
      break;

    case 'f':			/* Function returning another type */
      type1 = read_type (pp, objfile);
      type = make_function_type (type1, dbx_lookup_type (typenums));
      break;

    case 'g':                   /* Prototyped function.  (Sun)  */
      {
        /* Unresolved questions:

           - According to Sun's ``STABS Interface Manual'', for 'f'
           and 'F' symbol descriptors, a `0' in the argument type list
           indicates a varargs function.  But it doesn't say how 'g'
           type descriptors represent that info.  Someone with access
           to Sun's toolchain should try it out.

           - According to the comment in define_symbol (search for
           `process_prototype_types:'), Sun emits integer arguments as
           types which ref themselves --- like `void' types.  Do we
           have to deal with that here, too?  Again, someone with
           access to Sun's toolchain should try it out and let us
           know.  */

        const char *type_start = (*pp) - 1;
        struct type *return_type = read_type (pp, objfile);
        struct type *func_type
          = make_function_type (return_type, dbx_lookup_type (typenums));
        struct type_list {
          struct type *type;
          struct type_list *next;
        } *arg_types = 0;
        int num_args = 0;

        while (**pp && **pp != '#')
          {
            struct type *arg_type = read_type (pp, objfile);
            struct type_list *new = alloca (sizeof (*new));
            new->type = arg_type;
            new->next = arg_types;
            arg_types = new;
            num_args++;
          }
        if (**pp == '#')
          ++*pp;
        else
          {
	    complaint (&symfile_complaints,
		       "Prototyped function type didn't end arguments with `#':\n%s",
		       type_start);
          }

        /* If there is just one argument whose type is `void', then
           that's just an empty argument list.  */
        if (arg_types
            && ! arg_types->next
            && TYPE_CODE (arg_types->type) == TYPE_CODE_VOID)
          num_args = 0;

        TYPE_FIELDS (func_type)
          = (struct field *) TYPE_ALLOC (func_type,
                                         num_args * sizeof (struct field));
        memset (TYPE_FIELDS (func_type), 0, num_args * sizeof (struct field));
        {
          int i;
          struct type_list *t;

          /* We stuck each argument type onto the front of the list
             when we read it, so the list is reversed.  Build the
             fields array right-to-left.  */
          for (t = arg_types, i = num_args - 1; t; t = t->next, i--)
            TYPE_FIELD_TYPE (func_type, i) = t->type;
        }
        TYPE_NFIELDS (func_type) = num_args;
        TYPE_FLAGS (func_type) |= TYPE_FLAG_PROTOTYPED;

        type = func_type;
        break;
      }

    case 'k':			/* Const qualifier on some type (Sun) */
      type = read_type (pp, objfile);
      type = make_cvr_type (1, TYPE_VOLATILE (type), TYPE_RESTRICT(type), type,
			   dbx_lookup_type (typenums));
      break;

    case 'B':			/* Volatile qual on some type (Sun) */
      type = read_type (pp, objfile);
      type = make_cvr_type (TYPE_CONST (type), 1, TYPE_RESTRICT(type), type,
			   dbx_lookup_type (typenums));
      break;

    case '@':
      if (isdigit (**pp) || **pp == '(' || **pp == '-')
	{			/* Member (class & variable) type */
	  /* FIXME -- we should be doing smash_to_XXX types here.  */

	  struct type *domain = read_type (pp, objfile);
	  struct type *memtype;

	  if (**pp != ',')
	    /* Invalid member type data format.  */
	    return error_type (pp, objfile);
	  ++*pp;

	  memtype = read_type (pp, objfile);
	  type = dbx_alloc_type (typenums, objfile);
	  smash_to_member_type (type, domain, memtype);
	}
      else
	/* type attribute */
	{
	  char *attr = *pp;
	  /* Skip to the semicolon.  */
	  while (**pp != ';' && **pp != '\0')
	    ++(*pp);
	  if (**pp == '\0')
	    return error_type (pp, objfile);
	  else
	    ++ * pp;		/* Skip the semicolon.  */

	  switch (*attr)
	    {
	    case 's':		/* Size attribute */
	      type_size = atoi (attr + 1);
	      if (type_size <= 0)
		type_size = -1;
	      break;

	    case 'S':		/* String attribute */
	      /* FIXME: check to see if following type is array? */
	      is_string = 1;
	      break;

	    case 'V':		/* Vector attribute */
	      /* FIXME: check to see if following type is array? */
	      is_vector = 1;
	      break;

	    default:
	      /* Ignore unrecognized type attributes, so future compilers
	         can invent new ones.  */
	      break;
	    }
	  ++*pp;
	  goto again;
	}
      break;

    case '#':			/* Method (class & fn) type */
      if ((*pp)[0] == '#')
	{
	  /* We'll get the parameter types from the name.  */
	  struct type *return_type;

	  (*pp)++;
	  return_type = read_type (pp, objfile);
	  if (*(*pp)++ != ';')
	    complaint (&symfile_complaints,
		       "invalid (minimal) member type data format at symtab pos %d.",
		       symnum);
	  type = allocate_stub_method (return_type);
	  if (typenums[0] != -1)
	    *dbx_lookup_type (typenums) = type;
	}
      else
	{
	  struct type *domain = read_type (pp, objfile);
	  struct type *return_type;
	  struct field *args;
	  int nargs, varargs;

	  if (**pp != ',')
	    /* Invalid member type data format.  */
	    return error_type (pp, objfile);
	  else
	    ++(*pp);

	  return_type = read_type (pp, objfile);
	  args = read_args (pp, ';', objfile, &nargs, &varargs);
	  type = dbx_alloc_type (typenums, objfile);
	  smash_to_method_type (type, domain, return_type, args,
				nargs, varargs);
	}
      break;

    case 'r':			/* Range type */
      type = read_range_type (pp, typenums, objfile);
      if (typenums[0] != -1)
	*dbx_lookup_type (typenums) = type;
      break;

    case 'b':
	{
	  /* Sun ACC builtin int type */
	  type = read_sun_builtin_type (pp, typenums, objfile);
	  if (typenums[0] != -1)
	    *dbx_lookup_type (typenums) = type;
	}
      break;

    case 'R':			/* Sun ACC builtin float type */
      type = read_sun_floating_type (pp, typenums, objfile);
      if (typenums[0] != -1)
	*dbx_lookup_type (typenums) = type;
      break;

    case 'e':			/* Enumeration type */
      type = dbx_alloc_type (typenums, objfile);
      type = read_enum_type (pp, type, objfile);
      if (typenums[0] != -1)
	*dbx_lookup_type (typenums) = type;
      break;

    case 's':			/* Struct type */
    case 'u':			/* Union type */
      {
        enum type_code type_code = TYPE_CODE_UNDEF;
        type = dbx_alloc_type (typenums, objfile);
        switch (type_descriptor)
          {
          case 's':
            type_code = TYPE_CODE_STRUCT;
            break;
          case 'u':
            type_code = TYPE_CODE_UNION;
            break;
          }
        type = read_struct_type (pp, type, type_code, objfile);
        break;
      }

    case 'a':			/* Array type */
      if (**pp != 'r')
	return error_type (pp, objfile);
      ++*pp;

      type = dbx_alloc_type (typenums, objfile);
      type = read_array_type (pp, type, objfile);
      if (is_string)
	TYPE_CODE (type) = TYPE_CODE_STRING;
      if (is_vector)
	TYPE_FLAGS (type) |= TYPE_FLAG_VECTOR;
      break;

    case 'S':			/* Set or bitstring  type */
      type1 = read_type (pp, objfile);
      type = create_set_type ((struct type *) NULL, type1);
      if (is_string)
	TYPE_CODE (type) = TYPE_CODE_BITSTRING;
      if (typenums[0] != -1)
	*dbx_lookup_type (typenums) = type;
      break;

    default:
      --*pp;			/* Go back to the symbol in error */
      /* Particularly important if it was \0! */
      return error_type (pp, objfile);
    }

  if (type == 0)
    {
      warning ("GDB internal error, type is NULL in stabsread.c\n");
      return error_type (pp, objfile);
    }

  /* Size specified in a type attribute overrides any other size.  */
  if (type_size != -1)
    TYPE_LENGTH (type) = (type_size + TARGET_CHAR_BIT - 1) / TARGET_CHAR_BIT;

  return type;
}

/* RS/6000 xlc/dbx combination uses a set of builtin types, starting from -1.
   Return the proper type node for a given builtin type number. */

static struct type *
rs6000_builtin_type (int typenum)
{
  /* We recognize types numbered from -NUMBER_RECOGNIZED to -1.  */
#define NUMBER_RECOGNIZED 34
  /* This includes an empty slot for type number -0.  */
  static struct type *negative_types[NUMBER_RECOGNIZED + 1];
  struct type *rettype = NULL;

  if (typenum >= 0 || typenum < -NUMBER_RECOGNIZED)
    {
      complaint (&symfile_complaints, "Unknown builtin type %d", typenum);
      return builtin_type_error;
    }
  if (negative_types[-typenum] != NULL)
    return negative_types[-typenum];

#if TARGET_CHAR_BIT != 8
#error This code wrong for TARGET_CHAR_BIT not 8
  /* These definitions all assume that TARGET_CHAR_BIT is 8.  I think
     that if that ever becomes not true, the correct fix will be to
     make the size in the struct type to be in bits, not in units of
     TARGET_CHAR_BIT.  */
#endif

  switch (-typenum)
    {
    case 1:
      /* The size of this and all the other types are fixed, defined
         by the debugging format.  If there is a type called "int" which
         is other than 32 bits, then it should use a new negative type
         number (or avoid negative type numbers for that case).
         See stabs.texinfo.  */
      rettype = init_type (TYPE_CODE_INT, 4, 0, "int", NULL);
      break;
    case 2:
      rettype = init_type (TYPE_CODE_INT, 1, 0, "char", NULL);
      break;
    case 3:
      rettype = init_type (TYPE_CODE_INT, 2, 0, "short", NULL);
      break;
    case 4:
      rettype = init_type (TYPE_CODE_INT, 4, 0, "long", NULL);
      break;
    case 5:
      rettype = init_type (TYPE_CODE_INT, 1, TYPE_FLAG_UNSIGNED,
			   "unsigned char", NULL);
      break;
    case 6:
      rettype = init_type (TYPE_CODE_INT, 1, 0, "signed char", NULL);
      break;
    case 7:
      rettype = init_type (TYPE_CODE_INT, 2, TYPE_FLAG_UNSIGNED,
			   "unsigned short", NULL);
      break;
    case 8:
      rettype = init_type (TYPE_CODE_INT, 4, TYPE_FLAG_UNSIGNED,
			   "unsigned int", NULL);
      break;
    case 9:
      rettype = init_type (TYPE_CODE_INT, 4, TYPE_FLAG_UNSIGNED,
			   "unsigned", NULL);
    case 10:
      rettype = init_type (TYPE_CODE_INT, 4, TYPE_FLAG_UNSIGNED,
			   "unsigned long", NULL);
      break;
    case 11:
      rettype = init_type (TYPE_CODE_VOID, 1, 0, "void", NULL);
      break;
    case 12:
      /* IEEE single precision (32 bit).  */
      rettype = init_type (TYPE_CODE_FLT, 4, 0, "float", NULL);
      break;
    case 13:
      /* IEEE double precision (64 bit).  */
      rettype = init_type (TYPE_CODE_FLT, 8, 0, "double", NULL);
      break;
    case 14:
      /* This is an IEEE double on the RS/6000, and different machines with
         different sizes for "long double" should use different negative
         type numbers.  See stabs.texinfo.  */
      rettype = init_type (TYPE_CODE_FLT, 8, 0, "long double", NULL);
      break;
    case 15:
      rettype = init_type (TYPE_CODE_INT, 4, 0, "integer", NULL);
      break;
    case 16:
      rettype = init_type (TYPE_CODE_BOOL, 4, TYPE_FLAG_UNSIGNED,
			   "boolean", NULL);
      break;
    case 17:
      rettype = init_type (TYPE_CODE_FLT, 4, 0, "short real", NULL);
      break;
    case 18:
      rettype = init_type (TYPE_CODE_FLT, 8, 0, "real", NULL);
      break;
    case 19:
      rettype = init_type (TYPE_CODE_ERROR, 0, 0, "stringptr", NULL);
      break;
    case 20:
      rettype = init_type (TYPE_CODE_CHAR, 1, TYPE_FLAG_UNSIGNED,
			   "character", NULL);
      break;
    case 21:
      rettype = init_type (TYPE_CODE_BOOL, 1, TYPE_FLAG_UNSIGNED,
			   "logical*1", NULL);
      break;
    case 22:
      rettype = init_type (TYPE_CODE_BOOL, 2, TYPE_FLAG_UNSIGNED,
			   "logical*2", NULL);
      break;
    case 23:
      rettype = init_type (TYPE_CODE_BOOL, 4, TYPE_FLAG_UNSIGNED,
			   "logical*4", NULL);
      break;
    case 24:
      rettype = init_type (TYPE_CODE_BOOL, 4, TYPE_FLAG_UNSIGNED,
			   "logical", NULL);
      break;
    case 25:
      /* Complex type consisting of two IEEE single precision values.  */
      rettype = init_type (TYPE_CODE_COMPLEX, 8, 0, "complex", NULL);
      TYPE_TARGET_TYPE (rettype) = init_type (TYPE_CODE_FLT, 4, 0, "float",
					      NULL);
      break;
    case 26:
      /* Complex type consisting of two IEEE double precision values.  */
      rettype = init_type (TYPE_CODE_COMPLEX, 16, 0, "double complex", NULL);
      TYPE_TARGET_TYPE (rettype) = init_type (TYPE_CODE_FLT, 8, 0, "double",
					      NULL);
      break;
    case 27:
      rettype = init_type (TYPE_CODE_INT, 1, 0, "integer*1", NULL);
      break;
    case 28:
      rettype = init_type (TYPE_CODE_INT, 2, 0, "integer*2", NULL);
      break;
    case 29:
      rettype = init_type (TYPE_CODE_INT, 4, 0, "integer*4", NULL);
      break;
    case 30:
      rettype = init_type (TYPE_CODE_CHAR, 2, 0, "wchar", NULL);
      break;
    case 31:
      rettype = init_type (TYPE_CODE_INT, 8, 0, "long long", NULL);
      break;
    case 32:
      rettype = init_type (TYPE_CODE_INT, 8, TYPE_FLAG_UNSIGNED,
			   "unsigned long long", NULL);
      break;
    case 33:
      rettype = init_type (TYPE_CODE_INT, 8, TYPE_FLAG_UNSIGNED,
			   "logical*8", NULL);
      break;
    case 34:
      rettype = init_type (TYPE_CODE_INT, 8, 0, "integer*8", NULL);
      break;
    }
  negative_types[-typenum] = rettype;
  return rettype;
}

/* This page contains subroutines of read_type.  */

/* Replace *OLD_NAME with the method name portion of PHYSNAME.  */

static void
update_method_name_from_physname (char **old_name, char *physname)
{
  char *method_name;

  method_name = method_name_from_physname (physname);

  if (method_name == NULL)
    {
      complaint (&symfile_complaints,
		 "Method has bad physname %s\n", physname);
      return;
    }

  if (strcmp (*old_name, method_name) != 0)
    {
      xfree (*old_name);
      *old_name = method_name;
    }
  else
    xfree (method_name);
}

/* Read member function stabs info for C++ classes.  The form of each member
   function data is:

   NAME :: TYPENUM[=type definition] ARGS : PHYSNAME ;

   An example with two member functions is:

   afunc1::20=##15;:i;2A.;afunc2::20:i;2A.;

   For the case of overloaded operators, the format is op$::*.funcs, where
   $ is the CPLUS_MARKER (usually '$'), `*' holds the place for an operator
   name (such as `+=') and `.' marks the end of the operator name.

   Returns 1 for success, 0 for failure.  */

static int
read_member_functions (struct field_info *fip, char **pp, struct type *type,
		       struct objfile *objfile)
{
  int nfn_fields = 0;
  int length = 0;
  /* Total number of member functions defined in this class.  If the class
     defines two `f' functions, and one `g' function, then this will have
     the value 3.  */
  int total_length = 0;
  int i;
  struct next_fnfield
    {
      struct next_fnfield *next;
      struct fn_field fn_field;
    }
   *sublist;
  struct type *look_ahead_type;
  struct next_fnfieldlist *new_fnlist;
  struct next_fnfield *new_sublist;
  char *main_fn_name;
  char *p;

  /* Process each list until we find something that is not a member function
     or find the end of the functions. */

  while (**pp != ';')
    {
      /* We should be positioned at the start of the function name.
         Scan forward to find the first ':' and if it is not the
         first of a "::" delimiter, then this is not a member function. */
      p = *pp;
      while (*p != ':')
	{
	  p++;
	}
      if (p[1] != ':')
	{
	  break;
	}

      sublist = NULL;
      look_ahead_type = NULL;
      length = 0;

      new_fnlist = (struct next_fnfieldlist *)
	xmalloc (sizeof (struct next_fnfieldlist));
      make_cleanup (xfree, new_fnlist);
      memset (new_fnlist, 0, sizeof (struct next_fnfieldlist));

      if ((*pp)[0] == 'o' && (*pp)[1] == 'p' && is_cplus_marker ((*pp)[2]))
	{
	  /* This is a completely wierd case.  In order to stuff in the
	     names that might contain colons (the usual name delimiter),
	     Mike Tiemann defined a different name format which is
	     signalled if the identifier is "op$".  In that case, the
	     format is "op$::XXXX." where XXXX is the name.  This is
	     used for names like "+" or "=".  YUUUUUUUK!  FIXME!  */
	  /* This lets the user type "break operator+".
	     We could just put in "+" as the name, but that wouldn't
	     work for "*".  */
	  static char opname[32] = "op$";
	  char *o = opname + 3;

	  /* Skip past '::'.  */
	  *pp = p + 2;

	  STABS_CONTINUE (pp, objfile);
	  p = *pp;
	  while (*p != '.')
	    {
	      *o++ = *p++;
	    }
	  main_fn_name = savestring (opname, o - opname);
	  /* Skip past '.'  */
	  *pp = p + 1;
	}
      else
	{
	  main_fn_name = savestring (*pp, p - *pp);
	  /* Skip past '::'.  */
	  *pp = p + 2;
	}
      new_fnlist->fn_fieldlist.name = main_fn_name;

      do
	{
	  new_sublist =
	    (struct next_fnfield *) xmalloc (sizeof (struct next_fnfield));
	  make_cleanup (xfree, new_sublist);
	  memset (new_sublist, 0, sizeof (struct next_fnfield));

	  /* Check for and handle cretinous dbx symbol name continuation!  */
	  if (look_ahead_type == NULL)
	    {
	      /* Normal case. */
	      STABS_CONTINUE (pp, objfile);

	      new_sublist->fn_field.type = read_type (pp, objfile);
	      if (**pp != ':')
		{
		  /* Invalid symtab info for member function.  */
		  return 0;
		}
	    }
	  else
	    {
	      /* g++ version 1 kludge */
	      new_sublist->fn_field.type = look_ahead_type;
	      look_ahead_type = NULL;
	    }

	  (*pp)++;
	  p = *pp;
	  while (*p != ';')
	    {
	      p++;
	    }

	  /* If this is just a stub, then we don't have the real name here. */

	  if (TYPE_STUB (new_sublist->fn_field.type))
	    {
	      if (!TYPE_DOMAIN_TYPE (new_sublist->fn_field.type))
		TYPE_DOMAIN_TYPE (new_sublist->fn_field.type) = type;
	      new_sublist->fn_field.is_stub = 1;
	    }
	  new_sublist->fn_field.physname = savestring (*pp, p - *pp);
	  *pp = p + 1;

	  /* Set this member function's visibility fields.  */
	  switch (*(*pp)++)
	    {
	    case VISIBILITY_PRIVATE:
	      new_sublist->fn_field.is_private = 1;
	      break;
	    case VISIBILITY_PROTECTED:
	      new_sublist->fn_field.is_protected = 1;
	      break;
	    }

	  STABS_CONTINUE (pp, objfile);
	  switch (**pp)
	    {
	    case 'A':		/* Normal functions. */
	      new_sublist->fn_field.is_const = 0;
	      new_sublist->fn_field.is_volatile = 0;
	      (*pp)++;
	      break;
	    case 'B':		/* `const' member functions. */
	      new_sublist->fn_field.is_const = 1;
	      new_sublist->fn_field.is_volatile = 0;
	      (*pp)++;
	      break;
	    case 'C':		/* `volatile' member function. */
	      new_sublist->fn_field.is_const = 0;
	      new_sublist->fn_field.is_volatile = 1;
	      (*pp)++;
	      break;
	    case 'D':		/* `const volatile' member function. */
	      new_sublist->fn_field.is_const = 1;
	      new_sublist->fn_field.is_volatile = 1;
	      (*pp)++;
	      break;
	    case '*':		/* File compiled with g++ version 1 -- no info */
	    case '?':
	    case '.':
	      break;
	    default:
	      complaint (&symfile_complaints,
			 "const/volatile indicator missing, got '%c'", **pp);
	      break;
	    }

	  switch (*(*pp)++)
	    {
	    case '*':
	      {
		int nbits;
		/* virtual member function, followed by index.
		   The sign bit is set to distinguish pointers-to-methods
		   from virtual function indicies.  Since the array is
		   in words, the quantity must be shifted left by 1
		   on 16 bit machine, and by 2 on 32 bit machine, forcing
		   the sign bit out, and usable as a valid index into
		   the array.  Remove the sign bit here.  */
		new_sublist->fn_field.voffset =
		  (0x7fffffff & read_huge_number (pp, ';', &nbits)) + 2;
		if (nbits != 0)
		  return 0;

		STABS_CONTINUE (pp, objfile);
		if (**pp == ';' || **pp == '\0')
		  {
		    /* Must be g++ version 1.  */
		    new_sublist->fn_field.fcontext = 0;
		  }
		else
		  {
		    /* Figure out from whence this virtual function came.
		       It may belong to virtual function table of
		       one of its baseclasses.  */
		    look_ahead_type = read_type (pp, objfile);
		    if (**pp == ':')
		      {
			/* g++ version 1 overloaded methods. */
		      }
		    else
		      {
			new_sublist->fn_field.fcontext = look_ahead_type;
			if (**pp != ';')
			  {
			    return 0;
			  }
			else
			  {
			    ++*pp;
			  }
			look_ahead_type = NULL;
		      }
		  }
		break;
	      }
	    case '?':
	      /* static member function.  */
	      {
		int slen = strlen (main_fn_name);

		new_sublist->fn_field.voffset = VOFFSET_STATIC;

		/* For static member functions, we can't tell if they
		   are stubbed, as they are put out as functions, and not as
		   methods.
		   GCC v2 emits the fully mangled name if
		   dbxout.c:flag_minimal_debug is not set, so we have to
		   detect a fully mangled physname here and set is_stub
		   accordingly.  Fully mangled physnames in v2 start with
		   the member function name, followed by two underscores.
		   GCC v3 currently always emits stubbed member functions,
		   but with fully mangled physnames, which start with _Z.  */
		if (!(strncmp (new_sublist->fn_field.physname,
			       main_fn_name, slen) == 0
		      && new_sublist->fn_field.physname[slen] == '_'
		      && new_sublist->fn_field.physname[slen + 1] == '_'))
		  {
		    new_sublist->fn_field.is_stub = 1;
		  }
		break;
	      }

	    default:
	      /* error */
	      complaint (&symfile_complaints,
			 "member function type missing, got '%c'", (*pp)[-1]);
	      /* Fall through into normal member function.  */

	    case '.':
	      /* normal member function.  */
	      new_sublist->fn_field.voffset = 0;
	      new_sublist->fn_field.fcontext = 0;
	      break;
	    }

	  new_sublist->next = sublist;
	  sublist = new_sublist;
	  length++;
	  STABS_CONTINUE (pp, objfile);
	}
      while (**pp != ';' && **pp != '\0');

      (*pp)++;
      STABS_CONTINUE (pp, objfile);

      /* Skip GCC 3.X member functions which are duplicates of the callable
	 constructor/destructor.  */
      if (strcmp (main_fn_name, "__base_ctor") == 0
	  || strcmp (main_fn_name, "__base_dtor") == 0
	  || strcmp (main_fn_name, "__deleting_dtor") == 0)
	{
	  xfree (main_fn_name);
	}
      else
	{
	  int has_stub = 0;
	  int has_destructor = 0, has_other = 0;
	  int is_v3 = 0;
	  struct next_fnfield *tmp_sublist;

	  /* Various versions of GCC emit various mostly-useless
	     strings in the name field for special member functions.

	     For stub methods, we need to defer correcting the name
	     until we are ready to unstub the method, because the current
	     name string is used by gdb_mangle_name.  The only stub methods
	     of concern here are GNU v2 operators; other methods have their
	     names correct (see caveat below).

	     For non-stub methods, in GNU v3, we have a complete physname.
	     Therefore we can safely correct the name now.  This primarily
	     affects constructors and destructors, whose name will be
	     __comp_ctor or __comp_dtor instead of Foo or ~Foo.  Cast
	     operators will also have incorrect names; for instance,
	     "operator int" will be named "operator i" (i.e. the type is
	     mangled).

	     For non-stub methods in GNU v2, we have no easy way to
	     know if we have a complete physname or not.  For most
	     methods the result depends on the platform (if CPLUS_MARKER
	     can be `$' or `.', it will use minimal debug information, or
	     otherwise the full physname will be included).

	     Rather than dealing with this, we take a different approach.
	     For v3 mangled names, we can use the full physname; for v2,
	     we use cplus_demangle_opname (which is actually v2 specific),
	     because the only interesting names are all operators - once again
	     barring the caveat below.  Skip this process if any method in the
	     group is a stub, to prevent our fouling up the workings of
	     gdb_mangle_name.

	     The caveat: GCC 2.95.x (and earlier?) put constructors and
	     destructors in the same method group.  We need to split this
	     into two groups, because they should have different names.
	     So for each method group we check whether it contains both
	     routines whose physname appears to be a destructor (the physnames
	     for and destructors are always provided, due to quirks in v2
	     mangling) and routines whose physname does not appear to be a
	     destructor.  If so then we break up the list into two halves.
	     Even if the constructors and destructors aren't in the same group
	     the destructor will still lack the leading tilde, so that also
	     needs to be fixed.

	     So, to summarize what we expect and handle here:

	        Given         Given          Real         Real       Action
	     method name     physname      physname   method name

	     __opi            [none]     __opi__3Foo  operator int    opname
	                                                           [now or later]
	     Foo              _._3Foo       _._3Foo      ~Foo       separate and
	                                                               rename
	     operator i     _ZN3FoocviEv _ZN3FoocviEv operator int    demangle
	     __comp_ctor  _ZN3FooC1ERKS_ _ZN3FooC1ERKS_   Foo         demangle
	  */

	  tmp_sublist = sublist;
	  while (tmp_sublist != NULL)
	    {
	      if (tmp_sublist->fn_field.is_stub)
		has_stub = 1;
	      if (tmp_sublist->fn_field.physname[0] == '_'
		  && tmp_sublist->fn_field.physname[1] == 'Z')
		is_v3 = 1;

	      if (is_destructor_name (tmp_sublist->fn_field.physname))
		has_destructor++;
	      else
		has_other++;

	      tmp_sublist = tmp_sublist->next;
	    }

	  if (has_destructor && has_other)
	    {
	      struct next_fnfieldlist *destr_fnlist;
	      struct next_fnfield *last_sublist;

	      /* Create a new fn_fieldlist for the destructors.  */

	      destr_fnlist = (struct next_fnfieldlist *)
		xmalloc (sizeof (struct next_fnfieldlist));
	      make_cleanup (xfree, destr_fnlist);
	      memset (destr_fnlist, 0, sizeof (struct next_fnfieldlist));
	      destr_fnlist->fn_fieldlist.name
		= obconcat (&objfile->objfile_obstack, "", "~",
			    new_fnlist->fn_fieldlist.name);

	      destr_fnlist->fn_fieldlist.fn_fields = (struct fn_field *)
		obstack_alloc (&objfile->objfile_obstack,
			       sizeof (struct fn_field) * has_destructor);
	      memset (destr_fnlist->fn_fieldlist.fn_fields, 0,
		  sizeof (struct fn_field) * has_destructor);
	      tmp_sublist = sublist;
	      last_sublist = NULL;
	      i = 0;
	      while (tmp_sublist != NULL)
		{
		  if (!is_destructor_name (tmp_sublist->fn_field.physname))
		    {
		      tmp_sublist = tmp_sublist->next;
		      continue;
		    }
		  
		  destr_fnlist->fn_fieldlist.fn_fields[i++]
		    = tmp_sublist->fn_field;
		  if (last_sublist)
		    last_sublist->next = tmp_sublist->next;
		  else
		    sublist = tmp_sublist->next;
		  last_sublist = tmp_sublist;
		  tmp_sublist = tmp_sublist->next;
		}

	      destr_fnlist->fn_fieldlist.length = has_destructor;
	      destr_fnlist->next = fip->fnlist;
	      fip->fnlist = destr_fnlist;
	      nfn_fields++;
	      total_length += has_destructor;
	      length -= has_destructor;
	    }
	  else if (is_v3)
	    {
	      /* v3 mangling prevents the use of abbreviated physnames,
		 so we can do this here.  There are stubbed methods in v3
		 only:
		 - in -gstabs instead of -gstabs+
		 - or for static methods, which are output as a function type
		   instead of a method type.  */

	      update_method_name_from_physname (&new_fnlist->fn_fieldlist.name,
						sublist->fn_field.physname);
	    }
	  else if (has_destructor && new_fnlist->fn_fieldlist.name[0] != '~')
	    {
	      new_fnlist->fn_fieldlist.name = concat ("~", main_fn_name, NULL);
	      xfree (main_fn_name);
	    }
	  else if (!has_stub)
	    {
	      char dem_opname[256];
	      int ret;
	      ret = cplus_demangle_opname (new_fnlist->fn_fieldlist.name,
					      dem_opname, DMGL_ANSI);
	      if (!ret)
		ret = cplus_demangle_opname (new_fnlist->fn_fieldlist.name,
					     dem_opname, 0);
	      if (ret)
		new_fnlist->fn_fieldlist.name
		  = obsavestring (dem_opname, strlen (dem_opname),
				  &objfile->objfile_obstack);
	    }

	  new_fnlist->fn_fieldlist.fn_fields = (struct fn_field *)
	    obstack_alloc (&objfile->objfile_obstack,
			   sizeof (struct fn_field) * length);
	  memset (new_fnlist->fn_fieldlist.fn_fields, 0,
		  sizeof (struct fn_field) * length);
	  for (i = length; (i--, sublist); sublist = sublist->next)
	    {
	      new_fnlist->fn_fieldlist.fn_fields[i] = sublist->fn_field;
	    }

	  new_fnlist->fn_fieldlist.length = length;
	  new_fnlist->next = fip->fnlist;
	  fip->fnlist = new_fnlist;
	  nfn_fields++;
	  total_length += length;
	}
    }

  if (nfn_fields)
    {
      ALLOCATE_CPLUS_STRUCT_TYPE (type);
      TYPE_FN_FIELDLISTS (type) = (struct fn_fieldlist *)
	TYPE_ALLOC (type, sizeof (struct fn_fieldlist) * nfn_fields);
      memset (TYPE_FN_FIELDLISTS (type), 0,
	      sizeof (struct fn_fieldlist) * nfn_fields);
      TYPE_NFN_FIELDS (type) = nfn_fields;
      TYPE_NFN_FIELDS_TOTAL (type) = total_length;
    }

  return 1;
}

/* Special GNU C++ name.

   Returns 1 for success, 0 for failure.  "failure" means that we can't
   keep parsing and it's time for error_type().  */

static int
read_cpp_abbrev (struct field_info *fip, char **pp, struct type *type,
		 struct objfile *objfile)
{
  char *p;
  char *name;
  char cpp_abbrev;
  struct type *context;

  p = *pp;
  if (*++p == 'v')
    {
      name = NULL;
      cpp_abbrev = *++p;

      *pp = p + 1;

      /* At this point, *pp points to something like "22:23=*22...",
         where the type number before the ':' is the "context" and
         everything after is a regular type definition.  Lookup the
         type, find it's name, and construct the field name. */

      context = read_type (pp, objfile);

      switch (cpp_abbrev)
	{
	case 'f':		/* $vf -- a virtual function table pointer */
	  name = type_name_no_tag (context);
	  if (name == NULL)
	  {
		  name = "";
	  }
	  fip->list->field.name =
	    obconcat (&objfile->objfile_obstack, vptr_name, name, "");
	  break;

	case 'b':		/* $vb -- a virtual bsomethingorother */
	  name = type_name_no_tag (context);
	  if (name == NULL)
	    {
	      complaint (&symfile_complaints,
			 "C++ abbreviated type name unknown at symtab pos %d",
			 symnum);
	      name = "FOO";
	    }
	  fip->list->field.name =
	    obconcat (&objfile->objfile_obstack, vb_name, name, "");
	  break;

	default:
	  invalid_cpp_abbrev_complaint (*pp);
	  fip->list->field.name =
	    obconcat (&objfile->objfile_obstack,
		      "INVALID_CPLUSPLUS_ABBREV", "", "");
	  break;
	}

      /* At this point, *pp points to the ':'.  Skip it and read the
         field type. */

      p = ++(*pp);
      if (p[-1] != ':')
	{
	  invalid_cpp_abbrev_complaint (*pp);
	  return 0;
	}
      fip->list->field.type = read_type (pp, objfile);
      if (**pp == ',')
	(*pp)++;		/* Skip the comma.  */
      else
	return 0;

      {
	int nbits;
	FIELD_BITPOS (fip->list->field) = read_huge_number (pp, ';', &nbits);
	if (nbits != 0)
	  return 0;
      }
      /* This field is unpacked.  */
      FIELD_BITSIZE (fip->list->field) = 0;
      fip->list->visibility = VISIBILITY_PRIVATE;
    }
  else
    {
      invalid_cpp_abbrev_complaint (*pp);
      /* We have no idea what syntax an unrecognized abbrev would have, so
         better return 0.  If we returned 1, we would need to at least advance
         *pp to avoid an infinite loop.  */
      return 0;
    }
  return 1;
}

static void
read_one_struct_field (struct field_info *fip, char **pp, char *p,
		       struct type *type, struct objfile *objfile)
{
  fip->list->field.name =
    obsavestring (*pp, p - *pp, &objfile->objfile_obstack);
  *pp = p + 1;

  /* This means we have a visibility for a field coming. */
  if (**pp == '/')
    {
      (*pp)++;
      fip->list->visibility = *(*pp)++;
    }
  else
    {
      /* normal dbx-style format, no explicit visibility */
      fip->list->visibility = VISIBILITY_PUBLIC;
    }

  fip->list->field.type = read_type (pp, objfile);
  if (**pp == ':')
    {
      p = ++(*pp);
#if 0
      /* Possible future hook for nested types. */
      if (**pp == '!')
	{
	  fip->list->field.bitpos = (long) -2;	/* nested type */
	  p = ++(*pp);
	}
      else
	...;
#endif
      while (*p != ';')
	{
	  p++;
	}
      /* Static class member.  */
      SET_FIELD_PHYSNAME (fip->list->field, savestring (*pp, p - *pp));
      *pp = p + 1;
      return;
    }
  else if (**pp != ',')
    {
      /* Bad structure-type format.  */
      stabs_general_complaint ("bad structure-type format");
      return;
    }

  (*pp)++;			/* Skip the comma.  */

  {
    int nbits;
    FIELD_BITPOS (fip->list->field) = read_huge_number (pp, ',', &nbits);
    if (nbits != 0)
      {
	stabs_general_complaint ("bad structure-type format");
	return;
      }
    FIELD_BITSIZE (fip->list->field) = read_huge_number (pp, ';', &nbits);
    if (nbits != 0)
      {
	stabs_general_complaint ("bad structure-type format");
	return;
      }
  }

  if (FIELD_BITPOS (fip->list->field) == 0
      && FIELD_BITSIZE (fip->list->field) == 0)
    {
      /* This can happen in two cases: (1) at least for gcc 2.4.5 or so,
         it is a field which has been optimized out.  The correct stab for
         this case is to use VISIBILITY_IGNORE, but that is a recent
         invention.  (2) It is a 0-size array.  For example
         union { int num; char str[0]; } foo.  Printing "<no value>" for
         str in "p foo" is OK, since foo.str (and thus foo.str[3])
         will continue to work, and a 0-size array as a whole doesn't
         have any contents to print.

         I suspect this probably could also happen with gcc -gstabs (not
         -gstabs+) for static fields, and perhaps other C++ extensions.
         Hopefully few people use -gstabs with gdb, since it is intended
         for dbx compatibility.  */

      /* Ignore this field.  */
      fip->list->visibility = VISIBILITY_IGNORE;
    }
  else
    {
      /* Detect an unpacked field and mark it as such.
         dbx gives a bit size for all fields.
         Note that forward refs cannot be packed,
         and treat enums as if they had the width of ints.  */

      struct type *field_type = check_typedef (FIELD_TYPE (fip->list->field));

      if (TYPE_CODE (field_type) != TYPE_CODE_INT
	  && TYPE_CODE (field_type) != TYPE_CODE_RANGE
	  && TYPE_CODE (field_type) != TYPE_CODE_BOOL
	  && TYPE_CODE (field_type) != TYPE_CODE_ENUM)
	{
	  FIELD_BITSIZE (fip->list->field) = 0;
	}
      if ((FIELD_BITSIZE (fip->list->field)
	   == TARGET_CHAR_BIT * TYPE_LENGTH (field_type)
	   || (TYPE_CODE (field_type) == TYPE_CODE_ENUM
	       && FIELD_BITSIZE (fip->list->field) == TARGET_INT_BIT)
	  )
	  &&
	  FIELD_BITPOS (fip->list->field) % 8 == 0)
	{
	  FIELD_BITSIZE (fip->list->field) = 0;
	}
    }
}


/* Read struct or class data fields.  They have the form:

   NAME : [VISIBILITY] TYPENUM , BITPOS , BITSIZE ;

   At the end, we see a semicolon instead of a field.

   In C++, this may wind up being NAME:?TYPENUM:PHYSNAME; for
   a static field.

   The optional VISIBILITY is one of:

   '/0' (VISIBILITY_PRIVATE)
   '/1' (VISIBILITY_PROTECTED)
   '/2' (VISIBILITY_PUBLIC)
   '/9' (VISIBILITY_IGNORE)

   or nothing, for C style fields with public visibility.

   Returns 1 for success, 0 for failure.  */

static int
read_struct_fields (struct field_info *fip, char **pp, struct type *type,
		    struct objfile *objfile)
{
  char *p;
  struct nextfield *new;

  /* We better set p right now, in case there are no fields at all...    */

  p = *pp;

  /* Read each data member type until we find the terminating ';' at the end of
     the data member list, or break for some other reason such as finding the
     start of the member function list. */
  /* Stab string for structure/union does not end with two ';' in
     SUN C compiler 5.3 i.e. F6U2, hence check for end of string. */

  while (**pp != ';' && **pp != '\0')
    {
      STABS_CONTINUE (pp, objfile);
      /* Get space to record the next field's data.  */
      new = (struct nextfield *) xmalloc (sizeof (struct nextfield));
      make_cleanup (xfree, new);
      memset (new, 0, sizeof (struct nextfield));
      new->next = fip->list;
      fip->list = new;

      /* Get the field name.  */
      p = *pp;

      /* If is starts with CPLUS_MARKER it is a special abbreviation,
         unless the CPLUS_MARKER is followed by an underscore, in
         which case it is just the name of an anonymous type, which we
         should handle like any other type name.  */

      if (is_cplus_marker (p[0]) && p[1] != '_')
	{
	  if (!read_cpp_abbrev (fip, pp, type, objfile))
	    return 0;
	  continue;
	}

      /* Look for the ':' that separates the field name from the field
         values.  Data members are delimited by a single ':', while member
         functions are delimited by a pair of ':'s.  When we hit the member
         functions (if any), terminate scan loop and return. */

      while (*p != ':' && *p != '\0')
	{
	  p++;
	}
      if (*p == '\0')
	return 0;

      /* Check to see if we have hit the member functions yet.  */
      if (p[1] == ':')
	{
	  break;
	}
      read_one_struct_field (fip, pp, p, type, objfile);
    }
  if (p[0] == ':' && p[1] == ':')
    {
      /* (the deleted) chill the list of fields: the last entry (at
         the head) is a partially constructed entry which we now
         scrub. */
      fip->list = fip->list->next;
    }
  return 1;
}
/* *INDENT-OFF* */
/* The stabs for C++ derived classes contain baseclass information which
   is marked by a '!' character after the total size.  This function is
   called when we encounter the baseclass marker, and slurps up all the
   baseclass information.

   Immediately following the '!' marker is the number of base classes that
   the class is derived from, followed by information for each base class.
   For each base class, there are two visibility specifiers, a bit offset
   to the base class information within the derived class, a reference to
   the type for the base class, and a terminating semicolon.

   A typical example, with two base classes, would be "!2,020,19;0264,21;".
   						       ^^ ^ ^ ^  ^ ^  ^
	Baseclass information marker __________________|| | | |  | |  |
	Number of baseclasses __________________________| | | |  | |  |
	Visibility specifiers (2) ________________________| | |  | |  |
	Offset in bits from start of class _________________| |  | |  |
	Type number for base class ___________________________|  | |  |
	Visibility specifiers (2) _______________________________| |  |
	Offset in bits from start of class ________________________|  |
	Type number of base class ____________________________________|

  Return 1 for success, 0 for (error-type-inducing) failure.  */
/* *INDENT-ON* */



static int
read_baseclasses (struct field_info *fip, char **pp, struct type *type,
		  struct objfile *objfile)
{
  int i;
  struct nextfield *new;

  if (**pp != '!')
    {
      return 1;
    }
  else
    {
      /* Skip the '!' baseclass information marker. */
      (*pp)++;
    }

  ALLOCATE_CPLUS_STRUCT_TYPE (type);
  {
    int nbits;
    TYPE_N_BASECLASSES (type) = read_huge_number (pp, ',', &nbits);
    if (nbits != 0)
      return 0;
  }

#if 0
  /* Some stupid compilers have trouble with the following, so break
     it up into simpler expressions.  */
  TYPE_FIELD_VIRTUAL_BITS (type) = (B_TYPE *)
    TYPE_ALLOC (type, B_BYTES (TYPE_N_BASECLASSES (type)));
#else
  {
    int num_bytes = B_BYTES (TYPE_N_BASECLASSES (type));
    char *pointer;

    pointer = (char *) TYPE_ALLOC (type, num_bytes);
    TYPE_FIELD_VIRTUAL_BITS (type) = (B_TYPE *) pointer;
  }
#endif /* 0 */

  B_CLRALL (TYPE_FIELD_VIRTUAL_BITS (type), TYPE_N_BASECLASSES (type));

  for (i = 0; i < TYPE_N_BASECLASSES (type); i++)
    {
      new = (struct nextfield *) xmalloc (sizeof (struct nextfield));
      make_cleanup (xfree, new);
      memset (new, 0, sizeof (struct nextfield));
      new->next = fip->list;
      fip->list = new;
      FIELD_BITSIZE (new->field) = 0;	/* this should be an unpacked field! */

      STABS_CONTINUE (pp, objfile);
      switch (**pp)
	{
	case '0':
	  /* Nothing to do. */
	  break;
	case '1':
	  SET_TYPE_FIELD_VIRTUAL (type, i);
	  break;
	default:
	  /* Unknown character.  Complain and treat it as non-virtual.  */
	  {
	    complaint (&symfile_complaints,
		       "Unknown virtual character `%c' for baseclass", **pp);
	  }
	}
      ++(*pp);

      new->visibility = *(*pp)++;
      switch (new->visibility)
	{
	case VISIBILITY_PRIVATE:
	case VISIBILITY_PROTECTED:
	case VISIBILITY_PUBLIC:
	  break;
	default:
	  /* Bad visibility format.  Complain and treat it as
	     public.  */
	  {
	    complaint (&symfile_complaints,
		       "Unknown visibility `%c' for baseclass",
		       new->visibility);
	    new->visibility = VISIBILITY_PUBLIC;
	  }
	}

      {
	int nbits;

	/* The remaining value is the bit offset of the portion of the object
	   corresponding to this baseclass.  Always zero in the absence of
	   multiple inheritance.  */

	FIELD_BITPOS (new->field) = read_huge_number (pp, ',', &nbits);
	if (nbits != 0)
	  return 0;
      }

      /* The last piece of baseclass information is the type of the
         base class.  Read it, and remember it's type name as this
         field's name. */

      new->field.type = read_type (pp, objfile);
      new->field.name = type_name_no_tag (new->field.type);

      /* skip trailing ';' and bump count of number of fields seen */
      if (**pp == ';')
	(*pp)++;
      else
	return 0;
    }
  return 1;
}

/* The tail end of stabs for C++ classes that contain a virtual function
   pointer contains a tilde, a %, and a type number.
   The type number refers to the base class (possibly this class itself) which
   contains the vtable pointer for the current class.

   This function is called when we have parsed all the method declarations,
   so we can look for the vptr base class info.  */

static int
read_tilde_fields (struct field_info *fip, char **pp, struct type *type,
		   struct objfile *objfile)
{
  char *p;

  STABS_CONTINUE (pp, objfile);

  /* If we are positioned at a ';', then skip it. */
  if (**pp == ';')
    {
      (*pp)++;
    }

  if (**pp == '~')
    {
      (*pp)++;

      if (**pp == '=' || **pp == '+' || **pp == '-')
	{
	  /* Obsolete flags that used to indicate the presence
	     of constructors and/or destructors. */
	  (*pp)++;
	}

      /* Read either a '%' or the final ';'.  */
      if (*(*pp)++ == '%')
	{
	  /* The next number is the type number of the base class
	     (possibly our own class) which supplies the vtable for
	     this class.  Parse it out, and search that class to find
	     its vtable pointer, and install those into TYPE_VPTR_BASETYPE
	     and TYPE_VPTR_FIELDNO.  */

	  struct type *t;
	  int i;

	  t = read_type (pp, objfile);
	  p = (*pp)++;
	  while (*p != '\0' && *p != ';')
	    {
	      p++;
	    }
	  if (*p == '\0')
	    {
	      /* Premature end of symbol.  */
	      return 0;
	    }

	  TYPE_VPTR_BASETYPE (type) = t;
	  if (type == t)	/* Our own class provides vtbl ptr */
	    {
	      for (i = TYPE_NFIELDS (t) - 1;
		   i >= TYPE_N_BASECLASSES (t);
		   --i)
		{
		  char *name = TYPE_FIELD_NAME (t, i);
		  if (!strncmp (name, vptr_name, sizeof (vptr_name) - 2)
		      && is_cplus_marker (name[sizeof (vptr_name) - 2]))
		    {
		      TYPE_VPTR_FIELDNO (type) = i;
		      goto gotit;
		    }
		}
	      /* Virtual function table field not found.  */
	      complaint (&symfile_complaints,
			 "virtual function table pointer not found when defining class `%s'",
			 TYPE_NAME (type));
	      return 0;
	    }
	  else
	    {
	      TYPE_VPTR_FIELDNO (type) = TYPE_VPTR_FIELDNO (t);
	    }

	gotit:
	  *pp = p + 1;
	}
    }
  return 1;
}

static int
attach_fn_fields_to_type (struct field_info *fip, struct type *type)
{
  int n;

  for (n = TYPE_NFN_FIELDS (type);
       fip->fnlist != NULL;
       fip->fnlist = fip->fnlist->next)
    {
      --n;			/* Circumvent Sun3 compiler bug */
      TYPE_FN_FIELDLISTS (type)[n] = fip->fnlist->fn_fieldlist;
    }
  return 1;
}

/* Create the vector of fields, and record how big it is.
   We need this info to record proper virtual function table information
   for this class's virtual functions.  */

static int
attach_fields_to_type (struct field_info *fip, struct type *type,
		       struct objfile *objfile)
{
  int nfields = 0;
  int non_public_fields = 0;
  struct nextfield *scan;

  /* Count up the number of fields that we have, as well as taking note of
     whether or not there are any non-public fields, which requires us to
     allocate and build the private_field_bits and protected_field_bits
     bitfields. */

  for (scan = fip->list; scan != NULL; scan = scan->next)
    {
      nfields++;
      if (scan->visibility != VISIBILITY_PUBLIC)
	{
	  non_public_fields++;
	}
    }

  /* Now we know how many fields there are, and whether or not there are any
     non-public fields.  Record the field count, allocate space for the
     array of fields, and create blank visibility bitfields if necessary. */

  TYPE_NFIELDS (type) = nfields;
  TYPE_FIELDS (type) = (struct field *)
    TYPE_ALLOC (type, sizeof (struct field) * nfields);
  memset (TYPE_FIELDS (type), 0, sizeof (struct field) * nfields);

  if (non_public_fields)
    {
      ALLOCATE_CPLUS_STRUCT_TYPE (type);

      TYPE_FIELD_PRIVATE_BITS (type) =
	(B_TYPE *) TYPE_ALLOC (type, B_BYTES (nfields));
      B_CLRALL (TYPE_FIELD_PRIVATE_BITS (type), nfields);

      TYPE_FIELD_PROTECTED_BITS (type) =
	(B_TYPE *) TYPE_ALLOC (type, B_BYTES (nfields));
      B_CLRALL (TYPE_FIELD_PROTECTED_BITS (type), nfields);

      TYPE_FIELD_IGNORE_BITS (type) =
	(B_TYPE *) TYPE_ALLOC (type, B_BYTES (nfields));
      B_CLRALL (TYPE_FIELD_IGNORE_BITS (type), nfields);
    }

  /* Copy the saved-up fields into the field vector.  Start from the head
     of the list, adding to the tail of the field array, so that they end
     up in the same order in the array in which they were added to the list. */

  while (nfields-- > 0)
    {
      TYPE_FIELD (type, nfields) = fip->list->field;
      switch (fip->list->visibility)
	{
	case VISIBILITY_PRIVATE:
	  SET_TYPE_FIELD_PRIVATE (type, nfields);
	  break;

	case VISIBILITY_PROTECTED:
	  SET_TYPE_FIELD_PROTECTED (type, nfields);
	  break;

	case VISIBILITY_IGNORE:
	  SET_TYPE_FIELD_IGNORE (type, nfields);
	  break;

	case VISIBILITY_PUBLIC:
	  break;

	default:
	  /* Unknown visibility.  Complain and treat it as public.  */
	  {
	    complaint (&symfile_complaints, "Unknown visibility `%c' for field",
		       fip->list->visibility);
	  }
	  break;
	}
      fip->list = fip->list->next;
    }
  return 1;
}


/* Complain that the compiler has emitted more than one definition for the
   structure type TYPE.  */
static void 
complain_about_struct_wipeout (struct type *type)
{
  char *name = "";
  char *kind = "";

  if (TYPE_TAG_NAME (type))
    {
      name = TYPE_TAG_NAME (type);
      switch (TYPE_CODE (type))
        {
        case TYPE_CODE_STRUCT: kind = "struct "; break;
        case TYPE_CODE_UNION:  kind = "union ";  break;
        case TYPE_CODE_ENUM:   kind = "enum ";   break;
        default: kind = "";
        }
    }
  else if (TYPE_NAME (type))
    {
      name = TYPE_NAME (type);
      kind = "";
    }
  else
    {
      name = "<unknown>";
      kind = "";
    }

  complaint (&symfile_complaints,
	     "struct/union type gets multiply defined: %s%s", kind, name);
}


/* Read the description of a structure (or union type) and return an object
   describing the type.

   PP points to a character pointer that points to the next unconsumed token
   in the the stabs string.  For example, given stabs "A:T4=s4a:1,0,32;;",
   *PP will point to "4a:1,0,32;;".

   TYPE points to an incomplete type that needs to be filled in.

   OBJFILE points to the current objfile from which the stabs information is
   being read.  (Note that it is redundant in that TYPE also contains a pointer
   to this same objfile, so it might be a good idea to eliminate it.  FIXME). 
 */

static struct type *
read_struct_type (char **pp, struct type *type, enum type_code type_code,
                  struct objfile *objfile)
{
  struct cleanup *back_to;
  struct field_info fi;

  fi.list = NULL;
  fi.fnlist = NULL;

  /* When describing struct/union/class types in stabs, G++ always drops
     all qualifications from the name.  So if you've got:
       struct A { ... struct B { ... }; ... };
     then G++ will emit stabs for `struct A::B' that call it simply
     `struct B'.  Obviously, if you've got a real top-level definition for
     `struct B', or other nested definitions, this is going to cause
     problems.

     Obviously, GDB can't fix this by itself, but it can at least avoid
     scribbling on existing structure type objects when new definitions
     appear.  */
  if (! (TYPE_CODE (type) == TYPE_CODE_UNDEF
         || TYPE_STUB (type)))
    {
      complain_about_struct_wipeout (type);

      /* It's probably best to return the type unchanged.  */
      return type;
    }

  back_to = make_cleanup (null_cleanup, 0);

  INIT_CPLUS_SPECIFIC (type);
  TYPE_CODE (type) = type_code;
  TYPE_FLAGS (type) &= ~TYPE_FLAG_STUB;

  /* First comes the total size in bytes.  */

  {
    int nbits;
    TYPE_LENGTH (type) = read_huge_number (pp, 0, &nbits);
    if (nbits != 0)
      return error_type (pp, objfile);
  }

  /* Now read the baseclasses, if any, read the regular C struct or C++
     class member fields, attach the fields to the type, read the C++
     member functions, attach them to the type, and then read any tilde
     field (baseclass specifier for the class holding the main vtable). */

  if (!read_baseclasses (&fi, pp, type, objfile)
      || !read_struct_fields (&fi, pp, type, objfile)
      || !attach_fields_to_type (&fi, type, objfile)
      || !read_member_functions (&fi, pp, type, objfile)
      || !attach_fn_fields_to_type (&fi, type)
      || !read_tilde_fields (&fi, pp, type, objfile))
    {
      type = error_type (pp, objfile);
    }

  do_cleanups (back_to);
  return (type);
}

/* Read a definition of an array type,
   and create and return a suitable type object.
   Also creates a range type which represents the bounds of that
   array.  */

static struct type *
read_array_type (char **pp, struct type *type,
		 struct objfile *objfile)
{
  struct type *index_type, *element_type, *range_type;
  int lower, upper;
  int adjustable = 0;
  int nbits;

  /* Format of an array type:
     "ar<index type>;lower;upper;<array_contents_type>".
     OS9000: "arlower,upper;<array_contents_type>".

     Fortran adjustable arrays use Adigits or Tdigits for lower or upper;
     for these, produce a type like float[][].  */

    {
      index_type = read_type (pp, objfile);
      if (**pp != ';')
	/* Improper format of array type decl.  */
	return error_type (pp, objfile);
      ++*pp;
    }

  if (!(**pp >= '0' && **pp <= '9') && **pp != '-')
    {
      (*pp)++;
      adjustable = 1;
    }
  lower = read_huge_number (pp, ';', &nbits);

  if (nbits != 0)
    return error_type (pp, objfile);

  if (!(**pp >= '0' && **pp <= '9') && **pp != '-')
    {
      (*pp)++;
      adjustable = 1;
    }
  upper = read_huge_number (pp, ';', &nbits);
  if (nbits != 0)
    return error_type (pp, objfile);

  element_type = read_type (pp, objfile);

  if (adjustable)
    {
      lower = 0;
      upper = -1;
    }

  range_type =
    create_range_type ((struct type *) NULL, index_type, lower, upper);
  type = create_array_type (type, element_type, range_type);

  return type;
}


/* Read a definition of an enumeration type,
   and create and return a suitable type object.
   Also defines the symbols that represent the values of the type.  */

static struct type *
read_enum_type (char **pp, struct type *type,
		struct objfile *objfile)
{
  char *p;
  char *name;
  long n;
  struct symbol *sym;
  int nsyms = 0;
  struct pending **symlist;
  struct pending *osyms, *syms;
  int o_nsyms;
  int nbits;
  int unsigned_enum = 1;

#if 0
  /* FIXME!  The stabs produced by Sun CC merrily define things that ought
     to be file-scope, between N_FN entries, using N_LSYM.  What's a mother
     to do?  For now, force all enum values to file scope.  */
  if (within_function)
    symlist = &local_symbols;
  else
#endif
    symlist = &file_symbols;
  osyms = *symlist;
  o_nsyms = osyms ? osyms->nsyms : 0;

  /* The aix4 compiler emits an extra field before the enum members;
     my guess is it's a type of some sort.  Just ignore it.  */
  if (**pp == '-')
    {
      /* Skip over the type.  */
      while (**pp != ':')
	(*pp)++;

      /* Skip over the colon.  */
      (*pp)++;
    }

  /* Read the value-names and their values.
     The input syntax is NAME:VALUE,NAME:VALUE, and so on.
     A semicolon or comma instead of a NAME means the end.  */
  while (**pp && **pp != ';' && **pp != ',')
    {
      STABS_CONTINUE (pp, objfile);
      p = *pp;
      while (*p != ':')
	p++;
      name = obsavestring (*pp, p - *pp, &objfile->objfile_obstack);
      *pp = p + 1;
      n = read_huge_number (pp, ',', &nbits);
      if (nbits != 0)
	return error_type (pp, objfile);

      sym = (struct symbol *)
	obstack_alloc (&objfile->objfile_obstack, sizeof (struct symbol));
      memset (sym, 0, sizeof (struct symbol));
      DEPRECATED_SYMBOL_NAME (sym) = name;
      SYMBOL_LANGUAGE (sym) = current_subfile->language;
      SYMBOL_CLASS (sym) = LOC_CONST;
      SYMBOL_DOMAIN (sym) = VAR_DOMAIN;
      SYMBOL_VALUE (sym) = n;
      if (n < 0)
	unsigned_enum = 0;
      add_symbol_to_list (sym, symlist);
      nsyms++;
    }

  if (**pp == ';')
    (*pp)++;			/* Skip the semicolon.  */

  /* Now fill in the fields of the type-structure.  */

  TYPE_LENGTH (type) = TARGET_INT_BIT / HOST_CHAR_BIT;
  TYPE_CODE (type) = TYPE_CODE_ENUM;
  TYPE_FLAGS (type) &= ~TYPE_FLAG_STUB;
  if (unsigned_enum)
    TYPE_FLAGS (type) |= TYPE_FLAG_UNSIGNED;
  TYPE_NFIELDS (type) = nsyms;
  TYPE_FIELDS (type) = (struct field *)
    TYPE_ALLOC (type, sizeof (struct field) * nsyms);
  memset (TYPE_FIELDS (type), 0, sizeof (struct field) * nsyms);

  /* Find the symbols for the values and put them into the type.
     The symbols can be found in the symlist that we put them on
     to cause them to be defined.  osyms contains the old value
     of that symlist; everything up to there was defined by us.  */
  /* Note that we preserve the order of the enum constants, so
     that in something like "enum {FOO, LAST_THING=FOO}" we print
     FOO, not LAST_THING.  */

  for (syms = *symlist, n = nsyms - 1; syms; syms = syms->next)
    {
      int last = syms == osyms ? o_nsyms : 0;
      int j = syms->nsyms;
      for (; --j >= last; --n)
	{
	  struct symbol *xsym = syms->symbol[j];
	  SYMBOL_TYPE (xsym) = type;
	  TYPE_FIELD_NAME (type, n) = DEPRECATED_SYMBOL_NAME (xsym);
	  TYPE_FIELD_BITPOS (type, n) = SYMBOL_VALUE (xsym);
	  TYPE_FIELD_BITSIZE (type, n) = 0;
	}
      if (syms == osyms)
	break;
    }

  return type;
}

/* Sun's ACC uses a somewhat saner method for specifying the builtin
   typedefs in every file (for int, long, etc):

   type = b <signed> <width> <format type>; <offset>; <nbits>
   signed = u or s.
   optional format type = c or b for char or boolean.
   offset = offset from high order bit to start bit of type.
   width is # bytes in object of this type, nbits is # bits in type.

   The width/offset stuff appears to be for small objects stored in
   larger ones (e.g. `shorts' in `int' registers).  We ignore it for now,
   FIXME.  */

static struct type *
read_sun_builtin_type (char **pp, int typenums[2], struct objfile *objfile)
{
  int type_bits;
  int nbits;
  int signed_type;
  enum type_code code = TYPE_CODE_INT;

  switch (**pp)
    {
    case 's':
      signed_type = 1;
      break;
    case 'u':
      signed_type = 0;
      break;
    default:
      return error_type (pp, objfile);
    }
  (*pp)++;

  /* For some odd reason, all forms of char put a c here.  This is strange
     because no other type has this honor.  We can safely ignore this because
     we actually determine 'char'acterness by the number of bits specified in
     the descriptor.
     Boolean forms, e.g Fortran logical*X, put a b here.  */

  if (**pp == 'c')
    (*pp)++;
  else if (**pp == 'b')
    {
      code = TYPE_CODE_BOOL;
      (*pp)++;
    }

  /* The first number appears to be the number of bytes occupied
     by this type, except that unsigned short is 4 instead of 2.
     Since this information is redundant with the third number,
     we will ignore it.  */
  read_huge_number (pp, ';', &nbits);
  if (nbits != 0)
    return error_type (pp, objfile);

  /* The second number is always 0, so ignore it too. */
  read_huge_number (pp, ';', &nbits);
  if (nbits != 0)
    return error_type (pp, objfile);

  /* The third number is the number of bits for this type. */
  type_bits = read_huge_number (pp, 0, &nbits);
  if (nbits != 0)
    return error_type (pp, objfile);
  /* The type *should* end with a semicolon.  If it are embedded
     in a larger type the semicolon may be the only way to know where
     the type ends.  If this type is at the end of the stabstring we
     can deal with the omitted semicolon (but we don't have to like
     it).  Don't bother to complain(), Sun's compiler omits the semicolon
     for "void".  */
  if (**pp == ';')
    ++(*pp);

  if (type_bits == 0)
    return init_type (TYPE_CODE_VOID, 1,
		      signed_type ? 0 : TYPE_FLAG_UNSIGNED, (char *) NULL,
		      objfile);
  else
    return init_type (code,
		      type_bits / TARGET_CHAR_BIT,
		      signed_type ? 0 : TYPE_FLAG_UNSIGNED, (char *) NULL,
		      objfile);
}

static struct type *
read_sun_floating_type (char **pp, int typenums[2], struct objfile *objfile)
{
  int nbits;
  int details;
  int nbytes;
  struct type *rettype;

  /* The first number has more details about the type, for example
     FN_COMPLEX.  */
  details = read_huge_number (pp, ';', &nbits);
  if (nbits != 0)
    return error_type (pp, objfile);

  /* The second number is the number of bytes occupied by this type */
  nbytes = read_huge_number (pp, ';', &nbits);
  if (nbits != 0)
    return error_type (pp, objfile);

  if (details == NF_COMPLEX || details == NF_COMPLEX16
      || details == NF_COMPLEX32)
    {
      rettype = init_type (TYPE_CODE_COMPLEX, nbytes, 0, NULL, objfile);
      TYPE_TARGET_TYPE (rettype)
	= init_type (TYPE_CODE_FLT, nbytes / 2, 0, NULL, objfile);
      return rettype;
    }

  return init_type (TYPE_CODE_FLT, nbytes, 0, NULL, objfile);
}

/* Read a number from the string pointed to by *PP.
   The value of *PP is advanced over the number.
   If END is nonzero, the character that ends the
   number must match END, or an error happens;
   and that character is skipped if it does match.
   If END is zero, *PP is left pointing to that character.

   If the number fits in a long, set *BITS to 0 and return the value.
   If not, set *BITS to be the number of bits in the number and return 0.

   If encounter garbage, set *BITS to -1 and return 0.  */

static long
read_huge_number (char **pp, int end, int *bits)
{
  char *p = *pp;
  int sign = 1;
  long n = 0;
  int radix = 10;
  char overflow = 0;
  int nbits = 0;
  int c;
  long upper_limit;

  if (*p == '-')
    {
      sign = -1;
      p++;
    }

  /* Leading zero means octal.  GCC uses this to output values larger
     than an int (because that would be hard in decimal).  */
  if (*p == '0')
    {
      radix = 8;
      p++;
    }

  upper_limit = LONG_MAX / radix;

  while ((c = *p++) >= '0' && c < ('0' + radix))
    {
      if (n <= upper_limit)
	{
	  n *= radix;
	  n += c - '0';		/* FIXME this overflows anyway */
	}
      else
	overflow = 1;

      /* This depends on large values being output in octal, which is
         what GCC does. */
      if (radix == 8)
	{
	  if (nbits == 0)
	    {
	      if (c == '0')
		/* Ignore leading zeroes.  */
		;
	      else if (c == '1')
		nbits = 1;
	      else if (c == '2' || c == '3')
		nbits = 2;
	      else
		nbits = 3;
	    }
	  else
	    nbits += 3;
	}
    }
  if (end)
    {
      if (c && c != end)
	{
	  if (bits != NULL)
	    *bits = -1;
	  return 0;
	}
    }
  else
    --p;

  *pp = p;
  if (overflow)
    {
      if (nbits == 0)
	{
	  /* Large decimal constants are an error (because it is hard to
	     count how many bits are in them).  */
	  if (bits != NULL)
	    *bits = -1;
	  return 0;
	}

      /* -0x7f is the same as 0x80.  So deal with it by adding one to
         the number of bits.  */
      if (sign == -1)
	++nbits;
      if (bits)
	*bits = nbits;
    }
  else
    {
      if (bits)
	*bits = 0;
      return n * sign;
    }
  /* It's *BITS which has the interesting information.  */
  return 0;
}

static struct type *
read_range_type (char **pp, int typenums[2], struct objfile *objfile)
{
  char *orig_pp = *pp;
  int rangenums[2];
  long n2, n3;
  int n2bits, n3bits;
  int self_subrange;
  struct type *result_type;
  struct type *index_type = NULL;

  /* First comes a type we are a subrange of.
     In C it is usually 0, 1 or the type being defined.  */
  if (read_type_number (pp, rangenums) != 0)
    return error_type (pp, objfile);
  self_subrange = (rangenums[0] == typenums[0] &&
		   rangenums[1] == typenums[1]);

  if (**pp == '=')
    {
      *pp = orig_pp;
      index_type = read_type (pp, objfile);
    }

  /* A semicolon should now follow; skip it.  */
  if (**pp == ';')
    (*pp)++;

  /* The remaining two operands are usually lower and upper bounds
     of the range.  But in some special cases they mean something else.  */
  n2 = read_huge_number (pp, ';', &n2bits);
  n3 = read_huge_number (pp, ';', &n3bits);

  if (n2bits == -1 || n3bits == -1)
    return error_type (pp, objfile);

  if (index_type)
    goto handle_true_range;

  /* If limits are huge, must be large integral type.  */
  if (n2bits != 0 || n3bits != 0)
    {
      char got_signed = 0;
      char got_unsigned = 0;
      /* Number of bits in the type.  */
      int nbits = 0;

      /* Range from 0 to <large number> is an unsigned large integral type.  */
      if ((n2bits == 0 && n2 == 0) && n3bits != 0)
	{
	  got_unsigned = 1;
	  nbits = n3bits;
	}
      /* Range from <large number> to <large number>-1 is a large signed
         integral type.  Take care of the case where <large number> doesn't
         fit in a long but <large number>-1 does.  */
      else if ((n2bits != 0 && n3bits != 0 && n2bits == n3bits + 1)
	       || (n2bits != 0 && n3bits == 0
		   && (n2bits == sizeof (long) * HOST_CHAR_BIT)
		   && n3 == LONG_MAX))
	{
	  got_signed = 1;
	  nbits = n2bits;
	}

      if (got_signed || got_unsigned)
	{
	  return init_type (TYPE_CODE_INT, nbits / TARGET_CHAR_BIT,
			    got_unsigned ? TYPE_FLAG_UNSIGNED : 0, NULL,
			    objfile);
	}
      else
	return error_type (pp, objfile);
    }

  /* A type defined as a subrange of itself, with bounds both 0, is void.  */
  if (self_subrange && n2 == 0 && n3 == 0)
    return init_type (TYPE_CODE_VOID, 1, 0, NULL, objfile);

  /* If n3 is zero and n2 is positive, we want a floating type, and n2
     is the width in bytes.

     Fortran programs appear to use this for complex types also.  To
     distinguish between floats and complex, g77 (and others?)  seem
     to use self-subranges for the complexes, and subranges of int for
     the floats.

     Also note that for complexes, g77 sets n2 to the size of one of
     the member floats, not the whole complex beast.  My guess is that
     this was to work well with pre-COMPLEX versions of gdb. */

  if (n3 == 0 && n2 > 0)
    {
      struct type *float_type
	= init_type (TYPE_CODE_FLT, n2, 0, NULL, objfile);

      if (self_subrange)
	{
	  struct type *complex_type = 
	    init_type (TYPE_CODE_COMPLEX, 2 * n2, 0, NULL, objfile);
	  TYPE_TARGET_TYPE (complex_type) = float_type;
	  return complex_type;
	}
      else
	return float_type;
    }

  /* If the upper bound is -1, it must really be an unsigned int.  */

  else if (n2 == 0 && n3 == -1)
    {
      /* It is unsigned int or unsigned long.  */
      /* GCC 2.3.3 uses this for long long too, but that is just a GDB 3.5
         compatibility hack.  */
      return init_type (TYPE_CODE_INT, TARGET_INT_BIT / TARGET_CHAR_BIT,
			TYPE_FLAG_UNSIGNED, NULL, objfile);
    }

  /* Special case: char is defined (Who knows why) as a subrange of
     itself with range 0-127.  */
  else if (self_subrange && n2 == 0 && n3 == 127)
    return init_type (TYPE_CODE_INT, 1, TYPE_FLAG_NOSIGN, NULL, objfile);

  /* We used to do this only for subrange of self or subrange of int.  */
  else if (n2 == 0)
    {
      /* -1 is used for the upper bound of (4 byte) "unsigned int" and
         "unsigned long", and we already checked for that,
         so don't need to test for it here.  */

      if (n3 < 0)
	/* n3 actually gives the size.  */
	return init_type (TYPE_CODE_INT, -n3, TYPE_FLAG_UNSIGNED,
			  NULL, objfile);

      /* Is n3 == 2**(8n)-1 for some integer n?  Then it's an
         unsigned n-byte integer.  But do require n to be a power of
         two; we don't want 3- and 5-byte integers flying around.  */
      {
	int bytes;
	unsigned long bits;

	bits = n3;
	for (bytes = 0; (bits & 0xff) == 0xff; bytes++)
	  bits >>= 8;
	if (bits == 0
	    && ((bytes - 1) & bytes) == 0) /* "bytes is a power of two" */
	  return init_type (TYPE_CODE_INT, bytes, TYPE_FLAG_UNSIGNED, NULL,
			    objfile);
      }
    }
  /* I think this is for Convex "long long".  Since I don't know whether
     Convex sets self_subrange, I also accept that particular size regardless
     of self_subrange.  */
  else if (n3 == 0 && n2 < 0
	   && (self_subrange
	       || n2 == -TARGET_LONG_LONG_BIT / TARGET_CHAR_BIT))
    return init_type (TYPE_CODE_INT, -n2, 0, NULL, objfile);
  else if (n2 == -n3 - 1)
    {
      if (n3 == 0x7f)
	return init_type (TYPE_CODE_INT, 1, 0, NULL, objfile);
      if (n3 == 0x7fff)
	return init_type (TYPE_CODE_INT, 2, 0, NULL, objfile);
      if (n3 == 0x7fffffff)
	return init_type (TYPE_CODE_INT, 4, 0, NULL, objfile);
    }

  /* We have a real range type on our hands.  Allocate space and
     return a real pointer.  */
handle_true_range:

  if (self_subrange)
    index_type = builtin_type_int;
  else
    index_type = *dbx_lookup_type (rangenums);
  if (index_type == NULL)
    {
      /* Does this actually ever happen?  Is that why we are worrying
         about dealing with it rather than just calling error_type?  */

      static struct type *range_type_index;

      complaint (&symfile_complaints,
		 "base type %d of range type is not defined", rangenums[1]);
      if (range_type_index == NULL)
	range_type_index =
	  init_type (TYPE_CODE_INT, TARGET_INT_BIT / TARGET_CHAR_BIT,
		     0, "range type index type", NULL);
      index_type = range_type_index;
    }

  result_type = create_range_type ((struct type *) NULL, index_type, n2, n3);
  return (result_type);
}

/* Read in an argument list.  This is a list of types, separated by commas
   and terminated with END.  Return the list of types read in, or (struct type
   **)-1 if there is an error.  */

static struct field *
read_args (char **pp, int end, struct objfile *objfile, int *nargsp,
	   int *varargsp)
{
  /* FIXME!  Remove this arbitrary limit!  */
  struct type *types[1024];	/* allow for fns of 1023 parameters */
  int n = 0, i;
  struct field *rval;

  while (**pp != end)
    {
      if (**pp != ',')
	/* Invalid argument list: no ','.  */
	return (struct field *) -1;
      (*pp)++;
      STABS_CONTINUE (pp, objfile);
      types[n++] = read_type (pp, objfile);
    }
  (*pp)++;			/* get past `end' (the ':' character) */

  if (TYPE_CODE (types[n - 1]) != TYPE_CODE_VOID)
    *varargsp = 1;
  else
    {
      n--;
      *varargsp = 0;
    }

  rval = (struct field *) xmalloc (n * sizeof (struct field));
  memset (rval, 0, n * sizeof (struct field));
  for (i = 0; i < n; i++)
    rval[i].type = types[i];
  *nargsp = n;
  return rval;
}

/* Common block handling.  */

/* List of symbols declared since the last BCOMM.  This list is a tail
   of local_symbols.  When ECOMM is seen, the symbols on the list
   are noted so their proper addresses can be filled in later,
   using the common block base address gotten from the assembler
   stabs.  */

static struct pending *common_block;
static int common_block_i;

/* Name of the current common block.  We get it from the BCOMM instead of the
   ECOMM to match IBM documentation (even though IBM puts the name both places
   like everyone else).  */
static char *common_block_name;

/* Process a N_BCOMM symbol.  The storage for NAME is not guaranteed
   to remain after this function returns.  */

void
common_block_start (char *name, struct objfile *objfile)
{
  if (common_block_name != NULL)
    {
      complaint (&symfile_complaints,
		 "Invalid symbol data: common block within common block");
    }
  common_block = local_symbols;
  common_block_i = local_symbols ? local_symbols->nsyms : 0;
  common_block_name = obsavestring (name, strlen (name),
				    &objfile->objfile_obstack);
}

/* Process a N_ECOMM symbol.  */

void
common_block_end (struct objfile *objfile)
{
  /* Symbols declared since the BCOMM are to have the common block
     start address added in when we know it.  common_block and
     common_block_i point to the first symbol after the BCOMM in
     the local_symbols list; copy the list and hang it off the
     symbol for the common block name for later fixup.  */
  int i;
  struct symbol *sym;
  struct pending *new = 0;
  struct pending *next;
  int j;

  if (common_block_name == NULL)
    {
      complaint (&symfile_complaints, "ECOMM symbol unmatched by BCOMM");
      return;
    }

  sym = (struct symbol *)
    obstack_alloc (&objfile->objfile_obstack, sizeof (struct symbol));
  memset (sym, 0, sizeof (struct symbol));
  /* Note: common_block_name already saved on objfile_obstack */
  DEPRECATED_SYMBOL_NAME (sym) = common_block_name;
  SYMBOL_CLASS (sym) = LOC_BLOCK;

  /* Now we copy all the symbols which have been defined since the BCOMM.  */

  /* Copy all the struct pendings before common_block.  */
  for (next = local_symbols;
       next != NULL && next != common_block;
       next = next->next)
    {
      for (j = 0; j < next->nsyms; j++)
	add_symbol_to_list (next->symbol[j], &new);
    }

  /* Copy however much of COMMON_BLOCK we need.  If COMMON_BLOCK is
     NULL, it means copy all the local symbols (which we already did
     above).  */

  if (common_block != NULL)
    for (j = common_block_i; j < common_block->nsyms; j++)
      add_symbol_to_list (common_block->symbol[j], &new);

  SYMBOL_TYPE (sym) = (struct type *) new;

  /* Should we be putting local_symbols back to what it was?
     Does it matter?  */

  i = hashname (DEPRECATED_SYMBOL_NAME (sym));
  SYMBOL_VALUE_CHAIN (sym) = global_sym_chain[i];
  global_sym_chain[i] = sym;
  common_block_name = NULL;
}

/* Add a common block's start address to the offset of each symbol
   declared to be in it (by being between a BCOMM/ECOMM pair that uses
   the common block name).  */

static void
fix_common_block (struct symbol *sym, int valu)
{
  struct pending *next = (struct pending *) SYMBOL_TYPE (sym);
  for (; next; next = next->next)
    {
      int j;
      for (j = next->nsyms - 1; j >= 0; j--)
	SYMBOL_VALUE_ADDRESS (next->symbol[j]) += valu;
    }
}



/* What about types defined as forward references inside of a small lexical
   scope?  */
/* Add a type to the list of undefined types to be checked through
   once this file has been read in.  */

static void
add_undefined_type (struct type *type)
{
  if (undef_types_length == undef_types_allocated)
    {
      undef_types_allocated *= 2;
      undef_types = (struct type **)
	xrealloc ((char *) undef_types,
		  undef_types_allocated * sizeof (struct type *));
    }
  undef_types[undef_types_length++] = type;
}

/* Go through each undefined type, see if it's still undefined, and fix it
   up if possible.  We have two kinds of undefined types:

   TYPE_CODE_ARRAY:  Array whose target type wasn't defined yet.
   Fix:  update array length using the element bounds
   and the target type's length.
   TYPE_CODE_STRUCT, TYPE_CODE_UNION:  Structure whose fields were not
   yet defined at the time a pointer to it was made.
   Fix:  Do a full lookup on the struct/union tag.  */
void
cleanup_undefined_types (void)
{
  struct type **type;

  for (type = undef_types; type < undef_types + undef_types_length; type++)
    {
      switch (TYPE_CODE (*type))
	{

	case TYPE_CODE_STRUCT:
	case TYPE_CODE_UNION:
	case TYPE_CODE_ENUM:
	  {
	    /* Check if it has been defined since.  Need to do this here
	       as well as in check_typedef to deal with the (legitimate in
	       C though not C++) case of several types with the same name
	       in different source files.  */
	    if (TYPE_STUB (*type))
	      {
		struct pending *ppt;
		int i;
		/* Name of the type, without "struct" or "union" */
		char *typename = TYPE_TAG_NAME (*type);

		if (typename == NULL)
		  {
		    complaint (&symfile_complaints, "need a type name");
		    break;
		  }
		for (ppt = file_symbols; ppt; ppt = ppt->next)
		  {
		    for (i = 0; i < ppt->nsyms; i++)
		      {
			struct symbol *sym = ppt->symbol[i];

			if (SYMBOL_CLASS (sym) == LOC_TYPEDEF
			    && SYMBOL_DOMAIN (sym) == STRUCT_DOMAIN
			    && (TYPE_CODE (SYMBOL_TYPE (sym)) ==
				TYPE_CODE (*type))
			    && strcmp (DEPRECATED_SYMBOL_NAME (sym), typename) == 0)
                          replace_type (*type, SYMBOL_TYPE (sym));
		      }
		  }
	      }
	  }
	  break;

	default:
	  {
	    complaint (&symfile_complaints,
		       "forward-referenced types left unresolved, "
                       "type code %d.",
		       TYPE_CODE (*type));
	  }
	  break;
	}
    }

  undef_types_length = 0;
}

/* Scan through all of the global symbols defined in the object file,
   assigning values to the debugging symbols that need to be assigned
   to.  Get these symbols from the minimal symbol table.  */

void
scan_file_globals (struct objfile *objfile)
{
  int hash;
  struct minimal_symbol *msymbol;
  struct symbol *sym, *prev;
  struct objfile *resolve_objfile;

  /* SVR4 based linkers copy referenced global symbols from shared
     libraries to the main executable.
     If we are scanning the symbols for a shared library, try to resolve
     them from the minimal symbols of the main executable first.  */

  if (symfile_objfile && objfile != symfile_objfile)
    resolve_objfile = symfile_objfile;
  else
    resolve_objfile = objfile;

  while (1)
    {
      /* Avoid expensive loop through all minimal symbols if there are
         no unresolved symbols.  */
      for (hash = 0; hash < HASHSIZE; hash++)
	{
	  if (global_sym_chain[hash])
	    break;
	}
      if (hash >= HASHSIZE)
	return;

      for (msymbol = resolve_objfile->msymbols;
	   msymbol && DEPRECATED_SYMBOL_NAME (msymbol) != NULL;
	   msymbol++)
	{
	  QUIT;

	  /* Skip static symbols.  */
	  switch (MSYMBOL_TYPE (msymbol))
	    {
	    case mst_file_text:
	    case mst_file_data:
	    case mst_file_bss:
	      continue;
	    default:
	      break;
	    }

	  prev = NULL;

	  /* Get the hash index and check all the symbols
	     under that hash index. */

	  hash = hashname (DEPRECATED_SYMBOL_NAME (msymbol));

	  for (sym = global_sym_chain[hash]; sym;)
	    {
	      if (DEPRECATED_SYMBOL_NAME (msymbol)[0] == DEPRECATED_SYMBOL_NAME (sym)[0] &&
		  strcmp (DEPRECATED_SYMBOL_NAME (msymbol) + 1, DEPRECATED_SYMBOL_NAME (sym) + 1) == 0)
		{
		  /* Splice this symbol out of the hash chain and
		     assign the value we have to it. */
		  if (prev)
		    {
		      SYMBOL_VALUE_CHAIN (prev) = SYMBOL_VALUE_CHAIN (sym);
		    }
		  else
		    {
		      global_sym_chain[hash] = SYMBOL_VALUE_CHAIN (sym);
		    }

		  /* Check to see whether we need to fix up a common block.  */
		  /* Note: this code might be executed several times for
		     the same symbol if there are multiple references.  */
		  if (sym)
		    {
		      if (SYMBOL_CLASS (sym) == LOC_BLOCK)
			{
			  fix_common_block (sym,
					    SYMBOL_VALUE_ADDRESS (msymbol));
			}
		      else
			{
			  SYMBOL_VALUE_ADDRESS (sym)
			    = SYMBOL_VALUE_ADDRESS (msymbol);
			}
		      SYMBOL_SECTION (sym) = SYMBOL_SECTION (msymbol);
		    }

		  if (prev)
		    {
		      sym = SYMBOL_VALUE_CHAIN (prev);
		    }
		  else
		    {
		      sym = global_sym_chain[hash];
		    }
		}
	      else
		{
		  prev = sym;
		  sym = SYMBOL_VALUE_CHAIN (sym);
		}
	    }
	}
      if (resolve_objfile == objfile)
	break;
      resolve_objfile = objfile;
    }

  /* Change the storage class of any remaining unresolved globals to
     LOC_UNRESOLVED and remove them from the chain.  */
  for (hash = 0; hash < HASHSIZE; hash++)
    {
      sym = global_sym_chain[hash];
      while (sym)
	{
	  prev = sym;
	  sym = SYMBOL_VALUE_CHAIN (sym);

	  /* Change the symbol address from the misleading chain value
	     to address zero.  */
	  SYMBOL_VALUE_ADDRESS (prev) = 0;

	  /* Complain about unresolved common block symbols.  */
	  if (SYMBOL_CLASS (prev) == LOC_STATIC)
	    SYMBOL_CLASS (prev) = LOC_UNRESOLVED;
	  else
	    complaint (&symfile_complaints,
		       "%s: common block `%s' from global_sym_chain unresolved",
		       objfile->name, DEPRECATED_SYMBOL_NAME (prev));
	}
    }
  memset (global_sym_chain, 0, sizeof (global_sym_chain));
}

/* Initialize anything that needs initializing when starting to read
   a fresh piece of a symbol file, e.g. reading in the stuff corresponding
   to a psymtab.  */

void
stabsread_init (void)
{
}

/* Initialize anything that needs initializing when a completely new
   symbol file is specified (not just adding some symbols from another
   file, e.g. a shared library).  */

void
stabsread_new_init (void)
{
  /* Empty the hash table of global syms looking for values.  */
  memset (global_sym_chain, 0, sizeof (global_sym_chain));
}

/* Initialize anything that needs initializing at the same time as
   start_symtab() is called. */

void
start_stabs (void)
{
  global_stabs = NULL;		/* AIX COFF */
  /* Leave FILENUM of 0 free for builtin types and this file's types.  */
  n_this_object_header_files = 1;
  type_vector_length = 0;
  type_vector = (struct type **) 0;

  /* FIXME: If common_block_name is not already NULL, we should complain().  */
  common_block_name = NULL;
}

/* Call after end_symtab() */

void
end_stabs (void)
{
  if (type_vector)
    {
      xfree (type_vector);
    }
  type_vector = 0;
  type_vector_length = 0;
  previous_stab_code = 0;
}

void
finish_global_stabs (struct objfile *objfile)
{
  if (global_stabs)
    {
      patch_block_stabs (global_symbols, global_stabs, objfile);
      xfree (global_stabs);
      global_stabs = NULL;
    }
}

/* Find the end of the name, delimited by a ':', but don't match
   ObjC symbols which look like -[Foo bar::]:bla.  */
static char *
find_name_end (char *name)
{
  char *s = name;
  if (s[0] == '-' || *s == '+')
    {
      /* Must be an ObjC method symbol.  */
      if (s[1] != '[')
	{
	  error ("invalid symbol name \"%s\"", name);
	}
      s = strchr (s, ']');
      if (s == NULL)
	{
	  error ("invalid symbol name \"%s\"", name);
	}
      return strchr (s, ':');
    }
  else
    {
      return strchr (s, ':');
    }
}

/* Initializer for this module */

void
_initialize_stabsread (void)
{
  undef_types_allocated = 20;
  undef_types_length = 0;
  undef_types = (struct type **)
    xmalloc (undef_types_allocated * sizeof (struct type *));
}
