/* coff object file format
   Copyright 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006
   Free Software Foundation, Inc.

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

#ifndef OBJ_FORMAT_H
#define OBJ_FORMAT_H

#define OBJ_COFF 1

#include "targ-cpu.h"

/* This internal_lineno crap is to stop namespace pollution from the
   bfd internal coff headerfile.  */
#define internal_lineno bfd_internal_lineno
#include "coff/internal.h"
#undef internal_lineno

/* CPU-specific setup:  */

#ifdef TC_ARM
#include "coff/arm.h"
#ifndef TARGET_FORMAT
#define TARGET_FORMAT "coff-arm"
#endif
#endif

#ifdef TC_PPC
#ifdef TE_PE
#include "coff/powerpc.h"
#else
#include "coff/rs6000.h"
#endif
#endif

#ifdef TC_SPARC
#include "coff/sparc.h"
#endif

#ifdef TC_I386
#ifndef TE_PEP
#include "coff/x86_64.h"
#else
#include "coff/i386.h"
#endif

#ifdef TE_PE
#ifdef TE_PEP
extern const char *   x86_64_target_format (void);
#define TARGET_FORMAT x86_64_target_format ()
#define COFF_TARGET_FORMAT "pe-x86-64"
#else
#define TARGET_FORMAT "pe-i386"
#endif
#endif

#ifndef TARGET_FORMAT
#ifdef TE_PEP
#define TARGET_FORMAT "coff-x86-64"
#else
#define TARGET_FORMAT "coff-i386"
#endif
#endif
#endif

#ifdef TC_M68K
#include "coff/m68k.h"
#ifndef TARGET_FORMAT
#define TARGET_FORMAT "coff-m68k"
#endif
#endif

#ifdef TC_OR32
#include "coff/or32.h"
#define TARGET_FORMAT "coff-or32-big"
#endif

#ifdef TC_I960
#include "coff/i960.h"
#define TARGET_FORMAT "coff-Intel-little"
#endif

#ifdef TC_Z80
#include "coff/z80.h"
#define TARGET_FORMAT "coff-z80"
#endif

#ifdef TC_Z8K
#include "coff/z8k.h"
#define TARGET_FORMAT "coff-z8k"
#endif

#ifdef TC_H8300
#include "coff/h8300.h"
#define TARGET_FORMAT "coff-h8300"
#endif

#ifdef TC_H8500
#include "coff/h8500.h"
#define TARGET_FORMAT "coff-h8500"
#endif

#ifdef TC_MAXQ20
#include "coff/maxq.h"
#define TARGET_FORMAT "coff-maxq"
#endif

#ifdef TC_SH

#ifdef TE_PE
#define COFF_WITH_PE
#endif

#include "coff/sh.h"

#ifdef TE_PE
#define TARGET_FORMAT "pe-shl"
#else

#define TARGET_FORMAT					\
  (!target_big_endian					\
   ? (sh_small ? "coff-shl-small" : "coff-shl")		\
   : (sh_small ? "coff-sh-small" : "coff-sh"))

#endif
#endif

#ifdef TC_MIPS
#define COFF_WITH_PE
#include "coff/mipspe.h"
#undef  TARGET_FORMAT
#define TARGET_FORMAT "pe-mips"
#endif

#ifdef TC_TIC30
#include "coff/tic30.h"
#define TARGET_FORMAT "coff-tic30"
#endif

#ifdef TC_TIC4X
#include "coff/tic4x.h"
#define TARGET_FORMAT "coff2-tic4x"
#endif

#ifdef TC_TIC54X
#include "coff/tic54x.h"
#define TARGET_FORMAT "coff1-c54x"
#endif

#ifdef TC_MCORE
#include "coff/mcore.h"
#ifndef TARGET_FORMAT
#define TARGET_FORMAT "pe-mcore"
#endif
#endif

#ifdef TE_PE
/* PE weak symbols need USE_UNIQUE.  */
#define USE_UNIQUE 1

