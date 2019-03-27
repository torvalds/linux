/* ECOFF debugging support.
   Copyright 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002,
   2003, 2004, 2005, 2006
   Free Software Foundation, Inc.
   Contributed by Cygnus Support.
   This file was put together by Ian Lance Taylor <ian@cygnus.com>.  A
   good deal of it comes directly from mips-tfile.c, by Michael
   Meissner <meissner@osf.org>.

   This file is part of GAS.

   GAS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GAS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GAS; see the file COPYING.  If not, write to the Free
   Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#include "as.h"

/* This file is compiled conditionally for those targets which use
   ECOFF debugging information (e.g., MIPS ECOFF, MIPS ELF, Alpha
   ECOFF).  */

#include "ecoff.h"

#ifdef ECOFF_DEBUGGING

#include "coff/internal.h"
#include "coff/symconst.h"
#include "aout/stab_gnu.h"

#include "safe-ctype.h"

/* Why isn't this in coff/sym.h?  */
#define ST_RFDESCAPE 0xfff

/* This file constructs the information used by the ECOFF debugging
   format.  It just builds a large block of data.

   We support both ECOFF style debugging and stabs debugging (the
   stabs symbols are encapsulated in ECOFF symbols).  This should let
   us handle anything the compiler might throw at us.  */

/* Here is a brief description of the MIPS ECOFF symbol table, by
   Michael Meissner.  The MIPS symbol table has the following pieces:

	Symbolic Header
	    |
	    +--	Auxiliary Symbols
	    |
	    +--	Dense number table
	    |
	    +--	Optimizer Symbols
	    |
	    +--	External Strings
	    |
	    +--	External Symbols
	    |
	    +--	Relative file descriptors
	    |
	    +--	File table
		    |
		    +--	Procedure table
		    |
		    +--	Line number table
		    |
		    +--	Local Strings
		    |
		    +--	Local Symbols

   The symbolic header points to each of the other tables, and also
   contains the number of entries.  It also contains a magic number
   and MIPS compiler version number, such as 2.0.

   The auxiliary table is a series of 32 bit integers, that are
   referenced as needed from the local symbol table.  Unlike standard
   COFF, the aux.  information does not follow the symbol that uses
   it, but rather is a separate table.  In theory, this would allow
   the MIPS compilers to collapse duplicate aux. entries, but I've not
   noticed this happening with the 1.31 compiler suite.  The different
   types of aux. entries are:

    1)	dnLow: Low bound on array dimension.

    2)	dnHigh: High bound on array dimension.

    3)	isym: Index to the local symbol which is the start of the
	function for the end of function first aux. entry.

    4)	width: Width of structures and bitfields.

    5)	count: Count of ranges for variant part.

    6)	rndx: A relative index into the symbol table.  The relative
	index field has two parts: rfd which is a pointer into the
	relative file index table or ST_RFDESCAPE which says the next
	aux. entry is the file number, and index: which is the pointer
	into the local symbol within a given file table.  This is for
	things like references to types defined in another file.

    7)	Type information: This is like the COFF type bits, except it
	is 32 bits instead of 16; they still have room to add new
	basic types; and they can handle more than 6 levels of array,
	pointer, function, etc.  Each type information field contains
	the following structure members:

	    a)	fBitfield: a bit that says this is a bitfield, and the
		size in bits follows as the next aux. entry.

	    b)	continued: a bit that says the next aux. entry is a
		continuation of the current type information (in case
		there are more than 6 levels of array/ptr/function).

	    c)	bt: an integer containing the base type before adding
		array, pointer, function, etc. qualifiers.  The
		current base types that I have documentation for are:

			btNil		-- undefined
			btAdr		-- address - integer same size as ptr
			btChar		-- character
			btUChar		-- unsigned character
			btShort		-- short
			btUShort	-- unsigned short
			btInt		-- int
			btUInt		-- unsigned int
			btLong		-- long
			btULong		-- unsigned long
			btFloat		-- float (real)
			btDouble	-- Double (real)
			btStruct	-- Structure (Record)
			btUnion		-- Union (variant)
			btEnum		-- Enumerated
			btTypedef	-- defined via a typedef isymRef
			btRange		-- subrange of int
			btSet		-- pascal sets
			btComplex	-- fortran complex
			btDComplex	-- fortran double complex
			btIndirect	-- forward or unnamed typedef
			btFixedDec	-- Fixed Decimal
			btFloatDec	-- Float Decimal
			btString	-- Varying Length Character String
			btBit		-- Aligned Bit String
			btPicture	-- Picture
			btVoid		-- Void (MIPS cc revision >= 2.00)

	    d)	tq0 - tq5: type qualifier fields as needed.  The
		current type qualifier fields I have documentation for
		are:

			tqNil		-- no more qualifiers
			tqPtr		-- pointer
			tqProc		-- procedure
			tqArray		-- array
			tqFar		-- 8086 far pointers
			tqVol		-- volatile

   The dense number table is used in the front ends, and disappears by
   the time the .o is created.

   With the 1.31 compiler suite, the optimization symbols don't seem
   to be used as far as I can tell.

   The linker is the first entity that creates the relative file
   descriptor table, and I believe it is used so that the individual
   file table pointers don't have to be rewritten when the objects are
   merged together into the program file.

   Unlike COFF, the basic symbol & string tables are split into
   external and local symbols/strings.  The relocation information
   only goes off of the external symbol table, and the debug
   information only goes off of the internal symbol table.  The
   external symbols can have links to an appropriate file index and
   symbol within the file to give it the appropriate type information.
   Because of this, the external symbols are actually larger than the
   internal symbols (to contain the link information), and contain the
   local symbol structure as a member, though this member is not the
   first member of the external symbol structure (!).  I suspect this
   split is to make strip easier to deal with.

   Each file table has offsets for where the line numbers, local
   strings, local symbols, and procedure table starts from within the
   global tables, and the indexs are reset to 0 for each of those
   tables for the file.

   The procedure table contains the binary equivalents of the .ent
   (start of the function address), .frame (what register is the
   virtual frame pointer, constant offset from the register to obtain
   the VFP, and what register holds the return address), .mask/.fmask
   (bitmask of saved registers, and where the first register is stored
   relative to the VFP) assembler directives.  It also contains the
   low and high bounds of the line numbers if debugging is turned on.

   The line number table is a compressed form of the normal COFF line
   table.  Each line number entry is either 1 or 3 bytes long, and
   contains a signed delta from the previous line, and an unsigned
   count of the number of instructions this statement takes.

   The local symbol table contains the following fields:

    1)	iss: index to the local string table giving the name of the
	symbol.

    2)	value: value of the symbol (address, register number, etc.).

    3)	st: symbol type.  The current symbol types are:

	    stNil	  -- Nuthin' special
	    stGlobal	  -- external symbol
	    stStatic	  -- static
	    stParam	  -- procedure argument
	    stLocal	  -- local variable
	    stLabel	  -- label
	    stProc	  -- External Procedure
	    stBlock	  -- beginning of block
	    stEnd	  -- end (of anything)
	    stMember	  -- member (of anything)
	    stTypedef	  -- type definition
	    stFile	  -- file name
	    stRegReloc	  -- register relocation
	    stForward	  -- forwarding address
	    stStaticProc  -- Static procedure
	    stConstant	  -- const

    4)	sc: storage class.  The current storage classes are:

	    scText	  -- text symbol
	    scData	  -- initialized data symbol
	    scBss	  -- un-initialized data symbol
	    scRegister	  -- value of symbol is register number
	    scAbs	  -- value of symbol is absolute
	    scUndefined   -- who knows?
	    scCdbLocal	  -- variable's value is IN se->va.??
	    scBits	  -- this is a bit field
	    scCdbSystem	  -- value is IN debugger's address space
	    scRegImage	  -- register value saved on stack
	    scInfo	  -- symbol contains debugger information
	    scUserStruct  -- addr in struct user for current process
	    scSData	  -- load time only small data
	    scSBss	  -- load time only small common
	    scRData	  -- load time only read only data
	    scVar	  -- Var parameter (fortranpascal)
	    scCommon	  -- common variable
	    scSCommon	  -- small common
	    scVarRegister -- Var parameter in a register
	    scVariant	  -- Variant record
	    scSUndefined  -- small undefined(external) data
	    scInit	  -- .init section symbol

    5)	index: pointer to a local symbol or aux. entry.

   For the following program:

	#include <stdio.h>

	main(){
		printf("Hello World!\n");
		return 0;
	}

   Mips-tdump produces the following information:

   Global file header:
       magic number             0x162
       # sections               2
       timestamp                645311799, Wed Jun 13 17:16:39 1990
       symbolic header offset   284
       symbolic header size     96
       optional header          56
       flags                    0x0

   Symbolic header, magic number = 0x7009, vstamp = 1.31:

       Info                      Offset      Number       Bytes
       ====                      ======      ======      =====

       Line numbers                 380           4           4 [13]
       Dense numbers                  0           0           0
       Procedures Tables            384           1          52
       Local Symbols                436          16         192
       Optimization Symbols           0           0           0
       Auxiliary Symbols            628          39         156
       Local Strings                784          80          80
       External Strings             864         144         144
       File Tables                 1008           2         144
       Relative Files                 0           0           0
       External Symbols            1152          20         320

   File #0, "hello2.c"

       Name index  = 1          Readin      = No
       Merge       = No         Endian      = LITTLE
       Debug level = G2         Language    = C
       Adr         = 0x00000000

       Info                       Start      Number        Size      Offset
       ====                       =====      ======        ====      ======
       Local strings                  0          15          15         784
       Local symbols                  0           6          72         436
       Line numbers                   0          13          13         380
       Optimization symbols           0           0           0           0
       Procedures                     0           1          52         384
       Auxiliary symbols              0          14          56         628
       Relative Files                 0           0           0           0

    There are 6 local symbols, starting at 436

	Symbol# 0: "hello2.c"
	    End+1 symbol  = 6
	    String index  = 1
	    Storage class = Text        Index  = 6
	    Symbol type   = File        Value  = 0

	Symbol# 1: "main"
	    End+1 symbol  = 5
	    Type          = int
	    String index  = 10
	    Storage class = Text        Index  = 12
	    Symbol type   = Proc        Value  = 0

	Symbol# 2: ""
	    End+1 symbol  = 4
	    String index  = 0
	    Storage class = Text        Index  = 4
	    Symbol type   = Block       Value  = 8

	Symbol# 3: ""
	    First symbol  = 2
	    String index  = 0
	    Storage class = Text        Index  = 2
	    Symbol type   = End         Value  = 28

	Symbol# 4: "main"
	    First symbol  = 1
	    String index  = 10
	    Storage class = Text        Index  = 1
	    Symbol type   = End         Value  = 52

	Symbol# 5: "hello2.c"
	    First symbol  = 0
	    String index  = 1
	    Storage class = Text        Index  = 0
	    Symbol type   = End         Value  = 0

    There are 14 auxiliary table entries, starting at 628.

	* #0               0, [   0/      0], [ 0 0:0 0:0:0:0:0:0]
	* #1              24, [  24/      0], [ 6 0:0 0:0:0:0:0:0]
	* #2               8, [   8/      0], [ 2 0:0 0:0:0:0:0:0]
	* #3              16, [  16/      0], [ 4 0:0 0:0:0:0:0:0]
	* #4              24, [  24/      0], [ 6 0:0 0:0:0:0:0:0]
	* #5              32, [  32/      0], [ 8 0:0 0:0:0:0:0:0]
	* #6              40, [  40/      0], [10 0:0 0:0:0:0:0:0]
	* #7              44, [  44/      0], [11 0:0 0:0:0:0:0:0]
	* #8              12, [  12/      0], [ 3 0:0 0:0:0:0:0:0]
	* #9              20, [  20/      0], [ 5 0:0 0:0:0:0:0:0]
	* #10             28, [  28/      0], [ 7 0:0 0:0:0:0:0:0]
	* #11             36, [  36/      0], [ 9 0:0 0:0:0:0:0:0]
	  #12              5, [   5/      0], [ 1 1:0 0:0:0:0:0:0]
	  #13             24, [  24/      0], [ 6 0:0 0:0:0:0:0:0]

    There are 1 procedure descriptor entries, starting at 0.

	Procedure descriptor 0:
	    Name index   = 10          Name          = "main"
	    .mask 0x80000000,-4        .fmask 0x00000000,0
	    .frame $29,24,$31
	    Opt. start   = -1          Symbols start = 1
	    First line # = 3           Last line #   = 6
	    Line Offset  = 0           Address       = 0x00000000

	There are 4 bytes holding line numbers, starting at 380.
	    Line           3,   delta     0,   count  2
	    Line           4,   delta     1,   count  3
	    Line           5,   delta     1,   count  2
	    Line           6,   delta     1,   count  6

   File #1, "/usr/include/stdio.h"

    Name index  = 1          Readin      = No
    Merge       = Yes        Endian      = LITTLE
    Debug level = G2         Language    = C
    Adr         = 0x00000000

    Info                       Start      Number        Size      Offset
    ====                       =====      ======        ====      ======
    Local strings                 15          65          65         799
    Local symbols                  6          10         120         508
    Line numbers                   0           0           0         380
    Optimization symbols           0           0           0           0
    Procedures                     1           0           0         436
    Auxiliary symbols             14          25         100         684
    Relative Files                 0           0           0           0

    There are 10 local symbols, starting at 442

	Symbol# 0: "/usr/include/stdio.h"
	    End+1 symbol  = 10
	    String index  = 1
	    Storage class = Text        Index  = 10
	    Symbol type   = File        Value  = 0

	Symbol# 1: "_iobuf"
	    End+1 symbol  = 9
	    String index  = 22
	    Storage class = Info        Index  = 9
	    Symbol type   = Block       Value  = 20

	Symbol# 2: "_cnt"
	    Type          = int
	    String index  = 29
	    Storage class = Info        Index  = 4
	    Symbol type   = Member      Value  = 0

	Symbol# 3: "_ptr"
	    Type          = ptr to char
	    String index  = 34
	    Storage class = Info        Index  = 15
	    Symbol type   = Member      Value  = 32

	Symbol# 4: "_base"
	    Type          = ptr to char
	    String index  = 39
	    Storage class = Info        Index  = 16
	    Symbol type   = Member      Value  = 64

	Symbol# 5: "_bufsiz"
	    Type          = int
	    String index  = 45
	    Storage class = Info        Index  = 4
	    Symbol type   = Member      Value  = 96

	Symbol# 6: "_flag"
	    Type          = short
	    String index  = 53
	    Storage class = Info        Index  = 3
	    Symbol type   = Member      Value  = 128

	Symbol# 7: "_file"
	    Type          = char
	    String index  = 59
	    Storage class = Info        Index  = 2
	    Symbol type   = Member      Value  = 144

	Symbol# 8: ""
	    First symbol  = 1
	    String index  = 0
	    Storage class = Info        Index  = 1
	    Symbol type   = End         Value  = 0

	Symbol# 9: "/usr/include/stdio.h"
	    First symbol  = 0
	    String index  = 1
	    Storage class = Text        Index  = 0
	    Symbol type   = End         Value  = 0

    There are 25 auxiliary table entries, starting at 642.

	* #14             -1, [4095/1048575], [63 1:1 f:f:f:f:f:f]
	  #15          65544, [   8/     16], [ 2 0:0 1:0:0:0:0:0]
	  #16          65544, [   8/     16], [ 2 0:0 1:0:0:0:0:0]
	* #17         196656, [  48/     48], [12 0:0 3:0:0:0:0:0]
	* #18           8191, [4095/      1], [63 1:1 0:0:0:0:f:1]
	* #19              1, [   1/      0], [ 0 1:0 0:0:0:0:0:0]
	* #20          20479, [4095/      4], [63 1:1 0:0:0:0:f:4]
	* #21              1, [   1/      0], [ 0 1:0 0:0:0:0:0:0]
	* #22              0, [   0/      0], [ 0 0:0 0:0:0:0:0:0]
	* #23              2, [   2/      0], [ 0 0:1 0:0:0:0:0:0]
	* #24            160, [ 160/      0], [40 0:0 0:0:0:0:0:0]
	* #25              0, [   0/      0], [ 0 0:0 0:0:0:0:0:0]
	* #26              0, [   0/      0], [ 0 0:0 0:0:0:0:0:0]
	* #27              0, [   0/      0], [ 0 0:0 0:0:0:0:0:0]
	* #28              0, [   0/      0], [ 0 0:0 0:0:0:0:0:0]
	* #29              0, [   0/      0], [ 0 0:0 0:0:0:0:0:0]
	* #30              0, [   0/      0], [ 0 0:0 0:0:0:0:0:0]
	* #31              0, [   0/      0], [ 0 0:0 0:0:0:0:0:0]
	* #32              0, [   0/      0], [ 0 0:0 0:0:0:0:0:0]
	* #33              0, [   0/      0], [ 0 0:0 0:0:0:0:0:0]
	* #34              0, [   0/      0], [ 0 0:0 0:0:0:0:0:0]
	* #35              0, [   0/      0], [ 0 0:0 0:0:0:0:0:0]
	* #36              0, [   0/      0], [ 0 0:0 0:0:0:0:0:0]
	* #37              0, [   0/      0], [ 0 0:0 0:0:0:0:0:0]
	* #38              0, [   0/      0], [ 0 0:0 0:0:0:0:0:0]

    There are 0 procedure descriptor entries, starting at 1.

   There are 20 external symbols, starting at 1152

	Symbol# 0: "_iob"
	    Type          = array [3 {160}] of struct _iobuf { ifd = 1, index = 1 }
	    String index  = 0           Ifd    = 1
	    Storage class = Nil         Index  = 17
	    Symbol type   = Global      Value  = 60

	Symbol# 1: "fopen"
	    String index  = 5           Ifd    = 1
	    Storage class = Nil         Index  = 1048575
	    Symbol type   = Proc        Value  = 0

	Symbol# 2: "fdopen"
	    String index  = 11          Ifd    = 1
	    Storage class = Nil         Index  = 1048575
	    Symbol type   = Proc        Value  = 0

	Symbol# 3: "freopen"
	    String index  = 18          Ifd    = 1
	    Storage class = Nil         Index  = 1048575
	    Symbol type   = Proc        Value  = 0

	Symbol# 4: "popen"
	    String index  = 26          Ifd    = 1
	    Storage class = Nil         Index  = 1048575
	    Symbol type   = Proc        Value  = 0

	Symbol# 5: "tmpfile"
	    String index  = 32          Ifd    = 1
	    Storage class = Nil         Index  = 1048575
	    Symbol type   = Proc        Value  = 0

	Symbol# 6: "ftell"
	    String index  = 40          Ifd    = 1
	    Storage class = Nil         Index  = 1048575
	    Symbol type   = Proc        Value  = 0

	Symbol# 7: "rewind"
	    String index  = 46          Ifd    = 1
	    Storage class = Nil         Index  = 1048575
	    Symbol type   = Proc        Value  = 0

	Symbol# 8: "setbuf"
	    String index  = 53          Ifd    = 1
	    Storage class = Nil         Index  = 1048575
	    Symbol type   = Proc        Value  = 0

	Symbol# 9: "setbuffer"
	    String index  = 60          Ifd    = 1
	    Storage class = Nil         Index  = 1048575
	    Symbol type   = Proc        Value  = 0

	Symbol# 10: "setlinebuf"
	    String index  = 70          Ifd    = 1
	    Storage class = Nil         Index  = 1048575
	    Symbol type   = Proc        Value  = 0

	Symbol# 11: "fgets"
	    String index  = 81          Ifd    = 1
	    Storage class = Nil         Index  = 1048575
	    Symbol type   = Proc        Value  = 0

	Symbol# 12: "gets"
	    String index  = 87          Ifd    = 1
	    Storage class = Nil         Index  = 1048575
	    Symbol type   = Proc        Value  = 0

	Symbol# 13: "ctermid"
	    String index  = 92          Ifd    = 1
	    Storage class = Nil         Index  = 1048575
	    Symbol type   = Proc        Value  = 0

	Symbol# 14: "cuserid"
	    String index  = 100         Ifd    = 1
	    Storage class = Nil         Index  = 1048575
	    Symbol type   = Proc        Value  = 0

	Symbol# 15: "tempnam"
	    String index  = 108         Ifd    = 1
	    Storage class = Nil         Index  = 1048575
	    Symbol type   = Proc        Value  = 0

	Symbol# 16: "tmpnam"
	    String index  = 116         Ifd    = 1
	    Storage class = Nil         Index  = 1048575
	    Symbol type   = Proc        Value  = 0

	Symbol# 17: "sprintf"
	    String index  = 123         Ifd    = 1
	    Storage class = Nil         Index  = 1048575
	    Symbol type   = Proc        Value  = 0

	Symbol# 18: "main"
	    Type          = int
	    String index  = 131         Ifd    = 0
	    Storage class = Text        Index  = 1
	    Symbol type   = Proc        Value  = 0

	Symbol# 19: "printf"
	    String index  = 136         Ifd    = 0
	    Storage class = Undefined   Index  = 1048575
	    Symbol type   = Proc        Value  = 0

   The following auxiliary table entries were unused:

    #0               0  0x00000000  void
    #2               8  0x00000008  char
    #3              16  0x00000010  short
    #4              24  0x00000018  int
    #5              32  0x00000020  long
    #6              40  0x00000028  float
    #7              44  0x0000002c  double
    #8              12  0x0000000c  unsigned char
    #9              20  0x00000014  unsigned short
    #10             28  0x0000001c  unsigned int
    #11             36  0x00000024  unsigned long
    #14              0  0x00000000  void
    #15             24  0x00000018  int
    #19             32  0x00000020  long
    #20             40  0x00000028  float
    #21             44  0x0000002c  double
    #22             12  0x0000000c  unsigned char
    #23             20  0x00000014  unsigned short
    #24             28  0x0000001c  unsigned int
    #25             36  0x00000024  unsigned long
    #26             48  0x00000030  struct no name { ifd = -1, index = 1048575 }
*/

