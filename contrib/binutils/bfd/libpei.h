/* Support for the generic parts of PE/PEI; common header information.
   Copyright 1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003, 2005, 2006
   Free Software Foundation, Inc.
   Written by Cygnus Solutions.

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
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

/* Most of this hacked by  Steve Chamberlain,
			sac@cygnus.com

   PE/PEI rearrangement (and code added): Donn Terry
				       Softway Systems, Inc.  */

/* Hey look, some documentation [and in a place you expect to find it]!

   The main reference for the pei format is "Microsoft Portable Executable
   and Common Object File Format Specification 4.1".  Get it if you need to
   do some serious hacking on this code.

   Another reference:
   "Peering Inside the PE: A Tour of the Win32 Portable Executable
   File Format", MSJ 1994, Volume 9.

   The *sole* difference between the pe format and the pei format is that the
   latter has an MSDOS 2.0 .exe header on the front that prints the message
   "This app must be run under Windows." (or some such).
   (FIXME: Whether that statement is *really* true or not is unknown.
   Are there more subtle differences between pe and pei formats?
   For now assume there aren't.  If you find one, then for God sakes
   document it here!)

   The Microsoft docs use the word "image" instead of "executable" because
   the former can also refer to a DLL (shared library).  Confusion can arise
   because the `i' in `pei' also refers to "image".  The `pe' format can
   also create images (i.e. executables), it's just that to run on a win32
   system you need to use the pei format.

   FIXME: Please add more docs here so the next poor fool that has to hack
   on this code has a chance of getting something accomplished without
   wasting too much time.  */

#ifndef GET_FCN_LNNOPTR
#define GET_FCN_LNNOPTR(abfd, ext) \
  H_GET_32 (abfd, ext->x_sym.x_fcnary.x_fcn.x_lnnoptr)
#endif

#ifndef GET_FCN_ENDNDX
#define GET_FCN_ENDNDX(abfd, ext) \
  H_GET_32 (abfd, ext->x_sym.x_fcnary.x_fcn.x_endndx)
#endif

#ifndef PUT_FCN_LNNOPTR
#define PUT_FCN_LNNOPTR(abfd, in, ext) \
  H_PUT_32(abfd, in, ext->x_sym.x_fcnary.x_fcn.x_lnnoptr)
#endif
#ifndef PUT_FCN_ENDNDX
#define PUT_FCN_ENDNDX(abfd, in, ext) \
  H_PUT_32(abfd, in, ext->x_sym.x_fcnary.x_fcn.x_endndx)
#endif
#ifndef GET_LNSZ_LNNO
#define GET_LNSZ_LNNO(abfd, ext) \
  H_GET_16 (abfd, ext->x_sym.x_misc.x_lnsz.x_lnno)
#endif
#ifndef GET_LNSZ_SIZE
#define GET_LNSZ_SIZE(abfd, ext) \
  H_GET_16 (abfd, ext->x_sym.x_misc.x_lnsz.x_size)
#endif
#ifndef PUT_LNSZ_LNNO
#define PUT_LNSZ_LNNO(abfd, in, ext) \
  H_PUT_16(abfd, in, ext->x_sym.x_misc.x_lnsz.x_lnno)
#endif
#ifndef PUT_LNSZ_SIZE
#define PUT_LNSZ_SIZE(abfd, in, ext) \
  H_PUT_16(abfd, in, ext->x_sym.x_misc.x_lnsz.x_size)
#endif
#ifndef GET_SCN_SCNLEN
#define GET_SCN_SCNLEN(abfd, ext) \
  H_GET_32 (abfd, ext->x_scn.x_scnlen)
#endif
#ifndef GET_SCN_NRELOC
#define GET_SCN_NRELOC(abfd, ext) \
  H_GET_16 (abfd, ext->x_scn.x_nreloc)
#endif
#ifndef GET_SCN_NLINNO
#define GET_SCN_NLINNO(abfd, ext) \
  H_GET_16 (abfd, ext->x_scn.x_nlinno)
