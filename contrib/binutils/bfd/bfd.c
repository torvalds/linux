/* Generic BFD library interface and support routines.
   Copyright 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999,
   2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007
   Free Software Foundation, Inc.
   Written by Cygnus Support.

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

/*
SECTION
	<<typedef bfd>>

	A BFD has type <<bfd>>; objects of this type are the
	cornerstone of any application using BFD. Using BFD
	consists of making references though the BFD and to data in the BFD.

	Here is the structure that defines the type <<bfd>>.  It
	contains the major data about the file and pointers
	to the rest of the data.

CODE_FRAGMENT
.
.struct bfd
.{
.  {* A unique identifier of the BFD  *}
.  unsigned int id;
.
.  {* The filename the application opened the BFD with.  *}
.  const char *filename;
.
.  {* A pointer to the target jump table.  *}
.  const struct bfd_target *xvec;
.
.  {* The IOSTREAM, and corresponding IO vector that provide access
.     to the file backing the BFD.  *}
.  void *iostream;
.  const struct bfd_iovec *iovec;
.
.  {* Is the file descriptor being cached?  That is, can it be closed as
.     needed, and re-opened when accessed later?  *}
.  bfd_boolean cacheable;
.
.  {* Marks whether there was a default target specified when the
.     BFD was opened. This is used to select which matching algorithm
.     to use to choose the back end.  *}
.  bfd_boolean target_defaulted;
.
.  {* The caching routines use these to maintain a
.     least-recently-used list of BFDs.  *}
.  struct bfd *lru_prev, *lru_next;
.
.  {* When a file is closed by the caching routines, BFD retains
.     state information on the file here...  *}
.  ufile_ptr where;
.
.  {* ... and here: (``once'' means at least once).  *}
.  bfd_boolean opened_once;
.
.  {* Set if we have a locally maintained mtime value, rather than
.     getting it from the file each time.  *}
.  bfd_boolean mtime_set;
.
.  {* File modified time, if mtime_set is TRUE.  *}
.  long mtime;
.
.  {* Reserved for an unimplemented file locking extension.  *}
.  int ifd;
.
.  {* The format which belongs to the BFD. (object, core, etc.)  *}
.  bfd_format format;
.
.  {* The direction with which the BFD was opened.  *}
.  enum bfd_direction
.    {
.      no_direction = 0,
.      read_direction = 1,
.      write_direction = 2,
.      both_direction = 3
.    }
.  direction;
.
.  {* Format_specific flags.  *}
.  flagword flags;
.
.  {* Currently my_archive is tested before adding origin to
.     anything. I believe that this can become always an add of
.     origin, with origin set to 0 for non archive files.  *}
.  ufile_ptr origin;
.
.  {* Remember when output has begun, to stop strange things
.     from happening.  *}
.  bfd_boolean output_has_begun;
.
.  {* A hash table for section names.  *}
.  struct bfd_hash_table section_htab;
.
.  {* Pointer to linked list of sections.  *}
.  struct bfd_section *sections;
.
.  {* The last section on the section list.  *}
.  struct bfd_section *section_last;
.
.  {* The number of sections.  *}
.  unsigned int section_count;
.
.  {* Stuff only useful for object files:
.     The start address.  *}
.  bfd_vma start_address;
.
.  {* Used for input and output.  *}
.  unsigned int symcount;
.
.  {* Symbol table for output BFD (with symcount entries).  *}
.  struct bfd_symbol  **outsymbols;
.
.  {* Used for slurped dynamic symbol tables.  *}
.  unsigned int dynsymcount;
.
.  {* Pointer to structure which contains architecture information.  *}
.  const struct bfd_arch_info *arch_info;
.
.  {* Flag set if symbols from this BFD should not be exported.  *}
.  bfd_boolean no_export;
.
.  {* Stuff only useful for archives.  *}
.  void *arelt_data;
.  struct bfd *my_archive;      {* The containing archive BFD.  *}
.  struct bfd *archive_next;    {* The next BFD in the archive.  *}
.  struct bfd *archive_head;    {* The first BFD in the archive.  *}
.  bfd_boolean has_armap;
.
.  {* A chain of BFD structures involved in a link.  *}
.  struct bfd *link_next;
.
.  {* A field used by _bfd_generic_link_add_archive_symbols.  This will
.     be used only for archive elements.  *}
.  int archive_pass;
.
.  {* Used by the back end to hold private data.  *}
.  union
.    {
.      struct aout_data_struct *aout_data;
.      struct artdata *aout_ar_data;
.      struct _oasys_data *oasys_obj_data;
.      struct _oasys_ar_data *oasys_ar_data;
.      struct coff_tdata *coff_obj_data;
.      struct pe_tdata *pe_obj_data;
.      struct xcoff_tdata *xcoff_obj_data;
.      struct ecoff_tdata *ecoff_obj_data;
.      struct ieee_data_struct *ieee_data;
.      struct ieee_ar_data_struct *ieee_ar_data;
.      struct srec_data_struct *srec_data;
.      struct ihex_data_struct *ihex_data;
.      struct tekhex_data_struct *tekhex_data;
.      struct elf_obj_tdata *elf_obj_data;
.      struct nlm_obj_tdata *nlm_obj_data;
.      struct bout_data_struct *bout_data;
.      struct mmo_data_struct *mmo_data;
.      struct sun_core_struct *sun_core_data;
.      struct sco5_core_struct *sco5_core_data;
.      struct trad_core_struct *trad_core_data;
.      struct som_data_struct *som_data;
.      struct hpux_core_struct *hpux_core_data;
.      struct hppabsd_core_struct *hppabsd_core_data;
.      struct sgi_core_struct *sgi_core_data;
.      struct lynx_core_struct *lynx_core_data;
.      struct osf_core_struct *osf_core_data;
.      struct cisco_core_struct *cisco_core_data;
.      struct versados_data_struct *versados_data;
.      struct netbsd_core_struct *netbsd_core_data;
.      struct mach_o_data_struct *mach_o_data;
.      struct mach_o_fat_data_struct *mach_o_fat_data;
.      struct bfd_pef_data_struct *pef_data;
.      struct bfd_pef_xlib_data_struct *pef_xlib_data;
.      struct bfd_sym_data_struct *sym_data;
.      void *any;
.    }
.  tdata;
.
.  {* Used by the application to hold private data.  *}
.  void *usrdata;
.
.  {* Where all the allocated stuff under this BFD goes.  This is a
.     struct objalloc *, but we use void * to avoid requiring the inclusion
.     of objalloc.h.  *}
.  void *memory;
.};
.
*/

