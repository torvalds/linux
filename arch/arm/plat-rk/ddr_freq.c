#define pr_fmt(fmt) "ddrfreq: " fmt
#include <linux/clk.h>
#include <linux/cpu.h>
#include <linux/cpufreq.h>
#include <linux/delay.h>
#include <linux/earlysuspend.h>
#include <linux/freezer.h>
#include <linux/fs.h>
#include <linux/kthread.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include <mach/board.h>
#include <mach/clock.h>
#include <mach/ddr.h>
#include <mach/dvfs.h>

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

enum SYS_STATUS {
	SYS_STATUS_SUSPEND = 0,	// 0x01
	SYS_STATUS_VIDEO,	// 0x02
	SYS_STATUS_GPU,		// 0x04
	SYS_STATUS_RGA,		// 0x08
	SYS_STATUS_CIF0,	// 0x10
	SYS_STATUS_CIF1,	// 0x20
};

struct ddr {
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif
	struct clk *pll;
	struct clk *clk;
	unsigned long normal_rate;
	unsigned long video_rate;
	unsigned long idle_rate;
	unsigned long suspend_rate;
        char video_state;
	bool auto_self_refresh;
	char *mode;
	unsigned long sys_status;
	struct task_struct *task;
	wait_queue_head_t wait;
} ddr;

module_param_named(sys_status, ddr.sys_status, ulong, S_IRUGO);
module_param_named(video_state, ddr.video_state, byte, S_IRUGO);
module_param_named(auto_self_refresh, ddr.auto_self_refresh, bool, S_IRUGO);
module_param_named(mode, ddr.mode, charp, S_IRUGO);

static noinline void ddrfreq_set_sys_status(enum SYS_STATUS status)
{
	set_bit(status, &ddr.sys_status);
	wake_up(&ddr.wait);
}

static noinline void ddrfreq_clear_sys_status(enum SYS_STATUS status)
{
	clear_bit(status, &ddr.sys_status);
	wake_up(&ddr.wait);
}

