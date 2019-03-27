/* DWARF 2 debugging format support for GDB.
   Copyright 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003,
   2004
   Free Software Foundation, Inc.

   Adapted by Gary Funck (gary@intrepid.com), Intrepid Technology,
   Inc.  with support from Florida State University (under contract
   with the Ada Joint Program Office), and Silicon Graphics, Inc.
   Initial contribution by Brent Benson, Harris Computer Systems, Inc.,
   based on Fred Fish's (Cygnus Support) implementation of DWARF 1
   support in dwarfread.c

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#include "defs.h"
#include "bfd.h"
#include "symtab.h"
#include "gdbtypes.h"
#include "objfiles.h"
#include "elf/dwarf2.h"
#include "buildsym.h"
#include "demangle.h"
#include "expression.h"
#include "filenames.h"	/* for DOSish file names */
#include "macrotab.h"
#include "language.h"
#include "complaints.h"
#include "bcache.h"
#include "dwarf2expr.h"
#include "dwarf2loc.h"
#include "cp-support.h"

#include <fcntl.h>
#include "gdb_string.h"
#include "gdb_assert.h"
#include <sys/types.h>

#ifndef DWARF2_REG_TO_REGNUM
#define DWARF2_REG_TO_REGNUM(REG) (REG)
#endif

#if 0
/* .debug_info header for a compilation unit
   Because of alignment constraints, this structure has padding and cannot
   be mapped directly onto the beginning of the .debug_info section.  */
typedef struct comp_unit_header
  {
    unsigned int length;	/* length of the .debug_info
				   contribution */
    unsigned short version;	/* version number -- 2 for DWARF
				   version 2 */
    unsigned int abbrev_offset;	/* offset into .debug_abbrev section */
    unsigned char addr_size;	/* byte size of an address -- 4 */
  }
_COMP_UNIT_HEADER;
#define _ACTUAL_COMP_UNIT_HEADER_SIZE 11
#endif

/* .debug_pubnames header
   Because of alignment constraints, this structure has padding and cannot
   be mapped directly onto the beginning of the .debug_info section.  */
typedef struct pubnames_header
  {
    unsigned int length;	/* length of the .debug_pubnames
				   contribution  */
    unsigned char version;	/* version number -- 2 for DWARF
				   version 2 */
    unsigned int info_offset;	/* offset into .debug_info section */
    unsigned int info_size;	/* byte size of .debug_info section
				   portion */
  }
_PUBNAMES_HEADER;
#define _ACTUAL_PUBNAMES_HEADER_SIZE 13

/* .debug_pubnames header
   Because of alignment constraints, this structure has padding and cannot
   be mapped directly onto the beginning of the .debug_info section.  */
typedef struct aranges_header
  {
    unsigned int length;	/* byte len of the .debug_aranges
				   contribution */
    unsigned short version;	/* version number -- 2 for DWARF
				   version 2 */
    unsigned int info_offset;	/* offset into .debug_info section */
    unsigned char addr_size;	/* byte size of an address */
    unsigned char seg_size;	/* byte size of segment descriptor */
  }
_ARANGES_HEADER;
#define _ACTUAL_ARANGES_HEADER_SIZE 12

/* .debug_line statement program prologue
   Because of alignment constraints, this structure has padding and cannot
   be mapped directly onto the beginning of the .debug_info section.  */
typedef struct statement_prologue
  {
    unsigned int total_length;	/* byte length of the statement
				   information */
    unsigned short version;	/* version number -- 2 for DWARF
				   version 2 */
    unsigned int prologue_length;	/* # bytes between prologue &
					   stmt program */
    unsigned char minimum_instruction_length;	/* byte size of
						   smallest instr */
    unsigned char default_is_stmt;	/* initial value of is_stmt
					   register */
    char line_base;
    unsigned char line_range;
    unsigned char opcode_base;	/* number assigned to first special
				   opcode */
    unsigned char *standard_opcode_lengths;
  }
_STATEMENT_PROLOGUE;

/* offsets and sizes of debugging sections */

static unsigned int dwarf_info_size;
static unsigned int dwarf_abbrev_size;
static unsigned int dwarf_line_size;
static unsigned int dwarf_pubnames_size;
static unsigned int dwarf_aranges_size;
static unsigned int dwarf_loc_size;
static unsigned int dwarf_macinfo_size;
static unsigned int dwarf_str_size;
static unsigned int dwarf_ranges_size;
unsigned int dwarf_frame_size;
unsigned int dwarf_eh_frame_size;

static asection *dwarf_info_section;
static asection *dwarf_abbrev_section;
static asection *dwarf_line_section;
static asection *dwarf_pubnames_section;
static asection *dwarf_aranges_section;
static asection *dwarf_loc_section;
static asection *dwarf_macinfo_section;
static asection *dwarf_str_section;
static asection *dwarf_ranges_section;
asection *dwarf_frame_section;
asection *dwarf_eh_frame_section;

/* names of the debugging sections */

#define INFO_SECTION     ".debug_info"
#define ABBREV_SECTION   ".debug_abbrev"
#define LINE_SECTION     ".debug_line"
#define PUBNAMES_SECTION ".debug_pubnames"
#define ARANGES_SECTION  ".debug_aranges"
#define LOC_SECTION      ".debug_loc"
#define MACINFO_SECTION  ".debug_macinfo"
#define STR_SECTION      ".debug_str"
#define RANGES_SECTION   ".debug_ranges"
#define FRAME_SECTION    ".debug_frame"
#define EH_FRAME_SECTION ".eh_frame"

/* local data types */

/* We hold several abbreviation tables in memory at the same time. */
#ifndef ABBREV_HASH_SIZE
#define ABBREV_HASH_SIZE 121
#endif

/* The data in a compilation unit header, after target2host
   translation, looks like this.  */
struct comp_unit_head
  {
    unsigned long length;
    short version;
    unsigned int abbrev_offset;
    unsigned char addr_size;
    unsigned char signed_addr_p;
    unsigned int offset_size;	/* size of file offsets; either 4 or 8 */
    unsigned int initial_length_size; /* size of the length field; either
                                         4 or 12 */

    /* Offset to the first byte of this compilation unit header in the 
     * .debug_info section, for resolving relative reference dies. */

    unsigned int offset;

    /* Pointer to this compilation unit header in the .debug_info
     * section */

    char *cu_head_ptr;

    /* Pointer to the first die of this compilatio unit.  This will
     * be the first byte following the compilation unit header. */

    char *first_die_ptr;

    /* Pointer to the next compilation unit header in the program. */

    struct comp_unit_head *next;

    /* DWARF abbreviation table associated with this compilation unit */

    struct abbrev_info *dwarf2_abbrevs[ABBREV_HASH_SIZE];

    /* Base address of this compilation unit.  */

    CORE_ADDR base_address;

    /* Non-zero if base_address has been set.  */

    int base_known;
  };

/* Internal state when decoding a particular compilation unit.  */
struct dwarf2_cu
{
  /* The objfile containing this compilation unit.  */
  struct objfile *objfile;

  /* The header of the compilation unit.

     FIXME drow/2003-11-10: Some of the things from the comp_unit_head
     should be moved to the dwarf2_cu structure; for instance the abbrevs
     hash table.  */
  struct comp_unit_head header;

  struct function_range *first_fn, *last_fn, *cached_fn;

  /* The language we are debugging.  */
  enum language language;
  const struct language_defn *language_defn;

  /* The generic symbol table building routines have separate lists for
     file scope symbols and all all other scopes (local scopes).  So
     we need to select the right one to pass to add_symbol_to_list().
     We do it by keeping a pointer to the correct list in list_in_scope.

     FIXME: The original dwarf code just treated the file scope as the
     first local scope, and all other local scopes as nested local
     scopes, and worked fine.  Check to see if we really need to
     distinguish these in buildsym.c.  */
  struct pending **list_in_scope;

  /* Maintain an array of referenced fundamental types for the current
     compilation unit being read.  For DWARF version 1, we have to construct
     the fundamental types on the fly, since no information about the
     fundamental types is supplied.  Each such fundamental type is created by
     calling a language dependent routine to create the type, and then a
     pointer to that type is then placed in the array at the index specified
     by it's FT_<TYPENAME> value.  The array has a fixed size set by the
     FT_NUM_MEMBERS compile time constant, which is the number of predefined
     fundamental types gdb knows how to construct.  */
  struct type *ftypes[FT_NUM_MEMBERS];	/* Fundamental types */
};

/* The line number information for a compilation unit (found in the
   .debug_line section) begins with a "statement program header",
   which contains the following information.  */
struct line_header
{
  unsigned int total_length;
  unsigned short version;
  unsigned int header_length;
  unsigned char minimum_instruction_length;
  unsigned char default_is_stmt;
  int line_base;
  unsigned char line_range;
  unsigned char opcode_base;

  /* standard_opcode_lengths[i] is the number of operands for the
     standard opcode whose value is i.  This means that
     standard_opcode_lengths[0] is unused, and the last meaningful
     element is standard_opcode_lengths[opcode_base - 1].  */
  unsigned char *standard_opcode_lengths;

  /* The include_directories table.  NOTE!  These strings are not
     allocated with xmalloc; instead, they are pointers into
     debug_line_buffer.  If you try to free them, `free' will get
     indigestion.  */
  unsigned int num_include_dirs, include_dirs_size;
  char **include_dirs;

  /* The file_names table.  NOTE!  These strings are not allocated
     with xmalloc; instead, they are pointers into debug_line_buffer.
     Don't try to free them directly.  */
  unsigned int num_file_names, file_names_size;
  struct file_entry
  {
    char *name;
    unsigned int dir_index;
    unsigned int mod_time;
    unsigned int length;
  } *file_names;

  /* The start and end of the statement program following this
     header.  These point into dwarf_line_buffer.  */
  char *statement_program_start, *statement_program_end;
};

/* When we construct a partial symbol table entry we only
   need this much information. */
struct partial_die_info
  {
    enum dwarf_tag tag;
    unsigned char has_children;
    unsigned char is_external;
    unsigned char is_declaration;
    unsigned char has_type;
    unsigned int offset;
    unsigned int abbrev;
    char *name;
    int has_pc_info;
    CORE_ADDR lowpc;
    CORE_ADDR highpc;
    struct dwarf_block *locdesc;
    unsigned int language;
    char *sibling;
  };

/* This data structure holds the information of an abbrev. */
struct abbrev_info
  {
    unsigned int number;	/* number identifying abbrev */
    enum dwarf_tag tag;		/* dwarf tag */
    int has_children;		/* boolean */
    unsigned int num_attrs;	/* number of attributes */
    struct attr_abbrev *attrs;	/* an array of attribute descriptions */
    struct abbrev_info *next;	/* next in chain */
  };

struct attr_abbrev
  {
    enum dwarf_attribute name;
    enum dwarf_form form;
  };

/* This data structure holds a complete die structure. */
struct die_info
  {
    enum dwarf_tag tag;		/* Tag indicating type of die */
    unsigned int abbrev;	/* Abbrev number */
    unsigned int offset;	/* Offset in .debug_info section */
    unsigned int num_attrs;	/* Number of attributes */
    struct attribute *attrs;	/* An array of attributes */
    struct die_info *next_ref;	/* Next die in ref hash table */

    /* The dies in a compilation unit form an n-ary tree.  PARENT
       points to this die's parent; CHILD points to the first child of
       this node; and all the children of a given node are chained
       together via their SIBLING fields, terminated by a die whose
       tag is zero.  */
    struct die_info *child;	/* Its first child, if any.  */
    struct die_info *sibling;	/* Its next sibling, if any.  */
    struct die_info *parent;	/* Its parent, if any.  */

    struct type *type;		/* Cached type information */
  };

/* Attributes have a name and a value */
struct attribute
  {
    enum dwarf_attribute name;
    enum dwarf_form form;
    union
      {
	char *str;
	struct dwarf_block *blk;
	unsigned long unsnd;
	long int snd;
	CORE_ADDR addr;
      }
    u;
  };

struct function_range
{
  const char *name;
  CORE_ADDR lowpc, highpc;
  int seen_line;
  struct function_range *next;
};

/* Get at parts of an attribute structure */

#define DW_STRING(attr)    ((attr)->u.str)
#define DW_UNSND(attr)     ((attr)->u.unsnd)
#define DW_BLOCK(attr)     ((attr)->u.blk)
#define DW_SND(attr)       ((attr)->u.snd)
#define DW_ADDR(attr)	   ((attr)->u.addr)

/* Blocks are a bunch of untyped bytes. */
struct dwarf_block
  {
    unsigned int size;
    char *data;
  };

#ifndef ATTR_ALLOC_CHUNK
#define ATTR_ALLOC_CHUNK 4
#endif

/* A hash table of die offsets for following references.  */
#ifndef REF_HASH_SIZE
#define REF_HASH_SIZE 1021
#endif

static struct die_info *die_ref_table[REF_HASH_SIZE];

/* Obstack for allocating temporary storage used during symbol reading.  */
static struct obstack dwarf2_tmp_obstack;

/* Allocate fields for structs, unions and enums in this size.  */
#ifndef DW_FIELD_ALLOC_CHUNK
#define DW_FIELD_ALLOC_CHUNK 4
#endif

/* Actually data from the sections.  */
static char *dwarf_info_buffer;
static char *dwarf_abbrev_buffer;
static char *dwarf_line_buffer;
static char *dwarf_str_buffer;
static char *dwarf_macinfo_buffer;
static char *dwarf_ranges_buffer;
static char *dwarf_loc_buffer;

/* A zeroed version of a partial die for initialization purposes.  */
static struct partial_die_info zeroed_partial_die;

/* FIXME: decode_locdesc sets these variables to describe the location
   to the caller.  These ought to be a structure or something.   If
   none of the flags are set, the object lives at the address returned
   by decode_locdesc.  */

static int isreg;		/* Object lives in register.
				   decode_locdesc's return value is
				   the register number.  */

/* We put a pointer to this structure in the read_symtab_private field
   of the psymtab.
   The complete dwarf information for an objfile is kept in the
   objfile_obstack, so that absolute die references can be handled.
   Most of the information in this structure is related to an entire
   object file and could be passed via the sym_private field of the objfile.
   It is however conceivable that dwarf2 might not be the only type
   of symbols read from an object file.  */

struct dwarf2_pinfo
  {
    /* Pointer to start of dwarf info buffer for the objfile.  */

    char *dwarf_info_buffer;

    /* Offset in dwarf_info_buffer for this compilation unit. */

    unsigned long dwarf_info_offset;

    /* Pointer to start of dwarf abbreviation buffer for the objfile.  */

    char *dwarf_abbrev_buffer;

    /* Size of dwarf abbreviation section for the objfile.  */

    unsigned int dwarf_abbrev_size;

    /* Pointer to start of dwarf line buffer for the objfile.  */

    char *dwarf_line_buffer;

    /* Size of dwarf_line_buffer, in bytes.  */
    
    unsigned int dwarf_line_size;

    /* Pointer to start of dwarf string buffer for the objfile.  */

    char *dwarf_str_buffer;

    /* Size of dwarf string section for the objfile.  */

    unsigned int dwarf_str_size;

    /* Pointer to start of dwarf macro buffer for the objfile.  */

    char *dwarf_macinfo_buffer;

    /* Size of dwarf macinfo section for the objfile.  */
    
    unsigned int dwarf_macinfo_size;

    /* Pointer to start of dwarf ranges buffer for the objfile.  */

    char *dwarf_ranges_buffer;

    /* Size of dwarf ranges buffer for the objfile.  */

    unsigned int dwarf_ranges_size;

    /* Pointer to start of dwarf locations buffer for the objfile.  */

    char *dwarf_loc_buffer;

    /* Size of dwarf locations buffer for the objfile.  */

    unsigned int dwarf_loc_size;
  };

#define PST_PRIVATE(p) ((struct dwarf2_pinfo *)(p)->read_symtab_private)
#define DWARF_INFO_BUFFER(p) (PST_PRIVATE(p)->dwarf_info_buffer)
#define DWARF_INFO_OFFSET(p) (PST_PRIVATE(p)->dwarf_info_offset)
#define DWARF_ABBREV_BUFFER(p) (PST_PRIVATE(p)->dwarf_abbrev_buffer)
#define DWARF_ABBREV_SIZE(p) (PST_PRIVATE(p)->dwarf_abbrev_size)
#define DWARF_LINE_BUFFER(p) (PST_PRIVATE(p)->dwarf_line_buffer)
#define DWARF_LINE_SIZE(p)   (PST_PRIVATE(p)->dwarf_line_size)
#define DWARF_STR_BUFFER(p)  (PST_PRIVATE(p)->dwarf_str_buffer)
#define DWARF_STR_SIZE(p)    (PST_PRIVATE(p)->dwarf_str_size)
#define DWARF_MACINFO_BUFFER(p) (PST_PRIVATE(p)->dwarf_macinfo_buffer)
#define DWARF_MACINFO_SIZE(p)   (PST_PRIVATE(p)->dwarf_macinfo_size)
#define DWARF_RANGES_BUFFER(p)  (PST_PRIVATE(p)->dwarf_ranges_buffer)
#define DWARF_RANGES_SIZE(p)    (PST_PRIVATE(p)->dwarf_ranges_size)
#define DWARF_LOC_BUFFER(p)     (PST_PRIVATE(p)->dwarf_loc_buffer)
#define DWARF_LOC_SIZE(p)       (PST_PRIVATE(p)->dwarf_loc_size)

/* FIXME: We might want to set this from BFD via bfd_arch_bits_per_byte,
   but this would require a corresponding change in unpack_field_as_long
   and friends.  */
static int bits_per_byte = 8;

/* The routines that read and process dies for a C struct or C++ class
   pass lists of data member fields and lists of member function fields
   in an instance of a field_info structure, as defined below.  */
struct field_info
  {
    /* List of data member and baseclasses fields. */
    struct nextfield
      {
	struct nextfield *next;
	int accessibility;
	int virtuality;
	struct field field;
      }
     *fields;

    /* Number of fields.  */
    int nfields;

    /* Number of baseclasses.  */
    int nbaseclasses;

    /* Set if the accesibility of one of the fields is not public.  */
    int non_public_fields;

    /* Member function fields array, entries are allocated in the order they
       are encountered in the object file.  */
    struct nextfnfield
      {
	struct nextfnfield *next;
	struct fn_field fnfield;
      }
     *fnfields;

    /* Member function fieldlist array, contains name of possibly overloaded
       member function, number of overloaded member functions and a pointer
       to the head of the member function field chain.  */
    struct fnfieldlist
      {
	char *name;
	int length;
	struct nextfnfield *head;
      }
     *fnfieldlists;

    /* Number of entries in the fnfieldlists array.  */
    int nfnfields;
  };

/* Various complaints about symbol reading that don't abort the process */

static void
dwarf2_statement_list_fits_in_line_number_section_complaint (void)
{
  complaint (&symfile_complaints,
	     "statement list doesn't fit in .debug_line section");
}

static void
dwarf2_complex_location_expr_complaint (void)
{
  complaint (&symfile_complaints, "location expression too complex");
}

static void
dwarf2_const_value_length_mismatch_complaint (const char *arg1, int arg2,
					      int arg3)
{
  complaint (&symfile_complaints,
	     "const value length mismatch for '%s', got %d, expected %d", arg1,
	     arg2, arg3);
}

static void
dwarf2_macros_too_long_complaint (void)
{
  complaint (&symfile_complaints,
	     "macro info runs off end of `.debug_macinfo' section");
}

static void
dwarf2_macro_malformed_definition_complaint (const char *arg1)
{
  complaint (&symfile_complaints,
	     "macro debug info contains a malformed macro definition:\n`%s'",
	     arg1);
}

static void
dwarf2_invalid_attrib_class_complaint (const char *arg1, const char *arg2)
{
  complaint (&symfile_complaints,
	     "invalid attribute class or form for '%s' in '%s'", arg1, arg2);
}

/* local function prototypes */

static void dwarf2_locate_sections (bfd *, asection *, void *);

#if 0
static void dwarf2_build_psymtabs_easy (struct objfile *, int);
#endif

static void dwarf2_build_psymtabs_hard (struct objfile *, int);

static char *scan_partial_symbols (char *, CORE_ADDR *, CORE_ADDR *,
				   struct dwarf2_cu *,
				   const char *namespace);

static void add_partial_symbol (struct partial_die_info *, struct dwarf2_cu *,
				const char *namespace);

static int pdi_needs_namespace (enum dwarf_tag tag, const char *namespace);

static char *add_partial_namespace (struct partial_die_info *pdi,
				    char *info_ptr,
				    CORE_ADDR *lowpc, CORE_ADDR *highpc,
				    struct dwarf2_cu *cu,
				    const char *namespace);

static char *add_partial_structure (struct partial_die_info *struct_pdi,
				    char *info_ptr,
				    struct dwarf2_cu *cu,
				    const char *namespace);

static char *add_partial_enumeration (struct partial_die_info *enum_pdi,
				      char *info_ptr,
				      struct dwarf2_cu *cu,
				      const char *namespace);

static char *locate_pdi_sibling (struct partial_die_info *orig_pdi,
				 char *info_ptr,
				 bfd *abfd,
				 struct dwarf2_cu *cu);

static void dwarf2_psymtab_to_symtab (struct partial_symtab *);

static void psymtab_to_symtab_1 (struct partial_symtab *);

char *dwarf2_read_section (struct objfile *, asection *);

static void dwarf2_read_abbrevs (bfd *abfd, struct dwarf2_cu *cu);

static void dwarf2_empty_abbrev_table (void *);

static struct abbrev_info *dwarf2_lookup_abbrev (unsigned int,
						 struct dwarf2_cu *);

static char *read_partial_die (struct partial_die_info *,
			       bfd *, char *, struct dwarf2_cu *);

static char *read_full_die (struct die_info **, bfd *, char *,
			    struct dwarf2_cu *, int *);

static char *read_attribute (struct attribute *, struct attr_abbrev *,
			     bfd *, char *, struct dwarf2_cu *);

static char *read_attribute_value (struct attribute *, unsigned,
			     bfd *, char *, struct dwarf2_cu *);

static unsigned int read_1_byte (bfd *, char *);

static int read_1_signed_byte (bfd *, char *);

static unsigned int read_2_bytes (bfd *, char *);

static unsigned int read_4_bytes (bfd *, char *);

static unsigned long read_8_bytes (bfd *, char *);

static CORE_ADDR read_address (bfd *, char *ptr, struct dwarf2_cu *,
			       int *bytes_read);

static LONGEST read_initial_length (bfd *, char *,
                                    struct comp_unit_head *, int *bytes_read);

static LONGEST read_offset (bfd *, char *, const struct comp_unit_head *,
                            int *bytes_read);

static char *read_n_bytes (bfd *, char *, unsigned int);

static char *read_string (bfd *, char *, unsigned int *);

static char *read_indirect_string (bfd *, char *, const struct comp_unit_head *,
				   unsigned int *);

static unsigned long read_unsigned_leb128 (bfd *, char *, unsigned int *);

static long read_signed_leb128 (bfd *, char *, unsigned int *);

static void set_cu_language (unsigned int, struct dwarf2_cu *);

static struct attribute *dwarf2_attr (struct die_info *, unsigned int,
				      struct dwarf2_cu *);

static int die_is_declaration (struct die_info *, struct dwarf2_cu *cu);

static struct die_info *die_specification (struct die_info *die,
					   struct dwarf2_cu *);

static void free_line_header (struct line_header *lh);

static struct line_header *(dwarf_decode_line_header
                            (unsigned int offset,
                             bfd *abfd, struct dwarf2_cu *cu));

static void dwarf_decode_lines (struct line_header *, char *, bfd *,
				struct dwarf2_cu *);

static void dwarf2_start_subfile (char *, char *);

static struct symbol *new_symbol (struct die_info *, struct type *,
				  struct dwarf2_cu *);

static void dwarf2_const_value (struct attribute *, struct symbol *,
				struct dwarf2_cu *);

static void dwarf2_const_value_data (struct attribute *attr,
				     struct symbol *sym,
				     int bits);

static struct type *die_type (struct die_info *, struct dwarf2_cu *);

static struct type *die_containing_type (struct die_info *,
					 struct dwarf2_cu *);

#if 0
static struct type *type_at_offset (unsigned int, struct objfile *);
#endif

static struct type *tag_type_to_type (struct die_info *, struct dwarf2_cu *);

static void read_type_die (struct die_info *, struct dwarf2_cu *);

static char *determine_prefix (struct die_info *die, struct dwarf2_cu *);

static char *typename_concat (const char *prefix, const char *suffix);

static void read_typedef (struct die_info *, struct dwarf2_cu *);

static void read_base_type (struct die_info *, struct dwarf2_cu *);

static void read_subrange_type (struct die_info *die, struct dwarf2_cu *cu);

static void read_file_scope (struct die_info *, struct dwarf2_cu *);

static void read_func_scope (struct die_info *, struct dwarf2_cu *);

static void read_lexical_block_scope (struct die_info *, struct dwarf2_cu *);

static int dwarf2_get_pc_bounds (struct die_info *,
				 CORE_ADDR *, CORE_ADDR *, struct dwarf2_cu *);

static void get_scope_pc_bounds (struct die_info *,
				 CORE_ADDR *, CORE_ADDR *,
				 struct dwarf2_cu *);

static void dwarf2_add_field (struct field_info *, struct die_info *,
			      struct dwarf2_cu *);

static void dwarf2_attach_fields_to_type (struct field_info *,
					  struct type *, struct dwarf2_cu *);

static void dwarf2_add_member_fn (struct field_info *,
				  struct die_info *, struct type *,
				  struct dwarf2_cu *);

static void dwarf2_attach_fn_fields_to_type (struct field_info *,
					     struct type *, struct dwarf2_cu *);

static void read_structure_type (struct die_info *, struct dwarf2_cu *);

static void process_structure_scope (struct die_info *, struct dwarf2_cu *);

static char *determine_class_name (struct die_info *die, struct dwarf2_cu *cu);

static void read_common_block (struct die_info *, struct dwarf2_cu *);

static void read_namespace (struct die_info *die, struct dwarf2_cu *);

static const char *namespace_name (struct die_info *die,
				   int *is_anonymous, struct dwarf2_cu *);

static void read_enumeration_type (struct die_info *, struct dwarf2_cu *);

static void process_enumeration_scope (struct die_info *, struct dwarf2_cu *);

static struct type *dwarf_base_type (int, int, struct dwarf2_cu *);

static CORE_ADDR decode_locdesc (struct dwarf_block *, struct dwarf2_cu *);

static void read_array_type (struct die_info *, struct dwarf2_cu *);

static void read_tag_pointer_type (struct die_info *, struct dwarf2_cu *);

static void read_tag_unspecified_type (struct die_info *, struct dwarf2_cu *);

static void read_tag_ptr_to_member_type (struct die_info *,
					 struct dwarf2_cu *);

static void read_tag_reference_type (struct die_info *, struct dwarf2_cu *);

static void read_tag_const_type (struct die_info *, struct dwarf2_cu *);

static void read_tag_volatile_type (struct die_info *, struct dwarf2_cu *);

static void read_tag_restrict_type (struct die_info *, struct dwarf2_cu *);

static void read_tag_string_type (struct die_info *, struct dwarf2_cu *);

static void read_subroutine_type (struct die_info *, struct dwarf2_cu *);

static struct die_info *read_comp_unit (char *, bfd *, struct dwarf2_cu *);

static struct die_info *read_die_and_children (char *info_ptr, bfd *abfd,
					       struct dwarf2_cu *,
					       char **new_info_ptr,
					       struct die_info *parent);

static struct die_info *read_die_and_siblings (char *info_ptr, bfd *abfd,
					       struct dwarf2_cu *,
					       char **new_info_ptr,
					       struct die_info *parent);

static void free_die_list (struct die_info *);

static struct cleanup *make_cleanup_free_die_list (struct die_info *);

static void process_die (struct die_info *, struct dwarf2_cu *);

static char *dwarf2_linkage_name (struct die_info *, struct dwarf2_cu *);

static char *dwarf2_name (struct die_info *die, struct dwarf2_cu *);

static struct die_info *dwarf2_extension (struct die_info *die,
					  struct dwarf2_cu *);

static char *dwarf_tag_name (unsigned int);

static char *dwarf_attr_name (unsigned int);

static char *dwarf_form_name (unsigned int);

static char *dwarf_stack_op_name (unsigned int);

static char *dwarf_bool_name (unsigned int);

static char *dwarf_type_encoding_name (unsigned int);

#if 0
static char *dwarf_cfi_name (unsigned int);

struct die_info *copy_die (struct die_info *);
#endif

static struct die_info *sibling_die (struct die_info *);

static void dump_die (struct die_info *);

static void dump_die_list (struct die_info *);

static void store_in_ref_table (unsigned int, struct die_info *);

static void dwarf2_empty_hash_tables (void);

static unsigned int dwarf2_get_ref_die_offset (struct attribute *,
					       struct dwarf2_cu *);

static int dwarf2_get_attr_constant_value (struct attribute *, int);

static struct die_info *follow_die_ref (unsigned int);

static struct type *dwarf2_fundamental_type (struct objfile *, int,
					     struct dwarf2_cu *);

/* memory allocation interface */

static void dwarf2_free_tmp_obstack (void *);

static struct dwarf_block *dwarf_alloc_block (void);

static struct abbrev_info *dwarf_alloc_abbrev (void);

static struct die_info *dwarf_alloc_die (void);

static void initialize_cu_func_list (struct dwarf2_cu *);

static void add_to_cu_func_list (const char *, CORE_ADDR, CORE_ADDR,
				 struct dwarf2_cu *);

static void dwarf_decode_macros (struct line_header *, unsigned int,
                                 char *, bfd *, struct dwarf2_cu *);

static int attr_form_is_block (struct attribute *);

static void
dwarf2_symbol_mark_computed (struct attribute *attr, struct symbol *sym,
			     struct dwarf2_cu *cu);

/* Try to locate the sections we need for DWARF 2 debugging
   information and return true if we have enough to do something.  */

int
dwarf2_has_info (bfd *abfd)
{
  dwarf_info_section = 0;
  dwarf_abbrev_section = 0;
  dwarf_line_section = 0;
  dwarf_str_section = 0;
  dwarf_macinfo_section = 0;
  dwarf_frame_section = 0;
  dwarf_eh_frame_section = 0;
  dwarf_ranges_section = 0;
  dwarf_loc_section = 0;
  
  bfd_map_over_sections (abfd, dwarf2_locate_sections, NULL);
  return (dwarf_info_section != NULL && dwarf_abbrev_section != NULL);
}

/* This function is mapped across the sections and remembers the
   offset and size of each of the debugging sections we are interested
   in.  */

static void
dwarf2_locate_sections (bfd *ignore_abfd, asection *sectp, void *ignore_ptr)
{
  if (strcmp (sectp->name, INFO_SECTION) == 0)
    {
      dwarf_info_size = bfd_get_section_size (sectp);
      dwarf_info_section = sectp;
    }
  else if (strcmp (sectp->name, ABBREV_SECTION) == 0)
    {
      dwarf_abbrev_size = bfd_get_section_size (sectp);
      dwarf_abbrev_section = sectp;
    }
  else if (strcmp (sectp->name, LINE_SECTION) == 0)
    {
      dwarf_line_size = bfd_get_section_size (sectp);
      dwarf_line_section = sectp;
    }
  else if (strcmp (sectp->name, PUBNAMES_SECTION) == 0)
    {
      dwarf_pubnames_size = bfd_get_section_size (sectp);
      dwarf_pubnames_section = sectp;
    }
  else if (strcmp (sectp->name, ARANGES_SECTION) == 0)
    {
      dwarf_aranges_size = bfd_get_section_size (sectp);
      dwarf_aranges_section = sectp;
    }
  else if (strcmp (sectp->name, LOC_SECTION) == 0)
    {
      dwarf_loc_size = bfd_get_section_size (sectp);
      dwarf_loc_section = sectp;
    }
  else if (strcmp (sectp->name, MACINFO_SECTION) == 0)
    {
      dwarf_macinfo_size = bfd_get_section_size (sectp);
      dwarf_macinfo_section = sectp;
    }
  else if (strcmp (sectp->name, STR_SECTION) == 0)
    {
      dwarf_str_size = bfd_get_section_size (sectp);
      dwarf_str_section = sectp;
    }
  else if (strcmp (sectp->name, FRAME_SECTION) == 0)
    {
      dwarf_frame_size = bfd_get_section_size (sectp);
      dwarf_frame_section = sectp;
    }
  else if (strcmp (sectp->name, EH_FRAME_SECTION) == 0)
    {
      flagword aflag = bfd_get_section_flags (ignore_abfd, sectp);
      if (aflag & SEC_HAS_CONTENTS)
        {
          dwarf_eh_frame_size = bfd_get_section_size (sectp);
          dwarf_eh_frame_section = sectp;
        }
    }
  else if (strcmp (sectp->name, RANGES_SECTION) == 0)
    {
      dwarf_ranges_size = bfd_get_section_size (sectp);
      dwarf_ranges_section = sectp;
    }
}

