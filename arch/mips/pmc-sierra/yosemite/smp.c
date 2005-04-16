#include <linux/linkage.h>
#include <linux/sched.h>

#include <asm/pmon.h>
#include <asm/titan_dep.h>

extern unsigned int (*mips_hpt_read)(void);
extern void (*mips_hpt_init)(unsigned int);

#define LAUNCHSTACK_SIZE 256

static spinlock_t launch_lock __initdata;

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

/*
 * Detect available CPUs, populate phys_cpu_present_map before smp_init
 *
 * We don't want to start the secondary CPU yet nor do we have a nice probing
 * feature in PMON so we just assume presence of the secondary core.
 */
static char maxcpus_string[] __initdata =
	KERN_WARNING "max_cpus set to 0; using 1 instead\n";

void __init prom_prepare_cpus(unsigned int max_cpus)
{
	int enabled = 0, i;

	if (max_cpus == 0) {
		printk(maxcpus_string);
		max_cpus = 1;
	}

	cpus_clear(phys_cpu_present_map);

	for (i = 0; i < 2; i++) {
		if (i == max_cpus)
			break;

		/*
		 * The boot CPU
		 */
		cpu_set(i, phys_cpu_present_map);
		__cpu_number_map[i]	= i;
		__cpu_logical_map[i]	= i;
		enabled++;
	}

	/*
	 * Be paranoid.  Enable the IPI only if we're really about to go SMP.
	 */
	if (enabled > 1)
		set_c0_status(STATUSF_IP5);
}

/*
 * Firmware CPU startup hook
 * Complicated by PMON's weird interface which tries to minimic the UNIX fork.
 * It launches the next * available CPU and copies some information on the
 * stack so the first thing we do is throw away that stuff and load useful
 * values into the registers ...
 */
void prom_boot_secondary(int cpu, struct task_struct *idle)
{
	unsigned long gp = (unsigned long) idle->thread_info;
	unsigned long sp = gp + THREAD_SIZE - 32;

	secondary_sp = sp;
	secondary_gp = gp;

	spin_unlock(&launch_lock);
}

/* Hook for after all CPUs are online */
void prom_cpus_done(void)
{
}

/*
 *  After we've done initial boot, this function is called to allow the
 *  board code to clean up state, if needed
 */
void prom_init_secondary(void)
{
	mips_hpt_init(mips_hpt_read());

	set_c0_status(ST0_CO | ST0_IE | ST0_IM);
}

void prom_smp_finish(void)
{
}

asmlinkage void titan_mailbox_irq(struct pt_regs *regs)
{
	int cpu = smp_processor_id();
	unsigned long status;

	if (cpu == 0) {
		status = OCD_READ(RM9000x2_OCD_INTP0STATUS3);
		OCD_WRITE(RM9000x2_OCD_INTP0CLEAR3, status);
	}

	if (cpu == 1) {
		status = OCD_READ(RM9000x2_OCD_INTP1STATUS3);
		OCD_WRITE(RM9000x2_OCD_INTP1CLEAR3, status);
	}

	if (status & 0x2)
		smp_call_function_interrupt();
}

/*
 * Send inter-processor interrupt
 */
void core_send_ipi(int cpu, unsigned int action)
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