/* Redefinition of of storage classes as an enumeration for better
   debugging.  */

typedef enum sc {
  sc_Nil	 = scNil,	  /* no storage class */
  sc_Text	 = scText,	  /* text symbol */
  sc_Data	 = scData,	  /* initialized data symbol */
  sc_Bss	 = scBss,	  /* un-initialized data symbol */
  sc_Register	 = scRegister,	  /* value of symbol is register number */
  sc_Abs	 = scAbs,	  /* value of symbol is absolute */
  sc_Undefined	 = scUndefined,	  /* who knows? */
  sc_CdbLocal	 = scCdbLocal,	  /* variable's value is IN se->va.?? */
  sc_Bits	 = scBits,	  /* this is a bit field */
  sc_CdbSystem	 = scCdbSystem,	  /* value is IN CDB's address space */
  sc_RegImage	 = scRegImage,	  /* register value saved on stack */
  sc_Info	 = scInfo,	  /* symbol contains debugger information */
  sc_UserStruct	 = scUserStruct,  /* addr in struct user for current process */
  sc_SData	 = scSData,	  /* load time only small data */
  sc_SBss	 = scSBss,	  /* load time only small common */
  sc_RData	 = scRData,	  /* load time only read only data */
  sc_Var	 = scVar,	  /* Var parameter (fortran,pascal) */
  sc_Common	 = scCommon,	  /* common variable */
  sc_SCommon	 = scSCommon,	  /* small common */
  sc_VarRegister = scVarRegister, /* Var parameter in a register */
  sc_Variant	 = scVariant,	  /* Variant record */
  sc_SUndefined	 = scSUndefined,  /* small undefined(external) data */
  sc_Init	 = scInit,	  /* .init section symbol */
  sc_Max	 = scMax	  /* Max storage class+1 */
} sc_t;

/* Redefinition of symbol type.  */

typedef enum st {
  st_Nil	= stNil,	/* Nuthin' special */
  st_Global	= stGlobal,	/* external symbol */
  st_Static	= stStatic,	/* static */
  st_Param	= stParam,	/* procedure argument */
  st_Local	= stLocal,	/* local variable */
  st_Label	= stLabel,	/* label */
  st_Proc	= stProc,	/*     "      "	 Procedure */
  st_Block	= stBlock,	/* beginning of block */
  st_End	= stEnd,	/* end (of anything) */
  st_Member	= stMember,	/* member (of anything	- struct/union/enum */
  st_Typedef	= stTypedef,	/* type definition */
  st_File	= stFile,	/* file name */
  st_RegReloc	= stRegReloc,	/* register relocation */
  st_Forward	= stForward,	/* forwarding address */
  st_StaticProc	= stStaticProc,	/* load time only static procs */
  st_Constant	= stConstant,	/* const */
  st_Str	= stStr,	/* string */
  st_Number	= stNumber,	/* pure number (ie. 4 NOR 2+2) */
  st_Expr	= stExpr,	/* 2+2 vs. 4 */
  st_Type	= stType,	/* post-coercion SER */
  st_Max	= stMax		/* max type+1 */
} st_t;

/* Redefinition of type qualifiers.  */

typedef enum tq {
  tq_Nil	= tqNil,	/* bt is what you see */
  tq_Ptr	= tqPtr,	/* pointer */
  tq_Proc	= tqProc,	/* procedure */
  tq_Array	= tqArray,	/* duh */
  tq_Far	= tqFar,	/* longer addressing - 8086/8 land */
  tq_Vol	= tqVol,	/* volatile */
  tq_Max	= tqMax		/* Max type qualifier+1 */
} tq_t;

/* Redefinition of basic types.  */

typedef enum bt {
  bt_Nil	= btNil,	/* undefined */
  bt_Adr	= btAdr,	/* address - integer same size as pointer */
  bt_Char	= btChar,	/* character */
  bt_UChar	= btUChar,	/* unsigned character */
  bt_Short	= btShort,	/* short */
  bt_UShort	= btUShort,	/* unsigned short */
  bt_Int	= btInt,	/* int */
  bt_UInt	= btUInt,	/* unsigned int */
  bt_Long	= btLong,	/* long */
  bt_ULong	= btULong,	/* unsigned long */
  bt_Float	= btFloat,	/* float (real) */
  bt_Double	= btDouble,	/* Double (real) */
  bt_Struct	= btStruct,	/* Structure (Record) */
  bt_Union	= btUnion,	/* Union (variant) */
  bt_Enum	= btEnum,	/* Enumerated */
  bt_Typedef	= btTypedef,	/* defined via a typedef, isymRef points */
  bt_Range	= btRange,	/* subrange of int */
  bt_Set	= btSet,	/* pascal sets */
  bt_Complex	= btComplex,	/* fortran complex */
  bt_DComplex	= btDComplex,	/* fortran double complex */
  bt_Indirect	= btIndirect,	/* forward or unnamed typedef */
  bt_FixedDec	= btFixedDec,	/* Fixed Decimal */
  bt_FloatDec	= btFloatDec,	/* Float Decimal */
  bt_String	= btString,	/* Varying Length Character String */
  bt_Bit	= btBit,	/* Aligned Bit String */
  bt_Picture	= btPicture,	/* Picture */
  bt_Void	= btVoid,	/* Void */
  bt_Max	= btMax		/* Max basic type+1 */
} bt_t;

#define N_TQ itqMax

/* States for whether to hash type or not.  */
typedef enum hash_state {
  hash_no	= 0,		/* Don't hash type */
  hash_yes	= 1,		/* OK to hash type, or use previous hash */
  hash_record	= 2		/* OK to record hash, but don't use prev.  */
} hash_state_t;

/* Types of different sized allocation requests.  */
enum alloc_type {
  alloc_type_none,		/* dummy value */
  alloc_type_scope,		/* nested scopes linked list */
  alloc_type_vlinks,		/* glue linking pages in varray */
  alloc_type_shash,		/* string hash element */
  alloc_type_thash,		/* type hash element */
  alloc_type_tag,		/* struct/union/tag element */
  alloc_type_forward,		/* element to hold unknown tag */
  alloc_type_thead,		/* head of type hash list */
  alloc_type_varray,		/* general varray allocation */
  alloc_type_lineno,		/* line number list */
  alloc_type_last		/* last+1 element for array bounds */
};

/* Types of auxiliary type information.  */
enum aux_type {
  aux_tir,			/* TIR type information */
  aux_rndx,			/* relative index into symbol table */
  aux_dnLow,			/* low dimension */
  aux_dnHigh,			/* high dimension */
  aux_isym,			/* symbol table index (end of proc) */
  aux_iss,			/* index into string space (not used) */
  aux_width,			/* width for non-default sized struc fields */
  aux_count			/* count of ranges for variant arm */
};

/* Structures to provide n-number of virtual arrays, each of which can
   grow linearly, and which are written in the object file as
   sequential pages.  On systems with a BSD malloc, the
   MAX_CLUSTER_PAGES should be 1 less than a power of two, since
   malloc adds it's overhead, and rounds up to the next power of 2.
   Pages are linked together via a linked list.

   If PAGE_SIZE is > 4096, the string length in the shash_t structure
   can't be represented (assuming there are strings > 4096 bytes).  */

/* FIXME: Yes, there can be such strings while emitting C++ class debug
   info.  Templates are the offender here, the test case in question
   having a mangled class name of

     t7rb_tree4Z4xkeyZt4pair2ZC4xkeyZt7xsocket1Z4UserZt9select1st2Zt4pair\
     2ZC4xkeyZt7xsocket1Z4UserZ4xkeyZt4less1Z4xkey

   Repeat that a couple dozen times while listing the class members and
   you've got strings over 4k.  Hack around this for now by increasing
   the page size.  A proper solution would abandon this structure scheme
   certainly for very large strings, and possibly entirely.  */

#ifndef PAGE_SIZE
#define PAGE_SIZE (8*1024)	/* size of varray pages */
#endif

#define PAGE_USIZE ((unsigned long) PAGE_SIZE)

#ifndef MAX_CLUSTER_PAGES	/* # pages to get from system */
#define MAX_CLUSTER_PAGES 63
#endif

/* Linked list connecting separate page allocations.  */
typedef struct vlinks {
  struct vlinks	*prev;		/* previous set of pages */
  struct vlinks *next;		/* next set of pages */
  union  page   *datum;		/* start of page */
  unsigned long	 start_index;	/* starting index # of page */
} vlinks_t;

/* Virtual array header.  */
typedef struct varray {
  vlinks_t	*first;			/* first page link */
  vlinks_t	*last;			/* last page link */
  unsigned long	 num_allocated;		/* # objects allocated */
  unsigned short object_size;		/* size in bytes of each object */
  unsigned short objects_per_page;	/* # objects that can fit on a page */
  unsigned short objects_last_page;	/* # objects allocated on last page */
} varray_t;

#ifndef MALLOC_CHECK
#define OBJECTS_PER_PAGE(type) (PAGE_SIZE / sizeof (type))
#else
#define OBJECTS_PER_PAGE(type) ((sizeof (type) > 1) ? 1 : PAGE_SIZE)
#endif

#define INIT_VARRAY(type) {	/* macro to initialize a varray */	\
  (vlinks_t *)0,		/* first */				\
  (vlinks_t *)0,		/* last */				\
  0,				/* num_allocated */			\
  sizeof (type),		/* object_size */			\
  OBJECTS_PER_PAGE (type),	/* objects_per_page */			\
  OBJECTS_PER_PAGE (type),	/* objects_last_page */			\
}

/* Master type for indexes within the symbol table.  */
typedef unsigned long symint_t;

/* Linked list support for nested scopes (file, block, structure, etc.).  */
typedef struct scope {
  struct scope	*prev;		/* previous scope level */
  struct scope	*free;		/* free list pointer */
  struct localsym *lsym;	/* pointer to local symbol node */
  st_t		 type;		/* type of the node */
} scope_t;

/* For a local symbol we store a gas symbol as well as the debugging
   information we generate.  The gas symbol will be NULL if this is
   only a debugging symbol.  */
typedef struct localsym {
  const char *name;		/* symbol name */
  symbolS *as_sym;		/* symbol as seen by gas */
  bfd_vma addend;		/* addend to as_sym value */
  struct efdr *file_ptr;	/* file pointer */
  struct ecoff_proc *proc_ptr;	/* proc pointer */
  struct localsym *begin_ptr;	/* symbol at start of block */
  struct ecoff_aux *index_ptr;	/* index value to be filled in */
  struct forward *forward_ref;	/* forward references to this symbol */
  long sym_index;		/* final symbol index */
  EXTR ecoff_sym;		/* ECOFF debugging symbol */
} localsym_t;

/* For aux information we keep the type and the data.  */
typedef struct ecoff_aux {
  enum aux_type type;		/* aux type */
  AUXU data;			/* aux data */
} aux_t;

/* For a procedure we store the gas symbol as well as the PDR
   debugging information.  */
typedef struct ecoff_proc {
  localsym_t *sym;		/* associated symbol */
  PDR pdr;			/* ECOFF debugging info */
} proc_t;

/* Number of proc_t structures allocated.  */
static unsigned long proc_cnt;

/* Forward reference list for tags referenced, but not yet defined.  */
typedef struct forward {
  struct forward *next;		/* next forward reference */
  struct forward *free;		/* free list pointer */
  aux_t		 *ifd_ptr;	/* pointer to store file index */
  aux_t		 *index_ptr;	/* pointer to store symbol index */
} forward_t;

/* Linked list support for tags.  The first tag in the list is always
   the current tag for that block.  */
typedef struct tag {
  struct tag	 *free;		/* free list pointer */
  struct shash	 *hash_ptr;	/* pointer to the hash table head */
  struct tag	 *same_name;	/* tag with same name in outer scope */
  struct tag	 *same_block;	/* next tag defined in the same block.  */
  struct forward *forward_ref;	/* list of forward references */
  bt_t		  basic_type;	/* bt_Struct, bt_Union, or bt_Enum */
  symint_t	  ifd;		/* file # tag defined in */
  localsym_t	 *sym;		/* file's local symbols */
} tag_t;

/* Head of a block's linked list of tags.  */
typedef struct thead {
  struct thead	*prev;		/* previous block */
  struct thead	*free;		/* free list pointer */
  struct tag	*first_tag;	/* first tag in block defined */
} thead_t;

/* Union containing pointers to each the small structures which are freed up.  */
typedef union small_free {
  scope_t	*f_scope;	/* scope structure */
  thead_t	*f_thead;	/* tag head structure */
  tag_t		*f_tag;		/* tag element structure */
  forward_t	*f_forward;	/* forward tag reference */
} small_free_t;

/* String hash table entry.  */

typedef struct shash {
  char		*string;	/* string we are hashing */
  symint_t	 indx;		/* index within string table */
  EXTR		*esym_ptr;	/* global symbol pointer */
  localsym_t	*sym_ptr;	/* local symbol pointer */
  localsym_t	*end_ptr;	/* symbol pointer to end block */
  tag_t		*tag_ptr;	/* tag pointer */
  proc_t	*proc_ptr;	/* procedure descriptor pointer */
} shash_t;

/* Type hash table support.  The size of the hash table must fit
   within a page with the other extended file descriptor information.
   Because unique types which are hashed are fewer in number than
   strings, we use a smaller hash value.  */

#define HASHBITS 30

#ifndef THASH_SIZE
#define THASH_SIZE 113
#endif

typedef struct thash {
  struct thash	*next;		/* next hash value */
  AUXU		 type;		/* type we are hashing */
  symint_t	 indx;		/* index within string table */
} thash_t;

/* Extended file descriptor that contains all of the support necessary
   to add things to each file separately.  */
typedef struct efdr {
  FDR		 fdr;		/* File header to be written out */
  FDR		*orig_fdr;	/* original file header */
  char		*name;		/* filename */
  int		 fake;		/* whether this is faked .file */
  symint_t	 void_type;	/* aux. pointer to 'void' type */
  symint_t	 int_type;	/* aux. pointer to 'int' type */
  scope_t	*cur_scope;	/* current nested scopes */
  symint_t	 file_index;	/* current file number */
  int		 nested_scopes;	/* # nested scopes */
  varray_t	 strings;	/* local strings */
  varray_t	 symbols;	/* local symbols */
  varray_t	 procs;		/* procedures */
  varray_t	 aux_syms;	/* auxiliary symbols */
  struct efdr	*next_file;	/* next file descriptor */
				/* string/type hash tables */
  struct hash_control *str_hash;	/* string hash table */
  thash_t	*thash_head[THASH_SIZE];
} efdr_t;

/* Pre-initialized extended file structure.  */
static const efdr_t init_file = {
  {			/* FDR structure */
    0,			/* adr:		memory address of beginning of file */
    0,			/* rss:		file name (of source, if known) */
    0,			/* issBase:	file's string space */
    0,			/* cbSs:	number of bytes in the ss */
    0,			/* isymBase:	beginning of symbols */
    0,			/* csym:	count file's of symbols */
    0,			/* ilineBase:	file's line symbols */
    0,			/* cline:	count of file's line symbols */
    0,			/* ioptBase:	file's optimization entries */
    0,			/* copt:	count of file's optimization entries */
    0,			/* ipdFirst:	start of procedures for this file */
    0,			/* cpd:		count of procedures for this file */
    0,			/* iauxBase:	file's auxiliary entries */
    0,			/* caux:	count of file's auxiliary entries */
    0,			/* rfdBase:	index into the file indirect table */
    0,			/* crfd:	count file indirect entries */
    langC,		/* lang:	language for this file */
    1,			/* fMerge:	whether this file can be merged */
    0,			/* fReadin:	true if read in (not just created) */
    TARGET_BYTES_BIG_ENDIAN,  /* fBigendian:	if 1, compiled on big endian machine */
    GLEVEL_2,		/* glevel:	level this file was compiled with */
    0,			/* reserved:	reserved for future use */
    0,			/* cbLineOffset: byte offset from header for this file ln's */
    0,			/* cbLine:	size of lines for this file */
  },

  (FDR *)0,		/* orig_fdr:	original file header pointer */
  (char *)0,		/* name:	pointer to filename */
  0,			/* fake:	whether this is a faked .file */
  0,			/* void_type:	ptr to aux node for void type */
  0,			/* int_type:	ptr to aux node for int type */
  (scope_t *)0,		/* cur_scope:	current scope being processed */
  0,			/* file_index:	current file # */
  0,			/* nested_scopes: # nested scopes */
  INIT_VARRAY (char),	/* strings:	local string varray */
  INIT_VARRAY (localsym_t),	/* symbols:	local symbols varray */
  INIT_VARRAY (proc_t),	/* procs:	procedure varray */
  INIT_VARRAY (aux_t),	/* aux_syms:	auxiliary symbols varray */

  (struct efdr *)0,	/* next_file:	next file structure */

  (struct hash_control *)0,	/* str_hash:	string hash table */
  { 0 },		/* thash_head:	type hash table */
};

static efdr_t *first_file;			/* first file descriptor */
static efdr_t **last_file_ptr = &first_file;	/* file descriptor tail */

/* Line number information is kept in a list until the assembly is
   finished.  */
typedef struct lineno_list {
  struct lineno_list *next;	/* next element in list */
  efdr_t *file;			/* file this line is in */
  proc_t *proc;			/* procedure this line is in */
  fragS *frag;			/* fragment this line number is in */
  unsigned long paddr;		/* offset within fragment */
  long lineno;			/* actual line number */
} lineno_list_t;

static lineno_list_t *first_lineno;
static lineno_list_t *last_lineno;
static lineno_list_t **last_lineno_ptr = &first_lineno;

/* Sometimes there will be some .loc statements before a .ent.  We
   keep them in this list so that we can fill in the procedure pointer
   after we see the .ent.  */
static lineno_list_t *noproc_lineno;

/* Union of various things that are held in pages.  */
typedef union page {
  char		byte	[ PAGE_SIZE ];
  unsigned char	ubyte	[ PAGE_SIZE ];
  efdr_t	file	[ PAGE_SIZE / sizeof (efdr_t)	     ];
  FDR		ofile	[ PAGE_SIZE / sizeof (FDR)	     ];
  proc_t	proc	[ PAGE_SIZE / sizeof (proc_t)	     ];
  localsym_t	sym	[ PAGE_SIZE / sizeof (localsym_t)    ];
  aux_t		aux	[ PAGE_SIZE / sizeof (aux_t)	     ];
  DNR		dense	[ PAGE_SIZE / sizeof (DNR)	     ];
  scope_t	scope	[ PAGE_SIZE / sizeof (scope_t)	     ];
  vlinks_t	vlinks	[ PAGE_SIZE / sizeof (vlinks_t)	     ];
  shash_t	shash	[ PAGE_SIZE / sizeof (shash_t)	     ];
  thash_t	thash	[ PAGE_SIZE / sizeof (thash_t)	     ];
  tag_t		tag	[ PAGE_SIZE / sizeof (tag_t)	     ];
  forward_t	forward	[ PAGE_SIZE / sizeof (forward_t)     ];
  thead_t	thead	[ PAGE_SIZE / sizeof (thead_t)	     ];
  lineno_list_t	lineno	[ PAGE_SIZE / sizeof (lineno_list_t) ];
} page_type;

/* Structure holding allocation information for small sized structures.  */
typedef struct alloc_info {
  char		*alloc_name;	/* name of this allocation type (must be first) */
  page_type	*cur_page;	/* current page being allocated from */
  small_free_t	 free_list;	/* current free list if any */
  int		 unallocated;	/* number of elements unallocated on page */
  int		 total_alloc;	/* total number of allocations */
  int		 total_free;	/* total number of frees */
  int		 total_pages;	/* total number of pages allocated */
} alloc_info_t;

/* Type information collected together.  */
typedef struct type_info {
  bt_t	      basic_type;		/* basic type */
  int	      orig_type;		/* original COFF-based type */
  int	      num_tq;			/* # type qualifiers */
  int	      num_dims;			/* # dimensions */
  int	      num_sizes;		/* # sizes */
  int	      extra_sizes;		/* # extra sizes not tied with dims */
  tag_t *     tag_ptr;			/* tag pointer */
  int	      bitfield;			/* symbol is a bitfield */
  tq_t	      type_qualifiers[N_TQ];	/* type qualifiers (ptr, func, array)*/
  symint_t    dimensions     [N_TQ];	/* dimensions for each array */
  symint_t    sizes	     [N_TQ+2];	/* sizes of each array slice + size of
					   struct/union/enum + bitfield size */
} type_info_t;

/* Pre-initialized type_info struct.  */
static const type_info_t type_info_init = {
  bt_Nil,				/* basic type */
  T_NULL,				/* original COFF-based type */
  0,					/* # type qualifiers */
  0,					/* # dimensions */
  0,					/* # sizes */
  0,					/* sizes not tied with dims */
  NULL,					/* ptr to tag */
  0,					/* bitfield */
  {					/* type qualifiers */
    tq_Nil,
    tq_Nil,
    tq_Nil,
    tq_Nil,
    tq_Nil,
    tq_Nil,
  },
  {					/* dimensions */
    0,
    0,
    0,
    0,
    0,
    0
  },
  {					/* sizes */
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
  },
};

/* Global hash table for the tags table and global table for file
   descriptors.  */

static varray_t file_desc = INIT_VARRAY (efdr_t);

static struct hash_control *tag_hash;

