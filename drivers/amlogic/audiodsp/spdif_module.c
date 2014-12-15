/*******************************************************************
 * 
 *  Copyright C 2012 by Amlogic, Inc. All Rights Reserved.
 *
 *  Description: 
 *
 *  Author: jian.xu 
 *  Created: 04/18 2012
 *
 * amlogic s/pdif output driver 
 *******************************************************************/
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <asm/uaccess.h>	
#include <linux/device.h>	
#include <linux/mm.h>
#include <linux/dma-mapping.h>
#include <mach/am_regs.h>
#include <linux/amlogic/amports/dsp_register.h>

#include "spdif_module.h"


#define DEVICE_NAME    "audio_spdif"

static int device_opened = 0;
static int major_spdif;
static struct class *class_spdif;
static struct device *dev_spdif;
static struct mutex mutex_spdif;  
static  unsigned iec958_wr_offset;
extern void	aml_alsa_hw_reprepare(void);
extern  void audio_enable_ouput(int flag);

extern unsigned int IEC958_mode_raw;
extern unsigned int IEC958_mode_codec;


static inline int if_audio_output_iec958_enable(void)
{
	return READ_MPEG_REG_BITS(AIU_MEM_IEC958_CONTROL, 1, 2);	
}
static inline int if_audio_output_i2s_enable(void)
{
	return READ_MPEG_REG_BITS(AIU_MEM_I2S_CONTROL, 1, 2);
}
static inline void audio_output_iec958_enable(unsigned flag)
{
		if(flag){
	              WRITE_MPEG_REG(AIU_958_FORCE_LEFT, 0);
	              WRITE_MPEG_REG_BITS(AIU_958_DCU_FF_CTRL, 1, 0, 1);
	              WRITE_MPEG_REG_BITS(AIU_MEM_IEC958_CONTROL, 3, 1, 2);
		}
		else{
            		WRITE_MPEG_REG(AIU_958_DCU_FF_CTRL, 0);
            		WRITE_MPEG_REG_BITS(AIU_MEM_IEC958_CONTROL, 0, 1, 2);			
		}
}
static int audio_spdif_open(struct inode *inode, struct file *file)
{
	if (device_opened){
		printk("error,audio_spdif device busy\n");
		return -EBUSY;
	}	
	device_opened++;
	try_module_get(THIS_MODULE);
	return 0;
}
static int audio_spdif_release(struct inode *inode, struct file *file)
{
   // audio_enable_ouput(0);
    audio_output_iec958_enable(0);  
    device_opened--;		
    module_put(THIS_MODULE);
	IEC958_mode_codec = 0;
    return 0;
}
static long audio_spdif_ioctl(struct file *file, unsigned int cmd, unsigned long args)
{
	int err = 0;
	int tmp = 0;
	mutex_lock(&mutex_spdif);	
	switch(cmd){
		case AUDIO_SPDIF_GET_958_BUF_RD_OFFSET:
			tmp = READ_MPEG_REG(AIU_MEM_IEC958_RD_PTR) -READ_MPEG_REG(AIU_MEM_IEC958_START_PTR);
			put_user(tmp,(__s32 __user *)args);
			break;
		case AUDIO_SPDIF_GET_958_BUF_SIZE:
			tmp = READ_MPEG_REG(AIU_MEM_IEC958_END_PTR) -READ_MPEG_REG(AIU_MEM_IEC958_START_PTR)+64;//iec958_info.iec958_buffer_size;
			put_user(tmp,(__s32 __user *)args);
			break;
		case AUDIO_SPDIF_GET_958_ENABLE_STATUS:
			put_user(if_audio_output_iec958_enable(),(__s32 __user *)args);
			break;	
		case AUDIO_SPDIF_GET_I2S_ENABLE_STATUS:
			put_user(if_audio_output_i2s_enable(),(__s32 __user *)args);
			break;	
		case AUDIO_SPDIF_SET_958_ENABLE:
		//	IEC958_mode_raw = 1;
			//audio_enable_ouput(1);
			audio_output_iec958_enable(args);
			break;
		case AUDIO_SPDIF_SET_958_INIT_PREPARE:
			IEC958_mode_codec = 3; //dts pcm raw
			DSP_WD(DSP_IEC958_INIT_READY_INFO, 0x12345678);
			aml_alsa_hw_reprepare();
			DSP_WD(DSP_IEC958_INIT_READY_INFO, 0);
			break;
		case AUDIO_SPDIF_SET_958_WR_OFFSET:
			get_user(iec958_wr_offset,(__u32 __user *)args);
			break;	
		default:
			printk("audio spdif: cmd not implemented\n");
			break;
	}
	mutex_unlock(&mutex_spdif);
	return err;
}
static ssize_t audio_spdif_write(struct file *file, const char __user *userbuf,size_t len, loff_t * off)
{

	char   *wr_ptr;
	unsigned long wr_addr;
	dma_addr_t buf_map;	
	
	wr_addr = READ_MPEG_REG(AIU_MEM_IEC958_START_PTR)+iec958_wr_offset;
	wr_ptr= (char*)phys_to_virt(wr_addr);
	if(copy_from_user((void*)wr_ptr, (void*)userbuf, len) != 0)
	{
		printk("audio spdif: copy from user failed\n");
		return -EINVAL;	
	}
       buf_map = dma_map_single(NULL, (void *)wr_ptr,len, DMA_TO_DEVICE);		
       dma_unmap_single(NULL, buf_map,len, DMA_TO_DEVICE);
	return   0;
}

