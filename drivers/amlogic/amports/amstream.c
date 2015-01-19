/*
 * AMLOGIC Audio/Video streaming port driver.
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 *
 * Author:  Tim Yao <timyao@amlogic.com>
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/amlogic/major.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/amlogic/amports/amstream.h>
#include <linux/amlogic/amports/vformat.h>
#include <linux/amlogic/amports/aformat.h>
#include <linux/amlogic/amports/tsync.h>
#include <linux/amlogic/amports/ptsserv.h>
#include <linux/amlogic/amports/timestamp.h>

#include <asm/types.h>
#include <asm/uaccess.h>
#include <asm/sections.h>
#include <asm/io.h>
#include <mach/am_regs.h>

#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/dma-mapping.h>
#include <linux/dma-contiguous.h>
#include <asm/uaccess.h>
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
#include <mach/mod_gate.h>
#include <mach/power_gate.h>
#endif
#include "streambuf.h"
#include "streambuf_reg.h"
#include "tsdemux.h"
#include "psparser.h"
#include "esparser.h"
#include "vdec.h"
#include "adec.h"
#include "rmparser.h"
#include "amvideocap_priv.h"
#include "amports_priv.h"
#include "amports_config.h"
#include "tsync_pcr.h"

#include <linux/firmware.h>

#include <linux/of.h>
#include <linux/of_fdt.h>

#define DEVICE_NAME "amstream-dev"
#define DRIVER_NAME "amstream"
#define MODULE_NAME "amstream"

#define MAX_AMSTREAM_PORT_NUM ARRAY_SIZE(ports)
u32 amstream_port_num;
u32 amstream_buf_num;

extern void set_real_audio_info(void *arg);
//#define DATA_DEBUG
static int use_bufferlevelx10000=10000;
static int reset_canuse_buferlevel(int level);
#ifdef DATA_DEBUG
#include <linux/fs.h>
#define DEBUG_FILE_NAME     "/tmp/debug.tmp"
static struct file* debug_filp = NULL;
static loff_t debug_file_pos = 0;

void debug_file_write(const char __user *buf, size_t count)
{
    mm_segment_t old_fs;

    if (!debug_filp) {
        return;
    }

    old_fs = get_fs();
    set_fs(KERNEL_DS);

    if (count != vfs_write(debug_filp, buf, count, &debug_file_pos)) {
        printk("Failed to write debug file\n");
    }

    set_fs(old_fs);

    return;
}
#endif

#define DEFAULT_VIDEO_BUFFER_SIZE       (1024*1024*3)
#define DEFAULT_AUDIO_BUFFER_SIZE       (1024*768*2)
#define DEFAULT_SUBTITLE_BUFFER_SIZE     (1024*256)

static int amstream_open
(struct inode *inode, struct file *file);
static int amstream_release
(struct inode *inode, struct file *file);
static long amstream_ioctl
(struct file *file,
 unsigned int cmd, ulong arg);
static ssize_t amstream_vbuf_write
(struct file *file, const char *buf,
 size_t count, loff_t * ppos);
static ssize_t amstream_abuf_write
(struct file *file, const char *buf,
 size_t count, loff_t * ppos);
static ssize_t amstream_mpts_write
(struct file *file, const char *buf,
 size_t count, loff_t * ppos);
static ssize_t amstream_mpps_write
(struct file *file, const char *buf,
 size_t count, loff_t * ppos);
static ssize_t amstream_sub_read
(struct file *file, char *buf,
 size_t count, loff_t * ppos);
static ssize_t amstream_sub_write
(struct file *file, const char *buf,
 size_t count, loff_t * ppos);
static unsigned int amstream_sub_poll
(struct file *file, poll_table *wait_table);
static unsigned int amstream_userdata_poll
(struct file *file, poll_table *wait_table);
static ssize_t amstream_userdata_read
(struct file *file, char *buf,
 size_t count, loff_t * ppos);
static int (*amstream_vdec_status)
(struct vdec_status *vstatus);
static int (*amstream_adec_status)
(struct adec_status *astatus);
static int (*amstream_vdec_trickmode)
(unsigned long trickmode);
static ssize_t amstream_mprm_write
(struct file *file, const char *buf,
 size_t count, loff_t * ppos);

const static struct file_operations vbuf_fops = {
    .owner    = THIS_MODULE,
    .open     = amstream_open,
    .release  = amstream_release,
    .write    = amstream_vbuf_write,
    .unlocked_ioctl    = amstream_ioctl,
};

const static struct file_operations abuf_fops = {
    .owner    = THIS_MODULE,
    .open     = amstream_open,
    .release  = amstream_release,
    .write    = amstream_abuf_write,
    .unlocked_ioctl    = amstream_ioctl,
};

const static struct file_operations mpts_fops = {
    .owner    = THIS_MODULE,
    .open     = amstream_open,
    .release  = amstream_release,
    .write    = amstream_mpts_write,
    .unlocked_ioctl    = amstream_ioctl,
};

const static struct file_operations mpps_fops = {
    .owner    = THIS_MODULE,
    .open     = amstream_open,
    .release  = amstream_release,
    .write    = amstream_mpps_write,
    .unlocked_ioctl    = amstream_ioctl,
};

const static struct file_operations mprm_fops = {
    .owner    = THIS_MODULE,
    .open     = amstream_open,
    .release  = amstream_release,
    .write    = amstream_mprm_write,
    .unlocked_ioctl    = amstream_ioctl,
};

const static struct file_operations sub_fops = {
    .owner    = THIS_MODULE,
    .open     = amstream_open,
    .release  = amstream_release,
    .write    = amstream_sub_write,
    .unlocked_ioctl    = amstream_ioctl,
};

const static struct file_operations sub_read_fops = {
    .owner    = THIS_MODULE,
    .open     = amstream_open,
    .release  = amstream_release,
    .read     = amstream_sub_read,
    .poll     = amstream_sub_poll,
    .unlocked_ioctl    = amstream_ioctl,
};

const static struct file_operations userdata_fops = {
    .owner    = THIS_MODULE,
    .open     = amstream_open,
    .release  = amstream_release,
    .read     = amstream_userdata_read,
    .poll     = amstream_userdata_poll,
    .unlocked_ioctl    = amstream_ioctl,
};

const static struct file_operations amstream_fops = {
    .owner    = THIS_MODULE,
    .open     = amstream_open,
    .release  = amstream_release,
    .unlocked_ioctl    = amstream_ioctl,
};

/**************************************************/
static struct audio_info audio_dec_info;
struct dec_sysinfo amstream_dec_info;
static struct class *amstream_dev_class;
static DEFINE_MUTEX(amstream_mutex);

atomic_t subdata_ready = ATOMIC_INIT(0);
static int sub_type;
static int sub_port_inited;
/* wait queue for poll */
static wait_queue_head_t amstream_sub_wait;
atomic_t userdata_ready = ATOMIC_INIT(0);
static wait_queue_head_t amstream_userdata_wait;

static stream_port_t ports[] = {
    {
        .name = "amstream_vbuf",
        .type = PORT_TYPE_ES | PORT_TYPE_VIDEO,
        .fops = &vbuf_fops,
    },
    {
        .name = "amstream_abuf",
        .type = PORT_TYPE_ES | PORT_TYPE_AUDIO,
        .fops = &abuf_fops,
    },
    {
        .name = "amstream_mpts",
        .type = PORT_TYPE_MPTS | PORT_TYPE_VIDEO | PORT_TYPE_AUDIO | PORT_TYPE_SUB,
        .fops = &mpts_fops,
    },
    {
        .name  = "amstream_mpps",
        .type  = PORT_TYPE_MPPS | PORT_TYPE_VIDEO | PORT_TYPE_AUDIO | PORT_TYPE_SUB,
        .fops  = &mpps_fops,
    },
    {
        .name  = "amstream_rm",
        .type  = PORT_TYPE_RM | PORT_TYPE_VIDEO | PORT_TYPE_AUDIO,
        .fops  = &mprm_fops,
    },
    {
        .name  = "amstream_sub",
        .type  = PORT_TYPE_SUB,
        .fops  = &sub_fops,
    },
    {
        .name  = "amstream_sub_read",
        .type  = PORT_TYPE_SUB_RD,
        .fops  = &sub_read_fops,
    },
    {
        .name  = "amstream_userdata",
        .type  = PORT_TYPE_USERDATA,
        .fops  = &userdata_fops,
    },
    {
        .name  = "amstream_hevc",
        .type  = PORT_TYPE_ES | PORT_TYPE_VIDEO | PORT_TYPE_HEVC,
        .fops  = &vbuf_fops,
        .vformat = VFORMAT_HEVC,
    },
};

static stream_buf_t bufs[BUF_MAX_NUM] = {
    {
        .reg_base = VLD_MEM_VIFIFO_REG_BASE,
        .type = BUF_TYPE_VIDEO,
        .buf_start = 0,
        .buf_size = 0,
        .first_tstamp = INVALID_PTS
    },
    {
        .reg_base = AIU_MEM_AIFIFO_REG_BASE,
        .type = BUF_TYPE_AUDIO,
        .buf_start = 0,
        .buf_size = 0,
        .first_tstamp = INVALID_PTS
    },
    {
        .reg_base = 0,
        .type = BUF_TYPE_SUBTITLE,
        .buf_start = 0,
        .buf_size = 0,
        .first_tstamp = INVALID_PTS
    },
    {
        .reg_base = 0,
        .type = BUF_TYPE_USERDATA,
        .buf_start = 0,
        .buf_size = 0,
        .first_tstamp = INVALID_PTS
    },
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
    {
        .reg_base = HEVC_STREAM_REG_BASE,
        .type = BUF_TYPE_HEVC,
        .buf_start = 0,
        .buf_size = 0,
        .first_tstamp = INVALID_PTS
    },
#endif
};

