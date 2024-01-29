/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2011 by Kevin Cernekee (cernekee@gmail.com)
 *
 * SMP support for BMIPS
 */

#include <linux/init.h>
#include <linux/sched.h>
#include <linux/sched/hotplug.h>
#include <linux/sched/task_stack.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/reboot.h>
#include <linux/io.h>
#include <linux/compiler.h>
#include <linux/linkage.h>
#include <linux/bug.h>
#include <linux/kernel.h>
#include <linux/kexec.h>
#include <linux/irq.h>

#include <asm/time.h>
#include <asm/processor.h>
#include <asm/bootinfo.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/mipsregs.h>
#include <asm/bmips.h>
#include <asm/traps.h>
#include <asm/barrier.h>
#include <asm/cpu-features.h>

static int __maybe_unused max_cpus = 1;

/* these may be configured by the platform code */
int bmips_smp_enabled = 1;
int bmips_cpu_offset;
cpumask_t bmips_booted_mask;
unsigned long bmips_tp1_irqs = IE_IRQ1;

#define RESET_FROM_KSEG0		0x80080800
#define RESET_FROM_KSEG1		0xa0080800

static void bmips_set_reset_vec(int cpu, u32 val);

#ifdef CONFIG_SMP

#include <asm/smp.h>

/* initial $sp, $gp - used by arch/mips/kernel/bmips_vec.S */
unsigned long bmips_smp_boot_sp;
unsigned long bmips_smp_boot_gp;

static void bmips43xx_send_ipi_single(int cpu, unsigned int action);
static void bmips5000_send_ipi_single(int cpu, unsigned int action);
static irqreturn_t bmips43xx_ipi_interrupt(int irq, void *dev_id);
static irqreturn_t bmips5000_ipi_interrupt(int irq, void *dev_id);

/* SW interrupts 0,1 are used for interprocessor signaling */
#define IPI0_IRQ			(MIPS_CPU_IRQ_BASE + 0)
#define IPI1_IRQ			(MIPS_CPU_IRQ_BASE + 1)

#define CPUNUM(cpu, shift)		(((cpu) + bmips_cpu_offset) << (shift))
#define ACTION_CLR_IPI(cpu, ipi)	(0x2000 | CPUNUM(cpu, 9) | ((ipi) << 8))
#define ACTION_SET_IPI(cpu, ipi)	(0x3000 | CPUNUM(cpu, 9) | ((ipi) << 8))
#define ACTION_BOOT_THREAD(cpu)		(0x08 | CPUNUM(cpu, 0))

static void __init bmips_smp_setup(void)
{
	int i, cpu = 1, boot_cpu = 0;
	int cpu_hw_intr;

	switch (current_cpu_type()) {
	case CPU_BMIPS4350:
	case CPU_BMIPS4380:
		/* arbitration priority */
		clear_c0_brcm_cmt_ctrl(0x30);

		/* NBK and weak order flags */
		set_c0_brcm_config_0(0x30000);

		/* Find out if we are running on TP0 or TP1 */
		boot_cpu = !!(read_c0_brcm_cmt_local() & (1 << 31));

		/*
		 * MIPS interrupts 0,1 (SW INT 0,1) cross over to the other
		 * thread
		 * MIPS interrupt 2 (HW INT 0) is the CPU0 L1 controller output
		 * MIPS interrupt 3 (HW INT 1) is the CPU1 L1 controller output
		 */
		if (boot_cpu == 0)
			cpu_hw_intr = 0x02;
		else
			cpu_hw_intr = 0x1d;

		change_c0_brcm_cmt_intr(0xf8018000,
					(cpu_hw_intr << 27) | (0x03 << 15));

		/* single core, 2 threads (2 pipelines) */
		max_cpus = 2;

		break;
	case CPU_BMIPS5000:
		/* enable raceless SW interrupts */
		set_c0_brcm_config(0x03 << 22);

		/* route HW interrupt 0 to CPU0, HW interrupt 1 to CPU1 */
		change_c0_brcm_mode(0x1f << 27, 0x02 << 27);

		/* N cores, 2 threads per core */
		max_cpus = (((read_c0_brcm_config() >> 6) & 0x03) + 1) << 1;

		/* clear any pending SW interrupts */
		for (i = 0; i < max_cpus; i++) {
			write_c0_brcm_action(ACTION_CLR_IPI(i, 0));
			write_c0_brcm_action(ACTION_CLR_IPI(i, 1));
		}

		break;
	default:
		max_cpus = 1;
	}

	if (!bmips_smp_enabled)
		max_cpus = 1;

	/* this can be overridden by the BSP */
	if (!board_ebase_setup)
		board_ebase_setup = &bmips_ebase_setup;

	if (max_cpus > 1) {
		__cpu_number_map[boot_cpu] = 0;
		__cpu_logical_map[0] = boot_cpu;

		for (i = 0; i < max_cpus; i++) {
			if (i != boot_cpu) {
				__cpu_number_map[i] = cpu;
				__cpu_logical_map[cpu] = i;
				cpu++;
			}
			set_cpu_possible(i, 1);
			set_cpu_present(i, 1);
		}
	} else {
		__cpu_number_map[0] = boot_cpu;
		__cpu_logical_map[0] = 0;
		set_cpu_possible(0, 1);
		set_cpu_present(0, 1);
	}
}

