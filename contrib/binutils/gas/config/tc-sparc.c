/* tc-sparc.c -- Assemble for the SPARC
   Copyright 1989, 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998,
   1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007
   Free Software Foundation, Inc.
   This file is part of GAS, the GNU Assembler.

   GAS is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   GAS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public
   License along with GAS; see the file COPYING.  If not, write
   to the Free Software Foundation, 51 Franklin Street - Fifth Floor,
   Boston, MA 02110-1301, USA.  */

#include "as.h"
#include "safe-ctype.h"
#include "subsegs.h"

#include "opcode/sparc.h"
#include "dw2gencfi.h"

#ifdef OBJ_ELF
#include "elf/sparc.h"
#include "dwarf2dbg.h"
#endif

/* Some ancient Sun C compilers would not take such hex constants as
   unsigned, and would end up sign-extending them to form an offsetT,
   so use these constants instead.  */
#define U0xffffffff ((((unsigned long) 1 << 16) << 16) - 1)
#define U0x80000000 ((((unsigned long) 1 << 16) << 15))

static struct sparc_arch *lookup_arch PARAMS ((char *));
static void init_default_arch PARAMS ((void));
static int sparc_ip PARAMS ((char *, const struct sparc_opcode **));
static int in_signed_range PARAMS ((bfd_signed_vma, bfd_signed_vma));
static int in_unsigned_range PARAMS ((bfd_vma, bfd_vma));
static int in_bitfield_range PARAMS ((bfd_signed_vma, bfd_signed_vma));
static int sparc_ffs PARAMS ((unsigned int));
static void synthetize_setuw PARAMS ((const struct sparc_opcode *));
static void synthetize_setsw PARAMS ((const struct sparc_opcode *));
static void synthetize_setx PARAMS ((const struct sparc_opcode *));
static bfd_vma BSR PARAMS ((bfd_vma, int));
static int cmp_reg_entry PARAMS ((const PTR, const PTR));
static int parse_keyword_arg PARAMS ((int (*) (const char *), char **, int *));
static int parse_const_expr_arg PARAMS ((char **, int *));
static int get_expression PARAMS ((char *str));

/* Default architecture.  */
/* ??? The default value should be V8, but sparclite support was added
   by making it the default.  GCC now passes -Asparclite, so maybe sometime in
   the future we can set this to V8.  */
#ifndef DEFAULT_ARCH
#define DEFAULT_ARCH "sparclite"
#endif
static char *default_arch = DEFAULT_ARCH;

/* Non-zero if the initial values of `max_architecture' and `sparc_arch_size'
   have been set.  */
static int default_init_p;

/* Current architecture.  We don't bump up unless necessary.  */
static enum sparc_opcode_arch_val current_architecture = SPARC_OPCODE_ARCH_V6;

/* The maximum architecture level we can bump up to.
   In a 32 bit environment, don't allow bumping up to v9 by default.
   The native assembler works this way.  The user is required to pass
   an explicit argument before we'll create v9 object files.  However, if
   we don't see any v9 insns, a v8plus object file is not created.  */
static enum sparc_opcode_arch_val max_architecture;

/* Either 32 or 64, selects file format.  */
static int sparc_arch_size;
/* Initial (default) value, recorded separately in case a user option
   changes the value before md_show_usage is called.  */
static int default_arch_size;

#ifdef OBJ_ELF
/* The currently selected v9 memory model.  Currently only used for
   ELF.  */
static enum { MM_TSO, MM_PSO, MM_RMO } sparc_memory_model = MM_RMO;
#endif

static int architecture_requested;
static int warn_on_bump;

/* If warn_on_bump and the needed architecture is higher than this
   architecture, issue a warning.  */
static enum sparc_opcode_arch_val warn_after_architecture;

/* Non-zero if as should generate error if an undeclared g[23] register
   has been used in -64.  */
static int no_undeclared_regs;

/* Non-zero if we should try to relax jumps and calls.  */
static int sparc_relax;

/* Non-zero if we are generating PIC code.  */
int sparc_pic_code;

/* Non-zero if we should give an error when misaligned data is seen.  */
static int enforce_aligned_data;

extern int target_big_endian;

static int target_little_endian_data;

/* Symbols for global registers on v9.  */
static symbolS *globals[8];

/* The dwarf2 data alignment, adjusted for 32 or 64 bit.  */
int sparc_cie_data_alignment;

/* V9 and 86x have big and little endian data, but instructions are always big
   endian.  The sparclet has bi-endian support but both data and insns have
   the same endianness.  Global `target_big_endian' is used for data.
   The following macro is used for instructions.  */
#ifndef INSN_BIG_ENDIAN
#define INSN_BIG_ENDIAN (target_big_endian \
			 || default_arch_type == sparc86x \
			 || SPARC_OPCODE_ARCH_V9_P (max_architecture))
#endif

/* Handle of the OPCODE hash table.  */
static struct hash_control *op_hash;

static int mylog2 PARAMS ((int));
static void s_data1 PARAMS ((void));
static void s_seg PARAMS ((int));
static void s_proc PARAMS ((int));
static void s_reserve PARAMS ((int));
static void s_common PARAMS ((int));
static void s_empty PARAMS ((int));
static void s_uacons PARAMS ((int));
static void s_ncons PARAMS ((int));
#ifdef OBJ_ELF
static void s_register PARAMS ((int));
#endif

const pseudo_typeS md_pseudo_table[] =
{
  {"align", s_align_bytes, 0},	/* Defaulting is invalid (0).  */
  {"common", s_common, 0},
  {"empty", s_empty, 0},
  {"global", s_globl, 0},
  {"half", cons, 2},
  {"nword", s_ncons, 0},
  {"optim", s_ignore, 0},
  {"proc", s_proc, 0},
  {"reserve", s_reserve, 0},
  {"seg", s_seg, 0},
  {"skip", s_space, 0},
  {"word", cons, 4},
  {"xword", cons, 8},
  {"uahalf", s_uacons, 2},
  {"uaword", s_uacons, 4},
  {"uaxword", s_uacons, 8},
#ifdef OBJ_ELF
  /* These are specific to sparc/svr4.  */
  {"2byte", s_uacons, 2},
  {"4byte", s_uacons, 4},
  {"8byte", s_uacons, 8},
  {"register", s_register, 0},
#endif
  {NULL, 0, 0},
};

/* This array holds the chars that always start a comment.  If the
   pre-processor is disabled, these aren't very useful.  */
const char comment_chars[] = "!";	/* JF removed '|' from
                                           comment_chars.  */

/* This array holds the chars that only start a comment at the beginning of
   a line.  If the line seems to have the form '# 123 filename'
   .line and .file directives will appear in the pre-processed output.  */
/* Note that input_file.c hand checks for '#' at the beginning of the
   first line of the input file.  This is because the compiler outputs
   #NO_APP at the beginning of its output.  */
/* Also note that comments started like this one will always
   work if '/' isn't otherwise defined.  */
const char line_comment_chars[] = "#";

const char line_separator_chars[] = ";";

/* Chars that can be used to separate mant from exp in floating point
   nums.  */
const char EXP_CHARS[] = "eE";

/* Chars that mean this number is a floating point constant.
   As in 0f12.456
   or    0d1.2345e12  */
const char FLT_CHARS[] = "rRsSfFdDxXpP";

/* Also be aware that MAXIMUM_NUMBER_OF_CHARS_FOR_FLOAT may have to be
   changed in read.c.  Ideally it shouldn't have to know about it at all,
   but nothing is ideal around here.  */

#define isoctal(c)  ((unsigned) ((c) - '0') < 8)

struct sparc_it
  {
    char *error;
    unsigned long opcode;
    struct nlist *nlistp;
    expressionS exp;
    expressionS exp2;
    int pcrel;
    bfd_reloc_code_real_type reloc;
  };

struct sparc_it the_insn, set_insn;

static void output_insn
  PARAMS ((const struct sparc_opcode *, struct sparc_it *));

/* Table of arguments to -A.
   The sparc_opcode_arch table in sparc-opc.c is insufficient and incorrect
   for this use.  That table is for opcodes only.  This table is for opcodes
   and file formats.  */

enum sparc_arch_types {v6, v7, v8, sparclet, sparclite, sparc86x, v8plus,
		       v8plusa, v9, v9a, v9b, v9_64};

static struct sparc_arch {
  char *name;
  char *opcode_arch;
  enum sparc_arch_types arch_type;
  /* Default word size, as specified during configuration.
     A value of zero means can't be used to specify default architecture.  */
  int default_arch_size;
  /* Allowable arg to -A?  */
  int user_option_p;
} sparc_arch_table[] = {
  { "v6", "v6", v6, 0, 1 },
  { "v7", "v7", v7, 0, 1 },
  { "v8", "v8", v8, 32, 1 },
  { "sparclet", "sparclet", sparclet, 32, 1 },
  { "sparclite", "sparclite", sparclite, 32, 1 },
  { "sparc86x", "sparclite", sparc86x, 32, 1 },
  { "v8plus", "v9", v9, 0, 1 },
  { "v8plusa", "v9a", v9, 0, 1 },
  { "v8plusb", "v9b", v9, 0, 1 },
  { "v9", "v9", v9, 0, 1 },
  { "v9a", "v9a", v9, 0, 1 },
  { "v9b", "v9b", v9, 0, 1 },
  /* This exists to allow configure.in/Makefile.in to pass one
     value to specify both the default machine and default word size.  */
  { "v9-64", "v9", v9, 64, 0 },
  { NULL, NULL, v8, 0, 0 }
};

/* Variant of default_arch */
static enum sparc_arch_types default_arch_type;

static struct sparc_arch *
lookup_arch (name)
     char *name;
{
  struct sparc_arch *sa;

  for (sa = &sparc_arch_table[0]; sa->name != NULL; sa++)
    if (strcmp (sa->name, name) == 0)
      break;
  if (sa->name == NULL)
    return NULL;
  return sa;
}

/* Initialize the default opcode arch and word size from the default
   architecture name.  */

static void
init_default_arch ()
{
  struct sparc_arch *sa = lookup_arch (default_arch);

  if (sa == NULL
      || sa->default_arch_size == 0)
    as_fatal (_("Invalid default architecture, broken assembler."));

  max_architecture = sparc_opcode_lookup_arch (sa->opcode_arch);
  if (max_architecture == SPARC_OPCODE_ARCH_BAD)
    as_fatal (_("Bad opcode table, broken assembler."));
  default_arch_size = sparc_arch_size = sa->default_arch_size;
  default_init_p = 1;
  default_arch_type = sa->arch_type;
}

/* Called by TARGET_FORMAT.  */

const char *
sparc_target_format ()
{
  /* We don't get a chance to initialize anything before we're called,
     so handle that now.  */
  if (! default_init_p)
    init_default_arch ();

#ifdef OBJ_AOUT
#ifdef TE_NetBSD
  return "a.out-sparc-netbsd";
#else
#ifdef TE_SPARCAOUT
  if (target_big_endian)
    return "a.out-sunos-big";
  else if (default_arch_type == sparc86x && target_little_endian_data)
    return "a.out-sunos-big";
  else
    return "a.out-sparc-little";
#else
  return "a.out-sunos-big";
#endif
#endif
#endif

#ifdef OBJ_BOUT
  return "b.out.big";
#endif

#ifdef OBJ_COFF
#ifdef TE_LYNX
  return "coff-sparc-lynx";
#else
  return "coff-sparc";
#endif
#endif

#ifdef TE_VXWORKS
  return "elf32-sparc-vxworks";
#endif

#ifdef OBJ_ELF
  return sparc_arch_size == 64 ? ELF64_TARGET_FORMAT : ELF_TARGET_FORMAT;
#endif

  abort ();
}

/* md_parse_option
 *	Invocation line includes a switch not recognized by the base assembler.
 *	See if it's a processor-specific option.  These are:
 *
 *	-bump
 *		Warn on architecture bumps.  See also -A.
 *
 *	-Av6, -Av7, -Av8, -Asparclite, -Asparclet
 *		Standard 32 bit architectures.
 *	-Av9, -Av9a, -Av9b
 *		Sparc64 in either a 32 or 64 bit world (-32/-64 says which).
 *		This used to only mean 64 bits, but properly specifying it
 *		complicated gcc's ASM_SPECs, so now opcode selection is
 *		specified orthogonally to word size (except when specifying
 *		the default, but that is an internal implementation detail).
 *	-Av8plus, -Av8plusa, -Av8plusb
 *		Same as -Av9{,a,b}.
 *	-xarch=v8plus, -xarch=v8plusa, -xarch=v8plusb
 *		Same as -Av8plus{,a,b} -32, for compatibility with Sun's
 *		assembler.
 *	-xarch=v9, -xarch=v9a, -xarch=v9b
 *		Same as -Av9{,a,b} -64, for compatibility with Sun's
 *		assembler.
 *
 *		Select the architecture and possibly the file format.
 *		Instructions or features not supported by the selected
 *		architecture cause fatal errors.
 *
 *		The default is to start at v6, and bump the architecture up
 *		whenever an instruction is seen at a higher level.  In 32 bit
 *		environments, v9 is not bumped up to, the user must pass
 * 		-Av8plus{,a,b}.
 *
 *		If -bump is specified, a warning is printing when bumping to
 *		higher levels.
 *
 *		If an architecture is specified, all instructions must match
 *		that architecture.  Any higher level instructions are flagged
 *		as errors.  Note that in the 32 bit environment specifying
 *		-Av8plus does not automatically create a v8plus object file, a
 *		v9 insn must be seen.
 *
 *		If both an architecture and -bump are specified, the
 *		architecture starts at the specified level, but bumps are
 *		warnings.  Note that we can't set `current_architecture' to
 *		the requested level in this case: in the 32 bit environment,
 *		we still must avoid creating v8plus object files unless v9
 * 		insns are seen.
 *
 * Note:
 *		Bumping between incompatible architectures is always an
 *		error.  For example, from sparclite to v9.
 */

#ifdef OBJ_ELF
const char *md_shortopts = "A:K:VQ:sq";
#else
#ifdef OBJ_AOUT
const char *md_shortopts = "A:k";
#else
const char *md_shortopts = "A:";
#endif
#endif
struct option md_longopts[] = {
#define OPTION_BUMP (OPTION_MD_BASE)
  {"bump", no_argument, NULL, OPTION_BUMP},
#define OPTION_SPARC (OPTION_MD_BASE + 1)
  {"sparc", no_argument, NULL, OPTION_SPARC},
#define OPTION_XARCH (OPTION_MD_BASE + 2)
  {"xarch", required_argument, NULL, OPTION_XARCH},
#ifdef OBJ_ELF
#define OPTION_32 (OPTION_MD_BASE + 3)
  {"32", no_argument, NULL, OPTION_32},
#define OPTION_64 (OPTION_MD_BASE + 4)
  {"64", no_argument, NULL, OPTION_64},
#define OPTION_TSO (OPTION_MD_BASE + 5)
  {"TSO", no_argument, NULL, OPTION_TSO},
#define OPTION_PSO (OPTION_MD_BASE + 6)
  {"PSO", no_argument, NULL, OPTION_PSO},
#define OPTION_RMO (OPTION_MD_BASE + 7)
  {"RMO", no_argument, NULL, OPTION_RMO},
#endif
#ifdef SPARC_BIENDIAN
#define OPTION_LITTLE_ENDIAN (OPTION_MD_BASE + 8)
  {"EL", no_argument, NULL, OPTION_LITTLE_ENDIAN},
#define OPTION_BIG_ENDIAN (OPTION_MD_BASE + 9)
  {"EB", no_argument, NULL, OPTION_BIG_ENDIAN},
#endif
#define OPTION_ENFORCE_ALIGNED_DATA (OPTION_MD_BASE + 10)
  {"enforce-aligned-data", no_argument, NULL, OPTION_ENFORCE_ALIGNED_DATA},
#define OPTION_LITTLE_ENDIAN_DATA (OPTION_MD_BASE + 11)
  {"little-endian-data", no_argument, NULL, OPTION_LITTLE_ENDIAN_DATA},
#ifdef OBJ_ELF
#define OPTION_NO_UNDECLARED_REGS (OPTION_MD_BASE + 12)
  {"no-undeclared-regs", no_argument, NULL, OPTION_NO_UNDECLARED_REGS},
#define OPTION_UNDECLARED_REGS (OPTION_MD_BASE + 13)
  {"undeclared-regs", no_argument, NULL, OPTION_UNDECLARED_REGS},
#endif
#define OPTION_RELAX (OPTION_MD_BASE + 14)
  {"relax", no_argument, NULL, OPTION_RELAX},
#define OPTION_NO_RELAX (OPTION_MD_BASE + 15)
  {"no-relax", no_argument, NULL, OPTION_NO_RELAX},
  {NULL, no_argument, NULL, 0}
};

size_t md_longopts_size = sizeof (md_longopts);

