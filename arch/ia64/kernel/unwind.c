/*
 * Copyright (C) 1999-2004 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 2003 Fenghua Yu <fenghua.yu@intel.com>
 * 	- Change pt_regs_off() to make it less dependent on pt_regs structure.
 */
/*
 * This file implements call frame unwind support for the Linux
 * kernel.  Parsing and processing the unwind information is
 * time-consuming, so this implementation translates the unwind
 * descriptors into unwind scripts.  These scripts are very simple
 * (basically a sequence of assignments) and efficient to execute.
 * They are cached for later re-use.  Each script is specific for a
 * given instruction pointer address and the set of predicate values
 * that the script depends on (most unwind descriptors are
 * unconditional and scripts often do not depend on predicates at
 * all).  This code is based on the unwind conventions described in
 * the "IA-64 Software Conventions and Runtime Architecture" manual.
 *
 * SMP conventions:
 *	o updates to the global unwind data (in structure "unw") are serialized
 *	  by the unw.lock spinlock
 *	o each unwind script has its own read-write lock; a thread must acquire
 *	  a read lock before executing a script and must acquire a write lock
 *	  before modifying a script
 *	o if both the unw.lock spinlock and a script's read-write lock must be
 *	  acquired, then the read-write lock must be acquired first.
 */
#include <linux/module.h>
#include <linux/bootmem.h>
#include <linux/elf.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/slab.h>

#include <asm/unwind.h>

#include <asm/delay.h>
#include <asm/page.h>
#include <asm/ptrace.h>
#include <asm/ptrace_offsets.h>
#include <asm/rse.h>
#include <asm/sections.h>
#include <asm/system.h>
#include <asm/uaccess.h>

#include "entry.h"
#include "unwind_i.h"

#define UNW_LOG_CACHE_SIZE	7	/* each unw_script is ~256 bytes in size */
#define UNW_CACHE_SIZE		(1 << UNW_LOG_CACHE_SIZE)

#define UNW_LOG_HASH_SIZE	(UNW_LOG_CACHE_SIZE + 1)
#define UNW_HASH_SIZE		(1 << UNW_LOG_HASH_SIZE)

#define UNW_STATS	0	/* WARNING: this disabled interrupts for long time-spans!! */

#ifdef UNW_DEBUG
  static unsigned int unw_debug_level = UNW_DEBUG;
#  define UNW_DEBUG_ON(n)	unw_debug_level >= n
   /* Do not code a printk level, not all debug lines end in newline */
#  define UNW_DPRINT(n, ...)  if (UNW_DEBUG_ON(n)) printk(__VA_ARGS__)
#  undef inline
#  define inline
#else /* !UNW_DEBUG */
#  define UNW_DEBUG_ON(n)  0
#  define UNW_DPRINT(n, ...)
#endif /* UNW_DEBUG */

#if UNW_STATS
# define STAT(x...)	x
#else
# define STAT(x...)
#endif

#define alloc_reg_state()	kmalloc(sizeof(struct unw_reg_state), GFP_ATOMIC)
#define free_reg_state(usr)	kfree(usr)
#define alloc_labeled_state()	kmalloc(sizeof(struct unw_labeled_state), GFP_ATOMIC)
#define free_labeled_state(usr)	kfree(usr)

typedef unsigned long unw_word;
typedef unsigned char unw_hash_index_t;

static struct {
	spinlock_t lock;			/* spinlock for unwind data */

	/* list of unwind tables (one per load-module) */
	struct unw_table *tables;

	unsigned long r0;			/* constant 0 for r0 */

	/* table of registers that prologues can save (and order in which they're saved): */
	const unsigned char save_order[8];

	/* maps a preserved register index (preg_index) to corresponding switch_stack offset: */
	unsigned short sw_off[sizeof(struct unw_frame_info) / 8];

	unsigned short lru_head;		/* index of lead-recently used script */
	unsigned short lru_tail;		/* index of most-recently used script */

	/* index into unw_frame_info for preserved register i */
	unsigned short preg_index[UNW_NUM_REGS];

	short pt_regs_offsets[32];

	/* unwind table for the kernel: */
	struct unw_table kernel_table;

	/* unwind table describing the gate page (kernel code that is mapped into user space): */
	size_t gate_table_size;
	unsigned long *gate_table;

	/* hash table that maps instruction pointer to script index: */
	unsigned short hash[UNW_HASH_SIZE];

	/* script cache: */
	struct unw_script cache[UNW_CACHE_SIZE];

# ifdef UNW_DEBUG
	const char *preg_name[UNW_NUM_REGS];
# endif
# if UNW_STATS
	struct {
		struct {
			int lookups;
			int hinted_hits;
			int normal_hits;
			int collision_chain_traversals;
		} cache;
		struct {
			unsigned long build_time;
			unsigned long run_time;
			unsigned long parse_time;
			int builds;
			int news;
			int collisions;
			int runs;
		} script;
		struct {
			unsigned long init_time;
			unsigned long unwind_time;
			int inits;
			int unwinds;
		} api;
	} stat;
# endif
} unw = {
	.tables = &unw.kernel_table,
	.lock = __SPIN_LOCK_UNLOCKED(unw.lock),
	.save_order = {
		UNW_REG_RP, UNW_REG_PFS, UNW_REG_PSP, UNW_REG_PR,
		UNW_REG_UNAT, UNW_REG_LC, UNW_REG_FPSR, UNW_REG_PRI_UNAT_GR
	},
	.preg_index = {
		offsetof(struct unw_frame_info, pri_unat_loc)/8,	/* PRI_UNAT_GR */
		offsetof(struct unw_frame_info, pri_unat_loc)/8,	/* PRI_UNAT_MEM */
		offsetof(struct unw_frame_info, bsp_loc)/8,
		offsetof(struct unw_frame_info, bspstore_loc)/8,
		offsetof(struct unw_frame_info, pfs_loc)/8,
		offsetof(struct unw_frame_info, rnat_loc)/8,
		offsetof(struct unw_frame_info, psp)/8,
		offsetof(struct unw_frame_info, rp_loc)/8,
		offsetof(struct unw_frame_info, r4)/8,
		offsetof(struct unw_frame_info, r5)/8,
		offsetof(struct unw_frame_info, r6)/8,
		offsetof(struct unw_frame_info, r7)/8,
		offsetof(struct unw_frame_info, unat_loc)/8,
		offsetof(struct unw_frame_info, pr_loc)/8,
		offsetof(struct unw_frame_info, lc_loc)/8,
		offsetof(struct unw_frame_info, fpsr_loc)/8,
		offsetof(struct unw_frame_info, b1_loc)/8,
		offsetof(struct unw_frame_info, b2_loc)/8,
		offsetof(struct unw_frame_info, b3_loc)/8,
		offsetof(struct unw_frame_info, b4_loc)/8,
		offsetof(struct unw_frame_info, b5_loc)/8,
		offsetof(struct unw_frame_info, f2_loc)/8,
		offsetof(struct unw_frame_info, f3_loc)/8,
		offsetof(struct unw_frame_info, f4_loc)/8,
		offsetof(struct unw_frame_info, f5_loc)/8,
		offsetof(struct unw_frame_info, fr_loc[16 - 16])/8,
		offsetof(struct unw_frame_info, fr_loc[17 - 16])/8,
		offsetof(struct unw_frame_info, fr_loc[18 - 16])/8,
		offsetof(struct unw_frame_info, fr_loc[19 - 16])/8,
		offsetof(struct unw_frame_info, fr_loc[20 - 16])/8,
		offsetof(struct unw_frame_info, fr_loc[21 - 16])/8,
		offsetof(struct unw_frame_info, fr_loc[22 - 16])/8,
		offsetof(struct unw_frame_info, fr_loc[23 - 16])/8,
		offsetof(struct unw_frame_info, fr_loc[24 - 16])/8,
		offsetof(struct unw_frame_info, fr_loc[25 - 16])/8,
		offsetof(struct unw_frame_info, fr_loc[26 - 16])/8,
		offsetof(struct unw_frame_info, fr_loc[27 - 16])/8,
		offsetof(struct unw_frame_info, fr_loc[28 - 16])/8,
		offsetof(struct unw_frame_info, fr_loc[29 - 16])/8,
		offsetof(struct unw_frame_info, fr_loc[30 - 16])/8,
		offsetof(struct unw_frame_info, fr_loc[31 - 16])/8,
	},
	.pt_regs_offsets = {
		[0] = -1,
		offsetof(struct pt_regs,  r1),
		offsetof(struct pt_regs,  r2),
		offsetof(struct pt_regs,  r3),
		[4] = -1, [5] = -1, [6] = -1, [7] = -1,
		offsetof(struct pt_regs,  r8),
		offsetof(struct pt_regs,  r9),
		offsetof(struct pt_regs, r10),
		offsetof(struct pt_regs, r11),
		offsetof(struct pt_regs, r12),
		offsetof(struct pt_regs, r13),
		offsetof(struct pt_regs, r14),
		offsetof(struct pt_regs, r15),
		offsetof(struct pt_regs, r16),
		offsetof(struct pt_regs, r17),
		offsetof(struct pt_regs, r18),
		offsetof(struct pt_regs, r19),
		offsetof(struct pt_regs, r20),
		offsetof(struct pt_regs, r21),
		offsetof(struct pt_regs, r22),
		offsetof(struct pt_regs, r23),
		offsetof(struct pt_regs, r24),
		offsetof(struct pt_regs, r25),
		offsetof(struct pt_regs, r26),
		offsetof(struct pt_regs, r27),
		offsetof(struct pt_regs, r28),
		offsetof(struct pt_regs, r29),
		offsetof(struct pt_regs, r30),
		offsetof(struct pt_regs, r31),
	},
	.hash = { [0 ... UNW_HASH_SIZE - 1] = -1 },
#ifdef UNW_DEBUG
	.preg_name = {
		"pri_unat_gr", "pri_unat_mem", "bsp", "bspstore", "ar.pfs", "ar.rnat", "psp", "rp",
		"r4", "r5", "r6", "r7",
		"ar.unat", "pr", "ar.lc", "ar.fpsr",
		"b1", "b2", "b3", "b4", "b5",
		"f2", "f3", "f4", "f5",
		"f16", "f17", "f18", "f19", "f20", "f21", "f22", "f23",
		"f24", "f25", "f26", "f27", "f28", "f29", "f30", "f31"
	}
#endif
};

static inline int
read_only (void *addr)
{
	return (unsigned long) ((char *) addr - (char *) &unw.r0) < sizeof(unw.r0);
}