#include "sysdep.h"
#include <stdarg.h>
#include "bfd.h"
#include "bfdver.h"
#include "libiberty.h"
#include "demangle.h"
#include "safe-ctype.h"
#include "bfdlink.h"
#include "libbfd.h"
#include "coff/internal.h"
#include "coff/sym.h"
#include "libcoff.h"
#include "libecoff.h"
#undef obj_symbols
#include "elf-bfd.h"

#ifndef EXIT_FAILURE
#define EXIT_FAILURE 1
#endif


/* provide storage for subsystem, stack and heap data which may have been
   passed in on the command line.  Ld puts this data into a bfd_link_info
   struct which ultimately gets passed in to the bfd.  When it arrives, copy
   it to the following struct so that the data will be available in coffcode.h
   where it is needed.  The typedef's used are defined in bfd.h */

/*
SECTION
	Error reporting

	Most BFD functions return nonzero on success (check their
	individual documentation for precise semantics).  On an error,
	they call <<bfd_set_error>> to set an error condition that callers
	can check by calling <<bfd_get_error>>.
        If that returns <<bfd_error_system_call>>, then check
	<<errno>>.

	The easiest way to report a BFD error to the user is to
	use <<bfd_perror>>.

SUBSECTION
	Type <<bfd_error_type>>

	The values returned by <<bfd_get_error>> are defined by the
	enumerated type <<bfd_error_type>>.

CODE_FRAGMENT
.
.typedef enum bfd_error
.{
.  bfd_error_no_error = 0,
.  bfd_error_system_call,
.  bfd_error_invalid_target,
.  bfd_error_wrong_format,
.  bfd_error_wrong_object_format,
.  bfd_error_invalid_operation,
.  bfd_error_no_memory,
.  bfd_error_no_symbols,
.  bfd_error_no_armap,
.  bfd_error_no_more_archived_files,
.  bfd_error_malformed_archive,
.  bfd_error_file_not_recognized,
.  bfd_error_file_ambiguously_recognized,
.  bfd_error_no_contents,
.  bfd_error_nonrepresentable_section,
.  bfd_error_no_debug_section,
.  bfd_error_bad_value,
.  bfd_error_file_truncated,
.  bfd_error_file_too_big,
.  bfd_error_on_input,
.  bfd_error_invalid_error_code
.}
.bfd_error_type;
.
*/

static bfd_error_type bfd_error = bfd_error_no_error;
static bfd *input_bfd = NULL;
static bfd_error_type input_error = bfd_error_no_error;

const char *const bfd_errmsgs[] =
{
  N_("No error"),
  N_("System call error"),
  N_("Invalid bfd target"),
  N_("File in wrong format"),
  N_("Archive object file in wrong format"),
  N_("Invalid operation"),
  N_("Memory exhausted"),
  N_("No symbols"),
  N_("Archive has no index; run ranlib to add one"),
  N_("No more archived files"),
  N_("Malformed archive"),
  N_("File format not recognized"),
  N_("File format is ambiguous"),
  N_("Section has no contents"),
  N_("Nonrepresentable section on output"),
  N_("Symbol needs debug section which does not exist"),
  N_("Bad value"),
  N_("File truncated"),
  N_("File too big"),
  N_("Error reading %s: %s"),
  N_("#<Invalid error code>")
};

/*
FUNCTION
	bfd_get_error

SYNOPSIS
	bfd_error_type bfd_get_error (void);

DESCRIPTION
	Return the current BFD error condition.
*/

bfd_error_type
bfd_get_error (void)
{
  return bfd_error;
}

/*
FUNCTION
	bfd_set_error

SYNOPSIS
	void bfd_set_error (bfd_error_type error_tag, ...);

DESCRIPTION
	Set the BFD error condition to be @var{error_tag}.
	If @var{error_tag} is bfd_error_on_input, then this function
	takes two more parameters, the input bfd where the error
	occurred, and the bfd_error_type error.
*/

void
bfd_set_error (bfd_error_type error_tag, ...)
{
  bfd_error = error_tag;
  if (error_tag == bfd_error_on_input)
    {
      /* This is an error that occurred during bfd_close when
	 writing an archive, but on one of the input files.  */
      va_list ap;

      va_start (ap, error_tag);
      input_bfd = va_arg (ap, bfd *);
      input_error = va_arg (ap, int);
      if (input_error >= bfd_error_on_input)
	abort ();
      va_end (ap);
    }
}

/*
FUNCTION
	bfd_errmsg

SYNOPSIS
	const char *bfd_errmsg (bfd_error_type error_tag);

DESCRIPTION
	Return a string describing the error @var{error_tag}, or
	the system error if @var{error_tag} is <<bfd_error_system_call>>.
*/

const char *
bfd_errmsg (bfd_error_type error_tag)
{
#ifndef errno
  extern int errno;
#endif
  if (error_tag == bfd_error_on_input)
    {
      char *buf;
      const char *msg = bfd_errmsg (input_error);

      if (asprintf (&buf, _(bfd_errmsgs [error_tag]), input_bfd->filename, msg)
	  != -1)
	return buf;

      /* Ick, what to do on out of memory?  */
      return msg;
    }

  if (error_tag == bfd_error_system_call)
    return xstrerror (errno);

  if (error_tag > bfd_error_invalid_error_code)
    error_tag = bfd_error_invalid_error_code;	/* sanity check */

  return _(bfd_errmsgs [error_tag]);
}

/*
FUNCTION
	bfd_perror

SYNOPSIS
	void bfd_perror (const char *message);

DESCRIPTION
	Print to the standard error stream a string describing the
	last BFD error that occurred, or the last system error if
	the last BFD error was a system call failure.  If @var{message}
	is non-NULL and non-empty, the error string printed is preceded
	by @var{message}, a colon, and a space.  It is followed by a newline.
*/

void
bfd_perror (const char *message)
{
  if (message == NULL || *message == '\0')
    fprintf (stderr, "%s\n", bfd_errmsg (bfd_get_error ()));
  else
    fprintf (stderr, "%s: %s\n", message, bfd_errmsg (bfd_get_error ()));
}

/*
SUBSECTION
	BFD error handler

	Some BFD functions want to print messages describing the
	problem.  They call a BFD error handler function.  This
	function may be overridden by the program.

	The BFD error handler acts like printf.

CODE_FRAGMENT
.
.typedef void (*bfd_error_handler_type) (const char *, ...);
.
*/

/* The program name used when printing BFD error messages.  */

static const char *_bfd_error_program_name;

