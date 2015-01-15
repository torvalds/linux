/* pcr.c: Generic sparc64 performance counter infrastructure.
 *
 * Copyright (C) 2009 David S. Miller (davem@davemloft.net)
 */
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/irq.h>

#include <linux/irq_work.h>
#include <linux/ftrace.h>

#include <asm/pil.h>
#include <asm/pcr.h>
#include <asm/nmi.h>
#include <asm/asi.h>
#include <asm/spitfire.h>

/* This code is shared between various users of the performance
 * counters.  Users will be oprofile, pseudo-NMI watchdog, and the
 * perf_event support layer.
 */

/* Performance counter interrupts run unmasked at PIL level 15.
 * Therefore we can't do things like wakeups and other work
 * that expects IRQ disabling to be adhered to in locking etc.
 *
 * Therefore in such situations we defer the work by signalling
 * a lower level cpu IRQ.
 */
void __irq_entry deferred_pcr_work_irq(int irq, struct pt_regs *regs)
{
	struct pt_regs *old_regs;

	clear_softint(1 << PIL_DEFERRED_PCR_WORK);

	old_regs = set_irq_regs(regs);
	irq_enter();
#ifdef CONFIG_IRQ_WORK
	irq_work_run();
#endif
	irq_exit();
	set_irq_regs(old_regs);
}

void arch_irq_work_raise(void)
{
	set_softint(1 << PIL_DEFERRED_PCR_WORK);
}

const struct pcr_ops *pcr_ops;
EXPORT_SYMBOL_GPL(pcr_ops);

static u64 direct_pcr_read(unsigned long reg_num)
{
	u64 val;

	WARN_ON_ONCE(reg_num != 0);
	__asm__ __volatile__("rd %%pcr, %0" : "=r" (val));
	return val;
}

static void direct_pcr_write(unsigned long reg_num, u64 val)
{
	WARN_ON_ONCE(reg_num != 0);
	__asm__ __volatile__("wr %0, 0x0, %%pcr" : : "r" (val));
}

static u64 direct_pic_read(unsigned long reg_num)
{
	u64 val;

	WARN_ON_ONCE(reg_num != 0);
	__asm__ __volatile__("rd %%pic, %0" : "=r" (val));
	return val;
}

static void direct_pic_write(unsigned long reg_num, u64 val)
{
	WARN_ON_ONCE(reg_num != 0);

	/* Blackbird errata workaround.  See commentary in
	 * arch/sparc64/kernel/smp.c:smp_percpu_timer_interrupt()
	 * for more information.
	 */
	__asm__ __volatile__("ba,pt	%%xcc, 99f\n\t"
			     " nop\n\t"
			     ".align	64\n"
			  "99:wr	%0, 0x0, %%pic\n\t"
			     "rd	%%pic, %%g0" : : "r" (val));
}

static u64 direct_picl_value(unsigned int nmi_hz)
{
	u32 delta = local_cpu_data().clock_tick / nmi_hz;

	return ((u64)((0 - delta) & 0xffffffff)) << 32;
}

static const struct pcr_ops direct_pcr_ops = {
	.read_pcr		= direct_pcr_read,
	.write_pcr		= direct_pcr_write,
	.read_pic		= direct_pic_read,
	.write_pic		= direct_pic_write,
	.nmi_picl_value		= direct_picl_value,
	.pcr_nmi_enable		= (PCR_PIC_PRIV | PCR_STRACE | PCR_UTRACE),
	.pcr_nmi_disable	= PCR_PIC_PRIV,
};

static void n2_pcr_write(unsigned long reg_num, u64 val)
{
	unsigned long ret;

	WARN_ON_ONCE(reg_num != 0);
	if (val & PCR_N2_HTRACE) {
		ret = sun4v_niagara2_setperf(HV_N2_PERF_SPARC_CTL, val);
		if (ret != HV_EOK)
			direct_pcr_write(reg_num, val);
	} else
		direct_pcr_write(reg_num, val);
}

static u64 n2_picl_value(unsigned int nmi_hz)
{
	u32 delta = local_cpu_data().clock_tick / (nmi_hz << 2);

	return ((u64)((0 - delta) & 0xffffffff)) << 32;
}

static const struct pcr_ops n2_pcr_ops = {
	.read_pcr		= direct_pcr_read,
	.write_pcr		= n2_pcr_write,
	.read_pic		= direct_pic_read,
	.write_pic		= direct_pic_write,
	.nmi_picl_value		= n2_picl_value,
	.pcr_nmi_enable		= (PCR_PIC_PRIV | PCR_STRACE | PCR_UTRACE |
				   PCR_N2_TOE_OV1 |
				   (2 << PCR_N2_SL1_SHIFT) |
				   (0xff << PCR_N2_MASK1_SHIFT)),
	.pcr_nmi_disable	= PCR_PIC_PRIV,
};

static u64 n4_pcr_read(unsigned long reg_num)
{
	unsigned long val;

	(void) sun4v_vt_get_perfreg(reg_num, &val);

	return val;
}