stream_buf_t *get_buf_by_type(u32  type)
{
    if (PTS_TYPE_VIDEO == type) {
        return &bufs[BUF_TYPE_VIDEO];
    } 
    if (PTS_TYPE_AUDIO == type) {
        return &bufs[BUF_TYPE_AUDIO];
    }
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
    if (!IS_MESON_M8_CPU) {
        if (PTS_TYPE_HEVC == type) {
            return &bufs[BUF_TYPE_HEVC];
        }    
    }
#endif

    return NULL;
}

void set_sample_rate_info(int arg)
{
    audio_dec_info.sample_rate = arg;
    audio_dec_info.valid = 1;
}
void set_ch_num_info(int arg)
{
    audio_dec_info.channels= arg;
}
struct audio_info *get_audio_info(void) {
    return &audio_dec_info;
}

EXPORT_SYMBOL(get_audio_info);

static void amstream_change_vbufsize(stream_port_t *port,struct stream_buf_s *pvbuf)
{
    int condition;

    if (HAS_HEVC_VDEC) {
        condition = (pvbuf->type == BUF_TYPE_VIDEO) || (pvbuf->type == BUF_TYPE_HEVC);
    } else {
        condition = pvbuf->type == BUF_TYPE_VIDEO;
    }

    if (pvbuf->type == condition) {
        if (port->vformat == VFORMAT_H264_4K2K){				
            pvbuf->buf_size = pvbuf->default_buf_size;

            //printk(" amstream_change_vbufsize 4k2k bufsize[0x%x] defaultsize[0x%x]\n",bufs[BUF_TYPE_VIDEO].buf_size,pvbuf->default_buf_size);
        }else if((pvbuf->default_buf_size > MAX_STREAMBUFFER_SIZE)&& (port->vformat != VFORMAT_H264_4K2K)) {
            pvbuf->buf_size = MAX_STREAMBUFFER_SIZE;

            //printk(" amstream_change_vbufsize MAX_STREAMBUFFER_SIZE-[0x%x] defaultsize-[0x%x] vformat-[%d]\n",bufs[BUF_TYPE_VIDEO].buf_size,pvbuf->default_buf_size,port->vformat);
        } else {
            //printk(" amstream_change_vbufsize bufsize[0x%x] defaultbufsize [0x%x] \n",bufs[BUF_TYPE_VIDEO].buf_size,pvbuf->default_buf_size);	
        }

        reset_canuse_buferlevel(10000);
    }

    return;
}

static  void video_port_release(stream_port_t *port, struct stream_buf_s * pbuf, int release_num)
{
    switch (release_num) {
    default:
    case 0: /*release all*/
        /*  */
    case 4:
        esparser_release(pbuf);
    case 3:
        vdec_release(port->vformat);
    case 2:
        stbuf_release(pbuf);
    case 1:
        ;
    }
    return ;
}
static  int video_port_init(stream_port_t *port, struct stream_buf_s * pbuf)
{
    int r;
    if ((port->flag & PORT_FLAG_VFORMAT) == 0) {
        printk("vformat not set\n");
        return -EPERM;
    }
	
    amstream_change_vbufsize(port,pbuf);

    if (HAS_HEVC_VDEC) {
        if (port->type & PORT_TYPE_MPTS) {
            if (pbuf->type == BUF_TYPE_HEVC) {
                vdec_poweroff(VDEC_1);
            } else {
                vdec_poweroff(VDEC_HEVC);
            }
        }
    }

    r = stbuf_init(pbuf);
    if (r < 0) {
        printk("video_port_init %d, stbuf_init failed\n", __LINE__);
        return r;
    }

    r = vdec_init(port->vformat);
    if (r < 0) {
        printk("video_port_init %d, vdec_init failed\n", __LINE__);
        video_port_release(port, pbuf, 2);
        return r;
    }

    if (port->type & PORT_TYPE_ES) {
        r = esparser_init(pbuf);
        if (r < 0) {
            video_port_release(port, pbuf, 3);
            printk("esparser_init() failed\n");
            return r;
        }
    }

    pbuf->flag |= BUF_FLAG_IN_USE;

    return 0;
}

static void audio_port_release(stream_port_t *port, struct stream_buf_s * pbuf, int release_num)
{
    switch (release_num) {
    default:
    case 0: /*release all*/
        /*  */
    case 4:
        esparser_release(pbuf);
    case 3:
        adec_release(port->vformat);
    case 2:
        stbuf_release(pbuf);
    case 1:
        ;
    }
    return ;
}

static int audio_port_reset(stream_port_t *port, struct stream_buf_s * pbuf)
{
    int r;

    if ((port->flag & PORT_FLAG_AFORMAT) == 0) {
        printk("aformat not set\n");
        return 0;
    }

    pts_stop(PTS_TYPE_AUDIO);

    stbuf_release(pbuf);

    r = stbuf_init(pbuf);
    if (r < 0) {
        return r;
    }

    r = adec_init(port);
    if (r < 0) {
        audio_port_release(port, pbuf, 2);
        return r;
    }

    if (port->type & PORT_TYPE_ES) {
        esparser_audio_reset(pbuf);
    }

    if (port->type & PORT_TYPE_MPTS) {
        tsdemux_audio_reset();
    }

    if (port->type & PORT_TYPE_MPPS) {
        psparser_audio_reset();
    }

    if (port->type & PORT_TYPE_RM) {
        rm_audio_reset();
    }

    pbuf->flag |= BUF_FLAG_IN_USE;

    pts_start(PTS_TYPE_AUDIO);

    return 0;
}

static int sub_port_reset(stream_port_t *port, struct stream_buf_s * pbuf)
{
    int r;

    port->flag &= (~PORT_FLAG_INITED);

    stbuf_release(pbuf);

    r = stbuf_init(pbuf);
    if (r < 0) {
        return r;
    }

    if (port->type & PORT_TYPE_MPTS) {
        tsdemux_sub_reset();
    }

    if (port->type & PORT_TYPE_MPPS) {
        psparser_sub_reset();
    }

    if (port->sid == 0xffff) { // es sub
        esparser_sub_reset();
        pbuf->flag |= BUF_FLAG_PARSER;
    }

    pbuf->flag |= BUF_FLAG_IN_USE;

    port->flag |= PORT_FLAG_INITED;

    return 0;
}

static  int audio_port_init(stream_port_t *port, struct stream_buf_s * pbuf)
{
    int r;

    if ((port->flag & PORT_FLAG_AFORMAT) == 0) {
        printk("aformat not set\n");
        return 0;
    }

    r = stbuf_init(pbuf);
    if (r < 0) {
        return r;
    }
    r = adec_init(port);
    if (r < 0) {
        audio_port_release(port, pbuf, 2);
        return r;
    }
    if (port->type & PORT_TYPE_ES) {
        r = esparser_init(pbuf);
        if (r < 0) {
            audio_port_release(port, pbuf, 3);
            return r;
        }
    }
    pbuf->flag |= BUF_FLAG_IN_USE;
    return 0;
}

static void sub_port_release(stream_port_t *port, struct stream_buf_s * pbuf)
{
    if ((port->sid == 0xffff) && ((port->type & (PORT_TYPE_MPPS | PORT_TYPE_MPTS)) == 0)) { // this is es sub
        esparser_release(pbuf);
    }
    stbuf_release(pbuf);
    sub_port_inited = 0;
    return;
}

static int sub_port_init(stream_port_t *port, struct stream_buf_s * pbuf)
{
    int r;
    if ((port->flag & PORT_FLAG_SID) == 0) {
        printk("subtitle id not set\n");
        return 0;
    }

    r = stbuf_init(pbuf);
    if (r < 0) {
        return r;
    }

    if ((port->sid == 0xffff) && ((port->type & (PORT_TYPE_MPPS | PORT_TYPE_MPTS)) == 0)) { // es sub
        r = esparser_init(pbuf);
        if (r < 0) {
            sub_port_release(port, pbuf);
            return r;
        }
    }

    sub_port_inited = 1;
    return 0;
}