/*
 * Returns offset of rREG in struct pt_regs.
 */
static inline unsigned long
pt_regs_off (unsigned long reg)
{
	short off = -1;

	if (reg < ARRAY_SIZE(unw.pt_regs_offsets))
		off = unw.pt_regs_offsets[reg];

	if (off < 0) {
		UNW_DPRINT(0, "unwind.%s: bad scratch reg r%lu\n", __FUNCTION__, reg);
		off = 0;
	}
	return (unsigned long) off;
}

static inline struct pt_regs *
get_scratch_regs (struct unw_frame_info *info)
{
	if (!info->pt) {
		/* This should not happen with valid unwind info.  */
		UNW_DPRINT(0, "unwind.%s: bad unwind info: resetting info->pt\n", __FUNCTION__);
		if (info->flags & UNW_FLAG_INTERRUPT_FRAME)
			info->pt = (unsigned long) ((struct pt_regs *) info->psp - 1);
		else
			info->pt = info->sp - 16;
	}
	UNW_DPRINT(3, "unwind.%s: sp 0x%lx pt 0x%lx\n", __FUNCTION__, info->sp, info->pt);
	return (struct pt_regs *) info->pt;
}

/* Unwind accessors.  */

int
unw_access_gr (struct unw_frame_info *info, int regnum, unsigned long *val, char *nat, int write)
{
	unsigned long *addr, *nat_addr, nat_mask = 0, dummy_nat;
	struct unw_ireg *ireg;
	struct pt_regs *pt;

	if ((unsigned) regnum - 1 >= 127) {
		if (regnum == 0 && !write) {
			*val = 0;	/* read r0 always returns 0 */
			*nat = 0;
			return 0;
		}
		UNW_DPRINT(0, "unwind.%s: trying to access non-existent r%u\n",
			   __FUNCTION__, regnum);
		return -1;
	}

	if (regnum < 32) {
		if (regnum >= 4 && regnum <= 7) {
			/* access a preserved register */
			ireg = &info->r4 + (regnum - 4);
			addr = ireg->loc;
			if (addr) {
				nat_addr = addr + ireg->nat.off;
				switch (ireg->nat.type) {
				      case UNW_NAT_VAL:
					/* simulate getf.sig/setf.sig */
					if (write) {
						if (*nat) {
							/* write NaTVal and be done with it */
							addr[0] = 0;
							addr[1] = 0x1fffe;
							return 0;
						}
						addr[1] = 0x1003e;
					} else {
						if (addr[0] == 0 && addr[1] == 0x1ffe) {
							/* return NaT and be done with it */
							*val = 0;
							*nat = 1;
							return 0;
						}
					}
					/* fall through */
				      case UNW_NAT_NONE:
					dummy_nat = 0;
					nat_addr = &dummy_nat;
					break;

				      case UNW_NAT_MEMSTK:
					nat_mask = (1UL << ((long) addr & 0x1f8)/8);
					break;

				      case UNW_NAT_REGSTK:
					nat_addr = ia64_rse_rnat_addr(addr);
					if ((unsigned long) addr < info->regstk.limit
					    || (unsigned long) addr >= info->regstk.top)
					{
						UNW_DPRINT(0, "unwind.%s: %p outside of regstk "
							"[0x%lx-0x%lx)\n",
							__FUNCTION__, (void *) addr,
							info->regstk.limit,
							info->regstk.top);
						return -1;
					}
					if ((unsigned long) nat_addr >= info->regstk.top)
						nat_addr = &info->sw->ar_rnat;
					nat_mask = (1UL << ia64_rse_slot_num(addr));
					break;
				}
			} else {
				addr = &info->sw->r4 + (regnum - 4);
				nat_addr = &info->sw->ar_unat;
				nat_mask = (1UL << ((long) addr & 0x1f8)/8);
			}
		} else {
			/* access a scratch register */
			pt = get_scratch_regs(info);
			addr = (unsigned long *) ((unsigned long)pt + pt_regs_off(regnum));
			if (info->pri_unat_loc)
				nat_addr = info->pri_unat_loc;
			else
				nat_addr = &info->sw->caller_unat;
			nat_mask = (1UL << ((long) addr & 0x1f8)/8);
		}
	} else {
		/* access a stacked register */
		addr = ia64_rse_skip_regs((unsigned long *) info->bsp, regnum - 32);
		nat_addr = ia64_rse_rnat_addr(addr);
		if ((unsigned long) addr < info->regstk.limit
		    || (unsigned long) addr >= info->regstk.top)
		{
			UNW_DPRINT(0, "unwind.%s: ignoring attempt to access register outside "
				   "of rbs\n",  __FUNCTION__);
			return -1;
		}
		if ((unsigned long) nat_addr >= info->regstk.top)
			nat_addr = &info->sw->ar_rnat;
		nat_mask = (1UL << ia64_rse_slot_num(addr));
	}

	if (write) {
		if (read_only(addr)) {
			UNW_DPRINT(0, "unwind.%s: ignoring attempt to write read-only location\n",
				__FUNCTION__);
		} else {
			*addr = *val;
			if (*nat)
				*nat_addr |= nat_mask;
			else
				*nat_addr &= ~nat_mask;
		}
	} else {
		if ((*nat_addr & nat_mask) == 0) {
			*val = *addr;
			*nat = 0;
		} else {
			*val = 0;	/* if register is a NaT, *addr may contain kernel data! */
			*nat = 1;
		}
	}
	return 0;
}
EXPORT_SYMBOL(unw_access_gr);

int
unw_access_br (struct unw_frame_info *info, int regnum, unsigned long *val, int write)
{
	unsigned long *addr;
	struct pt_regs *pt;

	switch (regnum) {
		/* scratch: */
	      case 0: pt = get_scratch_regs(info); addr = &pt->b0; break;
	      case 6: pt = get_scratch_regs(info); addr = &pt->b6; break;
	      case 7: pt = get_scratch_regs(info); addr = &pt->b7; break;

		/* preserved: */
	      case 1: case 2: case 3: case 4: case 5:
		addr = *(&info->b1_loc + (regnum - 1));
		if (!addr)
			addr = &info->sw->b1 + (regnum - 1);
		break;

	      default:
		UNW_DPRINT(0, "unwind.%s: trying to access non-existent b%u\n",
			   __FUNCTION__, regnum);
		return -1;
	}
	if (write)
		if (read_only(addr)) {
			UNW_DPRINT(0, "unwind.%s: ignoring attempt to write read-only location\n",
				__FUNCTION__);
		} else
			*addr = *val;
	else
		*val = *addr;
	return 0;
}
EXPORT_SYMBOL(unw_access_br);

int
unw_access_fr (struct unw_frame_info *info, int regnum, struct ia64_fpreg *val, int write)
{
	struct ia64_fpreg *addr = NULL;
	struct pt_regs *pt;

	if ((unsigned) (regnum - 2) >= 126) {
		UNW_DPRINT(0, "unwind.%s: trying to access non-existent f%u\n",
			   __FUNCTION__, regnum);
		return -1;
	}

	if (regnum <= 5) {
		addr = *(&info->f2_loc + (regnum - 2));
		if (!addr)
			addr = &info->sw->f2 + (regnum - 2);
	} else if (regnum <= 15) {
		if (regnum <= 11) {
			pt = get_scratch_regs(info);
			addr = &pt->f6  + (regnum - 6);
		}
		else
			addr = &info->sw->f12 + (regnum - 12);
	} else if (regnum <= 31) {
		addr = info->fr_loc[regnum - 16];
		if (!addr)
			addr = &info->sw->f16 + (regnum - 16);
	} else {
		struct task_struct *t = info->task;

		if (write)
			ia64_sync_fph(t);
		else
			ia64_flush_fph(t);
		addr = t->thread.fph + (regnum - 32);
	}

	if (write)
		if (read_only(addr)) {
			UNW_DPRINT(0, "unwind.%s: ignoring attempt to write read-only location\n",
				__FUNCTION__);
		} else
			*addr = *val;
	else
		*val = *addr;
	return 0;
}
EXPORT_SYMBOL(unw_access_fr);

int
unw_access_ar (struct unw_frame_info *info, int regnum, unsigned long *val, int write)
{
	unsigned long *addr;
	struct pt_regs *pt;

	switch (regnum) {
	      case UNW_AR_BSP:
		addr = info->bsp_loc;
		if (!addr)
			addr = &info->sw->ar_bspstore;
		break;

	      case UNW_AR_BSPSTORE:
		addr = info->bspstore_loc;
		if (!addr)
			addr = &info->sw->ar_bspstore;
		break;

	      case UNW_AR_PFS:
		addr = info->pfs_loc;
		if (!addr)
			addr = &info->sw->ar_pfs;
		break;

	      case UNW_AR_RNAT:
		addr = info->rnat_loc;
		if (!addr)
			addr = &info->sw->ar_rnat;
		break;

	      case UNW_AR_UNAT:
		addr = info->unat_loc;
		if (!addr)
			addr = &info->sw->caller_unat;
		break;

	      case UNW_AR_LC:
		addr = info->lc_loc;
		if (!addr)
			addr = &info->sw->ar_lc;
		break;

	      case UNW_AR_EC:
		if (!info->cfm_loc)
			return -1;
		if (write)
			*info->cfm_loc =
				(*info->cfm_loc & ~(0x3fUL << 52)) | ((*val & 0x3f) << 52);
		else
			*val = (*info->cfm_loc >> 52) & 0x3f;
		return 0;

	      case UNW_AR_FPSR:
		addr = info->fpsr_loc;
		if (!addr)
			addr = &info->sw->ar_fpsr;
		break;

	      case UNW_AR_RSC:
		pt = get_scratch_regs(info);
		addr = &pt->ar_rsc;
		break;

	      case UNW_AR_CCV:
		pt = get_scratch_regs(info);
		addr = &pt->ar_ccv;
		break;

	      case UNW_AR_CSD:
		pt = get_scratch_regs(info);
		addr = &pt->ar_csd;
		break;

	      case UNW_AR_SSD:
		pt = get_scratch_regs(info);
		addr = &pt->ar_ssd;
		break;

	      default:
		UNW_DPRINT(0, "unwind.%s: trying to access non-existent ar%u\n",
			   __FUNCTION__, regnum);
		return -1;
	}

	if (write) {
		if (read_only(addr)) {
			UNW_DPRINT(0, "unwind.%s: ignoring attempt to write read-only location\n",
				__FUNCTION__);
		} else
			*addr = *val;
	} else
		*val = *addr;
	return 0;
}
EXPORT_SYMBOL(unw_access_ar);

