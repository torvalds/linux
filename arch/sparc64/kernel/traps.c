/* $Id: traps.c,v 1.85 2002/02/09 19:49:31 davem Exp $
 * arch/sparc64/kernel/traps.c
 *
 * Copyright (C) 1995,1997 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1997,1999,2000 Jakub Jelinek (jakub@redhat.com)
 */

/*
 * I like traps on v9, :))))
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/sched.h>  /* for jiffies */
#include <linux/kernel.h>
#include <linux/kallsyms.h>
#include <linux/signal.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/mm.h>
#include <linux/init.h>

#include <asm/delay.h>
#include <asm/system.h>
#include <asm/ptrace.h>
#include <asm/oplib.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/unistd.h>
#include <asm/uaccess.h>
#include <asm/fpumacro.h>
#include <asm/lsu.h>
#include <asm/dcu.h>
#include <asm/estate.h>
#include <asm/chafsr.h>
#include <asm/sfafsr.h>
#include <asm/psrcompat.h>
#include <asm/processor.h>
#include <asm/timer.h>
#include <asm/kdebug.h>
#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif

struct notifier_block *sparc64die_chain;
static DEFINE_SPINLOCK(die_notifier_lock);

int register_die_notifier(struct notifier_block *nb)
{
	int err = 0;
	unsigned long flags;
	spin_lock_irqsave(&die_notifier_lock, flags);
	err = notifier_chain_register(&sparc64die_chain, nb);
	spin_unlock_irqrestore(&die_notifier_lock, flags);
	return err;
}

/* When an irrecoverable trap occurs at tl > 0, the trap entry
 * code logs the trap state registers at every level in the trap
 * stack.  It is found at (pt_regs + sizeof(pt_regs)) and the layout
 * is as follows:
 */
struct tl1_traplog {
	struct {
		unsigned long tstate;
		unsigned long tpc;
		unsigned long tnpc;
		unsigned long tt;
	} trapstack[4];
	unsigned long tl;
};

static void dump_tl1_traplog(struct tl1_traplog *p)
{
	int i;

	printk("TRAPLOG: Error at trap level 0x%lx, dumping track stack.\n",
	       p->tl);
	for (i = 0; i < 4; i++) {
		printk(KERN_CRIT
		       "TRAPLOG: Trap level %d TSTATE[%016lx] TPC[%016lx] "
		       "TNPC[%016lx] TT[%lx]\n",
		       i + 1,
		       p->trapstack[i].tstate, p->trapstack[i].tpc,
		       p->trapstack[i].tnpc, p->trapstack[i].tt);
	}
}

void do_call_debug(struct pt_regs *regs) 
{ 
	notify_die(DIE_CALL, "debug call", regs, 0, 255, SIGINT); 
}

void bad_trap(struct pt_regs *regs, long lvl)
{
	char buffer[32];
	siginfo_t info;

	if (notify_die(DIE_TRAP, "bad trap", regs,
		       0, lvl, SIGTRAP) == NOTIFY_STOP)
		return;

	if (lvl < 0x100) {
		sprintf(buffer, "Bad hw trap %lx at tl0\n", lvl);
		die_if_kernel(buffer, regs);
	}

	lvl -= 0x100;
	if (regs->tstate & TSTATE_PRIV) {
		sprintf(buffer, "Kernel bad sw trap %lx", lvl);
		die_if_kernel(buffer, regs);
	}
	if (test_thread_flag(TIF_32BIT)) {
		regs->tpc &= 0xffffffff;
		regs->tnpc &= 0xffffffff;
	}
	info.si_signo = SIGILL;
	info.si_errno = 0;
	info.si_code = ILL_ILLTRP;
	info.si_addr = (void __user *)regs->tpc;
	info.si_trapno = lvl;
	force_sig_info(SIGILL, &info, current);
}

void bad_trap_tl1(struct pt_regs *regs, long lvl)
{
	char buffer[32];
	
	if (notify_die(DIE_TRAP_TL1, "bad trap tl1", regs,
		       0, lvl, SIGTRAP) == NOTIFY_STOP)
		return;

	dump_tl1_traplog((struct tl1_traplog *)(regs + 1));

	sprintf (buffer, "Bad trap %lx at tl>0", lvl);
	die_if_kernel (buffer, regs);
}

#ifdef CONFIG_DEBUG_BUGVERBOSE
void do_BUG(const char *file, int line)
{
	bust_spinlocks(1);
	printk("kernel BUG at %s:%d!\n", file, line);
}
#endif

void spitfire_insn_access_exception(struct pt_regs *regs, unsigned long sfsr, unsigned long sfar)
{
	siginfo_t info;

	if (notify_die(DIE_TRAP, "instruction access exception", regs,
		       0, 0x8, SIGTRAP) == NOTIFY_STOP)
		return;

	if (regs->tstate & TSTATE_PRIV) {
		printk("spitfire_insn_access_exception: SFSR[%016lx] "
		       "SFAR[%016lx], going.\n", sfsr, sfar);
		die_if_kernel("Iax", regs);
	}
	if (test_thread_flag(TIF_32BIT)) {
		regs->tpc &= 0xffffffff;
		regs->tnpc &= 0xffffffff;
	}
	info.si_signo = SIGSEGV;
	info.si_errno = 0;
	info.si_code = SEGV_MAPERR;
	info.si_addr = (void __user *)regs->tpc;
	info.si_trapno = 0;
	force_sig_info(SIGSEGV, &info, current);
}

void spitfire_insn_access_exception_tl1(struct pt_regs *regs, unsigned long sfsr, unsigned long sfar)
{
	if (notify_die(DIE_TRAP_TL1, "instruction access exception tl1", regs,
		       0, 0x8, SIGTRAP) == NOTIFY_STOP)
		return;

	dump_tl1_traplog((struct tl1_traplog *)(regs + 1));
	spitfire_insn_access_exception(regs, sfsr, sfar);
}

void spitfire_data_access_exception(struct pt_regs *regs, unsigned long sfsr, unsigned long sfar)
{
	siginfo_t info;

	if (notify_die(DIE_TRAP, "data access exception", regs,
		       0, 0x30, SIGTRAP) == NOTIFY_STOP)
		return;

	if (regs->tstate & TSTATE_PRIV) {
		/* Test if this comes from uaccess places. */
		const struct exception_table_entry *entry;

		entry = search_exception_tables(regs->tpc);
		if (entry) {
			/* Ouch, somebody is trying VM hole tricks on us... */
#ifdef DEBUG_EXCEPTIONS
			printk("Exception: PC<%016lx> faddr<UNKNOWN>\n", regs->tpc);
			printk("EX_TABLE: insn<%016lx> fixup<%016lx>\n",
			       regs->tpc, entry->fixup);
#endif
			regs->tpc = entry->fixup;
			regs->tnpc = regs->tpc + 4;
			return;
		}
		/* Shit... */
		printk("spitfire_data_access_exception: SFSR[%016lx] "
		       "SFAR[%016lx], going.\n", sfsr, sfar);
		die_if_kernel("Dax", regs);
	}

	info.si_signo = SIGSEGV;
	info.si_errno = 0;
	info.si_code = SEGV_MAPERR;
	info.si_addr = (void __user *)sfar;
	info.si_trapno = 0;
	force_sig_info(SIGSEGV, &info, current);
}

void spitfire_data_access_exception_tl1(struct pt_regs *regs, unsigned long sfsr, unsigned long sfar)
{
	if (notify_die(DIE_TRAP_TL1, "data access exception tl1", regs,
		       0, 0x30, SIGTRAP) == NOTIFY_STOP)
		return;

	dump_tl1_traplog((struct tl1_traplog *)(regs + 1));
	spitfire_data_access_exception(regs, sfsr, sfar);
}

#ifdef CONFIG_PCI
/* This is really pathetic... */
extern volatile int pci_poke_in_progress;
extern volatile int pci_poke_cpu;
extern volatile int pci_poke_faulted;
#endif

/* When access exceptions happen, we must do this. */
static void spitfire_clean_and_reenable_l1_caches(void)
{
	unsigned long va;

	if (tlb_type != spitfire)
		BUG();

	/* Clean 'em. */
	for (va =  0; va < (PAGE_SIZE << 1); va += 32) {
		spitfire_put_icache_tag(va, 0x0);
		spitfire_put_dcache_tag(va, 0x0);
	}

	/* Re-enable in LSU. */
	__asm__ __volatile__("flush %%g6\n\t"
			     "membar #Sync\n\t"
			     "stxa %0, [%%g0] %1\n\t"
			     "membar #Sync"
			     : /* no outputs */
			     : "r" (LSU_CONTROL_IC | LSU_CONTROL_DC |
				    LSU_CONTROL_IM | LSU_CONTROL_DM),
			     "i" (ASI_LSU_CONTROL)
			     : "memory");
}

static void spitfire_enable_estate_errors(void)
{
	__asm__ __volatile__("stxa	%0, [%%g0] %1\n\t"
			     "membar	#Sync"
			     : /* no outputs */
			     : "r" (ESTATE_ERR_ALL),
			       "i" (ASI_ESTATE_ERROR_EN));
}

static char ecc_syndrome_table[] = {
	0x4c, 0x40, 0x41, 0x48, 0x42, 0x48, 0x48, 0x49,
	0x43, 0x48, 0x48, 0x49, 0x48, 0x49, 0x49, 0x4a,
	0x44, 0x48, 0x48, 0x20, 0x48, 0x39, 0x4b, 0x48,
	0x48, 0x25, 0x31, 0x48, 0x28, 0x48, 0x48, 0x2c,
	0x45, 0x48, 0x48, 0x21, 0x48, 0x3d, 0x04, 0x48,
	0x48, 0x4b, 0x35, 0x48, 0x2d, 0x48, 0x48, 0x29,
	0x48, 0x00, 0x01, 0x48, 0x0a, 0x48, 0x48, 0x4b,
	0x0f, 0x48, 0x48, 0x4b, 0x48, 0x49, 0x49, 0x48,
	0x46, 0x48, 0x48, 0x2a, 0x48, 0x3b, 0x27, 0x48,
	0x48, 0x4b, 0x33, 0x48, 0x22, 0x48, 0x48, 0x2e,
	0x48, 0x19, 0x1d, 0x48, 0x1b, 0x4a, 0x48, 0x4b,
	0x1f, 0x48, 0x4a, 0x4b, 0x48, 0x4b, 0x4b, 0x48,
	0x48, 0x4b, 0x24, 0x48, 0x07, 0x48, 0x48, 0x36,
	0x4b, 0x48, 0x48, 0x3e, 0x48, 0x30, 0x38, 0x48,
	0x49, 0x48, 0x48, 0x4b, 0x48, 0x4b, 0x16, 0x48,
	0x48, 0x12, 0x4b, 0x48, 0x49, 0x48, 0x48, 0x4b,
	0x47, 0x48, 0x48, 0x2f, 0x48, 0x3f, 0x4b, 0x48,
	0x48, 0x06, 0x37, 0x48, 0x23, 0x48, 0x48, 0x2b,
	0x48, 0x05, 0x4b, 0x48, 0x4b, 0x48, 0x48, 0x32,
	0x26, 0x48, 0x48, 0x3a, 0x48, 0x34, 0x3c, 0x48,
	0x48, 0x11, 0x15, 0x48, 0x13, 0x4a, 0x48, 0x4b,
	0x17, 0x48, 0x4a, 0x4b, 0x48, 0x4b, 0x4b, 0x48,
	0x49, 0x48, 0x48, 0x4b, 0x48, 0x4b, 0x1e, 0x48,
	0x48, 0x1a, 0x4b, 0x48, 0x49, 0x48, 0x48, 0x4b,
	0x48, 0x08, 0x0d, 0x48, 0x02, 0x48, 0x48, 0x49,
	0x03, 0x48, 0x48, 0x49, 0x48, 0x4b, 0x4b, 0x48,
	0x49, 0x48, 0x48, 0x49, 0x48, 0x4b, 0x10, 0x48,
	0x48, 0x14, 0x4b, 0x48, 0x4b, 0x48, 0x48, 0x4b,
	0x49, 0x48, 0x48, 0x49, 0x48, 0x4b, 0x18, 0x48,
	0x48, 0x1c, 0x4b, 0x48, 0x4b, 0x48, 0x48, 0x4b,
	0x4a, 0x0c, 0x09, 0x48, 0x0e, 0x48, 0x48, 0x4b,
	0x0b, 0x48, 0x48, 0x4b, 0x48, 0x4b, 0x4b, 0x4a
};

