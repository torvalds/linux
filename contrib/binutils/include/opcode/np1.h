/* Print GOULD NPL instructions for GDB, the GNU debugger.
   Copyright 1986, 1987, 1989, 1991 Free Software Foundation, Inc.

This file is part of GDB.

GDB is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 1, or (at your option)
any later version.

GDB is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GDB; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

struct gld_opcode
{
  char *name;
  unsigned long opcode;
  unsigned long mask;
  char *args;
  int length;
};

/* We store four bytes of opcode for all opcodes because that
   is the most any of them need.  The actual length of an instruction
   is always at least 2 bytes, and at most four.  The length of the
   instruction is based on the opcode.

   The mask component is a mask saying which bits must match
   particular opcode in order for an instruction to be an instance
   of that opcode.

   The args component is a string containing characters
   that are used to format the arguments to the instruction. */

/* Kinds of operands:
   r  Register in first field
   R  Register in second field
   b  Base register in first field
   B  Base register in second field
   v  Vector register in first field
   V  Vector register in first field
   A  Optional address register (base register)
   X  Optional index register
   I  Immediate data (16bits signed)
   O  Offset field (16bits signed)
   h  Offset field (15bits signed)
   d  Offset field (14bits signed)
   S  Shift count field

   any other characters are printed as is...
*/

/* The assembler requires that this array be sorted as follows:
   all instances of the same mnemonic must be consecutive.
   All instances of the same mnemonic with the same number of operands
   must be consecutive.
 */
