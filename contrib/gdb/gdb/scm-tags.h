/* This is a minimally edited version of Guile's tags.h. */
/* classes: h_files */

#ifndef TAGSH
#define TAGSH
/*      Copyright 1995, 1999 Free Software Foundation, Inc.

 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * As a special exception, the Free Software Foundation gives permission
 * for additional uses of the text contained in its release of GUILE.
 *
 * The exception is that, if you link the GUILE library with other files
 * to produce an executable, this does not by itself cause the
 * resulting executable to be covered by the GNU General Public License.
 * Your use of that executable is in no way restricted on account of
 * linking the GUILE library code into it.
 *
 * This exception does not however invalidate any other reasons why
 * the executable file might be covered by the GNU General Public License.
 *
 * This exception applies only to the code released by the
 * Free Software Foundation under the name GUILE.  If you copy
 * code from other Free Software Foundation releases into a copy of
 * GUILE, as the General Public License permits, the exception does
 * not apply to the code that you add in this way.  To avoid misleading
 * anyone as to the status of such modified files, you must delete
 * this exception notice from them.
 *
 * If you write modifications of your own for GUILE, it is your choice
 * whether to permit this exception to apply to your modifications.
 * If you do not wish that, delete this exception notice.  
 */


/** This file defines the format of SCM values and cons pairs.  
 ** It is here that tag bits are assigned for various purposes.
 **/


/* Three Bit Tags

 * 000 -- a non-immediate value.  Points into the pair heap.  
 *
 * 001 -- a gloc (i.e., a resolved global variable in a CAR in a code graph)
 *        or the CAR of an object handle (i.e., the tagged pointer to the
 *        vtable part of a user-defined object).
 *
 *        If X has this tag, the value at CDAR(X - 1) distinguishes
 *        glocs from object handles.  The distinction only needs
 *        to be made in a few places.  Only a few parts of the code know
 *        about glocs.  In most cases, when a value in the CAR of a pair
 *        has the tag 001, it means that the pair is an object handle.
 *
 * 010 -- the tag for immediate, exact integers. 
 *
 * 011 -- in the CAR of a pair, this tag indicates that the pair is a closure.
 *        The remaining bits of the CAR are a pointer into the pair heap
 *        to the code graph for the closure.
 *
 * 1xy -- an extension tag which means that there is a five or six bit
 *        tag to the left of the low three bits.  See the nice diagrams
 *        in ../doc/code.doc if you want to know what the bits mean.
 */





#define scm_tc3_cons		0
#define scm_tc3_cons_gloc	1
#define scm_tc3_closure		3

#define scm_tc7_ssymbol		5
#define scm_tc7_msymbol		7
#define scm_tc7_string		13
#define scm_tc7_bvect		15
#define scm_tc7_vector		21
#define scm_tc7_lvector		23
#define scm_tc7_ivect		29
#define scm_tc7_uvect		31
/* spare 37 39 */
#define scm_tc7_fvect		45
#define scm_tc7_dvect		47
#define scm_tc7_cvect		53
#define scm_tc7_port		55
#define scm_tc7_contin		61
#define scm_tc7_cclo		63
/* spare 69 71 77 79 */
#define scm_tc7_subr_0		85
#define scm_tc7_subr_1		87
#define scm_tc7_cxr		93
#define scm_tc7_subr_3		95
#define scm_tc7_subr_2		101
#define scm_tc7_asubr		103
#define scm_tc7_subr_1o		109
#define scm_tc7_subr_2o		111
#define scm_tc7_lsubr_2		117
#define scm_tc7_lsubr		119
#define scm_tc7_rpsubr		125

#define scm_tc7_smob		127
#define scm_tc_free_cell	127

#define scm_tc16_flo		0x017f
#define scm_tc_flo		0x017fL

