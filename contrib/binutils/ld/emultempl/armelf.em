# This shell script emits a C file. -*- C -*-
#   Copyright 1991, 1993, 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003,
#   2004, 2005
#   Free Software Foundation, Inc.
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
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.
#

# This file is sourced from elf32.em, and defines extra arm-elf
# specific routines.
#
test -z "$TARGET2_TYPE" && TARGET2_TYPE="rel"
cat >>e${EMULATION_NAME}.c <<EOF

#include "elf/arm.h"

static char *thumb_entry_symbol = NULL;
static bfd *bfd_for_interwork;
static int byteswap_code = 0;
static int target1_is_rel = 0${TARGET1_IS_REL};
static char *target2_type = "${TARGET2_TYPE}";
static int fix_v4bx = 0;
static int use_blx = 0;
static bfd_arm_vfp11_fix vfp11_denorm_fix = BFD_ARM_VFP11_FIX_DEFAULT;
static int no_enum_size_warning = 0;
static int pic_veneer = 0;

static void
gld${EMULATION_NAME}_before_parse (void)
{
#ifndef TARGET_			/* I.e., if not generic.  */
  ldfile_set_output_arch ("`echo ${ARCH}`", bfd_arch_unknown);
#endif /* not TARGET_ */
  config.dynamic_link = ${DYNAMIC_LINK-TRUE};
  config.has_shared = `if test -n "$GENERATE_SHLIB_SCRIPT" ; then echo TRUE ; else echo FALSE ; fi`;
}

static void
arm_elf_after_open (void)
{
  if (strstr (bfd_get_target (output_bfd), "arm") == NULL)
    {
      /* The arm backend needs special fields in the output hash structure.
	 These will only be created if the output format is an arm format,
	 hence we do not support linking and changing output formats at the
	 same time.  Use a link followed by objcopy to change output formats.  */
      einfo ("%F%X%P: error: cannot change output format whilst linking ARM binaries\n");
      return;
    }

  {
    LANG_FOR_EACH_INPUT_STATEMENT (is)
      {
	bfd_elf32_arm_add_glue_sections_to_bfd (is->the_bfd, & link_info);
      }
  }

  /* Call the standard elf routine.  */
  gld${EMULATION_NAME}_after_open ();
}

static void
arm_elf_set_bfd_for_interworking (lang_statement_union_type *statement)
{
  if (statement->header.type == lang_input_section_enum)
    {
      asection *i = statement->input_section.section;

      if (!((lang_input_statement_type *) i->owner->usrdata)->just_syms_flag
	  && (i->flags & SEC_EXCLUDE) == 0)
	{
	  asection *output_section = i->output_section;

	  ASSERT (output_section->owner == output_bfd);

	  /* Don't attach the interworking stubs to a dynamic object, to
	     an empty section, etc.  */
	  if ((output_section->flags & SEC_HAS_CONTENTS) != 0
	      && (i->flags & SEC_NEVER_LOAD) == 0
	      && ! (i->owner->flags & DYNAMIC)
	      && ! i->owner->output_has_begun)
	    {
	      bfd_for_interwork = i->owner;
	      bfd_for_interwork->output_has_begun = TRUE;
	    }
	}
    }
}

static void
arm_elf_before_allocation (void)
{
  bfd *tem;

  if (link_info.input_bfds != NULL)
    {
      /* The interworking bfd must be the last one in the link.  */
      bfd_for_interwork = NULL;
      for (tem = link_info.input_bfds; tem != NULL; tem = tem->link_next)
	tem->output_has_begun = FALSE;

      lang_for_each_statement (arm_elf_set_bfd_for_interworking);
      for (tem = link_info.input_bfds; tem != NULL; tem = tem->link_next)
	tem->output_has_begun = FALSE;

      /* If bfd_for_interwork is NULL, then there are no loadable sections
	 with real contents to be linked, so we are not going to have to
	 create any interworking stubs, so it is OK not to call
	 bfd_elf32_arm_get_bfd_for_interworking.  */
      if (bfd_for_interwork != NULL)
	bfd_elf32_arm_get_bfd_for_interworking (bfd_for_interwork, &link_info);
    }

  bfd_elf32_arm_set_byteswap_code (&link_info, byteswap_code);

  /* Choose type of VFP11 erratum fix, or warn if specified fix is unnecessary
     due to architecture version.  */
  bfd_elf32_arm_set_vfp11_fix (output_bfd, &link_info);

  /* We should be able to set the size of the interworking stub section.  We
     can't do it until later if we have dynamic sections, though.  */
  if (! elf_hash_table (&link_info)->dynamic_sections_created)
    {
      /* Here we rummage through the found bfds to collect glue information.  */
      LANG_FOR_EACH_INPUT_STATEMENT (is)
	{
          /* Initialise mapping tables for code/data.  */
          bfd_elf32_arm_init_maps (is->the_bfd);

	  if (!bfd_elf32_arm_process_before_allocation (is->the_bfd,
							&link_info)
	      || !bfd_elf32_arm_vfp11_erratum_scan (is->the_bfd, &link_info))
	    /* xgettext:c-format */
	    einfo (_("Errors encountered processing file %s"), is->filename);
	}
    }

  /* Call the standard elf routine.  */
  gld${EMULATION_NAME}_before_allocation ();

  /* We have seen it all. Allocate it, and carry on.  */
  bfd_elf32_arm_allocate_interworking_sections (& link_info);
}