#define obj_set_weak_hook pecoff_obj_set_weak_hook
#define obj_clear_weak_hook pecoff_obj_clear_weak_hook
#endif

#ifndef OBJ_COFF_MAX_AUXENTRIES
#define OBJ_COFF_MAX_AUXENTRIES 1
#endif

#define obj_symbol_new_hook coff_obj_symbol_new_hook
#define obj_symbol_clone_hook coff_obj_symbol_clone_hook
#define obj_read_begin_hook coff_obj_read_begin_hook

#include "bfd/libcoff.h"

#define OUTPUT_FLAVOR bfd_target_coff_flavour

/* Alter the field names, for now, until we've fixed up the other
   references to use the new name.  */
#ifdef TC_I960
#define TC_SYMFIELD_TYPE	symbolS *
#define sy_tc			bal
#endif

#define OBJ_SYMFIELD_TYPE	unsigned long
#define sy_obj			sy_flags

/* We can't use the predefined section symbols in bfd/section.c, as
   COFF symbols have extra fields.  See bfd/libcoff.h:coff_symbol_type.  */
#ifndef obj_sec_sym_ok_for_reloc
#define obj_sec_sym_ok_for_reloc(SEC)	((SEC)->owner != 0)
#endif

#define SYM_AUXENT(S) \
  (&coffsymbol (symbol_get_bfdsym (S))->native[1].u.auxent)
#define SYM_AUXINFO(S) \
  (&coffsymbol (symbol_get_bfdsym (S))->native[1])

/* The number of auxiliary entries.  */
#define S_GET_NUMBER_AUXILIARY(s) \
  (coffsymbol (symbol_get_bfdsym (s))->native->u.syment.n_numaux)
/* The number of auxiliary entries.  */
#define S_SET_NUMBER_AUXILIARY(s, v)	(S_GET_NUMBER_AUXILIARY (s) = (v))

/* True if a symbol name is in the string table, i.e. its length is > 8.  */
#define S_IS_STRING(s)		(strlen (S_GET_NAME (s)) > 8 ? 1 : 0)

/* Auxiliary entry macros. SA_ stands for symbol auxiliary.  */
/* Omit the tv related fields.  */
/* Accessors.  */

#define SA_GET_SYM_TAGNDX(s)	(SYM_AUXENT (s)->x_sym.x_tagndx.l)
#define SA_GET_SYM_LNNO(s)	(SYM_AUXENT (s)->x_sym.x_misc.x_lnsz.x_lnno)
#define SA_GET_SYM_SIZE(s)	(SYM_AUXENT (s)->x_sym.x_misc.x_lnsz.x_size)
#define SA_GET_SYM_FSIZE(s)	(SYM_AUXENT (s)->x_sym.x_misc.x_fsize)
#define SA_GET_SYM_LNNOPTR(s)	(SYM_AUXENT (s)->x_sym.x_fcnary.x_fcn.x_lnnoptr)
#define SA_GET_SYM_ENDNDX(s)	(SYM_AUXENT (s)->x_sym.x_fcnary.x_fcn.x_endndx)
#define SA_GET_SYM_DIMEN(s,i)	(SYM_AUXENT (s)->x_sym.x_fcnary.x_ary.x_dimen[(i)])
#define SA_GET_FILE_FNAME(s)	(SYM_AUXENT (s)->x_file.x_fname)
#define SA_GET_SCN_SCNLEN(s)	(SYM_AUXENT (s)->x_scn.x_scnlen)
#define SA_GET_SCN_NRELOC(s)	(SYM_AUXENT (s)->x_scn.x_nreloc)
#define SA_GET_SCN_NLINNO(s)	(SYM_AUXENT (s)->x_scn.x_nlinno)