#define SCM_REAL_PART		(1L<<16)
#define SCM_IMAG_PART		(2L<<16)
#define scm_tc_dblr		(scm_tc16_flo|REAL_PART)
#define scm_tc_dblc		(scm_tc16_flo|REAL_PART|IMAG_PART)

#define scm_tc16_bigpos		0x027f
#define scm_tc16_bigneg		0x037f

#define scm_tc16_fport 		(scm_tc7_port + 0*256L)
#define scm_tc16_pipe 		(scm_tc7_port + 1*256L)
#define scm_tc16_strport	(scm_tc7_port + 2*256L)
#define scm_tc16_sfport 	(scm_tc7_port + 3*256L)



/* For cons pairs with immediate values in the CAR */
#define scm_tcs_cons_imcar 2:case 4:case 6:case 10:\
 case 12:case 14:case 18:case 20:\
 case 22:case 26:case 28:case 30:\
 case 34:case 36:case 38:case 42:\
 case 44:case 46:case 50:case 52:\
 case 54:case 58:case 60:case 62:\
 case 66:case 68:case 70:case 74:\
 case 76:case 78:case 82:case 84:\
 case 86:case 90:case 92:case 94:\
 case 98:case 100:case 102:case 106:\
 case 108:case 110:case 114:case 116:\
 case 118:case 122:case 124:case 126

/* For cons pairs with non-immediate values in the CAR */
#define scm_tcs_cons_nimcar 0:case 8:case 16:case 24:\
 case 32:case 40:case 48:case 56:\
 case 64:case 72:case 80:case 88:\
 case 96:case 104:case 112:case 120

/* A CONS_GLOC occurs in code.  It's CAR is a pointer to the
 * CDR of a variable.  The low order bits of the CAR are 001.
 * The CDR of the gloc is the code continuation.
 */
#define scm_tcs_cons_gloc 1:case 9:case 17:case 25:\
 case 33:case 41:case 49:case 57:\
 case 65:case 73:case 81:case 89:\
 case 97:case 105:case 113:case 121

#define scm_tcs_closures   3:case 11:case 19:case 27:\
 case 35:case 43:case 51:case 59:\
 case 67:case 75:case 83:case 91:\
 case 99:case 107:case 115:case 123

#define scm_tcs_subrs scm_tc7_asubr:case scm_tc7_subr_0:case scm_tc7_subr_1:case scm_tc7_cxr:\
 case scm_tc7_subr_3:case scm_tc7_subr_2:case scm_tc7_rpsubr:case scm_tc7_subr_1o:\
 case scm_tc7_subr_2o:case scm_tc7_lsubr_2:case scm_tc7_lsubr

#define scm_tcs_symbols scm_tc7_ssymbol:case scm_tc7_msymbol

#define scm_tcs_bignums tc16_bigpos:case tc16_bigneg



/* References to objects are of type SCM.  Values may be non-immediate
 * (pointers) or immediate (encoded, immutable, scalar values that fit
 * in an SCM variable).
 */

typedef long SCM;

/* Cray machines have pointers that are incremented once for each word,
 * rather than each byte, the 3 most significant bits encode the byte
 * within the word.  The following macros deal with this by storing the
 * native Cray pointers like the ones that looks like scm expects.  This
 * is done for any pointers that might appear in the car of a scm_cell, pointers
 * to scm_vector elts, functions, &c are not munged.
 */
#ifdef _UNICOS
#define SCM2PTR(x) ((int)(x) >> 3)
#define PTR2SCM(x) (((SCM)(x)) << 3)
#define SCM_POINTERS_MUNGED
#else
#define SCM2PTR(x) (x)
#define PTR2SCM(x) ((SCM)(x))
#endif /* def _UNICOS */



/* Immediate? Predicates 
 */
#define SCM_IMP(x) 	(6 & (int)(x))
#define SCM_NIMP(x) 	(!SCM_IMP(x))



enum scm_tags
  {
    scm_tc8_char = 0xf4
  };

