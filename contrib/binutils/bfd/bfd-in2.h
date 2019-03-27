/* DO NOT EDIT!  -*- buffer-read-only: t -*-  This file is automatically 
   generated from "bfd-in.h", "init.c", "opncls.c", "libbfd.c", 
   "bfdio.c", "bfdwin.c", "section.c", "archures.c", "reloc.c", 
   "syms.c", "bfd.c", "archive.c", "corefile.c", "targets.c", "format.c", 
   "linker.c" and "simple.c".
   Run "make headers" in your build bfd/ to regenerate.  */

/* Main header file for the bfd library -- portable access to object files.

   Copyright 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007
   Free Software Foundation, Inc.

   Contributed by Cygnus Support.

   This file is part of BFD, the Binary File Descriptor library.

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
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

#ifndef __BFD_H_SEEN__
#define __BFD_H_SEEN__

#ifdef __cplusplus
extern "C" {
#endif

#include "ansidecl.h"
#include "symcat.h"
#if defined (__STDC__) || defined (ALMOST_STDC) || defined (HAVE_STRINGIZE)
#ifndef SABER
/* This hack is to avoid a problem with some strict ANSI C preprocessors.
   The problem is, "32_" is not a valid preprocessing token, and we don't
   want extra underscores (e.g., "nlm_32_").  The XCONCAT2 macro will
   cause the inner CONCAT2 macros to be evaluated first, producing
   still-valid pp-tokens.  Then the final concatenation can be done.  */
#undef CONCAT4
#define CONCAT4(a,b,c,d) XCONCAT2(CONCAT2(a,b),CONCAT2(c,d))
#endif
#endif

/* This is a utility macro to handle the situation where the code
   wants to place a constant string into the code, followed by a
   comma and then the length of the string.  Doing this by hand
   is error prone, so using this macro is safer.  The macro will
   also safely handle the case where a NULL is passed as the arg.  */
#define STRING_COMMA_LEN(STR) (STR), ((STR) ? sizeof (STR) - 1 : 0)
/* Unfortunately it is not possible to use the STRING_COMMA_LEN macro
   to create the arguments to another macro, since the preprocessor
   will mis-count the number of arguments to the outer macro (by not
   evaluating STRING_COMMA_LEN and so missing the comma).  This is a
   problem for example when trying to use STRING_COMMA_LEN to build
   the arguments to the strncmp() macro.  Hence this alternative
   definition of strncmp is provided here.
   
   Note - these macros do NOT work if STR2 is not a constant string.  */
#define CONST_STRNEQ(STR1,STR2) (strncmp ((STR1), (STR2), sizeof (STR2) - 1) == 0)
  /* strcpy() can have a similar problem, but since we know we are
     copying a constant string, we can use memcpy which will be faster
     since there is no need to check for a NUL byte inside STR.  We
     can also save time if we do not need to copy the terminating NUL.  */
#define LITMEMCPY(DEST,STR2) memcpy ((DEST), (STR2), sizeof (STR2) - 1)
#define LITSTRCPY(DEST,STR2) memcpy ((DEST), (STR2), sizeof (STR2))


/* The word size used by BFD on the host.  This may be 64 with a 32
   bit target if the host is 64 bit, or if other 64 bit targets have
   been selected with --enable-targets, or if --enable-64-bit-bfd.  */
#define BFD_ARCH_SIZE @wordsize@

/* The word size of the default bfd target.  */
#define BFD_DEFAULT_TARGET_SIZE @bfd_default_target_size@

#define BFD_HOST_64BIT_LONG @BFD_HOST_64BIT_LONG@
#define BFD_HOST_LONG_LONG @BFD_HOST_LONG_LONG@
#if @BFD_HOST_64_BIT_DEFINED@
#define BFD_HOST_64_BIT @BFD_HOST_64_BIT@
#define BFD_HOST_U_64_BIT @BFD_HOST_U_64_BIT@
typedef BFD_HOST_64_BIT bfd_int64_t;
typedef BFD_HOST_U_64_BIT bfd_uint64_t;
#endif

#if BFD_ARCH_SIZE >= 64
#define BFD64
#endif

#ifndef INLINE
#if __GNUC__ >= 2
#define INLINE __inline__
#else
#define INLINE
#endif
#endif

/* Forward declaration.  */
typedef struct bfd bfd;

/* Boolean type used in bfd.  Too many systems define their own
   versions of "boolean" for us to safely typedef a "boolean" of
   our own.  Using an enum for "bfd_boolean" has its own set of
   problems, with strange looking casts required to avoid warnings
   on some older compilers.  Thus we just use an int.

   General rule: Functions which are bfd_boolean return TRUE on
   success and FALSE on failure (unless they're a predicate).  */

typedef int bfd_boolean;
#undef FALSE
#undef TRUE
#define FALSE 0
#define TRUE 1

#ifdef BFD64

#ifndef BFD_HOST_64_BIT
 #error No 64 bit integer type available
#endif /* ! defined (BFD_HOST_64_BIT) */

typedef BFD_HOST_U_64_BIT bfd_vma;
typedef BFD_HOST_64_BIT bfd_signed_vma;
typedef BFD_HOST_U_64_BIT bfd_size_type;
typedef BFD_HOST_U_64_BIT symvalue;

#ifndef fprintf_vma
#if BFD_HOST_64BIT_LONG
#define sprintf_vma(s,x) sprintf (s, "%016lx", x)
#define fprintf_vma(f,x) fprintf (f, "%016lx", x)
#else
#define _bfd_int64_low(x) ((unsigned long) (((x) & 0xffffffff)))
#define _bfd_int64_high(x) ((unsigned long) (((x) >> 32) & 0xffffffff))
#define fprintf_vma(s,x) \
  fprintf ((s), "%08lx%08lx", _bfd_int64_high (x), _bfd_int64_low (x))
#define sprintf_vma(s,x) \
  sprintf ((s), "%08lx%08lx", _bfd_int64_high (x), _bfd_int64_low (x))
#endif
#endif

#else /* not BFD64  */

/* Represent a target address.  Also used as a generic unsigned type
   which is guaranteed to be big enough to hold any arithmetic types
   we need to deal with.  */
typedef unsigned long bfd_vma;

/* A generic signed type which is guaranteed to be big enough to hold any
   arithmetic types we need to deal with.  Can be assumed to be compatible
   with bfd_vma in the same way that signed and unsigned ints are compatible
   (as parameters, in assignment, etc).  */
typedef long bfd_signed_vma;

typedef unsigned long symvalue;
typedef unsigned long bfd_size_type;

/* Print a bfd_vma x on stream s.  */
#define fprintf_vma(s,x) fprintf (s, "%08lx", x)
#define sprintf_vma(s,x) sprintf (s, "%08lx", x)

#endif /* not BFD64  */

#define HALF_BFD_SIZE_TYPE \
  (((bfd_size_type) 1) << (8 * sizeof (bfd_size_type) / 2))

#ifndef BFD_HOST_64_BIT
/* Fall back on a 32 bit type.  The idea is to make these types always
   available for function return types, but in the case that
   BFD_HOST_64_BIT is undefined such a function should abort or
   otherwise signal an error.  */
typedef bfd_signed_vma bfd_int64_t;
typedef bfd_vma bfd_uint64_t;
#endif

/* An offset into a file.  BFD always uses the largest possible offset
   based on the build time availability of fseek, fseeko, or fseeko64.  */
typedef @bfd_file_ptr@ file_ptr;
typedef unsigned @bfd_file_ptr@ ufile_ptr;

extern void bfd_sprintf_vma (bfd *, char *, bfd_vma);
extern void bfd_fprintf_vma (bfd *, void *, bfd_vma);

#define printf_vma(x) fprintf_vma(stdout,x)
#define bfd_printf_vma(abfd,x) bfd_fprintf_vma (abfd,stdout,x)

typedef unsigned int flagword;	/* 32 bits of flags */
typedef unsigned char bfd_byte;

/* File formats.  */

typedef enum bfd_format
{
  bfd_unknown = 0,	/* File format is unknown.  */
  bfd_object,		/* Linker/assembler/compiler output.  */
  bfd_archive,		/* Object archive file.  */
  bfd_core,		/* Core dump.  */
  bfd_type_end		/* Marks the end; don't use it!  */
}
bfd_format;

/* Values that may appear in the flags field of a BFD.  These also
   appear in the object_flags field of the bfd_target structure, where
   they indicate the set of flags used by that backend (not all flags
   are meaningful for all object file formats) (FIXME: at the moment,
   the object_flags values have mostly just been copied from backend
   to another, and are not necessarily correct).  */

/* No flags.  */
#define BFD_NO_FLAGS   	0x00

/* BFD contains relocation entries.  */
#define HAS_RELOC   	0x01

/* BFD is directly executable.  */
#define EXEC_P      	0x02

/* BFD has line number information (basically used for F_LNNO in a
   COFF header).  */
#define HAS_LINENO  	0x04

/* BFD has debugging information.  */
#define HAS_DEBUG   	0x08

/* BFD has symbols.  */
#define HAS_SYMS    	0x10

/* BFD has local symbols (basically used for F_LSYMS in a COFF
   header).  */
#define HAS_LOCALS  	0x20

/* BFD is a dynamic object.  */
#define DYNAMIC     	0x40

/* Text section is write protected (if D_PAGED is not set, this is
   like an a.out NMAGIC file) (the linker sets this by default, but
   clears it for -r or -N).  */
#define WP_TEXT     	0x80

/* BFD is dynamically paged (this is like an a.out ZMAGIC file) (the
   linker sets this by default, but clears it for -r or -n or -N).  */
#define D_PAGED     	0x100

/* BFD is relaxable (this means that bfd_relax_section may be able to
   do something) (sometimes bfd_relax_section can do something even if
   this is not set).  */
#define BFD_IS_RELAXABLE 0x200

/* This may be set before writing out a BFD to request using a
   traditional format.  For example, this is used to request that when
   writing out an a.out object the symbols not be hashed to eliminate
   duplicates.  */
#define BFD_TRADITIONAL_FORMAT 0x400

/* This flag indicates that the BFD contents are actually cached in
   memory.  If this is set, iostream points to a bfd_in_memory struct.  */
#define BFD_IN_MEMORY 0x800

/* The sections in this BFD specify a memory page.  */
#define HAS_LOAD_PAGE 0x1000

/* This BFD has been created by the linker and doesn't correspond
   to any input file.  */
#define BFD_LINKER_CREATED 0x2000

/* Symbols and relocation.  */

/* A count of carsyms (canonical archive symbols).  */
typedef unsigned long symindex;

/* How to perform a relocation.  */
typedef const struct reloc_howto_struct reloc_howto_type;

#define BFD_NO_MORE_SYMBOLS ((symindex) ~0)

/* General purpose part of a symbol X;
   target specific parts are in libcoff.h, libaout.h, etc.  */

#define bfd_get_section(x) ((x)->section)
#define bfd_get_output_section(x) ((x)->section->output_section)
#define bfd_set_section(x,y) ((x)->section) = (y)
#define bfd_asymbol_base(x) ((x)->section->vma)
#define bfd_asymbol_value(x) (bfd_asymbol_base(x) + (x)->value)
#define bfd_asymbol_name(x) ((x)->name)
/*Perhaps future: #define bfd_asymbol_bfd(x) ((x)->section->owner)*/
#define bfd_asymbol_bfd(x) ((x)->the_bfd)
#define bfd_asymbol_flavour(x) (bfd_asymbol_bfd(x)->xvec->flavour)

/* A canonical archive symbol.  */
/* This is a type pun with struct ranlib on purpose!  */
typedef struct carsym
{
  char *name;
  file_ptr file_offset;	/* Look here to find the file.  */
}
carsym;			/* To make these you call a carsymogen.  */

/* Used in generating armaps (archive tables of contents).
   Perhaps just a forward definition would do?  */
struct orl 			/* Output ranlib.  */
{
  char **name;		/* Symbol name.  */
  union
  {
    file_ptr pos;
    bfd *abfd;
  } u;			/* bfd* or file position.  */
  int namidx;		/* Index into string table.  */
};

/* Linenumber stuff.  */
typedef struct lineno_cache_entry
{
  unsigned int line_number;	/* Linenumber from start of function.  */
  union
  {
    struct bfd_symbol *sym;	/* Function name.  */
    bfd_vma offset;	    		/* Offset into section.  */
  } u;
}
alent;

/* Object and core file sections.  */

#define	align_power(addr, align)	\
  (((addr) + ((bfd_vma) 1 << (align)) - 1) & ((bfd_vma) -1 << (align)))

typedef struct bfd_section *sec_ptr;

#define bfd_get_section_name(bfd, ptr) ((ptr)->name + 0)
#define bfd_get_section_vma(bfd, ptr) ((ptr)->vma + 0)
#define bfd_get_section_lma(bfd, ptr) ((ptr)->lma + 0)
#define bfd_get_section_alignment(bfd, ptr) ((ptr)->alignment_power + 0)
#define bfd_section_name(bfd, ptr) ((ptr)->name)
#define bfd_section_size(bfd, ptr) ((ptr)->size)
#define bfd_get_section_size(ptr) ((ptr)->size)
#define bfd_section_vma(bfd, ptr) ((ptr)->vma)
#define bfd_section_lma(bfd, ptr) ((ptr)->lma)
#define bfd_section_alignment(bfd, ptr) ((ptr)->alignment_power)
#define bfd_get_section_flags(bfd, ptr) ((ptr)->flags + 0)
#define bfd_get_section_userdata(bfd, ptr) ((ptr)->userdata)

#define bfd_is_com_section(ptr) (((ptr)->flags & SEC_IS_COMMON) != 0)

#define bfd_set_section_vma(bfd, ptr, val) (((ptr)->vma = (ptr)->lma = (val)), ((ptr)->user_set_vma = TRUE), TRUE)
#define bfd_set_section_alignment(bfd, ptr, val) (((ptr)->alignment_power = (val)),TRUE)
#define bfd_set_section_userdata(bfd, ptr, val) (((ptr)->userdata = (val)),TRUE)
/* Find the address one past the end of SEC.  */
#define bfd_get_section_limit(bfd, sec) \
  (((sec)->rawsize ? (sec)->rawsize : (sec)->size) \
   / bfd_octets_per_byte (bfd))

/* Return TRUE if section has been discarded.  */
#define elf_discarded_section(sec)				\
  (!bfd_is_abs_section (sec)					\
   && bfd_is_abs_section ((sec)->output_section)		\
   && (sec)->sec_info_type != ELF_INFO_TYPE_MERGE		\
   && (sec)->sec_info_type != ELF_INFO_TYPE_JUST_SYMS)

/* Forward define.  */
struct stat;

typedef enum bfd_print_symbol
{
  bfd_print_symbol_name,
  bfd_print_symbol_more,
  bfd_print_symbol_all
} bfd_print_symbol_type;

/* Information about a symbol that nm needs.  */

typedef struct _symbol_info
{
  symvalue value;
  char type;
  const char *name;            /* Symbol name.  */
  unsigned char stab_type;     /* Stab type.  */
  char stab_other;             /* Stab other.  */
  short stab_desc;             /* Stab desc.  */
  const char *stab_name;       /* String for stab type.  */
} symbol_info;

/* Get the name of a stabs type code.  */

extern const char *bfd_get_stab_name (int);

/* Hash table routines.  There is no way to free up a hash table.  */

/* An element in the hash table.  Most uses will actually use a larger
   structure, and an instance of this will be the first field.  */

struct bfd_hash_entry
{
  /* Next entry for this hash code.  */
  struct bfd_hash_entry *next;
  /* String being hashed.  */
  const char *string;
  /* Hash code.  This is the full hash code, not the index into the
     table.  */
  unsigned long hash;
};

/* A hash table.  */

struct bfd_hash_table
{
  /* The hash array.  */
  struct bfd_hash_entry **table;
  /* A function used to create new elements in the hash table.  The
     first entry is itself a pointer to an element.  When this
     function is first invoked, this pointer will be NULL.  However,
     having the pointer permits a hierarchy of method functions to be
     built each of which calls the function in the superclass.  Thus
     each function should be written to allocate a new block of memory
     only if the argument is NULL.  */
  struct bfd_hash_entry *(*newfunc)
    (struct bfd_hash_entry *, struct bfd_hash_table *, const char *);
   /* An objalloc for this hash table.  This is a struct objalloc *,
     but we use void * to avoid requiring the inclusion of objalloc.h.  */
  void *memory;
  /* The number of slots in the hash table.  */
  unsigned int size;
  /* The number of entries in the hash table.  */
  unsigned int count;
  /* The size of elements.  */
  unsigned int entsize;
  /* If non-zero, don't grow the hash table.  */
  unsigned int frozen:1;
};

/* Initialize a hash table.  */
extern bfd_boolean bfd_hash_table_init
  (struct bfd_hash_table *,
   struct bfd_hash_entry *(*) (struct bfd_hash_entry *,
			       struct bfd_hash_table *,
			       const char *),
   unsigned int);

/* Initialize a hash table specifying a size.  */
extern bfd_boolean bfd_hash_table_init_n
  (struct bfd_hash_table *,
   struct bfd_hash_entry *(*) (struct bfd_hash_entry *,
			       struct bfd_hash_table *,
			       const char *),
   unsigned int, unsigned int);

/* Free up a hash table.  */
extern void bfd_hash_table_free
  (struct bfd_hash_table *);

/* Look up a string in a hash table.  If CREATE is TRUE, a new entry
   will be created for this string if one does not already exist.  The
   COPY argument must be TRUE if this routine should copy the string
   into newly allocated memory when adding an entry.  */
extern struct bfd_hash_entry *bfd_hash_lookup
  (struct bfd_hash_table *, const char *, bfd_boolean create,
   bfd_boolean copy);

/* Replace an entry in a hash table.  */
extern void bfd_hash_replace
  (struct bfd_hash_table *, struct bfd_hash_entry *old,
   struct bfd_hash_entry *nw);

/* Base method for creating a hash table entry.  */
extern struct bfd_hash_entry *bfd_hash_newfunc
  (struct bfd_hash_entry *, struct bfd_hash_table *, const char *);

/* Grab some space for a hash table entry.  */
extern void *bfd_hash_allocate
  (struct bfd_hash_table *, unsigned int);

/* Traverse a hash table in a random order, calling a function on each
   element.  If the function returns FALSE, the traversal stops.  The
   INFO argument is passed to the function.  */
extern void bfd_hash_traverse
  (struct bfd_hash_table *,
   bfd_boolean (*) (struct bfd_hash_entry *, void *),
   void *info);

/* Allows the default size of a hash table to be configured. New hash
   tables allocated using bfd_hash_table_init will be created with
   this size.  */
extern void bfd_hash_set_default_size (bfd_size_type);

/* This structure is used to keep track of stabs in sections
   information while linking.  */

struct stab_info
{
  /* A hash table used to hold stabs strings.  */
  struct bfd_strtab_hash *strings;
  /* The header file hash table.  */
  struct bfd_hash_table includes;
  /* The first .stabstr section.  */
  struct bfd_section *stabstr;
};

#define COFF_SWAP_TABLE (void *) &bfd_coff_std_swap_table

/* User program access to BFD facilities.  */

/* Direct I/O routines, for programs which know more about the object
   file than BFD does.  Use higher level routines if possible.  */

extern bfd_size_type bfd_bread (void *, bfd_size_type, bfd *);
extern bfd_size_type bfd_bwrite (const void *, bfd_size_type, bfd *);
extern int bfd_seek (bfd *, file_ptr, int);
extern file_ptr bfd_tell (bfd *);
extern int bfd_flush (bfd *);
extern int bfd_stat (bfd *, struct stat *);

/* Deprecated old routines.  */
#if __GNUC__
#define bfd_read(BUF, ELTSIZE, NITEMS, ABFD)				\
  (warn_deprecated ("bfd_read", __FILE__, __LINE__, __FUNCTION__),	\
   bfd_bread ((BUF), (ELTSIZE) * (NITEMS), (ABFD)))
#define bfd_write(BUF, ELTSIZE, NITEMS, ABFD)				\
  (warn_deprecated ("bfd_write", __FILE__, __LINE__, __FUNCTION__),	\
   bfd_bwrite ((BUF), (ELTSIZE) * (NITEMS), (ABFD)))
#else
#define bfd_read(BUF, ELTSIZE, NITEMS, ABFD)				\
  (warn_deprecated ("bfd_read", (const char *) 0, 0, (const char *) 0), \
   bfd_bread ((BUF), (ELTSIZE) * (NITEMS), (ABFD)))
#define bfd_write(BUF, ELTSIZE, NITEMS, ABFD)				\
  (warn_deprecated ("bfd_write", (const char *) 0, 0, (const char *) 0),\
   bfd_bwrite ((BUF), (ELTSIZE) * (NITEMS), (ABFD)))
#endif
extern void warn_deprecated (const char *, const char *, int, const char *);

/* Cast from const char * to char * so that caller can assign to
   a char * without a warning.  */
#define bfd_get_filename(abfd) ((char *) (abfd)->filename)
#define bfd_get_cacheable(abfd) ((abfd)->cacheable)
#define bfd_get_format(abfd) ((abfd)->format)
#define bfd_get_target(abfd) ((abfd)->xvec->name)
#define bfd_get_flavour(abfd) ((abfd)->xvec->flavour)
#define bfd_family_coff(abfd) \
  (bfd_get_flavour (abfd) == bfd_target_coff_flavour || \
   bfd_get_flavour (abfd) == bfd_target_xcoff_flavour)
#define bfd_big_endian(abfd) ((abfd)->xvec->byteorder == BFD_ENDIAN_BIG)
#define bfd_little_endian(abfd) ((abfd)->xvec->byteorder == BFD_ENDIAN_LITTLE)
#define bfd_header_big_endian(abfd) \
  ((abfd)->xvec->header_byteorder == BFD_ENDIAN_BIG)
#define bfd_header_little_endian(abfd) \
  ((abfd)->xvec->header_byteorder == BFD_ENDIAN_LITTLE)
#define bfd_get_file_flags(abfd) ((abfd)->flags)
#define bfd_applicable_file_flags(abfd) ((abfd)->xvec->object_flags)
#define bfd_applicable_section_flags(abfd) ((abfd)->xvec->section_flags)
#define bfd_my_archive(abfd) ((abfd)->my_archive)
#define bfd_has_map(abfd) ((abfd)->has_armap)

#define bfd_valid_reloc_types(abfd) ((abfd)->xvec->valid_reloc_types)
#define bfd_usrdata(abfd) ((abfd)->usrdata)

#define bfd_get_start_address(abfd) ((abfd)->start_address)
#define bfd_get_symcount(abfd) ((abfd)->symcount)
#define bfd_get_outsymbols(abfd) ((abfd)->outsymbols)
#define bfd_count_sections(abfd) ((abfd)->section_count)

#define bfd_get_dynamic_symcount(abfd) ((abfd)->dynsymcount)

#define bfd_get_symbol_leading_char(abfd) ((abfd)->xvec->symbol_leading_char)

#define bfd_set_cacheable(abfd,bool) (((abfd)->cacheable = bool), TRUE)

extern bfd_boolean bfd_cache_close
  (bfd *abfd);
/* NB: This declaration should match the autogenerated one in libbfd.h.  */

extern bfd_boolean bfd_cache_close_all (void);

extern bfd_boolean bfd_record_phdr
  (bfd *, unsigned long, bfd_boolean, flagword, bfd_boolean, bfd_vma,
   bfd_boolean, bfd_boolean, unsigned int, struct bfd_section **);

/* Byte swapping routines.  */

bfd_uint64_t bfd_getb64 (const void *);
bfd_uint64_t bfd_getl64 (const void *);
bfd_int64_t bfd_getb_signed_64 (const void *);
bfd_int64_t bfd_getl_signed_64 (const void *);
bfd_vma bfd_getb32 (const void *);
bfd_vma bfd_getl32 (const void *);
bfd_signed_vma bfd_getb_signed_32 (const void *);
bfd_signed_vma bfd_getl_signed_32 (const void *);
bfd_vma bfd_getb16 (const void *);
bfd_vma bfd_getl16 (const void *);
bfd_signed_vma bfd_getb_signed_16 (const void *);
bfd_signed_vma bfd_getl_signed_16 (const void *);
void bfd_putb64 (bfd_uint64_t, void *);
void bfd_putl64 (bfd_uint64_t, void *);
void bfd_putb32 (bfd_vma, void *);
void bfd_putl32 (bfd_vma, void *);
void bfd_putb16 (bfd_vma, void *);
void bfd_putl16 (bfd_vma, void *);

/* Byte swapping routines which take size and endiannes as arguments.  */

bfd_uint64_t bfd_get_bits (const void *, int, bfd_boolean);
void bfd_put_bits (bfd_uint64_t, void *, int, bfd_boolean);

extern bfd_boolean bfd_section_already_linked_table_init (void);
extern void bfd_section_already_linked_table_free (void);

/* Externally visible ECOFF routines.  */

#if defined(__STDC__) || defined(ALMOST_STDC)
struct ecoff_debug_info;
struct ecoff_debug_swap;
struct ecoff_extr;
struct bfd_symbol;
struct bfd_link_info;
struct bfd_link_hash_entry;
struct bfd_elf_version_tree;
#endif
extern bfd_vma bfd_ecoff_get_gp_value
  (bfd * abfd);
extern bfd_boolean bfd_ecoff_set_gp_value
  (bfd *abfd, bfd_vma gp_value);
extern bfd_boolean bfd_ecoff_set_regmasks
  (bfd *abfd, unsigned long gprmask, unsigned long fprmask,
   unsigned long *cprmask);
extern void *bfd_ecoff_debug_init
  (bfd *output_bfd, struct ecoff_debug_info *output_debug,
   const struct ecoff_debug_swap *output_swap, struct bfd_link_info *);
extern void bfd_ecoff_debug_free
  (void *handle, bfd *output_bfd, struct ecoff_debug_info *output_debug,
   const struct ecoff_debug_swap *output_swap, struct bfd_link_info *);
extern bfd_boolean bfd_ecoff_debug_accumulate
  (void *handle, bfd *output_bfd, struct ecoff_debug_info *output_debug,
   const struct ecoff_debug_swap *output_swap, bfd *input_bfd,
   struct ecoff_debug_info *input_debug,
   const struct ecoff_debug_swap *input_swap, struct bfd_link_info *);
extern bfd_boolean bfd_ecoff_debug_accumulate_other
  (void *handle, bfd *output_bfd, struct ecoff_debug_info *output_debug,
   const struct ecoff_debug_swap *output_swap, bfd *input_bfd,
   struct bfd_link_info *);
extern bfd_boolean bfd_ecoff_debug_externals
  (bfd *abfd, struct ecoff_debug_info *debug,
   const struct ecoff_debug_swap *swap, bfd_boolean relocatable,
   bfd_boolean (*get_extr) (struct bfd_symbol *, struct ecoff_extr *),
   void (*set_index) (struct bfd_symbol *, bfd_size_type));
extern bfd_boolean bfd_ecoff_debug_one_external
  (bfd *abfd, struct ecoff_debug_info *debug,
   const struct ecoff_debug_swap *swap, const char *name,
   struct ecoff_extr *esym);
extern bfd_size_type bfd_ecoff_debug_size
  (bfd *abfd, struct ecoff_debug_info *debug,
   const struct ecoff_debug_swap *swap);
extern bfd_boolean bfd_ecoff_write_debug
  (bfd *abfd, struct ecoff_debug_info *debug,
   const struct ecoff_debug_swap *swap, file_ptr where);
extern bfd_boolean bfd_ecoff_write_accumulated_debug
  (void *handle, bfd *abfd, struct ecoff_debug_info *debug,
   const struct ecoff_debug_swap *swap,
   struct bfd_link_info *info, file_ptr where);

/* Externally visible ELF routines.  */

struct bfd_link_needed_list
{
  struct bfd_link_needed_list *next;
  bfd *by;
  const char *name;
};

enum dynamic_lib_link_class {
  DYN_NORMAL = 0,
  DYN_AS_NEEDED = 1,
  DYN_DT_NEEDED = 2,
  DYN_NO_ADD_NEEDED = 4,
  DYN_NO_NEEDED = 8
};

enum notice_asneeded_action {
  notice_as_needed,
  notice_not_needed,
  notice_needed
};

extern bfd_boolean bfd_elf_record_link_assignment
  (bfd *, struct bfd_link_info *, const char *, bfd_boolean,
   bfd_boolean);
extern struct bfd_link_needed_list *bfd_elf_get_needed_list
  (bfd *, struct bfd_link_info *);
extern bfd_boolean bfd_elf_get_bfd_needed_list
  (bfd *, struct bfd_link_needed_list **);
