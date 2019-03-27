/* Read dbx symbol tables and convert to internal format, for GDB.
   Copyright 1986, 1987, 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995,
   1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004.
   Free Software Foundation, Inc.

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

/* This module provides three functions: dbx_symfile_init,
   which initializes to read a symbol file; dbx_new_init, which 
   discards existing cached information when all symbols are being
   discarded; and dbx_symfile_read, which reads a symbol table
   from a file.

   dbx_symfile_read only does the minimum work necessary for letting the
   user "name" things symbolically; it does not read the entire symtab.
   Instead, it reads the external and static symbols and puts them in partial
   symbol tables.  When more extensive information is requested of a
   file, the corresponding partial symbol table is mutated into a full
   fledged symbol table by going back and reading the symbols
   for real.  dbx_psymtab_to_symtab() is the function that does this */

#include "defs.h"
#include "gdb_string.h"

#if defined(USG) || defined(__CYGNUSCLIB__)
#include <sys/types.h>
#include <fcntl.h>
#endif

#include "gdb_obstack.h"
#include "gdb_stat.h"
#include "symtab.h"
#include "breakpoint.h"
#include "target.h"
#include "gdbcore.h"		/* for bfd stuff */
#include "libaout.h"		/* FIXME Secret internal BFD stuff for a.out */
#include "objfiles.h"
#include "buildsym.h"
#include "stabsread.h"
#include "gdb-stabs.h"
#include "demangle.h"
#include "language.h"		/* Needed for local_hex_string */
#include "complaints.h"
#include "cp-abi.h"
#include "gdb_assert.h"

#include "aout/aout64.h"
#include "aout/stab_gnu.h"	/* We always use GNU stabs, not native, now */


/* We put a pointer to this structure in the read_symtab_private field
   of the psymtab.  */

struct symloc
  {
    /* Offset within the file symbol table of first local symbol for this
       file.  */

    int ldsymoff;

    /* Length (in bytes) of the section of the symbol table devoted to
       this file's symbols (actually, the section bracketed may contain
       more than just this file's symbols).  If ldsymlen is 0, the only
       reason for this thing's existence is the dependency list.  Nothing
       else will happen when it is read in.  */

    int ldsymlen;

    /* The size of each symbol in the symbol file (in external form).  */

    int symbol_size;

    /* Further information needed to locate the symbols if they are in
       an ELF file.  */

    int symbol_offset;
    int string_offset;
    int file_string_offset;
  };

#define LDSYMOFF(p) (((struct symloc *)((p)->read_symtab_private))->ldsymoff)
#define LDSYMLEN(p) (((struct symloc *)((p)->read_symtab_private))->ldsymlen)
#define SYMLOC(p) ((struct symloc *)((p)->read_symtab_private))
#define SYMBOL_SIZE(p) (SYMLOC(p)->symbol_size)
#define SYMBOL_OFFSET(p) (SYMLOC(p)->symbol_offset)
#define STRING_OFFSET(p) (SYMLOC(p)->string_offset)
#define FILE_STRING_OFFSET(p) (SYMLOC(p)->file_string_offset)


/* Remember what we deduced to be the source language of this psymtab. */

static enum language psymtab_language = language_unknown;

/* The BFD for this file -- implicit parameter to next_symbol_text.  */

static bfd *symfile_bfd;

/* The size of each symbol in the symbol file (in external form).
   This is set by dbx_symfile_read when building psymtabs, and by
   dbx_psymtab_to_symtab when building symtabs.  */

static unsigned symbol_size;

/* This is the offset of the symbol table in the executable file. */

static unsigned symbol_table_offset;

/* This is the offset of the string table in the executable file. */

static unsigned string_table_offset;

/* For elf+stab executables, the n_strx field is not a simple index
   into the string table.  Instead, each .o file has a base offset in
   the string table, and the associated symbols contain offsets from
   this base.  The following two variables contain the base offset for
   the current and next .o files. */

static unsigned int file_string_table_offset;
static unsigned int next_file_string_table_offset;

/* .o and NLM files contain unrelocated addresses which are based at
   0.  When non-zero, this flag disables some of the special cases for
   Solaris elf+stab text addresses at location 0. */

static int symfile_relocatable = 0;

/* If this is nonzero, N_LBRAC, N_RBRAC, and N_SLINE entries are
   relative to the function start address.  */

static int block_address_function_relative = 0;

/* The lowest text address we have yet encountered.  This is needed
   because in an a.out file, there is no header field which tells us
   what address the program is actually going to be loaded at, so we
   need to make guesses based on the symbols (which *are* relocated to
   reflect the address it will be loaded at).  */

static CORE_ADDR lowest_text_address;

/* Non-zero if there is any line number info in the objfile.  Prevents
   end_psymtab from discarding an otherwise empty psymtab.  */

static int has_line_numbers;

/* Complaints about the symbols we have encountered.  */

static void
unknown_symtype_complaint (const char *arg1)
{
  complaint (&symfile_complaints, "unknown symbol type %s", arg1);
}

static void
lbrac_mismatch_complaint (int arg1)
{
  complaint (&symfile_complaints,
	     "N_LBRAC/N_RBRAC symbol mismatch at symtab pos %d", arg1);
}

static void
repeated_header_complaint (const char *arg1, int arg2)
{
  complaint (&symfile_complaints,
	     "\"repeated\" header file %s not previously seen, at symtab pos %d",
	     arg1, arg2);
}

/* find_text_range --- find start and end of loadable code sections

   The find_text_range function finds the shortest address range that
   encloses all sections containing executable code, and stores it in
   objfile's text_addr and text_size members.

   dbx_symfile_read will use this to finish off the partial symbol
   table, in some cases.  */

static void
find_text_range (bfd * sym_bfd, struct objfile *objfile)
{
  asection *sec;
  int found_any = 0;
  CORE_ADDR start = 0;
  CORE_ADDR end = 0;

  for (sec = sym_bfd->sections; sec; sec = sec->next)
    if (bfd_get_section_flags (sym_bfd, sec) & SEC_CODE)
      {
	CORE_ADDR sec_start = bfd_section_vma (sym_bfd, sec);
	CORE_ADDR sec_end = sec_start + bfd_section_size (sym_bfd, sec);

	if (found_any)
	  {
	    if (sec_start < start)
	      start = sec_start;
	    if (sec_end > end)
	      end = sec_end;
	  }
	else
	  {
	    start = sec_start;
	    end = sec_end;
	  }

	found_any = 1;
      }

  if (!found_any)
    error ("Can't find any code sections in symbol file");

  DBX_TEXT_ADDR (objfile) = start;
  DBX_TEXT_SIZE (objfile) = end - start;
}



/* During initial symbol readin, we need to have a structure to keep
   track of which psymtabs have which bincls in them.  This structure
   is used during readin to setup the list of dependencies within each
   partial symbol table. */

struct header_file_location
{
  char *name;			/* Name of header file */
  int instance;			/* See above */
  struct partial_symtab *pst;	/* Partial symtab that has the
				   BINCL/EINCL defs for this file */
};

/* The actual list and controling variables */
static struct header_file_location *bincl_list, *next_bincl;
static int bincls_allocated;

/* Local function prototypes */

extern void _initialize_dbxread (void);

static void read_ofile_symtab (struct partial_symtab *);

static void dbx_psymtab_to_symtab (struct partial_symtab *);

static void dbx_psymtab_to_symtab_1 (struct partial_symtab *);

static void read_dbx_dynamic_symtab (struct objfile *objfile);

static void read_dbx_symtab (struct objfile *);

static void free_bincl_list (struct objfile *);

static struct partial_symtab *find_corresponding_bincl_psymtab (char *, int);

static void add_bincl_to_list (struct partial_symtab *, char *, int);

static void init_bincl_list (int, struct objfile *);

static char *dbx_next_symbol_text (struct objfile *);

static void fill_symbuf (bfd *);

static void dbx_symfile_init (struct objfile *);

static void dbx_new_init (struct objfile *);

static void dbx_symfile_read (struct objfile *, int);

static void dbx_symfile_finish (struct objfile *);

static void record_minimal_symbol (char *, CORE_ADDR, int, struct objfile *);

static void add_new_header_file (char *, int);

static void add_old_header_file (char *, int);

static void add_this_object_header_file (int);

static struct partial_symtab *start_psymtab (struct objfile *, char *,
					     CORE_ADDR, int,
					     struct partial_symbol **,
					     struct partial_symbol **);

/* Free up old header file tables */

void
free_header_files (void)
{
  if (this_object_header_files)
    {
      xfree (this_object_header_files);
      this_object_header_files = NULL;
    }
  n_allocated_this_object_header_files = 0;
}

/* Allocate new header file tables */

void
init_header_files (void)
{
  n_allocated_this_object_header_files = 10;
  this_object_header_files = (int *) xmalloc (10 * sizeof (int));
}

/* Add header file number I for this object file
   at the next successive FILENUM.  */

static void
add_this_object_header_file (int i)
{
  if (n_this_object_header_files == n_allocated_this_object_header_files)
    {
      n_allocated_this_object_header_files *= 2;
      this_object_header_files
	= (int *) xrealloc ((char *) this_object_header_files,
		       n_allocated_this_object_header_files * sizeof (int));
    }

  this_object_header_files[n_this_object_header_files++] = i;
}

/* Add to this file an "old" header file, one already seen in
   a previous object file.  NAME is the header file's name.
   INSTANCE is its instance code, to select among multiple
   symbol tables for the same header file.  */

static void
add_old_header_file (char *name, int instance)
{
  struct header_file *p = HEADER_FILES (current_objfile);
  int i;

  for (i = 0; i < N_HEADER_FILES (current_objfile); i++)
    if (strcmp (p[i].name, name) == 0 && instance == p[i].instance)
      {
	add_this_object_header_file (i);
	return;
      }
  repeated_header_complaint (name, symnum);
}

/* Add to this file a "new" header file: definitions for its types follow.
   NAME is the header file's name.
   Most often this happens only once for each distinct header file,
   but not necessarily.  If it happens more than once, INSTANCE has
   a different value each time, and references to the header file
   use INSTANCE values to select among them.

   dbx output contains "begin" and "end" markers for each new header file,
   but at this level we just need to know which files there have been;
   so we record the file when its "begin" is seen and ignore the "end".  */

static void
add_new_header_file (char *name, int instance)
{
  int i;
  struct header_file *hfile;

  /* Make sure there is room for one more header file.  */

  i = N_ALLOCATED_HEADER_FILES (current_objfile);

  if (N_HEADER_FILES (current_objfile) == i)
    {
      if (i == 0)
	{
	  N_ALLOCATED_HEADER_FILES (current_objfile) = 10;
	  HEADER_FILES (current_objfile) = (struct header_file *)
	    xmalloc (10 * sizeof (struct header_file));
	}
      else
	{
	  i *= 2;
	  N_ALLOCATED_HEADER_FILES (current_objfile) = i;
	  HEADER_FILES (current_objfile) = (struct header_file *)
	    xrealloc ((char *) HEADER_FILES (current_objfile),
		      (i * sizeof (struct header_file)));
	}
    }

  /* Create an entry for this header file.  */

  i = N_HEADER_FILES (current_objfile)++;
  hfile = HEADER_FILES (current_objfile) + i;
  hfile->name = savestring (name, strlen (name));
  hfile->instance = instance;
  hfile->length = 10;
  hfile->vector
    = (struct type **) xmalloc (10 * sizeof (struct type *));
  memset (hfile->vector, 0, 10 * sizeof (struct type *));

  add_this_object_header_file (i);
}

#if 0
static struct type **
explicit_lookup_type (int real_filenum, int index)
{
  struct header_file *f = &HEADER_FILES (current_objfile)[real_filenum];

  if (index >= f->length)
    {
      f->length *= 2;
      f->vector = (struct type **)
	xrealloc (f->vector, f->length * sizeof (struct type *));
      memset (&f->vector[f->length / 2],
	      '\0', f->length * sizeof (struct type *) / 2);
    }
  return &f->vector[index];
}
#endif

static void
record_minimal_symbol (char *name, CORE_ADDR address, int type,
		       struct objfile *objfile)
{
  enum minimal_symbol_type ms_type;
  int section;
  asection *bfd_section;

  switch (type)
    {
    case N_TEXT | N_EXT:
      ms_type = mst_text;
      section = SECT_OFF_TEXT (objfile);
      bfd_section = DBX_TEXT_SECTION (objfile);
      break;
    case N_DATA | N_EXT:
      ms_type = mst_data;
      section = SECT_OFF_DATA (objfile);
      bfd_section = DBX_DATA_SECTION (objfile);
      break;
    case N_BSS | N_EXT:
      ms_type = mst_bss;
      section = SECT_OFF_BSS (objfile);
      bfd_section = DBX_BSS_SECTION (objfile);
      break;
    case N_ABS | N_EXT:
      ms_type = mst_abs;
      section = -1;
      bfd_section = NULL;
      break;
#ifdef N_SETV
    case N_SETV | N_EXT:
      ms_type = mst_data;
      section = SECT_OFF_DATA (objfile);
      bfd_section = DBX_DATA_SECTION (objfile);
      break;
    case N_SETV:
      /* I don't think this type actually exists; since a N_SETV is the result
         of going over many .o files, it doesn't make sense to have one
         file local.  */
      ms_type = mst_file_data;
      section = SECT_OFF_DATA (objfile);
      bfd_section = DBX_DATA_SECTION (objfile);
      break;
#endif
    case N_TEXT:
    case N_NBTEXT:
    case N_FN:
    case N_FN_SEQ:
      ms_type = mst_file_text;
      section = SECT_OFF_TEXT (objfile);
      bfd_section = DBX_TEXT_SECTION (objfile);
      break;
    case N_DATA:
      ms_type = mst_file_data;

      /* Check for __DYNAMIC, which is used by Sun shared libraries. 
         Record it as global even if it's local, not global, so
         lookup_minimal_symbol can find it.  We don't check symbol_leading_char
         because for SunOS4 it always is '_'.  */
      if (name[8] == 'C' && DEPRECATED_STREQ ("__DYNAMIC", name))
	ms_type = mst_data;

      /* Same with virtual function tables, both global and static.  */
      {
	char *tempstring = name;
	if (tempstring[0] == bfd_get_symbol_leading_char (objfile->obfd))
	  ++tempstring;
	if (is_vtable_name (tempstring))
	  ms_type = mst_data;
      }
      section = SECT_OFF_DATA (objfile);
      bfd_section = DBX_DATA_SECTION (objfile);
      break;
    case N_BSS:
      ms_type = mst_file_bss;
      section = SECT_OFF_BSS (objfile);
      bfd_section = DBX_BSS_SECTION (objfile);
      break;
    default:
      ms_type = mst_unknown;
      section = -1;
      bfd_section = NULL;
      break;
    }

  if ((ms_type == mst_file_text || ms_type == mst_text)
      && address < lowest_text_address)
    lowest_text_address = address;

  prim_record_minimal_symbol_and_info
    (name, address, ms_type, NULL, section, bfd_section, objfile);
}

/* Scan and build partial symbols for a symbol file.
   We have been initialized by a call to dbx_symfile_init, which 
   put all the relevant info into a "struct dbx_symfile_info",
   hung off the objfile structure.

   MAINLINE is true if we are reading the main symbol
   table (as opposed to a shared lib or dynamically loaded file).  */

