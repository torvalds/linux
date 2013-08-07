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
#include "maxfreq.h"
#include "rk_pm_tests.h"
#include "dvfs_table_scan.h"
/***************************************************************************/
#if defined(CONFIG_ARCH_RK3026)
#define TTYS3_FILE_PATH "/dev/ttyS1"
#else
#define TTYS3_FILE_PATH "/dev/ttyS3"
#endif
struct workqueue_struct	*workqueue_dvfs_table_scan;
struct work_struct 	work_dvfs_table_scan;
struct timer_list dvfs_table_scan_timer;
#define DVFS_TABLE_T_MSEC	1000
struct file *file = NULL;
mm_segment_t old_fs;
loff_t offset = 0;
static int dvfs_table_scan_run = 0;
static int file_read(char *file_path, char *buf)
{
	PM_DBG("read %s\n", file_path);

	file->f_op->read(file, (char *)buf, 32, &offset);

	return 0;
}

static int file_write(char *file_path, char *buf)
{
	PM_DBG("write %s %s size = %d\n", file_path, buf, strlen(buf));

	file->f_op->write(file, (char *)buf, strlen(buf), &offset);

	return 0;
}
static void dvfs_table_scan_timer_handler(unsigned long data)
{
	//PM_DBG("enter %s\n", __func__);
	char sendbuf[20];
	//static int sendcnt = 0;
	memset(sendbuf, 0, sizeof(sendbuf));
	if (dvfs_table_scan_run == 0) {
		PM_DBG("%s: STOP\n", __func__);
		return ;
	}
	//sprintf(sendbuf, "alive%d", sendcnt++);
	sprintf(sendbuf, "alive");
	PM_DBG("Sending alive signal...... %s\n", sendbuf);
	file_write(TTYS3_FILE_PATH, sendbuf);

	mod_timer(&dvfs_table_scan_timer, jiffies + msecs_to_jiffies(DVFS_TABLE_T_MSEC));
}
static void handler_dvfs_table_scan(struct work_struct *work)
{
	char buf[100];
	int ret = 0;
	while (dvfs_table_scan_run) {
		memset(buf, 0, sizeof(buf));
		ret = file_read(TTYS3_FILE_PATH, buf);
		//if (ret < 0)
		//	PM_ERR("read error %d\n", ret);
		if (strlen(buf) != 0)
			PM_DBG("%s: get %s|len=%d\n", __func__, buf, strlen(buf));
		//mdelay(100);
	}
}

static void uart_init(void)
{
	file = filp_open(TTYS3_FILE_PATH, O_RDWR | O_NOCTTY, 0);
	//file = filp_open(TTYS3_FILE_PATH, O_RDWR, 0);
	if (IS_ERR(file)) {
		PM_ERR("%s error open file  %s\n", __func__, TTYS3_FILE_PATH);
		return ;
	}
	old_fs = get_fs();
	set_fs(KERNEL_DS);
}
static void uart_deinit(void)
{
	set_fs(old_fs);
	filp_close(file, NULL);  
}
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
ssize_t dvfs_table_scan_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t n)
{
	char cmd[20];
	
	sscanf(buf, "%s", cmd);
	PM_DBG("%s: get command <%s>\n", __func__, cmd);
	if (0 == strncmp(cmd, "start", strlen("start"))) {
		uart_init();
		dvfs_table_scan_run = 1;
		file_write(TTYS3_FILE_PATH, "start");
		//queue_work(workqueue_dvfs_table_scan, &work_dvfs_table_scan);
		//dvfs_table_scan_timer.expires = jiffies + msecs_to_jiffies(DVFS_TABLE_T_MSEC);
		//add_timer(&dvfs_table_scan_timer);
		uart_deinit();

	} else if (0 == strncmp(cmd, "alive", strlen("alive"))) {
		uart_init();
		file_write(TTYS3_FILE_PATH, "alive");
		uart_deinit();

	} else if (0 == strncmp(cmd, "stop", strlen("stop"))) {
		uart_init();
		file_write(TTYS3_FILE_PATH, "stop");
		dvfs_table_scan_run = 0;
		uart_deinit();
	}
	return n;
}

static int __init dvfs_table_scan_init(void)
{
	workqueue_dvfs_table_scan = create_singlethread_workqueue("workqueue_dvfs_table_scan");
	INIT_WORK(&work_dvfs_table_scan, handler_dvfs_table_scan);
	setup_timer(&dvfs_table_scan_timer, dvfs_table_scan_timer_handler, (unsigned int)NULL);
	return 0;
}
late_initcall(dvfs_table_scan_init);
