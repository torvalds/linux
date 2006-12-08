/* Copyright (C) 2004 Mips Technologies, Inc */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/cpumask.h>
#include <linux/interrupt.h>

#include <asm/cpu.h>
#include <asm/processor.h>
#include <asm/atomic.h>
#include <asm/system.h>
#include <asm/hardirq.h>
#include <asm/hazards.h>
#include <asm/mmu_context.h>
#include <asm/smp.h>
#include <asm/mipsregs.h>
#include <asm/cacheflush.h>
#include <asm/time.h>
#include <asm/addrspace.h>
#include <asm/smtc.h>
#include <asm/smtc_ipi.h>
#include <asm/smtc_proc.h>

/*
 * This file should be built into the kernel only if CONFIG_MIPS_MT_SMTC is set.
 */

/*
 * MIPSCPU_INT_BASE is identically defined in both
 * asm-mips/mips-boards/maltaint.h and asm-mips/mips-boards/simint.h,
 * but as yet there's no properly organized include structure that
 * will ensure that the right *int.h file will be included for a
 * given platform build.
 */

#define MIPSCPU_INT_BASE	16

#define MIPS_CPU_IPI_IRQ	1

#define LOCK_MT_PRA() \
	local_irq_save(flags); \
	mtflags = dmt()

#define UNLOCK_MT_PRA() \
	emt(mtflags); \
	local_irq_restore(flags)

#define LOCK_CORE_PRA() \
	local_irq_save(flags); \
	mtflags = dvpe()

#define UNLOCK_CORE_PRA() \
	evpe(mtflags); \
	local_irq_restore(flags)

/*
 * Data structures purely associated with SMTC parallelism
 */


/*
 * Table for tracking ASIDs whose lifetime is prolonged.
 */

asiduse smtc_live_asid[MAX_SMTC_TLBS][MAX_SMTC_ASIDS];

/*
 * Clock interrupt "latch" buffers, per "CPU"
 */

unsigned int ipi_timer_latch[NR_CPUS];

/*
 * Number of InterProcessor Interupt (IPI) message buffers to allocate
 */

#define IPIBUF_PER_CPU 4

struct smtc_ipi_q IPIQ[NR_CPUS];
struct smtc_ipi_q freeIPIq;


/* Forward declarations */

void ipi_decode(struct smtc_ipi *);
void post_direct_ipi(int cpu, struct smtc_ipi *pipi);
void setup_cross_vpe_interrupts(void);
void init_smtc_stats(void);

/* Global SMTC Status */

unsigned int smtc_status = 0;

/* Boot command line configuration overrides */

static int vpelimit = 0;
static int tclimit = 0;
static int ipibuffers = 0;
static int nostlb = 0;
static int asidmask = 0;
unsigned long smtc_asid_mask = 0xff;

static int __init maxvpes(char *str)
{
	get_option(&str, &vpelimit);
	return 1;
}

static int __init maxtcs(char *str)
{
	get_option(&str, &tclimit);
	return 1;
}

static int __init ipibufs(char *str)
{
	get_option(&str, &ipibuffers);
	return 1;
}

static int __init stlb_disable(char *s)
{
	nostlb = 1;
	return 1;
}

static int __init asidmask_set(char *str)
{
	get_option(&str, &asidmask);
	switch (asidmask) {
	case 0x1:
	case 0x3:
	case 0x7:
	case 0xf:
	case 0x1f:
	case 0x3f:
	case 0x7f:
	case 0xff:
		smtc_asid_mask = (unsigned long)asidmask;
		break;
	default:
		printk("ILLEGAL ASID mask 0x%x from command line\n", asidmask);
	}
	return 1;
}

__setup("maxvpes=", maxvpes);
__setup("maxtcs=", maxtcs);
__setup("ipibufs=", ipibufs);
__setup("nostlb", stlb_disable);
__setup("asidmask=", asidmask_set);

/* Enable additional debug checks before going into CPU idle loop */
#define SMTC_IDLE_HOOK_DEBUG

#ifdef SMTC_IDLE_HOOK_DEBUG

static int hang_trig = 0;

static int __init hangtrig_enable(char *s)
{
	hang_trig = 1;
	return 1;
}


__setup("hangtrig", hangtrig_enable);

#define DEFAULT_BLOCKED_IPI_LIMIT 32

static int timerq_limit = DEFAULT_BLOCKED_IPI_LIMIT;

static int __init tintq(char *str)
{
	get_option(&str, &timerq_limit);
	return 1;
}

__setup("tintq=", tintq);

int imstuckcount[2][8];
/* vpemask represents IM/IE bits of per-VPE Status registers, low-to-high */
int vpemask[2][8] = {{0,1,1,0,0,0,0,1},{0,1,0,0,0,0,0,1}};
int tcnoprog[NR_CPUS];
static atomic_t idle_hook_initialized = {0};
static int clock_hang_reported[NR_CPUS];

#endif /* SMTC_IDLE_HOOK_DEBUG */

/* Initialize shared TLB - the should probably migrate to smtc_setup_cpus() */

void __init sanitize_tlb_entries(void)
{
	printk("Deprecated sanitize_tlb_entries() invoked\n");
}


/*
 * Configure shared TLB - VPC configuration bit must be set by caller
 */