/* Build a partial symbol table.  */

void
dwarf2_build_psymtabs (struct objfile *objfile, int mainline)
{

  /* We definitely need the .debug_info and .debug_abbrev sections */

  dwarf_info_buffer = dwarf2_read_section (objfile, dwarf_info_section);
  dwarf_abbrev_buffer = dwarf2_read_section (objfile, dwarf_abbrev_section);

  if (dwarf_line_section)
    dwarf_line_buffer = dwarf2_read_section (objfile, dwarf_line_section);
  else
    dwarf_line_buffer = NULL;

  if (dwarf_str_section)
    dwarf_str_buffer = dwarf2_read_section (objfile, dwarf_str_section);
  else
    dwarf_str_buffer = NULL;

  if (dwarf_macinfo_section)
    dwarf_macinfo_buffer = dwarf2_read_section (objfile,
						dwarf_macinfo_section);
  else
    dwarf_macinfo_buffer = NULL;

  if (dwarf_ranges_section)
    dwarf_ranges_buffer = dwarf2_read_section (objfile, dwarf_ranges_section);
  else
    dwarf_ranges_buffer = NULL;

  if (dwarf_loc_section)
    dwarf_loc_buffer = dwarf2_read_section (objfile, dwarf_loc_section);
  else
    dwarf_loc_buffer = NULL;

  if (mainline
      || (objfile->global_psymbols.size == 0
	  && objfile->static_psymbols.size == 0))
    {
      init_psymbol_list (objfile, 1024);
    }

#if 0
  if (dwarf_aranges_offset && dwarf_pubnames_offset)
    {
      /* Things are significantly easier if we have .debug_aranges and
         .debug_pubnames sections */

      dwarf2_build_psymtabs_easy (objfile, mainline);
    }
  else
#endif
    /* only test this case for now */
    {
      /* In this case we have to work a bit harder */
      dwarf2_build_psymtabs_hard (objfile, mainline);
    }
}

#if 0
/* Build the partial symbol table from the information in the
   .debug_pubnames and .debug_aranges sections.  */

static void
dwarf2_build_psymtabs_easy (struct objfile *objfile, int mainline)
{
  bfd *abfd = objfile->obfd;
  char *aranges_buffer, *pubnames_buffer;
  char *aranges_ptr, *pubnames_ptr;
  unsigned int entry_length, version, info_offset, info_size;

  pubnames_buffer = dwarf2_read_section (objfile,
					 dwarf_pubnames_section);
  pubnames_ptr = pubnames_buffer;
  while ((pubnames_ptr - pubnames_buffer) < dwarf_pubnames_size)
    {
      struct comp_unit_head cu_header;
      int bytes_read;

      entry_length = read_initial_length (abfd, pubnames_ptr, &cu_header,
                                         &bytes_read);
      pubnames_ptr += bytes_read;
      version = read_1_byte (abfd, pubnames_ptr);
      pubnames_ptr += 1;
      info_offset = read_4_bytes (abfd, pubnames_ptr);
      pubnames_ptr += 4;
      info_size = read_4_bytes (abfd, pubnames_ptr);
      pubnames_ptr += 4;
    }

  aranges_buffer = dwarf2_read_section (objfile,
					dwarf_aranges_section);

}
#endif

/* Read in the comp unit header information from the debug_info at
   info_ptr. */

static char *
read_comp_unit_head (struct comp_unit_head *cu_header,
		     char *info_ptr, bfd *abfd)
{
  int signed_addr;
  int bytes_read;
  cu_header->length = read_initial_length (abfd, info_ptr, cu_header,
                                           &bytes_read);
  info_ptr += bytes_read;
  cu_header->version = read_2_bytes (abfd, info_ptr);
  info_ptr += 2;
  cu_header->abbrev_offset = read_offset (abfd, info_ptr, cu_header,
                                          &bytes_read);
  info_ptr += bytes_read;
  cu_header->addr_size = read_1_byte (abfd, info_ptr);
  info_ptr += 1;
  signed_addr = bfd_get_sign_extend_vma (abfd);
  if (signed_addr < 0)
    internal_error (__FILE__, __LINE__,
		    "read_comp_unit_head: dwarf from non elf file");
  cu_header->signed_addr_p = signed_addr;
  return info_ptr;
}

/* Build the partial symbol table by doing a quick pass through the
   .debug_info and .debug_abbrev sections.  */

static void
dwarf2_build_psymtabs_hard (struct objfile *objfile, int mainline)
{
  /* Instead of reading this into a big buffer, we should probably use
     mmap()  on architectures that support it. (FIXME) */
  bfd *abfd = objfile->obfd;
  char *info_ptr, *abbrev_ptr;
  char *beg_of_comp_unit;
  struct partial_die_info comp_unit_die;
  struct partial_symtab *pst;
  struct cleanup *back_to;
  CORE_ADDR lowpc, highpc, baseaddr;

  info_ptr = dwarf_info_buffer;
  abbrev_ptr = dwarf_abbrev_buffer;

  /* We use dwarf2_tmp_obstack for objects that don't need to survive
     the partial symbol scan, like attribute values.

     We could reduce our peak memory consumption during partial symbol
     table construction by freeing stuff from this obstack more often
     --- say, after processing each compilation unit, or each die ---
     but it turns out that this saves almost nothing.  For an
     executable with 11Mb of Dwarf 2 data, I found about 64k allocated
     on dwarf2_tmp_obstack.  Some investigation showed:

     1) 69% of the attributes used forms DW_FORM_addr, DW_FORM_data*,
        DW_FORM_flag, DW_FORM_[su]data, and DW_FORM_ref*.  These are
        all fixed-length values not requiring dynamic allocation.

     2) 30% of the attributes used the form DW_FORM_string.  For
        DW_FORM_string, read_attribute simply hands back a pointer to
        the null-terminated string in dwarf_info_buffer, so no dynamic
        allocation is needed there either.

     3) The remaining 1% of the attributes all used DW_FORM_block1.
        75% of those were DW_AT_frame_base location lists for
        functions; the rest were DW_AT_location attributes, probably
        for the global variables.

     Anyway, what this all means is that the memory the dwarf2
     reader uses as temporary space reading partial symbols is about
     0.5% as much as we use for dwarf_*_buffer.  That's noise.  */

  obstack_init (&dwarf2_tmp_obstack);
  back_to = make_cleanup (dwarf2_free_tmp_obstack, NULL);

  /* Since the objects we're extracting from dwarf_info_buffer vary in
     length, only the individual functions to extract them (like
     read_comp_unit_head and read_partial_die) can really know whether
     the buffer is large enough to hold another complete object.

     At the moment, they don't actually check that.  If
     dwarf_info_buffer holds just one extra byte after the last
     compilation unit's dies, then read_comp_unit_head will happily
     read off the end of the buffer.  read_partial_die is similarly
     casual.  Those functions should be fixed.

     For this loop condition, simply checking whether there's any data
     left at all should be sufficient.  */
  while (info_ptr < dwarf_info_buffer + dwarf_info_size)
    {
      struct dwarf2_cu cu;
      beg_of_comp_unit = info_ptr;

      cu.objfile = objfile;
      info_ptr = read_comp_unit_head (&cu.header, info_ptr, abfd);

      if (cu.header.version != 2)
	{
	  error ("Dwarf Error: wrong version in compilation unit header (is %d, should be %d) [in module %s]", cu.header.version, 2, bfd_get_filename (abfd));
	  return;
	}
      if (cu.header.abbrev_offset >= dwarf_abbrev_size)
	{
	  error ("Dwarf Error: bad offset (0x%lx) in compilation unit header (offset 0x%lx + 6) [in module %s]",
		 (long) cu.header.abbrev_offset,
		 (long) (beg_of_comp_unit - dwarf_info_buffer),
		 bfd_get_filename (abfd));
	  return;
	}
      if (beg_of_comp_unit + cu.header.length + cu.header.initial_length_size
	  > dwarf_info_buffer + dwarf_info_size)
	{
	  error ("Dwarf Error: bad length (0x%lx) in compilation unit header (offset 0x%lx + 0) [in module %s]",
		 (long) cu.header.length,
		 (long) (beg_of_comp_unit - dwarf_info_buffer),
		 bfd_get_filename (abfd));
	  return;
	}
      /* Complete the cu_header */
      cu.header.offset = beg_of_comp_unit - dwarf_info_buffer;
      cu.header.first_die_ptr = info_ptr;
      cu.header.cu_head_ptr = beg_of_comp_unit;

      cu.list_in_scope = &file_symbols;

      /* Read the abbrevs for this compilation unit into a table */
      dwarf2_read_abbrevs (abfd, &cu);
      make_cleanup (dwarf2_empty_abbrev_table, cu.header.dwarf2_abbrevs);

      /* Read the compilation unit die */
      info_ptr = read_partial_die (&comp_unit_die, abfd, info_ptr,
				   &cu);

      /* Set the language we're debugging */
      set_cu_language (comp_unit_die.language, &cu);

      /* Allocate a new partial symbol table structure */
      pst = start_psymtab_common (objfile, objfile->section_offsets,
				  comp_unit_die.name ? comp_unit_die.name : "",
				  comp_unit_die.lowpc,
				  objfile->global_psymbols.next,
				  objfile->static_psymbols.next);

      pst->read_symtab_private = (char *)
	obstack_alloc (&objfile->objfile_obstack, sizeof (struct dwarf2_pinfo));
      DWARF_INFO_BUFFER (pst) = dwarf_info_buffer;
      DWARF_INFO_OFFSET (pst) = beg_of_comp_unit - dwarf_info_buffer;
      DWARF_ABBREV_BUFFER (pst) = dwarf_abbrev_buffer;
      DWARF_ABBREV_SIZE (pst) = dwarf_abbrev_size;
      DWARF_LINE_BUFFER (pst) = dwarf_line_buffer;
      DWARF_LINE_SIZE (pst) = dwarf_line_size;
      DWARF_STR_BUFFER (pst) = dwarf_str_buffer;
      DWARF_STR_SIZE (pst) = dwarf_str_size;
      DWARF_MACINFO_BUFFER (pst) = dwarf_macinfo_buffer;
      DWARF_MACINFO_SIZE (pst) = dwarf_macinfo_size;
      DWARF_RANGES_BUFFER (pst) = dwarf_ranges_buffer;
      DWARF_RANGES_SIZE (pst) = dwarf_ranges_size;
      DWARF_LOC_BUFFER (pst) = dwarf_loc_buffer;
      DWARF_LOC_SIZE (pst) = dwarf_loc_size;
      baseaddr = ANOFFSET (objfile->section_offsets, SECT_OFF_TEXT (objfile));

      /* Store the function that reads in the rest of the symbol table */
      pst->read_symtab = dwarf2_psymtab_to_symtab;

      /* Check if comp unit has_children.
         If so, read the rest of the partial symbols from this comp unit.
         If not, there's no more debug_info for this comp unit. */
      if (comp_unit_die.has_children)
	{
	  lowpc = ((CORE_ADDR) -1);
	  highpc = ((CORE_ADDR) 0);

	  info_ptr = scan_partial_symbols (info_ptr, &lowpc, &highpc,
					   &cu, NULL);

	  /* If we didn't find a lowpc, set it to highpc to avoid
	     complaints from `maint check'.  */
	  if (lowpc == ((CORE_ADDR) -1))
	    lowpc = highpc;
	  
	  /* If the compilation unit didn't have an explicit address range,
	     then use the information extracted from its child dies.  */
	  if (! comp_unit_die.has_pc_info)
	    {
	      comp_unit_die.lowpc = lowpc;
	      comp_unit_die.highpc = highpc;
	    }
	}
      pst->textlow = comp_unit_die.lowpc + baseaddr;
      pst->texthigh = comp_unit_die.highpc + baseaddr;

      pst->n_global_syms = objfile->global_psymbols.next -
	(objfile->global_psymbols.list + pst->globals_offset);
      pst->n_static_syms = objfile->static_psymbols.next -
	(objfile->static_psymbols.list + pst->statics_offset);
      sort_pst_symbols (pst);

      /* If there is already a psymtab or symtab for a file of this
         name, remove it. (If there is a symtab, more drastic things
         also happen.) This happens in VxWorks.  */
      free_named_symtabs (pst->filename);

      info_ptr = beg_of_comp_unit + cu.header.length 
                                  + cu.header.initial_length_size;
    }
  do_cleanups (back_to);
}

/* Read in all interesting dies to the end of the compilation unit or
   to the end of the current namespace.  NAMESPACE is NULL if we
   haven't yet encountered any DW_TAG_namespace entries; otherwise,
   it's the name of the current namespace.  In particular, it's the
   empty string if we're currently in the global namespace but have
   previously encountered a DW_TAG_namespace.  */

static char *
scan_partial_symbols (char *info_ptr, CORE_ADDR *lowpc,
		      CORE_ADDR *highpc, struct dwarf2_cu *cu,
		      const char *namespace)
{
  struct objfile *objfile = cu->objfile;
  bfd *abfd = objfile->obfd;
  struct partial_die_info pdi;

  /* Now, march along the PDI's, descending into ones which have
     interesting children but skipping the children of the other ones,
     until we reach the end of the compilation unit.  */

  while (1)
    {
      /* This flag tells whether or not info_ptr has gotten updated
	 inside the loop.  */
      int info_ptr_updated = 0;

      info_ptr = read_partial_die (&pdi, abfd, info_ptr, cu);

      /* Anonymous namespaces have no name but have interesting
	 children, so we need to look at them.  Ditto for anonymous
	 enums.  */

      if (pdi.name != NULL || pdi.tag == DW_TAG_namespace
	  || pdi.tag == DW_TAG_enumeration_type)
	{
	  switch (pdi.tag)
	    {
	    case DW_TAG_subprogram:
	      if (pdi.has_pc_info)
		{
		  if (pdi.lowpc < *lowpc)
		    {
		      *lowpc = pdi.lowpc;
		    }
		  if (pdi.highpc > *highpc)
		    {
		      *highpc = pdi.highpc;
		    }
		  if (!pdi.is_declaration)
		    {
		      add_partial_symbol (&pdi, cu, namespace);
		    }
		}
	      break;
	    case DW_TAG_variable:
	    case DW_TAG_typedef:
	    case DW_TAG_union_type:
	      if (!pdi.is_declaration)
		{
		  add_partial_symbol (&pdi, cu, namespace);
		}
	      break;
	    case DW_TAG_class_type:
	    case DW_TAG_structure_type:
	      if (!pdi.is_declaration)
		{
		  info_ptr = add_partial_structure (&pdi, info_ptr, cu,
						    namespace);
		  info_ptr_updated = 1;
		}
	      break;
	    case DW_TAG_enumeration_type:
	      if (!pdi.is_declaration)
		{
		  info_ptr = add_partial_enumeration (&pdi, info_ptr, cu,
						      namespace);
		  info_ptr_updated = 1;
		}
	      break;
	    case DW_TAG_base_type:
            case DW_TAG_subrange_type:
	      /* File scope base type definitions are added to the partial
	         symbol table.  */
	      add_partial_symbol (&pdi, cu, namespace);
	      break;
	    case DW_TAG_namespace:
	      /* We've hit a DW_TAG_namespace entry, so we know this
		 file has been compiled using a compiler that
		 generates them; update NAMESPACE to reflect that.  */
	      if (namespace == NULL)
		namespace = "";
	      info_ptr = add_partial_namespace (&pdi, info_ptr, lowpc, highpc,
						cu, namespace);
	      info_ptr_updated = 1;
	      break;
	    default:
	      break;
	    }
	}

      if (pdi.tag == 0)
	break;

      /* If the die has a sibling, skip to the sibling, unless another
	 function has already updated info_ptr for us.  */

      /* NOTE: carlton/2003-06-16: This is a bit hackish, but whether
	 or not we want to update this depends on enough stuff (not
	 only pdi.tag but also whether or not pdi.name is NULL) that
	 this seems like the easiest way to handle the issue.  */

      if (!info_ptr_updated)
	info_ptr = locate_pdi_sibling (&pdi, info_ptr, abfd, cu);
    }

  return info_ptr;
}

static void
add_partial_symbol (struct partial_die_info *pdi,
		    struct dwarf2_cu *cu, const char *namespace)
{
  struct objfile *objfile = cu->objfile;
  CORE_ADDR addr = 0;
  char *actual_name = pdi->name;
  const struct partial_symbol *psym = NULL;
  CORE_ADDR baseaddr;

  baseaddr = ANOFFSET (objfile->section_offsets, SECT_OFF_TEXT (objfile));

  /* If we're not in the global namespace and if the namespace name
     isn't encoded in a mangled actual_name, add it.  */
  
  if (pdi_needs_namespace (pdi->tag, namespace))
    {
      actual_name = alloca (strlen (pdi->name) + 2 + strlen (namespace) + 1);
      strcpy (actual_name, namespace);
      strcat (actual_name, "::");
      strcat (actual_name, pdi->name);
    }

  switch (pdi->tag)
    {
    case DW_TAG_subprogram:
      if (pdi->is_external)
	{
	  /*prim_record_minimal_symbol (actual_name, pdi->lowpc + baseaddr,
	     mst_text, objfile); */
	  psym = add_psymbol_to_list (actual_name, strlen (actual_name),
				      VAR_DOMAIN, LOC_BLOCK,
				      &objfile->global_psymbols,
				      0, pdi->lowpc + baseaddr,
				      cu->language, objfile);
	}
      else
	{
	  /*prim_record_minimal_symbol (actual_name, pdi->lowpc + baseaddr,
	     mst_file_text, objfile); */
	  psym = add_psymbol_to_list (actual_name, strlen (actual_name),
				      VAR_DOMAIN, LOC_BLOCK,
				      &objfile->static_psymbols,
				      0, pdi->lowpc + baseaddr,
				      cu->language, objfile);
	}
      break;
    case DW_TAG_variable:
      if (pdi->is_external)
	{
	  /* Global Variable.
	     Don't enter into the minimal symbol tables as there is
	     a minimal symbol table entry from the ELF symbols already.
	     Enter into partial symbol table if it has a location
	     descriptor or a type.
	     If the location descriptor is missing, new_symbol will create
	     a LOC_UNRESOLVED symbol, the address of the variable will then
	     be determined from the minimal symbol table whenever the variable
	     is referenced.
	     The address for the partial symbol table entry is not
	     used by GDB, but it comes in handy for debugging partial symbol
	     table building.  */

	  if (pdi->locdesc)
	    addr = decode_locdesc (pdi->locdesc, cu);
	  if (pdi->locdesc || pdi->has_type)
	    psym = add_psymbol_to_list (actual_name, strlen (actual_name),
					VAR_DOMAIN, LOC_STATIC,
					&objfile->global_psymbols,
					0, addr + baseaddr,
					cu->language, objfile);
	}
      else
	{
	  /* Static Variable. Skip symbols without location descriptors.  */
	  if (pdi->locdesc == NULL)
	    return;
	  addr = decode_locdesc (pdi->locdesc, cu);
	  /*prim_record_minimal_symbol (actual_name, addr + baseaddr,
	     mst_file_data, objfile); */
	  psym = add_psymbol_to_list (actual_name, strlen (actual_name),
				      VAR_DOMAIN, LOC_STATIC,
				      &objfile->static_psymbols,
				      0, addr + baseaddr,
				      cu->language, objfile);
	}
      break;
    case DW_TAG_typedef:
    case DW_TAG_base_type:
    case DW_TAG_subrange_type:
      add_psymbol_to_list (actual_name, strlen (actual_name),
			   VAR_DOMAIN, LOC_TYPEDEF,
			   &objfile->static_psymbols,
			   0, (CORE_ADDR) 0, cu->language, objfile);
      break;
    case DW_TAG_class_type:
    case DW_TAG_structure_type:
    case DW_TAG_union_type:
    case DW_TAG_enumeration_type:
      /* Skip aggregate types without children, these are external
         references.  */
      /* NOTE: carlton/2003-10-07: See comment in new_symbol about
	 static vs. global.  */
      if (pdi->has_children == 0)
	return;
      add_psymbol_to_list (actual_name, strlen (actual_name),
			   STRUCT_DOMAIN, LOC_TYPEDEF,
			   cu->language == language_cplus
			   ? &objfile->global_psymbols
			   : &objfile->static_psymbols,
			   0, (CORE_ADDR) 0, cu->language, objfile);

      if (cu->language == language_cplus)
	{
	  /* For C++, these implicitly act as typedefs as well. */
	  add_psymbol_to_list (actual_name, strlen (actual_name),
			       VAR_DOMAIN, LOC_TYPEDEF,
			       &objfile->global_psymbols,
			       0, (CORE_ADDR) 0, cu->language, objfile);
	}
      break;
    case DW_TAG_enumerator:
      add_psymbol_to_list (actual_name, strlen (actual_name),
			   VAR_DOMAIN, LOC_CONST,
			   cu->language == language_cplus
			   ? &objfile->global_psymbols
			   : &objfile->static_psymbols,
			   0, (CORE_ADDR) 0, cu->language, objfile);
      break;
    default:
      break;
    }

  /* Check to see if we should scan the name for possible namespace
     info.  Only do this if this is C++, if we don't have namespace
     debugging info in the file, if the psym is of an appropriate type
     (otherwise we'll have psym == NULL), and if we actually had a
     mangled name to begin with.  */

  if (cu->language == language_cplus
      && namespace == NULL
      && psym != NULL
      && SYMBOL_CPLUS_DEMANGLED_NAME (psym) != NULL)
    cp_check_possible_namespace_symbols (SYMBOL_CPLUS_DEMANGLED_NAME (psym),
					 objfile);
}

/* Determine whether a die of type TAG living in the C++ namespace
   NAMESPACE needs to have the name of the namespace prepended to the
   name listed in the die.  */

static int
pdi_needs_namespace (enum dwarf_tag tag, const char *namespace)
{
  if (namespace == NULL || namespace[0] == '\0')
    return 0;

  switch (tag)
    {
    case DW_TAG_typedef:
    case DW_TAG_class_type:
    case DW_TAG_structure_type:
    case DW_TAG_union_type:
    case DW_TAG_enumeration_type:
    case DW_TAG_enumerator:
      return 1;
    default:
      return 0;
    }
}

/* Read a partial die corresponding to a namespace; also, add a symbol
   corresponding to that namespace to the symbol table.  NAMESPACE is
   the name of the enclosing namespace.  */

static char *
add_partial_namespace (struct partial_die_info *pdi, char *info_ptr,
		       CORE_ADDR *lowpc, CORE_ADDR *highpc,
		       struct dwarf2_cu *cu, const char *namespace)
{
  struct objfile *objfile = cu->objfile;
  const char *new_name = pdi->name;
  char *full_name;

  /* Calculate the full name of the namespace that we just entered.  */

  if (new_name == NULL)
    new_name = "(anonymous namespace)";
  full_name = alloca (strlen (namespace) + 2 + strlen (new_name) + 1);
  strcpy (full_name, namespace);
  if (*namespace != '\0')
    strcat (full_name, "::");
  strcat (full_name, new_name);

  /* FIXME: carlton/2003-10-07: We can't just replace this by a call
     to add_partial_symbol, because we don't have a way to pass in the
     full name to that function; that might be a flaw in
     add_partial_symbol's interface.  */

  add_psymbol_to_list (full_name, strlen (full_name),
		       VAR_DOMAIN, LOC_TYPEDEF,
		       &objfile->global_psymbols,
		       0, 0, cu->language, objfile);

  /* Now scan partial symbols in that namespace.  */

  if (pdi->has_children)
    info_ptr = scan_partial_symbols (info_ptr, lowpc, highpc, cu, full_name);

  return info_ptr;
}

/* Read a partial die corresponding to a class or structure.  */

static char *
add_partial_structure (struct partial_die_info *struct_pdi, char *info_ptr,
		       struct dwarf2_cu *cu,
		       const char *namespace)
{
  bfd *abfd = cu->objfile->obfd;
  char *actual_class_name = NULL;

  if (cu->language == language_cplus
      && (namespace == NULL || namespace[0] == '\0')
      && struct_pdi->name != NULL
      && struct_pdi->has_children)
    {
      /* See if we can figure out if the class lives in a namespace
	 (or is nested within another class.)  We do this by looking
	 for a member function; its demangled name will contain
	 namespace info, if there is any.  */

      /* NOTE: carlton/2003-10-07: Getting the info this way changes
	 what template types look like, because the demangler
	 frequently doesn't give the same name as the debug info.  We
	 could fix this by only using the demangled name to get the
	 prefix (but see comment in read_structure_type).  */

      /* FIXME: carlton/2004-01-23: If NAMESPACE equals "", we have
	 the appropriate debug information, so it would be nice to be
	 able to avoid this hack.  But NAMESPACE may not be the
	 namespace where this class was defined: NAMESPACE reflects
	 where STRUCT_PDI occurs in the tree of dies, but because of
	 DW_AT_specification, that may not actually tell us where the
	 class is defined.  (See the comment in read_func_scope for an
	 example of how this could occur.)

         Unfortunately, our current partial symtab data structures are
         completely unable to deal with DW_AT_specification.  So, for
         now, the best thing to do is to get nesting information from
         places other than the tree structure of dies if there's any
         chance that a DW_AT_specification is involved. :-( */

      char *next_child = info_ptr;

      while (1)
	{
	  struct partial_die_info child_pdi;

	  next_child = read_partial_die (&child_pdi, abfd, next_child,
					 cu);
	  if (!child_pdi.tag)
	    break;
	  if (child_pdi.tag == DW_TAG_subprogram)
	    {
	      actual_class_name = class_name_from_physname (child_pdi.name);
	      if (actual_class_name != NULL)
		struct_pdi->name = actual_class_name;
	      break;
	    }
	  else
	    {
	      next_child = locate_pdi_sibling (&child_pdi, next_child,
					       abfd, cu);
	    }
	}
    }

  add_partial_symbol (struct_pdi, cu, namespace);
  xfree (actual_class_name);

  return locate_pdi_sibling (struct_pdi, info_ptr, abfd, cu);
}

/* Read a partial die corresponding to an enumeration type.  */

static char *
add_partial_enumeration (struct partial_die_info *enum_pdi, char *info_ptr,
			 struct dwarf2_cu *cu, const char *namespace)
{
  struct objfile *objfile = cu->objfile;
  bfd *abfd = objfile->obfd;
  struct partial_die_info pdi;

  if (enum_pdi->name != NULL)
    add_partial_symbol (enum_pdi, cu, namespace);
  
  while (1)
    {
      info_ptr = read_partial_die (&pdi, abfd, info_ptr, cu);
      if (pdi.tag == 0)
	break;
      if (pdi.tag != DW_TAG_enumerator || pdi.name == NULL)
	complaint (&symfile_complaints, "malformed enumerator DIE ignored");
      else
	add_partial_symbol (&pdi, cu, namespace);
    }

  return info_ptr;
}

/* Locate ORIG_PDI's sibling; INFO_PTR should point to the next DIE
   after ORIG_PDI.  */

static char *
locate_pdi_sibling (struct partial_die_info *orig_pdi, char *info_ptr,
		    bfd *abfd, struct dwarf2_cu *cu)
{
  /* Do we know the sibling already?  */
  
  if (orig_pdi->sibling)
    return orig_pdi->sibling;

  /* Are there any children to deal with?  */

  if (!orig_pdi->has_children)
    return info_ptr;

  /* Okay, we don't know the sibling, but we have children that we
     want to skip.  So read children until we run into one without a
     tag; return whatever follows it.  */

  while (1)
    {
      struct partial_die_info pdi;
      
      info_ptr = read_partial_die (&pdi, abfd, info_ptr, cu);

      if (pdi.tag == 0)
	return info_ptr;
      else
	info_ptr = locate_pdi_sibling (&pdi, info_ptr, abfd, cu);
    }
}

/* Expand this partial symbol table into a full symbol table.  */

static void
dwarf2_psymtab_to_symtab (struct partial_symtab *pst)
{
  /* FIXME: This is barely more than a stub.  */
  if (pst != NULL)
    {
      if (pst->readin)
	{
	  warning ("bug: psymtab for %s is already read in.", pst->filename);
	}
      else
	{
	  if (info_verbose)
	    {
	      printf_filtered ("Reading in symbols for %s...", pst->filename);
	      gdb_flush (gdb_stdout);
	    }

	  psymtab_to_symtab_1 (pst);

	  /* Finish up the debug error message.  */
	  if (info_verbose)
	    printf_filtered ("done.\n");
	}
    }
}

static void
psymtab_to_symtab_1 (struct partial_symtab *pst)
{
  struct objfile *objfile = pst->objfile;
  bfd *abfd = objfile->obfd;
  struct dwarf2_cu cu;
  struct die_info *dies;
  unsigned long offset;
  CORE_ADDR lowpc, highpc;
  struct die_info *child_die;
  char *info_ptr;
  struct symtab *symtab;
  struct cleanup *back_to;
  struct attribute *attr;
  CORE_ADDR baseaddr;

  /* Set local variables from the partial symbol table info.  */
  offset = DWARF_INFO_OFFSET (pst);
  dwarf_info_buffer = DWARF_INFO_BUFFER (pst);
  dwarf_abbrev_buffer = DWARF_ABBREV_BUFFER (pst);
  dwarf_abbrev_size = DWARF_ABBREV_SIZE (pst);
  dwarf_line_buffer = DWARF_LINE_BUFFER (pst);
  dwarf_line_size = DWARF_LINE_SIZE (pst);
  dwarf_str_buffer = DWARF_STR_BUFFER (pst);
  dwarf_str_size = DWARF_STR_SIZE (pst);
  dwarf_macinfo_buffer = DWARF_MACINFO_BUFFER (pst);
  dwarf_macinfo_size = DWARF_MACINFO_SIZE (pst);
  dwarf_ranges_buffer = DWARF_RANGES_BUFFER (pst);
  dwarf_ranges_size = DWARF_RANGES_SIZE (pst);
  dwarf_loc_buffer = DWARF_LOC_BUFFER (pst);
  dwarf_loc_size = DWARF_LOC_SIZE (pst);
  info_ptr = dwarf_info_buffer + offset;
  baseaddr = ANOFFSET (objfile->section_offsets, SECT_OFF_TEXT (objfile));

  /* We're in the global namespace.  */
  processing_current_prefix = "";

  obstack_init (&dwarf2_tmp_obstack);
  back_to = make_cleanup (dwarf2_free_tmp_obstack, NULL);

  buildsym_init ();
  make_cleanup (really_free_pendings, NULL);

  cu.objfile = objfile;

  /* read in the comp_unit header  */
  info_ptr = read_comp_unit_head (&cu.header, info_ptr, abfd);

  /* Read the abbrevs for this compilation unit  */
  dwarf2_read_abbrevs (abfd, &cu);
  make_cleanup (dwarf2_empty_abbrev_table, cu.header.dwarf2_abbrevs);

  cu.header.offset = offset;

  cu.list_in_scope = &file_symbols;

  dies = read_comp_unit (info_ptr, abfd, &cu);

  make_cleanup_free_die_list (dies);

  /* Find the base address of the compilation unit for range lists and
     location lists.  It will normally be specified by DW_AT_low_pc.
     In DWARF-3 draft 4, the base address could be overridden by
     DW_AT_entry_pc.  It's been removed, but GCC still uses this for
     compilation units with discontinuous ranges.  */

  cu.header.base_known = 0;
  cu.header.base_address = 0;

  attr = dwarf2_attr (dies, DW_AT_entry_pc, &cu);
  if (attr)
    {
      cu.header.base_address = DW_ADDR (attr);
      cu.header.base_known = 1;
    }
  else
    {
      attr = dwarf2_attr (dies, DW_AT_low_pc, &cu);
      if (attr)
	{
	  cu.header.base_address = DW_ADDR (attr);
	  cu.header.base_known = 1;
	}
    }

  /* Do line number decoding in read_file_scope () */
  process_die (dies, &cu);

  /* Some compilers don't define a DW_AT_high_pc attribute for the
     compilation unit.  If the DW_AT_high_pc is missing, synthesize
     it, by scanning the DIE's below the compilation unit.  */
  get_scope_pc_bounds (dies, &lowpc, &highpc, &cu);

  symtab = end_symtab (highpc + baseaddr, objfile, SECT_OFF_TEXT (objfile));

  /* Set symtab language to language from DW_AT_language.
     If the compilation is from a C file generated by language preprocessors,
     do not set the language if it was already deduced by start_subfile.  */
  if (symtab != NULL
      && !(cu.language == language_c && symtab->language != language_c))
    {
      symtab->language = cu.language;
    }
  pst->symtab = symtab;
  pst->readin = 1;

  do_cleanups (back_to);
}

