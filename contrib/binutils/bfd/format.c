/* Generic BFD support for file formats.
   Copyright 1990, 1991, 1992, 1993, 1994, 1995, 1999, 2000, 2001, 2002,
   2003, 2005, 2007 Free Software Foundation, Inc.
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
	File formats

	A format is a BFD concept of high level file contents type. The
	formats supported by BFD are:

	o <<bfd_object>>

	The BFD may contain data, symbols, relocations and debug info.

	o <<bfd_archive>>

	The BFD contains other BFDs and an optional index.

	o <<bfd_core>>

	The BFD contains the result of an executable core dump.

SUBSECTION
	File format functions
*/

#include "sysdep.h"
#include "bfd.h"
#include "libbfd.h"

/* IMPORT from targets.c.  */
extern const size_t _bfd_target_vector_entries;

/*
FUNCTION
	bfd_check_format

SYNOPSIS
	bfd_boolean bfd_check_format (bfd *abfd, bfd_format format);

DESCRIPTION
	Verify if the file attached to the BFD @var{abfd} is compatible
	with the format @var{format} (i.e., one of <<bfd_object>>,
	<<bfd_archive>> or <<bfd_core>>).

	If the BFD has been set to a specific target before the
	call, only the named target and format combination is
	checked. If the target has not been set, or has been set to
	<<default>>, then all the known target backends is
	interrogated to determine a match.  If the default target
	matches, it is used.  If not, exactly one target must recognize
	the file, or an error results.

	The function returns <<TRUE>> on success, otherwise <<FALSE>>
	with one of the following error codes:

	o <<bfd_error_invalid_operation>> -
	if <<format>> is not one of <<bfd_object>>, <<bfd_archive>> or
	<<bfd_core>>.

	o <<bfd_error_system_call>> -
	if an error occured during a read - even some file mismatches
	can cause bfd_error_system_calls.

	o <<file_not_recognised>> -
	none of the backends recognised the file format.

	o <<bfd_error_file_ambiguously_recognized>> -
	more than one backend recognised the file format.
*/

bfd_boolean
bfd_check_format (bfd *abfd, bfd_format format)
{
  return bfd_check_format_matches (abfd, format, NULL);
}

/*
FUNCTION
	bfd_check_format_matches

SYNOPSIS
	bfd_boolean bfd_check_format_matches
	  (bfd *abfd, bfd_format format, char ***matching);

DESCRIPTION
	Like <<bfd_check_format>>, except when it returns FALSE with
	<<bfd_errno>> set to <<bfd_error_file_ambiguously_recognized>>.  In that
	case, if @var{matching} is not NULL, it will be filled in with
	a NULL-terminated list of the names of the formats that matched,
	allocated with <<malloc>>.
	Then the user may choose a format and try again.

	When done with the list that @var{matching} points to, the caller
	should free it.
*/

