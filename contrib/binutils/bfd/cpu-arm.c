/* BFD support for the ARM processor
   Copyright 1994, 1997, 1999, 2000, 2002, 2003, 2004, 2005, 2007
   Free Software Foundation, Inc.
   Contributed by Richard Earnshaw (rwe@pegasus.esprit.ec.org)

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

#include "sysdep.h"
#include "bfd.h"
#include "libbfd.h"
#include "libiberty.h"

/* This routine is provided two arch_infos and works out which ARM
   machine which would be compatible with both and returns a pointer
   to its info structure.  */

static const bfd_arch_info_type *
compatible (const bfd_arch_info_type *a, const bfd_arch_info_type *b)
{
  /* If a & b are for different architecture we can do nothing.  */
  if (a->arch != b->arch)
      return NULL;

  /* If a & b are for the same machine then all is well.  */
  if (a->mach == b->mach)
    return a;

  /* Otherwise if either a or b is the 'default' machine
     then it can be polymorphed into the other.  */
  if (a->the_default)
    return b;

  if (b->the_default)
    return a;

  /* So far all newer ARM architecture cores are
     supersets of previous cores.  */
  if (a->mach < b->mach)
    return b;
  else if (a->mach > b->mach)
    return a;

  /* Never reached!  */
  return NULL;
}

static struct
{
  unsigned int mach;
  char *       name;
}
processors[] =
{
  { bfd_mach_arm_2,  "arm2"     },
  { bfd_mach_arm_2a, "arm250"   },
  { bfd_mach_arm_2a, "arm3"     },
  { bfd_mach_arm_3,  "arm6"     },
  { bfd_mach_arm_3,  "arm60"    },
  { bfd_mach_arm_3,  "arm600"   },
  { bfd_mach_arm_3,  "arm610"   },
  { bfd_mach_arm_3,  "arm7"     },
  { bfd_mach_arm_3,  "arm710"   },
  { bfd_mach_arm_3,  "arm7500"  },
  { bfd_mach_arm_3,  "arm7d"    },
  { bfd_mach_arm_3,  "arm7di"   },
  { bfd_mach_arm_3M, "arm7dm"   },
  { bfd_mach_arm_3M, "arm7dmi"  },
  { bfd_mach_arm_4T, "arm7tdmi" },
  { bfd_mach_arm_4,  "arm8"     },
  { bfd_mach_arm_4,  "arm810"   },
  { bfd_mach_arm_4,  "arm9"     },
  { bfd_mach_arm_4,  "arm920"   },
  { bfd_mach_arm_4T, "arm920t"  },
  { bfd_mach_arm_4T, "arm9tdmi" },
  { bfd_mach_arm_4,  "sa1"      },
  { bfd_mach_arm_4,  "strongarm"},
  { bfd_mach_arm_4,  "strongarm110" },
  { bfd_mach_arm_4,  "strongarm1100" },
  { bfd_mach_arm_XScale, "xscale" },
  { bfd_mach_arm_ep9312, "ep9312" },
  { bfd_mach_arm_iWMMXt, "iwmmxt" },
  { bfd_mach_arm_iWMMXt2, "iwmmxt2" }
};

static bfd_boolean
scan (const struct bfd_arch_info *info, const char *string)
{
  int  i;

  /* First test for an exact match.  */
  if (strcasecmp (string, info->printable_name) == 0)
    return TRUE;

  /* Next check for a processor name instead of an Architecture name.  */
  for (i = sizeof (processors) / sizeof (processors[0]); i--;)
    {
      if (strcasecmp (string, processors [i].name) == 0)
	break;
    }

  if (i != -1 && info->mach == processors [i].mach)
    return TRUE;

  /* Finally check for the default architecture.  */
  if (strcasecmp (string, "arm") == 0)
    return info->the_default;

  return FALSE;
}

#define N(number, print, default, next)  \
{  32, 32, 8, bfd_arch_arm, number, "arm", print, 4, default, compatible, scan, next }

