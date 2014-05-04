#define pr_fmt(fmt) "ddrfreq: " fmt
#include <linux/clk.h>
#include <linux/fb.h>
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
#include <linux/fb.h>
#include <linux/input.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <linux/vmalloc.h>
#include <linux/rockchip/common.h>
#include <linux/rockchip/dvfs.h>
#include <dt-bindings/clock/ddr.h>
#include <asm/io.h>
#include <linux/rockchip/grf.h>
#include <linux/rockchip/iomap.h>

extern int rockchip_cpufreq_reboot_limit_freq(void);

static struct dvfs_node *clk_cpu_dvfs_node = NULL;
static int ddr_boost = 0;
static int reboot_config_done = 0;

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
	struct dvfs_node *clk_dvfs_node;
	unsigned long normal_rate;
	unsigned long video_rate;
	unsigned long dualview_rate;
	unsigned long idle_rate;
	unsigned long suspend_rate;
	unsigned long reboot_rate;
	bool auto_freq;
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

static void ddrfreq_mode(bool auto_self_refresh, unsigned long *target_rate, char *name)
{
	unsigned int min_rate, max_rate;
	int freq_limit_en;

	ddr.mode = name;
	if (auto_self_refresh != ddr.auto_self_refresh) {
		ddr_set_auto_self_refresh(auto_self_refresh);
		ddr.auto_self_refresh = auto_self_refresh;
		dprintk(DEBUG_DDR, "change auto self refresh to %d when %s\n", auto_self_refresh, name);
	}
	if (*target_rate != dvfs_clk_get_rate(ddr.clk_dvfs_node)) {
		freq_limit_en = dvfs_clk_get_limit(clk_cpu_dvfs_node, &min_rate, &max_rate);
		dvfs_clk_enable_limit(clk_cpu_dvfs_node, 600000000, -1);
		if (dvfs_clk_set_rate(ddr.clk_dvfs_node, *target_rate) == 0) {
			*target_rate = dvfs_clk_get_rate(ddr.clk_dvfs_node);
			dprintk(DEBUG_DDR, "change freq to %lu MHz when %s\n", *target_rate / MHZ, name);
		}

		if (freq_limit_en) {
			dvfs_clk_enable_limit(clk_cpu_dvfs_node, min_rate, max_rate);
		} else {
			dvfs_clk_disable_limit(clk_cpu_dvfs_node);
		}
	}
}

static void ddr_freq_input_event(struct input_handle *handle, unsigned int type,
		unsigned int code, int value)
{
	if (type == EV_ABS)
		ddr_boost = 1;
}

static int ddr_freq_input_connect(struct input_handler *handler,
		struct input_dev *dev, const struct input_device_id *id)
{
	struct input_handle *handle;
	int error;

	handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "ddr_freq";

	error = input_register_handle(handle);
	if (error)
		goto err2;

	error = input_open_device(handle);
	if (error)
		goto err1;

	return 0;
err1:
	input_unregister_handle(handle);
err2:
	kfree(handle);
	return error;
}

static void ddr_freq_input_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id ddr_freq_ids[] = {
	
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT |
			INPUT_DEVICE_ID_MATCH_ABSBIT,
		.evbit = { BIT_MASK(EV_ABS) },
		.absbit = { [BIT_WORD(ABS_MT_POSITION_X)] =
			BIT_MASK(ABS_MT_POSITION_X) |
			BIT_MASK(ABS_MT_POSITION_Y) },
	},
	
	{
		.flags = INPUT_DEVICE_ID_MATCH_KEYBIT |
			INPUT_DEVICE_ID_MATCH_ABSBIT,
		.keybit = { [BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH) },
		.absbit = { [BIT_WORD(ABS_X)] =
			BIT_MASK(ABS_X) | BIT_MASK(ABS_Y) },
	},
	
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT,
		.evbit = { BIT_MASK(EV_KEY) },
	},
	{ },
};

static struct input_handler ddr_freq_input_handler = {
	.event		= ddr_freq_input_event,
	.connect	= ddr_freq_input_connect,
	.disconnect	= ddr_freq_input_disconnect,
	.name		= "ddr_freq",
	.id_table	= ddr_freq_ids,
};