void smtc_configure_tlb(void)
{
	int i,tlbsiz,vpes;
	unsigned long mvpconf0;
	unsigned long config1val;

	/* Set up ASID preservation table */
	for (vpes=0; vpes<MAX_SMTC_TLBS; vpes++) {
	    for(i = 0; i < MAX_SMTC_ASIDS; i++) {
		smtc_live_asid[vpes][i] = 0;
	    }
	}
	mvpconf0 = read_c0_mvpconf0();

	if ((vpes = ((mvpconf0 & MVPCONF0_PVPE)
			>> MVPCONF0_PVPE_SHIFT) + 1) > 1) {
	    /* If we have multiple VPEs, try to share the TLB */
	    if ((mvpconf0 & MVPCONF0_TLBS) && !nostlb) {
		/*
		 * If TLB sizing is programmable, shared TLB
		 * size is the total available complement.
		 * Otherwise, we have to take the sum of all
		 * static VPE TLB entries.
		 */
		if ((tlbsiz = ((mvpconf0 & MVPCONF0_PTLBE)
				>> MVPCONF0_PTLBE_SHIFT)) == 0) {
		    /*
		     * If there's more than one VPE, there had better
		     * be more than one TC, because we need one to bind
		     * to each VPE in turn to be able to read
		     * its configuration state!
		     */
		    settc(1);
		    /* Stop the TC from doing anything foolish */
		    write_tc_c0_tchalt(TCHALT_H);
		    mips_ihb();
		    /* No need to un-Halt - that happens later anyway */
		    for (i=0; i < vpes; i++) {
		    	write_tc_c0_tcbind(i);
			/*
			 * To be 100% sure we're really getting the right
			 * information, we exit the configuration state
			 * and do an IHB after each rebinding.
			 */
			write_c0_mvpcontrol(
				read_c0_mvpcontrol() & ~ MVPCONTROL_VPC );
			mips_ihb();
			/*
			 * Only count if the MMU Type indicated is TLB
			 */
			if (((read_vpe_c0_config() & MIPS_CONF_MT) >> 7) == 1) {
				config1val = read_vpe_c0_config1();
				tlbsiz += ((config1val >> 25) & 0x3f) + 1;
			}

			/* Put core back in configuration state */
			write_c0_mvpcontrol(
				read_c0_mvpcontrol() | MVPCONTROL_VPC );
			mips_ihb();
		    }
		}
		write_c0_mvpcontrol(read_c0_mvpcontrol() | MVPCONTROL_STLB);

		/*
		 * Setup kernel data structures to use software total,
		 * rather than read the per-VPE Config1 value. The values
		 * for "CPU 0" gets copied to all the other CPUs as part
		 * of their initialization in smtc_cpu_setup().
		 */

		tlbsiz = tlbsiz & 0x3f;	/* MIPS32 limits TLB indices to 64 */
		cpu_data[0].tlbsize = tlbsiz;
		smtc_status |= SMTC_TLB_SHARED;

		printk("TLB of %d entry pairs shared by %d VPEs\n",
			tlbsiz, vpes);
	    } else {
		printk("WARNING: TLB Not Sharable on SMTC Boot!\n");
	    }
	}
}


/*
 * Incrementally build the CPU map out of constituent MIPS MT cores,
 * using the specified available VPEs and TCs.  Plaform code needs
 * to ensure that each MIPS MT core invokes this routine on reset,
 * one at a time(!).
 *
 * This version of the build_cpu_map and prepare_cpus routines assumes
 * that *all* TCs of a MIPS MT core will be used for Linux, and that
 * they will be spread across *all* available VPEs (to minimise the
 * loss of efficiency due to exception service serialization).
 * An improved version would pick up configuration information and
 * possibly leave some TCs/VPEs as "slave" processors.
 *
 * Use c0_MVPConf0 to find out how many TCs are available, setting up
 * phys_cpu_present_map and the logical/physical mappings.
 */

int __init mipsmt_build_cpu_map(int start_cpu_slot)
{
	int i, ntcs;

	/*
	 * The CPU map isn't actually used for anything at this point,
	 * so it's not clear what else we should do apart from set
	 * everything up so that "logical" = "physical".
	 */
	ntcs = ((read_c0_mvpconf0() & MVPCONF0_PTC) >> MVPCONF0_PTC_SHIFT) + 1;
	for (i=start_cpu_slot; i<NR_CPUS && i<ntcs; i++) {
		cpu_set(i, phys_cpu_present_map);
		__cpu_number_map[i] = i;
		__cpu_logical_map[i] = i;
	}
	/* Initialize map of CPUs with FPUs */
	cpus_clear(mt_fpu_cpumask);

	/* One of those TC's is the one booting, and not a secondary... */
	printk("%i available secondary CPU TC(s)\n", i - 1);

	return i;
}

/*
 * Common setup before any secondaries are started
 * Make sure all CPU's are in a sensible state before we boot any of the
 * secondaries.
 *
 * For MIPS MT "SMTC" operation, we set up all TCs, spread as evenly
 * as possible across the available VPEs.
 */

static void smtc_tc_setup(int vpe, int tc, int cpu)
{
	settc(tc);
	write_tc_c0_tchalt(TCHALT_H);
	mips_ihb();
	write_tc_c0_tcstatus((read_tc_c0_tcstatus()
			& ~(TCSTATUS_TKSU | TCSTATUS_DA | TCSTATUS_IXMT))
			| TCSTATUS_A);
	write_tc_c0_tccontext(0);
	/* Bind tc to vpe */
	write_tc_c0_tcbind(vpe);
	/* In general, all TCs should have the same cpu_data indications */
	memcpy(&cpu_data[cpu], &cpu_data[0], sizeof(struct cpuinfo_mips));
	/* For 34Kf, start with TC/CPU 0 as sole owner of single FPU context */
	if (cpu_data[0].cputype == CPU_34K)
		cpu_data[cpu].options &= ~MIPS_CPU_FPU;
	cpu_data[cpu].vpe_id = vpe;
	cpu_data[cpu].tc_id = tc;
}


