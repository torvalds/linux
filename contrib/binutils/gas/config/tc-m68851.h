/* This file is tc-m68851.h

   Copyright 1987, 1988, 1989, 1990, 1991, 1992, 2000
   Free Software Foundation, Inc.

   This file is part of GAS, the GNU Assembler.

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

/*
 * pmmu.h
 */

/* I suppose we have to copyright this file.  Someone on the net sent it
   to us as part of the changes for the m68851 Memory Management Unit */

/* Copyright (C) 1987 Free Software Foundation, Inc.

   This file is part of Gas, the GNU Assembler.

   The GNU assembler is distributed in the hope that it will be
   useful, but WITHOUT ANY WARRANTY.  No author or distributor
   accepts responsibility to anyone for the consequences of using it
   or for whether it serves any particular purpose or works at all,
   unless he says so in writing.  Refer to the GNU Assembler General
   Public License for full details.

   Everyone is granted permission to copy, modify and redistribute
   the GNU Assembler, but only under the conditions described in the
   GNU Assembler General Public License.  A copy of this license is
   supposed to have been given to you along with the GNU Assembler
   so you can know your rights and responsibilities.  It should be
   in a file named COPYING.  Among other things, the copyright
   notice and this notice must be preserved on all copies.  */

#ifdef m68851

/*
  I didn't use much imagination in choosing the
  following codes, so many of them aren't very
  mnemonic. -rab

  P  pmmu register
  Possible values:
  000	TC	Translation Control reg
  100	CAL	Current Access Level
  101	VAL	Validate Access Level
  110	SCC	Stack Change Control
  111	AC	Access Control

  W  wide pmmu registers
  Possible values:
  001	DRP	Dma Root Pointer
  010	SRP	Supervisor Root Pointer
  011	CRP	Cpu Root Pointer

  f	function code register
  0	SFC
  1	DFC

  V	VAL register only

  X	BADx, BACx
  100	BAD	Breakpoint Acknowledge Data
  101	BAC	Breakpoint Acknowledge Control

  Y	PSR
  Z	PCSR

  |	memory 		(modes 2-6, 7.*)

  */

/*
 * these defines should be in m68k.c but
 * i put them here to keep all the m68851 stuff
 * together -rab
 * JF--Make sure these #s don't clash with the ones in m68k.c
 * That would be BAD.
 */
#define TC	(FPS+1)		/* 48 */
#define DRP	(TC+1)		/* 49 */
#define SRP	(DRP+1)		/* 50 */
#define CRP	(SRP+1)		/* 51 */
#define CAL	(CRP+1)		/* 52 */
#define VAL	(CAL+1)		/* 53 */
#define SCC	(VAL+1)		/* 54 */
#define AC	(SCC+1)		/* 55 */
#define BAD	(AC+1)		/* 56,57,58,59, 60,61,62,63 */
#define BAC	(BAD+8)		/* 64,65,66,67, 68,69,70,71 */
#define PSR	(BAC+8)		/* 72 */
#define PCSR	(PSR+1)		/* 73 */

/* name */	/* opcode */		/* match */		/* args */

