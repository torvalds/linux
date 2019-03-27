/* Print GOULD PN (PowerNode) instructions for GDB, the GNU debugger.
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
{ "abm",	0xa0080000,	0xfc080000,	"f,xOA,X",	4 },
{ "abr",	0x18080000,	0xfc0c0000,	"r,f",		2 },
{ "aci",	0xfc770000,	0xfc7f8000,	"r,I",		4 },
{ "adfd",	0xe0080002,	0xfc080002,	"r,xOA,X",	4 },
{ "adfw",	0xe0080000,	0xfc080000,	"r,xOA,X",	4 },
{ "adi",	0xc8010000,	0xfc7f0000,	"r,I",		4 },
{ "admb",	0xb8080000,	0xfc080000,	"r,xOA,X",	4 },
{ "admd",	0xb8000002,	0xfc080002,	"r,xOA,X",	4 },
{ "admh",	0xb8000001,	0xfc080001,	"r,xOA,X",	4 },
{ "admw",	0xb8000000,	0xfc080000,	"r,xOA,X",	4 },
{ "adr",	0x38000000,	0xfc0f0000,	"r,R",		2 },
{ "adrfd",	0x38090000,	0xfc0f0000,	"r,R",		2 },
{ "adrfw",	0x38010000,	0xfc0f0000,	"r,R",		2 },
{ "adrm",	0x38080000,	0xfc0f0000,	"r,R",		2 },
{ "ai", 	0xfc030000,	0xfc07ffff,	"I",		4 },
{ "anmb",	0x84080000,	0xfc080000,	"r,xOA,X",	4 },
{ "anmd",	0x84000002,	0xfc080002,	"r,xOA,X",	4 },
{ "anmh",	0x84000001,	0xfc080001,	"r,xOA,X",	4 },
{ "anmw",	0x84000000,	0xfc080000,	"r,xOA,X",	4 },
{ "anr",	0x04000000,	0xfc0f0000,	"r,R",		2 },
{ "armb",	0xe8080000,	0xfc080000,	"r,xOA,X",	4 },
{ "armd",	0xe8000002,	0xfc080002,	"r,xOA,X",	4 },
{ "armh",	0xe8000001,	0xfc080001,	"r,xOA,X",	4 },
{ "armw",	0xe8000000,	0xfc080000,	"r,xOA,X",	4 },
{ "bcf",	0xf0000000,	0xfc080000,	"I,xOA,X",	4 },
{ "bct",	0xec000000,	0xfc080000,	"I,xOA,X",	4 },
{ "bei",	0x00060000,	0xffff0000,	"",		2 },
{ "bft",	0xf0000000,	0xff880000,	"xOA,X",	4 },
{ "bib",	0xf4000000,	0xfc780000,	"r,xOA",	4 },
{ "bid",	0xf4600000,	0xfc780000,	"r,xOA",	4 },
{ "bih",	0xf4200000,	0xfc780000,	"r,xOA",	4 },
{ "biw",	0xf4400000,	0xfc780000,	"r,xOA",	4 },
{ "bl", 	0xf8800000,	0xff880000,	"xOA,X",	4 },
{ "bsub",	0x5c080000,	0xff8f0000,	"",		2 },
{ "bsubm",	0x28080000,	0xfc080000,	"",		4 },
{ "bu", 	0xec000000,	0xff880000,	"xOA,X",	4 },
{ "call",	0x28080000,	0xfc0f0000,	"",		2 },
{ "callm",	0x5c080000,	0xff880000,	"",		4 },
{ "camb",	0x90080000,	0xfc080000,	"r,xOA,X",	4 },
{ "camd",	0x90000002,	0xfc080002,	"r,xOA,X",	4 },
{ "camh",	0x90000001,	0xfc080001,	"r,xOA,X",	4 },
{ "camw",	0x90000000,	0xfc080000,	"r.xOA,X",	4 },
{ "car",	0x10000000,	0xfc0f0000,	"r,R",		2 },
{ "cd", 	0xfc060000,	0xfc070000,	"r,f",		4 },
{ "cea",	0x000f0000,	0xffff0000,	"",		2 },
{ "ci", 	0xc8050000,	0xfc7f0000,	"r,I",		4 },
{ "cmc",	0x040a0000,	0xfc7f0000,	"r",		2 },
{ "cmmb",	0x94080000,	0xfc080000,	"r,xOA,X",	4 },
{ "cmmd",	0x94000002,	0xfc080002,	"r,xOA,X",	4 },
{ "cmmh",	0x94000001,	0xfc080001,	"r,xOA,X",	4 },
{ "cmmw",	0x94000000,	0xfc080000,	"r,xOA,X",	4 },
{ "cmr",	0x14000000,	0xfc0f0000,	"r,R",		2 },
{ "daci",	0xfc7f0000,	0xfc7f8000,	"r,I",		4 },
{ "dae",	0x000e0000,	0xffff0000,	"",		2 },
{ "dai",	0xfc040000,	0xfc07ffff,	"I",		4 },
{ "dci",	0xfc6f0000,	0xfc7f8000,	"r,I",		4 },
{ "di", 	0xfc010000,	0xfc07ffff,	"I",		4 },
{ "dvfd",	0xe4000002,	0xfc080002,	"r,xOA,X",	4 },
{ "dvfw",	0xe4000000,	0xfc080000,	"r,xOA,X",	4 },
{ "dvi",	0xc8040000,	0xfc7f0000,	"r,I",		4 },
{ "dvmb",	0xc4080000,	0xfc080000,	"r,xOA,X",	4 },
{ "dvmh",	0xc4000001,	0xfc080001,	"r,xOA,X",	4 },
{ "dvmw",	0xc4000000,	0xfc080000,	"r,xOA,X",	4 },
{ "dvr",	0x380a0000,	0xfc0f0000,	"r,R",		2 },
{ "dvrfd",	0x380c0000,	0xfc0f0000,	"r,R",		4 },
{ "dvrfw",	0x38040000,	0xfc0f0000,	"r,xOA,X",	4 },
{ "eae",	0x00080000,	0xffff0000,	"",		2 },
{ "eci",	0xfc670000,	0xfc7f8080,	"r,I",		4 },
{ "ecwcs",	0xfc4f0000,	0xfc7f8000,	"",		4 },
{ "ei", 	0xfc000000,	0xfc07ffff,	"I",		4 },
{ "eomb",	0x8c080000,	0xfc080000,	"r,xOA,X",	4 },
{ "eomd",	0x8c000002,	0xfc080002,	"r,xOA,X",	4 },
{ "eomh",	0x8c000001,	0xfc080001,	"r,xOA,X",	4 },
{ "eomw",	0x8c000000,	0xfc080000,	"r,xOA,X",	4 },
{ "eor",	0x0c000000,	0xfc0f0000,	"r,R",		2 },
{ "eorm",	0x0c080000,	0xfc0f0000,	"r,R",		2 },
{ "es", 	0x00040000,	0xfc7f0000,	"r",		2 },
{ "exm",	0xa8000000,	0xff880000,	"xOA,X",	4 },
{ "exr",	0xc8070000,	0xfc7f0000,	"r",		2 },
{ "exrr",	0xc8070002,	0xfc7f0002,	"r",		2 },
{ "fixd",	0x380d0000,	0xfc0f0000,	"r,R",		2 },
{ "fixw",	0x38050000,	0xfc0f0000,	"r,R",		2 },
{ "fltd",	0x380f0000,	0xfc0f0000,	"r,R",		2 },
{ "fltw",	0x38070000,	0xfc0f0000,	"r,R",		2 },
{ "grio",	0xfc3f0000,	0xfc7f8000,	"r,I",		4 },
{ "halt",	0x00000000,	0xffff0000,	"",		2 },
{ "hio",	0xfc370000,	0xfc7f8000,	"r,I",		4 },
{ "jwcs",	0xfa080000,	0xff880000,	"xOA,X",	4 },
{ "la", 	0x50000000,	0xfc000000,	"r,xOA,X",	4 },
{ "labr",	0x58080000,	0xfc080000,	"b,xOA,X",	4 },
{ "lb", 	0xac080000,	0xfc080000,	"r,xOA,X",	4 },
{ "lcs", 	0x00030000,	0xfc7f0000,	"r",		2 },
{ "ld", 	0xac000002,	0xfc080002,	"r,xOA,X",	4 },
{ "lear", 	0x80000000,	0xfc080000,	"r,xOA,X",	4 },
{ "lf", 	0xcc000000,	0xfc080000,	"r,xOA,X",	4 },
{ "lfbr", 	0xcc080000,	0xfc080000,	"b,xOA,X",	4 },
{ "lh", 	0xac000001,	0xfc080001,	"r,xOA,X",	4 },
{ "li", 	0xc8000000,	0xfc7f0000,	"r,I",		4 },
{ "lmap",	0x2c070000,	0xfc7f0000,	"r",		2 },
{ "lmb",	0xb0080000,	0xfc080000,	"r,xOA,X",	4 },
{ "lmd",	0xb0000002,	0xfc080002,	"r,xOA,X",	4 },
{ "lmh",	0xb0000001,	0xfc080001,	"r,xOA,X",	4 },
{ "lmw",	0xb0000000,	0xfc080000,	"r,xOA,X",	4 },
{ "lnb",	0xb4080000,	0xfc080000,	"r,xOA,X",	4 },
{ "lnd",	0xb4000002,	0xfc080002,	"r,xOA,X",	4 },
{ "lnh",	0xb4000001,	0xfc080001,	"r,xOA,X",	4 },
{ "lnw",	0xb4000000,	0xfc080000,	"r,xOA,X",	4 },
{ "lpsd",	0xf9800000,	0xff880000,	"r,xOA,X",	4 },
{ "lpsdcm",	0xfa800000,	0xff880000,	"r,xOA,X",	4 },
{ "lw", 	0xac000000,	0xfc080000,	"r,xOA,X",	4 },
{ "lwbr", 	0x5c000000,	0xfc080000,	"b,xOA,X",	4 },
{ "mpfd",	0xe4080002,	0xfc080002,	"r,xOA,X",	4 },
{ "mpfw",	0xe4080000,	0xfc080000,	"r,xOA,X",	4 },
{ "mpi",	0xc8030000,	0xfc7f0000,	"r,I",		4 },
{ "mpmb",	0xc0080000,	0xfc080000,	"r,xOA,X",	4 },
{ "mpmh",	0xc0000001,	0xfc080001,	"r,xOA,X",	4 },
{ "mpmw",	0xc0000000,	0xfc080000,	"r,xOA,X",	4 },
{ "mpr",	0x38020000,	0xfc0f0000,	"r,R",		2 },
{ "mprfd",	0x380e0000,	0xfc0f0000,	"r,R",		2 },
{ "mprfw",	0x38060000,	0xfc0f0000,	"r,R",		2 },
{ "nop",	0x00020000,	0xffff0000,	"",		2 },
{ "ormb",	0x88080000,	0xfc080000,	"r,xOA,X",	4 },
{ "ormd",	0x88000002,	0xfc080002,	"r,xOA,X",	4 },
{ "ormh",	0x88000001,	0xfc080001,	"r,xOA,X",	4 },
{ "ormw",	0x88000000,	0xfc080000,	"r,xOA,X",	4 },
{ "orr",	0x08000000,	0xfc0f0000,	"r,R",		2 },
{ "orrm",	0x08080000,	0xfc0f0000,	"r,R",		2 },
{ "rdsts",	0x00090000,	0xfc7f0000,	"r",		2 },
{ "return",	0x280e0000,	0xfc7f0000,	"",		2 },
{ "ri", 	0xfc020000,	0xfc07ffff,	"I",		4 },
{ "rnd",	0x00050000,	0xfc7f0000,	"r",		2 },
{ "rpswt",	0x040b0000,	0xfc7f0000,	"r",		2 },
{ "rschnl",	0xfc2f0000,	0xfc7f8000,	"r,I",		4 },
{ "rsctl",	0xfc470000,	0xfc7f8000,	"r,I",		4 },
{ "rwcs",	0x000b0000,	0xfc0f0000,	"r,R",		2 },
{ "sacz",	0x10080000,	0xfc0f0000,	"r,R",		2 },
{ "sbm",	0x98080000,	0xfc080000,	"f,xOA,X",	4 },
{ "sbr",	0x18000000,	0xfc0c0000,	"r,f",		4 },
{ "sea",	0x000d0000,	0xffff0000,	"",		2 },
{ "setcpu",	0x2c090000,	0xfc7f0000,	"r",		2 },
{ "sio",	0xfc170000,	0xfc7f8000,	"r,I",		4 },
{ "sipu",	0x000a0000,	0xffff0000,	"",		2 },
{ "sla",	0x1c400000,	0xfc600000,	"r,S",		2 },
{ "slad",	0x20400000,	0xfc600000,	"r,S",		2 },
{ "slc",	0x24400000,	0xfc600000,	"r,S",		2 },
{ "sll",	0x1c600000,	0xfc600000,	"r,S",		2 },
{ "slld",	0x20600000,	0xfc600000,	"r,S",		2 },
{ "smc",	0x04070000,	0xfc070000,	"",		2 },
{ "sra",	0x1c000000,	0xfc600000,	"r,S",		2 },
{ "srad",	0x20000000,	0xfc600000,	"r,S",		2 },
{ "src",	0x24000000,	0xfc600000,	"r,S",		2 },
{ "srl",	0x1c200000,	0xfc600000,	"r,S",		2 },
{ "srld",	0x20200000,	0xfc600000,	"r,S",		2 },
{ "stb",	0xd4080000,	0xfc080000,	"r,xOA,X",	4 },
{ "std",	0xd4000002,	0xfc080002,	"r,xOA,X",	4 },
{ "stf",	0xdc000000,	0xfc080000,	"r,xOA,X",	4 },
{ "stfbr",	0x54000000,	0xfc080000,	"b,xOA,X",	4 },
{ "sth",	0xd4000001,	0xfc080001,	"r,xOA,X",	4 },
{ "stmb",	0xd8080000,	0xfc080000,	"r,xOA,X",	4 },
{ "stmd",	0xd8000002,	0xfc080002,	"r,xOA,X",	4 },
{ "stmh",	0xd8000001,	0xfc080001,	"r,xOA,X",	4 },
{ "stmw",	0xd8000000,	0xfc080000,	"r,xOA,X",	4 },
{ "stpio",	0xfc270000,	0xfc7f8000,	"r,I",		4 },
{ "stw",	0xd4000000,	0xfc080000,	"r,xOA,X",	4 },
{ "stwbr",	0x54000000,	0xfc080000,	"b,xOA,X",	4 },
{ "suabr",	0x58000000,	0xfc080000,	"b,xOA,X",	4 },
{ "sufd",	0xe0000002,	0xfc080002,	"r,xOA,X",	4 },
{ "sufw",	0xe0000000,	0xfc080000,	"r,xOA,X",	4 },
{ "sui",	0xc8020000,	0xfc7f0000,	"r,I",		4 },
{ "sumb",	0xbc080000,	0xfc080000,	"r,xOA,X",	4 },
{ "sumd",	0xbc000002,	0xfc080002,	"r,xOA,X",	4 },
{ "sumh",	0xbc000001,	0xfc080001,	"r,xOA,X",	4 },
{ "sumw",	0xbc000000,	0xfc080000,	"r,xOA,X",	4 },
{ "sur",	0x3c000000,	0xfc0f0000,	"r,R",		2 },
{ "surfd",	0x380b0000,	0xfc0f0000,	"r,xOA,X",	4 },
{ "surfw",	0x38030000,	0xfc0f0000,	"r,R",		2 },
{ "surm",	0x3c080000,	0xfc0f0000,	"r,R",		2 },
{ "svc",	0xc8060000,	0xffff0000,	"",		4 },
{ "tbm",	0xa4080000,	0xfc080000,	"f,xOA,X",	4 },
{ "tbr",	0x180c0000,	0xfc0c0000,	"r,f",		2 },
{ "tbrr",	0x2c020000,	0xfc0f0000,	"r,B",		2 },
{ "tccr",	0x28040000,	0xfc7f0000,	"",		2 },
{ "td", 	0xfc050000,	0xfc070000,	"r,f",		4 },
{ "tio",	0xfc1f0000,	0xfc7f8000,	"r,I",		4 },
{ "tmapr",	0x2c0a0000,	0xfc0f0000,	"r,R",		2 },
{ "tpcbr",	0x280c0000,	0xfc7f0000,	"r",		2 },
{ "trbr",	0x2c010000,	0xfc0f0000,	"b,R",		2 },
{ "trc",	0x2c030000,	0xfc0f0000,	"r,R",		2 },
{ "trcc",	0x28050000,	0xfc7f0000,	"",		2 },
{ "trcm",	0x2c0b0000,	0xfc0f0000,	"r,R",		2 },
{ "trn",	0x2c040000,	0xfc0f0000,	"r,R",		2 },
{ "trnm",	0x2c0c0000,	0xfc0f0000,	"r,R",		2 },
{ "trr",	0x2c000000,	0xfc0f0000,	"r,R",		2 },
{ "trrm",	0x2c080000,	0xfc0f0000,	"r,R",		2 },
{ "trsc",	0x2c0e0000,	0xfc0f0000,	"r,R",		2 },
{ "trsw",	0x28000000,	0xfc7f0000,	"r",		2 },
{ "tscr",	0x2c0f0000,	0xfc0f0000,	"r,R",		2 },
{ "uei",	0x00070000,	0xffff0000,	"",		2 },
{ "wait",	0x00010000,	0xffff0000,	"",		2 },
{ "wcwcs",	0xfc5f0000,	0xfc7f8000,	"",		4 },
{ "wwcs",	0x000c0000,	0xfc0f0000,	"r,R",		2 },
{ "xcbr",	0x28020000,	0xfc0f0000,	"b,B",		2 },
{ "xcr",	0x2c050000,	0xfc0f0000,	"r,R",		2 },
{ "xcrm",	0x2c0d0000,	0xfc0f0000,	"r,R",		2 },
{ "zbm",	0x9c080000,	0xfc080000,	"f,xOA,X",	4 },
{ "zbr",	0x18040000,	0xfc0c0000,	"r,f",		2 },
{ "zmb",	0xf8080000,	0xfc080000,	"r,xOA,X",	4 },
{ "zmd",	0xf8000002,	0xfc080002,	"r,xOA,X",	4 },
{ "zmh",	0xf8000001,	0xfc080001,	"r,xOA,X",	4 },
{ "zmw",	0xf8000000,	0xfc080000,	"r,xOA,X",	4 },
{ "zr", 	0x0c000000,	0xfc0f0000,	"r",		2 },
};

int numopcodes = sizeof(gld_opcodes) / sizeof(gld_opcodes[0]);

struct gld_opcode *endop = gld_opcodes + sizeof(gld_opcodes) /
		sizeof(gld_opcodes[0]);
