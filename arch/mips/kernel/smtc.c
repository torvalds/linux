/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 * Copyright (C) 2004 Mips Technologies, Inc
 * Copyright (C) 2008 Kevin D. Kissell
 */

#include <linux/clockchips.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/cpumask.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/module.h>

#include <asm/cpu.h>
#include <asm/processor.h>
#include <asm/atomic.h>
#include <asm/system.h>
#include <asm/hardirq.h>
#include <asm/hazards.h>
#include <asm/irq.h>
#include <asm/mmu_context.h>
#include <asm/mipsregs.h>
#include <asm/cacheflush.h>
#include <asm/time.h>
#include <asm/addrspace.h>
#include <asm/smtc.h>
#include <asm/smtc_proc.h>

/*
 * SMTC Kernel needs to manipulate low-level CPU interrupt mask
 * in do_IRQ. These are passed in setup_irq_smtc() and stored
 * in this table.
 */
unsigned long irq_hwmask[NR_IRQS];

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
 * Number of InterProcessor Interrupt (IPI) message buffers to allocate
 */

#define IPIBUF_PER_CPU 4

struct smtc_ipi_q IPIQ[NR_CPUS];
static struct smtc_ipi_q freeIPIq;


/* Forward declarations */

void ipi_decode(struct smtc_ipi *);
static void post_direct_ipi(int cpu, struct smtc_ipi *pipi);
static void setup_cross_vpe_interrupts(unsigned int nvpe);
void init_smtc_stats(void);

/* Global SMTC Status */

unsigned int smtc_status = 0;

/* Boot command line configuration overrides */

static int vpe0limit;
static int ipibuffers = 0;
static int nostlb = 0;
static int asidmask = 0;
unsigned long smtc_asid_mask = 0xff;

