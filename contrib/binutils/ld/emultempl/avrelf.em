# This shell script emits a C file. -*- C -*-
#   Copyright 2006
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
# Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, 
# MA 02110-1301 USA.

# This file is sourced from elf32.em, and defines extra avr-elf
# specific routines.  It is used to generate the trampolines for the avr6
# family devices where one needs to address the issue that it is not possible
# to reach the whole program memory by using 16 bit pointers.

cat >>e${EMULATION_NAME}.c <<EOF

#include "elf32-avr.h"
#include "ldctor.h"

/* The fake file and it's corresponding section meant to hold 
   the linker stubs if needed.  */

static lang_input_statement_type *stub_file;
static asection *avr_stub_section;

/* Variables set by the command-line parameters and transfered
   to the bfd without use of global shared variables.  */

static bfd_boolean avr_no_stubs = FALSE;
static bfd_boolean avr_debug_relax = FALSE;
static bfd_boolean avr_debug_stubs = FALSE;
static bfd_boolean avr_replace_call_ret_sequences = TRUE;
static bfd_vma avr_pc_wrap_around = 0x10000000;

/* Transfers information to the bfd frontend.  */

static void
avr_elf_set_global_bfd_parameters (void)
{
  elf32_avr_setup_params (& link_info,
                          stub_file->the_bfd,
                          avr_stub_section,
                          avr_no_stubs,
                          avr_debug_stubs,
                          avr_debug_relax,
                          avr_pc_wrap_around,
                          avr_replace_call_ret_sequences);
}


/* Makes a conservative estimate of the trampoline section size that could
   be corrected later on.  */

static void
avr_elf_${EMULATION_NAME}_before_allocation (void)
{
  int ret;

  gld${EMULATION_NAME}_before_allocation ();

  /* We only need stubs for the avr6 family.  */
  if (strcmp ("${EMULATION_NAME}","avr6"))
    avr_no_stubs = TRUE;

  avr_elf_set_global_bfd_parameters ();

  /* If generating a relocatable output file, then
     we don't  have to generate the trampolines.  */
  if (link_info.relocatable)
    avr_no_stubs = TRUE;

  if (avr_no_stubs)
    return;

  ret = elf32_avr_setup_section_lists (output_bfd, &link_info);

  if (ret < 0)
    einfo ("%X%P: can not setup the input section list: %E\n");

  if (ret <= 0)
    return;

  /* Call into the BFD backend to do the real "stub"-work.  */
  if (! elf32_avr_size_stubs (output_bfd, &link_info, TRUE))
    einfo ("%X%P: can not size stub section: %E\n");
}

/* This is called before the input files are opened.  We create a new
   fake input file to hold the stub section and generate the section itself.  */

static void
avr_elf_create_output_section_statements (void)
{
  flagword flags;

  stub_file = lang_add_input_file ("linker stubs",
                                   lang_input_file_is_fake_enum,
                                   NULL);

  stub_file->the_bfd = bfd_create ("linker stubs", output_bfd);
  if (stub_file->the_bfd == NULL
      || !bfd_set_arch_mach (stub_file->the_bfd,
                             bfd_get_arch (output_bfd),
                             bfd_get_mach (output_bfd)))
    {
      einfo ("%X%P: can not create stub BFD %E\n");
      return;
    }

  /* Now we add the stub section.  */

  avr_stub_section = bfd_make_section_anyway (stub_file->the_bfd,
                                              ".trampolines");
  if (avr_stub_section == NULL)
    goto err_ret;
  
  flags = (SEC_ALLOC | SEC_LOAD | SEC_READONLY | SEC_CODE
           | SEC_HAS_CONTENTS | SEC_RELOC | SEC_IN_MEMORY | SEC_KEEP);
  if (!bfd_set_section_flags (stub_file->the_bfd, avr_stub_section, flags))
    goto err_ret;

  avr_stub_section->alignment_power = 1;
  
  ldlang_add_file (stub_file);

  return;

  err_ret:
   einfo ("%X%P: can not make stub section: %E\n");
   return;
}

/* Re-calculates the size of the stubs so that we won't waste space.  */