void mipsmt_prepare_cpus(void)
{
	int i, vpe, tc, ntc, nvpe, tcpervpe, slop, cpu;
	unsigned long flags;
	unsigned long val;
	int nipi;
	struct smtc_ipi *pipi;

	/* disable interrupts so we can disable MT */
	local_irq_save(flags);
	/* disable MT so we can configure */
	dvpe();
	dmt();

	spin_lock_init(&freeIPIq.lock);

	/*
	 * We probably don't have as many VPEs as we do SMP "CPUs",
	 * but it's possible - and in any case we'll never use more!
	 */
	for (i=0; i<NR_CPUS; i++) {
		IPIQ[i].head = IPIQ[i].tail = NULL;
		spin_lock_init(&IPIQ[i].lock);
		IPIQ[i].depth = 0;
		ipi_timer_latch[i] = 0;
	}

	/* cpu_data index starts at zero */
	cpu = 0;
	cpu_data[cpu].vpe_id = 0;
	cpu_data[cpu].tc_id = 0;
	cpu++;

	/* Report on boot-time options */
	mips_mt_set_cpuoptions ();
	if (vpelimit > 0)
		printk("Limit of %d VPEs set\n", vpelimit);
	if (tclimit > 0)
		printk("Limit of %d TCs set\n", tclimit);
	if (nostlb) {
		printk("Shared TLB Use Inhibited - UNSAFE for Multi-VPE Operation\n");
	}
	if (asidmask)
		printk("ASID mask value override to 0x%x\n", asidmask);

	/* Temporary */
#ifdef SMTC_IDLE_HOOK_DEBUG
	if (hang_trig)
		printk("Logic Analyser Trigger on suspected TC hang\n");
#endif /* SMTC_IDLE_HOOK_DEBUG */

	/* Put MVPE's into 'configuration state' */
	write_c0_mvpcontrol( read_c0_mvpcontrol() | MVPCONTROL_VPC );

	val = read_c0_mvpconf0();
	nvpe = ((val & MVPCONF0_PVPE) >> MVPCONF0_PVPE_SHIFT) + 1;
	if (vpelimit > 0 && nvpe > vpelimit)
		nvpe = vpelimit;
	ntc = ((val & MVPCONF0_PTC) >> MVPCONF0_PTC_SHIFT) + 1;
	if (ntc > NR_CPUS)
		ntc = NR_CPUS;
	if (tclimit > 0 && ntc > tclimit)
		ntc = tclimit;
	tcpervpe = ntc / nvpe;
	slop = ntc % nvpe;	/* Residual TCs, < NVPE */

	/* Set up shared TLB */
	smtc_configure_tlb();

	for (tc = 0, vpe = 0 ; (vpe < nvpe) && (tc < ntc) ; vpe++) {
		/*
		 * Set the MVP bits.
		 */
		settc(tc);
		write_vpe_c0_vpeconf0(read_vpe_c0_vpeconf0() | VPECONF0_MVP);
		if (vpe != 0)
			printk(", ");
		printk("VPE %d: TC", vpe);
		for (i = 0; i < tcpervpe; i++) {
			/*
			 * TC 0 is bound to VPE 0 at reset,
			 * and is presumably executing this
			 * code.  Leave it alone!
			 */
			if (tc != 0) {
				smtc_tc_setup(vpe,tc, cpu);
				cpu++;
			}
			printk(" %d", tc);
			tc++;
		}
		if (slop) {
			if (tc != 0) {
				smtc_tc_setup(vpe,tc, cpu);
				cpu++;
			}
			printk(" %d", tc);
			tc++;
			slop--;
		}
		if (vpe != 0) {
			/*
			 * Clear any stale software interrupts from VPE's Cause
			 */
			write_vpe_c0_cause(0);

			/*
			 * Clear ERL/EXL of VPEs other than 0
			 * and set restricted interrupt enable/mask.
			 */
			write_vpe_c0_status((read_vpe_c0_status()
				& ~(ST0_BEV | ST0_ERL | ST0_EXL | ST0_IM))
				| (STATUSF_IP0 | STATUSF_IP1 | STATUSF_IP7
				| ST0_IE));
			/*
			 * set config to be the same as vpe0,
			 *  particularly kseg0 coherency alg
			 */
			write_vpe_c0_config(read_c0_config());
			/* Clear any pending timer interrupt */
			write_vpe_c0_compare(0);
			/* Propagate Config7 */
			write_vpe_c0_config7(read_c0_config7());
			write_vpe_c0_count(read_c0_count());
		}
		/* enable multi-threading within VPE */
		write_vpe_c0_vpecontrol(read_vpe_c0_vpecontrol() | VPECONTROL_TE);
		/* enable the VPE */
		write_vpe_c0_vpeconf0(read_vpe_c0_vpeconf0() | VPECONF0_VPA);
	}

	/*
	 * Pull any physically present but unused TCs out of circulation.
	 */
	while (tc < (((val & MVPCONF0_PTC) >> MVPCONF0_PTC_SHIFT) + 1)) {
		cpu_clear(tc, phys_cpu_present_map);
		cpu_clear(tc, cpu_present_map);
		tc++;
	}

	/* release config state */
	write_c0_mvpcontrol( read_c0_mvpcontrol() & ~ MVPCONTROL_VPC );

	printk("\n");

	/* Set up coprocessor affinity CPU mask(s) */

	for (tc = 0; tc < ntc; tc++) {
		if (cpu_data[tc].options & MIPS_CPU_FPU)
			cpu_set(tc, mt_fpu_cpumask);
	}

	/* set up ipi interrupts... */

	/* If we have multiple VPEs running, set up the cross-VPE interrupt */

	if (nvpe > 1)
		setup_cross_vpe_interrupts();

	/* Set up queue of free IPI "messages". */
	nipi = NR_CPUS * IPIBUF_PER_CPU;
	if (ipibuffers > 0)
		nipi = ipibuffers;

	pipi = kmalloc(nipi *sizeof(struct smtc_ipi), GFP_KERNEL);
	if (pipi == NULL)
		panic("kmalloc of IPI message buffers failed\n");
	else
		printk("IPI buffer pool of %d buffers\n", nipi);
	for (i = 0; i < nipi; i++) {
		smtc_ipi_nq(&freeIPIq, pipi);
		pipi++;
	}

	/* Arm multithreading and enable other VPEs - but all TCs are Halted */
	emt(EMT_ENABLE);
	evpe(EVPE_ENABLE);
	local_irq_restore(flags);
	/* Initialize SMTC /proc statistics/diagnostics */
	init_smtc_stats();
}