static char *syndrome_unknown = "<Unknown>";

static void spitfire_log_udb_syndrome(unsigned long afar, unsigned long udbh, unsigned long udbl, unsigned long bit)
{
	unsigned short scode;
	char memmod_str[64], *p;

	if (udbl & bit) {
		scode = ecc_syndrome_table[udbl & 0xff];
		if (prom_getunumber(scode, afar,
				    memmod_str, sizeof(memmod_str)) == -1)
			p = syndrome_unknown;
		else
			p = memmod_str;
		printk(KERN_WARNING "CPU[%d]: UDBL Syndrome[%x] "
		       "Memory Module \"%s\"\n",
		       smp_processor_id(), scode, p);
	}

	if (udbh & bit) {
		scode = ecc_syndrome_table[udbh & 0xff];
		if (prom_getunumber(scode, afar,
				    memmod_str, sizeof(memmod_str)) == -1)
			p = syndrome_unknown;
		else
			p = memmod_str;
		printk(KERN_WARNING "CPU[%d]: UDBH Syndrome[%x] "
		       "Memory Module \"%s\"\n",
		       smp_processor_id(), scode, p);
	}

}

static void spitfire_cee_log(unsigned long afsr, unsigned long afar, unsigned long udbh, unsigned long udbl, int tl1, struct pt_regs *regs)
{

	printk(KERN_WARNING "CPU[%d]: Correctable ECC Error "
	       "AFSR[%lx] AFAR[%016lx] UDBL[%lx] UDBH[%lx] TL>1[%d]\n",
	       smp_processor_id(), afsr, afar, udbl, udbh, tl1);

	spitfire_log_udb_syndrome(afar, udbh, udbl, UDBE_CE);

	/* We always log it, even if someone is listening for this
	 * trap.
	 */
	notify_die(DIE_TRAP, "Correctable ECC Error", regs,
		   0, TRAP_TYPE_CEE, SIGTRAP);

	/* The Correctable ECC Error trap does not disable I/D caches.  So
	 * we only have to restore the ESTATE Error Enable register.
	 */
	spitfire_enable_estate_errors();
}

static void spitfire_ue_log(unsigned long afsr, unsigned long afar, unsigned long udbh, unsigned long udbl, unsigned long tt, int tl1, struct pt_regs *regs)
{
	siginfo_t info;

	printk(KERN_WARNING "CPU[%d]: Uncorrectable Error AFSR[%lx] "
	       "AFAR[%lx] UDBL[%lx] UDBH[%ld] TT[%lx] TL>1[%d]\n",
	       smp_processor_id(), afsr, afar, udbl, udbh, tt, tl1);

	/* XXX add more human friendly logging of the error status
	 * XXX as is implemented for cheetah
	 */

	spitfire_log_udb_syndrome(afar, udbh, udbl, UDBE_UE);

	/* We always log it, even if someone is listening for this
	 * trap.
	 */
	notify_die(DIE_TRAP, "Uncorrectable Error", regs,
		   0, tt, SIGTRAP);

	if (regs->tstate & TSTATE_PRIV) {
		if (tl1)
			dump_tl1_traplog((struct tl1_traplog *)(regs + 1));
		die_if_kernel("UE", regs);
	}

	/* XXX need more intelligent processing here, such as is implemented
	 * XXX for cheetah errors, in fact if the E-cache still holds the
	 * XXX line with bad parity this will loop
	 */

	spitfire_clean_and_reenable_l1_caches();
	spitfire_enable_estate_errors();

	if (test_thread_flag(TIF_32BIT)) {
		regs->tpc &= 0xffffffff;
		regs->tnpc &= 0xffffffff;
	}
	info.si_signo = SIGBUS;
	info.si_errno = 0;
	info.si_code = BUS_OBJERR;
	info.si_addr = (void *)0;
	info.si_trapno = 0;
	force_sig_info(SIGBUS, &info, current);
}

void spitfire_access_error(struct pt_regs *regs, unsigned long status_encoded, unsigned long afar)
{
	unsigned long afsr, tt, udbh, udbl;
	int tl1;

	afsr = (status_encoded & SFSTAT_AFSR_MASK) >> SFSTAT_AFSR_SHIFT;
	tt = (status_encoded & SFSTAT_TRAP_TYPE) >> SFSTAT_TRAP_TYPE_SHIFT;
	tl1 = (status_encoded & SFSTAT_TL_GT_ONE) ? 1 : 0;
	udbl = (status_encoded & SFSTAT_UDBL_MASK) >> SFSTAT_UDBL_SHIFT;
	udbh = (status_encoded & SFSTAT_UDBH_MASK) >> SFSTAT_UDBH_SHIFT;

#ifdef CONFIG_PCI
	if (tt == TRAP_TYPE_DAE &&
	    pci_poke_in_progress && pci_poke_cpu == smp_processor_id()) {
		spitfire_clean_and_reenable_l1_caches();
		spitfire_enable_estate_errors();

		pci_poke_faulted = 1;
		regs->tnpc = regs->tpc + 4;
		return;
	}
#endif

	if (afsr & SFAFSR_UE)
		spitfire_ue_log(afsr, afar, udbh, udbl, tt, tl1, regs);

	if (tt == TRAP_TYPE_CEE) {
		/* Handle the case where we took a CEE trap, but ACK'd
		 * only the UE state in the UDB error registers.
		 */
		if (afsr & SFAFSR_UE) {
			if (udbh & UDBE_CE) {
				__asm__ __volatile__(
					"stxa	%0, [%1] %2\n\t"
					"membar	#Sync"
					: /* no outputs */
					: "r" (udbh & UDBE_CE),
					  "r" (0x0), "i" (ASI_UDB_ERROR_W));
			}
			if (udbl & UDBE_CE) {
				__asm__ __volatile__(
					"stxa	%0, [%1] %2\n\t"
					"membar	#Sync"
					: /* no outputs */
					: "r" (udbl & UDBE_CE),
					  "r" (0x18), "i" (ASI_UDB_ERROR_W));
			}
		}

		spitfire_cee_log(afsr, afar, udbh, udbl, tl1, regs);
	}
}

int cheetah_pcache_forced_on;

void cheetah_enable_pcache(void)
{
	unsigned long dcr;

	printk("CHEETAH: Enabling P-Cache on cpu %d.\n",
	       smp_processor_id());

	__asm__ __volatile__("ldxa [%%g0] %1, %0"
			     : "=r" (dcr)
			     : "i" (ASI_DCU_CONTROL_REG));
	dcr |= (DCU_PE | DCU_HPE | DCU_SPE | DCU_SL);
	__asm__ __volatile__("stxa %0, [%%g0] %1\n\t"
			     "membar #Sync"
			     : /* no outputs */
			     : "r" (dcr), "i" (ASI_DCU_CONTROL_REG));
}

/* Cheetah error trap handling. */
static unsigned long ecache_flush_physbase;
static unsigned long ecache_flush_linesize;
static unsigned long ecache_flush_size;

/* WARNING: The error trap handlers in assembly know the precise
 *	    layout of the following structure.
 *
 * C-level handlers below use this information to log the error
 * and then determine how to recover (if possible).
 */
struct cheetah_err_info {
/*0x00*/u64 afsr;
/*0x08*/u64 afar;

	/* D-cache state */
/*0x10*/u64 dcache_data[4];	/* The actual data	*/
/*0x30*/u64 dcache_index;	/* D-cache index	*/
/*0x38*/u64 dcache_tag;		/* D-cache tag/valid	*/
/*0x40*/u64 dcache_utag;	/* D-cache microtag	*/
/*0x48*/u64 dcache_stag;	/* D-cache snooptag	*/

	/* I-cache state */
/*0x50*/u64 icache_data[8];	/* The actual insns + predecode	*/
/*0x90*/u64 icache_index;	/* I-cache index	*/
/*0x98*/u64 icache_tag;		/* I-cache phys tag	*/
/*0xa0*/u64 icache_utag;	/* I-cache microtag	*/
/*0xa8*/u64 icache_stag;	/* I-cache snooptag	*/
/*0xb0*/u64 icache_upper;	/* I-cache upper-tag	*/
/*0xb8*/u64 icache_lower;	/* I-cache lower-tag	*/

	/* E-cache state */
/*0xc0*/u64 ecache_data[4];	/* 32 bytes from staging registers */
/*0xe0*/u64 ecache_index;	/* E-cache index	*/
/*0xe8*/u64 ecache_tag;		/* E-cache tag/state	*/

/*0xf0*/u64 __pad[32 - 30];
};
#define CHAFSR_INVALID		((u64)-1L)

/* This table is ordered in priority of errors and matches the
 * AFAR overwrite policy as well.
 */

struct afsr_error_table {
	unsigned long mask;
	const char *name;
};

static const char CHAFSR_PERR_msg[] =
	"System interface protocol error";
static const char CHAFSR_IERR_msg[] =
	"Internal processor error";
static const char CHAFSR_ISAP_msg[] =
	"System request parity error on incoming addresss";
static const char CHAFSR_UCU_msg[] =
	"Uncorrectable E-cache ECC error for ifetch/data";
static const char CHAFSR_UCC_msg[] =
	"SW Correctable E-cache ECC error for ifetch/data";
static const char CHAFSR_UE_msg[] =
	"Uncorrectable system bus data ECC error for read";
static const char CHAFSR_EDU_msg[] =
	"Uncorrectable E-cache ECC error for stmerge/blkld";
static const char CHAFSR_EMU_msg[] =
	"Uncorrectable system bus MTAG error";
static const char CHAFSR_WDU_msg[] =
	"Uncorrectable E-cache ECC error for writeback";
static const char CHAFSR_CPU_msg[] =
	"Uncorrectable ECC error for copyout";
static const char CHAFSR_CE_msg[] =
	"HW corrected system bus data ECC error for read";
static const char CHAFSR_EDC_msg[] =
	"HW corrected E-cache ECC error for stmerge/blkld";