int
md_parse_option (c, arg)
     int c;
     char *arg;
{
  /* We don't get a chance to initialize anything before we're called,
     so handle that now.  */
  if (! default_init_p)
    init_default_arch ();

  switch (c)
    {
    case OPTION_BUMP:
      warn_on_bump = 1;
      warn_after_architecture = SPARC_OPCODE_ARCH_V6;
      break;

    case OPTION_XARCH:
#ifdef OBJ_ELF
      if (strncmp (arg, "v9", 2) != 0)
	md_parse_option (OPTION_32, NULL);
      else
	md_parse_option (OPTION_64, NULL);
#endif
      /* Fall through.  */

    case 'A':
      {
	struct sparc_arch *sa;
	enum sparc_opcode_arch_val opcode_arch;

	sa = lookup_arch (arg);
	if (sa == NULL
	    || ! sa->user_option_p)
	  {
	    if (c == OPTION_XARCH)
	      as_bad (_("invalid architecture -xarch=%s"), arg);
	    else
	      as_bad (_("invalid architecture -A%s"), arg);
	    return 0;
	  }

	opcode_arch = sparc_opcode_lookup_arch (sa->opcode_arch);
	if (opcode_arch == SPARC_OPCODE_ARCH_BAD)
	  as_fatal (_("Bad opcode table, broken assembler."));

	max_architecture = opcode_arch;
	architecture_requested = 1;
      }
      break;

    case OPTION_SPARC:
      /* Ignore -sparc, used by SunOS make default .s.o rule.  */
      break;

    case OPTION_ENFORCE_ALIGNED_DATA:
      enforce_aligned_data = 1;
      break;

#ifdef SPARC_BIENDIAN
    case OPTION_LITTLE_ENDIAN:
      target_big_endian = 0;
      if (default_arch_type != sparclet)
	as_fatal ("This target does not support -EL");
      break;
    case OPTION_LITTLE_ENDIAN_DATA:
      target_little_endian_data = 1;
      target_big_endian = 0;
      if (default_arch_type != sparc86x
	  && default_arch_type != v9)
	as_fatal ("This target does not support --little-endian-data");
      break;
    case OPTION_BIG_ENDIAN:
      target_big_endian = 1;
      break;
#endif

#ifdef OBJ_AOUT
    case 'k':
      sparc_pic_code = 1;
      break;
#endif

#ifdef OBJ_ELF
    case OPTION_32:
    case OPTION_64:
      {
	const char **list, **l;

	sparc_arch_size = c == OPTION_32 ? 32 : 64;
	list = bfd_target_list ();
	for (l = list; *l != NULL; l++)
	  {
	    if (sparc_arch_size == 32)
	      {
		if (CONST_STRNEQ (*l, "elf32-sparc"))
		  break;
	      }
	    else
	      {
		if (CONST_STRNEQ (*l, "elf64-sparc"))
		  break;
	      }
	  }
	if (*l == NULL)
	  as_fatal (_("No compiled in support for %d bit object file format"),
		    sparc_arch_size);
	free (list);
      }
      break;

    case OPTION_TSO:
      sparc_memory_model = MM_TSO;
      break;

    case OPTION_PSO:
      sparc_memory_model = MM_PSO;
      break;

    case OPTION_RMO:
      sparc_memory_model = MM_RMO;
      break;

    case 'V':
      print_version_id ();
      break;

    case 'Q':
      /* Qy - do emit .comment
	 Qn - do not emit .comment.  */
      break;

    case 's':
      /* Use .stab instead of .stab.excl.  */
      break;

    case 'q':
      /* quick -- Native assembler does fewer checks.  */
      break;

    case 'K':
      if (strcmp (arg, "PIC") != 0)
	as_warn (_("Unrecognized option following -K"));
      else
	sparc_pic_code = 1;
      break;

    case OPTION_NO_UNDECLARED_REGS:
      no_undeclared_regs = 1;
      break;

    case OPTION_UNDECLARED_REGS:
      no_undeclared_regs = 0;
      break;
#endif

    case OPTION_RELAX:
      sparc_relax = 1;
      break;

    case OPTION_NO_RELAX:
      sparc_relax = 0;
      break;

    default:
      return 0;
    }

  return 1;
}

void
md_show_usage (stream)
     FILE *stream;
{
  const struct sparc_arch *arch;
  int column;

  /* We don't get a chance to initialize anything before we're called,
     so handle that now.  */
  if (! default_init_p)
    init_default_arch ();

  fprintf (stream, _("SPARC options:\n"));
  column = 0;
  for (arch = &sparc_arch_table[0]; arch->name; arch++)
    {
      if (!arch->user_option_p)
	continue;
      if (arch != &sparc_arch_table[0])
	fprintf (stream, " | ");
      if (column + strlen (arch->name) > 70)
	{
	  column = 0;
	  fputc ('\n', stream);
	}
      column += 5 + 2 + strlen (arch->name);
      fprintf (stream, "-A%s", arch->name);
    }
  for (arch = &sparc_arch_table[0]; arch->name; arch++)
    {
      if (!arch->user_option_p)
	continue;
      fprintf (stream, " | ");
      if (column + strlen (arch->name) > 65)
	{
	  column = 0;
	  fputc ('\n', stream);
	}
      column += 5 + 7 + strlen (arch->name);
      fprintf (stream, "-xarch=%s", arch->name);
    }
  fprintf (stream, _("\n\
			specify variant of SPARC architecture\n\
-bump			warn when assembler switches architectures\n\
-sparc			ignored\n\
--enforce-aligned-data	force .long, etc., to be aligned correctly\n\
-relax			relax jumps and branches (default)\n\
-no-relax		avoid changing any jumps and branches\n"));
#ifdef OBJ_AOUT
  fprintf (stream, _("\
-k			generate PIC\n"));
#endif
#ifdef OBJ_ELF
  fprintf (stream, _("\
-32			create 32 bit object file\n\
-64			create 64 bit object file\n"));
  fprintf (stream, _("\
			[default is %d]\n"), default_arch_size);
  fprintf (stream, _("\
-TSO			use Total Store Ordering\n\
-PSO			use Partial Store Ordering\n\
-RMO			use Relaxed Memory Ordering\n"));
  fprintf (stream, _("\
			[default is %s]\n"), (default_arch_size == 64) ? "RMO" : "TSO");
  fprintf (stream, _("\
-KPIC			generate PIC\n\
-V			print assembler version number\n\
-undeclared-regs	ignore application global register usage without\n\
			appropriate .register directive (default)\n\
-no-undeclared-regs	force error on application global register usage\n\
			without appropriate .register directive\n\
-q			ignored\n\
-Qy, -Qn		ignored\n\
-s			ignored\n"));
#endif
#ifdef SPARC_BIENDIAN
  fprintf (stream, _("\
-EL			generate code for a little endian machine\n\
-EB			generate code for a big endian machine\n\
--little-endian-data	generate code for a machine having big endian\n\
                        instructions and little endian data.\n"));
#endif
}

/* Native operand size opcode translation.  */
struct
  {
    char *name;
    char *name32;
    char *name64;
  } native_op_table[] =
{
  {"ldn", "ld", "ldx"},
  {"ldna", "lda", "ldxa"},
  {"stn", "st", "stx"},
  {"stna", "sta", "stxa"},
  {"slln", "sll", "sllx"},
  {"srln", "srl", "srlx"},
  {"sran", "sra", "srax"},
  {"casn", "cas", "casx"},
  {"casna", "casa", "casxa"},
  {"clrn", "clr", "clrx"},
  {NULL, NULL, NULL},
};

/* sparc64 privileged and hyperprivileged registers.  */

struct priv_reg_entry
{
  char *name;
  int regnum;
};

struct priv_reg_entry priv_reg_table[] =
{
  {"tpc", 0},
  {"tnpc", 1},
  {"tstate", 2},
  {"tt", 3},
  {"tick", 4},
  {"tba", 5},
  {"pstate", 6},
  {"tl", 7},
  {"pil", 8},
  {"cwp", 9},
  {"cansave", 10},
  {"canrestore", 11},
  {"cleanwin", 12},
  {"otherwin", 13},
  {"wstate", 14},
  {"fq", 15},
  {"gl", 16},
  {"ver", 31},
  {"", -1},			/* End marker.  */
};

struct priv_reg_entry hpriv_reg_table[] =
{
  {"hpstate", 0},
  {"htstate", 1},
  {"hintp", 3},
  {"htba", 5},
  {"hver", 6},
  {"hstick_cmpr", 31},
  {"", -1},			/* End marker.  */
};

/* v9a specific asrs.  */

struct priv_reg_entry v9a_asr_table[] =
{
  {"tick_cmpr", 23},
  {"sys_tick_cmpr", 25},
  {"sys_tick", 24},
  {"softint", 22},
  {"set_softint", 20},
  {"pic", 17},
  {"pcr", 16},
  {"gsr", 19},
  {"dcr", 18},
  {"clear_softint", 21},
  {"", -1},			/* End marker.  */
};

static int
cmp_reg_entry (parg, qarg)
     const PTR parg;
     const PTR qarg;
{
  const struct priv_reg_entry *p = (const struct priv_reg_entry *) parg;
  const struct priv_reg_entry *q = (const struct priv_reg_entry *) qarg;

  return strcmp (q->name, p->name);
}

/* This function is called once, at assembler startup time.  It should
   set up all the tables, etc. that the MD part of the assembler will
   need.  */

void
md_begin ()
{
  register const char *retval = NULL;
  int lose = 0;
  register unsigned int i = 0;

  /* We don't get a chance to initialize anything before md_parse_option
     is called, and it may not be called, so handle default initialization
     now if not already done.  */
  if (! default_init_p)
    init_default_arch ();

  sparc_cie_data_alignment = sparc_arch_size == 64 ? -8 : -4;
  op_hash = hash_new ();

  while (i < (unsigned int) sparc_num_opcodes)
    {
      const char *name = sparc_opcodes[i].name;
      retval = hash_insert (op_hash, name, (PTR) &sparc_opcodes[i]);
      if (retval != NULL)
	{
	  as_bad (_("Internal error: can't hash `%s': %s\n"),
		  sparc_opcodes[i].name, retval);
	  lose = 1;
	}
      do
	{
	  if (sparc_opcodes[i].match & sparc_opcodes[i].lose)
	    {
	      as_bad (_("Internal error: losing opcode: `%s' \"%s\"\n"),
		      sparc_opcodes[i].name, sparc_opcodes[i].args);
	      lose = 1;
	    }
	  ++i;
	}
      while (i < (unsigned int) sparc_num_opcodes
	     && !strcmp (sparc_opcodes[i].name, name));
    }

  for (i = 0; native_op_table[i].name; i++)
    {
      const struct sparc_opcode *insn;
      char *name = ((sparc_arch_size == 32)
		    ? native_op_table[i].name32
		    : native_op_table[i].name64);
      insn = (struct sparc_opcode *) hash_find (op_hash, name);
      if (insn == NULL)
	{
	  as_bad (_("Internal error: can't find opcode `%s' for `%s'\n"),
		  name, native_op_table[i].name);
	  lose = 1;
	}
      else
	{
	  retval = hash_insert (op_hash, native_op_table[i].name, (PTR) insn);
	  if (retval != NULL)
	    {
	      as_bad (_("Internal error: can't hash `%s': %s\n"),
		      sparc_opcodes[i].name, retval);
	      lose = 1;
	    }
	}
    }

  if (lose)
    as_fatal (_("Broken assembler.  No assembly attempted."));

  qsort (priv_reg_table, sizeof (priv_reg_table) / sizeof (priv_reg_table[0]),
	 sizeof (priv_reg_table[0]), cmp_reg_entry);

  /* If -bump, record the architecture level at which we start issuing
     warnings.  The behaviour is different depending upon whether an
     architecture was explicitly specified.  If it wasn't, we issue warnings
     for all upwards bumps.  If it was, we don't start issuing warnings until
     we need to bump beyond the requested architecture or when we bump between
     conflicting architectures.  */

  if (warn_on_bump
      && architecture_requested)
    {
      /* `max_architecture' records the requested architecture.
	 Issue warnings if we go above it.  */
      warn_after_architecture = max_architecture;

      /* Find the highest architecture level that doesn't conflict with
	 the requested one.  */
      for (max_architecture = SPARC_OPCODE_ARCH_MAX;
	   max_architecture > warn_after_architecture;
	   --max_architecture)
	if (! SPARC_OPCODE_CONFLICT_P (max_architecture,
				       warn_after_architecture))
	  break;
    }
}

/* Called after all assembly has been done.  */

void
sparc_md_end ()
{
  unsigned long mach = bfd_mach_sparc;

  if (sparc_arch_size == 64)
    switch (current_architecture)
      {
      case SPARC_OPCODE_ARCH_V9A: mach = bfd_mach_sparc_v9a; break;
      case SPARC_OPCODE_ARCH_V9B: mach = bfd_mach_sparc_v9b; break;
      default: mach = bfd_mach_sparc_v9; break;
      }
  else
    switch (current_architecture)
      {
      case SPARC_OPCODE_ARCH_SPARCLET: mach = bfd_mach_sparc_sparclet; break;
      case SPARC_OPCODE_ARCH_V9: mach = bfd_mach_sparc_v8plus; break;
      case SPARC_OPCODE_ARCH_V9A: mach = bfd_mach_sparc_v8plusa; break;
      case SPARC_OPCODE_ARCH_V9B: mach = bfd_mach_sparc_v8plusb; break;
      /* The sparclite is treated like a normal sparc.  Perhaps it shouldn't
	 be but for now it is (since that's the way it's always been
	 treated).  */
      default: break;
      }
  bfd_set_arch_mach (stdoutput, bfd_arch_sparc, mach);
}

/* Return non-zero if VAL is in the range -(MAX+1) to MAX.  */

static INLINE int
in_signed_range (val, max)
     bfd_signed_vma val, max;
{
  if (max <= 0)
    abort ();
  /* Sign-extend the value from the architecture word size, so that
     0xffffffff is always considered -1 on sparc32.  */
  if (sparc_arch_size == 32)
    {
      bfd_signed_vma sign = (bfd_signed_vma) 1 << 31;
      val = ((val & U0xffffffff) ^ sign) - sign;
    }
  if (val > max)
    return 0;
  if (val < ~max)
    return 0;
  return 1;
}

/* Return non-zero if VAL is in the range 0 to MAX.  */

static INLINE int
in_unsigned_range (val, max)
     bfd_vma val, max;
{
  if (val > max)
    return 0;
  return 1;
}

/* Return non-zero if VAL is in the range -(MAX/2+1) to MAX.
   (e.g. -15 to +31).  */

static INLINE int
in_bitfield_range (val, max)
     bfd_signed_vma val, max;
{
  if (max <= 0)
    abort ();
  if (val > max)
    return 0;
  if (val < ~(max >> 1))
    return 0;
  return 1;
}

static int
sparc_ffs (mask)
     unsigned int mask;
{
  int i;

  if (mask == 0)
    return -1;

  for (i = 0; (mask & 1) == 0; ++i)
    mask >>= 1;
  return i;
}

/* Implement big shift right.  */
static bfd_vma
BSR (val, amount)
     bfd_vma val;
     int amount;
{
  if (sizeof (bfd_vma) <= 4 && amount >= 32)
    as_fatal (_("Support for 64-bit arithmetic not compiled in."));
  return val >> amount;
}

/* For communication between sparc_ip and get_expression.  */
static char *expr_end;

/* Values for `special_case'.
   Instructions that require wierd handling because they're longer than
   4 bytes.  */
#define SPECIAL_CASE_NONE	0
#define	SPECIAL_CASE_SET	1
#define SPECIAL_CASE_SETSW	2
#define SPECIAL_CASE_SETX	3
/* FIXME: sparc-opc.c doesn't have necessary "S" trigger to enable this.  */
#define	SPECIAL_CASE_FDIV	4

/* Bit masks of various insns.  */
#define NOP_INSN 0x01000000
#define OR_INSN 0x80100000
#define XOR_INSN 0x80180000
#define FMOVS_INSN 0x81A00020
#define SETHI_INSN 0x01000000
#define SLLX_INSN 0x81281000
#define SRA_INSN 0x81380000

/* The last instruction to be assembled.  */
static const struct sparc_opcode *last_insn;
/* The assembled opcode of `last_insn'.  */
static unsigned long last_opcode;

/* Handle the set and setuw synthetic instructions.  */

static void
synthetize_setuw (insn)
     const struct sparc_opcode *insn;
{
  int need_hi22_p = 0;
  int rd = (the_insn.opcode & RD (~0)) >> 25;

  if (the_insn.exp.X_op == O_constant)
    {
      if (SPARC_OPCODE_ARCH_V9_P (max_architecture))
	{
	  if (sizeof (offsetT) > 4
	      && (the_insn.exp.X_add_number < 0
		  || the_insn.exp.X_add_number > (offsetT) U0xffffffff))
	    as_warn (_("set: number not in 0..4294967295 range"));
	}
      else
	{
	  if (sizeof (offsetT) > 4
	      && (the_insn.exp.X_add_number < -(offsetT) U0x80000000
		  || the_insn.exp.X_add_number > (offsetT) U0xffffffff))
	    as_warn (_("set: number not in -2147483648..4294967295 range"));
	  the_insn.exp.X_add_number = (int) the_insn.exp.X_add_number;
	}
    }

  /* See if operand is absolute and small; skip sethi if so.  */
  if (the_insn.exp.X_op != O_constant
      || the_insn.exp.X_add_number >= (1 << 12)
      || the_insn.exp.X_add_number < -(1 << 12))
    {
      the_insn.opcode = (SETHI_INSN | RD (rd)
			 | ((the_insn.exp.X_add_number >> 10)
			    & (the_insn.exp.X_op == O_constant
			       ? 0x3fffff : 0)));
      the_insn.reloc = (the_insn.exp.X_op != O_constant
			? BFD_RELOC_HI22 : BFD_RELOC_NONE);
      output_insn (insn, &the_insn);
      need_hi22_p = 1;
    }

  /* See if operand has no low-order bits; skip OR if so.  */
  if (the_insn.exp.X_op != O_constant
      || (need_hi22_p && (the_insn.exp.X_add_number & 0x3FF) != 0)
      || ! need_hi22_p)
    {
      the_insn.opcode = (OR_INSN | (need_hi22_p ? RS1 (rd) : 0)
			 | RD (rd) | IMMED
			 | (the_insn.exp.X_add_number
			    & (the_insn.exp.X_op != O_constant
			       ? 0 : need_hi22_p ? 0x3ff : 0x1fff)));
      the_insn.reloc = (the_insn.exp.X_op != O_constant
			? BFD_RELOC_LO10 : BFD_RELOC_NONE);
      output_insn (insn, &the_insn);
    }
}

/* Handle the setsw synthetic instruction.  */