extern bfd_boolean bfd_elf_size_dynamic_sections
  (bfd *, const char *, const char *, const char *, const char * const *,
   struct bfd_link_info *, struct bfd_section **,
   struct bfd_elf_version_tree *);
extern bfd_boolean bfd_elf_size_dynsym_hash_dynstr
  (bfd *, struct bfd_link_info *);
extern void bfd_elf_set_dt_needed_name
  (bfd *, const char *);
extern const char *bfd_elf_get_dt_soname
  (bfd *);
extern void bfd_elf_set_dyn_lib_class
  (bfd *, enum dynamic_lib_link_class);
extern int bfd_elf_get_dyn_lib_class
  (bfd *);
extern struct bfd_link_needed_list *bfd_elf_get_runpath_list
  (bfd *, struct bfd_link_info *);
extern bfd_boolean bfd_elf_discard_info
  (bfd *, struct bfd_link_info *);
extern unsigned int _bfd_elf_default_action_discarded
  (struct bfd_section *);

/* Return an upper bound on the number of bytes required to store a
   copy of ABFD's program header table entries.  Return -1 if an error
   occurs; bfd_get_error will return an appropriate code.  */
extern long bfd_get_elf_phdr_upper_bound
  (bfd *abfd);

/* Copy ABFD's program header table entries to *PHDRS.  The entries
   will be stored as an array of Elf_Internal_Phdr structures, as
   defined in include/elf/internal.h.  To find out how large the
   buffer needs to be, call bfd_get_elf_phdr_upper_bound.

   Return the number of program header table entries read, or -1 if an
   error occurs; bfd_get_error will return an appropriate code.  */
extern int bfd_get_elf_phdrs
  (bfd *abfd, void *phdrs);

/* Create a new BFD as if by bfd_openr.  Rather than opening a file,
   reconstruct an ELF file by reading the segments out of remote memory
   based on the ELF file header at EHDR_VMA and the ELF program headers it
   points to.  If not null, *LOADBASEP is filled in with the difference
   between the VMAs from which the segments were read, and the VMAs the
   file headers (and hence BFD's idea of each section's VMA) put them at.

   The function TARGET_READ_MEMORY is called to copy LEN bytes from the
   remote memory at target address VMA into the local buffer at MYADDR; it
   should return zero on success or an `errno' code on failure.  TEMPL must
   be a BFD for an ELF target with the word size and byte order found in
   the remote memory.  */
extern bfd *bfd_elf_bfd_from_remote_memory
  (bfd *templ, bfd_vma ehdr_vma, bfd_vma *loadbasep,
   int (*target_read_memory) (bfd_vma vma, bfd_byte *myaddr, int len));

/* Return the arch_size field of an elf bfd, or -1 if not elf.  */
extern int bfd_get_arch_size
  (bfd *);

/* Return TRUE if address "naturally" sign extends, or -1 if not elf.  */
extern int bfd_get_sign_extend_vma
  (bfd *);

extern struct bfd_section *_bfd_elf_tls_setup
  (bfd *, struct bfd_link_info *);

extern void _bfd_fix_excluded_sec_syms
  (bfd *, struct bfd_link_info *);

extern unsigned bfd_m68k_mach_to_features (int);

extern int bfd_m68k_features_to_mach (unsigned);

extern bfd_boolean bfd_m68k_elf32_create_embedded_relocs
  (bfd *, struct bfd_link_info *, struct bfd_section *, struct bfd_section *,
   char **);

extern bfd_boolean bfd_bfin_elf32_create_embedded_relocs
  (bfd *, struct bfd_link_info *, struct bfd_section *, struct bfd_section *,
   char **);

/* SunOS shared library support routines for the linker.  */

extern struct bfd_link_needed_list *bfd_sunos_get_needed_list
  (bfd *, struct bfd_link_info *);
extern bfd_boolean bfd_sunos_record_link_assignment
  (bfd *, struct bfd_link_info *, const char *);
extern bfd_boolean bfd_sunos_size_dynamic_sections
  (bfd *, struct bfd_link_info *, struct bfd_section **,
   struct bfd_section **, struct bfd_section **);

/* Linux shared library support routines for the linker.  */

extern bfd_boolean bfd_i386linux_size_dynamic_sections
  (bfd *, struct bfd_link_info *);
extern bfd_boolean bfd_m68klinux_size_dynamic_sections
  (bfd *, struct bfd_link_info *);
extern bfd_boolean bfd_sparclinux_size_dynamic_sections
  (bfd *, struct bfd_link_info *);

/* mmap hacks */

struct _bfd_window_internal;
typedef struct _bfd_window_internal bfd_window_internal;

typedef struct _bfd_window
{
  /* What the user asked for.  */
  void *data;
  bfd_size_type size;
  /* The actual window used by BFD.  Small user-requested read-only
     regions sharing a page may share a single window into the object
     file.  Read-write versions shouldn't until I've fixed things to
     keep track of which portions have been claimed by the
     application; don't want to give the same region back when the
     application wants two writable copies!  */
  struct _bfd_window_internal *i;
}
bfd_window;

extern void bfd_init_window
  (bfd_window *);
extern void bfd_free_window
  (bfd_window *);
extern bfd_boolean bfd_get_file_window
  (bfd *, file_ptr, bfd_size_type, bfd_window *, bfd_boolean);

/* XCOFF support routines for the linker.  */

extern bfd_boolean bfd_xcoff_link_record_set
  (bfd *, struct bfd_link_info *, struct bfd_link_hash_entry *, bfd_size_type);
extern bfd_boolean bfd_xcoff_import_symbol
  (bfd *, struct bfd_link_info *, struct bfd_link_hash_entry *, bfd_vma,
   const char *, const char *, const char *, unsigned int);
extern bfd_boolean bfd_xcoff_export_symbol
  (bfd *, struct bfd_link_info *, struct bfd_link_hash_entry *);
extern bfd_boolean bfd_xcoff_link_count_reloc
  (bfd *, struct bfd_link_info *, const char *);
extern bfd_boolean bfd_xcoff_record_link_assignment
  (bfd *, struct bfd_link_info *, const char *);
extern bfd_boolean bfd_xcoff_size_dynamic_sections
  (bfd *, struct bfd_link_info *, const char *, const char *,
   unsigned long, unsigned long, unsigned long, bfd_boolean,
   int, bfd_boolean, bfd_boolean, struct bfd_section **, bfd_boolean);
extern bfd_boolean bfd_xcoff_link_generate_rtinit
  (bfd *, const char *, const char *, bfd_boolean);

/* XCOFF support routines for ar.  */
extern bfd_boolean bfd_xcoff_ar_archive_set_magic
  (bfd *, char *);

/* Externally visible COFF routines.  */

#if defined(__STDC__) || defined(ALMOST_STDC)
struct internal_syment;
union internal_auxent;
#endif

extern bfd_boolean bfd_coff_get_syment
  (bfd *, struct bfd_symbol *, struct internal_syment *);

extern bfd_boolean bfd_coff_get_auxent
  (bfd *, struct bfd_symbol *, int, union internal_auxent *);

extern bfd_boolean bfd_coff_set_symbol_class
  (bfd *, struct bfd_symbol *, unsigned int);

extern bfd_boolean bfd_m68k_coff_create_embedded_relocs
  (bfd *, struct bfd_link_info *, struct bfd_section *, struct bfd_section *, char **);

/* ARM VFP11 erratum workaround support.  */
typedef enum
{
  BFD_ARM_VFP11_FIX_DEFAULT,
  BFD_ARM_VFP11_FIX_NONE,
  BFD_ARM_VFP11_FIX_SCALAR,
  BFD_ARM_VFP11_FIX_VECTOR
} bfd_arm_vfp11_fix;

extern void bfd_elf32_arm_init_maps
  (bfd *);

extern void bfd_elf32_arm_set_vfp11_fix
  (bfd *, struct bfd_link_info *);

extern bfd_boolean bfd_elf32_arm_vfp11_erratum_scan
  (bfd *, struct bfd_link_info *);

extern void bfd_elf32_arm_vfp11_fix_veneer_locations
  (bfd *, struct bfd_link_info *);

/* ARM Interworking support.  Called from linker.  */
extern bfd_boolean bfd_arm_allocate_interworking_sections
  (struct bfd_link_info *);

extern bfd_boolean bfd_arm_process_before_allocation
  (bfd *, struct bfd_link_info *, int);

extern bfd_boolean bfd_arm_get_bfd_for_interworking
  (bfd *, struct bfd_link_info *);

/* PE ARM Interworking support.  Called from linker.  */
extern bfd_boolean bfd_arm_pe_allocate_interworking_sections
  (struct bfd_link_info *);

extern bfd_boolean bfd_arm_pe_process_before_allocation
  (bfd *, struct bfd_link_info *, int);

extern bfd_boolean bfd_arm_pe_get_bfd_for_interworking
  (bfd *, struct bfd_link_info *);

/* ELF ARM Interworking support.  Called from linker.  */
extern bfd_boolean bfd_elf32_arm_allocate_interworking_sections
  (struct bfd_link_info *);

extern bfd_boolean bfd_elf32_arm_process_before_allocation
  (bfd *, struct bfd_link_info *);

void bfd_elf32_arm_set_target_relocs
  (bfd *, struct bfd_link_info *, int, char *, int, int, bfd_arm_vfp11_fix,
   int, int);

extern bfd_boolean bfd_elf32_arm_get_bfd_for_interworking
  (bfd *, struct bfd_link_info *);

extern bfd_boolean bfd_elf32_arm_add_glue_sections_to_bfd
  (bfd *, struct bfd_link_info *);

/* ELF ARM mapping symbol support */
#define BFD_ARM_SPECIAL_SYM_TYPE_MAP	(1 << 0)
#define BFD_ARM_SPECIAL_SYM_TYPE_TAG	(1 << 1)
#define BFD_ARM_SPECIAL_SYM_TYPE_OTHER  (1 << 2)
#define BFD_ARM_SPECIAL_SYM_TYPE_ANY	(~0)
extern bfd_boolean bfd_is_arm_special_symbol_name
  (const char * name, int type);

extern void bfd_elf32_arm_set_byteswap_code (struct bfd_link_info *, int);

/* ARM Note section processing.  */
extern bfd_boolean bfd_arm_merge_machines
  (bfd *, bfd *);

extern bfd_boolean bfd_arm_update_notes
  (bfd *, const char *);

extern unsigned int bfd_arm_get_mach_from_notes
  (bfd *, const char *);

/* TI COFF load page support.  */
extern void bfd_ticoff_set_section_load_page
  (struct bfd_section *, int);

extern int bfd_ticoff_get_section_load_page
  (struct bfd_section *);

/* H8/300 functions.  */
extern bfd_vma bfd_h8300_pad_address
  (bfd *, bfd_vma);

/* IA64 Itanium code generation.  Called from linker.  */
extern void bfd_elf32_ia64_after_parse
  (int);

extern void bfd_elf64_ia64_after_parse
  (int);

/* This structure is used for a comdat section, as in PE.  A comdat
   section is associated with a particular symbol.  When the linker
   sees a comdat section, it keeps only one of the sections with a
   given name and associated with a given symbol.  */

struct coff_comdat_info
{
  /* The name of the symbol associated with a comdat section.  */
  const char *name;

  /* The local symbol table index of the symbol associated with a
     comdat section.  This is only meaningful to the object file format
     specific code; it is not an index into the list returned by
     bfd_canonicalize_symtab.  */
  long symbol;
};

extern struct coff_comdat_info *bfd_coff_get_comdat_section
  (bfd *, struct bfd_section *);

/* Extracted from init.c.  */
void bfd_init (void);

/* Extracted from opncls.c.  */
bfd *bfd_fopen (const char *filename, const char *target,
    const char *mode, int fd);

bfd *bfd_openr (const char *filename, const char *target);

bfd *bfd_fdopenr (const char *filename, const char *target, int fd);

bfd *bfd_openstreamr (const char *, const char *, void *);

bfd *bfd_openr_iovec (const char *filename, const char *target,
    void *(*open) (struct bfd *nbfd,
    void *open_closure),
    void *open_closure,
    file_ptr (*pread) (struct bfd *nbfd,
    void *stream,
    void *buf,
    file_ptr nbytes,
    file_ptr offset),
    int (*close) (struct bfd *nbfd,
    void *stream),
    int (*stat) (struct bfd *abfd,
    void *stream,
    struct stat *sb));

bfd *bfd_openw (const char *filename, const char *target);

bfd_boolean bfd_close (bfd *abfd);

bfd_boolean bfd_close_all_done (bfd *);

bfd *bfd_create (const char *filename, bfd *templ);

bfd_boolean bfd_make_writable (bfd *abfd);

bfd_boolean bfd_make_readable (bfd *abfd);

unsigned long bfd_calc_gnu_debuglink_crc32
   (unsigned long crc, const unsigned char *buf, bfd_size_type len);

char *bfd_follow_gnu_debuglink (bfd *abfd, const char *dir);

struct bfd_section *bfd_create_gnu_debuglink_section
   (bfd *abfd, const char *filename);

bfd_boolean bfd_fill_in_gnu_debuglink_section
   (bfd *abfd, struct bfd_section *sect, const char *filename);

/* Extracted from libbfd.c.  */

/* Byte swapping macros for user section data.  */

#define bfd_put_8(abfd, val, ptr) \
  ((void) (*((unsigned char *) (ptr)) = (val) & 0xff))
#define bfd_put_signed_8 \
  bfd_put_8
#define bfd_get_8(abfd, ptr) \
  (*(unsigned char *) (ptr) & 0xff)
#define bfd_get_signed_8(abfd, ptr) \
  (((*(unsigned char *) (ptr) & 0xff) ^ 0x80) - 0x80)

#define bfd_put_16(abfd, val, ptr) \
  BFD_SEND (abfd, bfd_putx16, ((val),(ptr)))
#define bfd_put_signed_16 \
  bfd_put_16
#define bfd_get_16(abfd, ptr) \
  BFD_SEND (abfd, bfd_getx16, (ptr))
#define bfd_get_signed_16(abfd, ptr) \
  BFD_SEND (abfd, bfd_getx_signed_16, (ptr))

#define bfd_put_32(abfd, val, ptr) \
  BFD_SEND (abfd, bfd_putx32, ((val),(ptr)))
#define bfd_put_signed_32 \
  bfd_put_32
#define bfd_get_32(abfd, ptr) \
  BFD_SEND (abfd, bfd_getx32, (ptr))
#define bfd_get_signed_32(abfd, ptr) \
  BFD_SEND (abfd, bfd_getx_signed_32, (ptr))

#define bfd_put_64(abfd, val, ptr) \
  BFD_SEND (abfd, bfd_putx64, ((val), (ptr)))
#define bfd_put_signed_64 \
  bfd_put_64
#define bfd_get_64(abfd, ptr) \
  BFD_SEND (abfd, bfd_getx64, (ptr))
#define bfd_get_signed_64(abfd, ptr) \
  BFD_SEND (abfd, bfd_getx_signed_64, (ptr))

#define bfd_get(bits, abfd, ptr)                       \
  ((bits) == 8 ? (bfd_vma) bfd_get_8 (abfd, ptr)       \
   : (bits) == 16 ? bfd_get_16 (abfd, ptr)             \
   : (bits) == 32 ? bfd_get_32 (abfd, ptr)             \
   : (bits) == 64 ? bfd_get_64 (abfd, ptr)             \
   : (abort (), (bfd_vma) - 1))

#define bfd_put(bits, abfd, val, ptr)                  \
  ((bits) == 8 ? bfd_put_8  (abfd, val, ptr)           \
   : (bits) == 16 ? bfd_put_16 (abfd, val, ptr)                \
   : (bits) == 32 ? bfd_put_32 (abfd, val, ptr)                \
   : (bits) == 64 ? bfd_put_64 (abfd, val, ptr)                \
   : (abort (), (void) 0))


/* Byte swapping macros for file header data.  */

#define bfd_h_put_8(abfd, val, ptr) \
  bfd_put_8 (abfd, val, ptr)
#define bfd_h_put_signed_8(abfd, val, ptr) \
  bfd_put_8 (abfd, val, ptr)
#define bfd_h_get_8(abfd, ptr) \
  bfd_get_8 (abfd, ptr)
#define bfd_h_get_signed_8(abfd, ptr) \
  bfd_get_signed_8 (abfd, ptr)

#define bfd_h_put_16(abfd, val, ptr) \
  BFD_SEND (abfd, bfd_h_putx16, (val, ptr))
#define bfd_h_put_signed_16 \
  bfd_h_put_16
#define bfd_h_get_16(abfd, ptr) \
  BFD_SEND (abfd, bfd_h_getx16, (ptr))
#define bfd_h_get_signed_16(abfd, ptr) \
  BFD_SEND (abfd, bfd_h_getx_signed_16, (ptr))

#define bfd_h_put_32(abfd, val, ptr) \
  BFD_SEND (abfd, bfd_h_putx32, (val, ptr))
#define bfd_h_put_signed_32 \
  bfd_h_put_32
#define bfd_h_get_32(abfd, ptr) \
  BFD_SEND (abfd, bfd_h_getx32, (ptr))
#define bfd_h_get_signed_32(abfd, ptr) \
  BFD_SEND (abfd, bfd_h_getx_signed_32, (ptr))

#define bfd_h_put_64(abfd, val, ptr) \
  BFD_SEND (abfd, bfd_h_putx64, (val, ptr))
#define bfd_h_put_signed_64 \
  bfd_h_put_64
#define bfd_h_get_64(abfd, ptr) \
  BFD_SEND (abfd, bfd_h_getx64, (ptr))
#define bfd_h_get_signed_64(abfd, ptr) \
  BFD_SEND (abfd, bfd_h_getx_signed_64, (ptr))

/* Aliases for the above, which should eventually go away.  */

#define H_PUT_64  bfd_h_put_64
#define H_PUT_32  bfd_h_put_32
#define H_PUT_16  bfd_h_put_16
#define H_PUT_8   bfd_h_put_8
#define H_PUT_S64 bfd_h_put_signed_64
#define H_PUT_S32 bfd_h_put_signed_32
#define H_PUT_S16 bfd_h_put_signed_16
#define H_PUT_S8  bfd_h_put_signed_8
#define H_GET_64  bfd_h_get_64
#define H_GET_32  bfd_h_get_32
#define H_GET_16  bfd_h_get_16
#define H_GET_8   bfd_h_get_8
#define H_GET_S64 bfd_h_get_signed_64
#define H_GET_S32 bfd_h_get_signed_32
#define H_GET_S16 bfd_h_get_signed_16
#define H_GET_S8  bfd_h_get_signed_8


/* Extracted from bfdio.c.  */
long bfd_get_mtime (bfd *abfd);

file_ptr bfd_get_size (bfd *abfd);

/* Extracted from bfdwin.c.  */
/* Extracted from section.c.  */
typedef struct bfd_section
{
  /* The name of the section; the name isn't a copy, the pointer is
     the same as that passed to bfd_make_section.  */
  const char *name;

  /* A unique sequence number.  */
  int id;

  /* Which section in the bfd; 0..n-1 as sections are created in a bfd.  */
  int index;

  /* The next section in the list belonging to the BFD, or NULL.  */
  struct bfd_section *next;

  /* The previous section in the list belonging to the BFD, or NULL.  */
  struct bfd_section *prev;

  /* The field flags contains attributes of the section. Some
     flags are read in from the object file, and some are
     synthesized from other information.  */
  flagword flags;

#define SEC_NO_FLAGS   0x000

  /* Tells the OS to allocate space for this section when loading.
     This is clear for a section containing debug information only.  */
#define SEC_ALLOC      0x001

  /* Tells the OS to load the section from the file when loading.
     This is clear for a .bss section.  */
#define SEC_LOAD       0x002

  /* The section contains data still to be relocated, so there is
     some relocation information too.  */
#define SEC_RELOC      0x004

  /* A signal to the OS that the section contains read only data.  */
#define SEC_READONLY   0x008

  /* The section contains code only.  */
#define SEC_CODE       0x010

  /* The section contains data only.  */
#define SEC_DATA       0x020

  /* The section will reside in ROM.  */
#define SEC_ROM        0x040

  /* The section contains constructor information. This section
     type is used by the linker to create lists of constructors and
     destructors used by <<g++>>. When a back end sees a symbol
     which should be used in a constructor list, it creates a new
     section for the type of name (e.g., <<__CTOR_LIST__>>), attaches
     the symbol to it, and builds a relocation. To build the lists
     of constructors, all the linker has to do is catenate all the
     sections called <<__CTOR_LIST__>> and relocate the data
     contained within - exactly the operations it would peform on
     standard data.  */
#define SEC_CONSTRUCTOR 0x080

  /* The section has contents - a data section could be
     <<SEC_ALLOC>> | <<SEC_HAS_CONTENTS>>; a debug section could be
     <<SEC_HAS_CONTENTS>>  */
#define SEC_HAS_CONTENTS 0x100

  /* An instruction to the linker to not output the section
     even if it has information which would normally be written.  */
#define SEC_NEVER_LOAD 0x200

  /* The section contains thread local data.  */
#define SEC_THREAD_LOCAL 0x400

  /* The section has GOT references.  This flag is only for the
     linker, and is currently only used by the elf32-hppa back end.
     It will be set if global offset table references were detected
     in this section, which indicate to the linker that the section
     contains PIC code, and must be handled specially when doing a
     static link.  */
#define SEC_HAS_GOT_REF 0x800

  /* The section contains common symbols (symbols may be defined
     multiple times, the value of a symbol is the amount of
     space it requires, and the largest symbol value is the one
     used).  Most targets have exactly one of these (which we
     translate to bfd_com_section_ptr), but ECOFF has two.  */
#define SEC_IS_COMMON 0x1000

  /* The section contains only debugging information.  For
     example, this is set for ELF .debug and .stab sections.
     strip tests this flag to see if a section can be
     discarded.  */
#define SEC_DEBUGGING 0x2000

  /* The contents of this section are held in memory pointed to
     by the contents field.  This is checked by bfd_get_section_contents,
     and the data is retrieved from memory if appropriate.  */
#define SEC_IN_MEMORY 0x4000

  /* The contents of this section are to be excluded by the
     linker for executable and shared objects unless those
     objects are to be further relocated.  */
#define SEC_EXCLUDE 0x8000

  /* The contents of this section are to be sorted based on the sum of
     the symbol and addend values specified by the associated relocation
     entries.  Entries without associated relocation entries will be
     appended to the end of the section in an unspecified order.  */
#define SEC_SORT_ENTRIES 0x10000

  /* When linking, duplicate sections of the same name should be
     discarded, rather than being combined into a single section as
     is usually done.  This is similar to how common symbols are
     handled.  See SEC_LINK_DUPLICATES below.  */
#define SEC_LINK_ONCE 0x20000

  /* If SEC_LINK_ONCE is set, this bitfield describes how the linker
     should handle duplicate sections.  */
#define SEC_LINK_DUPLICATES 0x40000

  /* This value for SEC_LINK_DUPLICATES means that duplicate
     sections with the same name should simply be discarded.  */
#define SEC_LINK_DUPLICATES_DISCARD 0x0

  /* This value for SEC_LINK_DUPLICATES means that the linker
     should warn if there are any duplicate sections, although
     it should still only link one copy.  */
#define SEC_LINK_DUPLICATES_ONE_ONLY 0x80000

  /* This value for SEC_LINK_DUPLICATES means that the linker
     should warn if any duplicate sections are a different size.  */
#define SEC_LINK_DUPLICATES_SAME_SIZE 0x100000

  /* This value for SEC_LINK_DUPLICATES means that the linker
     should warn if any duplicate sections contain different
     contents.  */
#define SEC_LINK_DUPLICATES_SAME_CONTENTS \
  (SEC_LINK_DUPLICATES_ONE_ONLY | SEC_LINK_DUPLICATES_SAME_SIZE)

  /* This section was created by the linker as part of dynamic
     relocation or other arcane processing.  It is skipped when
     going through the first-pass output, trusting that someone
     else up the line will take care of it later.  */
#define SEC_LINKER_CREATED 0x200000

  /* This section should not be subject to garbage collection.
     Also set to inform the linker that this section should not be
     listed in the link map as discarded.  */
#define SEC_KEEP 0x400000

  /* This section contains "short" data, and should be placed
     "near" the GP.  */
#define SEC_SMALL_DATA 0x800000

  /* Attempt to merge identical entities in the section.
     Entity size is given in the entsize field.  */
#define SEC_MERGE 0x1000000

  /* If given with SEC_MERGE, entities to merge are zero terminated
     strings where entsize specifies character size instead of fixed
     size entries.  */
#define SEC_STRINGS 0x2000000

  /* This section contains data about section groups.  */
#define SEC_GROUP 0x4000000

  /* The section is a COFF shared library section.  This flag is
     only for the linker.  If this type of section appears in
     the input file, the linker must copy it to the output file
     without changing the vma or size.  FIXME: Although this
     was originally intended to be general, it really is COFF
     specific (and the flag was renamed to indicate this).  It
     might be cleaner to have some more general mechanism to
     allow the back end to control what the linker does with
     sections.  */
#define SEC_COFF_SHARED_LIBRARY 0x10000000

  /* This section contains data which may be shared with other
     executables or shared objects. This is for COFF only.  */
#define SEC_COFF_SHARED 0x20000000

  /* When a section with this flag is being linked, then if the size of
     the input section is less than a page, it should not cross a page
     boundary.  If the size of the input section is one page or more,
     it should be aligned on a page boundary.  This is for TI
     TMS320C54X only.  */
#define SEC_TIC54X_BLOCK 0x40000000

  /* Conditionally link this section; do not link if there are no
     references found to any symbol in the section.  This is for TI
     TMS320C54X only.  */
#define SEC_TIC54X_CLINK 0x80000000

  /*  End of section flags.  */

  /* Some internal packed boolean fields.  */

  /* See the vma field.  */
  unsigned int user_set_vma : 1;

  /* A mark flag used by some of the linker backends.  */
  unsigned int linker_mark : 1;

  /* Another mark flag used by some of the linker backends.  Set for
     output sections that have an input section.  */
  unsigned int linker_has_input : 1;

  /* Mark flags used by some linker backends for garbage collection.  */
  unsigned int gc_mark : 1;
  unsigned int gc_mark_from_eh : 1;

  /* The following flags are used by the ELF linker. */

  /* Mark sections which have been allocated to segments.  */
  unsigned int segment_mark : 1;

  /* Type of sec_info information.  */
  unsigned int sec_info_type:3;
#define ELF_INFO_TYPE_NONE      0
#define ELF_INFO_TYPE_STABS     1
#define ELF_INFO_TYPE_MERGE     2
#define ELF_INFO_TYPE_EH_FRAME  3
#define ELF_INFO_TYPE_JUST_SYMS 4

  /* Nonzero if this section uses RELA relocations, rather than REL.  */
  unsigned int use_rela_p:1;

  /* Bits used by various backends.  The generic code doesn't touch
     these fields.  */

  /* Nonzero if this section has TLS related relocations.  */
  unsigned int has_tls_reloc:1;

  /* Nonzero if this section has a call to __tls_get_addr.  */
  unsigned int has_tls_get_addr_call:1;

  /* Nonzero if this section has a gp reloc.  */
  unsigned int has_gp_reloc:1;

  /* Nonzero if this section needs the relax finalize pass.  */
  unsigned int need_finalize_relax:1;

  /* Whether relocations have been processed.  */
  unsigned int reloc_done : 1;

  /* End of internal packed boolean fields.  */

  /*  The virtual memory address of the section - where it will be
      at run time.  The symbols are relocated against this.  The
      user_set_vma flag is maintained by bfd; if it's not set, the
      backend can assign addresses (for example, in <<a.out>>, where
      the default address for <<.data>> is dependent on the specific
      target and various flags).  */
  bfd_vma vma;

  /*  The load address of the section - where it would be in a
      rom image; really only used for writing section header
      information.  */
  bfd_vma lma;

  /* The size of the section in octets, as it will be output.
     Contains a value even if the section has no contents (e.g., the
     size of <<.bss>>).  */
  bfd_size_type size;

  /* For input sections, the original size on disk of the section, in
     octets.  This field is used by the linker relaxation code.  It is
     currently only set for sections where the linker relaxation scheme
     doesn't cache altered section and reloc contents (stabs, eh_frame,
     SEC_MERGE, some coff relaxing targets), and thus the original size
     needs to be kept to read the section multiple times.
     For output sections, rawsize holds the section size calculated on
     a previous linker relaxation pass.  */
  bfd_size_type rawsize;

  /* If this section is going to be output, then this value is the
     offset in *bytes* into the output section of the first byte in the
     input section (byte ==> smallest addressable unit on the
     target).  In most cases, if this was going to start at the
     100th octet (8-bit quantity) in the output section, this value
     would be 100.  However, if the target byte size is 16 bits
     (bfd_octets_per_byte is "2"), this value would be 50.  */
  bfd_vma output_offset;

  /* The output section through which to map on output.  */
  struct bfd_section *output_section;

  /* The alignment requirement of the section, as an exponent of 2 -
     e.g., 3 aligns to 2^3 (or 8).  */
  unsigned int alignment_power;

  /* If an input section, a pointer to a vector of relocation
     records for the data in this section.  */
  struct reloc_cache_entry *relocation;

  /* If an output section, a pointer to a vector of pointers to
     relocation records for the data in this section.  */
  struct reloc_cache_entry **orelocation;

  /* The number of relocation records in one of the above.  */
  unsigned reloc_count;

  /* Information below is back end specific - and not always used
     or updated.  */

  /* File position of section data.  */
  file_ptr filepos;

  /* File position of relocation info.  */
  file_ptr rel_filepos;

  /* File position of line data.  */
  file_ptr line_filepos;

  /* Pointer to data for applications.  */
  void *userdata;

  /* If the SEC_IN_MEMORY flag is set, this points to the actual
     contents.  */
  unsigned char *contents;

  /* Attached line number information.  */
  alent *lineno;

  /* Number of line number records.  */
  unsigned int lineno_count;

  /* Entity size for merging purposes.  */
  unsigned int entsize;

  /* Points to the kept section if this section is a link-once section,
     and is discarded.  */
  struct bfd_section *kept_section;

  /* When a section is being output, this value changes as more
     linenumbers are written out.  */
  file_ptr moving_line_filepos;

  /* What the section number is in the target world.  */
  int target_index;

  void *used_by_bfd;

  /* If this is a constructor section then here is a list of the
     relocations created to relocate items within it.  */
  struct relent_chain *constructor_chain;

  /* The BFD which owns the section.  */
  bfd *owner;

  /* A symbol which points at this section only.  */
  struct bfd_symbol *symbol;
  struct bfd_symbol **symbol_ptr_ptr;

  /* Early in the link process, map_head and map_tail are used to build
     a list of input sections attached to an output section.  Later,
     output sections use these fields for a list of bfd_link_order
     structs.  */
  union {
    struct bfd_link_order *link_order;
    struct bfd_section *s;
  } map_head, map_tail;
} asection;

