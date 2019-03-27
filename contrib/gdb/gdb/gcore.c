/* Generate a core file for the inferior process.

   Copyright 2001, 2002, 2003, 2004 Free Software Foundation, Inc.

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

#include "defs.h"
#include "elf-bfd.h"
#include "infcall.h"
#include "inferior.h"
#include "gdbcore.h"
#include "objfiles.h"
#include "symfile.h"

#include "cli/cli-decode.h"

#include "gdb_assert.h"

static char *default_gcore_target (void);
static enum bfd_architecture default_gcore_arch (void);
static unsigned long default_gcore_mach (void);
static int gcore_memory_sections (bfd *);

/* Generate a core file from the inferior process.  */

static void
gcore_command (char *args, int from_tty)
{
  struct cleanup *old_chain;
  char *corefilename, corefilename_buffer[40];
  asection *note_sec = NULL;
  bfd *obfd;
  void *note_data = NULL;
  int note_size = 0;

  /* No use generating a corefile without a target process.  */
  if (!target_has_execution)
    noprocess ();

  if (args && *args)
    corefilename = args;
  else
    {
      /* Default corefile name is "core.PID".  */
      sprintf (corefilename_buffer, "core.%d", PIDGET (inferior_ptid));
      corefilename = corefilename_buffer;
    }

  if (info_verbose)
    fprintf_filtered (gdb_stdout,
		      "Opening corefile '%s' for output.\n", corefilename);

  /* Open the output file.  */
  obfd = bfd_openw (corefilename, default_gcore_target ());
  if (!obfd)
    error ("Failed to open '%s' for output.", corefilename);

  /* Need a cleanup that will close the file (FIXME: delete it?).  */
  old_chain = make_cleanup_bfd_close (obfd);

  bfd_set_format (obfd, bfd_core);
  bfd_set_arch_mach (obfd, default_gcore_arch (), default_gcore_mach ());

  /* An external target method must build the notes section.  */
  note_data = target_make_corefile_notes (obfd, &note_size);

  /* Create the note section.  */
  if (note_data != NULL && note_size != 0)
    {
      note_sec = bfd_make_section_anyway (obfd, "note0");
      if (note_sec == NULL)
	error ("Failed to create 'note' section for corefile: %s",
	       bfd_errmsg (bfd_get_error ()));

      bfd_set_section_vma (obfd, note_sec, 0);
      bfd_set_section_flags (obfd, note_sec,
			     SEC_HAS_CONTENTS | SEC_READONLY | SEC_ALLOC);
      bfd_set_section_alignment (obfd, note_sec, 0);
      bfd_set_section_size (obfd, note_sec, note_size);
    }

  /* Now create the memory/load sections.  */
  if (gcore_memory_sections (obfd) == 0)
    error ("gcore: failed to get corefile memory sections from target.");

  /* Write out the contents of the note section.  */
  if (note_data != NULL && note_size != 0)
    {
      if (!bfd_set_section_contents (obfd, note_sec, note_data, 0, note_size))
	warning ("writing note section (%s)", bfd_errmsg (bfd_get_error ()));
    }

  /* Succeeded.  */
  fprintf_filtered (gdb_stdout, "Saved corefile %s\n", corefilename);

  /* Clean-ups will close the output file and free malloc memory.  */
  do_cleanups (old_chain);
  return;
}

static unsigned long
default_gcore_mach (void)
{
#if 1	/* See if this even matters...  */
  return 0;
#else
#ifdef TARGET_ARCHITECTURE
  const struct bfd_arch_info *bfdarch = TARGET_ARCHITECTURE;

  if (bfdarch != NULL)
    return bfdarch->mach;
#endif /* TARGET_ARCHITECTURE */
  if (exec_bfd == NULL)
    error ("Can't find default bfd machine type (need execfile).");

  return bfd_get_mach (exec_bfd);
#endif /* 1 */
}

