#define pr_fmt(fmt) "ddrfreq: " fmt
#include <linux/clk.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/freezer.h>
#include <linux/fs.h>
#include <linux/kthread.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/sched/rt.h>
#include <linux/of.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <linux/vmalloc.h>
#include <linux/rockchip/dvfs.h>
#include <dt-bindings/clock/ddr.h>

#include "common.h"

enum {
	DEBUG_DDR = 1U << 0,
	DEBUG_VIDEO_STATE = 1U << 1,
	DEBUG_SUSPEND = 1U << 2,
	DEBUG_VERBOSE = 1U << 3,
};
static int debug_mask = DEBUG_DDR;
module_param(debug_mask, int, S_IRUGO | S_IWUSR | S_IWGRP);
#define dprintk(mask, fmt, ...) do { if (mask & debug_mask) pr_info(fmt, ##__VA_ARGS__); } while (0)

#define MHZ	(1000*1000)
#define KHZ	1000

struct ddr {
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif
	struct dvfs_node *clk_dvfs_node;
	unsigned long normal_rate;
	unsigned long video_rate;
	unsigned long dualview_rate;
	unsigned long idle_rate;
	unsigned long suspend_rate;
	unsigned long reboot_rate;
	bool auto_self_refresh;
	char *mode;
	unsigned long sys_status;
	struct task_struct *task;
	wait_queue_head_t wait;
};
static struct ddr ddr;

module_param_named(sys_status, ddr.sys_status, ulong, S_IRUGO);
module_param_named(auto_self_refresh, ddr.auto_self_refresh, bool, S_IRUGO);
module_param_named(mode, ddr.mode, charp, S_IRUGO);

static noinline void ddrfreq_set_sys_status(int status)
{
	ddr.sys_status |= status;
	wake_up(&ddr.wait);
}

static noinline void ddrfreq_clear_sys_status(int status)
{
	ddr.sys_status &= ~status;
	wake_up(&ddr.wait);
}
int ddr_set_rate(uint32_t nMHz);

static void ddrfreq_mode(bool auto_self_refresh, unsigned long *target_rate, char *name)
{
	ddr.mode = name;
	if (auto_self_refresh != ddr.auto_self_refresh) {
		ddr_set_auto_self_refresh(auto_self_refresh);
		ddr.auto_self_refresh = auto_self_refresh;
		dprintk(DEBUG_DDR, "change auto self refresh to %d when %s\n", auto_self_refresh, name);
	}
	if (*target_rate != dvfs_clk_get_rate(ddr.clk_dvfs_node)) {
		if (dvfs_clk_set_rate(ddr.clk_dvfs_node, *target_rate) == 0) {
			*target_rate = dvfs_clk_get_rate(ddr.clk_dvfs_node);
			dprintk(DEBUG_DDR, "change freq to %lu MHz when %s\n", *target_rate / MHZ, name);
		}
	}
}

static noinline void ddrfreq_work(unsigned long sys_status)
{
	static struct clk *cpu = NULL;
	static struct clk *gpu = NULL;
	unsigned long s = sys_status;

	if (!cpu)
		cpu = clk_get(NULL, "cpu");
	if (!gpu)
		gpu = clk_get(NULL, "gpu");
	
	dprintk(DEBUG_VERBOSE, "sys_status %02lx\n", sys_status);
	
	if (ddr.reboot_rate && (s & SYS_STATUS_REBOOT)) {
		ddrfreq_mode(false, &ddr.reboot_rate, "shutdown/reboot");
	} else if (ddr.suspend_rate && (s & SYS_STATUS_SUSPEND)) {
		ddrfreq_mode(true, &ddr.suspend_rate, "suspend");
	} else if (ddr.dualview_rate && 
		(s & SYS_STATUS_LCDC0) && (s & SYS_STATUS_LCDC1)) {
		ddrfreq_mode(false, &ddr.dualview_rate, "dual-view");
	} else if (ddr.video_rate && 
		((s & SYS_STATUS_VIDEO_720P)||(s & SYS_STATUS_VIDEO_1080P))) {
		ddrfreq_mode(false, &ddr.video_rate, "video");
	} else if (ddr.idle_rate
		&& !(s & SYS_STATUS_GPU)
		&& !(s & SYS_STATUS_RGA)
		&& !(s & SYS_STATUS_CIF0)
		&& !(s & SYS_STATUS_CIF1)
		&& (clk_get_rate(cpu) < 816 * MHZ)
		&& (clk_get_rate(gpu) <= 200 * MHZ)
		) {
		ddrfreq_mode(false, &ddr.idle_rate, "idle");
	} else {
		ddrfreq_mode(false, &ddr.normal_rate, "normal");
	}
}

