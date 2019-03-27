/* Definitions for XScale architectures using ELF
   Copyright (C) 1999, 2000, 2001 Free Software Foundation, Inc.
   Contributed by Catherine Moore <clm@cygnus.com>

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 2, or (at your
   option) any later version.

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING.  If not, write to
   the Free Software Foundation, 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

/* Run-time Target Specification.  */
#ifndef TARGET_VERSION
#define TARGET_VERSION	fputs (" (XScale/ELF non-Linux)", stderr);
#endif

#ifndef SUBTARGET_CPU_DEFAULT
#define SUBTARGET_CPU_DEFAULT 		TARGET_CPU_xscale
#endif

/* Note - there are three possible -mfpu= arguments that can be passed to
   the assembler:
   
     -mfpu=softvfp   This is the default.  It indicates thats doubles are
                     stored in a format compatible with the VFP
		     specification.  This is the newer double format, whereby
		     the endian-ness of the doubles matches the endian-ness
		     of the memory architecture.
     
     -mfpu=fpa       This is when -mhard-float is specified.
                     [It is not known if any XScale's have been made with
		     hardware floating point support, but nevertheless this
		     is what happens].
		     
     -mfpu=softfpa   This is when -msoft-float is specified.
                     This is the normal behavior of other arm configurations,
		     which for backwards compatibility purposes default to
		     supporting the old FPA format which was always big
		     endian, regardless of the endian-ness of the memory
		     system.  */
		     
#define SUBTARGET_EXTRA_ASM_SPEC "%{!mcpu=*:-mcpu=xscale} \
  %{mhard-float:-mfpu=fpa} \
  %{!mhard-float: %{msoft-float:-mfpu=softfpa;:-mfpu=softvfp}}"

#ifndef MULTILIB_DEFAULTS
#define MULTILIB_DEFAULTS \
  { "mlittle-endian", "mno-thumb-interwork", "marm", "msoft-float" }
#endif