struct gld_opcode gld_opcodes[] =
{
{ "lb",		0xb4080000,	0xfc080000,	"r,xOA,X",	4 },
{ "lnb",	0xb8080000,	0xfc080000,	"r,xOA,X",	4 },
{ "lbs",	0xec080000,	0xfc080000,	"r,xOA,X",	4 },
{ "lh",		0xb4000001,	0xfc080001,	"r,xOA,X",	4 },
{ "lnh",	0xb8000001,	0xfc080001,	"r,xOA,X",	4 },
{ "lw",		0xb4000000,	0xfc080000,	"r,xOA,X",	4 },
{ "lnw",	0xb8000000,	0xfc080000,	"r,xOA,X",	4 },
{ "ld",		0xb4000002,	0xfc080002,	"r,xOA,X",	4 },
{ "lnd",	0xb8000002,	0xfc080002,	"r,xOA,X",	4 },
{ "li",		0xf8000000,	0xfc7f0000,	"r,I",		4 },
{ "lpa",	0x50080000,	0xfc080000,	"r,xOA,X",	4 },
{ "la",		0x50000000,	0xfc080000,	"r,xOA,X",	4 },
{ "labr",	0x58080000,	0xfc080000,	"b,xOA,X",	4 },
{ "lbp",	0x90080000,	0xfc080000,	"r,xOA,X",	4 },
{ "lhp",	0x90000001,	0xfc080001,	"r,xOA,X",	4 },
{ "lwp",	0x90000000,	0xfc080000,	"r,xOA,X",	4 },
{ "ldp",	0x90000002,	0xfc080002,	"r,xOA,X",	4 },
{ "suabr",	0x58000000,	0xfc080000,	"b,xOA,X",	4 },
{ "lf",		0xbc000000,	0xfc080000,	"r,xOA,X",	4 },
{ "lfbr",	0xbc080000,	0xfc080000,	"b,xOA,X",	4 },
{ "lwbr",	0x5c000000,	0xfc080000,	"b,xOA,X",	4 },
{ "stb",	0xd4080000,	0xfc080000,	"r,xOA,X",	4 },
{ "sth",	0xd4000001,	0xfc080001,	"r,xOA,X",	4 },
{ "stw",	0xd4000000,	0xfc080000,	"r,xOA,X",	4 },
{ "std",	0xd4000002,	0xfc080002,	"r,xOA,X",	4 },
{ "stf",	0xdc000000,	0xfc080000,	"r,xOA,X",	4 },
{ "stfbr",	0xdc080000,	0xfc080000,	"b,xOA,X",	4 },
{ "stwbr",	0x54000000,	0xfc080000,	"b,xOA,X",	4 },
{ "zmb",	0xd8080000,	0xfc080000,	"r,xOA,X",	4 },
{ "zmh",	0xd8000001,	0xfc080001,	"r,xOA,X",	4 },
{ "zmw",	0xd8000000,	0xfc080000,	"r,xOA,X",	4 },
{ "zmd",	0xd8000002,	0xfc080002,	"r,xOA,X",	4 },
{ "stbp",	0x94080000,	0xfc080000,	"r,xOA,X",	4 },
{ "sthp",	0x94000001,	0xfc080001,	"r,xOA,X",	4 },
{ "stwp",	0x94000000,	0xfc080000,	"r,xOA,X",	4 },
{ "stdp",	0x94000002,	0xfc080002,	"r,xOA,X",	4 },
{ "lil",	0xf80b0000,	0xfc7f0000,	"r,D",		4 },
{ "lwsl1",	0xec000000,	0xfc080000,	"r,xOA,X",	4 },
{ "lwsl2",	0xfc000000,	0xfc080000,	"r,xOA,X",	4 },
{ "lwsl3",	0xfc080000,	0xfc080000,	"r,xOA,X",	4 },

{ "lvb",	0xb0080000,	0xfc080000,	"v,xOA,X",	4 },
{ "lvh",	0xb0000001,	0xfc080001,	"v,xOA,X",	4 },
{ "lvw",	0xb0000000,	0xfc080000,	"v,xOA,X",	4 },
{ "lvd",	0xb0000002,	0xfc080002,	"v,xOA,X",	4 },
{ "liv",	0x3c040000,	0xfc0f0000,	"v,R",		2 },
{ "livf",	0x3c080000,	0xfc0f0000,	"v,R",		2 },
{ "stvb",	0xd0080000,	0xfc080000,	"v,xOA,X",	4 },
{ "stvh",	0xd0000001,	0xfc080001,	"v,xOA,X",	4 },
{ "stvw",	0xd0000000,	0xfc080000,	"v,xOA,X",	4 },
{ "stvd",	0xd0000002,	0xfc080002,	"v,xOA,X",	4 },

{ "trr",	0x2c000000,	0xfc0f0000,	"r,R",		2 },
{ "trn",	0x2c040000,	0xfc0f0000,	"r,R",		2 },
{ "trnd",	0x2c0c0000,	0xfc0f0000,	"r,R",		2 },
{ "trabs",	0x2c010000,	0xfc0f0000,	"r,R",		2 },
{ "trabsd",	0x2c090000,	0xfc0f0000,	"r,R",		2 },
{ "trc",	0x2c030000,	0xfc0f0000,	"r,R",		2 },
{ "xcr",	0x28040000,	0xfc0f0000,	"r,R",		2 },
{ "cxcr",	0x2c060000,	0xfc0f0000,	"r,R",		2 },
{ "cxcrd",	0x2c0e0000,	0xfc0f0000,	"r,R",		2 },
{ "tbrr",	0x2c020000,	0xfc0f0000,	"r,B",		2 },
{ "trbr",	0x28030000,	0xfc0f0000,	"b,R",		2 },
{ "xcbr",	0x28020000,	0xfc0f0000,	"b,B",		2 },
{ "tbrbr",	0x28010000,	0xfc0f0000,	"b,B",		2 },

{ "trvv",	0x28050000,	0xfc0f0000,	"v,V",		2 },
{ "trvvn",	0x2c050000,	0xfc0f0000,	"v,V",		2 },
{ "trvvnd",	0x2c0d0000,	0xfc0f0000,	"v,V",		2 },
{ "trvab",	0x2c070000,	0xfc0f0000,	"v,V",		2 },
{ "trvabd",	0x2c0f0000,	0xfc0f0000,	"v,V",		2 },
{ "cmpv",	0x14060000,	0xfc0f0000,	"v,V",		2 },
{ "expv",	0x14070000,	0xfc0f0000,	"v,V",		2 },
{ "mrvvlt",	0x10030000,	0xfc0f0000,	"v,V",		2 },
{ "mrvvle",	0x10040000,	0xfc0f0000,	"v,V",		2 },
{ "mrvvgt",	0x14030000,	0xfc0f0000,	"v,V",		2 },
{ "mrvvge",	0x14040000,	0xfc0f0000,	"v,V",		2 },
{ "mrvveq",	0x10050000,	0xfc0f0000,	"v,V",		2 },
{ "mrvvne",	0x10050000,	0xfc0f0000,	"v,V",		2 },
{ "mrvrlt",	0x100d0000,	0xfc0f0000,	"v,R",		2 },
{ "mrvrle",	0x100e0000,	0xfc0f0000,	"v,R",		2 },
{ "mrvrgt",	0x140d0000,	0xfc0f0000,	"v,R",		2 },
{ "mrvrge",	0x140e0000,	0xfc0f0000,	"v,R",		2 },
{ "mrvreq",	0x100f0000,	0xfc0f0000,	"v,R",		2 },
{ "mrvrne",	0x140f0000,	0xfc0f0000,	"v,R",		2 },
{ "trvr",	0x140b0000,	0xfc0f0000,	"r,V",		2 },
{ "trrv",	0x140c0000,	0xfc0f0000,	"v,R",		2 },

{ "bu",		0x40000000,	0xff880000,	"xOA,X",	4 },
{ "bns",	0x70080000,	0xff880000,	"xOA,X",	4 },
{ "bnco",	0x70880000,	0xff880000,	"xOA,X",	4 },
{ "bge",	0x71080000,	0xff880000,	"xOA,X",	4 },
{ "bne",	0x71880000,	0xff880000,	"xOA,X",	4 },
{ "bunge",	0x72080000,	0xff880000,	"xOA,X",	4 },
{ "bunle",	0x72880000,	0xff880000,	"xOA,X",	4 },
{ "bgt",	0x73080000,	0xff880000,	"xOA,X",	4 },
{ "bnany",	0x73880000,	0xff880000,	"xOA,X",	4 },
{ "bs"	,	0x70000000,	0xff880000,	"xOA,X",	4 },
{ "bco",	0x70800000,	0xff880000,	"xOA,X",	4 },
{ "blt",	0x71000000,	0xff880000,	"xOA,X",	4 },
{ "beq",	0x71800000,	0xff880000,	"xOA,X",	4 },
{ "buge",	0x72000000,	0xff880000,	"xOA,X",	4 },
{ "bult",	0x72800000,	0xff880000,	"xOA,X",	4 },
{ "ble",	0x73000000,	0xff880000,	"xOA,X",	4 },
{ "bany",	0x73800000,	0xff880000,	"xOA,X",	4 },
{ "brlnk",	0x44000000,	0xfc080000,	"r,xOA,X",	4 },
{ "bib",	0x48000000,	0xfc080000,	"r,xOA,X",	4 },
{ "bih",	0x48080000,	0xfc080000,	"r,xOA,X",	4 },
{ "biw",	0x4c000000,	0xfc080000,	"r,xOA,X",	4 },
{ "bid",	0x4c080000,	0xfc080000,	"r,xOA,X",	4 },
{ "bivb",	0x60000000,	0xfc080000,	"r,xOA,X",	4 },
{ "bivh",	0x60080000,	0xfc080000,	"r,xOA,X",	4 },
{ "bivw",	0x64000000,	0xfc080000,	"r,xOA,X",	4 },
{ "bivd",	0x64080000,	0xfc080000,	"r,xOA,X",	4 },
{ "bvsb",	0x68000000,	0xfc080000,	"r,xOA,X",	4 },
{ "bvsh",	0x68080000,	0xfc080000,	"r,xOA,X",	4 },
{ "bvsw",	0x6c000000,	0xfc080000,	"r,xOA,X",	4 },
{ "bvsd",	0x6c080000,	0xfc080000,	"r,xOA,X",	4 },

{ "camb",	0x80080000,	0xfc080000,	"r,xOA,X",	4 },
{ "camh",	0x80000001,	0xfc080001,	"r,xOA,X",	4 },
{ "camw",	0x80000000,	0xfc080000,	"r,xOA,X",	4 },
{ "camd",	0x80000002,	0xfc080002,	"r,xOA,X",	4 },
{ "car",	0x10000000,	0xfc0f0000,	"r,R",		2 },
{ "card",	0x14000000,	0xfc0f0000,	"r,R",		2 },
{ "ci",		0xf8050000,	0xfc7f0000,	"r,I",		4 },
{ "chkbnd",	0x5c080000,	0xfc080000,	"r,xOA,X",	4 },

{ "cavv",	0x10010000,	0xfc0f0000,	"v,V",		2 },
{ "cavr",	0x10020000,	0xfc0f0000,	"v,R",		2 },
{ "cavvd",	0x10090000,	0xfc0f0000,	"v,V",		2 },
{ "cavrd",	0x100b0000,	0xfc0f0000,	"v,R",		2 },

{ "anmb",	0x84080000,	0xfc080000,	"r,xOA,X",	4 },
{ "anmh",	0x84000001,	0xfc080001,	"r,xOA,X",	4 },
{ "anmw",	0x84000000,	0xfc080000,	"r,xOA,X",	4 },
{ "anmd",	0x84000002,	0xfc080002,	"r,xOA,X",	4 },
{ "anr",	0x04000000,	0xfc0f0000,	"r,R",		2 },
{ "ani",	0xf8080000,	0xfc7f0000,	"r,I",		4 },
{ "ormb",	0xb8080000,	0xfc080000,	"r,xOA,X",	4 },
{ "ormh",	0xb8000001,	0xfc080001,	"r,xOA,X",	4 },
{ "ormw",	0xb8000000,	0xfc080000,	"r,xOA,X",	4 },
{ "ormd",	0xb8000002,	0xfc080002,	"r,xOA,X",	4 },
{ "orr",	0x08000000,	0xfc0f0000,	"r,R",		2 },
{ "oi",		0xf8090000,	0xfc7f0000,	"r,I",		4 },
{ "eomb",	0x8c080000,	0xfc080000,	"r,xOA,X",	4 },
{ "eomh",	0x8c000001,	0xfc080001,	"r,xOA,X",	4 },
{ "eomw",	0x8c000000,	0xfc080000,	"r,xOA,X",	4 },
{ "eomd",	0x8c000002,	0xfc080002,	"r,xOA,X",	4 },
{ "eor",	0x0c000000,	0xfc0f0000,	"r,R",		2 },
{ "eoi",	0xf80a0000,	0xfc7f0000,	"r,I",		4 },

{ "anvv",	0x04010000,	0xfc0f0000,	"v,V",		2 },
{ "anvr",	0x04020000,	0xfc0f0000,	"v,R",		2 },
{ "orvv",	0x08010000,	0xfc0f0000,	"v,V",		2 },
{ "orvr",	0x08020000,	0xfc0f0000,	"v,R",		2 },
{ "eovv",	0x0c010000,	0xfc0f0000,	"v,V",		2 },
{ "eovr",	0x0c020000,	0xfc0f0000,	"v,R",		2 },

{ "sacz",	0x100c0000,	0xfc0f0000,	"r,R",		2 },
{ "sla",	0x1c400000,	0xfc600000,	"r,S",		2 },
{ "sll",	0x1c600000,	0xfc600000,	"r,S",		2 },
{ "slc",	0x24400000,	0xfc600000,	"r,S",		2 },
{ "slad",	0x20400000,	0xfc600000,	"r,S",		2 },
{ "slld",	0x20600000,	0xfc600000,	"r,S",		2 },
{ "sra",	0x1c000000,	0xfc600000,	"r,S",		2 },
{ "srl",	0x1c200000,	0xfc600000,	"r,S",		2 },
{ "src",	0x24000000,	0xfc600000,	"r,S",		2 },
{ "srad",	0x20000000,	0xfc600000,	"r,S",		2 },
{ "srld",	0x20200000,	0xfc600000,	"r,S",		2 },
{ "sda",	0x3c030000,	0xfc0f0000,	"r,R",		2 },
{ "sdl",	0x3c020000,	0xfc0f0000,	"r,R",		2 },
{ "sdc",	0x3c010000,	0xfc0f0000,	"r,R",		2 },
{ "sdad",	0x3c0b0000,	0xfc0f0000,	"r,R",		2 },
{ "sdld",	0x3c0a0000,	0xfc0f0000,	"r,R",		2 },

{ "svda",	0x3c070000,	0xfc0f0000,	"v,R",		2 },
{ "svdl",	0x3c060000,	0xfc0f0000,	"v,R",		2 },
{ "svdc",	0x3c050000,	0xfc0f0000,	"v,R",		2 },
{ "svdad",	0x3c0e0000,	0xfc0f0000,	"v,R",		2 },
{ "svdld",	0x3c0d0000,	0xfc0f0000,	"v,R",		2 },

{ "sbm",	0xac080000,	0xfc080000,	"f,xOA,X",	4 },
{ "zbm",	0xac000000,	0xfc080000,	"f,xOA,X",	4 },
{ "tbm",	0xa8080000,	0xfc080000,	"f,xOA,X",	4 },
{ "incmb",	0xa0000000,	0xfc080000,	"xOA,X",	4 },
{ "incmh",	0xa0080000,	0xfc080000,	"xOA,X",	4 },
{ "incmw",	0xa4000000,	0xfc080000,	"xOA,X",	4 },
{ "incmd",	0xa4080000,	0xfc080000,	"xOA,X",	4 },
{ "sbmd",	0x7c080000,	0xfc080000,	"r,xOA,X",	4 },
{ "zbmd",	0x7c000000,	0xfc080000,	"r,xOA,X",	4 },
{ "tbmd",	0x78080000,	0xfc080000,	"r,xOA,X",	4 },

{ "ssm",	0x9c080000,	0xfc080000,	"f,xOA,X",	4 },
{ "zsm",	0x9c000000,	0xfc080000,	"f,xOA,X",	4 },
{ "tsm",	0x98080000,	0xfc080000,	"f,xOA,X",	4 },

{ "admb",	0xc8080000,	0xfc080000,	"r,xOA,X",	4 },
{ "admh",	0xc8000001,	0xfc080001,	"r,xOA,X",	4 },
{ "admw",	0xc8000000,	0xfc080000,	"r,xOA,X",	4 },
{ "admd",	0xc8000002,	0xfc080002,	"r,xOA,X",	4 },
{ "adr",	0x38000000,	0xfc0f0000,	"r,R",		2 },
{ "armb",	0xe8080000,	0xfc080000,	"r,xOA,X",	4 },
{ "armh",	0xe8000001,	0xfc080001,	"r,xOA,X",	4 },
{ "armw",	0xe8000000,	0xfc080000,	"r,xOA,X",	4 },
{ "armd",	0xe8000002,	0xfc080002,	"r,xOA,X",	4 },
{ "adi",	0xf8010000,	0xfc0f0000,	"r,I",		4 },
{ "sumb",	0xcc080000,	0xfc080000,	"r,xOA,X",	4 },
{ "sumh",	0xcc000001,	0xfc080001,	"r,xOA,X",	4 },
{ "sumw",	0xcc000000,	0xfc080000,	"r,xOA,X",	4 },
{ "sumd",	0xcc000002,	0xfc080002,	"r,xOA,X",	4 },
{ "sur",	0x3c000000,	0xfc0f0000,	"r,R",		2 },
{ "sui",	0xf8020000,	0xfc0f0000,	"r,I",		4 },
{ "mpmb",	0xc0080000,	0xfc080000,	"r,xOA,X",	4 },
{ "mpmh",	0xc0000001,	0xfc080001,	"r,xOA,X",	4 },
{ "mpmw",	0xc0000000,	0xfc080000,	"r,xOA,X",	4 },
{ "mpr",	0x38020000,	0xfc0f0000,	"r,R",		2 },
{ "mprd",	0x3c0f0000,	0xfc0f0000,	"r,R",		2 },
{ "mpi",	0xf8030000,	0xfc0f0000,	"r,I",		4 },
{ "dvmb",	0xc4080000,	0xfc080000,	"r,xOA,X",	4 },
{ "dvmh",	0xc4000001,	0xfc080001,	"r,xOA,X",	4 },
{ "dvmw",	0xc4000000,	0xfc080000,	"r,xOA,X",	4 },
{ "dvr",	0x380a0000,	0xfc0f0000,	"r,R",		2 },
{ "dvi",	0xf8040000,	0xfc0f0000,	"r,I",		4 },
{ "exs",	0x38080000,	0xfc0f0000,	"r,R",		2 },

{ "advv",	0x30000000,	0xfc0f0000,	"v,V",		2 },
{ "advvd",	0x30080000,	0xfc0f0000,	"v,V",		2 },
{ "adrv",	0x34000000,	0xfc0f0000,	"v,R",		2 },
{ "adrvd",	0x34080000,	0xfc0f0000,	"v,R",		2 },
{ "suvv",	0x30010000,	0xfc0f0000,	"v,V",		2 },
{ "suvvd",	0x30090000,	0xfc0f0000,	"v,V",		2 },
{ "surv",	0x34010000,	0xfc0f0000,	"v,R",		2 },
{ "survd",	0x34090000,	0xfc0f0000,	"v,R",		2 },
{ "mpvv",	0x30020000,	0xfc0f0000,	"v,V",		2 },
{ "mprv",	0x34020000,	0xfc0f0000,	"v,R",		2 },

{ "adfw",	0xe0080000,	0xfc080000,	"r,xOA,X",	4 },
{ "adfd",	0xe0080002,	0xfc080002,	"r,xOA,X",	4 },
{ "adrfw",	0x38010000,	0xfc0f0000,	"r,R",		2 },
{ "adrfd",	0x38090000,	0xfc0f0000,	"r,R",		2 },
{ "surfw",	0xe0000000,	0xfc080000,	"r,xOA,X",	4 },
{ "surfd",	0xe0000002,	0xfc080002,	"r,xOA,X",	4 },
{ "surfw",	0x38030000,	0xfc0f0000,	"r,R",		2 },
{ "surfd",	0x380b0000,	0xfc0f0000,	"r,R",		2 },
{ "mpfw",	0xe4080000,	0xfc080000,	"r,xOA,X",	4 },
{ "mpfd",	0xe4080002,	0xfc080002,	"r,xOA,X",	4 },
{ "mprfw",	0x38060000,	0xfc0f0000,	"r,R",		2 },
{ "mprfd",	0x380e0000,	0xfc0f0000,	"r,R",		2 },
{ "rfw",	0xe4000000,	0xfc080000,	"r,xOA,X",	4 },
{ "rfd",	0xe4000002,	0xfc080002,	"r,xOA,X",	4 },
{ "rrfw",	0x0c0e0000,	0xfc0f0000,	"r",		2 },
{ "rrfd",	0x0c0f0000,	0xfc0f0000,	"r",		2 },

{ "advvfw",	0x30040000,	0xfc0f0000,	"v,V",		2 },
{ "advvfd",	0x300c0000,	0xfc0f0000,	"v,V",		2 },
{ "adrvfw",	0x34040000,	0xfc0f0000,	"v,R",		2 },
{ "adrvfd",	0x340c0000,	0xfc0f0000,	"v,R",		2 },
{ "suvvfw",	0x30050000,	0xfc0f0000,	"v,V",		2 },
{ "suvvfd",	0x300d0000,	0xfc0f0000,	"v,V",		2 },
{ "survfw",	0x34050000,	0xfc0f0000,	"v,R",		2 },
{ "survfd",	0x340d0000,	0xfc0f0000,	"v,R",		2 },
{ "mpvvfw",	0x30060000,	0xfc0f0000,	"v,V",		2 },
{ "mpvvfd",	0x300e0000,	0xfc0f0000,	"v,V",		2 },
{ "mprvfw",	0x34060000,	0xfc0f0000,	"v,R",		2 },
{ "mprvfd",	0x340e0000,	0xfc0f0000,	"v,R",		2 },
{ "rvfw",	0x30070000,	0xfc0f0000,	"v",		2 },
{ "rvfd",	0x300f0000,	0xfc0f0000,	"v",		2 },

{ "fltw",	0x38070000,	0xfc0f0000,	"r,R",		2 },
{ "fltd",	0x380f0000,	0xfc0f0000,	"r,R",		2 },
{ "fixw",	0x38050000,	0xfc0f0000,	"r,R",		2 },
{ "fixd",	0x380d0000,	0xfc0f0000,	"r,R",		2 },
{ "cfpds",	0x3c090000,	0xfc0f0000,	"r,R",		2 },

{ "fltvw",	0x080d0000,	0xfc0f0000,	"v,V",		2 },
{ "fltvd",	0x080f0000,	0xfc0f0000,	"v,V",		2 },
{ "fixvw",	0x080c0000,	0xfc0f0000,	"v,V",		2 },
{ "fixvd",	0x080e0000,	0xfc0f0000,	"v,V",		2 },
{ "cfpvds",	0x0c0d0000,	0xfc0f0000,	"v,V",		2 },

{ "orvrn",	0x000a0000,	0xfc0f0000,	"r,V",		2 },
{ "andvrn",	0x00080000,	0xfc0f0000,	"r,V",		2 },
{ "frsteq",	0x04090000,	0xfc0f0000,	"r,V",		2 },
{ "sigma",	0x0c080000,	0xfc0f0000,	"r,V",		2 },
{ "sigmad",	0x0c0a0000,	0xfc0f0000,	"r,V",		2 },
{ "sigmf",	0x08080000,	0xfc0f0000,	"r,V",		2 },
{ "sigmfd",	0x080a0000,	0xfc0f0000,	"r,V",		2 },
{ "prodf",	0x04080000,	0xfc0f0000,	"r,V",		2 },
{ "prodfd",	0x040a0000,	0xfc0f0000,	"r,V",		2 },
{ "maxv",	0x10080000,	0xfc0f0000,	"r,V",		2 },
{ "maxvd",	0x100a0000,	0xfc0f0000,	"r,V",		2 },
{ "minv",	0x14080000,	0xfc0f0000,	"r,V",		2 },
{ "minvd",	0x140a0000,	0xfc0f0000,	"r,V",		2 },

{ "lpsd",	0xf0000000,	0xfc080000,	"xOA,X",	4 },
{ "ldc",	0xf0080000,	0xfc080000,	"xOA,X",	4 },
{ "spm",	0x040c0000,	0xfc0f0000,	"r",		2 },
{ "rpm",	0x040d0000,	0xfc0f0000,	"r",		2 },
{ "tritr",	0x00070000,	0xfc0f0000,	"r",		2 },
{ "trrit",	0x00060000,	0xfc0f0000,	"r",		2 },
{ "rpswt",	0x04080000,	0xfc0f0000,	"r",		2 },
{ "exr",	0xf8070000,	0xfc0f0000,	"",		4 },
{ "halt",	0x00000000,	0xfc0f0000,	"",		2 },
{ "wait",	0x00010000,	0xfc0f0000,	"",		2 },
{ "nop",	0x00020000,	0xfc0f0000,	"",		2 },
{ "eiae",	0x00030000,	0xfc0f0000,	"",		2 },
{ "efae",	0x000d0000,	0xfc0f0000,	"",		2 },
{ "diae",	0x000e0000,	0xfc0f0000,	"",		2 },
{ "dfae",	0x000f0000,	0xfc0f0000,	"",		2 },
{ "spvc",	0xf8060000,	0xfc0f0000,	"r,T,N",	4 },
{ "rdsts",	0x00090000,	0xfc0f0000,	"r",		2 },
{ "setcpu",	0x000c0000,	0xfc0f0000,	"r",		2 },
{ "cmc",	0x000b0000,	0xfc0f0000,	"r",		2 },
{ "trrcu",	0x00040000,	0xfc0f0000,	"r",		2 },
{ "attnio",	0x00050000,	0xfc0f0000,	"",		2 },
{ "fudit",	0x28080000,	0xfc0f0000,	"",		2 },
{ "break",	0x28090000,	0xfc0f0000,	"",		2 },
{ "frzss",	0x280a0000,	0xfc0f0000,	"",		2 },
{ "ripi",	0x04040000,	0xfc0f0000,	"r,R",		2 },
{ "xcp",	0x04050000,	0xfc0f0000,	"r",		2 },
{ "block",	0x04060000,	0xfc0f0000,	"",		2 },
{ "unblock",	0x04070000,	0xfc0f0000,	"",		2 },
{ "trsc",	0x08060000,	0xfc0f0000,	"r,R",		2 },
{ "tscr",	0x08070000,	0xfc0f0000,	"r,R",		2 },
{ "fq",		0x04080000,	0xfc0f0000,	"r",		2 },
{ "flupte",	0x2c080000,	0xfc0f0000,	"r",		2 },
{ "rviu",	0x040f0000,	0xfc0f0000,	"",		2 },
{ "ldel",	0x280c0000,	0xfc0f0000,	"r,R",		2 },
{ "ldu",	0x280d0000,	0xfc0f0000,	"r,R",		2 },
{ "stdecc",	0x280b0000,	0xfc0f0000,	"r,R",		2 },
{ "trpc",	0x08040000,	0xfc0f0000,	"r",		2 },
{ "tpcr",	0x08050000,	0xfc0f0000,	"r",		2 },
{ "ghalt",	0x0c050000,	0xfc0f0000,	"r",		2 },
{ "grun",	0x0c040000,	0xfc0f0000,	"",		2 },
{ "tmpr",	0x2c0a0000,	0xfc0f0000,	"r,R",		2 },
{ "trmp",	0x2c0b0000,	0xfc0f0000,	"r,R",		2 },

{ "trrve",	0x28060000,	0xfc0f0000,	"r",		2 },
{ "trver",	0x28070000,	0xfc0f0000,	"r",		2 },
{ "trvlr",	0x280f0000,	0xfc0f0000,	"r",		2 },

{ "linkfl",	0x18000000,	0xfc0f0000,	"r,R",		2 },
{ "linkbl",	0x18020000,	0xfc0f0000,	"r,R",		2 },
{ "linkfp",	0x18010000,	0xfc0f0000,	"r,R",		2 },
{ "linkbp",	0x18030000,	0xfc0f0000,	"r,R",		2 },
{ "linkpl",	0x18040000,	0xfc0f0000,	"r,R",		2 },
{ "ulinkl",	0x18080000,	0xfc0f0000,	"r,R",		2 },
{ "ulinkp",	0x18090000,	0xfc0f0000,	"r,R",		2 },
{ "ulinktl",	0x180a0000,	0xfc0f0000,	"r,R",		2 },
{ "ulinktp",	0x180b0000,	0xfc0f0000,	"r,R",		2 },
};

int numopcodes = sizeof(gld_opcodes) / sizeof(gld_opcodes[0]);

struct gld_opcode *endop = gld_opcodes + sizeof(gld_opcodes) /
		 		sizeof(gld_opcodes[0]);
