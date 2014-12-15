#include <linux/version.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/io.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <asm/cacheflush.h>
#include <linux/major.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/dma-mapping.h>
#include <linux/hrtimer.h>
#include <linux/err.h>
#include <linux/jiffies.h>

#include <mach/am_regs.h>
#include <linux/amlogic/amports/amaudio.h>

#include "amaudio2.h"

MODULE_DESCRIPTION("AMLOGIC Audio Control Interface driver V2");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("kevin.wang@amlogic.com");
MODULE_VERSION("2.0.0");

static const struct file_operations amaudio_fops = {
  .owner    =   THIS_MODULE,
  .open     =   amaudio_open,
  .unlocked_ioctl    =   amaudio_ioctl,
  .release  =   amaudio_release,
};

const static struct file_operations amaudio_out_fops = {
  .owner    =   THIS_MODULE,
  .open     =   amaudio_open,
  .release  =   amaudio_release,
  .mmap     = 	amaudio_mmap,
  .read     =   amaudio_read,
  .unlocked_ioctl    =   amaudio_ioctl,
};

const static struct file_operations amaudio_in_fops = {
  .owner    =   THIS_MODULE,
  .open     =   amaudio_open,
  .release  =   amaudio_release,
  .mmap	    = 	amaudio_mmap,
  .unlocked_ioctl    =   amaudio_ioctl,
};

const static struct file_operations amaudio_ctl_fops = {
  .owner    =   THIS_MODULE,
  .open     =   amaudio_open,
  .release  =   amaudio_release,
  .unlocked_ioctl    =   amaudio_ioctl,
};

const static struct file_operations amaudio_utils_fops = {
  .owner    =   THIS_MODULE,
  .open     =   amaudio_open,
  .release  =   amaudio_release,
  .unlocked_ioctl    =   amaudio_utils_ioctl,
};

static amaudio_port_t amaudio_ports[]={
  {
    .name = "amaudio2_out",
    .fops = &amaudio_out_fops,
  },
  {
    .name = "amaudio2_in",
    .fops = &amaudio_in_fops,
  },
  {
    .name = "amaudio2_ctl",
    .fops = &amaudio_ctl_fops,
  },
  {
    .name = "amaudio2_utils",
    .fops = &amaudio_utils_fops,
  },
};

#define AMAUDIO_DEVICE_COUNT    ARRAY_SIZE(amaudio_ports)

static dev_t amaudio_devno;
static struct class* amaudio_clsp;
static struct cdev*  amaudio_cdevp;

static int direct_left_gain = 128;
static int direct_right_gain = 128;
static int music_gain = 128;
static int audio_out_mode = 0;
static int audio_out_read_enable = 0;

static irqreturn_t i2s_out_callback(int irq, void* data);
static unsigned get_i2s_out_size(void);
static unsigned get_i2s_out_ptr(void);

#define SOFT_BUFFER_SIZE (PAGE_SIZE*16)
#define MAX_LATENCY (64*32*3)
#define MIN_LATENCY (64*32)
static unsigned latency = MIN_LATENCY*2; //20ms

static u64 amaudio_pcm_dmamask = DMA_BIT_MASK(32);
#define HRTIMER_PERIOD (1000000000UL/1000)

