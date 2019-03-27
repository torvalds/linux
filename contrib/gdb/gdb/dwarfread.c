/* DWARF debugging format support for GDB.

   Copyright 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999,
   2000, 2001, 2002, 2003, 2004 Free Software Foundation, Inc.

   Written by Fred Fish at Cygnus Support.  Portions based on dbxread.c,
   mipsread.c, coffread.c, and dwarfread.c from a Data General SVR4 gdb port.

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
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.  */

/*
   If you are looking for DWARF-2 support, you are in the wrong file.
   Go look in dwarf2read.c.  This file is for the original DWARF,
   also known as DWARF-1.

   DWARF-1 is slowly headed for obsoletion.

   In gcc HEAD 2003-11-29 16:28:31 UTC, no targets prefer dwarf-1.

   In gcc 3.3.2, these targets prefer dwarf-1:

     i[34567]86-sequent-ptx4*
     i[34567]86-sequent-sysv4*
     mips-sni-sysv4
     sparc-hal-solaris2*

   In gcc 3.2.2, these targets prefer dwarf-1:

     i[34567]86-dg-dgux*
     i[34567]86-sequent-ptx4*
     i[34567]86-sequent-sysv4*
     m88k-dg-dgux*
     mips-sni-sysv4
     sparc-hal-solaris2*

   In gcc 2.95.3, these targets prefer dwarf-1:

     i[34567]86-dg-dgux*
     i[34567]86-ncr-sysv4*
     i[34567]86-sequent-ptx4*
     i[34567]86-sequent-sysv4*
     i[34567]86-*-osf1*
     i[34567]86-*-sco3.2v5*
     i[34567]86-*-sysv4*
     i860-alliant-*
     i860-*-sysv4*
     m68k-atari-sysv4*
     m68k-cbm-sysv4*
     m68k-*-sysv4*
     m88k-dg-dgux*
     m88k-*-sysv4*
     mips-sni-sysv4
     mips-*-gnu*
     sh-*-elf*
     sh-*-rtemself*
     sparc-hal-solaris2*
     sparc-*-sysv4*

   Some non-gcc compilers produce dwarf-1: 

     PR gdb/1179 was from a user with Diab C++ 4.3.
     Other users have also reported using Diab compilers with dwarf-1.
     On 2003-06-09 the gdb list received a report from a user
       with Absoft ProFortran f77 which is dwarf-1.

   -- chastain 2003-12-01
*/

/*

   FIXME: Do we need to generate dependencies in partial symtabs?
   (Perhaps we don't need to).

   FIXME: Resolve minor differences between what information we put in the
   partial symbol table and what dbxread puts in.  For example, we don't yet
   put enum constants there.  And dbxread seems to invent a lot of typedefs
   we never see.  Use the new printpsym command to see the partial symbol table
   contents.

   FIXME: Figure out a better way to tell gdb about the name of the function
   contain the user's entry point (I.E. main())

   FIXME: See other FIXME's and "ifdef 0" scattered throughout the code for
   other things to work on, if you get bored. :-)

 */

#include "defs.h"
#include "symtab.h"
#include "gdbtypes.h"
#include "objfiles.h"
#include "elf/dwarf.h"
#include "buildsym.h"
#include "demangle.h"
#include "expression.h"		/* Needed for enum exp_opcode in language.h, sigh... */
#include "language.h"
#include "complaints.h"

#include <fcntl.h>
#include "gdb_string.h"

/* Some macros to provide DIE info for complaints. */

#define DIE_ID (curdie!=NULL ? curdie->die_ref : 0)
#define DIE_NAME (curdie!=NULL && curdie->at_name!=NULL) ? curdie->at_name : ""

/* Complaints that can be issued during DWARF debug info reading. */

static void
bad_die_ref_complaint (int arg1, const char *arg2, int arg3)
{
  complaint (&symfile_complaints,
	     "DIE @ 0x%x \"%s\", reference to DIE (0x%x) outside compilation unit",
	     arg1, arg2, arg3);
}

static void
unknown_attribute_form_complaint (int arg1, const char *arg2, int arg3)
{
  complaint (&symfile_complaints,
	     "DIE @ 0x%x \"%s\", unknown attribute form (0x%x)", arg1, arg2,
	     arg3);
}

static void
dup_user_type_definition_complaint (int arg1, const char *arg2)
{
  complaint (&symfile_complaints,
	     "DIE @ 0x%x \"%s\", internal error: duplicate user type definition",
	     arg1, arg2);
}

static void
bad_array_element_type_complaint (int arg1, const char *arg2, int arg3)
{
  complaint (&symfile_complaints,
	     "DIE @ 0x%x \"%s\", bad array element type attribute 0x%x", arg1,
	     arg2, arg3);
}

typedef unsigned int DIE_REF;	/* Reference to a DIE */

#ifndef GCC_PRODUCER
#define GCC_PRODUCER "GNU C "
#endif

#ifndef GPLUS_PRODUCER
#define GPLUS_PRODUCER "GNU C++ "
#endif

#ifndef LCC_PRODUCER
#define LCC_PRODUCER "NCR C/C++"
#endif

/* Flags to target_to_host() that tell whether or not the data object is
   expected to be signed.  Used, for example, when fetching a signed
   integer in the target environment which is used as a signed integer
   in the host environment, and the two environments have different sized
   ints.  In this case, *somebody* has to sign extend the smaller sized
   int. */

#define GET_UNSIGNED	0	/* No sign extension required */
#define GET_SIGNED	1	/* Sign extension required */

/* Defines for things which are specified in the document "DWARF Debugging
   Information Format" published by UNIX International, Programming Languages
   SIG.  These defines are based on revision 1.0.0, Jan 20, 1992. */

#define SIZEOF_DIE_LENGTH	4
#define SIZEOF_DIE_TAG		2
#define SIZEOF_ATTRIBUTE	2
#define SIZEOF_FORMAT_SPECIFIER	1
#define SIZEOF_FMT_FT		2
#define SIZEOF_LINETBL_LENGTH	4
#define SIZEOF_LINETBL_LINENO	4
#define SIZEOF_LINETBL_STMT	2
#define SIZEOF_LINETBL_DELTA	4
#define SIZEOF_LOC_ATOM_CODE	1

#define FORM_FROM_ATTR(attr)	((attr) & 0xF)	/* Implicitly specified */

/* Macros that return the sizes of various types of data in the target
   environment.

   FIXME:  Currently these are just compile time constants (as they are in
   other parts of gdb as well).  They need to be able to get the right size
   either from the bfd or possibly from the DWARF info.  It would be nice if
   the DWARF producer inserted DIES that describe the fundamental types in
   the target environment into the DWARF info, similar to the way dbx stabs
   producers produce information about their fundamental types. */

#define TARGET_FT_POINTER_SIZE(objfile)	(TARGET_PTR_BIT / TARGET_CHAR_BIT)
#define TARGET_FT_LONG_SIZE(objfile)	(TARGET_LONG_BIT / TARGET_CHAR_BIT)

/* The Amiga SVR4 header file <dwarf.h> defines AT_element_list as a
   FORM_BLOCK2, and this is the value emitted by the AT&T compiler.
   However, the Issue 2 DWARF specification from AT&T defines it as
   a FORM_BLOCK4, as does the latest specification from UI/PLSIG.
   For backwards compatibility with the AT&T compiler produced executables
   we define AT_short_element_list for this variant. */

#define	AT_short_element_list	 (0x00f0|FORM_BLOCK2)

/* The DWARF debugging information consists of two major pieces,
   one is a block of DWARF Information Entries (DIE's) and the other
   is a line number table.  The "struct dieinfo" structure contains
   the information for a single DIE, the one currently being processed.

   In order to make it easier to randomly access the attribute fields
   of the current DIE, which are specifically unordered within the DIE,
   each DIE is scanned and an instance of the "struct dieinfo"
   structure is initialized.

   Initialization is done in two levels.  The first, done by basicdieinfo(),
   just initializes those fields that are vital to deciding whether or not
   to use this DIE, how to skip past it, etc.  The second, done by the
   function completedieinfo(), fills in the rest of the information.

   Attributes which have block forms are not interpreted at the time
   the DIE is scanned, instead we just save pointers to the start
   of their value fields.

   Some fields have a flag <name>_p that is set when the value of the
   field is valid (I.E. we found a matching attribute in the DIE).  Since
   we may want to test for the presence of some attributes in the DIE,
   such as AT_low_pc, without restricting the values of the field,
   we need someway to note that we found such an attribute.

 */

typedef char BLOCK;

struct dieinfo
  {
    char *die;			/* Pointer to the raw DIE data */
    unsigned long die_length;	/* Length of the raw DIE data */
    DIE_REF die_ref;		/* Offset of this DIE */
    unsigned short die_tag;	/* Tag for this DIE */
    unsigned long at_padding;
    unsigned long at_sibling;
    BLOCK *at_location;
    char *at_name;
    unsigned short at_fund_type;
    BLOCK *at_mod_fund_type;
    unsigned long at_user_def_type;
    BLOCK *at_mod_u_d_type;
    unsigned short at_ordering;
    BLOCK *at_subscr_data;
    unsigned long at_byte_size;
    unsigned short at_bit_offset;
    unsigned long at_bit_size;
    BLOCK *at_element_list;
    unsigned long at_stmt_list;
    CORE_ADDR at_low_pc;
    CORE_ADDR at_high_pc;
    unsigned long at_language;
    unsigned long at_member;
    unsigned long at_discr;
    BLOCK *at_discr_value;
    BLOCK *at_string_length;
    char *at_comp_dir;
    char *at_producer;
    unsigned long at_start_scope;
    unsigned long at_stride_size;
    unsigned long at_src_info;
    char *at_prototyped;
    unsigned int has_at_low_pc:1;
    unsigned int has_at_stmt_list:1;
    unsigned int has_at_byte_size:1;
    unsigned int short_element_list:1;

    /* Kludge to identify register variables */

    unsigned int isreg;

    /* Kludge to identify optimized out variables */

    unsigned int optimized_out;

    /* Kludge to identify basereg references.
       Nonzero if we have an offset relative to a basereg.  */

    unsigned int offreg;

    /* Kludge to identify which base register is it relative to.  */

    unsigned int basereg;
  };

static int diecount;		/* Approximate count of dies for compilation unit */
static struct dieinfo *curdie;	/* For warnings and such */

static char *dbbase;		/* Base pointer to dwarf info */
static int dbsize;		/* Size of dwarf info in bytes */
static int dbroff;		/* Relative offset from start of .debug section */
static char *lnbase;		/* Base pointer to line section */

/* This value is added to each symbol value.  FIXME:  Generalize to 
   the section_offsets structure used by dbxread (once this is done,
   pass the appropriate section number to end_symtab).  */
static CORE_ADDR baseaddr;	/* Add to each symbol value */

/* The section offsets used in the current psymtab or symtab.  FIXME,
   only used to pass one value (baseaddr) at the moment.  */
static struct section_offsets *base_section_offsets;

/* We put a pointer to this structure in the read_symtab_private field
   of the psymtab.  */

struct dwfinfo
  {
    /* Always the absolute file offset to the start of the ".debug"
       section for the file containing the DIE's being accessed.  */
    file_ptr dbfoff;
    /* Relative offset from the start of the ".debug" section to the
       first DIE to be accessed.  When building the partial symbol
       table, this value will be zero since we are accessing the
       entire ".debug" section.  When expanding a partial symbol
       table entry, this value will be the offset to the first
       DIE for the compilation unit containing the symbol that
       triggers the expansion.  */
    int dbroff;
    /* The size of the chunk of DIE's being examined, in bytes.  */
    int dblength;
    /* The absolute file offset to the line table fragment.  Ignored
       when building partial symbol tables, but used when expanding
       them, and contains the absolute file offset to the fragment
       of the ".line" section containing the line numbers for the
       current compilation unit.  */
    file_ptr lnfoff;
  };

#define DBFOFF(p) (((struct dwfinfo *)((p)->read_symtab_private))->dbfoff)
#define DBROFF(p) (((struct dwfinfo *)((p)->read_symtab_private))->dbroff)
#define DBLENGTH(p) (((struct dwfinfo *)((p)->read_symtab_private))->dblength)
#define LNFOFF(p) (((struct dwfinfo *)((p)->read_symtab_private))->lnfoff)

/* The generic symbol table building routines have separate lists for
   file scope symbols and all all other scopes (local scopes).  So
   we need to select the right one to pass to add_symbol_to_list().
   We do it by keeping a pointer to the correct list in list_in_scope.

   FIXME:  The original dwarf code just treated the file scope as the first
   local scope, and all other local scopes as nested local scopes, and worked
   fine.  Check to see if we really need to distinguish these in buildsym.c */

struct pending **list_in_scope = &file_symbols;

/* DIES which have user defined types or modified user defined types refer to
   other DIES for the type information.  Thus we need to associate the offset
   of a DIE for a user defined type with a pointer to the type information.

   Originally this was done using a simple but expensive algorithm, with an
   array of unsorted structures, each containing an offset/type-pointer pair.
   This array was scanned linearly each time a lookup was done.  The result
   was that gdb was spending over half it's startup time munging through this
   array of pointers looking for a structure that had the right offset member.

   The second attempt used the same array of structures, but the array was
   sorted using qsort each time a new offset/type was recorded, and a binary
   search was used to find the type pointer for a given DIE offset.  This was
   even slower, due to the overhead of sorting the array each time a new
   offset/type pair was entered.

   The third attempt uses a fixed size array of type pointers, indexed by a
   value derived from the DIE offset.  Since the minimum DIE size is 4 bytes,
   we can divide any DIE offset by 4 to obtain a unique index into this fixed
   size array.  Since each element is a 4 byte pointer, it takes exactly as
   much memory to hold this array as to hold the DWARF info for a given
   compilation unit.  But it gets freed as soon as we are done with it.
   This has worked well in practice, as a reasonable tradeoff between memory
   consumption and speed, without having to resort to much more complicated
   algorithms. */

static struct type **utypes;	/* Pointer to array of user type pointers */
static int numutypes;		/* Max number of user type pointers */

/* Maintain an array of referenced fundamental types for the current
   compilation unit being read.  For DWARF version 1, we have to construct
   the fundamental types on the fly, since no information about the
   fundamental types is supplied.  Each such fundamental type is created by
   calling a language dependent routine to create the type, and then a
   pointer to that type is then placed in the array at the index specified
   by it's FT_<TYPENAME> value.  The array has a fixed size set by the
   FT_NUM_MEMBERS compile time constant, which is the number of predefined
   fundamental types gdb knows how to construct. */

static struct type *ftypes[FT_NUM_MEMBERS];	/* Fundamental types */