enum ddr_bandwidth_id{
    ddrbw_wr_num=0,
    ddrbw_rd_num,
    ddrbw_act_num,
    ddrbw_time_num,  
    ddrbw_eff,
    ddrbw_id_end
};



#define DDR_BOOST_HOLD_MS	300
#define HIGH_LOAD_HOLD_MS	300
#define HIGH_LOAD_DELAY_MS	0
#define LOW_LOAD_DELAY_MS	200
#define DDR_BOOST_HOLD		(DDR_BOOST_HOLD_MS/ddrbw_work_delay_ms)
#define HIGH_LOAD_HOLD		(DDR_BOOST_HOLD_MS/ddrbw_work_delay_ms)
#define HIGH_LOAD_DELAY		(HIGH_LOAD_DELAY_MS/ddrbw_work_delay_ms)
#define LOW_LOAD_DELAY		(LOW_LOAD_DELAY_MS/ddrbw_work_delay_ms)
#define DDR_RATE_NORMAL 	240000000
#define DDR_RATE_BOOST		324000000
#define DDR_RATE_HIGH_LOAD	533000000
#define DDR_RATE_1080P		240000000
#define DDR_RATE_4K		300000000
#define HIGH_LOAD_NORMAL	70
#define HGIH_LOAD_VIDEO		50

static struct workqueue_struct *ddr_freq_wq;
static u32 high_load = HIGH_LOAD_NORMAL;
static u32 ddrbw_work_delay_ms = 20; 
static u32 ddr_rate_normal = DDR_RATE_NORMAL;
static u32 ddr_rate_boost = DDR_RATE_BOOST;
static u32 ddr_rate_high_load = DDR_RATE_HIGH_LOAD;


//#define  ddr_monitor_start() grf_writel(0xc000c000,RK3288_GRF_SOC_CON4)
#define  ddr_monitor_start() grf_writel((((readl_relaxed(RK_PMU_VIRT + 0x9c)>>13)&7)==3)?0xc000c000:0xe000e000,RK3288_GRF_SOC_CON4)
#define  ddr_monitor_stop() grf_writel(0xc0000000,RK3288_GRF_SOC_CON4)

#define grf_readl(offset)	readl_relaxed(RK_GRF_VIRT + offset)
#define grf_writel(v, offset)	do { writel_relaxed(v, RK_GRF_VIRT + offset); dsb(); } while (0)


void ddr_bandwidth_get(u32 *ch0_eff, u32 *ch1_eff)
{
	u32 ddr_bw_val[2][ddrbw_id_end];
	u64 temp64;
	int i, j;

	for(j = 0; j < 2; j++) {
		for(i = 0; i < ddrbw_eff; i++ ){
	        	ddr_bw_val[j][i] = grf_readl(RK3288_GRF_SOC_STATUS11+i*4+j*16);
		}
	}

	temp64 = ((u64)ddr_bw_val[0][0]+ddr_bw_val[0][1])*4*100;
	do_div(temp64, ddr_bw_val[0][ddrbw_time_num]);
	ddr_bw_val[0][ddrbw_eff] = temp64;
	*ch0_eff = temp64;
	
	temp64 = ((u64)ddr_bw_val[1][0]+ddr_bw_val[1][1])*4*100;
	do_div(temp64, ddr_bw_val[1][ddrbw_time_num]);   
	ddr_bw_val[1][ddrbw_eff] = temp64;
	*ch1_eff = temp64;
}


