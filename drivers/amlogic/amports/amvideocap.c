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
 * Author:  Zhou Zhi <zhi.zhou@amlogic.com>
 *
 */

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/atomic.h>
#include <linux/bug.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/major.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/amlogic/amports/amvideocap.h>
#include <linux/amlogic/amports/vframe.h>
#include <linux/amlogic/ge2d/ge2d_main.h>
#include <linux/amlogic/ge2d/ge2d.h>
#include <linux/delay.h>

#include <linux/of.h>
#include <linux/of_fdt.h>

#include "amports_priv.h"
#include "amvideocap_priv.h"
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
#include <mach/mod_gate.h>
#endif


#ifdef CONFIG_PM
#include <linux/pm.h>
#endif

#define DRIVER_NAME "amvideocap"
#define MODULE_NAME "amvideocap"
#define DEVICE_NAME "amvideocap"

#define CAP_WIDTH_MAX      1920
#define CAP_HEIGHT_MAX     1080


MODULE_DESCRIPTION("Video Frame capture");
MODULE_AUTHOR("amlogic-bj");
MODULE_LICENSE("GPL");


#define AMCAP_MAX_OPEND 16
struct amvideocap_global_data {
    struct class *class;
    struct device *dev;
    struct device *micro_dev;
    int major;
    unsigned long phyaddr;
    unsigned long vaddr;
    unsigned long size;
    int opened_cnt;
    int flags;
    struct mutex lock;
    struct video_frame_info want;
    u64 wait_max_ms;
};
static struct amvideocap_global_data amvideocap_gdata;
static inline struct amvideocap_global_data *getgctrl(void) {
    return &amvideocap_gdata;
}
#define gLOCK() mutex_lock(&(getgctrl()->lock))
#define gUNLOCK() mutex_unlock(&(getgctrl()->lock))
#define gLOCKINIT() mutex_init(&(getgctrl()->lock))

/*********************************************************
 * /dev/amvideo APIs
 *********************************************************/
static int amvideocap_open(struct inode *inode, struct file *file)
{
    struct amvideocap_private *priv;
    gLOCK();
    if (!getgctrl()->phyaddr) {
        printk("Error,no memory have register for amvideocap\n");
        return -ENOMEM;
    }
    if (!getgctrl()->vaddr) {
        getgctrl()->vaddr = (unsigned long)ioremap_nocache(getgctrl()->phyaddr, getgctrl()->size);
        if (!getgctrl()->vaddr) {
            printk("%s: failed to remap y addr\n", __FUNCTION__);
            return -ENOMEM;
        }
    }
    if (getgctrl()->opened_cnt > AMCAP_MAX_OPEND) {
        gUNLOCK();
        printk("Too Many opend video cap files\n");
        return -EMFILE;
    }
    priv = kmalloc(sizeof(struct amvideocap_private), GFP_KERNEL);
    if (!priv) {
        gUNLOCK();
        printk("alloc memory failed for amvideo cap\n");
        return -ENOMEM;
    }
    memset(priv,0,sizeof(struct amvideocap_private));
    getgctrl()->opened_cnt++;
    gUNLOCK();
    file->private_data = priv;
    priv->phyaddr = getgctrl()->phyaddr;
    priv->vaddr = (u8*)getgctrl()->vaddr;
    priv->want=getgctrl()->want;
    priv->src_rect.x = -1;
    priv->src_rect.y = -1;
    priv->src_rect.width = -1;
    priv->src_rect.height = -1;
    return 0;
}

static int amvideocap_release(struct inode *inode, struct file *file)
{
    struct amvideocap_private *priv = file->private_data;
    kfree(priv);
    gLOCK();
    getgctrl()->opened_cnt--;
    gUNLOCK();
    return 0;
}

