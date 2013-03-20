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

#include "rk_pm_tests.h"
#include "freq_limit.h"
/***************************************************************************/
ssize_t freq_limit_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	char *str = buf;
	str += sprintf(str, "command(enable/disable) clk_name	min_rate	max_rate\n");
	if (str != buf)
		*(str - 1) = '\n';
	return (str - buf);

}

ssize_t freq_limit_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t n)
{
	char cmd[20], clk_name[20];
	unsigned int min_rate, max_rate;
	struct clk *clk;
	sscanf(buf, "%s %s %u %u", cmd, clk_name, &min_rate, &max_rate);
	
	clk = clk_get(NULL, clk_name);
	if (IS_ERR(clk)) {
		PM_ERR("get clk %s error\n", __func__);
		return n;
	}

	if (0 == strncmp(cmd, "enable", strlen("enable"))) {
		PM_DBG("limit enable clk(%s) min(%d) max(%d)\n", clk_name, min_rate, max_rate);
		dvfs_clk_enable_limit(clk, min_rate, max_rate);
		clk_set_rate(clk, min_rate);
	
	} else if (0 == strncmp(cmd, "disable", strlen("disable"))) {
		PM_DBG("limit disable clk(%s)\n", clk_name);
		dvfs_clk_disable_limit(clk);
	
	} else {
		PM_ERR("unknown command\n");
	}
	return n;
}