static int amaudio_open(struct inode *inode, struct file *file)
{
  amaudio_port_t* this = &amaudio_ports[iminor(inode)];
  amaudio_t * amaudio = kzalloc(sizeof(amaudio_t), GFP_KERNEL);
  int res = 0;
  
  if(iminor(inode)== 0){  	
  	printk(KERN_DEBUG "amaudio2_out opened\n");
  	if(!this->dev->dma_mask)
  		this->dev->dma_mask = &amaudio_pcm_dmamask;
  	if(!this->dev->coherent_dma_mask)
  		this->dev->coherent_dma_mask = 0xffffffff;
  		
  	amaudio->sw.addr = (char*)dma_alloc_coherent(this->dev, SOFT_BUFFER_SIZE, &amaudio->sw.paddr, GFP_KERNEL);
  	amaudio->sw.size = SOFT_BUFFER_SIZE;
  	if(!amaudio->sw.addr){
  		res = -ENOMEM;
		printk(KERN_ERR "amaudio2 out soft DMA buffer alloc failed\n");
  		goto error;
  	}

	amaudio->sw_read.addr = (char*)kzalloc((SOFT_BUFFER_SIZE), GFP_KERNEL);
	if(amaudio->sw_read.addr == 0){
		res = -ENOMEM;
		printk(KERN_ERR "amaudio2 out read soft buffer alloc failed\n");
		goto error;
	}
	amaudio->sw_read.size = SOFT_BUFFER_SIZE;
	
  	amaudio->hw.addr = (char*)aml_i2s_playback_start_addr;
  	amaudio->hw.paddr = aml_i2s_playback_phy_start_addr;
  	amaudio->hw.size = get_i2s_out_size();
  	amaudio->hw.rd = get_i2s_out_ptr();
		
	//printk(KERN_DEBUG "amaudio->sw.addr=%08x,amaudio->sw.paddr=%08x \n amaudio->hw.addr=%08x,amaudio->hw.paddr=%08x\n",
	//(unsigned int)amaudio->sw.addr,amaudio->sw.paddr,(unsigned int)amaudio->hw.addr,amaudio->hw.paddr);
	
  	WRITE_MPEG_REG_BITS(AIU_MEM_I2S_MASKS,0, 16, 16);
  	if(request_irq(INT_AMRISC_DC_PCMLAST, i2s_out_callback, IRQF_SHARED, "i2s_out",amaudio)){
  		res = -EINVAL;
  		goto error;
  	}
  	spin_lock_init(&amaudio->sw.lock);
  	spin_lock_init(&amaudio->hw.lock);
	spin_lock_init(&amaudio->sw_read.lock);
  	
  }else if(iminor(inode) == 1){
  	printk(KERN_DEBUG "amaudio2_in opened\n");
  	if(!this->dev->dma_mask)
  		this->dev->dma_mask = &amaudio_pcm_dmamask;
  	if(!this->dev->coherent_dma_mask)
  		this->dev->coherent_dma_mask = 0xffffffff;
  		
  }else if(iminor(inode) == 2){
  	printk(KERN_DEBUG "amaudio2_ctl opened\n");
  }else if(iminor(inode) == 3){
  	printk(KERN_DEBUG "amaudio2_utils opened\n");
  }else{
  	printk(KERN_ERR "BUG:%s,%d, please check\n", __FILE__, __LINE__);
  	res = -EINVAL;
  	goto error;
  }
  
  amaudio->type = iminor(inode);
  amaudio->dev = this->dev;
  file->private_data = amaudio;
  file->f_op = this->fops;
  this->runtime = amaudio;
	return res;
error:
	kfree(amaudio);
	return res;	
}

static int amaudio_release(struct inode *inode, struct file *file)
{
	//unsigned long irqflags;
	
	amaudio_t * amaudio = (amaudio_t *)file->private_data;
	
	//spin_lock_irqsave(&amaudio->hw.lock,irqflags);

	free_irq(INT_AMRISC_DC_PCMLAST, amaudio);

	//spin_unlock_irqrestore(&amaudio->hw.lock,irqflags);
	
	if(amaudio->sw.addr){
		dma_free_coherent(amaudio->dev, amaudio->sw.size, (void*)amaudio->sw.addr, amaudio->sw.paddr);
		amaudio->sw.addr = 0;
	}

	if(amaudio->sw_read.addr){
		kfree(amaudio->sw_read.addr);
		amaudio->sw_read.addr = 0;
	}
	kfree(amaudio);
	return 0;
}

