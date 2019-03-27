/* OS ABI variant handling for GDB.
   Copyright 2001, 2002, 2003 Free Software Foundation, Inc.
   
   This file is part of GDB.

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
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#ifndef OSABI_H
#define OSABI_H

/* Register an OS ABI sniffer.  Each arch/flavour may have more than
   one sniffer.  This is used to e.g. differentiate one OS's a.out from
   another.  The first sniffer to return something other than
   GDB_OSABI_UNKNOWN wins, so a sniffer should be careful to claim a file
   only if it knows for sure what it is.  */
void gdbarch_register_osabi_sniffer (enum bfd_architecture,
				     enum bfd_flavour,
				     enum gdb_osabi (*)(bfd *));

/* Register a handler for an OS ABI variant for a given architecture
   and machine type.  There should be only one handler for a given OS
   ABI for each architecture and machine type combination.  */
void gdbarch_register_osabi (enum bfd_architecture, unsigned long,
			     enum gdb_osabi,
                             void (*)(struct gdbarch_info,
				      struct gdbarch *));

/* Lookup the OS ABI corresponding to the specified BFD.  */
enum gdb_osabi gdbarch_lookup_osabi (bfd *);

/* Initialize the gdbarch for the specified OS ABI variant.  */
void gdbarch_init_osabi (struct gdbarch_info, struct gdbarch *);

/* Return the name of the specified OS ABI.  */
const char *gdbarch_osabi_name (enum gdb_osabi);

/* Helper routine for ELF file sniffers.  This looks at ABI tag note
   sections to determine the OS ABI from the note.  It should be called
   via bfd_map_over_sections.  */
void generic_elf_osabi_sniff_abi_tag_sections (bfd *, asection *, void *);

#endif /* OSABI_H */