static const char CHAFSR_EMC_msg[] =
	"HW corrected system bus MTAG ECC error";
static const char CHAFSR_WDC_msg[] =
	"HW corrected E-cache ECC error for writeback";
static const char CHAFSR_CPC_msg[] =
	"HW corrected ECC error for copyout";
static const char CHAFSR_TO_msg[] =
	"Unmapped error from system bus";
static const char CHAFSR_BERR_msg[] =
	"Bus error response from system bus";
static const char CHAFSR_IVC_msg[] =
	"HW corrected system bus data ECC error for ivec read";
static const char CHAFSR_IVU_msg[] =
	"Uncorrectable system bus data ECC error for ivec read";
static struct afsr_error_table __cheetah_error_table[] = {
	{	CHAFSR_PERR,	CHAFSR_PERR_msg		},
	{	CHAFSR_IERR,	CHAFSR_IERR_msg		},
	{	CHAFSR_ISAP,	CHAFSR_ISAP_msg		},
	{	CHAFSR_UCU,	CHAFSR_UCU_msg		},
	{	CHAFSR_UCC,	CHAFSR_UCC_msg		},
	{	CHAFSR_UE,	CHAFSR_UE_msg		},
	{	CHAFSR_EDU,	CHAFSR_EDU_msg		},
	{	CHAFSR_EMU,	CHAFSR_EMU_msg		},
	{	CHAFSR_WDU,	CHAFSR_WDU_msg		},
	{	CHAFSR_CPU,	CHAFSR_CPU_msg		},
	{	CHAFSR_CE,	CHAFSR_CE_msg		},
	{	CHAFSR_EDC,	CHAFSR_EDC_msg		},
	{	CHAFSR_EMC,	CHAFSR_EMC_msg		},
	{	CHAFSR_WDC,	CHAFSR_WDC_msg		},
	{	CHAFSR_CPC,	CHAFSR_CPC_msg		},
	{	CHAFSR_TO,	CHAFSR_TO_msg		},
	{	CHAFSR_BERR,	CHAFSR_BERR_msg		},
	/* These two do not update the AFAR. */
	{	CHAFSR_IVC,	CHAFSR_IVC_msg		},
	{	CHAFSR_IVU,	CHAFSR_IVU_msg		},
	{	0,		NULL			},
};
static const char CHPAFSR_DTO_msg[] =
	"System bus unmapped error for prefetch/storequeue-read";
static const char CHPAFSR_DBERR_msg[] =
	"System bus error for prefetch/storequeue-read";
static const char CHPAFSR_THCE_msg[] =
	"Hardware corrected E-cache Tag ECC error";
static const char CHPAFSR_TSCE_msg[] =
	"SW handled correctable E-cache Tag ECC error";
static const char CHPAFSR_TUE_msg[] =
	"Uncorrectable E-cache Tag ECC error";
static const char CHPAFSR_DUE_msg[] =
	"System bus uncorrectable data ECC error due to prefetch/store-fill";
static struct afsr_error_table __cheetah_plus_error_table[] = {
	{	CHAFSR_PERR,	CHAFSR_PERR_msg		},
	{	CHAFSR_IERR,	CHAFSR_IERR_msg		},
	{	CHAFSR_ISAP,	CHAFSR_ISAP_msg		},
	{	CHAFSR_UCU,	CHAFSR_UCU_msg		},
	{	CHAFSR_UCC,	CHAFSR_UCC_msg		},
	{	CHAFSR_UE,	CHAFSR_UE_msg		},
	{	CHAFSR_EDU,	CHAFSR_EDU_msg		},
	{	CHAFSR_EMU,	CHAFSR_EMU_msg		},
	{	CHAFSR_WDU,	CHAFSR_WDU_msg		},
	{	CHAFSR_CPU,	CHAFSR_CPU_msg		},
	{	CHAFSR_CE,	CHAFSR_CE_msg		},
	{	CHAFSR_EDC,	CHAFSR_EDC_msg		},
	{	CHAFSR_EMC,	CHAFSR_EMC_msg		},
	{	CHAFSR_WDC,	CHAFSR_WDC_msg		},
	{	CHAFSR_CPC,	CHAFSR_CPC_msg		},
	{	CHAFSR_TO,	CHAFSR_TO_msg		},
	{	CHAFSR_BERR,	CHAFSR_BERR_msg		},
	{	CHPAFSR_DTO,	CHPAFSR_DTO_msg		},
	{	CHPAFSR_DBERR,	CHPAFSR_DBERR_msg	},
	{	CHPAFSR_THCE,	CHPAFSR_THCE_msg	},
	{	CHPAFSR_TSCE,	CHPAFSR_TSCE_msg	},
	{	CHPAFSR_TUE,	CHPAFSR_TUE_msg		},
	{	CHPAFSR_DUE,	CHPAFSR_DUE_msg		},
	/* These two do not update the AFAR. */
	{	CHAFSR_IVC,	CHAFSR_IVC_msg		},
	{	CHAFSR_IVU,	CHAFSR_IVU_msg		},
	{	0,		NULL			},
};
static const char JPAFSR_JETO_msg[] =
	"System interface protocol error, hw timeout caused";
static const char JPAFSR_SCE_msg[] =
	"Parity error on system snoop results";
static const char JPAFSR_JEIC_msg[] =
	"System interface protocol error, illegal command detected";
static const char JPAFSR_JEIT_msg[] =
	"System interface protocol error, illegal ADTYPE detected";
static const char JPAFSR_OM_msg[] =
	"Out of range memory error has occurred";
static const char JPAFSR_ETP_msg[] =
	"Parity error on L2 cache tag SRAM";
static const char JPAFSR_UMS_msg[] =
	"Error due to unsupported store";
static const char JPAFSR_RUE_msg[] =
	"Uncorrectable ECC error from remote cache/memory";
static const char JPAFSR_RCE_msg[] =
	"Correctable ECC error from remote cache/memory";
static const char JPAFSR_BP_msg[] =
	"JBUS parity error on returned read data";
static const char JPAFSR_WBP_msg[] =
	"JBUS parity error on data for writeback or block store";
static const char JPAFSR_FRC_msg[] =
	"Foreign read to DRAM incurring correctable ECC error";
static const char JPAFSR_FRU_msg[] =
	"Foreign read to DRAM incurring uncorrectable ECC error";
static struct afsr_error_table __jalapeno_error_table[] = {
	{	JPAFSR_JETO,	JPAFSR_JETO_msg		},
	{	JPAFSR_SCE,	JPAFSR_SCE_msg		},
	{	JPAFSR_JEIC,	JPAFSR_JEIC_msg		},
	{	JPAFSR_JEIT,	JPAFSR_JEIT_msg		},
	{	CHAFSR_PERR,	CHAFSR_PERR_msg		},
	{	CHAFSR_IERR,	CHAFSR_IERR_msg		},
	{	CHAFSR_ISAP,	CHAFSR_ISAP_msg		},
	{	CHAFSR_UCU,	CHAFSR_UCU_msg		},
	{	CHAFSR_UCC,	CHAFSR_UCC_msg		},
	{	CHAFSR_UE,	CHAFSR_UE_msg		},
	{	CHAFSR_EDU,	CHAFSR_EDU_msg		},
	{	JPAFSR_OM,	JPAFSR_OM_msg		},
	{	CHAFSR_WDU,	CHAFSR_WDU_msg		},
	{	CHAFSR_CPU,	CHAFSR_CPU_msg		},
	{	CHAFSR_CE,	CHAFSR_CE_msg		},
	{	CHAFSR_EDC,	CHAFSR_EDC_msg		},
	{	JPAFSR_ETP,	JPAFSR_ETP_msg		},
	{	CHAFSR_WDC,	CHAFSR_WDC_msg		},
	{	CHAFSR_CPC,	CHAFSR_CPC_msg		},
	{	CHAFSR_TO,	CHAFSR_TO_msg		},
	{	CHAFSR_BERR,	CHAFSR_BERR_msg		},
	{	JPAFSR_UMS,	JPAFSR_UMS_msg		},
	{	JPAFSR_RUE,	JPAFSR_RUE_msg		},
	{	JPAFSR_RCE,	JPAFSR_RCE_msg		},
	{	JPAFSR_BP,	JPAFSR_BP_msg		},
	{	JPAFSR_WBP,	JPAFSR_WBP_msg		},
	{	JPAFSR_FRC,	JPAFSR_FRC_msg		},
	{	JPAFSR_FRU,	JPAFSR_FRU_msg		},
	/* These two do not update the AFAR. */
	{	CHAFSR_IVU,	CHAFSR_IVU_msg		},
	{	0,		NULL			},
};
static struct afsr_error_table *cheetah_error_table;
static unsigned long cheetah_afsr_errors;

/* This is allocated at boot time based upon the largest hardware
 * cpu ID in the system.  We allocate two entries per cpu, one for
 * TL==0 logging and one for TL >= 1 logging.
 */
struct cheetah_err_info *cheetah_error_log;

static __inline__ struct cheetah_err_info *cheetah_get_error_log(unsigned long afsr)
{
	struct cheetah_err_info *p;
	int cpu = smp_processor_id();

	if (!cheetah_error_log)
		return NULL;

	p = cheetah_error_log + (cpu * 2);
	if ((afsr & CHAFSR_TL1) != 0UL)
		p++;

	return p;
}

extern unsigned int tl0_icpe[], tl1_icpe[];
extern unsigned int tl0_dcpe[], tl1_dcpe[];
extern unsigned int tl0_fecc[], tl1_fecc[];
extern unsigned int tl0_cee[], tl1_cee[];
extern unsigned int tl0_iae[], tl1_iae[];
extern unsigned int tl0_dae[], tl1_dae[];
extern unsigned int cheetah_plus_icpe_trap_vector[], cheetah_plus_icpe_trap_vector_tl1[];
extern unsigned int cheetah_plus_dcpe_trap_vector[], cheetah_plus_dcpe_trap_vector_tl1[];
extern unsigned int cheetah_fecc_trap_vector[], cheetah_fecc_trap_vector_tl1[];
extern unsigned int cheetah_cee_trap_vector[], cheetah_cee_trap_vector_tl1[];
extern unsigned int cheetah_deferred_trap_vector[], cheetah_deferred_trap_vector_tl1[];

