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

#include <linux/fs.h>
#include <asm/unistd.h>
#include <asm/uaccess.h>
#include "rk_pm_tests.h"
#include "clk_rate.h"
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

ssize_t clk_rate_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	char *str = buf;
	str += sprintf(str, "set	[clk_name]	[rate(Hz)]\n"
			"reset	[clk_name]	[rate(Hz) = 0]\n");
	if (str != buf)
		*(str - 1) = '\n';
	return (str - buf);

}

ssize_t clk_rate_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t n)
{
	char cmd[20], clk_name[20], msg[50];
	int rate;
	struct clk *clk;
	sscanf(buf, "%s %s %d", cmd, clk_name, &rate);


	clk = clk_get(NULL, clk_name);
	if (IS_ERR(clk)) {
		PM_ERR("%s get clk %s error\n", __func__, clk_name);
		return n;
	}

	if (0 == strncmp(cmd, "set", strlen("set"))) {
		PM_DBG("Get command set %s %dHz\n", clk_name, rate);
		if (file_read(FILE_GOV_MODE, msg) != 0) {
			PM_ERR("read current governor error\n");
			return n;
		} else {
			PM_DBG("current governor = %s\n", msg);
		}

		strcpy(msg, "userspace");
		if (file_write(FILE_GOV_MODE, msg) != 0) {
			PM_ERR("set current governor error\n");
			return n;
		}

		dvfs_clk_enable_limit(clk, rate, rate);
		clk_set_rate(clk, rate);

	} else if (0 == strncmp(cmd, "reset", strlen("reset"))) {	
		PM_DBG("Get command reset %s\n", clk_name);
		if (file_read(FILE_GOV_MODE, msg) != 0) {
			PM_ERR("read current governor error\n");
			return n;
		} else {
			PM_DBG("current governor = %s\n", msg);
		}

		strcpy(msg, "interactive");
		if (file_write(FILE_GOV_MODE, msg) != 0) {
			PM_ERR("set current governor error\n");
			return n;
		}

		dvfs_clk_disable_limit(clk);
	}
	
	return n;
}

