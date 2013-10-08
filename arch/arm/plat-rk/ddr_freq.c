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
#include <linux/reboot.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include <mach/board.h>
#include <mach/clock.h>
#include <mach/ddr.h>
#include <mach/dvfs.h>

#include <linux/rk_fb.h>
//#include <linux/delay.h>

#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <linux/vmalloc.h>

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
	SYS_STATUS_VIDEO_720P,       // 0x04
	SYS_STATUS_VIDEO_1080P,       // 0x08
	SYS_STATUS_GPU,		// 0x10
	SYS_STATUS_RGA,		// 0x20
	SYS_STATUS_CIF0,	// 0x40
	SYS_STATUS_CIF1,	// 0x80
	SYS_STATUS_REBOOT,	// 0x100
	SYS_STATUS_LCDC0,	// 0x200
	SYS_STATUS_LCDC1,	// 0x400
};

struct ddr {
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif
	struct clk *pll;
	struct clk *clk;
	unsigned long normal_rate;
	unsigned long video_rate;
	unsigned long video_low_rate;
       unsigned long dualview_rate;
	unsigned long idle_rate;
	unsigned long suspend_rate;
	unsigned long reboot_rate;
        char video_state;
	bool auto_self_refresh;
	char *mode;
	unsigned long sys_status;
	struct task_struct *task;
	wait_queue_head_t wait;
};
static struct ddr ddr;

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
	if (ddr.reboot_rate && (s & (1 << SYS_STATUS_REBOOT))) {
		ddrfreq_mode(false, &ddr.reboot_rate, "shutdown/reboot");
	} else if (ddr.suspend_rate && (s & (1 << SYS_STATUS_SUSPEND))) {
		ddrfreq_mode(true, &ddr.suspend_rate, "suspend");
	} else if (ddr.dualview_rate
	      && (s & (1 << SYS_STATUS_LCDC0))
	      && (s & (1 << SYS_STATUS_LCDC1))
	      ) {
		ddrfreq_mode(false, &ddr.dualview_rate, "dual-view");
	} else if ((ddr.video_rate || ddr.video_low_rate) && (s & (1 << SYS_STATUS_VIDEO))) {
		if(ddr.video_low_rate && (s & (1 << SYS_STATUS_VIDEO_720P)))
			ddrfreq_mode(false, &ddr.video_low_rate, "video low");
		else if(ddr.video_rate && (s & (1 << SYS_STATUS_VIDEO_1080P)))
			ddrfreq_mode(false, &ddr.video_rate, "video");
		else
			ddrfreq_mode(false, &ddr.normal_rate, "video normal");
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
static volatile bool __sramdata cpu_pause[NR_CPUS];
static inline bool is_cpu0_paused(unsigned int cpu) { smp_rmb(); return cpu_pause[0]; }
static inline bool is_cpuX_paused(unsigned int cpu) { smp_rmb(); return cpu_pause[cpu]; }
static inline void set_cpuX_paused(unsigned int cpu, bool pause) { cpu_pause[cpu] = pause; smp_wmb(); }
static inline void set_cpu0_paused(bool pause)
{
	cpu_pause[0] = pause;
	smp_wmb();
}
#define MAX_TIMEOUT (16000000UL << 6) //>0.64s

/* Do not use stack, safe on SMP */
static void __sramfunc pause_cpu(void *info)
{
	unsigned int cpu = raw_smp_processor_id();

	set_cpuX_paused(cpu, true);
	while (is_cpu0_paused(cpu));
	set_cpuX_paused(cpu, false);
}

static void wait_cpu(void *info)
{
}

static int _ddr_change_freq_(uint32_t nMHz,struct ddr_freq_t ddr_freq_t)
{
	u32 timeout = MAX_TIMEOUT;
	unsigned int cpu;
	unsigned int this_cpu = smp_processor_id();
	int ret = 0;

	cpu_maps_update_begin();
	local_bh_disable();
	set_cpu0_paused(true);
	smp_call_function((smp_call_func_t)pause_cpu, NULL, 0);
	for_each_online_cpu(cpu) {
		if (cpu == this_cpu)
			continue;
		while (!is_cpuX_paused(cpu) && --timeout);
		if (timeout == 0) {
			pr_err("pause cpu %d timeout\n", cpu);
			goto out;
		}
	}

	ret = ddr_change_freq_sram(nMHz,ddr_freq_t);

out:
	set_cpu0_paused(false);
	local_bh_enable();
	smp_call_function(wait_cpu, NULL, true);
	cpu_maps_update_done();

	return ret;
}

static void _ddr_change_freq(uint32_t nMHz)
{
	struct ddr_freq_t ddr_freq_t;
	int test_count=0;

	ddr_freq_t.screen_ft_us = 0;
	ddr_freq_t.t0 = 0;
	ddr_freq_t.t1 = 0;

#if defined (DDR_CHANGE_FREQ_IN_LCDC_VSYNC)
	do
	{
		if(rk_fb_poll_wait_frame_complete() == true)
		{
			ddr_freq_t.t0 = cpu_clock(0);
			ddr_freq_t.screen_ft_us = rk_fb_get_prmry_screen_ft();

			test_count++;
                        if(test_count > 10) //test 10 times
                        {
				ddr_freq_t.screen_ft_us = 0xfefefefe;
				dprintk(DEBUG_DDR,"%s:test_count exceed maximum!\n",__func__);
                        }
			dprintk(DEBUG_VERBOSE,"%s:test_count=%d\n",__func__,test_count);
			usleep_range(ddr_freq_t.screen_ft_us-test_count*1000,ddr_freq_t.screen_ft_us-test_count*1000);

			flush_cache_all();
			outer_flush_all();
			flush_tlb_all();
		}
	}while(_ddr_change_freq_(nMHz,ddr_freq_t)==0);
#else
	_ddr_change_freq_(nMHz,ddr_freq_t);
#endif
}
#else
static void _ddr_change_freq(uint32_t nMHz)
{
	ddr_change_freq(nMHz);
}
#endif

static void ddr_set_rate(uint32_t nMHz)
{
	_ddr_change_freq(nMHz);
	ddr.clk->rate = ddr.clk->recalc(ddr.clk);
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
		ddrfreq_clear_sys_status(SYS_STATUS_VIDEO_720P);
		ddrfreq_clear_sys_status(SYS_STATUS_VIDEO_1080P);
		break;
	case '1':
		ddrfreq_set_sys_status(SYS_STATUS_VIDEO);

		if( (v_width == 0) && (v_height == 0)){
			ddrfreq_set_sys_status(SYS_STATUS_VIDEO_1080P);
		}
		else if(v_sync==1){
			if(ddr.video_low_rate && ((v_width*v_height) <= VIDEO_LOW_RESOLUTION) )
				ddrfreq_set_sys_status(SYS_STATUS_VIDEO_720P);
			else
				ddrfreq_set_sys_status(SYS_STATUS_VIDEO_1080P);
		}
		else{
			ddrfreq_clear_sys_status(SYS_STATUS_VIDEO_720P);
			ddrfreq_clear_sys_status(SYS_STATUS_VIDEO_1080P);
		}
		break;
	default:
		vfree(buf);
		return -EINVAL;

	}
	ddr.video_state = state;
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
CLK_NOTIFIER(pd_lcdc0, LCDC0);
CLK_NOTIFIER(pd_lcdc1, LCDC1);

static int ddrfreq_reboot_notifier_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	u32 timeout = 1000; // 10s
	ddrfreq_set_sys_status(SYS_STATUS_REBOOT);
	while (clk_get_rate(ddr.clk) != ddr.reboot_rate && --timeout) {
		msleep(10);
	}
	if (!timeout) {
		pr_err("failed to set ddr clk from %luMHz to %luMHz when shutdown/reboot\n", clk_get_rate(ddr.clk) / MHZ, ddr.reboot_rate / MHZ);
	}
	return NOTIFY_OK;
}