{"pbac",	one(0xf0c7),		one(0xffbf),		"Bc"},
{"pbacw",	one(0xf087),		one(0xffbf),		"Bc"},
{"pbas",	one(0xf0c6),		one(0xffbf),		"Bc"},
{"pbasw",	one(0xf086),		one(0xffbf),		"Bc"},
{"pbbc",	one(0xf0c1),		one(0xffbf),		"Bc"},
{"pbbcw",	one(0xf081),		one(0xffbf),		"Bc"},
{"pbbs",	one(0xf0c0),		one(0xffbf),		"Bc"},
{"pbbsw",	one(0xf080),		one(0xffbf),		"Bc"},
{"pbcc",	one(0xf0cf),		one(0xffbf),		"Bc"},
{"pbccw",	one(0xf08f),		one(0xffbf),		"Bc"},
{"pbcs",	one(0xf0ce),		one(0xffbf),		"Bc"},
{"pbcsw",	one(0xf08e),		one(0xffbf),		"Bc"},
{"pbgc",	one(0xf0cd),		one(0xffbf),		"Bc"},
{"pbgcw",	one(0xf08d),		one(0xffbf),		"Bc"},
{"pbgs",	one(0xf0cc),		one(0xffbf),		"Bc"},
{"pbgsw",	one(0xf08c),		one(0xffbf),		"Bc"},
{"pbic",	one(0xf0cb),		one(0xffbf),		"Bc"},
{"pbicw",	one(0xf08b),		one(0xffbf),		"Bc"},
{"pbis",	one(0xf0ca),		one(0xffbf),		"Bc"},
{"pbisw",	one(0xf08a),		one(0xffbf),		"Bc"},
{"pblc",	one(0xf0c3),		one(0xffbf),		"Bc"},
{"pblcw",	one(0xf083),		one(0xffbf),		"Bc"},
{"pbls",	one(0xf0c2),		one(0xffbf),		"Bc"},
{"pblsw",	one(0xf082),		one(0xffbf),		"Bc"},
{"pbsc",	one(0xf0c5),		one(0xffbf),		"Bc"},
{"pbscw",	one(0xf085),		one(0xffbf),		"Bc"},
{"pbss",	one(0xf0c4),		one(0xffbf),		"Bc"},
{"pbssw",	one(0xf084),		one(0xffbf),		"Bc"},
{"pbwc",	one(0xf0c9),		one(0xffbf),		"Bc"},
{"pbwcw",	one(0xf089),		one(0xffbf),		"Bc"},
{"pbws",	one(0xf0c8),		one(0xffbf),		"Bc"},
{"pbwsw",	one(0xf088),		one(0xffbf),		"Bc"},

{"pdbac",	two(0xf048, 0x0007),	two(0xfff8, 0xffff),	"DsBw"},
{"pdbas",	two(0xf048, 0x0006),	two(0xfff8, 0xffff),	"DsBw"},
{"pdbbc",	two(0xf048, 0x0001),	two(0xfff8, 0xffff),	"DsBw"},
{"pdbbs",	two(0xf048, 0x0000),	two(0xfff8, 0xffff),	"DsBw"},
{"pdbcc",	two(0xf048, 0x000f),	two(0xfff8, 0xffff),	"DsBw"},
{"pdbcs",	two(0xf048, 0x000e),	two(0xfff8, 0xffff),	"DsBw"},
{"pdbgc",	two(0xf048, 0x000d),	two(0xfff8, 0xffff),	"DsBw"},
{"pdbgs",	two(0xf048, 0x000c),	two(0xfff8, 0xffff),	"DsBw"},
{"pdbic",	two(0xf048, 0x000b),	two(0xfff8, 0xffff),	"DsBw"},
{"pdbis",	two(0xf048, 0x000a),	two(0xfff8, 0xffff),	"DsBw"},
{"pdblc",	two(0xf048, 0x0003),	two(0xfff8, 0xffff),	"DsBw"},
{"pdbls",	two(0xf048, 0x0002),	two(0xfff8, 0xffff),	"DsBw"},
{"pdbsc",	two(0xf048, 0x0005),	two(0xfff8, 0xffff),	"DsBw"},
{"pdbss",	two(0xf048, 0x0004),	two(0xfff8, 0xffff),	"DsBw"},
{"pdbwc",	two(0xf048, 0x0009),	two(0xfff8, 0xffff),	"DsBw"},
{"pdbws",	two(0xf048, 0x0008),	two(0xfff8, 0xffff),	"DsBw"},

{"pflusha",	two(0xf000, 0x2400),	two(0xffff, 0xffff),	"" },

{"pflush",	two(0xf000, 0x3010),	two(0xffc0, 0xfe10),	"T3T9" },
{"pflush",	two(0xf000, 0x3810),	two(0xffc0, 0xfe10),	"T3T9&s" },
{"pflush",	two(0xf000, 0x3008),	two(0xffc0, 0xfe18),	"D3T9" },
{"pflush",	two(0xf000, 0x3808),	two(0xffc0, 0xfe18),	"D3T9&s" },
{"pflush",	two(0xf000, 0x3000),	two(0xffc0, 0xfe1e),	"f3T9" },
{"pflush",	two(0xf000, 0x3800),	two(0xffc0, 0xfe1e),	"f3T9&s" },

