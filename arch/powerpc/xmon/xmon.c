/*
 * Routines providing a simple monitor for use on the PowerMac.
 *
 * Copyright (C) 1996-2005 Paul Mackerras.
 * Copyright (C) 2001 PPC64 Team, IBM Corp
 * Copyrignt (C) 2006 Michael Ellerman, IBM Corp
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/mm.h>
#include <linux/reboot.h>
#include <linux/delay.h>
#include <linux/kallsyms.h>
#include <linux/kmsg_dump.h>
#include <linux/cpumask.h>
#include <linux/export.h>
#include <linux/sysrq.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/bug.h>
#include <linux/nmi.h>

#include <asm/ptrace.h>
#include <asm/string.h>
#include <asm/prom.h>
#include <asm/machdep.h>
#include <asm/xmon.h>
#include <asm/processor.h>
#include <asm/pgtable.h>
#include <asm/mmu.h>
#include <asm/mmu_context.h>
#include <asm/cputable.h>
#include <asm/rtas.h>
#include <asm/sstep.h>
#include <asm/irq_regs.h>
#include <asm/spu.h>
#include <asm/spu_priv1.h>
#include <asm/setjmp.h>
#include <asm/reg.h>
#include <asm/debug.h>
#include <asm/hw_breakpoint.h>

#ifdef CONFIG_PPC64
#include <asm/hvcall.h>
#include <asm/paca.h>
#endif

#include "nonstdio.h"
#include "dis-asm.h"

#ifdef CONFIG_SMP
static cpumask_t cpus_in_xmon = CPU_MASK_NONE;
static unsigned long xmon_taken = 1;
static int xmon_owner;
static int xmon_gate;
#else
#define xmon_owner 0
#endif /* CONFIG_SMP */

static unsigned long in_xmon __read_mostly = 0;

static unsigned long adrs;
static int size = 1;
#define MAX_DUMP (128 * 1024)
static unsigned long ndump = 64;
static unsigned long nidump = 16;
static unsigned long ncsum = 4096;
static int termch;
static char tmpstr[128];

static long bus_error_jmp[JMP_BUF_LEN];
static int catch_memory_errors;
static long *xmon_fault_jmp[NR_CPUS];

/* Breakpoint stuff */
struct bpt {
	unsigned long	address;
	unsigned int	instr[2];
	atomic_t	ref_count;
	int		enabled;
	unsigned long	pad;
};

/* Bits in bpt.enabled */
#define BP_IABR_TE	1		/* IABR translation enabled */
#define BP_IABR		2
#define BP_TRAP		8
#define BP_DABR		0x10

#define NBPTS	256
static struct bpt bpts[NBPTS];
static struct bpt dabr;
static struct bpt *iabr;
static unsigned bpinstr = 0x7fe00008;	/* trap */

#define BP_NUM(bp)	((bp) - bpts + 1)

/* Prototypes */
static int cmds(struct pt_regs *);
static int mread(unsigned long, void *, int);
static int mwrite(unsigned long, void *, int);
static int handle_fault(struct pt_regs *);
static void byterev(unsigned char *, int);
static void memex(void);
static int bsesc(void);
static void dump(void);
static void prdump(unsigned long, long);
static int ppc_inst_dump(unsigned long, long, int);
static void dump_log_buf(void);
static void backtrace(struct pt_regs *);
static void excprint(struct pt_regs *);
static void prregs(struct pt_regs *);
static void memops(int);
static void memlocate(void);
static void memzcan(void);
static void memdiffs(unsigned char *, unsigned char *, unsigned, unsigned);
int skipbl(void);
int scanhex(unsigned long *valp);
static void scannl(void);
static int hexdigit(int);
void getstring(char *, int);
static void flush_input(void);
static int inchar(void);
static void take_input(char *);
static unsigned long read_spr(int);
static void write_spr(int, unsigned long);
static void super_regs(void);
static void remove_bpts(void);
static void insert_bpts(void);
static void remove_cpu_bpts(void);
static void insert_cpu_bpts(void);
static struct bpt *at_breakpoint(unsigned long pc);
static struct bpt *in_breakpoint_table(unsigned long pc, unsigned long *offp);
static int  do_step(struct pt_regs *);
static void bpt_cmds(void);
static void cacheflush(void);
static int  cpu_cmd(void);
static void csum(void);
static void bootcmds(void);
static void proccall(void);
void dump_segments(void);
static void symbol_lookup(void);
static void xmon_show_stack(unsigned long sp, unsigned long lr,
			    unsigned long pc);
static void xmon_print_symbol(unsigned long address, const char *mid,
			      const char *after);
static const char *getvecname(unsigned long vec);

static int do_spu_cmd(void);

#ifdef CONFIG_44x
static void dump_tlb_44x(void);
#endif
#ifdef CONFIG_PPC_BOOK3E
static void dump_tlb_book3e(void);
#endif

static int xmon_no_auto_backtrace;

extern void xmon_enter(void);
extern void xmon_leave(void);

#ifdef CONFIG_PPC64
#define REG		"%.16lx"
#else
#define REG		"%.8lx"
#endif

#ifdef __LITTLE_ENDIAN__
#define GETWORD(v)	(((v)[3] << 24) + ((v)[2] << 16) + ((v)[1] << 8) + (v)[0])
#else
#define GETWORD(v)	(((v)[0] << 24) + ((v)[1] << 16) + ((v)[2] << 8) + (v)[3])
#endif

#define isxdigit(c)	(('0' <= (c) && (c) <= '9') \
			 || ('a' <= (c) && (c) <= 'f') \
			 || ('A' <= (c) && (c) <= 'F'))
#define isalnum(c)	(('0' <= (c) && (c) <= '9') \
			 || ('a' <= (c) && (c) <= 'z') \
			 || ('A' <= (c) && (c) <= 'Z'))
#define isspace(c)	(c == ' ' || c == '\t' || c == 10 || c == 13 || c == 0)

static char *help_string = "\
Commands:\n\
  b	show breakpoints\n\
  bd	set data breakpoint\n\
  bi	set instruction breakpoint\n\
  bc	clear breakpoint\n"
#ifdef CONFIG_SMP
  "\
  c	print cpus stopped in xmon\n\
  c#	try to switch to cpu number h (in hex)\n"
#endif
  "\
  C	checksum\n\
  d	dump bytes\n\
  di	dump instructions\n\
  df	dump float values\n\
  dd	dump double values\n\
  dl    dump the kernel log buffer\n"
#ifdef CONFIG_PPC64
  "\
  dp[#]	dump paca for current cpu, or cpu #\n\
  dpa	dump paca for all possible cpus\n"
#endif
  "\
  dr	dump stream of raw bytes\n\
  e	print exception information\n\
  f	flush cache\n\
  la	lookup symbol+offset of specified address\n\
  ls	lookup address of specified symbol\n\
  m	examine/change memory\n\
  mm	move a block of memory\n\
  ms	set a block of memory\n\
  md	compare two blocks of memory\n\
  ml	locate a block of memory\n\
  mz	zero a block of memory\n\
  mi	show information about memory allocation\n\
  p 	call a procedure\n\
  r	print registers\n\
  s	single step\n"
#ifdef CONFIG_SPU_BASE
"  ss	stop execution on all spus\n\
  sr	restore execution on stopped spus\n\
  sf  #	dump spu fields for spu # (in hex)\n\
  sd  #	dump spu local store for spu # (in hex)\n\
  sdi #	disassemble spu local store for spu # (in hex)\n"
#endif
"  S	print special registers\n\
  t	print backtrace\n\
  x	exit monitor and recover\n\
  X	exit monitor and dont recover\n"
#if defined(CONFIG_PPC64) && !defined(CONFIG_PPC_BOOK3E)
"  u	dump segment table or SLB\n"
#elif defined(CONFIG_PPC_STD_MMU_32)
"  u	dump segment registers\n"
#elif defined(CONFIG_44x) || defined(CONFIG_PPC_BOOK3E)
"  u	dump TLB\n"
#endif
"  ?	help\n"
"  zr	reboot\n\
  zh	halt\n"
;

static struct pt_regs *xmon_regs;

static inline void sync(void)
{
	asm volatile("sync; isync");
}

static inline void store_inst(void *p)
{
	asm volatile ("dcbst 0,%0; sync; icbi 0,%0; isync" : : "r" (p));
}

static inline void cflush(void *p)
{
	asm volatile ("dcbf 0,%0; icbi 0,%0" : : "r" (p));
}

static inline void cinval(void *p)
{
	asm volatile ("dcbi 0,%0; icbi 0,%0" : : "r" (p));
}

/*
 * Disable surveillance (the service processor watchdog function)
 * while we are in xmon.
 * XXX we should re-enable it when we leave. :)
 */
#define SURVEILLANCE_TOKEN	9000

static inline void disable_surveillance(void)
{
#ifdef CONFIG_PPC_PSERIES
	/* Since this can't be a module, args should end up below 4GB. */
	static struct rtas_args args;

	/*
	 * At this point we have got all the cpus we can into
	 * xmon, so there is hopefully no other cpu calling RTAS
	 * at the moment, even though we don't take rtas.lock.
	 * If we did try to take rtas.lock there would be a
	 * real possibility of deadlock.
	 */
	args.token = rtas_token("set-indicator");
	if (args.token == RTAS_UNKNOWN_SERVICE)
		return;
	args.nargs = cpu_to_be32(3);
	args.nret = cpu_to_be32(1);
	args.rets = &args.args[3];
	args.args[0] = cpu_to_be32(SURVEILLANCE_TOKEN);
	args.args[1] = 0;
	args.args[2] = 0;
	enter_rtas(__pa(&args));
#endif /* CONFIG_PPC_PSERIES */
}

#ifdef CONFIG_SMP
static int xmon_speaker;

static void get_output_lock(void)
{
	int me = smp_processor_id() + 0x100;
	int last_speaker = 0, prev;
	long timeout;

	if (xmon_speaker == me)
		return;

	for (;;) {
		last_speaker = cmpxchg(&xmon_speaker, 0, me);
		if (last_speaker == 0)
			return;

		/*
		 * Wait a full second for the lock, we might be on a slow
		 * console, but check every 100us.
		 */
		timeout = 10000;
		while (xmon_speaker == last_speaker) {
			if (--timeout > 0) {
				udelay(100);
				continue;
			}

			/* hostile takeover */
			prev = cmpxchg(&xmon_speaker, last_speaker, me);
			if (prev == last_speaker)
				return;
			break;
		}
	}
}

static void release_output_lock(void)
{
	xmon_speaker = 0;
}

int cpus_are_in_xmon(void)
{
	return !cpumask_empty(&cpus_in_xmon);
}
#endif

static inline int unrecoverable_excp(struct pt_regs *regs)
{
#if defined(CONFIG_4xx) || defined(CONFIG_PPC_BOOK3E)
	/* We have no MSR_RI bit on 4xx or Book3e, so we simply return false */
	return 0;
#else
	return ((regs->msr & MSR_RI) == 0);
#endif
}