#define SCM_ITAG8(X)		((int)(X) & 0xff)
#define SCM_MAKE_ITAG8(X, TAG)	(((X)<<8) + TAG)
#define SCM_ITAG8_DATA(X)	((X)>>8)



/* Local Environment Structure
 */
#define SCM_ILOCP(n)		((0xff & (int)(n))==0xfc)
#define SCM_ILOC00		(0x000000fcL)
#define SCM_IDINC		(0x00100000L)
#define SCM_ICDR		(0x00080000L)
#define SCM_IFRINC		(0x00000100L)
#define SCM_IDSTMSK		(-SCM_IDINC)
#define SCM_IFRAME(n) 		((int)((SCM_ICDR-SCM_IFRINC)>>8) & ((int)(n)>>8))
#define SCM_IDIST(n) 		(((unsigned long)(n))>>20)
#define SCM_ICDRP(n) 		(SCM_ICDR & (n))


/* Immediate Symbols, Special Symbols, Flags (various constants).
 */

/* ISYMP tests for ISPCSYM and ISYM */
#define SCM_ISYMP(n) 		((0x187 & (int)(n))==4)

/* IFLAGP tests for ISPCSYM, ISYM and IFLAG */
#define SCM_IFLAGP(n) 		((0x87 & (int)(n))==4)
#define SCM_ISYMNUM(n) 		((int)((n)>>9))
#define SCM_ISYMCHARS(n) 	(scm_isymnames[SCM_ISYMNUM(n)])
#define SCM_MAKSPCSYM(n) 	(((n)<<9)+((n)<<3)+4L)
#define SCM_MAKISYM(n) 		(((n)<<9)+0x74L)
#define SCM_MAKIFLAG(n) 	(((n)<<9)+0x174L)

/* This table must agree with the declarations 
 * in repl.c: {Names of immediate symbols}.
 *
 * These are used only in eval but their values
 * have to be allocated here.
 *
 */

#define SCM_IM_AND		SCM_MAKSPCSYM(0)
#define SCM_IM_BEGIN		SCM_MAKSPCSYM(1)
#define SCM_IM_CASE		SCM_MAKSPCSYM(2)
#define SCM_IM_COND		SCM_MAKSPCSYM(3)
#define SCM_IM_DO		SCM_MAKSPCSYM(4)
#define SCM_IM_IF		SCM_MAKSPCSYM(5)
#define SCM_IM_LAMBDA		SCM_MAKSPCSYM(6)
#define SCM_IM_LET		SCM_MAKSPCSYM(7)
#define SCM_IM_LETSTAR		SCM_MAKSPCSYM(8)
#define SCM_IM_LETREC		SCM_MAKSPCSYM(9)
#define SCM_IM_OR		SCM_MAKSPCSYM(10)
#define SCM_IM_QUOTE		SCM_MAKSPCSYM(11)
#define SCM_IM_SET		SCM_MAKSPCSYM(12)
#define SCM_IM_DEFINE		SCM_MAKSPCSYM(13)
#define SCM_IM_APPLY		SCM_MAKISYM(14)
#define SCM_IM_CONT		SCM_MAKISYM(15)
#define SCM_NUM_ISYMS 16

/* Important immediates
 */

#define SCM_BOOL_F		SCM_MAKIFLAG(SCM_NUM_ISYMS+0)
#define SCM_BOOL_T 		SCM_MAKIFLAG(SCM_NUM_ISYMS+1)
#define SCM_UNDEFINED	 	SCM_MAKIFLAG(SCM_NUM_ISYMS+2)
#define SCM_EOF_VAL 		SCM_MAKIFLAG(SCM_NUM_ISYMS+3)

#ifdef SICP
#define SCM_EOL 		SCM_BOOL_F
#else
#define SCM_EOL			SCM_MAKIFLAG(SCM_NUM_ISYMS+4)
#endif