{"pflushs",	two(0xf000, 0x3410),	two(0xfff8, 0xfe10),	"T3T9" },
{"pflushs",	two(0xf000, 0x3c00),	two(0xfff8, 0xfe00),	"T3T9&s" },
{"pflushs",	two(0xf000, 0x3408),	two(0xfff8, 0xfe18),	"D3T9" },
{"pflushs",	two(0xf000, 0x3c08),	two(0xfff8, 0xfe18),	"D3T9&s" },
{"pflushs",	two(0xf000, 0x3400),	two(0xfff8, 0xfe1e),	"f3T9" },
{"pflushs",	two(0xf000, 0x3c00),	two(0xfff8, 0xfe1e),	"f3T9&s"},

{"pflushr",	two(0xf000, 0xa000),	two(0xffc0, 0xffff),	"|s" },

{"ploadr",	two(0xf000, 0x2210),	two(0xffc0, 0xfff0),	"T3&s" },
{"ploadr",	two(0xf000, 0x2208),	two(0xffc0, 0xfff8),	"D3&s" },
{"ploadr",	two(0xf000, 0x2200),	two(0xffc0, 0xfffe),	"f3&s" },
{"ploadw",	two(0xf000, 0x2010),	two(0xffc0, 0xfff0),	"T3&s" },
{"ploadw",	two(0xf000, 0x2008),	two(0xffc0, 0xfff8),	"D3&s" },
{"ploadw",	two(0xf000, 0x2000),	two(0xffc0, 0xfffe),	"f3&s" },

    /* TC, CRP, DRP, SRP, CAL, VAL, SCC, AC */
{"pmove",	two(0xf000, 0x4000),	two(0xffc0, 0xe3ff),	"*sP8" },
{"pmove",	two(0xf000, 0x4200),	two(0xffc0, 0xe3ff),	"P8%s" },
{"pmove",	two(0xf000, 0x4000),	two(0xffc0, 0xe3ff),	"|sW8" },
{"pmove",	two(0xf000, 0x4200),	two(0xffc0, 0xe3ff),	"W8~s" },

    /* BADx, BACx */
{"pmove",	two(0xf000, 0x6200),	two(0xffc0, 0xe3e3),	"*sX3" },
{"pmove",	two(0xf000, 0x6000),	two(0xffc0, 0xe3e3),	"X3%s" },

    /* PSR, PCSR */
    /* {"pmove",	two(0xf000, 0x6100),	two(oxffc0, oxffff),	"*sZ8" }, */
{"pmove",	two(0xf000, 0x6000),	two(0xffc0, 0xffff),	"*sY8" },
{"pmove",	two(0xf000, 0x6200),	two(0xffc0, 0xffff),	"Y8%s" },
{"pmove",	two(0xf000, 0x6600),	two(0xffc0, 0xffff),	"Z8%s" },

{"prestore",	one(0xf140),		one(0xffc0),		"&s"},
{"prestore",	one(0xf158),		one(0xfff8),		"+s"},
{"psave",	one(0xf100),		one(0xffc0),		"&s"},
{"psave",	one(0xf100),		one(0xffc0),		"+s"},

