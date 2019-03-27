/* Common VxWorks target definitions for GNU compiler.
   Copyright (C) 1999, 2000, 2001, 2002, 2003, 2004, 2005
   Free Software Foundation, Inc.
   Contributed by Wind River Systems.
   Rewritten by CodeSourcery, LLC.

This file is part of GCC.

GCC is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation; either version 2, or (at your option) any later
version.

GCC is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received a copy of the GNU General Public License
along with GCC; see the file COPYING.  If not, write to the Free
Software Foundation, 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.  */

/* In kernel mode, VxWorks provides all the libraries itself, as well as
   the functionality of startup files, etc.  In RTP mode, it behaves more
   like a traditional Unix, with more external files.  Most of our specs
   must be aware of the difference.  */

/* The directory containing the VxWorks target headers.  */
#define VXWORKS_TARGET_DIR  "/home/tornado/base6/target"

/* Since we provide a default -isystem, expand -isystem on the command
   line early.  */
#undef VXWORKS_ADDITIONAL_CPP_SPEC
#define VXWORKS_ADDITIONAL_CPP_SPEC " 					\
 %{!nostdinc:%{isystem*}}						\
 %{mrtp: -D__RTP__=1							\
	 %{!nostdinc:-isystem " VXWORKS_TARGET_DIR "/usr/h}}		\
 %{!mrtp:-D_WRS_KERNEL=1						\
	 %{!nostdinc:-isystem " VXWORKS_TARGET_DIR "/h}}"

/* The references to __init and __fini will be satisfied by
   libc_internal.a.  */
#undef VXWORKS_LIB_SPEC
#define	VXWORKS_LIB_SPEC						\
"%{mrtp:%{shared:-u " USER_LABEL_PREFIX "__init -u " USER_LABEL_PREFIX "__fini} \
	%{!shared:%{non-static:-u " USER_LABEL_PREFIX "_STI__6__rtld -ldl} \
		  --start-group -lc -lgcc -lc_internal -lnet -ldsi	\
		  --end-group}}"

/* The no-op spec for "-shared" below is present because otherwise GCC
   will treat it as an unrecognized option.  */
#undef VXWORKS_LINK_SPEC
#define VXWORKS_LINK_SPEC				\
"%{!mrtp:-r}						\
 %{!shared:						\
   %{mrtp:-q %{h*}					\
          %{R*} %{!Wl,-T*: %{!T*: %(link_start) }}	\
          %(link_target) %(link_os)}}			\
 %{v:-V}						\
 %{shared:-shared}					\
 %{Bstatic:-Bstatic}					\
 %{Bdynamic:-Bdynamic}					\
 %{!Xbind-lazy:-z now}					\
 %{Xbind-now:%{Xbind-lazy:				\
   %e-Xbind-now and -Xbind-lazy are incompatible}}	\
 %{mrtp:%{!shared:%{!non-static:-static}		\
 		  %{non-static:--force-dynamic --export-dynamic}}}"

/* For VxWorks, the system provides libc_internal.a.  This is a superset
   of libgcc.a; we want to use it.  Make sure not to dynamically export
   any of its symbols, though.  Always look for libgcc.a first so that
   we get the latest versions of the GNU intrinsics during our builds.  */
#undef VXWORKS_LIBGCC_SPEC
#define VXWORKS_LIBGCC_SPEC \
  "-lgcc %{mrtp:--exclude-libs=libc_internal,libgcc -lc_internal}"

#undef VXWORKS_STARTFILE_SPEC
#define	VXWORKS_STARTFILE_SPEC "%{mrtp:%{!shared:crt0.o%s}}"
#define VXWORKS_ENDFILE_SPEC ""

/* We can use .ctors/.dtors sections only in RTP mode.
   Unfortunately this must be an integer constant expression;
   fix up in override_options.  */
#undef VXWORKS_OVERRIDE_OPTIONS
#define VXWORKS_OVERRIDE_OPTIONS do { \
  targetm.have_ctors_dtors = TARGET_VXWORKS_RTP; \
} while (0)

/* The VxWorks runtime uses a clever trick to get the sentinel entry
   (-1) inserted at the beginning of the .ctors segment.  This trick
   will not work if we ever generate any entries in plain .ctors
   sections; we must always use .ctors.PRIORITY.  */
#define ALWAYS_NUMBER_CTORS_SECTIONS 1

/* The name of the symbol for the table of GOTs in a particular
   RTP.  */
#define VXWORKS_GOTT_BASE "__GOTT_BASE__"
/* The name of the symbol for the index into the table of GOTs for the
   GOT associated with the current shared library.  */
#define VXWORKS_GOTT_INDEX "__GOTT_INDEX__"

#define VXWORKS_KIND VXWORKS_KIND_NORMAL
