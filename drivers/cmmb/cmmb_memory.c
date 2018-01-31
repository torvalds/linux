/* SPDX-License-Identifier: GPL-2.0 */
#include "cmmb_memory.h"
#include "cmmb_class.h"
#include <linux/errno.h>
#include <linux/workqueue.h>
#include <asm/atomic.h>
#include <linux/vmalloc.h> 
#include <linux/slab.h>

#if 1
#define DBGERR(x...)	printk(KERN_INFO x)
#else
#define DBGERR(x...)
#endif

#if 0
#define DBG(x...)	printk(KERN_INFO x)
#else
#define DBG(x...)
#endif

struct cmmb_memory CMMB_memo;
static struct cmmb_device* cmmbmemo;


static int cmmbmemo_release(struct inode *inode, struct file *file)
{
    struct cmmb_memory *cmmb_memo = (struct cmmb_memory*)file->private_data;
    
    DBG("[CMMB HW]:[memory]: enter cmmb av memory release\n");
    
    mutex_lock(&cmmb_memo->mutex);
    
	cmmb_memo->usr--;
    
	if(cmmb_memo->usr == 0){
        vfree(cmmb_memo->video_buf);
        vfree(cmmb_memo->audio_buf);
        vfree(cmmb_memo->data_buf);
		mutex_unlock(&cmmb_memo->mutex);
        DBG("[CMMB HW]:[memory]: enter cmmb av memory release free buffer\n");
	} else{
		mutex_unlock(&cmmb_memo->mutex);
	}    
    return 0;
}

//hzb@20100416,在打开设备的时候申请空间
static int cmmbmemo_open(struct inode * inode, struct file * file)
{
    struct cmmb_memory *cmmbmemo = &CMMB_memo;
    int ret = 0;
    
    DBG("[CMMB HW]:[memory]: enter cmmb memo open\n");

    if (mutex_lock_interruptible(&cmmbmemo->mutex))
        return -ERESTARTSYS;
    
    cmmbmemo->usr++;
    
    if (cmmbmemo->usr == 1)
    {
        DBG("[CMMB HW]:[memory]:cmmb video buffer malloc\n");
        
        cmmbmemo->video_buf = NULL;
        cmmbmemo->audio_buf = NULL;
        cmmbmemo->data_buf  = NULL;

        //cmmbmemo->video_buf = vmalloc(CMMB_VIDEO_BUFFER_SIZE+1, GFP_KERNEL);
	cmmbmemo->video_buf   = vmalloc(CMMB_VIDEO_BUFFER_SIZE+1);

        if (cmmbmemo->video_buf == NULL){
            ret = - ENOMEM;
            DBGERR("[CMMB HW]:[memory]:[err]: cmmb video buffer malloc fail!!!\n");
            goto kmalloc_fail;
        }

        //cmmbmemo->audio_buf = vmalloc(CMMB_AUDIO_BUFFER_SIZE+1, GFP_KERNEL);
	cmmbmemo->audio_buf = vmalloc(CMMB_AUDIO_BUFFER_SIZE+1);
	
	
        if (cmmbmemo->audio_buf == NULL){
            ret = - ENOMEM;
            DBGERR("[CMMB HW]:[memory]:[err]: cmmb audio buffer malloc fail!!!\n");
            goto kmalloc_fail;
        }

        cmmbmemo->data_buf = vmalloc(1);

        if (cmmbmemo->data_buf == NULL){
            ret = - ENOMEM;
            DBGERR("[CMMB HW]:[memory]:[err]: cmmb data buffer malloc fail!!!\n");
            goto kmalloc_fail;
        }

        //hzb@20100415,init av ring buffers,cmmb need three ring buffers to store the demuxed data
        cmmb_ringbuffer_init(&cmmbmemo->buffer_Video, cmmbmemo->video_buf, CMMB_VIDEO_BUFFER_SIZE);  //init video ring buffer
        cmmb_ringbuffer_init(&cmmbmemo->buffer_Audio, cmmbmemo->audio_buf, CMMB_AUDIO_BUFFER_SIZE);  //init audio ring buffer
        cmmb_ringbuffer_init(&cmmbmemo->buffer_Data,  cmmbmemo->data_buf,  1);   //init data ring buffer

        cmmbmemo->w_datatype = CMMB_NULL_TYPE;
        cmmbmemo->r_datatype = CMMB_NULL_TYPE;
    }
    file->private_data = cmmbmemo;  //hzb@20100415,store the cmmbmemo struct in the file private data 
    mutex_unlock(&cmmbmemo->mutex);    
    return ret;
        
kmalloc_fail:
    vfree(cmmbmemo->video_buf);
    vfree(cmmbmemo->audio_buf);
    vfree(cmmbmemo->data_buf);
    mutex_unlock(&cmmbmemo->mutex);    
    return ret;        
}


