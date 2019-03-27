/* Symbol table definitions for GDB.

   Copyright 1986, 1988, 1989, 1990, 1991, 1992, 1993, 1994, 1995,
   1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2004 Free Software
   Foundation, Inc.

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

#if !defined (SYMTAB_H)
#define SYMTAB_H 1

/* Opaque declarations.  */
struct ui_file;
struct frame_info;
struct symbol;
struct obstack;
struct objfile;
struct block;
struct blockvector;
struct axs_value;
struct agent_expr;

/* Some of the structures in this file are space critical.
   The space-critical structures are:

     struct general_symbol_info
     struct symbol
     struct partial_symbol

   These structures are layed out to encourage good packing.
   They use ENUM_BITFIELD and short int fields, and they order the
   structure members so that fields less than a word are next
   to each other so they can be packed together. */

/* Rearranged: used ENUM_BITFIELD and rearranged field order in
   all the space critical structures (plus struct minimal_symbol).
   Memory usage dropped from 99360768 bytes to 90001408 bytes.
   I measured this with before-and-after tests of
   "HEAD-old-gdb -readnow HEAD-old-gdb" and
   "HEAD-new-gdb -readnow HEAD-old-gdb" on native i686-pc-linux-gnu,
   red hat linux 8, with LD_LIBRARY_PATH=/usr/lib/debug,
   typing "maint space 1" at the first command prompt.

   Here is another measurement (from andrew c):
     # no /usr/lib/debug, just plain glibc, like a normal user
     gdb HEAD-old-gdb
     (gdb) break internal_error
     (gdb) run
     (gdb) maint internal-error
     (gdb) backtrace
     (gdb) maint space 1

   gdb gdb_6_0_branch  2003-08-19  space used: 8896512
   gdb HEAD            2003-08-19  space used: 8904704
   gdb HEAD            2003-08-21  space used: 8396800 (+symtab.h)
   gdb HEAD            2003-08-21  space used: 8265728 (+gdbtypes.h)

   The third line shows the savings from the optimizations in symtab.h.
   The fourth line shows the savings from the optimizations in
   gdbtypes.h.  Both optimizations are in gdb HEAD now.

   --chastain 2003-08-21  */



/* Define a structure for the information that is common to all symbol types,
   including minimal symbols, partial symbols, and full symbols.  In a
   multilanguage environment, some language specific information may need to
   be recorded along with each symbol. */

/* This structure is space critical.  See space comments at the top. */

struct general_symbol_info
{
  /* Name of the symbol.  This is a required field.  Storage for the
     name is allocated on the objfile_obstack for the associated
     objfile.  For languages like C++ that make a distinction between
     the mangled name and demangled name, this is the mangled
     name.  */

  char *name;

  /* Value of the symbol.  Which member of this union to use, and what
     it means, depends on what kind of symbol this is and its
     SYMBOL_CLASS.  See comments there for more details.  All of these
     are in host byte order (though what they point to might be in
     target byte order, e.g. LOC_CONST_BYTES).  */

  union
  {
    /* The fact that this is a long not a LONGEST mainly limits the
       range of a LOC_CONST.  Since LOC_CONST_BYTES exists, I'm not
       sure that is a big deal.  */
    long ivalue;

    struct block *block;

    char *bytes;

    CORE_ADDR address;

    /* for opaque typedef struct chain */

    struct symbol *chain;
  }
  value;

  /* Since one and only one language can apply, wrap the language specific
     information inside a union. */

  union
  {
    struct cplus_specific
    {
      /* This is in fact used for C++, Java, and Objective C.  */
      char *demangled_name;
    }
    cplus_specific;
  }
  language_specific;

  /* Record the source code language that applies to this symbol.
     This is used to select one of the fields from the language specific
     union above. */

  ENUM_BITFIELD(language) language : 8;

  /* Which section is this symbol in?  This is an index into
     section_offsets for this objfile.  Negative means that the symbol
     does not get relocated relative to a section.
     Disclaimer: currently this is just used for xcoff, so don't
     expect all symbol-reading code to set it correctly (the ELF code
     also tries to set it correctly).  */

  short section;

  /* The bfd section associated with this symbol. */

  asection *bfd_section;
};

extern CORE_ADDR symbol_overlayed_address (CORE_ADDR, asection *);

/* Note that all the following SYMBOL_* macros are used with the
   SYMBOL argument being either a partial symbol, a minimal symbol or
   a full symbol.  All three types have a ginfo field.  In particular
   the SYMBOL_INIT_LANGUAGE_SPECIFIC, SYMBOL_INIT_DEMANGLED_NAME,
   SYMBOL_DEMANGLED_NAME macros cannot be entirely substituted by
   functions, unless the callers are changed to pass in the ginfo
   field only, instead of the SYMBOL parameter.  */

#define DEPRECATED_SYMBOL_NAME(symbol)	(symbol)->ginfo.name
#define SYMBOL_VALUE(symbol)		(symbol)->ginfo.value.ivalue
#define SYMBOL_VALUE_ADDRESS(symbol)	(symbol)->ginfo.value.address
#define SYMBOL_VALUE_BYTES(symbol)	(symbol)->ginfo.value.bytes
#define SYMBOL_BLOCK_VALUE(symbol)	(symbol)->ginfo.value.block
#define SYMBOL_VALUE_CHAIN(symbol)	(symbol)->ginfo.value.chain
#define SYMBOL_LANGUAGE(symbol)		(symbol)->ginfo.language
#define SYMBOL_SECTION(symbol)		(symbol)->ginfo.section
#define SYMBOL_BFD_SECTION(symbol)	(symbol)->ginfo.bfd_section

