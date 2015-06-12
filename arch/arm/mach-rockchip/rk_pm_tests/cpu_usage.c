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
#include <linux/random.h>

#include <linux/fs.h>
#include <asm/unistd.h>
#include <asm/uaccess.h>
#include "rk_pm_tests.h"
#include "cpu_usage.h"
/***************************************************************************/
static DEFINE_PER_CPU(struct work_struct, work_cpu_usage);
static DEFINE_PER_CPU(struct workqueue_struct *, workqueue_cpu_usage);

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
	struct workqueue_struct	*workqueue;
	struct work_struct *work;
	char cmd[20];
	int usage = 0;
	int cpu;

	sscanf(buf, "%s %d", cmd, &usage);

	if((!strncmp(cmd, "start", strlen("start")))) {
		PM_DBG("get cmd start\n");
		cpu_usage_run = 1;
		
		cpu_usage_percent = (ARM_MODE_TIMER_MSEC * usage) / 100;


		for_each_online_cpu(cpu){
			work = &per_cpu(work_cpu_usage, cpu);
			workqueue = per_cpu(workqueue_cpu_usage, cpu);
			if (!work || !workqueue){
				PM_ERR("work or workqueue NULL\n");
				return n;
			}	
			queue_work_on(cpu, workqueue, work);
		}
#if 0
		del_timer(&arm_mode_timer);
		arm_mode_timer.expires	= jiffies + msecs_to_jiffies(ARM_MODE_TIMER_MSEC);
		add_timer(&arm_mode_timer);
#endif
	
	} else if (!strncmp(cmd, "stop", strlen("stop"))) {
		PM_DBG("get cmd stop\n");
		cpu_usage_run = 0;
	}

	return n;
}

static int __init cpu_usage_init(void)
{
	struct workqueue_struct	**workqueue;
	struct work_struct 	*work;
	int cpu;
	
	for_each_online_cpu(cpu){	
		work = &per_cpu(work_cpu_usage, cpu);
		workqueue = &per_cpu(workqueue_cpu_usage, cpu);
		if (!work || !workqueue){
			PM_ERR("work or workqueue NULL\n");
			return -1;
		}
			
		*workqueue = create_singlethread_workqueue("workqueue_cpu_usage");
		INIT_WORK(work, handler_cpu_usage);
	}

	setup_timer(&arm_mode_timer, arm_mode_timer_handler, 0);
	return 0;
}
late_initcall(cpu_usage_init);
