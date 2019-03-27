/* Initialize "struct disassemble_info".

   Copyright 2003 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#include "sysdep.h"
#include "dis-asm.h"
#include "bfd.h"

void
init_disassemble_info (struct disassemble_info *info, void *stream,
		       fprintf_ftype fprintf_func)
{
  memset (info, 0, sizeof (*info));

  info->flavour = bfd_target_unknown_flavour;
  info->arch = bfd_arch_unknown;
  info->endian = BFD_ENDIAN_UNKNOWN;
  info->octets_per_byte = 1;
  info->fprintf_func = fprintf_func;
  info->stream = stream;
  info->read_memory_func = buffer_read_memory;
  info->memory_error_func = perror_memory;
  info->print_address_func = generic_print_address;
  info->symbol_at_address_func = generic_symbol_at_address;
  info->symbol_is_valid = generic_symbol_is_valid;
  info->display_endian = BFD_ENDIAN_UNKNOWN;
}