int
unw_access_pr (struct unw_frame_info *info, unsigned long *val, int write)
{
	unsigned long *addr;

	addr = info->pr_loc;
	if (!addr)
		addr = &info->sw->pr;

	if (write) {
		if (read_only(addr)) {
			UNW_DPRINT(0, "unwind.%s: ignoring attempt to write read-only location\n",
				__FUNCTION__);
		} else
			*addr = *val;
	} else
		*val = *addr;
	return 0;
}
EXPORT_SYMBOL(unw_access_pr);


/* Routines to manipulate the state stack.  */

static inline void
push (struct unw_state_record *sr)
{
	struct unw_reg_state *rs;

	rs = alloc_reg_state();
	if (!rs) {
		printk(KERN_ERR "unwind: cannot stack reg state!\n");
		return;
	}
	memcpy(rs, &sr->curr, sizeof(*rs));
	sr->curr.next = rs;
}

static void
pop (struct unw_state_record *sr)
{
	struct unw_reg_state *rs = sr->curr.next;

	if (!rs) {
		printk(KERN_ERR "unwind: stack underflow!\n");
		return;
	}
	memcpy(&sr->curr, rs, sizeof(*rs));
	free_reg_state(rs);
}

/* Make a copy of the state stack.  Non-recursive to avoid stack overflows.  */
static struct unw_reg_state *
dup_state_stack (struct unw_reg_state *rs)
{
	struct unw_reg_state *copy, *prev = NULL, *first = NULL;

	while (rs) {
		copy = alloc_reg_state();
		if (!copy) {
			printk(KERN_ERR "unwind.dup_state_stack: out of memory\n");
			return NULL;
		}
		memcpy(copy, rs, sizeof(*copy));
		if (first)
			prev->next = copy;
		else
			first = copy;
		rs = rs->next;
		prev = copy;
	}
	return first;
}

/* Free all stacked register states (but not RS itself).  */
static void
free_state_stack (struct unw_reg_state *rs)
{
	struct unw_reg_state *p, *next;

	for (p = rs->next; p != NULL; p = next) {
		next = p->next;
		free_reg_state(p);
	}
	rs->next = NULL;
}

/* Unwind decoder routines */

static enum unw_register_index __attribute_const__
decode_abreg (unsigned char abreg, int memory)
{
	switch (abreg) {
	      case 0x04 ... 0x07: return UNW_REG_R4 + (abreg - 0x04);
	      case 0x22 ... 0x25: return UNW_REG_F2 + (abreg - 0x22);
	      case 0x30 ... 0x3f: return UNW_REG_F16 + (abreg - 0x30);
	      case 0x41 ... 0x45: return UNW_REG_B1 + (abreg - 0x41);
	      case 0x60: return UNW_REG_PR;
	      case 0x61: return UNW_REG_PSP;
	      case 0x62: return memory ? UNW_REG_PRI_UNAT_MEM : UNW_REG_PRI_UNAT_GR;
	      case 0x63: return UNW_REG_RP;
	      case 0x64: return UNW_REG_BSP;
	      case 0x65: return UNW_REG_BSPSTORE;
	      case 0x66: return UNW_REG_RNAT;
	      case 0x67: return UNW_REG_UNAT;
	      case 0x68: return UNW_REG_FPSR;
	      case 0x69: return UNW_REG_PFS;
	      case 0x6a: return UNW_REG_LC;
	      default:
		break;
	}
	UNW_DPRINT(0, "unwind.%s: bad abreg=0x%x\n", __FUNCTION__, abreg);
	return UNW_REG_LC;
}

static void
set_reg (struct unw_reg_info *reg, enum unw_where where, int when, unsigned long val)
{
	reg->val = val;
	reg->where = where;
	if (reg->when == UNW_WHEN_NEVER)
		reg->when = when;
}

static void
alloc_spill_area (unsigned long *offp, unsigned long regsize,
		  struct unw_reg_info *lo, struct unw_reg_info *hi)
{
	struct unw_reg_info *reg;

	for (reg = hi; reg >= lo; --reg) {
		if (reg->where == UNW_WHERE_SPILL_HOME) {
			reg->where = UNW_WHERE_PSPREL;
			*offp -= regsize;
			reg->val = *offp;
		}
	}
}

static inline void
spill_next_when (struct unw_reg_info **regp, struct unw_reg_info *lim, unw_word t)
{
	struct unw_reg_info *reg;

	for (reg = *regp; reg <= lim; ++reg) {
		if (reg->where == UNW_WHERE_SPILL_HOME) {
			reg->when = t;
			*regp = reg + 1;
			return;
		}
	}
	UNW_DPRINT(0, "unwind.%s: excess spill!\n",  __FUNCTION__);
}

static inline void
finish_prologue (struct unw_state_record *sr)
{
	struct unw_reg_info *reg;
	unsigned long off;
	int i;

	/*
	 * First, resolve implicit register save locations (see Section "11.4.2.3 Rules
	 * for Using Unwind Descriptors", rule 3):
	 */
	for (i = 0; i < (int) ARRAY_SIZE(unw.save_order); ++i) {
		reg = sr->curr.reg + unw.save_order[i];
		if (reg->where == UNW_WHERE_GR_SAVE) {
			reg->where = UNW_WHERE_GR;
			reg->val = sr->gr_save_loc++;
		}
	}

	/*
	 * Next, compute when the fp, general, and branch registers get
	 * saved.  This must come before alloc_spill_area() because
	 * we need to know which registers are spilled to their home
	 * locations.
	 */
	if (sr->imask) {
		unsigned char kind, mask = 0, *cp = sr->imask;
		int t;
		static const unsigned char limit[3] = {
			UNW_REG_F31, UNW_REG_R7, UNW_REG_B5
		};
		struct unw_reg_info *(regs[3]);

		regs[0] = sr->curr.reg + UNW_REG_F2;
		regs[1] = sr->curr.reg + UNW_REG_R4;
		regs[2] = sr->curr.reg + UNW_REG_B1;

		for (t = 0; t < sr->region_len; ++t) {
			if ((t & 3) == 0)
				mask = *cp++;
			kind = (mask >> 2*(3-(t & 3))) & 3;
			if (kind > 0)
				spill_next_when(&regs[kind - 1], sr->curr.reg + limit[kind - 1],
						sr->region_start + t);
		}
	}
	/*
	 * Next, lay out the memory stack spill area:
	 */
	if (sr->any_spills) {
		off = sr->spill_offset;
		alloc_spill_area(&off, 16, sr->curr.reg + UNW_REG_F2, sr->curr.reg + UNW_REG_F31);
		alloc_spill_area(&off,  8, sr->curr.reg + UNW_REG_B1, sr->curr.reg + UNW_REG_B5);
		alloc_spill_area(&off,  8, sr->curr.reg + UNW_REG_R4, sr->curr.reg + UNW_REG_R7);
	}
}

/*
 * Region header descriptors.
 */

static void
desc_prologue (int body, unw_word rlen, unsigned char mask, unsigned char grsave,
	       struct unw_state_record *sr)
{
	int i, region_start;

	if (!(sr->in_body || sr->first_region))
		finish_prologue(sr);
	sr->first_region = 0;

	/* check if we're done: */
	if (sr->when_target < sr->region_start + sr->region_len) {
		sr->done = 1;
		return;
	}

	region_start = sr->region_start + sr->region_len;

	for (i = 0; i < sr->epilogue_count; ++i)
		pop(sr);
	sr->epilogue_count = 0;
	sr->epilogue_start = UNW_WHEN_NEVER;

	sr->region_start = region_start;
	sr->region_len = rlen;
	sr->in_body = body;

	if (!body) {
		push(sr);

		for (i = 0; i < 4; ++i) {
			if (mask & 0x8)
				set_reg(sr->curr.reg + unw.save_order[i], UNW_WHERE_GR,
					sr->region_start + sr->region_len - 1, grsave++);
			mask <<= 1;
		}
		sr->gr_save_loc = grsave;
		sr->any_spills = 0;
		sr->imask = NULL;
		sr->spill_offset = 0x10;	/* default to psp+16 */
	}
}

/*
 * Prologue descriptors.
 */

static inline void
desc_abi (unsigned char abi, unsigned char context, struct unw_state_record *sr)
{
	if (abi == 3 && context == 'i') {
		sr->flags |= UNW_FLAG_INTERRUPT_FRAME;
		UNW_DPRINT(3, "unwind.%s: interrupt frame\n",  __FUNCTION__);
	}
	else
		UNW_DPRINT(0, "unwind%s: ignoring unwabi(abi=0x%x,context=0x%x)\n",
				__FUNCTION__, abi, context);
}

static inline void
desc_br_gr (unsigned char brmask, unsigned char gr, struct unw_state_record *sr)
{
	int i;

	for (i = 0; i < 5; ++i) {
		if (brmask & 1)
			set_reg(sr->curr.reg + UNW_REG_B1 + i, UNW_WHERE_GR,
				sr->region_start + sr->region_len - 1, gr++);
		brmask >>= 1;
	}
}

static inline void
desc_br_mem (unsigned char brmask, struct unw_state_record *sr)
{
	int i;

	for (i = 0; i < 5; ++i) {
		if (brmask & 1) {
			set_reg(sr->curr.reg + UNW_REG_B1 + i, UNW_WHERE_SPILL_HOME,
				sr->region_start + sr->region_len - 1, 0);
			sr->any_spills = 1;
		}
		brmask >>= 1;
	}
}

static inline void
desc_frgr_mem (unsigned char grmask, unw_word frmask, struct unw_state_record *sr)
{
	int i;

	for (i = 0; i < 4; ++i) {
		if ((grmask & 1) != 0) {
			set_reg(sr->curr.reg + UNW_REG_R4 + i, UNW_WHERE_SPILL_HOME,
				sr->region_start + sr->region_len - 1, 0);
			sr->any_spills = 1;
		}
		grmask >>= 1;
	}
	for (i = 0; i < 20; ++i) {
		if ((frmask & 1) != 0) {
			int base = (i < 4) ? UNW_REG_F2 : UNW_REG_F16 - 4;
			set_reg(sr->curr.reg + base + i, UNW_WHERE_SPILL_HOME,
				sr->region_start + sr->region_len - 1, 0);
			sr->any_spills = 1;
		}
		frmask >>= 1;
	}
}

static inline void
desc_fr_mem (unsigned char frmask, struct unw_state_record *sr)
{
	int i;

	for (i = 0; i < 4; ++i) {
		if ((frmask & 1) != 0) {
			set_reg(sr->curr.reg + UNW_REG_F2 + i, UNW_WHERE_SPILL_HOME,
				sr->region_start + sr->region_len - 1, 0);
			sr->any_spills = 1;
		}
		frmask >>= 1;
	}
}