/*
 * Setup the PC, SP, and GP of a secondary processor and start it
 * running!
 * smp_bootstrap is the place to resume from
 * __KSTK_TOS(idle) is apparently the stack pointer
 * (unsigned long)idle->thread_info the gp
 *
 */
void smtc_boot_secondary(int cpu, struct task_struct *idle)
{
	extern u32 kernelsp[NR_CPUS];
	long flags;
	int mtflags;

	LOCK_MT_PRA();
	if (cpu_data[cpu].vpe_id != cpu_data[smp_processor_id()].vpe_id) {
		dvpe();
	}
	settc(cpu_data[cpu].tc_id);

	/* pc */
	write_tc_c0_tcrestart((unsigned long)&smp_bootstrap);

	/* stack pointer */
	kernelsp[cpu] = __KSTK_TOS(idle);
	write_tc_gpr_sp(__KSTK_TOS(idle));

	/* global pointer */
	write_tc_gpr_gp((unsigned long)idle->thread_info);

	smtc_status |= SMTC_MTC_ACTIVE;
	write_tc_c0_tchalt(0);
	if (cpu_data[cpu].vpe_id != cpu_data[smp_processor_id()].vpe_id) {
		evpe(EVPE_ENABLE);
	}
	UNLOCK_MT_PRA();
}

void smtc_init_secondary(void)
{
	/*
	 * Start timer on secondary VPEs if necessary.
	 * plat_timer_setup has already have been invoked by init/main
	 * on "boot" TC.  Like per_cpu_trap_init() hack, this assumes that
	 * SMTC init code assigns TCs consdecutively and in ascending order
	 * to across available VPEs.
	 */
	if (((read_c0_tcbind() & TCBIND_CURTC) != 0) &&
	    ((read_c0_tcbind() & TCBIND_CURVPE)
	    != cpu_data[smp_processor_id() - 1].vpe_id)){
		write_c0_compare (read_c0_count() + mips_hpt_frequency/HZ);
	}

	local_irq_enable();
}

void smtc_smp_finish(void)
{
	printk("TC %d going on-line as CPU %d\n",
		cpu_data[smp_processor_id()].tc_id, smp_processor_id());
}

void smtc_cpus_done(void)
{
}

/*
 * Support for SMTC-optimized driver IRQ registration
 */

/*
 * SMTC Kernel needs to manipulate low-level CPU interrupt mask
 * in do_IRQ. These are passed in setup_irq_smtc() and stored
 * in this table.
 */

int setup_irq_smtc(unsigned int irq, struct irqaction * new,
			unsigned long hwmask)
{
	irq_hwmask[irq] = hwmask;

	return setup_irq(irq, new);
}

/*
 * IPI model for SMTC is tricky, because interrupts aren't TC-specific.
 * Within a VPE one TC can interrupt another by different approaches.
 * The easiest to get right would probably be to make all TCs except
 * the target IXMT and set a software interrupt, but an IXMT-based
 * scheme requires that a handler must run before a new IPI could
 * be sent, which would break the "broadcast" loops in MIPS MT.
 * A more gonzo approach within a VPE is to halt the TC, extract
 * its Restart, Status, and a couple of GPRs, and program the Restart
 * address to emulate an interrupt.
 *
 * Within a VPE, one can be confident that the target TC isn't in
 * a critical EXL state when halted, since the write to the Halt
 * register could not have issued on the writing thread if the
 * halting thread had EXL set. So k0 and k1 of the target TC
 * can be used by the injection code.  Across VPEs, one can't
 * be certain that the target TC isn't in a critical exception
 * state. So we try a two-step process of sending a software
 * interrupt to the target VPE, which either handles the event
 * itself (if it was the target) or injects the event within
 * the VPE.
 */

void smtc_ipi_qdump(void)
{
	int i;

	for (i = 0; i < NR_CPUS ;i++) {
		printk("IPIQ[%d]: head = 0x%x, tail = 0x%x, depth = %d\n",
			i, (unsigned)IPIQ[i].head, (unsigned)IPIQ[i].tail,
			IPIQ[i].depth);
	}
}

/*
 * The standard atomic.h primitives don't quite do what we want
 * here: We need an atomic add-and-return-previous-value (which
 * could be done with atomic_add_return and a decrement) and an
 * atomic set/zero-and-return-previous-value (which can't really
 * be done with the atomic.h primitives). And since this is
 * MIPS MT, we can assume that we have LL/SC.
 */