/* Process a die and its children.  */

static void
process_die (struct die_info *die, struct dwarf2_cu *cu)
{
  switch (die->tag)
    {
    case DW_TAG_padding:
      break;
    case DW_TAG_compile_unit:
      read_file_scope (die, cu);
      break;
    case DW_TAG_subprogram:
      read_subroutine_type (die, cu);
      read_func_scope (die, cu);
      break;
    case DW_TAG_inlined_subroutine:
      /* FIXME:  These are ignored for now.
         They could be used to set breakpoints on all inlined instances
         of a function and make GDB `next' properly over inlined functions.  */
      break;
    case DW_TAG_lexical_block:
    case DW_TAG_try_block:
    case DW_TAG_catch_block:
      read_lexical_block_scope (die, cu);
      break;
    case DW_TAG_class_type:
    case DW_TAG_structure_type:
    case DW_TAG_union_type:
      read_structure_type (die, cu);
      process_structure_scope (die, cu);
      break;
    case DW_TAG_enumeration_type:
      read_enumeration_type (die, cu);
      process_enumeration_scope (die, cu);
      break;

    /* FIXME drow/2004-03-14: These initialize die->type, but do not create
       a symbol or process any children.  Therefore it doesn't do anything
       that won't be done on-demand by read_type_die.  */
    case DW_TAG_subroutine_type:
      read_subroutine_type (die, cu);
      break;
    case DW_TAG_array_type:
      read_array_type (die, cu);
      break;
    case DW_TAG_pointer_type:
      read_tag_pointer_type (die, cu);
      break;
    case DW_TAG_ptr_to_member_type:
      read_tag_ptr_to_member_type (die, cu);
      break;
    case DW_TAG_reference_type:
    case DW_TAG_rvalue_reference_type:
      read_tag_reference_type (die, cu);
      break;
    case DW_TAG_string_type:
      read_tag_string_type (die, cu);
      break;
    /* END FIXME */

    case DW_TAG_base_type:
      read_base_type (die, cu);
      /* Add a typedef symbol for the type definition, if it has a
	 DW_AT_name.  */
      new_symbol (die, die->type, cu);
      break;
    case DW_TAG_subrange_type:
      read_subrange_type (die, cu);
      /* Add a typedef symbol for the type definition, if it has a
         DW_AT_name.  */
      new_symbol (die, die->type, cu);
      break;
    case DW_TAG_common_block:
      read_common_block (die, cu);
      break;
    case DW_TAG_common_inclusion:
      break;
    case DW_TAG_namespace:
      processing_has_namespace_info = 1;
      read_namespace (die, cu);
      break;
    case DW_TAG_imported_declaration:
    case DW_TAG_imported_module:
      /* FIXME: carlton/2002-10-16: Eventually, we should use the
	 information contained in these.  DW_TAG_imported_declaration
	 dies shouldn't have children; DW_TAG_imported_module dies
	 shouldn't in the C++ case, but conceivably could in the
	 Fortran case, so we'll have to replace this gdb_assert if
	 Fortran compilers start generating that info.  */
      processing_has_namespace_info = 1;
      gdb_assert (die->child == NULL);
      break;
    default:
      new_symbol (die, NULL, cu);
      break;
    }
}

static void
initialize_cu_func_list (struct dwarf2_cu *cu)
{
  cu->first_fn = cu->last_fn = cu->cached_fn = NULL;
}

static void
read_file_scope (struct die_info *die, struct dwarf2_cu *cu)
{
  struct objfile *objfile = cu->objfile;
  struct comp_unit_head *cu_header = &cu->header;
  struct cleanup *back_to = make_cleanup (null_cleanup, 0);
  CORE_ADDR lowpc = ((CORE_ADDR) -1);
  CORE_ADDR highpc = ((CORE_ADDR) 0);
  struct attribute *attr;
  char *name = "<unknown>";
  char *comp_dir = NULL;
  struct die_info *child_die;
  bfd *abfd = objfile->obfd;
  struct line_header *line_header = 0;
  CORE_ADDR baseaddr;
  
  baseaddr = ANOFFSET (objfile->section_offsets, SECT_OFF_TEXT (objfile));

  get_scope_pc_bounds (die, &lowpc, &highpc, cu);

  /* If we didn't find a lowpc, set it to highpc to avoid complaints
     from finish_block.  */
  if (lowpc == ((CORE_ADDR) -1))
    lowpc = highpc;
  lowpc += baseaddr;
  highpc += baseaddr;

  attr = dwarf2_attr (die, DW_AT_name, cu);
  if (attr)
    {
      name = DW_STRING (attr);
    }
  attr = dwarf2_attr (die, DW_AT_comp_dir, cu);
  if (attr)
    {
      comp_dir = DW_STRING (attr);
      if (comp_dir)
	{
	  /* Irix 6.2 native cc prepends <machine>.: to the compilation
	     directory, get rid of it.  */
	  char *cp = strchr (comp_dir, ':');

	  if (cp && cp != comp_dir && cp[-1] == '.' && cp[1] == '/')
	    comp_dir = cp + 1;
	}
    }

  if (objfile->ei.entry_point >= lowpc &&
      objfile->ei.entry_point < highpc)
    {
      objfile->ei.deprecated_entry_file_lowpc = lowpc;
      objfile->ei.deprecated_entry_file_highpc = highpc;
    }

  attr = dwarf2_attr (die, DW_AT_language, cu);
  if (attr)
    {
      set_cu_language (DW_UNSND (attr), cu);
    }

  /* We assume that we're processing GCC output. */
  processing_gcc_compilation = 2;
#if 0
  /* FIXME:Do something here.  */
  if (dip->at_producer != NULL)
    {
      handle_producer (dip->at_producer);
    }
#endif

  /* The compilation unit may be in a different language or objfile,
     zero out all remembered fundamental types.  */
  memset (cu->ftypes, 0, FT_NUM_MEMBERS * sizeof (struct type *));

  start_symtab (name, comp_dir, lowpc);
  record_debugformat ("DWARF 2");

  initialize_cu_func_list (cu);

  /* Process all dies in compilation unit.  */
  if (die->child != NULL)
    {
      child_die = die->child;
      while (child_die && child_die->tag)
	{
	  process_die (child_die, cu);
	  child_die = sibling_die (child_die);
	}
    }

  /* Decode line number information if present.  */
  attr = dwarf2_attr (die, DW_AT_stmt_list, cu);
  if (attr)
    {
      unsigned int line_offset = DW_UNSND (attr);
      line_header = dwarf_decode_line_header (line_offset, abfd, cu);
      if (line_header)
        {
          make_cleanup ((make_cleanup_ftype *) free_line_header,
                        (void *) line_header);
          dwarf_decode_lines (line_header, comp_dir, abfd, cu);
        }
    }

  /* Decode macro information, if present.  Dwarf 2 macro information
     refers to information in the line number info statement program
     header, so we can only read it if we've read the header
     successfully.  */
  attr = dwarf2_attr (die, DW_AT_macro_info, cu);
  if (attr && line_header)
    {
      unsigned int macro_offset = DW_UNSND (attr);
      dwarf_decode_macros (line_header, macro_offset,
                           comp_dir, abfd, cu);
    }
  do_cleanups (back_to);
}

static void
add_to_cu_func_list (const char *name, CORE_ADDR lowpc, CORE_ADDR highpc,
		     struct dwarf2_cu *cu)
{
  struct function_range *thisfn;

  thisfn = (struct function_range *)
    obstack_alloc (&dwarf2_tmp_obstack, sizeof (struct function_range));
  thisfn->name = name;
  thisfn->lowpc = lowpc;
  thisfn->highpc = highpc;
  thisfn->seen_line = 0;
  thisfn->next = NULL;

  if (cu->last_fn == NULL)
      cu->first_fn = thisfn;
  else
      cu->last_fn->next = thisfn;

  cu->last_fn = thisfn;
}

static void
read_func_scope (struct die_info *die, struct dwarf2_cu *cu)
{
  struct objfile *objfile = cu->objfile;
  struct context_stack *new;
  CORE_ADDR lowpc;
  CORE_ADDR highpc;
  struct die_info *child_die;
  struct attribute *attr;
  char *name;
  const char *previous_prefix = processing_current_prefix;
  struct cleanup *back_to = NULL;
  CORE_ADDR baseaddr;

  baseaddr = ANOFFSET (objfile->section_offsets, SECT_OFF_TEXT (objfile));

  name = dwarf2_linkage_name (die, cu);

  /* Ignore functions with missing or empty names and functions with
     missing or invalid low and high pc attributes.  */
  if (name == NULL || !dwarf2_get_pc_bounds (die, &lowpc, &highpc, cu))
    return;

  if (cu->language == language_cplus)
    {
      struct die_info *spec_die = die_specification (die, cu);

      /* NOTE: carlton/2004-01-23: We have to be careful in the
         presence of DW_AT_specification.  For example, with GCC 3.4,
         given the code

           namespace N {
             void foo() {
               // Definition of N::foo.
             }
           }

         then we'll have a tree of DIEs like this:

         1: DW_TAG_compile_unit
           2: DW_TAG_namespace        // N
             3: DW_TAG_subprogram     // declaration of N::foo
           4: DW_TAG_subprogram       // definition of N::foo
                DW_AT_specification   // refers to die #3

         Thus, when processing die #4, we have to pretend that we're
         in the context of its DW_AT_specification, namely the contex
         of die #3.  */
	
      if (spec_die != NULL)
	{
	  char *specification_prefix = determine_prefix (spec_die, cu);
	  processing_current_prefix = specification_prefix;
	  back_to = make_cleanup (xfree, specification_prefix);
	}
    }

  lowpc += baseaddr;
  highpc += baseaddr;

  /* Record the function range for dwarf_decode_lines.  */
  add_to_cu_func_list (name, lowpc, highpc, cu);

  if (objfile->ei.entry_point >= lowpc &&
      objfile->ei.entry_point < highpc)
    {
      objfile->ei.entry_func_lowpc = lowpc;
      objfile->ei.entry_func_highpc = highpc;
    }

  new = push_context (0, lowpc);
  new->name = new_symbol (die, die->type, cu);

  /* If there is a location expression for DW_AT_frame_base, record
     it.  */
  attr = dwarf2_attr (die, DW_AT_frame_base, cu);
  if (attr)
    /* FIXME: cagney/2004-01-26: The DW_AT_frame_base's location
       expression is being recorded directly in the function's symbol
       and not in a separate frame-base object.  I guess this hack is
       to avoid adding some sort of frame-base adjunct/annex to the
       function's symbol :-(.  The problem with doing this is that it
       results in a function symbol with a location expression that
       has nothing to do with the location of the function, ouch!  The
       relationship should be: a function's symbol has-a frame base; a
       frame-base has-a location expression.  */
    dwarf2_symbol_mark_computed (attr, new->name, cu);

  cu->list_in_scope = &local_symbols;

  if (die->child != NULL)
    {
      child_die = die->child;
      while (child_die && child_die->tag)
	{
	  process_die (child_die, cu);
	  child_die = sibling_die (child_die);
	}
    }

  new = pop_context ();
  /* Make a block for the local symbols within.  */
  finish_block (new->name, &local_symbols, new->old_blocks,
		lowpc, highpc, objfile);
  
  /* In C++, we can have functions nested inside functions (e.g., when
     a function declares a class that has methods).  This means that
     when we finish processing a function scope, we may need to go
     back to building a containing block's symbol lists.  */
  local_symbols = new->locals;
  param_symbols = new->params;

  /* If we've finished processing a top-level function, subsequent
     symbols go in the file symbol list.  */
  if (outermost_context_p ())
    cu->list_in_scope = &file_symbols;

  processing_current_prefix = previous_prefix;
  if (back_to != NULL)
    do_cleanups (back_to);
}

/* Process all the DIES contained within a lexical block scope.  Start
   a new scope, process the dies, and then close the scope.  */

static void
read_lexical_block_scope (struct die_info *die, struct dwarf2_cu *cu)
{
  struct objfile *objfile = cu->objfile;
  struct context_stack *new;
  CORE_ADDR lowpc, highpc;
  struct die_info *child_die;
  CORE_ADDR baseaddr;

  baseaddr = ANOFFSET (objfile->section_offsets, SECT_OFF_TEXT (objfile));

  /* Ignore blocks with missing or invalid low and high pc attributes.  */
  /* ??? Perhaps consider discontiguous blocks defined by DW_AT_ranges
     as multiple lexical blocks?  Handling children in a sane way would
     be nasty.  Might be easier to properly extend generic blocks to 
     describe ranges.  */
  if (!dwarf2_get_pc_bounds (die, &lowpc, &highpc, cu))
    return;
  lowpc += baseaddr;
  highpc += baseaddr;

  push_context (0, lowpc);
  if (die->child != NULL)
    {
      child_die = die->child;
      while (child_die && child_die->tag)
	{
	  process_die (child_die, cu);
	  child_die = sibling_die (child_die);
	}
    }
  new = pop_context ();

  if (local_symbols != NULL)
    {
      finish_block (0, &local_symbols, new->old_blocks, new->start_addr,
		    highpc, objfile);
    }
  local_symbols = new->locals;
}

/* Get low and high pc attributes from a die.  Return 1 if the attributes
   are present and valid, otherwise, return 0.  Return -1 if the range is
   discontinuous, i.e. derived from DW_AT_ranges information.  */
static int
dwarf2_get_pc_bounds (struct die_info *die, CORE_ADDR *lowpc,
		      CORE_ADDR *highpc, struct dwarf2_cu *cu)
{
  struct objfile *objfile = cu->objfile;
  struct comp_unit_head *cu_header = &cu->header;
  struct attribute *attr;
  bfd *obfd = objfile->obfd;
  CORE_ADDR low = 0;
  CORE_ADDR high = 0;
  int ret = 0;

  attr = dwarf2_attr (die, DW_AT_high_pc, cu);
  if (attr)
    {
      high = DW_ADDR (attr);
      attr = dwarf2_attr (die, DW_AT_low_pc, cu);
      if (attr)
	low = DW_ADDR (attr);
      else
	/* Found high w/o low attribute.  */
	return 0;

      /* Found consecutive range of addresses.  */
      ret = 1;
    }
  else
    {
      attr = dwarf2_attr (die, DW_AT_ranges, cu);
      if (attr != NULL)
	{
	  unsigned int addr_size = cu_header->addr_size;
	  CORE_ADDR mask = ~(~(CORE_ADDR)1 << (addr_size * 8 - 1));
	  /* Value of the DW_AT_ranges attribute is the offset in the
	     .debug_ranges section.  */
	  unsigned int offset = DW_UNSND (attr);
	  /* Base address selection entry.  */
	  CORE_ADDR base;
	  int found_base;
	  int dummy;
	  char *buffer;
	  CORE_ADDR marker;
	  int low_set;
 
	  found_base = cu_header->base_known;
	  base = cu_header->base_address;

	  if (offset >= dwarf_ranges_size)
	    {
	      complaint (&symfile_complaints,
	                 "Offset %d out of bounds for DW_AT_ranges attribute",
			 offset);
	      return 0;
	    }
	  buffer = dwarf_ranges_buffer + offset;

	  /* Read in the largest possible address.  */
	  marker = read_address (obfd, buffer, cu, &dummy);
	  if ((marker & mask) == mask)
	    {
	      /* If we found the largest possible address, then
		 read the base address.  */
	      base = read_address (obfd, buffer + addr_size, cu, &dummy);
	      buffer += 2 * addr_size;
	      offset += 2 * addr_size;
	      found_base = 1;
	    }

	  low_set = 0;

	  while (1)
	    {
	      CORE_ADDR range_beginning, range_end;

	      range_beginning = read_address (obfd, buffer, cu, &dummy);
	      buffer += addr_size;
	      range_end = read_address (obfd, buffer, cu, &dummy);
	      buffer += addr_size;
	      offset += 2 * addr_size;

	      /* An end of list marker is a pair of zero addresses.  */
	      if (range_beginning == 0 && range_end == 0)
		/* Found the end of list entry.  */
		break;

	      /* Each base address selection entry is a pair of 2 values.
		 The first is the largest possible address, the second is
		 the base address.  Check for a base address here.  */
	      if ((range_beginning & mask) == mask)
		{
		  /* If we found the largest possible address, then
		     read the base address.  */
		  base = read_address (obfd, buffer + addr_size, cu, &dummy);
		  found_base = 1;
		  continue;
		}

	      if (!found_base)
		{
		  /* We have no valid base address for the ranges
		     data.  */
		  complaint (&symfile_complaints,
			     "Invalid .debug_ranges data (no base address)");
		  return 0;
		}

	      range_beginning += base;
	      range_end += base;

	      /* FIXME: This is recording everything as a low-high
		 segment of consecutive addresses.  We should have a
		 data structure for discontiguous block ranges
		 instead.  */
	      if (! low_set)
		{
		  low = range_beginning;
		  high = range_end;
		  low_set = 1;
		}
	      else
		{
		  if (range_beginning < low)
		    low = range_beginning;
		  if (range_end > high)
		    high = range_end;
		}
	    }

	  if (! low_set)
	    /* If the first entry is an end-of-list marker, the range
	       describes an empty scope, i.e. no instructions.  */
	    return 0;

	  ret = -1;
	}
    }

  if (high < low)
    return 0;

  /* When using the GNU linker, .gnu.linkonce. sections are used to
     eliminate duplicate copies of functions and vtables and such.
     The linker will arbitrarily choose one and discard the others.
     The AT_*_pc values for such functions refer to local labels in
     these sections.  If the section from that file was discarded, the
     labels are not in the output, so the relocs get a value of 0.
     If this is a discarded function, mark the pc bounds as invalid,
     so that GDB will ignore it.  */
  if (low == 0 && (bfd_get_file_flags (obfd) & HAS_RELOC) == 0)
    return 0;

  *lowpc = low;
  *highpc = high;
  return ret;
}

/* Get the low and high pc's represented by the scope DIE, and store
   them in *LOWPC and *HIGHPC.  If the correct values can't be
   determined, set *LOWPC to -1 and *HIGHPC to 0.  */

static void
get_scope_pc_bounds (struct die_info *die,
		     CORE_ADDR *lowpc, CORE_ADDR *highpc,
		     struct dwarf2_cu *cu)
{
  CORE_ADDR best_low = (CORE_ADDR) -1;
  CORE_ADDR best_high = (CORE_ADDR) 0;
  CORE_ADDR current_low, current_high;

  if (dwarf2_get_pc_bounds (die, &current_low, &current_high, cu))
    {
      best_low = current_low;
      best_high = current_high;
    }
  else
    {
      struct die_info *child = die->child;

      while (child && child->tag)
	{
	  switch (child->tag) {
	  case DW_TAG_subprogram:
	    if (dwarf2_get_pc_bounds (child, &current_low, &current_high, cu))
	      {
		best_low = min (best_low, current_low);
		best_high = max (best_high, current_high);
	      }
	    break;
	  case DW_TAG_namespace:
	    /* FIXME: carlton/2004-01-16: Should we do this for
	       DW_TAG_class_type/DW_TAG_structure_type, too?  I think
	       that current GCC's always emit the DIEs corresponding
	       to definitions of methods of classes as children of a
	       DW_TAG_compile_unit or DW_TAG_namespace (as opposed to
	       the DIEs giving the declarations, which could be
	       anywhere).  But I don't see any reason why the
	       standards says that they have to be there.  */
	    get_scope_pc_bounds (child, &current_low, &current_high, cu);

	    if (current_low != ((CORE_ADDR) -1))
	      {
		best_low = min (best_low, current_low);
		best_high = max (best_high, current_high);
	      }
	    break;
	  default:
	    /* Ignore. */
	    break;
	  }

	  child = sibling_die (child);
	}
    }

  *lowpc = best_low;
  *highpc = best_high;
}

/* Add an aggregate field to the field list.  */

static void
dwarf2_add_field (struct field_info *fip, struct die_info *die,
		  struct dwarf2_cu *cu)
{ 
  struct objfile *objfile = cu->objfile;
  struct nextfield *new_field;
  struct attribute *attr;
  struct field *fp;
  char *fieldname = "";

  /* Allocate a new field list entry and link it in.  */
  new_field = (struct nextfield *) xmalloc (sizeof (struct nextfield));
  make_cleanup (xfree, new_field);
  memset (new_field, 0, sizeof (struct nextfield));
  new_field->next = fip->fields;
  fip->fields = new_field;
  fip->nfields++;

  /* Handle accessibility and virtuality of field.
     The default accessibility for members is public, the default
     accessibility for inheritance is private.  */
  if (die->tag != DW_TAG_inheritance)
    new_field->accessibility = DW_ACCESS_public;
  else
    new_field->accessibility = DW_ACCESS_private;
  new_field->virtuality = DW_VIRTUALITY_none;

  attr = dwarf2_attr (die, DW_AT_accessibility, cu);
  if (attr)
    new_field->accessibility = DW_UNSND (attr);
  if (new_field->accessibility != DW_ACCESS_public)
    fip->non_public_fields = 1;
  attr = dwarf2_attr (die, DW_AT_virtuality, cu);
  if (attr)
    new_field->virtuality = DW_UNSND (attr);

  fp = &new_field->field;

  if (die->tag == DW_TAG_member && ! die_is_declaration (die, cu))
    {
      /* Data member other than a C++ static data member.  */
      
      /* Get type of field.  */
      fp->type = die_type (die, cu);

      FIELD_STATIC_KIND (*fp) = 0;

      /* Get bit size of field (zero if none).  */
      attr = dwarf2_attr (die, DW_AT_bit_size, cu);
      if (attr)
	{
	  FIELD_BITSIZE (*fp) = DW_UNSND (attr);
	}
      else
	{
	  FIELD_BITSIZE (*fp) = 0;
	}

      /* Get bit offset of field.  */
      attr = dwarf2_attr (die, DW_AT_data_member_location, cu);
      if (attr)
	{
	  FIELD_BITPOS (*fp) =
	    decode_locdesc (DW_BLOCK (attr), cu) * bits_per_byte;
	}
      else
	FIELD_BITPOS (*fp) = 0;
      attr = dwarf2_attr (die, DW_AT_bit_offset, cu);
      if (attr)
	{
	  if (BITS_BIG_ENDIAN)
	    {
	      /* For big endian bits, the DW_AT_bit_offset gives the
	         additional bit offset from the MSB of the containing
	         anonymous object to the MSB of the field.  We don't
	         have to do anything special since we don't need to
	         know the size of the anonymous object.  */
	      FIELD_BITPOS (*fp) += DW_UNSND (attr);
	    }
	  else
	    {
	      /* For little endian bits, compute the bit offset to the
	         MSB of the anonymous object, subtract off the number of
	         bits from the MSB of the field to the MSB of the
	         object, and then subtract off the number of bits of
	         the field itself.  The result is the bit offset of
	         the LSB of the field.  */
	      int anonymous_size;
	      int bit_offset = DW_UNSND (attr);

	      attr = dwarf2_attr (die, DW_AT_byte_size, cu);
	      if (attr)
		{
		  /* The size of the anonymous object containing
		     the bit field is explicit, so use the
		     indicated size (in bytes).  */
		  anonymous_size = DW_UNSND (attr);
		}
	      else
		{
		  /* The size of the anonymous object containing
		     the bit field must be inferred from the type
		     attribute of the data member containing the
		     bit field.  */
		  anonymous_size = TYPE_LENGTH (fp->type);
		}
	      FIELD_BITPOS (*fp) += anonymous_size * bits_per_byte
		- bit_offset - FIELD_BITSIZE (*fp);
	    }
	}

      /* Get name of field.  */
      attr = dwarf2_attr (die, DW_AT_name, cu);
      if (attr && DW_STRING (attr))
	fieldname = DW_STRING (attr);
      fp->name = obsavestring (fieldname, strlen (fieldname),
			       &objfile->objfile_obstack);

      /* Change accessibility for artificial fields (e.g. virtual table
         pointer or virtual base class pointer) to private.  */
      if (dwarf2_attr (die, DW_AT_artificial, cu))
	{
	  new_field->accessibility = DW_ACCESS_private;
	  fip->non_public_fields = 1;
	}
    }
  else if (die->tag == DW_TAG_member || die->tag == DW_TAG_variable)
    {
      /* C++ static member.  */

      /* NOTE: carlton/2002-11-05: It should be a DW_TAG_member that
	 is a declaration, but all versions of G++ as of this writing
	 (so through at least 3.2.1) incorrectly generate
	 DW_TAG_variable tags.  */
      
      char *physname;

      /* Get name of field.  */
      attr = dwarf2_attr (die, DW_AT_name, cu);
      if (attr && DW_STRING (attr))
	fieldname = DW_STRING (attr);
      else
	return;

      /* Get physical name.  */
      physname = dwarf2_linkage_name (die, cu);

      SET_FIELD_PHYSNAME (*fp, obsavestring (physname, strlen (physname),
					     &objfile->objfile_obstack));
      FIELD_TYPE (*fp) = die_type (die, cu);
      FIELD_NAME (*fp) = obsavestring (fieldname, strlen (fieldname),
				       &objfile->objfile_obstack);
    }
  else if (die->tag == DW_TAG_inheritance)
    {
      /* C++ base class field.  */
      attr = dwarf2_attr (die, DW_AT_data_member_location, cu);
      if (attr)
	FIELD_BITPOS (*fp) = (decode_locdesc (DW_BLOCK (attr), cu)
			      * bits_per_byte);
      FIELD_BITSIZE (*fp) = 0;
      FIELD_STATIC_KIND (*fp) = 0;
      FIELD_TYPE (*fp) = die_type (die, cu);
      FIELD_NAME (*fp) = type_name_no_tag (fp->type);
      fip->nbaseclasses++;
    }
}

/* Create the vector of fields, and attach it to the type.  */

static void
dwarf2_attach_fields_to_type (struct field_info *fip, struct type *type,
			      struct dwarf2_cu *cu)
{
  int nfields = fip->nfields;

  /* Record the field count, allocate space for the array of fields,
     and create blank accessibility bitfields if necessary.  */
  TYPE_NFIELDS (type) = nfields;
  TYPE_FIELDS (type) = (struct field *)
    TYPE_ALLOC (type, sizeof (struct field) * nfields);
  memset (TYPE_FIELDS (type), 0, sizeof (struct field) * nfields);

  if (fip->non_public_fields)
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

  /* If the type has baseclasses, allocate and clear a bit vector for
     TYPE_FIELD_VIRTUAL_BITS.  */
  if (fip->nbaseclasses)
    {
      int num_bytes = B_BYTES (fip->nbaseclasses);
      char *pointer;

      ALLOCATE_CPLUS_STRUCT_TYPE (type);
      pointer = (char *) TYPE_ALLOC (type, num_bytes);
      TYPE_FIELD_VIRTUAL_BITS (type) = (B_TYPE *) pointer;
      B_CLRALL (TYPE_FIELD_VIRTUAL_BITS (type), fip->nbaseclasses);
      TYPE_N_BASECLASSES (type) = fip->nbaseclasses;
    }

  /* Copy the saved-up fields into the field vector.  Start from the head
     of the list, adding to the tail of the field array, so that they end
     up in the same order in the array in which they were added to the list.  */
  while (nfields-- > 0)
    {
      TYPE_FIELD (type, nfields) = fip->fields->field;
      switch (fip->fields->accessibility)
	{
	case DW_ACCESS_private:
	  SET_TYPE_FIELD_PRIVATE (type, nfields);
	  break;

	case DW_ACCESS_protected:
	  SET_TYPE_FIELD_PROTECTED (type, nfields);
	  break;

	case DW_ACCESS_public:
	  break;

	default:
	  /* Unknown accessibility.  Complain and treat it as public.  */
	  {
	    complaint (&symfile_complaints, "unsupported accessibility %d",
		       fip->fields->accessibility);
	  }
	  break;
	}
      if (nfields < fip->nbaseclasses)
	{
	  switch (fip->fields->virtuality)
	    {
	    case DW_VIRTUALITY_virtual:
	    case DW_VIRTUALITY_pure_virtual:
	      SET_TYPE_FIELD_VIRTUAL (type, nfields);
	      break;
	    }
	}
      fip->fields = fip->fields->next;
    }
}

/* Add a member function to the proper fieldlist.  */

static void
dwarf2_add_member_fn (struct field_info *fip, struct die_info *die,
		      struct type *type, struct dwarf2_cu *cu)
{
  struct objfile *objfile = cu->objfile;
  struct attribute *attr;
  struct fnfieldlist *flp;
  int i;
  struct fn_field *fnp;
  char *fieldname;
  char *physname;
  struct nextfnfield *new_fnfield;

  /* Get name of member function.  */
  attr = dwarf2_attr (die, DW_AT_name, cu);
  if (attr && DW_STRING (attr))
    fieldname = DW_STRING (attr);
  else
    return;

  /* Get the mangled name.  */
  physname = dwarf2_linkage_name (die, cu);

  /* Look up member function name in fieldlist.  */
  for (i = 0; i < fip->nfnfields; i++)
    {
      if (strcmp (fip->fnfieldlists[i].name, fieldname) == 0)
	break;
    }

  /* Create new list element if necessary.  */
  if (i < fip->nfnfields)
    flp = &fip->fnfieldlists[i];
  else
    {
      if ((fip->nfnfields % DW_FIELD_ALLOC_CHUNK) == 0)
	{
	  fip->fnfieldlists = (struct fnfieldlist *)
	    xrealloc (fip->fnfieldlists,
		      (fip->nfnfields + DW_FIELD_ALLOC_CHUNK)
		      * sizeof (struct fnfieldlist));
	  if (fip->nfnfields == 0)
	    make_cleanup (free_current_contents, &fip->fnfieldlists);
	}
      flp = &fip->fnfieldlists[fip->nfnfields];
      flp->name = fieldname;
      flp->length = 0;
      flp->head = NULL;
      fip->nfnfields++;
    }

  /* Create a new member function field and chain it to the field list
     entry. */
  new_fnfield = (struct nextfnfield *) xmalloc (sizeof (struct nextfnfield));
  make_cleanup (xfree, new_fnfield);
  memset (new_fnfield, 0, sizeof (struct nextfnfield));
  new_fnfield->next = flp->head;
  flp->head = new_fnfield;
  flp->length++;

  /* Fill in the member function field info.  */
  fnp = &new_fnfield->fnfield;
  fnp->physname = obsavestring (physname, strlen (physname),
				&objfile->objfile_obstack);
  fnp->type = alloc_type (objfile);
  if (die->type && TYPE_CODE (die->type) == TYPE_CODE_FUNC)
    {
      int nparams = TYPE_NFIELDS (die->type);

      /* TYPE is the domain of this method, and DIE->TYPE is the type
	   of the method itself (TYPE_CODE_METHOD).  */
      smash_to_method_type (fnp->type, type,
			    TYPE_TARGET_TYPE (die->type),
			    TYPE_FIELDS (die->type),
			    TYPE_NFIELDS (die->type),
			    TYPE_VARARGS (die->type));

      /* Handle static member functions.
         Dwarf2 has no clean way to discern C++ static and non-static
         member functions. G++ helps GDB by marking the first
         parameter for non-static member functions (which is the
         this pointer) as artificial. We obtain this information
         from read_subroutine_type via TYPE_FIELD_ARTIFICIAL.  */
      if (nparams == 0 || TYPE_FIELD_ARTIFICIAL (die->type, 0) == 0)
	fnp->voffset = VOFFSET_STATIC;
    }
  else
    complaint (&symfile_complaints, "member function type missing for '%s'",
	       physname);