/* Record the language for the compilation unit which is currently being
   processed.  We know it once we have seen the TAG_compile_unit DIE,
   and we need it while processing the DIE's for that compilation unit.
   It is eventually saved in the symtab structure, but we don't finalize
   the symtab struct until we have processed all the DIE's for the
   compilation unit.  We also need to get and save a pointer to the 
   language struct for this language, so we can call the language
   dependent routines for doing things such as creating fundamental
   types. */

static enum language cu_language;
static const struct language_defn *cu_language_defn;

/* Forward declarations of static functions so we don't have to worry
   about ordering within this file.  */

static void free_utypes (void *);

static int attribute_size (unsigned int);

static CORE_ADDR target_to_host (char *, int, int, struct objfile *);

static void add_enum_psymbol (struct dieinfo *, struct objfile *);

static void handle_producer (char *);

static void read_file_scope (struct dieinfo *, char *, char *,
			     struct objfile *);

static void read_func_scope (struct dieinfo *, char *, char *,
			     struct objfile *);

static void read_lexical_block_scope (struct dieinfo *, char *, char *,
				      struct objfile *);

static void scan_partial_symbols (char *, char *, struct objfile *);

static void scan_compilation_units (char *, char *, file_ptr, file_ptr,
				    struct objfile *);

static void add_partial_symbol (struct dieinfo *, struct objfile *);

static void basicdieinfo (struct dieinfo *, char *, struct objfile *);

static void completedieinfo (struct dieinfo *, struct objfile *);

static void dwarf_psymtab_to_symtab (struct partial_symtab *);

static void psymtab_to_symtab_1 (struct partial_symtab *);

static void read_ofile_symtab (struct partial_symtab *);

static void process_dies (char *, char *, struct objfile *);

static void read_structure_scope (struct dieinfo *, char *, char *,
				  struct objfile *);

static struct type *decode_array_element_type (char *);

static struct type *decode_subscript_data_item (char *, char *);

static void dwarf_read_array_type (struct dieinfo *);

static void read_tag_pointer_type (struct dieinfo *dip);

static void read_tag_string_type (struct dieinfo *dip);

static void read_subroutine_type (struct dieinfo *, char *, char *);

static void read_enumeration (struct dieinfo *, char *, char *,
			      struct objfile *);

static struct type *struct_type (struct dieinfo *, char *, char *,
				 struct objfile *);

static struct type *enum_type (struct dieinfo *, struct objfile *);

static void decode_line_numbers (char *);

static struct type *decode_die_type (struct dieinfo *);

static struct type *decode_mod_fund_type (char *);

static struct type *decode_mod_u_d_type (char *);

static struct type *decode_modified_type (char *, unsigned int, int);

static struct type *decode_fund_type (unsigned int);

static char *create_name (char *, struct obstack *);

static struct type *lookup_utype (DIE_REF);

static struct type *alloc_utype (DIE_REF, struct type *);

static struct symbol *new_symbol (struct dieinfo *, struct objfile *);

static void synthesize_typedef (struct dieinfo *, struct objfile *,
				struct type *);

static int locval (struct dieinfo *);

static void set_cu_language (struct dieinfo *);

static struct type *dwarf_fundamental_type (struct objfile *, int);


/*

   LOCAL FUNCTION

   dwarf_fundamental_type -- lookup or create a fundamental type

   SYNOPSIS

   struct type *
   dwarf_fundamental_type (struct objfile *objfile, int typeid)

   DESCRIPTION

   DWARF version 1 doesn't supply any fundamental type information,
   so gdb has to construct such types.  It has a fixed number of
   fundamental types that it knows how to construct, which is the
   union of all types that it knows how to construct for all languages
   that it knows about.  These are enumerated in gdbtypes.h.

   As an example, assume we find a DIE that references a DWARF
   fundamental type of FT_integer.  We first look in the ftypes
   array to see if we already have such a type, indexed by the
   gdb internal value of FT_INTEGER.  If so, we simply return a
   pointer to that type.  If not, then we ask an appropriate
   language dependent routine to create a type FT_INTEGER, using
   defaults reasonable for the current target machine, and install
   that type in ftypes for future reference.

   RETURNS

   Pointer to a fundamental type.

 */

static struct type *
dwarf_fundamental_type (struct objfile *objfile, int typeid)
{
  if (typeid < 0 || typeid >= FT_NUM_MEMBERS)
    {
      error ("internal error - invalid fundamental type id %d", typeid);
    }

  /* Look for this particular type in the fundamental type vector.  If one is
     not found, create and install one appropriate for the current language
     and the current target machine. */

  if (ftypes[typeid] == NULL)
    {
      ftypes[typeid] = cu_language_defn->la_fund_type (objfile, typeid);
    }

  return (ftypes[typeid]);
}

/*

   LOCAL FUNCTION

   set_cu_language -- set local copy of language for compilation unit

   SYNOPSIS

   void
   set_cu_language (struct dieinfo *dip)

   DESCRIPTION

   Decode the language attribute for a compilation unit DIE and
   remember what the language was.  We use this at various times
   when processing DIE's for a given compilation unit.

   RETURNS

   No return value.

 */

static void
set_cu_language (struct dieinfo *dip)
{
  switch (dip->at_language)
    {
    case LANG_C89:
    case LANG_C:
      cu_language = language_c;
      break;
    case LANG_C_PLUS_PLUS:
      cu_language = language_cplus;
      break;
    case LANG_MODULA2:
      cu_language = language_m2;
      break;
    case LANG_FORTRAN77:
    case LANG_FORTRAN90:
      cu_language = language_fortran;
      break;
    case LANG_ADA83:
    case LANG_COBOL74:
    case LANG_COBOL85:
    case LANG_PASCAL83:
      /* We don't know anything special about these yet. */
      cu_language = language_unknown;
      break;
    default:
      /* If no at_language, try to deduce one from the filename */
      cu_language = deduce_language_from_filename (dip->at_name);
      break;
    }
  cu_language_defn = language_def (cu_language);
}

/*

   GLOBAL FUNCTION

   dwarf_build_psymtabs -- build partial symtabs from DWARF debug info

   SYNOPSIS

   void dwarf_build_psymtabs (struct objfile *objfile,
   int mainline, file_ptr dbfoff, unsigned int dbfsize,
   file_ptr lnoffset, unsigned int lnsize)

   DESCRIPTION

   This function is called upon to build partial symtabs from files
   containing DIE's (Dwarf Information Entries) and DWARF line numbers.

   It is passed a bfd* containing the DIES
   and line number information, the corresponding filename for that
   file, a base address for relocating the symbols, a flag indicating
   whether or not this debugging information is from a "main symbol
   table" rather than a shared library or dynamically linked file,
   and file offset/size pairs for the DIE information and line number
   information.

   RETURNS

   No return value.

 */

void
dwarf_build_psymtabs (struct objfile *objfile, int mainline, file_ptr dbfoff,
		      unsigned int dbfsize, file_ptr lnoffset,
		      unsigned int lnsize)
{
  bfd *abfd = objfile->obfd;
  struct cleanup *back_to;

  current_objfile = objfile;
  dbsize = dbfsize;
  dbbase = xmalloc (dbsize);
  dbroff = 0;
  if ((bfd_seek (abfd, dbfoff, SEEK_SET) != 0) ||
      (bfd_bread (dbbase, dbsize, abfd) != dbsize))
    {
      xfree (dbbase);
      error ("can't read DWARF data from '%s'", bfd_get_filename (abfd));
    }
  back_to = make_cleanup (xfree, dbbase);

  /* If we are reinitializing, or if we have never loaded syms yet, init.
     Since we have no idea how many DIES we are looking at, we just guess
     some arbitrary value. */

  if (mainline
      || (objfile->global_psymbols.size == 0
	  && objfile->static_psymbols.size == 0))
    {
      init_psymbol_list (objfile, 1024);
    }

  /* Save the relocation factor where everybody can see it.  */

  base_section_offsets = objfile->section_offsets;
  baseaddr = ANOFFSET (objfile->section_offsets, 0);

  /* Follow the compilation unit sibling chain, building a partial symbol
     table entry for each one.  Save enough information about each compilation
     unit to locate the full DWARF information later. */

  scan_compilation_units (dbbase, dbbase + dbsize, dbfoff, lnoffset, objfile);

  do_cleanups (back_to);
  current_objfile = NULL;
}

/*

   LOCAL FUNCTION

   read_lexical_block_scope -- process all dies in a lexical block

   SYNOPSIS

   static void read_lexical_block_scope (struct dieinfo *dip,
   char *thisdie, char *enddie)

   DESCRIPTION

   Process all the DIES contained within a lexical block scope.
   Start a new scope, process the dies, and then close the scope.

 */

static void
read_lexical_block_scope (struct dieinfo *dip, char *thisdie, char *enddie,
			  struct objfile *objfile)
{
  struct context_stack *new;

  push_context (0, dip->at_low_pc);
  process_dies (thisdie + dip->die_length, enddie, objfile);
  new = pop_context ();
  if (local_symbols != NULL)
    {
      finish_block (0, &local_symbols, new->old_blocks, new->start_addr,
		    dip->at_high_pc, objfile);
    }
  local_symbols = new->locals;
}

/*

   LOCAL FUNCTION

   lookup_utype -- look up a user defined type from die reference

   SYNOPSIS

   static type *lookup_utype (DIE_REF die_ref)

   DESCRIPTION

   Given a DIE reference, lookup the user defined type associated with
   that DIE, if it has been registered already.  If not registered, then
   return NULL.  Alloc_utype() can be called to register an empty
   type for this reference, which will be filled in later when the
   actual referenced DIE is processed.
 */

static struct type *
lookup_utype (DIE_REF die_ref)
{
  struct type *type = NULL;
  int utypeidx;

  utypeidx = (die_ref - dbroff) / 4;
  if ((utypeidx < 0) || (utypeidx >= numutypes))
    {
      bad_die_ref_complaint (DIE_ID, DIE_NAME, die_ref);
    }
  else
    {
      type = *(utypes + utypeidx);
    }
  return (type);
}


/*

   LOCAL FUNCTION

   alloc_utype  -- add a user defined type for die reference

   SYNOPSIS

   static type *alloc_utype (DIE_REF die_ref, struct type *utypep)

   DESCRIPTION

   Given a die reference DIE_REF, and a possible pointer to a user
   defined type UTYPEP, register that this reference has a user
   defined type and either use the specified type in UTYPEP or
   make a new empty type that will be filled in later.

   We should only be called after calling lookup_utype() to verify that
   there is not currently a type registered for DIE_REF.
 */

static struct type *
alloc_utype (DIE_REF die_ref, struct type *utypep)
{
  struct type **typep;
  int utypeidx;

  utypeidx = (die_ref - dbroff) / 4;
  typep = utypes + utypeidx;
  if ((utypeidx < 0) || (utypeidx >= numutypes))
    {
      utypep = dwarf_fundamental_type (current_objfile, FT_INTEGER);
      bad_die_ref_complaint (DIE_ID, DIE_NAME, die_ref);
    }
  else if (*typep != NULL)
    {
      utypep = *typep;
      complaint (&symfile_complaints,
		 "DIE @ 0x%x \"%s\", internal error: duplicate user type allocation",
		 DIE_ID, DIE_NAME);
    }
  else
    {
      if (utypep == NULL)
	{
	  utypep = alloc_type (current_objfile);
	}
      *typep = utypep;
    }
  return (utypep);
}

/*

   LOCAL FUNCTION

   free_utypes -- free the utypes array and reset pointer & count

   SYNOPSIS

   static void free_utypes (void *dummy)

   DESCRIPTION

   Called via do_cleanups to free the utypes array, reset the pointer to NULL,
   and set numutypes back to zero.  This ensures that the utypes does not get
   referenced after being freed.
 */

static void
free_utypes (void *dummy)
{
  xfree (utypes);
  utypes = NULL;
  numutypes = 0;
}


/*

   LOCAL FUNCTION

   decode_die_type -- return a type for a specified die

   SYNOPSIS

   static struct type *decode_die_type (struct dieinfo *dip)

   DESCRIPTION

   Given a pointer to a die information structure DIP, decode the
   type of the die and return a pointer to the decoded type.  All
   dies without specific types default to type int.
 */

static struct type *
decode_die_type (struct dieinfo *dip)
{
  struct type *type = NULL;

  if (dip->at_fund_type != 0)
    {
      type = decode_fund_type (dip->at_fund_type);
    }
  else if (dip->at_mod_fund_type != NULL)
    {
      type = decode_mod_fund_type (dip->at_mod_fund_type);
    }
  else if (dip->at_user_def_type)
    {
      type = lookup_utype (dip->at_user_def_type);
      if (type == NULL)
	{
	  type = alloc_utype (dip->at_user_def_type, NULL);
	}
    }
  else if (dip->at_mod_u_d_type)
    {
      type = decode_mod_u_d_type (dip->at_mod_u_d_type);
    }
  else
    {
      type = dwarf_fundamental_type (current_objfile, FT_VOID);
    }
  return (type);
}

/*

   LOCAL FUNCTION

   struct_type -- compute and return the type for a struct or union

   SYNOPSIS

   static struct type *struct_type (struct dieinfo *dip, char *thisdie,
   char *enddie, struct objfile *objfile)

   DESCRIPTION

   Given pointer to a die information structure for a die which
   defines a union or structure (and MUST define one or the other),
   and pointers to the raw die data that define the range of dies which
   define the members, compute and return the user defined type for the
   structure or union.
 */

static struct type *
struct_type (struct dieinfo *dip, char *thisdie, char *enddie,
	     struct objfile *objfile)
{
  struct type *type;
  struct nextfield
    {
      struct nextfield *next;
      struct field field;
    };
  struct nextfield *list = NULL;
  struct nextfield *new;
  int nfields = 0;
  int n;
  struct dieinfo mbr;
  char *nextdie;
  int anonymous_size;