/*
 * IPI IRQ setup - runs on CPU0
 */
static void bmips_prepare_cpus(unsigned int max_cpus)
{
	irqreturn_t (*bmips_ipi_interrupt)(int irq, void *dev_id);

	switch (current_cpu_type()) {
	case CPU_BMIPS4350:
	case CPU_BMIPS4380:
		bmips_ipi_interrupt = bmips43xx_ipi_interrupt;
		break;
	case CPU_BMIPS5000:
		bmips_ipi_interrupt = bmips5000_ipi_interrupt;
		break;
	default:
		return;
	}

	if (request_irq(IPI0_IRQ, bmips_ipi_interrupt,
			IRQF_PERCPU | IRQF_NO_SUSPEND, "smp_ipi0", NULL))
		panic("Can't request IPI0 interrupt");
	if (request_irq(IPI1_IRQ, bmips_ipi_interrupt,
			IRQF_PERCPU | IRQF_NO_SUSPEND, "smp_ipi1", NULL))
		panic("Can't request IPI1 interrupt");
}

/*
 * Tell the hardware to boot CPUx - runs on CPU0
 */
static int bmips_boot_secondary(int cpu, struct task_struct *idle)
{
	bmips_smp_boot_sp = __KSTK_TOS(idle);
	bmips_smp_boot_gp = (unsigned long)task_thread_info(idle);
	mb();

	/*
	 * Initial boot sequence for secondary CPU:
	 *   bmips_reset_nmi_vec @ a000_0000 ->
	 *   bmips_smp_entry ->
	 *   plat_wired_tlb_setup (cached function call; optional) ->
	 *   start_secondary (cached jump)
	 *
	 * Warm restart sequence:
	 *   play_dead WAIT loop ->
	 *   bmips_smp_int_vec @ BMIPS_WARM_RESTART_VEC ->
	 *   eret to play_dead ->
	 *   bmips_secondary_reentry ->
	 *   start_secondary
	 */

	pr_info("SMP: Booting CPU%d...\n", cpu);

	if (cpumask_test_cpu(cpu, &bmips_booted_mask)) {
		/* kseg1 might not exist if this CPU enabled XKS01 */
		bmips_set_reset_vec(cpu, RESET_FROM_KSEG0);

		switch (current_cpu_type()) {
		case CPU_BMIPS4350:
		case CPU_BMIPS4380:
			bmips43xx_send_ipi_single(cpu, 0);
			break;
		case CPU_BMIPS5000:
			bmips5000_send_ipi_single(cpu, 0);
			break;
		}
	} else {
		bmips_set_reset_vec(cpu, RESET_FROM_KSEG1);

		switch (current_cpu_type()) {
		case CPU_BMIPS4350:
		case CPU_BMIPS4380:
			/* Reset slave TP1 if booting from TP0 */
			if (cpu_logical_map(cpu) == 1)
				set_c0_brcm_cmt_ctrl(0x01);
			break;
		case CPU_BMIPS5000:
			write_c0_brcm_action(ACTION_BOOT_THREAD(cpu));
			break;
		}
		cpumask_set_cpu(cpu, &bmips_booted_mask);
	}

	return 0;
}