static int amvideocap_capture_get_frame(struct amvideocap_private *priv, vframe_t **vf, int *cur_index)
{
    int ret;
    ret = ext_get_cur_video_frame(vf, cur_index);
    return ret;
}
static int amvideocap_capture_put_frame(struct amvideocap_private *priv, vframe_t *vf)
{
    return ext_put_video_frame(vf);
}
static int amvideocap_get_input_format(vframe_t* vf)
{
    int format= GE2D_FORMAT_M24_NV21;    

    if ((vf->type & VIDTYPE_VIU_422) == VIDTYPE_VIU_422) {        
        format =  GE2D_FORMAT_S16_YUV422;
    } else if ((vf->type & VIDTYPE_VIU_444) == VIDTYPE_VIU_444) {        
        format = GE2D_FORMAT_S24_YUV444;
    } else if((vf->type & VIDTYPE_VIU_NV21) == VIDTYPE_VIU_NV21){        
        format= GE2D_FORMAT_M24_NV21;
    }
    return format;
}
static ssize_t  amvideocap_YUV_to_RGB(struct amvideocap_private *priv, u32 cur_index, int w, int h, vframe_t* vf, int outfmt)
{

    config_para_ex_t    ge2d_config;
    canvas_t cs0, cs1, cs2, cd;
    int canvas_idx = AMVIDEOCAP_CANVAS_INDEX;
    int y_index = cur_index & 0xff;
    int u_index = (cur_index >> 8) & 0xff;
    int v_index = (cur_index >> 16) & 0xff;
    int input_x, input_y, input_width, input_height, intfmt;
    unsigned long RGB_addr;
    ge2d_context_t *context = create_ge2d_work_queue();
    memset(&ge2d_config, 0, sizeof(config_para_ex_t));
    intfmt = amvideocap_get_input_format(vf);

    ///unsigned long RGB_phy_addr=getgctrl()->phyaddr;

    if (!priv->phyaddr) {
        printk("%s: failed to alloc y addr\n", __FUNCTION__);
        return -1;
    }
    
    RGB_addr = (unsigned long)priv->vaddr;
    if (!RGB_addr) {
        printk("%s: failed to remap y addr\n", __FUNCTION__);
        return -1;
    }    

    if(vf == NULL) {
        printk("%s: vf is NULL\n", __FUNCTION__);
        return -1;
    }


    canvas_config(canvas_idx, (unsigned long)priv->phyaddr, w * 3, h, CANVAS_ADDR_NOWRAP, CANVAS_BLKMODE_LINEAR);
    if(priv->src_rect.x < 0 || priv->src_rect.x > vf->width) {
        input_x = 0;
    } else {
        input_x = priv->src_rect.x;
    }

    if(priv->src_rect.y < 0 || priv->src_rect.y > vf->height) {
        input_y = 0;
    } else {
        input_y = priv->src_rect.y;
    }

    if(priv->src_rect.width < 0) {
        input_width = vf->width;
    } else if ((priv->src_rect.x + priv->src_rect.width) > vf->width) {
        input_width = priv->src_rect.x + priv->src_rect.width - vf->width;
    } else {
        input_width = priv->src_rect.width;
    }

    if(priv->src_rect.height < 0) {
        input_height = vf->height;
    } else if ((priv->src_rect.y + priv->src_rect.height) > vf->height) {
        input_height = priv->src_rect.y + priv->src_rect.height - vf->height;
    } else {
        input_height = priv->src_rect.height;
    }

    if(intfmt == GE2D_FORMAT_S16_YUV422) {
        input_height = input_height / 2;
    }

    ge2d_config.alu_const_color = 0;
    ge2d_config.bitmask_en  = 0;
    ge2d_config.src1_gb_alpha = 0;
    ge2d_config.dst_xy_swap = 0;

    canvas_read(y_index, &cs0);
    canvas_read(u_index, &cs1);
    canvas_read(v_index, &cs2);

    ge2d_config.src_planes[0].addr = cs0.addr;
    ge2d_config.src_planes[0].w = cs0.width;
    ge2d_config.src_planes[0].h = cs0.height;
    ge2d_config.src_planes[1].addr = cs1.addr;
    ge2d_config.src_planes[1].w = cs1.width;
    ge2d_config.src_planes[1].h = cs1.height;
    ge2d_config.src_planes[2].addr = cs2.addr;
    ge2d_config.src_planes[2].w = cs2.width;
    ge2d_config.src_planes[2].h = cs2.height;    

    ge2d_config.src_key.key_enable = 0;
    ge2d_config.src_key.key_mask = 0;
    ge2d_config.src_key.key_mode = 0;
    ge2d_config.src_key.key_color = 0;

    ge2d_config.src_para.canvas_index = cur_index;
    ge2d_config.src_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config.src_para.format = intfmt;
    ge2d_config.src_para.fill_color_en = 0;
    ge2d_config.src_para.fill_mode = 0;
    ge2d_config.src_para.x_rev = 0;
    ge2d_config.src_para.y_rev = 0;
    ge2d_config.src_para.color = 0;
    ge2d_config.src_para.top = input_y;
    ge2d_config.src_para.left = input_x;
    ge2d_config.src_para.width = input_width;
    ge2d_config.src_para.height = input_height;


    canvas_read(canvas_idx, &cd);    
    ge2d_config.dst_planes[0].addr = cd.addr;
    ge2d_config.dst_planes[0].w = cd.width;
    ge2d_config.dst_planes[0].h = cd.height;


    ge2d_config.dst_para.canvas_index = canvas_idx;
    ge2d_config.dst_para.mem_type = CANVAS_TYPE_INVALID;
    ge2d_config.dst_para.format =  outfmt;
    ge2d_config.dst_para.fill_color_en = 0;
    ge2d_config.dst_para.fill_mode = 0;
    ge2d_config.dst_para.x_rev = 0;
    ge2d_config.dst_para.y_rev = 0;
    ge2d_config.dst_xy_swap = 0;
    ge2d_config.dst_para.color = 0;
    ge2d_config.dst_para.top = 0;
    ge2d_config.dst_para.left = 0;
    ge2d_config.dst_para.width = w;
    ge2d_config.dst_para.height = h;

    if (ge2d_context_config_ex(context, &ge2d_config) < 0) {
        printk("++ge2d configing error.\n");
        return -1;
    }

    stretchblt_noalpha(context,
            0, 
            0, 
            ge2d_config.src_para.width,
            ge2d_config.src_para.height,
            0,
            0,
            ge2d_config.dst_para.width,
            ge2d_config.dst_para.height);
    if (context) {
        destroy_ge2d_work_queue(context);
        context = NULL;
    }
    return 0;
    //vfs_write(video_rgb_filp,RGB_addr,size, &video_yuv_pos);
}