static inline void
desc_gr_gr (unsigned char grmask, unsigned char gr, struct unw_state_record *sr)
{
	int i;

	for (i = 0; i < 4; ++i) {
		if ((grmask & 1) != 0)
			set_reg(sr->curr.reg + UNW_REG_R4 + i, UNW_WHERE_GR,
				sr->region_start + sr->region_len - 1, gr++);
		grmask >>= 1;
	}
}

static inline void
desc_gr_mem (unsigned char grmask, struct unw_state_record *sr)
{
	int i;

	for (i = 0; i < 4; ++i) {
		if ((grmask & 1) != 0) {
			set_reg(sr->curr.reg + UNW_REG_R4 + i, UNW_WHERE_SPILL_HOME,
				sr->region_start + sr->region_len - 1, 0);
			sr->any_spills = 1;
		}
		grmask >>= 1;
	}
}

static inline void
desc_mem_stack_f (unw_word t, unw_word size, struct unw_state_record *sr)
{
	set_reg(sr->curr.reg + UNW_REG_PSP, UNW_WHERE_NONE,
		sr->region_start + min_t(int, t, sr->region_len - 1), 16*size);
}

static inline void
desc_mem_stack_v (unw_word t, struct unw_state_record *sr)
{
	sr->curr.reg[UNW_REG_PSP].when = sr->region_start + min_t(int, t, sr->region_len - 1);
}

static inline void
desc_reg_gr (unsigned char reg, unsigned char dst, struct unw_state_record *sr)
{
	set_reg(sr->curr.reg + reg, UNW_WHERE_GR, sr->region_start + sr->region_len - 1, dst);
}

static inline void
desc_reg_psprel (unsigned char reg, unw_word pspoff, struct unw_state_record *sr)
{
	set_reg(sr->curr.reg + reg, UNW_WHERE_PSPREL, sr->region_start + sr->region_len - 1,
		0x10 - 4*pspoff);
}

static inline void
desc_reg_sprel (unsigned char reg, unw_word spoff, struct unw_state_record *sr)
{
	set_reg(sr->curr.reg + reg, UNW_WHERE_SPREL, sr->region_start + sr->region_len - 1,
		4*spoff);
}

static inline void
desc_rp_br (unsigned char dst, struct unw_state_record *sr)
{
	sr->return_link_reg = dst;
}

static inline void
desc_reg_when (unsigned char regnum, unw_word t, struct unw_state_record *sr)
{
	struct unw_reg_info *reg = sr->curr.reg + regnum;

	if (reg->where == UNW_WHERE_NONE)
		reg->where = UNW_WHERE_GR_SAVE;
	reg->when = sr->region_start + min_t(int, t, sr->region_len - 1);
}

static inline void
desc_spill_base (unw_word pspoff, struct unw_state_record *sr)
{
	sr->spill_offset = 0x10 - 4*pspoff;
}

static inline unsigned char *
desc_spill_mask (unsigned char *imaskp, struct unw_state_record *sr)
{
	sr->imask = imaskp;
	return imaskp + (2*sr->region_len + 7)/8;
}

/*
 * Body descriptors.
 */
static inline void
desc_epilogue (unw_word t, unw_word ecount, struct unw_state_record *sr)
{
	sr->epilogue_start = sr->region_start + sr->region_len - 1 - t;
	sr->epilogue_count = ecount + 1;
}

static inline void
desc_copy_state (unw_word label, struct unw_state_record *sr)
{
	struct unw_labeled_state *ls;

	for (ls = sr->labeled_states; ls; ls = ls->next) {
		if (ls->label == label) {
			free_state_stack(&sr->curr);
			memcpy(&sr->curr, &ls->saved_state, sizeof(sr->curr));
			sr->curr.next = dup_state_stack(ls->saved_state.next);
			return;
		}
	}
	printk(KERN_ERR "unwind: failed to find state labeled 0x%lx\n", label);
}

static inline void
desc_label_state (unw_word label, struct unw_state_record *sr)
{
	struct unw_labeled_state *ls;

	ls = alloc_labeled_state();
	if (!ls) {
		printk(KERN_ERR "unwind.desc_label_state(): out of memory\n");
		return;
	}
	ls->label = label;
	memcpy(&ls->saved_state, &sr->curr, sizeof(ls->saved_state));
	ls->saved_state.next = dup_state_stack(sr->curr.next);

	/* insert into list of labeled states: */
	ls->next = sr->labeled_states;
	sr->labeled_states = ls;
}

/*
 * General descriptors.
 */

static inline int
desc_is_active (unsigned char qp, unw_word t, struct unw_state_record *sr)
{
	if (sr->when_target <= sr->region_start + min_t(int, t, sr->region_len - 1))
		return 0;
	if (qp > 0) {
		if ((sr->pr_val & (1UL << qp)) == 0)
			return 0;
		sr->pr_mask |= (1UL << qp);
	}
	return 1;
}

static inline void
desc_restore_p (unsigned char qp, unw_word t, unsigned char abreg, struct unw_state_record *sr)
{
	struct unw_reg_info *r;

	if (!desc_is_active(qp, t, sr))
		return;

	r = sr->curr.reg + decode_abreg(abreg, 0);
	r->where = UNW_WHERE_NONE;
	r->when = UNW_WHEN_NEVER;
	r->val = 0;
}

static inline void
desc_spill_reg_p (unsigned char qp, unw_word t, unsigned char abreg, unsigned char x,
		     unsigned char ytreg, struct unw_state_record *sr)
{
	enum unw_where where = UNW_WHERE_GR;
	struct unw_reg_info *r;

	if (!desc_is_active(qp, t, sr))
		return;

	if (x)
		where = UNW_WHERE_BR;
	else if (ytreg & 0x80)
		where = UNW_WHERE_FR;

	r = sr->curr.reg + decode_abreg(abreg, 0);
	r->where = where;
	r->when = sr->region_start + min_t(int, t, sr->region_len - 1);
	r->val = (ytreg & 0x7f);
}

static inline void
desc_spill_psprel_p (unsigned char qp, unw_word t, unsigned char abreg, unw_word pspoff,
		     struct unw_state_record *sr)
{
	struct unw_reg_info *r;

	if (!desc_is_active(qp, t, sr))
		return;

	r = sr->curr.reg + decode_abreg(abreg, 1);
	r->where = UNW_WHERE_PSPREL;
	r->when = sr->region_start + min_t(int, t, sr->region_len - 1);
	r->val = 0x10 - 4*pspoff;
}

static inline void
desc_spill_sprel_p (unsigned char qp, unw_word t, unsigned char abreg, unw_word spoff,
		       struct unw_state_record *sr)
{
	struct unw_reg_info *r;

	if (!desc_is_active(qp, t, sr))
		return;

	r = sr->curr.reg + decode_abreg(abreg, 1);
	r->where = UNW_WHERE_SPREL;
	r->when = sr->region_start + min_t(int, t, sr->region_len - 1);
	r->val = 4*spoff;
}

#define UNW_DEC_BAD_CODE(code)			printk(KERN_ERR "unwind: unknown code 0x%02x\n", \
						       code);

/*
 * region headers:
 */
#define UNW_DEC_PROLOGUE_GR(fmt,r,m,gr,arg)	desc_prologue(0,r,m,gr,arg)
#define UNW_DEC_PROLOGUE(fmt,b,r,arg)		desc_prologue(b,r,0,32,arg)
/*
 * prologue descriptors:
 */
#define UNW_DEC_ABI(fmt,a,c,arg)		desc_abi(a,c,arg)
#define UNW_DEC_BR_GR(fmt,b,g,arg)		desc_br_gr(b,g,arg)
#define UNW_DEC_BR_MEM(fmt,b,arg)		desc_br_mem(b,arg)
#define UNW_DEC_FRGR_MEM(fmt,g,f,arg)		desc_frgr_mem(g,f,arg)
#define UNW_DEC_FR_MEM(fmt,f,arg)		desc_fr_mem(f,arg)
#define UNW_DEC_GR_GR(fmt,m,g,arg)		desc_gr_gr(m,g,arg)
#define UNW_DEC_GR_MEM(fmt,m,arg)		desc_gr_mem(m,arg)
#define UNW_DEC_MEM_STACK_F(fmt,t,s,arg)	desc_mem_stack_f(t,s,arg)
#define UNW_DEC_MEM_STACK_V(fmt,t,arg)		desc_mem_stack_v(t,arg)
#define UNW_DEC_REG_GR(fmt,r,d,arg)		desc_reg_gr(r,d,arg)
#define UNW_DEC_REG_PSPREL(fmt,r,o,arg)		desc_reg_psprel(r,o,arg)
#define UNW_DEC_REG_SPREL(fmt,r,o,arg)		desc_reg_sprel(r,o,arg)
#define UNW_DEC_REG_WHEN(fmt,r,t,arg)		desc_reg_when(r,t,arg)
#define UNW_DEC_PRIUNAT_WHEN_GR(fmt,t,arg)	desc_reg_when(UNW_REG_PRI_UNAT_GR,t,arg)
#define UNW_DEC_PRIUNAT_WHEN_MEM(fmt,t,arg)	desc_reg_when(UNW_REG_PRI_UNAT_MEM,t,arg)
#define UNW_DEC_PRIUNAT_GR(fmt,r,arg)		desc_reg_gr(UNW_REG_PRI_UNAT_GR,r,arg)
#define UNW_DEC_PRIUNAT_PSPREL(fmt,o,arg)	desc_reg_psprel(UNW_REG_PRI_UNAT_MEM,o,arg)
#define UNW_DEC_PRIUNAT_SPREL(fmt,o,arg)	desc_reg_sprel(UNW_REG_PRI_UNAT_MEM,o,arg)
#define UNW_DEC_RP_BR(fmt,d,arg)		desc_rp_br(d,arg)
#define UNW_DEC_SPILL_BASE(fmt,o,arg)		desc_spill_base(o,arg)
#define UNW_DEC_SPILL_MASK(fmt,m,arg)		(m = desc_spill_mask(m,arg))
/*
 * body descriptors:
 */
#define UNW_DEC_EPILOGUE(fmt,t,c,arg)		desc_epilogue(t,c,arg)
#define UNW_DEC_COPY_STATE(fmt,l,arg)		desc_copy_state(l,arg)
#define UNW_DEC_LABEL_STATE(fmt,l,arg)		desc_label_state(l,arg)
/*
 * general unwind descriptors:
 */
