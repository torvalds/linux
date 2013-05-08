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
#include <linux/cpufreq.h>
#include <mach/ddr.h>
#include <mach/dvfs.h>

#include <linux/fs.h>
#include <asm/unistd.h>
#include <asm/uaccess.h>
#include "rk_pm_tests.h"
#include "maxfreq.h"
/*
 * Warning: Must set ddr_freq = 200M, gpu_freq = 200M; to make sure ddr
 * and gpu can run under log = 1V.
 * Remember to close cpufreq's max freq limit
 * 
 * */
static struct cpufreq_frequency_table *dvfs_arm_table_save;
static struct cpufreq_frequency_table dvfs_arm_table_maxfreq[] = {
	{.frequency = 252 * 1000,	.index = 1000 * 1000},
	{.frequency = 312 * 1000, 	.index = 1000 * 1000},
	{.frequency = 504 * 1000,	.index = 1000 * 1000},
	{.frequency = 816 * 1000,	.index = 1000 * 1000},
	{.frequency = 912 * 1000,	.index = 1000 * 1000},
	{.frequency = 936 * 1000,	.index = 1000 * 1000},
	{.frequency = 960 * 1000,	.index = 1000 * 1000},
	{.frequency = 984 * 1000,	.index = 1000 * 1000},
	{.frequency = 1008 * 1000,	.index = 1000 * 1000},
	{.frequency = 1032 * 1000,	.index = 1000 * 1000},
	{.frequency = 1056 * 1000,	.index = 1000 * 1000},
	{.frequency = 1080 * 1000,	.index = 1000 * 1000},
	{.frequency = 1104 * 1000,	.index = 1000 * 1000},
	{.frequency = 1128 * 1000,	.index = 1000 * 1000},
	{.frequency = 1152 * 1000,	.index = 1000 * 1000},
	{.frequency = 1176 * 1000,	.index = 1000 * 1000},
	{.frequency = 1200 * 1000,	.index = 1000 * 1000},
	{.frequency = 1224 * 1000,	.index = 1000 * 1000},
	{.frequency = 1248 * 1000,	.index = 1000 * 1000},
	{.frequency = 1272 * 1000,	.index = 1000 * 1000},
	{.frequency = 1296 * 1000,	.index = 1000 * 1000},
	{.frequency = 1320 * 1000,	.index = 1000 * 1000},
	{.frequency = 1344 * 1000,	.index = 1000 * 1000},
	{.frequency = 1368 * 1000,	.index = 1000 * 1000},
	{.frequency = 1392 * 1000,	.index = 1000 * 1000},
	{.frequency = 1416 * 1000,	.index = 1000 * 1000},
	{.frequency = 1440 * 1000,	.index = 1000 * 1000},
	{.frequency = 1464 * 1000,	.index = 1000 * 1000},
	{.frequency = 1488 * 1000,	.index = 1000 * 1000},
	{.frequency = 1512 * 1000,	.index = 1000 * 1000},
	{.frequency = 1536 * 1000,	.index = 1000 * 1000},
	{.frequency = 1560 * 1000,	.index = 1000 * 1000},
	{.frequency = 1584 * 1000,	.index = 1000 * 1000},
	{.frequency = 1608 * 1000,	.index = 1000 * 1000},
	{.frequency = 1632 * 1000,	.index = 1000 * 1000},
	{.frequency = 1656 * 1000,	.index = 1000 * 1000},
	{.frequency = 1680 * 1000,	.index = 1000 * 1000},
	{.frequency = 1704 * 1000,	.index = 1000 * 1000},
	{.frequency = 1728 * 1000,	.index = 1000 * 1000},
	{.frequency = 1752 * 1000,	.index = 1000 * 1000},
	{.frequency = 1776 * 1000,	.index = 1000 * 1000},
	{.frequency = 1800 * 1000,	.index = 1000 * 1000},
	{.frequency = 1824 * 1000,	.index = 1000 * 1000},
	{.frequency = 1848 * 1000,	.index = 1000 * 1000},
	{.frequency = 1872 * 1000,	.index = 1000 * 1000},
	{.frequency = 1896 * 1000,	.index = 1000 * 1000},
	{.frequency = 1920 * 1000,	.index = 1000 * 1000},
	{.frequency = 1944 * 1000,	.index = 1000 * 1000},
	{.frequency = 1968 * 1000,	.index = 1000 * 1000},
	{.frequency = 1992 * 1000,	.index = 1000 * 1000},
	{.frequency = 2016 * 1000,	.index = 1000 * 1000},
	{.frequency = 2040 * 1000,	.index = 1000 * 1000},
	{.frequency = 2064 * 1000,	.index = 1000 * 1000},
	{.frequency = 2088 * 1000,	.index = 1000 * 1000},
	{.frequency = 2112 * 1000,	.index = 1000 * 1000},
	{.frequency = 2136 * 1000,	.index = 1000 * 1000},
	{.frequency = 2160 * 1000,	.index = 1000 * 1000},
	{.frequency = 2184 * 1000,	.index = 1000 * 1000},
	{.frequency = 2208 * 1000,	.index = 1000 * 1000},
	{.frequency = CPUFREQ_TABLE_END},
};
static int curr_mode = 0;
/***************************************************************************/
#define FILE_GOV_MODE "/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"
#define FILE_SETSPEED "/sys/devices/system/cpu/cpu0/cpufreq/scaling_setspeed"
#define FILE_CUR_FREQ "/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq"