static  int amstream_port_init(stream_port_t *port)
{
    int r;
    stream_buf_t *pvbuf = &bufs[BUF_TYPE_VIDEO];
    stream_buf_t *pabuf = &bufs[BUF_TYPE_AUDIO];
    stream_buf_t *psbuf = &bufs[BUF_TYPE_SUBTITLE];
    stream_buf_t *pubuf = &bufs[BUF_TYPE_USERDATA];

    if ((port->type & PORT_TYPE_AUDIO) && (port->flag & PORT_FLAG_AFORMAT)) {
        r = audio_port_init(port, pabuf);
        if (r < 0) {
            printk("audio_port_init  failed\n");
            goto error1;
        }
    }

    if ((port->type & PORT_TYPE_VIDEO) && (port->flag & PORT_FLAG_VFORMAT)) {
        pubuf->buf_size = 0;
        pubuf->buf_start = 0;
        pubuf->buf_wp = 0;
        pubuf->buf_rp = 0;

        if (HAS_HEVC_VDEC) {
            if (port->vformat == VFORMAT_HEVC) {
                pvbuf = &bufs[BUF_TYPE_HEVC];
            }
        }

        r = video_port_init(port, pvbuf);
        if (r < 0) {
            printk("video_port_init  failed\n");
            goto error2;
        }
    }

    if ((port->type & PORT_TYPE_SUB) && (port->flag & PORT_FLAG_SID)) {
        r = sub_port_init(port, psbuf);
        if (r < 0) {
            printk("sub_port_init  failed\n");
            goto error3;
        }
    }

    if (port->type & PORT_TYPE_MPTS) { 
	if (HAS_HEVC_VDEC) {
	       r = tsdemux_init((port->flag & PORT_FLAG_VID) ? port->vid : 0xffff,
	                         (port->flag & PORT_FLAG_AID) ? port->aid : 0xffff,
	                         (port->flag & PORT_FLAG_SID) ? port->sid : 0xffff,
	                         (port->pcr_inited == 1) ? port->pcrid : 0xffff,
	                         (port->vformat == VFORMAT_HEVC));
	}else{
	        r = tsdemux_init((port->flag & PORT_FLAG_VID) ? port->vid : 0xffff,
	                         (port->flag & PORT_FLAG_AID) ? port->aid : 0xffff,
	                         (port->flag & PORT_FLAG_SID) ? port->sid : 0xffff,
	                         (port->pcr_inited == 1) ? port->pcrid : 0xffff, 0);
	}
 
        if (r < 0) {
            printk("tsdemux_init  failed\n");
            goto error4;
        }
        tsync_pcr_start();
    }
    if (port->type & PORT_TYPE_MPPS) {
        r = psparser_init((port->flag & PORT_FLAG_VID) ? port->vid : 0xffff,
                          (port->flag & PORT_FLAG_AID) ? port->aid : 0xffff,
                          (port->flag & PORT_FLAG_SID) ? port->sid : 0xffff);
        if (r < 0) {
            printk("psparser_init  failed\n");
            goto error5;
        }
    }
    if (port->type & PORT_TYPE_RM) {
        rm_set_vasid((port->flag & PORT_FLAG_VID) ? port->vid : 0xffff,
                     (port->flag & PORT_FLAG_AID) ? port->aid : 0xffff);
    }

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6TVD
    if (!IS_MESON_M8M2_CPU) {
        if ((port->type & PORT_TYPE_VIDEO) && (port->vformat == VFORMAT_H264_4K2K)) {
            stbuf_vdec2_init(pvbuf);
        }
    }
#endif

    tsync_audio_break(0); // clear audio break

    port->flag |= PORT_FLAG_INITED;
    return 0;
    /*errors follow here*/
error5:
    tsdemux_release();
error4:
    sub_port_release(port, psbuf);
error3:
    video_port_release(port, pvbuf, 0);
error2:
    audio_port_release(port, pabuf, 0);
error1:

    return r;
}
static  int amstream_port_release(stream_port_t *port)
{
    stream_buf_t *pvbuf = &bufs[BUF_TYPE_VIDEO];
    stream_buf_t *pabuf = &bufs[BUF_TYPE_AUDIO];
    stream_buf_t *psbuf = &bufs[BUF_TYPE_SUBTITLE];

    if (HAS_HEVC_VDEC) {
        if (port->vformat == VFORMAT_HEVC) {
            pvbuf = &bufs[BUF_TYPE_HEVC];
        }
    }

    if (port->type & PORT_TYPE_MPTS) {
    	 tsync_pcr_stop();
        tsdemux_release();
    }

    if (port->type & PORT_TYPE_MPPS) {
        psparser_release();
    }


    if (port->type & PORT_TYPE_VIDEO) {
        video_port_release(port, pvbuf, 0);
    }

    if (port->type & PORT_TYPE_AUDIO) {
        audio_port_release(port, pabuf, 0);
    }

    if (port->type & PORT_TYPE_SUB) {
        sub_port_release(port, psbuf);
    }

    port->pcr_inited=0;
    port->flag = 0;
    return 0;
}

static void amstream_change_avid(stream_port_t *port)
{
    if (port->type & PORT_TYPE_MPTS) {
        tsdemux_change_avid((port->flag & PORT_FLAG_VID) ? port->vid : 0xffff,
                            (port->flag & PORT_FLAG_AID) ? port->aid : 0xffff);
    }

    if (port->type & PORT_TYPE_MPPS) {
        psparser_change_avid((port->flag & PORT_FLAG_VID) ? port->vid : 0xffff,
                             (port->flag & PORT_FLAG_AID) ? port->aid : 0xffff);
    }

    if (port->type & PORT_TYPE_RM) {
        rm_set_vasid((port->flag & PORT_FLAG_VID) ? port->vid : 0xffff,
                     (port->flag & PORT_FLAG_AID) ? port->aid : 0xffff);
    }

    return;
}

static void amstream_change_sid(stream_port_t *port)
{
    if (port->type & PORT_TYPE_MPTS) {
        tsdemux_change_sid((port->flag & PORT_FLAG_SID) ? port->sid : 0xffff);
    }

    if (port->type & PORT_TYPE_MPPS) {
        psparser_change_sid((port->flag & PORT_FLAG_SID) ? port->sid : 0xffff);
    }

    return;
}

/**************************************************/
static ssize_t amstream_vbuf_write(struct file *file, const char *buf,
                                   size_t count, loff_t * ppos)
{
    stream_port_t *port = (stream_port_t *)file->private_data;
    stream_buf_t *pbuf = NULL;
    int r;

    if (HAS_HEVC_VDEC) {
        pbuf = (port->type & PORT_TYPE_HEVC) ? &bufs[BUF_TYPE_HEVC] : &bufs[BUF_TYPE_VIDEO];
    } else {
        pbuf = &bufs[BUF_TYPE_VIDEO];
    }
    
    if (!(port->flag & PORT_FLAG_INITED)) {
        r = amstream_port_init(port);
        if (r < 0) {
            return r;
        }
    }

    if (port->flag & PORT_FLAG_DRM) {
        r = drm_write(file, pbuf, buf, count);
    } else {
        r = esparser_write(file, pbuf, buf, count);
    }
#ifdef DATA_DEBUG
    debug_file_write(buf, r);
#endif

    return r;
}

static ssize_t amstream_abuf_write(struct file *file, const char *buf,
                                   size_t count, loff_t * ppos)
{
    stream_port_t *port = (stream_port_t *)file->private_data;
    stream_buf_t *pbuf = &bufs[BUF_TYPE_AUDIO];
    int r;

    if (!(port->flag & PORT_FLAG_INITED)) {
        r = amstream_port_init(port);
        if (r < 0) {
            return r;
        }
    }

    if (port->flag & PORT_FLAG_DRM) {
        r = drm_write(file, pbuf, buf, count);
    } else {
        r = esparser_write(file, pbuf, buf, count);
    }

    return r;
}

static ssize_t amstream_mpts_write(struct file *file, const char *buf,
                                   size_t count, loff_t * ppos)
{
    stream_port_t *port = (stream_port_t *)file->private_data;
    stream_buf_t *pabuf = &bufs[BUF_TYPE_AUDIO];
    stream_buf_t *pvbuf = NULL;
    int r;

    if (HAS_HEVC_VDEC) {
        pvbuf = (port->vformat == VFORMAT_HEVC) ? &bufs[BUF_TYPE_HEVC] : &bufs[BUF_TYPE_VIDEO];
    } else {
        pvbuf = &bufs[BUF_TYPE_VIDEO];
    }

    if (!(port->flag & PORT_FLAG_INITED)) {
        r = amstream_port_init(port);
        if (r < 0) {
            return r;
        }
    }

#ifdef DATA_DEBUG
    debug_file_write(buf, count);
#endif
    return tsdemux_write(file, pvbuf, pabuf, buf, count);
}

static ssize_t amstream_mpps_write(struct file *file, const char *buf,
                                   size_t count, loff_t * ppos)
{
    stream_port_t *port = (stream_port_t *)file->private_data;
    stream_buf_t *pvbuf = &bufs[BUF_TYPE_VIDEO];
    stream_buf_t *pabuf = &bufs[BUF_TYPE_AUDIO];
    int r;

    if (!(port->flag & PORT_FLAG_INITED)) {
        r = amstream_port_init(port);
        if (r < 0) {
            return r;
        }
    }
    return psparser_write(file, pvbuf, pabuf, buf, count);
}

static ssize_t amstream_mprm_write(struct file *file, const char *buf,
                                   size_t count, loff_t * ppos)
{
    stream_port_t *port = (stream_port_t *)file->private_data;
    stream_buf_t *pvbuf = &bufs[BUF_TYPE_VIDEO];
    stream_buf_t *pabuf = &bufs[BUF_TYPE_AUDIO];
    int r;

    if (!(port->flag & PORT_FLAG_INITED)) {
        r = amstream_port_init(port);
        if (r < 0) {
            return r;
        }
    }
    return rmparser_write(file, pvbuf, pabuf, buf, count);
}

static ssize_t amstream_sub_read(struct file *file, char __user *buf, size_t count, loff_t * ppos)
{
    u32 sub_rp, sub_wp, sub_start, data_size, res;
    stream_buf_t *s_buf = &bufs[BUF_TYPE_SUBTITLE];

    if (sub_port_inited == 0) {
        return 0;
    }

    sub_rp = stbuf_sub_rp_get();
    sub_wp = stbuf_sub_wp_get();
    sub_start = stbuf_sub_start_get();

    if (sub_wp == sub_rp) {
        return 0;
    }

    if (sub_wp > sub_rp) {
        data_size = sub_wp - sub_rp;
    } else {
        data_size = s_buf->buf_size - sub_rp + sub_wp;
    }

    if (data_size > count) {
        data_size = count;
    }

    if (sub_wp < sub_rp) {
        int first_num = s_buf->buf_size - (sub_rp - sub_start);

        if (data_size <= first_num) {
            res = copy_to_user((void *)buf, (void *)(phys_to_virt(sub_rp)), data_size);
            if (res >= 0) {
                stbuf_sub_rp_set(sub_rp + data_size - res);
            }

            return data_size - res;
        } else {
            if (first_num > 0) {
                res = copy_to_user((void *)buf, (void *)(phys_to_virt(sub_rp)), first_num);
                if (res >= 0) {
                    stbuf_sub_rp_set(sub_rp + first_num - res);
                }

                return first_num - res;
            }

            res = copy_to_user((void *)buf, (void *)(phys_to_virt(sub_start)), data_size - first_num);

            if (res >= 0) {
                stbuf_sub_rp_set(sub_start + data_size - first_num - res);
            }

            return data_size - first_num - res;
        }
    } else {
        res = copy_to_user((void *)buf, (void *)(phys_to_virt(sub_rp)), data_size);

        if (res >= 0) {
            stbuf_sub_rp_set(sub_rp + data_size - res);
        }

        return data_size - res;
    }
}