bfd_boolean
bfd_check_format_matches (bfd *abfd, bfd_format format, char ***matching)
{
  extern const bfd_target binary_vec;
  const bfd_target * const *target;
  const bfd_target **matching_vector = NULL;
  const bfd_target *save_targ, *right_targ, *ar_right_targ;
  int match_count;
  int ar_match_index;

  if (!bfd_read_p (abfd)
      || (unsigned int) abfd->format >= (unsigned int) bfd_type_end)
    {
      bfd_set_error (bfd_error_invalid_operation);
      return FALSE;
    }

  if (abfd->format != bfd_unknown)
    return abfd->format == format;

  /* Since the target type was defaulted, check them
     all in the hope that one will be uniquely recognized.  */
  save_targ = abfd->xvec;
  match_count = 0;
  ar_match_index = _bfd_target_vector_entries;

  if (matching)
    {
      bfd_size_type amt;

      *matching = NULL;
      amt = sizeof (*matching_vector) * 2 * _bfd_target_vector_entries;
      matching_vector = bfd_malloc (amt);
      if (!matching_vector)
	return FALSE;
    }

  right_targ = 0;
  ar_right_targ = 0;

  /* Presume the answer is yes.  */
  abfd->format = format;

  /* If the target type was explicitly specified, just check that target.  */
  if (!abfd->target_defaulted)
    {
      if (bfd_seek (abfd, (file_ptr) 0, SEEK_SET) != 0)	/* rewind! */
	{
	  if (matching)
	    free (matching_vector);
	  return FALSE;
	}

      right_targ = BFD_SEND_FMT (abfd, _bfd_check_format, (abfd));

      if (right_targ)
	{
	  abfd->xvec = right_targ;	/* Set the target as returned.  */

	  if (matching)
	    free (matching_vector);

	  /* If the file was opened for update, then `output_has_begun'
	     some time ago when the file was created.  Do not recompute
	     sections sizes or alignments in _bfd_set_section_contents.
	     We can not set this flag until after checking the format,
	     because it will interfere with creation of BFD sections.  */
	  if (abfd->direction == both_direction)
	    abfd->output_has_begun = TRUE;

	  return TRUE;			/* File position has moved, BTW.  */
	}

      /* For a long time the code has dropped through to check all
	 targets if the specified target was wrong.  I don't know why,
	 and I'm reluctant to change it.  However, in the case of an
	 archive, it can cause problems.  If the specified target does
	 not permit archives (e.g., the binary target), then we should
	 not allow some other target to recognize it as an archive, but
	 should instead allow the specified target to recognize it as an
	 object.  When I first made this change, it broke the PE target,
	 because the specified pei-i386 target did not recognize the
	 actual pe-i386 archive.  Since there may be other problems of
	 this sort, I changed this test to check only for the binary
	 target.  */
      if (format == bfd_archive && save_targ == &binary_vec)
	{
	  abfd->xvec = save_targ;
	  abfd->format = bfd_unknown;

	  if (matching)
	    free (matching_vector);

	  bfd_set_error (bfd_error_file_not_recognized);

	  return FALSE;
	}
    }

  for (target = bfd_target_vector; *target != NULL; target++)
    {
      const bfd_target *temp;
      bfd_error_type err;

      /* Don't check the default target twice.  */
      if (*target == &binary_vec
	  || (!abfd->target_defaulted && *target == save_targ))
	continue;

      abfd->xvec = *target;	/* Change BFD's target temporarily.  */

      if (bfd_seek (abfd, (file_ptr) 0, SEEK_SET) != 0)
	{
	  if (matching)
	    free (matching_vector);
	  return FALSE;
	}

      /* If _bfd_check_format neglects to set bfd_error, assume
	 bfd_error_wrong_format.  We didn't used to even pay any
	 attention to bfd_error, so I suspect that some
	 _bfd_check_format might have this problem.  */
      bfd_set_error (bfd_error_wrong_format);

      temp = BFD_SEND_FMT (abfd, _bfd_check_format, (abfd));

      if (temp)
	{
	  /* This format checks out as ok!  */
	  right_targ = temp;

	  /* If this is the default target, accept it, even if other
	     targets might match.  People who want those other targets
	     have to set the GNUTARGET variable.  */
	  if (temp == bfd_default_vector[0])
	    {
	      match_count = 1;
	      break;
	    }

	  if (matching)
	    matching_vector[match_count] = temp;

	  match_count++;
	}
      else if ((err = bfd_get_error ()) == bfd_error_wrong_object_format
	       || err == bfd_error_file_ambiguously_recognized)
	{
	  /* An archive with objects of the wrong type, or an
	     ambiguous match.  We want this target to match if we get
	     no better matches.  */
	  if (ar_right_targ != bfd_default_vector[0])
	    ar_right_targ = *target;
	  if (matching)
	    matching_vector[ar_match_index] = *target;
	  ar_match_index++;
	}
      else if (err != bfd_error_wrong_format)
	{
	  abfd->xvec = save_targ;
	  abfd->format = bfd_unknown;

	  if (matching)
	    free (matching_vector);

	  return FALSE;
	}
    }

  if (match_count == 0)
    {
      /* Try partial matches.  */
      right_targ = ar_right_targ;

      if (right_targ == bfd_default_vector[0])
	{
	  match_count = 1;
	}
      else
	{
	  match_count = ar_match_index - _bfd_target_vector_entries;

	  if (matching && match_count > 1)
	    memcpy (matching_vector,
		    matching_vector + _bfd_target_vector_entries,
		    sizeof (*matching_vector) * match_count);
	}
    }

  if (match_count > 1
      && bfd_associated_vector != NULL
      && matching)
    {
      const bfd_target * const *assoc = bfd_associated_vector;

      while ((right_targ = *assoc++) != NULL)
	{
	  int i = match_count;

	  while (--i >= 0)
	    if (matching_vector[i] == right_targ)
	      break;

	  if (i >= 0)
	    {
	      match_count = 1;
	      break;
	    }
	}
    }

  if (match_count == 1)
    {
      abfd->xvec = right_targ;		/* Change BFD's target permanently.  */

      if (matching)
	free (matching_vector);

      /* If the file was opened for update, then `output_has_begun'
	 some time ago when the file was created.  Do not recompute
	 sections sizes or alignments in _bfd_set_section_contents.
	 We can not set this flag until after checking the format,
	 because it will interfere with creation of BFD sections.  */
      if (abfd->direction == both_direction)
	abfd->output_has_begun = TRUE;

      return TRUE;			/* File position has moved, BTW.  */
    }

  abfd->xvec = save_targ;		/* Restore original target type.  */
  abfd->format = bfd_unknown;		/* Restore original format.  */

  if (match_count == 0)
    {
      bfd_set_error (bfd_error_file_not_recognized);

      if (matching)
	free (matching_vector);
    }
  else
    {
      bfd_set_error (bfd_error_file_ambiguously_recognized);

      if (matching)
	{
	  *matching = (char **) matching_vector;
	  matching_vector[match_count] = NULL;
	  /* Return target names.  This is a little nasty.  Maybe we
	     should do another bfd_malloc?  */
	  while (--match_count >= 0)
	    {
	      const char *name = matching_vector[match_count]->name;
	      *(const char **) &matching_vector[match_count] = name;
	    }
	}
    }

  return FALSE;
}

