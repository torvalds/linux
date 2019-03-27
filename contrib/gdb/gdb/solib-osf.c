/* Handle OSF/1, Digital UNIX, and Tru64 shared libraries
   for GDB, the GNU Debugger.
   Copyright 1993, 1994, 1995, 1996, 1998, 1999, 2000, 2001
   Free Software Foundation, Inc.

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

/* When handling shared libraries, GDB has to find out the pathnames
   of all shared libraries that are currently loaded (to read in their
   symbols) and where the shared libraries are loaded in memory
   (to relocate them properly from their prelinked addresses to the
   current load address).

   Under OSF/1 there are two possibilities to get at this information:

   1) Peek around in the runtime loader structures.
   These are not documented, and they are not defined in the system
   header files. The definitions below were obtained by experimentation,
   but they seem stable enough.

   2) Use the libxproc.a library, which contains the equivalent ldr_*
   routines.  The library is documented in Tru64 5.x, but as of 5.1, it
   only allows a process to examine itself.  On earlier versions, it
   may require that the GDB executable be dynamically linked and that
   NAT_CLIBS include -lxproc -Wl,-expect_unresolved,ldr_process_context
   for GDB and all applications that are using libgdb.

   We will use the peeking approach until libxproc.a works for other
   processes.  */

#include "defs.h"

#include <sys/types.h>
#include <signal.h>
#include "gdb_string.h"

#include "bfd.h"
#include "symtab.h"
#include "symfile.h"
#include "objfiles.h"
#include "target.h"
#include "inferior.h"
#include "solist.h"

#ifdef USE_LDR_ROUTINES
# include <loader.h>
#endif

#ifndef USE_LDR_ROUTINES
/* Definition of runtime loader structures, found by experimentation.  */
#define RLD_CONTEXT_ADDRESS	0x3ffc0000000

/* Per-module information structure referenced by ldr_context_t.head.  */

typedef struct
  {
    CORE_ADDR next;
    CORE_ADDR previous;
    CORE_ADDR unknown1;
    CORE_ADDR module_name;
    CORE_ADDR modinfo_addr;	/* used by next_link_map_member() to detect
				   the end of the shared module list */
    long module_id;
    CORE_ADDR unknown2;
    CORE_ADDR unknown3;
    long region_count;
    CORE_ADDR regioninfo_addr;
  }
ldr_module_info_t;

/* Per-region structure referenced by ldr_module_info_t.regioninfo_addr.  */

typedef struct
  {
    long unknown1;
    CORE_ADDR regionname_addr;
    long protection;
    CORE_ADDR vaddr;
    CORE_ADDR mapaddr;
    long size;
    long unknown2[5];
  }
ldr_region_info_t;

/* Structure at RLD_CONTEXT_ADDRESS specifying the start and finish addresses
   of the shared module list.  */

typedef struct
  {
    CORE_ADDR unknown1;
    CORE_ADDR unknown2;
    CORE_ADDR head;
    CORE_ADDR tail;
  }
ldr_context_t;
#endif   /* !USE_LDR_ROUTINES */

/* Per-section information, stored in struct lm_info.secs.  */

struct lm_sec
  {
    CORE_ADDR offset;		/* difference between default and actual
				   virtual addresses of section .name */
    CORE_ADDR nameaddr;		/* address in inferior of section name */
    const char *name;		/* name of section, null if not fetched */
  };

/* Per-module information, stored in struct so_list.lm_info.  */

struct lm_info
  {
    int isloader;		/* whether the module is /sbin/loader */
    int nsecs;			/* length of .secs */
    struct lm_sec secs[1];	/* variable-length array of sections, sorted
				   by name */
  };

/* Context for iterating through the inferior's shared module list.  */

struct read_map_ctxt
  {
#ifdef USE_LDR_ROUTINES
    ldr_process_t proc;
    ldr_module_t next;
#else
    CORE_ADDR next;		/* next element in module list */
    CORE_ADDR tail;		/* last element in module list */
#endif
  };

/* Forward declaration for this module's autoinit function.  */

extern void _initialize_osf_solib (void);

#ifdef USE_LDR_ROUTINES
# if 0
/* This routine is intended to be called by ldr_* routines to read memory from
   the current target.  Usage:

     ldr_process = ldr_core_process ();
     ldr_set_core_reader (ldr_read_memory);
     ldr_xdetach (ldr_process);
     ldr_xattach (ldr_process);

   ldr_core_process() and ldr_read_memory() are neither documented nor
   declared in system header files.  They work with OSF/1 2.x, and they might
   work with later versions as well.  */

