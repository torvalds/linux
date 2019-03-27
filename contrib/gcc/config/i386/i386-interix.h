/* Target definitions for GCC for Intel 80386 running Interix
   Parts Copyright (C) 1991, 1999, 2000, 2002, 2003, 2004
   Free Software Foundation, Inc.

   Parts:
     by Douglas B. Rupp (drupp@cs.washington.edu).
     by Ron Guilmette (rfg@netcom.com).
     by Donn Terry (donn@softway.com).
     by Mumit Khan (khan@xraylith.wisc.edu).

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

/* The rest must follow.  */

#define DBX_DEBUGGING_INFO 1
#define SDB_DEBUGGING_INFO 1
#define PREFERRED_DEBUGGING_TYPE DBX_DEBUG

#define HANDLE_SYSV_PRAGMA 1
#undef HANDLE_PRAGMA_WEAK  /* until the link format can handle it */

/* By default, target has a 80387, uses IEEE compatible arithmetic,
   and returns float values in the 387 and needs stack probes
   We also align doubles to 64-bits for MSVC default compatibility
   We do bitfields MSVC-compatibly by default, too.  */
#undef TARGET_SUBTARGET_DEFAULT
#define TARGET_SUBTARGET_DEFAULT \
   (MASK_80387 | MASK_IEEE_FP | MASK_FLOAT_RETURNS | MASK_STACK_PROBE | \
    MASK_ALIGN_DOUBLE | MASK_MS_BITFIELD_LAYOUT)

#undef TARGET_CPU_DEFAULT
#define TARGET_CPU_DEFAULT 2 /* 486 */

#define WCHAR_TYPE_SIZE 16
#define WCHAR_TYPE "short unsigned int"

/* WinNT (and thus Interix) use unsigned int */
#define SIZE_TYPE "unsigned int"

#define ASM_LOAD_ADDR(loc, reg)   "     leal " #loc "," #reg "\n"

#define TARGET_DECLSPEC 1

/* cpp handles __STDC__ */
#define TARGET_OS_CPP_BUILTINS()					\
  do									\
    {									\
	builtin_define ("__INTERIX");					\
	builtin_define ("__OPENNT");					\
	builtin_define ("_M_IX86=300");					\
	builtin_define ("_X86_=1");					\
	builtin_define ("__stdcall=__attribute__((__stdcall__))");	\
	builtin_define ("__cdecl=__attribute__((__cdecl__))");		\
	builtin_assert ("system=unix");					\
	builtin_assert ("system=interix");				\
	if (preprocessing_asm_p ())					\
	  builtin_define_std ("LANGUAGE_ASSEMBLY");			\
	else								\
	  {								\
	     builtin_define_std ("LANGUAGE_C");				\
	     if (c_dialect_cxx ())					\
	       builtin_define_std ("LANGUAGE_C_PLUS_PLUS");		\
	     if (c_dialect_objc ())					\
	       builtin_define_std ("LANGUAGE_OBJECTIVE_C");		\
	  } 								\
    }									\
  while (0)

#undef CPP_SPEC
/* Write out the correct language type definition for the header files.  
   Unless we have assembler language, write out the symbols for C.
   mieee is an Alpha specific variant.  Cross pollination a bad idea.
   */
#define CPP_SPEC "-remap %{posix:-D_POSIX_SOURCE} \
-isystem %$INTERIX_ROOT/usr/include"

#define TARGET_VERSION fprintf (stderr, " (i386 Interix)");

/* The global __fltused is necessary to cause the printf/scanf routines
   for outputting/inputting floating point numbers to be loaded.  Since this
   is kind of hard to detect, we just do it all the time.  */
#undef X86_FILE_START_FLTUSED
#define X86_FILE_START_FLTUSED 1

/* A table of bytes codes used by the ASM_OUTPUT_ASCII and
   ASM_OUTPUT_LIMITED_STRING macros.  Each byte in the table
   corresponds to a particular byte value [0..255].  For any
   given byte value, if the value in the corresponding table
   position is zero, the given character can be output directly.
   If the table value is 1, the byte must be output as a \ooo
   octal escape.  If the tables value is anything else, then the
   byte value should be output as a \ followed by the value
   in the table.  Note that we can use standard UN*X escape
   sequences for many control characters, but we don't use
   \a to represent BEL because some svr4 assemblers (e.g. on
   the i386) don't know about that.  Also, we don't use \v
   since some versions of gas, such as 2.2 did not accept it.  */

#define ESCAPES \
"\1\1\1\1\1\1\1\1btn\1fr\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\
\0\0\"\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\
\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\\\0\0\0\
\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\1\
\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\
\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\
\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\
\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1"