void __init cheetah_ecache_flush_init(void)
{
	unsigned long largest_size, smallest_linesize, order, ver;
	int node, i, instance;

	/* Scan all cpu device tree nodes, note two values:
	 * 1) largest E-cache size
	 * 2) smallest E-cache line size
	 */
	largest_size = 0UL;
	smallest_linesize = ~0UL;

	instance = 0;
	while (!cpu_find_by_instance(instance, &node, NULL)) {
		unsigned long val;

		val = prom_getintdefault(node, "ecache-size",
					 (2 * 1024 * 1024));
		if (val > largest_size)
			largest_size = val;
		val = prom_getintdefault(node, "ecache-line-size", 64);
		if (val < smallest_linesize)
			smallest_linesize = val;
		instance++;
	}

	if (largest_size == 0UL || smallest_linesize == ~0UL) {
		prom_printf("cheetah_ecache_flush_init: Cannot probe cpu E-cache "
			    "parameters.\n");
		prom_halt();
	}

	ecache_flush_size = (2 * largest_size);
	ecache_flush_linesize = smallest_linesize;

	ecache_flush_physbase = find_ecache_flush_span(ecache_flush_size);

	if (ecache_flush_physbase == ~0UL) {
		prom_printf("cheetah_ecache_flush_init: Cannot find %d byte "
			    "contiguous physical memory.\n",
			    ecache_flush_size);
		prom_halt();
	}

	/* Now allocate error trap reporting scoreboard. */
	node = NR_CPUS * (2 * sizeof(struct cheetah_err_info));
	for (order = 0; order < MAX_ORDER; order++) {
		if ((PAGE_SIZE << order) >= node)
			break;
	}
	cheetah_error_log = (struct cheetah_err_info *)
		__get_free_pages(GFP_KERNEL, order);
	if (!cheetah_error_log) {
		prom_printf("cheetah_ecache_flush_init: Failed to allocate "
			    "error logging scoreboard (%d bytes).\n", node);
		prom_halt();
	}
	memset(cheetah_error_log, 0, PAGE_SIZE << order);

	/* Mark all AFSRs as invalid so that the trap handler will
	 * log new new information there.
	 */
	for (i = 0; i < 2 * NR_CPUS; i++)
		cheetah_error_log[i].afsr = CHAFSR_INVALID;

	__asm__ ("rdpr %%ver, %0" : "=r" (ver));
	if ((ver >> 32) == 0x003e0016) {
		cheetah_error_table = &__jalapeno_error_table[0];
		cheetah_afsr_errors = JPAFSR_ERRORS;
	} else if ((ver >> 32) == 0x003e0015) {
		cheetah_error_table = &__cheetah_plus_error_table[0];
		cheetah_afsr_errors = CHPAFSR_ERRORS;
	} else {
		cheetah_error_table = &__cheetah_error_table[0];
		cheetah_afsr_errors = CHAFSR_ERRORS;
	}

	/* Now patch trap tables. */
	memcpy(tl0_fecc, cheetah_fecc_trap_vector, (8 * 4));
	memcpy(tl1_fecc, cheetah_fecc_trap_vector_tl1, (8 * 4));
	memcpy(tl0_cee, cheetah_cee_trap_vector, (8 * 4));
	memcpy(tl1_cee, cheetah_cee_trap_vector_tl1, (8 * 4));
	memcpy(tl0_iae, cheetah_deferred_trap_vector, (8 * 4));
	memcpy(tl1_iae, cheetah_deferred_trap_vector_tl1, (8 * 4));
	memcpy(tl0_dae, cheetah_deferred_trap_vector, (8 * 4));
	memcpy(tl1_dae, cheetah_deferred_trap_vector_tl1, (8 * 4));
	if (tlb_type == cheetah_plus) {
		memcpy(tl0_dcpe, cheetah_plus_dcpe_trap_vector, (8 * 4));
		memcpy(tl1_dcpe, cheetah_plus_dcpe_trap_vector_tl1, (8 * 4));
		memcpy(tl0_icpe, cheetah_plus_icpe_trap_vector, (8 * 4));
		memcpy(tl1_icpe, cheetah_plus_icpe_trap_vector_tl1, (8 * 4));
	}
	flushi(PAGE_OFFSET);
}

static void cheetah_flush_ecache(void)
{
	unsigned long flush_base = ecache_flush_physbase;
	unsigned long flush_linesize = ecache_flush_linesize;
	unsigned long flush_size = ecache_flush_size;

	__asm__ __volatile__("1: subcc	%0, %4, %0\n\t"
			     "   bne,pt	%%xcc, 1b\n\t"
			     "    ldxa	[%2 + %0] %3, %%g0\n\t"
			     : "=&r" (flush_size)
			     : "0" (flush_size), "r" (flush_base),
			       "i" (ASI_PHYS_USE_EC), "r" (flush_linesize));
}

static void cheetah_flush_ecache_line(unsigned long physaddr)
{
	unsigned long alias;

	physaddr &= ~(8UL - 1UL);
	physaddr = (ecache_flush_physbase +
		    (physaddr & ((ecache_flush_size>>1UL) - 1UL)));
	alias = physaddr + (ecache_flush_size >> 1UL);
	__asm__ __volatile__("ldxa [%0] %2, %%g0\n\t"
			     "ldxa [%1] %2, %%g0\n\t"
			     "membar #Sync"
			     : /* no outputs */
			     : "r" (physaddr), "r" (alias),
			       "i" (ASI_PHYS_USE_EC));
}

/* Unfortunately, the diagnostic access to the I-cache tags we need to
 * use to clear the thing interferes with I-cache coherency transactions.
 *
 * So we must only flush the I-cache when it is disabled.
 */
static void __cheetah_flush_icache(void)
{
	unsigned int icache_size, icache_line_size;
	unsigned long addr;

	icache_size = local_cpu_data().icache_size;
	icache_line_size = local_cpu_data().icache_line_size;

	/* Clear the valid bits in all the tags. */
	for (addr = 0; addr < icache_size; addr += icache_line_size) {
		__asm__ __volatile__("stxa %%g0, [%0] %1\n\t"
				     "membar #Sync"
				     : /* no outputs */
				     : "r" (addr | (2 << 3)),
				       "i" (ASI_IC_TAG));
	}
}

static void cheetah_flush_icache(void)
{
	unsigned long dcu_save;

	/* Save current DCU, disable I-cache. */
	__asm__ __volatile__("ldxa [%%g0] %1, %0\n\t"
			     "or %0, %2, %%g1\n\t"
			     "stxa %%g1, [%%g0] %1\n\t"
			     "membar #Sync"
			     : "=r" (dcu_save)
			     : "i" (ASI_DCU_CONTROL_REG), "i" (DCU_IC)
			     : "g1");

	__cheetah_flush_icache();

	/* Restore DCU register */
	__asm__ __volatile__("stxa %0, [%%g0] %1\n\t"
			     "membar #Sync"
			     : /* no outputs */
			     : "r" (dcu_save), "i" (ASI_DCU_CONTROL_REG));
}

static void cheetah_flush_dcache(void)
{
	unsigned int dcache_size, dcache_line_size;
	unsigned long addr;

	dcache_size = local_cpu_data().dcache_size;
	dcache_line_size = local_cpu_data().dcache_line_size;

	for (addr = 0; addr < dcache_size; addr += dcache_line_size) {
		__asm__ __volatile__("stxa %%g0, [%0] %1\n\t"
				     "membar #Sync"
				     : /* no outputs */
				     : "r" (addr), "i" (ASI_DCACHE_TAG));
	}
}

/* In order to make the even parity correct we must do two things.
 * First, we clear DC_data_parity and set DC_utag to an appropriate value.
 * Next, we clear out all 32-bytes of data for that line.  Data of
 * all-zero + tag parity value of zero == correct parity.
 */
static void cheetah_plus_zap_dcache_parity(void)
{
	unsigned int dcache_size, dcache_line_size;
	unsigned long addr;

	dcache_size = local_cpu_data().dcache_size;
	dcache_line_size = local_cpu_data().dcache_line_size;

	for (addr = 0; addr < dcache_size; addr += dcache_line_size) {
		unsigned long tag = (addr >> 14);
		unsigned long line;

		__asm__ __volatile__("membar	#Sync\n\t"
				     "stxa	%0, [%1] %2\n\t"
				     "membar	#Sync"
				     : /* no outputs */
				     : "r" (tag), "r" (addr),
				       "i" (ASI_DCACHE_UTAG));
		for (line = addr; line < addr + dcache_line_size; line += 8)
			__asm__ __volatile__("membar	#Sync\n\t"
					     "stxa	%%g0, [%0] %1\n\t"
					     "membar	#Sync"
					     : /* no outputs */
					     : "r" (line),
					       "i" (ASI_DCACHE_DATA));
	}
}

/* Conversion tables used to frob Cheetah AFSR syndrome values into
 * something palatable to the memory controller driver get_unumber
 * routine.
 */
#define MT0	137
#define MT1	138
#define MT2	139
#define NONE	254
#define MTC0	140
#define MTC1	141
#define MTC2	142
#define MTC3	143
#define C0	128
#define C1	129
#define C2	130
#define C3	131
#define C4	132
#define C5	133
#define C6	134
#define C7	135
#define C8	136
#define M2	144
#define M3	145
#define M4	146
#define M	147
static unsigned char cheetah_ecc_syntab[] = {
/*00*/NONE, C0, C1, M2, C2, M2, M3, 47, C3, M2, M2, 53, M2, 41, 29, M,
/*01*/C4, M, M, 50, M2, 38, 25, M2, M2, 33, 24, M2, 11, M, M2, 16,
/*02*/C5, M, M, 46, M2, 37, 19, M2, M, 31, 32, M, 7, M2, M2, 10,
/*03*/M2, 40, 13, M2, 59, M, M2, 66, M, M2, M2, 0, M2, 67, 71, M,
/*04*/C6, M, M, 43, M, 36, 18, M, M2, 49, 15, M, 63, M2, M2, 6,
/*05*/M2, 44, 28, M2, M, M2, M2, 52, 68, M2, M2, 62, M2, M3, M3, M4,
/*06*/M2, 26, 106, M2, 64, M, M2, 2, 120, M, M2, M3, M, M3, M3, M4,
/*07*/116, M2, M2, M3, M2, M3, M, M4, M2, 58, 54, M2, M, M4, M4, M3,
/*08*/C7, M2, M, 42, M, 35, 17, M2, M, 45, 14, M2, 21, M2, M2, 5,
/*09*/M, 27, M, M, 99, M, M, 3, 114, M2, M2, 20, M2, M3, M3, M,
/*0a*/M2, 23, 113, M2, 112, M2, M, 51, 95, M, M2, M3, M2, M3, M3, M2,
/*0b*/103, M, M2, M3, M2, M3, M3, M4, M2, 48, M, M, 73, M2, M, M3,
/*0c*/M2, 22, 110, M2, 109, M2, M, 9, 108, M2, M, M3, M2, M3, M3, M,
/*0d*/102, M2, M, M, M2, M3, M3, M, M2, M3, M3, M2, M, M4, M, M3,
/*0e*/98, M, M2, M3, M2, M, M3, M4, M2, M3, M3, M4, M3, M, M, M,
/*0f*/M2, M3, M3, M, M3, M, M, M, 56, M4, M, M3, M4, M, M, M,
/*10*/C8, M, M2, 39, M, 34, 105, M2, M, 30, 104, M, 101, M, M, 4,
/*11*/M, M, 100, M, 83, M, M2, 12, 87, M, M, 57, M2, M, M3, M,
/*12*/M2, 97, 82, M2, 78, M2, M2, 1, 96, M, M, M, M, M, M3, M2,
/*13*/94, M, M2, M3, M2, M, M3, M, M2, M, 79, M, 69, M, M4, M,
/*14*/M2, 93, 92, M, 91, M, M2, 8, 90, M2, M2, M, M, M, M, M4,
/*15*/89, M, M, M3, M2, M3, M3, M, M, M, M3, M2, M3, M2, M, M3,
/*16*/86, M, M2, M3, M2, M, M3, M, M2, M, M3, M, M3, M, M, M3,
/*17*/M, M, M3, M2, M3, M2, M4, M, 60, M, M2, M3, M4, M, M, M2,
/*18*/M2, 88, 85, M2, 84, M, M2, 55, 81, M2, M2, M3, M2, M3, M3, M4,
/*19*/77, M, M, M, M2, M3, M, M, M2, M3, M3, M4, M3, M2, M, M,
/*1a*/74, M, M2, M3, M, M, M3, M, M, M, M3, M, M3, M, M4, M3,
/*1b*/M2, 70, 107, M4, 65, M2, M2, M, 127, M, M, M, M2, M3, M3, M,
/*1c*/80, M2, M2, 72, M, 119, 118, M, M2, 126, 76, M, 125, M, M4, M3,
/*1d*/M2, 115, 124, M, 75, M, M, M3, 61, M, M4, M, M4, M, M, M,
/*1e*/M, 123, 122, M4, 121, M4, M, M3, 117, M2, M2, M3, M4, M3, M, M,
/*1f*/111, M, M, M, M4, M3, M3, M, M, M, M3, M, M3, M2, M, M
};
static unsigned char cheetah_mtag_syntab[] = {
       NONE, MTC0,
       MTC1, NONE,
       MTC2, NONE,
       NONE, MT0,
       MTC3, NONE,
       NONE, MT1,
       NONE, MT2,
       NONE, NONE
};