static int xmon_core(struct pt_regs *regs, int fromipi)
{
	int cmd = 0;
	struct bpt *bp;
	long recurse_jmp[JMP_BUF_LEN];
	unsigned long offset;
	unsigned long flags;
#ifdef CONFIG_SMP
	int cpu;
	int secondary;
	unsigned long timeout;
#endif

	local_irq_save(flags);
	hard_irq_disable();

	bp = in_breakpoint_table(regs->nip, &offset);
	if (bp != NULL) {
		regs->nip = bp->address + offset;
		atomic_dec(&bp->ref_count);
	}

	remove_cpu_bpts();

#ifdef CONFIG_SMP
	cpu = smp_processor_id();
	if (cpumask_test_cpu(cpu, &cpus_in_xmon)) {
		get_output_lock();
		excprint(regs);
		printf("cpu 0x%x: Exception %lx %s in xmon, "
		       "returning to main loop\n",
		       cpu, regs->trap, getvecname(TRAP(regs)));
		release_output_lock();
		longjmp(xmon_fault_jmp[cpu], 1);
	}

	if (setjmp(recurse_jmp) != 0) {
		if (!in_xmon || !xmon_gate) {
			get_output_lock();
			printf("xmon: WARNING: bad recursive fault "
			       "on cpu 0x%x\n", cpu);
			release_output_lock();
			goto waiting;
		}
		secondary = !(xmon_taken && cpu == xmon_owner);
		goto cmdloop;
	}

	xmon_fault_jmp[cpu] = recurse_jmp;

	bp = NULL;
	if ((regs->msr & (MSR_IR|MSR_PR|MSR_64BIT)) == (MSR_IR|MSR_64BIT))
		bp = at_breakpoint(regs->nip);
	if (bp || unrecoverable_excp(regs))
		fromipi = 0;

	if (!fromipi) {
		get_output_lock();
		excprint(regs);
		if (bp) {
			printf("cpu 0x%x stopped at breakpoint 0x%lx (",
			       cpu, BP_NUM(bp));
			xmon_print_symbol(regs->nip, " ", ")\n");
		}
		if (unrecoverable_excp(regs))
			printf("WARNING: exception is not recoverable, "
			       "can't continue\n");
		release_output_lock();
	}

	cpumask_set_cpu(cpu, &cpus_in_xmon);

 waiting:
	secondary = 1;
	while (secondary && !xmon_gate) {
		if (in_xmon == 0) {
			if (fromipi)
				goto leave;
			secondary = test_and_set_bit(0, &in_xmon);
		}
		barrier();
	}

	if (!secondary && !xmon_gate) {
		/* we are the first cpu to come in */
		/* interrupt other cpu(s) */
		int ncpus = num_online_cpus();

		xmon_owner = cpu;
		mb();
		if (ncpus > 1) {
			smp_send_debugger_break();
			/* wait for other cpus to come in */
			for (timeout = 100000000; timeout != 0; --timeout) {
				if (cpumask_weight(&cpus_in_xmon) >= ncpus)
					break;
				barrier();
			}
		}
		remove_bpts();
		disable_surveillance();
		/* for breakpoint or single step, print the current instr. */
		if (bp || TRAP(regs) == 0xd00)
			ppc_inst_dump(regs->nip, 1, 0);
		printf("enter ? for help\n");
		mb();
		xmon_gate = 1;
		barrier();
	}

 cmdloop:
	while (in_xmon) {
		if (secondary) {
			if (cpu == xmon_owner) {
				if (!test_and_set_bit(0, &xmon_taken)) {
					secondary = 0;
					continue;
				}
				/* missed it */
				while (cpu == xmon_owner)
					barrier();
			}
			barrier();
		} else {
			cmd = cmds(regs);
			if (cmd != 0) {
				/* exiting xmon */
				insert_bpts();
				xmon_gate = 0;
				wmb();
				in_xmon = 0;
				break;
			}
			/* have switched to some other cpu */
			secondary = 1;
		}
	}
 leave:
	cpumask_clear_cpu(cpu, &cpus_in_xmon);
	xmon_fault_jmp[cpu] = NULL;
#else
	/* UP is simple... */
	if (in_xmon) {
		printf("Exception %lx %s in xmon, returning to main loop\n",
		       regs->trap, getvecname(TRAP(regs)));
		longjmp(xmon_fault_jmp[0], 1);
	}
	if (setjmp(recurse_jmp) == 0) {
		xmon_fault_jmp[0] = recurse_jmp;
		in_xmon = 1;

		excprint(regs);
		bp = at_breakpoint(regs->nip);
		if (bp) {
			printf("Stopped at breakpoint %lx (", BP_NUM(bp));
			xmon_print_symbol(regs->nip, " ", ")\n");
		}
		if (unrecoverable_excp(regs))
			printf("WARNING: exception is not recoverable, "
			       "can't continue\n");
		remove_bpts();
		disable_surveillance();
		/* for breakpoint or single step, print the current instr. */
		if (bp || TRAP(regs) == 0xd00)
			ppc_inst_dump(regs->nip, 1, 0);
		printf("enter ? for help\n");
	}

	cmd = cmds(regs);

	insert_bpts();
	in_xmon = 0;
#endif

#ifdef CONFIG_BOOKE
	if (regs->msr & MSR_DE) {
		bp = at_breakpoint(regs->nip);
		if (bp != NULL) {
			regs->nip = (unsigned long) &bp->instr[0];
			atomic_inc(&bp->ref_count);
		}
	}
#else
	if ((regs->msr & (MSR_IR|MSR_PR|MSR_64BIT)) == (MSR_IR|MSR_64BIT)) {
		bp = at_breakpoint(regs->nip);
		if (bp != NULL) {
			int stepped = emulate_step(regs, bp->instr[0]);
			if (stepped == 0) {
				regs->nip = (unsigned long) &bp->instr[0];
				atomic_inc(&bp->ref_count);
			} else if (stepped < 0) {
				printf("Couldn't single-step %s instruction\n",
				    (IS_RFID(bp->instr[0])? "rfid": "mtmsrd"));
			}
		}
	}
#endif
	insert_cpu_bpts();

	touch_nmi_watchdog();
	local_irq_restore(flags);

	return cmd != 'X' && cmd != EOF;
}

int xmon(struct pt_regs *excp)
{
	struct pt_regs regs;

	if (excp == NULL) {
		ppc_save_regs(&regs);
		excp = &regs;
	}

	return xmon_core(excp, 0);
}
EXPORT_SYMBOL(xmon);

irqreturn_t xmon_irq(int irq, void *d)
{
	unsigned long flags;
	local_irq_save(flags);
	printf("Keyboard interrupt\n");
	xmon(get_irq_regs());
	local_irq_restore(flags);
	return IRQ_HANDLED;
}

static int xmon_bpt(struct pt_regs *regs)
{
	struct bpt *bp;
	unsigned long offset;

	if ((regs->msr & (MSR_IR|MSR_PR|MSR_64BIT)) != (MSR_IR|MSR_64BIT))
		return 0;

	/* Are we at the trap at bp->instr[1] for some bp? */
	bp = in_breakpoint_table(regs->nip, &offset);
	if (bp != NULL && offset == 4) {
		regs->nip = bp->address + 4;
		atomic_dec(&bp->ref_count);
		return 1;
	}

	/* Are we at a breakpoint? */
	bp = at_breakpoint(regs->nip);
	if (!bp)
		return 0;

	xmon_core(regs, 0);

	return 1;
}

static int xmon_sstep(struct pt_regs *regs)
{
	if (user_mode(regs))
		return 0;
	xmon_core(regs, 0);
	return 1;
}

static int xmon_break_match(struct pt_regs *regs)
{
	if ((regs->msr & (MSR_IR|MSR_PR|MSR_64BIT)) != (MSR_IR|MSR_64BIT))
		return 0;
	if (dabr.enabled == 0)
		return 0;
	xmon_core(regs, 0);
	return 1;
}

static int xmon_iabr_match(struct pt_regs *regs)
{
	if ((regs->msr & (MSR_IR|MSR_PR|MSR_64BIT)) != (MSR_IR|MSR_64BIT))
		return 0;
	if (iabr == NULL)
		return 0;
	xmon_core(regs, 0);
	return 1;
}

static int xmon_ipi(struct pt_regs *regs)
{
#ifdef CONFIG_SMP
	if (in_xmon && !cpumask_test_cpu(smp_processor_id(), &cpus_in_xmon))
		xmon_core(regs, 1);
#endif
	return 0;
}

static int xmon_fault_handler(struct pt_regs *regs)
{
	struct bpt *bp;
	unsigned long offset;

	if (in_xmon && catch_memory_errors)
		handle_fault(regs);	/* doesn't return */

	if ((regs->msr & (MSR_IR|MSR_PR|MSR_64BIT)) == (MSR_IR|MSR_64BIT)) {
		bp = in_breakpoint_table(regs->nip, &offset);
		if (bp != NULL) {
			regs->nip = bp->address + offset;
			atomic_dec(&bp->ref_count);
		}
	}

	return 0;
}

static struct bpt *at_breakpoint(unsigned long pc)
{
	int i;
	struct bpt *bp;

	bp = bpts;
	for (i = 0; i < NBPTS; ++i, ++bp)
		if (bp->enabled && pc == bp->address)
			return bp;
	return NULL;
}

static struct bpt *in_breakpoint_table(unsigned long nip, unsigned long *offp)
{
	unsigned long off;

	off = nip - (unsigned long) bpts;
	if (off >= sizeof(bpts))
		return NULL;
	off %= sizeof(struct bpt);
	if (off != offsetof(struct bpt, instr[0])
	    && off != offsetof(struct bpt, instr[1]))
		return NULL;
	*offp = off - offsetof(struct bpt, instr[0]);
	return (struct bpt *) (nip - off);
}

static struct bpt *new_breakpoint(unsigned long a)
{
	struct bpt *bp;

	a &= ~3UL;
	bp = at_breakpoint(a);
	if (bp)
		return bp;

	for (bp = bpts; bp < &bpts[NBPTS]; ++bp) {
		if (!bp->enabled && atomic_read(&bp->ref_count) == 0) {
			bp->address = a;
			bp->instr[1] = bpinstr;
			store_inst(&bp->instr[1]);
			return bp;
		}
	}

	printf("Sorry, no free breakpoints.  Please clear one first.\n");
	return NULL;
}

static void insert_bpts(void)
{
	int i;
	struct bpt *bp;

	bp = bpts;
	for (i = 0; i < NBPTS; ++i, ++bp) {
		if ((bp->enabled & (BP_TRAP|BP_IABR)) == 0)
			continue;
		if (mread(bp->address, &bp->instr[0], 4) != 4) {
			printf("Couldn't read instruction at %lx, "
			       "disabling breakpoint there\n", bp->address);
			bp->enabled = 0;
			continue;
		}
		if (IS_MTMSRD(bp->instr[0]) || IS_RFID(bp->instr[0])) {
			printf("Breakpoint at %lx is on an mtmsrd or rfid "
			       "instruction, disabling it\n", bp->address);
			bp->enabled = 0;
			continue;
		}
		store_inst(&bp->instr[0]);
		if (bp->enabled & BP_IABR)
			continue;
		if (mwrite(bp->address, &bpinstr, 4) != 4) {
			printf("Couldn't write instruction at %lx, "
			       "disabling breakpoint there\n", bp->address);
			bp->enabled &= ~BP_TRAP;
			continue;
		}
		store_inst((void *)bp->address);
	}
}

static void insert_cpu_bpts(void)
{
	struct arch_hw_breakpoint brk;

	if (dabr.enabled) {
		brk.address = dabr.address;
		brk.type = (dabr.enabled & HW_BRK_TYPE_DABR) | HW_BRK_TYPE_PRIV_ALL;
		brk.len = 8;
		__set_breakpoint(&brk);
	}
	if (iabr && cpu_has_feature(CPU_FTR_IABR))
		mtspr(SPRN_IABR, iabr->address
			 | (iabr->enabled & (BP_IABR|BP_IABR_TE)));
}

static void remove_bpts(void)
{
	int i;
	struct bpt *bp;
	unsigned instr;

	bp = bpts;
	for (i = 0; i < NBPTS; ++i, ++bp) {
		if ((bp->enabled & (BP_TRAP|BP_IABR)) != BP_TRAP)
			continue;
		if (mread(bp->address, &instr, 4) == 4
		    && instr == bpinstr
		    && mwrite(bp->address, &bp->instr, 4) != 4)
			printf("Couldn't remove breakpoint at %lx\n",
			       bp->address);
		else
			store_inst((void *)bp->address);
	}
}

static void remove_cpu_bpts(void)
{
	hw_breakpoint_disable();
	if (cpu_has_feature(CPU_FTR_IABR))
		mtspr(SPRN_IABR, 0);
}

/* Command interpreting routine */
static char *last_cmd;