static int amaudio_mmap(struct file*file, struct vm_area_struct* vma)
{
	amaudio_t * amaudio = (amaudio_t *)file->private_data;
	if(amaudio->type == 0){
		int mmap_flag = dma_mmap_coherent(amaudio->dev, vma, (void*)amaudio->sw.addr, 
													amaudio->sw.paddr, amaudio->sw.size);
		//printk(KERN_DEBUG " amaudio->sw.addr=%08x,amaudio->sw.paddr=%08x, mmap_flag = %08x \n",
		//						(unsigned int)amaudio->sw.addr,amaudio->sw.paddr,mmap_flag);
		return mmap_flag;
	}else if(amaudio->type == 1){
		
	}else{
		return -ENODEV;
	}
	return 0;
}

static ssize_t amaudio_read(struct file *file, char __user *buf, size_t count, loff_t * pos){

	int ret = 0;
	amaudio_t * amaudio = (amaudio_t *)file->private_data;
	BUF* sw_read = &amaudio->sw_read;
	if(amaudio->type == 0){
		ret = copy_to_user((void*)buf, (void*)(sw_read->addr+sw_read->rd), count);
		return (count - ret);
	}else if(amaudio->type == 1){
		
	}else{
		return -ENODEV;
	}
	return 0;
}

static unsigned get_i2s_out_size(void)
{
	return READ_MPEG_REG(AIU_MEM_I2S_END_PTR) - READ_MPEG_REG(AIU_MEM_I2S_START_PTR) + 64;
}

static unsigned get_i2s_out_ptr(void)
{
	return READ_MPEG_REG(AIU_MEM_I2S_RD_PTR) - READ_MPEG_REG(AIU_MEM_I2S_START_PTR);
}

void cover_memcpy(BUF *des, int a, BUF *src, int b, unsigned count)
{
	int i=0;
	char *in,*out;
	out = des->addr + a;
	in = src->addr + b;
	for(i = 0; i < count; i++){
		out[i] = in[i];
	}
}

void direct_mix_memcpy(BUF *des, int a, BUF *src, int b, unsigned count)
{
	int i,j;
	short sampL,sampR;
	int samp;
	
	short *des_left = (short*)(des->addr + a);
	short *des_right = des_left + 16;
	short *src_left = (short*)(src->addr + b);
	short *src_right = src_left + 16;

	for(i = 0; i < count; i += 64){
		for(j = 0; j < 16; j ++){
			sampL = *src_left++;
			sampR = *src_right++;
			
			samp = (((*des_left)*music_gain) + sampL*direct_left_gain)>>8;
			if(samp > 0x7fff) samp = 0x7fff;
			if(samp < -0x8000) samp = -0x8000;
			*des_left++ = (short)(samp&0xffff);

			samp = (((*des_right)*music_gain) + sampR*direct_right_gain)>>8;
			if(samp > 0x7fff) samp = 0x7fff;
			if(samp < -0x8000) samp = -0x8000;
			*des_right++ = (short)(samp&0xffff);
		}
		src_left += 16;
		src_right += 16;
		des_left += 16;
		des_right += 16;
	}
}

void inter_mix_memcpy(BUF *des, int a, BUF *src, int b, unsigned count)
{
	int i,j;
	short sampL,sampR;
	int samp, sampLR;
	
	short *des_left = (short*)(des->addr+a);
	short *des_right = des_left + 16;
	short *src_left = (short*)(src->addr+b);
	short *src_right = src_left + 16;

	for(i = 0; i < count; i += 64){
		for(j = 0; j < 16; j ++){
			sampL = *src_left++;
			sampR = *src_right++;

			//Here has risk to distortion. Linein signals are always weak, so add them direct.
			sampLR = sampL*direct_left_gain + sampR*direct_right_gain;
			samp = (((*des_left)*music_gain) + sampLR)>>8;
			if(samp > 0x7fff) samp = 0x7fff;
			if(samp < -0x8000) samp = -0x8000;
			*des_left++ = (short)(samp&0xffff);

			samp = (((*des_right)*music_gain) + sampLR)>>8;
			if(samp > 0x7fff) samp = 0x7fff;
			if(samp < -0x8000) samp = -0x8000;
			*des_right++ = (short)(samp&0xffff);
		}
		src_left += 16;
		src_right += 16;
		des_left += 16;
		des_right += 16;
	}
}