static void n4_pcr_write(unsigned long reg_num, u64 val)
{
	(void) sun4v_vt_set_perfreg(reg_num, val);
}

static u64 n4_pic_read(unsigned long reg_num)
{
	unsigned long val;

	__asm__ __volatile__("ldxa [%1] %2, %0"
			     : "=r" (val)
			     : "r" (reg_num * 0x8UL), "i" (ASI_PIC));

	return val;
}

static void n4_pic_write(unsigned long reg_num, u64 val)
{
	__asm__ __volatile__("stxa %0, [%1] %2"
			     : /* no outputs */
			     : "r" (val), "r" (reg_num * 0x8UL), "i" (ASI_PIC));
}

static u64 n4_picl_value(unsigned int nmi_hz)
{
	u32 delta = local_cpu_data().clock_tick / (nmi_hz << 2);

	return ((u64)((0 - delta) & 0xffffffff));
}

static const struct pcr_ops n4_pcr_ops = {
	.read_pcr		= n4_pcr_read,
	.write_pcr		= n4_pcr_write,
	.read_pic		= n4_pic_read,
	.write_pic		= n4_pic_write,
	.nmi_picl_value		= n4_picl_value,
	.pcr_nmi_enable		= (PCR_N4_PICNPT | PCR_N4_STRACE |
				   PCR_N4_UTRACE | PCR_N4_TOE |
				   (26 << PCR_N4_SL_SHIFT)),
	.pcr_nmi_disable	= PCR_N4_PICNPT,
};

static u64 n5_pcr_read(unsigned long reg_num)
{
	unsigned long val;

	(void) sun4v_t5_get_perfreg(reg_num, &val);

	return val;
}

static void n5_pcr_write(unsigned long reg_num, u64 val)
{
	(void) sun4v_t5_set_perfreg(reg_num, val);
}

static const struct pcr_ops n5_pcr_ops = {
	.read_pcr		= n5_pcr_read,
	.write_pcr		= n5_pcr_write,
	.read_pic		= n4_pic_read,
	.write_pic		= n4_pic_write,
	.nmi_picl_value		= n4_picl_value,
	.pcr_nmi_enable		= (PCR_N4_PICNPT | PCR_N4_STRACE |
				   PCR_N4_UTRACE | PCR_N4_TOE |
				   (26 << PCR_N4_SL_SHIFT)),
	.pcr_nmi_disable	= PCR_N4_PICNPT,
};


static unsigned long perf_hsvc_group;
static unsigned long perf_hsvc_major;
static unsigned long perf_hsvc_minor;

static int __init register_perf_hsvc(void)
{
	unsigned long hverror;

	if (tlb_type == hypervisor) {
		switch (sun4v_chip_type) {
		case SUN4V_CHIP_NIAGARA1:
			perf_hsvc_group = HV_GRP_NIAG_PERF;
			break;

		case SUN4V_CHIP_NIAGARA2:
			perf_hsvc_group = HV_GRP_N2_CPU;
			break;

		case SUN4V_CHIP_NIAGARA3:
			perf_hsvc_group = HV_GRP_KT_CPU;
			break;

		case SUN4V_CHIP_NIAGARA4:
			perf_hsvc_group = HV_GRP_VT_CPU;
			break;

		case SUN4V_CHIP_NIAGARA5:
			perf_hsvc_group = HV_GRP_T5_CPU;
			break;

		default:
			return -ENODEV;
		}


		perf_hsvc_major = 1;
		perf_hsvc_minor = 0;
		hverror = sun4v_hvapi_register(perf_hsvc_group,
					       perf_hsvc_major,
					       &perf_hsvc_minor);
		if (hverror) {
			pr_err("perfmon: Could not register hvapi(0x%lx).\n",
			       hverror);
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

static int __init setup_sun4v_pcr_ops(void)
{
	int ret = 0;

	switch (sun4v_chip_type) {
	case SUN4V_CHIP_NIAGARA1:
	case SUN4V_CHIP_NIAGARA2:
	case SUN4V_CHIP_NIAGARA3:
		pcr_ops = &n2_pcr_ops;
		break;

	case SUN4V_CHIP_NIAGARA4:
		pcr_ops = &n4_pcr_ops;
		break;

	case SUN4V_CHIP_NIAGARA5:
		pcr_ops = &n5_pcr_ops;
		break;

	default:
		ret = -ENODEV;
		break;
	}

	return ret;
}

int __init pcr_arch_init(void)
{
	int err = register_perf_hsvc();

	if (err)
		return err;

	switch (tlb_type) {
	case hypervisor:
		err = setup_sun4v_pcr_ops();
		if (err)
			goto out_unregister;
		break;

	case cheetah:
	case cheetah_plus:
		pcr_ops = &direct_pcr_ops;
		break;

	case spitfire:
		/* UltraSPARC-I/II and derivatives lack a profile
		 * counter overflow interrupt so we can't make use of
		 * their hardware currently.
		 */
		/* fallthrough */
	default:
		err = -ENODEV;
		goto out_unregister;
	}

	return nmi_init();

out_unregister:
	unregister_perf_hsvc();
	return err;
}