static ssize_t amstream_sub_write(struct file *file, const char *buf,
                                  size_t count, loff_t * ppos)
{
    stream_port_t *port = (stream_port_t *)file->private_data;
    stream_buf_t *pbuf = &bufs[BUF_TYPE_SUBTITLE];
    int r;

    if (!(port->flag & PORT_FLAG_INITED)) {
        r = amstream_port_init(port);
        if (r < 0) {
            return r;
        }
    }
		printk("amstream_sub_write\n");
    r = esparser_write(file, pbuf, buf, count);
    if (r < 0) {
        return r;
    }

    wakeup_sub_poll();

    return r;
}

static unsigned int amstream_sub_poll(struct file *file, poll_table *wait_table)
{
    poll_wait(file, &amstream_sub_wait, wait_table);

    if (atomic_read(&subdata_ready)) {
        atomic_dec(&subdata_ready);
        return POLLOUT | POLLWRNORM;
    }

    return 0;
}

int wakeup_userdata_poll(int wp, int start_phyaddr, int buf_size)
{
    stream_buf_t *userdata_buf = &bufs[BUF_TYPE_USERDATA];
	userdata_buf->buf_start= start_phyaddr;
	userdata_buf->buf_wp = wp;
	userdata_buf->buf_size = buf_size;
    atomic_set(&userdata_ready, 1);
    wake_up_interruptible(&amstream_userdata_wait);
    return userdata_buf->buf_rp;
}

static unsigned int amstream_userdata_poll(struct file *file, poll_table *wait_table)
{
    poll_wait(file, &amstream_userdata_wait, wait_table);
    if (atomic_read(&userdata_ready)) {
        atomic_set(&userdata_ready, 0);
        return POLLIN | POLLRDNORM;
    }
    return 0;
}
static ssize_t amstream_userdata_read(struct file *file, char __user *buf, size_t count, loff_t * ppos)
{
    u32  data_size, res, retVal = 0, buf_wp;
    stream_buf_t *userdata_buf = &bufs[BUF_TYPE_USERDATA];
    buf_wp = userdata_buf->buf_wp;
    if (userdata_buf->buf_start == 0 || userdata_buf->buf_size == 0) {
        return 0;
    }
    if (buf_wp == userdata_buf->buf_rp) {
        return 0;
    }
    if (buf_wp > userdata_buf->buf_rp) {
        data_size = buf_wp - userdata_buf->buf_rp;
    } else {
        data_size = userdata_buf->buf_size - userdata_buf->buf_rp + buf_wp;
    }
    if (data_size > count) {
        data_size = count;
    }
    if (buf_wp < userdata_buf->buf_rp) {
        int first_num = userdata_buf->buf_size - userdata_buf->buf_rp ;
        if (data_size <= first_num) {
            res = copy_to_user((void *)buf, (void *)(phys_to_virt(userdata_buf->buf_rp + userdata_buf->buf_start)), data_size);
            userdata_buf->buf_rp +=  data_size - res;
            retVal =  data_size - res;
        } else {
            if (first_num > 0) {
                res = copy_to_user((void *)buf, (void *)(phys_to_virt(userdata_buf->buf_rp + userdata_buf->buf_start)), first_num);
               userdata_buf->buf_rp += first_num - res;
                retVal = first_num - res;
            }	else	{
            res = copy_to_user((void *)buf, (void *)(phys_to_virt(userdata_buf->buf_start)), data_size - first_num);
		userdata_buf->buf_rp =  data_size - first_num - res;
            retVal =  data_size - first_num - res;
            }
        }
    } else {
        res = copy_to_user((void *)buf, (void *)(phys_to_virt(userdata_buf->buf_rp + userdata_buf->buf_start)), data_size);
	userdata_buf->buf_rp +=  data_size - res;
        retVal = data_size - res;
    }
	return retVal;
}

static int amstream_open(struct inode *inode, struct file *file)
{
    s32 i;
    stream_port_t *s;
    stream_port_t *this = &ports[iminor(inode)];

    if (iminor(inode) >= amstream_port_num) {
        return (-ENODEV);
    }

    if (this->flag & PORT_FLAG_IN_USE) {
        return (-EBUSY);
    }

    /* check other ports conflict */
    for (s = &ports[0], i = 0; i < amstream_port_num; i++, s++) {
        if ((s->flag & PORT_FLAG_IN_USE) &&
            ((this->type) & (s->type) & (PORT_TYPE_VIDEO | PORT_TYPE_AUDIO))) {
            return (-EBUSY);
        }
    }

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
    switch_mod_gate_by_name("demux", 1);

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6TVD
    CLK_GATE_ON(HIU_PARSER_TOP);
#endif

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
    CLK_GATE_ON(VPU_INTR);
#endif

    if (this->type & PORT_TYPE_VIDEO) {
        switch_mod_gate_by_name("vdec", 1);

        if (HAS_HEVC_VDEC) {
            if (this->type & (PORT_TYPE_MPTS | PORT_TYPE_HEVC)) {
                vdec_poweron(VDEC_HEVC);
            }

            if ((this->type & PORT_TYPE_HEVC) == 0) {
                vdec_poweron(VDEC_1);
            }
        } else {
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6TVD
            vdec_poweron(VDEC_1);
#endif
        }

        memset(&amstream_dec_info, 0, sizeof(amstream_dec_info));
    }

    if (this->type & PORT_TYPE_AUDIO) {
        switch_mod_gate_by_name("audio", 1);
    }
#endif

    this->vid = 0;
    this->aid = 0;
    this->sid = 0;
    this->pcrid = 0xffff;
    file->f_op = this->fops;
    file->private_data = this;

    this->flag = PORT_FLAG_IN_USE;
    this->pcr_inited = 0;
#ifdef DATA_DEBUG
    debug_filp = filp_open(DEBUG_FILE_NAME, O_WRONLY, 0);
    if (IS_ERR(debug_filp)) {
        printk("amstream: open debug file failed\n");
        debug_filp = NULL;
    }
#endif

    return 0;
}

static int amstream_release(struct inode *inode, struct file *file)
{
    stream_port_t *this = &ports[iminor(inode)];

    if (iminor(inode) >= amstream_port_num) {
        return (-ENODEV);
    }
    if (this->flag & PORT_FLAG_INITED) {
        amstream_port_release(this);
    }
    if ((this->type & (PORT_TYPE_AUDIO | PORT_TYPE_VIDEO)) == PORT_TYPE_AUDIO) {
        s32 i;
        stream_port_t *s;
        for (s = &ports[0], i = 0; i < amstream_port_num; i++, s++) {
            if ((s->flag & PORT_FLAG_IN_USE) && (s->type & PORT_TYPE_VIDEO)) {
                break;
            }
        }
        if (i == amstream_port_num) {
            timestamp_firstvpts_set(0);
        }
    }    
    this->flag = 0;

    ///timestamp_pcrscr_set(0);

#ifdef DATA_DEBUG
    if (debug_filp) {
        filp_close(debug_filp, current->files);
        debug_filp = NULL;
        debug_file_pos = 0;
    }
#endif

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
    if (this->type & PORT_TYPE_VIDEO) {
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6TVD
        if (HAS_HEVC_VDEC) {
            vdec_poweroff(VDEC_HEVC);
        }
        
        vdec_poweroff(VDEC_1);
#endif

        switch_mod_gate_by_name("vdec", 0);
    }

    if (this->type & PORT_TYPE_AUDIO) {
        switch_mod_gate_by_name("audio", 0);
    }

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
    CLK_GATE_OFF(VPU_INTR);
#endif

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6TVD
    CLK_GATE_OFF(HIU_PARSER_TOP);
#endif

    switch_mod_gate_by_name("demux", 0);
#endif 

    return 0;
}

