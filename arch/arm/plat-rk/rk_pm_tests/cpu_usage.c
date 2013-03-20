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
#include <mach/cru.h>
#include <mach/dvfs.h>
#include <mach/sram.h>
#include <linux/random.h>

#include <linux/fs.h>
#include <asm/unistd.h>
#include <asm/uaccess.h>
#include "rk_pm_tests.h"
#include "cpu_usage.h"
/***************************************************************************/
#define cru_readl(offset)	readl_relaxed(RK2928_CRU_BASE + offset)
#define cru_writel(v, offset)	do { writel_relaxed(v, RK2928_CRU_BASE + offset); dsb(); } while (0)

struct workqueue_struct	*workqueue_cpu_usage0;
struct work_struct 	work_cpu_usage0;
struct workqueue_struct	*workqueue_cpu_usage1;
struct work_struct 	work_cpu_usage1;


int cpu_usage_run = 0;
int cpu_usage_percent = 0;

struct timer_list arm_mode_timer;
#define ARM_MODE_TIMER_MSEC	100
static void arm_mode_timer_handler(unsigned long data)
{
	int i;
	PM_DBG("enter %s\n", __func__);
	if (cpu_usage_run == 0) {
		PM_DBG("STOP\n");
		return ;
	}

	arm_mode_timer.expires  = jiffies + msecs_to_jiffies(ARM_MODE_TIMER_MSEC);
	add_timer(&arm_mode_timer);
	for(i = 0; i < cpu_usage_percent; i++) {
		mdelay(1);
	}
}

static void handler_cpu_usage(struct work_struct *work)
{	
#if 1
	while(cpu_usage_run) {
		barrier();
	}
#else
	del_timer(&arm_mode_timer);
	arm_mode_timer.expires	= jiffies + msecs_to_jiffies(ARM_MODE_TIMER_MSEC);
	add_timer(&arm_mode_timer);
#endif
}
ssize_t cpu_usage_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	char *str = buf;
	str += sprintf(str, "HELLO\n");
	if (str != buf)
		*(str - 1) = '\n';
	return (str - buf);

}
ssize_t cpu_usage_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t n)
{
	char cmd[20];
	int usage = 0;
	sscanf(buf, "%s %d", cmd, &usage);

	if((strncmp(cmd, "start", strlen("start")) == 0)) {
		PM_DBG("get cmd start\n");
		cpu_usage_run = 1;
		
		cpu_usage_percent = (ARM_MODE_TIMER_MSEC * usage) / 100;
		if (workqueue_cpu_usage0 == NULL) {
			PM_ERR("workqueue NULL\n");
			return n;
		}
		if (workqueue_cpu_usage1 == NULL) {
			PM_ERR("workqueue NULL\n");
			return n;
		}

		queue_work_on(0, workqueue_cpu_usage0, &work_cpu_usage0);
		queue_work_on(1, workqueue_cpu_usage1, &work_cpu_usage1);
#if 0
		del_timer(&arm_mode_timer);
		arm_mode_timer.expires	= jiffies + msecs_to_jiffies(ARM_MODE_TIMER_MSEC);
		add_timer(&arm_mode_timer);
#endif
	
	} else if (strncmp(cmd, "stop", strlen("stop")) == 0) {
		PM_DBG("get cmd stop\n");
		cpu_usage_run = 0;
	}

	return n;
}

static int __init cpu_usage_init(void)
{
	workqueue_cpu_usage0 = create_singlethread_workqueue("workqueue_cpu_usage0");
	INIT_WORK(&work_cpu_usage0, handler_cpu_usage);
	workqueue_cpu_usage1 = create_singlethread_workqueue("workqueue_cpu_usage1");
	INIT_WORK(&work_cpu_usage1, handler_cpu_usage);
	setup_timer(&arm_mode_timer, arm_mode_timer_handler, (unsigned int)NULL);
	return 0;
}
late_initcall(cpu_usage_init);
