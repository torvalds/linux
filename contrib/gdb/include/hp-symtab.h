/* Definitions and structures for reading debug symbols from the
   native HP C compiler.

   Written by the Center for Software Science at the University of Utah
   and by Cygnus Support.

   Copyright 1994, 1995, 1998, 1999, 2003 Free Software Foundation, Inc.

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

#ifndef HP_SYMTAB_INCLUDED
#define HP_SYMTAB_INCLUDED

/* General information:

   This header file defines and describes only the data structures
   necessary to read debug symbols produced by the HP C compiler,
   HP ANSI C++ compiler, and HP FORTRAN 90 compiler using the
   SOM object file format.  
   (For a full description of the debug format, ftp hpux-symtab.h from
   jaguar.cs.utah.edu:/dist).
   
   Additional notes (Rich Title)
   This file is a reverse-engineered version of a file called
   "symtab.h" which exists internal to HP's Computer Languages Organization
   in /CLO/Components/DDE/obj/som/symtab.h. Because HP's version of
   the file is copyrighted and not distributed, it is necessary for
   GDB to use the reverse-engineered version that follows.
   Work was done by Cygnus to reverse-engineer the C subset of symtab.h.
   The WDB project has extended this to also contain the C++ 
   symbol definitions, the F90 symbol definitions, 
   and the DOC (debugging-optimized-code) symbol definitions.
   In some cases (the C++ symbol definitions)
   I have added internal documentation here that
   goes beyond what is supplied in HP's symtab.h. If we someday
   unify these files again, the extra comments should be merged back
   into HP's symtab.h.
  
   -------------------------------------------------------------------

   Debug symbols are contained entirely within an unloadable space called
   $DEBUG$.  $DEBUG$ contains several subspaces which group related
   debug symbols.

   $GNTT$ contains information for global variables, types and contants.

   $LNTT$ contains information for procedures (including nesting), scoping
   information, local variables, types, and constants.

   $SLT$ contains source line information so that code addresses may be
   mapped to source lines.

   $VT$ contains various strings and constants for named objects (variables,
   typedefs, functions, etc).  Strings are stored as null-terminated character
   lists.  Constants always begin on word boundaries.  The first byte of
   the VT must be zero (a null string).

   $XT$ is not currently used by GDB.

   Many structures within the subspaces point to other structures within
   the same subspace, or to structures within a different subspace.  These
   pointers are represented as a structure index from the beginning of
   the appropriate subspace.  */

/* Used to describe where a constant is stored.  */
enum location_type
{
  LOCATION_IMMEDIATE,
  LOCATION_PTR,
  LOCATION_VT,
};

/* Languages supported by this debug format.  Within the data structures
   this type is limited to 4 bits for a maximum of 16 languages.  */
enum hp_language
{
  HP_LANGUAGE_UNKNOWN,
  HP_LANGUAGE_C,
  HP_LANGUAGE_FORTRAN,
  HP_LANGUAGE_F77 = HP_LANGUAGE_FORTRAN,
  HP_LANGUAGE_PASCAL,
  HP_LANGUAGE_MODCAL,
  HP_LANGUAGE_COBOL,
  HP_LANGUAGE_BASIC,
  HP_LANGUAGE_ADA,
  HP_LANGUAGE_CPLUSPLUS,
  HP_LANGUAGE_DMPASCAL
};


/* Basic data types available in this debug format.  Within the data
   structures this type is limited to 5 bits for a maximum of 32 basic
   data types.  */
enum hp_type
{
  HP_TYPE_UNDEFINED, /* 0 */
  HP_TYPE_BOOLEAN, /* 1 */
  HP_TYPE_CHAR, /* 2 */
  HP_TYPE_INT, /* 3 */
  HP_TYPE_UNSIGNED_INT, /* 4 */
  HP_TYPE_REAL, /* 5 */
  HP_TYPE_COMPLEX, /* 6 */
  HP_TYPE_STRING200, /* 7 */
  HP_TYPE_LONGSTRING200, /* 8 */
  HP_TYPE_TEXT, /* 9 */
  HP_TYPE_FLABEL, /* 10 */
  HP_TYPE_FTN_STRING_SPEC, /* 11 */
  HP_TYPE_MOD_STRING_SPEC, /* 12 */
  HP_TYPE_PACKED_DECIMAL, /* 13 */
  HP_TYPE_REAL_3000, /* 14 */
  HP_TYPE_MOD_STRING_3000, /* 15 */
  HP_TYPE_ANYPOINTER, /* 16 */
  HP_TYPE_GLOBAL_ANYPOINTER, /* 17 */
  HP_TYPE_LOCAL_ANYPOINTER, /* 18 */
  HP_TYPE_COMPLEXS3000, /* 19 */
  HP_TYPE_FTN_STRING_S300_COMPAT, /* 20 */ 
  HP_TYPE_FTN_STRING_VAX_COMPAT, /* 21 */
  HP_TYPE_BOOLEAN_S300_COMPAT, /* 22 */
  HP_TYPE_BOOLEAN_VAX_COMPAT, /* 23 */
  HP_TYPE_WIDE_CHAR, /* 24 */
  HP_TYPE_LONG, /* 25 */
  HP_TYPE_UNSIGNED_LONG, /* 26 */
  HP_TYPE_DOUBLE, /* 27 */
  HP_TYPE_TEMPLATE_ARG, /* 28 */
  HP_TYPE_VOID /* 29 */
};

/* An immediate name and type table entry.

   extension and immediate will always be one.
   global will always be zero.
   hp_type is the basic type this entry describes.
   bitlength is the length in bits for the basic type.  */
struct dnttp_immediate
{
  unsigned int extension:	1;
  unsigned int immediate:	1;
  unsigned int global:		1;
  unsigned int type: 		5;
  unsigned int bitlength:	24;
};

/* A nonimmediate name and type table entry.

   extension will always be one.
   immediate will always be zero.
   if global is zero, this entry points into the LNTT
   if global is one, this entry points into the GNTT
   index is the index within the GNTT or LNTT for this entry.  */
struct dnttp_nonimmediate
{
  unsigned int extension:	1;
  unsigned int immediate:	1;
  unsigned int global:		1;
  unsigned int index:		29;
};

/* A pointer to an entry in the GNTT and LNTT tables.  It has two
   forms depending on the type being described.

   The immediate form is used for simple entries and is one
   word.

   The nonimmediate form is used for complex entries and contains
   an index into the LNTT or GNTT which describes the entire type.

   If a dnttpointer is -1, then it is a NIL entry.  */

#define DNTTNIL (-1)
typedef union dnttpointer
{
  struct dnttp_immediate    dntti;
  struct dnttp_nonimmediate dnttp;
  int word;
} dnttpointer;

/* An index into the source line table.  As with dnttpointers, a sltpointer
   of -1 indicates a NIL entry.  */
#define SLTNIL (-1)
typedef int sltpointer;

/* Index into DOC (= "Debugging Optimized Code") line table.  */
#define LTNIL (-1)
typedef int ltpointer;

/* Index into context table.  */
#define CTXTNIL (-1)
typedef int ctxtpointer;

/* Unsigned byte offset into the VT.  */
typedef unsigned int vtpointer;

/* A DNTT entry (used within the GNTT and LNTT).

   DNTT entries are variable sized objects, but are always a multiple
   of 3 words (we call each group of 3 words a "block").

   The first bit in each block is an extension bit.  This bit is zero
   for the first block of a DNTT entry.  If the entry requires more
   than one block, then this bit is set to one in all blocks after
   the first one.  */

/* Each DNTT entry describes a particular debug symbol (beginning of
   a source file, a function, variables, structures, etc.

   The type of the DNTT entry is stored in the "kind" field within the
   DNTT entry itself.  */

