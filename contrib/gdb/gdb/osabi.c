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

#include "defs.h"

#include "gdb_assert.h"
#include "gdb_string.h"

#include "osabi.h"
#include "arch-utils.h"
#include "gdbcmd.h"
#include "command.h"

#include "elf-bfd.h"

#ifndef GDB_OSABI_DEFAULT
#define GDB_OSABI_DEFAULT GDB_OSABI_UNKNOWN
#endif

/* State for the "set osabi" command.  */
static enum { osabi_auto, osabi_default, osabi_user } user_osabi_state;
static enum gdb_osabi user_selected_osabi;
static const char *gdb_osabi_available_names[GDB_OSABI_INVALID + 3] = {
  "auto",
  "default",
  "none",
  NULL
};
static const char *set_osabi_string;

/* This table matches the indices assigned to enum gdb_osabi.  Keep
   them in sync.  */
static const char * const gdb_osabi_names[] =
{
  "none",

  "SVR4",
  "GNU/Hurd",
  "Solaris",
  "OSF/1",
  "GNU/Linux",
  "FreeBSD a.out",
  "FreeBSD ELF",
  "NetBSD a.out",
  "NetBSD ELF",
  "OpenBSD ELF",
  "Windows CE",
  "DJGPP",
  "NetWare",
  "Irix",
  "LynxOS",
  "Interix",
  "HP/UX ELF",
  "HP/UX SOM",

  "ARM EABI v1",
  "ARM EABI v2",
  "ARM APCS",
  "QNX Neutrino",

  "Cygwin",

  "<invalid>"
};

const char *
gdbarch_osabi_name (enum gdb_osabi osabi)
{
  if (osabi >= GDB_OSABI_UNKNOWN && osabi < GDB_OSABI_INVALID)
    return gdb_osabi_names[osabi];

  return gdb_osabi_names[GDB_OSABI_INVALID];
}

/* Handler for a given architecture/OS ABI pair.  There should be only
   one handler for a given OS ABI each architecture family.  */
struct gdb_osabi_handler  
{
  struct gdb_osabi_handler *next;
  const struct bfd_arch_info *arch_info;
  enum gdb_osabi osabi;
  void (*init_osabi)(struct gdbarch_info, struct gdbarch *);
};

static struct gdb_osabi_handler *gdb_osabi_handler_list;

void
gdbarch_register_osabi (enum bfd_architecture arch, unsigned long machine,
			enum gdb_osabi osabi,
                        void (*init_osabi)(struct gdbarch_info,
					   struct gdbarch *))
{
  struct gdb_osabi_handler **handler_p;
  const struct bfd_arch_info *arch_info = bfd_lookup_arch (arch, machine);
  const char **name_ptr;

  /* Registering an OS ABI handler for "unknown" is not allowed.  */
  if (osabi == GDB_OSABI_UNKNOWN)
    {
      internal_error
	(__FILE__, __LINE__,
	 "gdbarch_register_osabi: An attempt to register a handler for "
         "OS ABI \"%s\" for architecture %s was made.  The handler will "
	 "not be registered",
	 gdbarch_osabi_name (osabi),
	 bfd_printable_arch_mach (arch, machine));
      return;
    }

  gdb_assert (arch_info);

  for (handler_p = &gdb_osabi_handler_list; *handler_p != NULL;
       handler_p = &(*handler_p)->next)
    {
      if ((*handler_p)->arch_info == arch_info
	  && (*handler_p)->osabi == osabi)
	{
	  internal_error
	    (__FILE__, __LINE__,
	     "gdbarch_register_osabi: A handler for OS ABI \"%s\" "
	     "has already been registered for architecture %s",
	     gdbarch_osabi_name (osabi),
	     arch_info->printable_name);
	  /* If user wants to continue, override previous definition.  */
	  (*handler_p)->init_osabi = init_osabi;
	  return;
	}
    }

  (*handler_p)
    = (struct gdb_osabi_handler *) xmalloc (sizeof (struct gdb_osabi_handler));
  (*handler_p)->next = NULL;
  (*handler_p)->arch_info = arch_info;
  (*handler_p)->osabi = osabi;
  (*handler_p)->init_osabi = init_osabi;

  /* Add this OS ABI to the list of enum values for "set osabi", if it isn't
     already there.  */
  for (name_ptr = gdb_osabi_available_names; *name_ptr; name_ptr ++)
    {
      if (*name_ptr == gdbarch_osabi_name (osabi))
	return;
    }
  *name_ptr++ = gdbarch_osabi_name (osabi);
  *name_ptr = NULL;
}