void interleave_memcpy(BUF *des, int a, BUF *src, int b, unsigned count)
{
	int i,j;
	short *out = (short*)(des->addr + a);
	short *in_left = (short*)(src->addr + b);
	short *in_right = in_left + 16;
	
	for(i = 0; i < count; i += 64){
		for(j = 0; j < 16; j ++){
			*out++ = *in_left++;
			*out++ = *in_right++;
		}
		in_left += 16;
		in_right += 16;
	}
}

#define INT_NUM		(16)	//min 2, max 32
#define I2S_BLOCK	(64)
#define INT_BLOCK ((INT_NUM)*(I2S_BLOCK))

//#define AMAUDIO2_DEBUG
#ifdef AMAUDIO2_DEBUG
static int counter = 0;
#endif
static void i2s_copy(amaudio_t* amaudio)
{
	BUF* hw = &amaudio->hw;
	BUF* sw = &amaudio->sw;
	BUF* sw_read = &amaudio->sw_read;
	unsigned valid_data;
	unsigned long swirqflags, hwirqflags, sw_readirqflags;
	unsigned i2s_out_ptr = get_i2s_out_ptr();
	unsigned alsa_delay = (aml_i2s_alsa_write_addr + hw->size - i2s_out_ptr)%hw->size;
	unsigned amaudio_delay = (hw->wr + hw->size - i2s_out_ptr)%hw->size;
	
	spin_lock_irqsave(&hw->lock,hwirqflags);
	hw->rd = (int)i2s_out_ptr;
	hw->level -= INT_BLOCK;
	if(hw->level <= INT_BLOCK){
		hw->wr = ((hw->rd+latency)%hw->size);
		hw->wr /= INT_BLOCK;
		hw->wr *= INT_BLOCK;
		hw->level = latency;
		goto EXIT;
	}

	if((alsa_delay - amaudio_delay) <= INT_BLOCK){
		//printk(KERN_DEBUG "Reset hw pointer: alsa_delay:%x, amaudio_delay:%x, latency = %x\n",
		//	alsa_delay,amaudio_delay,latency);
		goto EXIT;
	}
	
#ifdef AMAUDIO2_DEBUG
	if(counter >= 500){
		//printk(KERN_DEBUG "alsa_delay:%x, amaudio_delay:%x, hw->level = %x\n",
		//	alsa_delay,amaudio_delay,hw->level);
		printk(KERN_DEBUG "sw->level = %x\n",sw->level);
		counter = 0;
	}
	counter++;
#endif

	if(audio_out_mode != 3){
		valid_data = sw->level&~0x3f;
		if(valid_data < INT_BLOCK) {
			goto EXIT;
		}
	}
	
	if(audio_out_read_enable == 1){
		valid_data = sw_read->level&~0x3f;
		if(valid_data < INT_BLOCK) {
			goto EXIT;
		}
	}

	BUG_ON((hw->wr+INT_BLOCK>hw->size)||(sw->rd+INT_BLOCK>sw->size));
	BUG_ON((hw->wr<0)||(sw->rd<0));

	if(audio_out_mode == 0){
		cover_memcpy(hw,hw->wr,sw,sw->rd,INT_BLOCK);
	}else if(audio_out_mode == 1){
		inter_mix_memcpy(hw,hw->wr,sw,sw->rd,INT_BLOCK);
	}else if(audio_out_mode == 2){
		direct_mix_memcpy(hw,hw->wr,sw,sw->rd,INT_BLOCK);
	}

	if(audio_out_read_enable == 1){
		interleave_memcpy(sw_read,sw_read->wr,hw,hw->wr,INT_BLOCK);
		spin_lock_irqsave(&sw_read->lock,sw_readirqflags);
		sw_read->wr = (sw_read->wr + INT_BLOCK)%sw_read->size;
		sw_read->level -= INT_BLOCK;
		spin_unlock_irqrestore(&sw_read->lock,sw_readirqflags);
	}
	
	hw->wr = (hw->wr + INT_BLOCK)%hw->size;
	hw->level += INT_BLOCK;
	
	spin_lock_irqsave(&sw->lock,swirqflags);
	sw->rd = (sw->rd + INT_BLOCK)%sw->size;
	sw->level -= INT_BLOCK;
	spin_unlock_irqrestore(&sw->lock,swirqflags);
	
EXIT:
	spin_unlock_irqrestore(&hw->lock,hwirqflags);
	return;
}