static void
synthetize_setsw (insn)
     const struct sparc_opcode *insn;
{
  int low32, rd, opc;

  rd = (the_insn.opcode & RD (~0)) >> 25;

  if (the_insn.exp.X_op != O_constant)
    {
      synthetize_setuw (insn);

      /* Need to sign extend it.  */
      the_insn.opcode = (SRA_INSN | RS1 (rd) | RD (rd));
      the_insn.reloc = BFD_RELOC_NONE;
      output_insn (insn, &the_insn);
      return;
    }

  if (sizeof (offsetT) > 4
      && (the_insn.exp.X_add_number < -(offsetT) U0x80000000
	  || the_insn.exp.X_add_number > (offsetT) U0xffffffff))
    as_warn (_("setsw: number not in -2147483648..4294967295 range"));

  low32 = the_insn.exp.X_add_number;

  if (low32 >= 0)
    {
      synthetize_setuw (insn);
      return;
    }

  opc = OR_INSN;

  the_insn.reloc = BFD_RELOC_NONE;
  /* See if operand is absolute and small; skip sethi if so.  */
  if (low32 < -(1 << 12))
    {
      the_insn.opcode = (SETHI_INSN | RD (rd)
			 | (((~the_insn.exp.X_add_number) >> 10) & 0x3fffff));
      output_insn (insn, &the_insn);
      low32 = 0x1c00 | (low32 & 0x3ff);
      opc = RS1 (rd) | XOR_INSN;
    }

  the_insn.opcode = (opc | RD (rd) | IMMED
		     | (low32 & 0x1fff));
  output_insn (insn, &the_insn);
}

/* Handle the setsw synthetic instruction.  */

static void
synthetize_setx (insn)
     const struct sparc_opcode *insn;
{
  int upper32, lower32;
  int tmpreg = (the_insn.opcode & RS1 (~0)) >> 14;
  int dstreg = (the_insn.opcode & RD (~0)) >> 25;
  int upper_dstreg;
  int need_hh22_p = 0, need_hm10_p = 0, need_hi22_p = 0, need_lo10_p = 0;
  int need_xor10_p = 0;

#define SIGNEXT32(x) ((((x) & U0xffffffff) ^ U0x80000000) - U0x80000000)
  lower32 = SIGNEXT32 (the_insn.exp.X_add_number);
  upper32 = SIGNEXT32 (BSR (the_insn.exp.X_add_number, 32));
#undef SIGNEXT32

  upper_dstreg = tmpreg;
  /* The tmp reg should not be the dst reg.  */
  if (tmpreg == dstreg)
    as_warn (_("setx: temporary register same as destination register"));

  /* ??? Obviously there are other optimizations we can do
     (e.g. sethi+shift for 0x1f0000000) and perhaps we shouldn't be
     doing some of these.  Later.  If you do change things, try to
     change all of this to be table driven as well.  */
  /* What to output depends on the number if it's constant.
     Compute that first, then output what we've decided upon.  */
  if (the_insn.exp.X_op != O_constant)
    {
      if (sparc_arch_size == 32)
	{
	  /* When arch size is 32, we want setx to be equivalent
	     to setuw for anything but constants.  */
	  the_insn.exp.X_add_number &= 0xffffffff;
	  synthetize_setuw (insn);
	  return;
	}
      need_hh22_p = need_hm10_p = need_hi22_p = need_lo10_p = 1;
      lower32 = 0;
      upper32 = 0;
    }
  else
    {
      /* Reset X_add_number, we've extracted it as upper32/lower32.
	 Otherwise fixup_segment will complain about not being able to
	 write an 8 byte number in a 4 byte field.  */
      the_insn.exp.X_add_number = 0;

      /* Only need hh22 if `or' insn can't handle constant.  */
      if (upper32 < -(1 << 12) || upper32 >= (1 << 12))
	need_hh22_p = 1;

      /* Does bottom part (after sethi) have bits?  */
      if ((need_hh22_p && (upper32 & 0x3ff) != 0)
	  /* No hh22, but does upper32 still have bits we can't set
	     from lower32?  */
	  || (! need_hh22_p && upper32 != 0 && upper32 != -1))
	need_hm10_p = 1;

      /* If the lower half is all zero, we build the upper half directly
	 into the dst reg.  */
      if (lower32 != 0
	  /* Need lower half if number is zero or 0xffffffff00000000.  */
	  || (! need_hh22_p && ! need_hm10_p))
	{
	  /* No need for sethi if `or' insn can handle constant.  */
	  if (lower32 < -(1 << 12) || lower32 >= (1 << 12)
	      /* Note that we can't use a negative constant in the `or'
		 insn unless the upper 32 bits are all ones.  */
	      || (lower32 < 0 && upper32 != -1)
	      || (lower32 >= 0 && upper32 == -1))
	    need_hi22_p = 1;

	  if (need_hi22_p && upper32 == -1)
	    need_xor10_p = 1;

	  /* Does bottom part (after sethi) have bits?  */
	  else if ((need_hi22_p && (lower32 & 0x3ff) != 0)
		   /* No sethi.  */
		   || (! need_hi22_p && (lower32 & 0x1fff) != 0)
		   /* Need `or' if we didn't set anything else.  */
		   || (! need_hi22_p && ! need_hh22_p && ! need_hm10_p))
	    need_lo10_p = 1;
	}
      else
	/* Output directly to dst reg if lower 32 bits are all zero.  */
	upper_dstreg = dstreg;
    }

  if (!upper_dstreg && dstreg)
    as_warn (_("setx: illegal temporary register g0"));

  if (need_hh22_p)
    {
      the_insn.opcode = (SETHI_INSN | RD (upper_dstreg)
			 | ((upper32 >> 10) & 0x3fffff));
      the_insn.reloc = (the_insn.exp.X_op != O_constant
			? BFD_RELOC_SPARC_HH22 : BFD_RELOC_NONE);
      output_insn (insn, &the_insn);
    }

  if (need_hi22_p)
    {
      the_insn.opcode = (SETHI_INSN | RD (dstreg)
			 | (((need_xor10_p ? ~lower32 : lower32)
			     >> 10) & 0x3fffff));
      the_insn.reloc = (the_insn.exp.X_op != O_constant
			? BFD_RELOC_SPARC_LM22 : BFD_RELOC_NONE);
      output_insn (insn, &the_insn);
    }

  if (need_hm10_p)
    {
      the_insn.opcode = (OR_INSN
			 | (need_hh22_p ? RS1 (upper_dstreg) : 0)
			 | RD (upper_dstreg)
			 | IMMED
			 | (upper32 & (need_hh22_p ? 0x3ff : 0x1fff)));
      the_insn.reloc = (the_insn.exp.X_op != O_constant
			? BFD_RELOC_SPARC_HM10 : BFD_RELOC_NONE);
      output_insn (insn, &the_insn);
    }

  if (need_lo10_p)
    {
      /* FIXME: One nice optimization to do here is to OR the low part
	 with the highpart if hi22 isn't needed and the low part is
	 positive.  */
      the_insn.opcode = (OR_INSN | (need_hi22_p ? RS1 (dstreg) : 0)
			 | RD (dstreg)
			 | IMMED
			 | (lower32 & (need_hi22_p ? 0x3ff : 0x1fff)));
      the_insn.reloc = (the_insn.exp.X_op != O_constant
			? BFD_RELOC_LO10 : BFD_RELOC_NONE);
      output_insn (insn, &the_insn);
    }

  /* If we needed to build the upper part, shift it into place.  */
  if (need_hh22_p || need_hm10_p)
    {
      the_insn.opcode = (SLLX_INSN | RS1 (upper_dstreg) | RD (upper_dstreg)
			 | IMMED | 32);
      the_insn.reloc = BFD_RELOC_NONE;
      output_insn (insn, &the_insn);
    }

  /* To get -1 in upper32, we do sethi %hi(~x), r; xor r, -0x400 | x, r.  */
  if (need_xor10_p)
    {
      the_insn.opcode = (XOR_INSN | RS1 (dstreg) | RD (dstreg) | IMMED
			 | 0x1c00 | (lower32 & 0x3ff));
      the_insn.reloc = BFD_RELOC_NONE;
      output_insn (insn, &the_insn);
    }

  /* If we needed to build both upper and lower parts, OR them together.  */
  else if ((need_hh22_p || need_hm10_p) && (need_hi22_p || need_lo10_p))
    {
      the_insn.opcode = (OR_INSN | RS1 (dstreg) | RS2 (upper_dstreg)
			 | RD (dstreg));
      the_insn.reloc = BFD_RELOC_NONE;
      output_insn (insn, &the_insn);
    }
}

/* Main entry point to assemble one instruction.  */

void
md_assemble (str)
     char *str;
{
  const struct sparc_opcode *insn;
  int special_case;

  know (str);
  special_case = sparc_ip (str, &insn);
  if (insn == NULL)
    return;

  /* We warn about attempts to put a floating point branch in a delay slot,
     unless the delay slot has been annulled.  */
  if (last_insn != NULL
      && (insn->flags & F_FBR) != 0
      && (last_insn->flags & F_DELAYED) != 0
      /* ??? This test isn't completely accurate.  We assume anything with
	 F_{UNBR,CONDBR,FBR} set is annullable.  */
      && ((last_insn->flags & (F_UNBR | F_CONDBR | F_FBR)) == 0
	  || (last_opcode & ANNUL) == 0))
    as_warn (_("FP branch in delay slot"));

  /* SPARC before v9 requires a nop instruction between a floating
     point instruction and a floating point branch.  We insert one
     automatically, with a warning.  */
  if (max_architecture < SPARC_OPCODE_ARCH_V9
      && last_insn != NULL
      && (insn->flags & F_FBR) != 0
      && (last_insn->flags & F_FLOAT) != 0)
    {
      struct sparc_it nop_insn;

      nop_insn.opcode = NOP_INSN;
      nop_insn.reloc = BFD_RELOC_NONE;
      output_insn (insn, &nop_insn);
      as_warn (_("FP branch preceded by FP instruction; NOP inserted"));
    }

  switch (special_case)
    {
    case SPECIAL_CASE_NONE:
      /* Normal insn.  */
      output_insn (insn, &the_insn);
      break;

    case SPECIAL_CASE_SETSW:
      synthetize_setsw (insn);
      break;

    case SPECIAL_CASE_SET:
      synthetize_setuw (insn);
      break;

    case SPECIAL_CASE_SETX:
      synthetize_setx (insn);
      break;

    case SPECIAL_CASE_FDIV:
      {
	int rd = (the_insn.opcode >> 25) & 0x1f;

	output_insn (insn, &the_insn);

	/* According to information leaked from Sun, the "fdiv" instructions
	   on early SPARC machines would produce incorrect results sometimes.
	   The workaround is to add an fmovs of the destination register to
	   itself just after the instruction.  This was true on machines
	   with Weitek 1165 float chips, such as the Sun-4/260 and /280.  */
	assert (the_insn.reloc == BFD_RELOC_NONE);
	the_insn.opcode = FMOVS_INSN | rd | RD (rd);
	output_insn (insn, &the_insn);
	return;
      }

    default:
      as_fatal (_("failed special case insn sanity check"));
    }
}

/* Subroutine of md_assemble to do the actual parsing.  */