#define SYMBOL_CPLUS_DEMANGLED_NAME(symbol)	\
  (symbol)->ginfo.language_specific.cplus_specific.demangled_name

/* Initializes the language dependent portion of a symbol
   depending upon the language for the symbol. */
#define SYMBOL_INIT_LANGUAGE_SPECIFIC(symbol,language) \
  (symbol_init_language_specific (&(symbol)->ginfo, (language)))
extern void symbol_init_language_specific (struct general_symbol_info *symbol,
					   enum language language);

#define SYMBOL_INIT_DEMANGLED_NAME(symbol,obstack) \
  (symbol_init_demangled_name (&(symbol)->ginfo, (obstack)))
extern void symbol_init_demangled_name (struct general_symbol_info *symbol,
					struct obstack *obstack);

#define SYMBOL_SET_NAMES(symbol,linkage_name,len,objfile) \
  symbol_set_names (&(symbol)->ginfo, linkage_name, len, objfile)
extern void symbol_set_names (struct general_symbol_info *symbol,
			      const char *linkage_name, int len,
			      struct objfile *objfile);

/* Now come lots of name accessor macros.  Short version as to when to
   use which: Use SYMBOL_NATURAL_NAME to refer to the name of the
   symbol in the original source code.  Use SYMBOL_LINKAGE_NAME if you
   want to know what the linker thinks the symbol's name is.  Use
   SYMBOL_PRINT_NAME for output.  Use SYMBOL_DEMANGLED_NAME if you
   specifically need to know whether SYMBOL_NATURAL_NAME and
   SYMBOL_LINKAGE_NAME are different.  Don't use
   DEPRECATED_SYMBOL_NAME at all: instances of that macro should be
   replaced by SYMBOL_NATURAL_NAME, SYMBOL_LINKAGE_NAME, or perhaps
   SYMBOL_PRINT_NAME.  */

/* Return SYMBOL's "natural" name, i.e. the name that it was called in
   the original source code.  In languages like C++ where symbols may
   be mangled for ease of manipulation by the linker, this is the
   demangled name.  */

#define SYMBOL_NATURAL_NAME(symbol) \
  (symbol_natural_name (&(symbol)->ginfo))
extern char *symbol_natural_name (const struct general_symbol_info *symbol);

/* Return SYMBOL's name from the point of view of the linker.  In
   languages like C++ where symbols may be mangled for ease of
   manipulation by the linker, this is the mangled name; otherwise,
   it's the same as SYMBOL_NATURAL_NAME.  This is currently identical
   to DEPRECATED_SYMBOL_NAME, but please use SYMBOL_LINKAGE_NAME when
   appropriate: it conveys the additional semantic information that
   you really have thought about the issue and decided that you mean
   SYMBOL_LINKAGE_NAME instead of SYMBOL_NATURAL_NAME.  */

#define SYMBOL_LINKAGE_NAME(symbol)	(symbol)->ginfo.name

/* Return the demangled name for a symbol based on the language for
   that symbol.  If no demangled name exists, return NULL. */
#define SYMBOL_DEMANGLED_NAME(symbol) \
  (symbol_demangled_name (&(symbol)->ginfo))
extern char *symbol_demangled_name (struct general_symbol_info *symbol);

/* Macro that returns a version of the name of a symbol that is
   suitable for output.  In C++ this is the "demangled" form of the
   name if demangle is on and the "mangled" form of the name if
   demangle is off.  In other languages this is just the symbol name.
   The result should never be NULL.  Don't use this for internal
   purposes (e.g. storing in a hashtable): it's only suitable for
   output.  */

#define SYMBOL_PRINT_NAME(symbol)					\
  (demangle ? SYMBOL_NATURAL_NAME (symbol) : SYMBOL_LINKAGE_NAME (symbol))

/* Macro that tests a symbol for a match against a specified name string.
   First test the unencoded name, then looks for and test a C++ encoded
   name if it exists.  Note that whitespace is ignored while attempting to
   match a C++ encoded name, so that "foo::bar(int,long)" is the same as
   "foo :: bar (int, long)".
   Evaluates to zero if the match fails, or nonzero if it succeeds. */

/* Macro that tests a symbol for a match against a specified name
   string.  It tests against SYMBOL_NATURAL_NAME, and it ignores
   whitespace and trailing parentheses.  (See strcmp_iw for details
   about its behavior.)  */

#define SYMBOL_MATCHES_NATURAL_NAME(symbol, name)			\
  (strcmp_iw (SYMBOL_NATURAL_NAME (symbol), (name)) == 0)

/* Classification types for a minimal symbol.  These should be taken as
   "advisory only", since if gdb can't easily figure out a
   classification it simply selects mst_unknown.  It may also have to
   guess when it can't figure out which is a better match between two
   types (mst_data versus mst_bss) for example.  Since the minimal
   symbol info is sometimes derived from the BFD library's view of a
   file, we need to live with what information bfd supplies. */

enum minimal_symbol_type
{
  mst_unknown = 0,		/* Unknown type, the default */
  mst_text,			/* Generally executable instructions */
  mst_data,			/* Generally initialized data */
  mst_bss,			/* Generally uninitialized data */
  mst_abs,			/* Generally absolute (nonrelocatable) */
  /* GDB uses mst_solib_trampoline for the start address of a shared
     library trampoline entry.  Breakpoints for shared library functions
     are put there if the shared library is not yet loaded.
     After the shared library is loaded, lookup_minimal_symbol will
     prefer the minimal symbol from the shared library (usually
     a mst_text symbol) over the mst_solib_trampoline symbol, and the
     breakpoints will be moved to their true address in the shared
     library via breakpoint_re_set.  */
  mst_solib_trampoline,		/* Shared library trampoline code */
  /* For the mst_file* types, the names are only guaranteed to be unique
     within a given .o file.  */
  mst_file_text,		/* Static version of mst_text */
  mst_file_data,		/* Static version of mst_data */
  mst_file_bss			/* Static version of mst_bss */
};