static enum bfd_architecture
default_gcore_arch (void)
{
#ifdef TARGET_ARCHITECTURE
  const struct bfd_arch_info * bfdarch = TARGET_ARCHITECTURE;

  if (bfdarch != NULL)
    return bfdarch->arch;
#endif
  if (exec_bfd == NULL)
    error ("Can't find bfd architecture for corefile (need execfile).");

  return bfd_get_arch (exec_bfd);
}

static char *
default_gcore_target (void)
{
  /* FIXME: This may only work for ELF targets.  */
  if (exec_bfd == NULL)
    return NULL;
  else
    return bfd_get_target (exec_bfd);
}

/* Derive a reasonable stack segment by unwinding the target stack,
   and store its limits in *BOTTOM and *TOP.  Return non-zero if
   successful.  */

static int
derive_stack_segment (bfd_vma *bottom, bfd_vma *top)
{
  struct frame_info *fi, *tmp_fi;

  gdb_assert (bottom);
  gdb_assert (top);

  /* Can't succeed without stack and registers.  */
  if (!target_has_stack || !target_has_registers)
    return 0;

  /* Can't succeed without current frame.  */
  fi = get_current_frame ();
  if (fi == NULL)
    return 0;

  /* Save frame pointer of TOS frame.  */
  *top = get_frame_base (fi);
  /* If current stack pointer is more "inner", use that instead.  */
  if (INNER_THAN (read_sp (), *top))
    *top = read_sp ();

  /* Find prev-most frame.  */
  while ((tmp_fi = get_prev_frame (fi)) != NULL)
    fi = tmp_fi;

  /* Save frame pointer of prev-most frame.  */
  *bottom = get_frame_base (fi);

  /* Now canonicalize their order, so that BOTTOM is a lower address
     (as opposed to a lower stack frame).  */
  if (*bottom > *top)
    {
      bfd_vma tmp_vma;

      tmp_vma = *top;
      *top = *bottom;
      *bottom = tmp_vma;
    }

  return 1;
}

/* Derive a reasonable heap segment for ABFD by looking at sbrk and
   the static data sections.  Store its limits in *BOTTOM and *TOP.
   Return non-zero if successful.  */

static int
derive_heap_segment (bfd *abfd, bfd_vma *bottom, bfd_vma *top)
{
  bfd_vma top_of_data_memory = 0;
  bfd_vma top_of_heap = 0;
  bfd_size_type sec_size;
  struct value *zero, *sbrk;
  bfd_vma sec_vaddr;
  asection *sec;

  gdb_assert (bottom);
  gdb_assert (top);

  /* This function depends on being able to call a function in the
     inferior.  */
  if (!target_has_execution)
    return 0;

  /* The following code assumes that the link map is arranged as
     follows (low to high addresses):

     ---------------------------------
     | text sections                 |
     ---------------------------------
     | data sections (including bss) |
     ---------------------------------
     | heap                          |
     --------------------------------- */

  for (sec = abfd->sections; sec; sec = sec->next)
    {
      if (bfd_get_section_flags (abfd, sec) & SEC_DATA
	  || strcmp (".bss", bfd_section_name (abfd, sec)) == 0)
	{
	  sec_vaddr = bfd_get_section_vma (abfd, sec);
	  sec_size = bfd_get_section_size (sec);
	  if (sec_vaddr + sec_size > top_of_data_memory)
	    top_of_data_memory = sec_vaddr + sec_size;
	}
    }

  /* Now get the top-of-heap by calling sbrk in the inferior.  */
  if (lookup_minimal_symbol ("sbrk", NULL, NULL) != NULL)
    {
      sbrk = find_function_in_inferior ("sbrk");
      if (sbrk == NULL)
	return 0;
    }
  else if (lookup_minimal_symbol ("_sbrk", NULL, NULL) != NULL)
    {
      sbrk = find_function_in_inferior ("_sbrk");
      if (sbrk == NULL)
	return 0;
    }
  else
    return 0;

  zero = value_from_longest (builtin_type_int, 0);
  gdb_assert (zero);
  sbrk = call_function_by_hand (sbrk, 1, &zero);
  if (sbrk == NULL)
    return 0;
  top_of_heap = value_as_long (sbrk);

  /* Return results.  */
  if (top_of_heap > top_of_data_memory)
    {
      *bottom = top_of_data_memory;
      *top = top_of_heap;
      return 1;
    }

  /* No additional heap space needs to be saved.  */
  return 0;
}

