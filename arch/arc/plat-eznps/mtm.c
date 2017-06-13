/*
 * Copyright(c) 2015 EZchip Technologies.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 */

#include <linux/smp.h>
#include <linux/io.h>
#include <linux/log2.h>
#include <asm/arcregs.h>
#include <plat/mtm.h>
#include <plat/smp.h>

#define MT_CTRL_HS_CNT		0xFF
#define MT_CTRL_ST_CNT		0xF
#define NPS_NUM_HW_THREADS	0x10

#ifdef CONFIG_EZNPS_MEM_ERROR_ALIGN
int do_memory_error(unsigned long address, struct pt_regs *regs)
{
	die("Invalid Mem Access", regs, address);

	return 1;
}
#endif

static void mtm_init_nat(int cpu)
{
	struct nps_host_reg_mtm_cfg mtm_cfg;
	struct nps_host_reg_aux_udmc udmc;
	int log_nat, nat = 0, i, t;

	/* Iterate core threads and update nat */
	for (i = 0, t = cpu; i < NPS_NUM_HW_THREADS; i++, t++)
		nat += test_bit(t, cpumask_bits(cpu_possible_mask));

	log_nat = ilog2(nat);

	udmc.value = read_aux_reg(CTOP_AUX_UDMC);
	udmc.nat = log_nat;
	write_aux_reg(CTOP_AUX_UDMC, udmc.value);

	mtm_cfg.value = ioread32be(MTM_CFG(cpu));
	mtm_cfg.nat = log_nat;
	iowrite32be(mtm_cfg.value, MTM_CFG(cpu));
}

static void mtm_init_thread(int cpu)
{
	int i, tries = 5;
	struct nps_host_reg_thr_init thr_init;
	struct nps_host_reg_thr_init_sts thr_init_sts;

	/* Set thread init register */
	thr_init.value = 0;
	iowrite32be(thr_init.value, MTM_THR_INIT(cpu));
	thr_init.thr_id = NPS_CPU_TO_THREAD_NUM(cpu);
	thr_init.str = 1;
	iowrite32be(thr_init.value, MTM_THR_INIT(cpu));

	/* Poll till thread init is done */
	for (i = 0; i < tries; i++) {
		thr_init_sts.value = ioread32be(MTM_THR_INIT_STS(cpu));
		if (thr_init_sts.thr_id == thr_init.thr_id) {
			if (thr_init_sts.bsy)
				continue;
			else if (thr_init_sts.err)
				pr_warn("Failed to thread init cpu %u\n", cpu);
			break;
		}

		pr_warn("Wrong thread id in thread init for cpu %u\n", cpu);
		break;
	}

	if (i == tries)
		pr_warn("Got thread init timeout for cpu %u\n", cpu);
}

int mtm_enable_thread(int cpu)
{
	struct nps_host_reg_mtm_cfg mtm_cfg;

	if (NPS_CPU_TO_THREAD_NUM(cpu) == 0)
		return 1;

	/* Enable thread in mtm */
	mtm_cfg.value = ioread32be(MTM_CFG(cpu));
	mtm_cfg.ten |= (1 << (NPS_CPU_TO_THREAD_NUM(cpu)));
	iowrite32be(mtm_cfg.value, MTM_CFG(cpu));

	return 0;
}

void mtm_enable_core(unsigned int cpu)
{
	int i;
	struct nps_host_reg_aux_mt_ctrl mt_ctrl;
	struct nps_host_reg_mtm_cfg mtm_cfg;

	if (NPS_CPU_TO_THREAD_NUM(cpu) != 0)
		return;

	/* Initialize Number of Active Threads */
	mtm_init_nat(cpu);

	/* Initialize mtm_cfg */
	mtm_cfg.value = ioread32be(MTM_CFG(cpu));
	mtm_cfg.ten = 1;
	iowrite32be(mtm_cfg.value, MTM_CFG(cpu));

	/* Initialize all other threads in core */
	for (i = 1; i < NPS_NUM_HW_THREADS; i++)
		mtm_init_thread(cpu + i);


	/* Enable HW schedule, stall counter, mtm */
	mt_ctrl.value = 0;
	mt_ctrl.hsen = 1;
	mt_ctrl.hs_cnt = MT_CTRL_HS_CNT;
	mt_ctrl.mten = 1;
	write_aux_reg(CTOP_AUX_MT_CTRL, mt_ctrl.value);

	/*
	 * HW scheduling mechanism will start working
	 * Only after call to instruction "schd.rw".
	 * cpu_relax() calls "schd.rw" instruction.
	 */
	cpu_relax();
}