/* These sections are global, and are managed by BFD.  The application
   and target back end are not permitted to change the values in
   these sections.  New code should use the section_ptr macros rather
   than referring directly to the const sections.  The const sections
   may eventually vanish.  */
#define BFD_ABS_SECTION_NAME "*ABS*"
#define BFD_UND_SECTION_NAME "*UND*"
#define BFD_COM_SECTION_NAME "*COM*"
#define BFD_IND_SECTION_NAME "*IND*"

/* The absolute section.  */
extern asection bfd_abs_section;
#define bfd_abs_section_ptr ((asection *) &bfd_abs_section)
#define bfd_is_abs_section(sec) ((sec) == bfd_abs_section_ptr)
/* Pointer to the undefined section.  */
extern asection bfd_und_section;
#define bfd_und_section_ptr ((asection *) &bfd_und_section)
#define bfd_is_und_section(sec) ((sec) == bfd_und_section_ptr)
/* Pointer to the common section.  */
extern asection bfd_com_section;
#define bfd_com_section_ptr ((asection *) &bfd_com_section)
/* Pointer to the indirect section.  */
extern asection bfd_ind_section;
#define bfd_ind_section_ptr ((asection *) &bfd_ind_section)
#define bfd_is_ind_section(sec) ((sec) == bfd_ind_section_ptr)

#define bfd_is_const_section(SEC)              \
 (   ((SEC) == bfd_abs_section_ptr)            \
  || ((SEC) == bfd_und_section_ptr)            \
  || ((SEC) == bfd_com_section_ptr)            \
  || ((SEC) == bfd_ind_section_ptr))

/* Macros to handle insertion and deletion of a bfd's sections.  These
   only handle the list pointers, ie. do not adjust section_count,
   target_index etc.  */
#define bfd_section_list_remove(ABFD, S) \
  do                                                   \
    {                                                  \
      asection *_s = S;                                \
      asection *_next = _s->next;                      \
      asection *_prev = _s->prev;                      \
      if (_prev)                                       \
        _prev->next = _next;                           \
      else                                             \
        (ABFD)->sections = _next;                      \
      if (_next)                                       \
        _next->prev = _prev;                           \
      else                                             \
        (ABFD)->section_last = _prev;                  \
    }                                                  \
  while (0)
#define bfd_section_list_append(ABFD, S) \
  do                                                   \
    {                                                  \
      asection *_s = S;                                \
      bfd *_abfd = ABFD;                               \
      _s->next = NULL;                                 \
      if (_abfd->section_last)                         \
        {                                              \
          _s->prev = _abfd->section_last;              \
          _abfd->section_last->next = _s;              \
        }                                              \
      else                                             \
        {                                              \
          _s->prev = NULL;                             \
          _abfd->sections = _s;                        \
        }                                              \
      _abfd->section_last = _s;                        \
    }                                                  \
  while (0)
#define bfd_section_list_prepend(ABFD, S) \
  do                                                   \
    {                                                  \
      asection *_s = S;                                \
      bfd *_abfd = ABFD;                               \
      _s->prev = NULL;                                 \
      if (_abfd->sections)                             \
        {                                              \
          _s->next = _abfd->sections;                  \
          _abfd->sections->prev = _s;                  \
        }                                              \
      else                                             \
        {                                              \
          _s->next = NULL;                             \
          _abfd->section_last = _s;                    \
        }                                              \
      _abfd->sections = _s;                            \
    }                                                  \
  while (0)
#define bfd_section_list_insert_after(ABFD, A, S) \
  do                                                   \
    {                                                  \
      asection *_a = A;                                \
      asection *_s = S;                                \
      asection *_next = _a->next;                      \
      _s->next = _next;                                \
      _s->prev = _a;                                   \
      _a->next = _s;                                   \
      if (_next)                                       \
        _next->prev = _s;                              \
      else                                             \
        (ABFD)->section_last = _s;                     \
    }                                                  \
  while (0)
#define bfd_section_list_insert_before(ABFD, B, S) \
  do                                                   \
    {                                                  \
      asection *_b = B;                                \
      asection *_s = S;                                \
      asection *_prev = _b->prev;                      \
      _s->prev = _prev;                                \
      _s->next = _b;                                   \
      _b->prev = _s;                                   \
      if (_prev)                                       \
        _prev->next = _s;                              \
      else                                             \
        (ABFD)->sections = _s;                         \
    }                                                  \
  while (0)
#define bfd_section_removed_from_list(ABFD, S) \
  ((S)->next == NULL ? (ABFD)->section_last != (S) : (S)->next->prev != (S))

#define BFD_FAKE_SECTION(SEC, FLAGS, SYM, NAME, IDX)                   \
  /* name, id,  index, next, prev, flags, user_set_vma,            */  \
  { NAME,  IDX, 0,     NULL, NULL, FLAGS, 0,                           \
                                                                       \
  /* linker_mark, linker_has_input, gc_mark, gc_mark_from_eh,      */  \
     0,           0,                1,       0,                        \
                                                                       \
  /* segment_mark, sec_info_type, use_rela_p, has_tls_reloc,       */  \
     0,            0,             0,          0,                       \
                                                                       \
  /* has_tls_get_addr_call, has_gp_reloc, need_finalize_relax,     */  \
     0,                     0,            0,                           \
                                                                       \
  /* reloc_done, vma, lma, size, rawsize                           */  \
     0,          0,   0,   0,    0,                                    \
                                                                       \
  /* output_offset, output_section,              alignment_power,  */  \
     0,             (struct bfd_section *) &SEC, 0,                    \
                                                                       \
  /* relocation, orelocation, reloc_count, filepos, rel_filepos,   */  \
     NULL,       NULL,        0,           0,       0,                 \
                                                                       \
  /* line_filepos, userdata, contents, lineno, lineno_count,       */  \
     0,            NULL,     NULL,     NULL,   0,                      \
                                                                       \
  /* entsize, kept_section, moving_line_filepos,                    */ \
     0,       NULL,          0,                                        \
                                                                       \
  /* target_index, used_by_bfd, constructor_chain, owner,          */  \
     0,            NULL,        NULL,              NULL,               \
                                                                       \
  /* symbol,                    symbol_ptr_ptr,                    */  \
     (struct bfd_symbol *) SYM, &SEC.symbol,                           \
                                                                       \
  /* map_head, map_tail                                            */  \
     { NULL }, { NULL }                                                \
    }

void bfd_section_list_clear (bfd *);

asection *bfd_get_section_by_name (bfd *abfd, const char *name);

asection *bfd_get_section_by_name_if
   (bfd *abfd,
    const char *name,
    bfd_boolean (*func) (bfd *abfd, asection *sect, void *obj),
    void *obj);

char *bfd_get_unique_section_name
   (bfd *abfd, const char *templat, int *count);

asection *bfd_make_section_old_way (bfd *abfd, const char *name);

asection *bfd_make_section_anyway_with_flags
   (bfd *abfd, const char *name, flagword flags);

asection *bfd_make_section_anyway (bfd *abfd, const char *name);

asection *bfd_make_section_with_flags
   (bfd *, const char *name, flagword flags);

asection *bfd_make_section (bfd *, const char *name);

bfd_boolean bfd_set_section_flags
   (bfd *abfd, asection *sec, flagword flags);

void bfd_map_over_sections
   (bfd *abfd,
    void (*func) (bfd *abfd, asection *sect, void *obj),
    void *obj);

asection *bfd_sections_find_if
   (bfd *abfd,
    bfd_boolean (*operation) (bfd *abfd, asection *sect, void *obj),
    void *obj);

bfd_boolean bfd_set_section_size
   (bfd *abfd, asection *sec, bfd_size_type val);

bfd_boolean bfd_set_section_contents
   (bfd *abfd, asection *section, const void *data,
    file_ptr offset, bfd_size_type count);

bfd_boolean bfd_get_section_contents
   (bfd *abfd, asection *section, void *location, file_ptr offset,
    bfd_size_type count);

bfd_boolean bfd_malloc_and_get_section
   (bfd *abfd, asection *section, bfd_byte **buf);

bfd_boolean bfd_copy_private_section_data
   (bfd *ibfd, asection *isec, bfd *obfd, asection *osec);

#define bfd_copy_private_section_data(ibfd, isection, obfd, osection) \
     BFD_SEND (obfd, _bfd_copy_private_section_data, \
               (ibfd, isection, obfd, osection))
bfd_boolean bfd_generic_is_group_section (bfd *, const asection *sec);

bfd_boolean bfd_generic_discard_group (bfd *abfd, asection *group);

/* Extracted from archures.c.  */
enum bfd_architecture
{
  bfd_arch_unknown,   /* File arch not known.  */
  bfd_arch_obscure,   /* Arch known, not one of these.  */
  bfd_arch_m68k,      /* Motorola 68xxx */
#define bfd_mach_m68000 1
#define bfd_mach_m68008 2
#define bfd_mach_m68010 3
#define bfd_mach_m68020 4
#define bfd_mach_m68030 5
#define bfd_mach_m68040 6
#define bfd_mach_m68060 7
#define bfd_mach_cpu32  8
#define bfd_mach_fido   9
#define bfd_mach_mcf_isa_a_nodiv 10
#define bfd_mach_mcf_isa_a 11
#define bfd_mach_mcf_isa_a_mac 12
#define bfd_mach_mcf_isa_a_emac 13
#define bfd_mach_mcf_isa_aplus 14
#define bfd_mach_mcf_isa_aplus_mac 15
#define bfd_mach_mcf_isa_aplus_emac 16
#define bfd_mach_mcf_isa_b_nousp 17
#define bfd_mach_mcf_isa_b_nousp_mac 18
#define bfd_mach_mcf_isa_b_nousp_emac 19
#define bfd_mach_mcf_isa_b 20
#define bfd_mach_mcf_isa_b_mac 21
#define bfd_mach_mcf_isa_b_emac 22
#define bfd_mach_mcf_isa_b_float 23
#define bfd_mach_mcf_isa_b_float_mac 24
#define bfd_mach_mcf_isa_b_float_emac 25
#define bfd_mach_mcf_isa_c 26
#define bfd_mach_mcf_isa_c_mac 27
#define bfd_mach_mcf_isa_c_emac 28
  bfd_arch_vax,       /* DEC Vax */
  bfd_arch_i960,      /* Intel 960 */
    /* The order of the following is important.
       lower number indicates a machine type that
       only accepts a subset of the instructions
       available to machines with higher numbers.
       The exception is the "ca", which is
       incompatible with all other machines except
       "core".  */

#define bfd_mach_i960_core      1
#define bfd_mach_i960_ka_sa     2
#define bfd_mach_i960_kb_sb     3
#define bfd_mach_i960_mc        4
#define bfd_mach_i960_xa        5
#define bfd_mach_i960_ca        6
#define bfd_mach_i960_jx        7
#define bfd_mach_i960_hx        8

  bfd_arch_or32,      /* OpenRISC 32 */

  bfd_arch_sparc,     /* SPARC */
#define bfd_mach_sparc                 1
/* The difference between v8plus and v9 is that v9 is a true 64 bit env.  */
#define bfd_mach_sparc_sparclet        2
#define bfd_mach_sparc_sparclite       3
#define bfd_mach_sparc_v8plus          4
#define bfd_mach_sparc_v8plusa         5 /* with ultrasparc add'ns.  */
#define bfd_mach_sparc_sparclite_le    6
#define bfd_mach_sparc_v9              7
#define bfd_mach_sparc_v9a             8 /* with ultrasparc add'ns.  */
#define bfd_mach_sparc_v8plusb         9 /* with cheetah add'ns.  */
#define bfd_mach_sparc_v9b             10 /* with cheetah add'ns.  */
/* Nonzero if MACH has the v9 instruction set.  */
#define bfd_mach_sparc_v9_p(mach) \
  ((mach) >= bfd_mach_sparc_v8plus && (mach) <= bfd_mach_sparc_v9b \
   && (mach) != bfd_mach_sparc_sparclite_le)
/* Nonzero if MACH is a 64 bit sparc architecture.  */
#define bfd_mach_sparc_64bit_p(mach) \
  ((mach) >= bfd_mach_sparc_v9 && (mach) != bfd_mach_sparc_v8plusb)
  bfd_arch_spu,       /* PowerPC SPU */
#define bfd_mach_spu           256 
  bfd_arch_mips,      /* MIPS Rxxxx */
#define bfd_mach_mips3000              3000
#define bfd_mach_mips3900              3900
#define bfd_mach_mips4000              4000
#define bfd_mach_mips4010              4010
#define bfd_mach_mips4100              4100
#define bfd_mach_mips4111              4111
#define bfd_mach_mips4120              4120
#define bfd_mach_mips4300              4300
#define bfd_mach_mips4400              4400
#define bfd_mach_mips4600              4600
#define bfd_mach_mips4650              4650
#define bfd_mach_mips5000              5000
#define bfd_mach_mips5400              5400
#define bfd_mach_mips5500              5500
#define bfd_mach_mips6000              6000
#define bfd_mach_mips7000              7000
#define bfd_mach_mips8000              8000
#define bfd_mach_mips9000              9000
#define bfd_mach_mips10000             10000
#define bfd_mach_mips12000             12000
#define bfd_mach_mips16                16
#define bfd_mach_mips5                 5
#define bfd_mach_mips_sb1              12310201 /* octal 'SB', 01 */
#define bfd_mach_mipsisa32             32
#define bfd_mach_mipsisa32r2           33
#define bfd_mach_mipsisa64             64
#define bfd_mach_mipsisa64r2           65
  bfd_arch_i386,      /* Intel 386 */
#define bfd_mach_i386_i386 1
#define bfd_mach_i386_i8086 2
#define bfd_mach_i386_i386_intel_syntax 3
#define bfd_mach_x86_64 64
#define bfd_mach_x86_64_intel_syntax 65
  bfd_arch_we32k,     /* AT&T WE32xxx */
  bfd_arch_tahoe,     /* CCI/Harris Tahoe */
  bfd_arch_i860,      /* Intel 860 */
  bfd_arch_i370,      /* IBM 360/370 Mainframes */
  bfd_arch_romp,      /* IBM ROMP PC/RT */
  bfd_arch_convex,    /* Convex */
  bfd_arch_m88k,      /* Motorola 88xxx */
  bfd_arch_m98k,      /* Motorola 98xxx */
  bfd_arch_pyramid,   /* Pyramid Technology */
  bfd_arch_h8300,     /* Renesas H8/300 (formerly Hitachi H8/300) */
#define bfd_mach_h8300    1
#define bfd_mach_h8300h   2
#define bfd_mach_h8300s   3
#define bfd_mach_h8300hn  4
#define bfd_mach_h8300sn  5
#define bfd_mach_h8300sx  6
#define bfd_mach_h8300sxn 7
  bfd_arch_pdp11,     /* DEC PDP-11 */
  bfd_arch_powerpc,   /* PowerPC */
#define bfd_mach_ppc           32
#define bfd_mach_ppc64         64
#define bfd_mach_ppc_403       403
#define bfd_mach_ppc_403gc     4030
#define bfd_mach_ppc_505       505
#define bfd_mach_ppc_601       601
#define bfd_mach_ppc_602       602
#define bfd_mach_ppc_603       603
#define bfd_mach_ppc_ec603e    6031
#define bfd_mach_ppc_604       604
#define bfd_mach_ppc_620       620
#define bfd_mach_ppc_630       630
#define bfd_mach_ppc_750       750
#define bfd_mach_ppc_860       860
#define bfd_mach_ppc_a35       35
#define bfd_mach_ppc_rs64ii    642
#define bfd_mach_ppc_rs64iii   643
#define bfd_mach_ppc_7400      7400
#define bfd_mach_ppc_e500      500
  bfd_arch_rs6000,    /* IBM RS/6000 */
#define bfd_mach_rs6k          6000
#define bfd_mach_rs6k_rs1      6001
#define bfd_mach_rs6k_rsc      6003
#define bfd_mach_rs6k_rs2      6002
  bfd_arch_hppa,      /* HP PA RISC */
#define bfd_mach_hppa10        10
#define bfd_mach_hppa11        11
#define bfd_mach_hppa20        20
#define bfd_mach_hppa20w       25
  bfd_arch_d10v,      /* Mitsubishi D10V */
#define bfd_mach_d10v          1
#define bfd_mach_d10v_ts2      2
#define bfd_mach_d10v_ts3      3
  bfd_arch_d30v,      /* Mitsubishi D30V */
  bfd_arch_dlx,       /* DLX */
  bfd_arch_m68hc11,   /* Motorola 68HC11 */
  bfd_arch_m68hc12,   /* Motorola 68HC12 */
#define bfd_mach_m6812_default 0
#define bfd_mach_m6812         1
#define bfd_mach_m6812s        2
  bfd_arch_z8k,       /* Zilog Z8000 */
#define bfd_mach_z8001         1
#define bfd_mach_z8002         2
  bfd_arch_h8500,     /* Renesas H8/500 (formerly Hitachi H8/500) */
  bfd_arch_sh,        /* Renesas / SuperH SH (formerly Hitachi SH) */
#define bfd_mach_sh            1
#define bfd_mach_sh2        0x20
#define bfd_mach_sh_dsp     0x2d
#define bfd_mach_sh2a       0x2a
#define bfd_mach_sh2a_nofpu 0x2b
#define bfd_mach_sh2a_nofpu_or_sh4_nommu_nofpu 0x2a1
#define bfd_mach_sh2a_nofpu_or_sh3_nommu 0x2a2
#define bfd_mach_sh2a_or_sh4  0x2a3
#define bfd_mach_sh2a_or_sh3e 0x2a4
#define bfd_mach_sh2e       0x2e
#define bfd_mach_sh3        0x30
#define bfd_mach_sh3_nommu  0x31
#define bfd_mach_sh3_dsp    0x3d
#define bfd_mach_sh3e       0x3e
#define bfd_mach_sh4        0x40
#define bfd_mach_sh4_nofpu  0x41
#define bfd_mach_sh4_nommu_nofpu  0x42
#define bfd_mach_sh4a       0x4a
#define bfd_mach_sh4a_nofpu 0x4b
#define bfd_mach_sh4al_dsp  0x4d
#define bfd_mach_sh5        0x50
  bfd_arch_alpha,     /* Dec Alpha */
#define bfd_mach_alpha_ev4  0x10
#define bfd_mach_alpha_ev5  0x20
#define bfd_mach_alpha_ev6  0x30
  bfd_arch_arm,       /* Advanced Risc Machines ARM.  */
#define bfd_mach_arm_unknown   0
#define bfd_mach_arm_2         1
#define bfd_mach_arm_2a        2
#define bfd_mach_arm_3         3
#define bfd_mach_arm_3M        4
#define bfd_mach_arm_4         5
#define bfd_mach_arm_4T        6
#define bfd_mach_arm_5         7
#define bfd_mach_arm_5T        8
#define bfd_mach_arm_5TE       9
#define bfd_mach_arm_XScale    10
#define bfd_mach_arm_ep9312    11
#define bfd_mach_arm_iWMMXt    12
#define bfd_mach_arm_iWMMXt2   13
  bfd_arch_ns32k,     /* National Semiconductors ns32000 */
  bfd_arch_w65,       /* WDC 65816 */
  bfd_arch_tic30,     /* Texas Instruments TMS320C30 */
  bfd_arch_tic4x,     /* Texas Instruments TMS320C3X/4X */
#define bfd_mach_tic3x         30
#define bfd_mach_tic4x         40
  bfd_arch_tic54x,    /* Texas Instruments TMS320C54X */
  bfd_arch_tic80,     /* TI TMS320c80 (MVP) */
  bfd_arch_v850,      /* NEC V850 */
#define bfd_mach_v850          1
#define bfd_mach_v850e         'E'
#define bfd_mach_v850e1        '1'
  bfd_arch_arc,       /* ARC Cores */
#define bfd_mach_arc_5         5
#define bfd_mach_arc_6         6
#define bfd_mach_arc_7         7
#define bfd_mach_arc_8         8
 bfd_arch_m32c,     /* Renesas M16C/M32C.  */
#define bfd_mach_m16c        0x75
#define bfd_mach_m32c        0x78
  bfd_arch_m32r,      /* Renesas M32R (formerly Mitsubishi M32R/D) */
#define bfd_mach_m32r          1 /* For backwards compatibility.  */
#define bfd_mach_m32rx         'x'
#define bfd_mach_m32r2         '2'
  bfd_arch_mn10200,   /* Matsushita MN10200 */
  bfd_arch_mn10300,   /* Matsushita MN10300 */
#define bfd_mach_mn10300               300
#define bfd_mach_am33          330
#define bfd_mach_am33_2        332
  bfd_arch_fr30,
#define bfd_mach_fr30          0x46523330
  bfd_arch_frv,
#define bfd_mach_frv           1
#define bfd_mach_frvsimple     2
#define bfd_mach_fr300         300
#define bfd_mach_fr400         400
#define bfd_mach_fr450         450
#define bfd_mach_frvtomcat     499     /* fr500 prototype */
#define bfd_mach_fr500         500
#define bfd_mach_fr550         550
  bfd_arch_mcore,
  bfd_arch_mep,
#define bfd_mach_mep           1
#define bfd_mach_mep_h1        0x6831
  bfd_arch_ia64,      /* HP/Intel ia64 */
#define bfd_mach_ia64_elf64    64
#define bfd_mach_ia64_elf32    32
  bfd_arch_ip2k,      /* Ubicom IP2K microcontrollers. */
#define bfd_mach_ip2022        1
#define bfd_mach_ip2022ext     2
 bfd_arch_iq2000,     /* Vitesse IQ2000.  */
#define bfd_mach_iq2000        1
#define bfd_mach_iq10          2
  bfd_arch_mt,
#define bfd_mach_ms1           1
#define bfd_mach_mrisc2        2
#define bfd_mach_ms2           3
  bfd_arch_pj,
  bfd_arch_avr,       /* Atmel AVR microcontrollers.  */
#define bfd_mach_avr1          1
#define bfd_mach_avr2          2
#define bfd_mach_avr3          3
#define bfd_mach_avr4          4
#define bfd_mach_avr5          5
#define bfd_mach_avr6          6
  bfd_arch_bfin,        /* ADI Blackfin */
#define bfd_mach_bfin          1
  bfd_arch_cr16,       /* National Semiconductor CompactRISC (ie CR16). */
#define bfd_mach_cr16          1
  bfd_arch_cr16c,       /* National Semiconductor CompactRISC. */
#define bfd_mach_cr16c         1
  bfd_arch_crx,       /*  National Semiconductor CRX.  */
#define bfd_mach_crx           1
  bfd_arch_cris,      /* Axis CRIS */
#define bfd_mach_cris_v0_v10   255
#define bfd_mach_cris_v32      32
#define bfd_mach_cris_v10_v32  1032
  bfd_arch_s390,      /* IBM s390 */
#define bfd_mach_s390_31       31
#define bfd_mach_s390_64       64
  bfd_arch_score,     /* Sunplus score */ 
  bfd_arch_openrisc,  /* OpenRISC */
  bfd_arch_mmix,      /* Donald Knuth's educational processor.  */
  bfd_arch_xstormy16,
#define bfd_mach_xstormy16     1
  bfd_arch_msp430,    /* Texas Instruments MSP430 architecture.  */
#define bfd_mach_msp11          11
#define bfd_mach_msp110         110
#define bfd_mach_msp12          12
#define bfd_mach_msp13          13
#define bfd_mach_msp14          14
#define bfd_mach_msp15          15
#define bfd_mach_msp16          16
#define bfd_mach_msp21          21
#define bfd_mach_msp31          31
#define bfd_mach_msp32          32
#define bfd_mach_msp33          33
#define bfd_mach_msp41          41
#define bfd_mach_msp42          42
#define bfd_mach_msp43          43
#define bfd_mach_msp44          44
  bfd_arch_xc16x,     /* Infineon's XC16X Series.               */
#define bfd_mach_xc16x         1
#define bfd_mach_xc16xl        2
#define bfd_mach_xc16xs         3
  bfd_arch_xtensa,    /* Tensilica's Xtensa cores.  */
#define bfd_mach_xtensa        1
   bfd_arch_maxq,     /* Dallas MAXQ 10/20 */
#define bfd_mach_maxq10    10
#define bfd_mach_maxq20    20
  bfd_arch_z80,
#define bfd_mach_z80strict      1 /* No undocumented opcodes.  */
#define bfd_mach_z80            3 /* With ixl, ixh, iyl, and iyh.  */
#define bfd_mach_z80full        7 /* All undocumented instructions.  */
#define bfd_mach_r800           11 /* R800: successor with multiplication.  */
  bfd_arch_last
  };

typedef struct bfd_arch_info
{
  int bits_per_word;
  int bits_per_address;
  int bits_per_byte;
  enum bfd_architecture arch;
  unsigned long mach;
  const char *arch_name;
  const char *printable_name;
  unsigned int section_align_power;
  /* TRUE if this is the default machine for the architecture.
     The default arch should be the first entry for an arch so that
     all the entries for that arch can be accessed via <<next>>.  */
  bfd_boolean the_default;
  const struct bfd_arch_info * (*compatible)
    (const struct bfd_arch_info *a, const struct bfd_arch_info *b);

  bfd_boolean (*scan) (const struct bfd_arch_info *, const char *);

  const struct bfd_arch_info *next;
}
bfd_arch_info_type;

const char *bfd_printable_name (bfd *abfd);

const bfd_arch_info_type *bfd_scan_arch (const char *string);

const char **bfd_arch_list (void);

const bfd_arch_info_type *bfd_arch_get_compatible
   (const bfd *abfd, const bfd *bbfd, bfd_boolean accept_unknowns);

void bfd_set_arch_info (bfd *abfd, const bfd_arch_info_type *arg);

enum bfd_architecture bfd_get_arch (bfd *abfd);

unsigned long bfd_get_mach (bfd *abfd);

unsigned int bfd_arch_bits_per_byte (bfd *abfd);

unsigned int bfd_arch_bits_per_address (bfd *abfd);

const bfd_arch_info_type *bfd_get_arch_info (bfd *abfd);

const bfd_arch_info_type *bfd_lookup_arch
   (enum bfd_architecture arch, unsigned long machine);

const char *bfd_printable_arch_mach
   (enum bfd_architecture arch, unsigned long machine);