static long amstream_ioctl(struct file *file,
                          unsigned int cmd, ulong arg)
{
    s32 r = 0;
    struct inode *inode = file->f_dentry->d_inode;
    stream_port_t *this = &ports[iminor(inode)];

    switch (cmd) {

    case AMSTREAM_IOC_VB_START:
        if ((this->type & PORT_TYPE_VIDEO) &&
            ((bufs[BUF_TYPE_VIDEO].flag & BUF_FLAG_IN_USE) == 0)) {
            if (HAS_HEVC_VDEC) {
                bufs[BUF_TYPE_HEVC].buf_start = arg;
            }
            bufs[BUF_TYPE_VIDEO].buf_start = arg;
        } else {
            r = -EINVAL;
        }
        break;

    case AMSTREAM_IOC_VB_SIZE:
        if ((this->type & PORT_TYPE_VIDEO) &&
            ((bufs[BUF_TYPE_VIDEO].flag & BUF_FLAG_IN_USE) == 0)) {
            if (bufs[BUF_TYPE_VIDEO].flag & BUF_FLAG_ALLOC) {
                if (HAS_HEVC_VDEC) {
                    r = stbuf_change_size(&bufs[BUF_TYPE_HEVC], arg);
                }
                r = stbuf_change_size(&bufs[BUF_TYPE_VIDEO], arg);
            }
        } else {
            r = -EINVAL;
        }
        break;

    case AMSTREAM_IOC_AB_START:
        if ((this->type & PORT_TYPE_AUDIO) &&
            ((bufs[BUF_TYPE_AUDIO].flag & BUF_FLAG_IN_USE) == 0)) {
            bufs[BUF_TYPE_AUDIO].buf_start = arg;
        } else {
            r = -EINVAL;
        }
        break;

    case AMSTREAM_IOC_AB_SIZE:
        if ((this->type & PORT_TYPE_AUDIO) &&
            ((bufs[BUF_TYPE_AUDIO].flag & BUF_FLAG_IN_USE) == 0)) {
            if (bufs[BUF_TYPE_AUDIO].flag & BUF_FLAG_ALLOC) {
                r = stbuf_change_size(&bufs[BUF_TYPE_AUDIO], arg);
            }
        } else {
            r = -EINVAL;
        }
        break;

    case AMSTREAM_IOC_VFORMAT:
        if ((this->type & PORT_TYPE_VIDEO) &&
            (arg < VFORMAT_MAX)) {
            this->vformat = (vformat_t)arg;
            this->flag |= PORT_FLAG_VFORMAT;
        } else {
            r = -EINVAL;
        }
        break;

    case AMSTREAM_IOC_AFORMAT:
        if ((this->type & PORT_TYPE_AUDIO) &&
            (arg < AFORMAT_MAX)) {
    	      memset(&audio_dec_info,0,sizeof(struct audio_info));//for new format,reset the audio info.
            this->aformat = (aformat_t)arg;
            this->flag |= PORT_FLAG_AFORMAT;
        } else {
            r = -EINVAL;
        }
        break;

    case AMSTREAM_IOC_VID:
        if (this->type & PORT_TYPE_VIDEO) {
            this->vid = (u32)arg;
            this->flag |= PORT_FLAG_VID;
        } else {
            r = -EINVAL;
        }

        break;

    case AMSTREAM_IOC_AID:
        if (this->type & PORT_TYPE_AUDIO) {
            this->aid = (u32)arg;
            this->flag |= PORT_FLAG_AID;

            if (this->flag & PORT_FLAG_INITED) {
                tsync_audio_break(1);
                amstream_change_avid(this);
            }
        } else {
            r = -EINVAL;
        }
        break;

    case AMSTREAM_IOC_SID:
        if (this->type & PORT_TYPE_SUB) {
            this->sid = (u32)arg;
            this->flag |= PORT_FLAG_SID;

            if (this->flag & PORT_FLAG_INITED) {
                amstream_change_sid(this);
            }
        } else {
            r = -EINVAL;
        }

        break;

    case AMSTREAM_IOC_PCRID:
	this->pcrid= (u32)arg;
	this->pcr_inited = 1;
       printk("set pcrid = 0x%x \n", this->pcrid);
    	break;
    	
    case AMSTREAM_IOC_VB_STATUS:
        if (this->type & PORT_TYPE_VIDEO) {
            struct am_io_param para;
            struct am_io_param *p = &para;
            stream_buf_t *buf = NULL;

            if (HAS_HEVC_VDEC) {
                buf = (this->vformat == VFORMAT_HEVC) ? &bufs[BUF_TYPE_HEVC] : &bufs[BUF_TYPE_VIDEO];
            } else {
                buf = &bufs[BUF_TYPE_VIDEO];
            }

            if (p == NULL) {
                r = -EINVAL;
            }

            p->status.size = stbuf_canusesize(buf);
            p->status.data_len = stbuf_level(buf);
            p->status.free_len = stbuf_space(buf);
            p->status.read_pointer = stbuf_rp(buf);
            if(copy_to_user((void *)arg,p,sizeof(para)))
                r = -EFAULT;
            return r;	
        } else {
            r = -EINVAL;
        }
        break;

    case AMSTREAM_IOC_AB_STATUS:
        if (this->type & PORT_TYPE_AUDIO) {
            struct am_io_param para;
            struct am_io_param *p = &para;
            stream_buf_t *buf = &bufs[BUF_TYPE_AUDIO];

            if (p == NULL) {
                r = -EINVAL;
            }

            p->status.size = stbuf_canusesize(buf);
            p->status.data_len = stbuf_level(buf);
            p->status.free_len = stbuf_space(buf);
            p->status.read_pointer = stbuf_rp(buf);
            if(copy_to_user((void *)arg,p,sizeof(para)))
               r = -EFAULT;
            return r;	
        } else {
            r = -EINVAL;
        }
        break;

    case AMSTREAM_IOC_SYSINFO:
        if (this->type & PORT_TYPE_VIDEO) {
            if (copy_from_user((void *)&amstream_dec_info, (void *)arg, sizeof(amstream_dec_info))) {
                r = -EFAULT;
            }
        } else {
            r = -EINVAL;
        }
        break;

    case AMSTREAM_IOC_ACHANNEL:
        if (this->type & PORT_TYPE_AUDIO) {
            this->achanl = (u32)arg;
	      set_ch_num_info(	(u32)arg);	
        } else {
            r = -EINVAL;
        }
        break;

    case AMSTREAM_IOC_SAMPLERATE:
        if (this->type & PORT_TYPE_AUDIO) {
            this->asamprate = (u32)arg;
            set_sample_rate_info((u32)arg);
        } else {
            r = -EINVAL;
        }
        break;

    case AMSTREAM_IOC_DATAWIDTH:
        if (this->type & PORT_TYPE_AUDIO) {
            this->adatawidth = (u32)arg;
        } else {
            r = -EINVAL;
        }
        break;

    case AMSTREAM_IOC_TSTAMP:
        if ((this->type & (PORT_TYPE_AUDIO | PORT_TYPE_VIDEO)) ==
            ((PORT_TYPE_AUDIO | PORT_TYPE_VIDEO))) {
            r = -EINVAL;
        } 
        else if (HAS_HEVC_VDEC && this->type & PORT_TYPE_HEVC) {
            r = es_vpts_checkin(&bufs[BUF_TYPE_HEVC], arg);
        }
        else if (this->type & PORT_TYPE_VIDEO) {
            r = es_vpts_checkin(&bufs[BUF_TYPE_VIDEO], arg);
        }
        else if (this->type & PORT_TYPE_AUDIO) {
            r = es_apts_checkin(&bufs[BUF_TYPE_AUDIO], arg);
        }
        break;

    case AMSTREAM_IOC_TSTAMP_uS64:
        if ((this->type & (PORT_TYPE_AUDIO | PORT_TYPE_VIDEO)) ==
        	((PORT_TYPE_AUDIO | PORT_TYPE_VIDEO))) {	
        	r = -EINVAL;
        } else{
            u64 pts;
            if(copy_from_user((void*)&pts,(void *)arg,sizeof(u64)))
                return -EFAULT;
            if (HAS_HEVC_VDEC) {
                if (this->type & PORT_TYPE_HEVC) {
                    r = es_vpts_checkin_us64(&bufs[BUF_TYPE_HEVC], pts);
                } else if (this->type & PORT_TYPE_VIDEO) {
                    r = es_vpts_checkin_us64(&bufs[BUF_TYPE_VIDEO],pts);
                } else if (this->type & PORT_TYPE_AUDIO) {
                    r = es_vpts_checkin_us64(&bufs[BUF_TYPE_AUDIO],pts);
                }
            } else {
                if (this->type & PORT_TYPE_VIDEO) {
                    r = es_vpts_checkin_us64(&bufs[BUF_TYPE_VIDEO],pts);
                } else if (this->type & PORT_TYPE_AUDIO) {
                    r = es_vpts_checkin_us64(&bufs[BUF_TYPE_AUDIO],pts);
                }
            }
        }
        break;

    case AMSTREAM_IOC_VDECSTAT:
        if ((this->type & PORT_TYPE_VIDEO) == 0) {
            return -EINVAL;
        }
        if (amstream_vdec_status == NULL) {
            return -ENODEV;
        } else {
            struct vdec_status vstatus;
            struct am_io_param para;
            struct am_io_param *p = &para;
            if (p == NULL) {
                return -EINVAL;
            }
            amstream_vdec_status(&vstatus);
            p->vstatus.width = vstatus.width;
            p->vstatus.height = vstatus.height;
            p->vstatus.fps = vstatus.fps;
            p->vstatus.error_count = vstatus.error_count;
            p->vstatus.status = vstatus.status;
            if(copy_to_user((void*)arg,p,sizeof(para)))
                r = -EFAULT;
            return r;
        }

    case AMSTREAM_IOC_ADECSTAT:
        if ((this->type & PORT_TYPE_AUDIO) == 0) {
            return -EINVAL;
        }
        if (amstream_adec_status == NULL) {
            return -ENODEV;
        } else {
            struct adec_status astatus;
            struct am_io_param para;
            struct am_io_param *p = &para;

            if (p == NULL) {
                return -EINVAL;
            }
            amstream_adec_status(&astatus);
            p->astatus.channels = astatus.channels;
            p->astatus.sample_rate = astatus.sample_rate;
            p->astatus.resolution = astatus.resolution;
            p->astatus.error_count = astatus.error_count;
            p->astatus.status = astatus.status;
            if(copy_to_user((void *)arg,p,sizeof(para)))
                r = -EFAULT;
            return r;
        }

    case AMSTREAM_IOC_PORT_INIT:
        r = amstream_port_init(this);
        break;

    case AMSTREAM_IOC_TRICKMODE:
        if ((this->type & PORT_TYPE_VIDEO) == 0) {
            return -EINVAL;
        }
        if (amstream_vdec_trickmode == NULL) {
            return -ENODEV;
        } else {
            amstream_vdec_trickmode(arg);
        }
        break;

    case AMSTREAM_IOC_AUDIO_INFO:
        if ((this->type & PORT_TYPE_VIDEO) || (this->type & PORT_TYPE_AUDIO)) {
            if (copy_from_user(&audio_dec_info, (void __user *)arg, sizeof(audio_dec_info))) {
                r = -EFAULT;
            }
        } else {
            r = -EINVAL;
        }
        break;

    case AMSTREAM_IOC_AUDIO_RESET:
        if (this->type & PORT_TYPE_AUDIO) {
            stream_buf_t *pabuf = &bufs[BUF_TYPE_AUDIO];

            r = audio_port_reset(this, pabuf);
        } else {
            r = -EINVAL;
        }

        break;

    case AMSTREAM_IOC_SUB_RESET:
        if (this->type & PORT_TYPE_SUB) {
            stream_buf_t *psbuf = &bufs[BUF_TYPE_SUBTITLE];

            r = sub_port_reset(this, psbuf);
        } else {
            r = -EINVAL;
        }
        break;

    case AMSTREAM_IOC_SUB_LENGTH:
        if (this->type & PORT_TYPE_SUB || this->type & PORT_TYPE_SUB_RD) {
            u32 sub_wp, sub_rp;
            stream_buf_t *psbuf = &bufs[BUF_TYPE_SUBTITLE];
            int val;
            sub_wp = stbuf_sub_wp_get();
            sub_rp = stbuf_sub_rp_get();

            if (sub_wp == sub_rp) {
                val= 0;
            } else if (sub_wp > sub_rp) {
                val = sub_wp - sub_rp;
            } else {
                val = psbuf->buf_size - (sub_rp - sub_wp);
            }
            put_user(val,(unsigned long __user *)arg);
        } else {
            r = -EINVAL;
        }
        break;

    case AMSTREAM_IOC_SET_DEC_RESET:
        tsync_set_dec_reset();
        break;

    case AMSTREAM_IOC_TS_SKIPBYTE:
        if ((int)arg >= 0) {
            tsdemux_set_skipbyte(arg);
        } else {
            r = -EINVAL;
        }
        break;

    case AMSTREAM_IOC_SUB_TYPE:
        sub_type = (int)arg;
        break;

    case AMSTREAM_IOC_APTS_LOOKUP:
             if (this->type & PORT_TYPE_AUDIO)
             {
                      u32 pts=0,offset;
                      get_user(offset, (unsigned long __user *)arg);
                      pts_lookup_offset(PTS_TYPE_AUDIO, offset, &pts, 300);
                      put_user(pts,(unsigned long __user *)arg);
                 }
                 return 0;
    case GET_FIRST_APTS_FLAG:
        if (this->type & PORT_TYPE_AUDIO)
        {
            put_user(first_pts_checkin_complete(PTS_TYPE_AUDIO),(unsigned long __user *)arg);
        }
        break;

    case AMSTREAM_IOC_APTS:
        put_user(timestamp_apts_get(),(unsigned long __user *)arg);
        break;

    case AMSTREAM_IOC_VPTS:
        put_user(timestamp_vpts_get(),(unsigned long __user *)arg);
        break;

    case AMSTREAM_IOC_PCRSCR:
        put_user(timestamp_pcrscr_get(),(unsigned long __user *)arg);
        break;
		
    case AMSTREAM_IOC_SET_PCRSCR:
        timestamp_pcrscr_set(arg);
        break;
    case AMSTREAM_IOC_GET_LAST_CHECKIN_APTS:
        put_user(get_last_checkin_pts(PTS_TYPE_AUDIO),(int *)arg);
        break;
    case AMSTREAM_IOC_GET_LAST_CHECKIN_VPTS:
        put_user(get_last_checkin_pts(PTS_TYPE_VIDEO),(int *)arg);
        break;
    case AMSTREAM_IOC_GET_LAST_CHECKOUT_APTS:
        put_user(get_last_checkout_pts(PTS_TYPE_AUDIO),(int *)arg);
        break;
    case AMSTREAM_IOC_GET_LAST_CHECKOUT_VPTS:
        put_user(get_last_checkout_pts(PTS_TYPE_VIDEO),(int *)arg);
        break;
    case AMSTREAM_IOC_SUB_NUM:
        put_user(psparser_get_sub_found_num(),(int *)arg);
        break;

    case AMSTREAM_IOC_SUB_INFO:
        if (arg > 0) {
            struct subtitle_info msub_info[MAX_SUB_NUM];
            struct subtitle_info *psub_info[MAX_SUB_NUM];
            int i;

            for (i = 0; i < MAX_SUB_NUM; i ++) {
                psub_info[i] = &msub_info[i];
            }					

            r = psparser_get_sub_info(psub_info);

            if(r == 0) {
                if (copy_to_user((void __user *)arg, msub_info, sizeof(struct subtitle_info) * MAX_SUB_NUM)) {
                    r = -EFAULT;
                }
            }
        }
        break;
    case AMSTREAM_IOC_SET_DEMUX:
        tsdemux_set_demux((int)arg);
        break;
    case AMSTREAM_IOC_SET_VIDEO_DELAY_LIMIT_MS:
        if (HAS_HEVC_VDEC) {
            bufs[BUF_TYPE_HEVC].max_buffer_delay_ms = (int)arg;
        }
        bufs[BUF_TYPE_VIDEO].max_buffer_delay_ms = (int)arg;
        break;
    case AMSTREAM_IOC_SET_AUDIO_DELAY_LIMIT_MS:
        bufs[BUF_TYPE_AUDIO].max_buffer_delay_ms = (int)arg;
        break;
    case AMSTREAM_IOC_GET_VIDEO_DELAY_LIMIT_MS:
        put_user(bufs[BUF_TYPE_VIDEO].max_buffer_delay_ms,(int *)arg);
        break;
    case AMSTREAM_IOC_GET_AUDIO_DELAY_LIMIT_MS:
        put_user(bufs[BUF_TYPE_AUDIO].max_buffer_delay_ms,(int *)arg);
        break;		
    case AMSTREAM_IOC_GET_VIDEO_CUR_DELAY_MS:
        {
            int delay;
            delay = calculation_stream_delayed_ms(PTS_TYPE_VIDEO,NULL,NULL);
            if (delay >= 0) {
                put_user(delay,(int *)arg);
            } else {
                put_user(0,(int *)arg);
            }
        }
        break;

    case AMSTREAM_IOC_GET_AUDIO_CUR_DELAY_MS:
        {
            int delay;
            delay = calculation_stream_delayed_ms(PTS_TYPE_AUDIO,NULL,NULL);
            if (delay >= 0) {
                put_user(delay,(int *)arg);
            } else {
                put_user(0,(int *)arg);
            }
        }
        break;
	case AMSTREAM_IOC_GET_AUDIO_AVG_BITRATE_BPS:
		{
		int delay;
		u32 avgbps;
		delay=calculation_stream_delayed_ms(PTS_TYPE_AUDIO,NULL,&avgbps);
		if(delay>=0)
			put_user(avgbps,(int *)arg);
		else 
			put_user(0,(int *)arg);
        break;
		}
	case AMSTREAM_IOC_GET_VIDEO_AVG_BITRATE_BPS:
		{
		int delay;
		u32 avgbps;
		delay=calculation_stream_delayed_ms(PTS_TYPE_VIDEO,NULL,&avgbps);
		if(delay>=0)
			put_user(avgbps,(int *)arg);
		else 
			put_user(0,(int *)arg);
        break;		
		}
	case AMSTREAM_IOC_SET_DRMMODE:
		if((u32)arg==1){
            printk("set drmmode\n");
			this->flag |= PORT_FLAG_DRM;
		}else{
		    this->flag &= (~PORT_FLAG_DRM);
			printk("no drmmode\n");
		}
		break;
     case AMSTREAM_IOC_SET_APTS:
        {
            unsigned long pts;
            if(get_user(pts, (unsigned long __user *)arg)){
                printk("Get audio pts from user space fault! \n");
                return -EFAULT;
            }
	    if(tsync_get_mode()==TSYNC_MODE_PCRMASTER)
		tsync_pcr_set_apts(pts);
            else
            tsync_set_apts(pts);
            break;
         }
    default:
        r = -ENOIOCTLCMD;
        break;
    }

    return r;
}

