/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2000, 2002-2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * Kernel unwind support.
 */

#define UNW_VER(x)		((x) >> 48)
#define UNW_FLAG_MASK		0x0000ffff00000000
#define UNW_FLAG_OSMASK		0x0000f00000000000
#define UNW_FLAG_EHANDLER(x)	((x) & 0x0000000100000000L)
#define UNW_FLAG_UHANDLER(x)	((x) & 0x0000000200000000L)
#define UNW_LENGTH(x)		((x) & 0x00000000ffffffffL)

enum unw_register_index {
	/* primary unat: */
	UNW_REG_PRI_UNAT_GR,
	UNW_REG_PRI_UNAT_MEM,

	/* register stack */
	UNW_REG_BSP,					/* register stack pointer */
	UNW_REG_BSPSTORE,
	UNW_REG_PFS,					/* previous function state */
	UNW_REG_RNAT,
	/* memory stack */
	UNW_REG_PSP,					/* previous memory stack pointer */
	/* return pointer: */
	UNW_REG_RP,

	/* preserved registers: */
	UNW_REG_R4, UNW_REG_R5, UNW_REG_R6, UNW_REG_R7,
	UNW_REG_UNAT, UNW_REG_PR, UNW_REG_LC, UNW_REG_FPSR,
	UNW_REG_B1, UNW_REG_B2, UNW_REG_B3, UNW_REG_B4, UNW_REG_B5,
	UNW_REG_F2, UNW_REG_F3, UNW_REG_F4, UNW_REG_F5,
	UNW_REG_F16, UNW_REG_F17, UNW_REG_F18, UNW_REG_F19,
	UNW_REG_F20, UNW_REG_F21, UNW_REG_F22, UNW_REG_F23,
	UNW_REG_F24, UNW_REG_F25, UNW_REG_F26, UNW_REG_F27,
	UNW_REG_F28, UNW_REG_F29, UNW_REG_F30, UNW_REG_F31,
	UNW_NUM_REGS
};

struct unw_info_block {
	u64 header;
	u64 desc[0];		/* unwind descriptors */
	/* personality routine and language-specific data follow behind descriptors */
};

struct unw_table {
	struct unw_table *next;		/* must be first member! */
	const char *name;
	unsigned long gp;		/* global pointer for this load-module */
	unsigned long segment_base;	/* base for offsets in the unwind table entries */
	unsigned long start;
	unsigned long end;
	const struct unw_table_entry *array;
	unsigned long length;
};

enum unw_where {
	UNW_WHERE_NONE,			/* register isn't saved at all */
	UNW_WHERE_GR,			/* register is saved in a general register */
	UNW_WHERE_FR,			/* register is saved in a floating-point register */
	UNW_WHERE_BR,			/* register is saved in a branch register */
	UNW_WHERE_SPREL,		/* register is saved on memstack (sp-relative) */
	UNW_WHERE_PSPREL,		/* register is saved on memstack (psp-relative) */
	/*
	 * At the end of each prologue these locations get resolved to
	 * UNW_WHERE_PSPREL and UNW_WHERE_GR, respectively:
	 */
	UNW_WHERE_SPILL_HOME,		/* register is saved in its spill home */
	UNW_WHERE_GR_SAVE		/* register is saved in next general register */
};

#define UNW_WHEN_NEVER	0x7fffffff

struct unw_reg_info {
	unsigned long val;		/* save location: register number or offset */
	enum unw_where where;		/* where the register gets saved */
	int when;			/* when the register gets saved */
};

struct unw_reg_state {
	struct unw_reg_state *next;		/* next (outer) element on state stack */
	struct unw_reg_info reg[UNW_NUM_REGS];	/* register save locations */
};

struct unw_labeled_state {
	struct unw_labeled_state *next;		/* next labeled state (or NULL) */
	unsigned long label;			/* label for this state */
	struct unw_reg_state saved_state;
};

struct unw_state_record {
	unsigned int first_region : 1;	/* is this the first region? */
	unsigned int done : 1;		/* are we done scanning descriptors? */
	unsigned int any_spills : 1;	/* got any register spills? */
	unsigned int in_body : 1;	/* are we inside a body (as opposed to a prologue)? */
	unsigned long flags;		/* see UNW_FLAG_* in unwind.h */

	u8 *imask;			/* imask of spill_mask record or NULL */
	unsigned long pr_val;		/* predicate values */
	unsigned long pr_mask;		/* predicate mask */
	long spill_offset;		/* psp-relative offset for spill base */
	int region_start;
	int region_len;
	int epilogue_start;
	int epilogue_count;
	int when_target;

	u8 gr_save_loc;			/* next general register to use for saving a register */
	u8 return_link_reg;		/* branch register in which the return link is passed */

	struct unw_labeled_state *labeled_states;	/* list of all labeled states */
	struct unw_reg_state curr;	/* current state */
};

enum unw_nat_type {
	UNW_NAT_NONE,		/* NaT not represented */
	UNW_NAT_VAL,		/* NaT represented by NaT value (fp reg) */
	UNW_NAT_MEMSTK,		/* NaT value is in unat word at offset OFF  */
	UNW_NAT_REGSTK		/* NaT is in rnat */
};

enum unw_insn_opcode {
	UNW_INSN_ADD,			/* s[dst] += val */
	UNW_INSN_ADD_PSP,		/* s[dst] = (s.psp + val) */
	UNW_INSN_ADD_SP,		/* s[dst] = (s.sp + val) */
	UNW_INSN_MOVE,			/* s[dst] = s[val] */
	UNW_INSN_MOVE2,			/* s[dst] = s[val]; s[dst+1] = s[val+1] */
	UNW_INSN_MOVE_STACKED,		/* s[dst] = ia64_rse_skip(*s.bsp, val) */
	UNW_INSN_SETNAT_MEMSTK,		/* s[dst+1].nat.type = MEMSTK;
					   s[dst+1].nat.off = *s.pri_unat - s[dst] */
	UNW_INSN_SETNAT_TYPE,		/* s[dst+1].nat.type = val */
	UNW_INSN_LOAD,			/* s[dst] = *s[val] */
	UNW_INSN_MOVE_SCRATCH,		/* s[dst] = scratch reg "val" */
	UNW_INSN_MOVE_CONST,            /* s[dst] = constant reg "val" */
};

struct unw_insn {
	unsigned int opc	:  4;
	unsigned int dst	:  9;
	signed int val		: 19;
};

/*
 * Preserved general static registers (r4-r7) give rise to two script
 * instructions; everything else yields at most one instruction; at
 * the end of the script, the psp gets popped, accounting for one more
 * instruction.
 */
#define UNW_MAX_SCRIPT_LEN	(UNW_NUM_REGS + 5)

struct unw_script {
	unsigned long ip;		/* ip this script is for */
	unsigned long pr_mask;		/* mask of predicates script depends on */
	unsigned long pr_val;		/* predicate values this script is for */
	rwlock_t lock;
	unsigned int flags;		/* see UNW_FLAG_* in unwind.h */
	unsigned short lru_chain;	/* used for least-recently-used chain */
	unsigned short coll_chain;	/* used for hash collisions */
	unsigned short hint;		/* hint for next script to try (or -1) */
	unsigned short count;		/* number of instructions in script */
	struct unw_insn insn[UNW_MAX_SCRIPT_LEN];
};