enum dntt_entry_type
{
  DNTT_TYPE_NIL = -1,
  DNTT_TYPE_SRCFILE,
  DNTT_TYPE_MODULE,
  DNTT_TYPE_FUNCTION,
  DNTT_TYPE_ENTRY,
  DNTT_TYPE_BEGIN,
  DNTT_TYPE_END,
  DNTT_TYPE_IMPORT,
  DNTT_TYPE_LABEL,
  DNTT_TYPE_FPARAM,
  DNTT_TYPE_SVAR,
  DNTT_TYPE_DVAR,
  DNTT_TYPE_HOLE1,
  DNTT_TYPE_CONST,
  DNTT_TYPE_TYPEDEF,
  DNTT_TYPE_TAGDEF,
  DNTT_TYPE_POINTER,
  DNTT_TYPE_ENUM,
  DNTT_TYPE_MEMENUM,
  DNTT_TYPE_SET,
  DNTT_TYPE_SUBRANGE,
  DNTT_TYPE_ARRAY,
  DNTT_TYPE_STRUCT,
  DNTT_TYPE_UNION,
  DNTT_TYPE_FIELD,
  DNTT_TYPE_VARIANT,
  DNTT_TYPE_FILE,
  DNTT_TYPE_FUNCTYPE,
  DNTT_TYPE_WITH,
  DNTT_TYPE_COMMON,
  DNTT_TYPE_COBSTRUCT,
  DNTT_TYPE_XREF,
  DNTT_TYPE_SA,
  DNTT_TYPE_MACRO,
  DNTT_TYPE_BLOCKDATA,
  DNTT_TYPE_CLASS_SCOPE,
  DNTT_TYPE_REFERENCE,
  DNTT_TYPE_PTRMEM,
  DNTT_TYPE_PTRMEMFUNC,
  DNTT_TYPE_CLASS,
  DNTT_TYPE_GENFIELD,
  DNTT_TYPE_VFUNC,
  DNTT_TYPE_MEMACCESS,
  DNTT_TYPE_INHERITANCE,
  DNTT_TYPE_FRIEND_CLASS,
  DNTT_TYPE_FRIEND_FUNC,
  DNTT_TYPE_MODIFIER,
  DNTT_TYPE_OBJECT_ID,
  DNTT_TYPE_MEMFUNC,
  DNTT_TYPE_TEMPLATE,
  DNTT_TYPE_TEMPLATE_ARG,
  DNTT_TYPE_FUNC_TEMPLATE,
  DNTT_TYPE_LINK,
  DNTT_TYPE_DYN_ARRAY_DESC,
  DNTT_TYPE_DESC_SUBRANGE,
  DNTT_TYPE_BEGIN_EXT,
  DNTT_TYPE_INLN,
  DNTT_TYPE_INLN_LIST,
  DNTT_TYPE_ALIAS,
  DNTT_TYPE_DOC_FUNCTION,
  DNTT_TYPE_DOC_MEMFUNC,
  DNTT_TYPE_MAX
};

/* DNTT_TYPE_SRCFILE:

   One DNTT_TYPE_SRCFILE symbol is output for the start of each source
   file and at the begin and end of an included file.  A DNTT_TYPE_SRCFILE
   entry is also output before each DNTT_TYPE_FUNC symbol so that debuggers
   can determine what file a function was defined in.

   LANGUAGE describes the source file's language.

   NAME points to an VT entry providing the source file's name.

   Note the name used for DNTT_TYPE_SRCFILE entries are exactly as seen
   by the compiler (ie they may be relative or absolute).  C include files
   via <> inclusion must use absolute paths.

   ADDRESS points to an SLT entry from which line number and code locations
   may be determined.  */

struct dntt_type_srcfile
{
  unsigned int extension:	1;
  unsigned int kind:		10;    /* DNTT_TYPE_SRCFILE */
  unsigned int language:	4;
  unsigned int unused:		17;
  vtpointer name;
  sltpointer address;
};

/* DNTT_TYPE_MODULE:

   A DNTT_TYPE_MODULE symbol is emitted for the start of a pascal
   module or C source file. A module indicates a compilation unit
   for name-scoping purposes; in that regard there should be 
   a 1-1 correspondence between GDB "symtab"'s and MODULE symbol records.

   Each DNTT_TYPE_MODULE must have an associated DNTT_TYPE_END symbol.

   NAME points to a VT entry providing the module's name.  Note C
   source files are considered nameless modules.

   ALIAS point to a VT entry providing a secondary name.

   ADDRESS points to an SLT entry from which line number and code locations
   may be determined.  */

struct dntt_type_module
{
  unsigned int extension:	1;
  unsigned int kind:		10; 	/* DNTT_TYPE_MODULE */
  unsigned int unused:		21;
  vtpointer name;
  vtpointer alias;
  dnttpointer unused2;
  sltpointer address;
};

/* DNTT_TYPE_FUNCTION,
   DNTT_TYPE_ENTRY,
   DNTT_TYPE_BLOCKDATA,
   DNTT_TYPE_MEMFUNC:

   A DNTT_TYPE_FUNCTION symbol is emitted for each function definition;
   a DNTT_TYPE_ENTRY symbols is used for secondary entry points.  Both
   symbols used the dntt_type_function structure.
   A DNTT_TYPE_BLOCKDATA symbol is emitted ...?
   A DNTT_TYPE_MEMFUNC symbol is emitted for inlined member functions (C++). 

   Each of DNTT_TYPE_FUNCTION must have a matching DNTT_TYPE_END.

   GLOBAL is nonzero if the function has global scope.

   LANGUAGE describes the function's source language.

   OPT_LEVEL describes the optimization level the function was compiled
   with.

   VARARGS is nonzero if the function uses varargs.

   NAME points to a VT entry providing the function's name.

   ALIAS points to a VT entry providing a secondary name for the function.

   FIRSTPARAM points to a LNTT entry which describes the parameter list.

   ADDRESS points to an SLT entry from which line number and code locations
   may be determined.

   ENTRYADDR is the memory address corresponding the function's entry point

   RETVAL points to a LNTT entry describing the function's return value.

   LOWADDR is the lowest memory address associated with this function.

   HIADDR is the highest memory address associated with this function.  */

struct dntt_type_function
{
  unsigned int extension:	1;
  unsigned int kind:		10;	/* DNTT_TYPE_FUNCTION,
				           DNTT_TYPE_ENTRY,
					   DNTT_TYPE_BLOCKDATA
					   or DNTT_TYPE_MEMFUNC */
  unsigned int global:		1;
  unsigned int language:	4;
  unsigned int nest_level:	5;
  unsigned int opt_level:	2;
  unsigned int varargs:		1;
  unsigned int lang_info:	4;
  unsigned int inlined:		1;
  unsigned int localalloc:	1;
  unsigned int expansion:	1;
  unsigned int unused:		1;
  vtpointer name;
  vtpointer alias;
  dnttpointer firstparam;
  sltpointer address;
  CORE_ADDR entryaddr;
  dnttpointer retval;
  CORE_ADDR lowaddr;
  CORE_ADDR hiaddr;
};

/* DNTT_TYPE_BEGIN:

   A DNTT_TYPE_BEGIN symbol is emitted to begin a new nested scope.
   Every DNTT_TYPE_BEGIN symbol must have a matching DNTT_TYPE_END symbol.

   CLASSFLAG is nonzero if this is the beginning of a c++ class definition.

   ADDRESS points to an SLT entry from which line number and code locations
   may be determined.  */

struct dntt_type_begin
{
  unsigned int extension:	1;
  unsigned int kind:		10;
  unsigned int classflag:	1;
  unsigned int unused:		20;
  sltpointer address;
};

/* DNTT_TYPE_END:

   A DNTT_TYPE_END symbol is emitted when closing a scope started by
   a DNTT_TYPE_MODULE, DNTT_TYPE_FUNCTION, DNTT_TYPE_WITH,
   DNTT_TYPE_COMMON, DNTT_TYPE_BEGIN, and DNTT_TYPE_CLASS_SCOPE symbols.

   ENDKIND describes what type of scope the DNTT_TYPE_END is closing
   (one of the above 6 kinds).

   CLASSFLAG is nonzero if this is the end of a c++ class definition.

   ADDRESS points to an SLT entry from which line number and code locations
   may be determined.

   BEGINSCOPE points to the LNTT entry which opened the scope.  */

struct dntt_type_end
{
  unsigned int extension:	1;
  unsigned int kind:		10;
  unsigned int endkind:		10;
  unsigned int classflag:	1;
  unsigned int unused:		10;
  sltpointer address;
  dnttpointer beginscope;
};

/* DNTT_TYPE_IMPORT is unused by GDB.  */
/* DNTT_TYPE_LABEL is unused by GDB.  */

/* DNTT_TYPE_FPARAM:

   A DNTT_TYPE_FPARAM symbol is emitted for a function argument.  When
   chained together the symbols represent an argument list for a function.

   REGPARAM is nonzero if this parameter was passed in a register.

   INDIRECT is nonzero if this parameter is a pointer to the parameter
   (pass by reference or pass by value for large items).

   LONGADDR is nonzero if the parameter is a 64bit pointer.

   NAME is a pointer into the VT for the parameter's name.

   LOCATION describes where the parameter is stored.  Depending on the
   parameter type LOCATION could be a register number, or an offset
   from the stack pointer.

   TYPE points to a NTT entry describing the type of this parameter.

   NEXTPARAM points to the LNTT entry describing the next parameter.  */