static void
arm_elf_after_allocation (void)
{
  /* Call the standard elf routine.  */
  after_allocation_default ();

  {
    LANG_FOR_EACH_INPUT_STATEMENT (is)
      {
        /* Figure out where VFP11 erratum veneers (and the labels returning
           from same) have been placed.  */
        bfd_elf32_arm_vfp11_fix_veneer_locations (is->the_bfd, &link_info);
      }
  }
}

static void
arm_elf_finish (void)
{
  struct bfd_link_hash_entry * h;

  /* Call the elf32.em routine.  */
  gld${EMULATION_NAME}_finish ();

  if (thumb_entry_symbol)
    {
      h = bfd_link_hash_lookup (link_info.hash, thumb_entry_symbol,
				FALSE, FALSE, TRUE);
    }
  else
    {
      struct elf_link_hash_entry * eh;

      if (!entry_symbol.name)
	return;

      h = bfd_link_hash_lookup (link_info.hash, entry_symbol.name,
				FALSE, FALSE, TRUE);
      eh = (struct elf_link_hash_entry *)h;
      if (!h || ELF_ST_TYPE(eh->type) != STT_ARM_TFUNC)
	return;
    }


  if (h != (struct bfd_link_hash_entry *) NULL
      && (h->type == bfd_link_hash_defined
	  || h->type == bfd_link_hash_defweak)
      && h->u.def.section->output_section != NULL)
    {
      static char buffer[32];
      bfd_vma val;

      /* Special procesing is required for a Thumb entry symbol.  The
	 bottom bit of its address must be set.  */
      val = (h->u.def.value
	     + bfd_get_section_vma (output_bfd,
				    h->u.def.section->output_section)
	     + h->u.def.section->output_offset);

      val |= 1;

      /* Now convert this value into a string and store it in entry_symbol
	 where the lang_finish() function will pick it up.  */
      buffer[0] = '0';
      buffer[1] = 'x';

      sprintf_vma (buffer + 2, val);

      if (thumb_entry_symbol != NULL && entry_symbol.name != NULL
	  && entry_from_cmdline)
	einfo (_("%P: warning: '--thumb-entry %s' is overriding '-e %s'\n"),
	       thumb_entry_symbol, entry_symbol.name);
      entry_symbol.name = buffer;
    }
  else
    einfo (_("%P: warning: connot find thumb start symbol %s\n"),
	   thumb_entry_symbol);
}

/* This is a convenient point to tell BFD about target specific flags.
   After the output has been created, but before inputs are read.  */
static void
arm_elf_create_output_section_statements (void)
{
  bfd_elf32_arm_set_target_relocs (output_bfd, &link_info, target1_is_rel,
				   target2_type, fix_v4bx, use_blx,
				   vfp11_denorm_fix, no_enum_size_warning,
				   pic_veneer);
}

EOF

# Define some shell vars to insert bits of code into the standard elf
# parse_args and list_options functions.
#
PARSE_AND_LIST_PROLOGUE='
#define OPTION_THUMB_ENTRY		301
#define OPTION_BE8			302
#define OPTION_TARGET1_REL		303
#define OPTION_TARGET1_ABS		304
#define OPTION_TARGET2			305
#define OPTION_FIX_V4BX			306
#define OPTION_USE_BLX			307
#define OPTION_VFP11_DENORM_FIX		308
#define OPTION_NO_ENUM_SIZE_WARNING	309
#define OPTION_PIC_VENEER		310
'

