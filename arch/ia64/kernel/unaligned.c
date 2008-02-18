/*
 * Architecture-specific unaligned trap handling.
 *
 * Copyright (C) 1999-2002, 2004 Hewlett-Packard Co
 *	Stephane Eranian <eranian@hpl.hp.com>
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * 2002/12/09   Fix rotating register handling (off-by-1 error, missing fr-rotation).  Fix
 *		get_rse_reg() to not leak kernel bits to user-level (reading an out-of-frame
 *		stacked register returns an undefined value; it does NOT trigger a
 *		"rsvd register fault").
 * 2001/10/11	Fix unaligned access to rotating registers in s/w pipelined loops.
 * 2001/08/13	Correct size of extended floats (float_fsz) from 16 to 10 bytes.
 * 2001/01/17	Add support emulation of unaligned kernel accesses.
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/tty.h>

#include <asm/intrinsics.h>
#include <asm/processor.h>
#include <asm/rse.h>
#include <asm/uaccess.h>
#include <asm/unaligned.h>

extern int die_if_kernel(char *str, struct pt_regs *regs, long err);

#undef DEBUG_UNALIGNED_TRAP

#ifdef DEBUG_UNALIGNED_TRAP
# define DPRINT(a...)	do { printk("%s %u: ", __FUNCTION__, __LINE__); printk (a); } while (0)
# define DDUMP(str,vp,len)	dump(str, vp, len)

static void
dump (const char *str, void *vp, size_t len)
{
	unsigned char *cp = vp;
	int i;

	printk("%s", str);
	for (i = 0; i < len; ++i)
		printk (" %02x", *cp++);
	printk("\n");
}
#else
# define DPRINT(a...)
# define DDUMP(str,vp,len)
#endif

#define IA64_FIRST_STACKED_GR	32
#define IA64_FIRST_ROTATING_FR	32
#define SIGN_EXT9		0xffffffffffffff00ul

/*
 *  sysctl settable hook which tells the kernel whether to honor the
 *  IA64_THREAD_UAC_NOPRINT prctl.  Because this is user settable, we want
 *  to allow the super user to enable/disable this for security reasons
 *  (i.e. don't allow attacker to fill up logs with unaligned accesses).
 */
int no_unaligned_warning;
static int noprint_warning;

/*
 * For M-unit:
 *
 *  opcode |   m  |   x6    |
 * --------|------|---------|
 * [40-37] | [36] | [35:30] |
 * --------|------|---------|
 *     4   |   1  |    6    | = 11 bits
 * --------------------------
 * However bits [31:30] are not directly useful to distinguish between
 * load/store so we can use [35:32] instead, which gives the following
 * mask ([40:32]) using 9 bits. The 'e' comes from the fact that we defer
 * checking the m-bit until later in the load/store emulation.
 */
#define IA64_OPCODE_MASK	0x1ef
#define IA64_OPCODE_SHIFT	32

/*
 * Table C-28 Integer Load/Store
 *
 * We ignore [35:32]= 0x6, 0x7, 0xE, 0xF
 *
 * ld8.fill, st8.fill  MUST be aligned because the RNATs are based on
 * the address (bits [8:3]), so we must failed.
 */
#define LD_OP            0x080
#define LDS_OP           0x081
#define LDA_OP           0x082
#define LDSA_OP          0x083
#define LDBIAS_OP        0x084
#define LDACQ_OP         0x085
/* 0x086, 0x087 are not relevant */
#define LDCCLR_OP        0x088
#define LDCNC_OP         0x089
#define LDCCLRACQ_OP     0x08a
#define ST_OP            0x08c
#define STREL_OP         0x08d
/* 0x08e,0x8f are not relevant */

/*
 * Table C-29 Integer Load +Reg
 *
 * we use the ld->m (bit [36:36]) field to determine whether or not we have
 * a load/store of this form.
 */

/*
 * Table C-30 Integer Load/Store +Imm
 *
 * We ignore [35:32]= 0x6, 0x7, 0xE, 0xF
 *
 * ld8.fill, st8.fill  must be aligned because the Nat register are based on
 * the address, so we must fail and the program must be fixed.
 */
#define LD_IMM_OP            0x0a0
#define LDS_IMM_OP           0x0a1
#define LDA_IMM_OP           0x0a2
#define LDSA_IMM_OP          0x0a3
#define LDBIAS_IMM_OP        0x0a4
#define LDACQ_IMM_OP         0x0a5
/* 0x0a6, 0xa7 are not relevant */
#define LDCCLR_IMM_OP        0x0a8
#define LDCNC_IMM_OP         0x0a9
#define LDCCLRACQ_IMM_OP     0x0aa
#define ST_IMM_OP            0x0ac
#define STREL_IMM_OP         0x0ad
/* 0x0ae,0xaf are not relevant */

/*
 * Table C-32 Floating-point Load/Store
 */
#define LDF_OP           0x0c0
#define LDFS_OP          0x0c1
#define LDFA_OP          0x0c2
#define LDFSA_OP         0x0c3
/* 0x0c6 is irrelevant */
#define LDFCCLR_OP       0x0c8
#define LDFCNC_OP        0x0c9
/* 0x0cb is irrelevant  */
#define STF_OP           0x0cc

/*
 * Table C-33 Floating-point Load +Reg
 *
 * we use the ld->m (bit [36:36]) field to determine whether or not we have
 * a load/store of this form.
 */

/*
 * Table C-34 Floating-point Load/Store +Imm
 */
#define LDF_IMM_OP       0x0e0
#define LDFS_IMM_OP      0x0e1
#define LDFA_IMM_OP      0x0e2
#define LDFSA_IMM_OP     0x0e3
/* 0x0e6 is irrelevant */
#define LDFCCLR_IMM_OP   0x0e8
#define LDFCNC_IMM_OP    0x0e9
#define STF_IMM_OP       0x0ec

typedef struct {
	unsigned long	 qp:6;	/* [0:5]   */
	unsigned long    r1:7;	/* [6:12]  */
	unsigned long   imm:7;	/* [13:19] */
	unsigned long    r3:7;	/* [20:26] */
	unsigned long     x:1;  /* [27:27] */
	unsigned long  hint:2;	/* [28:29] */
	unsigned long x6_sz:2;	/* [30:31] */
	unsigned long x6_op:4;	/* [32:35], x6 = x6_sz|x6_op */
	unsigned long     m:1;	/* [36:36] */
	unsigned long    op:4;	/* [37:40] */
	unsigned long   pad:23; /* [41:63] */
} load_store_t;


typedef enum {
	UPD_IMMEDIATE,	/* ldXZ r1=[r3],imm(9) */
	UPD_REG		/* ldXZ r1=[r3],r2     */
} update_t;

/*
 * We use tables to keep track of the offsets of registers in the saved state.
 * This way we save having big switch/case statements.
 *
 * We use bit 0 to indicate switch_stack or pt_regs.
 * The offset is simply shifted by 1 bit.
 * A 2-byte value should be enough to hold any kind of offset
 *
 * In case the calling convention changes (and thus pt_regs/switch_stack)
 * simply use RSW instead of RPT or vice-versa.
 */