static struct notifier_block ddrfreq_reboot_notifier = {
	.notifier_call = ddrfreq_reboot_notifier_event,
};

static int ddr_scale_rate_for_dvfs(struct clk *clk, unsigned long rate, dvfs_set_rate_callback set_rate)
{
        ddr_set_rate(rate/(1000*1000));
	/* return 0 when ok */
        return !( (clk_get_rate(clk)/MHZ) == (rate/MHZ));
}

#if defined(CONFIG_ARCH_RK3066B)
static int ddrfreq_scanfreq_datatraing_3168(void)
{
    struct cpufreq_frequency_table *table;
    uint32_t dqstr_freq,dqstr_value;
    uint32_t min_freq,max_freq;
    int i;
    table = dvfs_get_freq_volt_table(clk_get(NULL, "ddr"));
    if (!table)
    {
        pr_err("failed to get ddr freq volt table\n");
    }
    for (i = 0; table && table[i].frequency != CPUFREQ_TABLE_END; i++)
    {
        if(i == 0)
            min_freq = table[i].frequency / 1000;

        max_freq = table[i].frequency / 1000;
    }

    //get data training value for RK3066B ddr_change_freq
    for(dqstr_freq=min_freq; dqstr_freq<=max_freq; dqstr_freq=dqstr_freq+50)
    {
        if (clk_set_rate(ddr.clk, dqstr_freq*MHZ) != 0)
        {
            pr_err("failed to clk_set_rate ddr.clk %dhz\n",dqstr_freq*MHZ);
        }
        dqstr_value=(dqstr_freq-min_freq+1)/50;

        ddr_get_datatraing_value_3168(false,dqstr_value,min_freq);
    }
    ddr_get_datatraing_value_3168(true,0,min_freq);
    dprintk(DEBUG_DDR,"get datatraing from %dMhz to %dMhz\n",min_freq,max_freq);
    return 0;
}
#endif