  type = lookup_utype (dip->die_ref);
  if (type == NULL)
    {
      /* No forward references created an empty type, so install one now */
      type = alloc_utype (dip->die_ref, NULL);
    }
  INIT_CPLUS_SPECIFIC (type);
  switch (dip->die_tag)
    {
    case TAG_class_type:
      TYPE_CODE (type) = TYPE_CODE_CLASS;
      break;
    case TAG_structure_type:
      TYPE_CODE (type) = TYPE_CODE_STRUCT;
      break;
    case TAG_union_type:
      TYPE_CODE (type) = TYPE_CODE_UNION;
      break;
    default:
      /* Should never happen */
      TYPE_CODE (type) = TYPE_CODE_UNDEF;
      complaint (&symfile_complaints,
		 "DIE @ 0x%x \"%s\", missing class, structure, or union tag",
		 DIE_ID, DIE_NAME);
      break;
    }
  /* Some compilers try to be helpful by inventing "fake" names for
     anonymous enums, structures, and unions, like "~0fake" or ".0fake".
     Thanks, but no thanks... */
  if (dip->at_name != NULL
      && *dip->at_name != '~'
      && *dip->at_name != '.')
    {
      TYPE_TAG_NAME (type) = obconcat (&objfile->objfile_obstack,
				       "", "", dip->at_name);
    }
  /* Use whatever size is known.  Zero is a valid size.  We might however
     wish to check has_at_byte_size to make sure that some byte size was
     given explicitly, but DWARF doesn't specify that explicit sizes of
     zero have to present, so complaining about missing sizes should 
     probably not be the default. */
  TYPE_LENGTH (type) = dip->at_byte_size;
  thisdie += dip->die_length;
  while (thisdie < enddie)
    {
      basicdieinfo (&mbr, thisdie, objfile);
      completedieinfo (&mbr, objfile);
      if (mbr.die_length <= SIZEOF_DIE_LENGTH)
	{
	  break;
	}
      else if (mbr.at_sibling != 0)
	{
	  nextdie = dbbase + mbr.at_sibling - dbroff;
	}
      else
	{
	  nextdie = thisdie + mbr.die_length;
	}
      switch (mbr.die_tag)
	{
	case TAG_member:
	  /* Static fields can be either TAG_global_variable (GCC) or else
	     TAG_member with no location (Diab).  We could treat the latter like
	     the former... but since we don't support the former, just avoid
	     crashing on the latter for now.  */
	  if (mbr.at_location == NULL)
	    break;

	  /* Get space to record the next field's data.  */
	  new = (struct nextfield *) alloca (sizeof (struct nextfield));
	  new->next = list;
	  list = new;
	  /* Save the data.  */
	  list->field.name =
	    obsavestring (mbr.at_name, strlen (mbr.at_name),
			  &objfile->objfile_obstack);
	  FIELD_TYPE (list->field) = decode_die_type (&mbr);
	  FIELD_BITPOS (list->field) = 8 * locval (&mbr);
	  FIELD_STATIC_KIND (list->field) = 0;
	  /* Handle bit fields. */
	  FIELD_BITSIZE (list->field) = mbr.at_bit_size;
	  if (BITS_BIG_ENDIAN)
	    {
	      /* For big endian bits, the at_bit_offset gives the
	         additional bit offset from the MSB of the containing
	         anonymous object to the MSB of the field.  We don't
	         have to do anything special since we don't need to
	         know the size of the anonymous object. */
	      FIELD_BITPOS (list->field) += mbr.at_bit_offset;
	    }
	  else
	    {
	      /* For little endian bits, we need to have a non-zero
	         at_bit_size, so that we know we are in fact dealing
	         with a bitfield.  Compute the bit offset to the MSB
	         of the anonymous object, subtract off the number of
	         bits from the MSB of the field to the MSB of the
	         object, and then subtract off the number of bits of
	         the field itself.  The result is the bit offset of
	         the LSB of the field. */
	      if (mbr.at_bit_size > 0)
		{
		  if (mbr.has_at_byte_size)
		    {
		      /* The size of the anonymous object containing
		         the bit field is explicit, so use the
		         indicated size (in bytes). */
		      anonymous_size = mbr.at_byte_size;
		    }
		  else
		    {
		      /* The size of the anonymous object containing
		         the bit field matches the size of an object
		         of the bit field's type.  DWARF allows
		         at_byte_size to be left out in such cases, as
		         a debug information size optimization. */
		      anonymous_size = TYPE_LENGTH (list->field.type);
		    }
		  FIELD_BITPOS (list->field) +=
		    anonymous_size * 8 - mbr.at_bit_offset - mbr.at_bit_size;
		}
	    }
	  nfields++;
	  break;
	default:
	  process_dies (thisdie, nextdie, objfile);
	  break;
	}
      thisdie = nextdie;
    }
  /* Now create the vector of fields, and record how big it is.  We may
     not even have any fields, if this DIE was generated due to a reference
     to an anonymous structure or union.  In this case, TYPE_FLAG_STUB is
     set, which clues gdb in to the fact that it needs to search elsewhere
     for the full structure definition. */
  if (nfields == 0)
    {
      TYPE_FLAGS (type) |= TYPE_FLAG_STUB;
    }
  else
    {
      TYPE_NFIELDS (type) = nfields;
      TYPE_FIELDS (type) = (struct field *)
	TYPE_ALLOC (type, sizeof (struct field) * nfields);
      /* Copy the saved-up fields into the field vector.  */
      for (n = nfields; list; list = list->next)
	{
	  TYPE_FIELD (type, --n) = list->field;
	}
    }
  return (type);
}

/*

   LOCAL FUNCTION

   read_structure_scope -- process all dies within struct or union

   SYNOPSIS

   static void read_structure_scope (struct dieinfo *dip,
   char *thisdie, char *enddie, struct objfile *objfile)

   DESCRIPTION

   Called when we find the DIE that starts a structure or union
   scope (definition) to process all dies that define the members
   of the structure or union.  DIP is a pointer to the die info
   struct for the DIE that names the structure or union.

   NOTES

   Note that we need to call struct_type regardless of whether or not
   the DIE has an at_name attribute, since it might be an anonymous
   structure or union.  This gets the type entered into our set of
   user defined types.

   However, if the structure is incomplete (an opaque struct/union)
   then suppress creating a symbol table entry for it since gdb only
   wants to find the one with the complete definition.  Note that if
   it is complete, we just call new_symbol, which does it's own
   checking about whether the struct/union is anonymous or not (and
   suppresses creating a symbol table entry itself).

 */

static void
read_structure_scope (struct dieinfo *dip, char *thisdie, char *enddie,
		      struct objfile *objfile)
{
  struct type *type;
  struct symbol *sym;

  type = struct_type (dip, thisdie, enddie, objfile);
  if (!TYPE_STUB (type))
    {
      sym = new_symbol (dip, objfile);
      if (sym != NULL)
	{
	  SYMBOL_TYPE (sym) = type;
	  if (cu_language == language_cplus)
	    {
	      synthesize_typedef (dip, objfile, type);
	    }
	}
    }
}

/*

   LOCAL FUNCTION

   decode_array_element_type -- decode type of the array elements

   SYNOPSIS

   static struct type *decode_array_element_type (char *scan, char *end)

   DESCRIPTION

   As the last step in decoding the array subscript information for an
   array DIE, we need to decode the type of the array elements.  We are
   passed a pointer to this last part of the subscript information and
   must return the appropriate type.  If the type attribute is not
   recognized, just warn about the problem and return type int.
 */

static struct type *
decode_array_element_type (char *scan)
{
  struct type *typep;
  DIE_REF die_ref;
  unsigned short attribute;
  unsigned short fundtype;
  int nbytes;

  attribute = target_to_host (scan, SIZEOF_ATTRIBUTE, GET_UNSIGNED,
			      current_objfile);
  scan += SIZEOF_ATTRIBUTE;
  nbytes = attribute_size (attribute);
  if (nbytes == -1)
    {
      bad_array_element_type_complaint (DIE_ID, DIE_NAME, attribute);
      typep = dwarf_fundamental_type (current_objfile, FT_INTEGER);
    }
  else
    {
      switch (attribute)
	{
	case AT_fund_type:
	  fundtype = target_to_host (scan, nbytes, GET_UNSIGNED,
				     current_objfile);
	  typep = decode_fund_type (fundtype);
	  break;
	case AT_mod_fund_type:
	  typep = decode_mod_fund_type (scan);
	  break;
	case AT_user_def_type:
	  die_ref = target_to_host (scan, nbytes, GET_UNSIGNED,
				    current_objfile);
	  typep = lookup_utype (die_ref);
	  if (typep == NULL)
	    {
	      typep = alloc_utype (die_ref, NULL);
	    }
	  break;
	case AT_mod_u_d_type:
	  typep = decode_mod_u_d_type (scan);
	  break;
	default:
	  bad_array_element_type_complaint (DIE_ID, DIE_NAME, attribute);
	  typep = dwarf_fundamental_type (current_objfile, FT_INTEGER);
	  break;
	}
    }
  return (typep);
}

/*

   LOCAL FUNCTION

   decode_subscript_data_item -- decode array subscript item

   SYNOPSIS

   static struct type *
   decode_subscript_data_item (char *scan, char *end)

   DESCRIPTION

   The array subscripts and the data type of the elements of an
   array are described by a list of data items, stored as a block
   of contiguous bytes.  There is a data item describing each array
   dimension, and a final data item describing the element type.
   The data items are ordered the same as their appearance in the
   source (I.E. leftmost dimension first, next to leftmost second,
   etc).

   The data items describing each array dimension consist of four
   parts: (1) a format specifier, (2) type type of the subscript
   index, (3) a description of the low bound of the array dimension,
   and (4) a description of the high bound of the array dimension.

   The last data item is the description of the type of each of
   the array elements.

   We are passed a pointer to the start of the block of bytes
   containing the remaining data items, and a pointer to the first
   byte past the data.  This function recursively decodes the
   remaining data items and returns a type.

   If we somehow fail to decode some data, we complain about it
   and return a type "array of int".

   BUGS
   FIXME:  This code only implements the forms currently used
   by the AT&T and GNU C compilers.

   The end pointer is supplied for error checking, maybe we should
   use it for that...
 */

static struct type *
decode_subscript_data_item (char *scan, char *end)
{
  struct type *typep = NULL;	/* Array type we are building */
  struct type *nexttype;	/* Type of each element (may be array) */
  struct type *indextype;	/* Type of this index */
  struct type *rangetype;
  unsigned int format;
  unsigned short fundtype;
  unsigned long lowbound;
  unsigned long highbound;
  int nbytes;

  format = target_to_host (scan, SIZEOF_FORMAT_SPECIFIER, GET_UNSIGNED,
			   current_objfile);
  scan += SIZEOF_FORMAT_SPECIFIER;
  switch (format)
    {
    case FMT_ET:
      typep = decode_array_element_type (scan);
      break;
    case FMT_FT_C_C:
      fundtype = target_to_host (scan, SIZEOF_FMT_FT, GET_UNSIGNED,
				 current_objfile);
      indextype = decode_fund_type (fundtype);
      scan += SIZEOF_FMT_FT;
      nbytes = TARGET_FT_LONG_SIZE (current_objfile);
      lowbound = target_to_host (scan, nbytes, GET_UNSIGNED, current_objfile);
      scan += nbytes;
      highbound = target_to_host (scan, nbytes, GET_UNSIGNED, current_objfile);
      scan += nbytes;
      nexttype = decode_subscript_data_item (scan, end);
      if (nexttype == NULL)
	{
	  /* Munged subscript data or other problem, fake it. */
	  complaint (&symfile_complaints,
		     "DIE @ 0x%x \"%s\", can't decode subscript data items",
		     DIE_ID, DIE_NAME);
	  nexttype = dwarf_fundamental_type (current_objfile, FT_INTEGER);
	}
      rangetype = create_range_type ((struct type *) NULL, indextype,
				     lowbound, highbound);
      typep = create_array_type ((struct type *) NULL, nexttype, rangetype);
      break;
    case FMT_FT_C_X:
    case FMT_FT_X_C:
    case FMT_FT_X_X:
    case FMT_UT_C_C:
    case FMT_UT_C_X:
    case FMT_UT_X_C:
    case FMT_UT_X_X:
      complaint (&symfile_complaints,
		 "DIE @ 0x%x \"%s\", array subscript format 0x%x not handled yet",
		 DIE_ID, DIE_NAME, format);
      nexttype = dwarf_fundamental_type (current_objfile, FT_INTEGER);
      rangetype = create_range_type ((struct type *) NULL, nexttype, 0, 0);
      typep = create_array_type ((struct type *) NULL, nexttype, rangetype);
      break;
    default:
      complaint (&symfile_complaints,
		 "DIE @ 0x%x \"%s\", unknown array subscript format %x", DIE_ID,
		 DIE_NAME, format);
      nexttype = dwarf_fundamental_type (current_objfile, FT_INTEGER);
      rangetype = create_range_type ((struct type *) NULL, nexttype, 0, 0);
      typep = create_array_type ((struct type *) NULL, nexttype, rangetype);
      break;
    }
  return (typep);
}

/*

   LOCAL FUNCTION

   dwarf_read_array_type -- read TAG_array_type DIE

   SYNOPSIS

   static void dwarf_read_array_type (struct dieinfo *dip)

   DESCRIPTION

   Extract all information from a TAG_array_type DIE and add to
   the user defined type vector.
 */

static void
dwarf_read_array_type (struct dieinfo *dip)
{
  struct type *type;
  struct type *utype;
  char *sub;
  char *subend;
  unsigned short blocksz;
  int nbytes;

  if (dip->at_ordering != ORD_row_major)
    {
      /* FIXME:  Can gdb even handle column major arrays? */
      complaint (&symfile_complaints,
		 "DIE @ 0x%x \"%s\", array not row major; not handled correctly",
		 DIE_ID, DIE_NAME);
    }
  sub = dip->at_subscr_data;
  if (sub != NULL)
    {
      nbytes = attribute_size (AT_subscr_data);
      blocksz = target_to_host (sub, nbytes, GET_UNSIGNED, current_objfile);
      subend = sub + nbytes + blocksz;
      sub += nbytes;
      type = decode_subscript_data_item (sub, subend);
      utype = lookup_utype (dip->die_ref);
      if (utype == NULL)
	{
	  /* Install user defined type that has not been referenced yet. */
	  alloc_utype (dip->die_ref, type);
	}
      else if (TYPE_CODE (utype) == TYPE_CODE_UNDEF)
	{
	  /* Ick!  A forward ref has already generated a blank type in our
	     slot, and this type probably already has things pointing to it
	     (which is what caused it to be created in the first place).
	     If it's just a place holder we can plop our fully defined type
	     on top of it.  We can't recover the space allocated for our
	     new type since it might be on an obstack, but we could reuse
	     it if we kept a list of them, but it might not be worth it
	     (FIXME). */
	  *utype = *type;
	}
      else
	{
	  /* Double ick!  Not only is a type already in our slot, but
	     someone has decorated it.  Complain and leave it alone. */
	  dup_user_type_definition_complaint (DIE_ID, DIE_NAME);
	}
    }
}

/*

   LOCAL FUNCTION

   read_tag_pointer_type -- read TAG_pointer_type DIE

   SYNOPSIS

   static void read_tag_pointer_type (struct dieinfo *dip)

   DESCRIPTION

   Extract all information from a TAG_pointer_type DIE and add to
   the user defined type vector.
 */