#define RPO(x)	((size_t) &((struct pt_regs *)0)->x)
#define RSO(x)	((size_t) &((struct switch_stack *)0)->x)

#define RPT(x)		(RPO(x) << 1)
#define RSW(x)		(1| RSO(x)<<1)

#define GR_OFFS(x)	(gr_info[x]>>1)
#define GR_IN_SW(x)	(gr_info[x] & 0x1)

#define FR_OFFS(x)	(fr_info[x]>>1)
#define FR_IN_SW(x)	(fr_info[x] & 0x1)

static u16 gr_info[32]={
	0,			/* r0 is read-only : WE SHOULD NEVER GET THIS */

	RPT(r1), RPT(r2), RPT(r3),

	RSW(r4), RSW(r5), RSW(r6), RSW(r7),

	RPT(r8), RPT(r9), RPT(r10), RPT(r11),
	RPT(r12), RPT(r13), RPT(r14), RPT(r15),

	RPT(r16), RPT(r17), RPT(r18), RPT(r19),
	RPT(r20), RPT(r21), RPT(r22), RPT(r23),
	RPT(r24), RPT(r25), RPT(r26), RPT(r27),
	RPT(r28), RPT(r29), RPT(r30), RPT(r31)
};

static u16 fr_info[32]={
	0,			/* constant : WE SHOULD NEVER GET THIS */
	0,			/* constant : WE SHOULD NEVER GET THIS */

	RSW(f2), RSW(f3), RSW(f4), RSW(f5),

	RPT(f6), RPT(f7), RPT(f8), RPT(f9),
	RPT(f10), RPT(f11),

	RSW(f12), RSW(f13), RSW(f14),
	RSW(f15), RSW(f16), RSW(f17), RSW(f18), RSW(f19),
	RSW(f20), RSW(f21), RSW(f22), RSW(f23), RSW(f24),
	RSW(f25), RSW(f26), RSW(f27), RSW(f28), RSW(f29),
	RSW(f30), RSW(f31)
};

/* Invalidate ALAT entry for integer register REGNO.  */
static void
invala_gr (int regno)
{
#	define F(reg)	case reg: ia64_invala_gr(reg); break

	switch (regno) {
		F(  0); F(  1); F(  2); F(  3); F(  4); F(  5); F(  6); F(  7);
		F(  8); F(  9); F( 10); F( 11); F( 12); F( 13); F( 14); F( 15);
		F( 16); F( 17); F( 18); F( 19); F( 20); F( 21); F( 22); F( 23);
		F( 24); F( 25); F( 26); F( 27); F( 28); F( 29); F( 30); F( 31);
		F( 32); F( 33); F( 34); F( 35); F( 36); F( 37); F( 38); F( 39);
		F( 40); F( 41); F( 42); F( 43); F( 44); F( 45); F( 46); F( 47);
		F( 48); F( 49); F( 50); F( 51); F( 52); F( 53); F( 54); F( 55);
		F( 56); F( 57); F( 58); F( 59); F( 60); F( 61); F( 62); F( 63);
		F( 64); F( 65); F( 66); F( 67); F( 68); F( 69); F( 70); F( 71);
		F( 72); F( 73); F( 74); F( 75); F( 76); F( 77); F( 78); F( 79);
		F( 80); F( 81); F( 82); F( 83); F( 84); F( 85); F( 86); F( 87);
		F( 88); F( 89); F( 90); F( 91); F( 92); F( 93); F( 94); F( 95);
		F( 96); F( 97); F( 98); F( 99); F(100); F(101); F(102); F(103);
		F(104); F(105); F(106); F(107); F(108); F(109); F(110); F(111);
		F(112); F(113); F(114); F(115); F(116); F(117); F(118); F(119);
		F(120); F(121); F(122); F(123); F(124); F(125); F(126); F(127);
	}
#	undef F
}

/* Invalidate ALAT entry for floating-point register REGNO.  */
static void
invala_fr (int regno)
{
#	define F(reg)	case reg: ia64_invala_fr(reg); break

	switch (regno) {
		F(  0); F(  1); F(  2); F(  3); F(  4); F(  5); F(  6); F(  7);
		F(  8); F(  9); F( 10); F( 11); F( 12); F( 13); F( 14); F( 15);
		F( 16); F( 17); F( 18); F( 19); F( 20); F( 21); F( 22); F( 23);
		F( 24); F( 25); F( 26); F( 27); F( 28); F( 29); F( 30); F( 31);
		F( 32); F( 33); F( 34); F( 35); F( 36); F( 37); F( 38); F( 39);
		F( 40); F( 41); F( 42); F( 43); F( 44); F( 45); F( 46); F( 47);
		F( 48); F( 49); F( 50); F( 51); F( 52); F( 53); F( 54); F( 55);
		F( 56); F( 57); F( 58); F( 59); F( 60); F( 61); F( 62); F( 63);
		F( 64); F( 65); F( 66); F( 67); F( 68); F( 69); F( 70); F( 71);
		F( 72); F( 73); F( 74); F( 75); F( 76); F( 77); F( 78); F( 79);
		F( 80); F( 81); F( 82); F( 83); F( 84); F( 85); F( 86); F( 87);
		F( 88); F( 89); F( 90); F( 91); F( 92); F( 93); F( 94); F( 95);
		F( 96); F( 97); F( 98); F( 99); F(100); F(101); F(102); F(103);
		F(104); F(105); F(106); F(107); F(108); F(109); F(110); F(111);
		F(112); F(113); F(114); F(115); F(116); F(117); F(118); F(119);
		F(120); F(121); F(122); F(123); F(124); F(125); F(126); F(127);
	}
#	undef F
}

static inline unsigned long
rotate_reg (unsigned long sor, unsigned long rrb, unsigned long reg)
{
	reg += rrb;
	if (reg >= sor)
		reg -= sor;
	return reg;
}