static int
sparc_ip (str, pinsn)
     char *str;
     const struct sparc_opcode **pinsn;
{
  char *error_message = "";
  char *s;
  const char *args;
  char c;
  const struct sparc_opcode *insn;
  char *argsStart;
  unsigned long opcode;
  unsigned int mask = 0;
  int match = 0;
  int comma = 0;
  int v9_arg_p;
  int special_case = SPECIAL_CASE_NONE;

  s = str;
  if (ISLOWER (*s))
    {
      do
	++s;
      while (ISLOWER (*s) || ISDIGIT (*s));
    }

  switch (*s)
    {
    case '\0':
      break;

    case ',':
      comma = 1;
      /* Fall through.  */

    case ' ':
      *s++ = '\0';
      break;

    default:
      as_bad (_("Unknown opcode: `%s'"), str);
      *pinsn = NULL;
      return special_case;
    }
  insn = (struct sparc_opcode *) hash_find (op_hash, str);
  *pinsn = insn;
  if (insn == NULL)
    {
      as_bad (_("Unknown opcode: `%s'"), str);
      return special_case;
    }
  if (comma)
    {
      *--s = ',';
    }

  argsStart = s;
  for (;;)
    {
      opcode = insn->match;
      memset (&the_insn, '\0', sizeof (the_insn));
      the_insn.reloc = BFD_RELOC_NONE;
      v9_arg_p = 0;

      /* Build the opcode, checking as we go to make sure that the
         operands match.  */
      for (args = insn->args;; ++args)
	{
	  switch (*args)
	    {
	    case 'K':
	      {
		int kmask = 0;

		/* Parse a series of masks.  */
		if (*s == '#')
		  {
		    while (*s == '#')
		      {
			int mask;

			if (! parse_keyword_arg (sparc_encode_membar, &s,
						 &mask))
			  {
			    error_message = _(": invalid membar mask name");
			    goto error;
			  }
			kmask |= mask;
			while (*s == ' ')
			  ++s;
			if (*s == '|' || *s == '+')
			  ++s;
			while (*s == ' ')
			  ++s;
		      }
		  }
		else
		  {
		    if (! parse_const_expr_arg (&s, &kmask))
		      {
			error_message = _(": invalid membar mask expression");
			goto error;
		      }
		    if (kmask < 0 || kmask > 127)
		      {
			error_message = _(": invalid membar mask number");
			goto error;
		      }
		  }

		opcode |= MEMBAR (kmask);
		continue;
	      }

	    case '3':
	      {
		int smask = 0;

		if (! parse_const_expr_arg (&s, &smask))
		  {
		    error_message = _(": invalid siam mode expression");
		    goto error;
		  }
		if (smask < 0 || smask > 7)
		  {
		    error_message = _(": invalid siam mode number");
		    goto error;
		  }
		opcode |= smask;
		continue;
	      }

	    case '*':
	      {
		int fcn = 0;

		/* Parse a prefetch function.  */
		if (*s == '#')
		  {
		    if (! parse_keyword_arg (sparc_encode_prefetch, &s, &fcn))
		      {
			error_message = _(": invalid prefetch function name");
			goto error;
		      }
		  }
		else
		  {
		    if (! parse_const_expr_arg (&s, &fcn))
		      {
			error_message = _(": invalid prefetch function expression");
			goto error;
		      }
		    if (fcn < 0 || fcn > 31)
		      {
			error_message = _(": invalid prefetch function number");
			goto error;
		      }
		  }
		opcode |= RD (fcn);
		continue;
	      }

	    case '!':
	    case '?':
	      /* Parse a sparc64 privileged register.  */
	      if (*s == '%')
		{
		  struct priv_reg_entry *p = priv_reg_table;
		  unsigned int len = 9999999; /* Init to make gcc happy.  */

		  s += 1;
		  while (p->name[0] > s[0])
		    p++;
		  while (p->name[0] == s[0])
		    {
		      len = strlen (p->name);
		      if (strncmp (p->name, s, len) == 0)
			break;
		      p++;
		    }
		  if (p->name[0] != s[0])
		    {
		      error_message = _(": unrecognizable privileged register");
		      goto error;
		    }
		  if (*args == '?')
		    opcode |= (p->regnum << 14);
		  else
		    opcode |= (p->regnum << 25);
		  s += len;
		  continue;
		}
	      else
		{
		  error_message = _(": unrecognizable privileged register");
		  goto error;
		}

	    case '$':
	    case '%':
	      /* Parse a sparc64 hyperprivileged register.  */
	      if (*s == '%')
		{
		  struct priv_reg_entry *p = hpriv_reg_table;
		  unsigned int len = 9999999; /* Init to make gcc happy.  */

		  s += 1;
		  while (p->name[0] > s[0])
		    p++;
		  while (p->name[0] == s[0])
		    {
		      len = strlen (p->name);
		      if (strncmp (p->name, s, len) == 0)
			break;
		      p++;
		    }
		  if (p->name[0] != s[0])
		    {
		      error_message = _(": unrecognizable hyperprivileged register");
		      goto error;
		    }
		  if (*args == '$')
		    opcode |= (p->regnum << 14);
		  else
		    opcode |= (p->regnum << 25);
		  s += len;
		  continue;
		}
	      else
		{
		  error_message = _(": unrecognizable hyperprivileged register");
		  goto error;
		}

	    case '_':
	    case '/':
	      /* Parse a v9a/v9b ancillary state register.  */
	      if (*s == '%')
		{
		  struct priv_reg_entry *p = v9a_asr_table;
		  unsigned int len = 9999999; /* Init to make gcc happy.  */

		  s += 1;
		  while (p->name[0] > s[0])
		    p++;
		  while (p->name[0] == s[0])
		    {
		      len = strlen (p->name);
		      if (strncmp (p->name, s, len) == 0)
			break;
		      p++;
		    }
		  if (p->name[0] != s[0])
		    {
		      error_message = _(": unrecognizable v9a or v9b ancillary state register");
		      goto error;
		    }
		  if (*args == '/' && (p->regnum == 20 || p->regnum == 21))
		    {
		      error_message = _(": rd on write only ancillary state register");
		      goto error;
		    }
		  if (p->regnum >= 24
		      && (insn->architecture
			  & SPARC_OPCODE_ARCH_MASK (SPARC_OPCODE_ARCH_V9A)))
		    {
		      /* %sys_tick and %sys_tick_cmpr are v9bnotv9a */
		      error_message = _(": unrecognizable v9a ancillary state register");
		      goto error;
		    }
		  if (*args == '/')
		    opcode |= (p->regnum << 14);
		  else
		    opcode |= (p->regnum << 25);
		  s += len;
		  continue;
		}
	      else
		{
		  error_message = _(": unrecognizable v9a or v9b ancillary state register");
		  goto error;
		}

	    case 'M':
	    case 'm':
	      if (strncmp (s, "%asr", 4) == 0)
		{
		  s += 4;

		  if (ISDIGIT (*s))
		    {
		      long num = 0;

		      while (ISDIGIT (*s))
			{
			  num = num * 10 + *s - '0';
			  ++s;
			}

		      if (current_architecture >= SPARC_OPCODE_ARCH_V9)
			{
			  if (num < 16 || 31 < num)
			    {
			      error_message = _(": asr number must be between 16 and 31");
			      goto error;
			    }
			}
		      else
			{
			  if (num < 0 || 31 < num)
			    {
			      error_message = _(": asr number must be between 0 and 31");
			      goto error;
			    }
			}

		      opcode |= (*args == 'M' ? RS1 (num) : RD (num));
		      continue;
		    }
		  else
		    {
		      error_message = _(": expecting %asrN");
		      goto error;
		    }
		} /* if %asr  */
	      break;

	    case 'I':
	      the_insn.reloc = BFD_RELOC_SPARC_11;
	      goto immediate;

	    case 'j':
	      the_insn.reloc = BFD_RELOC_SPARC_10;
	      goto immediate;

	    case 'X':
	      /* V8 systems don't understand BFD_RELOC_SPARC_5.  */
	      if (SPARC_OPCODE_ARCH_V9_P (max_architecture))
		the_insn.reloc = BFD_RELOC_SPARC_5;
	      else
		the_insn.reloc = BFD_RELOC_SPARC13;
	      /* These fields are unsigned, but for upward compatibility,
		 allow negative values as well.  */
	      goto immediate;

	    case 'Y':
	      /* V8 systems don't understand BFD_RELOC_SPARC_6.  */
	      if (SPARC_OPCODE_ARCH_V9_P (max_architecture))
		the_insn.reloc = BFD_RELOC_SPARC_6;
	      else
		the_insn.reloc = BFD_RELOC_SPARC13;
	      /* These fields are unsigned, but for upward compatibility,
		 allow negative values as well.  */
	      goto immediate;

	    case 'k':
	      the_insn.reloc = /* RELOC_WDISP2_14 */ BFD_RELOC_SPARC_WDISP16;
	      the_insn.pcrel = 1;
	      goto immediate;

	    case 'G':
	      the_insn.reloc = BFD_RELOC_SPARC_WDISP19;
	      the_insn.pcrel = 1;
	      goto immediate;

	    case 'N':
	      if (*s == 'p' && s[1] == 'n')
		{
		  s += 2;
		  continue;
		}
	      break;

	    case 'T':
	      if (*s == 'p' && s[1] == 't')
		{
		  s += 2;
		  continue;
		}
	      break;

	    case 'z':
	      if (*s == ' ')
		{
		  ++s;
		}
	      if (strncmp (s, "%icc", 4) == 0)
		{
		  s += 4;
		  continue;
		}
	      break;

	    case 'Z':
	      if (*s == ' ')
		{
		  ++s;
		}
	      if (strncmp (s, "%xcc", 4) == 0)
		{
		  s += 4;
		  continue;
		}
	      break;

	    case '6':
	      if (*s == ' ')
		{
		  ++s;
		}
	      if (strncmp (s, "%fcc0", 5) == 0)
		{
		  s += 5;
		  continue;
		}
	      break;

	    case '7':
	      if (*s == ' ')
		{
		  ++s;
		}
	      if (strncmp (s, "%fcc1", 5) == 0)
		{
		  s += 5;
		  continue;
		}
	      break;

	    case '8':
	      if (*s == ' ')
		{
		  ++s;
		}
	      if (strncmp (s, "%fcc2", 5) == 0)
		{
		  s += 5;
		  continue;
		}
	      break;

	    case '9':
	      if (*s == ' ')
		{
		  ++s;
		}
	      if (strncmp (s, "%fcc3", 5) == 0)
		{
		  s += 5;
		  continue;
		}
	      break;

	    case 'P':
	      if (strncmp (s, "%pc", 3) == 0)
		{
		  s += 3;
		  continue;
		}
	      break;

	    case 'W':
	      if (strncmp (s, "%tick", 5) == 0)
		{
		  s += 5;
		  continue;
		}
	      break;

	    case '\0':		/* End of args.  */
	      if (s[0] == ',' && s[1] == '%')
		{
		  static const struct tls_ops
		  {
		    /* The name as it appears in assembler.  */
		    char *name;
		    /* strlen (name), precomputed for speed */
		    int len;
		    /* The reloc this pseudo-op translates to.  */
		    int reloc;
		    /* 1 if call.  */
		    int call;
		  }
		  tls_ops[] =
		  {
		    { "tgd_add", 7, BFD_RELOC_SPARC_TLS_GD_ADD, 0 },
		    { "tgd_call", 8, BFD_RELOC_SPARC_TLS_GD_CALL, 1 },
		    { "tldm_add", 8, BFD_RELOC_SPARC_TLS_LDM_ADD, 0 },
		    { "tldm_call", 9, BFD_RELOC_SPARC_TLS_LDM_CALL, 1 },
		    { "tldo_add", 8, BFD_RELOC_SPARC_TLS_LDO_ADD, 0 },
		    { "tie_ldx", 7, BFD_RELOC_SPARC_TLS_IE_LDX, 0 },
		    { "tie_ld", 6, BFD_RELOC_SPARC_TLS_IE_LD, 0 },
		    { "tie_add", 7, BFD_RELOC_SPARC_TLS_IE_ADD, 0 },
		    { NULL, 0, 0, 0 }
		  };
		  const struct tls_ops *o;
		  char *s1;
		  int npar = 0;

		  for (o = tls_ops; o->name; o++)
		    if (strncmp (s + 2, o->name, o->len) == 0)
		      break;
		  if (o->name == NULL)
		    break;

		  if (s[o->len + 2] != '(')
		    {
		      as_bad (_("Illegal operands: %%%s requires arguments in ()"), o->name);
		      return special_case;
		    }

		  if (! o->call && the_insn.reloc != BFD_RELOC_NONE)
		    {
		      as_bad (_("Illegal operands: %%%s cannot be used together with other relocs in the insn ()"),
			      o->name);
		      return special_case;
		    }

		  if (o->call
		      && (the_insn.reloc != BFD_RELOC_32_PCREL_S2
			  || the_insn.exp.X_add_number != 0
			  || the_insn.exp.X_add_symbol
			     != symbol_find_or_make ("__tls_get_addr")))
		    {
		      as_bad (_("Illegal operands: %%%s can be only used with call __tls_get_addr"),
			      o->name);
		      return special_case;
		    }

		  the_insn.reloc = o->reloc;
		  memset (&the_insn.exp, 0, sizeof (the_insn.exp));
		  s += o->len + 3;

		  for (s1 = s; *s1 && *s1 != ',' && *s1 != ']'; s1++)
		    if (*s1 == '(')
		      npar++;
		    else if (*s1 == ')')
		      {
			if (!npar)
			  break;
			npar--;
		      }

		  if (*s1 != ')')
		    {
		      as_bad (_("Illegal operands: %%%s requires arguments in ()"), o->name);
		      return special_case;
		    }

		  *s1 = '\0';
		  (void) get_expression (s);
		  *s1 = ')';
		  s = s1 + 1;
		}
	      if (*s == '\0')
		match = 1;
	      break;

	    case '+':
	      if (*s == '+')
		{
		  ++s;
		  continue;
		}
	      if (*s == '-')
		{
		  continue;
		}
	      break;

	    case '[':		/* These must match exactly.  */
	    case ']':
	    case ',':
	    case ' ':
	      if (*s++ == *args)
		continue;
	      break;

	    case '#':		/* Must be at least one digit.  */
	      if (ISDIGIT (*s++))
		{
		  while (ISDIGIT (*s))
		    {
		      ++s;
		    }
		  continue;
		}
	      break;

	    case 'C':		/* Coprocessor state register.  */
	      if (strncmp (s, "%csr", 4) == 0)
		{
		  s += 4;
		  continue;
		}
	      break;

	    case 'b':		/* Next operand is a coprocessor register.  */
	    case 'c':
	    case 'D':
	      if (*s++ == '%' && *s++ == 'c' && ISDIGIT (*s))
		{
		  mask = *s++;
		  if (ISDIGIT (*s))
		    {
		      mask = 10 * (mask - '0') + (*s++ - '0');
		      if (mask >= 32)
			{
			  break;
			}
		    }
		  else
		    {
		      mask -= '0';
		    }
		  switch (*args)
		    {

		    case 'b':
		      opcode |= mask << 14;
		      continue;

		    case 'c':
		      opcode |= mask;
		      continue;

		    case 'D':
		      opcode |= mask << 25;
		      continue;
		    }
		}
	      break;

	    case 'r':		/* next operand must be a register */
	    case 'O':
	    case '1':
	    case '2':
	    case 'd':
	      if (*s++ == '%')
		{
		  switch (c = *s++)
		    {

		    case 'f':	/* frame pointer */
		      if (*s++ == 'p')
			{
			  mask = 0x1e;
			  break;
			}
		      goto error;

		    case 'g':	/* global register */
		      c = *s++;
		      if (isoctal (c))
			{
			  mask = c - '0';
			  break;
			}
		      goto error;

		    case 'i':	/* in register */
		      c = *s++;
		      if (isoctal (c))
			{
			  mask = c - '0' + 24;
			  break;
			}
		      goto error;

		    case 'l':	/* local register */
		      c = *s++;
		      if (isoctal (c))
			{
			  mask = (c - '0' + 16);
			  break;
			}
		      goto error;

		    case 'o':	/* out register */
		      c = *s++;
		      if (isoctal (c))
			{
			  mask = (c - '0' + 8);
			  break;
			}
		      goto error;

		    case 's':	/* stack pointer */
		      if (*s++ == 'p')
			{
			  mask = 0xe;
			  break;
			}
		      goto error;

		    case 'r':	/* any register */
		      if (!ISDIGIT ((c = *s++)))
			{
			  goto error;
			}
		      /* FALLTHROUGH */
		    case '0':
		    case '1':
		    case '2':
		    case '3':
		    case '4':
		    case '5':
		    case '6':
		    case '7':
		    case '8':
		    case '9':
		      if (ISDIGIT (*s))
			{
			  if ((c = 10 * (c - '0') + (*s++ - '0')) >= 32)
			    {
			      goto error;
			    }
			}
		      else
			{
			  c -= '0';
			}
		      mask = c;
		      break;

		    default:
		      goto error;
		    }

		  if ((mask & ~1) == 2 && sparc_arch_size == 64
		      && no_undeclared_regs && ! globals[mask])
		    as_bad (_("detected global register use not covered by .register pseudo-op"));

		  /* Got the register, now figure out where
		     it goes in the opcode.  */
		  switch (*args)
		    {
		    case '1':
		      opcode |= mask << 14;
		      continue;

		    case '2':
		      opcode |= mask;
		      continue;

		    case 'd':
		      opcode |= mask << 25;
		      continue;

		    case 'r':
		      opcode |= (mask << 25) | (mask << 14);
		      continue;

		    case 'O':
		      opcode |= (mask << 25) | (mask << 0);
		      continue;
		    }
		}
	      break;

	    case 'e':		/* next operand is a floating point register */
	    case 'v':
	    case 'V':

	    case 'f':
	    case 'B':
	    case 'R':

	    case 'g':
	    case 'H':
	    case 'J':
	      {
		char format;

		if (*s++ == '%'
		    && ((format = *s) == 'f')
		    && ISDIGIT (*++s))
		  {
		    for (mask = 0; ISDIGIT (*s); ++s)
		      {
			mask = 10 * mask + (*s - '0');
		      }		/* read the number */

		    if ((*args == 'v'
			 || *args == 'B'
			 || *args == 'H')
			&& (mask & 1))
		      {
			break;
		      }		/* register must be even numbered */

		    if ((*args == 'V'
			 || *args == 'R'
			 || *args == 'J')
			&& (mask & 3))
		      {
			break;
		      }		/* register must be multiple of 4 */

		    if (mask >= 64)
		      {
			if (SPARC_OPCODE_ARCH_V9_P (max_architecture))
			  error_message = _(": There are only 64 f registers; [0-63]");
			else
			  error_message = _(": There are only 32 f registers; [0-31]");
			goto error;
		      }	/* on error */
		    else if (mask >= 32)
		      {
			if (SPARC_OPCODE_ARCH_V9_P (max_architecture))
			  {
			    if (*args == 'e' || *args == 'f' || *args == 'g')
			      {
				error_message
				  = _(": There are only 32 single precision f registers; [0-31]");
				goto error;
			      }
			    v9_arg_p = 1;
			    mask -= 31;	/* wrap high bit */
			  }
			else
			  {
			    error_message = _(": There are only 32 f registers; [0-31]");
			    goto error;
			  }
		      }
		  }
		else
		  {
		    break;
		  }	/* if not an 'f' register.  */

		switch (*args)
		  {
		  case 'v':
		  case 'V':
		  case 'e':
		    opcode |= RS1 (mask);
		    continue;

		  case 'f':
		  case 'B':
		  case 'R':
		    opcode |= RS2 (mask);
		    continue;

		  case 'g':
		  case 'H':
		  case 'J':
		    opcode |= RD (mask);
		    continue;
		  }		/* Pack it in.  */

		know (0);
		break;
	      }			/* float arg  */

	    case 'F':
	      if (strncmp (s, "%fsr", 4) == 0)
		{
		  s += 4;
		  continue;
		}
	      break;

	    case '0':		/* 64 bit immediate (set, setsw, setx insn)  */
	      the_insn.reloc = BFD_RELOC_NONE; /* reloc handled elsewhere  */
	      goto immediate;

	    case 'l':		/* 22 bit PC relative immediate  */
	      the_insn.reloc = BFD_RELOC_SPARC_WDISP22;
	      the_insn.pcrel = 1;
	      goto immediate;

	    case 'L':		/* 30 bit immediate  */
	      the_insn.reloc = BFD_RELOC_32_PCREL_S2;
	      the_insn.pcrel = 1;
	      goto immediate;

	    case 'h':
	    case 'n':		/* 22 bit immediate  */
	      the_insn.reloc = BFD_RELOC_SPARC22;
	      goto immediate;

	    case 'i':		/* 13 bit immediate  */
	      the_insn.reloc = BFD_RELOC_SPARC13;

	      /* fallthrough */

	    immediate:
	      if (*s == ' ')
		s++;

	      {
		char *s1;
		char *op_arg = NULL;
		static expressionS op_exp;
		bfd_reloc_code_real_type old_reloc = the_insn.reloc;

		/* Check for %hi, etc.  */
		if (*s == '%')
		  {
		    static const struct ops {
		      /* The name as it appears in assembler.  */
		      char *name;
		      /* strlen (name), precomputed for speed */
		      int len;
		      /* The reloc this pseudo-op translates to.  */
		      int reloc;
		      /* Non-zero if for v9 only.  */
		      int v9_p;
		      /* Non-zero if can be used in pc-relative contexts.  */
		      int pcrel_p;/*FIXME:wip*/
		    } ops[] = {
		      /* hix/lox must appear before hi/lo so %hix won't be
			 mistaken for %hi.  */
		      { "hix", 3, BFD_RELOC_SPARC_HIX22, 1, 0 },
		      { "lox", 3, BFD_RELOC_SPARC_LOX10, 1, 0 },
		      { "hi", 2, BFD_RELOC_HI22, 0, 1 },
		      { "lo", 2, BFD_RELOC_LO10, 0, 1 },
		      { "hh", 2, BFD_RELOC_SPARC_HH22, 1, 1 },
		      { "hm", 2, BFD_RELOC_SPARC_HM10, 1, 1 },
		      { "lm", 2, BFD_RELOC_SPARC_LM22, 1, 1 },
		      { "h44", 3, BFD_RELOC_SPARC_H44, 1, 0 },
		      { "m44", 3, BFD_RELOC_SPARC_M44, 1, 0 },
		      { "l44", 3, BFD_RELOC_SPARC_L44, 1, 0 },
		      { "uhi", 3, BFD_RELOC_SPARC_HH22, 1, 0 },
		      { "ulo", 3, BFD_RELOC_SPARC_HM10, 1, 0 },
		      { "tgd_hi22", 8, BFD_RELOC_SPARC_TLS_GD_HI22, 0, 0 },
		      { "tgd_lo10", 8, BFD_RELOC_SPARC_TLS_GD_LO10, 0, 0 },
		      { "tldm_hi22", 9, BFD_RELOC_SPARC_TLS_LDM_HI22, 0, 0 },
		      { "tldm_lo10", 9, BFD_RELOC_SPARC_TLS_LDM_LO10, 0, 0 },
		      { "tldo_hix22", 10, BFD_RELOC_SPARC_TLS_LDO_HIX22, 0,
									 0 },
		      { "tldo_lox10", 10, BFD_RELOC_SPARC_TLS_LDO_LOX10, 0,
									 0 },
		      { "tie_hi22", 8, BFD_RELOC_SPARC_TLS_IE_HI22, 0, 0 },
		      { "tie_lo10", 8, BFD_RELOC_SPARC_TLS_IE_LO10, 0, 0 },
		      { "tle_hix22", 9, BFD_RELOC_SPARC_TLS_LE_HIX22, 0, 0 },
		      { "tle_lox10", 9, BFD_RELOC_SPARC_TLS_LE_LOX10, 0, 0 },
		      { NULL, 0, 0, 0, 0 }
		    };
		    const struct ops *o;

		    for (o = ops; o->name; o++)
		      if (strncmp (s + 1, o->name, o->len) == 0)
			break;
		    if (o->name == NULL)
		      break;

		    if (s[o->len + 1] != '(')
		      {
			as_bad (_("Illegal operands: %%%s requires arguments in ()"), o->name);
			return special_case;
		      }

		    op_arg = o->name;
		    the_insn.reloc = o->reloc;
		    s += o->len + 2;
		    v9_arg_p = o->v9_p;
		  }

		/* Note that if the get_expression() fails, we will still
		   have created U entries in the symbol table for the
		   'symbols' in the input string.  Try not to create U
		   symbols for registers, etc.  */

		/* This stuff checks to see if the expression ends in
		   +%reg.  If it does, it removes the register from
		   the expression, and re-sets 's' to point to the
		   right place.  */

		if (op_arg)
		  {
		    int npar = 0;

		    for (s1 = s; *s1 && *s1 != ',' && *s1 != ']'; s1++)
		      if (*s1 == '(')
			npar++;
		      else if (*s1 == ')')
			{
			  if (!npar)
			    break;
			  npar--;
			}

		    if (*s1 != ')')
		      {
			as_bad (_("Illegal operands: %%%s requires arguments in ()"), op_arg);
			return special_case;
		      }

		    *s1 = '\0';
		    (void) get_expression (s);
		    *s1 = ')';
		    s = s1 + 1;
		    if (*s == ',' || *s == ']' || !*s)
		      continue;
		    if (*s != '+' && *s != '-')
		      {
			as_bad (_("Illegal operands: Can't do arithmetics other than + and - involving %%%s()"), op_arg);
			return special_case;
		      }
		    *s1 = '0';
		    s = s1;
		    op_exp = the_insn.exp;
		    memset (&the_insn.exp, 0, sizeof (the_insn.exp));
		  }

		for (s1 = s; *s1 && *s1 != ',' && *s1 != ']'; s1++)
		  ;

		if (s1 != s && ISDIGIT (s1[-1]))
		  {
		    if (s1[-2] == '%' && s1[-3] == '+')
		      s1 -= 3;
		    else if (strchr ("goli0123456789", s1[-2]) && s1[-3] == '%' && s1[-4] == '+')
		      s1 -= 4;
		    else
		      s1 = NULL;
		    if (s1)
		      {
			*s1 = '\0';
			if (op_arg && s1 == s + 1)
			  the_insn.exp.X_op = O_absent;
			else
			  (void) get_expression (s);
			*s1 = '+';
			if (op_arg)
			  *s = ')';
			s = s1;
		      }
		  }
		else
		  s1 = NULL;

		if (!s1)
		  {
		    (void) get_expression (s);
		    if (op_arg)
		      *s = ')';
		    s = expr_end;
		  }

		if (op_arg)
		  {
		    the_insn.exp2 = the_insn.exp;
		    the_insn.exp = op_exp;
		    if (the_insn.exp2.X_op == O_absent)
		      the_insn.exp2.X_op = O_illegal;
		    else if (the_insn.exp.X_op == O_absent)
		      {
			the_insn.exp = the_insn.exp2;
			the_insn.exp2.X_op = O_illegal;
		      }
		    else if (the_insn.exp.X_op == O_constant)
		      {
			valueT val = the_insn.exp.X_add_number;
			switch (the_insn.reloc)
			  {
			  default:
			    break;

			  case BFD_RELOC_SPARC_HH22:
			    val = BSR (val, 32);
			    /* Fall through.  */

			  case BFD_RELOC_SPARC_LM22:
			  case BFD_RELOC_HI22:
			    val = (val >> 10) & 0x3fffff;
			    break;

			  case BFD_RELOC_SPARC_HM10:
			    val = BSR (val, 32);
			    /* Fall through.  */

			  case BFD_RELOC_LO10:
			    val &= 0x3ff;
			    break;

			  case BFD_RELOC_SPARC_H44:
			    val >>= 22;
			    val &= 0x3fffff;
			    break;

			  case BFD_RELOC_SPARC_M44:
			    val >>= 12;
			    val &= 0x3ff;
			    break;

			  case BFD_RELOC_SPARC_L44:
			    val &= 0xfff;
			    break;

			  case BFD_RELOC_SPARC_HIX22:
			    val = ~val;
			    val = (val >> 10) & 0x3fffff;
			    break;

			  case BFD_RELOC_SPARC_LOX10:
			    val = (val & 0x3ff) | 0x1c00;
			    break;
			  }
			the_insn.exp = the_insn.exp2;
			the_insn.exp.X_add_number += val;
			the_insn.exp2.X_op = O_illegal;
			the_insn.reloc = old_reloc;
		      }
		    else if (the_insn.exp2.X_op != O_constant)
		      {
			as_bad (_("Illegal operands: Can't add non-constant expression to %%%s()"), op_arg);
			return special_case;
		      }
		    else
		      {
			if (old_reloc != BFD_RELOC_SPARC13
			    || the_insn.reloc != BFD_RELOC_LO10
			    || sparc_arch_size != 64
			    || sparc_pic_code)
			  {
			    as_bad (_("Illegal operands: Can't do arithmetics involving %%%s() of a relocatable symbol"), op_arg);
			    return special_case;
			  }
			the_insn.reloc = BFD_RELOC_SPARC_OLO10;
		      }
		  }
	      }
	      /* Check for constants that don't require emitting a reloc.  */
	      if (the_insn.exp.X_op == O_constant
		  && the_insn.exp.X_add_symbol == 0
		  && the_insn.exp.X_op_symbol == 0)
		{
		  /* For pc-relative call instructions, we reject
		     constants to get better code.  */
		  if (the_insn.pcrel
		      && the_insn.reloc == BFD_RELOC_32_PCREL_S2
		      && in_signed_range (the_insn.exp.X_add_number, 0x3fff))
		    {
		      error_message = _(": PC-relative operand can't be a constant");
		      goto error;
		    }

		  if (the_insn.reloc >= BFD_RELOC_SPARC_TLS_GD_HI22
		      && the_insn.reloc <= BFD_RELOC_SPARC_TLS_TPOFF64)
		    {
		      error_message = _(": TLS operand can't be a constant");
		      goto error;
		    }

		  /* Constants that won't fit are checked in md_apply_fix
		     and bfd_install_relocation.
		     ??? It would be preferable to install the constants
		     into the insn here and save having to create a fixS
		     for each one.  There already exists code to handle
		     all the various cases (e.g. in md_apply_fix and
		     bfd_install_relocation) so duplicating all that code
		     here isn't right.  */
		}

	      continue;

	    case 'a':
	      if (*s++ == 'a')
		{
		  opcode |= ANNUL;
		  continue;
		}
	      break;

	    case 'A':
	      {
		int asi = 0;

		/* Parse an asi.  */
		if (*s == '#')
		  {
		    if (! parse_keyword_arg (sparc_encode_asi, &s, &asi))
		      {
			error_message = _(": invalid ASI name");
			goto error;
		      }
		  }
		else
		  {
		    if (! parse_const_expr_arg (&s, &asi))
		      {
			error_message = _(": invalid ASI expression");
			goto error;
		      }
		    if (asi < 0 || asi > 255)
		      {
			error_message = _(": invalid ASI number");
			goto error;
		      }
		  }
		opcode |= ASI (asi);
		continue;
	      }			/* Alternate space.  */

	    case 'p':
	      if (strncmp (s, "%psr", 4) == 0)
		{
		  s += 4;
		  continue;
		}
	      break;

	    case 'q':		/* Floating point queue.  */
	      if (strncmp (s, "%fq", 3) == 0)
		{
		  s += 3;
		  continue;
		}
	      break;

	    case 'Q':		/* Coprocessor queue.  */
	      if (strncmp (s, "%cq", 3) == 0)
		{
		  s += 3;
		  continue;
		}
	      break;

	    case 'S':
	      if (strcmp (str, "set") == 0
		  || strcmp (str, "setuw") == 0)
		{
		  special_case = SPECIAL_CASE_SET;
		  continue;
		}
	      else if (strcmp (str, "setsw") == 0)
		{
		  special_case = SPECIAL_CASE_SETSW;
		  continue;
		}
	      else if (strcmp (str, "setx") == 0)
		{
		  special_case = SPECIAL_CASE_SETX;
		  continue;
		}
	      else if (strncmp (str, "fdiv", 4) == 0)
		{
		  special_case = SPECIAL_CASE_FDIV;
		  continue;
		}
	      break;

	    case 'o':
	      if (strncmp (s, "%asi", 4) != 0)
		break;
	      s += 4;
	      continue;

	    case 's':
	      if (strncmp (s, "%fprs", 5) != 0)
		break;
	      s += 5;
	      continue;

	    case 'E':
	      if (strncmp (s, "%ccr", 4) != 0)
		break;
	      s += 4;
	      continue;

	    case 't':
	      if (strncmp (s, "%tbr", 4) != 0)
		break;
	      s += 4;
	      continue;

	    case 'w':
	      if (strncmp (s, "%wim", 4) != 0)
		break;
	      s += 4;
	      continue;

	    case 'x':
	      {
		char *push = input_line_pointer;
		expressionS e;

		input_line_pointer = s;
		expression (&e);
		if (e.X_op == O_constant)
		  {
		    int n = e.X_add_number;
		    if (n != e.X_add_number || (n & ~0x1ff) != 0)
		      as_bad (_("OPF immediate operand out of range (0-0x1ff)"));
		    else
		      opcode |= e.X_add_number << 5;
		  }
		else
		  as_bad (_("non-immediate OPF operand, ignored"));
		s = input_line_pointer;
		input_line_pointer = push;
		continue;
	      }

	    case 'y':
	      if (strncmp (s, "%y", 2) != 0)
		break;
	      s += 2;
	      continue;

	    case 'u':
	    case 'U':
	      {
		/* Parse a sparclet cpreg.  */
		int cpreg;
		if (! parse_keyword_arg (sparc_encode_sparclet_cpreg, &s, &cpreg))
		  {
		    error_message = _(": invalid cpreg name");
		    goto error;
		  }
		opcode |= (*args == 'U' ? RS1 (cpreg) : RD (cpreg));
		continue;
	      }

	    default:
	      as_fatal (_("failed sanity check."));
	    }			/* switch on arg code.  */

	  /* Break out of for() loop.  */
	  break;
	}			/* For each arg that we expect.  */

    error:
      if (match == 0)
	{
	  /* Args don't match.  */
	  if (&insn[1] - sparc_opcodes < sparc_num_opcodes
	      && (insn->name == insn[1].name
		  || !strcmp (insn->name, insn[1].name)))
	    {
	      ++insn;
	      s = argsStart;
	      continue;
	    }
	  else
	    {
	      as_bad (_("Illegal operands%s"), error_message);
	      return special_case;
	    }
	}
      else
	{
	  /* We have a match.  Now see if the architecture is OK.  */
	  int needed_arch_mask = insn->architecture;

	  if (v9_arg_p)
	    {
	      needed_arch_mask &=
		~(SPARC_OPCODE_ARCH_MASK (SPARC_OPCODE_ARCH_V9) - 1);
	      if (! needed_arch_mask)
		needed_arch_mask =
		  SPARC_OPCODE_ARCH_MASK (SPARC_OPCODE_ARCH_V9);
	    }

	  if (needed_arch_mask
	      & SPARC_OPCODE_SUPPORTED (current_architecture))
	    /* OK.  */
	    ;
	  /* Can we bump up the architecture?  */
	  else if (needed_arch_mask
		   & SPARC_OPCODE_SUPPORTED (max_architecture))
	    {
	      enum sparc_opcode_arch_val needed_architecture =
		sparc_ffs (SPARC_OPCODE_SUPPORTED (max_architecture)
			   & needed_arch_mask);

	      assert (needed_architecture <= SPARC_OPCODE_ARCH_MAX);
	      if (warn_on_bump
		  && needed_architecture > warn_after_architecture)
		{
		  as_warn (_("architecture bumped from \"%s\" to \"%s\" on \"%s\""),
			   sparc_opcode_archs[current_architecture].name,
			   sparc_opcode_archs[needed_architecture].name,
			   str);
		  warn_after_architecture = needed_architecture;
		}
	      current_architecture = needed_architecture;
	    }
	  /* Conflict.  */
	  /* ??? This seems to be a bit fragile.  What if the next entry in
	     the opcode table is the one we want and it is supported?
	     It is possible to arrange the table today so that this can't
	     happen but what about tomorrow?  */
	  else
	    {
	      int arch, printed_one_p = 0;
	      char *p;
	      char required_archs[SPARC_OPCODE_ARCH_MAX * 16];

	      /* Create a list of the architectures that support the insn.  */
	      needed_arch_mask &= ~SPARC_OPCODE_SUPPORTED (max_architecture);
	      p = required_archs;
	      arch = sparc_ffs (needed_arch_mask);
	      while ((1 << arch) <= needed_arch_mask)
		{
		  if ((1 << arch) & needed_arch_mask)
		    {
		      if (printed_one_p)
			*p++ = '|';
		      strcpy (p, sparc_opcode_archs[arch].name);
		      p += strlen (p);
		      printed_one_p = 1;
		    }
		  ++arch;
		}

	      as_bad (_("Architecture mismatch on \"%s\"."), str);
	      as_tsktsk (_(" (Requires %s; requested architecture is %s.)"),
			 required_archs,
			 sparc_opcode_archs[max_architecture].name);
	      return special_case;
	    }
	} /* If no match.  */

      break;
    } /* Forever looking for a match.  */

  the_insn.opcode = opcode;
  return special_case;
}