#define SA_SET_SYM_LNNO(s,v)	(SYM_AUXENT (s)->x_sym.x_misc.x_lnsz.x_lnno = (v))
#define SA_SET_SYM_SIZE(s,v)	(SYM_AUXENT (s)->x_sym.x_misc.x_lnsz.x_size = (v))
#define SA_SET_SYM_FSIZE(s,v)	(SYM_AUXENT (s)->x_sym.x_misc.x_fsize = (v))
#define SA_SET_SYM_LNNOPTR(s,v)	(SYM_AUXENT (s)->x_sym.x_fcnary.x_fcn.x_lnnoptr = (v))
#define SA_SET_SYM_DIMEN(s,i,v)	(SYM_AUXENT (s)->x_sym.x_fcnary.x_ary.x_dimen[(i)] = (v))
#define SA_SET_FILE_FNAME(s,v)	strncpy (SYM_AUXENT (s)->x_file.x_fname, (v), FILNMLEN)
#define SA_SET_SCN_SCNLEN(s,v)	(SYM_AUXENT (s)->x_scn.x_scnlen = (v))
#define SA_SET_SCN_NRELOC(s,v)	(SYM_AUXENT (s)->x_scn.x_nreloc = (v))
#define SA_SET_SCN_NLINNO(s,v)	(SYM_AUXENT (s)->x_scn.x_nlinno = (v))

/* Internal use only definitions. SF_ stands for symbol flags.

   These values can be assigned to sy_symbol.ost_flags field of a symbolS.

   You'll break i960 if you shift the SYSPROC bits anywhere else.  for
   more on the balname/callname hack, see tc-i960.h.  b.out is done
   differently.  */

#define SF_I960_MASK	0x000001ff	/* Bits 0-8 are used by the i960 port.  */
#define SF_SYSPROC	0x0000003f	/* bits 0-5 are used to store the sysproc number.  */
#define SF_IS_SYSPROC	0x00000040	/* bit 6 marks symbols that are sysprocs.  */
#define SF_BALNAME	0x00000080	/* bit 7 marks BALNAME symbols.  */
#define SF_CALLNAME	0x00000100	/* bit 8 marks CALLNAME symbols.  */
				  
#define SF_NORMAL_MASK	0x0000ffff	/* bits 12-15 are general purpose.  */
				  
#define SF_STATICS	0x00001000	/* Mark the .text & all symbols.  */
#define SF_DEFINED	0x00002000	/* Symbol is defined in this file.  */
#define SF_STRING	0x00004000	/* Symbol name length > 8.  */
#define SF_LOCAL	0x00008000	/* Symbol must not be emitted.  */
				  
#define SF_DEBUG_MASK	0xffff0000	/* bits 16-31 are debug info.  */
				  
#define SF_FUNCTION	0x00010000	/* The symbol is a function.  */
#define SF_PROCESS	0x00020000	/* Process symbol before write.  */
#define SF_TAGGED	0x00040000	/* Is associated with a tag.  */
#define SF_TAG		0x00080000	/* Is a tag.  */
#define SF_DEBUG	0x00100000	/* Is in debug or abs section.  */
#define SF_GET_SEGMENT	0x00200000	/* Get the section of the forward symbol.  */
/* All other bits are unused.  */