/* Return the highest priority error conditon mentioned. */
static __inline__ unsigned long cheetah_get_hipri(unsigned long afsr)
{
	unsigned long tmp = 0;
	int i;

	for (i = 0; cheetah_error_table[i].mask; i++) {
		if ((tmp = (afsr & cheetah_error_table[i].mask)) != 0UL)
			return tmp;
	}
	return tmp;
}

static const char *cheetah_get_string(unsigned long bit)
{
	int i;

	for (i = 0; cheetah_error_table[i].mask; i++) {
		if ((bit & cheetah_error_table[i].mask) != 0UL)
			return cheetah_error_table[i].name;
	}
	return "???";
}

extern int chmc_getunumber(int, unsigned long, char *, int);

static void cheetah_log_errors(struct pt_regs *regs, struct cheetah_err_info *info,
			       unsigned long afsr, unsigned long afar, int recoverable)
{
	unsigned long hipri;
	char unum[256];

	printk("%s" "ERROR(%d): Cheetah error trap taken afsr[%016lx] afar[%016lx] TL1(%d)\n",
	       (recoverable ? KERN_WARNING : KERN_CRIT), smp_processor_id(),
	       afsr, afar,
	       (afsr & CHAFSR_TL1) ? 1 : 0);
	printk("%s" "ERROR(%d): TPC[%016lx] TNPC[%016lx] TSTATE[%016lx]\n",
	       (recoverable ? KERN_WARNING : KERN_CRIT), smp_processor_id(),
	       regs->tpc, regs->tnpc, regs->tstate);
	printk("%s" "ERROR(%d): M_SYND(%lx),  E_SYND(%lx)%s%s\n",
	       (recoverable ? KERN_WARNING : KERN_CRIT), smp_processor_id(),
	       (afsr & CHAFSR_M_SYNDROME) >> CHAFSR_M_SYNDROME_SHIFT,
	       (afsr & CHAFSR_E_SYNDROME) >> CHAFSR_E_SYNDROME_SHIFT,
	       (afsr & CHAFSR_ME) ? ", Multiple Errors" : "",
	       (afsr & CHAFSR_PRIV) ? ", Privileged" : "");
	hipri = cheetah_get_hipri(afsr);
	printk("%s" "ERROR(%d): Highest priority error (%016lx) \"%s\"\n",
	       (recoverable ? KERN_WARNING : KERN_CRIT), smp_processor_id(),
	       hipri, cheetah_get_string(hipri));

	/* Try to get unumber if relevant. */
#define ESYND_ERRORS	(CHAFSR_IVC | CHAFSR_IVU | \
			 CHAFSR_CPC | CHAFSR_CPU | \
			 CHAFSR_UE  | CHAFSR_CE  | \
			 CHAFSR_EDC | CHAFSR_EDU  | \
			 CHAFSR_UCC | CHAFSR_UCU  | \
			 CHAFSR_WDU | CHAFSR_WDC)
#define MSYND_ERRORS	(CHAFSR_EMC | CHAFSR_EMU)
	if (afsr & ESYND_ERRORS) {
		int syndrome;
		int ret;

		syndrome = (afsr & CHAFSR_E_SYNDROME) >> CHAFSR_E_SYNDROME_SHIFT;
		syndrome = cheetah_ecc_syntab[syndrome];
		ret = chmc_getunumber(syndrome, afar, unum, sizeof(unum));
		if (ret != -1)
			printk("%s" "ERROR(%d): AFAR E-syndrome [%s]\n",
			       (recoverable ? KERN_WARNING : KERN_CRIT),
			       smp_processor_id(), unum);
	} else if (afsr & MSYND_ERRORS) {
		int syndrome;
		int ret;

		syndrome = (afsr & CHAFSR_M_SYNDROME) >> CHAFSR_M_SYNDROME_SHIFT;
		syndrome = cheetah_mtag_syntab[syndrome];
		ret = chmc_getunumber(syndrome, afar, unum, sizeof(unum));
		if (ret != -1)
			printk("%s" "ERROR(%d): AFAR M-syndrome [%s]\n",
			       (recoverable ? KERN_WARNING : KERN_CRIT),
			       smp_processor_id(), unum);
	}

	/* Now dump the cache snapshots. */
	printk("%s" "ERROR(%d): D-cache idx[%x] tag[%016lx] utag[%016lx] stag[%016lx]\n",
	       (recoverable ? KERN_WARNING : KERN_CRIT), smp_processor_id(),
	       (int) info->dcache_index,
	       info->dcache_tag,
	       info->dcache_utag,
	       info->dcache_stag);
	printk("%s" "ERROR(%d): D-cache data0[%016lx] data1[%016lx] data2[%016lx] data3[%016lx]\n",
	       (recoverable ? KERN_WARNING : KERN_CRIT), smp_processor_id(),
	       info->dcache_data[0],
	       info->dcache_data[1],
	       info->dcache_data[2],
	       info->dcache_data[3]);
	printk("%s" "ERROR(%d): I-cache idx[%x] tag[%016lx] utag[%016lx] stag[%016lx] "
	       "u[%016lx] l[%016lx]\n",
	       (recoverable ? KERN_WARNING : KERN_CRIT), smp_processor_id(),
	       (int) info->icache_index,
	       info->icache_tag,
	       info->icache_utag,
	       info->icache_stag,
	       info->icache_upper,
	       info->icache_lower);
	printk("%s" "ERROR(%d): I-cache INSN0[%016lx] INSN1[%016lx] INSN2[%016lx] INSN3[%016lx]\n",
	       (recoverable ? KERN_WARNING : KERN_CRIT), smp_processor_id(),
	       info->icache_data[0],
	       info->icache_data[1],
	       info->icache_data[2],
	       info->icache_data[3]);
	printk("%s" "ERROR(%d): I-cache INSN4[%016lx] INSN5[%016lx] INSN6[%016lx] INSN7[%016lx]\n",
	       (recoverable ? KERN_WARNING : KERN_CRIT), smp_processor_id(),
	       info->icache_data[4],
	       info->icache_data[5],
	       info->icache_data[6],
	       info->icache_data[7]);
	printk("%s" "ERROR(%d): E-cache idx[%x] tag[%016lx]\n",
	       (recoverable ? KERN_WARNING : KERN_CRIT), smp_processor_id(),
	       (int) info->ecache_index, info->ecache_tag);
	printk("%s" "ERROR(%d): E-cache data0[%016lx] data1[%016lx] data2[%016lx] data3[%016lx]\n",
	       (recoverable ? KERN_WARNING : KERN_CRIT), smp_processor_id(),
	       info->ecache_data[0],
	       info->ecache_data[1],
	       info->ecache_data[2],
	       info->ecache_data[3]);

	afsr = (afsr & ~hipri) & cheetah_afsr_errors;
	while (afsr != 0UL) {
		unsigned long bit = cheetah_get_hipri(afsr);

		printk("%s" "ERROR: Multiple-error (%016lx) \"%s\"\n",
		       (recoverable ? KERN_WARNING : KERN_CRIT),
		       bit, cheetah_get_string(bit));

		afsr &= ~bit;
	}

	if (!recoverable)
		printk(KERN_CRIT "ERROR: This condition is not recoverable.\n");
}

static int cheetah_recheck_errors(struct cheetah_err_info *logp)
{
	unsigned long afsr, afar;
	int ret = 0;

	__asm__ __volatile__("ldxa [%%g0] %1, %0\n\t"
			     : "=r" (afsr)
			     : "i" (ASI_AFSR));
	if ((afsr & cheetah_afsr_errors) != 0) {
		if (logp != NULL) {
			__asm__ __volatile__("ldxa [%%g0] %1, %0\n\t"
					     : "=r" (afar)
					     : "i" (ASI_AFAR));
			logp->afsr = afsr;
			logp->afar = afar;
		}
		ret = 1;
	}
	__asm__ __volatile__("stxa %0, [%%g0] %1\n\t"
			     "membar #Sync\n\t"
			     : : "r" (afsr), "i" (ASI_AFSR));

	return ret;
}

void cheetah_fecc_handler(struct pt_regs *regs, unsigned long afsr, unsigned long afar)
{
	struct cheetah_err_info local_snapshot, *p;
	int recoverable;

	/* Flush E-cache */
	cheetah_flush_ecache();

	p = cheetah_get_error_log(afsr);
	if (!p) {
		prom_printf("ERROR: Early Fast-ECC error afsr[%016lx] afar[%016lx]\n",
			    afsr, afar);
		prom_printf("ERROR: CPU(%d) TPC[%016lx] TNPC[%016lx] TSTATE[%016lx]\n",
			    smp_processor_id(), regs->tpc, regs->tnpc, regs->tstate);
		prom_halt();
	}

	/* Grab snapshot of logged error. */
	memcpy(&local_snapshot, p, sizeof(local_snapshot));

	/* If the current trap snapshot does not match what the
	 * trap handler passed along into our args, big trouble.
	 * In such a case, mark the local copy as invalid.
	 *
	 * Else, it matches and we mark the afsr in the non-local
	 * copy as invalid so we may log new error traps there.
	 */
	if (p->afsr != afsr || p->afar != afar)
		local_snapshot.afsr = CHAFSR_INVALID;
	else
		p->afsr = CHAFSR_INVALID;

	cheetah_flush_icache();
	cheetah_flush_dcache();

	/* Re-enable I-cache/D-cache */
	__asm__ __volatile__("ldxa [%%g0] %0, %%g1\n\t"
			     "or %%g1, %1, %%g1\n\t"
			     "stxa %%g1, [%%g0] %0\n\t"
			     "membar #Sync"
			     : /* no outputs */
			     : "i" (ASI_DCU_CONTROL_REG),
			       "i" (DCU_DC | DCU_IC)
			     : "g1");

	/* Re-enable error reporting */
	__asm__ __volatile__("ldxa [%%g0] %0, %%g1\n\t"
			     "or %%g1, %1, %%g1\n\t"
			     "stxa %%g1, [%%g0] %0\n\t"
			     "membar #Sync"
			     : /* no outputs */
			     : "i" (ASI_ESTATE_ERROR_EN),
			       "i" (ESTATE_ERROR_NCEEN | ESTATE_ERROR_CEEN)
			     : "g1");

	/* Decide if we can continue after handling this trap and
	 * logging the error.
	 */
	recoverable = 1;
	if (afsr & (CHAFSR_PERR | CHAFSR_IERR | CHAFSR_ISAP))
		recoverable = 0;

	/* Re-check AFSR/AFAR.  What we are looking for here is whether a new
	 * error was logged while we had error reporting traps disabled.
	 */
	if (cheetah_recheck_errors(&local_snapshot)) {
		unsigned long new_afsr = local_snapshot.afsr;

		/* If we got a new asynchronous error, die... */
		if (new_afsr & (CHAFSR_EMU | CHAFSR_EDU |
				CHAFSR_WDU | CHAFSR_CPU |
				CHAFSR_IVU | CHAFSR_UE |
				CHAFSR_BERR | CHAFSR_TO))
			recoverable = 0;
	}

	/* Log errors. */
	cheetah_log_errors(regs, &local_snapshot, afsr, afar, recoverable);

	if (!recoverable)
		panic("Irrecoverable Fast-ECC error trap.\n");

	/* Flush E-cache to kick the error trap handlers out. */
	cheetah_flush_ecache();
}

