/*
 * arch/arm/mach-sun3i/pm/standby/sun3i_sram_entry.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include <sun3i_standby.h>

static struct aw_pm_arg pm_arg;
int standby_main(struct aw_pm_arg *arg)
{
	int i=0;
	struct sys_reg_t old_env;

	/* save args in sram */
	pm_arg.wakeup_mode = arg->wakeup_mode;
	for(i = 0;i<AW_PMU_ARG_LEN;i++)
		pm_arg.param[i] = arg->param[i];
	pm_arg.pmu_par.dev_addr = arg->pmu_par.dev_addr;
	pm_arg.pmu_par.reg_addr = arg->pmu_par.reg_addr;
	pm_arg.pmu_par.reg_val = arg->pmu_par.reg_val;

	/* save system env */
	standby_save_env(&old_env);

	/* enter system low power level */
	standby_enter_low();

	/* standby and wake up*/
	standby_loop(&pm_arg);

	/* restore system env */
	standby_restore_env(&old_env);

	/* exit system low power level */
	standby_exit_low();

	return 0;
}

