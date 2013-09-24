#include <linux/clk.h>
#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/resume-trace.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/syscalls.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/cpu.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/regulator/machine.h>
#include <plat/dma-pl330.h>
#include <linux/mfd/wm831x/core.h>
#include <linux/sysfs.h>
#include <linux/err.h>
#include <mach/ddr.h>
#include <mach/dvfs.h>
#include <linux/cpufreq.h>

#include "rk_pm_tests.h"
#include "dvfs_table_reset.h"
/***************************************************************************/
#define ARM_TABLE_CHANGE	1
#define GPU_TABLE_CHANGE	0
#define DDR_TABLE_CHANGE	0
#define VOLT_MODIFY_FREQ_L		37500
#define VOLT_MODIFY_FREQ_H		25000
#if ARM_TABLE_CHANGE
static struct cpufreq_frequency_table *arm_table;
#endif
#if GPU_TABLE_CHANGE
static struct cpufreq_frequency_table *gpu_table;
#endif
#if DDR_TABLE_CHANGE
static struct cpufreq_frequency_table *ddr_table;
#endif
struct clk *clk_arm, *clk_gpu, *clk_ddr;
static int __init dvfs_table_reset_init(void)
{
	int i = 0;
	printk("get arm clk OK\n");
#if ARM_TABLE_CHANGE
	clk_arm = clk_get(NULL, "cpu");
	if (IS_ERR(clk_arm))
		return PTR_ERR(clk_arm);
	PM_DBG("get arm clk OK\n");
	arm_table = dvfs_get_freq_volt_table(clk_arm);

	for (i = 0; arm_table[i].frequency != CPUFREQ_TABLE_END; i++) {
		if (arm_table[i].frequency <= 1008 * 1000) {
			printk("%s: set freq(%d), volt(%d), modify(%d)\n", __func__,
					arm_table[i].frequency, arm_table[i].index, VOLT_MODIFY_FREQ_L);
			arm_table[i].index -= VOLT_MODIFY_FREQ_L;
			printk("%s: set freq(%d), volt(%d), modify(%d)\n", __func__,
					arm_table[i].frequency, arm_table[i].index, VOLT_MODIFY_FREQ_L);
		} else {
			printk("%s: set freq(%d), volt(%d), modify(%d)\n", __func__,
					arm_table[i].frequency, arm_table[i].index, VOLT_MODIFY_FREQ_H);
			arm_table[i].index -= VOLT_MODIFY_FREQ_H;
			printk("%s: set freq(%d), volt(%d), modify(%d)\n", __func__,
					arm_table[i].frequency, arm_table[i].index, VOLT_MODIFY_FREQ_H);
		}
	}
	dvfs_set_freq_volt_table(clk_arm, arm_table);
#endif

#if GPU_TABLE_CHANGE
	clk_gpu = clk_get(NULL, "gpu");
	if (IS_ERR(clk_gpu))
		return PTR_ERR(clk_gpu);
	PM_DBG("get gpu clk OK\n");
	gpu_table = dvfs_get_freq_volt_table(clk_gpu);

	for (i = 0; gpu_table[i].frequency != CPUFREQ_TABLE_END; i++) {
		gpu_table[i].index -= VOLT_MODIFY_FREQ_H;
	}
	dvfs_set_freq_volt_table(clk_gpu, gpu_table);
#endif

#if DDR_TABLE_CHANGE
	clk_ddr = clk_get(NULL, "ddr");
	if (IS_ERR(clk_ddr))
		return PTR_ERR(clk_ddr);
	PM_DBG("get ddr clk OK\n");
	ddr_table = dvfs_get_freq_volt_table(clk_ddr);

	for (i = 0; gpu_table[i].frequency != CPUFREQ_TABLE_END; i++) {
		ddr_table[i] -= VOLT_MODIFY_FREQ_H;
	}
	dvfs_set_freq_volt_table(clk_ddr, ddr_table);
#endif

	return 0;
}

static void __exit dvfs_table_reset_exit(void)
{
	int i = 0;
#if ARM_TABLE_CHANGE
	for (i = 0; arm_table[i].frequency != CPUFREQ_TABLE_END; i++) {
		if (arm_table[i].frequency <= 1008 * 1000)
			arm_table[i].index += VOLT_MODIFY_FREQ_L;
		else
			arm_table[i].index += VOLT_MODIFY_FREQ_H;
	}
	dvfs_set_freq_volt_table(clk_arm, arm_table);
#endif

#if GPU_TABLE_CHANGE
	for (i = 0; gpu_table[i].frequency != CPUFREQ_TABLE_END; i++) {
		gpu_table[i].index += VOLT_MODIFY_FREQ_H;
	}
	dvfs_set_freq_volt_table(clk_gpu, gpu_table);
#endif

#if DDR_TABLE_CHANGE
	for (i = 0; ddr_table[i].frequency != CPUFREQ_TABLE_END; i++) {
		ddr_table[i].index += VOLT_MODIFY_FREQ_H;
	}
	dvfs_set_freq_volt_table(clk_ddr, ddr_table);
#endif

}

module_init(dvfs_table_reset_init);
module_exit(dvfs_table_reset_exit);