  /* Get fcontext from DW_AT_containing_type if present.  */
  if (dwarf2_attr (die, DW_AT_containing_type, cu) != NULL)
    fnp->fcontext = die_containing_type (die, cu);

  /* dwarf2 doesn't have stubbed physical names, so the setting of is_const
     and is_volatile is irrelevant, as it is needed by gdb_mangle_name only.  */

  /* Get accessibility.  */
  attr = dwarf2_attr (die, DW_AT_accessibility, cu);
  if (attr)
    {
      switch (DW_UNSND (attr))
	{
	case DW_ACCESS_private:
	  fnp->is_private = 1;
	  break;
	case DW_ACCESS_protected:
	  fnp->is_protected = 1;
	  break;
	}
    }

  /* Check for artificial methods.  */
  attr = dwarf2_attr (die, DW_AT_artificial, cu);
  if (attr && DW_UNSND (attr) != 0)
    fnp->is_artificial = 1;

  /* Get index in virtual function table if it is a virtual member function.  */
  attr = dwarf2_attr (die, DW_AT_vtable_elem_location, cu);
  if (attr)
    {
      /* Support the .debug_loc offsets */
      if (attr_form_is_block (attr))
        {
          fnp->voffset = decode_locdesc (DW_BLOCK (attr), cu) + 2;
        }
      else if (attr->form == DW_FORM_data4 || attr->form == DW_FORM_data8)
        {
	  dwarf2_complex_location_expr_complaint ();
        }
      else
        {
	  dwarf2_invalid_attrib_class_complaint ("DW_AT_vtable_elem_location",
						 fieldname);
        }
   }
}

/* Create the vector of member function fields, and attach it to the type.  */

static void
dwarf2_attach_fn_fields_to_type (struct field_info *fip, struct type *type,
				 struct dwarf2_cu *cu)
{
  struct fnfieldlist *flp;
  int total_length = 0;
  int i;

  ALLOCATE_CPLUS_STRUCT_TYPE (type);
  TYPE_FN_FIELDLISTS (type) = (struct fn_fieldlist *)
    TYPE_ALLOC (type, sizeof (struct fn_fieldlist) * fip->nfnfields);

  for (i = 0, flp = fip->fnfieldlists; i < fip->nfnfields; i++, flp++)
    {
      struct nextfnfield *nfp = flp->head;
      struct fn_fieldlist *fn_flp = &TYPE_FN_FIELDLIST (type, i);
      int k;

      TYPE_FN_FIELDLIST_NAME (type, i) = flp->name;
      TYPE_FN_FIELDLIST_LENGTH (type, i) = flp->length;
      fn_flp->fn_fields = (struct fn_field *)
	TYPE_ALLOC (type, sizeof (struct fn_field) * flp->length);
      for (k = flp->length; (k--, nfp); nfp = nfp->next)
	fn_flp->fn_fields[k] = nfp->fnfield;

      total_length += flp->length;
    }

  TYPE_NFN_FIELDS (type) = fip->nfnfields;
  TYPE_NFN_FIELDS_TOTAL (type) = total_length;
}

/* Called when we find the DIE that starts a structure or union scope
   (definition) to process all dies that define the members of the
   structure or union.

   NOTE: we need to call struct_type regardless of whether or not the
   DIE has an at_name attribute, since it might be an anonymous
   structure or union.  This gets the type entered into our set of
   user defined types.

   However, if the structure is incomplete (an opaque struct/union)
   then suppress creating a symbol table entry for it since gdb only
   wants to find the one with the complete definition.  Note that if
   it is complete, we just call new_symbol, which does it's own
   checking about whether the struct/union is anonymous or not (and
   suppresses creating a symbol table entry itself).  */

static void
read_structure_type (struct die_info *die, struct dwarf2_cu *cu)
{
  struct objfile *objfile = cu->objfile;
  struct type *type;
  struct attribute *attr;
  const char *previous_prefix = processing_current_prefix;
  struct cleanup *back_to = NULL;

  if (die->type)
    return;

  type = alloc_type (objfile);

  INIT_CPLUS_SPECIFIC (type);
  attr = dwarf2_attr (die, DW_AT_name, cu);
  if (attr && DW_STRING (attr))
    {
      if (cu->language == language_cplus)
	{
 	  char *new_prefix = determine_class_name (die, cu);
 	  TYPE_TAG_NAME (type) = obsavestring (new_prefix,
 					       strlen (new_prefix),
 					       &objfile->objfile_obstack);
 	  back_to = make_cleanup (xfree, new_prefix);
	  processing_current_prefix = new_prefix;
	}
      else
	{
	  TYPE_TAG_NAME (type) = DW_STRING (attr);
	}
    }

  if (die->tag == DW_TAG_structure_type)
    {
      TYPE_CODE (type) = TYPE_CODE_STRUCT;
    }
  else if (die->tag == DW_TAG_union_type)
    {
      TYPE_CODE (type) = TYPE_CODE_UNION;
    }
  else
    {
      /* FIXME: TYPE_CODE_CLASS is currently defined to TYPE_CODE_STRUCT
         in gdbtypes.h.  */
      TYPE_CODE (type) = TYPE_CODE_CLASS;
    }

  attr = dwarf2_attr (die, DW_AT_byte_size, cu);
  if (attr)
    {
      TYPE_LENGTH (type) = DW_UNSND (attr);
    }
  else
    {
      TYPE_LENGTH (type) = 0;
    }

  /* We need to add the type field to the die immediately so we don't
     infinitely recurse when dealing with pointers to the structure
     type within the structure itself. */
  die->type = type;

  if (die->child != NULL && ! die_is_declaration (die, cu))
    {
      struct field_info fi;
      struct die_info *child_die;
      struct cleanup *back_to = make_cleanup (null_cleanup, NULL);

      memset (&fi, 0, sizeof (struct field_info));

      child_die = die->child;

      while (child_die && child_die->tag)
	{
	  if (child_die->tag == DW_TAG_member
	      || child_die->tag == DW_TAG_variable)
	    {
	      /* NOTE: carlton/2002-11-05: A C++ static data member
		 should be a DW_TAG_member that is a declaration, but
		 all versions of G++ as of this writing (so through at
		 least 3.2.1) incorrectly generate DW_TAG_variable
		 tags for them instead.  */
	      dwarf2_add_field (&fi, child_die, cu);
	    }
	  else if (child_die->tag == DW_TAG_subprogram)
	    {
	      /* C++ member function. */
	      read_type_die (child_die, cu);
	      dwarf2_add_member_fn (&fi, child_die, type, cu);
	    }
	  else if (child_die->tag == DW_TAG_inheritance)
	    {
	      /* C++ base class field.  */
	      dwarf2_add_field (&fi, child_die, cu);
	    }
	  child_die = sibling_die (child_die);
	}

      /* Attach fields and member functions to the type.  */
      if (fi.nfields)
	dwarf2_attach_fields_to_type (&fi, type, cu);
      if (fi.nfnfields)
	{
	  dwarf2_attach_fn_fields_to_type (&fi, type, cu);

	  /* Get the type which refers to the base class (possibly this
	     class itself) which contains the vtable pointer for the current
	     class from the DW_AT_containing_type attribute.  */

	  if (dwarf2_attr (die, DW_AT_containing_type, cu) != NULL)
	    {
	      struct type *t = die_containing_type (die, cu);

	      TYPE_VPTR_BASETYPE (type) = t;
	      if (type == t)
		{
		  static const char vptr_name[] =
		  {'_', 'v', 'p', 't', 'r', '\0'};
		  int i;

		  /* Our own class provides vtbl ptr.  */
		  for (i = TYPE_NFIELDS (t) - 1;
		       i >= TYPE_N_BASECLASSES (t);
		       --i)
		    {
		      char *fieldname = TYPE_FIELD_NAME (t, i);

		      if ((strncmp (fieldname, vptr_name,
                                    strlen (vptr_name) - 1)
                           == 0)
			  && is_cplus_marker (fieldname[strlen (vptr_name)]))
			{
			  TYPE_VPTR_FIELDNO (type) = i;
			  break;
			}
		    }

		  /* Complain if virtual function table field not found.  */
		  if (i < TYPE_N_BASECLASSES (t))
		    complaint (&symfile_complaints,
			       "virtual function table pointer not found when defining class '%s'",
			       TYPE_TAG_NAME (type) ? TYPE_TAG_NAME (type) :
			       "");
		}
	      else
		{
		  TYPE_VPTR_FIELDNO (type) = TYPE_VPTR_FIELDNO (t);
		}
	    }
	}

      do_cleanups (back_to);
    }
  else
    {
      /* No children, must be stub. */
      TYPE_FLAGS (type) |= TYPE_FLAG_STUB;
    }

  processing_current_prefix = previous_prefix;
  if (back_to != NULL)
    do_cleanups (back_to);
}

static void
process_structure_scope (struct die_info *die, struct dwarf2_cu *cu)
{
  struct objfile *objfile = cu->objfile;
  const char *previous_prefix = processing_current_prefix;
  struct die_info *child_die = die->child;

  if (TYPE_TAG_NAME (die->type) != NULL)
    processing_current_prefix = TYPE_TAG_NAME (die->type);

  /* NOTE: carlton/2004-03-16: GCC 3.4 (or at least one of its
     snapshots) has been known to create a die giving a declaration
     for a class that has, as a child, a die giving a definition for a
     nested class.  So we have to process our children even if the
     current die is a declaration.  Normally, of course, a declaration
     won't have any children at all.  */

  while (child_die != NULL && child_die->tag)
    {
      if (child_die->tag == DW_TAG_member
	  || child_die->tag == DW_TAG_variable
	  || child_die->tag == DW_TAG_inheritance)
	{
	  /* Do nothing.  */
	}
      else
	process_die (child_die, cu);

      child_die = sibling_die (child_die);
    }

  if (die->child != NULL && ! die_is_declaration (die, cu))
    new_symbol (die, die->type, cu);

  processing_current_prefix = previous_prefix;
}

/* Given a DW_AT_enumeration_type die, set its type.  We do not
   complete the type's fields yet, or create any symbols.  */

static void
read_enumeration_type (struct die_info *die, struct dwarf2_cu *cu)
{
  struct objfile *objfile = cu->objfile;
  struct type *type;
  struct attribute *attr;

  if (die->type)
    return;

  type = alloc_type (objfile);

  TYPE_CODE (type) = TYPE_CODE_ENUM;
  attr = dwarf2_attr (die, DW_AT_name, cu);
  if (attr && DW_STRING (attr))
    {
      const char *name = DW_STRING (attr);

      if (processing_has_namespace_info)
	{
	  TYPE_TAG_NAME (type) = obconcat (&objfile->objfile_obstack,
					   processing_current_prefix,
					   processing_current_prefix[0] == '\0'
					   ? "" : "::",
					   name);
	}
      else
	{
	  TYPE_TAG_NAME (type) = obsavestring (name, strlen (name),
					       &objfile->objfile_obstack);
	}
    }

  attr = dwarf2_attr (die, DW_AT_byte_size, cu);
  if (attr)
    {
      TYPE_LENGTH (type) = DW_UNSND (attr);
    }
  else
    {
      TYPE_LENGTH (type) = 0;
    }

  die->type = type;
}

/* Determine the name of the type represented by DIE, which should be
   a named C++ compound type.  Return the name in question; the caller
   is responsible for xfree()'ing it.  */

static char *
determine_class_name (struct die_info *die, struct dwarf2_cu *cu)
{
  struct cleanup *back_to = NULL;
  struct die_info *spec_die = die_specification (die, cu);
  char *new_prefix = NULL;

  /* If this is the definition of a class that is declared by another
     die, then processing_current_prefix may not be accurate; see
     read_func_scope for a similar example.  */
  if (spec_die != NULL)
    {
      char *specification_prefix = determine_prefix (spec_die, cu);
      processing_current_prefix = specification_prefix;
      back_to = make_cleanup (xfree, specification_prefix);
    }

  /* If we don't have namespace debug info, guess the name by trying
     to demangle the names of members, just like we did in
     add_partial_structure.  */
  if (!processing_has_namespace_info)
    {
      struct die_info *child;

      for (child = die->child;
	   child != NULL && child->tag != 0;
	   child = sibling_die (child))
	{
	  if (child->tag == DW_TAG_subprogram)
	    {
	      new_prefix = class_name_from_physname (dwarf2_linkage_name
						     (child, cu));

	      if (new_prefix != NULL)
		break;
	    }
	}
    }

  if (new_prefix == NULL)
    {
      const char *name = dwarf2_name (die, cu);
      new_prefix = typename_concat (processing_current_prefix,
				    name ? name : "<<anonymous>>");
    }

  if (back_to != NULL)
    do_cleanups (back_to);

  return new_prefix;
}

/* Given a pointer to a die which begins an enumeration, process all
   the dies that define the members of the enumeration, and create the
   symbol for the enumeration type.

   NOTE: We reverse the order of the element list.  */

static void
process_enumeration_scope (struct die_info *die, struct dwarf2_cu *cu)
{
  struct objfile *objfile = cu->objfile;
  struct die_info *child_die;
  struct field *fields;
  struct attribute *attr;
  struct symbol *sym;
  int num_fields;
  int unsigned_enum = 1;

  num_fields = 0;
  fields = NULL;
  if (die->child != NULL)
    {
      child_die = die->child;
      while (child_die && child_die->tag)
	{
	  if (child_die->tag != DW_TAG_enumerator)
	    {
	      process_die (child_die, cu);
	    }
	  else
	    {
	      attr = dwarf2_attr (child_die, DW_AT_name, cu);
	      if (attr)
		{
		  sym = new_symbol (child_die, die->type, cu);
		  if (SYMBOL_VALUE (sym) < 0)
		    unsigned_enum = 0;

		  if ((num_fields % DW_FIELD_ALLOC_CHUNK) == 0)
		    {
		      fields = (struct field *)
			xrealloc (fields,
				  (num_fields + DW_FIELD_ALLOC_CHUNK)
				  * sizeof (struct field));
		    }

		  FIELD_NAME (fields[num_fields]) = DEPRECATED_SYMBOL_NAME (sym);
		  FIELD_TYPE (fields[num_fields]) = NULL;
		  FIELD_BITPOS (fields[num_fields]) = SYMBOL_VALUE (sym);
		  FIELD_BITSIZE (fields[num_fields]) = 0;
		  FIELD_STATIC_KIND (fields[num_fields]) = 0;

		  num_fields++;
		}
	    }

	  child_die = sibling_die (child_die);
	}

      if (num_fields)
	{
	  TYPE_NFIELDS (die->type) = num_fields;
	  TYPE_FIELDS (die->type) = (struct field *)
	    TYPE_ALLOC (die->type, sizeof (struct field) * num_fields);
	  memcpy (TYPE_FIELDS (die->type), fields,
		  sizeof (struct field) * num_fields);
	  xfree (fields);
	}
      if (unsigned_enum)
	TYPE_FLAGS (die->type) |= TYPE_FLAG_UNSIGNED;
    }

  new_symbol (die, die->type, cu);
}

/* Extract all information from a DW_TAG_array_type DIE and put it in
   the DIE's type field.  For now, this only handles one dimensional
   arrays.  */

static void
read_array_type (struct die_info *die, struct dwarf2_cu *cu)
{
  struct objfile *objfile = cu->objfile;
  struct die_info *child_die;
  struct type *type = NULL;
  struct type *element_type, *range_type, *index_type;
  struct type **range_types = NULL;
  struct attribute *attr;
  int ndim = 0;
  struct cleanup *back_to;

  /* Return if we've already decoded this type. */
  if (die->type)
    {
      return;
    }

  element_type = die_type (die, cu);

  /* Irix 6.2 native cc creates array types without children for
     arrays with unspecified length.  */
  if (die->child == NULL)
    {
      index_type = dwarf2_fundamental_type (objfile, FT_INTEGER, cu);
      range_type = create_range_type (NULL, index_type, 0, -1);
      die->type = create_array_type (NULL, element_type, range_type);
      return;
    }

  back_to = make_cleanup (null_cleanup, NULL);
  child_die = die->child;
  while (child_die && child_die->tag)
    {
      if (child_die->tag == DW_TAG_subrange_type)
	{
          read_subrange_type (child_die, cu);

          if (child_die->type != NULL)
            {
	      /* The range type was succesfully read. Save it for
                 the array type creation.  */
              if ((ndim % DW_FIELD_ALLOC_CHUNK) == 0)
                {
                  range_types = (struct type **)
                    xrealloc (range_types, (ndim + DW_FIELD_ALLOC_CHUNK)
                              * sizeof (struct type *));
                  if (ndim == 0)
                    make_cleanup (free_current_contents, &range_types);
	        }
	      range_types[ndim++] = child_die->type;
            }
	}
      child_die = sibling_die (child_die);
    }

  /* Dwarf2 dimensions are output from left to right, create the
     necessary array types in backwards order.  */
  type = element_type;
  while (ndim-- > 0)
    type = create_array_type (NULL, type, range_types[ndim]);

  /* Understand Dwarf2 support for vector types (like they occur on
     the PowerPC w/ AltiVec).  Gcc just adds another attribute to the
     array type.  This is not part of the Dwarf2/3 standard yet, but a
     custom vendor extension.  The main difference between a regular
     array and the vector variant is that vectors are passed by value
     to functions.  */
  attr = dwarf2_attr (die, DW_AT_GNU_vector, cu);
  if (attr)
    TYPE_FLAGS (type) |= TYPE_FLAG_VECTOR;

  do_cleanups (back_to);

  /* Install the type in the die. */
  die->type = type;
}

/* First cut: install each common block member as a global variable.  */

static void
read_common_block (struct die_info *die, struct dwarf2_cu *cu)
{
  struct die_info *child_die;
  struct attribute *attr;
  struct symbol *sym;
  CORE_ADDR base = (CORE_ADDR) 0;

  attr = dwarf2_attr (die, DW_AT_location, cu);
  if (attr)
    {
      /* Support the .debug_loc offsets */
      if (attr_form_is_block (attr))
        {
          base = decode_locdesc (DW_BLOCK (attr), cu);
        }
      else if (attr->form == DW_FORM_data4 || attr->form == DW_FORM_data8)
        {
	  dwarf2_complex_location_expr_complaint ();
        }
      else
        {
	  dwarf2_invalid_attrib_class_complaint ("DW_AT_location",
						 "common block member");
        }
    }
  if (die->child != NULL)
    {
      child_die = die->child;
      while (child_die && child_die->tag)
	{
	  sym = new_symbol (child_die, NULL, cu);
	  attr = dwarf2_attr (child_die, DW_AT_data_member_location, cu);
	  if (attr)
	    {
	      SYMBOL_VALUE_ADDRESS (sym) =
		base + decode_locdesc (DW_BLOCK (attr), cu);
	      add_symbol_to_list (sym, &global_symbols);
	    }
	  child_die = sibling_die (child_die);
	}
    }
}

/* Read a C++ namespace.  */

static void
read_namespace (struct die_info *die, struct dwarf2_cu *cu)
{
  struct objfile *objfile = cu->objfile;
  const char *previous_prefix = processing_current_prefix;
  const char *name;
  int is_anonymous;
  struct die_info *current_die;

  name = namespace_name (die, &is_anonymous, cu);

  /* Now build the name of the current namespace.  */

  if (previous_prefix[0] == '\0')
    {
      processing_current_prefix = name;
    }
  else
    {
      /* We need temp_name around because processing_current_prefix
	 is a const char *.  */
      char *temp_name = alloca (strlen (previous_prefix)
				+ 2 + strlen(name) + 1);
      strcpy (temp_name, previous_prefix);
      strcat (temp_name, "::");
      strcat (temp_name, name);

      processing_current_prefix = temp_name;
    }

  /* Add a symbol associated to this if we haven't seen the namespace
     before.  Also, add a using directive if it's an anonymous
     namespace.  */

  if (dwarf2_extension (die, cu) == NULL)
    {
      struct type *type;

      /* FIXME: carlton/2003-06-27: Once GDB is more const-correct,
	 this cast will hopefully become unnecessary.  */
      type = init_type (TYPE_CODE_NAMESPACE, 0, 0,
			(char *) processing_current_prefix,
			objfile);
      TYPE_TAG_NAME (type) = TYPE_NAME (type);

      new_symbol (die, type, cu);
      die->type = type;

      if (is_anonymous)
	cp_add_using_directive (processing_current_prefix,
				strlen (previous_prefix),
				strlen (processing_current_prefix));
    }

  if (die->child != NULL)
    {
      struct die_info *child_die = die->child;
      
      while (child_die && child_die->tag)
	{
	  process_die (child_die, cu);
	  child_die = sibling_die (child_die);
	}
    }

  processing_current_prefix = previous_prefix;
}

/* Return the name of the namespace represented by DIE.  Set
   *IS_ANONYMOUS to tell whether or not the namespace is an anonymous
   namespace.  */

static const char *
namespace_name (struct die_info *die, int *is_anonymous, struct dwarf2_cu *cu)
{
  struct die_info *current_die;
  const char *name = NULL;

  /* Loop through the extensions until we find a name.  */

  for (current_die = die;
       current_die != NULL;
       current_die = dwarf2_extension (die, cu))
    {
      name = dwarf2_name (current_die, cu);
      if (name != NULL)
	break;
    }

  /* Is it an anonymous namespace?  */

  *is_anonymous = (name == NULL);
  if (*is_anonymous)
    name = "(anonymous namespace)";

  return name;
}

/* Extract all information from a DW_TAG_pointer_type DIE and add to
   the user defined type vector.  */

static void
read_tag_pointer_type (struct die_info *die, struct dwarf2_cu *cu)
{
  struct comp_unit_head *cu_header = &cu->header;
  struct type *type;
  struct attribute *attr_byte_size;
  struct attribute *attr_address_class;
  int byte_size, addr_class;

  if (die->type)
    {
      return;
    }

  type = lookup_pointer_type (die_type (die, cu));

  attr_byte_size = dwarf2_attr (die, DW_AT_byte_size, cu);
  if (attr_byte_size)
    byte_size = DW_UNSND (attr_byte_size);
  else
    byte_size = cu_header->addr_size;

  attr_address_class = dwarf2_attr (die, DW_AT_address_class, cu);
  if (attr_address_class)
    addr_class = DW_UNSND (attr_address_class);
  else
    addr_class = DW_ADDR_none;

  /* If the pointer size or address class is different than the
     default, create a type variant marked as such and set the
     length accordingly.  */
  if (TYPE_LENGTH (type) != byte_size || addr_class != DW_ADDR_none)
    {
      if (ADDRESS_CLASS_TYPE_FLAGS_P ())
	{
	  int type_flags;

	  type_flags = ADDRESS_CLASS_TYPE_FLAGS (byte_size, addr_class);
	  gdb_assert ((type_flags & ~TYPE_FLAG_ADDRESS_CLASS_ALL) == 0);
	  type = make_type_with_address_space (type, type_flags);
	}
      else if (TYPE_LENGTH (type) != byte_size)
	{
	  complaint (&symfile_complaints, "invalid pointer size %d", byte_size);
	}
      else {
	/* Should we also complain about unhandled address classes?  */
      }
    }

  TYPE_LENGTH (type) = byte_size;
  die->type = type;
}

/* Extract all information from a DW_TAG_ptr_to_member_type DIE and add to
   the user defined type vector.  */

static void
read_tag_ptr_to_member_type (struct die_info *die, struct dwarf2_cu *cu)
{
  struct objfile *objfile = cu->objfile;
  struct type *type;
  struct type *to_type;
  struct type *domain;

  if (die->type)
    {
      return;
    }

  type = alloc_type (objfile);
  to_type = die_type (die, cu);
  domain = die_containing_type (die, cu);
  smash_to_member_type (type, domain, to_type);

  die->type = type;
}

/* Extract all information from a DW_TAG_reference_type DIE and add to
   the user defined type vector.  */

static void
read_tag_reference_type (struct die_info *die, struct dwarf2_cu *cu)
{
  struct comp_unit_head *cu_header = &cu->header;
  struct type *type;
  struct attribute *attr;

  if (die->type)
    {
      return;
    }

  type = lookup_reference_type (die_type (die, cu));
  attr = dwarf2_attr (die, DW_AT_byte_size, cu);
  if (attr)
    {
      TYPE_LENGTH (type) = DW_UNSND (attr);
    }
  else
    {
      TYPE_LENGTH (type) = cu_header->addr_size;
    }
  die->type = type;
}

static void
read_tag_unspecified_type (struct die_info *die, struct dwarf2_cu *cu)
{
  struct objfile *objfile = cu->objfile;
  struct type *type;
  struct attribute *attr;

  if (die->type)
    {
      return;
    }

  type = alloc_type (objfile);
  TYPE_LENGTH (type) = 0;
  attr = dwarf2_attr (die, DW_AT_name, cu);
  if (attr && DW_STRING (attr))
      TYPE_NAME (type) = DW_STRING (attr);

  die->type = type;
}

static void
read_tag_const_type (struct die_info *die, struct dwarf2_cu *cu)
{
  struct type *base_type;

  if (die->type)
    {
      return;
    }

  base_type = die_type (die, cu);
  die->type = make_cvr_type (1, TYPE_VOLATILE (base_type),
                             TYPE_RESTRICT (base_type), base_type, 0);
}

static void
read_tag_volatile_type (struct die_info *die, struct dwarf2_cu *cu)
{
  struct type *base_type;

  if (die->type)
    {
      return;
    }

  base_type = die_type (die, cu);
  die->type = make_cvr_type (TYPE_CONST (base_type), 1,
                             TYPE_RESTRICT (base_type), base_type, 0);
}

static void
read_tag_restrict_type (struct die_info *die, struct dwarf2_cu *cu)
{
  struct type *base_type;

  if (die->type)
    {
      return;
    }

  base_type = die_type (die, cu);
  die->type = make_cvr_type (TYPE_CONST (base_type), TYPE_VOLATILE (base_type),
                             1, base_type, 0);
}

/* Extract all information from a DW_TAG_string_type DIE and add to
   the user defined type vector.  It isn't really a user defined type,
   but it behaves like one, with other DIE's using an AT_user_def_type
   attribute to reference it.  */

static void
read_tag_string_type (struct die_info *die, struct dwarf2_cu *cu)
{
  struct objfile *objfile = cu->objfile;
  struct type *type, *range_type, *index_type, *char_type;
  struct attribute *attr;
  unsigned int length;

  if (die->type)
    {
      return;
    }

  attr = dwarf2_attr (die, DW_AT_string_length, cu);
  if (attr)
    {
      length = DW_UNSND (attr);
    }
  else
    {
      /* check for the DW_AT_byte_size attribute */
      attr = dwarf2_attr (die, DW_AT_byte_size, cu);
      if (attr)
        {
          length = DW_UNSND (attr);
        }
      else
        {
          length = 1;
        }
    }
  index_type = dwarf2_fundamental_type (objfile, FT_INTEGER, cu);
  range_type = create_range_type (NULL, index_type, 1, length);
  if (cu->language == language_fortran)
    {
      /* Need to create a unique string type for bounds
         information */
      type = create_string_type (0, range_type);
    }
  else
    {
      char_type = dwarf2_fundamental_type (objfile, FT_CHAR, cu);
      type = create_string_type (char_type, range_type);
    }
  die->type = type;
}

/* Handle DIES due to C code like:

   struct foo
   {
   int (*funcp)(int a, long l);
   int b;
   };

   ('funcp' generates a DW_TAG_subroutine_type DIE)
 */

static void
read_subroutine_type (struct die_info *die, struct dwarf2_cu *cu)
{
  struct type *type;		/* Type that this function returns */
  struct type *ftype;		/* Function that returns above type */
  struct attribute *attr;

  /* Decode the type that this subroutine returns */
  if (die->type)
    {
      return;
    }
  type = die_type (die, cu);
  ftype = lookup_function_type (type);

  /* All functions in C++ have prototypes.  */
  attr = dwarf2_attr (die, DW_AT_prototyped, cu);
  if ((attr && (DW_UNSND (attr) != 0))
      || cu->language == language_cplus)
    TYPE_FLAGS (ftype) |= TYPE_FLAG_PROTOTYPED;

  if (die->child != NULL)
    {
      struct die_info *child_die;
      int nparams = 0;
      int iparams = 0;

      /* Count the number of parameters.
         FIXME: GDB currently ignores vararg functions, but knows about
         vararg member functions.  */
      child_die = die->child;
      while (child_die && child_die->tag)
	{
	  if (child_die->tag == DW_TAG_formal_parameter)
	    nparams++;
	  else if (child_die->tag == DW_TAG_unspecified_parameters)
	    TYPE_FLAGS (ftype) |= TYPE_FLAG_VARARGS;
	  child_die = sibling_die (child_die);
	}

      /* Allocate storage for parameters and fill them in.  */
      TYPE_NFIELDS (ftype) = nparams;
      TYPE_FIELDS (ftype) = (struct field *)
	TYPE_ALLOC (ftype, nparams * sizeof (struct field));

      child_die = die->child;
      while (child_die && child_die->tag)
	{
	  if (child_die->tag == DW_TAG_formal_parameter)
	    {
	      /* Dwarf2 has no clean way to discern C++ static and non-static
	         member functions. G++ helps GDB by marking the first
	         parameter for non-static member functions (which is the
	         this pointer) as artificial. We pass this information
	         to dwarf2_add_member_fn via TYPE_FIELD_ARTIFICIAL.  */
	      attr = dwarf2_attr (child_die, DW_AT_artificial, cu);
	      if (attr)
		TYPE_FIELD_ARTIFICIAL (ftype, iparams) = DW_UNSND (attr);
	      else
		TYPE_FIELD_ARTIFICIAL (ftype, iparams) = 0;
	      TYPE_FIELD_TYPE (ftype, iparams) = die_type (child_die, cu);
	      iparams++;
	    }
	  child_die = sibling_die (child_die);
	}
    }

  die->type = ftype;
}

static void
read_typedef (struct die_info *die, struct dwarf2_cu *cu)
{
  struct objfile *objfile = cu->objfile;
  struct attribute *attr;
  char *name = NULL;

  if (!die->type)
    {
      attr = dwarf2_attr (die, DW_AT_name, cu);
      if (attr && DW_STRING (attr))
	{
	  name = DW_STRING (attr);
	}
      die->type = init_type (TYPE_CODE_TYPEDEF, 0, TYPE_FLAG_TARGET_STUB, name, objfile);
      TYPE_TARGET_TYPE (die->type) = die_type (die, cu);
    }
}

/* Find a representation of a given base type and install
   it in the TYPE field of the die.  */

static void
read_base_type (struct die_info *die, struct dwarf2_cu *cu)
{
  struct objfile *objfile = cu->objfile;
  struct type *type;
  struct attribute *attr;
  int encoding = 0, size = 0;

  /* If we've already decoded this die, this is a no-op. */
  if (die->type)
    {
      return;
    }

  attr = dwarf2_attr (die, DW_AT_encoding, cu);
  if (attr)
    {
      encoding = DW_UNSND (attr);
    }
  attr = dwarf2_attr (die, DW_AT_byte_size, cu);
  if (attr)
    {
      size = DW_UNSND (attr);
    }
  attr = dwarf2_attr (die, DW_AT_name, cu);
  if (attr && DW_STRING (attr))
    {
      enum type_code code = TYPE_CODE_INT;
      int type_flags = 0;

      switch (encoding)
	{
	case DW_ATE_address:
	  /* Turn DW_ATE_address into a void * pointer.  */
	  code = TYPE_CODE_PTR;
	  type_flags |= TYPE_FLAG_UNSIGNED;
	  break;
	case DW_ATE_boolean:
	  code = TYPE_CODE_BOOL;
	  type_flags |= TYPE_FLAG_UNSIGNED;
	  break;
	case DW_ATE_complex_float:
	  code = TYPE_CODE_COMPLEX;
	  break;
	case DW_ATE_float:
	  code = TYPE_CODE_FLT;
	  break;
	case DW_ATE_signed:
	case DW_ATE_signed_char:
	  break;
	case DW_ATE_unsigned:
	case DW_ATE_unsigned_char:
	  type_flags |= TYPE_FLAG_UNSIGNED;
	  break;
	default:
	  complaint (&symfile_complaints, "unsupported DW_AT_encoding: '%s'",
		     dwarf_type_encoding_name (encoding));
	  break;
	}
      type = init_type (code, size, type_flags, DW_STRING (attr), objfile);
      if (encoding == DW_ATE_address)
	TYPE_TARGET_TYPE (type) = dwarf2_fundamental_type (objfile, FT_VOID,
							   cu);
      else if (encoding == DW_ATE_complex_float)
	{
	  if (size == 32)
	    TYPE_TARGET_TYPE (type)
	      = dwarf2_fundamental_type (objfile, FT_EXT_PREC_FLOAT, cu);
	  else if (size == 16)
	    TYPE_TARGET_TYPE (type)
	      = dwarf2_fundamental_type (objfile, FT_DBL_PREC_FLOAT, cu);
	  else if (size == 8)
	    TYPE_TARGET_TYPE (type)
	      = dwarf2_fundamental_type (objfile, FT_FLOAT, cu);
	}
    }
  else
    {
      type = dwarf_base_type (encoding, size, cu);
    }
  die->type = type;
}