#define UNW_DEC_SPILL_REG_P(f,p,t,a,x,y,arg)	desc_spill_reg_p(p,t,a,x,y,arg)
#define UNW_DEC_SPILL_REG(f,t,a,x,y,arg)	desc_spill_reg_p(0,t,a,x,y,arg)
#define UNW_DEC_SPILL_PSPREL_P(f,p,t,a,o,arg)	desc_spill_psprel_p(p,t,a,o,arg)
#define UNW_DEC_SPILL_PSPREL(f,t,a,o,arg)	desc_spill_psprel_p(0,t,a,o,arg)
#define UNW_DEC_SPILL_SPREL_P(f,p,t,a,o,arg)	desc_spill_sprel_p(p,t,a,o,arg)
#define UNW_DEC_SPILL_SPREL(f,t,a,o,arg)	desc_spill_sprel_p(0,t,a,o,arg)
#define UNW_DEC_RESTORE_P(f,p,t,a,arg)		desc_restore_p(p,t,a,arg)
#define UNW_DEC_RESTORE(f,t,a,arg)		desc_restore_p(0,t,a,arg)

#include "unwind_decoder.c"


/* Unwind scripts. */

static inline unw_hash_index_t
hash (unsigned long ip)
{
#	define hashmagic	0x9e3779b97f4a7c16UL	/* based on (sqrt(5)/2-1)*2^64 */

	return (ip >> 4)*hashmagic >> (64 - UNW_LOG_HASH_SIZE);
#undef hashmagic
}

static inline long
cache_match (struct unw_script *script, unsigned long ip, unsigned long pr)
{
	read_lock(&script->lock);
	if (ip == script->ip && ((pr ^ script->pr_val) & script->pr_mask) == 0)
		/* keep the read lock... */
		return 1;
	read_unlock(&script->lock);
	return 0;
}

static inline struct unw_script *
script_lookup (struct unw_frame_info *info)
{
	struct unw_script *script = unw.cache + info->hint;
	unsigned short index;
	unsigned long ip, pr;

	if (UNW_DEBUG_ON(0))
		return NULL;	/* Always regenerate scripts in debug mode */

	STAT(++unw.stat.cache.lookups);

	ip = info->ip;
	pr = info->pr;

	if (cache_match(script, ip, pr)) {
		STAT(++unw.stat.cache.hinted_hits);
		return script;
	}

	index = unw.hash[hash(ip)];
	if (index >= UNW_CACHE_SIZE)
		return NULL;

	script = unw.cache + index;
	while (1) {
		if (cache_match(script, ip, pr)) {
			/* update hint; no locking required as single-word writes are atomic */
			STAT(++unw.stat.cache.normal_hits);
			unw.cache[info->prev_script].hint = script - unw.cache;
			return script;
		}
		if (script->coll_chain >= UNW_HASH_SIZE)
			return NULL;
		script = unw.cache + script->coll_chain;
		STAT(++unw.stat.cache.collision_chain_traversals);
	}
}

/*
 * On returning, a write lock for the SCRIPT is still being held.
 */
static inline struct unw_script *
script_new (unsigned long ip)
{
	struct unw_script *script, *prev, *tmp;
	unw_hash_index_t index;
	unsigned short head;

	STAT(++unw.stat.script.news);

	/*
	 * Can't (easily) use cmpxchg() here because of ABA problem
	 * that is intrinsic in cmpxchg()...
	 */
	head = unw.lru_head;
	script = unw.cache + head;
	unw.lru_head = script->lru_chain;

	/*
	 * We'd deadlock here if we interrupted a thread that is holding a read lock on
	 * script->lock.  Thus, if the write_trylock() fails, we simply bail out.  The
	 * alternative would be to disable interrupts whenever we hold a read-lock, but
	 * that seems silly.
	 */
	if (!write_trylock(&script->lock))
		return NULL;

	/* re-insert script at the tail of the LRU chain: */
	unw.cache[unw.lru_tail].lru_chain = head;
	unw.lru_tail = head;

	/* remove the old script from the hash table (if it's there): */
	if (script->ip) {
		index = hash(script->ip);
		tmp = unw.cache + unw.hash[index];
		prev = NULL;
		while (1) {
			if (tmp == script) {
				if (prev)
					prev->coll_chain = tmp->coll_chain;
				else
					unw.hash[index] = tmp->coll_chain;
				break;
			} else
				prev = tmp;
			if (tmp->coll_chain >= UNW_CACHE_SIZE)
			/* old script wasn't in the hash-table */
				break;
			tmp = unw.cache + tmp->coll_chain;
		}
	}

	/* enter new script in the hash table */
	index = hash(ip);
	script->coll_chain = unw.hash[index];
	unw.hash[index] = script - unw.cache;

	script->ip = ip;	/* set new IP while we're holding the locks */

	STAT(if (script->coll_chain < UNW_CACHE_SIZE) ++unw.stat.script.collisions);

	script->flags = 0;
	script->hint = 0;
	script->count = 0;
	return script;
}

static void
script_finalize (struct unw_script *script, struct unw_state_record *sr)
{
	script->pr_mask = sr->pr_mask;
	script->pr_val = sr->pr_val;
	/*
	 * We could down-grade our write-lock on script->lock here but
	 * the rwlock API doesn't offer atomic lock downgrading, so
	 * we'll just keep the write-lock and release it later when
	 * we're done using the script.
	 */
}

static inline void
script_emit (struct unw_script *script, struct unw_insn insn)
{
	if (script->count >= UNW_MAX_SCRIPT_LEN) {
		UNW_DPRINT(0, "unwind.%s: script exceeds maximum size of %u instructions!\n",
			__FUNCTION__, UNW_MAX_SCRIPT_LEN);
		return;
	}
	script->insn[script->count++] = insn;
}

static inline void
emit_nat_info (struct unw_state_record *sr, int i, struct unw_script *script)
{
	struct unw_reg_info *r = sr->curr.reg + i;
	enum unw_insn_opcode opc;
	struct unw_insn insn;
	unsigned long val = 0;

	switch (r->where) {
	      case UNW_WHERE_GR:
		if (r->val >= 32) {
			/* register got spilled to a stacked register */
			opc = UNW_INSN_SETNAT_TYPE;
			val = UNW_NAT_REGSTK;
		} else
			/* register got spilled to a scratch register */
			opc = UNW_INSN_SETNAT_MEMSTK;
		break;

	      case UNW_WHERE_FR:
		opc = UNW_INSN_SETNAT_TYPE;
		val = UNW_NAT_VAL;
		break;

	      case UNW_WHERE_BR:
		opc = UNW_INSN_SETNAT_TYPE;
		val = UNW_NAT_NONE;
		break;

	      case UNW_WHERE_PSPREL:
	      case UNW_WHERE_SPREL:
		opc = UNW_INSN_SETNAT_MEMSTK;
		break;

	      default:
		UNW_DPRINT(0, "unwind.%s: don't know how to emit nat info for where = %u\n",
			   __FUNCTION__, r->where);
		return;
	}
	insn.opc = opc;
	insn.dst = unw.preg_index[i];
	insn.val = val;
	script_emit(script, insn);
}

static void
compile_reg (struct unw_state_record *sr, int i, struct unw_script *script)
{
	struct unw_reg_info *r = sr->curr.reg + i;
	enum unw_insn_opcode opc;
	unsigned long val, rval;
	struct unw_insn insn;
	long need_nat_info;

	if (r->where == UNW_WHERE_NONE || r->when >= sr->when_target)
		return;

	opc = UNW_INSN_MOVE;
	val = rval = r->val;
	need_nat_info = (i >= UNW_REG_R4 && i <= UNW_REG_R7);

	switch (r->where) {
	      case UNW_WHERE_GR:
		if (rval >= 32) {
			opc = UNW_INSN_MOVE_STACKED;
			val = rval - 32;
		} else if (rval >= 4 && rval <= 7) {
			if (need_nat_info) {
				opc = UNW_INSN_MOVE2;
				need_nat_info = 0;
			}
			val = unw.preg_index[UNW_REG_R4 + (rval - 4)];
		} else if (rval == 0) {
			opc = UNW_INSN_MOVE_CONST;
			val = 0;
		} else {
			/* register got spilled to a scratch register */
			opc = UNW_INSN_MOVE_SCRATCH;
			val = pt_regs_off(rval);
		}
		break;

	      case UNW_WHERE_FR:
		if (rval <= 5)
			val = unw.preg_index[UNW_REG_F2  + (rval -  2)];
		else if (rval >= 16 && rval <= 31)
			val = unw.preg_index[UNW_REG_F16 + (rval - 16)];
		else {
			opc = UNW_INSN_MOVE_SCRATCH;
			if (rval <= 11)
				val = offsetof(struct pt_regs, f6) + 16*(rval - 6);
			else
				UNW_DPRINT(0, "unwind.%s: kernel may not touch f%lu\n",
					   __FUNCTION__, rval);
		}
		break;

	      case UNW_WHERE_BR:
		if (rval >= 1 && rval <= 5)
			val = unw.preg_index[UNW_REG_B1 + (rval - 1)];
		else {
			opc = UNW_INSN_MOVE_SCRATCH;
			if (rval == 0)
				val = offsetof(struct pt_regs, b0);
			else if (rval == 6)
				val = offsetof(struct pt_regs, b6);
			else
				val = offsetof(struct pt_regs, b7);
		}
		break;

	      case UNW_WHERE_SPREL:
		opc = UNW_INSN_ADD_SP;
		break;

	      case UNW_WHERE_PSPREL:
		opc = UNW_INSN_ADD_PSP;
		break;

	      default:
		UNW_DPRINT(0, "unwind%s: register %u has unexpected `where' value of %u\n",
			   __FUNCTION__, i, r->where);
		break;
	}
	insn.opc = opc;
	insn.dst = unw.preg_index[i];
	insn.val = val;
	script_emit(script, insn);
	if (need_nat_info)
		emit_nat_info(sr, i, script);

	if (i == UNW_REG_PSP) {
		/*
		 * info->psp must contain the _value_ of the previous
		 * sp, not it's save location.  We get this by
		 * dereferencing the value we just stored in
		 * info->psp:
		 */
		insn.opc = UNW_INSN_LOAD;
		insn.dst = insn.val = unw.preg_index[UNW_REG_PSP];
		script_emit(script, insn);
	}
}