/* Sniffer to find the OS ABI for a given file's architecture and flavour. 
   It is legal to have multiple sniffers for each arch/flavour pair, to
   disambiguate one OS's a.out from another, for example.  The first sniffer
   to return something other than GDB_OSABI_UNKNOWN wins, so a sniffer should
   be careful to claim a file only if it knows for sure what it is.  */
struct gdb_osabi_sniffer
{
  struct gdb_osabi_sniffer *next;
  enum bfd_architecture arch;   /* bfd_arch_unknown == wildcard */
  enum bfd_flavour flavour;
  enum gdb_osabi (*sniffer)(bfd *);
};

static struct gdb_osabi_sniffer *gdb_osabi_sniffer_list;

void
gdbarch_register_osabi_sniffer (enum bfd_architecture arch,
                                enum bfd_flavour flavour,
				enum gdb_osabi (*sniffer_fn)(bfd *))
{
  struct gdb_osabi_sniffer *sniffer;

  sniffer =
    (struct gdb_osabi_sniffer *) xmalloc (sizeof (struct gdb_osabi_sniffer));
  sniffer->arch = arch;
  sniffer->flavour = flavour;
  sniffer->sniffer = sniffer_fn;

  sniffer->next = gdb_osabi_sniffer_list;
  gdb_osabi_sniffer_list = sniffer;
}


enum gdb_osabi
gdbarch_lookup_osabi (bfd *abfd)
{
  struct gdb_osabi_sniffer *sniffer;
  enum gdb_osabi osabi, match;
  int match_specific;

  /* If we aren't in "auto" mode, return the specified OS ABI.  */
  if (user_osabi_state == osabi_user)
    return user_selected_osabi;

  /* If we don't have a binary, return the default OS ABI (if set) or
     an inconclusive result (otherwise).  */
  if (abfd == NULL) 
    {
      if (GDB_OSABI_DEFAULT != GDB_OSABI_UNKNOWN)
	return GDB_OSABI_DEFAULT;
      else
	return GDB_OSABI_UNINITIALIZED;
    }

  match = GDB_OSABI_UNKNOWN;
  match_specific = 0;

  for (sniffer = gdb_osabi_sniffer_list; sniffer != NULL;
       sniffer = sniffer->next)
    {
      if ((sniffer->arch == bfd_arch_unknown /* wildcard */
	   || sniffer->arch == bfd_get_arch (abfd))
	  && sniffer->flavour == bfd_get_flavour (abfd))
	{
	  osabi = (*sniffer->sniffer) (abfd);
	  if (osabi < GDB_OSABI_UNKNOWN || osabi >= GDB_OSABI_INVALID)
	    {
	      internal_error
		(__FILE__, __LINE__,
		 "gdbarch_lookup_osabi: invalid OS ABI (%d) from sniffer "
		 "for architecture %s flavour %d",
		 (int) osabi,
		 bfd_printable_arch_mach (bfd_get_arch (abfd), 0),
		 (int) bfd_get_flavour (abfd));
	    }
	  else if (osabi != GDB_OSABI_UNKNOWN)
	    {
	      /* A specific sniffer always overrides a generic sniffer.
		 Croak on multiple match if the two matches are of the
		 same class.  If the user wishes to continue, we'll use
		 the first match.  */
	      if (match != GDB_OSABI_UNKNOWN)
		{
		  if ((match_specific && sniffer->arch != bfd_arch_unknown)
		   || (!match_specific && sniffer->arch == bfd_arch_unknown))
		    {
		      internal_error
		        (__FILE__, __LINE__,
		         "gdbarch_lookup_osabi: multiple %sspecific OS ABI "
			 "match for architecture %s flavour %d: first "
			 "match \"%s\", second match \"%s\"",
			 match_specific ? "" : "non-",
		         bfd_printable_arch_mach (bfd_get_arch (abfd), 0),
		         (int) bfd_get_flavour (abfd),
		         gdbarch_osabi_name (match),
		         gdbarch_osabi_name (osabi));
		    }
		  else if (sniffer->arch != bfd_arch_unknown)
		    {
		      match = osabi;
		      match_specific = 1;
		    }
		}
	      else
		{
		  match = osabi;
		  if (sniffer->arch != bfd_arch_unknown)
		    match_specific = 1;
		}
	    }
	}
    }

  /* If we didn't find a match, but a default was specified at configure
     time, return the default.  */
  if (GDB_OSABI_DEFAULT != GDB_OSABI_UNKNOWN && match == GDB_OSABI_UNKNOWN)
    return GDB_OSABI_DEFAULT;
  else
    return match;
}


