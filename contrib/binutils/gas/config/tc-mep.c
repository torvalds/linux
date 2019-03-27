/* tc-mep.c -- Assembler for the Toshiba Media Processor.
   Copyright (C) 2001, 2002, 2003, 2004, 2005 Free Software Foundation.

   This file is part of GAS, the GNU Assembler.

   GAS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GAS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with GAS; see the file COPYING.  If not, write to
   the Free Software Foundation, 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.  */

#include <stdio.h>
#include "as.h"
#include "dwarf2dbg.h"
#include "subsegs.h"
#include "symcat.h"
#include "opcodes/mep-desc.h"
#include "opcodes/mep-opc.h"
#include "cgen.h"
#include "elf/common.h"
#include "elf/mep.h"
#include "libbfd.h"
#include "xregex.h"

/* Structure to hold all of the different components describing
   an individual instruction.  */
typedef struct
{
  const CGEN_INSN *	insn;
  const CGEN_INSN *	orig_insn;
  CGEN_FIELDS		fields;
#if CGEN_INT_INSN_P
  CGEN_INSN_INT         buffer [1];
#define INSN_VALUE(buf) (*(buf))
#else
  unsigned char         buffer [CGEN_MAX_INSN_SIZE];
#define INSN_VALUE(buf) (buf)
#endif
  char *		addr;
  fragS *		frag;
  int                   num_fixups;
  fixS *                fixups [GAS_CGEN_MAX_FIXUPS];
  int                   indices [MAX_OPERAND_INSTANCES];
} mep_insn;

static int mode = CORE; /* Start in core mode. */
static int pluspresent = 0;
static int allow_disabled_registers = 0;
static int library_flag = 0;

/* We're going to need to store all of the instructions along with
   their fixups so that we can parallelization grouping rules. */

static mep_insn saved_insns[MAX_SAVED_FIXUP_CHAINS];
static int num_insns_saved = 0;

const char comment_chars[]        = "#";
const char line_comment_chars[]   = ";#";
const char line_separator_chars[] = ";";
const char EXP_CHARS[]            = "eE";
const char FLT_CHARS[]            = "dD";

static void mep_switch_to_vliw_mode (int);
static void mep_switch_to_core_mode (int);
static void mep_s_vtext (int);
static void mep_noregerr (int);

/* The target specific pseudo-ops which we support.  */
const pseudo_typeS md_pseudo_table[] =
{
  { "word",	cons,	                        4 },
  { "file",	(void (*) (int)) dwarf2_directive_file,   	0 },
  { "loc",	dwarf2_directive_loc,   	0 },
  { "vliw", 	mep_switch_to_vliw_mode,	0 },
  { "core", 	mep_switch_to_core_mode,	0 },
  { "vtext", 	mep_s_vtext,             	0 },
  { "noregerr",	mep_noregerr,	             	0 },
  { NULL, 	NULL,		        	0 }
};

/* Relocations against symbols are done in two
   parts, with a HI relocation and a LO relocation.  Each relocation
   has only 16 bits of space to store an addend.  This means that in
   order for the linker to handle carries correctly, it must be able
   to locate both the HI and the LO relocation.  This means that the
   relocations must appear in order in the relocation table.

   In order to implement this, we keep track of each unmatched HI
   relocation.  We then sort them so that they immediately precede the
   corresponding LO relocation. */

struct mep_hi_fixup
{
  struct mep_hi_fixup * next;	/* Next HI fixup.  */
  fixS * fixp;			/* This fixup.  */
  segT seg;			/* The section this fixup is in.  */
};

/* The list of unmatched HI relocs.  */
static struct mep_hi_fixup * mep_hi_fixup_list;


#define OPTION_EB		(OPTION_MD_BASE + 0)
#define OPTION_EL		(OPTION_MD_BASE + 1)
#define OPTION_CONFIG		(OPTION_MD_BASE + 2)
#define OPTION_AVERAGE		(OPTION_MD_BASE + 3)
#define OPTION_NOAVERAGE	(OPTION_MD_BASE + 4)
#define OPTION_MULT		(OPTION_MD_BASE + 5)
#define OPTION_NOMULT		(OPTION_MD_BASE + 6)
#define OPTION_DIV		(OPTION_MD_BASE + 7)
#define OPTION_NODIV		(OPTION_MD_BASE + 8)
#define OPTION_BITOPS		(OPTION_MD_BASE + 9)
#define OPTION_NOBITOPS		(OPTION_MD_BASE + 10)
#define OPTION_LEADZ		(OPTION_MD_BASE + 11)
#define OPTION_NOLEADZ		(OPTION_MD_BASE + 12)
#define OPTION_ABSDIFF		(OPTION_MD_BASE + 13)
#define OPTION_NOABSDIFF	(OPTION_MD_BASE + 14)
#define OPTION_MINMAX		(OPTION_MD_BASE + 15)
#define OPTION_NOMINMAX		(OPTION_MD_BASE + 16)
#define OPTION_CLIP		(OPTION_MD_BASE + 17)
#define OPTION_NOCLIP		(OPTION_MD_BASE + 18)
#define OPTION_SATUR		(OPTION_MD_BASE + 19)
#define OPTION_NOSATUR		(OPTION_MD_BASE + 20)
#define OPTION_COP32		(OPTION_MD_BASE + 21)
#define OPTION_REPEAT		(OPTION_MD_BASE + 25)
#define OPTION_NOREPEAT		(OPTION_MD_BASE + 26)
#define OPTION_DEBUG		(OPTION_MD_BASE + 27)
#define OPTION_NODEBUG		(OPTION_MD_BASE + 28)
#define OPTION_LIBRARY		(OPTION_MD_BASE + 29)

struct option md_longopts[] = {
  { "EB",          no_argument, NULL, OPTION_EB},
  { "EL",          no_argument, NULL, OPTION_EL},
  { "mconfig",     required_argument, NULL, OPTION_CONFIG},
  { "maverage",    no_argument, NULL, OPTION_AVERAGE},
  { "mno-average", no_argument, NULL, OPTION_NOAVERAGE},
  { "mmult",       no_argument, NULL, OPTION_MULT},
  { "mno-mult",    no_argument, NULL, OPTION_NOMULT},
  { "mdiv",        no_argument, NULL, OPTION_DIV},
  { "mno-div",     no_argument, NULL, OPTION_NODIV},
  { "mbitops",     no_argument, NULL, OPTION_BITOPS},
  { "mno-bitops",  no_argument, NULL, OPTION_NOBITOPS},
  { "mleadz",      no_argument, NULL, OPTION_LEADZ},
  { "mno-leadz",   no_argument, NULL, OPTION_NOLEADZ},
  { "mabsdiff",    no_argument, NULL, OPTION_ABSDIFF},
  { "mno-absdiff", no_argument, NULL, OPTION_NOABSDIFF},
  { "mminmax",     no_argument, NULL, OPTION_MINMAX},
  { "mno-minmax",  no_argument, NULL, OPTION_NOMINMAX},
  { "mclip",       no_argument, NULL, OPTION_CLIP},
  { "mno-clip",    no_argument, NULL, OPTION_NOCLIP},
  { "msatur",      no_argument, NULL, OPTION_SATUR},
  { "mno-satur",   no_argument, NULL, OPTION_NOSATUR},
  { "mcop32",	   no_argument, NULL, OPTION_COP32},
  { "mdebug",      no_argument, NULL, OPTION_DEBUG},
  { "mno-debug",   no_argument, NULL, OPTION_NODEBUG},
  { "mlibrary",    no_argument, NULL, OPTION_LIBRARY},
  { NULL, 0, NULL, 0 } };
size_t md_longopts_size = sizeof (md_longopts);

const char * md_shortopts = "";
static int optbits = 0;
static int optbitset = 0;

