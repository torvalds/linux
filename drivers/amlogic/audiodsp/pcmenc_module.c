/*******************************************************************
 * 
 *  Copyright C 2012 by Amlogic, Inc. All Rights Reserved.
 *
 *  Description: 
 *
 *  Author: jian.xu 
 *  Created: 04/16 2012
 *
 *******************************************************************/
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <asm/uaccess.h>	
#include <linux/device.h>	
#include <linux/mm.h>
#include <mach/am_regs.h>
#include "pcmenc_stream.h"	
#include <linux/amlogic/amports/dsp_register.h>




static int __init audiodsp_pcmenc_init_module(void);
static void __exit audiodsp_pcmenc_exit_module(void);
static int audiodsp_pcmenc_open(struct inode *, struct file *);
static int audiodsp_pcmenc_release(struct inode *, struct file *);
static ssize_t audiodsp_pcmenc_read(struct file *, char *, size_t, loff_t *);
static ssize_t audiodsp_pcmenc_write(struct file *, const char *, size_t, loff_t *);
static int audiodsp_pcmenc_mmap(struct file *filp, struct vm_area_struct *vma);

static long audiodsp_pcmenc_ioctl( struct file *file, unsigned int cmd, unsigned long args);
static int audiodsp_pcmenc_create_stream_buffer(void);
static int audiodsp_pcmenc_destroy_stream_buffer(void);

#define SUCCESS 0
#define DEVICE_NAME "audiodsp_pcmenc"	

#define MIN_CACHE_ALIGN(x)	(((x-4)&(~0x1f)))
#define MAX_CACHE_ALIGN(x)	((x+0x1f)&(~0x1f))

#ifdef MIN
#undef MIN
#endif
#define MIN(x,y)	(((x)<(y))?(x):(y))


static int major;		
static struct class *class_pcmenc;
static struct device *dev_pcmenc;
static int device_opened = 0;	/* Is device open?  
                             * Used to prevent multiple access to device */
static pcm51_encoded_info_t pcminfo = {0};
typedef struct {
       void *stream_buffer_mem; 
	unsigned int stream_buffer_mem_size;
	unsigned long stream_buffer_start;
	unsigned long stream_buffer_end;
	unsigned long stream_buffer_size;	
	unsigned long user_read_offset; //the offset of the stream buffer which user space reading
}priv_data_t; 

static priv_data_t priv_data = {0};

static ssize_t pcmenc_ptr_show(struct class* class, struct class_attribute* attr,
    char* buf)
{
	  ssize_t ret = 0;
	  ret = sprintf(buf, "pcmenc runtime info:\n"
	                     "  pcmenc rd ptr :\t%lx\n"    
	                     "  pcmenc wr ptr :\t%lx\n"    
	                     "  pcmenc level  :\t%x\n",
	                     (DSP_RD(DSP_DECODE_51PCM_OUT_RD_ADDR)),
	                     (DSP_RD(DSP_DECODE_51PCM_OUT_WD_ADDR)),
	                     pcmenc_stream_content()
	                     );
  	return ret;
}
static struct class_attribute pcmenc_attrs[]={
  __ATTR_RO(pcmenc_ptr),
  __ATTR_NULL
};
static void create_pcmenc_attrs(struct class* class)
{
  int i=0,ret;
  for(i=0; pcmenc_attrs[i].attr.name; i++){
    ret=class_create_file(class, &pcmenc_attrs[i]);
  }
}
static void remove_amaudio_attrs(struct class* class)
{
  int i=0;
  for(i=0; pcmenc_attrs[i].attr.name; i++){
    class_remove_file(class, &pcmenc_attrs[i]);
  }
}
static struct file_operations fops = {
    .read = audiodsp_pcmenc_read,
    .unlocked_ioctl = audiodsp_pcmenc_ioctl,
    .write = audiodsp_pcmenc_write,
    .open = audiodsp_pcmenc_open,
    .mmap = audiodsp_pcmenc_mmap,
    .release = audiodsp_pcmenc_release
};
static int __init audiodsp_pcmenc_init_module(void)
{
    void *ptr_err;
    int ret = 0;
    major = register_chrdev(0, DEVICE_NAME, &fops);

    if (major < 0) {
        printk(KERN_ALERT "Registering char device %s failed with %d\n", DEVICE_NAME, major);
        return major;
    }

    class_pcmenc = class_create(THIS_MODULE, DEVICE_NAME);
    if(IS_ERR(ptr_err = class_pcmenc)){
        goto err0;
    }
    create_pcmenc_attrs(class_pcmenc);
    dev_pcmenc = device_create(class_pcmenc, NULL, MKDEV(major,0),NULL, DEVICE_NAME);
    if(IS_ERR(ptr_err = dev_pcmenc)){
        goto err1;
    }

    priv_data.stream_buffer_mem_size = 2*2*512*1024+PAGE_SIZE;//buffer should be page aligned for mmap operation

    ret = audiodsp_pcmenc_create_stream_buffer(); 
    if(ret){
        goto err2;
    }

    printk(KERN_INFO "amlogic audio dsp pcmenc device init!\n");

    return SUCCESS;

err2:
    device_destroy(class_pcmenc, MKDEV(major, 0));
err1:
    class_destroy(class_pcmenc);
err0:
    unregister_chrdev(major, DEVICE_NAME);
    return PTR_ERR(ptr_err);
}