static void
dbx_symfile_read (struct objfile *objfile, int mainline)
{
  bfd *sym_bfd;
  int val;
  struct cleanup *back_to;

  sym_bfd = objfile->obfd;

  /* .o and .nlm files are relocatables with text, data and bss segs based at
     0.  This flag disables special (Solaris stabs-in-elf only) fixups for
     symbols with a value of 0.  */

  symfile_relocatable = bfd_get_file_flags (sym_bfd) & HAS_RELOC;

  /* This is true for Solaris (and all other systems which put stabs
     in sections, hopefully, since it would be silly to do things
     differently from Solaris), and false for SunOS4 and other a.out
     file formats.  */
  block_address_function_relative =
    ((0 == strncmp (bfd_get_target (sym_bfd), "elf", 3))
     || (0 == strncmp (bfd_get_target (sym_bfd), "som", 3))
     || (0 == strncmp (bfd_get_target (sym_bfd), "coff", 4))
     || (0 == strncmp (bfd_get_target (sym_bfd), "pe", 2))
     || (0 == strncmp (bfd_get_target (sym_bfd), "epoc-pe", 7))
     || (0 == strncmp (bfd_get_target (sym_bfd), "nlm", 3)));

  val = bfd_seek (sym_bfd, DBX_SYMTAB_OFFSET (objfile), SEEK_SET);
  if (val < 0)
    perror_with_name (objfile->name);

  /* If we are reinitializing, or if we have never loaded syms yet, init */
  if (mainline
      || (objfile->global_psymbols.size == 0
	  &&  objfile->static_psymbols.size == 0))
    init_psymbol_list (objfile, DBX_SYMCOUNT (objfile));

  symbol_size = DBX_SYMBOL_SIZE (objfile);
  symbol_table_offset = DBX_SYMTAB_OFFSET (objfile);

  free_pending_blocks ();
  back_to = make_cleanup (really_free_pendings, 0);

  init_minimal_symbol_collection ();
  make_cleanup_discard_minimal_symbols ();

  /* Read stabs data from executable file and define symbols. */

  read_dbx_symtab (objfile);

  /* Add the dynamic symbols.  */

  read_dbx_dynamic_symtab (objfile);

  /* Install any minimal symbols that have been collected as the current
     minimal symbols for this objfile. */

  install_minimal_symbols (objfile);

  do_cleanups (back_to);
}

/* Initialize anything that needs initializing when a completely new
   symbol file is specified (not just adding some symbols from another
   file, e.g. a shared library).  */

static void
dbx_new_init (struct objfile *ignore)
{
  stabsread_new_init ();
  buildsym_new_init ();
  init_header_files ();
}


/* dbx_symfile_init ()
   is the dbx-specific initialization routine for reading symbols.
   It is passed a struct objfile which contains, among other things,
   the BFD for the file whose symbols are being read, and a slot for a pointer
   to "private data" which we fill with goodies.

   We read the string table into malloc'd space and stash a pointer to it.

   Since BFD doesn't know how to read debug symbols in a format-independent
   way (and may never do so...), we have to do it ourselves.  We will never
   be called unless this is an a.out (or very similar) file. 
   FIXME, there should be a cleaner peephole into the BFD environment here.  */

#define DBX_STRINGTAB_SIZE_SIZE sizeof(long)	/* FIXME */

static void
dbx_symfile_init (struct objfile *objfile)
{
  int val;
  bfd *sym_bfd = objfile->obfd;
  char *name = bfd_get_filename (sym_bfd);
  asection *text_sect;
  unsigned char size_temp[DBX_STRINGTAB_SIZE_SIZE];

  /* Allocate struct to keep track of the symfile */
  objfile->sym_stab_info = (struct dbx_symfile_info *)
    xmmalloc (objfile->md, sizeof (struct dbx_symfile_info));
  memset (objfile->sym_stab_info, 0, sizeof (struct dbx_symfile_info));

  DBX_TEXT_SECTION (objfile) = bfd_get_section_by_name (sym_bfd, ".text");
  DBX_DATA_SECTION (objfile) = bfd_get_section_by_name (sym_bfd, ".data");
  DBX_BSS_SECTION (objfile) = bfd_get_section_by_name (sym_bfd, ".bss");

  /* FIXME POKING INSIDE BFD DATA STRUCTURES */
#define	STRING_TABLE_OFFSET	(sym_bfd->origin + obj_str_filepos (sym_bfd))
#define	SYMBOL_TABLE_OFFSET	(sym_bfd->origin + obj_sym_filepos (sym_bfd))

  /* FIXME POKING INSIDE BFD DATA STRUCTURES */

  DBX_SYMFILE_INFO (objfile)->stab_section_info = NULL;

  text_sect = bfd_get_section_by_name (sym_bfd, ".text");
  if (!text_sect)
    error ("Can't find .text section in symbol file");
  DBX_TEXT_ADDR (objfile) = bfd_section_vma (sym_bfd, text_sect);
  DBX_TEXT_SIZE (objfile) = bfd_section_size (sym_bfd, text_sect);

  DBX_SYMBOL_SIZE (objfile) = obj_symbol_entry_size (sym_bfd);
  DBX_SYMCOUNT (objfile) = bfd_get_symcount (sym_bfd);
  DBX_SYMTAB_OFFSET (objfile) = SYMBOL_TABLE_OFFSET;

  /* Read the string table and stash it away in the objfile_obstack.
     When we blow away the objfile the string table goes away as well.
     Note that gdb used to use the results of attempting to malloc the
     string table, based on the size it read, as a form of sanity check
     for botched byte swapping, on the theory that a byte swapped string
     table size would be so totally bogus that the malloc would fail.  Now
     that we put in on the objfile_obstack, we can't do this since gdb gets
     a fatal error (out of virtual memory) if the size is bogus.  We can
     however at least check to see if the size is less than the size of
     the size field itself, or larger than the size of the entire file.
     Note that all valid string tables have a size greater than zero, since
     the bytes used to hold the size are included in the count. */

  if (STRING_TABLE_OFFSET == 0)
    {
      /* It appears that with the existing bfd code, STRING_TABLE_OFFSET
         will never be zero, even when there is no string table.  This
         would appear to be a bug in bfd. */
      DBX_STRINGTAB_SIZE (objfile) = 0;
      DBX_STRINGTAB (objfile) = NULL;
    }
  else
    {
      val = bfd_seek (sym_bfd, STRING_TABLE_OFFSET, SEEK_SET);
      if (val < 0)
	perror_with_name (name);

      memset (size_temp, 0, sizeof (size_temp));
      val = bfd_bread (size_temp, sizeof (size_temp), sym_bfd);
      if (val < 0)
	{
	  perror_with_name (name);
	}
      else if (val == 0)
	{
	  /* With the existing bfd code, STRING_TABLE_OFFSET will be set to
	     EOF if there is no string table, and attempting to read the size
	     from EOF will read zero bytes. */
	  DBX_STRINGTAB_SIZE (objfile) = 0;
	  DBX_STRINGTAB (objfile) = NULL;
	}
      else
	{
	  /* Read some data that would appear to be the string table size.
	     If there really is a string table, then it is probably the right
	     size.  Byteswap if necessary and validate the size.  Note that
	     the minimum is DBX_STRINGTAB_SIZE_SIZE.  If we just read some
	     random data that happened to be at STRING_TABLE_OFFSET, because
	     bfd can't tell us there is no string table, the sanity checks may
	     or may not catch this. */
	  DBX_STRINGTAB_SIZE (objfile) = bfd_h_get_32 (sym_bfd, size_temp);

	  if (DBX_STRINGTAB_SIZE (objfile) < sizeof (size_temp)
	      || DBX_STRINGTAB_SIZE (objfile) > bfd_get_size (sym_bfd))
	    error ("ridiculous string table size (%d bytes).",
		   DBX_STRINGTAB_SIZE (objfile));

	  DBX_STRINGTAB (objfile) =
	    (char *) obstack_alloc (&objfile->objfile_obstack,
				    DBX_STRINGTAB_SIZE (objfile));
	  OBJSTAT (objfile, sz_strtab += DBX_STRINGTAB_SIZE (objfile));

	  /* Now read in the string table in one big gulp.  */

	  val = bfd_seek (sym_bfd, STRING_TABLE_OFFSET, SEEK_SET);
	  if (val < 0)
	    perror_with_name (name);
	  val = bfd_bread (DBX_STRINGTAB (objfile),
			   DBX_STRINGTAB_SIZE (objfile),
			   sym_bfd);
	  if (val != DBX_STRINGTAB_SIZE (objfile))
	    perror_with_name (name);
	}
    }
}

/* Perform any local cleanups required when we are done with a particular
   objfile.  I.E, we are in the process of discarding all symbol information
   for an objfile, freeing up all memory held for it, and unlinking the
   objfile struct from the global list of known objfiles. */

static void
dbx_symfile_finish (struct objfile *objfile)
{
  if (objfile->sym_stab_info != NULL)
    {
      if (HEADER_FILES (objfile) != NULL)
	{
	  int i = N_HEADER_FILES (objfile);
	  struct header_file *hfiles = HEADER_FILES (objfile);

	  while (--i >= 0)
	    {
	      xfree (hfiles[i].name);
	      xfree (hfiles[i].vector);
	    }
	  xfree (hfiles);
	}
      xmfree (objfile->md, objfile->sym_stab_info);
    }
  free_header_files ();
}


/* Buffer for reading the symbol table entries.  */
static struct external_nlist symbuf[4096];
static int symbuf_idx;
static int symbuf_end;

/* Name of last function encountered.  Used in Solaris to approximate
   object file boundaries.  */
static char *last_function_name;

/* The address in memory of the string table of the object file we are
   reading (which might not be the "main" object file, but might be a
   shared library or some other dynamically loaded thing).  This is
   set by read_dbx_symtab when building psymtabs, and by
   read_ofile_symtab when building symtabs, and is used only by
   next_symbol_text.  FIXME: If that is true, we don't need it when
   building psymtabs, right?  */
static char *stringtab_global;

/* These variables are used to control fill_symbuf when the stabs
   symbols are not contiguous (as may be the case when a COFF file is
   linked using --split-by-reloc).  */
static struct stab_section_list *symbuf_sections;
static unsigned int symbuf_left;
static unsigned int symbuf_read;

/* This variable stores a global stabs buffer, if we read stabs into
   memory in one chunk in order to process relocations.  */
static bfd_byte *stabs_data;

/* Refill the symbol table input buffer
   and set the variables that control fetching entries from it.
   Reports an error if no data available.
   This function can read past the end of the symbol table
   (into the string table) but this does no harm.  */

static void
fill_symbuf (bfd *sym_bfd)
{
  unsigned int count;
  int nbytes;

  if (stabs_data)
    {
      nbytes = sizeof (symbuf);
      if (nbytes > symbuf_left)
        nbytes = symbuf_left;
      memcpy (symbuf, stabs_data + symbuf_read, nbytes);
    }
  else if (symbuf_sections == NULL)
    {
      count = sizeof (symbuf);
      nbytes = bfd_bread (symbuf, count, sym_bfd);
    }
  else
    {
      if (symbuf_left <= 0)
	{
	  file_ptr filepos = symbuf_sections->section->filepos;
	  if (bfd_seek (sym_bfd, filepos, SEEK_SET) != 0)
	    perror_with_name (bfd_get_filename (sym_bfd));
	  symbuf_left = bfd_section_size (sym_bfd, symbuf_sections->section);
	  symbol_table_offset = filepos - symbuf_read;
	  symbuf_sections = symbuf_sections->next;
	}

      count = symbuf_left;
      if (count > sizeof (symbuf))
	count = sizeof (symbuf);
      nbytes = bfd_bread (symbuf, count, sym_bfd);
    }

  if (nbytes < 0)
    perror_with_name (bfd_get_filename (sym_bfd));
  else if (nbytes == 0)
    error ("Premature end of file reading symbol table");
  symbuf_end = nbytes / symbol_size;
  symbuf_idx = 0;
  symbuf_left -= nbytes;
  symbuf_read += nbytes;
}

static void
stabs_seek (int sym_offset)
{
  if (stabs_data)
    {
      symbuf_read += sym_offset;
      symbuf_left -= sym_offset;
    }
  else
    bfd_seek (symfile_bfd, sym_offset, SEEK_CUR);
}

#define INTERNALIZE_SYMBOL(intern, extern, abfd)			\
  {									\
    (intern).n_type = bfd_h_get_8 (abfd, (extern)->e_type);		\
    (intern).n_strx = bfd_h_get_32 (abfd, (extern)->e_strx);		\
    (intern).n_desc = bfd_h_get_16 (abfd, (extern)->e_desc);  		\
    if (bfd_get_sign_extend_vma (abfd))					\
      (intern).n_value = bfd_h_get_signed_32 (abfd, (extern)->e_value);	\
    else								\
      (intern).n_value = bfd_h_get_32 (abfd, (extern)->e_value);	\
  }

/* Invariant: The symbol pointed to by symbuf_idx is the first one
   that hasn't been swapped.  Swap the symbol at the same time
   that symbuf_idx is incremented.  */

/* dbx allows the text of a symbol name to be continued into the
   next symbol name!  When such a continuation is encountered
   (a \ at the end of the text of a name)
   call this function to get the continuation.  */

static char *
dbx_next_symbol_text (struct objfile *objfile)
{
  struct internal_nlist nlist;

  if (symbuf_idx == symbuf_end)
    fill_symbuf (symfile_bfd);

  symnum++;
  INTERNALIZE_SYMBOL (nlist, &symbuf[symbuf_idx], symfile_bfd);
  OBJSTAT (objfile, n_stabs++);

  symbuf_idx++;

  return nlist.n_strx + stringtab_global + file_string_table_offset;
}

/* Initialize the list of bincls to contain none and have some
   allocated.  */

static void
init_bincl_list (int number, struct objfile *objfile)
{
  bincls_allocated = number;
  next_bincl = bincl_list = (struct header_file_location *)
    xmmalloc (objfile->md, bincls_allocated * sizeof (struct header_file_location));
}

/* Add a bincl to the list.  */

static void
add_bincl_to_list (struct partial_symtab *pst, char *name, int instance)
{
  if (next_bincl >= bincl_list + bincls_allocated)
    {
      int offset = next_bincl - bincl_list;
      bincls_allocated *= 2;
      bincl_list = (struct header_file_location *)
	xmrealloc (pst->objfile->md, (char *) bincl_list,
		   bincls_allocated * sizeof (struct header_file_location));
      next_bincl = bincl_list + offset;
    }
  next_bincl->pst = pst;
  next_bincl->instance = instance;
  next_bincl++->name = name;
}

/* Given a name, value pair, find the corresponding
   bincl in the list.  Return the partial symtab associated
   with that header_file_location.  */

static struct partial_symtab *
find_corresponding_bincl_psymtab (char *name, int instance)
{
  struct header_file_location *bincl;

  for (bincl = bincl_list; bincl < next_bincl; bincl++)
    if (bincl->instance == instance
	&& strcmp (name, bincl->name) == 0)
      return bincl->pst;

  repeated_header_complaint (name, symnum);
  return (struct partial_symtab *) 0;
}

/* Free the storage allocated for the bincl list.  */