int
md_parse_option (int c, char *arg ATTRIBUTE_UNUSED)
{
  int i, idx;
  switch (c)
    {
    case OPTION_EB:
      target_big_endian = 1;
      break;
    case OPTION_EL:
      target_big_endian = 0;
      break;
    case OPTION_CONFIG:
      idx = 0;
      for (i=1; mep_config_map[i].name; i++)
	if (strcmp (mep_config_map[i].name, arg) == 0)
	  {
	    idx = i;
	    break;
	  }
      if (!idx)
	{
	  fprintf (stderr, "Error: unknown configuration %s\n", arg);
	  return 0;
	}
      mep_config_index = idx;
      target_big_endian = mep_config_map[idx].big_endian;
      break;
    case OPTION_AVERAGE:
      optbits |= 1 << CGEN_INSN_OPTIONAL_AVE_INSN;
      optbitset |= 1 << CGEN_INSN_OPTIONAL_AVE_INSN;
      break;
    case OPTION_NOAVERAGE:
      optbits &= ~(1 << CGEN_INSN_OPTIONAL_AVE_INSN);
      optbitset |= 1 << CGEN_INSN_OPTIONAL_AVE_INSN;
      break;
    case OPTION_MULT:
      optbits |= 1 << CGEN_INSN_OPTIONAL_MUL_INSN;
      optbitset |= 1 << CGEN_INSN_OPTIONAL_MUL_INSN;
      break;
    case OPTION_NOMULT:
      optbits &= ~(1 << CGEN_INSN_OPTIONAL_MUL_INSN);
      optbitset |= 1 << CGEN_INSN_OPTIONAL_MUL_INSN;
      break;
    case OPTION_DIV:
      optbits |= 1 << CGEN_INSN_OPTIONAL_DIV_INSN;
      optbitset |= 1 << CGEN_INSN_OPTIONAL_DIV_INSN;
      break;
    case OPTION_NODIV:
      optbits &= ~(1 << CGEN_INSN_OPTIONAL_DIV_INSN);
      optbitset |= 1 << CGEN_INSN_OPTIONAL_DIV_INSN;
      break;
    case OPTION_BITOPS:
      optbits |= 1 << CGEN_INSN_OPTIONAL_BIT_INSN;
      optbitset |= 1 << CGEN_INSN_OPTIONAL_BIT_INSN;
      break;
    case OPTION_NOBITOPS:
      optbits &= ~(1 << CGEN_INSN_OPTIONAL_BIT_INSN);
      optbitset |= 1 << CGEN_INSN_OPTIONAL_BIT_INSN;
      break;
    case OPTION_LEADZ:
      optbits |= 1 << CGEN_INSN_OPTIONAL_LDZ_INSN;
      optbitset |= 1 << CGEN_INSN_OPTIONAL_LDZ_INSN;
      break;
    case OPTION_NOLEADZ:
      optbits &= ~(1 << CGEN_INSN_OPTIONAL_LDZ_INSN);
      optbitset |= 1 << CGEN_INSN_OPTIONAL_LDZ_INSN;
      break;
    case OPTION_ABSDIFF:
      optbits |= 1 << CGEN_INSN_OPTIONAL_ABS_INSN;
      optbitset |= 1 << CGEN_INSN_OPTIONAL_ABS_INSN;
      break;
    case OPTION_NOABSDIFF:
      optbits &= ~(1 << CGEN_INSN_OPTIONAL_ABS_INSN);
      optbitset |= 1 << CGEN_INSN_OPTIONAL_ABS_INSN;
      break;
    case OPTION_MINMAX:
      optbits |= 1 << CGEN_INSN_OPTIONAL_MINMAX_INSN;
      optbitset |= 1 << CGEN_INSN_OPTIONAL_MINMAX_INSN;
      break;
    case OPTION_NOMINMAX:
      optbits &= ~(1 << CGEN_INSN_OPTIONAL_MINMAX_INSN);
      optbitset |= 1 << CGEN_INSN_OPTIONAL_MINMAX_INSN;
      break;
    case OPTION_CLIP:
      optbits |= 1 << CGEN_INSN_OPTIONAL_CLIP_INSN;
      optbitset |= 1 << CGEN_INSN_OPTIONAL_CLIP_INSN;
      break;
    case OPTION_NOCLIP:
      optbits &= ~(1 << CGEN_INSN_OPTIONAL_CLIP_INSN);
      optbitset |= 1 << CGEN_INSN_OPTIONAL_CLIP_INSN;
      break;
    case OPTION_SATUR:
      optbits |= 1 << CGEN_INSN_OPTIONAL_SAT_INSN;
      optbitset |= 1 << CGEN_INSN_OPTIONAL_SAT_INSN;
      break;
    case OPTION_NOSATUR:
      optbits &= ~(1 << CGEN_INSN_OPTIONAL_SAT_INSN);
      optbitset |= 1 << CGEN_INSN_OPTIONAL_SAT_INSN;
      break;
    case OPTION_COP32:
      optbits |= 1 << CGEN_INSN_OPTIONAL_CP_INSN;
      optbitset |= 1 << CGEN_INSN_OPTIONAL_CP_INSN;
      break;
    case OPTION_DEBUG:
      optbits |= 1 << CGEN_INSN_OPTIONAL_DEBUG_INSN;
      optbitset |= 1 << CGEN_INSN_OPTIONAL_DEBUG_INSN;
      break;
    case OPTION_NODEBUG:
      optbits &= ~(1 << CGEN_INSN_OPTIONAL_DEBUG_INSN);
      optbitset |= 1 << CGEN_INSN_OPTIONAL_DEBUG_INSN;
      break;
    case OPTION_LIBRARY:
      library_flag = EF_MEP_LIBRARY;
      break;
    case OPTION_REPEAT:
    case OPTION_NOREPEAT:
      break;
    default:
      return 0;
    }
  return 1;
}

void
md_show_usage (FILE *stream)
{
  fprintf (stream, _("MeP specific command line options:\n\
  -EB                     assemble for a big endian system (default)\n\
  -EL                     assemble for a little endian system\n\
  -mconfig=<name>         specify a chip configuration to use\n\
  -maverage -mno-average -mmult -mno-mult -mdiv -mno-div\n\
  -mbitops -mno-bitops -mleadz -mno-leadz -mabsdiff -mno-absdiff\n\
  -mminmax -mno-minmax -mclip -mno-clip -msatur -mno-satur -mcop32\n\
                          enable/disable the given opcodes\n\
\n\
  If -mconfig is given, the other -m options modify it.  Otherwise,\n\
  if no -m options are given, all core opcodes are enabled;\n\
  if any enabling -m options are given, only those are enabled;\n\
  if only disabling -m options are given, only those are disabled.\n\
"));
  if (mep_config_map[1].name)
    {
      int i;
      fprintf (stream, "  -mconfig=STR            specify the configuration to use\n");
      fprintf (stream, "  Configurations:");
      for (i=0; mep_config_map[i].name; i++)
	fprintf (stream, " %s", mep_config_map[i].name);
      fprintf (stream, "\n");
    }
}



static void
mep_check_for_disabled_registers (mep_insn *insn)
{
  static int initted = 0;
  static int has_mul_div = 0;
  static int has_cop = 0;
  static int has_debug = 0;
  unsigned int b, r;

  if (allow_disabled_registers)
    return;

#if !CGEN_INT_INSN_P
  if (target_big_endian)
    b = insn->buffer[0] * 256 + insn->buffer[1];
  else
    b = insn->buffer[1] * 256 + insn->buffer[0];
#else
  b = insn->buffer[0];
#endif

  if ((b & 0xfffff00e) == 0x7008 /* stc */
      || (b & 0xfffff00e) == 0x700a /* ldc */)
    {
      if (!initted)
	{
	  initted = 1;
	  if ((MEP_OMASK & (1 << CGEN_INSN_OPTIONAL_MUL_INSN))
	      || (MEP_OMASK & (1 << CGEN_INSN_OPTIONAL_DIV_INSN)))
	    has_mul_div = 1;
	  if (MEP_OMASK & (1 << CGEN_INSN_OPTIONAL_DEBUG_INSN))
	    has_debug = 1;
	  if (MEP_OMASK & (1 << CGEN_INSN_OPTIONAL_CP_INSN))
	    has_cop = 1;
	}

      r = ((b & 0x00f0) >> 4) | ((b & 0x0001) << 4);
      switch (r)
	{
	case 7: /* $hi */
	case 8: /* $lo */
	  if (!has_mul_div)
	    as_bad ("$hi and $lo are disabled when MUL and DIV are off");
	  break;
	case 12: /* $mb0 */
	case 13: /* $me0 */
	case 14: /* $mb1 */
	case 15: /* $me1 */
	  if (!has_cop)
	    as_bad ("$mb0, $me0, $mb1, and $me1 are disabled when COP is off");
	  break;
	case 24: /* $dbg */
	case 25: /* $depc */
	  if (!has_debug)
	    as_bad ("$dbg and $depc are disabled when DEBUG is off");
	  break;
	}
    }
}

static int
mep_machine (void)
{
  switch (MEP_CPU)
    {
    default: break;
    case EF_MEP_CPU_C2: return bfd_mach_mep;
    case EF_MEP_CPU_C3: return bfd_mach_mep;
    case EF_MEP_CPU_C4: return bfd_mach_mep;
    case EF_MEP_CPU_H1: return bfd_mach_mep_h1;
    }

  return bfd_mach_mep;
}

/* The MeP version of the cgen parse_operand function.  The only difference
   from the standard version is that we want to avoid treating '$foo' and
   '($foo...)' as references to a symbol called '$foo'.  The chances are
   that '$foo' is really a misspelt register.  */