static void
set_rse_reg (struct pt_regs *regs, unsigned long r1, unsigned long val, int nat)
{
	struct switch_stack *sw = (struct switch_stack *) regs - 1;
	unsigned long *bsp, *bspstore, *addr, *rnat_addr, *ubs_end;
	unsigned long *kbs = (void *) current + IA64_RBS_OFFSET;
	unsigned long rnats, nat_mask;
	unsigned long on_kbs;
	long sof = (regs->cr_ifs) & 0x7f;
	long sor = 8 * ((regs->cr_ifs >> 14) & 0xf);
	long rrb_gr = (regs->cr_ifs >> 18) & 0x7f;
	long ridx = r1 - 32;

	if (ridx >= sof) {
		/* this should never happen, as the "rsvd register fault" has higher priority */
		DPRINT("ignoring write to r%lu; only %lu registers are allocated!\n", r1, sof);
		return;
	}

	if (ridx < sor)
		ridx = rotate_reg(sor, rrb_gr, ridx);

	DPRINT("r%lu, sw.bspstore=%lx pt.bspstore=%lx sof=%ld sol=%ld ridx=%ld\n",
	       r1, sw->ar_bspstore, regs->ar_bspstore, sof, (regs->cr_ifs >> 7) & 0x7f, ridx);

	on_kbs = ia64_rse_num_regs(kbs, (unsigned long *) sw->ar_bspstore);
	addr = ia64_rse_skip_regs((unsigned long *) sw->ar_bspstore, -sof + ridx);
	if (addr >= kbs) {
		/* the register is on the kernel backing store: easy... */
		rnat_addr = ia64_rse_rnat_addr(addr);
		if ((unsigned long) rnat_addr >= sw->ar_bspstore)
			rnat_addr = &sw->ar_rnat;
		nat_mask = 1UL << ia64_rse_slot_num(addr);

		*addr = val;
		if (nat)
			*rnat_addr |=  nat_mask;
		else
			*rnat_addr &= ~nat_mask;
		return;
	}

	if (!user_stack(current, regs)) {
		DPRINT("ignoring kernel write to r%lu; register isn't on the kernel RBS!", r1);
		return;
	}

	bspstore = (unsigned long *)regs->ar_bspstore;
	ubs_end = ia64_rse_skip_regs(bspstore, on_kbs);
	bsp     = ia64_rse_skip_regs(ubs_end, -sof);
	addr    = ia64_rse_skip_regs(bsp, ridx);

	DPRINT("ubs_end=%p bsp=%p addr=%p\n", (void *) ubs_end, (void *) bsp, (void *) addr);

	ia64_poke(current, sw, (unsigned long) ubs_end, (unsigned long) addr, val);

	rnat_addr = ia64_rse_rnat_addr(addr);

	ia64_peek(current, sw, (unsigned long) ubs_end, (unsigned long) rnat_addr, &rnats);
	DPRINT("rnat @%p = 0x%lx nat=%d old nat=%ld\n",
	       (void *) rnat_addr, rnats, nat, (rnats >> ia64_rse_slot_num(addr)) & 1);

	nat_mask = 1UL << ia64_rse_slot_num(addr);
	if (nat)
		rnats |=  nat_mask;
	else
		rnats &= ~nat_mask;
	ia64_poke(current, sw, (unsigned long) ubs_end, (unsigned long) rnat_addr, rnats);

	DPRINT("rnat changed to @%p = 0x%lx\n", (void *) rnat_addr, rnats);
}


static void
get_rse_reg (struct pt_regs *regs, unsigned long r1, unsigned long *val, int *nat)
{
	struct switch_stack *sw = (struct switch_stack *) regs - 1;
	unsigned long *bsp, *addr, *rnat_addr, *ubs_end, *bspstore;
	unsigned long *kbs = (void *) current + IA64_RBS_OFFSET;
	unsigned long rnats, nat_mask;
	unsigned long on_kbs;
	long sof = (regs->cr_ifs) & 0x7f;
	long sor = 8 * ((regs->cr_ifs >> 14) & 0xf);
	long rrb_gr = (regs->cr_ifs >> 18) & 0x7f;
	long ridx = r1 - 32;

	if (ridx >= sof) {
		/* read of out-of-frame register returns an undefined value; 0 in our case.  */
		DPRINT("ignoring read from r%lu; only %lu registers are allocated!\n", r1, sof);
		goto fail;
	}

	if (ridx < sor)
		ridx = rotate_reg(sor, rrb_gr, ridx);

	DPRINT("r%lu, sw.bspstore=%lx pt.bspstore=%lx sof=%ld sol=%ld ridx=%ld\n",
	       r1, sw->ar_bspstore, regs->ar_bspstore, sof, (regs->cr_ifs >> 7) & 0x7f, ridx);

	on_kbs = ia64_rse_num_regs(kbs, (unsigned long *) sw->ar_bspstore);
	addr = ia64_rse_skip_regs((unsigned long *) sw->ar_bspstore, -sof + ridx);
	if (addr >= kbs) {
		/* the register is on the kernel backing store: easy... */
		*val = *addr;
		if (nat) {
			rnat_addr = ia64_rse_rnat_addr(addr);
			if ((unsigned long) rnat_addr >= sw->ar_bspstore)
				rnat_addr = &sw->ar_rnat;
			nat_mask = 1UL << ia64_rse_slot_num(addr);
			*nat = (*rnat_addr & nat_mask) != 0;
		}
		return;
	}

	if (!user_stack(current, regs)) {
		DPRINT("ignoring kernel read of r%lu; register isn't on the RBS!", r1);
		goto fail;
	}

	bspstore = (unsigned long *)regs->ar_bspstore;
	ubs_end = ia64_rse_skip_regs(bspstore, on_kbs);
	bsp     = ia64_rse_skip_regs(ubs_end, -sof);
	addr    = ia64_rse_skip_regs(bsp, ridx);

	DPRINT("ubs_end=%p bsp=%p addr=%p\n", (void *) ubs_end, (void *) bsp, (void *) addr);

	ia64_peek(current, sw, (unsigned long) ubs_end, (unsigned long) addr, val);

	if (nat) {
		rnat_addr = ia64_rse_rnat_addr(addr);
		nat_mask = 1UL << ia64_rse_slot_num(addr);

		DPRINT("rnat @%p = 0x%lx\n", (void *) rnat_addr, rnats);

		ia64_peek(current, sw, (unsigned long) ubs_end, (unsigned long) rnat_addr, &rnats);
		*nat = (rnats & nat_mask) != 0;
	}
	return;

  fail:
	*val = 0;
	if (nat)
		*nat = 0;
	return;
}


static void
setreg (unsigned long regnum, unsigned long val, int nat, struct pt_regs *regs)
{
	struct switch_stack *sw = (struct switch_stack *) regs - 1;
	unsigned long addr;
	unsigned long bitmask;
	unsigned long *unat;

	/*
	 * First takes care of stacked registers
	 */
	if (regnum >= IA64_FIRST_STACKED_GR) {
		set_rse_reg(regs, regnum, val, nat);
		return;
	}

	/*
	 * Using r0 as a target raises a General Exception fault which has higher priority
	 * than the Unaligned Reference fault.
	 */

	/*
	 * Now look at registers in [0-31] range and init correct UNAT
	 */
	if (GR_IN_SW(regnum)) {
		addr = (unsigned long)sw;
		unat = &sw->ar_unat;
	} else {
		addr = (unsigned long)regs;
		unat = &sw->caller_unat;
	}
	DPRINT("tmp_base=%lx switch_stack=%s offset=%d\n",
	       addr, unat==&sw->ar_unat ? "yes":"no", GR_OFFS(regnum));
	/*
	 * add offset from base of struct
	 * and do it !
	 */
	addr += GR_OFFS(regnum);

	*(unsigned long *)addr = val;

	/*
	 * We need to clear the corresponding UNAT bit to fully emulate the load
	 * UNAT bit_pos = GR[r3]{8:3} form EAS-2.4
	 */
	bitmask   = 1UL << (addr >> 3 & 0x3f);
	DPRINT("*0x%lx=0x%lx NaT=%d prev_unat @%p=%lx\n", addr, val, nat, (void *) unat, *unat);
	if (nat) {
		*unat |= bitmask;
	} else {
		*unat &= ~bitmask;
	}
	DPRINT("*0x%lx=0x%lx NaT=%d new unat: %p=%lx\n", addr, val, nat, (void *) unat,*unat);
}