PARSE_AND_LIST_SHORTOPTS=p

PARSE_AND_LIST_LONGOPTS='
  { "no-pipeline-knowledge", no_argument, NULL, '\'p\''},
  { "thumb-entry", required_argument, NULL, OPTION_THUMB_ENTRY},
  { "be8", no_argument, NULL, OPTION_BE8},
  { "target1-rel", no_argument, NULL, OPTION_TARGET1_REL},
  { "target1-abs", no_argument, NULL, OPTION_TARGET1_ABS},
  { "target2", required_argument, NULL, OPTION_TARGET2},
  { "fix-v4bx", no_argument, NULL, OPTION_FIX_V4BX},
  { "use-blx", no_argument, NULL, OPTION_USE_BLX},
  { "vfp11-denorm-fix", required_argument, NULL, OPTION_VFP11_DENORM_FIX},
  { "no-enum-size-warning", no_argument, NULL, OPTION_NO_ENUM_SIZE_WARNING},
  { "pic-veneer", no_argument, NULL, OPTION_PIC_VENEER},
'

PARSE_AND_LIST_OPTIONS='
  fprintf (file, _("     --thumb-entry=<sym>      Set the entry point to be Thumb symbol <sym>\n"));
  fprintf (file, _("     --be8                    Oputput BE8 format image\n"));
  fprintf (file, _("     --target1=rel            Interpret R_ARM_TARGET1 as R_ARM_REL32\n"));
  fprintf (file, _("     --target1=abs            Interpret R_ARM_TARGET1 as R_ARM_ABS32\n"));
  fprintf (file, _("     --target2=<type>         Specify definition of R_ARM_TARGET2\n"));
  fprintf (file, _("     --fix-v4bx               Rewrite BX rn as MOV pc, rn for ARMv4\n"));
  fprintf (file, _("     --use-blx                Enable use of BLX instructions\n"));
  fprintf (file, _("     --vfp11-denorm-fix       Specify how to fix VFP11 denorm erratum\n"));
  fprintf (file, _("     --no-enum-size-warning   Don'\''t warn about objects with incompatible enum sizes\n"));
  fprintf (file, _("     --pic-veneer             Always generate PIC interworking veneers\n"));
'

PARSE_AND_LIST_ARGS_CASES='
    case '\'p\'':
      /* Only here for backwards compatibility.  */
      break;

    case OPTION_THUMB_ENTRY:
      thumb_entry_symbol = optarg;
      break;

    case OPTION_BE8:
      byteswap_code = 1;
      break;

    case OPTION_TARGET1_REL:
      target1_is_rel = 1;
      break;

    case OPTION_TARGET1_ABS:
      target1_is_rel = 0;
      break;

    case OPTION_TARGET2:
      target2_type = optarg;
      break;

    case OPTION_FIX_V4BX:
      fix_v4bx = 1;
      break;

    case OPTION_USE_BLX:
      use_blx = 1;
      break;
    
    case OPTION_VFP11_DENORM_FIX:
      if (strcmp (optarg, "none") == 0)
        vfp11_denorm_fix = BFD_ARM_VFP11_FIX_NONE;
      else if (strcmp (optarg, "scalar") == 0)
        vfp11_denorm_fix = BFD_ARM_VFP11_FIX_SCALAR;
      else if (strcmp (optarg, "vector") == 0)
        vfp11_denorm_fix = BFD_ARM_VFP11_FIX_VECTOR;
      else
        einfo (_("Unrecognized VFP11 fix type '\''%s'\''.\n"), optarg);
      break;

    case OPTION_NO_ENUM_SIZE_WARNING:
      no_enum_size_warning = 1;
      break;

    case OPTION_PIC_VENEER:
      pic_veneer = 1;
      break;
'

# We have our own after_open and before_allocation functions, but they call
# the standard routines, so give them a different name.
LDEMUL_AFTER_OPEN=arm_elf_after_open
LDEMUL_BEFORE_ALLOCATION=arm_elf_before_allocation
LDEMUL_AFTER_ALLOCATION=arm_elf_after_allocation
LDEMUL_CREATE_OUTPUT_SECTION_STATEMENTS=arm_elf_create_output_section_statements

# Replace the elf before_parse function with our own.
LDEMUL_BEFORE_PARSE=gld"${EMULATION_NAME}"_before_parse

# Call the extra arm-elf function
LDEMUL_FINISH=arm_elf_finish
