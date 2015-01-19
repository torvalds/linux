#include <linux/version.h>
#include <linux/module.h>
#include <linux/irq.h>
#include <linux/types.h>
#include <linux/input.h>
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
#include <asm/irq.h>
#include <asm/io.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/spinlock_types.h>
#include <linux/switch.h>
#include <asm/uaccess.h>
#include <mach/am_regs.h>
#include <linux/amlogic/hdmi_tx/hdmi_info_global.h>
#include <linux/amlogic/hdmi_tx/hdmi_tx_module.h>
#include <mach/hdmi_tx_reg.h>
#include "hdmi_tx_hdcp.h"
/*
    hdmi_tx_hdcp.c
    version 1.1
*/

static struct switch_dev hdcp_dev = {  // android ics switch device
       .name = "hdcp",
};

// For most cases, we don't use HDCP
// If using HDCP, need add follow command in boot/init.rc and recovery/boot/init.rc
// write /sys/module/hdmitx/parameters/hdmi_output_force 0
static int hdmi_output_force = 1;
static int hdmi_authenticated = -1;
static int hdmi_hdcp_process = 1;   // default hdcp is on, if aksv is 0, then disable

// Notic: the HDCP key setting has been moved to uboot
// On MBX project, it is too late for HDCP get from 
// other devices

/* verify ksv, 20 ones and 20 zeroes*/
int hdcp_ksv_valid(unsigned char * dat)
{
    int i, j, one_num = 0;
    for(i = 0; i < 5; i++){
        for(j=0;j<8;j++) {
            if((dat[i]>>j)&0x1) {
                one_num++;
            }
        }
    }
    if(one_num == 0)
        hdmi_print(INF, HDCP "no HDCP key available\n");
    return (one_num == 20);
}

static struct timer_list hdcp_monitor_timer;
static void hdcp_monitor_func(unsigned long arg)
{
    //static int hdcp_auth_flag = 0;
    hdmitx_dev_t* hdmitx_device = (hdmitx_dev_t* )hdcp_monitor_timer.data;
    if((hdmitx_device->HWOp.Cntl) && (hdmitx_device->log & (HDMI_LOG_HDCP))){
        hdmitx_device->HWOp.Cntl(hdmitx_device, HDMITX_HDCP_MONITOR, 1);
    }

    mod_timer(&hdcp_monitor_timer, jiffies + 2 * HZ);
}

static int hdmitx_hdcp_task(void *data)
{
    static int err_cnt = 0;
    static int time_cnt = 0;
    hdmitx_dev_t *hdmitx_device = (hdmitx_dev_t*)data;

    init_timer(&hdcp_monitor_timer);
    hdcp_monitor_timer.data = (ulong) data;
    hdcp_monitor_timer.function = hdcp_monitor_func;
    hdcp_monitor_timer.expires = jiffies + HZ;
    add_timer(&hdcp_monitor_timer);

    while(hdmitx_device->hpd_event != 0xff) {
        if((hdmitx_device->output_blank_flag == 1) && (hdmitx_device->hpd_state == 1) && (hdmitx_device->cur_VIC != HDMI_Unkown)) {
            err_cnt = 0;
            time_cnt = 1;
            hdmitx_device->output_blank_flag = 0;
#ifdef CONFIG_AML_HDMI_TX_HDCP
            hdmi_print(INF, HDCP "start hdcp\n");
            hdmitx_device->HWOp.CntlDDC(hdmitx_device, AVMUTE_OFF, 0);
            hdmitx_device->HWOp.CntlDDC(hdmitx_device, DDC_RESET_EDID, 0);
            hdmitx_device->HWOp.CntlDDC(hdmitx_device, DDC_EDID_READ_DATA, 0);
            hdmitx_device->HWOp.CntlDDC(hdmitx_device, DDC_RESET_HDCP, 0);
            hdmitx_device->HWOp.CntlDDC(hdmitx_device, DDC_HDCP_OP, HDCP_ON);
            msleep(100);
            while((hdmitx_device->hpd_state == 1) && (hdmitx_device->cur_VIC != HDMI_Unkown)) {
                hdmi_authenticated = hdmitx_device->HWOp.CntlDDC(hdmitx_device, DDC_HDCP_GET_AUTH, 0);
                switch_set_state(&hdcp_dev, hdmi_authenticated);
                hdmitx_device->HWOp.CntlConfig(hdmitx_device, CONF_VIDEO_BLANK_OP, hdmi_authenticated ? VIDEO_UNBLANK: VIDEO_BLANK);
                hdmitx_device->HWOp.CntlConfig(hdmitx_device, CONF_AUDIO_MUTE_OP, hdmi_authenticated ? AUDIO_UNMUTE : AUDIO_MUTE);
                if( !hdmi_authenticated ) {
                    err_cnt ++;
                    if(err_cnt & (3 << time_cnt)) {
                        time_cnt ++;
                        hdmi_print(ERR, HDCP "authenticated failed\n");
                    }
                }
                msleep(20);
                if(hdmitx_device->output_blank_flag == 1)
                    break;
            }
#else
            hdmitx_device->HWOp.CntlConfig(hdmitx_device, CONF_VIDEO_BLANK_OP, VIDEO_UNBLANK);
            hdmitx_device->HWOp.CntlConfig(hdmitx_device, CONF_AUDIO_MUTE_OP, AUDIO_UNMUTE);
            hdmitx_device->audio_step = 1;
#endif
        }
        msleep_interruptible(100);
    }

    return 0;
}

static int __init hdmitx_hdcp_init(void)
{
    hdmitx_dev_t *hdmitx_device = get_hdmitx_device();

    switch_dev_register(&hdcp_dev);

    hdmitx_device->task_hdcp = kthread_run(hdmitx_hdcp_task, (void*)hdmitx_device, "kthread_hdcp");

    return 0;
}

static void __exit hdmitx_hdcp_exit(void)
{
    switch_dev_unregister(&hdcp_dev);
}


MODULE_PARM_DESC(hdmi_authenticated, "\n hdmi_authenticated \n");
module_param(hdmi_authenticated, int, S_IRUGO);

MODULE_PARM_DESC(hdmi_hdcp_process, "\n hdmi_hdcp_process \n");
module_param(hdmi_hdcp_process, int, 0664);

MODULE_PARM_DESC(hdmi_output_force, "\n hdmi_output_force \n");
module_param(hdmi_output_force, int, 0664);


module_init(hdmitx_hdcp_init);
//device_initcall_sync(hdmitx_hdcp_init);
module_exit(hdmitx_hdcp_exit);
MODULE_DESCRIPTION("AMLOGIC HDMI TX HDCP driver");
MODULE_LICENSE("GPL");
