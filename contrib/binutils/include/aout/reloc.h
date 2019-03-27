/* reloc.h -- Header file for relocation information.
   Copyright 1989, 1990, 1991 Free Software Foundation, Inc.

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

/* Relocation types for a.out files using reloc_info_extended
   (SPARC and AMD 29000). */

#ifndef _RELOC_H_READ_
#define _RELOC_H_READ_ 1

enum reloc_type
  {
    RELOC_8,        RELOC_16,        RELOC_32, /* simple relocations */
    RELOC_DISP8,    RELOC_DISP16,    RELOC_DISP32, /* pc-rel displacement */
    RELOC_WDISP30,  RELOC_WDISP22,
    RELOC_HI22,     RELOC_22,
    RELOC_13,       RELOC_LO10,
    RELOC_SFA_BASE, RELOC_SFA_OFF13,
    RELOC_BASE10,   RELOC_BASE13,    RELOC_BASE22, /* P.I.C. (base-relative) */
    RELOC_PC10,     RELOC_PC22,	/* for some sort of pc-rel P.I.C. (?) */
    RELOC_JMP_TBL,		/* P.I.C. jump table */
    RELOC_SEGOFF16,		/* reputedly for shared libraries somehow */
    RELOC_GLOB_DAT,  RELOC_JMP_SLOT, RELOC_RELATIVE,
    RELOC_11,
    RELOC_WDISP2_14,
    RELOC_WDISP19,
    RELOC_HHI22,
    RELOC_HLO10,
    
    /* 29K relocation types */
    RELOC_JUMPTARG, RELOC_CONST,     RELOC_CONSTH,
    
    RELOC_WDISP14, RELOC_WDISP21,
    
    NO_RELOC
    };

#define	RELOC_TYPE_NAMES \
"8",		"16",		"32",		"DISP8",	\
"DISP16",	"DISP32",	"WDISP30",	"WDISP22",	\
"HI22",		"22",		"13",		"LO10",		\
"SFA_BASE",	"SFAOFF13",	"BASE10",	"BASE13",	\
"BASE22",	"PC10",		"PC22",		"JMP_TBL",	\
"SEGOFF16",	"GLOB_DAT",	"JMP_SLOT",	"RELATIVE",	\
"11",		"WDISP2_14",	"WDISP19", 	"HHI22",	\
"HLO10",							\
"JUMPTARG",	"CONST",	"CONSTH",	"WDISP14",	\
"WDISP21",	\
"NO_RELOC"

#endif /* _RELOC_H_READ_ */

/* end of reloc.h */
