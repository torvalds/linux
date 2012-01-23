/**
 * arch/arm/mach-softwinner/pm/standby/sun3i_sram_entry.c
 *
 *This application can only run in sram for allwin chips
 *This file is the entrance of sram and do not add any new function here!.
 *
 *author: yekai
 *date:2011-03-22
 *version:0.1
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

