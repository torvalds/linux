/* SPARC-specific values for a.out files 

   Copyright 2001 Free Software Foundation, Inc.

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

/* Some systems, e.g., AIX, may have defined this in header files already
   included.  */
#undef  TARGET_PAGE_SIZE
#define TARGET_PAGE_SIZE	0x2000		/* 8K.  aka NBPG in <sys/param.h> */
/* Note that some SPARCs have 4K pages, some 8K, some others.  */

#define SEG_SIZE_SPARC	TARGET_PAGE_SIZE
#define	SEG_SIZE_SUN3	0x20000		/* Resolution of r/w protection hw */

#define TEXT_START_ADDR	TARGET_PAGE_SIZE	/* Location 0 is not accessible */
#define N_HEADER_IN_TEXT(x) 1

/* Non-default definitions of the accessor macros... */

/* Segment size varies on Sun-3 versus Sun-4.  */

#define N_SEGSIZE(x)	(N_MACHTYPE(x) == M_SPARC?	SEG_SIZE_SPARC:	\
			 N_MACHTYPE(x) == M_68020?	SEG_SIZE_SUN3:	\
			/* Guess? */			TARGET_PAGE_SIZE)

/* Virtual Address of text segment from the a.out file.  For OMAGIC,
   (almost always "unlinked .o's" these days), should be zero.
   Sun added a kludge so that shared libraries linked ZMAGIC get
   an address of zero if a_entry (!!!) is lower than the otherwise
   expected text address.  These kludges have gotta go!
   For linked files, should reflect reality if we know it.  */

/* This differs from the version in aout64.h (which we override by defining
   it here) only for NMAGIC (we return TEXT_START_ADDR+EXEC_BYTES_SIZE;
   they return 0).  */

#define N_TXTADDR(x) \
    (N_MAGIC(x)==OMAGIC? 0 \
     : (N_MAGIC(x) == ZMAGIC && (x).a_entry < TEXT_START_ADDR)? 0 \
     : TEXT_START_ADDR+EXEC_BYTES_SIZE)

/* When a file is linked against a shared library on SunOS 4, the
   dynamic bit in the exec header is set, and the first symbol in the
   symbol table is __DYNAMIC.  Its value is the address of the
   following structure.  */

struct external_sun4_dynamic
{
  /* The version number of the structure.  SunOS 4.1.x creates files
     with version number 3, which is what this structure is based on.
     According to gdb, version 2 is similar.  I believe that version 2
     used a different type of procedure linkage table, and there may
     have been other differences.  */
  bfd_byte ld_version[4];
  /* The virtual address of a 28 byte structure used in debugging.
     The contents are filled in at run time by ld.so.  */
  bfd_byte ldd[4];
  /* The virtual address of another structure with information about
     how to relocate the executable at run time.  */
  bfd_byte ld[4];
};

/* The size of the debugging structure pointed to by the debugger
   field of __DYNAMIC.  */
#define EXTERNAL_SUN4_DYNAMIC_DEBUGGER_SIZE (24)

/* The structure pointed to by the linker field of __DYNAMIC.  As far
   as I can tell, most of the addresses in this structure are offsets
   within the file, but some are actually virtual addresses.  */

struct internal_sun4_dynamic_link
{
  /* Linked list of loaded objects.  This is filled in at runtime by
     ld.so and probably by dlopen.  */
  unsigned long ld_loaded;

  /* The address of the list of names of shared objects which must be
     included at runtime.  Each entry in the list is 16 bytes: the 4
     byte address of the string naming the object (e.g., for -lc this
     is "c"); 4 bytes of flags--the high bit is whether to search for
     the object using the library path; the 2 byte major version
     number; the 2 byte minor version number; the 4 byte address of
     the next entry in the list (zero if this is the last entry).  The
     version numbers seem to only be non-zero when doing library
     searching.  */
  unsigned long ld_need;

  /* The address of the path to search for the shared objects which
     must be included.  This points to a string in PATH format which
     is generated from the -L arguments to the linker.  According to
     the man page, ld.so implicitly adds ${LD_LIBRARY_PATH} to the
     beginning of this string and /lib:/usr/lib:/usr/local/lib to the
     end.  The string is terminated by a null byte.  This field is
     zero if there is no additional path.  */
  unsigned long ld_rules;

  /* The address of the global offset table.  This appears to be a
     virtual address, not a file offset.  The first entry in the
     global offset table seems to be the virtual address of the
     sun4_dynamic structure (the same value as the __DYNAMIC symbol).
     The global offset table is used for PIC code to hold the
     addresses of variables.  A dynamically linked file which does not
     itself contain PIC code has a four byte global offset table.  */
  unsigned long ld_got;