static void
avr_elf_finish (void)
{ 
  if (!avr_no_stubs)
    {
      /* Now build the linker stubs.  */
      if (stub_file->the_bfd->sections != NULL)
       {
         /* Call again the trampoline analyzer to initialize the trampoline
            stubs with the correct symbol addresses.  Since there could have
            been relaxation, the symbol addresses that were found during
            first call may no longer be correct.  */
         if (!elf32_avr_size_stubs (output_bfd, &link_info, FALSE))
           {
             einfo ("%X%P: can not size stub section: %E\n");
             return;
           }

         if (!elf32_avr_build_stubs (&link_info))
           einfo ("%X%P: can not build stubs: %E\n");
       }
    }

  gld${EMULATION_NAME}_finish ();
}


EOF


PARSE_AND_LIST_PROLOGUE='

#define OPTION_NO_CALL_RET_REPLACEMENT 301
#define OPTION_PMEM_WRAP_AROUND        302
#define OPTION_NO_STUBS                303
#define OPTION_DEBUG_STUBS             304
#define OPTION_DEBUG_RELAX             305
'

PARSE_AND_LIST_LONGOPTS='
  { "no-call-ret-replacement", no_argument, 
     NULL, OPTION_NO_CALL_RET_REPLACEMENT},
  { "pmem-wrap-around", required_argument, 
    NULL, OPTION_PMEM_WRAP_AROUND},
  { "no-stubs", no_argument, 
    NULL, OPTION_NO_STUBS},
  { "debug-stubs", no_argument, 
    NULL, OPTION_DEBUG_STUBS},
  { "debug-relax", no_argument, 
    NULL, OPTION_DEBUG_RELAX},
'

PARSE_AND_LIST_OPTIONS='
  fprintf (file, _("     --pmem-wrap-around=<val> "
                           "Make the linker relaxation machine assume that a\n"
                   "                              "
                           "program counter wrap-around occures at address\n"
                   "                              "
                           "<val>. Supported values are 8k, 16k, 32k and 64k.\n"));
  fprintf (file, _("     --no-call-ret-replacement "
                           "The relaxation machine normally will\n"
                   "                               "
                           "substitute two immediately following call/ret\n"
                   "                               "
                           "instructions by a single jump instruction.\n"
                   "                               "
                           "This option disables this optimization.\n"));
  fprintf (file, _("     --no-stubs "
                           "If the linker detects to attempt to access\n"
                   "                               "
                           "an instruction beyond 128k by a reloc that\n"
                   "                               "
                           "is limited to 128k max, it inserts a jump\n"
                   "                               "
                           "stub. You can de-active this with this switch.\n"));
  fprintf (file, _("     --debug-stubs Used for debugging avr-ld.\n"));
  fprintf (file, _("     --debug-relax Used for debugging avr-ld.\n"));
'

PARSE_AND_LIST_ARGS_CASES='

    case OPTION_PMEM_WRAP_AROUND:
      { 
        /* This variable is defined in the bfd library.  */
        if ((!strcmp (optarg,"32k"))      || (!strcmp (optarg,"32K")))
          avr_pc_wrap_around = 32768;
        else if ((!strcmp (optarg,"8k")) || (!strcmp (optarg,"8K")))
          avr_pc_wrap_around = 8192;
        else if ((!strcmp (optarg,"16k")) || (!strcmp (optarg,"16K")))
          avr_pc_wrap_around = 16384;
        else if ((!strcmp (optarg,"64k")) || (!strcmp (optarg,"64K")))
          avr_pc_wrap_around = 0x10000;
        else
          return FALSE;
      }
      break;

    case OPTION_DEBUG_STUBS:
      avr_debug_stubs = TRUE;
      break;

    case OPTION_DEBUG_RELAX:
      avr_debug_relax = TRUE;
      break;

    case OPTION_NO_STUBS:
      avr_no_stubs = TRUE;
      break;

    case OPTION_NO_CALL_RET_REPLACEMENT:
      {
        /* This variable is defined in the bfd library.  */
        avr_replace_call_ret_sequences = FALSE;
      }
      break;
'

#
# Put these extra avr-elf routines in ld_${EMULATION_NAME}_emulation
#
LDEMUL_BEFORE_ALLOCATION=avr_elf_${EMULATION_NAME}_before_allocation
LDEMUL_FINISH=avr_elf_finish
LDEMUL_CREATE_OUTPUT_SECTION_STATEMENTS=avr_elf_create_output_section_statements
