# This shell script emits a C file. -*- C -*-
#   Copyright 2006 Free Software Foundation, Inc.
#
# This file is part of GLD, the Gnu Linker.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.
#

# This file is sourced from elf32.em and from ELF targets that use
# generic.em.
#
cat >>e${EMULATION_NAME}.c <<EOF

static void
gld${EMULATION_NAME}_map_segments (bfd_boolean need_layout)
{
  int tries = 10;

  do
    {
      if (need_layout)
	{
	  lang_reset_memory_regions ();

	  /* Resize the sections.  */
	  lang_size_sections (NULL, TRUE);

	  /* Redo special stuff.  */
	  ldemul_after_allocation ();

	  /* Do the assignments again.  */
	  lang_do_assignments ();

	  need_layout = FALSE;
	}

      if (output_bfd->xvec->flavour == bfd_target_elf_flavour
	  && !link_info.relocatable)
	{
	  bfd_size_type phdr_size;

	  phdr_size = elf_tdata (output_bfd)->program_header_size;
	  /* If we don't have user supplied phdrs, throw away any
	     previous linker generated program headers.  */
	  if (lang_phdr_list == NULL)
	    elf_tdata (output_bfd)->segment_map = NULL;
	  if (!_bfd_elf_map_sections_to_segments (output_bfd, &link_info))
	    einfo ("%F%P: map sections to segments failed: %E\n");

	  if (phdr_size != elf_tdata (output_bfd)->program_header_size)
	    {
	      if (tries > 6)
		/* The first few times we allow any change to
		   phdr_size .  */
		need_layout = TRUE;
	      else if (phdr_size < elf_tdata (output_bfd)->program_header_size)
		/* After that we only allow the size to grow.  */
		need_layout = TRUE;
	      else
		elf_tdata (output_bfd)->program_header_size = phdr_size;
	    }
	}
    }
  while (need_layout && --tries);

  if (tries == 0)
    einfo (_("%P%F: looping in map_segments"));
}
EOF