#endif
#ifndef PUT_SCN_SCNLEN
#define PUT_SCN_SCNLEN(abfd, in, ext) \
  H_PUT_32(abfd, in, ext->x_scn.x_scnlen)
#endif
#ifndef PUT_SCN_NRELOC
#define PUT_SCN_NRELOC(abfd, in, ext) \
  H_PUT_16(abfd, in, ext->x_scn.x_nreloc)
#endif
#ifndef PUT_SCN_NLINNO
#define PUT_SCN_NLINNO(abfd, in, ext) \
  H_PUT_16(abfd,in, ext->x_scn.x_nlinno)
#endif
#ifndef GET_LINENO_LNNO
#define GET_LINENO_LNNO(abfd, ext) \
  H_GET_16 (abfd, ext->l_lnno);
#endif
#ifndef PUT_LINENO_LNNO
#define PUT_LINENO_LNNO(abfd, val, ext) \
  H_PUT_16(abfd,val, ext->l_lnno);
#endif

/* The f_symptr field in the filehdr is sometimes 64 bits.  */
#ifndef GET_FILEHDR_SYMPTR
#define GET_FILEHDR_SYMPTR H_GET_32
#endif
#ifndef PUT_FILEHDR_SYMPTR
#define PUT_FILEHDR_SYMPTR H_PUT_32
#endif

/* Some fields in the aouthdr are sometimes 64 bits.  */
#ifndef GET_AOUTHDR_TSIZE
#define GET_AOUTHDR_TSIZE H_GET_32
#endif
#ifndef PUT_AOUTHDR_TSIZE
#define PUT_AOUTHDR_TSIZE H_PUT_32
#endif
#ifndef GET_AOUTHDR_DSIZE
#define GET_AOUTHDR_DSIZE H_GET_32
#endif
#ifndef PUT_AOUTHDR_DSIZE
#define PUT_AOUTHDR_DSIZE H_PUT_32
#endif
#ifndef GET_AOUTHDR_BSIZE
#define GET_AOUTHDR_BSIZE H_GET_32
#endif
#ifndef PUT_AOUTHDR_BSIZE
#define PUT_AOUTHDR_BSIZE H_PUT_32
#endif
#ifndef GET_AOUTHDR_ENTRY
#define GET_AOUTHDR_ENTRY H_GET_32
#endif
#ifndef PUT_AOUTHDR_ENTRY
#define PUT_AOUTHDR_ENTRY H_PUT_32
#endif
#ifndef GET_AOUTHDR_TEXT_START
#define GET_AOUTHDR_TEXT_START H_GET_32
#endif
#ifndef PUT_AOUTHDR_TEXT_START
#define PUT_AOUTHDR_TEXT_START H_PUT_32
#endif
#ifndef GET_AOUTHDR_DATA_START
#define GET_AOUTHDR_DATA_START H_GET_32
#endif
#ifndef PUT_AOUTHDR_DATA_START
#define PUT_AOUTHDR_DATA_START H_PUT_32
#endif

/* Some fields in the scnhdr are sometimes 64 bits.  */
#ifndef GET_SCNHDR_PADDR
#define GET_SCNHDR_PADDR H_GET_32
#endif
#ifndef PUT_SCNHDR_PADDR
#define PUT_SCNHDR_PADDR H_PUT_32
#endif
#ifndef GET_SCNHDR_VADDR
#define GET_SCNHDR_VADDR H_GET_32
#endif
#ifndef PUT_SCNHDR_VADDR
#define PUT_SCNHDR_VADDR H_PUT_32
#endif
#ifndef GET_SCNHDR_SIZE
#define GET_SCNHDR_SIZE H_GET_32
#endif
#ifndef PUT_SCNHDR_SIZE
#define PUT_SCNHDR_SIZE H_PUT_32
#endif
#ifndef GET_SCNHDR_SCNPTR
#define GET_SCNHDR_SCNPTR H_GET_32
#endif
#ifndef PUT_SCNHDR_SCNPTR
#define PUT_SCNHDR_SCNPTR H_PUT_32
#endif
#ifndef GET_SCNHDR_RELPTR
#define GET_SCNHDR_RELPTR H_GET_32
#endif
#ifndef PUT_SCNHDR_RELPTR
#define PUT_SCNHDR_RELPTR H_PUT_32
#endif
#ifndef GET_SCNHDR_LNNOPTR
#define GET_SCNHDR_LNNOPTR H_GET_32
#endif
#ifndef PUT_SCNHDR_LNNOPTR
#define PUT_SCNHDR_LNNOPTR H_PUT_32
#endif