static const char *
mep_parse_operand (CGEN_CPU_DESC cd, enum cgen_parse_operand_type want,
		   const char **strP, int opindex, int opinfo,
		   enum cgen_parse_operand_result *resultP, bfd_vma *valueP)
{
  if (want == CGEN_PARSE_OPERAND_INTEGER || want == CGEN_PARSE_OPERAND_ADDRESS)
    {
      const char *next;

      next = *strP;
      while (*next == '(')
	next++;
      if (*next == '$')
	return "Not a valid literal";
    }
  return gas_cgen_parse_operand (cd, want, strP, opindex, opinfo,
				 resultP, valueP);
}

void
md_begin ()
{
  /* Initialize the `cgen' interface.  */

  /* If the user specifies no options, we default to allowing
     everything.  If the user specifies any enabling options, we
     default to allowing only what is specified.  If the user
     specifies only disabling options, we only disable what is
     specified.  If the user specifies options and a config, the
     options modify the config.  */
  if (optbits && mep_config_index == 0)
    MEP_OMASK = optbits;
  else
    MEP_OMASK = (MEP_OMASK & ~optbitset) | optbits;

  /* Set the machine number and endian.  */
  gas_cgen_cpu_desc = mep_cgen_cpu_open (CGEN_CPU_OPEN_MACHS, 0,
					 CGEN_CPU_OPEN_ENDIAN,
					 target_big_endian
					 ? CGEN_ENDIAN_BIG
					 : CGEN_ENDIAN_LITTLE,
					 CGEN_CPU_OPEN_ISAS, 0,
					 CGEN_CPU_OPEN_END);
  mep_cgen_init_asm (gas_cgen_cpu_desc);

  /* This is a callback from cgen to gas to parse operands.  */
  cgen_set_parse_operand_fn (gas_cgen_cpu_desc, mep_parse_operand);

  /* Identify the architecture.  */
  bfd_default_set_arch_mach (stdoutput, bfd_arch_mep, mep_machine ());

  /* Store the configuration number and core.  */
  bfd_set_private_flags (stdoutput, MEP_CPU | MEP_CONFIG | library_flag);

  /* Initialize the array we'll be using to store fixups.  */
  gas_cgen_initialize_saved_fixups_array();
}

/* Variant of mep_cgen_assemble_insn.  Assemble insn STR of cpu CD as a 
   coprocessor instruction, if possible, into FIELDS, BUF, and INSN.  */

static const CGEN_INSN *
mep_cgen_assemble_cop_insn (CGEN_CPU_DESC cd,
			    const char *str,
			    CGEN_FIELDS *fields,
			    CGEN_INSN_BYTES_PTR buf,
			    const struct cgen_insn *pinsn)
{
  const char *start;
  CGEN_INSN_LIST *ilist;
  const char *errmsg = NULL;

  /* The instructions are stored in hashed lists. */
  ilist = CGEN_ASM_LOOKUP_INSN (gas_cgen_cpu_desc, 
				CGEN_INSN_MNEMONIC (pinsn));

  start = str;
  for ( ; ilist != NULL ; ilist = CGEN_ASM_NEXT_INSN (ilist))
    {
      const CGEN_INSN *insn = ilist->insn;
      if (strcmp (CGEN_INSN_MNEMONIC (ilist->insn), 
		  CGEN_INSN_MNEMONIC (pinsn)) == 0
	  && MEP_INSN_COP_P (ilist->insn)
	  && mep_cgen_insn_supported (cd, insn))
	{
	  str = start;

	  /* skip this insn if str doesn't look right lexically */
	  if (CGEN_INSN_RX (insn) != NULL &&
	      regexec ((regex_t *) CGEN_INSN_RX (insn), str, 0, NULL, 0) == REG_NOMATCH)
	    continue;

	  /* Allow parse/insert handlers to obtain length of insn.  */
	  CGEN_FIELDS_BITSIZE (fields) = CGEN_INSN_BITSIZE (insn);

	  errmsg = CGEN_PARSE_FN (cd, insn) (cd, insn, & str, fields);
	  if (errmsg != NULL)
	    continue;
	  
	  errmsg = CGEN_INSERT_FN (cd, insn) (cd, insn, fields, buf,
					      (bfd_vma) 0);
	  if (errmsg != NULL)
	    continue;

	  return insn;
	}
    }
  return pinsn;
}

static void
mep_save_insn (mep_insn insn)
{
  /* Consider change MAX_SAVED_FIXUP_CHAINS to MAX_PARALLEL_INSNS. */
  if (num_insns_saved < 0 || num_insns_saved >= MAX_SAVED_FIXUP_CHAINS)
    {
      as_fatal("index into saved_insns[] out of bounds.");
      return;
    }
  saved_insns[num_insns_saved] = insn;
  gas_cgen_save_fixups(num_insns_saved);
  num_insns_saved++;
}

static void
mep_check_parallel32_scheduling (void)
{
  int insn0iscopro, insn1iscopro, insn0length, insn1length;

  /* More than two instructions means that either someone is referring to
     an internally parallel core or an internally parallel coprocessor,
     neither of which are supported at this time.  */
  if ( num_insns_saved > 2 )
    as_fatal("Internally paralled cores and coprocessors not supported.");

  /* If there are no insns saved, that's ok.  Just return.  This will
     happen when mep_process_saved_insns is called when the end of the
     source file is reached and there are no insns left to be processed.  */
  if (num_insns_saved == 0)
    return;

  /* Check some of the attributes of the first insn.  */
  insn0iscopro = MEP_INSN_COP_P (saved_insns[0].insn);
  insn0length = CGEN_FIELDS_BITSIZE (& saved_insns[0].fields);

  if (num_insns_saved == 2)
    {
      /* Check some of the attributes of the first insn.  */
      insn1iscopro = MEP_INSN_COP_P (saved_insns[1].insn);
      insn1length = CGEN_FIELDS_BITSIZE (& saved_insns[1].fields);

      if ((insn0iscopro && !insn1iscopro)
          || (insn1iscopro && !insn0iscopro))
	{
          /* We have one core and one copro insn.  If their sizes
             add up to 32, then the combination is valid.  */
	  if (insn0length + insn1length == 32)
	    return;
          else
	    as_bad ("core and copro insn lengths must total 32 bits.");
	}
      else
        as_bad ("vliw group must consist of 1 core and 1 copro insn."); 
    }
  else
    {
      /* If we arrive here, we have one saved instruction.  There are a
	 number of possible cases:

	 1.  The instruction is a 32 bit core or coprocessor insn and
             can be executed by itself.  Valid.

         2.  The instrucion is a core instruction for which a cop nop
             exists.  In this case, insert the cop nop into the saved
             insn array after the core insn and return.  Valid.

         3.  The instruction is a coprocessor insn for which a core nop
             exists.  In this case, move the coprocessor insn to the
             second element of the array and put the nop in the first
	     element then return.  Valid.

         4. The instruction is a core or coprocessor instruction for
            which there is no matching coprocessor or core nop to use
	    to form a valid vliw insn combination.  In this case, we
	    we have to abort.  */

      if (insn0length > 32)
	as_fatal ("Cannot use 48- or 64-bit insns with a 32 bit datapath.");

      if (insn0length == 32)
	return;

      /* Insn is smaller than datapath.  If there are no matching
         nops for this insn, then terminate assembly.  */
      if (CGEN_INSN_ATTR_VALUE (saved_insns[0].insn,
                                CGEN_INSN_VLIW32_NO_MATCHING_NOP))
	as_fatal ("No valid nop.");

      /* At this point we know that we have a single 16-bit insn that has 
	 a matching nop.  We have to assemble it and put it into the saved 
         insn and fixup chain arrays. */

      if (insn0iscopro)
	{
          char *errmsg;
          mep_insn insn;
         
          /* Move the insn and it's fixups to the second element of the
             saved insns arrary and insert a 16 bit core nope into the
             first element. */
             insn.insn = mep_cgen_assemble_insn (gas_cgen_cpu_desc, "nop",
                                                 &insn.fields, insn.buffer,
                                                 &errmsg);
             if (!insn.insn)
               {
                 as_bad ("%s", errmsg);
                 return;
               }

             /* Move the insn in element 0 to element 1 and insert the
                 nop into element 0.  Move the fixups in element 0 to
                 element 1 and save the current fixups to element 0.  
                 Really there aren't any fixups at this point because we're
                 inserting a nop but we might as well be general so that
                 if there's ever a need to insert a general insn, we'll
                 have an example. */
              saved_insns[1] = saved_insns[0];
              saved_insns[0] = insn;
              num_insns_saved++;
              gas_cgen_swap_fixups (0);
              gas_cgen_save_fixups (1);
	}
      else
	{
          char * errmsg;
          mep_insn insn;
	  int insn_num = saved_insns[0].insn->base->num;

	  /* Use 32 bit branches and skip the nop.  */
	  if (insn_num == MEP_INSN_BSR12
	      || insn_num == MEP_INSN_BEQZ
	      || insn_num == MEP_INSN_BNEZ)
	    return;

          /* Insert a 16-bit coprocessor nop.  Note that at the time */
          /* this was done, no 16-bit coprocessor nop was defined.   */
	  insn.insn = mep_cgen_assemble_insn (gas_cgen_cpu_desc, "cpnop16",
					      &insn.fields, insn.buffer,
					      &errmsg);
          if (!insn.insn)
            {
              as_bad ("%s", errmsg);
              return;
            }

          /* Now put the insn and fixups into the arrays.  */
          mep_save_insn (insn);
	}
    }
}