  /* The address of the procedure linkage table.  This appears to be a
     virtual address, not a file offset.

     On a SPARC, the table is composed of 12 byte entries, each of
     which consists of three instructions.  The first entry is
         sethi %hi(0),%g1
	 jmp %g1
	 nop
     These instructions are changed by ld.so into a jump directly into
     ld.so itself.  Each subsequent entry is
         save %sp, -96, %sp
	 call <address of first entry in procedure linkage table>
	 <reloc_number | 0x01000000>
     The reloc_number is the number of the reloc to use to resolve
     this entry.  The reloc will be a JMP_SLOT reloc against some
     symbol that is not defined in this object file but should be
     defined in a shared object (if it is not, ld.so will report a
     runtime error and exit).  The constant 0x010000000 turns the
     reloc number into a sethi of %g0, which does nothing since %g0 is
     hardwired to zero.

     When one of these entries is executed, it winds up calling into
     ld.so.  ld.so looks at the reloc number, available via the return
     address, to determine which entry this is.  It then looks at the
     reloc and patches up the entry in the table into a sethi and jmp
     to the real address followed by a nop.  This means that the reloc
     lookup only has to happen once, and it also means that the
     relocation only needs to be done if the function is actually
     called.  The relocation is expensive because ld.so must look up
     the symbol by name.

     The size of the procedure linkage table is given by the ld_plt_sz
     field.  */
  unsigned long ld_plt;

  /* The address of the relocs.  These are in the same format as
     ordinary relocs.  Symbol index numbers refer to the symbols
     pointed to by ld_stab.  I think the only way to determine the
     number of relocs is to assume that all the bytes from ld_rel to
     ld_hash contain reloc entries.  */
  unsigned long ld_rel;

  /* The address of a hash table of symbols.  The hash table has
     roughly the same number of entries as there are dynamic symbols;
     I think the only way to get the exact size is to assume that
     every byte from ld_hash to ld_stab is devoted to the hash table.

     Each entry in the hash table is eight bytes.  The first four
     bytes are a symbol index into the dynamic symbols.  The second
     four bytes are the index of the next hash table entry in the
     bucket.  The ld_buckets field gives the number of buckets, say B.
     The first B entries in the hash table each start a bucket which
     is chained through the second four bytes of each entry.  A value
     of zero ends the chain.

     The hash function is simply
         h = 0;
         while (*string != '\0')
	   h = (h << 1) + *string++;
	 h &= 0x7fffffff;

     To look up a symbol, compute the hash value of the name.  Take
     the modulos of hash value and the number of buckets.  Start at
     that entry in the hash table.  See if the symbol (from the first
     four bytes of the hash table entry) has the name you are looking
     for.  If not, use the chain field (the second four bytes of the
     hash table entry) to move on to the next entry in this bucket.
     If the chain field is zero you have reached the end of the
     bucket, and the symbol is not in the hash table.  */ 
  unsigned long ld_hash;

  /* The address of the symbol table.  This is a list of
     external_nlist structures.  The string indices are relative to
     the ld_symbols field.  I think the only way to determine the
     number of symbols is to assume that all the bytes between ld_stab
     and ld_symbols are external_nlist structures.  */
  unsigned long ld_stab;

  /* I don't know what this is for.  It seems to always be zero.  */
  unsigned long ld_stab_hash;

  /* The number of buckets in the hash table.  */
  unsigned long ld_buckets;

  /* The address of the symbol string table.  The first string in this
     string table need not be the empty string.  */
  unsigned long ld_symbols;

  /* The size in bytes of the symbol string table.  */
  unsigned long ld_symb_size;

  /* The size in bytes of the text segment.  */
  unsigned long ld_text;

  /* The size in bytes of the procedure linkage table.  */
  unsigned long ld_plt_sz;
};

/* The external form of the structure.  */

struct external_sun4_dynamic_link
{
  bfd_byte ld_loaded[4];
  bfd_byte ld_need[4];
  bfd_byte ld_rules[4];
  bfd_byte ld_got[4];
  bfd_byte ld_plt[4];
  bfd_byte ld_rel[4];
  bfd_byte ld_hash[4];
  bfd_byte ld_stab[4];
  bfd_byte ld_stab_hash[4];
  bfd_byte ld_buckets[4];
  bfd_byte ld_symbols[4];
  bfd_byte ld_symb_size[4];
  bfd_byte ld_text[4];
  bfd_byte ld_plt_sz[4];
};