#ifdef COFF_WITH_pex64

#define GET_OPTHDR_IMAGE_BASE            H_GET_64
#define PUT_OPTHDR_IMAGE_BASE            H_PUT_64
#define GET_OPTHDR_SIZE_OF_STACK_RESERVE H_GET_64
#define PUT_OPTHDR_SIZE_OF_STACK_RESERVE H_PUT_64
#define GET_OPTHDR_SIZE_OF_STACK_COMMIT  H_GET_64
#define PUT_OPTHDR_SIZE_OF_STACK_COMMIT  H_PUT_64
#define GET_OPTHDR_SIZE_OF_HEAP_RESERVE  H_GET_64
#define PUT_OPTHDR_SIZE_OF_HEAP_RESERVE  H_PUT_64
#define GET_OPTHDR_SIZE_OF_HEAP_COMMIT   H_GET_64
#define PUT_OPTHDR_SIZE_OF_HEAP_COMMIT   H_PUT_64
#define GET_PDATA_ENTRY                  bfd_get_32

#define _bfd_XX_bfd_copy_private_bfd_data_common	_bfd_pex64_bfd_copy_private_bfd_data_common
#define _bfd_XX_bfd_copy_private_section_data		_bfd_pex64_bfd_copy_private_section_data
#define _bfd_XX_get_symbol_info				_bfd_pex64_get_symbol_info
#define _bfd_XX_only_swap_filehdr_out			_bfd_pex64_only_swap_filehdr_out
#define _bfd_XX_print_private_bfd_data_common		_bfd_pex64_print_private_bfd_data_common
#define _bfd_XXi_final_link_postscript			_bfd_pex64i_final_link_postscript
#define _bfd_XXi_only_swap_filehdr_out			_bfd_pex64i_only_swap_filehdr_out
#define _bfd_XXi_swap_aouthdr_in			_bfd_pex64i_swap_aouthdr_in
#define _bfd_XXi_swap_aouthdr_out			_bfd_pex64i_swap_aouthdr_out
#define _bfd_XXi_swap_aux_in				_bfd_pex64i_swap_aux_in
#define _bfd_XXi_swap_aux_out				_bfd_pex64i_swap_aux_out
#define _bfd_XXi_swap_lineno_in				_bfd_pex64i_swap_lineno_in
#define _bfd_XXi_swap_lineno_out			_bfd_pex64i_swap_lineno_out
#define _bfd_XXi_swap_scnhdr_out			_bfd_pex64i_swap_scnhdr_out
#define _bfd_XXi_swap_sym_in				_bfd_pex64i_swap_sym_in
#define _bfd_XXi_swap_sym_out				_bfd_pex64i_swap_sym_out

#elif defined COFF_WITH_pep

#define GET_OPTHDR_IMAGE_BASE H_GET_64
#define PUT_OPTHDR_IMAGE_BASE H_PUT_64
#define GET_OPTHDR_SIZE_OF_STACK_RESERVE H_GET_64
#define PUT_OPTHDR_SIZE_OF_STACK_RESERVE H_PUT_64
#define GET_OPTHDR_SIZE_OF_STACK_COMMIT H_GET_64
#define PUT_OPTHDR_SIZE_OF_STACK_COMMIT H_PUT_64
#define GET_OPTHDR_SIZE_OF_HEAP_RESERVE H_GET_64
#define PUT_OPTHDR_SIZE_OF_HEAP_RESERVE H_PUT_64
#define GET_OPTHDR_SIZE_OF_HEAP_COMMIT H_GET_64
#define PUT_OPTHDR_SIZE_OF_HEAP_COMMIT H_PUT_64
#define GET_PDATA_ENTRY bfd_get_64