static void ddrbw_work_fn(struct work_struct *work)
{
	unsigned long rate;
	u32 ch0_eff, ch1_eff;
	static u32 ddr_boost_hold=0, high_load_hold=0;
	static u32 high_load_delay = 0, low_load_delay = 0;
	
	ddr_monitor_stop();
        ddr_bandwidth_get(&ch0_eff, &ch1_eff);

	if (ddr_boost) {
		ddr_boost = 0;
		//dvfs_clk_set_rate(ddr.clk_dvfs_node, DDR_BOOST_RATE);
		if (!high_load_hold && !low_load_delay) {
			rate = ddr_rate_boost;
			ddrfreq_mode(false, &rate, "boost");
			ddr_boost_hold = DDR_BOOST_HOLD;
		}
	} else if(!ddr_boost_hold && ((ch0_eff>high_load)||(ch1_eff>high_load))){
		low_load_delay = LOW_LOAD_DELAY;
		if (!high_load_delay) {
			//dvfs_clk_set_rate(ddr.clk_dvfs_node, HIGH_LOAD_RATE);
			rate = ddr_rate_high_load;
			ddrfreq_mode(false, &rate, "high load");
			high_load_hold = HIGH_LOAD_HOLD;
		} else {
			high_load_delay--;
		}
	} else {
		if (ddr_boost_hold) {
			ddr_boost_hold--;
		} else if (high_load_hold) {
			high_load_hold--;
		} else {
			high_load_delay = HIGH_LOAD_DELAY;
	       		//dvfs_clk_set_rate(ddr.clk_dvfs_node, DDR_NORMAL_RATE);
	       		if (!low_load_delay) {
	       			rate = ddr_rate_normal;
	       			ddrfreq_mode(false, &rate, "normal");
	       		} else {
	       			low_load_delay--;
	       		}
		}
  	  }

	ddr_monitor_start();

	queue_delayed_work_on(0, ddr_freq_wq, to_delayed_work(work), HZ*ddrbw_work_delay_ms/1000);
}