static int
cmds(struct pt_regs *excp)
{
	int cmd = 0;

	last_cmd = NULL;
	xmon_regs = excp;

	if (!xmon_no_auto_backtrace) {
		xmon_no_auto_backtrace = 1;
		xmon_show_stack(excp->gpr[1], excp->link, excp->nip);
	}

	for(;;) {
#ifdef CONFIG_SMP
		printf("%x:", smp_processor_id());
#endif /* CONFIG_SMP */
		printf("mon> ");
		flush_input();
		termch = 0;
		cmd = skipbl();
		if( cmd == '\n' ) {
			if (last_cmd == NULL)
				continue;
			take_input(last_cmd);
			last_cmd = NULL;
			cmd = inchar();
		}
		switch (cmd) {
		case 'm':
			cmd = inchar();
			switch (cmd) {
			case 'm':
			case 's':
			case 'd':
				memops(cmd);
				break;
			case 'l':
				memlocate();
				break;
			case 'z':
				memzcan();
				break;
			case 'i':
				show_mem(0);
				break;
			default:
				termch = cmd;
				memex();
			}
			break;
		case 'd':
			dump();
			break;
		case 'l':
			symbol_lookup();
			break;
		case 'r':
			prregs(excp);	/* print regs */
			break;
		case 'e':
			excprint(excp);
			break;
		case 'S':
			super_regs();
			break;
		case 't':
			backtrace(excp);
			break;
		case 'f':
			cacheflush();
			break;
		case 's':
			if (do_spu_cmd() == 0)
				break;
			if (do_step(excp))
				return cmd;
			break;
		case 'x':
		case 'X':
			return cmd;
		case EOF:
			printf(" <no input ...>\n");
			mdelay(2000);
			return cmd;
		case '?':
			xmon_puts(help_string);
			break;
		case 'b':
			bpt_cmds();
			break;
		case 'C':
			csum();
			break;
		case 'c':
			if (cpu_cmd())
				return 0;
			break;
		case 'z':
			bootcmds();
			break;
		case 'p':
			proccall();
			break;
#ifdef CONFIG_PPC_STD_MMU
		case 'u':
			dump_segments();
			break;
#elif defined(CONFIG_4xx)
		case 'u':
			dump_tlb_44x();
			break;
#elif defined(CONFIG_PPC_BOOK3E)
		case 'u':
			dump_tlb_book3e();
			break;
#endif
		default:
			printf("Unrecognized command: ");
			do {
				if (' ' < cmd && cmd <= '~')
					putchar(cmd);
				else
					printf("\\x%x", cmd);
				cmd = inchar();
			} while (cmd != '\n');
			printf(" (type ? for help)\n");
			break;
		}
	}
}

#ifdef CONFIG_BOOKE
static int do_step(struct pt_regs *regs)
{
	regs->msr |= MSR_DE;
	mtspr(SPRN_DBCR0, mfspr(SPRN_DBCR0) | DBCR0_IC | DBCR0_IDM);
	return 1;
}
#else
/*
 * Step a single instruction.
 * Some instructions we emulate, others we execute with MSR_SE set.
 */
static int do_step(struct pt_regs *regs)
{
	unsigned int instr;
	int stepped;

	/* check we are in 64-bit kernel mode, translation enabled */
	if ((regs->msr & (MSR_64BIT|MSR_PR|MSR_IR)) == (MSR_64BIT|MSR_IR)) {
		if (mread(regs->nip, &instr, 4) == 4) {
			stepped = emulate_step(regs, instr);
			if (stepped < 0) {
				printf("Couldn't single-step %s instruction\n",
				       (IS_RFID(instr)? "rfid": "mtmsrd"));
				return 0;
			}
			if (stepped > 0) {
				regs->trap = 0xd00 | (regs->trap & 1);
				printf("stepped to ");
				xmon_print_symbol(regs->nip, " ", "\n");
				ppc_inst_dump(regs->nip, 1, 0);
				return 0;
			}
		}
	}
	regs->msr |= MSR_SE;
	return 1;
}
#endif

static void bootcmds(void)
{
	int cmd;

	cmd = inchar();
	if (cmd == 'r')
		ppc_md.restart(NULL);
	else if (cmd == 'h')
		ppc_md.halt();
	else if (cmd == 'p')
		ppc_md.power_off();
}

static int cpu_cmd(void)
{
#ifdef CONFIG_SMP
	unsigned long cpu, first_cpu, last_cpu;
	int timeout;

	if (!scanhex(&cpu)) {
		/* print cpus waiting or in xmon */
		printf("cpus stopped:");
		last_cpu = first_cpu = NR_CPUS;
		for_each_possible_cpu(cpu) {
			if (cpumask_test_cpu(cpu, &cpus_in_xmon)) {
				if (cpu == last_cpu + 1) {
					last_cpu = cpu;
				} else {
					if (last_cpu != first_cpu)
						printf("-0x%lx", last_cpu);
					last_cpu = first_cpu = cpu;
					printf(" 0x%lx", cpu);
				}
			}
		}
		if (last_cpu != first_cpu)
			printf("-0x%lx", last_cpu);
		printf("\n");
		return 0;
	}
	/* try to switch to cpu specified */
	if (!cpumask_test_cpu(cpu, &cpus_in_xmon)) {
		printf("cpu 0x%x isn't in xmon\n", cpu);
		return 0;
	}
	xmon_taken = 0;
	mb();
	xmon_owner = cpu;
	timeout = 10000000;
	while (!xmon_taken) {
		if (--timeout == 0) {
			if (test_and_set_bit(0, &xmon_taken))
				break;
			/* take control back */
			mb();
			xmon_owner = smp_processor_id();
			printf("cpu 0x%x didn't take control\n", cpu);
			return 0;
		}
		barrier();
	}
	return 1;
#else
	return 0;
#endif /* CONFIG_SMP */
}

static unsigned short fcstab[256] = {
	0x0000, 0x1189, 0x2312, 0x329b, 0x4624, 0x57ad, 0x6536, 0x74bf,
	0x8c48, 0x9dc1, 0xaf5a, 0xbed3, 0xca6c, 0xdbe5, 0xe97e, 0xf8f7,
	0x1081, 0x0108, 0x3393, 0x221a, 0x56a5, 0x472c, 0x75b7, 0x643e,
	0x9cc9, 0x8d40, 0xbfdb, 0xae52, 0xdaed, 0xcb64, 0xf9ff, 0xe876,
	0x2102, 0x308b, 0x0210, 0x1399, 0x6726, 0x76af, 0x4434, 0x55bd,
	0xad4a, 0xbcc3, 0x8e58, 0x9fd1, 0xeb6e, 0xfae7, 0xc87c, 0xd9f5,
	0x3183, 0x200a, 0x1291, 0x0318, 0x77a7, 0x662e, 0x54b5, 0x453c,
	0xbdcb, 0xac42, 0x9ed9, 0x8f50, 0xfbef, 0xea66, 0xd8fd, 0xc974,
	0x4204, 0x538d, 0x6116, 0x709f, 0x0420, 0x15a9, 0x2732, 0x36bb,
	0xce4c, 0xdfc5, 0xed5e, 0xfcd7, 0x8868, 0x99e1, 0xab7a, 0xbaf3,
	0x5285, 0x430c, 0x7197, 0x601e, 0x14a1, 0x0528, 0x37b3, 0x263a,
	0xdecd, 0xcf44, 0xfddf, 0xec56, 0x98e9, 0x8960, 0xbbfb, 0xaa72,
	0x6306, 0x728f, 0x4014, 0x519d, 0x2522, 0x34ab, 0x0630, 0x17b9,
	0xef4e, 0xfec7, 0xcc5c, 0xddd5, 0xa96a, 0xb8e3, 0x8a78, 0x9bf1,
	0x7387, 0x620e, 0x5095, 0x411c, 0x35a3, 0x242a, 0x16b1, 0x0738,
	0xffcf, 0xee46, 0xdcdd, 0xcd54, 0xb9eb, 0xa862, 0x9af9, 0x8b70,
	0x8408, 0x9581, 0xa71a, 0xb693, 0xc22c, 0xd3a5, 0xe13e, 0xf0b7,
	0x0840, 0x19c9, 0x2b52, 0x3adb, 0x4e64, 0x5fed, 0x6d76, 0x7cff,
	0x9489, 0x8500, 0xb79b, 0xa612, 0xd2ad, 0xc324, 0xf1bf, 0xe036,
	0x18c1, 0x0948, 0x3bd3, 0x2a5a, 0x5ee5, 0x4f6c, 0x7df7, 0x6c7e,
	0xa50a, 0xb483, 0x8618, 0x9791, 0xe32e, 0xf2a7, 0xc03c, 0xd1b5,
	0x2942, 0x38cb, 0x0a50, 0x1bd9, 0x6f66, 0x7eef, 0x4c74, 0x5dfd,
	0xb58b, 0xa402, 0x9699, 0x8710, 0xf3af, 0xe226, 0xd0bd, 0xc134,
	0x39c3, 0x284a, 0x1ad1, 0x0b58, 0x7fe7, 0x6e6e, 0x5cf5, 0x4d7c,
	0xc60c, 0xd785, 0xe51e, 0xf497, 0x8028, 0x91a1, 0xa33a, 0xb2b3,
	0x4a44, 0x5bcd, 0x6956, 0x78df, 0x0c60, 0x1de9, 0x2f72, 0x3efb,
	0xd68d, 0xc704, 0xf59f, 0xe416, 0x90a9, 0x8120, 0xb3bb, 0xa232,
	0x5ac5, 0x4b4c, 0x79d7, 0x685e, 0x1ce1, 0x0d68, 0x3ff3, 0x2e7a,
	0xe70e, 0xf687, 0xc41c, 0xd595, 0xa12a, 0xb0a3, 0x8238, 0x93b1,
	0x6b46, 0x7acf, 0x4854, 0x59dd, 0x2d62, 0x3ceb, 0x0e70, 0x1ff9,
	0xf78f, 0xe606, 0xd49d, 0xc514, 0xb1ab, 0xa022, 0x92b9, 0x8330,
	0x7bc7, 0x6a4e, 0x58d5, 0x495c, 0x3de3, 0x2c6a, 0x1ef1, 0x0f78
};

#define FCS(fcs, c)	(((fcs) >> 8) ^ fcstab[((fcs) ^ (c)) & 0xff])

static void
csum(void)
{
	unsigned int i;
	unsigned short fcs;
	unsigned char v;

	if (!scanhex(&adrs))
		return;
	if (!scanhex(&ncsum))
		return;
	fcs = 0xffff;
	for (i = 0; i < ncsum; ++i) {
		if (mread(adrs+i, &v, 1) == 0) {
			printf("csum stopped at "REG"\n", adrs+i);
			break;
		}
		fcs = FCS(fcs, v);
	}
	printf("%x\n", fcs);
}

/*
 * Check if this is a suitable place to put a breakpoint.
 */
static long check_bp_loc(unsigned long addr)
{
	unsigned int instr;

	addr &= ~3;
	if (!is_kernel_addr(addr)) {
		printf("Breakpoints may only be placed at kernel addresses\n");
		return 0;
	}
	if (!mread(addr, &instr, sizeof(instr))) {
		printf("Can't read instruction at address %lx\n", addr);
		return 0;
	}
	if (IS_MTMSRD(instr) || IS_RFID(instr)) {
		printf("Breakpoints may not be placed on mtmsrd or rfid "
		       "instructions\n");
		return 0;
	}
	return 1;
}

static char *breakpoint_help_string =
    "Breakpoint command usage:\n"
    "b                show breakpoints\n"
    "b <addr> [cnt]   set breakpoint at given instr addr\n"
    "bc               clear all breakpoints\n"
    "bc <n/addr>      clear breakpoint number n or at addr\n"
    "bi <addr> [cnt]  set hardware instr breakpoint (POWER3/RS64 only)\n"
    "bd <addr> [cnt]  set hardware data breakpoint\n"
    "";