/* Try to fix a correctable error by pushing the line out from
 * the E-cache.  Recheck error reporting registers to see if the
 * problem is intermittent.
 */
static int cheetah_fix_ce(unsigned long physaddr)
{
	unsigned long orig_estate;
	unsigned long alias1, alias2;
	int ret;

	/* Make sure correctable error traps are disabled. */
	__asm__ __volatile__("ldxa	[%%g0] %2, %0\n\t"
			     "andn	%0, %1, %%g1\n\t"
			     "stxa	%%g1, [%%g0] %2\n\t"
			     "membar	#Sync"
			     : "=&r" (orig_estate)
			     : "i" (ESTATE_ERROR_CEEN),
			       "i" (ASI_ESTATE_ERROR_EN)
			     : "g1");

	/* We calculate alias addresses that will force the
	 * cache line in question out of the E-cache.  Then
	 * we bring it back in with an atomic instruction so
	 * that we get it in some modified/exclusive state,
	 * then we displace it again to try and get proper ECC
	 * pushed back into the system.
	 */
	physaddr &= ~(8UL - 1UL);
	alias1 = (ecache_flush_physbase +
		  (physaddr & ((ecache_flush_size >> 1) - 1)));
	alias2 = alias1 + (ecache_flush_size >> 1);
	__asm__ __volatile__("ldxa	[%0] %3, %%g0\n\t"
			     "ldxa	[%1] %3, %%g0\n\t"
			     "casxa	[%2] %3, %%g0, %%g0\n\t"
			     "membar	#StoreLoad | #StoreStore\n\t"
			     "ldxa	[%0] %3, %%g0\n\t"
			     "ldxa	[%1] %3, %%g0\n\t"
			     "membar	#Sync"
			     : /* no outputs */
			     : "r" (alias1), "r" (alias2),
			       "r" (physaddr), "i" (ASI_PHYS_USE_EC));

	/* Did that trigger another error? */
	if (cheetah_recheck_errors(NULL)) {
		/* Try one more time. */
		__asm__ __volatile__("ldxa [%0] %1, %%g0\n\t"
				     "membar #Sync"
				     : : "r" (physaddr), "i" (ASI_PHYS_USE_EC));
		if (cheetah_recheck_errors(NULL))
			ret = 2;
		else
			ret = 1;
	} else {
		/* No new error, intermittent problem. */
		ret = 0;
	}

	/* Restore error enables. */
	__asm__ __volatile__("stxa	%0, [%%g0] %1\n\t"
			     "membar	#Sync"
			     : : "r" (orig_estate), "i" (ASI_ESTATE_ERROR_EN));

	return ret;
}

/* Return non-zero if PADDR is a valid physical memory address. */
static int cheetah_check_main_memory(unsigned long paddr)
{
	unsigned long vaddr = PAGE_OFFSET + paddr;

	if (vaddr > (unsigned long) high_memory)
		return 0;

	return kern_addr_valid(vaddr);
}

void cheetah_cee_handler(struct pt_regs *regs, unsigned long afsr, unsigned long afar)
{
	struct cheetah_err_info local_snapshot, *p;
	int recoverable, is_memory;

	p = cheetah_get_error_log(afsr);
	if (!p) {
		prom_printf("ERROR: Early CEE error afsr[%016lx] afar[%016lx]\n",
			    afsr, afar);
		prom_printf("ERROR: CPU(%d) TPC[%016lx] TNPC[%016lx] TSTATE[%016lx]\n",
			    smp_processor_id(), regs->tpc, regs->tnpc, regs->tstate);
		prom_halt();
	}

	/* Grab snapshot of logged error. */
	memcpy(&local_snapshot, p, sizeof(local_snapshot));

	/* If the current trap snapshot does not match what the
	 * trap handler passed along into our args, big trouble.
	 * In such a case, mark the local copy as invalid.
	 *
	 * Else, it matches and we mark the afsr in the non-local
	 * copy as invalid so we may log new error traps there.
	 */
	if (p->afsr != afsr || p->afar != afar)
		local_snapshot.afsr = CHAFSR_INVALID;
	else
		p->afsr = CHAFSR_INVALID;

	is_memory = cheetah_check_main_memory(afar);

	if (is_memory && (afsr & CHAFSR_CE) != 0UL) {
		/* XXX Might want to log the results of this operation
		 * XXX somewhere... -DaveM
		 */
		cheetah_fix_ce(afar);
	}

	{
		int flush_all, flush_line;

		flush_all = flush_line = 0;
		if ((afsr & CHAFSR_EDC) != 0UL) {
			if ((afsr & cheetah_afsr_errors) == CHAFSR_EDC)
				flush_line = 1;
			else
				flush_all = 1;
		} else if ((afsr & CHAFSR_CPC) != 0UL) {
			if ((afsr & cheetah_afsr_errors) == CHAFSR_CPC)
				flush_line = 1;
			else
				flush_all = 1;
		}

		/* Trap handler only disabled I-cache, flush it. */
		cheetah_flush_icache();

		/* Re-enable I-cache */
		__asm__ __volatile__("ldxa [%%g0] %0, %%g1\n\t"
				     "or %%g1, %1, %%g1\n\t"
				     "stxa %%g1, [%%g0] %0\n\t"
				     "membar #Sync"
				     : /* no outputs */
				     : "i" (ASI_DCU_CONTROL_REG),
				     "i" (DCU_IC)
				     : "g1");

		if (flush_all)
			cheetah_flush_ecache();
		else if (flush_line)
			cheetah_flush_ecache_line(afar);
	}

	/* Re-enable error reporting */
	__asm__ __volatile__("ldxa [%%g0] %0, %%g1\n\t"
			     "or %%g1, %1, %%g1\n\t"
			     "stxa %%g1, [%%g0] %0\n\t"
			     "membar #Sync"
			     : /* no outputs */
			     : "i" (ASI_ESTATE_ERROR_EN),
			       "i" (ESTATE_ERROR_CEEN)
			     : "g1");

	/* Decide if we can continue after handling this trap and
	 * logging the error.
	 */
	recoverable = 1;
	if (afsr & (CHAFSR_PERR | CHAFSR_IERR | CHAFSR_ISAP))
		recoverable = 0;

	/* Re-check AFSR/AFAR */
	(void) cheetah_recheck_errors(&local_snapshot);

	/* Log errors. */
	cheetah_log_errors(regs, &local_snapshot, afsr, afar, recoverable);

	if (!recoverable)
		panic("Irrecoverable Correctable-ECC error trap.\n");
}

