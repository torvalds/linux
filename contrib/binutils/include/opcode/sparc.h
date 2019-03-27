/* Definitions for opcode table for the sparc.
   Copyright 1989, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 2000, 2002,
   2003, 2005 Free Software Foundation, Inc.

   This file is part of GAS, the GNU Assembler, GDB, the GNU debugger, and
   the GNU Binutils.

   GAS/GDB is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GAS/GDB is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GAS or GDB; see the file COPYING.	If not, write to
   the Free Software Foundation, 51 Franklin Street - Fifth Floor,
   Boston, MA 02110-1301, USA.  */

#include "ansidecl.h"

/* The SPARC opcode table (and other related data) is defined in
   the opcodes library in sparc-opc.c.  If you change anything here, make
   sure you fix up that file, and vice versa.  */

 /* FIXME-someday: perhaps the ,a's and such should be embedded in the
    instruction's name rather than the args.  This would make gas faster, pinsn
    slower, but would mess up some macros a bit.  xoxorich. */

/* List of instruction sets variations.
   These values are such that each element is either a superset of a
   preceding each one or they conflict in which case SPARC_OPCODE_CONFLICT_P
   returns non-zero.
   The values are indices into `sparc_opcode_archs' defined in sparc-opc.c.
   Don't change this without updating sparc-opc.c.  */

enum sparc_opcode_arch_val
{
  SPARC_OPCODE_ARCH_V6 = 0,
  SPARC_OPCODE_ARCH_V7,
  SPARC_OPCODE_ARCH_V8,
  SPARC_OPCODE_ARCH_SPARCLET,
  SPARC_OPCODE_ARCH_SPARCLITE,
  /* V9 variants must appear last.  */
  SPARC_OPCODE_ARCH_V9,
  SPARC_OPCODE_ARCH_V9A, /* V9 with ultrasparc additions.  */
  SPARC_OPCODE_ARCH_V9B, /* V9 with ultrasparc and cheetah additions.  */
  SPARC_OPCODE_ARCH_BAD  /* Error return from sparc_opcode_lookup_arch.  */
};

/* The highest architecture in the table.  */
#define SPARC_OPCODE_ARCH_MAX (SPARC_OPCODE_ARCH_BAD - 1)

/* Given an enum sparc_opcode_arch_val, return the bitmask to use in
   insn encoding/decoding.  */
#define SPARC_OPCODE_ARCH_MASK(arch) (1 << (arch))

/* Given a valid sparc_opcode_arch_val, return non-zero if it's v9.  */
#define SPARC_OPCODE_ARCH_V9_P(arch) ((arch) >= SPARC_OPCODE_ARCH_V9)

/* Table of cpu variants.  */

typedef struct sparc_opcode_arch
{
  const char *name;
  /* Mask of sparc_opcode_arch_val's supported.
     EG: For v7 this would be
     (SPARC_OPCODE_ARCH_MASK (..._V6) | SPARC_OPCODE_ARCH_MASK (..._V7)).
     These are short's because sparc_opcode.architecture is.  */
  short supported;
} sparc_opcode_arch;

extern const struct sparc_opcode_arch sparc_opcode_archs[];

/* Given architecture name, look up it's sparc_opcode_arch_val value.  */
extern enum sparc_opcode_arch_val sparc_opcode_lookup_arch (const char *);

/* Return the bitmask of supported architectures for ARCH.  */
#define SPARC_OPCODE_SUPPORTED(ARCH) (sparc_opcode_archs[ARCH].supported)

/* Non-zero if ARCH1 conflicts with ARCH2.
   IE: ARCH1 as a supported bit set that ARCH2 doesn't, and vice versa.  */
#define SPARC_OPCODE_CONFLICT_P(ARCH1, ARCH2) \
 (((SPARC_OPCODE_SUPPORTED (ARCH1) & SPARC_OPCODE_SUPPORTED (ARCH2)) \
   != SPARC_OPCODE_SUPPORTED (ARCH1)) \
  && ((SPARC_OPCODE_SUPPORTED (ARCH1) & SPARC_OPCODE_SUPPORTED (ARCH2)) \
     != SPARC_OPCODE_SUPPORTED (ARCH2)))

/* Structure of an opcode table entry.  */

typedef struct sparc_opcode
{
  const char *name;
  unsigned long match;	/* Bits that must be set.  */
  unsigned long lose;	/* Bits that must not be set.  */
  const char *args;
  /* This was called "delayed" in versions before the flags.  */
  char flags;
  short architecture;	/* Bitmask of sparc_opcode_arch_val's.  */
} sparc_opcode;

#define	F_DELAYED	1	/* Delayed branch.  */
#define	F_ALIAS		2	/* Alias for a "real" instruction.  */
#define	F_UNBR		4	/* Unconditional branch.  */
#define	F_CONDBR	8	/* Conditional branch.  */
#define	F_JSR		16	/* Subroutine call.  */
#define F_FLOAT		32	/* Floating point instruction (not a branch).  */
#define F_FBR		64	/* Floating point branch.  */
/* FIXME: Add F_ANACHRONISTIC flag for v9.  */

