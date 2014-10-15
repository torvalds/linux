/*
 * Amlogic M6 
 * driver-----------VDIN CONTROL
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
#define HDMI_DEBUG()  printk("VDINCTRL DEBUG: %s [%d]", __FUNCTION__, __LINE__)

#include <linux/version.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
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
#include <asm/uaccess.h>
#include <mach/am_regs.h>
#include "../tvin_global.h"
#include "../vdin/vdin_regs.h"
#include "../vdin/vdin_drv.h"
#include "../vdin/vdin_ctl.h"
#include "../vdin/vdin_sm.h"
#include "../tvin_format_table.h"
#include "../tvin_frontend.h"

#define DEVICE_NAME "vdin_ctrl"
#define VDIN_CTRL_COUNT 4

#define pr_dbg(fmt, args...) printk(KERN_DEBUG "vdin_ctrl: " fmt, ## args)
#define pr_error(fmt, args...) printk(KERN_ERR "vdin_ctrl: " fmt, ## args)


static dev_t vdinctrl_id;
static struct class *vdinctrl_class;
static struct device *vdinctrl_dev;

static int port = TVIN_PORT_HDMI1;
static int test_flag = 0;
int vdin_debug_flag = 0;
static int stable_threshold = 10;
static int is_hdmi_mode = 0;
static int horz_active = 0;
static int vert_active = 0;
static int is_interlace = 0;
static int vfreq = 0;

static int audio_status = 0;
static int audio_sample_freq = 0;
static int audio_channel_alloc = 0;

struct aud_info_s{
    int real_sample_rate;
};

#define FE_STATE_POWEROFF           0
#define FE_STATE_POWERON            1
#define FE_STATE_STABLE             2
#define FE_STATE_READY              3

char* state_name[] = {"poweroff", "poweron", "stable", "ready"};

typedef struct vdinctrl_dev_{
    struct cdev cdev;             /* The cdev structure */
    struct proc_dir_entry *proc_file;
    struct task_struct *task;
    struct aud_info_s aud_info;
    /**/
    unsigned int cur_width;
    unsigned int cur_height;
    unsigned int cur_frame_rate;
    unsigned int port;
    unsigned char fe_enable;
    unsigned char vdin_enable;
    unsigned char task_pause;
    /**/
    unsigned int state;
    enum tvin_sig_fmt_e fmt;
}vdinctrl_dev_t;


static vdinctrl_dev_t vdinctrl_device;


static void start_vdin(enum tvin_sig_fmt_e fmt);
/*****************************
*    vdin_ctrl attr management :
******************************/

static ssize_t show_enable(struct device * dev, struct device_attribute *attr, char * buf)
{   
    return sprintf(buf, "%d\n", vdinctrl_device.vdin_enable);
}
    
static ssize_t store_enable(struct device * dev, struct device_attribute *attr, const char * buf, size_t count)
{
    size_t r;
    int val;
    unsigned char vdin_enable;
    r = sscanf(buf, "%d", &val);
    if (r != 1) {
        return -EINVAL;
    }
    vdin_enable = val&0xff;
    if(vdin_enable != vdinctrl_device.vdin_enable){
        vdinctrl_device.vdin_enable = vdin_enable; 
        if(vdinctrl_device.vdin_enable == 1){
            //start_vdin(getHDMIRXHorzActive(), getHDMIRXVertActive(), 10000, IsHDMIRXInterlace());
        }
        else if(vdinctrl_device.vdin_enable == 0){
            //stop_vdin();
        }  
    }

    return count;
}


static ssize_t show_poweron(struct device * dev, struct device_attribute *attr, char * buf)
{   
    return sprintf(buf, "%d\n", vdinctrl_device.fe_enable);
}
    
static ssize_t store_poweron(struct device * dev, struct device_attribute *attr, const char * buf, size_t count)
{
    size_t r;
    int val;
    r = sscanf(buf, "%d", &val);
    if (r != 1) {
        return -EINVAL;
    }
    if(val != vdinctrl_device.fe_enable){
        vdinctrl_device.fe_enable = val; 
        if(vdinctrl_device.fe_enable == 1){
            vdinctrl_device.port = port;
        }
        else if(vdinctrl_device.fe_enable == 0){
        }  
    }

    return count;
}