static ssize_t ports_show(struct class *class, struct class_attribute *attr, char *buf)
{
    int i;
    char *pbuf = buf;
    stream_port_t *p = NULL;
    for (i = 0; i < amstream_port_num; i++) {
        p = &ports[i];
        /*name*/
        pbuf += sprintf(pbuf, "%s\t:\n", p->name);
        /*type*/
        pbuf += sprintf(pbuf, "\ttype:%d( ", p->type);
        if (p->type & PORT_TYPE_VIDEO) {
            pbuf += sprintf(pbuf, "%s ", "Video");
        }
        if (p->type & PORT_TYPE_AUDIO) {
            pbuf += sprintf(pbuf, "%s ", "Audio");
        }
        if (p->type & PORT_TYPE_MPTS) {
            pbuf += sprintf(pbuf, "%s ", "TS");
        }
        if (p->type & PORT_TYPE_MPPS) {
            pbuf += sprintf(pbuf, "%s ", "PS");
        }
        if (p->type & PORT_TYPE_ES) {
            pbuf += sprintf(pbuf, "%s ", "ES");
        }
        if (p->type & PORT_TYPE_RM) {
            pbuf += sprintf(pbuf, "%s ", "RM");
        }
        if (p->type & PORT_TYPE_SUB) {
            pbuf += sprintf(pbuf, "%s ", "Subtitle");
        }
        if (p->type & PORT_TYPE_SUB_RD) {
            pbuf += sprintf(pbuf, "%s ", "Subtitle_Read");
        }
        if (p->type & PORT_TYPE_USERDATA) {
            pbuf += sprintf(pbuf, "%s ", "userdata");
        }
        pbuf += sprintf(pbuf, ")\n");
        /*flag*/
        pbuf += sprintf(pbuf, "\tflag:%d( ", p->flag);
        if (p->flag & PORT_FLAG_IN_USE) {
            pbuf += sprintf(pbuf, "%s ", "Used");
        } else {
            pbuf += sprintf(pbuf, "%s ", "Unused");
        }
        if (p->flag & PORT_FLAG_INITED) {
            pbuf += sprintf(pbuf, "%s ", "inited");
        } else {
            pbuf += sprintf(pbuf, "%s ", "uninited");
        }
        pbuf += sprintf(pbuf, ")\n");
        /*others*/
        pbuf += sprintf(pbuf, "\tVformat:%d\n", (p->flag & PORT_FLAG_VFORMAT) ? p->vformat : -1);
        pbuf += sprintf(pbuf, "\tAformat:%d\n", (p->flag & PORT_FLAG_AFORMAT) ? p->aformat : -1);
        pbuf += sprintf(pbuf, "\tVid:%d\n", (p->flag & PORT_FLAG_VID) ? p->vid : -1);
        pbuf += sprintf(pbuf, "\tAid:%d\n", (p->flag & PORT_FLAG_AID) ? p->aid : -1);
        pbuf += sprintf(pbuf, "\tSid:%d\n", (p->flag & PORT_FLAG_SID) ? p->sid : -1);
        pbuf += sprintf(pbuf, "\tPCRid:%d\n", (p->pcr_inited == 1) ? p->pcrid : -1);
        pbuf += sprintf(pbuf, "\tachannel:%d\n", p->achanl);
        pbuf += sprintf(pbuf, "\tasamprate:%d\n", p->asamprate);
        pbuf += sprintf(pbuf, "\tadatawidth:%d\n\n", p->adatawidth);
    }
    return pbuf - buf;
}
static ssize_t bufs_show(struct class *class, struct class_attribute *attr, char *buf)
{
    int i;
    char *pbuf = buf;
    stream_buf_t *p = NULL;
    char buf_type[][12] = {"Video", "Audio", "Subtitle", "UserData", "HEVC"};

    for (i = 0; i < amstream_buf_num; i++) {
        p = &bufs[i];
        /*type*/
        pbuf += sprintf(pbuf, "%s buffer:", buf_type[p->type]);
        /*flag*/
        pbuf += sprintf(pbuf, "\tflag:%d( ", p->flag);
        if (p->flag & BUF_FLAG_ALLOC) {
            pbuf += sprintf(pbuf, "%s ", "Alloc");
        } else {
            pbuf += sprintf(pbuf, "%s ", "Unalloc");
        }
        if (p->flag & BUF_FLAG_IN_USE) {
            pbuf += sprintf(pbuf, "%s ", "Used");
        } else {
            pbuf += sprintf(pbuf, "%s ", "Noused");
        }
        if (p->flag & BUF_FLAG_PARSER) {
            pbuf += sprintf(pbuf, "%s ", "Parser");
        } else {
            pbuf += sprintf(pbuf, "%s ", "noParser");
        }
        if (p->flag & BUF_FLAG_FIRST_TSTAMP) {
            pbuf += sprintf(pbuf, "%s ", "firststamp");
        } else {
            pbuf += sprintf(pbuf, "%s ", "nofirststamp");
        }
        pbuf += sprintf(pbuf, ")\n");
        /*buf stats*/

        pbuf += sprintf(pbuf, "\tbuf addr:%#x\n", p->buf_start);

        if (p->type != BUF_TYPE_SUBTITLE) {
            pbuf += sprintf(pbuf, "\tbuf size:%#x\n", p->buf_size);
            pbuf += sprintf(pbuf, "\tbuf canusesize:%#x\n", p->canusebuf_size);
            pbuf += sprintf(pbuf, "\tbuf regbase:%#lx\n", p->reg_base);

            if (p->reg_base && p->flag & BUF_FLAG_IN_USE) {
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
                switch_mod_gate_by_name("vdec", 1);
#endif
                pbuf += sprintf(pbuf, "\tbuf level:%#x\n", stbuf_level(p));
                pbuf += sprintf(pbuf, "\tbuf space:%#x\n", stbuf_space(p));
                pbuf += sprintf(pbuf, "\tbuf read pointer:%#x\n", stbuf_rp(p));
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
                switch_mod_gate_by_name("vdec", 0);
#endif
            } else {
                pbuf += sprintf(pbuf, "\tbuf no used.\n");
            }
        } else {
            u32 sub_wp, sub_rp, data_size;
            sub_wp = stbuf_sub_wp_get();
            sub_rp = stbuf_sub_rp_get();
            if (sub_wp >= sub_rp) {
                data_size = sub_wp - sub_rp;
            } else {
                data_size = p->buf_size - sub_rp + sub_wp;
            }
            pbuf += sprintf(pbuf, "\tbuf size:%#x\n", p->buf_size);
	    pbuf += sprintf(pbuf, "\tbuf canusesize:%#x\n", p->canusebuf_size);
            pbuf += sprintf(pbuf, "\tbuf start:%#x\n", stbuf_sub_start_get());
            pbuf += sprintf(pbuf, "\tbuf write pointer:%#x\n", sub_wp);
            pbuf += sprintf(pbuf, "\tbuf read pointer:%#x\n", sub_rp);
            pbuf += sprintf(pbuf, "\tbuf level:%#x\n", data_size);
        }

        pbuf += sprintf(pbuf, "\tbuf first_stamp:%#x\n", p->first_tstamp);
        pbuf += sprintf(pbuf, "\tbuf wcnt:%#x\n\n", p->wcnt);
        pbuf += sprintf(pbuf, "\tbuf max_buffer_delay_ms:%dms\n", p->max_buffer_delay_ms);

        if (p->reg_base && p->flag & BUF_FLAG_IN_USE) {
            int calc_delayms=0;
            u32 bitrate=0,avg_bitrate=0;

            calc_delayms = calculation_stream_delayed_ms((p->type == BUF_TYPE_AUDIO) ? PTS_TYPE_AUDIO : PTS_TYPE_VIDEO,
                               &bitrate, &avg_bitrate);

            if (calc_delayms>=0) {
                pbuf += sprintf(pbuf, "\tbuf current delay:%dms\n",calc_delayms);
                pbuf += sprintf(pbuf, "\tbuf bitrate latest:%dbps,avg:%dbps\n",bitrate,avg_bitrate);
                pbuf += sprintf(pbuf, "\tbuf time after last pts:%d ms\n",
                                calculation_stream_ext_delayed_ms((p->type == BUF_TYPE_AUDIO) ? PTS_TYPE_AUDIO : PTS_TYPE_VIDEO));

                pbuf += sprintf(pbuf, "\tbuf time after last write data :%d ms\n",
                                (int)(jiffies_64 - p->last_write_jiffies64)*1000/HZ);
            }
        }
    }

    return pbuf - buf;
}