/* Define a simple structure used to hold some very basic information about
   all defined global symbols (text, data, bss, abs, etc).  The only required
   information is the general_symbol_info.

   In many cases, even if a file was compiled with no special options for
   debugging at all, as long as was not stripped it will contain sufficient
   information to build a useful minimal symbol table using this structure.
   Even when a file contains enough debugging information to build a full
   symbol table, these minimal symbols are still useful for quickly mapping
   between names and addresses, and vice versa.  They are also sometimes
   used to figure out what full symbol table entries need to be read in. */

struct minimal_symbol
{

  /* The general symbol info required for all types of symbols.

     The SYMBOL_VALUE_ADDRESS contains the address that this symbol
     corresponds to.  */

  struct general_symbol_info ginfo;

  /* The info field is available for caching machine-specific
     information so it doesn't have to rederive the info constantly
     (over a serial line).  It is initialized to zero and stays that
     way until target-dependent code sets it.  Storage for any data
     pointed to by this field should be allocated on the
     objfile_obstack for the associated objfile.  The type would be
     "void *" except for reasons of compatibility with older
     compilers.  This field is optional.

     Currently, the AMD 29000 tdep.c uses it to remember things it has decoded
     from the instructions in the function header, and the MIPS-16 code uses
     it to identify 16-bit procedures.  */

  char *info;

  /* Size of this symbol.  end_psymtab in dbxread.c uses this
     information to calculate the end of the partial symtab based on the
     address of the last symbol plus the size of the last symbol.  */

  unsigned long size;

#ifdef SOFUN_ADDRESS_MAYBE_MISSING
  /* Which source file is this symbol in?  Only relevant for mst_file_*.  */
  char *filename;
#endif

  /* Classification type for this minimal symbol.  */

  ENUM_BITFIELD(minimal_symbol_type) type : 8;

  /* Minimal symbols with the same hash key are kept on a linked
     list.  This is the link.  */

  struct minimal_symbol *hash_next;

  /* Minimal symbols are stored in two different hash tables.  This is
     the `next' pointer for the demangled hash table.  */

  struct minimal_symbol *demangled_hash_next;
};

#define MSYMBOL_INFO(msymbol)		(msymbol)->info
#define MSYMBOL_SIZE(msymbol)		(msymbol)->size
#define MSYMBOL_TYPE(msymbol)		(msymbol)->type



/* Represent one symbol name; a variable, constant, function or typedef.  */

/* Different name domains for symbols.  Looking up a symbol specifies a
   domain and ignores symbol definitions in other name domains. */

typedef enum domain_enum_tag
{
  /* UNDEF_DOMAIN is used when a domain has not been discovered or
     none of the following apply.  This usually indicates an error either
     in the symbol information or in gdb's handling of symbols. */

  UNDEF_DOMAIN,

  /* VAR_DOMAIN is the usual domain.  In C, this contains variables,
     function names, typedef names and enum type values. */

  VAR_DOMAIN,

  /* STRUCT_DOMAIN is used in C to hold struct, union and enum type names.
     Thus, if `struct foo' is used in a C program, it produces a symbol named
     `foo' in the STRUCT_DOMAIN. */

  STRUCT_DOMAIN,

  /* LABEL_DOMAIN may be used for names of labels (for gotos);
     currently it is not used and labels are not recorded at all.  */

  LABEL_DOMAIN,

  /* Searching domains. These overlap with VAR_DOMAIN, providing
     some granularity with the search_symbols function. */

  /* Everything in VAR_DOMAIN minus FUNCTIONS_-, TYPES_-, and
     METHODS_DOMAIN */
  VARIABLES_DOMAIN,

  /* All functions -- for some reason not methods, though. */
  FUNCTIONS_DOMAIN,

  /* All defined types */
  TYPES_DOMAIN,

  /* All class methods -- why is this separated out? */
  METHODS_DOMAIN
}
domain_enum;

/* An address-class says where to find the value of a symbol.  */

enum address_class
{
  /* Not used; catches errors */

  LOC_UNDEF,

  /* Value is constant int SYMBOL_VALUE, host byteorder */

  LOC_CONST,

  /* Value is at fixed address SYMBOL_VALUE_ADDRESS */

  LOC_STATIC,

  /* Value is in register.  SYMBOL_VALUE is the register number.  */

  LOC_REGISTER,

  /* It's an argument; the value is at SYMBOL_VALUE offset in arglist.  */

  LOC_ARG,

  /* Value address is at SYMBOL_VALUE offset in arglist.  */

  LOC_REF_ARG,

  /* Value is in register number SYMBOL_VALUE.  Just like LOC_REGISTER
     except this is an argument.  Probably the cleaner way to handle
     this would be to separate address_class (which would include
     separate ARG and LOCAL to deal with the frame's arguments
     (get_frame_args_address) versus the frame's locals
     (get_frame_locals_address), and an is_argument flag.

     For some symbol formats (stabs, for some compilers at least),
     the compiler generates two symbols, an argument and a register.
     In some cases we combine them to a single LOC_REGPARM in symbol
     reading, but currently not for all cases (e.g. it's passed on the
     stack and then loaded into a register).  */

  LOC_REGPARM,

  /* Value is in specified register.  Just like LOC_REGPARM except the
     register holds the address of the argument instead of the argument
     itself. This is currently used for the passing of structs and unions
     on sparc and hppa.  It is also used for call by reference where the
     address is in a register, at least by mipsread.c.  */

  LOC_REGPARM_ADDR,