/* Accessors.  */
#define SF_GET(s)		(* symbol_get_obj (s))
#define SF_GET_DEBUG(s)		(symbol_get_bfdsym (s)->flags & BSF_DEBUGGING)
#define SF_SET_DEBUG(s)		(symbol_get_bfdsym (s)->flags |= BSF_DEBUGGING)
#define SF_GET_NORMAL_FIELD(s)	(SF_GET (s) & SF_NORMAL_MASK)
#define SF_GET_DEBUG_FIELD(s)	(SF_GET (s) & SF_DEBUG_MASK)
#define SF_GET_FILE(s)		(SF_GET (s) & SF_FILE)
#define SF_GET_STATICS(s)	(SF_GET (s) & SF_STATICS)
#define SF_GET_DEFINED(s)	(SF_GET (s) & SF_DEFINED)
#define SF_GET_STRING(s)	(SF_GET (s) & SF_STRING)
#define SF_GET_LOCAL(s)		(SF_GET (s) & SF_LOCAL)
#define SF_GET_FUNCTION(s)      (SF_GET (s) & SF_FUNCTION)
#define SF_GET_PROCESS(s)	(SF_GET (s) & SF_PROCESS)
#define SF_GET_TAGGED(s)	(SF_GET (s) & SF_TAGGED)
#define SF_GET_TAG(s)		(SF_GET (s) & SF_TAG)
#define SF_GET_GET_SEGMENT(s)	(SF_GET (s) & SF_GET_SEGMENT)
#define SF_GET_I960(s)		(SF_GET (s) & SF_I960_MASK)	/* Used by i960.  */
#define SF_GET_BALNAME(s)	(SF_GET (s) & SF_BALNAME)	/* Used by i960.  */
#define SF_GET_CALLNAME(s)	(SF_GET (s) & SF_CALLNAME)	/* Used by i960.  */
#define SF_GET_IS_SYSPROC(s)	(SF_GET (s) & SF_IS_SYSPROC)	/* Used by i960.  */
#define SF_GET_SYSPROC(s)	(SF_GET (s) & SF_SYSPROC)	/* Used by i960.  */

/* Modifiers.  */
#define SF_SET(s,v)		(SF_GET (s) = (v))
#define SF_SET_NORMAL_FIELD(s,v)(SF_GET (s) |= ((v) & SF_NORMAL_MASK))
#define SF_SET_DEBUG_FIELD(s,v)	(SF_GET (s) |= ((v) & SF_DEBUG_MASK))
#define SF_SET_FILE(s)		(SF_GET (s) |= SF_FILE)
#define SF_SET_STATICS(s)	(SF_GET (s) |= SF_STATICS)
#define SF_SET_DEFINED(s)	(SF_GET (s) |= SF_DEFINED)
#define SF_SET_STRING(s)	(SF_GET (s) |= SF_STRING)
#define SF_SET_LOCAL(s)		(SF_GET (s) |= SF_LOCAL)
#define SF_CLEAR_LOCAL(s)	(SF_GET (s) &= ~SF_LOCAL)
#define SF_SET_FUNCTION(s)      (SF_GET (s) |= SF_FUNCTION)
#define SF_SET_PROCESS(s)	(SF_GET (s) |= SF_PROCESS)
#define SF_SET_TAGGED(s)	(SF_GET (s) |= SF_TAGGED)
#define SF_SET_TAG(s)		(SF_GET (s) |= SF_TAG)
#define SF_SET_GET_SEGMENT(s)	(SF_GET (s) |= SF_GET_SEGMENT)
#define SF_SET_I960(s,v)	(SF_GET (s) |= ((v) & SF_I960_MASK))	/* Used by i960.  */
#define SF_SET_BALNAME(s)	(SF_GET (s) |= SF_BALNAME)		/* Used by i960.  */
#define SF_SET_CALLNAME(s)	(SF_GET (s) |= SF_CALLNAME)		/* Used by i960.  */
#define SF_SET_IS_SYSPROC(s)	(SF_GET (s) |= SF_IS_SYSPROC)		/* Used by i960.  */
#define SF_SET_SYSPROC(s,v)	(SF_GET (s) |= ((v) & SF_SYSPROC))	/* Used by i960.  */


/*  Line number handling.  */
extern int text_lineno_number;
extern int coff_line_base;
extern int coff_n_line_nos;
extern symbolS *coff_last_function;

#define obj_emit_lineno(WHERE, LINE, FILE_START)	abort ()
#define obj_app_file(name, app)      c_dot_file_symbol (name, app)
#define obj_frob_symbol(S,P) 	     coff_frob_symbol (S, & P)
#define obj_frob_section(S)	     coff_frob_section (S)
#define obj_frob_file_after_relocs() coff_frob_file_after_relocs ()
#ifndef obj_adjust_symtab
#define obj_adjust_symtab()	     coff_adjust_symtab ()
#endif

/* Forward the segment of a forwarded symbol, handle assignments that
   just copy symbol values, etc.  */