/* Parse an argument that can be expressed as a keyword.
   (eg: #StoreStore or %ccfr).
   The result is a boolean indicating success.
   If successful, INPUT_POINTER is updated.  */

static int
parse_keyword_arg (lookup_fn, input_pointerP, valueP)
     int (*lookup_fn) PARAMS ((const char *));
     char **input_pointerP;
     int *valueP;
{
  int value;
  char c, *p, *q;

  p = *input_pointerP;
  for (q = p + (*p == '#' || *p == '%');
       ISALNUM (*q) || *q == '_';
       ++q)
    continue;
  c = *q;
  *q = 0;
  value = (*lookup_fn) (p);
  *q = c;
  if (value == -1)
    return 0;
  *valueP = value;
  *input_pointerP = q;
  return 1;
}

/* Parse an argument that is a constant expression.
   The result is a boolean indicating success.  */

static int
parse_const_expr_arg (input_pointerP, valueP)
     char **input_pointerP;
     int *valueP;
{
  char *save = input_line_pointer;
  expressionS exp;

  input_line_pointer = *input_pointerP;
  /* The next expression may be something other than a constant
     (say if we're not processing the right variant of the insn).
     Don't call expression unless we're sure it will succeed as it will
     signal an error (which we want to defer until later).  */
  /* FIXME: It might be better to define md_operand and have it recognize
     things like %asi, etc. but continuing that route through to the end
     is a lot of work.  */
  if (*input_line_pointer == '%')
    {
      input_line_pointer = save;
      return 0;
    }
  expression (&exp);
  *input_pointerP = input_line_pointer;
  input_line_pointer = save;
  if (exp.X_op != O_constant)
    return 0;
  *valueP = exp.X_add_number;
  return 1;
}

/* Subroutine of sparc_ip to parse an expression.  */

static int
get_expression (str)
     char *str;
{
  char *save_in;
  segT seg;

  save_in = input_line_pointer;
  input_line_pointer = str;
  seg = expression (&the_insn.exp);
  if (seg != absolute_section
      && seg != text_section
      && seg != data_section
      && seg != bss_section
      && seg != undefined_section)
    {
      the_insn.error = _("bad segment");
      expr_end = input_line_pointer;
      input_line_pointer = save_in;
      return 1;
    }
  expr_end = input_line_pointer;
  input_line_pointer = save_in;
  return 0;
}

/* Subroutine of md_assemble to output one insn.  */

static void
output_insn (insn, the_insn)
     const struct sparc_opcode *insn;
     struct sparc_it *the_insn;
{
  char *toP = frag_more (4);

  /* Put out the opcode.  */
  if (INSN_BIG_ENDIAN)
    number_to_chars_bigendian (toP, (valueT) the_insn->opcode, 4);
  else
    number_to_chars_littleendian (toP, (valueT) the_insn->opcode, 4);

  /* Put out the symbol-dependent stuff.  */
  if (the_insn->reloc != BFD_RELOC_NONE)
    {
      fixS *fixP =  fix_new_exp (frag_now,	/* Which frag.  */
				 (toP - frag_now->fr_literal),	/* Where.  */
				 4,		/* Size.  */
				 &the_insn->exp,
				 the_insn->pcrel,
				 the_insn->reloc);
      /* Turn off overflow checking in fixup_segment.  We'll do our
	 own overflow checking in md_apply_fix.  This is necessary because
	 the insn size is 4 and fixup_segment will signal an overflow for
	 large 8 byte quantities.  */
      fixP->fx_no_overflow = 1;
      if (the_insn->reloc == BFD_RELOC_SPARC_OLO10)
	fixP->tc_fix_data = the_insn->exp2.X_add_number;
    }

  last_insn = insn;
  last_opcode = the_insn->opcode;

#ifdef OBJ_ELF
  dwarf2_emit_insn (4);
#endif
}

/* This is identical to the md_atof in m68k.c.  I think this is right,
   but I'm not sure.

   Turn a string in input_line_pointer into a floating point constant
   of type TYPE, and store the appropriate bytes in *LITP.  The number
   of LITTLENUMS emitted is stored in *SIZEP.  An error message is
   returned, or NULL on OK.  */

/* Equal to MAX_PRECISION in atof-ieee.c.  */
#define MAX_LITTLENUMS 6

char *
md_atof (type, litP, sizeP)
     char type;
     char *litP;
     int *sizeP;
{
  int i, prec;
  LITTLENUM_TYPE words[MAX_LITTLENUMS];
  char *t;

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

    case 'x':
    case 'X':
      prec = 6;
      break;

    case 'p':
    case 'P':
      prec = 6;
      break;

    default:
      *sizeP = 0;
      return _("Bad call to MD_ATOF()");
    }

  t = atof_ieee (input_line_pointer, type, words);
  if (t)
    input_line_pointer = t;
  *sizeP = prec * sizeof (LITTLENUM_TYPE);

  if (target_big_endian)
    {
      for (i = 0; i < prec; i++)
	{
	  md_number_to_chars (litP, (valueT) words[i],
			      sizeof (LITTLENUM_TYPE));
	  litP += sizeof (LITTLENUM_TYPE);
	}
    }
  else
    {
      for (i = prec - 1; i >= 0; i--)
	{
	  md_number_to_chars (litP, (valueT) words[i],
			      sizeof (LITTLENUM_TYPE));
	  litP += sizeof (LITTLENUM_TYPE);
	}
    }

  return 0;
}