/*
 * Early setup - runs on secondary CPU after cache probe
 */
static void bmips_init_secondary(void)
{
	bmips_cpu_setup();

	switch (current_cpu_type()) {
	case CPU_BMIPS4350:
	case CPU_BMIPS4380:
		clear_c0_cause(smp_processor_id() ? C_SW1 : C_SW0);
		break;
	case CPU_BMIPS5000:
		write_c0_brcm_action(ACTION_CLR_IPI(smp_processor_id(), 0));
		cpu_set_core(&current_cpu_data, (read_c0_brcm_config() >> 25) & 3);
		break;
	}
}

/*
 * Late setup - runs on secondary CPU before entering the idle loop
 */
static void bmips_smp_finish(void)
{
	pr_info("SMP: CPU%d is running\n", smp_processor_id());

	/* make sure there won't be a timer interrupt for a little while */
	write_c0_compare(read_c0_count() + mips_hpt_frequency / HZ);

	irq_enable_hazard();
	set_c0_status(IE_SW0 | IE_SW1 | bmips_tp1_irqs | IE_IRQ5 | ST0_IE);
	irq_enable_hazard();
}

/*
 * BMIPS5000 raceless IPIs
 *
 * Each CPU has two inbound SW IRQs which are independent of all other CPUs.
 * IPI0 is used for SMP_RESCHEDULE_YOURSELF
 * IPI1 is used for SMP_CALL_FUNCTION
 */

static void bmips5000_send_ipi_single(int cpu, unsigned int action)
{
	write_c0_brcm_action(ACTION_SET_IPI(cpu, action == SMP_CALL_FUNCTION));
}

static irqreturn_t bmips5000_ipi_interrupt(int irq, void *dev_id)
{
	int action = irq - IPI0_IRQ;

	write_c0_brcm_action(ACTION_CLR_IPI(smp_processor_id(), action));

	if (action == 0)
		scheduler_ipi();
	else
		generic_smp_call_function_interrupt();

	return IRQ_HANDLED;
}

static void bmips5000_send_ipi_mask(const struct cpumask *mask,
	unsigned int action)
{
	unsigned int i;

	for_each_cpu(i, mask)
		bmips5000_send_ipi_single(i, action);
}

/*
 * BMIPS43xx racey IPIs
 *
 * We use one inbound SW IRQ for each CPU.
 *
 * A spinlock must be held in order to keep CPUx from accidentally clearing
 * an incoming IPI when it writes CP0 CAUSE to raise an IPI on CPUy.  The
 * same spinlock is used to protect the action masks.
 */

static DEFINE_SPINLOCK(ipi_lock);
static DEFINE_PER_CPU(int, ipi_action_mask);

static void bmips43xx_send_ipi_single(int cpu, unsigned int action)
{
	unsigned long flags;

	spin_lock_irqsave(&ipi_lock, flags);
	set_c0_cause(cpu ? C_SW1 : C_SW0);
	per_cpu(ipi_action_mask, cpu) |= action;
	irq_enable_hazard();
	spin_unlock_irqrestore(&ipi_lock, flags);
}