static int
ldr_read_memory (CORE_ADDR memaddr, char *myaddr, int len, int readstring)
{
  int result;
  char *buffer;

  if (readstring)
    {
      target_read_string (memaddr, &buffer, len, &result);
      if (result == 0)
	strcpy (myaddr, buffer);
      xfree (buffer);
    }
  else
    result = target_read_memory (memaddr, myaddr, len);

  if (result != 0)
    result = -result;
  return result;
}
# endif   /* 0 */
#endif   /* USE_LDR_ROUTINES */

/* Comparison for qsort() and bsearch(): return -1, 0, or 1 according to
   whether lm_sec *P1's name is lexically less than, equal to, or greater
   than that of *P2.  */

static int
lm_sec_cmp (const void *p1, const void *p2)
{
  const struct lm_sec *lms1 = p1, *lms2 = p2;
  return strcmp (lms1->name, lms2->name);
}

/* Sort LMI->secs so that osf_relocate_section_addresses() can binary-search
   it.  */

static void
lm_secs_sort (struct lm_info *lmi)
{
  qsort (lmi->secs, lmi->nsecs, sizeof *lmi->secs, lm_sec_cmp);
}

/* Populate name fields of LMI->secs.  */

static void
fetch_sec_names (struct lm_info *lmi)
{
#ifndef USE_LDR_ROUTINES
  int i, errcode;
  struct lm_sec *lms;
  char *name;

  for (i = 0; i < lmi->nsecs; i++)
    {
      lms = lmi->secs + i;
      target_read_string (lms->nameaddr, &name, PATH_MAX, &errcode);
      if (errcode != 0)
	{
	  warning ("unable to read shared sec name at 0x%lx", lms->nameaddr);
	  name = xstrdup ("");
	}
      lms->name = name;
    }
  lm_secs_sort (lmi);
#endif
}

/* target_so_ops callback.  Adjust SEC's addresses after it's been mapped into
   the process.  */

static void
osf_relocate_section_addresses (struct so_list *so,
				struct section_table *sec)
{
  struct lm_info *lmi;
  struct lm_sec lms_key, *lms;

  /* Fetch SO's section names if we haven't done so already.  */
  lmi = so->lm_info;
  if (lmi->nsecs && !lmi->secs[0].name)
    fetch_sec_names (lmi);

  /* Binary-search for offset information corresponding to SEC.  */
  lms_key.name = sec->the_bfd_section->name;
  lms = bsearch (&lms_key, lmi->secs, lmi->nsecs, sizeof *lms, lm_sec_cmp);
  if (lms)
    {
      sec->addr += lms->offset;
      sec->endaddr += lms->offset;
    }
}

/* target_so_ops callback.  Free parts of SO allocated by this file.  */

static void
osf_free_so (struct so_list *so)
{
  int i;
  const char *name;

  for (i = 0; i < so->lm_info->nsecs; i++)
    {
      name = so->lm_info->secs[i].name;
      if (name)
	xfree ((void *) name);
    }
  xfree (so->lm_info);
}

/* target_so_ops callback.  Discard information accumulated by this file and
   not freed by osf_free_so().  */

static void
osf_clear_solib (void)
{
  return;
}

/* target_so_ops callback.  Prepare to handle shared libraries after the
   inferior process has been created but before it's executed any
   instructions.

   For a statically bound executable, the inferior's first instruction is the
   one at "_start", or a similar text label. No further processing is needed
   in that case.

   For a dynamically bound executable, this first instruction is somewhere
   in the rld, and the actual user executable is not yet mapped in.
   We continue the inferior again, rld then maps in the actual user
   executable and any needed shared libraries and then sends
   itself a SIGTRAP.

   At that point we discover the names of all shared libraries and
   read their symbols in.

   FIXME

   This code does not properly handle hitting breakpoints which the
   user might have set in the rld itself.  Proper handling would have
   to check if the SIGTRAP happened due to a kill call.

   Also, what if child has exit()ed?  Must exit loop somehow.  */

static void
osf_solib_create_inferior_hook (void)
{
  /* Nothing to do for statically bound executables.  */

  if (symfile_objfile == NULL
      || symfile_objfile->obfd == NULL
      || ((bfd_get_file_flags (symfile_objfile->obfd) & DYNAMIC) == 0))
    return;

  /* Now run the target.  It will eventually get a SIGTRAP, at
     which point all of the libraries will have been mapped in and we
     can go groveling around in the rld structures to find
     out what we need to know about them. */

  clear_proceed_status ();
  stop_soon = STOP_QUIETLY;
  stop_signal = TARGET_SIGNAL_0;
  do
    {
      target_resume (minus_one_ptid, 0, stop_signal);
      wait_for_inferior ();
    }
  while (stop_signal != TARGET_SIGNAL_TRAP);

  /*  solib_add will call reinit_frame_cache.
     But we are stopped in the runtime loader and we do not have symbols
     for the runtime loader. So heuristic_proc_start will be called
     and will put out an annoying warning.
     Delaying the resetting of stop_soon until after symbol loading
     suppresses the warning.  */
  solib_add ((char *) 0, 0, (struct target_ops *) 0, auto_solib_add);
  stop_soon = NO_STOP_QUIETLY;

  /* Enable breakpoints disabled (unnecessarily) by clear_solib().  */
  re_enable_breakpoints_in_shlibs ();
}