unsigned int bfd_octets_per_byte (bfd *abfd);

unsigned int bfd_arch_mach_octets_per_byte
   (enum bfd_architecture arch, unsigned long machine);

/* Extracted from reloc.c.  */
typedef enum bfd_reloc_status
{
  /* No errors detected.  */
  bfd_reloc_ok,

  /* The relocation was performed, but there was an overflow.  */
  bfd_reloc_overflow,

  /* The address to relocate was not within the section supplied.  */
  bfd_reloc_outofrange,

  /* Used by special functions.  */
  bfd_reloc_continue,

  /* Unsupported relocation size requested.  */
  bfd_reloc_notsupported,

  /* Unused.  */
  bfd_reloc_other,

  /* The symbol to relocate against was undefined.  */
  bfd_reloc_undefined,

  /* The relocation was performed, but may not be ok - presently
     generated only when linking i960 coff files with i960 b.out
     symbols.  If this type is returned, the error_message argument
     to bfd_perform_relocation will be set.  */
  bfd_reloc_dangerous
 }
 bfd_reloc_status_type;


typedef struct reloc_cache_entry
{
  /* A pointer into the canonical table of pointers.  */
  struct bfd_symbol **sym_ptr_ptr;

  /* offset in section.  */
  bfd_size_type address;

  /* addend for relocation value.  */
  bfd_vma addend;

  /* Pointer to how to perform the required relocation.  */
  reloc_howto_type *howto;

}
arelent;

enum complain_overflow
{
  /* Do not complain on overflow.  */
  complain_overflow_dont,

  /* Complain if the value overflows when considered as a signed
     number one bit larger than the field.  ie. A bitfield of N bits
     is allowed to represent -2**n to 2**n-1.  */
  complain_overflow_bitfield,

  /* Complain if the value overflows when considered as a signed
     number.  */
  complain_overflow_signed,

  /* Complain if the value overflows when considered as an
     unsigned number.  */
  complain_overflow_unsigned
};

struct reloc_howto_struct
{
  /*  The type field has mainly a documentary use - the back end can
      do what it wants with it, though normally the back end's
      external idea of what a reloc number is stored
      in this field.  For example, a PC relative word relocation
      in a coff environment has the type 023 - because that's
      what the outside world calls a R_PCRWORD reloc.  */
  unsigned int type;

  /*  The value the final relocation is shifted right by.  This drops
      unwanted data from the relocation.  */
  unsigned int rightshift;

  /*  The size of the item to be relocated.  This is *not* a
      power-of-two measure.  To get the number of bytes operated
      on by a type of relocation, use bfd_get_reloc_size.  */
  int size;

  /*  The number of bits in the item to be relocated.  This is used
      when doing overflow checking.  */
  unsigned int bitsize;

  /*  Notes that the relocation is relative to the location in the
      data section of the addend.  The relocation function will
      subtract from the relocation value the address of the location
      being relocated.  */
  bfd_boolean pc_relative;

  /*  The bit position of the reloc value in the destination.
      The relocated value is left shifted by this amount.  */
  unsigned int bitpos;

  /* What type of overflow error should be checked for when
     relocating.  */
  enum complain_overflow complain_on_overflow;

  /* If this field is non null, then the supplied function is
     called rather than the normal function.  This allows really
     strange relocation methods to be accommodated (e.g., i960 callj
     instructions).  */
  bfd_reloc_status_type (*special_function)
    (bfd *, arelent *, struct bfd_symbol *, void *, asection *,
     bfd *, char **);

  /* The textual name of the relocation type.  */
  char *name;

  /* Some formats record a relocation addend in the section contents
     rather than with the relocation.  For ELF formats this is the
     distinction between USE_REL and USE_RELA (though the code checks
     for USE_REL == 1/0).  The value of this field is TRUE if the
     addend is recorded with the section contents; when performing a
     partial link (ld -r) the section contents (the data) will be
     modified.  The value of this field is FALSE if addends are
     recorded with the relocation (in arelent.addend); when performing
     a partial link the relocation will be modified.
     All relocations for all ELF USE_RELA targets should set this field
     to FALSE (values of TRUE should be looked on with suspicion).
     However, the converse is not true: not all relocations of all ELF
     USE_REL targets set this field to TRUE.  Why this is so is peculiar
     to each particular target.  For relocs that aren't used in partial
     links (e.g. GOT stuff) it doesn't matter what this is set to.  */
  bfd_boolean partial_inplace;

  /* src_mask selects the part of the instruction (or data) to be used
     in the relocation sum.  If the target relocations don't have an
     addend in the reloc, eg. ELF USE_REL, src_mask will normally equal
     dst_mask to extract the addend from the section contents.  If
     relocations do have an addend in the reloc, eg. ELF USE_RELA, this
     field should be zero.  Non-zero values for ELF USE_RELA targets are
     bogus as in those cases the value in the dst_mask part of the
     section contents should be treated as garbage.  */
  bfd_vma src_mask;

  /* dst_mask selects which parts of the instruction (or data) are
     replaced with a relocated value.  */
  bfd_vma dst_mask;

  /* When some formats create PC relative instructions, they leave
     the value of the pc of the place being relocated in the offset
     slot of the instruction, so that a PC relative relocation can
     be made just by adding in an ordinary offset (e.g., sun3 a.out).
     Some formats leave the displacement part of an instruction
     empty (e.g., m88k bcs); this flag signals the fact.  */
  bfd_boolean pcrel_offset;
};

#define HOWTO(C, R, S, B, P, BI, O, SF, NAME, INPLACE, MASKSRC, MASKDST, PC) \
  { (unsigned) C, R, S, B, P, BI, O, SF, NAME, INPLACE, MASKSRC, MASKDST, PC }
#define NEWHOWTO(FUNCTION, NAME, SIZE, REL, IN) \
  HOWTO (0, 0, SIZE, 0, REL, 0, complain_overflow_dont, FUNCTION, \
         NAME, FALSE, 0, 0, IN)

#define EMPTY_HOWTO(C) \
  HOWTO ((C), 0, 0, 0, FALSE, 0, complain_overflow_dont, NULL, \
         NULL, FALSE, 0, 0, FALSE)

#define HOWTO_PREPARE(relocation, symbol)               \
  {                                                     \
    if (symbol != NULL)                                 \
      {                                                 \
        if (bfd_is_com_section (symbol->section))       \
          {                                             \
            relocation = 0;                             \
          }                                             \
        else                                            \
          {                                             \
            relocation = symbol->value;                 \
          }                                             \
      }                                                 \
  }

unsigned int bfd_get_reloc_size (reloc_howto_type *);

typedef struct relent_chain
{
  arelent relent;
  struct relent_chain *next;
}
arelent_chain;

bfd_reloc_status_type bfd_check_overflow
   (enum complain_overflow how,
    unsigned int bitsize,
    unsigned int rightshift,
    unsigned int addrsize,
    bfd_vma relocation);

bfd_reloc_status_type bfd_perform_relocation
   (bfd *abfd,
    arelent *reloc_entry,
    void *data,
    asection *input_section,
    bfd *output_bfd,
    char **error_message);

bfd_reloc_status_type bfd_install_relocation
   (bfd *abfd,
    arelent *reloc_entry,
    void *data, bfd_vma data_start,
    asection *input_section,
    char **error_message);