static irqreturn_t bmips43xx_ipi_interrupt(int irq, void *dev_id)
{
	unsigned long flags;
	int action, cpu = irq - IPI0_IRQ;

	spin_lock_irqsave(&ipi_lock, flags);
	action = __this_cpu_read(ipi_action_mask);
	per_cpu(ipi_action_mask, cpu) = 0;
	clear_c0_cause(cpu ? C_SW1 : C_SW0);
	spin_unlock_irqrestore(&ipi_lock, flags);

	if (action & SMP_RESCHEDULE_YOURSELF)
		scheduler_ipi();
	if (action & SMP_CALL_FUNCTION)
		generic_smp_call_function_interrupt();

	return IRQ_HANDLED;
}

static void bmips43xx_send_ipi_mask(const struct cpumask *mask,
	unsigned int action)
{
	unsigned int i;

	for_each_cpu(i, mask)
		bmips43xx_send_ipi_single(i, action);
}

#ifdef CONFIG_HOTPLUG_CPU

static int bmips_cpu_disable(void)
{
	unsigned int cpu = smp_processor_id();

	pr_info("SMP: CPU%d is offline\n", cpu);

	set_cpu_online(cpu, false);
	calculate_cpu_foreign_map();
	irq_migrate_all_off_this_cpu();
	clear_c0_status(IE_IRQ5);

	local_flush_tlb_all();
	local_flush_icache_range(0, ~0);

	return 0;
}

static void bmips_cpu_die(unsigned int cpu)
{
}

void __ref play_dead(void)
{
	idle_task_exit();
	cpuhp_ap_report_dead();

	/* flush data cache */
	_dma_cache_wback_inv(0, ~0);

	/*
	 * Wakeup is on SW0 or SW1; disable everything else
	 * Use BEV !IV (BMIPS_WARM_RESTART_VEC) to avoid the regular Linux
	 * IRQ handlers; this clears ST0_IE and returns immediately.
	 */
	clear_c0_cause(CAUSEF_IV | C_SW0 | C_SW1);
	change_c0_status(
		IE_IRQ5 | bmips_tp1_irqs | IE_SW0 | IE_SW1 | ST0_IE | ST0_BEV,
		IE_SW0 | IE_SW1 | ST0_IE | ST0_BEV);
	irq_disable_hazard();

	/*
	 * wait for SW interrupt from bmips_boot_secondary(), then jump
	 * back to start_secondary()
	 */
	__asm__ __volatile__(
	"	wait\n"
	"	j	bmips_secondary_reentry\n"
	: : : "memory");

	BUG();
}

#endif /* CONFIG_HOTPLUG_CPU */

const struct plat_smp_ops bmips43xx_smp_ops = {
	.smp_setup		= bmips_smp_setup,
	.prepare_cpus		= bmips_prepare_cpus,
	.boot_secondary		= bmips_boot_secondary,
	.smp_finish		= bmips_smp_finish,
	.init_secondary		= bmips_init_secondary,
	.send_ipi_single	= bmips43xx_send_ipi_single,
	.send_ipi_mask		= bmips43xx_send_ipi_mask,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_disable		= bmips_cpu_disable,
	.cpu_die		= bmips_cpu_die,
#endif
#ifdef CONFIG_KEXEC_CORE
	.kexec_nonboot_cpu	= kexec_nonboot_cpu_jump,
#endif
};

const struct plat_smp_ops bmips5000_smp_ops = {
	.smp_setup		= bmips_smp_setup,
	.prepare_cpus		= bmips_prepare_cpus,
	.boot_secondary		= bmips_boot_secondary,
	.smp_finish		= bmips_smp_finish,
	.init_secondary		= bmips_init_secondary,
	.send_ipi_single	= bmips5000_send_ipi_single,
	.send_ipi_mask		= bmips5000_send_ipi_mask,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_disable		= bmips_cpu_disable,
	.cpu_die		= bmips_cpu_die,
#endif
#ifdef CONFIG_KEXEC_CORE
	.kexec_nonboot_cpu	= kexec_nonboot_cpu_jump,
#endif
};

#endif /* CONFIG_SMP */

/***********************************************************************
 * BMIPS vector relocation
 * This is primarily used for SMP boot, but it is applicable to some
 * UP BMIPS systems as well.
 ***********************************************************************/

