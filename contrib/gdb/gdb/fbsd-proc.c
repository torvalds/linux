/* FreeBSD-specific methods for using the /proc file system.

   Copyright 2002, 2003 Free Software Foundation, Inc.

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
#include "gdbcore.h"
#include "inferior.h"
#include "gdb_string.h"

#include <sys/procfs.h>
#include <sys/types.h>

#include "elf-bfd.h"

#include "gregset.h"

char *
child_pid_to_exec_file (int pid)
{
  char *path;
  char *buf;

  xasprintf (&path, "/proc/%d/file", pid);
  buf = xcalloc (MAXPATHLEN, sizeof (char));
  make_cleanup (xfree, path);
  make_cleanup (xfree, buf);

  if (readlink (path, buf, MAXPATHLEN) > 0)
    return buf;

  return NULL;
}

static int
read_mapping (FILE *mapfile, unsigned long *start, unsigned long *end,
	      char *protection)
{
  /* FreeBSD 5.1-RELEASE uses a 256-byte buffer.  */
  char buf[256];
  int resident, privateresident;
  unsigned long obj;
  int ret = EOF;

  /* As of FreeBSD 5.0-RELEASE, the layout is described in
     /usr/src/sys/fs/procfs/procfs_map.c.  Somewhere in 5.1-CURRENT a
     new column was added to the procfs map.  Therefore we can't use
     fscanf since we need to support older releases too.  */
  if (fgets (buf, sizeof buf, mapfile) != NULL)
    ret = sscanf (buf, "%lx %lx %d %d %lx %s", start, end,
		  &resident, &privateresident, &obj, protection);

  return (ret != 0 && ret != EOF);
}

static int
fbsd_find_memory_regions (int (*func) (CORE_ADDR, unsigned long,
				       int, int, int, void *),
			  void *obfd)
{
  pid_t pid = ptid_get_pid (inferior_ptid);
  char *mapfilename;
  FILE *mapfile;
  unsigned long start, end, size;
  char protection[4];
  int read, write, exec;

  xasprintf (&mapfilename, "/proc/%ld/map", (long) pid);
  mapfile = fopen (mapfilename, "r");
  if (mapfile == NULL)
    error ("Couldn't open %s\n", mapfilename);

  if (info_verbose)
    fprintf_filtered (gdb_stdout, 
		      "Reading memory regions from %s\n", mapfilename);

  /* Now iterate until end-of-file.  */
  while (read_mapping (mapfile, &start, &end, &protection[0]))
    {
      size = end - start;

      read = (strchr (protection, 'r') != 0);
      write = (strchr (protection, 'w') != 0);
      exec = (strchr (protection, 'x') != 0);

      if (info_verbose)
	{
	  fprintf_filtered (gdb_stdout, 
			    "Save segment, %ld bytes at 0x%s (%c%c%c)\n", 
			    size, paddr_nz (start),
			    read ? 'r' : '-',
			    write ? 'w' : '-',
			    exec ? 'x' : '-');
	}

      /* Invoke the callback function to create the corefile segment. */
      func (start, size, read, write, exec, obfd);
    }

  fclose (mapfile);
  return 0;
}

static char *
fbsd_make_corefile_notes (bfd *obfd, int *note_size)
{
  gregset_t gregs;
  fpregset_t fpregs;
  char *note_data = NULL;
  Elf_Internal_Ehdr *i_ehdrp;
  char fakename;

  /* Put a "FreeBSD" label in the ELF header.  */
  i_ehdrp = elf_elfheader (obfd);
  i_ehdrp->e_ident[EI_OSABI] = ELFOSABI_FREEBSD;

  fill_gregset (&gregs, -1);
  note_data = elfcore_write_prstatus (obfd, note_data, note_size,
				      ptid_get_pid (inferior_ptid),
				      stop_signal, &gregs);

  fill_fpregset (&fpregs, -1);
  note_data = elfcore_write_prfpreg (obfd, note_data, note_size,
				     &fpregs, sizeof (fpregs));

  fakename = '\0';
  note_data = elfcore_write_thrmisc (obfd, note_data, note_size,
				     &fakename, sizeof (fakename));

  if (get_exec_file (0))
    {
      char *fname = strrchr (get_exec_file (0), '/') + 1;
      char *psargs = xstrdup (fname);

      if (get_inferior_args ())
	psargs = reconcat (psargs, psargs, " ", get_inferior_args (), NULL);

      note_data = elfcore_write_prpsinfo (obfd, note_data, note_size,
					  fname, psargs);
    }

  make_cleanup (xfree, note_data);
  return note_data;
}


void
_initialize_fbsd_proc (void)
{
  extern void inftarg_set_find_memory_regions ();
  extern void inftarg_set_make_corefile_notes ();

  inftarg_set_find_memory_regions (fbsd_find_memory_regions);
  inftarg_set_make_corefile_notes (fbsd_make_corefile_notes);
}
