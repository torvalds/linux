#define pr_fmt(fmt) "ddrfreq: " fmt
#define DEBUG
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
#include <dt-bindings/clock/rk_system_status.h>
#include <asm/io.h>
#include <linux/rockchip/grf.h>
#include <linux/rockchip/iomap.h>
#include <linux/clk-private.h>
#include "../../../drivers/clk/rockchip/clk-pd.h"
#include "cpu_axi.h"

#ifdef CONFIG_CPU_FREQ
extern int rockchip_cpufreq_reboot_limit_freq(void);
#else
static inline int rockchip_cpufreq_reboot_limit_freq(void) { return 0; }
#endif

static DECLARE_COMPLETION(ddrfreq_completion);
static DEFINE_MUTEX(ddrfreq_mutex);

#define VOP_REQ_BLOCK
#ifdef VOP_REQ_BLOCK
static DECLARE_COMPLETION(vop_req_completion);
#endif

static struct dvfs_node *clk_cpu_dvfs_node = NULL;
static int ddr_boost = 0;
static int print=0;
static int watch=0;
static int high_load = 70;
static int low_load = 60;
static int auto_freq_interval_ms = 20;
static long down_rate_delay_ms = 500;
static unsigned long *auto_freq_table = NULL;
static int cur_freq_index;
static int auto_freq_table_size;
static unsigned long vop_bandwidth_update_jiffies = 0, vop_bandwidth = 0;
static int vop_bandwidth_update_flag = 0;

enum {
	DEBUG_DDR = 1U << 0,
	DEBUG_VIDEO_STATE = 1U << 1,
	DEBUG_SUSPEND = 1U << 2,
	DEBUG_VERBOSE = 1U << 3,
};
static int debug_mask = DEBUG_DDR;

module_param(debug_mask, int, S_IRUGO | S_IWUSR | S_IWGRP);
#define dprintk(mask, fmt, ...) do { if (mask & debug_mask) pr_debug(fmt, ##__VA_ARGS__); } while (0)

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

#define noc_readl(offset)       readl_relaxed(RK3288_SERVICE_BUS_VIRT + offset)
#define noc_writel(v, offset)   do { writel_relaxed(v, RK3288_SERVICE_BUS_VIRT + offset); dsb(); } while (0)

#define MHZ	(1000*1000)
#define KHZ	1000

struct video_info {
	int width;
	int height;
	int ishevc;
	int videoFramerate;
	int streamBitrate;

	struct list_head node;
};

struct ddr_bw_info{
    u32 ddr_wr;
    u32 ddr_rd;
    u32 ddr_act;
    u32 ddr_time;
    u32 ddr_total;
    u32 ddr_percent;

    u32 cpum;
    u32 gpu;
    u32 peri;
    u32 video;
    u32 vio0;
    u32 vio1;
    u32 vio2;
};
static struct ddr_bw_info ddr_bw_ch0={0}, ddr_bw_ch1={0};

