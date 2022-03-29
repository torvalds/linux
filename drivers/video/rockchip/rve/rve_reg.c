// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) Rockchip Electronics Co., Ltd.
 *
 * Author: Huang Lee <Putin.li@rock-chips.com>
 */

#define pr_fmt(fmt) "rve_reg: " fmt

#include "rve_reg.h"
#include "rve_job.h"

void rve_soft_reset(struct rve_scheduler_t *scheduler)
{
	u32 i;
	u32 reg;

	rve_write(1, RVE_SWREG5_IVE_IDLE_CTRL, scheduler);

	if (DEBUGGER_EN(REG)) {
		pr_err("dump reg info on soft reset");
		rve_dump_read_back_reg(scheduler);
	}

	if (DEBUGGER_EN(MSG)) {
		pr_err("soft reset idle_ctrl = %.8x, idle_prc_sta = %.8x",
			rve_read(RVE_SWREG5_IVE_IDLE_CTRL, scheduler),
			rve_read(RVE_SWREG3_IVE_IDLE_PRC_STA, scheduler));

		pr_err("work status = %.8x", rve_read(RVE_SWREG6_IVE_WORK_STA, scheduler));
	}

	mdelay(20);

	for (i = 0; i < RVE_RESET_TIMEOUT; i++) {
		reg = rve_read(RVE_SWREG3_IVE_IDLE_PRC_STA, scheduler);
		if (reg & 0x2) {
			pr_info("soft reset successfully");

			/* reset sw_softrst_rdy_sta reg */
			rve_write(0x30000, RVE_SWREG3_IVE_IDLE_PRC_STA, scheduler);

			/* reset RVE_SWREG6_IVE_WORK_STA */
			rve_write(0xff0000, RVE_SWREG6_IVE_WORK_STA, scheduler);

			/* clean up int */
			rve_write(0x30000, RVE_SWREG1_IVE_IRQ, scheduler);

			break;
		}

		udelay(1);
	}

	if (i == RVE_RESET_TIMEOUT)
		pr_err("soft reset timeout.\n");

	if (DEBUGGER_EN(MSG)) {
		pr_err("after soft reset idle_ctrl = %.8x, idle_prc_sta = %.8x",
			rve_read(RVE_SWREG5_IVE_IDLE_CTRL, scheduler),
			rve_read(RVE_SWREG3_IVE_IDLE_PRC_STA, scheduler));

		pr_err("work status = %x", rve_read(RVE_SWREG6_IVE_WORK_STA, scheduler));
	}
}

int rve_init_reg(struct rve_job *job)
{
	int ret = 0;

	if (DEBUGGER_EN(MSG))
		pr_err("TODO: debug info");

	return ret;
}

void rve_dump_read_back_reg(struct rve_scheduler_t *scheduler)
{
	int i;
	unsigned long flags;
	uint32_t sys_reg[8] = {0};
	uint32_t ltb_reg[12] = {0};
	uint32_t cfg_reg[40] = {0};
	uint32_t mmu_reg[12] = {0};

	spin_lock_irqsave(&scheduler->irq_lock, flags);

	for (i = 0; i < 8; i++)
		sys_reg[i] = rve_read(RVE_SYS_REG + i * 4, scheduler);

	for (i = 0; i < 12; i++)
		ltb_reg[i] = rve_read(RVE_LTB_REG + i * 4, scheduler);

	for (i = 0; i < 40; i++)
		cfg_reg[i] = rve_read(RVE_CFG_REG + i * 4, scheduler);

	for (i = 0; i < 12; i++)
		mmu_reg[i] = rve_read(RVE_MMU_REG + i * 4, scheduler);

	spin_unlock_irqrestore(&scheduler->irq_lock, flags);

	pr_info("sys_reg:");
	for (i = 0; i < 2; i++)
		pr_info("i = %x : %.8x %.8x %.8x %.8x\n", RVE_SYS_REG + i * 16,
			sys_reg[0 + i * 4], sys_reg[1 + i * 4],
			sys_reg[2 + i * 4], sys_reg[3 + i * 4]);

	pr_info("ltb_reg:");
	for (i = 0; i < 3; i++)
		pr_info("i = %x : %.8x %.8x %.8x %.8x\n", RVE_LTB_REG + i * 16,
			ltb_reg[0 + i * 4], ltb_reg[1 + i * 4],
			ltb_reg[2 + i * 4], ltb_reg[3 + i * 4]);

	pr_info("cfg_reg:");
	for (i = 0; i < 10; i++)
		pr_info("i = %x : %.8x %.8x %.8x %.8x\n", RVE_CFG_REG + i * 16,
			cfg_reg[0 + i * 4], cfg_reg[1 + i * 4],
			cfg_reg[2 + i * 4], cfg_reg[3 + i * 4]);

	pr_info("mmu_reg:");
	for (i = 0; i < 3; i++)
		pr_info("i = %x : %.8x %.8x %.8x %.8x\n", RVE_MMU_REG + i * 16,
			mmu_reg[0 + i * 4], mmu_reg[1 + i * 4],
			mmu_reg[2 + i * 4], mmu_reg[3 + i * 4]);
}

