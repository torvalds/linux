/* Definitions of target machine for GNU compiler.
   NEC VR Series Processors
   Copyright (c) 2002, 2004, 2005 Free Software Foundation, Inc.
   Contributed by Red Hat, Inc.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GCC is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to
the Free Software Foundation, 51 Franklin Street, Fifth Floor,
Boston, MA 02110-1301, USA.  */

#define DEFAULT_VR_ARCH "mfix-vr4130"
#define MIPS_ABI_DEFAULT ABI_EABI
#define MIPS_MARCH_CONTROLS_SOFT_FLOAT 1
#define MULTILIB_DEFAULTS \
	{ MULTILIB_ENDIAN_DEFAULT,		\
	  MULTILIB_ABI_DEFAULT,			\
	  DEFAULT_VR_ARCH }

#define DRIVER_SELF_SPECS \
	/* Enforce the default architecture.  This is mostly for	\
	   the assembler's benefit.  */					\
	"%{!march=*:%{!mfix-vr4120:%{!mfix-vr4130:"			\
	"-" DEFAULT_VR_ARCH "}}}",					\
									\
	/* Make -mfix-vr4120 imply -march=vr4120.  This cuts down	\
	   on command-line tautology and makes it easier for t-vr to	\
	   provide a -mfix-vr4120 multilib.  */				\
	"%{mfix-vr4120:%{!march=*:-march=vr4120}}",			\
									\
	/* Same idea for -mfix-vr4130.  */				\
	"%{mfix-vr4130:%{!march=*:-march=vr4130}}",			\
									\
	/* Make -mabi=eabi -mlong32 the default.  */			\
	"%{!mabi=*:-mabi=eabi %{!mlong*:-mlong32}}",			\
									\
	/* Make sure -mlong64 multilibs are chosen when	64-bit longs	\
	   are needed.  */						\
	"%{mabi=eabi:%{!mlong*:%{!mgp32:-mlong64}}}",			\
									\
	/* Remove -mgp32 if it is redundant.  */			\
	"%{mabi=32:%<mgp32}"