static void
read_tag_pointer_type (struct dieinfo *dip)
{
  struct type *type;
  struct type *utype;

  type = decode_die_type (dip);
  utype = lookup_utype (dip->die_ref);
  if (utype == NULL)
    {
      utype = lookup_pointer_type (type);
      alloc_utype (dip->die_ref, utype);
    }
  else
    {
      TYPE_TARGET_TYPE (utype) = type;
      TYPE_POINTER_TYPE (type) = utype;

      /* We assume the machine has only one representation for pointers!  */
      /* FIXME:  Possably a poor assumption  */
      TYPE_LENGTH (utype) = TARGET_PTR_BIT / TARGET_CHAR_BIT;
      TYPE_CODE (utype) = TYPE_CODE_PTR;
    }
}

/*

   LOCAL FUNCTION

   read_tag_string_type -- read TAG_string_type DIE

   SYNOPSIS

   static void read_tag_string_type (struct dieinfo *dip)

   DESCRIPTION

   Extract all information from a TAG_string_type DIE and add to
   the user defined type vector.  It isn't really a user defined
   type, but it behaves like one, with other DIE's using an
   AT_user_def_type attribute to reference it.
 */

static void
read_tag_string_type (struct dieinfo *dip)
{
  struct type *utype;
  struct type *indextype;
  struct type *rangetype;
  unsigned long lowbound = 0;
  unsigned long highbound;

  if (dip->has_at_byte_size)
    {
      /* A fixed bounds string */
      highbound = dip->at_byte_size - 1;
    }
  else
    {
      /* A varying length string.  Stub for now.  (FIXME) */
      highbound = 1;
    }
  indextype = dwarf_fundamental_type (current_objfile, FT_INTEGER);
  rangetype = create_range_type ((struct type *) NULL, indextype, lowbound,
				 highbound);

  utype = lookup_utype (dip->die_ref);
  if (utype == NULL)
    {
      /* No type defined, go ahead and create a blank one to use. */
      utype = alloc_utype (dip->die_ref, (struct type *) NULL);
    }
  else
    {
      /* Already a type in our slot due to a forward reference. Make sure it
         is a blank one.  If not, complain and leave it alone. */
      if (TYPE_CODE (utype) != TYPE_CODE_UNDEF)
	{
	  dup_user_type_definition_complaint (DIE_ID, DIE_NAME);
	  return;
	}
    }

  /* Create the string type using the blank type we either found or created. */
  utype = create_string_type (utype, rangetype);
}

/*

   LOCAL FUNCTION

   read_subroutine_type -- process TAG_subroutine_type dies

   SYNOPSIS

   static void read_subroutine_type (struct dieinfo *dip, char thisdie,
   char *enddie)

   DESCRIPTION

   Handle DIES due to C code like:

   struct foo {
   int (*funcp)(int a, long l);  (Generates TAG_subroutine_type DIE)
   int b;
   };

   NOTES

   The parameter DIES are currently ignored.  See if gdb has a way to
   include this info in it's type system, and decode them if so.  Is
   this what the type structure's "arg_types" field is for?  (FIXME)
 */

static void
read_subroutine_type (struct dieinfo *dip, char *thisdie, char *enddie)
{
  struct type *type;		/* Type that this function returns */
  struct type *ftype;		/* Function that returns above type */

  /* Decode the type that this subroutine returns */

  type = decode_die_type (dip);

  /* Check to see if we already have a partially constructed user
     defined type for this DIE, from a forward reference. */

  ftype = lookup_utype (dip->die_ref);
  if (ftype == NULL)
    {
      /* This is the first reference to one of these types.  Make
         a new one and place it in the user defined types. */
      ftype = lookup_function_type (type);
      alloc_utype (dip->die_ref, ftype);
    }
  else if (TYPE_CODE (ftype) == TYPE_CODE_UNDEF)
    {
      /* We have an existing partially constructed type, so bash it
         into the correct type. */
      TYPE_TARGET_TYPE (ftype) = type;
      TYPE_LENGTH (ftype) = 1;
      TYPE_CODE (ftype) = TYPE_CODE_FUNC;
    }
  else
    {
      dup_user_type_definition_complaint (DIE_ID, DIE_NAME);
    }
}

/*

   LOCAL FUNCTION

   read_enumeration -- process dies which define an enumeration

   SYNOPSIS

   static void read_enumeration (struct dieinfo *dip, char *thisdie,
   char *enddie, struct objfile *objfile)

   DESCRIPTION

   Given a pointer to a die which begins an enumeration, process all
   the dies that define the members of the enumeration.

   NOTES

   Note that we need to call enum_type regardless of whether or not we
   have a symbol, since we might have an enum without a tag name (thus
   no symbol for the tagname).
 */

static void
read_enumeration (struct dieinfo *dip, char *thisdie, char *enddie,
		  struct objfile *objfile)
{
  struct type *type;
  struct symbol *sym;

  type = enum_type (dip, objfile);
  sym = new_symbol (dip, objfile);
  if (sym != NULL)
    {
      SYMBOL_TYPE (sym) = type;
      if (cu_language == language_cplus)
	{
	  synthesize_typedef (dip, objfile, type);
	}
    }
}

/*

   LOCAL FUNCTION

   enum_type -- decode and return a type for an enumeration

   SYNOPSIS

   static type *enum_type (struct dieinfo *dip, struct objfile *objfile)

   DESCRIPTION

   Given a pointer to a die information structure for the die which
   starts an enumeration, process all the dies that define the members
   of the enumeration and return a type pointer for the enumeration.

   At the same time, for each member of the enumeration, create a
   symbol for it with domain VAR_DOMAIN and class LOC_CONST,
   and give it the type of the enumeration itself.

   NOTES

   Note that the DWARF specification explicitly mandates that enum
   constants occur in reverse order from the source program order,
   for "consistency" and because this ordering is easier for many
   compilers to generate. (Draft 6, sec 3.8.5, Enumeration type
   Entries).  Because gdb wants to see the enum members in program
   source order, we have to ensure that the order gets reversed while
   we are processing them.
 */

static struct type *
enum_type (struct dieinfo *dip, struct objfile *objfile)
{
  struct type *type;
  struct nextfield
    {
      struct nextfield *next;
      struct field field;
    };
  struct nextfield *list = NULL;
  struct nextfield *new;
  int nfields = 0;
  int n;
  char *scan;
  char *listend;
  unsigned short blocksz;
  struct symbol *sym;
  int nbytes;
  int unsigned_enum = 1;

  type = lookup_utype (dip->die_ref);
  if (type == NULL)
    {
      /* No forward references created an empty type, so install one now */
      type = alloc_utype (dip->die_ref, NULL);
    }
  TYPE_CODE (type) = TYPE_CODE_ENUM;
  /* Some compilers try to be helpful by inventing "fake" names for
     anonymous enums, structures, and unions, like "~0fake" or ".0fake".
     Thanks, but no thanks... */
  if (dip->at_name != NULL
      && *dip->at_name != '~'
      && *dip->at_name != '.')
    {
      TYPE_TAG_NAME (type) = obconcat (&objfile->objfile_obstack,
				       "", "", dip->at_name);
    }
  if (dip->at_byte_size != 0)
    {
      TYPE_LENGTH (type) = dip->at_byte_size;
    }
  scan = dip->at_element_list;
  if (scan != NULL)
    {
      if (dip->short_element_list)
	{
	  nbytes = attribute_size (AT_short_element_list);
	}
      else
	{
	  nbytes = attribute_size (AT_element_list);
	}
      blocksz = target_to_host (scan, nbytes, GET_UNSIGNED, objfile);
      listend = scan + nbytes + blocksz;
      scan += nbytes;
      while (scan < listend)
	{
	  new = (struct nextfield *) alloca (sizeof (struct nextfield));
	  new->next = list;
	  list = new;
	  FIELD_TYPE (list->field) = NULL;
	  FIELD_BITSIZE (list->field) = 0;
	  FIELD_STATIC_KIND (list->field) = 0;
	  FIELD_BITPOS (list->field) =
	    target_to_host (scan, TARGET_FT_LONG_SIZE (objfile), GET_SIGNED,
			    objfile);
	  scan += TARGET_FT_LONG_SIZE (objfile);
	  list->field.name = obsavestring (scan, strlen (scan),
					   &objfile->objfile_obstack);
	  scan += strlen (scan) + 1;
	  nfields++;
	  /* Handcraft a new symbol for this enum member. */
	  sym = (struct symbol *) obstack_alloc (&objfile->objfile_obstack,
						 sizeof (struct symbol));
	  memset (sym, 0, sizeof (struct symbol));
	  DEPRECATED_SYMBOL_NAME (sym) = create_name (list->field.name,
					   &objfile->objfile_obstack);
	  SYMBOL_INIT_LANGUAGE_SPECIFIC (sym, cu_language);
	  SYMBOL_DOMAIN (sym) = VAR_DOMAIN;
	  SYMBOL_CLASS (sym) = LOC_CONST;
	  SYMBOL_TYPE (sym) = type;
	  SYMBOL_VALUE (sym) = FIELD_BITPOS (list->field);
	  if (SYMBOL_VALUE (sym) < 0)
	    unsigned_enum = 0;
	  add_symbol_to_list (sym, list_in_scope);
	}
      /* Now create the vector of fields, and record how big it is. This is
         where we reverse the order, by pulling the members off the list in
         reverse order from how they were inserted.  If we have no fields
         (this is apparently possible in C++) then skip building a field
         vector. */
      if (nfields > 0)
	{
	  if (unsigned_enum)
	    TYPE_FLAGS (type) |= TYPE_FLAG_UNSIGNED;
	  TYPE_NFIELDS (type) = nfields;
	  TYPE_FIELDS (type) = (struct field *)
	    obstack_alloc (&objfile->objfile_obstack, sizeof (struct field) * nfields);
	  /* Copy the saved-up fields into the field vector.  */
	  for (n = 0; (n < nfields) && (list != NULL); list = list->next)
	    {
	      TYPE_FIELD (type, n++) = list->field;
	    }
	}
    }
  return (type);
}

/*

   LOCAL FUNCTION

   read_func_scope -- process all dies within a function scope

   DESCRIPTION

   Process all dies within a given function scope.  We are passed
   a die information structure pointer DIP for the die which
   starts the function scope, and pointers into the raw die data
   that define the dies within the function scope.

   For now, we ignore lexical block scopes within the function.
   The problem is that AT&T cc does not define a DWARF lexical
   block scope for the function itself, while gcc defines a
   lexical block scope for the function.  We need to think about
   how to handle this difference, or if it is even a problem.
   (FIXME)
 */

static void
read_func_scope (struct dieinfo *dip, char *thisdie, char *enddie,
		 struct objfile *objfile)
{
  struct context_stack *new;

  /* AT_name is absent if the function is described with an
     AT_abstract_origin tag.
     Ignore the function description for now to avoid GDB core dumps.
     FIXME: Add code to handle AT_abstract_origin tags properly.  */
  if (dip->at_name == NULL)
    {
      complaint (&symfile_complaints, "DIE @ 0x%x, AT_name tag missing",
		 DIE_ID);
      return;
    }

  if (objfile->ei.entry_point >= dip->at_low_pc &&
      objfile->ei.entry_point < dip->at_high_pc)
    {
      objfile->ei.entry_func_lowpc = dip->at_low_pc;
      objfile->ei.entry_func_highpc = dip->at_high_pc;
    }
  new = push_context (0, dip->at_low_pc);
  new->name = new_symbol (dip, objfile);
  list_in_scope = &local_symbols;
  process_dies (thisdie + dip->die_length, enddie, objfile);
  new = pop_context ();
  /* Make a block for the local symbols within.  */
  finish_block (new->name, &local_symbols, new->old_blocks,
		new->start_addr, dip->at_high_pc, objfile);
  list_in_scope = &file_symbols;
}


/*

   LOCAL FUNCTION

   handle_producer -- process the AT_producer attribute

   DESCRIPTION

   Perform any operations that depend on finding a particular
   AT_producer attribute.

 */

static void
handle_producer (char *producer)
{

  /* If this compilation unit was compiled with g++ or gcc, then set the
     processing_gcc_compilation flag. */

  if (DEPRECATED_STREQN (producer, GCC_PRODUCER, strlen (GCC_PRODUCER)))
    {
      char version = producer[strlen (GCC_PRODUCER)];
      processing_gcc_compilation = (version == '2' ? 2 : 1);
    }
  else
    {
      processing_gcc_compilation =
	strncmp (producer, GPLUS_PRODUCER, strlen (GPLUS_PRODUCER)) == 0;
    }

  /* Select a demangling style if we can identify the producer and if
     the current style is auto.  We leave the current style alone if it
     is not auto.  We also leave the demangling style alone if we find a
     gcc (cc1) producer, as opposed to a g++ (cc1plus) producer. */

  if (AUTO_DEMANGLING)
    {
      if (DEPRECATED_STREQN (producer, GPLUS_PRODUCER, strlen (GPLUS_PRODUCER)))
	{
#if 0
	  /* For now, stay with AUTO_DEMANGLING for g++ output, as we don't
	     know whether it will use the old style or v3 mangling.  */
	  set_demangling_style (GNU_DEMANGLING_STYLE_STRING);
#endif
	}
      else if (DEPRECATED_STREQN (producer, LCC_PRODUCER, strlen (LCC_PRODUCER)))
	{
	  set_demangling_style (LUCID_DEMANGLING_STYLE_STRING);
	}
    }
}


/*

   LOCAL FUNCTION

   read_file_scope -- process all dies within a file scope

   DESCRIPTION

   Process all dies within a given file scope.  We are passed a
   pointer to the die information structure for the die which
   starts the file scope, and pointers into the raw die data which
   mark the range of dies within the file scope.

   When the partial symbol table is built, the file offset for the line
   number table for each compilation unit is saved in the partial symbol
   table entry for that compilation unit.  As the symbols for each
   compilation unit are read, the line number table is read into memory
   and the variable lnbase is set to point to it.  Thus all we have to
   do is use lnbase to access the line number table for the current
   compilation unit.
 */

static void
read_file_scope (struct dieinfo *dip, char *thisdie, char *enddie,
		 struct objfile *objfile)
{
  struct cleanup *back_to;
  struct symtab *symtab;