/* Return non-zero if architecture A can run code written for
   architecture B.  */
static int
can_run_code_for (const struct bfd_arch_info *a, const struct bfd_arch_info *b)
{
  /* BFD's 'A->compatible (A, B)' functions return zero if A and B are
     incompatible.  But if they are compatible, it returns the 'more
     featureful' of the two arches.  That is, if A can run code
     written for B, but B can't run code written for A, then it'll
     return A.

     struct bfd_arch_info objects are singletons: that is, there's
     supposed to be exactly one instance for a given machine.  So you
     can tell whether two are equivalent by comparing pointers.  */
  return (a == b || a->compatible (a, b) == a);
}


void
gdbarch_init_osabi (struct gdbarch_info info, struct gdbarch *gdbarch)
{
  struct gdb_osabi_handler *handler;

  if (info.osabi == GDB_OSABI_UNKNOWN)
    {
      /* Don't complain about an unknown OSABI.  Assume the user knows
         what they are doing.  */
      return;
    }

  for (handler = gdb_osabi_handler_list; handler != NULL;
       handler = handler->next)
    {
      if (handler->osabi != info.osabi)
	continue;

      /* If the architecture described by ARCH_INFO can run code for
         the architcture we registered the handler for, then the
         handler is applicable.  Note, though, that if the handler is
         for an architecture that is a superset of ARCH_INFO, we can't
         use that --- it would be perfectly correct for it to install
         gdbarch methods that refer to registers / instructions /
         other facilities ARCH_INFO doesn't have.

         NOTE: kettenis/20021027: There may be more than one machine
	 type that is compatible with the desired machine type.  Right
	 now we simply return the first match, which is fine for now.
	 However, we might want to do something smarter in the future.  */
      /* NOTE: cagney/2003-10-23: The code for "a can_run_code_for b"
         is implemented using BFD's compatible method (a->compatible
         (b) == a -- the lowest common denominator between a and b is
         a).  That method's definition of compatible may not be as you
         expect.  For instance the test "amd64 can run code for i386"
         (or more generally "64-bit ISA can run code for the 32-bit
         ISA").  BFD doesn't normally consider 32-bit and 64-bit
         "compatible" so it doesn't succeed.  */
      if (can_run_code_for (info.bfd_arch_info, handler->arch_info))
	{
	  (*handler->init_osabi) (info, gdbarch);
	  return;
	}
    }

  warning
    ("A handler for the OS ABI \"%s\" is not built into this configuration\n"
     "of GDB.  Attempting to continue with the default %s settings.\n",
     gdbarch_osabi_name (info.osabi),
     info.bfd_arch_info->printable_name);
}

/* Limit on the amount of data to be read.  */
#define MAX_NOTESZ	128

/* Return non-zero if NOTE matches NAME, DESCSZ and TYPE.  */

static int
check_note (bfd *abfd, asection *sect, const char *note,
	    const char *name, unsigned long descsz, unsigned long type)
{
  unsigned long notesz;
  unsigned long rdescsz;

  /* Calculate the size of this note.  */
  notesz = 4 * 3;	/* namelen, optlen, type */
  notesz += strlen (name) + 1;
  notesz = ((notesz + 3) & ~3);

  /* If this assertion triggers, increase MAX_NOTESZ.  */
  gdb_assert (notesz <= MAX_NOTESZ);

  /* Check whether SECT is big enough to comtain the complete note.  */
  if (notesz > bfd_section_size (abfd, sect))
    return 0;

  /* Check the note name.  */
  if (bfd_h_get_32 (abfd, note) != (strlen (name) + 1)
      || strcmp (note + 12, name) != 0)
    return 0;

  /* Check the descriptor size.  */
  rdescsz = bfd_h_get_32 (abfd, note + 4);
  if (descsz != (unsigned long)-1 && rdescsz != descsz)
    return 0;

  notesz += rdescsz;
  notesz = ((notesz + 3) & ~3);

  /* Check whether SECT is big enough to comtain the complete note.  */
  if (notesz > bfd_section_size (abfd, sect))
    return 0;

  /* Check the note type.  */
  if (bfd_h_get_32 (abfd, note + 8) != type)
    return 0;

  return 1;
}

/* Generic sniffer for ELF flavoured files.  */

