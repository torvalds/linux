/* pep-dll.h: Header file for routines used to build Windows DLLs.
   Copyright 2006, 2007 Free Software Foundation, Inc.

   This file is part of BFD, the Binary File Descriptor library.

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
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.
   
   Written by Kai Tietz, OneVision Software GmbH&CoKg.  */

#ifndef PEP_DLL_H
#define PEP_DLL_H

#include "sysdep.h"
#include "bfd.h"
#include "bfdlink.h"
#include "deffile.h"

extern def_file * pep_def_file;
extern int pep_dll_export_everything;
extern int pep_dll_do_default_excludes;
extern int pep_dll_kill_ats;
extern int pep_dll_stdcall_aliases;
extern int pep_dll_warn_dup_exports;
extern int pep_dll_compat_implib;
extern int pep_dll_extra_pe_debug;

extern void pep_dll_id_target  (const char *);
extern void pep_dll_add_excludes  (const char *, const int);
extern void pep_dll_generate_def_file  (const char *);
extern void pep_dll_generate_implib  (def_file *, const char *);
extern void pep_process_import_defs  (bfd *, struct bfd_link_info *);
extern bfd_boolean pep_implied_import_dll  (const char *);
extern void pep_dll_build_sections  (bfd *, struct bfd_link_info *);
extern void pep_exe_build_sections  (bfd *, struct bfd_link_info *);
extern void pep_dll_fill_sections  (bfd *, struct bfd_link_info *);
extern void pep_exe_fill_sections  (bfd *, struct bfd_link_info *);
extern void pep_walk_relocs_of_symbol
  (struct bfd_link_info *, const char *, int (*) (arelent *, asection *));
extern void pep_create_import_fixup  (arelent * rel, asection *, int);
extern bfd_boolean pep_bfd_is_dll  (bfd *);

#endif /* PEP_DLL_H */