static ssize_t videobufused_show(struct class *class, struct class_attribute *attr, char *buf)
{
    char *pbuf = buf;
    stream_buf_t *p = NULL;
    p = &bufs[0];
	if (p->flag & BUF_FLAG_IN_USE) {
        pbuf += sprintf(pbuf, "%d ", 1);
    } 
	else 
	{
        pbuf += sprintf(pbuf, "%d ", 0);
    }
    return 1;
}

static ssize_t vcodec_profile_show(struct class *class, 
								   struct class_attribute *attr, 
								   char *buf)
{
	return vcodec_profile_read(buf);
}

static int reset_canuse_buferlevel(int levelx10000)
{
    int i;
    stream_buf_t *p = NULL;
    if(levelx10000>=0 && levelx10000<=10000)   
        use_bufferlevelx10000= levelx10000;
    else
       use_bufferlevelx10000=10000;    
    for (i = 0; i < amstream_buf_num; i++) {
               p = &bufs[i];
               p->canusebuf_size=((p->buf_size/1024)*use_bufferlevelx10000/10000)*1024;
               p->canusebuf_size+=1023;
               p->canusebuf_size&=~1023;
               if(p->canusebuf_size>p->buf_size)
                   p->canusebuf_size=p->buf_size;
    }
    return 0;
}
static ssize_t show_canuse_buferlevel(struct class *class, struct class_attribute *attr, char *buf)
{
        ssize_t size=sprintf(buf, "use_bufferlevel=%d/10000[=(set range[ 0~10000])=\n",use_bufferlevelx10000);
        return size;
}
static ssize_t store_canuse_buferlevel(struct class *class, struct class_attribute *attr, const char *buf, size_t size)
{
    unsigned val;
    ssize_t ret;


    ret = sscanf(buf, "%d", &val);     
    if(ret != 1 ) {
        return -EINVAL;
    }  
    val=val;
    reset_canuse_buferlevel(val);
    return size;
}
static ssize_t store_maxdelay(struct class *class, struct class_attribute *attr, const char *buf, size_t size)

{
    unsigned val;
    ssize_t ret;
	int i;

    ret = sscanf(buf, "%d", &val);     
    if(ret != 1 ) {
        return -EINVAL;
    }  
	for (i = 0; i < amstream_buf_num; i++) {
		bufs[i].max_buffer_delay_ms=val;
	}
    return size;
}
static ssize_t show_maxdelay(struct class *class, struct class_attribute *attr, char *buf)
{
    ssize_t size=0;
	size+=sprintf(buf, "%dms	//video max buffered data delay ms\n",bufs[0].max_buffer_delay_ms);
	size+=sprintf(buf, "%dms	//audio max buffered data delay ms\n",bufs[1].max_buffer_delay_ms);
    return size;
}


static struct class_attribute amstream_class_attrs[] = {
    __ATTR_RO(ports),
    __ATTR_RO(bufs),
    __ATTR_RO(vcodec_profile),
    __ATTR_RO(videobufused),
    __ATTR(canuse_buferlevel, S_IRUGO | S_IWUSR | S_IWGRP, show_canuse_buferlevel, store_canuse_buferlevel),
    __ATTR(max_buffer_delay_ms, S_IRUGO | S_IWUSR | S_IWGRP, show_maxdelay, store_maxdelay),
    __ATTR_NULL
};
static struct class amstream_class = {
        .name = "amstream",
        .class_attrs = amstream_class_attrs,
};
int request_video_firmware(const char * file_name,char *buf,int size)
{
	const struct firmware *firmware;
	int err=0;
	struct device *micro_dev;
	printk("try load %s  ...",file_name);
	micro_dev = device_create(&amstream_class,
					    NULL, MKDEV(AMSTREAM_MAJOR, 100),
					    NULL, "videodec");
	if(micro_dev ==NULL ){
		printk("device_create failed =%d\n",err);
		return -1;
	}
	if((err=request_firmware(&firmware,file_name, micro_dev))<0)
	{
		printk("can't load the %s,err=%d\n",file_name,err);
		goto error1;
	}
	if(firmware->size>size)
	{
		printk("not enough memory size for audiodsp code\n");
		err=-ENOMEM;
		goto release;
	}

	memcpy(buf,(char*)firmware->data,firmware->size);
    mb();
	printk("load mcode size=%d\n mcode name %s\n",firmware->size,file_name);
	err=firmware->size;
release:	
	release_firmware(firmware);
error1:
	device_destroy(&amstream_class, MKDEV(AMSTREAM_MAJOR, 100));
	return err;
}