static inline const struct unw_table_entry *
lookup (struct unw_table *table, unsigned long rel_ip)
{
	const struct unw_table_entry *e = NULL;
	unsigned long lo, hi, mid;

	/* do a binary search for right entry: */
	for (lo = 0, hi = table->length; lo < hi; ) {
		mid = (lo + hi) / 2;
		e = &table->array[mid];
		if (rel_ip < e->start_offset)
			hi = mid;
		else if (rel_ip >= e->end_offset)
			lo = mid + 1;
		else
			break;
	}
	if (rel_ip < e->start_offset || rel_ip >= e->end_offset)
		return NULL;
	return e;
}

/*
 * Build an unwind script that unwinds from state OLD_STATE to the
 * entrypoint of the function that called OLD_STATE.
 */
static inline struct unw_script *
build_script (struct unw_frame_info *info)
{
	const struct unw_table_entry *e = NULL;
	struct unw_script *script = NULL;
	struct unw_labeled_state *ls, *next;
	unsigned long ip = info->ip;
	struct unw_state_record sr;
	struct unw_table *table;
	struct unw_reg_info *r;
	struct unw_insn insn;
	u8 *dp, *desc_end;
	u64 hdr;
	int i;
	STAT(unsigned long start, parse_start;)

	STAT(++unw.stat.script.builds; start = ia64_get_itc());

	/* build state record */
	memset(&sr, 0, sizeof(sr));
	for (r = sr.curr.reg; r < sr.curr.reg + UNW_NUM_REGS; ++r)
		r->when = UNW_WHEN_NEVER;
	sr.pr_val = info->pr;

	UNW_DPRINT(3, "unwind.%s: ip 0x%lx\n", __FUNCTION__, ip);
	script = script_new(ip);
	if (!script) {
		UNW_DPRINT(0, "unwind.%s: failed to create unwind script\n",  __FUNCTION__);
		STAT(unw.stat.script.build_time += ia64_get_itc() - start);
		return NULL;
	}
	unw.cache[info->prev_script].hint = script - unw.cache;

	/* search the kernels and the modules' unwind tables for IP: */

	STAT(parse_start = ia64_get_itc());

	for (table = unw.tables; table; table = table->next) {
		if (ip >= table->start && ip < table->end) {
			e = lookup(table, ip - table->segment_base);
			break;
		}
	}
	if (!e) {
		/* no info, return default unwinder (leaf proc, no mem stack, no saved regs)  */
		UNW_DPRINT(1, "unwind.%s: no unwind info for ip=0x%lx (prev ip=0x%lx)\n",
			__FUNCTION__, ip, unw.cache[info->prev_script].ip);
		sr.curr.reg[UNW_REG_RP].where = UNW_WHERE_BR;
		sr.curr.reg[UNW_REG_RP].when = -1;
		sr.curr.reg[UNW_REG_RP].val = 0;
		compile_reg(&sr, UNW_REG_RP, script);
		script_finalize(script, &sr);
		STAT(unw.stat.script.parse_time += ia64_get_itc() - parse_start);
		STAT(unw.stat.script.build_time += ia64_get_itc() - start);
		return script;
	}

	sr.when_target = (3*((ip & ~0xfUL) - (table->segment_base + e->start_offset))/16
			  + (ip & 0xfUL));
	hdr = *(u64 *) (table->segment_base + e->info_offset);
	dp =   (u8 *)  (table->segment_base + e->info_offset + 8);
	desc_end = dp + 8*UNW_LENGTH(hdr);

	while (!sr.done && dp < desc_end)
		dp = unw_decode(dp, sr.in_body, &sr);

	if (sr.when_target > sr.epilogue_start) {
		/*
		 * sp has been restored and all values on the memory stack below
		 * psp also have been restored.
		 */
		sr.curr.reg[UNW_REG_PSP].val = 0;
		sr.curr.reg[UNW_REG_PSP].where = UNW_WHERE_NONE;
		sr.curr.reg[UNW_REG_PSP].when = UNW_WHEN_NEVER;
		for (r = sr.curr.reg; r < sr.curr.reg + UNW_NUM_REGS; ++r)
			if ((r->where == UNW_WHERE_PSPREL && r->val <= 0x10)
			    || r->where == UNW_WHERE_SPREL)
			{
				r->val = 0;
				r->where = UNW_WHERE_NONE;
				r->when = UNW_WHEN_NEVER;
			}
	}

	script->flags = sr.flags;

	/*
	 * If RP did't get saved, generate entry for the return link
	 * register.
	 */
	if (sr.curr.reg[UNW_REG_RP].when >= sr.when_target) {
		sr.curr.reg[UNW_REG_RP].where = UNW_WHERE_BR;
		sr.curr.reg[UNW_REG_RP].when = -1;
		sr.curr.reg[UNW_REG_RP].val = sr.return_link_reg;
		UNW_DPRINT(1, "unwind.%s: using default for rp at ip=0x%lx where=%d val=0x%lx\n",
			   __FUNCTION__, ip, sr.curr.reg[UNW_REG_RP].where,
			   sr.curr.reg[UNW_REG_RP].val);
	}

#ifdef UNW_DEBUG
	UNW_DPRINT(1, "unwind.%s: state record for func 0x%lx, t=%u:\n",
		__FUNCTION__, table->segment_base + e->start_offset, sr.when_target);
	for (r = sr.curr.reg; r < sr.curr.reg + UNW_NUM_REGS; ++r) {
		if (r->where != UNW_WHERE_NONE || r->when != UNW_WHEN_NEVER) {
			UNW_DPRINT(1, "  %s <- ", unw.preg_name[r - sr.curr.reg]);
			switch (r->where) {
			      case UNW_WHERE_GR:     UNW_DPRINT(1, "r%lu", r->val); break;
			      case UNW_WHERE_FR:     UNW_DPRINT(1, "f%lu", r->val); break;
			      case UNW_WHERE_BR:     UNW_DPRINT(1, "b%lu", r->val); break;
			      case UNW_WHERE_SPREL:  UNW_DPRINT(1, "[sp+0x%lx]", r->val); break;
			      case UNW_WHERE_PSPREL: UNW_DPRINT(1, "[psp+0x%lx]", r->val); break;
			      case UNW_WHERE_NONE:
				UNW_DPRINT(1, "%s+0x%lx", unw.preg_name[r - sr.curr.reg], r->val);
				break;

			      default:
				UNW_DPRINT(1, "BADWHERE(%d)", r->where);
				break;
			}
			UNW_DPRINT(1, "\t\t%d\n", r->when);
		}
	}
#endif

	STAT(unw.stat.script.parse_time += ia64_get_itc() - parse_start);

	/* translate state record into unwinder instructions: */

	/*
	 * First, set psp if we're dealing with a fixed-size frame;
	 * subsequent instructions may depend on this value.
	 */
	if (sr.when_target > sr.curr.reg[UNW_REG_PSP].when
	    && (sr.curr.reg[UNW_REG_PSP].where == UNW_WHERE_NONE)
	    && sr.curr.reg[UNW_REG_PSP].val != 0) {
		/* new psp is sp plus frame size */
		insn.opc = UNW_INSN_ADD;
		insn.dst = offsetof(struct unw_frame_info, psp)/8;
		insn.val = sr.curr.reg[UNW_REG_PSP].val;	/* frame size */
		script_emit(script, insn);
	}

	/* determine where the primary UNaT is: */
	if (sr.when_target < sr.curr.reg[UNW_REG_PRI_UNAT_GR].when)
		i = UNW_REG_PRI_UNAT_MEM;
	else if (sr.when_target < sr.curr.reg[UNW_REG_PRI_UNAT_MEM].when)
		i = UNW_REG_PRI_UNAT_GR;
	else if (sr.curr.reg[UNW_REG_PRI_UNAT_MEM].when > sr.curr.reg[UNW_REG_PRI_UNAT_GR].when)
		i = UNW_REG_PRI_UNAT_MEM;
	else
		i = UNW_REG_PRI_UNAT_GR;

	compile_reg(&sr, i, script);

	for (i = UNW_REG_BSP; i < UNW_NUM_REGS; ++i)
		compile_reg(&sr, i, script);

	/* free labeled register states & stack: */

	STAT(parse_start = ia64_get_itc());
	for (ls = sr.labeled_states; ls; ls = next) {
		next = ls->next;
		free_state_stack(&ls->saved_state);
		free_labeled_state(ls);
	}
	free_state_stack(&sr.curr);
	STAT(unw.stat.script.parse_time += ia64_get_itc() - parse_start);

	script_finalize(script, &sr);
	STAT(unw.stat.script.build_time += ia64_get_itc() - start);
	return script;
}

/*
 * Apply the unwinding actions represented by OPS and update SR to
 * reflect the state that existed upon entry to the function that this
 * unwinder represents.
 */
static inline void
run_script (struct unw_script *script, struct unw_frame_info *state)
{
	struct unw_insn *ip, *limit, next_insn;
	unsigned long opc, dst, val, off;
	unsigned long *s = (unsigned long *) state;
	STAT(unsigned long start;)

	STAT(++unw.stat.script.runs; start = ia64_get_itc());
	state->flags = script->flags;
	ip = script->insn;
	limit = script->insn + script->count;
	next_insn = *ip;

	while (ip++ < limit) {
		opc = next_insn.opc;
		dst = next_insn.dst;
		val = next_insn.val;
		next_insn = *ip;

	  redo:
		switch (opc) {
		      case UNW_INSN_ADD:
			s[dst] += val;
			break;

		      case UNW_INSN_MOVE2:
			if (!s[val])
				goto lazy_init;
			s[dst+1] = s[val+1];
			s[dst] = s[val];
			break;

		      case UNW_INSN_MOVE:
			if (!s[val])
				goto lazy_init;
			s[dst] = s[val];
			break;

		      case UNW_INSN_MOVE_SCRATCH:
			if (state->pt) {
				s[dst] = (unsigned long) get_scratch_regs(state) + val;
			} else {
				s[dst] = 0;
				UNW_DPRINT(0, "unwind.%s: no state->pt, dst=%ld, val=%ld\n",
					   __FUNCTION__, dst, val);
			}
			break;

		      case UNW_INSN_MOVE_CONST:
			if (val == 0)
				s[dst] = (unsigned long) &unw.r0;
			else {
				s[dst] = 0;
				UNW_DPRINT(0, "unwind.%s: UNW_INSN_MOVE_CONST bad val=%ld\n",
					   __FUNCTION__, val);
			}
			break;


		      case UNW_INSN_MOVE_STACKED:
			s[dst] = (unsigned long) ia64_rse_skip_regs((unsigned long *)state->bsp,
								    val);
			break;

		      case UNW_INSN_ADD_PSP:
			s[dst] = state->psp + val;
			break;

		      case UNW_INSN_ADD_SP:
			s[dst] = state->sp + val;
			break;

		      case UNW_INSN_SETNAT_MEMSTK:
			if (!state->pri_unat_loc)
				state->pri_unat_loc = &state->sw->caller_unat;
			/* register off. is a multiple of 8, so the least 3 bits (type) are 0 */
			s[dst+1] = ((unsigned long) state->pri_unat_loc - s[dst]) | UNW_NAT_MEMSTK;
			break;

		      case UNW_INSN_SETNAT_TYPE:
			s[dst+1] = val;
			break;

		      case UNW_INSN_LOAD:
#ifdef UNW_DEBUG
			if ((s[val] & (local_cpu_data->unimpl_va_mask | 0x7)) != 0
			    || s[val] < TASK_SIZE)
			{
				UNW_DPRINT(0, "unwind.%s: rejecting bad psp=0x%lx\n",
					   __FUNCTION__, s[val]);
				break;
			}
#endif
			s[dst] = *(unsigned long *) s[val];
			break;
		}
	}
	STAT(unw.stat.script.run_time += ia64_get_itc() - start);
	return;

  lazy_init:
	off = unw.sw_off[val];
	s[val] = (unsigned long) state->sw + off;
	if (off >= offsetof(struct switch_stack, r4) && off <= offsetof(struct switch_stack, r7))
		/*
		 * We're initializing a general register: init NaT info, too.  Note that
		 * the offset is a multiple of 8 which gives us the 3 bits needed for
		 * the type field.
		 */
		s[val+1] = (offsetof(struct switch_stack, ar_unat) - off) | UNW_NAT_MEMSTK;
	goto redo;
}