static int ddrfreq_init(void)
{
	int i, ret;
	struct cpufreq_frequency_table *table;
	int ddrfreq_version = 0;

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
	dvfs_clk_register_set_rate_callback(ddr.clk, ddr_scale_rate_for_dvfs);

	ddr.normal_rate = clk_get_rate(ddr.clk);
	ddr.reboot_rate = ddr.normal_rate;

	table = dvfs_get_freq_volt_table(ddr.clk);
	if (!table) {
		pr_err("failed to get ddr freq volt table\n");
	}

	for (i = 0; table && table[i].frequency != CPUFREQ_TABLE_END; i++) {
		if (table[i].frequency % 1000) {
			ddrfreq_version = 1;
		}

		if (table[i].frequency % 1000 > 100) {
			ddrfreq_version = 2;
			break;
		}
	}
        
	if (ddrfreq_version==0) {
		ddr.video_rate = 300 * MHZ;
		ddr.dualview_rate = ddr.normal_rate;
		ddr.suspend_rate = 200 * MHZ;
	}

	for (i = 0; ddrfreq_version == 1 && table && table[i].frequency != CPUFREQ_TABLE_END; i++) {
		unsigned int mode = table[i].frequency % 1000;
		unsigned long rate;

		table[i].frequency -= mode;
		rate = table[i].frequency * 1000;

		switch (mode) {
		case DDR_FREQ_NORMAL:
			ddr.normal_rate = rate;
			break;
		case DDR_FREQ_VIDEO_LOW:
			ddr.video_low_rate = rate;
			break;
		case DDR_FREQ_VIDEO:
			ddr.video_rate = rate;
			break;
		case DDR_FREQ_DUALVIEW:
			ddr.dualview_rate= rate;
			break;
		case DDR_FREQ_IDLE:
			ddr.idle_rate = rate;
			break;
		case DDR_FREQ_SUSPEND:
			ddr.suspend_rate = rate;
			break;
		}
	}

	for (i = 0; ddrfreq_version == 2 && table && table[i].frequency != CPUFREQ_TABLE_END; i++) {
		unsigned int mode = table[i].frequency % 1000;
		unsigned long rate;

		table[i].frequency -= mode;
		rate = table[i].frequency * 1000;

		if( (mode&DDR_FREQ_NORMAL) == DDR_FREQ_NORMAL)
			ddr.normal_rate = rate;

		if( (mode&DDR_FREQ_VIDEO_LOW) == DDR_FREQ_VIDEO_LOW)
			ddr.video_low_rate = rate;

		if( (mode&DDR_FREQ_VIDEO) == DDR_FREQ_VIDEO)
			ddr.video_rate = rate;

		if( (mode&DDR_FREQ_DUALVIEW) == DDR_FREQ_DUALVIEW)
			ddr.dualview_rate= rate;

		if( (mode&DDR_FREQ_IDLE) == DDR_FREQ_IDLE)
			ddr.idle_rate = rate;

		if( (mode&DDR_FREQ_SUSPEND) == DDR_FREQ_SUSPEND)
			ddr.suspend_rate = rate;
	}

	if (ddr.idle_rate) {
		REGISTER_CLK_NOTIFIER(pd_gpu);
		REGISTER_CLK_NOTIFIER(pd_rga);
		REGISTER_CLK_NOTIFIER(pd_cif0);
		REGISTER_CLK_NOTIFIER(pd_cif1);
	}

	if (ddr.dualview_rate) {
             REGISTER_CLK_NOTIFIER(pd_lcdc0);
             REGISTER_CLK_NOTIFIER(pd_lcdc1);
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

#if defined(CONFIG_ARCH_RK3066B)
       ddrfreq_scanfreq_datatraing_3168();
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

	pr_info("verion 3.2 20130917\n");
	pr_info("fix cpu pause bug\n");
	dprintk(DEBUG_DDR, "normal %luMHz video %luMHz video_low %luMHz dualview %luMHz idle %luMHz suspend %luMHz reboot %luMHz\n",
		ddr.normal_rate / MHZ, ddr.video_rate / MHZ, ddr.video_low_rate / MHZ, ddr.dualview_rate / MHZ, ddr.idle_rate / MHZ, ddr.suspend_rate / MHZ, ddr.reboot_rate / MHZ);

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
       if (ddr.dualview_rate) {
        UNREGISTER_CLK_NOTIFIER(pd_lcdc0);
        UNREGISTER_CLK_NOTIFIER(pd_lcdc1);
       }

	return ret;
}
late_initcall(ddrfreq_late_init);