/* Static types for int and void.  Also, remember the last function's
   type (which is set up when we encounter the declaration for the
   function, and used when the end block for the function is emitted.  */

static type_info_t int_type_info;
static type_info_t void_type_info;
static type_info_t last_func_type_info;
static symbolS *last_func_sym_value;

/* Convert COFF basic type to ECOFF basic type.  The T_NULL type
   really should use bt_Void, but this causes the current ecoff GDB to
   issue unsupported type messages, and the Ultrix 4.00 dbx (aka MIPS
   2.0) doesn't understand it, even though the compiler generates it.
   Maybe this will be fixed in 2.10 or 2.20 of the MIPS compiler
   suite, but for now go with what works.

   It would make sense for the .type and .scl directives to use the
   ECOFF numbers directly, rather than using the COFF numbers and
   mapping them.  Unfortunately, this is historically what mips-tfile
   expects, and changing gcc now would be a considerable pain (the
   native compiler generates debugging information internally, rather
   than via the assembler, so it will never use .type or .scl).  */

static const bt_t map_coff_types[] = {
  bt_Nil,			/* T_NULL */
  bt_Nil,			/* T_ARG */
  bt_Char,			/* T_CHAR */
  bt_Short,			/* T_SHORT */
  bt_Int,			/* T_INT */
  bt_Long,			/* T_LONG */
  bt_Float,			/* T_FLOAT */
  bt_Double,			/* T_DOUBLE */
  bt_Struct,			/* T_STRUCT */
  bt_Union,			/* T_UNION */
  bt_Enum,			/* T_ENUM */
  bt_Enum,			/* T_MOE */
  bt_UChar,			/* T_UCHAR */
  bt_UShort,			/* T_USHORT */
  bt_UInt,			/* T_UINT */
  bt_ULong			/* T_ULONG */
};

/* Convert COFF storage class to ECOFF storage class.  */
static const sc_t map_coff_storage[] = {
  sc_Nil,			/*   0: C_NULL */
  sc_Abs,			/*   1: C_AUTO	  auto var */
  sc_Undefined,			/*   2: C_EXT	  external */
  sc_Data,			/*   3: C_STAT	  static */
  sc_Register,			/*   4: C_REG	  register */
  sc_Undefined,			/*   5: C_EXTDEF  ??? */
  sc_Text,			/*   6: C_LABEL	  label */
  sc_Text,			/*   7: C_ULABEL  user label */
  sc_Info,			/*   8: C_MOS	  member of struct */
  sc_Abs,			/*   9: C_ARG	  argument */
  sc_Info,			/*  10: C_STRTAG  struct tag */
  sc_Info,			/*  11: C_MOU	  member of union */
  sc_Info,			/*  12: C_UNTAG   union tag */
  sc_Info,			/*  13: C_TPDEF	  typedef */
  sc_Data,			/*  14: C_USTATIC ??? */
  sc_Info,			/*  15: C_ENTAG	  enum tag */
  sc_Info,			/*  16: C_MOE	  member of enum */
  sc_Register,			/*  17: C_REGPARM register parameter */
  sc_Bits,			/*  18; C_FIELD	  bitfield */
  sc_Nil,			/*  19 */
  sc_Nil,			/*  20 */
  sc_Nil,			/*  21 */
  sc_Nil,			/*  22 */
  sc_Nil,			/*  23 */
  sc_Nil,			/*  24 */
  sc_Nil,			/*  25 */
  sc_Nil,			/*  26 */
  sc_Nil,			/*  27 */
  sc_Nil,			/*  28 */
  sc_Nil,			/*  29 */
  sc_Nil,			/*  30 */
  sc_Nil,			/*  31 */
  sc_Nil,			/*  32 */
  sc_Nil,			/*  33 */
  sc_Nil,			/*  34 */
  sc_Nil,			/*  35 */
  sc_Nil,			/*  36 */
  sc_Nil,			/*  37 */
  sc_Nil,			/*  38 */
  sc_Nil,			/*  39 */
  sc_Nil,			/*  40 */
  sc_Nil,			/*  41 */
  sc_Nil,			/*  42 */
  sc_Nil,			/*  43 */
  sc_Nil,			/*  44 */
  sc_Nil,			/*  45 */
  sc_Nil,			/*  46 */
  sc_Nil,			/*  47 */
  sc_Nil,			/*  48 */
  sc_Nil,			/*  49 */
  sc_Nil,			/*  50 */
  sc_Nil,			/*  51 */
  sc_Nil,			/*  52 */
  sc_Nil,			/*  53 */
  sc_Nil,			/*  54 */
  sc_Nil,			/*  55 */
  sc_Nil,			/*  56 */
  sc_Nil,			/*  57 */
  sc_Nil,			/*  58 */
  sc_Nil,			/*  59 */
  sc_Nil,			/*  60 */
  sc_Nil,			/*  61 */
  sc_Nil,			/*  62 */
  sc_Nil,			/*  63 */
  sc_Nil,			/*  64 */
  sc_Nil,			/*  65 */
  sc_Nil,			/*  66 */
  sc_Nil,			/*  67 */
  sc_Nil,			/*  68 */
  sc_Nil,			/*  69 */
  sc_Nil,			/*  70 */
  sc_Nil,			/*  71 */
  sc_Nil,			/*  72 */
  sc_Nil,			/*  73 */
  sc_Nil,			/*  74 */
  sc_Nil,			/*  75 */
  sc_Nil,			/*  76 */
  sc_Nil,			/*  77 */
  sc_Nil,			/*  78 */
  sc_Nil,			/*  79 */
  sc_Nil,			/*  80 */
  sc_Nil,			/*  81 */
  sc_Nil,			/*  82 */
  sc_Nil,			/*  83 */
  sc_Nil,			/*  84 */
  sc_Nil,			/*  85 */
  sc_Nil,			/*  86 */
  sc_Nil,			/*  87 */
  sc_Nil,			/*  88 */
  sc_Nil,			/*  89 */
  sc_Nil,			/*  90 */
  sc_Nil,			/*  91 */
  sc_Nil,			/*  92 */
  sc_Nil,			/*  93 */
  sc_Nil,			/*  94 */
  sc_Nil,			/*  95 */
  sc_Nil,			/*  96 */
  sc_Nil,			/*  97 */
  sc_Nil,			/*  98 */
  sc_Nil,			/*  99 */
  sc_Text,			/* 100: C_BLOCK  block start/end */
  sc_Text,			/* 101: C_FCN	 function start/end */
  sc_Info,			/* 102: C_EOS	 end of struct/union/enum */
  sc_Nil,			/* 103: C_FILE	 file start */
  sc_Nil,			/* 104: C_LINE	 line number */
  sc_Nil,			/* 105: C_ALIAS	 combined type info */
  sc_Nil,			/* 106: C_HIDDEN ??? */
};

/* Convert COFF storage class to ECOFF symbol type.  */
static const st_t map_coff_sym_type[] = {
  st_Nil,			/*   0: C_NULL */
  st_Local,			/*   1: C_AUTO	  auto var */
  st_Global,			/*   2: C_EXT	  external */
  st_Static,			/*   3: C_STAT	  static */
  st_Local,			/*   4: C_REG	  register */
  st_Global,			/*   5: C_EXTDEF  ??? */
  st_Label,			/*   6: C_LABEL	  label */
  st_Label,			/*   7: C_ULABEL  user label */
  st_Member,			/*   8: C_MOS	  member of struct */
  st_Param,			/*   9: C_ARG	  argument */
  st_Block,			/*  10: C_STRTAG  struct tag */
  st_Member,			/*  11: C_MOU	  member of union */
  st_Block,			/*  12: C_UNTAG   union tag */
  st_Typedef,			/*  13: C_TPDEF	  typedef */
  st_Static,			/*  14: C_USTATIC ??? */
  st_Block,			/*  15: C_ENTAG	  enum tag */
  st_Member,			/*  16: C_MOE	  member of enum */
  st_Param,			/*  17: C_REGPARM register parameter */
  st_Member,			/*  18; C_FIELD	  bitfield */
  st_Nil,			/*  19 */
  st_Nil,			/*  20 */
  st_Nil,			/*  21 */
  st_Nil,			/*  22 */
  st_Nil,			/*  23 */
  st_Nil,			/*  24 */
  st_Nil,			/*  25 */
  st_Nil,			/*  26 */
  st_Nil,			/*  27 */
  st_Nil,			/*  28 */
  st_Nil,			/*  29 */
  st_Nil,			/*  30 */
  st_Nil,			/*  31 */
  st_Nil,			/*  32 */
  st_Nil,			/*  33 */
  st_Nil,			/*  34 */
  st_Nil,			/*  35 */
  st_Nil,			/*  36 */
  st_Nil,			/*  37 */
  st_Nil,			/*  38 */
  st_Nil,			/*  39 */
  st_Nil,			/*  40 */
  st_Nil,			/*  41 */
  st_Nil,			/*  42 */
  st_Nil,			/*  43 */
  st_Nil,			/*  44 */
  st_Nil,			/*  45 */
  st_Nil,			/*  46 */
  st_Nil,			/*  47 */
  st_Nil,			/*  48 */
  st_Nil,			/*  49 */
  st_Nil,			/*  50 */
  st_Nil,			/*  51 */
  st_Nil,			/*  52 */
  st_Nil,			/*  53 */
  st_Nil,			/*  54 */
  st_Nil,			/*  55 */
  st_Nil,			/*  56 */
  st_Nil,			/*  57 */
  st_Nil,			/*  58 */
  st_Nil,			/*  59 */
  st_Nil,			/*  60 */
  st_Nil,			/*  61 */
  st_Nil,			/*  62 */
  st_Nil,			/*  63 */
  st_Nil,			/*  64 */
  st_Nil,			/*  65 */
  st_Nil,			/*  66 */
  st_Nil,			/*  67 */
  st_Nil,			/*  68 */
  st_Nil,			/*  69 */
  st_Nil,			/*  70 */
  st_Nil,			/*  71 */
  st_Nil,			/*  72 */
  st_Nil,			/*  73 */
  st_Nil,			/*  74 */
  st_Nil,			/*  75 */
  st_Nil,			/*  76 */
  st_Nil,			/*  77 */
  st_Nil,			/*  78 */
  st_Nil,			/*  79 */
  st_Nil,			/*  80 */
  st_Nil,			/*  81 */
  st_Nil,			/*  82 */
  st_Nil,			/*  83 */
  st_Nil,			/*  84 */
  st_Nil,			/*  85 */
  st_Nil,			/*  86 */
  st_Nil,			/*  87 */
  st_Nil,			/*  88 */
  st_Nil,			/*  89 */
  st_Nil,			/*  90 */
  st_Nil,			/*  91 */
  st_Nil,			/*  92 */
  st_Nil,			/*  93 */
  st_Nil,			/*  94 */
  st_Nil,			/*  95 */
  st_Nil,			/*  96 */
  st_Nil,			/*  97 */
  st_Nil,			/*  98 */
  st_Nil,			/*  99 */
  st_Block,			/* 100: C_BLOCK  block start/end */
  st_Proc,			/* 101: C_FCN	 function start/end */
  st_End,			/* 102: C_EOS	 end of struct/union/enum */
  st_File,			/* 103: C_FILE	 file start */
  st_Nil,			/* 104: C_LINE	 line number */
  st_Nil,			/* 105: C_ALIAS	 combined type info */
  st_Nil,			/* 106: C_HIDDEN ??? */
};

/* Keep track of different sized allocation requests.  */
static alloc_info_t alloc_counts[(int) alloc_type_last];

/* Record whether we have seen any debugging information.  */
int ecoff_debugging_seen = 0;

/* Various statics.  */
static efdr_t  *cur_file_ptr	= (efdr_t *) 0;	/* current file desc. header */
static proc_t  *cur_proc_ptr	= (proc_t *) 0;	/* current procedure header */
static proc_t  *first_proc_ptr  = (proc_t *) 0; /* first procedure header */
static thead_t *top_tag_head	= (thead_t *) 0; /* top level tag head */
static thead_t *cur_tag_head	= (thead_t *) 0; /* current tag head */
#ifdef ECOFF_DEBUG
static int	debug		= 0; 		/* trace functions */
#endif
static int	stabs_seen	= 0;		/* != 0 if stabs have been seen */

static int current_file_idx;
static const char *current_stabs_filename;

/* Pseudo symbol to use when putting stabs into the symbol table.  */
#ifndef STABS_SYMBOL
#define STABS_SYMBOL "@stabs"
#endif

static char stabs_symbol[] = STABS_SYMBOL;

/* Prototypes for functions defined in this file.  */

static void add_varray_page (varray_t *vp);
static symint_t add_string (varray_t *vp,
			    struct hash_control *hash_tbl,
			    const char *str,
			    shash_t **ret_hash);
static localsym_t *add_ecoff_symbol (const char *str, st_t type,
				     sc_t storage, symbolS *sym,
				     bfd_vma addend, symint_t value,
				     symint_t indx);
static symint_t add_aux_sym_symint (symint_t aux_word);
static symint_t add_aux_sym_rndx (int file_index, symint_t sym_index);
static symint_t add_aux_sym_tir (type_info_t *t,
				 hash_state_t state,
				 thash_t **hash_tbl);
static tag_t *get_tag (const char *tag, localsym_t *sym, bt_t basic_type);
static void add_unknown_tag (tag_t *ptag);
static void add_procedure (char *func);
static void add_file (const char *file_name, int indx, int fake);
#ifdef ECOFF_DEBUG
static char *sc_to_string (sc_t storage_class);
static char *st_to_string (st_t symbol_type);
#endif
static void mark_stabs (int);
static char *ecoff_add_bytes (char **buf, char **bufend,
			      char *bufptr, unsigned long need);
static unsigned long ecoff_padding_adjust
  (const struct ecoff_debug_swap *backend, char **buf, char **bufend,
   unsigned long offset, char **bufptrptr);
static unsigned long ecoff_build_lineno
  (const struct ecoff_debug_swap *backend, char **buf, char **bufend,
   unsigned long offset, long *linecntptr);
static unsigned long ecoff_build_symbols
  (const struct ecoff_debug_swap *backend, char **buf, char **bufend,
   unsigned long offset);
static unsigned long ecoff_build_procs
  (const struct ecoff_debug_swap *backend, char **buf, char **bufend,
   unsigned long offset);
static unsigned long ecoff_build_aux
  (const struct ecoff_debug_swap *backend, char **buf, char **bufend,
   unsigned long offset);
static unsigned long ecoff_build_strings (char **buf, char **bufend,
					  unsigned long offset,
					  varray_t *vp);
static unsigned long ecoff_build_ss
  (const struct ecoff_debug_swap *backend, char **buf, char **bufend,
   unsigned long offset);
static unsigned long ecoff_build_fdr
  (const struct ecoff_debug_swap *backend, char **buf, char **bufend,
   unsigned long offset);
static void ecoff_setup_ext (void);
static page_type *allocate_cluster (unsigned long npages);
static page_type *allocate_page (void);
static scope_t *allocate_scope (void);
static void free_scope (scope_t *ptr);
static vlinks_t *allocate_vlinks (void);
static shash_t *allocate_shash (void);
static thash_t *allocate_thash (void);
static tag_t *allocate_tag (void);
static void free_tag (tag_t *ptr);
static forward_t *allocate_forward (void);
static thead_t *allocate_thead (void);
static void free_thead (thead_t *ptr);
static lineno_list_t *allocate_lineno_list (void);

/* This function should be called when the assembler starts up.  */

void
ecoff_read_begin_hook (void)
{
  tag_hash = hash_new ();
  top_tag_head = allocate_thead ();
  top_tag_head->first_tag = (tag_t *) NULL;
  top_tag_head->free = (thead_t *) NULL;
  top_tag_head->prev = cur_tag_head;
  cur_tag_head = top_tag_head;
}

/* This function should be called when a symbol is created.  */

void
ecoff_symbol_new_hook (symbolS *symbolP)
{
  OBJ_SYMFIELD_TYPE *obj;

  /* Make sure that we have a file pointer, but only if we have seen a
     file.  If we haven't seen a file, then this is a probably special
     symbol created by md_begin which may required special handling at
     some point.  Creating a dummy file with a dummy name is certainly
     wrong.  */
  if (cur_file_ptr == (efdr_t *) NULL
      && seen_at_least_1_file ())
    add_file ((const char *) NULL, 0, 1);
  obj = symbol_get_obj (symbolP);
  obj->ecoff_file = cur_file_ptr;
  obj->ecoff_symbol = NULL;
  obj->ecoff_extern_size = 0;
}

/* Add a page to a varray object.  */

static void
add_varray_page (varray_t *vp /* varray to add page to */)
{
  vlinks_t *new_links = allocate_vlinks ();

#ifdef MALLOC_CHECK
  if (vp->object_size > 1)
    new_links->datum = (page_type *) xcalloc (1, vp->object_size);
  else
#endif
    new_links->datum = allocate_page ();

  alloc_counts[(int) alloc_type_varray].total_alloc++;
  alloc_counts[(int) alloc_type_varray].total_pages++;

  new_links->start_index = vp->num_allocated;
  vp->objects_last_page = 0;

  if (vp->first == (vlinks_t *) NULL)		/* first allocation? */
    vp->first = vp->last = new_links;
  else
    {						/* 2nd or greater allocation */
      new_links->prev = vp->last;
      vp->last->next = new_links;
      vp->last = new_links;
    }
}

/* Add a string (and null pad) to one of the string tables.  */

static symint_t
add_string (varray_t *vp,			/* string obstack */
	    struct hash_control *hash_tbl,	/* ptr to hash table */
	    const char *str,			/* string */
	    shash_t **ret_hash			/* return hash pointer */)
{
  register unsigned long len = strlen (str);
  register shash_t *hash_ptr;

  if (len >= PAGE_USIZE)
    as_fatal (_("string too big (%lu bytes)"), len);

  hash_ptr = (shash_t *) hash_find (hash_tbl, str);
  if (hash_ptr == (shash_t *) NULL)
    {
      register const char *err;

      if (vp->objects_last_page + len >= PAGE_USIZE)
	{
	  vp->num_allocated =
	    ((vp->num_allocated + PAGE_USIZE - 1) / PAGE_USIZE) * PAGE_USIZE;
	  add_varray_page (vp);
	}

      hash_ptr = allocate_shash ();
      hash_ptr->indx = vp->num_allocated;

      hash_ptr->string = &vp->last->datum->byte[vp->objects_last_page];

      vp->objects_last_page += len + 1;
      vp->num_allocated += len + 1;

      strcpy (hash_ptr->string, str);

      err = hash_insert (hash_tbl, str, (char *) hash_ptr);
      if (err)
	as_fatal (_("inserting \"%s\" into string hash table: %s"),
		  str, err);
    }

  if (ret_hash != (shash_t **) NULL)
    *ret_hash = hash_ptr;

  return hash_ptr->indx;
}

/* Add debugging information for a symbol.  */