/* target_so_ops callback.  Do additional symbol handling, lookup, etc. after
   symbols for a shared object have been loaded.  */

static void
osf_special_symbol_handling (void)
{
  return;
}

/* Initialize CTXT in preparation for iterating through the inferior's module
   list using read_map().  Return success.  */

static int
open_map (struct read_map_ctxt *ctxt)
{
#ifdef USE_LDR_ROUTINES
  /* Note: As originally written, ldr_my_process() was used to obtain
     the value for ctxt->proc.  This is incorrect, however, since
     ldr_my_process() retrieves the "unique identifier" associated
     with the current process (i.e. GDB) and not the one being
     debugged.  Presumably, the pid of the process being debugged is
     compatible with the "unique identifier" used by the ldr_
     routines, so we use that.  */
  ctxt->proc = ptid_get_pid (inferior_ptid);
  if (ldr_xattach (ctxt->proc) != 0)
    return 0;
  ctxt->next = LDR_NULL_MODULE;
#else
  CORE_ADDR ldr_context_addr, prev, next;
  ldr_context_t ldr_context;

  if (target_read_memory ((CORE_ADDR) RLD_CONTEXT_ADDRESS,
			  (char *) &ldr_context_addr,
			  sizeof (CORE_ADDR)) != 0)
    return 0;
  if (target_read_memory (ldr_context_addr,
			  (char *) &ldr_context,
			  sizeof (ldr_context_t)) != 0)
    return 0;
  ctxt->next = ldr_context.head;
  ctxt->tail = ldr_context.tail;
#endif
  return 1;
}

/* Initialize SO to have module NAME, /sbin/loader indicator ISLOADR, and
   space for NSECS sections.  */

static void
init_so (struct so_list *so, char *name, int isloader, int nsecs)
{
  int namelen, i;

  /* solib.c requires various fields to be initialized to 0.  */
  memset (so, 0, sizeof *so);

  /* Copy the name.  */
  namelen = strlen (name);
  if (namelen >= SO_NAME_MAX_PATH_SIZE)
    namelen = SO_NAME_MAX_PATH_SIZE - 1;

  memcpy (so->so_original_name, name, namelen);
  so->so_original_name[namelen] = '\0';
  memcpy (so->so_name, so->so_original_name, namelen + 1);

  /* Allocate section space.  */
  so->lm_info = xmalloc ((unsigned) &(((struct lm_info *)0)->secs) +
			 nsecs * sizeof *so->lm_info);
  so->lm_info->isloader = isloader;
  so->lm_info->nsecs = nsecs;
  for (i = 0; i < nsecs; i++)
    so->lm_info->secs[i].name = NULL;
}

/* Initialize SO's section SECIDX with name address NAMEADDR, name string
   NAME, default virtual address VADDR, and actual virtual address
   MAPADDR.  */

static void
init_sec (struct so_list *so, int secidx, CORE_ADDR nameaddr,
	  const char *name, CORE_ADDR vaddr, CORE_ADDR mapaddr)
{
  struct lm_sec *lms;

  lms = so->lm_info->secs + secidx;
  lms->nameaddr = nameaddr;
  lms->name = name;
  lms->offset = mapaddr - vaddr;
}

/* If there are more elements starting at CTXT in inferior's module list,
   store the next element in SO, advance CTXT to the next element, and return
   1, else return 0.  */