static void
bpt_cmds(void)
{
	int cmd;
	unsigned long a;
	int mode, i;
	struct bpt *bp;
	const char badaddr[] = "Only kernel addresses are permitted "
		"for breakpoints\n";

	cmd = inchar();
	switch (cmd) {
#ifndef CONFIG_8xx
	case 'd':	/* bd - hardware data breakpoint */
		mode = 7;
		cmd = inchar();
		if (cmd == 'r')
			mode = 5;
		else if (cmd == 'w')
			mode = 6;
		else
			termch = cmd;
		dabr.address = 0;
		dabr.enabled = 0;
		if (scanhex(&dabr.address)) {
			if (!is_kernel_addr(dabr.address)) {
				printf(badaddr);
				break;
			}
			dabr.address &= ~HW_BRK_TYPE_DABR;
			dabr.enabled = mode | BP_DABR;
		}
		break;

	case 'i':	/* bi - hardware instr breakpoint */
		if (!cpu_has_feature(CPU_FTR_IABR)) {
			printf("Hardware instruction breakpoint "
			       "not supported on this cpu\n");
			break;
		}
		if (iabr) {
			iabr->enabled &= ~(BP_IABR | BP_IABR_TE);
			iabr = NULL;
		}
		if (!scanhex(&a))
			break;
		if (!check_bp_loc(a))
			break;
		bp = new_breakpoint(a);
		if (bp != NULL) {
			bp->enabled |= BP_IABR | BP_IABR_TE;
			iabr = bp;
		}
		break;
#endif

	case 'c':
		if (!scanhex(&a)) {
			/* clear all breakpoints */
			for (i = 0; i < NBPTS; ++i)
				bpts[i].enabled = 0;
			iabr = NULL;
			dabr.enabled = 0;
			printf("All breakpoints cleared\n");
			break;
		}

		if (a <= NBPTS && a >= 1) {
			/* assume a breakpoint number */
			bp = &bpts[a-1];	/* bp nums are 1 based */
		} else {
			/* assume a breakpoint address */
			bp = at_breakpoint(a);
			if (bp == NULL) {
				printf("No breakpoint at %lx\n", a);
				break;
			}
		}

		printf("Cleared breakpoint %lx (", BP_NUM(bp));
		xmon_print_symbol(bp->address, " ", ")\n");
		bp->enabled = 0;
		break;

	default:
		termch = cmd;
		cmd = skipbl();
		if (cmd == '?') {
			printf(breakpoint_help_string);
			break;
		}
		termch = cmd;
		if (!scanhex(&a)) {
			/* print all breakpoints */
			printf("   type            address\n");
			if (dabr.enabled) {
				printf("   data   "REG"  [", dabr.address);
				if (dabr.enabled & 1)
					printf("r");
				if (dabr.enabled & 2)
					printf("w");
				printf("]\n");
			}
			for (bp = bpts; bp < &bpts[NBPTS]; ++bp) {
				if (!bp->enabled)
					continue;
				printf("%2x %s   ", BP_NUM(bp),
				    (bp->enabled & BP_IABR)? "inst": "trap");
				xmon_print_symbol(bp->address, "  ", "\n");
			}
			break;
		}

		if (!check_bp_loc(a))
			break;
		bp = new_breakpoint(a);
		if (bp != NULL)
			bp->enabled |= BP_TRAP;
		break;
	}
}

/* Very cheap human name for vector lookup. */
static
const char *getvecname(unsigned long vec)
{
	char *ret;

	switch (vec) {
	case 0x100:	ret = "(System Reset)"; break;
	case 0x200:	ret = "(Machine Check)"; break;
	case 0x300:	ret = "(Data Access)"; break;
	case 0x380:	ret = "(Data SLB Access)"; break;
	case 0x400:	ret = "(Instruction Access)"; break;
	case 0x480:	ret = "(Instruction SLB Access)"; break;
	case 0x500:	ret = "(Hardware Interrupt)"; break;
	case 0x600:	ret = "(Alignment)"; break;
	case 0x700:	ret = "(Program Check)"; break;
	case 0x800:	ret = "(FPU Unavailable)"; break;
	case 0x900:	ret = "(Decrementer)"; break;
	case 0x980:	ret = "(Hypervisor Decrementer)"; break;
	case 0xa00:	ret = "(Doorbell)"; break;
	case 0xc00:	ret = "(System Call)"; break;
	case 0xd00:	ret = "(Single Step)"; break;
	case 0xe40:	ret = "(Emulation Assist)"; break;
	case 0xe60:	ret = "(HMI)"; break;
	case 0xe80:	ret = "(Hypervisor Doorbell)"; break;
	case 0xf00:	ret = "(Performance Monitor)"; break;
	case 0xf20:	ret = "(Altivec Unavailable)"; break;
	case 0x1300:	ret = "(Instruction Breakpoint)"; break;
	case 0x1500:	ret = "(Denormalisation)"; break;
	case 0x1700:	ret = "(Altivec Assist)"; break;
	default: ret = "";
	}
	return ret;
}

static void get_function_bounds(unsigned long pc, unsigned long *startp,
				unsigned long *endp)
{
	unsigned long size, offset;
	const char *name;

	*startp = *endp = 0;
	if (pc == 0)
		return;
	if (setjmp(bus_error_jmp) == 0) {
		catch_memory_errors = 1;
		sync();
		name = kallsyms_lookup(pc, &size, &offset, NULL, tmpstr);
		if (name != NULL) {
			*startp = pc - offset;
			*endp = pc - offset + size;
		}
		sync();
	}
	catch_memory_errors = 0;
}

#define LRSAVE_OFFSET		(STACK_FRAME_LR_SAVE * sizeof(unsigned long))
#define MARKER_OFFSET		(STACK_FRAME_MARKER * sizeof(unsigned long))

static void xmon_show_stack(unsigned long sp, unsigned long lr,
			    unsigned long pc)
{
	int max_to_print = 64;
	unsigned long ip;
	unsigned long newsp;
	unsigned long marker;
	struct pt_regs regs;

	while (max_to_print--) {
		if (sp < PAGE_OFFSET) {
			if (sp != 0)
				printf("SP (%lx) is in userspace\n", sp);
			break;
		}

		if (!mread(sp + LRSAVE_OFFSET, &ip, sizeof(unsigned long))
		    || !mread(sp, &newsp, sizeof(unsigned long))) {
			printf("Couldn't read stack frame at %lx\n", sp);
			break;
		}

		/*
		 * For the first stack frame, try to work out if
		 * LR and/or the saved LR value in the bottommost
		 * stack frame are valid.
		 */
		if ((pc | lr) != 0) {
			unsigned long fnstart, fnend;
			unsigned long nextip;
			int printip = 1;

			get_function_bounds(pc, &fnstart, &fnend);
			nextip = 0;
			if (newsp > sp)
				mread(newsp + LRSAVE_OFFSET, &nextip,
				      sizeof(unsigned long));
			if (lr == ip) {
				if (lr < PAGE_OFFSET
				    || (fnstart <= lr && lr < fnend))
					printip = 0;
			} else if (lr == nextip) {
				printip = 0;
			} else if (lr >= PAGE_OFFSET
				   && !(fnstart <= lr && lr < fnend)) {
				printf("[link register   ] ");
				xmon_print_symbol(lr, " ", "\n");
			}
			if (printip) {
				printf("["REG"] ", sp);
				xmon_print_symbol(ip, " ", " (unreliable)\n");
			}
			pc = lr = 0;

		} else {
			printf("["REG"] ", sp);
			xmon_print_symbol(ip, " ", "\n");
		}

		/* Look for "regshere" marker to see if this is
		   an exception frame. */
		if (mread(sp + MARKER_OFFSET, &marker, sizeof(unsigned long))
		    && marker == STACK_FRAME_REGS_MARKER) {
			if (mread(sp + STACK_FRAME_OVERHEAD, &regs, sizeof(regs))
			    != sizeof(regs)) {
				printf("Couldn't read registers at %lx\n",
				       sp + STACK_FRAME_OVERHEAD);
				break;
			}
			printf("--- Exception: %lx %s at ", regs.trap,
			       getvecname(TRAP(&regs)));
			pc = regs.nip;
			lr = regs.link;
			xmon_print_symbol(pc, " ", "\n");
		}

		if (newsp == 0)
			break;

		sp = newsp;
	}
}

static void backtrace(struct pt_regs *excp)
{
	unsigned long sp;

	if (scanhex(&sp))
		xmon_show_stack(sp, 0, 0);
	else
		xmon_show_stack(excp->gpr[1], excp->link, excp->nip);
	scannl();
}

static void print_bug_trap(struct pt_regs *regs)
{
#ifdef CONFIG_BUG
	const struct bug_entry *bug;
	unsigned long addr;

	if (regs->msr & MSR_PR)
		return;		/* not in kernel */
	addr = regs->nip;	/* address of trap instruction */
	if (addr < PAGE_OFFSET)
		return;
	bug = find_bug(regs->nip);
	if (bug == NULL)
		return;
	if (is_warning_bug(bug))
		return;

#ifdef CONFIG_DEBUG_BUGVERBOSE
	printf("kernel BUG at %s:%u!\n",
	       bug->file, bug->line);
#else
	printf("kernel BUG at %p!\n", (void *)bug->bug_addr);
#endif
#endif /* CONFIG_BUG */
}

static void excprint(struct pt_regs *fp)
{
	unsigned long trap;

#ifdef CONFIG_SMP
	printf("cpu 0x%x: ", smp_processor_id());
#endif /* CONFIG_SMP */

	trap = TRAP(fp);
	printf("Vector: %lx %s at [%lx]\n", fp->trap, getvecname(trap), fp);
	printf("    pc: ");
	xmon_print_symbol(fp->nip, ": ", "\n");

	printf("    lr: ", fp->link);
	xmon_print_symbol(fp->link, ": ", "\n");

	printf("    sp: %lx\n", fp->gpr[1]);
	printf("   msr: %lx\n", fp->msr);

	if (trap == 0x300 || trap == 0x380 || trap == 0x600 || trap == 0x200) {
		printf("   dar: %lx\n", fp->dar);
		if (trap != 0x380)
			printf(" dsisr: %lx\n", fp->dsisr);
	}

	printf("  current = 0x%lx\n", current);
#ifdef CONFIG_PPC64
	printf("  paca    = 0x%lx\t softe: %d\t irq_happened: 0x%02x\n",
	       local_paca, local_paca->soft_enabled, local_paca->irq_happened);
#endif
	if (current) {
		printf("    pid   = %ld, comm = %s\n",
		       current->pid, current->comm);
	}

	if (trap == 0x700)
		print_bug_trap(fp);
}

static void prregs(struct pt_regs *fp)
{
	int n, trap;
	unsigned long base;
	struct pt_regs regs;

	if (scanhex(&base)) {
		if (setjmp(bus_error_jmp) == 0) {
			catch_memory_errors = 1;
			sync();
			regs = *(struct pt_regs *)base;
			sync();
			__delay(200);
		} else {
			catch_memory_errors = 0;
			printf("*** Error reading registers from "REG"\n",
			       base);
			return;
		}
		catch_memory_errors = 0;
		fp = &regs;
	}

#ifdef CONFIG_PPC64
	if (FULL_REGS(fp)) {
		for (n = 0; n < 16; ++n)
			printf("R%.2ld = "REG"   R%.2ld = "REG"\n",
			       n, fp->gpr[n], n+16, fp->gpr[n+16]);
	} else {
		for (n = 0; n < 7; ++n)
			printf("R%.2ld = "REG"   R%.2ld = "REG"\n",
			       n, fp->gpr[n], n+7, fp->gpr[n+7]);
	}
#else
	for (n = 0; n < 32; ++n) {
		printf("R%.2d = %.8x%s", n, fp->gpr[n],
		       (n & 3) == 3? "\n": "   ");
		if (n == 12 && !FULL_REGS(fp)) {
			printf("\n");
			break;
		}
	}
#endif
	printf("pc  = ");
	xmon_print_symbol(fp->nip, " ", "\n");
	if (TRAP(fp) != 0xc00 && cpu_has_feature(CPU_FTR_CFAR)) {
		printf("cfar= ");
		xmon_print_symbol(fp->orig_gpr3, " ", "\n");
	}
	printf("lr  = ");
	xmon_print_symbol(fp->link, " ", "\n");
	printf("msr = "REG"   cr  = %.8lx\n", fp->msr, fp->ccr);
	printf("ctr = "REG"   xer = "REG"   trap = %4lx\n",
	       fp->ctr, fp->xer, fp->trap);
	trap = TRAP(fp);
	if (trap == 0x300 || trap == 0x380 || trap == 0x600)
		printf("dar = "REG"   dsisr = %.8lx\n", fp->dar, fp->dsisr);
}