  /* Value is a local variable at SYMBOL_VALUE offset in stack frame.  */

  LOC_LOCAL,

  /* Value not used; definition in SYMBOL_TYPE.  Symbols in the domain
     STRUCT_DOMAIN all have this class.  */

  LOC_TYPEDEF,

  /* Value is address SYMBOL_VALUE_ADDRESS in the code */

  LOC_LABEL,

  /* In a symbol table, value is SYMBOL_BLOCK_VALUE of a `struct block'.
     In a partial symbol table, SYMBOL_VALUE_ADDRESS is the start address
     of the block.  Function names have this class. */

  LOC_BLOCK,

  /* Value is a constant byte-sequence pointed to by SYMBOL_VALUE_BYTES, in
     target byte order.  */

  LOC_CONST_BYTES,

  /* Value is arg at SYMBOL_VALUE offset in stack frame. Differs from
     LOC_LOCAL in that symbol is an argument; differs from LOC_ARG in
     that we find it in the frame (get_frame_locals_address), not in
     the arglist (get_frame_args_address).  Added for i960, which
     passes args in regs then copies to frame.  */

  LOC_LOCAL_ARG,

  /* Value is at SYMBOL_VALUE offset from the current value of
     register number SYMBOL_BASEREG.  This exists mainly for the same
     things that LOC_LOCAL and LOC_ARG do; but we need to do this
     instead because on 88k DWARF gives us the offset from the
     frame/stack pointer, rather than the offset from the "canonical
     frame address" used by COFF, stabs, etc., and we don't know how
     to convert between these until we start examining prologues.

     Note that LOC_BASEREG is much less general than a DWARF expression.
     We don't need the generality (at least not yet), and storing a general
     DWARF expression would presumably take up more space than the existing
     scheme.  */

  LOC_BASEREG,

  /* Same as LOC_BASEREG but it is an argument.  */

  LOC_BASEREG_ARG,

  /* Value is at fixed address, but the address of the variable has
     to be determined from the minimal symbol table whenever the
     variable is referenced.
     This happens if debugging information for a global symbol is
     emitted and the corresponding minimal symbol is defined
     in another object file or runtime common storage.
     The linker might even remove the minimal symbol if the global
     symbol is never referenced, in which case the symbol remains
     unresolved.  */

  LOC_UNRESOLVED,

  /* Value is at a thread-specific location calculated by a
     target-specific method. This is used only by hppa.  */

  LOC_HP_THREAD_LOCAL_STATIC,

  /* The variable does not actually exist in the program.
     The value is ignored.  */

  LOC_OPTIMIZED_OUT,

  /* The variable is static, but actually lives at * (address).
   * I.e. do an extra indirection to get to it.
   * This is used on HP-UX to get at globals that are allocated
   * in shared libraries, where references from images other
   * than the one where the global was allocated are done
   * with a level of indirection.
   */

  LOC_INDIRECT,

  /* The variable's address is computed by a set of location
     functions (see "struct location_funcs" below).  */
  LOC_COMPUTED,

  /* Same as LOC_COMPUTED, but for function arguments.  */
  LOC_COMPUTED_ARG
};

/* The methods needed to implement a symbol class.  These methods can
   use the symbol's .aux_value for additional per-symbol information.

   At present this is only used to implement location expressions.  */

struct symbol_ops
{

  /* Return the value of the variable SYMBOL, relative to the stack
     frame FRAME.  If the variable has been optimized out, return
     zero.

     Iff `read_needs_frame (SYMBOL)' is zero, then FRAME may be zero.  */

  struct value *(*read_variable) (struct symbol * symbol,
				  struct frame_info * frame);

  /* Return non-zero if we need a frame to find the value of the SYMBOL.  */
  int (*read_needs_frame) (struct symbol * symbol);

  /* Write to STREAM a natural-language description of the location of
     SYMBOL.  */
  int (*describe_location) (struct symbol * symbol, struct ui_file * stream);

  /* Tracepoint support.  Append bytecodes to the tracepoint agent
     expression AX that push the address of the object SYMBOL.  Set
     VALUE appropriately.  Note --- for objects in registers, this
     needn't emit any code; as long as it sets VALUE properly, then
     the caller will generate the right code in the process of
     treating this as an lvalue or rvalue.  */

  void (*tracepoint_var_ref) (struct symbol * symbol, struct agent_expr * ax,
			      struct axs_value * value);
};

/* This structure is space critical.  See space comments at the top. */

struct symbol
{

  /* The general symbol info required for all types of symbols. */

  struct general_symbol_info ginfo;

  /* Data type of value */

  struct type *type;

  /* Domain code.  */

  ENUM_BITFIELD(domain_enum_tag) domain : 6;

  /* Address class */
  /* NOTE: cagney/2003-11-02: The fields "aclass" and "ops" contain
     overlapping information.  By creating a per-aclass ops vector, or
     using the aclass as an index into an ops table, the aclass and
     ops fields can be merged.  The latter, for instance, would shave
     32-bits from each symbol (relative to a symbol lookup, any table
     index overhead would be in the noise).  */

  ENUM_BITFIELD(address_class) aclass : 6;

  /* Line number of definition.  FIXME:  Should we really make the assumption
     that nobody will try to debug files longer than 64K lines?  What about
     machine generated programs? */

  unsigned short line;

  /* Method's for symbol's of this class.  */
  /* NOTE: cagney/2003-11-02: See comment above attached to "aclass".  */

  const struct symbol_ops *ops;

  /* Some symbols require additional information to be recorded on a
     per- symbol basis.  Stash those values here. */

