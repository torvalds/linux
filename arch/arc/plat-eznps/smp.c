// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright(c) 2015 EZchip Technologies.
 */

#include <linux/smp.h>
#include <linux/of_fdt.h>
#include <linux/io.h>
#include <linux/irqdomain.h>
#include <asm/irq.h>
#include <plat/ctop.h>
#include <plat/smp.h>
#include <plat/mtm.h>

#define NPS_DEFAULT_MSID	0x34
#define NPS_MTM_CPU_CFG		0x90

static char smp_cpuinfo_buf[128] = {"Extn [EZNPS-SMP]\t: On\n"};

/* Get cpu map from device tree */
static int __init eznps_get_map(const char *name, struct cpumask *cpumask)
{
	unsigned long dt_root = of_get_flat_dt_root();
	const char *buf;

	buf = of_get_flat_dt_prop(dt_root, name, NULL);
	if (!buf)
		return 1;

	cpulist_parse(buf, cpumask);

	return 0;
}

/* Update board cpu maps */
static void __init eznps_init_cpumasks(void)
{
	struct cpumask cpumask;

	if (eznps_get_map("present-cpus", &cpumask)) {
		pr_err("Failed to get present-cpus from dtb");
		return;
	}
	init_cpu_present(&cpumask);

	if (eznps_get_map("possible-cpus", &cpumask)) {
		pr_err("Failed to get possible-cpus from dtb");
		return;
	}
	init_cpu_possible(&cpumask);
}

static void eznps_init_core(unsigned int cpu)
{
	u32 sync_value;
	struct nps_host_reg_aux_hw_comply hw_comply;
	struct nps_host_reg_aux_lpc lpc;

	if (NPS_CPU_TO_THREAD_NUM(cpu) != 0)
		return;

	hw_comply.value = read_aux_reg(CTOP_AUX_HW_COMPLY);
	hw_comply.me  = 1;
	hw_comply.le  = 1;
	hw_comply.te  = 1;
	write_aux_reg(CTOP_AUX_HW_COMPLY, hw_comply.value);

	/* Enable MMU clock */
	lpc.mep = 1;
	write_aux_reg(CTOP_AUX_LPC, lpc.value);

	/* Boot CPU only */
	if (!cpu) {
		/* Write to general purpose register in CRG */
		sync_value = ioread32be(REG_GEN_PURP_0);
		sync_value |= NPS_CRG_SYNC_BIT;
		iowrite32be(sync_value, REG_GEN_PURP_0);
	}
}

/*
 * Master kick starting another CPU
 */
static void __init eznps_smp_wakeup_cpu(int cpu, unsigned long pc)
{
	struct nps_host_reg_mtm_cpu_cfg cpu_cfg;

	if (mtm_enable_thread(cpu) == 0)
		return;

	/* set PC, dmsid, and start CPU */
	cpu_cfg.value = (u32)res_service;
	cpu_cfg.dmsid = NPS_DEFAULT_MSID;
	cpu_cfg.cs = 1;
	iowrite32be(cpu_cfg.value, nps_mtm_reg_addr(cpu, NPS_MTM_CPU_CFG));
}

static void eznps_ipi_send(int cpu)
{
	struct global_id gid;
	struct {
		union {
			struct {
				u32 num:8, cluster:8, core:8, thread:8;
			};
			u32 value;
		};
	} ipi;

	gid.value = cpu;
	ipi.thread = get_thread(gid);
	ipi.core = gid.core;
	ipi.cluster = nps_cluster_logic_to_phys(gid.cluster);
	ipi.num = NPS_IPI_IRQ;

	__asm__ __volatile__(
	"	mov r3, %0\n"
	"	.word %1\n"
	:
	: "r"(ipi.value), "i"(CTOP_INST_ASRI_0_R3)
	: "r3");
}

static void eznps_init_per_cpu(int cpu)
{
	smp_ipi_irq_setup(cpu, NPS_IPI_IRQ);

	eznps_init_core(cpu);
	mtm_enable_core(cpu);
}

struct plat_smp_ops plat_smp_ops = {
	.info		= smp_cpuinfo_buf,
	.init_early_smp	= eznps_init_cpumasks,
	.cpu_kick	= eznps_smp_wakeup_cpu,
	.ipi_send	= eznps_ipi_send,
	.init_per_cpu	= eznps_init_per_cpu,
};