static void bmips_wr_vec(unsigned long dst, char *start, char *end)
{
	memcpy((void *)dst, start, end - start);
	dma_cache_wback(dst, end - start);
	local_flush_icache_range(dst, dst + (end - start));
	instruction_hazard();
}

static inline void bmips_nmi_handler_setup(void)
{
	bmips_wr_vec(BMIPS_NMI_RESET_VEC, bmips_reset_nmi_vec,
		bmips_reset_nmi_vec_end);
	bmips_wr_vec(BMIPS_WARM_RESTART_VEC, bmips_smp_int_vec,
		bmips_smp_int_vec_end);
}

struct reset_vec_info {
	int cpu;
	u32 val;
};

static void bmips_set_reset_vec_remote(void *vinfo)
{
	struct reset_vec_info *info = vinfo;
	int shift = info->cpu & 0x01 ? 16 : 0;
	u32 mask = ~(0xffff << shift), val = info->val >> 16;

	preempt_disable();
	if (smp_processor_id() > 0) {
		smp_call_function_single(0, &bmips_set_reset_vec_remote,
					 info, 1);
	} else {
		if (info->cpu & 0x02) {
			/* BMIPS5200 "should" use mask/shift, but it's buggy */
			bmips_write_zscm_reg(0xa0, (val << 16) | val);
			bmips_read_zscm_reg(0xa0);
		} else {
			write_c0_brcm_bootvec((read_c0_brcm_bootvec() & mask) |
					      (val << shift));
		}
	}
	preempt_enable();
}

static void bmips_set_reset_vec(int cpu, u32 val)
{
	struct reset_vec_info info;

	if (current_cpu_type() == CPU_BMIPS5000) {
		/* this needs to run from CPU0 (which is always online) */
		info.cpu = cpu;
		info.val = val;
		bmips_set_reset_vec_remote(&info);
	} else {
		void __iomem *cbr = BMIPS_GET_CBR();

		if (cpu == 0)
			__raw_writel(val, cbr + BMIPS_RELO_VECTOR_CONTROL_0);
		else {
			if (current_cpu_type() != CPU_BMIPS4380)
				return;
			__raw_writel(val, cbr + BMIPS_RELO_VECTOR_CONTROL_1);
		}
	}
	__sync();
	back_to_back_c0_hazard();
}

void bmips_ebase_setup(void)
{
	unsigned long new_ebase = ebase;

	BUG_ON(ebase != CKSEG0);

	switch (current_cpu_type()) {
	case CPU_BMIPS4350:
		/*
		 * BMIPS4350 cannot relocate the normal vectors, but it
		 * can relocate the BEV=1 vectors.  So CPU1 starts up at
		 * the relocated BEV=1, IV=0 general exception vector @
		 * 0xa000_0380.
		 *
		 * set_uncached_handler() is used here because:
		 *  - CPU1 will run this from uncached space
		 *  - None of the cacheflush functions are set up yet
		 */
		set_uncached_handler(BMIPS_WARM_RESTART_VEC - CKSEG0,
			&bmips_smp_int_vec, 0x80);
		__sync();
		return;
	case CPU_BMIPS3300:
	case CPU_BMIPS4380:
		/*
		 * 0x8000_0000: reset/NMI (initially in kseg1)
		 * 0x8000_0400: normal vectors
		 */
		new_ebase = 0x80000400;
		bmips_set_reset_vec(0, RESET_FROM_KSEG0);
		break;
	case CPU_BMIPS5000:
		/*
		 * 0x8000_0000: reset/NMI (initially in kseg1)
		 * 0x8000_1000: normal vectors
		 */
		new_ebase = 0x80001000;
		bmips_set_reset_vec(0, RESET_FROM_KSEG0);
		write_c0_ebase(new_ebase);
		break;
	default:
		return;
	}

	board_nmi_handler_setup = &bmips_nmi_handler_setup;
	ebase = new_ebase;
}