  union
  {
    /* Used by LOC_BASEREG and LOC_BASEREG_ARG.  */
    short basereg;
    /* An arbitrary data pointer.  Note that this data must be
       allocated using the same obstack as the symbol itself.  */
    /* So far it is only used by LOC_COMPUTED and LOC_COMPUTED_ARG to
       find the location location information.  For a LOC_BLOCK symbol
       for a function in a compilation unit compiled with DWARF 2
       information, this is information used internally by the DWARF 2
       code --- specifically, the location expression for the frame
       base for this function.  */
    /* FIXME drow/2003-02-21: For the LOC_BLOCK case, it might be better
       to add a magic symbol to the block containing this information,
       or to have a generic debug info annotation slot for symbols.  */
    void *ptr;
  }
  aux_value;

  struct symbol *hash_next;
};


#define SYMBOL_DOMAIN(symbol)	(symbol)->domain
#define SYMBOL_CLASS(symbol)		(symbol)->aclass
#define SYMBOL_TYPE(symbol)		(symbol)->type
#define SYMBOL_LINE(symbol)		(symbol)->line
#define SYMBOL_BASEREG(symbol)		(symbol)->aux_value.basereg
#define SYMBOL_OBJFILE(symbol)          (symbol)->aux_value.objfile
#define SYMBOL_OPS(symbol)              (symbol)->ops
#define SYMBOL_LOCATION_BATON(symbol)   (symbol)->aux_value.ptr

/* A partial_symbol records the name, domain, and address class of
   symbols whose types we have not parsed yet.  For functions, it also
   contains their memory address, so we can find them from a PC value.
   Each partial_symbol sits in a partial_symtab, all of which are chained
   on a  partial symtab list and which points to the corresponding 
   normal symtab once the partial_symtab has been referenced.  */

/* This structure is space critical.  See space comments at the top. */

struct partial_symbol
{

  /* The general symbol info required for all types of symbols. */

  struct general_symbol_info ginfo;

  /* Name space code.  */

  ENUM_BITFIELD(domain_enum_tag) domain : 6;

  /* Address class (for info_symbols) */

  ENUM_BITFIELD(address_class) aclass : 6;

};

#define PSYMBOL_DOMAIN(psymbol)	(psymbol)->domain
#define PSYMBOL_CLASS(psymbol)		(psymbol)->aclass


/* Each item represents a line-->pc (or the reverse) mapping.  This is
   somewhat more wasteful of space than one might wish, but since only
   the files which are actually debugged are read in to core, we don't
   waste much space.  */

struct linetable_entry
{
  int line;
  CORE_ADDR pc;
};

/* The order of entries in the linetable is significant.  They should
   be sorted by increasing values of the pc field.  If there is more than
   one entry for a given pc, then I'm not sure what should happen (and
   I not sure whether we currently handle it the best way).

   Example: a C for statement generally looks like this

   10   0x100   - for the init/test part of a for stmt.
   20   0x200
   30   0x300
   10   0x400   - for the increment part of a for stmt.

   If an entry has a line number of zero, it marks the start of a PC
   range for which no line number information is available.  It is
   acceptable, though wasteful of table space, for such a range to be
   zero length.  */

struct linetable
{
  int nitems;

  /* Actually NITEMS elements.  If you don't like this use of the
     `struct hack', you can shove it up your ANSI (seriously, if the
     committee tells us how to do it, we can probably go along).  */
  struct linetable_entry item[1];
};

/* How to relocate the symbols from each section in a symbol file.
   Each struct contains an array of offsets.
   The ordering and meaning of the offsets is file-type-dependent;
   typically it is indexed by section numbers or symbol types or
   something like that.

   To give us flexibility in changing the internal representation
   of these offsets, the ANOFFSET macro must be used to insert and
   extract offset values in the struct.  */

struct section_offsets
{
  CORE_ADDR offsets[1];		/* As many as needed. */
};

#define	ANOFFSET(secoff, whichone) \
   ((whichone == -1) \
    ? (internal_error (__FILE__, __LINE__, "Section index is uninitialized"), -1) \
    : secoff->offsets[whichone])

/* The size of a section_offsets table for N sections.  */
#define SIZEOF_N_SECTION_OFFSETS(n) \
  (sizeof (struct section_offsets) \
   + sizeof (((struct section_offsets *) 0)->offsets) * ((n)-1))

/* Each source file or header is represented by a struct symtab. 
   These objects are chained through the `next' field.  */

struct symtab
{

  /* Chain of all existing symtabs.  */

  struct symtab *next;

  /* List of all symbol scope blocks for this symtab.  May be shared
     between different symtabs (and normally is for all the symtabs
     in a given compilation unit).  */

  struct blockvector *blockvector;

  /* Table mapping core addresses to line numbers for this file.
     Can be NULL if none.  Never shared between different symtabs.  */

  struct linetable *linetable;

  /* Section in objfile->section_offsets for the blockvector and
     the linetable.  Probably always SECT_OFF_TEXT.  */

  int block_line_section;

  /* If several symtabs share a blockvector, exactly one of them
     should be designated the primary, so that the blockvector
     is relocated exactly once by objfile_relocate.  */

  int primary;

  /* The macro table for this symtab.  Like the blockvector, this
     may be shared between different symtabs --- and normally is for
     all the symtabs in a given compilation unit.  */
  struct macro_table *macro_table;

  /* Name of this source file.  */

  char *filename;

  /* Directory in which it was compiled, or NULL if we don't know.  */

  char *dirname;

  /* This component says how to free the data we point to:
     free_contents => do a tree walk and free each object.
     free_nothing => do nothing; some other symtab will free
     the data this one uses.
     free_linetable => free just the linetable.  FIXME: Is this redundant
     with the primary field?  */

  enum free_code
  {
    free_nothing, free_contents, free_linetable
  }
  free_code;

