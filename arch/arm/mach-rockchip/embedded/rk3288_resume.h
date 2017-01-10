/*
 * Rockchip resume header (API from kernel to embedded code)
 *
 * Copyright (c) 2014 Google, Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __MACH_ROCKCHIP_RK3288_RESUME_H
#define __MACH_ROCKCHIP_RK3288_RESUME_H

#define RK3288_NUM_DDR_PORTS		2

#define RK3288_MAX_PWM_REGS		3
#define RK3288_MAX_DDR_PHY_DLL_REGS	7
#define RK3288_MAX_DDR_CTRL_REGS	64
#define RK3288_MAX_DDR_PHY_REGS		29
#define RK3288_MAX_DDR_MSCH_REGS	6
#define RK3288_MAX_DDR_PHY_ZQCR_REGS	2

#define RK3288_BOGUS_OFFSET		0xffffffff

/**
 * rk3288_ddr_save - Parameters needed to reinit SDRAM after suspend
 *
 * This structure contains data needed to restore SDRAM after suspend.
 * Generally:
 * - There are two controllers and we need to save data for both.  We save
 *   the same registers for both, so you see two sets of values and one sets
 *   of offsets (the register offset from the base of the controller).
 *   There are a few registers that are always the same for both controllers
 *   so we only save one set of values.
 *
 * Offsets are saved at init time and vals are saved on each suspend.
 *
 * NOTE: offsets are u32 values right now to keep everything 32-bit and avoid
 * 8-bit and 16-bit access problems in PMU SRAM (see WARNING below).
 * Technically, though, 8-bit and 16-bit _reads_ seem to work, so as long as
 * we were careful in setting things up we could possibly save some memory by
 * storing 16-bit offsets.  We can investigate if we ever get that tight on
 * space.
 */
struct rk3288_ddr_save_data {
	u32 pwm_addrs[RK3288_MAX_PWM_REGS];
	u32 pwm_vals[RK3288_MAX_PWM_REGS];

	u32 phy_dll_offsets[RK3288_MAX_DDR_PHY_DLL_REGS];
	u32 phy_dll_vals[RK3288_NUM_DDR_PORTS][RK3288_MAX_DDR_PHY_DLL_REGS];

	u32 ctrl_offsets[RK3288_MAX_DDR_CTRL_REGS];
	u32 ctrl_vals[RK3288_MAX_DDR_CTRL_REGS];		/* Both same */

	u32 phy_offsets[RK3288_MAX_DDR_PHY_REGS];
	u32 phy_vals[RK3288_NUM_DDR_PORTS][RK3288_MAX_DDR_PHY_REGS];

	u32 msch_offsets[RK3288_MAX_DDR_MSCH_REGS];
	u32 msch_vals[RK3288_NUM_DDR_PORTS][RK3288_MAX_DDR_MSCH_REGS];

	u32 phy_zqcr_offsets[RK3288_MAX_DDR_PHY_ZQCR_REGS];
	u32 phy_zqcr_vals[RK3288_MAX_DDR_PHY_ZQCR_REGS];	/* Both same */
};

/**
 * rk3288_resume_params - Parameter space for the resume code
 *
 * This structure is at the start of the resume blob and is used to communicate
 * between the resume blob and the callers.
 *
 * WARNING: This structure is sitting in PMU SRAM.  If you try to write to that
 * memory using an 8-bit access (or even 16-bit) you'll get an imprecise data
 * abort and it will be very hard to debug.  Keep everything in here as 32-bit
 * wide and aligned.  YOU'VE BEEN WARNED.
 *
 * @resume_loc:		The value here should be the resume address that the CPU
 *			is programmed to go to at resume time.
 *
 * @l2ctlr_f:		If non-zero we'll set l2ctlr at resume time.
 * @l2ctlr:		The value to set l2ctlr to at resume time.
 *
 * @ddr_resume_f	True if we should resume DDR.
 * @ddr_save_data:	Data for save / restore of DDR.
 *
 * @cpu_resume:		The function to jump to when we're all done.
 */
struct rk3288_resume_params {
	/* This is compiled in and can be read to find the resume location */
	__noreturn void (*resume_loc)(void);

	/* Filled in by the client of the resume code */
	u32 l2ctlr_f;		/* u32 not bool to avoid 8-bit SRAM access */
	u32 l2ctlr;

	u32 ddr_resume_f;	/* u32 not bool to avoid 8-bit SRAM access */
	struct rk3288_ddr_save_data ddr_save_data;

	__noreturn void (*cpu_resume)(void);
};

#endif /* __MACH_ROCKCHIP_RK3288_RESUME_H */