static void
free_bincl_list (struct objfile *objfile)
{
  xmfree (objfile->md, bincl_list);
  bincls_allocated = 0;
}

static void
do_free_bincl_list_cleanup (void *objfile)
{
  free_bincl_list (objfile);
}

static struct cleanup *
make_cleanup_free_bincl_list (struct objfile *objfile)
{
  return make_cleanup (do_free_bincl_list_cleanup, objfile);
}

/* Set namestring based on nlist.  If the string table index is invalid, 
   give a fake name, and print a single error message per symbol file read,
   rather than abort the symbol reading or flood the user with messages.  */

static char *
set_namestring (struct objfile *objfile, struct internal_nlist nlist)
{
  char *namestring;

  if (((unsigned) nlist.n_strx + file_string_table_offset) >=
      DBX_STRINGTAB_SIZE (objfile))
    {
      complaint (&symfile_complaints, "bad string table offset in symbol %d",
		 symnum);
      namestring = "<bad string table offset>";
    } 
  else
    namestring = nlist.n_strx + file_string_table_offset +
      DBX_STRINGTAB (objfile);
  return namestring;
}

/* Scan a SunOs dynamic symbol table for symbols of interest and
   add them to the minimal symbol table.  */

static void
read_dbx_dynamic_symtab (struct objfile *objfile)
{
  bfd *abfd = objfile->obfd;
  struct cleanup *back_to;
  int counter;
  long dynsym_size;
  long dynsym_count;
  asymbol **dynsyms;
  asymbol **symptr;
  arelent **relptr;
  long dynrel_size;
  long dynrel_count;
  arelent **dynrels;
  CORE_ADDR sym_value;
  char *name;

  /* Check that the symbol file has dynamic symbols that we know about.
     bfd_arch_unknown can happen if we are reading a sun3 symbol file
     on a sun4 host (and vice versa) and bfd is not configured
     --with-target=all.  This would trigger an assertion in bfd/sunos.c,
     so we ignore the dynamic symbols in this case.  */
  if (bfd_get_flavour (abfd) != bfd_target_aout_flavour
      || (bfd_get_file_flags (abfd) & DYNAMIC) == 0
      || bfd_get_arch (abfd) == bfd_arch_unknown)
    return;

  dynsym_size = bfd_get_dynamic_symtab_upper_bound (abfd);
  if (dynsym_size < 0)
    return;

  dynsyms = (asymbol **) xmalloc (dynsym_size);
  back_to = make_cleanup (xfree, dynsyms);

  dynsym_count = bfd_canonicalize_dynamic_symtab (abfd, dynsyms);
  if (dynsym_count < 0)
    {
      do_cleanups (back_to);
      return;
    }

  /* Enter dynamic symbols into the minimal symbol table
     if this is a stripped executable.  */
  if (bfd_get_symcount (abfd) <= 0)
    {
      symptr = dynsyms;
      for (counter = 0; counter < dynsym_count; counter++, symptr++)
	{
	  asymbol *sym = *symptr;
	  asection *sec;
	  int type;

	  sec = bfd_get_section (sym);

	  /* BFD symbols are section relative.  */
	  sym_value = sym->value + sec->vma;

	  if (bfd_get_section_flags (abfd, sec) & SEC_CODE)
	    {
	      sym_value += ANOFFSET (objfile->section_offsets, SECT_OFF_TEXT (objfile));
	      type = N_TEXT;
	    }
	  else if (bfd_get_section_flags (abfd, sec) & SEC_DATA)
	    {
	      sym_value += ANOFFSET (objfile->section_offsets, SECT_OFF_DATA (objfile));
	      type = N_DATA;
	    }
	  else if (bfd_get_section_flags (abfd, sec) & SEC_ALLOC)
	    {
	      sym_value += ANOFFSET (objfile->section_offsets, SECT_OFF_BSS (objfile));
	      type = N_BSS;
	    }
	  else
	    continue;

	  if (sym->flags & BSF_GLOBAL)
	    type |= N_EXT;

	  record_minimal_symbol ((char *) bfd_asymbol_name (sym), sym_value,
				 type, objfile);
	}
    }

  /* Symbols from shared libraries have a dynamic relocation entry
     that points to the associated slot in the procedure linkage table.
     We make a mininal symbol table entry with type mst_solib_trampoline
     at the address in the procedure linkage table.  */
  dynrel_size = bfd_get_dynamic_reloc_upper_bound (abfd);
  if (dynrel_size < 0)
    {
      do_cleanups (back_to);
      return;
    }

  dynrels = (arelent **) xmalloc (dynrel_size);
  make_cleanup (xfree, dynrels);

  dynrel_count = bfd_canonicalize_dynamic_reloc (abfd, dynrels, dynsyms);
  if (dynrel_count < 0)
    {
      do_cleanups (back_to);
      return;
    }

  for (counter = 0, relptr = dynrels;
       counter < dynrel_count;
       counter++, relptr++)
    {
      arelent *rel = *relptr;
      CORE_ADDR address =
      rel->address + ANOFFSET (objfile->section_offsets, SECT_OFF_DATA (objfile));

      switch (bfd_get_arch (abfd))
	{
	case bfd_arch_sparc:
	  if (rel->howto->type != RELOC_JMP_SLOT)
	    continue;
	  break;
	case bfd_arch_m68k:
	  /* `16' is the type BFD produces for a jump table relocation.  */
	  if (rel->howto->type != 16)
	    continue;

	  /* Adjust address in the jump table to point to
	     the start of the bsr instruction.  */
	  address -= 2;
	  break;
	default:
	  continue;
	}

      name = (char *) bfd_asymbol_name (*rel->sym_ptr_ptr);
      prim_record_minimal_symbol (name, address, mst_solib_trampoline,
				  objfile);
    }

  do_cleanups (back_to);
}

#ifdef SOFUN_ADDRESS_MAYBE_MISSING
static CORE_ADDR
find_stab_function_addr (char *namestring, char *filename,
			 struct objfile *objfile)
{
  struct minimal_symbol *msym;
  char *p;
  int n;

  p = strchr (namestring, ':');
  if (p == NULL)
    p = namestring;
  n = p - namestring;
  p = alloca (n + 2);
  strncpy (p, namestring, n);
  p[n] = 0;

  msym = lookup_minimal_symbol (p, filename, objfile);
  if (msym == NULL)
    {
      /* Sun Fortran appends an underscore to the minimal symbol name,
         try again with an appended underscore if the minimal symbol
         was not found.  */
      p[n] = '_';
      p[n + 1] = 0;
      msym = lookup_minimal_symbol (p, filename, objfile);
    }

  if (msym == NULL && filename != NULL)
    {
      /* Try again without the filename. */
      p[n] = 0;
      msym = lookup_minimal_symbol (p, NULL, objfile);
    }
  if (msym == NULL && filename != NULL)
    {
      /* And try again for Sun Fortran, but without the filename. */
      p[n] = '_';
      p[n + 1] = 0;
      msym = lookup_minimal_symbol (p, NULL, objfile);
    }

  return msym == NULL ? 0 : SYMBOL_VALUE_ADDRESS (msym);
}
#endif /* SOFUN_ADDRESS_MAYBE_MISSING */

static void
function_outside_compilation_unit_complaint (const char *arg1)
{
  complaint (&symfile_complaints,
	     "function `%s' appears to be defined outside of all compilation units",
	     arg1);
}

/* Setup partial_symtab's describing each source file for which
   debugging information is available. */