/* Some svr4 assemblers have a limit on the number of characters which
   can appear in the operand of a .string directive.  If your assembler
   has such a limitation, you should define STRING_LIMIT to reflect that
   limit.  Note that at least some svr4 assemblers have a limit on the
   actual number of bytes in the double-quoted string, and that they
   count each character in an escape sequence as one byte.  Thus, an
   escape sequence like \377 would count as four bytes.

   If your target assembler doesn't support the .string directive, you
   should define this to zero.
*/

#define STRING_LIMIT	((unsigned) 256)

#define STRING_ASM_OP	"\t.string\t"

/* The routine used to output NUL terminated strings.  We use a special
   version of this for most svr4 targets because doing so makes the
   generated assembly code more compact (and thus faster to assemble)
   as well as more readable, especially for targets like the i386
   (where the only alternative is to output character sequences as
   comma separated lists of numbers).  */

#define ASM_OUTPUT_LIMITED_STRING(FILE, STR)				\
  do									\
    {									\
      const unsigned char *_limited_str =				\
        (const unsigned char *) (STR);					\
      unsigned ch;							\
      fprintf ((FILE), "%s\"", STRING_ASM_OP);				\
      for (; (ch = *_limited_str); _limited_str++)			\
        {								\
	  int escape = ESCAPES[ch];					\
	  switch (escape)						\
	    {								\
	    case 0:							\
	      putc (ch, (FILE));					\
	      break;							\
	    case 1:							\
	      fprintf ((FILE), "\\%03o", ch);				\
	      break;							\
	    default:							\
	      putc ('\\', (FILE));					\
	      putc (escape, (FILE));					\
	      break;							\
	    }								\
        }								\
      fprintf ((FILE), "\"\n");						\
    }									\
  while (0)

/* The routine used to output sequences of byte values.  We use a special
   version of this for most svr4 targets because doing so makes the
   generated assembly code more compact (and thus faster to assemble)
   as well as more readable.  Note that if we find subparts of the
   character sequence which end with NUL (and which are shorter than
   STRING_LIMIT) we output those using ASM_OUTPUT_LIMITED_STRING.  */

#undef ASM_OUTPUT_ASCII
#define ASM_OUTPUT_ASCII(FILE, STR, LENGTH)				\
  do									\
    {									\
      const unsigned char *_ascii_bytes =				\
        (const unsigned char *) (STR);					\
      const unsigned char *limit = _ascii_bytes + (LENGTH);		\
      unsigned bytes_in_chunk = 0;					\
      for (; _ascii_bytes < limit; _ascii_bytes++)			\
        {								\
	  const unsigned char *p;					\
	  if (bytes_in_chunk >= 64)					\
	    {								\
	      fputc ('\n', (FILE));					\
	      bytes_in_chunk = 0;					\
	    }								\
	  for (p = _ascii_bytes; p < limit && *p != '\0'; p++)		\
	    continue;							\
	  if (p < limit && (p - _ascii_bytes) <= (long) STRING_LIMIT)	\
	    {								\
	      if (bytes_in_chunk > 0)					\
		{							\
		  fputc ('\n', (FILE));					\
		  bytes_in_chunk = 0;					\
		}							\
	      ASM_OUTPUT_LIMITED_STRING ((FILE), _ascii_bytes);		\
	      _ascii_bytes = p;						\
	    }								\
	  else								\
	    {								\
	      if (bytes_in_chunk == 0)					\
		fprintf ((FILE), "\t.byte\t");				\
	      else							\
		fputc (',', (FILE));					\
	      fprintf ((FILE), "0x%02x", *_ascii_bytes);		\
	      bytes_in_chunk += 5;					\
	    }								\
	}								\
      if (bytes_in_chunk > 0)						\
        fprintf ((FILE), "\n");						\
    }									\
  while (0)

/* Emit code to check the stack when allocating more that 4000
   bytes in one go.  */

#define CHECK_STACK_LIMIT 0x1000

/* the following are OSF linker (not gld) specific... we don't want them */
#undef HAS_INIT_SECTION
#undef LD_INIT_SWITCH
#undef LD_FINI_SWITCH

/* The following are needed for us to be able to use winnt.c, but are not
   otherwise meaningful to Interix.  (The functions that use these are
   never called because we don't do DLLs.) */
#define TARGET_NOP_FUN_DLLIMPORT 1
#define drectve_section()  /* nothing */

/* Objective-C has its own packing rules...
   Objc tries to parallel the code in stor-layout.c at runtime	
   (see libobjc/encoding.c).  This (compile-time) packing info isn't 
   available at runtime, so it's hopeless to try.

   And if the user tries to set the flag for objc, give an error
   so he has some clue.  */