struct dntt_type_fparam
{
  unsigned int extension:	1;
  unsigned int kind:		10;
  unsigned int regparam:	1;
  unsigned int indirect:	1;
  unsigned int longaddr:	1;
  unsigned int copyparam:	1;
  unsigned int dflt:		1;
  unsigned int doc_ranges:	1;
  unsigned int misc_kind:       1;
  unsigned int unused:		14;
  vtpointer name;
  CORE_ADDR location;
  dnttpointer type;
  dnttpointer nextparam;
  int misc;
};

/* DNTT_TYPE_SVAR:

   A DNTT_TYPE_SVAR is emitted to describe a variable in static storage.

   GLOBAL is nonzero if the variable has global scope.

   INDIRECT is nonzero if the variable is a pointer to an object.

   LONGADDR is nonzero if the variable is in long pointer space.

   STATICMEM is nonzero if the variable is a member of a class.

   A_UNION is nonzero if the variable is an anonymous union member.

   NAME is a pointer into the VT for the variable's name.

   LOCATION provides the memory address for the variable.

   TYPE is a pointer into either the GNTT or LNTT which describes
   the type of this variable.  */

struct dntt_type_svar
{
  unsigned int extension:	1;
  unsigned int kind:		10;
  unsigned int global:		1;
  unsigned int indirect:	1;
  unsigned int longaddr:	1;
  unsigned int staticmem:	1;
  unsigned int a_union:		1;
  unsigned int unused1:         1;
  unsigned int thread_specific: 1;
  unsigned int unused2:         14;
  vtpointer name;
  CORE_ADDR location;
  dnttpointer type;
  unsigned int offset;
  unsigned int displacement;
};

/* DNTT_TYPE_DVAR:

   A DNTT_TYPE_DVAR is emitted to describe automatic variables and variables
   held in registers.

   GLOBAL is nonzero if the variable has global scope.

   INDIRECT is nonzero if the variable is a pointer to an object.

   REGVAR is nonzero if the variable is in a register.

   A_UNION is nonzero if the variable is an anonymous union member.

   NAME is a pointer into the VT for the variable's name.

   LOCATION provides the memory address or register number for the variable.

   TYPE is a pointer into either the GNTT or LNTT which describes
   the type of this variable.  */

struct dntt_type_dvar
{
  unsigned int extension:	1;
  unsigned int kind:		10;
  unsigned int global:		1;
  unsigned int indirect:	1;
  unsigned int regvar:		1;
  unsigned int a_union:		1;
  unsigned int unused:		17;
  vtpointer name;
  int location;
  dnttpointer type;
  unsigned int offset;
};

/* DNTT_TYPE_CONST:

   A DNTT_TYPE_CONST symbol is emitted for program constants.

   GLOBAL is nonzero if the constant has global scope.

   INDIRECT is nonzero if the constant is a pointer to an object.

   LOCATION_TYPE describes where to find the constant's value
   (in the VT, memory, or embedded in an instruction).

   CLASSMEM is nonzero if the constant is a member of a class.

   NAME is a pointer into the VT for the constant's name.

   LOCATION provides the memory address, register number or pointer
   into the VT for the constant's value.

   TYPE is a pointer into either the GNTT or LNTT which describes
   the type of this variable.  */

struct dntt_type_const
{
  unsigned int extension:	1;
  unsigned int kind:		10;
  unsigned int global:		1;
  unsigned int indirect:	1;
  unsigned int location_type:	3;
  unsigned int classmem:	1;
  unsigned int unused:		15;
  vtpointer name;
  CORE_ADDR location;
  dnttpointer type;
  unsigned int offset;
  unsigned int displacement;
};

/* DNTT_TYPE_TYPEDEF and DNTT_TYPE_TAGDEF:

   The same structure is used to describe typedefs and tagdefs.

   DNTT_TYPE_TYPEDEFS are associated with C "typedefs".

   DNTT_TYPE_TAGDEFs are associated with C "struct", "union", and "enum"
   tags, which may have the same name as a typedef in the same scope.
   Also they are associated with C++ "class" tags, which implicitly have 
   the same name as the class type.

   GLOBAL is nonzero if the typedef/tagdef has global scope.

   TYPEINFO is used to determine if full type information is available
   for a tag.  (usually 1, but can be zero for opaque types in C).

   NAME is a pointer into the VT for the constant's name.

   TYPE points to the underlying type for the typedef/tagdef in the
   GNTT or LNTT.  */

struct dntt_type_type
{
  unsigned int extension:	1;
  unsigned int kind:		10;    /* DNTT_TYPE_TYPEDEF or 
                                          DNTT_TYPE_TAGDEF.  */
  unsigned int global:		1;
  unsigned int typeinfo:	1;
  unsigned int unused:		19;
  vtpointer name;
  dnttpointer type;                    /* Underlying type, which for TAGDEF's may be
                                          DNTT_TYPE_STRUCT, DNTT_TYPE_UNION,
                                          DNTT_TYPE_ENUM, or DNTT_TYPE_CLASS. 
                                          For TYPEDEF's other underlying types
                                          are also possible.  */
};

/* DNTT_TYPE_POINTER:

   Used to describe a pointer to an underlying type.

   POINTSTO is a pointer into the GNTT or LNTT for the type which this
   pointer points to.

   BITLENGTH is the length of the pointer (not the underlying type). */

struct dntt_type_pointer
{
  unsigned int extension:	1;
  unsigned int kind:		10;
  unsigned int unused:		21;
  dnttpointer pointsto;
  unsigned int bitlength;
};


/* DNTT_TYPE_ENUM:

   Used to describe enumerated types.

   FIRSTMEM is a pointer to a DNTT_TYPE_MEMENUM in the GNTT/LNTT which
   describes the first member (and contains a pointer to the chain of
   members).

   BITLENGTH is the number of bits used to hold the values of the enum's
   members.  */

struct dntt_type_enum
{
  unsigned int extension:	1;
  unsigned int kind:	10;
  unsigned int unused:		21;
  dnttpointer firstmem;
  unsigned int bitlength;
};

/* DNTT_TYPE_MEMENUM

   Used to describe members of an enumerated type.

   CLASSMEM is nonzero if this member is part of a class.

   NAME points into the VT for the name of this member.

   VALUE is the value of this enumeration member.

   NEXTMEM points to the next DNTT_TYPE_MEMENUM in the chain.  */

struct dntt_type_memenum
{
  unsigned int extension:	1;
  unsigned int kind:	10;
  unsigned int classmem:	1;
  unsigned int unused:		20;
  vtpointer name;
  unsigned int value;
  dnttpointer nextmem;
};

/* DNTT_TYPE_SET

   Used to describe PASCAL "set" type.

   DECLARATION describes the bitpacking of the set.

   SUBTYPE points to a DNTT entry describing the type of the members.

   BITLENGTH is the size of the set.  */ 

struct dntt_type_set
{
  unsigned int extension:	1;
  unsigned int kind:	10;
  unsigned int declaration:	2;
  unsigned int unused:		19;
  dnttpointer subtype;
  unsigned int bitlength;
};

/* DNTT_TYPE_SUBRANGE

   Used to describe subrange type.

   DYN_LOW describes the lower bound of the subrange:

     00 for a constant lower bound (found in LOWBOUND).

     01 for a dynamic lower bound with the lower bound found in the
     memory address pointed to by LOWBOUND.

     10 for a dynamic lower bound described by an variable found in the
     DNTT/LNTT (LOWBOUND would be a pointer into the DNTT/LNTT).

   DYN_HIGH is similar to DYN_LOW, except it describes the upper bound.

   SUBTYPE points to the type of the subrange.

   BITLENGTH is the length in bits needed to describe the subrange's
   values.  */

struct dntt_type_subrange
{
  unsigned int extension:	1;
  unsigned int kind:	10;
  unsigned int dyn_low:		2;
  unsigned int dyn_high:	2;
  unsigned int unused:		17;
  int lowbound;
  int highbound;
  dnttpointer subtype;
  unsigned int bitlength;
};

/* DNTT_TYPE_ARRAY

   Used to describe an array type.

   DECLARATION describes the bit packing used in the array.

   ARRAYISBYTES is nonzero if the field in arraylength describes the
   length in bytes rather than in bits.  A value of zero is used to
   describe an array with size 2**32.

   ELEMISBYTES is nonzero if the length if each element in the array
   is describes in bytes rather than bits.  A value of zero is used
   to an element with size 2**32.

   ELEMORDER is nonzero if the elements are indexed in increasing order.

   JUSTIFIED if the elements are left justified to index zero.

   ARRAYLENGTH is the length of the array.

   INDEXTYPE is a DNTT pointer to the type used to index the array.

   ELEMTYPE is a DNTT pointer to the type for the array elements.

   ELEMLENGTH is the length of each element in the array (including
   any padding).

   Multi-dimensional arrays are represented by ELEMTYPE pointing to
   another DNTT_TYPE_ARRAY.  */