/* This is the default routine to handle BFD error messages.
   Like fprintf (stderr, ...), but also handles some extra format specifiers.

   %A section name from section.  For group components, print group name too.
   %B file name from bfd.  For archive components, prints archive too.

   Note - because these two extra format specifiers require special handling
   they are scanned for and processed in this function, before calling
   vfprintf.  This means that the *arguments* for these format specifiers
   must be the first ones in the variable argument list, regardless of where
   the specifiers appear in the format string.  Thus for example calling
   this function with a format string of:

      "blah %s blah %A blah %d blah %B"

   would involve passing the arguments as:

      "blah %s blah %A blah %d blah %B",
        asection_for_the_%A,
	bfd_for_the_%B,
	string_for_the_%s,
	integer_for_the_%d);
 */

void
_bfd_default_error_handler (const char *fmt, ...)
{
  va_list ap;
  char *bufp;
  const char *new_fmt, *p;
  size_t avail = 1000;
  char buf[1000];

  if (_bfd_error_program_name != NULL)
    fprintf (stderr, "%s: ", _bfd_error_program_name);
  else
    fprintf (stderr, "BFD: ");

  va_start (ap, fmt);
  new_fmt = fmt;
  bufp = buf;

  /* Reserve enough space for the existing format string.  */
  avail -= strlen (fmt) + 1;
  if (avail > 1000)
    _exit (EXIT_FAILURE);

  p = fmt;
  while (1)
    {
      char *q;
      size_t len, extra, trim;

      p = strchr (p, '%');
      if (p == NULL || p[1] == '\0')
	{
	  if (new_fmt == buf)
	    {
	      len = strlen (fmt);
	      memcpy (bufp, fmt, len + 1);
	    }
	  break;
	}

      if (p[1] == 'A' || p[1] == 'B')
	{
	  len = p - fmt;
	  memcpy (bufp, fmt, len);
	  bufp += len;
	  fmt = p + 2;
	  new_fmt = buf;

	  /* If we run out of space, tough, you lose your ridiculously
	     long file or section name.  It's not safe to try to alloc
	     memory here;  We might be printing an out of memory message.  */
	  if (avail == 0)
	    {
	      *bufp++ = '*';
	      *bufp++ = '*';
	      *bufp = '\0';
	    }
	  else
	    {
	      if (p[1] == 'B')
		{
		  bfd *abfd = va_arg (ap, bfd *);

		  if (abfd == NULL)
		    /* Invoking %B with a null bfd pointer is an internal error.  */
		    abort ();
		  else if (abfd->my_archive)
		    snprintf (bufp, avail, "%s(%s)",
			      abfd->my_archive->filename, abfd->filename);
		  else
		    snprintf (bufp, avail, "%s", abfd->filename);
		}
	      else
		{
		  asection *sec = va_arg (ap, asection *);
		  bfd *abfd;
		  const char *group = NULL;
		  struct coff_comdat_info *ci;

		  if (sec == NULL)
		    /* Invoking %A with a null section pointer is an internal error.  */
		    abort ();
		  abfd = sec->owner;
		  if (abfd != NULL
		      && bfd_get_flavour (abfd) == bfd_target_elf_flavour
		      && elf_next_in_group (sec) != NULL
		      && (sec->flags & SEC_GROUP) == 0)
		    group = elf_group_name (sec);
		  else if (abfd != NULL
			   && bfd_get_flavour (abfd) == bfd_target_coff_flavour
			   && (ci = bfd_coff_get_comdat_section (sec->owner,
								 sec)) != NULL)
		    group = ci->name;
		  if (group != NULL)
		    snprintf (bufp, avail, "%s[%s]", sec->name, group);
		  else
		    snprintf (bufp, avail, "%s", sec->name);
		}
	      len = strlen (bufp);
	      avail = avail - len + 2;

	      /* We need to replace any '%' we printed by "%%".
		 First count how many.  */
	      q = bufp;
	      bufp += len;
	      extra = 0;
	      while ((q = strchr (q, '%')) != NULL)
		{
		  ++q;
		  ++extra;
		}

	      /* If there isn't room, trim off the end of the string.  */
	      q = bufp;
	      bufp += extra;
	      if (extra > avail)
		{
		  trim = extra - avail;
		  bufp -= trim;
		  do
		    {
		      if (*--q == '%')
			--extra;
		    }
		  while (--trim != 0);
		  *q = '\0';
		  avail = extra;
		}
	      avail -= extra;

	      /* Now double all '%' chars, shuffling the string as we go.  */
	      while (extra != 0)
		{
		  while ((q[extra] = *q) != '%')
		    --q;
		  q[--extra] = '%';
		  --q;
		}
	    }
	}
      p = p + 2;
    }

  vfprintf (stderr, new_fmt, ap);
  va_end (ap);

  putc ('\n', stderr);
}

/* This is a function pointer to the routine which should handle BFD
   error messages.  It is called when a BFD routine encounters an
   error for which it wants to print a message.  Going through a
   function pointer permits a program linked against BFD to intercept
   the messages and deal with them itself.  */

bfd_error_handler_type _bfd_error_handler = _bfd_default_error_handler;

/*
FUNCTION
	bfd_set_error_handler

SYNOPSIS
	bfd_error_handler_type bfd_set_error_handler (bfd_error_handler_type);

DESCRIPTION
	Set the BFD error handler function.  Returns the previous
	function.
*/

bfd_error_handler_type
bfd_set_error_handler (bfd_error_handler_type pnew)
{
  bfd_error_handler_type pold;

  pold = _bfd_error_handler;
  _bfd_error_handler = pnew;
  return pold;
}

/*
FUNCTION
	bfd_set_error_program_name

SYNOPSIS
	void bfd_set_error_program_name (const char *);

DESCRIPTION
	Set the program name to use when printing a BFD error.  This
	is printed before the error message followed by a colon and
	space.  The string must not be changed after it is passed to
	this function.
*/

void
bfd_set_error_program_name (const char *name)
{
  _bfd_error_program_name = name;
}

/*
FUNCTION
	bfd_get_error_handler

SYNOPSIS
	bfd_error_handler_type bfd_get_error_handler (void);

DESCRIPTION
	Return the BFD error handler function.
*/

bfd_error_handler_type
bfd_get_error_handler (void)
{
  return _bfd_error_handler;
}

/*
SECTION
	Miscellaneous

SUBSECTION
	Miscellaneous functions
*/

