/* pcr.c: Generic sparc64 performance counter infrastructure.
 *
 * Copyright (C) 2009 David S. Miller (davem@davemloft.net)
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/irq.h>

#include <asm/pil.h>
#include <asm/pcr.h>

/* This code is shared between various users of the performance
 * counters.  Users will be oprofile, pseudo-NMI watchdog, and the
 * perf_counter support layer.
 */

/* Performance counter interrupts run unmasked at PIL level 15.
 * Therefore we can't do things like wakeups and other work
 * that expects IRQ disabling to be adhered to in locking etc.
 *
 * Therefore in such situations we defer the work by signalling
 * a lower level cpu IRQ.
 */
void deferred_pcr_work_irq(int irq, struct pt_regs *regs)
{
	clear_softint(1 << PIL_DEFERRED_PCR_WORK);
}

void schedule_deferred_pcr_work(void)
{
	set_softint(1 << PIL_DEFERRED_PCR_WORK);
}

const struct pcr_ops *pcr_ops;
EXPORT_SYMBOL_GPL(pcr_ops);

static u64 direct_pcr_read(void)
{
	u64 val;

	read_pcr(val);
	return val;
}

static void direct_pcr_write(u64 val)
{
	write_pcr(val);
}

static const struct pcr_ops direct_pcr_ops = {
	.read	= direct_pcr_read,
	.write	= direct_pcr_write,
};

static void n2_pcr_write(u64 val)
{
	unsigned long ret;

	ret = sun4v_niagara2_setperf(HV_N2_PERF_SPARC_CTL, val);
	if (val != HV_EOK)
		write_pcr(val);
}

static const struct pcr_ops n2_pcr_ops = {
	.read	= direct_pcr_read,
	.write	= n2_pcr_write,
};

static unsigned long perf_hsvc_group;
static unsigned long perf_hsvc_major;
static unsigned long perf_hsvc_minor;

static int __init register_perf_hsvc(void)
{
	if (tlb_type == hypervisor) {
		switch (sun4v_chip_type) {
		case SUN4V_CHIP_NIAGARA1:
			perf_hsvc_group = HV_GRP_NIAG_PERF;
			break;

		case SUN4V_CHIP_NIAGARA2:
			perf_hsvc_group = HV_GRP_N2_CPU;
			break;

		default:
			return -ENODEV;
		}


		perf_hsvc_major = 1;
		perf_hsvc_minor = 0;
		if (sun4v_hvapi_register(perf_hsvc_group,
					 perf_hsvc_major,
					 &perf_hsvc_minor)) {
			printk("perfmon: Could not register hvapi.\n");
			return -ENODEV;
		}
	}
	return 0;
}

static void __init unregister_perf_hsvc(void)
{
	if (tlb_type != hypervisor)
		return;
	sun4v_hvapi_unregister(perf_hsvc_group);
}

int __init pcr_arch_init(void)
{
	int err = register_perf_hsvc();

	if (err)
		return err;

	switch (tlb_type) {
	case hypervisor:
		pcr_ops = &n2_pcr_ops;
		break;

	case spitfire:
	case cheetah:
	case cheetah_plus:
		pcr_ops = &direct_pcr_ops;
		break;

	default:
		err = -ENODEV;
		goto out_unregister;
	}

	return 0;

out_unregister:
	unregister_perf_hsvc();
	return err;
}

arch_initcall(pcr_arch_init);
