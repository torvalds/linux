/* Declarations of constants for internal format of MIPS ECOFF symbols.
   Originally contributed by MIPS Computer Systems and Third Eye Software.
   Changes contributed by Cygnus Support are in the public domain.

   This file is just aggregated with the files that make up the GNU
   release; it is not considered part of GAS, GDB, or other GNU
   programs.  */

/*
 * |-----------------------------------------------------------|
 * | Copyright (c) 1992, 1991, 1990 MIPS Computer Systems, Inc.|
 * | MIPS Computer Systems, Inc. grants reproduction and use   |
 * | rights to all parties, PROVIDED that this comment is      |
 * | maintained in the copy.                                   |
 * |-----------------------------------------------------------|
 */

/* (C) Copyright 1984 by Third Eye Software, Inc.
 *
 * Third Eye Software, Inc. grants reproduction and use rights to
 * all parties, PROVIDED that this comment is maintained in the copy.
 *
 * Third Eye makes no claims about the applicability of this
 * symbol table to a particular use.
 */

/* glevels for field in FDR */
#define GLEVEL_0	2
#define GLEVEL_1	1
#define GLEVEL_2	0	/* for upward compat reasons. */
#define GLEVEL_3	3

/* magic number fo symheader */
#define magicSym	0x7009
/* The Alpha uses this value instead, for some reason.  */
#define magicSym2	0x1992

/* Language codes */
#define langC		0	
#define langPascal	1
#define langFortran	2
#define	langAssembler	3	/* one Assembley inst might map to many mach */
#define langMachine	4
#define langNil		5
#define langAda		6
#define langPl1		7
#define langCobol	8
#define langStdc	9	/* FIXME: Collides with SGI langCplusplus */
#define langCplusplus	9	/* FIXME: Collides with langStdc */
#define langCplusplusV2	10	/* SGI addition */
#define langMax		11	/* maximum allowed 32 -- 5 bits */

/* The following are value definitions for the fields in the SYMR */

/*
 * Storage Classes
 */

#define scNil		0
#define scText		1	/* text symbol */
#define scData		2	/* initialized data symbol */
#define scBss		3	/* un-initialized data symbol */
#define scRegister	4	/* value of symbol is register number */
#define scAbs		5	/* value of symbol is absolute */
#define scUndefined	6	/* who knows? */
#define scCdbLocal	7	/* variable's value is IN se->va.?? */
#define scBits		8	/* this is a bit field */
#define scCdbSystem	9	/* variable's value is IN CDB's address space */
#define scDbx		9	/* overlap dbx internal use */
#define scRegImage	10	/* register value saved on stack */
#define scInfo		11	/* symbol contains debugger information */
#define scUserStruct	12	/* address in struct user for current process */
#define scSData		13	/* load time only small data */
#define scSBss		14	/* load time only small common */
#define scRData		15	/* load time only read only data */
#define scVar		16	/* Var parameter (fortran,pascal) */
#define scCommon	17	/* common variable */
#define scSCommon	18	/* small common */
#define scVarRegister	19	/* Var parameter in a register */
#define scVariant	20	/* Variant record */
#define scSUndefined	21	/* small undefined(external) data */
#define scInit		22	/* .init section symbol */
#define scBasedVar	23	/* Fortran or PL/1 ptr based var */ 
#define scXData         24      /* exception handling data */
#define scPData         25      /* Procedure section */
#define scFini          26      /* .fini section */
#define scRConst	27	/* .rconst section */
#define scMax		32


/*
 *   Symbol Types
 */

#define stNil		0	/* Nuthin' special */
#define stGlobal	1	/* external symbol */
#define stStatic	2	/* static */
#define stParam		3	/* procedure argument */
#define stLocal		4	/* local variable */
#define stLabel		5	/* label */
#define stProc		6	/*     "      "	 Procedure */
#define stBlock		7	/* beginnning of block */
#define stEnd		8	/* end (of anything) */
#define stMember	9	/* member (of anything	- struct/union/enum */
#define stTypedef	10	/* type definition */
#define stFile		11	/* file name */
#define stRegReloc	12	/* register relocation */
#define stForward	13	/* forwarding address */
#define stStaticProc	14	/* load time only static procs */
#define stConstant	15	/* const */
#define stStaParam	16	/* Fortran static parameters */
    /* These new symbol types have been recently added to SGI machines. */
#define stStruct	26	/* Beginning of block defining a struct type */
#define stUnion		27	/* Beginning of block defining a union type */
#define stEnum		28	/* Beginning of block defining an enum type */
#define stIndirect	34	/* Indirect type specification */
    /* Pseudo-symbols - internal to debugger */
#define stStr		60	/* string */
#define stNumber	61	/* pure number (ie. 4 NOR 2+2) */
#define stExpr		62	/* 2+2 vs. 4 */
#define stType		63	/* post-coersion SER */
#define stMax		64

/* definitions for fields in TIR */

/* type qualifiers for ti.tq0 -> ti.(itqMax-1) */
#define tqNil	0	/* bt is what you see */
#define tqPtr	1	/* pointer */
#define tqProc	2	/* procedure */
#define tqArray 3	/* duh */
#define tqFar	4	/* longer addressing - 8086/8 land */
#define tqVol	5	/* volatile */
#define tqConst 6	/* const */
#define tqMax	8

/* basic types as seen in ti.bt */
#define btNil		0	/* undefined (also, enum members) */
#define btAdr		1	/* address - integer same size as pointer */
#define btChar		2	/* character */
#define btUChar		3	/* unsigned character */
#define btShort		4	/* short */
#define btUShort	5	/* unsigned short */
#define btInt		6	/* int */
#define btUInt		7	/* unsigned int */
#define btLong		8	/* long */
#define btULong		9	/* unsigned long */
#define btFloat		10	/* float (real) */
#define btDouble	11	/* Double (real) */
#define btStruct	12	/* Structure (Record) */
#define btUnion		13	/* Union (variant) */
#define btEnum		14	/* Enumerated */
#define btTypedef	15	/* defined via a typedef, isymRef points */
#define btRange		16	/* subrange of int */
#define btSet		17	/* pascal sets */
#define btComplex	18	/* fortran complex */
#define btDComplex	19	/* fortran double complex */
#define btIndirect	20	/* forward or unnamed typedef */
#define btFixedDec	21	/* Fixed Decimal */
#define btFloatDec	22	/* Float Decimal */
#define btString	23	/* Varying Length Character String */
#define btBit		24	/* Aligned Bit String */
#define btPicture	25	/* Picture */
#define btVoid		26	/* void */
#define btLongLong	27	/* long long */
#define btULongLong	28	/* unsigned long long */
#define btMax		64

#if (_MFG == _MIPS)
/* optimization type codes */
#define otNil		0
#define otReg		1	/* move var to reg */
#define otBlock		2	/* begin basic block */
#define	otProc		3	/* procedure */
#define otInline	4	/* inline procedure */
#define otEnd		5	/* whatever you started */
#define otMax		6	/* KEEP UP TO DATE */
#endif /* (_MFG == _MIPS) */