static const bfd_arch_info_type arch_info_struct[] =
{
  N (bfd_mach_arm_2,      "armv2",   FALSE, & arch_info_struct[1]),
  N (bfd_mach_arm_2a,     "armv2a",  FALSE, & arch_info_struct[2]),
  N (bfd_mach_arm_3,      "armv3",   FALSE, & arch_info_struct[3]),
  N (bfd_mach_arm_3M,     "armv3m",  FALSE, & arch_info_struct[4]),
  N (bfd_mach_arm_4,      "armv4",   FALSE, & arch_info_struct[5]),
  N (bfd_mach_arm_4T,     "armv4t",  FALSE, & arch_info_struct[6]),
  N (bfd_mach_arm_5,      "armv5",   FALSE, & arch_info_struct[7]),
  N (bfd_mach_arm_5T,     "armv5t",  FALSE, & arch_info_struct[8]),
  N (bfd_mach_arm_5TE,    "armv5te", FALSE, & arch_info_struct[9]),
  N (bfd_mach_arm_XScale, "xscale",  FALSE, & arch_info_struct[10]),
  N (bfd_mach_arm_ep9312, "ep9312",  FALSE, & arch_info_struct[11]),
  N (bfd_mach_arm_iWMMXt, "iwmmxt",  FALSE, & arch_info_struct[12]),
  N (bfd_mach_arm_iWMMXt2, "iwmmxt2", FALSE, NULL)
};

const bfd_arch_info_type bfd_arm_arch =
  N (0, "arm", TRUE, & arch_info_struct[0]);

/* Support functions used by both the COFF and ELF versions of the ARM port.  */

/* Handle the merging of the 'machine' settings of input file IBFD
   and an output file OBFD.  These values actually represent the
   different possible ARM architecture variants.
   Returns TRUE if they were merged successfully or FALSE otherwise.  */

bfd_boolean
bfd_arm_merge_machines (bfd *ibfd, bfd *obfd)
{
  unsigned int in  = bfd_get_mach (ibfd);
  unsigned int out = bfd_get_mach (obfd);

  /* If the output architecture is unknown, we now have a value to set.  */
  if (out == bfd_mach_arm_unknown)
    bfd_set_arch_mach (obfd, bfd_arch_arm, in);

  /* If the input architecture is unknown,
     then so must be the output architecture.  */
  else if (in == bfd_mach_arm_unknown)
    /* FIXME: We ought to have some way to
       override this on the command line.  */
    bfd_set_arch_mach (obfd, bfd_arch_arm, bfd_mach_arm_unknown);

  /* If they are the same then nothing needs to be done.  */
  else if (out == in)
    ;

  /* Otherwise the general principle that a earlier architecture can be
     linked with a later architecture to produce a binary that will execute
     on the later architecture.

     We fail however if we attempt to link a Cirrus EP9312 binary with an
     Intel XScale binary, since these architecture have co-processors which
     will not both be present on the same physical hardware.  */
  else if (in == bfd_mach_arm_ep9312
	   && (out == bfd_mach_arm_XScale
	       || out == bfd_mach_arm_iWMMXt
	       || out == bfd_mach_arm_iWMMXt2))
    {
      _bfd_error_handler (_("\
ERROR: %B is compiled for the EP9312, whereas %B is compiled for XScale"),
			  ibfd, obfd);
      bfd_set_error (bfd_error_wrong_format);
      return FALSE;
    }
  else if (out == bfd_mach_arm_ep9312
	   && (in == bfd_mach_arm_XScale
	       || in == bfd_mach_arm_iWMMXt
	       || in == bfd_mach_arm_iWMMXt2))
    {
      _bfd_error_handler (_("\
ERROR: %B is compiled for the EP9312, whereas %B is compiled for XScale"),
			  obfd, ibfd);
      bfd_set_error (bfd_error_wrong_format);
      return FALSE;
    }
  else if (in > out)
    bfd_set_arch_mach (obfd, bfd_arch_arm, in);
  /* else
     Nothing to do.  */

  return TRUE;
}