/* Read the given DW_AT_subrange DIE.  */

static void
read_subrange_type (struct die_info *die, struct dwarf2_cu *cu)
{
  struct type *base_type;
  struct type *range_type;
  struct attribute *attr;
  int low = 0;
  int high = -1;
  
  /* If we have already decoded this die, then nothing more to do.  */
  if (die->type)
    return;

  base_type = die_type (die, cu);
  if (base_type == NULL)
    {
      complaint (&symfile_complaints,
                "DW_AT_type missing from DW_TAG_subrange_type");
      return;
    }

  if (TYPE_CODE (base_type) == TYPE_CODE_VOID)
    base_type = alloc_type (NULL);

  if (cu->language == language_fortran)
    { 
      /* FORTRAN implies a lower bound of 1, if not given.  */
      low = 1;
    }

  attr = dwarf2_attr (die, DW_AT_lower_bound, cu);
  if (attr)
    low = dwarf2_get_attr_constant_value (attr, 0);

  attr = dwarf2_attr (die, DW_AT_upper_bound, cu);
  if (attr)
    {       
      if (attr->form == DW_FORM_block1)
        {
          /* GCC encodes arrays with unspecified or dynamic length
             with a DW_FORM_block1 attribute.
             FIXME: GDB does not yet know how to handle dynamic
             arrays properly, treat them as arrays with unspecified
             length for now.

             FIXME: jimb/2003-09-22: GDB does not really know
             how to handle arrays of unspecified length
             either; we just represent them as zero-length
             arrays.  Choose an appropriate upper bound given
             the lower bound we've computed above.  */
          high = low - 1;
        }
      else
        high = dwarf2_get_attr_constant_value (attr, 1);
    }

  range_type = create_range_type (NULL, base_type, low, high);

  attr = dwarf2_attr (die, DW_AT_name, cu);
  if (attr && DW_STRING (attr))
    TYPE_NAME (range_type) = DW_STRING (attr);
  
  attr = dwarf2_attr (die, DW_AT_byte_size, cu);
  if (attr)
    TYPE_LENGTH (range_type) = DW_UNSND (attr);

  die->type = range_type;
}
  

/* Read a whole compilation unit into a linked list of dies.  */

static struct die_info *
read_comp_unit (char *info_ptr, bfd *abfd, struct dwarf2_cu *cu)
{
  /* Reset die reference table; we are
     building new ones now.  */
  dwarf2_empty_hash_tables ();

  return read_die_and_children (info_ptr, abfd, cu, &info_ptr, NULL);
}

/* Read a single die and all its descendents.  Set the die's sibling
   field to NULL; set other fields in the die correctly, and set all
   of the descendents' fields correctly.  Set *NEW_INFO_PTR to the
   location of the info_ptr after reading all of those dies.  PARENT
   is the parent of the die in question.  */

static struct die_info *
read_die_and_children (char *info_ptr, bfd *abfd,
		       struct dwarf2_cu *cu,
		       char **new_info_ptr,
		       struct die_info *parent)
{
  struct die_info *die;
  char *cur_ptr;
  int has_children;

  cur_ptr = read_full_die (&die, abfd, info_ptr, cu, &has_children);
  store_in_ref_table (die->offset, die);

  if (has_children)
    {
      die->child = read_die_and_siblings (cur_ptr, abfd, cu,
					  new_info_ptr, die);
    }
  else
    {
      die->child = NULL;
      *new_info_ptr = cur_ptr;
    }

  die->sibling = NULL;
  die->parent = parent;
  return die;
}

/* Read a die, all of its descendents, and all of its siblings; set
   all of the fields of all of the dies correctly.  Arguments are as
   in read_die_and_children.  */

static struct die_info *
read_die_and_siblings (char *info_ptr, bfd *abfd,
		       struct dwarf2_cu *cu,
		       char **new_info_ptr,
		       struct die_info *parent)
{
  struct die_info *first_die, *last_sibling;
  char *cur_ptr;

  cur_ptr = info_ptr;
  first_die = last_sibling = NULL;

  while (1)
    {
      struct die_info *die
	= read_die_and_children (cur_ptr, abfd, cu, &cur_ptr, parent);

      if (!first_die)
	{
	  first_die = die;
	}
      else
	{
	  last_sibling->sibling = die;
	}

      if (die->tag == 0)
	{
	  *new_info_ptr = cur_ptr;
	  return first_die;
	}
      else
	{
	  last_sibling = die;
	}
    }
}

/* Free a linked list of dies.  */

static void
free_die_list (struct die_info *dies)
{
  struct die_info *die, *next;

  die = dies;
  while (die)
    {
      if (die->child != NULL)
	free_die_list (die->child);
      next = die->sibling;
      xfree (die->attrs);
      xfree (die);
      die = next;
    }
}

static void
do_free_die_list_cleanup (void *dies)
{
  free_die_list (dies);
}

static struct cleanup *
make_cleanup_free_die_list (struct die_info *dies)
{
  return make_cleanup (do_free_die_list_cleanup, dies);
}


/* Read the contents of the section at OFFSET and of size SIZE from the
   object file specified by OBJFILE into the objfile_obstack and return it.  */

char *
dwarf2_read_section (struct objfile *objfile, asection *sectp)
{
  bfd *abfd = objfile->obfd;
  char *buf, *retbuf;
  bfd_size_type size = bfd_get_section_size (sectp);

  if (size == 0)
    return NULL;

  buf = (char *) obstack_alloc (&objfile->objfile_obstack, size);
  retbuf
    = (char *) symfile_relocate_debug_section (abfd, sectp, (bfd_byte *) buf);
  if (retbuf != NULL)
    return retbuf;

  if (bfd_seek (abfd, sectp->filepos, SEEK_SET) != 0
      || bfd_bread (buf, size, abfd) != size)
    error ("Dwarf Error: Can't read DWARF data from '%s'",
	   bfd_get_filename (abfd));

  return buf;
}

/* In DWARF version 2, the description of the debugging information is
   stored in a separate .debug_abbrev section.  Before we read any
   dies from a section we read in all abbreviations and install them
   in a hash table.  */

static void
dwarf2_read_abbrevs (bfd *abfd, struct dwarf2_cu *cu)
{
  struct comp_unit_head *cu_header = &cu->header;
  char *abbrev_ptr;
  struct abbrev_info *cur_abbrev;
  unsigned int abbrev_number, bytes_read, abbrev_name;
  unsigned int abbrev_form, hash_number;

  /* Initialize dwarf2 abbrevs */
  memset (cu_header->dwarf2_abbrevs, 0,
          ABBREV_HASH_SIZE*sizeof (struct abbrev_info *));

  abbrev_ptr = dwarf_abbrev_buffer + cu_header->abbrev_offset;
  abbrev_number = read_unsigned_leb128 (abfd, abbrev_ptr, &bytes_read);
  abbrev_ptr += bytes_read;

  /* loop until we reach an abbrev number of 0 */
  while (abbrev_number)
    {
      cur_abbrev = dwarf_alloc_abbrev ();

      /* read in abbrev header */
      cur_abbrev->number = abbrev_number;
      cur_abbrev->tag = read_unsigned_leb128 (abfd, abbrev_ptr, &bytes_read);
      abbrev_ptr += bytes_read;
      cur_abbrev->has_children = read_1_byte (abfd, abbrev_ptr);
      abbrev_ptr += 1;

      /* now read in declarations */
      abbrev_name = read_unsigned_leb128 (abfd, abbrev_ptr, &bytes_read);
      abbrev_ptr += bytes_read;
      abbrev_form = read_unsigned_leb128 (abfd, abbrev_ptr, &bytes_read);
      abbrev_ptr += bytes_read;
      while (abbrev_name)
	{
	  if ((cur_abbrev->num_attrs % ATTR_ALLOC_CHUNK) == 0)
	    {
	      cur_abbrev->attrs = (struct attr_abbrev *)
		xrealloc (cur_abbrev->attrs,
			  (cur_abbrev->num_attrs + ATTR_ALLOC_CHUNK)
			  * sizeof (struct attr_abbrev));
	    }
	  cur_abbrev->attrs[cur_abbrev->num_attrs].name = abbrev_name;
	  cur_abbrev->attrs[cur_abbrev->num_attrs++].form = abbrev_form;
	  abbrev_name = read_unsigned_leb128 (abfd, abbrev_ptr, &bytes_read);
	  abbrev_ptr += bytes_read;
	  abbrev_form = read_unsigned_leb128 (abfd, abbrev_ptr, &bytes_read);
	  abbrev_ptr += bytes_read;
	}

      hash_number = abbrev_number % ABBREV_HASH_SIZE;
      cur_abbrev->next = cu_header->dwarf2_abbrevs[hash_number];
      cu_header->dwarf2_abbrevs[hash_number] = cur_abbrev;

      /* Get next abbreviation.
         Under Irix6 the abbreviations for a compilation unit are not
         always properly terminated with an abbrev number of 0.
         Exit loop if we encounter an abbreviation which we have
         already read (which means we are about to read the abbreviations
         for the next compile unit) or if the end of the abbreviation
         table is reached.  */
      if ((unsigned int) (abbrev_ptr - dwarf_abbrev_buffer)
	  >= dwarf_abbrev_size)
	break;
      abbrev_number = read_unsigned_leb128 (abfd, abbrev_ptr, &bytes_read);
      abbrev_ptr += bytes_read;
      if (dwarf2_lookup_abbrev (abbrev_number, cu) != NULL)
	break;
    }
}

/* Empty the abbrev table for a new compilation unit.  */

static void
dwarf2_empty_abbrev_table (void *ptr_to_abbrevs_table)
{
  int i;
  struct abbrev_info *abbrev, *next;
  struct abbrev_info **abbrevs;

  abbrevs = (struct abbrev_info **)ptr_to_abbrevs_table;

  for (i = 0; i < ABBREV_HASH_SIZE; ++i)
    {
      next = NULL;
      abbrev = abbrevs[i];
      while (abbrev)
	{
	  next = abbrev->next;
	  xfree (abbrev->attrs);
	  xfree (abbrev);
	  abbrev = next;
	}
      abbrevs[i] = NULL;
    }
}

/* Lookup an abbrev_info structure in the abbrev hash table.  */

static struct abbrev_info *
dwarf2_lookup_abbrev (unsigned int number, struct dwarf2_cu *cu)
{
  struct comp_unit_head *cu_header = &cu->header;
  unsigned int hash_number;
  struct abbrev_info *abbrev;

  hash_number = number % ABBREV_HASH_SIZE;
  abbrev = cu_header->dwarf2_abbrevs[hash_number];

  while (abbrev)
    {
      if (abbrev->number == number)
	return abbrev;
      else
	abbrev = abbrev->next;
    }
  return NULL;
}

/* Read a minimal amount of information into the minimal die structure.  */

static char *
read_partial_die (struct partial_die_info *part_die, bfd *abfd,
		  char *info_ptr, struct dwarf2_cu *cu)
{
  unsigned int abbrev_number, bytes_read, i;
  struct abbrev_info *abbrev;
  struct attribute attr;
  struct attribute spec_attr;
  int found_spec_attr = 0;
  int has_low_pc_attr = 0;
  int has_high_pc_attr = 0;

  *part_die = zeroed_partial_die;
  abbrev_number = read_unsigned_leb128 (abfd, info_ptr, &bytes_read);
  info_ptr += bytes_read;
  if (!abbrev_number)
    return info_ptr;

  abbrev = dwarf2_lookup_abbrev (abbrev_number, cu);
  if (!abbrev)
    {
      error ("Dwarf Error: Could not find abbrev number %d [in module %s]", abbrev_number,
		      bfd_get_filename (abfd));
    }
  part_die->offset = info_ptr - dwarf_info_buffer;
  part_die->tag = abbrev->tag;
  part_die->has_children = abbrev->has_children;
  part_die->abbrev = abbrev_number;

  for (i = 0; i < abbrev->num_attrs; ++i)
    {
      info_ptr = read_attribute (&attr, &abbrev->attrs[i], abfd, info_ptr, cu);

      /* Store the data if it is of an attribute we want to keep in a
         partial symbol table.  */
      switch (attr.name)
	{
	case DW_AT_name:

	  /* Prefer DW_AT_MIPS_linkage_name over DW_AT_name.  */
	  if (part_die->name == NULL)
	    part_die->name = DW_STRING (&attr);
	  break;
	case DW_AT_MIPS_linkage_name:
	  part_die->name = DW_STRING (&attr);
	  break;
	case DW_AT_low_pc:
	  has_low_pc_attr = 1;
	  part_die->lowpc = DW_ADDR (&attr);
	  break;
	case DW_AT_high_pc:
	  has_high_pc_attr = 1;
	  part_die->highpc = DW_ADDR (&attr);
	  break;
	case DW_AT_location:
          /* Support the .debug_loc offsets */
          if (attr_form_is_block (&attr))
            {
	       part_die->locdesc = DW_BLOCK (&attr);
            }
          else if (attr.form == DW_FORM_data4 || attr.form == DW_FORM_data8)
            {
	      dwarf2_complex_location_expr_complaint ();
            }
          else
            {
	      dwarf2_invalid_attrib_class_complaint ("DW_AT_location",
						     "partial symbol information");
            }
	  break;
	case DW_AT_language:
	  part_die->language = DW_UNSND (&attr);
	  break;
	case DW_AT_external:
	  part_die->is_external = DW_UNSND (&attr);
	  break;
	case DW_AT_declaration:
	  part_die->is_declaration = DW_UNSND (&attr);
	  break;
	case DW_AT_type:
	  part_die->has_type = 1;
	  break;
	case DW_AT_abstract_origin:
	case DW_AT_specification:
	  found_spec_attr = 1;
	  spec_attr = attr;
	  break;
	case DW_AT_sibling:
	  /* Ignore absolute siblings, they might point outside of
	     the current compile unit.  */
	  if (attr.form == DW_FORM_ref_addr)
	    complaint (&symfile_complaints, "ignoring absolute DW_AT_sibling");
	  else
	    part_die->sibling =
	      dwarf_info_buffer + dwarf2_get_ref_die_offset (&attr, cu);
	  break;
	default:
	  break;
	}
    }

  /* If we found a reference attribute and the die has no name, try
     to find a name in the referred to die.  */

  if (found_spec_attr && part_die->name == NULL)
    {
      struct partial_die_info spec_die;
      char *spec_ptr;

      spec_ptr = dwarf_info_buffer
	+ dwarf2_get_ref_die_offset (&spec_attr, cu);
      read_partial_die (&spec_die, abfd, spec_ptr, cu);
      if (spec_die.name)
	{
	  part_die->name = spec_die.name;

	  /* Copy DW_AT_external attribute if it is set.  */
	  if (spec_die.is_external)
	    part_die->is_external = spec_die.is_external;
	}
    }

  /* When using the GNU linker, .gnu.linkonce. sections are used to
     eliminate duplicate copies of functions and vtables and such.
     The linker will arbitrarily choose one and discard the others.
     The AT_*_pc values for such functions refer to local labels in
     these sections.  If the section from that file was discarded, the
     labels are not in the output, so the relocs get a value of 0.
     If this is a discarded function, mark the pc bounds as invalid,
     so that GDB will ignore it.  */
  if (has_low_pc_attr && has_high_pc_attr
      && part_die->lowpc < part_die->highpc
      && (part_die->lowpc != 0
	  || (bfd_get_file_flags (abfd) & HAS_RELOC)))
    part_die->has_pc_info = 1;
  return info_ptr;
}

/* Read the die from the .debug_info section buffer.  Set DIEP to
   point to a newly allocated die with its information, except for its
   child, sibling, and parent fields.  Set HAS_CHILDREN to tell
   whether the die has children or not.  */

static char *
read_full_die (struct die_info **diep, bfd *abfd, char *info_ptr,
	       struct dwarf2_cu *cu, int *has_children)
{
  unsigned int abbrev_number, bytes_read, i, offset;
  struct abbrev_info *abbrev;
  struct die_info *die;

  offset = info_ptr - dwarf_info_buffer;
  abbrev_number = read_unsigned_leb128 (abfd, info_ptr, &bytes_read);
  info_ptr += bytes_read;
  if (!abbrev_number)
    {
      die = dwarf_alloc_die ();
      die->tag = 0;
      die->abbrev = abbrev_number;
      die->type = NULL;
      *diep = die;
      *has_children = 0;
      return info_ptr;
    }

  abbrev = dwarf2_lookup_abbrev (abbrev_number, cu);
  if (!abbrev)
    {
      error ("Dwarf Error: could not find abbrev number %d [in module %s]",
	     abbrev_number, 
	     bfd_get_filename (abfd));
    }
  die = dwarf_alloc_die ();
  die->offset = offset;
  die->tag = abbrev->tag;
  die->abbrev = abbrev_number;
  die->type = NULL;

  die->num_attrs = abbrev->num_attrs;
  die->attrs = (struct attribute *)
    xmalloc (die->num_attrs * sizeof (struct attribute));

  for (i = 0; i < abbrev->num_attrs; ++i)
    {
      info_ptr = read_attribute (&die->attrs[i], &abbrev->attrs[i],
				 abfd, info_ptr, cu);
    }

  *diep = die;
  *has_children = abbrev->has_children;
  return info_ptr;
}

/* Read an attribute value described by an attribute form.  */

static char *
read_attribute_value (struct attribute *attr, unsigned form,
		      bfd *abfd, char *info_ptr,
		      struct dwarf2_cu *cu)
{
  struct comp_unit_head *cu_header = &cu->header;
  unsigned int bytes_read;
  struct dwarf_block *blk;

  attr->form = form;
  switch (form)
    {
    case DW_FORM_addr:
    case DW_FORM_ref_addr:
      DW_ADDR (attr) = read_address (abfd, info_ptr, cu, &bytes_read);
      info_ptr += bytes_read;
      break;
    case DW_FORM_block2:
      blk = dwarf_alloc_block ();
      blk->size = read_2_bytes (abfd, info_ptr);
      info_ptr += 2;
      blk->data = read_n_bytes (abfd, info_ptr, blk->size);
      info_ptr += blk->size;
      DW_BLOCK (attr) = blk;
      break;
    case DW_FORM_block4:
      blk = dwarf_alloc_block ();
      blk->size = read_4_bytes (abfd, info_ptr);
      info_ptr += 4;
      blk->data = read_n_bytes (abfd, info_ptr, blk->size);
      info_ptr += blk->size;
      DW_BLOCK (attr) = blk;
      break;
    case DW_FORM_data2:
      DW_UNSND (attr) = read_2_bytes (abfd, info_ptr);
      info_ptr += 2;
      break;
    case DW_FORM_data4:
      DW_UNSND (attr) = read_4_bytes (abfd, info_ptr);
      info_ptr += 4;
      break;
    case DW_FORM_data8:
      DW_UNSND (attr) = read_8_bytes (abfd, info_ptr);
      info_ptr += 8;
      break;
    case DW_FORM_string:
      DW_STRING (attr) = read_string (abfd, info_ptr, &bytes_read);
      info_ptr += bytes_read;
      break;
    case DW_FORM_strp:
      DW_STRING (attr) = read_indirect_string (abfd, info_ptr, cu_header,
					       &bytes_read);
      info_ptr += bytes_read;
      break;
    case DW_FORM_block:
      blk = dwarf_alloc_block ();
      blk->size = read_unsigned_leb128 (abfd, info_ptr, &bytes_read);
      info_ptr += bytes_read;
      blk->data = read_n_bytes (abfd, info_ptr, blk->size);
      info_ptr += blk->size;
      DW_BLOCK (attr) = blk;
      break;
    case DW_FORM_block1:
      blk = dwarf_alloc_block ();
      blk->size = read_1_byte (abfd, info_ptr);
      info_ptr += 1;
      blk->data = read_n_bytes (abfd, info_ptr, blk->size);
      info_ptr += blk->size;
      DW_BLOCK (attr) = blk;
      break;
    case DW_FORM_data1:
      DW_UNSND (attr) = read_1_byte (abfd, info_ptr);
      info_ptr += 1;
      break;
    case DW_FORM_flag:
      DW_UNSND (attr) = read_1_byte (abfd, info_ptr);
      info_ptr += 1;
      break;
    case DW_FORM_flag_present:
      DW_UNSND (attr) = 1;
      break;
    case DW_FORM_sdata:
      DW_SND (attr) = read_signed_leb128 (abfd, info_ptr, &bytes_read);
      info_ptr += bytes_read;
      break;
    case DW_FORM_udata:
      DW_UNSND (attr) = read_unsigned_leb128 (abfd, info_ptr, &bytes_read);
      info_ptr += bytes_read;
      break;
    case DW_FORM_ref1:
      DW_UNSND (attr) = read_1_byte (abfd, info_ptr);
      info_ptr += 1;
      break;
    case DW_FORM_ref2:
      DW_UNSND (attr) = read_2_bytes (abfd, info_ptr);
      info_ptr += 2;
      break;
    case DW_FORM_ref4:
      DW_UNSND (attr) = read_4_bytes (abfd, info_ptr);
      info_ptr += 4;
      break;
    case DW_FORM_ref8:
      DW_UNSND (attr) = read_8_bytes (abfd, info_ptr);
      info_ptr += 8;
      break;
    case DW_FORM_ref_udata:
      DW_UNSND (attr) = read_unsigned_leb128 (abfd, info_ptr, &bytes_read);
      info_ptr += bytes_read;
      break;
    case DW_FORM_indirect:
      form = read_unsigned_leb128 (abfd, info_ptr, &bytes_read);
      info_ptr += bytes_read;
      info_ptr = read_attribute_value (attr, form, abfd, info_ptr, cu);
      break;
    default:
      error ("Dwarf Error: Cannot handle %s in DWARF reader [in module %s]",
	     dwarf_form_name (form),
	     bfd_get_filename (abfd));
    }
  return info_ptr;
}

/* Read an attribute described by an abbreviated attribute.  */

static char *
read_attribute (struct attribute *attr, struct attr_abbrev *abbrev,
		bfd *abfd, char *info_ptr, struct dwarf2_cu *cu)
{
  attr->name = abbrev->name;
  return read_attribute_value (attr, abbrev->form, abfd, info_ptr, cu);
}

/* read dwarf information from a buffer */

static unsigned int
read_1_byte (bfd *abfd, char *buf)
{
  return bfd_get_8 (abfd, (bfd_byte *) buf);
}

static int
read_1_signed_byte (bfd *abfd, char *buf)
{
  return bfd_get_signed_8 (abfd, (bfd_byte *) buf);
}

static unsigned int
read_2_bytes (bfd *abfd, char *buf)
{
  return bfd_get_16 (abfd, (bfd_byte *) buf);
}

static int
read_2_signed_bytes (bfd *abfd, char *buf)
{
  return bfd_get_signed_16 (abfd, (bfd_byte *) buf);
}

static unsigned int
read_4_bytes (bfd *abfd, char *buf)
{
  return bfd_get_32 (abfd, (bfd_byte *) buf);
}

static int
read_4_signed_bytes (bfd *abfd, char *buf)
{
  return bfd_get_signed_32 (abfd, (bfd_byte *) buf);
}

static unsigned long
read_8_bytes (bfd *abfd, char *buf)
{
  return bfd_get_64 (abfd, (bfd_byte *) buf);
}

static CORE_ADDR
read_address (bfd *abfd, char *buf, struct dwarf2_cu *cu, int *bytes_read)
{
  struct comp_unit_head *cu_header = &cu->header;
  CORE_ADDR retval = 0;

  if (cu_header->signed_addr_p)
    {
      switch (cu_header->addr_size)
	{
	case 2:
	  retval = bfd_get_signed_16 (abfd, (bfd_byte *) buf);
	  break;
	case 4:
	  retval = bfd_get_signed_32 (abfd, (bfd_byte *) buf);
	  break;
	case 8:
	  retval = bfd_get_signed_64 (abfd, (bfd_byte *) buf);
	  break;
	default:
	  internal_error (__FILE__, __LINE__,
			  "read_address: bad switch, signed [in module %s]",
			  bfd_get_filename (abfd));
	}
    }
  else
    {
      switch (cu_header->addr_size)
	{
	case 2:
	  retval = bfd_get_16 (abfd, (bfd_byte *) buf);
	  break;
	case 4:
	  retval = bfd_get_32 (abfd, (bfd_byte *) buf);
	  break;
	case 8:
	  retval = bfd_get_64 (abfd, (bfd_byte *) buf);
	  break;
	default:
	  internal_error (__FILE__, __LINE__,
			  "read_address: bad switch, unsigned [in module %s]",
			  bfd_get_filename (abfd));
	}
    }

  *bytes_read = cu_header->addr_size;
  return retval;
}

/* Read the initial length from a section.  The (draft) DWARF 3
   specification allows the initial length to take up either 4 bytes
   or 12 bytes.  If the first 4 bytes are 0xffffffff, then the next 8
   bytes describe the length and all offsets will be 8 bytes in length
   instead of 4.

   An older, non-standard 64-bit format is also handled by this
   function.  The older format in question stores the initial length
   as an 8-byte quantity without an escape value.  Lengths greater
   than 2^32 aren't very common which means that the initial 4 bytes
   is almost always zero.  Since a length value of zero doesn't make
   sense for the 32-bit format, this initial zero can be considered to
   be an escape value which indicates the presence of the older 64-bit
   format.  As written, the code can't detect (old format) lengths
   greater than 4GB.  If it becomes necessary to handle lengths somewhat
   larger than 4GB, we could allow other small values (such as the
   non-sensical values of 1, 2, and 3) to also be used as escape values
   indicating the presence of the old format.

   The value returned via bytes_read should be used to increment
   the relevant pointer after calling read_initial_length().
   
   As a side effect, this function sets the fields initial_length_size
   and offset_size in cu_header to the values appropriate for the
   length field.  (The format of the initial length field determines
   the width of file offsets to be fetched later with fetch_offset().)
   
   [ Note:  read_initial_length() and read_offset() are based on the
     document entitled "DWARF Debugging Information Format", revision
     3, draft 8, dated November 19, 2001.  This document was obtained
     from:

	http://reality.sgiweb.org/davea/dwarf3-draft8-011125.pdf
     
     This document is only a draft and is subject to change.  (So beware.)

     Details regarding the older, non-standard 64-bit format were
     determined empirically by examining 64-bit ELF files produced
     by the SGI toolchain on an IRIX 6.5 machine.

     - Kevin, July 16, 2002
   ] */

static LONGEST
read_initial_length (bfd *abfd, char *buf, struct comp_unit_head *cu_header,
                     int *bytes_read)
{
  LONGEST retval = 0;

  retval = bfd_get_32 (abfd, (bfd_byte *) buf);

  if (retval == 0xffffffff)
    {
      retval = bfd_get_64 (abfd, (bfd_byte *) buf + 4);
      *bytes_read = 12;
      if (cu_header != NULL)
	{
	  cu_header->initial_length_size = 12;
	  cu_header->offset_size = 8;
	}
    }
  else if (retval == 0)
    {
      /* Handle (non-standard) 64-bit DWARF2 formats such as that used
         by IRIX.  */
      retval = bfd_get_64 (abfd, (bfd_byte *) buf);
      *bytes_read = 8;
      if (cu_header != NULL)
	{
	  cu_header->initial_length_size = 8;
	  cu_header->offset_size = 8;
	}
    }
  else
    {
      *bytes_read = 4;
      if (cu_header != NULL)
	{
	  cu_header->initial_length_size = 4;
	  cu_header->offset_size = 4;
	}
    }

 return retval;
}

/* Read an offset from the data stream.  The size of the offset is
   given by cu_header->offset_size. */

static LONGEST
read_offset (bfd *abfd, char *buf, const struct comp_unit_head *cu_header,
             int *bytes_read)
{
  LONGEST retval = 0;

  switch (cu_header->offset_size)
    {
    case 4:
      retval = bfd_get_32 (abfd, (bfd_byte *) buf);
      *bytes_read = 4;
      break;
    case 8:
      retval = bfd_get_64 (abfd, (bfd_byte *) buf);
      *bytes_read = 8;
      break;
    default:
      internal_error (__FILE__, __LINE__,
		      "read_offset: bad switch [in module %s]",
		      bfd_get_filename (abfd));
    }

 return retval;
}

static char *
read_n_bytes (bfd *abfd, char *buf, unsigned int size)
{
  /* If the size of a host char is 8 bits, we can return a pointer
     to the buffer, otherwise we have to copy the data to a buffer
     allocated on the temporary obstack.  */
  gdb_assert (HOST_CHAR_BIT == 8);
  return buf;
}

static char *
read_string (bfd *abfd, char *buf, unsigned int *bytes_read_ptr)
{
  /* If the size of a host char is 8 bits, we can return a pointer
     to the string, otherwise we have to copy the string to a buffer
     allocated on the temporary obstack.  */
  gdb_assert (HOST_CHAR_BIT == 8);
  if (*buf == '\0')
    {
      *bytes_read_ptr = 1;
      return NULL;
    }
  *bytes_read_ptr = strlen (buf) + 1;
  return buf;
}

static char *
read_indirect_string (bfd *abfd, char *buf,
		      const struct comp_unit_head *cu_header,
		      unsigned int *bytes_read_ptr)
{
  LONGEST str_offset = read_offset (abfd, buf, cu_header,
				    (int *) bytes_read_ptr);

  if (dwarf_str_buffer == NULL)
    {
      error ("DW_FORM_strp used without .debug_str section [in module %s]",
		      bfd_get_filename (abfd));
      return NULL;
    }
  if (str_offset >= dwarf_str_size)
    {
      error ("DW_FORM_strp pointing outside of .debug_str section [in module %s]",
		      bfd_get_filename (abfd));
      return NULL;
    }
  gdb_assert (HOST_CHAR_BIT == 8);
  if (dwarf_str_buffer[str_offset] == '\0')
    return NULL;
  return dwarf_str_buffer + str_offset;
}

static unsigned long
read_unsigned_leb128 (bfd *abfd, char *buf, unsigned int *bytes_read_ptr)
{
  unsigned long result;
  unsigned int num_read;
  int i, shift;
  unsigned char byte;

  result = 0;
  shift = 0;
  num_read = 0;
  i = 0;
  while (1)
    {
      byte = bfd_get_8 (abfd, (bfd_byte *) buf);
      buf++;
      num_read++;
      result |= ((unsigned long)(byte & 127) << shift);
      if ((byte & 128) == 0)
	{
	  break;
	}
      shift += 7;
    }
  *bytes_read_ptr = num_read;
  return result;
}

static long
read_signed_leb128 (bfd *abfd, char *buf, unsigned int *bytes_read_ptr)
{
  long result;
  int i, shift, size, num_read;
  unsigned char byte;

  result = 0;
  shift = 0;
  size = 32;
  num_read = 0;
  i = 0;
  while (1)
    {
      byte = bfd_get_8 (abfd, (bfd_byte *) buf);
      buf++;
      num_read++;
      result |= ((long)(byte & 127) << shift);
      shift += 7;
      if ((byte & 128) == 0)
	{
	  break;
	}
    }
  if ((shift < size) && (byte & 0x40))
    {
      result |= -(1 << shift);
    }
  *bytes_read_ptr = num_read;
  return result;
}

static void
set_cu_language (unsigned int lang, struct dwarf2_cu *cu)
{
  switch (lang)
    {
    case DW_LANG_C89:
    case DW_LANG_C:
      cu->language = language_c;
      break;
    case DW_LANG_C_plus_plus:
      cu->language = language_cplus;
      break;
    case DW_LANG_Fortran77:
    case DW_LANG_Fortran90:
    case DW_LANG_Fortran95:
      cu->language = language_fortran;
      break;
    case DW_LANG_Mips_Assembler:
      cu->language = language_asm;
      break;
    case DW_LANG_Java:
      cu->language = language_java;
      break;
    case DW_LANG_Ada83:
    case DW_LANG_Ada95:
    case DW_LANG_Cobol74:
    case DW_LANG_Cobol85:
    case DW_LANG_Pascal83:
    case DW_LANG_Modula2:
    default:
      cu->language = language_minimal;
      break;
    }
  cu->language_defn = language_def (cu->language);
}