/*
FUNCTION
	bfd_get_reloc_upper_bound

SYNOPSIS
	long bfd_get_reloc_upper_bound (bfd *abfd, asection *sect);

DESCRIPTION
	Return the number of bytes required to store the
	relocation information associated with section @var{sect}
	attached to bfd @var{abfd}.  If an error occurs, return -1.

*/

long
bfd_get_reloc_upper_bound (bfd *abfd, sec_ptr asect)
{
  if (abfd->format != bfd_object)
    {
      bfd_set_error (bfd_error_invalid_operation);
      return -1;
    }

  return BFD_SEND (abfd, _get_reloc_upper_bound, (abfd, asect));
}

/*
FUNCTION
	bfd_canonicalize_reloc

SYNOPSIS
	long bfd_canonicalize_reloc
	  (bfd *abfd, asection *sec, arelent **loc, asymbol **syms);

DESCRIPTION
	Call the back end associated with the open BFD
	@var{abfd} and translate the external form of the relocation
	information attached to @var{sec} into the internal canonical
	form.  Place the table into memory at @var{loc}, which has
	been preallocated, usually by a call to
	<<bfd_get_reloc_upper_bound>>.  Returns the number of relocs, or
	-1 on error.

	The @var{syms} table is also needed for horrible internal magic
	reasons.

*/
long
bfd_canonicalize_reloc (bfd *abfd,
			sec_ptr asect,
			arelent **location,
			asymbol **symbols)
{
  if (abfd->format != bfd_object)
    {
      bfd_set_error (bfd_error_invalid_operation);
      return -1;
    }

  return BFD_SEND (abfd, _bfd_canonicalize_reloc,
		   (abfd, asect, location, symbols));
}

/*
FUNCTION
	bfd_set_reloc

SYNOPSIS
	void bfd_set_reloc
	  (bfd *abfd, asection *sec, arelent **rel, unsigned int count);

DESCRIPTION
	Set the relocation pointer and count within
	section @var{sec} to the values @var{rel} and @var{count}.
	The argument @var{abfd} is ignored.

*/

void
bfd_set_reloc (bfd *ignore_abfd ATTRIBUTE_UNUSED,
	       sec_ptr asect,
	       arelent **location,
	       unsigned int count)
{
  asect->orelocation = location;
  asect->reloc_count = count;
}

/*
FUNCTION
	bfd_set_file_flags

SYNOPSIS
	bfd_boolean bfd_set_file_flags (bfd *abfd, flagword flags);

DESCRIPTION
	Set the flag word in the BFD @var{abfd} to the value @var{flags}.

	Possible errors are:
	o <<bfd_error_wrong_format>> - The target bfd was not of object format.
	o <<bfd_error_invalid_operation>> - The target bfd was open for reading.
	o <<bfd_error_invalid_operation>> -
	The flag word contained a bit which was not applicable to the
	type of file.  E.g., an attempt was made to set the <<D_PAGED>> bit
	on a BFD format which does not support demand paging.

*/

bfd_boolean
bfd_set_file_flags (bfd *abfd, flagword flags)
{
  if (abfd->format != bfd_object)
    {
      bfd_set_error (bfd_error_wrong_format);
      return FALSE;
    }

  if (bfd_read_p (abfd))
    {
      bfd_set_error (bfd_error_invalid_operation);
      return FALSE;
    }

  bfd_get_file_flags (abfd) = flags;
  if ((flags & bfd_applicable_file_flags (abfd)) != flags)
    {
      bfd_set_error (bfd_error_invalid_operation);
      return FALSE;
    }

  return TRUE;
}

void
bfd_assert (const char *file, int line)
{
  (*_bfd_error_handler) (_("BFD %s assertion fail %s:%d"),
			 BFD_VERSION_STRING, file, line);
}

/* A more or less friendly abort message.  In libbfd.h abort is
   defined to call this function.  */

void
_bfd_abort (const char *file, int line, const char *fn)
{
  if (fn != NULL)
    (*_bfd_error_handler)
      (_("BFD %s internal error, aborting at %s line %d in %s\n"),
       BFD_VERSION_STRING, file, line, fn);
  else
    (*_bfd_error_handler)
      (_("BFD %s internal error, aborting at %s line %d\n"),
       BFD_VERSION_STRING, file, line);
  (*_bfd_error_handler) (_("Please report this bug.\n"));
  _exit (EXIT_FAILURE);
}

/*
FUNCTION
	bfd_get_arch_size

SYNOPSIS
 	int bfd_get_arch_size (bfd *abfd);

DESCRIPTION
	Returns the architecture address size, in bits, as determined
	by the object file's format.  For ELF, this information is
	included in the header.

RETURNS
	Returns the arch size in bits if known, <<-1>> otherwise.
*/

int
bfd_get_arch_size (bfd *abfd)
{
  if (abfd->xvec->flavour == bfd_target_elf_flavour)
    return get_elf_backend_data (abfd)->s->arch_size;

  return -1;
}

/*
FUNCTION
	bfd_get_sign_extend_vma

SYNOPSIS
 	int bfd_get_sign_extend_vma (bfd *abfd);

DESCRIPTION
	Indicates if the target architecture "naturally" sign extends
	an address.  Some architectures implicitly sign extend address
	values when they are converted to types larger than the size
	of an address.  For instance, bfd_get_start_address() will
	return an address sign extended to fill a bfd_vma when this is
	the case.

RETURNS
	Returns <<1>> if the target architecture is known to sign
	extend addresses, <<0>> if the target architecture is known to
	not sign extend addresses, and <<-1>> otherwise.
*/

int
bfd_get_sign_extend_vma (bfd *abfd)
{
  char *name;

  if (bfd_get_flavour (abfd) == bfd_target_elf_flavour)
    return get_elf_backend_data (abfd)->sign_extend_vma;

  name = bfd_get_target (abfd);

  /* Return a proper value for DJGPP & PE COFF.
     This function is required for DWARF2 support, but there is
     no place to store this information in the COFF back end.
     Should enough other COFF targets add support for DWARF2,
     a place will have to be found.  Until then, this hack will do.  */
  if (CONST_STRNEQ (name, "coff-go32")
      || strcmp (name, "pe-i386") == 0
      || strcmp (name, "pei-i386") == 0
      || strcmp (name, "pe-arm-wince-little") == 0
      || strcmp (name, "pei-arm-wince-little") == 0)
    return 1;

  bfd_set_error (bfd_error_wrong_format);
  return -1;
}