static int
find_save_locs (struct unw_frame_info *info)
{
	int have_write_lock = 0;
	struct unw_script *scr;
	unsigned long flags = 0;

	if ((info->ip & (local_cpu_data->unimpl_va_mask | 0xf)) || info->ip < TASK_SIZE) {
		/* don't let obviously bad addresses pollute the cache */
		/* FIXME: should really be level 0 but it occurs too often. KAO */
		UNW_DPRINT(1, "unwind.%s: rejecting bad ip=0x%lx\n", __FUNCTION__, info->ip);
		info->rp_loc = NULL;
		return -1;
	}

	scr = script_lookup(info);
	if (!scr) {
		spin_lock_irqsave(&unw.lock, flags);
		scr = build_script(info);
		if (!scr) {
			spin_unlock_irqrestore(&unw.lock, flags);
			UNW_DPRINT(0,
				   "unwind.%s: failed to locate/build unwind script for ip %lx\n",
				   __FUNCTION__, info->ip);
			return -1;
		}
		have_write_lock = 1;
	}
	info->hint = scr->hint;
	info->prev_script = scr - unw.cache;

	run_script(scr, info);

	if (have_write_lock) {
		write_unlock(&scr->lock);
		spin_unlock_irqrestore(&unw.lock, flags);
	} else
		read_unlock(&scr->lock);
	return 0;
}

static int
unw_valid(const struct unw_frame_info *info, unsigned long* p)
{
	unsigned long loc = (unsigned long)p;
	return (loc >= info->regstk.limit && loc < info->regstk.top) ||
	       (loc >= info->memstk.top && loc < info->memstk.limit);
}

int
unw_unwind (struct unw_frame_info *info)
{
	unsigned long prev_ip, prev_sp, prev_bsp;
	unsigned long ip, pr, num_regs;
	STAT(unsigned long start, flags;)
	int retval;

	STAT(local_irq_save(flags); ++unw.stat.api.unwinds; start = ia64_get_itc());

	prev_ip = info->ip;
	prev_sp = info->sp;
	prev_bsp = info->bsp;

	/* validate the return IP pointer */
	if (!unw_valid(info, info->rp_loc)) {
		/* FIXME: should really be level 0 but it occurs too often. KAO */
		UNW_DPRINT(1, "unwind.%s: failed to locate return link (ip=0x%lx)!\n",
			   __FUNCTION__, info->ip);
		STAT(unw.stat.api.unwind_time += ia64_get_itc() - start; local_irq_restore(flags));
		return -1;
	}
	/* restore the ip */
	ip = info->ip = *info->rp_loc;
	if (ip < GATE_ADDR) {
		UNW_DPRINT(2, "unwind.%s: reached user-space (ip=0x%lx)\n", __FUNCTION__, ip);
		STAT(unw.stat.api.unwind_time += ia64_get_itc() - start; local_irq_restore(flags));
		return -1;
	}

	/* validate the previous stack frame pointer */
	if (!unw_valid(info, info->pfs_loc)) {
		UNW_DPRINT(0, "unwind.%s: failed to locate ar.pfs!\n", __FUNCTION__);
		STAT(unw.stat.api.unwind_time += ia64_get_itc() - start; local_irq_restore(flags));
		return -1;
	}
	/* restore the cfm: */
	info->cfm_loc = info->pfs_loc;

	/* restore the bsp: */
	pr = info->pr;
	num_regs = 0;
	if ((info->flags & UNW_FLAG_INTERRUPT_FRAME)) {
		info->pt = info->sp + 16;
		if ((pr & (1UL << PRED_NON_SYSCALL)) != 0)
			num_regs = *info->cfm_loc & 0x7f;		/* size of frame */
		info->pfs_loc =
			(unsigned long *) (info->pt + offsetof(struct pt_regs, ar_pfs));
		UNW_DPRINT(3, "unwind.%s: interrupt_frame pt 0x%lx\n", __FUNCTION__, info->pt);
	} else
		num_regs = (*info->cfm_loc >> 7) & 0x7f;	/* size of locals */
	info->bsp = (unsigned long) ia64_rse_skip_regs((unsigned long *) info->bsp, -num_regs);
	if (info->bsp < info->regstk.limit || info->bsp > info->regstk.top) {
		UNW_DPRINT(0, "unwind.%s: bsp (0x%lx) out of range [0x%lx-0x%lx]\n",
			__FUNCTION__, info->bsp, info->regstk.limit, info->regstk.top);
		STAT(unw.stat.api.unwind_time += ia64_get_itc() - start; local_irq_restore(flags));
		return -1;
	}

	/* restore the sp: */
	info->sp = info->psp;
	if (info->sp < info->memstk.top || info->sp > info->memstk.limit) {
		UNW_DPRINT(0, "unwind.%s: sp (0x%lx) out of range [0x%lx-0x%lx]\n",
			__FUNCTION__, info->sp, info->memstk.top, info->memstk.limit);
		STAT(unw.stat.api.unwind_time += ia64_get_itc() - start; local_irq_restore(flags));
		return -1;
	}

	if (info->ip == prev_ip && info->sp == prev_sp && info->bsp == prev_bsp) {
		UNW_DPRINT(0, "unwind.%s: ip, sp, bsp unchanged; stopping here (ip=0x%lx)\n",
			   __FUNCTION__, ip);
		STAT(unw.stat.api.unwind_time += ia64_get_itc() - start; local_irq_restore(flags));
		return -1;
	}

	/* as we unwind, the saved ar.unat becomes the primary unat: */
	info->pri_unat_loc = info->unat_loc;

	/* finally, restore the predicates: */
	unw_get_pr(info, &info->pr);

	retval = find_save_locs(info);
	STAT(unw.stat.api.unwind_time += ia64_get_itc() - start; local_irq_restore(flags));
	return retval;
}
EXPORT_SYMBOL(unw_unwind);

int
unw_unwind_to_user (struct unw_frame_info *info)
{
	unsigned long ip, sp, pr = info->pr;

	do {
		unw_get_sp(info, &sp);
		if ((long)((unsigned long)info->task + IA64_STK_OFFSET - sp)
		    < IA64_PT_REGS_SIZE) {
			UNW_DPRINT(0, "unwind.%s: ran off the top of the kernel stack\n",
				   __FUNCTION__);
			break;
		}
		if (unw_is_intr_frame(info) &&
		    (pr & (1UL << PRED_USER_STACK)))
			return 0;
		if (unw_get_pr (info, &pr) < 0) {
			unw_get_rp(info, &ip);
			UNW_DPRINT(0, "unwind.%s: failed to read "
				   "predicate register (ip=0x%lx)\n",
				__FUNCTION__, ip);
			return -1;
		}
	} while (unw_unwind(info) >= 0);
	unw_get_ip(info, &ip);
	UNW_DPRINT(0, "unwind.%s: failed to unwind to user-level (ip=0x%lx)\n",
		   __FUNCTION__, ip);
	return -1;
}
EXPORT_SYMBOL(unw_unwind_to_user);

static void
init_frame_info (struct unw_frame_info *info, struct task_struct *t,
		 struct switch_stack *sw, unsigned long stktop)
{
	unsigned long rbslimit, rbstop, stklimit;
	STAT(unsigned long start, flags;)

	STAT(local_irq_save(flags); ++unw.stat.api.inits; start = ia64_get_itc());

	/*
	 * Subtle stuff here: we _could_ unwind through the switch_stack frame but we
	 * don't want to do that because it would be slow as each preserved register would
	 * have to be processed.  Instead, what we do here is zero out the frame info and
	 * start the unwind process at the function that created the switch_stack frame.
	 * When a preserved value in switch_stack needs to be accessed, run_script() will
	 * initialize the appropriate pointer on demand.
	 */
	memset(info, 0, sizeof(*info));

	rbslimit = (unsigned long) t + IA64_RBS_OFFSET;
	stklimit = (unsigned long) t + IA64_STK_OFFSET;

	rbstop   = sw->ar_bspstore;
	if (rbstop > stklimit || rbstop < rbslimit)
		rbstop = rbslimit;

	if (stktop <= rbstop)
		stktop = rbstop;
	if (stktop > stklimit)
		stktop = stklimit;

	info->regstk.limit = rbslimit;
	info->regstk.top   = rbstop;
	info->memstk.limit = stklimit;
	info->memstk.top   = stktop;
	info->task = t;
	info->sw  = sw;
	info->sp = info->psp = stktop;
	info->pr = sw->pr;
	UNW_DPRINT(3, "unwind.%s:\n"
		   "  task   0x%lx\n"
		   "  rbs = [0x%lx-0x%lx)\n"
		   "  stk = [0x%lx-0x%lx)\n"
		   "  pr     0x%lx\n"
		   "  sw     0x%lx\n"
		   "  sp     0x%lx\n",
		   __FUNCTION__, (unsigned long) t, rbslimit, rbstop, stktop, stklimit,
		   info->pr, (unsigned long) info->sw, info->sp);
	STAT(unw.stat.api.init_time += ia64_get_itc() - start; local_irq_restore(flags));
}

