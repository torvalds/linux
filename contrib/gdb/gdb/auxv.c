/* Auxiliary vector support for GDB, the GNU debugger.

   Copyright 2004 Free Software Foundation, Inc.

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
#include "target.h"
#include "gdbtypes.h"
#include "command.h"
#include "inferior.h"
#include "valprint.h"
#include "gdb_assert.h"

#include "auxv.h"
#include "elf/common.h"

#include <unistd.h>
#include <fcntl.h>


/* This function is called like a to_xfer_partial hook,
   but must be called with TARGET_OBJECT_AUXV.
   It handles access via /proc/PID/auxv, which is the common method.
   This function is appropriate for doing:
	   #define NATIVE_XFER_AUXV	procfs_xfer_auxv
   for a native target that uses inftarg.c's child_xfer_partial hook.  */

LONGEST
procfs_xfer_auxv (struct target_ops *ops,
		  int /* enum target_object */ object,
		  const char *annex,
		  void *readbuf,
		  const void *writebuf,
		  ULONGEST offset,
		  LONGEST len)
{
  char *pathname;
  int fd;
  LONGEST n;

  gdb_assert (object == TARGET_OBJECT_AUXV);
  gdb_assert (readbuf || writebuf);

  pathname = xstrprintf ("/proc/%d/auxv", PIDGET (inferior_ptid));
  fd = open (pathname, writebuf != NULL ? O_WRONLY : O_RDONLY);
  xfree (pathname);
  if (fd < 0)
    return -1;

  if (offset != (ULONGEST) 0
      && lseek (fd, (off_t) offset, SEEK_SET) != (off_t) offset)
    n = -1;
  else if (readbuf != NULL)
    n = read (fd, readbuf, len);
  else
    n = write (fd, writebuf, len);

  (void) close (fd);

  return n;
}

/* Read all the auxv data into a contiguous xmalloc'd buffer,
   stored in *DATA.  Return the size in bytes of this data.
   If zero, there is no data and *DATA is null.
   if < 0, there was an error and *DATA is null.  */
LONGEST
target_auxv_read (struct target_ops *ops, char **data)
{
  size_t auxv_alloc = 512, auxv_pos = 0;
  char *auxv = xmalloc (auxv_alloc);
  int n;

  while (1)
    {
      n = target_read_partial (ops, TARGET_OBJECT_AUXV,
			       NULL, &auxv[auxv_pos], 0,
			       auxv_alloc - auxv_pos);
      if (n <= 0)
	break;
      auxv_pos += n;
      if (auxv_pos < auxv_alloc) /* Read all there was.  */
	break;
      gdb_assert (auxv_pos == auxv_alloc);
      auxv_alloc *= 2;
      auxv = xrealloc (auxv, auxv_alloc);
    }

  if (auxv_pos == 0)
    {
      xfree (auxv);
      *data = NULL;
      return n;
    }

  *data = auxv;
  return auxv_pos;
}

/* Read one auxv entry from *READPTR, not reading locations >= ENDPTR.
   Return 0 if *READPTR is already at the end of the buffer.
   Return -1 if there is insufficient buffer for a whole entry.
   Return 1 if an entry was read into *TYPEP and *VALP.  */
int
target_auxv_parse (struct target_ops *ops, char **readptr, char *endptr,
		   CORE_ADDR *typep, CORE_ADDR *valp)
{
  const int sizeof_auxv_field = TYPE_LENGTH (builtin_type_void_data_ptr);
  char *ptr = *readptr;

  if (endptr == ptr)
    return 0;

  if (endptr - ptr < sizeof_auxv_field * 2)
    return -1;

  *typep = extract_unsigned_integer (ptr, sizeof_auxv_field);
  ptr += sizeof_auxv_field;
  *valp = extract_unsigned_integer (ptr, sizeof_auxv_field);
  ptr += sizeof_auxv_field;

  *readptr = ptr;
  return 1;
}

/* Extract the auxiliary vector entry with a_type matching MATCH.
   Return zero if no such entry was found, or -1 if there was
   an error getting the information.  On success, return 1 after
   storing the entry's value field in *VALP.  */
int
target_auxv_search (struct target_ops *ops, CORE_ADDR match, CORE_ADDR *valp)
{
  CORE_ADDR type, val;
  char *data;
  int n = target_auxv_read (ops, &data);
  char *ptr = data;
  int ents = 0;

  if (n <= 0)
    return n;

  while (1)
    switch (target_auxv_parse (ops, &ptr, data + n, &type, &val))
      {
      case 1:			/* Here's an entry, check it.  */
	if (type == match)
	  {
	    xfree (data);
	    *valp = val;
	    return 1;
	  }
	break;
      case 0:			/* End of the vector.  */
	xfree (data);
	return 0;
      default:			/* Bogosity.  */
	xfree (data);
	return -1;
      }

  /*NOTREACHED*/
}