/*
FUNCTION
	bfd_set_start_address

SYNOPSIS
 	bfd_boolean bfd_set_start_address (bfd *abfd, bfd_vma vma);

DESCRIPTION
	Make @var{vma} the entry point of output BFD @var{abfd}.

RETURNS
	Returns <<TRUE>> on success, <<FALSE>> otherwise.
*/

bfd_boolean
bfd_set_start_address (bfd *abfd, bfd_vma vma)
{
  abfd->start_address = vma;
  return TRUE;
}

/*
FUNCTION
	bfd_get_gp_size

SYNOPSIS
	unsigned int bfd_get_gp_size (bfd *abfd);

DESCRIPTION
	Return the maximum size of objects to be optimized using the GP
	register under MIPS ECOFF.  This is typically set by the <<-G>>
	argument to the compiler, assembler or linker.
*/

unsigned int
bfd_get_gp_size (bfd *abfd)
{
  if (abfd->format == bfd_object)
    {
      if (abfd->xvec->flavour == bfd_target_ecoff_flavour)
	return ecoff_data (abfd)->gp_size;
      else if (abfd->xvec->flavour == bfd_target_elf_flavour)
	return elf_gp_size (abfd);
    }
  return 0;
}

/*
FUNCTION
	bfd_set_gp_size

SYNOPSIS
	void bfd_set_gp_size (bfd *abfd, unsigned int i);

DESCRIPTION
	Set the maximum size of objects to be optimized using the GP
	register under ECOFF or MIPS ELF.  This is typically set by
	the <<-G>> argument to the compiler, assembler or linker.
*/

void
bfd_set_gp_size (bfd *abfd, unsigned int i)
{
  /* Don't try to set GP size on an archive or core file!  */
  if (abfd->format != bfd_object)
    return;

  if (abfd->xvec->flavour == bfd_target_ecoff_flavour)
    ecoff_data (abfd)->gp_size = i;
  else if (abfd->xvec->flavour == bfd_target_elf_flavour)
    elf_gp_size (abfd) = i;
}

/* Get the GP value.  This is an internal function used by some of the
   relocation special_function routines on targets which support a GP
   register.  */

bfd_vma
_bfd_get_gp_value (bfd *abfd)
{
  if (! abfd)
    return 0;
  if (abfd->format != bfd_object)
    return 0;

  if (abfd->xvec->flavour == bfd_target_ecoff_flavour)
    return ecoff_data (abfd)->gp;
  else if (abfd->xvec->flavour == bfd_target_elf_flavour)
    return elf_gp (abfd);

  return 0;
}

/* Set the GP value.  */

void
_bfd_set_gp_value (bfd *abfd, bfd_vma v)
{
  if (! abfd)
    BFD_FAIL ();
  if (abfd->format != bfd_object)
    return;

  if (abfd->xvec->flavour == bfd_target_ecoff_flavour)
    ecoff_data (abfd)->gp = v;
  else if (abfd->xvec->flavour == bfd_target_elf_flavour)
    elf_gp (abfd) = v;
}

/*
FUNCTION
	bfd_scan_vma

SYNOPSIS
	bfd_vma bfd_scan_vma (const char *string, const char **end, int base);

DESCRIPTION
	Convert, like <<strtoul>>, a numerical expression
	@var{string} into a <<bfd_vma>> integer, and return that integer.
	(Though without as many bells and whistles as <<strtoul>>.)
	The expression is assumed to be unsigned (i.e., positive).
	If given a @var{base}, it is used as the base for conversion.
	A base of 0 causes the function to interpret the string
	in hex if a leading "0x" or "0X" is found, otherwise
	in octal if a leading zero is found, otherwise in decimal.

	If the value would overflow, the maximum <<bfd_vma>> value is
	returned.
*/

bfd_vma
bfd_scan_vma (const char *string, const char **end, int base)
{
  bfd_vma value;
  bfd_vma cutoff;
  unsigned int cutlim;
  int overflow;

  /* Let the host do it if possible.  */
  if (sizeof (bfd_vma) <= sizeof (unsigned long))
    return strtoul (string, (char **) end, base);

#ifdef HAVE_STRTOULL
  if (sizeof (bfd_vma) <= sizeof (unsigned long long))
    return strtoull (string, (char **) end, base);
#endif

  if (base == 0)
    {
      if (string[0] == '0')
	{
	  if ((string[1] == 'x') || (string[1] == 'X'))
	    base = 16;
	  else
	    base = 8;
	}
    }

  if ((base < 2) || (base > 36))
    base = 10;

  if (base == 16
      && string[0] == '0'
      && (string[1] == 'x' || string[1] == 'X')
      && ISXDIGIT (string[2]))
    {
      string += 2;
    }

  cutoff = (~ (bfd_vma) 0) / (bfd_vma) base;
  cutlim = (~ (bfd_vma) 0) % (bfd_vma) base;
  value = 0;
  overflow = 0;
  while (1)
    {
      unsigned int digit;

      digit = *string;
      if (ISDIGIT (digit))
	digit = digit - '0';
      else if (ISALPHA (digit))
	digit = TOUPPER (digit) - 'A' + 10;
      else
	break;
      if (digit >= (unsigned int) base)
	break;
      if (value > cutoff || (value == cutoff && digit > cutlim))
	overflow = 1;
      value = value * base + digit;
      ++string;
    }

  if (overflow)
    value = ~ (bfd_vma) 0;

  if (end != NULL)
    *end = string;

  return value;
}

/*
FUNCTION
	bfd_copy_private_header_data

SYNOPSIS
	bfd_boolean bfd_copy_private_header_data (bfd *ibfd, bfd *obfd);

DESCRIPTION
	Copy private BFD header information from the BFD @var{ibfd} to the
	the BFD @var{obfd}.  This copies information that may require
	sections to exist, but does not require symbol tables.  Return
	<<true>> on success, <<false>> on error.
	Possible error returns are:

	o <<bfd_error_no_memory>> -
	Not enough memory exists to create private data for @var{obfd}.

.#define bfd_copy_private_header_data(ibfd, obfd) \
.     BFD_SEND (obfd, _bfd_copy_private_header_data, \
.		(ibfd, obfd))

*/

/*
FUNCTION
	bfd_copy_private_bfd_data

SYNOPSIS
	bfd_boolean bfd_copy_private_bfd_data (bfd *ibfd, bfd *obfd);

DESCRIPTION
	Copy private BFD information from the BFD @var{ibfd} to the
	the BFD @var{obfd}.  Return <<TRUE>> on success, <<FALSE>> on error.
	Possible error returns are:

	o <<bfd_error_no_memory>> -
	Not enough memory exists to create private data for @var{obfd}.

.#define bfd_copy_private_bfd_data(ibfd, obfd) \
.     BFD_SEND (obfd, _bfd_copy_private_bfd_data, \
.		(ibfd, obfd))

*/