/* Return the named attribute or NULL if not there.  */

static struct attribute *
dwarf2_attr (struct die_info *die, unsigned int name, struct dwarf2_cu *cu)
{
  unsigned int i;
  struct attribute *spec = NULL;

  for (i = 0; i < die->num_attrs; ++i)
    {
      if (die->attrs[i].name == name)
	{
	  return &die->attrs[i];
	}
      if (die->attrs[i].name == DW_AT_specification
	  || die->attrs[i].name == DW_AT_abstract_origin)
	spec = &die->attrs[i];
    }
  if (spec)
    {
      struct die_info *ref_die =
      follow_die_ref (dwarf2_get_ref_die_offset (spec, cu));

      if (ref_die)
	return dwarf2_attr (ref_die, name, cu);
    }

  return NULL;
}

static int
die_is_declaration (struct die_info *die, struct dwarf2_cu *cu)
{
  return (dwarf2_attr (die, DW_AT_declaration, cu)
	  && ! dwarf2_attr (die, DW_AT_specification, cu));
}

/* Return the die giving the specification for DIE, if there is
   one.  */

static struct die_info *
die_specification (struct die_info *die, struct dwarf2_cu *cu)
{
  struct attribute *spec_attr = dwarf2_attr (die, DW_AT_specification, cu);

  if (spec_attr == NULL)
    return NULL;
  else
    return follow_die_ref (dwarf2_get_ref_die_offset (spec_attr, cu));
}

/* Free the line_header structure *LH, and any arrays and strings it
   refers to.  */
static void
free_line_header (struct line_header *lh)
{
  if (lh->standard_opcode_lengths)
    xfree (lh->standard_opcode_lengths);

  /* Remember that all the lh->file_names[i].name pointers are
     pointers into debug_line_buffer, and don't need to be freed.  */
  if (lh->file_names)
    xfree (lh->file_names);

  /* Similarly for the include directory names.  */
  if (lh->include_dirs)
    xfree (lh->include_dirs);

  xfree (lh);
}


/* Add an entry to LH's include directory table.  */
static void
add_include_dir (struct line_header *lh, char *include_dir)
{
  /* Grow the array if necessary.  */
  if (lh->include_dirs_size == 0)
    {
      lh->include_dirs_size = 1; /* for testing */
      lh->include_dirs = xmalloc (lh->include_dirs_size
                                  * sizeof (*lh->include_dirs));
    }
  else if (lh->num_include_dirs >= lh->include_dirs_size)
    {
      lh->include_dirs_size *= 2;
      lh->include_dirs = xrealloc (lh->include_dirs,
                                   (lh->include_dirs_size
                                    * sizeof (*lh->include_dirs)));
    }

  lh->include_dirs[lh->num_include_dirs++] = include_dir;
}
 

/* Add an entry to LH's file name table.  */
static void
add_file_name (struct line_header *lh,
               char *name,
               unsigned int dir_index,
               unsigned int mod_time,
               unsigned int length)
{
  struct file_entry *fe;

  /* Grow the array if necessary.  */
  if (lh->file_names_size == 0)
    {
      lh->file_names_size = 1; /* for testing */
      lh->file_names = xmalloc (lh->file_names_size
                                * sizeof (*lh->file_names));
    }
  else if (lh->num_file_names >= lh->file_names_size)
    {
      lh->file_names_size *= 2;
      lh->file_names = xrealloc (lh->file_names,
                                 (lh->file_names_size
                                  * sizeof (*lh->file_names)));
    }

  fe = &lh->file_names[lh->num_file_names++];
  fe->name = name;
  fe->dir_index = dir_index;
  fe->mod_time = mod_time;
  fe->length = length;
}
 

/* Read the statement program header starting at OFFSET in
   dwarf_line_buffer, according to the endianness of ABFD.  Return a
   pointer to a struct line_header, allocated using xmalloc.

   NOTE: the strings in the include directory and file name tables of
   the returned object point into debug_line_buffer, and must not be
   freed.  */
static struct line_header *
dwarf_decode_line_header (unsigned int offset, bfd *abfd,
			  struct dwarf2_cu *cu)
{
  struct cleanup *back_to;
  struct line_header *lh;
  char *line_ptr;
  int bytes_read;
  int i;
  char *cur_dir, *cur_file;

  if (dwarf_line_buffer == NULL)
    {
      complaint (&symfile_complaints, "missing .debug_line section");
      return 0;
    }

  /* Make sure that at least there's room for the total_length field.  That
     could be 12 bytes long, but we're just going to fudge that.  */
  if (offset + 4 >= dwarf_line_size)
    {
      dwarf2_statement_list_fits_in_line_number_section_complaint ();
      return 0;
    }

  lh = xmalloc (sizeof (*lh));
  memset (lh, 0, sizeof (*lh));
  back_to = make_cleanup ((make_cleanup_ftype *) free_line_header,
                          (void *) lh);

  line_ptr = dwarf_line_buffer + offset;

  /* read in the header */
  lh->total_length = read_initial_length (abfd, line_ptr, &cu->header, &bytes_read);
  line_ptr += bytes_read;
  if (line_ptr + lh->total_length > dwarf_line_buffer + dwarf_line_size)
    {
      dwarf2_statement_list_fits_in_line_number_section_complaint ();
      return 0;
    }
  lh->statement_program_end = line_ptr + lh->total_length;
  lh->version = read_2_bytes (abfd, line_ptr);
  line_ptr += 2;
  lh->header_length = read_offset (abfd, line_ptr, &cu->header, &bytes_read);
  line_ptr += bytes_read;
  lh->minimum_instruction_length = read_1_byte (abfd, line_ptr);
  line_ptr += 1;
  lh->default_is_stmt = read_1_byte (abfd, line_ptr);
  line_ptr += 1;
  lh->line_base = read_1_signed_byte (abfd, line_ptr);
  line_ptr += 1;
  lh->line_range = read_1_byte (abfd, line_ptr);
  line_ptr += 1;
  lh->opcode_base = read_1_byte (abfd, line_ptr);
  line_ptr += 1;
  lh->standard_opcode_lengths
    = (unsigned char *) xmalloc (lh->opcode_base * sizeof (unsigned char));

  lh->standard_opcode_lengths[0] = 1;  /* This should never be used anyway.  */
  for (i = 1; i < lh->opcode_base; ++i)
    {
      lh->standard_opcode_lengths[i] = read_1_byte (abfd, line_ptr);
      line_ptr += 1;
    }

  /* Read directory table  */
  while ((cur_dir = read_string (abfd, line_ptr, &bytes_read)) != NULL)
    {
      line_ptr += bytes_read;
      add_include_dir (lh, cur_dir);
    }
  line_ptr += bytes_read;

  /* Read file name table */
  while ((cur_file = read_string (abfd, line_ptr, &bytes_read)) != NULL)
    {
      unsigned int dir_index, mod_time, length;

      line_ptr += bytes_read;
      dir_index = read_unsigned_leb128 (abfd, line_ptr, &bytes_read);
      line_ptr += bytes_read;
      mod_time = read_unsigned_leb128 (abfd, line_ptr, &bytes_read);
      line_ptr += bytes_read;
      length = read_unsigned_leb128 (abfd, line_ptr, &bytes_read);
      line_ptr += bytes_read;

      add_file_name (lh, cur_file, dir_index, mod_time, length);
    }
  line_ptr += bytes_read;
  lh->statement_program_start = line_ptr; 

  if (line_ptr > dwarf_line_buffer + dwarf_line_size)
    complaint (&symfile_complaints,
	       "line number info header doesn't fit in `.debug_line' section");

  discard_cleanups (back_to);
  return lh;
}

/* This function exists to work around a bug in certain compilers
   (particularly GCC 2.95), in which the first line number marker of a
   function does not show up until after the prologue, right before
   the second line number marker.  This function shifts ADDRESS down
   to the beginning of the function if necessary, and is called on
   addresses passed to record_line.  */

static CORE_ADDR
check_cu_functions (CORE_ADDR address, struct dwarf2_cu *cu)
{
  struct function_range *fn;

  /* Find the function_range containing address.  */
  if (!cu->first_fn)
    return address;

  if (!cu->cached_fn)
    cu->cached_fn = cu->first_fn;

  fn = cu->cached_fn;
  while (fn)
    if (fn->lowpc <= address && fn->highpc > address)
      goto found;
    else
      fn = fn->next;

  fn = cu->first_fn;
  while (fn && fn != cu->cached_fn)
    if (fn->lowpc <= address && fn->highpc > address)
      goto found;
    else
      fn = fn->next;

  return address;

 found:
  if (fn->seen_line)
    return address;
  if (address != fn->lowpc)
    complaint (&symfile_complaints,
	       "misplaced first line number at 0x%lx for '%s'",
	       (unsigned long) address, fn->name);
  fn->seen_line = 1;
  return fn->lowpc;
}

/* Decode the line number information for the compilation unit whose
   line number info is at OFFSET in the .debug_line section.
   The compilation directory of the file is passed in COMP_DIR.  */

static void
dwarf_decode_lines (struct line_header *lh, char *comp_dir, bfd *abfd,
		    struct dwarf2_cu *cu)
{
  char *line_ptr;
  char *line_end;
  unsigned int bytes_read;
  unsigned char op_code, extended_op, adj_opcode;
  CORE_ADDR baseaddr;
  struct objfile *objfile = cu->objfile;

  baseaddr = ANOFFSET (objfile->section_offsets, SECT_OFF_TEXT (objfile));

  line_ptr = lh->statement_program_start;
  line_end = lh->statement_program_end;

  /* Read the statement sequences until there's nothing left.  */
  while (line_ptr < line_end)
    {
      /* state machine registers  */
      CORE_ADDR address = 0;
      unsigned int file = 1;
      unsigned int line = 1;
      unsigned int column = 0;
      int is_stmt = lh->default_is_stmt;
      int basic_block = 0;
      int end_sequence = 0;

      /* Start a subfile for the current file of the state machine.  */
      if (lh->num_file_names >= file)
	{
	  /* lh->include_dirs and lh->file_names are 0-based, but the
	     directory and file name numbers in the statement program
	     are 1-based.  */
          struct file_entry *fe = &lh->file_names[file - 1];
          char *dir;
          if (fe->dir_index)
            dir = lh->include_dirs[fe->dir_index - 1];
          else
            dir = comp_dir;
	  dwarf2_start_subfile (fe->name, dir);
	}

      /* Decode the table. */
      while (!end_sequence)
	{
	  op_code = read_1_byte (abfd, line_ptr);
	  line_ptr += 1;

	  if (op_code >= lh->opcode_base)
	    {		/* Special operand.  */
	      adj_opcode = op_code - lh->opcode_base;
	      address += (adj_opcode / lh->line_range)
		* lh->minimum_instruction_length;
	      line += lh->line_base + (adj_opcode % lh->line_range);
	      /* append row to matrix using current values */
	      record_line (current_subfile, line, 
	                   check_cu_functions (address, cu));
	      basic_block = 1;
	    }
	  else switch (op_code)
	    {
	    case DW_LNS_extended_op:
	      line_ptr += 1;	/* ignore length */
	      extended_op = read_1_byte (abfd, line_ptr);
	      line_ptr += 1;
	      switch (extended_op)
		{
		case DW_LNE_end_sequence:
		  end_sequence = 1;
		  record_line (current_subfile, 0, address);
		  break;
		case DW_LNE_set_address:
		  address = read_address (abfd, line_ptr, cu, &bytes_read);
		  line_ptr += bytes_read;
		  address += baseaddr;
		  break;
		case DW_LNE_define_file:
                  {
                    char *cur_file;
                    unsigned int dir_index, mod_time, length;
                    
                    cur_file = read_string (abfd, line_ptr, &bytes_read);
                    line_ptr += bytes_read;
                    dir_index =
                      read_unsigned_leb128 (abfd, line_ptr, &bytes_read);
                    line_ptr += bytes_read;
                    mod_time =
                      read_unsigned_leb128 (abfd, line_ptr, &bytes_read);
                    line_ptr += bytes_read;
                    length =
                      read_unsigned_leb128 (abfd, line_ptr, &bytes_read);
                    line_ptr += bytes_read;
                    add_file_name (lh, cur_file, dir_index, mod_time, length);
                  }
		  break;
		default:
		  complaint (&symfile_complaints,
			     "mangled .debug_line section");
		  return;
		}
	      break;
	    case DW_LNS_copy:
	      record_line (current_subfile, line, 
	                   check_cu_functions (address, cu));
	      basic_block = 0;
	      break;
	    case DW_LNS_advance_pc:
	      address += lh->minimum_instruction_length
		* read_unsigned_leb128 (abfd, line_ptr, &bytes_read);
	      line_ptr += bytes_read;
	      break;
	    case DW_LNS_advance_line:
	      line += read_signed_leb128 (abfd, line_ptr, &bytes_read);
	      line_ptr += bytes_read;
	      break;
	    case DW_LNS_set_file:
              {
                /* lh->include_dirs and lh->file_names are 0-based,
                   but the directory and file name numbers in the
                   statement program are 1-based.  */
                struct file_entry *fe;
                char *dir;
                file = read_unsigned_leb128 (abfd, line_ptr, &bytes_read);
                line_ptr += bytes_read;
                fe = &lh->file_names[file - 1];
                if (fe->dir_index)
                  dir = lh->include_dirs[fe->dir_index - 1];
                else
                  dir = comp_dir;
                dwarf2_start_subfile (fe->name, dir);
              }
	      break;
	    case DW_LNS_set_column:
	      column = read_unsigned_leb128 (abfd, line_ptr, &bytes_read);
	      line_ptr += bytes_read;
	      break;
	    case DW_LNS_negate_stmt:
	      is_stmt = (!is_stmt);
	      break;
	    case DW_LNS_set_basic_block:
	      basic_block = 1;
	      break;
	    /* Add to the address register of the state machine the
	       address increment value corresponding to special opcode
	       255.  Ie, this value is scaled by the minimum instruction
	       length since special opcode 255 would have scaled the
	       the increment.  */
	    case DW_LNS_const_add_pc:
	      address += (lh->minimum_instruction_length
			  * ((255 - lh->opcode_base) / lh->line_range));
	      break;
	    case DW_LNS_fixed_advance_pc:
	      address += read_2_bytes (abfd, line_ptr);
	      line_ptr += 2;
	      break;
	    default:
	      {  /* Unknown standard opcode, ignore it.  */
		int i;
		for (i = 0; i < lh->standard_opcode_lengths[op_code]; i++)
		  {
		    (void) read_unsigned_leb128 (abfd, line_ptr, &bytes_read);
		    line_ptr += bytes_read;
		  }
	      }
	    }
	}
    }
}

/* Start a subfile for DWARF.  FILENAME is the name of the file and
   DIRNAME the name of the source directory which contains FILENAME
   or NULL if not known.
   This routine tries to keep line numbers from identical absolute and
   relative file names in a common subfile.

   Using the `list' example from the GDB testsuite, which resides in
   /srcdir and compiling it with Irix6.2 cc in /compdir using a filename
   of /srcdir/list0.c yields the following debugging information for list0.c:

   DW_AT_name:          /srcdir/list0.c
   DW_AT_comp_dir:              /compdir
   files.files[0].name: list0.h
   files.files[0].dir:  /srcdir
   files.files[1].name: list0.c
   files.files[1].dir:  /srcdir

   The line number information for list0.c has to end up in a single
   subfile, so that `break /srcdir/list0.c:1' works as expected.  */

static void
dwarf2_start_subfile (char *filename, char *dirname)
{
  /* If the filename isn't absolute, try to match an existing subfile
     with the full pathname.  */

  if (!IS_ABSOLUTE_PATH (filename) && dirname != NULL)
    {
      struct subfile *subfile;
      char *fullname = concat (dirname, "/", filename, NULL);

      for (subfile = subfiles; subfile; subfile = subfile->next)
	{
	  if (FILENAME_CMP (subfile->name, fullname) == 0)
	    {
	      current_subfile = subfile;
	      xfree (fullname);
	      return;
	    }
	}
      xfree (fullname);
    }
  start_subfile (filename, dirname);
}

static void
var_decode_location (struct attribute *attr, struct symbol *sym,
		     struct dwarf2_cu *cu)
{
  struct objfile *objfile = cu->objfile;
  struct comp_unit_head *cu_header = &cu->header;

  /* NOTE drow/2003-01-30: There used to be a comment and some special
     code here to turn a symbol with DW_AT_external and a
     SYMBOL_VALUE_ADDRESS of 0 into a LOC_UNRESOLVED symbol.  This was
     necessary for platforms (maybe Alpha, certainly PowerPC GNU/Linux
     with some versions of binutils) where shared libraries could have
     relocations against symbols in their debug information - the
     minimal symbol would have the right address, but the debug info
     would not.  It's no longer necessary, because we will explicitly
     apply relocations when we read in the debug information now.  */

  /* A DW_AT_location attribute with no contents indicates that a
     variable has been optimized away.  */
  if (attr_form_is_block (attr) && DW_BLOCK (attr)->size == 0)
    {
      SYMBOL_CLASS (sym) = LOC_OPTIMIZED_OUT;
      return;
    }

  /* Handle one degenerate form of location expression specially, to
     preserve GDB's previous behavior when section offsets are
     specified.  If this is just a DW_OP_addr then mark this symbol
     as LOC_STATIC.  */

  if (attr_form_is_block (attr)
      && DW_BLOCK (attr)->size == 1 + cu_header->addr_size
      && DW_BLOCK (attr)->data[0] == DW_OP_addr)
    {
      int dummy;

      SYMBOL_VALUE_ADDRESS (sym) =
	read_address (objfile->obfd, DW_BLOCK (attr)->data + 1, cu, &dummy);
      fixup_symbol_section (sym, objfile);
      SYMBOL_VALUE_ADDRESS (sym) += ANOFFSET (objfile->section_offsets,
					      SYMBOL_SECTION (sym));
      SYMBOL_CLASS (sym) = LOC_STATIC;
      return;
    }

  /* NOTE drow/2002-01-30: It might be worthwhile to have a static
     expression evaluator, and use LOC_COMPUTED only when necessary
     (i.e. when the value of a register or memory location is
     referenced, or a thread-local block, etc.).  Then again, it might
     not be worthwhile.  I'm assuming that it isn't unless performance
     or memory numbers show me otherwise.  */

  dwarf2_symbol_mark_computed (attr, sym, cu);
  SYMBOL_CLASS (sym) = LOC_COMPUTED;
}

/* Given a pointer to a DWARF information entry, figure out if we need
   to make a symbol table entry for it, and if so, create a new entry
   and return a pointer to it.
   If TYPE is NULL, determine symbol type from the die, otherwise
   used the passed type.  */

static struct symbol *
new_symbol (struct die_info *die, struct type *type, struct dwarf2_cu *cu)
{
  struct objfile *objfile = cu->objfile;
  struct symbol *sym = NULL;
  char *name;
  struct attribute *attr = NULL;
  struct attribute *attr2 = NULL;
  CORE_ADDR baseaddr;

  baseaddr = ANOFFSET (objfile->section_offsets, SECT_OFF_TEXT (objfile));

  if (die->tag != DW_TAG_namespace)
    name = dwarf2_linkage_name (die, cu);
  else
    name = TYPE_NAME (type);

  if (name)
    {
      sym = (struct symbol *) obstack_alloc (&objfile->objfile_obstack,
					     sizeof (struct symbol));
      OBJSTAT (objfile, n_syms++);
      memset (sym, 0, sizeof (struct symbol));

      /* Cache this symbol's name and the name's demangled form (if any).  */
      SYMBOL_LANGUAGE (sym) = cu->language;
      SYMBOL_SET_NAMES (sym, name, strlen (name), objfile);

      /* Default assumptions.
         Use the passed type or decode it from the die.  */
      SYMBOL_DOMAIN (sym) = VAR_DOMAIN;
      SYMBOL_CLASS (sym) = LOC_STATIC;
      if (type != NULL)
	SYMBOL_TYPE (sym) = type;
      else
	SYMBOL_TYPE (sym) = die_type (die, cu);
      attr = dwarf2_attr (die, DW_AT_decl_line, cu);
      if (attr)
	{
	  SYMBOL_LINE (sym) = DW_UNSND (attr);
	}
      switch (die->tag)
	{
	case DW_TAG_label:
	  attr = dwarf2_attr (die, DW_AT_low_pc, cu);
	  if (attr)
	    {
	      SYMBOL_VALUE_ADDRESS (sym) = DW_ADDR (attr) + baseaddr;
	    }
	  SYMBOL_CLASS (sym) = LOC_LABEL;
	  break;
	case DW_TAG_subprogram:
	  /* SYMBOL_BLOCK_VALUE (sym) will be filled in later by
	     finish_block.  */
	  SYMBOL_CLASS (sym) = LOC_BLOCK;
	  attr2 = dwarf2_attr (die, DW_AT_external, cu);
	  if (attr2 && (DW_UNSND (attr2) != 0))
	    {
	      add_symbol_to_list (sym, &global_symbols);
	    }
	  else
	    {
	      add_symbol_to_list (sym, cu->list_in_scope);
	    }
	  break;
	case DW_TAG_variable:
	  /* Compilation with minimal debug info may result in variables
	     with missing type entries. Change the misleading `void' type
	     to something sensible.  */
	  if (TYPE_CODE (SYMBOL_TYPE (sym)) == TYPE_CODE_VOID)
	    SYMBOL_TYPE (sym) = init_type (TYPE_CODE_INT,
					   TARGET_INT_BIT / HOST_CHAR_BIT, 0,
					   "<variable, no debug info>",
					   objfile);
	  attr = dwarf2_attr (die, DW_AT_const_value, cu);
	  if (attr)
	    {
	      dwarf2_const_value (attr, sym, cu);
	      attr2 = dwarf2_attr (die, DW_AT_external, cu);
	      if (attr2 && (DW_UNSND (attr2) != 0))
		add_symbol_to_list (sym, &global_symbols);
	      else
		add_symbol_to_list (sym, cu->list_in_scope);
	      break;
	    }
	  attr = dwarf2_attr (die, DW_AT_location, cu);
	  if (attr)
	    {
	      var_decode_location (attr, sym, cu);
	      attr2 = dwarf2_attr (die, DW_AT_external, cu);
	      if (attr2 && (DW_UNSND (attr2) != 0))
		add_symbol_to_list (sym, &global_symbols);
	      else
		add_symbol_to_list (sym, cu->list_in_scope);
	    }
	  else
	    {
	      /* We do not know the address of this symbol.
	         If it is an external symbol and we have type information
	         for it, enter the symbol as a LOC_UNRESOLVED symbol.
	         The address of the variable will then be determined from
	         the minimal symbol table whenever the variable is
	         referenced.  */
	      attr2 = dwarf2_attr (die, DW_AT_external, cu);
	      if (attr2 && (DW_UNSND (attr2) != 0)
		  && dwarf2_attr (die, DW_AT_type, cu) != NULL)
		{
		  SYMBOL_CLASS (sym) = LOC_UNRESOLVED;
		  add_symbol_to_list (sym, &global_symbols);
		}
	    }
	  break;
	case DW_TAG_formal_parameter:
	  attr = dwarf2_attr (die, DW_AT_location, cu);
	  if (attr)
	    {
	      var_decode_location (attr, sym, cu);
	      /* FIXME drow/2003-07-31: Is LOC_COMPUTED_ARG necessary?  */
	      if (SYMBOL_CLASS (sym) == LOC_COMPUTED)
		SYMBOL_CLASS (sym) = LOC_COMPUTED_ARG;
	    }
	  attr = dwarf2_attr (die, DW_AT_const_value, cu);
	  if (attr)
	    {
	      dwarf2_const_value (attr, sym, cu);
	    }
	  add_symbol_to_list (sym, cu->list_in_scope);
	  break;
	case DW_TAG_unspecified_parameters:
	  /* From varargs functions; gdb doesn't seem to have any
	     interest in this information, so just ignore it for now.
	     (FIXME?) */
	  break;
	case DW_TAG_class_type:
	case DW_TAG_structure_type:
	case DW_TAG_union_type:
	case DW_TAG_enumeration_type:
	  SYMBOL_CLASS (sym) = LOC_TYPEDEF;
	  SYMBOL_DOMAIN (sym) = STRUCT_DOMAIN;

	  /* Make sure that the symbol includes appropriate enclosing
	     classes/namespaces in its name.  These are calculated in
	     read_structure_type, and the correct name is saved in
	     the type.  */

	  if (cu->language == language_cplus)
	    {
	      struct type *type = SYMBOL_TYPE (sym);
	      
	      if (TYPE_TAG_NAME (type) != NULL)
		{
		  /* FIXME: carlton/2003-11-10: Should this use
		     SYMBOL_SET_NAMES instead?  (The same problem also
		     arises a further down in the function.)  */
		  SYMBOL_LINKAGE_NAME (sym)
		    = obsavestring (TYPE_TAG_NAME (type),
				    strlen (TYPE_TAG_NAME (type)),
				    &objfile->objfile_obstack);
		}
	    }

	  {
	    /* NOTE: carlton/2003-11-10: C++ class symbols shouldn't
	       really ever be static objects: otherwise, if you try
	       to, say, break of a class's method and you're in a file
	       which doesn't mention that class, it won't work unless
	       the check for all static symbols in lookup_symbol_aux
	       saves you.  See the OtherFileClass tests in
	       gdb.c++/namespace.exp.  */

	    struct pending **list_to_add;

	    list_to_add = (cu->list_in_scope == &file_symbols
			   && cu->language == language_cplus
			   ? &global_symbols : cu->list_in_scope);
	  
	    add_symbol_to_list (sym, list_to_add);

	    /* The semantics of C++ state that "struct foo { ... }" also
	       defines a typedef for "foo". Synthesize a typedef symbol so
	       that "ptype foo" works as expected.  */
	    if (cu->language == language_cplus)
	      {
		struct symbol *typedef_sym = (struct symbol *)
		  obstack_alloc (&objfile->objfile_obstack,
				 sizeof (struct symbol));
		*typedef_sym = *sym;
		SYMBOL_DOMAIN (typedef_sym) = VAR_DOMAIN;
		if (TYPE_NAME (SYMBOL_TYPE (sym)) == 0)
		  TYPE_NAME (SYMBOL_TYPE (sym)) =
		    obsavestring (SYMBOL_NATURAL_NAME (sym),
				  strlen (SYMBOL_NATURAL_NAME (sym)),
				  &objfile->objfile_obstack);
		add_symbol_to_list (typedef_sym, list_to_add);
	      }
	  }
	  break;
	case DW_TAG_typedef:
	  if (processing_has_namespace_info
	      && processing_current_prefix[0] != '\0')
	    {
	      SYMBOL_LINKAGE_NAME (sym) = obconcat (&objfile->objfile_obstack,
						    processing_current_prefix,
						    "::",
						    name);
	    }
	  SYMBOL_CLASS (sym) = LOC_TYPEDEF;
	  SYMBOL_DOMAIN (sym) = VAR_DOMAIN;
	  add_symbol_to_list (sym, cu->list_in_scope);
	  break;
	case DW_TAG_base_type:
        case DW_TAG_subrange_type:
	  SYMBOL_CLASS (sym) = LOC_TYPEDEF;
	  SYMBOL_DOMAIN (sym) = VAR_DOMAIN;
	  add_symbol_to_list (sym, cu->list_in_scope);
	  break;
	case DW_TAG_enumerator:
	  if (processing_has_namespace_info
	      && processing_current_prefix[0] != '\0')
	    {
	      SYMBOL_LINKAGE_NAME (sym) = obconcat (&objfile->objfile_obstack,
						    processing_current_prefix,
						    "::",
						    name);
	    }
	  attr = dwarf2_attr (die, DW_AT_const_value, cu);
	  if (attr)
	    {
	      dwarf2_const_value (attr, sym, cu);
	    }
	  {
	    /* NOTE: carlton/2003-11-10: See comment above in the
	       DW_TAG_class_type, etc. block.  */

	    struct pending **list_to_add;

	    list_to_add = (cu->list_in_scope == &file_symbols
			   && cu->language == language_cplus
			   ? &global_symbols : cu->list_in_scope);
	  
	    add_symbol_to_list (sym, list_to_add);
	  }
	  break;
	case DW_TAG_namespace:
	  SYMBOL_CLASS (sym) = LOC_TYPEDEF;
	  add_symbol_to_list (sym, &global_symbols);
	  break;
	default:
	  /* Not a tag we recognize.  Hopefully we aren't processing
	     trash data, but since we must specifically ignore things
	     we don't recognize, there is nothing else we should do at
	     this point. */
	  complaint (&symfile_complaints, "unsupported tag: '%s'",
		     dwarf_tag_name (die->tag));
	  break;
	}
    }
  return (sym);
}

/* Copy constant value from an attribute to a symbol.  */

static void
dwarf2_const_value (struct attribute *attr, struct symbol *sym,
		    struct dwarf2_cu *cu)
{
  struct objfile *objfile = cu->objfile;
  struct comp_unit_head *cu_header = &cu->header;
  struct dwarf_block *blk;

  switch (attr->form)
    {
    case DW_FORM_addr:
      if (TYPE_LENGTH (SYMBOL_TYPE (sym)) != cu_header->addr_size)
	dwarf2_const_value_length_mismatch_complaint (DEPRECATED_SYMBOL_NAME (sym),
						      cu_header->addr_size,
						      TYPE_LENGTH (SYMBOL_TYPE
								   (sym)));
      SYMBOL_VALUE_BYTES (sym) = (char *)
	obstack_alloc (&objfile->objfile_obstack, cu_header->addr_size);
      /* NOTE: cagney/2003-05-09: In-lined store_address call with
         it's body - store_unsigned_integer.  */
      store_unsigned_integer (SYMBOL_VALUE_BYTES (sym), cu_header->addr_size,
			      DW_ADDR (attr));
      SYMBOL_CLASS (sym) = LOC_CONST_BYTES;
      break;
    case DW_FORM_block1:
    case DW_FORM_block2:
    case DW_FORM_block4:
    case DW_FORM_block:
      blk = DW_BLOCK (attr);
      if (TYPE_LENGTH (SYMBOL_TYPE (sym)) != blk->size)
	dwarf2_const_value_length_mismatch_complaint (DEPRECATED_SYMBOL_NAME (sym),
						      blk->size,
						      TYPE_LENGTH (SYMBOL_TYPE
								   (sym)));
      SYMBOL_VALUE_BYTES (sym) = (char *)
	obstack_alloc (&objfile->objfile_obstack, blk->size);
      memcpy (SYMBOL_VALUE_BYTES (sym), blk->data, blk->size);
      SYMBOL_CLASS (sym) = LOC_CONST_BYTES;
      break;

      /* The DW_AT_const_value attributes are supposed to carry the
	 symbol's value "represented as it would be on the target
	 architecture."  By the time we get here, it's already been
	 converted to host endianness, so we just need to sign- or
	 zero-extend it as appropriate.  */
    case DW_FORM_data1:
      dwarf2_const_value_data (attr, sym, 8);
      break;
    case DW_FORM_data2:
      dwarf2_const_value_data (attr, sym, 16);
      break;
    case DW_FORM_data4:
      dwarf2_const_value_data (attr, sym, 32);
      break;
    case DW_FORM_data8:
      dwarf2_const_value_data (attr, sym, 64);
      break;

    case DW_FORM_sdata:
      SYMBOL_VALUE (sym) = DW_SND (attr);
      SYMBOL_CLASS (sym) = LOC_CONST;
      break;

    case DW_FORM_udata:
      SYMBOL_VALUE (sym) = DW_UNSND (attr);
      SYMBOL_CLASS (sym) = LOC_CONST;
      break;

    default:
      complaint (&symfile_complaints,
		 "unsupported const value attribute form: '%s'",
		 dwarf_form_name (attr->form));
      SYMBOL_VALUE (sym) = 0;
      SYMBOL_CLASS (sym) = LOC_CONST;
      break;
    }
}


/* Given an attr with a DW_FORM_dataN value in host byte order, sign-
   or zero-extend it as appropriate for the symbol's type.  */