static int amvideocap_capture_one_frame_l(struct amvideocap_private *priv, int curindex, int w, int h, vframe_t *vf, int outge2dfmt)
{
    int ret;
    switch_mod_gate_by_name("ge2d", 1);
    ret = amvideocap_YUV_to_RGB(priv, curindex, w, h, vf, outge2dfmt);
    switch_mod_gate_by_name("ge2d", 0);
    return ret;
}
static int amvideocap_format_to_byte4pix(int fmt)
{
    switch(fmt){
        case GE2D_FORMAT_S24_RGB:return 3;
        case GE2D_FORMAT_S32_RGBA:return 4;
        default:
                                  return 4;
    }
};


static int amvideocap_capture_one_frame(struct amvideocap_private *priv,vframe_t *vfput, int index)
{
    int w, h, ge2dfmt;
    int curindex;
    vframe_t *vf = vfput;
    int ret = 0;
    
    if (!vf) {
        ret = amvideocap_capture_get_frame(priv, &vf, &curindex);
    }else{
        curindex=index;
    }
    if (ret < 0 || !vf) {
        return -EAGAIN;
    }    

#define CHECK_AND_SETVAL(val,want,def) (val)=(want)>0?(want):(def)
    CHECK_AND_SETVAL(ge2dfmt,priv->want.fmt,vf->type);
    CHECK_AND_SETVAL(w,priv->want.width,vf->width);
    CHECK_AND_SETVAL(h,priv->want.height,vf->height);
#undef 	CHECK_AND_SETVAL

    w = (w < CAP_WIDTH_MAX) ? w :  CAP_WIDTH_MAX;
    h = (h < CAP_HEIGHT_MAX) ? h : CAP_HEIGHT_MAX;

    ret = amvideocap_capture_one_frame_l(priv, curindex, w, h, vf, priv->want.fmt);/*alway rgb24 now*/
    amvideocap_capture_put_frame(priv, vf);

    if (!ret) {
        priv->state = AMVIDEOCAP_STATE_FINISHED_CAPTURE;
        priv->src.width=vf->width;
        priv->src.height=vf->height;
        priv->out.timestamp_ms= vf->pts * 1000 / 90;
        priv->out.width=w;
        priv->out.height=h;
        priv->out.fmt=priv->want.fmt;
        priv->out.width_aligned=priv->out.width;
        priv->out.byte_per_pix=amvideocap_format_to_byte4pix(priv->out.fmt);//RGBn
    }else{
        priv->state = AMVIDEOCAP_STATE_ERROR;
    }
    return ret;
}
static int amvideocap_capture_one_frame_callback(unsigned long data, vframe_t *vfput, int index)
{
    struct amvideocap_req_data *reqdata = (struct amvideocap_req_data *)data;
    amvideocap_capture_one_frame(reqdata->privdata, vfput, index);
    return 0;
}