void
unw_init_frame_info (struct unw_frame_info *info, struct task_struct *t, struct switch_stack *sw)
{
	unsigned long sol;

	init_frame_info(info, t, sw, (unsigned long) (sw + 1) - 16);
	info->cfm_loc = &sw->ar_pfs;
	sol = (*info->cfm_loc >> 7) & 0x7f;
	info->bsp = (unsigned long) ia64_rse_skip_regs((unsigned long *) info->regstk.top, -sol);
	info->ip = sw->b0;
	UNW_DPRINT(3, "unwind.%s:\n"
		   "  bsp    0x%lx\n"
		   "  sol    0x%lx\n"
		   "  ip     0x%lx\n",
		   __FUNCTION__, info->bsp, sol, info->ip);
	find_save_locs(info);
}

EXPORT_SYMBOL(unw_init_frame_info);

void
unw_init_from_blocked_task (struct unw_frame_info *info, struct task_struct *t)
{
	struct switch_stack *sw = (struct switch_stack *) (t->thread.ksp + 16);

	UNW_DPRINT(1, "unwind.%s\n", __FUNCTION__);
	unw_init_frame_info(info, t, sw);
}
EXPORT_SYMBOL(unw_init_from_blocked_task);

static void
init_unwind_table (struct unw_table *table, const char *name, unsigned long segment_base,
		   unsigned long gp, const void *table_start, const void *table_end)
{
	const struct unw_table_entry *start = table_start, *end = table_end;

	table->name = name;
	table->segment_base = segment_base;
	table->gp = gp;
	table->start = segment_base + start[0].start_offset;
	table->end = segment_base + end[-1].end_offset;
	table->array = start;
	table->length = end - start;
}

void *
unw_add_unwind_table (const char *name, unsigned long segment_base, unsigned long gp,
		      const void *table_start, const void *table_end)
{
	const struct unw_table_entry *start = table_start, *end = table_end;
	struct unw_table *table;
	unsigned long flags;

	if (end - start <= 0) {
		UNW_DPRINT(0, "unwind.%s: ignoring attempt to insert empty unwind table\n",
			   __FUNCTION__);
		return NULL;
	}

	table = kmalloc(sizeof(*table), GFP_USER);
	if (!table)
		return NULL;

	init_unwind_table(table, name, segment_base, gp, table_start, table_end);

	spin_lock_irqsave(&unw.lock, flags);
	{
		/* keep kernel unwind table at the front (it's searched most commonly): */
		table->next = unw.tables->next;
		unw.tables->next = table;
	}
	spin_unlock_irqrestore(&unw.lock, flags);

	return table;
}

void
unw_remove_unwind_table (void *handle)
{
	struct unw_table *table, *prev;
	struct unw_script *tmp;
	unsigned long flags;
	long index;

	if (!handle) {
		UNW_DPRINT(0, "unwind.%s: ignoring attempt to remove non-existent unwind table\n",
			   __FUNCTION__);
		return;
	}

	table = handle;
	if (table == &unw.kernel_table) {
		UNW_DPRINT(0, "unwind.%s: sorry, freeing the kernel's unwind table is a "
			   "no-can-do!\n", __FUNCTION__);
		return;
	}

	spin_lock_irqsave(&unw.lock, flags);
	{
		/* first, delete the table: */

		for (prev = (struct unw_table *) &unw.tables; prev; prev = prev->next)
			if (prev->next == table)
				break;
		if (!prev) {
			UNW_DPRINT(0, "unwind.%s: failed to find unwind table %p\n",
				   __FUNCTION__, (void *) table);
			spin_unlock_irqrestore(&unw.lock, flags);
			return;
		}
		prev->next = table->next;
	}
	spin_unlock_irqrestore(&unw.lock, flags);

	/* next, remove hash table entries for this table */

	for (index = 0; index <= UNW_HASH_SIZE; ++index) {
		tmp = unw.cache + unw.hash[index];
		if (unw.hash[index] >= UNW_CACHE_SIZE
		    || tmp->ip < table->start || tmp->ip >= table->end)
			continue;

		write_lock(&tmp->lock);
		{
			if (tmp->ip >= table->start && tmp->ip < table->end) {
				unw.hash[index] = tmp->coll_chain;
				tmp->ip = 0;
			}
		}
		write_unlock(&tmp->lock);
	}

	kfree(table);
}

static int __init
create_gate_table (void)
{
	const struct unw_table_entry *entry, *start, *end;
	unsigned long *lp, segbase = GATE_ADDR;
	size_t info_size, size;
	char *info;
	Elf64_Phdr *punw = NULL, *phdr = (Elf64_Phdr *) (GATE_ADDR + GATE_EHDR->e_phoff);
	int i;

	for (i = 0; i < GATE_EHDR->e_phnum; ++i, ++phdr)
		if (phdr->p_type == PT_IA_64_UNWIND) {
			punw = phdr;
			break;
		}

	if (!punw) {
		printk("%s: failed to find gate DSO's unwind table!\n", __FUNCTION__);
		return 0;
	}

	start = (const struct unw_table_entry *) punw->p_vaddr;
	end = (struct unw_table_entry *) ((char *) start + punw->p_memsz);
	size  = 0;

	unw_add_unwind_table("linux-gate.so", segbase, 0, start, end);

	for (entry = start; entry < end; ++entry)
		size += 3*8 + 8 + 8*UNW_LENGTH(*(u64 *) (segbase + entry->info_offset));
	size += 8;	/* reserve space for "end of table" marker */

	unw.gate_table = kmalloc(size, GFP_KERNEL);
	if (!unw.gate_table) {
		unw.gate_table_size = 0;
		printk(KERN_ERR "%s: unable to create unwind data for gate page!\n", __FUNCTION__);
		return 0;
	}
	unw.gate_table_size = size;

	lp = unw.gate_table;
	info = (char *) unw.gate_table + size;

	for (entry = start; entry < end; ++entry, lp += 3) {
		info_size = 8 + 8*UNW_LENGTH(*(u64 *) (segbase + entry->info_offset));
		info -= info_size;
		memcpy(info, (char *) segbase + entry->info_offset, info_size);

		lp[0] = segbase + entry->start_offset;		/* start */
		lp[1] = segbase + entry->end_offset;		/* end */
		lp[2] = info - (char *) unw.gate_table;		/* info */
	}
	*lp = 0;	/* end-of-table marker */
	return 0;
}

__initcall(create_gate_table);

void __init
unw_init (void)
{
	extern char __gp[];
	extern void unw_hash_index_t_is_too_narrow (void);
	long i, off;

	if (8*sizeof(unw_hash_index_t) < UNW_LOG_HASH_SIZE)
		unw_hash_index_t_is_too_narrow();

	unw.sw_off[unw.preg_index[UNW_REG_PRI_UNAT_GR]] = SW(CALLER_UNAT);
	unw.sw_off[unw.preg_index[UNW_REG_BSPSTORE]] = SW(AR_BSPSTORE);
	unw.sw_off[unw.preg_index[UNW_REG_PFS]] = SW(AR_PFS);
	unw.sw_off[unw.preg_index[UNW_REG_RP]] = SW(B0);
	unw.sw_off[unw.preg_index[UNW_REG_UNAT]] = SW(CALLER_UNAT);
	unw.sw_off[unw.preg_index[UNW_REG_PR]] = SW(PR);
	unw.sw_off[unw.preg_index[UNW_REG_LC]] = SW(AR_LC);
	unw.sw_off[unw.preg_index[UNW_REG_FPSR]] = SW(AR_FPSR);
	for (i = UNW_REG_R4, off = SW(R4); i <= UNW_REG_R7; ++i, off += 8)
		unw.sw_off[unw.preg_index[i]] = off;
	for (i = UNW_REG_B1, off = SW(B1); i <= UNW_REG_B5; ++i, off += 8)
		unw.sw_off[unw.preg_index[i]] = off;
	for (i = UNW_REG_F2, off = SW(F2); i <= UNW_REG_F5; ++i, off += 16)
		unw.sw_off[unw.preg_index[i]] = off;
	for (i = UNW_REG_F16, off = SW(F16); i <= UNW_REG_F31; ++i, off += 16)
		unw.sw_off[unw.preg_index[i]] = off;

	for (i = 0; i < UNW_CACHE_SIZE; ++i) {
		if (i > 0)
			unw.cache[i].lru_chain = (i - 1);
		unw.cache[i].coll_chain = -1;
		rwlock_init(&unw.cache[i].lock);
	}
	unw.lru_head = UNW_CACHE_SIZE - 1;
	unw.lru_tail = 0;

	init_unwind_table(&unw.kernel_table, "kernel", KERNEL_START, (unsigned long) __gp,
			  __start_unwind, __end_unwind);
}

/*
 * DEPRECATED DEPRECATED DEPRECATED DEPRECATED DEPRECATED DEPRECATED DEPRECATED
 *
 *	This system call has been deprecated.  The new and improved way to get
 *	at the kernel's unwind info is via the gate DSO.  The address of the
 *	ELF header for this DSO is passed to user-level via AT_SYSINFO_EHDR.
 *
 * DEPRECATED DEPRECATED DEPRECATED DEPRECATED DEPRECATED DEPRECATED DEPRECATED
 *
 * This system call copies the unwind data into the buffer pointed to by BUF and returns
 * the size of the unwind data.  If BUF_SIZE is smaller than the size of the unwind data
 * or if BUF is NULL, nothing is copied, but the system call still returns the size of the
 * unwind data.
 *
 * The first portion of the unwind data contains an unwind table and rest contains the
 * associated unwind info (in no particular order).  The unwind table consists of a table
 * of entries of the form:
 *
 *	u64 start;	(64-bit address of start of function)
 *	u64 end;	(64-bit address of start of function)
 *	u64 info;	(BUF-relative offset to unwind info)
 *
 * The end of the unwind table is indicated by an entry with a START address of zero.
 *
 * Please see the IA-64 Software Conventions and Runtime Architecture manual for details
 * on the format of the unwind info.
 *
 * ERRORS
 *	EFAULT	BUF points outside your accessible address space.
 */
asmlinkage long
sys_getunwind (void __user *buf, size_t buf_size)
{
	if (buf && buf_size >= unw.gate_table_size)
		if (copy_to_user(buf, unw.gate_table, unw.gate_table_size) != 0)
			return -EFAULT;
	return unw.gate_table_size;
}