static void cacheflush(void)
{
	int cmd;
	unsigned long nflush;

	cmd = inchar();
	if (cmd != 'i')
		termch = cmd;
	scanhex((void *)&adrs);
	if (termch != '\n')
		termch = 0;
	nflush = 1;
	scanhex(&nflush);
	nflush = (nflush + L1_CACHE_BYTES - 1) / L1_CACHE_BYTES;
	if (setjmp(bus_error_jmp) == 0) {
		catch_memory_errors = 1;
		sync();

		if (cmd != 'i') {
			for (; nflush > 0; --nflush, adrs += L1_CACHE_BYTES)
				cflush((void *) adrs);
		} else {
			for (; nflush > 0; --nflush, adrs += L1_CACHE_BYTES)
				cinval((void *) adrs);
		}
		sync();
		/* wait a little while to see if we get a machine check */
		__delay(200);
	}
	catch_memory_errors = 0;
}

static unsigned long
read_spr(int n)
{
	unsigned int instrs[2];
	unsigned long (*code)(void);
	unsigned long ret = -1UL;
#ifdef CONFIG_PPC64
	unsigned long opd[3];

	opd[0] = (unsigned long)instrs;
	opd[1] = 0;
	opd[2] = 0;
	code = (unsigned long (*)(void)) opd;
#else
	code = (unsigned long (*)(void)) instrs;
#endif

	/* mfspr r3,n; blr */
	instrs[0] = 0x7c6002a6 + ((n & 0x1F) << 16) + ((n & 0x3e0) << 6);
	instrs[1] = 0x4e800020;
	store_inst(instrs);
	store_inst(instrs+1);

	if (setjmp(bus_error_jmp) == 0) {
		catch_memory_errors = 1;
		sync();

		ret = code();

		sync();
		/* wait a little while to see if we get a machine check */
		__delay(200);
		n = size;
	}

	return ret;
}

static void
write_spr(int n, unsigned long val)
{
	unsigned int instrs[2];
	unsigned long (*code)(unsigned long);
#ifdef CONFIG_PPC64
	unsigned long opd[3];

	opd[0] = (unsigned long)instrs;
	opd[1] = 0;
	opd[2] = 0;
	code = (unsigned long (*)(unsigned long)) opd;
#else
	code = (unsigned long (*)(unsigned long)) instrs;
#endif

	instrs[0] = 0x7c6003a6 + ((n & 0x1F) << 16) + ((n & 0x3e0) << 6);
	instrs[1] = 0x4e800020;
	store_inst(instrs);
	store_inst(instrs+1);

	if (setjmp(bus_error_jmp) == 0) {
		catch_memory_errors = 1;
		sync();

		code(val);

		sync();
		/* wait a little while to see if we get a machine check */
		__delay(200);
		n = size;
	}
}

static unsigned long regno;
extern char exc_prolog;
extern char dec_exc;

static void super_regs(void)
{
	int cmd;
	unsigned long val;

	cmd = skipbl();
	if (cmd == '\n') {
		unsigned long sp, toc;
		asm("mr %0,1" : "=r" (sp) :);
		asm("mr %0,2" : "=r" (toc) :);

		printf("msr  = "REG"  sprg0= "REG"\n",
		       mfmsr(), mfspr(SPRN_SPRG0));
		printf("pvr  = "REG"  sprg1= "REG"\n",
		       mfspr(SPRN_PVR), mfspr(SPRN_SPRG1));
		printf("dec  = "REG"  sprg2= "REG"\n",
		       mfspr(SPRN_DEC), mfspr(SPRN_SPRG2));
		printf("sp   = "REG"  sprg3= "REG"\n", sp, mfspr(SPRN_SPRG3));
		printf("toc  = "REG"  dar  = "REG"\n", toc, mfspr(SPRN_DAR));

		return;
	}

	scanhex(&regno);
	switch (cmd) {
	case 'w':
		val = read_spr(regno);
		scanhex(&val);
		write_spr(regno, val);
		/* fall through */
	case 'r':
		printf("spr %lx = %lx\n", regno, read_spr(regno));
		break;
	}
	scannl();
}

/*
 * Stuff for reading and writing memory safely
 */
static int
mread(unsigned long adrs, void *buf, int size)
{
	volatile int n;
	char *p, *q;

	n = 0;
	if (setjmp(bus_error_jmp) == 0) {
		catch_memory_errors = 1;
		sync();
		p = (char *)adrs;
		q = (char *)buf;
		switch (size) {
		case 2:
			*(u16 *)q = *(u16 *)p;
			break;
		case 4:
			*(u32 *)q = *(u32 *)p;
			break;
		case 8:
			*(u64 *)q = *(u64 *)p;
			break;
		default:
			for( ; n < size; ++n) {
				*q++ = *p++;
				sync();
			}
		}
		sync();
		/* wait a little while to see if we get a machine check */
		__delay(200);
		n = size;
	}
	catch_memory_errors = 0;
	return n;
}

static int
mwrite(unsigned long adrs, void *buf, int size)
{
	volatile int n;
	char *p, *q;

	n = 0;
	if (setjmp(bus_error_jmp) == 0) {
		catch_memory_errors = 1;
		sync();
		p = (char *) adrs;
		q = (char *) buf;
		switch (size) {
		case 2:
			*(u16 *)p = *(u16 *)q;
			break;
		case 4:
			*(u32 *)p = *(u32 *)q;
			break;
		case 8:
			*(u64 *)p = *(u64 *)q;
			break;
		default:
			for ( ; n < size; ++n) {
				*p++ = *q++;
				sync();
			}
		}
		sync();
		/* wait a little while to see if we get a machine check */
		__delay(200);
		n = size;
	} else {
		printf("*** Error writing address "REG"\n", adrs + n);
	}
	catch_memory_errors = 0;
	return n;
}

static int fault_type;
static int fault_except;
static char *fault_chars[] = { "--", "**", "##" };

static int handle_fault(struct pt_regs *regs)
{
	fault_except = TRAP(regs);
	switch (TRAP(regs)) {
	case 0x200:
		fault_type = 0;
		break;
	case 0x300:
	case 0x380:
		fault_type = 1;
		break;
	default:
		fault_type = 2;
	}

	longjmp(bus_error_jmp, 1);

	return 0;
}

#define SWAP(a, b, t)	((t) = (a), (a) = (b), (b) = (t))

static void
byterev(unsigned char *val, int size)
{
	int t;
	
	switch (size) {
	case 2:
		SWAP(val[0], val[1], t);
		break;
	case 4:
		SWAP(val[0], val[3], t);
		SWAP(val[1], val[2], t);
		break;
	case 8: /* is there really any use for this? */
		SWAP(val[0], val[7], t);
		SWAP(val[1], val[6], t);
		SWAP(val[2], val[5], t);
		SWAP(val[3], val[4], t);
		break;
	}
}

static int brev;
static int mnoread;

static char *memex_help_string =
    "Memory examine command usage:\n"
    "m [addr] [flags] examine/change memory\n"
    "  addr is optional.  will start where left off.\n"
    "  flags may include chars from this set:\n"
    "    b   modify by bytes (default)\n"
    "    w   modify by words (2 byte)\n"
    "    l   modify by longs (4 byte)\n"
    "    d   modify by doubleword (8 byte)\n"
    "    r   toggle reverse byte order mode\n"
    "    n   do not read memory (for i/o spaces)\n"
    "    .   ok to read (default)\n"
    "NOTE: flags are saved as defaults\n"
    "";

static char *memex_subcmd_help_string =
    "Memory examine subcommands:\n"
    "  hexval   write this val to current location\n"
    "  'string' write chars from string to this location\n"
    "  '        increment address\n"
    "  ^        decrement address\n"
    "  /        increment addr by 0x10.  //=0x100, ///=0x1000, etc\n"
    "  \\        decrement addr by 0x10.  \\\\=0x100, \\\\\\=0x1000, etc\n"
    "  `        clear no-read flag\n"
    "  ;        stay at this addr\n"
    "  v        change to byte mode\n"
    "  w        change to word (2 byte) mode\n"
    "  l        change to long (4 byte) mode\n"
    "  u        change to doubleword (8 byte) mode\n"
    "  m addr   change current addr\n"
    "  n        toggle no-read flag\n"
    "  r        toggle byte reverse flag\n"
    "  < count  back up count bytes\n"
    "  > count  skip forward count bytes\n"
    "  x        exit this mode\n"
    "";

static void
memex(void)
{
	int cmd, inc, i, nslash;
	unsigned long n;
	unsigned char val[16];

	scanhex((void *)&adrs);
	cmd = skipbl();
	if (cmd == '?') {
		printf(memex_help_string);
		return;
	} else {
		termch = cmd;
	}
	last_cmd = "m\n";
	while ((cmd = skipbl()) != '\n') {
		switch( cmd ){
		case 'b':	size = 1;	break;
		case 'w':	size = 2;	break;
		case 'l':	size = 4;	break;
		case 'd':	size = 8;	break;
		case 'r': 	brev = !brev;	break;
		case 'n':	mnoread = 1;	break;
		case '.':	mnoread = 0;	break;
		}
	}
	if( size <= 0 )
		size = 1;
	else if( size > 8 )
		size = 8;
	for(;;){
		if (!mnoread)
			n = mread(adrs, val, size);
		printf(REG"%c", adrs, brev? 'r': ' ');
		if (!mnoread) {
			if (brev)
				byterev(val, size);
			putchar(' ');
			for (i = 0; i < n; ++i)
				printf("%.2x", val[i]);
			for (; i < size; ++i)
				printf("%s", fault_chars[fault_type]);
		}
		putchar(' ');
		inc = size;
		nslash = 0;
		for(;;){
			if( scanhex(&n) ){
				for (i = 0; i < size; ++i)
					val[i] = n >> (i * 8);
				if (!brev)
					byterev(val, size);
				mwrite(adrs, val, size);
				inc = size;
			}
			cmd = skipbl();
			if (cmd == '\n')
				break;
			inc = 0;
			switch (cmd) {
			case '\'':
				for(;;){
					n = inchar();
					if( n == '\\' )
						n = bsesc();
					else if( n == '\'' )
						break;
					for (i = 0; i < size; ++i)
						val[i] = n >> (i * 8);
					if (!brev)
						byterev(val, size);
					mwrite(adrs, val, size);
					adrs += size;
				}
				adrs -= size;
				inc = size;
				break;
			case ',':
				adrs += size;
				break;
			case '.':
				mnoread = 0;
				break;
			case ';':
				break;
			case 'x':
			case EOF:
				scannl();
				return;
			case 'b':
			case 'v':
				size = 1;
				break;
			case 'w':
				size = 2;
				break;
			case 'l':
				size = 4;
				break;
			case 'u':
				size = 8;
				break;
			case '^':
				adrs -= size;
				break;
				break;
			case '/':
				if (nslash > 0)
					adrs -= 1 << nslash;
				else
					nslash = 0;
				nslash += 4;
				adrs += 1 << nslash;
				break;
			case '\\':
				if (nslash < 0)
					adrs += 1 << -nslash;
				else
					nslash = 0;
				nslash -= 4;
				adrs -= 1 << -nslash;
				break;
			case 'm':
				scanhex((void *)&adrs);
				break;
			case 'n':
				mnoread = 1;
				break;
			case 'r':
				brev = !brev;
				break;
			case '<':
				n = size;
				scanhex(&n);
				adrs -= n;
				break;
			case '>':
				n = size;
				scanhex(&n);
				adrs += n;
				break;
			case '?':
				printf(memex_subcmd_help_string);
				break;
			}
		}
		adrs += inc;
	}
}