static int __init vpe0tcs(char *str)
{
	get_option(&str, &vpe0limit);

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

__setup("vpe0tcs=", vpe0tcs);
__setup("ipibufs=", ipibufs);
__setup("nostlb", stlb_disable);
__setup("asidmask=", asidmask_set);

#ifdef CONFIG_SMTC_IDLE_HOOK_DEBUG

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

static int imstuckcount[2][8];
/* vpemask represents IM/IE bits of per-VPE Status registers, low-to-high */
static int vpemask[2][8] = {
	{0, 0, 1, 0, 0, 0, 0, 1},
	{0, 0, 0, 0, 0, 0, 0, 1}
};
int tcnoprog[NR_CPUS];
static atomic_t idle_hook_initialized = {0};
static int clock_hang_reported[NR_CPUS];

#endif /* CONFIG_SMTC_IDLE_HOOK_DEBUG */

/*
 * Configure shared TLB - VPC configuration bit must be set by caller
 */

static void smtc_configure_tlb(void)
{
	int i, tlbsiz, vpes;
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
		ehb();

		/*
		 * Setup kernel data structures to use software total,
		 * rather than read the per-VPE Config1 value. The values
		 * for "CPU 0" gets copied to all the other CPUs as part
		 * of their initialization in smtc_cpu_setup().
		 */

		/* MIPS32 limits TLB indices to 64 */
		if (tlbsiz > 64)
			tlbsiz = 64;
		cpu_data[0].tlbsize = current_cpu_data.tlbsize = tlbsiz;
		smtc_status |= SMTC_TLB_SHARED;
		local_flush_tlb_all();

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

int __init smtc_build_cpu_map(int start_cpu_slot)
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
#ifdef CONFIG_MIPS_MT_FPAFF
	/* Initialize map of CPUs with FPUs */
	cpus_clear(mt_fpu_cpumask);
#endif

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
	/*
	 * TCContext gets an offset from the base of the IPIQ array
	 * to be used in low-level code to detect the presence of
	 * an active IPI queue
	 */
	write_tc_c0_tccontext((sizeof(struct smtc_ipi_q) * cpu) << 16);
	/* Bind tc to vpe */
	write_tc_c0_tcbind(vpe);
	/* In general, all TCs should have the same cpu_data indications */
	memcpy(&cpu_data[cpu], &cpu_data[0], sizeof(struct cpuinfo_mips));
	/* For 34Kf, start with TC/CPU 0 as sole owner of single FPU context */
	if (cpu_data[0].cputype == CPU_34K ||
	    cpu_data[0].cputype == CPU_1004K)
		cpu_data[cpu].options &= ~MIPS_CPU_FPU;
	cpu_data[cpu].vpe_id = vpe;
	cpu_data[cpu].tc_id = tc;
	/* Multi-core SMTC hasn't been tested, but be prepared */
	cpu_data[cpu].core = (read_vpe_c0_ebase() >> 1) & 0xff;
}

/*
 * Tweak to get Count registes in as close a sync as possible.
 * Value seems good for 34K-class cores.
 */

#define CP0_SKEW 8

void smtc_prepare_cpus(int cpus)
{
	int i, vpe, tc, ntc, nvpe, tcpervpe[NR_CPUS], slop, cpu;
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
	}

	/* cpu_data index starts at zero */
	cpu = 0;
	cpu_data[cpu].vpe_id = 0;
	cpu_data[cpu].tc_id = 0;
	cpu_data[cpu].core = (read_c0_ebase() >> 1) & 0xff;
	cpu++;

	/* Report on boot-time options */
	mips_mt_set_cpuoptions();
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
#ifdef CONFIG_SMTC_IDLE_HOOK_DEBUG
	if (hang_trig)
		printk("Logic Analyser Trigger on suspected TC hang\n");
#endif /* CONFIG_SMTC_IDLE_HOOK_DEBUG */

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
	slop = ntc % nvpe;
	for (i = 0; i < nvpe; i++) {
		tcpervpe[i] = ntc / nvpe;
		if (slop) {
			if((slop - i) > 0) tcpervpe[i]++;
		}
	}
	/* Handle command line override for VPE0 */
	if (vpe0limit > ntc) vpe0limit = ntc;
	if (vpe0limit > 0) {
		int slopslop;
		if (vpe0limit < tcpervpe[0]) {
		    /* Reducing TC count - distribute to others */
		    slop = tcpervpe[0] - vpe0limit;
		    slopslop = slop % (nvpe - 1);
		    tcpervpe[0] = vpe0limit;
		    for (i = 1; i < nvpe; i++) {
			tcpervpe[i] += slop / (nvpe - 1);
			if(slopslop && ((slopslop - (i - 1) > 0)))
				tcpervpe[i]++;
		    }
		} else if (vpe0limit > tcpervpe[0]) {
		    /* Increasing TC count - steal from others */
		    slop = vpe0limit - tcpervpe[0];
		    slopslop = slop % (nvpe - 1);
		    tcpervpe[0] = vpe0limit;
		    for (i = 1; i < nvpe; i++) {
			tcpervpe[i] -= slop / (nvpe - 1);
			if(slopslop && ((slopslop - (i - 1) > 0)))
				tcpervpe[i]--;
		    }
		}
	}

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
		for (i = 0; i < tcpervpe[vpe]; i++) {
			/*
			 * TC 0 is bound to VPE 0 at reset,
			 * and is presumably executing this
			 * code.  Leave it alone!
			 */
			if (tc != 0) {
				smtc_tc_setup(vpe, tc, cpu);
				cpu++;
			}
			printk(" %d", tc);
			tc++;
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
			write_vpe_c0_count(read_c0_count() + CP0_SKEW);
			ehb();
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

#ifdef CONFIG_MIPS_MT_FPAFF
	for (tc = 0; tc < ntc; tc++) {
		if (cpu_data[tc].options & MIPS_CPU_FPU)
			cpu_set(tc, mt_fpu_cpumask);
	}
#endif

	/* set up ipi interrupts... */

	/* If we have multiple VPEs running, set up the cross-VPE interrupt */

	setup_cross_vpe_interrupts(nvpe);

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
void __cpuinit smtc_boot_secondary(int cpu, struct task_struct *idle)
{
	extern u32 kernelsp[NR_CPUS];
	unsigned long flags;
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
	write_tc_gpr_gp((unsigned long)task_thread_info(idle));

	smtc_status |= SMTC_MTC_ACTIVE;
	write_tc_c0_tchalt(0);
	if (cpu_data[cpu].vpe_id != cpu_data[smp_processor_id()].vpe_id) {
		evpe(EVPE_ENABLE);
	}
	UNLOCK_MT_PRA();
}

void smtc_init_secondary(void)
{
	local_irq_enable();
}

void smtc_smp_finish(void)
{
	int cpu = smp_processor_id();

	/*
	 * Lowest-numbered CPU per VPE starts a clock tick.
	 * Like per_cpu_trap_init() hack, this assumes that
	 * SMTC init code assigns TCs consdecutively and
	 * in ascending order across available VPEs.
	 */
	if (cpu > 0 && (cpu_data[cpu].vpe_id != cpu_data[cpu - 1].vpe_id))
		write_c0_compare(read_c0_count() + mips_hpt_frequency/HZ);

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
#ifdef CONFIG_SMTC_IDLE_HOOK_DEBUG
	unsigned int vpe = current_cpu_data.vpe_id;

	vpemask[vpe][irq - MIPS_CPU_IRQ_BASE] = 1;
#endif
	irq_hwmask[irq] = hwmask;

	return setup_irq(irq, new);
}

#ifdef CONFIG_MIPS_MT_SMTC_IRQAFF
/*
 * Support for IRQ affinity to TCs
 */

void smtc_set_irq_affinity(unsigned int irq, cpumask_t affinity)
{
	/*
	 * If a "fast path" cache of quickly decodable affinity state
	 * is maintained, this is where it gets done, on a call up
	 * from the platform affinity code.
	 */
}

void smtc_forward_irq(unsigned int irq)
{
	int target;

	/*
	 * OK wise guy, now figure out how to get the IRQ
	 * to be serviced on an authorized "CPU".
	 *
	 * Ideally, to handle the situation where an IRQ has multiple
	 * eligible CPUS, we would maintain state per IRQ that would
	 * allow a fair distribution of service requests.  Since the
	 * expected use model is any-or-only-one, for simplicity
	 * and efficiency, we just pick the easiest one to find.
	 */

	target = first_cpu(irq_desc[irq].affinity);

	/*
	 * We depend on the platform code to have correctly processed
	 * IRQ affinity change requests to ensure that the IRQ affinity
	 * mask has been purged of bits corresponding to nonexistent and
	 * offline "CPUs", and to TCs bound to VPEs other than the VPE
	 * connected to the physical interrupt input for the interrupt
	 * in question.  Otherwise we have a nasty problem with interrupt
	 * mask management.  This is best handled in non-performance-critical
	 * platform IRQ affinity setting code,  to minimize interrupt-time
	 * checks.
	 */

	/* If no one is eligible, service locally */
	if (target >= NR_CPUS) {
		do_IRQ_no_affinity(irq);
		return;
	}

	smtc_send_ipi(target, IRQ_AFFINITY_IPI, irq);
}

#endif /* CONFIG_MIPS_MT_SMTC_IRQAFF */

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

static void smtc_ipi_qdump(void)
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
static inline int atomic_postincrement(atomic_t *v)
{
	unsigned long result;

	unsigned long temp;

	__asm__ __volatile__(
	"1:	ll	%0, %2					\n"
	"	addu	%1, %0, 1				\n"
	"	sc	%1, %2					\n"
	"	beqz	%1, 1b					\n"
	__WEAK_LLSC_MB
	: "=&r" (result), "=&r" (temp), "=m" (v->counter)
	: "m" (v->counter)
	: "memory");

	return result;
}

void smtc_send_ipi(int cpu, int type, unsigned int action)
{
	int tcstatus;
	struct smtc_ipi *pipi;
	unsigned long flags;
	int mtflags;
	unsigned long tcrestart;
	extern void r4k_wait_irqoff(void), __pastwait(void);

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
		/* If not on same VPE, enqueue and send cross-VPE interrupt */
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
			 * If we're in the the irq-off version of the wait
			 * loop, we need to force exit from the wait and
			 * do a direct post of the IPI.
			 */
			if (cpu_wait == r4k_wait_irqoff) {
				tcrestart = read_tc_c0_tcrestart();
				if (tcrestart >= (unsigned long)r4k_wait_irqoff
				    && tcrestart < (unsigned long)__pastwait) {
					write_tc_c0_tcrestart(__pastwait);
					tcstatus &= ~TCSTATUS_IXMT;
					write_tc_c0_tcstatus(tcstatus);
					goto postdirect;
				}
			}
			/*
			 * Otherwise we queue the message for the target TC
			 * to pick up when he does a local_irq_restore()
			 */
			write_tc_c0_tchalt(0);
			UNLOCK_CORE_PRA();
			smtc_ipi_nq(&IPIQ[cpu], pipi);
		} else {
postdirect:
			post_direct_ipi(cpu, pipi);
			write_tc_c0_tchalt(0);
			UNLOCK_CORE_PRA();
		}
	}
}