/*
 * Return the (rotated) index for floating point register REGNUM (REGNUM must be in the
 * range from 32-127, result is in the range from 0-95.
 */
static inline unsigned long
fph_index (struct pt_regs *regs, long regnum)
{
	unsigned long rrb_fr = (regs->cr_ifs >> 25) & 0x7f;
	return rotate_reg(96, rrb_fr, (regnum - IA64_FIRST_ROTATING_FR));
}

static void
setfpreg (unsigned long regnum, struct ia64_fpreg *fpval, struct pt_regs *regs)
{
	struct switch_stack *sw = (struct switch_stack *)regs - 1;
	unsigned long addr;

	/*
	 * From EAS-2.5: FPDisableFault has higher priority than Unaligned
	 * Fault. Thus, when we get here, we know the partition is enabled.
	 * To update f32-f127, there are three choices:
	 *
	 *	(1) save f32-f127 to thread.fph and update the values there
	 *	(2) use a gigantic switch statement to directly access the registers
	 *	(3) generate code on the fly to update the desired register
	 *
	 * For now, we are using approach (1).
	 */
	if (regnum >= IA64_FIRST_ROTATING_FR) {
		ia64_sync_fph(current);
		current->thread.fph[fph_index(regs, regnum)] = *fpval;
	} else {
		/*
		 * pt_regs or switch_stack ?
		 */
		if (FR_IN_SW(regnum)) {
			addr = (unsigned long)sw;
		} else {
			addr = (unsigned long)regs;
		}

		DPRINT("tmp_base=%lx offset=%d\n", addr, FR_OFFS(regnum));

		addr += FR_OFFS(regnum);
		*(struct ia64_fpreg *)addr = *fpval;

		/*
		 * mark the low partition as being used now
		 *
		 * It is highly unlikely that this bit is not already set, but
		 * let's do it for safety.
		 */
		regs->cr_ipsr |= IA64_PSR_MFL;
	}
}

/*
 * Those 2 inline functions generate the spilled versions of the constant floating point
 * registers which can be used with stfX
 */
static inline void
float_spill_f0 (struct ia64_fpreg *final)
{
	ia64_stf_spill(final, 0);
}

static inline void
float_spill_f1 (struct ia64_fpreg *final)
{
	ia64_stf_spill(final, 1);
}

static void
getfpreg (unsigned long regnum, struct ia64_fpreg *fpval, struct pt_regs *regs)
{
	struct switch_stack *sw = (struct switch_stack *) regs - 1;
	unsigned long addr;

	/*
	 * From EAS-2.5: FPDisableFault has higher priority than
	 * Unaligned Fault. Thus, when we get here, we know the partition is
	 * enabled.
	 *
	 * When regnum > 31, the register is still live and we need to force a save
	 * to current->thread.fph to get access to it.  See discussion in setfpreg()
	 * for reasons and other ways of doing this.
	 */
	if (regnum >= IA64_FIRST_ROTATING_FR) {
		ia64_flush_fph(current);
		*fpval = current->thread.fph[fph_index(regs, regnum)];
	} else {
		/*
		 * f0 = 0.0, f1= 1.0. Those registers are constant and are thus
		 * not saved, we must generate their spilled form on the fly
		 */
		switch(regnum) {
		case 0:
			float_spill_f0(fpval);
			break;
		case 1:
			float_spill_f1(fpval);
			break;
		default:
			/*
			 * pt_regs or switch_stack ?
			 */
			addr =  FR_IN_SW(regnum) ? (unsigned long)sw
						 : (unsigned long)regs;

			DPRINT("is_sw=%d tmp_base=%lx offset=0x%x\n",
			       FR_IN_SW(regnum), addr, FR_OFFS(regnum));

			addr  += FR_OFFS(regnum);
			*fpval = *(struct ia64_fpreg *)addr;
		}
	}
}


static void
getreg (unsigned long regnum, unsigned long *val, int *nat, struct pt_regs *regs)
{
	struct switch_stack *sw = (struct switch_stack *) regs - 1;
	unsigned long addr, *unat;

	if (regnum >= IA64_FIRST_STACKED_GR) {
		get_rse_reg(regs, regnum, val, nat);
		return;
	}

	/*
	 * take care of r0 (read-only always evaluate to 0)
	 */
	if (regnum == 0) {
		*val = 0;
		if (nat)
			*nat = 0;
		return;
	}

	/*
	 * Now look at registers in [0-31] range and init correct UNAT
	 */
	if (GR_IN_SW(regnum)) {
		addr = (unsigned long)sw;
		unat = &sw->ar_unat;
	} else {
		addr = (unsigned long)regs;
		unat = &sw->caller_unat;
	}

	DPRINT("addr_base=%lx offset=0x%x\n", addr,  GR_OFFS(regnum));

	addr += GR_OFFS(regnum);

	*val  = *(unsigned long *)addr;

	/*
	 * do it only when requested
	 */
	if (nat)
		*nat  = (*unat >> (addr >> 3 & 0x3f)) & 0x1UL;
}

static void
emulate_load_updates (update_t type, load_store_t ld, struct pt_regs *regs, unsigned long ifa)
{
	/*
	 * IMPORTANT:
	 * Given the way we handle unaligned speculative loads, we should
	 * not get to this point in the code but we keep this sanity check,
	 * just in case.
	 */
	if (ld.x6_op == 1 || ld.x6_op == 3) {
		printk(KERN_ERR "%s: register update on speculative load, error\n", __FUNCTION__);
		if (die_if_kernel("unaligned reference on speculative load with register update\n",
				  regs, 30))
			return;
	}


	/*
	 * at this point, we know that the base register to update is valid i.e.,
	 * it's not r0
	 */
	if (type == UPD_IMMEDIATE) {
		unsigned long imm;

		/*
		 * Load +Imm: ldXZ r1=[r3],imm(9)
		 *
		 *
		 * form imm9: [13:19] contain the first 7 bits
		 */
		imm = ld.x << 7 | ld.imm;

		/*
		 * sign extend (1+8bits) if m set
		 */
		if (ld.m) imm |= SIGN_EXT9;

		/*
		 * ifa == r3 and we know that the NaT bit on r3 was clear so
		 * we can directly use ifa.
		 */
		ifa += imm;

		setreg(ld.r3, ifa, 0, regs);

		DPRINT("ld.x=%d ld.m=%d imm=%ld r3=0x%lx\n", ld.x, ld.m, imm, ifa);

	} else if (ld.m) {
		unsigned long r2;
		int nat_r2;

		/*
		 * Load +Reg Opcode: ldXZ r1=[r3],r2
		 *
		 * Note: that we update r3 even in the case of ldfX.a
		 * (where the load does not happen)
		 *
		 * The way the load algorithm works, we know that r3 does not
		 * have its NaT bit set (would have gotten NaT consumption
		 * before getting the unaligned fault). So we can use ifa
		 * which equals r3 at this point.
		 *
		 * IMPORTANT:
		 * The above statement holds ONLY because we know that we
		 * never reach this code when trying to do a ldX.s.
		 * If we ever make it to here on an ldfX.s then
		 */
		getreg(ld.imm, &r2, &nat_r2, regs);

		ifa += r2;

		/*
		 * propagate Nat r2 -> r3
		 */
		setreg(ld.r3, ifa, nat_r2, regs);

		DPRINT("imm=%d r2=%ld r3=0x%lx nat_r2=%d\n",ld.imm, r2, ifa, nat_r2);
	}
}


