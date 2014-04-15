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
#include <linux/rockchip/dvfs.h>

#include "rk_pm_tests.h"
#include "clk_volt.h"
/***************************************************************************/
ssize_t clk_volt_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	char *str = buf;
	str += sprintf(str, "usage:\n");
	str += sprintf(str, "get voltage:get [regulaotr_name]\n");
	str += sprintf(str, "set voltage:set [regulaotr_name] [voltage(uV)]\n");
	str += sprintf(str, "set suspend voltage:set [regulaotr_name] [voltage(uV)+1]\n");
	if (str != buf)
		*(str - 1) = '\n';
	return (str - buf);

}

#define SET_SUSPEND_VOLT_FLAG	(1<<0)

ssize_t clk_volt_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t n)
{
	
	struct regulator *regulator;
	char cmd[20], regulator_name[20];
	unsigned int volt;
	int need_put_regulator=0, ret=0;

	sscanf(buf, "%s %s %u", cmd, regulator_name, &volt);

	PM_DBG("%s: cmd(%s), regulator_name(%s), volt(%u)\n", 
		__func__, cmd, regulator_name, volt);

	regulator = dvfs_get_regulator(regulator_name);
	if (IS_ERR_OR_NULL(regulator)) {
		regulator = regulator_get(NULL, regulator_name);
		if (IS_ERR(regulator)){
			PM_ERR("%s: get dvfs_regulator %s error\n", __func__, regulator_name);
			return n;
		}
		need_put_regulator = 1;
	}	

	if (!strncmp(cmd, "set", strlen("set"))){
		if (volt & SET_SUSPEND_VOLT_FLAG){
			volt &= ~SET_SUSPEND_VOLT_FLAG;
			//ret = regulator_set_suspend_voltage(regulator, volt); 
			if (!ret)
				PM_DBG("%s: set %s suspend volt to %u uV ok\n", __func__, regulator_name, volt);
			else
				PM_DBG("%s: regulator_set_suspend_voltage err(%d)\n", __func__, ret);
		}else{
			ret = regulator_set_voltage(regulator, volt, volt); 
			if (!ret)
				PM_DBG("%s: set %s volt to %u uV ok\n", __func__, regulator_name, regulator_get_voltage(regulator));
			else
				PM_DBG("%s: regulator_set_voltage err(%d)\n", __func__, ret);
		}
		
	}
	if (!strncmp(cmd, "get", strlen("get"))){
		printk("%s: %s current is %d uV\n", 
			__func__, regulator_name, regulator_get_voltage(regulator));
	}

	if (need_put_regulator)
		regulator_put(regulator);

	return n;
}