void cheetah_deferred_handler(struct pt_regs *regs, unsigned long afsr, unsigned long afar)
{
	struct cheetah_err_info local_snapshot, *p;
	int recoverable, is_memory;

#ifdef CONFIG_PCI
	/* Check for the special PCI poke sequence. */
	if (pci_poke_in_progress && pci_poke_cpu == smp_processor_id()) {
		cheetah_flush_icache();
		cheetah_flush_dcache();

		/* Re-enable I-cache/D-cache */
		__asm__ __volatile__("ldxa [%%g0] %0, %%g1\n\t"
				     "or %%g1, %1, %%g1\n\t"
				     "stxa %%g1, [%%g0] %0\n\t"
				     "membar #Sync"
				     : /* no outputs */
				     : "i" (ASI_DCU_CONTROL_REG),
				       "i" (DCU_DC | DCU_IC)
				     : "g1");

		/* Re-enable error reporting */
		__asm__ __volatile__("ldxa [%%g0] %0, %%g1\n\t"
				     "or %%g1, %1, %%g1\n\t"
				     "stxa %%g1, [%%g0] %0\n\t"
				     "membar #Sync"
				     : /* no outputs */
				     : "i" (ASI_ESTATE_ERROR_EN),
				       "i" (ESTATE_ERROR_NCEEN | ESTATE_ERROR_CEEN)
				     : "g1");

		(void) cheetah_recheck_errors(NULL);

		pci_poke_faulted = 1;
		regs->tpc += 4;
		regs->tnpc = regs->tpc + 4;
		return;
	}
#endif

	p = cheetah_get_error_log(afsr);
	if (!p) {
		prom_printf("ERROR: Early deferred error afsr[%016lx] afar[%016lx]\n",
			    afsr, afar);
		prom_printf("ERROR: CPU(%d) TPC[%016lx] TNPC[%016lx] TSTATE[%016lx]\n",
			    smp_processor_id(), regs->tpc, regs->tnpc, regs->tstate);
		prom_halt();
	}

	/* Grab snapshot of logged error. */
	memcpy(&local_snapshot, p, sizeof(local_snapshot));

	/* If the current trap snapshot does not match what the
	 * trap handler passed along into our args, big trouble.
	 * In such a case, mark the local copy as invalid.
	 *
	 * Else, it matches and we mark the afsr in the non-local
	 * copy as invalid so we may log new error traps there.
	 */
	if (p->afsr != afsr || p->afar != afar)
		local_snapshot.afsr = CHAFSR_INVALID;
	else
		p->afsr = CHAFSR_INVALID;

	is_memory = cheetah_check_main_memory(afar);

	{
		int flush_all, flush_line;

		flush_all = flush_line = 0;
		if ((afsr & CHAFSR_EDU) != 0UL) {
			if ((afsr & cheetah_afsr_errors) == CHAFSR_EDU)
				flush_line = 1;
			else
				flush_all = 1;
		} else if ((afsr & CHAFSR_BERR) != 0UL) {
			if ((afsr & cheetah_afsr_errors) == CHAFSR_BERR)
				flush_line = 1;
			else
				flush_all = 1;
		}

		cheetah_flush_icache();
		cheetah_flush_dcache();

		/* Re-enable I/D caches */
		__asm__ __volatile__("ldxa [%%g0] %0, %%g1\n\t"
				     "or %%g1, %1, %%g1\n\t"
				     "stxa %%g1, [%%g0] %0\n\t"
				     "membar #Sync"
				     : /* no outputs */
				     : "i" (ASI_DCU_CONTROL_REG),
				     "i" (DCU_IC | DCU_DC)
				     : "g1");

		if (flush_all)
			cheetah_flush_ecache();
		else if (flush_line)
			cheetah_flush_ecache_line(afar);
	}

	/* Re-enable error reporting */
	__asm__ __volatile__("ldxa [%%g0] %0, %%g1\n\t"
			     "or %%g1, %1, %%g1\n\t"
			     "stxa %%g1, [%%g0] %0\n\t"
			     "membar #Sync"
			     : /* no outputs */
			     : "i" (ASI_ESTATE_ERROR_EN),
			     "i" (ESTATE_ERROR_NCEEN | ESTATE_ERROR_CEEN)
			     : "g1");

	/* Decide if we can continue after handling this trap and
	 * logging the error.
	 */
	recoverable = 1;
	if (afsr & (CHAFSR_PERR | CHAFSR_IERR | CHAFSR_ISAP))
		recoverable = 0;

	/* Re-check AFSR/AFAR.  What we are looking for here is whether a new
	 * error was logged while we had error reporting traps disabled.
	 */
	if (cheetah_recheck_errors(&local_snapshot)) {
		unsigned long new_afsr = local_snapshot.afsr;

		/* If we got a new asynchronous error, die... */
		if (new_afsr & (CHAFSR_EMU | CHAFSR_EDU |
				CHAFSR_WDU | CHAFSR_CPU |
				CHAFSR_IVU | CHAFSR_UE |
				CHAFSR_BERR | CHAFSR_TO))
			recoverable = 0;
	}

	/* Log errors. */
	cheetah_log_errors(regs, &local_snapshot, afsr, afar, recoverable);

	/* "Recoverable" here means we try to yank the page from ever
	 * being newly used again.  This depends upon a few things:
	 * 1) Must be main memory, and AFAR must be valid.
	 * 2) If we trapped from user, OK.
	 * 3) Else, if we trapped from kernel we must find exception
	 *    table entry (ie. we have to have been accessing user
	 *    space).
	 *
	 * If AFAR is not in main memory, or we trapped from kernel
	 * and cannot find an exception table entry, it is unacceptable
	 * to try and continue.
	 */
	if (recoverable && is_memory) {
		if ((regs->tstate & TSTATE_PRIV) == 0UL) {
			/* OK, usermode access. */
			recoverable = 1;
		} else {
			const struct exception_table_entry *entry;

			entry = search_exception_tables(regs->tpc);
			if (entry) {
				/* OK, kernel access to userspace. */
				recoverable = 1;

			} else {
				/* BAD, privileged state is corrupted. */
				recoverable = 0;
			}

			if (recoverable) {
				if (pfn_valid(afar >> PAGE_SHIFT))
					get_page(pfn_to_page(afar >> PAGE_SHIFT));
				else
					recoverable = 0;

				/* Only perform fixup if we still have a
				 * recoverable condition.
				 */
				if (recoverable) {
					regs->tpc = entry->fixup;
					regs->tnpc = regs->tpc + 4;
				}
			}
		}
	} else {
		recoverable = 0;
	}

	if (!recoverable)
		panic("Irrecoverable deferred error trap.\n");
}

/* Handle a D/I cache parity error trap.  TYPE is encoded as:
 *
 * Bit0:	0=dcache,1=icache
 * Bit1:	0=recoverable,1=unrecoverable
 *
 * The hardware has disabled both the I-cache and D-cache in
 * the %dcr register.  
 */
void cheetah_plus_parity_error(int type, struct pt_regs *regs)
{
	if (type & 0x1)
		__cheetah_flush_icache();
	else
		cheetah_plus_zap_dcache_parity();
	cheetah_flush_dcache();

	/* Re-enable I-cache/D-cache */
	__asm__ __volatile__("ldxa [%%g0] %0, %%g1\n\t"
			     "or %%g1, %1, %%g1\n\t"
			     "stxa %%g1, [%%g0] %0\n\t"
			     "membar #Sync"
			     : /* no outputs */
			     : "i" (ASI_DCU_CONTROL_REG),
			       "i" (DCU_DC | DCU_IC)
			     : "g1");

	if (type & 0x2) {
		printk(KERN_EMERG "CPU[%d]: Cheetah+ %c-cache parity error at TPC[%016lx]\n",
		       smp_processor_id(),
		       (type & 0x1) ? 'I' : 'D',
		       regs->tpc);
		panic("Irrecoverable Cheetah+ parity error.");
	}

	printk(KERN_WARNING "CPU[%d]: Cheetah+ %c-cache parity error at TPC[%016lx]\n",
	       smp_processor_id(),
	       (type & 0x1) ? 'I' : 'D',
	       regs->tpc);
}

void do_fpe_common(struct pt_regs *regs)
{
	if (regs->tstate & TSTATE_PRIV) {
		regs->tpc = regs->tnpc;
		regs->tnpc += 4;
	} else {
		unsigned long fsr = current_thread_info()->xfsr[0];
		siginfo_t info;

		if (test_thread_flag(TIF_32BIT)) {
			regs->tpc &= 0xffffffff;
			regs->tnpc &= 0xffffffff;
		}
		info.si_signo = SIGFPE;
		info.si_errno = 0;
		info.si_addr = (void __user *)regs->tpc;
		info.si_trapno = 0;
		info.si_code = __SI_FAULT;
		if ((fsr & 0x1c000) == (1 << 14)) {
			if (fsr & 0x10)
				info.si_code = FPE_FLTINV;
			else if (fsr & 0x08)
				info.si_code = FPE_FLTOVF;
			else if (fsr & 0x04)
				info.si_code = FPE_FLTUND;
			else if (fsr & 0x02)
				info.si_code = FPE_FLTDIV;
			else if (fsr & 0x01)
				info.si_code = FPE_FLTRES;
		}
		force_sig_info(SIGFPE, &info, current);
	}
}

void do_fpieee(struct pt_regs *regs)
{
	if (notify_die(DIE_TRAP, "fpu exception ieee", regs,
		       0, 0x24, SIGFPE) == NOTIFY_STOP)
		return;

	do_fpe_common(regs);
}

extern int do_mathemu(struct pt_regs *, struct fpustate *);

void do_fpother(struct pt_regs *regs)
{
	struct fpustate *f = FPUSTATE;
	int ret = 0;

	if (notify_die(DIE_TRAP, "fpu exception other", regs,
		       0, 0x25, SIGFPE) == NOTIFY_STOP)
		return;

	switch ((current_thread_info()->xfsr[0] & 0x1c000)) {
	case (2 << 14): /* unfinished_FPop */
	case (3 << 14): /* unimplemented_FPop */
		ret = do_mathemu(regs, f);
		break;
	}
	if (ret)
		return;
	do_fpe_common(regs);
}

void do_tof(struct pt_regs *regs)
{
	siginfo_t info;

	if (notify_die(DIE_TRAP, "tagged arithmetic overflow", regs,
		       0, 0x26, SIGEMT) == NOTIFY_STOP)
		return;

	if (regs->tstate & TSTATE_PRIV)
		die_if_kernel("Penguin overflow trap from kernel mode", regs);
	if (test_thread_flag(TIF_32BIT)) {
		regs->tpc &= 0xffffffff;
		regs->tnpc &= 0xffffffff;
	}
	info.si_signo = SIGEMT;
	info.si_errno = 0;
	info.si_code = EMT_TAGOVF;
	info.si_addr = (void __user *)regs->tpc;
	info.si_trapno = 0;
	force_sig_info(SIGEMT, &info, current);
}

void do_div0(struct pt_regs *regs)
{
	siginfo_t info;

	if (notify_die(DIE_TRAP, "integer division by zero", regs,
		       0, 0x28, SIGFPE) == NOTIFY_STOP)
		return;

	if (regs->tstate & TSTATE_PRIV)
		die_if_kernel("TL0: Kernel divide by zero.", regs);
	if (test_thread_flag(TIF_32BIT)) {
		regs->tpc &= 0xffffffff;
		regs->tnpc &= 0xffffffff;
	}
	info.si_signo = SIGFPE;
	info.si_errno = 0;
	info.si_code = FPE_INTDIV;
	info.si_addr = (void __user *)regs->tpc;
	info.si_trapno = 0;
	force_sig_info(SIGFPE, &info, current);
}

void instruction_dump (unsigned int *pc)
{
	int i;

	if ((((unsigned long) pc) & 3))
		return;

	printk("Instruction DUMP:");
	for (i = -3; i < 6; i++)
		printk("%c%08x%c",i?' ':'<',pc[i],i?' ':'>');
	printk("\n");
}

static void user_instruction_dump (unsigned int __user *pc)
{
	int i;
	unsigned int buf[9];
	
	if ((((unsigned long) pc) & 3))
		return;
		
	if (copy_from_user(buf, pc - 3, sizeof(buf)))
		return;

	printk("Instruction DUMP:");
	for (i = 0; i < 9; i++)
		printk("%c%08x%c",i==3?' ':'<',buf[i],i==3?' ':'>');
	printk("\n");
}

void show_stack(struct task_struct *tsk, unsigned long *_ksp)
{
	unsigned long pc, fp, thread_base, ksp;
	void *tp = task_stack_page(tsk);
	struct reg_window *rw;
	int count = 0;

	ksp = (unsigned long) _ksp;

	if (tp == current_thread_info())
		flushw_all();

	fp = ksp + STACK_BIAS;
	thread_base = (unsigned long) tp;

	printk("Call Trace:");
#ifdef CONFIG_KALLSYMS
	printk("\n");
#endif
	do {
		/* Bogus frame pointer? */
		if (fp < (thread_base + sizeof(struct thread_info)) ||
		    fp >= (thread_base + THREAD_SIZE))
			break;
		rw = (struct reg_window *)fp;
		pc = rw->ins[7];
		printk(" [%016lx] ", pc);
		print_symbol("%s\n", pc);
		fp = rw->ins[6] + STACK_BIAS;
	} while (++count < 16);
#ifndef CONFIG_KALLSYMS
	printk("\n");
#endif
}

void dump_stack(void)
{
	unsigned long *ksp;

	__asm__ __volatile__("mov	%%fp, %0"
			     : "=r" (ksp));
	show_stack(current, ksp);
}

EXPORT_SYMBOL(dump_stack);

static inline int is_kernel_stack(struct task_struct *task,
				  struct reg_window *rw)
{
	unsigned long rw_addr = (unsigned long) rw;
	unsigned long thread_base, thread_end;

	if (rw_addr < PAGE_OFFSET) {
		if (task != &init_task)
			return 0;
	}

	thread_base = (unsigned long) task_stack_page(task);
	thread_end = thread_base + sizeof(union thread_union);
	if (rw_addr >= thread_base &&
	    rw_addr < thread_end &&
	    !(rw_addr & 0x7UL))
		return 1;

	return 0;
}