#ifndef OBJ_COPY_SYMBOL_ATTRIBUTES
#ifndef TE_I386AIX
#define OBJ_COPY_SYMBOL_ATTRIBUTES(dest, src) \
  (SF_GET_GET_SEGMENT (dest) \
   ? (S_SET_SEGMENT (dest, S_GET_SEGMENT (src)), 0) \
   : 0)
#else
#define OBJ_COPY_SYMBOL_ATTRIBUTES(dest, src) \
  (SF_GET_GET_SEGMENT (dest) && S_GET_SEGMENT (dest) == SEG_UNKNOWN \
   ? (S_SET_SEGMENT (dest, S_GET_SEGMENT (src)), 0) \
   : 0)
#endif
#endif

/* Sanity check.  */

#ifdef TC_I960
#ifndef C_LEAFSTAT
hey ! Where is the C_LEAFSTAT definition ? i960 - coff support is depending on it.
#endif /* no C_LEAFSTAT */
#endif /* TC_I960 */

extern const pseudo_typeS coff_pseudo_table[];

#ifndef obj_pop_insert
#define obj_pop_insert() pop_insert (coff_pseudo_table)
#endif

/* In COFF, if a symbol is defined using .def/.val SYM/.endef, it's OK
   to redefine the symbol later on.  This can happen if C symbols use
   a prefix, and a symbol is defined both with and without the prefix,
   as in start/_start/__start in gcc/libgcc1-test.c.  */
#define RESOLVE_SYMBOL_REDEFINITION(sym)		\
(SF_GET_GET_SEGMENT (sym)				\
 ? (sym->sy_frag = frag_now,				\
    S_SET_VALUE (sym, frag_now_fix ()),			\
    S_SET_SEGMENT (sym, now_seg),			\
    0)							\
 : 0)

/* Stabs in a coff file go into their own section.  */
#define SEPARATE_STAB_SECTIONS 1

/* We need 12 bytes at the start of the section to hold some initial
   information.  */
#define INIT_STAB_SECTION(seg) obj_coff_init_stab_section (seg)

/* Store the number of relocations in the section aux entry.  */
#define SET_SECTION_RELOCS(sec, relocs, n) \
  SA_SET_SCN_NRELOC (section_symbol (sec), n)

#define obj_app_file(name, app) c_dot_file_symbol (name, app)

extern int  S_SET_DATA_TYPE              (symbolS *, int);
extern int  S_SET_STORAGE_CLASS          (symbolS *, int);
extern int  S_GET_STORAGE_CLASS          (symbolS *);
extern void SA_SET_SYM_ENDNDX            (symbolS *, symbolS *);
extern void coff_add_linesym             (symbolS *);
extern void c_dot_file_symbol            (const char *, int);
extern void coff_frob_symbol             (symbolS *, int *);
extern void coff_adjust_symtab           (void);
extern void coff_frob_section            (segT);
extern void coff_adjust_section_syms     (bfd *, asection *, void *);
extern void coff_frob_file_after_relocs  (void);
extern void coff_obj_symbol_new_hook     (symbolS *);
extern void coff_obj_symbol_clone_hook   (symbolS *, symbolS *);
extern void coff_obj_read_begin_hook     (void);
#ifdef TE_PE
extern void pecoff_obj_set_weak_hook     (symbolS *);
extern void pecoff_obj_clear_weak_hook   (symbolS *);
#endif
extern void obj_coff_section             (int);
extern segT obj_coff_add_segment         (const char *);
extern void obj_coff_section             (int);
extern void c_dot_file_symbol            (const char *, int);
extern segT s_get_segment                (symbolS *);
#ifndef tc_coff_symbol_emit_hook
extern void tc_coff_symbol_emit_hook     (symbolS *);
#endif
extern void obj_coff_pe_handle_link_once (void);
extern void obj_coff_init_stab_section   (segT);
extern void c_section_header             (struct internal_scnhdr *,
					  char *, long, long, long, long,
					  long, long, long, long);
#endif /* OBJ_FORMAT_H */
