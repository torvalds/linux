#include <linux/linkage.h>
#include <linux/sched.h>

#include <asm/pmon.h>
#include <asm/titan_dep.h>
#include <asm/time.h>

#define LAUNCHSTACK_SIZE 256

static __initdata DEFINE_SPINLOCK(launch_lock);

static unsigned long secondary_sp __initdata;
static unsigned long secondary_gp __initdata;

static unsigned char launchstack[LAUNCHSTACK_SIZE] __initdata
	__attribute__((aligned(2 * sizeof(long))));

static void __init prom_smp_bootstrap(void)
{
	local_irq_disable();

	while (spin_is_locked(&launch_lock));

	__asm__ __volatile__(
	"	move	$sp, %0		\n"
	"	move	$gp, %1		\n"
	"	j	smp_bootstrap	\n"
	:
	: "r" (secondary_sp), "r" (secondary_gp));
}

/*
 * PMON is a fragile beast.  It'll blow up once the mappings it's littering
 * right into the middle of KSEG3 are blown away so we have to grab the slave
 * core early and keep it in a waiting loop.
 */
void __init prom_grab_secondary(void)
{
	spin_lock(&launch_lock);

	pmon_cpustart(1, &prom_smp_bootstrap,
	              launchstack + LAUNCHSTACK_SIZE, 0);
}

void titan_mailbox_irq(void)
{
	int cpu = smp_processor_id();
	unsigned long status;

	switch (cpu) {
	case 0:
		status = OCD_READ(RM9000x2_OCD_INTP0STATUS3);
		OCD_WRITE(RM9000x2_OCD_INTP0CLEAR3, status);

		if (status & 0x2)
			smp_call_function_interrupt();
		break;

	case 1:
		status = OCD_READ(RM9000x2_OCD_INTP1STATUS3);
		OCD_WRITE(RM9000x2_OCD_INTP1CLEAR3, status);

		if (status & 0x2)
			smp_call_function_interrupt();
		break;
	}
}

/*
 * Send inter-processor interrupt
 */
static void yos_send_ipi_single(int cpu, unsigned int action)
{
	/*
	 * Generate an INTMSG so that it can be sent over to the
	 * destination CPU. The INTMSG will put the STATUS bits
	 * based on the action desired. An alternative strategy
	 * is to write to the Interrupt Set register, read the
	 * Interrupt Status register and clear the Interrupt
	 * Clear register. The latter is preffered.
	 */
	switch (action) {
	case SMP_RESCHEDULE_YOURSELF:
		if (cpu == 1)
			OCD_WRITE(RM9000x2_OCD_INTP1SET3, 4);
		else
			OCD_WRITE(RM9000x2_OCD_INTP0SET3, 4);
		break;

	case SMP_CALL_FUNCTION:
		if (cpu == 1)
			OCD_WRITE(RM9000x2_OCD_INTP1SET3, 2);
		else
			OCD_WRITE(RM9000x2_OCD_INTP0SET3, 2);
		break;
	}
}

static void yos_send_ipi_mask(cpumask_t mask, unsigned int action)
{
	unsigned int i;

	for_each_cpu_mask(i, mask)
		yos_send_ipi_single(i, action);
}

/*
 *  After we've done initial boot, this function is called to allow the
 *  board code to clean up state, if needed
 */
static void __cpuinit yos_init_secondary(void)
{
	set_c0_status(ST0_CO | ST0_IE | ST0_IM);
}

static void __cpuinit yos_smp_finish(void)
{
}

/* Hook for after all CPUs are online */
static void yos_cpus_done(void)
{
}

/*
 * Firmware CPU startup hook
 * Complicated by PMON's weird interface which tries to minimic the UNIX fork.
 * It launches the next * available CPU and copies some information on the
 * stack so the first thing we do is throw away that stuff and load useful
 * values into the registers ...
 */
static void __cpuinit yos_boot_secondary(int cpu, struct task_struct *idle)
{
	unsigned long gp = (unsigned long) task_thread_info(idle);
	unsigned long sp = __KSTK_TOS(idle);

	secondary_sp = sp;
	secondary_gp = gp;

	spin_unlock(&launch_lock);
}

/*
 * Detect available CPUs, populate phys_cpu_present_map before smp_init
 *
 * We don't want to start the secondary CPU yet nor do we have a nice probing
 * feature in PMON so we just assume presence of the secondary core.
 */
static void __init yos_smp_setup(void)
{
	int i;

	cpus_clear(phys_cpu_present_map);

	for (i = 0; i < 2; i++) {
		cpu_set(i, phys_cpu_present_map);
		__cpu_number_map[i]	= i;
		__cpu_logical_map[i]	= i;
	}
}

static void __init yos_prepare_cpus(unsigned int max_cpus)
{
	/*
	 * Be paranoid.  Enable the IPI only if we're really about to go SMP.
	 */
	if (cpus_weight(cpu_possible_map))
		set_c0_status(STATUSF_IP5);
}

struct plat_smp_ops yos_smp_ops = {
	.send_ipi_single	= yos_send_ipi_single,
	.send_ipi_mask		= yos_send_ipi_mask,
	.init_secondary		= yos_init_secondary,
	.smp_finish		= yos_smp_finish,
	.cpus_done		= yos_cpus_done,
	.boot_secondary		= yos_boot_secondary,
	.smp_setup		= yos_smp_setup,
	.prepare_cpus		= yos_prepare_cpus,
};