  if (objfile->ei.entry_point >= dip->at_low_pc &&
      objfile->ei.entry_point < dip->at_high_pc)
    {
      objfile->ei.deprecated_entry_file_lowpc = dip->at_low_pc;
      objfile->ei.deprecated_entry_file_highpc = dip->at_high_pc;
    }
  set_cu_language (dip);
  if (dip->at_producer != NULL)
    {
      handle_producer (dip->at_producer);
    }
  numutypes = (enddie - thisdie) / 4;
  utypes = (struct type **) xmalloc (numutypes * sizeof (struct type *));
  back_to = make_cleanup (free_utypes, NULL);
  memset (utypes, 0, numutypes * sizeof (struct type *));
  memset (ftypes, 0, FT_NUM_MEMBERS * sizeof (struct type *));
  start_symtab (dip->at_name, dip->at_comp_dir, dip->at_low_pc);
  record_debugformat ("DWARF 1");
  decode_line_numbers (lnbase);
  process_dies (thisdie + dip->die_length, enddie, objfile);

  symtab = end_symtab (dip->at_high_pc, objfile, 0);
  if (symtab != NULL)
    {
      symtab->language = cu_language;
    }
  do_cleanups (back_to);
}

/*

   LOCAL FUNCTION

   process_dies -- process a range of DWARF Information Entries

   SYNOPSIS

   static void process_dies (char *thisdie, char *enddie,
   struct objfile *objfile)

   DESCRIPTION

   Process all DIE's in a specified range.  May be (and almost
   certainly will be) called recursively.
 */

static void
process_dies (char *thisdie, char *enddie, struct objfile *objfile)
{
  char *nextdie;
  struct dieinfo di;

  while (thisdie < enddie)
    {
      basicdieinfo (&di, thisdie, objfile);
      if (di.die_length < SIZEOF_DIE_LENGTH)
	{
	  break;
	}
      else if (di.die_tag == TAG_padding)
	{
	  nextdie = thisdie + di.die_length;
	}
      else
	{
	  completedieinfo (&di, objfile);
	  if (di.at_sibling != 0)
	    {
	      nextdie = dbbase + di.at_sibling - dbroff;
	    }
	  else
	    {
	      nextdie = thisdie + di.die_length;
	    }
	  /* I think that these are always text, not data, addresses.  */
	  di.at_low_pc = SMASH_TEXT_ADDRESS (di.at_low_pc);
	  di.at_high_pc = SMASH_TEXT_ADDRESS (di.at_high_pc);
	  switch (di.die_tag)
	    {
	    case TAG_compile_unit:
	      /* Skip Tag_compile_unit if we are already inside a compilation
	         unit, we are unable to handle nested compilation units
	         properly (FIXME).  */
	      if (current_subfile == NULL)
		read_file_scope (&di, thisdie, nextdie, objfile);
	      else
		nextdie = thisdie + di.die_length;
	      break;
	    case TAG_global_subroutine:
	    case TAG_subroutine:
	      if (di.has_at_low_pc)
		{
		  read_func_scope (&di, thisdie, nextdie, objfile);
		}
	      break;
	    case TAG_lexical_block:
	      read_lexical_block_scope (&di, thisdie, nextdie, objfile);
	      break;
	    case TAG_class_type:
	    case TAG_structure_type:
	    case TAG_union_type:
	      read_structure_scope (&di, thisdie, nextdie, objfile);
	      break;
	    case TAG_enumeration_type:
	      read_enumeration (&di, thisdie, nextdie, objfile);
	      break;
	    case TAG_subroutine_type:
	      read_subroutine_type (&di, thisdie, nextdie);
	      break;
	    case TAG_array_type:
	      dwarf_read_array_type (&di);
	      break;
	    case TAG_pointer_type:
	      read_tag_pointer_type (&di);
	      break;
	    case TAG_string_type:
	      read_tag_string_type (&di);
	      break;
	    default:
	      new_symbol (&di, objfile);
	      break;
	    }
	}
      thisdie = nextdie;
    }
}

/*

   LOCAL FUNCTION

   decode_line_numbers -- decode a line number table fragment

   SYNOPSIS

   static void decode_line_numbers (char *tblscan, char *tblend,
   long length, long base, long line, long pc)

   DESCRIPTION

   Translate the DWARF line number information to gdb form.

   The ".line" section contains one or more line number tables, one for
   each ".line" section from the objects that were linked.

   The AT_stmt_list attribute for each TAG_source_file entry in the
   ".debug" section contains the offset into the ".line" section for the
   start of the table for that file.

   The table itself has the following structure:

   <table length><base address><source statement entry>
   4 bytes       4 bytes       10 bytes

   The table length is the total size of the table, including the 4 bytes
   for the length information.

   The base address is the address of the first instruction generated
   for the source file.

   Each source statement entry has the following structure:

   <line number><statement position><address delta>
   4 bytes      2 bytes             4 bytes

   The line number is relative to the start of the file, starting with
   line 1.

   The statement position either -1 (0xFFFF) or the number of characters
   from the beginning of the line to the beginning of the statement.

   The address delta is the difference between the base address and
   the address of the first instruction for the statement.

   Note that we must copy the bytes from the packed table to our local
   variables before attempting to use them, to avoid alignment problems
   on some machines, particularly RISC processors.

   BUGS

   Does gdb expect the line numbers to be sorted?  They are now by
   chance/luck, but are not required to be.  (FIXME)

   The line with number 0 is unused, gdb apparently can discover the
   span of the last line some other way. How?  (FIXME)
 */

static void
decode_line_numbers (char *linetable)
{
  char *tblscan;
  char *tblend;
  unsigned long length;
  unsigned long base;
  unsigned long line;
  unsigned long pc;

  if (linetable != NULL)
    {
      tblscan = tblend = linetable;
      length = target_to_host (tblscan, SIZEOF_LINETBL_LENGTH, GET_UNSIGNED,
			       current_objfile);
      tblscan += SIZEOF_LINETBL_LENGTH;
      tblend += length;
      base = target_to_host (tblscan, TARGET_FT_POINTER_SIZE (objfile),
			     GET_UNSIGNED, current_objfile);
      tblscan += TARGET_FT_POINTER_SIZE (objfile);
      base += baseaddr;
      while (tblscan < tblend)
	{
	  line = target_to_host (tblscan, SIZEOF_LINETBL_LINENO, GET_UNSIGNED,
				 current_objfile);
	  tblscan += SIZEOF_LINETBL_LINENO + SIZEOF_LINETBL_STMT;
	  pc = target_to_host (tblscan, SIZEOF_LINETBL_DELTA, GET_UNSIGNED,
			       current_objfile);
	  tblscan += SIZEOF_LINETBL_DELTA;
	  pc += base;
	  if (line != 0)
	    {
	      record_line (current_subfile, line, pc);
	    }
	}
    }
}

/*

   LOCAL FUNCTION

   locval -- compute the value of a location attribute

   SYNOPSIS

   static int locval (struct dieinfo *dip)

   DESCRIPTION

   Given pointer to a string of bytes that define a location, compute
   the location and return the value.
   A location description containing no atoms indicates that the
   object is optimized out. The optimized_out flag is set for those,
   the return value is meaningless.

   When computing values involving the current value of the frame pointer,
   the value zero is used, which results in a value relative to the frame
   pointer, rather than the absolute value.  This is what GDB wants
   anyway.

   When the result is a register number, the isreg flag is set, otherwise
   it is cleared.  This is a kludge until we figure out a better
   way to handle the problem.  Gdb's design does not mesh well with the
   DWARF notion of a location computing interpreter, which is a shame
   because the flexibility goes unused.

   NOTES

   Note that stack[0] is unused except as a default error return.
   Note that stack overflow is not yet handled.
 */

static int
locval (struct dieinfo *dip)
{
  unsigned short nbytes;
  unsigned short locsize;
  auto long stack[64];
  int stacki;
  char *loc;
  char *end;
  int loc_atom_code;
  int loc_value_size;

  loc = dip->at_location;
  nbytes = attribute_size (AT_location);
  locsize = target_to_host (loc, nbytes, GET_UNSIGNED, current_objfile);
  loc += nbytes;
  end = loc + locsize;
  stacki = 0;
  stack[stacki] = 0;
  dip->isreg = 0;
  dip->offreg = 0;
  dip->optimized_out = 1;
  loc_value_size = TARGET_FT_LONG_SIZE (current_objfile);
  while (loc < end)
    {
      dip->optimized_out = 0;
      loc_atom_code = target_to_host (loc, SIZEOF_LOC_ATOM_CODE, GET_UNSIGNED,
				      current_objfile);
      loc += SIZEOF_LOC_ATOM_CODE;
      switch (loc_atom_code)
	{
	case 0:
	  /* error */
	  loc = end;
	  break;
	case OP_REG:
	  /* push register (number) */
	  stack[++stacki]
	    = DWARF_REG_TO_REGNUM (target_to_host (loc, loc_value_size,
						   GET_UNSIGNED,
						   current_objfile));
	  loc += loc_value_size;
	  dip->isreg = 1;
	  break;
	case OP_BASEREG:
	  /* push value of register (number) */
	  /* Actually, we compute the value as if register has 0, so the
	     value ends up being the offset from that register.  */
	  dip->offreg = 1;
	  dip->basereg = target_to_host (loc, loc_value_size, GET_UNSIGNED,
					 current_objfile);
	  loc += loc_value_size;
	  stack[++stacki] = 0;
	  break;
	case OP_ADDR:
	  /* push address (relocated address) */
	  stack[++stacki] = target_to_host (loc, loc_value_size,
					    GET_UNSIGNED, current_objfile);
	  loc += loc_value_size;
	  break;
	case OP_CONST:
	  /* push constant (number)   FIXME: signed or unsigned! */
	  stack[++stacki] = target_to_host (loc, loc_value_size,
					    GET_SIGNED, current_objfile);
	  loc += loc_value_size;
	  break;
	case OP_DEREF2:
	  /* pop, deref and push 2 bytes (as a long) */
	  complaint (&symfile_complaints,
		     "DIE @ 0x%x \"%s\", OP_DEREF2 address 0x%lx not handled",
		     DIE_ID, DIE_NAME, stack[stacki]);
	  break;
	case OP_DEREF4:	/* pop, deref and push 4 bytes (as a long) */
	  complaint (&symfile_complaints,
		     "DIE @ 0x%x \"%s\", OP_DEREF4 address 0x%lx not handled",
		     DIE_ID, DIE_NAME, stack[stacki]);
	  break;
	case OP_ADD:		/* pop top 2 items, add, push result */
	  stack[stacki - 1] += stack[stacki];
	  stacki--;
	  break;
	}
    }
  return (stack[stacki]);
}

/*

   LOCAL FUNCTION

   read_ofile_symtab -- build a full symtab entry from chunk of DIE's

   SYNOPSIS

   static void read_ofile_symtab (struct partial_symtab *pst)

   DESCRIPTION

   When expanding a partial symbol table entry to a full symbol table
   entry, this is the function that gets called to read in the symbols
   for the compilation unit.  A pointer to the newly constructed symtab,
   which is now the new first one on the objfile's symtab list, is
   stashed in the partial symbol table entry.
 */

static void
read_ofile_symtab (struct partial_symtab *pst)
{
  struct cleanup *back_to;
  unsigned long lnsize;
  file_ptr foffset;
  bfd *abfd;
  char lnsizedata[SIZEOF_LINETBL_LENGTH];

  abfd = pst->objfile->obfd;
  current_objfile = pst->objfile;

  /* Allocate a buffer for the entire chunk of DIE's for this compilation
     unit, seek to the location in the file, and read in all the DIE's. */

  diecount = 0;
  dbsize = DBLENGTH (pst);
  dbbase = xmalloc (dbsize);
  dbroff = DBROFF (pst);
  foffset = DBFOFF (pst) + dbroff;
  base_section_offsets = pst->section_offsets;
  baseaddr = ANOFFSET (pst->section_offsets, 0);
  if (bfd_seek (abfd, foffset, SEEK_SET) ||
      (bfd_bread (dbbase, dbsize, abfd) != dbsize))
    {
      xfree (dbbase);
      error ("can't read DWARF data");
    }
  back_to = make_cleanup (xfree, dbbase);

  /* If there is a line number table associated with this compilation unit
     then read the size of this fragment in bytes, from the fragment itself.
     Allocate a buffer for the fragment and read it in for future 
     processing. */

  lnbase = NULL;
  if (LNFOFF (pst))
    {
      if (bfd_seek (abfd, LNFOFF (pst), SEEK_SET) ||
	  (bfd_bread (lnsizedata, sizeof (lnsizedata), abfd)
	   != sizeof (lnsizedata)))
	{
	  error ("can't read DWARF line number table size");
	}
      lnsize = target_to_host (lnsizedata, SIZEOF_LINETBL_LENGTH,
			       GET_UNSIGNED, pst->objfile);
      lnbase = xmalloc (lnsize);
      if (bfd_seek (abfd, LNFOFF (pst), SEEK_SET) ||
	  (bfd_bread (lnbase, lnsize, abfd) != lnsize))
	{
	  xfree (lnbase);
	  error ("can't read DWARF line numbers");
	}
      make_cleanup (xfree, lnbase);
    }

  process_dies (dbbase, dbbase + dbsize, pst->objfile);
  do_cleanups (back_to);
  current_objfile = NULL;
  pst->symtab = pst->objfile->symtabs;
}

/*

   LOCAL FUNCTION

   psymtab_to_symtab_1 -- do grunt work for building a full symtab entry

   SYNOPSIS

   static void psymtab_to_symtab_1 (struct partial_symtab *pst)

   DESCRIPTION

   Called once for each partial symbol table entry that needs to be
   expanded into a full symbol table entry.

 */

static void
psymtab_to_symtab_1 (struct partial_symtab *pst)
{
  int i;
  struct cleanup *old_chain;

  if (pst != NULL)
    {
      if (pst->readin)
	{
	  warning ("psymtab for %s already read in.  Shouldn't happen.",
		   pst->filename);
	}
      else
	{
	  /* Read in all partial symtabs on which this one is dependent */
	  for (i = 0; i < pst->number_of_dependencies; i++)
	    {
	      if (!pst->dependencies[i]->readin)
		{
		  /* Inform about additional files that need to be read in. */
		  if (info_verbose)
		    {
		      fputs_filtered (" ", gdb_stdout);
		      wrap_here ("");
		      fputs_filtered ("and ", gdb_stdout);
		      wrap_here ("");
		      printf_filtered ("%s...",
				       pst->dependencies[i]->filename);
		      wrap_here ("");
		      gdb_flush (gdb_stdout);	/* Flush output */
		    }
		  psymtab_to_symtab_1 (pst->dependencies[i]);
		}
	    }
	  if (DBLENGTH (pst))	/* Otherwise it's a dummy */
	    {
	      buildsym_init ();
	      old_chain = make_cleanup (really_free_pendings, 0);
	      read_ofile_symtab (pst);
	      if (info_verbose)
		{
		  printf_filtered ("%d DIE's, sorting...", diecount);
		  wrap_here ("");
		  gdb_flush (gdb_stdout);
		}
	      do_cleanups (old_chain);
	    }
	  pst->readin = 1;
	}
    }
}