static localsym_t *
add_ecoff_symbol (const char *str,	/* symbol name */
		  st_t type,		/* symbol type */
		  sc_t storage,		/* storage class */
		  symbolS *sym_value,	/* associated symbol.  */
		  bfd_vma addend,	/* addend to sym_value.  */
		  symint_t value,	/* value of symbol */
		  symint_t indx		/* index to local/aux. syms */)
{
  localsym_t *psym;
  register scope_t *pscope;
  register thead_t *ptag_head;
  register tag_t *ptag;
  register tag_t *ptag_next;
  register varray_t *vp;
  register int scope_delta = 0;
  shash_t *hash_ptr = (shash_t *) NULL;

  if (cur_file_ptr == (efdr_t *) NULL)
    as_fatal (_("no current file pointer"));

  vp = &cur_file_ptr->symbols;

  if (vp->objects_last_page == vp->objects_per_page)
    add_varray_page (vp);

  psym = &vp->last->datum->sym[vp->objects_last_page++];

  if (str == (const char *) NULL && sym_value != (symbolS *) NULL)
    psym->name = S_GET_NAME (sym_value);
  else
    psym->name = str;
  psym->as_sym = sym_value;
  if (sym_value != (symbolS *) NULL)
    symbol_get_obj (sym_value)->ecoff_symbol = psym;
  psym->addend = addend;
  psym->file_ptr = cur_file_ptr;
  psym->proc_ptr = cur_proc_ptr;
  psym->begin_ptr = (localsym_t *) NULL;
  psym->index_ptr = (aux_t *) NULL;
  psym->forward_ref = (forward_t *) NULL;
  psym->sym_index = -1;
  memset (&psym->ecoff_sym, 0, sizeof (EXTR));
  psym->ecoff_sym.asym.value = value;
  psym->ecoff_sym.asym.st = (unsigned) type;
  psym->ecoff_sym.asym.sc = (unsigned) storage;
  psym->ecoff_sym.asym.index = indx;

  /* If there is an associated symbol, we wait until the end of the
     assembly before deciding where to put the name (it may be just an
     external symbol).  Otherwise, this is just a debugging symbol and
     the name should go with the current file.  */
  if (sym_value == (symbolS *) NULL)
    psym->ecoff_sym.asym.iss = ((str == (const char *) NULL)
				? 0
				: add_string (&cur_file_ptr->strings,
					      cur_file_ptr->str_hash,
					      str,
					      &hash_ptr));

  ++vp->num_allocated;

  if (ECOFF_IS_STAB (&psym->ecoff_sym.asym))
    return psym;

  /* Save the symbol within the hash table if this is a static
     item, and it has a name.  */
  if (hash_ptr != (shash_t *) NULL
      && (type == st_Global || type == st_Static || type == st_Label
	  || type == st_Proc || type == st_StaticProc))
    hash_ptr->sym_ptr = psym;

  /* push or pop a scope if appropriate.  */
  switch (type)
    {
    default:
      break;

    case st_File:			/* beginning of file */
    case st_Proc:			/* procedure */
    case st_StaticProc:			/* static procedure */
    case st_Block:			/* begin scope */
      pscope = allocate_scope ();
      pscope->prev = cur_file_ptr->cur_scope;
      pscope->lsym = psym;
      pscope->type = type;
      cur_file_ptr->cur_scope = pscope;

      if (type != st_File)
	scope_delta = 1;

      /* For every block type except file, struct, union, or
         enumeration blocks, push a level on the tag stack.  We omit
         file types, so that tags can span file boundaries.  */
      if (type != st_File && storage != sc_Info)
	{
	  ptag_head = allocate_thead ();
	  ptag_head->first_tag = 0;
	  ptag_head->prev = cur_tag_head;
	  cur_tag_head = ptag_head;
	}
      break;

    case st_End:
      pscope = cur_file_ptr->cur_scope;
      if (pscope == (scope_t *) NULL)
	as_fatal (_("too many st_End's"));
      else
	{
	  st_t begin_type = (st_t) pscope->lsym->ecoff_sym.asym.st;

	  psym->begin_ptr = pscope->lsym;

	  if (begin_type != st_File)
	    scope_delta = -1;

	  /* Except for file, structure, union, or enumeration end
	     blocks remove all tags created within this scope.  */
	  if (begin_type != st_File && storage != sc_Info)
	    {
	      ptag_head = cur_tag_head;
	      cur_tag_head = ptag_head->prev;

	      for (ptag = ptag_head->first_tag;
		   ptag != (tag_t *) NULL;
		   ptag = ptag_next)
		{
		  if (ptag->forward_ref != (forward_t *) NULL)
		    add_unknown_tag (ptag);

		  ptag_next = ptag->same_block;
		  ptag->hash_ptr->tag_ptr = ptag->same_name;
		  free_tag (ptag);
		}

	      free_thead (ptag_head);
	    }

	  cur_file_ptr->cur_scope = pscope->prev;

	  /* block begin gets next sym #.  This is set when we know
	     the symbol index value.  */

	  /* Functions push two or more aux words as follows:
	     1st word: index+1 of the end symbol (filled in later).
	     2nd word: type of the function (plus any aux words needed).
	     Also, tie the external pointer back to the function begin symbol.  */
	  if (begin_type != st_File && begin_type != st_Block)
	    {
	      symint_t ty;
	      varray_t *svp = &cur_file_ptr->aux_syms;

	      pscope->lsym->ecoff_sym.asym.index = add_aux_sym_symint (0);
	      pscope->lsym->index_ptr =
		&svp->last->datum->aux[svp->objects_last_page - 1];
	      ty = add_aux_sym_tir (&last_func_type_info,
				    hash_no,
				    &cur_file_ptr->thash_head[0]);

/* This seems to be unnecessary.  I'm not even sure what it is
 * intended to do.  It's from mips-tfile.
 *	      if (last_func_sym_value != (symbolS *) NULL)
 *		{
 *		  last_func_sym_value->ifd = cur_file_ptr->file_index;
 *		  last_func_sym_value->index = ty;
 *		}
 */
	    }

	  free_scope (pscope);
	}
    }

  cur_file_ptr->nested_scopes += scope_delta;

#ifdef ECOFF_DEBUG
  if (debug && type != st_File
      && (debug > 2 || type == st_Block || type == st_End
	  || type == st_Proc || type == st_StaticProc))
    {
      char *sc_str = sc_to_string (storage);
      char *st_str = st_to_string (type);
      int depth = cur_file_ptr->nested_scopes + (scope_delta < 0);

      fprintf (stderr,
	       "\tlsym\tv= %10ld, depth= %2d, sc= %-12s",
	       value, depth, sc_str);

      if (str_start && str_end_p1 - str_start > 0)
	fprintf (stderr, " st= %-11s name= %.*s\n",
		 st_str, str_end_p1 - str_start, str_start);
      else
	{
	  unsigned long len = strlen (st_str);
	  fprintf (stderr, " st= %.*s\n", len - 1, st_str);
	}
    }
#endif

  return psym;
}

/* Add an auxiliary symbol (passing a symint).  This is actually used
   for integral aux types, not just symints.  */

static symint_t
add_aux_sym_symint (symint_t aux_word /* auxiliary information word */)
{
  register varray_t *vp;
  register aux_t *aux_ptr;

  if (cur_file_ptr == (efdr_t *) NULL)
    as_fatal (_("no current file pointer"));

  vp = &cur_file_ptr->aux_syms;

  if (vp->objects_last_page == vp->objects_per_page)
    add_varray_page (vp);

  aux_ptr = &vp->last->datum->aux[vp->objects_last_page++];
  aux_ptr->type = aux_isym;
  aux_ptr->data.isym = aux_word;

  return vp->num_allocated++;
}

/* Add an auxiliary symbol (passing a file/symbol index combo).  */

static symint_t
add_aux_sym_rndx (int file_index, symint_t sym_index)
{
  register varray_t *vp;
  register aux_t *aux_ptr;

  if (cur_file_ptr == (efdr_t *) NULL)
    as_fatal (_("no current file pointer"));

  vp = &cur_file_ptr->aux_syms;

  if (vp->objects_last_page == vp->objects_per_page)
    add_varray_page (vp);

  aux_ptr = &vp->last->datum->aux[vp->objects_last_page++];
  aux_ptr->type = aux_rndx;
  aux_ptr->data.rndx.rfd   = file_index;
  aux_ptr->data.rndx.index = sym_index;

  return vp->num_allocated++;
}

/* Add an auxiliary symbol (passing the basic type and possibly
   type qualifiers).  */

static symint_t
add_aux_sym_tir (type_info_t *t,	/* current type information */
		 hash_state_t state,	/* whether to hash type or not */
		 thash_t **hash_tbl	/* pointer to hash table to use */)
{
  register varray_t *vp;
  register aux_t *aux_ptr;
  static AUXU init_aux;
  symint_t ret;
  int i;
  AUXU aux;

  if (cur_file_ptr == (efdr_t *) NULL)
    as_fatal (_("no current file pointer"));

  vp = &cur_file_ptr->aux_syms;

  aux = init_aux;
  aux.ti.bt = (int) t->basic_type;
  aux.ti.continued = 0;
  aux.ti.fBitfield = t->bitfield;

  aux.ti.tq0 = (int) t->type_qualifiers[0];
  aux.ti.tq1 = (int) t->type_qualifiers[1];
  aux.ti.tq2 = (int) t->type_qualifiers[2];
  aux.ti.tq3 = (int) t->type_qualifiers[3];
  aux.ti.tq4 = (int) t->type_qualifiers[4];
  aux.ti.tq5 = (int) t->type_qualifiers[5];

  /* For anything that adds additional information, we must not hash,
     so check here, and reset our state.  */

  if (state != hash_no
      && (t->type_qualifiers[0] == tq_Array
	  || t->type_qualifiers[1] == tq_Array
	  || t->type_qualifiers[2] == tq_Array
	  || t->type_qualifiers[3] == tq_Array
	  || t->type_qualifiers[4] == tq_Array
	  || t->type_qualifiers[5] == tq_Array
	  || t->basic_type == bt_Struct
	  || t->basic_type == bt_Union
	  || t->basic_type == bt_Enum
	  || t->bitfield
	  || t->num_dims > 0))
    state = hash_no;

  /* See if we can hash this type, and save some space, but some types
     can't be hashed (because they contain arrays or continuations),
     and others can be put into the hash list, but cannot use existing
     types because other aux entries precede this one.  */

  if (state != hash_no)
    {
      register thash_t *hash_ptr;
      register symint_t hi;

      hi = aux.isym & ((1 << HASHBITS) - 1);
      hi %= THASH_SIZE;

      for (hash_ptr = hash_tbl[hi];
	   hash_ptr != (thash_t *)0;
	   hash_ptr = hash_ptr->next)
	{
	  if (aux.isym == hash_ptr->type.isym)
	    break;
	}

      if (hash_ptr != (thash_t *) NULL && state == hash_yes)
	return hash_ptr->indx;

      if (hash_ptr == (thash_t *) NULL)
	{
	  hash_ptr = allocate_thash ();
	  hash_ptr->next = hash_tbl[hi];
	  hash_ptr->type = aux;
	  hash_ptr->indx = vp->num_allocated;
	  hash_tbl[hi] = hash_ptr;
	}
    }

  /* Everything is set up, add the aux symbol.  */
  if (vp->objects_last_page == vp->objects_per_page)
    add_varray_page (vp);

  aux_ptr = &vp->last->datum->aux[vp->objects_last_page++];
  aux_ptr->type = aux_tir;
  aux_ptr->data = aux;

  ret = vp->num_allocated++;

  /* Add bitfield length if it exists.

     NOTE:  Mips documentation claims bitfield goes at the end of the
     AUX record, but the DECstation compiler emits it here.
     (This would only make a difference for enum bitfields.)

     Also note:  We use the last size given since gcc may emit 2
     for an enum bitfield.  */

  if (t->bitfield)
    (void) add_aux_sym_symint ((symint_t) t->sizes[t->num_sizes - 1]);

  /* Add tag information if needed.  Structure, union, and enum
     references add 2 aux symbols: a [file index, symbol index]
     pointer to the structure type, and the current file index.  */

  if (t->basic_type == bt_Struct
      || t->basic_type == bt_Union
      || t->basic_type == bt_Enum)
    {
      register symint_t file_index = t->tag_ptr->ifd;
      register localsym_t *sym = t->tag_ptr->sym;
      register forward_t *forward_ref = allocate_forward ();

      if (sym != (localsym_t *) NULL)
	{
	  forward_ref->next = sym->forward_ref;
	  sym->forward_ref = forward_ref;
	}
      else
	{
	  forward_ref->next = t->tag_ptr->forward_ref;
	  t->tag_ptr->forward_ref = forward_ref;
	}

      (void) add_aux_sym_rndx (ST_RFDESCAPE, indexNil);
      forward_ref->index_ptr
	= &vp->last->datum->aux[vp->objects_last_page - 1];

      (void) add_aux_sym_symint (file_index);
      forward_ref->ifd_ptr
	= &vp->last->datum->aux[vp->objects_last_page - 1];
    }

  /* Add information about array bounds if they exist.  */
  for (i = 0; i < t->num_dims; i++)
    {
      (void) add_aux_sym_rndx (ST_RFDESCAPE,
			       cur_file_ptr->int_type);

      (void) add_aux_sym_symint (cur_file_ptr->file_index);	/* file index*/
      (void) add_aux_sym_symint ((symint_t) 0);			/* low bound */
      (void) add_aux_sym_symint (t->dimensions[i] - 1);		/* high bound*/
      (void) add_aux_sym_symint ((t->dimensions[i] == 0)	/* stride */
				 ? 0
				 : (t->sizes[i] * 8) / t->dimensions[i]);
    };

  /* NOTE:  Mips documentation claims that the bitfield width goes here.
     But it needs to be emitted earlier.  */

  return ret;
}

/* Add a tag to the tag table (unless it already exists).  */

static tag_t *
get_tag (const char *tag,	/* tag name */
	 localsym_t *sym,	/* tag start block */
	 bt_t basic_type	/* bt_Struct, bt_Union, or bt_Enum */)
{
  shash_t *hash_ptr;
  const char *err;
  tag_t *tag_ptr;

  if (cur_file_ptr == (efdr_t *) NULL)
    as_fatal (_("no current file pointer"));

  hash_ptr = (shash_t *) hash_find (tag_hash, tag);

  if (hash_ptr != (shash_t *) NULL
      && hash_ptr->tag_ptr != (tag_t *) NULL)
    {
      tag_ptr = hash_ptr->tag_ptr;
      if (sym != (localsym_t *) NULL)
	{
	  tag_ptr->basic_type = basic_type;
	  tag_ptr->ifd        = cur_file_ptr->file_index;
	  tag_ptr->sym        = sym;
	}
      return tag_ptr;
    }

  if (hash_ptr == (shash_t *) NULL)
    {
      char *perm;

      perm = xstrdup (tag);
      hash_ptr = allocate_shash ();
      err = hash_insert (tag_hash, perm, (char *) hash_ptr);
      if (err)
	as_fatal (_("inserting \"%s\" into tag hash table: %s"),
		  tag, err);
      hash_ptr->string = perm;
    }

  tag_ptr = allocate_tag ();
  tag_ptr->forward_ref	= (forward_t *) NULL;
  tag_ptr->hash_ptr	= hash_ptr;
  tag_ptr->same_name	= hash_ptr->tag_ptr;
  tag_ptr->basic_type	= basic_type;
  tag_ptr->sym		= sym;
  tag_ptr->ifd		= ((sym == (localsym_t *) NULL)
			   ? (symint_t) -1
			   : cur_file_ptr->file_index);
  tag_ptr->same_block	= cur_tag_head->first_tag;

  cur_tag_head->first_tag = tag_ptr;
  hash_ptr->tag_ptr	  = tag_ptr;

  return tag_ptr;
}

/* Add an unknown {struct, union, enum} tag.  */

static void
add_unknown_tag (tag_t *ptag /* pointer to tag information */)
{
  shash_t *hash_ptr	= ptag->hash_ptr;
  char *name		= hash_ptr->string;
  localsym_t *sym;
  forward_t **pf;

#ifdef ECOFF_DEBUG
  if (debug > 1)
    {
      char *agg_type = "{unknown aggregate type}";
      switch (ptag->basic_type)
	{
	case bt_Struct:	agg_type = "struct";	break;
	case bt_Union:	agg_type = "union";	break;
	case bt_Enum:	agg_type = "enum";	break;
	default:				break;
	}

      fprintf (stderr, "unknown %s %.*s found\n", agg_type,
	       hash_ptr->len, name_start);
    }
#endif

  sym = add_ecoff_symbol (name,
			  st_Block,
			  sc_Info,
			  (symbolS *) NULL,
			  (bfd_vma) 0,
			  (symint_t) 0,
			  (symint_t) 0);

  (void) add_ecoff_symbol (name,
			   st_End,
			   sc_Info,
			   (symbolS *) NULL,
			   (bfd_vma) 0,
			   (symint_t) 0,
			   (symint_t) 0);

  for (pf = &sym->forward_ref; *pf != (forward_t *) NULL; pf = &(*pf)->next)
    ;
  *pf = ptag->forward_ref;
}

/* Add a procedure to the current file's list of procedures, and record
   this is the current procedure.  */

static void
add_procedure (char *func /* func name */)
{
  register varray_t *vp;
  register proc_t *new_proc_ptr;
  symbolS *sym;

#ifdef ECOFF_DEBUG
  if (debug)
    fputc ('\n', stderr);
#endif

  if (cur_file_ptr == (efdr_t *) NULL)
    as_fatal (_("no current file pointer"));

  vp = &cur_file_ptr->procs;

  if (vp->objects_last_page == vp->objects_per_page)
    add_varray_page (vp);

  cur_proc_ptr = new_proc_ptr = &vp->last->datum->proc[vp->objects_last_page++];

  if (first_proc_ptr == (proc_t *) NULL)
    first_proc_ptr = new_proc_ptr;

  vp->num_allocated++;

  new_proc_ptr->pdr.isym = -1;
  new_proc_ptr->pdr.iline = -1;
  new_proc_ptr->pdr.lnLow = -1;
  new_proc_ptr->pdr.lnHigh = -1;

  /* Set the BSF_FUNCTION flag for the symbol.  */
  sym = symbol_find_or_make (func);
  symbol_get_bfdsym (sym)->flags |= BSF_FUNCTION;

  /* Push the start of the function.  */
  new_proc_ptr->sym = add_ecoff_symbol ((const char *) NULL, st_Proc, sc_Text,
					sym, (bfd_vma) 0, (symint_t) 0,
					(symint_t) 0);

  ++proc_cnt;

  /* Fill in the linenos preceding the .ent, if any.  */
  if (noproc_lineno != (lineno_list_t *) NULL)
    {
      lineno_list_t *l;

      for (l = noproc_lineno; l != (lineno_list_t *) NULL; l = l->next)
	l->proc = new_proc_ptr;
      *last_lineno_ptr = noproc_lineno;
      while (*last_lineno_ptr != NULL)
	{
	  last_lineno = *last_lineno_ptr;
	  last_lineno_ptr = &last_lineno->next;
	}
      noproc_lineno = (lineno_list_t *) NULL;
    }
}

symbolS *
ecoff_get_cur_proc_sym (void)
{
  return (cur_proc_ptr ? cur_proc_ptr->sym->as_sym : NULL);
}

/* Add a new filename, and set up all of the file relative
   virtual arrays (strings, symbols, aux syms, etc.).  Record
   where the current file structure lives.  */

static void
add_file (const char *file_name, int indx ATTRIBUTE_UNUSED, int fake)
{
  register int first_ch;
  register efdr_t *fil_ptr;

#ifdef ECOFF_DEBUG
  if (debug)
    fprintf (stderr, "\tfile\t%.*s\n", len, file_start);
#endif

  /* If the file name is NULL, then no .file symbol appeared, and we
     want to use the actual file name.  */
  if (file_name == (const char *) NULL)
    {
      char *file;

      if (first_file != (efdr_t *) NULL)
	as_fatal (_("fake .file after real one"));
      as_where (&file, (unsigned int *) NULL);
      file_name = (const char *) file;

      /* Automatically generate ECOFF debugging information, since I
         think that's what other ECOFF assemblers do.  We don't do
         this if we see a .file directive with a string, since that
         implies that some sort of debugging information is being
         provided.  */
      if (! symbol_table_frozen && debug_type == DEBUG_UNSPECIFIED)
	debug_type = DEBUG_ECOFF;
    }
  else if (debug_type == DEBUG_UNSPECIFIED)
    debug_type = DEBUG_NONE;

#ifndef NO_LISTING
  if (listing)
    listing_source_file (file_name);
#endif

  current_stabs_filename = file_name;

  /* If we're creating stabs, then we don't actually make a new FDR.
     Instead, we just create a stabs symbol.  */
  if (stabs_seen)
    {
      (void) add_ecoff_symbol (file_name, st_Nil, sc_Nil,
			       symbol_new ("L0\001", now_seg,
					   (valueT) frag_now_fix (),
					   frag_now),
			       (bfd_vma) 0, 0, ECOFF_MARK_STAB (N_SOL));
      return;
    }

  first_ch = *file_name;

  /* FIXME: We can't safely merge files which have line number
     information (fMerge will be zero in this case).  Otherwise, we
     get incorrect line number debugging info.  See for instance
     ecoff_build_lineno, which will end up setting all file->fdr.*
     fields multiple times, resulting in incorrect debug info.  In
     order to make this work right, all line number and symbol info
     for the same source file has to be adjacent in the object file,
     so that a single file descriptor can be used to point to them.
     This would require maintaining file specific lists of line
     numbers and symbols for each file, so that they can be merged
     together (or output together) when two .file pseudo-ops are
     merged into one file descriptor.  */

  /* See if the file has already been created.  */
  for (fil_ptr = first_file;
       fil_ptr != (efdr_t *) NULL;
       fil_ptr = fil_ptr->next_file)
    {
      if (first_ch == fil_ptr->name[0]
	  && strcmp (file_name, fil_ptr->name) == 0
	  && fil_ptr->fdr.fMerge)
	{
	  cur_file_ptr = fil_ptr;
	  if (! fake)
	    cur_file_ptr->fake = 0;
	  break;
	}
    }

  /* If this is a new file, create it.  */
  if (fil_ptr == (efdr_t *) NULL)
    {
      if (file_desc.objects_last_page == file_desc.objects_per_page)
	add_varray_page (&file_desc);

      fil_ptr = cur_file_ptr =
	&file_desc.last->datum->file[file_desc.objects_last_page++];
      *fil_ptr = init_file;

      fil_ptr->file_index = current_file_idx++;
      ++file_desc.num_allocated;

      fil_ptr->fake = fake;

      /* Allocate the string hash table.  */
      fil_ptr->str_hash = hash_new ();

      /* Make sure 0 byte in string table is null  */
      add_string (&fil_ptr->strings,
		  fil_ptr->str_hash,
		  "",
		  (shash_t **)0);

      if (strlen (file_name) > PAGE_USIZE - 2)
	as_fatal (_("filename goes over one page boundary"));

      /* Push the start of the filename. We assume that the filename
         will be stored at string offset 1.  */
      (void) add_ecoff_symbol (file_name, st_File, sc_Text,
			       (symbolS *) NULL, (bfd_vma) 0,
			       (symint_t) 0, (symint_t) 0);
      fil_ptr->fdr.rss = 1;
      fil_ptr->name = &fil_ptr->strings.last->datum->byte[1];

      /* Update the linked list of file descriptors.  */
      *last_file_ptr = fil_ptr;
      last_file_ptr = &fil_ptr->next_file;

      /* Add void & int types to the file (void should be first to catch
         errant 0's within the index fields).  */
      fil_ptr->void_type = add_aux_sym_tir (&void_type_info,
					    hash_yes,
					    &cur_file_ptr->thash_head[0]);

      fil_ptr->int_type = add_aux_sym_tir (&int_type_info,
					   hash_yes,
					   &cur_file_ptr->thash_head[0]);
    }
}

/* This function is called when the assembler notices a preprocessor
   directive switching to a new file.  This will not happen in
   compiler output, only in hand coded assembler.  */

void
ecoff_new_file (const char *name, int appfile ATTRIBUTE_UNUSED)
{
  if (cur_file_ptr != NULL && strcmp (cur_file_ptr->name, name) == 0)
    return;
  add_file (name, 0, 0);

  /* This is a hand coded assembler file, so automatically turn on
     debugging information.  */
  if (debug_type == DEBUG_UNSPECIFIED)
    debug_type = DEBUG_ECOFF;
}