static int amvideocap_capture_one_frame_wait(struct amvideocap_private *priv, int waitms)
{
    unsigned long timeout = jiffies + waitms * HZ / 1000;
    int ret = 0;
    struct amvideocap_req_data reqdata;
    struct amvideocap_req req;
    priv->sended_end_frame_cap_req= 0;
    priv->state = AMVIDEOCAP_STATE_ON_CAPTURE;
    do {
        if (ret == -EAGAIN) {
            msleep(100);
        }
        if (priv->want.at_flags==CAP_FLAG_AT_END ) {
            if (!priv->sended_end_frame_cap_req) {
                reqdata.privdata = priv;
                req.callback = amvideocap_capture_one_frame_callback;
                req.data = (unsigned long)&reqdata;
                req.at_flags = priv->want.at_flags;
                req.timestamp_ms = priv->want.timestamp_ms;
                priv->sended_end_frame_cap_req = !ext_register_end_frame_callback(&req);
                ret =-EAGAIN;
            } else {
                if (priv->state == AMVIDEOCAP_STATE_FINISHED_CAPTURE) {
                    ret = 0;
                } else if(priv->state == AMVIDEOCAP_STATE_ON_CAPTURE){
                    ret = -EAGAIN;
                }
            }
        } else {
            ret = amvideocap_capture_one_frame(priv, NULL, 0);            
        }
    } while (ret == -EAGAIN && time_before(jiffies, timeout));
    ext_register_end_frame_callback(NULL);/*del req*/
    return ret;
}