static void ddrfreq_mode(bool auto_self_refresh, unsigned long *target_rate, char *name)
{
	ddr.mode = name;
	if (auto_self_refresh != ddr.auto_self_refresh) {
		ddr_set_auto_self_refresh(auto_self_refresh);
		ddr.auto_self_refresh = auto_self_refresh;
		dprintk(DEBUG_DDR, "change auto self refresh to %d when %s\n", auto_self_refresh, name);
	}
	if (*target_rate != clk_get_rate(ddr.clk)) {
		if (clk_set_rate(ddr.clk, *target_rate) == 0) {
			*target_rate = clk_get_rate(ddr.clk);
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
	if (s & (1 << SYS_STATUS_SUSPEND)) {
		ddrfreq_mode(true, &ddr.suspend_rate, "suspend");
	} else if (s & (1 << SYS_STATUS_VIDEO)) {
		ddrfreq_mode(false, &ddr.video_rate, "video");
	} else if (ddr.idle_rate
		&& !(s & (1 << SYS_STATUS_GPU))
		&& !(s & (1 << SYS_STATUS_RGA))
		&& !(s & (1 << SYS_STATUS_CIF0))
		&& !(s & (1 << SYS_STATUS_CIF1))
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

#ifdef CONFIG_SMP
static volatile bool __sramdata cpu1_pause;
static inline bool is_cpu1_paused(void) { smp_rmb(); return cpu1_pause; }
static inline void set_cpu1_pause(bool pause) { cpu1_pause = pause; smp_wmb(); }
#define MAX_TIMEOUT (16000000UL << 6) //>0.64s

static void __ddr_change_freq(void *info)
{
	uint32_t *value = info;
	u32 timeout = MAX_TIMEOUT;

	while (!is_cpu1_paused() && --timeout);
	if (timeout == 0)
		return;

	*value = ddr_change_freq(*value);

	set_cpu1_pause(false);
}

/* Do not use stack, safe on SMP */
static void __sramfunc pause_cpu1(void *info)
{
	u32 timeout = MAX_TIMEOUT;
	unsigned long flags;
	local_irq_save(flags);

	set_cpu1_pause(true);
	while (is_cpu1_paused() && --timeout);

	local_irq_restore(flags);
}

static uint32_t _ddr_change_freq(uint32_t nMHz)
{
	int this_cpu = get_cpu();

	set_cpu1_pause(false);
	if (this_cpu == 0) {
		if (smp_call_function_single(1, (smp_call_func_t)pause_cpu1, NULL, 0) == 0) {
			u32 timeout = MAX_TIMEOUT;
			while (!is_cpu1_paused() && --timeout);
			if (timeout == 0)
				goto out;
		}

		nMHz = ddr_change_freq(nMHz);

		set_cpu1_pause(false);
	} else {
		smp_call_function_single(0, __ddr_change_freq, &nMHz, 0);

		pause_cpu1(NULL);
	}

out:
	put_cpu();

	return nMHz;
}
#else
static uint32_t _ddr_change_freq(uint32_t nMHz)
{
	return ddr_change_freq(nMHz);
}
#endif

uint32_t ddr_set_rate(uint32_t nMHz)
{
	nMHz = _ddr_change_freq(nMHz);
	clk_set_rate(ddr.pll, 0);
	return nMHz;
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
	ddr.video_state = '0';
	ddrfreq_clear_sys_status(SYS_STATUS_VIDEO);
	return 0;
}

static ssize_t video_state_write(struct file *file, const char __user *buffer,
				 size_t count, loff_t *ppos)
{
	char state;

	if (count < 1)
		return count;
	if (copy_from_user(&state, buffer, 1)) {
		return -EFAULT;
	}

	dprintk(DEBUG_VIDEO_STATE, "video_state write %c\n", state);
	switch (state) {
	case '0':
		ddrfreq_clear_sys_status(SYS_STATUS_VIDEO);
		break;
	case '1':
		ddrfreq_set_sys_status(SYS_STATUS_VIDEO);
		break;
	default:
		return -EINVAL;
	}
	ddr.video_state = state;
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

static int ddrfreq_clk_event(enum SYS_STATUS status, unsigned long event)
{
	switch (event) {
	case CLK_PRE_ENABLE:
		ddrfreq_set_sys_status(status);
		break;
	case CLK_ABORT_ENABLE:
	case CLK_POST_DISABLE:
		ddrfreq_clear_sys_status(status);
		break;
	}

	return NOTIFY_OK;
}

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

CLK_NOTIFIER(pd_gpu, GPU);
CLK_NOTIFIER(pd_rga, RGA);
CLK_NOTIFIER(pd_cif0, CIF0);
CLK_NOTIFIER(pd_cif1, CIF1);

static int ddrfreq_init(void)
{
	int i, ret;
	struct cpufreq_frequency_table *table;

	init_waitqueue_head(&ddr.wait);
	ddr.video_state = '0';
	ddr.mode = "normal";

	ddr.pll = clk_get(NULL, "ddr_pll");
	ddr.clk = clk_get(NULL, "ddr");
	if (IS_ERR(ddr.clk)) {
		ret = PTR_ERR(ddr.clk);
		ddr.clk = NULL;
		pr_err("failed to get ddr clk, error %d\n", ret);
		return ret;
	}

	ddr.normal_rate = clk_get_rate(ddr.clk);
	ddr.video_rate = 300 * MHZ;
	ddr.suspend_rate = 200 * MHZ;

	table = dvfs_get_freq_volt_table(ddr.clk);
	if (!table) {
		pr_err("failed to get ddr freq volt table\n");
	}
	for (i = 0; table && table[i].frequency != CPUFREQ_TABLE_END; i++) {
		unsigned int mode = table[i].frequency % 1000;
		unsigned long rate;

		table[i].frequency -= mode;
		rate = table[i].frequency * 1000;

		switch (mode) {
		case DDR_FREQ_NORMAL:
			ddr.normal_rate = rate;
			break;
		case DDR_FREQ_VIDEO:
			ddr.video_rate = rate;
			break;
		case DDR_FREQ_IDLE:
			ddr.idle_rate = rate;
			break;
		case DDR_FREQ_SUSPEND:
			ddr.suspend_rate = rate;
			break;
		}
	}

	if (ddr.idle_rate) {
		REGISTER_CLK_NOTIFIER(pd_gpu);
		REGISTER_CLK_NOTIFIER(pd_rga);
		REGISTER_CLK_NOTIFIER(pd_cif0);
		REGISTER_CLK_NOTIFIER(pd_cif1);
	}

	return 0;

}
core_initcall(ddrfreq_init);

static int ddrfreq_late_init(void)
{
	int ret;
	struct sched_param param = { .sched_priority = MAX_RT_PRIO - 1 };

	if (!ddr.clk) {
		return -EINVAL;
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

	pr_info("verion 1.0\n");
	dprintk(DEBUG_VERBOSE, "normal %luMHz video %luMHz idle %luMHz suspend %luMHz\n",
		ddr.normal_rate / MHZ, ddr.video_rate / MHZ, ddr.idle_rate / MHZ, ddr.suspend_rate / MHZ);

	return 0;

err1:
#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&ddr.early_suspend);
#endif
	misc_deregister(&video_state_dev);
err:
	if (ddr.idle_rate) {
		UNREGISTER_CLK_NOTIFIER(pd_gpu);
		UNREGISTER_CLK_NOTIFIER(pd_rga);
		UNREGISTER_CLK_NOTIFIER(pd_cif0);
		UNREGISTER_CLK_NOTIFIER(pd_cif1);
	}

	return ret;
}
late_initcall(ddrfreq_late_init);
