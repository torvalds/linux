/* Routines to help build PEPI-format DLLs (Win64 etc)
   Copyright 2006 Free Software Foundation, Inc.

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

#define COFF_IMAGE_WITH_PE
#define COFF_WITH_PE
#define COFF_WITH_pex64

/* Local defined globals.  */
#define pe_def_file	            pep_def_file
#define pe_details	            pep_details
#define pe_dll_compat_implib        pep_dll_compat_implib
#define pe_dll_extra_pe_debug       pep_dll_extra_pe_debug
#define pe_dll_export_everything    pep_dll_export_everything
#define pe_dll_do_default_excludes  pep_dll_do_default_excludes
#define pe_dll_kill_ats             pep_dll_kill_ats
#define pe_dll_stdcall_aliases      pep_dll_stdcall_aliases
#define pe_dll_warn_dup_exports     pep_dll_warn_dup_exports

/* External globals.  */
#define pe_data_import_dll          pep_data_import_dll

/* Unique global name for functions to avoid double defined symbols.  */
#define pe_create_import_fixup      pep_create_import_fixup
#define pe_dll_generate_def_file    pep_dll_generate_def_file
#define pe_process_import_defs      pep_process_import_defs
#define pe_dll_id_target            pep_dll_id_target
#define pe_implied_import_dll       pep_implied_import_dll
#define pe_dll_build_sections       pep_dll_build_sections
#define pe_exe_build_sections       pep_exe_build_sections
#define pe_dll_fill_sections        pep_dll_fill_sections
#define pe_exe_fill_sections        pep_exe_fill_sections
#define pe_dll_generate_implib      pep_dll_generate_implib
#define pe_dll_add_excludes         pep_dll_add_excludes
#define pe_walk_relocs_of_symbol    pep_walk_relocs_of_symbol
#define pe_bfd_is_dll		    pep_bfd_is_dll

/* Uses x86_64 PE+.  */
#define pe_use_x86_64

#include "pe-dll.c"