struct dntt_type_array
{
  unsigned int extension:	1;
  unsigned int kind:	10;
  unsigned int declaration:	2;
  unsigned int dyn_low:		2;
  unsigned int dyn_high:	2;
  unsigned int arrayisbytes:	1;
  unsigned int elemisbytes:	1;
  unsigned int elemorder:	1;
  unsigned int justified:	1;
  unsigned int unused:		11;
  unsigned int arraylength;
  dnttpointer indextype;
  dnttpointer elemtype;
  unsigned int elemlength;
};

/* DNTT_TYPE_STRUCT

   DNTT_TYPE_STRUCT is used to describe a C structure.

   DECLARATION describes the bitpacking used.

   FIRSTFIELD is a DNTT pointer to the first field of the structure
   (each field contains a pointer to the next field, walk the list
   to access all fields of the structure).

   VARTAGFIELD and VARLIST are used for Pascal variant records.

   BITLENGTH is the size of the structure in bits.  */

struct dntt_type_struct
{
  unsigned int extension:	1;
  unsigned int kind:	10;
  unsigned int declaration:	2;
  unsigned int unused:		19;
  dnttpointer firstfield;
  dnttpointer vartagfield;
  dnttpointer varlist;
  unsigned int bitlength;
};

/* DNTT_TYPE_UNION

   DNTT_TYPE_UNION is used to describe a C union.

   FIRSTFIELD is a DNTT pointer to the beginning of the field chain.

   BITLENGTH is the size of the union in bits.  */

struct dntt_type_union
{
  unsigned int extension:	1;
  unsigned int kind:	10;
  unsigned int unused:		21;
  dnttpointer firstfield;
  unsigned int bitlength;
};

/* DNTT_TYPE_FIELD

   DNTT_TYPE_FIELD describes one field in a structure or union
   or C++ class.

   VISIBILITY is used to describe the visibility of the field
   (for c++.  public = 0, protected = 1, private = 2).

   A_UNION is nonzero if this field is a member of an anonymous union.

   STATICMEM is nonzero if this field is a static member of a template.

   NAME is a pointer into the VT for the name of the field.

   BITOFFSET gives the offset of this field in bits from the beginning
   of the structure or union this field is a member of.

   TYPE is a DNTT pointer to the type describing this field.

   BITLENGTH is the size of the entry in bits.

   NEXTFIELD is a DNTT pointer to the next field in the chain.  */

struct dntt_type_field
{
  unsigned int extension:	1;
  unsigned int kind:	10;
  unsigned int visibility:	2;
  unsigned int a_union:		1;
  unsigned int staticmem:	1;
  unsigned int unused:		17;
  vtpointer name;
  unsigned int bitoffset;
  dnttpointer type;
  unsigned int bitlength;
  dnttpointer nextfield;
};

/* DNTT_TYPE_VARIANT is unused by GDB.  */
/* DNTT_TYPE_FILE is unused by GDB.  */

/* DNTT_TYPE_FUNCTYPE

   I think this is used to describe a function type (e.g., would
   be emitted as part of a function-pointer description).

   VARARGS is nonzero if this function uses varargs.

   FIRSTPARAM is a DNTT pointer to the first entry in the parameter
   chain.

   RETVAL is a DNTT pointer to the type of the return value.  */

struct dntt_type_functype
{
  unsigned int extension:	1;
  unsigned int kind:		10;
  unsigned int varargs:		1;
  unsigned int info:		4;
  unsigned int unused:		16;
  unsigned int bitlength;
  dnttpointer firstparam;
  dnttpointer retval;
};

/* DNTT_TYPE_WITH is emitted by C++ to indicate "with" scoping semantics.
   (Probably also emitted by PASCAL to support "with"...).
   
   C++ example: Say "memfunc" is a method of class "c", and say
   "m" is a data member of class "c". Then from within "memfunc",
   it is legal to reference "m" directly (e.g. you don't have to
   say "this->m". The symbol table indicates
   this by emitting a DNTT_TYPE_WITH symbol within the function "memfunc",
   pointing to the type symbol for class "c".
 
   In GDB, this symbol record is unnecessary, 
   because GDB's symbol lookup algorithm
   infers the "with" semantics when it sees a "this" argument to the member
   function. So GDB can safely ignore the DNTT_TYPE_WITH record.

   A DNTT_TYPE_WITH has a matching DNTT_TYPE_END symbol.  */

struct dntt_type_with
{
  unsigned int extension:	1;    /* always zero */
  unsigned int kind:		10;   /* always DNTT_TYPE_WITH */
  unsigned int addrtype:  	2;    /* 0 => STATTYPE                */
                                      /* 1 => DYNTYPE                 */
                                      /* 2 => REGTYPE                 */
  unsigned int indirect: 	1;    /* 1 => pointer to object       */
  unsigned int longaddr:  	1;    /* 1 => in long pointer space   */
  unsigned int nestlevel: 	6;    /* # of nesting levels back     */
  unsigned int doc_ranges: 	1;    /* 1 => location is range list  */
  unsigned int unused:   	10;
  long location;       		      /* where stored (allocated)     */
  sltpointer address;
  dnttpointer type;                   /* type of with expression      */
  vtpointer name;                     /* name of with expression      */
  unsigned long  offset;              /* byte offset from location    */
};                                   

/* DNTT_TYPE_COMMON is unsupported by GDB.  */
/* A DNTT_TYPE_COMMON symbol must have a matching DNTT_TYPE_END symbol */

/* DNTT_TYPE_COBSTRUCT is unsupported by GDB.  */
/* DNTT_TYPE_XREF is unsupported by GDB.  */
/* DNTT_TYPE_SA is unsupported by GDB.  */
/* DNTT_TYPE_MACRO is unsupported by GDB */

/* DNTT_TYPE_BLOCKDATA has the same structure as DNTT_TYPE_FUNCTION */

/* The following are the C++ specific SOM records */

/*  The purpose of the DNTT_TYPE_CLASS_SCOPE is to bracket C++ methods
    and indicate the method name belongs in the "class scope" rather
    than in the module they are being defined in. For example:

    class c {
    ...
    void memfunc(); // member function
    };

    void c::memfunc()   // definition of class c's "memfunc"
    {
    ...
    }

    main()
    {
    ...
    }

    In the above, the name "memfunc" is not directly visible from "main".
    I.e., you have to say "break c::memfunc".
    If it were a normal function (not a method), it would be visible
    via the simple "break memfunc". Since "memfunc" otherwise looks
    like a normal FUNCTION in the symbol table, the bracketing
    CLASS_SCOPE is what is used to indicate it is really a method.
    

   A DNTT_TYPE_CLASS_SCOPE symbol must have a matching DNTT_TYPE_END symbol.  */

struct dntt_type_class_scope
{
  unsigned int extension:   1;	   /* Always zero.  */
  unsigned int kind:       10;     /* Always DNTT_TYPE_CLASS_SCOPE.  */
  unsigned int unused:     21; 
  sltpointer address         ;     /* Pointer to SLT entry.  */
  dnttpointer type           ;     /* Pointer to class type DNTT.  */
};

/* C++ reference parameter.
   The structure of this record is the same as DNTT_TYPE_POINTER - 
   refer to struct dntt_type_pointer.  */

/* The next two describe C++ pointer-to-data-member type, and 
   pointer-to-member-function type, respectively.
   DNTT_TYPE_PTRMEM and DNTT_TYPE_PTRMEMFUNC have the same structure.  */

struct dntt_type_ptrmem
{
  unsigned int extension:   1;	   /* Always zero.  */
  unsigned int kind:       10;     /* Always DNTT_TYPE_PTRMEM.  */
  unsigned int unused:	   21;
  dnttpointer pointsto	     ;     /* Pointer to class DNTT.  */
  dnttpointer memtype 	     ;     /* Type of member.  */
};

struct dntt_type_ptrmemfunc
{
  unsigned int extension:   1;	   /* Always zero.  */
  unsigned int kind:       10;     /* Always DNTT_TYPE_PTRMEMFUNC.  */
  unsigned int unused:	   21;
  dnttpointer pointsto	     ;     /* Pointer to class DNTT.  */
  dnttpointer memtype 	     ;     /* Type of member.  */
};

/* The DNTT_TYPE_CLASS symbol is emitted to describe a class type.
   "memberlist" points to a chained list of FIELD or GENFIELD records
   indicating the class members. "parentlist" points to a chained list
   of INHERITANCE records indicating classes from which we inherit
   fields.  */