static int
emulate_load_int (unsigned long ifa, load_store_t ld, struct pt_regs *regs)
{
	unsigned int len = 1 << ld.x6_sz;
	unsigned long val = 0;

	/*
	 * r0, as target, doesn't need to be checked because Illegal Instruction
	 * faults have higher priority than unaligned faults.
	 *
	 * r0 cannot be found as the base as it would never generate an
	 * unaligned reference.
	 */

	/*
	 * ldX.a we will emulate load and also invalidate the ALAT entry.
	 * See comment below for explanation on how we handle ldX.a
	 */

	if (len != 2 && len != 4 && len != 8) {
		DPRINT("unknown size: x6=%d\n", ld.x6_sz);
		return -1;
	}
	/* this assumes little-endian byte-order: */
	if (copy_from_user(&val, (void __user *) ifa, len))
		return -1;
	setreg(ld.r1, val, 0, regs);

	/*
	 * check for updates on any kind of loads
	 */
	if (ld.op == 0x5 || ld.m)
		emulate_load_updates(ld.op == 0x5 ? UPD_IMMEDIATE: UPD_REG, ld, regs, ifa);

	/*
	 * handling of various loads (based on EAS2.4):
	 *
	 * ldX.acq (ordered load):
	 *	- acquire semantics would have been used, so force fence instead.
	 *
	 * ldX.c.clr (check load and clear):
	 *	- if we get to this handler, it's because the entry was not in the ALAT.
	 *	  Therefore the operation reverts to a normal load
	 *
	 * ldX.c.nc (check load no clear):
	 *	- same as previous one
	 *
	 * ldX.c.clr.acq (ordered check load and clear):
	 *	- same as above for c.clr part. The load needs to have acquire semantics. So
	 *	  we use the fence semantics which is stronger and thus ensures correctness.
	 *
	 * ldX.a (advanced load):
	 *	- suppose ldX.a r1=[r3]. If we get to the unaligned trap it's because the
	 *	  address doesn't match requested size alignment. This means that we would
	 *	  possibly need more than one load to get the result.
	 *
	 *	  The load part can be handled just like a normal load, however the difficult
	 *	  part is to get the right thing into the ALAT. The critical piece of information
	 *	  in the base address of the load & size. To do that, a ld.a must be executed,
	 *	  clearly any address can be pushed into the table by using ld1.a r1=[r3]. Now
	 *	  if we use the same target register, we will be okay for the check.a instruction.
	 *	  If we look at the store, basically a stX [r3]=r1 checks the ALAT  for any entry
	 *	  which would overlap within [r3,r3+X] (the size of the load was store in the
	 *	  ALAT). If such an entry is found the entry is invalidated. But this is not good
	 *	  enough, take the following example:
	 *		r3=3
	 *		ld4.a r1=[r3]
	 *
	 *	  Could be emulated by doing:
	 *		ld1.a r1=[r3],1
	 *		store to temporary;
	 *		ld1.a r1=[r3],1
	 *		store & shift to temporary;
	 *		ld1.a r1=[r3],1
	 *		store & shift to temporary;
	 *		ld1.a r1=[r3]
	 *		store & shift to temporary;
	 *		r1=temporary
	 *
	 *	  So in this case, you would get the right value is r1 but the wrong info in
	 *	  the ALAT.  Notice that you could do it in reverse to finish with address 3
	 *	  but you would still get the size wrong.  To get the size right, one needs to
	 *	  execute exactly the same kind of load. You could do it from a aligned
	 *	  temporary location, but you would get the address wrong.
	 *
	 *	  So no matter what, it is not possible to emulate an advanced load
	 *	  correctly. But is that really critical ?
	 *
	 *	  We will always convert ld.a into a normal load with ALAT invalidated.  This
	 *	  will enable compiler to do optimization where certain code path after ld.a
	 *	  is not required to have ld.c/chk.a, e.g., code path with no intervening stores.
	 *
	 *	  If there is a store after the advanced load, one must either do a ld.c.* or
	 *	  chk.a.* to reuse the value stored in the ALAT. Both can "fail" (meaning no
	 *	  entry found in ALAT), and that's perfectly ok because:
	 *
	 *		- ld.c.*, if the entry is not present a  normal load is executed
	 *		- chk.a.*, if the entry is not present, execution jumps to recovery code
	 *
	 *	  In either case, the load can be potentially retried in another form.
	 *
	 *	  ALAT must be invalidated for the register (so that chk.a or ld.c don't pick
	 *	  up a stale entry later). The register base update MUST also be performed.
	 */

	/*
	 * when the load has the .acq completer then
	 * use ordering fence.
	 */
	if (ld.x6_op == 0x5 || ld.x6_op == 0xa)
		mb();

	/*
	 * invalidate ALAT entry in case of advanced load
	 */
	if (ld.x6_op == 0x2)
		invala_gr(ld.r1);

	return 0;
}