/* Write a value out to the object file, using the appropriate
   endianness.  */

void
md_number_to_chars (buf, val, n)
     char *buf;
     valueT val;
     int n;
{
  if (target_big_endian)
    number_to_chars_bigendian (buf, val, n);
  else if (target_little_endian_data
	   && ((n == 4 || n == 2) && ~now_seg->flags & SEC_ALLOC))
    /* Output debug words, which are not in allocated sections, as big
       endian.  */
    number_to_chars_bigendian (buf, val, n);
  else if (target_little_endian_data || ! target_big_endian)
    number_to_chars_littleendian (buf, val, n);
}

/* Apply a fixS to the frags, now that we know the value it ought to
   hold.  */

void
md_apply_fix (fixP, valP, segment)
     fixS *fixP;
     valueT *valP;
     segT segment ATTRIBUTE_UNUSED;
{
  char *buf = fixP->fx_where + fixP->fx_frag->fr_literal;
  offsetT val = * (offsetT *) valP;
  long insn;

  assert (fixP->fx_r_type < BFD_RELOC_UNUSED);

  fixP->fx_addnumber = val;	/* Remember value for emit_reloc.  */

#ifdef OBJ_ELF
  /* SPARC ELF relocations don't use an addend in the data field.  */
  if (fixP->fx_addsy != NULL)
    {
      switch (fixP->fx_r_type)
	{
	case BFD_RELOC_SPARC_TLS_GD_HI22:
	case BFD_RELOC_SPARC_TLS_GD_LO10:
	case BFD_RELOC_SPARC_TLS_GD_ADD:
	case BFD_RELOC_SPARC_TLS_GD_CALL:
	case BFD_RELOC_SPARC_TLS_LDM_HI22:
	case BFD_RELOC_SPARC_TLS_LDM_LO10:
	case BFD_RELOC_SPARC_TLS_LDM_ADD:
	case BFD_RELOC_SPARC_TLS_LDM_CALL:
	case BFD_RELOC_SPARC_TLS_LDO_HIX22:
	case BFD_RELOC_SPARC_TLS_LDO_LOX10:
	case BFD_RELOC_SPARC_TLS_LDO_ADD:
	case BFD_RELOC_SPARC_TLS_IE_HI22:
	case BFD_RELOC_SPARC_TLS_IE_LO10:
	case BFD_RELOC_SPARC_TLS_IE_LD:
	case BFD_RELOC_SPARC_TLS_IE_LDX:
	case BFD_RELOC_SPARC_TLS_IE_ADD:
	case BFD_RELOC_SPARC_TLS_LE_HIX22:
	case BFD_RELOC_SPARC_TLS_LE_LOX10:
	case BFD_RELOC_SPARC_TLS_DTPMOD32:
	case BFD_RELOC_SPARC_TLS_DTPMOD64:
	case BFD_RELOC_SPARC_TLS_DTPOFF32:
	case BFD_RELOC_SPARC_TLS_DTPOFF64:
	case BFD_RELOC_SPARC_TLS_TPOFF32:
	case BFD_RELOC_SPARC_TLS_TPOFF64:
	  S_SET_THREAD_LOCAL (fixP->fx_addsy);

	default:
	  break;
	}

      return;
    }
#endif

  /* This is a hack.  There should be a better way to
     handle this.  Probably in terms of howto fields, once
     we can look at these fixups in terms of howtos.  */
  if (fixP->fx_r_type == BFD_RELOC_32_PCREL_S2 && fixP->fx_addsy)
    val += fixP->fx_where + fixP->fx_frag->fr_address;

#ifdef OBJ_AOUT
  /* FIXME: More ridiculous gas reloc hacking.  If we are going to
     generate a reloc, then we just want to let the reloc addend set
     the value.  We do not want to also stuff the addend into the
     object file.  Including the addend in the object file works when
     doing a static link, because the linker will ignore the object
     file contents.  However, the dynamic linker does not ignore the
     object file contents.  */
  if (fixP->fx_addsy != NULL
      && fixP->fx_r_type != BFD_RELOC_32_PCREL_S2)
    val = 0;

  /* When generating PIC code, we do not want an addend for a reloc
     against a local symbol.  We adjust fx_addnumber to cancel out the
     value already included in val, and to also cancel out the
     adjustment which bfd_install_relocation will create.  */
  if (sparc_pic_code
      && fixP->fx_r_type != BFD_RELOC_32_PCREL_S2
      && fixP->fx_addsy != NULL
      && ! S_IS_COMMON (fixP->fx_addsy)
      && symbol_section_p (fixP->fx_addsy))
    fixP->fx_addnumber -= 2 * S_GET_VALUE (fixP->fx_addsy);

  /* When generating PIC code, we need to fiddle to get
     bfd_install_relocation to do the right thing for a PC relative
     reloc against a local symbol which we are going to keep.  */
  if (sparc_pic_code
      && fixP->fx_r_type == BFD_RELOC_32_PCREL_S2
      && fixP->fx_addsy != NULL
      && (S_IS_EXTERNAL (fixP->fx_addsy)
	  || S_IS_WEAK (fixP->fx_addsy))
      && S_IS_DEFINED (fixP->fx_addsy)
      && ! S_IS_COMMON (fixP->fx_addsy))
    {
      val = 0;
      fixP->fx_addnumber -= 2 * S_GET_VALUE (fixP->fx_addsy);
    }
#endif

  /* If this is a data relocation, just output VAL.  */

  if (fixP->fx_r_type == BFD_RELOC_16
      || fixP->fx_r_type == BFD_RELOC_SPARC_UA16)
    {
      md_number_to_chars (buf, val, 2);
    }
  else if (fixP->fx_r_type == BFD_RELOC_32
	   || fixP->fx_r_type == BFD_RELOC_SPARC_UA32
	   || fixP->fx_r_type == BFD_RELOC_SPARC_REV32)
    {
      md_number_to_chars (buf, val, 4);
    }
  else if (fixP->fx_r_type == BFD_RELOC_64
	   || fixP->fx_r_type == BFD_RELOC_SPARC_UA64)
    {
      md_number_to_chars (buf, val, 8);
    }
  else if (fixP->fx_r_type == BFD_RELOC_VTABLE_INHERIT
           || fixP->fx_r_type == BFD_RELOC_VTABLE_ENTRY)
    {
      fixP->fx_done = 0;
      return;
    }
  else
    {
      /* It's a relocation against an instruction.  */

      if (INSN_BIG_ENDIAN)
	insn = bfd_getb32 ((unsigned char *) buf);
      else
	insn = bfd_getl32 ((unsigned char *) buf);

      switch (fixP->fx_r_type)
	{
	case BFD_RELOC_32_PCREL_S2:
	  val = val >> 2;
	  /* FIXME: This increment-by-one deserves a comment of why it's
	     being done!  */
	  if (! sparc_pic_code
	      || fixP->fx_addsy == NULL
	      || symbol_section_p (fixP->fx_addsy))
	    ++val;

	  insn |= val & 0x3fffffff;

	  /* See if we have a delay slot.  */
	  if (sparc_relax && fixP->fx_where + 8 <= fixP->fx_frag->fr_fix)
	    {
#define G0		0
#define O7		15
#define XCC		(2 << 20)
#define COND(x)		(((x)&0xf)<<25)
#define CONDA		COND(0x8)
#define INSN_BPA	(F2(0,1) | CONDA | BPRED | XCC)
#define INSN_BA		(F2(0,2) | CONDA)
#define INSN_OR		F3(2, 0x2, 0)
#define INSN_NOP	F2(0,4)

	      long delay;

	      /* If the instruction is a call with either:
		 restore
		 arithmetic instruction with rd == %o7
		 where rs1 != %o7 and rs2 if it is register != %o7
		 then we can optimize if the call destination is near
		 by changing the call into a branch always.  */
	      if (INSN_BIG_ENDIAN)
		delay = bfd_getb32 ((unsigned char *) buf + 4);
	      else
		delay = bfd_getl32 ((unsigned char *) buf + 4);
	      if ((insn & OP (~0)) != OP (1) || (delay & OP (~0)) != OP (2))
		break;
	      if ((delay & OP3 (~0)) != OP3 (0x3d) /* Restore.  */
		  && ((delay & OP3 (0x28)) != 0 /* Arithmetic.  */
		      || ((delay & RD (~0)) != RD (O7))))
		break;
	      if ((delay & RS1 (~0)) == RS1 (O7)
		  || ((delay & F3I (~0)) == 0
		      && (delay & RS2 (~0)) == RS2 (O7)))
		break;
	      /* Ensure the branch will fit into simm22.  */
	      if ((val & 0x3fe00000)
		  && (val & 0x3fe00000) != 0x3fe00000)
		break;
	      /* Check if the arch is v9 and branch will fit
		 into simm19.  */
	      if (((val & 0x3c0000) == 0
		   || (val & 0x3c0000) == 0x3c0000)
		  && (sparc_arch_size == 64
		      || current_architecture >= SPARC_OPCODE_ARCH_V9))
		/* ba,pt %xcc  */
		insn = INSN_BPA | (val & 0x7ffff);
	      else
		/* ba  */
		insn = INSN_BA | (val & 0x3fffff);
	      if (fixP->fx_where >= 4
		  && ((delay & (0xffffffff ^ RS1 (~0)))
		      == (INSN_OR | RD (O7) | RS2 (G0))))
		{
		  long setter;
		  int reg;

		  if (INSN_BIG_ENDIAN)
		    setter = bfd_getb32 ((unsigned char *) buf - 4);
		  else
		    setter = bfd_getl32 ((unsigned char *) buf - 4);
		  if ((setter & (0xffffffff ^ RD (~0)))
		      != (INSN_OR | RS1 (O7) | RS2 (G0)))
		    break;
		  /* The sequence was
		     or %o7, %g0, %rN
		     call foo
		     or %rN, %g0, %o7

		     If call foo was replaced with ba, replace
		     or %rN, %g0, %o7 with nop.  */
		  reg = (delay & RS1 (~0)) >> 14;
		  if (reg != ((setter & RD (~0)) >> 25)
		      || reg == G0 || reg == O7)
		    break;

		  if (INSN_BIG_ENDIAN)
		    bfd_putb32 (INSN_NOP, (unsigned char *) buf + 4);
		  else
		    bfd_putl32 (INSN_NOP, (unsigned char *) buf + 4);
		}
	    }
	  break;

	case BFD_RELOC_SPARC_11:
	  if (! in_signed_range (val, 0x7ff))
	    as_bad_where (fixP->fx_file, fixP->fx_line,
			  _("relocation overflow"));
	  insn |= val & 0x7ff;
	  break;

	case BFD_RELOC_SPARC_10:
	  if (! in_signed_range (val, 0x3ff))
	    as_bad_where (fixP->fx_file, fixP->fx_line,
			  _("relocation overflow"));
	  insn |= val & 0x3ff;
	  break;

	case BFD_RELOC_SPARC_7:
	  if (! in_bitfield_range (val, 0x7f))
	    as_bad_where (fixP->fx_file, fixP->fx_line,
			  _("relocation overflow"));
	  insn |= val & 0x7f;
	  break;

	case BFD_RELOC_SPARC_6:
	  if (! in_bitfield_range (val, 0x3f))
	    as_bad_where (fixP->fx_file, fixP->fx_line,
			  _("relocation overflow"));
	  insn |= val & 0x3f;
	  break;

	case BFD_RELOC_SPARC_5:
	  if (! in_bitfield_range (val, 0x1f))
	    as_bad_where (fixP->fx_file, fixP->fx_line,
			  _("relocation overflow"));
	  insn |= val & 0x1f;
	  break;

	case BFD_RELOC_SPARC_WDISP16:
	  if ((val & 3)
	      || val >= 0x1fffc
	      || val <= -(offsetT) 0x20008)
	    as_bad_where (fixP->fx_file, fixP->fx_line,
			  _("relocation overflow"));
	  /* FIXME: The +1 deserves a comment.  */
	  val = (val >> 2) + 1;
	  insn |= ((val & 0xc000) << 6) | (val & 0x3fff);
	  break;

	case BFD_RELOC_SPARC_WDISP19:
	  if ((val & 3)
	      || val >= 0xffffc
	      || val <= -(offsetT) 0x100008)
	    as_bad_where (fixP->fx_file, fixP->fx_line,
			  _("relocation overflow"));
	  /* FIXME: The +1 deserves a comment.  */
	  val = (val >> 2) + 1;
	  insn |= val & 0x7ffff;
	  break;

	case BFD_RELOC_SPARC_HH22:
	  val = BSR (val, 32);
	  /* Fall through.  */

	case BFD_RELOC_SPARC_LM22:
	case BFD_RELOC_HI22:
	  if (!fixP->fx_addsy)
	    insn |= (val >> 10) & 0x3fffff;
	  else
	    /* FIXME: Need comment explaining why we do this.  */
	    insn &= ~0xffff;
	  break;

	case BFD_RELOC_SPARC22:
	  if (val & ~0x003fffff)
	    as_bad_where (fixP->fx_file, fixP->fx_line,
			  _("relocation overflow"));
	  insn |= (val & 0x3fffff);
	  break;

	case BFD_RELOC_SPARC_HM10:
	  val = BSR (val, 32);
	  /* Fall through.  */

	case BFD_RELOC_LO10:
	  if (!fixP->fx_addsy)
	    insn |= val & 0x3ff;
	  else
	    /* FIXME: Need comment explaining why we do this.  */
	    insn &= ~0xff;
	  break;

	case BFD_RELOC_SPARC_OLO10:
	  val &= 0x3ff;
	  val += fixP->tc_fix_data;
	  /* Fall through.  */

	case BFD_RELOC_SPARC13:
	  if (! in_signed_range (val, 0x1fff))
	    as_bad_where (fixP->fx_file, fixP->fx_line,
			  _("relocation overflow"));
	  insn |= val & 0x1fff;
	  break;

	case BFD_RELOC_SPARC_WDISP22:
	  val = (val >> 2) + 1;
	  /* Fall through.  */
	case BFD_RELOC_SPARC_BASE22:
	  insn |= val & 0x3fffff;
	  break;

	case BFD_RELOC_SPARC_H44:
	  if (!fixP->fx_addsy)
	    {
	      bfd_vma tval = val;
	      tval >>= 22;
	      insn |= tval & 0x3fffff;
	    }
	  break;

	case BFD_RELOC_SPARC_M44:
	  if (!fixP->fx_addsy)
	    insn |= (val >> 12) & 0x3ff;
	  break;

	case BFD_RELOC_SPARC_L44:
	  if (!fixP->fx_addsy)
	    insn |= val & 0xfff;
	  break;

	case BFD_RELOC_SPARC_HIX22:
	  if (!fixP->fx_addsy)
	    {
	      val ^= ~(offsetT) 0;
	      insn |= (val >> 10) & 0x3fffff;
	    }
	  break;

	case BFD_RELOC_SPARC_LOX10:
	  if (!fixP->fx_addsy)
	    insn |= 0x1c00 | (val & 0x3ff);
	  break;

	case BFD_RELOC_NONE:
	default:
	  as_bad_where (fixP->fx_file, fixP->fx_line,
			_("bad or unhandled relocation type: 0x%02x"),
			fixP->fx_r_type);
	  break;
	}

      if (INSN_BIG_ENDIAN)
	bfd_putb32 (insn, (unsigned char *) buf);
      else
	bfd_putl32 (insn, (unsigned char *) buf);
    }

  /* Are we finished with this relocation now?  */
  if (fixP->fx_addsy == 0 && !fixP->fx_pcrel)
    fixP->fx_done = 1;
}

/* Translate internal representation of relocation info to BFD target
   format.  */