static int ddrfreq_task(void *data)
{
	set_freezable();

	do {
		unsigned long status = ddr.sys_status;
		ddrfreq_work(status);
		wait_event_freezable(ddr.wait, (status != ddr.sys_status) || kthread_should_stop());
	} while (!kthread_should_stop());

	return 0;
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void ddrfreq_early_suspend(struct early_suspend *h)
{
	dprintk(DEBUG_SUSPEND, "early suspend\n");
	ddrfreq_set_sys_status(SYS_STATUS_SUSPEND);
}

static void ddrfreq_late_resume(struct early_suspend *h)
{
	dprintk(DEBUG_SUSPEND, "late resume\n");
	ddrfreq_clear_sys_status(SYS_STATUS_SUSPEND);
}
#endif

static int video_state_release(struct inode *inode, struct file *file)
{
	dprintk(DEBUG_VIDEO_STATE, "video_state release\n");
	ddrfreq_clear_sys_status(SYS_STATUS_VIDEO);
	return 0;
}

#define VIDEO_LOW_RESOLUTION       (1080*720)
static ssize_t video_state_write(struct file *file, const char __user *buffer,
				 size_t count, loff_t *ppos)
{
	char state;
	char *cookie_pot;
	char *p;
	char *buf = vzalloc(count);
	uint32_t v_width=0,v_height=0,v_sync=0;
	cookie_pot = buf;

	if(!buf)
		return -ENOMEM;

	if (count < 1){
		vfree(buf);
		return -EPERM;
	}

	if (copy_from_user(cookie_pot, buffer, count)) {
		vfree(buf);
		return -EFAULT;
	}

	dprintk(DEBUG_VIDEO_STATE, "video_state write %s,len %d\n", cookie_pot,count);

	state=cookie_pot[0];
	if( (count>=3) && (cookie_pot[2]=='w') )
	{
		strsep(&cookie_pot,",");
		strsep(&cookie_pot,"=");
		p=strsep(&cookie_pot,",");
		v_width = simple_strtol(p,NULL,10);
		strsep(&cookie_pot,"=");
		p=strsep(&cookie_pot,",");
		v_height= simple_strtol(p,NULL,10);
		strsep(&cookie_pot,"=");
		p=strsep(&cookie_pot,",");
		v_sync= simple_strtol(p,NULL,10);
		dprintk(DEBUG_VIDEO_STATE, "video_state %c,width=%d,height=%d,sync=%d\n", state,v_width,v_height,v_sync);
	}

	switch (state) {
	case '0':
		ddrfreq_clear_sys_status(SYS_STATUS_VIDEO);
		break;
	case '1':
		if( (v_width == 0) && (v_height == 0)){
			ddrfreq_set_sys_status(SYS_STATUS_VIDEO_1080P);
		}
		/*else if(v_sync==1){
			if(ddr.video_low_rate && ((v_width*v_height) <= VIDEO_LOW_RESOLUTION) )
				ddrfreq_set_sys_status(SYS_STATUS_VIDEO_720P);
			else
				ddrfreq_set_sys_status(SYS_STATUS_VIDEO_1080P);
		}*/
		else{
			ddrfreq_clear_sys_status(SYS_STATUS_VIDEO);
		}
		break;
	default:
		vfree(buf);
		return -EINVAL;

	}
	vfree(buf);
	return count;
}

static const struct file_operations video_state_fops = {
	.owner	= THIS_MODULE,
	.release= video_state_release,
	.write	= video_state_write,
};

static struct miscdevice video_state_dev = {
	.fops	= &video_state_fops,
	.name	= "video_state",
	.minor	= MISC_DYNAMIC_MINOR,
};
/*
static int ddrfreq_clk_event(int status, unsigned long event)
{
	switch (event) {
	case PRE_RATE_CHANGE:
		ddrfreq_set_sys_status(status);
		break;
	case POST_RATE_CHANGE:
	case ABORT_RATE_CHANGE:
		ddrfreq_clear_sys_status(status);
		break;
	}
	return NOTIFY_OK;
}
*/
#define CLK_NOTIFIER(name, status) \
static int ddrfreq_clk_##name##_event(struct notifier_block *this, unsigned long event, void *ptr) \
{ \
	return ddrfreq_clk_event(SYS_STATUS_##status, event); \
} \
static struct notifier_block ddrfreq_clk_##name##_notifier = { .notifier_call = ddrfreq_clk_##name##_event };

#define REGISTER_CLK_NOTIFIER(name) \
do { \
	struct clk *clk = clk_get(NULL, #name); \
	clk_notifier_register(clk, &ddrfreq_clk_##name##_notifier); \
	clk_put(clk); \
} while (0)

#define UNREGISTER_CLK_NOTIFIER(name) \
do { \
	struct clk *clk = clk_get(NULL, #name); \
	clk_notifier_unregister(clk, &ddrfreq_clk_##name##_notifier); \
	clk_put(clk); \
} while (0)
/*
CLK_NOTIFIER(pd_gpu, GPU);
CLK_NOTIFIER(pd_rga, RGA);
CLK_NOTIFIER(pd_cif0, CIF0);
CLK_NOTIFIER(pd_cif1, CIF1);
CLK_NOTIFIER(pd_lcdc0, LCDC0);
CLK_NOTIFIER(pd_lcdc1, LCDC1);
*/
static int ddrfreq_reboot_notifier_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	u32 timeout = 1000; // 10s
	ddrfreq_set_sys_status(SYS_STATUS_REBOOT);
	while (dvfs_clk_get_rate(ddr.clk_dvfs_node) != ddr.reboot_rate && --timeout) {
		msleep(10);
	}
	if (!timeout) {
		pr_err("failed to set ddr clk from %luMHz to %luMHz when shutdown/reboot\n", dvfs_clk_get_rate(ddr.clk_dvfs_node) / MHZ, ddr.reboot_rate / MHZ);
	}
	return NOTIFY_OK;
}

static struct notifier_block ddrfreq_reboot_notifier = {
	.notifier_call = ddrfreq_reboot_notifier_event,
};

int of_init_ddr_freq_table(void)
{
	struct device_node *clk_ddr_dev_node;
	const struct property *prop;
	const __be32 *val;
	int nr;
	
	clk_ddr_dev_node = of_find_node_by_name(NULL, "clk_ddr");
	if (IS_ERR_OR_NULL(clk_ddr_dev_node)) {
		pr_err("%s: get clk ddr dev node err\n", __func__);
		return PTR_ERR(clk_ddr_dev_node);
	}

	prop = of_find_property(clk_ddr_dev_node, "freq_table", NULL);
	if (!prop)
		return -ENODEV;
	if (!prop->value)
		return -ENODATA;

	nr = prop->length / sizeof(u32);
	if (nr % 2) {
		pr_err("%s: Invalid freq list\n", __func__);
		return -EINVAL;
	}

	val = prop->value;
	while (nr) {
		unsigned long status = be32_to_cpup(val++);
		unsigned long rate = be32_to_cpup(val++) * 1000;

		if (status & SYS_STATUS_NORMAL)
			ddr.normal_rate = rate;
		if (status & SYS_STATUS_SUSPEND)
			ddr.suspend_rate = rate;
		if ((status & SYS_STATUS_VIDEO_720P)||(status & SYS_STATUS_VIDEO_720P))
			ddr.video_rate = rate;
		if ((status & SYS_STATUS_LCDC0)&&(status & SYS_STATUS_LCDC1))
			ddr.dualview_rate = rate;
		if (status & SYS_STATUS_IDLE)
			ddr.idle_rate= rate;
		if (status & SYS_STATUS_REBOOT)
			ddr.reboot_rate= rate;

		nr -= 2;
	}

	return 0;
}

static int ddrfreq_init(void)
{
	struct sched_param param = { .sched_priority = MAX_RT_PRIO - 1 };
	int ret;
	
	ddr.clk_dvfs_node = clk_get_dvfs_node("clk_ddr");
	if (!ddr.clk_dvfs_node){
		return -EINVAL;
	}
	
	init_waitqueue_head(&ddr.wait);
	ddr.mode = "normal";

	ddr.normal_rate = dvfs_clk_get_rate(ddr.clk_dvfs_node);
	ddr.video_rate = ddr.normal_rate;
	ddr.dualview_rate = 0;
	ddr.idle_rate = 0;
	ddr.suspend_rate = ddr.normal_rate;
	ddr.reboot_rate = ddr.normal_rate;	

	of_init_ddr_freq_table();

	if (ddr.idle_rate) {
		//REGISTER_CLK_NOTIFIER(pd_gpu);
		//REGISTER_CLK_NOTIFIER(pd_rga);
		//REGISTER_CLK_NOTIFIER(pd_cif0);
		//REGISTER_CLK_NOTIFIER(pd_cif1);
	}

	if (ddr.dualview_rate) {
             //REGISTER_CLK_NOTIFIER(pd_lcdc0);
             //REGISTER_CLK_NOTIFIER(pd_lcdc1);
	}	

	ret = misc_register(&video_state_dev);
	if (unlikely(ret)) {
		pr_err("failed to register video_state misc device! error %d\n", ret);
		goto err;
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	ddr.early_suspend.suspend = ddrfreq_early_suspend;
	ddr.early_suspend.resume = ddrfreq_late_resume;
	ddr.early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB + 50;
	register_early_suspend(&ddr.early_suspend);
#endif

	ddr.task = kthread_create(ddrfreq_task, NULL, "ddrfreqd");
	if (IS_ERR(ddr.task)) {
		ret = PTR_ERR(ddr.task);
		pr_err("failed to create kthread! error %d\n", ret);
		goto err1;
	}

	sched_setscheduler_nocheck(ddr.task, SCHED_FIFO, &param);
	get_task_struct(ddr.task);
	kthread_bind(ddr.task, 0);
	wake_up_process(ddr.task);

	register_reboot_notifier(&ddrfreq_reboot_notifier);

	pr_info("verion 1.0 20140228\n");
	dprintk(DEBUG_DDR, "normal %luMHz video %luMHz dualview %luMHz idle %luMHz suspend %luMHz reboot %luMHz\n",
		ddr.normal_rate / MHZ, ddr.video_rate / MHZ, ddr.dualview_rate / MHZ, ddr.idle_rate / MHZ, ddr.suspend_rate / MHZ, ddr.reboot_rate / MHZ);

	return 0;

err1:
#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&ddr.early_suspend);
#endif
	misc_deregister(&video_state_dev);
err:
	if (ddr.idle_rate) {
		//UNREGISTER_CLK_NOTIFIER(pd_gpu);
		//UNREGISTER_CLK_NOTIFIER(pd_rga);
		//UNREGISTER_CLK_NOTIFIER(pd_cif0);
		//UNREGISTER_CLK_NOTIFIER(pd_cif1);
	}
       if (ddr.dualview_rate) {
        //UNREGISTER_CLK_NOTIFIER(pd_lcdc0);
        //UNREGISTER_CLK_NOTIFIER(pd_lcdc1);
       }

	return ret;
}
late_initcall(ddrfreq_init);