/*

   LOCAL FUNCTION

   dwarf_psymtab_to_symtab -- build a full symtab entry from partial one

   SYNOPSIS

   static void dwarf_psymtab_to_symtab (struct partial_symtab *pst)

   DESCRIPTION

   This is the DWARF support entry point for building a full symbol
   table entry from a partial symbol table entry.  We are passed a
   pointer to the partial symbol table entry that needs to be expanded.

 */

static void
dwarf_psymtab_to_symtab (struct partial_symtab *pst)
{

  if (pst != NULL)
    {
      if (pst->readin)
	{
	  warning ("psymtab for %s already read in.  Shouldn't happen.",
		   pst->filename);
	}
      else
	{
	  if (DBLENGTH (pst) || pst->number_of_dependencies)
	    {
	      /* Print the message now, before starting serious work, to avoid
	         disconcerting pauses.  */
	      if (info_verbose)
		{
		  printf_filtered ("Reading in symbols for %s...",
				   pst->filename);
		  gdb_flush (gdb_stdout);
		}

	      psymtab_to_symtab_1 (pst);

#if 0				/* FIXME:  Check to see what dbxread is doing here and see if
				   we need to do an equivalent or is this something peculiar to
				   stabs/a.out format.
				   Match with global symbols.  This only needs to be done once,
				   after all of the symtabs and dependencies have been read in.
				 */
	      scan_file_globals (pst->objfile);
#endif

	      /* Finish up the verbose info message.  */
	      if (info_verbose)
		{
		  printf_filtered ("done.\n");
		  gdb_flush (gdb_stdout);
		}
	    }
	}
    }
}

/*

   LOCAL FUNCTION

   add_enum_psymbol -- add enumeration members to partial symbol table

   DESCRIPTION

   Given pointer to a DIE that is known to be for an enumeration,
   extract the symbolic names of the enumeration members and add
   partial symbols for them.
 */

static void
add_enum_psymbol (struct dieinfo *dip, struct objfile *objfile)
{
  char *scan;
  char *listend;
  unsigned short blocksz;
  int nbytes;

  scan = dip->at_element_list;
  if (scan != NULL)
    {
      if (dip->short_element_list)
	{
	  nbytes = attribute_size (AT_short_element_list);
	}
      else
	{
	  nbytes = attribute_size (AT_element_list);
	}
      blocksz = target_to_host (scan, nbytes, GET_UNSIGNED, objfile);
      scan += nbytes;
      listend = scan + blocksz;
      while (scan < listend)
	{
	  scan += TARGET_FT_LONG_SIZE (objfile);
	  add_psymbol_to_list (scan, strlen (scan), VAR_DOMAIN, LOC_CONST,
			       &objfile->static_psymbols, 0, 0, cu_language,
			       objfile);
	  scan += strlen (scan) + 1;
	}
    }
}

/*

   LOCAL FUNCTION

   add_partial_symbol -- add symbol to partial symbol table

   DESCRIPTION

   Given a DIE, if it is one of the types that we want to
   add to a partial symbol table, finish filling in the die info
   and then add a partial symbol table entry for it.

   NOTES

   The caller must ensure that the DIE has a valid name attribute.
 */

static void
add_partial_symbol (struct dieinfo *dip, struct objfile *objfile)
{
  switch (dip->die_tag)
    {
    case TAG_global_subroutine:
      add_psymbol_to_list (dip->at_name, strlen (dip->at_name),
			   VAR_DOMAIN, LOC_BLOCK,
			   &objfile->global_psymbols,
			   0, dip->at_low_pc, cu_language, objfile);
      break;
    case TAG_global_variable:
      add_psymbol_to_list (dip->at_name, strlen (dip->at_name),
			   VAR_DOMAIN, LOC_STATIC,
			   &objfile->global_psymbols,
			   0, 0, cu_language, objfile);
      break;
    case TAG_subroutine:
      add_psymbol_to_list (dip->at_name, strlen (dip->at_name),
			   VAR_DOMAIN, LOC_BLOCK,
			   &objfile->static_psymbols,
			   0, dip->at_low_pc, cu_language, objfile);
      break;
    case TAG_local_variable:
      add_psymbol_to_list (dip->at_name, strlen (dip->at_name),
			   VAR_DOMAIN, LOC_STATIC,
			   &objfile->static_psymbols,
			   0, 0, cu_language, objfile);
      break;
    case TAG_typedef:
      add_psymbol_to_list (dip->at_name, strlen (dip->at_name),
			   VAR_DOMAIN, LOC_TYPEDEF,
			   &objfile->static_psymbols,
			   0, 0, cu_language, objfile);
      break;
    case TAG_class_type:
    case TAG_structure_type:
    case TAG_union_type:
    case TAG_enumeration_type:
      /* Do not add opaque aggregate definitions to the psymtab.  */
      if (!dip->has_at_byte_size)
	break;
      add_psymbol_to_list (dip->at_name, strlen (dip->at_name),
			   STRUCT_DOMAIN, LOC_TYPEDEF,
			   &objfile->static_psymbols,
			   0, 0, cu_language, objfile);
      if (cu_language == language_cplus)
	{
	  /* For C++, these implicitly act as typedefs as well. */
	  add_psymbol_to_list (dip->at_name, strlen (dip->at_name),
			       VAR_DOMAIN, LOC_TYPEDEF,
			       &objfile->static_psymbols,
			       0, 0, cu_language, objfile);
	}
      break;
    }
}
/* *INDENT-OFF* */
/*

LOCAL FUNCTION

	scan_partial_symbols -- scan DIE's within a single compilation unit

DESCRIPTION

	Process the DIE's within a single compilation unit, looking for
	interesting DIE's that contribute to the partial symbol table entry
	for this compilation unit.

NOTES

	There are some DIE's that may appear both at file scope and within
	the scope of a function.  We are only interested in the ones at file
	scope, and the only way to tell them apart is to keep track of the
	scope.  For example, consider the test case:

		static int i;
		main () { int j; }

	for which the relevant DWARF segment has the structure:
	
		0x51:
		0x23   global subrtn   sibling     0x9b
		                       name        main
		                       fund_type   FT_integer
		                       low_pc      0x800004cc
		                       high_pc     0x800004d4
		                            
		0x74:
		0x23   local var       sibling     0x97
		                       name        j
		                       fund_type   FT_integer
		                       location    OP_BASEREG 0xe
		                                   OP_CONST 0xfffffffc
		                                   OP_ADD
		0x97:
		0x4         
		
		0x9b:
		0x1d   local var       sibling     0xb8
		                       name        i
		                       fund_type   FT_integer
		                       location    OP_ADDR 0x800025dc
		                            
		0xb8:
		0x4         

	We want to include the symbol 'i' in the partial symbol table, but
	not the symbol 'j'.  In essence, we want to skip all the dies within
	the scope of a TAG_global_subroutine DIE.

	Don't attempt to add anonymous structures or unions since they have
	no name.  Anonymous enumerations however are processed, because we
	want to extract their member names (the check for a tag name is
	done later).

	Also, for variables and subroutines, check that this is the place
	where the actual definition occurs, rather than just a reference
	to an external.
 */
/* *INDENT-ON* */



static void
scan_partial_symbols (char *thisdie, char *enddie, struct objfile *objfile)
{
  char *nextdie;
  char *temp;
  struct dieinfo di;

  while (thisdie < enddie)
    {
      basicdieinfo (&di, thisdie, objfile);
      if (di.die_length < SIZEOF_DIE_LENGTH)
	{
	  break;
	}
      else
	{
	  nextdie = thisdie + di.die_length;
	  /* To avoid getting complete die information for every die, we
	     only do it (below) for the cases we are interested in. */
	  switch (di.die_tag)
	    {
	    case TAG_global_subroutine:
	    case TAG_subroutine:
	      completedieinfo (&di, objfile);
	      if (di.at_name && (di.has_at_low_pc || di.at_location))
		{
		  add_partial_symbol (&di, objfile);
		  /* If there is a sibling attribute, adjust the nextdie
		     pointer to skip the entire scope of the subroutine.
		     Apply some sanity checking to make sure we don't 
		     overrun or underrun the range of remaining DIE's */
		  if (di.at_sibling != 0)
		    {
		      temp = dbbase + di.at_sibling - dbroff;
		      if ((temp < thisdie) || (temp >= enddie))
			{
			  bad_die_ref_complaint (DIE_ID, DIE_NAME,
						 di.at_sibling);
			}
		      else
			{
			  nextdie = temp;
			}
		    }
		}
	      break;
	    case TAG_global_variable:
	    case TAG_local_variable:
	      completedieinfo (&di, objfile);
	      if (di.at_name && (di.has_at_low_pc || di.at_location))
		{
		  add_partial_symbol (&di, objfile);
		}
	      break;
	    case TAG_typedef:
	    case TAG_class_type:
	    case TAG_structure_type:
	    case TAG_union_type:
	      completedieinfo (&di, objfile);
	      if (di.at_name)
		{
		  add_partial_symbol (&di, objfile);
		}
	      break;
	    case TAG_enumeration_type:
	      completedieinfo (&di, objfile);
	      if (di.at_name)
		{
		  add_partial_symbol (&di, objfile);
		}
	      add_enum_psymbol (&di, objfile);
	      break;
	    }
	}
      thisdie = nextdie;
    }
}

/*

   LOCAL FUNCTION

   scan_compilation_units -- build a psymtab entry for each compilation

   DESCRIPTION

   This is the top level dwarf parsing routine for building partial
   symbol tables.

   It scans from the beginning of the DWARF table looking for the first
   TAG_compile_unit DIE, and then follows the sibling chain to locate
   each additional TAG_compile_unit DIE.

   For each TAG_compile_unit DIE it creates a partial symtab structure,
   calls a subordinate routine to collect all the compilation unit's
   global DIE's, file scope DIEs, typedef DIEs, etc, and then links the
   new partial symtab structure into the partial symbol table.  It also
   records the appropriate information in the partial symbol table entry
   to allow the chunk of DIE's and line number table for this compilation
   unit to be located and re-read later, to generate a complete symbol
   table entry for the compilation unit.

   Thus it effectively partitions up a chunk of DIE's for multiple
   compilation units into smaller DIE chunks and line number tables,
   and associates them with a partial symbol table entry.

   NOTES

   If any compilation unit has no line number table associated with
   it for some reason (a missing at_stmt_list attribute, rather than
   just one with a value of zero, which is valid) then we ensure that
   the recorded file offset is zero so that the routine which later
   reads line number table fragments knows that there is no fragment
   to read.

   RETURNS

   Returns no value.

 */

static void
scan_compilation_units (char *thisdie, char *enddie, file_ptr dbfoff,
			file_ptr lnoffset, struct objfile *objfile)
{
  char *nextdie;
  struct dieinfo di;
  struct partial_symtab *pst;
  int culength;
  int curoff;
  file_ptr curlnoffset;

  while (thisdie < enddie)
    {
      basicdieinfo (&di, thisdie, objfile);
      if (di.die_length < SIZEOF_DIE_LENGTH)
	{
	  break;
	}
      else if (di.die_tag != TAG_compile_unit)
	{
	  nextdie = thisdie + di.die_length;
	}
      else
	{
	  completedieinfo (&di, objfile);
	  set_cu_language (&di);
	  if (di.at_sibling != 0)
	    {
	      nextdie = dbbase + di.at_sibling - dbroff;
	    }
	  else
	    {
	      nextdie = thisdie + di.die_length;
	    }
	  curoff = thisdie - dbbase;
	  culength = nextdie - thisdie;
	  curlnoffset = di.has_at_stmt_list ? lnoffset + di.at_stmt_list : 0;

	  /* First allocate a new partial symbol table structure */

	  pst = start_psymtab_common (objfile, base_section_offsets,
				      di.at_name, di.at_low_pc,
				      objfile->global_psymbols.next,
				      objfile->static_psymbols.next);

	  pst->texthigh = di.at_high_pc;
	  pst->read_symtab_private = (char *)
	    obstack_alloc (&objfile->objfile_obstack,
			   sizeof (struct dwfinfo));
	  DBFOFF (pst) = dbfoff;
	  DBROFF (pst) = curoff;
	  DBLENGTH (pst) = culength;
	  LNFOFF (pst) = curlnoffset;
	  pst->read_symtab = dwarf_psymtab_to_symtab;

	  /* Now look for partial symbols */

	  scan_partial_symbols (thisdie + di.die_length, nextdie, objfile);

	  pst->n_global_syms = objfile->global_psymbols.next -
	    (objfile->global_psymbols.list + pst->globals_offset);
	  pst->n_static_syms = objfile->static_psymbols.next -
	    (objfile->static_psymbols.list + pst->statics_offset);
	  sort_pst_symbols (pst);
	  /* If there is already a psymtab or symtab for a file of this name,
	     remove it. (If there is a symtab, more drastic things also
	     happen.)  This happens in VxWorks.  */
	  free_named_symtabs (pst->filename);
	}
      thisdie = nextdie;
    }
}

/*

   LOCAL FUNCTION

   new_symbol -- make a symbol table entry for a new symbol

   SYNOPSIS

   static struct symbol *new_symbol (struct dieinfo *dip,
   struct objfile *objfile)

   DESCRIPTION

   Given a pointer to a DWARF information entry, figure out if we need
   to make a symbol table entry for it, and if so, create a new entry
   and return a pointer to it.
 */