#ifdef ECOFF_DEBUG

/* Convert storage class to string.  */

static char *
sc_to_string (storage_class)
     sc_t storage_class;
{
  switch (storage_class)
    {
    case sc_Nil:	 return "Nil,";
    case sc_Text:	 return "Text,";
    case sc_Data:	 return "Data,";
    case sc_Bss:	 return "Bss,";
    case sc_Register:	 return "Register,";
    case sc_Abs:	 return "Abs,";
    case sc_Undefined:	 return "Undefined,";
    case sc_CdbLocal:	 return "CdbLocal,";
    case sc_Bits:	 return "Bits,";
    case sc_CdbSystem:	 return "CdbSystem,";
    case sc_RegImage:	 return "RegImage,";
    case sc_Info:	 return "Info,";
    case sc_UserStruct:	 return "UserStruct,";
    case sc_SData:	 return "SData,";
    case sc_SBss:	 return "SBss,";
    case sc_RData:	 return "RData,";
    case sc_Var:	 return "Var,";
    case sc_Common:	 return "Common,";
    case sc_SCommon:	 return "SCommon,";
    case sc_VarRegister: return "VarRegister,";
    case sc_Variant:	 return "Variant,";
    case sc_SUndefined:	 return "SUndefined,";
    case sc_Init:	 return "Init,";
    case sc_Max:	 return "Max,";
    }

  return "???,";
}

#endif /* DEBUG */

#ifdef ECOFF_DEBUG

/* Convert symbol type to string.  */

static char *
st_to_string (symbol_type)
     st_t symbol_type;
{
  switch (symbol_type)
    {
    case st_Nil:	return "Nil,";
    case st_Global:	return "Global,";
    case st_Static:	return "Static,";
    case st_Param:	return "Param,";
    case st_Local:	return "Local,";
    case st_Label:	return "Label,";
    case st_Proc:	return "Proc,";
    case st_Block:	return "Block,";
    case st_End:	return "End,";
    case st_Member:	return "Member,";
    case st_Typedef:	return "Typedef,";
    case st_File:	return "File,";
    case st_RegReloc:	return "RegReloc,";
    case st_Forward:	return "Forward,";
    case st_StaticProc:	return "StaticProc,";
    case st_Constant:	return "Constant,";
    case st_Str:	return "String,";
    case st_Number:	return "Number,";
    case st_Expr:	return "Expr,";
    case st_Type:	return "Type,";
    case st_Max:	return "Max,";
    }

  return "???,";
}

#endif /* DEBUG */

/* Parse .begin directives which have a label as the first argument
   which gives the location of the start of the block.  */

void
ecoff_directive_begin (int ignore ATTRIBUTE_UNUSED)
{
  char *name;
  char name_end;

  if (cur_file_ptr == (efdr_t *) NULL)
    {
      as_warn (_(".begin directive without a preceding .file directive"));
      demand_empty_rest_of_line ();
      return;
    }

  if (cur_proc_ptr == (proc_t *) NULL)
    {
      as_warn (_(".begin directive without a preceding .ent directive"));
      demand_empty_rest_of_line ();
      return;
    }

  name = input_line_pointer;
  name_end = get_symbol_end ();

  (void) add_ecoff_symbol ((const char *) NULL, st_Block, sc_Text,
			   symbol_find_or_make (name),
			   (bfd_vma) 0, (symint_t) 0, (symint_t) 0);

  *input_line_pointer = name_end;

  /* The line number follows, but we don't use it.  */
  (void) get_absolute_expression ();
  demand_empty_rest_of_line ();
}

/* Parse .bend directives which have a label as the first argument
   which gives the location of the end of the block.  */

void
ecoff_directive_bend (int ignore ATTRIBUTE_UNUSED)
{
  char *name;
  char name_end;
  symbolS *endsym;

  if (cur_file_ptr == (efdr_t *) NULL)
    {
      as_warn (_(".bend directive without a preceding .file directive"));
      demand_empty_rest_of_line ();
      return;
    }

  if (cur_proc_ptr == (proc_t *) NULL)
    {
      as_warn (_(".bend directive without a preceding .ent directive"));
      demand_empty_rest_of_line ();
      return;
    }

  name = input_line_pointer;
  name_end = get_symbol_end ();

  /* The value is the distance between the .bend directive and the
     corresponding symbol.  We fill in the offset when we write out
     the symbol.  */
  endsym = symbol_find (name);
  if (endsym == (symbolS *) NULL)
    as_warn (_(".bend directive names unknown symbol"));
  else
    (void) add_ecoff_symbol ((const char *) NULL, st_End, sc_Text, endsym,
			     (bfd_vma) 0, (symint_t) 0, (symint_t) 0);

  *input_line_pointer = name_end;

  /* The line number follows, but we don't use it.  */
  (void) get_absolute_expression ();
  demand_empty_rest_of_line ();
}

/* COFF debugging information is provided as a series of directives
   (.def, .scl, etc.).  We build up information as we read the
   directives in the following static variables, and file it away when
   we reach the .endef directive.  */
static char *coff_sym_name;
static type_info_t coff_type;
static sc_t coff_storage_class;
static st_t coff_symbol_typ;
static int coff_is_function;
static char *coff_tag;
static valueT coff_value;
static symbolS *coff_sym_value;
static bfd_vma coff_sym_addend;
static int coff_inside_enumeration;

/* Handle a .def directive: start defining a symbol.  */

void
ecoff_directive_def (int ignore ATTRIBUTE_UNUSED)
{
  char *name;
  char name_end;

  ecoff_debugging_seen = 1;

  SKIP_WHITESPACE ();

  name = input_line_pointer;
  name_end = get_symbol_end ();

  if (coff_sym_name != (char *) NULL)
    as_warn (_(".def pseudo-op used inside of .def/.endef; ignored"));
  else if (*name == '\0')
    as_warn (_("empty symbol name in .def; ignored"));
  else
    {
      if (coff_sym_name != (char *) NULL)
	free (coff_sym_name);
      if (coff_tag != (char *) NULL)
	free (coff_tag);
      
      coff_sym_name = xstrdup (name);
      coff_type = type_info_init;
      coff_storage_class = sc_Nil;
      coff_symbol_typ = st_Nil;
      coff_is_function = 0;
      coff_tag = (char *) NULL;
      coff_value = 0;
      coff_sym_value = (symbolS *) NULL;
      coff_sym_addend = 0;
    }

  *input_line_pointer = name_end;

  demand_empty_rest_of_line ();
}

/* Handle a .dim directive, used to give dimensions for an array.  The
   arguments are comma separated numbers.  mips-tfile assumes that
   there will not be more than 6 dimensions, and gdb won't read any
   more than that anyhow, so I will also make that assumption.  */

void
ecoff_directive_dim (int ignore ATTRIBUTE_UNUSED)
{
  int dimens[N_TQ];
  int i;

  if (coff_sym_name == (char *) NULL)
    {
      as_warn (_(".dim pseudo-op used outside of .def/.endef; ignored"));
      demand_empty_rest_of_line ();
      return;
    }

  for (i = 0; i < N_TQ; i++)
    {
      SKIP_WHITESPACE ();
      dimens[i] = get_absolute_expression ();
      if (*input_line_pointer == ',')
	++input_line_pointer;
      else
	{
	  if (*input_line_pointer != '\n'
	      && *input_line_pointer != ';')
	    as_warn (_("badly formed .dim directive"));
	  break;
	}
    }

  if (i == N_TQ)
    --i;

  /* The dimensions are stored away in reverse order.  */
  for (; i >= 0; i--)
    {
      if (coff_type.num_dims >= N_TQ)
	{
	  as_warn (_("too many .dim entries"));
	  break;
	}
      coff_type.dimensions[coff_type.num_dims] = dimens[i];
      ++coff_type.num_dims;
    }

  demand_empty_rest_of_line ();
}

/* Handle a .scl directive, which sets the COFF storage class of the
   symbol.  */

void
ecoff_directive_scl (int ignore ATTRIBUTE_UNUSED)
{
  long val;

  if (coff_sym_name == (char *) NULL)
    {
      as_warn (_(".scl pseudo-op used outside of .def/.endef; ignored"));
      demand_empty_rest_of_line ();
      return;
    }

  val = get_absolute_expression ();

  coff_symbol_typ = map_coff_sym_type[val];
  coff_storage_class = map_coff_storage[val];

  demand_empty_rest_of_line ();
}

/* Handle a .size directive.  For some reason mips-tfile.c thinks that
   .size can have multiple arguments.  We humor it, although gcc will
   never generate more than one argument.  */

void
ecoff_directive_size (int ignore ATTRIBUTE_UNUSED)
{
  int sizes[N_TQ];
  int i;

  if (coff_sym_name == (char *) NULL)
    {
      as_warn (_(".size pseudo-op used outside of .def/.endef; ignored"));
      demand_empty_rest_of_line ();
      return;
    }

  for (i = 0; i < N_TQ; i++)
    {
      SKIP_WHITESPACE ();
      sizes[i] = get_absolute_expression ();
      if (*input_line_pointer == ',')
	++input_line_pointer;
      else
	{
	  if (*input_line_pointer != '\n'
	      && *input_line_pointer != ';')
	    as_warn (_("badly formed .size directive"));
	  break;
	}
    }

  if (i == N_TQ)
    --i;

  /* The sizes are stored away in reverse order.  */
  for (; i >= 0; i--)
    {
      if (coff_type.num_sizes >= N_TQ)
	{
	  as_warn (_("too many .size entries"));
	  break;
	}
      coff_type.sizes[coff_type.num_sizes] = sizes[i];
      ++coff_type.num_sizes;
    }

  demand_empty_rest_of_line ();
}

/* Handle the .type directive, which gives the COFF type of the
   symbol.  */

void
ecoff_directive_type (int ignore ATTRIBUTE_UNUSED)
{
  long val;
  tq_t *tq_ptr;
  tq_t *tq_shft;

  if (coff_sym_name == (char *) NULL)
    {
      as_warn (_(".type pseudo-op used outside of .def/.endef; ignored"));
      demand_empty_rest_of_line ();
      return;
    }

  val = get_absolute_expression ();

  coff_type.orig_type = BTYPE (val);
  coff_type.basic_type = map_coff_types[coff_type.orig_type];

  tq_ptr = &coff_type.type_qualifiers[N_TQ];
  while (val & ~N_BTMASK)
    {
      if (tq_ptr == &coff_type.type_qualifiers[0])
	{
	  /* FIXME: We could handle this by setting the continued bit.
	     There would still be a limit: the .type argument can not
	     be infinite.  */
	  as_warn (_("the type of %s is too complex; it will be simplified"),
		   coff_sym_name);
	  break;
	}
      if (ISPTR (val))
	*--tq_ptr = tq_Ptr;
      else if (ISFCN (val))
	*--tq_ptr = tq_Proc;
      else if (ISARY (val))
	*--tq_ptr = tq_Array;
      else
	as_fatal (_("Unrecognized .type argument"));

      val = DECREF (val);
    }

  tq_shft = &coff_type.type_qualifiers[0];
  while (tq_ptr != &coff_type.type_qualifiers[N_TQ])
    *tq_shft++ = *tq_ptr++;

  if (tq_shft != &coff_type.type_qualifiers[0] && tq_shft[-1] == tq_Proc)
    {
      /* If this is a function, ignore it, so that we don't get two
         entries (one from the .ent, and one for the .def that
         precedes it).  Save the type information so that the end
         block can properly add it after the begin block index.  For
         MIPS knows what reason, we must strip off the function type
         at this point.  */
      coff_is_function = 1;
      tq_shft[-1] = tq_Nil;
    }

  while (tq_shft != &coff_type.type_qualifiers[N_TQ])
    *tq_shft++ = tq_Nil;

  demand_empty_rest_of_line ();
}

/* Handle the .tag directive, which gives the name of a structure,
   union or enum.  */

void
ecoff_directive_tag (int ignore ATTRIBUTE_UNUSED)
{
  char *name;
  char name_end;

  if (coff_sym_name == (char *) NULL)
    {
      as_warn (_(".tag pseudo-op used outside of .def/.endef; ignored"));
      demand_empty_rest_of_line ();
      return;
    }

  name = input_line_pointer;
  name_end = get_symbol_end ();

  coff_tag = xstrdup (name);

  *input_line_pointer = name_end;

  demand_empty_rest_of_line ();
}

/* Handle the .val directive, which gives the value of the symbol.  It
   may be the name of a static or global symbol.  */

void
ecoff_directive_val (int ignore ATTRIBUTE_UNUSED)
{
  expressionS exp;

  if (coff_sym_name == (char *) NULL)
    {
      as_warn (_(".val pseudo-op used outside of .def/.endef; ignored"));
      demand_empty_rest_of_line ();
      return;
    }

  expression (&exp);
  if (exp.X_op != O_constant && exp.X_op != O_symbol)
    {
      as_bad (_(".val expression is too complex"));
      demand_empty_rest_of_line ();
      return;
    }

  if (exp.X_op == O_constant)
    coff_value = exp.X_add_number;
  else
    {
      coff_sym_value = exp.X_add_symbol;
      coff_sym_addend = exp.X_add_number;
    }

  demand_empty_rest_of_line ();
}

/* Handle the .endef directive, which terminates processing of COFF
   debugging information for a symbol.  */

void
ecoff_directive_endef (int ignore ATTRIBUTE_UNUSED)
{
  char *name;
  symint_t indx;
  localsym_t *sym;

  demand_empty_rest_of_line ();

  if (coff_sym_name == (char *) NULL)
    {
      as_warn (_(".endef pseudo-op used before .def; ignored"));
      return;
    }

  name = coff_sym_name;
  coff_sym_name = (char *) NULL;

  /* If the symbol is a static or external, we have already gotten the
     appropriate type and class, so make sure we don't override those
     values.  This is needed because there are some type and classes
     that are not in COFF, such as short data, etc.  */
  if (coff_sym_value != (symbolS *) NULL)
    {
      coff_symbol_typ = st_Nil;
      coff_storage_class = sc_Nil;
    }

  coff_type.extra_sizes = coff_tag != (char *) NULL;
  if (coff_type.num_dims > 0)
    {
      int diff = coff_type.num_dims - coff_type.num_sizes;
      int i = coff_type.num_dims - 1;
      int j;

      if (coff_type.num_sizes != 1 || diff < 0)
	{
	  as_warn (_("bad COFF debugging information"));
	  return;
	}

      /* If this is an array, make sure the same number of dimensions
         and sizes were passed, creating extra sizes for multiply
         dimensioned arrays if not passed.  */
      coff_type.extra_sizes = 0;
      if (diff)
	{
	  j = (sizeof (coff_type.sizes) / sizeof (coff_type.sizes[0])) - 1;
	  while (j >= 0)
	    {
	      coff_type.sizes[j] = (((j - diff) >= 0)
				    ? coff_type.sizes[j - diff]
				    : 0);
	      j--;
	    }

	  coff_type.num_sizes = i + 1;
	  for (i--; i >= 0; i--)
	    coff_type.sizes[i] = (coff_type.dimensions[i + 1] == 0
				  ? 0
				  : (coff_type.sizes[i + 1]
				     / coff_type.dimensions[i + 1]));
	}
    }
  else if (coff_symbol_typ == st_Member
	   && coff_type.num_sizes - coff_type.extra_sizes == 1)
    {
      /* Is this a bitfield?  This is indicated by a structure member
         having a size field that isn't an array.  */
      coff_type.bitfield = 1;
    }

  /* Except for enumeration members & begin/ending of scopes, put the
     type word in the aux. symbol table.  */
  if (coff_symbol_typ == st_Block || coff_symbol_typ == st_End)
    indx = 0;
  else if (coff_inside_enumeration)
    indx = cur_file_ptr->void_type;
  else
    {
      if (coff_type.basic_type == bt_Struct
	  || coff_type.basic_type == bt_Union
	  || coff_type.basic_type == bt_Enum)
	{
	  if (coff_tag == (char *) NULL)
	    {
	      as_warn (_("no tag specified for %s"), name);
	      return;
	    }

	  coff_type.tag_ptr = get_tag (coff_tag, (localsym_t *) NULL,
				       coff_type.basic_type);
	}

      if (coff_is_function)
	{
	  last_func_type_info = coff_type;
	  last_func_sym_value = coff_sym_value;
	  return;
	}

      indx = add_aux_sym_tir (&coff_type,
			      hash_yes,
			      &cur_file_ptr->thash_head[0]);
    }

  /* Do any last minute adjustments that are necessary.  */
  switch (coff_symbol_typ)
    {
    default:
      break;

      /* For the beginning of structs, unions, and enumerations, the
         size info needs to be passed in the value field.  */
    case st_Block:
      if (coff_type.num_sizes - coff_type.num_dims - coff_type.extra_sizes
	  != 1)
	{
	  as_warn (_("bad COFF debugging information"));
	  return;
	}
      else
	coff_value = coff_type.sizes[0];

      coff_inside_enumeration = (coff_type.orig_type == T_ENUM);
      break;

      /* For the end of structs, unions, and enumerations, omit the
         name which is always ".eos".  This needs to be done last, so
         that any error reporting above gives the correct name.  */
    case st_End:
      free (name);
      name = (char *) NULL;
      coff_value = 0;
      coff_inside_enumeration = 0;
      break;

      /* Members of structures and unions that aren't bitfields, need
         to adjust the value from a byte offset to a bit offset.
         Members of enumerations do not have the value adjusted, and
         can be distinguished by indx == indexNil.  For enumerations,
         update the maximum enumeration value.  */
    case st_Member:
      if (! coff_type.bitfield && ! coff_inside_enumeration)
	coff_value *= 8;

      break;
    }

  /* Add the symbol.  */
  sym = add_ecoff_symbol (name,
			  coff_symbol_typ,
			  coff_storage_class,
			  coff_sym_value,
			  coff_sym_addend,
			  (symint_t) coff_value,
			  indx);

  /* deal with struct, union, and enum tags.  */
  if (coff_symbol_typ == st_Block)
    {
      /* Create or update the tag information.  */
      tag_t *tag_ptr = get_tag (name,
				sym,
				coff_type.basic_type);
      forward_t **pf;

      /* Remember any forward references.  */
      for (pf = &sym->forward_ref;
	   *pf != (forward_t *) NULL;
	   pf = &(*pf)->next)
	;
      *pf = tag_ptr->forward_ref;
      tag_ptr->forward_ref = (forward_t *) NULL;
    }
}

/* Parse .end directives.  */

void
ecoff_directive_end (int ignore ATTRIBUTE_UNUSED)
{
  char *name;
  char name_end;
  symbolS *ent;

  if (cur_file_ptr == (efdr_t *) NULL)
    {
      as_warn (_(".end directive without a preceding .file directive"));
      demand_empty_rest_of_line ();
      return;
    }

  if (cur_proc_ptr == (proc_t *) NULL)
    {
      as_warn (_(".end directive without a preceding .ent directive"));
      demand_empty_rest_of_line ();
      return;
    }

  name = input_line_pointer;
  name_end = get_symbol_end ();

  if (name == input_line_pointer)
    {
      as_warn (_(".end directive has no name"));
      *input_line_pointer = name_end;
      demand_empty_rest_of_line ();
      return;
    }

  /* The value is the distance between the .end directive and the
     corresponding symbol.  We create a fake symbol to hold the
     current location, and put in the offset when we write out the
     symbol.  */
  ent = symbol_find (name);
  if (ent == (symbolS *) NULL)
    as_warn (_(".end directive names unknown symbol"));
  else
    (void) add_ecoff_symbol ((const char *) NULL, st_End, sc_Text,
			     symbol_new ("L0\001", now_seg,
					 (valueT) frag_now_fix (),
					 frag_now),
			     (bfd_vma) 0, (symint_t) 0, (symint_t) 0);

  cur_proc_ptr = (proc_t *) NULL;

  *input_line_pointer = name_end;
  demand_empty_rest_of_line ();
}

/* Parse .ent directives.  */

void
ecoff_directive_ent (int ignore ATTRIBUTE_UNUSED)
{
  char *name;
  char name_end;

  if (cur_file_ptr == (efdr_t *) NULL)
    add_file ((const char *) NULL, 0, 1);

  if (cur_proc_ptr != (proc_t *) NULL)
    {
      as_warn (_("second .ent directive found before .end directive"));
      demand_empty_rest_of_line ();
      return;
    }

  name = input_line_pointer;
  name_end = get_symbol_end ();

  if (name == input_line_pointer)
    {
      as_warn (_(".ent directive has no name"));
      *input_line_pointer = name_end;
      demand_empty_rest_of_line ();
      return;
    }

  add_procedure (name);

  *input_line_pointer = name_end;

  /* The .ent directive is sometimes followed by a number.  I'm not
     really sure what the number means.  I don't see any way to store
     the information in the PDR.  The Irix 4 assembler seems to ignore
     the information.  */
  SKIP_WHITESPACE ();
  if (*input_line_pointer == ',')
    {
      ++input_line_pointer;
      SKIP_WHITESPACE ();
    }
  if (ISDIGIT (*input_line_pointer)
      || *input_line_pointer == '-')
    (void) get_absolute_expression ();

  demand_empty_rest_of_line ();
}

/* Parse .extern directives.  */

void
ecoff_directive_extern (int ignore ATTRIBUTE_UNUSED)
{
  char *name;
  int c;
  symbolS *symbolp;
  valueT size;

  name = input_line_pointer;
  c = get_symbol_end ();
  symbolp = symbol_find_or_make (name);
  *input_line_pointer = c;

  S_SET_EXTERNAL (symbolp);

  if (*input_line_pointer == ',')
    ++input_line_pointer;
  size = get_absolute_expression ();

  symbol_get_obj (symbolp)->ecoff_extern_size = size;
}