/*
FUNCTION
	bfd_set_format

SYNOPSIS
	bfd_boolean bfd_set_format (bfd *abfd, bfd_format format);

DESCRIPTION
	This function sets the file format of the BFD @var{abfd} to the
	format @var{format}. If the target set in the BFD does not
	support the format requested, the format is invalid, or the BFD
	is not open for writing, then an error occurs.
*/

bfd_boolean
bfd_set_format (bfd *abfd, bfd_format format)
{
  if (bfd_read_p (abfd)
      || (unsigned int) abfd->format >= (unsigned int) bfd_type_end)
    {
      bfd_set_error (bfd_error_invalid_operation);
      return FALSE;
    }

  if (abfd->format != bfd_unknown)
    return abfd->format == format;

  /* Presume the answer is yes.  */
  abfd->format = format;

  if (!BFD_SEND_FMT (abfd, _bfd_set_format, (abfd)))
    {
      abfd->format = bfd_unknown;
      return FALSE;
    }

  return TRUE;
}

/*
FUNCTION
	bfd_format_string

SYNOPSIS
	const char *bfd_format_string (bfd_format format);

DESCRIPTION
	Return a pointer to a const string
	<<invalid>>, <<object>>, <<archive>>, <<core>>, or <<unknown>>,
	depending upon the value of @var{format}.
*/

const char *
bfd_format_string (bfd_format format)
{
  if (((int) format < (int) bfd_unknown)
      || ((int) format >= (int) bfd_type_end))
    return "invalid";

  switch (format)
    {
    case bfd_object:
      return "object";		/* Linker/assembler/compiler output.  */
    case bfd_archive:
      return "archive";		/* Object archive file.  */
    case bfd_core:
      return "core";		/* Core dump.  */
    default:
      return "unknown";
    }
}