static ssize_t cmmbmemo_read(struct file *file, char __user *buf, size_t count,loff_t *ppos)
{
    struct cmmb_memory *cmmbmemo = (struct cmmb_memory*)file->private_data;
    ssize_t avail_V, avail_A, avail_D;
    ssize_t ret;
    
    DBG("[CMMB HW]:[memory]:enter cmmb memory read\n");
    
    if (cmmbmemo->r_datatype == CMMB_VIDEO_TYPE){
#if 0         
        DECLARE_WAITQUEUE(wait, current);
        for(;;){
            avail_V = cmmb_ringbuffer_avail(&cmmbmemo->buffer_Video);
             
            if (avail_V < count){          
                add_wait_queue(&cmmbmemo->buffer_Video.queue, &wait);
                __set_current_state(TASK_INTERRUPTIBLE);
                schedule();
                remove_wait_queue(&cmmbmemo->buffer_Video.queue, &wait);
                if (signal_pending(current)){
                   ret = -ERESTARTSYS;
                   goto out2;
                }
            }
        }
#else
#if 0
        avail_V = cmmb_ringbuffer_avail(&cmmbmemo->buffer_Video);
        while (avail_V < count){
            DBG("[CMMB HW]:[memory]:cmmb memory read video data sleep!!\n");
            spin_lock(cmmbmemo->buffer_Video.lock);
            cmmbmemo->buffer_Video.condition = 0;
            spin_unlock(cmmbmemo->buffer_Video.lock);
            if (wait_event_interruptible(cmmbmemo->buffer_Video.queue, cmmbmemo->buffer_Video.condition))
                return -ERESTARTSYS;
            
            avail_V = cmmb_ringbuffer_avail(&cmmbmemo->buffer_Video);
            DBG("[CMMB HW]:[memory]:cmmb memory read video data awake\n");
        }
#endif 
	    avail_V = cmmb_ringbuffer_avail(&cmmbmemo->buffer_Video);
	    if (avail_V < count)  
	    	return 0;     
#endif          
        ret = cmmb_ringbuffer_read(&cmmbmemo->buffer_Video, buf, count, 1);   
     
        DBG("[CMMB HW]:[memory]:cmmb memory video read ret = 0x%x\n",ret);
    }else if (cmmbmemo->r_datatype == CMMB_AUDIO_TYPE){
#if 0
        DECLARE_WAITQUEUE(wait, current);
        for(;;){
            avail_A = cmmb_ringbuffer_avail(&cmmbmemo->buffer_Audio);
            if (avail_A < count){
                add_wait_queue(&cmmbmemo->buffer_Audio.queue, &wait);
                __set_current_state(TASK_INTERRUPTIBLE);
                schedule();
                remove_wait_queue(&cmmbmemo->buffer_Audio.queue, &wait);
                if (signal_pending(current)){
                    ret = -ERESTARTSYS;
                    goto out2;
                }
            }
        }
#else
#if 0
        avail_A = cmmb_ringbuffer_avail(&cmmbmemo->buffer_Audio);
        while (avail_A < count){
            DBG("[CMMB HW]:[memory]:cmmb memory read audio data sleep!!\n");
            spin_lock(cmmbmemo->buffer_Audio.lock);
            cmmbmemo->buffer_Audio.condition = 0;
            spin_unlock(cmmbmemo->buffer_Audio.lock);
            if (wait_event_interruptible(cmmbmemo->buffer_Audio.queue, cmmbmemo->buffer_Audio.condition))
                return -ERESTARTSYS;
            
            avail_A = cmmb_ringbuffer_avail(&cmmbmemo->buffer_Audio);
            DBG("[CMMB HW]:[memory]:cmmb memory read audio data awake\n");
        }
#endif
		avail_A = cmmb_ringbuffer_avail(&cmmbmemo->buffer_Audio);  
		if (avail_A < count)  
			return 0;    
#endif
        ret = cmmb_ringbuffer_read(&cmmbmemo->buffer_Audio, buf, count, 1);
    }else if(cmmbmemo->r_datatype == CMMB_DATA_TYPE){
 #if 0   
        DECLARE_WAITQUEUE(wait, current);
        for(;;){
           avail_D = cmmb_ringbuffer_avail(&cmmbmemo->buffer_Data);
           if (avail_D < count){
               add_wait_queue(&cmmbmemo->buffer_Data.queue, &wait);
               __set_current_state(TASK_INTERRUPTIBLE);
               schedule();
               remove_wait_queue(&cmmbmemo->buffer_Data.queue, &wait);
               if (signal_pending(current)){
                   ret = -ERESTARTSYS;
                   goto out2;
               }
           }
        }
#else
#if 0
        avail_D = cmmb_ringbuffer_avail(&cmmbmemo->buffer_Data);
        while (avail_D < count){
        DBG("[CMMB HW]:[memory]:cmmb memory read data sleep!!\n");
        spin_lock(cmmbmemo->buffer_Data.lock);
        cmmbmemo->buffer_Data.condition = 0;
        spin_unlock(cmmbmemo->buffer_Data.lock);
        if (wait_event_interruptible(cmmbmemo->buffer_Data.queue, cmmbmemo->buffer_Data.condition))
            return -ERESTARTSYS;
        
        avail_D= cmmb_ringbuffer_avail(&cmmbmemo->buffer_Data);
        DBG("[CMMB HW]:[memory]:cmmb memory read data awake\n");
        }
#endif
		avail_D = cmmb_ringbuffer_avail(&cmmbmemo->buffer_Data);  
		if (avail_D < count)  
			return 0;    	        
#endif
        ret = cmmb_ringbuffer_read(&cmmbmemo->buffer_Data, buf, count, 1);
    }
    
out2:
    cmmbmemo->r_datatype = CMMB_NULL_TYPE;
    return ret;;
}