typedef struct
{
  unsigned char	namesz[4];	/* Size of entry's owner string.  */
  unsigned char	descsz[4];	/* Size of the note descriptor.  */
  unsigned char	type[4];	/* Interpretation of the descriptor.  */
  char		name[1];	/* Start of the name+desc data.  */
} arm_Note;

static bfd_boolean
arm_check_note (bfd *abfd,
		bfd_byte *buffer,
		bfd_size_type buffer_size,
		const char *expected_name,
		char **description_return)
{
  unsigned long namesz;
  unsigned long descsz;
  unsigned long type;
  char *        descr;

  if (buffer_size < offsetof (arm_Note, name))
    return FALSE;

  /* We have to extract the values this way to allow for a
     host whose endian-ness is different from the target.  */
  namesz = bfd_get_32 (abfd, buffer);
  descsz = bfd_get_32 (abfd, buffer + offsetof (arm_Note, descsz));
  type   = bfd_get_32 (abfd, buffer + offsetof (arm_Note, type));
  descr  = (char *) buffer + offsetof (arm_Note, name);

  /* Check for buffer overflow.  */
  if (namesz + descsz + offsetof (arm_Note, name) > buffer_size)
    return FALSE;

  if (expected_name == NULL)
    {
      if (namesz != 0)
	return FALSE;
    }
  else
    { 
      if (namesz != ((strlen (expected_name) + 1 + 3) & ~3))
	return FALSE;
      
      if (strcmp (descr, expected_name) != 0)
	return FALSE;

      descr += (namesz + 3) & ~3;
    }

  /* FIXME: We should probably check the type as well.  */

  if (description_return != NULL)
    * description_return = descr;

  return TRUE;
}

#define NOTE_ARCH_STRING 	"arch: "

bfd_boolean
bfd_arm_update_notes (bfd *abfd, const char *note_section)
{
  asection *     arm_arch_section;
  bfd_size_type  buffer_size;
  bfd_byte *     buffer;
  char *         arch_string;
  char *         expected;

  /* Look for a note section.  If one is present check the architecture
     string encoded in it, and set it to the current architecture if it is
     different.  */
  arm_arch_section = bfd_get_section_by_name (abfd, note_section);

  if (arm_arch_section == NULL)
    return TRUE;

  buffer_size = arm_arch_section->size;
  if (buffer_size == 0)
    return FALSE;

  if (!bfd_malloc_and_get_section (abfd, arm_arch_section, &buffer))
    goto FAIL;

  /* Parse the note.  */
  if (! arm_check_note (abfd, buffer, buffer_size, NOTE_ARCH_STRING, & arch_string))
    goto FAIL;

  /* Check the architecture in the note against the architecture of the bfd.  */
  switch (bfd_get_mach (abfd))
    {
    default:
    case bfd_mach_arm_unknown: expected = "unknown"; break;
    case bfd_mach_arm_2:       expected = "armv2"; break;
    case bfd_mach_arm_2a:      expected = "armv2a"; break;
    case bfd_mach_arm_3:       expected = "armv3"; break;
    case bfd_mach_arm_3M:      expected = "armv3M"; break;
    case bfd_mach_arm_4:       expected = "armv4"; break;
    case bfd_mach_arm_4T:      expected = "armv4t"; break;
    case bfd_mach_arm_5:       expected = "armv5"; break;
    case bfd_mach_arm_5T:      expected = "armv5t"; break;
    case bfd_mach_arm_5TE:     expected = "armv5te"; break;
    case bfd_mach_arm_XScale:  expected = "XScale"; break;
    case bfd_mach_arm_ep9312:  expected = "ep9312"; break;
    case bfd_mach_arm_iWMMXt:  expected = "iWMMXt"; break;
    case bfd_mach_arm_iWMMXt2: expected = "iWMMXt2"; break;
    }

  if (strcmp (arch_string, expected) != 0)
    {
      strcpy ((char *) buffer + (offsetof (arm_Note, name)
				 + ((strlen (NOTE_ARCH_STRING) + 3) & ~3)),
	      expected);

      if (! bfd_set_section_contents (abfd, arm_arch_section, buffer,
				      (file_ptr) 0, buffer_size))
	{
	  (*_bfd_error_handler)
	    (_("warning: unable to update contents of %s section in %s"),
	     note_section, bfd_get_filename (abfd));
	  goto FAIL;
	}
    }

  free (buffer);
  return TRUE;

 FAIL:
  if (buffer != NULL)
    free (buffer);
  return FALSE;
}