static irqreturn_t i2s_out_callback(int irq, void* data)
{
	amaudio_t* amaudio = (amaudio_t*)data;
	BUF* hw = &amaudio->hw;
	unsigned tmp;

	//printk("irq: hw: rd=%d, wr=%d,level=%d\n", hw->rd, hw->wr, hw->level);
	tmp = READ_MPEG_REG_BITS(AIU_MEM_I2S_MASKS, 16, 16);
  	//printk("rd=%d, tmp=%d\n", hw->rd, tmp);
  	tmp = (tmp + INT_NUM + (hw->size>>6)) % (hw->size>>6);
  	WRITE_MPEG_REG_BITS(AIU_MEM_I2S_MASKS, tmp, 16, 16);
	
  	i2s_copy(amaudio);
  	
  	return IRQ_HANDLED;
}

//------------------------control interface-----------------------------------------

static long amaudio_ioctl(struct file *file,unsigned int cmd, unsigned long arg)
{
	amaudio_t * amaudio = (amaudio_t *)file->private_data;
	s32 r = 0;
	unsigned long swirqflags, hwirqflags, sw_readirqflags;
	switch(cmd){
		case AMAUDIO_IOC_GET_SIZE:		
			// total size of internal buffer
			r = amaudio->sw.size;
			break;
		case AMAUDIO_IOC_GET_PTR:
			// the read pointer of internal buffer
			spin_lock_irqsave(&amaudio->sw.lock, swirqflags);
			r = amaudio->sw.rd;
			spin_unlock_irqrestore(&amaudio->sw.lock, swirqflags);
			break;		
		case AMAUDIO_IOC_UPDATE_APP_PTR:
			// the user space write pointer of the internal buffer
			{
				unsigned int last_wr = amaudio->sw.wr;
				spin_lock_irqsave(&amaudio->sw.lock, swirqflags);
				amaudio->sw.wr = arg;
				amaudio->sw.level += (amaudio->sw.size + amaudio->sw.wr - last_wr)%amaudio->sw.size;
				spin_unlock_irqrestore(&amaudio->sw.lock, swirqflags);
				if(amaudio->sw.wr % 64){
					printk(KERN_WARNING "wr:%x, not 64 Bytes align\n", amaudio->sw.wr);
				}
			}
			break;
		case AMAUDIO_IOC_RESET:
			// reset the internal write buffer pointer to get a given latency
			// this api should be called before fill datas
			spin_lock_irqsave(&amaudio->hw.lock, hwirqflags);
			/*
			latency = arg;    //now can't allow user to set latency.
			if(latency < MIN_LATENCY)  latency = MIN_LATENCY;
			if(latency > MAX_LATENCY)  latency = MAX_LATENCY;
			if(latency%64)  latency = (latency >> 6) << 6;
			*/
			
			amaudio->hw.rd = -1;
			amaudio->hw.wr = -1;
			amaudio->hw.level = 0;
			spin_unlock_irqrestore(&amaudio->hw.lock, hwirqflags);
			// empty the buffer
			spin_lock_irqsave(&amaudio->sw.lock, swirqflags);
			amaudio->sw.wr = 0;
			amaudio->sw.rd = 0;
			amaudio->sw.level = 0;
			spin_unlock_irqrestore(&amaudio->sw.lock, swirqflags);
			// empty the out read buffer
			spin_lock_irqsave(&amaudio->sw_read.lock, sw_readirqflags);
			amaudio->sw_read.wr = 0;
			amaudio->sw_read.rd = 0;
			amaudio->sw_read.level = 0;
			spin_unlock_irqrestore(&amaudio->sw_read.lock, sw_readirqflags);
			
			printk(KERN_INFO "Reset amaudio2: latency=%d bytes\n", latency);
			break;
		case AMAUDIO_IOC_AUDIO_OUT_MODE:
			// audio_out_mode = 0, covered alsa audio mode; 
			// audio_out_mode = 1, karaOK mode, Linein left and right channel inter mixed with android alsa audio;
			// audio_out_mode = 2, TV in direct mix with android audio; 
			// audio_out_mode = 3, don't copy data to Hardware buffer 
			if(arg < 0 || arg > 3){
              return -EINVAL;
            }
            audio_out_mode = arg;
			break;
		case AMAUDIO_IOC_MIC_LEFT_GAIN:
			//in karaOK mode, mic volume can be set from 0-256
			if(arg < 0 || arg > 256){
              return -EINVAL;
            }
            direct_left_gain = arg;
			break;
		case AMAUDIO_IOC_MIC_RIGHT_GAIN:
			if(arg < 0 || arg > 256){
              return -EINVAL;
            }
            direct_right_gain = arg;
			break;
		case AMAUDIO_IOC_MUSIC_GAIN:
			//music volume can be set from 0-256
			if(arg < 0 || arg > 256){
              return -EINVAL;
            }
            music_gain = arg;
			break;
		case AMAUDIO_IOC_GET_PTR_READ:
			// the write pointer of internal read buffer
			spin_lock_irqsave(&amaudio->sw_read.lock, sw_readirqflags);
			r = amaudio->sw_read.wr;
			spin_unlock_irqrestore(&amaudio->sw_read.lock, sw_readirqflags);
			break;	
		case AMAUDIO_IOC_UPDATE_APP_PTR_READ:
			// the user space read pointer of the read buffer
			{
				unsigned int last_rd = amaudio->sw_read.rd;
				spin_lock_irqsave(&amaudio->sw_read.lock, sw_readirqflags);
				amaudio->sw_read.rd = arg;
				amaudio->sw_read.level += (amaudio->sw_read.size + amaudio->sw_read.rd - last_rd)
															%amaudio->sw_read.size;
				spin_unlock_irqrestore(&amaudio->sw_read.lock, sw_readirqflags);
				if(amaudio->sw_read.rd % 64){
					printk(KERN_WARNING "rd:%x, not 64 Bytes align\n", amaudio->sw_read.rd);
				}
			}
			break;
		case AMAUDIO_IOC_OUT_READ_ENABLE:
			//enable amaudio output read from hw buffer
			if(arg != 0 && arg != 1){
              return -EINVAL;
            }
            audio_out_read_enable = arg;
			break;
		default:
			break;
	};
	
	return r;
}