/* Parse .file directives.  */

void
ecoff_directive_file (int ignore ATTRIBUTE_UNUSED)
{
  int indx;
  char *name;
  int len;

  if (cur_proc_ptr != (proc_t *) NULL)
    {
      as_warn (_("no way to handle .file within .ent/.end section"));
      demand_empty_rest_of_line ();
      return;
    }

  indx = (int) get_absolute_expression ();

  /* FIXME: we don't have to save the name here.  */
  name = demand_copy_C_string (&len);

  add_file (name, indx - 1, 0);

  demand_empty_rest_of_line ();
}

/* Parse .fmask directives.  */

void
ecoff_directive_fmask (int ignore ATTRIBUTE_UNUSED)
{
  long val;

  if (cur_proc_ptr == (proc_t *) NULL)
    {
      as_warn (_(".fmask outside of .ent"));
      demand_empty_rest_of_line ();
      return;
    }

  if (get_absolute_expression_and_terminator (&val) != ',')
    {
      as_warn (_("bad .fmask directive"));
      --input_line_pointer;
      demand_empty_rest_of_line ();
      return;
    }

  cur_proc_ptr->pdr.fregmask = val;
  cur_proc_ptr->pdr.fregoffset = get_absolute_expression ();

  demand_empty_rest_of_line ();
}

/* Parse .frame directives.  */

void
ecoff_directive_frame (int ignore ATTRIBUTE_UNUSED)
{
  long val;

  if (cur_proc_ptr == (proc_t *) NULL)
    {
      as_warn (_(".frame outside of .ent"));
      demand_empty_rest_of_line ();
      return;
    }

  cur_proc_ptr->pdr.framereg = tc_get_register (1);

  SKIP_WHITESPACE ();
  if (*input_line_pointer++ != ','
      || get_absolute_expression_and_terminator (&val) != ',')
    {
      as_warn (_("bad .frame directive"));
      --input_line_pointer;
      demand_empty_rest_of_line ();
      return;
    }

  cur_proc_ptr->pdr.frameoffset = val;

  cur_proc_ptr->pdr.pcreg = tc_get_register (0);

  /* Alpha-OSF1 adds "the offset of saved $a0 from $sp", according to
     Sandro.  I don't yet know where this value should be stored, if
     anywhere.  Don't call demand_empty_rest_of_line ().  */
  s_ignore (42);
}

/* Parse .mask directives.  */

void
ecoff_directive_mask (int ignore ATTRIBUTE_UNUSED)
{
  long val;

  if (cur_proc_ptr == (proc_t *) NULL)
    {
      as_warn (_(".mask outside of .ent"));
      demand_empty_rest_of_line ();
      return;
    }

  if (get_absolute_expression_and_terminator (&val) != ',')
    {
      as_warn (_("bad .mask directive"));
      --input_line_pointer;
      demand_empty_rest_of_line ();
      return;
    }

  cur_proc_ptr->pdr.regmask = val;
  cur_proc_ptr->pdr.regoffset = get_absolute_expression ();

  demand_empty_rest_of_line ();
}

/* Parse .loc directives.  */

void
ecoff_directive_loc (int ignore ATTRIBUTE_UNUSED)
{
  lineno_list_t *list;
  symint_t lineno;

  if (cur_file_ptr == (efdr_t *) NULL)
    {
      as_warn (_(".loc before .file"));
      demand_empty_rest_of_line ();
      return;
    }

  if (now_seg != text_section)
    {
      as_warn (_(".loc outside of .text"));
      demand_empty_rest_of_line ();
      return;
    }

  /* Skip the file number.  */
  SKIP_WHITESPACE ();
  get_absolute_expression ();
  SKIP_WHITESPACE ();

  lineno = get_absolute_expression ();

#ifndef NO_LISTING
  if (listing)
    listing_source_line (lineno);
#endif

  /* If we're building stabs, then output a special label rather than
     ECOFF line number info.  */
  if (stabs_seen)
    {
      (void) add_ecoff_symbol ((char *) NULL, st_Label, sc_Text,
			       symbol_new ("L0\001", now_seg,
					   (valueT) frag_now_fix (),
					   frag_now),
			       (bfd_vma) 0, 0, lineno);
      return;
    }

  list = allocate_lineno_list ();

  list->next = (lineno_list_t *) NULL;
  list->file = cur_file_ptr;
  list->proc = cur_proc_ptr;
  list->frag = frag_now;
  list->paddr = frag_now_fix ();
  list->lineno = lineno;

  /* We don't want to merge files which have line numbers.  */
  cur_file_ptr->fdr.fMerge = 0;

  /* A .loc directive will sometimes appear before a .ent directive,
     which means that cur_proc_ptr will be NULL here.  Arrange to
     patch this up.  */
  if (cur_proc_ptr == (proc_t *) NULL)
    {
      lineno_list_t **pl;

      pl = &noproc_lineno;
      while (*pl != (lineno_list_t *) NULL)
	pl = &(*pl)->next;
      *pl = list;
    }
  else
    {
      last_lineno = list;
      *last_lineno_ptr = list;
      last_lineno_ptr = &list->next;
    }
}

/* The MIPS assembler sometimes inserts nop instructions in the
   instruction stream.  When this happens, we must patch up the .loc
   information so that it points to the instruction after the nop.  */

void
ecoff_fix_loc (fragS *old_frag, unsigned long old_frag_offset)
{
  if (last_lineno != NULL
      && last_lineno->frag == old_frag
      && last_lineno->paddr == old_frag_offset)
    {
      last_lineno->frag = frag_now;
      last_lineno->paddr = frag_now_fix ();
    }
}

/* Make sure the @stabs symbol is emitted.  */

static void
mark_stabs (int ignore ATTRIBUTE_UNUSED)
{
  if (! stabs_seen)
    {
      /* Add a dummy @stabs dymbol.  */
      stabs_seen = 1;
      (void) add_ecoff_symbol (stabs_symbol, stNil, scInfo,
			       (symbolS *) NULL,
			       (bfd_vma) 0, (symint_t) -1,
			       ECOFF_MARK_STAB (0));
    }
}

/* Parse .weakext directives.  */
#ifndef TC_MIPS
/* For TC_MIPS use the version in tc-mips.c.  */
void
ecoff_directive_weakext (int ignore ATTRIBUTE_UNUSED)
{
  char *name;
  int c;
  symbolS *symbolP;
  expressionS exp;

  name = input_line_pointer;
  c = get_symbol_end ();
  symbolP = symbol_find_or_make (name);
  *input_line_pointer = c;

  SKIP_WHITESPACE ();

  if (*input_line_pointer == ',')
    {
      if (S_IS_DEFINED (symbolP))
	{
	  as_bad (_("symbol `%s' is already defined"),
		  S_GET_NAME (symbolP));
	  ignore_rest_of_line ();
	  return;
	}

      ++input_line_pointer;
      SKIP_WHITESPACE ();
      if (! is_end_of_line[(unsigned char) *input_line_pointer])
	{
	  expression (&exp);
	  if (exp.X_op != O_symbol)
	    {
	      as_bad (_("bad .weakext directive"));
	      ignore_rest_of_line ();
	      return;
	    }
	  symbol_set_value_expression (symbolP, &exp);
	}
    }

  S_SET_WEAK (symbolP);

  demand_empty_rest_of_line ();
}
#endif /* not TC_MIPS */

/* Handle .stabs directives.  The actual parsing routine is done by a
   generic routine.  This routine is called via OBJ_PROCESS_STAB.
   When this is called, input_line_pointer will be pointing at the
   value field of the stab.

   .stabs directives have five fields:
	"string"	a string, encoding the type information.
	code		a numeric code, defined in <stab.h>
	0		a zero
	desc		a zero or line number
	value		a numeric value or an address.

    If the value is relocatable, we transform this into:
	iss		points as an index into string space
	value		value from lookup of the name
	st		st from lookup of the name
	sc		sc from lookup of the name
	index		code|CODE_MASK

    If the value is not relocatable, we transform this into:
	iss		points as an index into string space
	value		value
	st		st_Nil
	sc		sc_Nil
	index		code|CODE_MASK

    .stabn directives have four fields (string is null):
	code		a numeric code, defined in <stab.h>
	0		a zero
	desc		a zero or a line number
	value		a numeric value or an address.  */

void
ecoff_stab (segT sec ATTRIBUTE_UNUSED,
	    int what,
	    const char *string,
	    int type,
	    int other,
	    int desc)
{
  efdr_t *save_file_ptr = cur_file_ptr;
  symbolS *sym;
  symint_t value;
  bfd_vma addend;
  st_t st;
  sc_t sc;
  symint_t indx;
  localsym_t *hold = NULL;

  ecoff_debugging_seen = 1;

  /* We don't handle .stabd.  */
  if (what != 's' && what != 'n')
    {
      as_bad (_(".stab%c is not supported"), what);
      return;
    }

  /* A .stabn uses a null name, not an empty string.  */
  if (what == 'n')
    string = NULL;

  /* We ignore the other field.  */
  if (other != 0)
    as_warn (_(".stab%c: ignoring non-zero other field"), what);

  /* Make sure we have a current file.  */
  if (cur_file_ptr == (efdr_t *) NULL)
    {
      add_file ((const char *) NULL, 0, 1);
      save_file_ptr = cur_file_ptr;
    }

  /* For stabs in ECOFF, the first symbol must be @stabs.  This is a
     signal to gdb.  */
  if (stabs_seen == 0)
    mark_stabs (0);

  /* Line number stabs are handled differently, since they have two
     values, the line number and the address of the label.  We use the
     index field (aka desc) to hold the line number, and the value
     field to hold the address.  The symbol type is st_Label, which
     should be different from the other stabs, so that gdb can
     recognize it.  */
  if (type == N_SLINE)
    {
      SYMR dummy_symr;
      char *name;
      char name_end;

#ifndef NO_LISTING
      if (listing)
	listing_source_line ((unsigned int) desc);
#endif

      dummy_symr.index = desc;
      if (dummy_symr.index != desc)
	{
	  as_warn (_("line number (%d) for .stab%c directive cannot fit in index field (20 bits)"),
		   desc, what);
	  return;
	}

      name = input_line_pointer;
      name_end = get_symbol_end ();

      sym = symbol_find_or_make (name);
      *input_line_pointer = name_end;

      value = 0;
      addend = 0;
      st = st_Label;
      sc = sc_Text;
      indx = desc;
    }
  else
    {
#ifndef NO_LISTING
      if (listing && (type == N_SO || type == N_SOL))
	listing_source_file (string);
#endif

      if (ISDIGIT (*input_line_pointer)
	  || *input_line_pointer == '-'
	  || *input_line_pointer == '+')
	{
	  st = st_Nil;
	  sc = sc_Nil;
	  sym = (symbolS *) NULL;
	  value = get_absolute_expression ();
	  addend = 0;
	}
      else if (! is_name_beginner ((unsigned char) *input_line_pointer))
	{
	  as_warn (_("illegal .stab%c directive, bad character"), what);
	  return;
	}
      else
	{
	  expressionS exp;

	  sc = sc_Nil;
	  st = st_Nil;

	  expression (&exp);
	  if (exp.X_op == O_constant)
	    {
	      sym = NULL;
	      value = exp.X_add_number;
	      addend = 0;
	    }
	  else if (exp.X_op == O_symbol)
	    {
	      sym = exp.X_add_symbol;
	      value = 0;
	      addend = exp.X_add_number;
	    }
	  else
	    {
	      sym = make_expr_symbol (&exp);
	      value = 0;
	      addend = 0;
	    }
	}

      indx = ECOFF_MARK_STAB (type);
    }

  /* Don't store the stabs symbol we are creating as the type of the
     ECOFF symbol.  We want to compute the type of the ECOFF symbol
     independently.  */
  if (sym != (symbolS *) NULL)
    hold = symbol_get_obj (sym)->ecoff_symbol;

  (void) add_ecoff_symbol (string, st, sc, sym, addend, value, indx);

  if (sym != (symbolS *) NULL)
    symbol_get_obj (sym)->ecoff_symbol = hold;

  /* Restore normal file type.  */
  cur_file_ptr = save_file_ptr;
}

/* Frob an ECOFF symbol.  Small common symbols go into a special
   .scommon section rather than bfd_com_section.  */

void
ecoff_frob_symbol (symbolS *sym)
{
  if (S_IS_COMMON (sym)
      && S_GET_VALUE (sym) > 0
      && S_GET_VALUE (sym) <= bfd_get_gp_size (stdoutput))
    {
      static asection scom_section;
      static asymbol scom_symbol;

      /* We must construct a fake section similar to bfd_com_section
         but with the name .scommon.  */
      if (scom_section.name == NULL)
	{
	  scom_section = bfd_com_section;
	  scom_section.name = ".scommon";
	  scom_section.output_section = &scom_section;
	  scom_section.symbol = &scom_symbol;
	  scom_section.symbol_ptr_ptr = &scom_section.symbol;
	  scom_symbol = *bfd_com_section.symbol;
	  scom_symbol.name = ".scommon";
	  scom_symbol.section = &scom_section;
	}
      S_SET_SEGMENT (sym, &scom_section);
    }

  /* Double check weak symbols.  */
  if (S_IS_WEAK (sym))
    {
      if (S_IS_COMMON (sym))
	as_bad (_("symbol `%s' can not be both weak and common"),
		S_GET_NAME (sym));
    }
}

/* Add bytes to the symbolic information buffer.  */

static char *
ecoff_add_bytes (char **buf,
		 char **bufend,
		 char *bufptr,
		 unsigned long need)
{
  unsigned long at;
  unsigned long want;

  at = bufptr - *buf;
  need -= *bufend - bufptr;
  if (need < PAGE_SIZE)
    need = PAGE_SIZE;
  want = (*bufend - *buf) + need;
  *buf = xrealloc (*buf, want);
  *bufend = *buf + want;
  return *buf + at;
}

/* Adjust the symbolic information buffer to the alignment required
   for the ECOFF target debugging information.  */

static unsigned long
ecoff_padding_adjust (const struct ecoff_debug_swap *backend,
		      char **buf,
		      char **bufend,
		      unsigned long offset,
		      char **bufptrptr)
{
  bfd_size_type align;

  align = backend->debug_align;
  if ((offset & (align - 1)) != 0)
    {
      unsigned long add;

      add = align - (offset & (align - 1));
      if ((unsigned long) (*bufend - (*buf + offset)) < add)
	(void) ecoff_add_bytes (buf, bufend, *buf + offset, add);
      memset (*buf + offset, 0, add);
      offset += add;
      if (bufptrptr != (char **) NULL)
	*bufptrptr = *buf + offset;
    }

  return offset;
}

/* Build the line number information.  */

static unsigned long
ecoff_build_lineno (const struct ecoff_debug_swap *backend,
		    char **buf,
		    char **bufend,
		    unsigned long offset,
		    long *linecntptr)
{
  char *bufptr;
  register lineno_list_t *l;
  lineno_list_t *last;
  efdr_t *file;
  proc_t *proc;
  unsigned long c;
  long iline;
  long totcount;
  lineno_list_t first;
  lineno_list_t *local_first_lineno = first_lineno;

  if (linecntptr != (long *) NULL)
    *linecntptr = 0;

  bufptr = *buf + offset;

  file = (efdr_t *) NULL;
  proc = (proc_t *) NULL;
  last = (lineno_list_t *) NULL;
  c = offset;
  iline = 0;
  totcount = 0;

  /* FIXME?  Now that MIPS embedded-PIC is gone, it may be safe to
     remove this code.  */
  /* For some reason the address of the first procedure is ignored
     when reading line numbers.  This doesn't matter if the address of
     the first procedure is 0, but when gcc is generating MIPS
     embedded PIC code, it will put strings in the .text section
     before the first procedure.  We cope by inserting a dummy line if
     the address of the first procedure is not 0.  Hopefully this
     won't screw things up too badly.

     Don't do this for ECOFF assembly source line numbers.  They work
     without this extra attention.  */
  if (debug_type != DEBUG_ECOFF
      && first_proc_ptr != (proc_t *) NULL
      && local_first_lineno != (lineno_list_t *) NULL
      && ((S_GET_VALUE (first_proc_ptr->sym->as_sym)
	   + bfd_get_section_vma (stdoutput,
				  S_GET_SEGMENT (first_proc_ptr->sym->as_sym)))
	  != 0))
    {
      first.file = local_first_lineno->file;
      first.proc = local_first_lineno->proc;
      first.frag = &zero_address_frag;
      first.paddr = 0;
      first.lineno = 0;

      first.next = local_first_lineno;
      local_first_lineno = &first;
    }

  for (l = local_first_lineno; l != (lineno_list_t *) NULL; l = l->next)
    {
      long count;
      long delta;

      /* Get the offset to the memory address of the next line number
         (in words).  Do this first, so that we can skip ahead to the
         next useful line number entry.  */
      if (l->next == (lineno_list_t *) NULL)
	{
	  /* We want a count of zero, but it will be decremented
	     before it is used.  */
	  count = 1;
	}
      else if (l->next->frag->fr_address + l->next->paddr
	       > l->frag->fr_address + l->paddr)
	{
	  count = ((l->next->frag->fr_address + l->next->paddr
		    - (l->frag->fr_address + l->paddr))
		   >> 2);
	}
      else
	{
	  /* Don't change last, so we still get the right delta.  */
	  continue;
	}

      if (l->file != file || l->proc != proc)
	{
	  if (l->proc != proc && proc != (proc_t *) NULL)
	    proc->pdr.lnHigh = last->lineno;
	  if (l->file != file && file != (efdr_t *) NULL)
	    {
	      file->fdr.cbLine = c - file->fdr.cbLineOffset;
	      file->fdr.cline = totcount + count;
	      if (linecntptr != (long *) NULL)
		*linecntptr += totcount + count;
	      totcount = 0;
	    }

	  if (l->file != file)
	    {
	      efdr_t *last_file = file;

	      file = l->file;
	      if (last_file != (efdr_t *) NULL)
		file->fdr.ilineBase
		  = last_file->fdr.ilineBase + last_file->fdr.cline;
	      else
		file->fdr.ilineBase = 0;
	      file->fdr.cbLineOffset = c;
	    }
	  if (l->proc != proc)
	    {
	      proc = l->proc;
	      if (proc != (proc_t *) NULL)
		{
		  proc->pdr.lnLow = l->lineno;
		  proc->pdr.cbLineOffset = c - file->fdr.cbLineOffset;
		  proc->pdr.iline = totcount;
		}
	    }

	  last = (lineno_list_t *) NULL;
	}

      totcount += count;

      /* Get the offset to this line number.  */
      if (last == (lineno_list_t *) NULL)
	delta = 0;
      else
	delta = l->lineno - last->lineno;

      /* Put in the offset to this line number.  */
      while (delta != 0)
	{
	  int setcount;

	  /* 1 is added to each count read.  */
	  --count;
	  /* We can only adjust the word count by up to 15 words at a
	     time.  */
	  if (count <= 0x0f)
	    {
	      setcount = count;
	      count = 0;
	    }
	  else
	    {
	      setcount = 0x0f;
	      count -= 0x0f;
	    }
	  if (delta >= -7 && delta <= 7)
	    {
	      if (bufptr >= *bufend)
		bufptr = ecoff_add_bytes (buf, bufend, bufptr, (long) 1);
	      *bufptr++ = setcount + (delta << 4);
	      delta = 0;
	      ++c;
	    }
	  else
	    {
	      int set;

	      if (*bufend - bufptr < 3)
		bufptr = ecoff_add_bytes (buf, bufend, bufptr, (long) 3);
	      *bufptr++ = setcount + (8 << 4);
	      if (delta < -0x8000)
		{
		  set = -0x8000;
		  delta += 0x8000;
		}
	      else if (delta > 0x7fff)
		{
		  set = 0x7fff;
		  delta -= 0x7fff;
		}
	      else
		{
		  set = delta;
		  delta = 0;
		}
	      *bufptr++ = set >> 8;
	      *bufptr++ = set & 0xffff;
	      c += 3;
	    }
	}

      /* Finish adjusting the count.  */
      while (count > 0)
	{
	  if (bufptr >= *bufend)
	    bufptr = ecoff_add_bytes (buf, bufend, bufptr, (long) 1);
	  /* 1 is added to each count read.  */
	  --count;
	  if (count > 0x0f)
	    {
	      *bufptr++ = 0x0f;
	      count -= 0x0f;
	    }
	  else
	    {
	      *bufptr++ = count;
	      count = 0;
	    }
	  ++c;
	}

      ++iline;
      last = l;
    }

  if (proc != (proc_t *) NULL)
    proc->pdr.lnHigh = last->lineno;
  if (file != (efdr_t *) NULL)
    {
      file->fdr.cbLine = c - file->fdr.cbLineOffset;
      file->fdr.cline = totcount;
    }

  if (linecntptr != (long *) NULL)
    *linecntptr += totcount;

  c = ecoff_padding_adjust (backend, buf, bufend, c, &bufptr);

  return c;
}

/* Build and swap out the symbols.  */

