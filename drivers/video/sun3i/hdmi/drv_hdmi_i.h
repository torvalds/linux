
#ifndef  _DRV_HDMI_I_H_
#define  _DRV_HDMI_I_H_

#include <asm/uaccess.h>
#include <asm/memory.h>
#include <asm/unistd.h>
#include "asm-generic/int-ll64.h"
#include "linux/kernel.h"
#include "linux/mm.h"
#include "linux/semaphore.h"
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/dma-mapping.h>
#include <linux/sched.h>   //wake_up_process()
#include <linux/kthread.h> //kthread_create()¡¢kthread_run()
#include <linux/err.h> //IS_ERR()¡¢PTR_ERR()
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/cdev.h>
#include <linux/types.h>

#include <linux/drv_display.h>
#include <linux/drv_hdmi.h>

#define __err(msg...) printk(msg)

#if 1
#define __wrn(msg...) do{}while(0)
#define __msg(msg...) do{}while(0)
#define __inf(msg...) do{}while(0)
#else
#define __HERE__ {printk("File:%s,Line:%d;\t", __FILE__, __LINE__);}
#define __wrn printk
#define __msg printk
#define __inf printk
#endif


__s32 Hdmi_init(void);
__s32 Hdmi_exit(void);

__s32 Hdmi_open(void);
__s32 Hdmi_close(void);
__s32 Hdmi_set_display_mode(__disp_tv_mode_t mode);
__s32 Hdmi_mode_support(__u8 mode);
__s32 Hdmi_get_HPD_status(void);
__s32 Hdmi_Audio_Enable(__u8 mode, __u8 channel);
__s32 Hdmi_Set_Audio_Para(hdmi_audio_t * audio_para);


#endif