enum bfd_reloc_code_real {
  _dummy_first_bfd_reloc_code_real,


/* Basic absolute relocations of N bits.  */
  BFD_RELOC_64,
  BFD_RELOC_32,
  BFD_RELOC_26,
  BFD_RELOC_24,
  BFD_RELOC_16,
  BFD_RELOC_14,
  BFD_RELOC_8,

/* PC-relative relocations.  Sometimes these are relative to the address
of the relocation itself; sometimes they are relative to the start of
the section containing the relocation.  It depends on the specific target.

The 24-bit relocation is used in some Intel 960 configurations.  */
  BFD_RELOC_64_PCREL,
  BFD_RELOC_32_PCREL,
  BFD_RELOC_24_PCREL,
  BFD_RELOC_16_PCREL,
  BFD_RELOC_12_PCREL,
  BFD_RELOC_8_PCREL,

/* Section relative relocations.  Some targets need this for DWARF2.  */
  BFD_RELOC_32_SECREL,

/* For ELF.  */
  BFD_RELOC_32_GOT_PCREL,
  BFD_RELOC_16_GOT_PCREL,
  BFD_RELOC_8_GOT_PCREL,
  BFD_RELOC_32_GOTOFF,
  BFD_RELOC_16_GOTOFF,
  BFD_RELOC_LO16_GOTOFF,
  BFD_RELOC_HI16_GOTOFF,
  BFD_RELOC_HI16_S_GOTOFF,
  BFD_RELOC_8_GOTOFF,
  BFD_RELOC_64_PLT_PCREL,
  BFD_RELOC_32_PLT_PCREL,
  BFD_RELOC_24_PLT_PCREL,
  BFD_RELOC_16_PLT_PCREL,
  BFD_RELOC_8_PLT_PCREL,
  BFD_RELOC_64_PLTOFF,
  BFD_RELOC_32_PLTOFF,
  BFD_RELOC_16_PLTOFF,
  BFD_RELOC_LO16_PLTOFF,
  BFD_RELOC_HI16_PLTOFF,
  BFD_RELOC_HI16_S_PLTOFF,
  BFD_RELOC_8_PLTOFF,

/* Relocations used by 68K ELF.  */
  BFD_RELOC_68K_GLOB_DAT,
  BFD_RELOC_68K_JMP_SLOT,
  BFD_RELOC_68K_RELATIVE,

/* Linkage-table relative.  */
  BFD_RELOC_32_BASEREL,
  BFD_RELOC_16_BASEREL,
  BFD_RELOC_LO16_BASEREL,
  BFD_RELOC_HI16_BASEREL,
  BFD_RELOC_HI16_S_BASEREL,
  BFD_RELOC_8_BASEREL,
  BFD_RELOC_RVA,

/* Absolute 8-bit relocation, but used to form an address like 0xFFnn.  */
  BFD_RELOC_8_FFnn,

/* These PC-relative relocations are stored as word displacements --
i.e., byte displacements shifted right two bits.  The 30-bit word
displacement (<<32_PCREL_S2>> -- 32 bits, shifted 2) is used on the
SPARC.  (SPARC tools generally refer to this as <<WDISP30>>.)  The
signed 16-bit displacement is used on the MIPS, and the 23-bit
displacement is used on the Alpha.  */
  BFD_RELOC_32_PCREL_S2,
  BFD_RELOC_16_PCREL_S2,
  BFD_RELOC_23_PCREL_S2,

/* High 22 bits and low 10 bits of 32-bit value, placed into lower bits of
the target word.  These are used on the SPARC.  */
  BFD_RELOC_HI22,
  BFD_RELOC_LO10,

/* For systems that allocate a Global Pointer register, these are
displacements off that register.  These relocation types are
handled specially, because the value the register will have is
decided relatively late.  */
  BFD_RELOC_GPREL16,
  BFD_RELOC_GPREL32,

/* Reloc types used for i960/b.out.  */
  BFD_RELOC_I960_CALLJ,

/* SPARC ELF relocations.  There is probably some overlap with other
relocation types already defined.  */
  BFD_RELOC_NONE,
  BFD_RELOC_SPARC_WDISP22,
  BFD_RELOC_SPARC22,
  BFD_RELOC_SPARC13,
  BFD_RELOC_SPARC_GOT10,
  BFD_RELOC_SPARC_GOT13,
  BFD_RELOC_SPARC_GOT22,
  BFD_RELOC_SPARC_PC10,
  BFD_RELOC_SPARC_PC22,
  BFD_RELOC_SPARC_WPLT30,
  BFD_RELOC_SPARC_COPY,
  BFD_RELOC_SPARC_GLOB_DAT,
  BFD_RELOC_SPARC_JMP_SLOT,
  BFD_RELOC_SPARC_RELATIVE,
  BFD_RELOC_SPARC_UA16,
  BFD_RELOC_SPARC_UA32,
  BFD_RELOC_SPARC_UA64,

/* I think these are specific to SPARC a.out (e.g., Sun 4).  */
  BFD_RELOC_SPARC_BASE13,
  BFD_RELOC_SPARC_BASE22,

/* SPARC64 relocations  */
#define BFD_RELOC_SPARC_64 BFD_RELOC_64
  BFD_RELOC_SPARC_10,
  BFD_RELOC_SPARC_11,
  BFD_RELOC_SPARC_OLO10,
  BFD_RELOC_SPARC_HH22,
  BFD_RELOC_SPARC_HM10,
  BFD_RELOC_SPARC_LM22,
  BFD_RELOC_SPARC_PC_HH22,
  BFD_RELOC_SPARC_PC_HM10,
  BFD_RELOC_SPARC_PC_LM22,
  BFD_RELOC_SPARC_WDISP16,
  BFD_RELOC_SPARC_WDISP19,
  BFD_RELOC_SPARC_7,
  BFD_RELOC_SPARC_6,
  BFD_RELOC_SPARC_5,
#define BFD_RELOC_SPARC_DISP64 BFD_RELOC_64_PCREL
  BFD_RELOC_SPARC_PLT32,
  BFD_RELOC_SPARC_PLT64,
  BFD_RELOC_SPARC_HIX22,
  BFD_RELOC_SPARC_LOX10,
  BFD_RELOC_SPARC_H44,
  BFD_RELOC_SPARC_M44,
  BFD_RELOC_SPARC_L44,
  BFD_RELOC_SPARC_REGISTER,

/* SPARC little endian relocation  */
  BFD_RELOC_SPARC_REV32,

/* SPARC TLS relocations  */
  BFD_RELOC_SPARC_TLS_GD_HI22,
  BFD_RELOC_SPARC_TLS_GD_LO10,
  BFD_RELOC_SPARC_TLS_GD_ADD,
  BFD_RELOC_SPARC_TLS_GD_CALL,
  BFD_RELOC_SPARC_TLS_LDM_HI22,
  BFD_RELOC_SPARC_TLS_LDM_LO10,
  BFD_RELOC_SPARC_TLS_LDM_ADD,
  BFD_RELOC_SPARC_TLS_LDM_CALL,
  BFD_RELOC_SPARC_TLS_LDO_HIX22,
  BFD_RELOC_SPARC_TLS_LDO_LOX10,
  BFD_RELOC_SPARC_TLS_LDO_ADD,
  BFD_RELOC_SPARC_TLS_IE_HI22,
  BFD_RELOC_SPARC_TLS_IE_LO10,
  BFD_RELOC_SPARC_TLS_IE_LD,
  BFD_RELOC_SPARC_TLS_IE_LDX,
  BFD_RELOC_SPARC_TLS_IE_ADD,
  BFD_RELOC_SPARC_TLS_LE_HIX22,
  BFD_RELOC_SPARC_TLS_LE_LOX10,
  BFD_RELOC_SPARC_TLS_DTPMOD32,
  BFD_RELOC_SPARC_TLS_DTPMOD64,
  BFD_RELOC_SPARC_TLS_DTPOFF32,
  BFD_RELOC_SPARC_TLS_DTPOFF64,
  BFD_RELOC_SPARC_TLS_TPOFF32,
  BFD_RELOC_SPARC_TLS_TPOFF64,

/* SPU Relocations.  */
  BFD_RELOC_SPU_IMM7,
  BFD_RELOC_SPU_IMM8,
  BFD_RELOC_SPU_IMM10,
  BFD_RELOC_SPU_IMM10W,
  BFD_RELOC_SPU_IMM16,
  BFD_RELOC_SPU_IMM16W,
  BFD_RELOC_SPU_IMM18,
  BFD_RELOC_SPU_PCREL9a,
  BFD_RELOC_SPU_PCREL9b,
  BFD_RELOC_SPU_PCREL16,
  BFD_RELOC_SPU_LO16,
  BFD_RELOC_SPU_HI16,
  BFD_RELOC_SPU_PPU32,
  BFD_RELOC_SPU_PPU64,

/* Alpha ECOFF and ELF relocations.  Some of these treat the symbol or
"addend" in some special way.
For GPDISP_HI16 ("gpdisp") relocations, the symbol is ignored when
writing; when reading, it will be the absolute section symbol.  The
addend is the displacement in bytes of the "lda" instruction from
the "ldah" instruction (which is at the address of this reloc).  */
  BFD_RELOC_ALPHA_GPDISP_HI16,

/* For GPDISP_LO16 ("ignore") relocations, the symbol is handled as
with GPDISP_HI16 relocs.  The addend is ignored when writing the
relocations out, and is filled in with the file's GP value on
reading, for convenience.  */
  BFD_RELOC_ALPHA_GPDISP_LO16,

/* The ELF GPDISP relocation is exactly the same as the GPDISP_HI16
relocation except that there is no accompanying GPDISP_LO16
relocation.  */
  BFD_RELOC_ALPHA_GPDISP,

/* The Alpha LITERAL/LITUSE relocs are produced by a symbol reference;
the assembler turns it into a LDQ instruction to load the address of
the symbol, and then fills in a register in the real instruction.

The LITERAL reloc, at the LDQ instruction, refers to the .lita
section symbol.  The addend is ignored when writing, but is filled
in with the file's GP value on reading, for convenience, as with the
GPDISP_LO16 reloc.

The ELF_LITERAL reloc is somewhere between 16_GOTOFF and GPDISP_LO16.
It should refer to the symbol to be referenced, as with 16_GOTOFF,
but it generates output not based on the position within the .got
section, but relative to the GP value chosen for the file during the
final link stage.

The LITUSE reloc, on the instruction using the loaded address, gives
information to the linker that it might be able to use to optimize
away some literal section references.  The symbol is ignored (read
as the absolute section symbol), and the "addend" indicates the type
of instruction using the register:
1 - "memory" fmt insn
2 - byte-manipulation (byte offset reg)
3 - jsr (target of branch)  */
  BFD_RELOC_ALPHA_LITERAL,
  BFD_RELOC_ALPHA_ELF_LITERAL,
  BFD_RELOC_ALPHA_LITUSE,

/* The HINT relocation indicates a value that should be filled into the
"hint" field of a jmp/jsr/ret instruction, for possible branch-
prediction logic which may be provided on some processors.  */
  BFD_RELOC_ALPHA_HINT,

/* The LINKAGE relocation outputs a linkage pair in the object file,
which is filled by the linker.  */
  BFD_RELOC_ALPHA_LINKAGE,

/* The CODEADDR relocation outputs a STO_CA in the object file,
which is filled by the linker.  */
  BFD_RELOC_ALPHA_CODEADDR,

/* The GPREL_HI/LO relocations together form a 32-bit offset from the
GP register.  */
  BFD_RELOC_ALPHA_GPREL_HI16,
  BFD_RELOC_ALPHA_GPREL_LO16,

/* Like BFD_RELOC_23_PCREL_S2, except that the source and target must
share a common GP, and the target address is adjusted for
STO_ALPHA_STD_GPLOAD.  */
  BFD_RELOC_ALPHA_BRSGP,

/* Alpha thread-local storage relocations.  */
  BFD_RELOC_ALPHA_TLSGD,
  BFD_RELOC_ALPHA_TLSLDM,
  BFD_RELOC_ALPHA_DTPMOD64,
  BFD_RELOC_ALPHA_GOTDTPREL16,
  BFD_RELOC_ALPHA_DTPREL64,
  BFD_RELOC_ALPHA_DTPREL_HI16,
  BFD_RELOC_ALPHA_DTPREL_LO16,
  BFD_RELOC_ALPHA_DTPREL16,
  BFD_RELOC_ALPHA_GOTTPREL16,
  BFD_RELOC_ALPHA_TPREL64,
  BFD_RELOC_ALPHA_TPREL_HI16,
  BFD_RELOC_ALPHA_TPREL_LO16,
  BFD_RELOC_ALPHA_TPREL16,

/* Bits 27..2 of the relocation address shifted right 2 bits;
simple reloc otherwise.  */
  BFD_RELOC_MIPS_JMP,

/* The MIPS16 jump instruction.  */
  BFD_RELOC_MIPS16_JMP,

/* MIPS16 GP relative reloc.  */
  BFD_RELOC_MIPS16_GPREL,

/* High 16 bits of 32-bit value; simple reloc.  */
  BFD_RELOC_HI16,

/* High 16 bits of 32-bit value but the low 16 bits will be sign
extended and added to form the final result.  If the low 16
bits form a negative number, we need to add one to the high value
to compensate for the borrow when the low bits are added.  */
  BFD_RELOC_HI16_S,

/* Low 16 bits.  */
  BFD_RELOC_LO16,

/* High 16 bits of 32-bit pc-relative value  */
  BFD_RELOC_HI16_PCREL,

/* High 16 bits of 32-bit pc-relative value, adjusted  */
  BFD_RELOC_HI16_S_PCREL,

/* Low 16 bits of pc-relative value  */
  BFD_RELOC_LO16_PCREL,

/* MIPS16 high 16 bits of 32-bit value.  */
  BFD_RELOC_MIPS16_HI16,

/* MIPS16 high 16 bits of 32-bit value but the low 16 bits will be sign
extended and added to form the final result.  If the low 16
bits form a negative number, we need to add one to the high value
to compensate for the borrow when the low bits are added.  */
  BFD_RELOC_MIPS16_HI16_S,

/* MIPS16 low 16 bits.  */
  BFD_RELOC_MIPS16_LO16,

/* Relocation against a MIPS literal section.  */
  BFD_RELOC_MIPS_LITERAL,

/* MIPS ELF relocations.  */
  BFD_RELOC_MIPS_GOT16,
  BFD_RELOC_MIPS_CALL16,
  BFD_RELOC_MIPS_GOT_HI16,
  BFD_RELOC_MIPS_GOT_LO16,
  BFD_RELOC_MIPS_CALL_HI16,
  BFD_RELOC_MIPS_CALL_LO16,
  BFD_RELOC_MIPS_SUB,
  BFD_RELOC_MIPS_GOT_PAGE,
  BFD_RELOC_MIPS_GOT_OFST,
  BFD_RELOC_MIPS_GOT_DISP,
  BFD_RELOC_MIPS_SHIFT5,
  BFD_RELOC_MIPS_SHIFT6,
  BFD_RELOC_MIPS_INSERT_A,
  BFD_RELOC_MIPS_INSERT_B,
  BFD_RELOC_MIPS_DELETE,
  BFD_RELOC_MIPS_HIGHEST,
  BFD_RELOC_MIPS_HIGHER,
  BFD_RELOC_MIPS_SCN_DISP,
  BFD_RELOC_MIPS_REL16,
  BFD_RELOC_MIPS_RELGOT,
  BFD_RELOC_MIPS_JALR,
  BFD_RELOC_MIPS_TLS_DTPMOD32,
  BFD_RELOC_MIPS_TLS_DTPREL32,
  BFD_RELOC_MIPS_TLS_DTPMOD64,
  BFD_RELOC_MIPS_TLS_DTPREL64,
  BFD_RELOC_MIPS_TLS_GD,
  BFD_RELOC_MIPS_TLS_LDM,
  BFD_RELOC_MIPS_TLS_DTPREL_HI16,
  BFD_RELOC_MIPS_TLS_DTPREL_LO16,
  BFD_RELOC_MIPS_TLS_GOTTPREL,
  BFD_RELOC_MIPS_TLS_TPREL32,
  BFD_RELOC_MIPS_TLS_TPREL64,
  BFD_RELOC_MIPS_TLS_TPREL_HI16,
  BFD_RELOC_MIPS_TLS_TPREL_LO16,


/* MIPS ELF relocations (VxWorks extensions).  */
  BFD_RELOC_MIPS_COPY,
  BFD_RELOC_MIPS_JUMP_SLOT,


/* Fujitsu Frv Relocations.  */
  BFD_RELOC_FRV_LABEL16,
  BFD_RELOC_FRV_LABEL24,
  BFD_RELOC_FRV_LO16,
  BFD_RELOC_FRV_HI16,
  BFD_RELOC_FRV_GPREL12,
  BFD_RELOC_FRV_GPRELU12,
  BFD_RELOC_FRV_GPREL32,
  BFD_RELOC_FRV_GPRELHI,
  BFD_RELOC_FRV_GPRELLO,
  BFD_RELOC_FRV_GOT12,
  BFD_RELOC_FRV_GOTHI,
  BFD_RELOC_FRV_GOTLO,
  BFD_RELOC_FRV_FUNCDESC,
  BFD_RELOC_FRV_FUNCDESC_GOT12,
  BFD_RELOC_FRV_FUNCDESC_GOTHI,
  BFD_RELOC_FRV_FUNCDESC_GOTLO,
  BFD_RELOC_FRV_FUNCDESC_VALUE,
  BFD_RELOC_FRV_FUNCDESC_GOTOFF12,
  BFD_RELOC_FRV_FUNCDESC_GOTOFFHI,
  BFD_RELOC_FRV_FUNCDESC_GOTOFFLO,
  BFD_RELOC_FRV_GOTOFF12,
  BFD_RELOC_FRV_GOTOFFHI,
  BFD_RELOC_FRV_GOTOFFLO,
  BFD_RELOC_FRV_GETTLSOFF,
  BFD_RELOC_FRV_TLSDESC_VALUE,
  BFD_RELOC_FRV_GOTTLSDESC12,
  BFD_RELOC_FRV_GOTTLSDESCHI,
  BFD_RELOC_FRV_GOTTLSDESCLO,
  BFD_RELOC_FRV_TLSMOFF12,
  BFD_RELOC_FRV_TLSMOFFHI,
  BFD_RELOC_FRV_TLSMOFFLO,
  BFD_RELOC_FRV_GOTTLSOFF12,
  BFD_RELOC_FRV_GOTTLSOFFHI,
  BFD_RELOC_FRV_GOTTLSOFFLO,
  BFD_RELOC_FRV_TLSOFF,
  BFD_RELOC_FRV_TLSDESC_RELAX,
  BFD_RELOC_FRV_GETTLSOFF_RELAX,
  BFD_RELOC_FRV_TLSOFF_RELAX,
  BFD_RELOC_FRV_TLSMOFF,


/* This is a 24bit GOT-relative reloc for the mn10300.  */
  BFD_RELOC_MN10300_GOTOFF24,

/* This is a 32bit GOT-relative reloc for the mn10300, offset by two bytes
in the instruction.  */
  BFD_RELOC_MN10300_GOT32,

/* This is a 24bit GOT-relative reloc for the mn10300, offset by two bytes
in the instruction.  */
  BFD_RELOC_MN10300_GOT24,

/* This is a 16bit GOT-relative reloc for the mn10300, offset by two bytes
in the instruction.  */
  BFD_RELOC_MN10300_GOT16,

/* Copy symbol at runtime.  */
  BFD_RELOC_MN10300_COPY,

/* Create GOT entry.  */
  BFD_RELOC_MN10300_GLOB_DAT,

/* Create PLT entry.  */
  BFD_RELOC_MN10300_JMP_SLOT,

/* Adjust by program base.  */
  BFD_RELOC_MN10300_RELATIVE,


/* i386/elf relocations  */
  BFD_RELOC_386_GOT32,
  BFD_RELOC_386_PLT32,
  BFD_RELOC_386_COPY,
  BFD_RELOC_386_GLOB_DAT,
  BFD_RELOC_386_JUMP_SLOT,
  BFD_RELOC_386_RELATIVE,
  BFD_RELOC_386_GOTOFF,
  BFD_RELOC_386_GOTPC,
  BFD_RELOC_386_TLS_TPOFF,
  BFD_RELOC_386_TLS_IE,
  BFD_RELOC_386_TLS_GOTIE,
  BFD_RELOC_386_TLS_LE,
  BFD_RELOC_386_TLS_GD,
  BFD_RELOC_386_TLS_LDM,
  BFD_RELOC_386_TLS_LDO_32,
  BFD_RELOC_386_TLS_IE_32,
  BFD_RELOC_386_TLS_LE_32,
  BFD_RELOC_386_TLS_DTPMOD32,
  BFD_RELOC_386_TLS_DTPOFF32,
  BFD_RELOC_386_TLS_TPOFF32,
  BFD_RELOC_386_TLS_GOTDESC,
  BFD_RELOC_386_TLS_DESC_CALL,
  BFD_RELOC_386_TLS_DESC,

/* x86-64/elf relocations  */
  BFD_RELOC_X86_64_GOT32,
  BFD_RELOC_X86_64_PLT32,
  BFD_RELOC_X86_64_COPY,
  BFD_RELOC_X86_64_GLOB_DAT,
  BFD_RELOC_X86_64_JUMP_SLOT,
  BFD_RELOC_X86_64_RELATIVE,
  BFD_RELOC_X86_64_GOTPCREL,
  BFD_RELOC_X86_64_32S,
  BFD_RELOC_X86_64_DTPMOD64,
  BFD_RELOC_X86_64_DTPOFF64,
  BFD_RELOC_X86_64_TPOFF64,
  BFD_RELOC_X86_64_TLSGD,
  BFD_RELOC_X86_64_TLSLD,
  BFD_RELOC_X86_64_DTPOFF32,
  BFD_RELOC_X86_64_GOTTPOFF,
  BFD_RELOC_X86_64_TPOFF32,
  BFD_RELOC_X86_64_GOTOFF64,
  BFD_RELOC_X86_64_GOTPC32,
  BFD_RELOC_X86_64_GOT64,
  BFD_RELOC_X86_64_GOTPCREL64,
  BFD_RELOC_X86_64_GOTPC64,
  BFD_RELOC_X86_64_GOTPLT64,
  BFD_RELOC_X86_64_PLTOFF64,
  BFD_RELOC_X86_64_GOTPC32_TLSDESC,
  BFD_RELOC_X86_64_TLSDESC_CALL,
  BFD_RELOC_X86_64_TLSDESC,

/* ns32k relocations  */
  BFD_RELOC_NS32K_IMM_8,
  BFD_RELOC_NS32K_IMM_16,
  BFD_RELOC_NS32K_IMM_32,
  BFD_RELOC_NS32K_IMM_8_PCREL,
  BFD_RELOC_NS32K_IMM_16_PCREL,
  BFD_RELOC_NS32K_IMM_32_PCREL,
  BFD_RELOC_NS32K_DISP_8,
  BFD_RELOC_NS32K_DISP_16,
  BFD_RELOC_NS32K_DISP_32,
  BFD_RELOC_NS32K_DISP_8_PCREL,
  BFD_RELOC_NS32K_DISP_16_PCREL,
  BFD_RELOC_NS32K_DISP_32_PCREL,

/* PDP11 relocations  */
  BFD_RELOC_PDP11_DISP_8_PCREL,
  BFD_RELOC_PDP11_DISP_6_PCREL,

/* Picojava relocs.  Not all of these appear in object files.  */
  BFD_RELOC_PJ_CODE_HI16,
  BFD_RELOC_PJ_CODE_LO16,
  BFD_RELOC_PJ_CODE_DIR16,
  BFD_RELOC_PJ_CODE_DIR32,
  BFD_RELOC_PJ_CODE_REL16,
  BFD_RELOC_PJ_CODE_REL32,

/* Power(rs6000) and PowerPC relocations.  */
  BFD_RELOC_PPC_B26,
  BFD_RELOC_PPC_BA26,
  BFD_RELOC_PPC_TOC16,
  BFD_RELOC_PPC_B16,
  BFD_RELOC_PPC_B16_BRTAKEN,
  BFD_RELOC_PPC_B16_BRNTAKEN,
  BFD_RELOC_PPC_BA16,
  BFD_RELOC_PPC_BA16_BRTAKEN,
  BFD_RELOC_PPC_BA16_BRNTAKEN,
  BFD_RELOC_PPC_COPY,
  BFD_RELOC_PPC_GLOB_DAT,
  BFD_RELOC_PPC_JMP_SLOT,
  BFD_RELOC_PPC_RELATIVE,
  BFD_RELOC_PPC_LOCAL24PC,
  BFD_RELOC_PPC_EMB_NADDR32,
  BFD_RELOC_PPC_EMB_NADDR16,
  BFD_RELOC_PPC_EMB_NADDR16_LO,
  BFD_RELOC_PPC_EMB_NADDR16_HI,
  BFD_RELOC_PPC_EMB_NADDR16_HA,
  BFD_RELOC_PPC_EMB_SDAI16,
  BFD_RELOC_PPC_EMB_SDA2I16,
  BFD_RELOC_PPC_EMB_SDA2REL,
  BFD_RELOC_PPC_EMB_SDA21,
  BFD_RELOC_PPC_EMB_MRKREF,
  BFD_RELOC_PPC_EMB_RELSEC16,
  BFD_RELOC_PPC_EMB_RELST_LO,
  BFD_RELOC_PPC_EMB_RELST_HI,
  BFD_RELOC_PPC_EMB_RELST_HA,
  BFD_RELOC_PPC_EMB_BIT_FLD,
  BFD_RELOC_PPC_EMB_RELSDA,
  BFD_RELOC_PPC64_HIGHER,
  BFD_RELOC_PPC64_HIGHER_S,
  BFD_RELOC_PPC64_HIGHEST,
  BFD_RELOC_PPC64_HIGHEST_S,
  BFD_RELOC_PPC64_TOC16_LO,
  BFD_RELOC_PPC64_TOC16_HI,
  BFD_RELOC_PPC64_TOC16_HA,
  BFD_RELOC_PPC64_TOC,
  BFD_RELOC_PPC64_PLTGOT16,
  BFD_RELOC_PPC64_PLTGOT16_LO,
  BFD_RELOC_PPC64_PLTGOT16_HI,
  BFD_RELOC_PPC64_PLTGOT16_HA,
  BFD_RELOC_PPC64_ADDR16_DS,
  BFD_RELOC_PPC64_ADDR16_LO_DS,
  BFD_RELOC_PPC64_GOT16_DS,
  BFD_RELOC_PPC64_GOT16_LO_DS,
  BFD_RELOC_PPC64_PLT16_LO_DS,
  BFD_RELOC_PPC64_SECTOFF_DS,
  BFD_RELOC_PPC64_SECTOFF_LO_DS,
  BFD_RELOC_PPC64_TOC16_DS,
  BFD_RELOC_PPC64_TOC16_LO_DS,
  BFD_RELOC_PPC64_PLTGOT16_DS,
  BFD_RELOC_PPC64_PLTGOT16_LO_DS,

/* PowerPC and PowerPC64 thread-local storage relocations.  */
  BFD_RELOC_PPC_TLS,
  BFD_RELOC_PPC_TLSGD,
  BFD_RELOC_PPC_TLSLD,
  BFD_RELOC_PPC_DTPMOD,
  BFD_RELOC_PPC_TPREL16,
  BFD_RELOC_PPC_TPREL16_LO,
  BFD_RELOC_PPC_TPREL16_HI,
  BFD_RELOC_PPC_TPREL16_HA,
  BFD_RELOC_PPC_TPREL,
  BFD_RELOC_PPC_DTPREL16,
  BFD_RELOC_PPC_DTPREL16_LO,
  BFD_RELOC_PPC_DTPREL16_HI,
  BFD_RELOC_PPC_DTPREL16_HA,
  BFD_RELOC_PPC_DTPREL,
  BFD_RELOC_PPC_GOT_TLSGD16,
  BFD_RELOC_PPC_GOT_TLSGD16_LO,
  BFD_RELOC_PPC_GOT_TLSGD16_HI,
  BFD_RELOC_PPC_GOT_TLSGD16_HA,
  BFD_RELOC_PPC_GOT_TLSLD16,
  BFD_RELOC_PPC_GOT_TLSLD16_LO,
  BFD_RELOC_PPC_GOT_TLSLD16_HI,
  BFD_RELOC_PPC_GOT_TLSLD16_HA,
  BFD_RELOC_PPC_GOT_TPREL16,
  BFD_RELOC_PPC_GOT_TPREL16_LO,
  BFD_RELOC_PPC_GOT_TPREL16_HI,
  BFD_RELOC_PPC_GOT_TPREL16_HA,
  BFD_RELOC_PPC_GOT_DTPREL16,
  BFD_RELOC_PPC_GOT_DTPREL16_LO,
  BFD_RELOC_PPC_GOT_DTPREL16_HI,
  BFD_RELOC_PPC_GOT_DTPREL16_HA,
  BFD_RELOC_PPC64_TPREL16_DS,
  BFD_RELOC_PPC64_TPREL16_LO_DS,
  BFD_RELOC_PPC64_TPREL16_HIGHER,
  BFD_RELOC_PPC64_TPREL16_HIGHERA,
  BFD_RELOC_PPC64_TPREL16_HIGHEST,
  BFD_RELOC_PPC64_TPREL16_HIGHESTA,
  BFD_RELOC_PPC64_DTPREL16_DS,
  BFD_RELOC_PPC64_DTPREL16_LO_DS,
  BFD_RELOC_PPC64_DTPREL16_HIGHER,
  BFD_RELOC_PPC64_DTPREL16_HIGHERA,
  BFD_RELOC_PPC64_DTPREL16_HIGHEST,
  BFD_RELOC_PPC64_DTPREL16_HIGHESTA,

/* IBM 370/390 relocations  */
  BFD_RELOC_I370_D12,

/* The type of reloc used to build a constructor table - at the moment
probably a 32 bit wide absolute relocation, but the target can choose.
It generally does map to one of the other relocation types.  */
  BFD_RELOC_CTOR,

/* ARM 26 bit pc-relative branch.  The lowest two bits must be zero and are
not stored in the instruction.  */
  BFD_RELOC_ARM_PCREL_BRANCH,

/* ARM 26 bit pc-relative branch.  The lowest bit must be zero and is
not stored in the instruction.  The 2nd lowest bit comes from a 1 bit
field in the instruction.  */
  BFD_RELOC_ARM_PCREL_BLX,

/* Thumb 22 bit pc-relative branch.  The lowest bit must be zero and is
not stored in the instruction.  The 2nd lowest bit comes from a 1 bit
field in the instruction.  */
  BFD_RELOC_THUMB_PCREL_BLX,

/* ARM 26-bit pc-relative branch for an unconditional BL or BLX instruction.  */
  BFD_RELOC_ARM_PCREL_CALL,

/* ARM 26-bit pc-relative branch for B or conditional BL instruction.  */
  BFD_RELOC_ARM_PCREL_JUMP,

/* Thumb 7-, 9-, 12-, 20-, 23-, and 25-bit pc-relative branches.
The lowest bit must be zero and is not stored in the instruction.
Note that the corresponding ELF R_ARM_THM_JUMPnn constant has an
"nn" one smaller in all cases.  Note further that BRANCH23
corresponds to R_ARM_THM_CALL.  */
  BFD_RELOC_THUMB_PCREL_BRANCH7,
  BFD_RELOC_THUMB_PCREL_BRANCH9,
  BFD_RELOC_THUMB_PCREL_BRANCH12,
  BFD_RELOC_THUMB_PCREL_BRANCH20,
  BFD_RELOC_THUMB_PCREL_BRANCH23,
  BFD_RELOC_THUMB_PCREL_BRANCH25,

/* 12-bit immediate offset, used in ARM-format ldr and str instructions.  */
  BFD_RELOC_ARM_OFFSET_IMM,

/* 5-bit immediate offset, used in Thumb-format ldr and str instructions.  */
  BFD_RELOC_ARM_THUMB_OFFSET,

/* Pc-relative or absolute relocation depending on target.  Used for
entries in .init_array sections.  */
  BFD_RELOC_ARM_TARGET1,

/* Read-only segment base relative address.  */
  BFD_RELOC_ARM_ROSEGREL32,

/* Data segment base relative address.  */
  BFD_RELOC_ARM_SBREL32,

/* This reloc is used for references to RTTI data from exception handling
tables.  The actual definition depends on the target.  It may be a
pc-relative or some form of GOT-indirect relocation.  */
  BFD_RELOC_ARM_TARGET2,

/* 31-bit PC relative address.  */
  BFD_RELOC_ARM_PREL31,

/* Low and High halfword relocations for MOVW and MOVT instructions.  */
  BFD_RELOC_ARM_MOVW,
  BFD_RELOC_ARM_MOVT,
  BFD_RELOC_ARM_MOVW_PCREL,
  BFD_RELOC_ARM_MOVT_PCREL,
  BFD_RELOC_ARM_THUMB_MOVW,
  BFD_RELOC_ARM_THUMB_MOVT,
  BFD_RELOC_ARM_THUMB_MOVW_PCREL,
  BFD_RELOC_ARM_THUMB_MOVT_PCREL,

/* Relocations for setting up GOTs and PLTs for shared libraries.  */
  BFD_RELOC_ARM_JUMP_SLOT,
  BFD_RELOC_ARM_GLOB_DAT,
  BFD_RELOC_ARM_GOT32,
  BFD_RELOC_ARM_PLT32,
  BFD_RELOC_ARM_RELATIVE,
  BFD_RELOC_ARM_GOTOFF,
  BFD_RELOC_ARM_GOTPC,

/* ARM thread-local storage relocations.  */
  BFD_RELOC_ARM_TLS_GD32,
  BFD_RELOC_ARM_TLS_LDO32,
  BFD_RELOC_ARM_TLS_LDM32,
  BFD_RELOC_ARM_TLS_DTPOFF32,
  BFD_RELOC_ARM_TLS_DTPMOD32,
  BFD_RELOC_ARM_TLS_TPOFF32,
  BFD_RELOC_ARM_TLS_IE32,
  BFD_RELOC_ARM_TLS_LE32,

/* ARM group relocations.  */
  BFD_RELOC_ARM_ALU_PC_G0_NC,
  BFD_RELOC_ARM_ALU_PC_G0,
  BFD_RELOC_ARM_ALU_PC_G1_NC,
  BFD_RELOC_ARM_ALU_PC_G1,
  BFD_RELOC_ARM_ALU_PC_G2,
  BFD_RELOC_ARM_LDR_PC_G0,
  BFD_RELOC_ARM_LDR_PC_G1,
  BFD_RELOC_ARM_LDR_PC_G2,
  BFD_RELOC_ARM_LDRS_PC_G0,
  BFD_RELOC_ARM_LDRS_PC_G1,
  BFD_RELOC_ARM_LDRS_PC_G2,
  BFD_RELOC_ARM_LDC_PC_G0,
  BFD_RELOC_ARM_LDC_PC_G1,
  BFD_RELOC_ARM_LDC_PC_G2,
  BFD_RELOC_ARM_ALU_SB_G0_NC,
  BFD_RELOC_ARM_ALU_SB_G0,
  BFD_RELOC_ARM_ALU_SB_G1_NC,
  BFD_RELOC_ARM_ALU_SB_G1,
  BFD_RELOC_ARM_ALU_SB_G2,
  BFD_RELOC_ARM_LDR_SB_G0,
  BFD_RELOC_ARM_LDR_SB_G1,
  BFD_RELOC_ARM_LDR_SB_G2,
  BFD_RELOC_ARM_LDRS_SB_G0,
  BFD_RELOC_ARM_LDRS_SB_G1,
  BFD_RELOC_ARM_LDRS_SB_G2,
  BFD_RELOC_ARM_LDC_SB_G0,
  BFD_RELOC_ARM_LDC_SB_G1,
  BFD_RELOC_ARM_LDC_SB_G2,

/* These relocs are only used within the ARM assembler.  They are not
(at present) written to any object files.  */
  BFD_RELOC_ARM_IMMEDIATE,
  BFD_RELOC_ARM_ADRL_IMMEDIATE,
  BFD_RELOC_ARM_T32_IMMEDIATE,
  BFD_RELOC_ARM_T32_ADD_IMM,
  BFD_RELOC_ARM_T32_IMM12,
  BFD_RELOC_ARM_T32_ADD_PC12,
  BFD_RELOC_ARM_SHIFT_IMM,
  BFD_RELOC_ARM_SMC,
  BFD_RELOC_ARM_SWI,
  BFD_RELOC_ARM_MULTI,
  BFD_RELOC_ARM_CP_OFF_IMM,
  BFD_RELOC_ARM_CP_OFF_IMM_S2,
  BFD_RELOC_ARM_T32_CP_OFF_IMM,
  BFD_RELOC_ARM_T32_CP_OFF_IMM_S2,
  BFD_RELOC_ARM_ADR_IMM,
  BFD_RELOC_ARM_LDR_IMM,
  BFD_RELOC_ARM_LITERAL,
  BFD_RELOC_ARM_IN_POOL,
  BFD_RELOC_ARM_OFFSET_IMM8,
  BFD_RELOC_ARM_T32_OFFSET_U8,
  BFD_RELOC_ARM_T32_OFFSET_IMM,
  BFD_RELOC_ARM_HWLITERAL,
  BFD_RELOC_ARM_THUMB_ADD,
  BFD_RELOC_ARM_THUMB_IMM,
  BFD_RELOC_ARM_THUMB_SHIFT,

/* Renesas / SuperH SH relocs.  Not all of these appear in object files.  */
  BFD_RELOC_SH_PCDISP8BY2,
  BFD_RELOC_SH_PCDISP12BY2,
  BFD_RELOC_SH_IMM3,
  BFD_RELOC_SH_IMM3U,
  BFD_RELOC_SH_DISP12,
  BFD_RELOC_SH_DISP12BY2,
  BFD_RELOC_SH_DISP12BY4,
  BFD_RELOC_SH_DISP12BY8,
  BFD_RELOC_SH_DISP20,
  BFD_RELOC_SH_DISP20BY8,
  BFD_RELOC_SH_IMM4,
  BFD_RELOC_SH_IMM4BY2,
  BFD_RELOC_SH_IMM4BY4,
  BFD_RELOC_SH_IMM8,
  BFD_RELOC_SH_IMM8BY2,
  BFD_RELOC_SH_IMM8BY4,
  BFD_RELOC_SH_PCRELIMM8BY2,
  BFD_RELOC_SH_PCRELIMM8BY4,
  BFD_RELOC_SH_SWITCH16,
  BFD_RELOC_SH_SWITCH32,
  BFD_RELOC_SH_USES,
  BFD_RELOC_SH_COUNT,
  BFD_RELOC_SH_ALIGN,
  BFD_RELOC_SH_CODE,
  BFD_RELOC_SH_DATA,
  BFD_RELOC_SH_LABEL,
  BFD_RELOC_SH_LOOP_START,
  BFD_RELOC_SH_LOOP_END,
  BFD_RELOC_SH_COPY,
  BFD_RELOC_SH_GLOB_DAT,
  BFD_RELOC_SH_JMP_SLOT,
  BFD_RELOC_SH_RELATIVE,
  BFD_RELOC_SH_GOTPC,
  BFD_RELOC_SH_GOT_LOW16,
  BFD_RELOC_SH_GOT_MEDLOW16,
  BFD_RELOC_SH_GOT_MEDHI16,
  BFD_RELOC_SH_GOT_HI16,
  BFD_RELOC_SH_GOTPLT_LOW16,
  BFD_RELOC_SH_GOTPLT_MEDLOW16,
  BFD_RELOC_SH_GOTPLT_MEDHI16,
  BFD_RELOC_SH_GOTPLT_HI16,
  BFD_RELOC_SH_PLT_LOW16,
  BFD_RELOC_SH_PLT_MEDLOW16,
  BFD_RELOC_SH_PLT_MEDHI16,
  BFD_RELOC_SH_PLT_HI16,
  BFD_RELOC_SH_GOTOFF_LOW16,
  BFD_RELOC_SH_GOTOFF_MEDLOW16,
  BFD_RELOC_SH_GOTOFF_MEDHI16,
  BFD_RELOC_SH_GOTOFF_HI16,
  BFD_RELOC_SH_GOTPC_LOW16,
  BFD_RELOC_SH_GOTPC_MEDLOW16,
  BFD_RELOC_SH_GOTPC_MEDHI16,
  BFD_RELOC_SH_GOTPC_HI16,
  BFD_RELOC_SH_COPY64,
  BFD_RELOC_SH_GLOB_DAT64,
  BFD_RELOC_SH_JMP_SLOT64,
  BFD_RELOC_SH_RELATIVE64,
  BFD_RELOC_SH_GOT10BY4,
  BFD_RELOC_SH_GOT10BY8,
  BFD_RELOC_SH_GOTPLT10BY4,
  BFD_RELOC_SH_GOTPLT10BY8,
  BFD_RELOC_SH_GOTPLT32,
  BFD_RELOC_SH_SHMEDIA_CODE,
  BFD_RELOC_SH_IMMU5,
  BFD_RELOC_SH_IMMS6,
  BFD_RELOC_SH_IMMS6BY32,
  BFD_RELOC_SH_IMMU6,
  BFD_RELOC_SH_IMMS10,
  BFD_RELOC_SH_IMMS10BY2,
  BFD_RELOC_SH_IMMS10BY4,
  BFD_RELOC_SH_IMMS10BY8,
  BFD_RELOC_SH_IMMS16,
  BFD_RELOC_SH_IMMU16,
  BFD_RELOC_SH_IMM_LOW16,
  BFD_RELOC_SH_IMM_LOW16_PCREL,
  BFD_RELOC_SH_IMM_MEDLOW16,
  BFD_RELOC_SH_IMM_MEDLOW16_PCREL,
  BFD_RELOC_SH_IMM_MEDHI16,
  BFD_RELOC_SH_IMM_MEDHI16_PCREL,
  BFD_RELOC_SH_IMM_HI16,
  BFD_RELOC_SH_IMM_HI16_PCREL,
  BFD_RELOC_SH_PT_16,
  BFD_RELOC_SH_TLS_GD_32,
  BFD_RELOC_SH_TLS_LD_32,
  BFD_RELOC_SH_TLS_LDO_32,
  BFD_RELOC_SH_TLS_IE_32,
  BFD_RELOC_SH_TLS_LE_32,
  BFD_RELOC_SH_TLS_DTPMOD32,
  BFD_RELOC_SH_TLS_DTPOFF32,
  BFD_RELOC_SH_TLS_TPOFF32,

/* ARC Cores relocs.
ARC 22 bit pc-relative branch.  The lowest two bits must be zero and are
not stored in the instruction.  The high 20 bits are installed in bits 26
through 7 of the instruction.  */
  BFD_RELOC_ARC_B22_PCREL,

/* ARC 26 bit absolute branch.  The lowest two bits must be zero and are not
stored in the instruction.  The high 24 bits are installed in bits 23
through 0.  */
  BFD_RELOC_ARC_B26,

/* ADI Blackfin 16 bit immediate absolute reloc.  */
  BFD_RELOC_BFIN_16_IMM,

/* ADI Blackfin 16 bit immediate absolute reloc higher 16 bits.  */
  BFD_RELOC_BFIN_16_HIGH,

/* ADI Blackfin 'a' part of LSETUP.  */
  BFD_RELOC_BFIN_4_PCREL,

/* ADI Blackfin.  */
  BFD_RELOC_BFIN_5_PCREL,

/* ADI Blackfin 16 bit immediate absolute reloc lower 16 bits.  */
  BFD_RELOC_BFIN_16_LOW,

/* ADI Blackfin.  */
  BFD_RELOC_BFIN_10_PCREL,

/* ADI Blackfin 'b' part of LSETUP.  */
  BFD_RELOC_BFIN_11_PCREL,

/* ADI Blackfin.  */
  BFD_RELOC_BFIN_12_PCREL_JUMP,

/* ADI Blackfin Short jump, pcrel.  */
  BFD_RELOC_BFIN_12_PCREL_JUMP_S,

/* ADI Blackfin Call.x not implemented.  */
  BFD_RELOC_BFIN_24_PCREL_CALL_X,

/* ADI Blackfin Long Jump pcrel.  */
  BFD_RELOC_BFIN_24_PCREL_JUMP_L,

/* ADI Blackfin FD-PIC relocations.  */
  BFD_RELOC_BFIN_GOT17M4,
  BFD_RELOC_BFIN_GOTHI,
  BFD_RELOC_BFIN_GOTLO,
  BFD_RELOC_BFIN_FUNCDESC,
  BFD_RELOC_BFIN_FUNCDESC_GOT17M4,
  BFD_RELOC_BFIN_FUNCDESC_GOTHI,
  BFD_RELOC_BFIN_FUNCDESC_GOTLO,
  BFD_RELOC_BFIN_FUNCDESC_VALUE,
  BFD_RELOC_BFIN_FUNCDESC_GOTOFF17M4,
  BFD_RELOC_BFIN_FUNCDESC_GOTOFFHI,
  BFD_RELOC_BFIN_FUNCDESC_GOTOFFLO,
  BFD_RELOC_BFIN_GOTOFF17M4,
  BFD_RELOC_BFIN_GOTOFFHI,
  BFD_RELOC_BFIN_GOTOFFLO,

/* ADI Blackfin GOT relocation.  */
  BFD_RELOC_BFIN_GOT,

/* ADI Blackfin PLTPC relocation.  */
  BFD_RELOC_BFIN_PLTPC,

/* ADI Blackfin arithmetic relocation.  */
  BFD_ARELOC_BFIN_PUSH,

/* ADI Blackfin arithmetic relocation.  */
  BFD_ARELOC_BFIN_CONST,

/* ADI Blackfin arithmetic relocation.  */
  BFD_ARELOC_BFIN_ADD,

/* ADI Blackfin arithmetic relocation.  */
  BFD_ARELOC_BFIN_SUB,

/* ADI Blackfin arithmetic relocation.  */
  BFD_ARELOC_BFIN_MULT,

/* ADI Blackfin arithmetic relocation.  */
  BFD_ARELOC_BFIN_DIV,

/* ADI Blackfin arithmetic relocation.  */
  BFD_ARELOC_BFIN_MOD,

/* ADI Blackfin arithmetic relocation.  */
  BFD_ARELOC_BFIN_LSHIFT,

/* ADI Blackfin arithmetic relocation.  */
  BFD_ARELOC_BFIN_RSHIFT,

/* ADI Blackfin arithmetic relocation.  */
  BFD_ARELOC_BFIN_AND,

/* ADI Blackfin arithmetic relocation.  */
  BFD_ARELOC_BFIN_OR,

/* ADI Blackfin arithmetic relocation.  */
  BFD_ARELOC_BFIN_XOR,

/* ADI Blackfin arithmetic relocation.  */
  BFD_ARELOC_BFIN_LAND,

/* ADI Blackfin arithmetic relocation.  */
  BFD_ARELOC_BFIN_LOR,

/* ADI Blackfin arithmetic relocation.  */
  BFD_ARELOC_BFIN_LEN,

/* ADI Blackfin arithmetic relocation.  */
  BFD_ARELOC_BFIN_NEG,

/* ADI Blackfin arithmetic relocation.  */
  BFD_ARELOC_BFIN_COMP,

/* ADI Blackfin arithmetic relocation.  */
  BFD_ARELOC_BFIN_PAGE,

/* ADI Blackfin arithmetic relocation.  */
  BFD_ARELOC_BFIN_HWPAGE,

/* ADI Blackfin arithmetic relocation.  */
  BFD_ARELOC_BFIN_ADDR,

/* Mitsubishi D10V relocs.
This is a 10-bit reloc with the right 2 bits
assumed to be 0.  */
  BFD_RELOC_D10V_10_PCREL_R,

/* Mitsubishi D10V relocs.
This is a 10-bit reloc with the right 2 bits
assumed to be 0.  This is the same as the previous reloc
except it is in the left container, i.e.,
shifted left 15 bits.  */
  BFD_RELOC_D10V_10_PCREL_L,

/* This is an 18-bit reloc with the right 2 bits
assumed to be 0.  */
  BFD_RELOC_D10V_18,

/* This is an 18-bit reloc with the right 2 bits
assumed to be 0.  */
  BFD_RELOC_D10V_18_PCREL,

/* Mitsubishi D30V relocs.
This is a 6-bit absolute reloc.  */
  BFD_RELOC_D30V_6,

/* This is a 6-bit pc-relative reloc with
the right 3 bits assumed to be 0.  */
  BFD_RELOC_D30V_9_PCREL,

/* This is a 6-bit pc-relative reloc with
the right 3 bits assumed to be 0. Same
as the previous reloc but on the right side
of the container.  */
  BFD_RELOC_D30V_9_PCREL_R,

/* This is a 12-bit absolute reloc with the
right 3 bitsassumed to be 0.  */
  BFD_RELOC_D30V_15,

/* This is a 12-bit pc-relative reloc with
the right 3 bits assumed to be 0.  */
  BFD_RELOC_D30V_15_PCREL,

/* This is a 12-bit pc-relative reloc with
the right 3 bits assumed to be 0. Same
as the previous reloc but on the right side
of the container.  */
  BFD_RELOC_D30V_15_PCREL_R,

/* This is an 18-bit absolute reloc with
the right 3 bits assumed to be 0.  */
  BFD_RELOC_D30V_21,

/* This is an 18-bit pc-relative reloc with
the right 3 bits assumed to be 0.  */
  BFD_RELOC_D30V_21_PCREL,

/* This is an 18-bit pc-relative reloc with
the right 3 bits assumed to be 0. Same
as the previous reloc but on the right side
of the container.  */
  BFD_RELOC_D30V_21_PCREL_R,

/* This is a 32-bit absolute reloc.  */
  BFD_RELOC_D30V_32,

/* This is a 32-bit pc-relative reloc.  */
  BFD_RELOC_D30V_32_PCREL,

/* DLX relocs  */
  BFD_RELOC_DLX_HI16_S,

/* DLX relocs  */
  BFD_RELOC_DLX_LO16,

/* DLX relocs  */
  BFD_RELOC_DLX_JMP26,

/* Renesas M16C/M32C Relocations.  */
  BFD_RELOC_M32C_HI8,
  BFD_RELOC_M32C_RL_JUMP,
  BFD_RELOC_M32C_RL_1ADDR,
  BFD_RELOC_M32C_RL_2ADDR,

/* Renesas M32R (formerly Mitsubishi M32R) relocs.
This is a 24 bit absolute address.  */
  BFD_RELOC_M32R_24,

/* This is a 10-bit pc-relative reloc with the right 2 bits assumed to be 0.  */
  BFD_RELOC_M32R_10_PCREL,

/* This is an 18-bit reloc with the right 2 bits assumed to be 0.  */
  BFD_RELOC_M32R_18_PCREL,

/* This is a 26-bit reloc with the right 2 bits assumed to be 0.  */
  BFD_RELOC_M32R_26_PCREL,

/* This is a 16-bit reloc containing the high 16 bits of an address
used when the lower 16 bits are treated as unsigned.  */
  BFD_RELOC_M32R_HI16_ULO,

/* This is a 16-bit reloc containing the high 16 bits of an address
used when the lower 16 bits are treated as signed.  */
  BFD_RELOC_M32R_HI16_SLO,

/* This is a 16-bit reloc containing the lower 16 bits of an address.  */
  BFD_RELOC_M32R_LO16,

/* This is a 16-bit reloc containing the small data area offset for use in
add3, load, and store instructions.  */
  BFD_RELOC_M32R_SDA16,

/* For PIC.  */
  BFD_RELOC_M32R_GOT24,
  BFD_RELOC_M32R_26_PLTREL,
  BFD_RELOC_M32R_COPY,
  BFD_RELOC_M32R_GLOB_DAT,
  BFD_RELOC_M32R_JMP_SLOT,
  BFD_RELOC_M32R_RELATIVE,
  BFD_RELOC_M32R_GOTOFF,
  BFD_RELOC_M32R_GOTOFF_HI_ULO,
  BFD_RELOC_M32R_GOTOFF_HI_SLO,
  BFD_RELOC_M32R_GOTOFF_LO,
  BFD_RELOC_M32R_GOTPC24,
  BFD_RELOC_M32R_GOT16_HI_ULO,
  BFD_RELOC_M32R_GOT16_HI_SLO,
  BFD_RELOC_M32R_GOT16_LO,
  BFD_RELOC_M32R_GOTPC_HI_ULO,
  BFD_RELOC_M32R_GOTPC_HI_SLO,
  BFD_RELOC_M32R_GOTPC_LO,

/* This is a 9-bit reloc  */
  BFD_RELOC_V850_9_PCREL,

/* This is a 22-bit reloc  */
  BFD_RELOC_V850_22_PCREL,

/* This is a 16 bit offset from the short data area pointer.  */
  BFD_RELOC_V850_SDA_16_16_OFFSET,

/* This is a 16 bit offset (of which only 15 bits are used) from the
short data area pointer.  */
  BFD_RELOC_V850_SDA_15_16_OFFSET,

/* This is a 16 bit offset from the zero data area pointer.  */
  BFD_RELOC_V850_ZDA_16_16_OFFSET,

/* This is a 16 bit offset (of which only 15 bits are used) from the
zero data area pointer.  */
  BFD_RELOC_V850_ZDA_15_16_OFFSET,

/* This is an 8 bit offset (of which only 6 bits are used) from the
tiny data area pointer.  */
  BFD_RELOC_V850_TDA_6_8_OFFSET,

/* This is an 8bit offset (of which only 7 bits are used) from the tiny
data area pointer.  */
  BFD_RELOC_V850_TDA_7_8_OFFSET,

/* This is a 7 bit offset from the tiny data area pointer.  */
  BFD_RELOC_V850_TDA_7_7_OFFSET,

/* This is a 16 bit offset from the tiny data area pointer.  */
  BFD_RELOC_V850_TDA_16_16_OFFSET,

/* This is a 5 bit offset (of which only 4 bits are used) from the tiny
data area pointer.  */
  BFD_RELOC_V850_TDA_4_5_OFFSET,

/* This is a 4 bit offset from the tiny data area pointer.  */
  BFD_RELOC_V850_TDA_4_4_OFFSET,

/* This is a 16 bit offset from the short data area pointer, with the
bits placed non-contiguously in the instruction.  */
  BFD_RELOC_V850_SDA_16_16_SPLIT_OFFSET,

/* This is a 16 bit offset from the zero data area pointer, with the
bits placed non-contiguously in the instruction.  */
  BFD_RELOC_V850_ZDA_16_16_SPLIT_OFFSET,

/* This is a 6 bit offset from the call table base pointer.  */
  BFD_RELOC_V850_CALLT_6_7_OFFSET,

/* This is a 16 bit offset from the call table base pointer.  */
  BFD_RELOC_V850_CALLT_16_16_OFFSET,

/* Used for relaxing indirect function calls.  */
  BFD_RELOC_V850_LONGCALL,

/* Used for relaxing indirect jumps.  */
  BFD_RELOC_V850_LONGJUMP,

/* Used to maintain alignment whilst relaxing.  */
  BFD_RELOC_V850_ALIGN,

/* This is a variation of BFD_RELOC_LO16 that can be used in v850e ld.bu
instructions.  */
  BFD_RELOC_V850_LO16_SPLIT_OFFSET,

/* This is a 32bit pcrel reloc for the mn10300, offset by two bytes in the
instruction.  */
  BFD_RELOC_MN10300_32_PCREL,

/* This is a 16bit pcrel reloc for the mn10300, offset by two bytes in the
instruction.  */
  BFD_RELOC_MN10300_16_PCREL,

/* This is a 8bit DP reloc for the tms320c30, where the most
significant 8 bits of a 24 bit word are placed into the least
significant 8 bits of the opcode.  */
  BFD_RELOC_TIC30_LDP,

/* This is a 7bit reloc for the tms320c54x, where the least
significant 7 bits of a 16 bit word are placed into the least
significant 7 bits of the opcode.  */
  BFD_RELOC_TIC54X_PARTLS7,

/* This is a 9bit DP reloc for the tms320c54x, where the most
significant 9 bits of a 16 bit word are placed into the least
significant 9 bits of the opcode.  */
  BFD_RELOC_TIC54X_PARTMS9,

/* This is an extended address 23-bit reloc for the tms320c54x.  */
  BFD_RELOC_TIC54X_23,

/* This is a 16-bit reloc for the tms320c54x, where the least
significant 16 bits of a 23-bit extended address are placed into
the opcode.  */
  BFD_RELOC_TIC54X_16_OF_23,

/* This is a reloc for the tms320c54x, where the most
significant 7 bits of a 23-bit extended address are placed into
the opcode.  */
  BFD_RELOC_TIC54X_MS7_OF_23,

/* This is a 48 bit reloc for the FR30 that stores 32 bits.  */
  BFD_RELOC_FR30_48,

/* This is a 32 bit reloc for the FR30 that stores 20 bits split up into
two sections.  */
  BFD_RELOC_FR30_20,

/* This is a 16 bit reloc for the FR30 that stores a 6 bit word offset in
4 bits.  */
  BFD_RELOC_FR30_6_IN_4,

/* This is a 16 bit reloc for the FR30 that stores an 8 bit byte offset
into 8 bits.  */
  BFD_RELOC_FR30_8_IN_8,

/* This is a 16 bit reloc for the FR30 that stores a 9 bit short offset
into 8 bits.  */
  BFD_RELOC_FR30_9_IN_8,

/* This is a 16 bit reloc for the FR30 that stores a 10 bit word offset
into 8 bits.  */
  BFD_RELOC_FR30_10_IN_8,

/* This is a 16 bit reloc for the FR30 that stores a 9 bit pc relative
short offset into 8 bits.  */
  BFD_RELOC_FR30_9_PCREL,

/* This is a 16 bit reloc for the FR30 that stores a 12 bit pc relative
short offset into 11 bits.  */
  BFD_RELOC_FR30_12_PCREL,

/* Motorola Mcore relocations.  */
  BFD_RELOC_MCORE_PCREL_IMM8BY4,
  BFD_RELOC_MCORE_PCREL_IMM11BY2,
  BFD_RELOC_MCORE_PCREL_IMM4BY2,
  BFD_RELOC_MCORE_PCREL_32,
  BFD_RELOC_MCORE_PCREL_JSR_IMM11BY2,
  BFD_RELOC_MCORE_RVA,

/* Toshiba Media Processor Relocations.  */
  BFD_RELOC_MEP_8,
  BFD_RELOC_MEP_16,
  BFD_RELOC_MEP_32,
  BFD_RELOC_MEP_PCREL8A2,
  BFD_RELOC_MEP_PCREL12A2,
  BFD_RELOC_MEP_PCREL17A2,
  BFD_RELOC_MEP_PCREL24A2,
  BFD_RELOC_MEP_PCABS24A2,
  BFD_RELOC_MEP_LOW16,
  BFD_RELOC_MEP_HI16U,
  BFD_RELOC_MEP_HI16S,
  BFD_RELOC_MEP_GPREL,
  BFD_RELOC_MEP_TPREL,
  BFD_RELOC_MEP_TPREL7,
  BFD_RELOC_MEP_TPREL7A2,
  BFD_RELOC_MEP_TPREL7A4,
  BFD_RELOC_MEP_UIMM24,
  BFD_RELOC_MEP_ADDR24A4,
  BFD_RELOC_MEP_GNU_VTINHERIT,
  BFD_RELOC_MEP_GNU_VTENTRY,


/* These are relocations for the GETA instruction.  */
  BFD_RELOC_MMIX_GETA,
  BFD_RELOC_MMIX_GETA_1,
  BFD_RELOC_MMIX_GETA_2,
  BFD_RELOC_MMIX_GETA_3,

/* These are relocations for a conditional branch instruction.  */
  BFD_RELOC_MMIX_CBRANCH,
  BFD_RELOC_MMIX_CBRANCH_J,
  BFD_RELOC_MMIX_CBRANCH_1,
  BFD_RELOC_MMIX_CBRANCH_2,
  BFD_RELOC_MMIX_CBRANCH_3,

/* These are relocations for the PUSHJ instruction.  */
  BFD_RELOC_MMIX_PUSHJ,
  BFD_RELOC_MMIX_PUSHJ_1,
  BFD_RELOC_MMIX_PUSHJ_2,
  BFD_RELOC_MMIX_PUSHJ_3,
  BFD_RELOC_MMIX_PUSHJ_STUBBABLE,

/* These are relocations for the JMP instruction.  */
  BFD_RELOC_MMIX_JMP,
  BFD_RELOC_MMIX_JMP_1,
  BFD_RELOC_MMIX_JMP_2,
  BFD_RELOC_MMIX_JMP_3,

/* This is a relocation for a relative address as in a GETA instruction or
a branch.  */
  BFD_RELOC_MMIX_ADDR19,

/* This is a relocation for a relative address as in a JMP instruction.  */
  BFD_RELOC_MMIX_ADDR27,

/* This is a relocation for an instruction field that may be a general
register or a value 0..255.  */
  BFD_RELOC_MMIX_REG_OR_BYTE,

/* This is a relocation for an instruction field that may be a general
register.  */
  BFD_RELOC_MMIX_REG,

/* This is a relocation for two instruction fields holding a register and
an offset, the equivalent of the relocation.  */
  BFD_RELOC_MMIX_BASE_PLUS_OFFSET,

/* This relocation is an assertion that the expression is not allocated as
a global register.  It does not modify contents.  */
  BFD_RELOC_MMIX_LOCAL,

/* This is a 16 bit reloc for the AVR that stores 8 bit pc relative
short offset into 7 bits.  */
  BFD_RELOC_AVR_7_PCREL,

/* This is a 16 bit reloc for the AVR that stores 13 bit pc relative
short offset into 12 bits.  */
  BFD_RELOC_AVR_13_PCREL,

/* This is a 16 bit reloc for the AVR that stores 17 bit value (usually
program memory address) into 16 bits.  */
  BFD_RELOC_AVR_16_PM,

/* This is a 16 bit reloc for the AVR that stores 8 bit value (usually
data memory address) into 8 bit immediate value of LDI insn.  */
  BFD_RELOC_AVR_LO8_LDI,

/* This is a 16 bit reloc for the AVR that stores 8 bit value (high 8 bit
of data memory address) into 8 bit immediate value of LDI insn.  */
  BFD_RELOC_AVR_HI8_LDI,

/* This is a 16 bit reloc for the AVR that stores 8 bit value (most high 8 bit
of program memory address) into 8 bit immediate value of LDI insn.  */
  BFD_RELOC_AVR_HH8_LDI,

/* This is a 16 bit reloc for the AVR that stores 8 bit value (most high 8 bit
of 32 bit value) into 8 bit immediate value of LDI insn.  */
  BFD_RELOC_AVR_MS8_LDI,

/* This is a 16 bit reloc for the AVR that stores negated 8 bit value
(usually data memory address) into 8 bit immediate value of SUBI insn.  */
  BFD_RELOC_AVR_LO8_LDI_NEG,

/* This is a 16 bit reloc for the AVR that stores negated 8 bit value
(high 8 bit of data memory address) into 8 bit immediate value of
SUBI insn.  */
  BFD_RELOC_AVR_HI8_LDI_NEG,

/* This is a 16 bit reloc for the AVR that stores negated 8 bit value
(most high 8 bit of program memory address) into 8 bit immediate value
of LDI or SUBI insn.  */
  BFD_RELOC_AVR_HH8_LDI_NEG,

/* This is a 16 bit reloc for the AVR that stores negated 8 bit value (msb
of 32 bit value) into 8 bit immediate value of LDI insn.  */
  BFD_RELOC_AVR_MS8_LDI_NEG,

/* This is a 16 bit reloc for the AVR that stores 8 bit value (usually
command address) into 8 bit immediate value of LDI insn.  */
  BFD_RELOC_AVR_LO8_LDI_PM,

/* This is a 16 bit reloc for the AVR that stores 8 bit value 
(command address) into 8 bit immediate value of LDI insn. If the address
is beyond the 128k boundary, the linker inserts a jump stub for this reloc
in the lower 128k.  */
  BFD_RELOC_AVR_LO8_LDI_GS,

/* This is a 16 bit reloc for the AVR that stores 8 bit value (high 8 bit
of command address) into 8 bit immediate value of LDI insn.  */
  BFD_RELOC_AVR_HI8_LDI_PM,

/* This is a 16 bit reloc for the AVR that stores 8 bit value (high 8 bit
of command address) into 8 bit immediate value of LDI insn.  If the address
is beyond the 128k boundary, the linker inserts a jump stub for this reloc
below 128k.  */
  BFD_RELOC_AVR_HI8_LDI_GS,

/* This is a 16 bit reloc for the AVR that stores 8 bit value (most high 8 bit
of command address) into 8 bit immediate value of LDI insn.  */
  BFD_RELOC_AVR_HH8_LDI_PM,

/* This is a 16 bit reloc for the AVR that stores negated 8 bit value
(usually command address) into 8 bit immediate value of SUBI insn.  */
  BFD_RELOC_AVR_LO8_LDI_PM_NEG,

/* This is a 16 bit reloc for the AVR that stores negated 8 bit value
(high 8 bit of 16 bit command address) into 8 bit immediate value
of SUBI insn.  */
  BFD_RELOC_AVR_HI8_LDI_PM_NEG,

/* This is a 16 bit reloc for the AVR that stores negated 8 bit value
(high 6 bit of 22 bit command address) into 8 bit immediate
value of SUBI insn.  */
  BFD_RELOC_AVR_HH8_LDI_PM_NEG,

/* This is a 32 bit reloc for the AVR that stores 23 bit value
into 22 bits.  */
  BFD_RELOC_AVR_CALL,

/* This is a 16 bit reloc for the AVR that stores all needed bits
for absolute addressing with ldi with overflow check to linktime  */
  BFD_RELOC_AVR_LDI,

/* This is a 6 bit reloc for the AVR that stores offset for ldd/std
instructions  */
  BFD_RELOC_AVR_6,

/* This is a 6 bit reloc for the AVR that stores offset for adiw/sbiw
instructions  */
  BFD_RELOC_AVR_6_ADIW,

/* Direct 12 bit.  */
  BFD_RELOC_390_12,

/* 12 bit GOT offset.  */
  BFD_RELOC_390_GOT12,

/* 32 bit PC relative PLT address.  */
  BFD_RELOC_390_PLT32,

/* Copy symbol at runtime.  */
  BFD_RELOC_390_COPY,

/* Create GOT entry.  */
  BFD_RELOC_390_GLOB_DAT,

/* Create PLT entry.  */
  BFD_RELOC_390_JMP_SLOT,

/* Adjust by program base.  */
  BFD_RELOC_390_RELATIVE,

/* 32 bit PC relative offset to GOT.  */
  BFD_RELOC_390_GOTPC,

/* 16 bit GOT offset.  */
  BFD_RELOC_390_GOT16,

/* PC relative 16 bit shifted by 1.  */
  BFD_RELOC_390_PC16DBL,

/* 16 bit PC rel. PLT shifted by 1.  */
  BFD_RELOC_390_PLT16DBL,

/* PC relative 32 bit shifted by 1.  */
  BFD_RELOC_390_PC32DBL,

/* 32 bit PC rel. PLT shifted by 1.  */
  BFD_RELOC_390_PLT32DBL,

/* 32 bit PC rel. GOT shifted by 1.  */
  BFD_RELOC_390_GOTPCDBL,

/* 64 bit GOT offset.  */
  BFD_RELOC_390_GOT64,

/* 64 bit PC relative PLT address.  */
  BFD_RELOC_390_PLT64,

/* 32 bit rel. offset to GOT entry.  */
  BFD_RELOC_390_GOTENT,

/* 64 bit offset to GOT.  */
  BFD_RELOC_390_GOTOFF64,

/* 12-bit offset to symbol-entry within GOT, with PLT handling.  */
  BFD_RELOC_390_GOTPLT12,

/* 16-bit offset to symbol-entry within GOT, with PLT handling.  */
  BFD_RELOC_390_GOTPLT16,

/* 32-bit offset to symbol-entry within GOT, with PLT handling.  */
  BFD_RELOC_390_GOTPLT32,

/* 64-bit offset to symbol-entry within GOT, with PLT handling.  */
  BFD_RELOC_390_GOTPLT64,

/* 32-bit rel. offset to symbol-entry within GOT, with PLT handling.  */
  BFD_RELOC_390_GOTPLTENT,

/* 16-bit rel. offset from the GOT to a PLT entry.  */
  BFD_RELOC_390_PLTOFF16,

/* 32-bit rel. offset from the GOT to a PLT entry.  */
  BFD_RELOC_390_PLTOFF32,

/* 64-bit rel. offset from the GOT to a PLT entry.  */
  BFD_RELOC_390_PLTOFF64,

/* s390 tls relocations.  */
  BFD_RELOC_390_TLS_LOAD,
  BFD_RELOC_390_TLS_GDCALL,
  BFD_RELOC_390_TLS_LDCALL,
  BFD_RELOC_390_TLS_GD32,
  BFD_RELOC_390_TLS_GD64,
  BFD_RELOC_390_TLS_GOTIE12,
  BFD_RELOC_390_TLS_GOTIE32,
  BFD_RELOC_390_TLS_GOTIE64,
  BFD_RELOC_390_TLS_LDM32,
  BFD_RELOC_390_TLS_LDM64,
  BFD_RELOC_390_TLS_IE32,
  BFD_RELOC_390_TLS_IE64,
  BFD_RELOC_390_TLS_IEENT,
  BFD_RELOC_390_TLS_LE32,
  BFD_RELOC_390_TLS_LE64,
  BFD_RELOC_390_TLS_LDO32,
  BFD_RELOC_390_TLS_LDO64,
  BFD_RELOC_390_TLS_DTPMOD,
  BFD_RELOC_390_TLS_DTPOFF,
  BFD_RELOC_390_TLS_TPOFF,

/* Long displacement extension.  */
  BFD_RELOC_390_20,
  BFD_RELOC_390_GOT20,
  BFD_RELOC_390_GOTPLT20,
  BFD_RELOC_390_TLS_GOTIE20,

/* Score relocations  */
  BFD_RELOC_SCORE_DUMMY1,

/* Low 16 bit for load/store  */
  BFD_RELOC_SCORE_GPREL15,

/* This is a 24-bit reloc with the right 1 bit assumed to be 0  */
  BFD_RELOC_SCORE_DUMMY2,
  BFD_RELOC_SCORE_JMP,

/* This is a 19-bit reloc with the right 1 bit assumed to be 0  */
  BFD_RELOC_SCORE_BRANCH,

/* This is a 11-bit reloc with the right 1 bit assumed to be 0  */
  BFD_RELOC_SCORE16_JMP,

/* This is a 8-bit reloc with the right 1 bit assumed to be 0  */
  BFD_RELOC_SCORE16_BRANCH,

/* Undocumented Score relocs  */
  BFD_RELOC_SCORE_GOT15,
  BFD_RELOC_SCORE_GOT_LO16,
  BFD_RELOC_SCORE_CALL15,
  BFD_RELOC_SCORE_DUMMY_HI16,

/* Scenix IP2K - 9-bit register number / data address  */
  BFD_RELOC_IP2K_FR9,

/* Scenix IP2K - 4-bit register/data bank number  */
  BFD_RELOC_IP2K_BANK,

/* Scenix IP2K - low 13 bits of instruction word address  */
  BFD_RELOC_IP2K_ADDR16CJP,

/* Scenix IP2K - high 3 bits of instruction word address  */
  BFD_RELOC_IP2K_PAGE3,

/* Scenix IP2K - ext/low/high 8 bits of data address  */
  BFD_RELOC_IP2K_LO8DATA,
  BFD_RELOC_IP2K_HI8DATA,
  BFD_RELOC_IP2K_EX8DATA,

/* Scenix IP2K - low/high 8 bits of instruction word address  */
  BFD_RELOC_IP2K_LO8INSN,
  BFD_RELOC_IP2K_HI8INSN,

/* Scenix IP2K - even/odd PC modifier to modify snb pcl.0  */
  BFD_RELOC_IP2K_PC_SKIP,

/* Scenix IP2K - 16 bit word address in text section.  */
  BFD_RELOC_IP2K_TEXT,

/* Scenix IP2K - 7-bit sp or dp offset  */
  BFD_RELOC_IP2K_FR_OFFSET,

/* Scenix VPE4K coprocessor - data/insn-space addressing  */
  BFD_RELOC_VPE4KMATH_DATA,
  BFD_RELOC_VPE4KMATH_INSN,

/* These two relocations are used by the linker to determine which of
the entries in a C++ virtual function table are actually used.  When
the --gc-sections option is given, the linker will zero out the entries
that are not used, so that the code for those functions need not be
included in the output.

VTABLE_INHERIT is a zero-space relocation used to describe to the
linker the inheritance tree of a C++ virtual function table.  The
relocation's symbol should be the parent class' vtable, and the
relocation should be located at the child vtable.

VTABLE_ENTRY is a zero-space relocation that describes the use of a
virtual function table entry.  The reloc's symbol should refer to the
table of the class mentioned in the code.  Off of that base, an offset
describes the entry that is being used.  For Rela hosts, this offset
is stored in the reloc's addend.  For Rel hosts, we are forced to put
this offset in the reloc's section offset.  */
  BFD_RELOC_VTABLE_INHERIT,
  BFD_RELOC_VTABLE_ENTRY,

/* Intel IA64 Relocations.  */
  BFD_RELOC_IA64_IMM14,
  BFD_RELOC_IA64_IMM22,
  BFD_RELOC_IA64_IMM64,
  BFD_RELOC_IA64_DIR32MSB,
  BFD_RELOC_IA64_DIR32LSB,
  BFD_RELOC_IA64_DIR64MSB,
  BFD_RELOC_IA64_DIR64LSB,
  BFD_RELOC_IA64_GPREL22,
  BFD_RELOC_IA64_GPREL64I,
  BFD_RELOC_IA64_GPREL32MSB,
  BFD_RELOC_IA64_GPREL32LSB,
  BFD_RELOC_IA64_GPREL64MSB,
  BFD_RELOC_IA64_GPREL64LSB,
  BFD_RELOC_IA64_LTOFF22,
  BFD_RELOC_IA64_LTOFF64I,
  BFD_RELOC_IA64_PLTOFF22,
  BFD_RELOC_IA64_PLTOFF64I,
  BFD_RELOC_IA64_PLTOFF64MSB,
  BFD_RELOC_IA64_PLTOFF64LSB,
  BFD_RELOC_IA64_FPTR64I,
  BFD_RELOC_IA64_FPTR32MSB,
  BFD_RELOC_IA64_FPTR32LSB,
  BFD_RELOC_IA64_FPTR64MSB,
  BFD_RELOC_IA64_FPTR64LSB,
  BFD_RELOC_IA64_PCREL21B,
  BFD_RELOC_IA64_PCREL21BI,
  BFD_RELOC_IA64_PCREL21M,
  BFD_RELOC_IA64_PCREL21F,
  BFD_RELOC_IA64_PCREL22,
  BFD_RELOC_IA64_PCREL60B,
  BFD_RELOC_IA64_PCREL64I,
  BFD_RELOC_IA64_PCREL32MSB,
  BFD_RELOC_IA64_PCREL32LSB,
  BFD_RELOC_IA64_PCREL64MSB,
  BFD_RELOC_IA64_PCREL64LSB,
  BFD_RELOC_IA64_LTOFF_FPTR22,
  BFD_RELOC_IA64_LTOFF_FPTR64I,
  BFD_RELOC_IA64_LTOFF_FPTR32MSB,
  BFD_RELOC_IA64_LTOFF_FPTR32LSB,
  BFD_RELOC_IA64_LTOFF_FPTR64MSB,
  BFD_RELOC_IA64_LTOFF_FPTR64LSB,
  BFD_RELOC_IA64_SEGREL32MSB,
  BFD_RELOC_IA64_SEGREL32LSB,
  BFD_RELOC_IA64_SEGREL64MSB,
  BFD_RELOC_IA64_SEGREL64LSB,
  BFD_RELOC_IA64_SECREL32MSB,
  BFD_RELOC_IA64_SECREL32LSB,
  BFD_RELOC_IA64_SECREL64MSB,
  BFD_RELOC_IA64_SECREL64LSB,
  BFD_RELOC_IA64_REL32MSB,
  BFD_RELOC_IA64_REL32LSB,
  BFD_RELOC_IA64_REL64MSB,
  BFD_RELOC_IA64_REL64LSB,
  BFD_RELOC_IA64_LTV32MSB,
  BFD_RELOC_IA64_LTV32LSB,
  BFD_RELOC_IA64_LTV64MSB,
  BFD_RELOC_IA64_LTV64LSB,
  BFD_RELOC_IA64_IPLTMSB,
  BFD_RELOC_IA64_IPLTLSB,
  BFD_RELOC_IA64_COPY,
  BFD_RELOC_IA64_LTOFF22X,
  BFD_RELOC_IA64_LDXMOV,
  BFD_RELOC_IA64_TPREL14,
  BFD_RELOC_IA64_TPREL22,
  BFD_RELOC_IA64_TPREL64I,
  BFD_RELOC_IA64_TPREL64MSB,
  BFD_RELOC_IA64_TPREL64LSB,
  BFD_RELOC_IA64_LTOFF_TPREL22,
  BFD_RELOC_IA64_DTPMOD64MSB,
  BFD_RELOC_IA64_DTPMOD64LSB,
  BFD_RELOC_IA64_LTOFF_DTPMOD22,
  BFD_RELOC_IA64_DTPREL14,
  BFD_RELOC_IA64_DTPREL22,
  BFD_RELOC_IA64_DTPREL64I,
  BFD_RELOC_IA64_DTPREL32MSB,
  BFD_RELOC_IA64_DTPREL32LSB,
  BFD_RELOC_IA64_DTPREL64MSB,
  BFD_RELOC_IA64_DTPREL64LSB,
  BFD_RELOC_IA64_LTOFF_DTPREL22,

/* Motorola 68HC11 reloc.
This is the 8 bit high part of an absolute address.  */
  BFD_RELOC_M68HC11_HI8,

/* Motorola 68HC11 reloc.
This is the 8 bit low part of an absolute address.  */
  BFD_RELOC_M68HC11_LO8,

/* Motorola 68HC11 reloc.
This is the 3 bit of a value.  */
  BFD_RELOC_M68HC11_3B,

/* Motorola 68HC11 reloc.
This reloc marks the beginning of a jump/call instruction.
It is used for linker relaxation to correctly identify beginning
of instruction and change some branches to use PC-relative
addressing mode.  */
  BFD_RELOC_M68HC11_RL_JUMP,

/* Motorola 68HC11 reloc.
This reloc marks a group of several instructions that gcc generates
and for which the linker relaxation pass can modify and/or remove
some of them.  */
  BFD_RELOC_M68HC11_RL_GROUP,

/* Motorola 68HC11 reloc.
This is the 16-bit lower part of an address.  It is used for 'call'
instruction to specify the symbol address without any special
transformation (due to memory bank window).  */
  BFD_RELOC_M68HC11_LO16,

/* Motorola 68HC11 reloc.
This is a 8-bit reloc that specifies the page number of an address.
It is used by 'call' instruction to specify the page number of
the symbol.  */
  BFD_RELOC_M68HC11_PAGE,

/* Motorola 68HC11 reloc.
This is a 24-bit reloc that represents the address with a 16-bit
value and a 8-bit page number.  The symbol address is transformed
to follow the 16K memory bank of 68HC12 (seen as mapped in the window).  */
  BFD_RELOC_M68HC11_24,

/* Motorola 68HC12 reloc.
This is the 5 bits of a value.  */
  BFD_RELOC_M68HC12_5B,

/* NS CR16C Relocations.  */
  BFD_RELOC_16C_NUM08,
  BFD_RELOC_16C_NUM08_C,
  BFD_RELOC_16C_NUM16,
  BFD_RELOC_16C_NUM16_C,
  BFD_RELOC_16C_NUM32,
  BFD_RELOC_16C_NUM32_C,
  BFD_RELOC_16C_DISP04,
  BFD_RELOC_16C_DISP04_C,
  BFD_RELOC_16C_DISP08,
  BFD_RELOC_16C_DISP08_C,
  BFD_RELOC_16C_DISP16,
  BFD_RELOC_16C_DISP16_C,
  BFD_RELOC_16C_DISP24,
  BFD_RELOC_16C_DISP24_C,
  BFD_RELOC_16C_DISP24a,
  BFD_RELOC_16C_DISP24a_C,
  BFD_RELOC_16C_REG04,
  BFD_RELOC_16C_REG04_C,
  BFD_RELOC_16C_REG04a,
  BFD_RELOC_16C_REG04a_C,
  BFD_RELOC_16C_REG14,
  BFD_RELOC_16C_REG14_C,
  BFD_RELOC_16C_REG16,
  BFD_RELOC_16C_REG16_C,
  BFD_RELOC_16C_REG20,
  BFD_RELOC_16C_REG20_C,
  BFD_RELOC_16C_ABS20,
  BFD_RELOC_16C_ABS20_C,
  BFD_RELOC_16C_ABS24,
  BFD_RELOC_16C_ABS24_C,
  BFD_RELOC_16C_IMM04,
  BFD_RELOC_16C_IMM04_C,
  BFD_RELOC_16C_IMM16,
  BFD_RELOC_16C_IMM16_C,
  BFD_RELOC_16C_IMM20,
  BFD_RELOC_16C_IMM20_C,
  BFD_RELOC_16C_IMM24,
  BFD_RELOC_16C_IMM24_C,
  BFD_RELOC_16C_IMM32,
  BFD_RELOC_16C_IMM32_C,

/* NS CR16 Relocations.  */
  BFD_RELOC_CR16_NUM8,
  BFD_RELOC_CR16_NUM16,
  BFD_RELOC_CR16_NUM32,
  BFD_RELOC_CR16_NUM32a,
  BFD_RELOC_CR16_REGREL0,
  BFD_RELOC_CR16_REGREL4,
  BFD_RELOC_CR16_REGREL4a,
  BFD_RELOC_CR16_REGREL14,
  BFD_RELOC_CR16_REGREL14a,
  BFD_RELOC_CR16_REGREL16,
  BFD_RELOC_CR16_REGREL20,
  BFD_RELOC_CR16_REGREL20a,
  BFD_RELOC_CR16_ABS20,
  BFD_RELOC_CR16_ABS24,
  BFD_RELOC_CR16_IMM4,
  BFD_RELOC_CR16_IMM8,
  BFD_RELOC_CR16_IMM16,
  BFD_RELOC_CR16_IMM20,
  BFD_RELOC_CR16_IMM24,
  BFD_RELOC_CR16_IMM32,
  BFD_RELOC_CR16_IMM32a,
  BFD_RELOC_CR16_DISP4,
  BFD_RELOC_CR16_DISP8,
  BFD_RELOC_CR16_DISP16,
  BFD_RELOC_CR16_DISP20,
  BFD_RELOC_CR16_DISP24,
  BFD_RELOC_CR16_DISP24a,

/* NS CRX Relocations.  */
  BFD_RELOC_CRX_REL4,
  BFD_RELOC_CRX_REL8,
  BFD_RELOC_CRX_REL8_CMP,
  BFD_RELOC_CRX_REL16,
  BFD_RELOC_CRX_REL24,
  BFD_RELOC_CRX_REL32,
  BFD_RELOC_CRX_REGREL12,
  BFD_RELOC_CRX_REGREL22,
  BFD_RELOC_CRX_REGREL28,
  BFD_RELOC_CRX_REGREL32,
  BFD_RELOC_CRX_ABS16,
  BFD_RELOC_CRX_ABS32,
  BFD_RELOC_CRX_NUM8,
  BFD_RELOC_CRX_NUM16,
  BFD_RELOC_CRX_NUM32,
  BFD_RELOC_CRX_IMM16,
  BFD_RELOC_CRX_IMM32,
  BFD_RELOC_CRX_SWITCH8,
  BFD_RELOC_CRX_SWITCH16,
  BFD_RELOC_CRX_SWITCH32,

/* These relocs are only used within the CRIS assembler.  They are not
(at present) written to any object files.  */
  BFD_RELOC_CRIS_BDISP8,
  BFD_RELOC_CRIS_UNSIGNED_5,
  BFD_RELOC_CRIS_SIGNED_6,
  BFD_RELOC_CRIS_UNSIGNED_6,
  BFD_RELOC_CRIS_SIGNED_8,
  BFD_RELOC_CRIS_UNSIGNED_8,
  BFD_RELOC_CRIS_SIGNED_16,
  BFD_RELOC_CRIS_UNSIGNED_16,
  BFD_RELOC_CRIS_LAPCQ_OFFSET,
  BFD_RELOC_CRIS_UNSIGNED_4,

/* Relocs used in ELF shared libraries for CRIS.  */
  BFD_RELOC_CRIS_COPY,
  BFD_RELOC_CRIS_GLOB_DAT,
  BFD_RELOC_CRIS_JUMP_SLOT,
  BFD_RELOC_CRIS_RELATIVE,

/* 32-bit offset to symbol-entry within GOT.  */
  BFD_RELOC_CRIS_32_GOT,

/* 16-bit offset to symbol-entry within GOT.  */
  BFD_RELOC_CRIS_16_GOT,

/* 32-bit offset to symbol-entry within GOT, with PLT handling.  */
  BFD_RELOC_CRIS_32_GOTPLT,

/* 16-bit offset to symbol-entry within GOT, with PLT handling.  */
  BFD_RELOC_CRIS_16_GOTPLT,

/* 32-bit offset to symbol, relative to GOT.  */
  BFD_RELOC_CRIS_32_GOTREL,

/* 32-bit offset to symbol with PLT entry, relative to GOT.  */
  BFD_RELOC_CRIS_32_PLT_GOTREL,

/* 32-bit offset to symbol with PLT entry, relative to this relocation.  */
  BFD_RELOC_CRIS_32_PLT_PCREL,

/* Intel i860 Relocations.  */
  BFD_RELOC_860_COPY,
  BFD_RELOC_860_GLOB_DAT,
  BFD_RELOC_860_JUMP_SLOT,
  BFD_RELOC_860_RELATIVE,
  BFD_RELOC_860_PC26,
  BFD_RELOC_860_PLT26,
  BFD_RELOC_860_PC16,
  BFD_RELOC_860_LOW0,
  BFD_RELOC_860_SPLIT0,
  BFD_RELOC_860_LOW1,
  BFD_RELOC_860_SPLIT1,
  BFD_RELOC_860_LOW2,
  BFD_RELOC_860_SPLIT2,
  BFD_RELOC_860_LOW3,
  BFD_RELOC_860_LOGOT0,
  BFD_RELOC_860_SPGOT0,
  BFD_RELOC_860_LOGOT1,
  BFD_RELOC_860_SPGOT1,
  BFD_RELOC_860_LOGOTOFF0,
  BFD_RELOC_860_SPGOTOFF0,
  BFD_RELOC_860_LOGOTOFF1,
  BFD_RELOC_860_SPGOTOFF1,
  BFD_RELOC_860_LOGOTOFF2,
  BFD_RELOC_860_LOGOTOFF3,
  BFD_RELOC_860_LOPC,
  BFD_RELOC_860_HIGHADJ,
  BFD_RELOC_860_HAGOT,
  BFD_RELOC_860_HAGOTOFF,
  BFD_RELOC_860_HAPC,
  BFD_RELOC_860_HIGH,
  BFD_RELOC_860_HIGOT,
  BFD_RELOC_860_HIGOTOFF,

/* OpenRISC Relocations.  */
  BFD_RELOC_OPENRISC_ABS_26,
  BFD_RELOC_OPENRISC_REL_26,

/* H8 elf Relocations.  */
  BFD_RELOC_H8_DIR16A8,
  BFD_RELOC_H8_DIR16R8,
  BFD_RELOC_H8_DIR24A8,
  BFD_RELOC_H8_DIR24R8,
  BFD_RELOC_H8_DIR32A16,

/* Sony Xstormy16 Relocations.  */
  BFD_RELOC_XSTORMY16_REL_12,
  BFD_RELOC_XSTORMY16_12,
  BFD_RELOC_XSTORMY16_24,
  BFD_RELOC_XSTORMY16_FPTR16,

/* Self-describing complex relocations.  */
  BFD_RELOC_RELC,


/* Infineon Relocations.  */
  BFD_RELOC_XC16X_PAG,
  BFD_RELOC_XC16X_POF,
  BFD_RELOC_XC16X_SEG,
  BFD_RELOC_XC16X_SOF,

/* Relocations used by VAX ELF.  */
  BFD_RELOC_VAX_GLOB_DAT,
  BFD_RELOC_VAX_JMP_SLOT,
  BFD_RELOC_VAX_RELATIVE,

/* Morpho MT - 16 bit immediate relocation.  */
  BFD_RELOC_MT_PC16,

/* Morpho MT - Hi 16 bits of an address.  */
  BFD_RELOC_MT_HI16,

/* Morpho MT - Low 16 bits of an address.  */
  BFD_RELOC_MT_LO16,

/* Morpho MT - Used to tell the linker which vtable entries are used.  */
  BFD_RELOC_MT_GNU_VTINHERIT,

/* Morpho MT - Used to tell the linker which vtable entries are used.  */
  BFD_RELOC_MT_GNU_VTENTRY,

/* Morpho MT - 8 bit immediate relocation.  */
  BFD_RELOC_MT_PCINSN8,

/* msp430 specific relocation codes  */
  BFD_RELOC_MSP430_10_PCREL,
  BFD_RELOC_MSP430_16_PCREL,
  BFD_RELOC_MSP430_16,
  BFD_RELOC_MSP430_16_PCREL_BYTE,
  BFD_RELOC_MSP430_16_BYTE,
  BFD_RELOC_MSP430_2X_PCREL,
  BFD_RELOC_MSP430_RL_PCREL,

/* IQ2000 Relocations.  */
  BFD_RELOC_IQ2000_OFFSET_16,
  BFD_RELOC_IQ2000_OFFSET_21,
  BFD_RELOC_IQ2000_UHI16,

/* Special Xtensa relocation used only by PLT entries in ELF shared
objects to indicate that the runtime linker should set the value
to one of its own internal functions or data structures.  */
  BFD_RELOC_XTENSA_RTLD,

/* Xtensa relocations for ELF shared objects.  */
  BFD_RELOC_XTENSA_GLOB_DAT,
  BFD_RELOC_XTENSA_JMP_SLOT,
  BFD_RELOC_XTENSA_RELATIVE,

/* Xtensa relocation used in ELF object files for symbols that may require
PLT entries.  Otherwise, this is just a generic 32-bit relocation.  */
  BFD_RELOC_XTENSA_PLT,

/* Xtensa relocations to mark the difference of two local symbols.
These are only needed to support linker relaxation and can be ignored
when not relaxing.  The field is set to the value of the difference
assuming no relaxation.  The relocation encodes the position of the
first symbol so the linker can determine whether to adjust the field
value.  */
  BFD_RELOC_XTENSA_DIFF8,
  BFD_RELOC_XTENSA_DIFF16,
  BFD_RELOC_XTENSA_DIFF32,

/* Generic Xtensa relocations for instruction operands.  Only the slot
number is encoded in the relocation.  The relocation applies to the
last PC-relative immediate operand, or if there are no PC-relative
immediates, to the last immediate operand.  */
  BFD_RELOC_XTENSA_SLOT0_OP,
  BFD_RELOC_XTENSA_SLOT1_OP,
  BFD_RELOC_XTENSA_SLOT2_OP,
  BFD_RELOC_XTENSA_SLOT3_OP,
  BFD_RELOC_XTENSA_SLOT4_OP,
  BFD_RELOC_XTENSA_SLOT5_OP,
  BFD_RELOC_XTENSA_SLOT6_OP,
  BFD_RELOC_XTENSA_SLOT7_OP,
  BFD_RELOC_XTENSA_SLOT8_OP,
  BFD_RELOC_XTENSA_SLOT9_OP,
  BFD_RELOC_XTENSA_SLOT10_OP,
  BFD_RELOC_XTENSA_SLOT11_OP,
  BFD_RELOC_XTENSA_SLOT12_OP,
  BFD_RELOC_XTENSA_SLOT13_OP,
  BFD_RELOC_XTENSA_SLOT14_OP,

/* Alternate Xtensa relocations.  Only the slot is encoded in the
relocation.  The meaning of these relocations is opcode-specific.  */
  BFD_RELOC_XTENSA_SLOT0_ALT,
  BFD_RELOC_XTENSA_SLOT1_ALT,
  BFD_RELOC_XTENSA_SLOT2_ALT,
  BFD_RELOC_XTENSA_SLOT3_ALT,
  BFD_RELOC_XTENSA_SLOT4_ALT,
  BFD_RELOC_XTENSA_SLOT5_ALT,
  BFD_RELOC_XTENSA_SLOT6_ALT,
  BFD_RELOC_XTENSA_SLOT7_ALT,
  BFD_RELOC_XTENSA_SLOT8_ALT,
  BFD_RELOC_XTENSA_SLOT9_ALT,
  BFD_RELOC_XTENSA_SLOT10_ALT,
  BFD_RELOC_XTENSA_SLOT11_ALT,
  BFD_RELOC_XTENSA_SLOT12_ALT,
  BFD_RELOC_XTENSA_SLOT13_ALT,
  BFD_RELOC_XTENSA_SLOT14_ALT,

/* Xtensa relocations for backward compatibility.  These have all been
replaced by BFD_RELOC_XTENSA_SLOT0_OP.  */
  BFD_RELOC_XTENSA_OP0,
  BFD_RELOC_XTENSA_OP1,
  BFD_RELOC_XTENSA_OP2,

/* Xtensa relocation to mark that the assembler expanded the
instructions from an original target.  The expansion size is
encoded in the reloc size.  */
  BFD_RELOC_XTENSA_ASM_EXPAND,

/* Xtensa relocation to mark that the linker should simplify
assembler-expanded instructions.  This is commonly used
internally by the linker after analysis of a
BFD_RELOC_XTENSA_ASM_EXPAND.  */
  BFD_RELOC_XTENSA_ASM_SIMPLIFY,

/* 8 bit signed offset in (ix+d) or (iy+d).  */
  BFD_RELOC_Z80_DISP8,

/* DJNZ offset.  */
  BFD_RELOC_Z8K_DISP7,

/* CALR offset.  */
  BFD_RELOC_Z8K_CALLR,

/* 4 bit value.  */
  BFD_RELOC_Z8K_IMM4L,
  BFD_RELOC_UNUSED };