#define SCM_UNSPECIFIED		SCM_MAKIFLAG(SCM_NUM_ISYMS+5)



/* Heap Pairs and the Empty List Predicates
 */
#define SCM_NULLP(x) 	(SCM_EOL == (x))
#define SCM_NNULLP(x) 	(SCM_EOL != (x))
#define SCM_CELLP(x) 	(!SCM_NCELLP(x))
#define SCM_NCELLP(x) 	((sizeof(scm_cell)-1) & (int)(x))



#define SCM_UNBNDP(x) 	(SCM_UNDEFINED==(x))



/* Testing and Changing GC Marks in Various Standard Positions
 */
#define SCM_GCMARKP(x) 		(1 & (int)SCM_CDR(x))
#define SCM_GC8MARKP(x) 	(0x80 & (int)SCM_CAR(x))
#define SCM_SETGCMARK(x) 	(SCM_CDR(x) |= 1)
#define SCM_CLRGCMARK(x) 	(SCM_CDR(x) &= ~1L)
#define SCM_SETGC8MARK(x) 	(SCM_CAR(x) |= 0x80)
#define SCM_CLRGC8MARK(x) 	(SCM_CAR(x) &= ~0x80L)


/* Extracting Tag Bits, With or Without GC Safety and Optional Bits
 */
#define SCM_TYP3(x) 		(7 & (int)SCM_CAR(x))
#define SCM_TYP7(x) 		(0x7f & (int)SCM_CAR(x))
#define SCM_TYP7S(x) 		(0x7d & (int)SCM_CAR(x))
#define SCM_TYP16(x) 		(0xffff & (int)SCM_CAR(x))
#define SCM_TYP16S(x) 		(0xfeff & (int)SCM_CAR(x))
#define SCM_GCTYP16(x) 		(0xff7f & (int)SCM_CAR(x))


/* Two slightly extensible types: smobs and ptobs.

 */
#define SCM_SMOBNUM(x) (0x0ff & (CAR(x)>>8));
#define SCM_PTOBNUM(x) (0x0ff & (CAR(x)>>8));




#define SCM_DIRP(x) (SCM_NIMP(x) && (TYP16(x)==(scm_tc16_dir)))
#define SCM_OPDIRP(x) (SCM_NIMP(x) && (CAR(x)==(scm_tc16_dir | OPN)))



/* Lvectors 
 */
#define SCM_LVECTORP(x) (TYP7(x)==tc7_lvector)


#if 0

/* Sockets 
 */
#define tc_socket (tc7_port | OPN)
#define SCM_SOCKP(x) (((0x7f | OPN | RDNG | WRTNG) & CAR(x))==(tc_socket))
#define SCM_SOCKTYP(x) (CAR(x)>>24)



extern int scm_tc16_key_vector;
#define SCM_KEYVECP(X)   (scm_tc16_key_vector == TYP16 (X))
#define SCM_KEYVECLEN(OBJ) (((unsigned long)CAR (obj)) >> 16)


#define SCM_MALLOCDATA(obj) ((char *)CDR(obj))
#define SCM_MALLOCLEN(obj) (((unsigned long)CAR (obj)) >> 16)
#define SCM_WORDDATA(obj)  (CDR (obj))


#define SCM_BYTECODEP(X) ((TYP7 (X) == tc7_cclo) && (CCLO_SUBR (X) == rb_proc))
#define SCM_BYTECODE_CONSTANTS(X) (VELTS(X)[1])
#define SCM_BYTECODE_CODE(X) (VELTS(X)[2])
#define SCM_BYTECODE_NAME(X) (VELTS(X)[3])
#define SCM_BYTECODE_BCODE(X) (VELTS(X)[4])
#define SCM_BYTECODE_ELTS 5


#define SCM_FREEP(x) (CAR(x)==tc_free_cell)
#define SCM_NFREEP(x) (!FREEP(x))

#endif /* 0 */


#endif /* TAGSH */