int rve_set_reg(struct rve_job *job, struct rve_scheduler_t *scheduler)
{
	ktime_t now = ktime_get();
	//uint32_t cmd_reg[58];
	uint32_t *cmd_reg;
	int i;

	cmd_reg = job->regcmd_data->cmd_reg;

	if (DEBUGGER_EN(REG)) {
		pr_info("user readback:");
		for (i = 0; i < 14; i++)
			pr_info("%.8x %.8x %.8x %.8x\n",
				cmd_reg[0 + i * 4], cmd_reg[1 + i * 4],
				cmd_reg[2 + i * 4], cmd_reg[3 + i * 4]);
		pr_info("%.8x %.8x", cmd_reg[56], cmd_reg[57]);
	}

	/* clean up irq status reg */
	rve_write(0x00000, RVE_SWREG6_IVE_WORK_STA, scheduler);

	if (DEBUGGER_EN(MSG)) {
		pr_info("idle_ctrl = %x, idle_prc_sta = %x",
			rve_read(RVE_SWREG5_IVE_IDLE_CTRL, scheduler),
			rve_read(RVE_SWREG3_IVE_IDLE_PRC_STA, scheduler));

		pr_info("work status = %x", rve_read(RVE_SWREG6_IVE_WORK_STA, scheduler));
	}

	if (DEBUGGER_EN(TIME))
		pr_info("set cmd use time = %lld\n", ktime_to_us(ktime_sub(now, job->timestamp)));

	job->hw_running_time = now;
	job->hw_recoder_time = now;

	/* start hw, CMD buff */
	for (i = 0; i < 8; i++)
		rve_write(cmd_reg[i], RVE_SYS_REG + i * 4, scheduler);

	for (i = 0; i < 10; i++) {
		/* skip start reg */
		if (i == 2)
			continue;

		rve_write(cmd_reg[8 + i], RVE_LTB_REG + i * 4, scheduler);
	}

	/* 0x200(start)(40 - 1 = 39) need config after reg ready */
	for (i = 0; i < 39; i++)
		rve_write(cmd_reg[19 + i], RVE_CFG_REG + (i + 1) * 4, scheduler);

	//TODO: ddr config
	rve_write(0x30000, RVE_SWCFG5_CTRL, scheduler);
	rve_write(0xf4240, RVE_SWCFG6_TIMEOUT_THRESH, scheduler);
	rve_write(0x1f0001, RVE_SWCFG7_DDR_CTRL, scheduler);

	/* reset RVE_SWREG6_IVE_WORK_STA */
	rve_write(RVE_CLEAR_UP_REG6_WROK_STA, RVE_SWREG6_IVE_WORK_STA, scheduler);

	/* enable monitor */
	if (DEBUGGER_EN(MONITOR))
		rve_write(1, RVE_SWCFG32_MONITOR_CTRL0, scheduler);

	if (DEBUGGER_EN(REG)) {
		pr_err("before config:");
		rve_dump_read_back_reg(scheduler);
	}

	/* if llp mode enable, skip to enable slave mode */
	if (cmd_reg[11] != 1)
		rve_write(1, RVE_SWCFG0_EN, scheduler);
	else
		/* llp config done, to start hw */
		rve_write(cmd_reg[10], RVE_SWLTB2_CFG_DONE, scheduler);

	if (DEBUGGER_EN(REG)) {
		pr_err("after config:");
		rve_dump_read_back_reg(scheduler);
	}

	return 0;
}

int rve_get_version(struct rve_scheduler_t *scheduler)
{
	u32 major_version, minor_version, prod_num;
	u32 reg_version;

	if (!scheduler) {
		pr_err("scheduler is null\n");
		return -EINVAL;
	}

	reg_version = rve_read(RVE_SWREG0_IVE_VERSION, scheduler);

	major_version = (reg_version & RVE_MAJOR_VERSION_MASK) >> 8;
	minor_version = (reg_version & RVE_MINOR_VERSION_MASK);
	prod_num = (reg_version & RVE_PROD_NUM_MASK) >> 16;

	snprintf(scheduler->version.str, sizeof(scheduler->version.str), "[%x]%x.%x",
		prod_num, major_version, minor_version);

	scheduler->version.major = major_version;
	scheduler->version.minor = minor_version;
	scheduler->version.prod_num = prod_num;

	return 0;
}

void rve_get_monitor_info(struct rve_job *job)
{
	struct rve_sche_pid_info_t *pid_info = NULL;
	struct rve_scheduler_t *scheduler = NULL;
	unsigned long flags;
	uint32_t rd_bandwidth, wr_bandwidth, cycle_cnt;
	int i;

	scheduler = rve_job_get_scheduler(job);
	pid_info = scheduler->session.pid_info;

	/* monitor */
	if (DEBUGGER_EN(MONITOR)) {
		rd_bandwidth = rve_read(RVE_SWCFG37_MONITOR_INFO3, scheduler);
		wr_bandwidth = rve_read(RVE_SWCFG38_MONITOR_INFO4, scheduler);
		cycle_cnt = rve_read(RVE_SWCFG39_MONITOR_INFO5, scheduler);

		/* reset per htimer occur */
		rve_write(2, RVE_SWCFG32_MONITOR_CTRL0, scheduler);

		spin_lock_irqsave(&scheduler->irq_lock, flags);

		for (i = 0; i < RVE_MAX_PID_INFO; i++) {
			if (pid_info[i].pid == job->pid) {
				pid_info[i].last_job_rd_bandwidth = rd_bandwidth;
				pid_info[i].last_job_wr_bandwidth = wr_bandwidth;
				pid_info[i].last_job_cycle_cnt = cycle_cnt;
				break;
			}
		}

		if (DEBUGGER_EN(MSG))
			pr_info("rd_bandwidth = %d, wd_bandwidth = %d, cycle_cnt = %d\n",
				rd_bandwidth, wr_bandwidth, cycle_cnt);

		scheduler->session.rd_bandwidth += rd_bandwidth;
		scheduler->session.wr_bandwidth += wr_bandwidth;
		scheduler->session.cycle_cnt += cycle_cnt;

		spin_unlock_irqrestore(&scheduler->irq_lock, flags);
	}
}