static void
make_output_phdrs (bfd *obfd, asection *osec, void *ignored)
{
  int p_flags = 0;
  int p_type;

  /* FIXME: these constants may only be applicable for ELF.  */
  if (strncmp (bfd_section_name (obfd, osec), "load", 4) == 0)
    p_type = PT_LOAD;
  else
    p_type = PT_NOTE;

  p_flags |= PF_R;	/* Segment is readable.  */
  if (!(bfd_get_section_flags (obfd, osec) & SEC_READONLY))
    p_flags |= PF_W;	/* Segment is writable.  */
  if (bfd_get_section_flags (obfd, osec) & SEC_CODE)
    p_flags |= PF_X;	/* Segment is executable.  */

  bfd_record_phdr (obfd, p_type, 1, p_flags, 0, 0, 0, 0, 1, &osec);
}

static int
gcore_create_callback (CORE_ADDR vaddr, unsigned long size,
		       int read, int write, int exec, void *data)
{
  bfd *obfd = data;
  asection *osec;
  flagword flags = SEC_ALLOC | SEC_HAS_CONTENTS | SEC_LOAD;

  /* If the memory segment has no permissions set, ignore it, otherwise
     when we later try to access it for read/write, we'll get an error
     or jam the kernel.  */
  if (read == 0 && write == 0 && exec == 0)
    {
      if (info_verbose)
        {
          fprintf_filtered (gdb_stdout, "Ignore segment, %s bytes at 0x%s\n",
                           paddr_d (size), paddr_nz (vaddr));
        }

      return 0;
    }

  if (write == 0)
    {
      /* See if this region of memory lies inside a known file on disk.
	 If so, we can avoid copying its contents by clearing SEC_LOAD.  */
      struct objfile *objfile;
      struct obj_section *objsec;

      ALL_OBJSECTIONS (objfile, objsec)
	{
	  bfd *abfd = objfile->obfd;
	  asection *asec = objsec->the_bfd_section;
	  bfd_vma align = (bfd_vma) 1 << bfd_get_section_alignment (abfd,
								    asec);
	  bfd_vma start = objsec->addr & -align;
	  bfd_vma end = (objsec->endaddr + align - 1) & -align;
	  /* Match if either the entire memory region lies inside the
	     section (i.e. a mapping covering some pages of a large
	     segment) or the entire section lies inside the memory region
	     (i.e. a mapping covering multiple small sections).

	     This BFD was synthesized from reading target memory,
	     we don't want to omit that.  */
	  if (((vaddr >= start && vaddr + size <= end)
	       || (start >= vaddr && end <= vaddr + size))
	      && !(bfd_get_file_flags (abfd) & BFD_IN_MEMORY))
	    {
	      flags &= ~SEC_LOAD;
	      goto keep;	/* break out of two nested for loops */
	    }
	}

    keep:
      flags |= SEC_READONLY;
    }

  if (exec)
    flags |= SEC_CODE;
  else
    flags |= SEC_DATA;

  osec = bfd_make_section_anyway (obfd, "load");
  if (osec == NULL)
    {
      warning ("Couldn't make gcore segment: %s",
	       bfd_errmsg (bfd_get_error ()));
      return 1;
    }

  if (info_verbose)
    {
      fprintf_filtered (gdb_stdout, "Save segment, %s bytes at 0x%s\n",
			paddr_d (size), paddr_nz (vaddr));
    }

  bfd_set_section_size (obfd, osec, size);
  bfd_set_section_vma (obfd, osec, vaddr);
  bfd_section_lma (obfd, osec) = 0; /* ??? bfd_set_section_lma?  */
  bfd_set_section_flags (obfd, osec, flags);
  return 0;
}