static __inline__ int atomic_postincrement(unsigned int *pv)
{
	unsigned long result;

	unsigned long temp;

	__asm__ __volatile__(
	"1:	ll	%0, %2					\n"
	"	addu	%1, %0, 1				\n"
	"	sc	%1, %2					\n"
	"	beqz	%1, 1b					\n"
	"	sync						\n"
	: "=&r" (result), "=&r" (temp), "=m" (*pv)
	: "m" (*pv)
	: "memory");

	return result;
}

/* No longer used in IPI dispatch, but retained for future recycling */

static __inline__ int atomic_postclear(unsigned int *pv)
{
	unsigned long result;

	unsigned long temp;

	__asm__ __volatile__(
	"1:	ll	%0, %2					\n"
	"	or	%1, $0, $0				\n"
	"	sc	%1, %2					\n"
	"	beqz	%1, 1b					\n"
	"	sync						\n"
	: "=&r" (result), "=&r" (temp), "=m" (*pv)
	: "m" (*pv)
	: "memory");

	return result;
}


void smtc_send_ipi(int cpu, int type, unsigned int action)
{
	int tcstatus;
	struct smtc_ipi *pipi;
	long flags;
	int mtflags;

	if (cpu == smp_processor_id()) {
		printk("Cannot Send IPI to self!\n");
		return;
	}
	/* Set up a descriptor, to be delivered either promptly or queued */
	pipi = smtc_ipi_dq(&freeIPIq);
	if (pipi == NULL) {
		bust_spinlocks(1);
		mips_mt_regdump(dvpe());
		panic("IPI Msg. Buffers Depleted\n");
	}
	pipi->type = type;
	pipi->arg = (void *)action;
	pipi->dest = cpu;
	if (cpu_data[cpu].vpe_id != cpu_data[smp_processor_id()].vpe_id) {
		/* If not on same VPE, enqueue and send cross-VPE interupt */
		smtc_ipi_nq(&IPIQ[cpu], pipi);
		LOCK_CORE_PRA();
		settc(cpu_data[cpu].tc_id);
		write_vpe_c0_cause(read_vpe_c0_cause() | C_SW1);
		UNLOCK_CORE_PRA();
	} else {
		/*
		 * Not sufficient to do a LOCK_MT_PRA (dmt) here,
		 * since ASID shootdown on the other VPE may
		 * collide with this operation.
		 */
		LOCK_CORE_PRA();
		settc(cpu_data[cpu].tc_id);
		/* Halt the targeted TC */
		write_tc_c0_tchalt(TCHALT_H);
		mips_ihb();

		/*
	 	 * Inspect TCStatus - if IXMT is set, we have to queue
		 * a message. Otherwise, we set up the "interrupt"
		 * of the other TC
	 	 */
		tcstatus = read_tc_c0_tcstatus();

		if ((tcstatus & TCSTATUS_IXMT) != 0) {
			/*
			 * Spin-waiting here can deadlock,
			 * so we queue the message for the target TC.
			 */
			write_tc_c0_tchalt(0);
			UNLOCK_CORE_PRA();
			/* Try to reduce redundant timer interrupt messages */
			if (type == SMTC_CLOCK_TICK) {
			    if (atomic_postincrement(&ipi_timer_latch[cpu])!=0){
				smtc_ipi_nq(&freeIPIq, pipi);
				return;
			    }
			}
			smtc_ipi_nq(&IPIQ[cpu], pipi);
		} else {
			post_direct_ipi(cpu, pipi);
			write_tc_c0_tchalt(0);
			UNLOCK_CORE_PRA();
		}
	}
}

/*
 * Send IPI message to Halted TC, TargTC/TargVPE already having been set
 */
void post_direct_ipi(int cpu, struct smtc_ipi *pipi)
{
	struct pt_regs *kstack;
	unsigned long tcstatus;
	unsigned long tcrestart;
	extern u32 kernelsp[NR_CPUS];
	extern void __smtc_ipi_vector(void);

	/* Extract Status, EPC from halted TC */
	tcstatus = read_tc_c0_tcstatus();
	tcrestart = read_tc_c0_tcrestart();
	/* If TCRestart indicates a WAIT instruction, advance the PC */
	if ((tcrestart & 0x80000000)
	    && ((*(unsigned int *)tcrestart & 0xfe00003f) == 0x42000020)) {
		tcrestart += 4;
	}
	/*
	 * Save on TC's future kernel stack
	 *
	 * CU bit of Status is indicator that TC was
	 * already running on a kernel stack...
	 */
	if (tcstatus & ST0_CU0)  {
		/* Note that this "- 1" is pointer arithmetic */
		kstack = ((struct pt_regs *)read_tc_gpr_sp()) - 1;
	} else {
		kstack = ((struct pt_regs *)kernelsp[cpu]) - 1;
	}

	kstack->cp0_epc = (long)tcrestart;
	/* Save TCStatus */
	kstack->cp0_tcstatus = tcstatus;
	/* Pass token of operation to be performed kernel stack pad area */
	kstack->pad0[4] = (unsigned long)pipi;
	/* Pass address of function to be called likewise */
	kstack->pad0[5] = (unsigned long)&ipi_decode;
	/* Set interrupt exempt and kernel mode */
	tcstatus |= TCSTATUS_IXMT;
	tcstatus &= ~TCSTATUS_TKSU;
	write_tc_c0_tcstatus(tcstatus);
	ehb();
	/* Set TC Restart address to be SMTC IPI vector */
	write_tc_c0_tcrestart(__smtc_ipi_vector);
}

static void ipi_resched_interrupt(void)
{
	/* Return from interrupt should be enough to cause scheduler check */
}