static long amvideocap_ioctl(struct file *file, unsigned int cmd, ulong arg)
{
    int ret = 0;
    struct amvideocap_private *priv = file->private_data;
    switch (cmd) {
        case AMVIDEOCAP_IOW_SET_WANTFRAME_FORMAT:
            {
                priv->want.fmt=arg;
                break;
            }
        case AMVIDEOCAP_IOW_SET_WANTFRAME_WIDTH:
            {
                priv->want.width=arg;
                break;
            }
        case AMVIDEOCAP_IOW_SET_WANTFRAME_HEIGHT:
            {
                priv->want.height=arg;
                break;	
            }
        case AMVIDEOCAP_IOW_SET_WANTFRAME_TIMESTAMP_MS:
            {
                priv->want.timestamp_ms=arg;
                break;	
            }
        case AMVIDEOCAP_IOW_SET_WANTFRAME_AT_FLAGS:
            {
                priv->want.at_flags=arg;
                break;	
            }
        case AMVIDEOCAP_IOR_GET_FRAME_FORMAT:
            {
               if (copy_to_user((void*)arg,(void*)&priv->out.fmt,sizeof(priv->out.fmt)))
               {
                   ret = -EFAULT;
                   break;
               }
               break;
            }
        case AMVIDEOCAP_IOR_GET_FRAME_WIDTH:
            {
                if(copy_to_user((void*)arg,(void*)&priv->out.width,sizeof(priv->out.width)))
                {
			ret = -EFAULT;
			break;
                }
                break;	
            }
        case AMVIDEOCAP_IOR_GET_FRAME_HEIGHT:
            {
                if(copy_to_user((void*)arg,(void*)&priv->out.height,sizeof(priv->out.height)))
                {
			ret = -EFAULT;
			break;
                }
                break;
            }
        case AMVIDEOCAP_IOR_GET_FRAME_TIMESTAMP_MS:
            {
                if(copy_to_user((void*)arg,(void*)&priv->out.timestamp_ms,sizeof(priv->out.timestamp_ms)))
                {
			ret = -EFAULT;
			break;
                }
                break;	
            }
        case AMVIDEOCAP_IOR_GET_SRCFRAME_FORMAT:
            {
                if(copy_to_user((void*)arg,(void*)&priv->src.fmt,sizeof(priv->src.fmt)))
                {
			ret = -EFAULT;
			break;
                }
                break;	
            }
        case AMVIDEOCAP_IOR_GET_SRCFRAME_WIDTH:
            {
                if(copy_to_user((void*)arg,(void*)&priv->src.width,sizeof(priv->src.width)))
                {
			ret = -EFAULT;
			break;
                }
                break;	
            }
        case AMVIDEOCAP_IOR_GET_SRCFRAME_HEIGHT:
            {
                if(copy_to_user((void*)arg,(void*)&priv->src.height,sizeof(priv->src.height)))
                {
			ret = -EFAULT;
			break;
                }
                break;	
            }
        case AMVIDEOCAP_IOR_GET_STATE:
            {
                if(copy_to_user((void*)arg,(void*)&priv->state,sizeof(priv->state)))
                {
			ret = -EFAULT;
			break;
                }
                break;	
            }
        case AMVIDEOCAP_IOW_SET_WANTFRAME_WAIT_MAX_MS:
            {
                priv->wait_max_ms=arg;
                break;	
            }
        case AMVIDEOCAP_IOW_SET_START_CAPTURE:
            {
                ret=amvideocap_capture_one_frame_wait(priv,arg);
                break;
            }
        case AMVIDEOCAP_IOW_SET_CANCEL_CAPTURE:
            {
                if(priv->sended_end_frame_cap_req){
                    ext_register_end_frame_callback(NULL);/*del req*/
                    priv->sended_end_frame_cap_req=0;
                    priv->state = AMVIDEOCAP_STATE_INIT;
                }
                break;
            }
        case AMVIDEOCAP_IOR_SET_SRC_X:
            {
                priv->src_rect.x = arg;
                break;
            }
        case AMVIDEOCAP_IOR_SET_SRC_Y:
            {
                priv->src_rect.y = arg;
                break;
            }
        case AMVIDEOCAP_IOR_SET_SRC_WIDTH:
            {
                priv->src_rect.width = arg;
                break;
            }
        case AMVIDEOCAP_IOR_SET_SRC_HEIGHT:
            {
                priv->src_rect.height = arg;
                break;
            }
        default:
            printk("unkonw cmd=%x\n", cmd);
            ret = -1;
    }
    return ret;
}



static int amvideocap_mmap(struct file *file, struct vm_area_struct *vma)
{
    struct amvideocap_private *priv = file->private_data;
    unsigned long off = vma->vm_pgoff << PAGE_SHIFT;
    unsigned vm_size = vma->vm_end - vma->vm_start;
    if(!priv->phyaddr)
        return -EIO;

    if (vm_size == 0) {
        return -EAGAIN;
    }

    off += priv->phyaddr;

    vma->vm_flags |= VM_RESERVED | VM_IO;
    if (remap_pfn_range(vma, vma->vm_start, off >> PAGE_SHIFT,
                vma->vm_end - vma->vm_start, vma->vm_page_prot)) {
        printk("set_cached: failed remap_pfn_range\n");
        return -EAGAIN;
    }
    printk("amvideocap_mmap ok\n");
    return 0;
}
static ssize_t amvideocap_read(struct file *file, char __user *buf, size_t count, loff_t * ppos)
{
    struct amvideocap_private *priv = file->private_data;
    int waitdelay;
    int ret=0;
    int copied;
    loff_t pos;
    pos = *ppos;
    if(priv->wait_max_ms>0){
        waitdelay=priv->wait_max_ms;
    }else{
        if(priv->want.at_flags == CAP_FLAG_AT_END ) /*need end*/
            waitdelay=file->f_flags & O_NONBLOCK ? HZ  : HZ * 100;
        else
            waitdelay=file->f_flags & O_NONBLOCK ? HZ/100  : HZ * 10;
    }
    if(!pos){/*trigger a new capture,*/
        ret = amvideocap_capture_one_frame_wait(priv,waitdelay);

        if ((ret == 0) && (priv->state==AMVIDEOCAP_STATE_FINISHED_CAPTURE) && (priv->vaddr != NULL)) {
            int size = min((int)count, (priv->out.byte_per_pix * priv->out.width_aligned* priv->out.height));

            copied=copy_to_user(buf, priv->vaddr, size);
            if(copied){
                printk("amvideocap_read %d copy_to_user failed \n",size);
            }
            ret = size;
        }
    }else{
        /*read from old capture.*/
        if(priv->state !=AMVIDEOCAP_STATE_FINISHED_CAPTURE || priv->vaddr == NULL){
            ret=0;/*end.*/
        }else{
            int maxsize = priv->out.byte_per_pix * priv->out.width_aligned* priv->out.height;
            if(pos<maxsize){
                int rsize=min((int)count,(maxsize-(int)pos));

                copied=copy_to_user(buf, priv->vaddr+pos, rsize);
                if(copied){
                    printk("amvideocap_read11 %d copy_to_user failed \n",rsize);
                }
                ret = rsize;
            }else{
                ret=0;/*end.*/
            }
        }
    }
    if(ret>0){
        pos+=ret;
        *ppos=pos;
    }
    return ret;
}