static int
bsesc(void)
{
	int c;

	c = inchar();
	switch( c ){
	case 'n':	c = '\n';	break;
	case 'r':	c = '\r';	break;
	case 'b':	c = '\b';	break;
	case 't':	c = '\t';	break;
	}
	return c;
}

static void xmon_rawdump (unsigned long adrs, long ndump)
{
	long n, m, r, nr;
	unsigned char temp[16];

	for (n = ndump; n > 0;) {
		r = n < 16? n: 16;
		nr = mread(adrs, temp, r);
		adrs += nr;
		for (m = 0; m < r; ++m) {
			if (m < nr)
				printf("%.2x", temp[m]);
			else
				printf("%s", fault_chars[fault_type]);
		}
		n -= r;
		if (nr < r)
			break;
	}
	printf("\n");
}

#ifdef CONFIG_PPC64
static void dump_one_paca(int cpu)
{
	struct paca_struct *p;

	if (setjmp(bus_error_jmp) != 0) {
		printf("*** Error dumping paca for cpu 0x%x!\n", cpu);
		return;
	}

	catch_memory_errors = 1;
	sync();

	p = &paca[cpu];

	printf("paca for cpu 0x%x @ %p:\n", cpu, p);

	printf(" %-*s = %s\n", 16, "possible", cpu_possible(cpu) ? "yes" : "no");
	printf(" %-*s = %s\n", 16, "present", cpu_present(cpu) ? "yes" : "no");
	printf(" %-*s = %s\n", 16, "online", cpu_online(cpu) ? "yes" : "no");

#define DUMP(paca, name, format) \
	printf(" %-*s = %#-*"format"\t(0x%lx)\n", 16, #name, 18, paca->name, \
		offsetof(struct paca_struct, name));

	DUMP(p, lock_token, "x");
	DUMP(p, paca_index, "x");
	DUMP(p, kernel_toc, "lx");
	DUMP(p, kernelbase, "lx");
	DUMP(p, kernel_msr, "lx");
	DUMP(p, emergency_sp, "p");
#ifdef CONFIG_PPC_BOOK3S_64
	DUMP(p, mc_emergency_sp, "p");
	DUMP(p, in_mce, "x");
#endif
	DUMP(p, data_offset, "lx");
	DUMP(p, hw_cpu_id, "x");
	DUMP(p, cpu_start, "x");
	DUMP(p, kexec_state, "x");
	DUMP(p, __current, "p");
	DUMP(p, kstack, "lx");
	DUMP(p, stab_rr, "lx");
	DUMP(p, saved_r1, "lx");
	DUMP(p, trap_save, "x");
	DUMP(p, soft_enabled, "x");
	DUMP(p, irq_happened, "x");
	DUMP(p, io_sync, "x");
	DUMP(p, irq_work_pending, "x");
	DUMP(p, nap_state_lost, "x");

#undef DUMP

	catch_memory_errors = 0;
	sync();
}

static void dump_all_pacas(void)
{
	int cpu;

	if (num_possible_cpus() == 0) {
		printf("No possible cpus, use 'dp #' to dump individual cpus\n");
		return;
	}

	for_each_possible_cpu(cpu)
		dump_one_paca(cpu);
}

static void dump_pacas(void)
{
	unsigned long num;
	int c;

	c = inchar();
	if (c == 'a') {
		dump_all_pacas();
		return;
	}

	termch = c;	/* Put c back, it wasn't 'a' */

	if (scanhex(&num))
		dump_one_paca(num);
	else
		dump_one_paca(xmon_owner);
}
#endif

#define isxdigit(c)	(('0' <= (c) && (c) <= '9') \
			 || ('a' <= (c) && (c) <= 'f') \
			 || ('A' <= (c) && (c) <= 'F'))
static void
dump(void)
{
	int c;

	c = inchar();

#ifdef CONFIG_PPC64
	if (c == 'p') {
		dump_pacas();
		return;
	}
#endif

	if ((isxdigit(c) && c != 'f' && c != 'd') || c == '\n')
		termch = c;
	scanhex((void *)&adrs);
	if (termch != '\n')
		termch = 0;
	if (c == 'i') {
		scanhex(&nidump);
		if (nidump == 0)
			nidump = 16;
		else if (nidump > MAX_DUMP)
			nidump = MAX_DUMP;
		adrs += ppc_inst_dump(adrs, nidump, 1);
		last_cmd = "di\n";
	} else if (c == 'l') {
		dump_log_buf();
	} else if (c == 'r') {
		scanhex(&ndump);
		if (ndump == 0)
			ndump = 64;
		xmon_rawdump(adrs, ndump);
		adrs += ndump;
		last_cmd = "dr\n";
	} else {
		scanhex(&ndump);
		if (ndump == 0)
			ndump = 64;
		else if (ndump > MAX_DUMP)
			ndump = MAX_DUMP;
		prdump(adrs, ndump);
		adrs += ndump;
		last_cmd = "d\n";
	}
}

static void
prdump(unsigned long adrs, long ndump)
{
	long n, m, c, r, nr;
	unsigned char temp[16];

	for (n = ndump; n > 0;) {
		printf(REG, adrs);
		putchar(' ');
		r = n < 16? n: 16;
		nr = mread(adrs, temp, r);
		adrs += nr;
		for (m = 0; m < r; ++m) {
			if ((m & (sizeof(long) - 1)) == 0 && m > 0)
				putchar(' ');
			if (m < nr)
				printf("%.2x", temp[m]);
			else
				printf("%s", fault_chars[fault_type]);
		}
		for (; m < 16; ++m) {
			if ((m & (sizeof(long) - 1)) == 0)
				putchar(' ');
			printf("  ");
		}
		printf("  |");
		for (m = 0; m < r; ++m) {
			if (m < nr) {
				c = temp[m];
				putchar(' ' <= c && c <= '~'? c: '.');
			} else
				putchar(' ');
		}
		n -= r;
		for (; m < 16; ++m)
			putchar(' ');
		printf("|\n");
		if (nr < r)
			break;
	}
}

typedef int (*instruction_dump_func)(unsigned long inst, unsigned long addr);

static int
generic_inst_dump(unsigned long adr, long count, int praddr,
			instruction_dump_func dump_func)
{
	int nr, dotted;
	unsigned long first_adr;
	unsigned long inst, last_inst = 0;
	unsigned char val[4];

	dotted = 0;
	for (first_adr = adr; count > 0; --count, adr += 4) {
		nr = mread(adr, val, 4);
		if (nr == 0) {
			if (praddr) {
				const char *x = fault_chars[fault_type];
				printf(REG"  %s%s%s%s\n", adr, x, x, x, x);
			}
			break;
		}
		inst = GETWORD(val);
		if (adr > first_adr && inst == last_inst) {
			if (!dotted) {
				printf(" ...\n");
				dotted = 1;
			}
			continue;
		}
		dotted = 0;
		last_inst = inst;
		if (praddr)
			printf(REG"  %.8x", adr, inst);
		printf("\t");
		dump_func(inst, adr);
		printf("\n");
	}
	return adr - first_adr;
}

static int
ppc_inst_dump(unsigned long adr, long count, int praddr)
{
	return generic_inst_dump(adr, count, praddr, print_insn_powerpc);
}

void
print_address(unsigned long addr)
{
	xmon_print_symbol(addr, "\t# ", "");
}

void
dump_log_buf(void)
{
	struct kmsg_dumper dumper = { .active = 1 };
	unsigned char buf[128];
	size_t len;

	if (setjmp(bus_error_jmp) != 0) {
		printf("Error dumping printk buffer!\n");
		return;
	}

	catch_memory_errors = 1;
	sync();

	kmsg_dump_rewind_nolock(&dumper);
	while (kmsg_dump_get_line_nolock(&dumper, false, buf, sizeof(buf), &len)) {
		buf[len] = '\0';
		printf("%s", buf);
	}

	sync();
	/* wait a little while to see if we get a machine check */
	__delay(200);
	catch_memory_errors = 0;
}

/*
 * Memory operations - move, set, print differences
 */
static unsigned long mdest;		/* destination address */
static unsigned long msrc;		/* source address */
static unsigned long mval;		/* byte value to set memory to */
static unsigned long mcount;		/* # bytes to affect */
static unsigned long mdiffs;		/* max # differences to print */

static void
memops(int cmd)
{
	scanhex((void *)&mdest);
	if( termch != '\n' )
		termch = 0;
	scanhex((void *)(cmd == 's'? &mval: &msrc));
	if( termch != '\n' )
		termch = 0;
	scanhex((void *)&mcount);
	switch( cmd ){
	case 'm':
		memmove((void *)mdest, (void *)msrc, mcount);
		break;
	case 's':
		memset((void *)mdest, mval, mcount);
		break;
	case 'd':
		if( termch != '\n' )
			termch = 0;
		scanhex((void *)&mdiffs);
		memdiffs((unsigned char *)mdest, (unsigned char *)msrc, mcount, mdiffs);
		break;
	}
}

static void
memdiffs(unsigned char *p1, unsigned char *p2, unsigned nb, unsigned maxpr)
{
	unsigned n, prt;

	prt = 0;
	for( n = nb; n > 0; --n )
		if( *p1++ != *p2++ )
			if( ++prt <= maxpr )
				printf("%.16x %.2x # %.16x %.2x\n", p1 - 1,
					p1[-1], p2 - 1, p2[-1]);
	if( prt > maxpr )
		printf("Total of %d differences\n", prt);
}

static unsigned mend;
static unsigned mask;

static void
memlocate(void)
{
	unsigned a, n;
	unsigned char val[4];

	last_cmd = "ml";
	scanhex((void *)&mdest);
	if (termch != '\n') {
		termch = 0;
		scanhex((void *)&mend);
		if (termch != '\n') {
			termch = 0;
			scanhex((void *)&mval);
			mask = ~0;
			if (termch != '\n') termch = 0;
			scanhex((void *)&mask);
		}
	}
	n = 0;
	for (a = mdest; a < mend; a += 4) {
		if (mread(a, val, 4) == 4
			&& ((GETWORD(val) ^ mval) & mask) == 0) {
			printf("%.16x:  %.16x\n", a, GETWORD(val));
			if (++n >= 10)
				break;
		}
	}
}

static unsigned long mskip = 0x1000;
static unsigned long mlim = 0xffffffff;

static void
memzcan(void)
{
	unsigned char v;
	unsigned a;
	int ok, ook;

	scanhex(&mdest);
	if (termch != '\n') termch = 0;
	scanhex(&mskip);
	if (termch != '\n') termch = 0;
	scanhex(&mlim);
	ook = 0;
	for (a = mdest; a < mlim; a += mskip) {
		ok = mread(a, &v, 1);
		if (ok && !ook) {
			printf("%.8x .. ", a);
		} else if (!ok && ook)
			printf("%.8x\n", a - mskip);
		ook = ok;
		if (a + mskip < a)
			break;
	}
	if (ook)
		printf("%.8x\n", a - mskip);
}