static void ipi_call_interrupt(void)
{
	/* Invoke generic function invocation code in smp.c */
	smp_call_function_interrupt();
}

void ipi_decode(struct smtc_ipi *pipi)
{
	void *arg_copy = pipi->arg;
	int type_copy = pipi->type;
	int dest_copy = pipi->dest;

	smtc_ipi_nq(&freeIPIq, pipi);
	switch (type_copy) {
	case SMTC_CLOCK_TICK:
		/* Invoke Clock "Interrupt" */
		ipi_timer_latch[dest_copy] = 0;
#ifdef SMTC_IDLE_HOOK_DEBUG
		clock_hang_reported[dest_copy] = 0;
#endif /* SMTC_IDLE_HOOK_DEBUG */
		local_timer_interrupt(0, NULL);
		break;
	case LINUX_SMP_IPI:
		switch ((int)arg_copy) {
		case SMP_RESCHEDULE_YOURSELF:
			ipi_resched_interrupt();
			break;
		case SMP_CALL_FUNCTION:
			ipi_call_interrupt();
			break;
		default:
			printk("Impossible SMTC IPI Argument 0x%x\n",
				(int)arg_copy);
			break;
		}
		break;
	default:
		printk("Impossible SMTC IPI Type 0x%x\n", type_copy);
		break;
	}
}

void deferred_smtc_ipi(void)
{
	struct smtc_ipi *pipi;
	unsigned long flags;
/* DEBUG */
	int q = smp_processor_id();

	/*
	 * Test is not atomic, but much faster than a dequeue,
	 * and the vast majority of invocations will have a null queue.
	 */
	if (IPIQ[q].head != NULL) {
		while((pipi = smtc_ipi_dq(&IPIQ[q])) != NULL) {
			/* ipi_decode() should be called with interrupts off */
			local_irq_save(flags);
			ipi_decode(pipi);
			local_irq_restore(flags);
		}
	}
}

/*
 * Send clock tick to all TCs except the one executing the funtion
 */

void smtc_timer_broadcast(int vpe)
{
	int cpu;
	int myTC = cpu_data[smp_processor_id()].tc_id;
	int myVPE = cpu_data[smp_processor_id()].vpe_id;

	smtc_cpu_stats[smp_processor_id()].timerints++;

	for_each_online_cpu(cpu) {
		if (cpu_data[cpu].vpe_id == myVPE &&
		    cpu_data[cpu].tc_id != myTC)
			smtc_send_ipi(cpu, SMTC_CLOCK_TICK, 0);
	}
}

/*
 * Cross-VPE interrupts in the SMTC prototype use "software interrupts"
 * set via cross-VPE MTTR manipulation of the Cause register. It would be
 * in some regards preferable to have external logic for "doorbell" hardware
 * interrupts.
 */

static int cpu_ipi_irq = MIPSCPU_INT_BASE + MIPS_CPU_IPI_IRQ;

static irqreturn_t ipi_interrupt(int irq, void *dev_idm)
{
	int my_vpe = cpu_data[smp_processor_id()].vpe_id;
	int my_tc = cpu_data[smp_processor_id()].tc_id;
	int cpu;
	struct smtc_ipi *pipi;
	unsigned long tcstatus;
	int sent;
	long flags;
	unsigned int mtflags;
	unsigned int vpflags;

	/*
	 * So long as cross-VPE interrupts are done via
	 * MFTR/MTTR read-modify-writes of Cause, we need
	 * to stop other VPEs whenever the local VPE does
	 * anything similar.
	 */
	local_irq_save(flags);
	vpflags = dvpe();
	clear_c0_cause(0x100 << MIPS_CPU_IPI_IRQ);
	set_c0_status(0x100 << MIPS_CPU_IPI_IRQ);
	irq_enable_hazard();
	evpe(vpflags);
	local_irq_restore(flags);

	/*
	 * Cross-VPE Interrupt handler: Try to directly deliver IPIs
	 * queued for TCs on this VPE other than the current one.
	 * Return-from-interrupt should cause us to drain the queue
	 * for the current TC, so we ought not to have to do it explicitly here.
	 */

	for_each_online_cpu(cpu) {
		if (cpu_data[cpu].vpe_id != my_vpe)
			continue;

		pipi = smtc_ipi_dq(&IPIQ[cpu]);
		if (pipi != NULL) {
			if (cpu_data[cpu].tc_id != my_tc) {
				sent = 0;
				LOCK_MT_PRA();
				settc(cpu_data[cpu].tc_id);
				write_tc_c0_tchalt(TCHALT_H);
				mips_ihb();
				tcstatus = read_tc_c0_tcstatus();
				if ((tcstatus & TCSTATUS_IXMT) == 0) {
					post_direct_ipi(cpu, pipi);
					sent = 1;
				}
				write_tc_c0_tchalt(0);
				UNLOCK_MT_PRA();
				if (!sent) {
					smtc_ipi_req(&IPIQ[cpu], pipi);
				}
			} else {
				/*
				 * ipi_decode() should be called
				 * with interrupts off
				 */
				local_irq_save(flags);
				ipi_decode(pipi);
				local_irq_restore(flags);
			}
		}
	}

	return IRQ_HANDLED;
}

static void ipi_irq_dispatch(void)
{
	do_IRQ(cpu_ipi_irq);
}

static struct irqaction irq_ipi;