static ssize_t store_dbg(struct device * dev, struct device_attribute *attr, const char * buf, size_t count)
{
    char tmpbuf[128];
    int i=0;
    unsigned int adr;
    unsigned int value=0;
    while((buf[i])&&(buf[i]!=',')&&(buf[i]!=' ')){
        tmpbuf[i]=buf[i];
        i++;
    }
    tmpbuf[i]=0;
    if(strncmp(tmpbuf, "pause", 5)==0){
        vdinctrl_device.task_pause = 1;
        printk("Pause %s\n", __func__);
    }
    else if(strncmp(tmpbuf, "start", 5)==0){
        vdinctrl_device.task_pause = 0;
        printk("Start %s\n", __func__);
    }
    return 16;    
}

static DEVICE_ATTR(enable, S_IWUSR | S_IRUGO, show_enable, store_enable);
static DEVICE_ATTR(poweron, S_IWUSR | S_IRUGO, show_poweron, store_poweron);
static DEVICE_ATTR(debug, S_IRWXUGO, NULL, store_dbg);

/******************************
*  vdin_ctrl kernel task
*******************************/
#define DUMP_TIME 20

int vdin_ctrl_open_fe(int no , int port);

int vdin_ctrl_close_fe(int no);

int vdin_ctrl_start_fe(int no ,vdin_parm_t *para);

int vdin_ctrl_stop_fe(int no);

enum tvin_sig_fmt_e  vdin_ctrl_get_fmt(int no);

static void update_status(unsigned char clear)
{
}    


static int 
fe_ctrl_task_handle(void *data) 
{

    int stable_count = 0;
    int i;
    int pre_state;

    update_status(1);
    while(1){
        if(vdinctrl_device.task_pause){
            continue;
        }

        pre_state = vdinctrl_device.state;
        switch(vdinctrl_device.state){
            case FE_STATE_POWEROFF:
                if(vdinctrl_device.fe_enable){
                    vdin_ctrl_open_fe(0, vdinctrl_device.port);
                    vdinctrl_device.state = FE_STATE_POWERON;    
                }
                
                break;
            case FE_STATE_POWERON:
                if(vdinctrl_device.fe_enable == 0){
                    vdin_ctrl_close_fe(0);
                    vdinctrl_device.state = FE_STATE_POWEROFF;    
                }
                else if(tvin_get_sm_status(0)==TVIN_SM_STATUS_STABLE){
                    stable_count = 0;
                    vdinctrl_device.state = FE_STATE_STABLE;    
                }
                break;
            case FE_STATE_STABLE:
                if(vdinctrl_device.fe_enable == 0){
                    vdin_ctrl_close_fe(0);
                    update_status(1);
                    vdinctrl_device.state = FE_STATE_POWEROFF;    
                }
                else if(tvin_get_sm_status(0)!=TVIN_SM_STATUS_STABLE){
                    update_status(1);
                    vdinctrl_device.state = FE_STATE_POWERON;    
                }
                else{
                    update_status(0);
                    if(stable_count<stable_threshold){
                        stable_count++;
                    }
                    if((stable_count == stable_threshold)&&(vdinctrl_device.vdin_enable)){
                        vdinctrl_device.fmt = vdin_ctrl_get_fmt(0);
                        start_vdin(vdinctrl_device.fmt);
                        vdinctrl_device.state = FE_STATE_READY;    
                    }
                }

                break;
            case FE_STATE_READY:
                if(vdinctrl_device.fe_enable == 0){
                    vdin_ctrl_stop_fe(0);
                    vdin_ctrl_close_fe(0);
                    update_status(1);
                    vdinctrl_device.state = FE_STATE_POWEROFF;    
                }
                else if(tvin_get_sm_status(0)!=TVIN_SM_STATUS_STABLE){
                    vdin_ctrl_stop_fe(0);
                    update_status(1);
                    vdinctrl_device.state = FE_STATE_POWERON;    
                }
                else if(vdinctrl_device.vdin_enable == 0){
                    vdin_ctrl_stop_fe(0);
                    vdinctrl_device.state = FE_STATE_STABLE;    
                }
                else{
                    update_status(0);
                }
                break;
        }
        
        if(pre_state != vdinctrl_device.state){
            printk("[VDIN CTRL State] %s -> %s\n", state_name[pre_state], state_name[vdinctrl_device.state]);
        }

        msleep(10);

    } 
}    


