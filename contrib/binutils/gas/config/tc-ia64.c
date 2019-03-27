/* tc-ia64.c -- Assembler for the HP/Intel IA-64 architecture.
   Copyright 1998, 1999, 2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007
   Free Software Foundation, Inc.
   Contributed by David Mosberger-Tang <davidm@hpl.hp.com>

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
   the Free Software Foundation, 51 Franklin Street - Fifth Floor,
   Boston, MA 02110-1301, USA.  */

/*
  TODO:

  - optional operands
  - directives:
	.eb
	.estate
	.lb
	.popsection
	.previous
	.psr
	.pushsection
  - labels are wrong if automatic alignment is introduced
    (e.g., checkout the second real10 definition in test-data.s)
  - DV-related stuff:
	<reg>.safe_across_calls and any other DV-related directives I don't
	  have documentation for.
	verify mod-sched-brs reads/writes are checked/marked (and other
	notes)

 */

#include "as.h"
#include "safe-ctype.h"
#include "dwarf2dbg.h"
#include "subsegs.h"

#include "opcode/ia64.h"

#include "elf/ia64.h"

#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif

#define NELEMS(a)	((int) (sizeof (a)/sizeof ((a)[0])))

/* Some systems define MIN in, e.g., param.h.  */
#undef MIN
#define MIN(a,b)	((a) < (b) ? (a) : (b))

#define NUM_SLOTS	4
#define PREV_SLOT	md.slot[(md.curr_slot + NUM_SLOTS - 1) % NUM_SLOTS]
#define CURR_SLOT	md.slot[md.curr_slot]

#define O_pseudo_fixup (O_max + 1)

enum special_section
  {
    /* IA-64 ABI section pseudo-ops.  */
    SPECIAL_SECTION_BSS = 0,
    SPECIAL_SECTION_SBSS,
    SPECIAL_SECTION_SDATA,
    SPECIAL_SECTION_RODATA,
    SPECIAL_SECTION_COMMENT,
    SPECIAL_SECTION_UNWIND,
    SPECIAL_SECTION_UNWIND_INFO,
    /* HPUX specific section pseudo-ops.  */
    SPECIAL_SECTION_INIT_ARRAY,
    SPECIAL_SECTION_FINI_ARRAY,
  };

enum reloc_func
  {
    FUNC_DTP_MODULE,
    FUNC_DTP_RELATIVE,
    FUNC_FPTR_RELATIVE,
    FUNC_GP_RELATIVE,
    FUNC_LT_RELATIVE,
    FUNC_LT_RELATIVE_X,
    FUNC_PC_RELATIVE,
    FUNC_PLT_RELATIVE,
    FUNC_SEC_RELATIVE,
    FUNC_SEG_RELATIVE,
    FUNC_TP_RELATIVE,
    FUNC_LTV_RELATIVE,
    FUNC_LT_FPTR_RELATIVE,
    FUNC_LT_DTP_MODULE,
    FUNC_LT_DTP_RELATIVE,
    FUNC_LT_TP_RELATIVE,
    FUNC_IPLT_RELOC,
  };

enum reg_symbol
  {
    REG_GR	= 0,
    REG_FR	= (REG_GR + 128),
    REG_AR	= (REG_FR + 128),
    REG_CR	= (REG_AR + 128),
    REG_P	= (REG_CR + 128),
    REG_BR	= (REG_P  + 64),
    REG_IP	= (REG_BR + 8),
    REG_CFM,
    REG_PR,
    REG_PR_ROT,
    REG_PSR,
    REG_PSR_L,
    REG_PSR_UM,
    /* The following are pseudo-registers for use by gas only.  */
    IND_CPUID,
    IND_DBR,
    IND_DTR,
    IND_ITR,
    IND_IBR,
    IND_MSR,
    IND_PKR,
    IND_PMC,
    IND_PMD,
    IND_RR,
    /* The following pseudo-registers are used for unwind directives only:  */
    REG_PSP,
    REG_PRIUNAT,
    REG_NUM
  };

enum dynreg_type
  {
    DYNREG_GR = 0,	/* dynamic general purpose register */
    DYNREG_FR,		/* dynamic floating point register */
    DYNREG_PR,		/* dynamic predicate register */
    DYNREG_NUM_TYPES
  };

enum operand_match_result
  {
    OPERAND_MATCH,
    OPERAND_OUT_OF_RANGE,
    OPERAND_MISMATCH
  };

/* On the ia64, we can't know the address of a text label until the
   instructions are packed into a bundle.  To handle this, we keep
   track of the list of labels that appear in front of each
   instruction.  */
struct label_fix
{
  struct label_fix *next;
  struct symbol *sym;
  bfd_boolean dw2_mark_labels;
};

/* This is the endianness of the current section.  */
extern int target_big_endian;

/* This is the default endianness.  */
static int default_big_endian = TARGET_BYTES_BIG_ENDIAN;

void (*ia64_number_to_chars) PARAMS ((char *, valueT, int));

static void ia64_float_to_chars_bigendian
  PARAMS ((char *, LITTLENUM_TYPE *, int));
static void ia64_float_to_chars_littleendian
  PARAMS ((char *, LITTLENUM_TYPE *, int));
static void (*ia64_float_to_chars)
  PARAMS ((char *, LITTLENUM_TYPE *, int));

static struct hash_control *alias_hash;
static struct hash_control *alias_name_hash;
static struct hash_control *secalias_hash;
static struct hash_control *secalias_name_hash;

/* List of chars besides those in app.c:symbol_chars that can start an
   operand.  Used to prevent the scrubber eating vital white-space.  */
const char ia64_symbol_chars[] = "@?";

/* Characters which always start a comment.  */
const char comment_chars[] = "";

/* Characters which start a comment at the beginning of a line.  */
const char line_comment_chars[] = "#";

/* Characters which may be used to separate multiple commands on a
   single line.  */
const char line_separator_chars[] = ";{}";

/* Characters which are used to indicate an exponent in a floating
   point number.  */
const char EXP_CHARS[] = "eE";

/* Characters which mean that a number is a floating point constant,
   as in 0d1.0.  */
const char FLT_CHARS[] = "rRsSfFdDxXpP";

/* ia64-specific option processing:  */

const char *md_shortopts = "m:N:x::";

struct option md_longopts[] =
  {
#define OPTION_MCONSTANT_GP (OPTION_MD_BASE + 1)
    {"mconstant-gp", no_argument, NULL, OPTION_MCONSTANT_GP},
#define OPTION_MAUTO_PIC (OPTION_MD_BASE + 2)
    {"mauto-pic", no_argument, NULL, OPTION_MAUTO_PIC}
  };

size_t md_longopts_size = sizeof (md_longopts);

static struct
  {
    struct hash_control *pseudo_hash;	/* pseudo opcode hash table */
    struct hash_control *reg_hash;	/* register name hash table */
    struct hash_control *dynreg_hash;	/* dynamic register hash table */
    struct hash_control *const_hash;	/* constant hash table */
    struct hash_control *entry_hash;    /* code entry hint hash table */

    /* If X_op is != O_absent, the registername for the instruction's
       qualifying predicate.  If NULL, p0 is assumed for instructions
       that are predicatable.  */
    expressionS qp;

    /* Optimize for which CPU.  */
    enum
      {
	itanium1,
	itanium2
      } tune;

    /* What to do when hint.b is used.  */
    enum
      {
	hint_b_error,
	hint_b_warning,
	hint_b_ok
      } hint_b;

    unsigned int
      manual_bundling : 1,
      debug_dv: 1,
      detect_dv: 1,
      explicit_mode : 1,            /* which mode we're in */
      default_explicit_mode : 1,    /* which mode is the default */
      mode_explicitly_set : 1,      /* was the current mode explicitly set? */
      auto_align : 1,
      keep_pending_output : 1;

    /* What to do when something is wrong with unwind directives.  */
    enum
      {
	unwind_check_warning,
	unwind_check_error
      } unwind_check;

    /* Each bundle consists of up to three instructions.  We keep
       track of four most recent instructions so we can correctly set
       the end_of_insn_group for the last instruction in a bundle.  */
    int curr_slot;
    int num_slots_in_use;
    struct slot
      {
	unsigned int
	  end_of_insn_group : 1,
	  manual_bundling_on : 1,
	  manual_bundling_off : 1,
	  loc_directive_seen : 1;
	signed char user_template;	/* user-selected template, if any */
	unsigned char qp_regno;		/* qualifying predicate */
	/* This duplicates a good fraction of "struct fix" but we
	   can't use a "struct fix" instead since we can't call
	   fix_new_exp() until we know the address of the instruction.  */
	int num_fixups;
	struct insn_fix
	  {
	    bfd_reloc_code_real_type code;
	    enum ia64_opnd opnd;	/* type of operand in need of fix */
	    unsigned int is_pcrel : 1;	/* is operand pc-relative? */
	    expressionS expr;		/* the value to be inserted */
	  }
	fixup[2];			/* at most two fixups per insn */
	struct ia64_opcode *idesc;
	struct label_fix *label_fixups;
	struct label_fix *tag_fixups;
	struct unw_rec_list *unwind_record;	/* Unwind directive.  */
	expressionS opnd[6];
	char *src_file;
	unsigned int src_line;
	struct dwarf2_line_info debug_line;
      }
    slot[NUM_SLOTS];

    segT last_text_seg;

    struct dynreg
      {
	struct dynreg *next;		/* next dynamic register */
	const char *name;
	unsigned short base;		/* the base register number */
	unsigned short num_regs;	/* # of registers in this set */
      }
    *dynreg[DYNREG_NUM_TYPES], in, loc, out, rot;

    flagword flags;			/* ELF-header flags */

    struct mem_offset {
      unsigned hint:1;              /* is this hint currently valid? */
      bfd_vma offset;               /* mem.offset offset */
      bfd_vma base;                 /* mem.offset base */
    } mem_offset;

    int path;                       /* number of alt. entry points seen */
    const char **entry_labels;      /* labels of all alternate paths in
				       the current DV-checking block.  */
    int maxpaths;                   /* size currently allocated for
				       entry_labels */

    int pointer_size;       /* size in bytes of a pointer */
    int pointer_size_shift; /* shift size of a pointer for alignment */

    symbolS *indregsym[IND_RR - IND_CPUID + 1];
  }
md;

/* These are not const, because they are modified to MMI for non-itanium1
   targets below.  */
/* MFI bundle of nops.  */
static unsigned char le_nop[16] =
{
  0x0c, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
  0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00
};
/* MFI bundle of nops with stop-bit.  */
static unsigned char le_nop_stop[16] =
{
  0x0d, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
  0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00
};

/* application registers:  */

#define AR_K0		0
#define AR_K7		7
#define AR_RSC		16
#define AR_BSP		17
#define AR_BSPSTORE	18
#define AR_RNAT		19
#define AR_FCR		21
#define AR_EFLAG	24
#define AR_CSD		25
#define AR_SSD		26
#define AR_CFLG		27
#define AR_FSR		28
#define AR_FIR		29
#define AR_FDR		30
#define AR_CCV		32
#define AR_UNAT		36
#define AR_FPSR		40
#define AR_ITC		44
#define AR_PFS		64
#define AR_LC		65
#define AR_EC		66

static const struct
  {
    const char *name;
    unsigned int regnum;
  }
ar[] =
  {
    {"ar.k0",		AR_K0},		{"ar.k1",	AR_K0 + 1},
    {"ar.k2",		AR_K0 + 2},	{"ar.k3",	AR_K0 + 3},
    {"ar.k4",		AR_K0 + 4},	{"ar.k5",	AR_K0 + 5},
    {"ar.k6",		AR_K0 + 6},	{"ar.k7",	AR_K7},
    {"ar.rsc",		AR_RSC},	{"ar.bsp",	AR_BSP},
    {"ar.bspstore",	AR_BSPSTORE},	{"ar.rnat",	AR_RNAT},
    {"ar.fcr",		AR_FCR},	{"ar.eflag",	AR_EFLAG},
    {"ar.csd",		AR_CSD},	{"ar.ssd",	AR_SSD},
    {"ar.cflg",		AR_CFLG},	{"ar.fsr",	AR_FSR},
    {"ar.fir",		AR_FIR},	{"ar.fdr",	AR_FDR},
    {"ar.ccv",		AR_CCV},	{"ar.unat",	AR_UNAT},
    {"ar.fpsr",		AR_FPSR},	{"ar.itc",	AR_ITC},
    {"ar.pfs",		AR_PFS},	{"ar.lc",	AR_LC},
    {"ar.ec",		AR_EC},
  };

/* control registers:  */

#define CR_DCR           0
#define CR_ITM           1
#define CR_IVA           2
#define CR_PTA           8
#define CR_GPTA          9
#define CR_IPSR         16
#define CR_ISR          17
#define CR_IIP          19
#define CR_IFA          20
#define CR_ITIR         21
#define CR_IIPA         22
#define CR_IFS          23
#define CR_IIM          24
#define CR_IHA          25
#define CR_LID          64
#define CR_IVR          65
#define CR_TPR          66
#define CR_EOI          67
#define CR_IRR0         68
#define CR_IRR3         71
#define CR_ITV          72
#define CR_PMV          73
#define CR_CMCV         74
#define CR_LRR0         80
#define CR_LRR1         81

static const struct
  {
    const char *name;
    unsigned int regnum;
  }
cr[] =
  {
    {"cr.dcr",	CR_DCR},
    {"cr.itm",	CR_ITM},
    {"cr.iva",	CR_IVA},
    {"cr.pta",	CR_PTA},
    {"cr.gpta",	CR_GPTA},
    {"cr.ipsr",	CR_IPSR},
    {"cr.isr",	CR_ISR},
    {"cr.iip",	CR_IIP},
    {"cr.ifa",	CR_IFA},
    {"cr.itir",	CR_ITIR},
    {"cr.iipa",	CR_IIPA},
    {"cr.ifs",	CR_IFS},
    {"cr.iim",	CR_IIM},
    {"cr.iha",	CR_IHA},
    {"cr.lid",	CR_LID},
    {"cr.ivr",	CR_IVR},
    {"cr.tpr",	CR_TPR},
    {"cr.eoi",	CR_EOI},
    {"cr.irr0",	CR_IRR0},
    {"cr.irr1",	CR_IRR0 + 1},
    {"cr.irr2",	CR_IRR0 + 2},
    {"cr.irr3",	CR_IRR3},
    {"cr.itv",	CR_ITV},
    {"cr.pmv",	CR_PMV},
    {"cr.cmcv",	CR_CMCV},
    {"cr.lrr0",	CR_LRR0},
    {"cr.lrr1",	CR_LRR1}
  };

#define PSR_MFL         4
#define PSR_IC          13
#define PSR_DFL         18
#define PSR_CPL         32

static const struct const_desc
  {
    const char *name;
    valueT value;
  }
const_bits[] =
  {
    /* PSR constant masks:  */

    /* 0: reserved */
    {"psr.be",	((valueT) 1) << 1},
    {"psr.up",	((valueT) 1) << 2},
    {"psr.ac",	((valueT) 1) << 3},
    {"psr.mfl",	((valueT) 1) << 4},
    {"psr.mfh",	((valueT) 1) << 5},
    /* 6-12: reserved */
    {"psr.ic",	((valueT) 1) << 13},
    {"psr.i",	((valueT) 1) << 14},
    {"psr.pk",	((valueT) 1) << 15},
    /* 16: reserved */
    {"psr.dt",	((valueT) 1) << 17},
    {"psr.dfl",	((valueT) 1) << 18},
    {"psr.dfh",	((valueT) 1) << 19},
    {"psr.sp",	((valueT) 1) << 20},
    {"psr.pp",	((valueT) 1) << 21},
    {"psr.di",	((valueT) 1) << 22},
    {"psr.si",	((valueT) 1) << 23},
    {"psr.db",	((valueT) 1) << 24},
    {"psr.lp",	((valueT) 1) << 25},
    {"psr.tb",	((valueT) 1) << 26},
    {"psr.rt",	((valueT) 1) << 27},
    /* 28-31: reserved */
    /* 32-33: cpl (current privilege level) */
    {"psr.is",	((valueT) 1) << 34},
    {"psr.mc",	((valueT) 1) << 35},
    {"psr.it",	((valueT) 1) << 36},
    {"psr.id",	((valueT) 1) << 37},
    {"psr.da",	((valueT) 1) << 38},
    {"psr.dd",	((valueT) 1) << 39},
    {"psr.ss",	((valueT) 1) << 40},
    /* 41-42: ri (restart instruction) */
    {"psr.ed",	((valueT) 1) << 43},
    {"psr.bn",	((valueT) 1) << 44},
  };

/* indirect register-sets/memory:  */

static const struct
  {
    const char *name;
    unsigned int regnum;
  }
indirect_reg[] =
  {
    { "CPUID",	IND_CPUID },
    { "cpuid",	IND_CPUID },
    { "dbr",	IND_DBR },
    { "dtr",	IND_DTR },
    { "itr",	IND_ITR },
    { "ibr",	IND_IBR },
    { "msr",	IND_MSR },
    { "pkr",	IND_PKR },
    { "pmc",	IND_PMC },
    { "pmd",	IND_PMD },
    { "rr",	IND_RR },
  };

/* Pseudo functions used to indicate relocation types (these functions
   start with an at sign (@).  */
static struct
  {
    const char *name;
    enum pseudo_type
      {
	PSEUDO_FUNC_NONE,
	PSEUDO_FUNC_RELOC,
	PSEUDO_FUNC_CONST,
	PSEUDO_FUNC_REG,
	PSEUDO_FUNC_FLOAT
      }
    type;
    union
      {
	unsigned long ival;
	symbolS *sym;
      }
    u;
  }
pseudo_func[] =
  {
    /* reloc pseudo functions (these must come first!):  */
    { "dtpmod",	PSEUDO_FUNC_RELOC, { 0 } },
    { "dtprel",	PSEUDO_FUNC_RELOC, { 0 } },
    { "fptr",	PSEUDO_FUNC_RELOC, { 0 } },
    { "gprel",	PSEUDO_FUNC_RELOC, { 0 } },
    { "ltoff",	PSEUDO_FUNC_RELOC, { 0 } },
    { "ltoffx",	PSEUDO_FUNC_RELOC, { 0 } },
    { "pcrel",	PSEUDO_FUNC_RELOC, { 0 } },
    { "pltoff",	PSEUDO_FUNC_RELOC, { 0 } },
    { "secrel",	PSEUDO_FUNC_RELOC, { 0 } },
    { "segrel",	PSEUDO_FUNC_RELOC, { 0 } },
    { "tprel",	PSEUDO_FUNC_RELOC, { 0 } },
    { "ltv",	PSEUDO_FUNC_RELOC, { 0 } },
    { NULL, 0, { 0 } },	/* placeholder for FUNC_LT_FPTR_RELATIVE */
    { NULL, 0, { 0 } },	/* placeholder for FUNC_LT_DTP_MODULE */
    { NULL, 0, { 0 } },	/* placeholder for FUNC_LT_DTP_RELATIVE */
    { NULL, 0, { 0 } },	/* placeholder for FUNC_LT_TP_RELATIVE */
    { "iplt",	PSEUDO_FUNC_RELOC, { 0 } },

    /* mbtype4 constants:  */
    { "alt",	PSEUDO_FUNC_CONST, { 0xa } },
    { "brcst",	PSEUDO_FUNC_CONST, { 0x0 } },
    { "mix",	PSEUDO_FUNC_CONST, { 0x8 } },
    { "rev",	PSEUDO_FUNC_CONST, { 0xb } },
    { "shuf",	PSEUDO_FUNC_CONST, { 0x9 } },

    /* fclass constants:  */
    { "nat",	PSEUDO_FUNC_CONST, { 0x100 } },
    { "qnan",	PSEUDO_FUNC_CONST, { 0x080 } },
    { "snan",	PSEUDO_FUNC_CONST, { 0x040 } },
    { "pos",	PSEUDO_FUNC_CONST, { 0x001 } },
    { "neg",	PSEUDO_FUNC_CONST, { 0x002 } },
    { "zero",	PSEUDO_FUNC_CONST, { 0x004 } },
    { "unorm",	PSEUDO_FUNC_CONST, { 0x008 } },
    { "norm",	PSEUDO_FUNC_CONST, { 0x010 } },
    { "inf",	PSEUDO_FUNC_CONST, { 0x020 } },

    { "natval",	PSEUDO_FUNC_CONST, { 0x100 } }, /* old usage */

    /* hint constants: */
    { "pause",	PSEUDO_FUNC_CONST, { 0x0 } },

    /* unwind-related constants:  */
    { "svr4",	PSEUDO_FUNC_CONST,	{ ELFOSABI_NONE } },
    { "hpux",	PSEUDO_FUNC_CONST,	{ ELFOSABI_HPUX } },
    { "nt",	PSEUDO_FUNC_CONST,	{ 2 } },		/* conflicts w/ELFOSABI_NETBSD */
    { "linux",	PSEUDO_FUNC_CONST,	{ ELFOSABI_LINUX } },
    { "freebsd", PSEUDO_FUNC_CONST,	{ ELFOSABI_FREEBSD } },
    { "openvms", PSEUDO_FUNC_CONST,	{ ELFOSABI_OPENVMS } },
    { "nsk",	PSEUDO_FUNC_CONST,	{ ELFOSABI_NSK } },

    /* unwind-related registers:  */
    { "priunat",PSEUDO_FUNC_REG, { REG_PRIUNAT } }
  };

/* 41-bit nop opcodes (one per unit):  */
static const bfd_vma nop[IA64_NUM_UNITS] =
  {
    0x0000000000LL,	/* NIL => break 0 */
    0x0008000000LL,	/* I-unit nop */
    0x0008000000LL,	/* M-unit nop */
    0x4000000000LL,	/* B-unit nop */
    0x0008000000LL,	/* F-unit nop */
    0x0000000000LL,	/* L-"unit" nop immediate */
    0x0008000000LL,	/* X-unit nop */
  };

/* Can't be `const' as it's passed to input routines (which have the
   habit of setting temporary sentinels.  */
static char special_section_name[][20] =
  {
    {".bss"}, {".sbss"}, {".sdata"}, {".rodata"}, {".comment"},
    {".IA_64.unwind"}, {".IA_64.unwind_info"},
    {".init_array"}, {".fini_array"}
  };

/* The best template for a particular sequence of up to three
   instructions:  */
#define N	IA64_NUM_TYPES
static unsigned char best_template[N][N][N];
#undef N

/* Resource dependencies currently in effect */
static struct rsrc {
  int depind;                       /* dependency index */
  const struct ia64_dependency *dependency; /* actual dependency */
  unsigned specific:1,              /* is this a specific bit/regno? */
    link_to_qp_branch:1;           /* will a branch on the same QP clear it?*/
  int index;                        /* specific regno/bit within dependency */
  int note;                         /* optional qualifying note (0 if none) */
#define STATE_NONE 0
#define STATE_STOP 1
#define STATE_SRLZ 2
  int insn_srlz;                    /* current insn serialization state */
  int data_srlz;                    /* current data serialization state */
  int qp_regno;                     /* qualifying predicate for this usage */
  char *file;                       /* what file marked this dependency */
  unsigned int line;                /* what line marked this dependency */
  struct mem_offset mem_offset;     /* optional memory offset hint */
  enum { CMP_NONE, CMP_OR, CMP_AND } cmp_type; /* OR or AND compare? */
  int path;                         /* corresponding code entry index */
} *regdeps = NULL;
static int regdepslen = 0;
static int regdepstotlen = 0;
static const char *dv_mode[] = { "RAW", "WAW", "WAR" };
static const char *dv_sem[] = { "none", "implied", "impliedf",
				"data", "instr", "specific", "stop", "other" };
static const char *dv_cmp_type[] = { "none", "OR", "AND" };

/* Current state of PR mutexation */
static struct qpmutex {
  valueT prmask;
  int path;
} *qp_mutexes = NULL;          /* QP mutex bitmasks */
static int qp_mutexeslen = 0;
static int qp_mutexestotlen = 0;
static valueT qp_safe_across_calls = 0;

/* Current state of PR implications */
static struct qp_imply {
  unsigned p1:6;
  unsigned p2:6;
  unsigned p2_branched:1;
  int path;
} *qp_implies = NULL;
static int qp_implieslen = 0;
static int qp_impliestotlen = 0;

/* Keep track of static GR values so that indirect register usage can
   sometimes be tracked.  */
static struct gr {
  unsigned known:1;
  int path;
  valueT value;
} gr_values[128] = {
  {
    1,
#ifdef INT_MAX
    INT_MAX,
#else
    (((1 << (8 * sizeof(gr_values->path) - 2)) - 1) << 1) + 1,
#endif
    0
  }
};

/* Remember the alignment frag.  */
static fragS *align_frag;

/* These are the routines required to output the various types of
   unwind records.  */

/* A slot_number is a frag address plus the slot index (0-2).  We use the
   frag address here so that if there is a section switch in the middle of
   a function, then instructions emitted to a different section are not
   counted.  Since there may be more than one frag for a function, this
   means we also need to keep track of which frag this address belongs to
   so we can compute inter-frag distances.  This also nicely solves the
   problem with nops emitted for align directives, which can't easily be
   counted, but can easily be derived from frag sizes.  */

typedef struct unw_rec_list {
  unwind_record r;
  unsigned long slot_number;
  fragS *slot_frag;
  struct unw_rec_list *next;
} unw_rec_list;

#define SLOT_NUM_NOT_SET        (unsigned)-1

/* Linked list of saved prologue counts.  A very poor
   implementation of a map from label numbers to prologue counts.  */
typedef struct label_prologue_count
{
  struct label_prologue_count *next;
  unsigned long label_number;
  unsigned int prologue_count;
} label_prologue_count;

typedef struct proc_pending
{
  symbolS *sym;
  struct proc_pending *next;
} proc_pending;

static struct
{
  /* Maintain a list of unwind entries for the current function.  */
  unw_rec_list *list;
  unw_rec_list *tail;

  /* Any unwind entires that should be attached to the current slot
     that an insn is being constructed for.  */
  unw_rec_list *current_entry;

  /* These are used to create the unwind table entry for this function.  */
  proc_pending proc_pending;
  symbolS *info;		/* pointer to unwind info */
  symbolS *personality_routine;
  segT saved_text_seg;
  subsegT saved_text_subseg;
  unsigned int force_unwind_entry : 1;	/* force generation of unwind entry? */

  /* TRUE if processing unwind directives in a prologue region.  */
  unsigned int prologue : 1;
  unsigned int prologue_mask : 4;
  unsigned int prologue_gr : 7;
  unsigned int body : 1;
  unsigned int insn : 1;
  unsigned int prologue_count;	/* number of .prologues seen so far */
  /* Prologue counts at previous .label_state directives.  */
  struct label_prologue_count * saved_prologue_counts;

  /* List of split up .save-s.  */
  unw_p_record *pending_saves;
} unwind;

/* The input value is a negated offset from psp, and specifies an address
   psp - offset.  The encoded value is psp + 16 - (4 * offset).  Thus we
   must add 16 and divide by 4 to get the encoded value.  */

#define ENCODED_PSP_OFFSET(OFFSET) (((OFFSET) + 16) / 4)

typedef void (*vbyte_func) PARAMS ((int, char *, char *));

/* Forward declarations:  */
static void set_section PARAMS ((char *name));
static unsigned int set_regstack PARAMS ((unsigned int, unsigned int,
					  unsigned int, unsigned int));
static void dot_align (int);
static void dot_radix PARAMS ((int));
static void dot_special_section PARAMS ((int));
static void dot_proc PARAMS ((int));
static void dot_fframe PARAMS ((int));
static void dot_vframe PARAMS ((int));
static void dot_vframesp PARAMS ((int));
static void dot_save PARAMS ((int));
static void dot_restore PARAMS ((int));
static void dot_restorereg PARAMS ((int));
static void dot_handlerdata  PARAMS ((int));
static void dot_unwentry PARAMS ((int));
static void dot_altrp PARAMS ((int));
static void dot_savemem PARAMS ((int));
static void dot_saveg PARAMS ((int));
static void dot_savef PARAMS ((int));
static void dot_saveb PARAMS ((int));
static void dot_savegf PARAMS ((int));
static void dot_spill PARAMS ((int));
static void dot_spillreg PARAMS ((int));
static void dot_spillmem PARAMS ((int));
static void dot_label_state PARAMS ((int));
static void dot_copy_state PARAMS ((int));
static void dot_unwabi PARAMS ((int));
static void dot_personality PARAMS ((int));
static void dot_body PARAMS ((int));
static void dot_prologue PARAMS ((int));
static void dot_endp PARAMS ((int));
static void dot_template PARAMS ((int));
static void dot_regstk PARAMS ((int));
static void dot_rot PARAMS ((int));
static void dot_byteorder PARAMS ((int));
static void dot_psr PARAMS ((int));
static void dot_alias PARAMS ((int));
static void dot_ln PARAMS ((int));
static void cross_section PARAMS ((int ref, void (*cons) PARAMS((int)), int ua));
static void dot_xdata PARAMS ((int));
static void stmt_float_cons PARAMS ((int));
static void stmt_cons_ua PARAMS ((int));
static void dot_xfloat_cons PARAMS ((int));
static void dot_xstringer PARAMS ((int));
static void dot_xdata_ua PARAMS ((int));
static void dot_xfloat_cons_ua PARAMS ((int));
static void print_prmask PARAMS ((valueT mask));
static void dot_pred_rel PARAMS ((int));
static void dot_reg_val PARAMS ((int));
static void dot_serialize PARAMS ((int));
static void dot_dv_mode PARAMS ((int));
static void dot_entry PARAMS ((int));
static void dot_mem_offset PARAMS ((int));
static void add_unwind_entry PARAMS((unw_rec_list *, int));
static symbolS *declare_register PARAMS ((const char *name, unsigned int regnum));
static void declare_register_set PARAMS ((const char *, unsigned int, unsigned int));
static unsigned int operand_width PARAMS ((enum ia64_opnd));
static enum operand_match_result operand_match PARAMS ((const struct ia64_opcode *idesc,
							int index,
							expressionS *e));
static int parse_operand PARAMS ((expressionS *, int));
static struct ia64_opcode * parse_operands PARAMS ((struct ia64_opcode *));
static void build_insn PARAMS ((struct slot *, bfd_vma *));
static void emit_one_bundle PARAMS ((void));
static void fix_insn PARAMS ((fixS *, const struct ia64_operand *, valueT));
static bfd_reloc_code_real_type ia64_gen_real_reloc_type PARAMS ((struct symbol *sym,
								  bfd_reloc_code_real_type r_type));
static void insn_group_break PARAMS ((int, int, int));
static void mark_resource PARAMS ((struct ia64_opcode *, const struct ia64_dependency *,
				   struct rsrc *, int depind, int path));
static void add_qp_mutex PARAMS((valueT mask));
static void add_qp_imply PARAMS((int p1, int p2));
static void clear_qp_branch_flag PARAMS((valueT mask));
static void clear_qp_mutex PARAMS((valueT mask));
static void clear_qp_implies PARAMS((valueT p1_mask, valueT p2_mask));
static int has_suffix_p PARAMS((const char *, const char *));
static void clear_register_values PARAMS ((void));
static void print_dependency PARAMS ((const char *action, int depind));
static void instruction_serialization PARAMS ((void));
static void data_serialization PARAMS ((void));
static void remove_marked_resource PARAMS ((struct rsrc *));
static int is_conditional_branch PARAMS ((struct ia64_opcode *));
static int is_taken_branch PARAMS ((struct ia64_opcode *));
static int is_interruption_or_rfi PARAMS ((struct ia64_opcode *));
static int depends_on PARAMS ((int, struct ia64_opcode *));
static int specify_resource PARAMS ((const struct ia64_dependency *,
				     struct ia64_opcode *, int, struct rsrc [], int, int));
static int check_dv PARAMS((struct ia64_opcode *idesc));
static void check_dependencies PARAMS((struct ia64_opcode *));
static void mark_resources PARAMS((struct ia64_opcode *));
static void update_dependencies PARAMS((struct ia64_opcode *));
static void note_register_values PARAMS((struct ia64_opcode *));
static int qp_mutex PARAMS ((int, int, int));
static int resources_match PARAMS ((struct rsrc *, struct ia64_opcode *, int, int, int));
static void output_vbyte_mem PARAMS ((int, char *, char *));
static void count_output PARAMS ((int, char *, char *));
static void output_R1_format PARAMS ((vbyte_func, unw_record_type, int));
static void output_R2_format PARAMS ((vbyte_func, int, int, unsigned long));
static void output_R3_format PARAMS ((vbyte_func, unw_record_type, unsigned long));
static void output_P1_format PARAMS ((vbyte_func, int));
static void output_P2_format PARAMS ((vbyte_func, int, int));
static void output_P3_format PARAMS ((vbyte_func, unw_record_type, int));
static void output_P4_format PARAMS ((vbyte_func, unsigned char *, unsigned long));
static void output_P5_format PARAMS ((vbyte_func, int, unsigned long));
static void output_P6_format PARAMS ((vbyte_func, unw_record_type, int));
static void output_P7_format PARAMS ((vbyte_func, unw_record_type, unsigned long, unsigned long));
static void output_P8_format PARAMS ((vbyte_func, unw_record_type, unsigned long));
static void output_P9_format PARAMS ((vbyte_func, int, int));
static void output_P10_format PARAMS ((vbyte_func, int, int));
static void output_B1_format PARAMS ((vbyte_func, unw_record_type, unsigned long));
static void output_B2_format PARAMS ((vbyte_func, unsigned long, unsigned long));
static void output_B3_format PARAMS ((vbyte_func, unsigned long, unsigned long));
static void output_B4_format PARAMS ((vbyte_func, unw_record_type, unsigned long));
static char format_ab_reg PARAMS ((int, int));
static void output_X1_format PARAMS ((vbyte_func, unw_record_type, int, int, unsigned long,
				      unsigned long));
static void output_X2_format PARAMS ((vbyte_func, int, int, int, int, int, unsigned long));
static void output_X3_format PARAMS ((vbyte_func, unw_record_type, int, int, int, unsigned long,
				      unsigned long));
static void output_X4_format PARAMS ((vbyte_func, int, int, int, int, int, int, unsigned long));
static unw_rec_list *output_endp PARAMS ((void));
static unw_rec_list *output_prologue PARAMS ((void));
static unw_rec_list *output_prologue_gr PARAMS ((unsigned int, unsigned int));
static unw_rec_list *output_body PARAMS ((void));
static unw_rec_list *output_mem_stack_f PARAMS ((unsigned int));
static unw_rec_list *output_mem_stack_v PARAMS ((void));
static unw_rec_list *output_psp_gr PARAMS ((unsigned int));
static unw_rec_list *output_psp_sprel PARAMS ((unsigned int));
static unw_rec_list *output_rp_when PARAMS ((void));
static unw_rec_list *output_rp_gr PARAMS ((unsigned int));
static unw_rec_list *output_rp_br PARAMS ((unsigned int));
static unw_rec_list *output_rp_psprel PARAMS ((unsigned int));
static unw_rec_list *output_rp_sprel PARAMS ((unsigned int));
static unw_rec_list *output_pfs_when PARAMS ((void));
static unw_rec_list *output_pfs_gr PARAMS ((unsigned int));
static unw_rec_list *output_pfs_psprel PARAMS ((unsigned int));
static unw_rec_list *output_pfs_sprel PARAMS ((unsigned int));
static unw_rec_list *output_preds_when PARAMS ((void));
static unw_rec_list *output_preds_gr PARAMS ((unsigned int));
static unw_rec_list *output_preds_psprel PARAMS ((unsigned int));
static unw_rec_list *output_preds_sprel PARAMS ((unsigned int));
static unw_rec_list *output_fr_mem PARAMS ((unsigned int));
static unw_rec_list *output_frgr_mem PARAMS ((unsigned int, unsigned int));
static unw_rec_list *output_gr_gr PARAMS ((unsigned int, unsigned int));
static unw_rec_list *output_gr_mem PARAMS ((unsigned int));
static unw_rec_list *output_br_mem PARAMS ((unsigned int));
static unw_rec_list *output_br_gr PARAMS ((unsigned int, unsigned int));
static unw_rec_list *output_spill_base PARAMS ((unsigned int));
static unw_rec_list *output_unat_when PARAMS ((void));
static unw_rec_list *output_unat_gr PARAMS ((unsigned int));
static unw_rec_list *output_unat_psprel PARAMS ((unsigned int));
static unw_rec_list *output_unat_sprel PARAMS ((unsigned int));
static unw_rec_list *output_lc_when PARAMS ((void));
static unw_rec_list *output_lc_gr PARAMS ((unsigned int));
static unw_rec_list *output_lc_psprel PARAMS ((unsigned int));
static unw_rec_list *output_lc_sprel PARAMS ((unsigned int));
static unw_rec_list *output_fpsr_when PARAMS ((void));
static unw_rec_list *output_fpsr_gr PARAMS ((unsigned int));
static unw_rec_list *output_fpsr_psprel PARAMS ((unsigned int));
static unw_rec_list *output_fpsr_sprel PARAMS ((unsigned int));
static unw_rec_list *output_priunat_when_gr PARAMS ((void));
static unw_rec_list *output_priunat_when_mem PARAMS ((void));
static unw_rec_list *output_priunat_gr PARAMS ((unsigned int));
static unw_rec_list *output_priunat_psprel PARAMS ((unsigned int));
static unw_rec_list *output_priunat_sprel PARAMS ((unsigned int));
static unw_rec_list *output_bsp_when PARAMS ((void));
static unw_rec_list *output_bsp_gr PARAMS ((unsigned int));
static unw_rec_list *output_bsp_psprel PARAMS ((unsigned int));
static unw_rec_list *output_bsp_sprel PARAMS ((unsigned int));
static unw_rec_list *output_bspstore_when PARAMS ((void));
static unw_rec_list *output_bspstore_gr PARAMS ((unsigned int));
static unw_rec_list *output_bspstore_psprel PARAMS ((unsigned int));
static unw_rec_list *output_bspstore_sprel PARAMS ((unsigned int));
static unw_rec_list *output_rnat_when PARAMS ((void));
static unw_rec_list *output_rnat_gr PARAMS ((unsigned int));
static unw_rec_list *output_rnat_psprel PARAMS ((unsigned int));
static unw_rec_list *output_rnat_sprel PARAMS ((unsigned int));
static unw_rec_list *output_unwabi PARAMS ((unsigned long, unsigned long));
static unw_rec_list *output_epilogue PARAMS ((unsigned long));
static unw_rec_list *output_label_state PARAMS ((unsigned long));
static unw_rec_list *output_copy_state PARAMS ((unsigned long));
static unw_rec_list *output_spill_psprel PARAMS ((unsigned int, unsigned int, unsigned int,
						    unsigned int));
static unw_rec_list *output_spill_sprel PARAMS ((unsigned int, unsigned int, unsigned int,
						   unsigned int));
static unw_rec_list *output_spill_reg PARAMS ((unsigned int, unsigned int, unsigned int,
						 unsigned int, unsigned int));
static void process_one_record PARAMS ((unw_rec_list *, vbyte_func));
static void process_unw_records PARAMS ((unw_rec_list *, vbyte_func));
static int calc_record_size PARAMS ((unw_rec_list *));
static void set_imask PARAMS ((unw_rec_list *, unsigned long, unsigned long, unsigned int));
static unsigned long slot_index PARAMS ((unsigned long, fragS *,
					 unsigned long, fragS *,
					 int));
static unw_rec_list *optimize_unw_records PARAMS ((unw_rec_list *));
static void fixup_unw_records PARAMS ((unw_rec_list *, int));
static int parse_predicate_and_operand PARAMS ((expressionS *, unsigned *, const char *));
static void convert_expr_to_ab_reg PARAMS ((const expressionS *, unsigned int *, unsigned int *, const char *, int));
static void convert_expr_to_xy_reg PARAMS ((const expressionS *, unsigned int *, unsigned int *, const char *, int));
static unsigned int get_saved_prologue_count PARAMS ((unsigned long));
static void save_prologue_count PARAMS ((unsigned long, unsigned int));
static void free_saved_prologue_counts PARAMS ((void));

/* Determine if application register REGNUM resides only in the integer
   unit (as opposed to the memory unit).  */
static int
ar_is_only_in_integer_unit (int reg)
{
  reg -= REG_AR;
  return reg >= 64 && reg <= 111;
}

/* Determine if application register REGNUM resides only in the memory 
   unit (as opposed to the integer unit).  */
static int
ar_is_only_in_memory_unit (int reg)
{
  reg -= REG_AR;
  return reg >= 0 && reg <= 47;
}

/* Switch to section NAME and create section if necessary.  It's
   rather ugly that we have to manipulate input_line_pointer but I
   don't see any other way to accomplish the same thing without
   changing obj-elf.c (which may be the Right Thing, in the end).  */
static void
set_section (name)
     char *name;
{
  char *saved_input_line_pointer;

  saved_input_line_pointer = input_line_pointer;
  input_line_pointer = name;
  obj_elf_section (0);
  input_line_pointer = saved_input_line_pointer;
}

/* Map 's' to SHF_IA_64_SHORT.  */

int
ia64_elf_section_letter (letter, ptr_msg)
     int letter;
     char **ptr_msg;
{
  if (letter == 's')
    return SHF_IA_64_SHORT;
  else if (letter == 'o')
    return SHF_LINK_ORDER;

  *ptr_msg = _("Bad .section directive: want a,o,s,w,x,M,S,G,T in string");
  return -1;
}

/* Map SHF_IA_64_SHORT to SEC_SMALL_DATA.  */

flagword
ia64_elf_section_flags (flags, attr, type)
     flagword flags;
     int attr, type ATTRIBUTE_UNUSED;
{
  if (attr & SHF_IA_64_SHORT)
    flags |= SEC_SMALL_DATA;
  return flags;
}

int
ia64_elf_section_type (str, len)
     const char *str;
     size_t len;
{
#define STREQ(s) ((len == sizeof (s) - 1) && (strncmp (str, s, sizeof (s) - 1) == 0))

  if (STREQ (ELF_STRING_ia64_unwind_info))
    return SHT_PROGBITS;

  if (STREQ (ELF_STRING_ia64_unwind_info_once))
    return SHT_PROGBITS;

  if (STREQ (ELF_STRING_ia64_unwind))
    return SHT_IA_64_UNWIND;

  if (STREQ (ELF_STRING_ia64_unwind_once))
    return SHT_IA_64_UNWIND;

  if (STREQ ("unwind"))
    return SHT_IA_64_UNWIND;

  return -1;
#undef STREQ
}

static unsigned int
set_regstack (ins, locs, outs, rots)
     unsigned int ins, locs, outs, rots;
{
  /* Size of frame.  */
  unsigned int sof;

  sof = ins + locs + outs;
  if (sof > 96)
    {
      as_bad ("Size of frame exceeds maximum of 96 registers");
      return 0;
    }
  if (rots > sof)
    {
      as_warn ("Size of rotating registers exceeds frame size");
      return 0;
    }
  md.in.base = REG_GR + 32;
  md.loc.base = md.in.base + ins;
  md.out.base = md.loc.base + locs;

  md.in.num_regs  = ins;
  md.loc.num_regs = locs;
  md.out.num_regs = outs;
  md.rot.num_regs = rots;
  return sof;
}

void
ia64_flush_insns ()
{
  struct label_fix *lfix;
  segT saved_seg;
  subsegT saved_subseg;
  unw_rec_list *ptr;
  bfd_boolean mark;

  if (!md.last_text_seg)
    return;

  saved_seg = now_seg;
  saved_subseg = now_subseg;

  subseg_set (md.last_text_seg, 0);

  while (md.num_slots_in_use > 0)
    emit_one_bundle ();		/* force out queued instructions */

  /* In case there are labels following the last instruction, resolve
     those now.  */
  mark = FALSE;
  for (lfix = CURR_SLOT.label_fixups; lfix; lfix = lfix->next)
    {
      symbol_set_value_now (lfix->sym);
      mark |= lfix->dw2_mark_labels;
    }
  if (mark)
    {
      dwarf2_where (&CURR_SLOT.debug_line);
      CURR_SLOT.debug_line.flags |= DWARF2_FLAG_BASIC_BLOCK;
      dwarf2_gen_line_info (frag_now_fix (), &CURR_SLOT.debug_line);
    }
  CURR_SLOT.label_fixups = 0;

  for (lfix = CURR_SLOT.tag_fixups; lfix; lfix = lfix->next)
    symbol_set_value_now (lfix->sym);
  CURR_SLOT.tag_fixups = 0;

  /* In case there are unwind directives following the last instruction,
     resolve those now.  We only handle prologue, body, and endp directives
     here.  Give an error for others.  */
  for (ptr = unwind.current_entry; ptr; ptr = ptr->next)
    {
      switch (ptr->r.type)
	{
	case prologue:
	case prologue_gr:
	case body:
	case endp:
	  ptr->slot_number = (unsigned long) frag_more (0);
	  ptr->slot_frag = frag_now;
	  break;

	  /* Allow any record which doesn't have a "t" field (i.e.,
	     doesn't relate to a particular instruction).  */
	case unwabi:
	case br_gr:
	case copy_state:
	case fr_mem:
	case frgr_mem:
	case gr_gr:
	case gr_mem:
	case label_state:
	case rp_br:
	case spill_base:
	case spill_mask:
	  /* nothing */
	  break;

	default:
	  as_bad (_("Unwind directive not followed by an instruction."));
	  break;
	}
    }
  unwind.current_entry = NULL;

  subseg_set (saved_seg, saved_subseg);

  if (md.qp.X_op == O_register)
    as_bad ("qualifying predicate not followed by instruction");
}

static void
ia64_do_align (int nbytes)
{
  char *saved_input_line_pointer = input_line_pointer;

  input_line_pointer = "";
  s_align_bytes (nbytes);
  input_line_pointer = saved_input_line_pointer;
}

void
ia64_cons_align (nbytes)
     int nbytes;
{
  if (md.auto_align)
    {
      char *saved_input_line_pointer = input_line_pointer;
      input_line_pointer = "";
      s_align_bytes (nbytes);
      input_line_pointer = saved_input_line_pointer;
    }
}

/* Output COUNT bytes to a memory location.  */
static char *vbyte_mem_ptr = NULL;

void
output_vbyte_mem (count, ptr, comment)
     int count;
     char *ptr;
     char *comment ATTRIBUTE_UNUSED;
{
  int x;
  if (vbyte_mem_ptr == NULL)
    abort ();

  if (count == 0)
    return;
  for (x = 0; x < count; x++)
    *(vbyte_mem_ptr++) = ptr[x];
}

/* Count the number of bytes required for records.  */
static int vbyte_count = 0;
void
count_output (count, ptr, comment)
     int count;
     char *ptr ATTRIBUTE_UNUSED;
     char *comment ATTRIBUTE_UNUSED;
{
  vbyte_count += count;
}

static void
output_R1_format (f, rtype, rlen)
     vbyte_func f;
     unw_record_type rtype;
     int rlen;
{
  int r = 0;
  char byte;
  if (rlen > 0x1f)
    {
      output_R3_format (f, rtype, rlen);
      return;
    }

  if (rtype == body)
    r = 1;
  else if (rtype != prologue)
    as_bad ("record type is not valid");

  byte = UNW_R1 | (r << 5) | (rlen & 0x1f);
  (*f) (1, &byte, NULL);
}

static void
output_R2_format (f, mask, grsave, rlen)
     vbyte_func f;
     int mask, grsave;
     unsigned long rlen;
{
  char bytes[20];
  int count = 2;
  mask = (mask & 0x0f);
  grsave = (grsave & 0x7f);

  bytes[0] = (UNW_R2 | (mask >> 1));
  bytes[1] = (((mask & 0x01) << 7) | grsave);
  count += output_leb128 (bytes + 2, rlen, 0);
  (*f) (count, bytes, NULL);
}

static void
output_R3_format (f, rtype, rlen)
     vbyte_func f;
     unw_record_type rtype;
     unsigned long rlen;
{
  int r = 0, count;
  char bytes[20];
  if (rlen <= 0x1f)
    {
      output_R1_format (f, rtype, rlen);
      return;
    }

  if (rtype == body)
    r = 1;
  else if (rtype != prologue)
    as_bad ("record type is not valid");
  bytes[0] = (UNW_R3 | r);
  count = output_leb128 (bytes + 1, rlen, 0);
  (*f) (count + 1, bytes, NULL);
}

static void
output_P1_format (f, brmask)
     vbyte_func f;
     int brmask;
{
  char byte;
  byte = UNW_P1 | (brmask & 0x1f);
  (*f) (1, &byte, NULL);
}

static void
output_P2_format (f, brmask, gr)
     vbyte_func f;
     int brmask;
     int gr;
{
  char bytes[2];
  brmask = (brmask & 0x1f);
  bytes[0] = UNW_P2 | (brmask >> 1);
  bytes[1] = (((brmask & 1) << 7) | gr);
  (*f) (2, bytes, NULL);
}

static void
output_P3_format (f, rtype, reg)
     vbyte_func f;
     unw_record_type rtype;
     int reg;
{
  char bytes[2];
  int r = 0;
  reg = (reg & 0x7f);
  switch (rtype)
    {
    case psp_gr:
      r = 0;
      break;
    case rp_gr:
      r = 1;
      break;
    case pfs_gr:
      r = 2;
      break;
    case preds_gr:
      r = 3;
      break;
    case unat_gr:
      r = 4;
      break;
    case lc_gr:
      r = 5;
      break;
    case rp_br:
      r = 6;
      break;
    case rnat_gr:
      r = 7;
      break;
    case bsp_gr:
      r = 8;
      break;
    case bspstore_gr:
      r = 9;
      break;
    case fpsr_gr:
      r = 10;
      break;
    case priunat_gr:
      r = 11;
      break;
    default:
      as_bad ("Invalid record type for P3 format.");
    }
  bytes[0] = (UNW_P3 | (r >> 1));
  bytes[1] = (((r & 1) << 7) | reg);
  (*f) (2, bytes, NULL);
}

static void
output_P4_format (f, imask, imask_size)
     vbyte_func f;
     unsigned char *imask;
     unsigned long imask_size;
{
  imask[0] = UNW_P4;
  (*f) (imask_size, (char *) imask, NULL);
}

static void
output_P5_format (f, grmask, frmask)
     vbyte_func f;
     int grmask;
     unsigned long frmask;
{
  char bytes[4];
  grmask = (grmask & 0x0f);

  bytes[0] = UNW_P5;
  bytes[1] = ((grmask << 4) | ((frmask & 0x000f0000) >> 16));
  bytes[2] = ((frmask & 0x0000ff00) >> 8);
  bytes[3] = (frmask & 0x000000ff);
  (*f) (4, bytes, NULL);
}

static void
output_P6_format (f, rtype, rmask)
     vbyte_func f;
     unw_record_type rtype;
     int rmask;
{
  char byte;
  int r = 0;

  if (rtype == gr_mem)
    r = 1;
  else if (rtype != fr_mem)
    as_bad ("Invalid record type for format P6");
  byte = (UNW_P6 | (r << 4) | (rmask & 0x0f));
  (*f) (1, &byte, NULL);
}

static void
output_P7_format (f, rtype, w1, w2)
     vbyte_func f;
     unw_record_type rtype;
     unsigned long w1;
     unsigned long w2;
{
  char bytes[20];
  int count = 1;
  int r = 0;
  count += output_leb128 (bytes + 1, w1, 0);
  switch (rtype)
    {
    case mem_stack_f:
      r = 0;
      count += output_leb128 (bytes + count, w2 >> 4, 0);
      break;
    case mem_stack_v:
      r = 1;
      break;
    case spill_base:
      r = 2;
      break;
    case psp_sprel:
      r = 3;
      break;
    case rp_when:
      r = 4;
      break;
    case rp_psprel:
      r = 5;
      break;
    case pfs_when:
      r = 6;
      break;
    case pfs_psprel:
      r = 7;
      break;
    case preds_when:
      r = 8;
      break;
    case preds_psprel:
      r = 9;
      break;
    case lc_when:
      r = 10;
      break;
    case lc_psprel:
      r = 11;
      break;
    case unat_when:
      r = 12;
      break;
    case unat_psprel:
      r = 13;
      break;
    case fpsr_when:
      r = 14;
      break;
    case fpsr_psprel:
      r = 15;
      break;
    default:
      break;
    }
  bytes[0] = (UNW_P7 | r);
  (*f) (count, bytes, NULL);
}

static void
output_P8_format (f, rtype, t)
     vbyte_func f;
     unw_record_type rtype;
     unsigned long t;
{
  char bytes[20];
  int r = 0;
  int count = 2;
  bytes[0] = UNW_P8;
  switch (rtype)
    {
    case rp_sprel:
      r = 1;
      break;
    case pfs_sprel:
      r = 2;
      break;
    case preds_sprel:
      r = 3;
      break;
    case lc_sprel:
      r = 4;
      break;
    case unat_sprel:
      r = 5;
      break;
    case fpsr_sprel:
      r = 6;
      break;
    case bsp_when:
      r = 7;
      break;
    case bsp_psprel:
      r = 8;
      break;
    case bsp_sprel:
      r = 9;
      break;
    case bspstore_when:
      r = 10;
      break;
    case bspstore_psprel:
      r = 11;
      break;
    case bspstore_sprel:
      r = 12;
      break;
    case rnat_when:
      r = 13;
      break;
    case rnat_psprel:
      r = 14;
      break;
    case rnat_sprel:
      r = 15;
      break;
    case priunat_when_gr:
      r = 16;
      break;
    case priunat_psprel:
      r = 17;
      break;
    case priunat_sprel:
      r = 18;
      break;
    case priunat_when_mem:
      r = 19;
      break;
    default:
      break;
    }
  bytes[1] = r;
  count += output_leb128 (bytes + 2, t, 0);
  (*f) (count, bytes, NULL);
}

static void
output_P9_format (f, grmask, gr)
     vbyte_func f;
     int grmask;
     int gr;
{
  char bytes[3];
  bytes[0] = UNW_P9;
  bytes[1] = (grmask & 0x0f);
  bytes[2] = (gr & 0x7f);
  (*f) (3, bytes, NULL);
}

static void
output_P10_format (f, abi, context)
     vbyte_func f;
     int abi;
     int context;
{
  char bytes[3];
  bytes[0] = UNW_P10;
  bytes[1] = (abi & 0xff);
  bytes[2] = (context & 0xff);
  (*f) (3, bytes, NULL);
}

static void
output_B1_format (f, rtype, label)
     vbyte_func f;
     unw_record_type rtype;
     unsigned long label;
{
  char byte;
  int r = 0;
  if (label > 0x1f)
    {
      output_B4_format (f, rtype, label);
      return;
    }
  if (rtype == copy_state)
    r = 1;
  else if (rtype != label_state)
    as_bad ("Invalid record type for format B1");

  byte = (UNW_B1 | (r << 5) | (label & 0x1f));
  (*f) (1, &byte, NULL);
}

static void
output_B2_format (f, ecount, t)
     vbyte_func f;
     unsigned long ecount;
     unsigned long t;
{
  char bytes[20];
  int count = 1;
  if (ecount > 0x1f)
    {
      output_B3_format (f, ecount, t);
      return;
    }
  bytes[0] = (UNW_B2 | (ecount & 0x1f));
  count += output_leb128 (bytes + 1, t, 0);
  (*f) (count, bytes, NULL);
}

static void
output_B3_format (f, ecount, t)
     vbyte_func f;
     unsigned long ecount;
     unsigned long t;
{
  char bytes[20];
  int count = 1;
  if (ecount <= 0x1f)
    {
      output_B2_format (f, ecount, t);
      return;
    }
  bytes[0] = UNW_B3;
  count += output_leb128 (bytes + 1, t, 0);
  count += output_leb128 (bytes + count, ecount, 0);
  (*f) (count, bytes, NULL);
}

static void
output_B4_format (f, rtype, label)
     vbyte_func f;
     unw_record_type rtype;
     unsigned long label;
{
  char bytes[20];
  int r = 0;
  int count = 1;
  if (label <= 0x1f)
    {
      output_B1_format (f, rtype, label);
      return;
    }

  if (rtype == copy_state)
    r = 1;
  else if (rtype != label_state)
    as_bad ("Invalid record type for format B1");

  bytes[0] = (UNW_B4 | (r << 3));
  count += output_leb128 (bytes + 1, label, 0);
  (*f) (count, bytes, NULL);
}

static char
format_ab_reg (ab, reg)
     int ab;
     int reg;
{
  int ret;
  ab = (ab & 3);
  reg = (reg & 0x1f);
  ret = (ab << 5) | reg;
  return ret;
}

static void
output_X1_format (f, rtype, ab, reg, t, w1)
     vbyte_func f;
     unw_record_type rtype;
     int ab, reg;
     unsigned long t;
     unsigned long w1;
{
  char bytes[20];
  int r = 0;
  int count = 2;
  bytes[0] = UNW_X1;

  if (rtype == spill_sprel)
    r = 1;
  else if (rtype != spill_psprel)
    as_bad ("Invalid record type for format X1");
  bytes[1] = ((r << 7) | format_ab_reg (ab, reg));
  count += output_leb128 (bytes + 2, t, 0);
  count += output_leb128 (bytes + count, w1, 0);
  (*f) (count, bytes, NULL);
}

static void
output_X2_format (f, ab, reg, x, y, treg, t)
     vbyte_func f;
     int ab, reg;
     int x, y, treg;
     unsigned long t;
{
  char bytes[20];
  int count = 3;
  bytes[0] = UNW_X2;
  bytes[1] = (((x & 1) << 7) | format_ab_reg (ab, reg));
  bytes[2] = (((y & 1) << 7) | (treg & 0x7f));
  count += output_leb128 (bytes + 3, t, 0);
  (*f) (count, bytes, NULL);
}

static void
output_X3_format (f, rtype, qp, ab, reg, t, w1)
     vbyte_func f;
     unw_record_type rtype;
     int qp;
     int ab, reg;
     unsigned long t;
     unsigned long w1;
{
  char bytes[20];
  int r = 0;
  int count = 3;
  bytes[0] = UNW_X3;

  if (rtype == spill_sprel_p)
    r = 1;
  else if (rtype != spill_psprel_p)
    as_bad ("Invalid record type for format X3");
  bytes[1] = ((r << 7) | (qp & 0x3f));
  bytes[2] = format_ab_reg (ab, reg);
  count += output_leb128 (bytes + 3, t, 0);
  count += output_leb128 (bytes + count, w1, 0);
  (*f) (count, bytes, NULL);
}

static void
output_X4_format (f, qp, ab, reg, x, y, treg, t)
     vbyte_func f;
     int qp;
     int ab, reg;
     int x, y, treg;
     unsigned long t;
{
  char bytes[20];
  int count = 4;
  bytes[0] = UNW_X4;
  bytes[1] = (qp & 0x3f);
  bytes[2] = (((x & 1) << 7) | format_ab_reg (ab, reg));
  bytes[3] = (((y & 1) << 7) | (treg & 0x7f));
  count += output_leb128 (bytes + 4, t, 0);
  (*f) (count, bytes, NULL);
}

/* This function checks whether there are any outstanding .save-s and
   discards them if so.  */

static void
check_pending_save (void)
{
  if (unwind.pending_saves)
    {
      unw_rec_list *cur, *prev;

      as_warn ("Previous .save incomplete");
      for (cur = unwind.list, prev = NULL; cur; )
	if (&cur->r.record.p == unwind.pending_saves)
	  {
	    if (prev)
	      prev->next = cur->next;
	    else
	      unwind.list = cur->next;
	    if (cur == unwind.tail)
	      unwind.tail = prev;
	    if (cur == unwind.current_entry)
	      unwind.current_entry = cur->next;
	    /* Don't free the first discarded record, it's being used as
	       terminator for (currently) br_gr and gr_gr processing, and
	       also prevents leaving a dangling pointer to it in its
	       predecessor.  */
	    cur->r.record.p.grmask = 0;
	    cur->r.record.p.brmask = 0;
	    cur->r.record.p.frmask = 0;
	    prev = cur->r.record.p.next;
	    cur->r.record.p.next = NULL;
	    cur = prev;
	    break;
	  }
	else
	  {
	    prev = cur;
	    cur = cur->next;
	  }
      while (cur)
	{
	  prev = cur;
	  cur = cur->r.record.p.next;
	  free (prev);
	}
      unwind.pending_saves = NULL;
    }
}

/* This function allocates a record list structure, and initializes fields.  */

static unw_rec_list *
alloc_record (unw_record_type t)
{
  unw_rec_list *ptr;
  ptr = xmalloc (sizeof (*ptr));
  memset (ptr, 0, sizeof (*ptr));
  ptr->slot_number = SLOT_NUM_NOT_SET;
  ptr->r.type = t;
  return ptr;
}

/* Dummy unwind record used for calculating the length of the last prologue or
   body region.  */

static unw_rec_list *
output_endp ()
{
  unw_rec_list *ptr = alloc_record (endp);
  return ptr;
}

static unw_rec_list *
output_prologue ()
{
  unw_rec_list *ptr = alloc_record (prologue);
  memset (&ptr->r.record.r.mask, 0, sizeof (ptr->r.record.r.mask));
  return ptr;
}

static unw_rec_list *
output_prologue_gr (saved_mask, reg)
     unsigned int saved_mask;
     unsigned int reg;
{
  unw_rec_list *ptr = alloc_record (prologue_gr);
  memset (&ptr->r.record.r.mask, 0, sizeof (ptr->r.record.r.mask));
  ptr->r.record.r.grmask = saved_mask;
  ptr->r.record.r.grsave = reg;
  return ptr;
}

static unw_rec_list *
output_body ()
{
  unw_rec_list *ptr = alloc_record (body);
  return ptr;
}

static unw_rec_list *
output_mem_stack_f (size)
     unsigned int size;
{
  unw_rec_list *ptr = alloc_record (mem_stack_f);
  ptr->r.record.p.size = size;
  return ptr;
}

static unw_rec_list *
output_mem_stack_v ()
{
  unw_rec_list *ptr = alloc_record (mem_stack_v);
  return ptr;
}

static unw_rec_list *
output_psp_gr (gr)
     unsigned int gr;
{
  unw_rec_list *ptr = alloc_record (psp_gr);
  ptr->r.record.p.r.gr = gr;
  return ptr;
}

static unw_rec_list *
output_psp_sprel (offset)
     unsigned int offset;
{
  unw_rec_list *ptr = alloc_record (psp_sprel);
  ptr->r.record.p.off.sp = offset / 4;
  return ptr;
}

static unw_rec_list *
output_rp_when ()
{
  unw_rec_list *ptr = alloc_record (rp_when);
  return ptr;
}

static unw_rec_list *
output_rp_gr (gr)
     unsigned int gr;
{
  unw_rec_list *ptr = alloc_record (rp_gr);
  ptr->r.record.p.r.gr = gr;
  return ptr;
}

static unw_rec_list *
output_rp_br (br)
     unsigned int br;
{
  unw_rec_list *ptr = alloc_record (rp_br);
  ptr->r.record.p.r.br = br;
  return ptr;
}

static unw_rec_list *
output_rp_psprel (offset)
     unsigned int offset;
{
  unw_rec_list *ptr = alloc_record (rp_psprel);
  ptr->r.record.p.off.psp = ENCODED_PSP_OFFSET (offset);
  return ptr;
}

static unw_rec_list *
output_rp_sprel (offset)
     unsigned int offset;
{
  unw_rec_list *ptr = alloc_record (rp_sprel);
  ptr->r.record.p.off.sp = offset / 4;
  return ptr;
}

static unw_rec_list *
output_pfs_when ()
{
  unw_rec_list *ptr = alloc_record (pfs_when);
  return ptr;
}

static unw_rec_list *
output_pfs_gr (gr)
     unsigned int gr;
{
  unw_rec_list *ptr = alloc_record (pfs_gr);
  ptr->r.record.p.r.gr = gr;
  return ptr;
}

static unw_rec_list *
output_pfs_psprel (offset)
     unsigned int offset;
{
  unw_rec_list *ptr = alloc_record (pfs_psprel);
  ptr->r.record.p.off.psp = ENCODED_PSP_OFFSET (offset);
  return ptr;
}

static unw_rec_list *
output_pfs_sprel (offset)
     unsigned int offset;
{
  unw_rec_list *ptr = alloc_record (pfs_sprel);
  ptr->r.record.p.off.sp = offset / 4;
  return ptr;
}

static unw_rec_list *
output_preds_when ()
{
  unw_rec_list *ptr = alloc_record (preds_when);
  return ptr;
}

static unw_rec_list *
output_preds_gr (gr)
     unsigned int gr;
{
  unw_rec_list *ptr = alloc_record (preds_gr);
  ptr->r.record.p.r.gr = gr;
  return ptr;
}

static unw_rec_list *
output_preds_psprel (offset)
     unsigned int offset;
{
  unw_rec_list *ptr = alloc_record (preds_psprel);
  ptr->r.record.p.off.psp = ENCODED_PSP_OFFSET (offset);
  return ptr;
}

static unw_rec_list *
output_preds_sprel (offset)
     unsigned int offset;
{
  unw_rec_list *ptr = alloc_record (preds_sprel);
  ptr->r.record.p.off.sp = offset / 4;
  return ptr;
}

static unw_rec_list *
output_fr_mem (mask)
     unsigned int mask;
{
  unw_rec_list *ptr = alloc_record (fr_mem);
  unw_rec_list *cur = ptr;

  ptr->r.record.p.frmask = mask;
  unwind.pending_saves = &ptr->r.record.p;
  for (;;)
    {
      unw_rec_list *prev = cur;

      /* Clear least significant set bit.  */
      mask &= ~(mask & (~mask + 1));
      if (!mask)
	return ptr;
      cur = alloc_record (fr_mem);
      cur->r.record.p.frmask = mask;
      /* Retain only least significant bit.  */
      prev->r.record.p.frmask ^= mask;
      prev->r.record.p.next = cur;
    }
}

static unw_rec_list *
output_frgr_mem (gr_mask, fr_mask)
     unsigned int gr_mask;
     unsigned int fr_mask;
{
  unw_rec_list *ptr = alloc_record (frgr_mem);
  unw_rec_list *cur = ptr;

  unwind.pending_saves = &cur->r.record.p;
  cur->r.record.p.frmask = fr_mask;
  while (fr_mask)
    {
      unw_rec_list *prev = cur;

      /* Clear least significant set bit.  */
      fr_mask &= ~(fr_mask & (~fr_mask + 1));
      if (!gr_mask && !fr_mask)
	return ptr;
      cur = alloc_record (frgr_mem);
      cur->r.record.p.frmask = fr_mask;
      /* Retain only least significant bit.  */
      prev->r.record.p.frmask ^= fr_mask;
      prev->r.record.p.next = cur;
    }
  cur->r.record.p.grmask = gr_mask;
  for (;;)
    {
      unw_rec_list *prev = cur;

      /* Clear least significant set bit.  */
      gr_mask &= ~(gr_mask & (~gr_mask + 1));
      if (!gr_mask)
	return ptr;
      cur = alloc_record (frgr_mem);
      cur->r.record.p.grmask = gr_mask;
      /* Retain only least significant bit.  */
      prev->r.record.p.grmask ^= gr_mask;
      prev->r.record.p.next = cur;
    }
}

static unw_rec_list *
output_gr_gr (mask, reg)
     unsigned int mask;
     unsigned int reg;
{
  unw_rec_list *ptr = alloc_record (gr_gr);
  unw_rec_list *cur = ptr;

  ptr->r.record.p.grmask = mask;
  ptr->r.record.p.r.gr = reg;
  unwind.pending_saves = &ptr->r.record.p;
  for (;;)
    {
      unw_rec_list *prev = cur;

      /* Clear least significant set bit.  */
      mask &= ~(mask & (~mask + 1));
      if (!mask)
	return ptr;
      cur = alloc_record (gr_gr);
      cur->r.record.p.grmask = mask;
      /* Indicate this record shouldn't be output.  */
      cur->r.record.p.r.gr = REG_NUM;
      /* Retain only least significant bit.  */
      prev->r.record.p.grmask ^= mask;
      prev->r.record.p.next = cur;
    }
}

static unw_rec_list *
output_gr_mem (mask)
     unsigned int mask;
{
  unw_rec_list *ptr = alloc_record (gr_mem);
  unw_rec_list *cur = ptr;

  ptr->r.record.p.grmask = mask;
  unwind.pending_saves = &ptr->r.record.p;
  for (;;)
    {
      unw_rec_list *prev = cur;

      /* Clear least significant set bit.  */
      mask &= ~(mask & (~mask + 1));
      if (!mask)
	return ptr;
      cur = alloc_record (gr_mem);
      cur->r.record.p.grmask = mask;
      /* Retain only least significant bit.  */
      prev->r.record.p.grmask ^= mask;
      prev->r.record.p.next = cur;
    }
}

static unw_rec_list *
output_br_mem (unsigned int mask)
{
  unw_rec_list *ptr = alloc_record (br_mem);
  unw_rec_list *cur = ptr;

  ptr->r.record.p.brmask = mask;
  unwind.pending_saves = &ptr->r.record.p;
  for (;;)
    {
      unw_rec_list *prev = cur;

      /* Clear least significant set bit.  */
      mask &= ~(mask & (~mask + 1));
      if (!mask)
	return ptr;
      cur = alloc_record (br_mem);
      cur->r.record.p.brmask = mask;
      /* Retain only least significant bit.  */
      prev->r.record.p.brmask ^= mask;
      prev->r.record.p.next = cur;
    }
}

static unw_rec_list *
output_br_gr (mask, reg)
     unsigned int mask;
     unsigned int reg;
{
  unw_rec_list *ptr = alloc_record (br_gr);
  unw_rec_list *cur = ptr;

  ptr->r.record.p.brmask = mask;
  ptr->r.record.p.r.gr = reg;
  unwind.pending_saves = &ptr->r.record.p;
  for (;;)
    {
      unw_rec_list *prev = cur;

      /* Clear least significant set bit.  */
      mask &= ~(mask & (~mask + 1));
      if (!mask)
	return ptr;
      cur = alloc_record (br_gr);
      cur->r.record.p.brmask = mask;
      /* Indicate this record shouldn't be output.  */
      cur->r.record.p.r.gr = REG_NUM;
      /* Retain only least significant bit.  */
      prev->r.record.p.brmask ^= mask;
      prev->r.record.p.next = cur;
    }
}

static unw_rec_list *
output_spill_base (offset)
     unsigned int offset;
{
  unw_rec_list *ptr = alloc_record (spill_base);
  ptr->r.record.p.off.psp = ENCODED_PSP_OFFSET (offset);
  return ptr;
}

static unw_rec_list *
output_unat_when ()
{
  unw_rec_list *ptr = alloc_record (unat_when);
  return ptr;
}

static unw_rec_list *
output_unat_gr (gr)
     unsigned int gr;
{
  unw_rec_list *ptr = alloc_record (unat_gr);
  ptr->r.record.p.r.gr = gr;
  return ptr;
}

static unw_rec_list *
output_unat_psprel (offset)
     unsigned int offset;
{
  unw_rec_list *ptr = alloc_record (unat_psprel);
  ptr->r.record.p.off.psp = ENCODED_PSP_OFFSET (offset);
  return ptr;
}

static unw_rec_list *
output_unat_sprel (offset)
     unsigned int offset;
{
  unw_rec_list *ptr = alloc_record (unat_sprel);
  ptr->r.record.p.off.sp = offset / 4;
  return ptr;
}

static unw_rec_list *
output_lc_when ()
{
  unw_rec_list *ptr = alloc_record (lc_when);
  return ptr;
}

static unw_rec_list *
output_lc_gr (gr)
     unsigned int gr;
{
  unw_rec_list *ptr = alloc_record (lc_gr);
  ptr->r.record.p.r.gr = gr;
  return ptr;
}

static unw_rec_list *
output_lc_psprel (offset)
     unsigned int offset;
{
  unw_rec_list *ptr = alloc_record (lc_psprel);
  ptr->r.record.p.off.psp = ENCODED_PSP_OFFSET (offset);
  return ptr;
}

static unw_rec_list *
output_lc_sprel (offset)
     unsigned int offset;
{
  unw_rec_list *ptr = alloc_record (lc_sprel);
  ptr->r.record.p.off.sp = offset / 4;
  return ptr;
}

static unw_rec_list *
output_fpsr_when ()
{
  unw_rec_list *ptr = alloc_record (fpsr_when);
  return ptr;
}

static unw_rec_list *
output_fpsr_gr (gr)
     unsigned int gr;
{
  unw_rec_list *ptr = alloc_record (fpsr_gr);
  ptr->r.record.p.r.gr = gr;
  return ptr;
}

static unw_rec_list *
output_fpsr_psprel (offset)
     unsigned int offset;
{
  unw_rec_list *ptr = alloc_record (fpsr_psprel);
  ptr->r.record.p.off.psp = ENCODED_PSP_OFFSET (offset);
  return ptr;
}

static unw_rec_list *
output_fpsr_sprel (offset)
     unsigned int offset;
{
  unw_rec_list *ptr = alloc_record (fpsr_sprel);
  ptr->r.record.p.off.sp = offset / 4;
  return ptr;
}

static unw_rec_list *
output_priunat_when_gr ()
{
  unw_rec_list *ptr = alloc_record (priunat_when_gr);
  return ptr;
}

static unw_rec_list *
output_priunat_when_mem ()
{
  unw_rec_list *ptr = alloc_record (priunat_when_mem);
  return ptr;
}

static unw_rec_list *
output_priunat_gr (gr)
     unsigned int gr;
{
  unw_rec_list *ptr = alloc_record (priunat_gr);
  ptr->r.record.p.r.gr = gr;
  return ptr;
}

static unw_rec_list *
output_priunat_psprel (offset)
     unsigned int offset;
{
  unw_rec_list *ptr = alloc_record (priunat_psprel);
  ptr->r.record.p.off.psp = ENCODED_PSP_OFFSET (offset);
  return ptr;
}

static unw_rec_list *
output_priunat_sprel (offset)
     unsigned int offset;
{
  unw_rec_list *ptr = alloc_record (priunat_sprel);
  ptr->r.record.p.off.sp = offset / 4;
  return ptr;
}

static unw_rec_list *
output_bsp_when ()
{
  unw_rec_list *ptr = alloc_record (bsp_when);
  return ptr;
}

static unw_rec_list *
output_bsp_gr (gr)
     unsigned int gr;
{
  unw_rec_list *ptr = alloc_record (bsp_gr);
  ptr->r.record.p.r.gr = gr;
  return ptr;
}

static unw_rec_list *
output_bsp_psprel (offset)
     unsigned int offset;
{
  unw_rec_list *ptr = alloc_record (bsp_psprel);
  ptr->r.record.p.off.psp = ENCODED_PSP_OFFSET (offset);
  return ptr;
}

static unw_rec_list *
output_bsp_sprel (offset)
     unsigned int offset;
{
  unw_rec_list *ptr = alloc_record (bsp_sprel);
  ptr->r.record.p.off.sp = offset / 4;
  return ptr;
}

static unw_rec_list *
output_bspstore_when ()
{
  unw_rec_list *ptr = alloc_record (bspstore_when);
  return ptr;
}

static unw_rec_list *
output_bspstore_gr (gr)
     unsigned int gr;
{
  unw_rec_list *ptr = alloc_record (bspstore_gr);
  ptr->r.record.p.r.gr = gr;
  return ptr;
}

static unw_rec_list *
output_bspstore_psprel (offset)
     unsigned int offset;
{
  unw_rec_list *ptr = alloc_record (bspstore_psprel);
  ptr->r.record.p.off.psp = ENCODED_PSP_OFFSET (offset);
  return ptr;
}

static unw_rec_list *
output_bspstore_sprel (offset)
     unsigned int offset;
{
  unw_rec_list *ptr = alloc_record (bspstore_sprel);
  ptr->r.record.p.off.sp = offset / 4;
  return ptr;
}

static unw_rec_list *
output_rnat_when ()
{
  unw_rec_list *ptr = alloc_record (rnat_when);
  return ptr;
}

static unw_rec_list *
output_rnat_gr (gr)
     unsigned int gr;
{
  unw_rec_list *ptr = alloc_record (rnat_gr);
  ptr->r.record.p.r.gr = gr;
  return ptr;
}

static unw_rec_list *
output_rnat_psprel (offset)
     unsigned int offset;
{
  unw_rec_list *ptr = alloc_record (rnat_psprel);
  ptr->r.record.p.off.psp = ENCODED_PSP_OFFSET (offset);
  return ptr;
}

static unw_rec_list *
output_rnat_sprel (offset)
     unsigned int offset;
{
  unw_rec_list *ptr = alloc_record (rnat_sprel);
  ptr->r.record.p.off.sp = offset / 4;
  return ptr;
}

static unw_rec_list *
output_unwabi (abi, context)
     unsigned long abi;
     unsigned long context;
{
  unw_rec_list *ptr = alloc_record (unwabi);
  ptr->r.record.p.abi = abi;
  ptr->r.record.p.context = context;
  return ptr;
}

static unw_rec_list *
output_epilogue (unsigned long ecount)
{
  unw_rec_list *ptr = alloc_record (epilogue);
  ptr->r.record.b.ecount = ecount;
  return ptr;
}

static unw_rec_list *
output_label_state (unsigned long label)
{
  unw_rec_list *ptr = alloc_record (label_state);
  ptr->r.record.b.label = label;
  return ptr;
}

static unw_rec_list *
output_copy_state (unsigned long label)
{
  unw_rec_list *ptr = alloc_record (copy_state);
  ptr->r.record.b.label = label;
  return ptr;
}

static unw_rec_list *
output_spill_psprel (ab, reg, offset, predicate)
     unsigned int ab;
     unsigned int reg;
     unsigned int offset;
     unsigned int predicate;
{
  unw_rec_list *ptr = alloc_record (predicate ? spill_psprel_p : spill_psprel);
  ptr->r.record.x.ab = ab;
  ptr->r.record.x.reg = reg;
  ptr->r.record.x.where.pspoff = ENCODED_PSP_OFFSET (offset);
  ptr->r.record.x.qp = predicate;
  return ptr;
}

static unw_rec_list *
output_spill_sprel (ab, reg, offset, predicate)
     unsigned int ab;
     unsigned int reg;
     unsigned int offset;
     unsigned int predicate;
{
  unw_rec_list *ptr = alloc_record (predicate ? spill_sprel_p : spill_sprel);
  ptr->r.record.x.ab = ab;
  ptr->r.record.x.reg = reg;
  ptr->r.record.x.where.spoff = offset / 4;
  ptr->r.record.x.qp = predicate;
  return ptr;
}

static unw_rec_list *
output_spill_reg (ab, reg, targ_reg, xy, predicate)
     unsigned int ab;
     unsigned int reg;
     unsigned int targ_reg;
     unsigned int xy;
     unsigned int predicate;
{
  unw_rec_list *ptr = alloc_record (predicate ? spill_reg_p : spill_reg);
  ptr->r.record.x.ab = ab;
  ptr->r.record.x.reg = reg;
  ptr->r.record.x.where.reg = targ_reg;
  ptr->r.record.x.xy = xy;
  ptr->r.record.x.qp = predicate;
  return ptr;
}

/* Given a unw_rec_list process the correct format with the
   specified function.  */

static void
process_one_record (ptr, f)
     unw_rec_list *ptr;
     vbyte_func f;
{
  unsigned int fr_mask, gr_mask;

  switch (ptr->r.type)
    {
      /* This is a dummy record that takes up no space in the output.  */
    case endp:
      break;

    case gr_mem:
    case fr_mem:
    case br_mem:
    case frgr_mem:
      /* These are taken care of by prologue/prologue_gr.  */
      break;

    case prologue_gr:
    case prologue:
      if (ptr->r.type == prologue_gr)
	output_R2_format (f, ptr->r.record.r.grmask,
			  ptr->r.record.r.grsave, ptr->r.record.r.rlen);
      else
	output_R1_format (f, ptr->r.type, ptr->r.record.r.rlen);

      /* Output descriptor(s) for union of register spills (if any).  */
      gr_mask = ptr->r.record.r.mask.gr_mem;
      fr_mask = ptr->r.record.r.mask.fr_mem;
      if (fr_mask)
	{
	  if ((fr_mask & ~0xfUL) == 0)
	    output_P6_format (f, fr_mem, fr_mask);
	  else
	    {
	      output_P5_format (f, gr_mask, fr_mask);
	      gr_mask = 0;
	    }
	}
      if (gr_mask)
	output_P6_format (f, gr_mem, gr_mask);
      if (ptr->r.record.r.mask.br_mem)
	output_P1_format (f, ptr->r.record.r.mask.br_mem);

      /* output imask descriptor if necessary:  */
      if (ptr->r.record.r.mask.i)
	output_P4_format (f, ptr->r.record.r.mask.i,
			  ptr->r.record.r.imask_size);
      break;

    case body:
      output_R1_format (f, ptr->r.type, ptr->r.record.r.rlen);
      break;
    case mem_stack_f:
    case mem_stack_v:
      output_P7_format (f, ptr->r.type, ptr->r.record.p.t,
			ptr->r.record.p.size);
      break;
    case psp_gr:
    case rp_gr:
    case pfs_gr:
    case preds_gr:
    case unat_gr:
    case lc_gr:
    case fpsr_gr:
    case priunat_gr:
    case bsp_gr:
    case bspstore_gr:
    case rnat_gr:
      output_P3_format (f, ptr->r.type, ptr->r.record.p.r.gr);
      break;
    case rp_br:
      output_P3_format (f, rp_br, ptr->r.record.p.r.br);
      break;
    case psp_sprel:
      output_P7_format (f, psp_sprel, ptr->r.record.p.off.sp, 0);
      break;
    case rp_when:
    case pfs_when:
    case preds_when:
    case unat_when:
    case lc_when:
    case fpsr_when:
      output_P7_format (f, ptr->r.type, ptr->r.record.p.t, 0);
      break;
    case rp_psprel:
    case pfs_psprel:
    case preds_psprel:
    case unat_psprel:
    case lc_psprel:
    case fpsr_psprel:
    case spill_base:
      output_P7_format (f, ptr->r.type, ptr->r.record.p.off.psp, 0);
      break;
    case rp_sprel:
    case pfs_sprel:
    case preds_sprel:
    case unat_sprel:
    case lc_sprel:
    case fpsr_sprel:
    case priunat_sprel:
    case bsp_sprel:
    case bspstore_sprel:
    case rnat_sprel:
      output_P8_format (f, ptr->r.type, ptr->r.record.p.off.sp);
      break;
    case gr_gr:
      if (ptr->r.record.p.r.gr < REG_NUM)
	{
	  const unw_rec_list *cur = ptr;

	  gr_mask = cur->r.record.p.grmask;
	  while ((cur = cur->r.record.p.next) != NULL)
	    gr_mask |= cur->r.record.p.grmask;
	  output_P9_format (f, gr_mask, ptr->r.record.p.r.gr);
	}
      break;
    case br_gr:
      if (ptr->r.record.p.r.gr < REG_NUM)
	{
	  const unw_rec_list *cur = ptr;

	  gr_mask = cur->r.record.p.brmask;
	  while ((cur = cur->r.record.p.next) != NULL)
	    gr_mask |= cur->r.record.p.brmask;
	  output_P2_format (f, gr_mask, ptr->r.record.p.r.gr);
	}
      break;
    case spill_mask:
      as_bad ("spill_mask record unimplemented.");
      break;
    case priunat_when_gr:
    case priunat_when_mem:
    case bsp_when:
    case bspstore_when:
    case rnat_when:
      output_P8_format (f, ptr->r.type, ptr->r.record.p.t);
      break;
    case priunat_psprel:
    case bsp_psprel:
    case bspstore_psprel:
    case rnat_psprel:
      output_P8_format (f, ptr->r.type, ptr->r.record.p.off.psp);
      break;
    case unwabi:
      output_P10_format (f, ptr->r.record.p.abi, ptr->r.record.p.context);
      break;
    case epilogue:
      output_B3_format (f, ptr->r.record.b.ecount, ptr->r.record.b.t);
      break;
    case label_state:
    case copy_state:
      output_B4_format (f, ptr->r.type, ptr->r.record.b.label);
      break;
    case spill_psprel:
      output_X1_format (f, ptr->r.type, ptr->r.record.x.ab,
			ptr->r.record.x.reg, ptr->r.record.x.t,
			ptr->r.record.x.where.pspoff);
      break;
    case spill_sprel:
      output_X1_format (f, ptr->r.type, ptr->r.record.x.ab,
			ptr->r.record.x.reg, ptr->r.record.x.t,
			ptr->r.record.x.where.spoff);
      break;
    case spill_reg:
      output_X2_format (f, ptr->r.record.x.ab, ptr->r.record.x.reg,
			ptr->r.record.x.xy >> 1, ptr->r.record.x.xy,
			ptr->r.record.x.where.reg, ptr->r.record.x.t);
      break;
    case spill_psprel_p:
      output_X3_format (f, ptr->r.type, ptr->r.record.x.qp,
			ptr->r.record.x.ab, ptr->r.record.x.reg,
			ptr->r.record.x.t, ptr->r.record.x.where.pspoff);
      break;
    case spill_sprel_p:
      output_X3_format (f, ptr->r.type, ptr->r.record.x.qp,
			ptr->r.record.x.ab, ptr->r.record.x.reg,
			ptr->r.record.x.t, ptr->r.record.x.where.spoff);
      break;
    case spill_reg_p:
      output_X4_format (f, ptr->r.record.x.qp, ptr->r.record.x.ab,
			ptr->r.record.x.reg, ptr->r.record.x.xy >> 1,
			ptr->r.record.x.xy, ptr->r.record.x.where.reg,
			ptr->r.record.x.t);
      break;
    default:
      as_bad ("record_type_not_valid");
      break;
    }
}

/* Given a unw_rec_list list, process all the records with
   the specified function.  */
static void
process_unw_records (list, f)
     unw_rec_list *list;
     vbyte_func f;
{
  unw_rec_list *ptr;
  for (ptr = list; ptr; ptr = ptr->next)
    process_one_record (ptr, f);
}

/* Determine the size of a record list in bytes.  */
static int
calc_record_size (list)
     unw_rec_list *list;
{
  vbyte_count = 0;
  process_unw_records (list, count_output);
  return vbyte_count;
}

/* Return the number of bits set in the input value.
   Perhaps this has a better place...  */
#if __GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 4)
# define popcount __builtin_popcount
#else
static int
popcount (unsigned x)
{
  static const unsigned char popcnt[16] =
    {
      0, 1, 1, 2,
      1, 2, 2, 3,
      1, 2, 2, 3,
      2, 3, 3, 4
    };

  if (x < NELEMS (popcnt))
    return popcnt[x];
  return popcnt[x % NELEMS (popcnt)] + popcount (x / NELEMS (popcnt));
}
#endif

/* Update IMASK bitmask to reflect the fact that one or more registers
   of type TYPE are saved starting at instruction with index T.  If N
   bits are set in REGMASK, it is assumed that instructions T through
   T+N-1 save these registers.

   TYPE values:
	0: no save
	1: instruction saves next fp reg
	2: instruction saves next general reg
	3: instruction saves next branch reg */
static void
set_imask (region, regmask, t, type)
     unw_rec_list *region;
     unsigned long regmask;
     unsigned long t;
     unsigned int type;
{
  unsigned char *imask;
  unsigned long imask_size;
  unsigned int i;
  int pos;

  imask = region->r.record.r.mask.i;
  imask_size = region->r.record.r.imask_size;
  if (!imask)
    {
      imask_size = (region->r.record.r.rlen * 2 + 7) / 8 + 1;
      imask = xmalloc (imask_size);
      memset (imask, 0, imask_size);

      region->r.record.r.imask_size = imask_size;
      region->r.record.r.mask.i = imask;
    }

  i = (t / 4) + 1;
  pos = 2 * (3 - t % 4);
  while (regmask)
    {
      if (i >= imask_size)
	{
	  as_bad ("Ignoring attempt to spill beyond end of region");
	  return;
	}

      imask[i] |= (type & 0x3) << pos;

      regmask &= (regmask - 1);
      pos -= 2;
      if (pos < 0)
	{
	  pos = 0;
	  ++i;
	}
    }
}

/* Return the number of instruction slots from FIRST_ADDR to SLOT_ADDR.
   SLOT_FRAG is the frag containing SLOT_ADDR, and FIRST_FRAG is the frag
   containing FIRST_ADDR.  If BEFORE_RELAX, then we use worst-case estimates
   for frag sizes.  */

unsigned long
slot_index (slot_addr, slot_frag, first_addr, first_frag, before_relax)
     unsigned long slot_addr;
     fragS *slot_frag;
     unsigned long first_addr;
     fragS *first_frag;
     int before_relax;
{
  unsigned long index = 0;

  /* First time we are called, the initial address and frag are invalid.  */
  if (first_addr == 0)
    return 0;

  /* If the two addresses are in different frags, then we need to add in
     the remaining size of this frag, and then the entire size of intermediate
     frags.  */
  while (slot_frag != first_frag)
    {
      unsigned long start_addr = (unsigned long) &first_frag->fr_literal;

      if (! before_relax)
	{
	  /* We can get the final addresses only during and after
	     relaxation.  */
	  if (first_frag->fr_next && first_frag->fr_next->fr_address)
	    index += 3 * ((first_frag->fr_next->fr_address
			   - first_frag->fr_address
			     - first_frag->fr_fix) >> 4);
	}
      else
	/* We don't know what the final addresses will be. We try our
	   best to estimate.  */
	switch (first_frag->fr_type)
	  {
	  default:
	    break;

	  case rs_space:
	    as_fatal ("only constant space allocation is supported");
	    break;

	  case rs_align:
	  case rs_align_code:
	  case rs_align_test:
	    /* Take alignment into account.  Assume the worst case
	       before relaxation.  */
	    index += 3 * ((1 << first_frag->fr_offset) >> 4);
	    break;

	  case rs_org:
	    if (first_frag->fr_symbol)
	      {
		as_fatal ("only constant offsets are supported");
		break;
	      }
	  case rs_fill:
	    index += 3 * (first_frag->fr_offset >> 4);
	    break;
	  }

      /* Add in the full size of the frag converted to instruction slots.  */
      index += 3 * (first_frag->fr_fix >> 4);
      /* Subtract away the initial part before first_addr.  */
      index -= (3 * ((first_addr >> 4) - (start_addr >> 4))
		+ ((first_addr & 0x3) - (start_addr & 0x3)));

      /* Move to the beginning of the next frag.  */
      first_frag = first_frag->fr_next;
      first_addr = (unsigned long) &first_frag->fr_literal;

      /* This can happen if there is section switching in the middle of a
	 function, causing the frag chain for the function to be broken.
	 It is too difficult to recover safely from this problem, so we just
	 exit with an error.  */
      if (first_frag == NULL)
	as_fatal ("Section switching in code is not supported.");
    }

  /* Add in the used part of the last frag.  */
  index += (3 * ((slot_addr >> 4) - (first_addr >> 4))
	    + ((slot_addr & 0x3) - (first_addr & 0x3)));
  return index;
}

/* Optimize unwind record directives.  */

static unw_rec_list *
optimize_unw_records (list)
     unw_rec_list *list;
{
  if (!list)
    return NULL;

  /* If the only unwind record is ".prologue" or ".prologue" followed
     by ".body", then we can optimize the unwind directives away.  */
  if (list->r.type == prologue
      && (list->next->r.type == endp
	  || (list->next->r.type == body && list->next->next->r.type == endp)))
    return NULL;

  return list;
}

/* Given a complete record list, process any records which have
   unresolved fields, (ie length counts for a prologue).  After
   this has been run, all necessary information should be available
   within each record to generate an image.  */

static void
fixup_unw_records (list, before_relax)
     unw_rec_list *list;
     int before_relax;
{
  unw_rec_list *ptr, *region = 0;
  unsigned long first_addr = 0, rlen = 0, t;
  fragS *first_frag = 0;

  for (ptr = list; ptr; ptr = ptr->next)
    {
      if (ptr->slot_number == SLOT_NUM_NOT_SET)
	as_bad (" Insn slot not set in unwind record.");
      t = slot_index (ptr->slot_number, ptr->slot_frag,
		      first_addr, first_frag, before_relax);
      switch (ptr->r.type)
	{
	case prologue:
	case prologue_gr:
	case body:
	  {
	    unw_rec_list *last;
	    int size;
	    unsigned long last_addr = 0;
	    fragS *last_frag = NULL;

	    first_addr = ptr->slot_number;
	    first_frag = ptr->slot_frag;
	    /* Find either the next body/prologue start, or the end of
	       the function, and determine the size of the region.  */
	    for (last = ptr->next; last != NULL; last = last->next)
	      if (last->r.type == prologue || last->r.type == prologue_gr
		  || last->r.type == body || last->r.type == endp)
		{
		  last_addr = last->slot_number;
		  last_frag = last->slot_frag;
		  break;
		}
	    size = slot_index (last_addr, last_frag, first_addr, first_frag,
			       before_relax);
	    rlen = ptr->r.record.r.rlen = size;
	    if (ptr->r.type == body)
	      /* End of region.  */
	      region = 0;
	    else
	      region = ptr;
	    break;
	  }
	case epilogue:
	  if (t < rlen)
	    ptr->r.record.b.t = rlen - 1 - t;
	  else
	    /* This happens when a memory-stack-less procedure uses a
	       ".restore sp" directive at the end of a region to pop
	       the frame state.  */
	    ptr->r.record.b.t = 0;
	  break;

	case mem_stack_f:
	case mem_stack_v:
	case rp_when:
	case pfs_when:
	case preds_when:
	case unat_when:
	case lc_when:
	case fpsr_when:
	case priunat_when_gr:
	case priunat_when_mem:
	case bsp_when:
	case bspstore_when:
	case rnat_when:
	  ptr->r.record.p.t = t;
	  break;

	case spill_reg:
	case spill_sprel:
	case spill_psprel:
	case spill_reg_p:
	case spill_sprel_p:
	case spill_psprel_p:
	  ptr->r.record.x.t = t;
	  break;

	case frgr_mem:
	  if (!region)
	    {
	      as_bad ("frgr_mem record before region record!");
	      return;
	    }
	  region->r.record.r.mask.fr_mem |= ptr->r.record.p.frmask;
	  region->r.record.r.mask.gr_mem |= ptr->r.record.p.grmask;
	  set_imask (region, ptr->r.record.p.frmask, t, 1);
	  set_imask (region, ptr->r.record.p.grmask, t, 2);
	  break;
	case fr_mem:
	  if (!region)
	    {
	      as_bad ("fr_mem record before region record!");
	      return;
	    }
	  region->r.record.r.mask.fr_mem |= ptr->r.record.p.frmask;
	  set_imask (region, ptr->r.record.p.frmask, t, 1);
	  break;
	case gr_mem:
	  if (!region)
	    {
	      as_bad ("gr_mem record before region record!");
	      return;
	    }
	  region->r.record.r.mask.gr_mem |= ptr->r.record.p.grmask;
	  set_imask (region, ptr->r.record.p.grmask, t, 2);
	  break;
	case br_mem:
	  if (!region)
	    {
	      as_bad ("br_mem record before region record!");
	      return;
	    }
	  region->r.record.r.mask.br_mem |= ptr->r.record.p.brmask;
	  set_imask (region, ptr->r.record.p.brmask, t, 3);
	  break;

	case gr_gr:
	  if (!region)
	    {
	      as_bad ("gr_gr record before region record!");
	      return;
	    }
	  set_imask (region, ptr->r.record.p.grmask, t, 2);
	  break;
	case br_gr:
	  if (!region)
	    {
	      as_bad ("br_gr record before region record!");
	      return;
	    }
	  set_imask (region, ptr->r.record.p.brmask, t, 3);
	  break;

	default:
	  break;
	}
    }
}

/* Estimate the size of a frag before relaxing.  We only have one type of frag
   to handle here, which is the unwind info frag.  */

int
ia64_estimate_size_before_relax (fragS *frag,
				 asection *segtype ATTRIBUTE_UNUSED)
{
  unw_rec_list *list;
  int len, size, pad;

  /* ??? This code is identical to the first part of ia64_convert_frag.  */
  list = (unw_rec_list *) frag->fr_opcode;
  fixup_unw_records (list, 0);

  len = calc_record_size (list);
  /* pad to pointer-size boundary.  */
  pad = len % md.pointer_size;
  if (pad != 0)
    len += md.pointer_size - pad;
  /* Add 8 for the header.  */
  size = len + 8;
  /* Add a pointer for the personality offset.  */
  if (frag->fr_offset)
    size += md.pointer_size;

  /* fr_var carries the max_chars that we created the fragment with.
     We must, of course, have allocated enough memory earlier.  */
  assert (frag->fr_var >= size);

  return frag->fr_fix + size;
}

/* This function converts a rs_machine_dependent variant frag into a
  normal fill frag with the unwind image from the the record list.  */
void
ia64_convert_frag (fragS *frag)
{
  unw_rec_list *list;
  int len, size, pad;
  valueT flag_value;

  /* ??? This code is identical to ia64_estimate_size_before_relax.  */
  list = (unw_rec_list *) frag->fr_opcode;
  fixup_unw_records (list, 0);

  len = calc_record_size (list);
  /* pad to pointer-size boundary.  */
  pad = len % md.pointer_size;
  if (pad != 0)
    len += md.pointer_size - pad;
  /* Add 8 for the header.  */
  size = len + 8;
  /* Add a pointer for the personality offset.  */
  if (frag->fr_offset)
    size += md.pointer_size;

  /* fr_var carries the max_chars that we created the fragment with.
     We must, of course, have allocated enough memory earlier.  */
  assert (frag->fr_var >= size);

  /* Initialize the header area. fr_offset is initialized with
     unwind.personality_routine.  */
  if (frag->fr_offset)
    {
      if (md.flags & EF_IA_64_ABI64)
	flag_value = (bfd_vma) 3 << 32;
      else
	/* 32-bit unwind info block.  */
	flag_value = (bfd_vma) 0x1003 << 32;
    }
  else
    flag_value = 0;

 md_number_to_chars (frag->fr_literal,
		     (((bfd_vma) 1 << 48) /* Version.  */
		      | flag_value        /* U & E handler flags.  */
		      | (len / md.pointer_size)), /* Length.  */
		     8);

  /* Skip the header.  */
  vbyte_mem_ptr = frag->fr_literal + 8;
  process_unw_records (list, output_vbyte_mem);

  /* Fill the padding bytes with zeros.  */
  if (pad != 0)
    md_number_to_chars (frag->fr_literal + len + 8 - md.pointer_size + pad, 0,
			md.pointer_size - pad);

  frag->fr_fix += size;
  frag->fr_type = rs_fill;
  frag->fr_var = 0;
  frag->fr_offset = 0;
}

static int
parse_predicate_and_operand (e, qp, po)
     expressionS * e;
     unsigned * qp;
     const char * po;
{
  int sep = parse_operand (e, ',');

  *qp = e->X_add_number - REG_P;
  if (e->X_op != O_register || *qp > 63)
    {
      as_bad ("First operand to .%s must be a predicate", po);
      *qp = 0;
    }
  else if (*qp == 0)
    as_warn ("Pointless use of p0 as first operand to .%s", po);
  if (sep == ',')
    sep = parse_operand (e, ',');
  else
    e->X_op = O_absent;
  return sep;
}

static void
convert_expr_to_ab_reg (e, ab, regp, po, n)
     const expressionS *e;
     unsigned int *ab;
     unsigned int *regp;
     const char * po;
     int n;
{
  unsigned int reg = e->X_add_number;

  *ab = *regp = 0; /* Anything valid is good here.  */

  if (e->X_op != O_register)
    reg = REG_GR; /* Anything invalid is good here.  */

  if (reg >= (REG_GR + 4) && reg <= (REG_GR + 7))
    {
      *ab = 0;
      *regp = reg - REG_GR;
    }
  else if ((reg >= (REG_FR + 2) && reg <= (REG_FR + 5))
	   || (reg >= (REG_FR + 16) && reg <= (REG_FR + 31)))
    {
      *ab = 1;
      *regp = reg - REG_FR;
    }
  else if (reg >= (REG_BR + 1) && reg <= (REG_BR + 5))
    {
      *ab = 2;
      *regp = reg - REG_BR;
    }
  else
    {
      *ab = 3;
      switch (reg)
	{
	case REG_PR:		*regp =  0; break;
	case REG_PSP:		*regp =  1; break;
	case REG_PRIUNAT:	*regp =  2; break;
	case REG_BR + 0:	*regp =  3; break;
	case REG_AR + AR_BSP:	*regp =  4; break;
	case REG_AR + AR_BSPSTORE: *regp = 5; break;
	case REG_AR + AR_RNAT:	*regp =  6; break;
	case REG_AR + AR_UNAT:	*regp =  7; break;
	case REG_AR + AR_FPSR:	*regp =  8; break;
	case REG_AR + AR_PFS:	*regp =  9; break;
	case REG_AR + AR_LC:	*regp = 10; break;

	default:
	  as_bad ("Operand %d to .%s must be a preserved register", n, po);
	  break;
	}
    }
}

static void
convert_expr_to_xy_reg (e, xy, regp, po, n)
     const expressionS *e;
     unsigned int *xy;
     unsigned int *regp;
     const char * po;
     int n;
{
  unsigned int reg = e->X_add_number;

  *xy = *regp = 0; /* Anything valid is good here.  */

  if (e->X_op != O_register)
    reg = REG_GR; /* Anything invalid is good here.  */

  if (reg >= (REG_GR + 1) && reg <= (REG_GR + 127))
    {
      *xy = 0;
      *regp = reg - REG_GR;
    }
  else if (reg >= (REG_FR + 2) && reg <= (REG_FR + 127))
    {
      *xy = 1;
      *regp = reg - REG_FR;
    }
  else if (reg >= REG_BR && reg <= (REG_BR + 7))
    {
      *xy = 2;
      *regp = reg - REG_BR;
    }
  else
    as_bad ("Operand %d to .%s must be a writable register", n, po);
}

static void
dot_align (int arg)
{
  /* The current frag is an alignment frag.  */
  align_frag = frag_now;
  s_align_bytes (arg);
}

static void
dot_radix (dummy)
     int dummy ATTRIBUTE_UNUSED;
{
  char *radix;
  int ch;

  SKIP_WHITESPACE ();

  if (is_it_end_of_statement ())
    return;
  radix = input_line_pointer;
  ch = get_symbol_end ();
  ia64_canonicalize_symbol_name (radix);
  if (strcasecmp (radix, "C"))
    as_bad ("Radix `%s' unsupported or invalid", radix);
  *input_line_pointer = ch;
  demand_empty_rest_of_line ();
}

/* Helper function for .loc directives.  If the assembler is not generating
   line number info, then we need to remember which instructions have a .loc
   directive, and only call dwarf2_gen_line_info for those instructions.  */

static void
dot_loc (int x)
{
  CURR_SLOT.loc_directive_seen = 1;
  dwarf2_directive_loc (x);
}

/* .sbss, .bss etc. are macros that expand into ".section SECNAME".  */
static void
dot_special_section (which)
     int which;
{
  set_section ((char *) special_section_name[which]);
}

/* Return -1 for warning and 0 for error.  */

static int
unwind_diagnostic (const char * region, const char *directive)
{
  if (md.unwind_check == unwind_check_warning)
    {
      as_warn (".%s outside of %s", directive, region);
      return -1;
    }
  else
    {
      as_bad (".%s outside of %s", directive, region);
      ignore_rest_of_line ();
      return 0;
    }
}

/* Return 1 if a directive is in a procedure, -1 if a directive isn't in
   a procedure but the unwind directive check is set to warning, 0 if
   a directive isn't in a procedure and the unwind directive check is set
   to error.  */

static int
in_procedure (const char *directive)
{
  if (unwind.proc_pending.sym
      && (!unwind.saved_text_seg || strcmp (directive, "endp") == 0))
    return 1;
  return unwind_diagnostic ("procedure", directive);
}

/* Return 1 if a directive is in a prologue, -1 if a directive isn't in
   a prologue but the unwind directive check is set to warning, 0 if
   a directive isn't in a prologue and the unwind directive check is set
   to error.  */

static int
in_prologue (const char *directive)
{
  int in = in_procedure (directive);

  if (in > 0 && !unwind.prologue)
    in = unwind_diagnostic ("prologue", directive);
  check_pending_save ();
  return in;
}

/* Return 1 if a directive is in a body, -1 if a directive isn't in
   a body but the unwind directive check is set to warning, 0 if
   a directive isn't in a body and the unwind directive check is set
   to error.  */

static int
in_body (const char *directive)
{
  int in = in_procedure (directive);

  if (in > 0 && !unwind.body)
    in = unwind_diagnostic ("body region", directive);
  return in;
}

static void
add_unwind_entry (ptr, sep)
     unw_rec_list *ptr;
     int sep;
{
  if (ptr)
    {
      if (unwind.tail)
	unwind.tail->next = ptr;
      else
	unwind.list = ptr;
      unwind.tail = ptr;

      /* The current entry can in fact be a chain of unwind entries.  */
      if (unwind.current_entry == NULL)
	unwind.current_entry = ptr;
    }

  /* The current entry can in fact be a chain of unwind entries.  */
  if (unwind.current_entry == NULL)
    unwind.current_entry = ptr;

  if (sep == ',')
    {
      /* Parse a tag permitted for the current directive.  */
      int ch;

      SKIP_WHITESPACE ();
      ch = get_symbol_end ();
      /* FIXME: For now, just issue a warning that this isn't implemented.  */
      {
	static int warned;

	if (!warned)
	  {
	    warned = 1;
	    as_warn ("Tags on unwind pseudo-ops aren't supported, yet");
	  }
      }
      *input_line_pointer = ch;
    }
  if (sep != NOT_A_CHAR)
    demand_empty_rest_of_line ();
}

static void
dot_fframe (dummy)
     int dummy ATTRIBUTE_UNUSED;
{
  expressionS e;
  int sep;

  if (!in_prologue ("fframe"))
    return;

  sep = parse_operand (&e, ',');

  if (e.X_op != O_constant)
    {
      as_bad ("First operand to .fframe must be a constant");
      e.X_add_number = 0;
    }
  add_unwind_entry (output_mem_stack_f (e.X_add_number), sep);
}

static void
dot_vframe (dummy)
     int dummy ATTRIBUTE_UNUSED;
{
  expressionS e;
  unsigned reg;
  int sep;

  if (!in_prologue ("vframe"))
    return;

  sep = parse_operand (&e, ',');
  reg = e.X_add_number - REG_GR;
  if (e.X_op != O_register || reg > 127)
    {
      as_bad ("First operand to .vframe must be a general register");
      reg = 0;
    }
  add_unwind_entry (output_mem_stack_v (), sep);
  if (! (unwind.prologue_mask & 2))
    add_unwind_entry (output_psp_gr (reg), NOT_A_CHAR);
  else if (reg != unwind.prologue_gr
		  + (unsigned) popcount (unwind.prologue_mask & (-2 << 1)))
    as_warn ("Operand of .vframe contradicts .prologue");
}

static void
dot_vframesp (psp)
     int psp;
{
  expressionS e;
  int sep;

  if (psp)
    as_warn (".vframepsp is meaningless, assuming .vframesp was meant");

  if (!in_prologue ("vframesp"))
    return;

  sep = parse_operand (&e, ',');
  if (e.X_op != O_constant)
    {
      as_bad ("Operand to .vframesp must be a constant (sp-relative offset)");
      e.X_add_number = 0;
    }
  add_unwind_entry (output_mem_stack_v (), sep);
  add_unwind_entry (output_psp_sprel (e.X_add_number), NOT_A_CHAR);
}

static void
dot_save (dummy)
     int dummy ATTRIBUTE_UNUSED;
{
  expressionS e1, e2;
  unsigned reg1, reg2;
  int sep;

  if (!in_prologue ("save"))
    return;

  sep = parse_operand (&e1, ',');
  if (sep == ',')
    sep = parse_operand (&e2, ',');
  else
    e2.X_op = O_absent;

  reg1 = e1.X_add_number;
  /* Make sure its a valid ar.xxx reg, OR its br0, aka 'rp'.  */
  if (e1.X_op != O_register)
    {
      as_bad ("First operand to .save not a register");
      reg1 = REG_PR; /* Anything valid is good here.  */
    }
  reg2 = e2.X_add_number - REG_GR;
  if (e2.X_op != O_register || reg2 > 127)
    {
      as_bad ("Second operand to .save not a valid register");
      reg2 = 0;
    }
  switch (reg1)
    {
    case REG_AR + AR_BSP:
      add_unwind_entry (output_bsp_when (), sep);
      add_unwind_entry (output_bsp_gr (reg2), NOT_A_CHAR);
      break;
    case REG_AR + AR_BSPSTORE:
      add_unwind_entry (output_bspstore_when (), sep);
      add_unwind_entry (output_bspstore_gr (reg2), NOT_A_CHAR);
      break;
    case REG_AR + AR_RNAT:
      add_unwind_entry (output_rnat_when (), sep);
      add_unwind_entry (output_rnat_gr (reg2), NOT_A_CHAR);
      break;
    case REG_AR + AR_UNAT:
      add_unwind_entry (output_unat_when (), sep);
      add_unwind_entry (output_unat_gr (reg2), NOT_A_CHAR);
      break;
    case REG_AR + AR_FPSR:
      add_unwind_entry (output_fpsr_when (), sep);
      add_unwind_entry (output_fpsr_gr (reg2), NOT_A_CHAR);
      break;
    case REG_AR + AR_PFS:
      add_unwind_entry (output_pfs_when (), sep);
      if (! (unwind.prologue_mask & 4))
	add_unwind_entry (output_pfs_gr (reg2), NOT_A_CHAR);
      else if (reg2 != unwind.prologue_gr
		       + (unsigned) popcount (unwind.prologue_mask & (-4 << 1)))
	as_warn ("Second operand of .save contradicts .prologue");
      break;
    case REG_AR + AR_LC:
      add_unwind_entry (output_lc_when (), sep);
      add_unwind_entry (output_lc_gr (reg2), NOT_A_CHAR);
      break;
    case REG_BR:
      add_unwind_entry (output_rp_when (), sep);
      if (! (unwind.prologue_mask & 8))
	add_unwind_entry (output_rp_gr (reg2), NOT_A_CHAR);
      else if (reg2 != unwind.prologue_gr)
	as_warn ("Second operand of .save contradicts .prologue");
      break;
    case REG_PR:
      add_unwind_entry (output_preds_when (), sep);
      if (! (unwind.prologue_mask & 1))
	add_unwind_entry (output_preds_gr (reg2), NOT_A_CHAR);
      else if (reg2 != unwind.prologue_gr
		       + (unsigned) popcount (unwind.prologue_mask & (-1 << 1)))
	as_warn ("Second operand of .save contradicts .prologue");
      break;
    case REG_PRIUNAT:
      add_unwind_entry (output_priunat_when_gr (), sep);
      add_unwind_entry (output_priunat_gr (reg2), NOT_A_CHAR);
      break;
    default:
      as_bad ("First operand to .save not a valid register");
      add_unwind_entry (NULL, sep);
      break;
    }
}

static void
dot_restore (dummy)
     int dummy ATTRIBUTE_UNUSED;
{
  expressionS e1;
  unsigned long ecount;	/* # of _additional_ regions to pop */
  int sep;

  if (!in_body ("restore"))
    return;

  sep = parse_operand (&e1, ',');
  if (e1.X_op != O_register || e1.X_add_number != REG_GR + 12)
    as_bad ("First operand to .restore must be stack pointer (sp)");

  if (sep == ',')
    {
      expressionS e2;

      sep = parse_operand (&e2, ',');
      if (e2.X_op != O_constant || e2.X_add_number < 0)
	{
	  as_bad ("Second operand to .restore must be a constant >= 0");
	  e2.X_add_number = 0;
	}
      ecount = e2.X_add_number;
    }
  else
    ecount = unwind.prologue_count - 1;

  if (ecount >= unwind.prologue_count)
    {
      as_bad ("Epilogue count of %lu exceeds number of nested prologues (%u)",
	      ecount + 1, unwind.prologue_count);
      ecount = 0;
    }

  add_unwind_entry (output_epilogue (ecount), sep);

  if (ecount < unwind.prologue_count)
    unwind.prologue_count -= ecount + 1;
  else
    unwind.prologue_count = 0;
}

static void
dot_restorereg (pred)
     int pred;
{
  unsigned int qp, ab, reg;
  expressionS e;
  int sep;
  const char * const po = pred ? "restorereg.p" : "restorereg";

  if (!in_procedure (po))
    return;

  if (pred)
    sep = parse_predicate_and_operand (&e, &qp, po);
  else
    {
      sep = parse_operand (&e, ',');
      qp = 0;
    }
  convert_expr_to_ab_reg (&e, &ab, &reg, po, 1 + pred);

  add_unwind_entry (output_spill_reg (ab, reg, 0, 0, qp), sep);
}

static char *special_linkonce_name[] =
  {
    ".gnu.linkonce.ia64unw.", ".gnu.linkonce.ia64unwi."
  };

static void
start_unwind_section (const segT text_seg, int sec_index)
{
  /*
    Use a slightly ugly scheme to derive the unwind section names from
    the text section name:

    text sect.  unwind table sect.
    name:       name:                      comments:
    ----------  -----------------          --------------------------------
    .text       .IA_64.unwind
    .text.foo   .IA_64.unwind.text.foo
    .foo        .IA_64.unwind.foo
    .gnu.linkonce.t.foo
		.gnu.linkonce.ia64unw.foo
    _info       .IA_64.unwind_info         gas issues error message (ditto)
    _infoFOO    .IA_64.unwind_infoFOO      gas issues error message (ditto)

    This mapping is done so that:

	(a) An object file with unwind info only in .text will use
	    unwind section names .IA_64.unwind and .IA_64.unwind_info.
	    This follows the letter of the ABI and also ensures backwards
	    compatibility with older toolchains.

	(b) An object file with unwind info in multiple text sections
	    will use separate unwind sections for each text section.
	    This allows us to properly set the "sh_info" and "sh_link"
	    fields in SHT_IA_64_UNWIND as required by the ABI and also
	    lets GNU ld support programs with multiple segments
	    containing unwind info (as might be the case for certain
	    embedded applications).

	(c) An error is issued if there would be a name clash.
  */

  const char *text_name, *sec_text_name;
  char *sec_name;
  const char *prefix = special_section_name [sec_index];
  const char *suffix;
  size_t prefix_len, suffix_len, sec_name_len;

  sec_text_name = segment_name (text_seg);
  text_name = sec_text_name;
  if (strncmp (text_name, "_info", 5) == 0)
    {
      as_bad ("Illegal section name `%s' (causes unwind section name clash)",
	      text_name);
      ignore_rest_of_line ();
      return;
    }
  if (strcmp (text_name, ".text") == 0)
    text_name = "";

  /* Build the unwind section name by appending the (possibly stripped)
     text section name to the unwind prefix.  */
  suffix = text_name;
  if (strncmp (text_name, ".gnu.linkonce.t.",
	       sizeof (".gnu.linkonce.t.") - 1) == 0)
    {
      prefix = special_linkonce_name [sec_index - SPECIAL_SECTION_UNWIND];
      suffix += sizeof (".gnu.linkonce.t.") - 1;
    }

  prefix_len = strlen (prefix);
  suffix_len = strlen (suffix);
  sec_name_len = prefix_len + suffix_len;
  sec_name = alloca (sec_name_len + 1);
  memcpy (sec_name, prefix, prefix_len);
  memcpy (sec_name + prefix_len, suffix, suffix_len);
  sec_name [sec_name_len] = '\0';

  /* Handle COMDAT group.  */
  if ((text_seg->flags & SEC_LINK_ONCE) != 0
      && (elf_section_flags (text_seg) & SHF_GROUP) != 0)
    {
      char *section;
      size_t len, group_name_len;
      const char *group_name = elf_group_name (text_seg);

      if (group_name == NULL)
	{
	  as_bad ("Group section `%s' has no group signature",
		  sec_text_name);
	  ignore_rest_of_line ();
	  return;
	}
      /* We have to construct a fake section directive. */
      group_name_len = strlen (group_name);
      len = (sec_name_len
	     + 16			/* ,"aG",@progbits,  */
	     + group_name_len		/* ,group_name  */
	     + 7);			/* ,comdat  */

      section = alloca (len + 1);
      memcpy (section, sec_name, sec_name_len);
      memcpy (section + sec_name_len, ",\"aG\",@progbits,", 16);
      memcpy (section + sec_name_len + 16, group_name, group_name_len);
      memcpy (section + len - 7, ",comdat", 7);
      section [len] = '\0';
      set_section (section);
    }
  else
    {
      set_section (sec_name);
      bfd_set_section_flags (stdoutput, now_seg,
			     SEC_LOAD | SEC_ALLOC | SEC_READONLY);
    }

  elf_linked_to_section (now_seg) = text_seg;
}

static void
generate_unwind_image (const segT text_seg)
{
  int size, pad;
  unw_rec_list *list;

  /* Mark the end of the unwind info, so that we can compute the size of the
     last unwind region.  */
  add_unwind_entry (output_endp (), NOT_A_CHAR);

  /* Force out pending instructions, to make sure all unwind records have
     a valid slot_number field.  */
  ia64_flush_insns ();

  /* Generate the unwind record.  */
  list = optimize_unw_records (unwind.list);
  fixup_unw_records (list, 1);
  size = calc_record_size (list);

  if (size > 0 || unwind.force_unwind_entry)
    {
      unwind.force_unwind_entry = 0;
      /* pad to pointer-size boundary.  */
      pad = size % md.pointer_size;
      if (pad != 0)
	size += md.pointer_size - pad;
      /* Add 8 for the header.  */
      size += 8;
      /* Add a pointer for the personality offset.  */
      if (unwind.personality_routine)
	size += md.pointer_size;
    }

  /* If there are unwind records, switch sections, and output the info.  */
  if (size != 0)
    {
      expressionS exp;
      bfd_reloc_code_real_type reloc;

      start_unwind_section (text_seg, SPECIAL_SECTION_UNWIND_INFO);

      /* Make sure the section has 4 byte alignment for ILP32 and
	 8 byte alignment for LP64.  */
      frag_align (md.pointer_size_shift, 0, 0);
      record_alignment (now_seg, md.pointer_size_shift);

      /* Set expression which points to start of unwind descriptor area.  */
      unwind.info = expr_build_dot ();
      
      frag_var (rs_machine_dependent, size, size, 0, 0,
		(offsetT) (long) unwind.personality_routine,
		(char *) list);

      /* Add the personality address to the image.  */
      if (unwind.personality_routine != 0)
	{
	  exp.X_op = O_symbol;
	  exp.X_add_symbol = unwind.personality_routine;
	  exp.X_add_number = 0;

	  if (md.flags & EF_IA_64_BE)
	    {
	      if (md.flags & EF_IA_64_ABI64)
		reloc = BFD_RELOC_IA64_LTOFF_FPTR64MSB;
	      else
		reloc = BFD_RELOC_IA64_LTOFF_FPTR32MSB;
	    }
	  else
	    {
	      if (md.flags & EF_IA_64_ABI64)
		reloc = BFD_RELOC_IA64_LTOFF_FPTR64LSB;
	      else
		reloc = BFD_RELOC_IA64_LTOFF_FPTR32LSB;
	    }

	  fix_new_exp (frag_now, frag_now_fix () - md.pointer_size,
		       md.pointer_size, &exp, 0, reloc);
	  unwind.personality_routine = 0;
	}
    }

  free_saved_prologue_counts ();
  unwind.list = unwind.tail = unwind.current_entry = NULL;
}

static void
dot_handlerdata (dummy)
     int dummy ATTRIBUTE_UNUSED;
{
  if (!in_procedure ("handlerdata"))
    return;
  unwind.force_unwind_entry = 1;

  /* Remember which segment we're in so we can switch back after .endp */
  unwind.saved_text_seg = now_seg;
  unwind.saved_text_subseg = now_subseg;

  /* Generate unwind info into unwind-info section and then leave that
     section as the currently active one so dataXX directives go into
     the language specific data area of the unwind info block.  */
  generate_unwind_image (now_seg);
  demand_empty_rest_of_line ();
}

static void
dot_unwentry (dummy)
     int dummy ATTRIBUTE_UNUSED;
{
  if (!in_procedure ("unwentry"))
    return;
  unwind.force_unwind_entry = 1;
  demand_empty_rest_of_line ();
}

static void
dot_altrp (dummy)
     int dummy ATTRIBUTE_UNUSED;
{
  expressionS e;
  unsigned reg;

  if (!in_prologue ("altrp"))
    return;

  parse_operand (&e, 0);
  reg = e.X_add_number - REG_BR;
  if (e.X_op != O_register || reg > 7)
    {
      as_bad ("First operand to .altrp not a valid branch register");
      reg = 0;
    }
  add_unwind_entry (output_rp_br (reg), 0);
}

static void
dot_savemem (psprel)
     int psprel;
{
  expressionS e1, e2;
  int sep;
  int reg1, val;
  const char * const po = psprel ? "savepsp" : "savesp";

  if (!in_prologue (po))
    return;

  sep = parse_operand (&e1, ',');
  if (sep == ',')
    sep = parse_operand (&e2, ',');
  else
    e2.X_op = O_absent;

  reg1 = e1.X_add_number;
  val = e2.X_add_number;

  /* Make sure its a valid ar.xxx reg, OR its br0, aka 'rp'.  */
  if (e1.X_op != O_register)
    {
      as_bad ("First operand to .%s not a register", po);
      reg1 = REG_PR; /* Anything valid is good here.  */
    }
  if (e2.X_op != O_constant)
    {
      as_bad ("Second operand to .%s not a constant", po);
      val = 0;
    }

  switch (reg1)
    {
    case REG_AR + AR_BSP:
      add_unwind_entry (output_bsp_when (), sep);
      add_unwind_entry ((psprel
			 ? output_bsp_psprel
			 : output_bsp_sprel) (val), NOT_A_CHAR);
      break;
    case REG_AR + AR_BSPSTORE:
      add_unwind_entry (output_bspstore_when (), sep);
      add_unwind_entry ((psprel
			 ? output_bspstore_psprel
			 : output_bspstore_sprel) (val), NOT_A_CHAR);
      break;
    case REG_AR + AR_RNAT:
      add_unwind_entry (output_rnat_when (), sep);
      add_unwind_entry ((psprel
			 ? output_rnat_psprel
			 : output_rnat_sprel) (val), NOT_A_CHAR);
      break;
    case REG_AR + AR_UNAT:
      add_unwind_entry (output_unat_when (), sep);
      add_unwind_entry ((psprel
			 ? output_unat_psprel
			 : output_unat_sprel) (val), NOT_A_CHAR);
      break;
    case REG_AR + AR_FPSR:
      add_unwind_entry (output_fpsr_when (), sep);
      add_unwind_entry ((psprel
			 ? output_fpsr_psprel
			 : output_fpsr_sprel) (val), NOT_A_CHAR);
      break;
    case REG_AR + AR_PFS:
      add_unwind_entry (output_pfs_when (), sep);
      add_unwind_entry ((psprel
			 ? output_pfs_psprel
			 : output_pfs_sprel) (val), NOT_A_CHAR);
      break;
    case REG_AR + AR_LC:
      add_unwind_entry (output_lc_when (), sep);
      add_unwind_entry ((psprel
			 ? output_lc_psprel
			 : output_lc_sprel) (val), NOT_A_CHAR);
      break;
    case REG_BR:
      add_unwind_entry (output_rp_when (), sep);
      add_unwind_entry ((psprel
			 ? output_rp_psprel
			 : output_rp_sprel) (val), NOT_A_CHAR);
      break;
    case REG_PR:
      add_unwind_entry (output_preds_when (), sep);
      add_unwind_entry ((psprel
			 ? output_preds_psprel
			 : output_preds_sprel) (val), NOT_A_CHAR);
      break;
    case REG_PRIUNAT:
      add_unwind_entry (output_priunat_when_mem (), sep);
      add_unwind_entry ((psprel
			 ? output_priunat_psprel
			 : output_priunat_sprel) (val), NOT_A_CHAR);
      break;
    default:
      as_bad ("First operand to .%s not a valid register", po);
      add_unwind_entry (NULL, sep);
      break;
    }
}

static void
dot_saveg (dummy)
     int dummy ATTRIBUTE_UNUSED;
{
  expressionS e;
  unsigned grmask;
  int sep;

  if (!in_prologue ("save.g"))
    return;

  sep = parse_operand (&e, ',');

  grmask = e.X_add_number;
  if (e.X_op != O_constant
      || e.X_add_number <= 0
      || e.X_add_number > 0xf)
    {
      as_bad ("First operand to .save.g must be a positive 4-bit constant");
      grmask = 0;
    }

  if (sep == ',')
    {
      unsigned reg;
      int n = popcount (grmask);

      parse_operand (&e, 0);
      reg = e.X_add_number - REG_GR;
      if (e.X_op != O_register || reg > 127)
	{
	  as_bad ("Second operand to .save.g must be a general register");
	  reg = 0;
	}
      else if (reg > 128U - n)
	{
	  as_bad ("Second operand to .save.g must be the first of %d general registers", n);
	  reg = 0;
	}
      add_unwind_entry (output_gr_gr (grmask, reg), 0);
    }
  else
    add_unwind_entry (output_gr_mem (grmask), 0);
}

static void
dot_savef (dummy)
     int dummy ATTRIBUTE_UNUSED;
{
  expressionS e;

  if (!in_prologue ("save.f"))
    return;

  parse_operand (&e, 0);

  if (e.X_op != O_constant
      || e.X_add_number <= 0
      || e.X_add_number > 0xfffff)
    {
      as_bad ("Operand to .save.f must be a positive 20-bit constant");
      e.X_add_number = 0;
    }
  add_unwind_entry (output_fr_mem (e.X_add_number), 0);
}

static void
dot_saveb (dummy)
     int dummy ATTRIBUTE_UNUSED;
{
  expressionS e;
  unsigned brmask;
  int sep;

  if (!in_prologue ("save.b"))
    return;

  sep = parse_operand (&e, ',');

  brmask = e.X_add_number;
  if (e.X_op != O_constant
      || e.X_add_number <= 0
      || e.X_add_number > 0x1f)
    {
      as_bad ("First operand to .save.b must be a positive 5-bit constant");
      brmask = 0;
    }

  if (sep == ',')
    {
      unsigned reg;
      int n = popcount (brmask);

      parse_operand (&e, 0);
      reg = e.X_add_number - REG_GR;
      if (e.X_op != O_register || reg > 127)
	{
	  as_bad ("Second operand to .save.b must be a general register");
	  reg = 0;
	}
      else if (reg > 128U - n)
	{
	  as_bad ("Second operand to .save.b must be the first of %d general registers", n);
	  reg = 0;
	}
      add_unwind_entry (output_br_gr (brmask, reg), 0);
    }
  else
    add_unwind_entry (output_br_mem (brmask), 0);
}

static void
dot_savegf (dummy)
     int dummy ATTRIBUTE_UNUSED;
{
  expressionS e1, e2;

  if (!in_prologue ("save.gf"))
    return;

  if (parse_operand (&e1, ',') == ',')
    parse_operand (&e2, 0);
  else
    e2.X_op = O_absent;

  if (e1.X_op != O_constant
      || e1.X_add_number < 0
      || e1.X_add_number > 0xf)
    {
      as_bad ("First operand to .save.gf must be a non-negative 4-bit constant");
      e1.X_op = O_absent;
      e1.X_add_number = 0;
    }
  if (e2.X_op != O_constant
      || e2.X_add_number < 0
      || e2.X_add_number > 0xfffff)
    {
      as_bad ("Second operand to .save.gf must be a non-negative 20-bit constant");
      e2.X_op = O_absent;
      e2.X_add_number = 0;
    }
  if (e1.X_op == O_constant
      && e2.X_op == O_constant
      && e1.X_add_number == 0
      && e2.X_add_number == 0)
    as_bad ("Operands to .save.gf may not be both zero");

  add_unwind_entry (output_frgr_mem (e1.X_add_number, e2.X_add_number), 0);
}

static void
dot_spill (dummy)
     int dummy ATTRIBUTE_UNUSED;
{
  expressionS e;

  if (!in_prologue ("spill"))
    return;

  parse_operand (&e, 0);

  if (e.X_op != O_constant)
    {
      as_bad ("Operand to .spill must be a constant");
      e.X_add_number = 0;
    }
  add_unwind_entry (output_spill_base (e.X_add_number), 0);
}

static void
dot_spillreg (pred)
     int pred;
{
  int sep;
  unsigned int qp, ab, xy, reg, treg;
  expressionS e;
  const char * const po = pred ? "spillreg.p" : "spillreg";

  if (!in_procedure (po))
    return;

  if (pred)
    sep = parse_predicate_and_operand (&e, &qp, po);
  else
    {
      sep = parse_operand (&e, ',');
      qp = 0;
    }
  convert_expr_to_ab_reg (&e, &ab, &reg, po, 1 + pred);

  if (sep == ',')
    sep = parse_operand (&e, ',');
  else
    e.X_op = O_absent;
  convert_expr_to_xy_reg (&e, &xy, &treg, po, 2 + pred);

  add_unwind_entry (output_spill_reg (ab, reg, treg, xy, qp), sep);
}

static void
dot_spillmem (psprel)
     int psprel;
{
  expressionS e;
  int pred = (psprel < 0), sep;
  unsigned int qp, ab, reg;
  const char * po;

  if (pred)
    {
      psprel = ~psprel;
      po = psprel ? "spillpsp.p" : "spillsp.p";
    }
  else
    po = psprel ? "spillpsp" : "spillsp";

  if (!in_procedure (po))
    return;

  if (pred)
    sep = parse_predicate_and_operand (&e, &qp, po);
  else
    {
      sep = parse_operand (&e, ',');
      qp = 0;
    }
  convert_expr_to_ab_reg (&e, &ab, &reg, po, 1 + pred);

  if (sep == ',')
    sep = parse_operand (&e, ',');
  else
    e.X_op = O_absent;
  if (e.X_op != O_constant)
    {
      as_bad ("Operand %d to .%s must be a constant", 2 + pred, po);
      e.X_add_number = 0;
    }

  if (psprel)
    add_unwind_entry (output_spill_psprel (ab, reg, e.X_add_number, qp), sep);
  else
    add_unwind_entry (output_spill_sprel (ab, reg, e.X_add_number, qp), sep);
}

static unsigned int
get_saved_prologue_count (lbl)
     unsigned long lbl;
{
  label_prologue_count *lpc = unwind.saved_prologue_counts;

  while (lpc != NULL && lpc->label_number != lbl)
    lpc = lpc->next;

  if (lpc != NULL)
    return lpc->prologue_count;

  as_bad ("Missing .label_state %ld", lbl);
  return 1;
}

static void
save_prologue_count (lbl, count)
     unsigned long lbl;
     unsigned int count;
{
  label_prologue_count *lpc = unwind.saved_prologue_counts;

  while (lpc != NULL && lpc->label_number != lbl)
    lpc = lpc->next;

  if (lpc != NULL)
    lpc->prologue_count = count;
  else
    {
      label_prologue_count *new_lpc = xmalloc (sizeof (* new_lpc));

      new_lpc->next = unwind.saved_prologue_counts;
      new_lpc->label_number = lbl;
      new_lpc->prologue_count = count;
      unwind.saved_prologue_counts = new_lpc;
    }
}

static void
free_saved_prologue_counts ()
{
  label_prologue_count *lpc = unwind.saved_prologue_counts;
  label_prologue_count *next;

  while (lpc != NULL)
    {
      next = lpc->next;
      free (lpc);
      lpc = next;
    }

  unwind.saved_prologue_counts = NULL;
}

static void
dot_label_state (dummy)
     int dummy ATTRIBUTE_UNUSED;
{
  expressionS e;

  if (!in_body ("label_state"))
    return;

  parse_operand (&e, 0);
  if (e.X_op == O_constant)
    save_prologue_count (e.X_add_number, unwind.prologue_count);
  else
    {
      as_bad ("Operand to .label_state must be a constant");
      e.X_add_number = 0;
    }
  add_unwind_entry (output_label_state (e.X_add_number), 0);
}

static void
dot_copy_state (dummy)
     int dummy ATTRIBUTE_UNUSED;
{
  expressionS e;

  if (!in_body ("copy_state"))
    return;

  parse_operand (&e, 0);
  if (e.X_op == O_constant)
    unwind.prologue_count = get_saved_prologue_count (e.X_add_number);
  else
    {
      as_bad ("Operand to .copy_state must be a constant");
      e.X_add_number = 0;
    }
  add_unwind_entry (output_copy_state (e.X_add_number), 0);
}

static void
dot_unwabi (dummy)
     int dummy ATTRIBUTE_UNUSED;
{
  expressionS e1, e2;
  unsigned char sep;

  if (!in_prologue ("unwabi"))
    return;

  sep = parse_operand (&e1, ',');
  if (sep == ',')
    parse_operand (&e2, 0);
  else
    e2.X_op = O_absent;

  if (e1.X_op != O_constant)
    {
      as_bad ("First operand to .unwabi must be a constant");
      e1.X_add_number = 0;
    }

  if (e2.X_op != O_constant)
    {
      as_bad ("Second operand to .unwabi must be a constant");
      e2.X_add_number = 0;
    }

  add_unwind_entry (output_unwabi (e1.X_add_number, e2.X_add_number), 0);
}

static void
dot_personality (dummy)
     int dummy ATTRIBUTE_UNUSED;
{
  char *name, *p, c;
  if (!in_procedure ("personality"))
    return;
  SKIP_WHITESPACE ();
  name = input_line_pointer;
  c = get_symbol_end ();
  p = input_line_pointer;
  unwind.personality_routine = symbol_find_or_make (name);
  unwind.force_unwind_entry = 1;
  *p = c;
  SKIP_WHITESPACE ();
  demand_empty_rest_of_line ();
}

static void
dot_proc (dummy)
     int dummy ATTRIBUTE_UNUSED;
{
  char *name, *p, c;
  symbolS *sym;
  proc_pending *pending, *last_pending;

  if (unwind.proc_pending.sym)
    {
      (md.unwind_check == unwind_check_warning
       ? as_warn
       : as_bad) ("Missing .endp after previous .proc");
      while (unwind.proc_pending.next)
	{
	  pending = unwind.proc_pending.next;
	  unwind.proc_pending.next = pending->next;
	  free (pending);
	}
    }
  last_pending = NULL;

  /* Parse names of main and alternate entry points and mark them as
     function symbols:  */
  while (1)
    {
      SKIP_WHITESPACE ();
      name = input_line_pointer;
      c = get_symbol_end ();
      p = input_line_pointer;
      if (!*name)
	as_bad ("Empty argument of .proc");
      else
	{
	  sym = symbol_find_or_make (name);
	  if (S_IS_DEFINED (sym))
	    as_bad ("`%s' was already defined", name);
	  else if (!last_pending)
	    {
	      unwind.proc_pending.sym = sym;
	      last_pending = &unwind.proc_pending;
	    }
	  else
	    {
	      pending = xmalloc (sizeof (*pending));
	      pending->sym = sym;
	      last_pending = last_pending->next = pending;
	    }
	  symbol_get_bfdsym (sym)->flags |= BSF_FUNCTION;
	}
      *p = c;
      SKIP_WHITESPACE ();
      if (*input_line_pointer != ',')
	break;
      ++input_line_pointer;
    }
  if (!last_pending)
    {
      unwind.proc_pending.sym = expr_build_dot ();
      last_pending = &unwind.proc_pending;
    }
  last_pending->next = NULL;
  demand_empty_rest_of_line ();
  ia64_do_align (16);

  unwind.prologue = 0;
  unwind.prologue_count = 0;
  unwind.body = 0;
  unwind.insn = 0;
  unwind.list = unwind.tail = unwind.current_entry = NULL;
  unwind.personality_routine = 0;
}

static void
dot_body (dummy)
     int dummy ATTRIBUTE_UNUSED;
{
  if (!in_procedure ("body"))
    return;
  if (!unwind.prologue && !unwind.body && unwind.insn)
    as_warn ("Initial .body should precede any instructions");
  check_pending_save ();

  unwind.prologue = 0;
  unwind.prologue_mask = 0;
  unwind.body = 1;

  add_unwind_entry (output_body (), 0);
}

static void
dot_prologue (dummy)
     int dummy ATTRIBUTE_UNUSED;
{
  unsigned mask = 0, grsave = 0;

  if (!in_procedure ("prologue"))
    return;
  if (unwind.prologue)
    {
      as_bad (".prologue within prologue");
      ignore_rest_of_line ();
      return;
    }
  if (!unwind.body && unwind.insn)
    as_warn ("Initial .prologue should precede any instructions");

  if (!is_it_end_of_statement ())
    {
      expressionS e;
      int n, sep = parse_operand (&e, ',');

      if (e.X_op != O_constant
	  || e.X_add_number < 0
	  || e.X_add_number > 0xf)
	as_bad ("First operand to .prologue must be a positive 4-bit constant");
      else if (e.X_add_number == 0)
	as_warn ("Pointless use of zero first operand to .prologue");
      else
	mask = e.X_add_number;
	n = popcount (mask);

      if (sep == ',')
	parse_operand (&e, 0);
      else
	e.X_op = O_absent;
      if (e.X_op == O_constant
	  && e.X_add_number >= 0
	  && e.X_add_number < 128)
	{
	  if (md.unwind_check == unwind_check_error)
	    as_warn ("Using a constant as second operand to .prologue is deprecated");
	  grsave = e.X_add_number;
	}
      else if (e.X_op != O_register
	       || (grsave = e.X_add_number - REG_GR) > 127)
	{
	  as_bad ("Second operand to .prologue must be a general register");
	  grsave = 0;
	}
      else if (grsave > 128U - n)
	{
	  as_bad ("Second operand to .prologue must be the first of %d general registers", n);
	  grsave = 0;
	}

    }

  if (mask)
    add_unwind_entry (output_prologue_gr (mask, grsave), 0);
  else
    add_unwind_entry (output_prologue (), 0);

  unwind.prologue = 1;
  unwind.prologue_mask = mask;
  unwind.prologue_gr = grsave;
  unwind.body = 0;
  ++unwind.prologue_count;
}

static void
dot_endp (dummy)
     int dummy ATTRIBUTE_UNUSED;
{
  expressionS e;
  int bytes_per_address;
  long where;
  segT saved_seg;
  subsegT saved_subseg;
  proc_pending *pending;
  int unwind_check = md.unwind_check;

  md.unwind_check = unwind_check_error;
  if (!in_procedure ("endp"))
    return;
  md.unwind_check = unwind_check;

  if (unwind.saved_text_seg)
    {
      saved_seg = unwind.saved_text_seg;
      saved_subseg = unwind.saved_text_subseg;
      unwind.saved_text_seg = NULL;
    }
  else
    {
      saved_seg = now_seg;
      saved_subseg = now_subseg;
    }

  insn_group_break (1, 0, 0);

  /* If there wasn't a .handlerdata, we haven't generated an image yet.  */
  if (!unwind.info)
    generate_unwind_image (saved_seg);

  if (unwind.info || unwind.force_unwind_entry)
    {
      symbolS *proc_end;

      subseg_set (md.last_text_seg, 0);
      proc_end = expr_build_dot ();

      start_unwind_section (saved_seg, SPECIAL_SECTION_UNWIND);

      /* Make sure that section has 4 byte alignment for ILP32 and
         8 byte alignment for LP64.  */
      record_alignment (now_seg, md.pointer_size_shift);

      /* Need space for 3 pointers for procedure start, procedure end,
	 and unwind info.  */
      memset (frag_more (3 * md.pointer_size), 0, 3 * md.pointer_size);
      where = frag_now_fix () - (3 * md.pointer_size);
      bytes_per_address = bfd_arch_bits_per_address (stdoutput) / 8;

      /* Issue the values of  a) Proc Begin, b) Proc End, c) Unwind Record.  */
      e.X_op = O_pseudo_fixup;
      e.X_op_symbol = pseudo_func[FUNC_SEG_RELATIVE].u.sym;
      e.X_add_number = 0;
      if (!S_IS_LOCAL (unwind.proc_pending.sym)
	  && S_IS_DEFINED (unwind.proc_pending.sym))
	e.X_add_symbol = symbol_temp_new (S_GET_SEGMENT (unwind.proc_pending.sym),
					  S_GET_VALUE (unwind.proc_pending.sym),
					  symbol_get_frag (unwind.proc_pending.sym));
      else
	e.X_add_symbol = unwind.proc_pending.sym;
      ia64_cons_fix_new (frag_now, where, bytes_per_address, &e);

      e.X_op = O_pseudo_fixup;
      e.X_op_symbol = pseudo_func[FUNC_SEG_RELATIVE].u.sym;
      e.X_add_number = 0;
      e.X_add_symbol = proc_end;
      ia64_cons_fix_new (frag_now, where + bytes_per_address,
			 bytes_per_address, &e);

      if (unwind.info)
	{
	  e.X_op = O_pseudo_fixup;
	  e.X_op_symbol = pseudo_func[FUNC_SEG_RELATIVE].u.sym;
	  e.X_add_number = 0;
	  e.X_add_symbol = unwind.info;
	  ia64_cons_fix_new (frag_now, where + (bytes_per_address * 2),
			     bytes_per_address, &e);
	}
    }
  subseg_set (saved_seg, saved_subseg);

  /* Set symbol sizes.  */
  pending = &unwind.proc_pending;
  if (S_GET_NAME (pending->sym))
    {
      do
	{
	  symbolS *sym = pending->sym;

	  if (!S_IS_DEFINED (sym))
	    as_bad ("`%s' was not defined within procedure", S_GET_NAME (sym));
	  else if (S_GET_SIZE (sym) == 0
		   && symbol_get_obj (sym)->size == NULL)
	    {
	      fragS *frag = symbol_get_frag (sym);

	      if (frag)
		{
		  if (frag == frag_now && SEG_NORMAL (now_seg))
		    S_SET_SIZE (sym, frag_now_fix () - S_GET_VALUE (sym));
		  else
		    {
		      symbol_get_obj (sym)->size =
			(expressionS *) xmalloc (sizeof (expressionS));
		      symbol_get_obj (sym)->size->X_op = O_subtract;
		      symbol_get_obj (sym)->size->X_add_symbol
			= symbol_new (FAKE_LABEL_NAME, now_seg,
				      frag_now_fix (), frag_now);
		      symbol_get_obj (sym)->size->X_op_symbol = sym;
		      symbol_get_obj (sym)->size->X_add_number = 0;
		    }
		}
	    }
	} while ((pending = pending->next) != NULL);
    }

  /* Parse names of main and alternate entry points.  */
  while (1)
    {
      char *name, *p, c;

      SKIP_WHITESPACE ();
      name = input_line_pointer;
      c = get_symbol_end ();
      p = input_line_pointer;
      if (!*name)
	(md.unwind_check == unwind_check_warning
	 ? as_warn
	 : as_bad) ("Empty argument of .endp");
      else
	{
	  symbolS *sym = symbol_find (name);

	  for (pending = &unwind.proc_pending; pending; pending = pending->next)
	    {
	      if (sym == pending->sym)
		{
		  pending->sym = NULL;
		  break;
		}
	    }
	  if (!sym || !pending)
	    as_warn ("`%s' was not specified with previous .proc", name);
	}
      *p = c;
      SKIP_WHITESPACE ();
      if (*input_line_pointer != ',')
	break;
      ++input_line_pointer;
    }
  demand_empty_rest_of_line ();

  /* Deliberately only checking for the main entry point here; the
     language spec even says all arguments to .endp are ignored.  */
  if (unwind.proc_pending.sym
      && S_GET_NAME (unwind.proc_pending.sym)
      && strcmp (S_GET_NAME (unwind.proc_pending.sym), FAKE_LABEL_NAME))
    as_warn ("`%s' should be an operand to this .endp",
	     S_GET_NAME (unwind.proc_pending.sym));
  while (unwind.proc_pending.next)
    {
      pending = unwind.proc_pending.next;
      unwind.proc_pending.next = pending->next;
      free (pending);
    }
  unwind.proc_pending.sym = unwind.info = NULL;
}

static void
dot_template (template)
     int template;
{
  CURR_SLOT.user_template = template;
}

static void
dot_regstk (dummy)
     int dummy ATTRIBUTE_UNUSED;
{
  int ins, locs, outs, rots;

  if (is_it_end_of_statement ())
    ins = locs = outs = rots = 0;
  else
    {
      ins = get_absolute_expression ();
      if (*input_line_pointer++ != ',')
	goto err;
      locs = get_absolute_expression ();
      if (*input_line_pointer++ != ',')
	goto err;
      outs = get_absolute_expression ();
      if (*input_line_pointer++ != ',')
	goto err;
      rots = get_absolute_expression ();
    }
  set_regstack (ins, locs, outs, rots);
  return;

 err:
  as_bad ("Comma expected");
  ignore_rest_of_line ();
}

static void
dot_rot (type)
     int type;
{
  offsetT num_regs;
  valueT num_alloced = 0;
  struct dynreg **drpp, *dr;
  int ch, base_reg = 0;
  char *name, *start;
  size_t len;

  switch (type)
    {
    case DYNREG_GR: base_reg = REG_GR + 32; break;
    case DYNREG_FR: base_reg = REG_FR + 32; break;
    case DYNREG_PR: base_reg = REG_P + 16; break;
    default: break;
    }

  /* First, remove existing names from hash table.  */
  for (dr = md.dynreg[type]; dr && dr->num_regs; dr = dr->next)
    {
      hash_delete (md.dynreg_hash, dr->name);
      /* FIXME: Free dr->name.  */
      dr->num_regs = 0;
    }

  drpp = &md.dynreg[type];
  while (1)
    {
      start = input_line_pointer;
      ch = get_symbol_end ();
      len = strlen (ia64_canonicalize_symbol_name (start));
      *input_line_pointer = ch;

      SKIP_WHITESPACE ();
      if (*input_line_pointer != '[')
	{
	  as_bad ("Expected '['");
	  goto err;
	}
      ++input_line_pointer;	/* skip '[' */

      num_regs = get_absolute_expression ();

      if (*input_line_pointer++ != ']')
	{
	  as_bad ("Expected ']'");
	  goto err;
	}
      if (num_regs <= 0)
	{
	  as_bad ("Number of elements must be positive");
	  goto err;
	}
      SKIP_WHITESPACE ();

      num_alloced += num_regs;
      switch (type)
	{
	case DYNREG_GR:
	  if (num_alloced > md.rot.num_regs)
	    {
	      as_bad ("Used more than the declared %d rotating registers",
		      md.rot.num_regs);
	      goto err;
	    }
	  break;
	case DYNREG_FR:
	  if (num_alloced > 96)
	    {
	      as_bad ("Used more than the available 96 rotating registers");
	      goto err;
	    }
	  break;
	case DYNREG_PR:
	  if (num_alloced > 48)
	    {
	      as_bad ("Used more than the available 48 rotating registers");
	      goto err;
	    }
	  break;

	default:
	  break;
	}

      if (!*drpp)
	{
	  *drpp = obstack_alloc (&notes, sizeof (*dr));
	  memset (*drpp, 0, sizeof (*dr));
	}

      name = obstack_alloc (&notes, len + 1);
      memcpy (name, start, len);
      name[len] = '\0';

      dr = *drpp;
      dr->name = name;
      dr->num_regs = num_regs;
      dr->base = base_reg;
      drpp = &dr->next;
      base_reg += num_regs;

      if (hash_insert (md.dynreg_hash, name, dr))
	{
	  as_bad ("Attempt to redefine register set `%s'", name);
	  obstack_free (&notes, name);
	  goto err;
	}

      if (*input_line_pointer != ',')
	break;
      ++input_line_pointer;	/* skip comma */
      SKIP_WHITESPACE ();
    }
  demand_empty_rest_of_line ();
  return;

 err:
  ignore_rest_of_line ();
}

static void
dot_byteorder (byteorder)
     int byteorder;
{
  segment_info_type *seginfo = seg_info (now_seg);

  if (byteorder == -1)
    {
      if (seginfo->tc_segment_info_data.endian == 0)
	seginfo->tc_segment_info_data.endian = default_big_endian ? 1 : 2;
      byteorder = seginfo->tc_segment_info_data.endian == 1;
    }
  else
    seginfo->tc_segment_info_data.endian = byteorder ? 1 : 2;

  if (target_big_endian != byteorder)
    {
      target_big_endian = byteorder;
      if (target_big_endian)
	{
	  ia64_number_to_chars = number_to_chars_bigendian;
	  ia64_float_to_chars = ia64_float_to_chars_bigendian;
	}
      else
	{
	  ia64_number_to_chars = number_to_chars_littleendian;
	  ia64_float_to_chars = ia64_float_to_chars_littleendian;
	}
    }
}

static void
dot_psr (dummy)
     int dummy ATTRIBUTE_UNUSED;
{
  char *option;
  int ch;

  while (1)
    {
      option = input_line_pointer;
      ch = get_symbol_end ();
      if (strcmp (option, "lsb") == 0)
	md.flags &= ~EF_IA_64_BE;
      else if (strcmp (option, "msb") == 0)
	md.flags |= EF_IA_64_BE;
      else if (strcmp (option, "abi32") == 0)
	md.flags &= ~EF_IA_64_ABI64;
      else if (strcmp (option, "abi64") == 0)
	md.flags |= EF_IA_64_ABI64;
      else
	as_bad ("Unknown psr option `%s'", option);
      *input_line_pointer = ch;

      SKIP_WHITESPACE ();
      if (*input_line_pointer != ',')
	break;

      ++input_line_pointer;
      SKIP_WHITESPACE ();
    }
  demand_empty_rest_of_line ();
}

static void
dot_ln (dummy)
     int dummy ATTRIBUTE_UNUSED;
{
  new_logical_line (0, get_absolute_expression ());
  demand_empty_rest_of_line ();
}

static void
cross_section (ref, cons, ua)
     int ref;
     void (*cons) PARAMS((int));
     int ua;
{
  char *start, *end;
  int saved_auto_align;
  unsigned int section_count;

  SKIP_WHITESPACE ();
  start = input_line_pointer;
  if (*start == '"')
    {
      int len;
      char *name;

      name = demand_copy_C_string (&len);
      obstack_free(&notes, name);
      if (!name)
	{
	  ignore_rest_of_line ();
	  return;
	}
    }
  else
    {
      char c = get_symbol_end ();

      if (input_line_pointer == start)
	{
	  as_bad ("Missing section name");
	  ignore_rest_of_line ();
	  return;
	}
      *input_line_pointer = c;
    }
  end = input_line_pointer;
  SKIP_WHITESPACE ();
  if (*input_line_pointer != ',')
    {
      as_bad ("Comma expected after section name");
      ignore_rest_of_line ();
      return;
    }
  *end = '\0';
  end = input_line_pointer + 1;		/* skip comma */
  input_line_pointer = start;
  md.keep_pending_output = 1;
  section_count = bfd_count_sections(stdoutput);
  obj_elf_section (0);
  if (section_count != bfd_count_sections(stdoutput))
    as_warn ("Creating sections with .xdataN/.xrealN/.xstringZ is deprecated.");
  input_line_pointer = end;
  saved_auto_align = md.auto_align;
  if (ua)
    md.auto_align = 0;
  (*cons) (ref);
  if (ua)
    md.auto_align = saved_auto_align;
  obj_elf_previous (0);
  md.keep_pending_output = 0;
}

static void
dot_xdata (size)
     int size;
{
  cross_section (size, cons, 0);
}

/* Why doesn't float_cons() call md_cons_align() the way cons() does?  */

static void
stmt_float_cons (kind)
     int kind;
{
  size_t alignment;

  switch (kind)
    {
    case 'd':
      alignment = 8;
      break;

    case 'x':
    case 'X':
      alignment = 16;
      break;

    case 'f':
    default:
      alignment = 4;
      break;
    }
  ia64_do_align (alignment);
  float_cons (kind);
}

static void
stmt_cons_ua (size)
     int size;
{
  int saved_auto_align = md.auto_align;

  md.auto_align = 0;
  cons (size);
  md.auto_align = saved_auto_align;
}

static void
dot_xfloat_cons (kind)
     int kind;
{
  cross_section (kind, stmt_float_cons, 0);
}

static void
dot_xstringer (zero)
     int zero;
{
  cross_section (zero, stringer, 0);
}

static void
dot_xdata_ua (size)
     int size;
{
  cross_section (size, cons, 1);
}

static void
dot_xfloat_cons_ua (kind)
     int kind;
{
  cross_section (kind, float_cons, 1);
}

/* .reg.val <regname>,value */

static void
dot_reg_val (dummy)
     int dummy ATTRIBUTE_UNUSED;
{
  expressionS reg;

  expression_and_evaluate (&reg);
  if (reg.X_op != O_register)
    {
      as_bad (_("Register name expected"));
      ignore_rest_of_line ();
    }
  else if (*input_line_pointer++ != ',')
    {
      as_bad (_("Comma expected"));
      ignore_rest_of_line ();
    }
  else
    {
      valueT value = get_absolute_expression ();
      int regno = reg.X_add_number;
      if (regno <= REG_GR || regno > REG_GR + 127)
	as_warn (_("Register value annotation ignored"));
      else
	{
	  gr_values[regno - REG_GR].known = 1;
	  gr_values[regno - REG_GR].value = value;
	  gr_values[regno - REG_GR].path = md.path;
	}
    }
  demand_empty_rest_of_line ();
}

/*
  .serialize.data
  .serialize.instruction
 */
static void
dot_serialize (type)
     int type;
{
  insn_group_break (0, 0, 0);
  if (type)
    instruction_serialization ();
  else
    data_serialization ();
  insn_group_break (0, 0, 0);
  demand_empty_rest_of_line ();
}

/* select dv checking mode
   .auto
   .explicit
   .default

   A stop is inserted when changing modes
 */

static void
dot_dv_mode (type)
     int type;
{
  if (md.manual_bundling)
    as_warn (_("Directive invalid within a bundle"));

  if (type == 'E' || type == 'A')
    md.mode_explicitly_set = 0;
  else
    md.mode_explicitly_set = 1;

  md.detect_dv = 1;
  switch (type)
    {
    case 'A':
    case 'a':
      if (md.explicit_mode)
	insn_group_break (1, 0, 0);
      md.explicit_mode = 0;
      break;
    case 'E':
    case 'e':
      if (!md.explicit_mode)
	insn_group_break (1, 0, 0);
      md.explicit_mode = 1;
      break;
    default:
    case 'd':
      if (md.explicit_mode != md.default_explicit_mode)
	insn_group_break (1, 0, 0);
      md.explicit_mode = md.default_explicit_mode;
      md.mode_explicitly_set = 0;
      break;
    }
}

static void
print_prmask (mask)
     valueT mask;
{
  int regno;
  char *comma = "";
  for (regno = 0; regno < 64; regno++)
    {
      if (mask & ((valueT) 1 << regno))
	{
	  fprintf (stderr, "%s p%d", comma, regno);
	  comma = ",";
	}
    }
}

/*
  .pred.rel.clear [p1 [,p2 [,...]]]     (also .pred.rel "clear" or @clear)
  .pred.rel.imply p1, p2                (also .pred.rel "imply" or @imply)
  .pred.rel.mutex p1, p2 [,...]         (also .pred.rel "mutex" or @mutex)
  .pred.safe_across_calls p1 [, p2 [,...]]
 */

static void
dot_pred_rel (type)
     int type;
{
  valueT mask = 0;
  int count = 0;
  int p1 = -1, p2 = -1;

  if (type == 0)
    {
      if (*input_line_pointer == '"')
	{
	  int len;
	  char *form = demand_copy_C_string (&len);

	  if (strcmp (form, "mutex") == 0)
	    type = 'm';
	  else if (strcmp (form, "clear") == 0)
	    type = 'c';
	  else if (strcmp (form, "imply") == 0)
	    type = 'i';
	  obstack_free (&notes, form);
	}
      else if (*input_line_pointer == '@')
	{
	  char *form = ++input_line_pointer;
	  char c = get_symbol_end();

	  if (strcmp (form, "mutex") == 0)
	    type = 'm';
	  else if (strcmp (form, "clear") == 0)
	    type = 'c';
	  else if (strcmp (form, "imply") == 0)
	    type = 'i';
	  *input_line_pointer = c;
	}
      else
	{
	  as_bad (_("Missing predicate relation type"));
	  ignore_rest_of_line ();
	  return;
	}
      if (type == 0)
	{
	  as_bad (_("Unrecognized predicate relation type"));
	  ignore_rest_of_line ();
	  return;
	}
      if (*input_line_pointer == ',')
	++input_line_pointer;
      SKIP_WHITESPACE ();
    }

  SKIP_WHITESPACE ();
  while (1)
    {
      valueT bits = 1;
      int regno;
      expressionS pr, *pr1, *pr2;

      expression_and_evaluate (&pr);
      if (pr.X_op == O_register
	  && pr.X_add_number >= REG_P
	  && pr.X_add_number <= REG_P + 63)
	{
	  regno = pr.X_add_number - REG_P;
	  bits <<= regno;
	  count++;
	  if (p1 == -1)
	    p1 = regno;
	  else if (p2 == -1)
	    p2 = regno;
	}
      else if (type != 'i'
	  && pr.X_op == O_subtract
	  && (pr1 = symbol_get_value_expression (pr.X_add_symbol))
	  && pr1->X_op == O_register
	  && pr1->X_add_number >= REG_P
	  && pr1->X_add_number <= REG_P + 63
	  && (pr2 = symbol_get_value_expression (pr.X_op_symbol))
	  && pr2->X_op == O_register
	  && pr2->X_add_number >= REG_P
	  && pr2->X_add_number <= REG_P + 63)
	{
	  /* It's a range.  */
	  int stop;

	  regno = pr1->X_add_number - REG_P;
	  stop = pr2->X_add_number - REG_P;
	  if (regno >= stop)
	    {
	      as_bad (_("Bad register range"));
	      ignore_rest_of_line ();
	      return;
	    }
	  bits = ((bits << stop) << 1) - (bits << regno);
	  count += stop - regno + 1;
	}
      else
	{
	  as_bad (_("Predicate register expected"));
	  ignore_rest_of_line ();
	  return;
	}
      if (mask & bits)
	as_warn (_("Duplicate predicate register ignored"));
      mask |= bits;
      if (*input_line_pointer != ',')
	break;
      ++input_line_pointer;
      SKIP_WHITESPACE ();
    }

  switch (type)
    {
    case 'c':
      if (count == 0)
	mask = ~(valueT) 0;
      clear_qp_mutex (mask);
      clear_qp_implies (mask, (valueT) 0);
      break;
    case 'i':
      if (count != 2 || p1 == -1 || p2 == -1)
	as_bad (_("Predicate source and target required"));
      else if (p1 == 0 || p2 == 0)
	as_bad (_("Use of p0 is not valid in this context"));
      else
	add_qp_imply (p1, p2);
      break;
    case 'm':
      if (count < 2)
	{
	  as_bad (_("At least two PR arguments expected"));
	  break;
	}
      else if (mask & 1)
	{
	  as_bad (_("Use of p0 is not valid in this context"));
	  break;
	}
      add_qp_mutex (mask);
      break;
    case 's':
      /* note that we don't override any existing relations */
      if (count == 0)
	{
	  as_bad (_("At least one PR argument expected"));
	  break;
	}
      if (md.debug_dv)
	{
	  fprintf (stderr, "Safe across calls: ");
	  print_prmask (mask);
	  fprintf (stderr, "\n");
	}
      qp_safe_across_calls = mask;
      break;
    }
  demand_empty_rest_of_line ();
}

/* .entry label [, label [, ...]]
   Hint to DV code that the given labels are to be considered entry points.
   Otherwise, only global labels are considered entry points.  */

static void
dot_entry (dummy)
     int dummy ATTRIBUTE_UNUSED;
{
  const char *err;
  char *name;
  int c;
  symbolS *symbolP;

  do
    {
      name = input_line_pointer;
      c = get_symbol_end ();
      symbolP = symbol_find_or_make (name);

      err = hash_insert (md.entry_hash, S_GET_NAME (symbolP), (PTR) symbolP);
      if (err)
	as_fatal (_("Inserting \"%s\" into entry hint table failed: %s"),
		  name, err);

      *input_line_pointer = c;
      SKIP_WHITESPACE ();
      c = *input_line_pointer;
      if (c == ',')
	{
	  input_line_pointer++;
	  SKIP_WHITESPACE ();
	  if (*input_line_pointer == '\n')
	    c = '\n';
	}
    }
  while (c == ',');

  demand_empty_rest_of_line ();
}

/* .mem.offset offset, base
   "base" is used to distinguish between offsets from a different base.  */

static void
dot_mem_offset (dummy)
  int dummy ATTRIBUTE_UNUSED;
{
  md.mem_offset.hint = 1;
  md.mem_offset.offset = get_absolute_expression ();
  if (*input_line_pointer != ',')
    {
      as_bad (_("Comma expected"));
      ignore_rest_of_line ();
      return;
    }
  ++input_line_pointer;
  md.mem_offset.base = get_absolute_expression ();
  demand_empty_rest_of_line ();
}

/* ia64-specific pseudo-ops:  */
const pseudo_typeS md_pseudo_table[] =
  {
    { "radix", dot_radix, 0 },
    { "lcomm", s_lcomm_bytes, 1 },
    { "loc", dot_loc, 0 },
    { "bss", dot_special_section, SPECIAL_SECTION_BSS },
    { "sbss", dot_special_section, SPECIAL_SECTION_SBSS },
    { "sdata", dot_special_section, SPECIAL_SECTION_SDATA },
    { "rodata", dot_special_section, SPECIAL_SECTION_RODATA },
    { "comment", dot_special_section, SPECIAL_SECTION_COMMENT },
    { "ia_64.unwind", dot_special_section, SPECIAL_SECTION_UNWIND },
    { "ia_64.unwind_info", dot_special_section, SPECIAL_SECTION_UNWIND_INFO },
    { "init_array", dot_special_section, SPECIAL_SECTION_INIT_ARRAY },
    { "fini_array", dot_special_section, SPECIAL_SECTION_FINI_ARRAY },
    { "proc", dot_proc, 0 },
    { "body", dot_body, 0 },
    { "prologue", dot_prologue, 0 },
    { "endp", dot_endp, 0 },

    { "fframe", dot_fframe, 0 },
    { "vframe", dot_vframe, 0 },
    { "vframesp", dot_vframesp, 0 },
    { "vframepsp", dot_vframesp, 1 },
    { "save", dot_save, 0 },
    { "restore", dot_restore, 0 },
    { "restorereg", dot_restorereg, 0 },
    { "restorereg.p", dot_restorereg, 1 },
    { "handlerdata", dot_handlerdata, 0 },
    { "unwentry", dot_unwentry, 0 },
    { "altrp", dot_altrp, 0 },
    { "savesp", dot_savemem, 0 },
    { "savepsp", dot_savemem, 1 },
    { "save.g", dot_saveg, 0 },
    { "save.f", dot_savef, 0 },
    { "save.b", dot_saveb, 0 },
    { "save.gf", dot_savegf, 0 },
    { "spill", dot_spill, 0 },
    { "spillreg", dot_spillreg, 0 },
    { "spillsp", dot_spillmem, 0 },
    { "spillpsp", dot_spillmem, 1 },
    { "spillreg.p", dot_spillreg, 1 },
    { "spillsp.p", dot_spillmem, ~0 },
    { "spillpsp.p", dot_spillmem, ~1 },
    { "label_state", dot_label_state, 0 },
    { "copy_state", dot_copy_state, 0 },
    { "unwabi", dot_unwabi, 0 },
    { "personality", dot_personality, 0 },
    { "mii", dot_template, 0x0 },
    { "mli", dot_template, 0x2 }, /* old format, for compatibility */
    { "mlx", dot_template, 0x2 },
    { "mmi", dot_template, 0x4 },
    { "mfi", dot_template, 0x6 },
    { "mmf", dot_template, 0x7 },
    { "mib", dot_template, 0x8 },
    { "mbb", dot_template, 0x9 },
    { "bbb", dot_template, 0xb },
    { "mmb", dot_template, 0xc },
    { "mfb", dot_template, 0xe },
    { "align", dot_align, 0 },
    { "regstk", dot_regstk, 0 },
    { "rotr", dot_rot, DYNREG_GR },
    { "rotf", dot_rot, DYNREG_FR },
    { "rotp", dot_rot, DYNREG_PR },
    { "lsb", dot_byteorder, 0 },
    { "msb", dot_byteorder, 1 },
    { "psr", dot_psr, 0 },
    { "alias", dot_alias, 0 },
    { "secalias", dot_alias, 1 },
    { "ln", dot_ln, 0 },		/* source line info (for debugging) */

    { "xdata1", dot_xdata, 1 },
    { "xdata2", dot_xdata, 2 },
    { "xdata4", dot_xdata, 4 },
    { "xdata8", dot_xdata, 8 },
    { "xdata16", dot_xdata, 16 },
    { "xreal4", dot_xfloat_cons, 'f' },
    { "xreal8", dot_xfloat_cons, 'd' },
    { "xreal10", dot_xfloat_cons, 'x' },
    { "xreal16", dot_xfloat_cons, 'X' },
    { "xstring", dot_xstringer, 0 },
    { "xstringz", dot_xstringer, 1 },

    /* unaligned versions:  */
    { "xdata2.ua", dot_xdata_ua, 2 },
    { "xdata4.ua", dot_xdata_ua, 4 },
    { "xdata8.ua", dot_xdata_ua, 8 },
    { "xdata16.ua", dot_xdata_ua, 16 },
    { "xreal4.ua", dot_xfloat_cons_ua, 'f' },
    { "xreal8.ua", dot_xfloat_cons_ua, 'd' },
    { "xreal10.ua", dot_xfloat_cons_ua, 'x' },
    { "xreal16.ua", dot_xfloat_cons_ua, 'X' },

    /* annotations/DV checking support */
    { "entry", dot_entry, 0 },
    { "mem.offset", dot_mem_offset, 0 },
    { "pred.rel", dot_pred_rel, 0 },
    { "pred.rel.clear", dot_pred_rel, 'c' },
    { "pred.rel.imply", dot_pred_rel, 'i' },
    { "pred.rel.mutex", dot_pred_rel, 'm' },
    { "pred.safe_across_calls", dot_pred_rel, 's' },
    { "reg.val", dot_reg_val, 0 },
    { "serialize.data", dot_serialize, 0 },
    { "serialize.instruction", dot_serialize, 1 },
    { "auto", dot_dv_mode, 'a' },
    { "explicit", dot_dv_mode, 'e' },
    { "default", dot_dv_mode, 'd' },

    /* ??? These are needed to make gas/testsuite/gas/elf/ehopt.s work.
       IA-64 aligns data allocation pseudo-ops by default, so we have to
       tell it that these ones are supposed to be unaligned.  Long term,
       should rewrite so that only IA-64 specific data allocation pseudo-ops
       are aligned by default.  */
    {"2byte", stmt_cons_ua, 2},
    {"4byte", stmt_cons_ua, 4},
    {"8byte", stmt_cons_ua, 8},

    { NULL, 0, 0 }
  };

static const struct pseudo_opcode
  {
    const char *name;
    void (*handler) (int);
    int arg;
  }
pseudo_opcode[] =
  {
    /* these are more like pseudo-ops, but don't start with a dot */
    { "data1", cons, 1 },
    { "data2", cons, 2 },
    { "data4", cons, 4 },
    { "data8", cons, 8 },
    { "data16", cons, 16 },
    { "real4", stmt_float_cons, 'f' },
    { "real8", stmt_float_cons, 'd' },
    { "real10", stmt_float_cons, 'x' },
    { "real16", stmt_float_cons, 'X' },
    { "string", stringer, 0 },
    { "stringz", stringer, 1 },

    /* unaligned versions:  */
    { "data2.ua", stmt_cons_ua, 2 },
    { "data4.ua", stmt_cons_ua, 4 },
    { "data8.ua", stmt_cons_ua, 8 },
    { "data16.ua", stmt_cons_ua, 16 },
    { "real4.ua", float_cons, 'f' },
    { "real8.ua", float_cons, 'd' },
    { "real10.ua", float_cons, 'x' },
    { "real16.ua", float_cons, 'X' },
  };

/* Declare a register by creating a symbol for it and entering it in
   the symbol table.  */

static symbolS *
declare_register (name, regnum)
     const char *name;
     unsigned int regnum;
{
  const char *err;
  symbolS *sym;

  sym = symbol_create (name, reg_section, regnum, &zero_address_frag);

  err = hash_insert (md.reg_hash, S_GET_NAME (sym), (PTR) sym);
  if (err)
    as_fatal ("Inserting \"%s\" into register table failed: %s",
	      name, err);

  return sym;
}

static void
declare_register_set (prefix, num_regs, base_regnum)
     const char *prefix;
     unsigned int num_regs;
     unsigned int base_regnum;
{
  char name[8];
  unsigned int i;

  for (i = 0; i < num_regs; ++i)
    {
      snprintf (name, sizeof (name), "%s%u", prefix, i);
      declare_register (name, base_regnum + i);
    }
}

static unsigned int
operand_width (opnd)
     enum ia64_opnd opnd;
{
  const struct ia64_operand *odesc = &elf64_ia64_operands[opnd];
  unsigned int bits = 0;
  int i;

  bits = 0;
  for (i = 0; i < NELEMS (odesc->field) && odesc->field[i].bits; ++i)
    bits += odesc->field[i].bits;

  return bits;
}

static enum operand_match_result
operand_match (idesc, index, e)
     const struct ia64_opcode *idesc;
     int index;
     expressionS *e;
{
  enum ia64_opnd opnd = idesc->operands[index];
  int bits, relocatable = 0;
  struct insn_fix *fix;
  bfd_signed_vma val;

  switch (opnd)
    {
      /* constants:  */

    case IA64_OPND_AR_CCV:
      if (e->X_op == O_register && e->X_add_number == REG_AR + 32)
	return OPERAND_MATCH;
      break;

    case IA64_OPND_AR_CSD:
      if (e->X_op == O_register && e->X_add_number == REG_AR + 25)
	return OPERAND_MATCH;
      break;

    case IA64_OPND_AR_PFS:
      if (e->X_op == O_register && e->X_add_number == REG_AR + 64)
	return OPERAND_MATCH;
      break;

    case IA64_OPND_GR0:
      if (e->X_op == O_register && e->X_add_number == REG_GR + 0)
	return OPERAND_MATCH;
      break;

    case IA64_OPND_IP:
      if (e->X_op == O_register && e->X_add_number == REG_IP)
	return OPERAND_MATCH;
      break;

    case IA64_OPND_PR:
      if (e->X_op == O_register && e->X_add_number == REG_PR)
	return OPERAND_MATCH;
      break;

    case IA64_OPND_PR_ROT:
      if (e->X_op == O_register && e->X_add_number == REG_PR_ROT)
	return OPERAND_MATCH;
      break;

    case IA64_OPND_PSR:
      if (e->X_op == O_register && e->X_add_number == REG_PSR)
	return OPERAND_MATCH;
      break;

    case IA64_OPND_PSR_L:
      if (e->X_op == O_register && e->X_add_number == REG_PSR_L)
	return OPERAND_MATCH;
      break;

    case IA64_OPND_PSR_UM:
      if (e->X_op == O_register && e->X_add_number == REG_PSR_UM)
	return OPERAND_MATCH;
      break;

    case IA64_OPND_C1:
      if (e->X_op == O_constant)
	{
	  if (e->X_add_number == 1)
	    return OPERAND_MATCH;
	  else
	    return OPERAND_OUT_OF_RANGE;
	}
      break;

    case IA64_OPND_C8:
      if (e->X_op == O_constant)
	{
	  if (e->X_add_number == 8)
	    return OPERAND_MATCH;
	  else
	    return OPERAND_OUT_OF_RANGE;
	}
      break;

    case IA64_OPND_C16:
      if (e->X_op == O_constant)
	{
	  if (e->X_add_number == 16)
	    return OPERAND_MATCH;
	  else
	    return OPERAND_OUT_OF_RANGE;
	}
      break;

      /* register operands:  */

    case IA64_OPND_AR3:
      if (e->X_op == O_register && e->X_add_number >= REG_AR
	  && e->X_add_number < REG_AR + 128)
	return OPERAND_MATCH;
      break;

    case IA64_OPND_B1:
    case IA64_OPND_B2:
      if (e->X_op == O_register && e->X_add_number >= REG_BR
	  && e->X_add_number < REG_BR + 8)
	return OPERAND_MATCH;
      break;

    case IA64_OPND_CR3:
      if (e->X_op == O_register && e->X_add_number >= REG_CR
	  && e->X_add_number < REG_CR + 128)
	return OPERAND_MATCH;
      break;

    case IA64_OPND_F1:
    case IA64_OPND_F2:
    case IA64_OPND_F3:
    case IA64_OPND_F4:
      if (e->X_op == O_register && e->X_add_number >= REG_FR
	  && e->X_add_number < REG_FR + 128)
	return OPERAND_MATCH;
      break;

    case IA64_OPND_P1:
    case IA64_OPND_P2:
      if (e->X_op == O_register && e->X_add_number >= REG_P
	  && e->X_add_number < REG_P + 64)
	return OPERAND_MATCH;
      break;

    case IA64_OPND_R1:
    case IA64_OPND_R2:
    case IA64_OPND_R3:
      if (e->X_op == O_register && e->X_add_number >= REG_GR
	  && e->X_add_number < REG_GR + 128)
	return OPERAND_MATCH;
      break;

    case IA64_OPND_R3_2:
      if (e->X_op == O_register && e->X_add_number >= REG_GR)
	{
	  if (e->X_add_number < REG_GR + 4)
	    return OPERAND_MATCH;
	  else if (e->X_add_number < REG_GR + 128)
	    return OPERAND_OUT_OF_RANGE;
	}
      break;

      /* indirect operands:  */
    case IA64_OPND_CPUID_R3:
    case IA64_OPND_DBR_R3:
    case IA64_OPND_DTR_R3:
    case IA64_OPND_ITR_R3:
    case IA64_OPND_IBR_R3:
    case IA64_OPND_MSR_R3:
    case IA64_OPND_PKR_R3:
    case IA64_OPND_PMC_R3:
    case IA64_OPND_PMD_R3:
    case IA64_OPND_RR_R3:
      if (e->X_op == O_index && e->X_op_symbol
	  && (S_GET_VALUE (e->X_op_symbol) - IND_CPUID
	      == opnd - IA64_OPND_CPUID_R3))
	return OPERAND_MATCH;
      break;

    case IA64_OPND_MR3:
      if (e->X_op == O_index && !e->X_op_symbol)
	return OPERAND_MATCH;
      break;

      /* immediate operands:  */
    case IA64_OPND_CNT2a:
    case IA64_OPND_LEN4:
    case IA64_OPND_LEN6:
      bits = operand_width (idesc->operands[index]);
      if (e->X_op == O_constant)
	{
	  if ((bfd_vma) (e->X_add_number - 1) < ((bfd_vma) 1 << bits))
	    return OPERAND_MATCH;
	  else
	    return OPERAND_OUT_OF_RANGE;
	}
      break;

    case IA64_OPND_CNT2b:
      if (e->X_op == O_constant)
	{
	  if ((bfd_vma) (e->X_add_number - 1) < 3)
	    return OPERAND_MATCH;
	  else
	    return OPERAND_OUT_OF_RANGE;
	}
      break;

    case IA64_OPND_CNT2c:
      val = e->X_add_number;
      if (e->X_op == O_constant)
	{
	  if ((val == 0 || val == 7 || val == 15 || val == 16))
	    return OPERAND_MATCH;
	  else
	    return OPERAND_OUT_OF_RANGE;
	}
      break;

    case IA64_OPND_SOR:
      /* SOR must be an integer multiple of 8 */
      if (e->X_op == O_constant && e->X_add_number & 0x7)
	return OPERAND_OUT_OF_RANGE;
    case IA64_OPND_SOF:
    case IA64_OPND_SOL:
      if (e->X_op == O_constant)
	{
	  if ((bfd_vma) e->X_add_number <= 96)
	    return OPERAND_MATCH;
	  else
	    return OPERAND_OUT_OF_RANGE;
	}
      break;

    case IA64_OPND_IMMU62:
      if (e->X_op == O_constant)
	{
	  if ((bfd_vma) e->X_add_number < ((bfd_vma) 1 << 62))
	    return OPERAND_MATCH;
	  else
	    return OPERAND_OUT_OF_RANGE;
	}
      else
	{
	  /* FIXME -- need 62-bit relocation type */
	  as_bad (_("62-bit relocation not yet implemented"));
	}
      break;

    case IA64_OPND_IMMU64:
      if (e->X_op == O_symbol || e->X_op == O_pseudo_fixup
	  || e->X_op == O_subtract)
	{
	  fix = CURR_SLOT.fixup + CURR_SLOT.num_fixups;
	  fix->code = BFD_RELOC_IA64_IMM64;
	  if (e->X_op != O_subtract)
	    {
	      fix->code = ia64_gen_real_reloc_type (e->X_op_symbol, fix->code);
	      if (e->X_op == O_pseudo_fixup)
		e->X_op = O_symbol;
	    }

	  fix->opnd = idesc->operands[index];
	  fix->expr = *e;
	  fix->is_pcrel = 0;
	  ++CURR_SLOT.num_fixups;
	  return OPERAND_MATCH;
	}
      else if (e->X_op == O_constant)
	return OPERAND_MATCH;
      break;

    case IA64_OPND_IMMU5b:
      if (e->X_op == O_constant)
	{
	  val = e->X_add_number;
	  if (val >= 32 && val <= 63)
	    return OPERAND_MATCH;
	  else
	    return OPERAND_OUT_OF_RANGE;
	}
      break;

    case IA64_OPND_CCNT5:
    case IA64_OPND_CNT5:
    case IA64_OPND_CNT6:
    case IA64_OPND_CPOS6a:
    case IA64_OPND_CPOS6b:
    case IA64_OPND_CPOS6c:
    case IA64_OPND_IMMU2:
    case IA64_OPND_IMMU7a:
    case IA64_OPND_IMMU7b:
    case IA64_OPND_IMMU21:
    case IA64_OPND_IMMU24:
    case IA64_OPND_MBTYPE4:
    case IA64_OPND_MHTYPE8:
    case IA64_OPND_POS6:
      bits = operand_width (idesc->operands[index]);
      if (e->X_op == O_constant)
	{
	  if ((bfd_vma) e->X_add_number < ((bfd_vma) 1 << bits))
	    return OPERAND_MATCH;
	  else
	    return OPERAND_OUT_OF_RANGE;
	}
      break;

    case IA64_OPND_IMMU9:
      bits = operand_width (idesc->operands[index]);
      if (e->X_op == O_constant)
	{
	  if ((bfd_vma) e->X_add_number < ((bfd_vma) 1 << bits))
	    {
	      int lobits = e->X_add_number & 0x3;
	      if (((bfd_vma) e->X_add_number & 0x3C) != 0 && lobits == 0)
		e->X_add_number |= (bfd_vma) 0x3;
	      return OPERAND_MATCH;
	    }
	  else
	    return OPERAND_OUT_OF_RANGE;
	}
      break;

    case IA64_OPND_IMM44:
      /* least 16 bits must be zero */
      if ((e->X_add_number & 0xffff) != 0)
	/* XXX technically, this is wrong: we should not be issuing warning
	   messages until we're sure this instruction pattern is going to
	   be used! */
	as_warn (_("lower 16 bits of mask ignored"));

      if (e->X_op == O_constant)
	{
	  if (((e->X_add_number >= 0
		&& (bfd_vma) e->X_add_number < ((bfd_vma) 1 << 44))
	       || (e->X_add_number < 0
		   && (bfd_vma) -e->X_add_number <= ((bfd_vma) 1 << 44))))
	    {
	      /* sign-extend */
	      if (e->X_add_number >= 0
		  && (e->X_add_number & ((bfd_vma) 1 << 43)) != 0)
		{
		  e->X_add_number |= ~(((bfd_vma) 1 << 44) - 1);
		}
	      return OPERAND_MATCH;
	    }
	  else
	    return OPERAND_OUT_OF_RANGE;
	}
      break;

    case IA64_OPND_IMM17:
      /* bit 0 is a don't care (pr0 is hardwired to 1) */
      if (e->X_op == O_constant)
	{
	  if (((e->X_add_number >= 0
		&& (bfd_vma) e->X_add_number < ((bfd_vma) 1 << 17))
	       || (e->X_add_number < 0
		   && (bfd_vma) -e->X_add_number <= ((bfd_vma) 1 << 17))))
	    {
	      /* sign-extend */
	      if (e->X_add_number >= 0
		  && (e->X_add_number & ((bfd_vma) 1 << 16)) != 0)
		{
		  e->X_add_number |= ~(((bfd_vma) 1 << 17) - 1);
		}
	      return OPERAND_MATCH;
	    }
	  else
	    return OPERAND_OUT_OF_RANGE;
	}
      break;

    case IA64_OPND_IMM14:
    case IA64_OPND_IMM22:
      relocatable = 1;
    case IA64_OPND_IMM1:
    case IA64_OPND_IMM8:
    case IA64_OPND_IMM8U4:
    case IA64_OPND_IMM8M1:
    case IA64_OPND_IMM8M1U4:
    case IA64_OPND_IMM8M1U8:
    case IA64_OPND_IMM9a:
    case IA64_OPND_IMM9b:
      bits = operand_width (idesc->operands[index]);
      if (relocatable && (e->X_op == O_symbol
			  || e->X_op == O_subtract
			  || e->X_op == O_pseudo_fixup))
	{
	  fix = CURR_SLOT.fixup + CURR_SLOT.num_fixups;

	  if (idesc->operands[index] == IA64_OPND_IMM14)
	    fix->code = BFD_RELOC_IA64_IMM14;
	  else
	    fix->code = BFD_RELOC_IA64_IMM22;

	  if (e->X_op != O_subtract)
	    {
	      fix->code = ia64_gen_real_reloc_type (e->X_op_symbol, fix->code);
	      if (e->X_op == O_pseudo_fixup)
		e->X_op = O_symbol;
	    }

	  fix->opnd = idesc->operands[index];
	  fix->expr = *e;
	  fix->is_pcrel = 0;
	  ++CURR_SLOT.num_fixups;
	  return OPERAND_MATCH;
	}
      else if (e->X_op != O_constant
	       && ! (e->X_op == O_big && opnd == IA64_OPND_IMM8M1U8))
	return OPERAND_MISMATCH;

      if (opnd == IA64_OPND_IMM8M1U4)
	{
	  /* Zero is not valid for unsigned compares that take an adjusted
	     constant immediate range.  */
	  if (e->X_add_number == 0)
	    return OPERAND_OUT_OF_RANGE;

	  /* Sign-extend 32-bit unsigned numbers, so that the following range
	     checks will work.  */
	  val = e->X_add_number;
	  if (((val & (~(bfd_vma) 0 << 32)) == 0)
	      && ((val & ((bfd_vma) 1 << 31)) != 0))
	    val = ((val << 32) >> 32);

	  /* Check for 0x100000000.  This is valid because
	     0x100000000-1 is the same as ((uint32_t) -1).  */
	  if (val == ((bfd_signed_vma) 1 << 32))
	    return OPERAND_MATCH;

	  val = val - 1;
	}
      else if (opnd == IA64_OPND_IMM8M1U8)
	{
	  /* Zero is not valid for unsigned compares that take an adjusted
	     constant immediate range.  */
	  if (e->X_add_number == 0)
	    return OPERAND_OUT_OF_RANGE;

	  /* Check for 0x10000000000000000.  */
	  if (e->X_op == O_big)
	    {
	      if (generic_bignum[0] == 0
		  && generic_bignum[1] == 0
		  && generic_bignum[2] == 0
		  && generic_bignum[3] == 0
		  && generic_bignum[4] == 1)
		return OPERAND_MATCH;
	      else
		return OPERAND_OUT_OF_RANGE;
	    }
	  else
	    val = e->X_add_number - 1;
	}
      else if (opnd == IA64_OPND_IMM8M1)
	val = e->X_add_number - 1;
      else if (opnd == IA64_OPND_IMM8U4)
	{
	  /* Sign-extend 32-bit unsigned numbers, so that the following range
	     checks will work.  */
	  val = e->X_add_number;
	  if (((val & (~(bfd_vma) 0 << 32)) == 0)
	      && ((val & ((bfd_vma) 1 << 31)) != 0))
	    val = ((val << 32) >> 32);
	}
      else
	val = e->X_add_number;

      if ((val >= 0 && (bfd_vma) val < ((bfd_vma) 1 << (bits - 1)))
	  || (val < 0 && (bfd_vma) -val <= ((bfd_vma) 1 << (bits - 1))))
	return OPERAND_MATCH;
      else
	return OPERAND_OUT_OF_RANGE;

    case IA64_OPND_INC3:
      /* +/- 1, 4, 8, 16 */
      val = e->X_add_number;
      if (val < 0)
	val = -val;
      if (e->X_op == O_constant)
	{
	  if ((val == 1 || val == 4 || val == 8 || val == 16))
	    return OPERAND_MATCH;
	  else
	    return OPERAND_OUT_OF_RANGE;
	}
      break;

    case IA64_OPND_TGT25:
    case IA64_OPND_TGT25b:
    case IA64_OPND_TGT25c:
    case IA64_OPND_TGT64:
      if (e->X_op == O_symbol)
	{
	  fix = CURR_SLOT.fixup + CURR_SLOT.num_fixups;
	  if (opnd == IA64_OPND_TGT25)
	    fix->code = BFD_RELOC_IA64_PCREL21F;
	  else if (opnd == IA64_OPND_TGT25b)
	    fix->code = BFD_RELOC_IA64_PCREL21M;
	  else if (opnd == IA64_OPND_TGT25c)
	    fix->code = BFD_RELOC_IA64_PCREL21B;
	  else if (opnd == IA64_OPND_TGT64)
	    fix->code = BFD_RELOC_IA64_PCREL60B;
	  else
	    abort ();

	  fix->code = ia64_gen_real_reloc_type (e->X_op_symbol, fix->code);
	  fix->opnd = idesc->operands[index];
	  fix->expr = *e;
	  fix->is_pcrel = 1;
	  ++CURR_SLOT.num_fixups;
	  return OPERAND_MATCH;
	}
    case IA64_OPND_TAG13:
    case IA64_OPND_TAG13b:
      switch (e->X_op)
	{
	case O_constant:
	  return OPERAND_MATCH;

	case O_symbol:
	  fix = CURR_SLOT.fixup + CURR_SLOT.num_fixups;
	  /* There are no external relocs for TAG13/TAG13b fields, so we
	     create a dummy reloc.  This will not live past md_apply_fix.  */
	  fix->code = BFD_RELOC_UNUSED;
	  fix->code = ia64_gen_real_reloc_type (e->X_op_symbol, fix->code);
	  fix->opnd = idesc->operands[index];
	  fix->expr = *e;
	  fix->is_pcrel = 1;
	  ++CURR_SLOT.num_fixups;
	  return OPERAND_MATCH;

	default:
	  break;
	}
      break;

    case IA64_OPND_LDXMOV:
      fix = CURR_SLOT.fixup + CURR_SLOT.num_fixups;
      fix->code = BFD_RELOC_IA64_LDXMOV;
      fix->opnd = idesc->operands[index];
      fix->expr = *e;
      fix->is_pcrel = 0;
      ++CURR_SLOT.num_fixups;
      return OPERAND_MATCH;

    default:
      break;
    }
  return OPERAND_MISMATCH;
}

static int
parse_operand (e, more)
     expressionS *e;
     int more;
{
  int sep = '\0';

  memset (e, 0, sizeof (*e));
  e->X_op = O_absent;
  SKIP_WHITESPACE ();
  expression_and_evaluate (e);
  sep = *input_line_pointer;
  if (more && (sep == ',' || sep == more))
    ++input_line_pointer;
  return sep;
}

/* Returns the next entry in the opcode table that matches the one in
   IDESC, and frees the entry in IDESC.  If no matching entry is
   found, NULL is returned instead.  */

static struct ia64_opcode *
get_next_opcode (struct ia64_opcode *idesc)
{
  struct ia64_opcode *next = ia64_find_next_opcode (idesc);
  ia64_free_opcode (idesc);
  return next;
}

/* Parse the operands for the opcode and find the opcode variant that
   matches the specified operands, or NULL if no match is possible.  */

static struct ia64_opcode *
parse_operands (idesc)
     struct ia64_opcode *idesc;
{
  int i = 0, highest_unmatched_operand, num_operands = 0, num_outputs = 0;
  int error_pos, out_of_range_pos, curr_out_of_range_pos, sep = 0;
  int reg1, reg2;
  char reg_class;
  enum ia64_opnd expected_operand = IA64_OPND_NIL;
  enum operand_match_result result;
  char mnemonic[129];
  char *first_arg = 0, *end, *saved_input_pointer;
  unsigned int sof;

  assert (strlen (idesc->name) <= 128);

  strcpy (mnemonic, idesc->name);
  if (idesc->operands[2] == IA64_OPND_SOF
      || idesc->operands[1] == IA64_OPND_SOF)
    {
      /* To make the common idiom "alloc loc?=ar.pfs,0,1,0,0" work, we
	 can't parse the first operand until we have parsed the
	 remaining operands of the "alloc" instruction.  */
      SKIP_WHITESPACE ();
      first_arg = input_line_pointer;
      end = strchr (input_line_pointer, '=');
      if (!end)
	{
	  as_bad ("Expected separator `='");
	  return 0;
	}
      input_line_pointer = end + 1;
      ++i;
      ++num_outputs;
    }

  for (; ; ++i)
    {
      if (i < NELEMS (CURR_SLOT.opnd)) 
	{
	  sep = parse_operand (CURR_SLOT.opnd + i, '=');
	  if (CURR_SLOT.opnd[i].X_op == O_absent)
	    break;
	}
      else
	{
	  expressionS dummy;

	  sep = parse_operand (&dummy, '=');
	  if (dummy.X_op == O_absent)
	    break;
	}

      ++num_operands;

      if (sep != '=' && sep != ',')
	break;

      if (sep == '=')
	{
	  if (num_outputs > 0)
	    as_bad ("Duplicate equal sign (=) in instruction");
	  else
	    num_outputs = i + 1;
	}
    }
  if (sep != '\0')
    {
      as_bad ("Illegal operand separator `%c'", sep);
      return 0;
    }

  if (idesc->operands[2] == IA64_OPND_SOF
      || idesc->operands[1] == IA64_OPND_SOF)
    {
      /* Map alloc r1=ar.pfs,i,l,o,r to alloc r1=ar.pfs,(i+l+o),(i+l),r.
	 Note, however, that due to that mapping operand numbers in error
	 messages for any of the constant operands will not be correct.  */
      know (strcmp (idesc->name, "alloc") == 0);
      /* The first operand hasn't been parsed/initialized, yet (but
	 num_operands intentionally doesn't account for that).  */
      i = num_operands > 4 ? 2 : 1;
#define FORCE_CONST(n) (CURR_SLOT.opnd[n].X_op == O_constant \
			? CURR_SLOT.opnd[n].X_add_number \
			: 0)
      sof = set_regstack (FORCE_CONST(i),
			  FORCE_CONST(i + 1),
			  FORCE_CONST(i + 2),
			  FORCE_CONST(i + 3));
#undef FORCE_CONST

      /* now we can parse the first arg:  */
      saved_input_pointer = input_line_pointer;
      input_line_pointer = first_arg;
      sep = parse_operand (CURR_SLOT.opnd + 0, '=');
      if (sep != '=')
	--num_outputs;	/* force error */
      input_line_pointer = saved_input_pointer;

      CURR_SLOT.opnd[i].X_add_number = sof;
      if (CURR_SLOT.opnd[i + 1].X_op == O_constant
	  && CURR_SLOT.opnd[i + 2].X_op == O_constant)
	CURR_SLOT.opnd[i + 1].X_add_number
	  = sof - CURR_SLOT.opnd[i + 2].X_add_number;
      else
	CURR_SLOT.opnd[i + 1].X_op = O_illegal;
      CURR_SLOT.opnd[i + 2] = CURR_SLOT.opnd[i + 3];
    }

  highest_unmatched_operand = -4;
  curr_out_of_range_pos = -1;
  error_pos = 0;
  for (; idesc; idesc = get_next_opcode (idesc))
    {
      if (num_outputs != idesc->num_outputs)
	continue;		/* mismatch in # of outputs */
      if (highest_unmatched_operand < 0)
	highest_unmatched_operand |= 1;
      if (num_operands > NELEMS (idesc->operands)
	  || (num_operands < NELEMS (idesc->operands)
	   && idesc->operands[num_operands])
	  || (num_operands > 0 && !idesc->operands[num_operands - 1]))
	continue;		/* mismatch in number of arguments */
      if (highest_unmatched_operand < 0)
	highest_unmatched_operand |= 2;

      CURR_SLOT.num_fixups = 0;

      /* Try to match all operands.  If we see an out-of-range operand,
	 then continue trying to match the rest of the operands, since if
	 the rest match, then this idesc will give the best error message.  */

      out_of_range_pos = -1;
      for (i = 0; i < num_operands && idesc->operands[i]; ++i)
	{
	  result = operand_match (idesc, i, CURR_SLOT.opnd + i);
	  if (result != OPERAND_MATCH)
	    {
	      if (result != OPERAND_OUT_OF_RANGE)
		break;
	      if (out_of_range_pos < 0)
		/* remember position of the first out-of-range operand: */
		out_of_range_pos = i;
	    }
	}

      /* If we did not match all operands, or if at least one operand was
	 out-of-range, then this idesc does not match.  Keep track of which
	 idesc matched the most operands before failing.  If we have two
	 idescs that failed at the same position, and one had an out-of-range
	 operand, then prefer the out-of-range operand.  Thus if we have
	 "add r0=0x1000000,r1" we get an error saying the constant is out
	 of range instead of an error saying that the constant should have been
	 a register.  */

      if (i != num_operands || out_of_range_pos >= 0)
	{
	  if (i > highest_unmatched_operand
	      || (i == highest_unmatched_operand
		  && out_of_range_pos > curr_out_of_range_pos))
	    {
	      highest_unmatched_operand = i;
	      if (out_of_range_pos >= 0)
		{
		  expected_operand = idesc->operands[out_of_range_pos];
		  error_pos = out_of_range_pos;
		}
	      else
		{
		  expected_operand = idesc->operands[i];
		  error_pos = i;
		}
	      curr_out_of_range_pos = out_of_range_pos;
	    }
	  continue;
	}

      break;
    }
  if (!idesc)
    {
      if (expected_operand)
	as_bad ("Operand %u of `%s' should be %s",
		error_pos + 1, mnemonic,
		elf64_ia64_operands[expected_operand].desc);
      else if (highest_unmatched_operand < 0 && !(highest_unmatched_operand & 1))
	as_bad ("Wrong number of output operands");
      else if (highest_unmatched_operand < 0 && !(highest_unmatched_operand & 2))
	as_bad ("Wrong number of input operands");
      else
	as_bad ("Operand mismatch");
      return 0;
    }

  /* Check that the instruction doesn't use
     - r0, f0, or f1 as output operands
     - the same predicate twice as output operands
     - r0 as address of a base update load or store
     - the same GR as output and address of a base update load
     - two even- or two odd-numbered FRs as output operands of a floating
       point parallel load.
     At most two (conflicting) output (or output-like) operands can exist,
     (floating point parallel loads have three outputs, but the base register,
     if updated, cannot conflict with the actual outputs).  */
  reg2 = reg1 = -1;
  for (i = 0; i < num_operands; ++i)
    {
      int regno = 0;

      reg_class = 0;
      switch (idesc->operands[i])
	{
	case IA64_OPND_R1:
	case IA64_OPND_R2:
	case IA64_OPND_R3:
	  if (i < num_outputs)
	    {
	      if (CURR_SLOT.opnd[i].X_add_number == REG_GR)
		reg_class = 'r';
	      else if (reg1 < 0)
		reg1 = CURR_SLOT.opnd[i].X_add_number;
	      else if (reg2 < 0)
		reg2 = CURR_SLOT.opnd[i].X_add_number;
	    }
	  break;
	case IA64_OPND_P1:
	case IA64_OPND_P2:
	  if (i < num_outputs)
	    {
	      if (reg1 < 0)
		reg1 = CURR_SLOT.opnd[i].X_add_number;
	      else if (reg2 < 0)
		reg2 = CURR_SLOT.opnd[i].X_add_number;
	    }
	  break;
	case IA64_OPND_F1:
	case IA64_OPND_F2:
	case IA64_OPND_F3:
	case IA64_OPND_F4:
	  if (i < num_outputs)
	    {
	      if (CURR_SLOT.opnd[i].X_add_number >= REG_FR
		  && CURR_SLOT.opnd[i].X_add_number <= REG_FR + 1)
		{
		  reg_class = 'f';
		  regno = CURR_SLOT.opnd[i].X_add_number - REG_FR;
		}
	      else if (reg1 < 0)
		reg1 = CURR_SLOT.opnd[i].X_add_number;
	      else if (reg2 < 0)
		reg2 = CURR_SLOT.opnd[i].X_add_number;
	    }
	  break;
	case IA64_OPND_MR3:
	  if (idesc->flags & IA64_OPCODE_POSTINC)
	    {
	      if (CURR_SLOT.opnd[i].X_add_number == REG_GR)
		reg_class = 'm';
	      else if (reg1 < 0)
		reg1 = CURR_SLOT.opnd[i].X_add_number;
	      else if (reg2 < 0)
		reg2 = CURR_SLOT.opnd[i].X_add_number;
	    }
	  break;
	default:
	  break;
	}
      switch (reg_class)
	{
	case 0:
	  break;
	default:
	  as_warn ("Invalid use of `%c%d' as output operand", reg_class, regno);
	  break;
	case 'm':
	  as_warn ("Invalid use of `r%d' as base update address operand", regno);
	  break;
	}
    }
  if (reg1 == reg2)
    {
      if (reg1 >= REG_GR && reg1 <= REG_GR + 127)
	{
	  reg1 -= REG_GR;
	  reg_class = 'r';
	}
      else if (reg1 >= REG_P && reg1 <= REG_P + 63)
	{
	  reg1 -= REG_P;
	  reg_class = 'p';
	}
      else if (reg1 >= REG_FR && reg1 <= REG_FR + 127)
	{
	  reg1 -= REG_FR;
	  reg_class = 'f';
	}
      else
	reg_class = 0;
      if (reg_class)
	as_warn ("Invalid duplicate use of `%c%d'", reg_class, reg1);
    }
  else if (((reg1 >= REG_FR && reg1 <= REG_FR + 31
	     && reg2 >= REG_FR && reg2 <= REG_FR + 31)
	    || (reg1 >= REG_FR + 32 && reg1 <= REG_FR + 127
	     && reg2 >= REG_FR + 32 && reg2 <= REG_FR + 127))
	   && ! ((reg1 ^ reg2) & 1))
    as_warn ("Invalid simultaneous use of `f%d' and `f%d'",
	     reg1 - REG_FR, reg2 - REG_FR);
  else if ((reg1 >= REG_FR && reg1 <= REG_FR + 31
	    && reg2 >= REG_FR + 32 && reg2 <= REG_FR + 127)
	   || (reg1 >= REG_FR + 32 && reg1 <= REG_FR + 127
	    && reg2 >= REG_FR && reg2 <= REG_FR + 31))
    as_warn ("Dangerous simultaneous use of `f%d' and `f%d'",
	     reg1 - REG_FR, reg2 - REG_FR);
  return idesc;
}

static void
build_insn (slot, insnp)
     struct slot *slot;
     bfd_vma *insnp;
{
  const struct ia64_operand *odesc, *o2desc;
  struct ia64_opcode *idesc = slot->idesc;
  bfd_vma insn;
  bfd_signed_vma val;
  const char *err;
  int i;

  insn = idesc->opcode | slot->qp_regno;

  for (i = 0; i < NELEMS (idesc->operands) && idesc->operands[i]; ++i)
    {
      if (slot->opnd[i].X_op == O_register
	  || slot->opnd[i].X_op == O_constant
	  || slot->opnd[i].X_op == O_index)
	val = slot->opnd[i].X_add_number;
      else if (slot->opnd[i].X_op == O_big)
	{
	  /* This must be the value 0x10000000000000000.  */
	  assert (idesc->operands[i] == IA64_OPND_IMM8M1U8);
	  val = 0;
	}
      else
	val = 0;

      switch (idesc->operands[i])
	{
	case IA64_OPND_IMMU64:
	  *insnp++ = (val >> 22) & 0x1ffffffffffLL;
	  insn |= (((val & 0x7f) << 13) | (((val >> 7) & 0x1ff) << 27)
		   | (((val >> 16) & 0x1f) << 22) | (((val >> 21) & 0x1) << 21)
		   | (((val >> 63) & 0x1) << 36));
	  continue;

	case IA64_OPND_IMMU62:
	  val &= 0x3fffffffffffffffULL;
	  if (val != slot->opnd[i].X_add_number)
	    as_warn (_("Value truncated to 62 bits"));
	  *insnp++ = (val >> 21) & 0x1ffffffffffLL;
	  insn |= (((val & 0xfffff) << 6) | (((val >> 20) & 0x1) << 36));
	  continue;

	case IA64_OPND_TGT64:
	  val >>= 4;
	  *insnp++ = ((val >> 20) & 0x7fffffffffLL) << 2;
	  insn |= ((((val >> 59) & 0x1) << 36)
		   | (((val >> 0) & 0xfffff) << 13));
	  continue;

	case IA64_OPND_AR3:
	  val -= REG_AR;
	  break;

	case IA64_OPND_B1:
	case IA64_OPND_B2:
	  val -= REG_BR;
	  break;

	case IA64_OPND_CR3:
	  val -= REG_CR;
	  break;

	case IA64_OPND_F1:
	case IA64_OPND_F2:
	case IA64_OPND_F3:
	case IA64_OPND_F4:
	  val -= REG_FR;
	  break;

	case IA64_OPND_P1:
	case IA64_OPND_P2:
	  val -= REG_P;
	  break;

	case IA64_OPND_R1:
	case IA64_OPND_R2:
	case IA64_OPND_R3:
	case IA64_OPND_R3_2:
	case IA64_OPND_CPUID_R3:
	case IA64_OPND_DBR_R3:
	case IA64_OPND_DTR_R3:
	case IA64_OPND_ITR_R3:
	case IA64_OPND_IBR_R3:
	case IA64_OPND_MR3:
	case IA64_OPND_MSR_R3:
	case IA64_OPND_PKR_R3:
	case IA64_OPND_PMC_R3:
	case IA64_OPND_PMD_R3:
	case IA64_OPND_RR_R3:
	  val -= REG_GR;
	  break;

	default:
	  break;
	}

      odesc = elf64_ia64_operands + idesc->operands[i];
      err = (*odesc->insert) (odesc, val, &insn);
      if (err)
	as_bad_where (slot->src_file, slot->src_line,
		      "Bad operand value: %s", err);
      if (idesc->flags & IA64_OPCODE_PSEUDO)
	{
	  if ((idesc->flags & IA64_OPCODE_F2_EQ_F3)
	      && odesc == elf64_ia64_operands + IA64_OPND_F3)
	    {
	      o2desc = elf64_ia64_operands + IA64_OPND_F2;
	      (*o2desc->insert) (o2desc, val, &insn);
	    }
	  if ((idesc->flags & IA64_OPCODE_LEN_EQ_64MCNT)
	      && (odesc == elf64_ia64_operands + IA64_OPND_CPOS6a
		  || odesc == elf64_ia64_operands + IA64_OPND_POS6))
	    {
	      o2desc = elf64_ia64_operands + IA64_OPND_LEN6;
	      (*o2desc->insert) (o2desc, 64 - val, &insn);
	    }
	}
    }
  *insnp = insn;
}

static void
emit_one_bundle ()
{
  int manual_bundling_off = 0, manual_bundling = 0;
  enum ia64_unit required_unit, insn_unit = 0;
  enum ia64_insn_type type[3], insn_type;
  unsigned int template, orig_template;
  bfd_vma insn[3] = { -1, -1, -1 };
  struct ia64_opcode *idesc;
  int end_of_insn_group = 0, user_template = -1;
  int n, i, j, first, curr, last_slot;
  bfd_vma t0 = 0, t1 = 0;
  struct label_fix *lfix;
  bfd_boolean mark_label;
  struct insn_fix *ifix;
  char mnemonic[16];
  fixS *fix;
  char *f;
  int addr_mod;

  first = (md.curr_slot + NUM_SLOTS - md.num_slots_in_use) % NUM_SLOTS;
  know (first >= 0 && first < NUM_SLOTS);
  n = MIN (3, md.num_slots_in_use);

  /* Determine template: user user_template if specified, best match
     otherwise:  */

  if (md.slot[first].user_template >= 0)
    user_template = template = md.slot[first].user_template;
  else
    {
      /* Auto select appropriate template.  */
      memset (type, 0, sizeof (type));
      curr = first;
      for (i = 0; i < n; ++i)
	{
	  if (md.slot[curr].label_fixups && i != 0)
	    break;
	  type[i] = md.slot[curr].idesc->type;
	  curr = (curr + 1) % NUM_SLOTS;
	}
      template = best_template[type[0]][type[1]][type[2]];
    }

  /* initialize instructions with appropriate nops:  */
  for (i = 0; i < 3; ++i)
    insn[i] = nop[ia64_templ_desc[template].exec_unit[i]];

  f = frag_more (16);

  /* Check to see if this bundle is at an offset that is a multiple of 16-bytes
     from the start of the frag.  */
  addr_mod = frag_now_fix () & 15;
  if (frag_now->has_code && frag_now->insn_addr != addr_mod)
    as_bad (_("instruction address is not a multiple of 16"));
  frag_now->insn_addr = addr_mod;
  frag_now->has_code = 1;

  /* now fill in slots with as many insns as possible:  */
  curr = first;
  idesc = md.slot[curr].idesc;
  end_of_insn_group = 0;
  last_slot = -1;
  for (i = 0; i < 3 && md.num_slots_in_use > 0; ++i)
    {
      /* If we have unwind records, we may need to update some now.  */
      unw_rec_list *ptr = md.slot[curr].unwind_record;
      unw_rec_list *end_ptr = NULL;

      if (ptr)
	{
	  /* Find the last prologue/body record in the list for the current
	     insn, and set the slot number for all records up to that point.
	     This needs to be done now, because prologue/body records refer to
	     the current point, not the point after the instruction has been
	     issued.  This matters because there may have been nops emitted
	     meanwhile.  Any non-prologue non-body record followed by a
	     prologue/body record must also refer to the current point.  */
	  unw_rec_list *last_ptr;

	  for (j = 1; end_ptr == NULL && j < md.num_slots_in_use; ++j)
	    end_ptr = md.slot[(curr + j) % NUM_SLOTS].unwind_record;
	  for (last_ptr = NULL; ptr != end_ptr; ptr = ptr->next)
	    if (ptr->r.type == prologue || ptr->r.type == prologue_gr
		|| ptr->r.type == body)
	      last_ptr = ptr;
	  if (last_ptr)
	    {
	      /* Make last_ptr point one after the last prologue/body
		 record.  */
	      last_ptr = last_ptr->next;
	      for (ptr = md.slot[curr].unwind_record; ptr != last_ptr;
		   ptr = ptr->next)
		{
		  ptr->slot_number = (unsigned long) f + i;
		  ptr->slot_frag = frag_now;
		}
	      /* Remove the initialized records, so that we won't accidentally
		 update them again if we insert a nop and continue.  */
	      md.slot[curr].unwind_record = last_ptr;
	    }
	}

      manual_bundling_off = md.slot[curr].manual_bundling_off;
      if (md.slot[curr].manual_bundling_on)
	{
	  if (curr == first)
	    manual_bundling = 1;
	  else
	  break; /* Need to start a new bundle.  */
	}

      /* If this instruction specifies a template, then it must be the first
	 instruction of a bundle.  */
      if (curr != first && md.slot[curr].user_template >= 0)
	break;

      if (idesc->flags & IA64_OPCODE_SLOT2)
	{
	  if (manual_bundling && !manual_bundling_off)
	    {
	      as_bad_where (md.slot[curr].src_file, md.slot[curr].src_line,
			    "`%s' must be last in bundle", idesc->name);
	      if (i < 2)
		manual_bundling = -1; /* Suppress meaningless post-loop errors.  */
	    }
	  i = 2;
	}
      if (idesc->flags & IA64_OPCODE_LAST)
	{
	  int required_slot;
	  unsigned int required_template;

	  /* If we need a stop bit after an M slot, our only choice is
	     template 5 (M;;MI).  If we need a stop bit after a B
	     slot, our only choice is to place it at the end of the
	     bundle, because the only available templates are MIB,
	     MBB, BBB, MMB, and MFB.  We don't handle anything other
	     than M and B slots because these are the only kind of
	     instructions that can have the IA64_OPCODE_LAST bit set.  */
	  required_template = template;
	  switch (idesc->type)
	    {
	    case IA64_TYPE_M:
	      required_slot = 0;
	      required_template = 5;
	      break;

	    case IA64_TYPE_B:
	      required_slot = 2;
	      break;

	    default:
	      as_bad_where (md.slot[curr].src_file, md.slot[curr].src_line,
			    "Internal error: don't know how to force %s to end"
			    "of instruction group", idesc->name);
	      required_slot = i;
	      break;
	    }
	  if (manual_bundling
	      && (i > required_slot
		  || (required_slot == 2 && !manual_bundling_off)
		  || (user_template >= 0
		      /* Changing from MMI to M;MI is OK.  */
		      && (template ^ required_template) > 1)))
	    {
	      as_bad_where (md.slot[curr].src_file, md.slot[curr].src_line,
			    "`%s' must be last in instruction group",
			    idesc->name);
	      if (i < 2 && required_slot == 2 && !manual_bundling_off)
		manual_bundling = -1; /* Suppress meaningless post-loop errors.  */
	    }
	  if (required_slot < i)
	    /* Can't fit this instruction.  */
	    break;

	  i = required_slot;
	  if (required_template != template)
	    {
	      /* If we switch the template, we need to reset the NOPs
	         after slot i.  The slot-types of the instructions ahead
	         of i never change, so we don't need to worry about
	         changing NOPs in front of this slot.  */
	      for (j = i; j < 3; ++j)
	        insn[j] = nop[ia64_templ_desc[required_template].exec_unit[j]];

	      /* We just picked a template that includes the stop bit in the
		 middle, so we don't need another one emitted later.  */
	      md.slot[curr].end_of_insn_group = 0;
	    }
	  template = required_template;
	}
      if (curr != first && md.slot[curr].label_fixups)
	{
	  if (manual_bundling)
	    {
	      as_bad_where (md.slot[curr].src_file, md.slot[curr].src_line,
			  "Label must be first in a bundle");
	      manual_bundling = -1; /* Suppress meaningless post-loop errors.  */
	    }
	  /* This insn must go into the first slot of a bundle.  */
	  break;
	}

      if (end_of_insn_group && md.num_slots_in_use >= 1)
	{
	  /* We need an instruction group boundary in the middle of a
	     bundle.  See if we can switch to an other template with
	     an appropriate boundary.  */

	  orig_template = template;
	  if (i == 1 && (user_template == 4
			 || (user_template < 0
			     && (ia64_templ_desc[template].exec_unit[0]
				 == IA64_UNIT_M))))
	    {
	      template = 5;
	      end_of_insn_group = 0;
	    }
	  else if (i == 2 && (user_template == 0
			      || (user_template < 0
				  && (ia64_templ_desc[template].exec_unit[1]
				      == IA64_UNIT_I)))
		   /* This test makes sure we don't switch the template if
		      the next instruction is one that needs to be first in
		      an instruction group.  Since all those instructions are
		      in the M group, there is no way such an instruction can
		      fit in this bundle even if we switch the template.  The
		      reason we have to check for this is that otherwise we
		      may end up generating "MI;;I M.." which has the deadly
		      effect that the second M instruction is no longer the
		      first in the group! --davidm 99/12/16  */
		   && (idesc->flags & IA64_OPCODE_FIRST) == 0)
	    {
	      template = 1;
	      end_of_insn_group = 0;
	    }
	  else if (i == 1
		   && user_template == 0
		   && !(idesc->flags & IA64_OPCODE_FIRST))
	    /* Use the next slot.  */
	    continue;
	  else if (curr != first)
	    /* can't fit this insn */
	    break;

	  if (template != orig_template)
	    /* if we switch the template, we need to reset the NOPs
	       after slot i.  The slot-types of the instructions ahead
	       of i never change, so we don't need to worry about
	       changing NOPs in front of this slot.  */
	    for (j = i; j < 3; ++j)
	      insn[j] = nop[ia64_templ_desc[template].exec_unit[j]];
	}
      required_unit = ia64_templ_desc[template].exec_unit[i];

      /* resolve dynamic opcodes such as "break", "hint", and "nop":  */
      if (idesc->type == IA64_TYPE_DYN)
	{
	  enum ia64_opnd opnd1, opnd2;

	  if ((strcmp (idesc->name, "nop") == 0)
	      || (strcmp (idesc->name, "break") == 0))
	    insn_unit = required_unit;
	  else if (strcmp (idesc->name, "hint") == 0)
	    {
	      insn_unit = required_unit;
	      if (required_unit == IA64_UNIT_B)
		{
		  switch (md.hint_b)
		    {
		    case hint_b_ok:
		      break;
		    case hint_b_warning:
		      as_warn ("hint in B unit may be treated as nop");
		      break;
		    case hint_b_error:
		      /* When manual bundling is off and there is no
			 user template, we choose a different unit so
			 that hint won't go into the current slot. We
			 will fill the current bundle with nops and
			 try to put hint into the next bundle.  */
		      if (!manual_bundling && user_template < 0)
			insn_unit = IA64_UNIT_I;
		      else
			as_bad ("hint in B unit can't be used");
		      break;
		    }
		}
	    }
	  else if (strcmp (idesc->name, "chk.s") == 0
	      || strcmp (idesc->name, "mov") == 0)
	    {
	      insn_unit = IA64_UNIT_M;
	      if (required_unit == IA64_UNIT_I
		  || (required_unit == IA64_UNIT_F && template == 6))
		insn_unit = IA64_UNIT_I;
	    }
	  else
	    as_fatal ("emit_one_bundle: unexpected dynamic op");

	  snprintf (mnemonic, sizeof (mnemonic), "%s.%c",
		    idesc->name, "?imbfxx"[insn_unit]);
	  opnd1 = idesc->operands[0];
	  opnd2 = idesc->operands[1];
	  ia64_free_opcode (idesc);
	  idesc = ia64_find_opcode (mnemonic);
	  /* moves to/from ARs have collisions */
	  if (opnd1 == IA64_OPND_AR3 || opnd2 == IA64_OPND_AR3)
	    {
	      while (idesc != NULL
		     && (idesc->operands[0] != opnd1
			 || idesc->operands[1] != opnd2))
		idesc = get_next_opcode (idesc);
	    }
	  md.slot[curr].idesc = idesc;
	}
      else
	{
	  insn_type = idesc->type;
	  insn_unit = IA64_UNIT_NIL;
	  switch (insn_type)
	    {
	    case IA64_TYPE_A:
	      if (required_unit == IA64_UNIT_I || required_unit == IA64_UNIT_M)
		insn_unit = required_unit;
	      break;
	    case IA64_TYPE_X: insn_unit = IA64_UNIT_L; break;
	    case IA64_TYPE_I: insn_unit = IA64_UNIT_I; break;
	    case IA64_TYPE_M: insn_unit = IA64_UNIT_M; break;
	    case IA64_TYPE_B: insn_unit = IA64_UNIT_B; break;
	    case IA64_TYPE_F: insn_unit = IA64_UNIT_F; break;
	    default:				       break;
	    }
	}

      if (insn_unit != required_unit)
	continue;		/* Try next slot.  */

      /* Now is a good time to fix up the labels for this insn.  */
      mark_label = FALSE;
      for (lfix = md.slot[curr].label_fixups; lfix; lfix = lfix->next)
	{
	  S_SET_VALUE (lfix->sym, frag_now_fix () - 16);
	  symbol_set_frag (lfix->sym, frag_now);
	  mark_label |= lfix->dw2_mark_labels;
	}
      for (lfix = md.slot[curr].tag_fixups; lfix; lfix = lfix->next)
	{
	  S_SET_VALUE (lfix->sym, frag_now_fix () - 16 + i);
	  symbol_set_frag (lfix->sym, frag_now);
	}

      if (debug_type == DEBUG_DWARF2
	  || md.slot[curr].loc_directive_seen
	  || mark_label)
	{
	  bfd_vma addr = frag_now->fr_address + frag_now_fix () - 16 + i;

	  md.slot[curr].loc_directive_seen = 0;
	  if (mark_label)
	    md.slot[curr].debug_line.flags |= DWARF2_FLAG_BASIC_BLOCK;

	  dwarf2_gen_line_info (addr, &md.slot[curr].debug_line);
	}

      build_insn (md.slot + curr, insn + i);

      ptr = md.slot[curr].unwind_record;
      if (ptr)
	{
	  /* Set slot numbers for all remaining unwind records belonging to the
	     current insn.  There can not be any prologue/body unwind records
	     here.  */
	  for (; ptr != end_ptr; ptr = ptr->next)
	    {
	      ptr->slot_number = (unsigned long) f + i;
	      ptr->slot_frag = frag_now;
	    }
	  md.slot[curr].unwind_record = NULL;
	}

      if (required_unit == IA64_UNIT_L)
	{
	  know (i == 1);
	  /* skip one slot for long/X-unit instructions */
	  ++i;
	}
      --md.num_slots_in_use;
      last_slot = i;

      for (j = 0; j < md.slot[curr].num_fixups; ++j)
	{
	  ifix = md.slot[curr].fixup + j;
	  fix = fix_new_exp (frag_now, frag_now_fix () - 16 + i, 8,
			     &ifix->expr, ifix->is_pcrel, ifix->code);
	  fix->tc_fix_data.opnd = ifix->opnd;
	  fix->fx_file = md.slot[curr].src_file;
	  fix->fx_line = md.slot[curr].src_line;
	}

      end_of_insn_group = md.slot[curr].end_of_insn_group;

      /* clear slot:  */
      ia64_free_opcode (md.slot[curr].idesc);
      memset (md.slot + curr, 0, sizeof (md.slot[curr]));
      md.slot[curr].user_template = -1;

      if (manual_bundling_off)
	{
	  manual_bundling = 0;
	  break;
	}
      curr = (curr + 1) % NUM_SLOTS;
      idesc = md.slot[curr].idesc;
    }

  /* A user template was specified, but the first following instruction did
     not fit.  This can happen with or without manual bundling.  */
  if (md.num_slots_in_use > 0 && last_slot < 0)
    {
      as_bad_where (md.slot[curr].src_file, md.slot[curr].src_line,
		    "`%s' does not fit into %s template",
		    idesc->name, ia64_templ_desc[template].name);
      /* Drop first insn so we don't livelock.  */
      --md.num_slots_in_use;
      know (curr == first);
      ia64_free_opcode (md.slot[curr].idesc);
      memset (md.slot + curr, 0, sizeof (md.slot[curr]));
      md.slot[curr].user_template = -1;
    }
  else if (manual_bundling > 0)
    {
      if (md.num_slots_in_use > 0)
	{
	  if (last_slot >= 2)
	    as_bad_where (md.slot[curr].src_file, md.slot[curr].src_line,
			  "`%s' does not fit into bundle", idesc->name);
	  else
	    {
	      const char *where;

	      if (template == 2)
		where = "X slot";
	      else if (last_slot == 0)
		where = "slots 2 or 3";
	      else
		where = "slot 3";
	      as_bad_where (md.slot[curr].src_file, md.slot[curr].src_line,
			    "`%s' can't go in %s of %s template",
			    idesc->name, where, ia64_templ_desc[template].name);
	    }
	}
      else
	as_bad_where (md.slot[curr].src_file, md.slot[curr].src_line,
		      "Missing '}' at end of file");
    }
	
  know (md.num_slots_in_use < NUM_SLOTS);

  t0 = end_of_insn_group | (template << 1) | (insn[0] << 5) | (insn[1] << 46);
  t1 = ((insn[1] >> 18) & 0x7fffff) | (insn[2] << 23);

  number_to_chars_littleendian (f + 0, t0, 8);
  number_to_chars_littleendian (f + 8, t1, 8);
}

int
md_parse_option (c, arg)
     int c;
     char *arg;
{

  switch (c)
    {
    /* Switches from the Intel assembler.  */
    case 'm':
      if (strcmp (arg, "ilp64") == 0
	  || strcmp (arg, "lp64") == 0
	  || strcmp (arg, "p64") == 0)
	{
	  md.flags |= EF_IA_64_ABI64;
	}
      else if (strcmp (arg, "ilp32") == 0)
	{
	  md.flags &= ~EF_IA_64_ABI64;
	}
      else if (strcmp (arg, "le") == 0)
	{
	  md.flags &= ~EF_IA_64_BE;
	  default_big_endian = 0;
	}
      else if (strcmp (arg, "be") == 0)
	{
	  md.flags |= EF_IA_64_BE;
	  default_big_endian = 1;
	}
      else if (strncmp (arg, "unwind-check=", 13) == 0)
	{
	  arg += 13;
	  if (strcmp (arg, "warning") == 0)
	    md.unwind_check = unwind_check_warning;
	  else if (strcmp (arg, "error") == 0)
	    md.unwind_check = unwind_check_error;
	  else
	    return 0;
	}
      else if (strncmp (arg, "hint.b=", 7) == 0)
	{
	  arg += 7;
	  if (strcmp (arg, "ok") == 0)
	    md.hint_b = hint_b_ok;
	  else if (strcmp (arg, "warning") == 0)
	    md.hint_b = hint_b_warning;
	  else if (strcmp (arg, "error") == 0)
	    md.hint_b = hint_b_error;
	  else
	    return 0;
	}
      else if (strncmp (arg, "tune=", 5) == 0)
	{
	  arg += 5;
	  if (strcmp (arg, "itanium1") == 0)
	    md.tune = itanium1;
	  else if (strcmp (arg, "itanium2") == 0)
	    md.tune = itanium2;
	  else
	    return 0;
	}
      else
	return 0;
      break;

    case 'N':
      if (strcmp (arg, "so") == 0)
	{
	  /* Suppress signon message.  */
	}
      else if (strcmp (arg, "pi") == 0)
	{
	  /* Reject privileged instructions.  FIXME */
	}
      else if (strcmp (arg, "us") == 0)
	{
	  /* Allow union of signed and unsigned range.  FIXME */
	}
      else if (strcmp (arg, "close_fcalls") == 0)
	{
	  /* Do not resolve global function calls.  */
	}
      else
	return 0;
      break;

    case 'C':
      /* temp[="prefix"]  Insert temporary labels into the object file
			  symbol table prefixed by "prefix".
			  Default prefix is ":temp:".
       */
      break;

    case 'a':
      /* indirect=<tgt>	Assume unannotated indirect branches behavior
			according to <tgt> --
			exit:	branch out from the current context (default)
			labels:	all labels in context may be branch targets
       */
      if (strncmp (arg, "indirect=", 9) != 0)
        return 0;
      break;

    case 'x':
      /* -X conflicts with an ignored option, use -x instead */
      md.detect_dv = 1;
      if (!arg || strcmp (arg, "explicit") == 0)
	{
	  /* set default mode to explicit */
	  md.default_explicit_mode = 1;
	  break;
	}
      else if (strcmp (arg, "auto") == 0)
	{
	  md.default_explicit_mode = 0;
	}
      else if (strcmp (arg, "none") == 0)
	{
	  md.detect_dv = 0;
	}
      else if (strcmp (arg, "debug") == 0)
	{
	  md.debug_dv = 1;
	}
      else if (strcmp (arg, "debugx") == 0)
	{
	  md.default_explicit_mode = 1;
	  md.debug_dv = 1;
	}
      else if (strcmp (arg, "debugn") == 0)
	{
	  md.debug_dv = 1;
	  md.detect_dv = 0;
	}
      else
	{
	  as_bad (_("Unrecognized option '-x%s'"), arg);
	}
      break;

    case 'S':
      /* nops		Print nops statistics.  */
      break;

    /* GNU specific switches for gcc.  */
    case OPTION_MCONSTANT_GP:
      md.flags |= EF_IA_64_CONS_GP;
      break;

    case OPTION_MAUTO_PIC:
      md.flags |= EF_IA_64_NOFUNCDESC_CONS_GP;
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
  fputs (_("\
IA-64 options:\n\
  --mconstant-gp	  mark output file as using the constant-GP model\n\
			  (sets ELF header flag EF_IA_64_CONS_GP)\n\
  --mauto-pic		  mark output file as using the constant-GP model\n\
			  without function descriptors (sets ELF header flag\n\
			  EF_IA_64_NOFUNCDESC_CONS_GP)\n\
  -milp32|-milp64|-mlp64|-mp64	select data model (default -mlp64)\n\
  -mle | -mbe		  select little- or big-endian byte order (default -mle)\n\
  -mtune=[itanium1|itanium2]\n\
			  tune for a specific CPU (default -mtune=itanium2)\n\
  -munwind-check=[warning|error]\n\
			  unwind directive check (default -munwind-check=warning)\n\
  -mhint.b=[ok|warning|error]\n\
			  hint.b check (default -mhint.b=error)\n\
  -x | -xexplicit	  turn on dependency violation checking\n\
  -xauto		  automagically remove dependency violations (default)\n\
  -xnone		  turn off dependency violation checking\n\
  -xdebug		  debug dependency violation checker\n\
  -xdebugn		  debug dependency violation checker but turn off\n\
			  dependency violation checking\n\
  -xdebugx		  debug dependency violation checker and turn on\n\
			  dependency violation checking\n"),
	stream);
}

void
ia64_after_parse_args ()
{
  if (debug_type == DEBUG_STABS)
    as_fatal (_("--gstabs is not supported for ia64"));
}

/* Return true if TYPE fits in TEMPL at SLOT.  */

static int
match (int templ, int type, int slot)
{
  enum ia64_unit unit;
  int result;

  unit = ia64_templ_desc[templ].exec_unit[slot];
  switch (type)
    {
    case IA64_TYPE_DYN: result = 1; break; /* for nop and break */
    case IA64_TYPE_A:
      result = (unit == IA64_UNIT_I || unit == IA64_UNIT_M);
      break;
    case IA64_TYPE_X:	result = (unit == IA64_UNIT_L); break;
    case IA64_TYPE_I:	result = (unit == IA64_UNIT_I); break;
    case IA64_TYPE_M:	result = (unit == IA64_UNIT_M); break;
    case IA64_TYPE_B:	result = (unit == IA64_UNIT_B); break;
    case IA64_TYPE_F:	result = (unit == IA64_UNIT_F); break;
    default:		result = 0; break;
    }
  return result;
}

/* For Itanium 1, add a bit of extra goodness if a nop of type F or B would fit
   in TEMPL at SLOT.  For Itanium 2, add a bit of extra goodness if a nop of
   type M or I would fit in TEMPL at SLOT.  */

static inline int
extra_goodness (int templ, int slot)
{
  switch (md.tune)
    {
    case itanium1:
      if (slot == 1 && match (templ, IA64_TYPE_F, slot))
	return 2;
      else if (slot == 2 && match (templ, IA64_TYPE_B, slot))
	return 1;
      else
	return 0;
      break;
    case itanium2:
      if (match (templ, IA64_TYPE_M, slot)
	  || match (templ, IA64_TYPE_I, slot))
	/* Favor M- and I-unit NOPs.  We definitely want to avoid
	   F-unit and B-unit may cause split-issue or less-than-optimal
	   branch-prediction.  */
	return 2;
      else
	return 0;
      break;
    default:
      abort ();
      return 0;
    }
}

/* This function is called once, at assembler startup time.  It sets
   up all the tables, etc. that the MD part of the assembler will need
   that can be determined before arguments are parsed.  */
void
md_begin ()
{
  int i, j, k, t, goodness, best, ok;
  const char *err;
  char name[8];

  md.auto_align = 1;
  md.explicit_mode = md.default_explicit_mode;

  bfd_set_section_alignment (stdoutput, text_section, 4);

  /* Make sure function pointers get initialized.  */
  target_big_endian = -1;
  dot_byteorder (default_big_endian);

  alias_hash = hash_new ();
  alias_name_hash = hash_new ();
  secalias_hash = hash_new ();
  secalias_name_hash = hash_new ();

  pseudo_func[FUNC_DTP_MODULE].u.sym =
    symbol_new (".<dtpmod>", undefined_section, FUNC_DTP_MODULE,
		&zero_address_frag);

  pseudo_func[FUNC_DTP_RELATIVE].u.sym =
    symbol_new (".<dtprel>", undefined_section, FUNC_DTP_RELATIVE,
		&zero_address_frag);

  pseudo_func[FUNC_FPTR_RELATIVE].u.sym =
    symbol_new (".<fptr>", undefined_section, FUNC_FPTR_RELATIVE,
		&zero_address_frag);

  pseudo_func[FUNC_GP_RELATIVE].u.sym =
    symbol_new (".<gprel>", undefined_section, FUNC_GP_RELATIVE,
		&zero_address_frag);

  pseudo_func[FUNC_LT_RELATIVE].u.sym =
    symbol_new (".<ltoff>", undefined_section, FUNC_LT_RELATIVE,
		&zero_address_frag);

  pseudo_func[FUNC_LT_RELATIVE_X].u.sym =
    symbol_new (".<ltoffx>", undefined_section, FUNC_LT_RELATIVE_X,
		&zero_address_frag);

  pseudo_func[FUNC_PC_RELATIVE].u.sym =
    symbol_new (".<pcrel>", undefined_section, FUNC_PC_RELATIVE,
		&zero_address_frag);

  pseudo_func[FUNC_PLT_RELATIVE].u.sym =
    symbol_new (".<pltoff>", undefined_section, FUNC_PLT_RELATIVE,
		&zero_address_frag);

  pseudo_func[FUNC_SEC_RELATIVE].u.sym =
    symbol_new (".<secrel>", undefined_section, FUNC_SEC_RELATIVE,
		&zero_address_frag);

  pseudo_func[FUNC_SEG_RELATIVE].u.sym =
    symbol_new (".<segrel>", undefined_section, FUNC_SEG_RELATIVE,
		&zero_address_frag);

  pseudo_func[FUNC_TP_RELATIVE].u.sym =
    symbol_new (".<tprel>", undefined_section, FUNC_TP_RELATIVE,
		&zero_address_frag);

  pseudo_func[FUNC_LTV_RELATIVE].u.sym =
    symbol_new (".<ltv>", undefined_section, FUNC_LTV_RELATIVE,
		&zero_address_frag);

  pseudo_func[FUNC_LT_FPTR_RELATIVE].u.sym =
    symbol_new (".<ltoff.fptr>", undefined_section, FUNC_LT_FPTR_RELATIVE,
		&zero_address_frag);

  pseudo_func[FUNC_LT_DTP_MODULE].u.sym =
    symbol_new (".<ltoff.dtpmod>", undefined_section, FUNC_LT_DTP_MODULE,
		&zero_address_frag);

  pseudo_func[FUNC_LT_DTP_RELATIVE].u.sym =
    symbol_new (".<ltoff.dptrel>", undefined_section, FUNC_LT_DTP_RELATIVE,
		&zero_address_frag);

  pseudo_func[FUNC_LT_TP_RELATIVE].u.sym =
    symbol_new (".<ltoff.tprel>", undefined_section, FUNC_LT_TP_RELATIVE,
		&zero_address_frag);

  pseudo_func[FUNC_IPLT_RELOC].u.sym =
    symbol_new (".<iplt>", undefined_section, FUNC_IPLT_RELOC,
		&zero_address_frag);

 if (md.tune != itanium1)
   {
     /* Convert MFI NOPs bundles into MMI NOPs bundles.  */
     le_nop[0] = 0x8;
     le_nop_stop[0] = 0x9;
   }

  /* Compute the table of best templates.  We compute goodness as a
     base 4 value, in which each match counts for 3.  Match-failures
     result in NOPs and we use extra_goodness() to pick the execution
     units that are best suited for issuing the NOP.  */
  for (i = 0; i < IA64_NUM_TYPES; ++i)
    for (j = 0; j < IA64_NUM_TYPES; ++j)
      for (k = 0; k < IA64_NUM_TYPES; ++k)
	{
	  best = 0;
	  for (t = 0; t < NELEMS (ia64_templ_desc); ++t)
	    {
	      goodness = 0;
	      if (match (t, i, 0))
		{
		  if (match (t, j, 1))
		    {
		      if ((t == 2 && j == IA64_TYPE_X) || match (t, k, 2))
			goodness = 3 + 3 + 3;
		      else
			goodness = 3 + 3 + extra_goodness (t, 2);
		    }
		  else if (match (t, j, 2))
		    goodness = 3 + 3 + extra_goodness (t, 1);
		  else
		    {
		      goodness = 3;
		      goodness += extra_goodness (t, 1);
		      goodness += extra_goodness (t, 2);
		    }
		}
	      else if (match (t, i, 1))
		{
		  if ((t == 2 && i == IA64_TYPE_X) || match (t, j, 2))
		    goodness = 3 + 3;
		  else
		    goodness = 3 + extra_goodness (t, 2);
		}
	      else if (match (t, i, 2))
		goodness = 3 + extra_goodness (t, 1);

	      if (goodness > best)
		{
		  best = goodness;
		  best_template[i][j][k] = t;
		}
	    }
	}

#ifdef DEBUG_TEMPLATES
  /* For debugging changes to the best_template calculations.  We don't care
     about combinations with invalid instructions, so start the loops at 1.  */
  for (i = 0; i < IA64_NUM_TYPES; ++i)
    for (j = 0; j < IA64_NUM_TYPES; ++j)
      for (k = 0; k < IA64_NUM_TYPES; ++k)
	{
	  char type_letter[IA64_NUM_TYPES] = { 'n', 'a', 'i', 'm', 'b', 'f',
					       'x', 'd' };
	  fprintf (stderr, "%c%c%c %s\n", type_letter[i], type_letter[j],
		   type_letter[k],
		   ia64_templ_desc[best_template[i][j][k]].name);
	}
#endif

  for (i = 0; i < NUM_SLOTS; ++i)
    md.slot[i].user_template = -1;

  md.pseudo_hash = hash_new ();
  for (i = 0; i < NELEMS (pseudo_opcode); ++i)
    {
      err = hash_insert (md.pseudo_hash, pseudo_opcode[i].name,
			 (void *) (pseudo_opcode + i));
      if (err)
	as_fatal ("ia64.md_begin: can't hash `%s': %s",
		  pseudo_opcode[i].name, err);
    }

  md.reg_hash = hash_new ();
  md.dynreg_hash = hash_new ();
  md.const_hash = hash_new ();
  md.entry_hash = hash_new ();

  /* general registers:  */
  declare_register_set ("r", 128, REG_GR);
  declare_register ("gp", REG_GR +  1);
  declare_register ("sp", REG_GR + 12);
  declare_register ("tp", REG_GR + 13);
  declare_register_set ("ret", 4, REG_GR + 8);

  /* floating point registers:  */
  declare_register_set ("f", 128, REG_FR);
  declare_register_set ("farg", 8, REG_FR + 8);
  declare_register_set ("fret", 8, REG_FR + 8);

  /* branch registers:  */
  declare_register_set ("b", 8, REG_BR);
  declare_register ("rp", REG_BR + 0);

  /* predicate registers:  */
  declare_register_set ("p", 64, REG_P);
  declare_register ("pr", REG_PR);
  declare_register ("pr.rot", REG_PR_ROT);

  /* application registers:  */
  declare_register_set ("ar", 128, REG_AR);
  for (i = 0; i < NELEMS (ar); ++i)
    declare_register (ar[i].name, REG_AR + ar[i].regnum);

  /* control registers:  */
  declare_register_set ("cr", 128, REG_CR);
  for (i = 0; i < NELEMS (cr); ++i)
    declare_register (cr[i].name, REG_CR + cr[i].regnum);

  declare_register ("ip", REG_IP);
  declare_register ("cfm", REG_CFM);
  declare_register ("psr", REG_PSR);
  declare_register ("psr.l", REG_PSR_L);
  declare_register ("psr.um", REG_PSR_UM);

  for (i = 0; i < NELEMS (indirect_reg); ++i)
    {
      unsigned int regnum = indirect_reg[i].regnum;

      md.indregsym[regnum - IND_CPUID] = declare_register (indirect_reg[i].name, regnum);
    }

  /* pseudo-registers used to specify unwind info:  */
  declare_register ("psp", REG_PSP);

  for (i = 0; i < NELEMS (const_bits); ++i)
    {
      err = hash_insert (md.const_hash, const_bits[i].name,
			 (PTR) (const_bits + i));
      if (err)
	as_fatal ("Inserting \"%s\" into constant hash table failed: %s",
		  name, err);
    }

  /* Set the architecture and machine depending on defaults and command line
     options.  */
  if (md.flags & EF_IA_64_ABI64)
    ok = bfd_set_arch_mach (stdoutput, bfd_arch_ia64, bfd_mach_ia64_elf64);
  else
    ok = bfd_set_arch_mach (stdoutput, bfd_arch_ia64, bfd_mach_ia64_elf32);

  if (! ok)
     as_warn (_("Could not set architecture and machine"));

  /* Set the pointer size and pointer shift size depending on md.flags */

  if (md.flags & EF_IA_64_ABI64)
    {
      md.pointer_size = 8;         /* pointers are 8 bytes */
      md.pointer_size_shift = 3;   /* alignment is 8 bytes = 2^2 */
    }
  else
    {
      md.pointer_size = 4;         /* pointers are 4 bytes */
      md.pointer_size_shift = 2;   /* alignment is 4 bytes = 2^2 */
    }

  md.mem_offset.hint = 0;
  md.path = 0;
  md.maxpaths = 0;
  md.entry_labels = NULL;
}

/* Set the default options in md.  Cannot do this in md_begin because
   that is called after md_parse_option which is where we set the
   options in md based on command line options.  */

void
ia64_init (argc, argv)
     int argc ATTRIBUTE_UNUSED;
     char **argv ATTRIBUTE_UNUSED;
{
  md.flags = MD_FLAGS_DEFAULT;
  md.detect_dv = 1;
  /* FIXME: We should change it to unwind_check_error someday.  */
  md.unwind_check = unwind_check_warning;
  md.hint_b = hint_b_error;
  md.tune = itanium2;
}

/* Return a string for the target object file format.  */

const char *
ia64_target_format ()
{
  if (OUTPUT_FLAVOR == bfd_target_elf_flavour)
    {
      if (md.flags & EF_IA_64_BE)
	{
	  if (md.flags & EF_IA_64_ABI64)
#if defined(TE_AIX50)
	    return "elf64-ia64-aix-big";
#elif defined(TE_HPUX)
	    return "elf64-ia64-hpux-big";
#else
	    return "elf64-ia64-big";
#endif
	  else
#if defined(TE_AIX50)
	    return "elf32-ia64-aix-big";
#elif defined(TE_HPUX)
	    return "elf32-ia64-hpux-big";
#else
	    return "elf32-ia64-big";
#endif
	}
      else
	{
	  if (md.flags & EF_IA_64_ABI64)
#if defined(TE_AIX50)
	    return "elf64-ia64-aix-little";
#elif defined(TE_FreeBSD)
	    return "elf64-ia64-freebsd";
#else
	    return "elf64-ia64-little";
#endif
	  else
#ifdef TE_AIX50
	    return "elf32-ia64-aix-little";
#else
	    return "elf32-ia64-little";
#endif
	}
    }
  else
    return "unknown-format";
}

void
ia64_end_of_source ()
{
  /* terminate insn group upon reaching end of file:  */
  insn_group_break (1, 0, 0);

  /* emits slots we haven't written yet:  */
  ia64_flush_insns ();

  bfd_set_private_flags (stdoutput, md.flags);

  md.mem_offset.hint = 0;
}

void
ia64_start_line ()
{
  static int first;

  if (!first) {
    /* Make sure we don't reference input_line_pointer[-1] when that's
       not valid.  */
    first = 1;
    return;
  }

  if (md.qp.X_op == O_register)
    as_bad ("qualifying predicate not followed by instruction");
  md.qp.X_op = O_absent;

  if (ignore_input ())
    return;

  if (input_line_pointer[0] == ';' && input_line_pointer[-1] == ';')
    {
      if (md.detect_dv && !md.explicit_mode)
	{
	  static int warned;

	  if (!warned)
	    {
	      warned = 1;
	      as_warn (_("Explicit stops are ignored in auto mode"));
	    }
	}
      else
	insn_group_break (1, 0, 0);
    }
  else if (input_line_pointer[-1] == '{')
    {
      if (md.manual_bundling)
	as_warn ("Found '{' when manual bundling is already turned on");
      else
	CURR_SLOT.manual_bundling_on = 1;
      md.manual_bundling = 1;

      /* Bundling is only acceptable in explicit mode
	 or when in default automatic mode.  */
      if (md.detect_dv && !md.explicit_mode)
	{
	  if (!md.mode_explicitly_set
	      && !md.default_explicit_mode)
	    dot_dv_mode ('E');
	  else
	    as_warn (_("Found '{' after explicit switch to automatic mode"));
	}
    }
  else if (input_line_pointer[-1] == '}')
    {
      if (!md.manual_bundling)
	as_warn ("Found '}' when manual bundling is off");
      else
	PREV_SLOT.manual_bundling_off = 1;
      md.manual_bundling = 0;

      /* switch back to automatic mode, if applicable */
      if (md.detect_dv
	  && md.explicit_mode
	  && !md.mode_explicitly_set
	  && !md.default_explicit_mode)
	dot_dv_mode ('A');
    }
}

/* This is a hook for ia64_frob_label, so that it can distinguish tags from
   labels.  */
static int defining_tag = 0;

int
ia64_unrecognized_line (ch)
     int ch;
{
  switch (ch)
    {
    case '(':
      expression_and_evaluate (&md.qp);
      if (*input_line_pointer++ != ')')
	{
	  as_bad ("Expected ')'");
	  return 0;
	}
      if (md.qp.X_op != O_register)
	{
	  as_bad ("Qualifying predicate expected");
	  return 0;
	}
      if (md.qp.X_add_number < REG_P || md.qp.X_add_number >= REG_P + 64)
	{
	  as_bad ("Predicate register expected");
	  return 0;
	}
      return 1;

    case '[':
      {
	char *s;
	char c;
	symbolS *tag;
	int temp;

	if (md.qp.X_op == O_register)
	  {
	    as_bad ("Tag must come before qualifying predicate.");
	    return 0;
	  }

	/* This implements just enough of read_a_source_file in read.c to
	   recognize labels.  */
	if (is_name_beginner (*input_line_pointer))
	  {
	    s = input_line_pointer;
	    c = get_symbol_end ();
	  }
	else if (LOCAL_LABELS_FB
		 && ISDIGIT (*input_line_pointer))
	  {
	    temp = 0;
	    while (ISDIGIT (*input_line_pointer))
	      temp = (temp * 10) + *input_line_pointer++ - '0';
	    fb_label_instance_inc (temp);
	    s = fb_label_name (temp, 0);
	    c = *input_line_pointer;
	  }
	else
	  {
	    s = NULL;
	    c = '\0';
	  }
	if (c != ':')
	  {
	    /* Put ':' back for error messages' sake.  */
	    *input_line_pointer++ = ':';
	    as_bad ("Expected ':'");
	    return 0;
	  }

	defining_tag = 1;
	tag = colon (s);
	defining_tag = 0;
	/* Put ':' back for error messages' sake.  */
	*input_line_pointer++ = ':';
	if (*input_line_pointer++ != ']')
	  {
	    as_bad ("Expected ']'");
	    return 0;
	  }
	if (! tag)
	  {
	    as_bad ("Tag name expected");
	    return 0;
	  }
	return 1;
      }

    default:
      break;
    }

  /* Not a valid line.  */
  return 0;
}

void
ia64_frob_label (sym)
     struct symbol *sym;
{
  struct label_fix *fix;

  /* Tags need special handling since they are not bundle breaks like
     labels.  */
  if (defining_tag)
    {
      fix = obstack_alloc (&notes, sizeof (*fix));
      fix->sym = sym;
      fix->next = CURR_SLOT.tag_fixups;
      fix->dw2_mark_labels = FALSE;
      CURR_SLOT.tag_fixups = fix;

      return;
    }

  if (bfd_get_section_flags (stdoutput, now_seg) & SEC_CODE)
    {
      md.last_text_seg = now_seg;
      fix = obstack_alloc (&notes, sizeof (*fix));
      fix->sym = sym;
      fix->next = CURR_SLOT.label_fixups;
      fix->dw2_mark_labels = dwarf2_loc_mark_labels;
      CURR_SLOT.label_fixups = fix;

      /* Keep track of how many code entry points we've seen.  */
      if (md.path == md.maxpaths)
	{
	  md.maxpaths += 20;
	  md.entry_labels = (const char **)
	    xrealloc ((void *) md.entry_labels,
		      md.maxpaths * sizeof (char *));
	}
      md.entry_labels[md.path++] = S_GET_NAME (sym);
    }
}

#ifdef TE_HPUX
/* The HP-UX linker will give unresolved symbol errors for symbols
   that are declared but unused.  This routine removes declared,
   unused symbols from an object.  */
int
ia64_frob_symbol (sym)
     struct symbol *sym;
{
  if ((S_GET_SEGMENT (sym) == &bfd_und_section && ! symbol_used_p (sym) &&
       ELF_ST_VISIBILITY (S_GET_OTHER (sym)) == STV_DEFAULT)
      || (S_GET_SEGMENT (sym) == &bfd_abs_section
	  && ! S_IS_EXTERNAL (sym)))
    return 1;
  return 0;
}
#endif

void
ia64_flush_pending_output ()
{
  if (!md.keep_pending_output
      && bfd_get_section_flags (stdoutput, now_seg) & SEC_CODE)
    {
      /* ??? This causes many unnecessary stop bits to be emitted.
	 Unfortunately, it isn't clear if it is safe to remove this.  */
      insn_group_break (1, 0, 0);
      ia64_flush_insns ();
    }
}

/* Do ia64-specific expression optimization.  All that's done here is
   to transform index expressions that are either due to the indexing
   of rotating registers or due to the indexing of indirect register
   sets.  */
int
ia64_optimize_expr (l, op, r)
     expressionS *l;
     operatorT op;
     expressionS *r;
{
  if (op != O_index)
    return 0;
  resolve_expression (l);
  if (l->X_op == O_register)
    {
      unsigned num_regs = l->X_add_number >> 16;

      resolve_expression (r);
      if (num_regs)
	{
	  /* Left side is a .rotX-allocated register.  */
	  if (r->X_op != O_constant)
	    {
	      as_bad ("Rotating register index must be a non-negative constant");
	      r->X_add_number = 0;
	    }
	  else if ((valueT) r->X_add_number >= num_regs)
	    {
	      as_bad ("Index out of range 0..%u", num_regs - 1);
	      r->X_add_number = 0;
	    }
	  l->X_add_number = (l->X_add_number & 0xffff) + r->X_add_number;
	  return 1;
	}
      else if (l->X_add_number >= IND_CPUID && l->X_add_number <= IND_RR)
	{
	  if (r->X_op != O_register
	      || r->X_add_number < REG_GR
	      || r->X_add_number > REG_GR + 127)
	    {
	      as_bad ("Indirect register index must be a general register");
	      r->X_add_number = REG_GR;
	    }
	  l->X_op = O_index;
	  l->X_op_symbol = md.indregsym[l->X_add_number - IND_CPUID];
	  l->X_add_number = r->X_add_number;
	  return 1;
	}
    }
  as_bad ("Index can only be applied to rotating or indirect registers");
  /* Fall back to some register use of which has as little as possible
     side effects, to minimize subsequent error messages.  */
  l->X_op = O_register;
  l->X_add_number = REG_GR + 3;
  return 1;
}

int
ia64_parse_name (name, e, nextcharP)
     char *name;
     expressionS *e;
     char *nextcharP;
{
  struct const_desc *cdesc;
  struct dynreg *dr = 0;
  unsigned int idx;
  struct symbol *sym;
  char *end;

  if (*name == '@')
    {
      enum pseudo_type pseudo_type = PSEUDO_FUNC_NONE;

      /* Find what relocation pseudo-function we're dealing with.  */
      for (idx = 0; idx < NELEMS (pseudo_func); ++idx)
	if (pseudo_func[idx].name
	    && pseudo_func[idx].name[0] == name[1]
	    && strcmp (pseudo_func[idx].name + 1, name + 2) == 0)
	  {
	    pseudo_type = pseudo_func[idx].type;
	    break;
	  }
      switch (pseudo_type)
	{
	case PSEUDO_FUNC_RELOC:
	  end = input_line_pointer;
	  if (*nextcharP != '(')
	    {
	      as_bad ("Expected '('");
	      break;
	    }
	  /* Skip '('.  */
	  ++input_line_pointer;
	  expression (e);
	  if (*input_line_pointer != ')')
	    {
	      as_bad ("Missing ')'");
	      goto done;
	    }
	  /* Skip ')'.  */
	  ++input_line_pointer;
	  if (e->X_op != O_symbol)
	    {
	      if (e->X_op != O_pseudo_fixup)
		{
		  as_bad ("Not a symbolic expression");
		  goto done;
		}
	      if (idx != FUNC_LT_RELATIVE)
		{
		  as_bad ("Illegal combination of relocation functions");
		  goto done;
		}
	      switch (S_GET_VALUE (e->X_op_symbol))
		{
		case FUNC_FPTR_RELATIVE:
		  idx = FUNC_LT_FPTR_RELATIVE; break;
		case FUNC_DTP_MODULE:
		  idx = FUNC_LT_DTP_MODULE; break;
		case FUNC_DTP_RELATIVE:
		  idx = FUNC_LT_DTP_RELATIVE; break;
		case FUNC_TP_RELATIVE:
		  idx = FUNC_LT_TP_RELATIVE; break;
		default:
		  as_bad ("Illegal combination of relocation functions");
		  goto done;
		}
	    }
	  /* Make sure gas doesn't get rid of local symbols that are used
	     in relocs.  */
	  e->X_op = O_pseudo_fixup;
	  e->X_op_symbol = pseudo_func[idx].u.sym;
	done:
	  *nextcharP = *input_line_pointer;
	  break;

	case PSEUDO_FUNC_CONST:
	  e->X_op = O_constant;
	  e->X_add_number = pseudo_func[idx].u.ival;
	  break;

	case PSEUDO_FUNC_REG:
	  e->X_op = O_register;
	  e->X_add_number = pseudo_func[idx].u.ival;
	  break;

	default:
	  return 0;
	}
      return 1;
    }

  /* first see if NAME is a known register name:  */
  sym = hash_find (md.reg_hash, name);
  if (sym)
    {
      e->X_op = O_register;
      e->X_add_number = S_GET_VALUE (sym);
      return 1;
    }

  cdesc = hash_find (md.const_hash, name);
  if (cdesc)
    {
      e->X_op = O_constant;
      e->X_add_number = cdesc->value;
      return 1;
    }

  /* check for inN, locN, or outN:  */
  idx = 0;
  switch (name[0])
    {
    case 'i':
      if (name[1] == 'n' && ISDIGIT (name[2]))
	{
	  dr = &md.in;
	  idx = 2;
	}
      break;

    case 'l':
      if (name[1] == 'o' && name[2] == 'c' && ISDIGIT (name[3]))
	{
	  dr = &md.loc;
	  idx = 3;
	}
      break;

    case 'o':
      if (name[1] == 'u' && name[2] == 't' && ISDIGIT (name[3]))
	{
	  dr = &md.out;
	  idx = 3;
	}
      break;

    default:
      break;
    }

  /* Ignore register numbers with leading zeroes, except zero itself.  */
  if (dr && (name[idx] != '0' || name[idx + 1] == '\0'))
    {
      unsigned long regnum;

      /* The name is inN, locN, or outN; parse the register number.  */
      regnum = strtoul (name + idx, &end, 10);
      if (end > name + idx && *end == '\0' && regnum < 96)
	{
	  if (regnum >= dr->num_regs)
	    {
	      if (!dr->num_regs)
		as_bad ("No current frame");
	      else
		as_bad ("Register number out of range 0..%u",
			dr->num_regs - 1);
	      regnum = 0;
	    }
	  e->X_op = O_register;
	  e->X_add_number = dr->base + regnum;
	  return 1;
	}
    }

  end = alloca (strlen (name) + 1);
  strcpy (end, name);
  name = ia64_canonicalize_symbol_name (end);
  if ((dr = hash_find (md.dynreg_hash, name)))
    {
      /* We've got ourselves the name of a rotating register set.
	 Store the base register number in the low 16 bits of
	 X_add_number and the size of the register set in the top 16
	 bits.  */
      e->X_op = O_register;
      e->X_add_number = dr->base | (dr->num_regs << 16);
      return 1;
    }
  return 0;
}

/* Remove the '#' suffix that indicates a symbol as opposed to a register.  */

char *
ia64_canonicalize_symbol_name (name)
     char *name;
{
  size_t len = strlen (name), full = len;

  while (len > 0 && name[len - 1] == '#')
    --len;
  if (len <= 0)
    {
      if (full > 0)
	as_bad ("Standalone `#' is illegal");
    }
  else if (len < full - 1)
    as_warn ("Redundant `#' suffix operators");
  name[len] = '\0';
  return name;
}

/* Return true if idesc is a conditional branch instruction.  This excludes
   the modulo scheduled branches, and br.ia.  Mod-sched branches are excluded
   because they always read/write resources regardless of the value of the
   qualifying predicate.  br.ia must always use p0, and hence is always
   taken.  Thus this function returns true for branches which can fall
   through, and which use no resources if they do fall through.  */

static int
is_conditional_branch (idesc)
     struct ia64_opcode *idesc;
{
  /* br is a conditional branch.  Everything that starts with br. except
     br.ia, br.c{loop,top,exit}, and br.w{top,exit} is a conditional branch.
     Everything that starts with brl is a conditional branch.  */
  return (idesc->name[0] == 'b' && idesc->name[1] == 'r'
	  && (idesc->name[2] == '\0'
	      || (idesc->name[2] == '.' && idesc->name[3] != 'i'
		  && idesc->name[3] != 'c' && idesc->name[3] != 'w')
	      || idesc->name[2] == 'l'
	      /* br.cond, br.call, br.clr  */
	      || (idesc->name[2] == '.' && idesc->name[3] == 'c'
		  && (idesc->name[4] == 'a' || idesc->name[4] == 'o'
		      || (idesc->name[4] == 'l' && idesc->name[5] == 'r')))));
}

/* Return whether the given opcode is a taken branch.  If there's any doubt,
   returns zero.  */

static int
is_taken_branch (idesc)
     struct ia64_opcode *idesc;
{
  return ((is_conditional_branch (idesc) && CURR_SLOT.qp_regno == 0)
	  || strncmp (idesc->name, "br.ia", 5) == 0);
}

/* Return whether the given opcode is an interruption or rfi.  If there's any
   doubt, returns zero.  */

static int
is_interruption_or_rfi (idesc)
     struct ia64_opcode *idesc;
{
  if (strcmp (idesc->name, "rfi") == 0)
    return 1;
  return 0;
}

/* Returns the index of the given dependency in the opcode's list of chks, or
   -1 if there is no dependency.  */

static int
depends_on (depind, idesc)
     int depind;
     struct ia64_opcode *idesc;
{
  int i;
  const struct ia64_opcode_dependency *dep = idesc->dependencies;
  for (i = 0; i < dep->nchks; i++)
    {
      if (depind == DEP (dep->chks[i]))
	return i;
    }
  return -1;
}

/* Determine a set of specific resources used for a particular resource
   class.  Returns the number of specific resources identified  For those
   cases which are not determinable statically, the resource returned is
   marked nonspecific.

   Meanings of value in 'NOTE':
   1) only read/write when the register number is explicitly encoded in the
   insn.
   2) only read CFM when accessing a rotating GR, FR, or PR.  mov pr only
   accesses CFM when qualifying predicate is in the rotating region.
   3) general register value is used to specify an indirect register; not
   determinable statically.
   4) only read the given resource when bits 7:0 of the indirect index
   register value does not match the register number of the resource; not
   determinable statically.
   5) all rules are implementation specific.
   6) only when both the index specified by the reader and the index specified
   by the writer have the same value in bits 63:61; not determinable
   statically.
   7) only access the specified resource when the corresponding mask bit is
   set
   8) PSR.dfh is only read when these insns reference FR32-127.  PSR.dfl is
   only read when these insns reference FR2-31
   9) PSR.mfl is only written when these insns write FR2-31.  PSR.mfh is only
   written when these insns write FR32-127
   10) The PSR.bn bit is only accessed when one of GR16-31 is specified in the
   instruction
   11) The target predicates are written independently of PR[qp], but source
   registers are only read if PR[qp] is true.  Since the state of PR[qp]
   cannot statically be determined, all source registers are marked used.
   12) This insn only reads the specified predicate register when that
   register is the PR[qp].
   13) This reference to ld-c only applies to teh GR whose value is loaded
   with data returned from memory, not the post-incremented address register.
   14) The RSE resource includes the implementation-specific RSE internal
   state resources.  At least one (and possibly more) of these resources are
   read by each instruction listed in IC:rse-readers.  At least one (and
   possibly more) of these resources are written by each insn listed in
   IC:rse-writers.
   15+16) Represents reserved instructions, which the assembler does not
   generate.
   17) CR[TPR] has a RAW dependency only between mov-to-CR-TPR and
   mov-to-PSR-l or ssm instructions that set PSR.i, PSR.pp or PSR.up.

   Memory resources (i.e. locations in memory) are *not* marked or tracked by
   this code; there are no dependency violations based on memory access.
*/

#define MAX_SPECS 256
#define DV_CHK 1
#define DV_REG 0

static int
specify_resource (dep, idesc, type, specs, note, path)
     const struct ia64_dependency *dep;
     struct ia64_opcode *idesc;
     int type;                         /* is this a DV chk or a DV reg? */
     struct rsrc specs[MAX_SPECS];     /* returned specific resources */
     int note;                         /* resource note for this insn's usage */
     int path;                         /* which execution path to examine */
{
  int count = 0;
  int i;
  int rsrc_write = 0;
  struct rsrc tmpl;

  if (dep->mode == IA64_DV_WAW
      || (dep->mode == IA64_DV_RAW && type == DV_REG)
      || (dep->mode == IA64_DV_WAR && type == DV_CHK))
    rsrc_write = 1;

  /* template for any resources we identify */
  tmpl.dependency = dep;
  tmpl.note = note;
  tmpl.insn_srlz = tmpl.data_srlz = 0;
  tmpl.qp_regno = CURR_SLOT.qp_regno;
  tmpl.link_to_qp_branch = 1;
  tmpl.mem_offset.hint = 0;
  tmpl.mem_offset.offset = 0;
  tmpl.mem_offset.base = 0;
  tmpl.specific = 1;
  tmpl.index = -1;
  tmpl.cmp_type = CMP_NONE;
  tmpl.depind = 0;
  tmpl.file = NULL;
  tmpl.line = 0;
  tmpl.path = 0;

#define UNHANDLED \
as_warn (_("Unhandled dependency %s for %s (%s), note %d"), \
dep->name, idesc->name, (rsrc_write?"write":"read"), note)
#define KNOWN(REG) (gr_values[REG].known && gr_values[REG].path >= path)

  /* we don't need to track these */
  if (dep->semantics == IA64_DVS_NONE)
    return 0;

  switch (dep->specifier)
    {
    case IA64_RS_AR_K:
      if (note == 1)
	{
	  if (idesc->operands[!rsrc_write] == IA64_OPND_AR3)
	    {
	      int regno = CURR_SLOT.opnd[!rsrc_write].X_add_number - REG_AR;
	      if (regno >= 0 && regno <= 7)
		{
		  specs[count] = tmpl;
		  specs[count++].index = regno;
		}
	    }
	}
      else if (note == 0)
	{
	  for (i = 0; i < 8; i++)
	    {
	      specs[count] = tmpl;
	      specs[count++].index = i;
	    }
	}
      else
	{
	  UNHANDLED;
	}
      break;

    case IA64_RS_AR_UNAT:
      /* This is a mov =AR or mov AR= instruction.  */
      if (idesc->operands[!rsrc_write] == IA64_OPND_AR3)
	{
	  int regno = CURR_SLOT.opnd[!rsrc_write].X_add_number - REG_AR;
	  if (regno == AR_UNAT)
	    {
	      specs[count++] = tmpl;
	    }
	}
      else
	{
	  /* This is a spill/fill, or other instruction that modifies the
	     unat register.  */

	  /* Unless we can determine the specific bits used, mark the whole
	     thing; bits 8:3 of the memory address indicate the bit used in
	     UNAT.  The .mem.offset hint may be used to eliminate a small
	     subset of conflicts.  */
	  specs[count] = tmpl;
	  if (md.mem_offset.hint)
	    {
	      if (md.debug_dv)
		fprintf (stderr, "  Using hint for spill/fill\n");
	      /* The index isn't actually used, just set it to something
		 approximating the bit index.  */
	      specs[count].index = (md.mem_offset.offset >> 3) & 0x3F;
	      specs[count].mem_offset.hint = 1;
	      specs[count].mem_offset.offset = md.mem_offset.offset;
	      specs[count++].mem_offset.base = md.mem_offset.base;
	    }
	  else
	    {
	      specs[count++].specific = 0;
	    }
	}
      break;

    case IA64_RS_AR:
      if (note == 1)
	{
	  if (idesc->operands[!rsrc_write] == IA64_OPND_AR3)
	    {
	      int regno = CURR_SLOT.opnd[!rsrc_write].X_add_number - REG_AR;
	      if ((regno >= 8 && regno <= 15)
		  || (regno >= 20 && regno <= 23)
		  || (regno >= 31 && regno <= 39)
		  || (regno >= 41 && regno <= 47)
		  || (regno >= 67 && regno <= 111))
		{
		  specs[count] = tmpl;
		  specs[count++].index = regno;
		}
	    }
	}
      else
	{
	  UNHANDLED;
	}
      break;

    case IA64_RS_ARb:
      if (note == 1)
	{
	  if (idesc->operands[!rsrc_write] == IA64_OPND_AR3)
	    {
	      int regno = CURR_SLOT.opnd[!rsrc_write].X_add_number - REG_AR;
	      if ((regno >= 48 && regno <= 63)
		  || (regno >= 112 && regno <= 127))
		{
		  specs[count] = tmpl;
		  specs[count++].index = regno;
		}
	    }
	}
      else if (note == 0)
	{
	  for (i = 48; i < 64; i++)
	    {
	      specs[count] = tmpl;
	      specs[count++].index = i;
	    }
	  for (i = 112; i < 128; i++)
	    {
	      specs[count] = tmpl;
	      specs[count++].index = i;
	    }
	}
      else
	{
	  UNHANDLED;
	}
      break;

    case IA64_RS_BR:
      if (note != 1)
	{
	  UNHANDLED;
	}
      else
	{
	  if (rsrc_write)
	    {
	      for (i = 0; i < idesc->num_outputs; i++)
		if (idesc->operands[i] == IA64_OPND_B1
		    || idesc->operands[i] == IA64_OPND_B2)
		  {
		    specs[count] = tmpl;
		    specs[count++].index =
		      CURR_SLOT.opnd[i].X_add_number - REG_BR;
		  }
	    }
	  else
	    {
	      for (i = idesc->num_outputs; i < NELEMS (idesc->operands); i++)
		if (idesc->operands[i] == IA64_OPND_B1
		    || idesc->operands[i] == IA64_OPND_B2)
		  {
		    specs[count] = tmpl;
		    specs[count++].index =
		      CURR_SLOT.opnd[i].X_add_number - REG_BR;
		  }
	    }
	}
      break;

    case IA64_RS_CPUID: /* four or more registers */
      if (note == 3)
	{
	  if (idesc->operands[!rsrc_write] == IA64_OPND_CPUID_R3)
	    {
	      int regno = CURR_SLOT.opnd[!rsrc_write].X_add_number - REG_GR;
	      if (regno >= 0 && regno < NELEMS (gr_values)
		  && KNOWN (regno))
		{
		  specs[count] = tmpl;
		  specs[count++].index = gr_values[regno].value & 0xFF;
		}
	      else
		{
		  specs[count] = tmpl;
		  specs[count++].specific = 0;
		}
	    }
	}
      else
	{
	  UNHANDLED;
	}
      break;

    case IA64_RS_DBR: /* four or more registers */
      if (note == 3)
	{
	  if (idesc->operands[!rsrc_write] == IA64_OPND_DBR_R3)
	    {
	      int regno = CURR_SLOT.opnd[!rsrc_write].X_add_number - REG_GR;
	      if (regno >= 0 && regno < NELEMS (gr_values)
		  && KNOWN (regno))
		{
		  specs[count] = tmpl;
		  specs[count++].index = gr_values[regno].value & 0xFF;
		}
	      else
		{
		  specs[count] = tmpl;
		  specs[count++].specific = 0;
		}
	    }
	}
      else if (note == 0 && !rsrc_write)
	{
	  specs[count] = tmpl;
	  specs[count++].specific = 0;
	}
      else
	{
	  UNHANDLED;
	}
      break;

    case IA64_RS_IBR: /* four or more registers */
      if (note == 3)
	{
	  if (idesc->operands[!rsrc_write] == IA64_OPND_IBR_R3)
	    {
	      int regno = CURR_SLOT.opnd[!rsrc_write].X_add_number - REG_GR;
	      if (regno >= 0 && regno < NELEMS (gr_values)
		  && KNOWN (regno))
		{
		  specs[count] = tmpl;
		  specs[count++].index = gr_values[regno].value & 0xFF;
		}
	      else
		{
		  specs[count] = tmpl;
		  specs[count++].specific = 0;
		}
	    }
	}
      else
	{
	  UNHANDLED;
	}
      break;

    case IA64_RS_MSR:
      if (note == 5)
	{
	  /* These are implementation specific.  Force all references to
	     conflict with all other references.  */
	  specs[count] = tmpl;
	  specs[count++].specific = 0;
	}
      else
	{
	  UNHANDLED;
	}
      break;

    case IA64_RS_PKR: /* 16 or more registers */
      if (note == 3 || note == 4)
	{
	  if (idesc->operands[!rsrc_write] == IA64_OPND_PKR_R3)
	    {
	      int regno = CURR_SLOT.opnd[!rsrc_write].X_add_number - REG_GR;
	      if (regno >= 0 && regno < NELEMS (gr_values)
		  && KNOWN (regno))
		{
		  if (note == 3)
		    {
		      specs[count] = tmpl;
		      specs[count++].index = gr_values[regno].value & 0xFF;
		    }
		  else
		    for (i = 0; i < NELEMS (gr_values); i++)
		      {
			/* Uses all registers *except* the one in R3.  */
			if ((unsigned)i != (gr_values[regno].value & 0xFF))
			  {
			    specs[count] = tmpl;
			    specs[count++].index = i;
			  }
		      }
		}
	      else
		{
		  specs[count] = tmpl;
		  specs[count++].specific = 0;
		}
	    }
	}
      else if (note == 0)
	{
	  /* probe et al.  */
	  specs[count] = tmpl;
	  specs[count++].specific = 0;
	}
      break;

    case IA64_RS_PMC: /* four or more registers */
      if (note == 3)
	{
	  if (idesc->operands[!rsrc_write] == IA64_OPND_PMC_R3
	      || (!rsrc_write && idesc->operands[1] == IA64_OPND_PMD_R3))

	    {
	      int index = ((idesc->operands[1] == IA64_OPND_R3 && !rsrc_write)
			   ? 1 : !rsrc_write);
	      int regno = CURR_SLOT.opnd[index].X_add_number - REG_GR;
	      if (regno >= 0 && regno < NELEMS (gr_values)
		  && KNOWN (regno))
		{
		  specs[count] = tmpl;
		  specs[count++].index = gr_values[regno].value & 0xFF;
		}
	      else
		{
		  specs[count] = tmpl;
		  specs[count++].specific = 0;
		}
	    }
	}
      else
	{
	  UNHANDLED;
	}
      break;

    case IA64_RS_PMD: /* four or more registers */
      if (note == 3)
	{
	  if (idesc->operands[!rsrc_write] == IA64_OPND_PMD_R3)
	    {
	      int regno = CURR_SLOT.opnd[!rsrc_write].X_add_number - REG_GR;
	      if (regno >= 0 && regno < NELEMS (gr_values)
		  && KNOWN (regno))
		{
		  specs[count] = tmpl;
		  specs[count++].index = gr_values[regno].value & 0xFF;
		}
	      else
		{
		  specs[count] = tmpl;
		  specs[count++].specific = 0;
		}
	    }
	}
      else
	{
	  UNHANDLED;
	}
      break;

    case IA64_RS_RR: /* eight registers */
      if (note == 6)
	{
	  if (idesc->operands[!rsrc_write] == IA64_OPND_RR_R3)
	    {
	      int regno = CURR_SLOT.opnd[!rsrc_write].X_add_number - REG_GR;
	      if (regno >= 0 && regno < NELEMS (gr_values)
		  && KNOWN (regno))
		{
		  specs[count] = tmpl;
		  specs[count++].index = (gr_values[regno].value >> 61) & 0x7;
		}
	      else
		{
		  specs[count] = tmpl;
		  specs[count++].specific = 0;
		}
	    }
	}
      else if (note == 0 && !rsrc_write)
	{
	  specs[count] = tmpl;
	  specs[count++].specific = 0;
	}
      else
	{
	  UNHANDLED;
	}
      break;

    case IA64_RS_CR_IRR:
      if (note == 0)
	{
	  /* handle mov-from-CR-IVR; it's a read that writes CR[IRR] */
	  int regno = CURR_SLOT.opnd[1].X_add_number - REG_CR;
	  if (rsrc_write
	      && idesc->operands[1] == IA64_OPND_CR3
	      && regno == CR_IVR)
	    {
	      for (i = 0; i < 4; i++)
		{
		  specs[count] = tmpl;
		  specs[count++].index = CR_IRR0 + i;
		}
	    }
	}
      else if (note == 1)
	{
	  int regno = CURR_SLOT.opnd[!rsrc_write].X_add_number - REG_CR;
	  if (idesc->operands[!rsrc_write] == IA64_OPND_CR3
	      && regno >= CR_IRR0
	      && regno <= CR_IRR3)
	    {
	      specs[count] = tmpl;
	      specs[count++].index = regno;
	    }
	}
      else
	{
	  UNHANDLED;
	}
      break;

    case IA64_RS_CR_LRR:
      if (note != 1)
	{
	  UNHANDLED;
	}
      else
	{
	  int regno = CURR_SLOT.opnd[!rsrc_write].X_add_number - REG_CR;
	  if (idesc->operands[!rsrc_write] == IA64_OPND_CR3
	      && (regno == CR_LRR0 || regno == CR_LRR1))
	    {
	      specs[count] = tmpl;
	      specs[count++].index = regno;
	    }
	}
      break;

    case IA64_RS_CR:
      if (note == 1)
	{
	  if (idesc->operands[!rsrc_write] == IA64_OPND_CR3)
	    {
	      specs[count] = tmpl;
	      specs[count++].index =
		CURR_SLOT.opnd[!rsrc_write].X_add_number - REG_CR;
	    }
	}
      else
	{
	  UNHANDLED;
	}
      break;

    case IA64_RS_FR:
    case IA64_RS_FRb:
      if (note != 1)
	{
	  UNHANDLED;
	}
      else if (rsrc_write)
	{
	  if (dep->specifier == IA64_RS_FRb
	      && idesc->operands[0] == IA64_OPND_F1)
	    {
	      specs[count] = tmpl;
	      specs[count++].index = CURR_SLOT.opnd[0].X_add_number - REG_FR;
	    }
	}
      else
	{
	  for (i = idesc->num_outputs; i < NELEMS (idesc->operands); i++)
	    {
	      if (idesc->operands[i] == IA64_OPND_F2
		  || idesc->operands[i] == IA64_OPND_F3
		  || idesc->operands[i] == IA64_OPND_F4)
		{
		  specs[count] = tmpl;
		  specs[count++].index =
		    CURR_SLOT.opnd[i].X_add_number - REG_FR;
		}
	    }
	}
      break;

    case IA64_RS_GR:
      if (note == 13)
	{
	  /* This reference applies only to the GR whose value is loaded with
	     data returned from memory.  */
	  specs[count] = tmpl;
	  specs[count++].index = CURR_SLOT.opnd[0].X_add_number - REG_GR;
	}
      else if (note == 1)
	{
	  if (rsrc_write)
	    {
	      for (i = 0; i < idesc->num_outputs; i++)
		if (idesc->operands[i] == IA64_OPND_R1
		    || idesc->operands[i] == IA64_OPND_R2
		    || idesc->operands[i] == IA64_OPND_R3)
		  {
		    specs[count] = tmpl;
		    specs[count++].index =
		      CURR_SLOT.opnd[i].X_add_number - REG_GR;
		  }
	      if (idesc->flags & IA64_OPCODE_POSTINC)
		for (i = 0; i < NELEMS (idesc->operands); i++)
		  if (idesc->operands[i] == IA64_OPND_MR3)
		    {
		      specs[count] = tmpl;
		      specs[count++].index =
			CURR_SLOT.opnd[i].X_add_number - REG_GR;
		    }
	    }
	  else
	    {
	      /* Look for anything that reads a GR.  */
	      for (i = 0; i < NELEMS (idesc->operands); i++)
		{
		  if (idesc->operands[i] == IA64_OPND_MR3
		      || idesc->operands[i] == IA64_OPND_CPUID_R3
		      || idesc->operands[i] == IA64_OPND_DBR_R3
		      || idesc->operands[i] == IA64_OPND_IBR_R3
		      || idesc->operands[i] == IA64_OPND_MSR_R3
		      || idesc->operands[i] == IA64_OPND_PKR_R3
		      || idesc->operands[i] == IA64_OPND_PMC_R3
		      || idesc->operands[i] == IA64_OPND_PMD_R3
		      || idesc->operands[i] == IA64_OPND_RR_R3
		      || ((i >= idesc->num_outputs)
			  && (idesc->operands[i] == IA64_OPND_R1
			      || idesc->operands[i] == IA64_OPND_R2
			      || idesc->operands[i] == IA64_OPND_R3
			      /* addl source register.  */
			      || idesc->operands[i] == IA64_OPND_R3_2)))
		    {
		      specs[count] = tmpl;
		      specs[count++].index =
			CURR_SLOT.opnd[i].X_add_number - REG_GR;
		    }
		}
	    }
	}
      else
	{
	  UNHANDLED;
	}
      break;

      /* This is the same as IA64_RS_PRr, except that the register range is
	 from 1 - 15, and there are no rotating register reads/writes here.  */
    case IA64_RS_PR:
      if (note == 0)
	{
	  for (i = 1; i < 16; i++)
	    {
	      specs[count] = tmpl;
	      specs[count++].index = i;
	    }
	}
      else if (note == 7)
	{
	  valueT mask = 0;
	  /* Mark only those registers indicated by the mask.  */
	  if (rsrc_write)
	    {
	      mask = CURR_SLOT.opnd[2].X_add_number;
	      for (i = 1; i < 16; i++)
		if (mask & ((valueT) 1 << i))
		  {
		    specs[count] = tmpl;
		    specs[count++].index = i;
		  }
	    }
	  else
	    {
	      UNHANDLED;
	    }
	}
      else if (note == 11) /* note 11 implies note 1 as well */
	{
	  if (rsrc_write)
	    {
	      for (i = 0; i < idesc->num_outputs; i++)
		{
		  if (idesc->operands[i] == IA64_OPND_P1
		      || idesc->operands[i] == IA64_OPND_P2)
		    {
		      int regno = CURR_SLOT.opnd[i].X_add_number - REG_P;
		      if (regno >= 1 && regno < 16)
			{
			  specs[count] = tmpl;
			  specs[count++].index = regno;
			}
		    }
		}
	    }
	  else
	    {
	      UNHANDLED;
	    }
	}
      else if (note == 12)
	{
	  if (CURR_SLOT.qp_regno >= 1 && CURR_SLOT.qp_regno < 16)
	    {
	      specs[count] = tmpl;
	      specs[count++].index = CURR_SLOT.qp_regno;
	    }
	}
      else if (note == 1)
	{
	  if (rsrc_write)
	    {
	      int p1 = CURR_SLOT.opnd[0].X_add_number - REG_P;
	      int p2 = CURR_SLOT.opnd[1].X_add_number - REG_P;
	      int or_andcm = strstr (idesc->name, "or.andcm") != NULL;
	      int and_orcm = strstr (idesc->name, "and.orcm") != NULL;

	      if ((idesc->operands[0] == IA64_OPND_P1
		   || idesc->operands[0] == IA64_OPND_P2)
		  && p1 >= 1 && p1 < 16)
		{
		  specs[count] = tmpl;
		  specs[count].cmp_type =
		    (or_andcm ? CMP_OR : (and_orcm ? CMP_AND : CMP_NONE));
		  specs[count++].index = p1;
		}
	      if ((idesc->operands[1] == IA64_OPND_P1
		   || idesc->operands[1] == IA64_OPND_P2)
		  && p2 >= 1 && p2 < 16)
		{
		  specs[count] = tmpl;
		  specs[count].cmp_type =
		    (or_andcm ? CMP_AND : (and_orcm ? CMP_OR : CMP_NONE));
		  specs[count++].index = p2;
		}
	    }
	  else
	    {
	      if (CURR_SLOT.qp_regno >= 1 && CURR_SLOT.qp_regno < 16)
		{
		  specs[count] = tmpl;
		  specs[count++].index = CURR_SLOT.qp_regno;
		}
	      if (idesc->operands[1] == IA64_OPND_PR)
		{
		  for (i = 1; i < 16; i++)
		    {
		      specs[count] = tmpl;
		      specs[count++].index = i;
		    }
		}
	    }
	}
      else
	{
	  UNHANDLED;
	}
      break;

      /* This is the general case for PRs.  IA64_RS_PR and IA64_RS_PR63 are
	 simplified cases of this.  */
    case IA64_RS_PRr:
      if (note == 0)
	{
	  for (i = 16; i < 63; i++)
	    {
	      specs[count] = tmpl;
	      specs[count++].index = i;
	    }
	}
      else if (note == 7)
	{
	  valueT mask = 0;
	  /* Mark only those registers indicated by the mask.  */
	  if (rsrc_write
	      && idesc->operands[0] == IA64_OPND_PR)
	    {
	      mask = CURR_SLOT.opnd[2].X_add_number;
	      if (mask & ((valueT) 1 << 16))
		for (i = 16; i < 63; i++)
		  {
		    specs[count] = tmpl;
		    specs[count++].index = i;
		  }
	    }
	  else if (rsrc_write
		   && idesc->operands[0] == IA64_OPND_PR_ROT)
	    {
	      for (i = 16; i < 63; i++)
		{
		  specs[count] = tmpl;
		  specs[count++].index = i;
		}
	    }
	  else
	    {
	      UNHANDLED;
	    }
	}
      else if (note == 11) /* note 11 implies note 1 as well */
	{
	  if (rsrc_write)
	    {
	      for (i = 0; i < idesc->num_outputs; i++)
		{
		  if (idesc->operands[i] == IA64_OPND_P1
		      || idesc->operands[i] == IA64_OPND_P2)
		    {
		      int regno = CURR_SLOT.opnd[i].X_add_number - REG_P;
		      if (regno >= 16 && regno < 63)
			{
			  specs[count] = tmpl;
			  specs[count++].index = regno;
			}
		    }
		}
	    }
	  else
	    {
	      UNHANDLED;
	    }
	}
      else if (note == 12)
	{
	  if (CURR_SLOT.qp_regno >= 16 && CURR_SLOT.qp_regno < 63)
	    {
	      specs[count] = tmpl;
	      specs[count++].index = CURR_SLOT.qp_regno;
	    }
	}
      else if (note == 1)
	{
	  if (rsrc_write)
	    {
	      int p1 = CURR_SLOT.opnd[0].X_add_number - REG_P;
	      int p2 = CURR_SLOT.opnd[1].X_add_number - REG_P;
	      int or_andcm = strstr (idesc->name, "or.andcm") != NULL;
	      int and_orcm = strstr (idesc->name, "and.orcm") != NULL;

	      if ((idesc->operands[0] == IA64_OPND_P1
		   || idesc->operands[0] == IA64_OPND_P2)
		  && p1 >= 16 && p1 < 63)
		{
		  specs[count] = tmpl;
		  specs[count].cmp_type =
		    (or_andcm ? CMP_OR : (and_orcm ? CMP_AND : CMP_NONE));
		  specs[count++].index = p1;
		}
	      if ((idesc->operands[1] == IA64_OPND_P1
		   || idesc->operands[1] == IA64_OPND_P2)
		  && p2 >= 16 && p2 < 63)
		{
		  specs[count] = tmpl;
		  specs[count].cmp_type =
		    (or_andcm ? CMP_AND : (and_orcm ? CMP_OR : CMP_NONE));
		  specs[count++].index = p2;
		}
	    }
	  else
	    {
	      if (CURR_SLOT.qp_regno >= 16 && CURR_SLOT.qp_regno < 63)
		{
		  specs[count] = tmpl;
		  specs[count++].index = CURR_SLOT.qp_regno;
		}
	      if (idesc->operands[1] == IA64_OPND_PR)
		{
		  for (i = 16; i < 63; i++)
		    {
		      specs[count] = tmpl;
		      specs[count++].index = i;
		    }
		}
	    }
	}
      else
	{
	  UNHANDLED;
	}
      break;

    case IA64_RS_PSR:
      /* Verify that the instruction is using the PSR bit indicated in
	 dep->regindex.  */
      if (note == 0)
	{
	  if (idesc->operands[!rsrc_write] == IA64_OPND_PSR_UM)
	    {
	      if (dep->regindex < 6)
		{
		  specs[count++] = tmpl;
		}
	    }
	  else if (idesc->operands[!rsrc_write] == IA64_OPND_PSR)
	    {
	      if (dep->regindex < 32
		  || dep->regindex == 35
		  || dep->regindex == 36
		  || (!rsrc_write && dep->regindex == PSR_CPL))
		{
		  specs[count++] = tmpl;
		}
	    }
	  else if (idesc->operands[!rsrc_write] == IA64_OPND_PSR_L)
	    {
	      if (dep->regindex < 32
		  || dep->regindex == 35
		  || dep->regindex == 36
		  || (rsrc_write && dep->regindex == PSR_CPL))
		{
		  specs[count++] = tmpl;
		}
	    }
	  else
	    {
	      /* Several PSR bits have very specific dependencies.  */
	      switch (dep->regindex)
		{
		default:
		  specs[count++] = tmpl;
		  break;
		case PSR_IC:
		  if (rsrc_write)
		    {
		      specs[count++] = tmpl;
		    }
		  else
		    {
		      /* Only certain CR accesses use PSR.ic */
		      if (idesc->operands[0] == IA64_OPND_CR3
			  || idesc->operands[1] == IA64_OPND_CR3)
			{
			  int index =
			    ((idesc->operands[0] == IA64_OPND_CR3)
			     ? 0 : 1);
			  int regno =
			    CURR_SLOT.opnd[index].X_add_number - REG_CR;

			  switch (regno)
			    {
			    default:
			      break;
			    case CR_ITIR:
			    case CR_IFS:
			    case CR_IIM:
			    case CR_IIP:
			    case CR_IPSR:
			    case CR_ISR:
			    case CR_IFA:
			    case CR_IHA:
			    case CR_IIPA:
			      specs[count++] = tmpl;
			      break;
			    }
			}
		    }
		  break;
		case PSR_CPL:
		  if (rsrc_write)
		    {
		      specs[count++] = tmpl;
		    }
		  else
		    {
		      /* Only some AR accesses use cpl */
		      if (idesc->operands[0] == IA64_OPND_AR3
			  || idesc->operands[1] == IA64_OPND_AR3)
			{
			  int index =
			    ((idesc->operands[0] == IA64_OPND_AR3)
			     ? 0 : 1);
			  int regno =
			    CURR_SLOT.opnd[index].X_add_number - REG_AR;

			  if (regno == AR_ITC
			      || (index == 0
				  && (regno == AR_ITC
				      || regno == AR_RSC
				      || (regno >= AR_K0
					  && regno <= AR_K7))))
			    {
			      specs[count++] = tmpl;
			    }
			}
		      else
			{
			  specs[count++] = tmpl;
			}
		      break;
		    }
		}
	    }
	}
      else if (note == 7)
	{
	  valueT mask = 0;
	  if (idesc->operands[0] == IA64_OPND_IMMU24)
	    {
	      mask = CURR_SLOT.opnd[0].X_add_number;
	    }
	  else
	    {
	      UNHANDLED;
	    }
	  if (mask & ((valueT) 1 << dep->regindex))
	    {
	      specs[count++] = tmpl;
	    }
	}
      else if (note == 8)
	{
	  int min = dep->regindex == PSR_DFL ? 2 : 32;
	  int max = dep->regindex == PSR_DFL ? 31 : 127;
	  /* dfh is read on FR32-127; dfl is read on FR2-31 */
	  for (i = 0; i < NELEMS (idesc->operands); i++)
	    {
	      if (idesc->operands[i] == IA64_OPND_F1
		  || idesc->operands[i] == IA64_OPND_F2
		  || idesc->operands[i] == IA64_OPND_F3
		  || idesc->operands[i] == IA64_OPND_F4)
		{
		  int reg = CURR_SLOT.opnd[i].X_add_number - REG_FR;
		  if (reg >= min && reg <= max)
		    {
		      specs[count++] = tmpl;
		    }
		}
	    }
	}
      else if (note == 9)
	{
	  int min = dep->regindex == PSR_MFL ? 2 : 32;
	  int max = dep->regindex == PSR_MFL ? 31 : 127;
	  /* mfh is read on writes to FR32-127; mfl is read on writes to
	     FR2-31 */
	  for (i = 0; i < idesc->num_outputs; i++)
	    {
	      if (idesc->operands[i] == IA64_OPND_F1)
		{
		  int reg = CURR_SLOT.opnd[i].X_add_number - REG_FR;
		  if (reg >= min && reg <= max)
		    {
		      specs[count++] = tmpl;
		    }
		}
	    }
	}
      else if (note == 10)
	{
	  for (i = 0; i < NELEMS (idesc->operands); i++)
	    {
	      if (idesc->operands[i] == IA64_OPND_R1
		  || idesc->operands[i] == IA64_OPND_R2
		  || idesc->operands[i] == IA64_OPND_R3)
		{
		  int regno = CURR_SLOT.opnd[i].X_add_number - REG_GR;
		  if (regno >= 16 && regno <= 31)
		    {
		      specs[count++] = tmpl;
		    }
		}
	    }
	}
      else
	{
	  UNHANDLED;
	}
      break;

    case IA64_RS_AR_FPSR:
      if (idesc->operands[!rsrc_write] == IA64_OPND_AR3)
	{
	  int regno = CURR_SLOT.opnd[!rsrc_write].X_add_number - REG_AR;
	  if (regno == AR_FPSR)
	    {
	      specs[count++] = tmpl;
	    }
	}
      else
	{
	  specs[count++] = tmpl;
	}
      break;

    case IA64_RS_ARX:
      /* Handle all AR[REG] resources */
      if (note == 0 || note == 1)
	{
	  int regno = CURR_SLOT.opnd[!rsrc_write].X_add_number - REG_AR;
	  if (idesc->operands[!rsrc_write] == IA64_OPND_AR3
	      && regno == dep->regindex)
	    {
	      specs[count++] = tmpl;
	    }
	  /* other AR[REG] resources may be affected by AR accesses */
	  else if (idesc->operands[0] == IA64_OPND_AR3)
	    {
	      /* AR[] writes */
	      regno = CURR_SLOT.opnd[0].X_add_number - REG_AR;
	      switch (dep->regindex)
		{
		default:
		  break;
		case AR_BSP:
		case AR_RNAT:
		  if (regno == AR_BSPSTORE)
		    {
		      specs[count++] = tmpl;
		    }
		case AR_RSC:
		  if (!rsrc_write &&
		      (regno == AR_BSPSTORE
		       || regno == AR_RNAT))
		    {
		      specs[count++] = tmpl;
		    }
		  break;
		}
	    }
	  else if (idesc->operands[1] == IA64_OPND_AR3)
	    {
	      /* AR[] reads */
	      regno = CURR_SLOT.opnd[1].X_add_number - REG_AR;
	      switch (dep->regindex)
		{
		default:
		  break;
		case AR_RSC:
		  if (regno == AR_BSPSTORE || regno == AR_RNAT)
		    {
		      specs[count++] = tmpl;
		    }
		  break;
		}
	    }
	  else
	    {
	      specs[count++] = tmpl;
	    }
	}
      else
	{
	  UNHANDLED;
	}
      break;

    case IA64_RS_CRX:
      /* Handle all CR[REG] resources.
	 ??? FIXME: The rule 17 isn't really handled correctly.   */
      if (note == 0 || note == 1 || note == 17)
	{
	  if (idesc->operands[!rsrc_write] == IA64_OPND_CR3)
	    {
	      int regno = CURR_SLOT.opnd[!rsrc_write].X_add_number - REG_CR;
	      if (regno == dep->regindex)
		{
		  specs[count++] = tmpl;
		}
	      else if (!rsrc_write)
		{
		  /* Reads from CR[IVR] affect other resources.  */
		  if (regno == CR_IVR)
		    {
		      if ((dep->regindex >= CR_IRR0
			   && dep->regindex <= CR_IRR3)
			  || dep->regindex == CR_TPR)
			{
			  specs[count++] = tmpl;
			}
		    }
		}
	    }
	  else
	    {
	      specs[count++] = tmpl;
	    }
	}
      else
	{
	  UNHANDLED;
	}
      break;

    case IA64_RS_INSERVICE:
      /* look for write of EOI (67) or read of IVR (65) */
      if ((idesc->operands[0] == IA64_OPND_CR3
	   && CURR_SLOT.opnd[0].X_add_number - REG_CR == CR_EOI)
	  || (idesc->operands[1] == IA64_OPND_CR3
	      && CURR_SLOT.opnd[1].X_add_number - REG_CR == CR_IVR))
	{
	  specs[count++] = tmpl;
	}
      break;

    case IA64_RS_GR0:
      if (note == 1)
	{
	  specs[count++] = tmpl;
	}
      else
	{
	  UNHANDLED;
	}
      break;

    case IA64_RS_CFM:
      if (note != 2)
	{
	  specs[count++] = tmpl;
	}
      else
	{
	  /* Check if any of the registers accessed are in the rotating region.
	     mov to/from pr accesses CFM only when qp_regno is in the rotating
	     region */
	  for (i = 0; i < NELEMS (idesc->operands); i++)
	    {
	      if (idesc->operands[i] == IA64_OPND_R1
		  || idesc->operands[i] == IA64_OPND_R2
		  || idesc->operands[i] == IA64_OPND_R3)
		{
		  int num = CURR_SLOT.opnd[i].X_add_number - REG_GR;
		  /* Assumes that md.rot.num_regs is always valid */
		  if (md.rot.num_regs > 0
		      && num > 31
		      && num < 31 + md.rot.num_regs)
		    {
		      specs[count] = tmpl;
		      specs[count++].specific = 0;
		    }
		}
	      else if (idesc->operands[i] == IA64_OPND_F1
		       || idesc->operands[i] == IA64_OPND_F2
		       || idesc->operands[i] == IA64_OPND_F3
		       || idesc->operands[i] == IA64_OPND_F4)
		{
		  int num = CURR_SLOT.opnd[i].X_add_number - REG_FR;
		  if (num > 31)
		    {
		      specs[count] = tmpl;
		      specs[count++].specific = 0;
		    }
		}
	      else if (idesc->operands[i] == IA64_OPND_P1
		       || idesc->operands[i] == IA64_OPND_P2)
		{
		  int num = CURR_SLOT.opnd[i].X_add_number - REG_P;
		  if (num > 15)
		    {
		      specs[count] = tmpl;
		      specs[count++].specific = 0;
		    }
		}
	    }
	  if (CURR_SLOT.qp_regno > 15)
	    {
	      specs[count] = tmpl;
	      specs[count++].specific = 0;
	    }
	}
      break;

      /* This is the same as IA64_RS_PRr, except simplified to account for
	 the fact that there is only one register.  */
    case IA64_RS_PR63:
      if (note == 0)
	{
	  specs[count++] = tmpl;
	}
      else if (note == 7)
	{
	  valueT mask = 0;
	  if (idesc->operands[2] == IA64_OPND_IMM17)
	    mask = CURR_SLOT.opnd[2].X_add_number;
	  if (mask & ((valueT) 1 << 63))
	    specs[count++] = tmpl;
	}
      else if (note == 11)
	{
	  if ((idesc->operands[0] == IA64_OPND_P1
	       && CURR_SLOT.opnd[0].X_add_number - REG_P == 63)
	      || (idesc->operands[1] == IA64_OPND_P2
		  && CURR_SLOT.opnd[1].X_add_number - REG_P == 63))
	    {
	      specs[count++] = tmpl;
	    }
	}
      else if (note == 12)
	{
	  if (CURR_SLOT.qp_regno == 63)
	    {
	      specs[count++] = tmpl;
	    }
	}
      else if (note == 1)
	{
	  if (rsrc_write)
	    {
	      int p1 = CURR_SLOT.opnd[0].X_add_number - REG_P;
	      int p2 = CURR_SLOT.opnd[1].X_add_number - REG_P;
	      int or_andcm = strstr (idesc->name, "or.andcm") != NULL;
	      int and_orcm = strstr (idesc->name, "and.orcm") != NULL;

	      if (p1 == 63
		  && (idesc->operands[0] == IA64_OPND_P1
		      || idesc->operands[0] == IA64_OPND_P2))
		{
		  specs[count] = tmpl;
		  specs[count++].cmp_type =
		    (or_andcm ? CMP_OR : (and_orcm ? CMP_AND : CMP_NONE));
		}
	      if (p2 == 63
		  && (idesc->operands[1] == IA64_OPND_P1
		      || idesc->operands[1] == IA64_OPND_P2))
		{
		  specs[count] = tmpl;
		  specs[count++].cmp_type =
		    (or_andcm ? CMP_AND : (and_orcm ? CMP_OR : CMP_NONE));
		}
	    }
	  else
	    {
	      if (CURR_SLOT.qp_regno == 63)
		{
		  specs[count++] = tmpl;
		}
	    }
	}
      else
	{
	  UNHANDLED;
	}
      break;

    case IA64_RS_RSE:
      /* FIXME we can identify some individual RSE written resources, but RSE
	 read resources have not yet been completely identified, so for now
	 treat RSE as a single resource */
      if (strncmp (idesc->name, "mov", 3) == 0)
	{
	  if (rsrc_write)
	    {
	      if (idesc->operands[0] == IA64_OPND_AR3
		  && CURR_SLOT.opnd[0].X_add_number - REG_AR == AR_BSPSTORE)
		{
		  specs[count++] = tmpl;
		}
	    }
	  else
	    {
	      if (idesc->operands[0] == IA64_OPND_AR3)
		{
		  if (CURR_SLOT.opnd[0].X_add_number - REG_AR == AR_BSPSTORE
		      || CURR_SLOT.opnd[0].X_add_number - REG_AR == AR_RNAT)
		    {
		      specs[count++] = tmpl;
		    }
		}
	      else if (idesc->operands[1] == IA64_OPND_AR3)
		{
		  if (CURR_SLOT.opnd[1].X_add_number - REG_AR == AR_BSP
		      || CURR_SLOT.opnd[1].X_add_number - REG_AR == AR_BSPSTORE
		      || CURR_SLOT.opnd[1].X_add_number - REG_AR == AR_RNAT)
		    {
		      specs[count++] = tmpl;
		    }
		}
	    }
	}
      else
	{
	  specs[count++] = tmpl;
	}
      break;

    case IA64_RS_ANY:
      /* FIXME -- do any of these need to be non-specific? */
      specs[count++] = tmpl;
      break;

    default:
      as_bad (_("Unrecognized dependency specifier %d\n"), dep->specifier);
      break;
    }

  return count;
}

/* Clear branch flags on marked resources.  This breaks the link between the
   QP of the marking instruction and a subsequent branch on the same QP.  */

static void
clear_qp_branch_flag (mask)
     valueT mask;
{
  int i;
  for (i = 0; i < regdepslen; i++)
    {
      valueT bit = ((valueT) 1 << regdeps[i].qp_regno);
      if ((bit & mask) != 0)
	{
	  regdeps[i].link_to_qp_branch = 0;
	}
    }
}

/* MASK contains 2 and only 2 PRs which are mutually exclusive.  Remove
   any mutexes which contain one of the PRs and create new ones when
   needed.  */

static int
update_qp_mutex (valueT mask)
{
  int i;
  int add = 0;

  i = 0;
  while (i < qp_mutexeslen)
    {
      if ((qp_mutexes[i].prmask & mask) != 0)
	{
	  /* If it destroys and creates the same mutex, do nothing.  */
	  if (qp_mutexes[i].prmask == mask
	      && qp_mutexes[i].path == md.path)
	    {
	      i++;
	      add = -1;
	    }
	  else
	    {
	      int keep = 0;

	      if (md.debug_dv)
		{
		  fprintf (stderr, "  Clearing mutex relation");
		  print_prmask (qp_mutexes[i].prmask);
		  fprintf (stderr, "\n");
		}
	      
	      /* Deal with the old mutex with more than 3+ PRs only if
		 the new mutex on the same execution path with it.

		 FIXME: The 3+ mutex support is incomplete.
		 dot_pred_rel () may be a better place to fix it.  */
	      if (qp_mutexes[i].path == md.path)
		{
		  /* If it is a proper subset of the mutex, create a
		     new mutex.  */
		  if (add == 0
		      && (qp_mutexes[i].prmask & mask) == mask)
		    add = 1;
		  
		  qp_mutexes[i].prmask &= ~mask;
		  if (qp_mutexes[i].prmask & (qp_mutexes[i].prmask - 1))
		    {
		      /* Modify the mutex if there are more than one
			 PR left.  */
		      keep = 1;
		      i++;
		    }
		}
	      
	      if (keep == 0)
		/* Remove the mutex.  */
		qp_mutexes[i] = qp_mutexes[--qp_mutexeslen];
	    }
	}
      else
	++i;
    }

  if (add == 1)
    add_qp_mutex (mask);

  return add;
}

/* Remove any mutexes which contain any of the PRs indicated in the mask.

   Any changes to a PR clears the mutex relations which include that PR.  */

static void
clear_qp_mutex (mask)
     valueT mask;
{
  int i;

  i = 0;
  while (i < qp_mutexeslen)
    {
      if ((qp_mutexes[i].prmask & mask) != 0)
	{
	  if (md.debug_dv)
	    {
	      fprintf (stderr, "  Clearing mutex relation");
	      print_prmask (qp_mutexes[i].prmask);
	      fprintf (stderr, "\n");
	    }
	  qp_mutexes[i] = qp_mutexes[--qp_mutexeslen];
	}
      else
	++i;
    }
}

/* Clear implies relations which contain PRs in the given masks.
   P1_MASK indicates the source of the implies relation, while P2_MASK
   indicates the implied PR.  */

static void
clear_qp_implies (p1_mask, p2_mask)
     valueT p1_mask;
     valueT p2_mask;
{
  int i;

  i = 0;
  while (i < qp_implieslen)
    {
      if ((((valueT) 1 << qp_implies[i].p1) & p1_mask) != 0
	  || (((valueT) 1 << qp_implies[i].p2) & p2_mask) != 0)
	{
	  if (md.debug_dv)
	    fprintf (stderr, "Clearing implied relation PR%d->PR%d\n",
		     qp_implies[i].p1, qp_implies[i].p2);
	  qp_implies[i] = qp_implies[--qp_implieslen];
	}
      else
	++i;
    }
}

/* Add the PRs specified to the list of implied relations.  */

static void
add_qp_imply (p1, p2)
     int p1, p2;
{
  valueT mask;
  valueT bit;
  int i;

  /* p0 is not meaningful here.  */
  if (p1 == 0 || p2 == 0)
    abort ();

  if (p1 == p2)
    return;

  /* If it exists already, ignore it.  */
  for (i = 0; i < qp_implieslen; i++)
    {
      if (qp_implies[i].p1 == p1
	  && qp_implies[i].p2 == p2
	  && qp_implies[i].path == md.path
	  && !qp_implies[i].p2_branched)
	return;
    }

  if (qp_implieslen == qp_impliestotlen)
    {
      qp_impliestotlen += 20;
      qp_implies = (struct qp_imply *)
	xrealloc ((void *) qp_implies,
		  qp_impliestotlen * sizeof (struct qp_imply));
    }
  if (md.debug_dv)
    fprintf (stderr, "  Registering PR%d implies PR%d\n", p1, p2);
  qp_implies[qp_implieslen].p1 = p1;
  qp_implies[qp_implieslen].p2 = p2;
  qp_implies[qp_implieslen].path = md.path;
  qp_implies[qp_implieslen++].p2_branched = 0;

  /* Add in the implied transitive relations; for everything that p2 implies,
     make p1 imply that, too; for everything that implies p1, make it imply p2
     as well.  */
  for (i = 0; i < qp_implieslen; i++)
    {
      if (qp_implies[i].p1 == p2)
	add_qp_imply (p1, qp_implies[i].p2);
      if (qp_implies[i].p2 == p1)
	add_qp_imply (qp_implies[i].p1, p2);
    }
  /* Add in mutex relations implied by this implies relation; for each mutex
     relation containing p2, duplicate it and replace p2 with p1.  */
  bit = (valueT) 1 << p1;
  mask = (valueT) 1 << p2;
  for (i = 0; i < qp_mutexeslen; i++)
    {
      if (qp_mutexes[i].prmask & mask)
	add_qp_mutex ((qp_mutexes[i].prmask & ~mask) | bit);
    }
}

/* Add the PRs specified in the mask to the mutex list; this means that only
   one of the PRs can be true at any time.  PR0 should never be included in
   the mask.  */

static void
add_qp_mutex (mask)
     valueT mask;
{
  if (mask & 0x1)
    abort ();

  if (qp_mutexeslen == qp_mutexestotlen)
    {
      qp_mutexestotlen += 20;
      qp_mutexes = (struct qpmutex *)
	xrealloc ((void *) qp_mutexes,
		  qp_mutexestotlen * sizeof (struct qpmutex));
    }
  if (md.debug_dv)
    {
      fprintf (stderr, "  Registering mutex on");
      print_prmask (mask);
      fprintf (stderr, "\n");
    }
  qp_mutexes[qp_mutexeslen].path = md.path;
  qp_mutexes[qp_mutexeslen++].prmask = mask;
}

static int
has_suffix_p (name, suffix)
     const char *name;
     const char *suffix;
{
  size_t namelen = strlen (name);
  size_t sufflen = strlen (suffix);

  if (namelen <= sufflen)
    return 0;
  return strcmp (name + namelen - sufflen, suffix) == 0;
}

static void
clear_register_values ()
{
  int i;
  if (md.debug_dv)
    fprintf (stderr, "  Clearing register values\n");
  for (i = 1; i < NELEMS (gr_values); i++)
    gr_values[i].known = 0;
}

/* Keep track of register values/changes which affect DV tracking.

   optimization note: should add a flag to classes of insns where otherwise we
   have to examine a group of strings to identify them.  */

static void
note_register_values (idesc)
     struct ia64_opcode *idesc;
{
  valueT qp_changemask = 0;
  int i;

  /* Invalidate values for registers being written to.  */
  for (i = 0; i < idesc->num_outputs; i++)
    {
      if (idesc->operands[i] == IA64_OPND_R1
	  || idesc->operands[i] == IA64_OPND_R2
	  || idesc->operands[i] == IA64_OPND_R3)
	{
	  int regno = CURR_SLOT.opnd[i].X_add_number - REG_GR;
	  if (regno > 0 && regno < NELEMS (gr_values))
	    gr_values[regno].known = 0;
	}
      else if (idesc->operands[i] == IA64_OPND_R3_2)
	{
	  int regno = CURR_SLOT.opnd[i].X_add_number - REG_GR;
	  if (regno > 0 && regno < 4)
	    gr_values[regno].known = 0;
	}
      else if (idesc->operands[i] == IA64_OPND_P1
	       || idesc->operands[i] == IA64_OPND_P2)
	{
	  int regno = CURR_SLOT.opnd[i].X_add_number - REG_P;
	  qp_changemask |= (valueT) 1 << regno;
	}
      else if (idesc->operands[i] == IA64_OPND_PR)
	{
	  if (idesc->operands[2] & (valueT) 0x10000)
	    qp_changemask = ~(valueT) 0x1FFFF | idesc->operands[2];
	  else
	    qp_changemask = idesc->operands[2];
	  break;
	}
      else if (idesc->operands[i] == IA64_OPND_PR_ROT)
	{
	  if (idesc->operands[1] & ((valueT) 1 << 43))
	    qp_changemask = -((valueT) 1 << 44) | idesc->operands[1];
	  else
	    qp_changemask = idesc->operands[1];
	  qp_changemask &= ~(valueT) 0xFFFF;
	  break;
	}
    }

  /* Always clear qp branch flags on any PR change.  */
  /* FIXME there may be exceptions for certain compares.  */
  clear_qp_branch_flag (qp_changemask);

  /* Invalidate rotating registers on insns which affect RRBs in CFM.  */
  if (idesc->flags & IA64_OPCODE_MOD_RRBS)
    {
      qp_changemask |= ~(valueT) 0xFFFF;
      if (strcmp (idesc->name, "clrrrb.pr") != 0)
	{
	  for (i = 32; i < 32 + md.rot.num_regs; i++)
	    gr_values[i].known = 0;
	}
      clear_qp_mutex (qp_changemask);
      clear_qp_implies (qp_changemask, qp_changemask);
    }
  /* After a call, all register values are undefined, except those marked
     as "safe".  */
  else if (strncmp (idesc->name, "br.call", 6) == 0
	   || strncmp (idesc->name, "brl.call", 7) == 0)
    {
      /* FIXME keep GR values which are marked as "safe_across_calls"  */
      clear_register_values ();
      clear_qp_mutex (~qp_safe_across_calls);
      clear_qp_implies (~qp_safe_across_calls, ~qp_safe_across_calls);
      clear_qp_branch_flag (~qp_safe_across_calls);
    }
  else if (is_interruption_or_rfi (idesc)
	   || is_taken_branch (idesc))
    {
      clear_register_values ();
      clear_qp_mutex (~(valueT) 0);
      clear_qp_implies (~(valueT) 0, ~(valueT) 0);
    }
  /* Look for mutex and implies relations.  */
  else if ((idesc->operands[0] == IA64_OPND_P1
	    || idesc->operands[0] == IA64_OPND_P2)
	   && (idesc->operands[1] == IA64_OPND_P1
	       || idesc->operands[1] == IA64_OPND_P2))
    {
      int p1 = CURR_SLOT.opnd[0].X_add_number - REG_P;
      int p2 = CURR_SLOT.opnd[1].X_add_number - REG_P;
      valueT p1mask = (p1 != 0) ? (valueT) 1 << p1 : 0;
      valueT p2mask = (p2 != 0) ? (valueT) 1 << p2 : 0;

      /* If both PRs are PR0, we can't really do anything.  */
      if (p1 == 0 && p2 == 0)
	{
	  if (md.debug_dv)
	    fprintf (stderr, "  Ignoring PRs due to inclusion of p0\n");
	}
      /* In general, clear mutexes and implies which include P1 or P2,
	 with the following exceptions.  */
      else if (has_suffix_p (idesc->name, ".or.andcm")
	       || has_suffix_p (idesc->name, ".and.orcm"))
	{
	  clear_qp_implies (p2mask, p1mask);
	}
      else if (has_suffix_p (idesc->name, ".andcm")
	       || has_suffix_p (idesc->name, ".and"))
	{
	  clear_qp_implies (0, p1mask | p2mask);
	}
      else if (has_suffix_p (idesc->name, ".orcm")
	       || has_suffix_p (idesc->name, ".or"))
	{
	  clear_qp_mutex (p1mask | p2mask);
	  clear_qp_implies (p1mask | p2mask, 0);
	}
      else
	{
	  int added = 0;

	  clear_qp_implies (p1mask | p2mask, p1mask | p2mask);

	  /* If one of the PRs is PR0, we call clear_qp_mutex.  */
	  if (p1 == 0 || p2 == 0)
	    clear_qp_mutex (p1mask | p2mask);
	  else
	    added = update_qp_mutex (p1mask | p2mask);

	  if (CURR_SLOT.qp_regno == 0
	      || has_suffix_p (idesc->name, ".unc"))
	    {
	      if (added == 0 && p1 && p2)
		add_qp_mutex (p1mask | p2mask);
	      if (CURR_SLOT.qp_regno != 0)
		{
		  if (p1)
		    add_qp_imply (p1, CURR_SLOT.qp_regno);
		  if (p2)
		    add_qp_imply (p2, CURR_SLOT.qp_regno);
		}
	    }
	}
    }
  /* Look for mov imm insns into GRs.  */
  else if (idesc->operands[0] == IA64_OPND_R1
	   && (idesc->operands[1] == IA64_OPND_IMM22
	       || idesc->operands[1] == IA64_OPND_IMMU64)
	   && CURR_SLOT.opnd[1].X_op == O_constant
	   && (strcmp (idesc->name, "mov") == 0
	       || strcmp (idesc->name, "movl") == 0))
    {
      int regno = CURR_SLOT.opnd[0].X_add_number - REG_GR;
      if (regno > 0 && regno < NELEMS (gr_values))
	{
	  gr_values[regno].known = 1;
	  gr_values[regno].value = CURR_SLOT.opnd[1].X_add_number;
	  gr_values[regno].path = md.path;
	  if (md.debug_dv)
	    {
	      fprintf (stderr, "  Know gr%d = ", regno);
	      fprintf_vma (stderr, gr_values[regno].value);
	      fputs ("\n", stderr);
	    }
	}
    }
  /* Look for dep.z imm insns.  */
  else if (idesc->operands[0] == IA64_OPND_R1
	   && idesc->operands[1] == IA64_OPND_IMM8
	   && strcmp (idesc->name, "dep.z") == 0)
    {
      int regno = CURR_SLOT.opnd[0].X_add_number - REG_GR;
      if (regno > 0 && regno < NELEMS (gr_values))
	{
	  valueT value = CURR_SLOT.opnd[1].X_add_number;

	  if (CURR_SLOT.opnd[3].X_add_number < 64)
	    value &= ((valueT)1 << CURR_SLOT.opnd[3].X_add_number) - 1;
	  value <<= CURR_SLOT.opnd[2].X_add_number;
	  gr_values[regno].known = 1;
	  gr_values[regno].value = value;
	  gr_values[regno].path = md.path;
	  if (md.debug_dv)
	    {
	      fprintf (stderr, "  Know gr%d = ", regno);
	      fprintf_vma (stderr, gr_values[regno].value);
	      fputs ("\n", stderr);
	    }
	}
    }
  else
    {
      clear_qp_mutex (qp_changemask);
      clear_qp_implies (qp_changemask, qp_changemask);
    }
}

/* Return whether the given predicate registers are currently mutex.  */

static int
qp_mutex (p1, p2, path)
     int p1;
     int p2;
     int path;
{
  int i;
  valueT mask;

  if (p1 != p2)
    {
      mask = ((valueT) 1 << p1) | (valueT) 1 << p2;
      for (i = 0; i < qp_mutexeslen; i++)
	{
	  if (qp_mutexes[i].path >= path
	      && (qp_mutexes[i].prmask & mask) == mask)
	    return 1;
	}
    }
  return 0;
}

/* Return whether the given resource is in the given insn's list of chks
   Return 1 if the conflict is absolutely determined, 2 if it's a potential
   conflict.  */

static int
resources_match (rs, idesc, note, qp_regno, path)
     struct rsrc *rs;
     struct ia64_opcode *idesc;
     int note;
     int qp_regno;
     int path;
{
  struct rsrc specs[MAX_SPECS];
  int count;

  /* If the marked resource's qp_regno and the given qp_regno are mutex,
     we don't need to check.  One exception is note 11, which indicates that
     target predicates are written regardless of PR[qp].  */
  if (qp_mutex (rs->qp_regno, qp_regno, path)
      && note != 11)
    return 0;

  count = specify_resource (rs->dependency, idesc, DV_CHK, specs, note, path);
  while (count-- > 0)
    {
      /* UNAT checking is a bit more specific than other resources */
      if (rs->dependency->specifier == IA64_RS_AR_UNAT
	  && specs[count].mem_offset.hint
	  && rs->mem_offset.hint)
	{
	  if (rs->mem_offset.base == specs[count].mem_offset.base)
	    {
	      if (((rs->mem_offset.offset >> 3) & 0x3F) ==
		  ((specs[count].mem_offset.offset >> 3) & 0x3F))
		return 1;
	      else
		continue;
	    }
	}

      /* Skip apparent PR write conflicts where both writes are an AND or both
	 writes are an OR.  */
      if (rs->dependency->specifier == IA64_RS_PR
	  || rs->dependency->specifier == IA64_RS_PRr
	  || rs->dependency->specifier == IA64_RS_PR63)
	{
	  if (specs[count].cmp_type != CMP_NONE
	      && specs[count].cmp_type == rs->cmp_type)
	    {
	      if (md.debug_dv)
		fprintf (stderr, "  %s on parallel compare allowed (PR%d)\n",
			 dv_mode[rs->dependency->mode],
			 rs->dependency->specifier != IA64_RS_PR63 ?
			 specs[count].index : 63);
	      continue;
	    }
	  if (md.debug_dv)
	    fprintf (stderr,
		     "  %s on parallel compare conflict %s vs %s on PR%d\n",
		     dv_mode[rs->dependency->mode],
		     dv_cmp_type[rs->cmp_type],
		     dv_cmp_type[specs[count].cmp_type],
		     rs->dependency->specifier != IA64_RS_PR63 ?
		     specs[count].index : 63);

	}

      /* If either resource is not specific, conservatively assume a conflict
       */
      if (!specs[count].specific || !rs->specific)
	return 2;
      else if (specs[count].index == rs->index)
	return 1;
    }

  return 0;
}

/* Indicate an instruction group break; if INSERT_STOP is non-zero, then
   insert a stop to create the break.  Update all resource dependencies
   appropriately.  If QP_REGNO is non-zero, only apply the break to resources
   which use the same QP_REGNO and have the link_to_qp_branch flag set.
   If SAVE_CURRENT is non-zero, don't affect resources marked by the current
   instruction.  */

static void
insn_group_break (insert_stop, qp_regno, save_current)
     int insert_stop;
     int qp_regno;
     int save_current;
{
  int i;

  if (insert_stop && md.num_slots_in_use > 0)
    PREV_SLOT.end_of_insn_group = 1;

  if (md.debug_dv)
    {
      fprintf (stderr, "  Insn group break%s",
	       (insert_stop ? " (w/stop)" : ""));
      if (qp_regno != 0)
	fprintf (stderr, " effective for QP=%d", qp_regno);
      fprintf (stderr, "\n");
    }

  i = 0;
  while (i < regdepslen)
    {
      const struct ia64_dependency *dep = regdeps[i].dependency;

      if (qp_regno != 0
	  && regdeps[i].qp_regno != qp_regno)
	{
	  ++i;
	  continue;
	}

      if (save_current
	  && CURR_SLOT.src_file == regdeps[i].file
	  && CURR_SLOT.src_line == regdeps[i].line)
	{
	  ++i;
	  continue;
	}

      /* clear dependencies which are automatically cleared by a stop, or
	 those that have reached the appropriate state of insn serialization */
      if (dep->semantics == IA64_DVS_IMPLIED
	  || dep->semantics == IA64_DVS_IMPLIEDF
	  || regdeps[i].insn_srlz == STATE_SRLZ)
	{
	  print_dependency ("Removing", i);
	  regdeps[i] = regdeps[--regdepslen];
	}
      else
	{
	  if (dep->semantics == IA64_DVS_DATA
	      || dep->semantics == IA64_DVS_INSTR
	      || dep->semantics == IA64_DVS_SPECIFIC)
	    {
	      if (regdeps[i].insn_srlz == STATE_NONE)
		regdeps[i].insn_srlz = STATE_STOP;
	      if (regdeps[i].data_srlz == STATE_NONE)
		regdeps[i].data_srlz = STATE_STOP;
	    }
	  ++i;
	}
    }
}

/* Add the given resource usage spec to the list of active dependencies.  */

static void
mark_resource (idesc, dep, spec, depind, path)
     struct ia64_opcode *idesc ATTRIBUTE_UNUSED;
     const struct ia64_dependency *dep ATTRIBUTE_UNUSED;
     struct rsrc *spec;
     int depind;
     int path;
{
  if (regdepslen == regdepstotlen)
    {
      regdepstotlen += 20;
      regdeps = (struct rsrc *)
	xrealloc ((void *) regdeps,
		  regdepstotlen * sizeof (struct rsrc));
    }

  regdeps[regdepslen] = *spec;
  regdeps[regdepslen].depind = depind;
  regdeps[regdepslen].path = path;
  regdeps[regdepslen].file = CURR_SLOT.src_file;
  regdeps[regdepslen].line = CURR_SLOT.src_line;

  print_dependency ("Adding", regdepslen);

  ++regdepslen;
}

static void
print_dependency (action, depind)
     const char *action;
     int depind;
{
  if (md.debug_dv)
    {
      fprintf (stderr, "  %s %s '%s'",
	       action, dv_mode[(regdeps[depind].dependency)->mode],
	       (regdeps[depind].dependency)->name);
      if (regdeps[depind].specific && regdeps[depind].index >= 0)
	fprintf (stderr, " (%d)", regdeps[depind].index);
      if (regdeps[depind].mem_offset.hint)
	{
	  fputs (" ", stderr);
	  fprintf_vma (stderr, regdeps[depind].mem_offset.base);
	  fputs ("+", stderr);
	  fprintf_vma (stderr, regdeps[depind].mem_offset.offset);
	}
      fprintf (stderr, "\n");
    }
}

static void
instruction_serialization ()
{
  int i;
  if (md.debug_dv)
    fprintf (stderr, "  Instruction serialization\n");
  for (i = 0; i < regdepslen; i++)
    if (regdeps[i].insn_srlz == STATE_STOP)
      regdeps[i].insn_srlz = STATE_SRLZ;
}

static void
data_serialization ()
{
  int i = 0;
  if (md.debug_dv)
    fprintf (stderr, "  Data serialization\n");
  while (i < regdepslen)
    {
      if (regdeps[i].data_srlz == STATE_STOP
	  /* Note: as of 991210, all "other" dependencies are cleared by a
	     data serialization.  This might change with new tables */
	  || (regdeps[i].dependency)->semantics == IA64_DVS_OTHER)
	{
	  print_dependency ("Removing", i);
	  regdeps[i] = regdeps[--regdepslen];
	}
      else
	++i;
    }
}

/* Insert stops and serializations as needed to avoid DVs.  */

static void
remove_marked_resource (rs)
     struct rsrc *rs;
{
  switch (rs->dependency->semantics)
    {
    case IA64_DVS_SPECIFIC:
      if (md.debug_dv)
	fprintf (stderr, "Implementation-specific, assume worst case...\n");
      /* ...fall through...  */
    case IA64_DVS_INSTR:
      if (md.debug_dv)
	fprintf (stderr, "Inserting instr serialization\n");
      if (rs->insn_srlz < STATE_STOP)
	insn_group_break (1, 0, 0);
      if (rs->insn_srlz < STATE_SRLZ)
	{
	  struct slot oldslot = CURR_SLOT;
	  /* Manually jam a srlz.i insn into the stream */
	  memset (&CURR_SLOT, 0, sizeof (CURR_SLOT));
	  CURR_SLOT.user_template = -1;
	  CURR_SLOT.idesc = ia64_find_opcode ("srlz.i");
	  instruction_serialization ();
	  md.curr_slot = (md.curr_slot + 1) % NUM_SLOTS;
	  if (++md.num_slots_in_use >= NUM_SLOTS)
	    emit_one_bundle ();
	  CURR_SLOT = oldslot;
	}
      insn_group_break (1, 0, 0);
      break;
    case IA64_DVS_OTHER: /* as of rev2 (991220) of the DV tables, all
			    "other" types of DV are eliminated
			    by a data serialization */
    case IA64_DVS_DATA:
      if (md.debug_dv)
	fprintf (stderr, "Inserting data serialization\n");
      if (rs->data_srlz < STATE_STOP)
	insn_group_break (1, 0, 0);
      {
	struct slot oldslot = CURR_SLOT;
	/* Manually jam a srlz.d insn into the stream */
	memset (&CURR_SLOT, 0, sizeof (CURR_SLOT));
	CURR_SLOT.user_template = -1;
	CURR_SLOT.idesc = ia64_find_opcode ("srlz.d");
	data_serialization ();
	md.curr_slot = (md.curr_slot + 1) % NUM_SLOTS;
	if (++md.num_slots_in_use >= NUM_SLOTS)
	  emit_one_bundle ();
	CURR_SLOT = oldslot;
      }
      break;
    case IA64_DVS_IMPLIED:
    case IA64_DVS_IMPLIEDF:
      if (md.debug_dv)
	fprintf (stderr, "Inserting stop\n");
      insn_group_break (1, 0, 0);
      break;
    default:
      break;
    }
}

/* Check the resources used by the given opcode against the current dependency
   list.

   The check is run once for each execution path encountered.  In this case,
   a unique execution path is the sequence of instructions following a code
   entry point, e.g. the following has three execution paths, one starting
   at L0, one at L1, and one at L2.

   L0:     nop
   L1:     add
   L2:     add
   br.ret
*/

static void
check_dependencies (idesc)
     struct ia64_opcode *idesc;
{
  const struct ia64_opcode_dependency *opdeps = idesc->dependencies;
  int path;
  int i;

  /* Note that the number of marked resources may change within the
     loop if in auto mode.  */
  i = 0;
  while (i < regdepslen)
    {
      struct rsrc *rs = &regdeps[i];
      const struct ia64_dependency *dep = rs->dependency;
      int chkind;
      int note;
      int start_over = 0;

      if (dep->semantics == IA64_DVS_NONE
	  || (chkind = depends_on (rs->depind, idesc)) == -1)
	{
	  ++i;
	  continue;
	}

      note = NOTE (opdeps->chks[chkind]);

      /* Check this resource against each execution path seen thus far.  */
      for (path = 0; path <= md.path; path++)
	{
	  int matchtype;

	  /* If the dependency wasn't on the path being checked, ignore it.  */
	  if (rs->path < path)
	    continue;

	  /* If the QP for this insn implies a QP which has branched, don't
	     bother checking.  Ed. NOTE: I don't think this check is terribly
	     useful; what's the point of generating code which will only be
	     reached if its QP is zero?
	     This code was specifically inserted to handle the following code,
	     based on notes from Intel's DV checking code, where p1 implies p2.

		  mov r4 = 2
	     (p2) br.cond L
	     (p1) mov r4 = 7
	  */
	  if (CURR_SLOT.qp_regno != 0)
	    {
	      int skip = 0;
	      int implies;
	      for (implies = 0; implies < qp_implieslen; implies++)
		{
		  if (qp_implies[implies].path >= path
		      && qp_implies[implies].p1 == CURR_SLOT.qp_regno
		      && qp_implies[implies].p2_branched)
		    {
		      skip = 1;
		      break;
		    }
		}
	      if (skip)
		continue;
	    }

	  if ((matchtype = resources_match (rs, idesc, note,
					    CURR_SLOT.qp_regno, path)) != 0)
	    {
	      char msg[1024];
	      char pathmsg[256] = "";
	      char indexmsg[256] = "";
	      int certain = (matchtype == 1 && CURR_SLOT.qp_regno == 0);

	      if (path != 0)
		snprintf (pathmsg, sizeof (pathmsg),
			  " when entry is at label '%s'",
			 md.entry_labels[path - 1]);
	      if (matchtype == 1 && rs->index >= 0)
		snprintf (indexmsg, sizeof (indexmsg),
			  ", specific resource number is %d",
			 rs->index);
	      snprintf (msg, sizeof (msg),
			"Use of '%s' %s %s dependency '%s' (%s)%s%s",
		       idesc->name,
		       (certain ? "violates" : "may violate"),
		       dv_mode[dep->mode], dep->name,
		       dv_sem[dep->semantics],
		       pathmsg, indexmsg);

	      if (md.explicit_mode)
		{
		  as_warn ("%s", msg);
		  if (path < md.path)
		    as_warn (_("Only the first path encountering the conflict "
			       "is reported"));
		  as_warn_where (rs->file, rs->line,
				 _("This is the location of the "
				   "conflicting usage"));
		  /* Don't bother checking other paths, to avoid duplicating
		     the same warning */
		  break;
		}
	      else
		{
		  if (md.debug_dv)
		    fprintf (stderr, "%s @ %s:%d\n", msg, rs->file, rs->line);

		  remove_marked_resource (rs);

		  /* since the set of dependencies has changed, start over */
		  /* FIXME -- since we're removing dvs as we go, we
		     probably don't really need to start over...  */
		  start_over = 1;
		  break;
		}
	    }
	}
      if (start_over)
	i = 0;
      else
	++i;
    }
}

/* Register new dependencies based on the given opcode.  */

static void
mark_resources (idesc)
     struct ia64_opcode *idesc;
{
  int i;
  const struct ia64_opcode_dependency *opdeps = idesc->dependencies;
  int add_only_qp_reads = 0;

  /* A conditional branch only uses its resources if it is taken; if it is
     taken, we stop following that path.  The other branch types effectively
     *always* write their resources.  If it's not taken, register only QP
     reads.  */
  if (is_conditional_branch (idesc) || is_interruption_or_rfi (idesc))
    {
      add_only_qp_reads = 1;
    }

  if (md.debug_dv)
    fprintf (stderr, "Registering '%s' resource usage\n", idesc->name);

  for (i = 0; i < opdeps->nregs; i++)
    {
      const struct ia64_dependency *dep;
      struct rsrc specs[MAX_SPECS];
      int note;
      int path;
      int count;

      dep = ia64_find_dependency (opdeps->regs[i]);
      note = NOTE (opdeps->regs[i]);

      if (add_only_qp_reads
	  && !(dep->mode == IA64_DV_WAR
	       && (dep->specifier == IA64_RS_PR
		   || dep->specifier == IA64_RS_PRr
		   || dep->specifier == IA64_RS_PR63)))
	continue;

      count = specify_resource (dep, idesc, DV_REG, specs, note, md.path);

      while (count-- > 0)
	{
	  mark_resource (idesc, dep, &specs[count],
			 DEP (opdeps->regs[i]), md.path);
	}

      /* The execution path may affect register values, which may in turn
	 affect which indirect-access resources are accessed.  */
      switch (dep->specifier)
	{
	default:
	  break;
	case IA64_RS_CPUID:
	case IA64_RS_DBR:
	case IA64_RS_IBR:
	case IA64_RS_MSR:
	case IA64_RS_PKR:
	case IA64_RS_PMC:
	case IA64_RS_PMD:
	case IA64_RS_RR:
	  for (path = 0; path < md.path; path++)
	    {
	      count = specify_resource (dep, idesc, DV_REG, specs, note, path);
	      while (count-- > 0)
		mark_resource (idesc, dep, &specs[count],
			       DEP (opdeps->regs[i]), path);
	    }
	  break;
	}
    }
}

/* Remove dependencies when they no longer apply.  */

static void
update_dependencies (idesc)
     struct ia64_opcode *idesc;
{
  int i;

  if (strcmp (idesc->name, "srlz.i") == 0)
    {
      instruction_serialization ();
    }
  else if (strcmp (idesc->name, "srlz.d") == 0)
    {
      data_serialization ();
    }
  else if (is_interruption_or_rfi (idesc)
	   || is_taken_branch (idesc))
    {
      /* Although technically the taken branch doesn't clear dependencies
	 which require a srlz.[id], we don't follow the branch; the next
	 instruction is assumed to start with a clean slate.  */
      regdepslen = 0;
      md.path = 0;
    }
  else if (is_conditional_branch (idesc)
	   && CURR_SLOT.qp_regno != 0)
    {
      int is_call = strstr (idesc->name, ".call") != NULL;

      for (i = 0; i < qp_implieslen; i++)
	{
	  /* If the conditional branch's predicate is implied by the predicate
	     in an existing dependency, remove that dependency.  */
	  if (qp_implies[i].p2 == CURR_SLOT.qp_regno)
	    {
	      int depind = 0;
	      /* Note that this implied predicate takes a branch so that if
		 a later insn generates a DV but its predicate implies this
		 one, we can avoid the false DV warning.  */
	      qp_implies[i].p2_branched = 1;
	      while (depind < regdepslen)
		{
		  if (regdeps[depind].qp_regno == qp_implies[i].p1)
		    {
		      print_dependency ("Removing", depind);
		      regdeps[depind] = regdeps[--regdepslen];
		    }
		  else
		    ++depind;
		}
	    }
	}
      /* Any marked resources which have this same predicate should be
	 cleared, provided that the QP hasn't been modified between the
	 marking instruction and the branch.  */
      if (is_call)
	{
	  insn_group_break (0, CURR_SLOT.qp_regno, 1);
	}
      else
	{
	  i = 0;
	  while (i < regdepslen)
	    {
	      if (regdeps[i].qp_regno == CURR_SLOT.qp_regno
		  && regdeps[i].link_to_qp_branch
		  && (regdeps[i].file != CURR_SLOT.src_file
		      || regdeps[i].line != CURR_SLOT.src_line))
		{
		  /* Treat like a taken branch */
		  print_dependency ("Removing", i);
		  regdeps[i] = regdeps[--regdepslen];
		}
	      else
		++i;
	    }
	}
    }
}

/* Examine the current instruction for dependency violations.  */

static int
check_dv (idesc)
     struct ia64_opcode *idesc;
{
  if (md.debug_dv)
    {
      fprintf (stderr, "Checking %s for violations (line %d, %d/%d)\n",
	       idesc->name, CURR_SLOT.src_line,
	       idesc->dependencies->nchks,
	       idesc->dependencies->nregs);
    }

  /* Look through the list of currently marked resources; if the current
     instruction has the dependency in its chks list which uses that resource,
     check against the specific resources used.  */
  check_dependencies (idesc);

  /* Look up the instruction's regdeps (RAW writes, WAW writes, and WAR reads),
     then add them to the list of marked resources.  */
  mark_resources (idesc);

  /* There are several types of dependency semantics, and each has its own
     requirements for being cleared

     Instruction serialization (insns separated by interruption, rfi, or
     writer + srlz.i + reader, all in separate groups) clears DVS_INSTR.

     Data serialization (instruction serialization, or writer + srlz.d +
     reader, where writer and srlz.d are in separate groups) clears
     DVS_DATA. (This also clears DVS_OTHER, but that is not guaranteed to
     always be the case).

     Instruction group break (groups separated by stop, taken branch,
     interruption or rfi) clears DVS_IMPLIED and DVS_IMPLIEDF.
   */
  update_dependencies (idesc);

  /* Sometimes, knowing a register value allows us to avoid giving a false DV
     warning.  Keep track of as many as possible that are useful.  */
  note_register_values (idesc);

  /* We don't need or want this anymore.  */
  md.mem_offset.hint = 0;

  return 0;
}

/* Translate one line of assembly.  Pseudo ops and labels do not show
   here.  */
void
md_assemble (str)
     char *str;
{
  char *saved_input_line_pointer, *mnemonic;
  const struct pseudo_opcode *pdesc;
  struct ia64_opcode *idesc;
  unsigned char qp_regno;
  unsigned int flags;
  int ch;

  saved_input_line_pointer = input_line_pointer;
  input_line_pointer = str;

  /* extract the opcode (mnemonic):  */

  mnemonic = input_line_pointer;
  ch = get_symbol_end ();
  pdesc = (struct pseudo_opcode *) hash_find (md.pseudo_hash, mnemonic);
  if (pdesc)
    {
      *input_line_pointer = ch;
      (*pdesc->handler) (pdesc->arg);
      goto done;
    }

  /* Find the instruction descriptor matching the arguments.  */

  idesc = ia64_find_opcode (mnemonic);
  *input_line_pointer = ch;
  if (!idesc)
    {
      as_bad ("Unknown opcode `%s'", mnemonic);
      goto done;
    }

  idesc = parse_operands (idesc);
  if (!idesc)
    goto done;

  /* Handle the dynamic ops we can handle now:  */
  if (idesc->type == IA64_TYPE_DYN)
    {
      if (strcmp (idesc->name, "add") == 0)
	{
	  if (CURR_SLOT.opnd[2].X_op == O_register
	      && CURR_SLOT.opnd[2].X_add_number < 4)
	    mnemonic = "addl";
	  else
	    mnemonic = "adds";
	  ia64_free_opcode (idesc);
	  idesc = ia64_find_opcode (mnemonic);
	}
      else if (strcmp (idesc->name, "mov") == 0)
	{
	  enum ia64_opnd opnd1, opnd2;
	  int rop;

	  opnd1 = idesc->operands[0];
	  opnd2 = idesc->operands[1];
	  if (opnd1 == IA64_OPND_AR3)
	    rop = 0;
	  else if (opnd2 == IA64_OPND_AR3)
	    rop = 1;
	  else
	    abort ();
	  if (CURR_SLOT.opnd[rop].X_op == O_register)
	    {
	      if (ar_is_only_in_integer_unit (CURR_SLOT.opnd[rop].X_add_number))
		mnemonic = "mov.i";
	      else if (ar_is_only_in_memory_unit (CURR_SLOT.opnd[rop].X_add_number))
		mnemonic = "mov.m";
	      else
		rop = -1;
	    }
	  else
	    abort ();
	  if (rop >= 0)
	    {
	      ia64_free_opcode (idesc);
	      idesc = ia64_find_opcode (mnemonic);
	      while (idesc != NULL
		     && (idesc->operands[0] != opnd1
			 || idesc->operands[1] != opnd2))
		idesc = get_next_opcode (idesc);
	    }
	}
    }
  else if (strcmp (idesc->name, "mov.i") == 0
	   || strcmp (idesc->name, "mov.m") == 0)
    {
      enum ia64_opnd opnd1, opnd2;
      int rop;
      
      opnd1 = idesc->operands[0];
      opnd2 = idesc->operands[1];
      if (opnd1 == IA64_OPND_AR3)
	rop = 0;
      else if (opnd2 == IA64_OPND_AR3)
	rop = 1;
      else
	abort ();
      if (CURR_SLOT.opnd[rop].X_op == O_register)
	{
	  char unit = 'a';
	  if (ar_is_only_in_integer_unit (CURR_SLOT.opnd[rop].X_add_number))
	    unit = 'i';
	  else if (ar_is_only_in_memory_unit (CURR_SLOT.opnd[rop].X_add_number))
	    unit = 'm';
	  if (unit != 'a' && unit != idesc->name [4])
	    as_bad ("AR %d can only be accessed by %c-unit",
		    (int) (CURR_SLOT.opnd[rop].X_add_number - REG_AR),
		    TOUPPER (unit));
	}
    }
  else if (strcmp (idesc->name, "hint.b") == 0)
    {
      switch (md.hint_b)
	{
	case hint_b_ok:
	  break;
	case hint_b_warning:
	  as_warn ("hint.b may be treated as nop");
	  break;
	case hint_b_error:
	  as_bad ("hint.b shouldn't be used");
	  break;
	}
    }

  qp_regno = 0;
  if (md.qp.X_op == O_register)
    {
      qp_regno = md.qp.X_add_number - REG_P;
      md.qp.X_op = O_absent;
    }

  flags = idesc->flags;

  if ((flags & IA64_OPCODE_FIRST) != 0)
    {
      /* The alignment frag has to end with a stop bit only if the
	 next instruction after the alignment directive has to be
	 the first instruction in an instruction group.  */
      if (align_frag)
	{
	  while (align_frag->fr_type != rs_align_code)
	    {
	      align_frag = align_frag->fr_next;
	      if (!align_frag)
		break;
	    }
	  /* align_frag can be NULL if there are directives in
	     between.  */
	  if (align_frag && align_frag->fr_next == frag_now)
	    align_frag->tc_frag_data = 1;
	}

      insn_group_break (1, 0, 0);
    }
  align_frag = NULL;

  if ((flags & IA64_OPCODE_NO_PRED) != 0 && qp_regno != 0)
    {
      as_bad ("`%s' cannot be predicated", idesc->name);
      goto done;
    }

  /* Build the instruction.  */
  CURR_SLOT.qp_regno = qp_regno;
  CURR_SLOT.idesc = idesc;
  as_where (&CURR_SLOT.src_file, &CURR_SLOT.src_line);
  dwarf2_where (&CURR_SLOT.debug_line);

  /* Add unwind entries, if there are any.  */
  if (unwind.current_entry)
    {
      CURR_SLOT.unwind_record = unwind.current_entry;
      unwind.current_entry = NULL;
    }
  if (unwind.pending_saves)
    {
      if (unwind.pending_saves->next)
	{
	  /* Attach the next pending save to the next slot so that its
	     slot number will get set correctly.  */
	  add_unwind_entry (unwind.pending_saves->next, NOT_A_CHAR);
	  unwind.pending_saves = &unwind.pending_saves->next->r.record.p;
	}
      else
	unwind.pending_saves = NULL;
    }
  if (unwind.proc_pending.sym && S_IS_DEFINED (unwind.proc_pending.sym))
    unwind.insn = 1;

  /* Check for dependency violations.  */
  if (md.detect_dv)
    check_dv (idesc);

  md.curr_slot = (md.curr_slot + 1) % NUM_SLOTS;
  if (++md.num_slots_in_use >= NUM_SLOTS)
    emit_one_bundle ();

  if ((flags & IA64_OPCODE_LAST) != 0)
    insn_group_break (1, 0, 0);

  md.last_text_seg = now_seg;

 done:
  input_line_pointer = saved_input_line_pointer;
}

/* Called when symbol NAME cannot be found in the symbol table.
   Should be used for dynamic valued symbols only.  */

symbolS *
md_undefined_symbol (name)
     char *name ATTRIBUTE_UNUSED;
{
  return 0;
}

/* Called for any expression that can not be recognized.  When the
   function is called, `input_line_pointer' will point to the start of
   the expression.  */

void
md_operand (e)
     expressionS *e;
{
  switch (*input_line_pointer)
    {
    case '[':
      ++input_line_pointer;
      expression_and_evaluate (e);
      if (*input_line_pointer != ']')
	{
	  as_bad ("Closing bracket missing");
	  goto err;
	}
      else
	{
	  if (e->X_op != O_register
	      || e->X_add_number < REG_GR
	      || e->X_add_number > REG_GR + 127)
	    {
	      as_bad ("Index must be a general register");
	      e->X_add_number = REG_GR;
	    }

	  ++input_line_pointer;
	  e->X_op = O_index;
	}
      break;

    default:
      break;
    }
  return;

 err:
  ignore_rest_of_line ();
}

/* Return 1 if it's OK to adjust a reloc by replacing the symbol with
   a section symbol plus some offset.  For relocs involving @fptr(),
   directives we don't want such adjustments since we need to have the
   original symbol's name in the reloc.  */
int
ia64_fix_adjustable (fix)
     fixS *fix;
{
  /* Prevent all adjustments to global symbols */
  if (S_IS_EXTERNAL (fix->fx_addsy) || S_IS_WEAK (fix->fx_addsy))
    return 0;

  switch (fix->fx_r_type)
    {
    case BFD_RELOC_IA64_FPTR64I:
    case BFD_RELOC_IA64_FPTR32MSB:
    case BFD_RELOC_IA64_FPTR32LSB:
    case BFD_RELOC_IA64_FPTR64MSB:
    case BFD_RELOC_IA64_FPTR64LSB:
    case BFD_RELOC_IA64_LTOFF_FPTR22:
    case BFD_RELOC_IA64_LTOFF_FPTR64I:
      return 0;
    default:
      break;
    }

  return 1;
}

int
ia64_force_relocation (fix)
     fixS *fix;
{
  switch (fix->fx_r_type)
    {
    case BFD_RELOC_IA64_FPTR64I:
    case BFD_RELOC_IA64_FPTR32MSB:
    case BFD_RELOC_IA64_FPTR32LSB:
    case BFD_RELOC_IA64_FPTR64MSB:
    case BFD_RELOC_IA64_FPTR64LSB:

    case BFD_RELOC_IA64_LTOFF22:
    case BFD_RELOC_IA64_LTOFF64I:
    case BFD_RELOC_IA64_LTOFF_FPTR22:
    case BFD_RELOC_IA64_LTOFF_FPTR64I:
    case BFD_RELOC_IA64_PLTOFF22:
    case BFD_RELOC_IA64_PLTOFF64I:
    case BFD_RELOC_IA64_PLTOFF64MSB:
    case BFD_RELOC_IA64_PLTOFF64LSB:

    case BFD_RELOC_IA64_LTOFF22X:
    case BFD_RELOC_IA64_LDXMOV:
      return 1;

    default:
      break;
    }

  return generic_force_reloc (fix);
}

/* Decide from what point a pc-relative relocation is relative to,
   relative to the pc-relative fixup.  Er, relatively speaking.  */
long
ia64_pcrel_from_section (fix, sec)
     fixS *fix;
     segT sec;
{
  unsigned long off = fix->fx_frag->fr_address + fix->fx_where;

  if (bfd_get_section_flags (stdoutput, sec) & SEC_CODE)
    off &= ~0xfUL;

  return off;
}


/* Used to emit section-relative relocs for the dwarf2 debug data.  */
void
ia64_dwarf2_emit_offset (symbolS *symbol, unsigned int size)
{
  expressionS expr;

  expr.X_op = O_pseudo_fixup;
  expr.X_op_symbol = pseudo_func[FUNC_SEC_RELATIVE].u.sym;
  expr.X_add_number = 0;
  expr.X_add_symbol = symbol;
  emit_expr (&expr, size);
}

/* This is called whenever some data item (not an instruction) needs a
   fixup.  We pick the right reloc code depending on the byteorder
   currently in effect.  */
void
ia64_cons_fix_new (f, where, nbytes, exp)
     fragS *f;
     int where;
     int nbytes;
     expressionS *exp;
{
  bfd_reloc_code_real_type code;
  fixS *fix;

  switch (nbytes)
    {
      /* There are no reloc for 8 and 16 bit quantities, but we allow
	 them here since they will work fine as long as the expression
	 is fully defined at the end of the pass over the source file.  */
    case 1: code = BFD_RELOC_8; break;
    case 2: code = BFD_RELOC_16; break;
    case 4:
      if (target_big_endian)
	code = BFD_RELOC_IA64_DIR32MSB;
      else
	code = BFD_RELOC_IA64_DIR32LSB;
      break;

    case 8:
      /* In 32-bit mode, data8 could mean function descriptors too.  */
      if (exp->X_op == O_pseudo_fixup
	  && exp->X_op_symbol
	  && S_GET_VALUE (exp->X_op_symbol) == FUNC_IPLT_RELOC
	  && !(md.flags & EF_IA_64_ABI64))
	{
	  if (target_big_endian)
	    code = BFD_RELOC_IA64_IPLTMSB;
	  else
	    code = BFD_RELOC_IA64_IPLTLSB;
	  exp->X_op = O_symbol;
	  break;
	}
      else
	{
	  if (target_big_endian)
	    code = BFD_RELOC_IA64_DIR64MSB;
	  else
	    code = BFD_RELOC_IA64_DIR64LSB;
	  break;
	}

    case 16:
      if (exp->X_op == O_pseudo_fixup
	  && exp->X_op_symbol
	  && S_GET_VALUE (exp->X_op_symbol) == FUNC_IPLT_RELOC)
	{
	  if (target_big_endian)
	    code = BFD_RELOC_IA64_IPLTMSB;
	  else
	    code = BFD_RELOC_IA64_IPLTLSB;
	  exp->X_op = O_symbol;
	  break;
	}
      /* FALLTHRU */

    default:
      as_bad ("Unsupported fixup size %d", nbytes);
      ignore_rest_of_line ();
      return;
    }

  if (exp->X_op == O_pseudo_fixup)
    {
      exp->X_op = O_symbol;
      code = ia64_gen_real_reloc_type (exp->X_op_symbol, code);
      /* ??? If code unchanged, unsupported.  */
    }

  fix = fix_new_exp (f, where, nbytes, exp, 0, code);
  /* We need to store the byte order in effect in case we're going
     to fix an 8 or 16 bit relocation (for which there no real
     relocs available).  See md_apply_fix().  */
  fix->tc_fix_data.bigendian = target_big_endian;
}

/* Return the actual relocation we wish to associate with the pseudo
   reloc described by SYM and R_TYPE.  SYM should be one of the
   symbols in the pseudo_func array, or NULL.  */

static bfd_reloc_code_real_type
ia64_gen_real_reloc_type (sym, r_type)
     struct symbol *sym;
     bfd_reloc_code_real_type r_type;
{
  bfd_reloc_code_real_type new = 0;
  const char *type = NULL, *suffix = "";

  if (sym == NULL)
    {
      return r_type;
    }

  switch (S_GET_VALUE (sym))
    {
    case FUNC_FPTR_RELATIVE:
      switch (r_type)
	{
	case BFD_RELOC_IA64_IMM64:	new = BFD_RELOC_IA64_FPTR64I; break;
	case BFD_RELOC_IA64_DIR32MSB:	new = BFD_RELOC_IA64_FPTR32MSB; break;
	case BFD_RELOC_IA64_DIR32LSB:	new = BFD_RELOC_IA64_FPTR32LSB; break;
	case BFD_RELOC_IA64_DIR64MSB:	new = BFD_RELOC_IA64_FPTR64MSB; break;
	case BFD_RELOC_IA64_DIR64LSB:	new = BFD_RELOC_IA64_FPTR64LSB; break;
	default:			type = "FPTR"; break;
	}
      break;

    case FUNC_GP_RELATIVE:
      switch (r_type)
	{
	case BFD_RELOC_IA64_IMM22:	new = BFD_RELOC_IA64_GPREL22; break;
	case BFD_RELOC_IA64_IMM64:	new = BFD_RELOC_IA64_GPREL64I; break;
	case BFD_RELOC_IA64_DIR32MSB:	new = BFD_RELOC_IA64_GPREL32MSB; break;
	case BFD_RELOC_IA64_DIR32LSB:	new = BFD_RELOC_IA64_GPREL32LSB; break;
	case BFD_RELOC_IA64_DIR64MSB:	new = BFD_RELOC_IA64_GPREL64MSB; break;
	case BFD_RELOC_IA64_DIR64LSB:	new = BFD_RELOC_IA64_GPREL64LSB; break;
	default:			type = "GPREL"; break;
	}
      break;

    case FUNC_LT_RELATIVE:
      switch (r_type)
	{
	case BFD_RELOC_IA64_IMM22:	new = BFD_RELOC_IA64_LTOFF22; break;
	case BFD_RELOC_IA64_IMM64:	new = BFD_RELOC_IA64_LTOFF64I; break;
	default:			type = "LTOFF"; break;
	}
      break;

    case FUNC_LT_RELATIVE_X:
      switch (r_type)
	{
	case BFD_RELOC_IA64_IMM22:	new = BFD_RELOC_IA64_LTOFF22X; break;
	default:			type = "LTOFF"; suffix = "X"; break;
	}
      break;

    case FUNC_PC_RELATIVE:
      switch (r_type)
	{
	case BFD_RELOC_IA64_IMM22:	new = BFD_RELOC_IA64_PCREL22; break;
	case BFD_RELOC_IA64_IMM64:	new = BFD_RELOC_IA64_PCREL64I; break;
	case BFD_RELOC_IA64_DIR32MSB:	new = BFD_RELOC_IA64_PCREL32MSB; break;
	case BFD_RELOC_IA64_DIR32LSB:	new = BFD_RELOC_IA64_PCREL32LSB; break;
	case BFD_RELOC_IA64_DIR64MSB:	new = BFD_RELOC_IA64_PCREL64MSB; break;
	case BFD_RELOC_IA64_DIR64LSB:	new = BFD_RELOC_IA64_PCREL64LSB; break;
	default:			type = "PCREL"; break;
	}
      break;

    case FUNC_PLT_RELATIVE:
      switch (r_type)
	{
	case BFD_RELOC_IA64_IMM22:	new = BFD_RELOC_IA64_PLTOFF22; break;
	case BFD_RELOC_IA64_IMM64:	new = BFD_RELOC_IA64_PLTOFF64I; break;
	case BFD_RELOC_IA64_DIR64MSB:	new = BFD_RELOC_IA64_PLTOFF64MSB;break;
	case BFD_RELOC_IA64_DIR64LSB:	new = BFD_RELOC_IA64_PLTOFF64LSB;break;
	default:			type = "PLTOFF"; break;
	}
      break;

    case FUNC_SEC_RELATIVE:
      switch (r_type)
	{
	case BFD_RELOC_IA64_DIR32MSB:	new = BFD_RELOC_IA64_SECREL32MSB;break;
	case BFD_RELOC_IA64_DIR32LSB:	new = BFD_RELOC_IA64_SECREL32LSB;break;
	case BFD_RELOC_IA64_DIR64MSB:	new = BFD_RELOC_IA64_SECREL64MSB;break;
	case BFD_RELOC_IA64_DIR64LSB:	new = BFD_RELOC_IA64_SECREL64LSB;break;
	default:			type = "SECREL"; break;
	}
      break;

    case FUNC_SEG_RELATIVE:
      switch (r_type)
	{
	case BFD_RELOC_IA64_DIR32MSB:	new = BFD_RELOC_IA64_SEGREL32MSB;break;
	case BFD_RELOC_IA64_DIR32LSB:	new = BFD_RELOC_IA64_SEGREL32LSB;break;
	case BFD_RELOC_IA64_DIR64MSB:	new = BFD_RELOC_IA64_SEGREL64MSB;break;
	case BFD_RELOC_IA64_DIR64LSB:	new = BFD_RELOC_IA64_SEGREL64LSB;break;
	default:			type = "SEGREL"; break;
	}
      break;

    case FUNC_LTV_RELATIVE:
      switch (r_type)
	{
	case BFD_RELOC_IA64_DIR32MSB:	new = BFD_RELOC_IA64_LTV32MSB; break;
	case BFD_RELOC_IA64_DIR32LSB:	new = BFD_RELOC_IA64_LTV32LSB; break;
	case BFD_RELOC_IA64_DIR64MSB:	new = BFD_RELOC_IA64_LTV64MSB; break;
	case BFD_RELOC_IA64_DIR64LSB:	new = BFD_RELOC_IA64_LTV64LSB; break;
	default:			type = "LTV"; break;
	}
      break;

    case FUNC_LT_FPTR_RELATIVE:
      switch (r_type)
	{
	case BFD_RELOC_IA64_IMM22:
	  new = BFD_RELOC_IA64_LTOFF_FPTR22; break;
	case BFD_RELOC_IA64_IMM64:
	  new = BFD_RELOC_IA64_LTOFF_FPTR64I; break;
	case BFD_RELOC_IA64_DIR32MSB:
	  new = BFD_RELOC_IA64_LTOFF_FPTR32MSB; break;
	case BFD_RELOC_IA64_DIR32LSB:
	  new = BFD_RELOC_IA64_LTOFF_FPTR32LSB; break;
	case BFD_RELOC_IA64_DIR64MSB:
	  new = BFD_RELOC_IA64_LTOFF_FPTR64MSB; break;
	case BFD_RELOC_IA64_DIR64LSB:
	  new = BFD_RELOC_IA64_LTOFF_FPTR64LSB; break;
	default:
	  type = "LTOFF_FPTR"; break;
	}
      break;

    case FUNC_TP_RELATIVE:
      switch (r_type)
	{
	case BFD_RELOC_IA64_IMM14:      new = BFD_RELOC_IA64_TPREL14; break;
	case BFD_RELOC_IA64_IMM22:      new = BFD_RELOC_IA64_TPREL22; break;
	case BFD_RELOC_IA64_IMM64:      new = BFD_RELOC_IA64_TPREL64I; break;
	case BFD_RELOC_IA64_DIR64MSB:   new = BFD_RELOC_IA64_TPREL64MSB; break;
	case BFD_RELOC_IA64_DIR64LSB:   new = BFD_RELOC_IA64_TPREL64LSB; break;
	default:                        type = "TPREL"; break;
	}
      break;

    case FUNC_LT_TP_RELATIVE:
      switch (r_type)
	{
	case BFD_RELOC_IA64_IMM22:
	  new = BFD_RELOC_IA64_LTOFF_TPREL22; break;
	default:
	  type = "LTOFF_TPREL"; break;
	}
      break;

    case FUNC_DTP_MODULE:
      switch (r_type)
	{
	case BFD_RELOC_IA64_DIR64MSB:
	  new = BFD_RELOC_IA64_DTPMOD64MSB; break;
	case BFD_RELOC_IA64_DIR64LSB:
	  new = BFD_RELOC_IA64_DTPMOD64LSB; break;
	default:
	  type = "DTPMOD"; break;
	}
      break;

    case FUNC_LT_DTP_MODULE:
      switch (r_type)
	{
	case BFD_RELOC_IA64_IMM22:
	  new = BFD_RELOC_IA64_LTOFF_DTPMOD22; break;
	default:
	  type = "LTOFF_DTPMOD"; break;
	}
      break;

    case FUNC_DTP_RELATIVE:
      switch (r_type)
	{
	case BFD_RELOC_IA64_DIR32MSB:
	  new = BFD_RELOC_IA64_DTPREL32MSB; break;
	case BFD_RELOC_IA64_DIR32LSB:
	  new = BFD_RELOC_IA64_DTPREL32LSB; break;
	case BFD_RELOC_IA64_DIR64MSB:
	  new = BFD_RELOC_IA64_DTPREL64MSB; break;
	case BFD_RELOC_IA64_DIR64LSB:
	  new = BFD_RELOC_IA64_DTPREL64LSB; break;
	case BFD_RELOC_IA64_IMM14:
	  new = BFD_RELOC_IA64_DTPREL14; break;
	case BFD_RELOC_IA64_IMM22:
	  new = BFD_RELOC_IA64_DTPREL22; break;
	case BFD_RELOC_IA64_IMM64:
	  new = BFD_RELOC_IA64_DTPREL64I; break;
	default:
	  type = "DTPREL"; break;
	}
      break;

    case FUNC_LT_DTP_RELATIVE:
      switch (r_type)
	{
	case BFD_RELOC_IA64_IMM22:
	  new = BFD_RELOC_IA64_LTOFF_DTPREL22; break;
	default:
	  type = "LTOFF_DTPREL"; break;
	}
      break;

    case FUNC_IPLT_RELOC:
      switch (r_type)
	{
	case BFD_RELOC_IA64_IPLTMSB:    return r_type;
	case BFD_RELOC_IA64_IPLTLSB:    return r_type;
	default:                        type = "IPLT"; break;
	}
      break;

    default:
      abort ();
    }

  if (new)
    return new;
  else
    {
      int width;

      if (!type)
	abort ();
      switch (r_type)
	{
	case BFD_RELOC_IA64_DIR32MSB: width = 32; suffix = "MSB"; break;
	case BFD_RELOC_IA64_DIR32LSB: width = 32; suffix = "LSB"; break;
	case BFD_RELOC_IA64_DIR64MSB: width = 64; suffix = "MSB"; break;
	case BFD_RELOC_IA64_DIR64LSB: width = 64; suffix = "LSB"; break;
	case BFD_RELOC_UNUSED:        width = 13; break;
	case BFD_RELOC_IA64_IMM14:    width = 14; break;
	case BFD_RELOC_IA64_IMM22:    width = 22; break;
	case BFD_RELOC_IA64_IMM64:    width = 64; suffix = "I"; break;
	default:                      abort ();
	}

      /* This should be an error, but since previously there wasn't any
	 diagnostic here, dont't make it fail because of this for now.  */
      as_warn ("Cannot express %s%d%s relocation", type, width, suffix);
      return r_type;
    }
}

/* Here is where generate the appropriate reloc for pseudo relocation
   functions.  */
void
ia64_validate_fix (fix)
     fixS *fix;
{
  switch (fix->fx_r_type)
    {
    case BFD_RELOC_IA64_FPTR64I:
    case BFD_RELOC_IA64_FPTR32MSB:
    case BFD_RELOC_IA64_FPTR64LSB:
    case BFD_RELOC_IA64_LTOFF_FPTR22:
    case BFD_RELOC_IA64_LTOFF_FPTR64I:
      if (fix->fx_offset != 0)
	as_bad_where (fix->fx_file, fix->fx_line,
		      "No addend allowed in @fptr() relocation");
      break;
    default:
      break;
    }
}

static void
fix_insn (fix, odesc, value)
     fixS *fix;
     const struct ia64_operand *odesc;
     valueT value;
{
  bfd_vma insn[3], t0, t1, control_bits;
  const char *err;
  char *fixpos;
  long slot;

  slot = fix->fx_where & 0x3;
  fixpos = fix->fx_frag->fr_literal + (fix->fx_where - slot);

  /* Bundles are always in little-endian byte order */
  t0 = bfd_getl64 (fixpos);
  t1 = bfd_getl64 (fixpos + 8);
  control_bits = t0 & 0x1f;
  insn[0] = (t0 >>  5) & 0x1ffffffffffLL;
  insn[1] = ((t0 >> 46) & 0x3ffff) | ((t1 & 0x7fffff) << 18);
  insn[2] = (t1 >> 23) & 0x1ffffffffffLL;

  err = NULL;
  if (odesc - elf64_ia64_operands == IA64_OPND_IMMU64)
    {
      insn[1] = (value >> 22) & 0x1ffffffffffLL;
      insn[2] |= (((value & 0x7f) << 13)
		  | (((value >> 7) & 0x1ff) << 27)
		  | (((value >> 16) & 0x1f) << 22)
		  | (((value >> 21) & 0x1) << 21)
		  | (((value >> 63) & 0x1) << 36));
    }
  else if (odesc - elf64_ia64_operands == IA64_OPND_IMMU62)
    {
      if (value & ~0x3fffffffffffffffULL)
	err = "integer operand out of range";
      insn[1] = (value >> 21) & 0x1ffffffffffLL;
      insn[2] |= (((value & 0xfffff) << 6) | (((value >> 20) & 0x1) << 36));
    }
  else if (odesc - elf64_ia64_operands == IA64_OPND_TGT64)
    {
      value >>= 4;
      insn[1] = ((value >> 20) & 0x7fffffffffLL) << 2;
      insn[2] |= ((((value >> 59) & 0x1) << 36)
		  | (((value >> 0) & 0xfffff) << 13));
    }
  else
    err = (*odesc->insert) (odesc, value, insn + slot);

  if (err)
    as_bad_where (fix->fx_file, fix->fx_line, "%s", err);

  t0 = control_bits | (insn[0] << 5) | (insn[1] << 46);
  t1 = ((insn[1] >> 18) & 0x7fffff) | (insn[2] << 23);
  number_to_chars_littleendian (fixpos + 0, t0, 8);
  number_to_chars_littleendian (fixpos + 8, t1, 8);
}

/* Attempt to simplify or even eliminate a fixup.  The return value is
   ignored; perhaps it was once meaningful, but now it is historical.
   To indicate that a fixup has been eliminated, set FIXP->FX_DONE.

   If fixp->fx_addsy is non-NULL, we'll have to generate a reloc entry
   (if possible).  */

void
md_apply_fix (fix, valP, seg)
     fixS *fix;
     valueT *valP;
     segT seg ATTRIBUTE_UNUSED;
{
  char *fixpos;
  valueT value = *valP;

  fixpos = fix->fx_frag->fr_literal + fix->fx_where;

  if (fix->fx_pcrel)
    {
    switch (fix->fx_r_type)
      {
      case BFD_RELOC_IA64_PCREL21B: break;
      case BFD_RELOC_IA64_PCREL21BI: break;
      case BFD_RELOC_IA64_PCREL21F: break;
      case BFD_RELOC_IA64_PCREL21M: break;
      case BFD_RELOC_IA64_PCREL60B: break;
      case BFD_RELOC_IA64_PCREL22: break;
      case BFD_RELOC_IA64_PCREL64I: break;
      case BFD_RELOC_IA64_PCREL32MSB: break;
      case BFD_RELOC_IA64_PCREL32LSB: break;
      case BFD_RELOC_IA64_PCREL64MSB: break;
      case BFD_RELOC_IA64_PCREL64LSB: break;
      default:
	fix->fx_r_type = ia64_gen_real_reloc_type (pseudo_func[FUNC_PC_RELATIVE].u.sym,
					       fix->fx_r_type);
	break;
      }
    }
  if (fix->fx_addsy)
    {
      switch (fix->fx_r_type)
	{
	case BFD_RELOC_UNUSED:
	  /* This must be a TAG13 or TAG13b operand.  There are no external
	     relocs defined for them, so we must give an error.  */
	  as_bad_where (fix->fx_file, fix->fx_line,
			"%s must have a constant value",
			elf64_ia64_operands[fix->tc_fix_data.opnd].desc);
	  fix->fx_done = 1;
	  return;

	case BFD_RELOC_IA64_TPREL14:
	case BFD_RELOC_IA64_TPREL22:
	case BFD_RELOC_IA64_TPREL64I:
	case BFD_RELOC_IA64_LTOFF_TPREL22:
	case BFD_RELOC_IA64_LTOFF_DTPMOD22:
	case BFD_RELOC_IA64_DTPREL14:
	case BFD_RELOC_IA64_DTPREL22:
	case BFD_RELOC_IA64_DTPREL64I:
	case BFD_RELOC_IA64_LTOFF_DTPREL22:
	  S_SET_THREAD_LOCAL (fix->fx_addsy);
	  break;

	default:
	  break;
	}
    }
  else if (fix->tc_fix_data.opnd == IA64_OPND_NIL)
    {
      if (fix->tc_fix_data.bigendian)
	number_to_chars_bigendian (fixpos, value, fix->fx_size);
      else
	number_to_chars_littleendian (fixpos, value, fix->fx_size);
      fix->fx_done = 1;
    }
  else
    {
      fix_insn (fix, elf64_ia64_operands + fix->tc_fix_data.opnd, value);
      fix->fx_done = 1;
    }
}

/* Generate the BFD reloc to be stuck in the object file from the
   fixup used internally in the assembler.  */

arelent *
tc_gen_reloc (sec, fixp)
     asection *sec ATTRIBUTE_UNUSED;
     fixS *fixp;
{
  arelent *reloc;

  reloc = xmalloc (sizeof (*reloc));
  reloc->sym_ptr_ptr = (asymbol **) xmalloc (sizeof (asymbol *));
  *reloc->sym_ptr_ptr = symbol_get_bfdsym (fixp->fx_addsy);
  reloc->address = fixp->fx_frag->fr_address + fixp->fx_where;
  reloc->addend = fixp->fx_offset;
  reloc->howto = bfd_reloc_type_lookup (stdoutput, fixp->fx_r_type);

  if (!reloc->howto)
    {
      as_bad_where (fixp->fx_file, fixp->fx_line,
		    "Cannot represent %s relocation in object file",
		    bfd_get_reloc_code_name (fixp->fx_r_type));
    }
  return reloc;
}

/* Turn a string in input_line_pointer into a floating point constant
   of type TYPE, and store the appropriate bytes in *LIT.  The number
   of LITTLENUMS emitted is stored in *SIZE.  An error message is
   returned, or NULL on OK.  */

#define MAX_LITTLENUMS 5

char *
md_atof (type, lit, size)
     int type;
     char *lit;
     int *size;
{
  LITTLENUM_TYPE words[MAX_LITTLENUMS];
  char *t;
  int prec;

  switch (type)
    {
      /* IEEE floats */
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
    case 'p':
    case 'P':
      prec = 5;
      break;

    default:
      *size = 0;
      return "Bad call to MD_ATOF()";
    }
  t = atof_ieee (input_line_pointer, type, words);
  if (t)
    input_line_pointer = t;

  (*ia64_float_to_chars) (lit, words, prec);

  if (type == 'X')
    {
      /* It is 10 byte floating point with 6 byte padding.  */
      memset (&lit [10], 0, 6);
      *size = 8 * sizeof (LITTLENUM_TYPE);
    }
  else
    *size = prec * sizeof (LITTLENUM_TYPE);

  return 0;
}

/* Handle ia64 specific semantics of the align directive.  */

void
ia64_md_do_align (n, fill, len, max)
     int n ATTRIBUTE_UNUSED;
     const char *fill ATTRIBUTE_UNUSED;
     int len ATTRIBUTE_UNUSED;
     int max ATTRIBUTE_UNUSED;
{
  if (subseg_text_p (now_seg))
    ia64_flush_insns ();
}

/* This is called from HANDLE_ALIGN in write.c.  Fill in the contents
   of an rs_align_code fragment.  */

void
ia64_handle_align (fragp)
     fragS *fragp;
{
  int bytes;
  char *p;
  const unsigned char *nop;

  if (fragp->fr_type != rs_align_code)
    return;

  /* Check if this frag has to end with a stop bit.  */
  nop = fragp->tc_frag_data ? le_nop_stop : le_nop;

  bytes = fragp->fr_next->fr_address - fragp->fr_address - fragp->fr_fix;
  p = fragp->fr_literal + fragp->fr_fix;

  /* If no paddings are needed, we check if we need a stop bit.  */ 
  if (!bytes && fragp->tc_frag_data)
    {
      if (fragp->fr_fix < 16)
#if 1
	/* FIXME: It won't work with
	   .align 16
	   alloc r32=ar.pfs,1,2,4,0
	 */
	;
#else
	as_bad_where (fragp->fr_file, fragp->fr_line,
		      _("Can't add stop bit to mark end of instruction group"));
#endif
      else
	/* Bundles are always in little-endian byte order. Make sure
	   the previous bundle has the stop bit.  */
	*(p - 16) |= 1;
    }

  /* Make sure we are on a 16-byte boundary, in case someone has been
     putting data into a text section.  */
  if (bytes & 15)
    {
      int fix = bytes & 15;
      memset (p, 0, fix);
      p += fix;
      bytes -= fix;
      fragp->fr_fix += fix;
    }

  /* Instruction bundles are always little-endian.  */
  memcpy (p, nop, 16);
  fragp->fr_var = 16;
}

static void
ia64_float_to_chars_bigendian (char *lit, LITTLENUM_TYPE *words,
			       int prec)
{
  while (prec--)
    {
      number_to_chars_bigendian (lit, (long) (*words++),
				 sizeof (LITTLENUM_TYPE));
      lit += sizeof (LITTLENUM_TYPE);
    }
}

static void
ia64_float_to_chars_littleendian (char *lit, LITTLENUM_TYPE *words,
				  int prec)
{
  while (prec--)
    {
      number_to_chars_littleendian (lit, (long) (words[prec]),
				    sizeof (LITTLENUM_TYPE));
      lit += sizeof (LITTLENUM_TYPE);
    }
}

void
ia64_elf_section_change_hook  (void)
{
  if (elf_section_type (now_seg) == SHT_IA_64_UNWIND
      && elf_linked_to_section (now_seg) == NULL)
    elf_linked_to_section (now_seg) = text_section;
  dot_byteorder (-1);
}

/* Check if a label should be made global.  */
void
ia64_check_label (symbolS *label)
{
  if (*input_line_pointer == ':')
    {
      S_SET_EXTERNAL (label);
      input_line_pointer++;
    }
}

/* Used to remember where .alias and .secalias directives are seen. We
   will rename symbol and section names when we are about to output
   the relocatable file.  */
struct alias
{
  char *file;		/* The file where the directive is seen.  */
  unsigned int line;	/* The line number the directive is at.  */
  const char *name;	/* The original name of the symbol.  */
};

/* Called for .alias and .secalias directives. If SECTION is 1, it is
   .secalias. Otherwise, it is .alias.  */
static void
dot_alias (int section)
{
  char *name, *alias;
  char delim;
  char *end_name;
  int len;
  const char *error_string;
  struct alias *h;
  const char *a;
  struct hash_control *ahash, *nhash;
  const char *kind;

  name = input_line_pointer;
  delim = get_symbol_end ();
  end_name = input_line_pointer;
  *end_name = delim;

  if (name == end_name)
    {
      as_bad (_("expected symbol name"));
      ignore_rest_of_line ();
      return;
    }

  SKIP_WHITESPACE ();

  if (*input_line_pointer != ',')
    {
      *end_name = 0;
      as_bad (_("expected comma after \"%s\""), name);
      *end_name = delim;
      ignore_rest_of_line ();
      return;
    }

  input_line_pointer++;
  *end_name = 0;
  ia64_canonicalize_symbol_name (name);

  /* We call demand_copy_C_string to check if alias string is valid.
     There should be a closing `"' and no `\0' in the string.  */
  alias = demand_copy_C_string (&len);
  if (alias == NULL)
    {
      ignore_rest_of_line ();
      return;
    }

  /* Make a copy of name string.  */
  len = strlen (name) + 1;
  obstack_grow (&notes, name, len);
  name = obstack_finish (&notes);

  if (section)
    {
      kind = "section";
      ahash = secalias_hash;
      nhash = secalias_name_hash;
    }
  else
    {
      kind = "symbol";
      ahash = alias_hash;
      nhash = alias_name_hash;
    }

  /* Check if alias has been used before.  */
  h = (struct alias *) hash_find (ahash, alias);
  if (h)
    {
      if (strcmp (h->name, name))
	as_bad (_("`%s' is already the alias of %s `%s'"),
		alias, kind, h->name);
      goto out;
    }

  /* Check if name already has an alias.  */
  a = (const char *) hash_find (nhash, name);
  if (a)
    {
      if (strcmp (a, alias))
	as_bad (_("%s `%s' already has an alias `%s'"), kind, name, a);
      goto out;
    }

  h = (struct alias *) xmalloc (sizeof (struct alias));
  as_where (&h->file, &h->line);
  h->name = name;
  
  error_string = hash_jam (ahash, alias, (PTR) h);
  if (error_string)
    {
      as_fatal (_("inserting \"%s\" into %s alias hash table failed: %s"),
		alias, kind, error_string);
      goto out;
    }

  error_string = hash_jam (nhash, name, (PTR) alias);
  if (error_string)
    {
      as_fatal (_("inserting \"%s\" into %s name hash table failed: %s"),
		alias, kind, error_string);
out:
      obstack_free (&notes, name);
      obstack_free (&notes, alias);
    }

  demand_empty_rest_of_line ();
}

/* It renames the original symbol name to its alias.  */
static void
do_alias (const char *alias, PTR value)
{
  struct alias *h = (struct alias *) value;
  symbolS *sym = symbol_find (h->name);

  if (sym == NULL)
    as_warn_where (h->file, h->line,
		   _("symbol `%s' aliased to `%s' is not used"),
		   h->name, alias);
    else
      S_SET_NAME (sym, (char *) alias);
}

/* Called from write_object_file.  */
void
ia64_adjust_symtab (void)
{
  hash_traverse (alias_hash, do_alias);
}

/* It renames the original section name to its alias.  */
static void
do_secalias (const char *alias, PTR value)
{
  struct alias *h = (struct alias *) value;
  segT sec = bfd_get_section_by_name (stdoutput, h->name);

  if (sec == NULL)
    as_warn_where (h->file, h->line,
		   _("section `%s' aliased to `%s' is not used"),
		   h->name, alias);
  else
    sec->name = alias;
}

/* Called from write_object_file.  */
void
ia64_frob_file (void)
{
  hash_traverse (secalias_hash, do_secalias);
}