void setup_cross_vpe_interrupts(void)
{
	if (!cpu_has_vint)
		panic("SMTC Kernel requires Vectored Interupt support");

	set_vi_handler(MIPS_CPU_IPI_IRQ, ipi_irq_dispatch);

	irq_ipi.handler = ipi_interrupt;
	irq_ipi.flags = IRQF_DISABLED;
	irq_ipi.name = "SMTC_IPI";

	setup_irq_smtc(cpu_ipi_irq, &irq_ipi, (0x100 << MIPS_CPU_IPI_IRQ));

	irq_desc[cpu_ipi_irq].status |= IRQ_PER_CPU;
	set_irq_handler(cpu_ipi_irq, handle_percpu_irq);
}

/*
 * SMTC-specific hacks invoked from elsewhere in the kernel.
 */

void smtc_idle_loop_hook(void)
{
#ifdef SMTC_IDLE_HOOK_DEBUG
	int im;
	int flags;
	int mtflags;
	int bit;
	int vpe;
	int tc;
	int hook_ntcs;
	/*
	 * printk within DMT-protected regions can deadlock,
	 * so buffer diagnostic messages for later output.
	 */
	char *pdb_msg;
	char id_ho_db_msg[768]; /* worst-case use should be less than 700 */

	if (atomic_read(&idle_hook_initialized) == 0) { /* fast test */
		if (atomic_add_return(1, &idle_hook_initialized) == 1) {
			int mvpconf0;
			/* Tedious stuff to just do once */
			mvpconf0 = read_c0_mvpconf0();
			hook_ntcs = ((mvpconf0 & MVPCONF0_PTC) >> MVPCONF0_PTC_SHIFT) + 1;
			if (hook_ntcs > NR_CPUS)
				hook_ntcs = NR_CPUS;
			for (tc = 0; tc < hook_ntcs; tc++) {
				tcnoprog[tc] = 0;
				clock_hang_reported[tc] = 0;
	    		}
			for (vpe = 0; vpe < 2; vpe++)
				for (im = 0; im < 8; im++)
					imstuckcount[vpe][im] = 0;
			printk("Idle loop test hook initialized for %d TCs\n", hook_ntcs);
			atomic_set(&idle_hook_initialized, 1000);
		} else {
			/* Someone else is initializing in parallel - let 'em finish */
			while (atomic_read(&idle_hook_initialized) < 1000)
				;
		}
	}

	/* Have we stupidly left IXMT set somewhere? */
	if (read_c0_tcstatus() & 0x400) {
		write_c0_tcstatus(read_c0_tcstatus() & ~0x400);
		ehb();
		printk("Dangling IXMT in cpu_idle()\n");
	}

	/* Have we stupidly left an IM bit turned off? */
#define IM_LIMIT 2000
	local_irq_save(flags);
	mtflags = dmt();
	pdb_msg = &id_ho_db_msg[0];
	im = read_c0_status();
	vpe = cpu_data[smp_processor_id()].vpe_id;
	for (bit = 0; bit < 8; bit++) {
		/*
		 * In current prototype, I/O interrupts
		 * are masked for VPE > 0
		 */
		if (vpemask[vpe][bit]) {
			if (!(im & (0x100 << bit)))
				imstuckcount[vpe][bit]++;
			else
				imstuckcount[vpe][bit] = 0;
			if (imstuckcount[vpe][bit] > IM_LIMIT) {
				set_c0_status(0x100 << bit);
				ehb();
				imstuckcount[vpe][bit] = 0;
				pdb_msg += sprintf(pdb_msg,
					"Dangling IM %d fixed for VPE %d\n", bit,
					vpe);
			}
		}
	}

	/*
	 * Now that we limit outstanding timer IPIs, check for hung TC
	 */
	for (tc = 0; tc < NR_CPUS; tc++) {
		/* Don't check ourself - we'll dequeue IPIs just below */
		if ((tc != smp_processor_id()) &&
		    ipi_timer_latch[tc] > timerq_limit) {
		    if (clock_hang_reported[tc] == 0) {
			pdb_msg += sprintf(pdb_msg,
				"TC %d looks hung with timer latch at %d\n",
				tc, ipi_timer_latch[tc]);
			clock_hang_reported[tc]++;
			}
		}
	}
	emt(mtflags);
	local_irq_restore(flags);
	if (pdb_msg != &id_ho_db_msg[0])
		printk("CPU%d: %s", smp_processor_id(), id_ho_db_msg);
#endif /* SMTC_IDLE_HOOK_DEBUG */
	/*
	 * To the extent that we've ever turned interrupts off,
	 * we may have accumulated deferred IPIs.  This is subtle.
	 * If we use the smtc_ipi_qdepth() macro, we'll get an
	 * exact number - but we'll also disable interrupts
	 * and create a window of failure where a new IPI gets
	 * queued after we test the depth but before we re-enable
	 * interrupts. So long as IXMT never gets set, however,
	 * we should be OK:  If we pick up something and dispatch
	 * it here, that's great. If we see nothing, but concurrent
	 * with this operation, another TC sends us an IPI, IXMT
	 * is clear, and we'll handle it as a real pseudo-interrupt
	 * and not a pseudo-pseudo interrupt.
	 */
	if (IPIQ[smp_processor_id()].depth > 0) {
		struct smtc_ipi *pipi;
		extern void self_ipi(struct smtc_ipi *);

		if ((pipi = smtc_ipi_dq(&IPIQ[smp_processor_id()])) != NULL) {
			self_ipi(pipi);
			smtc_cpu_stats[smp_processor_id()].selfipis++;
		}
	}
}