static long amaudio_utils_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	//amaudio_t * amaudio = (amaudio_t *)file->private_data;
	return 0;
}

#undef MEMCPY
static ssize_t status_show(struct class* class, struct class_attribute* attr,
    char* buf)
{
	int i=0;
	for(i=0;i<sizeof(amaudio_ports)/sizeof(amaudio_ports[0]);i++){
		amaudio_port_t* this = &amaudio_ports[i];
		amaudio_t* rt = (amaudio_t*)this->runtime;
		BUF hw, sw;
		unsigned long swflockflags, hwlockflags;
		if(rt == NULL) break;
		spin_lock_irqsave(&rt->sw.lock,swflockflags);
		memcpy(&sw, &rt->sw, sizeof(BUF));
		spin_unlock_irqrestore(&rt->sw.lock,swflockflags);
		
		spin_lock_irqsave(&rt->hw.lock,hwlockflags);
		memcpy(&hw, &rt->hw, sizeof(BUF));
		spin_unlock_irqrestore(&rt->sw.lock,hwlockflags);
		
		printk("HW:addr=%08x,size=%d,rd=%d,wr=%d,level=%d\n", 
			(unsigned int)hw.addr, hw.size, hw.rd, hw.wr, hw.level);
		printk("SW:addr=%08x,size=%d,rd=%d,wr=%d,level=%d\n", 
			(unsigned int)sw.addr, sw.size, sw.rd, sw.wr, sw.level);	
		printk("cnt: %d, %d, %d, %d, %d, %d, %d, %d\n", 
			rt->cnt0, rt->cnt1, rt->cnt2, rt->cnt3, rt->cnt4, rt->cnt5, rt->cnt6, rt->cnt7);			
	}
	return 0;
}

