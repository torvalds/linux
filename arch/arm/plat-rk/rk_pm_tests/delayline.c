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
#include "delayline.h"
/***************************************************************************/
#define ARM_MODE_TIMER_MSEC	500
static int delayline_time_msec = ARM_MODE_TIMER_MSEC;

static struct timer_list delayline_timer;
int running = 0;
extern int rk3188_get_delayline_value(void);

static void timer_handler(unsigned long data)
{
	int delayline = rk3188_get_delayline_value();
	PM_DBG("enter %s\n", __func__);

	if (running == 0) {
		PM_DBG("STOP\n");
		return ;
	}

	mod_timer(&delayline_timer, jiffies + msecs_to_jiffies(delayline_time_msec));
	printk("Current delayline value = %d\n", delayline);
}

ssize_t delayline_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	char *str = buf;
	int delayline = rk3188_get_delayline_value();
	str += sprintf(str, "Current delayline value = %d\necho start [time] > delayline\n", delayline);
	if (str != buf)
		*(str - 1) = '\n';
	return (str - buf);

}

ssize_t delayline_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t n)
{
	
	char cmd[20];
	sscanf(buf, "%s %d", cmd, &delayline_time_msec);

	if((strncmp(cmd, "start", strlen("start")) == 0)) {
		PM_DBG("get cmd start, time = %d\n", delayline_time_msec);
		running = 1;
		delayline_timer.expires	= jiffies + msecs_to_jiffies(delayline_time_msec);
		add_timer(&delayline_timer);
	}else if ((strncmp(cmd, "stop", strlen("stop")) == 0)) {
		PM_DBG("get cmd stop\n");
		running = 0;
	}
	return n;
}

static int __init delayline_init(void)
{
	setup_timer(&delayline_timer, timer_handler, (unsigned int)NULL);
	return 0;
}
late_initcall(delayline_init);