void
generic_elf_osabi_sniff_abi_tag_sections (bfd *abfd, asection *sect, void *obj)
{
  enum gdb_osabi *osabi = obj;
  const char *name;
  unsigned int sectsize;
  char *note;

  name = bfd_get_section_name (abfd, sect);
  sectsize = bfd_section_size (abfd, sect);

  /* Limit the amount of data to read.  */
  if (sectsize > MAX_NOTESZ)
    sectsize = MAX_NOTESZ;

  note = alloca (sectsize);
  bfd_get_section_contents (abfd, sect, note, 0, sectsize);

  /* .note.ABI-tag notes, used by GNU/Linux and FreeBSD.  */
  if (strcmp (name, ".note.ABI-tag") == 0)
    {
      /* GNU.  */
      if (check_note (abfd, sect, note, "GNU", 16, NT_GNU_ABI_TAG))
	{
	  unsigned int abi_tag = bfd_h_get_32 (abfd, note + 16);

	  switch (abi_tag)
	    {
	    case GNU_ABI_TAG_LINUX:
	      *osabi = GDB_OSABI_LINUX;
	      break;

	    case GNU_ABI_TAG_HURD:
	      *osabi = GDB_OSABI_HURD;
	      break;

	    case GNU_ABI_TAG_SOLARIS:
	      *osabi = GDB_OSABI_SOLARIS;
	      break;

	    case GNU_ABI_TAG_FREEBSD:
	      *osabi = GDB_OSABI_FREEBSD_ELF;
	      break;

	    case GNU_ABI_TAG_NETBSD:
	      *osabi = GDB_OSABI_NETBSD_ELF;
	      break;

	    default:
	      internal_error (__FILE__, __LINE__, "\
generic_elf_osabi_sniff_abi_tag_sections: unknown OS number %d",
			      abi_tag);
	    }
	  return;
	}

      /* FreeBSD.  */
      if (check_note (abfd, sect, note, "FreeBSD", 4, NT_FREEBSD_ABI_TAG))
	{
	  /* There is no need to check the version yet.  */
	  *osabi = GDB_OSABI_FREEBSD_ELF;
	  return;
	}

      return;
    }

  /* .note.tag notes, used by FreeBSD.  */
  if (strcmp (name, ".note.tag") == 0)
   {
      /* FreeBSD.  */
      if (check_note (abfd, sect, note, "FreeBSD", 4, NT_FREEBSD_TAG))
	{
	  /* There is no need to check the version yet.  */
	  *osabi = GDB_OSABI_FREEBSD_ELF;
	  return;
	}
      if (check_note (abfd, sect, note, "FreeBSD", 4, NT_FREEBSD_NOINIT_TAG))
	{
	  /* There is no need to check the version yet.  */
	  *osabi = GDB_OSABI_FREEBSD_ELF;
	  return;
	}
      if (check_note (abfd, sect, note, "FreeBSD", -1, NT_FREEBSD_ARCH_TAG))
	{
	  /* There is no need to check the version yet.  */
	  *osabi = GDB_OSABI_FREEBSD_ELF;
	  return;
	}

      return;
    }
      
  /* .note.netbsd.ident notes, used by NetBSD.  */
  if (strcmp (name, ".note.netbsd.ident") == 0
      && check_note (abfd, sect, note, "NetBSD", 4, NT_NETBSD_IDENT))
    {
      /* There is no need to check the version yet.  */
      *osabi = GDB_OSABI_NETBSD_ELF;
      return;
    }

  /* .note.openbsd.ident notes, used by OpenBSD.  */
  if (strcmp (name, ".note.openbsd.ident") == 0
      && check_note (abfd, sect, note, "OpenBSD", 4, NT_OPENBSD_IDENT))
    {
      /* There is no need to check the version yet.  */
      *osabi = GDB_OSABI_OPENBSD_ELF;
      return;
    }

  /* .note.netbsdcore.procinfo notes, used by NetBSD.  */
  if (strcmp (name, ".note.netbsdcore.procinfo") == 0)
    {
      *osabi = GDB_OSABI_NETBSD_ELF;
      return;
    }
}