static void
read_dbx_symtab (struct objfile *objfile)
{
  struct external_nlist *bufp = 0;	/* =0 avoids gcc -Wall glitch */
  struct internal_nlist nlist;
  CORE_ADDR text_addr;
  int text_size;

  char *namestring;
  int nsl;
  int past_first_source_file = 0;
  CORE_ADDR last_o_file_start = 0;
  CORE_ADDR last_function_start = 0;
  struct cleanup *back_to;
  bfd *abfd;
  int textlow_not_set;
  int data_sect_index;

  /* Current partial symtab */
  struct partial_symtab *pst;

  /* List of current psymtab's include files */
  char **psymtab_include_list;
  int includes_allocated;
  int includes_used;

  /* Index within current psymtab dependency list */
  struct partial_symtab **dependency_list;
  int dependencies_used, dependencies_allocated;

  text_addr = DBX_TEXT_ADDR (objfile);
  text_size = DBX_TEXT_SIZE (objfile);

  /* FIXME.  We probably want to change stringtab_global rather than add this
     while processing every symbol entry.  FIXME.  */
  file_string_table_offset = 0;
  next_file_string_table_offset = 0;

  stringtab_global = DBX_STRINGTAB (objfile);

  pst = (struct partial_symtab *) 0;

  includes_allocated = 30;
  includes_used = 0;
  psymtab_include_list = (char **) alloca (includes_allocated *
					   sizeof (char *));

  dependencies_allocated = 30;
  dependencies_used = 0;
  dependency_list =
    (struct partial_symtab **) alloca (dependencies_allocated *
				       sizeof (struct partial_symtab *));

  /* Init bincl list */
  init_bincl_list (20, objfile);
  back_to = make_cleanup_free_bincl_list (objfile);

  last_source_file = NULL;

  lowest_text_address = (CORE_ADDR) -1;

  symfile_bfd = objfile->obfd;	/* For next_text_symbol */
  abfd = objfile->obfd;
  symbuf_end = symbuf_idx = 0;
  next_symbol_text_func = dbx_next_symbol_text;
  textlow_not_set = 1;
  has_line_numbers = 0;

  /* FIXME: jimb/2003-09-12: We don't apply the right section's offset
     to global and static variables.  The stab for a global or static
     variable doesn't give us any indication of which section it's in,
     so we can't tell immediately which offset in
     objfile->section_offsets we should apply to the variable's
     address.

     We could certainly find out which section contains the variable
     by looking up the variable's unrelocated address with
     find_pc_section, but that would be expensive; this is the
     function that constructs the partial symbol tables by examining
     every symbol in the entire executable, and it's
     performance-critical.  So that expense would not be welcome.  I'm
     not sure what to do about this at the moment.

     What we have done for years is to simply assume that the .data
     section's offset is appropriate for all global and static
     variables.  Recently, this was expanded to fall back to the .bss
     section's offset if there is no .data section, and then to the
     .rodata section's offset.  */
  data_sect_index = objfile->sect_index_data;
  if (data_sect_index == -1)
    data_sect_index = SECT_OFF_BSS (objfile);
  if (data_sect_index == -1)
    data_sect_index = SECT_OFF_RODATA (objfile);

  /* If data_sect_index is still -1, that's okay.  It's perfectly fine
     for the file to have no .data, no .bss, and no .text at all, if
     it also has no global or static variables.  If it does, we will
     get an internal error from an ANOFFSET macro below when we try to
     use data_sect_index.  */

  for (symnum = 0; symnum < DBX_SYMCOUNT (objfile); symnum++)
    {
      /* Get the symbol for this run and pull out some info */
      QUIT;			/* allow this to be interruptable */
      if (symbuf_idx == symbuf_end)
	fill_symbuf (abfd);
      bufp = &symbuf[symbuf_idx++];

      /*
       * Special case to speed up readin.
       */
      if (bfd_h_get_8 (abfd, bufp->e_type) == N_SLINE)
	{
	  has_line_numbers = 1;
	  continue;
	}

      INTERNALIZE_SYMBOL (nlist, bufp, abfd);
      OBJSTAT (objfile, n_stabs++);

      /* Ok.  There is a lot of code duplicated in the rest of this
         switch statement (for efficiency reasons).  Since I don't
         like duplicating code, I will do my penance here, and
         describe the code which is duplicated:

         *) The assignment to namestring.
         *) The call to strchr.
         *) The addition of a partial symbol the the two partial
         symbol lists.  This last is a large section of code, so
         I've imbedded it in the following macro.
      */

      switch (nlist.n_type)
	{
	  char *p;
	  /*
	   * Standard, external, non-debugger, symbols
	   */

	  case N_TEXT | N_EXT:
	  case N_NBTEXT | N_EXT:
	  nlist.n_value += ANOFFSET (objfile->section_offsets, SECT_OFF_TEXT (objfile));
	  goto record_it;

	  case N_DATA | N_EXT:
	  case N_NBDATA | N_EXT:
	  nlist.n_value += ANOFFSET (objfile->section_offsets, SECT_OFF_DATA (objfile));
	  goto record_it;

	  case N_BSS:
	  case N_BSS | N_EXT:
	  case N_NBBSS | N_EXT:
	  case N_SETV | N_EXT:		/* FIXME, is this in BSS? */
	  nlist.n_value += ANOFFSET (objfile->section_offsets, SECT_OFF_BSS (objfile));
	  goto record_it;

	  case N_ABS | N_EXT:
	  record_it:
	  namestring = set_namestring (objfile, nlist);

	  bss_ext_symbol:
	  record_minimal_symbol (namestring, nlist.n_value,
				 nlist.n_type, objfile);	/* Always */
	  continue;

	  /* Standard, local, non-debugger, symbols */

	  case N_NBTEXT:

	  /* We need to be able to deal with both N_FN or N_TEXT,
	     because we have no way of knowing whether the sys-supplied ld
	     or GNU ld was used to make the executable.  Sequents throw
	     in another wrinkle -- they renumbered N_FN.  */

	  case N_FN:
	  case N_FN_SEQ:
	  case N_TEXT:
	  nlist.n_value += ANOFFSET (objfile->section_offsets, SECT_OFF_TEXT (objfile));
	  namestring = set_namestring (objfile, nlist);

	  if ((namestring[0] == '-' && namestring[1] == 'l')
	      || (namestring[(nsl = strlen (namestring)) - 1] == 'o'
		  && namestring[nsl - 2] == '.'))
	  {
	    if (objfile->ei.entry_point < nlist.n_value &&
		objfile->ei.entry_point >= last_o_file_start)
	      {
		objfile->ei.deprecated_entry_file_lowpc = last_o_file_start;
		objfile->ei.deprecated_entry_file_highpc = nlist.n_value;
	      }
	    if (past_first_source_file && pst
		/* The gould NP1 uses low values for .o and -l symbols
		   which are not the address.  */
		&& nlist.n_value >= pst->textlow)
	      {
		end_psymtab (pst, psymtab_include_list, includes_used,
			     symnum * symbol_size,
			     nlist.n_value > pst->texthigh
			     ? nlist.n_value : pst->texthigh,
			     dependency_list, dependencies_used, textlow_not_set);
		pst = (struct partial_symtab *) 0;
		includes_used = 0;
		dependencies_used = 0;
	      }
	    else
	      past_first_source_file = 1;
	    last_o_file_start = nlist.n_value;
	  }
	  else
	  goto record_it;
	  continue;

	  case N_DATA:
	  nlist.n_value += ANOFFSET (objfile->section_offsets, SECT_OFF_DATA (objfile));
	  goto record_it;

	  case N_UNDF | N_EXT:
	  if (nlist.n_value != 0)
	  {
	    /* This is a "Fortran COMMON" symbol.  See if the target
	       environment knows where it has been relocated to.  */

	    CORE_ADDR reladdr;

	    namestring = set_namestring (objfile, nlist);
	    if (target_lookup_symbol (namestring, &reladdr))
	      {
		continue;		/* Error in lookup; ignore symbol for now.  */
	      }
	    nlist.n_type ^= (N_BSS ^ N_UNDF);	/* Define it as a bss-symbol */
	    nlist.n_value = reladdr;
	    goto bss_ext_symbol;
	  }
	  continue;			/* Just undefined, not COMMON */

	  case N_UNDF:
	  if (processing_acc_compilation && nlist.n_strx == 1)
	  {
	    /* Deal with relative offsets in the string table
	       used in ELF+STAB under Solaris.  If we want to use the
	       n_strx field, which contains the name of the file,
	       we must adjust file_string_table_offset *before* calling
	       set_namestring().  */
	    past_first_source_file = 1;
	    file_string_table_offset = next_file_string_table_offset;
	    next_file_string_table_offset =
	      file_string_table_offset + nlist.n_value;
	    if (next_file_string_table_offset < file_string_table_offset)
	      error ("string table offset backs up at %d", symnum);
	    /* FIXME -- replace error() with complaint.  */
	    continue;
	  }
	  continue;

	  /* Lots of symbol types we can just ignore.  */

	  case N_ABS:
	  case N_NBDATA:
	  case N_NBBSS:
	  continue;

	  /* Keep going . . . */

	  /*
	   * Special symbol types for GNU
	   */
	  case N_INDR:
	  case N_INDR | N_EXT:
	  case N_SETA:
	  case N_SETA | N_EXT:
	  case N_SETT:
	  case N_SETT | N_EXT:
	  case N_SETD:
	  case N_SETD | N_EXT:
	  case N_SETB:
	  case N_SETB | N_EXT:
	  case N_SETV:
	  continue;

	  /*
	   * Debugger symbols
	   */

	  case N_SO:
	  {
	    CORE_ADDR valu;
	    static int prev_so_symnum = -10;
	    static int first_so_symnum;
	    char *p;
	    int prev_textlow_not_set;

	    valu = nlist.n_value + ANOFFSET (objfile->section_offsets, SECT_OFF_TEXT (objfile));

	    prev_textlow_not_set = textlow_not_set;

#ifdef SOFUN_ADDRESS_MAYBE_MISSING
	    /* A zero value is probably an indication for the SunPRO 3.0
	       compiler. end_psymtab explicitly tests for zero, so
	       don't relocate it.  */

	    if (nlist.n_value == 0)
	      {
		textlow_not_set = 1;
		valu = 0;
	      }
	    else
	      textlow_not_set = 0;
#else
	    textlow_not_set = 0;
#endif
	    past_first_source_file = 1;

	    if (prev_so_symnum != symnum - 1)
	      {			/* Here if prev stab wasn't N_SO */
		first_so_symnum = symnum;

		if (pst)
		  {
		    end_psymtab (pst, psymtab_include_list, includes_used,
				 symnum * symbol_size,
				 valu > pst->texthigh ? valu : pst->texthigh,
				 dependency_list, dependencies_used,
				 prev_textlow_not_set);
		    pst = (struct partial_symtab *) 0;
		    includes_used = 0;
		    dependencies_used = 0;
		  }
	      }

	    prev_so_symnum = symnum;

	    /* End the current partial symtab and start a new one */

	    namestring = set_namestring (objfile, nlist);

	    /* Null name means end of .o file.  Don't start a new one. */
	    if (*namestring == '\000')
	      continue;

	    /* Some compilers (including gcc) emit a pair of initial N_SOs.
	       The first one is a directory name; the second the file name.
	       If pst exists, is empty, and has a filename ending in '/',
	       we assume the previous N_SO was a directory name. */

	    p = strrchr (namestring, '/');
	    if (p && *(p + 1) == '\000')
	      continue;		/* Simply ignore directory name SOs */

	    /* Some other compilers (C++ ones in particular) emit useless
	       SOs for non-existant .c files.  We ignore all subsequent SOs that
	       immediately follow the first.  */

	    if (!pst)
	      pst = start_psymtab (objfile,
				   namestring, valu,
				   first_so_symnum * symbol_size,
				   objfile->global_psymbols.next,
				   objfile->static_psymbols.next);
	    continue;
	  }

	  case N_BINCL:
	  {
	    enum language tmp_language;
	    /* Add this bincl to the bincl_list for future EXCLs.  No
	       need to save the string; it'll be around until
	       read_dbx_symtab function returns */

	    namestring = set_namestring (objfile, nlist);
	    tmp_language = deduce_language_from_filename (namestring);

	    /* Only change the psymtab's language if we've learned
	       something useful (eg. tmp_language is not language_unknown).
	       In addition, to match what start_subfile does, never change
	       from C++ to C.  */
	    if (tmp_language != language_unknown
		&& (tmp_language != language_c
		    || psymtab_language != language_cplus))
	    psymtab_language = tmp_language;

	    if (pst == NULL)
	    {
	      /* FIXME: we should not get here without a PST to work on.
		 Attempt to recover.  */
	      complaint (&symfile_complaints,
			 "N_BINCL %s not in entries for any file, at symtab pos %d",
			 namestring, symnum);
	      continue;
	    }
	    add_bincl_to_list (pst, namestring, nlist.n_value);

	    /* Mark down an include file in the current psymtab */

	    goto record_include_file;
	  }

	  case N_SOL:
	  {
	    enum language tmp_language;
	    /* Mark down an include file in the current psymtab */

	    namestring = set_namestring (objfile, nlist);
	    tmp_language = deduce_language_from_filename (namestring);

	    /* Only change the psymtab's language if we've learned
	       something useful (eg. tmp_language is not language_unknown).
	       In addition, to match what start_subfile does, never change
	       from C++ to C.  */
	    if (tmp_language != language_unknown
		&& (tmp_language != language_c
		    || psymtab_language != language_cplus))
	    psymtab_language = tmp_language;

	    /* In C++, one may expect the same filename to come round many
	       times, when code is coming alternately from the main file
	       and from inline functions in other files. So I check to see
	       if this is a file we've seen before -- either the main
	       source file, or a previously included file.

	       This seems to be a lot of time to be spending on N_SOL, but
	       things like "break c-exp.y:435" need to work (I
	       suppose the psymtab_include_list could be hashed or put
	       in a binary tree, if profiling shows this is a major hog).  */
	    if (pst && strcmp (namestring, pst->filename) == 0)
	    continue;
	    {
	      int i;
	      for (i = 0; i < includes_used; i++)
		if (strcmp (namestring, psymtab_include_list[i]) == 0)
		  {
		    i = -1;
		    break;
		  }
	      if (i == -1)
		continue;
	    }

	    record_include_file:

	    psymtab_include_list[includes_used++] = namestring;
	    if (includes_used >= includes_allocated)
	    {
	      char **orig = psymtab_include_list;

	      psymtab_include_list = (char **)
		alloca ((includes_allocated *= 2) *
			sizeof (char *));
	      memcpy (psymtab_include_list, orig,
		      includes_used * sizeof (char *));
	    }
	    continue;
	  }
	  case N_LSYM:			/* Typedef or automatic variable. */
	  case N_STSYM:		/* Data seg var -- static  */
	  case N_LCSYM:		/* BSS      "  */
	  case N_ROSYM:		/* Read-only data seg var -- static.  */
	  case N_NBSTS:		/* Gould nobase.  */
	  case N_NBLCS:		/* symbols.  */
	  case N_FUN:
	  case N_GSYM:			/* Global (extern) variable; can be
					   data or bss (sigh FIXME).  */

	  /* Following may probably be ignored; I'll leave them here
	     for now (until I do Pascal and Modula 2 extensions).  */

	  case N_PC:			/* I may or may not need this; I
					   suspect not.  */
	  case N_M2C:			/* I suspect that I can ignore this here. */
	  case N_SCOPE:		/* Same.   */

	  namestring = set_namestring (objfile, nlist);

	  /* See if this is an end of function stab.  */
	  if (pst && nlist.n_type == N_FUN && *namestring == '\000')
	  {
	    CORE_ADDR valu;

	    /* It's value is the size (in bytes) of the function for
	       function relative stabs, or the address of the function's
	       end for old style stabs.  */
	    valu = nlist.n_value + last_function_start;
	    if (pst->texthigh == 0 || valu > pst->texthigh)
	      pst->texthigh = valu;
	    break;
	  }

	  p = (char *) strchr (namestring, ':');
	  if (!p)
	  continue;			/* Not a debugging symbol.   */



	  /* Main processing section for debugging symbols which
	     the initial read through the symbol tables needs to worry
	     about.  If we reach this point, the symbol which we are
	     considering is definitely one we are interested in.
	     p must also contain the (valid) index into the namestring
	     which indicates the debugging type symbol.  */

	  switch (p[1])
	  {
	  case 'S':
	    nlist.n_value += ANOFFSET (objfile->section_offsets, data_sect_index);
#ifdef STATIC_TRANSFORM_NAME
	    namestring = STATIC_TRANSFORM_NAME (namestring);
#endif
	    add_psymbol_to_list (namestring, p - namestring,
				 VAR_DOMAIN, LOC_STATIC,
				 &objfile->static_psymbols,
				 0, nlist.n_value,
				 psymtab_language, objfile);
	    continue;
	  case 'G':
	    nlist.n_value += ANOFFSET (objfile->section_offsets, data_sect_index);
	    /* The addresses in these entries are reported to be
	       wrong.  See the code that reads 'G's for symtabs. */
	    add_psymbol_to_list (namestring, p - namestring,
				 VAR_DOMAIN, LOC_STATIC,
				 &objfile->global_psymbols,
				 0, nlist.n_value,
				 psymtab_language, objfile);
	    continue;

	  case 'T':
	    /* When a 'T' entry is defining an anonymous enum, it
	       may have a name which is the empty string, or a
	       single space.  Since they're not really defining a
	       symbol, those shouldn't go in the partial symbol
	       table.  We do pick up the elements of such enums at
	       'check_enum:', below.  */
	    if (p >= namestring + 2
		|| (p == namestring + 1
		    && namestring[0] != ' '))
	      {
		add_psymbol_to_list (namestring, p - namestring,
				     STRUCT_DOMAIN, LOC_TYPEDEF,
				     &objfile->static_psymbols,
				     nlist.n_value, 0,
				     psymtab_language, objfile);
		if (p[2] == 't')
		  {
		    /* Also a typedef with the same name.  */
		    add_psymbol_to_list (namestring, p - namestring,
					 VAR_DOMAIN, LOC_TYPEDEF,
					 &objfile->static_psymbols,
					 nlist.n_value, 0,
					 psymtab_language, objfile);
		    p += 1;
		  }
	      }
	    goto check_enum;
	  case 't':
	    if (p != namestring)	/* a name is there, not just :T... */
	      {
		add_psymbol_to_list (namestring, p - namestring,
				     VAR_DOMAIN, LOC_TYPEDEF,
				     &objfile->static_psymbols,
				     nlist.n_value, 0,
				     psymtab_language, objfile);
	      }
	  check_enum:
	    /* If this is an enumerated type, we need to
	       add all the enum constants to the partial symbol
	       table.  This does not cover enums without names, e.g.
	       "enum {a, b} c;" in C, but fortunately those are
	       rare.  There is no way for GDB to find those from the
	       enum type without spending too much time on it.  Thus
	       to solve this problem, the compiler needs to put out the
	       enum in a nameless type.  GCC2 does this.  */

	    /* We are looking for something of the form
	       <name> ":" ("t" | "T") [<number> "="] "e"
	       {<constant> ":" <value> ","} ";".  */

	    /* Skip over the colon and the 't' or 'T'.  */
	    p += 2;
	    /* This type may be given a number.  Also, numbers can come
	       in pairs like (0,26).  Skip over it.  */
	    while ((*p >= '0' && *p <= '9')
		   || *p == '(' || *p == ',' || *p == ')'
		   || *p == '=')
	      p++;

	    if (*p++ == 'e')
	      {
		/* The aix4 compiler emits extra crud before the members.  */
		if (*p == '-')
		  {
		    /* Skip over the type (?).  */
		    while (*p != ':')
		      p++;

		    /* Skip over the colon.  */
		    p++;
		  }

		/* We have found an enumerated type.  */
		/* According to comments in read_enum_type
		   a comma could end it instead of a semicolon.
		   I don't know where that happens.
		   Accept either.  */
		while (*p && *p != ';' && *p != ',')
		  {
		    char *q;

		    /* Check for and handle cretinous dbx symbol name
		       continuation!  */
		    if (*p == '\\' || (*p == '?' && p[1] == '\0'))
		      p = next_symbol_text (objfile);

		    /* Point to the character after the name
		       of the enum constant.  */
		    for (q = p; *q && *q != ':'; q++)
		      ;
		    /* Note that the value doesn't matter for
		       enum constants in psymtabs, just in symtabs.  */
		    add_psymbol_to_list (p, q - p,
					 VAR_DOMAIN, LOC_CONST,
					 &objfile->static_psymbols, 0,
					 0, psymtab_language, objfile);
		    /* Point past the name.  */
		    p = q;
		    /* Skip over the value.  */
		    while (*p && *p != ',')
		      p++;
		    /* Advance past the comma.  */
		    if (*p)
		      p++;
		  }
	      }
	    continue;
	  case 'c':
	    /* Constant, e.g. from "const" in Pascal.  */
	    add_psymbol_to_list (namestring, p - namestring,
				 VAR_DOMAIN, LOC_CONST,
				 &objfile->static_psymbols, nlist.n_value,
				 0, psymtab_language, objfile);
	    continue;

	  case 'f':
	    if (! pst)
	      {
		int name_len = p - namestring;
		char *name = xmalloc (name_len + 1);
		memcpy (name, namestring, name_len);
		name[name_len] = '\0';
		function_outside_compilation_unit_complaint (name);
		xfree (name);
	      }
	    nlist.n_value += ANOFFSET (objfile->section_offsets, SECT_OFF_TEXT (objfile));
	    /* Kludges for ELF/STABS with Sun ACC */
	    last_function_name = namestring;
#ifdef SOFUN_ADDRESS_MAYBE_MISSING
	    /* Do not fix textlow==0 for .o or NLM files, as 0 is a legit
	       value for the bottom of the text seg in those cases. */
	    if (nlist.n_value == ANOFFSET (objfile->section_offsets, 
					   SECT_OFF_TEXT (objfile)))
	      {
		CORE_ADDR minsym_valu = 
		  find_stab_function_addr (namestring, pst->filename, objfile);
		/* find_stab_function_addr will return 0 if the minimal
		   symbol wasn't found.  (Unfortunately, this might also
		   be a valid address.)  Anyway, if it *does* return 0,
		   it is likely that the value was set correctly to begin
		   with... */
		if (minsym_valu != 0)
		  nlist.n_value = minsym_valu;
	      }
	    if (pst && textlow_not_set)
	      {
		pst->textlow = nlist.n_value;
		textlow_not_set = 0;
	      }
#endif
	    /* End kludge.  */

	    /* Keep track of the start of the last function so we
	       can handle end of function symbols.  */
	    last_function_start = nlist.n_value;

	    /* In reordered executables this function may lie outside
	       the bounds created by N_SO symbols.  If that's the case
	       use the address of this function as the low bound for
	       the partial symbol table.  */
	    if (pst
		&& (textlow_not_set
		    || (nlist.n_value < pst->textlow
			&& (nlist.n_value
			    != ANOFFSET (objfile->section_offsets,
					 SECT_OFF_TEXT (objfile))))))
	      {
		pst->textlow = nlist.n_value;
		textlow_not_set = 0;
	      }
	    add_psymbol_to_list (namestring, p - namestring,
				 VAR_DOMAIN, LOC_BLOCK,
				 &objfile->static_psymbols,
				 0, nlist.n_value,
				 psymtab_language, objfile);
	    continue;

	    /* Global functions were ignored here, but now they
	       are put into the global psymtab like one would expect.
	       They're also in the minimal symbol table.  */
	  case 'F':
	    if (! pst)
	      {
		int name_len = p - namestring;
		char *name = xmalloc (name_len + 1);
		memcpy (name, namestring, name_len);
		name[name_len] = '\0';
		function_outside_compilation_unit_complaint (name);
		xfree (name);
	      }
	    nlist.n_value += ANOFFSET (objfile->section_offsets, SECT_OFF_TEXT (objfile));
	    /* Kludges for ELF/STABS with Sun ACC */
	    last_function_name = namestring;
#ifdef SOFUN_ADDRESS_MAYBE_MISSING
	    /* Do not fix textlow==0 for .o or NLM files, as 0 is a legit
	       value for the bottom of the text seg in those cases. */
	    if (nlist.n_value == ANOFFSET (objfile->section_offsets, 
					   SECT_OFF_TEXT (objfile)))
	      {
		CORE_ADDR minsym_valu = 
		  find_stab_function_addr (namestring, pst->filename, objfile);
		/* find_stab_function_addr will return 0 if the minimal
		   symbol wasn't found.  (Unfortunately, this might also
		   be a valid address.)  Anyway, if it *does* return 0,
		   it is likely that the value was set correctly to begin
		   with... */
		if (minsym_valu != 0)
		  nlist.n_value = minsym_valu;
	      }
	    if (pst && textlow_not_set)
	      {
		pst->textlow = nlist.n_value;
		textlow_not_set = 0;
	      }
#endif
	    /* End kludge.  */

	    /* Keep track of the start of the last function so we
	       can handle end of function symbols.  */
	    last_function_start = nlist.n_value;

	    /* In reordered executables this function may lie outside
	       the bounds created by N_SO symbols.  If that's the case
	       use the address of this function as the low bound for
	       the partial symbol table.  */
	    if (pst
		&& (textlow_not_set
		    || (nlist.n_value < pst->textlow
			&& (nlist.n_value
			    != ANOFFSET (objfile->section_offsets,
					 SECT_OFF_TEXT (objfile))))))
	      {
		pst->textlow = nlist.n_value;
		textlow_not_set = 0;
	      }
	    add_psymbol_to_list (namestring, p - namestring,
				 VAR_DOMAIN, LOC_BLOCK,
				 &objfile->global_psymbols,
				 0, nlist.n_value,
				 psymtab_language, objfile);
	    continue;

	    /* Two things show up here (hopefully); static symbols of
	       local scope (static used inside braces) or extensions
	       of structure symbols.  We can ignore both.  */
	  case 'V':
	  case '(':
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
	  case '-':
	  case '#':		/* for symbol identification (used in live ranges) */
	    continue;

	  case ':':
	    /* It is a C++ nested symbol.  We don't need to record it
	       (I don't think); if we try to look up foo::bar::baz,
	       then symbols for the symtab containing foo should get
	       read in, I think.  */
	    /* Someone says sun cc puts out symbols like
	       /foo/baz/maclib::/usr/local/bin/maclib,
	       which would get here with a symbol type of ':'.  */
	    continue;

	  default:
	    /* Unexpected symbol descriptor.  The second and subsequent stabs
	       of a continued stab can show up here.  The question is
	       whether they ever can mimic a normal stab--it would be
	       nice if not, since we certainly don't want to spend the
	       time searching to the end of every string looking for
	       a backslash.  */

	    complaint (&symfile_complaints, "unknown symbol descriptor `%c'",
		       p[1]);

	    /* Ignore it; perhaps it is an extension that we don't
	       know about.  */
	    continue;
	  }

	  case N_EXCL:

	  namestring = set_namestring (objfile, nlist);

	  /* Find the corresponding bincl and mark that psymtab on the
	     psymtab dependency list */
	  {
	    struct partial_symtab *needed_pst =
	      find_corresponding_bincl_psymtab (namestring, nlist.n_value);

	    /* If this include file was defined earlier in this file,
	       leave it alone.  */
	    if (needed_pst == pst)
	      continue;

	    if (needed_pst)
	      {
		int i;
		int found = 0;

		for (i = 0; i < dependencies_used; i++)
		  if (dependency_list[i] == needed_pst)
		    {
		      found = 1;
		      break;
		    }

		/* If it's already in the list, skip the rest.  */
		if (found)
		  continue;

		dependency_list[dependencies_used++] = needed_pst;
		if (dependencies_used >= dependencies_allocated)
		  {
		    struct partial_symtab **orig = dependency_list;
		    dependency_list =
		      (struct partial_symtab **)
		      alloca ((dependencies_allocated *= 2)
			      * sizeof (struct partial_symtab *));
		    memcpy (dependency_list, orig,
			    (dependencies_used
			     * sizeof (struct partial_symtab *)));
#ifdef DEBUG_INFO
		    fprintf_unfiltered (gdb_stderr, "Had to reallocate dependency list.\n");
		    fprintf_unfiltered (gdb_stderr, "New dependencies allocated: %d\n",
					dependencies_allocated);
#endif
		  }
	      }
	  }
	  continue;

	  case N_ENDM:
#ifdef SOFUN_ADDRESS_MAYBE_MISSING
	  /* Solaris 2 end of module, finish current partial symbol table.
	     end_psymtab will set pst->texthigh to the proper value, which
	     is necessary if a module compiled without debugging info
	     follows this module.  */
	  if (pst)
	  {
	    end_psymtab (pst, psymtab_include_list, includes_used,
			 symnum * symbol_size,
			 (CORE_ADDR) 0,
			 dependency_list, dependencies_used, textlow_not_set);
	    pst = (struct partial_symtab *) 0;
	    includes_used = 0;
	    dependencies_used = 0;
	  }
#endif
	  continue;

	  case N_RBRAC:
#ifdef HANDLE_RBRAC
	  HANDLE_RBRAC (nlist.n_value);
	  continue;
#endif
	  case N_EINCL:
	  case N_DSLINE:
	  case N_BSLINE:
	  case N_SSYM:			/* Claim: Structure or union element.
					   Hopefully, I can ignore this.  */
	  case N_ENTRY:		/* Alternate entry point; can ignore. */
	  case N_MAIN:			/* Can definitely ignore this.   */
	  case N_CATCH:		/* These are GNU C++ extensions */
	  case N_EHDECL:		/* that can safely be ignored here. */
	  case N_LENG:
	  case N_BCOMM:
	  case N_ECOMM:
	  case N_ECOML:
	  case N_FNAME:
	  case N_SLINE:
	  case N_RSYM:
	  case N_PSYM:
	  case N_LBRAC:
	  case N_NSYMS:		/* Ultrix 4.0: symbol count */
	  case N_DEFD:			/* GNU Modula-2 */
	  case N_ALIAS:		/* SunPro F77: alias name, ignore for now.  */

	  case N_OBJ:			/* useless types from Solaris */
	  case N_OPT:
	  case N_PATCH:
	  /* These symbols aren't interesting; don't worry about them */

	  continue;

	  default:
	  /* If we haven't found it yet, ignore it.  It's probably some
	     new type we don't know about yet.  */
	  unknown_symtype_complaint (local_hex_string (nlist.n_type));
	  continue;
	}
    }

  /* If there's stuff to be cleaned up, clean it up.  */
  if (DBX_SYMCOUNT (objfile) > 0	/* We have some syms */
      /*FIXME, does this have a bug at start address 0? */
      && last_o_file_start
      && objfile->ei.entry_point < nlist.n_value
      && objfile->ei.entry_point >= last_o_file_start)
    {
      objfile->ei.deprecated_entry_file_lowpc = last_o_file_start;
      objfile->ei.deprecated_entry_file_highpc = nlist.n_value;
    }

  if (pst)
    {
      /* Don't set pst->texthigh lower than it already is.  */
      CORE_ADDR text_end =
	(lowest_text_address == (CORE_ADDR) -1
	 ? (text_addr + ANOFFSET (objfile->section_offsets, SECT_OFF_TEXT (objfile)))
	 : lowest_text_address)
	+ text_size;

      end_psymtab (pst, psymtab_include_list, includes_used,
		   symnum * symbol_size,
		   text_end > pst->texthigh ? text_end : pst->texthigh,
		   dependency_list, dependencies_used, textlow_not_set);
    }

  do_cleanups (back_to);
}

