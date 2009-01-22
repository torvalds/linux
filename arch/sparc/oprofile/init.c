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
#include <asm/hypervisor.h>
#include <asm/spitfire.h>
#include <asm/cpudata.h>
#include <asm/irq.h>
#include <asm/pcr.h>

static int nmi_enabled;

/* In order to commonize as much of the implementation as
 * possible, we use PICH as our counter.  Mostly this is
 * to accomodate Niagara-1 which can only count insn cycles
 * in PICH.
 */
static u64 picl_value(void)
{
	u32 delta = local_cpu_data().clock_tick / HZ;

	return ((u64)((0 - delta) & 0xffffffff)) << 32;
}

#define PCR_SUN4U_ENABLE	(PCR_PIC_PRIV | PCR_STRACE | PCR_UTRACE)
#define PCR_N2_ENABLE		(PCR_PIC_PRIV | PCR_STRACE | PCR_UTRACE | \
				 PCR_N2_TOE_OV1 | \
				 (2 << PCR_N2_SL1_SHIFT) | \
				 (0xff << PCR_N2_MASK1_SHIFT))

static u64 pcr_enable;

static void nmi_handler(struct pt_regs *regs)
{
	pcr_ops->write(PCR_PIC_PRIV);

	if (nmi_enabled) {
		oprofile_add_sample(regs, 0);

		write_pic(picl_value());
		pcr_ops->write(pcr_enable);
	}
}

/* We count "clock cycle" events in the lower 32-bit PIC.
 * Then configure it such that it overflows every HZ, and thus
 * generates a level 15 interrupt at that frequency.
 */
static void cpu_nmi_start(void *_unused)
{
	pcr_ops->write(PCR_PIC_PRIV);
	write_pic(picl_value());

	pcr_ops->write(pcr_enable);
}

static void cpu_nmi_stop(void *_unused)
{
	pcr_ops->write(PCR_PIC_PRIV);
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
	switch (tlb_type) {
	case hypervisor:
		pcr_enable = PCR_N2_ENABLE;
		break;

	case cheetah:
	case cheetah_plus:
		pcr_enable = PCR_SUN4U_ENABLE;
		break;

	default:
		return -ENODEV;
	}

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