static struct symbol *
new_symbol (struct dieinfo *dip, struct objfile *objfile)
{
  struct symbol *sym = NULL;

  if (dip->at_name != NULL)
    {
      sym = (struct symbol *) obstack_alloc (&objfile->objfile_obstack,
					     sizeof (struct symbol));
      OBJSTAT (objfile, n_syms++);
      memset (sym, 0, sizeof (struct symbol));
      /* default assumptions */
      SYMBOL_DOMAIN (sym) = VAR_DOMAIN;
      SYMBOL_CLASS (sym) = LOC_STATIC;
      SYMBOL_TYPE (sym) = decode_die_type (dip);

      /* If this symbol is from a C++ compilation, then attempt to cache the
         demangled form for future reference.  This is a typical time versus
         space tradeoff, that was decided in favor of time because it sped up
         C++ symbol lookups by a factor of about 20. */

      SYMBOL_LANGUAGE (sym) = cu_language;
      SYMBOL_SET_NAMES (sym, dip->at_name, strlen (dip->at_name), objfile);
      switch (dip->die_tag)
	{
	case TAG_label:
	  SYMBOL_VALUE_ADDRESS (sym) = dip->at_low_pc;
	  SYMBOL_CLASS (sym) = LOC_LABEL;
	  break;
	case TAG_global_subroutine:
	case TAG_subroutine:
	  SYMBOL_VALUE_ADDRESS (sym) = dip->at_low_pc;
	  SYMBOL_TYPE (sym) = lookup_function_type (SYMBOL_TYPE (sym));
	  if (dip->at_prototyped)
	    TYPE_FLAGS (SYMBOL_TYPE (sym)) |= TYPE_FLAG_PROTOTYPED;
	  SYMBOL_CLASS (sym) = LOC_BLOCK;
	  if (dip->die_tag == TAG_global_subroutine)
	    {
	      add_symbol_to_list (sym, &global_symbols);
	    }
	  else
	    {
	      add_symbol_to_list (sym, list_in_scope);
	    }
	  break;
	case TAG_global_variable:
	  if (dip->at_location != NULL)
	    {
	      SYMBOL_VALUE_ADDRESS (sym) = locval (dip);
	      add_symbol_to_list (sym, &global_symbols);
	      SYMBOL_CLASS (sym) = LOC_STATIC;
	      SYMBOL_VALUE (sym) += baseaddr;
	    }
	  break;
	case TAG_local_variable:
	  if (dip->at_location != NULL)
	    {
	      int loc = locval (dip);
	      if (dip->optimized_out)
		{
		  SYMBOL_CLASS (sym) = LOC_OPTIMIZED_OUT;
		}
	      else if (dip->isreg)
		{
		  SYMBOL_CLASS (sym) = LOC_REGISTER;
		}
	      else if (dip->offreg)
		{
		  SYMBOL_CLASS (sym) = LOC_BASEREG;
		  SYMBOL_BASEREG (sym) = dip->basereg;
		}
	      else
		{
		  SYMBOL_CLASS (sym) = LOC_STATIC;
		  SYMBOL_VALUE (sym) += baseaddr;
		}
	      if (SYMBOL_CLASS (sym) == LOC_STATIC)
		{
		  /* LOC_STATIC address class MUST use SYMBOL_VALUE_ADDRESS,
		     which may store to a bigger location than SYMBOL_VALUE. */
		  SYMBOL_VALUE_ADDRESS (sym) = loc;
		}
	      else
		{
		  SYMBOL_VALUE (sym) = loc;
		}
	      add_symbol_to_list (sym, list_in_scope);
	    }
	  break;
	case TAG_formal_parameter:
	  if (dip->at_location != NULL)
	    {
	      SYMBOL_VALUE (sym) = locval (dip);
	    }
	  add_symbol_to_list (sym, list_in_scope);
	  if (dip->isreg)
	    {
	      SYMBOL_CLASS (sym) = LOC_REGPARM;
	    }
	  else if (dip->offreg)
	    {
	      SYMBOL_CLASS (sym) = LOC_BASEREG_ARG;
	      SYMBOL_BASEREG (sym) = dip->basereg;
	    }
	  else
	    {
	      SYMBOL_CLASS (sym) = LOC_ARG;
	    }
	  break;
	case TAG_unspecified_parameters:
	  /* From varargs functions; gdb doesn't seem to have any interest in
	     this information, so just ignore it for now. (FIXME?) */
	  break;
	case TAG_class_type:
	case TAG_structure_type:
	case TAG_union_type:
	case TAG_enumeration_type:
	  SYMBOL_CLASS (sym) = LOC_TYPEDEF;
	  SYMBOL_DOMAIN (sym) = STRUCT_DOMAIN;
	  add_symbol_to_list (sym, list_in_scope);
	  break;
	case TAG_typedef:
	  SYMBOL_CLASS (sym) = LOC_TYPEDEF;
	  SYMBOL_DOMAIN (sym) = VAR_DOMAIN;
	  add_symbol_to_list (sym, list_in_scope);
	  break;
	default:
	  /* Not a tag we recognize.  Hopefully we aren't processing trash
	     data, but since we must specifically ignore things we don't
	     recognize, there is nothing else we should do at this point. */
	  break;
	}
    }
  return (sym);
}

/*

   LOCAL FUNCTION

   synthesize_typedef -- make a symbol table entry for a "fake" typedef

   SYNOPSIS

   static void synthesize_typedef (struct dieinfo *dip,
   struct objfile *objfile,
   struct type *type);

   DESCRIPTION

   Given a pointer to a DWARF information entry, synthesize a typedef
   for the name in the DIE, using the specified type.

   This is used for C++ class, structs, unions, and enumerations to
   set up the tag name as a type.

 */

static void
synthesize_typedef (struct dieinfo *dip, struct objfile *objfile,
		    struct type *type)
{
  struct symbol *sym = NULL;

  if (dip->at_name != NULL)
    {
      sym = (struct symbol *)
	obstack_alloc (&objfile->objfile_obstack, sizeof (struct symbol));
      OBJSTAT (objfile, n_syms++);
      memset (sym, 0, sizeof (struct symbol));
      DEPRECATED_SYMBOL_NAME (sym) = create_name (dip->at_name,
				       &objfile->objfile_obstack);
      SYMBOL_INIT_LANGUAGE_SPECIFIC (sym, cu_language);
      SYMBOL_TYPE (sym) = type;
      SYMBOL_CLASS (sym) = LOC_TYPEDEF;
      SYMBOL_DOMAIN (sym) = VAR_DOMAIN;
      add_symbol_to_list (sym, list_in_scope);
    }
}

/*

   LOCAL FUNCTION

   decode_mod_fund_type -- decode a modified fundamental type

   SYNOPSIS

   static struct type *decode_mod_fund_type (char *typedata)

   DESCRIPTION

   Decode a block of data containing a modified fundamental
   type specification.  TYPEDATA is a pointer to the block,
   which starts with a length containing the size of the rest
   of the block.  At the end of the block is a fundmental type
   code value that gives the fundamental type.  Everything
   in between are type modifiers.

   We simply compute the number of modifiers and call the general
   function decode_modified_type to do the actual work.
 */

static struct type *
decode_mod_fund_type (char *typedata)
{
  struct type *typep = NULL;
  unsigned short modcount;
  int nbytes;

  /* Get the total size of the block, exclusive of the size itself */

  nbytes = attribute_size (AT_mod_fund_type);
  modcount = target_to_host (typedata, nbytes, GET_UNSIGNED, current_objfile);
  typedata += nbytes;

  /* Deduct the size of the fundamental type bytes at the end of the block. */

  modcount -= attribute_size (AT_fund_type);

  /* Now do the actual decoding */

  typep = decode_modified_type (typedata, modcount, AT_mod_fund_type);
  return (typep);
}

/*

   LOCAL FUNCTION

   decode_mod_u_d_type -- decode a modified user defined type

   SYNOPSIS

   static struct type *decode_mod_u_d_type (char *typedata)

   DESCRIPTION

   Decode a block of data containing a modified user defined
   type specification.  TYPEDATA is a pointer to the block,
   which consists of a two byte length, containing the size
   of the rest of the block.  At the end of the block is a
   four byte value that gives a reference to a user defined type.
   Everything in between are type modifiers.

   We simply compute the number of modifiers and call the general
   function decode_modified_type to do the actual work.
 */

static struct type *
decode_mod_u_d_type (char *typedata)
{
  struct type *typep = NULL;
  unsigned short modcount;
  int nbytes;

  /* Get the total size of the block, exclusive of the size itself */

  nbytes = attribute_size (AT_mod_u_d_type);
  modcount = target_to_host (typedata, nbytes, GET_UNSIGNED, current_objfile);
  typedata += nbytes;

  /* Deduct the size of the reference type bytes at the end of the block. */

  modcount -= attribute_size (AT_user_def_type);

  /* Now do the actual decoding */

  typep = decode_modified_type (typedata, modcount, AT_mod_u_d_type);
  return (typep);
}

/*

   LOCAL FUNCTION

   decode_modified_type -- decode modified user or fundamental type

   SYNOPSIS

   static struct type *decode_modified_type (char *modifiers,
   unsigned short modcount, int mtype)

   DESCRIPTION

   Decode a modified type, either a modified fundamental type or
   a modified user defined type.  MODIFIERS is a pointer to the
   block of bytes that define MODCOUNT modifiers.  Immediately
   following the last modifier is a short containing the fundamental
   type or a long containing the reference to the user defined
   type.  Which one is determined by MTYPE, which is either
   AT_mod_fund_type or AT_mod_u_d_type to indicate what modified
   type we are generating.

   We call ourself recursively to generate each modified type,`
   until MODCOUNT reaches zero, at which point we have consumed
   all the modifiers and generate either the fundamental type or
   user defined type.  When the recursion unwinds, each modifier
   is applied in turn to generate the full modified type.

   NOTES

   If we find a modifier that we don't recognize, and it is not one
   of those reserved for application specific use, then we issue a
   warning and simply ignore the modifier.

   BUGS

   We currently ignore MOD_const and MOD_volatile.  (FIXME)

 */

static struct type *
decode_modified_type (char *modifiers, unsigned int modcount, int mtype)
{
  struct type *typep = NULL;
  unsigned short fundtype;
  DIE_REF die_ref;
  char modifier;
  int nbytes;

  if (modcount == 0)
    {
      switch (mtype)
	{
	case AT_mod_fund_type:
	  nbytes = attribute_size (AT_fund_type);
	  fundtype = target_to_host (modifiers, nbytes, GET_UNSIGNED,
				     current_objfile);
	  typep = decode_fund_type (fundtype);
	  break;
	case AT_mod_u_d_type:
	  nbytes = attribute_size (AT_user_def_type);
	  die_ref = target_to_host (modifiers, nbytes, GET_UNSIGNED,
				    current_objfile);
	  typep = lookup_utype (die_ref);
	  if (typep == NULL)
	    {
	      typep = alloc_utype (die_ref, NULL);
	    }
	  break;
	default:
	  complaint (&symfile_complaints,
		     "DIE @ 0x%x \"%s\", botched modified type decoding (mtype 0x%x)",
		     DIE_ID, DIE_NAME, mtype);
	  typep = dwarf_fundamental_type (current_objfile, FT_INTEGER);
	  break;
	}
    }
  else
    {
      modifier = *modifiers++;
      typep = decode_modified_type (modifiers, --modcount, mtype);
      switch (modifier)
	{
	case MOD_pointer_to:
	  typep = lookup_pointer_type (typep);
	  break;
	case MOD_reference_to:
	  typep = lookup_reference_type (typep);
	  break;
	case MOD_const:
	  complaint (&symfile_complaints,
		     "DIE @ 0x%x \"%s\", type modifier 'const' ignored", DIE_ID,
		     DIE_NAME);	/* FIXME */
	  break;
	case MOD_volatile:
	  complaint (&symfile_complaints,
		     "DIE @ 0x%x \"%s\", type modifier 'volatile' ignored",
		     DIE_ID, DIE_NAME);	/* FIXME */
	  break;
	default:
	  if (!(MOD_lo_user <= (unsigned char) modifier))
#if 0
/* This part of the test would always be true, and it triggers a compiler
   warning.  */
		&& (unsigned char) modifier <= MOD_hi_user))
#endif
	    {
	      complaint (&symfile_complaints,
			 "DIE @ 0x%x \"%s\", unknown type modifier %u", DIE_ID,
			 DIE_NAME, modifier);
	    }
	  break;
	}
    }
  return (typep);
}

/*

   LOCAL FUNCTION

   decode_fund_type -- translate basic DWARF type to gdb base type

   DESCRIPTION

   Given an integer that is one of the fundamental DWARF types,
   translate it to one of the basic internal gdb types and return
   a pointer to the appropriate gdb type (a "struct type *").

   NOTES

   For robustness, if we are asked to translate a fundamental
   type that we are unprepared to deal with, we return int so
   callers can always depend upon a valid type being returned,
   and so gdb may at least do something reasonable by default.
   If the type is not in the range of those types defined as
   application specific types, we also issue a warning.
 */

static struct type *
decode_fund_type (unsigned int fundtype)
{
  struct type *typep = NULL;

  switch (fundtype)
    {

    case FT_void:
      typep = dwarf_fundamental_type (current_objfile, FT_VOID);
      break;

    case FT_boolean:		/* Was FT_set in AT&T version */
      typep = dwarf_fundamental_type (current_objfile, FT_BOOLEAN);
      break;

    case FT_pointer:		/* (void *) */
      typep = dwarf_fundamental_type (current_objfile, FT_VOID);
      typep = lookup_pointer_type (typep);
      break;

    case FT_char:
      typep = dwarf_fundamental_type (current_objfile, FT_CHAR);
      break;

    case FT_signed_char:
      typep = dwarf_fundamental_type (current_objfile, FT_SIGNED_CHAR);
      break;

    case FT_unsigned_char:
      typep = dwarf_fundamental_type (current_objfile, FT_UNSIGNED_CHAR);
      break;

    case FT_short:
      typep = dwarf_fundamental_type (current_objfile, FT_SHORT);
      break;

    case FT_signed_short:
      typep = dwarf_fundamental_type (current_objfile, FT_SIGNED_SHORT);
      break;

    case FT_unsigned_short:
      typep = dwarf_fundamental_type (current_objfile, FT_UNSIGNED_SHORT);
      break;

    case FT_integer:
      typep = dwarf_fundamental_type (current_objfile, FT_INTEGER);
      break;

    case FT_signed_integer:
      typep = dwarf_fundamental_type (current_objfile, FT_SIGNED_INTEGER);
      break;

    case FT_unsigned_integer:
      typep = dwarf_fundamental_type (current_objfile, FT_UNSIGNED_INTEGER);
      break;

    case FT_long:
      typep = dwarf_fundamental_type (current_objfile, FT_LONG);
      break;

    case FT_signed_long:
      typep = dwarf_fundamental_type (current_objfile, FT_SIGNED_LONG);
      break;

    case FT_unsigned_long:
      typep = dwarf_fundamental_type (current_objfile, FT_UNSIGNED_LONG);
      break;

    case FT_long_long:
      typep = dwarf_fundamental_type (current_objfile, FT_LONG_LONG);
      break;

    case FT_signed_long_long:
      typep = dwarf_fundamental_type (current_objfile, FT_SIGNED_LONG_LONG);
      break;

    case FT_unsigned_long_long:
      typep = dwarf_fundamental_type (current_objfile, FT_UNSIGNED_LONG_LONG);
      break;

    case FT_float:
      typep = dwarf_fundamental_type (current_objfile, FT_FLOAT);
      break;

    case FT_dbl_prec_float:
      typep = dwarf_fundamental_type (current_objfile, FT_DBL_PREC_FLOAT);
      break;

    case FT_ext_prec_float:
      typep = dwarf_fundamental_type (current_objfile, FT_EXT_PREC_FLOAT);
      break;

    case FT_complex:
      typep = dwarf_fundamental_type (current_objfile, FT_COMPLEX);
      break;

    case FT_dbl_prec_complex:
      typep = dwarf_fundamental_type (current_objfile, FT_DBL_PREC_COMPLEX);
      break;

    case FT_ext_prec_complex:
      typep = dwarf_fundamental_type (current_objfile, FT_EXT_PREC_COMPLEX);
      break;

    }

  if (typep == NULL)
    {
      typep = dwarf_fundamental_type (current_objfile, FT_INTEGER);
      if (!(FT_lo_user <= fundtype && fundtype <= FT_hi_user))
	{
	  complaint (&symfile_complaints,
		     "DIE @ 0x%x \"%s\", unexpected fundamental type 0x%x",
		     DIE_ID, DIE_NAME, fundtype);
	}
    }

  return (typep);
}