/*
FUNCTION
	bfd_merge_private_bfd_data

SYNOPSIS
	bfd_boolean bfd_merge_private_bfd_data (bfd *ibfd, bfd *obfd);

DESCRIPTION
	Merge private BFD information from the BFD @var{ibfd} to the
	the output file BFD @var{obfd} when linking.  Return <<TRUE>>
	on success, <<FALSE>> on error.  Possible error returns are:

	o <<bfd_error_no_memory>> -
	Not enough memory exists to create private data for @var{obfd}.

.#define bfd_merge_private_bfd_data(ibfd, obfd) \
.     BFD_SEND (obfd, _bfd_merge_private_bfd_data, \
.		(ibfd, obfd))

*/

/*
FUNCTION
	bfd_set_private_flags

SYNOPSIS
	bfd_boolean bfd_set_private_flags (bfd *abfd, flagword flags);

DESCRIPTION
	Set private BFD flag information in the BFD @var{abfd}.
	Return <<TRUE>> on success, <<FALSE>> on error.  Possible error
	returns are:

	o <<bfd_error_no_memory>> -
	Not enough memory exists to create private data for @var{obfd}.

.#define bfd_set_private_flags(abfd, flags) \
.     BFD_SEND (abfd, _bfd_set_private_flags, (abfd, flags))

*/

/*
FUNCTION
	Other functions

DESCRIPTION
	The following functions exist but have not yet been documented.

.#define bfd_sizeof_headers(abfd, info) \
.       BFD_SEND (abfd, _bfd_sizeof_headers, (abfd, info))
.
.#define bfd_find_nearest_line(abfd, sec, syms, off, file, func, line) \
.       BFD_SEND (abfd, _bfd_find_nearest_line, \
.                 (abfd, sec, syms, off, file, func, line))
.
.#define bfd_find_line(abfd, syms, sym, file, line) \
.       BFD_SEND (abfd, _bfd_find_line, \
.                 (abfd, syms, sym, file, line))
.
.#define bfd_find_inliner_info(abfd, file, func, line) \
.       BFD_SEND (abfd, _bfd_find_inliner_info, \
.                 (abfd, file, func, line))
.
.#define bfd_debug_info_start(abfd) \
.       BFD_SEND (abfd, _bfd_debug_info_start, (abfd))
.
.#define bfd_debug_info_end(abfd) \
.       BFD_SEND (abfd, _bfd_debug_info_end, (abfd))
.
.#define bfd_debug_info_accumulate(abfd, section) \
.       BFD_SEND (abfd, _bfd_debug_info_accumulate, (abfd, section))
.
.#define bfd_stat_arch_elt(abfd, stat) \
.       BFD_SEND (abfd, _bfd_stat_arch_elt,(abfd, stat))
.
.#define bfd_update_armap_timestamp(abfd) \
.       BFD_SEND (abfd, _bfd_update_armap_timestamp, (abfd))
.
.#define bfd_set_arch_mach(abfd, arch, mach)\
.       BFD_SEND ( abfd, _bfd_set_arch_mach, (abfd, arch, mach))
.
.#define bfd_relax_section(abfd, section, link_info, again) \
.       BFD_SEND (abfd, _bfd_relax_section, (abfd, section, link_info, again))
.
.#define bfd_gc_sections(abfd, link_info) \
.	BFD_SEND (abfd, _bfd_gc_sections, (abfd, link_info))
.
.#define bfd_merge_sections(abfd, link_info) \
.	BFD_SEND (abfd, _bfd_merge_sections, (abfd, link_info))
.
.#define bfd_is_group_section(abfd, sec) \
.	BFD_SEND (abfd, _bfd_is_group_section, (abfd, sec))
.
.#define bfd_discard_group(abfd, sec) \
.	BFD_SEND (abfd, _bfd_discard_group, (abfd, sec))
.
.#define bfd_link_hash_table_create(abfd) \
.	BFD_SEND (abfd, _bfd_link_hash_table_create, (abfd))
.
.#define bfd_link_hash_table_free(abfd, hash) \
.	BFD_SEND (abfd, _bfd_link_hash_table_free, (hash))
.
.#define bfd_link_add_symbols(abfd, info) \
.	BFD_SEND (abfd, _bfd_link_add_symbols, (abfd, info))
.
.#define bfd_link_just_syms(abfd, sec, info) \
.	BFD_SEND (abfd, _bfd_link_just_syms, (sec, info))
.
.#define bfd_final_link(abfd, info) \
.	BFD_SEND (abfd, _bfd_final_link, (abfd, info))
.
.#define bfd_free_cached_info(abfd) \
.       BFD_SEND (abfd, _bfd_free_cached_info, (abfd))
.
.#define bfd_get_dynamic_symtab_upper_bound(abfd) \
.	BFD_SEND (abfd, _bfd_get_dynamic_symtab_upper_bound, (abfd))
.
.#define bfd_print_private_bfd_data(abfd, file)\
.	BFD_SEND (abfd, _bfd_print_private_bfd_data, (abfd, file))
.
.#define bfd_canonicalize_dynamic_symtab(abfd, asymbols) \
.	BFD_SEND (abfd, _bfd_canonicalize_dynamic_symtab, (abfd, asymbols))
.
.#define bfd_get_synthetic_symtab(abfd, count, syms, dyncount, dynsyms, ret) \
.	BFD_SEND (abfd, _bfd_get_synthetic_symtab, (abfd, count, syms, \
.						    dyncount, dynsyms, ret))
.
.#define bfd_get_dynamic_reloc_upper_bound(abfd) \
.	BFD_SEND (abfd, _bfd_get_dynamic_reloc_upper_bound, (abfd))
.
.#define bfd_canonicalize_dynamic_reloc(abfd, arels, asyms) \
.	BFD_SEND (abfd, _bfd_canonicalize_dynamic_reloc, (abfd, arels, asyms))
.
.extern bfd_byte *bfd_get_relocated_section_contents
.  (bfd *, struct bfd_link_info *, struct bfd_link_order *, bfd_byte *,
.   bfd_boolean, asymbol **);
.

*/