  /* A function to call to free space, if necessary.  This is IN
     ADDITION to the action indicated by free_code.  */

  void (*free_func)(struct symtab *symtab);

  /* Total number of lines found in source file.  */

  int nlines;

  /* line_charpos[N] is the position of the (N-1)th line of the
     source file.  "position" means something we can lseek() to; it
     is not guaranteed to be useful any other way.  */

  int *line_charpos;

  /* Language of this source file.  */

  enum language language;

  /* String that identifies the format of the debugging information, such
     as "stabs", "dwarf 1", "dwarf 2", "coff", etc.  This is mostly useful
     for automated testing of gdb but may also be information that is
     useful to the user. */

  char *debugformat;

  /* String of version information.  May be zero.  */

  char *version;

  /* Full name of file as found by searching the source path.
     NULL if not yet known.  */

  char *fullname;

  /* Object file from which this symbol information was read.  */

  struct objfile *objfile;

};

#define BLOCKVECTOR(symtab)	(symtab)->blockvector
#define LINETABLE(symtab)	(symtab)->linetable


/* Each source file that has not been fully read in is represented by
   a partial_symtab.  This contains the information on where in the
   executable the debugging symbols for a specific file are, and a
   list of names of global symbols which are located in this file.
   They are all chained on partial symtab lists.

   Even after the source file has been read into a symtab, the
   partial_symtab remains around.  They are allocated on an obstack,
   objfile_obstack.  FIXME, this is bad for dynamic linking or VxWorks-
   style execution of a bunch of .o's.  */

struct partial_symtab
{

  /* Chain of all existing partial symtabs.  */

  struct partial_symtab *next;

  /* Name of the source file which this partial_symtab defines */

  char *filename;

  /* Full path of the source file.  NULL if not known.  */

  char *fullname;

  /* Information about the object file from which symbols should be read.  */

  struct objfile *objfile;

  /* Set of relocation offsets to apply to each section.  */

  struct section_offsets *section_offsets;

  /* Range of text addresses covered by this file; texthigh is the
     beginning of the next section. */

  CORE_ADDR textlow;
  CORE_ADDR texthigh;

  /* Array of pointers to all of the partial_symtab's which this one
     depends on.  Since this array can only be set to previous or
     the current (?) psymtab, this dependency tree is guaranteed not
     to have any loops.  "depends on" means that symbols must be read
     for the dependencies before being read for this psymtab; this is
     for type references in stabs, where if foo.c includes foo.h, declarations
     in foo.h may use type numbers defined in foo.c.  For other debugging
     formats there may be no need to use dependencies.  */

  struct partial_symtab **dependencies;

  int number_of_dependencies;

  /* Global symbol list.  This list will be sorted after readin to
     improve access.  Binary search will be the usual method of
     finding a symbol within it. globals_offset is an integer offset
     within global_psymbols[].  */

  int globals_offset;
  int n_global_syms;

  /* Static symbol list.  This list will *not* be sorted after readin;
     to find a symbol in it, exhaustive search must be used.  This is
     reasonable because searches through this list will eventually
     lead to either the read in of a files symbols for real (assumed
     to take a *lot* of time; check) or an error (and we don't care
     how long errors take).  This is an offset and size within
     static_psymbols[].  */

  int statics_offset;
  int n_static_syms;

  /* Pointer to symtab eventually allocated for this source file, 0 if
     !readin or if we haven't looked for the symtab after it was readin.  */

  struct symtab *symtab;

  /* Pointer to function which will read in the symtab corresponding to
     this psymtab.  */

  void (*read_symtab) (struct partial_symtab *);

  /* Information that lets read_symtab() locate the part of the symbol table
     that this psymtab corresponds to.  This information is private to the
     format-dependent symbol reading routines.  For further detail examine
     the various symbol reading modules.  Should really be (void *) but is
     (char *) as with other such gdb variables.  (FIXME) */

  char *read_symtab_private;

  /* Non-zero if the symtab corresponding to this psymtab has been readin */

  unsigned char readin;
};

/* A fast way to get from a psymtab to its symtab (after the first time).  */
#define	PSYMTAB_TO_SYMTAB(pst)  \
    ((pst) -> symtab != NULL ? (pst) -> symtab : psymtab_to_symtab (pst))


/* The virtual function table is now an array of structures which have the
   form { int16 offset, delta; void *pfn; }. 

   In normal virtual function tables, OFFSET is unused.
   DELTA is the amount which is added to the apparent object's base
   address in order to point to the actual object to which the
   virtual function should be applied.
   PFN is a pointer to the virtual function.

   Note that this macro is g++ specific (FIXME). */

#define VTBL_FNADDR_OFFSET 2

/* External variables and functions for the objects described above. */

/* See the comment in symfile.c about how current_objfile is used. */

extern struct objfile *current_objfile;

/* True if we are nested inside psymtab_to_symtab. */

extern int currently_reading_symtab;

/* From utils.c.  */
extern int demangle;
extern int asm_demangle;

/* symtab.c lookup functions */

/* lookup a symbol table by source file name */

extern struct symtab *lookup_symtab (const char *);

/* lookup a symbol by name (optional block, optional symtab) */

extern struct symbol *lookup_symbol (const char *, const struct block *,
				     const domain_enum, int *,
				     struct symtab **);

/* A default version of lookup_symbol_nonlocal for use by languages
   that can't think of anything better to do.  */

extern struct symbol *basic_lookup_symbol_nonlocal (const char *,
						    const char *,
						    const struct block *,
						    const domain_enum,
						    struct symtab **);

/* Some helper functions for languages that need to write their own
   lookup_symbol_nonlocal functions.  */

/* Lookup a symbol in the static block associated to BLOCK, if there
   is one; do nothing if BLOCK is NULL or a global block.  */