#define _bfd_XX_bfd_copy_private_bfd_data_common	_bfd_pep_bfd_copy_private_bfd_data_common
#define _bfd_XX_bfd_copy_private_section_data		_bfd_pep_bfd_copy_private_section_data
#define _bfd_XX_get_symbol_info				_bfd_pep_get_symbol_info
#define _bfd_XX_only_swap_filehdr_out			_bfd_pep_only_swap_filehdr_out
#define _bfd_XX_print_private_bfd_data_common		_bfd_pep_print_private_bfd_data_common
#define _bfd_XXi_final_link_postscript			_bfd_pepi_final_link_postscript
#define _bfd_XXi_only_swap_filehdr_out			_bfd_pepi_only_swap_filehdr_out
#define _bfd_XXi_swap_aouthdr_in			_bfd_pepi_swap_aouthdr_in
#define _bfd_XXi_swap_aouthdr_out			_bfd_pepi_swap_aouthdr_out
#define _bfd_XXi_swap_aux_in				_bfd_pepi_swap_aux_in
#define _bfd_XXi_swap_aux_out				_bfd_pepi_swap_aux_out
#define _bfd_XXi_swap_lineno_in				_bfd_pepi_swap_lineno_in
#define _bfd_XXi_swap_lineno_out			_bfd_pepi_swap_lineno_out
#define _bfd_XXi_swap_scnhdr_out			_bfd_pepi_swap_scnhdr_out
#define _bfd_XXi_swap_sym_in				_bfd_pepi_swap_sym_in
#define _bfd_XXi_swap_sym_out				_bfd_pepi_swap_sym_out

#else /* !COFF_WITH_pep */

#define GET_OPTHDR_IMAGE_BASE H_GET_32
#define PUT_OPTHDR_IMAGE_BASE H_PUT_32
#define GET_OPTHDR_SIZE_OF_STACK_RESERVE H_GET_32
#define PUT_OPTHDR_SIZE_OF_STACK_RESERVE H_PUT_32
#define GET_OPTHDR_SIZE_OF_STACK_COMMIT H_GET_32
#define PUT_OPTHDR_SIZE_OF_STACK_COMMIT H_PUT_32
#define GET_OPTHDR_SIZE_OF_HEAP_RESERVE H_GET_32
#define PUT_OPTHDR_SIZE_OF_HEAP_RESERVE H_PUT_32
#define GET_OPTHDR_SIZE_OF_HEAP_COMMIT H_GET_32
#define PUT_OPTHDR_SIZE_OF_HEAP_COMMIT H_PUT_32
#define GET_PDATA_ENTRY bfd_get_32

#define _bfd_XX_bfd_copy_private_bfd_data_common	_bfd_pe_bfd_copy_private_bfd_data_common
#define _bfd_XX_bfd_copy_private_section_data		_bfd_pe_bfd_copy_private_section_data
#define _bfd_XX_get_symbol_info				_bfd_pe_get_symbol_info
#define _bfd_XX_only_swap_filehdr_out			_bfd_pe_only_swap_filehdr_out
#define _bfd_XX_print_private_bfd_data_common		_bfd_pe_print_private_bfd_data_common
#define _bfd_XXi_final_link_postscript			_bfd_pei_final_link_postscript
#define _bfd_XXi_only_swap_filehdr_out			_bfd_pei_only_swap_filehdr_out
#define _bfd_XXi_swap_aouthdr_in			_bfd_pei_swap_aouthdr_in
#define _bfd_XXi_swap_aouthdr_out			_bfd_pei_swap_aouthdr_out
#define _bfd_XXi_swap_aux_in				_bfd_pei_swap_aux_in
#define _bfd_XXi_swap_aux_out				_bfd_pei_swap_aux_out
#define _bfd_XXi_swap_lineno_in				_bfd_pei_swap_lineno_in
#define _bfd_XXi_swap_lineno_out			_bfd_pei_swap_lineno_out
#define _bfd_XXi_swap_scnhdr_out			_bfd_pei_swap_scnhdr_out
#define _bfd_XXi_swap_sym_in				_bfd_pei_swap_sym_in
#define _bfd_XXi_swap_sym_out				_bfd_pei_swap_sym_out