static void
mep_check_parallel64_scheduling (void)
{
  int insn0iscopro, insn1iscopro, insn0length, insn1length;

  /* More than two instructions means that someone is referring to an
     internally parallel core or an internally parallel coprocessor.  */
  /* These are not currently supported.  */
  if (num_insns_saved > 2)
    as_fatal ("Internally parallel cores of coprocessors not supported.");

  /* If there are no insns saved, that's ok.  Just return.  This will
     happen when mep_process_saved_insns is called when the end of the
     source file is reached and there are no insns left to be processed.  */
  if (num_insns_saved == 0)
    return;

  /* Check some of the attributes of the first insn.  */
  insn0iscopro = MEP_INSN_COP_P (saved_insns[0].insn);
  insn0length = CGEN_FIELDS_BITSIZE (& saved_insns[0].fields);

  if (num_insns_saved == 2)
    {
      /* Check some of the attributes of the first insn. */
      insn1iscopro = MEP_INSN_COP_P (saved_insns[1].insn);
      insn1length = CGEN_FIELDS_BITSIZE (& saved_insns[1].fields);

      if ((insn0iscopro && !insn1iscopro)
	  || (insn1iscopro && !insn0iscopro))
	{
	  /* We have one core and one copro insn.  If their sizes
	     add up to 64, then the combination is valid.  */
	  if (insn0length + insn1length == 64)
            return;
	  else
            as_bad ("core and copro insn lengths must total 64 bits.");
	}
      else
        as_bad ("vliw group must consist of 1 core and 1 copro insn.");
    }
  else
    {
      /* If we arrive here, we have one saved instruction.  There are a
	 number of possible cases:

         1.  The instruction is a 64 bit coprocessor insn and can be
             executed by itself.  Valid.

         2.  The instrucion is a core instruction for which a cop nop
             exists.  In this case, insert the cop nop into the saved
             insn array after the core insn and return.  Valid.

         3.  The instruction is a coprocessor insn for which a core nop
             exists.  In this case, move the coprocessor insn to the
             second element of the array and put the nop in the first
             element then return.  Valid.

         4.  The instruction is a core or coprocessor instruction for
             which there is no matching coprocessor or core nop to use
             to form a valid vliw insn combination.  In this case, we
	     we have to abort.  */

      /* If the insn is 64 bits long, it can run alone.  The size check
	 is done indepependantly of whether the insn is core or copro
	 in case 64 bit coprocessor insns are added later.  */
      if (insn0length == 64)
        return;

      /* Insn is smaller than datapath.  If there are no matching
	 nops for this insn, then terminate assembly.  */
      if (CGEN_INSN_ATTR_VALUE (saved_insns[0].insn,
				CGEN_INSN_VLIW64_NO_MATCHING_NOP))
	as_fatal ("No valid nop.");

      if (insn0iscopro)
	{
	  char *errmsg;
	  mep_insn insn;
          int i;

          /* Initialize the insn buffer.  */
          for (i = 0; i < 64; i++)
             insn.buffer[i] = '\0';

	  /* We have a coprocessor insn.  At this point in time there
	     are is 32-bit core nop.  There is only a 16-bit core
	     nop.  The idea is to allow for a relatively arbitrary
	     coprocessor to be specified.  We aren't looking at
	     trying to cover future changes in the core at this time
	     since it is assumed that the core will remain fairly
	     static.  If there ever are 32 or 48 bit core nops added,
	     they will require entries below.  */

	  if (insn0length == 48)
	    {
	      /* Move the insn and fixups to the second element of the
		 arrays then assemble and insert a 16 bit core nop.  */
	      insn.insn = mep_cgen_assemble_insn (gas_cgen_cpu_desc, "nop",
						  & insn.fields, insn.buffer,
						  & errmsg);
	    }
          else
            {
              /* If this is reached, then we have a single coprocessor
                 insn that is not 48 bits long, but for which the assembler
                 thinks there is a matching core nop.  If a 32-bit core
                 nop has been added, then make the necessary changes and
                 handle its assembly and insertion here.  Otherwise,
                 go figure out why either:
              
                 1. The assembler thinks that there is a 32-bit core nop
                    to match a 32-bit coprocessor insn, or
                 2. The assembler thinks that there is a 48-bit core nop
                    to match a 16-bit coprocessor insn.  */

              as_fatal ("Assembler expects a non-existent core nop.");
            }

	 if (!insn.insn)
	   {
	     as_bad ("%s", errmsg);
	     return;
	   }

         /* Move the insn in element 0 to element 1 and insert the
            nop into element 0.  Move the fixups in element 0 to
            element 1 and save the current fixups to element 0. 
	    Really there aren't any fixups at this point because we're
	    inserting a nop but we might as well be general so that
	    if there's ever a need to insert a general insn, we'll
	    have an example. */

         saved_insns[1] = saved_insns[0];
         saved_insns[0] = insn;
         num_insns_saved++;
         gas_cgen_swap_fixups(0);
         gas_cgen_save_fixups(1);

	}
      else
	{
	  char * errmsg;
	  mep_insn insn;
          int i;

          /* Initialize the insn buffer */
          for (i = 0; i < 64; i++)
             insn.buffer[i] = '\0';

	  /* We have a core insn.  We have to handle all possible nop
	     lengths.  If a coprocessor doesn't have a nop of a certain
	     length but there exists core insns that when combined with
	      a nop of that length would fill the datapath, those core
	      insns will be flagged with the VLIW_NO_CORRESPONDING_NOP
	      attribute.  That will ensure that when used in a way that
	      requires a nop to be inserted, assembly will terminate
	      before reaching this section of code.  This guarantees
	      that cases below which would result in the attempted
	      insertion of nop that doesn't exist will never be entered.  */
	  if (insn0length == 16)
	    {
	      /* Insert 48 bit coprocessor nop.          */
	      /* Assemble it and put it into the arrays. */
	      insn.insn = mep_cgen_assemble_insn (gas_cgen_cpu_desc, "cpnop48",
						  &insn.fields, insn.buffer,
						  &errmsg);
	    }
	  else if (insn0length == 32)
	    {
	      /* Insert 32 bit coprocessor nop. */
	      insn.insn = mep_cgen_assemble_insn (gas_cgen_cpu_desc, "cpnop32",
						  &insn.fields, insn.buffer,
						  &errmsg);
	    }
	  else if (insn0length == 48)
	    {
	      /* Insert 16 bit coprocessor nop. */
	      insn.insn = mep_cgen_assemble_insn (gas_cgen_cpu_desc, "cpnop16",
						  &insn.fields, insn.buffer,
						  &errmsg);
	    }
	  else
	    /* Core insn has an invalid length.  Something has gone wrong. */
	    as_fatal ("Core insn has invalid length!  Something is wrong!");

	  if (!insn.insn)
	    {
	      as_bad ("%s", errmsg);
	      return;
	    }

	  /* Now put the insn and fixups into the arrays.  */
	  mep_save_insn (insn);
	}
    }
}

/* The scheduling functions are just filters for invalid combinations.
   If there is a violation, they terminate assembly.  Otherise they
   just fall through.  Succesful combinations cause no side effects
   other than valid nop insertion.  */

static void
mep_check_parallel_scheduling (void)
{
  /* This is where we will eventually read the config information
     and choose which scheduling checking function to call.  */   
  if (MEP_VLIW64)
    mep_check_parallel64_scheduling ();
  else
    mep_check_parallel32_scheduling ();
}

static void
mep_process_saved_insns (void)
{
  int i;

  gas_cgen_save_fixups (MAX_SAVED_FIXUP_CHAINS - 1);

  /* We have to check for valid scheduling here. */
  mep_check_parallel_scheduling ();

  /* If the last call didn't cause assembly to terminate, we have
     a valid vliw insn/insn pair saved. Restore this instructions'
     fixups and process the insns. */
  for (i = 0;i<num_insns_saved;i++)
    {
      gas_cgen_restore_fixups (i);
      gas_cgen_finish_insn (saved_insns[i].insn, saved_insns[i].buffer,
			    CGEN_FIELDS_BITSIZE (& saved_insns[i].fields),
			    1, NULL);
    }
  gas_cgen_restore_fixups (MAX_SAVED_FIXUP_CHAINS - 1);

  /* Clear the fixups and reset the number insn saved to 0. */
  gas_cgen_initialize_saved_fixups_array ();
  num_insns_saved = 0;
  listing_prev_line ();
}