typedef enum bfd_reloc_code_real bfd_reloc_code_real_type;
reloc_howto_type *bfd_reloc_type_lookup
   (bfd *abfd, bfd_reloc_code_real_type code);
reloc_howto_type *bfd_reloc_name_lookup
   (bfd *abfd, const char *reloc_name);

const char *bfd_get_reloc_code_name (bfd_reloc_code_real_type code);

/* Extracted from syms.c.  */

typedef struct bfd_symbol
{
  /* A pointer to the BFD which owns the symbol. This information
     is necessary so that a back end can work out what additional
     information (invisible to the application writer) is carried
     with the symbol.

     This field is *almost* redundant, since you can use section->owner
     instead, except that some symbols point to the global sections
     bfd_{abs,com,und}_section.  This could be fixed by making
     these globals be per-bfd (or per-target-flavor).  FIXME.  */
  struct bfd *the_bfd; /* Use bfd_asymbol_bfd(sym) to access this field.  */

  /* The text of the symbol. The name is left alone, and not copied; the
     application may not alter it.  */
  const char *name;

  /* The value of the symbol.  This really should be a union of a
     numeric value with a pointer, since some flags indicate that
     a pointer to another symbol is stored here.  */
  symvalue value;

  /* Attributes of a symbol.  */
#define BSF_NO_FLAGS    0x00

  /* The symbol has local scope; <<static>> in <<C>>. The value
     is the offset into the section of the data.  */
#define BSF_LOCAL      0x01

  /* The symbol has global scope; initialized data in <<C>>. The
     value is the offset into the section of the data.  */
#define BSF_GLOBAL     0x02

  /* The symbol has global scope and is exported. The value is
     the offset into the section of the data.  */
#define BSF_EXPORT     BSF_GLOBAL /* No real difference.  */

  /* A normal C symbol would be one of:
     <<BSF_LOCAL>>, <<BSF_FORT_COMM>>,  <<BSF_UNDEFINED>> or
     <<BSF_GLOBAL>>.  */

  /* The symbol is a debugging record. The value has an arbitrary
     meaning, unless BSF_DEBUGGING_RELOC is also set.  */
#define BSF_DEBUGGING  0x08

  /* The symbol denotes a function entry point.  Used in ELF,
     perhaps others someday.  */
#define BSF_FUNCTION    0x10

  /* Used by the linker.  */
#define BSF_KEEP        0x20
#define BSF_KEEP_G      0x40

  /* A weak global symbol, overridable without warnings by
     a regular global symbol of the same name.  */
#define BSF_WEAK        0x80

  /* This symbol was created to point to a section, e.g. ELF's
     STT_SECTION symbols.  */
#define BSF_SECTION_SYM 0x100

  /* The symbol used to be a common symbol, but now it is
     allocated.  */
#define BSF_OLD_COMMON  0x200

  /* The default value for common data.  */
#define BFD_FORT_COMM_DEFAULT_VALUE 0

  /* In some files the type of a symbol sometimes alters its
     location in an output file - ie in coff a <<ISFCN>> symbol
     which is also <<C_EXT>> symbol appears where it was
     declared and not at the end of a section.  This bit is set
     by the target BFD part to convey this information.  */
#define BSF_NOT_AT_END    0x400

  /* Signal that the symbol is the label of constructor section.  */
#define BSF_CONSTRUCTOR   0x800

  /* Signal that the symbol is a warning symbol.  The name is a
     warning.  The name of the next symbol is the one to warn about;
     if a reference is made to a symbol with the same name as the next
     symbol, a warning is issued by the linker.  */
#define BSF_WARNING       0x1000

  /* Signal that the symbol is indirect.  This symbol is an indirect
     pointer to the symbol with the same name as the next symbol.  */
#define BSF_INDIRECT      0x2000

  /* BSF_FILE marks symbols that contain a file name.  This is used
     for ELF STT_FILE symbols.  */
#define BSF_FILE          0x4000

  /* Symbol is from dynamic linking information.  */
#define BSF_DYNAMIC       0x8000

  /* The symbol denotes a data object.  Used in ELF, and perhaps
     others someday.  */
#define BSF_OBJECT        0x10000

  /* This symbol is a debugging symbol.  The value is the offset
     into the section of the data.  BSF_DEBUGGING should be set
     as well.  */
#define BSF_DEBUGGING_RELOC 0x20000

  /* This symbol is thread local.  Used in ELF.  */
#define BSF_THREAD_LOCAL  0x40000

  /* This symbol represents a complex relocation expression,
     with the expression tree serialized in the symbol name.  */
#define BSF_RELC 0x80000

  /* This symbol represents a signed complex relocation expression,
     with the expression tree serialized in the symbol name.  */
#define BSF_SRELC 0x100000

  flagword flags;

  /* A pointer to the section to which this symbol is
     relative.  This will always be non NULL, there are special
     sections for undefined and absolute symbols.  */
  struct bfd_section *section;

  /* Back end special data.  */
  union
    {
      void *p;
      bfd_vma i;
    }
  udata;
}
asymbol;