/* All sparc opcodes are 32 bits, except for the `set' instruction (really a
   macro), which is 64 bits. It is handled as a special case.

   The match component is a mask saying which bits must match a particular
   opcode in order for an instruction to be an instance of that opcode.

   The args component is a string containing one character for each operand of the
   instruction.

   Kinds of operands:
	#	Number used by optimizer.	It is ignored.
	1	rs1 register.
	2	rs2 register.
	d	rd register.
	e	frs1 floating point register.
	v	frs1 floating point register (double/even).
	V	frs1 floating point register (quad/multiple of 4).
	f	frs2 floating point register.
	B	frs2 floating point register (double/even).
	R	frs2 floating point register (quad/multiple of 4).
	g	frsd floating point register.
	H	frsd floating point register (double/even).
	J	frsd floating point register (quad/multiple of 4).
	b	crs1 coprocessor register
	c	crs2 coprocessor register
	D	crsd coprocessor register
	m	alternate space register (asr) in rd
	M	alternate space register (asr) in rs1
	h	22 high bits.
	X	5 bit unsigned immediate
	Y	6 bit unsigned immediate
	3	SIAM mode (3 bits). (v9b)
	K	MEMBAR mask (7 bits). (v9)
	j	10 bit Immediate. (v9)
	I	11 bit Immediate. (v9)
	i	13 bit Immediate.
	n	22 bit immediate.
	k	2+14 bit PC relative immediate. (v9)
	G	19 bit PC relative immediate. (v9)
	l	22 bit PC relative immediate.
	L	30 bit PC relative immediate.
	a	Annul.	The annul bit is set.
	A	Alternate address space. Stored as 8 bits.
	C	Coprocessor state register.
	F	floating point state register.
	p	Processor state register.
	N	Branch predict clear ",pn" (v9)
	T	Branch predict set ",pt" (v9)
	z	%icc. (v9)
	Z	%xcc. (v9)
	q	Floating point queue.
	r	Single register that is both rs1 and rd.
	O	Single register that is both rs2 and rd.
	Q	Coprocessor queue.
	S	Special case.
	t	Trap base register.
	w	Window invalid mask register.
	y	Y register.
	u	sparclet coprocessor registers in rd position
	U	sparclet coprocessor registers in rs1 position
	E	%ccr. (v9)
	s	%fprs. (v9)
	P	%pc.  (v9)
	W	%tick.	(v9)
	o	%asi. (v9)
	6	%fcc0. (v9)
	7	%fcc1. (v9)
	8	%fcc2. (v9)
	9	%fcc3. (v9)
	!	Privileged Register in rd (v9)
	?	Privileged Register in rs1 (v9)
	*	Prefetch function constant. (v9)
	x	OPF field (v9 impdep).
	0	32/64 bit immediate for set or setx (v9) insns
	_	Ancillary state register in rd (v9a)
	/	Ancillary state register in rs1 (v9a)

  The following chars are unused: (note: ,[] are used as punctuation)
  [45].  */

#define OP2(x)		(((x) & 0x7) << 22)  /* Op2 field of format2 insns.  */
#define OP3(x)		(((x) & 0x3f) << 19) /* Op3 field of format3 insns.  */
#define OP(x)		((unsigned) ((x) & 0x3) << 30) /* Op field of all insns.  */
#define OPF(x)		(((x) & 0x1ff) << 5) /* Opf field of float insns.  */
#define OPF_LOW5(x)	OPF ((x) & 0x1f)     /* V9.  */
#define F3F(x, y, z)	(OP (x) | OP3 (y) | OPF (z)) /* Format3 float insns.  */
#define F3I(x)		(((x) & 0x1) << 13)  /* Immediate field of format 3 insns.  */
#define F2(x, y)	(OP (x) | OP2(y))    /* Format 2 insns.  */
#define F3(x, y, z)	(OP (x) | OP3(y) | F3I(z)) /* Format3 insns.  */
#define F1(x)		(OP (x))
#define DISP30(x)	((x) & 0x3fffffff)
#define ASI(x)		(((x) & 0xff) << 5)  /* Asi field of format3 insns.  */
#define RS2(x)		((x) & 0x1f)         /* Rs2 field.  */
#define SIMM13(x)	((x) & 0x1fff)       /* Simm13 field.  */
#define RD(x)		(((x) & 0x1f) << 25) /* Destination register field.  */
#define RS1(x)		(((x) & 0x1f) << 14) /* Rs1 field.  */
#define ASI_RS2(x)	(SIMM13 (x))
#define MEMBAR(x)	((x) & 0x7f)
#define SLCPOP(x)	(((x) & 0x7f) << 6)  /* Sparclet cpop.  */

#define ANNUL	(1 << 29)
#define BPRED	(1 << 19)	/* V9.  */
#define	IMMED	F3I (1)
#define RD_G0	RD (~0)
#define	RS1_G0	RS1 (~0)
#define	RS2_G0	RS2 (~0)

extern const struct sparc_opcode sparc_opcodes[];
extern const int sparc_num_opcodes;

extern int sparc_encode_asi (const char *);
extern const char *sparc_decode_asi (int);
extern int sparc_encode_membar (const char *);
extern const char *sparc_decode_membar (int);
extern int sparc_encode_prefetch (const char *);
extern const char *sparc_decode_prefetch (int);
extern int sparc_encode_sparclet_cpreg (const char *);
extern const char *sparc_decode_sparclet_cpreg (int);

/* Local Variables:
   fill-column: 131
   comment-column: 0
   End: */