bfd_byte *
bfd_get_relocated_section_contents (bfd *abfd,
				    struct bfd_link_info *link_info,
				    struct bfd_link_order *link_order,
				    bfd_byte *data,
				    bfd_boolean relocatable,
				    asymbol **symbols)
{
  bfd *abfd2;
  bfd_byte *(*fn) (bfd *, struct bfd_link_info *, struct bfd_link_order *,
		   bfd_byte *, bfd_boolean, asymbol **);

  if (link_order->type == bfd_indirect_link_order)
    {
      abfd2 = link_order->u.indirect.section->owner;
      if (abfd2 == NULL)
	abfd2 = abfd;
    }
  else
    abfd2 = abfd;

  fn = abfd2->xvec->_bfd_get_relocated_section_contents;

  return (*fn) (abfd, link_info, link_order, data, relocatable, symbols);
}

/* Record information about an ELF program header.  */

bfd_boolean
bfd_record_phdr (bfd *abfd,
		 unsigned long type,
		 bfd_boolean flags_valid,
		 flagword flags,
		 bfd_boolean at_valid,
		 bfd_vma at,
		 bfd_boolean includes_filehdr,
		 bfd_boolean includes_phdrs,
		 unsigned int count,
		 asection **secs)
{
  struct elf_segment_map *m, **pm;
  bfd_size_type amt;

  if (bfd_get_flavour (abfd) != bfd_target_elf_flavour)
    return TRUE;

  amt = sizeof (struct elf_segment_map);
  amt += ((bfd_size_type) count - 1) * sizeof (asection *);
  m = bfd_zalloc (abfd, amt);
  if (m == NULL)
    return FALSE;

  m->p_type = type;
  m->p_flags = flags;
  m->p_paddr = at;
  m->p_flags_valid = flags_valid;
  m->p_paddr_valid = at_valid;
  m->includes_filehdr = includes_filehdr;
  m->includes_phdrs = includes_phdrs;
  m->count = count;
  if (count > 0)
    memcpy (m->sections, secs, count * sizeof (asection *));

  for (pm = &elf_tdata (abfd)->segment_map; *pm != NULL; pm = &(*pm)->next)
    ;
  *pm = m;

  return TRUE;
}

void
bfd_sprintf_vma (bfd *abfd, char *buf, bfd_vma value)
{
  if (bfd_get_flavour (abfd) == bfd_target_elf_flavour)
    get_elf_backend_data (abfd)->elf_backend_sprintf_vma (abfd, buf, value);
  else
    sprintf_vma (buf, value);
}

void
bfd_fprintf_vma (bfd *abfd, void *stream, bfd_vma value)
{
  if (bfd_get_flavour (abfd) == bfd_target_elf_flavour)
    get_elf_backend_data (abfd)->elf_backend_fprintf_vma (abfd, stream, value);
#ifdef BFD64
  /* fprintf_vma() on a 64-bit enabled host will always print a 64-bit
     value, but really we want to display the address in the target's
     address size.  Since we do not have a field in the bfd structure
     to tell us this, we take a guess, based on the target's name.  */
  else if (strstr (bfd_get_target (abfd), "64") == NULL
	   && strcmp (bfd_get_target (abfd), "mmo") != 0)
    fprintf ((FILE *) stream, "%08lx", (unsigned long) (value & 0xffffffff));
#endif
  else
    fprintf_vma ((FILE *) stream, value);
}

/*
FUNCTION
	bfd_alt_mach_code

SYNOPSIS
	bfd_boolean bfd_alt_mach_code (bfd *abfd, int alternative);

DESCRIPTION

	When more than one machine code number is available for the
	same machine type, this function can be used to switch between
	the preferred one (alternative == 0) and any others.  Currently,
	only ELF supports this feature, with up to two alternate
	machine codes.
*/

bfd_boolean
bfd_alt_mach_code (bfd *abfd, int alternative)
{
  if (bfd_get_flavour (abfd) == bfd_target_elf_flavour)
    {
      int code;

      switch (alternative)
	{
	case 0:
	  code = get_elf_backend_data (abfd)->elf_machine_code;
	  break;

	case 1:
	  code = get_elf_backend_data (abfd)->elf_machine_alt1;
	  if (code == 0)
	    return FALSE;
	  break;

	case 2:
	  code = get_elf_backend_data (abfd)->elf_machine_alt2;
	  if (code == 0)
	    return FALSE;
	  break;

	default:
	  return FALSE;
	}

      elf_elfheader (abfd)->e_machine = code;

      return TRUE;
    }

  return FALSE;
}

/*
CODE_FRAGMENT

.struct bfd_preserve
.{
.  void *marker;
.  void *tdata;
.  flagword flags;
.  const struct bfd_arch_info *arch_info;
.  struct bfd_section *sections;
.  struct bfd_section *section_last;
.  unsigned int section_count;
.  struct bfd_hash_table section_htab;
.};
.
*/

/*
FUNCTION
	bfd_preserve_save

SYNOPSIS
	bfd_boolean bfd_preserve_save (bfd *, struct bfd_preserve *);

DESCRIPTION
	When testing an object for compatibility with a particular
	target back-end, the back-end object_p function needs to set
	up certain fields in the bfd on successfully recognizing the
	object.  This typically happens in a piecemeal fashion, with
	failures possible at many points.  On failure, the bfd is
	supposed to be restored to its initial state, which is
	virtually impossible.  However, restoring a subset of the bfd
	state works in practice.  This function stores the subset and
	reinitializes the bfd.

*/

bfd_boolean
bfd_preserve_save (bfd *abfd, struct bfd_preserve *preserve)
{
  preserve->tdata = abfd->tdata.any;
  preserve->arch_info = abfd->arch_info;
  preserve->flags = abfd->flags;
  preserve->sections = abfd->sections;
  preserve->section_last = abfd->section_last;
  preserve->section_count = abfd->section_count;
  preserve->section_htab = abfd->section_htab;

  if (! bfd_hash_table_init (&abfd->section_htab, bfd_section_hash_newfunc,
			     sizeof (struct section_hash_entry)))
    return FALSE;

  abfd->tdata.any = NULL;
  abfd->arch_info = &bfd_default_arch_struct;
  abfd->flags &= BFD_IN_MEMORY;
  abfd->sections = NULL;
  abfd->section_last = NULL;
  abfd->section_count = 0;

  return TRUE;
}

/*
FUNCTION
	bfd_preserve_restore

SYNOPSIS
	void bfd_preserve_restore (bfd *, struct bfd_preserve *);

DESCRIPTION
	This function restores bfd state saved by bfd_preserve_save.
	If MARKER is non-NULL in struct bfd_preserve then that block
	and all subsequently bfd_alloc'd memory is freed.

*/