struct dntt_type_class 
{
  unsigned int extension:   1;     /* Always zero.  */
  unsigned int kind:       10;     /* Always DNTT_TYPE_CLASS.  */
  unsigned int abstract:    1;     /* Is this an abstract class?  */
  unsigned int class_decl:  2;     /* 0=class,1=union,2=struct.  */
  unsigned int expansion:   1;     /* 1=template expansion.  */
  unsigned int unused:     17;     
  dnttpointer memberlist     ;     /* Ptr to chain of [GEN]FIELDs.  */
  unsigned long vtbl_loc     ;     /* Offset in obj of ptr to vtbl.  */
  dnttpointer parentlist     ;     /* Ptr to K_INHERITANCE list.  */
  unsigned long bitlength    ;     /* Total at this level.  */
  dnttpointer identlist      ;     /* Ptr to chain of class ident's.  */
  dnttpointer friendlist     ;     /* Ptr to K_FRIEND list.  */
  dnttpointer templateptr    ;     /* Ptr to template.  */
  dnttpointer nextexp        ;     /* Ptr to next expansion.  */
};

/* Class members are indicated via either the FIELD record (for
   data members, same as for C struct fields), or by the GENFIELD record
   (for member functions).  */

struct dntt_type_genfield
{
  unsigned int extension:   1;	   /* Always zero.  */
  unsigned int kind:       10;     /* Always DNTT_TYPE_GENFIELD.  */
  unsigned int visibility:  2;     /* Pub = 0, prot = 1, priv = 2.  */
  unsigned int a_union:     1;     /* 1 => anonymous union member.  */
  unsigned int unused:	   18;
  dnttpointer field	     ;     /* Pointer to field or qualifier.  */
  dnttpointer nextfield      ;     /* Pointer to next field.  */
};

/* C++ virtual functions.  */

struct dntt_type_vfunc
{
  unsigned int extension:   1;	   /* always zero */
  unsigned int kind:       10;     /* always DNTT_TYPE_VFUNC */
  unsigned int pure:        1;     /* pure virtual function ?       */
  unsigned int unused:	   20;
  dnttpointer funcptr        ;     /* points to FUNCTION symbol     */
  unsigned long vtbl_offset  ;     /* offset into vtbl for virtual  */
};

/* Not precisely sure what this is intended for - DDE ignores it.  */

struct dntt_type_memaccess
{
  unsigned int extension:   1;	   /* always zero */
  unsigned int kind:       10;     /* always DNTT_TYPE_MEMACCESS */
  unsigned int unused:	   21;
  dnttpointer classptr	     ;     /* pointer to base class         */
  dnttpointer field          ;     /* pointer field                 */
};

/* The DNTT_TYPE_INHERITANCE record describes derived classes.
   In particular, the "parentlist" field of the CLASS record points
   to a list of INHERITANCE records for classes from which we 
   inherit members.  */

struct dntt_type_inheritance
{
  unsigned int extension:   1;	   /* always zero */
  unsigned int kind:       10;     /* always DNTT_TYPE_INHERITANCE */
  unsigned int Virtual:     1;     /* virtual base class ?          */
  unsigned int visibility:  2;     /* pub = 0, prot = 1, priv = 2   */
  unsigned int unused:	   18;
  dnttpointer classname      ;     /* first parent class, if any    */
  unsigned long offset       ;     /* offset to start of base class */
  dnttpointer next           ;     /* pointer to next K_INHERITANCE */
  unsigned long future[2]    ;     /* padding to 3-word block end   */
};

/* C++ "friend" classes ... */

struct dntt_type_friend_class
{
  unsigned int extension:   1;	   /* always zero */
  unsigned int kind:       10;     /* always DNTT_TYPE_FRIEND_CLASS */
  unsigned int unused:	   21;
  dnttpointer classptr       ;     /* pointer to class DNTT         */
  dnttpointer next           ;     /* next DNTT_FRIEND              */
};

struct dntt_type_friend_func
{
  unsigned int extension:   1;	   /* always zero */
  unsigned int kind:       10;     /* always DNTT_TYPE_FRIEND_FUNC */
  unsigned int unused:	   21;
  dnttpointer funcptr        ;     /* pointer to function           */
  dnttpointer classptr       ;     /* pointer to class DNTT         */
  dnttpointer next           ;     /* next DNTT_FRIEND              */
  unsigned long future[2]    ;     /* padding to 3-word block end   */
};

/* DDE appears to ignore the DNTT_TYPE_MODIFIER record.
   It could perhaps be used to give better "ptype" output in GDB;
   otherwise it is probably safe for GDB to ignore it also.  */

struct dntt_type_modifier
{
  unsigned int extension:   1;	   /* always zero */
  unsigned int kind:       10;     /* always DNTT_TYPE_MODIFIER */
  unsigned int m_const:     1;     /* const                         */
  unsigned int m_static:    1;     /* static                        */
  unsigned int m_void:      1;     /* void                          */
  unsigned int m_volatile:  1;     /* volatile                      */
  unsigned int m_duplicate: 1;     /* duplicate                     */
  unsigned int unused:	   16;
  dnttpointer type           ;     /* subtype                       */
  unsigned long future       ;     /* padding to 3-word block end   */
};

/* I'm not sure what this was intended for - DDE ignores it.  */

struct dntt_type_object_id
{
  unsigned int extension:   1;	   /* always zero */
  unsigned int kind:       10;     /* always DNTT_TYPE_OBJECT_ID */
  unsigned int indirect:    1;     /* Is object_ident addr of addr? */
  unsigned int unused:	   20;
  unsigned long object_ident ;     /* object identifier             */
  unsigned long offset       ;     /* offset to start of base class */
  dnttpointer next           ;     /* pointer to next K_OBJECT_ID   */
  unsigned long segoffset    ;     /* for linker fixup              */
  unsigned long future       ;     /* padding to 3-word block end   */
};

/* No separate dntt_type_memfunc; same as dntt_type_func */

/* Symbol records to support templates. These only get used
   in DDE's "describe" output (like GDB's "ptype").  */

/* The TEMPLATE record is the header for a template-class.
   Like the CLASS record, a TEMPLATE record has a memberlist that
   points to a list of template members. It also has an arglist
   pointing to a list of TEMPLATE_ARG records.  */

struct dntt_type_template
{
  unsigned int extension:   1;	   /* always zero */
  unsigned int kind:       10;     /* always DNTT_TYPE_TEMPLATE */
  unsigned int abstract:    1;     /* is this an abstract class?    */
  unsigned int class_decl:  2;     /* 0=class,1=union,2=struct      */
  unsigned int unused:	   18;
  dnttpointer memberlist     ;     /* ptr to chain of K_[GEN]FIELDs */
  long unused2               ;     /* offset in obj of ptr to vtbl  */
  dnttpointer parentlist     ;     /* ptr to K_INHERITANCE list     */
  unsigned long bitlength    ;     /* total at this level           */
  dnttpointer identlist      ;     /* ptr to chain of class ident's */
  dnttpointer friendlist     ;     /* ptr to K_FRIEND list          */
  dnttpointer arglist        ;     /* ptr to argument list          */
  dnttpointer expansions     ;     /* ptr to expansion list         */
};

/* Template-class arguments are a list of TEMPL_ARG records
   chained together. The "name" field is the name of the formal.
   E.g.:
   
     template <class T> class q { ... };
   
   Then "T" is the name of the formal argument.  */

struct dntt_type_templ_arg
{
  unsigned int extension:   1;	   /* always zero */
  unsigned int kind:       10;     /* always DNTT_TYPE_TEMPL_ARG */
  unsigned int usagetype:   1;     /* 0 type-name 1 expression     */
  unsigned int unused:	   20;
  vtpointer name             ;     /* name of argument             */
  dnttpointer type           ;     /* for non type arguments       */
  dnttpointer nextarg        ;     /* Next argument if any         */
  long future[2]             ;     /* padding to 3-word block end  */
};

/* FUNC_TEMPLATE records are sort of like FUNCTION, but are emitted
   for template member functions. E.g.,
   
     template <class T> class q
     {
        ...
        void f();
        ... 
     };
   
   Within the list of FIELDs/GENFIELDs defining the member list
   of the template "q", "f" would appear as a FUNC_TEMPLATE.
   We'll also see instances of FUNCTION "f" records for each 
   instantiation of the template.  */