arelent **
tc_gen_reloc (section, fixp)
     asection *section;
     fixS *fixp;
{
  static arelent *relocs[3];
  arelent *reloc;
  bfd_reloc_code_real_type code;

  relocs[0] = reloc = (arelent *) xmalloc (sizeof (arelent));
  relocs[1] = NULL;

  reloc->sym_ptr_ptr = (asymbol **) xmalloc (sizeof (asymbol *));
  *reloc->sym_ptr_ptr = symbol_get_bfdsym (fixp->fx_addsy);
  reloc->address = fixp->fx_frag->fr_address + fixp->fx_where;

  switch (fixp->fx_r_type)
    {
    case BFD_RELOC_16:
    case BFD_RELOC_32:
    case BFD_RELOC_HI22:
    case BFD_RELOC_LO10:
    case BFD_RELOC_32_PCREL_S2:
    case BFD_RELOC_SPARC13:
    case BFD_RELOC_SPARC22:
    case BFD_RELOC_SPARC_BASE13:
    case BFD_RELOC_SPARC_WDISP16:
    case BFD_RELOC_SPARC_WDISP19:
    case BFD_RELOC_SPARC_WDISP22:
    case BFD_RELOC_64:
    case BFD_RELOC_SPARC_5:
    case BFD_RELOC_SPARC_6:
    case BFD_RELOC_SPARC_7:
    case BFD_RELOC_SPARC_10:
    case BFD_RELOC_SPARC_11:
    case BFD_RELOC_SPARC_HH22:
    case BFD_RELOC_SPARC_HM10:
    case BFD_RELOC_SPARC_LM22:
    case BFD_RELOC_SPARC_PC_HH22:
    case BFD_RELOC_SPARC_PC_HM10:
    case BFD_RELOC_SPARC_PC_LM22:
    case BFD_RELOC_SPARC_H44:
    case BFD_RELOC_SPARC_M44:
    case BFD_RELOC_SPARC_L44:
    case BFD_RELOC_SPARC_HIX22:
    case BFD_RELOC_SPARC_LOX10:
    case BFD_RELOC_SPARC_REV32:
    case BFD_RELOC_SPARC_OLO10:
    case BFD_RELOC_SPARC_UA16:
    case BFD_RELOC_SPARC_UA32:
    case BFD_RELOC_SPARC_UA64:
    case BFD_RELOC_8_PCREL:
    case BFD_RELOC_16_PCREL:
    case BFD_RELOC_32_PCREL:
    case BFD_RELOC_64_PCREL:
    case BFD_RELOC_SPARC_PLT32:
    case BFD_RELOC_SPARC_PLT64:
    case BFD_RELOC_VTABLE_ENTRY:
    case BFD_RELOC_VTABLE_INHERIT:
    case BFD_RELOC_SPARC_TLS_GD_HI22:
    case BFD_RELOC_SPARC_TLS_GD_LO10:
    case BFD_RELOC_SPARC_TLS_GD_ADD:
    case BFD_RELOC_SPARC_TLS_GD_CALL:
    case BFD_RELOC_SPARC_TLS_LDM_HI22:
    case BFD_RELOC_SPARC_TLS_LDM_LO10:
    case BFD_RELOC_SPARC_TLS_LDM_ADD:
    case BFD_RELOC_SPARC_TLS_LDM_CALL:
    case BFD_RELOC_SPARC_TLS_LDO_HIX22:
    case BFD_RELOC_SPARC_TLS_LDO_LOX10:
    case BFD_RELOC_SPARC_TLS_LDO_ADD:
    case BFD_RELOC_SPARC_TLS_IE_HI22:
    case BFD_RELOC_SPARC_TLS_IE_LO10:
    case BFD_RELOC_SPARC_TLS_IE_LD:
    case BFD_RELOC_SPARC_TLS_IE_LDX:
    case BFD_RELOC_SPARC_TLS_IE_ADD:
    case BFD_RELOC_SPARC_TLS_LE_HIX22:
    case BFD_RELOC_SPARC_TLS_LE_LOX10:
    case BFD_RELOC_SPARC_TLS_DTPOFF32:
    case BFD_RELOC_SPARC_TLS_DTPOFF64:
      code = fixp->fx_r_type;
      break;
    default:
      abort ();
      return NULL;
    }

#if defined (OBJ_ELF) || defined (OBJ_AOUT)
  /* If we are generating PIC code, we need to generate a different
     set of relocs.  */

#ifdef OBJ_ELF
#define GOT_NAME "_GLOBAL_OFFSET_TABLE_"
#else
#define GOT_NAME "__GLOBAL_OFFSET_TABLE_"
#endif
#ifdef TE_VXWORKS
#define GOTT_BASE "__GOTT_BASE__"
#define GOTT_INDEX "__GOTT_INDEX__"
#endif

  /* This code must be parallel to the OBJ_ELF tc_fix_adjustable.  */

  if (sparc_pic_code)
    {
      switch (code)
	{
	case BFD_RELOC_32_PCREL_S2:
	  if (generic_force_reloc (fixp))
	    code = BFD_RELOC_SPARC_WPLT30;
	  break;
	case BFD_RELOC_HI22:
	  code = BFD_RELOC_SPARC_GOT22;
	  if (fixp->fx_addsy != NULL)
	    {
	      if (strcmp (S_GET_NAME (fixp->fx_addsy), GOT_NAME) == 0)
		code = BFD_RELOC_SPARC_PC22;
#ifdef TE_VXWORKS
	      if (strcmp (S_GET_NAME (fixp->fx_addsy), GOTT_BASE) == 0
		  || strcmp (S_GET_NAME (fixp->fx_addsy), GOTT_INDEX) == 0)
		code = BFD_RELOC_HI22; /* Unchanged.  */
#endif
	    }
	  break;
	case BFD_RELOC_LO10:
	  code = BFD_RELOC_SPARC_GOT10;
	  if (fixp->fx_addsy != NULL)
	    {
	      if (strcmp (S_GET_NAME (fixp->fx_addsy), GOT_NAME) == 0)
		code = BFD_RELOC_SPARC_PC10;
#ifdef TE_VXWORKS
	      if (strcmp (S_GET_NAME (fixp->fx_addsy), GOTT_BASE) == 0
		  || strcmp (S_GET_NAME (fixp->fx_addsy), GOTT_INDEX) == 0)
		code = BFD_RELOC_LO10; /* Unchanged.  */
#endif
	    }
	  break;
	case BFD_RELOC_SPARC13:
	  code = BFD_RELOC_SPARC_GOT13;
	  break;
	default:
	  break;
	}
    }
#endif /* defined (OBJ_ELF) || defined (OBJ_AOUT)  */

  /* Nothing is aligned in DWARF debugging sections.  */
  if (bfd_get_section_flags (stdoutput, section) & SEC_DEBUGGING)
    switch (code)
      {
      case BFD_RELOC_16: code = BFD_RELOC_SPARC_UA16; break;
      case BFD_RELOC_32: code = BFD_RELOC_SPARC_UA32; break;
      case BFD_RELOC_64: code = BFD_RELOC_SPARC_UA64; break;
      default: break;
      }

  if (code == BFD_RELOC_SPARC_OLO10)
    reloc->howto = bfd_reloc_type_lookup (stdoutput, BFD_RELOC_LO10);
  else
    reloc->howto = bfd_reloc_type_lookup (stdoutput, code);
  if (reloc->howto == 0)
    {
      as_bad_where (fixp->fx_file, fixp->fx_line,
		    _("internal error: can't export reloc type %d (`%s')"),
		    fixp->fx_r_type, bfd_get_reloc_code_name (code));
      xfree (reloc);
      relocs[0] = NULL;
      return relocs;
    }

  /* @@ Why fx_addnumber sometimes and fx_offset other times?  */
#ifdef OBJ_AOUT

  if (reloc->howto->pc_relative == 0
      || code == BFD_RELOC_SPARC_PC10
      || code == BFD_RELOC_SPARC_PC22)
    reloc->addend = fixp->fx_addnumber;
  else if (sparc_pic_code
	   && fixp->fx_r_type == BFD_RELOC_32_PCREL_S2
	   && fixp->fx_addsy != NULL
	   && (S_IS_EXTERNAL (fixp->fx_addsy)
	       || S_IS_WEAK (fixp->fx_addsy))
	   && S_IS_DEFINED (fixp->fx_addsy)
	   && ! S_IS_COMMON (fixp->fx_addsy))
    reloc->addend = fixp->fx_addnumber;
  else
    reloc->addend = fixp->fx_offset - reloc->address;

#else /* elf or coff  */

  if (code != BFD_RELOC_32_PCREL_S2
      && code != BFD_RELOC_SPARC_WDISP22
      && code != BFD_RELOC_SPARC_WDISP16
      && code != BFD_RELOC_SPARC_WDISP19
      && code != BFD_RELOC_SPARC_WPLT30
      && code != BFD_RELOC_SPARC_TLS_GD_CALL
      && code != BFD_RELOC_SPARC_TLS_LDM_CALL)
    reloc->addend = fixp->fx_addnumber;
  else if (symbol_section_p (fixp->fx_addsy))
    reloc->addend = (section->vma
		     + fixp->fx_addnumber
		     + md_pcrel_from (fixp));
  else
    reloc->addend = fixp->fx_offset;
#endif

  /* We expand R_SPARC_OLO10 to R_SPARC_LO10 and R_SPARC_13
     on the same location.  */
  if (code == BFD_RELOC_SPARC_OLO10)
    {
      relocs[1] = reloc = (arelent *) xmalloc (sizeof (arelent));
      relocs[2] = NULL;

      reloc->sym_ptr_ptr = (asymbol **) xmalloc (sizeof (asymbol *));
      *reloc->sym_ptr_ptr
	= symbol_get_bfdsym (section_symbol (absolute_section));
      reloc->address = fixp->fx_frag->fr_address + fixp->fx_where;
      reloc->howto = bfd_reloc_type_lookup (stdoutput, BFD_RELOC_SPARC13);
      reloc->addend = fixp->tc_fix_data;
    }

  return relocs;
}

/* We have no need to default values of symbols.  */

symbolS *
md_undefined_symbol (name)
     char *name ATTRIBUTE_UNUSED;
{
  return 0;
}

/* Round up a section size to the appropriate boundary.  */

valueT
md_section_align (segment, size)
     segT segment ATTRIBUTE_UNUSED;
     valueT size;
{
#ifndef OBJ_ELF
  /* This is not right for ELF; a.out wants it, and COFF will force
     the alignment anyways.  */
  valueT align = ((valueT) 1
		  << (valueT) bfd_get_section_alignment (stdoutput, segment));
  valueT newsize;

  /* Turn alignment value into a mask.  */
  align--;
  newsize = (size + align) & ~align;
  return newsize;
#else
  return size;
#endif
}

/* Exactly what point is a PC-relative offset relative TO?
   On the sparc, they're relative to the address of the offset, plus
   its size.  This gets us to the following instruction.
   (??? Is this right?  FIXME-SOON)  */
long
md_pcrel_from (fixP)
     fixS *fixP;
{
  long ret;

  ret = fixP->fx_where + fixP->fx_frag->fr_address;
  if (! sparc_pic_code
      || fixP->fx_addsy == NULL
      || symbol_section_p (fixP->fx_addsy))
    ret += fixP->fx_size;
  return ret;
}

/* Return log2 (VALUE), or -1 if VALUE is not an exact positive power
   of two.  */

static int
mylog2 (value)
     int value;
{
  int shift;

  if (value <= 0)
    return -1;

  for (shift = 0; (value & 1) == 0; value >>= 1)
    ++shift;

  return (value == 1) ? shift : -1;
}

/* Sort of like s_lcomm.  */

#ifndef OBJ_ELF
static int max_alignment = 15;
#endif

static void
s_reserve (ignore)
     int ignore ATTRIBUTE_UNUSED;
{
  char *name;
  char *p;
  char c;
  int align;
  int size;
  int temp;
  symbolS *symbolP;

  name = input_line_pointer;
  c = get_symbol_end ();
  p = input_line_pointer;
  *p = c;
  SKIP_WHITESPACE ();

  if (*input_line_pointer != ',')
    {
      as_bad (_("Expected comma after name"));
      ignore_rest_of_line ();
      return;
    }

  ++input_line_pointer;

  if ((size = get_absolute_expression ()) < 0)
    {
      as_bad (_("BSS length (%d.) <0! Ignored."), size);
      ignore_rest_of_line ();
      return;
    }				/* Bad length.  */

  *p = 0;
  symbolP = symbol_find_or_make (name);
  *p = c;

  if (strncmp (input_line_pointer, ",\"bss\"", 6) != 0
      && strncmp (input_line_pointer, ",\".bss\"", 7) != 0)
    {
      as_bad (_("bad .reserve segment -- expected BSS segment"));
      return;
    }

  if (input_line_pointer[2] == '.')
    input_line_pointer += 7;
  else
    input_line_pointer += 6;
  SKIP_WHITESPACE ();

  if (*input_line_pointer == ',')
    {
      ++input_line_pointer;

      SKIP_WHITESPACE ();
      if (*input_line_pointer == '\n')
	{
	  as_bad (_("missing alignment"));
	  ignore_rest_of_line ();
	  return;
	}

      align = (int) get_absolute_expression ();

#ifndef OBJ_ELF
      if (align > max_alignment)
	{
	  align = max_alignment;
	  as_warn (_("alignment too large; assuming %d"), align);
	}
#endif

      if (align < 0)
	{
	  as_bad (_("negative alignment"));
	  ignore_rest_of_line ();
	  return;
	}

      if (align != 0)
	{
	  temp = mylog2 (align);
	  if (temp < 0)
	    {
	      as_bad (_("alignment not a power of 2"));
	      ignore_rest_of_line ();
	      return;
	    }

	  align = temp;
	}

      record_alignment (bss_section, align);
    }
  else
    align = 0;

  if (!S_IS_DEFINED (symbolP)
#ifdef OBJ_AOUT
      && S_GET_OTHER (symbolP) == 0
      && S_GET_DESC (symbolP) == 0
#endif
      )
    {
      if (! need_pass_2)
	{
	  char *pfrag;
	  segT current_seg = now_seg;
	  subsegT current_subseg = now_subseg;

	  /* Switch to bss.  */
	  subseg_set (bss_section, 1);

	  if (align)
	    /* Do alignment.  */
	    frag_align (align, 0, 0);

	  /* Detach from old frag.  */
	  if (S_GET_SEGMENT (symbolP) == bss_section)
	    symbol_get_frag (symbolP)->fr_symbol = NULL;

	  symbol_set_frag (symbolP, frag_now);
	  pfrag = frag_var (rs_org, 1, 1, (relax_substateT) 0, symbolP,
			    (offsetT) size, (char *) 0);
	  *pfrag = 0;

	  S_SET_SEGMENT (symbolP, bss_section);

	  subseg_set (current_seg, current_subseg);

#ifdef OBJ_ELF
	  S_SET_SIZE (symbolP, size);
#endif
	}
    }
  else
    {
      as_warn ("Ignoring attempt to re-define symbol %s",
	       S_GET_NAME (symbolP));
    }				/* if not redefining.  */

  demand_empty_rest_of_line ();
}

static void
s_common (ignore)
     int ignore ATTRIBUTE_UNUSED;
{
  char *name;
  char c;
  char *p;
  offsetT temp, size;
  symbolS *symbolP;

  name = input_line_pointer;
  c = get_symbol_end ();
  /* Just after name is now '\0'.  */
  p = input_line_pointer;
  *p = c;
  SKIP_WHITESPACE ();
  if (*input_line_pointer != ',')
    {
      as_bad (_("Expected comma after symbol-name"));
      ignore_rest_of_line ();
      return;
    }

  /* Skip ','.  */
  input_line_pointer++;

  if ((temp = get_absolute_expression ()) < 0)
    {
      as_bad (_(".COMMon length (%lu) out of range ignored"),
	      (unsigned long) temp);
      ignore_rest_of_line ();
      return;
    }
  size = temp;
  *p = 0;
  symbolP = symbol_find_or_make (name);
  *p = c;
  if (S_IS_DEFINED (symbolP) && ! S_IS_COMMON (symbolP))
    {
      as_bad (_("Ignoring attempt to re-define symbol"));
      ignore_rest_of_line ();
      return;
    }
  if (S_GET_VALUE (symbolP) != 0)
    {
      if (S_GET_VALUE (symbolP) != (valueT) size)
	{
	  as_warn (_("Length of .comm \"%s\" is already %ld. Not changed to %ld."),
		   S_GET_NAME (symbolP), (long) S_GET_VALUE (symbolP), (long) size);
	}
    }
  else
    {
#ifndef OBJ_ELF
      S_SET_VALUE (symbolP, (valueT) size);
      S_SET_EXTERNAL (symbolP);
#endif
    }
  know (symbol_get_frag (symbolP) == &zero_address_frag);
  if (*input_line_pointer != ',')
    {
      as_bad (_("Expected comma after common length"));
      ignore_rest_of_line ();
      return;
    }
  input_line_pointer++;
  SKIP_WHITESPACE ();
  if (*input_line_pointer != '"')
    {
      temp = get_absolute_expression ();

#ifndef OBJ_ELF
      if (temp > max_alignment)
	{
	  temp = max_alignment;
	  as_warn (_("alignment too large; assuming %ld"), (long) temp);
	}
#endif

      if (temp < 0)
	{
	  as_bad (_("negative alignment"));
	  ignore_rest_of_line ();
	  return;
	}

#ifdef OBJ_ELF
      if (symbol_get_obj (symbolP)->local)
	{
	  segT old_sec;
	  int old_subsec;
	  char *p;
	  int align;

	  old_sec = now_seg;
	  old_subsec = now_subseg;

	  if (temp == 0)
	    align = 0;
	  else
	    align = mylog2 (temp);

	  if (align < 0)
	    {
	      as_bad (_("alignment not a power of 2"));
	      ignore_rest_of_line ();
	      return;
	    }

	  record_alignment (bss_section, align);
	  subseg_set (bss_section, 0);
	  if (align)
	    frag_align (align, 0, 0);
	  if (S_GET_SEGMENT (symbolP) == bss_section)
	    symbol_get_frag (symbolP)->fr_symbol = 0;
	  symbol_set_frag (symbolP, frag_now);
	  p = frag_var (rs_org, 1, 1, (relax_substateT) 0, symbolP,
			(offsetT) size, (char *) 0);
	  *p = 0;
	  S_SET_SEGMENT (symbolP, bss_section);
	  S_CLEAR_EXTERNAL (symbolP);
	  S_SET_SIZE (symbolP, size);
	  subseg_set (old_sec, old_subsec);
	}
      else
#endif /* OBJ_ELF  */
	{
	allocate_common:
	  S_SET_VALUE (symbolP, (valueT) size);
#ifdef OBJ_ELF
	  S_SET_ALIGN (symbolP, temp);
	  S_SET_SIZE (symbolP, size);
#endif
	  S_SET_EXTERNAL (symbolP);
	  S_SET_SEGMENT (symbolP, bfd_com_section_ptr);
	}
    }
  else
    {
      input_line_pointer++;
      /* @@ Some use the dot, some don't.  Can we get some consistency??  */
      if (*input_line_pointer == '.')
	input_line_pointer++;
      /* @@ Some say data, some say bss.  */
      if (strncmp (input_line_pointer, "bss\"", 4)
	  && strncmp (input_line_pointer, "data\"", 5))
	{
	  while (*--input_line_pointer != '"')
	    ;
	  input_line_pointer--;
	  goto bad_common_segment;
	}
      while (*input_line_pointer++ != '"')
	;
      goto allocate_common;
    }

  symbol_get_bfdsym (symbolP)->flags |= BSF_OBJECT;

  demand_empty_rest_of_line ();
  return;

  {
  bad_common_segment:
    p = input_line_pointer;
    while (*p && *p != '\n')
      p++;
    c = *p;
    *p = '\0';
    as_bad (_("bad .common segment %s"), input_line_pointer + 1);
    *p = c;
    input_line_pointer = p;
    ignore_rest_of_line ();
    return;
  }
}