/* Allocate and partially fill a partial symtab.  It will be
   completely filled at the end of the symbol list.

   SYMFILE_NAME is the name of the symbol-file we are reading from, and ADDR
   is the address relative to which its symbols are (incremental) or 0
   (normal). */


static struct partial_symtab *
start_psymtab (struct objfile *objfile, char *filename, CORE_ADDR textlow,
	       int ldsymoff, struct partial_symbol **global_syms,
	       struct partial_symbol **static_syms)
{
  struct partial_symtab *result =
  start_psymtab_common (objfile, objfile->section_offsets,
			filename, textlow, global_syms, static_syms);

  result->read_symtab_private = (char *)
    obstack_alloc (&objfile->objfile_obstack, sizeof (struct symloc));
  LDSYMOFF (result) = ldsymoff;
  result->read_symtab = dbx_psymtab_to_symtab;
  SYMBOL_SIZE (result) = symbol_size;
  SYMBOL_OFFSET (result) = symbol_table_offset;
  STRING_OFFSET (result) = string_table_offset;
  FILE_STRING_OFFSET (result) = file_string_table_offset;

  /* If we're handling an ELF file, drag some section-relocation info
     for this source file out of the ELF symbol table, to compensate for
     Sun brain death.  This replaces the section_offsets in this psymtab,
     if successful.  */
  elfstab_offset_sections (objfile, result);

  /* Deduce the source language from the filename for this psymtab. */
  psymtab_language = deduce_language_from_filename (filename);

  return result;
}

/* Close off the current usage of PST.  
   Returns PST or NULL if the partial symtab was empty and thrown away.

   FIXME:  List variables and peculiarities of same.  */

struct partial_symtab *
end_psymtab (struct partial_symtab *pst, char **include_list, int num_includes,
	     int capping_symbol_offset, CORE_ADDR capping_text,
	     struct partial_symtab **dependency_list, int number_dependencies,
	     int textlow_not_set)
{
  int i;
  struct objfile *objfile = pst->objfile;

  if (capping_symbol_offset != -1)
    LDSYMLEN (pst) = capping_symbol_offset - LDSYMOFF (pst);
  pst->texthigh = capping_text;

#ifdef SOFUN_ADDRESS_MAYBE_MISSING
  /* Under Solaris, the N_SO symbols always have a value of 0,
     instead of the usual address of the .o file.  Therefore,
     we have to do some tricks to fill in texthigh and textlow.
     The first trick is: if we see a static
     or global function, and the textlow for the current pst
     is not set (ie: textlow_not_set), then we use that function's
     address for the textlow of the pst.  */

  /* Now, to fill in texthigh, we remember the last function seen
     in the .o file.  Also, there's a hack in
     bfd/elf.c and gdb/elfread.c to pass the ELF st_size field
     to here via the misc_info field.  Therefore, we can fill in
     a reliable texthigh by taking the address plus size of the
     last function in the file.  */

  if (pst->texthigh == 0 && last_function_name)
    {
      char *p;
      int n;
      struct minimal_symbol *minsym;

      p = strchr (last_function_name, ':');
      if (p == NULL)
	p = last_function_name;
      n = p - last_function_name;
      p = alloca (n + 2);
      strncpy (p, last_function_name, n);
      p[n] = 0;

      minsym = lookup_minimal_symbol (p, pst->filename, objfile);
      if (minsym == NULL)
	{
	  /* Sun Fortran appends an underscore to the minimal symbol name,
	     try again with an appended underscore if the minimal symbol
	     was not found.  */
	  p[n] = '_';
	  p[n + 1] = 0;
	  minsym = lookup_minimal_symbol (p, pst->filename, objfile);
	}

      if (minsym)
	pst->texthigh = SYMBOL_VALUE_ADDRESS (minsym) + MSYMBOL_SIZE (minsym);

      last_function_name = NULL;
    }

  /* this test will be true if the last .o file is only data */
  if (textlow_not_set)
    pst->textlow = pst->texthigh;
  else
    {
      struct partial_symtab *p1;

      /* If we know our own starting text address, then walk through all other
         psymtabs for this objfile, and if any didn't know their ending text
         address, set it to our starting address.  Take care to not set our
         own ending address to our starting address, nor to set addresses on
         `dependency' files that have both textlow and texthigh zero.  */

      ALL_OBJFILE_PSYMTABS (objfile, p1)
      {
	if (p1->texthigh == 0 && p1->textlow != 0 && p1 != pst)
	  {
	    p1->texthigh = pst->textlow;
	    /* if this file has only data, then make textlow match texthigh */
	    if (p1->textlow == 0)
	      p1->textlow = p1->texthigh;
	  }
      }
    }

  /* End of kludge for patching Solaris textlow and texthigh.  */
#endif /* SOFUN_ADDRESS_MAYBE_MISSING.  */

  pst->n_global_syms =
    objfile->global_psymbols.next - (objfile->global_psymbols.list + pst->globals_offset);
  pst->n_static_syms =
    objfile->static_psymbols.next - (objfile->static_psymbols.list + pst->statics_offset);

  pst->number_of_dependencies = number_dependencies;
  if (number_dependencies)
    {
      pst->dependencies = (struct partial_symtab **)
	obstack_alloc (&objfile->objfile_obstack,
		    number_dependencies * sizeof (struct partial_symtab *));
      memcpy (pst->dependencies, dependency_list,
	      number_dependencies * sizeof (struct partial_symtab *));
    }
  else
    pst->dependencies = 0;

  for (i = 0; i < num_includes; i++)
    {
      struct partial_symtab *subpst =
      allocate_psymtab (include_list[i], objfile);

      /* Copy the sesction_offsets array from the main psymtab. */
      subpst->section_offsets = pst->section_offsets;
      subpst->read_symtab_private =
	(char *) obstack_alloc (&objfile->objfile_obstack,
				sizeof (struct symloc));
      LDSYMOFF (subpst) =
	LDSYMLEN (subpst) =
	subpst->textlow =
	subpst->texthigh = 0;

      /* We could save slight bits of space by only making one of these,
         shared by the entire set of include files.  FIXME-someday.  */
      subpst->dependencies = (struct partial_symtab **)
	obstack_alloc (&objfile->objfile_obstack,
		       sizeof (struct partial_symtab *));
      subpst->dependencies[0] = pst;
      subpst->number_of_dependencies = 1;

      subpst->globals_offset =
	subpst->n_global_syms =
	subpst->statics_offset =
	subpst->n_static_syms = 0;

      subpst->readin = 0;
      subpst->symtab = 0;
      subpst->read_symtab = pst->read_symtab;
    }

  sort_pst_symbols (pst);

  /* If there is already a psymtab or symtab for a file of this name, remove it.
     (If there is a symtab, more drastic things also happen.)
     This happens in VxWorks.  */
  free_named_symtabs (pst->filename);

  if (num_includes == 0
      && number_dependencies == 0
      && pst->n_global_syms == 0
      && pst->n_static_syms == 0
      && has_line_numbers == 0)
    {
      /* Throw away this psymtab, it's empty.  We can't deallocate it, since
         it is on the obstack, but we can forget to chain it on the list.  */
      /* Empty psymtabs happen as a result of header files which don't have
         any symbols in them.  There can be a lot of them.  But this check
         is wrong, in that a psymtab with N_SLINE entries but nothing else
         is not empty, but we don't realize that.  Fixing that without slowing
         things down might be tricky.  */

      discard_psymtab (pst);

      /* Indicate that psymtab was thrown away.  */
      pst = (struct partial_symtab *) NULL;
    }
  return pst;
}