static void __exit audiodsp_pcmenc_exit_module(void)
{
    device_destroy(class_pcmenc, MKDEV(major, 0));
    remove_amaudio_attrs(class_pcmenc);
    class_destroy(class_pcmenc);
    unregister_chrdev(major, DEVICE_NAME);

    audiodsp_pcmenc_destroy_stream_buffer();
}

static int audiodsp_pcmenc_open(struct inode *inode, struct file *file)
{
	if (device_opened)
		return -EBUSY;
	device_opened++;
	try_module_get(THIS_MODULE);
#if  0
	if(buf == NULL)	
		buf = (char *)kmalloc(priv_data.stream_buffer_size, GFP_KERNEL);
	if(buf == NULL){
		device_opened--; 	
		module_put(THIS_MODULE);
		printk("pcmenc: malloc buffer failed\n");
		return -ENOMEM;
	}
#endif /* 0 */
	//audiodsp_pcmenc_create_stream_buffer(); //init the r/p register	
	pcmenc_stream_init();
	return SUCCESS;
}

static int audiodsp_pcmenc_release(struct inode *inode, struct file *file)
{
    device_opened--;		
    module_put(THIS_MODULE);
#if 0    
    if(buf){
        kfree(buf);
        buf = NULL;
    }
#endif
	audiodsp_pcmenc_create_stream_buffer(); //init the r/p register 

    pcmenc_stream_deinit();

    return SUCCESS;
}

static ssize_t audiodsp_pcmenc_read(struct file *filp,	
        char __user *buffer,	
        size_t length,	
        loff_t * offset)
{
#if 1
    int bytes_read = 0;
    int len = 0;
    if(buffer == NULL){
        return 0;
    }
    len = MIN(length, pcmenc_stream_content());
    bytes_read = pcmenc_stream_read(buffer, len);
    return bytes_read;
#else
    printk(KERN_ALERT "Sorry, read operation isn't supported.\n");
    return -EINVAL;
#endif
	return length;
}