#define bfd_get_symtab_upper_bound(abfd) \
     BFD_SEND (abfd, _bfd_get_symtab_upper_bound, (abfd))

bfd_boolean bfd_is_local_label (bfd *abfd, asymbol *sym);

bfd_boolean bfd_is_local_label_name (bfd *abfd, const char *name);

#define bfd_is_local_label_name(abfd, name) \
  BFD_SEND (abfd, _bfd_is_local_label_name, (abfd, name))

bfd_boolean bfd_is_target_special_symbol (bfd *abfd, asymbol *sym);

#define bfd_is_target_special_symbol(abfd, sym) \
  BFD_SEND (abfd, _bfd_is_target_special_symbol, (abfd, sym))

#define bfd_canonicalize_symtab(abfd, location) \
  BFD_SEND (abfd, _bfd_canonicalize_symtab, (abfd, location))

bfd_boolean bfd_set_symtab
   (bfd *abfd, asymbol **location, unsigned int count);

void bfd_print_symbol_vandf (bfd *abfd, void *file, asymbol *symbol);

#define bfd_make_empty_symbol(abfd) \
  BFD_SEND (abfd, _bfd_make_empty_symbol, (abfd))

asymbol *_bfd_generic_make_empty_symbol (bfd *);

#define bfd_make_debug_symbol(abfd,ptr,size) \
  BFD_SEND (abfd, _bfd_make_debug_symbol, (abfd, ptr, size))

int bfd_decode_symclass (asymbol *symbol);

bfd_boolean bfd_is_undefined_symclass (int symclass);

void bfd_symbol_info (asymbol *symbol, symbol_info *ret);

bfd_boolean bfd_copy_private_symbol_data
   (bfd *ibfd, asymbol *isym, bfd *obfd, asymbol *osym);

#define bfd_copy_private_symbol_data(ibfd, isymbol, obfd, osymbol) \
  BFD_SEND (obfd, _bfd_copy_private_symbol_data, \
            (ibfd, isymbol, obfd, osymbol))

/* Extracted from bfd.c.  */
struct bfd
{
  /* A unique identifier of the BFD  */
  unsigned int id;

  /* The filename the application opened the BFD with.  */
  const char *filename;

  /* A pointer to the target jump table.  */
  const struct bfd_target *xvec;

  /* The IOSTREAM, and corresponding IO vector that provide access
     to the file backing the BFD.  */
  void *iostream;
  const struct bfd_iovec *iovec;

  /* Is the file descriptor being cached?  That is, can it be closed as
     needed, and re-opened when accessed later?  */
  bfd_boolean cacheable;

  /* Marks whether there was a default target specified when the
     BFD was opened. This is used to select which matching algorithm
     to use to choose the back end.  */
  bfd_boolean target_defaulted;

  /* The caching routines use these to maintain a
     least-recently-used list of BFDs.  */
  struct bfd *lru_prev, *lru_next;

  /* When a file is closed by the caching routines, BFD retains
     state information on the file here...  */
  ufile_ptr where;

  /* ... and here: (``once'' means at least once).  */
  bfd_boolean opened_once;

  /* Set if we have a locally maintained mtime value, rather than
     getting it from the file each time.  */
  bfd_boolean mtime_set;

  /* File modified time, if mtime_set is TRUE.  */
  long mtime;

  /* Reserved for an unimplemented file locking extension.  */
  int ifd;

  /* The format which belongs to the BFD. (object, core, etc.)  */
  bfd_format format;

  /* The direction with which the BFD was opened.  */
  enum bfd_direction
    {
      no_direction = 0,
      read_direction = 1,
      write_direction = 2,
      both_direction = 3
    }
  direction;

  /* Format_specific flags.  */
  flagword flags;

  /* Currently my_archive is tested before adding origin to
     anything. I believe that this can become always an add of
     origin, with origin set to 0 for non archive files.  */
  ufile_ptr origin;

  /* Remember when output has begun, to stop strange things
     from happening.  */
  bfd_boolean output_has_begun;

  /* A hash table for section names.  */
  struct bfd_hash_table section_htab;

  /* Pointer to linked list of sections.  */
  struct bfd_section *sections;

  /* The last section on the section list.  */
  struct bfd_section *section_last;

  /* The number of sections.  */
  unsigned int section_count;

  /* Stuff only useful for object files:
     The start address.  */
  bfd_vma start_address;

  /* Used for input and output.  */
  unsigned int symcount;

  /* Symbol table for output BFD (with symcount entries).  */
  struct bfd_symbol  **outsymbols;

  /* Used for slurped dynamic symbol tables.  */
  unsigned int dynsymcount;

  /* Pointer to structure which contains architecture information.  */
  const struct bfd_arch_info *arch_info;

  /* Flag set if symbols from this BFD should not be exported.  */
  bfd_boolean no_export;

  /* Stuff only useful for archives.  */
  void *arelt_data;
  struct bfd *my_archive;      /* The containing archive BFD.  */
  struct bfd *archive_next;    /* The next BFD in the archive.  */
  struct bfd *archive_head;    /* The first BFD in the archive.  */
  bfd_boolean has_armap;

  /* A chain of BFD structures involved in a link.  */
  struct bfd *link_next;

  /* A field used by _bfd_generic_link_add_archive_symbols.  This will
     be used only for archive elements.  */
  int archive_pass;

  /* Used by the back end to hold private data.  */
  union
    {
      struct aout_data_struct *aout_data;
      struct artdata *aout_ar_data;
      struct _oasys_data *oasys_obj_data;
      struct _oasys_ar_data *oasys_ar_data;
      struct coff_tdata *coff_obj_data;
      struct pe_tdata *pe_obj_data;
      struct xcoff_tdata *xcoff_obj_data;
      struct ecoff_tdata *ecoff_obj_data;
      struct ieee_data_struct *ieee_data;
      struct ieee_ar_data_struct *ieee_ar_data;
      struct srec_data_struct *srec_data;
      struct ihex_data_struct *ihex_data;
      struct tekhex_data_struct *tekhex_data;
      struct elf_obj_tdata *elf_obj_data;
      struct nlm_obj_tdata *nlm_obj_data;
      struct bout_data_struct *bout_data;
      struct mmo_data_struct *mmo_data;
      struct sun_core_struct *sun_core_data;
      struct sco5_core_struct *sco5_core_data;
      struct trad_core_struct *trad_core_data;
      struct som_data_struct *som_data;
      struct hpux_core_struct *hpux_core_data;
      struct hppabsd_core_struct *hppabsd_core_data;
      struct sgi_core_struct *sgi_core_data;
      struct lynx_core_struct *lynx_core_data;
      struct osf_core_struct *osf_core_data;
      struct cisco_core_struct *cisco_core_data;
      struct versados_data_struct *versados_data;
      struct netbsd_core_struct *netbsd_core_data;
      struct mach_o_data_struct *mach_o_data;
      struct mach_o_fat_data_struct *mach_o_fat_data;
      struct bfd_pef_data_struct *pef_data;
      struct bfd_pef_xlib_data_struct *pef_xlib_data;
      struct bfd_sym_data_struct *sym_data;
      void *any;
    }
  tdata;

  /* Used by the application to hold private data.  */
  void *usrdata;

  /* Where all the allocated stuff under this BFD goes.  This is a
     struct objalloc *, but we use void * to avoid requiring the inclusion
     of objalloc.h.  */
  void *memory;
};