static enum gdb_osabi
generic_elf_osabi_sniffer (bfd *abfd)
{
  unsigned int elfosabi;
  enum gdb_osabi osabi = GDB_OSABI_UNKNOWN;

  elfosabi = elf_elfheader (abfd)->e_ident[EI_OSABI];

  switch (elfosabi)
    {
    case ELFOSABI_NONE:
      /* When elfosabi is ELFOSABI_NONE (0), then the ELF structures in the
         file are conforming to the base specification for that machine
	 (there are no OS-specific extensions).  In order to determine the
	 real OS in use we must look for OS notes that have been added.  */
      bfd_map_over_sections (abfd,
			     generic_elf_osabi_sniff_abi_tag_sections,
			     &osabi);
      break;

    case ELFOSABI_FREEBSD:
      osabi = GDB_OSABI_FREEBSD_ELF;
      break;

    case ELFOSABI_NETBSD:
      osabi = GDB_OSABI_NETBSD_ELF;
      break;

    case ELFOSABI_LINUX:
      osabi = GDB_OSABI_LINUX;
      break;

    case ELFOSABI_HURD:
      osabi = GDB_OSABI_HURD;
      break;

    case ELFOSABI_SOLARIS:
      osabi = GDB_OSABI_SOLARIS;
      break;

    case ELFOSABI_HPUX:
      osabi = GDB_OSABI_HPUX_ELF;
      break;
    }

  if (osabi == GDB_OSABI_UNKNOWN)
    {
      /* The FreeBSD folks have been naughty; they stored the string
         "FreeBSD" in the padding of the e_ident field of the ELF
         header to "brand" their ELF binaries in FreeBSD 3.x.  */
      if (strcmp (&elf_elfheader (abfd)->e_ident[8], "FreeBSD") == 0)
	osabi = GDB_OSABI_FREEBSD_ELF;
    }

  return osabi;
}

static void
set_osabi (char *args, int from_tty, struct cmd_list_element *c)
{
  struct gdbarch_info info;

  if (strcmp (set_osabi_string, "auto") == 0)
    user_osabi_state = osabi_auto;
  else if (strcmp (set_osabi_string, "default") == 0)
    {
      user_selected_osabi = GDB_OSABI_DEFAULT;
      user_osabi_state = osabi_user;
    }
  else if (strcmp (set_osabi_string, "none") == 0)
    {
      user_selected_osabi = GDB_OSABI_UNKNOWN;
      user_osabi_state = osabi_user;
    }
  else
    {
      int i;
      for (i = 1; i < GDB_OSABI_INVALID; i++)
	if (strcmp (set_osabi_string, gdbarch_osabi_name (i)) == 0)
	  {
	    user_selected_osabi = i;
	    user_osabi_state = osabi_user;
	    break;
	  }
      if (i == GDB_OSABI_INVALID)
	internal_error (__FILE__, __LINE__,
			"Invalid OS ABI \"%s\" passed to command handler.",
			set_osabi_string);
    }

  /* NOTE: At some point (true multiple architectures) we'll need to be more
     graceful here.  */
  gdbarch_info_init (&info);
  if (! gdbarch_update_p (info))
    internal_error (__FILE__, __LINE__, "Updating OS ABI failed.");
}

static void
show_osabi (char *args, int from_tty)
{
  if (user_osabi_state == osabi_auto)
    printf_filtered ("The current OS ABI is \"auto\" (currently \"%s\").\n",
		     gdbarch_osabi_name (gdbarch_osabi (current_gdbarch)));
  else
    printf_filtered ("The current OS ABI is \"%s\".\n",
		     gdbarch_osabi_name (user_selected_osabi));

  if (GDB_OSABI_DEFAULT != GDB_OSABI_UNKNOWN)
    printf_filtered ("The default OS ABI is \"%s\".\n",
		     gdbarch_osabi_name (GDB_OSABI_DEFAULT));
}

extern initialize_file_ftype _initialize_gdb_osabi; /* -Wmissing-prototype */

void
_initialize_gdb_osabi (void)
{
  struct cmd_list_element *c;

  if (strcmp (gdb_osabi_names[GDB_OSABI_INVALID], "<invalid>") != 0)
    internal_error
      (__FILE__, __LINE__,
       "_initialize_gdb_osabi: gdb_osabi_names[] is inconsistent");

  /* Register a generic sniffer for ELF flavoured files.  */
  gdbarch_register_osabi_sniffer (bfd_arch_unknown,
				  bfd_target_elf_flavour,
				  generic_elf_osabi_sniffer);

  /* Register the "set osabi" command.  */
  c = add_set_enum_cmd ("osabi", class_support, gdb_osabi_available_names,
			&set_osabi_string, "Set OS ABI of target.", &setlist);

  set_cmd_sfunc (c, set_osabi);
  add_cmd ("osabi", class_support, show_osabi, "Show OS/ABI of target.",
	   &showlist);
  user_osabi_state = osabi_auto;
}