void
md_assemble (char * str)
{
  static CGEN_BITSET* isas = NULL;
  char * errmsg;

  /* Initialize GAS's cgen interface for a new instruction.  */
  gas_cgen_init_parse ();

  /* There are two possible modes: core and vliw.  We have to assemble
     differently for each.

     Core Mode:  We assemble normally.  All instructions are on a
                 single line and are made up of one mnemonic and one
                 set of operands.
     VLIW Mode:  Vliw combinations are indicated as follows:

		       core insn
		     + copro insn

                 We want to handle the general case where more than
                 one instruction can be preceeded by a +.  This will
                 happen later if we add support for internally parallel
                 coprocessors.  We'll make the parsing nice and general
                 so that it can handle an arbitrary number of insns
                 with leading +'s.  The actual checking for valid
                 combinations is done elsewhere.  */

  /* Initialize the isa to refer to the core.  */
  if (isas == NULL)
    isas = cgen_bitset_copy (& MEP_CORE_ISA);
  else
    {
      cgen_bitset_clear (isas);
      cgen_bitset_union (isas, & MEP_CORE_ISA, isas);
    }
  gas_cgen_cpu_desc->isas = isas;

  if (mode == VLIW)
    {
      /* VLIW mode.  */

      int thisInsnIsCopro = 0;
      mep_insn insn;
      int i;
      
      /* Initialize the insn buffer */
	
      if (! CGEN_INT_INSN_P)
         for (i=0; i < CGEN_MAX_INSN_SIZE; i++)
            insn.buffer[i]='\0';

      /* Can't tell core / copro insns apart at parse time! */
      cgen_bitset_union (isas, & MEP_COP_ISA, isas);

      /* Assemble the insn so we can examine its attributes. */
      insn.insn = mep_cgen_assemble_insn (gas_cgen_cpu_desc, str,
					  &insn.fields, insn.buffer,
					  &errmsg);
      if (!insn.insn)
	{
	  as_bad ("%s", errmsg);
	  return;
	}
      mep_check_for_disabled_registers (&insn);

      /* Check to see if it's a coprocessor instruction. */
      thisInsnIsCopro = MEP_INSN_COP_P (insn.insn);

      if (!thisInsnIsCopro)
	{
	  insn.insn = mep_cgen_assemble_cop_insn (gas_cgen_cpu_desc, str,
						  &insn.fields, insn.buffer,
						  insn.insn);
	  thisInsnIsCopro = MEP_INSN_COP_P (insn.insn);
	  mep_check_for_disabled_registers (&insn);
	}

      if (pluspresent)
	{
	  /* A plus was present. */
	  /* Check for a + with a core insn and abort if found. */
	  if (!thisInsnIsCopro)
	    {
	      as_fatal("A core insn cannot be preceeded by a +.\n");
	      return;
	    }

	  if (num_insns_saved > 0)
	    {
	      /* There are insns in the queue. Add this one. */
	      mep_save_insn (insn);
	    }
	  else
	    {
	      /* There are no insns in the queue and a plus is present.
		 This is a syntax error.  Let's not tolerate this.
		 We can relax this later if necessary.  */
	      as_bad (_("Invalid use of parallelization operator."));
	      return;
	    }
	}
      else
	{
	  /* No plus was present. */
	  if (num_insns_saved > 0)
	    {
	      /* There are insns saved and we came across an insn without a
		 leading +.  That's the signal to process the saved insns
		 before proceeding then treat the current insn as the first
		 in a new vliw group.  */
	      mep_process_saved_insns ();
	      num_insns_saved = 0;
	      /* mep_save_insn (insn); */
	    }
	  mep_save_insn (insn);
#if 0
	  else
	    {

              /* Core Insn. Add it to the beginning of the queue. */
              mep_save_insn (insn);
	      /* gas_cgen_save_fixups(num_insns_saved); */
	    }
#endif
	}

      pluspresent = 0;
    }
  else
    {
      /* Core mode.  */

      /* Only single instructions are assembled in core mode. */
      mep_insn insn;

      /* If a leading '+' was present, issue an error.
	 That's not allowed in core mode. */
      if (pluspresent)
	{
	  as_bad (_("Leading plus sign not allowed in core mode"));
	  return;
	}

      insn.insn = mep_cgen_assemble_insn
	(gas_cgen_cpu_desc, str, & insn.fields, insn.buffer, & errmsg);

      if (!insn.insn)
	{
	  as_bad ("%s", errmsg);
	  return;
	}
      gas_cgen_finish_insn (insn.insn, insn.buffer,
			    CGEN_FIELDS_BITSIZE (& insn.fields), 1, NULL);
      mep_check_for_disabled_registers (&insn);
    }
}

valueT
md_section_align (segT segment, valueT size)
{
  int align = bfd_get_section_alignment (stdoutput, segment);
  return ((size + (1 << align) - 1) & (-1 << align));
}


symbolS *
md_undefined_symbol (char *name ATTRIBUTE_UNUSED)
{
  return 0;
}

/* Interface to relax_segment.  */


const relax_typeS md_relax_table[] =
{
  /* The fields are:
     1) most positive reach of this state,
     2) most negative reach of this state,
     3) how many bytes this mode will have in the variable part of the frag
     4) which index into the table to try if we can't fit into this one.  */
  /* Note that we use "beq" because "jmp" has a peculiarity - it cannot
     jump to addresses with any bits 27..24 set.  So, we use beq as a
     17-bit pc-relative branch to avoid using jmp, just in case.  */

  /* 0 */ {     0,      0, 0, 0 }, /* unused */
  /* 1 */ {     0,      0, 0, 0 }, /* marker for "don't know yet" */

  /* 2 */ {  2047,  -2048, 0, 3 }, /* bsr12 */
  /* 3 */ {     0,      0, 2, 0 }, /* bsr16 */

  /* 4 */ {  2047,  -2048, 0, 5 }, /* bra */
  /* 5 */ { 65535, -65536, 2, 6 }, /* beq $0,$0 */
  /* 6 */ {     0,      0, 2, 0 }, /* jmp24 */

  /* 7 */ { 65535, -65536, 0, 8 }, /* beqi */
  /* 8 */ {     0,      0, 4, 0 }, /* bnei/jmp */

  /* 9 */  {   127,   -128, 0, 10 }, /* beqz */
  /* 10 */ { 65535, -65536, 2, 11 }, /* beqi */
  /* 11 */ {     0,      0, 4,  0 }, /* bnei/jmp */

  /* 12 */ { 65535, -65536, 0, 13 }, /* bnei */
  /* 13 */ {     0,      0, 4,  0 }, /* beqi/jmp */

  /* 14 */ {   127,   -128, 0, 15 }, /* bnez */
  /* 15 */ { 65535, -65536, 2, 16 }, /* bnei */
  /* 16 */ {     0,      0, 4,  0 },  /* beqi/jmp */

  /* 17 */ { 65535, -65536, 0, 13 }, /* bgei */
  /* 18 */ {     0,      0, 4,  0 },
  /* 19 */ { 65535, -65536, 0, 13 }, /* blti */
  /* 20 */ {     0,      0, 4,  0 },
  /* 19 */ { 65535, -65536, 0, 13 }, /* bcpeq */
  /* 20 */ {     0,      0, 4,  0 },
  /* 19 */ { 65535, -65536, 0, 13 }, /* bcpne */
  /* 20 */ {     0,      0, 4,  0 },
  /* 19 */ { 65535, -65536, 0, 13 }, /* bcpat */
  /* 20 */ {     0,      0, 4,  0 },
  /* 19 */ { 65535, -65536, 0, 13 }, /* bcpaf */
  /* 20 */ {     0,      0, 4,  0 }
};

/* Pseudo-values for 64 bit "insns" which are combinations of two 32
   bit insns.  */
typedef enum {
  MEP_PSEUDO64_NONE,
  MEP_PSEUDO64_16BITCC,
  MEP_PSEUDO64_32BITCC,
} MepPseudo64Values;

