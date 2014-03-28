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
//#include <plat/dma-pl330.h>
#include <linux/mfd/wm831x/core.h>
#include <linux/sysfs.h>
#include <linux/err.h>
//#include <mach/ddr.h>
//#include <mach/dvfs.h>
#include <linux/watchdog.h>


#include <linux/fs.h>
#include <asm/unistd.h>
#include <asm/uaccess.h>
#include "rk_pm_tests.h"
#include "rk_pm_tests.h"
#include "dvfs_table_scan.h"
/***************************************************************************/
struct workqueue_struct	*workqueue_dvfs_table_scan;
struct work_struct 	work_dvfs_table_scan;
struct timer_list dvfs_table_scan_timer;
#define DVFS_TABLE_T_MSEC	1000
struct file *file = NULL;
mm_segment_t old_fs;
loff_t offset = 0;
static int dvfs_table_scan_run = 0;

ssize_t dvfs_table_scan_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	char *str = buf;
	str += sprintf(str, "start: \tstart scan dvfs table, send alive every 1s\n"
			"stop: \tstop send alive signal\n");
	if (str != buf)
		*(str - 1) = '\n';
	return (str - buf);

}
void rk29_wdt_start(void);
void rk29_wdt_stop(void);
void rk29_wdt_keepalive(void);
int rk29_wdt_set_heartbeat(int timeout);

#define ALIVE_INTERVAL			2	//s
#define WATCHDOG_TIMEOUT		(10*ALIVE_INTERVAL)
#define RESERVE_TIME_FOR_TIMEOUT 	600000	//ms

ssize_t dvfs_table_scan_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t n)
{
	char cmd[20];
	int cur_test_need_time=0; //ms
	static int cur_test_timeout_cnt = 0, start_flag = 0;

	sscanf(buf, "%s %d", cmd, &cur_test_need_time);

	printk("%s: get command <%s>\n", __func__, cmd);
	if (0 == strncmp(cmd, "start", strlen("start"))) {
		if (cur_test_need_time == 0)
			return n;
		cur_test_timeout_cnt = (cur_test_need_time + RESERVE_TIME_FOR_TIMEOUT)/1000/ALIVE_INTERVAL;
		rk29_wdt_start();
		rk29_wdt_set_heartbeat(WATCHDOG_TIMEOUT);
		start_flag = 1;
	} else if (0 == strncmp(cmd, "alive", strlen("alive"))) {
		if(start_flag == 0)
			return n;
		printk("cur_test_timeout_cnt:%d\n", cur_test_timeout_cnt);
		if (cur_test_timeout_cnt-- > 0)		
			rk29_wdt_keepalive();

	} else if (0 == strncmp(cmd, "stop", strlen("stop"))) {
		if(start_flag == 0)
			return n;
		cur_test_timeout_cnt = -1;
		rk29_wdt_stop();
		start_flag = 0;
	}

	return n;
}