struct ddr {
	struct dvfs_node *clk_dvfs_node;
	struct list_head video_info_list;
	unsigned long normal_rate;
	unsigned long video_1080p_rate;
	unsigned long video_4k_rate;
	unsigned long performance_rate;
	unsigned long dualview_rate;
	unsigned long hdmi_rate;
	unsigned long idle_rate;
	unsigned long suspend_rate;
	unsigned long reboot_rate;
	unsigned long boost_rate;
	unsigned long isp_rate;
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

static unsigned long auto_freq_round(unsigned long freq)
{
	int i;

	if (!auto_freq_table)
		return -EINVAL;

	for (i = 0; auto_freq_table[i] != 0; i++) {
		if (auto_freq_table[i] >= freq) {
			return auto_freq_table[i];
		}
	}

	return auto_freq_table[i-1];
}

static unsigned long auto_freq_get_index(unsigned long freq)
{
	int i;

	if (!auto_freq_table)
		return 0;

	for (i = 0; auto_freq_table[i] != 0; i++) {
		if (auto_freq_table[i] >= freq) {
			return i;
		}
	}
	return i-1;
}

static unsigned int auto_freq_update_index(unsigned long freq)
{
	cur_freq_index = auto_freq_get_index(freq);

	return cur_freq_index;
}


static unsigned long auto_freq_get_next_step(void)
{
	if (cur_freq_index < auto_freq_table_size-1) {
			return auto_freq_table[cur_freq_index+1];
	}

	return auto_freq_table[cur_freq_index];
}

static void ddrfreq_mode(bool auto_self_refresh, unsigned long target_rate, char *name)
{
	unsigned int min_rate, max_rate;
	int freq_limit_en;

	ddr.mode = name;
	if (auto_self_refresh != ddr.auto_self_refresh) {
		ddr_set_auto_self_refresh(auto_self_refresh);
		ddr.auto_self_refresh = auto_self_refresh;
		dprintk(DEBUG_DDR, "change auto self refresh to %d when %s\n", auto_self_refresh, name);
	}

	if (target_rate != dvfs_clk_get_last_set_rate(ddr.clk_dvfs_node)) {
		freq_limit_en = dvfs_clk_get_limit(clk_cpu_dvfs_node, &min_rate, &max_rate);

		dvfs_clk_enable_limit(clk_cpu_dvfs_node, 600000000, -1);
		if (dvfs_clk_set_rate(ddr.clk_dvfs_node, target_rate) == 0) {
			target_rate = dvfs_clk_get_rate(ddr.clk_dvfs_node);
			auto_freq_update_index(target_rate);
			dprintk(DEBUG_DDR, "change freq to %lu MHz when %s\n", target_rate / MHZ, name);
		}

		if (freq_limit_en) {
			dvfs_clk_enable_limit(clk_cpu_dvfs_node, min_rate, max_rate);
		} else {
			dvfs_clk_disable_limit(clk_cpu_dvfs_node);
		}
	}
}

unsigned long req_freq_by_vop(unsigned long bandwidth)
{
	if (time_after(jiffies, vop_bandwidth_update_jiffies+down_rate_delay_ms))
		return 0;

	if (bandwidth >= 5000){
		return 800000000;
	}

	if (bandwidth >= 3500) {
		return 456000000;
	}

	if (bandwidth >= 2600) {
		return 396000000;
	}
	if (bandwidth >= 2000) {
		return 324000000;
	}

	return 0;
}

void ddr_monitor_start(void)
{
    int i;

    for(i=1;i<8;i++)
    {
        noc_writel(0x8, (0x400*i+0x8));
        noc_writel(0x1, (0x400*i+0xc));
        noc_writel(0x6, (0x400*i+0x138));
        noc_writel(0x10, (0x400*i+0x14c));
        noc_writel(0x8, (0x400*i+0x160));
        noc_writel(0x10, (0x400*i+0x174));
    }
    grf_writel((((readl_relaxed(RK_PMU_VIRT + 0x9c)>>13)&7)==3)?0xc000c000:0xe000e000,RK3288_GRF_SOC_CON4); // ddr
    for(i=1;i<8;i++)
    {
        noc_writel(0x1, (0x400*i+0x28));
    }
}

void ddr_monitor_stop(void)
{
    grf_writel(0xc0000000,RK3288_GRF_SOC_CON4);  //ddr
}

void ddr_bandwidth_get(struct ddr_bw_info *ddr_bw_ch0, struct ddr_bw_info *ddr_bw_ch1)
{
	u32 ddr_bw_val[2][ddrbw_id_end], ddr_freq;
	u64 temp64;
	int i, j;

	ddr_monitor_stop();
	for(j = 0; j < 2; j++) {
		for(i = 0; i < ddrbw_eff; i++ ){
	        	ddr_bw_val[j][i] = grf_readl(RK3288_GRF_SOC_STATUS11+i*4+j*16);
		}
	}
	if (!ddr_bw_val[0][ddrbw_time_num])
		goto end;

	if (ddr_bw_ch0) {
		ddr_freq = readl_relaxed(RK_DDR_VIRT + 0xc0);

		temp64 = ((u64)ddr_bw_val[0][0]+ddr_bw_val[0][1])*4*100;
		do_div(temp64, ddr_bw_val[0][ddrbw_time_num]);
		ddr_bw_val[0][ddrbw_eff] = temp64;

		ddr_bw_ch0->ddr_percent = temp64;
		ddr_bw_ch0->ddr_time = ddr_bw_val[0][ddrbw_time_num]/(ddr_freq*1000);
		ddr_bw_ch0->ddr_wr = (ddr_bw_val[0][ddrbw_wr_num]*8*4)*ddr_freq/ddr_bw_val[0][ddrbw_time_num];
		ddr_bw_ch0->ddr_rd = (ddr_bw_val[0][ddrbw_rd_num]*8*4)*ddr_freq/ddr_bw_val[0][ddrbw_time_num];
		ddr_bw_ch0->ddr_act = ddr_bw_val[0][ddrbw_act_num];
		ddr_bw_ch0->ddr_total = ddr_freq*2*4;

		ddr_bw_ch0->cpum = (noc_readl(0x400+0x178)<<16)
		          + (noc_readl(0x400+0x164));
		ddr_bw_ch0->gpu = (noc_readl(0x800+0x178)<<16)
		          + (noc_readl(0x800+0x164));
		ddr_bw_ch0->peri = (noc_readl(0xc00+0x178)<<16)
		          + (noc_readl(0xc00+0x164));
		ddr_bw_ch0->video = (noc_readl(0x1000+0x178)<<16)
		          + (noc_readl(0x1000+0x164));
		ddr_bw_ch0->vio0 = (noc_readl(0x1400+0x178)<<16)
		          + (noc_readl(0x1400+0x164));
		ddr_bw_ch0->vio1 = (noc_readl(0x1800+0x178)<<16)
		          + (noc_readl(0x1800+0x164));
		ddr_bw_ch0->vio2 = (noc_readl(0x1c00+0x178)<<16)
		          + (noc_readl(0x1c00+0x164));

		ddr_bw_ch0->cpum = ddr_bw_ch0->cpum*ddr_freq/ddr_bw_val[0][ddrbw_time_num];
		ddr_bw_ch0->gpu = ddr_bw_ch0->gpu*ddr_freq/ddr_bw_val[0][ddrbw_time_num];
		ddr_bw_ch0->peri = ddr_bw_ch0->peri*ddr_freq/ddr_bw_val[0][ddrbw_time_num];
		ddr_bw_ch0->video = ddr_bw_ch0->video*ddr_freq/ddr_bw_val[0][ddrbw_time_num];
		ddr_bw_ch0->vio0 = ddr_bw_ch0->vio0*ddr_freq/ddr_bw_val[0][ddrbw_time_num];
		ddr_bw_ch0->vio1 = ddr_bw_ch0->vio1*ddr_freq/ddr_bw_val[0][ddrbw_time_num];
		ddr_bw_ch0->vio2 = ddr_bw_ch0->vio2*ddr_freq/ddr_bw_val[0][ddrbw_time_num];
	}
end:
	ddr_monitor_start();
}

static void ddr_auto_freq(void)
{
	unsigned long freq, new_freq=0, vop_req_freq=0, total_bw_req_freq=0;
	u32 ddr_percent, target_load;
	static u32 local_jiffies=0, max_ddr_percent=0;

	if (!local_jiffies)
		local_jiffies = jiffies;
	freq = dvfs_clk_get_rate(ddr.clk_dvfs_node);

        ddr_bandwidth_get(&ddr_bw_ch0, &ddr_bw_ch1);
	ddr_percent = ddr_bw_ch0.ddr_percent;

	if ((watch)||(print)) {
		if((watch == 2)&& (ddr_bw_ch0.ddr_percent < max_ddr_percent)) {
		    return;
		} else if(watch == 2) {
		    max_ddr_percent = ddr_bw_ch0.ddr_percent;
		}
		printk("Unit:MB/s total  use%%    rd    wr  cpum   gpu  peri video  vio0  vio1  vio2\n");
		printk("%3u(ms): %5u %5u %5u %5u %5u %5u %5u %5u %5u %5u %5u\n",
			ddr_bw_ch0.ddr_time,
			ddr_bw_ch0.ddr_total,
			ddr_bw_ch0.ddr_percent,
			ddr_bw_ch0.ddr_rd,
			ddr_bw_ch0.ddr_wr,
			ddr_bw_ch0.cpum,
			ddr_bw_ch0.gpu,
			ddr_bw_ch0.peri,
			ddr_bw_ch0.video,
			ddr_bw_ch0.vio0,
			ddr_bw_ch0.vio1,
			ddr_bw_ch0.vio2);

		if (watch)
			return;
	}

	if (ddr_boost) {
		ddr_boost = 0;
		new_freq = max(ddr.boost_rate, new_freq);
	}

	if(ddr_percent > high_load){
		total_bw_req_freq = auto_freq_get_next_step();
	} else if (ddr_percent < low_load){
		target_load = (low_load+high_load)/2;
		total_bw_req_freq = ddr_percent*(freq/target_load);
	}
	new_freq = max(total_bw_req_freq, new_freq);

	vop_req_freq = req_freq_by_vop(vop_bandwidth);
	new_freq = max(vop_req_freq, new_freq);

	new_freq = auto_freq_round(new_freq);

	if (new_freq < freq) {
		if (time_after(jiffies, local_jiffies+down_rate_delay_ms/10)) {
			local_jiffies = jiffies;
			ddrfreq_mode(false, new_freq, "auto down rate");
		}
	} else if(new_freq > freq){
		local_jiffies = jiffies;
		ddrfreq_mode(false, new_freq, "auto up rate");
	}
}

static noinline long ddrfreq_work(unsigned long sys_status)
{
	long timeout = MAX_SCHEDULE_TIMEOUT;
	unsigned long target_rate = 0;
	unsigned long s = sys_status;
	bool auto_self_refresh = false;
	char *mode = NULL;

	dprintk(DEBUG_VERBOSE, "sys_status %02lx\n", sys_status);

	if (ddr.reboot_rate && (s & SYS_STATUS_REBOOT)) {
		ddrfreq_mode(false, ddr.reboot_rate, "shutdown/reboot");
		rockchip_cpufreq_reboot_limit_freq();

		return timeout;
	}

	if (ddr.suspend_rate && (s & SYS_STATUS_SUSPEND)) {
		if (ddr.suspend_rate > target_rate) {
			target_rate = ddr.suspend_rate;
			auto_self_refresh = true;
			mode = "suspend";
		}
	}

	if (ddr.performance_rate && (s & SYS_STATUS_PERFORMANCE)) {
		if (ddr.performance_rate > target_rate) {
			target_rate = ddr.performance_rate;
			auto_self_refresh = false;
			mode = "performance";
		}
	}

	 if (ddr.dualview_rate &&
		(s & SYS_STATUS_LCDC0) && (s & SYS_STATUS_LCDC1)) {
		 if (ddr.dualview_rate > target_rate) {
			 target_rate = ddr.dualview_rate;
			 auto_self_refresh = false;
			 mode = "dual-view";
		 }
	 }

	 if (ddr.hdmi_rate &&
		(s & SYS_STATUS_HDMI)) {
		 if (ddr.hdmi_rate > target_rate) {
			 target_rate = ddr.hdmi_rate;
			 auto_self_refresh = false;
			 mode = "hdmi";
		 }
	 }

	if (ddr.video_4k_rate && (s & SYS_STATUS_VIDEO_4K)) {
		if (ddr.video_4k_rate > target_rate) {
			target_rate = ddr.video_4k_rate;
			auto_self_refresh = false;
			mode = "video_4k";
		}
	}

	if (ddr.video_1080p_rate && (s & SYS_STATUS_VIDEO_1080P)) {
		if (ddr.video_1080p_rate > target_rate) {
			target_rate = ddr.video_1080p_rate;
			auto_self_refresh = false;
			mode = "video_1080p";
		}
	}

	if (ddr.isp_rate && (s & SYS_STATUS_ISP)) {
		if (ddr.isp_rate > target_rate) {
			target_rate = ddr.isp_rate;
			auto_self_refresh = false;
			mode = "isp";
		}
	}

	if (target_rate > 0) {
		ddrfreq_mode(auto_self_refresh, target_rate, mode);
	} else {
		if (ddr.auto_freq) {
			ddr_auto_freq();
			timeout = auto_freq_interval_ms/10;
		}
		else {
			ddrfreq_mode(false, ddr.normal_rate, "normal");
		}
	}

	return timeout;
#if 0

	if (ddr.reboot_rate && (s & SYS_STATUS_REBOOT)) {
		ddrfreq_mode(false, &ddr.reboot_rate, "shutdown/reboot");
		rockchip_cpufreq_reboot_limit_freq();
		reboot_config_done = 1;
	} else if (ddr.suspend_rate && (s & SYS_STATUS_SUSPEND)) {
		ddrfreq_mode(true, &ddr.suspend_rate, "suspend");
	} else if (ddr.dualview_rate && 
		(s & SYS_STATUS_LCDC0) && (s & SYS_STATUS_LCDC1)) {
		ddrfreq_mode(false, &ddr.dualview_rate, "dual-view");
	} else if (ddr.video_1080p_rate && (s & SYS_STATUS_VIDEO_1080P)) {
		ddrfreq_mode(false, &ddr.video_1080p_rate, "video_1080p");
	} else if (ddr.video_4k_rate && (s & SYS_STATUS_VIDEO_4K)) {
		ddrfreq_mode(false, &ddr.video_4k_rate, "video_4k");
	} else if (ddr.performance_rate && (s & SYS_STATUS_PERFORMANCE)) {
		ddrfreq_mode(false, &ddr.performance_rate, "performance");
	}  else if (ddr.isp_rate && (s & SYS_STATUS_ISP)) {
		ddrfreq_mode(false, &ddr.isp_rate, "isp");
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
		if (ddr.auto_freq) {
			ddr_auto_freq();
			timeout = auto_freq_interval_ms;
		}
		else {
			ddrfreq_mode(false, &ddr.normal_rate, "normal");
		}
	}



	return timeout;
#endif
}

static int ddrfreq_task(void *data)
{
	long timeout;
	unsigned long status=ddr.sys_status, old_status=ddr.sys_status;

	set_freezable();

	do {
		status = ddr.sys_status;
		if (vop_bandwidth_update_flag) {
			vop_bandwidth_update_flag = 0;
#ifdef VOP_REQ_BLOCK
			complete(&vop_req_completion);
#endif
		}

		timeout = ddrfreq_work(status);
		if (old_status != status) {
			complete(&ddrfreq_completion);
		}
		wait_event_freezable_timeout(ddr.wait, vop_bandwidth_update_flag || (status != ddr.sys_status) || kthread_should_stop(), timeout);
		old_status = status;
	} while (!kthread_should_stop());

	return 0;
}

void add_video_info(struct video_info *video_info)
{
	if (video_info)
		list_add(&video_info->node, &ddr.video_info_list);
}

void del_video_info(struct video_info *video_info)
{
	if (video_info) {
		list_del(&video_info->node);
		kfree(video_info);
	}
}

void clear_video_info(void)
{
	struct video_info *video_info, *next;

	list_for_each_entry_safe(video_info, next, &ddr.video_info_list, node) {
		del_video_info(video_info);
	}
}

struct video_info *find_video_info(struct video_info *match_video_info)
{
	struct video_info *video_info;