static struct {
  int insn;
  int growth;
  int insn_for_extern;
} subtype_mappings[] = {
  { 0, 0, 0 },
  { 0, 0, 0 },
  { MEP_INSN_BSR12, 0, MEP_INSN_BSR24 },
  { MEP_INSN_BSR24, 2, MEP_INSN_BSR24 },
  { MEP_INSN_BRA,   0, MEP_INSN_BRA   },
  { MEP_INSN_BEQ,   2, MEP_INSN_BEQ   },
  { MEP_INSN_JMP,   2, MEP_INSN_JMP   },
  { MEP_INSN_BEQI,  0, MEP_INSN_BEQI  },
  { -1,             4, MEP_PSEUDO64_32BITCC },
  { MEP_INSN_BEQZ,  0, MEP_INSN_BEQZ  },
  { MEP_INSN_BEQI,  2, MEP_INSN_BEQI  },
  { -1,             4, MEP_PSEUDO64_16BITCC },
  { MEP_INSN_BNEI,  0, MEP_INSN_BNEI  },
  { -1,             4, MEP_PSEUDO64_32BITCC },
  { MEP_INSN_BNEZ,  0, MEP_INSN_BNEZ  },
  { MEP_INSN_BNEI,  2, MEP_INSN_BNEI  },
  { -1,             4, MEP_PSEUDO64_16BITCC },
  { MEP_INSN_BGEI,  0, MEP_INSN_BGEI  },
  { -1,             4, MEP_PSEUDO64_32BITCC },
  { MEP_INSN_BLTI,  0, MEP_INSN_BLTI  },
  { -1,             4, MEP_PSEUDO64_32BITCC },
  { MEP_INSN_BCPEQ, 0, MEP_INSN_BCPEQ  },
  { -1,             4, MEP_PSEUDO64_32BITCC },
  { MEP_INSN_BCPNE, 0, MEP_INSN_BCPNE  },
  { -1,             4, MEP_PSEUDO64_32BITCC },
  { MEP_INSN_BCPAT, 0, MEP_INSN_BCPAT  },
  { -1,             4, MEP_PSEUDO64_32BITCC },
  { MEP_INSN_BCPAF, 0, MEP_INSN_BCPAF  },
  { -1,             4, MEP_PSEUDO64_32BITCC }
};
#define NUM_MAPPINGS (sizeof (subtype_mappings) / sizeof (subtype_mappings[0]))

void
mep_prepare_relax_scan (fragS *fragP, offsetT *aim, relax_substateT this_state)
{
  symbolS *symbolP = fragP->fr_symbol;
  if (symbolP && !S_IS_DEFINED (symbolP))
    *aim = 0;
  /* Adjust for MeP pcrel not being relative to the next opcode.  */
  *aim += 2 + md_relax_table[this_state].rlx_length;
}

static int
insn_to_subtype (int insn)
{
  unsigned int i;
  for (i=0; i<NUM_MAPPINGS; i++)
    if (insn == subtype_mappings[i].insn)
      return i;
  abort ();
}

/* Return an initial guess of the length by which a fragment must grow
   to hold a branch to reach its destination.  Also updates fr_type
   and fr_subtype as necessary.

   Called just before doing relaxation.  Any symbol that is now
   undefined will not become defined.  The guess for fr_var is
   ACTUALLY the growth beyond fr_fix.  Whatever we do to grow fr_fix
   or fr_var contributes to our returned value.  Although it may not
   be explicit in the frag, pretend fr_var starts with a 0 value.  */

int
md_estimate_size_before_relax (fragS * fragP, segT segment)
{
  if (fragP->fr_subtype == 1)
    fragP->fr_subtype = insn_to_subtype (fragP->fr_cgen.insn->base->num);

  if (S_GET_SEGMENT (fragP->fr_symbol) != segment)
    {
      int new_insn;

      new_insn = subtype_mappings[fragP->fr_subtype].insn_for_extern;
      fragP->fr_subtype = insn_to_subtype (new_insn);
    }

  if (MEP_VLIW && ! MEP_VLIW64
      && (bfd_get_section_flags (stdoutput, segment) & SEC_MEP_VLIW))
    {
      /* Use 32 bit branches for vliw32 so the vliw word is not split.  */
      switch (fragP->fr_cgen.insn->base->num)
	{
	case MEP_INSN_BSR12:
	  fragP->fr_subtype = insn_to_subtype 
	    (subtype_mappings[fragP->fr_subtype].insn_for_extern);
	  break;
	case MEP_INSN_BEQZ:
	  fragP->fr_subtype ++;
	  break;
	case MEP_INSN_BNEZ:
	  fragP->fr_subtype ++;
	  break;
	}
    }

  if (fragP->fr_cgen.insn->base
      && fragP->fr_cgen.insn->base->num
         != subtype_mappings[fragP->fr_subtype].insn)
    {
      int new_insn= subtype_mappings[fragP->fr_subtype].insn;
      if (new_insn != -1)
	{
	  fragP->fr_cgen.insn = (fragP->fr_cgen.insn
				 - fragP->fr_cgen.insn->base->num
				 + new_insn);
	}
    }

  return subtype_mappings[fragP->fr_subtype].growth;
}

/* *fragP has been relaxed to its final size, and now needs to have
   the bytes inside it modified to conform to the new size.

   Called after relaxation is finished.
   fragP->fr_type == rs_machine_dependent.
   fragP->fr_subtype is the subtype of what the address relaxed to.  */

static int
target_address_for (fragS *frag)
{
  int rv = frag->fr_offset;
  symbolS *sym = frag->fr_symbol;

  if (sym)
    rv += S_GET_VALUE (sym);

  return rv;
}

void
md_convert_frag (bfd *abfd  ATTRIBUTE_UNUSED, 
		 segT sec ATTRIBUTE_UNUSED,
		 fragS *fragP)
{
  int addend, rn, bit = 0;
  int operand;
  int where = fragP->fr_opcode - fragP->fr_literal;
  int e = target_big_endian ? 0 : 1;

  addend = target_address_for (fragP) - (fragP->fr_address + where);

  if (subtype_mappings[fragP->fr_subtype].insn == -1)
    {
      fragP->fr_fix += subtype_mappings[fragP->fr_subtype].growth;
      switch (subtype_mappings[fragP->fr_subtype].insn_for_extern)
	{
	case MEP_PSEUDO64_16BITCC:
	  fragP->fr_opcode[1^e] = ((fragP->fr_opcode[1^e] & 1) ^ 1) | 0x06;
	  fragP->fr_opcode[2^e] = 0xd8;
	  fragP->fr_opcode[3^e] = 0x08;
	  fragP->fr_opcode[4^e] = 0;
	  fragP->fr_opcode[5^e] = 0;
	  where += 2;
	  break;
	case MEP_PSEUDO64_32BITCC:
	  if (fragP->fr_opcode[0^e] & 0x10)
	    fragP->fr_opcode[1^e] ^= 0x01;
	  else
	    fragP->fr_opcode[1^e] ^= 0x04;
	  fragP->fr_opcode[2^e] = 0;
	  fragP->fr_opcode[3^e] = 4;
	  fragP->fr_opcode[4^e] = 0xd8;
	  fragP->fr_opcode[5^e] = 0x08;
	  fragP->fr_opcode[6^e] = 0;
	  fragP->fr_opcode[7^e] = 0;
	  where += 4;
	  break;
	default:
	  abort ();
	}
      fragP->fr_cgen.insn = (fragP->fr_cgen.insn
			     - fragP->fr_cgen.insn->base->num
			     + MEP_INSN_JMP);
      operand = MEP_OPERAND_PCABS24A2;
    }
  else
    switch (fragP->fr_cgen.insn->base->num)
      {
      case MEP_INSN_BSR12:
	fragP->fr_opcode[0^e] = 0xb0 | ((addend >> 8) & 0x0f);
	fragP->fr_opcode[1^e] = 0x01 | (addend & 0xfe);
	operand = MEP_OPERAND_PCREL12A2;
	break;

      case MEP_INSN_BSR24:
	fragP->fr_fix += 2;
	fragP->fr_opcode[0^e] = 0xd8 | ((addend >> 5) & 0x07);
	fragP->fr_opcode[1^e] = 0x09 | ((addend << 3) & 0xf0);
	fragP->fr_opcode[2^e] = 0x00 | ((addend >>16) & 0xff);
	fragP->fr_opcode[3^e] = 0x00 | ((addend >> 8) & 0xff);
	operand = MEP_OPERAND_PCREL24A2;
	break;

      case MEP_INSN_BRA:
	fragP->fr_opcode[0^e] = 0xb0 | ((addend >> 8) & 0x0f);
	fragP->fr_opcode[1^e] = 0x00 | (addend & 0xfe);
	operand = MEP_OPERAND_PCREL12A2;
	break;

      case MEP_INSN_BEQ:
	/* The default relax_frag doesn't change the state if there is no
	   growth, so we must manually handle converting out-of-range BEQ
	   instructions to JMP.  */
	if (addend <= 65535 && addend >= -65536)
	  {
	    fragP->fr_fix += 2;
	    fragP->fr_opcode[0^e] = 0xe0;
	    fragP->fr_opcode[1^e] = 0x01;
	    fragP->fr_opcode[2^e] = 0x00 | ((addend >> 9) & 0xff);
	    fragP->fr_opcode[3^e] = 0x00 | ((addend >> 1) & 0xff);
	    operand = MEP_OPERAND_PCREL17A2;
	    break;
	  }
	/* ...FALLTHROUGH... */

      case MEP_INSN_JMP:
	addend = target_address_for (fragP);
	fragP->fr_fix += 2;
	fragP->fr_opcode[0^e] = 0xd8 | ((addend >> 5) & 0x07);
	fragP->fr_opcode[1^e] = 0x08 | ((addend << 3) & 0xf0);
	fragP->fr_opcode[2^e] = 0x00 | ((addend >>16) & 0xff);
	fragP->fr_opcode[3^e] = 0x00 | ((addend >> 8) & 0xff);
	operand = MEP_OPERAND_PCABS24A2;
	break;

      case MEP_INSN_BNEZ:
	bit = 1;
      case MEP_INSN_BEQZ:
	fragP->fr_opcode[1^e] = bit | (addend & 0xfe);
	operand = MEP_OPERAND_PCREL8A2;
	break;

      case MEP_INSN_BNEI:
	bit = 4;
      case MEP_INSN_BEQI:
	if (subtype_mappings[fragP->fr_subtype].growth)
	  {
	    fragP->fr_fix += subtype_mappings[fragP->fr_subtype].growth;
	    rn = fragP->fr_opcode[0^e] & 0x0f;
	    fragP->fr_opcode[0^e] = 0xe0 | rn;
	    fragP->fr_opcode[1^e] = bit;
	  }
	fragP->fr_opcode[2^e] = 0x00 | ((addend >> 9) & 0xff);
	fragP->fr_opcode[3^e] = 0x00 | ((addend >> 1) & 0xff);
	operand = MEP_OPERAND_PCREL17A2;
	break;

      case MEP_INSN_BLTI:
      case MEP_INSN_BGEI:
      case MEP_INSN_BCPEQ:
      case MEP_INSN_BCPNE:
      case MEP_INSN_BCPAT:
      case MEP_INSN_BCPAF:
	/* No opcode change needed, just operand.  */
	fragP->fr_opcode[2^e] = (addend >> 9) & 0xff;
	fragP->fr_opcode[3^e] = (addend >> 1) & 0xff;
	operand = MEP_OPERAND_PCREL17A2;
	break;

      default:
	abort ();
      }

  if (S_GET_SEGMENT (fragP->fr_symbol) != sec
      || operand == MEP_OPERAND_PCABS24A2)
    {
      assert (fragP->fr_cgen.insn != 0);
      gas_cgen_record_fixup (fragP,
			     where,
			     fragP->fr_cgen.insn,
			     (fragP->fr_fix - where) * 8,
			     cgen_operand_lookup_by_num (gas_cgen_cpu_desc,
							 operand),
			     fragP->fr_cgen.opinfo,
			     fragP->fr_symbol, fragP->fr_offset);
    }
}