static unsigned long
ecoff_build_symbols (const struct ecoff_debug_swap *backend,
		     char **buf,
		     char **bufend,
		     unsigned long offset)
{
  const bfd_size_type external_sym_size = backend->external_sym_size;
  void (* const swap_sym_out) (bfd *, const SYMR *, PTR)
    = backend->swap_sym_out;
  char *sym_out;
  long isym;
  vlinks_t *file_link;

  sym_out = *buf + offset;

  isym = 0;

  /* The symbols are stored by file.  */
  for (file_link = file_desc.first;
       file_link != (vlinks_t *) NULL;
       file_link = file_link->next)
    {
      int ifilesym;
      int fil_cnt;
      efdr_t *fil_ptr;
      efdr_t *fil_end;

      if (file_link->next == (vlinks_t *) NULL)
	fil_cnt = file_desc.objects_last_page;
      else
	fil_cnt = file_desc.objects_per_page;
      fil_ptr = file_link->datum->file;
      fil_end = fil_ptr + fil_cnt;
      for (; fil_ptr < fil_end; fil_ptr++)
	{
	  vlinks_t *sym_link;

	  fil_ptr->fdr.isymBase = isym;
	  ifilesym = isym;
	  for (sym_link = fil_ptr->symbols.first;
	       sym_link != (vlinks_t *) NULL;
	       sym_link = sym_link->next)
	    {
	      int sym_cnt;
	      localsym_t *sym_ptr;
	      localsym_t *sym_end;

	      if (sym_link->next == (vlinks_t *) NULL)
		sym_cnt = fil_ptr->symbols.objects_last_page;
	      else
		sym_cnt = fil_ptr->symbols.objects_per_page;
	      sym_ptr = sym_link->datum->sym;
	      sym_end = sym_ptr + sym_cnt;
	      for (; sym_ptr < sym_end; sym_ptr++)
		{
		  int local;
		  symbolS *as_sym;
		  forward_t *f;

		  know (sym_ptr->file_ptr == fil_ptr);

		  /* If there is no associated gas symbol, then this
		     is a pure debugging symbol.  We have already
		     added the name (if any) to fil_ptr->strings.
		     Otherwise we must decide whether this is an
		     external or a local symbol (actually, it may be
		     both if the local provides additional debugging
		     information for the external).  */
		  local = 1;
		  as_sym = sym_ptr->as_sym;
		  if (as_sym != (symbolS *) NULL)
		    {
		      symint_t indx;

		      /* The value of a block start symbol is the
		         offset from the start of the procedure.  For
		         other symbols we just use the gas value (but
		         we must offset it by the vma of the section,
		         just as BFD does, because BFD will not see
		         this value).  */
		      if (sym_ptr->ecoff_sym.asym.st == (int) st_Block
			  && sym_ptr->ecoff_sym.asym.sc == (int) sc_Text)
			{
			  symbolS *begin_sym;

			  know (sym_ptr->proc_ptr != (proc_t *) NULL);
			  begin_sym = sym_ptr->proc_ptr->sym->as_sym;
			  if (S_GET_SEGMENT (as_sym)
			      != S_GET_SEGMENT (begin_sym))
			    as_warn (_(".begin/.bend in different segments"));
			  sym_ptr->ecoff_sym.asym.value =
			    S_GET_VALUE (as_sym) - S_GET_VALUE (begin_sym);
			}
		      else
			sym_ptr->ecoff_sym.asym.value =
			  (S_GET_VALUE (as_sym)
			   + bfd_get_section_vma (stdoutput,
						  S_GET_SEGMENT (as_sym))
			   + sym_ptr->addend);

		      sym_ptr->ecoff_sym.weakext = S_IS_WEAK (as_sym);

		      /* Set st_Proc to st_StaticProc for local
			 functions.  */
		      if (sym_ptr->ecoff_sym.asym.st == st_Proc
			  && S_IS_DEFINED (as_sym)
			  && ! S_IS_EXTERNAL (as_sym)
			  && ! S_IS_WEAK (as_sym))
			sym_ptr->ecoff_sym.asym.st = st_StaticProc;

		      /* Get the type and storage class based on where
		         the symbol actually wound up.  Traditionally,
		         N_LBRAC and N_RBRAC are *not* relocated.  */
		      indx = sym_ptr->ecoff_sym.asym.index;
		      if (sym_ptr->ecoff_sym.asym.st == st_Nil
			  && sym_ptr->ecoff_sym.asym.sc == sc_Nil
			  && (! ECOFF_IS_STAB (&sym_ptr->ecoff_sym.asym)
			      || ((ECOFF_UNMARK_STAB (indx) != N_LBRAC)
				  && (ECOFF_UNMARK_STAB (indx) != N_RBRAC))))
			{
			  segT seg;
			  const char *segname;
			  st_t st;
			  sc_t sc;

			  seg = S_GET_SEGMENT (as_sym);
			  segname = segment_name (seg);

			  if (! ECOFF_IS_STAB (&sym_ptr->ecoff_sym.asym)
			      && (S_IS_EXTERNAL (as_sym)
				  || S_IS_WEAK (as_sym)
				  || ! S_IS_DEFINED (as_sym)))
			    {
			      if ((symbol_get_bfdsym (as_sym)->flags
				   & BSF_FUNCTION) != 0)
				st = st_Proc;
			      else
				st = st_Global;
			    }
			  else if (seg == text_section)
			    st = st_Label;
			  else
			    st = st_Static;

			  if (! S_IS_DEFINED (as_sym))
			    {
			      valueT s;

			      s = symbol_get_obj (as_sym)->ecoff_extern_size;
			      if (s == 0
				  || s > bfd_get_gp_size (stdoutput))
				sc = sc_Undefined;
			      else
				{
				  sc = sc_SUndefined;
				  sym_ptr->ecoff_sym.asym.value = s;
				}
#ifdef S_SET_SIZE
			      S_SET_SIZE (as_sym, s);
#endif
			    }
			  else if (S_IS_COMMON (as_sym))
			    {
			      if (S_GET_VALUE (as_sym) > 0
				  && (S_GET_VALUE (as_sym)
				      <= bfd_get_gp_size (stdoutput)))
				sc = sc_SCommon;
			      else
				sc = sc_Common;
			    }
			  else if (seg == text_section)
			    sc = sc_Text;
			  else if (seg == data_section)
			    sc = sc_Data;
			  else if (strcmp (segname, ".rdata") == 0
				   || strcmp (segname, ".rodata") == 0)
			    sc = sc_RData;
			  else if (strcmp (segname, ".sdata") == 0)
			    sc = sc_SData;
			  else if (seg == bss_section)
			    sc = sc_Bss;
			  else if (strcmp (segname, ".sbss") == 0)
			    sc = sc_SBss;
			  else if (seg == &bfd_abs_section)
			    sc = sc_Abs;
			  else
			    {
			      /* This must be a user named section.
			         This is not possible in ECOFF, but it
			         is in ELF.  */
			      sc = sc_Data;
			    }

			  sym_ptr->ecoff_sym.asym.st = (int) st;
			  sym_ptr->ecoff_sym.asym.sc = (int) sc;
			}

		      /* This is just an external symbol if it is
		         outside a procedure and it has a type.
		         FIXME: g++ will generate symbols which have
		         different names in the debugging information
		         than the actual symbol.  Should we handle
		         them here?  */
		      if ((S_IS_EXTERNAL (as_sym)
			   || S_IS_WEAK (as_sym)
			   || ! S_IS_DEFINED (as_sym))
			  && sym_ptr->proc_ptr == (proc_t *) NULL
			  && sym_ptr->ecoff_sym.asym.st != (int) st_Nil
			  && ! ECOFF_IS_STAB (&sym_ptr->ecoff_sym.asym))
			local = 0;

		      /* This is just an external symbol if it is a
		         common symbol.  */
		      if (S_IS_COMMON (as_sym))
			local = 0;

		      /* If an st_end symbol has an associated gas
		         symbol, then it is a local label created for
		         a .bend or .end directive.  Stabs line
		         numbers will have \001 in the names.  */
		      if (local
			  && sym_ptr->ecoff_sym.asym.st != st_End
			  && strchr (sym_ptr->name, '\001') == 0)
			sym_ptr->ecoff_sym.asym.iss =
			  add_string (&fil_ptr->strings,
				      fil_ptr->str_hash,
				      sym_ptr->name,
				      (shash_t **) NULL);
		    }

		  /* We now know the index of this symbol; fill in
		     locations that have been waiting for that
		     information.  */
		  if (sym_ptr->begin_ptr != (localsym_t *) NULL)
		    {
		      localsym_t *begin_ptr;
		      st_t begin_type;

		      know (local);
		      begin_ptr = sym_ptr->begin_ptr;
		      know (begin_ptr->sym_index != -1);
		      sym_ptr->ecoff_sym.asym.index = begin_ptr->sym_index;
		      if (sym_ptr->ecoff_sym.asym.sc != (int) sc_Info)
			sym_ptr->ecoff_sym.asym.iss =
			  begin_ptr->ecoff_sym.asym.iss;

		      begin_type = begin_ptr->ecoff_sym.asym.st;
		      if (begin_type == st_File
			  || begin_type == st_Block)
			{
			  begin_ptr->ecoff_sym.asym.index =
			    isym - ifilesym + 1;
			  (*swap_sym_out) (stdoutput,
					   &begin_ptr->ecoff_sym.asym,
					   (*buf
					    + offset
					    + (begin_ptr->sym_index
					       * external_sym_size)));
			}
		      else
			{
			  know (begin_ptr->index_ptr != (aux_t *) NULL);
			  begin_ptr->index_ptr->data.isym =
			    isym - ifilesym + 1;
			}

		      /* The value of the symbol marking the end of a
		         procedure is the size of the procedure.  The
		         value of the symbol marking the end of a
		         block is the offset from the start of the
		         procedure to the block.  */
		      if (begin_type == st_Proc
			  || begin_type == st_StaticProc)
			{
			  know (as_sym != (symbolS *) NULL);
			  know (begin_ptr->as_sym != (symbolS *) NULL);
			  if (S_GET_SEGMENT (as_sym)
			      != S_GET_SEGMENT (begin_ptr->as_sym))
			    as_warn (_(".begin/.bend in different segments"));
			  sym_ptr->ecoff_sym.asym.value =
			    (S_GET_VALUE (as_sym)
			     - S_GET_VALUE (begin_ptr->as_sym));

			  /* If the size is odd, this is probably a
			     mips16 function; force it to be even.  */
			  if ((sym_ptr->ecoff_sym.asym.value & 1) != 0)
			    ++sym_ptr->ecoff_sym.asym.value;

#ifdef S_SET_SIZE
			  S_SET_SIZE (begin_ptr->as_sym,
				      sym_ptr->ecoff_sym.asym.value);
#endif
			}
		      else if (begin_type == st_Block
			       && sym_ptr->ecoff_sym.asym.sc != (int) sc_Info)
			{
			  symbolS *begin_sym;

			  know (as_sym != (symbolS *) NULL);
			  know (sym_ptr->proc_ptr != (proc_t *) NULL);
			  begin_sym = sym_ptr->proc_ptr->sym->as_sym;
			  if (S_GET_SEGMENT (as_sym)
			      != S_GET_SEGMENT (begin_sym))
			    as_warn (_(".begin/.bend in different segments"));
			  sym_ptr->ecoff_sym.asym.value =
			    S_GET_VALUE (as_sym) - S_GET_VALUE (begin_sym);
			}
		    }

		  for (f = sym_ptr->forward_ref;
		       f != (forward_t *) NULL;
		       f = f->next)
		    {
		      know (local);
		      f->ifd_ptr->data.isym = fil_ptr->file_index;
		      f->index_ptr->data.rndx.index = isym - ifilesym;
		    }

		  if (local)
		    {
		      if ((bfd_size_type)(*bufend - sym_out) < external_sym_size)
			sym_out = ecoff_add_bytes (buf, bufend,
						   sym_out,
						   external_sym_size);
		      (*swap_sym_out) (stdoutput, &sym_ptr->ecoff_sym.asym,
				       sym_out);
		      sym_out += external_sym_size;

		      sym_ptr->sym_index = isym;

		      if (sym_ptr->proc_ptr != (proc_t *) NULL
			  && sym_ptr->proc_ptr->sym == sym_ptr)
			sym_ptr->proc_ptr->pdr.isym = isym - ifilesym;

		      ++isym;
		    }

		  /* Record the local symbol index and file number in
		     case this is an external symbol.  Note that this
		     destroys the asym.index field.  */
		  if (as_sym != (symbolS *) NULL
		      && symbol_get_obj (as_sym)->ecoff_symbol == sym_ptr)
		    {
		      if ((sym_ptr->ecoff_sym.asym.st == st_Proc
			   || sym_ptr->ecoff_sym.asym.st == st_StaticProc)
			  && local)
			sym_ptr->ecoff_sym.asym.index = isym - ifilesym - 1;
		      sym_ptr->ecoff_sym.ifd = fil_ptr->file_index;

		      /* Don't try to merge an FDR which has an
		         external symbol attached to it.  */
		      if (S_IS_EXTERNAL (as_sym) || S_IS_WEAK (as_sym))
			fil_ptr->fdr.fMerge = 0;
		    }
		}
	    }
	  fil_ptr->fdr.csym = isym - fil_ptr->fdr.isymBase;
	}
    }

  return offset + isym * external_sym_size;
}

/* Swap out the procedure information.  */

static unsigned long
ecoff_build_procs (const struct ecoff_debug_swap *backend,
		   char **buf,
		   char **bufend,
		   unsigned long offset)
{
  const bfd_size_type external_pdr_size = backend->external_pdr_size;
  void (* const swap_pdr_out) (bfd *, const PDR *, PTR)
    = backend->swap_pdr_out;
  char *pdr_out;
  long iproc;
  vlinks_t *file_link;

  pdr_out = *buf + offset;

  iproc = 0;

  /* The procedures are stored by file.  */
  for (file_link = file_desc.first;
       file_link != (vlinks_t *) NULL;
       file_link = file_link->next)
    {
      int fil_cnt;
      efdr_t *fil_ptr;
      efdr_t *fil_end;

      if (file_link->next == (vlinks_t *) NULL)
	fil_cnt = file_desc.objects_last_page;
      else
	fil_cnt = file_desc.objects_per_page;
      fil_ptr = file_link->datum->file;
      fil_end = fil_ptr + fil_cnt;
      for (; fil_ptr < fil_end; fil_ptr++)
	{
	  vlinks_t *proc_link;
	  int first;

	  fil_ptr->fdr.ipdFirst = iproc;
	  first = 1;
	  for (proc_link = fil_ptr->procs.first;
	       proc_link != (vlinks_t *) NULL;
	       proc_link = proc_link->next)
	    {
	      int prc_cnt;
	      proc_t *proc_ptr;
	      proc_t *proc_end;

	      if (proc_link->next == (vlinks_t *) NULL)
		prc_cnt = fil_ptr->procs.objects_last_page;
	      else
		prc_cnt = fil_ptr->procs.objects_per_page;
	      proc_ptr = proc_link->datum->proc;
	      proc_end = proc_ptr + prc_cnt;
	      for (; proc_ptr < proc_end; proc_ptr++)
		{
		  symbolS *adr_sym;
		  unsigned long adr;

		  adr_sym = proc_ptr->sym->as_sym;
		  adr = (S_GET_VALUE (adr_sym)
			 + bfd_get_section_vma (stdoutput,
						S_GET_SEGMENT (adr_sym)));
		  if (first)
		    {
		      /* This code used to force the adr of the very
		         first fdr to be 0.  However, the native tools
		         don't do that, and I can't remember why it
		         used to work that way, so I took it out.  */
		      fil_ptr->fdr.adr = adr;
		      first = 0;
		    }
		  proc_ptr->pdr.adr = adr - fil_ptr->fdr.adr;
		  if ((bfd_size_type)(*bufend - pdr_out) < external_pdr_size)
		    pdr_out = ecoff_add_bytes (buf, bufend,
					       pdr_out,
					       external_pdr_size);
		  (*swap_pdr_out) (stdoutput, &proc_ptr->pdr, pdr_out);
		  pdr_out += external_pdr_size;
		  ++iproc;
		}
	    }
	  fil_ptr->fdr.cpd = iproc - fil_ptr->fdr.ipdFirst;
	}
    }

  return offset + iproc * external_pdr_size;
}

/* Swap out the aux information.  */

static unsigned long
ecoff_build_aux (const struct ecoff_debug_swap *backend,
		 char **buf,
		 char **bufend,
		 unsigned long offset)
{
  int bigendian;
  union aux_ext *aux_out;
  long iaux;
  vlinks_t *file_link;

  bigendian = bfd_big_endian (stdoutput);

  aux_out = (union aux_ext *) (*buf + offset);

  iaux = 0;

  /* The aux entries are stored by file.  */
  for (file_link = file_desc.first;
       file_link != (vlinks_t *) NULL;
       file_link = file_link->next)
    {
      int fil_cnt;
      efdr_t *fil_ptr;
      efdr_t *fil_end;

      if (file_link->next == (vlinks_t *) NULL)
	fil_cnt = file_desc.objects_last_page;
      else
	fil_cnt = file_desc.objects_per_page;
      fil_ptr = file_link->datum->file;
      fil_end = fil_ptr + fil_cnt;
      for (; fil_ptr < fil_end; fil_ptr++)
	{
	  vlinks_t *aux_link;

	  fil_ptr->fdr.fBigendian = bigendian;
	  fil_ptr->fdr.iauxBase = iaux;
	  for (aux_link = fil_ptr->aux_syms.first;
	       aux_link != (vlinks_t *) NULL;
	       aux_link = aux_link->next)
	    {
	      int aux_cnt;
	      aux_t *aux_ptr;
	      aux_t *aux_end;

	      if (aux_link->next == (vlinks_t *) NULL)
		aux_cnt = fil_ptr->aux_syms.objects_last_page;
	      else
		aux_cnt = fil_ptr->aux_syms.objects_per_page;
	      aux_ptr = aux_link->datum->aux;
	      aux_end = aux_ptr + aux_cnt;
	      for (; aux_ptr < aux_end; aux_ptr++)
		{
		  if ((unsigned long) (*bufend - (char *) aux_out)
		      < sizeof (union aux_ext))
		    aux_out = ((union aux_ext *)
			       ecoff_add_bytes (buf, bufend,
						(char *) aux_out,
						sizeof (union aux_ext)));
		  switch (aux_ptr->type)
		    {
		    case aux_tir:
		      (*backend->swap_tir_out) (bigendian,
						&aux_ptr->data.ti,
						&aux_out->a_ti);
		      break;
		    case aux_rndx:
		      (*backend->swap_rndx_out) (bigendian,
						 &aux_ptr->data.rndx,
						 &aux_out->a_rndx);
		      break;
		    case aux_dnLow:
		      AUX_PUT_DNLOW (bigendian, aux_ptr->data.dnLow,
				     aux_out);
		      break;
		    case aux_dnHigh:
		      AUX_PUT_DNHIGH (bigendian, aux_ptr->data.dnHigh,
				      aux_out);
		      break;
		    case aux_isym:
		      AUX_PUT_ISYM (bigendian, aux_ptr->data.isym,
				    aux_out);
		      break;
		    case aux_iss:
		      AUX_PUT_ISS (bigendian, aux_ptr->data.iss,
				   aux_out);
		      break;
		    case aux_width:
		      AUX_PUT_WIDTH (bigendian, aux_ptr->data.width,
				     aux_out);
		      break;
		    case aux_count:
		      AUX_PUT_COUNT (bigendian, aux_ptr->data.count,
				     aux_out);
		      break;
		    }

		  ++aux_out;
		  ++iaux;
		}
	    }
	  fil_ptr->fdr.caux = iaux - fil_ptr->fdr.iauxBase;
	}
    }

  return ecoff_padding_adjust (backend, buf, bufend,
			       offset + iaux * sizeof (union aux_ext),
			       (char **) NULL);
}

/* Copy out the strings from a varray_t.  This returns the number of
   bytes copied, rather than the new offset.  */

static unsigned long
ecoff_build_strings (char **buf,
		     char **bufend,
		     unsigned long offset,
		     varray_t *vp)
{
  unsigned long istr;
  char *str_out;
  vlinks_t *str_link;

  str_out = *buf + offset;

  istr = 0;

  for (str_link = vp->first;
       str_link != (vlinks_t *) NULL;
       str_link = str_link->next)
    {
      unsigned long str_cnt;

      if (str_link->next == (vlinks_t *) NULL)
	str_cnt = vp->objects_last_page;
      else
	str_cnt = vp->objects_per_page;

      if ((unsigned long)(*bufend - str_out) < str_cnt)
	str_out = ecoff_add_bytes (buf, bufend, str_out, str_cnt);

      memcpy (str_out, str_link->datum->byte, str_cnt);
      str_out += str_cnt;
      istr += str_cnt;
    }

  return istr;
}

/* Dump out the local strings.  */

static unsigned long
ecoff_build_ss (const struct ecoff_debug_swap *backend,
		char **buf,
		char **bufend,
		unsigned long offset)
{
  long iss;
  vlinks_t *file_link;

  iss = 0;

  for (file_link = file_desc.first;
       file_link != (vlinks_t *) NULL;
       file_link = file_link->next)
    {
      int fil_cnt;
      efdr_t *fil_ptr;
      efdr_t *fil_end;

      if (file_link->next == (vlinks_t *) NULL)
	fil_cnt = file_desc.objects_last_page;
      else
	fil_cnt = file_desc.objects_per_page;
      fil_ptr = file_link->datum->file;
      fil_end = fil_ptr + fil_cnt;
      for (; fil_ptr < fil_end; fil_ptr++)
	{
	  long ss_cnt;

	  fil_ptr->fdr.issBase = iss;
	  ss_cnt = ecoff_build_strings (buf, bufend, offset + iss,
					&fil_ptr->strings);
	  fil_ptr->fdr.cbSs = ss_cnt;
	  iss += ss_cnt;
	}
    }

  return ecoff_padding_adjust (backend, buf, bufend, offset + iss,
			       (char **) NULL);
}