static struct resource memobj;
static int  amstream_probe(struct platform_device *pdev)
{
    int i;
    int r;
    stream_port_t *st;
    struct resource *res;

    printk("Amlogic A/V streaming port init\n");

    if (HAS_HEVC_VDEC) {
        amstream_port_num = MAX_AMSTREAM_PORT_NUM;
        amstream_buf_num = BUF_MAX_NUM;
    } else {
        amstream_port_num = MAX_AMSTREAM_PORT_NUM - 1;
        amstream_buf_num = BUF_MAX_NUM - 1;
    }

    r = class_register(&amstream_class);
    if (r) {
        printk("amstream class create fail.\n");
        return r;
    }

    r = astream_dev_register();
    if (r) {
        return r;
    }

    r = register_chrdev(AMSTREAM_MAJOR, "amstream", &amstream_fops);
    if (r < 0) {
        printk("Can't allocate major for amstreaming device\n");

        goto error2;
    }

    vdec_set_decinfo(&amstream_dec_info);

    amstream_dev_class = class_create(THIS_MODULE, DEVICE_NAME);

    for (st = &ports[0], i = 0; i < amstream_port_num; i++, st++) {
        st->class_dev = device_create(amstream_dev_class, NULL,
                                      MKDEV(AMSTREAM_MAJOR, i), NULL,
                                      ports[i].name);
    }

    amstream_vdec_status = NULL;
    amstream_adec_status = NULL;
    if (tsdemux_class_register() != 0) {
        r = (-EIO);
        goto error3;
    }

#if 0
    res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
#else
    res = &memobj;
    r = find_reserve_block(pdev->dev.of_node->name,0);
    if(r < 0){
        printk("can not find %s%d reserve block\n",pdev->dev.of_node->name,1);
	 r = -EFAULT;
	 goto error3;
    }
    res->start = (phys_addr_t)get_reserve_block_addr(r);
    res->end = res->start+ (phys_addr_t)get_reserve_block_size(r)-1;
#endif
    if (!res) {
        printk("Can not obtain I/O memory, and will allocate stream buffer!\n");

        if (stbuf_change_size(&bufs[BUF_TYPE_VIDEO], DEFAULT_VIDEO_BUFFER_SIZE) != 0) {
            r = (-ENOMEM);
            goto error4;
        }
        if (stbuf_change_size(&bufs[BUF_TYPE_AUDIO], DEFAULT_AUDIO_BUFFER_SIZE) != 0) {
            r = (-ENOMEM);
            goto error5;
        }
        if (stbuf_change_size(&bufs[BUF_TYPE_SUBTITLE], DEFAULT_SUBTITLE_BUFFER_SIZE) != 0) {
            r = (-ENOMEM);
            goto error6;
        }
    } else {
        bufs[BUF_TYPE_VIDEO].buf_start = res->start;
        bufs[BUF_TYPE_VIDEO].buf_size = resource_size(res) - DEFAULT_AUDIO_BUFFER_SIZE - DEFAULT_SUBTITLE_BUFFER_SIZE;
        bufs[BUF_TYPE_VIDEO].flag |= BUF_FLAG_IOMEM;
        bufs[BUF_TYPE_VIDEO].default_buf_size = bufs[BUF_TYPE_VIDEO].buf_size;

        bufs[BUF_TYPE_AUDIO].buf_start = res->start + bufs[BUF_TYPE_VIDEO].buf_size;
        bufs[BUF_TYPE_AUDIO].buf_size = DEFAULT_AUDIO_BUFFER_SIZE;
        bufs[BUF_TYPE_AUDIO].flag |= BUF_FLAG_IOMEM;

        bufs[BUF_TYPE_SUBTITLE].buf_start = res->start + resource_size(res) - DEFAULT_SUBTITLE_BUFFER_SIZE;
        bufs[BUF_TYPE_SUBTITLE].buf_size = DEFAULT_SUBTITLE_BUFFER_SIZE;
        bufs[BUF_TYPE_SUBTITLE].flag = BUF_FLAG_IOMEM;
    }

    if (HAS_HEVC_VDEC) {
        bufs[BUF_TYPE_HEVC].buf_start = bufs[BUF_TYPE_VIDEO].buf_start;
        bufs[BUF_TYPE_HEVC].buf_size  = bufs[BUF_TYPE_VIDEO].buf_size;

        if (bufs[BUF_TYPE_VIDEO].flag & BUF_FLAG_IOMEM) {
            bufs[BUF_TYPE_HEVC].flag |= BUF_FLAG_IOMEM;
        }

        bufs[BUF_TYPE_HEVC].default_buf_size = bufs[BUF_TYPE_VIDEO].default_buf_size;
    }

    if (stbuf_fetch_init() != 0) {
        r = (-ENOMEM);
        goto error7;
    }
    init_waitqueue_head(&amstream_sub_wait);
    init_waitqueue_head(&amstream_userdata_wait);
    reset_canuse_buferlevel(10000);

    return 0;

error7:
    if (bufs[BUF_TYPE_SUBTITLE].flag & BUF_FLAG_ALLOC) {
        stbuf_change_size(&bufs[BUF_TYPE_SUBTITLE], 0);
    }
error6:
    if (bufs[BUF_TYPE_AUDIO].flag & BUF_FLAG_ALLOC) {
        stbuf_change_size(&bufs[BUF_TYPE_AUDIO], 0);
    }
error5:
    if (bufs[BUF_TYPE_VIDEO].flag & BUF_FLAG_ALLOC) {
        stbuf_change_size(&bufs[BUF_TYPE_VIDEO], 0);
    }
error4:
    tsdemux_class_unregister();
error3:
    for (st = &ports[0], i = 0; i < amstream_port_num; i++, st++) {
        device_destroy(amstream_dev_class, MKDEV(AMSTREAM_MAJOR, i));
    }
    class_destroy(amstream_dev_class);
error2:
    unregister_chrdev(AMSTREAM_MAJOR, "amstream");
    //error1:
    astream_dev_unregister();
    return (r);
}

static int  amstream_remove(struct platform_device *pdev)
{
    int i;
    stream_port_t *st;
    if (bufs[BUF_TYPE_VIDEO].flag & BUF_FLAG_ALLOC) {
        stbuf_change_size(&bufs[BUF_TYPE_VIDEO], 0);
    }
    if (bufs[BUF_TYPE_AUDIO].flag & BUF_FLAG_ALLOC) {
        stbuf_change_size(&bufs[BUF_TYPE_AUDIO], 0);
    }
    stbuf_fetch_release();
    tsdemux_class_unregister();
    for (st = &ports[0], i = 0; i < amstream_port_num; i++, st++) {
        device_destroy(amstream_dev_class, MKDEV(AMSTREAM_MAJOR, i));
    }

    class_destroy(amstream_dev_class);

    unregister_chrdev(AMSTREAM_MAJOR, DEVICE_NAME);

    astream_dev_unregister();

    amstream_vdec_status = NULL;
    amstream_adec_status = NULL;
    amstream_vdec_trickmode = NULL;

    printk("Amlogic A/V streaming port release\n");

    return 0;
}

void set_vdec_func(int (*vdec_func)(struct vdec_status *))
{
    amstream_vdec_status = vdec_func;
    return;
}

void set_adec_func(int (*adec_func)(struct adec_status *))
{
    amstream_adec_status = adec_func;
    return;
}

void set_trickmode_func(int (*trickmode_func)(unsigned long trickmode))
{
    amstream_vdec_trickmode = trickmode_func;
    return;
}

void wakeup_sub_poll(void)
{
    atomic_inc(&subdata_ready);
    wake_up_interruptible(&amstream_sub_wait);

    return;
}

int get_sub_type(void)
{
    return sub_type;
}
/*get pes buffers */

stream_buf_t* get_stream_buffer(int id)
{
	if(id>=BUF_MAX_NUM)
	{
		return 0;
	}
	return &bufs[id];
}

EXPORT_SYMBOL(set_vdec_func);
EXPORT_SYMBOL(set_adec_func);
EXPORT_SYMBOL(set_trickmode_func);
EXPORT_SYMBOL(wakeup_sub_poll);
EXPORT_SYMBOL(get_sub_type);

#ifdef CONFIG_USE_OF
static const struct of_device_id amlogic_mesonstream_dt_match[]={
	{	.compatible = "amlogic,mesonstream",
	},
	{},
};
#else
#define amlogic_mesonstream_dt_match NULL
#endif

static struct platform_driver
        amstream_driver = {
    .probe      = amstream_probe,
    .remove     = amstream_remove,
    .driver     = {
        .name   = "mesonstream",
        .of_match_table = amlogic_mesonstream_dt_match,
    }
};

static int __init amstream_module_init(void)
{
    if (platform_driver_register(&amstream_driver)) {
        printk("failed to register amstream module\n");
        return -ENODEV;
    }

    return 0;
}

static void __exit amstream_module_exit(void)
{
    platform_driver_unregister(&amstream_driver);
    return ;
}

module_init(amstream_module_init);
module_exit(amstream_module_exit);

MODULE_DESCRIPTION("AMLOGIC streaming port driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tim Yao <timyao@amlogic.com>");