static int
emulate_store_int (unsigned long ifa, load_store_t ld, struct pt_regs *regs)
{
	unsigned long r2;
	unsigned int len = 1 << ld.x6_sz;

	/*
	 * if we get to this handler, Nat bits on both r3 and r2 have already
	 * been checked. so we don't need to do it
	 *
	 * extract the value to be stored
	 */
	getreg(ld.imm, &r2, NULL, regs);

	/*
	 * we rely on the macros in unaligned.h for now i.e.,
	 * we let the compiler figure out how to read memory gracefully.
	 *
	 * We need this switch/case because the way the inline function
	 * works. The code is optimized by the compiler and looks like
	 * a single switch/case.
	 */
	DPRINT("st%d [%lx]=%lx\n", len, ifa, r2);

	if (len != 2 && len != 4 && len != 8) {
		DPRINT("unknown size: x6=%d\n", ld.x6_sz);
		return -1;
	}

	/* this assumes little-endian byte-order: */
	if (copy_to_user((void __user *) ifa, &r2, len))
		return -1;

	/*
	 * stX [r3]=r2,imm(9)
	 *
	 * NOTE:
	 * ld.r3 can never be r0, because r0 would not generate an
	 * unaligned access.
	 */
	if (ld.op == 0x5) {
		unsigned long imm;

		/*
		 * form imm9: [12:6] contain first 7bits
		 */
		imm = ld.x << 7 | ld.r1;
		/*
		 * sign extend (8bits) if m set
		 */
		if (ld.m) imm |= SIGN_EXT9;
		/*
		 * ifa == r3 (NaT is necessarily cleared)
		 */
		ifa += imm;

		DPRINT("imm=%lx r3=%lx\n", imm, ifa);

		setreg(ld.r3, ifa, 0, regs);
	}
	/*
	 * we don't have alat_invalidate_multiple() so we need
	 * to do the complete flush :-<<
	 */
	ia64_invala();

	/*
	 * stX.rel: use fence instead of release
	 */
	if (ld.x6_op == 0xd)
		mb();

	return 0;
}

/*
 * floating point operations sizes in bytes
 */
static const unsigned char float_fsz[4]={
	10, /* extended precision (e) */
	8,  /* integer (8)            */
	4,  /* single precision (s)   */
	8   /* double precision (d)   */
};

static inline void
mem2float_extended (struct ia64_fpreg *init, struct ia64_fpreg *final)
{
	ia64_ldfe(6, init);
	ia64_stop();
	ia64_stf_spill(final, 6);
}

static inline void
mem2float_integer (struct ia64_fpreg *init, struct ia64_fpreg *final)
{
	ia64_ldf8(6, init);
	ia64_stop();
	ia64_stf_spill(final, 6);
}

static inline void
mem2float_single (struct ia64_fpreg *init, struct ia64_fpreg *final)
{
	ia64_ldfs(6, init);
	ia64_stop();
	ia64_stf_spill(final, 6);
}

static inline void
mem2float_double (struct ia64_fpreg *init, struct ia64_fpreg *final)
{
	ia64_ldfd(6, init);
	ia64_stop();
	ia64_stf_spill(final, 6);
}

static inline void
float2mem_extended (struct ia64_fpreg *init, struct ia64_fpreg *final)
{
	ia64_ldf_fill(6, init);
	ia64_stop();
	ia64_stfe(final, 6);
}

static inline void
float2mem_integer (struct ia64_fpreg *init, struct ia64_fpreg *final)
{
	ia64_ldf_fill(6, init);
	ia64_stop();
	ia64_stf8(final, 6);
}

static inline void
float2mem_single (struct ia64_fpreg *init, struct ia64_fpreg *final)
{
	ia64_ldf_fill(6, init);
	ia64_stop();
	ia64_stfs(final, 6);
}

static inline void
float2mem_double (struct ia64_fpreg *init, struct ia64_fpreg *final)
{
	ia64_ldf_fill(6, init);
	ia64_stop();
	ia64_stfd(final, 6);
}

static int
emulate_load_floatpair (unsigned long ifa, load_store_t ld, struct pt_regs *regs)
{
	struct ia64_fpreg fpr_init[2];
	struct ia64_fpreg fpr_final[2];
	unsigned long len = float_fsz[ld.x6_sz];

	/*
	 * fr0 & fr1 don't need to be checked because Illegal Instruction faults have
	 * higher priority than unaligned faults.
	 *
	 * r0 cannot be found as the base as it would never generate an unaligned
	 * reference.
	 */

	/*
	 * make sure we get clean buffers
	 */
	memset(&fpr_init, 0, sizeof(fpr_init));
	memset(&fpr_final, 0, sizeof(fpr_final));

	/*
	 * ldfpX.a: we don't try to emulate anything but we must
	 * invalidate the ALAT entry and execute updates, if any.
	 */
	if (ld.x6_op != 0x2) {
		/*
		 * This assumes little-endian byte-order.  Note that there is no "ldfpe"
		 * instruction:
		 */
		if (copy_from_user(&fpr_init[0], (void __user *) ifa, len)
		    || copy_from_user(&fpr_init[1], (void __user *) (ifa + len), len))
			return -1;

		DPRINT("ld.r1=%d ld.imm=%d x6_sz=%d\n", ld.r1, ld.imm, ld.x6_sz);
		DDUMP("frp_init =", &fpr_init, 2*len);
		/*
		 * XXX fixme
		 * Could optimize inlines by using ldfpX & 2 spills
		 */
		switch( ld.x6_sz ) {
			case 0:
				mem2float_extended(&fpr_init[0], &fpr_final[0]);
				mem2float_extended(&fpr_init[1], &fpr_final[1]);
				break;
			case 1:
				mem2float_integer(&fpr_init[0], &fpr_final[0]);
				mem2float_integer(&fpr_init[1], &fpr_final[1]);
				break;
			case 2:
				mem2float_single(&fpr_init[0], &fpr_final[0]);
				mem2float_single(&fpr_init[1], &fpr_final[1]);
				break;
			case 3:
				mem2float_double(&fpr_init[0], &fpr_final[0]);
				mem2float_double(&fpr_init[1], &fpr_final[1]);
				break;
		}
		DDUMP("fpr_final =", &fpr_final, 2*len);
		/*
		 * XXX fixme
		 *
		 * A possible optimization would be to drop fpr_final and directly
		 * use the storage from the saved context i.e., the actual final
		 * destination (pt_regs, switch_stack or thread structure).
		 */
		setfpreg(ld.r1, &fpr_final[0], regs);
		setfpreg(ld.imm, &fpr_final[1], regs);
	}

	/*
	 * Check for updates: only immediate updates are available for this
	 * instruction.
	 */
	if (ld.m) {
		/*
		 * the immediate is implicit given the ldsz of the operation:
		 * single: 8 (2x4) and for  all others it's 16 (2x8)
		 */
		ifa += len<<1;

		/*
		 * IMPORTANT:
		 * the fact that we force the NaT of r3 to zero is ONLY valid
		 * as long as we don't come here with a ldfpX.s.
		 * For this reason we keep this sanity check
		 */
		if (ld.x6_op == 1 || ld.x6_op == 3)
			printk(KERN_ERR "%s: register update on speculative load pair, error\n",
			       __FUNCTION__);

		setreg(ld.r3, ifa, 0, regs);
	}

	/*
	 * Invalidate ALAT entries, if any, for both registers.
	 */
	if (ld.x6_op == 0x2) {
		invala_fr(ld.r1);
		invala_fr(ld.imm);
	}
	return 0;
}