static void start_vdin(enum tvin_sig_fmt_e fmt)
{
    //int width, height, frame_rate, field_flag;
    vdin_parm_t para;
    if(vdinctrl_device.vdin_enable == 0){
        return;
    }    
    
        //vdinctrl_device.cur_width = width;
        //vdinctrl_device.cur_height = height;
        //vdinctrl_device.cur_frame_rate = frame_rate;
        
        memset( &para, 0, sizeof(para));
#if 1
        para.port  = vdinctrl_device.port;
        para.fmt = fmt;
#else
        para.port  = TVIN_PORT_HDMI1;
        para.frame_rate = 60;
        para.h_active = 1280;
        para.v_active = 720;
        para.fmt = TVIN_SIG_FMT_MAX+1;
        para.scan_mode = TVIN_SCAN_MODE_PROGRESSIVE;	
        para.hsync_phase = 1;
        para.vsync_phase = 0;
        //para.hs_bp = 0;
        //para.vs_bp = 2;
        para.cfmt = TVIN_YUV444; //TVIN_RGB444;
        para.reserved = 0; //skip_num
#endif

        vdin_ctrl_start_fe(0, &para);

        printk("%s: fmt %x\n", __func__, para.fmt);        
        //printk("%s: %dx%d %d/s\n", __func__, width, height, frame_rate);
        
        vdinctrl_device.aud_info.real_sample_rate = audio_sample_freq;                    

}


/*****************************
*    vdin_ctrl driver file_operations 
*    
******************************/
static int vdinctrl_open(struct inode *node, struct file *file)
{
    vdinctrl_dev_t *vdin_ctrl_in_devp;

    /* Get the per-device structure that contains this cdev */
    vdin_ctrl_in_devp = container_of(node->i_cdev, vdinctrl_dev_t, cdev);
    file->private_data = vdin_ctrl_in_devp;

    return 0;

}


static int vdinctrl_release(struct inode *node, struct file *file)
{
    //vdinctrl_dev_t *vdin_ctrl_in_devp = file->private_data;

    /* Reset file pointer */

    /* Release some other fields */
    /* ... */
    return 0;
}



static int vdinctrl_ioctl(struct inode *node, struct file *file, unsigned int cmd,   unsigned long args)
{
    int   r = 0;
    switch (cmd) {
        default:
            break;
    }
    return r;
}

const static struct file_operations vdinctrl_fops = {
    .owner    = THIS_MODULE,
    .open     = vdinctrl_open,
    .release  = vdinctrl_release,
//    .ioctl    = vdinctrl_ioctl,
};


static int vdinctrl_probe(struct platform_device *pdev)
{
    int r;
    HDMI_DEBUG();
    pr_dbg("vdinctrl_probe\n");




    r = alloc_chrdev_region(&vdinctrl_id, 0, VDIN_CTRL_COUNT, DEVICE_NAME);
    if (r < 0) {
        pr_error("Can't register major for vdin_ctrl device\n");
        return r;
    }
    vdinctrl_class = class_create(THIS_MODULE, DEVICE_NAME);
    if (IS_ERR(vdinctrl_class))
    {
        unregister_chrdev_region(vdinctrl_id, VDIN_CTRL_COUNT);
        return -1;
        //return PTR_ERR(aoe_class);
    }

    cdev_init(&(vdinctrl_device.cdev), &vdinctrl_fops);
    vdinctrl_device.cdev.owner = THIS_MODULE;
    cdev_add(&(vdinctrl_device.cdev), vdinctrl_id, VDIN_CTRL_COUNT);

    //vdinctrl_dev = device_create(vdinctrl_class, NULL, vdinctrl_id, "vdin_ctrl%d", 0);
    vdinctrl_dev = device_create(vdinctrl_class, NULL, vdinctrl_id, NULL, "vdin_ctrl%d", 0); //kernel>=2.6.27 
    device_create_file(vdinctrl_dev, &dev_attr_enable);
    device_create_file(vdinctrl_dev, &dev_attr_poweron);
    device_create_file(vdinctrl_dev, &dev_attr_debug);
    
    if (vdinctrl_dev == NULL) {
        pr_error("device_create create error\n");
        class_destroy(vdinctrl_class);
        r = -EEXIST;
        return r;
    }
    vdinctrl_device.task = kthread_run(fe_ctrl_task_handle, &vdinctrl_device, "kthread_hdmi");
    
	if (r < 0){
		printk(KERN_ERR "vdin_ctrl: register switch dev failed\n");
		return r;
	}    

    return r;
}