/* Functions concerning relocs.  */

void
mep_apply_fix (fixS *fixP, valueT *valP, segT seg ATTRIBUTE_UNUSED)
{
  /* If we already know the fixup value, adjust it in the same
     way that the linker would have done.  */
  if (fixP->fx_addsy == 0)
    switch (fixP->fx_cgen.opinfo)
      {
      case BFD_RELOC_MEP_LOW16:
	*valP = ((long)(*valP & 0xffff)) << 16 >> 16;
	break;
      case BFD_RELOC_MEP_HI16U:
	*valP >>= 16;
	break;
      case BFD_RELOC_MEP_HI16S:
	*valP = (*valP + 0x8000) >> 16;
	break;
      }

  /* Now call cgen's md_aply_fix.  */
  gas_cgen_md_apply_fix (fixP, valP, seg);
}

long
md_pcrel_from_section (fixS *fixP, segT sec)
{
  if (fixP->fx_addsy != (symbolS *) NULL
      && (! S_IS_DEFINED (fixP->fx_addsy)
	  || S_GET_SEGMENT (fixP->fx_addsy) != sec))
    /* The symbol is undefined (or is defined but not in this section).
       Let the linker figure it out.  */
    return 0;

  /* Return the address of the opcode - cgen adjusts for opcode size
     itself, to be consistent with the disassembler, which must do
     so.  */
  return fixP->fx_where + fixP->fx_frag->fr_address;
}

/* Return the bfd reloc type for OPERAND of INSN at fixup FIXP.
   Returns BFD_RELOC_NONE if no reloc type can be found.
   *FIXP may be modified if desired.  */

#if defined (__STDC__) || defined (ALMOST_STDC) || defined (HAVE_STRINGIZE)
#define MAP(n) case MEP_OPERAND_##n: return BFD_RELOC_MEP_##n;
#else
#define MAP(n) case MEP_OPERAND_/**/n: return BFD_RELOC_MEP_/**/n;
#endif

bfd_reloc_code_real_type
md_cgen_lookup_reloc (const CGEN_INSN *insn ATTRIBUTE_UNUSED,
		      const CGEN_OPERAND *operand,
		      fixS *fixP)
{
  enum bfd_reloc_code_real reloc = fixP->fx_cgen.opinfo;
  static char printed[MEP_OPERAND_MAX] = { 0 };

  /* If there's a reloc here, it's because the parser saw a %foo() and
     is giving us the correct reloc to use, or because we converted to
     a different size reloc below and want to avoid "converting" more
     than once.  */
  if (reloc && reloc != BFD_RELOC_NONE)
    return reloc;

  switch (operand->type)
    {
      MAP (PCREL8A2);	/* beqz */
      MAP (PCREL12A2);	/* bsr16 */
      MAP (PCREL17A2);	/* beqi */
      MAP (PCREL24A2);	/* bsr24 */
      MAP (PCABS24A2);	/* jmp */
      MAP (UIMM24);	/* mov */
      MAP (ADDR24A4);	/* sw/lw */

    /* The rest of the relocs should be generated by the parser,
       for things such as %tprel(), etc. */
    case MEP_OPERAND_SIMM16:
#ifdef OBJ_COMPLEX_RELC
      /* coalescing this into RELOC_MEP_16 is actually a bug,
	 since it's a signed operand. let the relc code handle it. */
      return BFD_RELOC_RELC; 
#endif

    case MEP_OPERAND_UIMM16:
    case MEP_OPERAND_SDISP16:
    case MEP_OPERAND_CODE16:
      fixP->fx_where += 2;
      /* to avoid doing the above add twice */
      fixP->fx_cgen.opinfo = BFD_RELOC_MEP_16;
      return BFD_RELOC_MEP_16;

    default:
#ifdef OBJ_COMPLEX_RELC
      /* this is not an error, yet. 
	 pass it to the linker. */
      return BFD_RELOC_RELC;
#endif
      if (printed[operand->type])
	return BFD_RELOC_NONE;
      printed[operand->type] = 1;

      as_bad_where (fixP->fx_file, fixP->fx_line,
		    _("Don't know how to relocate plain operands of type %s"),
		    operand->name);

      /* Print some helpful hints for the user.  */
      switch (operand->type)
	{
	case MEP_OPERAND_UDISP7:
	case MEP_OPERAND_UDISP7A2:
	case MEP_OPERAND_UDISP7A4:
	  as_bad_where (fixP->fx_file, fixP->fx_line,
			_("Perhaps you are missing %%tpoff()?"));
	  break;
	default:
	  break;
	}
      return BFD_RELOC_NONE;
    }
}

/* Called while parsing an instruction to create a fixup.
   We need to check for HI16 relocs and queue them up for later sorting.  */

fixS *
mep_cgen_record_fixup_exp (fragS *frag,
			   int where,
			   const CGEN_INSN *insn,
			   int length,
			   const CGEN_OPERAND *operand,
			   int opinfo,
			   expressionS *exp)
{
  fixS * fixP = gas_cgen_record_fixup_exp (frag, where, insn, length,
					   operand, opinfo, exp);
  return fixP;
}

/* Return BFD reloc type from opinfo field in a fixS.
   It's tricky using fx_r_type in mep_frob_file because the values
   are BFD_RELOC_UNUSED + operand number.  */
#define FX_OPINFO_R_TYPE(f) ((f)->fx_cgen.opinfo)

/* Sort any unmatched HI16 relocs so that they immediately precede
   the corresponding LO16 reloc.  This is called before md_apply_fix and
   tc_gen_reloc.  */