static inline struct reg_window *kernel_stack_up(struct reg_window *rw)
{
	unsigned long fp = rw->ins[6];

	if (!fp)
		return NULL;

	return (struct reg_window *) (fp + STACK_BIAS);
}

void die_if_kernel(char *str, struct pt_regs *regs)
{
	static int die_counter;
	extern void __show_regs(struct pt_regs * regs);
	extern void smp_report_regs(void);
	int count = 0;
	
	/* Amuse the user. */
	printk(
"              \\|/ ____ \\|/\n"
"              \"@'/ .. \\`@\"\n"
"              /_| \\__/ |_\\\n"
"                 \\__U_/\n");

	printk("%s(%d): %s [#%d]\n", current->comm, current->pid, str, ++die_counter);
	notify_die(DIE_OOPS, str, regs, 0, 255, SIGSEGV);
	__asm__ __volatile__("flushw");
	__show_regs(regs);
	if (regs->tstate & TSTATE_PRIV) {
		struct reg_window *rw = (struct reg_window *)
			(regs->u_regs[UREG_FP] + STACK_BIAS);

		/* Stop the back trace when we hit userland or we
		 * find some badly aligned kernel stack.
		 */
		while (rw &&
		       count++ < 30&&
		       is_kernel_stack(current, rw)) {
			printk("Caller[%016lx]", rw->ins[7]);
			print_symbol(": %s", rw->ins[7]);
			printk("\n");

			rw = kernel_stack_up(rw);
		}
		instruction_dump ((unsigned int *) regs->tpc);
	} else {
		if (test_thread_flag(TIF_32BIT)) {
			regs->tpc &= 0xffffffff;
			regs->tnpc &= 0xffffffff;
		}
		user_instruction_dump ((unsigned int __user *) regs->tpc);
	}
#ifdef CONFIG_SMP
	smp_report_regs();
#endif
                                                	
	if (regs->tstate & TSTATE_PRIV)
		do_exit(SIGKILL);
	do_exit(SIGSEGV);
}

extern int handle_popc(u32 insn, struct pt_regs *regs);
extern int handle_ldf_stq(u32 insn, struct pt_regs *regs);

void do_illegal_instruction(struct pt_regs *regs)
{
	unsigned long pc = regs->tpc;
	unsigned long tstate = regs->tstate;
	u32 insn;
	siginfo_t info;

	if (notify_die(DIE_TRAP, "illegal instruction", regs,
		       0, 0x10, SIGILL) == NOTIFY_STOP)
		return;

	if (tstate & TSTATE_PRIV)
		die_if_kernel("Kernel illegal instruction", regs);
	if (test_thread_flag(TIF_32BIT))
		pc = (u32)pc;
	if (get_user(insn, (u32 __user *) pc) != -EFAULT) {
		if ((insn & 0xc1ffc000) == 0x81700000) /* POPC */ {
			if (handle_popc(insn, regs))
				return;
		} else if ((insn & 0xc1580000) == 0xc1100000) /* LDQ/STQ */ {
			if (handle_ldf_stq(insn, regs))
				return;
		}
	}
	info.si_signo = SIGILL;
	info.si_errno = 0;
	info.si_code = ILL_ILLOPC;
	info.si_addr = (void __user *)pc;
	info.si_trapno = 0;
	force_sig_info(SIGILL, &info, current);
}

void mem_address_unaligned(struct pt_regs *regs, unsigned long sfar, unsigned long sfsr)
{
	siginfo_t info;

	if (notify_die(DIE_TRAP, "memory address unaligned", regs,
		       0, 0x34, SIGSEGV) == NOTIFY_STOP)
		return;

	if (regs->tstate & TSTATE_PRIV) {
		extern void kernel_unaligned_trap(struct pt_regs *regs,
						  unsigned int insn, 
						  unsigned long sfar,
						  unsigned long sfsr);

		kernel_unaligned_trap(regs, *((unsigned int *)regs->tpc),
				      sfar, sfsr);
		return;
	}
	info.si_signo = SIGBUS;
	info.si_errno = 0;
	info.si_code = BUS_ADRALN;
	info.si_addr = (void __user *)sfar;
	info.si_trapno = 0;
	force_sig_info(SIGBUS, &info, current);
}

void do_privop(struct pt_regs *regs)
{
	siginfo_t info;

	if (notify_die(DIE_TRAP, "privileged operation", regs,
		       0, 0x11, SIGILL) == NOTIFY_STOP)
		return;

	if (test_thread_flag(TIF_32BIT)) {
		regs->tpc &= 0xffffffff;
		regs->tnpc &= 0xffffffff;
	}
	info.si_signo = SIGILL;
	info.si_errno = 0;
	info.si_code = ILL_PRVOPC;
	info.si_addr = (void __user *)regs->tpc;
	info.si_trapno = 0;
	force_sig_info(SIGILL, &info, current);
}

void do_privact(struct pt_regs *regs)
{
	do_privop(regs);
}

/* Trap level 1 stuff or other traps we should never see... */
void do_cee(struct pt_regs *regs)
{
	die_if_kernel("TL0: Cache Error Exception", regs);
}

void do_cee_tl1(struct pt_regs *regs)
{
	dump_tl1_traplog((struct tl1_traplog *)(regs + 1));
	die_if_kernel("TL1: Cache Error Exception", regs);
}

void do_dae_tl1(struct pt_regs *regs)
{
	dump_tl1_traplog((struct tl1_traplog *)(regs + 1));
	die_if_kernel("TL1: Data Access Exception", regs);
}

void do_iae_tl1(struct pt_regs *regs)
{
	dump_tl1_traplog((struct tl1_traplog *)(regs + 1));
	die_if_kernel("TL1: Instruction Access Exception", regs);
}

void do_div0_tl1(struct pt_regs *regs)
{
	dump_tl1_traplog((struct tl1_traplog *)(regs + 1));
	die_if_kernel("TL1: DIV0 Exception", regs);
}

void do_fpdis_tl1(struct pt_regs *regs)
{
	dump_tl1_traplog((struct tl1_traplog *)(regs + 1));
	die_if_kernel("TL1: FPU Disabled", regs);
}

void do_fpieee_tl1(struct pt_regs *regs)
{
	dump_tl1_traplog((struct tl1_traplog *)(regs + 1));
	die_if_kernel("TL1: FPU IEEE Exception", regs);
}

void do_fpother_tl1(struct pt_regs *regs)
{
	dump_tl1_traplog((struct tl1_traplog *)(regs + 1));
	die_if_kernel("TL1: FPU Other Exception", regs);
}

void do_ill_tl1(struct pt_regs *regs)
{
	dump_tl1_traplog((struct tl1_traplog *)(regs + 1));
	die_if_kernel("TL1: Illegal Instruction Exception", regs);
}

void do_irq_tl1(struct pt_regs *regs)
{
	dump_tl1_traplog((struct tl1_traplog *)(regs + 1));
	die_if_kernel("TL1: IRQ Exception", regs);
}

void do_lddfmna_tl1(struct pt_regs *regs)
{
	dump_tl1_traplog((struct tl1_traplog *)(regs + 1));
	die_if_kernel("TL1: LDDF Exception", regs);
}

void do_stdfmna_tl1(struct pt_regs *regs)
{
	dump_tl1_traplog((struct tl1_traplog *)(regs + 1));
	die_if_kernel("TL1: STDF Exception", regs);
}

void do_paw(struct pt_regs *regs)
{
	die_if_kernel("TL0: Phys Watchpoint Exception", regs);
}

void do_paw_tl1(struct pt_regs *regs)
{
	dump_tl1_traplog((struct tl1_traplog *)(regs + 1));
	die_if_kernel("TL1: Phys Watchpoint Exception", regs);
}

void do_vaw(struct pt_regs *regs)
{
	die_if_kernel("TL0: Virt Watchpoint Exception", regs);
}

void do_vaw_tl1(struct pt_regs *regs)
{
	dump_tl1_traplog((struct tl1_traplog *)(regs + 1));
	die_if_kernel("TL1: Virt Watchpoint Exception", regs);
}

void do_tof_tl1(struct pt_regs *regs)
{
	dump_tl1_traplog((struct tl1_traplog *)(regs + 1));
	die_if_kernel("TL1: Tag Overflow Exception", regs);
}

void do_getpsr(struct pt_regs *regs)
{
	regs->u_regs[UREG_I0] = tstate_to_psr(regs->tstate);
	regs->tpc   = regs->tnpc;
	regs->tnpc += 4;
	if (test_thread_flag(TIF_32BIT)) {
		regs->tpc &= 0xffffffff;
		regs->tnpc &= 0xffffffff;
	}
}

extern void thread_info_offsets_are_bolixed_dave(void);

/* Only invoked on boot processor. */
void __init trap_init(void)
{
	/* Compile time sanity check. */
	if (TI_TASK != offsetof(struct thread_info, task) ||
	    TI_FLAGS != offsetof(struct thread_info, flags) ||
	    TI_CPU != offsetof(struct thread_info, cpu) ||
	    TI_FPSAVED != offsetof(struct thread_info, fpsaved) ||
	    TI_KSP != offsetof(struct thread_info, ksp) ||
	    TI_FAULT_ADDR != offsetof(struct thread_info, fault_address) ||
	    TI_KREGS != offsetof(struct thread_info, kregs) ||
	    TI_UTRAPS != offsetof(struct thread_info, utraps) ||
	    TI_EXEC_DOMAIN != offsetof(struct thread_info, exec_domain) ||
	    TI_REG_WINDOW != offsetof(struct thread_info, reg_window) ||
	    TI_RWIN_SPTRS != offsetof(struct thread_info, rwbuf_stkptrs) ||
	    TI_GSR != offsetof(struct thread_info, gsr) ||
	    TI_XFSR != offsetof(struct thread_info, xfsr) ||
	    TI_USER_CNTD0 != offsetof(struct thread_info, user_cntd0) ||
	    TI_USER_CNTD1 != offsetof(struct thread_info, user_cntd1) ||
	    TI_KERN_CNTD0 != offsetof(struct thread_info, kernel_cntd0) ||
	    TI_KERN_CNTD1 != offsetof(struct thread_info, kernel_cntd1) ||
	    TI_PCR != offsetof(struct thread_info, pcr_reg) ||
	    TI_CEE_STUFF != offsetof(struct thread_info, cee_stuff) ||
	    TI_PRE_COUNT != offsetof(struct thread_info, preempt_count) ||
	    TI_NEW_CHILD != offsetof(struct thread_info, new_child) ||
	    TI_SYS_NOERROR != offsetof(struct thread_info, syscall_noerror) ||
	    TI_RESTART_BLOCK != offsetof(struct thread_info, restart_block) ||
	    TI_KUNA_REGS != offsetof(struct thread_info, kern_una_regs) ||
	    TI_KUNA_INSN != offsetof(struct thread_info, kern_una_insn) ||
	    TI_FPREGS != offsetof(struct thread_info, fpregs) ||
	    (TI_FPREGS & (64 - 1)))
		thread_info_offsets_are_bolixed_dave();

	/* Attach to the address space of init_task.  On SMP we
	 * do this in smp.c:smp_callin for other cpus.
	 */
	atomic_inc(&init_mm.mm_count);
	current->active_mm = &init_mm;
}