const static struct file_operations amvideocap_fops = {
    .owner    = THIS_MODULE,
    .open     = amvideocap_open,
    .read     = amvideocap_read,
    .mmap     = amvideocap_mmap,
    .release  = amvideocap_release,
    .unlocked_ioctl    = amvideocap_ioctl,
    ///  .poll     = amvideocap_poll,
};

static ssize_t show_amvideocap_config(struct class *class, struct class_attribute *attr, char *buf)
{
    char *pbuf = buf;
    pbuf += sprintf(pbuf, "at_flags:%d\n",getgctrl()->want.at_flags);
    pbuf += sprintf(pbuf, "timestampms:%lld\n",getgctrl()->want.timestamp_ms);
    pbuf += sprintf(pbuf, "width:%d\n",getgctrl()->want.width);
    pbuf += sprintf(pbuf, "height:%d\n",getgctrl()->want.height);
    pbuf += sprintf(pbuf, "format:%d\n",getgctrl()->want.fmt);
    pbuf += sprintf(pbuf, "waitmaxms:%lld\n",getgctrl()->wait_max_ms);
    return (pbuf - buf);
}

static ssize_t store_amvideocap_config(struct class *class, struct class_attribute *attr, const char *buf, size_t size)

{
    int ret,val;
    const char *pbuf=buf;
    for(;pbuf&&pbuf[0]!='\0';){
#ifdef GETVAL
#undef GETVAL
#endif
#define GETVAL(tag,v)\
        val=0;\
        ret=sscanf(pbuf,tag ":%d", &val); \
        if(ret==1) {v=val;pbuf += strlen(tag);goto tonext;};     
        GETVAL("timestamp",getgctrl()->want.timestamp_ms);
        GETVAL("width",getgctrl()->want.width);
        GETVAL("height",getgctrl()->want.height);	
        GETVAL("format",getgctrl()->want.fmt);
        GETVAL("waitmaxms",getgctrl()->wait_max_ms);
        GETVAL("at_flags",getgctrl()->want.at_flags);
#undef GETVAL
        pbuf++;
tonext:
        while(pbuf[0]!=';' && pbuf[0]!='\0')pbuf++;
        if(pbuf[0]==';')
            pbuf++;
    }
    return size;
}

