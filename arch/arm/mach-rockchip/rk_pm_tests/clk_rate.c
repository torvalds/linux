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
#include <linux/mfd/wm831x/core.h>
#include <linux/sysfs.h>
#include <linux/err.h>

#include <linux/fs.h>
#include <asm/unistd.h>
#include <asm/uaccess.h>
#include <linux/rockchip/dvfs.h>

#include "rk_pm_tests.h"
#include "clk_rate.h"
/***************************************************************************/
#define FILE_GOV_MODE "/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor"

#if 0
static int file_read(char *file_path, char *buf)
{
	struct file *file = NULL;
	mm_segment_t old_fs;
	loff_t offset = 0;

	PM_DBG("%s: read %s\n", __func__, file_path);
	file = filp_open(file_path, O_RDONLY, 0);

	if (IS_ERR(file)) {
		PM_ERR("%s: error open file  %s\n", __func__, file_path);
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
#endif

static int file_write(char *file_path, char *buf)
{
	struct file *file = NULL;
	mm_segment_t old_fs;
	loff_t offset = 0;

	PM_DBG("%s: write %s %s size = %d\n", __func__,
	       file_path, buf, (int)strlen(buf));
	file = filp_open(file_path, O_RDWR, 0);

	if (IS_ERR(file)) {
		PM_ERR("%s: error open file  %s\n", __func__, file_path);
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
	str += sprintf(str, "set [clk_name] [rate(Hz)]\n"
			"rawset [clk_name] [rate(Hz)]\n");
	if (str != buf)
		*(str - 1) = '\n';
	return (str - buf);

}

ssize_t clk_rate_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t n)
{
	struct dvfs_node *clk_dvfs_node = NULL;
	struct clk *clk;
	char cmd[20], clk_name[20];
	unsigned long rate=0;
	int ret=0;
		
	sscanf(buf, "%s %s %lu", cmd, clk_name, &rate);
	
	PM_DBG("%s: cmd(%s), clk_name(%s), rate(%lu)\n", __func__, cmd, clk_name, rate);

	if (!strncmp(cmd, "set", strlen("set"))) {
		clk_dvfs_node = clk_get_dvfs_node(clk_name);
		if (!clk_dvfs_node) {
			PM_ERR("%s: clk(%s) get dvfs node error\n", __func__, clk_name);
			return n;
		}
		
		if (!strncmp(clk_name, "clk_core", strlen("clk_core"))) {
			if (file_write(FILE_GOV_MODE, "userspace") != 0) {
				PM_ERR("%s: set current governor error\n", __func__);
				return n;
			}
		}

		ret = dvfs_clk_enable_limit(clk_dvfs_node, rate, rate);
	} else {
		clk = clk_get(NULL, clk_name);
		if (IS_ERR_OR_NULL(clk)) {
			PM_ERR("%s: get clk(%s) err(%ld)\n", 
				__func__, clk_name, PTR_ERR(clk));
			return n;
		}
		
		if (!strncmp(cmd, "rawset", strlen("rawset"))) {
			ret = clk_set_rate(clk, rate);
		} else if (!strncmp(cmd, "open", strlen("open"))) {
			ret = clk_prepare_enable(clk);
		} else if (!strncmp(cmd, "close", strlen("close"))) {	
			clk_disable_unprepare(clk);
		}
	}

	if (ret) {
		PM_ERR("%s: set rate err(%d)", __func__, ret);
	}
	return n;
}

