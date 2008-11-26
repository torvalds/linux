/**
 * @file init.c
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 */

#include <linux/kernel.h>
#include <linux/oprofile.h>
#include <linux/errno.h>
#include <linux/init.h>
 
#ifdef CONFIG_SPARC64
#include <asm/spitfire.h>
#include <asm/cpudata.h>
#include <asm/irq.h>

static int nmi_enabled;

static u64 picl_value(void)
{
	u32 delta = local_cpu_data().clock_tick / HZ;

	return (0 - delta) & 0xffffffff;
}

#define PCR_PIC_PRIV	0x1 /* PIC access is privileged */
#define PCR_STRACE	0x2 /* Trace supervisor events  */
#define PCR_UTRACE	0x4 /* Trace user events        */

static void nmi_handler(struct pt_regs *regs)
{
	write_pcr(PCR_PIC_PRIV);

	if (nmi_enabled) {
		oprofile_add_sample(regs, 0);

		write_pic(picl_value());
		write_pcr(PCR_PIC_PRIV | PCR_STRACE | PCR_UTRACE);
	}
}

/* We count "clock cycle" events in the lower 32-bit PIC.
 * Then configure it such that it overflows every HZ, and thus
 * generates a level 15 interrupt at that frequency.
 */
static void cpu_nmi_start(void *_unused)
{
	write_pcr(PCR_PIC_PRIV);
	write_pic(picl_value());

	/* Bit 0: PIC access is privileged
	 * Bit 1: Supervisor Trace
	 * Bit 2: User Trace
	 *
	 * And the event selection code for cpu cycles is zero.
	 */
	write_pcr(PCR_PIC_PRIV | PCR_STRACE | PCR_UTRACE);
}

static void cpu_nmi_stop(void *_unused)
{
	write_pcr(PCR_PIC_PRIV);
}

static int nmi_start(void)
{
	int err = register_perfctr_intr(nmi_handler);

	if (!err) {
		nmi_enabled = 1;
		wmb();
		err = on_each_cpu(cpu_nmi_start, NULL, 1);
		if (err) {
			nmi_enabled = 0;
			wmb();
			on_each_cpu(cpu_nmi_stop, NULL, 1);
			release_perfctr_intr(nmi_handler);
		}
	}

	return err;
}

static void nmi_stop(void)
{
	nmi_enabled = 0;
	wmb();

	on_each_cpu(cpu_nmi_stop, NULL, 1);
	release_perfctr_intr(nmi_handler);
	synchronize_sched();
}

static int oprofile_nmi_init(struct oprofile_operations *ops)
{
	if (tlb_type != cheetah && tlb_type != cheetah_plus)
		return -ENODEV;

	ops->create_files = NULL;
	ops->setup = NULL;
	ops->shutdown = NULL;
	ops->start = nmi_start;
	ops->stop = nmi_stop;
	ops->cpu_type = "timer";

	printk(KERN_INFO "oprofile: Using perfctr based NMI timer interrupt.\n");

	return 0;
}
#endif

int __init oprofile_arch_init(struct oprofile_operations *ops)
{
	int ret = -ENODEV;

#ifdef CONFIG_SPARC64
	ret = oprofile_nmi_init(ops);
	if (!ret)
		return ret;
#endif

	return ret;
}


void oprofile_arch_exit(void)
{
}
