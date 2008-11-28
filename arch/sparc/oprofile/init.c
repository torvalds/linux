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

static int nmi_enabled;

struct pcr_ops {
	u64 (*read)(void);
	void (*write)(u64);
};
static const struct pcr_ops *pcr_ops;

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

#define PCR_PIC_PRIV		0x00000001 /* PIC access is privileged */
#define PCR_STRACE		0x00000002 /* Trace supervisor events  */
#define PCR_UTRACE		0x00000004 /* Trace user events        */
#define PCR_N2_HTRACE		0x00000008 /* Trace hypervisor events  */
#define PCR_N2_TOE_OV0		0x00000010 /* Trap if PIC 0 overflows  */
#define PCR_N2_TOE_OV1		0x00000020 /* Trap if PIC 1 overflows  */
#define PCR_N2_MASK0		0x00003fc0
#define PCR_N2_MASK0_SHIFT	6
#define PCR_N2_SL0		0x0003c000
#define PCR_N2_SL0_SHIFT	14
#define PCR_N2_OV0		0x00040000
#define PCR_N2_MASK1		0x07f80000
#define PCR_N2_MASK1_SHIFT	19
#define PCR_N2_SL1		0x78000000
#define PCR_N2_SL1_SHIFT	27
#define PCR_N2_OV1		0x80000000

#define PCR_SUN4U_ENABLE	(PCR_PIC_PRIV | PCR_STRACE | PCR_UTRACE)
#define PCR_N2_ENABLE		(PCR_PIC_PRIV | PCR_STRACE | PCR_UTRACE | \
				 PCR_N2_TOE_OV1 | \
				 (2 << PCR_N2_SL1_SHIFT) | \
				 (0xff << PCR_N2_MASK1_SHIFT))

static u64 pcr_enable = PCR_SUN4U_ENABLE;

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
			printk("perfmon: Could not register N2 hvapi.\n");
			return -ENODEV;
		}
	}
	return 0;
}

static void unregister_perf_hsvc(void)
{
	if (tlb_type != hypervisor)
		return;
	sun4v_hvapi_unregister(perf_hsvc_group);
}

static int oprofile_nmi_init(struct oprofile_operations *ops)
{
	int err = register_perf_hsvc();

	if (err)
		return err;

	switch (tlb_type) {
	case hypervisor:
		pcr_ops = &n2_pcr_ops;
		pcr_enable = PCR_N2_ENABLE;
		break;

	case cheetah:
	case cheetah_plus:
		pcr_ops = &direct_pcr_ops;
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
#ifdef CONFIG_SPARC64
	unregister_perf_hsvc();
#endif
}