void smtc_soft_dump(void)
{
	int i;

	printk("Counter Interrupts taken per CPU (TC)\n");
	for (i=0; i < NR_CPUS; i++) {
		printk("%d: %ld\n", i, smtc_cpu_stats[i].timerints);
	}
	printk("Self-IPI invocations:\n");
	for (i=0; i < NR_CPUS; i++) {
		printk("%d: %ld\n", i, smtc_cpu_stats[i].selfipis);
	}
	smtc_ipi_qdump();
	printk("Timer IPI Backlogs:\n");
	for (i=0; i < NR_CPUS; i++) {
		printk("%d: %d\n", i, ipi_timer_latch[i]);
	}
	printk("%d Recoveries of \"stolen\" FPU\n",
	       atomic_read(&smtc_fpu_recoveries));
}


/*
 * TLB management routines special to SMTC
 */

void smtc_get_new_mmu_context(struct mm_struct *mm, unsigned long cpu)
{
	unsigned long flags, mtflags, tcstat, prevhalt, asid;
	int tlb, i;

	/*
	 * It would be nice to be able to use a spinlock here,
	 * but this is invoked from within TLB flush routines
	 * that protect themselves with DVPE, so if a lock is
         * held by another TC, it'll never be freed.
	 *
	 * DVPE/DMT must not be done with interrupts enabled,
	 * so even so most callers will already have disabled
	 * them, let's be really careful...
	 */

	local_irq_save(flags);
	if (smtc_status & SMTC_TLB_SHARED) {
		mtflags = dvpe();
		tlb = 0;
	} else {
		mtflags = dmt();
		tlb = cpu_data[cpu].vpe_id;
	}
	asid = asid_cache(cpu);

	do {
		if (!((asid += ASID_INC) & ASID_MASK) ) {
			if (cpu_has_vtag_icache)
				flush_icache_all();
			/* Traverse all online CPUs (hack requires contigous range) */
			for (i = 0; i < num_online_cpus(); i++) {
				/*
				 * We don't need to worry about our own CPU, nor those of
				 * CPUs who don't share our TLB.
				 */
				if ((i != smp_processor_id()) &&
				    ((smtc_status & SMTC_TLB_SHARED) ||
				     (cpu_data[i].vpe_id == cpu_data[cpu].vpe_id))) {
					settc(cpu_data[i].tc_id);
					prevhalt = read_tc_c0_tchalt() & TCHALT_H;
					if (!prevhalt) {
						write_tc_c0_tchalt(TCHALT_H);
						mips_ihb();
					}
					tcstat = read_tc_c0_tcstatus();
					smtc_live_asid[tlb][(tcstat & ASID_MASK)] |= (asiduse)(0x1 << i);
					if (!prevhalt)
						write_tc_c0_tchalt(0);
				}
			}
			if (!asid)		/* fix version if needed */
				asid = ASID_FIRST_VERSION;
			local_flush_tlb_all();	/* start new asid cycle */
		}
	} while (smtc_live_asid[tlb][(asid & ASID_MASK)]);

	/*
	 * SMTC shares the TLB within VPEs and possibly across all VPEs.
	 */
	for (i = 0; i < num_online_cpus(); i++) {
		if ((smtc_status & SMTC_TLB_SHARED) ||
		    (cpu_data[i].vpe_id == cpu_data[cpu].vpe_id))
			cpu_context(i, mm) = asid_cache(i) = asid;
	}

	if (smtc_status & SMTC_TLB_SHARED)
		evpe(mtflags);
	else
		emt(mtflags);
	local_irq_restore(flags);
}

/*
 * Invoked from macros defined in mmu_context.h
 * which must already have disabled interrupts
 * and done a DVPE or DMT as appropriate.
 */

void smtc_flush_tlb_asid(unsigned long asid)
{
	int entry;
	unsigned long ehi;

	entry = read_c0_wired();

	/* Traverse all non-wired entries */
	while (entry < current_cpu_data.tlbsize) {
		write_c0_index(entry);
		ehb();
		tlb_read();
		ehb();
		ehi = read_c0_entryhi();
		if ((ehi & ASID_MASK) == asid) {
		    /*
		     * Invalidate only entries with specified ASID,
		     * makiing sure all entries differ.
		     */
		    write_c0_entryhi(CKSEG0 + (entry << (PAGE_SHIFT + 1)));
		    write_c0_entrylo0(0);
		    write_c0_entrylo1(0);
		    mtc0_tlbw_hazard();
		    tlb_write_indexed();
		}
		entry++;
	}
	write_c0_index(PARKED_INDEX);
	tlbw_use_hazard();
}

/*
 * Support for single-threading cache flush operations.
 */

int halt_state_save[NR_CPUS];

/*
 * To really, really be sure that nothing is being done
 * by other TCs, halt them all.  This code assumes that
 * a DVPE has already been done, so while their Halted
 * state is theoretically architecturally unstable, in
 * practice, it's not going to change while we're looking
 * at it.
 */

void smtc_cflush_lockdown(void)
{
	int cpu;

	for_each_online_cpu(cpu) {
		if (cpu != smp_processor_id()) {
			settc(cpu_data[cpu].tc_id);
			halt_state_save[cpu] = read_tc_c0_tchalt();
			write_tc_c0_tchalt(TCHALT_H);
		}
	}
	mips_ihb();
}

/* It would be cheating to change the cpu_online states during a flush! */

void smtc_cflush_release(void)
{
	int cpu;

	/*
	 * Start with a hazard barrier to ensure
	 * that all CACHE ops have played through.
	 */
	mips_ihb();

	for_each_online_cpu(cpu) {
		if (cpu != smp_processor_id()) {
			settc(cpu_data[cpu].tc_id);
			write_tc_c0_tchalt(halt_state_save[cpu]);
		}
	}
	mips_ihb();
}