typedef enum bfd_error
{
  bfd_error_no_error = 0,
  bfd_error_system_call,
  bfd_error_invalid_target,
  bfd_error_wrong_format,
  bfd_error_wrong_object_format,
  bfd_error_invalid_operation,
  bfd_error_no_memory,
  bfd_error_no_symbols,
  bfd_error_no_armap,
  bfd_error_no_more_archived_files,
  bfd_error_malformed_archive,
  bfd_error_file_not_recognized,
  bfd_error_file_ambiguously_recognized,
  bfd_error_no_contents,
  bfd_error_nonrepresentable_section,
  bfd_error_no_debug_section,
  bfd_error_bad_value,
  bfd_error_file_truncated,
  bfd_error_file_too_big,
  bfd_error_on_input,
  bfd_error_invalid_error_code
}
bfd_error_type;

bfd_error_type bfd_get_error (void);

void bfd_set_error (bfd_error_type error_tag, ...);

const char *bfd_errmsg (bfd_error_type error_tag);

void bfd_perror (const char *message);

typedef void (*bfd_error_handler_type) (const char *, ...);

bfd_error_handler_type bfd_set_error_handler (bfd_error_handler_type);

void bfd_set_error_program_name (const char *);

bfd_error_handler_type bfd_get_error_handler (void);

long bfd_get_reloc_upper_bound (bfd *abfd, asection *sect);

long bfd_canonicalize_reloc
   (bfd *abfd, asection *sec, arelent **loc, asymbol **syms);

void bfd_set_reloc
   (bfd *abfd, asection *sec, arelent **rel, unsigned int count);

bfd_boolean bfd_set_file_flags (bfd *abfd, flagword flags);

int bfd_get_arch_size (bfd *abfd);

int bfd_get_sign_extend_vma (bfd *abfd);

bfd_boolean bfd_set_start_address (bfd *abfd, bfd_vma vma);

unsigned int bfd_get_gp_size (bfd *abfd);

void bfd_set_gp_size (bfd *abfd, unsigned int i);

bfd_vma bfd_scan_vma (const char *string, const char **end, int base);

bfd_boolean bfd_copy_private_header_data (bfd *ibfd, bfd *obfd);

#define bfd_copy_private_header_data(ibfd, obfd) \
     BFD_SEND (obfd, _bfd_copy_private_header_data, \
               (ibfd, obfd))
bfd_boolean bfd_copy_private_bfd_data (bfd *ibfd, bfd *obfd);

#define bfd_copy_private_bfd_data(ibfd, obfd) \
     BFD_SEND (obfd, _bfd_copy_private_bfd_data, \
               (ibfd, obfd))
bfd_boolean bfd_merge_private_bfd_data (bfd *ibfd, bfd *obfd);

#define bfd_merge_private_bfd_data(ibfd, obfd) \
     BFD_SEND (obfd, _bfd_merge_private_bfd_data, \
               (ibfd, obfd))
bfd_boolean bfd_set_private_flags (bfd *abfd, flagword flags);

#define bfd_set_private_flags(abfd, flags) \
     BFD_SEND (abfd, _bfd_set_private_flags, (abfd, flags))
#define bfd_sizeof_headers(abfd, info) \
       BFD_SEND (abfd, _bfd_sizeof_headers, (abfd, info))

#define bfd_find_nearest_line(abfd, sec, syms, off, file, func, line) \
       BFD_SEND (abfd, _bfd_find_nearest_line, \
                 (abfd, sec, syms, off, file, func, line))

#define bfd_find_line(abfd, syms, sym, file, line) \
       BFD_SEND (abfd, _bfd_find_line, \
                 (abfd, syms, sym, file, line))

#define bfd_find_inliner_info(abfd, file, func, line) \
       BFD_SEND (abfd, _bfd_find_inliner_info, \
                 (abfd, file, func, line))

#define bfd_debug_info_start(abfd) \
       BFD_SEND (abfd, _bfd_debug_info_start, (abfd))

#define bfd_debug_info_end(abfd) \
       BFD_SEND (abfd, _bfd_debug_info_end, (abfd))

#define bfd_debug_info_accumulate(abfd, section) \
       BFD_SEND (abfd, _bfd_debug_info_accumulate, (abfd, section))

#define bfd_stat_arch_elt(abfd, stat) \
       BFD_SEND (abfd, _bfd_stat_arch_elt,(abfd, stat))

#define bfd_update_armap_timestamp(abfd) \
       BFD_SEND (abfd, _bfd_update_armap_timestamp, (abfd))

#define bfd_set_arch_mach(abfd, arch, mach)\
       BFD_SEND ( abfd, _bfd_set_arch_mach, (abfd, arch, mach))

#define bfd_relax_section(abfd, section, link_info, again) \
       BFD_SEND (abfd, _bfd_relax_section, (abfd, section, link_info, again))

#define bfd_gc_sections(abfd, link_info) \
       BFD_SEND (abfd, _bfd_gc_sections, (abfd, link_info))

#define bfd_merge_sections(abfd, link_info) \
       BFD_SEND (abfd, _bfd_merge_sections, (abfd, link_info))

#define bfd_is_group_section(abfd, sec) \
       BFD_SEND (abfd, _bfd_is_group_section, (abfd, sec))

#define bfd_discard_group(abfd, sec) \
       BFD_SEND (abfd, _bfd_discard_group, (abfd, sec))

#define bfd_link_hash_table_create(abfd) \
       BFD_SEND (abfd, _bfd_link_hash_table_create, (abfd))

#define bfd_link_hash_table_free(abfd, hash) \
       BFD_SEND (abfd, _bfd_link_hash_table_free, (hash))

#define bfd_link_add_symbols(abfd, info) \
       BFD_SEND (abfd, _bfd_link_add_symbols, (abfd, info))

#define bfd_link_just_syms(abfd, sec, info) \
       BFD_SEND (abfd, _bfd_link_just_syms, (sec, info))

#define bfd_final_link(abfd, info) \
       BFD_SEND (abfd, _bfd_final_link, (abfd, info))

#define bfd_free_cached_info(abfd) \
       BFD_SEND (abfd, _bfd_free_cached_info, (abfd))

#define bfd_get_dynamic_symtab_upper_bound(abfd) \
       BFD_SEND (abfd, _bfd_get_dynamic_symtab_upper_bound, (abfd))

#define bfd_print_private_bfd_data(abfd, file)\
       BFD_SEND (abfd, _bfd_print_private_bfd_data, (abfd, file))

#define bfd_canonicalize_dynamic_symtab(abfd, asymbols) \
       BFD_SEND (abfd, _bfd_canonicalize_dynamic_symtab, (abfd, asymbols))

#define bfd_get_synthetic_symtab(abfd, count, syms, dyncount, dynsyms, ret) \
       BFD_SEND (abfd, _bfd_get_synthetic_symtab, (abfd, count, syms, \
                                                   dyncount, dynsyms, ret))

#define bfd_get_dynamic_reloc_upper_bound(abfd) \
       BFD_SEND (abfd, _bfd_get_dynamic_reloc_upper_bound, (abfd))

#define bfd_canonicalize_dynamic_reloc(abfd, arels, asyms) \
       BFD_SEND (abfd, _bfd_canonicalize_dynamic_reloc, (abfd, arels, asyms))

extern bfd_byte *bfd_get_relocated_section_contents
  (bfd *, struct bfd_link_info *, struct bfd_link_order *, bfd_byte *,
   bfd_boolean, asymbol **);

bfd_boolean bfd_alt_mach_code (bfd *abfd, int alternative);

struct bfd_preserve
{
  void *marker;
  void *tdata;
  flagword flags;
  const struct bfd_arch_info *arch_info;
  struct bfd_section *sections;
  struct bfd_section *section_last;
  unsigned int section_count;
  struct bfd_hash_table section_htab;
};

bfd_boolean bfd_preserve_save (bfd *, struct bfd_preserve *);

void bfd_preserve_restore (bfd *, struct bfd_preserve *);

void bfd_preserve_finish (bfd *, struct bfd_preserve *);

bfd_vma bfd_emul_get_maxpagesize (const char *);

void bfd_emul_set_maxpagesize (const char *, bfd_vma);

bfd_vma bfd_emul_get_commonpagesize (const char *);

void bfd_emul_set_commonpagesize (const char *, bfd_vma);

char *bfd_demangle (bfd *, const char *, int);

/* Extracted from archive.c.  */
symindex bfd_get_next_mapent
   (bfd *abfd, symindex previous, carsym **sym);

bfd_boolean bfd_set_archive_head (bfd *output, bfd *new_head);

bfd *bfd_openr_next_archived_file (bfd *archive, bfd *previous);

/* Extracted from corefile.c.  */
const char *bfd_core_file_failing_command (bfd *abfd);

int bfd_core_file_failing_signal (bfd *abfd);

bfd_boolean core_file_matches_executable_p
   (bfd *core_bfd, bfd *exec_bfd);

bfd_boolean generic_core_file_matches_executable_p
   (bfd *core_bfd, bfd *exec_bfd);

/* Extracted from targets.c.  */
#define BFD_SEND(bfd, message, arglist) \
  ((*((bfd)->xvec->message)) arglist)

#ifdef DEBUG_BFD_SEND
#undef BFD_SEND
#define BFD_SEND(bfd, message, arglist) \
  (((bfd) && (bfd)->xvec && (bfd)->xvec->message) ? \
    ((*((bfd)->xvec->message)) arglist) : \
    (bfd_assert (__FILE__,__LINE__), NULL))
#endif
#define BFD_SEND_FMT(bfd, message, arglist) \
  (((bfd)->xvec->message[(int) ((bfd)->format)]) arglist)

#ifdef DEBUG_BFD_SEND
#undef BFD_SEND_FMT
#define BFD_SEND_FMT(bfd, message, arglist) \
  (((bfd) && (bfd)->xvec && (bfd)->xvec->message) ? \
   (((bfd)->xvec->message[(int) ((bfd)->format)]) arglist) : \
   (bfd_assert (__FILE__,__LINE__), NULL))
#endif

enum bfd_flavour
{
  bfd_target_unknown_flavour,
  bfd_target_aout_flavour,
  bfd_target_coff_flavour,
  bfd_target_ecoff_flavour,
  bfd_target_xcoff_flavour,
  bfd_target_elf_flavour,
  bfd_target_ieee_flavour,
  bfd_target_nlm_flavour,
  bfd_target_oasys_flavour,
  bfd_target_tekhex_flavour,
  bfd_target_srec_flavour,
  bfd_target_ihex_flavour,
  bfd_target_som_flavour,
  bfd_target_os9k_flavour,
  bfd_target_versados_flavour,
  bfd_target_msdos_flavour,
  bfd_target_ovax_flavour,
  bfd_target_evax_flavour,
  bfd_target_mmo_flavour,
  bfd_target_mach_o_flavour,
  bfd_target_pef_flavour,
  bfd_target_pef_xlib_flavour,
  bfd_target_sym_flavour
};

enum bfd_endian { BFD_ENDIAN_BIG, BFD_ENDIAN_LITTLE, BFD_ENDIAN_UNKNOWN };

/* Forward declaration.  */
typedef struct bfd_link_info _bfd_link_info;

typedef struct bfd_target
{
  /* Identifies the kind of target, e.g., SunOS4, Ultrix, etc.  */
  char *name;

 /* The "flavour" of a back end is a general indication about
    the contents of a file.  */
  enum bfd_flavour flavour;

  /* The order of bytes within the data area of a file.  */
  enum bfd_endian byteorder;

 /* The order of bytes within the header parts of a file.  */
  enum bfd_endian header_byteorder;

  /* A mask of all the flags which an executable may have set -
     from the set <<BFD_NO_FLAGS>>, <<HAS_RELOC>>, ...<<D_PAGED>>.  */
  flagword object_flags;

 /* A mask of all the flags which a section may have set - from
    the set <<SEC_NO_FLAGS>>, <<SEC_ALLOC>>, ...<<SET_NEVER_LOAD>>.  */
  flagword section_flags;

 /* The character normally found at the front of a symbol.
    (if any), perhaps `_'.  */
  char symbol_leading_char;

 /* The pad character for file names within an archive header.  */
  char ar_pad_char;

  /* The maximum number of characters in an archive header.  */
  unsigned short ar_max_namelen;

  /* Entries for byte swapping for data. These are different from the
     other entry points, since they don't take a BFD as the first argument.
     Certain other handlers could do the same.  */
  bfd_uint64_t   (*bfd_getx64) (const void *);
  bfd_int64_t    (*bfd_getx_signed_64) (const void *);
  void           (*bfd_putx64) (bfd_uint64_t, void *);
  bfd_vma        (*bfd_getx32) (const void *);
  bfd_signed_vma (*bfd_getx_signed_32) (const void *);
  void           (*bfd_putx32) (bfd_vma, void *);
  bfd_vma        (*bfd_getx16) (const void *);
  bfd_signed_vma (*bfd_getx_signed_16) (const void *);
  void           (*bfd_putx16) (bfd_vma, void *);

  /* Byte swapping for the headers.  */
  bfd_uint64_t   (*bfd_h_getx64) (const void *);
  bfd_int64_t    (*bfd_h_getx_signed_64) (const void *);
  void           (*bfd_h_putx64) (bfd_uint64_t, void *);
  bfd_vma        (*bfd_h_getx32) (const void *);
  bfd_signed_vma (*bfd_h_getx_signed_32) (const void *);
  void           (*bfd_h_putx32) (bfd_vma, void *);
  bfd_vma        (*bfd_h_getx16) (const void *);
  bfd_signed_vma (*bfd_h_getx_signed_16) (const void *);
  void           (*bfd_h_putx16) (bfd_vma, void *);

  /* Format dependent routines: these are vectors of entry points
     within the target vector structure, one for each format to check.  */

  /* Check the format of a file being read.  Return a <<bfd_target *>> or zero.  */
  const struct bfd_target *(*_bfd_check_format[bfd_type_end]) (bfd *);

  /* Set the format of a file being written.  */
  bfd_boolean (*_bfd_set_format[bfd_type_end]) (bfd *);

  /* Write cached information into a file being written, at <<bfd_close>>.  */
  bfd_boolean (*_bfd_write_contents[bfd_type_end]) (bfd *);


  /* Generic entry points.  */
#define BFD_JUMP_TABLE_GENERIC(NAME) \
  NAME##_close_and_cleanup, \
  NAME##_bfd_free_cached_info, \
  NAME##_new_section_hook, \
  NAME##_get_section_contents, \
  NAME##_get_section_contents_in_window

  /* Called when the BFD is being closed to do any necessary cleanup.  */
  bfd_boolean (*_close_and_cleanup) (bfd *);
  /* Ask the BFD to free all cached information.  */
  bfd_boolean (*_bfd_free_cached_info) (bfd *);
  /* Called when a new section is created.  */
  bfd_boolean (*_new_section_hook) (bfd *, sec_ptr);
  /* Read the contents of a section.  */
  bfd_boolean (*_bfd_get_section_contents)
    (bfd *, sec_ptr, void *, file_ptr, bfd_size_type);
  bfd_boolean (*_bfd_get_section_contents_in_window)
    (bfd *, sec_ptr, bfd_window *, file_ptr, bfd_size_type);

  /* Entry points to copy private data.  */
#define BFD_JUMP_TABLE_COPY(NAME) \
  NAME##_bfd_copy_private_bfd_data, \
  NAME##_bfd_merge_private_bfd_data, \
  _bfd_generic_init_private_section_data, \
  NAME##_bfd_copy_private_section_data, \
  NAME##_bfd_copy_private_symbol_data, \
  NAME##_bfd_copy_private_header_data, \
  NAME##_bfd_set_private_flags, \
  NAME##_bfd_print_private_bfd_data

  /* Called to copy BFD general private data from one object file
     to another.  */
  bfd_boolean (*_bfd_copy_private_bfd_data) (bfd *, bfd *);
  /* Called to merge BFD general private data from one object file
     to a common output file when linking.  */
  bfd_boolean (*_bfd_merge_private_bfd_data) (bfd *, bfd *);
  /* Called to initialize BFD private section data from one object file
     to another.  */
#define bfd_init_private_section_data(ibfd, isec, obfd, osec, link_info) \
  BFD_SEND (obfd, _bfd_init_private_section_data, (ibfd, isec, obfd, osec, link_info))
  bfd_boolean (*_bfd_init_private_section_data)
    (bfd *, sec_ptr, bfd *, sec_ptr, struct bfd_link_info *);
  /* Called to copy BFD private section data from one object file
     to another.  */
  bfd_boolean (*_bfd_copy_private_section_data)
    (bfd *, sec_ptr, bfd *, sec_ptr);
  /* Called to copy BFD private symbol data from one symbol
     to another.  */
  bfd_boolean (*_bfd_copy_private_symbol_data)
    (bfd *, asymbol *, bfd *, asymbol *);
  /* Called to copy BFD private header data from one object file
     to another.  */
  bfd_boolean (*_bfd_copy_private_header_data)
    (bfd *, bfd *);
  /* Called to set private backend flags.  */
  bfd_boolean (*_bfd_set_private_flags) (bfd *, flagword);

  /* Called to print private BFD data.  */
  bfd_boolean (*_bfd_print_private_bfd_data) (bfd *, void *);

  /* Core file entry points.  */
#define BFD_JUMP_TABLE_CORE(NAME) \
  NAME##_core_file_failing_command, \
  NAME##_core_file_failing_signal, \
  NAME##_core_file_matches_executable_p

  char *      (*_core_file_failing_command) (bfd *);
  int         (*_core_file_failing_signal) (bfd *);
  bfd_boolean (*_core_file_matches_executable_p) (bfd *, bfd *);

  /* Archive entry points.  */
#define BFD_JUMP_TABLE_ARCHIVE(NAME) \
  NAME##_slurp_armap, \
  NAME##_slurp_extended_name_table, \
  NAME##_construct_extended_name_table, \
  NAME##_truncate_arname, \
  NAME##_write_armap, \
  NAME##_read_ar_hdr, \
  NAME##_openr_next_archived_file, \
  NAME##_get_elt_at_index, \
  NAME##_generic_stat_arch_elt, \
  NAME##_update_armap_timestamp

  bfd_boolean (*_bfd_slurp_armap) (bfd *);
  bfd_boolean (*_bfd_slurp_extended_name_table) (bfd *);
  bfd_boolean (*_bfd_construct_extended_name_table)
    (bfd *, char **, bfd_size_type *, const char **);
  void        (*_bfd_truncate_arname) (bfd *, const char *, char *);
  bfd_boolean (*write_armap)
    (bfd *, unsigned int, struct orl *, unsigned int, int);
  void *      (*_bfd_read_ar_hdr_fn) (bfd *);
  bfd *       (*openr_next_archived_file) (bfd *, bfd *);
#define bfd_get_elt_at_index(b,i) BFD_SEND (b, _bfd_get_elt_at_index, (b,i))
  bfd *       (*_bfd_get_elt_at_index) (bfd *, symindex);
  int         (*_bfd_stat_arch_elt) (bfd *, struct stat *);
  bfd_boolean (*_bfd_update_armap_timestamp) (bfd *);

  /* Entry points used for symbols.  */
#define BFD_JUMP_TABLE_SYMBOLS(NAME) \
  NAME##_get_symtab_upper_bound, \
  NAME##_canonicalize_symtab, \
  NAME##_make_empty_symbol, \
  NAME##_print_symbol, \
  NAME##_get_symbol_info, \
  NAME##_bfd_is_local_label_name, \
  NAME##_bfd_is_target_special_symbol, \
  NAME##_get_lineno, \
  NAME##_find_nearest_line, \
  _bfd_generic_find_line, \
  NAME##_find_inliner_info, \
  NAME##_bfd_make_debug_symbol, \
  NAME##_read_minisymbols, \
  NAME##_minisymbol_to_symbol

  long        (*_bfd_get_symtab_upper_bound) (bfd *);
  long        (*_bfd_canonicalize_symtab)
    (bfd *, struct bfd_symbol **);
  struct bfd_symbol *
              (*_bfd_make_empty_symbol) (bfd *);
  void        (*_bfd_print_symbol)
    (bfd *, void *, struct bfd_symbol *, bfd_print_symbol_type);
#define bfd_print_symbol(b,p,s,e) BFD_SEND (b, _bfd_print_symbol, (b,p,s,e))
  void        (*_bfd_get_symbol_info)
    (bfd *, struct bfd_symbol *, symbol_info *);
#define bfd_get_symbol_info(b,p,e) BFD_SEND (b, _bfd_get_symbol_info, (b,p,e))
  bfd_boolean (*_bfd_is_local_label_name) (bfd *, const char *);
  bfd_boolean (*_bfd_is_target_special_symbol) (bfd *, asymbol *);
  alent *     (*_get_lineno) (bfd *, struct bfd_symbol *);
  bfd_boolean (*_bfd_find_nearest_line)
    (bfd *, struct bfd_section *, struct bfd_symbol **, bfd_vma,
     const char **, const char **, unsigned int *);
  bfd_boolean (*_bfd_find_line)
    (bfd *, struct bfd_symbol **, struct bfd_symbol *,
     const char **, unsigned int *);
  bfd_boolean (*_bfd_find_inliner_info)
    (bfd *, const char **, const char **, unsigned int *);
 /* Back-door to allow format-aware applications to create debug symbols
    while using BFD for everything else.  Currently used by the assembler
    when creating COFF files.  */
  asymbol *   (*_bfd_make_debug_symbol)
    (bfd *, void *, unsigned long size);
#define bfd_read_minisymbols(b, d, m, s) \
  BFD_SEND (b, _read_minisymbols, (b, d, m, s))
  long        (*_read_minisymbols)
    (bfd *, bfd_boolean, void **, unsigned int *);
#define bfd_minisymbol_to_symbol(b, d, m, f) \
  BFD_SEND (b, _minisymbol_to_symbol, (b, d, m, f))
  asymbol *   (*_minisymbol_to_symbol)
    (bfd *, bfd_boolean, const void *, asymbol *);

  /* Routines for relocs.  */
#define BFD_JUMP_TABLE_RELOCS(NAME) \
  NAME##_get_reloc_upper_bound, \
  NAME##_canonicalize_reloc, \
  NAME##_bfd_reloc_type_lookup, \
  NAME##_bfd_reloc_name_lookup

  long        (*_get_reloc_upper_bound) (bfd *, sec_ptr);
  long        (*_bfd_canonicalize_reloc)
    (bfd *, sec_ptr, arelent **, struct bfd_symbol **);
  /* See documentation on reloc types.  */
  reloc_howto_type *
              (*reloc_type_lookup) (bfd *, bfd_reloc_code_real_type);
  reloc_howto_type *
              (*reloc_name_lookup) (bfd *, const char *);


  /* Routines used when writing an object file.  */
#define BFD_JUMP_TABLE_WRITE(NAME) \
  NAME##_set_arch_mach, \
  NAME##_set_section_contents

  bfd_boolean (*_bfd_set_arch_mach)
    (bfd *, enum bfd_architecture, unsigned long);
  bfd_boolean (*_bfd_set_section_contents)
    (bfd *, sec_ptr, const void *, file_ptr, bfd_size_type);

  /* Routines used by the linker.  */
#define BFD_JUMP_TABLE_LINK(NAME) \
  NAME##_sizeof_headers, \
  NAME##_bfd_get_relocated_section_contents, \
  NAME##_bfd_relax_section, \
  NAME##_bfd_link_hash_table_create, \
  NAME##_bfd_link_hash_table_free, \
  NAME##_bfd_link_add_symbols, \
  NAME##_bfd_link_just_syms, \
  NAME##_bfd_final_link, \
  NAME##_bfd_link_split_section, \
  NAME##_bfd_gc_sections, \
  NAME##_bfd_merge_sections, \
  NAME##_bfd_is_group_section, \
  NAME##_bfd_discard_group, \
  NAME##_section_already_linked \

  int         (*_bfd_sizeof_headers) (bfd *, struct bfd_link_info *);
  bfd_byte *  (*_bfd_get_relocated_section_contents)
    (bfd *, struct bfd_link_info *, struct bfd_link_order *,
     bfd_byte *, bfd_boolean, struct bfd_symbol **);

  bfd_boolean (*_bfd_relax_section)
    (bfd *, struct bfd_section *, struct bfd_link_info *, bfd_boolean *);

  /* Create a hash table for the linker.  Different backends store
     different information in this table.  */
  struct bfd_link_hash_table *
              (*_bfd_link_hash_table_create) (bfd *);

  /* Release the memory associated with the linker hash table.  */
  void        (*_bfd_link_hash_table_free) (struct bfd_link_hash_table *);

  /* Add symbols from this object file into the hash table.  */
  bfd_boolean (*_bfd_link_add_symbols) (bfd *, struct bfd_link_info *);

  /* Indicate that we are only retrieving symbol values from this section.  */
  void        (*_bfd_link_just_syms) (asection *, struct bfd_link_info *);

  /* Do a link based on the link_order structures attached to each
     section of the BFD.  */
  bfd_boolean (*_bfd_final_link) (bfd *, struct bfd_link_info *);

  /* Should this section be split up into smaller pieces during linking.  */
  bfd_boolean (*_bfd_link_split_section) (bfd *, struct bfd_section *);

  /* Remove sections that are not referenced from the output.  */
  bfd_boolean (*_bfd_gc_sections) (bfd *, struct bfd_link_info *);

  /* Attempt to merge SEC_MERGE sections.  */
  bfd_boolean (*_bfd_merge_sections) (bfd *, struct bfd_link_info *);

  /* Is this section a member of a group?  */
  bfd_boolean (*_bfd_is_group_section) (bfd *, const struct bfd_section *);

  /* Discard members of a group.  */
  bfd_boolean (*_bfd_discard_group) (bfd *, struct bfd_section *);

  /* Check if SEC has been already linked during a reloceatable or
     final link.  */
  void (*_section_already_linked) (bfd *, struct bfd_section *,
                                   struct bfd_link_info *);

  /* Routines to handle dynamic symbols and relocs.  */
#define BFD_JUMP_TABLE_DYNAMIC(NAME) \
  NAME##_get_dynamic_symtab_upper_bound, \
  NAME##_canonicalize_dynamic_symtab, \
  NAME##_get_synthetic_symtab, \
  NAME##_get_dynamic_reloc_upper_bound, \
  NAME##_canonicalize_dynamic_reloc

  /* Get the amount of memory required to hold the dynamic symbols.  */
  long        (*_bfd_get_dynamic_symtab_upper_bound) (bfd *);
  /* Read in the dynamic symbols.  */
  long        (*_bfd_canonicalize_dynamic_symtab)
    (bfd *, struct bfd_symbol **);
  /* Create synthetized symbols.  */
  long        (*_bfd_get_synthetic_symtab)
    (bfd *, long, struct bfd_symbol **, long, struct bfd_symbol **,
     struct bfd_symbol **);
  /* Get the amount of memory required to hold the dynamic relocs.  */
  long        (*_bfd_get_dynamic_reloc_upper_bound) (bfd *);
  /* Read in the dynamic relocs.  */
  long        (*_bfd_canonicalize_dynamic_reloc)
    (bfd *, arelent **, struct bfd_symbol **);

  /* Opposite endian version of this target.  */
  const struct bfd_target * alternative_target;

  /* Data for use by back-end routines, which isn't
     generic enough to belong in this structure.  */
  const void *backend_data;

} bfd_target;

bfd_boolean bfd_set_default_target (const char *name);

const bfd_target *bfd_find_target (const char *target_name, bfd *abfd);

const char ** bfd_target_list (void);

const bfd_target *bfd_search_for_target
   (int (*search_func) (const bfd_target *, void *),
    void *);

/* Extracted from format.c.  */
bfd_boolean bfd_check_format (bfd *abfd, bfd_format format);

bfd_boolean bfd_check_format_matches
   (bfd *abfd, bfd_format format, char ***matching);

bfd_boolean bfd_set_format (bfd *abfd, bfd_format format);

const char *bfd_format_string (bfd_format format);

/* Extracted from linker.c.  */
bfd_boolean bfd_link_split_section (bfd *abfd, asection *sec);

#define bfd_link_split_section(abfd, sec) \
       BFD_SEND (abfd, _bfd_link_split_section, (abfd, sec))

void bfd_section_already_linked (bfd *abfd, asection *sec,
    struct bfd_link_info *info);

#define bfd_section_already_linked(abfd, sec, info) \
       BFD_SEND (abfd, _section_already_linked, (abfd, sec, info))

/* Extracted from simple.c.  */
bfd_byte *bfd_simple_get_relocated_section_contents
   (bfd *abfd, asection *sec, bfd_byte *outbuf, asymbol **symbol_table);

#ifdef __cplusplus
}
#endif
#endif