extern struct symbol *lookup_symbol_static (const char *name,
					    const char *linkage_name,
					    const struct block *block,
					    const domain_enum domain,
					    struct symtab **symtab);

/* Lookup a symbol in all files' global blocks (searching psymtabs if
   necessary).  */

extern struct symbol *lookup_symbol_global (const char *name,
					    const char *linkage_name,
					    const domain_enum domain,
					    struct symtab **symtab);

/* Lookup a symbol within the block BLOCK.  This, unlike
   lookup_symbol_block, will set SYMTAB and BLOCK_FOUND correctly, and
   will fix up the symbol if necessary.  */

extern struct symbol *lookup_symbol_aux_block (const char *name,
					       const char *linkage_name,
					       const struct block *block,
					       const domain_enum domain,
					       struct symtab **symtab);

/* Lookup a partial symbol.  */

extern struct partial_symbol *lookup_partial_symbol (struct partial_symtab *,
						     const char *,
						     const char *, int,
						     domain_enum);

/* lookup a symbol by name, within a specified block */

extern struct symbol *lookup_block_symbol (const struct block *, const char *,
					   const char *,
					   const domain_enum);

/* lookup a [struct, union, enum] by name, within a specified block */

extern struct type *lookup_struct (char *, struct block *);

extern struct type *lookup_union (char *, struct block *);

extern struct type *lookup_enum (char *, struct block *);

/* from blockframe.c: */

/* lookup the function symbol corresponding to the address */

extern struct symbol *find_pc_function (CORE_ADDR);

/* lookup the function corresponding to the address and section */

extern struct symbol *find_pc_sect_function (CORE_ADDR, asection *);

/* lookup function from address, return name, start addr and end addr */

extern int find_pc_partial_function (CORE_ADDR, char **, CORE_ADDR *,
				     CORE_ADDR *);

extern void clear_pc_function_cache (void);

extern int find_pc_sect_partial_function (CORE_ADDR, asection *,
					  char **, CORE_ADDR *, CORE_ADDR *);

/* from symtab.c: */

/* lookup partial symbol table by filename */

extern struct partial_symtab *lookup_partial_symtab (const char *);

/* lookup partial symbol table by address */

extern struct partial_symtab *find_pc_psymtab (CORE_ADDR);

/* lookup partial symbol table by address and section */

extern struct partial_symtab *find_pc_sect_psymtab (CORE_ADDR, asection *);

/* lookup full symbol table by address */

extern struct symtab *find_pc_symtab (CORE_ADDR);

/* lookup full symbol table by address and section */

extern struct symtab *find_pc_sect_symtab (CORE_ADDR, asection *);

/* lookup partial symbol by address */

extern struct partial_symbol *find_pc_psymbol (struct partial_symtab *,
					       CORE_ADDR);

/* lookup partial symbol by address and section */

extern struct partial_symbol *find_pc_sect_psymbol (struct partial_symtab *,
						    CORE_ADDR, asection *);

extern int find_pc_line_pc_range (CORE_ADDR, CORE_ADDR *, CORE_ADDR *);

extern void reread_symbols (void);

extern struct type *lookup_transparent_type (const char *);
extern struct type *basic_lookup_transparent_type (const char *);


/* Macro for name of symbol to indicate a file compiled with gcc. */
#ifndef GCC_COMPILED_FLAG_SYMBOL
#define GCC_COMPILED_FLAG_SYMBOL "gcc_compiled."
#endif

/* Macro for name of symbol to indicate a file compiled with gcc2. */
#ifndef GCC2_COMPILED_FLAG_SYMBOL
#define GCC2_COMPILED_FLAG_SYMBOL "gcc2_compiled."
#endif

/* Functions for dealing with the minimal symbol table, really a misc
   address<->symbol mapping for things we don't have debug symbols for.  */

extern void prim_record_minimal_symbol (const char *, CORE_ADDR,
					enum minimal_symbol_type,
					struct objfile *);

extern struct minimal_symbol *prim_record_minimal_symbol_and_info
  (const char *, CORE_ADDR,
   enum minimal_symbol_type,
   char *info, int section, asection * bfd_section, struct objfile *);

extern unsigned int msymbol_hash_iw (const char *);

extern unsigned int msymbol_hash (const char *);

extern void
add_minsym_to_hash_table (struct minimal_symbol *sym,
			  struct minimal_symbol **table);

extern struct minimal_symbol *lookup_minimal_symbol (const char *,
						     const char *,
						     struct objfile *);

extern struct minimal_symbol *lookup_minimal_symbol_text (const char *,
							  struct objfile *);

struct minimal_symbol *lookup_minimal_symbol_solib_trampoline (const char *,
							       struct objfile
							       *);

extern struct minimal_symbol *lookup_minimal_symbol_by_pc (CORE_ADDR);

extern struct minimal_symbol *lookup_minimal_symbol_by_pc_section (CORE_ADDR,
								   asection
								   *);

extern struct minimal_symbol
  *lookup_solib_trampoline_symbol_by_pc (CORE_ADDR);

extern CORE_ADDR find_solib_trampoline_target (CORE_ADDR);

extern void init_minimal_symbol_collection (void);

extern struct cleanup *make_cleanup_discard_minimal_symbols (void);

extern void install_minimal_symbols (struct objfile *);

/* Sort all the minimal symbols in OBJFILE.  */

extern void msymbols_sort (struct objfile *objfile);

struct symtab_and_line
{
  struct symtab *symtab;
  asection *section;
  /* Line number.  Line numbers start at 1 and proceed through symtab->nlines.
     0 is never a valid line number; it is used to indicate that line number
     information is not available.  */
  int line;