static ssize_t cmmbmemo_write(struct file *file, char __user *buf, size_t count,loff_t *ppos)
{
    struct cmmb_memory *cmmbmemo = (struct cmmb_memory*)file->private_data;
    ssize_t free_V, free_A, free_D;
    ssize_t ret;
    static int loop = 0;
    
    DBG("[CMMB HW]:[memory]:enter cmmbdemux_write\n");
    
    if (cmmbmemo->w_datatype == CMMB_VIDEO_TYPE){
        
        free_V = cmmb_ringbuffer_free(&cmmbmemo->buffer_Video);
        if (free_V >= count){
           ret = cmmb_ringbuffer_write(&cmmbmemo->buffer_Video, buf, count);
        }
        //cmmbmemo->w_datatype = CMMB_NULL_TYPE;
#if 0
        spin_lock(cmmbmemo->buffer_Video.lock);
        cmmbmemo->buffer_Video.condition = 1;
        spin_unlock(cmmbmemo->buffer_Video.lock);
        wake_up_interruptible(&cmmbmemo->buffer_Video.queue);
#endif
    }else if (cmmbmemo->w_datatype == CMMB_AUDIO_TYPE){
        free_A = cmmb_ringbuffer_free(&cmmbmemo->buffer_Audio);
        if (free_A >= count){
           ret = cmmb_ringbuffer_write(&cmmbmemo->buffer_Audio, buf, count);
        }
        //cmmbmemo->w_datatype = CMMB_NULL_TYPE;
#if 0
        spin_lock(cmmbmemo->buffer_Audio.lock);
        cmmbmemo->buffer_Audio.condition = 1;
        spin_unlock(cmmbmemo->buffer_Audio.lock);
#endif
        //wake_up_interruptible(&cmmbmemo->buffer_Audio.queue);
    }else if(cmmbmemo->w_datatype == CMMB_DATA_TYPE){
        free_D = cmmb_ringbuffer_free(&cmmbmemo->buffer_Data);
        if (free_D >= count){
           ret = cmmb_ringbuffer_write(&cmmbmemo->buffer_Data, buf, count);
        }
        //cmmbmemo->w_datatype = CMMB_NULL_TYPE;
#if 0
        spin_lock(cmmbmemo->buffer_Data.lock);
        cmmbmemo->buffer_Data.condition = 1;
        spin_unlock(cmmbmemo->buffer_Data.lock);
#endif
        //wake_up_interruptible(&cmmbmemo->buffer_Data.queue);
    }

    return ret;
}