#endif /* !COFF_WITH_pep */

/* Returns true if the target is a PE executable target.  */
#define bfd_target_pei_p(xvec)				\
  (CONST_STRNEQ ((xvec)->name, "pei-"))

/* Return the arch string of a PE executable target.  */
#define bfd_target_pei_arch(xvec)				\
  ((xvec)->name + sizeof ("pei-") - 1)

/* Returns true if the target is an EFI target.  */
#define bfd_target_efi_p(xvec)				\
   (CONST_STRNEQ ((xvec)->name, "efi-app-"))

/* Return the arch string of an EFI target.  */
#define bfd_target_efi_arch(xvec)				\
  ((xvec)->name + sizeof ("efi-app-") - 1)

/* Macro: Returns true if the bfd is a PE executable as opposed to a
   PE object file.  */
#define bfd_pe_executable_p(abfd)			\
  (bfd_target_pei_p ((abfd)->xvec)			\
   || bfd_target_efi_p ((abfd)->xvec))

/* These functions are architecture dependent, and are in peicode.h:
   coff_swap_reloc_in
   int coff_swap_reloc_out
   coff_swap_filehdr_in
   coff_swap_scnhdr_in
   pe_mkobject
   pe_mkobject_hook  */

/* The functions described below are common across all PE/PEI
   implementations architecture types, and actually appear in
   peigen.c.  */

#define coff_swap_sym_in      _bfd_XXi_swap_sym_in
#define coff_swap_sym_out     _bfd_XXi_swap_sym_out
#define coff_swap_aux_in      _bfd_XXi_swap_aux_in
#define coff_swap_aux_out     _bfd_XXi_swap_aux_out
#define coff_swap_lineno_in   _bfd_XXi_swap_lineno_in
#define coff_swap_lineno_out  _bfd_XXi_swap_lineno_out
#define coff_swap_aouthdr_in  _bfd_XXi_swap_aouthdr_in
#define coff_swap_aouthdr_out _bfd_XXi_swap_aouthdr_out
#define coff_swap_scnhdr_out  _bfd_XXi_swap_scnhdr_out

#ifndef coff_final_link_postscript
#define coff_final_link_postscript _bfd_XXi_final_link_postscript
#endif

void        _bfd_XXi_swap_sym_in (bfd *, void *, void *);
unsigned    _bfd_XXi_swap_sym_out (bfd *, void *, void *);
void        _bfd_XXi_swap_aux_in (bfd *, void *, int, int, int, int, void *);
unsigned    _bfd_XXi_swap_aux_out (bfd *, void *, int, int, int, int, void *);
void        _bfd_XXi_swap_lineno_in (bfd *, void *, void *);
unsigned    _bfd_XXi_swap_lineno_out (bfd *, void *, void *);
void        _bfd_XXi_swap_aouthdr_in (bfd *, void *, void *);
unsigned    _bfd_XXi_swap_aouthdr_out (bfd *, void *, void *);
unsigned    _bfd_XXi_swap_scnhdr_out (bfd *, void *, void *);
bfd_boolean _bfd_XX_print_private_bfd_data_common (bfd *, void *);
bfd_boolean _bfd_XX_bfd_copy_private_bfd_data_common (bfd *, bfd *);
void        _bfd_XX_get_symbol_info (bfd *, asymbol *, symbol_info *);
bfd_boolean _bfd_XXi_final_link_postscript (bfd *, struct coff_final_link_info *);

/* The following are needed only for ONE of pe or pei, but don't
   otherwise vary; peicode.h fixes up ifdefs but we provide the
   prototype.  */

unsigned    _bfd_XX_only_swap_filehdr_out  (bfd *, void *, void *);
unsigned    _bfd_XXi_only_swap_filehdr_out (bfd *, void *, void *);
bfd_boolean _bfd_XX_bfd_copy_private_section_data (bfd *, asection *, bfd *, asection *);