static int
emulate_load_float (unsigned long ifa, load_store_t ld, struct pt_regs *regs)
{
	struct ia64_fpreg fpr_init;
	struct ia64_fpreg fpr_final;
	unsigned long len = float_fsz[ld.x6_sz];

	/*
	 * fr0 & fr1 don't need to be checked because Illegal Instruction
	 * faults have higher priority than unaligned faults.
	 *
	 * r0 cannot be found as the base as it would never generate an
	 * unaligned reference.
	 */

	/*
	 * make sure we get clean buffers
	 */
	memset(&fpr_init,0, sizeof(fpr_init));
	memset(&fpr_final,0, sizeof(fpr_final));

	/*
	 * ldfX.a we don't try to emulate anything but we must
	 * invalidate the ALAT entry.
	 * See comments in ldX for descriptions on how the various loads are handled.
	 */
	if (ld.x6_op != 0x2) {
		if (copy_from_user(&fpr_init, (void __user *) ifa, len))
			return -1;

		DPRINT("ld.r1=%d x6_sz=%d\n", ld.r1, ld.x6_sz);
		DDUMP("fpr_init =", &fpr_init, len);
		/*
		 * we only do something for x6_op={0,8,9}
		 */
		switch( ld.x6_sz ) {
			case 0:
				mem2float_extended(&fpr_init, &fpr_final);
				break;
			case 1:
				mem2float_integer(&fpr_init, &fpr_final);
				break;
			case 2:
				mem2float_single(&fpr_init, &fpr_final);
				break;
			case 3:
				mem2float_double(&fpr_init, &fpr_final);
				break;
		}
		DDUMP("fpr_final =", &fpr_final, len);
		/*
		 * XXX fixme
		 *
		 * A possible optimization would be to drop fpr_final and directly
		 * use the storage from the saved context i.e., the actual final
		 * destination (pt_regs, switch_stack or thread structure).
		 */
		setfpreg(ld.r1, &fpr_final, regs);
	}

	/*
	 * check for updates on any loads
	 */
	if (ld.op == 0x7 || ld.m)
		emulate_load_updates(ld.op == 0x7 ? UPD_IMMEDIATE: UPD_REG, ld, regs, ifa);

	/*
	 * invalidate ALAT entry in case of advanced floating point loads
	 */
	if (ld.x6_op == 0x2)
		invala_fr(ld.r1);

	return 0;
}


static int
emulate_store_float (unsigned long ifa, load_store_t ld, struct pt_regs *regs)
{
	struct ia64_fpreg fpr_init;
	struct ia64_fpreg fpr_final;
	unsigned long len = float_fsz[ld.x6_sz];

	/*
	 * make sure we get clean buffers
	 */
	memset(&fpr_init,0, sizeof(fpr_init));
	memset(&fpr_final,0, sizeof(fpr_final));

	/*
	 * if we get to this handler, Nat bits on both r3 and r2 have already
	 * been checked. so we don't need to do it
	 *
	 * extract the value to be stored
	 */
	getfpreg(ld.imm, &fpr_init, regs);
	/*
	 * during this step, we extract the spilled registers from the saved
	 * context i.e., we refill. Then we store (no spill) to temporary
	 * aligned location
	 */
	switch( ld.x6_sz ) {
		case 0:
			float2mem_extended(&fpr_init, &fpr_final);
			break;
		case 1:
			float2mem_integer(&fpr_init, &fpr_final);
			break;
		case 2:
			float2mem_single(&fpr_init, &fpr_final);
			break;
		case 3:
			float2mem_double(&fpr_init, &fpr_final);
			break;
	}
	DPRINT("ld.r1=%d x6_sz=%d\n", ld.r1, ld.x6_sz);
	DDUMP("fpr_init =", &fpr_init, len);
	DDUMP("fpr_final =", &fpr_final, len);

	if (copy_to_user((void __user *) ifa, &fpr_final, len))
		return -1;

	/*
	 * stfX [r3]=r2,imm(9)
	 *
	 * NOTE:
	 * ld.r3 can never be r0, because r0 would not generate an
	 * unaligned access.
	 */
	if (ld.op == 0x7) {
		unsigned long imm;

		/*
		 * form imm9: [12:6] contain first 7bits
		 */
		imm = ld.x << 7 | ld.r1;
		/*
		 * sign extend (8bits) if m set
		 */
		if (ld.m)
			imm |= SIGN_EXT9;
		/*
		 * ifa == r3 (NaT is necessarily cleared)
		 */
		ifa += imm;

		DPRINT("imm=%lx r3=%lx\n", imm, ifa);

		setreg(ld.r3, ifa, 0, regs);
	}
	/*
	 * we don't have alat_invalidate_multiple() so we need
	 * to do the complete flush :-<<
	 */
	ia64_invala();

	return 0;
}

/*
 * Make sure we log the unaligned access, so that user/sysadmin can notice it and
 * eventually fix the program.  However, we don't want to do that for every access so we
 * pace it with jiffies.  This isn't really MP-safe, but it doesn't really have to be
 * either...
 */
static int
within_logging_rate_limit (void)
{
	static unsigned long count, last_time;

	if (jiffies - last_time > 5*HZ)
		count = 0;
	if (count < 5) {
		last_time = jiffies;
		count++;
		return 1;
	}
	return 0;

}