/* Swap out the file descriptors.  */

static unsigned long
ecoff_build_fdr (const struct ecoff_debug_swap *backend,
		 char **buf,
		 char **bufend,
		 unsigned long offset)
{
  const bfd_size_type external_fdr_size = backend->external_fdr_size;
  void (* const swap_fdr_out) (bfd *, const FDR *, PTR)
    = backend->swap_fdr_out;
  long ifile;
  char *fdr_out;
  vlinks_t *file_link;

  ifile = 0;

  fdr_out = *buf + offset;

  for (file_link = file_desc.first;
       file_link != (vlinks_t *) NULL;
       file_link = file_link->next)
    {
      int fil_cnt;
      efdr_t *fil_ptr;
      efdr_t *fil_end;

      if (file_link->next == (vlinks_t *) NULL)
	fil_cnt = file_desc.objects_last_page;
      else
	fil_cnt = file_desc.objects_per_page;
      fil_ptr = file_link->datum->file;
      fil_end = fil_ptr + fil_cnt;
      for (; fil_ptr < fil_end; fil_ptr++)
	{
	  if ((bfd_size_type)(*bufend - fdr_out) < external_fdr_size)
	    fdr_out = ecoff_add_bytes (buf, bufend, fdr_out,
				       external_fdr_size);
	  (*swap_fdr_out) (stdoutput, &fil_ptr->fdr, fdr_out);
	  fdr_out += external_fdr_size;
	  ++ifile;
	}
    }

  return offset + ifile * external_fdr_size;
}

/* Set up the external symbols.  These are supposed to be handled by
   the backend.  This routine just gets the right information and
   calls a backend function to deal with it.  */

static void
ecoff_setup_ext (void)
{
  register symbolS *sym;

  for (sym = symbol_rootP; sym != (symbolS *) NULL; sym = symbol_next (sym))
    {
      if (symbol_get_obj (sym)->ecoff_symbol == NULL)
	continue;

      /* If this is a local symbol, then force the fields to zero.  */
      if (! S_IS_EXTERNAL (sym)
	  && ! S_IS_WEAK (sym)
	  && S_IS_DEFINED (sym))
	{
	  struct localsym *lsym;

	  lsym = symbol_get_obj (sym)->ecoff_symbol;
	  lsym->ecoff_sym.asym.value = 0;
	  lsym->ecoff_sym.asym.st = (int) st_Nil;
	  lsym->ecoff_sym.asym.sc = (int) sc_Nil;
	  lsym->ecoff_sym.asym.index = indexNil;
	}

      obj_ecoff_set_ext (sym, &symbol_get_obj (sym)->ecoff_symbol->ecoff_sym);
    }
}

/* Build the ECOFF debugging information.  */

unsigned long
ecoff_build_debug (HDRR *hdr,
		   char **bufp,
		   const struct ecoff_debug_swap *backend)
{
  const bfd_size_type external_pdr_size = backend->external_pdr_size;
  tag_t *ptag;
  tag_t *ptag_next;
  efdr_t *fil_ptr;
  int end_warning;
  efdr_t *hold_file_ptr;
  proc_t *hold_proc_ptr;
  symbolS *sym;
  char *buf;
  char *bufend;
  unsigned long offset;

  /* Make sure we have a file.  */
  if (first_file == (efdr_t *) NULL)
    add_file ((const char *) NULL, 0, 1);

  /* Handle any top level tags.  */
  for (ptag = top_tag_head->first_tag;
       ptag != (tag_t *) NULL;
       ptag = ptag_next)
    {
      if (ptag->forward_ref != (forward_t *) NULL)
	add_unknown_tag (ptag);

      ptag_next = ptag->same_block;
      ptag->hash_ptr->tag_ptr = ptag->same_name;
      free_tag (ptag);
    }

  free_thead (top_tag_head);

  /* Look through the symbols.  Add debugging information for each
     symbol that has not already received it.  */
  hold_file_ptr = cur_file_ptr;
  hold_proc_ptr = cur_proc_ptr;
  cur_proc_ptr = (proc_t *) NULL;
  for (sym = symbol_rootP; sym != (symbolS *) NULL; sym = symbol_next (sym))
    {
      if (symbol_get_obj (sym)->ecoff_symbol != NULL
	  || symbol_get_obj (sym)->ecoff_file == (efdr_t *) NULL
	  || (symbol_get_bfdsym (sym)->flags & BSF_SECTION_SYM) != 0)
	continue;

      cur_file_ptr = symbol_get_obj (sym)->ecoff_file;
      add_ecoff_symbol ((const char *) NULL, st_Nil, sc_Nil, sym,
			(bfd_vma) 0, S_GET_VALUE (sym), indexNil);
    }
  cur_proc_ptr = hold_proc_ptr;
  cur_file_ptr = hold_file_ptr;

  /* Output an ending symbol for all the files.  We have to do this
     here for the last file, so we may as well do it for all of the
     files.  */
  end_warning = 0;
  for (fil_ptr = first_file;
       fil_ptr != (efdr_t *) NULL;
       fil_ptr = fil_ptr->next_file)
    {
      cur_file_ptr = fil_ptr;
      while (cur_file_ptr->cur_scope != (scope_t *) NULL
	     && cur_file_ptr->cur_scope->prev != (scope_t *) NULL)
	{
	  cur_file_ptr->cur_scope = cur_file_ptr->cur_scope->prev;
	  if (! end_warning && ! cur_file_ptr->fake)
	    {
	      as_warn (_("missing .end or .bend at end of file"));
	      end_warning = 1;
	    }
	}
      if (cur_file_ptr->cur_scope != (scope_t *) NULL)
	(void) add_ecoff_symbol ((const char *) NULL,
				 st_End, sc_Text,
				 (symbolS *) NULL,
				 (bfd_vma) 0,
				 (symint_t) 0,
				 (symint_t) 0);
    }

  /* Build the symbolic information.  */
  offset = 0;
  buf = xmalloc (PAGE_SIZE);
  bufend = buf + PAGE_SIZE;

  /* Build the line number information.  */
  hdr->cbLineOffset = offset;
  offset = ecoff_build_lineno (backend, &buf, &bufend, offset,
			       &hdr->ilineMax);
  hdr->cbLine = offset - hdr->cbLineOffset;

  /* We don't use dense numbers at all.  */
  hdr->idnMax = 0;
  hdr->cbDnOffset = 0;

  /* We can't build the PDR table until we have built the symbols,
     because a PDR contains a symbol index.  However, we set aside
     space at this point.  */
  hdr->ipdMax = proc_cnt;
  hdr->cbPdOffset = offset;
  if ((bfd_size_type)(bufend - (buf + offset)) < proc_cnt * external_pdr_size)
    (void) ecoff_add_bytes (&buf, &bufend, buf + offset,
			    proc_cnt * external_pdr_size);
  offset += proc_cnt * external_pdr_size;

  /* Build the local symbols.  */
  hdr->cbSymOffset = offset;
  offset = ecoff_build_symbols (backend, &buf, &bufend, offset);
  hdr->isymMax = (offset - hdr->cbSymOffset) / backend->external_sym_size;

  /* Building the symbols initializes the symbol index in the PDR's.
     Now we can swap out the PDR's.  */
  (void) ecoff_build_procs (backend, &buf, &bufend, hdr->cbPdOffset);

  /* We don't use optimization symbols.  */
  hdr->ioptMax = 0;
  hdr->cbOptOffset = 0;

  /* Swap out the auxiliary type information.  */
  hdr->cbAuxOffset = offset;
  offset = ecoff_build_aux (backend, &buf, &bufend, offset);
  hdr->iauxMax = (offset - hdr->cbAuxOffset) / sizeof (union aux_ext);

  /* Copy out the local strings.  */
  hdr->cbSsOffset = offset;
  offset = ecoff_build_ss (backend, &buf, &bufend, offset);
  hdr->issMax = offset - hdr->cbSsOffset;

  /* We don't use relative file descriptors.  */
  hdr->crfd = 0;
  hdr->cbRfdOffset = 0;

  /* Swap out the file descriptors.  */
  hdr->cbFdOffset = offset;
  offset = ecoff_build_fdr (backend, &buf, &bufend, offset);
  hdr->ifdMax = (offset - hdr->cbFdOffset) / backend->external_fdr_size;

  /* Set up the external symbols, which are handled by the BFD back
     end.  */
  hdr->issExtMax = 0;
  hdr->cbSsExtOffset = 0;
  hdr->iextMax = 0;
  hdr->cbExtOffset = 0;
  ecoff_setup_ext ();

  know ((offset & (backend->debug_align - 1)) == 0);

  /* FIXME: This value should be determined from the .verstamp directive,
     with reasonable defaults in config files.  */
#ifdef TC_ALPHA
  hdr->vstamp = 0x030b;
#else
  hdr->vstamp = 0x020b;
#endif

  *bufp = buf;
  return offset;
}

/* Allocate a cluster of pages.  */

#ifndef MALLOC_CHECK

static page_type *
allocate_cluster (unsigned long npages)
{
  register page_type *value = (page_type *) xmalloc (npages * PAGE_USIZE);

#ifdef ECOFF_DEBUG
  if (debug > 3)
    fprintf (stderr, "\talloc\tnpages = %d, value = 0x%.8x\n", npages, value);
#endif

  memset (value, 0, npages * PAGE_USIZE);

  return value;
}

static page_type *cluster_ptr = NULL;
static unsigned long pages_left = 0;

#endif /* MALLOC_CHECK */

/* Allocate one page (which is initialized to 0).  */

static page_type *
allocate_page (void)
{
#ifndef MALLOC_CHECK

  if (pages_left == 0)
    {
      pages_left = MAX_CLUSTER_PAGES;
      cluster_ptr = allocate_cluster (pages_left);
    }

  pages_left--;
  return cluster_ptr++;

#else /* MALLOC_CHECK */

  page_type *ptr;

  ptr = xmalloc (PAGE_USIZE);
  memset (ptr, 0, PAGE_USIZE);
  return ptr;

#endif /* MALLOC_CHECK */
}

/* Allocate scoping information.  */

static scope_t *
allocate_scope (void)
{
  register scope_t *ptr;
  static scope_t initial_scope;

#ifndef MALLOC_CHECK

  ptr = alloc_counts[(int) alloc_type_scope].free_list.f_scope;
  if (ptr != (scope_t *) NULL)
    alloc_counts[(int) alloc_type_scope].free_list.f_scope = ptr->free;
  else
    {
      register int unallocated	= alloc_counts[(int) alloc_type_scope].unallocated;
      register page_type *cur_page	= alloc_counts[(int) alloc_type_scope].cur_page;

      if (unallocated == 0)
	{
	  unallocated = PAGE_SIZE / sizeof (scope_t);
	  alloc_counts[(int) alloc_type_scope].cur_page = cur_page = allocate_page ();
	  alloc_counts[(int) alloc_type_scope].total_pages++;
	}

      ptr = &cur_page->scope[--unallocated];
      alloc_counts[(int) alloc_type_scope].unallocated = unallocated;
    }

#else

  ptr = (scope_t *) xmalloc (sizeof (scope_t));

#endif

  alloc_counts[(int) alloc_type_scope].total_alloc++;
  *ptr = initial_scope;
  return ptr;
}

/* Free scoping information.  */

static void
free_scope (scope_t *ptr)
{
  alloc_counts[(int) alloc_type_scope].total_free++;

#ifndef MALLOC_CHECK
  ptr->free = alloc_counts[(int) alloc_type_scope].free_list.f_scope;
  alloc_counts[(int) alloc_type_scope].free_list.f_scope = ptr;
#else
  free ((PTR) ptr);
#endif
}

/* Allocate links for pages in a virtual array.  */

static vlinks_t *
allocate_vlinks (void)
{
  register vlinks_t *ptr;
  static vlinks_t initial_vlinks;

#ifndef MALLOC_CHECK

  register int unallocated = alloc_counts[(int) alloc_type_vlinks].unallocated;
  register page_type *cur_page = alloc_counts[(int) alloc_type_vlinks].cur_page;

  if (unallocated == 0)
    {
      unallocated = PAGE_SIZE / sizeof (vlinks_t);
      alloc_counts[(int) alloc_type_vlinks].cur_page = cur_page = allocate_page ();
      alloc_counts[(int) alloc_type_vlinks].total_pages++;
    }

  ptr = &cur_page->vlinks[--unallocated];
  alloc_counts[(int) alloc_type_vlinks].unallocated = unallocated;

#else

  ptr = (vlinks_t *) xmalloc (sizeof (vlinks_t));

#endif

  alloc_counts[(int) alloc_type_vlinks].total_alloc++;
  *ptr = initial_vlinks;
  return ptr;
}

/* Allocate string hash buckets.  */

static shash_t *
allocate_shash (void)
{
  register shash_t *ptr;
  static shash_t initial_shash;

#ifndef MALLOC_CHECK

  register int unallocated = alloc_counts[(int) alloc_type_shash].unallocated;
  register page_type *cur_page = alloc_counts[(int) alloc_type_shash].cur_page;

  if (unallocated == 0)
    {
      unallocated = PAGE_SIZE / sizeof (shash_t);
      alloc_counts[(int) alloc_type_shash].cur_page = cur_page = allocate_page ();
      alloc_counts[(int) alloc_type_shash].total_pages++;
    }

  ptr = &cur_page->shash[--unallocated];
  alloc_counts[(int) alloc_type_shash].unallocated = unallocated;

#else

  ptr = (shash_t *) xmalloc (sizeof (shash_t));

#endif

  alloc_counts[(int) alloc_type_shash].total_alloc++;
  *ptr = initial_shash;
  return ptr;
}

/* Allocate type hash buckets.  */

static thash_t *
allocate_thash (void)
{
  register thash_t *ptr;
  static thash_t initial_thash;

#ifndef MALLOC_CHECK

  register int unallocated = alloc_counts[(int) alloc_type_thash].unallocated;
  register page_type *cur_page = alloc_counts[(int) alloc_type_thash].cur_page;

  if (unallocated == 0)
    {
      unallocated = PAGE_SIZE / sizeof (thash_t);
      alloc_counts[(int) alloc_type_thash].cur_page = cur_page = allocate_page ();
      alloc_counts[(int) alloc_type_thash].total_pages++;
    }

  ptr = &cur_page->thash[--unallocated];
  alloc_counts[(int) alloc_type_thash].unallocated = unallocated;

#else

  ptr = (thash_t *) xmalloc (sizeof (thash_t));

#endif

  alloc_counts[(int) alloc_type_thash].total_alloc++;
  *ptr = initial_thash;
  return ptr;
}

/* Allocate structure, union, or enum tag information.  */

static tag_t *
allocate_tag (void)
{
  register tag_t *ptr;
  static tag_t initial_tag;

#ifndef MALLOC_CHECK

  ptr = alloc_counts[(int) alloc_type_tag].free_list.f_tag;
  if (ptr != (tag_t *) NULL)
    alloc_counts[(int) alloc_type_tag].free_list.f_tag = ptr->free;
  else
    {
      register int unallocated = alloc_counts[(int) alloc_type_tag].unallocated;
      register page_type *cur_page = alloc_counts[(int) alloc_type_tag].cur_page;

      if (unallocated == 0)
	{
	  unallocated = PAGE_SIZE / sizeof (tag_t);
	  alloc_counts[(int) alloc_type_tag].cur_page = cur_page = allocate_page ();
	  alloc_counts[(int) alloc_type_tag].total_pages++;
	}

      ptr = &cur_page->tag[--unallocated];
      alloc_counts[(int) alloc_type_tag].unallocated = unallocated;
    }

#else

  ptr = (tag_t *) xmalloc (sizeof (tag_t));

#endif

  alloc_counts[(int) alloc_type_tag].total_alloc++;
  *ptr = initial_tag;
  return ptr;
}

/* Free scoping information.  */

static void
free_tag (tag_t *ptr)
{
  alloc_counts[(int) alloc_type_tag].total_free++;

#ifndef MALLOC_CHECK
  ptr->free = alloc_counts[(int) alloc_type_tag].free_list.f_tag;
  alloc_counts[(int) alloc_type_tag].free_list.f_tag = ptr;
#else
  free ((PTR_T) ptr);
#endif
}

/* Allocate forward reference to a yet unknown tag.  */

static forward_t *
allocate_forward (void)
{
  register forward_t *ptr;
  static forward_t initial_forward;

#ifndef MALLOC_CHECK

  register int unallocated = alloc_counts[(int) alloc_type_forward].unallocated;
  register page_type *cur_page = alloc_counts[(int) alloc_type_forward].cur_page;

  if (unallocated == 0)
    {
      unallocated = PAGE_SIZE / sizeof (forward_t);
      alloc_counts[(int) alloc_type_forward].cur_page = cur_page = allocate_page ();
      alloc_counts[(int) alloc_type_forward].total_pages++;
    }

  ptr = &cur_page->forward[--unallocated];
  alloc_counts[(int) alloc_type_forward].unallocated = unallocated;

#else

  ptr = (forward_t *) xmalloc (sizeof (forward_t));

#endif

  alloc_counts[(int) alloc_type_forward].total_alloc++;
  *ptr = initial_forward;
  return ptr;
}

/* Allocate head of type hash list.  */

static thead_t *
allocate_thead (void)
{
  register thead_t *ptr;
  static thead_t initial_thead;

#ifndef MALLOC_CHECK

  ptr = alloc_counts[(int) alloc_type_thead].free_list.f_thead;
  if (ptr != (thead_t *) NULL)
    alloc_counts[(int) alloc_type_thead].free_list.f_thead = ptr->free;
  else
    {
      register int unallocated = alloc_counts[(int) alloc_type_thead].unallocated;
      register page_type *cur_page = alloc_counts[(int) alloc_type_thead].cur_page;

      if (unallocated == 0)
	{
	  unallocated = PAGE_SIZE / sizeof (thead_t);
	  alloc_counts[(int) alloc_type_thead].cur_page = cur_page = allocate_page ();
	  alloc_counts[(int) alloc_type_thead].total_pages++;
	}

      ptr = &cur_page->thead[--unallocated];
      alloc_counts[(int) alloc_type_thead].unallocated = unallocated;
    }

#else

  ptr = (thead_t *) xmalloc (sizeof (thead_t));

#endif

  alloc_counts[(int) alloc_type_thead].total_alloc++;
  *ptr = initial_thead;
  return ptr;
}

/* Free scoping information.  */

static void
free_thead (thead_t *ptr)
{
  alloc_counts[(int) alloc_type_thead].total_free++;

#ifndef MALLOC_CHECK
  ptr->free = (thead_t *) alloc_counts[(int) alloc_type_thead].free_list.f_thead;
  alloc_counts[(int) alloc_type_thead].free_list.f_thead = ptr;
#else
  free ((PTR_T) ptr);
#endif
}

static lineno_list_t *
allocate_lineno_list (void)
{
  register lineno_list_t *ptr;
  static lineno_list_t initial_lineno_list;

#ifndef MALLOC_CHECK

  register int unallocated = alloc_counts[(int) alloc_type_lineno].unallocated;
  register page_type *cur_page = alloc_counts[(int) alloc_type_lineno].cur_page;

  if (unallocated == 0)
    {
      unallocated = PAGE_SIZE / sizeof (lineno_list_t);
      alloc_counts[(int) alloc_type_lineno].cur_page = cur_page = allocate_page ();
      alloc_counts[(int) alloc_type_lineno].total_pages++;
    }

  ptr = &cur_page->lineno[--unallocated];
  alloc_counts[(int) alloc_type_lineno].unallocated = unallocated;

#else

  ptr = (lineno_list_t *) xmalloc (sizeof (lineno_list_t));

#endif

  alloc_counts[(int) alloc_type_lineno].total_alloc++;
  *ptr = initial_lineno_list;
  return ptr;
}

void
ecoff_set_gp_prolog_size (int sz)
{
  if (cur_proc_ptr == 0)
    return;

  cur_proc_ptr->pdr.gp_prologue = sz;
  if (cur_proc_ptr->pdr.gp_prologue != sz)
    {
      as_warn (_("GP prologue size exceeds field size, using 0 instead"));
      cur_proc_ptr->pdr.gp_prologue = 0;
    }

  cur_proc_ptr->pdr.gp_used = 1;
}

int
ecoff_no_current_file (void)
{
  return cur_file_ptr == (efdr_t *) NULL;
}

void
ecoff_generate_asm_lineno (void)
{
  unsigned int lineno;
  char *filename;
  lineno_list_t *list;

  as_where (&filename, &lineno);

  if (current_stabs_filename == (char *) NULL
      || strcmp (current_stabs_filename, filename))
    add_file (filename, 0, 1);

  list = allocate_lineno_list ();

  list->next = (lineno_list_t *) NULL;
  list->file = cur_file_ptr;
  list->proc = cur_proc_ptr;
  list->frag = frag_now;
  list->paddr = frag_now_fix ();
  list->lineno = lineno;

  /* We don't want to merge files which have line numbers.  */
  cur_file_ptr->fdr.fMerge = 0;

  /* A .loc directive will sometimes appear before a .ent directive,
     which means that cur_proc_ptr will be NULL here.  Arrange to
     patch this up.  */
  if (cur_proc_ptr == (proc_t *) NULL)
    {
      lineno_list_t **pl;

      pl = &noproc_lineno;
      while (*pl != (lineno_list_t *) NULL)
	pl = &(*pl)->next;
      *pl = list;
    }
  else
    {
      last_lineno = list;
      *last_lineno_ptr = list;
      last_lineno_ptr = &list->next;
    }
}

#else

void
ecoff_generate_asm_lineno (void)
{
}

#endif /* ECOFF_DEBUGGING */