struct dntt_type_func_template
{
  unsigned int extension:   1;	   /* always zero */
  unsigned int kind:       10;     /* always DNTT_TYPE_FUNC_TEMPLATE */
  unsigned int public:      1;     /* 1 => globally visible        */
  unsigned int language:    4;     /* type of language             */
  unsigned int level:       5;     /* nesting level (top level = 0)*/
  unsigned int optimize:    2;     /* level of optimization        */
  unsigned int varargs:     1;     /* ellipses.  Pascal/800 later  */ 
  unsigned int info:        4;     /* lang-specific stuff; F_xxxx  */
  unsigned int inlined:     1;
  unsigned int localloc:    1;     /* 0 at top, 1 at end of block  */
  unsigned int unused:      2;
  vtpointer name             ;     /* name of function             */
  vtpointer alias            ;     /* alternate name, if any       */
  dnttpointer firstparam     ;     /* first FPARAM, if any         */
  dnttpointer retval         ;     /* return type, if any          */
  dnttpointer arglist        ;     /* ptr to argument list         */
};

/* LINK is apparently intended to link together function template
   definitions with their instantiations. However, it is not clear
   why this would be needed, except to provide the information on
   a "ptype" command. And as far as I can tell, aCC does not 
   generate this record.  */

struct dntt_type_link
{
  unsigned int extension:   1;	   /* always zero */
  unsigned int kind:       10;     /* always DNTT_TYPE_LINK */
  unsigned int linkKind:    4;     /* always LINK_UNKNOWN          */
  unsigned int unused:	   17;
  long future1               ;     /* expansion                    */
  dnttpointer ptr1           ;     /* link from template           */
  dnttpointer ptr2           ;     /* to expansion                 */
  long future[2]             ;     /* padding to 3-word block end  */
};

/* end of C++ specific SOM's.  */

/* DNTT_TYPE_DYN_ARRAY_DESC is unused by GDB */
/* DNTT_TYPE_DESC_SUBRANGE is unused by GDB */
/* DNTT_TYPE_BEGIN_EXT is unused by GDB */
/* DNTT_TYPE_INLN is unused by GDB */
/* DNTT_TYPE_INLN_LIST is unused by GDB */
/* DNTT_TYPE_ALIAS is unused by GDB */

struct dntt_type_doc_function
{
  unsigned int extension: 1;   /* always zero                  */
  unsigned int kind:     10;   /* K_DOC_FUNCTION or            */
                               /* K_DOC_MEMFUNC                */
  unsigned int global:    1;   /* 1 => globally visible        */
  unsigned int language:  4;   /* type of language             */
  unsigned int level:     5;   /* nesting level (top level = 0)*/
  unsigned int optimize:  2;   /* level of optimization        */
  unsigned int varargs:   1;   /* ellipses.  Pascal/800 later  */
  unsigned int info:      4;   /* lang-specific stuff; F_xxxx  */
  unsigned int inlined:   1;
  unsigned int localloc:  1;   /* 0 at top, 1 at end of block  */
  unsigned int expansion: 1;   /* 1 = function expansion       */
  unsigned int doc_clone: 1;
  vtpointer name;              /* name of function             */
  vtpointer alias;             /* alternate name, if any       */
  dnttpointer firstparam;      /* first FPARAM, if any         */
  sltpointer address;          /* code and text locations      */
  CORE_ADDR entryaddr;         /* address of entry point       */
  dnttpointer retval;          /* return type, if any          */
  CORE_ADDR lowaddr;           /* lowest address of function   */
  CORE_ADDR hiaddr;            /* highest address of function  */
  dnttpointer inline_list;     /* pointer to first inline    */
  ltpointer lt_offset;         /* start of frag/cp line table  */
  ctxtpointer ctxt_offset;     /* start of context table for this routine */
};

/* DNTT_TYPE_DOC_MEMFUNC is unused by GDB */

/* DNTT_TYPE_GENERIC and DNTT_TYPE_BLOCK are convience structures
   so we can examine a DNTT entry in a generic fashion.  */
struct dntt_type_generic
{
  unsigned int word[9];
};

struct dntt_type_block
{
  unsigned int extension:	1;
  unsigned int kind:            10;
  unsigned int unused:		21;
  unsigned int word[2];
};

/* One entry in a DNTT (either the LNTT or GNTT).  
   This is a union of the above 60 or so structure definitions.  */

union dnttentry
{
  struct dntt_type_srcfile dsfile;
  struct dntt_type_module dmodule;
  struct dntt_type_function dfunc;
  struct dntt_type_function dentry;
  struct dntt_type_begin dbegin;
  struct dntt_type_end dend;
  struct dntt_type_fparam dfparam;
  struct dntt_type_svar dsvar;
  struct dntt_type_dvar ddvar;
  struct dntt_type_const dconst;
  struct dntt_type_type dtype;
  struct dntt_type_type dtag;
  struct dntt_type_pointer dptr;
  struct dntt_type_enum denum;
  struct dntt_type_memenum dmember;
  struct dntt_type_set dset;
  struct dntt_type_subrange dsubr;
  struct dntt_type_array darray;
  struct dntt_type_struct dstruct;
  struct dntt_type_union dunion;
  struct dntt_type_field dfield;
  struct dntt_type_functype dfunctype;
  struct dntt_type_with dwith;
  struct dntt_type_function dblockdata;
  struct dntt_type_class_scope dclass_scope;
  struct dntt_type_pointer dreference;
  struct dntt_type_ptrmem dptrmem;
  struct dntt_type_ptrmemfunc dptrmemfunc;
  struct dntt_type_class dclass;
  struct dntt_type_genfield dgenfield;
  struct dntt_type_vfunc dvfunc;
  struct dntt_type_memaccess dmemaccess;
  struct dntt_type_inheritance dinheritance;
  struct dntt_type_friend_class dfriend_class;
  struct dntt_type_friend_func dfriend_func;
  struct dntt_type_modifier dmodifier;
  struct dntt_type_object_id dobject_id;
  struct dntt_type_template dtemplate;
  struct dntt_type_templ_arg dtempl_arg;
  struct dntt_type_func_template dfunc_template;
  struct dntt_type_link dlink;
  struct dntt_type_doc_function ddocfunc;
  struct dntt_type_generic dgeneric;
  struct dntt_type_block dblock;
};

/* Source line entry types.  */
enum slttype
{
  SLT_NORMAL,
  SLT_SRCFILE,
  SLT_MODULE,
  SLT_FUNCTION,
  SLT_ENTRY,
  SLT_BEGIN,
  SLT_END,
  SLT_WITH,
  SLT_EXIT,
  SLT_ASSIST,
  SLT_MARKER,
  SLT_CLASS_SCOPE,
  SLT_INLN,
  SLT_NORMAL_OFFSET,
};

/* A normal source line entry.  Simply provides a mapping of a source
   line number to a code address.

   SLTDESC will always be SLT_NORMAL or SLT_EXIT.  */

struct slt_normal
{
  unsigned int sltdesc:	4;
  unsigned int line:	28;
  CORE_ADDR address;
};

struct slt_normal_off
{
  unsigned int sltdesc:	4;
  unsigned int offset:	6;
  unsigned int line:	22;
  CORE_ADDR address;
};

/* A special source line entry.  Provides a mapping of a declaration
   to a line number.  These entries point back into the DNTT which
   references them.  */

struct slt_special
{
  unsigned int sltdesc:	4;
  unsigned int line:	28;
  dnttpointer backptr;
};

/* Used to describe nesting.

   For nested languages, an slt_assist entry must follow each SLT_FUNC
   entry in the SLT.  The address field will point forward to the
   first slt_normal entry within the function's scope.  */

struct slt_assist
{
  unsigned int sltdesc:	4;
  unsigned int unused:	28;
  sltpointer address;
};

struct slt_generic
{
  unsigned int word[2];
};

union sltentry
{
  struct slt_normal snorm;
  struct slt_normal_off snormoff;
  struct slt_special sspec;
  struct slt_assist sasst;
  struct slt_generic sgeneric;
};

/* $LINES$ declarations
   This is the line table used for optimized code, which is only present 
   in the new $PROGRAM_INFO$ debug space.  */

#define DST_LN_ESCAPE_FLAG1   15
#define DST_LN_ESCAPE_FLAG2   14
#define DST_LN_CTX_SPEC1      13  
#define DST_LN_CTX_SPEC2      12

/* Escape function codes:  */

typedef enum
{
  dst_ln_pad,          /* pad byte */
  dst_ln_escape_1,     /* reserved */
  dst_ln_dpc1_dln1,    /* 1 byte line delta, 1 byte pc delta */
  dst_ln_dpc2_dln2,    /* 2 bytes line delta, 2 bytes pc delta */
  dst_ln_pc4_ln4,      /* 4 bytes ABSOLUTE line number, 4 bytes ABSOLUTE pc */
  dst_ln_dpc0_dln1,    /* 1 byte line delta, pc delta = 0 */
  dst_ln_ln_off_1,     /* statement escape, stmt # = 1 (2nd stmt on line) */
  dst_ln_ln_off,       /* statement escape, stmt # = next byte */
  dst_ln_entry,        /* entry escape, next byte is entry number */
  dst_ln_exit,         /* exit escape */
  dst_ln_stmt_end,     /* gap escape, 4 bytes pc delta */
  dst_ln_stmt_cp,      /* current stmt is a critical point */
  dst_ln_escape_12,    /* reserved */
  dst_ln_escape_13,    /* this is an exception site record */
  dst_ln_nxt_byte,     /* next byte contains the real escape code */
  dst_ln_end,          /* end escape, final entry follows */
  dst_ln_escape1_END_OF_ENUM
}
dst_ln_escape1_t;