int cmmbmemo_valueinit(struct file *file)
{
    struct cmmb_memory *cmmbmemo = file->private_data;
    int ret = 0;

    DBG("[CMMB HW]:[memory]: enter cmmb memo open\n");

    cmmbmemo->video_buf = NULL;
    cmmbmemo->audio_buf = NULL;
    cmmbmemo->data_buf  = NULL;

    cmmbmemo->video_buf = kzalloc(CMMB_VIDEO_BUFFER_SIZE+1, GFP_KERNEL);

    if (cmmbmemo->video_buf == NULL){
        ret = - ENOMEM;
        DBGERR("[CMMB HW]:[memory]:[err]: cmmb video buffer malloc fail!!!\n");
        goto kmalloc_fail;
    }

    cmmbmemo->audio_buf = kzalloc(CMMB_AUDIO_BUFFER_SIZE+1, GFP_KERNEL);

    if (cmmbmemo->audio_buf == NULL){
        ret = - ENOMEM;
        DBGERR("[CMMB HW]:[memory]:[err]: cmmb audio buffer malloc fail!!!\n");
        goto kmalloc_fail;
    }

    cmmbmemo->data_buf = kzalloc(1, GFP_KERNEL);

    if (cmmbmemo->data_buf == NULL){
        ret = - ENOMEM;
        DBGERR("[CMMB HW]:[memory]:[err]: cmmb data buffer malloc fail!!!\n");
        goto kmalloc_fail;
    }

    //hzb@20100415,init av ring buffers,cmmb need three ring buffers to store the demuxed data
    cmmb_ringbuffer_init(&cmmbmemo->buffer_Video, cmmbmemo->video_buf, CMMB_VIDEO_BUFFER_SIZE);  //init video ring buffer
    cmmb_ringbuffer_init(&cmmbmemo->buffer_Audio, cmmbmemo->audio_buf, CMMB_AUDIO_BUFFER_SIZE);  //init audio ring buffer
    cmmb_ringbuffer_init(&cmmbmemo->buffer_Data,  cmmbmemo->data_buf,  1);   //init data ring buffer

    cmmbmemo->w_datatype = CMMB_NULL_TYPE;
    cmmbmemo->r_datatype = CMMB_NULL_TYPE;

    return ret;

kmalloc_fail:
    kfree(cmmbmemo->video_buf);
    kfree(cmmbmemo->audio_buf);
    kfree(cmmbmemo->data_buf);
    return ret;   
}