static int
objfile_find_memory_regions (int (*func) (CORE_ADDR, unsigned long,
					  int, int, int, void *),
			     void *obfd)
{
  /* Use objfile data to create memory sections.  */
  struct objfile *objfile;
  struct obj_section *objsec;
  bfd_vma temp_bottom, temp_top;

  /* Call callback function for each objfile section.  */
  ALL_OBJSECTIONS (objfile, objsec)
    {
      bfd *ibfd = objfile->obfd;
      asection *isec = objsec->the_bfd_section;
      flagword flags = bfd_get_section_flags (ibfd, isec);
      int ret;

      if ((flags & SEC_ALLOC) || (flags & SEC_LOAD))
	{
	  int size = bfd_section_size (ibfd, isec);
	  int ret;

	  ret = (*func) (objsec->addr, bfd_section_size (ibfd, isec),
			 1, /* All sections will be readable.  */
			 (flags & SEC_READONLY) == 0, /* Writable.  */
			 (flags & SEC_CODE) != 0, /* Executable.  */
			 obfd);
	  if (ret != 0)
	    return ret;
	}
    }

  /* Make a stack segment.  */
  if (derive_stack_segment (&temp_bottom, &temp_top))
    (*func) (temp_bottom, temp_top - temp_bottom,
	     1, /* Stack section will be readable.  */
	     1, /* Stack section will be writable.  */
	     0, /* Stack section will not be executable.  */
	     obfd);

  /* Make a heap segment. */
  if (derive_heap_segment (exec_bfd, &temp_bottom, &temp_top))
    (*func) (temp_bottom, temp_top - temp_bottom,
	     1, /* Heap section will be readable.  */
	     1, /* Heap section will be writable.  */
	     0, /* Heap section will not be executable.  */
	     obfd);

  return 0;
}

static void
gcore_copy_callback (bfd *obfd, asection *osec, void *ignored)
{
  bfd_size_type size = bfd_section_size (obfd, osec);
  struct cleanup *old_chain = NULL;
  void *memhunk;

  /* Read-only sections are marked; we don't have to copy their contents.  */
  if ((bfd_get_section_flags (obfd, osec) & SEC_LOAD) == 0)
    return;

  /* Only interested in "load" sections.  */
  if (strncmp ("load", bfd_section_name (obfd, osec), 4) != 0)
    return;

  memhunk = xmalloc (size);
  /* ??? This is crap since xmalloc should never return NULL.  */
  if (memhunk == NULL)
    error ("Not enough memory to create corefile.");
  old_chain = make_cleanup (xfree, memhunk);

  if (target_read_memory (bfd_section_vma (obfd, osec),
			  memhunk, size) != 0)
    warning ("Memory read failed for corefile section, %s bytes at 0x%s\n",
	     paddr_d (size), paddr (bfd_section_vma (obfd, osec)));
  if (!bfd_set_section_contents (obfd, osec, memhunk, 0, size))
    warning ("Failed to write corefile contents (%s).",
	     bfd_errmsg (bfd_get_error ()));

  do_cleanups (old_chain);	/* Frees MEMHUNK.  */
}

static int
gcore_memory_sections (bfd *obfd)
{
  if (target_find_memory_regions (gcore_create_callback, obfd) != 0)
    return 0;			/* FIXME: error return/msg?  */

  /* Record phdrs for section-to-segment mapping.  */
  bfd_map_over_sections (obfd, make_output_phdrs, NULL);

  /* Copy memory region contents.  */
  bfd_map_over_sections (obfd, gcore_copy_callback, NULL);

  return 1;
}

void
_initialize_gcore (void)
{
  add_com ("generate-core-file", class_files, gcore_command,
	   "\
Save a core file with the current state of the debugged process.\n\
Argument is optional filename.  Default filename is 'core.<process_id>'.");

  add_com_alias ("gcore", "generate-core-file", class_files, 1);
  exec_set_find_memory_regions (objfile_find_memory_regions);
}
