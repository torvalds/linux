/*
 * Amlogic M2
 * frame buffer driver-----------Deinterlace
 * Author: Rain Zhang <rain.zhang@amlogic.com>
 * Copyright (C) 2010 Amlogic, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */


#include <linux/version.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/semaphore.h>
#include <linux/workqueue.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/mm.h>
#include <linux/major.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/cdev.h>
#include <linux/proc_fs.h>
#include <linux/list.h>

//#include <linux/aml_common.h>
#include <asm/uaccess.h>
#include <mach/am_regs.h>
#include <linux/of_fdt.h>
#include <linux/amlogic/osd/osd_dev.h>
#include <linux/amlogic/amports/vframe.h>
#include <linux/amlogic/amports/vframe_provider.h>
#include <linux/amlogic/amports/vframe_receiver.h>
#include <linux/amlogic/amports/canvas.h>
#include "deinterlace.h"
#include "deinterlace_module.h"
#include <linux/amlogic/tvin/tvin_v4l2.h>

#if defined(NEW_DI_TV)
#define FORCE_BOB_SUPPORT
#define ENABLE_SPIN_LOCK_ALWAYS
#else
#define ENABLE_SPIN_LOCK_ALWAYS
#endif
#ifdef DET3D
#include "detect3d.h"
#endif

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
#define RUN_DI_PROCESS_IN_IRQ
#define RUN_REG_IN_IRQ
#endif

//#define RUN_DI_PROCESS_IN_TIMER_IRQ
//#define RUN_DI_PROCESS_IN_TIMER

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
#ifdef CONFIG_AML_VSYNC_FIQ_ENABLE
#define FIQ_VSYNC
#define FIQ_IRQ_ON_MULTI_CORE
#else
#undef FIQ_VSYNC
#endif
#else
#define FIQ_VSYNC
#endif

#ifdef FIQ_VSYNC
#include <plat/fiq_bridge.h>
//#include <linux/fiq_bridge.h>
static DEFINE_SPINLOCK(di_lock2);
#ifdef FIQ_IRQ_ON_MULTI_CORE
/* mutil-core */
#define di_lock_irqfiq_save(irq_flag, fiq_flag) \
                if(receiver_is_amvideo){\
                    raw_local_save_flags(fiq_flag); \
                    local_fiq_disable(); \
                }\
                spin_lock_irqsave(&di_lock2, irq_flag);


#define di_unlock_irqfiq_restore(irq_flag, fiq_flag) \
                spin_unlock_irqrestore(&di_lock2, irq_flag);\
                if(receiver_is_amvideo){\
                    raw_local_irq_restore(fiq_flag);\
                }


#else
#define di_lock_irqfiq_save(irq_flag, fiq_flag) \
                if(receiver_is_amvideo){\
                    raw_local_save_flags(fiq_flag); \
                    local_fiq_disable(); \
                }\
                else{\
                    spin_lock_irqsave(&di_lock2, irq_flag);\
                }

#define di_unlock_irqfiq_restore(irq_flag, fiq_flag) \
                if(receiver_is_amvideo){\
                    raw_local_irq_restore(fiq_flag);\
                }\
                else{\
                    spin_unlock_irqrestore(&di_lock2, irq_flag);\
                }
#endif


#else
/* !FIQ_VSYNC */

#ifdef ENABLE_SPIN_LOCK_ALWAYS
static DEFINE_SPINLOCK(di_lock2);
#define di_lock_irqfiq_save(irq_flag, fiq_flag) \
				fiq_flag=0; \
                spin_lock_irqsave(&di_lock2, irq_flag);

#define di_unlock_irqfiq_restore(irq_flag, fiq_flag) \
                spin_unlock_irqrestore(&di_lock2, irq_flag);

#else
#define di_lock_irqfiq_save(irq_flag, fiq_flag)
#define di_unlock_irqfiq_restore(irq_flag, fiq_flag)
#endif

#endif

int mpeg2vdin_flag = 0;
module_param(mpeg2vdin_flag,int,0644);
MODULE_PARM_DESC(mpeg2vdin_flag,"mpeg2vdin_en");

int mpeg2vdin_en = 0;
module_param(mpeg2vdin_en,int,0644);
MODULE_PARM_DESC(mpeg2vdin_en,"mpeg2vdin_en");

static bool overturn = false;
module_param(overturn,bool,0644);
MODULE_PARM_DESC(overturn,"overturn /disable reverse");

static bool check_start_drop_prog = false;
module_param(check_start_drop_prog,bool,0644);
MODULE_PARM_DESC(check_start_drop_prog,"enable/disable progress start drop function");
#define CHECK_VDIN_BUF_ERROR

#define DEVICE_NAME "deinterlace"

#ifdef DI_USE_FIXED_CANVAS_IDX
bool is_vsync_rdma_enable(void);
#endif

//#ifdef DEBUG
#define pr_dbg(fmt, args...) printk(KERN_DEBUG "DI: " fmt, ## args)
//#else
//#define pr_dbg(fmt, args...)
//#endif
#define pr_error(fmt, args...) printk(KERN_ERR "DI: " fmt, ## args)

extern void timestamp_pcrscr_enable(u32 enable);

char* vf_get_receiver_name(const char* provider_name);

#ifdef CONFIG_VSYNC_RDMA
void enable_rdma(int enable_flag);
#endif

static DEFINE_SPINLOCK(plist_lock);

static di_dev_t di_device;

static dev_t di_id;
static struct class *di_class;

#define INIT_FLAG_NOT_LOAD 0x80
static char version_s[] = "2014-08-11a";//add mpeg2vdin for m8
static unsigned char boot_init_flag=0;
static int receiver_is_amvideo = 1;

static unsigned char new_keep_last_frame_enable = 0;
static int bypass_state = 1;
static int bypass_prog = 0;
static int bypass_hd_prog = 0;
#if (MESON_CPU_TYPE>=MESON_CPU_TYPE_MESON8)
static int bypass_interlace_output = 0;
#else
static int bypass_interlace_output = 1;
#endif
#ifdef CONFIG_AM_DEINTERLACE_SD_ONLY
static int bypass_hd = 1;
#else
static int bypass_hd = 0;
#endif
static int bypass_superd = 1;
static int bypass_all = 0;
static int bypass_trick_mode = 1;
static int bypass_1080p = 0;
static int bypass_3d = 1;
static int invert_top_bot = 0;
static int skip_top_bot = 0;
static char interlace_output_flag = 0;
static int bypass_get_buf_threshold = 4;

#if (MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8)
static int post_hold_line = 17;//for m8 1080i/50 output
static int force_update_post_reg = 0x10;
#elif MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
static int post_hold_line = 20;
static int force_update_post_reg = 0x10;/* bit[4], 1 call process_fun every vsync; 0 call process_fun when toggle frame (early_process_fun is called)
                                        bit[3:0] set force_update_post_reg = 1: only update post reg in vsync for one time
                                         set force_update_post_reg = 2: always update post reg in vsync
                                      */
#else
static int post_hold_line = 10;
static int force_update_post_reg = 0x12;
#endif
static int bypass_post = 0;
static int bypass_post_state = 0;
static int bypass_dynamic = 0;
static int bypass_dynamic_flag = 0;
static int vdin_source_flag = 0;
static int update_post_reg_count = 1;
static int timeout_miss_policy = 0; /* 0, use di_wr_buf still; 1, call pre_de_done_buf_clear to clear di_wr_buf ; 2, do nothing*/

static int post_ready_empty_count = 0;

static int force_width = 0;
static int force_height = 0;
#ifdef NEW_DI_V1
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
static int di_vscale_skip_enable = 4;
#else
static int di_vscale_skip_enable = 3;
#endif
#else
static int di_vscale_skip_enable = 1;
#endif

#ifdef RUN_DI_PROCESS_IN_IRQ
/*
    di_process() run in irq,
    di_reg_process(), di_unreg_process() run in kernel thread
    di_vf_put(), di_vf_peek(), di_vf_get() run in irq if FIQ_VSYNC is not defined,
                                           run in fiq if FIQ_VSYNC is defined.
    di_receiver_event_fun() run in task or irq

*/
/*
    important:
     to set input2pre, VFRAME_EVENT_PROVIDER_VFRAME_READY of vdin should be sent in irq
*/
#ifdef NEW_DI_V1
#undef CHECK_DI_DONE
#else
#define CHECK_DI_DONE
#endif

#ifdef NEW_DI_TV
static int input2pre = 1;
static bool use_2_interlace_buff = true;/*false:process progress by field;true: process progress by frame with 2 interlace buffer*/
#else
static int input2pre = 0;
static bool use_2_interlace_buff = false;
#endif
static int input2pre_buf_miss_count = 0;
static int input2pre_proc_miss_count = 0;
static int input2pre_throw_count = 6;

#ifdef NEW_DI_V1
static int input2pre_miss_policy = 0; /* 0, do not force pre_de_busy to 0, use di_wr_buf after de_irq happen; 1, force pre_de_busy to 0 and call pre_de_done_buf_clear to clear di_wr_buf */
#else
static int input2pre_miss_policy = 0; /* 0, do not force pre_de_busy to 0, use di_wr_buf after de_irq happen; 1, force pre_de_busy to 0 and call pre_de_done_buf_clear to clear di_wr_buf */
#endif
#else
#ifdef NEW_DI_TV
static bool use_2_interlace_buff = true;/*false:process progress by field;true: process progress by frame with 2 interlace buffer*/
#else
static bool use_2_interlace_buff = false;
#endif
#endif
    /* prog_proc_config,
        bit[2:1]: when two field buffers are used, 0 use vpp for blending ,
                                                   1 use post_di module for blending
                                                   2 debug mode, bob with top field
                                                   3 debug mode, bot with bot field
        bit[0]:
         0 "prog vdin" use two field buffers, 1 "prog vdin" use single frame buffer

        bit[4]:
         0 "prog frame from decoder" use two field buffers, 1 use single frame buffer

        bit[5]:
         when two field buffers are used for decoder (bit[4] is 0): 0, use process_count; 1, handle prog frame as two interlace frames
        bit[6]:(bit[4] is 0,bit[5] is 0,use_2_interlace_buff is 0): 0, process progress frame as field,blend by post; 
                                                                    1, process progress frame as field,process by normal di

    */
static int prog_proc_config = (1<<1)|1; /*
                                            for source include both progressive and interlace pictures,
                                            always use post_di module for blending
                                         */
#define is_handle_prog_frame_as_interlace(vframe)  (((prog_proc_config&0x30)==0x20)&&((vframe->type & VIDTYPE_VIU_422)==0))

static int pulldown_detect = 0x10;
static int skip_wrong_field = 1;
static int frame_count = 0;
static int provider_vframe_level = 0;
static int disp_frame_count = 0;
static int start_frame_drop_count = 2;
//static int start_frame_hold_count = 0;

static int force_trig_cnt = 0;
static int di_process_cnt = 0;
static int video_peek_cnt = 0;
static int force_bob_flag = 0;
int di_vscale_skip_count = 0;
static int di_vscale_skip_count_real = 0;
#ifdef D2D3_SUPPORT
static int d2d3_enable = 1;
#endif

#ifdef DET3D
bool det3d_en = false;
static unsigned int det3d_mode = 0;
#endif


static int force_duration_0 = 0;

#ifdef NEW_DI_V1
static unsigned int di_new_mode_mask = 0xffff;
static unsigned int di_new_mode = 0x1;
static int use_reg_cfg = 1;
#define get_new_mode_flag()   ((di_new_mode&di_new_mode_mask&0xffff)|(di_new_mode_mask>>16))
#else
static int use_reg_cfg = 0;
#endif
static uint init_flag=0;
static unsigned char active_flag=0;
static unsigned char recovery_flag=0;
static unsigned int force_recovery=1;
static unsigned int force_recovery_count=0;
static unsigned int  recovery_log_reason;
static unsigned int  recovery_log_queue_idx;
static di_buf_t*  recovery_log_di_buf;


#define VFM_NAME "deinterlace"

static long same_field_top_count = 0;
static long same_field_bot_count = 0;
static long same_w_r_canvas_count = 0;
static long pulldown_count = 0;
static long pulldown_buffer_mode = 1; /* bit 0:
                                        0, keep 3 buffers in pre_ready_list for checking;
                                        1, keep 4 buffers in pre_ready_list for checking;
                                     */
static long pulldown_win_mode = 0x11111;
static int same_field_source_flag_th = 60;
int nr_hfilt_en = 0;

static int pre_hold_line = 10;
static int pre_urgent = 0;
static int post_urgent = 1;

/*pre process speed debug */
static int pre_process_time = 0;
static int pre_process_time_force = 0;
/**/
#define USED_LOCAL_BUF_MAX 3
static int used_local_buf_index[USED_LOCAL_BUF_MAX];
static int used_post_buf_index = -1;

#define DisableVideoLayer() \
    do { CLEAR_MPEG_REG_MASK(VPP_MISC, \
         VPP_VD1_PREBLEND|VPP_VD2_PREBLEND|VPP_VD2_POSTBLEND|VPP_VD1_POSTBLEND ); \
    } while (0)

static int di_receiver_event_fun(int type, void* data, void* arg);
static void di_uninit_buf(void);
static unsigned char is_bypass(vframe_t *vf_in);
static void log_buffer_state(unsigned char* tag);
u32 get_blackout_policy(void);
//static void put_get_disp_buf(void);

static const struct vframe_receiver_op_s di_vf_receiver =
{
    .event_cb = di_receiver_event_fun
};

static struct vframe_receiver_s di_vf_recv;

static vframe_t *di_vf_peek(void* arg);
static vframe_t *di_vf_get(void* arg);
static void di_vf_put(vframe_t *vf, void* arg);
static int di_event_cb(int type, void *data, void *private_data);
static void di_process(void);
static void di_reg_process(void);
static void di_unreg_process(void);
#ifdef RUN_REG_IN_IRQ
static void di_unreg_process_irq(void);
#endif

static const struct vframe_operations_s deinterlace_vf_provider =
{
    .peek = di_vf_peek,
    .get  = di_vf_get,
    .put  = di_vf_put,
    .event_cb = di_event_cb,
};

static struct vframe_provider_s di_vf_prov;

int di_sema_init_flag = 0;
static struct semaphore  di_sema;

#ifdef FIQ_VSYNC
static bridge_item_t fiq_handle_item;
static irqreturn_t di_vf_put_isr(int irq, void *dev_id)
{
#ifdef RUN_DI_PROCESS_IN_IRQ
    int i;
#ifdef RUN_REG_IN_IRQ
    if(active_flag){
        di_unreg_process_irq();
        di_reg_process();
    }
#endif
    for(i=0; i<2; i++){
        if(active_flag){
            di_process();
        }
    }
    log_buffer_state("pro");
#else
    up(&di_sema);
#endif
    return IRQ_HANDLED;
}
#endif

void trigger_pre_di_process(char idx)
{
    if(di_sema_init_flag == 0)
        return ;

    if(idx!='p')
    {
        log_buffer_state((idx=='i')?"irq":((idx=='p')?"put":((idx=='r')?"rdy":"oth")));
    }

#if (defined RUN_DI_PROCESS_IN_IRQ)
#ifdef FIQ_VSYNC
    fiq_bridge_pulse_trigger(&fiq_handle_item);
#else
    WRITE_CBUS_REG(ISA_TIMERC, 1);
#endif
//#ifndef RUN_REG_IN_IRQ
    if((idx != 'p')&&(idx != 'i')){
        /* trigger di_reg_process and di_unreg_process */
        up(&di_sema);
    }
//#endif
#elif (defined RUN_DI_PROCESS_IN_TIMER_IRQ)
    if((idx != 'p')&&(idx != 'i')){
        /* trigger di_reg_process and di_unreg_process */
        up(&di_sema);
    }
#elif (!(defined RUN_DI_PROCESS_IN_TIMER))
#ifdef FIQ_VSYNC
    if(idx == 'p'){
		    fiq_bridge_pulse_trigger(&fiq_handle_item);
    }
    else
#endif
    {
        up(&di_sema);
    }
#endif
}

#define DI_PRE_INTERVAL     (HZ/100)

static struct timer_list di_pre_timer;
static struct work_struct di_pre_work;

static void di_pre_timer_cb(unsigned long arg)
{
    struct timer_list *timer = (struct timer_list *)arg;

    schedule_work(&di_pre_work);

    timer->expires = jiffies + DI_PRE_INTERVAL;
    add_timer(timer);
}


static void set_output_mode_info(void)
{
    const vinfo_t *info;
    info = get_current_vinfo();

    if(info){
        if((strncmp(info->name, "480cvbs", 7) == 0)||(strncmp(info->name, "576cvbs", 7) == 0)||
            (strncmp(info->name, "480i", 4) == 0) ||(strncmp(info->name, "576i", 4) == 0)||
            (strncmp(info->name, "1080i", 5) == 0)){
            interlace_output_flag = 1;
        }
        else{
            interlace_output_flag = 0;
        }
    }
}

static int display_notify_callback_v(struct notifier_block *block, unsigned long cmd , void *para)
{
    if (cmd != VOUT_EVENT_MODE_CHANGE)
        return 0;

    set_output_mode_info();
    return 0;
}

static struct notifier_block display_mode_notifier_nb_v = {
    .notifier_call    = display_notify_callback_v,
};

/*****************************
*    di attr management :
*    enable
*    mode
*    reg
******************************/
/*config attr*/
static ssize_t show_config(struct device * dev, struct device_attribute *attr, char * buf)
{
    int pos=0;
    return pos;
}

static ssize_t store_config(struct device * dev, struct device_attribute *attr, const char * buf, size_t count);

static void dump_state(void);
static void dump_di_buf(di_buf_t* di_buf);
static void dump_pool(int index);
static void dump_vframe(vframe_t* vf);
static void force_source_change(void);

#define DI_RUN_FLAG_RUN             0
#define DI_RUN_FLAG_PAUSE           1
#define DI_RUN_FLAG_STEP            2
#define DI_RUN_FLAG_STEP_DONE       3

static int run_flag = DI_RUN_FLAG_RUN;
static int pre_run_flag = DI_RUN_FLAG_RUN;
static int dump_state_flag = 0;
#ifdef NEW_DI_V1
static char dump_path[20]={0};
//static int capture_flag =0 ;
#endif

static ssize_t store_dbg(struct device * dev, struct device_attribute *attr, const char * buf, size_t count)
{
    if(strncmp(buf, "buf", 3)==0){
        di_buf_t* di_buf_tmp = (di_buf_t*)simple_strtoul(buf+3,NULL,16);
        dump_di_buf(di_buf_tmp);
    }
    else if(strncmp(buf, "vframe", 6)==0){
        vframe_t* vf = (vframe_t*)simple_strtoul(buf+6,NULL,16);
        dump_vframe(vf);
    }
    else if(strncmp(buf, "pool", 4)==0){
        int idx = simple_strtoul(buf+4,NULL,10);
        dump_pool(idx);
    }
    else if(strncmp(buf, "state", 4)==0){
        dump_state();
    }
    else if(strncmp(buf,"bypass_prog", 11)==0){
        force_source_change();
        if(buf[11]=='1'){
            bypass_prog = 1;
        }
        else{
            bypass_prog = 0;
        }
    }
    else if(strncmp(buf, "prog_proc_config", 16)==0){
        if(buf[16]=='1'){
            prog_proc_config = 1;
        }
        else{
            prog_proc_config = 0;
        }
    }
    else if(strncmp(buf, "time_div", 8)==0){
    }
    else if(strncmp(buf, "show_osd", 8)==0){
        Wr(VIU_OSD1_CTRL_STAT, Rd(VIU_OSD1_CTRL_STAT)|(0xff<<12));
    }
    else if(strncmp(buf, "run", 3)==0){
        //timestamp_pcrscr_enable(1);
        run_flag = DI_RUN_FLAG_RUN;
    }
    else if(strncmp(buf, "pause", 5)==0){
        //timestamp_pcrscr_enable(0);
        run_flag = DI_RUN_FLAG_PAUSE;
    }
    else if(strncmp(buf, "step", 4)==0){
        run_flag = DI_RUN_FLAG_STEP;
    }
    else if(strncmp(buf, "prun", 4)==0){
        pre_run_flag = DI_RUN_FLAG_RUN;
    }
    else if(strncmp(buf, "ppause", 6)==0){
        pre_run_flag = DI_RUN_FLAG_PAUSE;
    }
    else if(strncmp(buf, "pstep", 5)==0){
        pre_run_flag = DI_RUN_FLAG_STEP;
    }
    else if(strncmp(buf, "dumptsync", 9) == 0){

    }
    else if(strncmp(buf, "robust_test", 11) == 0){
        recovery_flag = 1;
    }

    return count;
}
#ifdef NEW_DI_V1
static int __init di_read_canvas_reverse(char *str)
{
        unsigned char *ptr = str;
        pr_info("%s: bootargs is %s.\n",__func__,str);
        if(strstr(ptr,"1")){
                invert_top_bot |= 0x1;
                overturn = true;
        }
        else{
                invert_top_bot &= (~0x1) ;
                overturn = false;
        }

        return 0;
}
__setup("panel_reverse=",di_read_canvas_reverse);
#endif
unsigned int di_debug_flag = 0x10;//enable rdma even di bypassed
static unsigned char* di_log_buf=NULL;
static unsigned int di_log_wr_pos=0;
static unsigned int di_log_rd_pos=0;
static unsigned int di_log_buf_size=0;
static unsigned int di_printk_flag=0;
unsigned int di_log_flag=0;
unsigned int buf_state_log_threshold = 16;
unsigned int buf_state_log_start = 0; /*  set to 1 by condition of "post_ready count < buf_state_log_threshold",
																					reset to 0 by set buf_state_log_threshold as 0 */

static DEFINE_SPINLOCK(di_print_lock);

#define PRINT_TEMP_BUF_SIZE 128

int di_print_buf(char* buf, int len)
{
    unsigned long flags;
    int pos;
    int di_log_rd_pos_;
    if(di_log_buf_size==0)
        return 0;

    spin_lock_irqsave(&di_print_lock, flags);
    di_log_rd_pos_=di_log_rd_pos;
    if(di_log_wr_pos>=di_log_rd_pos)
        di_log_rd_pos_+=di_log_buf_size;

    for(pos=0;pos<len && di_log_wr_pos<(di_log_rd_pos_-1);pos++,di_log_wr_pos++){
        if(di_log_wr_pos>=di_log_buf_size)
            di_log_buf[di_log_wr_pos-di_log_buf_size]=buf[pos];
        else
            di_log_buf[di_log_wr_pos]=buf[pos];
    }
    if(di_log_wr_pos>=di_log_buf_size)
        di_log_wr_pos-=di_log_buf_size;
    spin_unlock_irqrestore(&di_print_lock, flags);
    return pos;

}

//static int log_seq = 0;
#if 0
#define di_print printk
#else
int di_print(const char *fmt, ...)
{
    va_list args;
    int avail = PRINT_TEMP_BUF_SIZE;
    char buf[PRINT_TEMP_BUF_SIZE];
    int pos,len=0;

    if(di_printk_flag&1){
        if(di_log_flag&DI_LOG_PRECISE_TIMESTAMP){
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
            printk("%u:", (unsigned int)Rd(ISA_TIMERE));
#endif
        }
        va_start(args, fmt);
        vprintk(fmt, args);
        va_end(args);
        return 0;
    }

    if(di_log_buf_size==0)
        return 0;

    //len += snprintf(buf+len, avail-len, "%d:",log_seq++);
    if(di_log_flag&DI_LOG_TIMESTAMP){
        len += snprintf(buf+len, avail-len, "%u:", (unsigned int)jiffies);
    }
    else if(di_log_flag&DI_LOG_PRECISE_TIMESTAMP){
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
        len += snprintf(buf+len, avail-len, "%u:", (unsigned int)READ_CBUS_REG(ISA_TIMERE));
#else
        if(READ_CBUS_REG(ISA_TIMERB)==0){
            WRITE_CBUS_REG(ISA_TIMER_MUX, (READ_CBUS_REG(ISA_TIMER_MUX)&(~(3<<TIMER_B_INPUT_BIT)))
                |(TIMER_UNIT_100us<<TIMER_B_INPUT_BIT)|(1<<13)|(1<<17));
            WRITE_CBUS_REG(ISA_TIMERB, 0xffff);
            printk("Deinterlace: Init 100us TimerB\n");
        }
        len += snprintf(buf+len, avail-len, "%u:", (unsigned int)(0x10000-READ_CBUS_REG(ISA_TIMERB)));
#endif
    }
    va_start(args, fmt);
    len += vsnprintf(buf+len, avail-len, fmt, args);
    va_end(args);

    if ((avail-len) <= 0) {
        buf[PRINT_TEMP_BUF_SIZE - 1] = '\0';
    }
    pos = di_print_buf(buf, len);
    //printk("di_print:%d %d\n", di_log_wr_pos, di_log_rd_pos);
	  return pos;
}
#endif

static ssize_t read_log(char * buf)
{
    unsigned long flags;
    ssize_t read_size=0;
    if(di_log_buf_size==0)
        return 0;
    //printk("show_log:%d %d\n", di_log_wr_pos, di_log_rd_pos);
    spin_lock_irqsave(&di_print_lock, flags);
    if(di_log_rd_pos<di_log_wr_pos){
        read_size = di_log_wr_pos-di_log_rd_pos;
    }
    else if(di_log_rd_pos>di_log_wr_pos){
        read_size = di_log_buf_size-di_log_rd_pos;
    }
    if(read_size>PAGE_SIZE)
        read_size=PAGE_SIZE;
    if(read_size>0)
        memcpy(buf, di_log_buf+di_log_rd_pos, read_size);

    di_log_rd_pos += read_size;
    if(di_log_rd_pos>=di_log_buf_size)
        di_log_rd_pos = 0;
    spin_unlock_irqrestore(&di_print_lock, flags);
    return read_size;
}

static ssize_t show_log(struct device * dev, struct device_attribute *attr, char * buf)
{
    return read_log(buf);
}

#ifdef DI_DEBUG
char log_tmp_buf[PAGE_SIZE];
static void dump_log(void)
{
    while(read_log(log_tmp_buf)>0){
        printk("%s", log_tmp_buf);
    }
}
#endif

static ssize_t store_log(struct device * dev, struct device_attribute *attr, const char * buf, size_t count)
{
    int tmp;
    unsigned long flags;
    if(strncmp(buf, "bufsize", 7)==0){
        tmp = simple_strtoul(buf+7,NULL,10);
        spin_lock_irqsave(&di_print_lock, flags);
        if(di_log_buf){
            kfree(di_log_buf);
            di_log_buf=NULL;
            di_log_buf_size=0;
            di_log_rd_pos=0;
            di_log_wr_pos=0;
        }
        if(tmp>=1024){
            di_log_buf_size=0;
            di_log_rd_pos=0;
            di_log_wr_pos=0;
            di_log_buf=kmalloc(tmp, GFP_KERNEL);
            if(di_log_buf){
                di_log_buf_size=tmp;
            }
        }
        spin_unlock_irqrestore(&di_print_lock, flags);
        printk("di_store:set bufsize tmp %d %d\n",tmp, di_log_buf_size);
    }
    else if(strncmp(buf, "printk", 6)==0){
        di_printk_flag = simple_strtoul(buf+6, NULL, 10);
    }
    else{
        di_print(0, "%s", buf);
    }
    return 16;
}

static int set_noise_reduction_level(void)
{
    int nr_zone_0 = 4, nr_zone_1 = 8, nr_zone_2 = 12;
    //int nr_hfilt_en = 0;
    int nr_hfilt_mb_en = 0;
    /* int post_mb_en = 0;
    int blend_mtn_filt_en = 1;
    int blend_data_filt_en = 1;
    */
    unsigned int nr_strength = 0, nr_gain2 = 0, nr_gain1 = 0, nr_gain0 = 0;

    nr_strength = noise_reduction_level;
    if (nr_strength > 64)
        nr_strength = 64;
    nr_gain2 = 64 - nr_strength;
    nr_gain1 = nr_gain2 - ((nr_gain2 * nr_strength + 32) >> 6);
    nr_gain0 = nr_gain1 - ((nr_gain1 * nr_strength + 32) >> 6);
    nr_ctrl1 = (64 << 24) | (nr_gain2 << 16) | (nr_gain1 << 8) | (nr_gain0 << 0);

    nr_ctrl0 =     (1 << 31 ) |          									// nr yuv enable.
                       	(1 << 30 ) |          												// nr range. 3 point
                       	(0 << 29 ) |          												// max of 3 point.
                       	(nr_hfilt_en << 28 ) |          									// nr hfilter enable.
                       	(nr_hfilt_mb_en << 27 ) |          									// nr hfilter motion_blur enable.
#ifdef NEW_DI_V1
                                (1 << 25)                 |//enable nr2
#endif
                                (nr_zone_2 <<16 ) |   												// zone 2
                       	(nr_zone_1 << 8 ) |    												// zone 1
                       	(nr_zone_0 << 0 ) ;   												// zone 0

//    blend_ctrl =     ( post_mb_en << 28 ) |      											// post motion blur enable.
//                              ( 0 << 27 ) |               													// mtn3p(l, c, r) max.
//                              ( 0 << 26 ) |               													// mtn3p(l, c, r) min.
//                              ( 0 << 25 ) |               													// mtn3p(l, c, r) ave.
//                              ( 1 << 24 ) |               													// mtntopbot max
//                              ( blend_mtn_filt_en  << 23 ) | 												// blend mtn filter enable.
//                              ( blend_data_filt_en << 22 ) | 												// blend data filter enable.
//                                kdeint0;                              												// kdeint.
    return 0;

}

typedef struct{
    char* name;
    uint* param;
    int (*proc_fun)(void);
}di_param_t;

unsigned long reg_mtn_info[7];

di_param_t di_params[]=
{
    {"di_mtn_1_ctrl1", &di_mtn_1_ctrl1, NULL},
    {"ei_ctrl0",     &ei_ctrl0, NULL  },
    {"ei_ctrl1",     &ei_ctrl1, NULL   },
    {"ei_ctrl2",     &ei_ctrl2, NULL   },
#ifdef NEW_DI_V1
    {"ei_ctrl3",     &ei_ctrl3, NULL   },
#endif
    {"nr_ctrl0",     &nr_ctrl0, NULL   },
    {"nr_ctrl1",     &nr_ctrl1, NULL   },
    {"nr_ctrl2",     &nr_ctrl2, NULL   },
    {"nr_ctrl3",     &nr_ctrl3, NULL   },

    {"mtn_ctrl_char_diff_cnt",     &mtn_ctrl_char_diff_cnt, NULL   },
    {"mtn_ctrl_low_level",	   &mtn_ctrl_low_level, NULL   },
    {"mtn_ctrl_high_level",	   &mtn_ctrl_high_level, NULL   },
    {"mtn_ctrl", &mtn_ctrl, NULL},
    {"mtn_ctrl_diff_level",	   &mtn_ctrl_diff_level, NULL   },
    {"mtn_ctrl1_reduce",    &mtn_ctrl1_reduce, NULL  },
    {"mtn_ctrl1_shift",    &mtn_ctrl1_shift, NULL  },
    {"mtn_ctrl1", &mtn_ctrl1, NULL},
    {"blend_ctrl",   &blend_ctrl, NULL },
    {"kdeint0",       &kdeint0, NULL },
    {"kdeint1",       &kdeint1, NULL },
    {"kdeint2",       &kdeint2, NULL },
    {"blend_ctrl1",  &blend_ctrl1, NULL },
    {"blend_ctrl1_char_level", &blend_ctrl1_char_level, NULL},
    {"blend_ctrl1_angle_thd", &blend_ctrl1_angle_thd, NULL},
    {"blend_ctrl1_filt_thd", &blend_ctrl1_filt_thd, NULL},
    {"blend_ctrl1_diff_thd", &blend_ctrl1_diff_thd, NULL},
    {"blend_ctrl2",  &blend_ctrl2, NULL },
    {"blend_ctrl2_black_level", &blend_ctrl2_black_level, NULL},
    {"blend_ctrl2_mtn_no_mov", &blend_ctrl2_mtn_no_mov, NULL},
    {"mtn_thre_1_low",&mtn_thre_1_low,NULL},
    {"mtn_thre_1_high",&mtn_thre_1_high,NULL},
    {"mtn_thre_2_low",&mtn_thre_2_low,NULL},
    {"mtn_thre_2_high",&mtn_thre_2_high,NULL},
    {"mtn_info0",((uint*)&reg_mtn_info[0]) ,NULL},
    {"mtn_info1",((uint*)&reg_mtn_info[1]) ,NULL},
    {"mtn_info2",((uint*)&reg_mtn_info[2]) ,NULL},
    {"mtn_info3",((uint*)&reg_mtn_info[3]) ,NULL},
    {"mtn_info4",((uint*)&reg_mtn_info[4]) ,NULL},
    {"mtn_info5",((uint*)&reg_mtn_info[5]) ,NULL},
 	{"mtn_info6",((uint*)&reg_mtn_info[6]) ,NULL},


    {"post_ctrl__di_blend_en",  &post_ctrl__di_blend_en, NULL},
    {"post_ctrl__di_post_repeat",  &post_ctrl__di_post_repeat, NULL},
    {"di_pre_ctrl__di_pre_repeat",  &di_pre_ctrl__di_pre_repeat, NULL},

    {"noise_reduction_level", &noise_reduction_level, set_noise_reduction_level},

    {"field_32lvl", &field_32lvl, NULL},
    {"field_22lvl", &field_22lvl, NULL},
    {"field_pd_frame_diff_chg_th",          &(field_pd_th.frame_diff_chg_th), NULL},
    {"field_pd_frame_diff_num_chg_th",      &(field_pd_th.frame_diff_num_chg_th), NULL},
    {"field_pd_field_diff_chg_th",          &(field_pd_th.field_diff_chg_th), NULL},
    {"field_pd_field_diff_num_chg_th",      &(field_pd_th.field_diff_num_chg_th), NULL},
    {"field_pd_frame_diff_skew_th",     &(field_pd_th.frame_diff_skew_th), NULL},
    {"field_pd_frame_diff_num_skew_th", &(field_pd_th.frame_diff_num_skew_th), NULL},

    {"win0_start_x_r", &pd_win_prop[0].win_start_x_r, NULL},
    {"win0_end_x_r", &pd_win_prop[0].win_end_x_r, NULL},
    {"win0_start_y_r", &pd_win_prop[0].win_start_y_r, NULL},
    {"win0_end_y_r", &pd_win_prop[0].win_end_y_r, NULL},
    {"win0_32lvl", &pd_win_prop[0].win_32lvl, NULL},
    {"win0_22lvl", &pd_win_prop[0].win_22lvl, NULL},
    {"win0_pd_frame_diff_chg_th",           &(win_pd_th[0].frame_diff_chg_th), NULL},
    {"win0_pd_frame_diff_num_chg_th",       &(win_pd_th[0].frame_diff_num_chg_th), NULL},
    {"win0_pd_field_diff_chg_th",           &(win_pd_th[0].field_diff_chg_th), NULL},
    {"win0_pd_field_diff_num_chg_th",       &(win_pd_th[0].field_diff_num_chg_th), NULL},
    {"win0_pd_frame_diff_skew_th",      &(win_pd_th[0].frame_diff_skew_th), NULL},
    {"win0_pd_frame_diff_num_skew_th",  &(win_pd_th[0].frame_diff_num_skew_th), NULL},
    {"win0_pd_field_diff_num_th",  &(win_pd_th[0].field_diff_num_th), NULL},

    {"win1_start_x_r", &pd_win_prop[1].win_start_x_r, NULL},
    {"win1_end_x_r",   &pd_win_prop[1].win_end_x_r, NULL},
    {"win1_start_y_r", &pd_win_prop[1].win_start_y_r, NULL},
    {"win1_end_y_r",   &pd_win_prop[1].win_end_y_r, NULL},
    {"win1_32lvl", &pd_win_prop[1].win_32lvl, NULL},
    {"win1_22lvl", &pd_win_prop[1].win_22lvl, NULL},
    {"win1_pd_frame_diff_chg_th",           &(win_pd_th[1].frame_diff_chg_th), NULL},
    {"win1_pd_frame_diff_num_chg_th",       &(win_pd_th[1].frame_diff_num_chg_th), NULL},
    {"win1_pd_field_diff_chg_th",           &(win_pd_th[1].field_diff_chg_th), NULL},
    {"win1_pd_field_diff_num_chg_th",       &(win_pd_th[1].field_diff_num_chg_th), NULL},
    {"win1_pd_frame_diff_skew_th",      &(win_pd_th[1].frame_diff_skew_th), NULL},
    {"win1_pd_frame_diff_num_skew_th",  &(win_pd_th[1].frame_diff_num_skew_th), NULL},
    {"win1_pd_field_diff_num_th",  &(win_pd_th[1].field_diff_num_th), NULL},

    {"win2_start_x_r", &pd_win_prop[2].win_start_x_r, NULL},
    {"win2_end_x_r",   &pd_win_prop[2].win_end_x_r, NULL},
    {"win2_start_y_r", &pd_win_prop[2].win_start_y_r, NULL},
    {"win2_end_y_r",   &pd_win_prop[2].win_end_y_r, NULL},
    {"win2_32lvl", &pd_win_prop[2].win_32lvl, NULL},
    {"win2_22lvl", &pd_win_prop[2].win_22lvl, NULL},
    {"win2_pd_frame_diff_chg_th",           &(win_pd_th[2].frame_diff_chg_th), NULL},
    {"win2_pd_frame_diff_num_chg_th",       &(win_pd_th[2].frame_diff_num_chg_th), NULL},
    {"win2_pd_field_diff_chg_th",           &(win_pd_th[2].field_diff_chg_th), NULL},
    {"win2_pd_field_diff_num_chg_th",       &(win_pd_th[2].field_diff_num_chg_th), NULL},
    {"win2_pd_frame_diff_skew_th",      &(win_pd_th[2].frame_diff_skew_th), NULL},
    {"win2_pd_frame_diff_num_skew_th",  &(win_pd_th[2].frame_diff_num_skew_th), NULL},
    {"win2_pd_field_diff_num_th",  &(win_pd_th[2].field_diff_num_th), NULL},

    {"win3_start_x_r", &pd_win_prop[3].win_start_x_r, NULL},
    {"win3_end_x_r",   &pd_win_prop[3].win_end_x_r, NULL},
    {"win3_start_y_r", &pd_win_prop[3].win_start_y_r, NULL},
    {"win3_end_y_r",   &pd_win_prop[3].win_end_y_r, NULL},
    {"win3_32lvl", &pd_win_prop[3].win_32lvl, NULL},
    {"win3_22lvl", &pd_win_prop[3].win_22lvl, NULL},
    {"win3_pd_frame_diff_chg_th",           &(win_pd_th[3].frame_diff_chg_th), NULL},
    {"win3_pd_frame_diff_num_chg_th",       &(win_pd_th[3].frame_diff_num_chg_th), NULL},
    {"win3_pd_field_diff_chg_th",           &(win_pd_th[3].field_diff_chg_th), NULL},
    {"win3_pd_field_diff_num_chg_th",       &(win_pd_th[3].field_diff_num_chg_th), NULL},
    {"win3_pd_frame_diff_skew_th",      &(win_pd_th[3].frame_diff_skew_th), NULL},
    {"win3_pd_frame_diff_num_skew_th",  &(win_pd_th[3].frame_diff_num_skew_th), NULL},
    {"win3_pd_field_diff_num_th",  &(win_pd_th[3].field_diff_num_th), NULL},

    {"win4_start_x_r", &pd_win_prop[4].win_start_x_r, NULL},
    {"win4_end_x_r",   &pd_win_prop[4].win_end_x_r, NULL},
    {"win4_start_y_r", &pd_win_prop[4].win_start_y_r, NULL},
    {"win4_end_y_r",   &pd_win_prop[4].win_end_y_r, NULL},
    {"win4_32lvl", &pd_win_prop[4].win_32lvl, NULL},
    {"win4_22lvl", &pd_win_prop[4].win_22lvl, NULL},
    {"win4_pd_frame_diff_chg_th",           &(win_pd_th[4].frame_diff_chg_th), NULL},
    {"win4_pd_frame_diff_num_chg_th",       &(win_pd_th[4].frame_diff_num_chg_th), NULL},
    {"win4_pd_field_diff_chg_th",           &(win_pd_th[4].field_diff_chg_th), NULL},
    {"win4_pd_field_diff_num_chg_th",       &(win_pd_th[4].field_diff_num_chg_th), NULL},
    {"win4_pd_frame_diff_skew_th",      &(win_pd_th[4].frame_diff_skew_th), NULL},
    {"win4_pd_frame_diff_num_skew_th",  &(win_pd_th[4].frame_diff_num_skew_th), NULL},
    {"win4_pd_field_diff_num_th",  &(win_pd_th[4].field_diff_num_th), NULL},

    {"pd32_match_num",  &pd32_match_num, NULL},
    {"pd32_diff_num_0_th",  &pd32_diff_num_0_th, NULL},
    {"pd32_debug_th", &pd32_debug_th, NULL},
    {"pd22_th", &pd22_th, NULL},
    {"pd22_num_th", &pd22_num_th, NULL},

    {"", NULL}
};

static ssize_t show_parameters(struct device * dev, struct device_attribute *attr, char * buf)
{
    int i = 0;
    int len = 0;
    for(i=0; di_params[i].param; i++){
        len += sprintf(buf+len, "%s=%08x\n", di_params[i].name, *(di_params[i].param));
    }
    return len;
}

static char tmpbuf[128];
static ssize_t store_parameters(struct device * dev, struct device_attribute *attr, const char * buf, size_t count)
{
    int i = 0;
    int j;
    while((buf[i])&&(buf[i]!='=')&&(buf[i]!=' ')){
        tmpbuf[i]=buf[i];
        i++;
    }
    tmpbuf[i]=0;
    //printk("%s\n", tmpbuf);
    if(strcmp("reset", tmpbuf)==0){
        reset_di_para();
        di_print("reset param\n");
    }
    else{
        for(j=0; di_params[j].param; j++){
            if(strcmp(di_params[j].name, tmpbuf)==0){
                int value=simple_strtoul(buf+i+1, NULL, 16);
                *(di_params[j].param) = value;
                if(di_params[j].proc_fun){
                    di_params[j].proc_fun();
                }
                di_print("set %s=%x\n", di_params[j].name, value);
                break;
            }
        }
    }
    return count;
}

typedef struct{
    char* name;
    uint* status;
}di_status_t;

di_status_t di_status[]=
{
    {"active",     &init_flag  },
    {"", NULL}
};

static ssize_t show_status(struct device * dev, struct device_attribute *attr, char * buf)
{
    int i = 0;
    int len = 0;
    di_print("%s\n", __func__);
    for(i=0; di_status[i].status; i++){
        len += sprintf(buf+len, "%s=%08x\n", di_status[i].name, *(di_status[i].status));
    }
    return len;
}

static ssize_t show_vframe_status(struct device *dev, struct device_attribute* attr, char* buf)
{
    int ret = 0;
    vframe_states_t states;
    struct vframe_provider_s *vfp;
    vfp = vf_get_provider(VFM_NAME);
    if (vfp && vfp->ops && vfp->ops->vf_states) {
        ret = vfp->ops->vf_states(&states, vfp->op_arg);
    }
    if (ret == 0) {
        ret += sprintf(buf + ret, "vframe_pool_size=%d\n", states.vf_pool_size);
        ret += sprintf(buf + ret, "vframe buf_free_num=%d\n", states.buf_free_num);
        ret += sprintf(buf + ret, "vframe buf_recycle_num=%d\n", states.buf_recycle_num);
        ret += sprintf(buf + ret, "vframe buf_avail_num=%d\n", states.buf_avail_num);

    } else {
        ret += sprintf(buf + ret, "vframe no states\n");
    }

    return ret;
}

#ifdef NEW_DI_V1
static ssize_t store_dump_mem(struct device * dev, struct device_attribute *attr, const char * buf, size_t len);
#endif
static DEVICE_ATTR(config, 0664, show_config, store_config);
static DEVICE_ATTR(parameters, 0664, show_parameters, store_parameters);
static DEVICE_ATTR(debug, 0664, NULL, store_dbg);
#ifdef NEW_DI_V1
static DEVICE_ATTR(dump_pic, 0664, NULL, store_dump_mem);
#endif
static DEVICE_ATTR(log, 0664, show_log, store_log);
static DEVICE_ATTR(status, 0664, show_status, NULL);
static DEVICE_ATTR(provider_vframe_status, 0664, show_vframe_status, NULL);
/***************************
* di buffer management
***************************/
#define MAX_IN_BUF_NUM        16
#define MAX_LOCAL_BUF_NUM     12
#define MAX_POST_BUF_NUM      16

#define VFRAME_TYPE_IN          1
#define VFRAME_TYPE_LOCAL       2
#define VFRAME_TYPE_POST        3
#define VFRAME_TYPE_NUM         3
#ifdef DI_DEBUG
static char* vframe_type_name[] = {"", "di_buf_in", "di_buf_loc", "di_buf_post"};
#endif

static unsigned long di_mem_start;
static unsigned long di_mem_size;
#if defined(CONFIG_AM_DEINTERLACE_SD_ONLY)
static unsigned int default_width = 720;
static unsigned int default_height = 576;
#else
static unsigned int default_width = 1920;
static unsigned int default_height = 1080;
#endif
static int local_buf_num;

    /*
        progressive frame process type config:
        0, process by field;
        1, process by frame (only valid for vdin source whose width/height does not change)
    */
static vframe_t* vframe_in[MAX_IN_BUF_NUM];
static vframe_t vframe_in_dup[MAX_IN_BUF_NUM];
static vframe_t vframe_local[MAX_LOCAL_BUF_NUM*2];
static vframe_t vframe_post[MAX_POST_BUF_NUM];
static di_buf_t* cur_post_ready_di_buf = NULL;

static di_buf_t di_buf_local[MAX_LOCAL_BUF_NUM*2];
static di_buf_t di_buf_in[MAX_IN_BUF_NUM];
static di_buf_t di_buf_post[MAX_POST_BUF_NUM];

/*
all buffers are in
1) list of local_free_list,in_free_list,pre_ready_list,recycle_list
2) di_pre_stru.di_inp_buf
3) di_pre_stru.di_wr_buf
4) cur_post_ready_di_buf
5) (di_buf_t*)(vframe->private_data)->di_buf[]

6) post_free_list_head
8) (di_buf_t*)(vframe->private_data)
*/
#define QUEUE_LOCAL_FREE       0
#define QUEUE_IN_FREE          1
#define QUEUE_PRE_READY        2
#define QUEUE_POST_FREE        3
#define QUEUE_POST_READY       4
#define QUEUE_RECYCLE          5
#define QUEUE_DISPLAY          6
#define QUEUE_TMP              7
#define QUEUE_NUM 8

#ifdef USE_LIST
static struct list_head local_free_list_head = LIST_HEAD_INIT(local_free_list_head); // vframe is local_vframe
static struct list_head in_free_list_head = LIST_HEAD_INIT(in_free_list_head); //vframe is NULL
static struct list_head pre_ready_list_head = LIST_HEAD_INIT(pre_ready_list_head); //vframe is either local_vframe or in_vframe
static struct list_head recycle_list_head = LIST_HEAD_INIT(recycle_list_head); //vframe is either local_vframe or in_vframe

static struct list_head post_free_list_head = LIST_HEAD_INIT(post_free_list_head); //vframe is post_vframe
static struct list_head post_ready_list_head = LIST_HEAD_INIT(post_ready_list_head); //vframe is post_vframe

static struct list_head display_list_head = LIST_HEAD_INIT(display_list_head); //vframe sent out for displaying

static struct list_head* list_head_array[QUEUE_NUM];

#define get_di_buf_head(queue_idx) \
    list_first_entry(list_head_array[queue_idx], struct di_buf_s, list)

static void queue_init(int local_buffer_num)
{
    list_head_array[QUEUE_LOCAL_FREE] = &local_free_list_head;
    list_head_array[QUEUE_IN_FREE] = &in_free_list_head;
    list_head_array[QUEUE_PRE_READY] = &pre_ready_list_head;
    list_head_array[QUEUE_POST_FREE] = &post_free_list_head;
    list_head_array[QUEUE_POST_READY] = &post_ready_list_head;
    list_head_array[QUEUE_RECYCLE] = &recycle_list_head;
    list_head_array[QUEUE_DISPLAY] = &display_list_head;
    list_head_array[QUEUE_TMP] = &post_ready_list_head;

}

static bool queue_empty(int queue_idx)
{
    return list_empty(list_head_array[queue_idx]);
}

static void queue_out(di_buf_t* di_buf)
{
    list_del(&(di_buf->list));
}

static void queue_in(di_buf_t* di_buf, int queue_idx)
{
    list_add_tail(&(di_buf->list), list_head_array[queue_idx]);
}

static int list_count(int queue_idx)
{
    int count = 0;
    di_buf_t *p = NULL, *ptmp;
    list_for_each_entry_safe(p, ptmp, list_head_array[queue_idx], list) {
        count++;
    }
    return count;
}

#define queue_for_each_entry(di_buf, ptm, queue_idx, list)  \
    list_for_each_entry_safe(di_buf, ptmp, list_head_array[queue_idx], list)

#else
#define MAX_QUEUE_POOL_SIZE   256
typedef struct queue_s{
    int num;
    int in_idx;
    int out_idx;
    int type; /* 0, first in first out; 1, general;2, fix position for di buf*/
    unsigned int pool[MAX_QUEUE_POOL_SIZE];
}queue_t;
static queue_t queue[QUEUE_NUM];

struct di_buf_pool_s{
    di_buf_t* di_buf_ptr;
    unsigned int size;
}di_buf_pool[VFRAME_TYPE_NUM];

#define queue_for_each_entry(di_buf, ptm, queue_idx, list)  \
    for(itmp=0; ((di_buf = get_di_buf(queue_idx, &itmp))!=NULL); )

static void queue_init(int local_buffer_num)
{
    int i, j;
    for(i=0; i<QUEUE_NUM; i++){
        queue_t* q = &queue[i];
        for(j=0; j<MAX_QUEUE_POOL_SIZE; j++){
            q->pool[j] = 0;
        }
        q->in_idx = 0;
        q->out_idx = 0;
        q->num = 0;
        q->type = 0;
        if((i==QUEUE_RECYCLE)||(i==QUEUE_DISPLAY)||(i==QUEUE_TMP)){
            q->type = 1;
        }
        if((i==QUEUE_LOCAL_FREE) && use_2_interlace_buff)
       	    q->type = 2;
    }
    if(local_buffer_num > 0){
        di_buf_pool[VFRAME_TYPE_IN-1].di_buf_ptr = &di_buf_in[0];
        di_buf_pool[VFRAME_TYPE_IN-1].size = MAX_IN_BUF_NUM;

        di_buf_pool[VFRAME_TYPE_LOCAL-1].di_buf_ptr = &di_buf_local[0];
        di_buf_pool[VFRAME_TYPE_LOCAL-1].size = local_buffer_num;

        di_buf_pool[VFRAME_TYPE_POST-1].di_buf_ptr = &di_buf_post[0];
        di_buf_pool[VFRAME_TYPE_POST-1].size = MAX_POST_BUF_NUM;
    }

}

static di_buf_t* get_di_buf(int queue_idx, int* start_pos)
{
    queue_t* q = &(queue[queue_idx]);
    int idx=0;
    unsigned int pool_idx, di_buf_idx;
    di_buf_t* di_buf = NULL;
    int start_pos_init = *start_pos;

#ifdef DI_DEBUG
    if(di_log_flag&DI_LOG_QUEUE){
        di_print("%s:<%d:%d,%d,%d> %d\n", __func__, queue_idx, q->num, q->in_idx, q->out_idx, *start_pos);
    }
#endif
    if(q->type==0){
        if((*start_pos) < q->num){
            idx = q->out_idx + (*start_pos);
            if(idx >= MAX_QUEUE_POOL_SIZE){
                idx-=MAX_QUEUE_POOL_SIZE;
            }
            (*start_pos)++;
        }
        else{
            idx = MAX_QUEUE_POOL_SIZE;
        }
    }
    else if((q->type == 1)||(q->type == 2)){
        for(idx=(*start_pos); idx<MAX_QUEUE_POOL_SIZE; idx++){
            if(q->pool[idx]!=0){
                *start_pos = idx+1;
                break;
            }
        }
    }
    if(idx<MAX_QUEUE_POOL_SIZE){
        pool_idx = ((q->pool[idx]>>8)&0xff)-1;
        di_buf_idx = q->pool[idx]&0xff;
        if(pool_idx < VFRAME_TYPE_NUM){
            if(di_buf_idx < di_buf_pool[pool_idx].size){
                di_buf = &(di_buf_pool[pool_idx].di_buf_ptr[di_buf_idx]);
            }
        }
    }

    if((di_buf)&&( (((pool_idx+1)<<8)|di_buf_idx) != ((di_buf->type<<8)|(di_buf->index)))){
#ifdef DI_DEBUG
        printk("%s: Error (%x,%x)\n", __func__,  (((pool_idx+1)<<8)|di_buf_idx), ((di_buf->type<<8)|(di_buf->index)) );
#endif
        if(recovery_flag==0){
            recovery_log_reason = 1;
            recovery_log_queue_idx = (start_pos_init<<8)|queue_idx;
            recovery_log_di_buf = di_buf;
        }
        recovery_flag++;
        di_buf = NULL;
    }

#ifdef DI_DEBUG
    if(di_log_flag&DI_LOG_QUEUE){
        if(di_buf){
            di_print("%s: %x(%d,%d)\n", __func__, di_buf, pool_idx, di_buf_idx);
        }
        else{
            di_print("%s: %x\n", __func__, di_buf);
        }
    }
#endif
    return di_buf;
}


static di_buf_t* get_di_buf_head(int queue_idx)
{
    queue_t* q = &(queue[queue_idx]);
    int idx;
    unsigned int pool_idx, di_buf_idx;
    di_buf_t* di_buf = NULL;
#ifdef DI_DEBUG
    if(di_log_flag&DI_LOG_QUEUE){
        di_print("%s:<%d:%d,%d,%d>\n", __func__,queue_idx,q->num, q->in_idx, q->out_idx);
    }
#endif
    if(q->num > 0){
        if(q->type==0){
            idx = q->out_idx;
        }
        else{
            for(idx=0; idx<MAX_QUEUE_POOL_SIZE; idx++){
                if(q->pool[idx]!=0){
                    break;
                }
            }
        }
        if(idx<MAX_QUEUE_POOL_SIZE){
            pool_idx = ((q->pool[idx]>>8)&0xff)-1;
            di_buf_idx = q->pool[idx]&0xff;
            if(pool_idx < VFRAME_TYPE_NUM){
                if(di_buf_idx < di_buf_pool[pool_idx].size){
                    di_buf = &(di_buf_pool[pool_idx].di_buf_ptr[di_buf_idx]);
                }
            }
        }
    }

    if((di_buf)&&( (((pool_idx+1)<<8)|di_buf_idx) != ((di_buf->type<<8)|(di_buf->index)))){
#ifdef DI_DEBUG
        printk("%s: Error (%x,%x)\n", __func__,  (((pool_idx+1)<<8)|di_buf_idx), ((di_buf->type<<8)|(di_buf->index)) );
#endif
        if(recovery_flag==0){
            recovery_log_reason = 2;
            recovery_log_queue_idx = queue_idx;
            recovery_log_di_buf = di_buf;
        }
        recovery_flag++;
        di_buf = NULL;
    }

#ifdef DI_DEBUG
    if(di_log_flag&DI_LOG_QUEUE){
        if(di_buf){
            di_print("%s: %x(%d,%d)\n", __func__, di_buf, pool_idx, di_buf_idx);
        }
        else{
            di_print("%s: %x\n", __func__, di_buf);
        }
    }
#endif
    return di_buf;

}

static void queue_out(di_buf_t* di_buf)
{
    int i;
    queue_t* q;
    if(di_buf == NULL){
#ifdef DI_DEBUG
        printk("%s:Error\n", __func__);
#endif
        if(recovery_flag==0){
            recovery_log_reason = 3;
        }
        recovery_flag++;
        return;
    }
    if(di_buf->queue_index>=0 && di_buf->queue_index<QUEUE_NUM){
        q = &(queue[di_buf->queue_index]);
#ifdef DI_DEBUG
        if(di_log_flag&DI_LOG_QUEUE){
            di_print("%s:<%d:%d,%d,%d> %x\n", __func__, di_buf->queue_index, q->num, q->in_idx, q->out_idx, di_buf);
        }
#endif
        if(q->num > 0){
            if(q->type==0){
                if(q->pool[q->out_idx] == ((di_buf->type<<8)|(di_buf->index))){
                    q->num--;
                    q->pool[q->out_idx] = 0;
                    q->out_idx++;
                    if(q->out_idx>=MAX_QUEUE_POOL_SIZE){
                        q->out_idx = 0;
                    }
                    di_buf->queue_index = -1;
                }
                else{
#ifdef DI_DEBUG
                    printk("%s: Error (%d, %x,%x)\n", __func__, di_buf->queue_index, q->pool[q->out_idx], ((di_buf->type<<8)|(di_buf->index)) );
#endif
                    if(recovery_flag==0){
                        recovery_log_reason = 4;
                        recovery_log_queue_idx = di_buf->queue_index;
                        recovery_log_di_buf = di_buf;
                    }
                    recovery_flag++;
                }
            }
            else if(q->type==1){
                int pool_val = (di_buf->type<<8)|(di_buf->index);
                for(i=0; i<MAX_QUEUE_POOL_SIZE; i++){
                    if(q->pool[i]==pool_val){
                        q->num--;
                        q->pool[i]=0;
                        di_buf->queue_index = -1;
                        break;
                    }
                }
                if(i==MAX_QUEUE_POOL_SIZE){
#ifdef DI_DEBUG
                    printk("%s: Error\n", __func__);
#endif
                    if(recovery_flag==0){
                        recovery_log_reason = 5;
                        recovery_log_queue_idx = di_buf->queue_index;
                        recovery_log_di_buf = di_buf;
                    }
                    recovery_flag++;
                }
            }else if(q->type==2){
            	int pool_val = (di_buf->type<<8)|(di_buf->index);
            	if((di_buf->index < MAX_QUEUE_POOL_SIZE)&&(q->pool[di_buf->index]==pool_val)){
            	    q->num--;
            	    q->pool[di_buf->index]=0;
            	    di_buf->queue_index = -1;
                }else{
#ifdef DI_DEBUG
                    printk("%s: Error\n", __func__);
#endif
                    if(recovery_flag==0){
                        recovery_log_reason = 5;
                        recovery_log_queue_idx = di_buf->queue_index;
                        recovery_log_di_buf = di_buf;
                    }
                    recovery_flag++;
                }

            }
        }
    }
    else{
#ifdef DI_DEBUG
        printk("%s: Error, queue_index %d is not right\n", __func__, di_buf->queue_index);
#endif
        if(recovery_flag==0){
            recovery_log_reason = 6;
            recovery_log_queue_idx = 0;
            recovery_log_di_buf = di_buf;
        }
        recovery_flag++;
    }
#ifdef DI_DEBUG
    if(di_log_flag&DI_LOG_QUEUE){
        di_print("%s done\n",__func__);
    }
#endif
}

static void queue_in(di_buf_t* di_buf, int queue_idx)
{
    queue_t* q = NULL;
    if(di_buf == NULL){
#ifdef DI_DEBUG
        printk("%s:Error\n", __func__);
#endif
        if(recovery_flag==0){
            recovery_log_reason = 7;
            recovery_log_queue_idx = queue_idx;
            recovery_log_di_buf = di_buf;
        }
        recovery_flag++;
        return;
    }
    if(di_buf->queue_index != -1){
#ifdef DI_DEBUG
        printk("%s:%s[%d] Error, queue_index(%d) is not -1\n", __func__, vframe_type_name[di_buf->type], di_buf->index,di_buf->queue_index);
#endif
        if(recovery_flag==0){
            recovery_log_reason = 8;
            recovery_log_queue_idx = queue_idx;
            recovery_log_di_buf = di_buf;
        }
        recovery_flag++;
        return;
    }
    q = &(queue[queue_idx]);
#ifdef DI_DEBUG
    if(di_log_flag&DI_LOG_QUEUE){
        di_print("%s:<%d:%d,%d,%d> %x\n", __func__,queue_idx, q->num, q->in_idx, q->out_idx,di_buf);
    }
#endif
    if(q->type==0){
        q->pool[q->in_idx] = (di_buf->type<<8)|(di_buf->index);
        di_buf->queue_index = queue_idx;
        q->in_idx++;
        if(q->in_idx>=MAX_QUEUE_POOL_SIZE){
            q->in_idx = 0;
        }
        q->num++;
    }
    else if(q->type==1){
        int i;
        for(i=0; i<MAX_QUEUE_POOL_SIZE; i++){
            if(q->pool[i]==0){
                q->pool[i] = (di_buf->type<<8)|(di_buf->index);
                di_buf->queue_index = queue_idx;
                q->num++;
                break;
            }
        }
        if(i==MAX_QUEUE_POOL_SIZE){
#ifdef DI_DEBUG
            printk("%s: Error\n", __func__);
#endif
            if(recovery_flag==0){
                recovery_log_reason = 9;
                recovery_log_queue_idx = queue_idx;
            }
            recovery_flag++;
        }
    }else if(q->type==2){
    	if((di_buf->index < MAX_QUEUE_POOL_SIZE)&&(q->pool[di_buf->index]==0)){
    	   q->pool[di_buf->index] = (di_buf->type<<8)|(di_buf->index);
           di_buf->queue_index = queue_idx;
           q->num++;
    	}else{
#ifdef DI_DEBUG
            printk("%s: Error\n", __func__);
#endif
            if(recovery_flag==0){
                recovery_log_reason = 9;
                recovery_log_queue_idx = queue_idx;
            }
            recovery_flag++;
    	}
    }
#ifdef DI_DEBUG
    if(di_log_flag&DI_LOG_QUEUE){
        di_print("%s done\n",__func__);
    }
#endif
}

static int list_count(int queue_idx)
{
    return queue[queue_idx].num;
}

static bool queue_empty(int queue_idx)
{
    return (queue[queue_idx].num == 0);
}
#endif

static bool is_in_queue(di_buf_t* di_buf, int queue_idx)
{
    bool ret = 0;
    di_buf_t *p = NULL;
    int itmp;
    queue_for_each_entry(p, ptmp, queue_idx, list) {
        if(p==di_buf){
            ret = 1;
            break;
        }
    }
    return ret;
}

typedef struct{
    /* pre input */
    DI_MIF_t di_inp_mif;
    DI_MIF_t di_mem_mif;
    DI_MIF_t di_chan2_mif;
    di_buf_t* di_inp_buf;
    di_buf_t* di_inp_buf_next;
    di_buf_t* di_mem_buf_dup_p;
    di_buf_t* di_chan2_buf_dup_p;
    /* pre output */
    DI_SIM_MIF_t di_nrwr_mif;
    DI_SIM_MIF_t di_mtnwr_mif;
    di_buf_t* di_wr_buf;
#ifdef NEW_DI_V1
    DI_SIM_MIF_t di_contp2rd_mif;
    DI_SIM_MIF_t di_contprd_mif;
    DI_SIM_MIF_t di_contwr_mif;
    int field_count_for_cont;
                    /*
                     0 (f0,null,f0)->nr0,
                     1 (f1,nr0,f1)->nr1_cnt,
                     2 (f2,nr1_cnt,nr0)->nr2_cnt
                     3 (f3,nr2_cnt,nr1_cnt)->nr3_cnt
                     */
#endif
    /* pre state */
    int in_seq;
    int recycle_seq;
    int pre_ready_seq;

    int pre_de_busy; /* 1 if pre_de is not done */
    int pre_de_busy_timer_count;
    int pre_de_process_done; /* flag when irq done */
    int pre_de_clear_flag;
    int unreg_req_flag; /* flag is set when VFRAME_EVENT_PROVIDER_UNREG*/
    int unreg_req_flag2;
    int reg_flag;
    int force_unreg_req_flag;
    int disable_req_flag;
        /* current source info */
    int cur_width;
    int cur_height;
    int cur_inp_type;
    int cur_source_type;
    int cur_sig_fmt;
    unsigned int orientation;
    int cur_prog_flag; /* 1 for progressive source */
    int process_count; /* valid only when prog_proc_type is 0, for progressive source: top field 1, bot field 0 */
    int source_change_flag;

    unsigned char prog_proc_type; /* set by prog_proc_config when source is vdin,0:use 2 i serial buffer,1:use 1 p buffer,3:use 2 i paralleling buffer*/
    unsigned char enable_mtnwr;
    unsigned char enable_pulldown_check;

    int same_field_source_flag;
    int left_right;/*1,left eye; 0,right eye in field alternative*/
    /*input2pre*/
    int bypass_start_count;  /* need discard some vframe when input2pre => bypass */
    int vdin2nr;
#ifdef CONFIG_POST_PROCESS_MANAGER_3D_PROCESS
    enum tvin_trans_fmt source_trans_fmt;
    enum tvin_trans_fmt det3d_trans_fmt;
#endif
#ifdef DET3D
    int vframe_interleave_flag;
#endif
    /**/
    int pre_de_irq_timeout_count;
    int pre_throw_flag;
    int bad_frame_throw_count;
}di_pre_stru_t;
static di_pre_stru_t di_pre_stru;

static void dump_di_pre_stru(void)
{
    printk("di_pre_stru:\n");
    printk("di_mem_buf_dup_p       = 0x%p\n", di_pre_stru.di_mem_buf_dup_p);
    printk("di_chan2_buf_dup_p     = 0x%p\n", di_pre_stru.di_chan2_buf_dup_p);
    printk("in_seq                 = %d\n", di_pre_stru.in_seq);
    printk("recycle_seq            = %d\n", di_pre_stru.recycle_seq);
    printk("pre_ready_seq          = %d\n", di_pre_stru.pre_ready_seq);
    printk("pre_de_busy            = %d\n", di_pre_stru.pre_de_busy);
    printk("pre_de_busy_timer_count= %d\n", di_pre_stru.pre_de_busy_timer_count);
    printk("pre_de_process_done    = %d\n", di_pre_stru.pre_de_process_done);
    printk("pre_de_irq_timeout_count=%d\n",di_pre_stru.pre_de_irq_timeout_count);
    printk("unreg_req_flag         = %d\n", di_pre_stru.unreg_req_flag);
    printk("cur_width              = %d\n", di_pre_stru.cur_width);
    printk("cur_height             = %d\n", di_pre_stru.cur_height);
    printk("cur_inp_type           = 0x%x\n", di_pre_stru.cur_inp_type);
    printk("cur_source_type        = %d\n",  di_pre_stru.cur_source_type);
    printk("cur_prog_flag          = %d\n", di_pre_stru.cur_prog_flag);
    printk("process_count          = %d\n", di_pre_stru.process_count);
    printk("source_change_flag     = %d\n", di_pre_stru.source_change_flag);
    printk("prog_proc_type         = %d\n", di_pre_stru.prog_proc_type);
    printk("enable_mtnwr           = %d\n", di_pre_stru.enable_mtnwr);
    printk("enable_pulldown_check  = %d\n", di_pre_stru.enable_pulldown_check);
    printk("same_field_source_flag = %d\n", di_pre_stru.same_field_source_flag);
#ifdef DET3D
    printk("vframe_interleave_flag = %d\n", di_pre_stru.vframe_interleave_flag);
#endif
    printk("left_right 		   = %d\n", di_pre_stru.left_right);
}

typedef struct{
    DI_MIF_t di_buf0_mif;
    DI_MIF_t di_buf1_mif;
    DI_SIM_MIF_t di_mtncrd_mif;
    DI_SIM_MIF_t di_mtnprd_mif;
    int update_post_reg_flag;
    int post_process_fun_index;
    int run_early_proc_fun_flag;
    int cur_disp_index;
    int canvas_id;
    int next_canvas_id;
    bool toggle_flag;
    bool vscale_skip_flag;
    uint start_pts;
}di_post_stru_t;
static di_post_stru_t di_post_stru;
#ifdef NEW_DI_V1
static ssize_t store_dump_mem(struct device * dev, struct device_attribute *attr, const char * buf, size_t len)
{
	unsigned int n=0, canvas_w=0, canvas_h=0, canvas_real_size=0;
        char *buf_orig, *ps, *token;
        char *parm[6] = {NULL};
        struct file *filp = NULL;
	loff_t pos = 0;
	void * buff = NULL;
	mm_segment_t old_fs;

        if(!buf)
	    return len;
        buf_orig = kstrdup(buf, GFP_KERNEL);
        ps = buf_orig;
        while (1) {
            token = strsep(&ps, " \n");
            if (token == NULL)
                break;
            if (*token == '\0')
                continue;
            parm[n++] = token;
        }
	if(!strncmp(parm[0],"capture",strlen("capture"))){
	    if(parm[1]!=NULL)
		strcpy(dump_path,parm[1]);
	    if(unlikely(di_pre_stru.di_mem_buf_dup_p==NULL)){
                dump_state_flag=0;
	        return len;
	    }
	    canvas_w = (di_pre_stru.di_mem_buf_dup_p->canvas_config_size>>16)&0xffff;
   	    canvas_h = (di_pre_stru.di_mem_buf_dup_p->canvas_config_size)&0xffff;
	    canvas_real_size=canvas_w*canvas_h*2;
	    old_fs = get_fs();
	    set_fs(KERNEL_DS);
	    //	printk("dump path =%s\n",dump_path);
	    filp = filp_open(dump_path,O_RDWR|O_CREAT,0666);
	    if(IS_ERR(filp)){
		printk(KERN_ERR"create %s error.\n",dump_path);
		return len;
	    }
	    dump_state_flag=1;
	    for(n=0; n < 1; n++){
		pos = canvas_real_size * n;
		buf = phys_to_virt(di_pre_stru.di_mem_buf_dup_p->nr_adr+ local_buf_num*n);
		vfs_write(filp,buff,canvas_real_size,&pos);
		/*	pr_info("di_chan2_buf_dup_p:\n  nr:%u,mtn:%u,cnt:%u\n",di_pre_stru.di_chan2_buf_dup_p->nr_adr,
														di_pre_stru.di_chan2_buf_dup_p->mtn_adr,
														di_pre_stru.di_chan2_buf_dup_p->cnt_adr);
			pr_info("di_inp_buf:\n  nr:%u,mtn:%u,cnt:%u\n",di_pre_stru.di_inp_buf->nr_adr,
														di_pre_stru.di_inp_buf->mtn_adr,
														di_pre_stru.di_inp_buf->cnt_adr);
			pr_info("di_wr_buf:\n   nr:%u,mtn:%u,cnt:%u\n",di_pre_stru.di_wr_buf->nr_adr,
														di_pre_stru.di_wr_buf->mtn_adr,
														di_pre_stru.di_wr_buf->cnt_adr);
			pr_info("di_mem_buf_dup_p:\n  nr:%u,mtn:%u,cnt:%u\n",di_pre_stru.di_mem_buf_dup_p->nr_adr,
														di_pre_stru.di_mem_buf_dup_p->mtn_adr,
														di_pre_stru.di_mem_buf_dup_p->cnt_adr);
			pr_info("di_mem_start=%u\n",di_mem_start);
			*/
	    }
	    vfs_fsync(filp,0);
	    dump_state_flag=0;
	    filp_close(filp,NULL);
	    set_fs(old_fs);
	    pr_info("write buffer %2d of %2u  from %u to %s.\n",n,canvas_real_size,di_pre_stru.di_mem_buf_dup_p->nr_adr,dump_path);
	}
	else
	    printk("wrong dump di canvas\n");

	return len;

}
#endif

#define is_from_vdin(vframe) (vframe->type & VIDTYPE_VIU_422)
static void recycle_vframe_type_pre(di_buf_t* di_buf);
static void recycle_vframe_type_post(di_buf_t* di_buf);
#ifdef DI_DEBUG
static void recycle_vframe_type_post_print(di_buf_t* di_buf, const char* func, const int line);
#endif

reg_cfg_t* reg_cfg_head = NULL;

#ifdef NEW_DI_V1
/*****************cvbs and tuner stand defintion******************************/
reg_cfg_t di_default_post_tuner =
{
		NULL,
		((0 << VFRAME_SOURCE_TYPE_OTHERS) |
		 (1 << VFRAME_SOURCE_TYPE_TUNER)  |
		 (1 << VFRAME_SOURCE_TYPE_CVBS)   |
		 (0 << VFRAME_SOURCE_TYPE_COMP)   |
		 (0 << VFRAME_SOURCE_TYPE_HDMI)
		 ),
		1,
		0,
		{
			   ((TVIN_SIG_FMT_CVBS_NTSC_M << 16) | TVIN_SIG_FMT_CVBS_SECAM),
				0
		},
		{
				{DI_MTN_1_CTRL1, 0xa0202015, 0, 31},
				{DI_MTN_1_CTRL2, 0x1a1a3a62, 0, 31},
				{DI_MTN_1_CTRL3, 0x15200a0a, 0, 31},
				{DI_MTN_1_CTRL4, 0x01040440, 0, 31},
				{DI_MTN_1_CTRL5, 0x74000d0d, 0, 31},
//#define DI_MTN_1_CTRL6
				{0x17a9, 0x0d5a1520, 0, 31},
//#define DI_MTN_1_CTRL7
				{0x17aa, 0x0a0a0201, 0, 31},
//#define DI_MTN_1_CTRL8
				{0x17ab, 0x1a1a2662, 0, 31},
//#define DI_MTN_1_CTRL9
				{0x17ac, 0x0d200302, 0, 31},
//#define DI_MTN_1_CTRL10
				{0x17ad, 0x02020606, 0, 31},
//#define DI_MTN_1_CTRL11
				{0x17ae, 0x05080304, 0, 31},
//#define DI_MTN_1_CTRL12
				{0x17af, 0x40020a04, 0, 31},
				{0},
		}
};

/*****************comp stand defintion******************************/

reg_cfg_t di_default_post_comp =
{
		NULL,
		((0 << VFRAME_SOURCE_TYPE_OTHERS) |
		 (0 << VFRAME_SOURCE_TYPE_TUNER)  |
		 (0 << VFRAME_SOURCE_TYPE_CVBS)   |
		 (1 << VFRAME_SOURCE_TYPE_COMP)   |
		 (0 << VFRAME_SOURCE_TYPE_HDMI)
		 ),
		1,
		0,
		{
			   ((TVIN_SIG_FMT_COMP_480P_60HZ_D000 << 16) | TVIN_SIG_FMT_COMP_576I_50HZ_D000),
				0
		},
		{
				{DI_MTN_1_CTRL1, 0xa0202015, 0, 31},
				{DI_MTN_1_CTRL2, 0x1a1a3a62, 0, 31},
				{DI_MTN_1_CTRL3, 0x15200a0a, 0, 31},
				{DI_MTN_1_CTRL4, 0x01040440, 0, 31},
				{DI_MTN_1_CTRL5, 0x74000d0d, 0, 31},
//#define DI_MTN_1_CTRL6
				{0x17a9, 0x0d5a1520, 0, 31},
//#define DI_MTN_1_CTRL7
				{0x17aa, 0x0a0a0201, 0, 31},
//#define DI_MTN_1_CTRL8
				{0x17ab, 0x1a1a2662, 0, 31},
//#define DI_MTN_1_CTRL9
				{0x17ac, 0x0d200302, 0, 31},
//#define DI_MTN_1_CTRL10
				{0x17ad, 0x02020606, 0, 31},
//#define DI_MTN_1_CTRL11
				{0x17ae, 0x05080304, 0, 31},
//#define DI_MTN_1_CTRL12
				{0x17af, 0x40020a04, 0, 31},
				{0},
		}
};

/***************** hdmi stand defintion******************************/

reg_cfg_t di_default_post_hdmi =
{
		NULL,
		((0 << VFRAME_SOURCE_TYPE_OTHERS) |
		 (0 << VFRAME_SOURCE_TYPE_TUNER)  |
		 (0 << VFRAME_SOURCE_TYPE_CVBS)   |
		 (0 << VFRAME_SOURCE_TYPE_COMP)   |
		 (1 << VFRAME_SOURCE_TYPE_HDMI)
		 ),
		1,
		0,
		{
				((TVIN_SIG_FMT_HDMI_640X480P_60HZ << 16) | TVIN_SIG_FMT_HDMI_720X480P_60HZ),
				((TVIN_SIG_FMT_HDMI_1440X480I_60HZ << 16) | TVIN_SIG_FMT_HDMI_1440X480P_60HZ),
				((TVIN_SIG_FMT_HDMI_720X576P_50HZ << 16) | TVIN_SIG_FMT_HDMI_720X576P_50HZ),
				((TVIN_SIG_FMT_HDMI_1440X576I_50HZ << 16) | TVIN_SIG_FMT_HDMI_1440X576P_50HZ),
				((TVIN_SIG_FMT_HDMI_2880X480P_60HZ << 16) | TVIN_SIG_FMT_HDMI_2880X576P_60HZ),
				((TVIN_SIG_FMT_HDMI_720X576P_100HZ << 16) | TVIN_SIG_FMT_HDMI_1440X576I_100HZ),
				((TVIN_SIG_FMT_HDMI_720X480P_120HZ << 16) | TVIN_SIG_FMT_HDMI_1440X480I_240HZ),
				((TVIN_SIG_FMT_HDMI_720X480P_60HZ_FRAME_PACKING << 16) | TVIN_SIG_FMT_HDMI_720X576P_50HZ_FRAME_PACKING),
				0
		},
		{
				{DI_MTN_1_CTRL1, 0xa0202015, 0, 31},
				{DI_MTN_1_CTRL2, 0x1a1a3a62, 0, 31},
				{DI_MTN_1_CTRL3, 0x15200a0a, 0, 31},
				{DI_MTN_1_CTRL4, 0x01040440, 0, 31},
				{DI_MTN_1_CTRL5, 0x74000d0d, 0, 31},
//#define DI_MTN_1_CTRL6
				{0x17a9, 0x0d5a1520, 0, 31},
//#define DI_MTN_1_CTRL7
				{0x17aa, 0x0a0a0201, 0, 31},
//#define DI_MTN_1_CTRL8
				{0x17ab, 0x1a1a2662, 0, 31},
//#define DI_MTN_1_CTRL9
				{0x17ac, 0x0d200302, 0, 31},
//#define DI_MTN_1_CTRL10
				{0x17ad, 0x02020606, 0, 31},
//#define DI_MTN_1_CTRL11
				{0x17ae, 0x05080304, 0, 31},
//#define DI_MTN_1_CTRL12
				{0x17af, 0x40020a04, 0, 31},
				{0},
		}
};
/*****************high defintion******************************/

reg_cfg_t di_default_post_hd =
{

        NULL,
        ((1 << VFRAME_SOURCE_TYPE_OTHERS) |
         (0 << VFRAME_SOURCE_TYPE_TUNER)  |
         (0 << VFRAME_SOURCE_TYPE_CVBS)   |
         (1 << VFRAME_SOURCE_TYPE_COMP)   |
         (1 << VFRAME_SOURCE_TYPE_HDMI)
         ),
        1,
        0,
        {
                ((TVIN_SIG_FMT_COMP_720P_59HZ_D940 << 16) | TVIN_SIG_FMT_COMP_1080I_60HZ_D000),
		((TVIN_SIG_FMT_HDMI_1280X720P_60HZ << 16) | TVIN_SIG_FMT_HDMI_1920X1080I_60HZ),
		((TVIN_SIG_FMT_HDMI_1920X1080P_60HZ << 16) | TVIN_SIG_FMT_HDMI_1920X1080P_60HZ),
		((TVIN_SIG_FMT_HDMI_1280X720P_50HZ << 16) | TVIN_SIG_FMT_HDMI_1920X1080I_50HZ_A),
		((TVIN_SIG_FMT_HDMI_1920X1080P_50HZ << 16) | TVIN_SIG_FMT_HDMI_1920X1080P_30HZ),
		((TVIN_SIG_FMT_HDMI_1920X1080I_50HZ_B << 16) | TVIN_SIG_FMT_HDMI_1920X1080I_100HZ),
		((TVIN_SIG_FMT_HDMI_1920X1080I_120HZ << 16) | TVIN_SIG_FMT_HDMI_1280X720P_120HZ),
		((TVIN_SIG_FMT_HDMI_1280X720P_24HZ << 16) | TVIN_SIG_FMT_HDMI_1920X1080P_30HZ_FRAME_PACKING),
		((TVIN_SIG_FMT_HDMI_1920X1080I_60HZ_ALTERNATIVE << 16) | TVIN_SIG_FMT_HDMI_1920X1080P_30HZ_ALTERNATIVE),
                0
        },
        {
                {DI_MTN_1_CTRL1, 0xa0202015, 0, 31},
                {DI_MTN_1_CTRL2, 0x141a2062, 0, 31},
                {DI_MTN_1_CTRL3, 0x1520050a, 0, 31},
                {DI_MTN_1_CTRL4, 0x08800840, 0, 31},//0x08800840
                {DI_MTN_1_CTRL5, 0x74000d0d, 0, 31},
//#define DI_MTN_1_CTRL6
                {0x17a9, 0x0d5a1520, 0, 31},
//#define DI_MTN_1_CTRL7
                {0x17aa, 0x0a0a0201, 0, 31},
//#define DI_MTN_1_CTRL8
                {0x17ab, 0x1a1a2662, 0, 31},
//#define DI_MTN_1_CTRL9
                {0x17ac, 0x0d200302, 0, 31},
//#define DI_MTN_1_CTRL10
                {0x17ad, 0x02020606, 0, 31},
//#define DI_MTN_1_CTRL11
                {0x17ae, 0x05080304, 0, 31},
//#define DI_MTN_1_CTRL12
                {0x17af, 0x40020a04, 0, 31},
                {0},
        }
};
/*****************dtv stand  defintion******************************/

reg_cfg_t di_dtv_post_stand =
{

        NULL,
        ((1 << VFRAME_SOURCE_TYPE_OTHERS) |
         (0 << VFRAME_SOURCE_TYPE_TUNER)  |
         (0 << VFRAME_SOURCE_TYPE_CVBS)   |
         (0 << VFRAME_SOURCE_TYPE_COMP)   |
         (0 << VFRAME_SOURCE_TYPE_HDMI)
         ),
        1,
        1,
        {
             0
        },
        {
                {DI_MTN_1_CTRL1, 0xa0202015, 0, 31},
                {DI_MTN_1_CTRL2, 0x141a2062, 0, 31},
                {DI_MTN_1_CTRL3, 0x1520050a, 0, 31},
                {DI_MTN_1_CTRL4, 0x01040410, 0, 31},//0x08800840
                {DI_MTN_1_CTRL5, 0x74000d0d, 0, 31},
//#define DI_MTN_1_CTRL6
                {0x17a9, 0x0d5a1520, 0, 31},
//#define DI_MTN_1_CTRL7
                {0x17aa, 0x0a0a0201, 0, 31},
//#define DI_MTN_1_CTRL8
                {0x17ab, 0x1a1a2662, 0, 31},
//#define DI_MTN_1_CTRL9
                {0x17ac, 0x0d200302, 0, 31},
//#define DI_MTN_1_CTRL10
                {0x17ad, 0x02020606, 0, 31},
//#define DI_MTN_1_CTRL11
                {0x17ae, 0x05080304, 0, 31},
//#define DI_MTN_1_CTRL12
                {0x17af, 0x40020a04, 0, 31},
                {0},
        }
};
/* new pre and post di setting */
reg_cfg_t di_default_pre =
{
        NULL,
        ((1 << VFRAME_SOURCE_TYPE_OTHERS) |
         (1 << VFRAME_SOURCE_TYPE_TUNER)  |
         (1 << VFRAME_SOURCE_TYPE_CVBS)   |
         (1 << VFRAME_SOURCE_TYPE_COMP)	  |
         (1 << VFRAME_SOURCE_TYPE_HDMI)
         ),
        0,
        0,
        {
                ((TVIN_SIG_FMT_COMP_480P_60HZ_D000 << 16) | TVIN_SIG_FMT_CVBS_SECAM),
                0
        },
        {
                {DI_EI_CTRL3,  0x0000013, 0, 27},
                {DI_EI_CTRL4, 0x151b3084, 0, 31},
                {DI_EI_CTRL5, 0x5273204f, 0, 31},
                {DI_EI_CTRL6, 0x50232815, 0, 31},
                {DI_EI_CTRL7, 0x2fb56650, 0, 31},
                {DI_EI_CTRL8, 0x230019a4, 0, 31},
                {DI_EI_CTRL9, 0x7cb9bb33, 0, 31},
//#define DI_EI_CTRL10
                {0x1793, 0x0842c6a9,0, 31},
//#define DI_EI_CTRL11
                {0x179e, 0x486ab07a,0, 31},
//#define DI_EI_CTRL12
                {0x179f, 0xdb0c2503,0, 31},
//#define DI_EI_CTRL13
                {0x17a8, 0x0f021414 ,0, 31},
                {0},
        }
};
reg_cfg_t di_default_post =
{
        NULL,
        ((1 << VFRAME_SOURCE_TYPE_OTHERS) |
         (1 << VFRAME_SOURCE_TYPE_TUNER)  |
         (1 << VFRAME_SOURCE_TYPE_CVBS)   |
         (1 << VFRAME_SOURCE_TYPE_COMP)   |
         (1 << VFRAME_SOURCE_TYPE_HDMI)
         ),
        1,
        2,
        {
               ((TVIN_SIG_FMT_COMP_480P_60HZ_D000 << 16) | TVIN_SIG_FMT_CVBS_SECAM),
                0
        },
        {
                {DI_MTN_1_CTRL1,         0, 30, 1},
                {DI_MTN_1_CTRL1, 0x0202015,  0, 27},
                {DI_MTN_1_CTRL2, 0x141a2062, 0, 31},
                {DI_MTN_1_CTRL3, 0x1520050a, 0, 31},
                {DI_MTN_1_CTRL4, 0x08800840, 0, 31},
                {DI_MTN_1_CTRL5, 0x74000d0d, 0, 31},
//#define DI_MTN_1_CTRL6
                {0x17a9, 0x0d5a1520, 0, 31},
//#define DI_MTN_1_CTRL7
                {0x17aa, 0x0a0a0201, 0, 31},
//#define DI_MTN_1_CTRL8
                {0x17ab, 0x1a1a2662, 0, 31},
//#define DI_MTN_1_CTRL9
                {0x17ac, 0x0d200302, 0, 31},
//#define DI_MTN_1_CTRL10
                {0x17ad, 0x02020606, 0, 31},
//#define DI_MTN_1_CTRL11
                {0x17ae, 0x05080304, 0, 31},
//#define DI_MTN_1_CTRL12
                {0x17af, 0x40020a04, 0, 31},
                {0},
        }
};



void di_add_reg_cfg(reg_cfg_t* reg_cfg)
{
    reg_cfg->next = reg_cfg_head;
    reg_cfg_head = reg_cfg;
}

static void di_apply_reg_cfg(unsigned char pre_post_type)
{
    if(use_reg_cfg){
        reg_cfg_t* reg_cfg = reg_cfg_head;
        int ii;
        while(reg_cfg){
            unsigned char set_flag = 0;
            if((pre_post_type==reg_cfg->pre_post_type)&&
                ((1<<di_pre_stru.cur_source_type)&reg_cfg->source_types_enable)){
                if(di_pre_stru.cur_source_type==VFRAME_SOURCE_TYPE_OTHERS&&
					    (2!=reg_cfg->dtv_defintion_type)){
			    //if:dtv stand defintion  else if:high defintion
		    	if(di_pre_stru.cur_height<720&&reg_cfg->dtv_defintion_type){
	    			set_flag = 1;
	    		}else if(di_pre_stru.cur_height>=720&&(!reg_cfg->dtv_defintion_type)){
				set_flag = 1;
    			}

	    	}else{
                for(ii=0; ii<FMT_MAX_NUM; ii++){
                    if(reg_cfg->sig_fmt_range[ii]==0){
                        break;
                    }
                    else if((di_pre_stru.cur_sig_fmt >= ((reg_cfg->sig_fmt_range[ii]>>16)&0xffff))&&
                        (di_pre_stru.cur_sig_fmt <= (reg_cfg->sig_fmt_range[ii]&0xffff))){
                        set_flag = 1;
                        break;
                    }
                }
            }
            }
            if(set_flag){
                for(ii=0; ii<REG_SET_MAX_NUM; ii++){
                    if(reg_cfg->reg_set[ii].adr){

                        if(pre_post_type){
                            VSYNC_WR_MPEG_REG_BITS(reg_cfg->reg_set[ii].adr, reg_cfg->reg_set[ii].val,
                                reg_cfg->reg_set[ii].start, reg_cfg->reg_set[ii].len);
                        	}
                        else
                            Wr_reg_bits(reg_cfg->reg_set[ii].adr, reg_cfg->reg_set[ii].val,
                                reg_cfg->reg_set[ii].start, reg_cfg->reg_set[ii].len);
                    }
                    else{
                        break;
                    }
                }
                break;
            }
            reg_cfg = reg_cfg->next;
        }
    }

}
#endif


static void dis2_di(void)
{
    ulong flags=0,fiq_flag=0, irq_flag2=0;
    init_flag = 0;
    di_lock_irqfiq_save(irq_flag2, fiq_flag);
    //vf_unreg_provider(&di_vf_prov);
    vf_light_unreg_provider(&di_vf_prov);
    di_unlock_irqfiq_restore(irq_flag2, fiq_flag);
    spin_lock_irqsave(&plist_lock, flags);
    di_lock_irqfiq_save(irq_flag2, fiq_flag);
    if(di_pre_stru.di_inp_buf){
        if(vframe_in[di_pre_stru.di_inp_buf->index]){
            vf_put(vframe_in[di_pre_stru.di_inp_buf->index], VFM_NAME);
            vframe_in[di_pre_stru.di_inp_buf->index] = NULL;
            vf_notify_provider(VFM_NAME, VFRAME_EVENT_RECEIVER_PUT, NULL);
        }
        //list_add_tail(&(di_pre_stru.di_inp_buf->list), &in_free_list_head);
        di_pre_stru.di_inp_buf->invert_top_bot_flag = 0;
        queue_in(di_pre_stru.di_inp_buf, QUEUE_IN_FREE);
        di_pre_stru.di_inp_buf = NULL;
    }
    di_uninit_buf();
    di_set_power_control(0,0);
    if(get_blackout_policy()){
        di_set_power_control(1,0);
    }
    di_unlock_irqfiq_restore(irq_flag2, fiq_flag);
    spin_unlock_irqrestore(&plist_lock, flags);
}

static ssize_t store_config(struct device * dev, struct device_attribute *attr, const char * buf, size_t count)
{
    if(strncmp(buf, "disable", 7)==0){
#ifdef DI_DEBUG
        di_print("%s: disable\n", __func__);
#endif
        if(init_flag){
            di_pre_stru.disable_req_flag = 1;
            provider_vframe_level = 0;
            bypass_dynamic_flag = 0;
            post_ready_empty_count = 0;

            trigger_pre_di_process('d');
            while(di_pre_stru.disable_req_flag){
                msleep(1);
            }
        }
    }
    else if(strncmp(buf, "dis2", 4)==0){
        dis2_di();
    }
    return count;
}

static unsigned char is_progressive(vframe_t* vframe)
{
    return ((vframe->type & VIDTYPE_TYPEMASK) == VIDTYPE_PROGRESSIVE);
}

static void force_source_change(void)
{
    di_pre_stru.cur_inp_type = 0;
}

static unsigned char is_source_change(vframe_t* vframe)
{
#define VFRAME_FORMAT_MASK  (VIDTYPE_VIU_422|VIDTYPE_VIU_FIELD|VIDTYPE_VIU_SINGLE_PLANE|VIDTYPE_VIU_444|VIDTYPE_MVC)
    if(
        (di_pre_stru.cur_width!=vframe->width)||
        (di_pre_stru.cur_height!=vframe->height)||
        ((di_pre_stru.cur_prog_flag!=is_progressive(vframe))&&(!is_handle_prog_frame_as_interlace(vframe)))||
        (((di_pre_stru.cur_inp_type&VFRAME_FORMAT_MASK)!=(vframe->type&VFRAME_FORMAT_MASK))&&(!is_handle_prog_frame_as_interlace(vframe)))||
        (di_pre_stru.cur_source_type != vframe->source_type)
        ){
        return 1;
    }
    return 0;
}
/*
static unsigned char is_vframe_type_change(vframe_t* vframe)
{
    if(
        (di_pre_stru.cur_prog_flag!=is_progressive(vframe))||
        ((di_pre_stru.cur_inp_type&VFRAME_FORMAT_MASK)!=(vframe->type&VFRAME_FORMAT_MASK))
        ){
        return 1;
    }
    return 0;
}
*/
static int trick_mode;
static unsigned char is_bypass(vframe_t *vf_in)
{
    if(di_debug_flag&0x10000){ //for debugging
        return (di_debug_flag>>17)&0x1;
    }

    if(bypass_interlace_output && interlace_output_flag)
        return 1;

    if(bypass_all)
        return 1;
    if(di_pre_stru.cur_prog_flag&&
        ((bypass_prog)||
        (di_pre_stru.cur_width>1920)||(di_pre_stru.cur_height>1080)
        ||(di_pre_stru.cur_inp_type&VIDTYPE_VIU_444))
        )
        return 1;
    if(di_pre_stru.cur_prog_flag&&
		(di_pre_stru.cur_width==1920)&&(di_pre_stru.cur_height==1080)
        &&(bypass_1080p)
        )
        return 1;

    if(di_pre_stru.cur_prog_flag&&
		(di_pre_stru.cur_width>720)&&(di_pre_stru.cur_height>576)
        &&(bypass_hd_prog)
        )
        return 1;

    if(bypass_hd&&
        ((di_pre_stru.cur_width>720)||(di_pre_stru.cur_height>576))
      )
      return 1;
    if(bypass_superd&&
	((di_pre_stru.cur_width>1920)||(di_pre_stru.cur_height>1080))
      )
	return 1;

    if(di_pre_stru.cur_inp_type&VIDTYPE_MVC)
        return 1;

    if(di_pre_stru.cur_source_type == VFRAME_SOURCE_TYPE_PPMGR)
        return 1;

    if(((bypass_trick_mode)&&(new_keep_last_frame_enable==0))||(bypass_trick_mode&0x2)){
        int trick_mode;
        query_video_status(0, &trick_mode);
        if (trick_mode) return 1;
    }

#ifdef CONFIG_POST_PROCESS_MANAGER_3D_PROCESS
    if(bypass_3d&&
        (di_pre_stru.source_trans_fmt!=0))
        return 1;
#endif
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
/*prot is conflict with di post*/
    if(di_pre_stru.orientation)
	return 1;

    if((di_vscale_skip_enable & 0x4)&& vf_in){
	di_vscale_skip_count = get_current_vscale_skip_count(vf_in);
	if(di_vscale_skip_count > 0 && di_pre_stru.cur_prog_flag)
            return 1;
	return 0;
    }
    #endif
    return 0;

}

static unsigned char is_bypass_post(void)
{
    if(di_debug_flag&0x40000){ //for debugging
        return (di_debug_flag>>19)&0x1;
    }

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
/*prot is conflict with di post*/
    if(di_pre_stru.orientation)
	return 1;
#endif
    if((bypass_post)||(bypass_dynamic_flag&1)){
        return 1;
    }

#ifdef DET3D
    if(det3d_en){
        return 1;
    }

    if(di_pre_stru.vframe_interleave_flag!=0){
        return 1;
    }
#endif
    return 0;
}

#ifdef RUN_DI_PROCESS_IN_IRQ
static unsigned char is_input2pre(void)
{
    if( input2pre
        &&vdin_source_flag
        &&(bypass_state==0)){
#ifdef NEW_DI_V1
        pre_urgent = 1;
#endif
        return 1;
    }

#ifdef NEW_DI_V1
    pre_urgent = 0;
#endif
    return 0;
}
#endif

#ifdef DI_USE_FIXED_CANVAS_IDX
static int di_post_buf0_canvas_idx[2];
static int di_post_buf1_canvas_idx[2];
static int di_post_mtncrd_canvas_idx[2];
static int di_post_mtnprd_canvas_idx[2];

static void config_canvas_idx(di_buf_t* di_buf, int nr_canvas_idx, int mtn_canvas_idx)
{
    if(di_buf){
        int width = (di_buf->canvas_config_size>>16)&0xffff;
        int canvas_height = (di_buf->canvas_config_size)&0xffff;
        if(di_buf->canvas_config_flag == 1){
            if(nr_canvas_idx>=0){
                di_buf->nr_canvas_idx = nr_canvas_idx;
                canvas_config(nr_canvas_idx, di_buf->nr_adr, width*2, canvas_height, 0, 0);
            }
        }
        else if(di_buf->canvas_config_flag == 2){
            if(nr_canvas_idx>=0){
                di_buf->nr_canvas_idx = nr_canvas_idx;
                canvas_config(nr_canvas_idx, di_buf->nr_adr, width*2, canvas_height/2, 0, 0);
            }
	          if(mtn_canvas_idx>=0){
                di_buf->mtn_canvas_idx = mtn_canvas_idx;
                canvas_config(mtn_canvas_idx, di_buf->mtn_adr, width/2, canvas_height/2, 0, 0);
	          }
        }
        if(nr_canvas_idx>=0){
            di_buf->vframe->canvas0Addr = di_buf->nr_canvas_idx;
            di_buf->vframe->canvas1Addr = di_buf->nr_canvas_idx;
        }
    }
}

#ifdef NEW_DI_V1
static void config_cnt_canvas_idx(di_buf_t* di_buf, int cnt_canvas_idx)
{
    if(di_buf){
        int width = (di_buf->canvas_config_size>>16)&0xffff;
        int canvas_height = (di_buf->canvas_config_size)&0xffff;
        di_buf->cnt_canvas_idx = cnt_canvas_idx;
        canvas_config(cnt_canvas_idx, di_buf->cnt_adr, width/2, canvas_height/2, 0, 0);
    }
}
#endif

#else

static void config_canvas(di_buf_t* di_buf)
{
    if(di_buf){
        int width = (di_buf->canvas_config_size>>16)&0xffff;
        int canvas_height = (di_buf->canvas_config_size)&0xffff;
        if(di_buf->canvas_config_flag == 1){
            canvas_config(di_buf->nr_canvas_idx, di_buf->nr_adr, width*2, canvas_height, 0, 0);
            di_buf->canvas_config_flag = 0;
        }
        else if(di_buf->canvas_config_flag == 2){
            canvas_config(di_buf->nr_canvas_idx, di_buf->nr_adr, width*2, canvas_height/2, 0, 0);
	          canvas_config(di_buf->mtn_canvas_idx, di_buf->mtn_adr, width/2, canvas_height/2, 0, 0);
            di_buf->canvas_config_flag = 0;
        }

    }
}

#endif

static int di_init_buf(int width, int height, unsigned char prog_flag)
{
    int i, local_buf_num_available, local_buf_num_valid;
    int canvas_height = height + 8;
#ifdef D2D3_SUPPORT
    unsigned dp_buf_size = 0;
    unsigned dp_mem_start = 0;
#endif
    frame_count = 0;
    disp_frame_count = 0;
    cur_post_ready_di_buf = NULL;
    for(i=0; i<MAX_IN_BUF_NUM; i++){
	vframe_in[i]=NULL;
    }
    memset(&di_pre_stru, 0, sizeof(di_pre_stru));

    if(prog_flag){
        di_pre_stru.prog_proc_type = 1;
#ifdef D2D3_SUPPORT
        if(d2d3_enable){
            dp_buf_size = 256*canvas_height/2;
            local_buf_num = di_mem_size/((width*canvas_height*2)+dp_buf_size);
            dp_mem_start = di_mem_start + (width*canvas_height*2)*local_buf_num;
        }
        else
#endif
        local_buf_num = di_mem_size/(width*canvas_height*2);
        local_buf_num_available = local_buf_num;
        if(local_buf_num > (2*MAX_LOCAL_BUF_NUM)){
            local_buf_num = 2*MAX_LOCAL_BUF_NUM;
        }
        if(local_buf_num >= 6){
            new_keep_last_frame_enable = 1;
        }
        else{
            new_keep_last_frame_enable = 0;
        }
    }
    else{
        di_pre_stru.prog_proc_type = 0;
#ifdef D2D3_SUPPORT
        if(d2d3_enable){
            dp_buf_size = 256*canvas_height/2;
#ifdef NEW_DI_V1
            local_buf_num = di_mem_size/((width*canvas_height*6/4)+dp_buf_size);
            dp_mem_start = di_mem_start + (width*canvas_height*6/4)*local_buf_num;
#else
            local_buf_num = di_mem_size/((width*canvas_height*5/4)+dp_buf_size);
            dp_mem_start = di_mem_start + (width*canvas_height*5/4)*local_buf_num;
#endif
        }
        else
#endif
#ifdef NEW_DI_V1
        local_buf_num = di_mem_size/(width*canvas_height*6/4);
#else
        local_buf_num = di_mem_size/(width*canvas_height*5/4);
#endif
        local_buf_num_available = local_buf_num;
        if(local_buf_num > MAX_LOCAL_BUF_NUM){
            local_buf_num = MAX_LOCAL_BUF_NUM;
        }
        if(local_buf_num >= 8){
            new_keep_last_frame_enable = 1;
        }
        else{
            new_keep_last_frame_enable = 0;
        }
    }

    same_w_r_canvas_count = 0;
    same_field_top_count = 0;
    same_field_bot_count = 0;

    queue_init(local_buf_num);
    local_buf_num_valid = local_buf_num;
    for(i=0; i<local_buf_num; i++){
        di_buf_t* di_buf = &(di_buf_local[i]);
        int ii = USED_LOCAL_BUF_MAX;
        if((used_post_buf_index != -1)&&(new_keep_last_frame_enable)){
            for(ii=0; ii<USED_LOCAL_BUF_MAX; ii++){
                //printk("%s %d %d\n", __func__, di_buf->index, used_local_buf_index[ii]);
                if(i == used_local_buf_index[ii]){
                    di_print("%s skip %d\n", __func__, i);
                    break;
                }
            }
            if(ii<USED_LOCAL_BUF_MAX){
                local_buf_num_valid--;
            }
        }

        if(ii>=USED_LOCAL_BUF_MAX){
            memset(di_buf, sizeof(di_buf_t), 0);
            di_buf->type = VFRAME_TYPE_LOCAL;
            di_buf->pre_ref_count = 0;
            di_buf->post_ref_count = 0;
            if(prog_flag){
                di_buf->nr_adr = di_mem_start + (width*canvas_height*2)*i;
#ifndef DI_USE_FIXED_CANVAS_IDX
    	        di_buf->nr_canvas_idx = DEINTERLACE_CANVAS_BASE_INDEX+i;
#endif
	              //canvas_config(di_buf->nr_canvas_idx, di_buf->nr_adr, width*2, canvas_height, 0, 0);
                di_buf->canvas_config_flag = 1;
                di_buf->canvas_config_size = (width<<16)|canvas_height;
#ifdef D2D3_SUPPORT
                if(d2d3_enable){
                    di_buf->dp_buf_adr = dp_mem_start + (i*dp_buf_size);
                    di_buf->dp_buf_size = dp_buf_size;
                }
#endif
            }
            else{
#ifdef NEW_DI_V1
                di_buf->nr_adr = di_mem_start + (width*canvas_height*6/4)*i;
#else
                di_buf->nr_adr = di_mem_start + (width*canvas_height*5/4)*i;
#endif
#ifndef DI_USE_FIXED_CANVAS_IDX
    	        di_buf->nr_canvas_idx = DEINTERLACE_CANVAS_BASE_INDEX+i*2;
#endif
	              //canvas_config(di_buf->nr_canvas_idx, di_buf->nr_adr, width*2, canvas_height/2, 0, 0);
#ifdef NEW_DI_V1
                di_buf->mtn_adr = di_mem_start + (width*canvas_height*6/4)*i + (width*canvas_height);
                di_buf->cnt_adr = di_mem_start + (width*canvas_height*6/4)*i + (width*canvas_height)*5/4;
#else
                di_buf->mtn_adr = di_mem_start + (width*canvas_height*5/4)*i + (width*canvas_height);
#endif
#ifndef DI_USE_FIXED_CANVAS_IDX
    	        di_buf->mtn_canvas_idx = DEINTERLACE_CANVAS_BASE_INDEX+i*2+1;
#endif
	              //canvas_config(di_buf->mtn_canvas_idx, di_buf->mtn_adr, width/2, canvas_height/2, 0, 0);
                di_buf->canvas_config_flag = 2;
                di_buf->canvas_config_size = (width<<16)|canvas_height;
#ifdef D2D3_SUPPORT
                if(d2d3_enable){
                    di_buf->dp_buf_adr = dp_mem_start + (i*dp_buf_size);
                    di_buf->dp_buf_size = dp_buf_size;
                }
#endif
            }
            di_buf->index = i;
            di_buf->vframe = &(vframe_local[i]);
            di_buf->vframe->private_data = di_buf;
            di_buf->vframe->canvas0Addr = di_buf->nr_canvas_idx;
            di_buf->vframe->canvas1Addr = di_buf->nr_canvas_idx;
            di_buf->queue_index = -1;
            di_buf->invert_top_bot_flag = 0;
            queue_in(di_buf, QUEUE_LOCAL_FREE);
        }
    }

    for(i=0; i<MAX_IN_BUF_NUM; i++){
        di_buf_t* di_buf = &(di_buf_in[i]);
        if(di_buf){
            memset(di_buf, sizeof(di_buf_t), 0);
            di_buf->type = VFRAME_TYPE_IN;
            di_buf->pre_ref_count = 0;
            di_buf->post_ref_count = 0;
            di_buf->vframe = &(vframe_in_dup[i]);
            di_buf->vframe->private_data = di_buf;
            di_buf->index = i;
            di_buf->queue_index = -1;
            di_buf->invert_top_bot_flag = 0;
            queue_in(di_buf, QUEUE_IN_FREE);
        }
    }

    for(i=0; i<MAX_POST_BUF_NUM; i++){
    	di_buf_t* di_buf = &(di_buf_post[i]);
	if(di_buf){
            if( i != used_post_buf_index ){
                memset(di_buf, sizeof(di_buf_t), 0);
                di_buf->type = VFRAME_TYPE_POST;
                di_buf->index = i;
                di_buf->vframe = &(vframe_post[i]);
                di_buf->vframe->private_data = di_buf;
                di_buf->queue_index = -1;
                di_buf->invert_top_bot_flag = 0;
                queue_in(di_buf, QUEUE_POST_FREE);
            }
        }
    }

//    printk("%s: version %s, prog_proc_type %d, buf start 0x%x, size 0x%x, buf(w%d,h%d) cur num %d (available %d,  total %d) \n",
//        __func__, version_s, di_pre_stru.prog_proc_type, (unsigned int)di_mem_start,
//        (unsigned int)di_mem_size, width, canvas_height, local_buf_num_valid, local_buf_num_available, local_buf_num);

    return 0;
}

static void di_uninit_buf(void)
{
    di_buf_t *p = NULL;//, *ptmp;
    int i, ii=0;
    int itmp;

    //vframe_t* cur_vf = get_cur_dispbuf();
    if(!queue_empty(QUEUE_DISPLAY)){
        for(i=0; i<USED_LOCAL_BUF_MAX; i++){
    	    used_local_buf_index[i] = -1;
        }
        used_post_buf_index = -1;
    }

    queue_for_each_entry(p, ptmp, QUEUE_DISPLAY, list) {
        if(p->di_buf[0]->type!=VFRAME_TYPE_IN && p->process_fun_index != PROCESS_FUN_NULL){
#if 1
	    if(p->index == di_post_stru.cur_disp_index){
	        used_post_buf_index = p->index;
	        for(i=0; i<USED_LOCAL_BUF_MAX; i++){
                    if(p->di_buf_dup_p[i] != NULL){
	        	used_local_buf_index[ii] = p->di_buf_dup_p[i]->index;
			/* prepare for recycle the keep buffer*/
			p->di_buf_dup_p[i]->pre_ref_count = 0;
			p->di_buf_dup_p[i]->post_ref_count = 0;
                        if(is_in_queue(p->di_buf_dup_p[i],p->di_buf_dup_p[i]->queue_index))
                            queue_out(p->di_buf_dup_p[i]);
	        	ii++;
                        if(p->di_buf_dup_p[i]->di_wr_linked_buf)
                            used_local_buf_index[ii] = p->di_buf_dup_p[i]->di_wr_linked_buf->index;
                    }
	        }
		queue_out(p);

#else
	        if(cur_vf->private_data == p){
	            used_post_buf_index = p->index;
	            for(i=0; i<USED_LOCAL_BUF_MAX; i++){
                        if(p->di_buf_dup_p[i] != NULL)
                        {
	        	    used_local_buf_index[ii] = p->di_buf_dup_p[i]->index;
	        	    ii++;
	        	    if(p->di_buf_dup_p[i]->di_wr_linked_buf)
	        	    	used_local_buf_index[ii] = p->di_buf_dup_p[i]->di_wr_linked_buf->index;
                        }
	        }
#endif
	        break;
	    }
	}
        if(ii>=USED_LOCAL_BUF_MAX){
	    break;
	}
    }
    if(used_post_buf_index != -1){
        printk("%s keep cur di_buf %d (%d %d %d)\n",
               __func__, used_post_buf_index, used_local_buf_index[0],
                     used_local_buf_index[1],used_local_buf_index[2]);
    }
#ifdef USE_LIST
    list_for_each_entry_safe(p, ptmp, &local_free_list_head, list) {
        list_del(&p->list);
    }
    list_for_each_entry_safe(p, ptmp, &in_free_list_head, list) {
        list_del(&p->list);
    }
    list_for_each_entry_safe(p, ptmp, &pre_ready_list_head, list) {
        list_del(&p->list);
    }
    list_for_each_entry_safe(p, ptmp, &recycle_list_head, list) {
        list_del(&p->list);
    }
    list_for_each_entry_safe(p, ptmp, &post_free_list_head, list) {
        list_del(&p->list);
    }
    list_for_each_entry_safe(p, ptmp, &post_ready_list_head, list) {
        list_del(&p->list);
    }
    list_for_each_entry_safe(p, ptmp, &display_list_head, list) {
        list_del(&p->list);
    }
#else
    queue_init(0);
#endif
    for(i=0; i<MAX_IN_BUF_NUM; i++){
	vframe_in[i]=NULL;
    }

    di_pre_stru.pre_de_process_done = 0;
    di_pre_stru.pre_de_busy = 0;

}
/*
static void di_clean_in_buf(void)
{
    di_buf_t *di_buf = NULL;//, *ptmp;
    int itmp;
    int i;
    //di_printk_flag = 1;
    frame_count = 0;
    //disp_frame_count = 0; //do not set it to 0 here to make start_frame_hold_count not work when "keep last frame" is enabled
    while(!queue_empty(QUEUE_POST_READY)){
        di_buf = get_di_buf_head(QUEUE_POST_READY);
        recycle_vframe_type_post(di_buf);
#ifdef DI_DEBUG
        recycle_vframe_type_post_print(di_buf, __func__, __LINE__);
#endif
    }

    if(di_pre_stru.di_mem_buf_dup_p){
        di_pre_stru.di_mem_buf_dup_p->pre_ref_count = 0;
        di_pre_stru.di_mem_buf_dup_p = NULL;
    }
    if(di_pre_stru.di_chan2_buf_dup_p){
        di_pre_stru.di_chan2_buf_dup_p->pre_ref_count = 0;
        di_pre_stru.di_chan2_buf_dup_p = NULL;
    }

    di_pre_stru.process_count = 0;
    if(di_pre_stru.di_inp_buf){
        if(vframe_in[di_pre_stru.di_inp_buf->index]){
            vframe_in[di_pre_stru.di_inp_buf->index] = NULL;
        }
        di_pre_stru.di_inp_buf->invert_top_bot_flag = 0;
        queue_in(di_pre_stru.di_inp_buf, QUEUE_IN_FREE);
        di_pre_stru.di_inp_buf = NULL;
    }

    if(di_pre_stru.di_wr_buf){
        di_pre_stru.di_wr_buf->pre_ref_count = 0;
        queue_in(di_pre_stru.di_wr_buf, QUEUE_RECYCLE);
        di_pre_stru.di_wr_buf = NULL;
    }

    queue_for_each_entry(di_buf, ptmp, QUEUE_PRE_READY, list) {
        queue_out(di_buf);
        queue_in(di_buf, QUEUE_RECYCLE);
    }

    queue_for_each_entry(di_buf, ptmp, QUEUE_RECYCLE, list) {
        if(di_buf->type == VFRAME_TYPE_IN){
            di_buf->pre_ref_count = 0;
            queue_out(di_buf);
            if(vframe_in[di_buf->index]){
                vframe_in[di_buf->index] = NULL;
            }
            di_buf->invert_top_bot_flag = 0;
            queue_in(di_buf, QUEUE_IN_FREE);
        }
        else{
            if((di_buf->pre_ref_count == 0)&&(di_buf->post_ref_count == 0)){
                queue_out(di_buf);
                di_buf->invert_top_bot_flag = 0;
                queue_in(di_buf, QUEUE_LOCAL_FREE);
            }
        }
    }
    di_pre_stru.cur_width = 0;
    di_pre_stru.cur_height= 0;
    di_pre_stru.cur_prog_flag = 0;
    di_pre_stru.cur_inp_type = 0;
    di_pre_stru.cur_source_type = 0;
    di_pre_stru.cur_sig_fmt = 0;

    di_pre_stru.pre_de_process_done = 0;
    di_pre_stru.pre_de_busy = 0;

    for(i=0; i<MAX_IN_BUF_NUM; i++){
			vframe_in[i]=NULL;
		}

}
*/
static void log_buffer_state(unsigned char* tag)
{
    if(di_log_flag&DI_LOG_BUFFER_STATE){
        di_buf_t *p = NULL;//, *ptmp;
        int itmp;
        int in_free = 0;
        int local_free = 0;
        int pre_ready = 0;
        int post_free = 0;
        int post_ready = 0;
        int post_ready_ext = 0;
        int display = 0;
        int display_ext = 0;
        int recycle = 0;
        int di_inp = 0;
        int di_wr = 0;
        ulong fiq_flag=0, irq_flag2=0;
        di_lock_irqfiq_save(irq_flag2, fiq_flag);
        in_free = list_count(QUEUE_IN_FREE);
        local_free = list_count(QUEUE_LOCAL_FREE);
        pre_ready = list_count(QUEUE_PRE_READY);
        post_free = list_count(QUEUE_POST_FREE);
        post_ready = list_count(QUEUE_POST_READY);
        queue_for_each_entry(p, ptmp, QUEUE_POST_READY, list) {
            if(p->di_buf[0]){
                post_ready_ext++;
            }
            if(p->di_buf[1]){
                post_ready_ext++;
            }
        }
        queue_for_each_entry(p, ptmp, QUEUE_DISPLAY, list) {
            display++;
            if(p->di_buf[0]){
                display_ext++;
            }
            if(p->di_buf[1]){
                display_ext++;
            }
        }
        recycle = list_count(QUEUE_RECYCLE);

        if(di_pre_stru.di_inp_buf)
            di_inp++;
        if(di_pre_stru.di_wr_buf)
            di_wr++;

				if(buf_state_log_threshold == 0){
					  buf_state_log_start = 0;
				}
				else if(post_ready < buf_state_log_threshold){
			  		buf_state_log_start = 1;
				}
				if(buf_state_log_start){
	        di_print("[%s]i %d, i_f %d/%d, l_f %d/%d, pre_r %d, post_f %d/%d, post_r (%d:%d), disp (%d:%d),rec %d, di_i %d, di_w %d\r\n",
	            tag,
	            provider_vframe_level,
	            in_free,MAX_IN_BUF_NUM,
	            local_free, local_buf_num,
	            pre_ready,
	            post_free, MAX_POST_BUF_NUM,
	            post_ready, post_ready_ext,
	            display, display_ext,
	            recycle,
	            di_inp, di_wr
	            );
				}
        di_unlock_irqfiq_restore(irq_flag2, fiq_flag);

    }


}

static void dump_di_buf(di_buf_t* di_buf)
{
    printk("di_buf %p vframe %p:\n", di_buf, di_buf->vframe);
    printk("index %d, post_proc_flag %d, new_format_flag %d, type %d, seq %d, pre_ref_count %d, post_ref_count %d, queue_index %d pulldown_mode %d process_fun_index %d\n",
        di_buf->index, di_buf->post_proc_flag, di_buf->new_format_flag, di_buf->type, di_buf->seq, di_buf->pre_ref_count,
        di_buf->post_ref_count, di_buf->queue_index, di_buf->pulldown_mode, di_buf->process_fun_index);
    printk("di_buf: %p, %p, di_buf_dup_p: %p, %p, %p, %p, %p\n",
        di_buf->di_buf[0],di_buf->di_buf[1],di_buf->di_buf_dup_p[0],di_buf->di_buf_dup_p[1],di_buf->di_buf_dup_p[2],di_buf->di_buf_dup_p[3],di_buf->di_buf_dup_p[4]);
    printk("nr_adr 0x%x, nr_canvas_idx 0x%x, mtn_adr 0x%x, mtn_canvas_idx 0x%x",
        di_buf->nr_adr, di_buf->nr_canvas_idx, di_buf->mtn_adr, di_buf->mtn_canvas_idx);
#ifdef NEW_DI_V1
    printk("cnt_adr 0x%x, cnt_canvas_idx 0x%x\n",
        di_buf->cnt_adr, di_buf->cnt_canvas_idx);
#endif
}

static void dump_pool(int index)
{

    int j;
    queue_t* q = &queue[index];
    printk("queue[%d]: in_idx %d, out_idx %d, num %d, type %d\n", index, q->in_idx, q->out_idx, q->num, q->type);
    for(j=0; j<MAX_QUEUE_POOL_SIZE; j++){
        printk("%x ", q->pool[j]);
        if(((j+1)%16)==0){
            printk("\n");
        }
    }
    printk("\n");
}

static void dump_vframe(vframe_t* vf)
{
    printk("vframe %p:\n", vf);
    printk("index %d, type 0x%x, type_backup 0x%x, blend_mode %d, duration %d, duration_pulldown %d, pts %d, flag 0x%x\n",
            vf->index, vf->type, vf->type_backup, vf->blend_mode, vf->duration, vf->duration_pulldown, vf->pts, vf->flag);
    printk("canvas0Addr 0x%x, canvas1Addr 0x%x, bufWidth %d, width %d, height %d, ratio_control 0x%x, orientation 0x%x, source_type %d, phase %d, soruce_mode %d, sig_fmt %d\n",
            vf->canvas0Addr, vf->canvas1Addr, vf->bufWidth, vf->width, vf->height, vf->ratio_control, vf->orientation,
            vf->source_type, vf->phase, vf->source_mode, vf->sig_fmt);
#ifdef CONFIG_POST_PROCESS_MANAGER_3D_PROCESS
    printk("trans_fmt 0x%x, left_eye(%d %d %d %d), right_eye(%d %d %d %d)\n",
            vf->trans_fmt, vf->left_eye.start_x, vf->left_eye.start_y, vf->left_eye.width, vf->left_eye.height,
            vf->right_eye.start_x, vf->right_eye.start_y, vf->right_eye.width, vf->right_eye.height);
#endif
    printk("mode_3d_enable %d, orientation %u, early_process_fun 0x%x, process_fun 0x%x, private_data %p\n",
            vf->mode_3d_enable, vf->orientation, (unsigned int)vf->early_process_fun, (unsigned int)vf->process_fun, vf->private_data);
    printk("pixel_ratio %d list %p\n",
            vf->pixel_ratio, &vf->list);

}

static void print_di_buf(di_buf_t* di_buf, int format)
{
    if(format == 1){
        if(di_buf){
            printk("    +index %d, 0x%p, type %d, vframetype 0x%x, trans_fmt %u\n", di_buf->index, di_buf, di_buf->type,
                        di_buf->vframe->type,
#ifdef CONFIG_POST_PROCESS_MANAGER_3D_PROCESS
                        di_buf->vframe->trans_fmt
#else
                        0
#endif
                        );
            if(di_buf->di_wr_linked_buf){
      	        printk("  linked  +index %d, 0x%p, type %d\n",
                        di_buf->di_wr_linked_buf->index, di_buf->di_wr_linked_buf,
                        di_buf->di_wr_linked_buf->type);
            }
        }


    }
    else if(format == 2){
        if(di_buf){
            printk("index %d, 0x%p(vframe 0x%p), type %d, vframetype 0x%x, trans_fmt %u,duration %d pts %d\n",
                    di_buf->index, di_buf, di_buf->vframe, di_buf->type, di_buf->vframe->type,
#ifdef CONFIG_POST_PROCESS_MANAGER_3D_PROCESS
                    di_buf->vframe->trans_fmt,
#else
                    0,
#endif
                    di_buf->vframe->duration, di_buf->vframe->pts);
            if(di_buf->di_wr_linked_buf){
       	        printk("linked index %d, 0x%p, type %d\n",
                        di_buf->di_wr_linked_buf->index, di_buf->di_wr_linked_buf,
                        di_buf->di_wr_linked_buf->type);
            }
        }


    }

}

static void dump_state(void)
{
    di_buf_t *p = NULL;//, *ptmp;
    int itmp;
    int i;
    dump_state_flag = 1;
    printk("version %s, provider vframe level %d, init_flag %d, is_bypass %d, receiver_is_amvideo %d\n",
        version_s, provider_vframe_level, init_flag, is_bypass(NULL), receiver_is_amvideo);
    printk("recovery_flag = %d, recovery_log_reason=%d, recovery_log_queue_idx=%d, recovery_log_di_buf=0x%p\n",
        recovery_flag, recovery_log_reason, recovery_log_queue_idx, recovery_log_di_buf);

    printk("new_keep_last_frame_enable %d, used_post_buf_index %d(0x%p), used_local_buf_index:\n", new_keep_last_frame_enable,
        used_post_buf_index, (used_post_buf_index==-1)?NULL:&(di_buf_post[used_post_buf_index]));
		for(i=0; i<USED_LOCAL_BUF_MAX; i++){
		    int tmp = used_local_buf_index[i];
    	    printk("%d(0x%p) ",tmp, (tmp==-1)?NULL:&(di_buf_local[tmp]));
		}

    printk("\nin_free_list (max %d):\n", MAX_IN_BUF_NUM);
    queue_for_each_entry(p, ptmp, QUEUE_IN_FREE, list) {
        printk("index %2d, 0x%x, type %d\n", p->index, (unsigned int)p, p->type);
    }
    printk("local_free_list (max %d):\n", local_buf_num);
    queue_for_each_entry(p, ptmp, QUEUE_LOCAL_FREE, list) {
        printk("index %2d, 0x%p, type %d\n", p->index, p, p->type);
    }
    printk("pre_ready_list:\n");
    queue_for_each_entry(p, ptmp, QUEUE_PRE_READY, list) {
        print_di_buf(p, 2);
    }
    printk("post_free_list (max %d):\n", MAX_POST_BUF_NUM);
    queue_for_each_entry(p, ptmp, QUEUE_POST_FREE, list) {
        printk("index %2d, 0x%p, type %d, vframetype 0x%x\n", p->index, p, p->type ,p->vframe->type);
    }
    printk("post_ready_list:\n");
    queue_for_each_entry(p, ptmp, QUEUE_POST_READY, list) {
        print_di_buf(p, 2);
        print_di_buf(p->di_buf[0], 1);
        print_di_buf(p->di_buf[1], 1);
    }
    printk("display_list:\n");
    queue_for_each_entry(p, ptmp, QUEUE_DISPLAY, list) {
        print_di_buf(p, 2);
        print_di_buf(p->di_buf[0], 1);
        print_di_buf(p->di_buf[1], 1);
    }
    printk("recycle_list:\n");
    queue_for_each_entry(p, ptmp, QUEUE_RECYCLE, list) {
        printk("index %2d, 0x%p, type %d, vframetype 0x%x pre_ref_count %d post_ref_count %d\n", p->index, p, p->type, p->vframe->type, p->pre_ref_count, p->post_ref_count);
        if(p->di_wr_linked_buf){
       	    printk("linked index %2d, 0x%p, type %d pre_ref_count %d post_ref_count %d\n",
                    p->di_wr_linked_buf->index, p->di_wr_linked_buf, p->di_wr_linked_buf->type,
                    p->di_wr_linked_buf->pre_ref_count, p->di_wr_linked_buf->post_ref_count);
        }
    }
    if(di_pre_stru.di_inp_buf)
        printk("di_inp_buf:index %d, 0x%p, type %d\n", di_pre_stru.di_inp_buf->index, di_pre_stru.di_inp_buf, di_pre_stru.di_inp_buf->type);
    else
        printk("di_inp_buf: NULL\n");
    if(di_pre_stru.di_wr_buf)
        printk("di_wr_buf:index %d, 0x%p, type %d\n", di_pre_stru.di_wr_buf->index, di_pre_stru.di_wr_buf, di_pre_stru.di_wr_buf->type);
    else
        printk("di_wr_buf: NULL\n");
    dump_di_pre_stru();
    printk("vframe_in[]:");
    for(i=0; i<MAX_IN_BUF_NUM; i++){
			  printk("0x%p ",vframe_in[i]);
		}
		printk("\n");
    printk("vf_peek()=>0x%p\n",vf_peek(VFM_NAME));
    printk("di_process_cnt = %d, video_peek_cnt = %d, force_trig_cnt = %d\n", di_process_cnt, video_peek_cnt, force_trig_cnt);
    dump_state_flag = 0;

}

static unsigned char check_di_buf(di_buf_t* di_buf, int reason)
{
        int error = 0;
        if(di_buf == NULL){
            printk("%s: Error %d, di_buf is NULL\n", __func__, reason);
            return 1;
        }
        else{
            if(di_buf->type == VFRAME_TYPE_IN){
                if(di_buf->vframe != &vframe_in_dup[di_buf->index]){
                    error = 1;
                }
            }
            else if(di_buf->type == VFRAME_TYPE_LOCAL){
                if(di_buf->vframe != &vframe_local[di_buf->index]){
                    error = 1;
                }
            }
            else if(di_buf->type == VFRAME_TYPE_POST){
                if(di_buf->vframe != &vframe_post[di_buf->index]){
                    error = 1;
                }
            }
            else{
                error = 1;
            }

            if(error){
                printk("%s: Error %d, di_buf wrong\n", __func__, reason);
                if(recovery_flag==0){
                    recovery_log_reason = reason;
                }
                recovery_flag++;
                dump_di_buf(di_buf);
                return 1;
            }
        }
        return 0;
}


/*
*  di pre process
*/
#ifdef NEW_DI_V1
static void config_di_cnt_mif(DI_SIM_MIF_t* di_cnt_mif, di_buf_t* di_buf)
{
 			if(di_buf){
        	di_cnt_mif->start_x 		= 0;
        	di_cnt_mif->end_x 			= di_buf->vframe->width - 1;
        	di_cnt_mif->start_y 		= 0;
        	di_cnt_mif->end_y 			= di_buf->vframe->height/2 - 1;
        	di_cnt_mif->canvas_num = di_buf->cnt_canvas_idx;
      }
}
#endif

static void config_di_wr_mif(DI_SIM_MIF_t* di_nrwr_mif, DI_SIM_MIF_t* di_mtnwr_mif, di_buf_t* di_buf, vframe_t* in_vframe)
{
    	di_nrwr_mif->canvas_num = di_buf->nr_canvas_idx;

    	di_nrwr_mif->start_x			= 0;
    	di_nrwr_mif->end_x 			= in_vframe->width - 1;
    	di_nrwr_mif->start_y			= 0;
 			if(di_pre_stru.prog_proc_type == 0){
    	    di_nrwr_mif->end_y 			= in_vframe->height/2 - 1;
    	}
    	else{
          di_nrwr_mif->end_y 			= in_vframe->height - 1;
      }

 			if(di_pre_stru.prog_proc_type == 0){
        	di_mtnwr_mif->start_x 		= 0;
        	di_mtnwr_mif->end_x 			= in_vframe->width - 1;
        	di_mtnwr_mif->start_y 		= 0;
        	di_mtnwr_mif->end_y 			= in_vframe->height/2 - 1;
        	di_mtnwr_mif->canvas_num = di_buf->mtn_canvas_idx;
      }
}

static void config_di_mif(DI_MIF_t* di_mif, di_buf_t*di_buf)
{
	    if(di_buf == NULL)
	        return;
	    di_mif->canvas0_addr0 = di_buf->vframe->canvas0Addr & 0xff;
	    di_mif->canvas0_addr1 = (di_buf->vframe->canvas0Addr>>8) & 0xff;
	    di_mif->canvas0_addr2 = (di_buf->vframe->canvas0Addr>>16) & 0xff;

		if ( di_buf->vframe->type & VIDTYPE_VIU_422 )
		{ //from vdin or local vframe
        if((!is_progressive(di_buf->vframe)) //interlace, from vdin or local vframe
			      ||(di_pre_stru.prog_proc_type)   //progressive(by frame), from vdin or local frame
			    ){
    			di_mif->video_mode = 0;
    			di_mif->set_separate_en = 0;
    			di_mif->src_field_mode = 0;
    			di_mif->output_field_num = 0;
    			di_mif->burst_size_y = 3;
    			di_mif->burst_size_cb = 0;
    			di_mif->burst_size_cr = 0;
    			di_mif->luma_x_start0 	= 0;
    			di_mif->luma_x_end0 		= di_buf->vframe->width - 1;
    			di_mif->luma_y_start0 	= 0;
    			if(di_pre_stru.prog_proc_type){
    			    di_mif->luma_y_end0 		= di_buf->vframe->height - 1;
    			}
    			else{
    			    di_mif->luma_y_end0 		= di_buf->vframe->height/2 - 1;
    			}
    			di_mif->chroma_x_start0 	= 0;
    			di_mif->chroma_x_end0 	= 0;
    			di_mif->chroma_y_start0 	= 0;
    			di_mif->chroma_y_end0 	= 0;

    	    di_mif->canvas0_addr0 = di_buf->vframe->canvas0Addr & 0xff;
    	    di_mif->canvas0_addr1 = (di_buf->vframe->canvas0Addr>>8) & 0xff;
    	    di_mif->canvas0_addr2 = (di_buf->vframe->canvas0Addr>>16) & 0xff;
    	  }
    	  else{
    	      //progressive (by field), from vdin only
            di_mif->video_mode = 0;
            di_mif->set_separate_en = 0;
            di_mif->src_field_mode = 1;
            di_mif->burst_size_y = 3;
            di_mif->burst_size_cb = 0;
            di_mif->burst_size_cr = 0;

            if(di_pre_stru.process_count>0){    //process top
    			    di_mif->output_field_num = 0;    									// top

        			di_mif->luma_x_start0 	= 0;
        			di_mif->luma_x_end0 		= di_buf->vframe->width - 1;
        			di_mif->luma_y_start0 	= 0;
        			di_mif->luma_y_end0 		= di_buf->vframe->height - 2;
        			di_mif->chroma_x_start0 	= 0;
        			di_mif->chroma_x_end0 	= di_buf->vframe->width/2 - 1;
        			di_mif->chroma_y_start0 	= 0;
        			di_mif->chroma_y_end0 	= di_buf->vframe->height/2 - 2;
            }
            else{        //process bot
        			di_mif->output_field_num = 1;    									// bottom

        			di_mif->luma_x_start0 	= 0;
        			di_mif->luma_x_end0 		= di_buf->vframe->width - 1;
        			di_mif->luma_y_start0 	= 1;
        			di_mif->luma_y_end0 		= di_buf->vframe->height - 1;
        			di_mif->chroma_x_start0 	= 0;
        			di_mif->chroma_x_end0 	= di_buf->vframe->width/2 - 1;
        			di_mif->chroma_y_start0 	= 1;
        			di_mif->chroma_y_end0 	= di_buf->vframe->height/2 - 1;
        	}
    	 }
		}
		else{
		    //from decoder
			di_mif->video_mode = 0;
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
      if ( di_buf->vframe->type & VIDTYPE_VIU_NV21 ){
			    di_mif->set_separate_en = 2;
			}
			else
#endif
			{
			    di_mif->set_separate_en = 1;
			}
			di_mif->burst_size_y = 3;
			di_mif->burst_size_cb = 1;
			di_mif->burst_size_cr = 1;

      if(is_progressive(di_buf->vframe)&&(di_pre_stru.prog_proc_type)){
			        di_mif->src_field_mode = 0;
    			    di_mif->output_field_num = 0;    									// top

        			di_mif->luma_x_start0 	= 0;
        			di_mif->luma_x_end0 		= di_buf->vframe->width - 1;
        			di_mif->luma_y_start0 	= 0;
        			di_mif->luma_y_end0 		= di_buf->vframe->height - 1;
        			di_mif->chroma_x_start0 	= 0;
        			di_mif->chroma_x_end0 	= di_buf->vframe->width/2 - 1;
        			di_mif->chroma_y_start0 	= 0;
        			di_mif->chroma_y_end0 	= di_buf->vframe->height/2 - 1;
      }
      else{
			    di_mif->src_field_mode = 1;
          if(((di_buf->vframe->type & VIDTYPE_TYPEMASK) == VIDTYPE_INTERLACE_TOP)
            ||(di_pre_stru.process_count>0)){
    			    di_mif->output_field_num = 0;    									// top

        			di_mif->luma_x_start0 	= 0;
        			di_mif->luma_x_end0 		= di_buf->vframe->width - 1;
        			di_mif->luma_y_start0 	= 0;
        			di_mif->luma_y_end0 		= di_buf->vframe->height - 2;
        			di_mif->chroma_x_start0 	= 0;
        			di_mif->chroma_x_end0 	= di_buf->vframe->width/2 - 1;
        			di_mif->chroma_y_start0 	= 0;
        			di_mif->chroma_y_end0 	= di_buf->vframe->height/2 - 2;
    			}
          else{
    			    di_mif->output_field_num = 1;    									// bottom

        			di_mif->luma_x_start0 	= 0;
        			di_mif->luma_x_end0 		= di_buf->vframe->width - 1;
        			di_mif->luma_y_start0 	= 1;
        			di_mif->luma_y_end0 		= di_buf->vframe->height - 1;
        			di_mif->chroma_x_start0 	= 0;
        			di_mif->chroma_x_end0 	= di_buf->vframe->width/2 - 1;
        			di_mif->chroma_y_start0 	= 1;
        			di_mif->chroma_y_end0 	= di_buf->vframe->height/2 - 1;
    			}
    	}
		}

}

static void pre_de_process(void)
{
  int chan2_field_num = 1;
#ifdef NEW_DI_V1
  int cont_rd = 1;
#endif
#ifdef DI_DEBUG
    di_print("%s: start\n", __func__);
#endif
    di_pre_stru.pre_de_busy = 1;
    di_pre_stru.pre_de_busy_timer_count = 0;

    config_di_mif(&di_pre_stru.di_inp_mif, di_pre_stru.di_inp_buf);
    //printk("set_separate_en=%d vframe->type %d\n", di_pre_stru.di_inp_mif.set_separate_en, di_pre_stru.di_inp_buf->vframe->type);
#ifdef DI_USE_FIXED_CANVAS_IDX
    if((di_pre_stru.di_mem_buf_dup_p!=NULL && di_pre_stru.di_mem_buf_dup_p!=di_pre_stru.di_inp_buf)){
        config_canvas_idx(di_pre_stru.di_mem_buf_dup_p, DI_PRE_MEM_NR_CANVAS_IDX, -1);
#ifdef NEW_DI_V1
        config_cnt_canvas_idx(di_pre_stru.di_mem_buf_dup_p, DI_CONTP2RD_CANVAS_IDX);
#endif
    }
    if(di_pre_stru.di_chan2_buf_dup_p!=NULL){
        config_canvas_idx(di_pre_stru.di_chan2_buf_dup_p, DI_PRE_CHAN2_NR_CANVAS_IDX, -1);
#ifdef NEW_DI_V1
        config_cnt_canvas_idx(di_pre_stru.di_chan2_buf_dup_p, DI_CONTPRD_CANVAS_IDX);
#endif
    }
    config_canvas_idx(di_pre_stru.di_wr_buf, DI_PRE_WR_NR_CANVAS_IDX, DI_PRE_WR_MTN_CANVAS_IDX);
#ifdef NEW_DI_V1
    config_cnt_canvas_idx(di_pre_stru.di_wr_buf, DI_CONTWR_CANVAS_IDX);
#endif
#endif
    config_di_mif(&di_pre_stru.di_mem_mif, di_pre_stru.di_mem_buf_dup_p);
    config_di_mif(&di_pre_stru.di_chan2_mif, di_pre_stru.di_chan2_buf_dup_p);
    config_di_wr_mif(&di_pre_stru.di_nrwr_mif, &di_pre_stru.di_mtnwr_mif,
        di_pre_stru.di_wr_buf, di_pre_stru.di_inp_buf->vframe);
#ifdef NEW_DI_V1
    config_di_cnt_mif(&di_pre_stru.di_contp2rd_mif, di_pre_stru.di_mem_buf_dup_p);
    config_di_cnt_mif(&di_pre_stru.di_contprd_mif, di_pre_stru.di_chan2_buf_dup_p);
    config_di_cnt_mif(&di_pre_stru.di_contwr_mif, di_pre_stru.di_wr_buf);
#endif

    if((di_pre_stru.di_chan2_buf_dup_p)&&
        ((di_pre_stru.di_chan2_buf_dup_p->vframe->type & VIDTYPE_TYPEMASK) == VIDTYPE_INTERLACE_TOP)){
            chan2_field_num = 0;
    }

    Wr(DI_PRE_SIZE,    di_pre_stru.di_nrwr_mif.end_x|(di_pre_stru.di_nrwr_mif.end_y << 16) );

    // set interrupt mask for pre module.
#ifdef NEW_DI_V1
  Wr(DI_INTR_CTRL, ((di_pre_stru.enable_mtnwr?1:0) << 16) |       // mask nrwr interrupt.
                ((di_pre_stru.enable_mtnwr?0:1) << 17) |       //  mtnwr interrupt.
                    (1 << 18) |       // mask diwr interrupt.
                    (1 << 19) |       // mask hist check interrupt.
					(1 << 20) |       // mask cont interrupt.
                     0xf );            // clean all pending interrupt bits.
#else
    Wr(DI_INTR_CTRL, (0 << 16) |       //  nrwr interrupt.
                ((di_pre_stru.enable_mtnwr?0:1) << 17) |       //  mtnwr interrupt.
                (1 << 18) |       //  diwr interrupt.
                (1 << 19) |       //  hist check interrupt.
                 0xf );            // clean all pending interrupt bits.
#endif
#if 1
		enable_di_mode_check_2(
		   	(pd_win_prop[0].win_start_x_r*di_pre_stru.di_inp_buf->vframe->width/WIN_SIZE_FACTOR),
        (pd_win_prop[0].win_end_x_r*di_pre_stru.di_inp_buf->vframe->width/WIN_SIZE_FACTOR)-1,
		   	(pd_win_prop[0].win_start_y_r*(di_pre_stru.di_inp_buf->vframe->height/2)/WIN_SIZE_FACTOR),
		   	(pd_win_prop[0].win_end_y_r*(di_pre_stru.di_inp_buf->vframe->height/2)/WIN_SIZE_FACTOR)-1,
		   	(pd_win_prop[1].win_start_x_r*di_pre_stru.di_inp_buf->vframe->width/WIN_SIZE_FACTOR),
        (pd_win_prop[1].win_end_x_r*di_pre_stru.di_inp_buf->vframe->width/WIN_SIZE_FACTOR)-1,
		   	(pd_win_prop[1].win_start_y_r*(di_pre_stru.di_inp_buf->vframe->height/2)/WIN_SIZE_FACTOR),
		   	(pd_win_prop[1].win_end_y_r*(di_pre_stru.di_inp_buf->vframe->height/2)/WIN_SIZE_FACTOR)-1,
		   	(pd_win_prop[2].win_start_x_r*di_pre_stru.di_inp_buf->vframe->width/WIN_SIZE_FACTOR),
        (pd_win_prop[2].win_end_x_r*di_pre_stru.di_inp_buf->vframe->width/WIN_SIZE_FACTOR)-1,
		   	(pd_win_prop[2].win_start_y_r*(di_pre_stru.di_inp_buf->vframe->height/2)/WIN_SIZE_FACTOR),
		   	(pd_win_prop[2].win_end_y_r*(di_pre_stru.di_inp_buf->vframe->height/2)/WIN_SIZE_FACTOR)-1,
		   	(pd_win_prop[3].win_start_x_r*di_pre_stru.di_inp_buf->vframe->width/WIN_SIZE_FACTOR),
        (pd_win_prop[3].win_end_x_r*di_pre_stru.di_inp_buf->vframe->width/WIN_SIZE_FACTOR)-1,
		   	(pd_win_prop[3].win_start_y_r*(di_pre_stru.di_inp_buf->vframe->height/2)/WIN_SIZE_FACTOR),
		   	(pd_win_prop[3].win_end_y_r*(di_pre_stru.di_inp_buf->vframe->height/2)/WIN_SIZE_FACTOR)-1,
		   	(pd_win_prop[4].win_start_x_r*di_pre_stru.di_inp_buf->vframe->width/WIN_SIZE_FACTOR),
        (pd_win_prop[4].win_end_x_r*di_pre_stru.di_inp_buf->vframe->width/WIN_SIZE_FACTOR)-1,
		   	(pd_win_prop[4].win_start_y_r*(di_pre_stru.di_inp_buf->vframe->height/2)/WIN_SIZE_FACTOR),
		   	(pd_win_prop[4].win_end_y_r*(di_pre_stru.di_inp_buf->vframe->height/2)/WIN_SIZE_FACTOR)-1
		   	);
#else
        enable_di_mode_check_2(
		    0, di_pre_stru.di_inp_buf->vframe->width-1, 0, di_pre_stru.di_inp_buf->vframe->height/2-1,						// window 0 ( start_x, end_x, start_y, end_y)
		   	0, di_pre_stru.di_inp_buf->vframe->width-1, 0, di_pre_stru.di_inp_buf->vframe->height/2-1,						// window 1 ( start_x, end_x, start_y, end_y)
		   	0, di_pre_stru.di_inp_buf->vframe->width-1, 0, di_pre_stru.di_inp_buf->vframe->height/2-1,						// window 2 ( start_x, end_x, start_y, end_y)
		   	0, di_pre_stru.di_inp_buf->vframe->width-1, 0, di_pre_stru.di_inp_buf->vframe->height/2-1,						// window 3 ( start_x, end_x, start_y, end_y)
		   	0, di_pre_stru.di_inp_buf->vframe->width-1, 0, di_pre_stru.di_inp_buf->vframe->height/2-1						// window 4 ( start_x, end_x, start_y, end_y)
		   	);
#endif
    //Wr(DI_PRE_CTRL, 0x3 << 30); // remove it for M6, can not disalbe it here

    enable_di_pre_aml (  &di_pre_stru.di_inp_mif,               // di_inp
               &di_pre_stru.di_mem_mif,               // di_mem
               &di_pre_stru.di_chan2_mif,               // chan2
               &di_pre_stru.di_nrwr_mif,               // nrwrite
               &di_pre_stru.di_mtnwr_mif,            // mtn write
#ifdef NEW_DI_V1
               &di_pre_stru.di_contp2rd_mif,
               &di_pre_stru.di_contprd_mif,
               &di_pre_stru.di_contwr_mif,
#endif
               1,                      // nr enable
               di_pre_stru.enable_mtnwr,                      // mtn enable
               di_pre_stru.enable_pulldown_check,                                 // pd32 check_en
               di_pre_stru.enable_pulldown_check,                                  // pd22 check_en
#if defined(CONFIG_ARCH_MESON)
			         1,                      											// hist check_en
#else
			         0,                      											// hist check_en
#endif
               chan2_field_num,                      //  field num for chan2. 1 bottom, 0 top.
               0,                      // pre viu link.
               pre_hold_line,                     //hold line.
               pre_urgent
             );
		Wr(DI_PRE_CTRL, Rd(DI_PRE_CTRL)|(0x3 << 30)); //add for M6, reset
#ifdef NEW_DI_V1
    if(get_new_mode_flag() == 1){
    if (di_pre_stru.cur_prog_flag == 1) {
		di_mtn_1_ctrl1 &= (~(1<<31)); // disable contwr
                di_mtn_1_ctrl1 &= (~(1<<29));//disable txt
		cont_rd = 0;
    } else {
        di_mtn_1_ctrl1 |= (1<<31); //enable contwr
        di_mtn_1_ctrl1 &= (~(1<<29));//disable txt
        cont_rd = 1;
	}
        if(di_pre_stru.field_count_for_cont >= 3){
	    di_mtn_1_ctrl1 &= (~(1<<30)); // enable contp2rd and contprd
            di_mtn_1_ctrl1 |= 1<<29;//enable txt
            Wr(DI_CLKG_CTRL, 0xfeff0000); //di enable nr clock gate
            Wr(DI_PRE_CTRL, Rd(DI_PRE_CTRL)|(cont_rd<<25));
        }
		    di_pre_stru.field_count_for_cont++;
    }
    else if(get_new_mode_flag()==0){
        di_mtn_1_ctrl1 &= (~(1<<31)); // disable contwr
    }
	Wr(DI_MTN_1_CTRL1, di_mtn_1_ctrl1);
	//Wr(DI_PRE_CTRL, Rd(DI_PRE_CTRL)|(1<<25));
    di_apply_reg_cfg(0);
#endif
#ifdef SUPPORT_MPEG_TO_VDIN
	    if(mpeg2vdin_flag){
		vdin_arg_t vdin_arg;
		vdin_v4l2_ops_t *vdin_ops = get_vdin_v4l2_ops();
		vdin_arg.cmd = VDIN_CMD_FORCE_GO_FIELD;
		if(vdin_ops->tvin_vdin_func)
		    vdin_ops->tvin_vdin_func(0,&vdin_arg);
	    }
#endif

}

static void pre_de_done_buf_clear(void)
{
    if(di_pre_stru.di_wr_buf){
        di_pre_stru.di_wr_buf->pre_ref_count = 0;
        di_pre_stru.di_wr_buf->post_ref_count = 0;
        queue_in(di_pre_stru.di_wr_buf, QUEUE_RECYCLE);
        di_pre_stru.di_wr_buf = NULL;

        if((di_pre_stru.prog_proc_type==2) && di_pre_stru.di_wr_buf->di_wr_linked_buf){
            queue_in(di_pre_stru.di_wr_buf->di_wr_linked_buf,QUEUE_RECYCLE);
            di_pre_stru.di_wr_buf->di_wr_linked_buf = NULL;
        }
    }
    if(di_pre_stru.di_inp_buf){
        if(di_pre_stru.di_mem_buf_dup_p == di_pre_stru.di_inp_buf){
            di_pre_stru.di_mem_buf_dup_p = NULL;
        }
        queue_in(di_pre_stru.di_inp_buf, QUEUE_RECYCLE);
        di_pre_stru.di_inp_buf = NULL;
    }
}

static void top_bot_config(di_buf_t* di_buf)
{
    vframe_t* vframe = di_buf->vframe;
    if(((invert_top_bot&0x1)!=0) && (!is_progressive(vframe))){
        if(di_buf->invert_top_bot_flag == 0){
            if((vframe->type & VIDTYPE_TYPEMASK) == VIDTYPE_INTERLACE_TOP){
                vframe->type&=(~VIDTYPE_TYPEMASK);
                vframe->type|=VIDTYPE_INTERLACE_BOTTOM;
            }
            else{
                vframe->type&=(~VIDTYPE_TYPEMASK);
                vframe->type|=VIDTYPE_INTERLACE_TOP;
            }
            di_buf->invert_top_bot_flag = 1;
        }
    }
}

static void pre_de_done_buf_config(void)
{
    ulong fiq_flag=0, irq_flag2=0;
    if(di_pre_stru.di_wr_buf){
        if(di_pre_stru.pre_throw_flag>0){
            di_pre_stru.di_wr_buf->throw_flag = 1;
            di_pre_stru.pre_throw_flag--;
        }
        else{
            di_pre_stru.di_wr_buf->throw_flag = 0;
        }
        read_pulldown_info(&(di_pre_stru.di_wr_buf->field_pd_info),
                            &(di_pre_stru.di_wr_buf->win_pd_info[0])
                            );
        read_mtn_info(di_pre_stru.di_wr_buf->mtn_info,reg_mtn_info);

        if(di_pre_stru.cur_prog_flag){
            if(di_pre_stru.prog_proc_type == 0){
                if((di_pre_stru.process_count>0)
                    &&(is_progressive(di_pre_stru.di_mem_buf_dup_p->vframe))){
                        // di_mem_buf_dup_p->vframe is from in_list, and it is top field
                    di_pre_stru.di_chan2_buf_dup_p = di_pre_stru.di_wr_buf;
#ifdef DI_DEBUG
                    di_print("%s: set di_chan2_buf_dup_p to di_wr_buf\n", __func__);
#endif
                }
                else{ // di_mem_buf_dup->vfrme is either local vframe, or bot field of vframe from in_list
                    di_pre_stru.di_mem_buf_dup_p->pre_ref_count = 0;
                    di_pre_stru.di_mem_buf_dup_p = di_pre_stru.di_chan2_buf_dup_p;
                    di_pre_stru.di_chan2_buf_dup_p = di_pre_stru.di_wr_buf;
#ifdef DI_DEBUG
                    di_print("%s: set di_mem_buf_dup_p to di_chan2_buf_dup_p; set di_chan2_buf_dup_p to di_wr_buf\n", __func__);
#endif
                }
            }
            else{
                di_pre_stru.di_mem_buf_dup_p->pre_ref_count = 0;
                /*recycle the progress throw buffer*/
                if(di_pre_stru.di_wr_buf->throw_flag){
               	    di_pre_stru.di_wr_buf->pre_ref_count = 0;
                    di_pre_stru.di_mem_buf_dup_p = NULL;
#ifdef DI_DEBUG
                    di_print("%s set throw %s[%d] pre_ref_count to 0.\n",__func__,vframe_type_name[di_pre_stru.di_wr_buf->type],di_pre_stru.di_wr_buf->index);
#endif
                } else {
                    di_pre_stru.di_mem_buf_dup_p = di_pre_stru.di_wr_buf;
                }
#ifdef DI_DEBUG
                di_print("%s: set di_mem_buf_dup_p to di_wr_buf\n", __func__);
#endif
            }

            di_pre_stru.di_wr_buf->seq = di_pre_stru.pre_ready_seq++;
            di_pre_stru.di_wr_buf->post_ref_count = 0;
	    di_pre_stru.di_wr_buf->left_right = di_pre_stru.left_right;
            if(di_pre_stru.source_change_flag){
                di_pre_stru.di_wr_buf->new_format_flag = 1;
                di_pre_stru.source_change_flag = 0;
            }
            else{
                di_pre_stru.di_wr_buf->new_format_flag = 0;
            }
            if(bypass_state == 1){
                di_pre_stru.di_wr_buf->new_format_flag = 1;
                bypass_state = 0;
//#ifdef DI_DEBUG
     		di_print("%s:bypass_state change to 0, is_bypass() %d trick_mode %d bypass_all %d\n", __func__, is_bypass(NULL), trick_mode, bypass_all);
//#endif
            }
#ifdef CONFIG_VSYNC_RDMA
	    if((di_debug_flag&0x10)==0){
		if(interlace_output_flag==0)
        	    enable_rdma(1);
            }
#endif
            queue_in(di_pre_stru.di_wr_buf, QUEUE_PRE_READY);
#ifdef DI_DEBUG
            di_print("%s: process_count %d, %s[%d] => pre_ready_list\n",
                __func__, di_pre_stru.process_count, vframe_type_name[di_pre_stru.di_wr_buf->type], di_pre_stru.di_wr_buf->index);
#endif
            di_pre_stru.di_wr_buf = NULL;
        }
        else{
            di_pre_stru.di_mem_buf_dup_p->pre_ref_count = 0;
            di_pre_stru.di_mem_buf_dup_p = NULL;
            if(di_pre_stru.di_chan2_buf_dup_p){
                di_pre_stru.di_mem_buf_dup_p = di_pre_stru.di_chan2_buf_dup_p;
#ifdef DI_DEBUG
                di_print("%s: set di_mem_buf_dup_p to di_chan2_buf_dup_p\n", __func__);
#endif
            }
            di_pre_stru.di_chan2_buf_dup_p = di_pre_stru.di_wr_buf;

            if(di_pre_stru.di_wr_buf->post_proc_flag == 2){
                //add dummy buf, will not be displayed
                if(!queue_empty(QUEUE_LOCAL_FREE)){
                    di_buf_t* di_buf_tmp;
                    di_buf_tmp = get_di_buf_head(QUEUE_LOCAL_FREE);
                    if(di_buf_tmp){
                        queue_out(di_buf_tmp);
                        di_buf_tmp->pre_ref_count = 0;
                        di_buf_tmp->post_ref_count = 0;
                        di_buf_tmp->post_proc_flag = 3;
                        di_buf_tmp->new_format_flag = 0;
                        queue_in(di_buf_tmp, QUEUE_PRE_READY);
                    }
#ifdef DI_DEBUG
                    di_print("%s: dummy %s[%d] => pre_ready_list\n",
                    __func__, vframe_type_name[di_buf_tmp->type], di_buf_tmp->index);
#endif
               }
            }
            di_pre_stru.di_wr_buf->seq = di_pre_stru.pre_ready_seq++;
	    di_pre_stru.di_wr_buf->left_right = di_pre_stru.left_right;
            di_pre_stru.di_wr_buf->post_ref_count = 0;
            if(di_pre_stru.source_change_flag){
                di_pre_stru.di_wr_buf->new_format_flag = 1;
                di_pre_stru.source_change_flag = 0;
            }
            else{
                di_pre_stru.di_wr_buf->new_format_flag = 0;
            }
            if(bypass_state == 1){
                di_pre_stru.di_wr_buf->new_format_flag = 1;
                bypass_state = 0;
//#ifdef DI_DEBUG
        						di_print("%s:bypass_state change to 0, is_bypass() %d trick_mode %d bypass_all %d\n", __func__, is_bypass(NULL), trick_mode, bypass_all);
//#endif
            }
#ifdef CONFIG_VSYNC_RDMA
			if((di_debug_flag&0x10)==0){
				if(interlace_output_flag==0)
                	enable_rdma(1);
            }
#endif
            //top_bot_config(di_pre_stru.di_wr_buf);
            queue_in(di_pre_stru.di_wr_buf, QUEUE_PRE_READY);

#ifdef DI_DEBUG
            di_print("%s: %s[%d] => pre_ready_list\n",
                __func__, vframe_type_name[di_pre_stru.di_wr_buf->type], di_pre_stru.di_wr_buf->index);
#endif
            di_pre_stru.di_wr_buf = NULL;

        }
    }

    if((di_pre_stru.process_count==0)&&(di_pre_stru.di_inp_buf)){
#ifdef DI_DEBUG
        di_print("%s: %s[%d] => recycle_list\n",
            __func__, vframe_type_name[di_pre_stru.di_inp_buf->type], di_pre_stru.di_inp_buf->index);
#endif
        di_lock_irqfiq_save(irq_flag2, fiq_flag);
        queue_in(di_pre_stru.di_inp_buf, QUEUE_RECYCLE);
        di_pre_stru.di_inp_buf = NULL;
        di_unlock_irqfiq_restore(irq_flag2, fiq_flag);

    }
}

#if defined(NEW_DI_TV)
/* add for di Reg re-init */
static enum vframe_source_type_e  vframe_source_type = VFRAME_SOURCE_TYPE_OTHERS;
static void di_set_para_by_tvinfo(vframe_t* vframe)
{
    if (vframe->source_type == vframe_source_type)
        return;
    pr_info("%s: tvinfo change, reset di Reg \n", __FUNCTION__);
    vframe_source_type = vframe->source_type;
    /* add for smooth skin */
    if (vframe_source_type != VFRAME_SOURCE_TYPE_OTHERS)
        nr_hfilt_en = 1;
    else
        nr_hfilt_en = 0;

    //if input is pal and ntsc
    if (vframe_source_type != VFRAME_SOURCE_TYPE_TUNER)
    {
        ei_ctrl0 =  (255 << 16) |     		// ei_filter.
                      (1 << 8) |        				// ei_threshold.
                      (0 << 2) |         				// ei bypass cf2.
                      (0 << 1);        				// ei bypass far1

        ei_ctrl1 =   (90 << 24) |      		// ei diff
                      (10 << 16) |       				// ei ang45
                      (15 << 8 ) |        				// ei peak.
                       45;             				// ei cross.

        ei_ctrl2 =    (10 << 24) |       		// close2
                      (10 << 16) |       				// close1
                      (10 << 8 ) |       				// far2
                       93;             				// far1
 #ifdef NEW_DI_V1
       ei_ctrl3 = 0x80000013;
#endif
        kdeint2 = 25;
	mtn_ctrl= 0xe228c440;
    #ifdef CONFIG_MACH_MESON2_7366M_REFE03
        if (vframe_source_type == VFRAME_SOURCE_TYPE_COMP)
            blend_ctrl = 0x15f00019;
        else
    #endif
		blend_ctrl=0x1f00019;
	pr_info("%s: tvinfo change, reset di Reg \n", __FUNCTION__);
    }
    else        //input is tuner
    {
        ei_ctrl0 =  (255 << 16) |     		// ei_filter.
                      (1 << 8) |        				// ei_threshold.
                      (0 << 2) |         				// ei bypass cf2.
                      (0 << 1);        				// ei bypass far1

        ei_ctrl1 =   ( 90 << 24) |      		// ei diff
                      (192 << 16) |       				// ei ang45
                      (15 << 8 ) |        				// ei peak.
                       128;             				// ei cross.

        ei_ctrl2 =    (10 << 24) |       		// close2
                      (255 << 16) |       				// close1
                      (10 << 8 ) |       				// far2
                       255;             				// far1
 #ifdef NEW_DI_V1
        ei_ctrl3 = 0x80000013;
#endif
	if(kdeint1==0x10){
              kdeint2 = 25;
		mtn_ctrl= 0xe228c440 ;
		blend_ctrl=0x1f00019;
	}
       else{
	       kdeint2 = 25;
        #ifdef CONFIG_MESON2_CHIP_C
            mtn_ctrl = 0xe228c440;
            blend_ctrl = 0x15f00019;
            mtn_ctrl1_shift = 0x00000055;
        #else
		mtn_ctrl= 0x0 ;
		blend_ctrl=0x19f00019;
        #endif
		pr_info("%s: tvinfo change, reset di Reg in tuner source \n", __FUNCTION__);
       }
    }

   	//Wr(DI_EI_CTRL0, ei_ctrl0);
   	//Wr(DI_EI_CTRL1, ei_ctrl1);
   	//Wr(DI_EI_CTRL2, ei_ctrl2);

}
#endif
static void recycle_vframe_type_pre(di_buf_t* di_buf)
{
    ulong fiq_flag=0, irq_flag2=0;
    di_lock_irqfiq_save(irq_flag2, fiq_flag);

    queue_in(di_buf, QUEUE_RECYCLE);

    di_unlock_irqfiq_restore(irq_flag2, fiq_flag);
}
/*
* it depend on local buffer queue type is 2
*/
static int peek_free_linked_buf(void)
{
    di_buf_t *p = NULL;
    int itmp, p_index = -2;

    if(list_count(QUEUE_LOCAL_FREE) < 2)
    	return -1;

    queue_for_each_entry(p, ptmp, QUEUE_LOCAL_FREE, list) {
        if(abs(p->index - p_index)==1){
            return min(p->index,p_index);
        }
        else{
            p_index = p->index;
        }
    }
    return -1;
}
/*
* it depend on local buffer queue type is 2
*/
static di_buf_t *get_free_linked_buf(int idx)
{
    di_buf_t *di_buf=NULL, *di_buf_linked=NULL;
    int pool_idx = 0, di_buf_idx = 0;

    queue_t *q = &(queue[QUEUE_LOCAL_FREE]);
    if(list_count(QUEUE_LOCAL_FREE)<2)
    	return NULL;
    if(q->pool[idx]!=0 && q->pool[idx+1]!=0){
    	pool_idx = ((q->pool[idx]>>8)&0xff)-1;
        di_buf_idx = q->pool[idx]&0xff;
        if(pool_idx < VFRAME_TYPE_NUM){
            if(di_buf_idx < di_buf_pool[pool_idx].size){
                di_buf = &(di_buf_pool[pool_idx].di_buf_ptr[di_buf_idx]);
                queue_out(di_buf);
            }
        }
        pool_idx = ((q->pool[idx+1]>>8)&0xff)-1;
        di_buf_idx = q->pool[idx+1]&0xff;
        if(pool_idx < VFRAME_TYPE_NUM){
            if(di_buf_idx < di_buf_pool[pool_idx].size){
                di_buf_linked = &(di_buf_pool[pool_idx].di_buf_ptr[di_buf_idx]);
                queue_out(di_buf_linked);
            }
        }
        di_buf->di_wr_linked_buf = di_buf_linked;

    }
    return di_buf;
}

static unsigned char pre_de_buf_config(void)
{
    di_buf_t *di_buf = NULL;
    vframe_t* vframe;
    int i, di_linked_buf_idx = -1;

    if((queue_empty(QUEUE_IN_FREE)&&(di_pre_stru.process_count==0))||
        queue_empty(QUEUE_LOCAL_FREE)){
        return 0;
    }

    if(is_bypass(NULL)){ //some provider has problem if receiver get all buffers of provider
        int in_buf_num = 0;
        for(i=0; i<MAX_IN_BUF_NUM; i++){
            if(vframe_in[i]!=NULL){
                in_buf_num++;
            }
        }
        if(in_buf_num>bypass_get_buf_threshold
#ifdef DET3D
            && (di_pre_stru.vframe_interleave_flag == 0)
#endif
            ){
            return 0;
        }
    }else if(di_pre_stru.prog_proc_type == 2){
    	di_linked_buf_idx = peek_free_linked_buf();
	if(di_linked_buf_idx == -1)
            return 0;
    }

    if(di_pre_stru.di_inp_buf_next){
        di_pre_stru.di_inp_buf = di_pre_stru.di_inp_buf_next;
        di_pre_stru.di_inp_buf_next = NULL;
#ifdef DI_DEBUG
        di_print("%s: di_inp_buf_next %s[%d] => di_inp_buf\n", __func__, vframe_type_name[di_pre_stru.di_inp_buf->type], di_pre_stru.di_inp_buf->index);
#endif
        if(di_pre_stru.di_mem_buf_dup_p == NULL){//use n
            di_pre_stru.di_mem_buf_dup_p = di_pre_stru.di_inp_buf;
#ifdef DI_DEBUG
            di_print("%s: set di_mem_buf_dup_p to be di_inp_buf\n", __func__);
#endif
        }
    }
    else if(di_pre_stru.process_count>0){
        /* previous is progressive top */
        di_pre_stru.process_count = 0;
    }
    else{
        //check if source change
#ifdef CHECK_VDIN_BUF_ERROR
#define WR_CANVAS_BIT                   0
#define WR_CANVAS_WID                   8

        vframe = vf_peek(VFM_NAME);

        if(vframe&&is_from_vdin(vframe)){
            if(vframe->canvas0Addr == READ_CBUS_REG_BITS((VDIN_WR_CTRL + 0), WR_CANVAS_BIT, WR_CANVAS_WID)){
                same_w_r_canvas_count++;
            }
        }
#endif

        vframe = vf_get(VFM_NAME);

        if(vframe == NULL){
            return 0;
        }
	if(vframe->width > 10000 || vframe->height > 10000 || di_pre_stru.bad_frame_throw_count > 0){
	    if(vframe->width > 10000 || vframe->height > 10000)
	        di_pre_stru.bad_frame_throw_count = 10;
	    di_pre_stru.bad_frame_throw_count--;
	    vf_put(vframe, VFM_NAME);
	    return 0;
	}

        if(((vframe->type & VIDTYPE_TYPEMASK) != VIDTYPE_PROGRESSIVE)&&(vframe->width==1920)&&(vframe->height==1088)){
            force_height = 1080 ;
         }
         else {
            force_height = 0 ;
         }

#ifdef CONFIG_POST_PROCESS_MANAGER_3D_PROCESS
        di_pre_stru.source_trans_fmt = vframe->trans_fmt;
	di_pre_stru.left_right = di_pre_stru.left_right?0:1;
#endif

        if(((invert_top_bot&0x2)!=0) && (!is_progressive(vframe))){
            if((vframe->type & VIDTYPE_TYPEMASK) == VIDTYPE_INTERLACE_TOP){
                vframe->type&=(~VIDTYPE_TYPEMASK);
                vframe->type|=VIDTYPE_INTERLACE_BOTTOM;
            }
            else{
                vframe->type&=(~VIDTYPE_TYPEMASK);
                vframe->type|=VIDTYPE_INTERLACE_TOP;
            }
        }

        if(force_width){
            vframe->width = force_width;
        }
        if(force_height){
            vframe->height = force_height;
        }

#ifdef DI_DEBUG
        di_print("%s: vf_get => 0x%p\n", __func__, vframe);
#endif
        provider_vframe_level--;
        di_buf = get_di_buf_head(QUEUE_IN_FREE);

        if(check_di_buf(di_buf, 10)){
            return 0;
        }

        if(di_log_flag&DI_LOG_VFRAME){
            dump_vframe(vframe);
        }
	#ifdef SUPPORT_MPEG_TO_VDIN
	if((!is_from_vdin(vframe))&&(vframe->sig_fmt == TVIN_SIG_FMT_NULL)&&mpeg2vdin_flag){
	    vdin_arg_t vdin_arg;
	    vdin_v4l2_ops_t *vdin_ops = get_vdin_v4l2_ops();
	    vdin_arg.cmd = VDIN_CMD_GET_HISTGRAM;
	    vdin_arg.private = (unsigned int)vframe;
	    if(vdin_ops->tvin_vdin_func)
	        vdin_ops->tvin_vdin_func(0,&vdin_arg);
	}
	#endif
        memcpy(di_buf->vframe, vframe, sizeof(vframe_t));

        di_buf->vframe->private_data = di_buf;
        vframe_in[di_buf->index] = vframe;
        di_buf->seq = di_pre_stru.in_seq;
        di_pre_stru.in_seq++;
        queue_out(di_buf);

        if(is_source_change(vframe)){ /* source change*/
            if(di_pre_stru.di_mem_buf_dup_p){
            	/*avoid only 2 i field then p field*/
            	if((di_pre_stru.cur_prog_flag == 0) && use_2_interlace_buff)
            	    di_pre_stru.di_mem_buf_dup_p->post_proc_flag = -1;
            	di_pre_stru.di_mem_buf_dup_p->pre_ref_count = 0;
                di_pre_stru.di_mem_buf_dup_p = NULL;

            }
            if(di_pre_stru.di_chan2_buf_dup_p){
            	/*avoid only 1 i field then p field*/
            	if((di_pre_stru.cur_prog_flag == 0) && use_2_interlace_buff)
            	    di_pre_stru.di_chan2_buf_dup_p->post_proc_flag = -1;
            	di_pre_stru.di_chan2_buf_dup_p->pre_ref_count = 0;
                di_pre_stru.di_chan2_buf_dup_p = NULL;
            }
//#ifdef DI_DEBUG
            printk("%s: source change: 0x%x/%d/%d/%d=>0x%x/%d/%d/%d\n", __func__,
                di_pre_stru.cur_inp_type, di_pre_stru.cur_width, di_pre_stru.cur_height, di_pre_stru.cur_source_type,
                di_buf->vframe->type, di_buf->vframe->width, di_buf->vframe->height, di_buf->vframe->source_type);
//#endif
            di_pre_stru.cur_width = di_buf->vframe->width;
            di_pre_stru.cur_height= di_buf->vframe->height;
            di_pre_stru.cur_prog_flag = is_progressive(di_buf->vframe);
            if(di_pre_stru.cur_prog_flag){
            	if((use_2_interlace_buff)&&!(prog_proc_config&0x10))
                    di_pre_stru.prog_proc_type = 2;
                else
              	    di_pre_stru.prog_proc_type = prog_proc_config&0x10;
            }
            else
            	di_pre_stru.prog_proc_type = 0;
            di_pre_stru.cur_inp_type = di_buf->vframe->type;
            di_pre_stru.cur_source_type = di_buf->vframe->source_type;
            di_pre_stru.cur_sig_fmt = di_buf->vframe->sig_fmt;
	    di_pre_stru.orientation = di_buf->vframe->orientation;
            di_pre_stru.source_change_flag = 1;
            di_pre_stru.same_field_source_flag = 0;
#if defined (NEW_DI_TV)
            di_set_para_by_tvinfo(vframe);
#endif
#ifdef SUPPORT_MPEG_TO_VDIN
	    if((!is_from_vdin(vframe))&&(vframe->sig_fmt == TVIN_SIG_FMT_NULL)&&
	    	(mpeg2vdin_en)){
		vdin_arg_t vdin_arg;
		vdin_v4l2_ops_t *vdin_ops = get_vdin_v4l2_ops();
		vdin_arg.cmd = VDIN_CMD_MPEGIN_START;
		vdin_arg.h_active = di_pre_stru.cur_width;
		vdin_arg.v_active = di_pre_stru.cur_height;
		if(vdin_ops->tvin_vdin_func){
		    vdin_ops->tvin_vdin_func(0,&vdin_arg);
		}
		mpeg2vdin_flag = 1;
	    }
#endif
#ifdef NEW_DI_V1
            di_pre_stru.field_count_for_cont = 0;
#endif
        }
        else{
            /* check if top/bot interleaved */
            if(di_pre_stru.cur_prog_flag == 0){
                if((di_pre_stru.cur_inp_type & VIDTYPE_TYPEMASK) ==
                    (di_buf->vframe->type & VIDTYPE_TYPEMASK)){
#ifdef CHECK_VDIN_BUF_ERROR
                    if ((di_buf->vframe->type & VIDTYPE_TYPEMASK) == VIDTYPE_INTERLACE_TOP)
                        same_field_top_count++;
                    else
                        same_field_bot_count++;
#endif
                    if(di_pre_stru.same_field_source_flag<same_field_source_flag_th){
                        /*some source's filed is top or bot always*/
                        di_pre_stru.same_field_source_flag++;

                    if(skip_wrong_field && is_from_vdin(di_buf->vframe)){
                        recycle_vframe_type_pre(di_buf);
                        return 0;
                    }
                }
            }
                else{
                    di_pre_stru.same_field_source_flag=0;
                }
            }
            di_pre_stru.cur_inp_type = di_buf->vframe->type;
        }

            if(is_bypass(di_buf->vframe)){
                // bypass progressive
                di_buf->seq = di_pre_stru.pre_ready_seq++;
                di_buf->post_ref_count = 0;
                if(di_pre_stru.source_change_flag){
                    di_buf->new_format_flag = 1;
                    di_pre_stru.source_change_flag = 0;
                }
                else{
                    di_buf->new_format_flag = 0;
                }

                if(bypass_state == 0){
	                if(di_pre_stru.di_mem_buf_dup_p){
	                    di_pre_stru.di_mem_buf_dup_p->pre_ref_count = 0;
	                    di_pre_stru.di_mem_buf_dup_p = NULL;
	                }
	                if(di_pre_stru.di_chan2_buf_dup_p){
	                    di_pre_stru.di_chan2_buf_dup_p->pre_ref_count = 0;
	                    di_pre_stru.di_chan2_buf_dup_p = NULL;
	                }

	    		if(di_pre_stru.di_wr_buf){
			    di_pre_stru.process_count = 0;
		            di_pre_stru.di_wr_buf->pre_ref_count = 0;
		            di_pre_stru.di_wr_buf->post_ref_count = 0;
                            recycle_vframe_type_pre(di_pre_stru.di_wr_buf);

#ifdef DI_DEBUG
		            di_print("%s: %s[%d] => recycle_list\n", __func__, vframe_type_name[di_pre_stru.di_wr_buf->type], di_pre_stru.di_wr_buf->index);
#endif
	                    di_pre_stru.di_wr_buf = NULL;
	                }

                        di_buf->new_format_flag = 1;
                        bypass_state = 1;
#ifdef CONFIG_VSYNC_RDMA
		        if((di_debug_flag&0x10)==0){
                            enable_rdma(0);
                        }
#endif
//#ifdef DI_DEBUG
        		di_print("%s:bypass_state change to 1, is_bypass() %d trick_mode %d bypass_all %d\n", __func__, is_bypass(NULL), trick_mode, bypass_all);
//#endif
                }

                top_bot_config(di_buf);
                queue_in(di_buf, QUEUE_PRE_READY);
                di_buf->post_proc_flag = 0;
#ifdef DI_DEBUG
                di_print("%s: %s[%d] => pre_ready_list\n", __func__, vframe_type_name[di_buf->type], di_buf->index);
#endif
                return 0;
            }
            else if(is_progressive(vframe)){
                if(is_handle_prog_frame_as_interlace(vframe)&&(is_progressive(vframe))){
                    di_buf_t* di_buf_tmp = NULL;
                    vframe_in[di_buf->index] = NULL;
                    di_buf->vframe->type&=(~VIDTYPE_TYPEMASK);
                    di_buf->vframe->type|=VIDTYPE_INTERLACE_TOP;
                    di_buf->vframe->duration >>= 1;
                    di_buf->post_proc_flag = 0;

                    di_buf_tmp = get_di_buf_head(QUEUE_IN_FREE);
                    di_buf_tmp->vframe->private_data = di_buf_tmp;
                    di_buf_tmp->seq = di_pre_stru.in_seq;
                    di_pre_stru.in_seq++;
                    queue_out(di_buf_tmp);
                    vframe_in[di_buf_tmp->index] = vframe;
                    memcpy(di_buf_tmp->vframe, vframe, sizeof(vframe_t));
                    di_pre_stru.di_inp_buf_next = di_buf_tmp;
                    di_buf_tmp->vframe->type &=(~VIDTYPE_TYPEMASK);
                    di_buf_tmp->vframe->type |=VIDTYPE_INTERLACE_BOTTOM;
                    di_buf_tmp->vframe->duration >>= 1;
                    di_buf_tmp->post_proc_flag = 0;

                    di_pre_stru.di_inp_buf = di_buf;
#ifdef DI_DEBUG
                    di_print("%s: %s[%d] => di_inp_buf; %s[%d] => di_inp_buf_next\n",
                        __func__, vframe_type_name[di_buf->type], di_buf->index,
                        vframe_type_name[di_buf_tmp->type], di_buf_tmp->index);
#endif

                    if(di_pre_stru.di_mem_buf_dup_p == NULL){// use n
                        di_pre_stru.di_mem_buf_dup_p = di_buf;
#ifdef DI_DEBUG
                        di_print("%s: set di_mem_buf_dup_p to be di_inp_buf\n", __func__);
#endif
                    }
                }
                else{
                    //n
                    di_buf->post_proc_flag = 0;
                    if(prog_proc_config & 0x40)
                    	di_buf->post_proc_flag = 1;
                    di_pre_stru.di_inp_buf = di_buf;
                    if(di_pre_stru.prog_proc_type == 0){
                        di_pre_stru.process_count = 1;
                    }
                    else{
                        di_pre_stru.process_count = 0;
                    }
#ifdef DI_DEBUG
                    di_print("%s: %s[%d] => di_inp_buf, process_count %d\n",
                        __func__, vframe_type_name[di_buf->type], di_buf->index, di_pre_stru.process_count);
#endif
                    if(di_pre_stru.di_mem_buf_dup_p == NULL){//use n
                        di_pre_stru.di_mem_buf_dup_p = di_buf;
#ifdef DI_DEBUG
                        di_print("%s: set di_mem_buf_dup_p to be di_inp_buf\n", __func__);
#endif
                    }
                }
            }
            else{
#ifdef NEW_DI_V1
            if(get_new_mode_flag() == 1){
                if(di_pre_stru.di_chan2_buf_dup_p == NULL){
                    di_pre_stru.field_count_for_cont = 0;
		    di_mtn_1_ctrl1 |= (1<<30); //ignore contp2rd and contprd

                }
            }
#endif
            //n
            di_buf->post_proc_flag = 1;
            di_pre_stru.di_inp_buf = di_buf;
#ifdef DI_DEBUG
            di_print("%s: %s[%d] => di_inp_buf\n", __func__, vframe_type_name[di_buf->type], di_buf->index);
#endif

            if(di_pre_stru.di_mem_buf_dup_p == NULL){// use n
                di_pre_stru.di_mem_buf_dup_p = di_buf;
#ifdef DI_DEBUG
                di_print("%s: set di_mem_buf_dup_p to be di_inp_buf\n", __func__);
#endif
            }
        }
     }

     /* di_wr_buf */
     if(di_pre_stru.prog_proc_type == 2){
     	 di_linked_buf_idx = peek_free_linked_buf();
     	 if(di_linked_buf_idx != -1)
             di_buf = get_free_linked_buf(di_linked_buf_idx);
         else
             di_buf = NULL;
	 if(di_buf == NULL){
	     //recycle_vframe_type_pre(di_pre_stru.di_inp_buf);
	     /*save for next process*/
	     di_pre_stru.di_inp_buf_next = di_pre_stru.di_inp_buf;
	     return 0;
	 }
	 di_buf->post_proc_flag = 0;
	 di_buf->di_wr_linked_buf->pre_ref_count = 0;
	 di_buf->di_wr_linked_buf->post_ref_count = 0;
	 di_buf->canvas_config_flag = 1;
     }else{
         di_buf= get_di_buf_head(QUEUE_LOCAL_FREE);
     	 if(check_di_buf(di_buf, 11)){
 	     recycle_vframe_type_pre(di_pre_stru.di_inp_buf);
             return 0;
     	 }
	 queue_out(di_buf);
	 di_buf->di_wr_linked_buf = NULL;
     }

     di_pre_stru.di_wr_buf = di_buf;
     di_pre_stru.di_wr_buf->pre_ref_count = 1;

#ifndef DI_USE_FIXED_CANVAS_IDX
     config_canvas(di_buf);
#endif

#ifdef DI_DEBUG
     di_print("%s: %s[%d] => di_wr_buf\n", __func__, vframe_type_name[di_buf->type], di_buf->index);
     if(di_buf->di_wr_linked_buf)
         di_print("%s: linked %s[%d] => di_wr_buf\n", __func__, vframe_type_name[di_buf->di_wr_linked_buf->type], di_buf->di_wr_linked_buf->index);
#endif

    memcpy(di_buf->vframe, di_pre_stru.di_inp_buf->vframe, sizeof(vframe_t));
    di_buf->vframe->private_data = di_buf;
    di_buf->vframe->canvas0Addr = di_buf->nr_canvas_idx;
    di_buf->vframe->canvas1Addr = di_buf->nr_canvas_idx;

    if(di_pre_stru.prog_proc_type){
        di_buf->vframe->type = VIDTYPE_PROGRESSIVE | VIDTYPE_VIU_422 | VIDTYPE_VIU_SINGLE_PLANE | VIDTYPE_VIU_FIELD;
        if(di_pre_stru.cur_inp_type & VIDTYPE_PRE_INTERLACE){
            di_buf->vframe->type |= VIDTYPE_PRE_INTERLACE;
        }
    }
    else{
        if(((di_pre_stru.di_inp_buf->vframe->type & VIDTYPE_TYPEMASK) == VIDTYPE_INTERLACE_TOP)
            ||(di_pre_stru.process_count>0)){
    	    di_buf->vframe->type = VIDTYPE_INTERLACE_TOP | VIDTYPE_VIU_422 | VIDTYPE_VIU_SINGLE_PLANE | VIDTYPE_VIU_FIELD;
    	}
        else{
    	    di_buf->vframe->type = VIDTYPE_INTERLACE_BOTTOM | VIDTYPE_VIU_422 | VIDTYPE_VIU_SINGLE_PLANE | VIDTYPE_VIU_FIELD;
    	}
    }

    /* */
    if(is_bypass_post()){
        if(bypass_post_state == 0){
            di_pre_stru.source_change_flag = 1;
        }
        bypass_post_state = 1;
    }
    else{
        if(bypass_post_state){
            di_pre_stru.source_change_flag = 1;
        }
        bypass_post_state = 0;
    }

    //if(is_progressive(di_pre_stru.di_inp_buf->vframe)){
    if(di_pre_stru.di_inp_buf->post_proc_flag==0){
        di_pre_stru.enable_mtnwr = 0;
        di_pre_stru.enable_pulldown_check = 0;
        di_buf->post_proc_flag = 0;
    }
    else if(bypass_post_state){
        di_pre_stru.enable_mtnwr = 0;
        di_pre_stru.enable_pulldown_check = 0;
        di_buf->post_proc_flag = 0;
    }
    else{
        if(di_pre_stru.di_chan2_buf_dup_p == NULL){
            di_pre_stru.enable_mtnwr = 0;
            di_pre_stru.enable_pulldown_check = 0;
            di_buf->post_proc_flag = 2;
        }
        else{
            di_pre_stru.enable_mtnwr = 1;
            di_pre_stru.enable_pulldown_check = 1;
            di_buf->post_proc_flag = 1;
        }
    }

#ifndef USE_LIST
    if((di_pre_stru.di_mem_buf_dup_p == di_pre_stru.di_wr_buf)||
        (di_pre_stru.di_chan2_buf_dup_p == di_pre_stru.di_wr_buf)){
        printk("+++++++++++++++++++++++\n");
        if(recovery_flag==0){
            recovery_log_reason = 12;
        }
        recovery_flag++;
        return 0;
    }
#endif
    return 1;

}

static int check_recycle_buf(void)
{
    di_buf_t *di_buf = NULL;//, *ptmp;
    int itmp;
    int ret = 0;
    queue_for_each_entry(di_buf, ptmp, QUEUE_RECYCLE, list) {
        if((di_buf->pre_ref_count == 0)&&(di_buf->post_ref_count == 0)){
            if(di_buf->type == VFRAME_TYPE_IN){
                queue_out(di_buf);
                if(vframe_in[di_buf->index]){
                    vf_put(vframe_in[di_buf->index], VFM_NAME);
                    vf_notify_provider(VFM_NAME, VFRAME_EVENT_RECEIVER_PUT, NULL);
#ifdef DI_DEBUG
                    di_print("%s: vf_put(%d) %x\n", __func__, di_pre_stru.recycle_seq, vframe_in[di_buf->index]);
#endif
                    vframe_in[di_buf->index] = NULL;
                }
                di_buf->invert_top_bot_flag = 0;
                queue_in(di_buf, QUEUE_IN_FREE);
                di_pre_stru.recycle_seq++;
                ret |= 1;
            }
            else{
                queue_out(di_buf);
                di_buf->invert_top_bot_flag = 0;
                queue_in(di_buf, QUEUE_LOCAL_FREE);
                if(di_buf->di_wr_linked_buf){
               	    queue_in(di_buf->di_wr_linked_buf,QUEUE_LOCAL_FREE);
#ifdef DI_DEBUG
            	    di_print("%s: linked %s[%d]=>recycle_list\n",__func__, vframe_type_name[di_buf->di_wr_linked_buf->type], di_buf->di_wr_linked_buf->index);
#endif
               	    di_buf->di_wr_linked_buf = NULL;
                }
                ret |= 2;
            }
#ifdef DI_DEBUG
            di_print("%s: recycle %s[%d]\n", __func__, vframe_type_name[di_buf->type], di_buf->index);
#endif
        }
    }
    return ret;
}

#ifdef DET3D
static void set3d_view(enum tvin_trans_fmt trans_fmt, struct vframe_s *vf)
{
    struct vframe_view_s *left_eye, *right_eye;

    left_eye  = &vf->left_eye;
    right_eye = &vf->right_eye;

    switch (trans_fmt)
    {
        case TVIN_TFMT_3D_DET_LR:
        case TVIN_TFMT_3D_LRH_OLOR:
            left_eye->start_x    = 0;
            left_eye->start_y    = 0;
            left_eye->width      = vf->width >> 1;
            left_eye->height     = vf->height;
            right_eye->start_x   = vf->width >> 1;
            right_eye->start_y   = 0;
            right_eye->width     = vf->width >> 1;
            right_eye->height    = vf->height;
            break;
        case TVIN_TFMT_3D_DET_TB:
        case TVIN_TFMT_3D_TB:
            left_eye->start_x    = 0;
            left_eye->start_y    = 0;
            left_eye->width      = vf->width;
            left_eye->height     = vf->height >> 1;
            right_eye->start_x   = 0;
            right_eye->start_y   = vf->height >> 1;
            right_eye->width     = vf->width;
            right_eye->height    = vf->height >> 1;
            break;
        case TVIN_TFMT_3D_DET_INTERLACE:
            left_eye->start_x    = 0;
            left_eye->start_y    = 0;
            left_eye->width      = vf->width;
            left_eye->height     = vf->height >> 1;
            right_eye->start_x   = 0;
            right_eye->start_y   = 0;
            right_eye->width     = vf->width;
            right_eye->height    = vf->height >> 1;
            break;
        case TVIN_TFMT_3D_DET_CHESSBOARD:
            /***
             * LRLRLR     LRLRLR
             * LRLRLR  or RLRLRL
             * LRLRLR     LRLRLR
             * LRLRLR     RLRLRL
             */

            break;
         default:  //2D
            left_eye->start_x    = 0;
            left_eye->start_y    = 0;
            left_eye->width      = 0;
            left_eye->height     = 0;
            right_eye->start_x   = 0;
            right_eye->start_y   = 0;
            right_eye->width     = 0;
            right_eye->height    = 0;
            break;
    }
}

/*
static int get_3d_info(struct vframe_s *vf)
{
    int ret = 0;

    vf->trans_fmt = det3d_fmt_detect();
    printk("[det3d..]new 3d fmt: %d \n", vf->trans_fmt);

    vdin_set_view(vf->trans_fmt, vf);

    return ret;
}
*/

static irqreturn_t det3d_irq(int irq, void *dev_instance)
{
   unsigned int data32 = 0;

    if (det3d_en){
        data32 = det3d_fmt_detect();
        if (det3d_mode != data32) {
            det3d_mode = data32;
            printk("[det3d..]new 3d fmt: %d \n", det3d_mode);
        }
    }else{
        det3d_mode = 0;
    }
    di_pre_stru.det3d_trans_fmt = det3d_mode;
   return IRQ_HANDLED;
}
#endif
static irqreturn_t de_irq(int irq, void *dev_instance)
{
#ifndef CHECK_DI_DONE
   unsigned int data32;
   data32 = Rd(DI_INTR_CTRL);
   //if ( (data32 & 0xf) != 0x1 ) {
   //     printk("%s: error %x\n", __func__, data32);
   //} else {
        Wr(DI_INTR_CTRL, data32);
   //}
#endif
    //Wr(A9_0_IRQ_IN1_INTR_STAT_CLR, 1 << 14);
   //Rd(A9_0_IRQ_IN1_INTR_STAT_CLR);
    if(pre_process_time_force){
        return IRQ_HANDLED;
    }
    if(di_pre_stru.pre_de_busy==0){
        di_print("%s: wrong enter %x\n", __func__, Rd(DI_INTR_CTRL));
        return IRQ_HANDLED;
    }
#ifdef DI_DEBUG
        di_print("%s: start\n", __func__);
#endif

    di_pre_stru.pre_de_process_done = 1;
    di_pre_stru.pre_de_busy = 0;

    if(init_flag){
        //printk("%s:up di sema\n", __func__);
        trigger_pre_di_process('i');
    }

#ifdef DI_DEBUG
        di_print("%s: end\n", __func__);
#endif

   return IRQ_HANDLED;
}

/*
di post process
*/
static void inc_post_ref_count(di_buf_t* di_buf)
{
//    int post_blend_mode;

    if(di_buf == NULL){
#ifdef DI_DEBUG
        printk("%s: Error\n", __func__);
#endif
        if(recovery_flag==0){
            recovery_log_reason = 13;
        }
        recovery_flag++;
    }

	  if(di_buf->di_buf_dup_p[1]){
	  di_buf->di_buf_dup_p[1]->post_ref_count++;
	  }
    if (di_buf->pulldown_mode != PULL_DOWN_BLEND_2){
        if(di_buf->di_buf_dup_p[0]){
        di_buf->di_buf_dup_p[0]->post_ref_count++;
        }
    }
    if(di_buf->di_buf_dup_p[2]){
    di_buf->di_buf_dup_p[2]->post_ref_count++;
    }
}

static void dec_post_ref_count(di_buf_t* di_buf)
{

    if(di_buf == NULL){
#ifdef DI_DEBUG
        printk("%s: Error\n", __func__);
#endif
        if(recovery_flag==0){
            recovery_log_reason = 14;
        }
        recovery_flag++;
    }
		if(di_buf->pulldown_mode == PULL_DOWN_BUF1)
			return;
	  if(di_buf->di_buf_dup_p[1]){
	  di_buf->di_buf_dup_p[1]->post_ref_count--;
	  }
    if (di_buf->pulldown_mode != PULL_DOWN_BLEND_2){
        if(di_buf->di_buf_dup_p[0]){
        di_buf->di_buf_dup_p[0]->post_ref_count--;
        }
    }
    if(di_buf->di_buf_dup_p[2]){
    di_buf->di_buf_dup_p[2]->post_ref_count--;
}
}

static void process_vscale_skip(di_buf_t* di_buf, vframe_t* disp_vf)
{
    //vframe_t* di_post_vf = di_buf->vframe;
    di_buf_t* di_buf_i = NULL;
    if(di_buf->di_buf[0]!=NULL &&
        di_buf->process_fun_index!=PROCESS_FUN_NULL){ //di post is enabled
        di_vscale_skip_count = get_current_vscale_skip_count(disp_vf);
        if(((di_vscale_skip_count>0)&&(di_vscale_skip_enable&0x1))||(di_vscale_skip_enable>>16)||(bypass_dynamic_flag&0x2)){
            int width = (di_buf->di_buf[0]->canvas_config_size>>16)&0xffff;
            int canvas_height = (di_buf->di_buf[0]->canvas_config_size)&0xffff;
            if(di_vscale_skip_enable&0x2){//drop the bottom field
                if((di_buf->di_buf_dup_p[0])&&(di_buf->di_buf_dup_p[1])){
	    		          di_buf_i = (di_buf->di_buf_dup_p[1]->vframe->type & VIDTYPE_TYPEMASK)==VIDTYPE_INTERLACE_TOP ? di_buf->di_buf_dup_p[1] : di_buf->di_buf_dup_p[0];
                }
                else{
                    di_buf_i = di_buf->di_buf[0];
                }
            }
            else{
	            if((di_buf->di_buf[0]->post_proc_flag > 0)&&(di_buf->di_buf_dup_p[1])){
	                di_buf_i = di_buf->di_buf_dup_p[1];
	            }
	            else{
	                di_buf_i = di_buf->di_buf[0];
	            }
            }
            disp_vf->type = di_buf_i->vframe->type;
            //printk("%s (%x %x) (%x %x)\n", __func__, disp_vf, disp_vf->type, di_buf_i->vframe, di_buf_i->vframe->type);
            disp_vf->width = di_buf_i->vframe->width;
            disp_vf->height = di_buf_i->vframe->height;
            disp_vf->duration = di_buf_i->vframe->duration;
            disp_vf->pts = di_buf_i->vframe->pts;
            disp_vf->flag = di_buf_i->vframe->flag;
            disp_vf->canvas0Addr = di_post_buf0_canvas_idx[di_post_stru.canvas_id];
            disp_vf->canvas1Addr = di_post_buf0_canvas_idx[di_post_stru.canvas_id];
            canvas_config(di_post_buf0_canvas_idx[di_post_stru.canvas_id], di_buf_i->nr_adr, width*2, canvas_height, 0, 0);
            disable_post_deinterlace_2();
            di_post_stru.vscale_skip_flag = true;
        }
    }
}

static int de_post_disable_fun(void* arg, vframe_t* disp_vf)
{
    di_buf_t* di_buf = (di_buf_t*)arg;

    di_post_stru.vscale_skip_flag = false;
    di_post_stru.toggle_flag = true;

    process_vscale_skip(di_buf, disp_vf);
    //for atv static image flickering
    if(di_buf->process_fun_index == PROCESS_FUN_NULL){
        disable_post_deinterlace_2();
    }
    return 1; //called for new_format_flag, make video set video_property_changed
}

static int do_nothing_fun(void* arg, vframe_t* disp_vf)
{
    di_buf_t* di_buf = (di_buf_t*)arg;

    di_post_stru.vscale_skip_flag = false;
    di_post_stru.toggle_flag = true;

    process_vscale_skip(di_buf, disp_vf);

    if(di_buf->process_fun_index == PROCESS_FUN_NULL){
        if(Rd(DI_IF1_GEN_REG)&0x1){
            disable_post_deinterlace_2();
        }
    }
    return 0;
}

static int do_pre_only_fun(void* arg, vframe_t* disp_vf)
{
    di_post_stru.vscale_skip_flag = false;
    di_post_stru.toggle_flag = true;

#ifdef DI_USE_FIXED_CANVAS_IDX
    if(arg){
        di_buf_t* di_buf = (di_buf_t*)arg;
        vframe_t* vf = di_buf->vframe;
        int width = (di_buf->di_buf[0]->canvas_config_size>>16)&0xffff;
        int canvas_height = (di_buf->di_buf[0]->canvas_config_size)&0xffff;
#ifdef CONFIG_VSYNC_RDMA
	      if((is_vsync_rdma_enable()&&((force_update_post_reg&0x40)==0))||(force_update_post_reg&0x20)){
       	        di_post_stru.canvas_id = di_post_stru.next_canvas_id;
        }
        else{
                di_post_stru.canvas_id = 0;
                di_post_stru.next_canvas_id = 1;
        }
#endif

        canvas_config(di_post_buf0_canvas_idx[di_post_stru.canvas_id], di_buf->di_buf[0]->nr_adr, width*2, canvas_height, 0, 0);

        vf->canvas0Addr = di_post_buf0_canvas_idx[di_post_stru.canvas_id];
        vf->canvas1Addr = di_post_buf0_canvas_idx[di_post_stru.canvas_id];
#ifdef DET3D
        if(di_pre_stru.vframe_interleave_flag && di_buf->di_buf[1]){
            canvas_config(di_post_buf1_canvas_idx[di_post_stru.canvas_id], di_buf->di_buf[1]->nr_adr, width*2, canvas_height, 0, 0);
            vf->canvas1Addr = di_post_buf1_canvas_idx[di_post_stru.canvas_id];
	    vf->duration <<= 1;
        }
#endif
        di_post_stru.next_canvas_id = di_post_stru.canvas_id?0:1;

        if(di_buf->process_fun_index == PROCESS_FUN_NULL){
            if(Rd(DI_IF1_GEN_REG)&0x1){
                disable_post_deinterlace_2();
            }
        }


    }
#endif

    return 0;
}

static void get_vscale_skip_count(unsigned par)
{
    di_vscale_skip_count_real = (par >> 24)&0xff;
}

#define get_vpp_reg_update_flag(par) ((par>>16)&0x1)

static int blend_mode = -2;
module_param(blend_mode,int,0664);
MODULE_PARM_DESC(blend_mode,"\n force post blend mode \n");

static int de_post_process(void* arg, unsigned zoom_start_x_lines,
     unsigned zoom_end_x_lines, unsigned zoom_start_y_lines, unsigned zoom_end_y_lines, vframe_t* disp_vf)
{
    di_buf_t* di_buf = (di_buf_t*)arg;
    int di_width, di_height, di_start_x, di_end_x, di_start_y, di_end_y;
    int hold_line = post_hold_line;
   	int post_blend_en=0, post_blend_mode=0, blend_mtn_en=0, ei_en;

    if(di_get_power_control(1)==0){
        return 0;
    }
    get_vscale_skip_count(zoom_start_x_lines);

    if(di_post_stru.vscale_skip_flag){
        return 0;
    }

    if((!di_post_stru.toggle_flag)&&((force_update_post_reg&0x10)==0))
        return 0;

    if(di_post_stru.toggle_flag)
	top_bot_config(di_buf->di_buf_dup_p[1]);

    di_post_stru.toggle_flag = false;

    di_post_stru.cur_disp_index = di_buf->index;

    if((di_post_stru.post_process_fun_index != 1)||((force_update_post_reg&0xf)!=0)){
        force_update_post_reg &= ~0x1;
        di_post_stru.post_process_fun_index = 1;
	    	di_post_stru.update_post_reg_flag = update_post_reg_count;
    }

    if(get_vpp_reg_update_flag(zoom_start_x_lines)){
	    	di_post_stru.update_post_reg_flag = update_post_reg_count;
	    	//printk("%s set update_post_reg_flag to %d\n", __func__, di_post_stru.update_post_reg_flag);
	  }

    zoom_start_x_lines = zoom_start_x_lines&0xffff;
    zoom_end_x_lines = zoom_end_x_lines&0xffff;
    zoom_start_y_lines = zoom_start_y_lines&0xffff;
    zoom_end_y_lines = zoom_end_y_lines&0xffff;

    if((init_flag == 0)&&(new_keep_last_frame_enable == 0)){
        return 0;
    }

    di_start_x = zoom_start_x_lines;
    di_end_x = zoom_end_x_lines;
    di_width = di_end_x - di_start_x + 1;
    di_start_y = zoom_start_y_lines;
    di_end_y = zoom_end_y_lines;
    di_height = di_end_y - di_start_y + 1;
//printk("height = (%d %d %d %d %d)\n", di_buf->vframe->height, zoom_start_x_lines, zoom_end_x_lines, zoom_start_y_lines, zoom_end_y_lines);

    	if ( Rd(DI_POST_SIZE) != ((di_width-1) | ((di_height-1)<<16))
    		|| (di_post_stru.di_buf0_mif.luma_x_start0 != di_start_x) || (di_post_stru.di_buf0_mif.luma_y_start0 != di_start_y/2) )
    	{
    	     initial_di_post_2(di_width, di_height, hold_line);
	     di_post_stru.di_buf0_mif.luma_x_start0 	= di_start_x;
	     di_post_stru.di_buf0_mif.luma_x_end0 	= di_end_x;
	     di_post_stru.di_buf0_mif.luma_y_start0 	= di_start_y>>1;
	     di_post_stru.di_buf0_mif.luma_y_end0 	= di_end_y >>1 ;
	     di_post_stru.di_buf1_mif.luma_x_start0 	= di_start_x;
	     di_post_stru.di_buf1_mif.luma_x_end0 	= di_end_x;
	     di_post_stru.di_buf1_mif.luma_y_start0 	= di_start_y>>1;
	     di_post_stru.di_buf1_mif.luma_y_end0 	= di_end_y >>1;
	     di_post_stru.di_mtncrd_mif.start_x 	= di_start_x;
	     di_post_stru.di_mtncrd_mif.end_x 	        = di_end_x;
	     di_post_stru.di_mtncrd_mif.start_y 	= di_start_y>>1;
	     di_post_stru.di_mtncrd_mif.end_y 	        = di_end_y >>1;
	     di_post_stru.di_mtnprd_mif.start_x 	= di_start_x;
	     di_post_stru.di_mtnprd_mif.end_x 	        = di_end_x;
	     di_post_stru.di_mtnprd_mif.start_y 	= di_start_y>>1;
	     di_post_stru.di_mtnprd_mif.end_y 	        = di_end_y >>1;
	     di_post_stru.update_post_reg_flag = update_post_reg_count;
    	}

#ifdef DI_USE_FIXED_CANVAS_IDX
#ifdef CONFIG_VSYNC_RDMA
	if((is_vsync_rdma_enable()&&((force_update_post_reg&0x40)==0))||(force_update_post_reg&0x20)){
       	     di_post_stru.canvas_id = di_post_stru.next_canvas_id;
        }
        else{
             di_post_stru.canvas_id = 0;
             di_post_stru.next_canvas_id = 1;
        }
#endif
	/*for debug*/
	if(blend_mode != -2)
	    di_buf->pulldown_mode = blend_mode;
		
	switch(di_buf->pulldown_mode){
	    case PULL_DONW_BLEND_0:
	    case PULL_DOWN_NORMAL:
		config_canvas_idx(di_buf->di_buf_dup_p[1], di_post_buf0_canvas_idx[di_post_stru.canvas_id], -1);
		config_canvas_idx(di_buf->di_buf_dup_p[1], -1, di_post_mtnprd_canvas_idx[di_post_stru.canvas_id]);
		config_canvas_idx(di_buf->di_buf_dup_p[0], di_post_buf1_canvas_idx[di_post_stru.canvas_id], -1);
		config_canvas_idx(di_buf->di_buf_dup_p[0], -1, di_post_mtncrd_canvas_idx[di_post_stru.canvas_id]);
		break;
	    case PULL_DOWN_BLEND_2:
		config_canvas_idx(di_buf->di_buf_dup_p[1], di_post_buf0_canvas_idx[di_post_stru.canvas_id], -1);
		config_canvas_idx(di_buf->di_buf_dup_p[1], -1, di_post_mtnprd_canvas_idx[di_post_stru.canvas_id]);
		config_canvas_idx(di_buf->di_buf_dup_p[2], di_post_buf1_canvas_idx[di_post_stru.canvas_id], -1);
		config_canvas_idx(di_buf->di_buf_dup_p[2], -1, di_post_mtncrd_canvas_idx[di_post_stru.canvas_id]);
		break;
	    case PULL_DOWN_MTN:
		config_canvas_idx(di_buf->di_buf_dup_p[1], di_post_buf0_canvas_idx[di_post_stru.canvas_id], -1);
		config_canvas_idx(di_buf->di_buf_dup_p[1], -1, di_post_mtnprd_canvas_idx[di_post_stru.canvas_id]);
		config_canvas_idx(di_buf->di_buf_dup_p[0], di_post_buf1_canvas_idx[di_post_stru.canvas_id], -1);
		config_canvas_idx(di_buf->di_buf_dup_p[0], -1, di_post_mtncrd_canvas_idx[di_post_stru.canvas_id]);
		break;
	    case PULL_DOWN_BUF1://wave with buf1
		config_canvas_idx(di_buf->di_buf_dup_p[1], di_post_buf0_canvas_idx[di_post_stru.canvas_id], -1);
		config_canvas_idx(di_buf->di_buf_dup_p[1], -1, di_post_mtnprd_canvas_idx[di_post_stru.canvas_id]);
		config_canvas_idx(di_buf->di_buf_dup_p[0], di_post_buf1_canvas_idx[di_post_stru.canvas_id], -1);
		config_canvas_idx(di_buf->di_buf_dup_p[0], -1, di_post_mtncrd_canvas_idx[di_post_stru.canvas_id]);
		break;
	    case PULL_DOWN_EI:
		config_canvas_idx(di_buf->di_buf_dup_p[1], di_post_buf0_canvas_idx[di_post_stru.canvas_id], -1);
		break;
	    default:
		break;
	}    
     	di_post_stru.next_canvas_id = di_post_stru.canvas_id?0:1;
#endif
	switch(di_buf->pulldown_mode){
	    case PULL_DONW_BLEND_0:
	    case PULL_DOWN_NORMAL:
		di_post_stru.di_buf0_mif.canvas0_addr0 = di_buf->di_buf_dup_p[1]->nr_canvas_idx;
		di_post_stru.di_buf1_mif.canvas0_addr0 = di_buf->di_buf_dup_p[0]->nr_canvas_idx;
		di_post_stru.di_mtnprd_mif.canvas_num = di_buf->di_buf_dup_p[1]->mtn_canvas_idx;
		di_post_stru.di_mtncrd_mif.canvas_num = di_buf->di_buf_dup_p[0]->mtn_canvas_idx;
		post_blend_mode = 3;
		blend_mtn_en = 1;
		ei_en = 1;
		post_blend_en = 1;
		break;
	    case PULL_DOWN_BLEND_2:
		di_post_stru.di_buf0_mif.canvas0_addr0 = di_buf->di_buf_dup_p[1]->nr_canvas_idx;
		di_post_stru.di_buf1_mif.canvas0_addr0 = di_buf->di_buf_dup_p[2]->nr_canvas_idx;
		di_post_stru.di_mtnprd_mif.canvas_num = di_buf->di_buf_dup_p[1]->mtn_canvas_idx;
		di_post_stru.di_mtncrd_mif.canvas_num = di_buf->di_buf_dup_p[2]->mtn_canvas_idx;
		post_blend_mode = 3;
		blend_mtn_en = 1;
		ei_en = 1;
		post_blend_en = 1;
		break;
	    case PULL_DOWN_MTN:
		di_post_stru.di_buf0_mif.canvas0_addr0 = di_buf->di_buf_dup_p[1]->nr_canvas_idx;
		di_post_stru.di_buf1_mif.canvas0_addr0 = di_buf->di_buf_dup_p[0]->nr_canvas_idx;
		di_post_stru.di_mtnprd_mif.canvas_num = di_buf->di_buf_dup_p[1]->mtn_canvas_idx;
		di_post_stru.di_mtncrd_mif.canvas_num = di_buf->di_buf_dup_p[0]->mtn_canvas_idx;
		post_blend_mode = 0;
		blend_mtn_en = 1;
		ei_en = 1;
		post_blend_en = 1;
		break;
	    case PULL_DOWN_BUF1:
		di_post_stru.di_buf0_mif.canvas0_addr0 = di_buf->di_buf_dup_p[1]->nr_canvas_idx;
		di_post_stru.di_mtnprd_mif.canvas_num = di_buf->di_buf_dup_p[1]->mtn_canvas_idx;
		di_post_stru.di_buf1_mif.canvas0_addr0 = di_buf->di_buf_dup_p[0]->nr_canvas_idx;
		di_post_stru.di_mtncrd_mif.canvas_num = di_buf->di_buf_dup_p[0]->mtn_canvas_idx;
		post_blend_mode = 1;
		blend_mtn_en = 1;//must enable
		ei_en = 1;//must enable
		post_blend_en = 1;
		break;
	    case PULL_DOWN_EI:
		di_post_stru.di_buf0_mif.canvas0_addr0 = di_buf->di_buf_dup_p[1]->nr_canvas_idx;
		post_blend_mode = 2;
		blend_mtn_en = 0;//must enable
		ei_en = 1;
		post_blend_en = 0;
		break;
	    default:
		break;
	}
	di_print("post_blend_mode %d\n", post_blend_mode);
#ifdef NEW_DI_V1
    if(get_new_mode_flag() == 1){
        blend_ctrl |= (1<<31);
    }
    else if(get_new_mode_flag()==0){
        blend_ctrl &= (~(1<<31));
    }
#endif

    if((di_post_stru.update_post_reg_flag)&&((force_update_post_reg&0x80)==0))
	enable_di_post_2 (
	    		&di_post_stru.di_buf0_mif,
	    		&di_post_stru.di_buf1_mif,
	    		NULL,
	    		&di_post_stru.di_mtncrd_mif,
	    		&di_post_stru.di_mtnprd_mif,
	    		ei_en, 																// ei enable
	    		post_blend_en,													// blend enable
	    		blend_mtn_en,													// blend mtn enable
	    		post_blend_mode,												// blend mode.
	    		1,                 												// di_vpp_en.
	    		0,                 												// di_ddr_en.
	    		(di_buf->di_buf_dup_p[1]->vframe->type & VIDTYPE_TYPEMASK)==VIDTYPE_INTERLACE_TOP ? 0 : 1,		// 1 bottom generate top
	    		hold_line,
	    		post_urgent,
	    		reg_mtn_info
	    	);
	else
	     di_post_switch_buffer (
	    		&di_post_stru.di_buf0_mif,
	    		&di_post_stru.di_buf1_mif,
	    		NULL,
	    		&di_post_stru.di_mtncrd_mif,
	    		&di_post_stru.di_mtnprd_mif,
	    		ei_en, 																// ei enable
	    		post_blend_en,													// blend enable
	    		blend_mtn_en,													// blend mtn enable
	    		post_blend_mode,												// blend mode.
	    		1,                 												// di_vpp_en.
	    		0,                 												// di_ddr_en.
	    		(di_buf->di_buf_dup_p[1]->vframe->type & VIDTYPE_TYPEMASK)==VIDTYPE_INTERLACE_TOP ? 0 : 1,		// 1 bottom generate top
	    		hold_line,
	    		post_urgent,
	    		reg_mtn_info
	    	);

#ifdef NEW_DI_V1
    if(di_post_stru.update_post_reg_flag){
        di_apply_reg_cfg(1);
    }

    di_post_read_reverse_irq(overturn);
#endif

    if(di_post_stru.update_post_reg_flag>0)
	di_post_stru.update_post_reg_flag--;
    return 0;
}
#if 0
static int de_post_process_pd(void* arg, unsigned zoom_start_x_lines,
     unsigned zoom_end_x_lines, unsigned zoom_start_y_lines, unsigned zoom_end_y_lines, vframe_t* disp_vf)
{
    di_buf_t* di_buf = (di_buf_t*)arg;
    int di_width, di_height, di_start_x, di_end_x, di_start_y, di_end_y;
    int hold_line = post_hold_line;
   	int post_blend_mode;

    if(di_get_power_control(1)==0){
        return 0;
    }
    get_vscale_skip_count(zoom_start_x_lines);

    if(di_post_stru.vscale_skip_flag){
        return 0;
    }

    if((!di_post_stru.toggle_flag)&&((force_update_post_reg&0x10)==0))
        return 0;

    if(di_post_stru.toggle_flag)
	top_bot_config(di_buf->di_buf_dup_p[1]);

    di_post_stru.toggle_flag = false;

    di_post_stru.cur_disp_index = di_buf->index;

    if((di_post_stru.post_process_fun_index != 2)||((force_update_post_reg&0xf)!=0)){
        force_update_post_reg &= ~0x1;
        di_post_stru.post_process_fun_index = 2;
        di_post_stru.update_post_reg_flag = update_post_reg_count;
    }

    if(get_vpp_reg_update_flag(zoom_start_x_lines)){
	    	di_post_stru.update_post_reg_flag = update_post_reg_count;
	    	//printk("%s set update_post_reg_flag to %d\n", __func__, di_post_stru.update_post_reg_flag);
    }

    zoom_start_x_lines = zoom_start_x_lines&0xffff;
    zoom_end_x_lines = zoom_end_x_lines&0xffff;
    zoom_start_y_lines = zoom_start_y_lines&0xffff;
    zoom_end_y_lines = zoom_end_y_lines&0xffff;

    if(di_buf->pulldown_mode == 0)
        post_blend_mode = 0;
    else
        post_blend_mode =1;

    if((init_flag == 0)&&(new_keep_last_frame_enable == 0)){
        return 0;
    }

    di_start_x = zoom_start_x_lines;
    di_end_x = zoom_end_x_lines;
    di_width = di_end_x - di_start_x + 1;
    di_start_y = zoom_start_y_lines;
    di_end_y = zoom_end_y_lines;
    di_height = di_end_y - di_start_y + 1;
//printk("height = (%d %d %d %d %d)\n", di_buf->vframe->height, zoom_start_x_lines, zoom_end_x_lines, zoom_start_y_lines, zoom_end_y_lines);

    	if ( Rd(DI_POST_SIZE) != ((di_width-1) | ((di_height-1)<<16))
    		|| (di_post_stru.di_buf0_mif.luma_x_start0 != di_start_x) || (di_post_stru.di_buf0_mif.luma_y_start0 != di_start_y/2) )
    	{
    		initial_di_post_2(di_width, di_height, hold_line);
		di_post_stru.di_buf0_mif.luma_x_start0 	= di_start_x;
		di_post_stru.di_buf0_mif.luma_x_end0 	= di_end_x;
		di_post_stru.di_buf0_mif.luma_y_start0 	= di_start_y>>1;
		di_post_stru.di_buf0_mif.luma_y_end0 	= di_end_y >>1 ;
		di_post_stru.di_buf1_mif.luma_x_start0 	= di_start_x;
		di_post_stru.di_buf1_mif.luma_x_end0 	= di_end_x;
		di_post_stru.di_buf1_mif.luma_y_start0 	= di_start_y>>1;
		di_post_stru.di_buf1_mif.luma_y_end0 	= di_end_y >>1;
	    	di_post_stru.di_mtncrd_mif.start_x 	= di_start_x;
	    	di_post_stru.di_mtncrd_mif.end_x 	= di_end_x;
	    	di_post_stru.di_mtncrd_mif.start_y 	= di_start_y>>1;
	    	di_post_stru.di_mtncrd_mif.end_y 	= di_end_y >>1;
	        di_post_stru.di_mtnprd_mif.start_x 	= di_start_x;
	    	di_post_stru.di_mtnprd_mif.end_x 	= di_end_x;
	        di_post_stru.di_mtnprd_mif.start_y 	= di_start_y>>1;
	    	di_post_stru.di_mtnprd_mif.end_y 	= di_end_y >>1;
	    	di_post_stru.update_post_reg_flag = update_post_reg_count;
    	}
#ifdef DI_USE_FIXED_CANVAS_IDX
#ifdef CONFIG_VSYNC_RDMA
	if((is_vsync_rdma_enable()&&((force_update_post_reg&0x40)==0))||(force_update_post_reg&0x20)){
       	        di_post_stru.canvas_id = di_post_stru.next_canvas_id;
        }
        else{
                di_post_stru.canvas_id = 0;
                di_post_stru.next_canvas_id = 1;
        }
#endif

      config_canvas_idx(di_buf->di_buf_dup_p[1], di_post_buf0_canvas_idx[di_post_stru.canvas_id], -1);
      if (post_blend_mode == 1)
          config_canvas_idx(di_buf->di_buf_dup_p[2], di_post_buf1_canvas_idx[di_post_stru.canvas_id], -1);
      else
        config_canvas_idx(di_buf->di_buf_dup_p[0], di_post_buf1_canvas_idx[di_post_stru.canvas_id], -1);

      config_canvas_idx(di_buf->di_buf_dup_p[1], -1, di_post_mtncrd_canvas_idx[di_post_stru.canvas_id]);
      config_canvas_idx(di_buf->di_buf_dup_p[2], -1, di_post_mtnprd_canvas_idx[di_post_stru.canvas_id]);
   		di_post_stru.next_canvas_id = di_post_stru.canvas_id?0:1;
#endif

	    di_post_stru.di_buf0_mif.canvas0_addr0 = di_buf->di_buf_dup_p[1]->nr_canvas_idx;
	    if (post_blend_mode == 1 && di_buf->di_buf_dup_p[2]){
	        di_post_stru.di_buf1_mif.canvas0_addr0 = di_buf->di_buf_dup_p[2]->nr_canvas_idx;
	    }else
	        di_post_stru.di_buf1_mif.canvas0_addr0 = di_buf->di_buf_dup_p[0]->nr_canvas_idx;
	    di_post_stru.di_mtncrd_mif.canvas_num = di_buf->di_buf_dup_p[1]->mtn_canvas_idx;
      if(di_buf->di_buf_dup_p[2])
          di_post_stru.di_mtnprd_mif.canvas_num = di_buf->di_buf_dup_p[2]->mtn_canvas_idx;

      if((di_post_stru.update_post_reg_flag)&&((force_update_post_reg&0x80)==0))
  	    enable_di_post_pd (
	    		&di_post_stru.di_buf0_mif,
	    		&di_post_stru.di_buf1_mif,
	    		NULL,
	    		&di_post_stru.di_mtncrd_mif,
	    		&di_post_stru.di_mtnprd_mif,
	    		1, 																// ei enable
	    		1,													// blend enable
	    		1,													// blend mtn enable
	    		3,//post_blend_mode normal												// blend mode.
	    		1,                 												// di_vpp_en.
	    		0,                 												// di_ddr_en.
	    		(di_buf->di_buf_dup_p[1]->vframe->type & VIDTYPE_TYPEMASK)==VIDTYPE_INTERLACE_TOP ? 0 : 1,		// 1 bottom generate top
	    		hold_line,
	    		post_urgent
	    	);
	    else
  	    di_post_switch_buffer_pd (
	    		&di_post_stru.di_buf0_mif,
	    		&di_post_stru.di_buf1_mif,
	    		NULL,
	    		&di_post_stru.di_mtncrd_mif,
	    		&di_post_stru.di_mtnprd_mif,
	    		1, 																// ei enable
	    		1,													// blend enable
	    		1,													// blend mtn enable
	    		3,//post_blend_mode normal												// blend mode.
	    		1,                 												// di_vpp_en.
	    		0,                 												// di_ddr_en.
	    		(di_buf->di_buf_dup_p[1]->vframe->type & VIDTYPE_TYPEMASK)==VIDTYPE_INTERLACE_TOP ? 0 : 1,		// 1 bottom generate top
	    		hold_line,
	    		post_urgent
	    	);
#ifdef NEW_DI_V1
    di_post_read_reverse_irq(overturn);
#endif
    if(di_post_stru.update_post_reg_flag>0)
	    di_post_stru.update_post_reg_flag--;

    return 0;
}

static int de_post_process_prog(void* arg, unsigned zoom_start_x_lines,
     unsigned zoom_end_x_lines, unsigned zoom_start_y_lines, unsigned zoom_end_y_lines, vframe_t* disp_vf)
{
    di_buf_t* di_buf = (di_buf_t*)arg;
    int di_width, di_height, di_start_x, di_end_x, di_start_y, di_end_y;
    int hold_line = post_hold_line;
   	int post_blend_mode;

    if(di_get_power_control(1)==0){
        return 0;
    }
    get_vscale_skip_count(zoom_start_x_lines);

    if(di_post_stru.vscale_skip_flag){
        return 0;
    }

    if((!di_post_stru.toggle_flag)&&((force_update_post_reg&0x10)==0))
        return 0;

    if(di_post_stru.toggle_flag)
	top_bot_config(di_buf->di_buf_dup_p[0]);

    di_post_stru.toggle_flag = false;

    di_post_stru.cur_disp_index = di_buf->index;

    if((di_post_stru.post_process_fun_index != 3)||((force_update_post_reg&0xf)!=0)){
        force_update_post_reg &= ~0x1;
        di_post_stru.post_process_fun_index = 3;
	    	di_post_stru.update_post_reg_flag = update_post_reg_count;
    }

    if(get_vpp_reg_update_flag(zoom_start_x_lines)){
	    	di_post_stru.update_post_reg_flag = update_post_reg_count;
	    	//printk("%s set update_post_reg_flag to %d\n", __func__, di_post_stru.update_post_reg_flag);
	  }

    zoom_start_x_lines = zoom_start_x_lines&0xffff;
    zoom_end_x_lines = zoom_end_x_lines&0xffff;
    zoom_start_y_lines = zoom_start_y_lines&0xffff;
    zoom_end_y_lines = zoom_end_y_lines&0xffff;

    post_blend_mode =1;

    if((init_flag == 0)&&(new_keep_last_frame_enable == 0)){
        return 0;
    }

    di_start_x = zoom_start_x_lines;
    di_end_x = zoom_end_x_lines;
    di_width = di_end_x - di_start_x + 1;
    di_start_y = zoom_start_y_lines;
    di_end_y = zoom_end_y_lines;
    di_height = di_end_y - di_start_y + 1;
//printk("height = (%d %d %d %d %d)\n", di_buf->vframe->height, zoom_start_x_lines, zoom_end_x_lines, zoom_start_y_lines, zoom_end_y_lines);

    	if ( Rd(DI_POST_SIZE) != ((di_width-1) | ((di_height-1)<<16))
    		|| (di_post_stru.di_buf0_mif.luma_x_start0 != di_start_x) || (di_post_stru.di_buf0_mif.luma_y_start0 != di_start_y/2) )
    	{
    		initial_di_post_2(di_width, di_height, hold_line);
	        di_post_stru.di_buf0_mif.luma_x_start0 	= di_start_x;
		di_post_stru.di_buf0_mif.luma_x_end0 	= di_end_x;
		di_post_stru.di_buf0_mif.luma_y_start0 	= di_start_y>>1;
		di_post_stru.di_buf0_mif.luma_y_end0 	= di_end_y >>1 ;
		di_post_stru.di_buf1_mif.luma_x_start0 	= di_start_x;
		di_post_stru.di_buf1_mif.luma_x_end0 	= di_end_x;
		di_post_stru.di_buf1_mif.luma_y_start0 	= di_start_y>>1;
		di_post_stru.di_buf1_mif.luma_y_end0 	= di_end_y >>1;
	    	di_post_stru.di_mtncrd_mif.start_x 	= di_start_x;
	    	di_post_stru.di_mtncrd_mif.end_x 	= di_end_x;
	    	di_post_stru.di_mtncrd_mif.start_y 	= di_start_y>>1;
	    	di_post_stru.di_mtncrd_mif.end_y 	= di_end_y >>1;
	        di_post_stru.di_mtnprd_mif.start_x 	= di_start_x;
	    	di_post_stru.di_mtnprd_mif.end_x 	= di_end_x;
	        di_post_stru.di_mtnprd_mif.start_y 	= di_start_y>>1;
	    	di_post_stru.di_mtnprd_mif.end_y 	= di_end_y >>1;
	    	di_post_stru.update_post_reg_flag = update_post_reg_count;
    	}

#ifdef DI_USE_FIXED_CANVAS_IDX
#ifdef CONFIG_VSYNC_RDMA
	if((is_vsync_rdma_enable()&&((force_update_post_reg&0x40)==0))||(force_update_post_reg&0x20)){
       	        di_post_stru.canvas_id = di_post_stru.next_canvas_id;
        }
        else{
        	di_post_stru.canvas_id = 0;
        	di_post_stru.next_canvas_id = 1;
        }
#endif

      config_canvas_idx(di_buf->di_buf_dup_p[0], di_post_buf0_canvas_idx[di_post_stru.canvas_id], di_post_mtncrd_canvas_idx[di_post_stru.canvas_id]);
      config_canvas_idx(di_buf->di_buf_dup_p[1], di_post_buf1_canvas_idx[di_post_stru.canvas_id], di_post_mtnprd_canvas_idx[di_post_stru.canvas_id]);
   		di_post_stru.next_canvas_id = di_post_stru.canvas_id?0:1;
#endif
	    di_post_stru.di_buf0_mif.canvas0_addr0 = di_buf->di_buf_dup_p[0]->nr_canvas_idx;
      di_post_stru.di_buf1_mif.canvas0_addr0 = di_buf->di_buf_dup_p[1]->nr_canvas_idx;
	    di_post_stru.di_mtncrd_mif.canvas_num = di_buf->di_buf_dup_p[0]->mtn_canvas_idx;
      di_post_stru.di_mtnprd_mif.canvas_num = di_buf->di_buf_dup_p[1]->mtn_canvas_idx;

      if((di_post_stru.update_post_reg_flag)&&((force_update_post_reg&0x80)==0))
	      enable_di_post_pd (
	    		&di_post_stru.di_buf0_mif,
	    		&di_post_stru.di_buf1_mif,
	    		NULL,
	    		&di_post_stru.di_mtncrd_mif,
	    		&di_post_stru.di_mtnprd_mif,
	    		1, 																// ei enable
	    		1,													// blend enable
	    		1,													// blend mtn enable
	    		post_blend_mode,												// blend mode.
	    		1,                 												// di_vpp_en.
	    		0,                 												// di_ddr_en.
	    		(di_buf->di_buf_dup_p[0]->vframe->type & VIDTYPE_TYPEMASK)==VIDTYPE_INTERLACE_TOP ? 0 : 1,		// 1 bottom generate top
	    		hold_line,
	    		post_urgent
	    	);
	    else
	      di_post_switch_buffer_pd (
	    		&di_post_stru.di_buf0_mif,
	    		&di_post_stru.di_buf1_mif,
	    		NULL,
	    		&di_post_stru.di_mtncrd_mif,
	    		&di_post_stru.di_mtnprd_mif,
	    		1, 																// ei enable
	    		1,													// blend enable
	    		1,													// blend mtn enable
	    		post_blend_mode,												// blend mode.
	    		1,                 												// di_vpp_en.
	    		0,                 												// di_ddr_en.
	    		(di_buf->di_buf_dup_p[0]->vframe->type & VIDTYPE_TYPEMASK)==VIDTYPE_INTERLACE_TOP ? 0 : 1,		// 1 bottom generate top
	    		hold_line,
	    		post_urgent
	    	);
#ifdef NEW_DI_V1
    di_post_read_reverse_irq(overturn);
#endif

    if(di_post_stru.update_post_reg_flag>0)
	    di_post_stru.update_post_reg_flag--;
    return 0;
}

#ifdef FORCE_BOB_SUPPORT
static int de_post_process_force_bob(void* arg, unsigned zoom_start_x_lines,
     unsigned zoom_end_x_lines, unsigned zoom_start_y_lines, unsigned zoom_end_y_lines, vframe_t* disp_vf)
{
    de_post_process(arg, zoom_start_x_lines, zoom_end_x_lines, zoom_start_y_lines, zoom_end_y_lines, disp_vf);
    #ifdef NEW_DI_V1
    VSYNC_WR_MPEG_REG_BITS(DI_POST_CTRL, 0, 5, 1);
    #else
    VSYNC_WR_MPEG_REG(DI_BLEND_CTRL, 0, 20, 1);
    #endif
    return 0;
}
#endif
#endif
int pd_detect_rst ;

static void recycle_vframe_type_post(di_buf_t* di_buf)
{
    int i;
    if(di_buf == NULL){
#ifdef DI_DEBUG
        printk("%s:Error\n", __func__);
#endif
        if(recovery_flag==0){
            recovery_log_reason = 15;
        }
        recovery_flag++;
        return;
    }
    if(di_buf->process_fun_index == PROCESS_FUN_DI){
        dec_post_ref_count(di_buf);
    }
    for(i=0;i<2;i++){
        if(di_buf->di_buf[i]){
            queue_in(di_buf->di_buf[i], QUEUE_RECYCLE);
         }
    }
    queue_out(di_buf); //remove it from display_list_head
    di_buf->invert_top_bot_flag = 0;
    queue_in(di_buf, QUEUE_POST_FREE);
}

#ifdef DI_DEBUG
static void recycle_vframe_type_post_print(di_buf_t* di_buf, const char* func, const int line)
{
    int i;
    di_print("%s:%d ", func,line);
    for(i=0;i<2;i++){
        if(di_buf->di_buf[i]){
            di_print("%s[%d]<%d>=>recycle_list; ", vframe_type_name[di_buf->di_buf[i]->type], di_buf->di_buf[i]->index, i);
        }
    }
    di_print("%s[%d] =>post_free_list\n", vframe_type_name[di_buf->type], di_buf->index);
}
#endif


static int pulldown_process(di_buf_t* di_buf, int buffer_count)
{
    int pulldown_type=-1; /* 0, 2:2; 1, m:n */
    int win_pd_type[5] = {-1,-1,-1,-1,-1};
    int ii;
    int pulldown_mode2;
    int pulldown_mode_ret;
    insert_pd_his(&di_buf->di_buf_dup_p[1]->field_pd_info);
    if(buffer_count==4){
    //4 buffers
            cal_pd_parameters(&di_buf->di_buf_dup_p[2]->field_pd_info, &di_buf->di_buf_dup_p[1]->field_pd_info, &di_buf->di_buf_dup_p[3]->field_pd_info, &field_pd_th); //cal parameters of di_buf_dup_p[2]
            pattern_check_pre_2(0, &di_buf->di_buf_dup_p[3]->field_pd_info,
                 &di_buf->di_buf_dup_p[2]->field_pd_info,
                &di_buf->di_buf_dup_p[1]->field_pd_info,
                (int *)&di_buf->di_buf_dup_p[2]->pulldown_mode,
                (int *)&di_buf->di_buf_dup_p[1]->pulldown_mode,
                &pulldown_type, &field_pd_th
              );

              for(ii=0;ii<MAX_WIN_NUM; ii++){
                cal_pd_parameters(&di_buf->di_buf_dup_p[2]->win_pd_info[ii], &di_buf->di_buf_dup_p[1]->win_pd_info[ii], &di_buf->di_buf_dup_p[3]->win_pd_info[ii], &field_pd_th); //cal parameters of di_buf_dup_p[2]
                pattern_check_pre_2(ii+1, &di_buf->di_buf_dup_p[3]->win_pd_info[ii],
                     &di_buf->di_buf_dup_p[2]->win_pd_info[ii],
                    &di_buf->di_buf_dup_p[1]->win_pd_info[ii],
                    &(di_buf->di_buf_dup_p[2]->win_pd_mode[ii]),
                    &(di_buf->di_buf_dup_p[1]->win_pd_mode[ii]),
                    &(win_pd_type[ii]), &win_pd_th[ii]
                  );
              }
    }
    else{
    //3 buffers
            cal_pd_parameters(&di_buf->di_buf_dup_p[1]->field_pd_info, &di_buf->di_buf_dup_p[0]->field_pd_info, &di_buf->di_buf_dup_p[2]->field_pd_info, &field_pd_th); //cal parameters of di_buf_dup_p[1]
            pattern_check_pre_2(0, &di_buf->di_buf_dup_p[2]->field_pd_info,
                 &di_buf->di_buf_dup_p[1]->field_pd_info,
                &di_buf->di_buf_dup_p[0]->field_pd_info,
                (int *)&di_buf->di_buf_dup_p[1]->pulldown_mode,
                NULL, &pulldown_type, &field_pd_th);

              for(ii=0;ii<MAX_WIN_NUM; ii++){
                cal_pd_parameters(&di_buf->di_buf_dup_p[1]->win_pd_info[ii], &di_buf->di_buf_dup_p[0]->win_pd_info[ii], &di_buf->di_buf_dup_p[2]->win_pd_info[ii], &field_pd_th); //cal parameters of di_buf_dup_p[1]
                pattern_check_pre_2(ii+1, &di_buf->di_buf_dup_p[2]->win_pd_info[ii],
                     &di_buf->di_buf_dup_p[1]->win_pd_info[ii],
                    &di_buf->di_buf_dup_p[0]->win_pd_info[ii],
                    &(di_buf->di_buf_dup_p[1]->win_pd_mode[ii]),
                    NULL,&(win_pd_type[ii]), &win_pd_th[ii]
                  );
              }
    }
    pulldown_mode_ret = pulldown_mode2 = detect_pd32();

    if(di_log_flag&DI_LOG_PULLDOWN)
    {
        di_buf_t* dp = di_buf->di_buf_dup_p[1];
        di_print("%02d (%x%x%x) %08x %06x %08x %06x %02x %02x %02x %02x %02x %02x %02x %02x ", dp->seq%100,
                dp->pulldown_mode<0?0xf:dp->pulldown_mode, pulldown_type<0?0xf:pulldown_type,
                pulldown_mode2<0?0xf:pulldown_mode2,
                dp->field_pd_info.frame_diff, dp->field_pd_info.frame_diff_num,
              dp->field_pd_info.field_diff, dp->field_pd_info.field_diff_num,
              dp->field_pd_info.frame_diff_by_pre, dp->field_pd_info.frame_diff_num_by_pre,
              dp->field_pd_info.field_diff_by_pre, dp->field_pd_info.field_diff_num_by_pre,
              dp->field_pd_info.field_diff_by_next, dp->field_pd_info.field_diff_num_by_next,
              dp->field_pd_info.frame_diff_skew_ratio, dp->field_pd_info.frame_diff_num_skew_ratio);
        for(ii=0; ii<MAX_WIN_NUM; ii++){
            di_print("(%x,%x) %08x %06x %08x %06x %02x %02x %02x %02x %02x %02x %02x %02x ", dp->win_pd_mode[ii]<0?0xf:dp->win_pd_mode[ii],
                 win_pd_type[ii]<0?0xf:win_pd_type[ii],
                dp->win_pd_info[ii].frame_diff, dp->win_pd_info[ii].frame_diff_num,
              dp->win_pd_info[ii].field_diff, dp->win_pd_info[ii].field_diff_num,
              dp->win_pd_info[ii].frame_diff_by_pre, dp->win_pd_info[ii].frame_diff_num_by_pre,
              dp->win_pd_info[ii].field_diff_by_pre, dp->win_pd_info[ii].field_diff_num_by_pre,
              dp->win_pd_info[ii].field_diff_by_next, dp->win_pd_info[ii].field_diff_num_by_next,
              dp->win_pd_info[ii].frame_diff_skew_ratio, dp->win_pd_info[ii].frame_diff_num_skew_ratio);
        }
        di_print("\n");
    }

    di_buf->pulldown_mode = PULL_DOWN_NORMAL;
    if(pulldown_detect){
        if(pulldown_detect&0x1){
            di_buf->pulldown_mode = di_buf->di_buf_dup_p[1]->pulldown_mode; //used by de_post_process
        }
        if(pulldown_detect&0x10){
            if((pulldown_mode2 >=0) && (pd_detect_rst > 15)){
                di_buf->pulldown_mode = pulldown_mode2;
            }
        }
    if(pd_detect_rst <= 32)
        pd_detect_rst ++;


        if((pulldown_win_mode&0xfffff)!=0){
            int ii;
            unsigned mode;
            for(ii=0; ii<5; ii++){
                mode = (pulldown_win_mode>>(ii*4))&0xf;
                if(mode==1){
                    if(di_buf->di_buf_dup_p[1]->pulldown_mode == 0){
                        if((di_buf->di_buf_dup_p[1]->win_pd_info[ii].field_diff_num*win_pd_th[ii].field_diff_num_th) >=
                            pd_win_prop[ii].pixels_num){
                            break;
                        }
                    }
                    else{
                        if((di_buf->di_buf_dup_p[2]->win_pd_info[ii].field_diff_num*win_pd_th[ii].field_diff_num_th) >=
                            pd_win_prop[ii].pixels_num){
                            break;
                        }
                    }
                    if ((ii!= 0) &&
                        (ii!= 5) &&
                        (ii!= 4) &&
                        (pulldown_mode2 == 1) &&
                        (((di_buf->di_buf_dup_p[2]->win_pd_info[ii+1].field_diff_num*100)  < di_buf->di_buf_dup_p[1]->win_pd_info[ii+1].field_diff_num) &&
                         ((di_buf->di_buf_dup_p[2]->win_pd_info[ii-1].field_diff_num*100)  < di_buf->di_buf_dup_p[1]->win_pd_info[ii-1].field_diff_num) &&
                         ((di_buf->di_buf_dup_p[2]->win_pd_info[ii  ].field_diff_num*100) >= di_buf->di_buf_dup_p[1]->win_pd_info[ii  ].field_diff_num)
                        )
                       )
                       {
                            di_print("out %x %06x %06x \n",
                                     ii,
                                     di_buf->di_buf_dup_p[2]->win_pd_info[ii].field_diff_num,
                                     di_buf->di_buf_dup_p[1]->win_pd_info[ii].field_diff_num);
		                        pd_detect_rst =0;
		                        break;
	                    }
                    if ((ii!= 0) &&
                        (ii!= 5) &&
                        (ii!= 4) &&
                        (pulldown_mode2 == 0) &&
                        ((di_buf->di_buf_dup_p[1]->win_pd_info[ii+1].field_diff_num*100)<di_buf->di_buf_dup_p[1]->win_pd_info[ii].field_diff_num) &&
                        ((di_buf->di_buf_dup_p[1]->win_pd_info[ii-1].field_diff_num*100)<di_buf->di_buf_dup_p[1]->win_pd_info[ii].field_diff_num)
                       )
                       {
                           pd_detect_rst =0;
                           break;
                       }
                }
                else if(mode==2){
                    if( (pulldown_type == 0) &&
                        (di_buf->di_buf_dup_p[1]->win_pd_info[ii].field_diff_num_pattern
                            != di_buf->di_buf_dup_p[1]->field_pd_info.field_diff_num_pattern )
                            ){
                        break;
                    }
                    if( (pulldown_type == 1) &&
                        (di_buf->di_buf_dup_p[1]->win_pd_info[ii].frame_diff_num_pattern
                            != di_buf->di_buf_dup_p[1]->field_pd_info.frame_diff_num_pattern )
                            ){
                        break;
                    }
                }
                else if(mode==3){
                    if((di_buf->di_buf_dup_p[1]->win_pd_mode[ii]!= di_buf->di_buf_dup_p[1]->pulldown_mode)
                        ||(pulldown_type!=win_pd_type[ii])){
                        break;
                    }
                }
            }
            if(ii<5){
                di_buf->pulldown_mode = PULL_DOWN_NORMAL;
                if(mode==1){
               //     printk("Deinterlace pulldown %s, win%d pd field_diff_num %08x/%08x is too big\n",
               //         (pulldown_type==0)?"2:2":"3:2", ii, pd_win_prop[ii].pixels_num,
               //         (di_buf->di_buf_dup_p[1]->pulldown_mode==0)? di_buf->di_buf_dup_p[1]->win_pd_info[ii].field_diff_num:di_buf->di_buf_dup_p[2]->win_pd_info[ii].field_diff_num
               //             );
                }
                else if(mode==2){
             //       printk("Deinterlace pulldown %s, win%d pd pattern %08x is different from field pd pattern %08x\n",
             //           (pulldown_type==0)?"2:2":"3:2", ii,
             //           (pulldown_type==0)? di_buf->di_buf_dup_p[1]->win_pd_info[ii].field_diff_num_pattern:di_buf->di_buf_dup_p[1]->win_pd_info[ii].frame_diff_num_pattern,
             //           (pulldown_type==0)? di_buf->di_buf_dup_p[1]->field_pd_info.field_diff_num_pattern:di_buf->di_buf_dup_p[1]->field_pd_info.frame_diff_num_pattern
             //              );
                }
                else{
             //      printk("Deinterlace pulldown, win%d pd type (%d, %d) is different from field pd type (%d, %d)\n",
             //           ii, di_buf->di_buf_dup_p[1]->win_pd_mode[ii], win_pd_type[ii],
             //           di_buf->di_buf_dup_p[1]->pulldown_mode, pulldown_type);
                }
            }
        }
        /*

        if(di_buf->pulldown_mode>=0)
             printk("%s pulldown\n", (pulldown_type==0)?"2:2":"m:n");

        if((di_buf->vframe->tvin_sig_fmt == TVIN_SIG_FMT_CVBS_PAL_I)
                && (pulldown_type == 1)){
                    //m:n not do
                 di_buf->pulldown_mode = -1;
                 printk("m:n ignore\n");
        }

        if((di_buf->vframe->tvin_sig_fmt == TVIN_SIG_FMT_CVBS_NTSC_M)
                && (pulldown_type == 0)){
                    //2:2 not do
                 di_buf->pulldown_mode = -1;
                 printk("2:2 ignore\n");

        }
        */
        if(di_buf->pulldown_mode != -1){
            pulldown_count++;
        }
#if defined(NEW_DI_TV)
       if(di_buf->vframe->source_type == VFRAME_SOURCE_TYPE_TUNER)                      {
                 di_buf->pulldown_mode = PULL_DOWN_NORMAL;
                 //printk("2:2 ignore\n");
        }
#endif
    }
    return pulldown_mode_ret;
}
#ifdef FORCE_BOB_SUPPORT
static void force_bob_vframe(di_buf_t* di_buf)
{

    ulong fiq_flag=0, irq_flag2=0;
    di_buf->vframe->type = VIDTYPE_PROGRESSIVE| VIDTYPE_VIU_422 | VIDTYPE_VIU_SINGLE_PLANE | VIDTYPE_VIU_FIELD;
    if((force_bob_flag==1)||(force_bob_flag==2)){
        di_buf->vframe->duration<<=1;
    }
    di_buf->vframe->private_data = di_buf;
    if(di_buf->di_buf_dup_p[1]->new_format_flag){ //if(di_buf->di_buf_dup_p[1]->post_proc_flag == 2){
        di_buf->vframe->early_process_fun = de_post_disable_fun;
    }
    else{
        di_buf->vframe->early_process_fun = do_nothing_fun;
    }
    di_buf->pulldown_mode = PULL_DOWN_EI;
    di_buf->vframe->process_fun = de_post_process;
    di_buf->process_fun_index = PROCESS_FUN_DI;
    inc_post_ref_count(di_buf);

    di_buf->di_buf[0] = di_buf->di_buf_dup_p[0];
    di_buf->di_buf[1] = NULL;
    queue_out(di_buf->di_buf[0]);
    if(frame_count == 0){
        di_post_stru.start_pts = di_buf->vframe->pts;
    }
    di_lock_irqfiq_save(irq_flag2, fiq_flag);
    if((frame_count<start_frame_drop_count) ||
       (((di_buf->di_buf_dup_p[1]->vframe->type & VIDTYPE_TYPEMASK)==VIDTYPE_INTERLACE_TOP)
            && ((force_bob_flag&1)==0)) ||
       (((di_buf->di_buf_dup_p[1]->vframe->type & VIDTYPE_TYPEMASK)==VIDTYPE_INTERLACE_BOTTOM)
            && ((force_bob_flag&2)==0))) {

        queue_in(di_buf, QUEUE_TMP);
        recycle_vframe_type_post(di_buf);
#ifdef DI_DEBUG
        recycle_vframe_type_post_print(di_buf, __func__, __LINE__);
#endif
    } else {
    	if(frame_count == start_frame_drop_count){
            if((di_post_stru.start_pts != 0) && (di_buf->vframe->pts == 0))
                di_buf->vframe->pts = di_post_stru.start_pts;
            di_post_stru.start_pts = 0;
        }
        queue_in(di_buf, QUEUE_POST_READY);
    }
    di_unlock_irqfiq_restore(irq_flag2, fiq_flag);
}
#endif
static int process_post_vframe(void)
{
/*
   1) get buf from post_free_list, config it according to buf in pre_ready_list, send it to post_ready_list
        (it will be send to post_free_list in di_vf_put())
   2) get buf from pre_ready_list, attach it to buf from post_free_list
        (it will be send to recycle_list in di_vf_put() )
*/
    ulong fiq_flag=0, irq_flag2=0;
    int i,pulldown_mode_hise = 0;
    int ret = 0;
    int buffer_keep_count = 3;
    di_buf_t* di_buf = NULL;
    di_buf_t *ready_di_buf;
    di_buf_t *p = NULL;//, *ptmp;
    int itmp;
    int ready_count = list_count(QUEUE_PRE_READY);
    bool check_drop = false;
    if(queue_empty(QUEUE_POST_FREE)){
        return 0;
    }
    if(ready_count>0){
        ready_di_buf = get_di_buf_head(QUEUE_PRE_READY);
        if((ready_di_buf == NULL)||(ready_di_buf->vframe == NULL)){
#ifdef DI_DEBUG
            printk("%s:Error1\n", __func__);
#endif
            if(recovery_flag==0){
                recovery_log_reason = 16;
            }
            recovery_flag++;
            return 0;
        }

        if((ready_di_buf->post_proc_flag)&&(ready_count>=buffer_keep_count)){
            i = 0;
            queue_for_each_entry(p, ptmp, QUEUE_PRE_READY, list) {
                //if(p->post_proc_flag == 0){
                if(p->type == VFRAME_TYPE_IN){
                    ready_di_buf->post_proc_flag = -1;
                    ready_di_buf->new_format_flag = 1;
                }
                i++;
                if(i>2)break;
            }
        }

        if(ready_di_buf->post_proc_flag>0){
            if(ready_count>=buffer_keep_count){
                di_lock_irqfiq_save(irq_flag2, fiq_flag);
                di_buf = get_di_buf_head(QUEUE_POST_FREE);
                if(check_di_buf(di_buf, 17)){
                    return 0;
                }
                queue_out(di_buf);
                di_unlock_irqfiq_restore(irq_flag2, fiq_flag);

                i = 0;
                queue_for_each_entry(p, ptmp, QUEUE_PRE_READY, list) {
                    di_buf->di_buf_dup_p[i++] = p;
                    if(i>=buffer_keep_count)
                        break;
                }
                if(i<buffer_keep_count){
#ifdef DI_DEBUG
                    printk("%s:Error3\n", __func__);
#endif
                    if(recovery_flag==0){
                        recovery_log_reason = 18;
                    }
                    recovery_flag++;
                    return 0;
                }

                memcpy(di_buf->vframe, di_buf->di_buf_dup_p[1]->vframe, sizeof(vframe_t));
                di_buf->vframe->private_data = di_buf;
#ifdef D2D3_SUPPORT
                if(d2d3_enable){
                    di_buf->dp_buf_adr = di_buf->di_buf_dup_p[1]->dp_buf_adr;
                    di_buf->dp_buf_size = di_buf->di_buf_dup_p[1]->dp_buf_size;
                }
                else{
                    di_buf->dp_buf_adr = 0;
                    di_buf->dp_buf_size = 0;
                }
#endif
                if(di_buf->di_buf_dup_p[1]->post_proc_flag == 3){//dummy, not for display
               	    inc_post_ref_count(di_buf);
                    di_buf->di_buf[0] = di_buf->di_buf_dup_p[0];
                    di_buf->di_buf[1] = NULL;
                    queue_out(di_buf->di_buf[0]);
                    di_lock_irqfiq_save(irq_flag2, fiq_flag);
                    queue_in(di_buf, QUEUE_TMP);
                    recycle_vframe_type_post(di_buf);

                    di_unlock_irqfiq_restore(irq_flag2, fiq_flag);

#ifdef DI_DEBUG
                    di_print("%s <dummy>: ", __func__);
#endif
                }
                else{
                    if(di_buf->di_buf_dup_p[1]->post_proc_flag == 2){
                        reset_pulldown_state();
                        di_buf->pulldown_mode = 1; /* blend with di_buf->di_buf_dup_p[2] */
                    }
                    else{
                        pulldown_mode_hise = pulldown_process(di_buf, buffer_keep_count);
                    }
#ifdef FORCE_BOB_SUPPORT
        /*added for hisense*/
                    if(pd_enable){
                        if(pulldown_mode_hise == 2)
                            force_bob_flag = 1;
                        else
                            force_bob_flag = 0;
                    }
                    if(force_bob_flag!=0){
                        force_bob_vframe(di_buf);
                        frame_count++;
                    }
                    else
#endif
                    {
                        di_buf->vframe->type = VIDTYPE_PROGRESSIVE| VIDTYPE_VIU_422 | VIDTYPE_VIU_SINGLE_PLANE | VIDTYPE_VIU_FIELD;
                        if(di_buf->di_buf_dup_p[1]->new_format_flag){ //if(di_buf->di_buf_dup_p[1]->post_proc_flag == 2){
                            di_buf->vframe->early_process_fun = de_post_disable_fun;
                        }
                        else{
                            di_buf->vframe->early_process_fun = do_nothing_fun;
                        }

                        if(di_buf->di_buf_dup_p[1]->type == VFRAME_TYPE_IN){ /* next will be bypass */
                            di_buf->vframe->type = VIDTYPE_PROGRESSIVE | VIDTYPE_VIU_422 | VIDTYPE_VIU_SINGLE_PLANE | VIDTYPE_VIU_FIELD;
                            di_buf->vframe->height >>= 1;
                            di_buf->vframe->canvas0Addr = di_buf->di_buf_dup_p[0]->nr_canvas_idx; //top
                            di_buf->vframe->canvas1Addr = di_buf->di_buf_dup_p[0]->nr_canvas_idx;
                            di_buf->vframe->process_fun = NULL;
                            di_buf->process_fun_index = PROCESS_FUN_NULL;
                        }
                        /*
                        else if(di_buf->pulldown_mode >= 0){
                            di_buf->vframe->process_fun = de_post_process;
                            di_buf->process_fun_index = PROCESS_FUN_DI;
                            inc_post_ref_count(di_buf);
                        }
                        */
                        else{
                            di_buf->vframe->process_fun = de_post_process;
                            di_buf->process_fun_index = PROCESS_FUN_DI;
                            inc_post_ref_count(di_buf);
                        }
                        di_buf->di_buf[0] = di_buf->di_buf_dup_p[0];
                        di_buf->di_buf[1] = NULL;
                        queue_out(di_buf->di_buf[0]);
			if(frame_count == 0){
                            di_post_stru.start_pts = di_buf->vframe->pts;
                    	}
                        di_lock_irqfiq_save(irq_flag2, fiq_flag);
                        if((frame_count<start_frame_drop_count)||
                            (di_buf->di_buf_dup_p[0]->throw_flag)||(di_buf->di_buf_dup_p[1]->throw_flag)||
                            (di_buf->di_buf_dup_p[2]->throw_flag)){
                            queue_in(di_buf, QUEUE_TMP);
                            recycle_vframe_type_post(di_buf);
#ifdef DI_DEBUG
                            recycle_vframe_type_post_print(di_buf, __func__, __LINE__);
#endif
                        }
                        else{
			    if(frame_count == start_frame_drop_count){
                                if((di_post_stru.start_pts != 0) && (di_buf->vframe->pts == 0))
                                    di_buf->vframe->pts = di_post_stru.start_pts;
                                di_post_stru.start_pts = 0;
                            }
                            queue_in(di_buf, QUEUE_POST_READY);
                        }
                        frame_count++;
                        di_unlock_irqfiq_restore(irq_flag2, fiq_flag);
                    }

#ifdef DI_DEBUG
                    di_print("%s <interlace>: ", __func__);
#endif
                    vf_notify_receiver(VFM_NAME,VFRAME_EVENT_PROVIDER_VFRAME_READY,NULL);
                }
                ret = 1;
            }
        }
        else{
#ifdef DET3D
            if((ready_di_buf->vframe->trans_fmt == 0)&&bypass_post_state){
                if(det3d_en && di_pre_stru.det3d_trans_fmt != 0){
                    ready_di_buf->vframe->trans_fmt = di_pre_stru.det3d_trans_fmt;
                    set3d_view(di_pre_stru.det3d_trans_fmt, ready_di_buf->vframe);
                }
            }
#endif
            if(is_progressive(ready_di_buf->vframe)||
                ready_di_buf->type == VFRAME_TYPE_IN ||
                ready_di_buf->post_proc_flag <0 ||
                bypass_post_state
                ){
                int vframe_process_count = 1;
#ifdef DET3D
                int dual_vframe_flag = 0;
                if((di_pre_stru.vframe_interleave_flag && ready_di_buf->left_right)||(bypass_post&0x100)){
                    dual_vframe_flag = 1;
                    vframe_process_count = 2;
                }
#endif
                if(skip_top_bot && (!is_progressive(ready_di_buf->vframe))){
                    vframe_process_count = 2;
                }

                if(ready_count >= vframe_process_count){
                    di_buf_t* di_buf_i;
                    di_lock_irqfiq_save(irq_flag2, fiq_flag);
                    di_buf = get_di_buf_head(QUEUE_POST_FREE);

                    if(check_di_buf(di_buf, 19)){
                        return 0;
                    }
                    queue_out(di_buf);
                    di_unlock_irqfiq_restore(irq_flag2, fiq_flag);

                    i = 0;
                    queue_for_each_entry(p, ptmp, QUEUE_PRE_READY, list) {
                        di_buf->di_buf_dup_p[i++] = p;
                        if(i>=vframe_process_count) {
                       	    di_buf->di_buf_dup_p[i] = NULL;
                            break;
                        }
                    }
                    if(i<vframe_process_count){
#ifdef DI_DEBUG
                        printk("%s:Error6\n", __func__);
#endif
                        if(recovery_flag==0){
                            recovery_log_reason = 22;
                        }
                        recovery_flag++;
                        return 0;
                    }

                    di_buf_i = di_buf->di_buf_dup_p[0];
                    if(!is_progressive(ready_di_buf->vframe)){
                        if(skip_top_bot == 1){
    	    		                di_buf_i = (di_buf->di_buf_dup_p[1]->vframe->type & VIDTYPE_TYPEMASK)==VIDTYPE_INTERLACE_TOP
    	    		                        ? di_buf->di_buf_dup_p[1] : di_buf->di_buf_dup_p[0];
                        }
                        else if(skip_top_bot == 2){
    	    		                di_buf_i = (di_buf->di_buf_dup_p[1]->vframe->type & VIDTYPE_TYPEMASK)==VIDTYPE_INTERLACE_BOTTOM
    	    		                        ? di_buf->di_buf_dup_p[1] : di_buf->di_buf_dup_p[0];
                        }
                    }

                    memcpy(di_buf->vframe, di_buf_i->vframe, sizeof(vframe_t));
                    di_buf->vframe->private_data = di_buf;
#ifdef D2D3_SUPPORT
                    if((ready_di_buf->type == VFRAME_TYPE_IN)||(d2d3_enable == 0)){
                        di_buf->dp_buf_adr = 0;
                        di_buf->dp_buf_size = 0;
                    }
                    else{
                        di_buf->dp_buf_adr = ready_di_buf->dp_buf_adr;
                        di_buf->dp_buf_size = ready_di_buf->dp_buf_size;
                    }
#endif

                    if(ready_di_buf->new_format_flag && (ready_di_buf->type == VFRAME_TYPE_IN)){
                        di_buf->vframe->early_process_fun = de_post_disable_fun;
                    }
                    else{
                        if(ready_di_buf->type == VFRAME_TYPE_IN)
                            di_buf->vframe->early_process_fun = do_nothing_fun;
                        else{
                            di_buf->vframe->early_process_fun = do_pre_only_fun;
                          }
                    }
                    di_buf->vframe->process_fun = NULL;
                    di_buf->process_fun_index = PROCESS_FUN_NULL;
                    di_buf->di_buf[0] = ready_di_buf;
                    di_buf->di_buf[1] = NULL;
                    queue_out(ready_di_buf);

#ifdef DET3D
                    if(dual_vframe_flag){
                        di_buf->di_buf[1] = di_buf->di_buf_dup_p[1];
                        queue_out(di_buf->di_buf[1]);
                    }
#endif
                    di_lock_irqfiq_save(irq_flag2, fiq_flag);
                    if((check_start_drop_prog && is_progressive(ready_di_buf->vframe))||
                    	  !is_progressive(ready_di_buf->vframe))
                        check_drop = true;
                    if(check_drop && (frame_count == 0)){
                        di_post_stru.start_pts = di_buf->vframe->pts;
                    }

                    if((check_drop &&(frame_count<start_frame_drop_count))||
                        (di_buf->di_buf[0]->throw_flag)){
                        queue_in(di_buf, QUEUE_TMP);
                        recycle_vframe_type_post(di_buf);
#ifdef DI_DEBUG
                        recycle_vframe_type_post_print(di_buf, __func__, __LINE__);
#endif
                    } else {
                        if(check_drop && (frame_count == start_frame_drop_count)){
                            if((di_post_stru.start_pts != 0) && (di_buf->vframe->pts == 0))
                                di_buf->vframe->pts = di_post_stru.start_pts;
                            di_post_stru.start_pts = 0;
                        }
                        queue_in(di_buf, QUEUE_POST_READY);
                    }
                    frame_count++;
                    di_unlock_irqfiq_restore(irq_flag2, fiq_flag);

#ifdef DI_DEBUG
                    di_print("%s <prog by frame>: ", __func__);
#endif
                    ret = 1;
                    vf_notify_receiver(VFM_NAME,VFRAME_EVENT_PROVIDER_VFRAME_READY,NULL);
                }
            }
            else if(ready_count>=2){ /*for progressive input,type 1:separate tow fields,type 2:bypass post as frame*/
                unsigned char prog_tb_field_proc_type = (prog_proc_config>>1)&0x3;
                di_lock_irqfiq_save(irq_flag2, fiq_flag);
                di_buf = get_di_buf_head(QUEUE_POST_FREE);

                if(check_di_buf(di_buf, 20)){
                    return 0;
                }

                queue_out(di_buf);
                di_unlock_irqfiq_restore(irq_flag2, fiq_flag);

                i = 0;
                queue_for_each_entry(p, ptmp, QUEUE_PRE_READY, list) {
                    di_buf->di_buf_dup_p[i++] = p;
                    if(i>=2) {
                    	di_buf->di_buf_dup_p[i] = NULL;
                        break;
                    }
                }
                if(i<2){
#ifdef DI_DEBUG
                    printk("%s:Error6\n", __func__);
#endif
                    if(recovery_flag==0){
                        recovery_log_reason = 21;
                    }
                    recovery_flag++;
                    return 0;
                }

                memcpy(di_buf->vframe, di_buf->di_buf_dup_p[0]->vframe, sizeof(vframe_t));
                di_buf->vframe->private_data = di_buf;
#ifdef D2D3_SUPPORT
                if(d2d3_enable){
                    di_buf->dp_buf_adr = di_buf->di_buf_dup_p[0]->dp_buf_adr;
                    di_buf->dp_buf_size = di_buf->di_buf_dup_p[0]->dp_buf_size;
                }
                else{
                    di_buf->dp_buf_adr = 0;
                    di_buf->dp_buf_size = 0;
                }
#endif
		/*separate one progressive frame as two interlace fields*/
                if(prog_tb_field_proc_type == 1){
					//do weave by di post
                    di_buf->vframe->type = VIDTYPE_PROGRESSIVE| VIDTYPE_VIU_422 | VIDTYPE_VIU_SINGLE_PLANE | VIDTYPE_VIU_FIELD;
                    if(di_buf->di_buf_dup_p[0]->new_format_flag){
                        di_buf->vframe->early_process_fun = de_post_disable_fun;
                    }
                    else{
                        di_buf->vframe->early_process_fun = do_nothing_fun;
                    }
                    di_buf->pulldown_mode = PULL_DOWN_BUF1;
                    di_buf->vframe->process_fun = de_post_process;
                    di_buf->process_fun_index = PROCESS_FUN_DI;
                }
                else if(prog_tb_field_proc_type == 0){
                    //to do: need change for DI_USE_FIXED_CANVAS_IDX
                    //do weave by vpp
                    di_buf->vframe->type = VIDTYPE_PROGRESSIVE| VIDTYPE_VIU_422 | VIDTYPE_VIU_SINGLE_PLANE; // top and bot are in separate buffer, do not set VIDTYPE_VIU_FIELD
                    if((di_buf->di_buf_dup_p[0]->new_format_flag)
                        ||(Rd(DI_IF1_GEN_REG)&1)){
                        di_buf->vframe->early_process_fun = de_post_disable_fun;
                    }
                    else{
                        di_buf->vframe->early_process_fun = do_nothing_fun;
                    }
                    di_buf->vframe->process_fun = NULL;
                    di_buf->process_fun_index = PROCESS_FUN_NULL;
                    di_buf->vframe->canvas0Addr = di_buf->di_buf_dup_p[0]->nr_canvas_idx; //top
                    di_buf->vframe->canvas1Addr = di_buf->di_buf_dup_p[1]->nr_canvas_idx; //bot
                }
                else{
                    //to do: need change for DI_USE_FIXED_CANVAS_IDX
                    di_buf->vframe->type = VIDTYPE_PROGRESSIVE | VIDTYPE_VIU_422 | VIDTYPE_VIU_SINGLE_PLANE | VIDTYPE_VIU_FIELD;
                    di_buf->vframe->height >>= 1;
                    if((di_buf->di_buf_dup_p[0]->new_format_flag)
                        ||(Rd(DI_IF1_GEN_REG)&1)){
                        di_buf->vframe->early_process_fun = de_post_disable_fun;
                    }
                    else{
                        di_buf->vframe->early_process_fun = do_nothing_fun;
                    }
                    if(prog_tb_field_proc_type == 2){
                    di_buf->vframe->canvas0Addr = di_buf->di_buf_dup_p[0]->nr_canvas_idx; //top
                        di_buf->vframe->canvas1Addr = di_buf->di_buf_dup_p[0]->nr_canvas_idx;
                    }
                    else{
                        di_buf->vframe->canvas0Addr = di_buf->di_buf_dup_p[1]->nr_canvas_idx; //top
                        di_buf->vframe->canvas1Addr = di_buf->di_buf_dup_p[1]->nr_canvas_idx;
                    }
                }

                di_buf->di_buf[0] = di_buf->di_buf_dup_p[0];
                queue_out(di_buf->di_buf[0]);
                /*check if the field is error,then drop*/
		if((di_buf->di_buf_dup_p[0]->vframe->type & VIDTYPE_TYPEMASK)==VIDTYPE_INTERLACE_BOTTOM){
               	    di_buf->di_buf[1] = di_buf->di_buf_dup_p[1] = NULL;
               	    di_lock_irqfiq_save(irq_flag2, fiq_flag);
               	    queue_in(di_buf, QUEUE_TMP);
                    recycle_vframe_type_post(di_buf);
                    printk("%s drop field %d.\n",__func__,di_buf->di_buf_dup_p[0]->seq);
                }else{ 
                    di_buf->di_buf[1] = di_buf->di_buf_dup_p[1];
                    queue_out(di_buf->di_buf[1]);
	
                    di_lock_irqfiq_save(irq_flag2, fiq_flag);
                    if((frame_count == 0) && check_start_drop_prog){
                        di_post_stru.start_pts = di_buf->vframe->pts;
                    }
                    if(((frame_count<start_frame_drop_count) && check_start_drop_prog)||
                        (di_buf->di_buf_dup_p[0]->throw_flag)||(di_buf->di_buf_dup_p[1]->throw_flag)){
                        queue_in(di_buf, QUEUE_TMP);
                        recycle_vframe_type_post(di_buf);
#ifdef DI_DEBUG
                        recycle_vframe_type_post_print(di_buf, __func__, __LINE__);
#endif
                    }
                    else{
                        if((frame_count == start_frame_drop_count) && check_start_drop_prog){
                            if((di_post_stru.start_pts != 0) && (di_buf->vframe->pts == 0))
                                di_buf->vframe->pts = di_post_stru.start_pts;
                            di_post_stru.start_pts = 0;
                        }
                        queue_in(di_buf, QUEUE_POST_READY);
                   }
                }
                frame_count++;
                di_unlock_irqfiq_restore(irq_flag2, fiq_flag);
#ifdef DI_DEBUG
                di_print("%s <prog by field>: ", __func__);
#endif
                ret = 1;
                vf_notify_receiver(VFM_NAME,VFRAME_EVENT_PROVIDER_VFRAME_READY,NULL);
            }
        }
#ifdef DI_DEBUG
        if(di_buf){
            di_print("%s[%d](", vframe_type_name[di_buf->type], di_buf->index);
            for(i=0; i< 2; i++){
                if(di_buf->di_buf[i])
                    di_print("%s[%d],",vframe_type_name[di_buf->di_buf[i]->type], di_buf->di_buf[i]->index);
            }
            di_print(")(vframe type %x dur %d)", di_buf->vframe->type, di_buf->vframe->duration);
            if(di_buf->di_buf_dup_p[1]&&(di_buf->di_buf_dup_p[1]->post_proc_flag == 3)){
                di_print("=> recycle_list\n");
            }
            else{
                di_print("=> post_ready_list\n");
            }
        }
#endif
    }
    return ret;
}

/*
di task
*/
static void di_unreg_process(void)
{
        if((di_pre_stru.unreg_req_flag||di_pre_stru.force_unreg_req_flag||di_pre_stru.disable_req_flag)&&
            (di_pre_stru.pre_de_busy==0)){
            //printk("===unreg_req_flag\n");
#ifdef RUN_REG_IN_IRQ
            if(init_flag){
                init_flag = 0;
                vf_unreg_provider(&di_vf_prov);
                di_pre_stru.unreg_req_flag2 = 1;
                trigger_pre_di_process('u');
            }
            else{
                di_pre_stru.force_unreg_req_flag = 0;
                di_pre_stru.disable_req_flag = 0;
                recovery_flag = 0;
                di_pre_stru.unreg_req_flag = 0;
                //di_pre_stru.unreg_req_flag2 = 0; clear in timer irq
            }
#else
/* !RUN_REG_IN_IRQ*/

			ulong flags=0,fiq_flag=0, irq_flag2=0;
            if(di_pre_stru.force_unreg_req_flag||di_pre_stru.disable_req_flag){
#ifdef DI_DEBUG
                di_print("%s: force_unreg\n", __func__);
#endif
                printk("%s: force_unreg\n", __func__);
                goto unreg;
            }
            else{
unreg:
                if(init_flag){
                init_flag = 0;
                di_lock_irqfiq_save(irq_flag2, fiq_flag);
                vf_unreg_provider(&di_vf_prov);
                di_unlock_irqfiq_restore(irq_flag2, fiq_flag);
#if (!(defined RUN_DI_PROCESS_IN_IRQ))||(defined ENABLE_SPIN_LOCK_ALWAYS)
                spin_lock_irqsave(&plist_lock, flags);
#endif
                di_lock_irqfiq_save(irq_flag2, fiq_flag);
#ifdef DI_DEBUG
                di_print("%s: di_uninit_buf\n", __func__);
#endif
                di_uninit_buf();
                di_set_power_control(0,0);
                if(get_blackout_policy()){
                    di_set_power_control(1,0);
                }
                di_unlock_irqfiq_restore(irq_flag2, fiq_flag);

#if (!(defined RUN_DI_PROCESS_IN_IRQ))||(defined ENABLE_SPIN_LOCK_ALWAYS)
                spin_unlock_irqrestore(&plist_lock, flags);
#endif
                }
                di_pre_stru.force_unreg_req_flag = 0;
                di_pre_stru.disable_req_flag = 0;
                recovery_flag = 0;
            }
            di_pre_stru.unreg_req_flag = 0;
            di_pre_stru.unreg_req_flag2 = 0;

#endif
        }


}

#ifdef RUN_REG_IN_IRQ
static void di_unreg_process_irq(void)
{
    ulong flags=0,fiq_flag=0, irq_flag2=0;
    if(di_pre_stru.unreg_req_flag2){
#if (defined ENABLE_SPIN_LOCK_ALWAYS)
        spin_lock_irqsave(&plist_lock, flags);
#endif
        di_lock_irqfiq_save(irq_flag2, fiq_flag);
#ifdef DI_DEBUG
        di_print("%s: di_uninit_buf\n", __func__);
#endif
        di_uninit_buf();
        di_set_power_control(0,0);
        if(get_blackout_policy()){
            di_set_power_control(1,0);
        }
        di_unlock_irqfiq_restore(irq_flag2, fiq_flag);

#if (defined ENABLE_SPIN_LOCK_ALWAYS)
        spin_unlock_irqrestore(&plist_lock, flags);
#endif

        di_pre_stru.force_unreg_req_flag = 0;
        di_pre_stru.disable_req_flag = 0;
        recovery_flag = 0;

        di_pre_stru.unreg_req_flag = 0;
        di_pre_stru.unreg_req_flag2 = 0;
    }
}

#endif


static void di_reg_process_2(void)
{
    /*get vout information first time*/
    set_output_mode_info();
    vf_provider_init(&di_vf_prov, VFM_NAME, &deinterlace_vf_provider, NULL);
    vf_reg_provider(&di_vf_prov);
    vf_notify_receiver(VFM_NAME,VFRAME_EVENT_PROVIDER_START,NULL);
    init_flag = 1;
}

static void di_reg_process(void)
{
    ulong flags=0,fiq_flag=0, irq_flag2=0;
    vframe_t * vframe;

    if(init_flag == 0 && di_pre_stru.reg_flag == 0){
        if((pre_run_flag != DI_RUN_FLAG_RUN)&&(pre_run_flag != DI_RUN_FLAG_STEP)){
            return;
        }
        if(pre_run_flag == DI_RUN_FLAG_STEP)
            pre_run_flag = DI_RUN_FLAG_STEP_DONE;

        vframe = vf_peek(VFM_NAME);

        if(vframe){
            di_set_power_control(0,1);
            di_set_power_control(1,1);

/* add for di Reg re-init */
#ifdef NEW_DI_TV
di_set_para_by_tvinfo(vframe);
#else
di_load_nr_setting();
#endif
            if(di_printk_flag&2){
                di_printk_flag=1;
            }
#ifdef DI_DEBUG
            di_print("%s: vframe come => di_init_buf\n", __func__);
#endif
            if(is_progressive(vframe)&&(prog_proc_config&0x10)
                    //(
                    //(is_from_vdin(vframe)&&(prog_proc_config&0x1))||
                    //((!is_from_vdin(vframe))&&(prog_proc_config&0x10))
                    //)
                ){
#if (!(defined RUN_DI_PROCESS_IN_IRQ))||(defined ENABLE_SPIN_LOCK_ALWAYS)
                spin_lock_irqsave(&plist_lock, flags);
#endif
                di_lock_irqfiq_save(irq_flag2, fiq_flag);
                //di_init_buf(vframe->width, vframe->height, 1);
                di_init_buf(default_width, default_height, 1);
                di_unlock_irqfiq_restore(irq_flag2, fiq_flag);

#if (!(defined RUN_DI_PROCESS_IN_IRQ))||(defined ENABLE_SPIN_LOCK_ALWAYS)
                spin_unlock_irqrestore(&plist_lock, flags);
#endif
            }
            else{
#if (!(defined RUN_DI_PROCESS_IN_IRQ))||(defined ENABLE_SPIN_LOCK_ALWAYS)
                spin_lock_irqsave(&plist_lock, flags);
#endif
                di_lock_irqfiq_save(irq_flag2, fiq_flag);
                di_init_buf(default_width, default_height, 0);
                di_unlock_irqfiq_restore(irq_flag2, fiq_flag);

#if (!(defined RUN_DI_PROCESS_IN_IRQ))||(defined ENABLE_SPIN_LOCK_ALWAYS)
                spin_unlock_irqrestore(&plist_lock, flags);
#endif
            }

            reset_pulldown_state();

#ifdef RUN_REG_IN_IRQ
            di_pre_stru.reg_flag = 1;
            trigger_pre_di_process('u');
#else
            di_reg_process_2();
#endif
        }
    }

}



static void dynamic_bypass_process(void)
{
        if ((disp_frame_count > 0)&&(vdin_source_flag == 0)){
            int ready_count = list_count(QUEUE_POST_READY);
            if (bypass_dynamic_flag==0){
                if (ready_count == 0){
                    if (post_ready_empty_count < 10){
	                post_ready_empty_count++;
                    }
                    else{
		                bypass_dynamic_flag = bypass_dynamic;
                        post_ready_empty_count = 30;
                    }
                }
                else post_ready_empty_count = 0;
            }
            else{
                if (ready_count>=4){
                    post_ready_empty_count--;
                    if (post_ready_empty_count <= 0){
                        bypass_dynamic_flag = 0;
                    }
                }
                else post_ready_empty_count = 60;
            }
        }
}


static void di_process(void)
{
    ulong flags=0,fiq_flag=0, irq_flag2=0;
	/* add for di Reg re-init */
	//di_set_para_by_tvinfo(vframe);
	  di_process_cnt++;

        if((init_flag)&&(recovery_flag == 0)&&(dump_state_flag == 0)){
            if(bypass_dynamic!=0){
			    dynamic_bypass_process();
            }
#if (!(defined RUN_DI_PROCESS_IN_IRQ))||(defined ENABLE_SPIN_LOCK_ALWAYS)
            spin_lock_irqsave(&plist_lock, flags);
#endif
            if(di_pre_stru.pre_de_busy == 0){
                if(di_pre_stru.pre_de_process_done){
#ifdef CHECK_DI_DONE
/* also for NEW_DI ? 7/15/2013 */
                    unsigned int data32 = Rd(DI_INTR_CTRL); /*
                                                                DI_INTR_CTRL[bit 0], NRWR_done, set by hardware when NRWR is done, clear by write 1 by code;
                                                                            [bit 1] MTNWR_done, set by hardware when MTNWR is done, clear by write 1 by code;
                                                                these two bits have nothing to do with DI_INTR_CTRL[16](NRW irq mask, 0 to enable irq)
                                                                    and DI_INTR_CTRL[17](MTN irq mask, 0 to enable irq).
                                                                    two interrupts are raised if both DI_INTR_CTRL[16] and DI_INTR_CTRL[17] are 0
                                                            */
                    if(((data32&0x1)&&((di_pre_stru.enable_mtnwr==0)||(data32&0x2)))
                        ||(di_pre_stru.pre_de_clear_flag==2)){
                        Wr(DI_INTR_CTRL, data32);
#endif
                        pre_process_time = di_pre_stru.pre_de_busy_timer_count;
                        pre_de_done_buf_config();

                        di_pre_stru.pre_de_process_done = 0;
                        di_pre_stru.pre_de_clear_flag = 0;
                        //Wr(DI_PRE_CTRL, 0x3 << 30|Rd(DI_PRE_CTRL) & 0x14); //disable, and reset
#ifdef CHECK_DI_DONE
                    }
#endif
                }
                else if(di_pre_stru.pre_de_clear_flag==1){
                    di_lock_irqfiq_save(irq_flag2, fiq_flag);
                    pre_de_done_buf_clear();
                    di_unlock_irqfiq_restore(irq_flag2, fiq_flag);

                    di_pre_stru.pre_de_process_done = 0;
                    di_pre_stru.pre_de_clear_flag = 0;
                }
            }

            di_lock_irqfiq_save(irq_flag2, fiq_flag);
            while(check_recycle_buf()&1){};
            di_unlock_irqfiq_restore(irq_flag2, fiq_flag);


            if((di_pre_stru.pre_de_busy==0) && (di_pre_stru.pre_de_process_done==0)){
                if((pre_run_flag == DI_RUN_FLAG_RUN)||(pre_run_flag == DI_RUN_FLAG_STEP)){
                    if(pre_run_flag == DI_RUN_FLAG_STEP)
                        pre_run_flag = DI_RUN_FLAG_STEP_DONE;
                    if(pre_de_buf_config()){
#ifdef D2D3_SUPPORT
               	if(d2d3_enable){
                    vf_notify_receiver_by_name("d2d3",VFRAME_EVENT_PROVIDER_DPBUF_CONFIG, di_pre_stru.di_wr_buf->vframe);
                }
#endif
                pre_de_process();
            }
                }
            }
#ifdef NEW_DI_V1
    di_post_read_reverse(overturn);
#endif
            while(process_post_vframe()){};


#if (!(defined RUN_DI_PROCESS_IN_IRQ))||(defined ENABLE_SPIN_LOCK_ALWAYS)
            spin_unlock_irqrestore(&plist_lock, flags);
#endif
        }
}

void di_timer_handle(struct work_struct *work)
{
    if(di_pre_stru.pre_de_busy){
        di_pre_stru.pre_de_busy_timer_count++;
        if(di_pre_stru.pre_de_busy_timer_count>=100){
            di_pre_stru.pre_de_busy_timer_count = 0;
            di_pre_stru.pre_de_irq_timeout_count++;
            if(timeout_miss_policy==0){
                di_pre_stru.pre_de_process_done = 1;
                di_pre_stru.pre_de_busy = 0;
                di_pre_stru.pre_de_clear_flag = 2;
            }
            else if(timeout_miss_policy==1){
                di_pre_stru.pre_de_clear_flag = 1;
                di_pre_stru.pre_de_busy = 0;
            }
            else if(timeout_miss_policy==2){
            }
            pr_info("******* DI ********** wait pre_de_irq timeout\n");
        }
    }

#ifdef RUN_DI_PROCESS_IN_TIMER
    {
        int i;

        di_unreg_process();

        di_reg_process();

        for(i=0; i<10; i++){
            if(active_flag){
                di_process();
            }
        }
    }
#endif

    //if(force_trig){
       force_trig_cnt++;
       trigger_pre_di_process('t');
    //}

    if(force_recovery){
        if(recovery_flag||(force_recovery&0x2)){
            force_recovery_count++;
            if(init_flag){
                printk("===================DI force recovery ==================\n");
                force_recovery &= (~0x2);
                dis2_di();
                recovery_flag = 0;
            }
        }
    }

}

static int di_task_handle(void *data)
{
		int ret = 0;
    while (1)
    {

        ret = down_interruptible(&di_sema);
        if(active_flag){
#ifndef RUN_REG_IN_IRQ
            di_unreg_process();
            di_reg_process();
#else
            di_unreg_process();
            if(di_pre_stru.reg_flag){
                di_reg_process_2();
                di_pre_stru.reg_flag = 0;
            }
#endif

#if (!(defined RUN_DI_PROCESS_IN_IRQ))&&(!(defined RUN_DI_PROCESS_IN_TIMER_IRQ))
            di_process();
            log_buffer_state("pro");
#endif
        }
    }

    return 0;

}
#if (defined RUN_DI_PROCESS_IN_IRQ)||(defined RUN_DI_PROCESS_IN_TIMER_IRQ)
static irqreturn_t timer_irq(int irq, void *dev_instance)
{
   //unsigned int data32;
   int i;
#ifdef RUN_DI_PROCESS_IN_TIMER_IRQ
    if(di_pre_stru.pre_de_busy){
        di_pre_stru.pre_de_busy_timer_count++;

        if(pre_process_time_force){
            if(di_pre_stru.pre_de_busy_timer_count >= pre_process_time_force){
                di_pre_stru.pre_de_process_done = 1;
                di_pre_stru.pre_de_busy = 0;
            }
        }
    }
#endif

#ifdef RUN_REG_IN_IRQ
    if(active_flag){
        di_unreg_process_irq();
        di_reg_process();
    }
#endif

    for(i=0; i<2; i++){
        if(active_flag){
            di_process();
        }
    }
    log_buffer_state("pro");
   return IRQ_HANDLED;
}
#endif
/*
provider/receiver interface

*/

unsigned int vf_keep_current(void);
static int di_receiver_event_fun(int type, void* data, void* arg)
{
    int i;
    ulong flags;
    if(type == VFRAME_EVENT_PROVIDER_QUREY_VDIN2NR){
        return di_pre_stru.vdin2nr;
    }
    else if(type == VFRAME_EVENT_PROVIDER_UNREG){
#ifdef DI_DEBUG
        di_print("%s , is_bypass() %d trick_mode %d bypass_all %d\n", __func__, is_bypass(), trick_mode, bypass_all);
#endif
        if((Rd(DI_IF1_GEN_REG)&0x1)==0){
            //post di is disabled, so can call vf_keep_current() to keep displayed vframe
            //vf_keep_current();
        }
#ifdef DI_DEBUG
        di_print("%s: vf_notify_receiver unreg\n", __func__);
#endif
        di_pre_stru.unreg_req_flag = 1;
        provider_vframe_level = 0;
        bypass_dynamic_flag = 0;
        post_ready_empty_count = 0;

        trigger_pre_di_process('n');
        while(di_pre_stru.unreg_req_flag){
            msleep(10);
		    }
#ifdef SUPPORT_MPEG_TO_VDIN
	if(mpeg2vdin_flag){
	    vdin_arg_t vdin_arg;
	    vdin_v4l2_ops_t *vdin_ops = get_vdin_v4l2_ops();
	    vdin_arg.cmd = VDIN_CMD_MPEGIN_STOP;
	    if(vdin_ops->tvin_vdin_func){
		vdin_ops->tvin_vdin_func(0,&vdin_arg);
	    }
	    mpeg2vdin_flag = 0;
	}
#endif
        bypass_state = 1;
#ifdef RUN_DI_PROCESS_IN_IRQ
        if(vdin_source_flag){
            Wr_reg_bits(VDIN_WR_CTRL, 0x3, 24, 3);
        }
#endif
    }
    else if(type == VFRAME_EVENT_PROVIDER_RESET){
#ifdef DI_DEBUG
        di_print("%s: VFRAME_EVENT_PROVIDER_RESET\n", __func__);
#endif
        goto light_unreg;
    }
    else if(type == VFRAME_EVENT_PROVIDER_LIGHT_UNREG){
#ifdef DI_DEBUG
        di_print("%s: vf_notify_receiver ligth unreg\n", __func__);
#endif
light_unreg:
        provider_vframe_level = 0;
        bypass_dynamic_flag = 0;
        post_ready_empty_count = 0;

       spin_lock_irqsave(&plist_lock, flags);
        for(i=0; i<MAX_IN_BUF_NUM; i++){
#ifdef DI_DEBUG
            if(vframe_in[i]){
                printk("DI:clear vframe_in[%d]\n", i);
            }
#endif
            vframe_in[i] = NULL;
        }
       spin_unlock_irqrestore(&plist_lock, flags);
    }
    else if(type == VFRAME_EVENT_PROVIDER_LIGHT_UNREG_RETURN_VFRAME){
        unsigned char vf_put_flag = 0;
#ifdef DI_DEBUG
        di_print("%s: VFRAME_EVENT_PROVIDER_LIGHT_UNREG_RETURN_VFRAME\n", __func__);
#endif
        provider_vframe_level = 0;
        bypass_dynamic_flag = 0;
        post_ready_empty_count = 0;

        DisableVideoLayer(); //do not display garbage when 2d->3d or 3d->2d
       spin_lock_irqsave(&plist_lock, flags);
        for(i=0; i<MAX_IN_BUF_NUM; i++){
            if(vframe_in[i]){
                vf_put(vframe_in[i], VFM_NAME);
#ifdef DI_DEBUG
                printk("DI:clear vframe_in[%d]\n", i);
#endif
                vf_put_flag = 1;
            }
            vframe_in[i] = NULL;
        }
        if(vf_put_flag)
            vf_notify_provider(VFM_NAME, VFRAME_EVENT_RECEIVER_PUT, NULL);

       spin_unlock_irqrestore(&plist_lock, flags);
    }
    else if(type == VFRAME_EVENT_PROVIDER_VFRAME_READY){
#ifdef DI_DEBUG
        di_print("%s: vframe ready\n", __func__);
#endif
        provider_vframe_level++;
        trigger_pre_di_process('r');

#ifdef RUN_DI_PROCESS_IN_IRQ
#define INPUT2PRE_2_BYPASS_SKIP_COUNT   4
        if(active_flag && vdin_source_flag){
            if(is_bypass(NULL)){
                if(di_pre_stru.pre_de_busy == 0){
                    Wr_reg_bits(VDIN_WR_CTRL, 0x3, 24, 3);
                    di_pre_stru.vdin2nr = 0;
                }
                if(di_pre_stru.bypass_start_count<INPUT2PRE_2_BYPASS_SKIP_COUNT){
                    vframe_t* vframe_tmp = vf_get(VFM_NAME);
                    vf_put(vframe_tmp, VFM_NAME);
                    vf_notify_provider(VFM_NAME, VFRAME_EVENT_RECEIVER_PUT, NULL);
                    di_pre_stru.bypass_start_count++;
                }
            }
            else if(is_input2pre()){
                di_pre_stru.bypass_start_count = 0;
                if((di_pre_stru.pre_de_busy!=0)&&(input2pre_miss_policy==1 && frame_count<30 )){
                    di_pre_stru.pre_de_clear_flag = 1;
                    di_pre_stru.pre_de_busy = 0;
                    input2pre_buf_miss_count++;
                }

                if(di_pre_stru.pre_de_busy==0){
                    Wr_reg_bits(VDIN_WR_CTRL, 0x5, 24, 3);
                    di_pre_stru.vdin2nr = 1;
                    di_process();
                    log_buffer_state("pr_");
                    if(di_pre_stru.pre_de_busy == 0){
                        input2pre_proc_miss_count++;
                    }
                }
                else{
                    vframe_t* vframe_tmp = vf_get(VFM_NAME);
                    vf_put(vframe_tmp, VFM_NAME);
                    vf_notify_provider(VFM_NAME, VFRAME_EVENT_RECEIVER_PUT, NULL);
                    input2pre_buf_miss_count++;
                    if((di_pre_stru.cur_width>720 && di_pre_stru.cur_height>576)||
                        (input2pre_throw_count&0x10000)){
			                    di_pre_stru.pre_throw_flag = input2pre_throw_count&0xffff;
                    }
                }
            }
            else{
                if(di_pre_stru.pre_de_busy == 0){
                    Wr_reg_bits(VDIN_WR_CTRL, 0x3, 24, 3);
                    di_pre_stru.vdin2nr = 0;
                }
                di_pre_stru.bypass_start_count = INPUT2PRE_2_BYPASS_SKIP_COUNT;
            }
        }
#endif
    }
    else if(type == VFRAME_EVENT_PROVIDER_QUREY_STATE){
        int in_buf_num = 0;
        if(recovery_flag){
            return RECEIVER_INACTIVE;
        }

        for(i=0; i<MAX_IN_BUF_NUM; i++){
            if(vframe_in[i]!=NULL){
                in_buf_num++;
            }
        }
        if(bypass_state==1){
            if(in_buf_num>1){
                return RECEIVER_ACTIVE ;
            }else{
                return RECEIVER_INACTIVE;
            }
        }
        else{
            if(in_buf_num>0){
                return RECEIVER_ACTIVE ;
            }else{
                return RECEIVER_INACTIVE;
            }
        }
    }
    else if(type == VFRAME_EVENT_PROVIDER_REG){
        char* provider_name = (char*)data;
        bypass_state = 0;
#if (defined RUN_DI_PROCESS_IN_IRQ)&&(!(defined FIQ_VSYNC))
        WRITE_CBUS_REG_BITS(ISA_TIMER_MUX,0,14,1);// one time
        WRITE_CBUS_REG_BITS(ISA_TIMER_MUX,0,4,2);// 1us
        WRITE_CBUS_REG_BITS(ISA_TIMER_MUX,1,18,1);// enable timer c
        WRITE_CBUS_REG(ISA_TIMERC, 1);
#endif
        if(strncmp(provider_name, "vdin", 4)==0){
            vdin_source_flag = 1;
        }
        else{
            vdin_source_flag = 0;
            pre_urgent = 0;
        }
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
        if(strcmp(vf_get_receiver_name(VFM_NAME), "ppmgr") == 0 ){
            di_post_stru.run_early_proc_fun_flag = 1;
            receiver_is_amvideo = 0;
            //printk("set run_early_proc_fun_flag to 1\n");
        }
        else{
            di_post_stru.run_early_proc_fun_flag = 0;
            receiver_is_amvideo = 1;
        }
#endif
    }
#ifdef DET3D
    else if(type == VFRAME_EVENT_PROVIDER_SET_3D_VFRAME_INTERLEAVE){
        int flag = (int)data;
        di_pre_stru.vframe_interleave_flag = flag;
    }
#endif

    return 0;
}

static void fast_process(void)
{
	int i;
	ulong flags=0, fiq_flag=0, irq_flag2=0;
	if(active_flag&& is_bypass(NULL)&&(bypass_get_buf_threshold<=1)&&(init_flag)&&(recovery_flag == 0)&&(dump_state_flag==0)){
        if(vf_peek(VFM_NAME)==NULL){
       	    return;
       	}
				for(i=0; i<2; i++){
            spin_lock_irqsave(&plist_lock, flags);
            if(di_pre_stru.pre_de_process_done){
                pre_de_done_buf_config();

                //Wr(DI_PRE_CTRL, 0x3 << 30|Rd(DI_PRE_CTRL) & 0x14); //disable, and reset
                di_pre_stru.pre_de_process_done = 0;
            }

            di_lock_irqfiq_save(irq_flag2, fiq_flag);
            while(check_recycle_buf()&1){};
            di_unlock_irqfiq_restore(irq_flag2, fiq_flag);

            if((di_pre_stru.pre_de_busy==0) && (di_pre_stru.pre_de_process_done==0)){
                if((pre_run_flag == DI_RUN_FLAG_RUN)||(pre_run_flag == DI_RUN_FLAG_STEP)){
                    if(pre_run_flag == DI_RUN_FLAG_STEP)
                        pre_run_flag = DI_RUN_FLAG_STEP_DONE;
                    if(pre_de_buf_config()){
#ifdef D2D3_SUPPORT
               	        if(d2d3_enable){
                            vf_notify_receiver_by_name("d2d3",VFRAME_EVENT_PROVIDER_DPBUF_CONFIG, di_pre_stru.di_wr_buf->vframe);
                        }
#endif
                        pre_de_process();
                    }
                }
            }

            while(process_post_vframe()){};


            spin_unlock_irqrestore(&plist_lock, flags);

       }

   }
}

static vframe_t *di_vf_peek(void* arg)
{
    vframe_t* vframe_ret = NULL;
    di_buf_t* di_buf = NULL;
    video_peek_cnt++;
    if((init_flag == 0)||recovery_flag||di_pre_stru.unreg_req_flag||dump_state_flag)
        return NULL;
    if((run_flag == DI_RUN_FLAG_PAUSE)||
        (run_flag == DI_RUN_FLAG_STEP_DONE))
        return NULL;

    log_buffer_state("pek");

    fast_process();
#ifdef SUPPORT_START_FRAME_HOLD
    if((disp_frame_count==0)&&(is_bypass(NULL)==0)){
        int ready_count = list_count(QUEUE_POST_READY);
        if(ready_count>start_frame_hold_count){
           di_buf = get_di_buf_head(QUEUE_POST_READY);
           if(di_buf){
           vframe_ret = di_buf->vframe;
        }
    }
    }
    else
#endif
    {
    if(!queue_empty(QUEUE_POST_READY)){
       di_buf = get_di_buf_head(QUEUE_POST_READY);
       if(di_buf){
       vframe_ret = di_buf->vframe;
    }
    }
    }
#ifdef DI_DEBUG
    if(vframe_ret){
         di_print("%s: %s[%d]:%x\n", __func__, vframe_type_name[di_buf->type], di_buf->index, vframe_ret);
    }
#endif
    if(force_duration_0){
        if(vframe_ret){
            vframe_ret->duration = 0;
        }
    }
    return vframe_ret;
}
/*recycle the buffer for keeping buffer*/
static void recycle_keep_buffer(void)
{
    ulong irq_flag2=0, fiq_flag=0;
    int i=0;
    if((used_post_buf_index != -1)&&(new_keep_last_frame_enable)){
	if(di_buf_post[used_post_buf_index].type == VFRAME_TYPE_POST){
            printk("%s recycle keep cur di_buf %d (",__func__, used_post_buf_index);
            di_lock_irqfiq_save(irq_flag2, fiq_flag);
	    for(i=0;i<USED_LOCAL_BUF_MAX;i++){
		if(di_buf_post[used_post_buf_index].di_buf_dup_p[i]){
	            queue_in(di_buf_post[used_post_buf_index].di_buf_dup_p[i],QUEUE_RECYCLE);
	            printk(" %d ",di_buf_post[used_post_buf_index].di_buf_dup_p[i]->index);
	          }
		}
	    queue_in(&di_buf_post[used_post_buf_index],QUEUE_POST_FREE);
	    di_unlock_irqfiq_restore(irq_flag2, fiq_flag);
	    printk(")\n");
	}
	used_post_buf_index = -1;
    }
 }
static vframe_t *di_vf_get(void* arg)
{
    vframe_t* vframe_ret = NULL;
    di_buf_t* di_buf = NULL;
    ulong irq_flag2=0, fiq_flag=0;

    if((init_flag == 0)||recovery_flag||di_pre_stru.unreg_req_flag||dump_state_flag)
        return NULL;

    if((run_flag == DI_RUN_FLAG_PAUSE)||
        (run_flag == DI_RUN_FLAG_STEP_DONE))
        return NULL;

#ifdef SUPPORT_START_FRAME_HOLD
    if((disp_frame_count==0)&&(is_bypass(NULL)==0)){
        int ready_count = list_count(QUEUE_POST_READY);
        if(ready_count>start_frame_hold_count){
            goto get_vframe;
        }
    }
    else
#endif
    if (!queue_empty(QUEUE_POST_READY)){
#ifdef SUPPORT_START_FRAME_HOLD
get_vframe:
#endif
        log_buffer_state("ge_");
        di_lock_irqfiq_save(irq_flag2, fiq_flag);

       di_buf = get_di_buf_head(QUEUE_POST_READY);
       queue_out(di_buf);
       queue_in(di_buf, QUEUE_DISPLAY); //add it into display_list

        di_unlock_irqfiq_restore(irq_flag2, fiq_flag);

       if(di_buf){
       vframe_ret = di_buf->vframe;
        }
       disp_frame_count++;
       if(run_flag == DI_RUN_FLAG_STEP){
            run_flag = DI_RUN_FLAG_STEP_DONE;
       }
       log_buffer_state("get");
    }
#ifdef DI_DEBUG
     if(vframe_ret){
         di_print("%s: %s[%d]:%x\n", __func__, vframe_type_name[di_buf->type], di_buf->index, vframe_ret);
     }
#endif
    if(force_duration_0){
        if(vframe_ret){
            vframe_ret->duration = 0;
        }
    }

    if(di_post_stru.run_early_proc_fun_flag && vframe_ret){
        if(vframe_ret->early_process_fun == do_pre_only_fun){
            vframe_ret->early_process_fun(vframe_ret->private_data, vframe_ret);
        }
    }
    //if(vframe_ret)
       //recycle_keep_buffer();

    return vframe_ret;
}

static void di_vf_put(vframe_t *vf, void* arg)
{
    di_buf_t* di_buf = (di_buf_t*)vf->private_data;
    ulong irq_flag2=0, fiq_flag=0;
    //di_buf_t *p = NULL;
    //int itmp = 0;
    if((init_flag == 0)||recovery_flag){
#ifdef DI_DEBUG
        di_print("%s: %x\n", __func__, vf);
#endif
        return;
    }
    log_buffer_state("pu_");
    recycle_keep_buffer();
    if(di_buf->type == VFRAME_TYPE_POST){
        di_lock_irqfiq_save(irq_flag2, fiq_flag);

        if(is_in_queue(di_buf, QUEUE_DISPLAY)){
            recycle_vframe_type_post(di_buf);
        }
        di_unlock_irqfiq_restore(irq_flag2, fiq_flag);
#ifdef DI_DEBUG
        recycle_vframe_type_post_print(di_buf, __func__, __LINE__);
#endif
    }
    else{
        di_lock_irqfiq_save(irq_flag2, fiq_flag);
        queue_in(di_buf, QUEUE_RECYCLE);
        di_unlock_irqfiq_restore(irq_flag2, fiq_flag);
#ifdef DI_DEBUG
        di_print("%s: %s[%d] =>recycle_list\n", __func__, vframe_type_name[di_buf->type], di_buf->index);
#endif
    }

    trigger_pre_di_process('p');
}

static int di_event_cb(int type, void *data, void *private_data)
{
    if(type == VFRAME_EVENT_RECEIVER_FORCE_UNREG){
#ifdef DI_DEBUG
        di_print("%s: VFRAME_EVENT_RECEIVER_FORCE_UNREG\n", __func__);
#endif
        di_pre_stru.force_unreg_req_flag = 1;
        provider_vframe_level = 0;
        bypass_dynamic_flag = 0;
        post_ready_empty_count = 0;

        trigger_pre_di_process('f');
        while(di_pre_stru.force_unreg_req_flag){
            msleep(1);
        }
    }
    return 0;
}

/*****************************
*    di driver file_operations
*
******************************/
static int di_open(struct inode *node, struct file *file)
{
    di_dev_t *di_in_devp;

    /* Get the per-device structure that contains this cdev */
    di_in_devp = container_of(node->i_cdev, di_dev_t, cdev);
    file->private_data = di_in_devp;

    return 0;

}


static int di_release(struct inode *node, struct file *file)
{
    //di_dev_t *di_in_devp = file->private_data;

    /* Reset file pointer */

    /* Release some other fields */
    /* ... */
    return 0;
}


#if MESON_CPU_TYPE < MESON_CPU_TYPE_MESON6
static int di_ioctl(struct inode *node, struct file *file, unsigned int cmd,   unsigned long args)
{
    int   r = 0;
    switch (cmd) {
        default:
            break;
    }
    return r;
}
#endif

const static struct file_operations di_fops = {
    .owner    = THIS_MODULE,
    .open     = di_open,
    .release  = di_release,
#if MESON_CPU_TYPE < MESON_CPU_TYPE_MESON6
    .ioctl    = di_ioctl,
#endif
};

static ssize_t show_frame_format(struct device *dev, struct device_attribute* attr, char* buf)
{
    int ret = 0;
    if(init_flag){
	ret += sprintf(buf + ret, "%s\n", di_pre_stru.cur_prog_flag?"progressive":"interlace");
    }else{
	ret += sprintf(buf + ret, "%s\n", "null");
    }
    return ret;
}
static DEVICE_ATTR(frame_format, 0664, show_frame_format, NULL);

static struct resource memobj;
static int di_probe(struct platform_device *pdev)
{
    int r, i;
    struct resource *mem;
    int buf_num_avail;
    pr_dbg("di_probe\n");
    const void*name;
    int offset,size;
    vout_register_client(&display_mode_notifier_nb_v);

    memset(&di_post_stru, 0, sizeof(di_post_stru));
    di_post_stru.next_canvas_id = 1;
#ifdef DI_USE_FIXED_CANVAS_IDX
    di_post_buf0_canvas_idx[0] = DI_POST_BUF0_CANVAS_IDX;
    di_post_buf1_canvas_idx[0] = DI_POST_BUF1_CANVAS_IDX;
    di_post_mtncrd_canvas_idx[0] = DI_POST_MTNCRD_CANVAS_IDX;
    di_post_mtnprd_canvas_idx[0] = DI_POST_MTNPRD_CANVAS_IDX;

#ifdef CONFIG_VSYNC_RDMA
    di_post_buf0_canvas_idx[1] = DI_POST_BUF0_CANVAS_IDX2;
    di_post_buf1_canvas_idx[1] = DI_POST_BUF1_CANVAS_IDX2;
    di_post_mtncrd_canvas_idx[1] = DI_POST_MTNCRD_CANVAS_IDX2;
    di_post_mtnprd_canvas_idx[1] = DI_POST_MTNPRD_CANVAS_IDX2;
#else
    di_post_buf0_canvas_idx[1] = DI_POST_BUF0_CANVAS_IDX;
    di_post_buf1_canvas_idx[1] = DI_POST_BUF1_CANVAS_IDX;
    di_post_mtncrd_canvas_idx[1] = DI_POST_MTNCRD_CANVAS_IDX;
    di_post_mtnprd_canvas_idx[1] = DI_POST_MTNPRD_CANVAS_IDX;
#endif
#endif

    /* call di_add_reg_cfg() */
#ifdef NEW_DI_V1
	#if (defined(CONFIG_MACH_MESON6TV_H31)||defined(CONFIG_MACH_MESON6TV_H32))  //tcl
	{
		di_add_reg_cfg(&di_default_pre);
		di_add_reg_cfg(&di_default_post_hd);
		di_add_reg_cfg(&di_default_post_tuner);
		di_add_reg_cfg(&di_default_post_comp);
		di_add_reg_cfg(&di_default_post_hdmi);
		di_add_reg_cfg(&di_dtv_post_stand);
		printk("add the di_default_pre/post_hd/post_tuner/post_comp/post_hdmi/post_stand cfg \n");
	}
	#else//default
	{
    di_add_reg_cfg(&di_default_pre);
    di_add_reg_cfg(&di_default_post);
	}
	#endif
#endif
    /**/
    r = alloc_chrdev_region(&di_id, 0, DI_COUNT, DEVICE_NAME);
    if (r < 0) {
        pr_error("Can't register major for di device\n");
        return r;
    }
    di_class = class_create(THIS_MODULE, DEVICE_NAME);
    if (IS_ERR(di_class))
    {
        unregister_chrdev_region(di_id, DI_COUNT);
        return -1;
    }

    memset(&di_device, 0, sizeof(di_dev_t));

    cdev_init(&(di_device.cdev), &di_fops);
    di_device.cdev.owner = THIS_MODULE;
    cdev_add(&(di_device.cdev), di_id, DI_COUNT);

    di_device.devt = MKDEV(MAJOR(di_id), 0);
    di_device.dev = device_create(di_class, &pdev->dev, di_device.devt, &di_device, "di%d", 0); //kernel>=2.6.27

    if (di_device.dev == NULL) {
        pr_error("device_create create error\n");
        class_destroy(di_class);
        r = -EEXIST;
        return r;
    }

    device_create_file(di_device.dev, &dev_attr_config);
    device_create_file(di_device.dev, &dev_attr_debug);
#ifdef NEW_DI_V1
	 device_create_file(di_device.dev, &dev_attr_dump_pic);
#endif
    device_create_file(di_device.dev, &dev_attr_log);
    device_create_file(di_device.dev, &dev_attr_parameters);
    device_create_file(di_device.dev, &dev_attr_status);
    device_create_file(di_device.dev, &dev_attr_provider_vframe_status);
    device_create_file(di_device.dev, &dev_attr_frame_format);
    mem = &memobj;
    r = find_reserve_block(pdev->dev.of_node->name,0);
    if(r < 0){
        name = of_get_property(pdev->dev.of_node,"share-memory-name",NULL);
        if(!name){
            pr_error("\ndeinterlace memory resource undefined.\n");
            return -EFAULT;
        }else{
            r= find_reserve_block_by_name(name);
            if(r<0){
                pr_error("\ndeinterlace memory resource undefined2.\n");
                return -EFAULT;
            }
            name= of_get_property(pdev->dev.of_node,"share-memory-offset",NULL);
            if(name)
                offset= of_read_ulong(name,1);
            else{
                pr_error("\ndeinterlace memory resource undefined3.\n");
                return -EFAULT;
            }
            name= of_get_property(pdev->dev.of_node,"share-memory-size",NULL);
            if(name)
                size= of_read_ulong(name,1);
            else{
	        pr_error("\ndeinterlace memory resource undefined4.\n");
	        return -EFAULT;
        }			
	mem->start = (phys_addr_t)get_reserve_block_addr(r)+ offset;
	mem->end = mem->start+ size-1;
	}
    }else{
        mem->start = (phys_addr_t)get_reserve_block_addr(r);
        mem->end = mem->start+ (phys_addr_t)get_reserve_block_size(r)-1;
    }

    for(i=0; i<USED_LOCAL_BUF_MAX; i++){
    	used_local_buf_index[i] = -1;
    }
    used_post_buf_index = -1;
// declare deinterlace memory
    di_print("Deinterlace memory: start = 0x%x, end = 0x%x, size=0x%x\n", mem->start, mem->end, mem->end-mem->start);

    di_mem_start = mem->start;
    di_mem_size = mem->end - mem->start + 1;
    init_flag = 0;

    /* set start_frame_hold_count base on buffer size */
    buf_num_avail = di_mem_size/(default_width*(default_height+8)*5/4);
    if(buf_num_avail > MAX_LOCAL_BUF_NUM){
        buf_num_avail = MAX_LOCAL_BUF_NUM;
    }
    /**/

    vf_receiver_init(&di_vf_recv, VFM_NAME, &di_vf_receiver, NULL);
    vf_reg_receiver(&di_vf_recv);
    active_flag = 1;
     //data32 = (*P_A9_0_IRQ_IN1_INTR_STAT_CLR);
    r = request_irq(INT_DEINTERLACE, &de_irq,
                    IRQF_SHARED, "deinterlace",
                    (void *)"deinterlace");
#ifdef DET3D
   r = request_irq(INT_DET3D, &det3d_irq,
                   IRQF_SHARED, "det3d",
                   (void *)"det3d");
#endif
#if 0 //MESON_CPU_TYPE <= MESON_CPU_TYPE_MESON6TV
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
    WRITE_CBUS_REG(SYS_CPU_0_IRQ_IN1_INTR_MASK, READ_CBUS_REG(SYS_CPU_0_IRQ_IN1_INTR_MASK)|(1<<14));
#else
    WRITE_CBUS_REG(A9_0_IRQ_IN1_INTR_MASK, READ_CBUS_REG(A9_0_IRQ_IN1_INTR_MASK)|(1<<14));
#endif
#endif
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
    sema_init(&di_sema,1);
#else
    init_MUTEX(&di_sema);
#endif
    di_sema_init_flag=1;
#ifdef FIQ_VSYNC
	fiq_handle_item.handle=di_vf_put_isr;
	fiq_handle_item.key=(u32)di_vf_put_isr;
	fiq_handle_item.name="di_vf_put_isr";
	if(register_fiq_bridge_handle(&fiq_handle_item)){
    pr_dbg("%s: register_fiq_bridge_handle fail\n", __func__);
	}
#endif
    reset_di_para();
    di_hw_init();

    /* timer */
    INIT_WORK(&di_pre_work, di_timer_handle);
    init_timer(&di_pre_timer);
    di_pre_timer.data = (ulong) & di_pre_timer;
    di_pre_timer.function = di_pre_timer_cb;
    di_pre_timer.expires = jiffies + DI_PRE_INTERVAL;
    add_timer(&di_pre_timer);
    /**/
#if (!(defined RUN_DI_PROCESS_IN_TIMER))
    di_device.task = kthread_run(di_task_handle, &di_device, "kthread_di");
#endif
#if (defined RUN_DI_PROCESS_IN_IRQ)&&(!(defined FIQ_VSYNC))

    WRITE_CBUS_REG_BITS(ISA_TIMER_MUX,0,14,1);// one time
    WRITE_CBUS_REG_BITS(ISA_TIMER_MUX,0,4,2);// 1us
    WRITE_CBUS_REG_BITS(ISA_TIMER_MUX,1,18,1);// enable timer c

    r = request_irq(INT_TIMER_C, &timer_irq,
                    IRQF_SHARED, "timerC",
                    (void *)"timerC");
#endif
#ifdef RUN_DI_PROCESS_IN_TIMER_IRQ
    WRITE_CBUS_REG(ISA_TIMERC, 100);// timerc starting count value
    WRITE_CBUS_REG_BITS(ISA_TIMER_MUX,1,14,1);// periodic
    WRITE_CBUS_REG_BITS(ISA_TIMER_MUX,0,4,2);// 10us
    WRITE_CBUS_REG_BITS(ISA_TIMER_MUX,1,18,1);// enable timer c

    r = request_irq(INT_TIMER_C, &timer_irq,
                    IRQF_SHARED, "timerC",
                    (void *)"timerC");
#endif

    di_set_power_control(0,0);
    di_set_power_control(1,0);
    return r;
}

static int di_remove(struct platform_device *pdev)
{
    di_hw_uninit();
    di_device.di_event = 0xff;
    kthread_stop(di_device.task);
#ifdef FIQ_VSYNC
  	unregister_fiq_bridge_handle(&fiq_handle_item);
#endif
    vf_unreg_provider(&di_vf_prov);
    vf_unreg_receiver(&di_vf_recv);

    di_uninit_buf();
    di_set_power_control(0,0);
    di_set_power_control(1,0);
    /* Remove the cdev */
    device_remove_file(di_device.dev, &dev_attr_config);
    device_remove_file(di_device.dev, &dev_attr_debug);
    device_remove_file(di_device.dev, &dev_attr_log);
#ifdef NEW_DI_V1
	device_remove_file(di_device.dev, &dev_attr_dump_pic);
#endif
    device_remove_file(di_device.dev, &dev_attr_parameters);
    device_remove_file(di_device.dev, &dev_attr_status);

    cdev_del(&di_device.cdev);

    device_destroy(di_class, di_id);

    class_destroy(di_class);

    unregister_chrdev_region(di_id, DI_COUNT);
    return 0;
}

#ifdef CONFIG_PM
static int save_init_flag = 0;
static int di_suspend(struct platform_device *pdev,pm_message_t state)
{
#if (defined RUN_DI_PROCESS_IN_IRQ)&&(!(defined FIQ_VSYNC))
    WRITE_CBUS_REG_BITS(ISA_TIMER_MUX,0,18,1);// disable timer c
#endif
#if 1
//fix suspend/resume crash problem
    save_init_flag = init_flag;
    init_flag = 0;
    if(di_pre_stru.di_inp_buf){
        if(vframe_in[di_pre_stru.di_inp_buf->index]){
            vf_put(vframe_in[di_pre_stru.di_inp_buf->index], VFM_NAME);
            vframe_in[di_pre_stru.di_inp_buf->index] = NULL;
            vf_notify_provider(VFM_NAME, VFRAME_EVENT_RECEIVER_PUT, NULL);
        }
	}
#endif

    di_set_power_control(0,0);
    di_set_power_control(1,0);
    pr_info("di: di_suspend\n");
    return 0;
}

static int di_resume(struct platform_device *pdev)
{
    init_flag = save_init_flag;
    if(init_flag){
        di_set_power_control(0,1);
        di_set_power_control(1,1);
    }
#if (defined RUN_DI_PROCESS_IN_IRQ)&&(!(defined FIQ_VSYNC))
    WRITE_CBUS_REG_BITS(ISA_TIMER_MUX,0,14,1);// one time
    WRITE_CBUS_REG_BITS(ISA_TIMER_MUX,0,4,2);// 1us
    WRITE_CBUS_REG_BITS(ISA_TIMER_MUX,1,18,1);// enable timer c
    WRITE_CBUS_REG(ISA_TIMERC, 1);
#endif
    pr_info("di_hdmirx: resume module\n");
    return 0;
}
#endif

#ifdef CONFIG_USE_OF
static const struct of_device_id amlogic_deinterlace_dt_match[]={
	{	.compatible = "amlogic,deinterlace",
	},
	{},
};
#else
#define amlogic_deinterlace_dt_match NULL
#endif

static struct platform_driver di_driver = {
    .probe      = di_probe,
    .remove     = di_remove,
#ifdef CONFIG_PM
    .suspend    = di_suspend,
    .resume     = di_resume,
#endif
    .driver     = {
        .name   = DEVICE_NAME,
		    .owner	= THIS_MODULE,
        .of_match_table = amlogic_deinterlace_dt_match,
    }
};

static struct platform_device* deinterlace_device = NULL;


static int  __init di_init(void)
{
    if(boot_init_flag&INIT_FLAG_NOT_LOAD)
        return 0;

    pr_dbg("di_init\n");
#if 0
	  deinterlace_device = platform_device_alloc(DEVICE_NAME,0);
    if (!deinterlace_device) {
        pr_error("failed to alloc deinterlace_device\n");
        return -ENOMEM;
    }

    if(platform_device_add(deinterlace_device)){
        platform_device_put(deinterlace_device);
        pr_error("failed to add deinterlace_device\n");
        return -ENODEV;
    }
    if (platform_driver_register(&di_driver)) {
        pr_error("failed to register di module\n");

        platform_device_del(deinterlace_device);
        platform_device_put(deinterlace_device);
        return -ENODEV;
    }
#else
    if (platform_driver_register(&di_driver)) {
        di_print("failed to register di module\n");
        return -ENODEV;
    }
#endif
    return 0;
}




static void __exit di_exit(void)
{
    pr_dbg("di_exit\n");
    platform_driver_unregister(&di_driver);
    platform_device_unregister(deinterlace_device);
    deinterlace_device = NULL;
    return ;
}

MODULE_PARM_DESC(bypass_hd, "\n bypass_hd \n");
module_param(bypass_hd, int, 0664);

MODULE_PARM_DESC(bypass_hd_prog, "\n bypass_hd_prog \n");
module_param(bypass_hd_prog, int, 0664);

MODULE_PARM_DESC(bypass_interlace_output, "\n bypass_interlace_output \n");
module_param(bypass_interlace_output, int, 0664);

MODULE_PARM_DESC(bypass_prog, "\n bypass_prog \n");
module_param(bypass_prog, int, 0664);

MODULE_PARM_DESC(bypass_superd, "\n bypass_superd \n");
module_param(bypass_superd, int, 0664);

MODULE_PARM_DESC(bypass_all, "\n bypass_all \n");
module_param(bypass_all, int, 0664);

MODULE_PARM_DESC(bypass_1080p, "\n bypass_1080p \n");
module_param(bypass_1080p, int, 0664);

MODULE_PARM_DESC(bypass_3d, "\n bypass_3d \n");
module_param(bypass_3d, int, 0664);

MODULE_PARM_DESC(bypass_dynamic, "\n bypass_dynamic \n");
module_param(bypass_dynamic, int, 0664);

MODULE_PARM_DESC(bypass_trick_mode, "\n bypass_trick_mode \n");
module_param(bypass_trick_mode, int, 0664);

MODULE_PARM_DESC(invert_top_bot, "\n invert_top_bot \n");
module_param(invert_top_bot, int, 0664);

MODULE_PARM_DESC(skip_top_bot, "\n skip_top_bot \n");
module_param(skip_top_bot, int, 0664);

MODULE_PARM_DESC(force_width, "\n force_width \n");
module_param(force_width, int, 0664);

MODULE_PARM_DESC(force_height, "\n force_height \n");
module_param(force_height, int, 0664);

MODULE_PARM_DESC(bypass_get_buf_threshold, "\n bypass_get_buf_threshold\n");
module_param(bypass_get_buf_threshold, uint, 0664);

MODULE_PARM_DESC(pulldown_detect, "\n pulldown_detect \n");
module_param(pulldown_detect, int, 0664);

MODULE_PARM_DESC(prog_proc_config, "\n prog_proc_config \n");
module_param(prog_proc_config, int, 0664);

MODULE_PARM_DESC(skip_wrong_field, "\n skip_wrong_field \n");
module_param(skip_wrong_field, int, 0664);

MODULE_PARM_DESC(frame_count, "\n frame_count \n");
module_param(frame_count, int, 0664);

MODULE_PARM_DESC(start_frame_drop_count, "\n start_frame_drop_count \n");
module_param(start_frame_drop_count, int, 0664);

#ifdef SUPPORT_START_FRAME_HOLD
MODULE_PARM_DESC(start_frame_hold_count, "\n start_frame_hold_count \n");
module_param(start_frame_hold_count, int, 0664);
#endif

module_init(di_init);
module_exit(di_exit);

MODULE_PARM_DESC(same_w_r_canvas_count, "\n canvas of write and read are same \n");
module_param(same_w_r_canvas_count, long, 0664);

MODULE_PARM_DESC(same_field_top_count, "\n same top field \n");
module_param(same_field_top_count, long, 0664);

MODULE_PARM_DESC(same_field_bot_count, "\n same bottom field \n");
module_param(same_field_bot_count, long, 0664);

MODULE_PARM_DESC(same_field_source_flag_th, "\n same_field_source_flag_th \n");
module_param(same_field_source_flag_th, int, 0664);

MODULE_PARM_DESC(pulldown_count, "\n pulldown_count \n");
module_param(pulldown_count, long, 0664);

MODULE_PARM_DESC(pulldown_buffer_mode, "\n pulldown_buffer_mode \n");
module_param(pulldown_buffer_mode, long, 0664);

MODULE_PARM_DESC(pulldown_win_mode, "\n pulldown_win_mode \n");
module_param(pulldown_win_mode, long, 0664);

MODULE_PARM_DESC(di_log_flag, "\n di log flag \n");
module_param(di_log_flag, int, 0664);

MODULE_PARM_DESC(di_debug_flag, "\n di debug flag \n");
module_param(di_debug_flag, int, 0664);

MODULE_PARM_DESC(buf_state_log_threshold, "\n buf_state_log_threshold \n");
module_param(buf_state_log_threshold, int, 0664);

MODULE_PARM_DESC(bypass_state, "\n bypass_state\n");
module_param(bypass_state, uint, 0664);

MODULE_PARM_DESC(force_bob_flag, "\n force_bob_flag\n");
module_param(force_bob_flag, uint, 0664);

MODULE_PARM_DESC(di_vscale_skip_enable, "\n di_vscale_skip_enable\n");
module_param(di_vscale_skip_enable, uint, 0664);

MODULE_PARM_DESC(di_vscale_skip_count, "\n di_vscale_skip_count\n");
module_param(di_vscale_skip_count, uint, 0664);

MODULE_PARM_DESC(di_vscale_skip_count_real, "\n di_vscale_skip_count_real\n");
module_param(di_vscale_skip_count_real, uint, 0664);

#ifdef D2D3_SUPPORT
MODULE_PARM_DESC(d2d3_enable, "\n d2d3_enable\n");
module_param(d2d3_enable, uint, 0664);
#endif

#ifdef DET3D
MODULE_PARM_DESC(det3d_en, "\n det3d_enable\n");
module_param(det3d_en, bool, 0664);
MODULE_PARM_DESC(det3d_mode, "\n det3d_mode\n");
module_param(det3d_mode, uint, 0664);
#endif

#ifdef NEW_DI_V1
MODULE_PARM_DESC(di_new_mode_mask, "\n di_new_mode_mask\n");
module_param(di_new_mode_mask, uint, 0664);
#endif

MODULE_PARM_DESC(pre_hold_line, "\n pre_hold_line\n");
module_param(pre_hold_line, uint, 0664);

MODULE_PARM_DESC(pre_urgent, "\n pre_urgent\n");
module_param(pre_urgent, uint, 0664);

MODULE_PARM_DESC(post_hold_line, "\n post_hold_line\n");
module_param(post_hold_line, uint, 0664);

MODULE_PARM_DESC(post_urgent, "\n post_urgent\n");
module_param(post_urgent, uint, 0664);

MODULE_PARM_DESC(force_duration_0, "\n force_duration_0\n");
module_param(force_duration_0, uint, 0664);

MODULE_PARM_DESC(use_reg_cfg, "\n use_reg_cfg\n");
module_param(use_reg_cfg, uint, 0664);

MODULE_PARM_DESC(di_printk_flag, "\n di_printk_flag\n");
module_param(di_printk_flag, uint, 0664);

MODULE_PARM_DESC(force_recovery, "\n force_recovery\n");
module_param(force_recovery, uint, 0664);

MODULE_PARM_DESC(force_recovery_count, "\n force_recovery_count\n");
module_param(force_recovery_count, uint, 0664);

MODULE_PARM_DESC(pre_process_time_force, "\n pre_process_time_force\n");
module_param(pre_process_time_force, uint, 0664);

MODULE_PARM_DESC(pre_process_time, "\n pre_process_time\n");
module_param(pre_process_time, uint, 0664);

#ifdef RUN_DI_PROCESS_IN_IRQ
MODULE_PARM_DESC(input2pre, "\n input2pre\n");
module_param(input2pre, uint, 0664);

MODULE_PARM_DESC(input2pre_buf_miss_count, "\n input2pre_buf_miss_count\n");
module_param(input2pre_buf_miss_count, uint, 0664);

MODULE_PARM_DESC(input2pre_proc_miss_count, "\n input2pre_proc_miss_count\n");
module_param(input2pre_proc_miss_count, uint, 0664);

MODULE_PARM_DESC(input2pre_miss_policy, "\n input2pre_miss_policy\n");
module_param(input2pre_miss_policy, uint, 0664);

MODULE_PARM_DESC(input2pre_throw_count, "\n input2pre_throw_count\n");
module_param(input2pre_throw_count, uint, 0664);
#endif

MODULE_PARM_DESC(timeout_miss_policy, "\n timeout_miss_policy\n");
module_param(timeout_miss_policy, uint, 0664);

MODULE_PARM_DESC(bypass_post, "\n bypass_post\n");
module_param(bypass_post, uint, 0664);

MODULE_PARM_DESC(bypass_post_state, "\n bypass_post_state\n");
module_param(bypass_post_state, uint, 0664);

MODULE_PARM_DESC(force_update_post_reg, "\n force_update_post_reg\n");
module_param(force_update_post_reg, uint, 0664);

MODULE_PARM_DESC(update_post_reg_count, "\n update_post_reg_count\n");
module_param(update_post_reg_count, uint, 0664);

module_param(use_2_interlace_buff,bool,0664);
MODULE_PARM_DESC(use_2_interlace_buff,"/n debug for progress interlace mixed source /n");

MODULE_DESCRIPTION("AMLOGIC HDMI TX driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0.0");


static char* next_token_ex(char* seperator, char *buf, unsigned size, unsigned offset, unsigned *token_len, unsigned *token_offset)
{ /* besides characters defined in seperator, '\"' are used as seperator; and any characters in '\"' will not act as seperator */
	char *pToken = NULL;
    char last_seperator = 0;
    char trans_char_flag = 0;
    if(buf){
    	for (;offset<size;offset++){
    	    int ii=0;
    	    char ch;
            if (buf[offset] == '\\'){
                trans_char_flag = 1;
                continue;
            }
    	    while(((ch=seperator[ii++])!=buf[offset])&&(ch)){
    	    }
    	    if (ch){
                if (!pToken){
    	            continue;
                }
    	        else {
                    if (last_seperator != '"'){
    	                *token_len = (unsigned)(buf + offset - pToken);
    	                *token_offset = offset;
    	                return pToken;
    	            }
    	        }
            }
    	    else if (!pToken)
            {
                if (trans_char_flag&&(buf[offset] == '"'))
                    last_seperator = buf[offset];
    	        pToken = &buf[offset];
            }
            else if ((trans_char_flag&&(buf[offset] == '"'))&&(last_seperator == '"')){
                *token_len = (unsigned)(buf + offset - pToken - 2);
                *token_offset = offset + 1;
                return pToken + 1;
            }
            trans_char_flag = 0;
        }
        if (pToken) {
            *token_len = (unsigned)(buf + offset - pToken);
            *token_offset = offset;
        }
    }
	return pToken;
}

static  int __init di_boot_para_setup(char *s)
{
    char separator[]={' ',',',';',0x0};
    char *token;
    unsigned token_len, token_offset, offset=0;
    int size=strlen(s);
    do{
        token=next_token_ex(separator, s, size, offset, &token_len, &token_offset);
        if(token){
            if((token_len==3) && (strncmp(token, "off", token_len)==0)){
                boot_init_flag|=INIT_FLAG_NOT_LOAD;
            }
        }
        offset=token_offset;
    }while(token);
    return 0;
}

__setup("di=",di_boot_para_setup);


vframe_t* get_di_inp_vframe(void)
{
    vframe_t* vframe = NULL;
    if(di_pre_stru.di_inp_buf){
        vframe = di_pre_stru.di_inp_buf->vframe;
    }
    return vframe;
}