static void proccall(void)
{
	unsigned long args[8];
	unsigned long ret;
	int i;
	typedef unsigned long (*callfunc_t)(unsigned long, unsigned long,
			unsigned long, unsigned long, unsigned long,
			unsigned long, unsigned long, unsigned long);
	callfunc_t func;

	if (!scanhex(&adrs))
		return;
	if (termch != '\n')
		termch = 0;
	for (i = 0; i < 8; ++i)
		args[i] = 0;
	for (i = 0; i < 8; ++i) {
		if (!scanhex(&args[i]) || termch == '\n')
			break;
		termch = 0;
	}
	func = (callfunc_t) adrs;
	ret = 0;
	if (setjmp(bus_error_jmp) == 0) {
		catch_memory_errors = 1;
		sync();
		ret = func(args[0], args[1], args[2], args[3],
			   args[4], args[5], args[6], args[7]);
		sync();
		printf("return value is 0x%lx\n", ret);
	} else {
		printf("*** %x exception occurred\n", fault_except);
	}
	catch_memory_errors = 0;
}

/* Input scanning routines */
int
skipbl(void)
{
	int c;

	if( termch != 0 ){
		c = termch;
		termch = 0;
	} else
		c = inchar();
	while( c == ' ' || c == '\t' )
		c = inchar();
	return c;
}

#define N_PTREGS	44
static char *regnames[N_PTREGS] = {
	"r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7",
	"r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
	"r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",
	"r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31",
	"pc", "msr", "or3", "ctr", "lr", "xer", "ccr",
#ifdef CONFIG_PPC64
	"softe",
#else
	"mq",
#endif
	"trap", "dar", "dsisr", "res"
};

int
scanhex(unsigned long *vp)
{
	int c, d;
	unsigned long v;

	c = skipbl();
	if (c == '%') {
		/* parse register name */
		char regname[8];
		int i;

		for (i = 0; i < sizeof(regname) - 1; ++i) {
			c = inchar();
			if (!isalnum(c)) {
				termch = c;
				break;
			}
			regname[i] = c;
		}
		regname[i] = 0;
		for (i = 0; i < N_PTREGS; ++i) {
			if (strcmp(regnames[i], regname) == 0) {
				if (xmon_regs == NULL) {
					printf("regs not available\n");
					return 0;
				}
				*vp = ((unsigned long *)xmon_regs)[i];
				return 1;
			}
		}
		printf("invalid register name '%%%s'\n", regname);
		return 0;
	}

	/* skip leading "0x" if any */

	if (c == '0') {
		c = inchar();
		if (c == 'x') {
			c = inchar();
		} else {
			d = hexdigit(c);
			if (d == EOF) {
				termch = c;
				*vp = 0;
				return 1;
			}
		}
	} else if (c == '$') {
		int i;
		for (i=0; i<63; i++) {
			c = inchar();
			if (isspace(c)) {
				termch = c;
				break;
			}
			tmpstr[i] = c;
		}
		tmpstr[i++] = 0;
		*vp = 0;
		if (setjmp(bus_error_jmp) == 0) {
			catch_memory_errors = 1;
			sync();
			*vp = kallsyms_lookup_name(tmpstr);
			sync();
		}
		catch_memory_errors = 0;
		if (!(*vp)) {
			printf("unknown symbol '%s'\n", tmpstr);
			return 0;
		}
		return 1;
	}

	d = hexdigit(c);
	if (d == EOF) {
		termch = c;
		return 0;
	}
	v = 0;
	do {
		v = (v << 4) + d;
		c = inchar();
		d = hexdigit(c);
	} while (d != EOF);
	termch = c;
	*vp = v;
	return 1;
}

static void
scannl(void)
{
	int c;

	c = termch;
	termch = 0;
	while( c != '\n' )
		c = inchar();
}

static int hexdigit(int c)
{
	if( '0' <= c && c <= '9' )
		return c - '0';
	if( 'A' <= c && c <= 'F' )
		return c - ('A' - 10);
	if( 'a' <= c && c <= 'f' )
		return c - ('a' - 10);
	return EOF;
}

void
getstring(char *s, int size)
{
	int c;

	c = skipbl();
	do {
		if( size > 1 ){
			*s++ = c;
			--size;
		}
		c = inchar();
	} while( c != ' ' && c != '\t' && c != '\n' );
	termch = c;
	*s = 0;
}

static char line[256];
static char *lineptr;

static void
flush_input(void)
{
	lineptr = NULL;
}

static int
inchar(void)
{
	if (lineptr == NULL || *lineptr == 0) {
		if (xmon_gets(line, sizeof(line)) == NULL) {
			lineptr = NULL;
			return EOF;
		}
		lineptr = line;
	}
	return *lineptr++;
}

static void
take_input(char *str)
{
	lineptr = str;
}


static void
symbol_lookup(void)
{
	int type = inchar();
	unsigned long addr;
	static char tmp[64];

	switch (type) {
	case 'a':
		if (scanhex(&addr))
			xmon_print_symbol(addr, ": ", "\n");
		termch = 0;
		break;
	case 's':
		getstring(tmp, 64);
		if (setjmp(bus_error_jmp) == 0) {
			catch_memory_errors = 1;
			sync();
			addr = kallsyms_lookup_name(tmp);
			if (addr)
				printf("%s: %lx\n", tmp, addr);
			else
				printf("Symbol '%s' not found.\n", tmp);
			sync();
		}
		catch_memory_errors = 0;
		termch = 0;
		break;
	}
}


/* Print an address in numeric and symbolic form (if possible) */
static void xmon_print_symbol(unsigned long address, const char *mid,
			      const char *after)
{
	char *modname;
	const char *name = NULL;
	unsigned long offset, size;

	printf(REG, address);
	if (setjmp(bus_error_jmp) == 0) {
		catch_memory_errors = 1;
		sync();
		name = kallsyms_lookup(address, &size, &offset, &modname,
				       tmpstr);
		sync();
		/* wait a little while to see if we get a machine check */
		__delay(200);
	}

	catch_memory_errors = 0;

	if (name) {
		printf("%s%s+%#lx/%#lx", mid, name, offset, size);
		if (modname)
			printf(" [%s]", modname);
	}
	printf("%s", after);
}

#ifdef CONFIG_PPC_BOOK3S_64
void dump_segments(void)
{
	int i;
	unsigned long esid,vsid,valid;
	unsigned long llp;

	printf("SLB contents of cpu 0x%x\n", smp_processor_id());

	for (i = 0; i < mmu_slb_size; i++) {
		asm volatile("slbmfee  %0,%1" : "=r" (esid) : "r" (i));
		asm volatile("slbmfev  %0,%1" : "=r" (vsid) : "r" (i));
		valid = (esid & SLB_ESID_V);
		if (valid | esid | vsid) {
			printf("%02d %016lx %016lx", i, esid, vsid);
			if (valid) {
				llp = vsid & SLB_VSID_LLP;
				if (vsid & SLB_VSID_B_1T) {
					printf("  1T  ESID=%9lx  VSID=%13lx LLP:%3lx \n",
						GET_ESID_1T(esid),
						(vsid & ~SLB_VSID_B) >> SLB_VSID_SHIFT_1T,
						llp);
				} else {
					printf(" 256M ESID=%9lx  VSID=%13lx LLP:%3lx \n",
						GET_ESID(esid),
						(vsid & ~SLB_VSID_B) >> SLB_VSID_SHIFT,
						llp);
				}
			} else
				printf("\n");
		}
	}
}
#endif

#ifdef CONFIG_PPC_STD_MMU_32
void dump_segments(void)
{
	int i;

	printf("sr0-15 =");
	for (i = 0; i < 16; ++i)
		printf(" %x", mfsrin(i));
	printf("\n");
}
#endif

#ifdef CONFIG_44x
static void dump_tlb_44x(void)
{
	int i;

	for (i = 0; i < PPC44x_TLB_SIZE; i++) {
		unsigned long w0,w1,w2;
		asm volatile("tlbre  %0,%1,0" : "=r" (w0) : "r" (i));
		asm volatile("tlbre  %0,%1,1" : "=r" (w1) : "r" (i));
		asm volatile("tlbre  %0,%1,2" : "=r" (w2) : "r" (i));
		printf("[%02x] %08x %08x %08x ", i, w0, w1, w2);
		if (w0 & PPC44x_TLB_VALID) {
			printf("V %08x -> %01x%08x %c%c%c%c%c",
			       w0 & PPC44x_TLB_EPN_MASK,
			       w1 & PPC44x_TLB_ERPN_MASK,
			       w1 & PPC44x_TLB_RPN_MASK,
			       (w2 & PPC44x_TLB_W) ? 'W' : 'w',
			       (w2 & PPC44x_TLB_I) ? 'I' : 'i',
			       (w2 & PPC44x_TLB_M) ? 'M' : 'm',
			       (w2 & PPC44x_TLB_G) ? 'G' : 'g',
			       (w2 & PPC44x_TLB_E) ? 'E' : 'e');
		}
		printf("\n");
	}
}
#endif /* CONFIG_44x */

#ifdef CONFIG_PPC_BOOK3E
static void dump_tlb_book3e(void)
{
	u32 mmucfg, pidmask, lpidmask;
	u64 ramask;
	int i, tlb, ntlbs, pidsz, lpidsz, rasz, lrat = 0;
	int mmu_version;
	static const char *pgsz_names[] = {
		"  1K",
		"  2K",
		"  4K",
		"  8K",
		" 16K",
		" 32K",
		" 64K",
		"128K",
		"256K",
		"512K",
		"  1M",
		"  2M",
		"  4M",
		"  8M",
		" 16M",
		" 32M",
		" 64M",
		"128M",
		"256M",
		"512M",
		"  1G",
		"  2G",
		"  4G",
		"  8G",
		" 16G",
		" 32G",
		" 64G",
		"128G",
		"256G",
		"512G",
		"  1T",
		"  2T",
	};

	/* Gather some infos about the MMU */
	mmucfg = mfspr(SPRN_MMUCFG);
	mmu_version = (mmucfg & 3) + 1;
	ntlbs = ((mmucfg >> 2) & 3) + 1;
	pidsz = ((mmucfg >> 6) & 0x1f) + 1;
	lpidsz = (mmucfg >> 24) & 0xf;
	rasz = (mmucfg >> 16) & 0x7f;
	if ((mmu_version > 1) && (mmucfg & 0x10000))
		lrat = 1;
	printf("Book3E MMU MAV=%d.0,%d TLBs,%d-bit PID,%d-bit LPID,%d-bit RA\n",
	       mmu_version, ntlbs, pidsz, lpidsz, rasz);
	pidmask = (1ul << pidsz) - 1;
	lpidmask = (1ul << lpidsz) - 1;
	ramask = (1ull << rasz) - 1;

	for (tlb = 0; tlb < ntlbs; tlb++) {
		u32 tlbcfg;
		int nent, assoc, new_cc = 1;
		printf("TLB %d:\n------\n", tlb);
		switch(tlb) {
		case 0:
			tlbcfg = mfspr(SPRN_TLB0CFG);
			break;
		case 1:
			tlbcfg = mfspr(SPRN_TLB1CFG);
			break;
		case 2:
			tlbcfg = mfspr(SPRN_TLB2CFG);
			break;
		case 3:
			tlbcfg = mfspr(SPRN_TLB3CFG);
			break;
		default:
			printf("Unsupported TLB number !\n");
			continue;
		}
		nent = tlbcfg & 0xfff;
		assoc = (tlbcfg >> 24) & 0xff;
		for (i = 0; i < nent; i++) {
			u32 mas0 = MAS0_TLBSEL(tlb);
			u32 mas1 = MAS1_TSIZE(BOOK3E_PAGESZ_4K);
			u64 mas2 = 0;
			u64 mas7_mas3;
			int esel = i, cc = i;

			if (assoc != 0) {
				cc = i / assoc;
				esel = i % assoc;
				mas2 = cc * 0x1000;
			}

			mas0 |= MAS0_ESEL(esel);
			mtspr(SPRN_MAS0, mas0);
			mtspr(SPRN_MAS1, mas1);
			mtspr(SPRN_MAS2, mas2);
			asm volatile("tlbre  0,0,0" : : : "memory");
			mas1 = mfspr(SPRN_MAS1);
			mas2 = mfspr(SPRN_MAS2);
			mas7_mas3 = mfspr(SPRN_MAS7_MAS3);
			if (assoc && (i % assoc) == 0)
				new_cc = 1;
			if (!(mas1 & MAS1_VALID))
				continue;
			if (assoc == 0)
				printf("%04x- ", i);
			else if (new_cc)
				printf("%04x-%c", cc, 'A' + esel);
			else
				printf("    |%c", 'A' + esel);
			new_cc = 0;
			printf(" %016llx %04x %s %c%c AS%c",
			       mas2 & ~0x3ffull,
			       (mas1 >> 16) & 0x3fff,
			       pgsz_names[(mas1 >> 7) & 0x1f],
			       mas1 & MAS1_IND ? 'I' : ' ',
			       mas1 & MAS1_IPROT ? 'P' : ' ',
			       mas1 & MAS1_TS ? '1' : '0');
			printf(" %c%c%c%c%c%c%c",
			       mas2 & MAS2_X0 ? 'a' : ' ',
			       mas2 & MAS2_X1 ? 'v' : ' ',
			       mas2 & MAS2_W  ? 'w' : ' ',
			       mas2 & MAS2_I  ? 'i' : ' ',
			       mas2 & MAS2_M  ? 'm' : ' ',
			       mas2 & MAS2_G  ? 'g' : ' ',
			       mas2 & MAS2_E  ? 'e' : ' ');
			printf(" %016llx", mas7_mas3 & ramask & ~0x7ffull);
			if (mas1 & MAS1_IND)
				printf(" %s\n",
				       pgsz_names[(mas7_mas3 >> 1) & 0x1f]);
			else
				printf(" U%c%c%c S%c%c%c\n",
				       mas7_mas3 & MAS3_UX ? 'x' : ' ',
				       mas7_mas3 & MAS3_UW ? 'w' : ' ',
				       mas7_mas3 & MAS3_UR ? 'r' : ' ',
				       mas7_mas3 & MAS3_SX ? 'x' : ' ',
				       mas7_mas3 & MAS3_SW ? 'w' : ' ',
				       mas7_mas3 & MAS3_SR ? 'r' : ' ');
		}
	}
}
#endif /* CONFIG_PPC_BOOK3E */