typedef enum
{
  dst_ln_ctx_1,        	/* next byte describes context switch with 5-bit */
  			/* index into the image table and 3-bit run length. */
			/* If run length is 0, end with another cxt specifier or ctx_end */
  dst_ln_ctx_2,        	/* next 2 bytes switch context: 13 bit index, 3 bit run length */
  dst_ln_ctx_4,        	/* next 4 bytes switch context: 29 bit index, 3 bit run length */
  dst_ln_ctx_end,      	/* end current context */
  dst_ln_col_run_1,    	/* next byte is column position of start of next statement, */
                        /* following byte is length of statement */
  dst_ln_col_run_2,    	/* next 2 bytes is column position of start of next statement, */
                        /* following 2 bytes is length of statement */
  dst_ln_init_base1,   	/* next 4 bytes are absolute PC, followed by 1 byte of line number */
  dst_ln_init_base2,   	/* next 4 bytes are absolute PC, followed by 2 bytes of line number */
  dst_ln_init_base3,   	/* next 4 bytes are absolute PC, followed by 3 bytes of line number */
  dst_ln_escape2_END_OF_ENUM
}
dst_ln_escape2_t;           

typedef union
{
  struct
  {
    unsigned int     pc_delta : 4;      /* 4 bit pc delta */
    int              ln_delta : 4;      /* 4 bit line number delta */
  }
  delta;

  struct
  {
    unsigned int     esc_flag : 4;      /* alias for pc_delta  */
    unsigned int     esc_code : 4;      /* escape function code (dst_ln_escape1_t, or ...2_t */
  }
  esc;

  struct
  {
    unsigned int     esc_flag   : 4;      /* dst_ln_ctx_spec1, or dst_ln_ctx_spec2 */
    unsigned int     run_length : 2;      
    unsigned int     ctx_index  : 2;      /* ...spec2 contains index;  ...spec1, index - 4 */
  }
  ctx_spec;

  char               sdata;               /* signed data byte */
  unsigned char      udata;               /* unsigned data byte */
}
dst_ln_entry_t,
  * dst_ln_entry_ptr_t;

/* Warning: although the above union occupies only 1 byte the compiler treats
   it as having size 2 (the minimum size of a struct).  Therefore a sequence of
   dst_ln_entry_t's cannot be described as an array, and walking through such a
   sequence requires convoluted code such as
        ln_ptr = (dst_ln_entry_ptr_t) (char*) ln_ptr + 1
   We regret the inconvenience.  */

/* Structure for interpreting the byte following a dst_ln_ctx1 entry.  */
typedef struct
{
    unsigned int          ctx1_index : 5;      /* 5 bit index into context table */
    unsigned int          ctx1_run_length : 3; /* 3 bit run length */
} dst_ln_ctx1_t,
  *dst_ln_ctx1_ptr_t;

/* Structure for interpreting the bytes following a dst_ln_ctx2 entry.  */
typedef struct
{
    unsigned int          ctx2_index : 13;     /* 13 bit index into context table */
    unsigned int          ctx2_run_length : 3; /* 3 bit run length */
} dst_ln_ctx2_t,
  *dst_ln_ctx2_ptr_t;

/* Structure for interpreting the bytes following a dst_ln_ctx4 entry.  */
typedef struct
{
    unsigned int          ctx4_index : 29;     /* 29 bit index into context table */
    unsigned int          ctx4_run_length : 3; /* 3 bit run length */
} dst_ln_ctx4_t,
  *dst_ln_ctx4_ptr_t;


/*  PXDB definitions.
  
   PXDB is a post-processor which takes the executable file
   and massages the debug information so that the debugger may
   start up and run more efficiently.  Some of the tasks
   performed by PXDB are:
  
   o   Remove duplicate global type and variable information
       from the GNTT,
  
   o   Append the GNTT onto the end of the LNTT and place both
       back in the LNTT section,
  
   o   Build quick look-up tables (description follows) for
       files, procedures, modules, and paragraphs (for Cobol),
       placing these in the GNTT section,
  
   o   Reconstruct the header appearing in the header section
       to access this information.
  
   The "quick look-up" tables are in the $GNTT$ sub-space, in
   the following order:
  
       Procedures    -sorted by address
       Source files  -sorted by address (of the
                      generated code from routines)
       Modules       -sorted by address
       Classes       -<unsorted?>
       Address Alias -sorted by index <?>
       Object IDs    -sorted by object identifier
  
   Most quick entries have (0-based) indices into the LNTT tables to
   the full entries for the item it describes.
  
   The post-PXDB header is in the $HEADER$ sub-space.  Alas, it
   occurs in different forms, depending on the optimization level
   in the compilation step and whether PXDB was run or not. The
   worst part is the forms aren't self-describing, so we'll have
   to grovel in the bits to figure out what kind we're looking at
   (see hp_get_header in hp-psymtab-read.c).  */

/* PXDB versions.  */

#define PXDB_VERSION_CPLUSPLUS	1
#define PXDB_VERSION_7_4	2
#define PXDB_VERSION_CPP_30	3
#define PXDB_VERSION_DDE_3_2A	4
#define PXDB_VERSION_DDE_3_2	5
#define PXDB_VERSION_DDE_4_0	6

#define PXDB_VERSION_2_1	1

/* Header version for the case that there is no DOC info
   but the executable has been processed by pxdb (the easy
   case, from "cc -g").  */

typedef struct PXDB_struct
{
  int              pd_entries;   /* # of entries in function look-up table */
  int              fd_entries;   /* # of entries in file look-up table */
  int              md_entries;   /* # of entries in module look-up table */
  unsigned int     pxdbed : 1;   /* 1 => file has been preprocessed      */
  unsigned int     bighdr : 1;   /* 1 => this header contains 'time' word */
  unsigned int     sa_header : 1;/* 1 => created by SA version of pxdb */
			           /*   used for version check in xdb */
  unsigned int     inlined: 1;   /* one or more functions have been inlined */
  unsigned int     spare:12;
  short            version;      /* pxdb header version */
  int              globals;      /* index into the DNTT where GNTT begins */
  unsigned int     time;         /* modify time of file before being pxdbed */
  int              pg_entries;   /* # of entries in label look-up table */
  int              functions;    /* actual number of functions */
  int              files;        /* actual number of files */
  int              cd_entries;   /* # of entries in class look-up table */
  int              aa_entries;   /* # of entries in addr alias look-up table */
  int              oi_entries;   /* # of entries in object id look-up table */
} PXDB_header, *PXDB_header_ptr;

/* Header version for the case that there is no DOC info and the
   executable has NOT been processed by pxdb.  */

typedef struct XDB_header_struct
{
  long gntt_length; 
  long lntt_length; 
  long slt_length; 
  long vt_length; 
  long xt_length; 
} XDB_header;

/* Header version for the case that there is DOC info and the
   executable has been processed by pxdb. */

typedef struct DOC_info_PXDB_header_struct
{
  unsigned int xdb_header: 1; 	      /* bit set if this is post-3.1 xdb */ 
  unsigned int doc_header: 1;         /* bit set if this is doc-style header */
  unsigned int version: 8;            /* version of pxdb see defines
				         PXDB_VERSION_* in this file.  */
  unsigned int reserved_for_flags: 16;/* for future use; -- must be 
                                         set to zero.  */
  unsigned int has_aux_pd_table: 1;   /* $GNTT$ has aux PD table */
  unsigned int has_expr_table: 1;     /* space has $EXPR$ */       
  unsigned int has_range_table: 1;    /* space has $RANGE$ */       
  unsigned int has_context_table: 1;  /* space has $SRC_CTXT$ */    
  unsigned int has_lines_table: 1;    /* space contains a $LINES$
                                         subspace for line tables.  */
  unsigned int has_lt_offset_map: 1;  /* space contains an lt_offset
                                         subspace for line table mapping.  */
  /* The following fields are the same as those in the PXDB_header in $DEBUG$ */
  int           pd_entries;   /* # of entries in function look-up table */
  int           fd_entries;   /* # of entries in file look-up table */
  int           md_entries;   /* # of entries in module look-up table */
  unsigned int  pxdbed : 1;   /* 1 => file has been preprocessed      */
  unsigned int  bighdr : 1;   /* 1 => this header contains 'time' word */
  unsigned int  sa_header : 1;/* 1 => created by SA version of pxdb */
                              /*   used for version check in xdb */
  unsigned int  inlined: 1;   /* one or more functions have been inlined */
  unsigned int  spare : 28;
  int      	globals;      /* index into the DNTT where GNTT begins */
  unsigned int  time;         /* modify time of file before being pxdbed */
  int           pg_entries;   /* # of entries in label look-up table */
  int           functions;    /* actual number of functions */
  int           files;        /* actual number of files */
  int           cd_entries;   /* # of entries in class look-up table */
  int           aa_entries;   /* # of entries in addr alias look-up table */
  int           oi_entries;   /* # of entries in object id look-up table */
} DOC_info_PXDB_header;