  CORE_ADDR pc;
  CORE_ADDR end;
};

extern void init_sal (struct symtab_and_line *sal);

struct symtabs_and_lines
{
  struct symtab_and_line *sals;
  int nelts;
};



/* Some types and macros needed for exception catchpoints.
   Can't put these in target.h because symtab_and_line isn't
   known there. This file will be included by breakpoint.c,
   hppa-tdep.c, etc. */

/* Enums for exception-handling support */
enum exception_event_kind
{
  EX_EVENT_THROW,
  EX_EVENT_CATCH
};

/* Type for returning info about an exception */
struct exception_event_record
{
  enum exception_event_kind kind;
  struct symtab_and_line throw_sal;
  struct symtab_and_line catch_sal;
  /* This may need to be extended in the future, if
     some platforms allow reporting more information,
     such as point of rethrow, type of exception object,
     type expected by catch clause, etc. */
};

#define CURRENT_EXCEPTION_KIND       (current_exception_event->kind)
#define CURRENT_EXCEPTION_CATCH_SAL  (current_exception_event->catch_sal)
#define CURRENT_EXCEPTION_CATCH_LINE (current_exception_event->catch_sal.line)
#define CURRENT_EXCEPTION_CATCH_FILE (current_exception_event->catch_sal.symtab->filename)
#define CURRENT_EXCEPTION_CATCH_PC   (current_exception_event->catch_sal.pc)
#define CURRENT_EXCEPTION_THROW_SAL  (current_exception_event->throw_sal)
#define CURRENT_EXCEPTION_THROW_LINE (current_exception_event->throw_sal.line)
#define CURRENT_EXCEPTION_THROW_FILE (current_exception_event->throw_sal.symtab->filename)
#define CURRENT_EXCEPTION_THROW_PC   (current_exception_event->throw_sal.pc)


/* Given a pc value, return line number it is in.  Second arg nonzero means
   if pc is on the boundary use the previous statement's line number.  */

extern struct symtab_and_line find_pc_line (CORE_ADDR, int);

/* Same function, but specify a section as well as an address */

extern struct symtab_and_line find_pc_sect_line (CORE_ADDR, asection *, int);

/* Given a symtab and line number, return the pc there.  */

extern int find_line_pc (struct symtab *, int, CORE_ADDR *);

extern int find_line_pc_range (struct symtab_and_line, CORE_ADDR *,
			       CORE_ADDR *);

extern void resolve_sal_pc (struct symtab_and_line *);

/* Given a string, return the line specified by it.  For commands like "list"
   and "breakpoint".  */

extern struct symtabs_and_lines decode_line_spec (char *, int);

extern struct symtabs_and_lines decode_line_spec_1 (char *, int);

/* Symmisc.c */

void maintenance_print_symbols (char *, int);

void maintenance_print_psymbols (char *, int);

void maintenance_print_msymbols (char *, int);

void maintenance_print_objfiles (char *, int);

void maintenance_info_symtabs (char *, int);

void maintenance_info_psymtabs (char *, int);

void maintenance_check_symtabs (char *, int);

/* maint.c */

void maintenance_print_statistics (char *, int);

extern void free_symtab (struct symtab *);

/* Symbol-reading stuff in symfile.c and solib.c.  */

extern struct symtab *psymtab_to_symtab (struct partial_symtab *);

extern void clear_solib (void);

/* source.c */

extern int identify_source_line (struct symtab *, int, int, CORE_ADDR);

extern void print_source_lines (struct symtab *, int, int, int);

extern void forget_cached_source_info (void);

extern void select_source_symtab (struct symtab *);

extern char **make_symbol_completion_list (char *, char *);

extern char **make_file_symbol_completion_list (char *, char *, char *);

extern char **make_source_files_completion_list (char *, char *);

/* symtab.c */

extern struct partial_symtab *find_main_psymtab (void);

extern struct symtab *find_line_symtab (struct symtab *, int, int *, int *);

extern struct symtab_and_line find_function_start_sal (struct symbol *sym,
						       int);

/* symfile.c */

extern void clear_symtab_users (void);

extern enum language deduce_language_from_filename (char *);

/* symtab.c */

extern int in_prologue (CORE_ADDR pc, CORE_ADDR func_start);

extern CORE_ADDR skip_prologue_using_sal (CORE_ADDR func_addr);

extern struct symbol *fixup_symbol_section (struct symbol *,
					    struct objfile *);

extern struct partial_symbol *fixup_psymbol_section (struct partial_symbol
						     *psym,
						     struct objfile *objfile);

/* Symbol searching */

/* When using search_symbols, a list of the following structs is returned.
   Callers must free the search list using free_search_symbols! */
struct symbol_search
{
  /* The block in which the match was found. Could be, for example,
     STATIC_BLOCK or GLOBAL_BLOCK. */
  int block;

  /* Information describing what was found.

     If symtab abd symbol are NOT NULL, then information was found
     for this match. */
  struct symtab *symtab;
  struct symbol *symbol;

  /* If msymbol is non-null, then a match was made on something for
     which only minimal_symbols exist. */
  struct minimal_symbol *msymbol;

  /* A link to the next match, or NULL for the end. */
  struct symbol_search *next;
};

extern void search_symbols (char *, domain_enum, int, char **,
			    struct symbol_search **);
extern void free_search_symbols (struct symbol_search *);
extern struct cleanup *make_cleanup_free_search_symbols (struct symbol_search
							 *);

/* The name of the ``main'' function.
   FIXME: cagney/2001-03-20: Can't make main_name() const since some
   of the calling code currently assumes that the string isn't
   const. */
extern void set_main_name (const char *name);
extern /*const */ char *main_name (void);

#endif /* !defined(SYMTAB_H) */