/* Handle the .empty pseudo-op.  This suppresses the warnings about
   invalid delay slot usage.  */

static void
s_empty (ignore)
     int ignore ATTRIBUTE_UNUSED;
{
  /* The easy way to implement is to just forget about the last
     instruction.  */
  last_insn = NULL;
}

static void
s_seg (ignore)
     int ignore ATTRIBUTE_UNUSED;
{

  if (strncmp (input_line_pointer, "\"text\"", 6) == 0)
    {
      input_line_pointer += 6;
      s_text (0);
      return;
    }
  if (strncmp (input_line_pointer, "\"data\"", 6) == 0)
    {
      input_line_pointer += 6;
      s_data (0);
      return;
    }
  if (strncmp (input_line_pointer, "\"data1\"", 7) == 0)
    {
      input_line_pointer += 7;
      s_data1 ();
      return;
    }
  if (strncmp (input_line_pointer, "\"bss\"", 5) == 0)
    {
      input_line_pointer += 5;
      /* We only support 2 segments -- text and data -- for now, so
	 things in the "bss segment" will have to go into data for now.
	 You can still allocate SEG_BSS stuff with .lcomm or .reserve.  */
      subseg_set (data_section, 255);	/* FIXME-SOMEDAY.  */
      return;
    }
  as_bad (_("Unknown segment type"));
  demand_empty_rest_of_line ();
}

static void
s_data1 ()
{
  subseg_set (data_section, 1);
  demand_empty_rest_of_line ();
}

static void
s_proc (ignore)
     int ignore ATTRIBUTE_UNUSED;
{
  while (!is_end_of_line[(unsigned char) *input_line_pointer])
    {
      ++input_line_pointer;
    }
  ++input_line_pointer;
}

/* This static variable is set by s_uacons to tell sparc_cons_align
   that the expression does not need to be aligned.  */

static int sparc_no_align_cons = 0;

/* This static variable is set by sparc_cons to emit requested types
   of relocations in cons_fix_new_sparc.  */

static const char *sparc_cons_special_reloc;

/* This handles the unaligned space allocation pseudo-ops, such as
   .uaword.  .uaword is just like .word, but the value does not need
   to be aligned.  */

static void
s_uacons (bytes)
     int bytes;
{
  /* Tell sparc_cons_align not to align this value.  */
  sparc_no_align_cons = 1;
  cons (bytes);
  sparc_no_align_cons = 0;
}

/* This handles the native word allocation pseudo-op .nword.
   For sparc_arch_size 32 it is equivalent to .word,  for
   sparc_arch_size 64 it is equivalent to .xword.  */

static void
s_ncons (bytes)
     int bytes ATTRIBUTE_UNUSED;
{
  cons (sparc_arch_size == 32 ? 4 : 8);
}

#ifdef OBJ_ELF
/* Handle the SPARC ELF .register pseudo-op.  This sets the binding of a
   global register.
   The syntax is:

   .register %g[2367],{#scratch|symbolname|#ignore}
*/

static void
s_register (ignore)
     int ignore ATTRIBUTE_UNUSED;
{
  char c;
  int reg;
  int flags;
  const char *regname;

  if (input_line_pointer[0] != '%'
      || input_line_pointer[1] != 'g'
      || ((input_line_pointer[2] & ~1) != '2'
	  && (input_line_pointer[2] & ~1) != '6')
      || input_line_pointer[3] != ',')
    as_bad (_("register syntax is .register %%g[2367],{#scratch|symbolname|#ignore}"));
  reg = input_line_pointer[2] - '0';
  input_line_pointer += 4;

  if (*input_line_pointer == '#')
    {
      ++input_line_pointer;
      regname = input_line_pointer;
      c = get_symbol_end ();
      if (strcmp (regname, "scratch") && strcmp (regname, "ignore"))
	as_bad (_("register syntax is .register %%g[2367],{#scratch|symbolname|#ignore}"));
      if (regname[0] == 'i')
	regname = NULL;
      else
	regname = "";
    }
  else
    {
      regname = input_line_pointer;
      c = get_symbol_end ();
    }
  if (sparc_arch_size == 64)
    {
      if (globals[reg])
	{
	  if ((regname && globals[reg] != (symbolS *) 1
	       && strcmp (S_GET_NAME (globals[reg]), regname))
	      || ((regname != NULL) ^ (globals[reg] != (symbolS *) 1)))
	    as_bad (_("redefinition of global register"));
	}
      else
	{
	  if (regname == NULL)
	    globals[reg] = (symbolS *) 1;
	  else
	    {
	      if (*regname)
		{
		  if (symbol_find (regname))
		    as_bad (_("Register symbol %s already defined."),
			    regname);
		}
	      globals[reg] = symbol_make (regname);
	      flags = symbol_get_bfdsym (globals[reg])->flags;
	      if (! *regname)
		flags = flags & ~(BSF_GLOBAL|BSF_LOCAL|BSF_WEAK);
	      if (! (flags & (BSF_GLOBAL|BSF_LOCAL|BSF_WEAK)))
		flags |= BSF_GLOBAL;
	      symbol_get_bfdsym (globals[reg])->flags = flags;
	      S_SET_VALUE (globals[reg], (valueT) reg);
	      S_SET_ALIGN (globals[reg], reg);
	      S_SET_SIZE (globals[reg], 0);
	      /* Although we actually want undefined_section here,
		 we have to use absolute_section, because otherwise
		 generic as code will make it a COM section.
		 We fix this up in sparc_adjust_symtab.  */
	      S_SET_SEGMENT (globals[reg], absolute_section);
	      S_SET_OTHER (globals[reg], 0);
	      elf_symbol (symbol_get_bfdsym (globals[reg]))
		->internal_elf_sym.st_info =
		  ELF_ST_INFO(STB_GLOBAL, STT_REGISTER);
	      elf_symbol (symbol_get_bfdsym (globals[reg]))
		->internal_elf_sym.st_shndx = SHN_UNDEF;
	    }
	}
    }

  *input_line_pointer = c;

  demand_empty_rest_of_line ();
}

/* Adjust the symbol table.  We set undefined sections for STT_REGISTER
   symbols which need it.  */

void
sparc_adjust_symtab ()
{
  symbolS *sym;

  for (sym = symbol_rootP; sym != NULL; sym = symbol_next (sym))
    {
      if (ELF_ST_TYPE (elf_symbol (symbol_get_bfdsym (sym))
		       ->internal_elf_sym.st_info) != STT_REGISTER)
	continue;

      if (ELF_ST_TYPE (elf_symbol (symbol_get_bfdsym (sym))
		       ->internal_elf_sym.st_shndx != SHN_UNDEF))
	continue;

      S_SET_SEGMENT (sym, undefined_section);
    }
}
#endif

/* If the --enforce-aligned-data option is used, we require .word,
   et. al., to be aligned correctly.  We do it by setting up an
   rs_align_code frag, and checking in HANDLE_ALIGN to make sure that
   no unexpected alignment was introduced.

   The SunOS and Solaris native assemblers enforce aligned data by
   default.  We don't want to do that, because gcc can deliberately
   generate misaligned data if the packed attribute is used.  Instead,
   we permit misaligned data by default, and permit the user to set an
   option to check for it.  */

void
sparc_cons_align (nbytes)
     int nbytes;
{
  int nalign;
  char *p;

  /* Only do this if we are enforcing aligned data.  */
  if (! enforce_aligned_data)
    return;

  /* Don't align if this is an unaligned pseudo-op.  */
  if (sparc_no_align_cons)
    return;

  nalign = mylog2 (nbytes);
  if (nalign == 0)
    return;

  assert (nalign > 0);

  if (now_seg == absolute_section)
    {
      if ((abs_section_offset & ((1 << nalign) - 1)) != 0)
	as_bad (_("misaligned data"));
      return;
    }

  p = frag_var (rs_align_test, 1, 1, (relax_substateT) 0,
		(symbolS *) NULL, (offsetT) nalign, (char *) NULL);

  record_alignment (now_seg, nalign);
}

/* This is called from HANDLE_ALIGN in tc-sparc.h.  */

void
sparc_handle_align (fragp)
     fragS *fragp;
{
  int count, fix;
  char *p;

  count = fragp->fr_next->fr_address - fragp->fr_address - fragp->fr_fix;

  switch (fragp->fr_type)
    {
    case rs_align_test:
      if (count != 0)
	as_bad_where (fragp->fr_file, fragp->fr_line, _("misaligned data"));
      break;

    case rs_align_code:
      p = fragp->fr_literal + fragp->fr_fix;
      fix = 0;

      if (count & 3)
	{
	  fix = count & 3;
	  memset (p, 0, fix);
	  p += fix;
	  count -= fix;
	}

      if (SPARC_OPCODE_ARCH_V9_P (max_architecture) && count > 8)
	{
	  unsigned wval = (0x30680000 | count >> 2); /* ba,a,pt %xcc, 1f  */
	  if (INSN_BIG_ENDIAN)
	    number_to_chars_bigendian (p, wval, 4);
	  else
	    number_to_chars_littleendian (p, wval, 4);
	  p += 4;
	  count -= 4;
	  fix += 4;
	}

      if (INSN_BIG_ENDIAN)
	number_to_chars_bigendian (p, 0x01000000, 4);
      else
	number_to_chars_littleendian (p, 0x01000000, 4);

      fragp->fr_fix += fix;
      fragp->fr_var = 4;
      break;

    default:
      break;
    }
}

#ifdef OBJ_ELF
/* Some special processing for a Sparc ELF file.  */

void
sparc_elf_final_processing ()
{
  /* Set the Sparc ELF flag bits.  FIXME: There should probably be some
     sort of BFD interface for this.  */
  if (sparc_arch_size == 64)
    {
      switch (sparc_memory_model)
	{
	case MM_RMO:
	  elf_elfheader (stdoutput)->e_flags |= EF_SPARCV9_RMO;
	  break;
	case MM_PSO:
	  elf_elfheader (stdoutput)->e_flags |= EF_SPARCV9_PSO;
	  break;
	default:
	  break;
	}
    }
  else if (current_architecture >= SPARC_OPCODE_ARCH_V9)
    elf_elfheader (stdoutput)->e_flags |= EF_SPARC_32PLUS;
  if (current_architecture == SPARC_OPCODE_ARCH_V9A)
    elf_elfheader (stdoutput)->e_flags |= EF_SPARC_SUN_US1;
  else if (current_architecture == SPARC_OPCODE_ARCH_V9B)
    elf_elfheader (stdoutput)->e_flags |= EF_SPARC_SUN_US1|EF_SPARC_SUN_US3;
}

void
sparc_cons (exp, size)
     expressionS *exp;
     int size;
{
  char *save;

  SKIP_WHITESPACE ();
  sparc_cons_special_reloc = NULL;
  save = input_line_pointer;
  if (input_line_pointer[0] == '%'
      && input_line_pointer[1] == 'r'
      && input_line_pointer[2] == '_')
    {
      if (strncmp (input_line_pointer + 3, "disp", 4) == 0)
	{
	  input_line_pointer += 7;
	  sparc_cons_special_reloc = "disp";
	}
      else if (strncmp (input_line_pointer + 3, "plt", 3) == 0)
	{
	  if (size != 4 && size != 8)
	    as_bad (_("Illegal operands: %%r_plt in %d-byte data field"), size);
	  else
	    {
	      input_line_pointer += 6;
	      sparc_cons_special_reloc = "plt";
	    }
	}
      else if (strncmp (input_line_pointer + 3, "tls_dtpoff", 10) == 0)
	{
	  if (size != 4 && size != 8)
	    as_bad (_("Illegal operands: %%r_tls_dtpoff in %d-byte data field"), size);
	  else
	    {
	      input_line_pointer += 13;
	      sparc_cons_special_reloc = "tls_dtpoff";
	    }
	}
      if (sparc_cons_special_reloc)
	{
	  int bad = 0;

	  switch (size)
	    {
	    case 1:
	      if (*input_line_pointer != '8')
		bad = 1;
	      input_line_pointer--;
	      break;
	    case 2:
	      if (input_line_pointer[0] != '1' || input_line_pointer[1] != '6')
		bad = 1;
	      break;
	    case 4:
	      if (input_line_pointer[0] != '3' || input_line_pointer[1] != '2')
		bad = 1;
	      break;
	    case 8:
	      if (input_line_pointer[0] != '6' || input_line_pointer[1] != '4')
		bad = 1;
	      break;
	    default:
	      bad = 1;
	      break;
	    }

	  if (bad)
	    {
	      as_bad (_("Illegal operands: Only %%r_%s%d allowed in %d-byte data fields"),
		      sparc_cons_special_reloc, size * 8, size);
	    }
	  else
	    {
	      input_line_pointer += 2;
	      if (*input_line_pointer != '(')
		{
		  as_bad (_("Illegal operands: %%r_%s%d requires arguments in ()"),
			  sparc_cons_special_reloc, size * 8);
		  bad = 1;
		}
	    }

	  if (bad)
	    {
	      input_line_pointer = save;
	      sparc_cons_special_reloc = NULL;
	    }
	  else
	    {
	      int c;
	      char *end = ++input_line_pointer;
	      int npar = 0;

	      while (! is_end_of_line[(c = *end)])
		{
		  if (c == '(')
	  	    npar++;
		  else if (c == ')')
	  	    {
		      if (!npar)
	      		break;
		      npar--;
		    }
	    	  end++;
		}

	      if (c != ')')
		as_bad (_("Illegal operands: %%r_%s%d requires arguments in ()"),
			sparc_cons_special_reloc, size * 8);
	      else
		{
		  *end = '\0';
		  expression (exp);
		  *end = c;
		  if (input_line_pointer != end)
		    {
		      as_bad (_("Illegal operands: %%r_%s%d requires arguments in ()"),
			      sparc_cons_special_reloc, size * 8);
		    }
		  else
		    {
		      input_line_pointer++;
		      SKIP_WHITESPACE ();
		      c = *input_line_pointer;
		      if (! is_end_of_line[c] && c != ',')
			as_bad (_("Illegal operands: garbage after %%r_%s%d()"),
			        sparc_cons_special_reloc, size * 8);
		    }
		}
	    }
	}
    }
  if (sparc_cons_special_reloc == NULL)
    expression (exp);
}

#endif

/* This is called by emit_expr via TC_CONS_FIX_NEW when creating a
   reloc for a cons.  We could use the definition there, except that
   we want to handle little endian relocs specially.  */

void
cons_fix_new_sparc (frag, where, nbytes, exp)
     fragS *frag;
     int where;
     unsigned int nbytes;
     expressionS *exp;
{
  bfd_reloc_code_real_type r;

  r = (nbytes == 1 ? BFD_RELOC_8 :
       (nbytes == 2 ? BFD_RELOC_16 :
	(nbytes == 4 ? BFD_RELOC_32 : BFD_RELOC_64)));

  if (target_little_endian_data
      && nbytes == 4
      && now_seg->flags & SEC_ALLOC)
    r = BFD_RELOC_SPARC_REV32;

  if (sparc_cons_special_reloc)
    {
      if (*sparc_cons_special_reloc == 'd')
	switch (nbytes)
	  {
	  case 1: r = BFD_RELOC_8_PCREL; break;
	  case 2: r = BFD_RELOC_16_PCREL; break;
	  case 4: r = BFD_RELOC_32_PCREL; break;
	  case 8: r = BFD_RELOC_64_PCREL; break;
	  default: abort ();
	  }
      else if (*sparc_cons_special_reloc == 'p')
	switch (nbytes)
	  {
	  case 4: r = BFD_RELOC_SPARC_PLT32; break;
	  case 8: r = BFD_RELOC_SPARC_PLT64; break;
	  }
      else
	switch (nbytes)
	  {
	  case 4: r = BFD_RELOC_SPARC_TLS_DTPOFF32; break;
	  case 8: r = BFD_RELOC_SPARC_TLS_DTPOFF64; break;
	  }
    }
  else if (sparc_no_align_cons)
    {
      switch (nbytes)
	{
	case 2: r = BFD_RELOC_SPARC_UA16; break;
	case 4: r = BFD_RELOC_SPARC_UA32; break;
	case 8: r = BFD_RELOC_SPARC_UA64; break;
	default: abort ();
	}
   }

  fix_new_exp (frag, where, (int) nbytes, exp, 0, r);
  sparc_cons_special_reloc = NULL;
}

void
sparc_cfi_frame_initial_instructions ()
{
  cfi_add_CFA_def_cfa (14, sparc_arch_size == 64 ? 0x7ff : 0);
}

int
sparc_regname_to_dw2regnum (char *regname)
{
  char *p, *q;

  if (!regname[0])
    return -1;

  q = "goli";
  p = strchr (q, regname[0]);
  if (p)
    {
      if (regname[1] < '0' || regname[1] > '8' || regname[2])
	return -1;
      return (p - q) * 8 + regname[1] - '0';
    }
  if (regname[0] == 's' && regname[1] == 'p' && !regname[2])
    return 14;
  if (regname[0] == 'f' && regname[1] == 'p' && !regname[2])
    return 30;
  if (regname[0] == 'f' || regname[0] == 'r')
    {
      unsigned int regnum;

      regnum = strtoul (regname + 1, &q, 10);
      if (p == q || *q)
        return -1;
      if (regnum >= ((regname[0] == 'f'
		      && SPARC_OPCODE_ARCH_V9_P (max_architecture))
		     ? 64 : 32))
	return -1;
      if (regname[0] == 'f')
	{
          regnum += 32;
          if (regnum >= 64 && (regnum & 1))
	    return -1;
        }
      return regnum;
    }
  return -1;
}

void
sparc_cfi_emit_pcrel_expr (expressionS *exp, unsigned int nbytes)
{
  sparc_cons_special_reloc = "disp";
  sparc_no_align_cons = 1;
  emit_expr (exp, nbytes);
  sparc_no_align_cons = 0;
  sparc_cons_special_reloc = NULL;
}