static ssize_t show_audio_out_mode(struct class* class, struct class_attribute* attr,
    char* buf)
{
	return sprintf(buf, "%d\n", audio_out_mode);
}

static ssize_t store_audio_out_mode(struct class* class, struct class_attribute* attr,
   const char* buf, size_t count )
{
	if(buf[0] == '0'){
		printk(KERN_INFO "Audio_in data covered the android local data as output!\n");
		audio_out_mode = 0;
	}else if(buf[0] == '1'){
		printk(KERN_INFO "Audio_in left/right channels and the android local data inter mixed as output!\n");
		audio_out_mode = 1;
	}else if(buf[0] == '2'){
		printk(KERN_INFO "Audio_in data direct mixed with the android local data as output!\n");
		audio_out_mode = 2;
	}else if(buf[0] == '3'){
		printk(KERN_INFO "Audio_in don't copy data to hardware buffer!\n");
		audio_out_mode = 3;
	}
	return count;
}

static ssize_t show_direct_left_gain(struct class* class, struct class_attribute* attr,
    char* buf)
{
	return sprintf(buf, "%d\n", direct_left_gain);
}

static ssize_t store_direct_left_gain(struct class* class, struct class_attribute* attr,
   const char* buf, size_t count )
{
	int val = 0;
  	if(buf[0])
  		val=simple_strtol(buf, NULL, 16);
	
  	if(val < 0) val = 0;
  	if(val > 256) val = 256;

  	direct_left_gain = val;
  	printk(KERN_INFO "direct_left_gain set to 0x%x\n", direct_left_gain);
  	return count;
}

static ssize_t show_direct_right_gain(struct class* class, struct class_attribute* attr,
    char* buf)
{
	return sprintf(buf, "%d\n", direct_right_gain);
}

static ssize_t store_direct_right_gain(struct class* class, struct class_attribute* attr,
   const char* buf, size_t count )
{
	int val = 0;
  	if(buf[0])
  		val=simple_strtol(buf, NULL, 16);
	
  	if(val < 0) val = 0;
  	if(val > 256) val = 256;

  	direct_right_gain = val;
  	printk(KERN_INFO "direct_right_gain set to 0x%x\n", direct_right_gain);
  	return count;
}

static ssize_t show_music_gain(struct class* class, struct class_attribute* attr,
    char* buf)
{
	return sprintf(buf, "%d\n", music_gain);
}

static ssize_t store_music_gain(struct class* class, struct class_attribute* attr,
   const char* buf, size_t count )
{
	int val = 0;
  	if(buf[0])
  		val=simple_strtol(buf, NULL, 16);
	
  	if(val < 0) val = 0;
  	if(val > 256) val = 256;

  	music_gain = val;
  	printk(KERN_INFO "music_gain set to 0x%x\n", music_gain);
  	return count;
}

static ssize_t show_audio_read_enable(struct class* class, struct class_attribute* attr,
    char* buf)
{
	return sprintf(buf, "%d\n", audio_out_read_enable);
}

static ssize_t store_audio_read_enable(struct class* class, struct class_attribute* attr,
   const char* buf, size_t count )
{
	if(buf[0] == '0'){
		printk(KERN_INFO "Read audio data is disable!\n");
		audio_out_read_enable = 0;
	}else if(buf[0] == '1'){
		printk(KERN_INFO "Read audio data is enable!\n");
		audio_out_read_enable = 1;
	}else{
		printk(KERN_INFO "Invalid argument!\n");
	}
  	return count;
}