/*

   LOCAL FUNCTION

   create_name -- allocate a fresh copy of a string on an obstack

   DESCRIPTION

   Given a pointer to a string and a pointer to an obstack, allocates
   a fresh copy of the string on the specified obstack.

 */

static char *
create_name (char *name, struct obstack *obstackp)
{
  int length;
  char *newname;

  length = strlen (name) + 1;
  newname = (char *) obstack_alloc (obstackp, length);
  strcpy (newname, name);
  return (newname);
}

/*

   LOCAL FUNCTION

   basicdieinfo -- extract the minimal die info from raw die data

   SYNOPSIS

   void basicdieinfo (char *diep, struct dieinfo *dip,
   struct objfile *objfile)

   DESCRIPTION

   Given a pointer to raw DIE data, and a pointer to an instance of a
   die info structure, this function extracts the basic information
   from the DIE data required to continue processing this DIE, along
   with some bookkeeping information about the DIE.

   The information we absolutely must have includes the DIE tag,
   and the DIE length.  If we need the sibling reference, then we
   will have to call completedieinfo() to process all the remaining
   DIE information.

   Note that since there is no guarantee that the data is properly
   aligned in memory for the type of access required (indirection
   through anything other than a char pointer), and there is no
   guarantee that it is in the same byte order as the gdb host,
   we call a function which deals with both alignment and byte
   swapping issues.  Possibly inefficient, but quite portable.

   We also take care of some other basic things at this point, such
   as ensuring that the instance of the die info structure starts
   out completely zero'd and that curdie is initialized for use
   in error reporting if we have a problem with the current die.

   NOTES

   All DIE's must have at least a valid length, thus the minimum
   DIE size is SIZEOF_DIE_LENGTH.  In order to have a valid tag, the
   DIE size must be at least SIZEOF_DIE_TAG larger, otherwise they
   are forced to be TAG_padding DIES.

   Padding DIES must be at least SIZEOF_DIE_LENGTH in length, implying
   that if a padding DIE is used for alignment and the amount needed is
   less than SIZEOF_DIE_LENGTH, then the padding DIE has to be big
   enough to align to the next alignment boundry.

   We do some basic sanity checking here, such as verifying that the
   length of the die would not cause it to overrun the recorded end of
   the buffer holding the DIE info.  If we find a DIE that is either
   too small or too large, we force it's length to zero which should
   cause the caller to take appropriate action.
 */

static void
basicdieinfo (struct dieinfo *dip, char *diep, struct objfile *objfile)
{
  curdie = dip;
  memset (dip, 0, sizeof (struct dieinfo));
  dip->die = diep;
  dip->die_ref = dbroff + (diep - dbbase);
  dip->die_length = target_to_host (diep, SIZEOF_DIE_LENGTH, GET_UNSIGNED,
				    objfile);
  if ((dip->die_length < SIZEOF_DIE_LENGTH) ||
      ((diep + dip->die_length) > (dbbase + dbsize)))
    {
      complaint (&symfile_complaints,
		 "DIE @ 0x%x \"%s\", malformed DIE, bad length (%ld bytes)",
		 DIE_ID, DIE_NAME, dip->die_length);
      dip->die_length = 0;
    }
  else if (dip->die_length < (SIZEOF_DIE_LENGTH + SIZEOF_DIE_TAG))
    {
      dip->die_tag = TAG_padding;
    }
  else
    {
      diep += SIZEOF_DIE_LENGTH;
      dip->die_tag = target_to_host (diep, SIZEOF_DIE_TAG, GET_UNSIGNED,
				     objfile);
    }
}

/*

   LOCAL FUNCTION

   completedieinfo -- finish reading the information for a given DIE

   SYNOPSIS

   void completedieinfo (struct dieinfo *dip, struct objfile *objfile)

   DESCRIPTION

   Given a pointer to an already partially initialized die info structure,
   scan the raw DIE data and finish filling in the die info structure
   from the various attributes found.

   Note that since there is no guarantee that the data is properly
   aligned in memory for the type of access required (indirection
   through anything other than a char pointer), and there is no
   guarantee that it is in the same byte order as the gdb host,
   we call a function which deals with both alignment and byte
   swapping issues.  Possibly inefficient, but quite portable.

   NOTES

   Each time we are called, we increment the diecount variable, which
   keeps an approximate count of the number of dies processed for
   each compilation unit.  This information is presented to the user
   if the info_verbose flag is set.

 */

static void
completedieinfo (struct dieinfo *dip, struct objfile *objfile)
{
  char *diep;			/* Current pointer into raw DIE data */
  char *end;			/* Terminate DIE scan here */
  unsigned short attr;		/* Current attribute being scanned */
  unsigned short form;		/* Form of the attribute */
  int nbytes;			/* Size of next field to read */

  diecount++;
  diep = dip->die;
  end = diep + dip->die_length;
  diep += SIZEOF_DIE_LENGTH + SIZEOF_DIE_TAG;
  while (diep < end)
    {
      attr = target_to_host (diep, SIZEOF_ATTRIBUTE, GET_UNSIGNED, objfile);
      diep += SIZEOF_ATTRIBUTE;
      nbytes = attribute_size (attr);
      if (nbytes == -1)
	{
	  complaint (&symfile_complaints,
		     "DIE @ 0x%x \"%s\", unknown attribute length, skipped remaining attributes",
		     DIE_ID, DIE_NAME);
	  diep = end;
	  continue;
	}
      switch (attr)
	{
	case AT_fund_type:
	  dip->at_fund_type = target_to_host (diep, nbytes, GET_UNSIGNED,
					      objfile);
	  break;
	case AT_ordering:
	  dip->at_ordering = target_to_host (diep, nbytes, GET_UNSIGNED,
					     objfile);
	  break;
	case AT_bit_offset:
	  dip->at_bit_offset = target_to_host (diep, nbytes, GET_UNSIGNED,
					       objfile);
	  break;
	case AT_sibling:
	  dip->at_sibling = target_to_host (diep, nbytes, GET_UNSIGNED,
					    objfile);
	  break;
	case AT_stmt_list:
	  dip->at_stmt_list = target_to_host (diep, nbytes, GET_UNSIGNED,
					      objfile);
	  dip->has_at_stmt_list = 1;
	  break;
	case AT_low_pc:
	  dip->at_low_pc = target_to_host (diep, nbytes, GET_UNSIGNED,
					   objfile);
	  dip->at_low_pc += baseaddr;
	  dip->has_at_low_pc = 1;
	  break;
	case AT_high_pc:
	  dip->at_high_pc = target_to_host (diep, nbytes, GET_UNSIGNED,
					    objfile);
	  dip->at_high_pc += baseaddr;
	  break;
	case AT_language:
	  dip->at_language = target_to_host (diep, nbytes, GET_UNSIGNED,
					     objfile);
	  break;
	case AT_user_def_type:
	  dip->at_user_def_type = target_to_host (diep, nbytes,
						  GET_UNSIGNED, objfile);
	  break;
	case AT_byte_size:
	  dip->at_byte_size = target_to_host (diep, nbytes, GET_UNSIGNED,
					      objfile);
	  dip->has_at_byte_size = 1;
	  break;
	case AT_bit_size:
	  dip->at_bit_size = target_to_host (diep, nbytes, GET_UNSIGNED,
					     objfile);
	  break;
	case AT_member:
	  dip->at_member = target_to_host (diep, nbytes, GET_UNSIGNED,
					   objfile);
	  break;
	case AT_discr:
	  dip->at_discr = target_to_host (diep, nbytes, GET_UNSIGNED,
					  objfile);
	  break;
	case AT_location:
	  dip->at_location = diep;
	  break;
	case AT_mod_fund_type:
	  dip->at_mod_fund_type = diep;
	  break;
	case AT_subscr_data:
	  dip->at_subscr_data = diep;
	  break;
	case AT_mod_u_d_type:
	  dip->at_mod_u_d_type = diep;
	  break;
	case AT_element_list:
	  dip->at_element_list = diep;
	  dip->short_element_list = 0;
	  break;
	case AT_short_element_list:
	  dip->at_element_list = diep;
	  dip->short_element_list = 1;
	  break;
	case AT_discr_value:
	  dip->at_discr_value = diep;
	  break;
	case AT_string_length:
	  dip->at_string_length = diep;
	  break;
	case AT_name:
	  dip->at_name = diep;
	  break;
	case AT_comp_dir:
	  /* For now, ignore any "hostname:" portion, since gdb doesn't
	     know how to deal with it.  (FIXME). */
	  dip->at_comp_dir = strrchr (diep, ':');
	  if (dip->at_comp_dir != NULL)
	    {
	      dip->at_comp_dir++;
	    }
	  else
	    {
	      dip->at_comp_dir = diep;
	    }
	  break;
	case AT_producer:
	  dip->at_producer = diep;
	  break;
	case AT_start_scope:
	  dip->at_start_scope = target_to_host (diep, nbytes, GET_UNSIGNED,
						objfile);
	  break;
	case AT_stride_size:
	  dip->at_stride_size = target_to_host (diep, nbytes, GET_UNSIGNED,
						objfile);
	  break;
	case AT_src_info:
	  dip->at_src_info = target_to_host (diep, nbytes, GET_UNSIGNED,
					     objfile);
	  break;
	case AT_prototyped:
	  dip->at_prototyped = diep;
	  break;
	default:
	  /* Found an attribute that we are unprepared to handle.  However
	     it is specifically one of the design goals of DWARF that
	     consumers should ignore unknown attributes.  As long as the
	     form is one that we recognize (so we know how to skip it),
	     we can just ignore the unknown attribute. */
	  break;
	}
      form = FORM_FROM_ATTR (attr);
      switch (form)
	{
	case FORM_DATA2:
	  diep += 2;
	  break;
	case FORM_DATA4:
	case FORM_REF:
	  diep += 4;
	  break;
	case FORM_DATA8:
	  diep += 8;
	  break;
	case FORM_ADDR:
	  diep += TARGET_FT_POINTER_SIZE (objfile);
	  break;
	case FORM_BLOCK2:
	  diep += 2 + target_to_host (diep, nbytes, GET_UNSIGNED, objfile);
	  break;
	case FORM_BLOCK4:
	  diep += 4 + target_to_host (diep, nbytes, GET_UNSIGNED, objfile);
	  break;
	case FORM_STRING:
	  diep += strlen (diep) + 1;
	  break;
	default:
	  unknown_attribute_form_complaint (DIE_ID, DIE_NAME, form);
	  diep = end;
	  break;
	}
    }
}

/*

   LOCAL FUNCTION

   target_to_host -- swap in target data to host

   SYNOPSIS

   target_to_host (char *from, int nbytes, int signextend,
   struct objfile *objfile)

   DESCRIPTION

   Given pointer to data in target format in FROM, a byte count for
   the size of the data in NBYTES, a flag indicating whether or not
   the data is signed in SIGNEXTEND, and a pointer to the current
   objfile in OBJFILE, convert the data to host format and return
   the converted value.

   NOTES

   FIXME:  If we read data that is known to be signed, and expect to
   use it as signed data, then we need to explicitly sign extend the
   result until the bfd library is able to do this for us.

   FIXME: Would a 32 bit target ever need an 8 byte result?

 */

static CORE_ADDR
target_to_host (char *from, int nbytes, int signextend,	/* FIXME:  Unused */
		struct objfile *objfile)
{
  CORE_ADDR rtnval;

  switch (nbytes)
    {
    case 8:
      rtnval = bfd_get_64 (objfile->obfd, (bfd_byte *) from);
      break;
    case 4:
      rtnval = bfd_get_32 (objfile->obfd, (bfd_byte *) from);
      break;
    case 2:
      rtnval = bfd_get_16 (objfile->obfd, (bfd_byte *) from);
      break;
    case 1:
      rtnval = bfd_get_8 (objfile->obfd, (bfd_byte *) from);
      break;
    default:
      complaint (&symfile_complaints,
		 "DIE @ 0x%x \"%s\", no bfd support for %d byte data object",
		 DIE_ID, DIE_NAME, nbytes);
      rtnval = 0;
      break;
    }
  return (rtnval);
}

/*

   LOCAL FUNCTION

   attribute_size -- compute size of data for a DWARF attribute

   SYNOPSIS

   static int attribute_size (unsigned int attr)

   DESCRIPTION

   Given a DWARF attribute in ATTR, compute the size of the first
   piece of data associated with this attribute and return that
   size.

   Returns -1 for unrecognized attributes.

 */

static int
attribute_size (unsigned int attr)
{
  int nbytes;			/* Size of next data for this attribute */
  unsigned short form;		/* Form of the attribute */

  form = FORM_FROM_ATTR (attr);
  switch (form)
    {
    case FORM_STRING:		/* A variable length field is next */
      nbytes = 0;
      break;
    case FORM_DATA2:		/* Next 2 byte field is the data itself */
    case FORM_BLOCK2:		/* Next 2 byte field is a block length */
      nbytes = 2;
      break;
    case FORM_DATA4:		/* Next 4 byte field is the data itself */
    case FORM_BLOCK4:		/* Next 4 byte field is a block length */
    case FORM_REF:		/* Next 4 byte field is a DIE offset */
      nbytes = 4;
      break;
    case FORM_DATA8:		/* Next 8 byte field is the data itself */
      nbytes = 8;
      break;
    case FORM_ADDR:		/* Next field size is target sizeof(void *) */
      nbytes = TARGET_FT_POINTER_SIZE (objfile);
      break;
    default:
      unknown_attribute_form_complaint (DIE_ID, DIE_NAME, form);
      nbytes = -1;
      break;
    }
  return (nbytes);
}