static void
dbx_psymtab_to_symtab_1 (struct partial_symtab *pst)
{
  struct cleanup *old_chain;
  int i;

  if (!pst)
    return;

  if (pst->readin)
    {
      fprintf_unfiltered (gdb_stderr, "Psymtab for %s already read in.  Shouldn't happen.\n",
			  pst->filename);
      return;
    }

  /* Read in all partial symtabs on which this one is dependent */
  for (i = 0; i < pst->number_of_dependencies; i++)
    if (!pst->dependencies[i]->readin)
      {
	/* Inform about additional files that need to be read in.  */
	if (info_verbose)
	  {
	    fputs_filtered (" ", gdb_stdout);
	    wrap_here ("");
	    fputs_filtered ("and ", gdb_stdout);
	    wrap_here ("");
	    printf_filtered ("%s...", pst->dependencies[i]->filename);
	    wrap_here ("");	/* Flush output */
	    gdb_flush (gdb_stdout);
	  }
	dbx_psymtab_to_symtab_1 (pst->dependencies[i]);
      }

  if (LDSYMLEN (pst))		/* Otherwise it's a dummy */
    {
      /* Init stuff necessary for reading in symbols */
      stabsread_init ();
      buildsym_init ();
      old_chain = make_cleanup (really_free_pendings, 0);
      file_string_table_offset = FILE_STRING_OFFSET (pst);
      symbol_size = SYMBOL_SIZE (pst);

      /* Read in this file's symbols */
      bfd_seek (pst->objfile->obfd, SYMBOL_OFFSET (pst), SEEK_SET);
      read_ofile_symtab (pst);

      do_cleanups (old_chain);
    }

  pst->readin = 1;
}

/* Read in all of the symbols for a given psymtab for real.
   Be verbose about it if the user wants that.  */

static void
dbx_psymtab_to_symtab (struct partial_symtab *pst)
{
  bfd *sym_bfd;
  struct cleanup *back_to = NULL;

  if (!pst)
    return;

  if (pst->readin)
    {
      fprintf_unfiltered (gdb_stderr, "Psymtab for %s already read in.  Shouldn't happen.\n",
			  pst->filename);
      return;
    }

  if (LDSYMLEN (pst) || pst->number_of_dependencies)
    {
      /* Print the message now, before reading the string table,
         to avoid disconcerting pauses.  */
      if (info_verbose)
	{
	  printf_filtered ("Reading in symbols for %s...", pst->filename);
	  gdb_flush (gdb_stdout);
	}

      sym_bfd = pst->objfile->obfd;

      next_symbol_text_func = dbx_next_symbol_text;

      if (DBX_STAB_SECTION (pst->objfile))
	{
	  stabs_data
	    = symfile_relocate_debug_section (pst->objfile->obfd,
					      DBX_STAB_SECTION (pst->objfile),
					      NULL);
	  if (stabs_data)
	    back_to = make_cleanup (free_current_contents, (void *) &stabs_data);
	}

      dbx_psymtab_to_symtab_1 (pst);

      if (back_to)
	do_cleanups (back_to);

      /* Match with global symbols.  This only needs to be done once,
         after all of the symtabs and dependencies have been read in.   */
      scan_file_globals (pst->objfile);

      /* Finish up the debug error message.  */
      if (info_verbose)
	printf_filtered ("done.\n");
    }
}

/* Read in a defined section of a specific object file's symbols. */

static void
read_ofile_symtab (struct partial_symtab *pst)
{
  char *namestring;
  struct external_nlist *bufp;
  struct internal_nlist nlist;
  unsigned char type;
  unsigned max_symnum;
  bfd *abfd;
  struct objfile *objfile;
  int sym_offset;		/* Offset to start of symbols to read */
  int sym_size;			/* Size of symbols to read */
  CORE_ADDR text_offset;	/* Start of text segment for symbols */
  int text_size;		/* Size of text segment for symbols */
  struct section_offsets *section_offsets;

  objfile = pst->objfile;
  sym_offset = LDSYMOFF (pst);
  sym_size = LDSYMLEN (pst);
  text_offset = pst->textlow;
  text_size = pst->texthigh - pst->textlow;
  /* This cannot be simply objfile->section_offsets because of
     elfstab_offset_sections() which initializes the psymtab section
     offsets information in a special way, and that is different from
     objfile->section_offsets. */ 
  section_offsets = pst->section_offsets;

  current_objfile = objfile;
  subfile_stack = NULL;

  stringtab_global = DBX_STRINGTAB (objfile);
  last_source_file = NULL;

  abfd = objfile->obfd;
  symfile_bfd = objfile->obfd;	/* Implicit param to next_text_symbol */
  symbuf_end = symbuf_idx = 0;
  symbuf_read = 0;
  symbuf_left = sym_offset + sym_size;

  /* It is necessary to actually read one symbol *before* the start
     of this symtab's symbols, because the GCC_COMPILED_FLAG_SYMBOL
     occurs before the N_SO symbol.

     Detecting this in read_dbx_symtab
     would slow down initial readin, so we look for it here instead.  */
  if (!processing_acc_compilation && sym_offset >= (int) symbol_size)
    {
      stabs_seek (sym_offset - symbol_size);
      fill_symbuf (abfd);
      bufp = &symbuf[symbuf_idx++];
      INTERNALIZE_SYMBOL (nlist, bufp, abfd);
      OBJSTAT (objfile, n_stabs++);

      namestring = set_namestring (objfile, nlist);

      processing_gcc_compilation = 0;
      if (nlist.n_type == N_TEXT)
	{
	  const char *tempstring = namestring;

	  if (DEPRECATED_STREQ (namestring, GCC_COMPILED_FLAG_SYMBOL))
	    processing_gcc_compilation = 1;
	  else if (DEPRECATED_STREQ (namestring, GCC2_COMPILED_FLAG_SYMBOL))
	    processing_gcc_compilation = 2;
	  if (tempstring[0] == bfd_get_symbol_leading_char (symfile_bfd))
	    ++tempstring;
	  if (DEPRECATED_STREQN (tempstring, "__gnu_compiled", 14))
	    processing_gcc_compilation = 2;
	}

      /* Try to select a C++ demangling based on the compilation unit
         producer. */

#if 0
      /* For now, stay with AUTO_DEMANGLING for g++ output, as we don't
	 know whether it will use the old style or v3 mangling.  */
      if (processing_gcc_compilation)
	{
	  if (AUTO_DEMANGLING)
	    {
	      set_demangling_style (GNU_DEMANGLING_STYLE_STRING);
	    }
	}
#endif
    }
  else
    {
      /* The N_SO starting this symtab is the first symbol, so we
         better not check the symbol before it.  I'm not this can
         happen, but it doesn't hurt to check for it.  */
      stabs_seek (sym_offset);
      processing_gcc_compilation = 0;
    }

  if (symbuf_idx == symbuf_end)
    fill_symbuf (abfd);
  bufp = &symbuf[symbuf_idx];
  if (bfd_h_get_8 (abfd, bufp->e_type) != N_SO)
    error ("First symbol in segment of executable not a source symbol");

  max_symnum = sym_size / symbol_size;

  for (symnum = 0;
       symnum < max_symnum;
       symnum++)
    {
      QUIT;			/* Allow this to be interruptable */
      if (symbuf_idx == symbuf_end)
	fill_symbuf (abfd);
      bufp = &symbuf[symbuf_idx++];
      INTERNALIZE_SYMBOL (nlist, bufp, abfd);
      OBJSTAT (objfile, n_stabs++);

      type = bfd_h_get_8 (abfd, bufp->e_type);

      namestring = set_namestring (objfile, nlist);

      if (type & N_STAB)
	{
	  process_one_symbol (type, nlist.n_desc, nlist.n_value,
			      namestring, section_offsets, objfile);
	}
      /* We skip checking for a new .o or -l file; that should never
         happen in this routine. */
      else if (type == N_TEXT)
	{
	  /* I don't think this code will ever be executed, because
	     the GCC_COMPILED_FLAG_SYMBOL usually is right before
	     the N_SO symbol which starts this source file.
	     However, there is no reason not to accept
	     the GCC_COMPILED_FLAG_SYMBOL anywhere.  */

	  if (DEPRECATED_STREQ (namestring, GCC_COMPILED_FLAG_SYMBOL))
	    processing_gcc_compilation = 1;
	  else if (DEPRECATED_STREQ (namestring, GCC2_COMPILED_FLAG_SYMBOL))
	    processing_gcc_compilation = 2;

#if 0
	  /* For now, stay with AUTO_DEMANGLING for g++ output, as we don't
	     know whether it will use the old style or v3 mangling.  */
	  if (AUTO_DEMANGLING)
	    {
	      set_demangling_style (GNU_DEMANGLING_STYLE_STRING);
	    }
#endif
	}
      else if (type & N_EXT || type == (unsigned char) N_TEXT
	       || type == (unsigned char) N_NBTEXT
	)
	{
	  /* Global symbol: see if we came across a dbx defintion for
	     a corresponding symbol.  If so, store the value.  Remove
	     syms from the chain when their values are stored, but
	     search the whole chain, as there may be several syms from
	     different files with the same name. */
	  /* This is probably not true.  Since the files will be read
	     in one at a time, each reference to a global symbol will
	     be satisfied in each file as it appears. So we skip this
	     section. */
	  ;
	}
    }

  current_objfile = NULL;

  /* In a Solaris elf file, this variable, which comes from the
     value of the N_SO symbol, will still be 0.  Luckily, text_offset,
     which comes from pst->textlow is correct. */
  if (last_source_start_addr == 0)
    last_source_start_addr = text_offset;

  /* In reordered executables last_source_start_addr may not be the
     lower bound for this symtab, instead use text_offset which comes
     from pst->textlow which is correct.  */
  if (last_source_start_addr > text_offset)
    last_source_start_addr = text_offset;

  pst->symtab = end_symtab (text_offset + text_size, objfile, SECT_OFF_TEXT (objfile));

  end_stabs ();
}


/* This handles a single symbol from the symbol-file, building symbols
   into a GDB symtab.  It takes these arguments and an implicit argument.

   TYPE is the type field of the ".stab" symbol entry.
   DESC is the desc field of the ".stab" entry.
   VALU is the value field of the ".stab" entry.
   NAME is the symbol name, in our address space.
   SECTION_OFFSETS is a set of amounts by which the sections of this object
   file were relocated when it was loaded into memory.
   Note that these section_offsets are not the 
   objfile->section_offsets but the pst->section_offsets.
   All symbols that refer
   to memory locations need to be offset by these amounts.
   OBJFILE is the object file from which we are reading symbols.
   It is used in end_symtab.  */

void
process_one_symbol (int type, int desc, CORE_ADDR valu, char *name,
		    struct section_offsets *section_offsets,
		    struct objfile *objfile)
{
#ifdef SUN_FIXED_LBRAC_BUG
  /* If SUN_FIXED_LBRAC_BUG is defined, then it tells us whether we need
     to correct the address of N_LBRAC's.  If it is not defined, then
     we never need to correct the addresses.  */

  /* This records the last pc address we've seen.  We depend on there being
     an SLINE or FUN or SO before the first LBRAC, since the variable does
     not get reset in between reads of different symbol files.  */
  static CORE_ADDR last_pc_address;
#endif

  struct context_stack *new;
  /* This remembers the address of the start of a function.  It is used
     because in Solaris 2, N_LBRAC, N_RBRAC, and N_SLINE entries are
     relative to the current function's start address.  On systems
     other than Solaris 2, this just holds the SECT_OFF_TEXT value, and is
     used to relocate these symbol types rather than SECTION_OFFSETS.  */
  static CORE_ADDR function_start_offset;

  /* This holds the address of the start of a function, without the system
     peculiarities of function_start_offset.  */
  static CORE_ADDR last_function_start;

  /* If this is nonzero, we've seen an N_SLINE since the start of the
     current function.  We use this to tell us to move the first sline
     to the beginning of the function regardless of what its given
     value is. */
  static int sline_found_in_function = 1;

  /* If this is nonzero, we've seen a non-gcc N_OPT symbol for this source
     file.  Used to detect the SunPRO solaris compiler.  */
  static int n_opt_found;

  /* The stab type used for the definition of the last function.
     N_STSYM or N_GSYM for SunOS4 acc; N_FUN for other compilers.  */
  static int function_stab_type = 0;

  if (!block_address_function_relative)
    /* N_LBRAC, N_RBRAC and N_SLINE entries are not relative to the
       function start address, so just use the text offset.  */
    function_start_offset = ANOFFSET (section_offsets, SECT_OFF_TEXT (objfile));

  /* Something is wrong if we see real data before
     seeing a source file name.  */

  if (last_source_file == NULL && type != (unsigned char) N_SO)
    {
      /* Ignore any symbols which appear before an N_SO symbol.
         Currently no one puts symbols there, but we should deal
         gracefully with the case.  A complain()t might be in order,
         but this should not be an error ().  */
      return;
    }