void
ia64_handle_unaligned (unsigned long ifa, struct pt_regs *regs)
{
	struct ia64_psr *ipsr = ia64_psr(regs);
	mm_segment_t old_fs = get_fs();
	unsigned long bundle[2];
	unsigned long opcode;
	struct siginfo si;
	const struct exception_table_entry *eh = NULL;
	union {
		unsigned long l;
		load_store_t insn;
	} u;
	int ret = -1;

	if (ia64_psr(regs)->be) {
		/* we don't support big-endian accesses */
		if (die_if_kernel("big-endian unaligned accesses are not supported", regs, 0))
			return;
		goto force_sigbus;
	}

	/*
	 * Treat kernel accesses for which there is an exception handler entry the same as
	 * user-level unaligned accesses.  Otherwise, a clever program could trick this
	 * handler into reading an arbitrary kernel addresses...
	 */
	if (!user_mode(regs))
		eh = search_exception_tables(regs->cr_iip + ia64_psr(regs)->ri);
	if (user_mode(regs) || eh) {
		if ((current->thread.flags & IA64_THREAD_UAC_SIGBUS) != 0)
			goto force_sigbus;

		if (!no_unaligned_warning &&
		    !(current->thread.flags & IA64_THREAD_UAC_NOPRINT) &&
		    within_logging_rate_limit())
		{
			char buf[200];	/* comm[] is at most 16 bytes... */
			size_t len;

			len = sprintf(buf, "%s(%d): unaligned access to 0x%016lx, "
				      "ip=0x%016lx\n\r", current->comm,
				      task_pid_nr(current),
				      ifa, regs->cr_iip + ipsr->ri);
			/*
			 * Don't call tty_write_message() if we're in the kernel; we might
			 * be holding locks...
			 */
			if (user_mode(regs))
				tty_write_message(current->signal->tty, buf);
			buf[len-1] = '\0';	/* drop '\r' */
			/* watch for command names containing %s */
			printk(KERN_WARNING "%s", buf);
		} else {
			if (no_unaligned_warning && !noprint_warning) {
				noprint_warning = 1;
				printk(KERN_WARNING "%s(%d) encountered an "
				       "unaligned exception which required\n"
				       "kernel assistance, which degrades "
				       "the performance of the application.\n"
				       "Unaligned exception warnings have "
				       "been disabled by the system "
				       "administrator\n"
				       "echo 0 > /proc/sys/kernel/ignore-"
				       "unaligned-usertrap to re-enable\n",
				       current->comm, task_pid_nr(current));
			}
		}
	} else {
		if (within_logging_rate_limit())
			printk(KERN_WARNING "kernel unaligned access to 0x%016lx, ip=0x%016lx\n",
			       ifa, regs->cr_iip + ipsr->ri);
		set_fs(KERNEL_DS);
	}

	DPRINT("iip=%lx ifa=%lx isr=%lx (ei=%d, sp=%d)\n",
	       regs->cr_iip, ifa, regs->cr_ipsr, ipsr->ri, ipsr->it);

	if (__copy_from_user(bundle, (void __user *) regs->cr_iip, 16))
		goto failure;

	/*
	 * extract the instruction from the bundle given the slot number
	 */
	switch (ipsr->ri) {
	      case 0: u.l = (bundle[0] >>  5); break;
	      case 1: u.l = (bundle[0] >> 46) | (bundle[1] << 18); break;
	      case 2: u.l = (bundle[1] >> 23); break;
	}
	opcode = (u.l >> IA64_OPCODE_SHIFT) & IA64_OPCODE_MASK;

	DPRINT("opcode=%lx ld.qp=%d ld.r1=%d ld.imm=%d ld.r3=%d ld.x=%d ld.hint=%d "
	       "ld.x6=0x%x ld.m=%d ld.op=%d\n", opcode, u.insn.qp, u.insn.r1, u.insn.imm,
	       u.insn.r3, u.insn.x, u.insn.hint, u.insn.x6_sz, u.insn.m, u.insn.op);

	/*
	 * IMPORTANT:
	 * Notice that the switch statement DOES not cover all possible instructions
	 * that DO generate unaligned references. This is made on purpose because for some
	 * instructions it DOES NOT make sense to try and emulate the access. Sometimes it
	 * is WRONG to try and emulate. Here is a list of instruction we don't emulate i.e.,
	 * the program will get a signal and die:
	 *
	 *	load/store:
	 *		- ldX.spill
	 *		- stX.spill
	 *	Reason: RNATs are based on addresses
	 *		- ld16
	 *		- st16
	 *	Reason: ld16 and st16 are supposed to occur in a single
	 *		memory op
	 *
	 *	synchronization:
	 *		- cmpxchg
	 *		- fetchadd
	 *		- xchg
	 *	Reason: ATOMIC operations cannot be emulated properly using multiple
	 *	        instructions.
	 *
	 *	speculative loads:
	 *		- ldX.sZ
	 *	Reason: side effects, code must be ready to deal with failure so simpler
	 *		to let the load fail.
	 * ---------------------------------------------------------------------------------
	 * XXX fixme
	 *
	 * I would like to get rid of this switch case and do something
	 * more elegant.
	 */
	switch (opcode) {
	      case LDS_OP:
	      case LDSA_OP:
		if (u.insn.x)
			/* oops, really a semaphore op (cmpxchg, etc) */
			goto failure;
		/* no break */
	      case LDS_IMM_OP:
	      case LDSA_IMM_OP:
	      case LDFS_OP:
	      case LDFSA_OP:
	      case LDFS_IMM_OP:
		/*
		 * The instruction will be retried with deferred exceptions turned on, and
		 * we should get Nat bit installed
		 *
		 * IMPORTANT: When PSR_ED is set, the register & immediate update forms
		 * are actually executed even though the operation failed. So we don't
		 * need to take care of this.
		 */
		DPRINT("forcing PSR_ED\n");
		regs->cr_ipsr |= IA64_PSR_ED;
		goto done;

	      case LD_OP:
	      case LDA_OP:
	      case LDBIAS_OP:
	      case LDACQ_OP:
	      case LDCCLR_OP:
	      case LDCNC_OP:
	      case LDCCLRACQ_OP:
		if (u.insn.x)
			/* oops, really a semaphore op (cmpxchg, etc) */
			goto failure;
		/* no break */
	      case LD_IMM_OP:
	      case LDA_IMM_OP:
	      case LDBIAS_IMM_OP:
	      case LDACQ_IMM_OP:
	      case LDCCLR_IMM_OP:
	      case LDCNC_IMM_OP:
	      case LDCCLRACQ_IMM_OP:
		ret = emulate_load_int(ifa, u.insn, regs);
		break;

	      case ST_OP:
	      case STREL_OP:
		if (u.insn.x)
			/* oops, really a semaphore op (cmpxchg, etc) */
			goto failure;
		/* no break */
	      case ST_IMM_OP:
	      case STREL_IMM_OP:
		ret = emulate_store_int(ifa, u.insn, regs);
		break;

	      case LDF_OP:
	      case LDFA_OP:
	      case LDFCCLR_OP:
	      case LDFCNC_OP:
		if (u.insn.x)
			ret = emulate_load_floatpair(ifa, u.insn, regs);
		else
			ret = emulate_load_float(ifa, u.insn, regs);
		break;

	      case LDF_IMM_OP:
	      case LDFA_IMM_OP:
	      case LDFCCLR_IMM_OP:
	      case LDFCNC_IMM_OP:
		ret = emulate_load_float(ifa, u.insn, regs);
		break;

	      case STF_OP:
	      case STF_IMM_OP:
		ret = emulate_store_float(ifa, u.insn, regs);
		break;

	      default:
		goto failure;
	}
	DPRINT("ret=%d\n", ret);
	if (ret)
		goto failure;

	if (ipsr->ri == 2)
		/*
		 * given today's architecture this case is not likely to happen because a
		 * memory access instruction (M) can never be in the last slot of a
		 * bundle. But let's keep it for now.
		 */
		regs->cr_iip += 16;
	ipsr->ri = (ipsr->ri + 1) & 0x3;

	DPRINT("ipsr->ri=%d iip=%lx\n", ipsr->ri, regs->cr_iip);
  done:
	set_fs(old_fs);		/* restore original address limit */
	return;

  failure:
	/* something went wrong... */
	if (!user_mode(regs)) {
		if (eh) {
			ia64_handle_exception(regs, eh);
			goto done;
		}
		if (die_if_kernel("error during unaligned kernel access\n", regs, ret))
			return;
		/* NOT_REACHED */
	}
  force_sigbus:
	si.si_signo = SIGBUS;
	si.si_errno = 0;
	si.si_code = BUS_ADRALN;
	si.si_addr = (void __user *) ifa;
	si.si_flags = 0;
	si.si_isr = 0;
	si.si_imm = 0;
	force_sig_info(SIGBUS, &si, current);
	goto done;
}