static void xmon_init(int enable)
{
	if (enable) {
		__debugger = xmon;
		__debugger_ipi = xmon_ipi;
		__debugger_bpt = xmon_bpt;
		__debugger_sstep = xmon_sstep;
		__debugger_iabr_match = xmon_iabr_match;
		__debugger_break_match = xmon_break_match;
		__debugger_fault_handler = xmon_fault_handler;
	} else {
		__debugger = NULL;
		__debugger_ipi = NULL;
		__debugger_bpt = NULL;
		__debugger_sstep = NULL;
		__debugger_iabr_match = NULL;
		__debugger_break_match = NULL;
		__debugger_fault_handler = NULL;
	}
}

#ifdef CONFIG_MAGIC_SYSRQ
static void sysrq_handle_xmon(int key)
{
	/* ensure xmon is enabled */
	xmon_init(1);
	debugger(get_irq_regs());
}

static struct sysrq_key_op sysrq_xmon_op = {
	.handler =	sysrq_handle_xmon,
	.help_msg =	"xmon(x)",
	.action_msg =	"Entering xmon",
};

static int __init setup_xmon_sysrq(void)
{
	register_sysrq_key('x', &sysrq_xmon_op);
	return 0;
}
__initcall(setup_xmon_sysrq);
#endif /* CONFIG_MAGIC_SYSRQ */

static int __initdata xmon_early, xmon_off;

static int __init early_parse_xmon(char *p)
{
	if (!p || strncmp(p, "early", 5) == 0) {
		/* just "xmon" is equivalent to "xmon=early" */
		xmon_init(1);
		xmon_early = 1;
	} else if (strncmp(p, "on", 2) == 0)
		xmon_init(1);
	else if (strncmp(p, "off", 3) == 0)
		xmon_off = 1;
	else if (strncmp(p, "nobt", 4) == 0)
		xmon_no_auto_backtrace = 1;
	else
		return 1;

	return 0;
}
early_param("xmon", early_parse_xmon);

void __init xmon_setup(void)
{
#ifdef CONFIG_XMON_DEFAULT
	if (!xmon_off)
		xmon_init(1);
#endif
	if (xmon_early)
		debugger(NULL);
}

#ifdef CONFIG_SPU_BASE

struct spu_info {
	struct spu *spu;
	u64 saved_mfc_sr1_RW;
	u32 saved_spu_runcntl_RW;
	unsigned long dump_addr;
	u8 stopped_ok;
};

#define XMON_NUM_SPUS	16	/* Enough for current hardware */

static struct spu_info spu_info[XMON_NUM_SPUS];

void xmon_register_spus(struct list_head *list)
{
	struct spu *spu;

	list_for_each_entry(spu, list, full_list) {
		if (spu->number >= XMON_NUM_SPUS) {
			WARN_ON(1);
			continue;
		}

		spu_info[spu->number].spu = spu;
		spu_info[spu->number].stopped_ok = 0;
		spu_info[spu->number].dump_addr = (unsigned long)
				spu_info[spu->number].spu->local_store;
	}
}

static void stop_spus(void)
{
	struct spu *spu;
	int i;
	u64 tmp;

	for (i = 0; i < XMON_NUM_SPUS; i++) {
		if (!spu_info[i].spu)
			continue;

		if (setjmp(bus_error_jmp) == 0) {
			catch_memory_errors = 1;
			sync();

			spu = spu_info[i].spu;

			spu_info[i].saved_spu_runcntl_RW =
				in_be32(&spu->problem->spu_runcntl_RW);

			tmp = spu_mfc_sr1_get(spu);
			spu_info[i].saved_mfc_sr1_RW = tmp;

			tmp &= ~MFC_STATE1_MASTER_RUN_CONTROL_MASK;
			spu_mfc_sr1_set(spu, tmp);

			sync();
			__delay(200);

			spu_info[i].stopped_ok = 1;

			printf("Stopped spu %.2d (was %s)\n", i,
					spu_info[i].saved_spu_runcntl_RW ?
					"running" : "stopped");
		} else {
			catch_memory_errors = 0;
			printf("*** Error stopping spu %.2d\n", i);
		}
		catch_memory_errors = 0;
	}
}

static void restart_spus(void)
{
	struct spu *spu;
	int i;

	for (i = 0; i < XMON_NUM_SPUS; i++) {
		if (!spu_info[i].spu)
			continue;

		if (!spu_info[i].stopped_ok) {
			printf("*** Error, spu %d was not successfully stopped"
					", not restarting\n", i);
			continue;
		}

		if (setjmp(bus_error_jmp) == 0) {
			catch_memory_errors = 1;
			sync();

			spu = spu_info[i].spu;
			spu_mfc_sr1_set(spu, spu_info[i].saved_mfc_sr1_RW);
			out_be32(&spu->problem->spu_runcntl_RW,
					spu_info[i].saved_spu_runcntl_RW);

			sync();
			__delay(200);

			printf("Restarted spu %.2d\n", i);
		} else {
			catch_memory_errors = 0;
			printf("*** Error restarting spu %.2d\n", i);
		}
		catch_memory_errors = 0;
	}
}

#define DUMP_WIDTH	23
#define DUMP_VALUE(format, field, value)				\
do {									\
	if (setjmp(bus_error_jmp) == 0) {				\
		catch_memory_errors = 1;				\
		sync();							\
		printf("  %-*s = "format"\n", DUMP_WIDTH,		\
				#field, value);				\
		sync();							\
		__delay(200);						\
	} else {							\
		catch_memory_errors = 0;				\
		printf("  %-*s = *** Error reading field.\n",		\
					DUMP_WIDTH, #field);		\
	}								\
	catch_memory_errors = 0;					\
} while (0)

#define DUMP_FIELD(obj, format, field)	\
	DUMP_VALUE(format, field, obj->field)

static void dump_spu_fields(struct spu *spu)
{
	printf("Dumping spu fields at address %p:\n", spu);

	DUMP_FIELD(spu, "0x%x", number);
	DUMP_FIELD(spu, "%s", name);
	DUMP_FIELD(spu, "0x%lx", local_store_phys);
	DUMP_FIELD(spu, "0x%p", local_store);
	DUMP_FIELD(spu, "0x%lx", ls_size);
	DUMP_FIELD(spu, "0x%x", node);
	DUMP_FIELD(spu, "0x%lx", flags);
	DUMP_FIELD(spu, "%d", class_0_pending);
	DUMP_FIELD(spu, "0x%lx", class_0_dar);
	DUMP_FIELD(spu, "0x%lx", class_1_dar);
	DUMP_FIELD(spu, "0x%lx", class_1_dsisr);
	DUMP_FIELD(spu, "0x%lx", irqs[0]);
	DUMP_FIELD(spu, "0x%lx", irqs[1]);
	DUMP_FIELD(spu, "0x%lx", irqs[2]);
	DUMP_FIELD(spu, "0x%x", slb_replace);
	DUMP_FIELD(spu, "%d", pid);
	DUMP_FIELD(spu, "0x%p", mm);
	DUMP_FIELD(spu, "0x%p", ctx);
	DUMP_FIELD(spu, "0x%p", rq);
	DUMP_FIELD(spu, "0x%p", timestamp);
	DUMP_FIELD(spu, "0x%lx", problem_phys);
	DUMP_FIELD(spu, "0x%p", problem);
	DUMP_VALUE("0x%x", problem->spu_runcntl_RW,
			in_be32(&spu->problem->spu_runcntl_RW));
	DUMP_VALUE("0x%x", problem->spu_status_R,
			in_be32(&spu->problem->spu_status_R));
	DUMP_VALUE("0x%x", problem->spu_npc_RW,
			in_be32(&spu->problem->spu_npc_RW));
	DUMP_FIELD(spu, "0x%p", priv2);
	DUMP_FIELD(spu, "0x%p", pdata);
}

int
spu_inst_dump(unsigned long adr, long count, int praddr)
{
	return generic_inst_dump(adr, count, praddr, print_insn_spu);
}

static void dump_spu_ls(unsigned long num, int subcmd)
{
	unsigned long offset, addr, ls_addr;

	if (setjmp(bus_error_jmp) == 0) {
		catch_memory_errors = 1;
		sync();
		ls_addr = (unsigned long)spu_info[num].spu->local_store;
		sync();
		__delay(200);
	} else {
		catch_memory_errors = 0;
		printf("*** Error: accessing spu info for spu %d\n", num);
		return;
	}
	catch_memory_errors = 0;

	if (scanhex(&offset))
		addr = ls_addr + offset;
	else
		addr = spu_info[num].dump_addr;

	if (addr >= ls_addr + LS_SIZE) {
		printf("*** Error: address outside of local store\n");
		return;
	}

	switch (subcmd) {
	case 'i':
		addr += spu_inst_dump(addr, 16, 1);
		last_cmd = "sdi\n";
		break;
	default:
		prdump(addr, 64);
		addr += 64;
		last_cmd = "sd\n";
		break;
	}

	spu_info[num].dump_addr = addr;
}

static int do_spu_cmd(void)
{
	static unsigned long num = 0;
	int cmd, subcmd = 0;

	cmd = inchar();
	switch (cmd) {
	case 's':
		stop_spus();
		break;
	case 'r':
		restart_spus();
		break;
	case 'd':
		subcmd = inchar();
		if (isxdigit(subcmd) || subcmd == '\n')
			termch = subcmd;
	case 'f':
		scanhex(&num);
		if (num >= XMON_NUM_SPUS || !spu_info[num].spu) {
			printf("*** Error: invalid spu number\n");
			return 0;
		}

		switch (cmd) {
		case 'f':
			dump_spu_fields(spu_info[num].spu);
			break;
		default:
			dump_spu_ls(num, subcmd);
			break;
		}

		break;
	default:
		return -1;
	}

	return 0;
}
#else /* ! CONFIG_SPU_BASE */
static int do_spu_cmd(void)
{
	return -1;
}
#endif