static DECLARE_DELAYED_WORK(ddrbw_work, ddrbw_work_fn);

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

	if (ddr.auto_freq)
		cancel_delayed_work_sync(&ddrbw_work);
	
	if (ddr.reboot_rate && (s & SYS_STATUS_REBOOT)) {
		ddrfreq_mode(false, &ddr.reboot_rate, "shutdown/reboot");
		rockchip_cpufreq_reboot_limit_freq();
		reboot_config_done = 1;
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
		if (ddr.auto_freq)
			queue_delayed_work_on(0, ddr_freq_wq, &ddrbw_work, 0);
		else
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
	} while (!kthread_should_stop() && !reboot_config_done);

	return 0;
}

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
		high_load = HIGH_LOAD_NORMAL;
		ddr_rate_normal = DDR_RATE_NORMAL;
		ddr_rate_high_load = DDR_RATE_HIGH_LOAD;
		if (!ddr.auto_freq)
			ddrfreq_clear_sys_status(SYS_STATUS_VIDEO);
		break;
	case '1':
		high_load = HGIH_LOAD_VIDEO;
		ddr_rate_normal = DDR_RATE_1080P;
		ddr_rate_high_load = DDR_RATE_4K;
		if( (v_width == 0) && (v_height == 0)){
			if (!ddr.auto_freq)
				ddrfreq_set_sys_status(SYS_STATUS_VIDEO_1080P);
		}
		else if(v_sync==1){
			//if(ddr.video_low_rate && ((v_width*v_height) <= VIDEO_LOW_RESOLUTION) )
			//	ddrfreq_set_sys_status(SYS_STATUS_VIDEO_720P);
			//else
			if (!ddr.auto_freq)
				ddrfreq_set_sys_status(SYS_STATUS_VIDEO_1080P);
		}
		else{
			if (!ddr.auto_freq)
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
	while (!reboot_config_done && --timeout) {
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

	prop = of_find_property(clk_ddr_dev_node, "auto_freq", NULL);
	if (prop && prop->value)
		ddr.auto_freq = be32_to_cpup(prop->value);

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
#if 0//defined(CONFIG_RK_PM_TESTS)
static void ddrfreq_tst_init(void);
#endif

static int ddr_freq_suspend_notifier_call(struct notifier_block *self,
				unsigned long action, void *data)
{
	struct fb_event *event = data;
	int blank_mode = *((int *)event->data);

	if (action == FB_EARLY_EVENT_BLANK) {
		switch (blank_mode) {
		case FB_BLANK_UNBLANK:
			ddrfreq_clear_sys_status(SYS_STATUS_SUSPEND);
			break;
		default:
			break;
		}
	}
	else if (action == FB_EVENT_BLANK) {
		switch (blank_mode) {
		case FB_BLANK_POWERDOWN:
			ddrfreq_set_sys_status(SYS_STATUS_SUSPEND);
			break;
		default:
			break;
		}
	}

	return NOTIFY_OK;
}

static struct notifier_block ddr_freq_suspend_notifier = {
		.notifier_call = ddr_freq_suspend_notifier_call,
};




static int ddrfreq_init(void)
{
	struct sched_param param = { .sched_priority = MAX_RT_PRIO - 1 };
	int ret;
#if 0//defined(CONFIG_RK_PM_TESTS)
        ddrfreq_tst_init();
#endif
	clk_cpu_dvfs_node = clk_get_dvfs_node("clk_core");
	if (!clk_cpu_dvfs_node){
		return -EINVAL;
	}
	
	memset(&ddr, 0x00, sizeof(ddr));
	ddr.clk_dvfs_node = clk_get_dvfs_node("clk_ddr");
	if (!ddr.clk_dvfs_node){
		return -EINVAL;
	}
	
	clk_enable_dvfs(ddr.clk_dvfs_node);

	ddr_freq_wq = alloc_workqueue("ddr_freq", WQ_NON_REENTRANT | WQ_MEM_RECLAIM | WQ_HIGHPRI | WQ_FREEZABLE, 1);	
	
	init_waitqueue_head(&ddr.wait);
	ddr.mode = "normal";

	ddr.normal_rate = dvfs_clk_get_rate(ddr.clk_dvfs_node);
	ddr.suspend_rate = ddr.normal_rate;
	ddr.reboot_rate = ddr.normal_rate;

	of_init_ddr_freq_table();

	ret = input_register_handler(&ddr_freq_input_handler);
	if (ret)
		ddr.auto_freq = false;

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

	fb_register_client(&ddr_freq_suspend_notifier);
	register_reboot_notifier(&ddrfreq_reboot_notifier);

	pr_info("verion 1.0 20140228\n");
	dprintk(DEBUG_DDR, "normal %luMHz video %luMHz dualview %luMHz idle %luMHz suspend %luMHz reboot %luMHz\n",
		ddr.normal_rate / MHZ, ddr.video_rate / MHZ, ddr.dualview_rate / MHZ, ddr.idle_rate / MHZ, ddr.suspend_rate / MHZ, ddr.reboot_rate / MHZ);

	return 0;

err1:
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

/****************************ddr bandwith tst************************************/
#if 0//defined(CONFIG_RK_PM_TESTS)

#define USE_NORMAL_TIME

#ifdef USE_NORMAL_TIME
static struct timer_list ddrbw_timer;
#else
static struct hrtimer ddrbw_hrtimer;
#endif
enum ddr_bandwidth_id{
    ddrbw_wr_num=0,
    ddrbw_rd_num,
    ddrbw_act_num,
    ddrbw_time_num,  
    ddrbw_eff,
    ddrbw_id_end
};
#define grf_readl(offset)	readl_relaxed(RK_GRF_VIRT + offset)
#define grf_writel(v, offset)	do { writel_relaxed(v, RK_GRF_VIRT + offset); dsb(); } while (0)

static u32 ddr_bw_show_st=0;

#define  ddr_monitor_start() grf_writel(0xc000c000,RK3288_GRF_SOC_CON4)
#define  ddr_monitor_end() grf_writel(0xc0000000,RK3288_GRF_SOC_CON4)

static ssize_t ddrbw_dyn_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	char *s = buf;
	return (s - buf);
}

static ssize_t ddrbw_dyn_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t n)
{
	//const char *pbuf;

	if((strncmp(buf, "start", strlen("start")) == 0)) {
            ddr_bw_show_st=1;
            ddr_monitor_start();
            
            #ifdef USE_NORMAL_TIME
            mod_timer(&ddrbw_timer, jiffies + msecs_to_jiffies(500));
            #else
            hrtimer_start(&ddrbw_hrtimer, ktime_set(0, 5 * 1000 * 1000*1000), HRTIMER_MODE_REL);
            #endif

	} else if((strncmp(buf, "stop", strlen("stop")) == 0)) {
	    ddr_bw_show_st=0;
            ddr_monitor_end();
	}

	return n;
}

static void ddr_bandwidth_get(void)
{
    u32 ddr_bw_val[2][ddrbw_id_end];
    int i,j;
    u64 temp64;
    
    
    for(j=0;j<2;j++)
    for(i=0;i<ddrbw_eff;i++)
    {
        ddr_bw_val[j][i]=grf_readl(RK3288_GRF_SOC_STATUS11+i*4+j*16);
    }
    ddr_monitor_end();//stop
    ddr_monitor_start();
    
    temp64=((u64)ddr_bw_val[0][0]+ddr_bw_val[0][1])*8*100;
    
   // printk("ch0 %llu\n",temp64);

    do_div(temp64,ddr_bw_val[0][ddrbw_time_num]);
    ddr_bw_val[0][ddrbw_eff]= temp64;
    temp64=((u64)ddr_bw_val[1][0]+ddr_bw_val[1][1])*8*100;
    
    //printk("ch1 %llu\n",temp64);

    do_div(temp64,ddr_bw_val[1][ddrbw_time_num]);   
    ddr_bw_val[1][ddrbw_eff]=  temp64;

    printk("ddrch0,wr,rd,act,time,percent(%x,%x,%x,%x,%d)\n", 
                               ddr_bw_val[0][0],ddr_bw_val[0][1],ddr_bw_val[0][2],ddr_bw_val[0][3],ddr_bw_val[0][4]);
    printk("ddrch1,wr,rd,act,time,percent(%x,%x,%x,%x,%d)\n", 
                               ddr_bw_val[1][0],ddr_bw_val[1][1],ddr_bw_val[1][2],ddr_bw_val[1][3],ddr_bw_val[1][4]);
    
}

#ifdef USE_NORMAL_TIME
static void ddrbw_timer_fn(unsigned long data)
{
	//int i;
        ddr_bandwidth_get();
        if(ddr_bw_show_st)
        {
            mod_timer(&ddrbw_timer, jiffies + msecs_to_jiffies(500));
         }
}
#else
struct hrtimer ddrbw_hrtimer;
static enum hrtimer_restart ddrbw_hrtimer_timer_func(struct hrtimer *timer)
{
	int i;
        ddr_bandwidth_get();
        if(ddr_bw_show_st)
        hrtimer_start(timer, ktime_set(0, 1 * 1000 * 1000), HRTIMER_MODE_REL);

}
#endif

struct ddrfreq_attribute {
	struct attribute	attr;
	ssize_t (*show)(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf);
	ssize_t (*store)(struct kobject *kobj, struct kobj_attribute *attr,
			const char *buf, size_t n);
};

static struct ddrfreq_attribute ddrfreq_attrs[] = {
	/*     node_name	permision		show_func	store_func */    
	__ATTR(ddrbw,	S_IRUSR | S_IRGRP | S_IWUSR,ddrbw_dyn_show,	ddrbw_dyn_store),
};
int rk_pm_tests_kobj_atrradd(const struct attribute *attr);

static void ddrfreq_tst_init(void)
{
        int i,ret;
#ifdef USE_NORMAL_TIME
            init_timer(&ddrbw_timer);
            //ddrbw_timer.expires = jiffies+msecs_to_jiffies(1);
            ddrbw_timer.function = ddrbw_timer_fn;
            //mod_timer(&ddrbw_timer,jiffies+msecs_to_jiffies(1));
#else
            hrtimer_init(&ddrbw_hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
            ddrbw_hrtimer.function = ddrbw_hrtimer_timer_func;
            //hrtimer_start(&ddrbw_hrtimer,ktime_set(0, 5*1000*1000),HRTIMER_MODE_REL);
#endif
            printk("*****%s*****\n",__FUNCTION__);

            ret = rk_pm_tests_kobj_atrradd(&ddrfreq_attrs[0].attr);
            if (ret != 0) {
                printk("create ddrfreq sysfs node error\n");
                return;
            }

}
#endif