static struct
{
  const char * string;
  unsigned int mach;
}
architectures[] =
{
  { "armv2",   bfd_mach_arm_2 },
  { "armv2a",  bfd_mach_arm_2a },
  { "armv3",   bfd_mach_arm_3 },
  { "armv3M",  bfd_mach_arm_3M },
  { "armv4",   bfd_mach_arm_4 },
  { "armv4t",  bfd_mach_arm_4T },
  { "armv5",   bfd_mach_arm_5 },
  { "armv5t",  bfd_mach_arm_5T },
  { "armv5te", bfd_mach_arm_5TE },
  { "XScale",  bfd_mach_arm_XScale },
  { "ep9312",  bfd_mach_arm_ep9312 },
  { "iWMMXt",  bfd_mach_arm_iWMMXt },
  { "iWMMXt2", bfd_mach_arm_iWMMXt2 }
};

/* Extract the machine number stored in a note section.  */
unsigned int
bfd_arm_get_mach_from_notes (bfd *abfd, const char *note_section)
{
  asection *     arm_arch_section;
  bfd_size_type  buffer_size;
  bfd_byte *     buffer;
  char *         arch_string;
  int            i;

  /* Look for a note section.  If one is present check the architecture
     string encoded in it, and set it to the current architecture if it is
     different.  */
  arm_arch_section = bfd_get_section_by_name (abfd, note_section);

  if (arm_arch_section == NULL)
    return bfd_mach_arm_unknown;

  buffer_size = arm_arch_section->size;
  if (buffer_size == 0)
    return bfd_mach_arm_unknown;

  if (!bfd_malloc_and_get_section (abfd, arm_arch_section, &buffer))
    goto FAIL;

  /* Parse the note.  */
  if (! arm_check_note (abfd, buffer, buffer_size, NOTE_ARCH_STRING, & arch_string))
    goto FAIL;

  /* Interpret the architecture string.  */
  for (i = ARRAY_SIZE (architectures); i--;)
    if (strcmp (arch_string, architectures[i].string) == 0)
      {
	free (buffer);
	return architectures[i].mach;
      }

 FAIL:
  if (buffer != NULL)
    free (buffer);
  return bfd_mach_arm_unknown;
}

bfd_boolean
bfd_is_arm_special_symbol_name (const char * name, int type)
{
  /* The ARM compiler outputs several obsolete forms.  Recognize them
     in addition to the standard $a, $t and $d.  We are somewhat loose
     in what we accept here, since the full set is not documented.  */
  if (!name || name[0] != '$')
    return FALSE;
  if (name[1] == 'a' || name[1] == 't' || name[1] == 'd')
    type &= BFD_ARM_SPECIAL_SYM_TYPE_MAP;
  else if (name[1] == 'm' || name[1] == 'f' || name[1] == 'p')
    type &= BFD_ARM_SPECIAL_SYM_TYPE_TAG;
  else if (name[1] >= 'a' && name[1] <= 'z')
    type &= BFD_ARM_SPECIAL_SYM_TYPE_OTHER;
  else
    return FALSE;

  return (type != 0 && (name[2] == 0 || name[2] == '.'));
}