static ssize_t audio_spdif_read(struct file *filp,	
        char __user *buffer,	
        size_t length,	
        loff_t * offset)
{
    printk(KERN_ALERT "audio spdif: read operation isn't supported.\n");
    return -EINVAL;
}

static int audio_spdif_mmap(struct file *file, struct vm_area_struct *vma)
{
    unsigned long off = vma->vm_pgoff << PAGE_SHIFT;
    unsigned vm_size = vma->vm_end - vma->vm_start;
    if (vm_size == 0) {
	 printk("audio spdif:vm_size 0\n"); 	
        return -EAGAIN;
    }
    off = READ_MPEG_REG(AIU_MEM_IEC958_START_PTR);//mapping the 958 dma buffer to user space to write

    vma->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP | VM_IO/*|VM_MAYWRITE|VM_MAYSHARE*/;
    if (remap_pfn_range(vma, vma->vm_start, off >> PAGE_SHIFT,
                        vma->vm_end - vma->vm_start, vma->vm_page_prot)) {
        printk("	audio spdif : failed remap_pfn_range\n");
        return -EAGAIN;
    }
    printk("audio spdif: mmap finished\n");	
    printk("audio spdif: 958 dma buf:py addr 0x%x,vir addr 0x%x\n",READ_MPEG_REG(AIU_MEM_IEC958_START_PTR), \
			(unsigned int)phys_to_virt(READ_MPEG_REG(AIU_MEM_IEC958_START_PTR)));		
    return 0;

}
static ssize_t audio_spdif_ptr_show(struct class* class, struct class_attribute* attr,
    char* buf)
{
	  ssize_t ret = 0;
	  ret = sprintf(buf, "iec958 buf runtime info:\n"
	                     "  iec958 rd ptr :\t%x\n"    
	                     "  iec958 wr ptr :\t%x\n" ,   
	                     READ_MPEG_REG(AIU_MEM_IEC958_RD_PTR),
	                     (READ_MPEG_REG(AIU_MEM_IEC958_START_PTR)+iec958_wr_offset));
  	return ret;
}
static ssize_t audio_spdif_buf_show(struct class* class, struct class_attribute* attr,
    char* buf)
{
	  ssize_t ret = 0;
	  unsigned *ptr = (unsigned*)phys_to_virt(READ_MPEG_REG(AIU_MEM_IEC958_RD_PTR)+iec958_wr_offset);
	  ret = sprintf(buf, "iec958 buf  info:\n"
	                     "  iec958 wr ptr val:\t[%x][%x][%x][%x]\n"    ,
	                     ptr[0], ptr[1],ptr[2], ptr[3]);
  	return ret;
}

static struct class_attribute audio_spdif_attrs[]={
  __ATTR_RO(audio_spdif_ptr),
  __ATTR_RO(audio_spdif_buf),
  __ATTR_NULL
};
static struct file_operations fops_spdif = {
    .read = audio_spdif_read,
    .unlocked_ioctl = audio_spdif_ioctl,
    .write = audio_spdif_write,
    .open = audio_spdif_open,
    .mmap = audio_spdif_mmap,
    .release = audio_spdif_release
};
static void create_audio_spdif_attrs(struct class* class)
{
  int i=0,ret;
  for(i=0; audio_spdif_attrs[i].attr.name; i++){
    ret=class_create_file(class, &audio_spdif_attrs[i]);
  }
}
static int __init audio_spdif_init_module(void)
{
    void *ptr_err;
    major_spdif = register_chrdev(0, DEVICE_NAME, &fops_spdif);
    if (major_spdif < 0) {
        printk(KERN_ALERT "Registering spdif char device %s failed with %d\n", DEVICE_NAME, major_spdif);
        return major_spdif;
    }
    class_spdif = class_create(THIS_MODULE, DEVICE_NAME);
    if(IS_ERR(ptr_err = class_spdif)){
        goto err0;
    }
    create_audio_spdif_attrs(class_spdif);
    dev_spdif = device_create(class_spdif, NULL, MKDEV(major_spdif,0),NULL, DEVICE_NAME);
    if(IS_ERR(ptr_err = dev_spdif)){
        goto err1;
    }
    mutex_init(&mutex_spdif);	
    printk(KERN_INFO "amlogic audio spdif interface device init!\n");
    return 0;
#if 0
err2:
    device_destroy(class_spdif, MKDEV(major_spdif, 0));
#endif
err1:
    class_destroy(class_spdif);
err0:
    unregister_chrdev(major_spdif, DEVICE_NAME);
    return PTR_ERR(ptr_err);
}
static int __exit  audio_spdif_exit_module(void)
{
    device_destroy(class_spdif, MKDEV(major_spdif, 0));
    class_destroy(class_spdif);
    unregister_chrdev(major_spdif, DEVICE_NAME);	
    return 0;	
}
module_init(audio_spdif_init_module);
module_exit(audio_spdif_exit_module);
MODULE_DESCRIPTION("AMLOGIC IEC958 output interface driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("jian.xu <jian.xu@amlogic.com>");