static struct class_attribute amvideocap_class_attrs[] = {
    __ATTR(config, S_IRUGO | S_IWUSR | S_IWGRP, show_amvideocap_config, store_amvideocap_config),
    __ATTR_NULL
};
static struct class amvideocap_class = {
    .name = MODULE_NAME,
    .class_attrs = amvideocap_class_attrs,
};
s32 amvideocap_register_memory(unsigned char *phybufaddr, int phybufsize)
{
    printk("amvideocap_register_memory %p %d\n", phybufaddr, phybufsize);
    getgctrl()->phyaddr = (unsigned long)phybufaddr;
    getgctrl()->size = (unsigned long)phybufsize;
    getgctrl()->vaddr = 0;
    return 0;
}
s32 amvideocap_dev_register(unsigned char *phybufaddr, int phybufsize)
{
    s32 r = 0;
    printk("amvideocap_dev_register buf:%p,size:%x.\n", phybufaddr, phybufsize);

    gLOCKINIT();
    r = register_chrdev(0, DEVICE_NAME, &amvideocap_fops);
    if (r < 0) {
        printk("Can't register major for amvideocap device\n");
        return r;
    }
    getgctrl()->major = r;
    r = class_register(&amvideocap_class);
    if (r) {
        printk("amvideocap class create fail.\n");
        goto err1;
    }
    getgctrl()->class = &amvideocap_class;
    getgctrl()->dev = device_create(getgctrl()->class,
            NULL, MKDEV(getgctrl()->major, 0),
            NULL, DEVICE_NAME "0");
    if (getgctrl()->dev == NULL) {
        printk("amvideocap device_create fail.\n");
        r = -EEXIST;
        goto err2;
    }
    if (phybufaddr != NULL) {
        getgctrl()->phyaddr = (unsigned long)phybufaddr;
        getgctrl()->size = (unsigned long)phybufsize;
    }
    getgctrl()->wait_max_ms=0;
    getgctrl()->want.fmt=GE2D_FORMAT_S24_RGB;
    getgctrl()->want.width=0;
    getgctrl()->want.height=0;
    getgctrl()->want.timestamp_ms=0;
    getgctrl()->want.at_flags=CAP_FLAG_AT_CURRENT;/*get last frame*/
    return 0;
err2:
    class_unregister(&amvideocap_class);
err1:
    unregister_chrdev(getgctrl()->major, DEVICE_NAME);
    return r;
}

s32 amvideocap_dev_unregister(void)
{
    device_destroy(getgctrl()->class, MKDEV(getgctrl()->major, 0));
    class_unregister(getgctrl()->class);
    unregister_chrdev(getgctrl()->major, DEVICE_NAME);

    return 0;
}


/*******************************************************************
 *
 * interface for Linux driver
 *
 * ******************************************************************/

static struct resource memobj;
/* for driver. */
static int amvideocap_probe(struct platform_device *pdev)
{
    unsigned int buf_size;
    struct resource *mem;
    int idx;

    mem = &memobj;
    printk("amvideocap_probe,%s\n", pdev->dev.of_node->name);

    idx = find_reserve_block(pdev->dev.of_node->name,0);
    if(idx < 0){
	    printk("amvideocap memory resource undefined.\n");
        return -EFAULT;
    }
    mem->start = (phys_addr_t)get_reserve_block_addr(idx);
    mem->end = mem->start+ (phys_addr_t)get_reserve_block_size(idx)-1;
    buf_size = mem->end - mem->start + 1;
    amvideocap_dev_register((unsigned char *)mem->start,buf_size);
    return 0;
}

static int amvideocap_remove(struct platform_device *plat_dev)
{
    //struct rtc_device *rtc = platform_get_drvdata(plat_dev);
    //rtc_device_unregister(rtc);
    //device_remove_file(&plat_dev->dev, &dev_attr_irq);
    amvideocap_dev_unregister();
    return 0;
}

#ifdef CONFIG_USE_OF
static const struct of_device_id amlogic_amvideocap_dt_match[]={
	{	.compatible = "amlogic,amvideocap",
	},
	{},
};
#else
#define amlogic_amvideocap_dt_match NULL
#endif

/* general interface for a linux driver .*/
struct platform_driver amvideocap_drv = {
    .probe  = amvideocap_probe,
    .remove = amvideocap_remove,
    .driver = {
        .name = "amvideocap",
        .of_match_table = amlogic_amvideocap_dt_match,
    }
};

static int __init
amvideocap_init_module(void)
{
    int err;

    printk("amvideocap_init_module\n");
    if ((err = platform_driver_register(&amvideocap_drv))) {
        return err;
    }

    return err;

}

static void __exit
amvideocap_remove_module(void)
{
    platform_driver_unregister(&amvideocap_drv);
    printk("amvideocap module removed.\n");
}

module_init(amvideocap_init_module);
module_exit(amvideocap_remove_module);

MODULE_DESCRIPTION("AMLOGIC  amvideocap driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Wang Jian <jian.wang@amlogic.com>");