	if (!match_video_info)
		return NULL;

	list_for_each_entry(video_info, &ddr.video_info_list, node) {
		if ((video_info->width == match_video_info->width)
			&& (video_info->height == match_video_info->height)
			&& (video_info->ishevc== match_video_info->ishevc)
			&& (video_info->videoFramerate == match_video_info->videoFramerate)
			&& (video_info->streamBitrate== match_video_info->streamBitrate)) {

			return video_info;
		}

	}

	return NULL;
}

void update_video_info(void)
{
	struct video_info *video_info, *max_res_video;
	int max_res=0, res=0;

	if (list_empty(&ddr.video_info_list)) {
		rockchip_clear_system_status(SYS_STATUS_VIDEO_1080P|SYS_STATUS_VIDEO_4K);
		return;
	}

	list_for_each_entry(video_info, &ddr.video_info_list, node) {
		res = video_info->width * video_info->height;
		if (res > max_res) {
			max_res = res;
			max_res_video = video_info;
		}
	}

	if (max_res <= 1920*1080)
		rockchip_set_system_status(SYS_STATUS_VIDEO_1080P);
	else
		rockchip_set_system_status(SYS_STATUS_VIDEO_4K);

	return;
}

/***format: width=val,height=val,ishevc=val,videoFramerate=val,streamBitrate=val***/
static long get_video_param(char **str)
{
	char *p;

	strsep(str,"=");
	p=strsep(str,",");
	if (p)
		return simple_strtol(p,NULL,10);

	return 0;
}

static ssize_t video_state_write(struct file *file, const char __user *buffer,
				 size_t count, loff_t *ppos)
{
	struct video_info *video_info = NULL;
	char state, *cookie_pot, *buf = vzalloc(count);
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

	dprintk(DEBUG_VIDEO_STATE, "%s: %s,len %d\n", __func__, cookie_pot,count);

	state=cookie_pot[0];
	if( (count>=3) && (cookie_pot[2]=='w') )
	{
		video_info = kzalloc(sizeof(struct video_info), GFP_KERNEL);
		if (!video_info){
			vfree(buf);
			return -ENOMEM;
		}
		INIT_LIST_HEAD(&video_info->node);

		strsep(&cookie_pot,",");

		video_info->width = get_video_param(&cookie_pot);
		video_info->height = get_video_param(&cookie_pot);
		video_info->ishevc = get_video_param(&cookie_pot);
		video_info->videoFramerate = get_video_param(&cookie_pot);
		video_info->streamBitrate = get_video_param(&cookie_pot);

		dprintk(DEBUG_VIDEO_STATE, "%s: video_state=%c,width=%d,height=%d,ishevc=%d,videoFramerate=%d,streamBitrate=%d\n",
			__func__, state,video_info->width,video_info->height,
			video_info->ishevc, video_info->videoFramerate,
			video_info->streamBitrate);

	}
	switch (state) {
	case '0':
		del_video_info(find_video_info(video_info));
		kfree(video_info);
		update_video_info();
		break;
	case '1':
		add_video_info(video_info);
		update_video_info();
		break;
	case 'p'://performance
		rockchip_set_system_status(SYS_STATUS_PERFORMANCE);
		break;
	case 'n'://normal
		rockchip_clear_system_status(SYS_STATUS_PERFORMANCE);
		break;
	default:
		vfree(buf);
		return -EINVAL;

	}

	vfree(buf);
	return count;
}

static int video_state_release(struct inode *inode, struct file *file)
{
	dprintk(DEBUG_VIDEO_STATE, "video_state release\n");
	clear_video_info();
	update_video_info();
	return 0;
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

static long ddr_freq_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	unsigned long bandwidth = *(int*)arg;
	unsigned long vop_req_freq;
	int ret = -1;

	vop_bandwidth = bandwidth;
	vop_bandwidth_update_jiffies = jiffies;
	vop_req_freq = req_freq_by_vop(bandwidth);
	if (dvfs_clk_get_rate(ddr.clk_dvfs_node) >= vop_req_freq) {
		ret = 0;
	}

	vop_bandwidth_update_flag = 1;
	wake_up(&ddr.wait);
#ifdef VOP_REQ_BLOCK
	wait_for_completion(&vop_req_completion);
#endif

	return ret;
}


static const struct file_operations ddr_freq_fops = {
	.owner	= THIS_MODULE,
	.unlocked_ioctl = ddr_freq_ioctl,
};

static struct miscdevice ddr_freq_dev = {
	.fops	= &ddr_freq_fops,
	.name	= "ddr_freq",
	.mode	= S_IRUGO | S_IWUSR | S_IWUGO,
	.minor	= MISC_DYNAMIC_MINOR,
};

#ifdef CONFIG_INPUT
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
#endif
#if 0
static int ddrfreq_clk_event(int status, unsigned long event)
{
	switch (event) {
	case RK_CLK_PD_PREPARE:
		ddrfreq_set_sys_status(status);
		break;
	case RK_CLK_PD_UNPREPARE:
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
	rk_clk_pd_notifier_register(clk, &ddrfreq_clk_##name##_notifier); \
	clk_put(clk); \
} while (0)

#define UNREGISTER_CLK_NOTIFIER(name) \
do { \
	struct clk *clk = clk_get(NULL, #name); \
	rk_clk_pd_notifier_unregister(clk, &ddrfreq_clk_##name##_notifier); \
	clk_put(clk); \
} while (0)

CLK_NOTIFIER(pd_isp, ISP)
CLK_NOTIFIER(pd_vop0, LCDC0)
CLK_NOTIFIER(pd_vop1, LCDC1)
#endif

static int ddrfreq_reboot_notifier_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	rockchip_set_system_status(SYS_STATUS_REBOOT);

	return NOTIFY_OK;
}

static struct notifier_block ddrfreq_reboot_notifier = {
	.notifier_call = ddrfreq_reboot_notifier_event,
};

static int ddr_freq_suspend_notifier_call(struct notifier_block *self,
				unsigned long action, void *data)
{
	struct fb_event *event = data;
	int blank_mode = *((int *)event->data);

	if (action == FB_EARLY_EVENT_BLANK) {
		switch (blank_mode) {
		case FB_BLANK_UNBLANK:
			rockchip_clear_system_status(SYS_STATUS_SUSPEND);
			break;
		default:
			break;
		}
	}
	else if (action == FB_EVENT_BLANK) {
		switch (blank_mode) {
		case FB_BLANK_POWERDOWN:
			rockchip_set_system_status(SYS_STATUS_SUSPEND);
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

static int ddrfreq_system_status_notifier_call(struct notifier_block *nb,
				unsigned long val, void *data)
{
	mutex_lock(&ddrfreq_mutex);
	ddr.sys_status = val;
	wake_up(&ddr.wait);
	wait_for_completion(&ddrfreq_completion);
	mutex_unlock(&ddrfreq_mutex);

	return NOTIFY_OK;
}

static struct notifier_block ddrfreq_system_status_notifier = {
		.notifier_call = ddrfreq_system_status_notifier_call,
};


int of_init_ddr_freq_table(void)
{
	struct device_node *clk_ddr_dev_node;
	const struct property *prop;
	const __be32 *val;
	int nr, i=0;
	
	clk_ddr_dev_node = of_find_node_by_name(NULL, "clk_ddr");
	if (IS_ERR_OR_NULL(clk_ddr_dev_node)) {
		pr_err("%s: get clk ddr dev node err\n", __func__);
		return PTR_ERR(clk_ddr_dev_node);
	}

	prop = of_find_property(clk_ddr_dev_node, "auto-freq", NULL);
	if (prop && prop->value)
		ddr.auto_freq = be32_to_cpup(prop->value);

	prop = of_find_property(clk_ddr_dev_node, "auto-freq-table", NULL);
	if (prop && prop->value) {
		nr = prop->length / sizeof(u32);
		auto_freq_table = kzalloc((sizeof(u32) *(nr+1)), GFP_KERNEL);
		val = prop->value;
		while (nr) {
			auto_freq_table[i++] =
				dvfs_clk_round_rate(ddr.clk_dvfs_node, 1000 * be32_to_cpup(val++));
			nr--;
		}
		cur_freq_index = 0;
		auto_freq_table_size = i;
	}

	prop = of_find_property(clk_ddr_dev_node, "freq-table", NULL);
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
		unsigned long rate =
			dvfs_clk_round_rate(ddr.clk_dvfs_node, be32_to_cpup(val++) * 1000);

		if (status & SYS_STATUS_NORMAL)
			ddr.normal_rate = rate;
		if (status & SYS_STATUS_SUSPEND)
			ddr.suspend_rate = rate;
		if (status & SYS_STATUS_VIDEO_1080P)
			ddr.video_1080p_rate = rate;
		if (status & SYS_STATUS_VIDEO_4K)
			ddr.video_4k_rate = rate;
		if (status & SYS_STATUS_PERFORMANCE)
			ddr.performance_rate= rate;
		if ((status & SYS_STATUS_LCDC0)&&(status & SYS_STATUS_LCDC1))
			ddr.dualview_rate = rate;
		if (status & SYS_STATUS_HDMI)
			ddr.hdmi_rate = rate;
		if (status & SYS_STATUS_IDLE)
			ddr.idle_rate= rate;
		if (status & SYS_STATUS_REBOOT)
			ddr.reboot_rate= rate;
		if (status & SYS_STATUS_BOOST)
			ddr.boost_rate= rate;
		if (status & SYS_STATUS_ISP)
			ddr.isp_rate= rate;

		nr -= 2;
	}

	return 0;
}

static int ddrfreq_scale_rate_for_dvfs(struct clk *clk, unsigned long rate)
{
	unsigned long real_rate;

	real_rate = ddr_change_freq(rate/MHZ);
	real_rate *= MHZ;
	if (!real_rate)
		return -EAGAIN;

	clk->parent->rate = clk->rate = real_rate;

	return 0;
}

#if defined(CONFIG_RK_PM_TESTS)
static void ddrfreq_tst_init(void);
#endif

static int ddrfreq_init(void)
{
	int ret, i;
	struct sched_param param = { .sched_priority = MAX_RT_PRIO - 1 };

#if defined(CONFIG_RK_PM_TESTS)
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

	dvfs_clk_register_set_rate_callback(ddr.clk_dvfs_node, ddrfreq_scale_rate_for_dvfs);
	
	init_waitqueue_head(&ddr.wait);
	INIT_LIST_HEAD(&ddr.video_info_list);
	ddr.mode = "normal";
	ddr.normal_rate = dvfs_clk_get_rate(ddr.clk_dvfs_node);
	ddr.sys_status = rockchip_get_system_status();

	of_init_ddr_freq_table();

	if (!ddr.reboot_rate)
		ddr.reboot_rate = ddr.normal_rate;

#ifdef CONFIG_INPUT
	ret = input_register_handler(&ddr_freq_input_handler);
	if (ret)
		ddr.auto_freq = false;
#endif

	//REGISTER_CLK_NOTIFIER(pd_isp);
	//REGISTER_CLK_NOTIFIER(pd_vop0);
	//REGISTER_CLK_NOTIFIER(pd_vop1);

	ret = misc_register(&video_state_dev);
	ret = misc_register(&ddr_freq_dev);
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

	rockchip_register_system_status_notifier(&ddrfreq_system_status_notifier);
	fb_register_client(&ddr_freq_suspend_notifier);
	register_reboot_notifier(&ddrfreq_reboot_notifier);

	pr_info("verion 1.2 20140526\n");
	pr_info("normal %luMHz video_1080p %luMHz video_4k %luMHz dualview %luMHz idle %luMHz suspend %luMHz reboot %luMHz\n",
		ddr.normal_rate / MHZ,
		ddr.video_1080p_rate / MHZ,
		ddr.video_4k_rate / MHZ,
		ddr.dualview_rate / MHZ,
		ddr.idle_rate / MHZ,
		ddr.suspend_rate / MHZ,
		ddr.reboot_rate / MHZ);

	pr_info("auto-freq=%d\n", ddr.auto_freq);
	if (auto_freq_table) {
		for (i = 0; i < auto_freq_table_size; i++) {
			pr_info("auto-freq-table[%d] %luMHz\n", i, auto_freq_table[i] / MHZ);
		}
	} else {
		pr_info("auto-freq-table epmty!\n");
	}
	return 0;

err1:
	misc_deregister(&video_state_dev);
err:
	return ret;
}
late_initcall(ddrfreq_init);
/****************************ddr bandwith tst************************************/
#if defined(CONFIG_RK_PM_TESTS)
static ssize_t ddrbw_dyn_show(struct kobject *kobj, struct kobj_attribute *attr,
		char *buf)
{
	char *str = buf;
	str += sprintf(str, "print: %d\n", print);
	str += sprintf(str, "watch: %d\n", watch);
	str += sprintf(str, "high_load: %d\n", high_load);
	str += sprintf(str, "low_load: %d\n", low_load);
	str += sprintf(str, "auto_freq_interval_ms: %d\n", auto_freq_interval_ms);
	str += sprintf(str, "down_rate_delay_ms: %ld\n", down_rate_delay_ms);
//	str += sprintf(str, "low_load_last_ms: %d\n", low_load_last_ms);
	if (str != buf)
		*(str - 1) = '\n';
	return (str - buf);
}

static ssize_t ddrbw_dyn_store(struct kobject *kobj, struct kobj_attribute *attr,
		const char *buf, size_t n)
{
	int value;
	char var_name[64];

	sscanf(buf, "%s %u", var_name, &value);

	if((strncmp(buf, "print", strlen("print")) == 0)) {
		print = value;
	} else if((strncmp(buf, "watch", strlen("watch")) == 0)) {
		watch = value;
	} else if((strncmp(buf, "high", strlen("high")) == 0)) {
		high_load = value;
	} else if((strncmp(buf, "low", strlen("low")) == 0)) {
		low_load = value;
	} else if((strncmp(buf, "interval", strlen("interval")) == 0)) {
		auto_freq_interval_ms = value;
	} else if((strncmp(buf, "downdelay", strlen("downdelay")) == 0)) {
		down_rate_delay_ms = value;
	}
	return n;
}

struct ddrfreq_attribute {
	struct attribute	attr;
	ssize_t (*show)(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf);
	ssize_t (*store)(struct kobject *kobj, struct kobj_attribute *attr,
			const char *buf, size_t n);
};

static struct ddrfreq_attribute ddrfreq_attrs[] = {
	/*     node_name	permision		show_func	store_func */    
	__ATTR(ddrfreq,	S_IRUSR|S_IRGRP|S_IWUSR,	ddrbw_dyn_show,	ddrbw_dyn_store),
};
int rk_pm_tests_kobj_atrradd(const struct attribute *attr);

static void ddrfreq_tst_init(void)
{
	int ret;

	ret = rk_pm_tests_kobj_atrradd(&ddrfreq_attrs[0].attr);

	if (ret) {
		printk("%s: create ddrfreq sysfs node error, ret: %d\n", __func__, ret);
		return;
	}
}
#endif