static int
read_map (struct read_map_ctxt *ctxt, struct so_list *so)
{
  ldr_module_info_t minf;
  ldr_region_info_t rinf;

#ifdef USE_LDR_ROUTINES
  size_t size;
  ldr_region_t i;

  /* Retrieve the next element.  */
  if (ldr_next_module (ctxt->proc, &ctxt->next) != 0)
    return 0;
  if (ctxt->next == LDR_NULL_MODULE)
    return 0;
  if (ldr_inq_module (ctxt->proc, ctxt->next, &minf, sizeof minf, &size) != 0)
    return 0;

  /* Initialize the module name and section count.  */
  init_so (so, minf.lmi_name, 0, minf.lmi_nregion);

  /* Retrieve section names and offsets.  */
  for (i = 0; i < minf.lmi_nregion; i++)
    {
      if (ldr_inq_region (ctxt->proc, ctxt->next, i, &rinf,
			  sizeof rinf, &size) != 0)
	goto err;
      init_sec (so, (int) i, 0, xstrdup (rinf.lri_name),
		(CORE_ADDR) rinf.lri_vaddr, (CORE_ADDR) rinf.lri_mapaddr);
    }
  lm_secs_sort (so->lm_info);
#else
  char *name;
  int errcode, i;

  /* Retrieve the next element.  */
  if (!ctxt->next)
    return 0;
  if (target_read_memory (ctxt->next, (char *) &minf, sizeof minf) != 0)
    return 0;
  if (ctxt->next == ctxt->tail)
    ctxt->next = 0;
  else
    ctxt->next = minf.next;

  /* Initialize the module name and section count.  */
  target_read_string (minf.module_name, &name, PATH_MAX, &errcode);
  if (errcode != 0)
    return 0;
  init_so (so, name, !minf.modinfo_addr, minf.region_count);
  xfree (name);

  /* Retrieve section names and offsets.  */
  for (i = 0; i < minf.region_count; i++)
    {
      if (target_read_memory (minf.regioninfo_addr + i * sizeof rinf,
			      (char *) &rinf, sizeof rinf) != 0)
	goto err;
      init_sec (so, i, rinf.regionname_addr, NULL, rinf.vaddr, rinf.mapaddr);
    }
#endif   /* !USE_LDR_ROUTINES */
  return 1;

 err:
  osf_free_so (so);
  return 0;
}

/* Free resources allocated by open_map (CTXT).  */

static void
close_map (struct read_map_ctxt *ctxt)
{
#ifdef USE_LDR_ROUTINES
  ldr_xdetach (ctxt->proc);
#endif
}

/* target_so_ops callback.  Return a list of shared objects currently loaded
   in the inferior.  */

static struct so_list *
osf_current_sos (void)
{
  struct so_list *head = NULL, *tail, *newtail, so;
  struct read_map_ctxt ctxt;
  int skipped_main;

  if (!open_map (&ctxt))
    return NULL;

  /* Read subsequent elements.  */
  for (skipped_main = 0;;)
    {
      if (!read_map (&ctxt, &so))
	break;

      /* Skip the main program module, which is first in the list after
         /sbin/loader.  */
      if (!so.lm_info->isloader && !skipped_main)
	{
	  osf_free_so (&so);
	  skipped_main = 1;
	  continue;
	}

      newtail = xmalloc (sizeof *newtail);
      if (!head)
	head = newtail;
      else
	tail->next = newtail;
      tail = newtail;

      memcpy (tail, &so, sizeof so);
      tail->next = NULL;
    }

 done:
  close_map (&ctxt);
  return head;
}

/* target_so_ops callback.  Attempt to locate and open the main symbol
   file.  */

static int
osf_open_symbol_file_object (void *from_ttyp)
{
  struct read_map_ctxt ctxt;
  struct so_list so;
  int found;

  if (symfile_objfile)
    if (!query ("Attempt to reload symbols from process? "))
      return 0;

  /* The first module after /sbin/loader is the main program.  */
  if (!open_map (&ctxt))
    return 0;
  for (found = 0; !found;)
    {
      if (!read_map (&ctxt, &so))
	break;
      found = !so.lm_info->isloader;
      osf_free_so (&so);
    }
  close_map (&ctxt);

  if (found)
    symbol_file_add_main (so.so_name, *(int *) from_ttyp);
  return found;
}

/* target_so_ops callback.  Return whether PC is in the dynamic linker.  */

static int
osf_in_dynsym_resolve_code (CORE_ADDR pc)
{
  /* This function currently always return False. This is a temporary
     solution which only consequence is to introduce a minor incovenience
     for the user: When stepping inside a subprogram located in a shared
     library, gdb might stop inside the dynamic loader code instead of
     inside the subprogram itself. See the explanations in infrun.c about
     the IN_SOLIB_DYNSYM_RESOLVE_CODE macro for more details. */
  return 0;
}

static struct target_so_ops osf_so_ops;

void
_initialize_osf_solib (void)
{
  osf_so_ops.relocate_section_addresses = osf_relocate_section_addresses;
  osf_so_ops.free_so = osf_free_so;
  osf_so_ops.clear_solib = osf_clear_solib;
  osf_so_ops.solib_create_inferior_hook = osf_solib_create_inferior_hook;
  osf_so_ops.special_symbol_handling = osf_special_symbol_handling;
  osf_so_ops.current_sos = osf_current_sos;
  osf_so_ops.open_symbol_file_object = osf_open_symbol_file_object;
  osf_so_ops.in_dynsym_resolve_code = osf_in_dynsym_resolve_code;

  /* FIXME: Don't do this here.  *_gdbarch_init() should set so_ops. */
  current_target_so_ops = &osf_so_ops;
}