static struct class_attribute amaudio_attrs[]={
	__ATTR(aml_audio_out_mode,  S_IRUGO | S_IWUSR, show_audio_out_mode, store_audio_out_mode),
	__ATTR(aml_direct_left_gain,  S_IRUGO | S_IWUSR, show_direct_left_gain, store_direct_left_gain),
	__ATTR(aml_direct_right_gain,  S_IRUGO | S_IWUSR, show_direct_right_gain, store_direct_right_gain),
	__ATTR(aml_music_gain,  S_IRUGO | S_IWUSR, show_music_gain, store_music_gain),
	__ATTR(aml_audio_read_enable,  S_IRUGO | S_IWUSR, show_audio_read_enable, store_audio_read_enable),
	__ATTR_RO(status),
	__ATTR_NULL
};

static void create_amaudio_attrs(struct class* class)
{
  int i=0;
  for(i=0; amaudio_attrs[i].attr.name; i++){
    if(class_create_file(class, &amaudio_attrs[i]) < 0)
      break;
  }
}

static void remove_amaudio_attrs(struct class* class)
{
  int i=0;
  for(i=0; amaudio_attrs[i].attr.name; i++){
    class_remove_file(class, &amaudio_attrs[i]);
  }
}

static int __init amaudio2_init(void)
{
	int ret = 0;
  int i=0;
  amaudio_port_t* ap;

  ret = alloc_chrdev_region(&amaudio_devno, 0, AMAUDIO_DEVICE_COUNT, AMAUDIO_DEVICE_NAME);
  if(ret < 0){
    printk(KERN_ERR "amaudio: faild to alloc major number\n");
    ret = - ENODEV;
    goto err;
  }
  amaudio_clsp = class_create(THIS_MODULE, AMAUDIO_CLASS_NAME);
  if(IS_ERR(amaudio_clsp)){
    ret = PTR_ERR(amaudio_clsp);
    goto err1;
  }
  
  create_amaudio_attrs(amaudio_clsp);
  
  amaudio_cdevp = kmalloc(sizeof(struct cdev), GFP_KERNEL);
  if(!amaudio_cdevp){
    printk(KERN_ERR "amaudio: failed to allocate memory\n");
    ret = -ENOMEM;
    goto err2;
  }
  // connect the file operation with cdev
  cdev_init(amaudio_cdevp, &amaudio_fops);
  amaudio_cdevp->owner = THIS_MODULE;
  // connect the major/minor number to cdev
  ret = cdev_add(amaudio_cdevp, amaudio_devno, AMAUDIO_DEVICE_COUNT);
  if(ret){
    printk(KERN_ERR "amaudio:failed to add cdev\n");
    goto err3;
  } 
  for(ap = &amaudio_ports[0], i=0; i< AMAUDIO_DEVICE_COUNT; ap++,  i++){    
    ap->dev = device_create(amaudio_clsp, NULL, MKDEV(MAJOR(amaudio_devno),i), NULL,amaudio_ports[i].name);
    if(IS_ERR(ap->dev)){
      printk(KERN_ERR "amaudio: failed to create amaudio device node\n");
      goto err4;
    }
  }

  printk(KERN_INFO "amaudio: device %s created\n", AMAUDIO_DEVICE_NAME);
  return 0;

err4:
  cdev_del(amaudio_cdevp);
err3:
  kfree(amaudio_cdevp);
err2:
  remove_amaudio_attrs(amaudio_clsp);
  class_destroy(amaudio_clsp);  
err1:
  unregister_chrdev_region(amaudio_devno, AMAUDIO_DEVICE_COUNT);
err:
	
  return ret;  
}

static void __exit amaudio2_exit(void)
{
	int i=0;
  unregister_chrdev_region(amaudio_devno, 1);
  for(i=0; i< AMAUDIO_DEVICE_COUNT; i++){
    device_destroy(amaudio_clsp, MKDEV(MAJOR(amaudio_devno),i));
  }
  cdev_del(amaudio_cdevp);
  kfree(amaudio_cdevp);
  remove_amaudio_attrs(amaudio_clsp);
  class_destroy(amaudio_clsp);
  return;
}

module_init(amaudio2_init);
module_exit(amaudio2_exit);