{"psac",	two(0xf040, 0x0007),	two(0xffc0, 0xffff),	"@s"},
{"psas",	two(0xf040, 0x0006),	two(0xffc0, 0xffff),	"@s"},
{"psbc",	two(0xf040, 0x0001),	two(0xffc0, 0xffff),	"@s"},
{"psbs",	two(0xf040, 0x0000),	two(0xffc0, 0xffff),	"@s"},
{"pscc",	two(0xf040, 0x000f),	two(0xffc0, 0xffff),	"@s"},
{"pscs",	two(0xf040, 0x000e),	two(0xffc0, 0xffff),	"@s"},
{"psgc",	two(0xf040, 0x000d),	two(0xffc0, 0xffff),	"@s"},
{"psgs",	two(0xf040, 0x000c),	two(0xffc0, 0xffff),	"@s"},
{"psic",	two(0xf040, 0x000b),	two(0xffc0, 0xffff),	"@s"},
{"psis",	two(0xf040, 0x000a),	two(0xffc0, 0xffff),	"@s"},
{"pslc",	two(0xf040, 0x0003),	two(0xffc0, 0xffff),	"@s"},
{"psls",	two(0xf040, 0x0002),	two(0xffc0, 0xffff),	"@s"},
{"pssc",	two(0xf040, 0x0005),	two(0xffc0, 0xffff),	"@s"},
{"psss",	two(0xf040, 0x0004),	two(0xffc0, 0xffff),	"@s"},
{"pswc",	two(0xf040, 0x0009),	two(0xffc0, 0xffff),	"@s"},
{"psws",	two(0xf040, 0x0008),	two(0xffc0, 0xffff),	"@s"},

{"ptestr",	two(0xf000, 0x8210),	two(0xffc0, 0xe3f0),	"T3&sQ8" },
{"ptestr",	two(0xf000, 0x8310),	two(0xffc0, 0xe310),	"T3&sQ8A9" },
{"ptestr",	two(0xf000, 0x8208),	two(0xffc0, 0xe3f8),	"D3&sQ8" },
{"ptestr",	two(0xf000, 0x8308),	two(0xffc0, 0xe318),	"D3&sQ8A9" },
{"ptestr",	two(0xf000, 0x8200),	two(0xffc0, 0xe3fe),	"f3&sQ8" },
{"ptestr",	two(0xf000, 0x8300),	two(0xffc0, 0xe31e),	"f3&sQ8A9" },

{"ptestw",	two(0xf000, 0x8010),	two(0xffc0, 0xe3f0),	"T3&sQ8" },
{"ptestw",	two(0xf000, 0x8110),	two(0xffc0, 0xe310),	"T3&sQ8A9" },
{"ptestw",	two(0xf000, 0x8008),	two(0xffc0, 0xe3f8),	"D3&sQ8" },
{"ptestw",	two(0xf000, 0x8108),	two(0xffc0, 0xe318),	"D3&sQ8A9" },
{"ptestw",	two(0xf000, 0x8000),	two(0xffc0, 0xe3fe),	"f3&sQ8" },
{"ptestw",	two(0xf000, 0x8100),	two(0xffc0, 0xe31e),	"f3&sQ8A9" },

{"ptrapacw",	two(0xf07a, 0x0007),	two(0xffff, 0xffff),	"#w"},
{"ptrapacl",	two(0xf07b, 0x0007),	two(0xffff, 0xffff),	"#l"},
{"ptrapac",	two(0xf07c, 0x0007),	two(0xffff, 0xffff),	""},

{"ptrapasw",	two(0xf07a, 0x0006),	two(0xffff, 0xffff),	"#w"},
{"ptrapasl",	two(0xf07b, 0x0006),	two(0xffff, 0xffff),	"#l"},
{"ptrapas",	two(0xf07c, 0x0006),	two(0xffff, 0xffff),	""},

{"ptrapbcw",	two(0xf07a, 0x0001),	two(0xffff, 0xffff),	"#w"},
{"ptrapbcl",	two(0xf07b, 0x0001),	two(0xffff, 0xffff),	"#l"},
{"ptrapbc",	two(0xf07c, 0x0001),	two(0xffff, 0xffff),	""},

{"ptrapbsw",	two(0xf07a, 0x0000),	two(0xffff, 0xffff),	"#w"},
{"ptrapbsl",	two(0xf07b, 0x0000),	two(0xffff, 0xffff),	"#l"},
{"ptrapbs",	two(0xf07c, 0x0000),	two(0xffff, 0xffff),	""},

{"ptrapccw",	two(0xf07a, 0x000f),	two(0xffff, 0xffff),	"#w"},
{"ptrapccl",	two(0xf07b, 0x000f),	two(0xffff, 0xffff),	"#l"},
{"ptrapcc",	two(0xf07c, 0x000f),	two(0xffff, 0xffff),	""},