static void
dwarf2_const_value_data (struct attribute *attr,
			 struct symbol *sym,
			 int bits)
{
  LONGEST l = DW_UNSND (attr);

  if (bits < sizeof (l) * 8)
    {
      if (TYPE_UNSIGNED (SYMBOL_TYPE (sym)))
	l &= ((LONGEST) 1 << bits) - 1;
      else
	l = (l << (sizeof (l) * 8 - bits)) >> (sizeof (l) * 8 - bits);
    }

  SYMBOL_VALUE (sym) = l;
  SYMBOL_CLASS (sym) = LOC_CONST;
}


/* Return the type of the die in question using its DW_AT_type attribute.  */

static struct type *
die_type (struct die_info *die, struct dwarf2_cu *cu)
{
  struct type *type;
  struct attribute *type_attr;
  struct die_info *type_die;
  unsigned int ref;

  type_attr = dwarf2_attr (die, DW_AT_type, cu);
  if (!type_attr)
    {
      /* A missing DW_AT_type represents a void type.  */
      return dwarf2_fundamental_type (cu->objfile, FT_VOID, cu);
    }
  else
    {
      ref = dwarf2_get_ref_die_offset (type_attr, cu);
      type_die = follow_die_ref (ref);
      if (!type_die)
	{
	  error ("Dwarf Error: Cannot find referent at offset %d [in module %s]", 
			  ref, cu->objfile->name);
	  return NULL;
	}
    }
  type = tag_type_to_type (type_die, cu);
  if (!type)
    {
      dump_die (type_die);
      error ("Dwarf Error: Problem turning type die at offset into gdb type [in module %s]",
		      cu->objfile->name);
    }
  return type;
}

/* Return the containing type of the die in question using its
   DW_AT_containing_type attribute.  */

static struct type *
die_containing_type (struct die_info *die, struct dwarf2_cu *cu)
{
  struct type *type = NULL;
  struct attribute *type_attr;
  struct die_info *type_die = NULL;
  unsigned int ref;

  type_attr = dwarf2_attr (die, DW_AT_containing_type, cu);
  if (type_attr)
    {
      ref = dwarf2_get_ref_die_offset (type_attr, cu);
      type_die = follow_die_ref (ref);
      if (!type_die)
	{
	  error ("Dwarf Error: Cannot find referent at offset %d [in module %s]", ref, 
			  cu->objfile->name);
	  return NULL;
	}
      type = tag_type_to_type (type_die, cu);
    }
  if (!type)
    {
      if (type_die)
	dump_die (type_die);
      error ("Dwarf Error: Problem turning containing type into gdb type [in module %s]", 
		      cu->objfile->name);
    }
  return type;
}

#if 0
static struct type *
type_at_offset (unsigned int offset, struct dwarf2_cu *cu)
{
  struct die_info *die;
  struct type *type;

  die = follow_die_ref (offset);
  if (!die)
    {
      error ("Dwarf Error: Cannot find type referent at offset %d.", offset);
      return NULL;
    }
  type = tag_type_to_type (die, cu);
  return type;
}
#endif

static struct type *
tag_type_to_type (struct die_info *die, struct dwarf2_cu *cu)
{
  if (die->type)
    {
      return die->type;
    }
  else
    {
      read_type_die (die, cu);
      if (!die->type)
	{
	  dump_die (die);
	  error ("Dwarf Error: Cannot find type of die 0x%x [in module %s]", 
			  die->tag, cu->objfile->name);
	}
      return die->type;
    }
}

static void
read_type_die (struct die_info *die, struct dwarf2_cu *cu)
{
  char *prefix = determine_prefix (die, cu);
  const char *old_prefix = processing_current_prefix;
  struct cleanup *back_to = make_cleanup (xfree, prefix);
  processing_current_prefix = prefix;
  
  switch (die->tag)
    {
    case DW_TAG_class_type:
    case DW_TAG_structure_type:
    case DW_TAG_union_type:
      read_structure_type (die, cu);
      break;
    case DW_TAG_enumeration_type:
      read_enumeration_type (die, cu);
      break;
    case DW_TAG_subprogram:
    case DW_TAG_subroutine_type:
      read_subroutine_type (die, cu);
      break;
    case DW_TAG_array_type:
      read_array_type (die, cu);
      break;
    case DW_TAG_pointer_type:
      read_tag_pointer_type (die, cu);
      break;
    case DW_TAG_unspecified_type:
      read_tag_unspecified_type (die, cu);
      break;
    case DW_TAG_ptr_to_member_type:
      read_tag_ptr_to_member_type (die, cu);
      break;
    case DW_TAG_reference_type:
    case DW_TAG_rvalue_reference_type:
      read_tag_reference_type (die, cu);
      break;
    case DW_TAG_const_type:
      read_tag_const_type (die, cu);
      break;
    case DW_TAG_volatile_type:
      read_tag_volatile_type (die, cu);
      break;
    case DW_TAG_restrict_type:
      read_tag_restrict_type (die, cu);
      break;
    case DW_TAG_string_type:
      read_tag_string_type (die, cu);
      break;
    case DW_TAG_typedef:
      read_typedef (die, cu);
      break;
    case DW_TAG_subrange_type:
      read_subrange_type (die, cu);
      break;
    case DW_TAG_base_type:
      read_base_type (die, cu);
      break;
    default:
      complaint (&symfile_complaints, "unexepected tag in read_type_die: '%s'",
		 dwarf_tag_name (die->tag));
      break;
    }

  processing_current_prefix = old_prefix;
  do_cleanups (back_to);
}

/* Return the name of the namespace/class that DIE is defined within,
   or "" if we can't tell.  The caller should xfree the result.  */

/* NOTE: carlton/2004-01-23: See read_func_scope (and the comment
   therein) for an example of how to use this function to deal with
   DW_AT_specification.  */

static char *
determine_prefix (struct die_info *die, struct dwarf2_cu *cu)
{
  struct die_info *parent;

  if (cu->language != language_cplus)
    return NULL;

  parent = die->parent;

  if (parent == NULL)
    {
      return xstrdup ("");
    }
  else
    {
      switch (parent->tag) {
      case DW_TAG_namespace:
	{
	  /* FIXME: carlton/2004-03-05: Should I follow extension dies
	     before doing this check?  */
	  if (parent->type != NULL && TYPE_TAG_NAME (parent->type) != NULL)
	    {
	      return xstrdup (TYPE_TAG_NAME (parent->type));
	    }
	  else
	    {
	      int dummy;
	      char *parent_prefix = determine_prefix (parent, cu);
	      char *retval = typename_concat (parent_prefix,
					      namespace_name (parent, &dummy,
							      cu));
	      xfree (parent_prefix);
	      return retval;
	    }
	}
	break;
      case DW_TAG_class_type:
      case DW_TAG_structure_type:
	{
	  if (parent->type != NULL && TYPE_TAG_NAME (parent->type) != NULL)
	    {
	      return xstrdup (TYPE_TAG_NAME (parent->type));
	    }
	  else
	    {
	      const char *old_prefix = processing_current_prefix;
	      char *new_prefix = determine_prefix (parent, cu);
	      char *retval;

	      processing_current_prefix = new_prefix;
	      retval = determine_class_name (parent, cu);
	      processing_current_prefix = old_prefix;

	      xfree (new_prefix);
	      return retval;
	    }
	}
      default:
	return determine_prefix (parent, cu);
      }
    }
}

/* Return a newly-allocated string formed by concatenating PREFIX,
   "::", and SUFFIX, except that if PREFIX is NULL or the empty
   string, just return a copy of SUFFIX.  */

static char *
typename_concat (const char *prefix, const char *suffix)
{
  if (prefix == NULL || prefix[0] == '\0')
    return xstrdup (suffix);
  else
    {
      char *retval = xmalloc (strlen (prefix) + 2 + strlen (suffix) + 1);

      strcpy (retval, prefix);
      strcat (retval, "::");
      strcat (retval, suffix);

      return retval;
    }
}

static struct type *
dwarf_base_type (int encoding, int size, struct dwarf2_cu *cu)
{
  struct objfile *objfile = cu->objfile;

  /* FIXME - this should not produce a new (struct type *)
     every time.  It should cache base types.  */
  struct type *type;
  switch (encoding)
    {
    case DW_ATE_address:
      type = dwarf2_fundamental_type (objfile, FT_VOID, cu);
      return type;
    case DW_ATE_boolean:
      type = dwarf2_fundamental_type (objfile, FT_BOOLEAN, cu);
      return type;
    case DW_ATE_complex_float:
      if (size == 16)
	{
	  type = dwarf2_fundamental_type (objfile, FT_DBL_PREC_COMPLEX, cu);
	}
      else
	{
	  type = dwarf2_fundamental_type (objfile, FT_COMPLEX, cu);
	}
      return type;
    case DW_ATE_float:
      if (size == 8)
	{
	  type = dwarf2_fundamental_type (objfile, FT_DBL_PREC_FLOAT, cu);
	}
      else
	{
	  type = dwarf2_fundamental_type (objfile, FT_FLOAT, cu);
	}
      return type;
    case DW_ATE_signed:
      switch (size)
	{
	case 1:
	  type = dwarf2_fundamental_type (objfile, FT_SIGNED_CHAR, cu);
	  break;
	case 2:
	  type = dwarf2_fundamental_type (objfile, FT_SIGNED_SHORT, cu);
	  break;
	default:
	case 4:
	  type = dwarf2_fundamental_type (objfile, FT_SIGNED_INTEGER, cu);
	  break;
	}
      return type;
    case DW_ATE_signed_char:
      type = dwarf2_fundamental_type (objfile, FT_SIGNED_CHAR, cu);
      return type;
    case DW_ATE_unsigned:
      switch (size)
	{
	case 1:
	  type = dwarf2_fundamental_type (objfile, FT_UNSIGNED_CHAR, cu);
	  break;
	case 2:
	  type = dwarf2_fundamental_type (objfile, FT_UNSIGNED_SHORT, cu);
	  break;
	default:
	case 4:
	  type = dwarf2_fundamental_type (objfile, FT_UNSIGNED_INTEGER, cu);
	  break;
	}
      return type;
    case DW_ATE_unsigned_char:
      type = dwarf2_fundamental_type (objfile, FT_UNSIGNED_CHAR, cu);
      return type;
    default:
      type = dwarf2_fundamental_type (objfile, FT_SIGNED_INTEGER, cu);
      return type;
    }
}

#if 0
struct die_info *
copy_die (struct die_info *old_die)
{
  struct die_info *new_die;
  int i, num_attrs;

  new_die = (struct die_info *) xmalloc (sizeof (struct die_info));
  memset (new_die, 0, sizeof (struct die_info));

  new_die->tag = old_die->tag;
  new_die->has_children = old_die->has_children;
  new_die->abbrev = old_die->abbrev;
  new_die->offset = old_die->offset;
  new_die->type = NULL;

  num_attrs = old_die->num_attrs;
  new_die->num_attrs = num_attrs;
  new_die->attrs = (struct attribute *)
    xmalloc (num_attrs * sizeof (struct attribute));

  for (i = 0; i < old_die->num_attrs; ++i)
    {
      new_die->attrs[i].name = old_die->attrs[i].name;
      new_die->attrs[i].form = old_die->attrs[i].form;
      new_die->attrs[i].u.addr = old_die->attrs[i].u.addr;
    }

  new_die->next = NULL;
  return new_die;
}
#endif

/* Return sibling of die, NULL if no sibling.  */

static struct die_info *
sibling_die (struct die_info *die)
{
  return die->sibling;
}

/* Get linkage name of a die, return NULL if not found.  */

static char *
dwarf2_linkage_name (struct die_info *die, struct dwarf2_cu *cu)
{
  struct attribute *attr;

  attr = dwarf2_attr (die, DW_AT_MIPS_linkage_name, cu);
  if (attr && DW_STRING (attr))
    return DW_STRING (attr);
  attr = dwarf2_attr (die, DW_AT_name, cu);
  if (attr && DW_STRING (attr))
    return DW_STRING (attr);
  return NULL;
}

/* Get name of a die, return NULL if not found.  */

static char *
dwarf2_name (struct die_info *die, struct dwarf2_cu *cu)
{
  struct attribute *attr;

  attr = dwarf2_attr (die, DW_AT_name, cu);
  if (attr && DW_STRING (attr))
    return DW_STRING (attr);
  return NULL;
}

/* Return the die that this die in an extension of, or NULL if there
   is none.  */

static struct die_info *
dwarf2_extension (struct die_info *die, struct dwarf2_cu *cu)
{
  struct attribute *attr;
  struct die_info *extension_die;
  unsigned int ref;

  attr = dwarf2_attr (die, DW_AT_extension, cu);
  if (attr == NULL)
    return NULL;

  ref = dwarf2_get_ref_die_offset (attr, cu);
  extension_die = follow_die_ref (ref);
  if (!extension_die)
    {
      error ("Dwarf Error: Cannot find referent at offset %d.", ref);
    }

  return extension_die;
}

/* Convert a DIE tag into its string name.  */

static char *
dwarf_tag_name (unsigned tag)
{
  switch (tag)
    {
    case DW_TAG_padding:
      return "DW_TAG_padding";
    case DW_TAG_array_type:
      return "DW_TAG_array_type";
    case DW_TAG_class_type:
      return "DW_TAG_class_type";
    case DW_TAG_entry_point:
      return "DW_TAG_entry_point";
    case DW_TAG_enumeration_type:
      return "DW_TAG_enumeration_type";
    case DW_TAG_formal_parameter:
      return "DW_TAG_formal_parameter";
    case DW_TAG_imported_declaration:
      return "DW_TAG_imported_declaration";
    case DW_TAG_label:
      return "DW_TAG_label";
    case DW_TAG_lexical_block:
      return "DW_TAG_lexical_block";
    case DW_TAG_member:
      return "DW_TAG_member";
    case DW_TAG_pointer_type:
      return "DW_TAG_pointer_type";
    case DW_TAG_reference_type:
      return "DW_TAG_reference_type";
    case DW_TAG_rvalue_reference_type:
      return "DW_TAG_rvalue_reference_type";
    case DW_TAG_compile_unit:
      return "DW_TAG_compile_unit";
    case DW_TAG_string_type:
      return "DW_TAG_string_type";
    case DW_TAG_structure_type:
      return "DW_TAG_structure_type";
    case DW_TAG_subroutine_type:
      return "DW_TAG_subroutine_type";
    case DW_TAG_typedef:
      return "DW_TAG_typedef";
    case DW_TAG_union_type:
      return "DW_TAG_union_type";
    case DW_TAG_unspecified_parameters:
      return "DW_TAG_unspecified_parameters";
    case DW_TAG_variant:
      return "DW_TAG_variant";
    case DW_TAG_common_block:
      return "DW_TAG_common_block";
    case DW_TAG_common_inclusion:
      return "DW_TAG_common_inclusion";
    case DW_TAG_inheritance:
      return "DW_TAG_inheritance";
    case DW_TAG_inlined_subroutine:
      return "DW_TAG_inlined_subroutine";
    case DW_TAG_module:
      return "DW_TAG_module";
    case DW_TAG_ptr_to_member_type:
      return "DW_TAG_ptr_to_member_type";
    case DW_TAG_set_type:
      return "DW_TAG_set_type";
    case DW_TAG_subrange_type:
      return "DW_TAG_subrange_type";
    case DW_TAG_with_stmt:
      return "DW_TAG_with_stmt";
    case DW_TAG_access_declaration:
      return "DW_TAG_access_declaration";
    case DW_TAG_base_type:
      return "DW_TAG_base_type";
    case DW_TAG_catch_block:
      return "DW_TAG_catch_block";
    case DW_TAG_const_type:
      return "DW_TAG_const_type";
    case DW_TAG_constant:
      return "DW_TAG_constant";
    case DW_TAG_enumerator:
      return "DW_TAG_enumerator";
    case DW_TAG_file_type:
      return "DW_TAG_file_type";
    case DW_TAG_friend:
      return "DW_TAG_friend";
    case DW_TAG_namelist:
      return "DW_TAG_namelist";
    case DW_TAG_namelist_item:
      return "DW_TAG_namelist_item";
    case DW_TAG_packed_type:
      return "DW_TAG_packed_type";
    case DW_TAG_subprogram:
      return "DW_TAG_subprogram";
    case DW_TAG_template_type_param:
      return "DW_TAG_template_type_param";
    case DW_TAG_template_value_param:
      return "DW_TAG_template_value_param";
    case DW_TAG_thrown_type:
      return "DW_TAG_thrown_type";
    case DW_TAG_try_block:
      return "DW_TAG_try_block";
    case DW_TAG_variant_part:
      return "DW_TAG_variant_part";
    case DW_TAG_variable:
      return "DW_TAG_variable";
    case DW_TAG_volatile_type:
      return "DW_TAG_volatile_type";
    case DW_TAG_dwarf_procedure:
      return "DW_TAG_dwarf_procedure";
    case DW_TAG_restrict_type:
      return "DW_TAG_restrict_type";
    case DW_TAG_interface_type:
      return "DW_TAG_interface_type";
    case DW_TAG_namespace:
      return "DW_TAG_namespace";
    case DW_TAG_imported_module:
      return "DW_TAG_imported_module";
    case DW_TAG_unspecified_type:
      return "DW_TAG_unspecified_type";
    case DW_TAG_partial_unit:
      return "DW_TAG_partial_unit";
    case DW_TAG_imported_unit:
      return "DW_TAG_imported_unit";
    case DW_TAG_MIPS_loop:
      return "DW_TAG_MIPS_loop";
    case DW_TAG_format_label:
      return "DW_TAG_format_label";
    case DW_TAG_function_template:
      return "DW_TAG_function_template";
    case DW_TAG_class_template:
      return "DW_TAG_class_template";
    default:
      return "DW_TAG_<unknown>";
    }
}

/* Convert a DWARF attribute code into its string name.  */

static char *
dwarf_attr_name (unsigned attr)
{
  switch (attr)
    {
    case DW_AT_sibling:
      return "DW_AT_sibling";
    case DW_AT_location:
      return "DW_AT_location";
    case DW_AT_name:
      return "DW_AT_name";
    case DW_AT_ordering:
      return "DW_AT_ordering";
    case DW_AT_subscr_data:
      return "DW_AT_subscr_data";
    case DW_AT_byte_size:
      return "DW_AT_byte_size";
    case DW_AT_bit_offset:
      return "DW_AT_bit_offset";
    case DW_AT_bit_size:
      return "DW_AT_bit_size";
    case DW_AT_element_list:
      return "DW_AT_element_list";
    case DW_AT_stmt_list:
      return "DW_AT_stmt_list";
    case DW_AT_low_pc:
      return "DW_AT_low_pc";
    case DW_AT_high_pc:
      return "DW_AT_high_pc";
    case DW_AT_language:
      return "DW_AT_language";
    case DW_AT_member:
      return "DW_AT_member";
    case DW_AT_discr:
      return "DW_AT_discr";
    case DW_AT_discr_value:
      return "DW_AT_discr_value";
    case DW_AT_visibility:
      return "DW_AT_visibility";
    case DW_AT_import:
      return "DW_AT_import";
    case DW_AT_string_length:
      return "DW_AT_string_length";
    case DW_AT_common_reference:
      return "DW_AT_common_reference";
    case DW_AT_comp_dir:
      return "DW_AT_comp_dir";
    case DW_AT_const_value:
      return "DW_AT_const_value";
    case DW_AT_containing_type:
      return "DW_AT_containing_type";
    case DW_AT_default_value:
      return "DW_AT_default_value";
    case DW_AT_inline:
      return "DW_AT_inline";
    case DW_AT_is_optional:
      return "DW_AT_is_optional";
    case DW_AT_lower_bound:
      return "DW_AT_lower_bound";
    case DW_AT_producer:
      return "DW_AT_producer";
    case DW_AT_prototyped:
      return "DW_AT_prototyped";
    case DW_AT_return_addr:
      return "DW_AT_return_addr";
    case DW_AT_start_scope:
      return "DW_AT_start_scope";
    case DW_AT_stride_size:
      return "DW_AT_stride_size";
    case DW_AT_upper_bound:
      return "DW_AT_upper_bound";
    case DW_AT_abstract_origin:
      return "DW_AT_abstract_origin";
    case DW_AT_accessibility:
      return "DW_AT_accessibility";
    case DW_AT_address_class:
      return "DW_AT_address_class";
    case DW_AT_artificial:
      return "DW_AT_artificial";
    case DW_AT_base_types:
      return "DW_AT_base_types";
    case DW_AT_calling_convention:
      return "DW_AT_calling_convention";
    case DW_AT_count:
      return "DW_AT_count";
    case DW_AT_data_member_location:
      return "DW_AT_data_member_location";
    case DW_AT_decl_column:
      return "DW_AT_decl_column";
    case DW_AT_decl_file:
      return "DW_AT_decl_file";
    case DW_AT_decl_line:
      return "DW_AT_decl_line";
    case DW_AT_declaration:
      return "DW_AT_declaration";
    case DW_AT_discr_list:
      return "DW_AT_discr_list";
    case DW_AT_encoding:
      return "DW_AT_encoding";
    case DW_AT_external:
      return "DW_AT_external";
    case DW_AT_frame_base:
      return "DW_AT_frame_base";
    case DW_AT_friend:
      return "DW_AT_friend";
    case DW_AT_identifier_case:
      return "DW_AT_identifier_case";
    case DW_AT_macro_info:
      return "DW_AT_macro_info";
    case DW_AT_namelist_items:
      return "DW_AT_namelist_items";
    case DW_AT_priority:
      return "DW_AT_priority";
    case DW_AT_segment:
      return "DW_AT_segment";
    case DW_AT_specification:
      return "DW_AT_specification";
    case DW_AT_static_link:
      return "DW_AT_static_link";
    case DW_AT_type:
      return "DW_AT_type";
    case DW_AT_use_location:
      return "DW_AT_use_location";
    case DW_AT_variable_parameter:
      return "DW_AT_variable_parameter";
    case DW_AT_virtuality:
      return "DW_AT_virtuality";
    case DW_AT_vtable_elem_location:
      return "DW_AT_vtable_elem_location";
    case DW_AT_allocated:
      return "DW_AT_allocated";
    case DW_AT_associated:
      return "DW_AT_associated";
    case DW_AT_data_location:
      return "DW_AT_data_location";
    case DW_AT_stride:
      return "DW_AT_stride";
    case DW_AT_entry_pc:
      return "DW_AT_entry_pc";
    case DW_AT_use_UTF8:
      return "DW_AT_use_UTF8";
    case DW_AT_extension:
      return "DW_AT_extension";
    case DW_AT_ranges:
      return "DW_AT_ranges";
    case DW_AT_trampoline:
      return "DW_AT_trampoline";
    case DW_AT_call_column:
      return "DW_AT_call_column";
    case DW_AT_call_file:
      return "DW_AT_call_file";
    case DW_AT_call_line:
      return "DW_AT_call_line";
#ifdef MIPS
    case DW_AT_MIPS_fde:
      return "DW_AT_MIPS_fde";
    case DW_AT_MIPS_loop_begin:
      return "DW_AT_MIPS_loop_begin";
    case DW_AT_MIPS_tail_loop_begin:
      return "DW_AT_MIPS_tail_loop_begin";
    case DW_AT_MIPS_epilog_begin:
      return "DW_AT_MIPS_epilog_begin";
    case DW_AT_MIPS_loop_unroll_factor:
      return "DW_AT_MIPS_loop_unroll_factor";
    case DW_AT_MIPS_software_pipeline_depth:
      return "DW_AT_MIPS_software_pipeline_depth";
#endif
    case DW_AT_MIPS_linkage_name:
      return "DW_AT_MIPS_linkage_name";

    case DW_AT_sf_names:
      return "DW_AT_sf_names";
    case DW_AT_src_info:
      return "DW_AT_src_info";
    case DW_AT_mac_info:
      return "DW_AT_mac_info";
    case DW_AT_src_coords:
      return "DW_AT_src_coords";
    case DW_AT_body_begin:
      return "DW_AT_body_begin";
    case DW_AT_body_end:
      return "DW_AT_body_end";
    case DW_AT_GNU_vector:
      return "DW_AT_GNU_vector";
    default:
      return "DW_AT_<unknown>";
    }
}

/* Convert a DWARF value form code into its string name.  */

static char *
dwarf_form_name (unsigned form)
{
  switch (form)
    {
    case DW_FORM_addr:
      return "DW_FORM_addr";
    case DW_FORM_block2:
      return "DW_FORM_block2";
    case DW_FORM_block4:
      return "DW_FORM_block4";
    case DW_FORM_data2:
      return "DW_FORM_data2";
    case DW_FORM_data4:
      return "DW_FORM_data4";
    case DW_FORM_data8:
      return "DW_FORM_data8";
    case DW_FORM_string:
      return "DW_FORM_string";
    case DW_FORM_block:
      return "DW_FORM_block";
    case DW_FORM_block1:
      return "DW_FORM_block1";
    case DW_FORM_data1:
      return "DW_FORM_data1";
    case DW_FORM_flag:
      return "DW_FORM_flag";
    case DW_FORM_sdata:
      return "DW_FORM_sdata";
    case DW_FORM_strp:
      return "DW_FORM_strp";
    case DW_FORM_udata:
      return "DW_FORM_udata";
    case DW_FORM_ref_addr:
      return "DW_FORM_ref_addr";
    case DW_FORM_ref1:
      return "DW_FORM_ref1";
    case DW_FORM_ref2:
      return "DW_FORM_ref2";
    case DW_FORM_ref4:
      return "DW_FORM_ref4";
    case DW_FORM_ref8:
      return "DW_FORM_ref8";
    case DW_FORM_ref_udata:
      return "DW_FORM_ref_udata";
    case DW_FORM_indirect:
      return "DW_FORM_indirect";
    default:
      return "DW_FORM_<unknown>";
    }
}

/* Convert a DWARF stack opcode into its string name.  */

static char *
dwarf_stack_op_name (unsigned op)
{
  switch (op)
    {
    case DW_OP_addr:
      return "DW_OP_addr";
    case DW_OP_deref:
      return "DW_OP_deref";
    case DW_OP_const1u:
      return "DW_OP_const1u";
    case DW_OP_const1s:
      return "DW_OP_const1s";
    case DW_OP_const2u:
      return "DW_OP_const2u";
    case DW_OP_const2s:
      return "DW_OP_const2s";
    case DW_OP_const4u:
      return "DW_OP_const4u";
    case DW_OP_const4s:
      return "DW_OP_const4s";
    case DW_OP_const8u:
      return "DW_OP_const8u";
    case DW_OP_const8s:
      return "DW_OP_const8s";
    case DW_OP_constu:
      return "DW_OP_constu";
    case DW_OP_consts:
      return "DW_OP_consts";
    case DW_OP_dup:
      return "DW_OP_dup";
    case DW_OP_drop:
      return "DW_OP_drop";
    case DW_OP_over:
      return "DW_OP_over";
    case DW_OP_pick:
      return "DW_OP_pick";
    case DW_OP_swap:
      return "DW_OP_swap";
    case DW_OP_rot:
      return "DW_OP_rot";
    case DW_OP_xderef:
      return "DW_OP_xderef";
    case DW_OP_abs:
      return "DW_OP_abs";
    case DW_OP_and:
      return "DW_OP_and";
    case DW_OP_div:
      return "DW_OP_div";
    case DW_OP_minus:
      return "DW_OP_minus";
    case DW_OP_mod:
      return "DW_OP_mod";
    case DW_OP_mul:
      return "DW_OP_mul";
    case DW_OP_neg:
      return "DW_OP_neg";
    case DW_OP_not:
      return "DW_OP_not";
    case DW_OP_or:
      return "DW_OP_or";
    case DW_OP_plus:
      return "DW_OP_plus";
    case DW_OP_plus_uconst:
      return "DW_OP_plus_uconst";
    case DW_OP_shl:
      return "DW_OP_shl";
    case DW_OP_shr:
      return "DW_OP_shr";
    case DW_OP_shra:
      return "DW_OP_shra";
    case DW_OP_xor:
      return "DW_OP_xor";
    case DW_OP_bra:
      return "DW_OP_bra";
    case DW_OP_eq:
      return "DW_OP_eq";
    case DW_OP_ge:
      return "DW_OP_ge";
    case DW_OP_gt:
      return "DW_OP_gt";
    case DW_OP_le:
      return "DW_OP_le";
    case DW_OP_lt:
      return "DW_OP_lt";
    case DW_OP_ne:
      return "DW_OP_ne";
    case DW_OP_skip:
      return "DW_OP_skip";
    case DW_OP_lit0:
      return "DW_OP_lit0";
    case DW_OP_lit1:
      return "DW_OP_lit1";
    case DW_OP_lit2:
      return "DW_OP_lit2";
    case DW_OP_lit3:
      return "DW_OP_lit3";
    case DW_OP_lit4:
      return "DW_OP_lit4";
    case DW_OP_lit5:
      return "DW_OP_lit5";
    case DW_OP_lit6:
      return "DW_OP_lit6";
    case DW_OP_lit7:
      return "DW_OP_lit7";
    case DW_OP_lit8:
      return "DW_OP_lit8";
    case DW_OP_lit9:
      return "DW_OP_lit9";
    case DW_OP_lit10:
      return "DW_OP_lit10";
    case DW_OP_lit11:
      return "DW_OP_lit11";
    case DW_OP_lit12:
      return "DW_OP_lit12";
    case DW_OP_lit13:
      return "DW_OP_lit13";
    case DW_OP_lit14:
      return "DW_OP_lit14";
    case DW_OP_lit15:
      return "DW_OP_lit15";
    case DW_OP_lit16:
      return "DW_OP_lit16";
    case DW_OP_lit17:
      return "DW_OP_lit17";
    case DW_OP_lit18:
      return "DW_OP_lit18";
    case DW_OP_lit19:
      return "DW_OP_lit19";
    case DW_OP_lit20:
      return "DW_OP_lit20";
    case DW_OP_lit21:
      return "DW_OP_lit21";
    case DW_OP_lit22:
      return "DW_OP_lit22";
    case DW_OP_lit23:
      return "DW_OP_lit23";
    case DW_OP_lit24:
      return "DW_OP_lit24";
    case DW_OP_lit25:
      return "DW_OP_lit25";
    case DW_OP_lit26:
      return "DW_OP_lit26";
    case DW_OP_lit27:
      return "DW_OP_lit27";
    case DW_OP_lit28:
      return "DW_OP_lit28";
    case DW_OP_lit29:
      return "DW_OP_lit29";
    case DW_OP_lit30:
      return "DW_OP_lit30";
    case DW_OP_lit31:
      return "DW_OP_lit31";
    case DW_OP_reg0:
      return "DW_OP_reg0";
    case DW_OP_reg1:
      return "DW_OP_reg1";
    case DW_OP_reg2:
      return "DW_OP_reg2";
    case DW_OP_reg3:
      return "DW_OP_reg3";
    case DW_OP_reg4:
      return "DW_OP_reg4";
    case DW_OP_reg5:
      return "DW_OP_reg5";
    case DW_OP_reg6:
      return "DW_OP_reg6";
    case DW_OP_reg7:
      return "DW_OP_reg7";
    case DW_OP_reg8:
      return "DW_OP_reg8";
    case DW_OP_reg9:
      return "DW_OP_reg9";
    case DW_OP_reg10:
      return "DW_OP_reg10";
    case DW_OP_reg11:
      return "DW_OP_reg11";
    case DW_OP_reg12:
      return "DW_OP_reg12";
    case DW_OP_reg13:
      return "DW_OP_reg13";
    case DW_OP_reg14:
      return "DW_OP_reg14";
    case DW_OP_reg15:
      return "DW_OP_reg15";
    case DW_OP_reg16:
      return "DW_OP_reg16";
    case DW_OP_reg17:
      return "DW_OP_reg17";
    case DW_OP_reg18:
      return "DW_OP_reg18";
    case DW_OP_reg19:
      return "DW_OP_reg19";
    case DW_OP_reg20:
      return "DW_OP_reg20";
    case DW_OP_reg21:
      return "DW_OP_reg21";
    case DW_OP_reg22:
      return "DW_OP_reg22";
    case DW_OP_reg23:
      return "DW_OP_reg23";
    case DW_OP_reg24:
      return "DW_OP_reg24";
    case DW_OP_reg25:
      return "DW_OP_reg25";
    case DW_OP_reg26:
      return "DW_OP_reg26";
    case DW_OP_reg27:
      return "DW_OP_reg27";
    case DW_OP_reg28:
      return "DW_OP_reg28";
    case DW_OP_reg29:
      return "DW_OP_reg29";
    case DW_OP_reg30:
      return "DW_OP_reg30";
    case DW_OP_reg31:
      return "DW_OP_reg31";
    case DW_OP_breg0:
      return "DW_OP_breg0";
    case DW_OP_breg1:
      return "DW_OP_breg1";
    case DW_OP_breg2:
      return "DW_OP_breg2";
    case DW_OP_breg3:
      return "DW_OP_breg3";
    case DW_OP_breg4:
      return "DW_OP_breg4";
    case DW_OP_breg5:
      return "DW_OP_breg5";
    case DW_OP_breg6:
      return "DW_OP_breg6";
    case DW_OP_breg7:
      return "DW_OP_breg7";
    case DW_OP_breg8:
      return "DW_OP_breg8";
    case DW_OP_breg9:
      return "DW_OP_breg9";
    case DW_OP_breg10:
      return "DW_OP_breg10";
    case DW_OP_breg11:
      return "DW_OP_breg11";
    case DW_OP_breg12:
      return "DW_OP_breg12";
    case DW_OP_breg13:
      return "DW_OP_breg13";
    case DW_OP_breg14:
      return "DW_OP_breg14";
    case DW_OP_breg15:
      return "DW_OP_breg15";
    case DW_OP_breg16:
      return "DW_OP_breg16";
    case DW_OP_breg17:
      return "DW_OP_breg17";
    case DW_OP_breg18:
      return "DW_OP_breg18";
    case DW_OP_breg19:
      return "DW_OP_breg19";
    case DW_OP_breg20:
      return "DW_OP_breg20";
    case DW_OP_breg21:
      return "DW_OP_breg21";
    case DW_OP_breg22:
      return "DW_OP_breg22";
    case DW_OP_breg23:
      return "DW_OP_breg23";
    case DW_OP_breg24:
      return "DW_OP_breg24";
    case DW_OP_breg25:
      return "DW_OP_breg25";
    case DW_OP_breg26:
      return "DW_OP_breg26";
    case DW_OP_breg27:
      return "DW_OP_breg27";
    case DW_OP_breg28:
      return "DW_OP_breg28";
    case DW_OP_breg29:
      return "DW_OP_breg29";
    case DW_OP_breg30:
      return "DW_OP_breg30";
    case DW_OP_breg31:
      return "DW_OP_breg31";
    case DW_OP_regx:
      return "DW_OP_regx";
    case DW_OP_fbreg:
      return "DW_OP_fbreg";
    case DW_OP_bregx:
      return "DW_OP_bregx";
    case DW_OP_piece:
      return "DW_OP_piece";
    case DW_OP_deref_size:
      return "DW_OP_deref_size";
    case DW_OP_xderef_size:
      return "DW_OP_xderef_size";
    case DW_OP_nop:
      return "DW_OP_nop";
      /* DWARF 3 extensions.  */
    case DW_OP_push_object_address:
      return "DW_OP_push_object_address";
    case DW_OP_call2:
      return "DW_OP_call2";
    case DW_OP_call4:
      return "DW_OP_call4";
    case DW_OP_call_ref:
      return "DW_OP_call_ref";
      /* GNU extensions.  */
    case DW_OP_GNU_push_tls_address:
      return "DW_OP_GNU_push_tls_address";
    case DW_OP_GNU_uninit:
      return "DW_OP_GNU_uninit";
    default:
      return "OP_<unknown>";
    }
}