  switch (type)
    {
    case N_FUN:
    case N_FNAME:

      if (*name == '\000')
	{
	  /* This N_FUN marks the end of a function.  This closes off the
	     current block.  */

 	  if (context_stack_depth <= 0)
 	    {
	      lbrac_mismatch_complaint (symnum);
 	      break;
 	    }

	  /* The following check is added before recording line 0 at
	     end of function so as to handle hand-generated stabs
	     which may have an N_FUN stabs at the end of the function, but
	     no N_SLINE stabs.  */
	  if (sline_found_in_function)
	    record_line (current_subfile, 0, last_function_start + valu);

	  within_function = 0;
	  new = pop_context ();

	  /* Make a block for the local symbols within.  */
	  finish_block (new->name, &local_symbols, new->old_blocks,
			new->start_addr, new->start_addr + valu,
			objfile);

	  /* May be switching to an assembler file which may not be using
	     block relative stabs, so reset the offset.  */
	  if (block_address_function_relative)
	    function_start_offset = 0;

	  break;
	}

      sline_found_in_function = 0;

      /* Relocate for dynamic loading */
      valu += ANOFFSET (section_offsets, SECT_OFF_TEXT (objfile));
      valu = SMASH_TEXT_ADDRESS (valu);
      last_function_start = valu;

      goto define_a_symbol;

    case N_LBRAC:
      /* This "symbol" just indicates the start of an inner lexical
         context within a function.  */

      /* Ignore extra outermost context from SunPRO cc and acc.  */
      if (n_opt_found && desc == 1)
	break;

      if (block_address_function_relative)
	/* Relocate for Sun ELF acc fn-relative syms.  */
	valu += function_start_offset;
      else
	/* On most machines, the block addresses are relative to the
	   N_SO, the linker did not relocate them (sigh).  */
	valu += last_source_start_addr;

#ifdef SUN_FIXED_LBRAC_BUG
      if (!SUN_FIXED_LBRAC_BUG && valu < last_pc_address)
	{
	  /* Patch current LBRAC pc value to match last handy pc value */
	  complaint (&symfile_complaints, "bad block start address patched");
	  valu = last_pc_address;
	}
#endif
      new = push_context (desc, valu);
      break;

    case N_RBRAC:
      /* This "symbol" just indicates the end of an inner lexical
         context that was started with N_LBRAC.  */

      /* Ignore extra outermost context from SunPRO cc and acc.  */
      if (n_opt_found && desc == 1)
	break;

      if (block_address_function_relative)
	/* Relocate for Sun ELF acc fn-relative syms.  */
	valu += function_start_offset;
      else
	/* On most machines, the block addresses are relative to the
	   N_SO, the linker did not relocate them (sigh).  */
	valu += last_source_start_addr;

      if (context_stack_depth <= 0)
	{
	  lbrac_mismatch_complaint (symnum);
	  break;
	}

      new = pop_context ();
      if (desc != new->depth)
	lbrac_mismatch_complaint (symnum);

      /* Some compilers put the variable decls inside of an
         LBRAC/RBRAC block.  This macro should be nonzero if this
         is true.  DESC is N_DESC from the N_RBRAC symbol.
         GCC_P is true if we've detected the GCC_COMPILED_SYMBOL
         or the GCC2_COMPILED_SYMBOL.  */
#if !defined (VARIABLES_INSIDE_BLOCK)
#define VARIABLES_INSIDE_BLOCK(desc, gcc_p) 0
#endif

      /* Can only use new->locals as local symbols here if we're in
         gcc or on a machine that puts them before the lbrack.  */
      if (!VARIABLES_INSIDE_BLOCK (desc, processing_gcc_compilation))
	{
	  if (local_symbols != NULL)
	    {
	      /* GCC development snapshots from March to December of
		 2000 would output N_LSYM entries after N_LBRAC
		 entries.  As a consequence, these symbols are simply
		 discarded.  Complain if this is the case.  Note that
		 there are some compilers which legitimately put local
		 symbols within an LBRAC/RBRAC block; this complaint
		 might also help sort out problems in which
		 VARIABLES_INSIDE_BLOCK is incorrectly defined.  */
	      complaint (&symfile_complaints,
			 "misplaced N_LBRAC entry; discarding local symbols which have no enclosing block");
	    }
	  local_symbols = new->locals;
	}

      if (context_stack_depth
	  > !VARIABLES_INSIDE_BLOCK (desc, processing_gcc_compilation))
	{
	  /* This is not the outermost LBRAC...RBRAC pair in the function,
	     its local symbols preceded it, and are the ones just recovered
	     from the context stack.  Define the block for them (but don't
	     bother if the block contains no symbols.  Should we complain
	     on blocks without symbols?  I can't think of any useful purpose
	     for them).  */
	  if (local_symbols != NULL)
	    {
	      /* Muzzle a compiler bug that makes end < start.  (which
	         compilers?  Is this ever harmful?).  */
	      if (new->start_addr > valu)
		{
		  complaint (&symfile_complaints,
			     "block start larger than block end");
		  new->start_addr = valu;
		}
	      /* Make a block for the local symbols within.  */
	      finish_block (0, &local_symbols, new->old_blocks,
			    new->start_addr, valu, objfile);
	    }
	}
      else
	{
	  /* This is the outermost LBRAC...RBRAC pair.  There is no
	     need to do anything; leave the symbols that preceded it
	     to be attached to the function's own block.  We need to
	     indicate that we just moved outside of the function.  */
	  within_function = 0;
	}

      if (VARIABLES_INSIDE_BLOCK (desc, processing_gcc_compilation))
	/* Now pop locals of block just finished.  */
	local_symbols = new->locals;
      break;

    case N_FN:
    case N_FN_SEQ:
      /* This kind of symbol indicates the start of an object file.  */
      /* Relocate for dynamic loading */
      valu += ANOFFSET (section_offsets, SECT_OFF_TEXT (objfile));
      break;

    case N_SO:
      /* This type of symbol indicates the start of data
         for one source file.
         Finish the symbol table of the previous source file
         (if any) and start accumulating a new symbol table.  */
      /* Relocate for dynamic loading */
      valu += ANOFFSET (section_offsets, SECT_OFF_TEXT (objfile));

      n_opt_found = 0;

#ifdef SUN_FIXED_LBRAC_BUG
      last_pc_address = valu;	/* Save for SunOS bug circumcision */
#endif

#ifdef PCC_SOL_BROKEN
      /* pcc bug, occasionally puts out SO for SOL.  */
      if (context_stack_depth > 0)
	{
	  start_subfile (name, NULL);
	  break;
	}
#endif
      if (last_source_file)
	{
	  /* Check if previous symbol was also an N_SO (with some
	     sanity checks).  If so, that one was actually the directory
	     name, and the current one is the real file name.
	     Patch things up. */
	  if (previous_stab_code == (unsigned char) N_SO)
	    {
	      patch_subfile_names (current_subfile, name);
	      break;		/* Ignore repeated SOs */
	    }
	  end_symtab (valu, objfile, SECT_OFF_TEXT (objfile));
	  end_stabs ();
	}

      /* Null name means this just marks the end of text for this .o file.
         Don't start a new symtab in this case.  */
      if (*name == '\000')
	break;

      if (block_address_function_relative)
	function_start_offset = 0;

      start_stabs ();
      start_symtab (name, NULL, valu);
      record_debugformat ("stabs");
      break;

    case N_SOL:
      /* This type of symbol indicates the start of data for
         a sub-source-file, one whose contents were copied or
         included in the compilation of the main source file
         (whose name was given in the N_SO symbol.)  */
      /* Relocate for dynamic loading */
      valu += ANOFFSET (section_offsets, SECT_OFF_TEXT (objfile));
      start_subfile (name, current_subfile->dirname);
      break;

    case N_BINCL:
      push_subfile ();
      add_new_header_file (name, valu);
      start_subfile (name, current_subfile->dirname);
      break;

    case N_EINCL:
      start_subfile (pop_subfile (), current_subfile->dirname);
      break;

    case N_EXCL:
      add_old_header_file (name, valu);
      break;

    case N_SLINE:
      /* This type of "symbol" really just records
         one line-number -- core-address correspondence.
         Enter it in the line list for this symbol table.  */

      /* Relocate for dynamic loading and for ELF acc fn-relative syms.  */
      valu += function_start_offset;

#ifdef SUN_FIXED_LBRAC_BUG
      last_pc_address = valu;	/* Save for SunOS bug circumcision */
#endif
      /* If this is the first SLINE note in the function, record it at
	 the start of the function instead of at the listed location.  */
      if (within_function && sline_found_in_function == 0)
	{
	  record_line (current_subfile, desc, last_function_start);
	  sline_found_in_function = 1;
	}
      else
	record_line (current_subfile, desc, valu);
      break;

    case N_BCOMM:
      common_block_start (name, objfile);
      break;

    case N_ECOMM:
      common_block_end (objfile);
      break;

      /* The following symbol types need to have the appropriate offset added
         to their value; then we process symbol definitions in the name.  */

    case N_STSYM:		/* Static symbol in data seg */
    case N_LCSYM:		/* Static symbol in BSS seg */
    case N_ROSYM:		/* Static symbol in Read-only data seg */
      /* HORRID HACK DEPT.  However, it's Sun's furgin' fault.
         Solaris2's stabs-in-elf makes *most* symbols relative
         but leaves a few absolute (at least for Solaris 2.1 and version
         2.0.1 of the SunPRO compiler).  N_STSYM and friends sit on the fence.
         .stab "foo:S...",N_STSYM        is absolute (ld relocates it)
         .stab "foo:V...",N_STSYM        is relative (section base subtracted).
         This leaves us no choice but to search for the 'S' or 'V'...
         (or pass the whole section_offsets stuff down ONE MORE function
         call level, which we really don't want to do).  */
      {
	char *p;

	/* .o files and NLMs have non-zero text seg offsets, but don't need
	   their static syms offset in this fashion.  XXX - This is really a
	   crock that should be fixed in the solib handling code so that I
	   don't have to work around it here. */

	if (!symfile_relocatable)
	  {
	    p = strchr (name, ':');
	    if (p != 0 && p[1] == 'S')
	      {
		/* The linker relocated it.  We don't want to add an
		   elfstab_offset_sections-type offset, but we *do* want
		   to add whatever solib.c passed to symbol_file_add as
		   addr (this is known to affect SunOS4, and I suspect ELF
		   too).  Since elfstab_offset_sections currently does not
		   muck with the text offset (there is no Ttext.text
		   symbol), we can get addr from the text offset.  If
		   elfstab_offset_sections ever starts dealing with the
		   text offset, and we still need to do this, we need to
		   invent a SECT_OFF_ADDR_KLUDGE or something.  */
		valu += ANOFFSET (section_offsets, SECT_OFF_TEXT (objfile));
		goto define_a_symbol;
	      }
	  }
	/* Since it's not the kludge case, re-dispatch to the right handler. */
	switch (type)
	  {
	  case N_STSYM:
	    goto case_N_STSYM;
	  case N_LCSYM:
	    goto case_N_LCSYM;
	  case N_ROSYM:
	    goto case_N_ROSYM;
	  default:
	    internal_error (__FILE__, __LINE__, "failed internal consistency check");
	  }
      }

    case_N_STSYM:		/* Static symbol in data seg */
    case N_DSLINE:		/* Source line number, data seg */
      valu += ANOFFSET (section_offsets, SECT_OFF_DATA (objfile));
      goto define_a_symbol;

    case_N_LCSYM:		/* Static symbol in BSS seg */
    case N_BSLINE:		/* Source line number, bss seg */
      /*   N_BROWS:       overlaps with N_BSLINE */
      valu += ANOFFSET (section_offsets, SECT_OFF_BSS (objfile));
      goto define_a_symbol;

    case_N_ROSYM:		/* Static symbol in Read-only data seg */
      valu += ANOFFSET (section_offsets, SECT_OFF_RODATA (objfile));
      goto define_a_symbol;

    case N_ENTRY:		/* Alternate entry point */
      /* Relocate for dynamic loading */
      valu += ANOFFSET (section_offsets, SECT_OFF_TEXT (objfile));
      goto define_a_symbol;

      /* The following symbol types we don't know how to process.  Handle
         them in a "default" way, but complain to people who care.  */
    default:
    case N_CATCH:		/* Exception handler catcher */
    case N_EHDECL:		/* Exception handler name */
    case N_PC:			/* Global symbol in Pascal */
    case N_M2C:		/* Modula-2 compilation unit */
      /*   N_MOD2:        overlaps with N_EHDECL */
    case N_SCOPE:		/* Modula-2 scope information */
    case N_ECOML:		/* End common (local name) */
    case N_NBTEXT:		/* Gould Non-Base-Register symbols??? */
    case N_NBDATA:
    case N_NBBSS:
    case N_NBSTS:
    case N_NBLCS:
      unknown_symtype_complaint (local_hex_string (type));
      /* FALLTHROUGH */

      /* The following symbol types don't need the address field relocated,
         since it is either unused, or is absolute.  */
    define_a_symbol:
    case N_GSYM:		/* Global variable */
    case N_NSYMS:		/* Number of symbols (ultrix) */
    case N_NOMAP:		/* No map?  (ultrix) */
    case N_RSYM:		/* Register variable */
    case N_DEFD:		/* Modula-2 GNU module dependency */
    case N_SSYM:		/* Struct or union element */
    case N_LSYM:		/* Local symbol in stack */
    case N_PSYM:		/* Parameter variable */
    case N_LENG:		/* Length of preceding symbol type */
      if (name)
	{
	  int deftype;
	  char *colon_pos = strchr (name, ':');
	  if (colon_pos == NULL)
	    deftype = '\0';
	  else
	    deftype = colon_pos[1];

	  switch (deftype)
	    {
	    case 'f':
	    case 'F':
	      function_stab_type = type;

#ifdef SOFUN_ADDRESS_MAYBE_MISSING
	      /* Deal with the SunPRO 3.0 compiler which omits the address
	         from N_FUN symbols.  */
	      if (type == N_FUN
		  && valu == ANOFFSET (section_offsets, SECT_OFF_TEXT (objfile)))
		{
		  CORE_ADDR minsym_valu = 
		    find_stab_function_addr (name, last_source_file, objfile);

		  /* find_stab_function_addr will return 0 if the minimal
		     symbol wasn't found.  (Unfortunately, this might also
		     be a valid address.)  Anyway, if it *does* return 0,
		     it is likely that the value was set correctly to begin
		     with... */
		  if (minsym_valu != 0)
		    valu = minsym_valu;
		}
#endif

#ifdef SUN_FIXED_LBRAC_BUG
	      /* The Sun acc compiler, under SunOS4, puts out
	         functions with N_GSYM or N_STSYM.  The problem is
	         that the address of the symbol is no good (for N_GSYM
	         it doesn't even attept an address; for N_STSYM it
	         puts out an address but then it gets relocated
	         relative to the data segment, not the text segment).
	         Currently we can't fix this up later as we do for
	         some types of symbol in scan_file_globals.
	         Fortunately we do have a way of finding the address -
	         we know that the value in last_pc_address is either
	         the one we want (if we're dealing with the first
	         function in an object file), or somewhere in the
	         previous function. This means that we can use the
	         minimal symbol table to get the address.  */

	      /* Starting with release 3.0, the Sun acc compiler,
	         under SunOS4, puts out functions with N_FUN and a value
	         of zero. This gets relocated to the start of the text
	         segment of the module, which is no good either.
	         Under SunOS4 we can deal with this as N_SLINE and N_SO
	         entries contain valid absolute addresses.
	         Release 3.0 acc also puts out N_OPT entries, which makes
	         it possible to discern acc from cc or gcc.  */

	      if (type == N_GSYM || type == N_STSYM
		  || (type == N_FUN
		      && n_opt_found && !block_address_function_relative))
		{
		  struct minimal_symbol *m;
		  int l = colon_pos - name;

		  m = lookup_minimal_symbol_by_pc (last_pc_address);
		  if (m && strncmp (DEPRECATED_SYMBOL_NAME (m), name, l) == 0
		      && DEPRECATED_SYMBOL_NAME (m)[l] == '\0')
		    /* last_pc_address was in this function */
		    valu = SYMBOL_VALUE (m);
		  else if (m && DEPRECATED_SYMBOL_NAME (m + 1)
			   && strncmp (DEPRECATED_SYMBOL_NAME (m + 1), name, l) == 0
			   && DEPRECATED_SYMBOL_NAME (m + 1)[l] == '\0')
		    /* last_pc_address was in last function */
		    valu = SYMBOL_VALUE (m + 1);
		  else
		    /* Not found - use last_pc_address (for finish_block) */
		    valu = last_pc_address;
		}

	      last_pc_address = valu;	/* Save for SunOS bug circumcision */
#endif

	      if (block_address_function_relative)
		/* For Solaris 2.0 compilers, the block addresses and
		   N_SLINE's are relative to the start of the
		   function.  On normal systems, and when using gcc on
		   Solaris 2.0, these addresses are just absolute, or
		   relative to the N_SO, depending on
		   BLOCK_ADDRESS_ABSOLUTE.  */
		function_start_offset = valu;

	      within_function = 1;

	      if (context_stack_depth > 1)
		{
		  complaint (&symfile_complaints,
			     "unmatched N_LBRAC before symtab pos %d", symnum);
		  break;
		}

	      if (context_stack_depth > 0)
		{
		  new = pop_context ();
		  /* Make a block for the local symbols within.  */
		  finish_block (new->name, &local_symbols, new->old_blocks,
				new->start_addr, valu, objfile);
		}

	      new = push_context (0, valu);
	      new->name = define_symbol (valu, name, desc, type, objfile);
	      break;

	    default:
	      define_symbol (valu, name, desc, type, objfile);
	      break;
	    }
	}
      break;

      /* We use N_OPT to carry the gcc2_compiled flag.  Sun uses it
         for a bunch of other flags, too.  Someday we may parse their
         flags; for now we ignore theirs and hope they'll ignore ours.  */
    case N_OPT:		/* Solaris 2:  Compiler options */
      if (name)
	{
	  if (strcmp (name, GCC2_COMPILED_FLAG_SYMBOL) == 0)
	    {
	      processing_gcc_compilation = 2;
#if 0				/* Works, but is experimental.  -fnf */
	      /* For now, stay with AUTO_DEMANGLING for g++ output, as we don't
		 know whether it will use the old style or v3 mangling.  */
	      if (AUTO_DEMANGLING)
		{
		  set_demangling_style (GNU_DEMANGLING_STYLE_STRING);
		}
#endif
	    }
	  else
	    n_opt_found = 1;
	}
      break;

    case N_MAIN:		/* Name of main routine.  */
      /* FIXME: If one has a symbol file with N_MAIN and then replaces
	 it with a symbol file with "main" and without N_MAIN.  I'm
	 not sure exactly what rule to follow but probably something
	 like: N_MAIN takes precedence over "main" no matter what
	 objfile it is in; If there is more than one N_MAIN, choose
	 the one in the symfile_objfile; If there is more than one
	 N_MAIN within a given objfile, complain() and choose
	 arbitrarily. (kingdon) */
      if (name != NULL)
	set_main_name (name);
      break;

      /* The following symbol types can be ignored.  */
    case N_OBJ:		/* Solaris 2:  Object file dir and name */
    case N_PATCH:	/* Solaris2: Patch Run Time Checker.  */
      /*   N_UNDF:                   Solaris 2:  file separator mark */
      /*   N_UNDF: -- we will never encounter it, since we only process one
         file's symbols at once.  */
    case N_ENDM:		/* Solaris 2:  End of module */
    case N_ALIAS:		/* SunPro F77: alias name, ignore for now.  */
      break;
    }

