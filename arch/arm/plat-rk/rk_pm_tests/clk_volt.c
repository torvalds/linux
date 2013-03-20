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
#include "clk_volt.h"
/***************************************************************************/
ssize_t clk_volt_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	char *str = buf;
	str += sprintf(str, "[regulaotr_name]	[voltage(uV)]\n");
	if (str != buf)
		*(str - 1) = '\n';
	return (str - buf);

}

ssize_t clk_volt_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t n)
{
	char regulator_name[20];
	unsigned int volt;
	int ret = 0;
	struct regulator *regulator;
	
	sscanf(buf, "%s %u", regulator_name, &volt);
	
	regulator = dvfs_get_regulator(regulator_name);
	if (IS_ERR(regulator)) {
		PM_ERR("%s get dvfs_regulator %s error\n", __func__, regulator_name);
		return n;
	}

	ret = regulator_set_voltage(regulator, volt, volt); 
	PM_DBG("ret = %d\n", ret);


//	if (0 == strncmp(cmd, "enable", strlen("enable"))) {
	return n;
}