/*
 * Send IPI message to Halted TC, TargTC/TargVPE already having been set
 */
static void post_direct_ipi(int cpu, struct smtc_ipi *pipi)
{
	struct pt_regs *kstack;
	unsigned long tcstatus;
	unsigned long tcrestart;
	extern u32 kernelsp[NR_CPUS];
	extern void __smtc_ipi_vector(void);
//printk("%s: on %d for %d\n", __func__, smp_processor_id(), cpu);

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

DECLARE_PER_CPU(struct clock_event_device, mips_clockevent_device);

void ipi_decode(struct smtc_ipi *pipi)
{
	unsigned int cpu = smp_processor_id();
	struct clock_event_device *cd;
	void *arg_copy = pipi->arg;
	int type_copy = pipi->type;
	smtc_ipi_nq(&freeIPIq, pipi);
	switch (type_copy) {
	case SMTC_CLOCK_TICK:
		irq_enter();
		kstat_this_cpu.irqs[MIPS_CPU_IRQ_BASE + 1]++;
		cd = &per_cpu(mips_clockevent_device, cpu);
		cd->event_handler(cd);
		irq_exit();
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
#ifdef CONFIG_MIPS_MT_SMTC_IRQAFF
	case IRQ_AFFINITY_IPI:
		/*
		 * Accept a "forwarded" interrupt that was initially
		 * taken by a TC who doesn't have affinity for the IRQ.
		 */
		do_IRQ_no_affinity((int)arg_copy);
		break;
#endif /* CONFIG_MIPS_MT_SMTC_IRQAFF */
	default:
		printk("Impossible SMTC IPI Type 0x%x\n", type_copy);
		break;
	}
}

/*
 * Similar to smtc_ipi_replay(), but invoked from context restore,
 * so it reuses the current exception frame rather than set up a
 * new one with self_ipi.
 */

void deferred_smtc_ipi(void)
{
	int cpu = smp_processor_id();

	/*
	 * Test is not atomic, but much faster than a dequeue,
	 * and the vast majority of invocations will have a null queue.
	 * If irq_disabled when this was called, then any IPIs queued
	 * after we test last will be taken on the next irq_enable/restore.
	 * If interrupts were enabled, then any IPIs added after the
	 * last test will be taken directly.
	 */

	while (IPIQ[cpu].head != NULL) {
		struct smtc_ipi_q *q = &IPIQ[cpu];
		struct smtc_ipi *pipi;
		unsigned long flags;

		/*
		 * It may be possible we'll come in with interrupts
		 * already enabled.
		 */
		local_irq_save(flags);

		spin_lock(&q->lock);
		pipi = __smtc_ipi_dq(q);
		spin_unlock(&q->lock);
		if (pipi != NULL)
			ipi_decode(pipi);
		/*
		 * The use of the __raw_local restore isn't
		 * as obviously necessary here as in smtc_ipi_replay(),
		 * but it's more efficient, given that we're already
		 * running down the IPI queue.
		 */
		__raw_local_irq_restore(flags);
	}
}

/*
 * Cross-VPE interrupts in the SMTC prototype use "software interrupts"
 * set via cross-VPE MTTR manipulation of the Cause register. It would be
 * in some regards preferable to have external logic for "doorbell" hardware
 * interrupts.
 */

static int cpu_ipi_irq = MIPS_CPU_IRQ_BASE + MIPS_CPU_IPI_IRQ;

static irqreturn_t ipi_interrupt(int irq, void *dev_idm)
{
	int my_vpe = cpu_data[smp_processor_id()].vpe_id;
	int my_tc = cpu_data[smp_processor_id()].tc_id;
	int cpu;
	struct smtc_ipi *pipi;
	unsigned long tcstatus;
	int sent;
	unsigned long flags;
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

static struct irqaction irq_ipi = {
	.handler	= ipi_interrupt,
	.flags		= IRQF_DISABLED,
	.name		= "SMTC_IPI",
	.flags		= IRQF_PERCPU
};

static void setup_cross_vpe_interrupts(unsigned int nvpe)
{
	if (nvpe < 1)
		return;

	if (!cpu_has_vint)
		panic("SMTC Kernel requires Vectored Interrupt support");

	set_vi_handler(MIPS_CPU_IPI_IRQ, ipi_irq_dispatch);

	setup_irq_smtc(cpu_ipi_irq, &irq_ipi, (0x100 << MIPS_CPU_IPI_IRQ));

	set_irq_handler(cpu_ipi_irq, handle_percpu_irq);
}

/*
 * SMTC-specific hacks invoked from elsewhere in the kernel.
 */

 /*
  * smtc_ipi_replay is called from raw_local_irq_restore
  */

void smtc_ipi_replay(void)
{
	unsigned int cpu = smp_processor_id();

	/*
	 * To the extent that we've ever turned interrupts off,
	 * we may have accumulated deferred IPIs.  This is subtle.
	 * we should be OK:  If we pick up something and dispatch
	 * it here, that's great. If we see nothing, but concurrent
	 * with this operation, another TC sends us an IPI, IXMT
	 * is clear, and we'll handle it as a real pseudo-interrupt
	 * and not a pseudo-pseudo interrupt.  The important thing
	 * is to do the last check for queued message *after* the
	 * re-enabling of interrupts.
	 */
	while (IPIQ[cpu].head != NULL) {
		struct smtc_ipi_q *q = &IPIQ[cpu];
		struct smtc_ipi *pipi;
		unsigned long flags;

		/*
		 * It's just possible we'll come in with interrupts
		 * already enabled.
		 */
		local_irq_save(flags);

		spin_lock(&q->lock);
		pipi = __smtc_ipi_dq(q);
		spin_unlock(&q->lock);
		/*
		 ** But use a raw restore here to avoid recursion.
		 */
		__raw_local_irq_restore(flags);

		if (pipi) {
			self_ipi(pipi);
			smtc_cpu_stats[cpu].selfipis++;
		}
	}
}

EXPORT_SYMBOL(smtc_ipi_replay);

void smtc_idle_loop_hook(void)
{
#ifdef CONFIG_SMTC_IDLE_HOOK_DEBUG
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
	vpe = current_cpu_data.vpe_id;
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

	emt(mtflags);
	local_irq_restore(flags);
	if (pdb_msg != &id_ho_db_msg[0])
		printk("CPU%d: %s", smp_processor_id(), id_ho_db_msg);
#endif /* CONFIG_SMTC_IDLE_HOOK_DEBUG */

	smtc_ipi_replay();
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
			for_each_online_cpu(i) {
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
	for_each_online_cpu(i) {
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

static int halt_state_save[NR_CPUS];

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