void
bfd_preserve_restore (bfd *abfd, struct bfd_preserve *preserve)
{
  bfd_hash_table_free (&abfd->section_htab);

  abfd->tdata.any = preserve->tdata;
  abfd->arch_info = preserve->arch_info;
  abfd->flags = preserve->flags;
  abfd->section_htab = preserve->section_htab;
  abfd->sections = preserve->sections;
  abfd->section_last = preserve->section_last;
  abfd->section_count = preserve->section_count;

  /* bfd_release frees all memory more recently bfd_alloc'd than
     its arg, as well as its arg.  */
  if (preserve->marker != NULL)
    {
      bfd_release (abfd, preserve->marker);
      preserve->marker = NULL;
    }
}

/*
FUNCTION
	bfd_preserve_finish

SYNOPSIS
	void bfd_preserve_finish (bfd *, struct bfd_preserve *);

DESCRIPTION
	This function should be called when the bfd state saved by
	bfd_preserve_save is no longer needed.  ie. when the back-end
	object_p function returns with success.

*/

void
bfd_preserve_finish (bfd *abfd ATTRIBUTE_UNUSED, struct bfd_preserve *preserve)
{
  /* It would be nice to be able to free more memory here, eg. old
     tdata, but that's not possible since these blocks are sitting
     inside bfd_alloc'd memory.  The section hash is on a separate
     objalloc.  */
  bfd_hash_table_free (&preserve->section_htab);
}

/*
FUNCTION
	bfd_emul_get_maxpagesize

SYNOPSIS
 	bfd_vma bfd_emul_get_maxpagesize (const char *);

DESCRIPTION
	Returns the maximum page size, in bytes, as determined by
	emulation.

RETURNS
	Returns the maximum page size in bytes for ELF, abort
	otherwise.
*/

bfd_vma
bfd_emul_get_maxpagesize (const char *emul)
{
  const bfd_target *target;

  target = bfd_find_target (emul, NULL);
  if (target != NULL
      && target->flavour == bfd_target_elf_flavour)
    return xvec_get_elf_backend_data (target)->maxpagesize;

  abort ();
  return 0;
}

static void
bfd_elf_set_pagesize (const bfd_target *target, bfd_vma size,
		      int offset, const bfd_target *orig_target)
{
  if (target->flavour == bfd_target_elf_flavour)
    {
      const struct elf_backend_data *bed;

      bed = xvec_get_elf_backend_data (target);
      *((bfd_vma *) ((char *) bed + offset)) = size;
    }

  if (target->alternative_target
      && target->alternative_target != orig_target)
    bfd_elf_set_pagesize (target->alternative_target, size, offset,
			  orig_target);
}

/*
FUNCTION
	bfd_emul_set_maxpagesize

SYNOPSIS
 	void bfd_emul_set_maxpagesize (const char *, bfd_vma);

DESCRIPTION
	For ELF, set the maximum page size for the emulation.  It is
	a no-op for other formats.

*/

void
bfd_emul_set_maxpagesize (const char *emul, bfd_vma size)
{
  const bfd_target *target;

  target = bfd_find_target (emul, NULL);
  if (target)
    bfd_elf_set_pagesize (target, size,
			  offsetof (struct elf_backend_data,
				    maxpagesize), target);
}

/*
FUNCTION
	bfd_emul_get_commonpagesize

SYNOPSIS
 	bfd_vma bfd_emul_get_commonpagesize (const char *);

DESCRIPTION
	Returns the common page size, in bytes, as determined by
	emulation.

RETURNS
	Returns the common page size in bytes for ELF, abort otherwise.
*/

bfd_vma
bfd_emul_get_commonpagesize (const char *emul)
{
  const bfd_target *target;

  target = bfd_find_target (emul, NULL);
  if (target != NULL
      && target->flavour == bfd_target_elf_flavour)
    return xvec_get_elf_backend_data (target)->commonpagesize;

  abort ();
  return 0;
}

/*
FUNCTION
	bfd_emul_set_commonpagesize

SYNOPSIS
 	void bfd_emul_set_commonpagesize (const char *, bfd_vma);

DESCRIPTION
	For ELF, set the common page size for the emulation.  It is
	a no-op for other formats.

*/

void
bfd_emul_set_commonpagesize (const char *emul, bfd_vma size)
{
  const bfd_target *target;

  target = bfd_find_target (emul, NULL);
  if (target)
    bfd_elf_set_pagesize (target, size,
			  offsetof (struct elf_backend_data,
				    commonpagesize), target);
}

/*
FUNCTION
	bfd_demangle

SYNOPSIS
	char *bfd_demangle (bfd *, const char *, int);

DESCRIPTION
	Wrapper around cplus_demangle.  Strips leading underscores and
	other such chars that would otherwise confuse the demangler.
	If passed a g++ v3 ABI mangled name, returns a buffer allocated
	with malloc holding the demangled name.  Returns NULL otherwise
	and on memory alloc failure.
*/

char *
bfd_demangle (bfd *abfd, const char *name, int options)
{
  char *res, *alloc;
  const char *pre, *suf;
  size_t pre_len;

  if (abfd != NULL
      && *name != '\0'
      && bfd_get_symbol_leading_char (abfd) == *name)
    ++name;

  /* This is a hack for better error reporting on XCOFF, PowerPC64-ELF
     or the MS PE format.  These formats have a number of leading '.'s
     on at least some symbols, so we remove all dots to avoid
     confusing the demangler.  */
  pre = name;
  while (*name == '.' || *name == '$')
    ++name;
  pre_len = name - pre;

  /* Strip off @plt and suchlike too.  */
  alloc = NULL;
  suf = strchr (name, '@');
  if (suf != NULL)
    {
      alloc = bfd_malloc (suf - name + 1);
      if (alloc == NULL)
	return NULL;
      memcpy (alloc, name, suf - name);
      alloc[suf - name] = '\0';
      name = alloc;
    }

  res = cplus_demangle (name, options);

  if (alloc != NULL)
    free (alloc);

  if (res == NULL)
    return NULL;

  /* Put back any prefix or suffix.  */
  if (pre_len != 0 || suf != NULL)
    {
      size_t len;
      size_t suf_len;
      char *final;

      len = strlen (res);
      if (suf == NULL)
	suf = res + len;
      suf_len = strlen (suf) + 1;
      final = bfd_malloc (pre_len + len + suf_len);
      if (final != NULL)
	{
	  memcpy (final, pre, pre_len);
	  memcpy (final + pre_len, res, len);
	  memcpy (final + pre_len + len, suf, suf_len);
	}
      free (res);
      res = final;
    }

  return res;
}