asmlinkage void __weak plat_wired_tlb_setup(void)
{
	/*
	 * Called when starting/restarting a secondary CPU.
	 * Kernel stacks and other important data might only be accessible
	 * once the wired entries are present.
	 */
}

void bmips_cpu_setup(void)
{
	void __iomem __maybe_unused *cbr = BMIPS_GET_CBR();
	u32 __maybe_unused cfg;

	switch (current_cpu_type()) {
	case CPU_BMIPS3300:
		/* Set BIU to async mode */
		set_c0_brcm_bus_pll(BIT(22));
		__sync();

		/* put the BIU back in sync mode */
		clear_c0_brcm_bus_pll(BIT(22));

		/* clear BHTD to enable branch history table */
		clear_c0_brcm_reset(BIT(16));

		/* Flush and enable RAC */
		cfg = __raw_readl(cbr + BMIPS_RAC_CONFIG);
		__raw_writel(cfg | 0x100, cbr + BMIPS_RAC_CONFIG);
		__raw_readl(cbr + BMIPS_RAC_CONFIG);

		cfg = __raw_readl(cbr + BMIPS_RAC_CONFIG);
		__raw_writel(cfg | 0xf, cbr + BMIPS_RAC_CONFIG);
		__raw_readl(cbr + BMIPS_RAC_CONFIG);

		cfg = __raw_readl(cbr + BMIPS_RAC_ADDRESS_RANGE);
		__raw_writel(cfg | 0x0fff0000, cbr + BMIPS_RAC_ADDRESS_RANGE);
		__raw_readl(cbr + BMIPS_RAC_ADDRESS_RANGE);
		break;

	case CPU_BMIPS4380:
		/* CBG workaround for early BMIPS4380 CPUs */
		switch (read_c0_prid()) {
		case 0x2a040:
		case 0x2a042:
		case 0x2a044:
		case 0x2a060:
			cfg = __raw_readl(cbr + BMIPS_L2_CONFIG);
			__raw_writel(cfg & ~0x07000000, cbr + BMIPS_L2_CONFIG);
			__raw_readl(cbr + BMIPS_L2_CONFIG);
		}

		/* clear BHTD to enable branch history table */
		clear_c0_brcm_config_0(BIT(21));

		/* XI/ROTR enable */
		set_c0_brcm_config_0(BIT(23));
		set_c0_brcm_cmt_ctrl(BIT(15));
		break;

	case CPU_BMIPS5000:
		/* enable RDHWR, BRDHWR */
		set_c0_brcm_config(BIT(17) | BIT(21));

		/* Disable JTB */
		__asm__ __volatile__(
		"	.set	noreorder\n"
		"	li	$8, 0x5a455048\n"
		"	.word	0x4088b00f\n"	/* mtc0	t0, $22, 15 */
		"	.word	0x4008b008\n"	/* mfc0	t0, $22, 8 */
		"	li	$9, 0x00008000\n"
		"	or	$8, $8, $9\n"
		"	.word	0x4088b008\n"	/* mtc0	t0, $22, 8 */
		"	sync\n"
		"	li	$8, 0x0\n"
		"	.word	0x4088b00f\n"	/* mtc0	t0, $22, 15 */
		"	.set	reorder\n"
		: : : "$8", "$9");

		/* XI enable */
		set_c0_brcm_config(BIT(27));

		/* enable MIPS32R2 ROR instruction for XI TLB handlers */
		__asm__ __volatile__(
		"	li	$8, 0x5a455048\n"
		"	.word	0x4088b00f\n"	/* mtc0 $8, $22, 15 */
		"	nop; nop; nop\n"
		"	.word	0x4008b008\n"	/* mfc0 $8, $22, 8 */
		"	lui	$9, 0x0100\n"
		"	or	$8, $9\n"
		"	.word	0x4088b008\n"	/* mtc0 $8, $22, 8 */
		: : : "$8", "$9");
		break;
	}
}