static long cmmbmemo_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct cmmb_memory *cmmbmemo = (struct cmmb_memory*)file->private_data;
    long ret = 0;
    
    DBG("[CMMB HW]:[memory]:enter cmmbdemux_ioctl\n");

    switch (cmd){
	    case CMMB_MEMO_WRITE:{
            cmmbmemo->w_datatype = arg;
        }
        break;
        
        case CMMB_MEMO_READ:{
            cmmbmemo->r_datatype = arg;
        }
        break;

        case CMMB_MEMO_FLUSH_ONE:{
            if (arg == CMMB_VIDEO_TYPE){
                cmmb_ringbuffer_flush(&cmmbmemo->buffer_Video);
            }else if (arg == CMMB_AUDIO_TYPE){
                cmmb_ringbuffer_flush(&cmmbmemo->buffer_Audio);
            }else if (arg == CMMB_DATA_TYPE){
                cmmb_ringbuffer_flush(&cmmbmemo->buffer_Data);
            }else{
                ret = - EINVAL;
            }
        }
        break;
       
        case CMMB_MEMO_FLUSH_ALL:{
            cmmb_ringbuffer_flush(&cmmbmemo->buffer_Video);
            cmmb_ringbuffer_flush(&cmmbmemo->buffer_Audio);
            cmmb_ringbuffer_flush(&cmmbmemo->buffer_Data);
        }
        break;
        
        case CMMB_MEMO_INIT:{
            return cmmbmemo_valueinit(file);
        }
        break;

        case CMMB_SET_VIDEO_TYPE:{
            cmmbmemo->videotype = arg;
        }
        break;
        
        case CMMB_SET_AUDIO_TYPE:{
            cmmbmemo->audiotype = arg;
        }
        break;
        
        case CMMB_SET_AUDIO_SAMPLE:{
            cmmbmemo->audiosample = arg;
        }
        break;

        case CMMB_GET_VIDEO_TYPE:{
            return cmmbmemo->videotype;
        }
        break;
        
        case CMMB_GET_AUDIO_TYPE:{
            return cmmbmemo->audiotype;
        }
        break;
        
        case CMMB_GET_AUDIO_SAMPLE:{
            return cmmbmemo->audiosample;
        }
        break;

        case CMMB_GET_BUFF_FREE:{
            if (arg == CMMB_VIDEO_TYPE){
                ret = (long)cmmb_ringbuffer_free(&cmmbmemo->buffer_Video);
            }else if (arg == CMMB_AUDIO_TYPE){
                ret = (long)cmmb_ringbuffer_free(&cmmbmemo->buffer_Audio);
            }else if (arg == CMMB_DATA_TYPE){
                ret = (long)cmmb_ringbuffer_free(&cmmbmemo->buffer_Data);
            }else{
                ret = - EINVAL;
            }
        }
        break;

        case CMMB_GET_BUFF_AVAIL:{
            if (arg == CMMB_VIDEO_TYPE){
                ret = (long)cmmb_ringbuffer_avail(&cmmbmemo->buffer_Video);
            }else if (arg == CMMB_AUDIO_TYPE){
                ret = (long)cmmb_ringbuffer_avail(&cmmbmemo->buffer_Audio);
            }else if (arg == CMMB_DATA_TYPE){
                ret = (long)cmmb_ringbuffer_avail(&cmmbmemo->buffer_Data);
            }else{
                ret = - EINVAL;
            }
        }
        break;
        
        default:
            ;
        break;
    }
    return ret;
}

static unsigned int cmmbmemo_poll(struct file *file, struct poll_table_struct *wait)
{
    struct cmmb_demux *cmmbmemo = (struct cmmb_memory*)file->private_data;
    unsigned int mask = 0;

    DBG("[CMMB HW]:[memory]:%s [%d]\n",__FUNCTION__,__LINE__);  
    
    //2todo memo poll, now doing nothing
  
    return mask;
}


static int cmmbmemo_mmap(struct file *file, struct vm_area_struct *vma)
{
    //2 todo memo mmmap, now doing nothing
    DBG("[CMMB HW]:[memory]:enter cmmbdemux_ioctl\n");
    return 0;
}


struct file_operations cmmbmemeo_fops = 
{
    .open  = cmmbmemo_open,
    .release = cmmbmemo_release,    
    .read  = cmmbmemo_read,
    .write = cmmbmemo_write,
    .mmap  = cmmbmemo_mmap,
    .poll  = cmmbmemo_poll,
    .unlocked_ioctl = cmmbmemo_ioctl,
};

static int __init cmmbmemo_init(void)
{
    int res;
    
    DBG("[CMMB HW]:[memory]:%s [%d]\n",__FUNCTION__,__LINE__);  
	res =cmmb_register_device(&CMMB_adapter,&cmmbmemo, &cmmbmemeo_fops, NULL, CMMB_DEVICE_MEMO,"cmmb_memo");
    mutex_init(&CMMB_memo.mutex);
    CMMB_memo.usr = 0;
    return res;
}

static void __exit cmmbmemo_exit(void)
{
    DBG("[CMMB HW]:[memory]:%s [%d]\n",__FUNCTION__,__LINE__);  
    cmmb_unregister_device(cmmbmemo);
    //mutex_destroy(mutex);
}

module_init(cmmbmemo_init);
module_exit(cmmbmemo_exit);

MODULE_DESCRIPTION("CMMB demodulator general driver");
MODULE_AUTHOR("HT,HZB,HH,LW");
MODULE_LICENSE("GPL");