/* Header version for the case that there is DOC info and the
   executable has NOT been processed by pxdb.  */

typedef struct DOC_info_header_struct
{
  unsigned int xdb_header: 1; 	/* bit set if this is post-3.1 xdb */ 
  unsigned int doc_header: 1;     /* bit set if this is doc-style header*/
  unsigned int version: 8;      /* version of debug/header 
                                   format. For 10.0 the value 
                                   will be 1. For "Davis" the value is 2.  */
  unsigned int reserved_for_flags: 18; /* for future use; -- must be set to zero.  */
  unsigned int has_range_table: 1;     /* space contains a $RANGE$ subspace for variable ranges.  */
  unsigned int has_context_table: 1;   /* space contains a $CTXT$ subspace for context/inline table.  */
  unsigned int has_lines_table: 1;     /* space contains a $LINES$ subspace for line tables. */
  unsigned int has_lt_offset_map: 1;   /* space contains an lt_offset subspace for line table mapping.  */

  long   gntt_length;  /* same as old header */
  long   lntt_length;  /* same as old header */
  long   slt_length;   /* same as old header */
  long   vt_length;    /* same as old header */
  long   xt_length;    /* same as old header */
  long   ctxt_length;  /* present only if version >= 2 */
  long   range_length; /* present only if version >= 2 */
  long   expr_length;  /* present only if version >= 2 */

} DOC_info_header;

typedef union GenericDebugHeader_union
{
   PXDB_header          no_doc;
   DOC_info_PXDB_header doc;
   XDB_header           no_pxdb_no_doc;
   DOC_info_header      no_pxdb_doc;
} GenericDebugHeader;


/*  Procedure Descriptor:
    An element of the procedure quick look-up table.  */

typedef struct quick_procedure
{
  long           isym;		/* 0-based index of first symbol
                                   for procedure in $LNTT$, 
                                   i.e. the procedure itself.  */
  CORE_ADDR	 adrStart;	/* memory adr of start of proc	*/
  CORE_ADDR	 adrEnd;	/* memory adr of end of proc	*/
  char         	*sbAlias;	/* alias name of procedure	*/
  char          *sbProc;	/* real name of procedure	*/
  CORE_ADDR	 adrBp;		/* address of entry breakpoint  */
  CORE_ADDR	 adrExitBp;	/* address of exit breakpoint   */
  int            icd;           /* member of this class (index) */	
  unsigned int	 ipd;		/* index of template for this   */
                                /* function (index)           */
  unsigned int	 unused:    5;
  unsigned int	 no_lt_offset: 1;/* no entry in lt_offset table */
  unsigned int	 fTemplate: 1;	/* function template		*/
  unsigned int	 fExpansion: 1;	/* function expansion		*/
  unsigned int	 linked	  : 1;	/* linked with other expansions	*/
  unsigned int	 duplicate: 1;  /* clone of another procedure   */
  unsigned int	 overloaded:1;  /* overloaded function          */
  unsigned int	 member:    1;  /* class member function        */
  unsigned int	 constructor:1; /* constructor function         */
  unsigned int	 destructor:1;  /* destructor function          */
  unsigned int   Static:    1;  /* static function              */
  unsigned int   Virtual:   1;  /* virtual function             */
  unsigned int   constant:  1;  /* constant function            */
  unsigned int   pure:      1;  /* pure (virtual) function      */
  unsigned int   language:  4;  /* procedure's language         */
  unsigned int   inlined:   1;  /* function has been inlined    */
  unsigned int   Operator:  1;  /* operator function            */
  unsigned int	 stub:      1;  /* bodyless function            */
  unsigned int	 optimize:  2;	/* optimization level   	*/
  unsigned int	 level:     5;	/* nesting level (top=0)	*/
} quick_procedure_entry, *quick_procedure_entry_ptr;

/*  Source File Descriptor:
    An element of the source file quick look-up table.  */

typedef struct quick_source
{
  long	         isym;		/* 0-based index in $LNTT$ of
                                   first symbol for this file.     */
  CORE_ADDR      adrStart;	/* mem adr of start of file's code */
  CORE_ADDR      adrEnd;	/* mem adr of end of file's code   */
  char	        *sbFile;	/* name of source file		   */
  unsigned int   fHasDecl: 1;	/* do we have a .d file?	   */
  unsigned int   fWarned:  1;	/* have warned about age problems? */
  unsigned int   fSrcfile: 1;   /* 0 => include 1=> source         */
  unsigned short ilnMac;	/* lines in file (0 if don't know) */
  int	         ipd;		/* 0-based index of first procedure
                                   in this file, in the quick
                                   look-up table of procedures.    */
  unsigned int  *rgLn;		/* line pointer array, if any	   */
} quick_file_entry, *quick_file_entry_ptr;

/*  Module Descriptor:
    An element of the module quick reference table.  */

typedef struct quick_module
{
  long           isym;		   /* 0-based index of first
                                      symbol for module.        */
  CORE_ADDR	 adrStart;	   /* adr of start of mod.	*/
  CORE_ADDR	 adrEnd;	   /* adr of end of mod.	*/
  char	        *sbAlias;	   /* alias name of module   	*/
  char	        *sbMod;		   /* real name of module	*/
  unsigned int   imports:       1; /* module have any imports?  */
  unsigned int   vars_in_front: 1; /* module globals in front?  */
  unsigned int   vars_in_gaps:  1; /* module globals in gaps?   */
  unsigned int   language:      4; /* type of language          */
  unsigned int   unused      : 25;
  unsigned int   unused2;	   /* space for future stuff	*/
} quick_module_entry, *quick_module_entry_ptr;

/*  Auxiliary Procedure Descriptor:
    An element of the auxiliary procedure quick look-up table.  */

typedef struct quick_aux_procedure
{
  long	 isym_inln;	/* start on inline list for proc */
  long   spare;
} quick_aux_procedure_entry, *quick_aux_procedure_entry_ptr;

/*  Paragraph Descriptor:
    An element of the paragraph quick look-up table.  */

typedef struct quick_paragraph
{
  long             isym;       /* first symbol for label (index)  */
  CORE_ADDR        adrStart;   /* memory adr of start of label    */
  CORE_ADDR        adrEnd;     /* memory adr of end of label      */
  char            *sbLab;      /* name of label                   */
  unsigned int     inst;       /* Used in xdb to store inst @ bp  */
  unsigned int     sect:    1; /* true = section, false = parag.  */
  unsigned int     unused: 31; /* future use                      */
} quick_paragraph_entry, *quick_paragraph_entry_ptr;

/* Class Descriptor:
   An element of the class quick look-up table.  */

typedef struct quick_class
{
  char	         *sbClass;	/* name of class	        */
  long            isym;         /* class symbol (tag)           */
  unsigned int	  type : 2;	/* 0=class, 1=union, 2=struct   */
  unsigned int	  fTemplate : 1;/* class template               */
  unsigned int	  expansion : 1;/* template expansion           */
  unsigned int	  unused    :28;
  sltpointer      lowscope;	/* beginning of defined scope   */
  sltpointer      hiscope;	/* end of defined scope         */
} quick_class_entry, *quick_class_entry_ptr;

/* Address Alias Entry
   An element of the address alias quick look-up table.  */

typedef struct quick_alias
{
  CORE_ADDR     low;
  CORE_ADDR     high;
  int           index;
  unsigned int	unused : 31;
  unsigned int	alternate : 1;	/* alternate unnamed aliases?   */
} quick_alias_entry, *quick_alias_entry_ptr;

/* Object Identification Entry
   An element of the object identification quick look-up table.  */

typedef struct quick_obj_ID
{
  CORE_ADDR    obj_ident;	/* class identifier         */
  long         isym;		/* class symbol             */
  long         offset;		/* offset to object start   */
} quick_obj_ID_entry, *quick_obj_ID_entry_ptr;

#endif /* HP_SYMTAB_INCLUDED */
