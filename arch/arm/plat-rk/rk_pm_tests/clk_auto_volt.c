#include <linux/clk.h>
#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/workqueue.h>
#include <linux/regulator/machine.h>
#include <mach/dvfs.h>

#include "rk_pm_tests.h"
#include "clk_auto_volt.h"
/***************************************************************************/
#define VOLT_UP    1
#define VOLT_DOWN  0

static int wq_is_run = 0;
static unsigned int volt_direction;
static unsigned int begin_volt;
static unsigned int stop_volt;
static unsigned int volt;
static unsigned int volt_step = 25000;
static struct regulator *regulator = NULL;

static struct workqueue_struct *scal_volt_wq = NULL;
static struct delayed_work scal_volt_work;

static unsigned int timer_tick;

static void scal_volt_func(struct work_struct *work)
{
	int ret = 0;

	ret = regulator_set_voltage(regulator, volt, volt);
	if (ret) {
		PM_ERR("regulator_set_voltage failed!\n");
		PM_ERR("stop!\n");
		return;
	}
	else
		PM_DBG("regulator_set_voltage: %d(uV)\n",volt);

	if (volt_direction == VOLT_DOWN) {
		volt = volt - volt_step;
		if(volt <  stop_volt) {
			PM_DBG("stop!\n");
			return;
		}
	}
	else if (volt_direction == VOLT_UP) {
		volt = volt + volt_step;
		if (volt > stop_volt) {
			PM_DBG("stop!\n");
			return;
		}
	}

	queue_delayed_work(scal_volt_wq, &scal_volt_work, msecs_to_jiffies(timer_tick*1000));		
}

ssize_t clk_auto_volt_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	char *str = buf;
	str += sprintf(str, "[start/stop] [up/down]  [regulator_name]  [begin_volt(uV)]  [stop_volt(uv)]  [volt_step(uv)]  [timer_tick(s)]\n");

	return (str - buf);
}

ssize_t clk_auto_volt_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t n)
{
	char cmd[10];
	char volt_flag[10];
	char regulator_name[20];

	sscanf(buf, "%s %s %s %u %u %u %u", cmd, volt_flag, regulator_name, &begin_volt, &stop_volt, &volt_step, &timer_tick);

	if (0 == strncmp(cmd, "start", strlen("start"))) {
		if (0 == strncmp(volt_flag, "up", strlen("up"))) {	
			if (begin_volt >= stop_volt) {
				PM_ERR("wrong! begin_volt >= stop_volt!\n");
				return -EINVAL;
			}
			volt_direction = VOLT_UP;			

		} else if (0 == strncmp(volt_flag, "down", strlen("down"))) {	
			if (begin_volt <= stop_volt) {
				PM_ERR("wrong! begin_volt <= stop_volt!\n");
				return -EINVAL;
			}
			volt_direction = VOLT_DOWN;

		} else {  	
			PM_ERR("argument %s is invalid!\n",volt_flag);
			return -EINVAL;
		}

		regulator = dvfs_get_regulator(regulator_name);
		if (IS_ERR_OR_NULL(regulator)) {
			PM_ERR("%s get dvfs_regulator %s error\n", __func__, regulator_name);
			return -ENOMEM;
		}		

		if (wq_is_run == 1) {
			cancel_delayed_work(&scal_volt_work);
			flush_workqueue(scal_volt_wq);
			//destroy_workqueue(scal_volt_wq);
			wq_is_run = 0;
		}

		PM_DBG("begin!\n");		

		volt = begin_volt;

		scal_volt_wq = create_workqueue("scal volt wq");
		INIT_DELAYED_WORK(&scal_volt_work, scal_volt_func);
		queue_delayed_work(scal_volt_wq, &scal_volt_work, msecs_to_jiffies(timer_tick*1000));	

		wq_is_run = 1;	

	} else if (0 == strncmp(cmd, "stop", strlen("stop"))) {
		if (wq_is_run == 1) {
			cancel_delayed_work(&scal_volt_work);
			flush_workqueue(scal_volt_wq);
			//destroy_workqueue(scal_volt_wq);
			wq_is_run = 0;
		}

	} else {
		PM_ERR("argument %s is invalid!\n", cmd);
		return -EINVAL;
	}

	return  n;
}