#undef  SUBTARGET_OVERRIDE_OPTIONS
#define SUBTARGET_OVERRIDE_OPTIONS					\
do {									\
  if (strcmp (lang_hooks.name, "GNU Objective-C") == 0)			\
    {									\
      if ((target_flags & MASK_MS_BITFIELD_LAYOUT) != 0			\
	  && (target_flags_explicit & MASK_MS_BITFIELD_LAYOUT) != 0)	\
	{								\
	   error ("ms-bitfields not supported for objc");		\
	}								\
      target_flags &= ~MASK_MS_BITFIELD_LAYOUT;				\
    }									\
} while (0)

#define EH_FRAME_IN_DATA_SECTION

#define READONLY_DATA_SECTION_ASM_OP	"\t.section\t.rdata,\"r\""

/* The MS compilers take alignment as a number of bytes, so we do as well */
#undef ASM_OUTPUT_ALIGN
#define ASM_OUTPUT_ALIGN(FILE,LOG) \
  if ((LOG)!=0) fprintf ((FILE), "\t.balign %d\n", 1<<(LOG))

/* The linker will take care of this, and having them causes problems with
   ld -r (specifically -rU).  */
#define CTOR_LISTS_DEFINED_EXTERNALLY 1

#define SET_ASM_OP	"\t.set\t"
/* Output a definition (implements alias) */
#define ASM_OUTPUT_DEF(FILE,LABEL1,LABEL2)				\
do									\
{									\
    fprintf ((FILE), "%s", SET_ASM_OP);					\
    assemble_name (FILE, LABEL1);					\
    fprintf (FILE, ",");						\
    assemble_name (FILE, LABEL2);					\
    fprintf (FILE, "\n");						\
    }									\
while (0)

#define HOST_PTR_AS_INT unsigned long

#define PCC_BITFIELD_TYPE_MATTERS 1

/* The following two flags are usually "off" for i386, because some non-gnu
   tools (for the i386) don't handle them.  However, we don't have that
   problem, so....  */

/* Forward references to tags are allowed.  */
#define SDB_ALLOW_FORWARD_REFERENCES

/* Unknown tags are also allowed.  */
#define SDB_ALLOW_UNKNOWN_REFERENCES

/* The integer half of this list needs to be constant.  However, there's
   a lot of disagreement about what the floating point adjustments should
   be.  We pick one that works with gdb.  (The underlying problem is
   what to do about the segment registers.  Since we have access to them
   from /proc, we'll allow them to be accessed in gdb, even tho the
   gcc compiler can't generate them.  (There's some evidence that 
   MSVC does, but possibly only for certain special "canned" sequences.) */

#undef DBX_REGISTER_NUMBER
#define DBX_REGISTER_NUMBER(n) \
(TARGET_64BIT ? dbx64_register_map[n] \
 : (n) == 0 ? 0 \
 : (n) == 1 ? 2 \
 : (n) == 2 ? 1 \
 : (n) == 3 ? 3 \
 : (n) == 4 ? 6 \
 : (n) == 5 ? 7 \
 : (n) == 6 ? 5 \
 : (n) == 7 ? 4 \
 : ((n) >= FIRST_STACK_REG && (n) <= LAST_STACK_REG) ? (n)+8 \
 : (-1))

/* Define this macro if references to a symbol must be treated
   differently depending on something about the variable or
   function named by the symbol (such as what section it is in).  */

#undef TARGET_ENCODE_SECTION_INFO
#define TARGET_ENCODE_SECTION_INFO i386_pe_encode_section_info
#undef  TARGET_STRIP_NAME_ENCODING
#define TARGET_STRIP_NAME_ENCODING  i386_pe_strip_name_encoding_full

#if 0	
/* Turn this back on when the linker is updated to handle grouped
   .data$ sections correctly. See corresponding note in i386/interix.c. 
   MK.  */

/* Interix uses explicit import from shared libraries.  */
#define MULTIPLE_SYMBOL_SPACES 1

extern void i386_pe_unique_section (tree, int);
#define TARGET_ASM_UNIQUE_SECTION i386_pe_unique_section
#define TARGET_ASM_FUNCTION_RODATA_SECTION default_no_function_rodata_section

#define SUPPORTS_ONE_ONLY 1
#endif /* 0 */

/* Switch into a generic section.  */
#define TARGET_ASM_NAMED_SECTION  default_pe_asm_named_section

/* DWARF2 Unwinding doesn't work with exception handling yet.  */
#define DWARF2_UNWIND_INFO 0

/* Don't assume anything about the header files.  */
#define NO_IMPLICIT_EXTERN_C

/* MSVC returns structs of up to 8 bytes via registers.  */

#define DEFAULT_PCC_STRUCT_RETURN 0

#undef RETURN_IN_MEMORY
#define RETURN_IN_MEMORY(TYPE) \
  (TYPE_MODE (TYPE) == BLKmode || \
     (AGGREGATE_TYPE_P (TYPE) && int_size_in_bytes(TYPE) > 8 ))