{"ptrapcsw",	two(0xf07a, 0x000e),	two(0xffff, 0xffff),	"#w"},
{"ptrapcsl",	two(0xf07b, 0x000e),	two(0xffff, 0xffff),	"#l"},
{"ptrapcs",	two(0xf07c, 0x000e),	two(0xffff, 0xffff),	""},

{"ptrapgcw",	two(0xf07a, 0x000d),	two(0xffff, 0xffff),	"#w"},
{"ptrapgcl",	two(0xf07b, 0x000d),	two(0xffff, 0xffff),	"#l"},
{"ptrapgc",	two(0xf07c, 0x000d),	two(0xffff, 0xffff),	""},

{"ptrapgsw",	two(0xf07a, 0x000c),	two(0xffff, 0xffff),	"#w"},
{"ptrapgsl",	two(0xf07b, 0x000c),	two(0xffff, 0xffff),	"#l"},
{"ptrapgs",	two(0xf07c, 0x000c),	two(0xffff, 0xffff),	""},

{"ptrapicw",	two(0xf07a, 0x000b),	two(0xffff, 0xffff),	"#w"},
{"ptrapicl",	two(0xf07b, 0x000b),	two(0xffff, 0xffff),	"#l"},
{"ptrapic",	two(0xf07c, 0x000b),	two(0xffff, 0xffff),	""},

{"ptrapisw",	two(0xf07a, 0x000a),	two(0xffff, 0xffff),	"#w"},
{"ptrapisl",	two(0xf07b, 0x000a),	two(0xffff, 0xffff),	"#l"},
{"ptrapis",	two(0xf07c, 0x000a),	two(0xffff, 0xffff),	""},

{"ptraplcw",	two(0xf07a, 0x0003),	two(0xffff, 0xffff),	"#w"},
{"ptraplcl",	two(0xf07b, 0x0003),	two(0xffff, 0xffff),	"#l"},
{"ptraplc",	two(0xf07c, 0x0003),	two(0xffff, 0xffff),	""},

{"ptraplsw",	two(0xf07a, 0x0002),	two(0xffff, 0xffff),	"#w"},
{"ptraplsl",	two(0xf07b, 0x0002),	two(0xffff, 0xffff),	"#l"},
{"ptrapls",	two(0xf07c, 0x0002),	two(0xffff, 0xffff),	""},

{"ptrapscw",	two(0xf07a, 0x0005),	two(0xffff, 0xffff),	"#w"},
{"ptrapscl",	two(0xf07b, 0x0005),	two(0xffff, 0xffff),	"#l"},
{"ptrapsc",	two(0xf07c, 0x0005),	two(0xffff, 0xffff),	""},

{"ptrapssw",	two(0xf07a, 0x0004),	two(0xffff, 0xffff),	"#w"},
{"ptrapssl",	two(0xf07b, 0x0004),	two(0xffff, 0xffff),	"#l"},
{"ptrapss",	two(0xf07c, 0x0004),	two(0xffff, 0xffff),	""},

{"ptrapwcw",	two(0xf07a, 0x0009),	two(0xffff, 0xffff),	"#w"},
{"ptrapwcl",	two(0xf07b, 0x0009),	two(0xffff, 0xffff),	"#l"},
{"ptrapwc",	two(0xf07c, 0x0009),	two(0xffff, 0xffff),	""},

{"ptrapwsw",	two(0xf07a, 0x0008),	two(0xffff, 0xffff),	"#w"},
{"ptrapwsl",	two(0xf07b, 0x0008),	two(0xffff, 0xffff),	"#l"},
{"ptrapws",	two(0xf07c, 0x0008),	two(0xffff, 0xffff),	""},

{"pvalid",	two(0xf000, 0x2800),	two(0xffc0, 0xffff),	"Vs&s"},
{"pvalid",	two(0xf000, 0x2c00),	two(0xffc0, 0xfff8),	"A3&s" },

#endif /* m68851 */