  /* '#' is a GNU C extension to allow one symbol to refer to another
     related symbol.

     Generally this is used so that an alias can refer to its main
     symbol.  */
  if (name[0] == '#')
    {
      /* Initialize symbol reference names and determine if this is 
         a definition.  If symbol reference is being defined, go 
         ahead and add it.  Otherwise, just return sym. */

      char *s = name;
      int refnum;

      /* If this stab defines a new reference ID that is not on the
         reference list, then put it on the reference list.

         We go ahead and advance NAME past the reference, even though
         it is not strictly necessary at this time.  */
      refnum = symbol_reference_defined (&s);
      if (refnum >= 0)
	if (!ref_search (refnum))
	  ref_add (refnum, 0, name, valu);
      name = s;
    }


  previous_stab_code = type;
}

/* FIXME: The only difference between this and elfstab_build_psymtabs
   is the call to install_minimal_symbols for elf, and the support for
   split sections.  If the differences are really that small, the code
   should be shared.  */

/* Scan and build partial symbols for an coff symbol file.
   The coff file has already been processed to get its minimal symbols.

   This routine is the equivalent of dbx_symfile_init and dbx_symfile_read
   rolled into one.

   OBJFILE is the object file we are reading symbols from.
   ADDR is the address relative to which the symbols are (e.g.
   the base address of the text segment).
   MAINLINE is true if we are reading the main symbol
   table (as opposed to a shared lib or dynamically loaded file).
   TEXTADDR is the address of the text section.
   TEXTSIZE is the size of the text section.
   STABSECTS is the list of .stab sections in OBJFILE.
   STABSTROFFSET and STABSTRSIZE define the location in OBJFILE where the
   .stabstr section exists.

   This routine is mostly copied from dbx_symfile_init and dbx_symfile_read,
   adjusted for coff details. */

void
coffstab_build_psymtabs (struct objfile *objfile, int mainline,
			 CORE_ADDR textaddr, unsigned int textsize,
			 struct stab_section_list *stabsects,
			 file_ptr stabstroffset, unsigned int stabstrsize)
{
  int val;
  bfd *sym_bfd = objfile->obfd;
  char *name = bfd_get_filename (sym_bfd);
  struct dbx_symfile_info *info;
  unsigned int stabsize;

  /* There is already a dbx_symfile_info allocated by our caller.
     It might even contain some info from the coff symtab to help us.  */
  info = objfile->sym_stab_info;

  DBX_TEXT_ADDR (objfile) = textaddr;
  DBX_TEXT_SIZE (objfile) = textsize;

#define	COFF_STABS_SYMBOL_SIZE	12	/* XXX FIXME XXX */
  DBX_SYMBOL_SIZE (objfile) = COFF_STABS_SYMBOL_SIZE;
  DBX_STRINGTAB_SIZE (objfile) = stabstrsize;

  if (stabstrsize > bfd_get_size (sym_bfd))
    error ("ridiculous string table size: %d bytes", stabstrsize);
  DBX_STRINGTAB (objfile) = (char *)
    obstack_alloc (&objfile->objfile_obstack, stabstrsize + 1);
  OBJSTAT (objfile, sz_strtab += stabstrsize + 1);

  /* Now read in the string table in one big gulp.  */

  val = bfd_seek (sym_bfd, stabstroffset, SEEK_SET);
  if (val < 0)
    perror_with_name (name);
  val = bfd_bread (DBX_STRINGTAB (objfile), stabstrsize, sym_bfd);
  if (val != stabstrsize)
    perror_with_name (name);

  stabsread_new_init ();
  buildsym_new_init ();
  free_header_files ();
  init_header_files ();

  processing_acc_compilation = 1;

  /* In a coff file, we've already installed the minimal symbols that came
     from the coff (non-stab) symbol table, so always act like an
     incremental load here. */
  if (stabsects->next == NULL)
    {
      stabsize = bfd_section_size (sym_bfd, stabsects->section);
      DBX_SYMCOUNT (objfile) = stabsize / DBX_SYMBOL_SIZE (objfile);
      DBX_SYMTAB_OFFSET (objfile) = stabsects->section->filepos;
    }
  else
    {
      struct stab_section_list *stabsect;

      DBX_SYMCOUNT (objfile) = 0;
      for (stabsect = stabsects; stabsect != NULL; stabsect = stabsect->next)
	{
	  stabsize = bfd_section_size (sym_bfd, stabsect->section);
	  DBX_SYMCOUNT (objfile) += stabsize / DBX_SYMBOL_SIZE (objfile);
	}

      DBX_SYMTAB_OFFSET (objfile) = stabsects->section->filepos;

      symbuf_sections = stabsects->next;
      symbuf_left = bfd_section_size (sym_bfd, stabsects->section);
      symbuf_read = 0;
    }

  dbx_symfile_read (objfile, 0);
}

/* Scan and build partial symbols for an ELF symbol file.
   This ELF file has already been processed to get its minimal symbols,
   and any DWARF symbols that were in it.

   This routine is the equivalent of dbx_symfile_init and dbx_symfile_read
   rolled into one.

   OBJFILE is the object file we are reading symbols from.
   ADDR is the address relative to which the symbols are (e.g.
   the base address of the text segment).
   MAINLINE is true if we are reading the main symbol
   table (as opposed to a shared lib or dynamically loaded file).
   STABSECT is the BFD section information for the .stab section.
   STABSTROFFSET and STABSTRSIZE define the location in OBJFILE where the
   .stabstr section exists.

   This routine is mostly copied from dbx_symfile_init and dbx_symfile_read,
   adjusted for elf details. */

void
elfstab_build_psymtabs (struct objfile *objfile, int mainline,
			asection *stabsect,
			file_ptr stabstroffset, unsigned int stabstrsize)
{
  int val;
  bfd *sym_bfd = objfile->obfd;
  char *name = bfd_get_filename (sym_bfd);
  struct dbx_symfile_info *info;
  struct cleanup *back_to = NULL;

  /* There is already a dbx_symfile_info allocated by our caller.
     It might even contain some info from the ELF symtab to help us.  */
  info = objfile->sym_stab_info;

  /* Find the first and last text address.  dbx_symfile_read seems to
     want this.  */
  find_text_range (sym_bfd, objfile);

#define	ELF_STABS_SYMBOL_SIZE	12	/* XXX FIXME XXX */
  DBX_SYMBOL_SIZE (objfile) = ELF_STABS_SYMBOL_SIZE;
  DBX_SYMCOUNT (objfile)
    = bfd_section_size (objfile->obfd, stabsect) / DBX_SYMBOL_SIZE (objfile);
  DBX_STRINGTAB_SIZE (objfile) = stabstrsize;
  DBX_SYMTAB_OFFSET (objfile) = stabsect->filepos;
  DBX_STAB_SECTION (objfile) = stabsect;

  if (stabstrsize > bfd_get_size (sym_bfd))
    error ("ridiculous string table size: %d bytes", stabstrsize);
  DBX_STRINGTAB (objfile) = (char *)
    obstack_alloc (&objfile->objfile_obstack, stabstrsize + 1);
  OBJSTAT (objfile, sz_strtab += stabstrsize + 1);

  /* Now read in the string table in one big gulp.  */

  val = bfd_seek (sym_bfd, stabstroffset, SEEK_SET);
  if (val < 0)
    perror_with_name (name);
  val = bfd_bread (DBX_STRINGTAB (objfile), stabstrsize, sym_bfd);
  if (val != stabstrsize)
    perror_with_name (name);

  stabsread_new_init ();
  buildsym_new_init ();
  free_header_files ();
  init_header_files ();

  processing_acc_compilation = 1;

  symbuf_read = 0;
  symbuf_left = bfd_section_size (objfile->obfd, stabsect);
  stabs_data = symfile_relocate_debug_section (objfile->obfd, stabsect, NULL);
  if (stabs_data)
    back_to = make_cleanup (free_current_contents, (void *) &stabs_data);

  /* In an elf file, we've already installed the minimal symbols that came
     from the elf (non-stab) symbol table, so always act like an
     incremental load here.  dbx_symfile_read should not generate any new
     minimal symbols, since we will have already read the ELF dynamic symbol
     table and normal symbol entries won't be in the ".stab" section; but in
     case it does, it will install them itself.  */
  dbx_symfile_read (objfile, 0);

  if (back_to)
    do_cleanups (back_to);
}

/* Scan and build partial symbols for a file with special sections for stabs
   and stabstrings.  The file has already been processed to get its minimal
   symbols, and any other symbols that might be necessary to resolve GSYMs.

   This routine is the equivalent of dbx_symfile_init and dbx_symfile_read
   rolled into one.

   OBJFILE is the object file we are reading symbols from.
   ADDR is the address relative to which the symbols are (e.g. the base address
   of the text segment).
   MAINLINE is true if we are reading the main symbol table (as opposed to a
   shared lib or dynamically loaded file).
   STAB_NAME is the name of the section that contains the stabs.
   STABSTR_NAME is the name of the section that contains the stab strings.

   This routine is mostly copied from dbx_symfile_init and dbx_symfile_read. */

void
stabsect_build_psymtabs (struct objfile *objfile, int mainline, char *stab_name,
			 char *stabstr_name, char *text_name)
{
  int val;
  bfd *sym_bfd = objfile->obfd;
  char *name = bfd_get_filename (sym_bfd);
  asection *stabsect;
  asection *stabstrsect;
  asection *text_sect;

  stabsect = bfd_get_section_by_name (sym_bfd, stab_name);
  stabstrsect = bfd_get_section_by_name (sym_bfd, stabstr_name);

  if (!stabsect)
    return;

  if (!stabstrsect)
    error ("stabsect_build_psymtabs:  Found stabs (%s), but not string section (%s)",
	   stab_name, stabstr_name);

  objfile->sym_stab_info = (struct dbx_symfile_info *)
    xmalloc (sizeof (struct dbx_symfile_info));
  memset (objfile->sym_stab_info, 0, sizeof (struct dbx_symfile_info));

  text_sect = bfd_get_section_by_name (sym_bfd, text_name);
  if (!text_sect)
    error ("Can't find %s section in symbol file", text_name);
  DBX_TEXT_ADDR (objfile) = bfd_section_vma (sym_bfd, text_sect);
  DBX_TEXT_SIZE (objfile) = bfd_section_size (sym_bfd, text_sect);

  DBX_SYMBOL_SIZE (objfile) = sizeof (struct external_nlist);
  DBX_SYMCOUNT (objfile) = bfd_section_size (sym_bfd, stabsect)
    / DBX_SYMBOL_SIZE (objfile);
  DBX_STRINGTAB_SIZE (objfile) = bfd_section_size (sym_bfd, stabstrsect);
  DBX_SYMTAB_OFFSET (objfile) = stabsect->filepos;	/* XXX - FIXME: POKING INSIDE BFD DATA STRUCTURES */

  if (DBX_STRINGTAB_SIZE (objfile) > bfd_get_size (sym_bfd))
    error ("ridiculous string table size: %d bytes", DBX_STRINGTAB_SIZE (objfile));
  DBX_STRINGTAB (objfile) = (char *)
    obstack_alloc (&objfile->objfile_obstack, DBX_STRINGTAB_SIZE (objfile) + 1);
  OBJSTAT (objfile, sz_strtab += DBX_STRINGTAB_SIZE (objfile) + 1);

  /* Now read in the string table in one big gulp.  */

  val = bfd_get_section_contents (sym_bfd,	/* bfd */
				  stabstrsect,	/* bfd section */
				  DBX_STRINGTAB (objfile),	/* input buffer */
				  0,	/* offset into section */
				  DBX_STRINGTAB_SIZE (objfile));	/* amount to read */

  if (!val)
    perror_with_name (name);

  stabsread_new_init ();
  buildsym_new_init ();
  free_header_files ();
  init_header_files ();

  /* Now, do an incremental load */

  processing_acc_compilation = 1;
  dbx_symfile_read (objfile, 0);
}

static struct sym_fns aout_sym_fns =
{
  bfd_target_aout_flavour,
  dbx_new_init,			/* sym_new_init: init anything gbl to entire symtab */
  dbx_symfile_init,		/* sym_init: read initial info, setup for sym_read() */
  dbx_symfile_read,		/* sym_read: read a symbol file into symtab */
  dbx_symfile_finish,		/* sym_finish: finished with file, cleanup */
  default_symfile_offsets,	/* sym_offsets: parse user's offsets to internal form */
  NULL				/* next: pointer to next struct sym_fns */
};

void
_initialize_dbxread (void)
{
  add_symtab_fns (&aout_sym_fns);
}