static int file_read(char *file_path, char *buf)
{
	struct file *file = NULL;
	mm_segment_t old_fs;
	loff_t offset = 0;

	PM_DBG("read %s\n", file_path);
	file = filp_open(file_path, O_RDONLY, 0);

	if (IS_ERR(file)) {
		PM_ERR("%s error open file  %s\n", __func__, file_path);
		return -1;
	}

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	file->f_op->read(file, (char *)buf, 32, &offset);
	sscanf(buf, "%s", buf);

	set_fs(old_fs);
	filp_close(file, NULL);  

	file = NULL;

	return 0;

}

static int file_write(char *file_path, char *buf)
{
	struct file *file = NULL;
	mm_segment_t old_fs;
	loff_t offset = 0;

	PM_DBG("write %s %s size = %d\n", file_path, buf, strlen(buf));
	file = filp_open(file_path, O_RDWR, 0);

	if (IS_ERR(file)) {
		PM_ERR("%s error open file  %s\n", __func__, file_path);
		return -1;
	}

	old_fs = get_fs();
	set_fs(KERNEL_DS);

	file->f_op->write(file, (char *)buf, strlen(buf), &offset);

	set_fs(old_fs);
	filp_close(file, NULL);  

	file = NULL;

	return 0;

}

ssize_t maxfreq_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	char *str = buf;
	str += sprintf(str, "setarmvolt	[ArmVoltage(mV)]\n"
			"reset\n");
	if (str != buf)
		*(str - 1) = '\n';
	return (str - buf);

}

ssize_t maxfreq_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t n)
{
	char cmd[20], gov_mode[50];
	int armVolt, i = 0;
	struct clk *clk;
	sscanf(buf, "%s %d", cmd, &armVolt);


	clk = clk_get(NULL, "cpu");
	if (IS_ERR(clk)) {
		PM_ERR("%s get arm clk error\n", __func__);
		return n;
	}

	if (0 == strncmp(cmd, "setarmvolt", strlen("setarmvolt"))) {
		if (armVolt < 500000 || armVolt > 1500000) {
			PM_ERR("Arm Volt = %d, out of bound\n", armVolt);
			return n;
		}

		PM_DBG("Get command Set Arm Volt = %d mV\n", armVolt);
		if (file_read(FILE_GOV_MODE, gov_mode) != 0) {
			PM_ERR("read current governor error\n");
			return n;
		} else {
			PM_DBG("current governor = %s\n", gov_mode);
		}

		strcpy(gov_mode, "userspace");
		if (file_write(FILE_GOV_MODE, gov_mode) != 0) {
			PM_ERR("set current governor error\n");
			return n;
		}

		if (curr_mode == 0) {
			// save normal table
			PM_DBG("save dvfs normal table\n");
			dvfs_arm_table_save = dvfs_get_freq_volt_table(clk);
			curr_mode = 1;
		}
		for (i = 0; dvfs_arm_table_maxfreq[i].frequency != CPUFREQ_TABLE_END; i++) {
			dvfs_arm_table_maxfreq[i].index = armVolt;
		}
		
		dvfs_set_freq_volt_table(clk, dvfs_arm_table_maxfreq);
		clk_set_rate(clk, dvfs_arm_table_maxfreq[0].frequency * 1000);

	} else if (0 == strncmp(cmd, "reset", strlen("reset"))) {	
		PM_DBG("Get command reset\n");
		if (file_read(FILE_GOV_MODE, gov_mode) != 0) {
			PM_ERR("read current governor error\n");
			return n;
		} else {
			PM_DBG("current governor = %s\n", gov_mode);
		}

		dvfs_set_freq_volt_table(clk, dvfs_arm_table_save);
		strcpy(gov_mode, "interactive");
		if (file_write(FILE_GOV_MODE, gov_mode) != 0) {
			PM_ERR("set current governor error\n");
			return n;
		}
		curr_mode = 0;
	}
	
	return n;
}