/* Print the contents of the target's AUXV on the specified file. */
int
fprint_target_auxv (struct ui_file *file, struct target_ops *ops)
{
  CORE_ADDR type, val;
  char *data;
  int len = target_auxv_read (ops, &data);
  char *ptr = data;
  int ents = 0;

  if (len <= 0)
    return len;

  while (target_auxv_parse (ops, &ptr, data + len, &type, &val) > 0)
    {
      extern int addressprint;
      const char *name = "???";
      const char *description = "";
      enum { dec, hex, str } flavor = hex;

      switch (type)
	{
#define TAG(tag, text, kind) \
	case tag: name = #tag; description = text; flavor = kind; break
	  TAG (AT_NULL, "End of vector", hex);
	  TAG (AT_IGNORE, "Entry should be ignored", hex);
	  TAG (AT_EXECFD, "File descriptor of program", dec);
	  TAG (AT_PHDR, "Program headers for program", hex);
	  TAG (AT_PHENT, "Size of program header entry", dec);
	  TAG (AT_PHNUM, "Number of program headers", dec);
	  TAG (AT_PAGESZ, "System page size", dec);
	  TAG (AT_BASE, "Base address of interpreter", hex);
	  TAG (AT_FLAGS, "Flags", hex);
	  TAG (AT_ENTRY, "Entry point of program", hex);
	  TAG (AT_NOTELF, "Program is not ELF", dec);
	  TAG (AT_UID, "Real user ID", dec);
	  TAG (AT_EUID, "Effective user ID", dec);
	  TAG (AT_GID, "Real group ID", dec);
	  TAG (AT_EGID, "Effective group ID", dec);
	  TAG (AT_CLKTCK, "Frequency of times()", dec);
	  TAG (AT_PLATFORM, "String identifying platform", str);
	  TAG (AT_HWCAP, "Machine-dependent CPU capability hints", hex);
	  TAG (AT_FPUCW, "Used FPU control word", dec);
	  TAG (AT_DCACHEBSIZE, "Data cache block size", dec);
	  TAG (AT_ICACHEBSIZE, "Instruction cache block size", dec);
	  TAG (AT_UCACHEBSIZE, "Unified cache block size", dec);
	  TAG (AT_IGNOREPPC, "Entry should be ignored", dec);
	  TAG (AT_SYSINFO, "Special system info/entry points", hex);
	  TAG (AT_SYSINFO_EHDR, "System-supplied DSO's ELF header", hex);
	  TAG (AT_SECURE, "Boolean, was exec setuid-like?", dec);
	  TAG (AT_SUN_UID, "Effective user ID", dec);
	  TAG (AT_SUN_RUID, "Real user ID", dec);
	  TAG (AT_SUN_GID, "Effective group ID", dec);
	  TAG (AT_SUN_RGID, "Real group ID", dec);
	  TAG (AT_SUN_LDELF, "Dynamic linker's ELF header", hex);
	  TAG (AT_SUN_LDSHDR, "Dynamic linker's section headers", hex);
	  TAG (AT_SUN_LDNAME, "String giving name of dynamic linker", str);
	  TAG (AT_SUN_LPAGESZ, "Large pagesize", dec);
	  TAG (AT_SUN_PLATFORM, "Platform name string", str);
	  TAG (AT_SUN_HWCAP, "Machine-dependent CPU capability hints", hex);
	  TAG (AT_SUN_IFLUSH, "Should flush icache?", dec);
	  TAG (AT_SUN_CPU, "CPU name string", str);
	  TAG (AT_SUN_EMUL_ENTRY, "COFF entry point address", hex);
	  TAG (AT_SUN_EMUL_EXECFD, "COFF executable file descriptor", dec);
	  TAG (AT_SUN_EXECNAME,
	       "Canonicalized file name given to execve", str);
	  TAG (AT_SUN_MMU, "String for name of MMU module", str);
	  TAG (AT_SUN_LDDATA, "Dynamic linker's data segment address", hex);
	}

      fprintf_filtered (file, "%-4s %-20s %-30s ",
			paddr_d (type), name, description);
      switch (flavor)
	{
	case dec:
	  fprintf_filtered (file, "%s\n", paddr_d (val));
	  break;
	case hex:
	  fprintf_filtered (file, "0x%s\n", paddr_nz (val));
	  break;
	case str:
	  if (addressprint)
	    fprintf_filtered (file, "0x%s", paddr_nz (val));
	  val_print_string (val, -1, 1, file);
	  fprintf_filtered (file, "\n");
	  break;
	}
      ++ents;
    }

  xfree (data);

  return ents;
}

static void
info_auxv_command (char *cmd, int from_tty)
{
  if (! target_has_stack)
    error ("The program has no auxiliary information now.");
  else
    {
      int ents = fprint_target_auxv (gdb_stdout, &current_target);
      if (ents < 0)
	error ("No auxiliary vector found, or failed reading it.");
      else if (ents == 0)
	error ("Auxiliary vector is empty.");
    }
}


extern initialize_file_ftype _initialize_auxv; /* -Wmissing-prototypes; */

void
_initialize_auxv (void)
{
  add_info ("auxv", info_auxv_command,
	    "Display the inferior's auxiliary vector.\n\
This is information provided by the operating system at program startup.");
}