static ssize_t audiodsp_pcmenc_write(struct file *filp, const char *buff, size_t len, loff_t * off)
{
    printk(KERN_ALERT "Sorry, this operation isn't supported.\n");
    return -EINVAL;
}
static int audiodsp_pcmenc_mmap(struct file *filp, struct vm_area_struct *vma)
{
    unsigned long off = vma->vm_pgoff << PAGE_SHIFT;
    unsigned vm_size = vma->vm_end - vma->vm_start;

    if (vm_size == 0) {
        return -EAGAIN;
    }
    off += virt_to_phys((void*)priv_data.stream_buffer_start);

    vma->vm_flags |= VM_DONTEXPAND | VM_DONTDUMP | VM_IO;

    if (remap_pfn_range(vma, vma->vm_start, off >> PAGE_SHIFT,
                        vma->vm_end - vma->vm_start, vma->vm_page_prot)) {
        printk("	pcmenc : failed remap_pfn_range\n");
        return -EAGAIN;
    }
    printk("pcmenc: mmap finished\n");	
    return 0;

	
}
static long audiodsp_pcmenc_ioctl( struct file *file, unsigned int cmd, unsigned long args)
{
	int ret = 0;
	switch(cmd){
		case AUDIODSP_PCMENC_GET_RING_BUF_SIZE:
			put_user(priv_data.stream_buffer_size,(__u64 __user *)args);
			break;
		case AUDIODSP_PCMENC_GET_RING_BUF_CONTENT:
			put_user(pcmenc_stream_content(),(__s32 __user *)args);
			break;
		case AUDIODSP_PCMENC_GET_RING_BUF_SPACE:
			put_user(pcmenc_stream_space(),(__s32 __user *)args);
			break;
		case AUDIODSP_PCMENC_SET_RING_BUF_RPTR:
			priv_data.user_read_offset = (unsigned long)args;
		//	printk("dsp rd ptr %x\n",DSP_RD(DSP_DECODE_51PCM_OUT_RD_ADDR));
			DSP_WD(DSP_DECODE_51PCM_OUT_RD_ADDR, ARM_2_ARC_ADDR_SWAP(priv_data.stream_buffer_start+priv_data.user_read_offset));
		//	printk("dsp rd ptr change to %x\n",DSP_RD(DSP_DECODE_51PCM_OUT_RD_ADDR));

			break;
		case AUDIODSP_PCMENC_GET_PCMINFO:
			if(args == 0){
				printk("pcm enc: args invalid\n");
				ret = -EINVAL;
			}
			 ret = copy_to_user( (void __user *)args,&pcminfo, sizeof(pcminfo));
			 if(ret != 0){
			 	printk("pcm enc:copy to user error\n");
			 }
			break;
		default:
			printk("pcmenc:un-implemented  cmd\n");	
			break;
	}
	return ret;
}