static int vdinctrl_remove(struct platform_device *pdev)
{
    kthread_stop(vdinctrl_device.task);
    
    //vdinctrl_remove();
    /* Remove the cdev */
    device_remove_file(vdinctrl_dev, &dev_attr_enable);
    device_remove_file(vdinctrl_dev, &dev_attr_poweron);
    device_remove_file(vdinctrl_dev, &dev_attr_debug);

    cdev_del(&vdinctrl_device.cdev);

    device_destroy(vdinctrl_class, vdinctrl_id);

    class_destroy(vdinctrl_class);

    unregister_chrdev_region(vdinctrl_id, VDIN_CTRL_COUNT);
    return 0;
}

#ifdef CONFIG_PM
static int vdinctrl_suspend(struct platform_device *pdev,pm_message_t state)
{
    pr_info("vdin_ctrl: vdin_ctrl_suspend\n");
    return 0;
}

static int vdinctrl_resume(struct platform_device *pdev)
{
    pr_info("vdin_ctrl: resume module\n");
    return 0;
}

static struct platform_driver vdinctrl_driver = {
    .probe      = vdinctrl_probe,
    .remove     = vdinctrl_remove,
#ifdef CONFIG_PM
    .suspend    = vdinctrl_suspend,
    .resume     = vdinctrl_resume,
#endif
    .driver     = {
        .name   = DEVICE_NAME,
		    .owner	= THIS_MODULE,
    }
};

static struct platform_device* vdin_ctrl_device = NULL;


static int  __init vdinctrl_init(void)
{
    pr_dbg("vdinctrl_init\n");
    memset(&vdinctrl_device, 0, sizeof(vdinctrl_device));
	  vdin_ctrl_device = platform_device_alloc(DEVICE_NAME,0);
    if (!vdin_ctrl_device) {
        pr_error("failed to alloc vdin_ctrl_device\n");
        return -ENOMEM;
    }
    
    if(platform_device_add(vdin_ctrl_device)){
        platform_device_put(vdin_ctrl_device);
        pr_error("failed to add vdin_ctrl_device\n");
        return -ENODEV;
    }
    if (platform_driver_register(&vdinctrl_driver)) {
        pr_error("failed to register vdin_ctrl module\n");
        
        platform_device_del(vdin_ctrl_device);
        platform_device_put(vdin_ctrl_device);
        return -ENODEV;
    }
    return 0;
}




static void __exit vdin_ctrl_exit(void)
{
    pr_dbg("vdin_ctrl_exit\n");
    platform_driver_unregister(&vdinctrl_driver);
    platform_device_unregister(vdin_ctrl_device); 
    vdin_ctrl_device = NULL;
    return ;
}

module_init(vdinctrl_init);
module_exit(vdin_ctrl_exit);

MODULE_PARM_DESC(vdin_debug_flag, "\n vdin_debug_flag \n");
module_param(vdin_debug_flag, int, 0664);

MODULE_PARM_DESC(stable_threshold, "\n stable_threshold \n");
module_param(stable_threshold, int, 0664);

MODULE_PARM_DESC(horz_active, "\n horz_active \n");
module_param(horz_active, int, 0664);

MODULE_PARM_DESC(vert_active, "\n vert_active \n");
module_param(vert_active, int, 0664);

MODULE_PARM_DESC(is_interlace, "\n is_interlace \n");
module_param(is_interlace, int, 0664);

MODULE_PARM_DESC(vfreq, "\n vfreq \n");
module_param(vfreq, int, 0664);

MODULE_PARM_DESC(audio_status, "\n audio_status \n");
module_param(audio_status, int, 0664);

MODULE_PARM_DESC(audio_sample_freq, "\n audio_sample_freq \n");
module_param(audio_sample_freq, int, 0664);

MODULE_PARM_DESC(audio_channel_alloc, "\n audio_channel_alloc \n");
module_param(audio_channel_alloc, int, 0664);

MODULE_PARM_DESC(port, "\n port \n");
module_param(port, int, 0664);

MODULE_PARM_DESC(test_flag, "\n test_flag \n");
module_param(test_flag, int, 0664);

MODULE_DESCRIPTION("VDIN CTRL driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0.0");


#endif