void
mep_frob_file ()
{
  struct mep_hi_fixup * l;

  for (l = mep_hi_fixup_list; l != NULL; l = l->next)
    {
      segment_info_type * seginfo;
      int pass;

      assert (FX_OPINFO_R_TYPE (l->fixp) == BFD_RELOC_HI16
	      || FX_OPINFO_R_TYPE (l->fixp) == BFD_RELOC_LO16);

      /* Check quickly whether the next fixup happens to be a matching low.  */
      if (l->fixp->fx_next != NULL
	  && FX_OPINFO_R_TYPE (l->fixp->fx_next) == BFD_RELOC_LO16
	  && l->fixp->fx_addsy == l->fixp->fx_next->fx_addsy
	  && l->fixp->fx_offset == l->fixp->fx_next->fx_offset)
	continue;

      /* Look through the fixups for this segment for a matching
         `low'.  When we find one, move the high just in front of it.
         We do this in two passes.  In the first pass, we try to find
         a unique `low'.  In the second pass, we permit multiple
         high's relocs for a single `low'.  */
      seginfo = seg_info (l->seg);
      for (pass = 0; pass < 2; pass++)
	{
	  fixS * f;
	  fixS * prev;

	  prev = NULL;
	  for (f = seginfo->fix_root; f != NULL; f = f->fx_next)
	    {
	      /* Check whether this is a `low' fixup which matches l->fixp.  */
	      if (FX_OPINFO_R_TYPE (f) == BFD_RELOC_LO16
		  && f->fx_addsy == l->fixp->fx_addsy
		  && f->fx_offset == l->fixp->fx_offset
		  && (pass == 1
		      || prev == NULL
		      || (FX_OPINFO_R_TYPE (prev) != BFD_RELOC_HI16)
		      || prev->fx_addsy != f->fx_addsy
		      || prev->fx_offset !=  f->fx_offset))
		{
		  fixS ** pf;

		  /* Move l->fixp before f.  */
		  for (pf = &seginfo->fix_root;
		       * pf != l->fixp;
		       pf = & (* pf)->fx_next)
		    assert (* pf != NULL);

		  * pf = l->fixp->fx_next;

		  l->fixp->fx_next = f;
		  if (prev == NULL)
		    seginfo->fix_root = l->fixp;
		  else
		    prev->fx_next = l->fixp;

		  break;
		}

	      prev = f;
	    }

	  if (f != NULL)
	    break;

	  if (pass == 1)
	    as_warn_where (l->fixp->fx_file, l->fixp->fx_line,
			   _("Unmatched high relocation"));
	}
    }
}

/* See whether we need to force a relocation into the output file. */

int
mep_force_relocation (fixS *fixp)
{
  if (   fixp->fx_r_type == BFD_RELOC_VTABLE_INHERIT
	 || fixp->fx_r_type == BFD_RELOC_VTABLE_ENTRY)
    return 1;

  /* Allow branches to global symbols to be resolved at assembly time.
     This is consistent with way relaxable branches are handled, since
     branches to both global and local symbols are relaxed.  It also
     corresponds to the assumptions made in md_pcrel_from_section.  */
  return S_FORCE_RELOC (fixp->fx_addsy, !fixp->fx_pcrel);
}

/* Write a value out to the object file, using the appropriate endianness.  */

void
md_number_to_chars (char *buf, valueT val, int n)
{
  if (target_big_endian)
    number_to_chars_bigendian (buf, val, n);
  else
    number_to_chars_littleendian (buf, val, n);
}

/* Turn a string in input_line_pointer into a floating point constant
   of type type, and store the appropriate bytes in *litP.  The number
   of LITTLENUMS emitted is stored in *sizeP .  An error message is
   returned, or NULL on OK. */

/* Equal to MAX_PRECISION in atof-ieee.c */
#define MAX_LITTLENUMS 6

char *
md_atof (int type, char *litP, int *sizeP)
{
  int              i;
  int              prec;
  LITTLENUM_TYPE   words [MAX_LITTLENUMS];
  char *           t;

  switch (type)
    {
    case 'f':
    case 'F':
    case 's':
    case 'S':
      prec = 2;
      break;

    case 'd':
    case 'D':
    case 'r':
    case 'R':
      prec = 4;
      break;

    /* FIXME: Some targets allow other format chars for bigger sizes here.  */
    default:
      *sizeP = 0;
      return _("Bad call to md_atof()");
    }

  t = atof_ieee (input_line_pointer, type, words);
  if (t)
    input_line_pointer = t;
  * sizeP = prec * sizeof (LITTLENUM_TYPE);

  for (i = 0; i < prec; i++)
    {
      md_number_to_chars (litP, (valueT) words[i],
			  sizeof (LITTLENUM_TYPE));
      litP += sizeof (LITTLENUM_TYPE);
    }

  return 0;
}


bfd_boolean
mep_fix_adjustable (fixS *fixP)
{
  bfd_reloc_code_real_type reloc_type;

  if ((int) fixP->fx_r_type >= (int) BFD_RELOC_UNUSED)
    {
      const CGEN_INSN *insn = NULL;
      int opindex = (int) fixP->fx_r_type - (int) BFD_RELOC_UNUSED;
      const CGEN_OPERAND *operand
	= cgen_operand_lookup_by_num(gas_cgen_cpu_desc, opindex);
      reloc_type = md_cgen_lookup_reloc (insn, operand, fixP);
    }
  else
    reloc_type = fixP->fx_r_type;

  if (fixP->fx_addsy == NULL)
    return 1;

  /* Prevent all adjustments to global symbols. */
  if (S_IS_EXTERNAL (fixP->fx_addsy))
    return 0;

  if (S_IS_WEAK (fixP->fx_addsy))
    return 0;

  /* We need the symbol name for the VTABLE entries */
  if (reloc_type == BFD_RELOC_VTABLE_INHERIT
      || reloc_type == BFD_RELOC_VTABLE_ENTRY)
    return 0;

  return 1;
}

int
mep_elf_section_letter (int letter, char **ptrmsg)
{
  if (letter == 'v')
    return SHF_MEP_VLIW;

  *ptrmsg = _("Bad .section directive: want a,v,w,x,M,S in string");
  return 0;
}

flagword
mep_elf_section_flags (flagword flags, int attr, int type ATTRIBUTE_UNUSED)
{
  if (attr & SHF_MEP_VLIW)
    flags |= SEC_MEP_VLIW;
  return flags;
}

/* In vliw mode, the default section is .vtext.  We have to be able
   to switch into .vtext using only the .vtext directive.  */

static segT
mep_vtext_section (void)
{
  static segT vtext_section;

  if (! vtext_section)
    {
      flagword applicable = bfd_applicable_section_flags (stdoutput);
      vtext_section = subseg_new (VTEXT_SECTION_NAME, 0);
      bfd_set_section_flags (stdoutput, vtext_section,
			     applicable & (SEC_ALLOC | SEC_LOAD | SEC_RELOC
					   | SEC_CODE | SEC_READONLY
					   | SEC_MEP_VLIW));
    }

  return vtext_section;
}

static void
mep_s_vtext (int ignore ATTRIBUTE_UNUSED)
{
  int temp;

  /* Record previous_section and previous_subsection.  */
  obj_elf_section_change_hook ();

  temp = get_absolute_expression ();
  subseg_set (mep_vtext_section (), (subsegT) temp);
  demand_empty_rest_of_line ();
}

static void
mep_switch_to_core_mode (int dummy ATTRIBUTE_UNUSED)
{
  mep_process_saved_insns ();
  pluspresent = 0;
  mode = CORE;
}

static void
mep_switch_to_vliw_mode (int dummy ATTRIBUTE_UNUSED)
{
  if (! MEP_VLIW)
    as_bad (_(".vliw unavailable when VLIW is disabled."));
  mode = VLIW;
  /* Switch into .vtext here too. */
  /* mep_s_vtext(); */
}

/* This is an undocumented pseudo-op used to disable gas's
   "disabled_registers" check.  Used for code which checks for those
   registers at runtime.  */
static void
mep_noregerr (int i ATTRIBUTE_UNUSED)
{
  allow_disabled_registers = 1;
}

/* mep_unrecognized_line: This is called when a line that can't be parsed
   is encountered.  We use it to check for a leading '+' sign which indicates
   that the current instruction is a coprocessor instruction that is to be
   parallelized with a previous core insn.  This function accepts the '+' and
   rejects all other characters that might indicate garbage at the beginning
   of the line.  The '+' character gets lost as the calling loop continues,
   so we need to indicate that we saw it.  */

int
mep_unrecognized_line (int ch)
{
  switch (ch)
    {
    case '+':
      pluspresent = 1;
      return 1; /* '+' indicates an instruction to be parallelized. */
    default:
      return 0; /* If it's not a '+', the line can't be parsed. */
    }
}

void
mep_cleanup (void)
{
  /* Take care of any insns left to be parallelized when the file ends.
     This is mainly here to handle the case where the file ends with an
     insn preceeded by a + or the file ends unexpectedly.  */
  if (mode == VLIW)
    mep_process_saved_insns ();
}

int
mep_flush_pending_output (void)
{
  if (mode == VLIW)
    {
      mep_process_saved_insns ();
      pluspresent = 0;
    }

  return 1; 
}