static int audiodsp_pcmenc_create_stream_buffer(void)
{
    dma_addr_t buf_map;
#if 0
    DSP_WD(DSP_DECODE_51PCM_OUT_START_ADDR, ARM_2_ARC_ADDR_SWAP(0));
    DSP_WD(DSP_DECODE_51PCM_OUT_END_ADDR,ARM_2_ARC_ADDR_SWAP(0));
    DSP_WD(DSP_DECODE_51PCM_OUT_RD_ADDR,ARM_2_ARC_ADDR_SWAP(0));
    DSP_WD(DSP_DECODE_51PCM_OUT_WD_ADDR,ARM_2_ARC_ADDR_SWAP(0));
#endif
    if(priv_data.stream_buffer_mem_size == 0){
        return 0;
    }
    if(priv_data.stream_buffer_mem == NULL)
        priv_data.stream_buffer_mem = (void*)kmalloc(priv_data.stream_buffer_mem_size,GFP_KERNEL);
    if(priv_data.stream_buffer_mem == NULL){
        printk("kmalloc error,no memory for audio dsp pcmenc stream buffer\n");
        return -ENOMEM;
    }
    memset((void *)priv_data.stream_buffer_mem, 0, priv_data.stream_buffer_mem_size);
    buf_map = dma_map_single(NULL, (void *)priv_data.stream_buffer_mem, priv_data.stream_buffer_mem_size, DMA_TO_DEVICE);
    dma_unmap_single(NULL, buf_map,  priv_data.stream_buffer_mem_size, DMA_TO_DEVICE);
    wmb();	
    priv_data.stream_buffer_start = PAGE_ALIGN((unsigned long)priv_data.stream_buffer_mem+PAGE_SIZE-1);
    priv_data.stream_buffer_end = PAGE_ALIGN((unsigned long)priv_data.stream_buffer_mem + priv_data.stream_buffer_mem_size);
    priv_data.stream_buffer_size = priv_data.stream_buffer_end - priv_data.stream_buffer_start;
    if(priv_data.stream_buffer_size < 0){
        printk("DSP pcmenc Stream buffer set error,must more larger,mensize = %d,buffer size=%ld\n",
                priv_data.stream_buffer_mem_size,priv_data.stream_buffer_size);
        kfree(priv_data.stream_buffer_mem);
        priv_data.stream_buffer_mem = NULL;
        return -2;
    }

    DSP_WD(DSP_DECODE_51PCM_OUT_START_ADDR,ARM_2_ARC_ADDR_SWAP(priv_data.stream_buffer_start));
    DSP_WD(DSP_DECODE_51PCM_OUT_END_ADDR,ARM_2_ARC_ADDR_SWAP(priv_data.stream_buffer_end));
    DSP_WD(DSP_DECODE_51PCM_OUT_RD_ADDR,ARM_2_ARC_ADDR_SWAP(priv_data.stream_buffer_start));
    DSP_WD(DSP_DECODE_51PCM_OUT_WD_ADDR,ARM_2_ARC_ADDR_SWAP(priv_data.stream_buffer_start));

    printk("DSP pcmenc stream buffer to [%#lx-%#lx]\n",(long unsigned int)ARM_2_ARC_ADDR_SWAP(priv_data.stream_buffer_start),(long unsigned int) ARM_2_ARC_ADDR_SWAP(priv_data.stream_buffer_end));
    return 0;
}

static int audiodsp_pcmenc_destroy_stream_buffer(void)
{
    if(priv_data.stream_buffer_mem){
        kfree(priv_data.stream_buffer_mem);
        priv_data.stream_buffer_mem = NULL;
    }

    DSP_WD(DSP_DECODE_51PCM_OUT_START_ADDR, ARM_2_ARC_ADDR_SWAP(0));
    DSP_WD(DSP_DECODE_51PCM_OUT_END_ADDR,ARM_2_ARC_ADDR_SWAP(0));
    DSP_WD(DSP_DECODE_51PCM_OUT_RD_ADDR,ARM_2_ARC_ADDR_SWAP(0));
    DSP_WD(DSP_DECODE_51PCM_OUT_WD_ADDR,ARM_2_ARC_ADDR_SWAP(0));
    return 0;
}
void set_pcminfo_data(void *pcm_encoded_info)
{
    	dma_addr_t buf_map;
/*as this ptr got from arc dsp side,which mapping to 0 address,so add this dsp start offset */ 		
	pcm51_encoded_info_t *info = (pcm51_encoded_info_t*)((unsigned)pcm_encoded_info+AUDIO_DSP_START_ADDR);
	/* inv dcache as this data from device */
	buf_map = dma_map_single(dev_pcmenc,(void*)info ,sizeof(pcm51_encoded_info_t),DMA_FROM_DEVICE);
	dma_unmap_single(dev_pcmenc,buf_map,sizeof(pcm51_encoded_info_t),DMA_FROM_DEVICE);	
	memcpy(&pcminfo,info,sizeof(pcm51_encoded_info_t));
	printk("got pcm51 info from dsp \n");
}
EXPORT_SYMBOL(set_pcminfo_data);


module_init(audiodsp_pcmenc_init_module);
module_exit(audiodsp_pcmenc_exit_module);
MODULE_DESCRIPTION("AMLOGIC PCM encoder interface driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("jian.xu <jian.xu@amlogic.com>");