static char *
dwarf_bool_name (unsigned mybool)
{
  if (mybool)
    return "TRUE";
  else
    return "FALSE";
}

/* Convert a DWARF type code into its string name.  */

static char *
dwarf_type_encoding_name (unsigned enc)
{
  switch (enc)
    {
    case DW_ATE_address:
      return "DW_ATE_address";
    case DW_ATE_boolean:
      return "DW_ATE_boolean";
    case DW_ATE_complex_float:
      return "DW_ATE_complex_float";
    case DW_ATE_float:
      return "DW_ATE_float";
    case DW_ATE_signed:
      return "DW_ATE_signed";
    case DW_ATE_signed_char:
      return "DW_ATE_signed_char";
    case DW_ATE_unsigned:
      return "DW_ATE_unsigned";
    case DW_ATE_unsigned_char:
      return "DW_ATE_unsigned_char";
    case DW_ATE_imaginary_float:
      return "DW_ATE_imaginary_float";
    default:
      return "DW_ATE_<unknown>";
    }
}

/* Convert a DWARF call frame info operation to its string name. */

#if 0
static char *
dwarf_cfi_name (unsigned cfi_opc)
{
  switch (cfi_opc)
    {
    case DW_CFA_advance_loc:
      return "DW_CFA_advance_loc";
    case DW_CFA_offset:
      return "DW_CFA_offset";
    case DW_CFA_restore:
      return "DW_CFA_restore";
    case DW_CFA_nop:
      return "DW_CFA_nop";
    case DW_CFA_set_loc:
      return "DW_CFA_set_loc";
    case DW_CFA_advance_loc1:
      return "DW_CFA_advance_loc1";
    case DW_CFA_advance_loc2:
      return "DW_CFA_advance_loc2";
    case DW_CFA_advance_loc4:
      return "DW_CFA_advance_loc4";
    case DW_CFA_offset_extended:
      return "DW_CFA_offset_extended";
    case DW_CFA_restore_extended:
      return "DW_CFA_restore_extended";
    case DW_CFA_undefined:
      return "DW_CFA_undefined";
    case DW_CFA_same_value:
      return "DW_CFA_same_value";
    case DW_CFA_register:
      return "DW_CFA_register";
    case DW_CFA_remember_state:
      return "DW_CFA_remember_state";
    case DW_CFA_restore_state:
      return "DW_CFA_restore_state";
    case DW_CFA_def_cfa:
      return "DW_CFA_def_cfa";
    case DW_CFA_def_cfa_register:
      return "DW_CFA_def_cfa_register";
    case DW_CFA_def_cfa_offset:
      return "DW_CFA_def_cfa_offset";

    /* DWARF 3 */
    case DW_CFA_def_cfa_expression:
      return "DW_CFA_def_cfa_expression";
    case DW_CFA_expression:
      return "DW_CFA_expression";
    case DW_CFA_offset_extended_sf:
      return "DW_CFA_offset_extended_sf";
    case DW_CFA_def_cfa_sf:
      return "DW_CFA_def_cfa_sf";
    case DW_CFA_def_cfa_offset_sf:
      return "DW_CFA_def_cfa_offset_sf";

      /* SGI/MIPS specific */
    case DW_CFA_MIPS_advance_loc8:
      return "DW_CFA_MIPS_advance_loc8";

    /* GNU extensions */
    case DW_CFA_GNU_window_save:
      return "DW_CFA_GNU_window_save";
    case DW_CFA_GNU_args_size:
      return "DW_CFA_GNU_args_size";
    case DW_CFA_GNU_negative_offset_extended:
      return "DW_CFA_GNU_negative_offset_extended";

    default:
      return "DW_CFA_<unknown>";
    }
}
#endif

static void
dump_die (struct die_info *die)
{
  unsigned int i;

  fprintf_unfiltered (gdb_stderr, "Die: %s (abbrev = %d, offset = %d)\n",
	   dwarf_tag_name (die->tag), die->abbrev, die->offset);
  fprintf_unfiltered (gdb_stderr, "\thas children: %s\n",
	   dwarf_bool_name (die->child != NULL));

  fprintf_unfiltered (gdb_stderr, "\tattributes:\n");
  for (i = 0; i < die->num_attrs; ++i)
    {
      fprintf_unfiltered (gdb_stderr, "\t\t%s (%s) ",
	       dwarf_attr_name (die->attrs[i].name),
	       dwarf_form_name (die->attrs[i].form));
      switch (die->attrs[i].form)
	{
	case DW_FORM_ref_addr:
	case DW_FORM_addr:
	  fprintf_unfiltered (gdb_stderr, "address: ");
	  print_address_numeric (DW_ADDR (&die->attrs[i]), 1, gdb_stderr);
	  break;
	case DW_FORM_block2:
	case DW_FORM_block4:
	case DW_FORM_block:
	case DW_FORM_block1:
	  fprintf_unfiltered (gdb_stderr, "block: size %d", DW_BLOCK (&die->attrs[i])->size);
	  break;
	case DW_FORM_data1:
	case DW_FORM_data2:
	case DW_FORM_data4:
	case DW_FORM_data8:
	case DW_FORM_ref1:
	case DW_FORM_ref2:
	case DW_FORM_ref4:
	case DW_FORM_udata:
	case DW_FORM_sdata:
	  fprintf_unfiltered (gdb_stderr, "constant: %ld", DW_UNSND (&die->attrs[i]));
	  break;
	case DW_FORM_string:
	case DW_FORM_strp:
	  fprintf_unfiltered (gdb_stderr, "string: \"%s\"",
		   DW_STRING (&die->attrs[i])
		   ? DW_STRING (&die->attrs[i]) : "");
	  break;
	case DW_FORM_flag:
	  if (DW_UNSND (&die->attrs[i]))
	    fprintf_unfiltered (gdb_stderr, "flag: TRUE");
	  else
	    fprintf_unfiltered (gdb_stderr, "flag: FALSE");
	  break;
	case DW_FORM_flag_present:
	  fprintf_unfiltered (gdb_stderr, "flag: TRUE");
	  break;
	case DW_FORM_indirect:
	  /* the reader will have reduced the indirect form to
	     the "base form" so this form should not occur */
	  fprintf_unfiltered (gdb_stderr, "unexpected attribute form: DW_FORM_indirect");
	  break;
	default:
	  fprintf_unfiltered (gdb_stderr, "unsupported attribute form: %d.",
		   die->attrs[i].form);
	}
      fprintf_unfiltered (gdb_stderr, "\n");
    }
}

static void
dump_die_list (struct die_info *die)
{
  while (die)
    {
      dump_die (die);
      if (die->child != NULL)
	dump_die_list (die->child);
      if (die->sibling != NULL)
	dump_die_list (die->sibling);
    }
}

static void
store_in_ref_table (unsigned int offset, struct die_info *die)
{
  int h;
  struct die_info *old;

  h = (offset % REF_HASH_SIZE);
  old = die_ref_table[h];
  die->next_ref = old;
  die_ref_table[h] = die;
}


static void
dwarf2_empty_hash_tables (void)
{
  memset (die_ref_table, 0, sizeof (die_ref_table));
}

static unsigned int
dwarf2_get_ref_die_offset (struct attribute *attr, struct dwarf2_cu *cu)
{
  unsigned int result = 0;

  switch (attr->form)
    {
    case DW_FORM_ref_addr:
      result = DW_ADDR (attr);
      break;
    case DW_FORM_ref1:
    case DW_FORM_ref2:
    case DW_FORM_ref4:
    case DW_FORM_ref8:
    case DW_FORM_ref_udata:
      result = cu->header.offset + DW_UNSND (attr);
      break;
    default:
      complaint (&symfile_complaints,
		 "unsupported die ref attribute form: '%s'",
		 dwarf_form_name (attr->form));
    }
  return result;
}

/* Return the constant value held by the given attribute.  Return -1
   if the value held by the attribute is not constant.  */

static int
dwarf2_get_attr_constant_value (struct attribute *attr, int default_value)
{
  if (attr->form == DW_FORM_sdata)
    return DW_SND (attr);
  else if (attr->form == DW_FORM_udata
           || attr->form == DW_FORM_data1
           || attr->form == DW_FORM_data2
           || attr->form == DW_FORM_data4
           || attr->form == DW_FORM_data8)
    return DW_UNSND (attr);
  else
    {
      complaint (&symfile_complaints, "Attribute value is not a constant (%s)",
                 dwarf_form_name (attr->form));
      return default_value;
    }
}

static struct die_info *
follow_die_ref (unsigned int offset)
{
  struct die_info *die;
  int h;

  h = (offset % REF_HASH_SIZE);
  die = die_ref_table[h];
  while (die)
    {
      if (die->offset == offset)
	{
	  return die;
	}
      die = die->next_ref;
    }
  return NULL;
}

static struct type *
dwarf2_fundamental_type (struct objfile *objfile, int typeid,
			 struct dwarf2_cu *cu)
{
  if (typeid < 0 || typeid >= FT_NUM_MEMBERS)
    {
      error ("Dwarf Error: internal error - invalid fundamental type id %d [in module %s]",
	     typeid, objfile->name);
    }

  /* Look for this particular type in the fundamental type vector.  If
     one is not found, create and install one appropriate for the
     current language and the current target machine. */

  if (cu->ftypes[typeid] == NULL)
    {
      cu->ftypes[typeid] = cu->language_defn->la_fund_type (objfile, typeid);
    }

  return (cu->ftypes[typeid]);
}

/* Decode simple location descriptions.
   Given a pointer to a dwarf block that defines a location, compute
   the location and return the value.

   NOTE drow/2003-11-18: This function is called in two situations
   now: for the address of static or global variables (partial symbols
   only) and for offsets into structures which are expected to be
   (more or less) constant.  The partial symbol case should go away,
   and only the constant case should remain.  That will let this
   function complain more accurately.  A few special modes are allowed
   without complaint for global variables (for instance, global
   register values and thread-local values).

   A location description containing no operations indicates that the
   object is optimized out.  The return value is 0 for that case.
   FIXME drow/2003-11-16: No callers check for this case any more; soon all
   callers will only want a very basic result and this can become a
   complaint.

   When the result is a register number, the global isreg flag is set,
   otherwise it is cleared.

   Note that stack[0] is unused except as a default error return.
   Note that stack overflow is not yet handled.  */

static CORE_ADDR
decode_locdesc (struct dwarf_block *blk, struct dwarf2_cu *cu)
{
  struct objfile *objfile = cu->objfile;
  struct comp_unit_head *cu_header = &cu->header;
  int i;
  int size = blk->size;
  char *data = blk->data;
  CORE_ADDR stack[64];
  int stacki;
  unsigned int bytes_read, unsnd;
  unsigned char op;

  i = 0;
  stacki = 0;
  stack[stacki] = 0;
  isreg = 0;

  while (i < size)
    {
      op = data[i++];
      switch (op)
	{
	case DW_OP_lit0:
	case DW_OP_lit1:
	case DW_OP_lit2:
	case DW_OP_lit3:
	case DW_OP_lit4:
	case DW_OP_lit5:
	case DW_OP_lit6:
	case DW_OP_lit7:
	case DW_OP_lit8:
	case DW_OP_lit9:
	case DW_OP_lit10:
	case DW_OP_lit11:
	case DW_OP_lit12:
	case DW_OP_lit13:
	case DW_OP_lit14:
	case DW_OP_lit15:
	case DW_OP_lit16:
	case DW_OP_lit17:
	case DW_OP_lit18:
	case DW_OP_lit19:
	case DW_OP_lit20:
	case DW_OP_lit21:
	case DW_OP_lit22:
	case DW_OP_lit23:
	case DW_OP_lit24:
	case DW_OP_lit25:
	case DW_OP_lit26:
	case DW_OP_lit27:
	case DW_OP_lit28:
	case DW_OP_lit29:
	case DW_OP_lit30:
	case DW_OP_lit31:
	  stack[++stacki] = op - DW_OP_lit0;
	  break;

	case DW_OP_reg0:
	case DW_OP_reg1:
	case DW_OP_reg2:
	case DW_OP_reg3:
	case DW_OP_reg4:
	case DW_OP_reg5:
	case DW_OP_reg6:
	case DW_OP_reg7:
	case DW_OP_reg8:
	case DW_OP_reg9:
	case DW_OP_reg10:
	case DW_OP_reg11:
	case DW_OP_reg12:
	case DW_OP_reg13:
	case DW_OP_reg14:
	case DW_OP_reg15:
	case DW_OP_reg16:
	case DW_OP_reg17:
	case DW_OP_reg18:
	case DW_OP_reg19:
	case DW_OP_reg20:
	case DW_OP_reg21:
	case DW_OP_reg22:
	case DW_OP_reg23:
	case DW_OP_reg24:
	case DW_OP_reg25:
	case DW_OP_reg26:
	case DW_OP_reg27:
	case DW_OP_reg28:
	case DW_OP_reg29:
	case DW_OP_reg30:
	case DW_OP_reg31:
	  isreg = 1;
	  stack[++stacki] = op - DW_OP_reg0;
	  if (i < size)
	    dwarf2_complex_location_expr_complaint ();
	  break;

	case DW_OP_regx:
	  isreg = 1;
	  unsnd = read_unsigned_leb128 (NULL, (data + i), &bytes_read);
	  i += bytes_read;
	  stack[++stacki] = unsnd;
	  if (i < size)
	    dwarf2_complex_location_expr_complaint ();
	  break;

	case DW_OP_addr:
	  stack[++stacki] = read_address (objfile->obfd, &data[i],
					  cu, &bytes_read);
	  i += bytes_read;
	  break;

	case DW_OP_const1u:
	  stack[++stacki] = read_1_byte (objfile->obfd, &data[i]);
	  i += 1;
	  break;

	case DW_OP_const1s:
	  stack[++stacki] = read_1_signed_byte (objfile->obfd, &data[i]);
	  i += 1;
	  break;

	case DW_OP_const2u:
	  stack[++stacki] = read_2_bytes (objfile->obfd, &data[i]);
	  i += 2;
	  break;

	case DW_OP_const2s:
	  stack[++stacki] = read_2_signed_bytes (objfile->obfd, &data[i]);
	  i += 2;
	  break;

	case DW_OP_const4u:
	  stack[++stacki] = read_4_bytes (objfile->obfd, &data[i]);
	  i += 4;
	  break;

	case DW_OP_const4s:
	  stack[++stacki] = read_4_signed_bytes (objfile->obfd, &data[i]);
	  i += 4;
	  break;

	case DW_OP_constu:
	  stack[++stacki] = read_unsigned_leb128 (NULL, (data + i),
						  &bytes_read);
	  i += bytes_read;
	  break;

	case DW_OP_consts:
	  stack[++stacki] = read_signed_leb128 (NULL, (data + i), &bytes_read);
	  i += bytes_read;
	  break;

	case DW_OP_dup:
	  stack[stacki + 1] = stack[stacki];
	  stacki++;
	  break;

	case DW_OP_plus:
	  stack[stacki - 1] += stack[stacki];
	  stacki--;
	  break;

	case DW_OP_plus_uconst:
	  stack[stacki] += read_unsigned_leb128 (NULL, (data + i), &bytes_read);
	  i += bytes_read;
	  break;

	case DW_OP_minus:
	  stack[stacki - 1] -= stack[stacki];
	  stacki--;
	  break;

	case DW_OP_deref:
	  /* If we're not the last op, then we definitely can't encode
	     this using GDB's address_class enum.  This is valid for partial
	     global symbols, although the variable's address will be bogus
	     in the psymtab.  */
	  if (i < size)
	    dwarf2_complex_location_expr_complaint ();
	  break;

        case DW_OP_GNU_push_tls_address:
	  /* The top of the stack has the offset from the beginning
	     of the thread control block at which the variable is located.  */
	  /* Nothing should follow this operator, so the top of stack would
	     be returned.  */
	  /* This is valid for partial global symbols, but the variable's
	     address will be bogus in the psymtab.  */
	  if (i < size)
	    dwarf2_complex_location_expr_complaint ();
          break;

	case DW_OP_GNU_uninit:
	  break;

	default:
	  complaint (&symfile_complaints, "unsupported stack op: '%s'",
		     dwarf_stack_op_name (op));
	  return (stack[stacki]);
	}
    }
  return (stack[stacki]);
}

/* memory allocation interface */

static void
dwarf2_free_tmp_obstack (void *ignore)
{
  obstack_free (&dwarf2_tmp_obstack, NULL);
}

static struct dwarf_block *
dwarf_alloc_block (void)
{
  struct dwarf_block *blk;

  blk = (struct dwarf_block *)
    obstack_alloc (&dwarf2_tmp_obstack, sizeof (struct dwarf_block));
  return (blk);
}

static struct abbrev_info *
dwarf_alloc_abbrev (void)
{
  struct abbrev_info *abbrev;

  abbrev = (struct abbrev_info *) xmalloc (sizeof (struct abbrev_info));
  memset (abbrev, 0, sizeof (struct abbrev_info));
  return (abbrev);
}

static struct die_info *
dwarf_alloc_die (void)
{
  struct die_info *die;

  die = (struct die_info *) xmalloc (sizeof (struct die_info));
  memset (die, 0, sizeof (struct die_info));
  return (die);
}


/* Macro support.  */


/* Return the full name of file number I in *LH's file name table.
   Use COMP_DIR as the name of the current directory of the
   compilation.  The result is allocated using xmalloc; the caller is
   responsible for freeing it.  */
static char *
file_full_name (int file, struct line_header *lh, const char *comp_dir)
{
  struct file_entry *fe = &lh->file_names[file - 1];
  
  if (IS_ABSOLUTE_PATH (fe->name))
    return xstrdup (fe->name);
  else
    {
      const char *dir;
      int dir_len;
      char *full_name;

      if (fe->dir_index)
        dir = lh->include_dirs[fe->dir_index - 1];
      else
        dir = comp_dir;

      if (dir)
        {
          dir_len = strlen (dir);
          full_name = xmalloc (dir_len + 1 + strlen (fe->name) + 1);
          strcpy (full_name, dir);
          full_name[dir_len] = '/';
          strcpy (full_name + dir_len + 1, fe->name);
          return full_name;
        }
      else
        return xstrdup (fe->name);
    }
}


static struct macro_source_file *
macro_start_file (int file, int line,
                  struct macro_source_file *current_file,
                  const char *comp_dir,
                  struct line_header *lh, struct objfile *objfile)
{
  /* The full name of this source file.  */
  char *full_name = file_full_name (file, lh, comp_dir);

  /* We don't create a macro table for this compilation unit
     at all until we actually get a filename.  */
  if (! pending_macros)
    pending_macros = new_macro_table (&objfile->objfile_obstack,
                                      objfile->macro_cache);

  if (! current_file)
    /* If we have no current file, then this must be the start_file
       directive for the compilation unit's main source file.  */
    current_file = macro_set_main (pending_macros, full_name);
  else
    current_file = macro_include (current_file, line, full_name);

  xfree (full_name);
              
  return current_file;
}


/* Copy the LEN characters at BUF to a xmalloc'ed block of memory,
   followed by a null byte.  */
static char *
copy_string (const char *buf, int len)
{
  char *s = xmalloc (len + 1);
  memcpy (s, buf, len);
  s[len] = '\0';

  return s;
}


static const char *
consume_improper_spaces (const char *p, const char *body)
{
  if (*p == ' ')
    {
      complaint (&symfile_complaints,
		 "macro definition contains spaces in formal argument list:\n`%s'",
		 body);

      while (*p == ' ')
        p++;
    }

  return p;
}


static void
parse_macro_definition (struct macro_source_file *file, int line,
                        const char *body)
{
  const char *p;

  /* The body string takes one of two forms.  For object-like macro
     definitions, it should be:

        <macro name> " " <definition>

     For function-like macro definitions, it should be:

        <macro name> "() " <definition>
     or
        <macro name> "(" <arg name> ( "," <arg name> ) * ") " <definition>

     Spaces may appear only where explicitly indicated, and in the
     <definition>.

     The Dwarf 2 spec says that an object-like macro's name is always
     followed by a space, but versions of GCC around March 2002 omit
     the space when the macro's definition is the empty string. 

     The Dwarf 2 spec says that there should be no spaces between the
     formal arguments in a function-like macro's formal argument list,
     but versions of GCC around March 2002 include spaces after the
     commas.  */


  /* Find the extent of the macro name.  The macro name is terminated
     by either a space or null character (for an object-like macro) or
     an opening paren (for a function-like macro).  */
  for (p = body; *p; p++)
    if (*p == ' ' || *p == '(')
      break;

  if (*p == ' ' || *p == '\0')
    {
      /* It's an object-like macro.  */
      int name_len = p - body;
      char *name = copy_string (body, name_len);
      const char *replacement;

      if (*p == ' ')
        replacement = body + name_len + 1;
      else
        {
	  dwarf2_macro_malformed_definition_complaint (body);
          replacement = body + name_len;
        }
      
      macro_define_object (file, line, name, replacement);

      xfree (name);
    }
  else if (*p == '(')
    {
      /* It's a function-like macro.  */
      char *name = copy_string (body, p - body);
      int argc = 0;
      int argv_size = 1;
      char **argv = xmalloc (argv_size * sizeof (*argv));

      p++;

      p = consume_improper_spaces (p, body);

      /* Parse the formal argument list.  */
      while (*p && *p != ')')
        {
          /* Find the extent of the current argument name.  */
          const char *arg_start = p;

          while (*p && *p != ',' && *p != ')' && *p != ' ')
            p++;

          if (! *p || p == arg_start)
	    dwarf2_macro_malformed_definition_complaint (body);
          else
            {
              /* Make sure argv has room for the new argument.  */
              if (argc >= argv_size)
                {
                  argv_size *= 2;
                  argv = xrealloc (argv, argv_size * sizeof (*argv));
                }

              argv[argc++] = copy_string (arg_start, p - arg_start);
            }

          p = consume_improper_spaces (p, body);

          /* Consume the comma, if present.  */
          if (*p == ',')
            {
              p++;

              p = consume_improper_spaces (p, body);
            }
        }

      if (*p == ')')
        {
          p++;

          if (*p == ' ')
            /* Perfectly formed definition, no complaints.  */
            macro_define_function (file, line, name,
                                   argc, (const char **) argv, 
                                   p + 1);
          else if (*p == '\0')
            {
              /* Complain, but do define it.  */
	      dwarf2_macro_malformed_definition_complaint (body);
              macro_define_function (file, line, name,
                                     argc, (const char **) argv, 
                                     p);
            }
          else
            /* Just complain.  */
	    dwarf2_macro_malformed_definition_complaint (body);
        }
      else
        /* Just complain.  */
	dwarf2_macro_malformed_definition_complaint (body);

      xfree (name);
      {
        int i;

        for (i = 0; i < argc; i++)
          xfree (argv[i]);
      }
      xfree (argv);
    }
  else
    dwarf2_macro_malformed_definition_complaint (body);
}


static void
dwarf_decode_macros (struct line_header *lh, unsigned int offset,
                     char *comp_dir, bfd *abfd,
                     struct dwarf2_cu *cu)
{
  char *mac_ptr, *mac_end;
  struct macro_source_file *current_file = 0;

  if (dwarf_macinfo_buffer == NULL)
    {
      complaint (&symfile_complaints, "missing .debug_macinfo section");
      return;
    }

  mac_ptr = dwarf_macinfo_buffer + offset;
  mac_end = dwarf_macinfo_buffer + dwarf_macinfo_size;

  for (;;)
    {
      enum dwarf_macinfo_record_type macinfo_type;

      /* Do we at least have room for a macinfo type byte?  */
      if (mac_ptr >= mac_end)
        {
	  dwarf2_macros_too_long_complaint ();
          return;
        }

      macinfo_type = read_1_byte (abfd, mac_ptr);
      mac_ptr++;

      switch (macinfo_type)
        {
          /* A zero macinfo type indicates the end of the macro
             information.  */
        case 0:
          return;

        case DW_MACINFO_define:
        case DW_MACINFO_undef:
          {
            int bytes_read;
            int line;
            char *body;

            line = read_unsigned_leb128 (abfd, mac_ptr, &bytes_read);
            mac_ptr += bytes_read;
            body = read_string (abfd, mac_ptr, &bytes_read);
            mac_ptr += bytes_read;

            if (! current_file)
	      complaint (&symfile_complaints,
			 "debug info gives macro %s outside of any file: %s",
			 macinfo_type ==
			 DW_MACINFO_define ? "definition" : macinfo_type ==
			 DW_MACINFO_undef ? "undefinition" :
			 "something-or-other", body);
            else
              {
                if (macinfo_type == DW_MACINFO_define)
                  parse_macro_definition (current_file, line, body);
                else if (macinfo_type == DW_MACINFO_undef)
                  macro_undef (current_file, line, body);
              }
          }
          break;

        case DW_MACINFO_start_file:
          {
            int bytes_read;
            int line, file;

            line = read_unsigned_leb128 (abfd, mac_ptr, &bytes_read);
            mac_ptr += bytes_read;
            file = read_unsigned_leb128 (abfd, mac_ptr, &bytes_read);
            mac_ptr += bytes_read;

            current_file = macro_start_file (file, line,
                                             current_file, comp_dir,
                                             lh, cu->objfile);
          }
          break;

        case DW_MACINFO_end_file:
          if (! current_file)
	    complaint (&symfile_complaints,
		       "macro debug info has an unmatched `close_file' directive");
          else
            {
              current_file = current_file->included_by;
              if (! current_file)
                {
                  enum dwarf_macinfo_record_type next_type;

                  /* GCC circa March 2002 doesn't produce the zero
                     type byte marking the end of the compilation
                     unit.  Complain if it's not there, but exit no
                     matter what.  */

                  /* Do we at least have room for a macinfo type byte?  */
                  if (mac_ptr >= mac_end)
                    {
		      dwarf2_macros_too_long_complaint ();
                      return;
                    }

                  /* We don't increment mac_ptr here, so this is just
                     a look-ahead.  */
                  next_type = read_1_byte (abfd, mac_ptr);
                  if (next_type != 0)
		    complaint (&symfile_complaints,
			       "no terminating 0-type entry for macros in `.debug_macinfo' section");

                  return;
                }
            }
          break;

        case DW_MACINFO_vendor_ext:
          {
            int bytes_read;
            int constant;
            char *string;

            constant = read_unsigned_leb128 (abfd, mac_ptr, &bytes_read);
            mac_ptr += bytes_read;
            string = read_string (abfd, mac_ptr, &bytes_read);
            mac_ptr += bytes_read;

            /* We don't recognize any vendor extensions.  */
          }
          break;
        }
    }
}

/* Check if the attribute's form is a DW_FORM_block*
   if so return true else false. */
static int
attr_form_is_block (struct attribute *attr)
{
  return (attr == NULL ? 0 :
      attr->form == DW_FORM_block1
      || attr->form == DW_FORM_block2
      || attr->form == DW_FORM_block4
      || attr->form == DW_FORM_block);
}

static void
dwarf2_symbol_mark_computed (struct attribute *attr, struct symbol *sym,
			     struct dwarf2_cu *cu)
{
  if (attr->form == DW_FORM_data4 || attr->form == DW_FORM_data8)
    {
      struct dwarf2_loclist_baton *baton;

      baton = obstack_alloc (&cu->objfile->objfile_obstack,
			     sizeof (struct dwarf2_loclist_baton));
      baton->objfile = cu->objfile;

      /* We don't know how long the location list is, but make sure we
	 don't run off the edge of the section.  */
      baton->size = dwarf_loc_size - DW_UNSND (attr);
      baton->data = dwarf_loc_buffer + DW_UNSND (attr);
      baton->base_address = cu->header.base_address;
      if (cu->header.base_known == 0)
	complaint (&symfile_complaints,
		   "Location list used without specifying the CU base address.");

      SYMBOL_OPS (sym) = &dwarf2_loclist_funcs;
      SYMBOL_LOCATION_BATON (sym) = baton;
    }
  else
    {
      struct dwarf2_locexpr_baton *baton;

      baton = obstack_alloc (&cu->objfile->objfile_obstack,
			     sizeof (struct dwarf2_locexpr_baton));
      baton->objfile = cu->objfile;

      if (attr_form_is_block (attr))
	{
	  /* Note that we're just copying the block's data pointer
	     here, not the actual data.  We're still pointing into the
	     dwarf_info_buffer for SYM's objfile; right now we never
	     release that buffer, but when we do clean up properly
	     this may need to change.  */
	  baton->size = DW_BLOCK (attr)->size;
	  baton->data = DW_BLOCK (attr)->data;
	}
      else
	{
	  dwarf2_invalid_attrib_class_complaint ("location description",
						 SYMBOL_NATURAL_NAME (sym));
	  baton->size = 0;
	  baton->data = NULL;
	}
      
      SYMBOL_OPS (sym) = &dwarf2_locexpr_funcs;
      SYMBOL_LOCATION_BATON (sym) = baton;
    }
}
